#include "LinearAllocator.h"
#include "earray.h"
#include "mathutil.h"
#include "MemTrack.h"
#include "DebugState.h"

#define DEBUG_HEAP_CORRUPTION 0


#define LA_MIN_BLOCK_SIZE				1024
#define LA_MIN_CHUNK_SIZE				32

#if DEBUG_HEAP_CORRUPTION
#define assertHeapValid() assertHeapValidInternal()
#else
#define assertHeapValid()
#endif

typedef struct LAFreeChunk
{
	int dummy_UNUSED;
} LAFreeChunk;

typedef struct LinearAllocator
{
	int block_size, max_blocks;
	LAFreeChunk **free_chunks;
	GuardedAlignedMalloc **alloced_blocks;
	U32 *sizes;
	void **large_allocations;

	bool zeromem;
#ifdef LINALLOC_EXTRA_CHECKING
	bool lastCallerCalledDone;
#endif
#ifdef _FULLDEBUG
	U32 lastSize;
	const char *lastCallerFname;
	U32 lastCallerLine;
#endif

	MEM_DBG_STRUCT_PARMS
} LinearAllocator;

static void assertHeapValidInternal(void)
{
	if (dbg_state.test1)
		assertHeapValidateAll();
}

LinearAllocator *linAllocCreateDbg(int block_size, int max_size, bool zeromem MEM_DBG_PARMS)
{
	LinearAllocator *la = scalloc(1,sizeof(LinearAllocator));
	la->block_size = MAX(block_size, LA_MIN_BLOCK_SIZE);
	if (max_size <= 0)
		la->max_blocks = INT_MAX;
	else
		la->max_blocks = round(ceil(max_size / ((F32)la->block_size)));
	la->zeromem = zeromem;
	MEM_DBG_STRUCT_PARMS_INIT(la);
	return la;
}

// reset allocator to the beginning, keeping the blocks allocated
void linAllocClear(LinearAllocator *la)
{
	int i, dirty_blocks = 0;

	if (!la)
		return;
		
	PERFINFO_AUTO_START_FUNC();

	if (la->zeromem)
	{
		dirty_blocks = eaSize(&la->alloced_blocks);
		for (i = eaiSize(&la->sizes) - 1; i >= 0; --i)
		{
			if (la->sizes[i] == (U32)la->block_size)
				--dirty_blocks;
		}
	}

	eaiClear(&la->sizes);
	eaClear(&la->free_chunks);

	for (i = 0; i < eaSize(&la->alloced_blocks); ++i)
	{
		GuardedAlignedMalloc *new_chunk = la->alloced_blocks[i];
		
		if (i < dirty_blocks) // dirty_blocks will be 0 if la->zeromem is false
			memset(new_chunk->aligned_protected_memory, 0, la->block_size);

#ifdef _FULLDEBUG
		if (la->zeromem)
			assertmsg(((LAFreeChunk*)new_chunk->aligned_protected_memory)->dummy_UNUSED == 0, "Linear allocator memory is corrupted!  Most likely somebody is writing beyond their allocated size");
#endif

		steaiPush(&la->sizes, la->block_size, la);
		steaPush(&la->free_chunks, (LAFreeChunk*)new_chunk->aligned_protected_memory, la);
	}

	eaClearEx(&la->large_allocations, NULL);
	
	PERFINFO_AUTO_STOP();
}

void linAllocSetMemoryLimit(LinearAllocator *la, int max_size)
{
	int i, new_max_blocks;

	if (!la)
		return;

	if (max_size <= 0)
		new_max_blocks = INT_MAX;
	else
		new_max_blocks = round(ceil(max_size / ((F32)la->block_size)));

	if (new_max_blocks < la->max_blocks && new_max_blocks < eaSize(&la->alloced_blocks))
	{
		// free extra alloced blocks
		for (i = new_max_blocks; i < eaSize(&la->alloced_blocks); ++i)
		{
			GuardedAlignedMalloc * block = la->alloced_blocks[i];
			la->alloced_blocks[i] = NULL;
			guarded_aligned_free(block);
			free(block);
		}

		assert(new_max_blocks < eaiSize(&la->sizes));
		assert(new_max_blocks < eaSize(&la->free_chunks));

		steaSetSize(&la->alloced_blocks, new_max_blocks, la);
		steaiSetSize(&la->sizes, new_max_blocks, la);
		steaSetSize(&la->free_chunks, new_max_blocks, la);
	}

	la->max_blocks = new_max_blocks;
}

