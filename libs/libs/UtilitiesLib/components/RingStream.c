#include "file.h"
#include "GlobalTypes.h"
#include "mathutil.h"
#include "RingStream.h"
#include "timing.h"
#include "utils.h"
#include "wininclude.h"

typedef struct RingStreamImpl {
	char *buffer;					// Storage, size buffer_size
	size_t buffer_size_mask;		// buffer_size - 1
	size_t buffer_size;				// Size in bytes of buffer, must be power of 2
	size_t buffer_count;			// Total number of bytes written into buffer
} *RingStream;

// Create a new ring stream.
RingStream ringStreamCreate_dbg(size_t ringbuffer_size MEM_DBG_PARMS)
{
	RingStream ring;

	PERFINFO_AUTO_START_FUNC();

	// Allocate struct.
	ring = smalloc(sizeof(*ring));

	// Round size up to a power of 2.
	if (ringbuffer_size != (U32)ringbuffer_size)							// No 64-bit version of highBitIndex().
		ringbuffer_size = ~(U32)0;
	ring->buffer_size = (size_t)1 << (highBitIndex((U32)ringbuffer_size-1) + 1);
	ring->buffer_size_mask = ring->buffer_size - 1;
	ring->buffer_count = 0;

	// Allocate buffer
	ring->buffer = smalloc(ring->buffer_size);
	memset(ring->buffer, 0xdd, ring->buffer_size);

	PERFINFO_AUTO_STOP_FUNC();

	return ring;
}

// Destroy a ring stream.
void ringStreamDestroy(RingStream ring)
{
	PERFINFO_AUTO_START_FUNC();
	free(ring->buffer);
	free(ring);
	PERFINFO_AUTO_STOP_FUNC();
}

// Advance the stream by addend.
static size_t ringStreamAdd(RingStream ring, size_t addend)
{
	size_t last_count;

#ifdef _WIN64
	last_count = InterlockedExchangeAdd64(&ring->buffer_count, addend);
#elif defined(_WIN32)
	last_count = InterlockedExchangeAdd(&ring->buffer_count, addend);
#else
	last_count = ring->buffer_count;
	ring->buffer_count += addend;
#endif

	return last_count;
}

// Push data onto the end of a ring stream, possibly overwriting data at the beginning.
void ringStreamPush(RingStream ring, const char *data, size_t data_size)
{
	size_t push_size;
	size_t last_count;
	size_t position;
	size_t split;

	// Clamp push_size to the buffer size.
	push_size = data_size;
	if (push_size > ring->buffer_size)
	{
		size_t excess = push_size - ring->buffer_size;
		push_size -= excess;
		data += excess;
		ringStreamAdd(ring, excess);
	}

	// Increment the count.
	last_count = ringStreamAdd(ring, push_size);

	// Get position.
	position = last_count & ring->buffer_size_mask;

	// Write the wrap-around part, if any.
	if (push_size > ring->buffer_size - position)
	{
		split = ring->buffer_size - position;
		memcpy(ring->buffer, data + split, push_size - split);
	}
	else
		split = push_size;

	// Write the part before the wrap.
	memcpy(ring->buffer + position, data, split);
}

// Return a pointer to the buffer.
const char *ringStreamBuffer(RingStream ring)
{
	return ring->buffer;
}

// Next byte to be written in buffer
size_t ringStreamPosition(RingStream ring)
{
	return ring->buffer_count & ring->buffer_size_mask;
}

// Write the contents of the ring buffer to a file.
void ringStreamWriteDebugFile(RingStream ring, const char *identifier)
{
	char filename[MAX_PATH];
	size_t count;
	FILE *outfile;
	size_t position;
	size_t split;
	size_t written;
	int result;

	PERFINFO_AUTO_START_FUNC();
	
	// Generate unique filename.
	count = ring->buffer_count;
	sprintf(filename, "%s/%s_%s_%"FORM_LL"u_%d.dat", fileLogDir(), identifier, GlobalTypeToShortName(GetAppGlobalType()), (U64)count, (int)getpid());
	forwardSlashes(filename);
	fixDoubleSlashes(filename);

	// Make log directory, if necessary.
	mkdirtree(filename);

	// Open file.
	outfile = fopen(filename, "w");
	if (!outfile)
	{
		ErrorFilenamef(filename, __FUNCTION__ ": Open failed");
		return;
	}

	// Write data before wrap.
	position = (count & ring->buffer_size_mask);
	split = ring->buffer_size - position;
	if (count > ring->buffer_size)
	{
		written = fwrite(ring->buffer + position, 1, split, outfile);
		if (written != split)
			ErrorFilenamef(filename, __FUNCTION__ ": Write #1 failed");
	}

	// Write data after wrap.
	written = fwrite(ring->buffer, 1, position, outfile);
	if (written != position)
		ErrorFilenamef(filename, __FUNCTION__ ": Write #2 failed");
	
	// Close file.
	result = fclose(outfile);
	if (result)
	{
		ErrorFilenamef(filename, __FUNCTION__ ": Close failed");
	}

	PERFINFO_AUTO_STOP_FUNC();
}
