/***************************************************************************
 
 
 
 ***************************************************************************/

#include "PigFileWrapper.h"
#include <string.h>
#include "piglib.h"
#include "hoglib.h"

#include "utils.h"
#include "zutils.h"
//#define WIN32_LEAN_AND_MEAN
#include "wininclude.h"
#include "timing.h"

AUTO_RUN_ANON(memBudgetAddMapping(__FILE__, BUDGET_Unsorted);); // TODO: Track memory usage by caller

enum {
	PIG_FLAG_EOF = 1 << 0,
	PIG_FLAG_NBUF = 1 << 1,
};

#define DEFAULT_BUFFER_SIZE 2048

struct PigFileHandle {
	PigFileDescriptor data;
	U32 pos;
	U8 flags; //EOF - any others?
	U8 *buffer; // Buffered data for zipped files and fgetc() calls
	U32 bufferlen; // Amount of data in the buffer
	U32 bufferpos; // Position in the file that the buffer begins at
	U32 last_used_milliseconds; // For freeing zipped data buffer
};

#define MAX_FILES 2048
int pig_file_handles_max=0;
PigFileHandle pig_file_handles[MAX_FILES];

static bool inited=false;
static CRITICAL_SECTION pigFileWrapperCritsec;
void initPigFileHandles(void) {
	if (!inited) {
		InitializeCriticalSection(&pigFileWrapperCritsec);
		memset(pig_file_handles, 0, sizeof(PigFileHandle)*ARRAY_SIZE(pig_file_handles));
		pig_file_handles_max = 0;
		inited = true;
	}
}

PigFileHandle *createPigFileHandle() {
	PigFileHandle *fh = NULL;
	int i;

	initPigFileHandles();

	EnterCriticalSection(&pigFileWrapperCritsec);

	for (i=0; i<ARRAY_SIZE(pig_file_handles); i++) {
		if (pig_file_handles[i].data.parent_hog==NULL) {
			fh = &pig_file_handles[i];
			fh->data.parent_hog = (void *)1;
			fh->pos = 0;
			fh->flags = 0;
			fh->bufferlen = 0;
			fh->last_used_milliseconds = timeGetTime();
			assert(fh->buffer == NULL);
			fh->buffer = NULL;
			pig_file_handles_max = MAX(pig_file_handles_max, i);
			LeaveCriticalSection(&pigFileWrapperCritsec);
			return fh;
		}
	}
	LeaveCriticalSection(&pigFileWrapperCritsec);
	assert(!"Ran out of pig_file_handles!");
	return NULL;
}


void *pig_fopen(const char *name,const char *how)
{
	PigFileDescriptor pfd;
	int pig_index = -1;
	int file_index = -1;
	assert(0);
	// This function does not work, we would need to search through the individual pigs to
	//  find this file.  Instead, use the FolderCache

	pfd = PigSetGetFileInfo(pig_index, file_index, name);

	return pig_fopen_pfd(&pfd, how);
}

void *pig_fopen_pfd(PigFileDescriptor *pfd,const char *how) {
	PigFileHandle *pfh;

	if (strcspn(how, "wWaA+")!=strlen(how)) {
		assert(!"Write accessed asked for on a pigged file!");
		return NULL;
	}

	if (!pfd || !pfd->parent_hog) {
		assert(!"Bad handle passed to pig_fopen_pfd");
		return NULL;
	}

	pfh = createPigFileHandle();
	pfh->data = *pfd;
	if (pfh->pos == pfd->size) { // == 0
		pfh->flags |= PIG_FLAG_EOF;
	}

	if (hogFileIsZipped(pfh->data.parent_hog, pfh->data.file_index)) {
		unzipStreamInit();
	}

	if (pig_debug)
		OutputDebugStringf("PIG: fopen(%s)\r\n", pfh->data.debug_name);

	return (void *)pfh;
}


int pig_fclose(PigFileHandle *handle) {
	HogFile *parent_hog = handle->data.parent_hog;
	assertmsg(handle->data.parent_hog, "Tried to close an already closed file");
	if (handle->buffer) {
		EnterCriticalSection(&pigFileWrapperCritsec);
		SAFE_FREE(handle->buffer);
		LeaveCriticalSection(&pigFileWrapperCritsec);
	}
	// Must do this last!
	handle->data.parent_hog = (void *)0;
	if (handle->data.release_hog_on_close)
		hogFileDestroy(parent_hog, true);
	return 0;
}

