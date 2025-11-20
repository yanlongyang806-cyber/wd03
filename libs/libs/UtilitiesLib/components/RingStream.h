// Simple mutable byte stream ring buffer

// RingBuffer.h for an allocator-type ring buffer, or memlog.h for a ring buffer intended specifically for in-memory logging

#ifndef CRYPTIC_RING_STREAM_H
#define CRYPTIC_RING_STREAM_H

typedef struct RingStreamImpl *RingStream;

// Create a new ring stream.
#define ringStreamCreate(ringbuffer_size) ringStreamCreate_dbg(ringbuffer_size MEM_DBG_PARMS_INIT)
RingStream ringStreamCreate_dbg(size_t ringbuffer_size MEM_DBG_PARMS);

// Destroy a ring stream.
void ringStreamDestroy(RingStream ring);

// Push data onto the end of a ring stream, possibly overwriting data at the beginning.
// This is thread-safe as long as all buffers being pushed simultaneously is not larger
// than the size of the buffer.  Otherwise, the contents of the buffer may be mangled, but
// it's safe otherwise.
void ringStreamPush(RingStream ring, const char *data, size_t data_size);

// Return a pointer to the buffer.
const char *ringStreamBuffer(RingStream ring);

// Next byte to be written in buffer
size_t ringStreamPosition(RingStream ring);

// Write the contents of the ring buffer to a file.
void ringStreamWriteDebugFile(RingStream ring, const char *identifier);

#endif  // CRYPTIC_RING_STREAM_H