#ifdef LINALLOC_EXTRA_CHECKING
void linAllocDoneDebug(LinearAllocator *la)
{
	int i;

	if (!la)
		return;

	if (!la->zeromem)
		return;

	for (i = 0; i < eaSize(&la->free_chunks); ++i)
	{
		LAFreeChunk *new_chunk = la->free_chunks[i];
#if 0 // Slow/robust checking
		char *p;
		int c=la->sizes[i];
		for (p=(char*)la->free_chunks[i]; !*p && c; p++, c--); // Check all the rest of the memory
		assertmsg(!c, "Linear allocator memory is corrupted!  Most likely somebody is writing beyond their allocated size");
#else // Fast
		assertmsg(new_chunk->dummy_UNUSED == 0, "Linear allocator memory is corrupted!  Most likely somebody is writing beyond their allocated size");
#endif
	}

	la->lastCallerCalledDone = true;
}
#endif

void linAllocCheck(LinearAllocator *la)
{
	int i;

	assert(la && la->zeromem);

	for (i = 0; i < eaSize(&la->free_chunks); ++i)
	{
		char *p;
		int c=la->sizes[i];
		for (p=(char*)la->free_chunks[i]; !*p && c; p++, c--); // Check all the rest of the memory
		assertmsg(!c, "Linear allocator memory is corrupted!  Most likely somebody is writing beyond their allocated size");
	}
}

// free all blocks and the allocator struct
void linAllocDestroy(LinearAllocator *la)
{
	if (!la)
		return;

	eaDestroy(&la->free_chunks);
	eaDestroyEx(&la->alloced_blocks, NULL);
	eaiDestroy(&la->sizes);
	free(la);
}

// this will always allocate "bytes", but will return the next aligned address if bAlign16 is true
__forceinline void *allocFromChunk(LinearAllocator *la, int bytes, int diff, int index, bool bAlign16 FD_MEM_DBG_PARMS)
{
	void *mem = la->free_chunks[index];

	assert(diff >= 0 && diff < la->block_size);

#ifdef _FULLDEBUG
	if (la->zeromem)
		assertmsg(la->free_chunks[index]->dummy_UNUSED == 0, "Linear allocator memory is corrupted!  Most likely somebody is writing beyond their allocated size");
#endif

	assertHeapValid();
	if (diff <= LA_MIN_CHUNK_SIZE) {
		// Just remove it
		eaRemove(&la->free_chunks, index);
		eaiRemove(&la->sizes, index);
	} else {
		// Update size info, put in new chunk
		LAFreeChunk *new_chunk = (LAFreeChunk *)(((U8*)mem) + bytes);
		la->sizes[index] = diff;
		la->free_chunks[index] = new_chunk;
#ifdef _FULLDEBUG
		if (la->zeromem)
		{
#if 0 // Robust checking
			char *p;
			int c=diff+bytes;
			for (p=mem; !*p && c; p++, c--); // Check all the rest of the memory
			assertmsg(!c, "Linear allocator memory is corrupted!  Most likely somebody is writing beyond their allocated size");
#else // Simple checking
			assertmsg(new_chunk->dummy_UNUSED == 0, "Linear allocator memory is corrupted!  Most likely somebody is writing beyond their allocated size");
#endif
		}
		la->lastSize = bytes;
		la->lastCallerFname = caller_fname;
		la->lastCallerLine = line;
#endif
#ifdef LINALLOC_EXTRA_CHECKING
		la->lastCallerCalledDone = false;
#endif
	}

	if (bAlign16)
	{
		mem = (U8 *)(((intptr_t)mem+0xf) & ~0xf);
	}

	return mem;
}

static int _getBytesNeededToAlign16(LinearAllocator *la, U8 * pMemAddress,int iBytes)
{
	U8 * pAlignedAddress = (U8 *)((intptr_t)(pMemAddress+0xf) & ~0xf);

	return (int)(pAlignedAddress-pMemAddress)+iBytes;
}