int pig_fseek(PigFileHandle *handle,long dist,int whence) {
	long newpos = handle->pos;
	switch(whence) {
	case SEEK_SET:
		assert(dist>=0);
		newpos = dist;
		break;
	case SEEK_CUR:
		newpos += dist;
		break;
	case SEEK_END:
		newpos = handle->data.size + dist;
		break;
	default:
		assert(false);
	}
	if (newpos > (long)handle->data.size) {
		if (pig_debug)
			printf("warning: seek past end of file (%s)\n", handle->data.debug_name);
		handle->pos = handle->data.size;
	} else if (newpos < 0) {
		if (pig_debug)
			printf("warning: seek past begining of file (%s)\n", handle->data.debug_name);
		handle->pos = 0;
	} else {
		handle->pos = newpos;
	}
	if (handle->pos >= handle->data.size) {
		handle->flags |= PIG_FLAG_EOF;
	} else {
		handle->flags &= ~PIG_FLAG_EOF;
	}
	return 0;
}

int pig_fgetc(PigFileHandle *handle MEM_DBG_PARMS) {

	if (handle->flags & PIG_FLAG_EOF)
		return EOF;

	handle->last_used_milliseconds = timeGetTime();

	if (handle->buffer && (handle->pos >= handle->bufferpos) &&
		(handle->pos < handle->bufferpos + handle->bufferlen))
	{
		// We have this character in our buffer
		int ret = handle->buffer[handle->pos - handle->bufferpos];
		pig_fseek(handle, 1, SEEK_CUR);
		return ret;
	}

	// This character is not in the current buffer, get a new one
	// Allocate a buffer and read into it
	if (hogFileIsZipped(handle->data.parent_hog, handle->data.file_index)) {
		// It's zipped, the lower level will do all the buffering for us, just read a character!
		int ret=0;
		pig_fread(handle, &ret, 1 MEM_DBG_PARMS_CALL);
		return ret;
	} else {
		char *buffer;
		unsigned int len;
		int ret;
		U32 oldpos = handle->pos;
		// Do some buffering
		buffer = smalloc(DEFAULT_BUFFER_SIZE);
		len = pig_fread(handle, buffer, DEFAULT_BUFFER_SIZE MEM_DBG_PARMS_CALL);
		if (!handle->buffer) {
			// Save the buffer
			handle->buffer = buffer;
			handle->bufferlen = len;
			handle->bufferpos = oldpos;
			if (handle->bufferlen==0) {
				return EOF;
			}
		} else {
			// A buffer was allocated in pig_fread, assume it's the whole file
			ret=buffer[0];
			assert(handle->bufferlen >= len); // If not, we should keep our buffer
			free(buffer);
		}
		pig_fseek(handle, handle->bufferpos + 1, SEEK_SET); // Return cursor to where it should be
		return handle->buffer[0];
	}
}

long pig_ftell(PigFileHandle *handle) {
	return handle->pos;
}

