#include "patchxfer.h"
#include "patchxferhttp.h"
#include "patchfilespec.h"
#include "patchdb.h"
#include "pcl_client_internal.h"
#include "bindiff.h"
#include "fileWatch.h"
#include "utils.h"
#include "patchcommonutils.h"
#include "hoglib.h"
#include "piglib.h"
#include "piglib_internal.h"
#include "earray.h"
#include "pcl_client_struct.h"
#include "timing.h"
#include "../net/net.h"
#include "trivia.h"
#include "estring.h"
#include "zutils.h"
#include "logging.h"
#include "pcl_client.h"  // for internal function pclReportResetAfterHang(), maybe should be separate internal header
#include "wininclude.h"
#include "MemAlloc.h"
#include "MemTrack.h"

AUTO_RUN_ANON(memBudgetAddMapping(__FILE__, BUDGET_FileSystem););

#define LOGPACKET(client, fmt, ...) pclMSpf(fmt, __VA_ARGS__) // pcllog(client, PCLLOG_PACKET, fmt, __VA_ARGS__)

// Retry this many times trying to make bindiffing work.  Bindiffing is deterministic, but each subsequent time uses less granular fingerprints, which hopefully
// will not fail in the same way.  Note that it is not safe the broken data because it might not match the checksum it is supposed to match, so this can actually
// corrupt the hogg; it can also cause immediate failures in hoglib if it needs to be uncompressed to be written, in the case of files that are always stored
// uncompressed.
#define RETRIES_BEFORE_GET_WHOLE_FILE 1

#define MIN_FILE_SIZE_FOR_DIFF 8192	// according to measurement, bindiffing doesn't accomplish anything on small zipped files (the zip compressor state is reset every 4k of input data)
#define XFERRER_HANG_WARNING			30	// warn every x seconds
#define XFERRER_HANG_RESET				10	// reset every x warnings
#define XFERRER_DIFF_COMPRESSED true // allow diffing on compressed data?

// Address space limitation guard: Do everything possible to avoid allocating a memory chunk larger than this size.
#ifdef _M_X64
#define XFERRER_MEMORY_SIZE_CUTOFF 2*1024*1024*1024U
#else
#define XFERRER_MEMORY_SIZE_CUTOFF 512*1024*1024
#endif

// TODO: This mechanism is disabled, because HTTP always transfers files disabled, and with the rsyncable compression fixes, it isn't really that bad.
// However, it might be revived if there's a good reason, so this is going to stay here for now.
static char *g_force_uncompressed_xfer[] = {"info", "bin", "exe"};

typedef struct {
	PatchXfer *xfer;
	PCL_Client *client;
	char *file_path;
	U8 *data;
	U32 len;
	U32 timestamp;
	U32 crc;
	bool use_mutex;
	PCL_FileFlags flags;
	U32 compressed_unc_len;
} xferWriteFileArgs;

void xferFree(PatchXferrer *xferrer, PatchXfer *xfer);
static void xferCallback(PatchXferrer *xferrer, PatchXfer *xfer, PCL_ErrorCode error, const char *error_details);

const char* xferStateGetName(XferState state)
{
	switch(state)
	{
		#define CASE(x) case x:return #x + 5;
		CASE(XFER_REQ_VIEW);
		CASE(XFER_WAIT_VIEW);
		CASE(XFER_REQ_FILEINFO);
		CASE(XFER_WAIT_FILEINFO);
		CASE(XFER_REQ_FINGERPRINTS);
		CASE(XFER_WAIT_FINGERPRINTS);
		CASE(XFER_REQ_DATA);
		CASE(XFER_WAIT_DATA);
		CASE(XFER_REQ_WRITE);
		CASE(XFER_WAIT_WRITE);
		CASE(XFER_COMPLETE);
		CASE(XFER_COMPLETE_NOCHANGE);
		CASE(XFER_FILENOTFOUND);
		#undef CASE
		default:{
			if(state >= 0 && state < XFER_STATE_COUNT){
				return "NAME NOT SET!!!!";
			}else{
				return "UNKNOWN!!!!!!!!!";
			}
		}
	}
}

// Switch an xfer to a new state
static __forceinline void xferSetState(PatchXfer *xfer, XferState state)
{
	if (xfer->state != state)
	{
		xfer->state = state;
#ifdef DEBUG_PCL_TIMING
		xfer->entered_state = timerCpuTicks64();
#endif
	}
}

// Return true if this state or its corresponding wait state involves waiting on the server before proceeding.
static bool xferStateWaitingOnServer(XferState state)
{
	switch (state)
	{
		case XFER_REQ_WRITE:
		case XFER_WAIT_WRITE:
			return false;
	}
	return true;
}

static bool xferIsForceUncompressed(PatchXfer *xfer)
{
	int i;
	char *extension = strrchr(xfer->filename_to_write, '.');
	if(!extension)
		return false;
	for(i=ARRAY_SIZE(g_force_uncompressed_xfer)-1; i>=0; i--)
	{
		if(stricmp(extension, g_force_uncompressed_xfer[i])==0)
			return true;
	}
	return false;
}

// Return true if an xfer is for a manifest.
static bool xferIsManifest(const PatchXfer *xfer)
{
	return !!strEndsWith(xfer->filename_to_get, ".manifest");
}

// Return true if an xfer is for a filespec.
static bool xferIsFilespec(const PatchXfer *xfer)
{
	return !!strEndsWith(xfer->filename_to_get, ".filespec");
}

#define FILE_INFO_BYTES	100 // arbitrary size to limit sending infinite file info requests

// Get information about a data.
static XferStateInfo *xferGetStateInfo(PatchXfer * xfer)
{
	XferStateInfo *info = calloc(1, sizeof(XferStateInfo));
	info->filename = xfer->filename_to_write;
	info->state = xferGetState(xfer);
	info->bytes_requested = xfer->bytes_requested;
	info->start_ticks = xfer->start;
	info->block_size = (xfer->get_header ? HEADER_BLOCK_SIZE : (xfer->print_sizes ? xfer->print_sizes[xfer->print_idx] : 0));
	info->blocks_so_far = xfer->blocks_so_far;
	info->blocks_total = xfer->blocks_total;
	info->cum_bytes_requested = xfer->cum_bytes_requested;
	info->total_bytes_sent = xfer->total_bytes_sent;
	info->total_bytes_received = xfer->total_bytes_received;
	info->filedata_size = xfer->get_compressed ? xfer->com_len : xfer->unc_len;
	return info;
}

void xferrerGetStateInfo(PatchXferrer * xferrer, XferStateInfo *** state_info)
{
	int i;

	PERFINFO_AUTO_START_FUNC();

	for(i = eaSize(&xferrer->xfers) - 1; i >= 0; i--)
	{
		PatchXfer * xfer = xferrer->xfers[i];
		eaPush(state_info, xferGetStateInfo(xfer));
	}

	PERFINFO_AUTO_STOP_FUNC();
}

bool fileTooDifferent(PatchXfer *xfer)
{
	U32 new_len = xfer->get_compressed ? xfer->com_len : xfer->unc_len;
	return xfer->old_len > new_len * 2
		|| new_len > xfer->old_len * 2;
}

PatchXferrer * xferrerInit(PCL_Client * client)
{
	PatchXferrer * xferrer = calloc(sizeof(*xferrer), 1);
	
	xferrer->client = client;
	xferrer->comm = client->comm;
	xferrer->max_net_bytes = DEFAULT_MAX_NET_BYTES;
	xferrer->net_bytes_free = xferrer->max_net_bytes;
	xferrer->timestamp = timerCpuSeconds();
	xferrer->max_mem_usage = MAX_XFER_MEMORY_USAGE_DEFAULT;

	return xferrer;
}

static U32 getPatchFileSizeFromManifest(PatchXferrer * xferrer, const char *fname)
{
	DirEntry *pEntry = patchFindPath(xferrer->client->db, fname, 0);
	if(pEntry)
	{
		if(pEntry->versions)
		{
			return pEntry->versions[0]->size;
		}
	}

	return 0;
}

int cmpXferPriority(const PatchXfer **a, const PatchXfer **b)
{
	int diff;
	assert(a && *a);
	assert(b && *b);
	diff = (*a)->priority - (*b)->priority;
	if(diff)
		return diff;
	else if((*a)->timestamp < (*b)->timestamp)
		return 1;
	else if((*a)->timestamp > (*b)->timestamp)
		return -1;
	else
		return 0;
}

// Account additional memory to an xfer.
static void xferrerAddMemUsage(PatchXferrer * xferrer, PatchXfer * xfer, S32 size)
{
	xfer->mem_usage += size;
	xferrer->current_mem_usage += size;

	pclMSpf("Adding %d bytes=%"FORM_LL"u (%s)", size, xferrer->current_mem_usage, xfer->filename_to_get);
}

// Guess how much memory an xfer will use.
static void xferrerEstimateMemUsage(PatchXferrer * xferrer, PatchXfer * xfer)
{
	devassert(!xfer->mem_usage);
	xferrerAddMemUsage(xferrer, xfer, getPatchFileSizeFromManifest(xferrer, xfer->filename_to_get));
}

// Calculate how much memory an xfer is currently using.
static void xferrerCalculateMemUsage(PatchXferrer * xferrer, PatchXfer * xfer)
{
	S64 size = 0;

	devassert(!xfer->mem_usage);

	// Base xfer.
	size = sizeof(xfer) + sizeof(*xfer);

	// File data
	if (xfer->new_data)
		size += xfer->get_compressed ? xfer->com_block_len : xfer->unc_block_len;

	// Bindiffing memory
	if (xfer->old_data)
		size += xfer->old_block_len;
	if (xfer->old_copied)
		size += xfer->old_copied_len;
	if (xfer->fingerprints)
		size += xfer->fingerprints_len;
	if (xfer->scoreboard)
		size += xfer->scoreboard_len;

	// Add this memory.
	xferrerAddMemUsage(xferrer, xfer, getPatchFileSizeFromManifest(xferrer, xfer->filename_to_get));
}

// Reset the amount of memory accounted to this xfer to zero.
static void xferrerRemoveMemUsage(PatchXferrer * xferrer, PatchXfer * xfer)
{
	xferrer->current_mem_usage -= xfer->mem_usage;
	xfer->mem_usage = 0;

	pclMSpf("Removing %d bytes=%"FORM_LL"u (%s)", xfer->mem_usage, xferrer->current_mem_usage, xfer->filename_to_get);
}

static void xferAdd(PatchXferrer * xferrer, PatchXfer * xfer)
{
	xferrerEstimateMemUsage(xferrer, xfer);

	eaInsert(&xferrer->xfers, xfer, (int) eaBFind(xferrer->xfers, cmpXferPriority, xfer));
	xferrer->next_xfer = eaSize(&xferrer->xfers) - 1;
	eaPush(&xferrer->xfers_order, xfer);
}

static void xferRemove(PatchXferrer * xferrer, PatchXfer * xfer)
{
	xferrerRemoveMemUsage(xferrer, xfer);
	xferrer->net_bytes_free += (xfer->bytes_requested - xfer->http_bytes_requested);
	xferHttpRemove(xferrer, xfer);

	eaFindAndRemove(&xferrer->xfers, xfer);
	eaFindAndRemove(&xferrer->xfers_order, xfer);
}

void xferrerReset(PatchXferrer * xferrer)
{
	int i;

	PERFINFO_AUTO_START_FUNC();

	xferrer->has_ever_reset = true;

	xferrer->net_bytes_free = xferrer->max_net_bytes;

	for(i = 0; i < eaSize(&xferrer->xfers); i++)
	{
		PatchXfer * xfer = xferrer->xfers[i];
		xferSetState(xfer, XFER_RESTART);
		if (xfer->used_http)
		{
			xfer->use_http = false;
			++xfer->http_fail;
		}
		xfer->print_bytes_requested = 0;
		xfer->bytes_requested = 0;
		xfer->http_bytes_requested = 0;
	}

	EARRAY_FOREACH_REVERSE_BEGIN(xferrer->http_connections, j);
	{
		xferHttpDestroyConnection(xferrer, xferrer->http_connections[j]);
	}
	EARRAY_FOREACH_END;

	PERFINFO_AUTO_STOP_FUNC();
}