// allocate a chunk of memory from the allocator
void *linAllocExInternal(LinearAllocator *la, int bytes, bool allow_failure, bool bAlign16 FD_MEM_DBG_PARMS)
{
	GuardedAlignedMalloc * new_block = NULL;
	void *mem;
	int i;
	int free_chunk_size = eaSize(&la->free_chunks);

	assertHeapValid();

	// alloc a bigger block in this case
	if (bytes > la->block_size)
	{
		if (allow_failure)
			return NULL;

		if (la->zeromem)
			mem = stcalloc(1, bytes, la);
		else
			mem = stmalloc(bytes, la);

		steaPush(&la->large_allocations, mem, la);

		return mem;
	}

	if (allow_failure && eaSize(&la->alloced_blocks) >= la->max_blocks)
		free_chunk_size -= eaSize(&la->alloced_blocks) - la->max_blocks;

	// look for existing chunk of appropriate size
	for (i=0; i<free_chunk_size; i++) 
	{
		int chunk_bytes = la->sizes[i];
		if (chunk_bytes >= bytes)
		{
			int diff;
			int iBytesToAllocate = bytes;
			if (bAlign16)
			{
				iBytesToAllocate = _getBytesNeededToAlign16(la,(U8 *)la->free_chunks[i],bytes);
				if (iBytesToAllocate > chunk_bytes)
				{
					// not good enough for us
					continue;
				}
			}
			diff = chunk_bytes - iBytesToAllocate;
			return allocFromChunk(la, iBytesToAllocate, diff, i, bAlign16 FD_MEM_DBG_PARMS_CALL);
		}
	}

	if (allow_failure && eaSize(&la->alloced_blocks) >= la->max_blocks)
		return NULL;

	// add new block
	new_block = stcalloc(1, sizeof(*new_block), la);
	guarded_aligned_malloc(new_block, la->block_size, 1, 1);
	mem = new_block->aligned_protected_memory;
	if (la->zeromem)
		memset(mem, 0, la->block_size);
	assertHeapValid();
	steaPush(&la->alloced_blocks, new_block, la);
	steaPush(&la->free_chunks, mem, la);
	steaiPush(&la->sizes, la->block_size, la);
	assertHeapValid();

	bytes = _getBytesNeededToAlign16(la,mem,bytes);
	return allocFromChunk(la, bytes, la->block_size - bytes, eaSize(&la->free_chunks)-1, bAlign16 FD_MEM_DBG_PARMS_CALL);
}

