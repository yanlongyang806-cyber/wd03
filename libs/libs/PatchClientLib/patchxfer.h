#ifndef _PATCHXFER_H
#define _PATCHXFER_H
GCC_SYSTEM

#include "pcl_client.h"

typedef	struct FileVersion FileVersion;
typedef struct HogFile HogFile;
typedef struct NetComm NetComm;
typedef struct NetLink NetLink;
typedef struct Packet Packet;
typedef struct PatchXfer PatchXfer;
typedef struct PatchXferrerHttpConnection PatchXferrerHttpConnection;
typedef struct XferStateInfo XferStateInfo;


typedef enum
{
	XFER_REQ_VIEW = 0,
	XFER_WAIT_VIEW,
	XFER_REQ_FILEINFO,
	XFER_WAIT_FILEINFO,
	XFER_REQ_FINGERPRINTS,
	XFER_WAIT_FINGERPRINTS,
	XFER_REQ_DATA,
	XFER_WAIT_DATA,
	XFER_REQ_WRITE,
	XFER_WAIT_WRITE,
	XFER_COMPLETE,
	XFER_COMPLETE_NOCHANGE,
	XFER_COMPLETE_IGNORED,
	XFER_RESTART,
	XFER_FILENOTFOUND,

	// Add new states above this line.
	XFER_STATE_COUNT
} XferState;

typedef struct PatchXfer
{
	char		filename_to_get[MAX_PATH], filename_to_write[MAX_PATH];

	U32			file_time;		// what modified time should we write?
	HogFile		*read_hogg;		// where should we read the file from?
	HogFile		*write_hogg;			// where should we put the file?
	FileVersion *ver;			// FileVersion that is being transfered, optional
	PCL_FileFlags file_flags;
	PCL_GetFileCallback callback; // called when xfer is finished
	void		*userData;		// passed to callback

	const char	*http_server;	// HTTP server that this xfer should use, null if default
	U16			http_port;
	const char	*path_prefix;

	bool		use_mutex;
	bool		get_header;		// xferring header instead of file
	bool		get_compressed;	// whether to work on the already deflated file
	bool		skip_final_checksum; // whether to do a checksum of the data before writing

	int			retries;		// when something goes wrong
	int         restarts;       // when something goes really wrong
	int			http_retries;	// when an HTTP request is restarted, without any error
	int			http_fail;		// when HTTP fails
	int			http_used;		// set if HTTP has been used at all

	XferState	state;			// what step are we on in the transfer?
	U64			entered_state;	// timerCpuTicks64() when we entered the state, for debugging
	int			priority;		// highest number transfers first
	U64			timestamp;		// when transfer was last updated
	U64			start;			// when transfer was started
	U32			id;				// unique id for network communication

	int			bytes_requested;		// if we ask for too much, server will disconnect
	int			print_bytes_requested;	// size represented by total number of outstanding fingerprint requests
	int			cum_bytes_requested;	// cumulative bytes_requested
	int			total_bytes_sent;		// total bytes sent for this xfer
	int			total_bytes_received;	// total bytes received for this xfer
	int			mem_usage;				// memory usage accounted to this xfer

	// HTTP patching
	bool		use_http;				// if true, file is being transferred with HTTP
	bool		used_http;				// if true, HTTP has been used for some part of the transfer process
	bool		http_sent;				// true if http request has been sent
	int			split_requests;			// if non-zero, this is the number of additional requests split from the main request
	int			http_bytes_requested;	// the portion of bytes_requested requested by http
	int			http_overload_bytes;	// number of extra bytes requested that aren't yet accounted to this
										// xfer because we wanted to request the whole file
	bool		http_trimmed_blocks;	// true after xferHttpTrimTrailingBlocks() has been run
	bool		sent_range_request;		// true if we sent the server a range request
	bool		sent_multi_range;		// true if sent_range_request, and it was for more than one range
	char		*http_request_debug;	// debug: the request that we sent
	char		*http_range_debug;		// debug: the ranges that we requested
	char		*http_header_debug;		// debug: the headers that we got back
	char		*http_mime_debug;		// debug: mime part headers
	S64			http_req_sent;			// time that this request was sent
	U64			http_req_size;			// size of the request
	

	// Information from the server
	int			actual_rev;				// actual revision of the base fileversion, if any; otherwise -1.
	bool		slipstream_available;	// the server offered us slipstream data

	U32			unc_len;			// server file size
	U32			unc_block_len;		// server file size rounded up to the largest block the server has fingerprints for
	U32			unc_crc;			// first 32 bits of md5 checksum for whole file
	U32			unc_num_print_sizes;// number of block sizes of fingerprints server has for this file
	U32			*unc_print_sizes;	// what the sizes are. (1024, 256, etc)

	U32			com_len;			// server file size in hogg
	U32			com_block_len;		// server file size in hogg rounded up to the largest block size
	U32			com_crc;
	U32			com_num_print_sizes;
	U32			*com_print_sizes;

	// These operate on compressed data if get_compressed is true
	U8			*old_data;			// local file data
	U32			old_len;			// local file size
	U32			old_block_len;		// local file size rounded up to the largest block the server has fingerprints for
	U8			*new_data;			// server file data

	int			print_idx;			// which one we are currently working with
	U32			num_print_sizes;
	U32			*print_sizes;		// shallow copy of unc_print_sizes or com_print_sizes
	U32			*fingerprints;		// fingerprint data we are currently transferring or working with
	U32			*scoreboard;		// array of indexes matching the new file for where we've found matches in the old file
	U8			*old_copied;		// array of indexes matching the old file for where we've already copied out data
	U32			fingerprints_len;	// size in bytes of fingerprints
	U32			scoreboard_len;		// size in bytes of scoreboard
	U32			old_copied_len;		// size in bytes of old_copied

	int			num_block_reqs;	// bindiff generates a list of blocks to request (data or fingerprints)
	U32			*block_reqs;
	int			curr_req_idx;		// net buffering: last request sent to server

	U32 blocks_so_far;
	U32 blocks_total;
} PatchXfer;

