#include "../../3rdparty/zlib/zlib.h"
#include "AtExit.h"
#include "cpu_count.h"
#include "crypt.h"
#include "file.h"
#include "MemAlloc.h"
#include "osdependent.h"
#include "ScratchStack.h"
#include "timing.h"
#include "workerthread.h"
#include "zutils.h"

AUTO_RUN_ANON(memBudgetAddMapping(__FILE__, BUDGET_EngineMisc);); // Should just be generic ZStream

#if _PS3
#elif _XBOX
#pragma comment(lib, "zlibXbox.lib")
#else
#ifdef _WIN64
#ifdef _CODE_COVERAGE
#pragma comment(lib, "zlibX64noasm.lib")
#else
#pragma comment(lib, "zlibX64.lib")
#endif
#else
#ifdef _CODE_COVERAGE
#pragma comment(lib, "zlibWin32noasm.lib")
#else
#pragma comment(lib, "zlibWin32.lib")
#endif
#endif
#endif

// Comment out the following to use a stock zlib that lacks deflateSetRsyncParameters_().
#define USE_RSYNCABLE_ZLIB

// Set to 1 to use TLS pages
//
// TLS segments (declspec(thread)) are theoretically better than TLS slots (TlsGetValue()).  They're a lot faster,
// and they use less memory, and their usage is simpler and less error-prone.  However, in this application, the
// first two factors are insignificant.  TLS segments do not work correctly pre-Vista on dynamically-loaded DLLs,
// so TLS slot support must be used for GimmeDLL on XP.  My opinion is that the value of having all systems using
// the same implementation outweights the value of slightly better performance on some systems.  Therefore, TLS
// segment support is off by default.  I'm keeping the code though because it may have some future use.
//
// In my testing on an i7-2600 Sandy Bridge 3.4 GHz with 16 GB RAM on Windows 7, 32-bit:
// declspec(thread)		5 cycles per call
// TlsGetValue()		22 cycles per call
#define USE_TLS_SEGMENT 0
static const int use_tls_segment = USE_TLS_SEGMENT;

// Zip context
typedef struct ZipContext
{
	// Per-thread deflate() context
	z_stream*				zStreamDeflate;
	int						zStreamDeflateLevel;
	bool					zStreamDeflateRsyncable;

	// Per-thread inflate() context
	z_stream*				zStreamInflate;
	U32						totalBytesIn;
	U32						totalBytesOut;
	bool					zDisableUnzipSleep;
} ZipContext;

// TLS slot for zip context.
static DWORD tls_index = TLS_OUT_OF_INDEXES;

static void* utilZAlloc(void* opaque, U32 items, U32 size)
{
	return malloc(items * size);
}

static void utilZFree(void* opaque, void* address)
{
	SAFE_FREE(address);
}

// Called when a thread exits.
static void clearZipDataCallback(void *userdata, int thread_id, void *tls_value)
{
	clearZipData();
}

// Get the per-thread zip data.
static ZipContext *acquirePerThreadZipData()
{
	ZipContext *context;

	if (!use_tls_segment || !IsUsingVista())
	{
		// Allocate a TLS slot.
		ATOMIC_INIT_BEGIN;
		tls_index = TlsAlloc();
		assertmsgf(tls_index != TLS_OUT_OF_INDEXES, "TlsAlloc() returned TLS_OUT_OF_INDEXES: error %d", GetLastError());
		AtThreadExitInThread(clearZipDataCallback, NULL);
		ATOMIC_INIT_END;

		// Allocate per-thread context.
		context = TlsGetValue(tls_index);
		if (!context)
		{
			bool success;
			assertmsgf(GetLastError() == ERROR_SUCCESS, "TlsGetValue() failed: error %d", GetLastError());
			context = calloc(1, sizeof(*context));
			success = TlsSetValue(tls_index, context);
			assertmsgf(success, "TlsSetValue() failed: error %d", GetLastError());
		}
	}
	else
	{
#if USE_TLS_SEGMENT
		__declspec(thread) static ZipContext thread_specific_context = {0};
		tls_index = 0;
		context = &thread_specific_context;
#else
		context = NULL;
#endif
	}

	// Never get here.
	return context;
}

