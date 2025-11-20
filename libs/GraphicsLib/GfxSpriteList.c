#include "GfxSpriteList.h"
#include "GfxTexturesInline.h"
#include "GfxTexAtlas.h"
#include "RdrDrawable.h"
#include "RdrDrawList.h"

#include "MemoryPool.h"
#include "MemRef.h"
#include "LinearAllocator.h"
#include "Color.h"
#include "utils.h"
#include "UnitSpec.h"

AUTO_RUN_ANON(memBudgetAddMapping(__FILE__, BUDGET_Renderer););

typedef struct AtlasTex AtlasTex;
typedef struct BasicTexture BasicTexture;

//Use a dynarray to store temp sprite objects instead of a memory pool. This is faster since there is less
//work done in allocating a struct but it also means the linked list code needs to work with indices
#define USE_DYNARRAY
//Use an index buffer to render sprites. This avoids the overhead of copying around all the vertex data
//when sorting it but requires more calls to malloc/free and the use of temp VBOs in the render thread.
#define USE_IDX_BUFFER

//this slows it down a bit
//#define ASSERT_ON_BAD_REF

#ifdef USE_DYNARRAY

typedef int EntryRef;

#ifdef ASSERT_ON_BAD_REF
#define _ENTRY_TO_REF(sl, e) (assert((e - sl->dynArrayElements.data) >= 0 && (e - sl->dynArrayElements.data) < sl->dynArrayElements.count), e ? (e - sl->dynArrayElements.data) + 1 : (assert(0),0))
#define _REF_TO_ENTRY(sl, r) (assert(r > 0 && r <= sl->dynArrayElements.count), r ? sl->dynArrayElements.data + (r-1) : (assert(0),0))
#else
#define _ENTRY_TO_REF(sl, e) ((e - sl->dynArrayElements.data) + 1)
#define _REF_TO_ENTRY(sl, r) (sl->dynArrayElements.data + (r-1))
#endif

#else

typedef GfxSpriteListEntry* EntryRef;
#define _ENTRY_TO_REF(sl, e) (e)
#define _REF_TO_ENTRY(sl, r) (r)

#endif

static int gfxSpriteListMaxWasteMult = 4;
static int gfxSpriteListPrintPerFrameAlloc = 0;
static int gfxSpriteListSpamPoolState = 0;
static int gfxSpriteListReallocAll = 0;
//make this pretty large like a few seconds so that tooltips dont cause
//it to repeatedly purge
#ifdef USE_IDX_BUFFER
static int gfxSpriteListCleanupsBeforeFree = 200;
#else
static int gfxSpriteListCleanupsBeforeFree = 100; //there are half as many cleanups in this mode
#endif
static int gfxSpriteListMinAllocKB = 100;
static int gfxSpriteListMinAllocIncrementKB = 50;

//Allow buffers up to this times the requested size to be used instead of freeing it and getting a smaller one
AUTO_CMD_INT(gfxSpriteListMaxWasteMult, gfxSpriteListMaxWasteMult);

//spew debug spam when need to add to the pool
AUTO_CMD_INT(gfxSpriteListPrintPerFrameAlloc, gfxSpriteListPrintPerFrameAlloc);

//free all the pools and load new ones
AUTO_CMD_INT(gfxSpriteListReallocAll, gfxSpriteListReallocAll);

//number of pool cleanups before we free unused buffers
AUTO_CMD_INT(gfxSpriteListCleanupsBeforeFree, gfxSpriteListCleanupsBeforeFree);

//the minimum block size in KB
AUTO_CMD_INT(gfxSpriteListMinAllocKB, gfxSpriteListMinAllocKB);

//the step size up from the minimum block size in KB
AUTO_CMD_INT(gfxSpriteListMinAllocIncrementKB, gfxSpriteListMinAllocIncrementKB);


//output the info the sprite buffer pool constantly
AUTO_CMD_INT(gfxSpriteListSpamPoolState, gfxSpriteListSpamPoolState);

typedef struct GfxSpriteListEntry
{
	F32 zValue;

	EntryRef nextRef;	

	EntryRef sameZListRef;
	EntryRef sameZListTailRef;

#ifdef USE_IDX_BUFFER
	U32 index; 
#else
	RdrSpriteState spriteState;
	RdrSpriteVertex spriteVerts[4];
#endif
	AtlasTex* atex[2];
	BasicTexture* btex[2];
} GfxSpriteListEntry;

typedef struct GfxSpriteListMemPoolEntry
{
	U32		rendersUnused, size;
	void*	block;
} GfxSpriteListMemPoolEntry;

typedef struct GfxSpriteList
{
	EntryRef sortedHeadRef;
	EntryRef sortedTailRef;
	EntryRef sortedLastAddedRef;

	EntryRef headRef3D;
	EntryRef tailRef3D;

	U32 count, count3D;
	bool storeTexPointers, isSpriteCache;

	#ifdef USE_DYNARRAY
	struct 
	{
		GfxSpriteListEntry* data;
		int count;
		int size;
	} dynArrayElements;
	#else
	MP_DEFINE_MEMBER(GfxSpriteListEntry);
	#endif
	
	#ifdef USE_IDX_BUFFER
	
	struct 
	{
		RdrSpriteState* data;
		int count;
		int size;
	} dynArrayStates;
	struct 
	{
		RdrSpriteVertex* data;
		int count;
		int size;
	} dynArrayVerts;

	//3d sprites are rendered at a different time so they need to be stored separately since the render thread takes over the buffers
	struct 
	{
		RdrSpriteState* data;
		int count;
		int size;
	} dynArrayStates3D;
	struct 
	{
		RdrSpriteVertex* data;
		int count;
		int size;
	} dynArrayVerts3D;

	#endif

	struct  
	{
		GfxSpriteListMemPoolEntry* data;
		int count, size;
	} dynArrayMemPoolFree, dynArrayMemPoolAlloc;


} GfxSpriteList;

__forceinline static void gfxSpriteSetupTexHandles(RdrSpriteState* sstate, AtlasTex *atex1, BasicTexture *btex1, AtlasTex *atex2, BasicTexture *btex2)
{
	RdrSpriteEffect effect = sstate->sprite_effect;
	
	PERFINFO_AUTO_START_FUNC_L3();
	
	if (btex1)
		sstate->tex_handle1 = texDemandLoadFixedInvis(btex1);
	else if (atex1)
		sstate->tex_handle1 = atlasDemandLoadTexture(atex1);
	else
		sstate->tex_handle1 = 0;

	//Tex_handle2 is unioned with other stuff for these types
	if (effect < RdrSpriteEffect_DistField1Layer || effect > RdrSpriteEffect_DistField2LayerGradient)
	{
		if (btex2)
			sstate->tex_handle2 = texDemandLoadFixedInvis(btex2);
		else if (atex2)
			sstate->tex_handle2 = atlasDemandLoadTexture(atex2);
		else 
			sstate->tex_handle2 = 0;

		//LDM: this is a kind of a hack but some UI stuff depends on this
		if (sstate->tex_handle2)
		{
			if (sstate->sprite_effect == RdrSpriteEffect_Desaturate || sstate->sprite_effect == RdrSpriteEffect_Desaturate_TwoTex)
				sstate->sprite_effect = RdrSpriteEffect_Desaturate_TwoTex;
			else
				sstate->sprite_effect = RdrSpriteEffect_TwoTex;
		}
	}
	
	PERFINFO_AUTO_STOP_L3();
}