typedef struct PatchXferrer
{
	NetComm * comm;
	PatchXfer ** xfers;
	PatchXfer ** xfers_order;		// CrypticLauncher hack for xfers dialog  TODO: eliminate this
	PCL_Client * client;
	S64 progress_recieved;
	S64 progress_total;
	U32 completed_files;
	U32 total_files;
	U64 overlay_bytes;				// Number of bytes copied by overlay
	U64 written_bytes;				// Number of bytes written to disk
	U32 curr_id;
	int next_xfer;
	U32 timestamp;
	unsigned int timewarnings;
	char **big_files;				// Files bigger than max_mem_usage
	bool compress_as_server;		// Ignore file extension when compressing
	bool has_ever_reset;			// true if xferrerReset() has been called
	U32 last_net_stats_record;		// SS2000 of the last time we recorded stats
	U32 last_net_stats_report;		// SS2000 of the last time we reported stats

	// Resource limits
	U32 max_net_bytes;
	U32 net_bytes_free;
	U64 max_mem_usage;				// For limiting size of current transfers
	U64 current_mem_usage;

	// HTTP xfer
	bool use_http;					// Allow transferring files with HTTP.
	char *http_server;				// Hostname of HTTP server
	unsigned short http_port;		// HTTP server port
	char *path_prefix;				// Prefix of files on HTTP server
	PatchXferrerHttpConnection **http_connections; // Active HTTP connections
	U32 successful_requests;		// number of successful requests in total
	int http_requests_reset;		// number of HTTP requests sent: reset periodically
	int http_fails_reset;			// number of HTTP failures experienced: reset periodically
	int http_fails;					// number of HTTP failures experienced
	U64 http_bytes_received;		// total number of bytes received from the server, on previous links
	U32 http_problem_reports;		// number of problems that have been reported

	// HTTP statistics
	U64 http_header_bytes;
	U64 http_mime_bytes;
	U64 http_body_bytes;
	U64 http_extra_bytes;

	// HTTP period statistics
	// cpu ticks of the first req on any HTTP connection
	F32 *http_vec_first_req;
	// cpu ticks of the last send req
	F32 *http_vec_sub_req;
	// number of requests sent
	U64 *http_vec_reqs_sent;
	U64 http_stat_reqs_sent;
	// number of requests received.
	U64 *http_vec_reqs_recv;
	U64 http_stat_reqs_recv;
	// total amount of data sent
	U64 *http_vec_sent_total;
	U64 http_stat_sent_total;
	// total amount of sent data, wasted due to errors
	U64 *http_vec_sent_error;
	U64 http_stat_sent_error;
	// total amount of data received
	U64 *http_vec_recv_total;
	U64 http_stat_recv_total;
	// total amount of data received, that is overhead (metadata, etc)
	U64 *http_vec_recv_overhead;
	U64 http_stat_recv_overhead;
	// total amount of data received, wasted due to errors
	U64 *http_vec_recv_error;
	U64 http_stat_recv_error;
	// number of range requests received
	U64 *http_vec_ranges;
	U64 http_stat_ranges;
	// number of multipart range requests received
	U64 *http_vec_multiranges;
	U64 http_stat_multiranges;
} PatchXferrer;