static ZipContext *utilInitZStreamDeflate(int level, bool rsyncable)
{
	ZipContext *context = acquirePerThreadZipData();

	if (context->zStreamDeflate && (level != context->zStreamDeflateLevel || rsyncable != context->zStreamDeflateRsyncable))
	{
		deflateEnd(context->zStreamDeflate);
		free(context->zStreamDeflate);
		context->zStreamDeflate = NULL;
	}

	if(!context->zStreamDeflate)
	{
		context->zStreamDeflate	= calloc(1, sizeof(*context->zStreamDeflate));

		context->zStreamDeflate->zalloc	= utilZAlloc;
		context->zStreamDeflate->zfree	= utilZFree;

		deflateInit(context->zStreamDeflate,level);
		context->zStreamDeflateLevel = level;
		context->zStreamDeflateRsyncable = rsyncable;

#ifdef USE_RSYNCABLE_ZLIB
		if (rsyncable)
		{
			// These constants are copied from the default values in Kevin Day's rsyncable patch.
			// They appear to be based upon some empirical experimentation by him.  However, they seem misleading.  In my testing,
			// 4096 does not actually yield flushes every 4096 bytes, either compressed or uncompressed.  This might indicate some sort
			// of problem with Kevin's patch, but at least it seems to yield valid data, and be consistent, which is the important part.
			// Note that if these are changed, or the compression level is changed, the bindiff will be large, as the consistency of
			// the relative position of the flushes is essential for compressed bindiffing.
			deflateSetRsyncParameters_(context->zStreamDeflate, Z_RSYNCABLE_RSSUM, 30, 4096);
		}
#endif
	}
	else
	{
		deflateReset(context->zStreamDeflate);
	}

	return context;
}

void unzipStreamInit(void)
{
	ZipContext *context = acquirePerThreadZipData();

	if(!context->zStreamInflate)
	{
		context->zStreamInflate	= calloc(1, sizeof(*context->zStreamInflate));

		context->zStreamInflate->zalloc	= utilZAlloc;
		context->zStreamInflate->zfree	= utilZFree;

		context->zDisableUnzipSleep = getNumRealCpus() > 1;
		context->totalBytesIn = 0;
		context->totalBytesOut = 0;

		PERFINFO_AUTO_START_L2("inflateInit",1);
		inflateInit(context->zStreamInflate);
		PERFINFO_AUTO_STOP_L2();
	}
	else
	{
		PERFINFO_AUTO_START_L2("inflateReset",1);
		inflateReset(context->zStreamInflate);
		context->totalBytesOut = 0;
		context->totalBytesIn = 0;
		PERFINFO_AUTO_STOP_L2();
	}
}

void unzipStreamEnd(void)
{
	ZipContext *context = acquirePerThreadZipData();
	if (context) {
		context->totalBytesOut = 0;
		context->totalBytesIn = 0;
	}
}

