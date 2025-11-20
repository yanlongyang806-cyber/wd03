
#include <new>

extern "C" {
#include "file.h"
#undef FILE
#include "stdtypes.h"
#include "MemAlloc.h"
#undef strcpy_s
}
#include "timing.h"
#undef sprintf
#include <memory>

// Giant hack: When built wi VS2010, we get one C++ heap allocation happening before getting into main, so if the module
//  calls setMemoryAllocators, this one allocation is going to be in the wrong heap, and will crash on process exit
//  To work around this, just "leaking" this allocation on process exit, not calling free.
// If we get more than one of these, we'll probably want a more complex solution (passing a flag so that free and
//  malloc skip the g_malloc_func callback for all C++ allocations would probably be fine).
extern "C" CRTMallocFunc g_malloc_func;
static void *pre_hook_alloc=NULL;
static int pre_hook_count=0;

void operator delete(void* memory)
{
	if (g_malloc_func && memory == pre_hook_alloc)
		return;
	free(memory);
}

void operator delete[](void *memory)
{
	if (g_malloc_func && memory == pre_hook_alloc)
		return;
	free(memory);
}

//#if defined(_DEBUG) || defined(PROFILE)
void * operator new( size_t cb, int nBlockUse, const char* szFileName, int nLine)
{
	void *ret = malloc_timed(cb, nBlockUse, szFileName, nLine);
	if (!g_malloc_func)
	{
		pre_hook_alloc = ret;
		pre_hook_count++;
	} else {
		assert(pre_hook_count<=1);
	}
	return ret;
}

void * operator new[](size_t cb, int nBlockUse, const char* szFileName, int nLine)
{
    void *ret = malloc_timed(cb, nBlockUse, szFileName, nLine);
	if (!g_malloc_func)
	{
		pre_hook_alloc = ret;
		pre_hook_count++;
	} else {
		assert(pre_hook_count<=1);
	}
	return ret;
}
//#else
void * operator new( size_t cb )
{
	void *ret = malloc_timed(cb, _NORMAL_BLOCK, "Unknown C++", 0);
	if (!g_malloc_func)
	{
		pre_hook_alloc = ret;
		pre_hook_count++;
	} else {
		assert(pre_hook_count<=1);
	}
	return ret;
}

void * operator new[]( size_t cb )
{
	void *ret = malloc_timed(cb, _NORMAL_BLOCK, "Unknown C++[]", 0);
	if (!g_malloc_func)
	{
		pre_hook_alloc = ret;
		pre_hook_count++;
	} else {
		assert(pre_hook_count<=1);
	}
	return ret;
}
//#endif
