#define NO_MEMCHECK_OK // to allow including stdtypes.h in headers

#include "PhysicsSDKPrivate.h"

#if !PSDK_DISABLED

#include "PhysXLoader.h"
#include "NxPhysics.h"
#include "NxPhysicsSDK.h"
#include "NxMaterial.h"
#include "NxScene.h"

#include "NxUserOutputStream.h"

// After Novodex, before any of ours
#undef NO_MEMCHECK_OK
#undef NO_MEMCHECK_H
#include "memcheck.h"
#include "timing.h"

#include "error.h"
#include "utils.h"
#include "wininclude.h"
#include "file.h"

// -------------------------------------------------------------------------------------------------------------------
// OUTPUT STREAM
// -------------------------------------------------------------------------------------------------------------------

struct PSDKOutputStream : public NxUserOutputStream
{
	/**
	Reports an error code.

	\param code Error code, see #NxErrorCode
	\param message Message to display.
	\param file File error occured in.
	\param line Line number error occured on.
	*/
	void reportError(NxErrorCode code, const char * message, const char *file, int line)
	{
		#if 0
			if ( code < NXE_DB_INFO )
			{
				FatalErrorf("Error %d: %s... %s line %d\n", code, message, file, line);
			}
			else // non-fatal error
		#endif
		{
			//assertmsg(0, message);

			#if 0
				Errorf(	"PhysX error %d, thread %d: %s... %s line %d\n",
						code,
						GetCurrentThreadId(),
						message,
						file,
						line);
			#endif

			if(!psdkState.flags.disableErrors){
				if(!strStartsWith(message, "Creating static compound shape")){
					printfColor(COLOR_BRIGHT|COLOR_RED|COLOR_GREEN,
								"PhysX error %d, thread %d: %s (%s:%d).\n",
								code,
								GetCurrentThreadId(),
								message,
								file,
								line);
				}
			}
		}
	}

	/**
	Reports an assertion violation.  The user should return 

	\param message Message to display.
	\param file File error occured in.
	\param line Line number error occured on.
	*/
	NxAssertResponse reportAssertViolation(const char * message, const char *file, int line) 
	{
		//printf("%s... %s line %d\n", message, file, line);
		//assert(0);
		FatalErrorf("%s... %s line %d\n", message, file, line);
		return NX_AR_BREAKPOINT;
	}

	/**
	Simply prints some debug text

	\param message Message to display.
	*/
	void print(const char * message)
	{
		printf("%s", message);
	}
};

NxUserOutputStream* psdkGetOutputStream(void){
	static PSDKOutputStream* psdkStream = new PSDKOutputStream;
	
	return psdkStream;
}


// -------------------------------------------------------------------------------------------------------------------
// MEMORY ALLOCATION
// -------------------------------------------------------------------------------------------------------------------

struct PSDKAllocator : public NxUserAllocator
{
	PERFINFO_TYPE*	perfInfo;
	const char*		heapPerfInfoName;
	
	PSDKAllocator() :
		heapPerfInfoName("NxHeap"),
		perfInfo(NULL)
	{
	}

	virtual void* mallocDEBUG(size_t size, const char* fileName, int line)
	{
		PERFINFO_AUTO_START_STATIC(heapPerfInfoName, &perfInfo, 1);
#if _XBOX
		const int PHYSX_ALLOC_ALIGNMENT = 16;
		void* result = aligned_malloc_dbg(size, PHYSX_ALLOC_ALIGNMENT, fileName, line);
#else
		void* result = _malloc_dbg(size, 1, fileName, line);
#endif
		PERFINFO_AUTO_STOP();
		return result;
	}
#pragma push_macro("malloc")
#pragma push_macro("free")
#pragma push_macro("realloc")
	#undef malloc
	#undef free
	#undef realloc

	virtual void* malloc(size_t size)
	{
		return mallocDEBUG(size, __FILE__, __LINE__);
	}

	virtual void* realloc(void* memory, size_t size)
	{
		PERFINFO_AUTO_START_STATIC(heapPerfInfoName, &perfInfo, 1);
		void* result = _realloc_dbg(memory, size, 1, __FILE__, __LINE__);
		PERFINFO_AUTO_STOP();
		return result;
	}

	virtual void free(void* memory)
	{
		PERFINFO_AUTO_START_STATIC(heapPerfInfoName, &perfInfo, 1);
		_free_dbg(memory, 1);
		PERFINFO_AUTO_STOP();
	}
#pragma pop_macro("malloc")
#pragma pop_macro("free")
#pragma pop_macro("realloc")
};

static PSDKAllocator* psdkAllocator;

NxUserAllocator* psdkGetAllocator(void){
	if(!psdkAllocator){
		psdkAllocator = new PSDKAllocator;
	}
	return psdkAllocator;
}














// -------------------------------------------------------------------------------------------------------------------
// SHARED STREAM MEMORY
// -------------------------------------------------------------------------------------------------------------------