int unzipStream(char* outbuf, U32* outsize, const void* inbuf, U32 insize, bool never_sleep)
{
	int unzipRet=0;
	U32 outRemaining = *outsize;
	ZipContext *context;
	z_stream* zStream;

	context = acquirePerThreadZipData();
	if (!context) {
		return Z_STREAM_ERROR;
	}

	zStream = context->zStreamInflate;

	if(outRemaining <= 100){
		PERFINFO_AUTO_START("inflate <= 100", 1);
	}
	else if(outRemaining <= 1000){
		PERFINFO_AUTO_START("inflate 100-1k", 1);
	}
	else if(outRemaining <= 10000){
		PERFINFO_AUTO_START("inflate 1k-10k", 1);
	}
	else if(outRemaining <= 100000){
		PERFINFO_AUTO_START("inflate 10k-100k", 1);
	}
	else if(outRemaining <= 1000000){
		PERFINFO_AUTO_START("inflate 100k-1M", 1);
	}
	else if(outRemaining <= 10000000){
		PERFINFO_AUTO_START("inflate 1M-10M", 1);
	}
	else{
		PERFINFO_AUTO_START("inflate > 10M", 1);
	}

	zStream->next_in		= (void *)inbuf;
	zStream->avail_in		= insize;
	zStream->total_out		= 0;

	while(outRemaining){
		U32 curSize = min(outRemaining, 100000);
		int ret;

		zStream->avail_out		= curSize;
		zStream->next_out		= outbuf;

		outRemaining -= curSize;
		outbuf += curSize;

		PERFINFO_AUTO_START("inflate", 1);
		ret = inflate(zStream, Z_NO_FLUSH);
		PERFINFO_AUTO_STOP();

		if(ret != Z_OK && ret != Z_STREAM_END){
			//assert(ret == Z_OK || ret == Z_STREAM_END);
			unzipRet = ret;
			break;
		}

		if(outRemaining && !never_sleep && !context->zDisableUnzipSleep){
			Sleep(0);
		}
	}

	//assert(zStream->avail_out == 0);
	*outsize = zStream->total_out;
	context->totalBytesOut += zStream->total_out;
	context->totalBytesIn += insize - zStream->avail_in;

	PERFINFO_AUTO_STOP();

	return unzipRet;
}

U32 unzipStreamTell(void)
{
	ZipContext *context = acquirePerThreadZipData();
	return context ? context->totalBytesOut : 0;
}

U32 unzipStreamConsumed(void)
{
	ZipContext *context = acquirePerThreadZipData();
	return context ? context->totalBytesIn : 0;
}

S32 unzipStreamRemaining(void)
{
	ZipContext *context = acquirePerThreadZipData();
	return context ? context->zStreamInflate->avail_in : 0;
}

// Returns non-zero on failure
int unzipDataEx(char* outbuf, U32* outsize, const void* inbuf, U32 insize, bool never_sleep)
{
	int unzipRet;

	PERFINFO_AUTO_START(__FUNCTION__, 1);

	PERFINFO_AUTO_START_L3("init", 1);
	unzipStreamInit();
	PERFINFO_AUTO_STOP_L3();

	unzipRet = unzipStream(outbuf, outsize, inbuf, insize, never_sleep);

	PERFINFO_AUTO_STOP();

	return unzipRet;
}

void *zipDataEx_dbg(const void *src,U32 src_len,U32 *zip_size_p, int level, bool rsyncable, int special_heap MEM_DBG_PARMS)
{
	ZipContext *context;
	U8			*zip_data;
	bool		scratch_alloc = false;			// If true, used ScratchAlloc() instead of alloca().
	bool		no_scratch = false;				// If true, no temporary buffer is used.
	U32			zip_size;
	U8			*result;
	int			ret;

	PERFINFO_AUTO_START_FUNC();

	context = utilInitZStreamDeflate(level, rsyncable);

	// FIXME: Currently, it seems that deflateBound() may have a bug where it does not give a conservative bound for small files.  The following
	// addend is a temporary workaround.
	zip_size = deflateBound(context->zStreamDeflate, src_len) + 100;

	// Allocate temporary space to compress into.
	// This is optimized based on the assumption that the compression ratio will be high, and so the cost of having to copy from the temporary
	// space into the return buffer is small.  This avoids excessively fragmenting the heap by placing a large allocation on it and then
	// resizing it.  However, if the allocation is large enough it doesn't fit on the scratch stack, then don't make the temporary copy,
	// as it would serve no purpose.
	if (zip_size <= 16384)
		zip_data = _alloca(zip_size);
	else
	{
		PERFINFO_AUTO_START("zip alloc", 1);
		zip_data = ScratchAllocNoOverflowUninitialized(zip_size);
		if (zip_data)
			scratch_alloc = true;
		else
		{
			if (special_heap)
				zip_data = malloc_timed_canfail(zip_size, special_heap, false MEM_DBG_PARMS_CALL);
			else
				zip_data = smalloc(zip_size);
			no_scratch = true;
		}
		PERFINFO_AUTO_STOP();
	}

	// Do the actual compression.
	context->zStreamDeflate->next_in = (void *)src;
	context->zStreamDeflate->avail_in = src_len;
	context->zStreamDeflate->next_out = zip_data;
	context->zStreamDeflate->avail_out = zip_size;
	ret = deflate(context->zStreamDeflate, Z_FINISH);
	assert(ret == Z_STREAM_END);

	// Allocate the result buffer and copy the result into it.
	// If we weren't using a temporary buffer, just resize the output allocation to the correct size.
	zip_size = context->zStreamDeflate->total_out;
	if (no_scratch)
		result = realloc(zip_data, zip_size);
	else
	{
		if (special_heap)
			result = malloc_timed_canfail(zip_size, special_heap, false MEM_DBG_PARMS_CALL);
		else
			result = smalloc(zip_size);
		memcpy(result, zip_data, zip_size);
		if (scratch_alloc)
			ScratchFree(zip_data);
	}
	*zip_size_p = zip_size;

	PERFINFO_AUTO_STOP();

	return result;
}