static void* gfxSpriteListMemPoolAlloc(GfxSpriteList* sl, U32 size, U32* actualSize)
{
	int i = 0;
	GfxSpriteListMemPoolEntry* newEntry;
	GfxSpriteListMemPoolEntry* goodEntry = NULL;
	U32 minAllocBytes = (U32)gfxSpriteListMinAllocKB*1024;
	U32 minAllocIncrBytes = (U32)gfxSpriteListMinAllocIncrementKB*1024;
	PERFINFO_AUTO_START_FUNC();

	//dont allocate really small sizes, there's no point
	
	if (size <= minAllocBytes)
	{
		size = minAllocBytes;
	}
	else
	{
		size = minAllocBytes + ((int)ceilf((float)(size - minAllocBytes)/(float)minAllocIncrBytes))*minAllocIncrBytes;
	}
	

	for (i = 0; i < sl->dynArrayMemPoolFree.count; i++)
	{
		GfxSpriteListMemPoolEntry* entry = sl->dynArrayMemPoolFree.data + i;
		U32 minValue = size;
		U32 maxValue = size * gfxSpriteListMaxWasteMult;
		if (entry->size >= minValue && entry->size <= maxValue) //if this one is basically the right size use it (the test is more picky if aggressive trimming is on)
		{
			if (!goodEntry || goodEntry->size > entry->size || goodEntry->rendersUnused > entry->rendersUnused)
				goodEntry = entry; //look for the smallest, newest free one that matches
		}
	}

	if (goodEntry)
	{
		goodEntry->rendersUnused = 0;
		if (actualSize)
			*actualSize = goodEntry->size;
		newEntry = dynArrayAddStruct_no_memset(sl->dynArrayMemPoolAlloc.data, sl->dynArrayMemPoolAlloc.count, sl->dynArrayMemPoolAlloc.size);
		*newEntry = *goodEntry;
		//remove the entry
		if (--sl->dynArrayMemPoolFree.count)
			*goodEntry = sl->dynArrayMemPoolFree.data[sl->dynArrayMemPoolFree.count];

		PERFINFO_AUTO_STOP_FUNC();
		return newEntry->block;
	}

	//if we got here we didnt find it

	newEntry = dynArrayAddStruct_no_memset(sl->dynArrayMemPoolAlloc.data, sl->dynArrayMemPoolAlloc.count, sl->dynArrayMemPoolAlloc.size);
	newEntry->rendersUnused = 0;
	newEntry->size = size;
	newEntry->block = memrefAlloc(size);

	if (actualSize)
		*actualSize = size;
	
	if (gfxSpriteListPrintPerFrameAlloc)
		OutputDebugStringf("GfxSpriteList: Had to allocate %i bytes.\n", size);

	PERFINFO_AUTO_STOP_FUNC();
	return newEntry->block;
}

//this is the same as the one in utils_opt.c but with the memsets removed and calling the custom allocator
static void * gfxSpriteListMemPoolAlloc_dynArrayAdd_dbg(GfxSpriteList* sl, void **basep,int struct_size,int *countPtr,int *max_countPtr,int num_structs)
{
	char	*base = *basep;
	int count = *countPtr;
	int max_count = *max_countPtr;

	PERFINFO_AUTO_START_FUNC();

	if (count > max_count - num_structs)
	{
		char	*newbase;
		U32 got_size;
		if (!max_count)
			max_count = num_structs;
		(max_count) <<= 1;
		if (num_structs > 1)
			(max_count) += num_structs;
		newbase = gfxSpriteListMemPoolAlloc(sl, struct_size * max_count, &got_size); //the old one will get cleaned up in gfxSpriteListMemPoolCleanup
		memcpy_fast(newbase, base, struct_size * count);
		assert(newbase && (int)got_size/struct_size >= max_count);
		*max_countPtr = got_size/struct_size;
		*basep = base = newbase;
	}

	count+=num_structs;
	*countPtr = count;

	PERFINFO_AUTO_STOP();

	return base + struct_size * (count - num_structs);
}

#define gfxSpriteListMemPoolAlloc_dynArrayAdd_no_memset(sl, basep,struct_size,count,max_count,num_structs)			\
	(((int)((count)+(num_structs))>(int)max_count) ?															\
	gfxSpriteListMemPoolAlloc_dynArrayAdd_dbg(sl,(void**)&(basep),(struct_size),&(count),&(max_count),(num_structs)):	\
	(count+=(num_structs),((char*)(basep)+(count-(num_structs))*(struct_size))))

//you can only call this after you have sent your buffers to the render thread
//IF YOU ALLOC A BUFFER THEN CALL THIS BEFORE SENDING IT, IT WILL GET FREED!
static void gfxSpriteListMemPoolCleanup(GfxSpriteList* sl)
{
	int i = 0;
	int sz = 0;
	int inFlight = 0;
	PERFINFO_AUTO_START_FUNC();

	for (i = 0; i < sl->dynArrayMemPoolFree.count; i++)
	{
		GfxSpriteListMemPoolEntry* entry = sl->dynArrayMemPoolFree.data + i;
		entry->rendersUnused++;
		sz += entry->size;
		if (entry->rendersUnused > (U32)gfxSpriteListCleanupsBeforeFree)
		{
			int result = memrefDecrement(entry->block);
			assert(result == 0); //we should have the only reference

			if (gfxSpriteListPrintPerFrameAlloc)
				OutputDebugStringf("GfxSpriteList: freed %i bytes.\n", entry->size);

			if (--sl->dynArrayMemPoolFree.count)
				*entry = sl->dynArrayMemPoolFree.data[sl->dynArrayMemPoolFree.count];
			i--;
		}
	}

	for (i = 0; i < sl->dynArrayMemPoolAlloc.count; i++)
	{
		GfxSpriteListMemPoolEntry* entry = sl->dynArrayMemPoolAlloc.data + i;
		int refCount;
		sz += entry->size;
		memrefIncrement(entry->block); //this should be safe since the only other thing that calls memRefDecrement is the renderthread and it can never trigger a free
		refCount = memrefDecrement(entry->block);
		if (refCount == 1)
		{
			//we have the only reference
			GfxSpriteListMemPoolEntry* newEntry;
			entry->rendersUnused = 0;
			newEntry = dynArrayAddStruct_no_memset(sl->dynArrayMemPoolFree.data, sl->dynArrayMemPoolFree.count, sl->dynArrayMemPoolFree.size);
			*newEntry = *entry;
			//remove the entry
			if (--sl->dynArrayMemPoolAlloc.count)
				*entry = sl->dynArrayMemPoolAlloc.data[sl->dynArrayMemPoolAlloc.count];
			i--;
		}
		else
		{
			inFlight++;
		}
	}

	if (gfxSpriteListSpamPoolState == 1)
		OutputDebugStringf("Free: %i Alloc: %i %s\n", sl->dynArrayMemPoolFree.count, sl->dynArrayMemPoolAlloc.count, friendlyBytes(sz));
	else if (gfxSpriteListSpamPoolState == 2)
		OutputDebugStringf("In flight: %i\n", inFlight);

	PERFINFO_AUTO_STOP_FUNC();
}