void xferrerDestroy(PatchXferrer **xferrer)
{
	int i, n;

	PERFINFO_AUTO_START_FUNC();

	if(!xferrer || !*xferrer)
	{
		PERFINFO_AUTO_STOP_FUNC();
		return;
	}

	if(n = eaSize(&(*xferrer)->big_files)) // assignment
	{
		PCL_Client *client = (*xferrer)->client;
		pcllog(client, PCLLOG_FILEONLY, "Warning: The following files were over %2.2fMB in size.", (F32)(*xferrer)->max_mem_usage/(1024*1024));
		for(i = 0; i < n; ++i)
			pcllog(client, PCLLOG_FILEONLY, "\t%s", (*xferrer)->big_files[i]);
		pcllog(client, PCLLOG_FILEONLY, "");
	}

	EARRAY_FOREACH_REVERSE_BEGIN((*xferrer)->http_connections, j);
	{
		xferHttpDestroyConnection(*xferrer, (*xferrer)->http_connections[j]);
	}
	EARRAY_FOREACH_END;

	EARRAY_CONST_FOREACH_BEGIN((*xferrer)->xfers, j, m);
	{
		xferFree(*xferrer, (*xferrer)->xfers[j]);
	}
	EARRAY_FOREACH_END;
	eaDestroy(&(*xferrer)->xfers);
	eaDestroy(&(*xferrer)->xfers_order);
	eaDestroyEx(&(*xferrer)->big_files, NULL);
	free((*xferrer)->http_server);
	free((*xferrer)->path_prefix);
	free(*xferrer);
	*xferrer = NULL;

	PERFINFO_AUTO_STOP_FUNC();
}

bool verifynetbytes(PatchXferrer * xferrer)
{
	int		i;
	int		net_bytes = xferrer->max_net_bytes;

	for(i=eaSize(&xferrer->xfers)-1;i>=0;i--)
		net_bytes -= xferrer->xfers[i]->bytes_requested - xferrer->xfers[i]->http_bytes_requested;
	if (net_bytes != xferrer->net_bytes_free)
	{
		pcllog(xferrer->client, PCLLOG_SPAM, "net_bytes != net_bytes_free");
		ErrorDetailsf("max_net_bytes %lu net_bytes %lu net_bytes_free %lu",
			xferrer->max_net_bytes, net_bytes, xferrer->net_bytes_free);
		Errorf("net_bytes != net_bytes_free");
	}
	return net_bytes == xferrer->net_bytes_free;
}

bool xferOkToRequestBytes(PatchXferrer * xferrer, PatchXfer *xfer, int amount)
{
	U32	last_net_bytes_free = xferrer->net_bytes_free;
	U32	last_bytes_requested = xfer->bytes_requested;
	int verifyret;

	verifyret = verifynetbytes(xferrer);
	assert(verifyret);
	if ((int)xferrer->net_bytes_free < amount)
		return false;
	xfer->bytes_requested += amount;
	xferrer->net_bytes_free -= amount;
	if (amount > 0)
		xfer->cum_bytes_requested += amount;
	verifyret = verifynetbytes(xferrer);
	assert(verifyret);
	return true;
}

void reqFileInfo(PatchXferrer * xferrer, PatchXfer *xfer)
{
	Packet	*pak;
	HogFileIndex hfi;
	
	// Try to take an early out if the file is in a hogg and differs only by timestamp.
	if(xfer->write_hogg && xfer->ver)
	{
		hfi = hogFileFind(xfer->write_hogg, xfer->filename_to_write);
		if(hfi != HOG_INVALID_INDEX && 
		   xfer->ver->size == hogFileGetFileSize(xfer->write_hogg, hfi) &&
		   xfer->ver->checksum == hogFileGetFileChecksum(xfer->write_hogg, hfi))
		{
			// File is in a hogg and is identical other than timestamp
			// Adjust the timestamp to match the manifest and move on
			// JE: This can happen if a file is up to date but not properly mirrored as well!
			hogFileModifyUpdateTimestamp(xfer->write_hogg, hfi, xfer->ver->modified);
			xferSetState(xfer, XFER_COMPLETE);
			return;
		}
	}
	else if (!xfer->write_hogg && xfer->ver)
	{
		if (xfer->filename_to_write &&
			*xfer->filename_to_write &&
			xfer->ver->size == fileSize(xfer->filename_to_write) &&
			xfer->ver->checksum == patchChecksumFile(xfer->filename_to_write))
		{
			// File is on disk and is identical other than timestamp
			// Adjust the timestamp to match the manifest and move on
			fileSetTimestamp(xfer->filename_to_write, xfer->ver->modified);
			xferSetState(xfer, XFER_COMPLETE);
			return;
		}
	}

	// Can we find this fileversion in one of the overlay hoggs?
	if(xferrer->client->filespec && xfer->ver)
	{
		char filepath[MAX_PATH] = "";
		U32 checksum;
		//Warning: If fileOverlay is turned on, this function performs a checksum while looking for the appropriate file, which will cause a stall. (TODO: Fix this)
		HogFile *hogg = fileSpecGetHoggHandleForFileVersionEx(xferrer->client->filespec, xfer->ver, false, xferrer->client->useFileOverlay, &hfi, &filepath, &checksum);
		if(hogg)
		{
			U32 count;
			// Found it!
			xfer->file_time = hogFileGetFileTimestamp(hogg, hfi);

			// Check if it is already in the correct hogg
			if(hogg == xfer->write_hogg)
			{
				xferSetState(xfer, XFER_COMPLETE_NOCHANGE);
				return;
			}

			xfer->unc_crc = hogFileGetFileChecksum(hogg, hfi);
			hogFileGetSizes(hogg, hfi, &xfer->unc_len, &xfer->com_len);
			if(xfer->com_len)
			{
				// TODO: use fileLoader
				xfer->new_data = hogFileExtractCompressed(hogg, hfi, &count);
				//assert(xfer->new_data); TODO: handle this case: the overlay hog is corrupted, or something
				// shouldn't hoglib be able to deal with on its own though?
				assert(xfer->com_len == count);
				xfer->get_compressed = true;
			}
			else
			{
				// TODO: use fileLoader

				xfer->new_data = hogFileExtract(hogg, hfi, &count, NULL);
				//assert(xfer->new_data); TODO: handle this case: the overlay hog is corrupted, or something
				// shouldn't hoglib be able to deal with on its own though?
				assert(xfer->unc_len == count);
				xfer->get_compressed = false;
			}
			xferrer->overlay_bytes += count;
			xfer->skip_final_checksum = true; // Bypass the final checksum
			xferSetState(xfer, XFER_REQ_WRITE);
			xferrerRemoveMemUsage(xferrer, xfer);
			xferrerCalculateMemUsage(xferrer, xfer);
			return;
		}
		//TODO: We don't want to check files on disk if we're writing to a hogg, as we won't have a compressed length and therefore will not be able to compress it
		//even if the server wants to. This could possibly be properly checked and implemented later.
		else if (*filepath && !xfer->write_hogg) 
		{
			U32 count;
			// Found it!
			xfer->file_time = fileLastChangedAbsolute(filepath);

			// Check if it is already in the correct place
			if(!strcmpi(filepath, NULL_TO_EMPTY(xfer->filename_to_write)))
			{
				xferSetState(xfer, XFER_COMPLETE_NOCHANGE);
				return;
			}

			xfer->new_data = fileAlloc(filepath, &count);
			//It is possible to get a filepath without a checksum if you are allowing inexact matches. However, since we are not, this checksum is guaranteed to have a value.
			xfer->unc_crc = checksum;
			xfer->get_compressed = false;
			xfer->unc_len = count;

			xferrer->overlay_bytes += count;
			xfer->skip_final_checksum = true; // Bypass the final checksum
			xferSetState(xfer, XFER_REQ_WRITE);
			xferrerRemoveMemUsage(xferrer, xfer);
			xferrerCalculateMemUsage(xferrer, xfer);
			return;
		}
	}

	if (!xferOkToRequestBytes(xferrer, xfer, FILE_INFO_BYTES))
		return;

	pak = pktCreate(xferrer->client->link, PATCHCLIENT_REQ_FILEINFO);
	pktSendString(pak,xfer->filename_to_get);
	pktSendBits(pak,32,xfer->id);
	xfer->total_bytes_sent += pktGetSize(pak);
	pktSend(&pak);
	LOGPACKET(xferrer->client, "PATCHCLIENT_REQ_FILEINFO %s %i", xfer->filename_to_get, xfer->id);
	xferSetState(xfer, XFER_WAIT_FILEINFO);
}

void reqHeaderInfo(PatchXferrer * xferrer, PatchXfer * xfer)
{
	Packet * pak;

	if (!xferOkToRequestBytes(xferrer, xfer, FILE_INFO_BYTES))
		return;

	pak = pktCreate(xferrer->client->link, PATCHCLIENT_REQ_HEADERINFO);
	pktSendString(pak, xfer->filename_to_get);
	pktSendBits(pak, 32, xfer->id);
	xfer->total_bytes_sent += pktGetSize(pak);
	pktSend(&pak);
	LOGPACKET(xferrer->client, "PATCHCLIENT_REQ_HEADERINFO %s %i\n", xfer->filename_to_get, xfer->id);
	xferSetState(xfer, XFER_WAIT_FILEINFO);
}

void reqEntireFile(PatchXferrer * xferrer, PatchXfer * xfer)
{
	U32 block_size = xfer->print_sizes[xfer->num_print_sizes - 1];
	U32 new_len = xfer->get_compressed ? xfer->com_len : xfer->unc_len;
	int i;
	U32 num_blocks = (new_len + block_size - 1) / block_size;
	int blocks_per_req = MAX_SINGLE_REQUEST_BYTES / block_size;

	xfer->print_idx = xfer->num_print_sizes - 1;
	xfer->curr_req_idx = 0;
	xfer->num_block_reqs = (num_blocks * block_size + MAX_SINGLE_REQUEST_BYTES-1) / MAX_SINGLE_REQUEST_BYTES;
	SAFE_FREE(xfer->block_reqs);
	xfer->block_reqs = malloc(sizeof(xfer->block_reqs[0]) * 2 * xfer->num_block_reqs);

	xfer->blocks_so_far = 0;
	xfer->blocks_total = 0;

	for(i = 0; i < xfer->num_block_reqs; i++)
	{
		int	blocks = blocks_per_req;

		xfer->block_reqs[i*2+0] = i * blocks_per_req;
		if (i == xfer->num_block_reqs - 1 && num_blocks % blocks)
			blocks = num_blocks % blocks;
		xfer->block_reqs[i*2+1] = blocks;
		xfer->blocks_total += blocks;
		assert(xfer->block_reqs[i*2+0] >= 0 && xfer->block_reqs[i*2+1] >= 0);
	}

	assert(xfer->num_block_reqs > 0);
	xferSetState(xfer, XFER_REQ_DATA);
}

static void receiveHeaderInfo(PatchXferrer * xferrer, Packet *pak,PatchXfer *xfer)
{
	HogFileIndex hfi;

	xfer->unc_len = pktGetBits(pak, 32);
	xfer->unc_crc = pktGetBits(pak, 32);
	
	if(xfer->unc_len == 0)
	{
		xferSetState(xfer, XFER_COMPLETE);
		xfer->new_data = NULL;
	}
	else if(xfer->unc_len == (U32)0 - 1 && xfer->unc_crc == 0)
	{
		xferSetState(xfer, XFER_FILENOTFOUND);
		xfer->unc_len = 0;
		xfer->new_data = NULL;
	}
	else
	{
		xfer->unc_block_len = (xfer->unc_len + HEADER_BLOCK_SIZE - 1) & ~(HEADER_BLOCK_SIZE - 1);
		hfi = hogFileFind(xfer->read_hogg, xfer->filename_to_write);
		if(hfi != HOG_INVALID_INDEX)
		{
			// TODO: use fileLoader
			xfer->old_data = hogFileExtract(xfer->read_hogg, hfi, &xfer->old_len, NULL);
		}

		if(xfer->old_len == xfer->unc_len && xfer->unc_crc == patchChecksum(xfer->old_data, xfer->old_len))
		{
			xferSetState(xfer, XFER_COMPLETE);
			xfer->new_data = xfer->old_data;
			xfer->old_data = NULL;
		}
		else
		{
			int i;
			U32 num_blocks = (xfer->unc_len + HEADER_BLOCK_SIZE - 1) / HEADER_BLOCK_SIZE;
			U32 blocks_per_req = MAX_SINGLE_REQUEST_BYTES / HEADER_BLOCK_SIZE;

			xfer->curr_req_idx = 0;
			xfer->num_block_reqs = (num_blocks * HEADER_BLOCK_SIZE + MAX_SINGLE_REQUEST_BYTES-1) / MAX_SINGLE_REQUEST_BYTES;
			SAFE_FREE(xfer->block_reqs);
			xfer->block_reqs = malloc(sizeof(xfer->block_reqs[0]) * 2 * xfer->num_block_reqs);
			xfer->new_data = malloc(xfer->unc_block_len);

			xfer->blocks_total = 0;
			xfer->blocks_so_far = 0;

			for(i = 0; i < xfer->num_block_reqs; i++)
			{
				U32	blocks = blocks_per_req;

				xfer->block_reqs[i*2+0] = i * blocks_per_req;
				if (i == xfer->num_block_reqs - 1 && num_blocks % blocks)
					blocks = num_blocks % blocks;
				xfer->block_reqs[i*2+1] = blocks;
				xfer->blocks_total += blocks;
			}

			xferSetState(xfer, XFER_REQ_DATA);
		}
	}

	xferOkToRequestBytes(xferrer, xfer, -FILE_INFO_BYTES);
	SAFE_FREE(xfer->old_data);
}