// Optimization/tuning: Free resources associated with this thread's zip context.
// WARNING: This function is called by the thread destructor, so it must abide by the same restrictions as DllMain().
// Before changing this function, please carefully study the documentation for AtThreadExitInThread().
void clearZipData()
{
	ZipContext *context;
	bool free_context = false;

	// If we've never done a zutils TLS operation, return.
	if (tls_index == TLS_OUT_OF_INDEXES)
		return;
	
	// Get the context.
	if (!use_tls_segment || !IsUsingVista())
	{
		context = TlsGetValue(tls_index);
		TlsSetValue(tls_index, 0);
		free_context = true;
	}
	else
		context = acquirePerThreadZipData();

	// Free the context.
	if (context)
	{
		if (context->zStreamDeflate)
		{
			deflateEnd(context->zStreamDeflate);
			free(context->zStreamDeflate);
		}
		if (context->zStreamInflate)
		{
			inflateEnd(context->zStreamInflate);
			free(context->zStreamInflate);
		}
		if (free_context)
			free(context);
	}
}

// zlib doesn't provide a means to get the full uncompressed size,
// so here we go. 
size_t calcZipFileSize(const char *filename)
{
	unsigned char read_buffer[1024];
	size_t iTotalBytes = 0;
	size_t iBytesRead;
	FileWrapper *pFile;

	pFile = fopen(filename, "rbz");

	if(pFile)
	{
		while( (iBytesRead = fread(read_buffer, 1, 1024, pFile)) > 0 )
		{
			iTotalBytes += iBytesRead;
		}

		fclose(pFile);
	}

	return iTotalBytes;
}

// Internal debugging function
void debugZipTestContextAcquire()
{
	volatile static ZipContext *context;
	context = acquirePerThreadZipData();
}

#if 0
// Works fine, just not useful for us, so temporarily removing this to remove the linking against xcompress.lib

#pragma comment(lib, "xcompress.lib")

static XMEMCOMPRESSION_CONTEXT xContextCompress;
static XMEMDECOMPRESSION_CONTEXT xContextDecompress;
static CRITICAL_SECTION xCSCompress;
static CRITICAL_SECTION xCSDecompress;

static void xCompressEnter(void)
{
	if (!xContextCompress) {
		// Create a compression context.
		if (FAILED(XMemCreateCompressionContext(XMEMCODEC_DEFAULT, NULL, 0, &xContextCompress)))
		{
			assert(0);
		}
		InitializeCriticalSection(&xCSCompress);
	}
	EnterCriticalSection(&xCSCompress);
}

static void xCompressLeave(void)
{
	LeaveCriticalSection(&xCSCompress);
}

static void xDecompressEnter(void)
{
	if (!xContextDecompress) {
		// Create a compression context.
		if (FAILED(XMemCreateDecompressionContext(XMEMCODEC_DEFAULT, NULL, 0, &xContextDecompress)))
		{
			assert(0);
		}
		InitializeCriticalSection(&xCSDecompress);
	}
	EnterCriticalSection(&xCSDecompress);
}

static void xDecompressLeave(void)
{
	LeaveCriticalSection(&xCSDecompress);
}