static void gfxSpriteListMemPoolFreeAll(GfxSpriteList* sl)
{
	int i = 0;

	PERFINFO_AUTO_START_FUNC();

	for (i = 0; i < sl->dynArrayMemPoolFree.count; i++)
	{
		GfxSpriteListMemPoolEntry* entry = sl->dynArrayMemPoolFree.data + i;
		int result = memrefDecrement(entry->block);
		assert(result == 0); //we should have the only reference

		if (gfxSpriteListPrintPerFrameAlloc)
			OutputDebugStringf("GfxSpriteList: freed %i bytes.\n", entry->size);
	}

	for (i = 0; i < sl->dynArrayMemPoolAlloc.count; i++)
	{
		GfxSpriteListMemPoolEntry* entry = sl->dynArrayMemPoolAlloc.data + i;
		memrefDecrement(entry->block);

		if (gfxSpriteListPrintPerFrameAlloc)
			OutputDebugStringf("GfxSpriteList: derefed %i in-flight bytes.\n", entry->size);
	}

	free(sl->dynArrayMemPoolFree.data);
	free(sl->dynArrayMemPoolAlloc.data);
	memset(&sl->dynArrayMemPoolAlloc,0, sizeof(sl->dynArrayMemPoolAlloc));
	memset(&sl->dynArrayMemPoolFree,0, sizeof(sl->dynArrayMemPoolFree));

	PERFINFO_AUTO_STOP_FUNC();
}


GfxSpriteList* gfxCreateSpriteList(U32 mpSize, bool storeTexPointers, bool isSpriteCache)
{
	GfxSpriteList* ret = calloc(1, sizeof(GfxSpriteList));
	#ifdef USE_DYNARRAY
	#else
	MP_CREATE_MEMBER(ret, GfxSpriteListEntry, mpSize);
	#endif
	ret->storeTexPointers = storeTexPointers;
	ret->isSpriteCache = isSpriteCache;
	return ret;
}

void gfxDestroySpriteList(GfxSpriteList* sl)
{
	#ifdef USE_DYNARRAY
	free(sl->dynArrayElements.data); //the pool is only used for stuff sent to the render thread
	#else
	MP_DESTROY_MEMBER(sl, GfxSpriteListEntry);
	#endif

#ifdef USE_IDX_BUFFER
	if (sl->isSpriteCache) //sprite caches dont use the memory pool since there's no point
	{
		free(sl->dynArrayStates.data);
		free(sl->dynArrayVerts.data);
	}
	free(sl->dynArrayStates3D.data);
	free(sl->dynArrayVerts3D.data);
#endif
	if (!sl->isSpriteCache)
		gfxSpriteListMemPoolFreeAll(sl);

	free(sl);
}

static void gfxClearSpriteListSorted(GfxSpriteList* sl)
{
#ifdef USE_IDX_BUFFER
	sl->dynArrayStates.count = 0;
	sl->dynArrayVerts.count = 0;
#endif

	sl->sortedHeadRef = 0;
	sl->sortedTailRef = 0;
	sl->sortedLastAddedRef = 0;
	sl->count = 0;
}

static void gfxClearSpriteList3D(GfxSpriteList* sl)
{
#ifdef USE_IDX_BUFFER
	sl->dynArrayStates3D.count = 0;
	sl->dynArrayVerts3D.count = 0;
#endif

	sl->headRef3D = 0;
	sl->tailRef3D = 0;
	sl->count3D = 0;
}

void gfxClearSpriteList(GfxSpriteList* sl)
{
	gfxClearSpriteListSorted(sl);
	gfxClearSpriteList3D(sl);

#ifdef USE_DYNARRAY
	sl->dynArrayElements.count = 0;
#else
	mpFreeAll(sl->MP_NAME(GfxSpriteListEntry)); //this is shared between both
#endif

}


GfxSpriteListEntry* gfxStartAddSpriteToList(GfxSpriteList* sl, F32 zvalue, bool b3D, RdrSpriteState** outSpriteState, RdrSpriteVertex** outFourVerts)
{
	GfxSpriteListEntry* entry;
#ifdef USE_IDX_BUFFER
	int entryIdx;
#endif //USE_IDX_BUFFER
	PERFINFO_AUTO_START_FUNC_L3();

	#ifdef USE_DYNARRAY
	entry = dynArrayAdd_no_memset(sl->dynArrayElements.data, sizeof(GfxSpriteListEntry), sl->dynArrayElements.count, sl->dynArrayElements.size, 1);
	#else
	entry = MP_ALLOC_MEMBER(sl, GfxSpriteListEntry);
	#endif
	entry->zValue = zvalue;

#ifdef USE_IDX_BUFFER
	if (b3D)
		entryIdx = sl->dynArrayStates3D.count;
	else
		entryIdx = sl->dynArrayStates.count;

	entry->index = entryIdx; //we need to store this since we cant get it by subtracting pointers

	if (b3D)
	{
		*outSpriteState = dynArrayAdd_no_memset(sl->dynArrayStates3D.data, sizeof(RdrSpriteState), sl->dynArrayStates3D.count, sl->dynArrayStates3D.size, 1);
		*outFourVerts = dynArrayAdd_no_memset(sl->dynArrayVerts3D.data, sizeof(RdrSpriteVertex), sl->dynArrayVerts3D.count, sl->dynArrayVerts3D.size, 4);
	}
	else
	{
		if (sl->isSpriteCache)
		{
			//we dont use the memory pool for sprite caches since we dont need to hand off the memory to the render thread every frame
			*outSpriteState = dynArrayAdd_no_memset(sl->dynArrayStates.data, sizeof(RdrSpriteState), sl->dynArrayStates.count, sl->dynArrayStates.size, 1);
			*outFourVerts = dynArrayAdd_no_memset(sl->dynArrayVerts.data, sizeof(RdrSpriteVertex), sl->dynArrayVerts.count, sl->dynArrayVerts.size, 4);
		}
		else
		{
			*outSpriteState = gfxSpriteListMemPoolAlloc_dynArrayAdd_no_memset(sl, sl->dynArrayStates.data, sizeof(RdrSpriteState), sl->dynArrayStates.count, sl->dynArrayStates.size, 1);
			*outFourVerts = gfxSpriteListMemPoolAlloc_dynArrayAdd_no_memset(sl ,sl->dynArrayVerts.data, sizeof(RdrSpriteVertex), sl->dynArrayVerts.count, sl->dynArrayVerts.size, 4);
		}
	}
	

#ifdef ASSERT_ON_BAD_REF
	if (b3D)
	{
		assert(sl->dynArrayStates3D.count*4 == sl->dynArrayVerts3D.count);
		assert(entryIdx == sl->dynArrayStates3D.count-1);
	}
	else
	{
		assert(sl->dynArrayStates.count*4 == sl->dynArrayVerts.count);
		assert(entryIdx == sl->dynArrayStates.count-1);

	}
#endif //ASSERT_ON_BAD_REF

#else //USE_IDX_BUFFER
	*outSpriteState = &entry->spriteState; //these are inside the element itself
	*outFourVerts = entry->spriteVerts;
#endif //USE_IDX_BUFFER
	PERFINFO_AUTO_STOP_L3();

	return entry;
}
#define NO_COUNTS




