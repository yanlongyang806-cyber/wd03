#include "MirrorRingBuffer.h"

bool mirrorRingCreate(MirrorRingBuffer *buf, size_t size)
{
	int numRetries;
	size_t allocSize = size + size;

    // size must be a multiple of 64k (the system allocation granularity).
	assert((size & 0xffff) == 0);

	buf->mapping = NULL;
	buf->size = size;
	buf->base = NULL;
	buf->put = 0;
	buf->get = 0;

	// WIN32 does not have a way to create a file mapping in reserved memory. Reserved memory can only be
	// used in a subsequent call to VirtualAlloc. So we first try to allocate enough memory, then immediately
	// free it and try to set up our mapping at that address. This is subject to a race condition, so we retry
	// a few times.

	for (numRetries = 5; numRetries > 0; --numRetries) {
		void *base = NULL, *mirror = NULL;

		void *target = VirtualAlloc(0, allocSize, MEM_RESERVE, PAGE_NOACCESS);
		if (!target) {
			return false;
		}

		VirtualFree(target, 0, MEM_RELEASE);

		buf->mapping = CreateFileMappingA(INVALID_HANDLE_VALUE, NULL, PAGE_READWRITE, (U64)allocSize >> 32, allocSize & 0xffffffffu, 0);
		if (!buf->mapping) {
			continue;
		}

		base = MapViewOfFileEx(buf->mapping, FILE_MAP_ALL_ACCESS, 0, 0, size, target);
		if (!base) {
			CloseHandle(buf->mapping);
			continue;;
		}

		mirror = MapViewOfFileEx(buf->mapping, FILE_MAP_ALL_ACCESS, 0, 0, size, (U8 *)target + size);
		if (!mirror) {
			UnmapViewOfFile(base);
			CloseHandle(buf->mapping);
			continue;
		}

		// everything worked!
		buf->base = base;
		break;
	}

	return buf->base != NULL;
}

void mirrorRingDestroy(MirrorRingBuffer *buf)
{
	if (buf->base) {
		UnmapViewOfFile(buf->base);
		UnmapViewOfFile((U8*)buf->base + buf->size);
		buf->base = NULL;
	}

	if (buf->mapping) {
		CloseHandle(buf->mapping);
		buf->mapping = NULL;
	}
}

size_t mirrorRingMaxSize(MirrorRingBuffer *buf)
{
	return buf->size - 1;
}

size_t mirrorRingAvail(MirrorRingBuffer *buf)
{
	return (buf->put > buf->get ? buf->size - (buf->put - buf->get) : buf->get - buf->put) - 1;
}

void *mirrorRingAlloc(MirrorRingBuffer *buf, size_t size)
{
	void *ret;

	if (size > mirrorRingAvail(buf)) {
		return NULL;
	}

	ret = (U8 *)buf->base + buf->put;
	buf->put = (buf->put + size) % buf->size;

	return ret;
}

void mirrorRingFree(MirrorRingBuffer *buf, size_t size)
{
	assert(buf->size - mirrorRingAvail(buf) >= size);

	buf->get = (buf->get + size) % buf->size;
}
