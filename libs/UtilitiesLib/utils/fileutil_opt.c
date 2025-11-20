#include "fileutil.h"
#include "StringUtil.h"

AUTO_RUN_ANON(memBudgetAddMapping(__FILE__, BUDGET_EngineMisc););

static bool cwd_is_fixed = false;  // If true, assume cwd never changes.

char * fileGetcwd(char * _DstBuf, int _SizeInBytes)
{
#if _PS3
	strcpy_s( _DstBuf, _SizeInBytes, "/app_home/" );
	return _DstBuf;
#elif _XBOX
	strcpy_s( _DstBuf, _SizeInBytes, "game:\\" );
	return _DstBuf;
#else

	// When called in a tight loop, fileGetcwd() is actually quite slow.  Caching it here is much easier
	// than trying to raise it, since the loop is frequently in code that has nothing to do with the
	// filesystem.  For instance, the hog system calls this once per hog operation to form the mutex
	// name.
	if (cwd_is_fixed)
	{
		static char cwd[MAX_PATH];
		static size_t len;
		S16 widecwd[MAX_PATH];
		size_t end;
		ATOMIC_INIT_BEGIN;
		_wgetcwd(widecwd, ARRAY_SIZE(widecwd));
		WideToUTF8StrConvert(widecwd, cwd, ARRAY_SIZE(cwd));
		len = strlen(cwd);
		ATOMIC_INIT_END;
		end = MIN(len, (size_t)_SizeInBytes - 1);
		memcpy(_DstBuf, cwd, end);
		_DstBuf[end] = 0;
		return _DstBuf;
	}

	{
		S16 widecwd[MAX_PATH];
		_wgetcwd(widecwd, ARRAY_SIZE(widecwd));
		WideToUTF8StrConvert(widecwd, _DstBuf, _SizeInBytes);
		return _DstBuf;
	}


#endif
}

// If true, assume that the cwd never changes.
// WARNING: Subtle, yet horrible bugs will result if you set this when it is not, in fact, true.
void fileSetFixedCwd(bool fixed)
{
	cwd_is_fixed = fixed;
}







//anything added after this will inherit the #undefs and not use cryptic file system stuff
#undef fgetc
#undef fputc
#undef fseek
#undef fopen
#undef fclose
#undef ftell
#undef fread
#undef fwrite
#undef fgets
#undef fprintf
#undef fflush
#undef setvbuf
#undef FILE

//NOTE NOTE make sure to keep these in sync with the prototypes in file.c
FileWritingBuffer *FileWritingBuffer_Create(void)
{
	return calloc(sizeof(FileWritingBuffer), 1);
}

void FileWritingBuffer_Destroy(FileWritingBuffer **ppBuffer)
{
	if (ppBuffer && *ppBuffer)
	{
		free(*ppBuffer);
		*ppBuffer = NULL;
	}
}

void FileWritingBuffer_FlushOldestFullBuffer(FileWritingBuffer *pBuffer, FILE *pFile)
{
	int iOldBufferIndex;
	if (!pBuffer->iNumFullBuffers)
	{
		return;
	}

	iOldBufferIndex = (pBuffer->iCurActiveBuffer + FWB_NUM_BUFFERS - pBuffer->iNumFullBuffers) % FWB_NUM_BUFFERS;
	fwrite(pBuffer->buffers[iOldBufferIndex], FW_BUFFER_SIZE, 1, pFile);
	pBuffer->iCurrentSeekInRealFile += FW_BUFFER_SIZE;
	pBuffer->iNumFullBuffers--;
}

void FileWritingBuffer_Flush(FileWritingBuffer *pBuffer, FILE *pFile)
{
	while (pBuffer->iNumFullBuffers)
	{
		FileWritingBuffer_FlushOldestFullBuffer(pBuffer, pFile);
	}

	if (pBuffer->iBytesWrittenInActiveBuffer)
	{
		fwrite(pBuffer->buffers[pBuffer->iCurActiveBuffer], pBuffer->iBytesWrittenInActiveBuffer, 1, pFile);
		pBuffer->iCurrentSeekInRealFile += pBuffer->iBytesWrittenInActiveBuffer;
	}

	pBuffer->iBytesWrittenInActiveBuffer = 0;
}

void FileWritingBuffer_MaybeJustFilledABlock(FileWritingBuffer *pBuffer, FILE *pFile)
{
	if (pBuffer->iBytesWrittenInActiveBuffer == FW_BUFFER_SIZE)
	{
		pBuffer->iNumFullBuffers++;
		pBuffer->iBytesWrittenInActiveBuffer = 0;
		pBuffer->iCurActiveBuffer++;
		pBuffer->iCurActiveBuffer %= FWB_NUM_BUFFERS;

		if (pBuffer->iNumFullBuffers == FWB_NUM_BUFFERS - 1)
		{
				FileWritingBuffer_FlushOldestFullBuffer(pBuffer, pFile);
		}
	}

}

int FileWritingBuffer_PutC(FileWritingBuffer *pBuffer, FILE *pFile, char c)
{
	pBuffer->buffers[pBuffer->iCurActiveBuffer][pBuffer->iBytesWrittenInActiveBuffer] = c;
	pBuffer->iBytesWrittenInActiveBuffer++;


	return c;
}