void gfxInsertSpriteListEntry(GfxSpriteList* sl, GfxSpriteListEntry* entry, bool is3D)
{
	#define ENTRY_TO_REF(e) _ENTRY_TO_REF(sl, e)
	#define REF_TO_ENTRY(r) _REF_TO_ENTRY(sl, r)

	EntryRef entryRef;

	PERFINFO_AUTO_START_FUNC_L3();
	
	entry->sameZListRef = 0;
	entry->sameZListTailRef = 0;
	entry->nextRef = 0;

	//cache this so we dont call ENTRY_TO_REF over and over in the case where it actually does something
	entryRef = ENTRY_TO_REF(entry); 

	if (is3D)
	{
		#ifndef NO_COUNTS
		ADD_MISC_COUNT(1, "threeD");
		#endif
		if (sl->tailRef3D)
		{
			REF_TO_ENTRY(sl->tailRef3D)->nextRef = entryRef;
		}

		entry->nextRef = 0;
		sl->tailRef3D = entryRef;

		if (!sl->headRef3D)
			sl->headRef3D = entryRef;

		sl->count3D++;

		PERFINFO_AUTO_STOP_L3();
		return;
	}

	sl->count++;

	//It's the first one
	if (sl->sortedHeadRef == 0 && sl->sortedTailRef == 0)
	{
		#ifndef NO_COUNTS
		ADD_MISC_COUNT(1, "in order");
		#endif
		sl->sortedLastAddedRef = sl->sortedHeadRef = sl->sortedTailRef = entryRef;
		PERFINFO_AUTO_STOP_L3();
		return;
	}
	
	assert(sl->sortedHeadRef && sl->sortedTailRef && sl->sortedLastAddedRef);

	//check the easy cases first
	if (entry->zValue == REF_TO_ENTRY(sl->sortedLastAddedRef)->zValue)
	{
		EntryRef oldTailRef = REF_TO_ENTRY(sl->sortedLastAddedRef)->sameZListTailRef;
		REF_TO_ENTRY(sl->sortedLastAddedRef)->sameZListTailRef = entryRef;
		if (oldTailRef)
			REF_TO_ENTRY(oldTailRef)->sameZListRef = entryRef;
		else
			REF_TO_ENTRY(sl->sortedLastAddedRef)->sameZListRef = entryRef;

		//dont need to change sortedLastAdded
#ifndef NO_COUNTS
		ADD_MISC_COUNT(1, "same as last");
#endif
	}
	//just after the last ref
	else if (entry->zValue > REF_TO_ENTRY(sl->sortedLastAddedRef)->zValue && REF_TO_ENTRY(sl->sortedLastAddedRef)->nextRef && entry->zValue < REF_TO_ENTRY(REF_TO_ENTRY(sl->sortedLastAddedRef)->nextRef)->zValue)
	{
		entry->nextRef = REF_TO_ENTRY(sl->sortedLastAddedRef)->nextRef;
		REF_TO_ENTRY(sl->sortedLastAddedRef)->nextRef = entryRef;
		
		sl->sortedLastAddedRef = entryRef;
#ifndef NO_COUNTS
		ADD_MISC_COUNT(1, "between last and next");
#endif
	}
	else if (entry->zValue == REF_TO_ENTRY(sl->sortedHeadRef)->zValue)
	{
		EntryRef oldTailRef = REF_TO_ENTRY(sl->sortedHeadRef)->sameZListTailRef;
		REF_TO_ENTRY(sl->sortedHeadRef)->sameZListTailRef = entryRef;
		if (oldTailRef)
			REF_TO_ENTRY(oldTailRef)->sameZListRef = entryRef;
		else
			REF_TO_ENTRY(sl->sortedHeadRef)->sameZListRef = entryRef;

		sl->sortedLastAddedRef = sl->sortedHeadRef; //only update this to the root node
		#ifndef NO_COUNTS
		ADD_MISC_COUNT(1, "same as front");
		#endif
	}
	else if (entry->zValue < REF_TO_ENTRY(sl->sortedHeadRef)->zValue)
	{
		entry->nextRef = sl->sortedHeadRef;
		sl->sortedHeadRef = entryRef;

		sl->sortedLastAddedRef = entryRef; //only update this to the root node
		#ifndef NO_COUNTS
		ADD_MISC_COUNT(1, "at front");
		#endif
	}
	else if (entry->zValue == REF_TO_ENTRY(sl->sortedTailRef)->zValue)
	{
		EntryRef oldTailRef = REF_TO_ENTRY(sl->sortedTailRef)->sameZListTailRef;
		REF_TO_ENTRY(sl->sortedTailRef)->sameZListTailRef = entryRef;
		if (oldTailRef)
			REF_TO_ENTRY(oldTailRef)->sameZListRef = entryRef;
		else
			REF_TO_ENTRY(sl->sortedTailRef)->sameZListRef = entryRef;

		sl->sortedLastAddedRef = sl->sortedTailRef; //only update this to the root node
		#ifndef NO_COUNTS
		ADD_MISC_COUNT(1, "in order (same)");
		#endif
	}
	else if (entry->zValue >= REF_TO_ENTRY(sl->sortedTailRef)->zValue)
	{
		entry->nextRef = 0;
		REF_TO_ENTRY(sl->sortedTailRef)->nextRef = entryRef;
		sl->sortedTailRef = entryRef;

		sl->sortedLastAddedRef = entryRef; //only update this to the root node
		#ifndef NO_COUNTS
		ADD_MISC_COUNT(1, "in order");
		#endif
	}
	else
	{
		//we need to search but we can skip the head
		EntryRef curNodeRef = REF_TO_ENTRY(sl->sortedHeadRef)->nextRef;
		EntryRef prevNodeRef = sl->sortedHeadRef;
		int travCount = 1;

		// If this entry is higher than the last added entry, begin search there
		if (entry->zValue > REF_TO_ENTRY(sl->sortedLastAddedRef)->zValue){
			curNodeRef = REF_TO_ENTRY(sl->sortedLastAddedRef)->nextRef;
			prevNodeRef = sl->sortedLastAddedRef;
		}

		while(curNodeRef)
		{
			GfxSpriteListEntry* curNode = REF_TO_ENTRY(curNodeRef);
			if (entry->zValue == curNode->zValue)
			{
				EntryRef oldTailRef = curNode->sameZListTailRef;
				curNode->sameZListTailRef = entryRef;
				if (oldTailRef)
					REF_TO_ENTRY(oldTailRef)->sameZListRef = entryRef;
				else
					curNode->sameZListRef = entryRef;

				sl->sortedLastAddedRef = curNodeRef; //only update this to the root node
				#ifndef NO_COUNTS
				ADD_MISC_COUNT(1, "out of order but found same");
				ADD_MISC_COUNT(travCount, "out of order (nodes checked)");
				#endif
				break;
			}
			else if (entry->zValue < curNode->zValue)
			{
				REF_TO_ENTRY(prevNodeRef)->nextRef = entryRef;
				entry->nextRef = curNodeRef;
				sl->sortedLastAddedRef = entryRef; //only update this to the root node
				#ifndef NO_COUNTS
				ADD_MISC_COUNT(1, "out of order");
				ADD_MISC_COUNT(travCount, "out of order (nodes checked)");
				#endif
				break;
			}
			prevNodeRef = curNodeRef;
			curNodeRef = curNode->nextRef; travCount++;
			
		}
		assertmsg(curNodeRef, "This should never happen");
	}

	PERFINFO_AUTO_STOP_L3();

	#undef ENTRY_TO_REF
	#undef REF_TO_ENTRY
}