// This function makes the important decision of whether to get the whole file, diff the file, or don't get the file,
// and whether to act on compressed or uncompressed data.  It loads the old data for diffing.
static void receiveFileInfo(PatchXferrer * xferrer, Packet *pak, PatchXfer *xfer)
{
	U32 i, file_time;
	char *slipstream_data = NULL;
	bool compressed_data_available = true;
	size_t new_data_size;

	xferOkToRequestBytes(xferrer, xfer, -FILE_INFO_BYTES);

	xfer->unc_len				= pktGetBits(pak,32);
	xfer->com_len				= pktGetBits(pak,32); // this order is for legacy
	xfer->unc_crc				= pktGetBits(pak,32);

	// Check if this file is actually on the server
	if(xfer->unc_len == (U32)0 - 1 && xfer->unc_crc == 0)
	{
		xferSetState(xfer, XFER_FILENOTFOUND);
		xfer->unc_len = 0;
		return;
	}

	xfer->unc_num_print_sizes	= pktGetBits(pak,32);
	xfer->unc_print_sizes		= malloc(xfer->unc_num_print_sizes * sizeof(xfer->unc_print_sizes[0]));
	for(i = 0; i < xfer->unc_num_print_sizes; i++)
		xfer->unc_print_sizes[i]= pktGetBits(pak,32);
	xfer->unc_block_len = (xfer->unc_len + xfer->unc_print_sizes[0]-1) & ~(xfer->unc_print_sizes[0]-1);

	if(xfer->com_len)
	{
		xfer->com_crc				= pktGetBits(pak,32);
		xfer->com_num_print_sizes	= pktGetBits(pak,32);
	}
	else
	{
		xfer->com_crc = 0;
		xfer->com_num_print_sizes = 0;
	}
	if(xfer->com_num_print_sizes)
	{
		xfer->com_print_sizes		= malloc(xfer->com_num_print_sizes * sizeof(xfer->com_print_sizes[0]));
		for(i = 0; i < xfer->com_num_print_sizes; i++)
			xfer->com_print_sizes[i]= pktGetBits(pak,32);
		xfer->com_block_len = (xfer->com_len + xfer->com_print_sizes[0]-1) & ~(xfer->com_print_sizes[0]-1);
	}
	else
	{
		xfer->com_print_sizes = NULL;
		xfer->com_block_len = 0;
		compressed_data_available = false;
	}

	file_time = pktGetBits(pak,32);
	if(xfer->file_time && xfer->file_time != file_time && !xferIsManifest(xfer))
		pcllog(xferrer->client, PCLLOG_WARNING, "%s has manifest timestamp %d, but the server gave timestamp %d.  Keeping manifest time.",xfer->filename_to_write,xfer->file_time,file_time);
	else
		xfer->file_time = file_time;

	// Stop here on empty file.
	if(xfer->unc_len == 0)
	{
		assert(xfer->unc_crc == 0);
		SAFE_FREE(xfer->unc_print_sizes);
		xfer->get_compressed = false;
		xfer->new_data = malloc(0);
		xferSetState(xfer, XFER_REQ_WRITE);
		xfer->file_time = file_time;
		return;
	}

	xfer->slipstream_available = false;
	if(!pktEnd(pak) && pktGetBool(pak))
	{
		assertmsg(!pktEnd(pak), "Can't find slipstream data");
		slipstream_data = pktGetBytesTemp(pak, xfer->com_len);
		assertmsg(slipstream_data, "Not enough bytes for the slipstream data");
		xfer->slipstream_available = true;
	}

	// Get revision from packet, if available.
	if (!pktEnd(pak))
		xfer->actual_rev = pktGetBits(pak,32);

	// Done getting from pak

	xfer->get_compressed = ((xfer->retries <= RETRIES_BEFORE_GET_WHOLE_FILE && xfer->com_len /* && !xferIsForceUncompressed(xfer) */ )
		|| xferHttpShouldUseHttp(xferrer, xfer)
		|| (xfer->file_flags & PCL_XFER_COMPRESSED) && xfer->actual_rev>=0
		|| slipstream_data)
		&& compressed_data_available;
	xfer->num_print_sizes = 0;
	xfer->print_sizes = NULL;

	// Address space limitation guard: Don't allow files larger than a certain size to be compressed.
	if (!slipstream_data && xfer->unc_len > XFERRER_MEMORY_SIZE_CUTOFF)
	{
		xfer->get_compressed = false;
		xfer->com_len = 0;
		xfer->com_block_len = 0;
		xfer->com_crc = 0;
		xfer->com_num_print_sizes = 0;
		xfer->com_print_sizes = NULL;
	}

	// If bin-skipping is on and the file is a bin, check the timestamps
	if(xferrer->client->file_flags & PCL_SKIP_BINS && xferrer->client->filespec && fileSpecIsBin(xferrer->client->filespec, xfer->filename_to_get))
	{
		char filepath[MAX_PATH];
		U32 filetime;
		sprintf(filepath, "%s/data/%s", xferrer->client->root_folder, xfer->filename_to_write);
		filetime = (U32)fileLastChangedAbsolute(filepath);
		if(filetime > xfer->file_time)
		{
			//printf("Skipping bin file %s due to local changes\n", xfer->filename_to_get);
			xferSetState(xfer, XFER_COMPLETE_IGNORED);
			return;
		}
	}

	// Determine if we want to do diffing or just get everything
	PERFINFO_AUTO_START("SetupBindiffing", 1);
	xfer->old_data = NULL; // get the entire file

	// TODO: Use fileSpecGetHoggHandleForFileVersionEx() to try to bindiff off an overlay hogg?

	if(	!(xfer->file_flags & PCL_DISABLE_BINDIFF) &&
		xfer->retries <= RETRIES_BEFORE_GET_WHOLE_FILE &&
		xfer->unc_len >= MIN_FILE_SIZE_FOR_DIFF &&
		!slipstream_data &&
		!(xferrer->client->file_flags & PCL_IN_MEMORY)
		&& !(xferIsManifest(xfer) && (xferrer->client->file_flags & PCL_METADATA_IN_MEMORY))
		&& xfer->unc_len < XFERRER_MEMORY_SIZE_CUTOFF)
	{
		if(xfer->read_hogg)
		{
			HogFileIndex hfi = hogFileFind(xfer->read_hogg, xfer->filename_to_write);
			if(hfi != HOG_INVALID_INDEX)
			{
				if(xfer->get_compressed && XFERRER_DIFF_COMPRESSED)
				{
					// TODO: use fileLoader
					xfer->old_data = hogFileExtractCompressed(xfer->read_hogg, hfi, &xfer->old_len);
				}
				if(xfer->old_data == NULL)
				{
					// TODO: use fileLoader
					xfer->old_data = hogFileExtract(xfer->read_hogg, hfi, &xfer->old_len, NULL);
					if(xfer->old_data != NULL && xfer->get_compressed)
					{
						// The existing file is stored uncompressed, but we're otherwise good to get compressed data.
						// Choose whether to fall back to getting uncompressed data, or synthesizing compressed data.
						if (!XFERRER_DIFF_COMPRESSED
							|| !xferrer->compress_as_server && pigShouldBeUncompressed(strrchr(xfer->filename_to_write,'.')) && !xferHttpShouldUseHttp(xferrer, xfer)
								&& !(xfer->file_flags & PCL_XFER_COMPRESSED))
							xfer->get_compressed = false;

						else
						{
							char *uncompressed = xfer->old_data;
							U32 uncompressed_len = xfer->old_len;
							PERFINFO_AUTO_START("ZipForBindiff", 1);
							pclZipData(uncompressed, uncompressed_len, &xfer->old_len, &xfer->old_data);
							free(uncompressed);
							PERFINFO_AUTO_STOP();
						}
					}
				}
			}
		}
		else
		{
			TriviaMutex mutex;
			if(xfer->use_mutex)
				mutex = triviaAcquireDumbMutex(xfer->filename_to_write);
			xfer->old_data = fileAlloc(xfer->filename_to_write, &xfer->old_len);
			if(xfer->use_mutex)
				triviaReleaseDumbMutex(mutex);
		}
	}

	// If we actually have old data and we're still sure it's a good idea, go ahead and set up diffing
	if(xfer->old_data)
	{
		if(fileTooDifferent(xfer))
		{
			SAFE_FREE(xfer->old_data);
		}
		else if(( xfer->get_compressed && xfer->com_len == xfer->old_len && xfer->com_crc == patchChecksum(xfer->old_data,xfer->old_len)) ||
				(!xfer->get_compressed && xfer->unc_len == xfer->old_len && xfer->unc_crc == patchChecksum(xfer->old_data,xfer->old_len)) )
		{
			// This can happen when operating without a manifest
			xferSetState(xfer, XFER_COMPLETE_NOCHANGE);
			PERFINFO_AUTO_STOP_CHECKED("SetupBindiffing");
			return;
		}
		else // diffing
		{
			xfer->old_block_len		= xfer->old_len + (xfer->get_compressed ? xfer->com_print_sizes[0] : xfer->unc_print_sizes[0])-1;
			xfer->old_data			= realloc_canfail(xfer->old_data,xfer->old_block_len);
			xfer->old_copied_len	= ((xfer->old_block_len - 1) >> 3) + 1;
			if (xfer->old_data)
				xfer->old_copied		= calloc_canfail(xfer->old_copied_len, 1);
			if (xfer->old_data && xfer->old_copied)
				memset(xfer->old_data + xfer->old_len, 0, xfer->old_block_len - xfer->old_len);
			else
			{
				// Not enough memory for bindiffing.
				SAFE_FREE(xfer->old_data);
				SAFE_FREE(xfer->old_copied);
				xfer->old_block_len = 0;
				xfer->old_copied_len = 0;
				ADD_MISC_COUNT(1000000, "OOM: bindiffing canceled");
			}
		}
	}
	if(!xfer->old_data)
		xfer->old_len = 0;
	PERFINFO_AUTO_STOP_CHECKED("SetupBindiffing");

	new_data_size = xfer->get_compressed ? xfer->com_block_len : xfer->unc_block_len;
	xfer->new_data = calloc_canfail(new_data_size, 1);
	if (!xfer->new_data)
	{
		xfer->old_block_len = 0;
		SAFE_FREE(xfer->old_data);
		xfer->old_copied_len = 0;
		SAFE_FREE(xfer->old_copied);
		xfer->new_data = calloc(new_data_size, 1);
	}
	xfer->num_print_sizes = xfer->get_compressed ? xfer->com_num_print_sizes : xfer->unc_num_print_sizes;
	xfer->print_sizes = xfer->get_compressed ? xfer->com_print_sizes : xfer->unc_print_sizes;

	if(slipstream_data)
	{
		memcpy(xfer->new_data, slipstream_data, xfer->com_len);
		xferSetState(xfer, XFER_REQ_WRITE);
	}
	else if(xfer->old_data == NULL) // get the entire file
	{
		reqEntireFile(xferrer, xfer);
	}
	else
	{
		U32 block_len = xfer->get_compressed ? xfer->com_block_len : xfer->unc_block_len;
		xfer->fingerprints_len	= block_len / xfer->print_sizes[xfer->num_print_sizes-1] * sizeof(xfer->fingerprints[0]);
		xfer->fingerprints		= calloc(xfer->fingerprints_len, 1);
		xfer->scoreboard_len	= block_len / xfer->print_sizes[xfer->num_print_sizes-1] * 4;
		xfer->scoreboard		= calloc(xfer->scoreboard_len, 1);
		xfer->print_idx			= 0;
		xfer->state				= XFER_REQ_FINGERPRINTS;
	}

	// Recalculate memory usage.
	xferrerRemoveMemUsage(xferrer, xfer);
	xferrerCalculateMemUsage(xferrer, xfer);
}

