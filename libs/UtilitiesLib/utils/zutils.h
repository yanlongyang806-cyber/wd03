#pragma once

typedef struct WorkerThread WorkerThread;

// Unzip data
#define unzipData(outbuf, outsize, inbuf, insize) unzipDataEx(outbuf, outsize, inbuf, insize, false)
int unzipDataEx(SA_PRE_GOOD SA_POST_NN_ELEMS_VAR(*outsize) char* outbuf, SA_PARAM_NN_VALID U32* outsize, SA_PRE_NN_RELEMS_VAR(insize) const void* inbuf, U32 insize, bool never_sleep);

// Unzip data in pieces
void unzipStreamInit(void);
int unzipStream(SA_PRE_GOOD SA_POST_NN_ELEMS_VAR(*outsize) char* outbuf, SA_PARAM_NN_VALID U32* outsize, SA_PRE_NN_RELEMS_VAR(insize) const void* inbuf, U32 insize, bool never_sleep);
U32 unzipStreamTell(void);
U32 unzipStreamConsumed(void);
S32 unzipStreamRemaining(void);

// Zip data
SA_RET_NN_VALID void *zipDataEx_dbg(SA_PRE_NN_RBYTES_VAR(src_len) const void *src, U32 src_len, SA_PRE_NN_FREE SA_POST_NN_VALID U32 *zip_size_p, int level, bool rsyncable, int special_heap MEM_DBG_PARMS);
#define zipData(src, src_len, zip_size_p) zipDataEx_dbg(src, src_len, zip_size_p, 9, false, 0 MEM_DBG_PARMS_INIT)
#define zipDataEx(src, src_len, zip_size_p, level, rsyncable, special_heap) zipDataEx_dbg(src, src_len, zip_size_p, level, rsyncable, special_heap MEM_DBG_PARMS_INIT)

// Optimization/tuning: Free resources associated with this thread's zip context.
void clearZipData(void);

size_t calcZipFileSize(const char *filename);

// Internal debugging function
void debugZipTestContextAcquire(void);
extern WorkerThread *gpZipWorkerThread;


void zipTick_Internal(void);
static __forceinline void zipTick(void)
{
	if (gpZipWorkerThread)
	{
		zipTick_Internal();
	}
}

typedef void (*ZipCallbackFunc)(void *pData, int iDataSize, void *pUserData);
typedef void (*ZipFileCallbackFunc)(void *pData, int iDataSize, int iUncompressedDataSize, U32 iCRC, void *pUserData);

//returns all the buffers and sizes involved, just because, why not?
typedef void (*UnzipCallbackFunc)(bool bSucceeded, void *pZippedData, int iZippedSize, void *pUnzipBuffer, int iUnzipBufferSize, int iBytesUnzipped,
	void *pUserData);

void ThreadedZip(void *pDataToZip, int iDataSize, ZipCallbackFunc pCB, void *pUserData);
void ThreadedLoadAndZipFile(char *pFileNameToZip, ZipFileCallbackFunc pCB, void *pUserData);
void ThreadedUnzip(void *pDataToUnzip, int iZippedSize, void *pUnzipBuffer, int iUnzipBufferSize, UnzipCallbackFunc pCB, void *pUserData);


//not optimized, just utility
SA_ORET_OP_STR char *BufferToZippedEncodedString(SA_PARAM_NN_VALID void *pBuf, int iBufSize);
SA_ORET_OP_VALID void *EncodedZippedStringToBuffer(SA_PARAM_NN_STR char *pString, SA_PRE_GOOD SA_POST_OP_VALID int *piBufSize);