static void *xCompressData_dbg(void *src, U32 src_len, U32 *zip_size_p MEM_DBG_PARMS)
{
	HRESULT hr = E_FAIL;
	size_t zip_size = src_len*1.02+64;
	void *zipped;

	zipped = smalloc(zip_size);

	xCompressEnter();
	hr = XMemCompress(xContextCompress, zipped, &zip_size, src, src_len);
	xCompressLeave();

	if (FAILED(hr)) {
		SAFE_FREE(zipped);
		zip_size = 0;
	} else {
		zipped = srealloc(zipped, zip_size);
	}
	*zip_size_p = (U32)zip_size;

	return zipped;
}

// Returns non-zero on failure
static int xUncompressData(char* outbuf, U32* outsize, void* inbuf, U32 insize)
{
	HRESULT hr = E_FAIL;
	size_t outsize_temp = *outsize;

	xDecompressEnter();
	hr = XMemDecompress(xContextDecompress, outbuf, &outsize_temp, inbuf, insize);
	xDecompressLeave();

	*outsize = (U32)outsize_temp;

	if (FAILED(hr))
		return 1;
	return 0;
}

static void zipSpeedTestInternal(char *buffer_in, int size, char *buffer_out)
{
	char *zipped;
	int zipsize;
	int unzipsize;

	loadstart_printf("Zlib compress... ");
	zipped = zipData(buffer_in, size, &zipsize);
	loadend_printf("done (%s).", friendlyBytes(zipsize));
	loadstart_printf("Zlib uncompress... ");
	unzipsize = size;
	unzipData(buffer_out, &unzipsize);
	loadend_printf("done.");
	free(zipped);

	loadstart_printf("XCompress... ");
	zipped = xCompressData_dbg(buffer_in, size, &zipsize MEM_DBG_PARMS_INIT);
	loadend_printf("done (%s).", friendlyBytes(zipsize));
	loadstart_printf("XDecompress... ");
	unzipsize = size;
	xUncompressData(buffer_out, &unzipsize, zipped, zipsize);
	loadend_printf("done.");
	free(zipped);
}

// AUTO_COMMAND;
void zipSpeedTest(int dummy)
{
	int i=0, j, k=0;
	int size = 25*1024*1024;
	char *buffer_in = calloc(size, 1);
	char *buffer_out = calloc(size, 1);
	loadstart_printf("ZipSpeedTest");
	for (j=0; j<5; j++)  {
		loadstart_printf("Random");
		for (i=0; i<size; i++) 
			buffer_in[i] = rand() & 0xff;
		zipSpeedTestInternal(buffer_in, size, buffer_out);
		loadend_printf("Random done.");
		loadstart_printf("Semi-sequential");
		k = 0;
		for (i=0; i<size; i++) {
			int r = rand();
			if (r < RAND_MAX / 3)
				k--;
			else if (r < RAND_MAX * 2.f / 3)
				k++;
			buffer_in[i] = k & 0xff;
		}
		zipSpeedTestInternal(buffer_in, size, buffer_out);
		loadend_printf("Semi-sequential done.");
		{
			int temp_size;
			char *temp = fileAlloc("texture_library/costumes/Avatars/male/M_Generic_Hips_Tight_03_N.wtex", &temp_size);
			loadstart_printf("Normal-map (%s)", friendlyBytes(temp_size));
			zipSpeedTestInternal(temp, temp_size, buffer_out);
			loadend_printf("Normal-map done.");
			free(temp);
		}
		{
			int temp_size;
			char *temp = fileAlloc("bin/DynFx.bin", &temp_size);
			loadstart_printf("Bin (%s)", friendlyBytes(temp_size));
			zipSpeedTestInternal(temp, temp_size, buffer_out);
			loadend_printf("Bin done.");
			free(temp);
		}
	}
	loadend_printf("ZipSpeedTest done.");
	free(buffer_in);
	free(buffer_out);
}