long pig_fread(PigFileHandle *handle, void *buf, long size MEM_DBG_PARMS)
{
	U32 numread;
	if (handle->flags & PIG_FLAG_EOF)
		return 0;

	assert(size >= 0);

	// Check for reading past the end
	if (handle->pos + size > handle->data.size) {
		size = handle->data.size - handle->pos;
	}

	if (handle->pos + size <= handle->data.header_data_size) {
		// going to read from the cached header (handled by lower level code)
		goto NormalRead;
	}

ReadFromBuffer:
	handle->last_used_milliseconds = timeGetTime();

	// Check for a read from the buffered data
	if (handle->buffer && (handle->pos >= handle->bufferpos) &&
		(handle->pos < handle->bufferpos + handle->bufferlen))
	{
		// We have at least a character in our buffer
		numread = MIN((handle->bufferpos + handle->bufferlen) - handle->pos, (U32)size);
		memcpy(buf, handle->buffer + handle->pos - handle->bufferpos, numread);
		size-=numread;
		pig_fseek(handle, numread, SEEK_CUR); // advance handle->pos
		buf=((U8*)buf)+numread;
		if (size==0) // read everything from buffer
			return numread;
		// I don't think we should ever get here unless both fread and fgetc are used on the same file
	}

	if ((handle->flags & PIG_FLAG_NBUF) || ((U32)size == handle->data.size && handle->pos == 0)) {
		// Reading the entire file or unbuffered read, just pass it through
		goto NormalRead;
	}

	// otherwise...
	// going to be reading a partial chunk of data
	// if it's zipped, let's read the whole file and cache it
	if (hogFileIsZipped(handle->data.parent_hog, handle->data.file_index)) {
		// Zipped!
		SAFE_FREE(handle->buffer);
		handle->bufferlen = handle->data.size;
		handle->buffer = smalloc(handle->bufferlen);
		handle->bufferpos = 0;
		PigSetExtractBytes(&(handle->data), handle->buffer, 0, handle->bufferlen);
		goto ReadFromBuffer;
	}

NormalRead:
	numread = PigSetExtractBytes(&(handle->data), buf, handle->pos, size);
	handle->pos += numread;
	if (handle->pos >= handle->data.size) {
		handle->flags |= PIG_FLAG_EOF;
	}
	return numread;
}

char *pig_fgets(PigFileHandle *handle, char *buf, int len MEM_DBG_PARMS) {
	int pos=0;
	int ch;
	
	if (handle->flags & PIG_FLAG_EOF) {
		return NULL;
	}

	ch = pig_fgetc(handle MEM_DBG_PARMS_CALL);

	while (!(handle->flags & PIG_FLAG_EOF) && ch!='\n' && ch && pos<len-1) {
		buf[pos++] = (char)ch;
		ch = pig_fgetc(handle MEM_DBG_PARMS_CALL);
	}
	if (pos && buf[pos-1]=='\r') { // Assumes gets only called on ascii files, not binary
		buf[pos-1]=ch;
	} else {
		buf[pos++]=ch;
	}
	buf[pos]=0;
	return buf;
}

int pig_setvbuf(PigFileHandle *handle, char *buffer, int mode, size_t size)
{
	// we don't support controlling the size of the buffer, just enabling it.
	if (mode == _IONBF) {
		handle->flags |= PIG_FLAG_NBUF;
		SAFE_FREE(handle->buffer);
	} else {
		handle->flags &= ~PIG_FLAG_NBUF;
	}

	return 0;
}

long pig_filelength(PigFileHandle *handle) {
	return handle->data.size;
}

void *pig_lockRealPointer(PigFileHandle *handle)
{
	assert(0); // Not implemented with respect to Hog files
	return NULL; //fileRealPointer(handle->data.parent->file);
}

void pig_unlockRealPointer(PigFileHandle *handle) {
	assert(0); // Not implemented with respect to Hog files
}

void *pig_duphandle(PigFileHandle *handle)
{
	return hogFileDupHandle(handle->data.parent_hog, handle->data.file_index);
}


// Frees a buffer, if there is one, on a zipped file handle
void pig_freeBuffer(PigFileHandle *handle)
{
	EnterCriticalSection(&pigFileWrapperCritsec);
	SAFE_FREE(handle->buffer);
	LeaveCriticalSection(&pigFileWrapperCritsec);
}

// Frees buffers on handles that haven't been accessed in a long time
#define MILLISECONDS_TO_KEEP 5*60*1000
void pig_freeOldBuffers(void)
{
	if (1)
	{
		// This was only really useful for the FreeType font handles which were giant and rarely used
		// This code is also not thread-safe (can free the buffer when it is in use, though only under
		//  extreme circumstances, such as alt-tabbing while it's copying the data or page file resize).
		return;
	} else {
		int i;
		U32 time = timeGetTime();
		for (i=0; i<pig_file_handles_max; i++)
		{
			if (pig_file_handles[i].buffer &&
				(U32)(time - pig_file_handles[i].last_used_milliseconds) >= MILLISECONDS_TO_KEEP)
			{
				pig_freeBuffer(&pig_file_handles[i]);
			}
		}
	}
}