//we need to allocate a new entry in the correct memory pool
static GfxSpriteListEntry* gfxInsertSpriteListEntryFromOtherList(GfxSpriteList* sl, GfxSpriteList* srcList, GfxSpriteListEntry* entry, bool is3D)
{
	GfxSpriteListEntry* newEntry;
	PERFINFO_AUTO_START_FUNC();

#ifdef USE_DYNARRAY
	newEntry = dynArrayAdd_no_memset(sl->dynArrayElements.data, sizeof(GfxSpriteListEntry), sl->dynArrayElements.count, sl->dynArrayElements.size, 1);
#else
	newEntry = MP_ALLOC_MEMBER(sl, GfxSpriteListEntry);
#endif
	memcpy(newEntry, entry, sizeof(GfxSpriteListEntry));

#ifdef USE_IDX_BUFFER
	{
		RdrSpriteState* newState;
		RdrSpriteVertex* newVerts;
		int srcEntryIdx, newEntryIdx;
		
		srcEntryIdx = entry->index;

		if (is3D)
		{
			newEntryIdx = sl->dynArrayStates3D.count;
			newState = dynArrayAdd_no_memset(sl->dynArrayStates3D.data, sizeof(RdrSpriteState), sl->dynArrayStates3D.count, sl->dynArrayStates3D.size, 1);
			newVerts = dynArrayAdd_no_memset(sl->dynArrayVerts3D.data, sizeof(RdrSpriteVertex), sl->dynArrayVerts3D.count, sl->dynArrayVerts3D.size, 4);
			memcpy(newState, srcList->dynArrayStates3D.data+srcEntryIdx, sizeof(RdrSpriteState));
			memcpy(newVerts, srcList->dynArrayVerts3D.data+srcEntryIdx*4, sizeof(RdrSpriteVertex)*4);
		}
		else
		{
			newEntryIdx = sl->dynArrayStates.count;
			if (sl->isSpriteCache)
			{
				//we dont use the memory pool for sprite caches since we dont need to hand off the memory to the render thread every frame
				newState = dynArrayAdd_no_memset(sl->dynArrayStates.data, sizeof(RdrSpriteState), sl->dynArrayStates.count, sl->dynArrayStates.size, 1);
				newVerts = dynArrayAdd_no_memset(sl->dynArrayVerts.data, sizeof(RdrSpriteVertex), sl->dynArrayVerts.count, sl->dynArrayVerts.size, 4);
			}
			else
			{
				newState = gfxSpriteListMemPoolAlloc_dynArrayAdd_no_memset(sl, sl->dynArrayStates.data, sizeof(RdrSpriteState), sl->dynArrayStates.count, sl->dynArrayStates.size, 1);
				newVerts = gfxSpriteListMemPoolAlloc_dynArrayAdd_no_memset(sl, sl->dynArrayVerts.data, sizeof(RdrSpriteVertex), sl->dynArrayVerts.count, sl->dynArrayVerts.size, 4);
			}
			memcpy(newState, srcList->dynArrayStates.data+srcEntryIdx, sizeof(RdrSpriteState));
			memcpy(newVerts, srcList->dynArrayVerts.data+srcEntryIdx*4, sizeof(RdrSpriteVertex)*4);
		}
		
		newEntry->index = newEntryIdx; //we need to store this since we cant get it by subtracting pointers

#ifdef ASSERT_ON_BAD_REF
		if (is3D)
		{
			assert(sl->dynArrayStates3D.count*4 == sl->dynArrayVerts3D.count);
			assert(newEntry->index == sl->dynArrayStates3D.count-1);
		}
		else
		{
			assert(sl->dynArrayStates.count*4 == sl->dynArrayVerts.count);
			assert(newEntry->index == sl->dynArrayStates.count-1);

		}
#endif //ASSERT_ON_BAD_REF
	}
	
#endif //USE_IDX_BUFFER

	gfxInsertSpriteListEntry(sl, newEntry, is3D);

	PERFINFO_AUTO_STOP_FUNC();
	return newEntry;
}

void gfxSpriteListHookupTextures(GfxSpriteList* sl, GfxSpriteListEntry* entry, AtlasTex *atex1, BasicTexture *btex1, AtlasTex *atex2, BasicTexture *btex2, bool is3D)
{
	PERFINFO_AUTO_START_FUNC();

	if (sl->storeTexPointers)
	{
		entry->atex[0] = atex1;
		entry->atex[1] = atex2;

		entry->btex[0] = btex1;
		entry->btex[1] = btex2;
	}

#ifdef USE_IDX_BUFFER
	{
		if (is3D)
			gfxSpriteSetupTexHandles(sl->dynArrayStates3D.data + entry->index, atex1, btex1, atex2, btex2);
		else
			gfxSpriteSetupTexHandles(sl->dynArrayStates.data + entry->index, atex1, btex1, atex2, btex2);
	}
#else
	gfxSpriteSetupTexHandles(&entry->spriteState, atex1, btex1, atex2, btex2);
#endif

	PERFINFO_AUTO_STOP_FUNC();
}