/*
	Results:
					PC		Xbox
Random (25MB did not compress down in either case)
	zlib compress	1.6		5.94
	XMemCompress	3.3		15.2
	zlib uncompress	0.05	0.10
	XMemDecompress	0.05	0.05
Semi-random (25MB down to 10.3MB zlib, 7.8MB XCompress)
	zlib compress	1.8		6.71
	XMemCompress	11.6	20.2
	zlib uncompress	0.18	0.70
	XMemDecompress	0.22	0.49
Normal-map texture (5.33MB down to 2.3MB zlib, 1.7MB XCompress)
	zlib compress	0.44	1.47
	XMemCompress	2.36	4.82
	zlib uncompress	0.06	0.17
	XMemDecompress	0.06	0.11
DynFX.bin (9.5MB down to 351KB zlib, 235kb XCompress)
	zlib compress	0.17	0.76
	XMemCompress	3.16	5.32
	zlib uncompress	0.03	0.11
	XMemDecompress	0.05	0.03
*/

#endif


//main-thread-to-worker-thread
enum
{
	ZIPCMD_ZIP = WT_CMD_USER_START,
	ZIPCMD_ZIP_DONE,

	ZIPCMD_ZIP_FILE,
	ZIPCMD_ZIP_FILE_DONE,

	ZIPCMD_UNZIP,
	ZIPCMD_UNZIP_DONE,
};


//stuff for background-thread zipping/unzipping:
WorkerThread *gpZipWorkerThread = NULL;

typedef struct ZipThread_ZipStruct
{
	void *pDataToZip;
	int iDataSize;
	ZipCallbackFunc pCB;
	void *pUserData;
} ZipThread_ZipStruct;

typedef struct ZipThread_ZipFileStruct
{
	char fileName[CRYPTIC_MAX_PATH];
	ZipFileCallbackFunc pCB;
	void *pUserData;
} ZipThread_ZipFileStruct;

typedef struct ZipThread_UnzipStruct
{
	UnzipCallbackFunc pCB;
	void *pUserData;
	void *pDataToUnzip;
	int iZippedSize;
	void *pUnzipBuffer;
	int iUnzipBufferSize;
} ZipThread_UnzipStruct;

typedef struct ZipThread_ZipResultStruct
{
	void *pOutData;
	U32 iOutDataSize;
	ZipCallbackFunc pCB;
	void *pUserData;
} ZipThread_ZipResultStruct;

typedef struct ZipThread_ZipFileResultStruct
{
	void *pOutData;
	U32 iOutDataSize;
	U32 iOutUncompressedSize;
	U32 iOutCRC;
	ZipFileCallbackFunc pCB;
	void *pUserData;
} ZipThread_ZipFileResultStruct;

typedef struct ZipThread_UnzipResultStruct
{
	UnzipCallbackFunc pCB;
	bool bSucceeded;
	void *pUserData;
	void *pZippedData;
	int iZippedDataSize;
	void *pUnzipBuffer;
	int iUnzipBufferSize;
	int iBytesUnzipped;
} ZipThread_UnzipResultStruct;




static void zipThread_Zip(void *user_data, void *data, WTCmdPacket *packet)
{
	ZipThread_ZipStruct *pZipStruct = (ZipThread_ZipStruct*)data;
	ZipThread_ZipResultStruct resultStruct = {0};
	resultStruct.pCB = pZipStruct->pCB;
	resultStruct.pUserData = pZipStruct->pUserData;
	resultStruct.pOutData = zipData(pZipStruct->pDataToZip, pZipStruct->iDataSize, &resultStruct.iOutDataSize);
	wtQueueMsg(gpZipWorkerThread, ZIPCMD_ZIP_DONE, &resultStruct, sizeof(resultStruct));
}