NxU8* PSDKSharedStream::pTempBuffer;
NxU32 PSDKSharedStream::uiBufferSize;
NxU8* PSDKSharedStream::pCursor;

PSDKSharedStream::PSDKSharedStream()
{
	// allocate a temp buffer
	if ( !pTempBuffer )
	{
		// 1 k for now...
		uiBufferSize = 1024;
		pTempBuffer = (NxU8*)malloc(uiBufferSize);
	}

	pCursor = pTempBuffer;

#ifdef PSDK_STREAM_DEBUG
	sdContents = NULL;
	sdFile = NULL;
	sdContentsCursor = NULL;
	if(!sdFile)
	{
		char filename[MAX_PATH];
		sprintf(filename, "%s\\allocstream-shared.txt", fileTempDir());
		if(fileExists(filename))
		{
			sdContents = (char*)fileAlloc(filename, &sdContentsLen);
			sdContentsCursor = sdContents;
		}
		else
			sdFile = (FILE*)fopen(filename, "wb");
	}
#endif
}

PSDKSharedStream::~PSDKSharedStream()
{
#ifdef PSDK_STREAM_DEBUG
	if(sdFile)
		fclose(sdFile);
#endif
}

bool PSDKSharedStream::isValid() const
{
	return !!pCursor;
}

void PSDKSharedStream::resetByteCounter() 
{
	pCursor = pTempBuffer;
}

void PSDKSharedStream::checkTempBufferSize( NxU32 size )
{
	if ( pCursor + size >= pTempBuffer + uiBufferSize)
	{
		NxU8* pOldBuffer = pTempBuffer;
		NxU32 uiOldBufferSize = uiBufferSize;

		// we must grow temp buffer, by 50% for now
		NxU32 uiGrowth = uiOldBufferSize >> 1;
		while ( uiGrowth < size ) //just in case, will probably never happen
			uiGrowth <<= 1; // double it

		uiBufferSize = uiOldBufferSize + uiGrowth;
		pTempBuffer = (NxU8*)malloc(uiBufferSize);
		assert(pTempBuffer);

		memcpy(pTempBuffer, pOldBuffer, (pCursor - pOldBuffer));

		pCursor += ( pTempBuffer - pOldBuffer );

		free(pOldBuffer);
	}
}

void PSDKSharedStream::freeTempBuffer()
{
	SAFE_FREE( pTempBuffer );
}

NxU8* PSDKSharedStream::getBuffer()
{
	return pTempBuffer;
}

int	PSDKSharedStream::getBufferSize()
{
	return pCursor - pTempBuffer;
}

#define CREATE_READ(type, name) type PSDKSharedStream::name() const {type a; readBuffer(&a, sizeof(a)); return a;}
	CREATE_READ(NxU8, readByte);
	CREATE_READ(NxU16, readWord);
	CREATE_READ(NxU32, readDword);
	CREATE_READ(NxF32, readFloat);
	CREATE_READ(NxF64, readDouble);
#undef CREATE_READ

void PSDKSharedStream::readBuffer(void* buffer, NxU32 size)	const
{
	memcpy(buffer, pCursor, size);
	
    pCursor += size;
}

#define CREATE_STORE(type, name) NxStream& PSDKSharedStream::name(type a){return storeBuffer(&a, sizeof(a));}
	CREATE_STORE(NxU8, storeByte);
	CREATE_STORE(NxU16, storeWord);
	CREATE_STORE(NxU32, storeDword);
	CREATE_STORE(NxReal, storeFloat);
	CREATE_STORE(NxF64, storeDouble);
#undef CREATE_STORE

NxStream& PSDKSharedStream::storeBuffer(const void* buffer, NxU32 size)
{
#ifdef PSDK_STREAM_DEBUG
	if(sdContents)	// compare
	{
		static int x = 0;
		NxU32 i;
		for(i=0; i<size; i++)
		{
			if(*(sdContentsCursor+i)!=*(((char*)buffer)+i))
				x++;
		}
	}
	else			// write
		fwrite(buffer, size, 1, sdFile);
#endif

	checkTempBufferSize(size);
	memcpy(pCursor, buffer, size);
	pCursor += size;

#ifdef PSDK_STREAM_DEBUG
	sdContentsCursor += size;
#endif

	return *this;
}



// -------------------------------------------------------------------------------------------------------------------
// NON-SHARED STREAM MEMORY
// -------------------------------------------------------------------------------------------------------------------

PSDKAllocedStream::PSDKAllocedStream()
{
	// allocate a temp buffer
	// 1 k for now...
	uiBufferSize = 1024;
	pTempBuffer = (NxU8*)malloc(uiBufferSize);

	pCursor = pTempBuffer;

#ifdef PSDK_STREAM_DEBUG
	sdContents = NULL;
	sdFile = NULL;
	sdContentsCursor = NULL;
	byteCount = 0;
	if(!sdFile)
	{
		char filename[MAX_PATH];
		sprintf(filename, "%s\\allocstream.txt", fileTempDir());
		if(fileExists(filename))
		{
			sdContents = (char*)fileAlloc(filename, &sdContentsLen);
			sdContentsCursor = sdContents;
		}
		else
		{
			sdFile = (FILE*)fopen(filename, "wb");
			sprintf(filename, "%s\\allocstream_read.txt", fileTempDir());
		}
	}
#endif
}

