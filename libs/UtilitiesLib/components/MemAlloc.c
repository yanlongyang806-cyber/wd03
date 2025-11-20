//////////////////////////////////////////////////////////////////////////
// This file no longer has anything called (left over code from the Xbox)
// but does contain useful notes about fill values used by various memory
// systems.

#define MEMALLOC_C
#include "MemAlloc.h"

//////////////////////////////////////////////////////////////////////////
// CRT memory fill values

#define MALLOC_UNINITIALIZED_VALUE		0xCDCDCDCD
#define MALLOC_NOMANSLAND_VALUE			0xFDFDFDFD
#define FREE_VALUE						0xDDDDDDDD

#define HEAPALLOC_UNINITIALIZED_VALUE	0xBAADF00D
#define HEAPFREE_VALUE					0xFEEEFEEE

//////////////////////////////////////////////////////////////////////////
// Other Fill Values

#define MEMORY_POOL_FREED				0xFAFAFAFA
#define MEMORYPOOL_SENTINEL_VALUE		0xFBFBFBFB
#define THREAD_REFERENCE_UNINIT			0xFCFCFCFC
#if _MSC_VER < 1500 // VS 2005
#define _SECURECRT_FILL_BUFFER_PATTERN	0xFD // 0xFDFDFDFD
#else // VS 2008
#define _SECURECRT_FILL_BUFFER_PATTERN	0xFE // 0xFEFEFEFE
#endif
#define WT_OVERRUN_VALUE				0x4BADBEEF

#define PS3_STACK_OVERFLOW_VALUES		0xDE5EA5ED

//////////////////////////////////////////////////////////////////////////

const char *HeapNameFromID(int special_heap)
{
	if(special_heap >= 0 && special_heap <= CRYPTIC_FIRST_HEAP)
		return "Main";

	switch(special_heap)
	{
	case CRYPTIC_CONTAINER_HEAP:
		return "ContainerHeap";
	case CRYPTIC_PACKET_HEAP:
		return "PacketHeap";
	case CRYPTIC_CHURN_HEAP:
		return "ChurnHeap";
	case CRYPTIC_STRUCT_STRING_HEAP:
		return "StructStringHeap";
	case CRYPTIC_TSMP_HEAP:
		return "TSMPHeap";
	case CRYPTIC_SUBSCRIPTION_HEAP:
		return "SubscriptionHeap";
	default:
		return "UnknownHeap";
	}
}


#define PERFINFO_ALLOC_START(func, size)
#define PERFINFO_ALLOC_END()

#if _XBOX
void* physicalmalloc_timed(size_t size, int address, int alignment, int protect, const char *name, const char *filename, int linenumber)
{
	void * result;
	PERFINFO_ALLOC_START("physicalmalloc", size);

	result = XPhysicalAlloc(size,address,alignment,protect);
	memMonitorTrackUserMemory(name?name:"XBOX_PHYSICAL", name?0:1, size, MM_ALLOC);

	PERFINFO_ALLOC_END();
    return result;
}

void physicalfree_timed(void *userData, const char *name)
{
	PERFINFO_ALLOC_START("physicalfree", -1);
	memMonitorTrackUserMemory(name?name:"XBOX_PHYSICAL", name?0:1, -(ptrdiff_t)XPhysicalSize(userData), MM_FREE);
	XPhysicalFree(userData);
	PERFINFO_ALLOC_END();
}

AUTO_RUN_ANON(memBudgetAddMapping("XMemAlloc:D3D:Physical", BUDGET_Unknown););
AUTO_RUN_ANON(memBudgetAddMapping("XMemAlloc:D3D:Virtual", BUDGET_Unknown););
AUTO_RUN_ANON(memBudgetAddMapping("XMemAlloc:D3DX", BUDGET_Unknown););
AUTO_RUN_ANON(memBudgetAddMapping("XMemAlloc:XAudio", BUDGET_Unknown););
AUTO_RUN_ANON(memBudgetAddMapping("XMemAlloc:XHV", BUDGET_Unknown););
AUTO_RUN_ANON(memBudgetAddMapping("XMemAlloc:UnknownPhysical", BUDGET_Unknown););
AUTO_RUN_ANON(memBudgetAddMapping("XMemAlloc:UnknownVirtual", BUDGET_Unknown););
AUTO_RUN_ANON(memBudgetAddMapping("XMemAlloc:ShaderCompiler:XGraphics", BUDGET_Renderer);); // Should be 0 at run-time
AUTO_RUN_ANON(memBudgetAddMapping("XMemAlloc:ShaderCompiler:Physical", BUDGET_Renderer);); // Should be 0 at run-time
AUTO_RUN_ANON(memBudgetAddMapping("XMemAlloc:ShaderCompiler:Virtual", BUDGET_Renderer);); // Should be 0 at run-time