__forceinline static updateSpriteTexHandles(GfxSpriteList* sl, GfxSpriteListEntry* entry, bool is3D)
{
#ifdef USE_IDX_BUFFER
	if (is3D)
		gfxSpriteSetupTexHandles(sl->dynArrayStates3D.data + entry->index, entry->atex[0], entry->btex[0], entry->atex[1], entry->btex[1]);
	else
		gfxSpriteSetupTexHandles(sl->dynArrayStates.data + entry->index, entry->atex[0], entry->btex[0], entry->atex[1], entry->btex[1]);
#else
	gfxSpriteSetupTexHandles(&entry->spriteState, entry->atex[0], entry->btex[0], entry->atex[1], entry->btex[1]);
#endif
}

void gfxMergeSpriteLists(GfxSpriteList* dst, GfxSpriteList* src, bool updateTexHandles)
{
	#define ENTRY_TO_REF(e) _ENTRY_TO_REF(src, e)
	#define REF_TO_ENTRY(r) _REF_TO_ENTRY(src, r)
	EntryRef curNodeRef = src->sortedHeadRef;
	
	PERFINFO_AUTO_START_FUNC();

	while(curNodeRef)
	{
		GfxSpriteListEntry* curNode = REF_TO_ENTRY(curNodeRef);
		GfxSpriteListEntry* newEntry;
		EntryRef nextNodeRef;
		
		newEntry = gfxInsertSpriteListEntryFromOtherList(dst, src, curNode, false);
		if (updateTexHandles && src->storeTexPointers)
			updateSpriteTexHandles(dst, newEntry, false);
		
		nextNodeRef = curNode->nextRef;
		//now traverse the same z list
		curNodeRef = curNode->sameZListRef;
		while(curNodeRef)
		{
			curNode = REF_TO_ENTRY(curNodeRef);
			
			newEntry = gfxInsertSpriteListEntryFromOtherList(dst, src, curNode, false);
			if (updateTexHandles && src->storeTexPointers)
				updateSpriteTexHandles(dst, newEntry, false);

			curNodeRef = curNode->sameZListRef;
		}

		curNodeRef = nextNodeRef;
	}

	curNodeRef = src->headRef3D;
	while(curNodeRef)
	{
		GfxSpriteListEntry* curNode = REF_TO_ENTRY(curNodeRef);
		GfxSpriteListEntry* newEntry;

		newEntry = gfxInsertSpriteListEntryFromOtherList(dst, src, curNode, true);
		if (updateTexHandles && src->storeTexPointers)
			updateSpriteTexHandles(dst, newEntry, true);

		curNodeRef = curNode->nextRef;
	}
	
	PERFINFO_AUTO_STOP_FUNC();
	
	#undef ENTRY_TO_REF
	#undef REF_TO_ENTRY
}

#ifdef USE_IDX_BUFFER

//Make sure these are the same in rt_xsprite.c
#if !PLATFORM_CONSOLE
const static int idxsPerQuad = 6; //no D3DPT_QUADLIST
#else
const static int idxsPerQuad = 4;
#endif


__forceinline static addSpriteToIdxBuffer(GfxSpriteList* sl, GfxSpriteListEntry * entry, U16* idxBuffer, int* outCurIdx)
{
	int curIdx = *outCurIdx;
	int entryIdx;
	U16* curBufferPart = idxBuffer + curIdx;

	entryIdx = entry->index;

	switch(idxsPerQuad)
	{
	case 4:
		curBufferPart[0] = entryIdx*4;
		curBufferPart[1] = entryIdx*4+1;
		curBufferPart[2] = entryIdx*4+2;
		curBufferPart[3] = entryIdx*4+3;
		break;
	case 6:
		curBufferPart[0] = entryIdx*4;
		curBufferPart[1] = entryIdx*4+1;
		curBufferPart[2] = entryIdx*4+2;
		curBufferPart[3] = entryIdx*4;
		curBufferPart[4] = entryIdx*4+2;
		curBufferPart[5] = entryIdx*4+3;
		break;
	}
	*outCurIdx = curIdx + idxsPerQuad;
}

__forceinline static addSpriteToIdxBuffer32(GfxSpriteList* sl, GfxSpriteListEntry * entry, U32* idxBuffer, int* outCurIdx)
{
	int curIdx = *outCurIdx;
	int entryIdx;
	U32* curBufferPart = idxBuffer + curIdx;

	entryIdx = entry->index;

	switch(idxsPerQuad)
	{
	case 4:
		curBufferPart[0] = entryIdx*4;
		curBufferPart[1] = entryIdx*4+1;
		curBufferPart[2] = entryIdx*4+2;
		curBufferPart[3] = entryIdx*4+3;
		break;
	case 6:
		curBufferPart[0] = entryIdx*4;
		curBufferPart[1] = entryIdx*4+1;
		curBufferPart[2] = entryIdx*4+2;
		curBufferPart[3] = entryIdx*4;
		curBufferPart[4] = entryIdx*4+2;
		curBufferPart[5] = entryIdx*4+3;
		break;
	}
	*outCurIdx = curIdx + idxsPerQuad;
}


#endif