PSDKAllocedStream::PSDKAllocedStream(void* buffer, NxU32 size)
{
	// take ownership of the buffer
	uiBufferSize = size;
	pTempBuffer = (NxU8*)buffer;

	pCursor = pTempBuffer + size;

#ifdef PSDK_STREAM_DEBUG
	sdContents = NULL;
	sdFile = NULL;
	sdContentsCursor = NULL;
	byteCount = 0;
	if(!sdFile)
	{
		char filename[MAX_PATH];
		sprintf(filename, "%s\\allocstream.txt", fileTempDir());
		if(fileExists(filename))
		{
			sdContents = (char*)fileAlloc(filename, &sdContentsLen);
			sdContentsCursor = sdContents;
		}
		else
		{
			sdFile = (FILE*)fopen(filename, "wb");
			sprintf(filename, "%s\\allocstream_read.txt", fileTempDir());
		}
	}
#endif
}

PSDKAllocedStream::~PSDKAllocedStream()
{
	SAFE_FREE(pTempBuffer);

#ifdef PSDK_STREAM_DEBUG
	if(sdFile)
	{
		fclose(sdFile);
	}
#endif
}

bool PSDKAllocedStream::isValid() const
{
	return !!pCursor;
}

void PSDKAllocedStream::resetByteCounter() 
{
	pCursor = pTempBuffer;
}

void PSDKAllocedStream::checkTempBufferSize( NxU32 size )
{
	if ( pCursor + size >= pTempBuffer + uiBufferSize)
	{
		NxU8* pOldBuffer = pTempBuffer;
		NxU32 uiOldBufferSize = uiBufferSize;

		// we must grow temp buffer, by 50% for now
		NxU32 uiGrowth = uiOldBufferSize >> 1;
		while ( uiGrowth < size ) //just in case, will probably never happen
			uiGrowth <<= 1; // double it

		uiBufferSize = uiOldBufferSize + uiGrowth;
		pTempBuffer = (NxU8*)malloc(uiBufferSize);
		assert(pTempBuffer);

		memcpy(pTempBuffer, pOldBuffer, (pCursor - pOldBuffer));

		pCursor += ( pTempBuffer - pOldBuffer );

		free(pOldBuffer);
	}
}

NxU8* PSDKAllocedStream::getBuffer()
{
	return pTempBuffer;
}

int	PSDKAllocedStream::getBufferSize()
{
	return pCursor - pTempBuffer;
}

void* PSDKAllocedStream::stealBuffer()
{
	void *pBuffer = pTempBuffer;
	pTempBuffer = NULL;
	resetByteCounter();
	return pBuffer;
}

void PSDKAllocedStream::freeBuffer(void *data)
{
    if(data)
        free(data);
}

#define CREATE_READ(type, name) type PSDKAllocedStream::name() const {type a; readBuffer(&a, sizeof(a)); return a;}
	CREATE_READ(NxU8, readByte);
	CREATE_READ(NxU16, readWord);
	CREATE_READ(NxU32, readDword);
	CREATE_READ(NxF32, readFloat);
	CREATE_READ(NxF64, readDouble);
#undef CREATE_READ

void PSDKAllocedStream::readBuffer(void* buffer, NxU32 size)	const
{
	memcpy(buffer, pCursor, size);
	((PSDKAllocedStream*)this)->pCursor += size;
}

#define CREATE_STORE(type, name) NxStream& PSDKAllocedStream::name(type a){return storeBuffer(&a, sizeof(a));}
	CREATE_STORE(NxU8, storeByte);
	CREATE_STORE(NxU16, storeWord);
	CREATE_STORE(NxU32, storeDword);
	CREATE_STORE(NxReal, storeFloat);
	CREATE_STORE(NxF64, storeDouble);
#undef CREATE_STORE

NxStream& PSDKAllocedStream::storeBuffer(const void* buffer, NxU32 size)
{
#ifdef PSDK_STREAM_DEBUG
	if(sdContents)	// compare
	{
		static int x = 0;
		NxU32 i;
		for(i=0; i<size; i++)
		{
			if(*(sdContentsCursor+i)!=*(((char*)buffer)+i))
				x++;
		}
	}
	else			// write
		fwrite(buffer, size, 1, sdFile);
#endif

	checkTempBufferSize(size);
	memcpy(pCursor, buffer, size);
	pCursor += size;

#ifdef PSDK_STREAM_DEBUG
	sdContentsCursor += size;
#endif

	return *this;
}

#endif