S64 FileWritingBuffer_Tell(FileWritingBuffer *pBuffer)
{
	return pBuffer->iCurrentSeekInRealFile + pBuffer->iBytesWrittenInActiveBuffer + pBuffer->iNumFullBuffers * FW_BUFFER_SIZE;
}

S64 FileWritingBuffer_Seek(FileWritingBuffer *pBuffer, FILE *pFile, S64 dist,int whence)
{
	S64 iAbsoluteSeekValue;
	S64 iCurTell = FileWritingBuffer_Tell(pBuffer);
	S64 iAmountToBackUp;
	S64 iAmountInBufferAfterBackup;
	int iFullBuffersAfterBackup;
	int iFullBufferDiff;
	int ret;

	switch(whence)
	{
	xcase SEEK_SET:
		iAbsoluteSeekValue = dist;

	xcase SEEK_CUR:
	case SEEK_END:
	default:
		iAbsoluteSeekValue = iCurTell + dist;
	}

	//if we are seeking to before what we have in RAM, then we abandon all our buffers, seek in the file and start over
	if (iAbsoluteSeekValue < pBuffer->iCurrentSeekInRealFile)
	{
		pBuffer->iBytesWrittenInActiveBuffer = pBuffer->iNumFullBuffers = 0;

		ret = fseek(pFile, iAbsoluteSeekValue, SEEK_SET);

		if (ret == 0)
		{
			//success
			pBuffer->iCurrentSeekInRealFile = iAbsoluteSeekValue;
		}
		else
		{
			pBuffer->iCurrentSeekInRealFile = ftell(pFile);
		}

		return ret;
	}

	//if we are trying to seek past the end of the file, do nothing
	if (iAbsoluteSeekValue > iCurTell)
	{
		//not clear to me if this should be a failure or not, hopefully doesn't matter
		return -1;
	}

	if (iAbsoluteSeekValue == iCurTell)
	{
		return 0;
	}

	iAmountToBackUp = iCurTell - iAbsoluteSeekValue;
	iAmountInBufferAfterBackup = pBuffer->iBytesWrittenInActiveBuffer + pBuffer->iNumFullBuffers * FW_BUFFER_SIZE - iAmountToBackUp;
	iFullBuffersAfterBackup = iAmountInBufferAfterBackup / FW_BUFFER_SIZE;
	if (iFullBuffersAfterBackup == pBuffer->iNumFullBuffers)
	{
		pBuffer->iBytesWrittenInActiveBuffer = iAmountInBufferAfterBackup - iFullBuffersAfterBackup * FW_BUFFER_SIZE;
		return 0;
	}

	iFullBufferDiff = pBuffer->iNumFullBuffers - iFullBuffersAfterBackup;
	pBuffer->iNumFullBuffers -= iFullBufferDiff;
	pBuffer->iCurActiveBuffer = (pBuffer->iCurActiveBuffer + FWB_NUM_BUFFERS - iFullBufferDiff) % FWB_NUM_BUFFERS;
	pBuffer->iBytesWrittenInActiveBuffer = iAmountInBufferAfterBackup - iFullBuffersAfterBackup * FW_BUFFER_SIZE;

	return 0;

}


intptr_t FileWritingBuffer_Fwrite(FileWritingBuffer *pBuffer, FILE *pFile, const char *pBufferToWrite, size_t size1, size_t size2)
{
	S64 iTotalSize = size1 * size2;
	U32 iSpaceInCurBlock;

	//any time we're writing more than one buffer's worth of stuff, we flush and then just fwrite it, we're never going to be seeking back into
	//a block this big
	if (iTotalSize > FW_BUFFER_SIZE)
	{
		intptr_t ret;
		FileWritingBuffer_Flush(pBuffer, pFile);
		ret = fwrite(pBufferToWrite, size1, size2, pFile);
		pBuffer->iCurrentSeekInRealFile += ret * size1;
		return ret;
	}

	//easiest case... the whole thing fits in the current block we're writing
	iSpaceInCurBlock = FW_BUFFER_SIZE - pBuffer->iBytesWrittenInActiveBuffer;
	if (iSpaceInCurBlock >= iTotalSize)
	{
		memcpy(pBuffer->buffers[pBuffer->iCurActiveBuffer] + pBuffer->iBytesWrittenInActiveBuffer, pBufferToWrite, iTotalSize);
		pBuffer->iBytesWrittenInActiveBuffer += iTotalSize;
		
		FileWritingBuffer_MaybeJustFilledABlock(pBuffer, pFile);
	

		return size2;
	}
	else
	{
		int iOverflowBytes = iTotalSize - iSpaceInCurBlock;
		
		//need to write part into one block, part into the next
		memcpy(pBuffer->buffers[pBuffer->iCurActiveBuffer] + pBuffer->iBytesWrittenInActiveBuffer, pBufferToWrite, iSpaceInCurBlock);
		pBuffer->iBytesWrittenInActiveBuffer += iSpaceInCurBlock;
		FileWritingBuffer_MaybeJustFilledABlock(pBuffer, pFile);

		memcpy(pBuffer->buffers[pBuffer->iCurActiveBuffer], pBufferToWrite + iSpaceInCurBlock, iOverflowBytes);
		pBuffer->iBytesWrittenInActiveBuffer += iOverflowBytes;

		return size2;
	}
}