const char *XAllocName(XALLOC_ATTRIBUTES attr)
{
	switch (attr.dwAllocatorId)
	{
		xcase eXALLOCAllocatorId_D3D:
			return attr.dwMemoryType?"XMemAlloc:D3D:Physical":"XMemAlloc:D3D:Virtual";
		xcase eXALLOCAllocatorId_D3DX:
			if (!attr.dwMemoryType) // Shouldn't be any physical D3DX allocs
				return "XMemAlloc:D3DX";
		xcase eXALLOCAllocatorId_XAUDIO:
		acase 151: // eXALLOCAllocatorId_XAUDIO2:
			if (!attr.dwMemoryType)
				return "XMemAlloc:XAudio";
		xcase eXALLOCAllocatorId_XHV:
			if (!attr.dwMemoryType)
				return "XMemAlloc:XHV";
		xcase eXALLOCAllocatorId_XGRAPHICS:
			if (!attr.dwMemoryType)
				return "XMemAlloc:ShaderCompiler:XGraphics";
		xcase eXALLOCAllocatorId_SHADERCOMPILER:
			return attr.dwMemoryType?"XMemAlloc:ShaderCompiler:Physical":"XMemAlloc:ShaderCompiler:Virtual";
	}
	printf("Unknown XMemAlloc allocation type: %d please update XAllocName()\n", attr.dwAllocatorId);
	return attr.dwMemoryType?"XMemAlloc:UnknownPhysical":"XMemAlloc:UnknownVirtual";
}

// TODO: remove this heap of crap and replace with a real heap

static bool g_persistant_physical_hack;
static DWORD g_persistant_physical_thread_id;
static const char *g_xmemalloc_blamee;
static struct {
	U8 *head;
	int offs;
} persistant_physical_allocs[12]; // Vertex shaders were taking around 10 slots, pixel 2 slots
#define PERSISTANT_PHYSICAL_CHUNKSIZE 256*1024
void XMemAllocPersistantPhysical(bool allowed)
{
	DWORD tid = GetCurrentThreadId();
	assert(!g_persistant_physical_thread_id || g_persistant_physical_thread_id == tid); // Can be called only from one thread
	g_persistant_physical_thread_id = tid;
	g_persistant_physical_hack = allowed;
}

void XMemAllocSetBlamee(const char *blamee)
{
	DWORD tid = GetCurrentThreadId();
	assert(!g_persistant_physical_thread_id || g_persistant_physical_thread_id == tid); // Can be called only from one thread
	g_persistant_physical_thread_id = tid;
	g_xmemalloc_blamee = blamee;
}

bool g_print_xmemallocs=false;