void gfxRenderSpriteList(GfxSpriteList* sl, RdrDrawList* drawList, bool alsoClear)
{
#define ENTRY_TO_REF(e) _ENTRY_TO_REF(sl, e)
#define REF_TO_ENTRY(r) _REF_TO_ENTRY(sl, r)
#ifdef USE_IDX_BUFFER
	RdrSpritesPkg* pkg;
	int screenw, screenh;
	#ifdef ASSERT_ON_BAD_REF
	int spritesWritten = 0;
	#endif
	EntryRef curNodeRef;
	int curIdxInBuffer = 0;
	int idxCount = idxsPerQuad*sl->count;
	bool need32bitIdxBuffer = gfx_state.debug.force_32bit_sprite_idx_buffer || (4*sl->count) > 0xFFFF;
	int ref_count;
	U16* idxBuffer = 0;
	U32* idxBuffer32 = 0;
	
	PERFINFO_AUTO_START_FUNC();

	assertmsg(!sl->isSpriteCache, "You are trying to render a sprite cache directly, merge it into the main list instead.");

	if (sl->count == 0)
	{
		PERFINFO_AUTO_STOP_FUNC();	
		return;
	}

	//make sure the cleanup leaves these alone (they are only referenced by the pool currently and would get cleaned up)
	memrefIncrement(sl->dynArrayStates.data);
	memrefIncrement(sl->dynArrayVerts.data);
	gfxSpriteListMemPoolCleanup(sl);

	if (need32bitIdxBuffer)
		idxBuffer32 = gfxSpriteListMemPoolAlloc(sl, sizeof(U32)*idxCount, NULL);
	else
		idxBuffer = gfxSpriteListMemPoolAlloc(sl, sizeof(U16)*idxCount, NULL);

	gfxGetActiveSurfaceSizeInline(&screenw, &screenh);
	pkg = rdrStartDrawSpritesImmediateUP(gfx_state.currentDevice->rdr_device, drawList, sl->count, screenw, screenh,
		sl->dynArrayStates.data, sl->dynArrayVerts.data, idxBuffer, idxBuffer32, true, true, true, true);

	//after releasing these temp refs there should be two left: the pool and the render thread (it can't release until rdrEndDrawSpritesImmediate)
	ref_count = memrefDecrement(sl->dynArrayStates.data);
	assert(ref_count == 2);
	ref_count = memrefDecrement(sl->dynArrayVerts.data);
	assert(ref_count == 2);
	
	curNodeRef = sl->sortedHeadRef;
	while(curNodeRef)
	{
		GfxSpriteListEntry* curNode = REF_TO_ENTRY(curNodeRef);
		EntryRef nextNodeRef;

		if (need32bitIdxBuffer)
			addSpriteToIdxBuffer32(sl, curNode, idxBuffer32, &curIdxInBuffer);
		else
			addSpriteToIdxBuffer(sl, curNode, idxBuffer, &curIdxInBuffer);
		
		nextNodeRef = curNode->nextRef; 
		#ifdef ASSERT_ON_BAD_REF
		spritesWritten++;
		#endif
		//now traverse the same z list
		curNodeRef = curNode->sameZListRef;
		while(curNodeRef)
		{
			curNode = REF_TO_ENTRY(curNodeRef);
			
			if (need32bitIdxBuffer)
				addSpriteToIdxBuffer32(sl, curNode, idxBuffer32, &curIdxInBuffer);
			else
				addSpriteToIdxBuffer(sl, curNode, idxBuffer, &curIdxInBuffer);

			curNodeRef = curNode->sameZListRef; 
			#ifdef ASSERT_ON_BAD_REF
			spritesWritten++;
			#endif
		}

		curNodeRef = nextNodeRef; 
	}

	
	pkg->sprite_count = pkg->state_array_size;
#ifdef ASSERT_ON_BAD_REF
	assert(spritesWritten == pkg->state_array_size);
#endif
	
	ADD_MISC_COUNT(pkg->state_array_size, "sprites written");
	rdrEndDrawSpritesImmediate(gfx_state.currentDevice->rdr_device);

	gfxSpriteListMemPoolCleanup(sl); //we've handed off all the buffers so cleanup again incase they are already done and we can use them again

	if (gfxSpriteListReallocAll && alsoClear)
	{
		gfxSpriteListMemPoolFreeAll(sl);
		gfxSpriteListReallocAll = 0;
	}

	if (alsoClear)
	{
		//clear out the pointers since the render thread has the data now
		//if we are also clearing it we dont need to save a copy which is why the
		//combo render and then clear operation exists
		U32 actualSize;

		sl->dynArrayStates.data = gfxSpriteListMemPoolAlloc(sl, sizeof(RdrSpriteState)*sl->dynArrayStates.count, &actualSize);
		sl->dynArrayStates.size = actualSize/sizeof(RdrSpriteState);
		sl->dynArrayStates.count = 0;

		sl->dynArrayVerts.data = gfxSpriteListMemPoolAlloc(sl, sizeof(RdrSpriteVertex)*sl->dynArrayVerts.count, &actualSize);
		sl->dynArrayVerts.size = actualSize/sizeof(RdrSpriteVertex);
		sl->dynArrayVerts.count = 0;

		gfxClearSpriteListSorted(sl);
	}
	else
	{
		//because the old buffers are inaccessable now we can't support this mode
		assertmsg(false, "The mode doesn't work with the sprite memory pool mode.");
	}


	gfx_state.debug.too_many_sprites_for_16bit_idx = need32bitIdxBuffer;

	PERFINFO_AUTO_STOP_FUNC();	
#else //USE_IDX_BUFFER
	RdrSpritesPkg* pkg;
	int screenw, screenh;
	int spritesWritten = 0;
	EntryRef curNodeRef;
	RdrSpriteState* states;
	RdrSpriteVertex* verts;

	PERFINFO_AUTO_START_FUNC();

	assertmsg(!sl->isSpriteCache, "You are trying to render a sprite cache directly, merge it into the main list instead.");

	gfxSpriteListMemPoolCleanup(sl);

	if (gfxSpriteListReallocAll)
	{
		gfxSpriteListMemPoolFreeAll(sl);
		gfxSpriteListReallocAll = 0;
	}

	if (sl->count == 0)
	{
		PERFINFO_AUTO_STOP_FUNC();	
		return;
	}


	states = gfxSpriteListMemPoolAlloc(sl, sizeof(RdrSpriteState)*sl->count, NULL);
	verts = gfxSpriteListMemPoolAlloc(sl, sizeof(RdrSpriteVertex)*sl->count*4, NULL);

	gfxGetActiveSurfaceSizeInline(&screenw, &screenh);
	pkg = rdrStartDrawSpritesImmediateUP(gfx_state.currentDevice->rdr_device, drawList, sl->count, screenw, screenh,
		states, verts, NULL, NULL, true, true, false, true);

	curNodeRef = sl->sortedHeadRef;
	while(curNodeRef)
	{
		GfxSpriteListEntry* curNode = REF_TO_ENTRY(curNodeRef);
		EntryRef nextNodeRef;
		memcpy(&pkg->states[spritesWritten], &curNode->spriteState, sizeof(RdrSpriteState));
		memcpy(&pkg->vertices[spritesWritten*4], curNode->spriteVerts, sizeof(RdrSpriteVertex)*4);

		nextNodeRef = curNode->nextRef; 
		spritesWritten++;
		//now traverse the same z list
		curNodeRef = curNode->sameZListRef;
		while(curNodeRef)
		{
			curNode = REF_TO_ENTRY(curNodeRef);
			memcpy(&pkg->states[spritesWritten], &curNode->spriteState, sizeof(RdrSpriteState));
			memcpy(&pkg->vertices[spritesWritten*4], curNode->spriteVerts, sizeof(RdrSpriteVertex)*4);

			curNodeRef = curNode->sameZListRef; 
			spritesWritten++;
		}

		curNodeRef = nextNodeRef; 
	}

	pkg->sprite_count = pkg->state_array_size;
	assert(spritesWritten == pkg->state_array_size);

	ADD_MISC_COUNT(pkg->state_array_size, "sprites written");
	ADD_MISC_COUNT(pkg->state_array_size * (sizeof(RdrSpriteState) + sizeof(RdrSpriteVertex)*4), "bytes copied");


	rdrEndDrawSpritesImmediate(gfx_state.currentDevice->rdr_device);
	
	if (alsoClear)
		gfxClearSpriteListSorted(sl);

	gfx_state.debug.too_many_sprites_for_16bit_idx = false;

	PERFINFO_AUTO_STOP_FUNC();	
#endif //USE_IDX_BUFFER

#undef ENTRY_TO_REF
#undef REF_TO_ENTRY
}