static void zipThread_ZipFile(void *user_data, void *data, WTCmdPacket *packet)
{
	ZipThread_ZipFileStruct *pZipStruct = (ZipThread_ZipFileStruct*)data;
	ZipThread_ZipFileResultStruct resultStruct = {0};
	char *pBuf;
	char tempFileName[CRYPTIC_MAX_PATH];
	resultStruct.pCB = pZipStruct->pCB;
	resultStruct.pUserData = pZipStruct->pUserData;

	if (fileIsAbsolutePath(pZipStruct->fileName))
	{
		strcpy(tempFileName, pZipStruct->fileName);
	}
	else
	{
		sprintf(tempFileName, "./%s", pZipStruct->fileName);
	}

	if (!(pBuf = fileAlloc(tempFileName, &resultStruct.iOutUncompressedSize)))
	{
		wtQueueMsg(gpZipWorkerThread, ZIPCMD_ZIP_FILE_DONE, &resultStruct, sizeof(resultStruct));
		return;
	}

	resultStruct.pOutData = zipData(pBuf, resultStruct.iOutUncompressedSize, &resultStruct.iOutDataSize);
	resultStruct.iOutCRC = cryptAdler32(pBuf, resultStruct.iOutUncompressedSize);
	free(pBuf);

	wtQueueMsg(gpZipWorkerThread, ZIPCMD_ZIP_FILE_DONE, &resultStruct, sizeof(resultStruct));
}

static void zipThread_Unzip(void *user_data, void *data, WTCmdPacket *packet)
{
	ZipThread_UnzipStruct *pUnzipStruct = (ZipThread_UnzipStruct*)data;
	ZipThread_UnzipResultStruct resultStruct = {0};
	resultStruct.pUnzipBuffer = pUnzipStruct->pUnzipBuffer;
	resultStruct.iZippedDataSize = pUnzipStruct->iZippedSize;
	resultStruct.pZippedData = pUnzipStruct->pDataToUnzip;
	resultStruct.iUnzipBufferSize = pUnzipStruct->iUnzipBufferSize;
	resultStruct.iBytesUnzipped = resultStruct.iUnzipBufferSize;
	resultStruct.pCB = pUnzipStruct->pCB;
	resultStruct.pUserData = pUnzipStruct->pUserData;

	resultStruct.bSucceeded = !unzipDataEx(resultStruct.pUnzipBuffer, &resultStruct.iBytesUnzipped, resultStruct.pZippedData, resultStruct.iZippedDataSize, true);

	wtQueueMsg(gpZipWorkerThread, ZIPCMD_UNZIP_DONE, &resultStruct, sizeof(resultStruct));
}



static void zipThread_ZipDone(void *user_data, void *data, WTCmdPacket *packet)
{
	ZipThread_ZipResultStruct *pResultStruct = (ZipThread_ZipResultStruct*)data;
	pResultStruct->pCB(pResultStruct->pOutData, pResultStruct->iOutDataSize, pResultStruct->pUserData);
}

static void zipThread_ZipFileDone(void *user_data, void *data, WTCmdPacket *packet)
{
	ZipThread_ZipFileResultStruct *pResultStruct = (ZipThread_ZipFileResultStruct*)data;
	pResultStruct->pCB(pResultStruct->pOutData, pResultStruct->iOutDataSize, pResultStruct->iOutUncompressedSize, pResultStruct->iOutCRC, pResultStruct->pUserData);
}

static void zipThread_UnzipDone(void *user_data, void *data, WTCmdPacket *packet)
{
	ZipThread_UnzipResultStruct *pResultStruct = (ZipThread_UnzipResultStruct*)data;
	pResultStruct->pCB(pResultStruct->bSucceeded, pResultStruct->pZippedData, pResultStruct->iZippedDataSize, pResultStruct->pUnzipBuffer, pResultStruct->iUnzipBufferSize, 
		pResultStruct->iBytesUnzipped, pResultStruct->pUserData);
}

static void LazyWorkerThreadInit(void)
{
	if (!gpZipWorkerThread)
	{
		gpZipWorkerThread = wtCreate(16, 16, NULL, "ZippingThread");
		wtRegisterCmdDispatch(gpZipWorkerThread, ZIPCMD_ZIP, zipThread_Zip);
		wtRegisterCmdDispatch(gpZipWorkerThread, ZIPCMD_ZIP_FILE, zipThread_ZipFile);
		wtRegisterCmdDispatch(gpZipWorkerThread, ZIPCMD_UNZIP, zipThread_Unzip);
	
		wtRegisterMsgDispatch(gpZipWorkerThread, ZIPCMD_ZIP_DONE, zipThread_ZipDone);
		wtRegisterMsgDispatch(gpZipWorkerThread, ZIPCMD_ZIP_FILE_DONE, zipThread_ZipFileDone);
		wtRegisterMsgDispatch(gpZipWorkerThread, ZIPCMD_UNZIP_DONE, zipThread_UnzipDone);

		wtSetThreaded(gpZipWorkerThread, true, 0, false);
		wtStart(gpZipWorkerThread);
	}
}