void reqFingerPrints(PatchXferrer * xferrer, PatchXfer *xfer)
{
	Packet	*pak;
	U32 sub_count=1, packet_bytes = MAX_SINGLE_REQUEST_BYTES;
	int i, last_idx;

	if (xfer->print_idx)
		sub_count = xfer->print_sizes[xfer->print_idx-1] / xfer->print_sizes[xfer->print_idx];

	packet_bytes = MAX_SINGLE_REQUEST_BYTES;

	for(i=xfer->curr_req_idx;i<xfer->num_block_reqs;i++)
	{
		U32		bytes = xfer->block_reqs[i*2+1] * sub_count * sizeof(U32);

		if (packet_bytes < bytes || !xferOkToRequestBytes(xferrer, xfer, bytes))
			break;
		packet_bytes -= bytes;
	}
	if (i == xfer->curr_req_idx)
		return;
	last_idx = i;

	pak = pktCreate(xferrer->client->link, xfer->get_compressed ? PATCHCLIENT_REQ_FINGERPRINTS_COMPRESSED : PATCHCLIENT_REQ_FINGERPRINTS);
	pktSendString(pak,xfer->filename_to_get);
	pktSendBits(pak,32,xfer->id);
	assert(xfer->print_idx >= 0 && xfer->print_idx < (int)xfer->num_print_sizes);
	pktSendBits(pak,8,xfer->print_idx);
	pktSendBits(pak,32,last_idx - xfer->curr_req_idx);
	for(i=xfer->curr_req_idx;i<last_idx;i++)
	{
		U32 new_block_len = xfer->get_compressed ? xfer->com_block_len : xfer->unc_block_len;
		assert( (xfer->block_reqs[i*2] + xfer->block_reqs[i*2+1]) >= 0 &&
			(xfer->block_reqs[i*2] + xfer->block_reqs[i*2+1]) * sub_count * xfer->print_sizes[xfer->print_idx] <= new_block_len);
		xfer->print_bytes_requested += xfer->block_reqs[i*2+1] * sub_count * xfer->print_sizes[xfer->print_idx];
		pktSendBits(pak,32,xfer->block_reqs[i*2] * sub_count);
		pktSendBits(pak,32,xfer->block_reqs[i*2+1] * sub_count);
	}
	xfer->total_bytes_sent += pktGetSize(pak);
	pktSend(&pak);
	xfer->curr_req_idx = last_idx;
	if (xfer->curr_req_idx >= xfer->num_block_reqs)
	{
		xfer->curr_req_idx = 0;
		xferSetState(xfer, XFER_WAIT_FINGERPRINTS);
		return;
	}
}

void reqFirstFingerPrints(PatchXferrer * xferrer, PatchXfer *xfer)
{
	U32		num_prints, num_reqs, i, prints_per_req;

	if (!xfer->block_reqs)
	{
		U32 new_len = xfer->get_compressed ? xfer->com_len : xfer->unc_len;
		num_prints = (new_len + xfer->print_sizes[xfer->print_idx]-1) / xfer->print_sizes[xfer->print_idx];
		num_reqs = xfer->num_block_reqs = ((num_prints * sizeof(U32)) + MAX_SINGLE_REQUEST_BYTES -1) / MAX_SINGLE_REQUEST_BYTES;

		xfer->blocks_total = 0;
		xfer->blocks_so_far = 0;

		xfer->block_reqs = malloc(sizeof(U32) * 2 * num_reqs);
		for(i=0;i<num_reqs;i++)
		{
			xfer->block_reqs[i*2+0] = i * MAX_SINGLE_REQUEST_BYTES / sizeof(U32);
			prints_per_req = MAX_SINGLE_REQUEST_BYTES / sizeof(U32);
			if (i == num_reqs - 1 && (num_prints % prints_per_req) > 0)
				prints_per_req = num_prints % prints_per_req;
			xfer->block_reqs[i*2+1] = prints_per_req;
			xfer->blocks_total += prints_per_req;
		}
	}
	reqFingerPrints(xferrer, xfer);
}

static int receiveFingerprints(PatchXferrer * xferrer, Packet *pak, PatchXfer *xfer, bool compressed)
{
	U32		print_idx = xfer->print_idx;
	U32		*print_sizes = xfer->print_sizes;
	U32		new_block_len = xfer->get_compressed ? xfer->com_block_len : xfer->unc_block_len;
	U32		num_prints = new_block_len / print_sizes[print_idx];
	U32		sub_count=1;
	int		i, num_reqs;
	U32		*scoreboard = xfer->scoreboard;
	U32		max_contiguous, next_sub_count;

	assert(xfer->get_compressed == compressed);

	if (print_idx + xfer->retries >= xfer->num_print_sizes-1) // requesting raw data
		max_contiguous = MAX_SINGLE_REQUEST_BYTES / print_sizes[print_idx];
	else // requesting next set of fingerprints
	{
		next_sub_count = xfer->print_sizes[xfer->print_idx] / xfer->print_sizes[xfer->print_idx+1];
		max_contiguous = MAX_SINGLE_REQUEST_BYTES/ (next_sub_count * sizeof(U32));
	}

	if (print_idx)
		sub_count = print_sizes[print_idx-1] / print_sizes[print_idx];
	num_reqs = pktGetBits(pak,32);
	for(i=0;i<num_reqs;i++)
	{
		int		start,count,j;

		start = pktGetBits(pak,32);
		count = pktGetBits(pak,32);
		xfer->blocks_so_far += count;
		for(j = 0; j < count; j++)
		{
			xfer->fingerprints[start + j] = pktGetBits(pak, 32);
		}
		xferOkToRequestBytes(xferrer, xfer,-count * sizeof(U32));
	}
	if (xfer->state != XFER_WAIT_FINGERPRINTS || xfer->bytes_requested != 0)
		return 1;
	
	xfer->print_bytes_requested = 0;
	SAFE_FREE(xfer->block_reqs);

	xfer->num_block_reqs = bindiffCreatePatchReqFromFingerprints(
		xfer->fingerprints,num_prints,
		xfer->old_data,xfer->old_block_len,
		xfer->new_data,new_block_len,
		&xfer->block_reqs,print_sizes[print_idx],
		scoreboard, xfer->old_copied, max_contiguous, xfer->retries + 2, NULL);

	xfer->blocks_total = 0;
	xfer->blocks_so_far = 0;
	for(i = 0; i < xfer->num_block_reqs; i++)
		xfer->blocks_total += xfer->block_reqs[i*2+1];

	// The file hasn't changed; it's just been truncated to a shorter length, or increased to a larger size, padded by zeros.
	if (xfer->num_block_reqs == 0 && xfer->old_len != (xfer->get_compressed ? xfer->com_len : xfer->unc_len))
	{
		xferSetState(xfer, XFER_REQ_WRITE);
		return 1;
	}

	// The file hasn't changed, and it's file size hasn't change: this should not happen.
	else if (xfer->num_block_reqs == 0)
	{
		ErrorDetailsf("filename %s rev %d old_len %d compressed %d", xfer->filename_to_get,
			xfer->ver ? xfer->ver->rev : -1, xfer->old_len, (int)xfer->get_compressed);
		Errorf("Internal bindiffing error: the old and new files are unexpectedly identical");
		pcllog(xferrer->client, PCLLOG_ERROR, "Bindiffing reports files are identical, retrying...");
		++xfer->retries;
		xferSetState(xfer, XFER_REQ_FILEINFO);
		return 1;
	}

	// Done getting fingerprints; start requesting data blocks.
	else if (print_idx + xfer->retries >= xfer->num_print_sizes-1)
	{
		xferSetState(xfer, XFER_REQ_DATA);
		return 0;
	}

	print_idx++; xfer->print_idx++;
	sub_count = print_sizes[print_idx-1] / print_sizes[print_idx];

	xfer->blocks_total *= sub_count;

	for(i = num_prints - 1; (int) i >= 0; i--)
	{
		U32 j;

		if (scoreboard[i])
		{
			for(j=0;j<sub_count;j++)
				scoreboard[i*sub_count+j] = scoreboard[i] + j * print_sizes[print_idx];
		}
		else
		{
			for(j=0;j<sub_count;j++)
				scoreboard[i*sub_count+j] = 0;
		}
	}
	xferSetState(xfer, XFER_REQ_FINGERPRINTS);

	return 1;
}

void reqHeaderBlocks(PatchXferrer * xferrer, PatchXfer *xfer)
{
	U32 packet_bytes;
	int i, last_idx;
	Packet * pak;

	packet_bytes = MAX_SINGLE_REQUEST_BYTES;
	for(i=xfer->curr_req_idx; i<xfer->num_block_reqs; i++)
	{
		U32 bytes = xfer->block_reqs[i*2+1] * HEADER_BLOCK_SIZE;

		if (packet_bytes < bytes )
			break;
		if (!xferOkToRequestBytes(xferrer, xfer, bytes))
			break;
		packet_bytes -= bytes;
	}
	if (i == xfer->curr_req_idx) // couldn't fit anything in request buffer
		return;

	last_idx = i;

	pak = pktCreate(xferrer->client->link, PATCHCLIENT_REQ_HEADER_BLOCKS);
	pktSendString(pak,xfer->filename_to_get);
	pktSendBits(pak,32,xfer->id);
	pktSendBits(pak,32,last_idx - xfer->curr_req_idx);

	for(i = xfer->curr_req_idx; i < last_idx; i++)
	{
		pktSendBits(pak,32,xfer->block_reqs[i*2]);
		pktSendBits(pak,32,xfer->block_reqs[i*2+1]);
	}
	xfer->total_bytes_sent += pktGetSize(pak);
	pktSend(&pak);

	xfer->curr_req_idx = last_idx;
	if (last_idx >= xfer->num_block_reqs)
	{
		xfer->curr_req_idx = 0;
		xferSetState(xfer, XFER_WAIT_DATA);
		return;
	}
}

void reqDataBlocks(PatchXferrer * xferrer, PatchXfer *xfer)
{
	U32		block_size = xfer->print_sizes[xfer->print_idx];
	U32		packet_bytes;
	int i, last_idx;
	Packet	*pak;

	// If this should be an HTTP xfer, do that instead of using PCL block requests.
	// TODO: Support getting manifest and filespec with HTTP.
	if (xferHttpShouldUseHttp(xferrer, xfer) && xfer->get_compressed && !xferIsManifest(xfer) && !xferIsFilespec(xfer))
	{
		xfer->use_http = true;
		xfer->used_http = true;
		xferHttpReqDataBlocks(xferrer, xfer);
		return;
	}

	packet_bytes = MAX_SINGLE_REQUEST_BYTES;
	for(i=xfer->curr_req_idx;i<xfer->num_block_reqs;i++)
	{
		U32		bytes = xfer->block_reqs[i*2+1] * block_size;

		if (packet_bytes < bytes )
			break;
		if (!xferOkToRequestBytes(xferrer, xfer,bytes))
			break;
		packet_bytes -= bytes;
	}
	if (i == xfer->curr_req_idx) // couldn't fit anything in request buffer
		return;

	last_idx = i;

	if(xfer->get_compressed)
		pak = pktCreate(xferrer->client->link, PATCHCLIENT_REQ_BLOCKS_COMPRESSED);
	else
		pak = pktCreate(xferrer->client->link, PATCHCLIENT_REQ_BLOCKS);
	pktSendString(pak,xfer->filename_to_get);
	pktSendBits(pak,32,xfer->id);
	pktSendBits(pak,32,xfer->print_sizes[xfer->print_idx]);
	pktSendBits(pak,32,last_idx - xfer->curr_req_idx);

	for(i=xfer->curr_req_idx;i<last_idx;i++)
	{
		assert( (xfer->block_reqs[i*2] + xfer->block_reqs[i*2+1]) >= 0 &&
			(xfer->block_reqs[i*2] + xfer->block_reqs[i*2+1]) * xfer->print_sizes[xfer->print_idx] <=
			(xfer->get_compressed ? xfer->com_block_len : xfer->unc_block_len) );
		pktSendBits(pak,32,xfer->block_reqs[i*2]);
		pktSendBits(pak,32,xfer->block_reqs[i*2+1]);
	}
	xfer->total_bytes_sent += pktGetSize(pak);
	pktSend(&pak);

	xfer->curr_req_idx = last_idx;
	if (last_idx >= xfer->num_block_reqs)
	{
		xfer->curr_req_idx = 0;
		xferSetState(xfer, XFER_WAIT_DATA);
		return;
	}
}