// Override default allocators
LPVOID WINAPI XMemAlloc(SIZE_T dwSize, DWORD dwAllocAttributes)
{
	DWORD tid=0;
	LPVOID ret=NULL;
	SIZE_T effSize = dwSize;
	XALLOC_ATTRIBUTES attr = *(XALLOC_ATTRIBUTES*)&dwAllocAttributes;
	PERFINFO_ALLOC_START("XMemAlloc", 1);
	if (g_print_xmemallocs)
		printf("XMemAlloc(%d, type:%d, id:%d, align:%d, protect:%d physical:%d)", dwSize, attr.dwObjectType, attr.dwAllocatorId, attr.dwAlignment, attr.dwMemoryProtect, attr.dwMemoryType);
#define MAX_ALIGNMENT XALLOC_PHYSICAL_ALIGNMENT_256
#define DEFAULT_PROTECT XALLOC_MEMPROTECT_WRITECOMBINE
	if (g_persistant_physical_hack && attr.dwMemoryProtect == DEFAULT_PROTECT && attr.dwMemoryType==XALLOC_MEMTYPE_PHYSICAL && attr.dwAlignment >= XALLOC_PHYSICAL_ALIGNMENT_4 && attr.dwAlignment <= MAX_ALIGNMENT)
	{
		if ((tid=GetCurrentThreadId()) == g_persistant_physical_thread_id)
		{
			int i;
			int align = 1 << attr.dwAlignment;
			int alignmask = align-1;
			int useme=-1;
			int bestslack=0;

			// First look for pre-aligned, perfect match
			for (i=0; i<ARRAY_SIZE(persistant_physical_allocs) && useme==-1; i++) 
			{
				if (((int)dwSize <= PERSISTANT_PHYSICAL_CHUNKSIZE - persistant_physical_allocs[i].offs) &&
					(persistant_physical_allocs[i].offs & alignmask) == 0 &&
					persistant_physical_allocs[i].head)
				{
					useme = i;
				}
			}
			// Now, look for any place it fits
			for (i=0; i<ARRAY_SIZE(persistant_physical_allocs); i++) 
			{
				if ((int)dwSize <= PERSISTANT_PHYSICAL_CHUNKSIZE - ((persistant_physical_allocs[i].offs + alignmask) & ~alignmask))
				{
					int slack = ((persistant_physical_allocs[i].offs + alignmask) & ~alignmask) - persistant_physical_allocs[i].offs;
					if (useme == -1 || slack < bestslack && persistant_physical_allocs[i].head)
					{
						useme = i;
						bestslack = slack;
					}
				}
			}
			if (useme!=-1)
			{
				if (!persistant_physical_allocs[useme].head) {
					XALLOC_ATTRIBUTES attrNew;
					attrNew.dwObjectType = 0;
					attrNew.dwHeapTracksAttributes = 0;
					attrNew.dwMustSucceed = 1;
					attrNew.dwFixedSize = 0;
					attrNew.dwAllocatorId = 0;
					attrNew.dwAlignment = MAX_ALIGNMENT;
					attrNew.dwMemoryProtect = DEFAULT_PROTECT;
					attrNew.dwZeroInitialize = 1;
					attrNew.dwMemoryType = XALLOC_MEMTYPE_PHYSICAL;
					persistant_physical_allocs[useme].head = XMemAllocDefault(PERSISTANT_PHYSICAL_CHUNKSIZE, *(DWORD*)&attrNew);
				}
				ret = persistant_physical_allocs[useme].head + ((persistant_physical_allocs[useme].offs + alignmask) & ~alignmask);
				effSize = dwSize + (U8*)ret - (persistant_physical_allocs[useme].head + persistant_physical_allocs[useme].offs);
				persistant_physical_allocs[useme].offs += effSize;
				assert(persistant_physical_allocs[useme].offs <= PERSISTANT_PHYSICAL_CHUNKSIZE);
				memMonitorTrackUserMemory(XAllocName(attr), 1, effSize, MM_ALLOC);
				if (g_print_xmemallocs)
					printf(" using PERSISTANT_PHYSICAL\n");
			}
		}
	}
	if (!ret)
	{
		effSize = attr.dwMemoryType?((dwSize+4095)&~(4096-1)):dwSize;
		memMonitorTrackUserMemory(XAllocName(attr), 1, effSize, MM_ALLOC);
		ret = XMemAllocDefault(dwSize, dwAllocAttributes);
		if (g_print_xmemallocs)
			printf("\n");
	}
	if (g_xmemalloc_blamee)
	{
		if (!tid)
			tid = GetCurrentThreadId();
		if (tid == g_persistant_physical_thread_id)
		{
			memMonitorTrackUserMemory(g_xmemalloc_blamee, 1, effSize, MM_ALLOC);
		}
	}
	PERFINFO_ALLOC_END();
	return ret;
}
VOID WINAPI XMemFree(PVOID pAddress, DWORD dwAllocAttributes)
{
	XALLOC_ATTRIBUTES attr = *(XALLOC_ATTRIBUTES*)&dwAllocAttributes;
	DWORD dwSize;
	int i;
	bool bDoNotFree=false;
	if (!pAddress)
		return;
	PERFINFO_ALLOC_START("XMemFree", -1);
	for (i=0; i<ARRAY_SIZE(persistant_physical_allocs); i++) {
		if ((U8*)pAddress >= persistant_physical_allocs[i].head &&
			(U8*)pAddress < persistant_physical_allocs[i].head + persistant_physical_allocs[i].offs)
		{
			// Shouldn't happen ('cept during shader reload), but let it slide
			bDoNotFree = true;
		}
	}
	if (!bDoNotFree)
	{
		dwSize = XMemSizeDefault(pAddress, dwAllocAttributes);
		if (g_print_xmemallocs)
			printf("XMemFree(%d, type:%d, id:%d, align:%d, physical:%d)\n", dwSize, attr.dwObjectType, attr.dwAllocatorId, attr.dwAlignment, attr.dwMemoryType);
		memMonitorTrackUserMemory(XAllocName(attr), 1, -(int)(attr.dwMemoryType?((dwSize+4095)&~(4096-1)):dwSize), MM_FREE);
		XMemFreeDefault(pAddress, dwAllocAttributes);
		if (g_xmemalloc_blamee)
		{
			if (GetCurrentThreadId() == g_persistant_physical_thread_id)
			{
				// Shouldn't happen ('cept during shader reload), but let it slide
				memMonitorTrackUserMemory(g_xmemalloc_blamee, 1, -(int)(attr.dwMemoryType?((dwSize+4095)&~(4096-1)):dwSize), MM_FREE);
			}
		}
	}

	PERFINFO_ALLOC_END();
}

#else
void XMemAllocPersistantPhysical(bool allowed)
{
}
void XMemAllocSetBlamee(const char *blamee)
{
}
#endif