// Create a xferrer
PatchXferrer * xferrerInit(PCL_Client * client);

// Find out if the xferrer can take another xfer (of the optionally provided filename)
bool xferrerFull(PatchXferrer * xferrer, const char *fname);

// Delete a xferrer
void xferrerDestroy(PatchXferrer ** xferrer);
void xferrerReset(PatchXferrer * xferrer);
void xferProcess(PatchXferrer * xferrer);

bool xferOkToRequestBytes(PatchXferrer * xferrer, PatchXfer *xfer,int amount);

// Enable HTTP for an xferrer.
void xferEnableHttp(PatchXferrer *xferrer);

// Return total number of bytes received.
U64 xferBytesReceived(PatchXferrer *xferrer);

PatchXfer * xferStart(PatchXferrer * xferrer, const char *fname, const char *fname_to_write, int priority, U32 timestamp, bool mutex, bool force,
					  PCL_FileFlags flags, PCL_GetFileCallback callback, HogFile * read_hogg, HogFile * write_hogg, FileVersion *ver, void * userData);
PatchXfer * xferStartHeader(PatchXferrer * xferrer, const char * filename, HogFile * read_hogg, HogFile *write_hogg);

void xferClear(PatchXfer * xfer);

// Debug output stuff
char * xferGetState(PatchXfer * xfer);
void xferrerGetStateInfo(PatchXferrer * xferrer, XferStateInfo *** state_info);

// Handler to call when a packet comes in for the xferrer
void handleXferMsg(Packet * pak, int cmd, PatchXferrer * xferrer);

// Handles renaming to '*.deleteme', etc.  Expects file_path to be machinePath'd.
// Returns NULL on success, an error string otherwise.
char* xferWriteFileToDisk(PCL_Client *client, char *file_path, U8 *data, U32 len, U32 timestamp, U32 crc,
								bool use_mutex, PCL_FileFlags flags, bool *renamed, U32 compressed_unc_len, void *handle);

// Set max_net_bytes for this xferrer.  Return false if it wasn't able to be set at this time.
bool xferSetMaxNetBytes(PatchXferrer * xferrer, U32 max_net_bytes);

// Set max_mem_usage for this xferrer.  Return false if it wasn't able to be set at this time.
bool xferSetMaxMemUsage(PatchXferrer * xferrer, U64 max_mem_usage);

// Get information from a bad packet report from the server.
void pclReadBadPacket(Packet *pak, int *cmd, int *extra1, int *extra2, int *extra3, char **estrErrorString);

#if PLATFORM_CONSOLE
#define MAX_XFERS    4
#else
#define MAX_XFERS	100
#endif
#define MAX_XFER_MEMORY_USAGE_DEFAULT (50 * 1024 * 1024)

#endif