static void receiveHeaderBlocks(PatchXferrer * xferrer, Packet * pak, PatchXfer * xfer)
{
	U32 num_reqs, i;

	num_reqs = pktGetBits(pak, 32);

	for(i = 0; i < num_reqs; i++)
	{
		int start, count;

		start = pktGetBits(pak, 32);
		count = pktGetBits(pak, 32);
		pktGetBytes(pak, HEADER_BLOCK_SIZE * count, xfer->new_data + start * HEADER_BLOCK_SIZE);
		xferOkToRequestBytes(xferrer, xfer, HEADER_BLOCK_SIZE * -count);
		xfer->blocks_so_far += count;
	}

	if(xfer->bytes_requested == 0 && xfer->state == XFER_WAIT_DATA)
		xferSetState(xfer, XFER_COMPLETE);
}

static void receiveBlocks(PatchXferrer * xferrer, Packet * pak, PatchXfer * xfer, bool compressed)
{
	U32 num_reqs, i, block_size = xfer->print_sizes[xfer->print_idx];

	//assertHeapValidateAll();
	assert(xfer->get_compressed == compressed);
	num_reqs = pktGetBits(pak,32);

	for(i=0;i<num_reqs;i++)
	{
		int		start,count;

		start = pktGetBits(pak,32);
		count = pktGetBits(pak,32);
		pktGetBytes(pak, block_size * count, xfer->new_data + start * block_size);
		xferOkToRequestBytes(xferrer, xfer, block_size * -count);
		xfer->blocks_so_far += count;
	}
	if (xfer->bytes_requested == 0 && xfer->state == XFER_WAIT_DATA)
		xferSetState(xfer, XFER_REQ_WRITE);
	//assertHeapValidateAll();
}

void deleteRenamedFile(PatchXfer *xfer)
{
	char file_path[MAX_PATH];
	machinePath(file_path,xfer->filename_to_write);
	strcat(file_path,".deleteme");
	fwChmod(file_path, _S_IREAD|_S_IWRITE);
	unlink(file_path);
}

char* xferWriteFileToDisk(PCL_Client *client, char *file_path, U8 *data, U32 len, U32 timestamp, U32 crc, bool use_mutex,
								PCL_FileFlags flags, bool *renamed, U32 compressed_unc_len, void *handle)
{
	char *ret = NULL;
	FILE *fout;
	size_t written = 0;
	TriviaMutex mutex;
	U32 tries = 0;
	char dirname[MAX_PATH];
	bool should_free_data = false;

	PERFINFO_AUTO_START_FUNC();

	devassert(!data || !handle);

	ANALYSIS_ASSUME(data);
	ANALYSIS_ASSUME(handle);

	if(client->verbose_logging)
		filelog_printf("patchxfer", "xferWriteFileToDisk: fname=%s size=%lu crc=%lu unc_len=%lu", file_path, len, crc, compressed_unc_len);

	if (compressed_unc_len)
	{
		U32 unc_len = compressed_unc_len;
		char *unc_data = malloc(unc_len);
		int result = unzipData(unc_data, &unc_len, data, len);
		devassert(!handle);
		if (result)
		{
			estrPrintf(&ret, "Failed to decompress file data for %s!", file_path);
			PERFINFO_AUTO_STOP_FUNC();
			return ret;
		}
		data = unc_data;
		len = unc_len;
		should_free_data = true;
	}

	if(renamed)
		*renamed = false;

	strcpy(dirname, file_path);
	if (!dirExists(getDirectoryName(dirname))) // Do quick FileWatcher-backed query first before slow disk operations
		assertmsgf(makeDirectoriesForFile(file_path), "Unable to make directories for %s", file_path);

	if(fileExists(file_path) && (flags & (PCL_BACKUP_TIMESTAMP_CHANGES|PCL_BACKUP_WRITEABLES)))
	{
		bool backup = false;
		bool isIdentical = false;
		FWStatType status = {0};
		fwStat(file_path, &status);

		if (status.st_size == len) {
			char *tempdata = fileAlloc(file_path, NULL);
			if (memcmp(tempdata, data, len)==0)
				isIdentical = true;
			fileFree(tempdata);
		}

		// FIXME: don't backup .manifests and .filespecs
		if (!isIdentical)
		{
			if(flags&PCL_BACKUP_TIMESTAMP_CHANGES && (U32)status.st_mtime > timestamp)
			{
				pcllog(client, PCLLOG_WARNING, "%s has a newer timestamp than the current version, backing it up.", file_path);
				backup = true;
			}
			else if(flags&PCL_BACKUP_WRITEABLES && status.st_mode&_S_IWRITE)
			{
				pcllog(client, PCLLOG_WARNING, "%s is not flagged readonly, and not checked out, backing it up.", file_path);
				backup = true;
			}
		}

		if(backup)
			fileRenameToBak(file_path);
	}

	if(use_mutex)
		mutex = triviaAcquireDumbMutex(file_path);
	fwChmod(file_path, _S_IREAD|_S_IWRITE);
	fout = fopen(file_path, "wb");
	if(!fout) // the file may be in use... it might be this executable!
	{
		if(fileExists(file_path))
		{
			char delete_me[MAX_PATH];
			sprintf(delete_me, "%s.deleteme", file_path);
			if(fileExists(delete_me) && unlink(delete_me) != 0)
				estrPrintf(&ret, "Could not delete previously renamed file (%s)", delete_me);
			else if(rename(file_path, delete_me) != 0)
				estrPrintf(&ret, "Could not rename file that could not be overwritten (%s)", file_path);
			else
			{
				if(renamed)
				{
					*renamed = true;
				}
				fout = fopen(file_path, "wb");

				if(!fout)
					estrPrintf(&ret, "Could not open new file for writing (%s)", file_path);
			}
		}
		else
			estrPrintf(&ret, "Cannot create file (%s)", file_path);
	}
	if(ret)
	{
		if(use_mutex)
			triviaReleaseDumbMutex(mutex);
		if (should_free_data)
			free(data);
		PERFINFO_AUTO_STOP_FUNC();
		return ret;
	}
	
	if (handle)
	{
		char buf[1024*8];
		for(;;)
		{
			size_t to_write;
			DWORD bytes_read;
			bool success;
			size_t written_this_time;

			to_write = len - written;
			if (!to_write)
				break;
			PERFINFO_AUTO_START_BLOCKING("ReadFile", 1);
			success = ReadFile(handle, buf, (DWORD)MIN(sizeof(buf), to_write), &bytes_read, NULL);
			PERFINFO_AUTO_STOP();
			if (!success || !bytes_read)
				break;
			written_this_time = fwrite(buf, 1, bytes_read, fout);
			if (!written_this_time)
				break;
			written += written_this_time;
		}
		PERFINFO_AUTO_START_BLOCKING("CloseHandle", 1);
		CloseHandle(handle);
		PERFINFO_AUTO_STOP();
	}
	else
	{
		while(tries < 5 && written != len)
		{
			written += fwrite(data + written, 1, len - written, fout);
			tries += 1;
		}
	}
	fclose(fout);
	if(use_mutex)
		triviaReleaseDumbMutex(mutex);

	if(written != len)
	{
		estrPrintf(&ret, "Could not write entire file %s! (%u/%u)", file_path, written, len);
		if (should_free_data)
			free(data);
		PERFINFO_AUTO_STOP_FUNC();
		return ret; 
	}

	if(timestamp)
	{
		if(!fileSetTimestamp(file_path, timestamp))
		{
			estrPrintf(&ret, "Could not set file timestamp on %s to %u!", file_path, timestamp);

			if (should_free_data)
				free(data);
			PERFINFO_AUTO_STOP_FUNC();
			return ret; 
		}
	}

	if(fwChmod(file_path, (flags & PCL_SET_READ_ONLY) ? _S_IREAD : (_S_IREAD|_S_IWRITE)))
	{
		PERFINFO_AUTO_STOP_FUNC();
		if (should_free_data)
			free(data);
		return estrCreateFromStr("Could not set file permissions!");
	}

	if(client->verifyAllFiles && strstri(file_path, PATCH_DIR)==NULL)
	{
		// Read back the file and check that it is okay
		FWStatType statbuf = {0};
		U32 disk_crc;

		if(fwStat(file_path, &statbuf))
		{
			estrPrintf(&ret, "Unable to read back file %s!", file_path);
			PERFINFO_AUTO_STOP_FUNC();
			return ret;
		}
		if(statbuf.st_size != len)
		{
			estrPrintf(&ret, "Size mismatch during %s readback. Got %u, expecting %u.", file_path, statbuf.st_size, len);
			PERFINFO_AUTO_STOP_FUNC();
			return ret;
		}
		disk_crc = patchChecksumFile(file_path);
		if(disk_crc != crc)
		{
			estrPrintf(&ret, "CRC mismatch during %s readback. Got %u, expecting %u.", file_path, disk_crc, crc);
			PERFINFO_AUTO_STOP_FUNC();
			return ret;
		}
	}

	if(client->verbose_logging)
		filelog_printf("patchxfer", "xferWriteFileToDisk: fname=%s successful", file_path);

	if (should_free_data)
		free(data);

	PERFINFO_AUTO_STOP_FUNC();
	return NULL;
}

// Set max_net_bytes for this xferrer.  Return false if it wasn't able to be set at this time.
bool xferSetMaxNetBytes(PatchXferrer * xferrer, U32 max_net_bytes)
{
	bool verifyret;

	verifyret = verifynetbytes(xferrer);
	assert(verifyret);

	if (xferrer->net_bytes_free != xferrer->max_net_bytes || eaSize(&xferrer->http_connections))
		return false;
	xferrer->max_net_bytes = max_net_bytes;
	xferrer->net_bytes_free = xferrer->max_net_bytes;

	verifyret = verifynetbytes(xferrer);
	assert(verifyret);

	return true;
}

// Set max_mem_usage for this xferrer.  Return false if it wasn't able to be set at this time.
bool xferSetMaxMemUsage(PatchXferrer * xferrer, U64 max_mem_usage)
{
	xferrer->max_mem_usage = max_mem_usage;
	return true;
}

// Get information from a bad packet report from the server.
void pclReadBadPacket(Packet *pak, int *cmd, int *extra1, int *extra2, int *extra3, char **estrErrorString)
{
	const char *error;
	*cmd = pktGetBits(pak, 32);
	*extra1 = pktGetBits(pak, 32);
	*extra2 = pktGetBits(pak, 32);
	*extra3 = pktGetBits(pak, 32);
	error = pktGetStringTemp(pak);
	if (error)
		estrCopy2(estrErrorString, error);
	else
		estrClear(estrErrorString);
}

// TODO: This really should use a WorkerThread-based design, so it doesn't have to create a separate parallel thread for each file it needs to write.
unsigned int WINAPI thread_xferWriteFileToDisk(void *arglist)
{
	char *ret = NULL;
	bool renamed;
	char file_path[MAX_PATH];
	xferWriteFileArgs *args = (xferWriteFileArgs *)arglist;
	int i;

	EXCEPTION_HANDLER_BEGIN

	machinePath(file_path, args->file_path);

	for(i=0; i<=5; ++i)
	{
		if(ret) estrDestroy(&ret);
		ret = xferWriteFileToDisk(args->client, file_path, args->data, args->len, args->timestamp, args->crc, args->use_mutex,
			args->flags, &renamed, args->compressed_unc_len, NULL);
		if(ret == NULL) break;
		Sleep(100);
	}
	if(ret)
	{
		if(args->xfer->restarts >= MAX_RESTARTS)
		{
			triviaPrintf("Filename", "%s", file_path);
			triviaPrintf("LastError", "%s", lastWinErr());
			// FIXME: Accessing the client from a background thread like this is not safe.
			// It also should be using REPORT_ERROR_STRING
			triviaPrintf("LinkStatus", "%s", args->client->link && !linkDisconnected(args->client->link)?"connected":"disconnected");
			args->client->error = PCL_COULD_NOT_WRITE_LOCKED_FILE;
		}
		else
		{
			static bool first = true;
			if (first)
			{
				ErrorfForceCallstack("xferWriteFileToDisk failure (%s):\n%s\n%s\n%s", file_path, ret, lastWinErr(), (linkDisconnected(args->client->link)?"disconnected":"connected"));
				first = false;
			}
			else
				Errorf("xferWriteFileToDisk failure (%s):\n%s\n%s\n%s", file_path, ret, lastWinErr(), (linkDisconnected(args->client->link)?"disconnected":"connected"));
			xferSetState(args->xfer, XFER_RESTART);
		}
		estrDestroy(&ret);
	}
	else
	{
		if(renamed)
			args->client->needs_restart = true;
		xferSetState(args->xfer, XFER_COMPLETE);
	}
	free(args);

	EXCEPTION_HANDLER_END

	return 0;
}