void ThreadedZip(void *pDataToZip, int iDataSize, ZipCallbackFunc pCB, void *pUserData)
{
	ZipThread_ZipStruct msgStruct = {0};
	msgStruct.pDataToZip = pDataToZip;
	msgStruct.iDataSize = iDataSize;
	msgStruct.pCB = pCB;
	msgStruct.pUserData = pUserData;
	LazyWorkerThreadInit();
	wtQueueCmd(gpZipWorkerThread, ZIPCMD_ZIP, &msgStruct, sizeof(msgStruct));
}

void ThreadedLoadAndZipFile(char *pFileNameToZip, ZipFileCallbackFunc pCB, void *pUserData)
{
	ZipThread_ZipFileStruct msgStruct = {0};
	msgStruct.pCB = pCB;
	msgStruct.pUserData = pUserData;
	strcpy(msgStruct.fileName, pFileNameToZip);
	LazyWorkerThreadInit();
	wtQueueCmd(gpZipWorkerThread, ZIPCMD_ZIP_FILE, &msgStruct, sizeof(msgStruct));

}

void ThreadedUnzip(void *pDataToUnzip, int iZippedSize, void *pUnzipBuffer, int iUnzipBufferSize, UnzipCallbackFunc pCB, void *pUserData)
{
	ZipThread_UnzipStruct msgStruct = {0};
	msgStruct.pCB = pCB;
	msgStruct.pUserData = pUserData;
	msgStruct.pDataToUnzip = pDataToUnzip;
	msgStruct.iZippedSize = iZippedSize;
	msgStruct.pUnzipBuffer = pUnzipBuffer;
	msgStruct.iUnzipBufferSize = iUnzipBufferSize;
	LazyWorkerThreadInit();
	wtQueueCmd(gpZipWorkerThread, ZIPCMD_UNZIP, &msgStruct, sizeof(msgStruct));
}


void zipTick_Internal(void)
{
	if (gpZipWorkerThread)
	{
		wtMonitor(gpZipWorkerThread);
	}
}



//not optimized, just utility
char *BufferToZippedEncodedString(void *pBuf, int iBufSize)
{
	void *pZipped;
	int iZippedSize;
	char *pEncodedEString = NULL;
	char *pRetVal;
	pZipped = zipData(pBuf, iBufSize, &iZippedSize);
	estrStackCreate(&pEncodedEString);
	estrBase64Encode(&pEncodedEString, pZipped, iZippedSize);
	estrInsertf(&pEncodedEString, 0, "%d_", iBufSize);
	pRetVal = strdup(pEncodedEString);
	free(pZipped);
	estrDestroy(&pEncodedEString);
	return pRetVal;	
}


void *EncodedZippedStringToBuffer(char *pString, int *piBufSize)
{
	int iInLen = (int)strlen(pString);
	char *pUnderScore = strchr(pString, '_');
	int iOriginalSize;
	char *pOutBuf;
	char *pZippedBuf = NULL;
	
	if (!pUnderScore)
	{
		return NULL;
	}

	iOriginalSize = atoi(pString);
	pOutBuf = malloc(iOriginalSize);
	estrStackCreate(&pZippedBuf);
	estrBase64Decode(&pZippedBuf, pUnderScore + 1, iInLen - (pUnderScore - pString) - 1);
	unzipData(pOutBuf, &iOriginalSize, pZippedBuf, estrLength(&pZippedBuf));

	estrDestroy(&pZippedBuf);
	if (piBufSize)
	{
		*piBufSize = iOriginalSize;
	}

	return pOutBuf;
}






