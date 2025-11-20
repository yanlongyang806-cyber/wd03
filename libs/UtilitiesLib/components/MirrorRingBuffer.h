#pragma once
GCC_SYSTEM
#include "wininclude.h"

// A Mirror Ring Buffer is a SPSC queue that "unwraps" the buffer by mapping two contiguous regions
// of address space to the same memory. This means that reads and writes can assume a flat memory
// model without having to worry about wrap-around, at the cost of using twice the virtual memory.
// The buffer is thread-safe as long as all writes happen on one thread and all reads on another.
//
typedef struct MirrorRingBuffer {
	void *base;
	size_t size;
	volatile size_t put;
	volatile size_t get;
	HANDLE mapping;
} MirrorRingBuffer;

// Initializes a ring buffer with the given size.
// Size must be a multiple of 64k (the system allocation granularity).
bool mirrorRingCreate(MirrorRingBuffer *buf, size_t size);

// Frees a buffer's virtual memory and file mapping.
void mirrorRingDestroy(MirrorRingBuffer *buf);

// Total amount of space in the buffer, available and used.
size_t mirrorRingMaxSize(MirrorRingBuffer *buf);

// Returns the available size in the buffer.
size_t mirrorRingAvail(MirrorRingBuffer *buf);

// Allocates memory in the ring buffer. Returns NULL if 
// space is not available.
void *mirrorRingAlloc(MirrorRingBuffer *buf, size_t size);

// Frees memory from the ring buffer.
void mirrorRingFree(MirrorRingBuffer *buf, size_t size);