//Takes ownership of xfer->new_data
void writeFile(PatchXferrer *xferrer, PatchXfer *xfer)
{
	PERFINFO_AUTO_START_FUNC();

	if(xfer->file_flags & PCL_NO_WRITE)
	{
		SAFE_FREE(xfer->new_data);
		return;
	}

	if(xfer->write_hogg)
	{
		U32 timestamp;
		NewPigEntry entry = {0};
		S32 doWriteFileToHogg = 1;

		if(xfer->file_time)
			timestamp = xfer->file_time;
		else
			timestamp = getCurrentFileTime();

		if(doWriteFileToHogg)
		{
			//assertHeapValidateAll();
			//assert(_CrtIsValidHeapPointer(xfer->new_data));

			// New unified hog updating code
			entry.fname = xfer->filename_to_write;
			entry.timestamp = timestamp;
			entry.data = xfer->new_data;
			entry.checksum[0] = xfer->unc_crc; // this should be enough as long as we are working with hoggs
			// entry.header_data handled automatically
			entry.size = xfer->unc_len;
			xferrer->written_bytes += entry.size;
			if(xfer->get_compressed)
				entry.pack_size = xfer->com_len; // We are handing off already compressed data
			if(!xfer->com_len)
				entry.dont_pack = 1; // If the server didn't compress it, we certainly don't want to
			else if(xferrer->compress_as_server)
				entry.must_pack = 1; // The server compressed it and we want to follow suit
			
			if(xferrer->client->verbose_logging)
				filelog_printf("patchxfer", "hogFileModifyUpdateNamed2: hogg=%s fname=%s size=%u crc=%u", hogFileGetArchiveFileName(xfer->write_hogg), entry.fname, entry.size, entry.checksum[0]);

			// Blacklight hack: Don't write asynchronously if the file is big, to save RAM, since we're in a background thread anyway.
			pclMSpf("hogFileModifyUpdateNamed2\n");
			if (entry.size > xferrer->max_mem_usage || memTrackIsReserveMemoryChunkBusy())
			{
				PERFINFO_AUTO_START_BLOCKING("hogFileModifyUpdateNamedSync2", 1);
				hogFileModifyUpdateNamedSync2(xfer->write_hogg, &entry);
				PERFINFO_AUTO_STOP();
				PERFINFO_AUTO_START_BLOCKING("hogFileModifyFlush", 1);
				hogFileModifyFlush(xfer->write_hogg);
				PERFINFO_AUTO_STOP();
			}
			else
				hogFileModifyUpdateNamed2(xfer->write_hogg, &entry);

			pclMSpf("done hogFileModifyUpdateNamed2\n");

			// Read back the file, make sure it is correct
			if(xferrer->client->verifyAllFiles)
			{
				HogFileIndex hfi;
				U32 crc, len;
				char *data;
				bool valid = false;

				hfi = hogFileFind(xfer->write_hogg, xfer->filename_to_write);
				crc = hogFileGetFileChecksum(xfer->write_hogg, hfi);
				assertmsgf(crc==xfer->unc_crc, "CRC mismatch on %s readback. Got %u, expecting %u.", xfer->filename_to_write, crc, xfer->unc_crc);
				data = hogFileExtract(xfer->write_hogg, hfi, &len, &valid);
				assertmsgf(data, "No data on %s readback.", xfer->filename_to_write);
				assertmsgf(len==xfer->unc_len, "Length mismatch on %s readback. Got %u, expecting %u.", xfer->filename_to_write, len, xfer->unc_len);
				assertmsgf(valid, "CRC failure on %s readback.", xfer->filename_to_write);
				free(data);
			}

			xfer->new_data = NULL;
		}
	}
	else
	{
		bool renamed;
		char *ret;
		char file_path[MAX_PATH];
		machinePath(file_path,xfer->filename_to_write);
		pclMSpf("xferWriteFileToDisk\n");
		ret = xferWriteFileToDisk(xferrer->client, file_path, xfer->new_data, xfer->get_compressed ? xfer->com_len : xfer->unc_len, xfer->file_time,
			xfer->unc_crc, xfer->use_mutex, xfer->file_flags, &renamed, xfer->get_compressed ? xfer->unc_len : 0, NULL);
		pclMSpf("done xferWriteFileToDisk\n");
		assertmsg(ret == NULL, ret);
		estrDestroy(&ret);
		if(renamed)
			xferrer->client->needs_restart = true;
	}

	SAFE_FREE(xfer->new_data);

	PERFINFO_AUTO_STOP_FUNC();
}

static void xferClearReqData(PatchXfer * xfer)
{
	SAFE_FREE(xfer->old_data);
	SAFE_FREE(xfer->unc_print_sizes);
	SAFE_FREE(xfer->com_print_sizes);
	SAFE_FREE(xfer->fingerprints);
	SAFE_FREE(xfer->scoreboard);
	SAFE_FREE(xfer->old_copied);
	SAFE_FREE(xfer->block_reqs);
	estrDestroy(&xfer->http_request_debug);
	estrDestroy(&xfer->http_range_debug);
	estrDestroy(&xfer->http_header_debug);
	estrDestroy(&xfer->http_mime_debug);
	xfer->num_block_reqs = 0;
	xfer->print_sizes = NULL;
}

void xferClear(PatchXfer * xfer)
{
	PERFINFO_AUTO_START_FUNC();
	SAFE_FREE(xfer->new_data);
	xferClearReqData(xfer);
	xfer->use_http = false;
	xfer->used_http = false;
	xfer->slipstream_available = false;
	xfer->http_trimmed_blocks = false;
	PERFINFO_AUTO_STOP();
}

void xferFree(PatchXferrer *xferrer, PatchXfer *xfer)
{
	xferClear(xfer);
	if (xfer->file_flags & PCL_CALLBACK_ON_DESTROY)
		xferCallback(xferrer, xfer, PCL_DESTROYED, NULL);
	SAFE_FREE(xfer);
}

// Reset the warning timer on the xferrer.
static void xferTimestamp(PatchXferrer *xferrer)
{
	if (xferrer->timewarnings)
	{
		pcllog(xferrer->client, PCLLOG_SPAM, "xferrer resuming, everything is ok!  yay!  (total hang time: %ds)", timerCpuSeconds()-xferrer->timestamp);
	}
	xferrer->timestamp = timerCpuSeconds();
	xferrer->timewarnings = 0;
}

PatchXfer * xferStartHeader(PatchXferrer * xferrer, const char * filename, HogFile * read_hogg, HogFile * write_hogg)
{
	PatchXfer * xfer;

	if(xferrerFull(xferrer, filename))
		return NULL;

	xfer = calloc(1, sizeof(PatchXfer));

	xfer->get_header = true;
	strcpy(xfer->filename_to_get, filename);
	strcpy(xfer->filename_to_write, filename);
	xfer->read_hogg = read_hogg;
	xfer->write_hogg = write_hogg;
	xfer->priority = 1;
	xfer->start = xfer->timestamp = timerCpuTicks64();
	xfer->state = -1;
	xferSetState(xfer, XFER_REQ_FILEINFO);
	xfer->id = ++(xferrer->curr_id);
	xferAdd(xferrer, xfer);
	xferTimestamp(xferrer);

	return xfer;
}

PatchXfer *xferStart(PatchXferrer * xferrer, const char *fname, const char *fname_to_write, int priority, U32 timestamp, bool use_mutex, bool force,
					 PCL_FileFlags flags, PCL_GetFileCallback callback, HogFile * read_hogg, HogFile * write_hogg, FileVersion *ver, void * userData)
{
	PatchXfer * xfer;

	if(!force && xferrerFull(xferrer, fname))
		return NULL;

	xfer = calloc(1, sizeof(PatchXfer));

	xfer->file_time = timestamp;
	strcpy(xfer->filename_to_get, fname);
	strcpy(xfer->filename_to_write, fname_to_write);
	xfer->read_hogg = read_hogg;
	xfer->write_hogg = write_hogg;
	xfer->priority = priority;
	xfer->start = xfer->timestamp = timerCpuTicks64();
	xfer->state = -1;
	xferSetState(xfer, XFER_REQ_FILEINFO);
	xfer->id = ++(xferrer->curr_id);
	xfer->use_mutex = use_mutex;
	xfer->file_flags = flags;
	xfer->callback = callback;
	xfer->ver = ver;
	xfer->actual_rev = -1;
	xfer->userData = userData;
	xferAdd(xferrer, xfer);
	xferTimestamp(xferrer);

	if(xferrer->client->verbose_logging)
		filelog_printf("patchxfer", "xferStart: get=%s write=%s", xfer->filename_to_get, xfer->filename_to_write);

	return xfer;
}

static PatchXfer *xferRestart(PatchXferrer *xferrer, PatchXfer *xfer)
{
	PatchXfer *new_xfer;

	new_xfer = xferStart(xferrer, xfer->filename_to_get, xfer->filename_to_write, xfer->priority, xfer->file_time, xfer->use_mutex, true,
		xfer->file_flags, xfer->callback, xfer->read_hogg, xfer->write_hogg, xfer->ver, xfer->userData);
	if(!new_xfer)
		return NULL;

	xfer->callback = NULL;
	xfer->userData = NULL;
	new_xfer->restarts = xfer->restarts + 1;

	return new_xfer;
}

// Call the xfer's callback.
// This function guarantees that an xfer completion callback is not called more than once.
static void xferCallback(PatchXferrer *xferrer, PatchXfer *xfer, PCL_ErrorCode error, const char *error_details)
{
	if (xfer->callback)
	{
		XferStateInfo *info;

		PERFINFO_AUTO_START("xferGetStateInfo", 1);
		info = xferGetStateInfo(xfer);
		PERFINFO_AUTO_STOP();

		PERFINFO_AUTO_START("doCallback", 1);
		xfer->callback(xferrer->client, xfer->filename_to_get, info, error, error_details, xfer->userData);
		PERFINFO_AUTO_STOP();

		free(info);
	}
	xfer->callback = NULL;
}

PatchXfer *xferFindById(PatchXferrer * xferrer, U32 id)
{
	int		i;

	for(i=0;i<eaSize(&xferrer->xfers) ;i++)
	{
		if (xferrer->xfers[i]->id == id)
			return xferrer->xfers[i];
	}
	return 0;
}

// Report an unexpected packet.
static void xferUnexpectedPacket(PatchXferrer *xferrer, PatchXfer *xfer, int id, int cmd)
{
	pcllog(xferrer->client, PCLLOG_ERROR, "Received unexpected packet for xfer: cmd %d id %d xfer %s header %d", cmd, id, xfer->filename_to_get, !!xfer->get_header);
	ErrorDetailsf("cmd %d id %d xfer %s header %d", cmd, id, xfer->filename_to_get, !!xfer->get_header);
	Errorf("Received unexpected packet for xfer");
}

void handleXferMsg(Packet * pak, int cmd, PatchXferrer * xferrer)
{
	U32			id;
	PatchXfer	*xfer;

	assert(xferrer);

	// Update activity timer.
	xferTimestamp(xferrer);

	// Handle reports of bad packets from the server by restarting.
	if (cmd == PATCHSERVER_BAD_PACKET)
	{
		int error_cmd, extra1, extra2, extra3;
		char *error = NULL;
		char *full_error = NULL;

		// Try to find an associated xfer.
		estrStackCreate(&error);
		pclReadBadPacket(pak, &error_cmd, &extra1, &extra2, &extra3, &error);
		xfer = NULL;
		if (extra1)
			xfer = xferFindById(xferrer, extra1);

		// Report the error.
		estrStackCreate(&full_error);
		estrPrintf(&full_error, "xfer %s cmd %d extra1 %d extra2 %d extra3 %d error \"%s\"", xfer ? xfer->filename_to_get : "(unknown)", error_cmd, extra1, extra2, extra3, error);;
		estrDestroy(&error);
		pcllog(xferrer->client, PCLLOG_ERROR, "Bad packet in xfer: %s", full_error);
		ErrorDetailsf("%s", full_error);
		Errorf("Bad packet in xfer");
		estrDestroy(&full_error);

		// Reset the xferrer.
		xferrerReset(xferrer);

		return;
	}

	// Get xfer associated with this packet.
	id = pktGetBits(pak,32);
	assert(id <= xferrer->curr_id);
	xfer = xferFindById(xferrer, id);

	// this can happen after a reset
	// FIXME: this is a big hole in error checking
	if(!xfer && xferrer->has_ever_reset)
		return;

	// If this packet is for an unknown xfer, log it.
	if (!xfer)
	{
		pcllog(xferrer->client, PCLLOG_ERROR, "Received packet for unexpected xfer: cmd %d id %d", cmd, id);
		ErrorDetailsf("cmd %d id %d", cmd, id);
		Errorf("Received packet for unknown xfer");
		return;
	}

	// Account received bytes to this xfer.
	xfer->total_bytes_received += pktGetSize(pak);

	// TODO: more safety checking
	// I'm assuming the above comment refers to the fact that many of the below functions assume that the contents
	// of whatever packets they get back match the packets that were sent.  Not only is there no validation of this,
	// but these routines in most cases just blindly process whatever comes in, even if it might mean writing to
	// invalid memory.  If something minor goes wrong such that the response doesn't match what was expected,
	// the xferrer will generally just stall out.

	if(xfer->get_header)
	{
		switch(cmd)
		{
		xcase PATCHSERVER_HEADERINFO:
			receiveHeaderInfo(xferrer, pak, xfer);
		xcase PATCHSERVER_HEADER_BLOCKS:
			receiveHeaderBlocks(xferrer, pak, xfer);
		xdefault:
			xferUnexpectedPacket(xferrer, xfer, id, cmd);
		}
	}
	else
	{
		switch(cmd)
		{
		xcase PATCHSERVER_FILEINFO:
			receiveFileInfo(xferrer, pak, xfer);
		xcase PATCHSERVER_FINGERPRINTS:
			receiveFingerprints(xferrer, pak, xfer, false);
		xcase PATCHSERVER_FINGERPRINTS_COMPRESSED:
			receiveFingerprints(xferrer, pak, xfer, true);
		xcase PATCHSERVER_BLOCKS:
			receiveBlocks(xferrer, pak, xfer, false);
		xcase PATCHSERVER_BLOCKS_COMPRESSED:
			receiveBlocks(xferrer, pak, xfer, true);
		xdefault:
			xferUnexpectedPacket(xferrer, xfer, id, cmd);
		}
	}
}