void gfxRenderSpriteList3D(GfxSpriteList* sl, RdrDrawList* draw_list, bool alsoClear)
{
	int screenX, screenY;
	Vec2 mid;
	Vec2 invmid;
	int i;
	int sprite_count = sl->count3D;
	const float *actualCamPos = gfx_state.currentCameraView->frustum.cammat[3];
	EntryRef curNodeRef;
	
	if (!sprite_count)
		return;

	#ifdef USE_IDX_BUFFER
	assert(sprite_count == sl->dynArrayStates3D.count);
	#endif

	gfxGetActiveSurfaceSizeInline(&screenX, &screenY);
	setVec2(mid, screenX/2.f, screenY/2.f);
	setVec2(invmid, 1.f/mid[0], 1.f/mid[1]);
	
	curNodeRef = sl->headRef3D;
	for (i=0; i < sprite_count; i++)
	{
		GfxSpriteListEntry* curNode = _REF_TO_ENTRY(sl, curNodeRef);
		#ifdef USE_IDX_BUFFER
		RdrSpriteState* state = sl->dynArrayStates3D.data + curNode->index;
		RdrSpriteVertex* vert4 = sl->dynArrayVerts3D.data + curNode->index*4;
		#else
		RdrSpriteState* state = &curNode->spriteState;
		RdrSpriteVertex* vert4 = curNode->spriteVerts;
		#endif
		RdrDrawablePrimitive* quad = rdrDrawListAllocPrimitive( draw_list, RTYPE_PRIMITIVE, true );

		assert(curNodeRef);

		if (quad)
		{
			Vec3 deltaX;
			Vec3 deltaY;
			Vec3 deltaZ;
			Vec3 deltaXNeg;
			Vec3 deltaYNeg;
			Vec3 deltaXPos;
			Vec3 deltaYPos;
			Vec2 deltaScaleNeg;
			Vec2 deltaScalePos;
			Mat4 cameraMatrix;
			copyMat4(gfx_state.currentCameraView->frustum.cammat, cameraMatrix);

			subVec2(mid, vert4[0].point, deltaScaleNeg);
			mulVecVec2(deltaScaleNeg, invmid, deltaScaleNeg);
			subVec2(vert4[2].point, mid, deltaScalePos);
			mulVecVec2(deltaScalePos, invmid, deltaScalePos);

			if( !gfx_state.currentCameraController->ortho_mode_ex ) {
				float headshotZ = -curNode->zValue;
				Vec3 tempDeltaX = { headshotZ * gfx_state.currentCameraView->frustum.htan, 0, 0 };
				Vec3 tempDeltaY = { 0, headshotZ * gfx_state.currentCameraView->frustum.vtan, 0 };
				Vec3 tempDeltaZ = { 0, 0, headshotZ };
				mulVecMat3( tempDeltaX, cameraMatrix, deltaX );
				mulVecMat3( tempDeltaY, cameraMatrix, deltaY );
				mulVecMat3( tempDeltaZ, cameraMatrix, deltaZ );
			} else {
				assertmsg(0, "todo");

				//float epsilon = 0.005 * (gfx_state.currentCameraController->ortho_near
				//	- gfx_state.currentCameraController->ortho_far);
				//Vec3 tempDeltaX = { gfx_state.currentCameraController->ortho_width / 2, 0, 0 };
				//Vec3 tempDeltaY = { 0, gfx_state.currentCameraController->ortho_height / 2, 0 };
				//Vec3 tempDeltaZ = { 0, 0, gfx_state.currentCameraController->ortho_far + epsilon };
				//mulVecMat3( tempDeltaX, cameraMatrix, deltaX );
				//mulVecMat3( tempDeltaY, cameraMatrix, deltaY );
				//mulVecMat3( tempDeltaZ, cameraMatrix, deltaZ );

			}

			scaleVec3(deltaX, -deltaScaleNeg[0], deltaXNeg);
			scaleVec3(deltaY, deltaScaleNeg[1], deltaYNeg);
			scaleVec3(deltaX, deltaScalePos[0], deltaXPos);
			scaleVec3(deltaY, -deltaScalePos[1], deltaYPos);

			quad->in_3d = true;
			quad->type = RP_QUAD;
			quad->filled = (gfx_state.wireframe!=2);
			quad->tonemapped = false;
			quad->has_tex_coords = true;
			quad->tex_handle = state->tex_handle1;
			quad->no_ztest = state->ignore_depth_test;
			memcpy(quad->sprite_state,state,sizeof(RdrSpriteState));

			{
				int j;
				for (j=0; j<4; j++)
				{
					
					#ifdef _XBOX
					Color tmpColor;
					tmpColor.r = vert4[j].color.a;
					tmpColor.g = vert4[j].color.b;
					tmpColor.b = vert4[j].color.g;
					tmpColor.a = vert4[j].color.r;
					colorToVec4(quad->vertices[j].color, tmpColor);
					#else
					colorToVec4(quad->vertices[j].color, vert4[j].color);
					#endif
					copyVec2(vert4[j].texcoords, quad->vertices[j].texcoord);
				}

				{
					Vec3 temp;
					copyVec3( actualCamPos, temp );
					addVec3( temp, deltaXNeg, temp );
					addVec3( temp, deltaYNeg, temp );
					addVec3( temp, deltaZ, temp );
					copyVec3( temp, quad->vertices[ 0 ].pos );
				}
				{
					Vec3 temp;
					copyVec3( actualCamPos, temp );
					addVec3( temp, deltaXPos, temp );
					addVec3( temp, deltaYNeg, temp );
					addVec3( temp, deltaZ, temp );
					copyVec3( temp, quad->vertices[ 1 ].pos );
				}
				{
					Vec3 temp;
					copyVec3( actualCamPos, temp );
					addVec3( temp, deltaXPos, temp );
					addVec3( temp, deltaYPos, temp );
					addVec3( temp, deltaZ, temp );
					copyVec3( temp, quad->vertices[ 2 ].pos );
				}
				{
					Vec3 temp;
					copyVec3( actualCamPos, temp );
					addVec3( temp, deltaXNeg, temp );
					addVec3( temp, deltaYPos, temp );
					addVec3( temp, deltaZ, temp );
					copyVec3( temp, quad->vertices[ 3 ].pos );
				}
			}

			rdrDrawListAddPrimitive( draw_list, quad, RST_AUTO, ROC_PRIMITIVE );
		}
		curNodeRef = curNode->nextRef;
	}

	if (alsoClear)
		gfxClearSpriteList3D(sl);
}


GfxSpriteList* gfxGetDefaultSpriteList()
{
	return gfx_state.currentDevice->sprite_list;
}

void gfxMaterialsAssertTexNotInSpriteList(GfxSpriteList* sl, BasicTexture* tex)
{
	int i;
	PERFINFO_AUTO_START_FUNC();	

	if (!sl || !tex || !sl->storeTexPointers)
	{
		PERFINFO_AUTO_STOP_FUNC();	
		return;
	}

	for (i = 0; i < sl->dynArrayElements.count; ++i)
	{
		GfxSpriteListEntry* curNode = sl->dynArrayElements.data + i;
		assert(curNode->btex[0] != tex);
		assert(curNode->btex[1] != tex);
	}

	PERFINFO_AUTO_STOP_FUNC();	
}