// Tests the linear allocator
AUTO_COMMAND ACMD_CATEGORY(Debug) ACMD_CMDLINE;
void linAllocTest(void)
{
	int sizes[] = {
		212,
		44, 212, 44, 44, 88, 212, 44, 44, 88, 212, 44, 44, 88, 212, 44, 212, 44, 44, 44, 44, 44, 88, 212, 44, 44, 88, 212, 44, 44, 88, 212, 44, 44, 88, 212, 44, 44, 88, 212, 44, 44, 88, 212, 44, 44, 88, 212, 44, 212, 44, 44, 44, 44, 88, 212, 44, 44, 88, 212, 44, 44, 88, 212, 44,
		44, 88, 212, 44, 44, 88, 212, 44, 44, 88, 212, 44, 44, 88, 212, 44, 44, 88, 212, 44, 44, 88, 212, 44, 212, 44, 212, 44, 212, 44, 212, 44, 212, 44, 212, 44, 212, 44, 212, 44, 212, 44, 212, 44, 212, 44, 212, 44, 212, 44, 212, 44, 212, 44, 212, 44, 212, 44, 212, 44, 212, 44, 212, 44,
		212, 44, 212, 44, 212, 44, 212, 44, 212, 44, 212, 44, 212, 44, 212, 44, 212, 44, 212, 44, 212, 44, 212, 44, 212, 44, 212, 44, 212, 44, 212, 44, 212, 44, 212, 44, 212, 44, 212, 44, 212, 44, 212, 44, 212, 44, 212, 44, 212, 44, 212, 44, 212, 44, 44, 44, 44, 44, 44, 44, 212, 44, 212, 44, 212, 44,
		212, 44, 212, 44, 212, 44, 212, 44, 212, 44, 212, 44, 212, 44, 212, 44, 212, 44, 212, 44, 212, 44, 212, 44, 212, 44, 212, 44, 212, 44, 212, 44, 212, 44, 212, 44, 212, 44, 212, 44, 212, 44, 212, 44, 212, 44, 212, 44, 44, 88, 212, 44, 212, 44, 212, 44, 212, 44, 212, 44, 212, 44, 212, 44, 212, 44, 212, 44, 212, 44, 212, 44, 212, 44, 212, 44, 212, 44, 212, 44, 212, 44, 212, 44, 212,
		44, 212, 44, 212, 44, 212, 44, 212, 44, 212, 44, 212, 44, 212, 44, 212, 44, 212, 44, 212, 44, 212, 44, 212, 44, 44, 88, 212, 44, 212, 44, 212, 44, 212, 44, 212, 44, 212, 44, 212, 44, 212, 44, 212, 44, 212, 44, 212, 44, 212, 44, 212, 44, 212, 44, 212, 44, 212, 44, 212, 44, 44, 88, 212,
		44, 212, 44, 212, 44, 212, 44, 212, 44, 212, 44, 212, 44, 212, 44, 212, 44, 212, 44, 212, 44, 212, 44, 212, 44, 212, 44, 212, 44, 212, 44, 212, 44, 212, 44, 212, 44, 212, 44, 212, 44, 212, 44, 212, 44, 212, 44, 212, 44, 212, 44, 212, 44, 212, 44, 212, 44, 212, 44, 212, 44, 212, 44, 212, 44, 212,
		44, 212, 44, 212, 44, 212, 44, 212, 44, 212, 44, 212, 44, 212, 44, 212, 44, 212, 44, 44, 80, 212, 44, 212, 44, 212, 44, 212, 44, 212, 44, 212, 44, 212, 44, 212, 44, 212, 44, 212, 44, 212, 44, 212, 44, 212, 44, 212, 44, 212, 44, 212, 44, 212, 44, 212, 44, 212, 44, 212, 44, 212, 44, 212, 44, 212, 44, 212,
		44, 212, 44, 212, 44, 212, 44, 212, 44, 212, 44, 212, 44, 212, 44, 212, 44, 212, 44, 212, 44, 212, 44, 212, 44, 212, 44, 212, 44, 212, 44, 212, 44, 212, 44, 212, 44, 212, 44, 212, 44, 212, 44, 212, 44, 212, 44, 212, 44, 212, 44, 212, 44, 212, 44, 212, 44, 212, 44, 212, 44, 212, 44, 212, 44,
		212, 44, 212, 44, 212, 44, 212, 44, 212, 44, 212, 44, 212, 44, 212, 44, 212, 44, 212, 44, 212, 44, 212, 44, 212, 44, 212, 44, 212, 44, 212, 44, 212, 44, 212, 44, 212, 44, 212, 44, 44, 88, 212, 44, 212, 44, 212, 44, 212, 44, 212, 44, 212, 44, 212, 44, 212, 44, 212, 44, 212, 44, 212, 44, 212, 44,
		212, 44, 212, 44, 212, 44, 212, 44, 212, 44, 212, 44, 212, 44, 212, 44, 212, 44, 212, 44, 212, 44, 212, 44, 212, 44, 212, 44, 212, 44, 212, 44, 212, 44, 212, 44, 212, 44, 212, 44, 212, 44, 212, 44, 212, 44, 212, 44, 212, 44, 212, 44, 212, 44, 212, 44, 212, 44, 212, 44, 212, 44, 212, 44,
		212, 44, 212, 44, 212, 44, 212, 44, 212, 44, 212, 44, 212, 44, 212, 44, 212, 44, 212, 44, 212, 44, 212, 44, 212, 44, 212, 44, 212, 44, 212, 44, 212, 44, 212, 44, 212, 44, 212, 44, 212, 44, 212, 44, 212, 44, 212, 44, 212, 44, 212, 44, 212, 44, 212, 44, 212, 44, 212, 44, 212, 44, 212, 44,
		212, 44, 212, 44, 212, 44, 212, 44, 212, 44, 212, 44, 212, 44, 212, 44, 212, 44, 212, 44, 212, 44, 212, 44, 212, 44, 212, 44, 212, 44, 212, 44, 212, 44, 212, 44, 212, 44, 212, 44, 212, 44, 212, 44, 212, 44, 212, 44, 212, 44, 212, 44, 212, 44, 212, 44, 212, 44, 212, 44, 212, 44, 212, 44, 212, 44,
		212, 44, 212, 44, 212, 44, 212, 44, 212, 44, 212, 44, 212, 44, 212, 44, 212, 44, 212, 44, 212, 44, 212, 44, 212, 44, 212, 44, 212, 44, 212, 44, 212, 44, 212, 44, 212, 44, 212, 44, 212, 44, 212, 44, 212, 44, 212, 44, 212, 44, 212, 44, 212, 44, 212, 44, 212, 44, 212, 44, 212, 44, 212, 44, 212, 44, 212, 44, 212,
		44, 212, 44, 212, 44, 212, 44, 212, 44, 212, 44, 212, 44, 212, 44, 212, 44, 212, 44, 212, 44, 212, 44, 212, 44, 212, 44, 212, 44, 212, 44, 212, 44, 212, 44, 212, 44, 212, 44, 212, 44, 212, 44, 212, 44, 212, 44, 212, 44, 212, 44, 212, 44, 212, 44, 212, 44, 212, 44, 212, 44, 212, 44, 212,
		44, 212, 44, 212, 44, 212, 44, 212, 44, 212, 44, 212, 44, 212, 44, 212, 44, 212, 44, 212, 44, 212, 44, 212, 44, 212, 44, 212, 44, 212, 44, 212, 44, 212, 44, 212, 44, 212, 44, 212, 44, 212, 44, 212, 44, 212, 44, 212, 44, 212, 44, 212, 44, 212, 44, 212, 44, 212, 44, 212, 44, 212, 44, 212,
		44, 212, 44, 44, 88, 212, 44, 44, 88, 212, 44, 44, 88, 212, 44, 44, 88, 212, 44, 44, 88, 212, 44, 44, 88, 212, 44, 212, 44, 212, 44, 212, 44, 212, 44, 212, 44, 44, 88, 212, 44, 212, 44, 212, 44, 212, 44, 212, 44, 212, 44, 212, 44, 212, 44, 212, 44, 212, 44, 212, 44, 212, 44, 212, 44, 212, 44, 212, 44, 212, 44,
		212, 44, 212, 44, 212, 44, 212, 44, 212, 44, 212, 44, 212, 44, 212, 44, 212, 44, 212, 44, 212, 44, 212, 44, 212, 44, 212, 44, 212, 44, 212, 44, 212, 44, 212, 44, 212, 44, 44, 312, 212, 44, 212, 44, 212, 44, 44, 212, 44, 212, 44, 44, 44, 96, 212, 44, 212, 44, 44, 44, 96, 212, 44, 212, 44, 44, 44, 96, 212, 44, 212, 44, 44, 44, 96, 212, 44, 212, 44, 44,
		44, 96, 212, 44, 212, 44, 44, 44, 96, 212, 44, 212, 44, 44, 44, 96, 212, 44, 212, 44, 44, 212, 44, 212, 44, 44, 44, 312, 212, 44, 44, 312, 212, 44, 44, 312, 212, 44, 44, 312, 212, 44, 44, 312, 212, 44, 44, 312, 212, 44, 44, 312, 212, 44, 44, 312, 212, 44, 44, 312, 212, 44, 44, 312, 212,
		44, 44, 312, 212, 44, 44, 312, 212, 44, 44, 312, 212, 44, 44, 312, 212, 44, 44, 312, 212, 44, 44, 312, 212, 44, 44, 312, 212, 44, 44, 312, 212, 44, 44, 312, 212, 44, 44, 312, 212, 44, 44, 312, 44, 312, 212, 44, 44, 312, 44, 312, 212, 44, 44, 44, 312, 44, 312, 212, 44, 44, 44, 312, 212, 44, 212,
		44, 44, 312, 212, 44, 44, 312, 212, 44, 212, 44, 44, 312, 212, 44, 212, 44, 212, 44, 44, 312, 212, 44, 212, 44, 212, 44, 44, 312, 212, 44, 212, 44, 212, 44, 44, 312, 212, 44, 44, 312, 212, 44, 212, 44, 212, 44, 212, 44, 212, 44, 44, 212, 44, 44, 212, 44, 44, 212, 44, 44, 212, 44, 44, 212,
		44, 44, 212, 44, 44, 212, 44, 44, 212, 44, 44, 212, 44, 44, 212, 44, 44, 212, 44, 44, 212, 44, 44, 212, 44, 44, 212, 44, 44, 212, 44, 212, 44, 212, 44, 44, 312, 212, 44, 44, 312, 44, 312, 212, 44, 44, 44, 80, 212, 44, 44, 80, 212, 44, 44, 80, 212, 44, 212, 44, 44, 212, 44, 44,
		212, 44, 44, 212, 44, 44, 212, 44, 44, 212, 44, 44, 212, 44, 44, 212, 44, 44, 212, 44, 44, 212, 44, 44, 212, 44, 44, 212, 44, 44, 212, 44, 44, 212, 44, 44, 212, 44, 44, 212, 44, 212, 44, 44, 312, 44, 312, 212, 44, 44, 44, 312, 212, 44, 44, 312, 212, 44, 44, 312, 212, 44, 44, 312,
		212, 44, 44, 312, 212, 44, 44, 312, 212, 44, 44, 312, 212, 44, 44, 312, 212, 44, 212, 44, 44, 212, 44, 212, 44, 212, 44, 44, 212, 44, 44, 212, 44, 44, 212, 44, 44, 212, 44, 44, 212, 44, 44, 212, 44, 44, 212, 44, 44, 212, 44, 44, 212, 44, 44, 212, 44, 44, 44, 312, 212, 44, 44, 312, 212, 44, 44,
		312, 212, 44, 44, 312, 212, 44, 44, 312, 212, 44, 44, 312, 44, 312, 212, 44, 44, 212, 44, 212, 44, 44, 212, 44, 44, 212, 44, 44, 212, 44, 44, 212, 44, 44, 212, 44, 212, 44, 44, 212, 44, 44, 212, 44, 44, 212, 44, 44, 212, 44, 44, 212, 44, 44, 212, 44, 44, 212, 44, 44, 212, 44, 44,
		212, 44, 212, 44, 44, 212, 44, 212, 44, 44, 312, 212, 44, 44, 312, 212, 44, 44, 312, 212, 44, 44, 312, 212, 44, 44, 312, 212, 44, 44, 312, 212, 44, 44, 312, 212, 44, 44, 312, 212, 44, 44, 312, 44, 312, 212, 44, 44, 44, 312, 212, 44, 212, 44, 44, 212, 44, 44, 212, 44, 44, 212, 44, 44, 212, 44,
		44, 212, 44, 44, 212, 44, 44, 212, 44, 44, 212, 44, 44, 212, 44, 44, 212, 44, 44, 212, 44, 44, 212, 44, 44, 212, 44, 44, 212, 44, 212, 44, 44, 212, 44, 212, 44, 212, 44, 212, 44, 212, 44, 44, 312, 212, 44, 44, 312, 44, 312, 212, 44, 44, 44, 312, 212, 44, 212, 44, 44, 212, 44, 44, 212, 44, 44, 212, 44,
		44, 212, 44, 44, 212, 44, 44, 212, 44, 212, 44, 44, 212, 44, 44, 212, 44, 44, 212, 44, 44, 212, 44, 44, 212, 44, 212, 44, 212, 44, 44, 312, 212, 44, 44, 312, 212, 44, 44, 312, 212, 44, 44, 312, 212, 44, 44, 312, 212, 44, 44, 312, 212, 44, 44, 312, 212, 44, 44, 312, 212, 44, 44, 312,
		212, 44, 44, 312, 212, 44, 44, 312, 212, 44, 44, 312, 212, 44, 44, 312, 212, 44, 44, 312, 212, 44, 44, 312, 212, 44, 44, 312, 212, 44, 44, 312, 212, 44, 212, 44, 44, 312, 212, 44, 212, 44, 44, 212, 44, 44, 212, 44, 44, 212, 44, 44, 212, 44, 212, 44, 212, 44, 44, 212, 44, 44, 212, 44,
		44, 212, 44, 44, 212, 44, 44, 212, 44, 44, 212, 44, 44, 212, 44, 44, 212, 44, 44, 212, 44, 44, 44, 312, 212, 44, 44, 312, 44, 312, 212, 44, 44, 212, 44, 44, 212, 44, 44, 212, 44, 212, 44, 44, 312, 44, 312, 212, 44, 44, 212, 44, 212, 44, 44, 212, 44, 212, 44, 44, 312, 212, 44, 44, 312, 44,
		312, 212, 44, 44, 212, 44, 44, 88, 212, 44, 44, 88, 212, 44, 44, 88, 212, 44, 44, 88, 212, 44, 44, 88, 212, 44, 44, 88, 212, 44, 44, 88, 212, 44, 44, 88, 212, 44, 44, 88, 212, 44, 212, 44, 44, 88, 212, 44, 212, 44, 212, 44, 44, 312, 212, 44, 212, 44, 44, 88, 212, 44, 212, 44, 44,
		312, 212, 44, 44, 312, 212, 44, 212, 44, 212, 44, 212, 44, 44, 312, 212, 44, 212, 44, 44, 312, 212, 44, 212, 44, 212, 44, 212, 44, 212, 44, 212, 44, 212, 44, 212, 44, 212, 44, 212, 44, 212, 44, 212, 44, 212, 44, 44, 312, 212, 44, 212, 44, 44, 88, 212, 44, 212, 44, 212, 44, 212, 44, 212, 44, 212, 44, 44, 44, 312, 212, 44,
		212, 44, 44, 44, 88, 212, 44, 212, 44, 44, 212, 44, 212, 44, 212, 44, 44, 312, 44, 312, 212, 44, 44, 44, 312, 212, 44, 212, 44, 44, 44, 88, 212, 44, 212, 44, 212, 44, 212, 44, 212, 44, 44, 88, 212, 44, 44, 88, 212, 44, 44, 88, 212, 44, 212, 44, 212, 44, 212, 44, 212, 44, 44, 88, 212, 44, 44, 88, 212, 44,
		44, 312, 212, 44, 212, 44, 44, 88, 212, 44, 44, 312, 212, 44, 212, 44, 44, 80, 212, 44, 44, 312, 212, 44, 212, 44, 212, 44, 212, 44, 44, 312, 212, 44, 212, 44, 212, 44, 212, 44, 44, 312, 212, 44, 212, 44, 212, 44, 44, 312, 212, 44, 44, 312, 212, 44, 44, 312, 212, 44, 44, 88, 212, 44, 212,
		44, 212, 44, 44, 312, 212, 44, 212, 44, 44, 88, 212, 44, 44, 88, 212, 44, 44, 88, 212, 44, 212, 44, 212, 44, 212, 44, 44, 312, 212, 44, 212, 44, 44, 88, 212, 44, 44, 88, 212, 44, 44, 88, 212, 44, 212, 44, 212, 44, 44, 88, 212, 44, 44, 88, 212, 44, 44, 88, 212, 44, 212, 44, 212,
		44, 212, 44, 44, 88, 212, 44, 44, 88, 212, 44, 44, 88, 212, 44, 44, 88, 212, 44, 212, 44, 44, 88, 212, 44, 44, 88, 212, 44, 44, 88, 212, 44, 212, 44, 44, 88, 212, 44, 212, 44, 44, 212, 44, 44, 212, 44, 44, 212, 44, 44, 212, 44, 44, 212, 44, 44, 44, 88, 212, 44, 212, 44, 212,
		44, 212, 44, 212, 44, 212, 44, 212, 44, 212, 44, 212, 44, 212, 44, 212, 44, 212, 44, 212, 44, 212, 44, 212, 44, 212, 44, 212, 44, 212, 44, 212, 44, 212, 44, 212, 44, 212, 44, 212, 44, 212, 44, 212, 44, 212, 44, 212, 44, 212, 44, 212, 44, 212, 44, 212, 44, 212, 44, 212, 44, 212, 44, 212, 44,
		212, 44, 212, 44, 212, 44, 212, 44, 212, 44, 212, 44, 212, 44, 212, 44, 212, 44, 212, 44, 212, 44, 212, 44, 212, 44, 212, 44, 212, 44, 212, 44, 212, 44, 212, 44, 212, 44, 212, 44, 212, 44, 212, 44, 212, 44, 212, 44, 212, 44, 212, 44, 212, 44, 212, 44, 212, 44, 212, 44, 212, 44, 212, 44, 212, 44, 212, 44, 212, 44,
		212, 44, 212, 44, 212, 44, 212, 44, 212, 44, 212, 44, 212, 44, 212, 44, 212, 44, 212, 44, 212, 44, 212, 44, 212, 44, 212, 44, 212, 44, 212, 44, 212, 44, 212, 44, 212, 44, 212, 44, 212, 44, 212, 44, 212, 44, 212, 44, 212, 44, 212, 44, 212, 44, 212, 44, 212, 44, 212, 44, 212, 44, 212, 44, 212,
		44, 212, 44, 212, 44, 212, 44, 212, 44, 212, 44, 212, 44, 44, 88, 212, 44, 44, 88, 212, 44, 44, 88, 212, 44, 44, 88, 212, 44, 212, 44, 44, 212, 44, 44, 212, 44, 44, 212, 44, 44, 212, 44, 44, 212, 44, 44, 44, 88, 212, 44, 212, 44, 44, 212, 44, 44, 212, 44, 44, 212, 44, 44, 212,
		44, 44, 44, 88, 212, 44, 44, 88, 212, 44, 212, 44, 44, 212, 44, 44, 212, 44, 44, 212, 44, 44, 212, 44, 44, 212, 44, 44, 212, 44, 44, 212, 44, 44, 212, 44, 44, 212, 44, 44, 212, 44, 44, 212, 44, 44, 212, 44, 44, 212, 44, 44, 212, 44, 44, 212, 44, 44, 212, 44, 44, 212, 44, 44, 212,
		44, 44, 212, 44, 44, 212, 44, 44, 212, 44, 44, 212, 44, 44, 212, 44, 44, 212, 44, 44, 212, 44, 44, 212, 44, 44, 212, 44, 44, 212, 44, 44, 212, 44, 44, 212, 44, 44, 212, 44, 44, 212, 44, 44, 212, 44, 44, 212, 44, 44, 212, 44, 212, 44, 44, 44, 88, 212, 44, 44, 88, 212, 44, 44, 88, 212,
		44, 212, 44, 212, 44, 212, 44, 212, 44, 212, 44, 212, 44, 212, 44, 212, 44, 212, 44, 212, 44, 212, 44, 212, 44, 212, 44, 212, 44, 212, 44, 212, 44, 212, 44, 212, 44, 212, 44, 212, 44, 212, 44, 212, 44, 212, 44, 212, 44, 212, 44, 212, 44, 212, 44, 212, 44, 212, 44, 212, 44, 212, 44, 212,
		44, 212, 44, 212, 44, 212, 44, 212, 44, 212, 44, 212, 44, 212, 44, 212, 44, 212, 44, 212, 44, 212, 44, 212, 44, 212, 44, 212, 44, 212, 44, 212, 44, 212, 44, 212, 44, 212, 44, 212, 44, 212, 44, 212, 44, 212, 44, 212, 44, 212, 44, 212, 44, 212, 44, 212, 44, 212, 44, 212, 44, 212, 44, 212, 44, 212, 44, 212, 44,
		212, 44, 212, 44, 212, 44, 212, 44, 212, 44, 212, 44, 212, 44, 212, 44, 212, 44, 212, 44, 212, 44, 212, 44, 212, 44, 212, 44, 212, 44, 212, 44, 212, 44, 212, 44, 212, 44, 212, 44, 212, 44, 212, 44, 212, 44, 212, 44, 212, 44, 212, 44, 212, 44, 212, 44, 212, 44, 212, 44, 212, 44, 212, 44, 212, 44,
		212, 44, 212, 44, 44, 88, 212, 44, 44, 88, 212, 44, 212, 44, 212, 44, 212, 44, 212, 44, 44, 88, 212, 44, 212, 44, 212, 44, 212, 44, 212, 44, 212, 44, 212, 44, 212, 44, 212, 44, 212, 44, 212, 44, 212, 44, 212, 44, 212, 44, 212, 44, 212, 44, 212, 44, 212, 44, 44, 88, 212, 44, 44, 88,
		212, 44, 44, 88, 212, 44, 212, 44, 212, 44, 44, 212, 44, 212, 44, 212, 44, 212, 44, 44, 212, 44, 212, 44, 212, 44, 212, 44, 212, 44, 212, 44, 212, 44, 44, 88, 212, 44, 212, 44, 44, 44, 88, 212, 44, 212, 44, 44, 212, 44, 44, 212, 44, 212, 44, 212, 44, 44, 212, 44, 212, 44, 212, 44, 212, 44, 212, 44, 212, 44, 44, 212, 44, 212, 44, 44, 212, 44, 44, 212,
		44, 212, 44, 212, 44, 212, 44, 212, 44, 212, 44, 212, 44, 212, 44, 212, 44, 212, 44, 212, 44, 44, 212, 44, 212, 44, 212, 44, 212, 44, 212, 44, 212, 44, 212, 44, 212, 44, 212, 44, 212, 44, 212, 44, 212, 44, 44, 212, 44, 44, 212, 44, 212, 44, 212, 44, 212, 44, 212, 44, 212, 44, 212, 44, 212,
		44, 212, 44, 212, 44, 212, 44, 44, 212, 44, 44, 212, 44, 44, 212, 44, 212, 44, 44, 212, 44, 212, 44, 212, 44, 212, 44, 212, 44, 212, 44, 212, 44, 212, 44, 212, 44, 212, 44, 212, 44, 44, 44, 212, 44, 212, 44, 44, 44, 212, 44, 212, 44, 44, 44, 212, 44, 212, 44, 44, 44, 212, 44, 212,
		44, 44, 44, 212, 44, 212, 44, 44, 44, 212, 44, 212, 44, 44, 44, 212, 44, 212, 44, 44, 44, 212, 44, 212, 44, 44, 44, 212, 44, 212, 44, 44, 44, 212, 44, 212, 44, 44, 44, 212, 44, 212, 44, 44, 44, 212, 44, 212, 44, 44, 44, 212, 44, 212, 44, 212, 44, 212, 44, 212, 44, 212, 44, 212, 44, 212,
		44, 212, 44, 212, 44, 212, 44, 212, 44, 212, 44, 212, 44, 212, 44, 212, 44, 212, 44, 212, 44, 212, 44, 212, 44, 212, 44, 212, 44, 212, 44, 212, 44, 212, 44, 212, 44, 212, 44, 212, 44, 212, 44, 212, 44, 212, 44, 212, 44, 44, 212, 44, 44, 212, 44, 44, 212, 44, 44, 212, 44, 44, 212, 44, 44,
		212, 44, 44, 212, 44, 44, 212, 44, 212, 44, 212, 44, 212, 44, 44, 44, 212, 44, 44, 44, 212, 44, 44, 44, 212, 44, 44, 44, 44, 224, 212, 44, 44, 224, 212, 44, 44, 224, 212, 44, 44, 224, 212, 44, 44, 248, 212, 44, 44, 224, 212, 44, 44, 224, 212, 44, 44, 296, 212, 44, 44, 224, 212, 44,
		44, 224, 212, 44, 44, 224, 212, 44, 212, 44, 212, 44, 212, 44, 212, 44, 212, 44, 212, 44, 212, 44, 212, 44, 212, 44,
	};

	LinearAllocator *la = linAllocCreate(44*256, true);
	int i, j;
	S64 ticksStart, ticksEnd;

	for (i=0; i<ARRAY_SIZE(sizes); i++) {
		linAlloc(la, sizes[i]);
	}
	linAllocClear(la);
	ticksStart = timerCpuTicks64();
	for (j=0; j<10000; j++) {
		for (i=0; i<ARRAY_SIZE(sizes); i++) {
			linAlloc(la, sizes[i]);
		}
		linAllocClear(la);
	}
	ticksEnd = timerCpuTicks64();
	printf("\ntook %"FORM_LL"d ticks\n\n", ticksEnd - ticksStart);

	linAllocDestroy(la);
}