char * xferGetState(PatchXfer * xfer)
{
	switch(xfer->state)
	{
		#define CASE(x) case x:return #x + 5
		CASE(XFER_REQ_VIEW);
		CASE(XFER_WAIT_VIEW);
		CASE(XFER_REQ_FILEINFO);
		CASE(XFER_WAIT_FILEINFO);
		CASE(XFER_REQ_FINGERPRINTS);
		CASE(XFER_WAIT_FINGERPRINTS);
		CASE(XFER_REQ_DATA);
		CASE(XFER_WAIT_DATA);
		CASE(XFER_REQ_WRITE);
		CASE(XFER_WAIT_WRITE);
		CASE(XFER_COMPLETE);
		CASE(XFER_COMPLETE_NOCHANGE);
		CASE(XFER_COMPLETE_IGNORED);
		CASE(XFER_RESTART);
		CASE(XFER_FILENOTFOUND);
		#undef CASE
		default:
			return "UNKNOWN_STATE";
	}
}

bool xferProcessOneXfer(PatchXferrer * xferrer, PatchXfer * xfer)
{
	U32 last_bytes_free = xferrer->net_bytes_free;
	bool good;
	U64 start;
	U32 tested_checksum;
	bool ret;
	static StaticCmdPerf cmdPerf[XFER_STATE_COUNT];
	int bindiffed = 0;
	U32 hog_op_count;

	EnterStaticCmdPerfMutex();
	if(xfer->state >= 0 && xfer->state < ARRAY_SIZE(cmdPerf)){
		if(!cmdPerf[xfer->state].name){
			char buffer[100];
			sprintf(buffer, "Xfer:%s", xferGetState(xfer));
			cmdPerf[xfer->state].name = strdup(buffer);
		}
		PERFINFO_AUTO_START_STATIC(cmdPerf[xfer->state].name, &cmdPerf[xfer->state].pi, 1);
	}else{
		PERFINFO_AUTO_START("Xfer:Unknown", 1);
	}
	LeaveStaticCmdPerfMutex();

	start = timerCpuTicks64();

	// Process HTTP xfers separately.
	if (xfer->use_http)
	{
		ret = xferHttpReqDataBlocks(xferrer, xfer);
		PERFINFO_AUTO_STOP();
		return ret;
	}

	switch(xfer->state)
	{
	xcase XFER_FILENOTFOUND:
		pcllog(xferrer->client, PCLLOG_ERROR, "XFER_FILENOTFOUND %s", xfer->filename_to_get);
		if(xfer->restarts < MAX_RESTARTS)
			xferSetState(xfer, XFER_RESTART);
		else
		{
			xferCallback(xferrer, xfer, PCL_FILE_NOT_FOUND, NULL);
			REPORT_ERROR_STRING(xferrer->client, PCL_FILE_NOT_FOUND, xfer->filename_to_get);
			xferRemove(xferrer, xfer);
			xferFree(xferrer, xfer);
			xferrer->client->error = PCL_FILE_NOT_FOUND;
		}
		ret = false;

	xcase XFER_REQ_FILEINFO:
		if(xfer->get_header)
			reqHeaderInfo(xferrer, xfer);
		else
			reqFileInfo(xferrer, xfer);
		ret = last_bytes_free != xferrer->net_bytes_free;

	xcase XFER_REQ_FINGERPRINTS:
		if (!xfer->print_idx)
			reqFirstFingerPrints(xferrer, xfer);
		else
			reqFingerPrints(xferrer, xfer);
		ret = last_bytes_free != xferrer->net_bytes_free;

	xcase XFER_REQ_DATA:
		if(xfer->get_header)
			reqHeaderBlocks(xferrer, xfer);
		else
			reqDataBlocks(xferrer, xfer);
		ret = last_bytes_free != xferrer->net_bytes_free;

	xcase XFER_REQ_WRITE: // Start writing the file in a thread
		xferTimestamp(xferrer);
		assert(!xfer->get_header);

		hog_op_count = hogTotalPendingOperationCount();

		// Throttle writes if hoglib isn't keeping up, to avoid excess resource use in the hog system.
		if (xfer->write_hogg && hog_op_count > 1024)
		{
			ret = false;
			break;
		}

		if (xfer->blocks_total)
			bindiffed = (xfer->get_compressed ? xfer->com_len : xfer->unc_len) -
				xfer->blocks_total * xfer->print_sizes[xfer->print_idx];
		bindiffed = bindiffed >= 0 ? bindiffed : 0;
		if (bindiffed)
			pclMSpf("bindiffed %s %d", xfer->filename_to_write, bindiffed);
		
		if(xfer->skip_final_checksum)
			good = true;
		else
		{
			U32 expected_crc = xfer->get_compressed ? xfer->com_crc : xfer->unc_crc;
			U32 actual_crc = xfer->get_compressed ? patchChecksum(xfer->new_data, xfer->com_len) : patchChecksum(xfer->new_data, xfer->unc_len);
			good = expected_crc == actual_crc;
			if(!good)
			{

				// If the file is not being bindiffed, there should be no checksum errors.
				if (!xfer->old_data)
				{
					// If this file was downloaded with HTTP, record it as an HTTP error.
					if (xfer->http_used)
						xferHttpXferRecordFail(xferrer, xfer, "crc failure when writing");
	
					ErrorDetailsf("filename %s compressed %d http %d expected %lu actual %lu len %lu file_time %lu ver %d rev %d",
						xfer->filename_to_write, (int)xfer->get_compressed, (int)xfer->http_used, expected_crc, actual_crc,
						xfer->get_compressed ? xfer->unc_len : xfer->com_len, xfer->file_time,
						xfer->ver ? xfer->ver->version : -1, xfer->ver ? xfer->ver->rev : -1);
					Errorf("Checksum verification failed after download");
				}

				// Save out a copy of the bad data for later analysis
				if(xferrer->client->bad_files)
				{
					char badfilename[MAX_PATH*2], temp[MAX_PATH], *colon;
					strcpy(temp, xfer->filename_to_write);
					colon = strchr(temp, ':');
					if(colon) *colon = '_';
					sprintf(badfilename, "%s/%s/%s", xferrer->client->bad_files, xferrer->client->project, temp);
					if(xfer->get_compressed)
						strcat(badfilename, ".gz");
					if(strlen(badfilename) < MAX_PATH)
					{
						makeDirectoriesForFile(badfilename);
						xferWriteFileToDisk(xferrer->client, badfilename, xfer->new_data, xfer->get_compressed?xfer->com_len:xfer->unc_len, xfer->file_time, xfer->get_compressed?xfer->com_crc:xfer->unc_crc, false, xfer->file_flags, NULL, 0, NULL);
					}
				}
			}
		}

		// Do a deep verify of all recieved files
		if(good && xfer->get_compressed && xferrer->client->verifyAllFiles)
		{
			U32		size_uncompressed = xfer->unc_len;
			char*	data_uncompressed = malloc(size_uncompressed);

			if(	unzipData(	data_uncompressed,
				&size_uncompressed,
				xfer->new_data,
				xfer->com_len) ||
				size_uncompressed != xfer->unc_len)
			{
				printfColor(COLOR_BRIGHT|COLOR_RED,
					"Failed to decompress received file: %s\n",
					xfer->filename_to_write);
				ErrorDetailsf("filename %s", xfer->filename_to_write);
				Errorf("Failed to decompress received file");
				good = false;
			}

			if(good && xfer->unc_crc != patchChecksum(data_uncompressed, size_uncompressed))
			{
				printfColor(COLOR_BRIGHT|COLOR_RED,
					"Failed to checksum received file: %s\n",
					xfer->filename_to_write);
				ErrorDetailsf("filename %s", xfer->filename_to_write);
				Errorf("Failed to checksum received file");
				good = false;
			}

			SAFE_FREE(data_uncompressed);
		}

		if(good)
		{
			//bool renamed;
			//const char *ret;
			//char file_path[MAX_PATH];
			xferWriteFileArgs *args;
			PCL_FileFlags write_in_memory;

			pclMSpf("good (%s)\n",
				xfer->filename_to_write);
			// !!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!

			assert(!xfer->get_header);
			pclMSpf("xferWriteFileToDisk\n");

			// Release bindiffing memory.
			xferClearReqData(xfer);
			xferrerRemoveMemUsage(xferrer, xfer);
			xferrerCalculateMemUsage(xferrer, xfer);

			if(xfer->ver)
				write_in_memory = xferrer->client->file_flags & PCL_IN_MEMORY;
			else
				write_in_memory = xferrer->client->file_flags & (PCL_IN_MEMORY|PCL_METADATA_IN_MEMORY);
			if(write_in_memory)
			{
				FileVersion *ver;
				char *new_data;

				if(xfer->ver)
				{
					// Store the data in the FileVersion
					ver = xfer->ver;
					assert(ver->size == xfer->unc_len);
					assert(ver->checksum == xfer->unc_crc);
				}
				else
				{
					// Special file, store the data in the client for later
					if(xferIsManifest(xfer))
					{
						ver = xferrer->client->in_memory_data.manifest = calloc(1, sizeof(FileVersion));
					}
					else if(xferIsFilespec(xfer))
					{
						ver = xferrer->client->in_memory_data.filespec = calloc(1, sizeof(FileVersion));
					}
					else
					{
						assertmsgf(0, "Don't know how to do an in-memory download of %s", xfer->filename_to_get);
					}
					ver->size = xfer->unc_len;
					ver->checksum = xfer->unc_crc;
					ver->modified = xfer->file_time;
				}
				xferrer->written_bytes += ver->size;

				// If the data needs to be decompressed, decompress it.
				// FIXME: This should probably happen in some sort of background thread.
				if (xfer->get_compressed)
				{
					U32 unc_len;
					int result;

					PERFINFO_AUTO_START("InMemoryUnzip", 1);

					unc_len = xfer->unc_len;
					new_data = malloc(unc_len);
					result = unzipData(new_data, &unc_len, xfer->new_data, xfer->com_len);
					free(xfer->new_data);

					PERFINFO_AUTO_STOP_CHECKED("InMemoryUnzip");
				}
				else
					new_data = xfer->new_data;
				xfer->new_data = NULL;

				// Copy the data pointer over to the FileVersion
				SAFE_FREE(ver->in_memory_data);
				ver->in_memory_data = new_data;

				xferSetState(xfer, XFER_COMPLETE);
			}
			else if(xfer->write_hogg)
			{
				writeFile(xferrer, xfer);
				xferSetState(xfer, XFER_COMPLETE);
			}
			else
			{
				args = callocStruct(xferWriteFileArgs);
				args->xfer = xfer;
				args->client = xferrer->client;
				args->file_path = xfer->filename_to_write;
				args->data = xfer->new_data;
				args->len = xfer->get_compressed ? xfer->com_len : xfer->unc_len;
				args->timestamp = xfer->file_time;
				args->use_mutex = xfer->use_mutex;
				args->flags = xfer->file_flags;
				args->crc = xfer->unc_crc;
				args->compressed_unc_len = xfer->get_compressed ? xfer->unc_len : 0;
				xferSetState(xfer, XFER_WAIT_WRITE);

				xferrer->written_bytes += args->len;

				// FIXME: Try to limit the time that both compressed and uncompressed data need to be allocated at once.
				if (xfer->get_compressed)
					xferrerAddMemUsage(xferrer, xfer, args->compressed_unc_len);

				if(xfer->file_flags & PCL_NO_WRITE)
				{
					xferSetState(xfer, XFER_COMPLETE);
				}
				else
				{
#if _PS3
					CreateThreadPS3(0, thread_xferWriteFileToDisk, args, 0);
#else
					CloseHandle((HANDLE)_beginthreadex(NULL, 0, thread_xferWriteFileToDisk, args, 0, NULL));
#endif
				}
			}
		}
		else
		{
			pclMSpf("!good (%s)\n",
				xfer->filename_to_write);

			pcllog(xferrer->client, PCLLOG_FILEONLY, "transfer failed, trying again for %s (retry %i, %s)", xfer->filename_to_write, xfer->retries, xfer->get_compressed?"compressed":"uncompressed");
			xfer->retries += 1;
			xferClear(xfer);
			xferSetState(xfer, XFER_REQ_FILEINFO);

			pclMSpf("done !good (%s)\n",
				xfer->filename_to_write);
		}
		ret = true;

		pclMSpf("done !getheader\n");

	xcase XFER_WAIT_WRITE:
		// Background thread is running to write file, do not stall.
		xferrer->timestamp = timerCpuSeconds();
		ret = false;

	xcase XFER_COMPLETE:
		xferTimestamp(xferrer);
		if(xfer->get_header)
		{
			pclMSpf("getheader (%s)\n",
					xfer->filename_to_write);

			if(xfer->unc_len)
			{
				tested_checksum = patchChecksum(xfer->new_data, xfer->unc_len);
				good = (tested_checksum == xfer->unc_crc);
				if(good || xfer->retries >= 1000)
				{
					if(xfer->retries >= 1000)
						pcllog(xferrer->client, PCLLOG_SPAM, "Writing header %s after repeated bad transfers", xfer->filename_to_write);
					pclMSpf("updating in hogg (%s)\n",
							xfer->filename_to_write);
					hogFileModifyUpdateNamed(xfer->write_hogg, xfer->filename_to_write, xfer->new_data, xfer->unc_len, 0, NULL);
					pclMSpf("done updating in hogg (%s)\n",
							xfer->filename_to_write);
					xfer->new_data = NULL;
					xferrer->progress_recieved += xfer->unc_len;
					xferrer->completed_files++;
					xferrer->written_bytes += xfer->unc_len;
					xferRemove(xferrer, xfer);
					xferFree(xferrer, xfer);
				}
				else
				{
					xfer->retries += 1;
					xferClear(xfer);
					xferSetState(xfer, XFER_REQ_FILEINFO);
				}
			}
			else
			{
				xferrer->completed_files++;
				xferRemove(xferrer, xfer);
				xferFree(xferrer, xfer);
			}

			pclMSpf("done getheader\n");
		}
		else
		{
			// File was written in the BG thread, process callbacks.
			if(xfer->retries)
				pcllog(xferrer->client, PCLLOG_FILEONLY, "Retry succeeded for %s, everything is OK!!!", xfer->filename_to_write);
			xferrer->progress_recieved += xfer->unc_len;
			xferrer->completed_files++;
			xferCallback(xferrer, xfer, PCL_SUCCESS, NULL);
			xferRemove(xferrer, xfer);
			xferFree(xferrer, xfer);


			pclMSpf("done !getheader\n");
		}
		ret = false;

	xcase XFER_COMPLETE_NOCHANGE:
		pclMSpf("%s is already up to date", xfer->filename_to_write);
		xferTimestamp(xferrer);
// 		xferrer->progress_recieved += xfer->unc_len;
// 		xferrer->completed_files++;
		// update the file's timestamp, since only crc and size are verified when setting XFER_COMPLETE_NOCHANGE
		if(xfer->file_time)
		{
			if(xfer->write_hogg)
				hogFileModifyUpdateTimestamp(xfer->write_hogg, hogFileFind(xfer->write_hogg, xfer->filename_to_write), xfer->file_time);
			else
			{
				TriviaMutex mutex;
				if(xfer->use_mutex)
					mutex = triviaAcquireDumbMutex(xfer->filename_to_write);
				fileSetTimestamp(xfer->filename_to_write, xfer->file_time);
				if(xfer->use_mutex)
					triviaReleaseDumbMutex(mutex);
			}
		}
		if(!xfer->write_hogg)
			deleteRenamedFile(xfer);
		xferCallback(xferrer, xfer, PCL_SUCCESS, NULL);
		xferRemove(xferrer, xfer);
		xferFree(xferrer, xfer);
		ret = false;

	xcase XFER_COMPLETE_IGNORED:
		xferrer->timestamp = timerCpuSeconds();
		xferrer->progress_recieved += xfer->unc_len;
		xferrer->completed_files++;
		xferCallback(xferrer, xfer, PCL_SUCCESS, NULL);
		xferRemove(xferrer, xfer);
		xferFree(xferrer, xfer);
		ret = false;

	xcase XFER_RESTART:
		if(xferRestart(xferrer, xfer))
		{
			xferRemove(xferrer, xfer);
			xferFree(xferrer, xfer);
		}
		ret = false;

	xdefault:
		ret = false;
	};

	if (ret)
		pclMSpf("made progress in xferProcessOneXfer (state=%s, file=%s)\n",
			xferStateGetName(xfer->state),
			xfer->filename_to_get);

	PERFINFO_AUTO_STOP();

	return ret;
}

bool xferrerFull(PatchXferrer * xferrer, const char *fname)
{
	if(eaSize(&xferrer->xfers) >= MAX_XFERS)
	{
		return true;
	}

	if(xferrer->current_mem_usage >= xferrer->max_mem_usage)
	{
		return true;
	}

	if(fname)
	{
		U32 uFileSizeFromManifest = getPatchFileSizeFromManifest(xferrer, fname);

		if((xferrer->current_mem_usage + uFileSizeFromManifest) >= xferrer->max_mem_usage)
		{
			if(eaSize(&xferrer->xfers) == 0)
			{
				// We'll allow this one by itself, with a warning, even though it might
				// completely exceed the patch max-mem boundary.
// JE&AL: We don't care about logging this; causing little persistent allocations causing fragmentation
// 				pcllog(xferrer->client, PCLLOG_FILEONLY, "Patching '%s', even though it is %2.2fMB in size. (%2.2fMB is the current maximum).",
// 							fname, (F32)uFileSizeFromManifest / (1024*1024), (F32)xferrer->max_mem_usage / (1024*1024));
// 				eaPush(&xferrer->big_files,strdup(fname));

				return false;
			}

			return true;
		}
	}

	return false;
}

// Collect some network transfer-related statistics.
static void xferReportNetStats(PatchXferrer * xferrer)
{
	U32 now;
	const U32 record_period = 1;		// Collect instantaneous stats this often
	const U32 report_period = 60;		// Report stats this often
	U32 elapsed;
	char *logline = NULL;

	PERFINFO_AUTO_START_FUNC();

	// Only report once per period.
	now = timeSecondsSince2000();
	if (now < xferrer->last_net_stats_record + record_period)
	{
		PERFINFO_AUTO_STOP_FUNC();
		return;
	}
	xferrer->last_net_stats_record = now;
	// TODO: record some PCL statistics, analogous to the HTTP ones
	// Since the server is also recording these statistics for PCL, it's not as important as HTTP, where
	// server-side statistics may not be available.
	if (xferrer->use_http)
		xferHttpRecordNetStats(xferrer);

	// Only record once per period.
	if (now < xferrer->last_net_stats_report + report_period)
	{
		PERFINFO_AUTO_STOP_FUNC();
		return;
	}
	elapsed = xferrer->last_net_stats_report ? now - xferrer->last_net_stats_report : 0;
	xferrer->last_net_stats_report = now;

	// Collect statistics.
	// TODO: report some PCL statistics, analogous to the HTTP ones
	// Note: I failed to prefix the HTTP-specific statistics with something HTTP-specific, so there
	// will need to be some distinction for when PCL stats are added.
	estrPrintf(&logline, "elapsed %lu", elapsed);
	if (xferrer->client->project)
		estrPrintf(&logline, " project %s", xferrer->client->project);
	if (xferrer->use_http)
		xferHttpReportNetStats(xferrer, &logline);

	// Send statistics.
	pclSendLog(xferrer->client, "ClientThroughput", "%s", logline);
	estrDestroy(&logline);

	PERFINFO_AUTO_STOP_FUNC();
}

void xferProcess(PatchXferrer * xferrer)
{
	int i, priority;
	bool progress, any_progress = false;
	PatchXfer ** xfers = NULL;
	U32 now;

	PERFINFO_AUTO_START_FUNC();

	pclMSpf2("xferProcess %d xfers", eaSize(&xfers));

	xferReportNetStats(xferrer);
	
	eaCopy(&xfers, &xferrer->xfers);
	do 
	{
		progress = false;
		for(i = eaSize(&xfers) - 1; i >= 0; i--)
		{
			if(progress && xfers[i]->priority != priority)
				break;

			if(xferProcessOneXfer(xferrer, xfers[i]))
			{
				progress = true;
				any_progress = true;
				priority = xfers[i]->priority;
				xfers[i]->timestamp = timerCpuTicks64();
			}
			else
				eaRemove(&xfers, i);
		}
	} while(progress);
	eaDestroy(&xfers);
	eaQSort(xferrer->xfers, cmpXferPriority);
	//assert(verifynetbytes(xferrer));

	now = timerCpuSeconds();
	if(eaSize(&xferrer->xfers) && now - xferrer->timestamp > XFERRER_HANG_WARNING*(xferrer->timewarnings+1))
	{
		if(++xferrer->timewarnings < XFERRER_HANG_RESET)
		{
			pcllog(xferrer->client, PCLLOG_SPAM, "xferrer hanging, %ds now", timerCpuSeconds()-xferrer->timestamp);
		}
		else
		{
			pclReportResetAfterHang(xferrer->client, now - xferrer->timestamp);
			xferrerReset(xferrer);

			// When resetting the xferrer, disable HTTP, in case HTTP somehow contributed.
			// Once we have more faith in HTTP's robustness, this may be disabled.
			xferrer->use_http = false;
		}
	}

	// Performance counters
	if (PERFINFO_RUN_CONDITIONS)
	{
		unsigned long waiting_on_server = 0;
		unsigned long waiting_on_write = 0;
		U32 bytes_requested = 0;
		U32 print_bytes_requested = 0;
		EARRAY_CONST_FOREACH_BEGIN(xferrer->xfers, j, m);
		{
			PatchXfer *xfer = xferrer->xfers[j];
			if (xferStateWaitingOnServer(xfer->state))
				++waiting_on_server;
			if (xfer->state == XFER_WAIT_WRITE)
				++waiting_on_write;
			bytes_requested += xfer->bytes_requested + xfer->http_bytes_requested;
			print_bytes_requested += xfer->print_bytes_requested;
		}
		EARRAY_FOREACH_END;
		if (any_progress)
		{
			ADD_MISC_COUNT(1000000, "progress");
		}
		else
		{
			ADD_MISC_COUNT(1000000, "no progress");
		}
		if (waiting_on_server)
		{
			ADD_MISC_COUNT(1000000, "waiting on server");
		}
		else
		{
			ADD_MISC_COUNT(1000000, "not waiting on server");
		}
		ADD_MISC_COUNT(100000*waiting_on_write, "pending writes");
		ADD_MISC_COUNT(bytes_requested, "bytes_requested");
		ADD_MISC_COUNT(print_bytes_requested, "print_bytes_requested");
	}

	pclMSpf2("done xferProcess progress %d", !!progress);

	PERFINFO_AUTO_STOP_FUNC();
}

// Enable HTTP for an xferrer.
void xferEnableHttp(PatchXferrer *xferrer)
{
	xferrer->use_http = true;
}

// Return total number of bytes received.
U64 xferBytesReceived(PatchXferrer *xferrer)
{
	U64 count = 0;
	return xferrer->http_bytes_received + xferHttpBytesReceived(xferrer);
}

// Moved this to the end because it was confusing CTAGS/WorkspaceWhiz for some reason :(
STATIC_ASSERT(PCL_SLIPSTREAM_THRESHOLD < MIN_FILE_SIZE_FOR_DIFF);
