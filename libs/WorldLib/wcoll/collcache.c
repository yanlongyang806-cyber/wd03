#include <string.h>
#include "Capsule.h"
#include "earray.h"
#include "mathutil.h"
#include "collcache.h"
#include "StashTable.h"
#include "HashFunctions.h"
#include "wininclude.h"
#include "WorldColl.h"
#include "WorldGrid.h"
#include "GlobalTypes.h"
#include "ZoneMap.h"
#include "timing.h"
#include "WorldLib.h"
#include "PhysicsSDK.h"
#include "utils.h"

#define LOG_TO_MOVEMENT_LOG 0

#define HASH(x, size)	MurmurHash2_inline((void*)x, (size), 0xa74d94e3)
#define HASH_STRUCT(x)	HASH(x, sizeof(*(x)))

static int coll_cache_bits;
#define COLL_CACHE_SIZE ((U32)(1 << coll_cache_bits))
#define COLL_CACHE_MASK (COLL_CACHE_SIZE -1)

static int obj_cache_bits;
#define OBJ_CACHE_SIZE ((U32)(1 << obj_cache_bits))
#define OBJ_CACHE_MASK (OBJ_CACHE_SIZE -1)

static int height_cache_bits;
#define HEIGHT_CACHE_SIZE ((U32)(1 << height_cache_bits))
#define HEIGHT_CACHE_MASK (HEIGHT_CACHE_SIZE - 1)

static int coll_cache_disabled = 0;
static int obj_cache_disabled = 0;

static int obj_cache_validate_all_shapes = 0;

AUTO_CMD_INT(obj_cache_disabled, obj_cache_disabled);

CRITICAL_SECTION objCacheDebugCS;

AUTO_RUN_ANON(memBudgetAddMapping(__FILE__, BUDGET_Physics););

typedef struct
{
	WorldColl*	wc;
	Vec3		start;
	U32			flags;
	U32			valid_id;
	F32			end;
	U32			count;
	U32			atomic_lock;
	void*		shapes[32-9];
} ObjCache;

#ifndef _M_X64
	STATIC_ASSERT(sizeof(ObjCache) == 4 * 32);
#endif

typedef struct
{
	CollCacheParams	params;
	U32			valid_id;
	int			atomic_lock;
} ParamCacheEntry;

typedef struct HeightCacheParams {
	int posXZ[2];
}HeightCacheParams;

typedef struct HeightCacheEntry {
	WorldColl* wc;
	HeightCacheParams params;
	F32 height;
	F32 query_height;
	int atomic_lock;
	U32 valid_id;
}HeightCacheEntry;

HeightCacheEntry* height_cache = NULL;

WorldCollCollideResults	*coll_cache;
ObjCache					*obj_cache;
ParamCacheEntry				*param_cache;
static U32					*valid_ids;

int grid_cache_query,grid_cache_find,objcache_cant_fit,grid_cache_fail_id;

int collCacheSetDisabled(int disabled){
	int old = coll_cache_disabled;
	
	coll_cache_disabled = disabled;
	
	return old;
}

#define ID_REGION_SIZE 16.f

static U32 globalid;

__forceinline static U32 getValidId(const Vec3 start)
{
	int		ix,iz,t;
	U32		hash;

	ix = start[0] * (1.f/ID_REGION_SIZE);
	iz = start[2] * (1.f/ID_REGION_SIZE);
	t = ix * iz;
	hash = HASH_STRUCT(&t) & COLL_CACHE_MASK;
	return valid_ids[hash];
}

void objCacheVerifyOnActorDestroy(	PSDKActor* psdkActor,
									int startx,
									int startz,
									int endx,
									int endz);

void collCacheInvalidate(	PSDKActor* psdkActor,
							const Vec3 start_f,
							const Vec3 end_f)
{
#if !PSDK_DISABLED
	int			t,ix,iz,start_x,start_z,end_x,end_z,size_x,size_z;
	U32			hash;
	Vec3		adjusted_s;
	Vec3		adjusted_e;
	Vec3		start_f_local;
	Vec3		end_f_local;

	if (!psdkActor || !valid_ids)
		return;
		
	PERFINFO_AUTO_START_FUNC();

	if(	!start_f ||
		!end_f)
	{
		start_f = start_f_local;
		end_f = end_f_local;
		psdkActorGetBounds(psdkActor, start_f_local, end_f_local);
	}

	globalid++;

	copyVec3(start_f, adjusted_s);
	adjusted_s[0] -= 2.f;
	adjusted_s[2] -= 2.f;
	start_x = adjusted_s[0] * (1.f/ID_REGION_SIZE);
	start_z = adjusted_s[2] * (1.f/ID_REGION_SIZE);

	copyVec3(end_f, adjusted_e);
	adjusted_e[0] += 2.f;
	adjusted_e[2] += 2.f;
	end_x = adjusted_e[0] * (1.f/ID_REGION_SIZE);
	end_z = adjusted_e[2] * (1.f/ID_REGION_SIZE);

	size_x = end_x - start_x;
	size_z = end_z - start_z;

	for(iz=0;iz<=size_z;iz++)
	{
		for(ix=0;ix<=size_x;ix++)
		{
			t = (start_x+ix) * (start_z+iz);
			hash = HASH_STRUCT(&t) & COLL_CACHE_MASK;
			valid_ids[hash] = globalid;
		}
	}

	if(obj_cache_validate_all_shapes)
		objCacheVerifyOnActorDestroy(psdkActor, start_x, start_z, end_x, end_z);

	PERFINFO_AUTO_STOP();
#endif
}

int collCacheFind(const CollCacheParams *params,WorldCollCollideResults *collOut)
{
	U32					hash;
	ParamCacheEntry*	entry;
	S32					result;

	if (coll_cache_disabled || !coll_cache_bits)
		return 0;
	PERFINFO_AUTO_START_FUNC();
	result = 0;
	grid_cache_query++;
	hash = HASH_STRUCT(params);

	entry = &param_cache[hash & COLL_CACHE_MASK];
	if(	InterlockedIncrement(&entry->atomic_lock) == 1 &&
		!CompareStruct(&entry->params,params))
	{
		// for now just check if anything has updated at all, might be good enough
		// otherwise could just check all blocks along line or something
		if(globalid != entry->valid_id){
			grid_cache_fail_id++;
		}else{
			*collOut = coll_cache[hash & COLL_CACHE_MASK];
			grid_cache_find++;
			result = 1;
		}
	}
	
	InterlockedDecrement(&entry->atomic_lock);
	PERFINFO_AUTO_STOP();
	return result;
}

void collCacheSet(const CollCacheParams *params,const WorldCollCollideResults *coll)
{
	U32				hash;
	ParamCacheEntry	*entry;

	if (coll_cache_disabled || !coll_cache_bits)
		return;
	if (coll->errorFlags.noCell || coll->errorFlags.noScene)
		return;
	hash = HASH_STRUCT(params);
	entry = &param_cache[hash & COLL_CACHE_MASK];
	if(InterlockedIncrement(&entry->atomic_lock) == 1){
		entry->params = *params;
		entry->valid_id = globalid;
		coll_cache[hash & COLL_CACHE_MASK] = *coll;
	}
	InterlockedDecrement(&entry->atomic_lock);
}

void collCacheSetSize(int bits)
{
	SAFE_FREE(coll_cache);
	SAFE_FREE(param_cache);
	coll_cache_bits = bits;
	if (!bits)
		return;
	coll_cache = callocStructs(WorldCollCollideResults, COLL_CACHE_SIZE);
	param_cache = callocStructs(ParamCacheEntry, COLL_CACHE_SIZE);
	valid_ids = callocStructs(U32, COLL_CACHE_SIZE);
}

void collCacheReset(void)
{
	ZeroStructs(coll_cache, COLL_CACHE_SIZE);
	ZeroStructs(param_cache, COLL_CACHE_SIZE);
	ZeroStructs(obj_cache, OBJ_CACHE_SIZE);
	ZeroStructs(height_cache, HEIGHT_CACHE_SIZE);
}

int objCacheFitBlock(Vec3 start,Vec3 end,F32 *radius)
{
	if (obj_cache_disabled ||
		coll_cache_disabled ||
		!obj_cache_bits ||
		*radius > 1.f ||
		start[0] != end[0] ||
		start[2] != end[2] ||
		fabs(start[1] - end[1]) > 6.f)
	{
		return 0;
	}

	end[0] = start[0] = round(start[0]);
	end[2] = start[2] = round(start[2]);
	if(start[1] < end[1]){
		SWAPF32(start[1], end[1]);
	}
	end[1] -= 2.5;
	start[1] += .3;

	*radius = 1.5;	// NOTE this is 1.5 instead of 2 because the radius gets converted to an bounding box in physX
	return 1;
}

typedef struct
{
	WorldColl*	wc;
	F32			start_xz[2];
	U32			flags;
} ObjCacheHash;

__forceinline static ObjCache *findObj(WorldColl* wc,const Vec3 start,U32 flags)
{
	ObjCacheHash	obj;
	U32				hash;

	obj.wc = wc;
	obj.start_xz[0] = round(start[0]);
	obj.start_xz[1] = round(start[2]);
	obj.flags = flags;
	hash = HASH_STRUCT(&obj);
	return &obj_cache[hash & OBJ_CACHE_MASK];
}

typedef struct ObjCacheDebugInfo{
	Vec3 minBounds;
	Vec3 maxBounds;
	#if !PSDK_DISABLED
		PSDKShape* psdkShape;
		PSDKActor* psdkActor;
	#endif
	U32 curId;
}ObjCacheDebugInfo;

StashTable objCacheDebugTable;

void objCacheAddShapeTracking(const Vec3 start, const Vec3 end, U32 flags, void* shapes[], int count)
{
#if !PSDK_DISABLED
	int i;
	int ix, iz;

	ix = start[0] * (1.f/ID_REGION_SIZE);
	iz = start[2] * (1.f/ID_REGION_SIZE);

	for(i = count-1; i >= 0; i--)
	{
		Vec3 minBounds;
		Vec3 maxBounds;
		PSDKActor* psdkActor;
		Vec3 adjusted_s;
		Vec3 adjusted_e;
		int start_x, start_z, end_x, end_z, size_x, size_z;
		ObjCacheDebugInfo* debugInfo;
		StashElement elem;

		psdkShapeGetActor(shapes[i], &psdkActor);
		psdkActorGetBounds(psdkActor, minBounds, maxBounds);

		EnterCriticalSection(&objCacheDebugCS);
		if(stashAddressAddPointerAndGetElement(objCacheDebugTable, shapes[i], &debugInfo, false, &elem))
		{
			debugInfo = callocStruct(ObjCacheDebugInfo);
			stashElementSetPointer(elem, debugInfo);
		}
		else
			debugInfo = stashElementGetPointer(elem);
		LeaveCriticalSection(&objCacheDebugCS);

		copyVec3(minBounds, debugInfo->minBounds);
		copyVec3(maxBounds, debugInfo->maxBounds);
		debugInfo->psdkActor = psdkActor;
		debugInfo->psdkShape = shapes[i];

		debugInfo->curId = globalid;

		copyVec3(minBounds, adjusted_s);
		adjusted_s[0] -= 2.f;
		adjusted_s[2] -= 2.f;
		start_x = adjusted_s[0] * (1.f/ID_REGION_SIZE);
		start_z = adjusted_s[2] * (1.f/ID_REGION_SIZE);

		copyVec3(maxBounds, adjusted_e);
		adjusted_e[0] += 2.f;
		adjusted_e[2] += 2.f;
		end_x = adjusted_e[0] * (1.f/ID_REGION_SIZE);
		end_z = adjusted_e[2] * (1.f/ID_REGION_SIZE);

		size_x = end_x - start_x;
		size_z = end_z - start_z;

		devassert(start_x <= ix && start_x + size_x >= ix);
		devassert(start_z <= iz && start_z + size_z >= iz);
	}
#endif
}

void objCacheSet(WorldColl* wc,const Vec3 start,const Vec3 end,U32 flags,void *shapes[],int count)
{
	ObjCache	*obj;

	if(	obj_cache_disabled ||
		coll_cache_disabled ||
		!obj_cache_bits ||
		count > ARRAY_SIZE(obj->shapes))
	{
		objcache_cant_fit++;
		return;
	}
	obj = findObj(wc,start,flags);
	if(InterlockedIncrement(&obj->atomic_lock) == 1){
		assert(start[1] >= end[1]);
		obj->wc = wc;
		copyVec3(start,obj->start);
		obj->end = end[1];
		obj->count = count;
		obj->flags = flags;
		obj->valid_id = globalid;
		CopyStructs(obj->shapes,shapes,count);

		if(obj_cache_validate_all_shapes)
			objCacheAddShapeTracking(start, end, flags, shapes, count);
	}

	InterlockedDecrement(&obj->atomic_lock);
}

int obj_query_count,obj_query_count_xz,obj_query_count_end,obj_query_count_flags,obj_cache_count;
int start_bad,end_bad;
int endbad_hist[10];

int objCacheFind(WorldColl* wc,const Vec3 start,const Vec3 end,F32 radius,U32 flags,void** shapesOut,U32* countOut)
{
	ObjCache*	obj;
	S32			result;

	if (obj_cache_disabled ||
		coll_cache_disabled ||
		!obj_cache_bits ||
		round(start[0]) != round(end[0]) ||
		round(start[2]) != round(end[2]))
	{
		return 0;
	}
	result = 0;
	obj_query_count++;
	obj = findObj(wc, start,flags);
	if(	InterlockedIncrement(&obj->atomic_lock) == 1 &&
		round(start[0]) == round(obj->start[0]) &&
		round(start[2]) == round(obj->start[2]))
	{
		F32 s1 = start[1];
		F32 e1 = end[1];
		
		if(s1 < e1){
			SWAPF32(s1, e1);
		}
		
		obj_query_count_xz++;
		
		if(	wc == obj->wc &&
			s1 <= obj->start[1] &&
			e1 >= obj->end)
		{
			obj_query_count_end++;
			
			if(flags == obj->flags){
				obj_query_count_flags++;
				
				if(obj->valid_id >= getValidId(start)){
					obj_cache_count++;
					*countOut = obj->count;
					CopyStructs(shapesOut, obj->shapes, obj->count);
					result = 1;
					
					#if LOG_TO_MOVEMENT_LOG
					globMovementLog("Found %d shapes (%1.3f, %1.3f, %1.3f) - (%1.3f) flags 0x%x.\n"
									"Input (%1.3f, %1.3f, %1.3f) - (%1.3f, %1.3f, %1.3f), r%1.3f",
									obj->count,
									vecParamsXYZ(obj->start),
									obj->end,
									obj->flags,
									vecParamsXYZ(start),
									vecParamsXYZ(end),
									radius);
					#endif
				}
			}
		}
	}
	InterlockedDecrement(&obj->atomic_lock);
	return result;
}

void objCacheSetSize(int bits)
{
	SAFE_FREE(obj_cache);
	obj_cache_bits = bits;
	if (!bits)
		return;
	obj_cache = callocStructs(ObjCache, OBJ_CACHE_SIZE);
}

#if !PSDK_DISABLED
typedef struct ObjCacheDestroyedActor{
	PSDKActor*	psdkActor;
	PSDKShape** psdkShapes;
	int shape_count;
	U32 curId;
	S64 absTime;
	int startx, startz, endx, endz;
}ObjCacheDestroyedActor;

ObjCacheDestroyedActor** destroyedActors;
#endif

void objCacheVerifyOnActorDestroy(PSDKActor* psdkActor, int startx, int startz, int endx, int endz)
{
#if !PSDK_DISABLED
	int i, j;
	ObjCacheDestroyedActor* d;
	PSDKShape** shapes;

	if (obj_cache_disabled || coll_cache_disabled || !obj_cache_bits)
		return;
	
	d = callocStruct(ObjCacheDestroyedActor);
	eaPush(&destroyedActors, d);

	d->psdkActor = psdkActor;
	d->shape_count = psdkActorGetShapeCount(psdkActor);
	d->curId = globalid;
	d->absTime = ABS_TIME;
	d->startx = startx;
	d->startz = startz;
	d->endx = endx;
	d->endz = endz;

	psdkActorGetShapesArray(psdkActor, &shapes);
	d->psdkShapes = callocStructs(PSDKShape*, d->shape_count);
	CopyStructs(d->psdkShapes, shapes, d->shape_count);

	for(i = OBJ_CACHE_SIZE-1; i >= 0; i--)
	{
		ObjCache* obj = &obj_cache[i];

		if(getValidId(obj->start) != obj->valid_id)
			continue;

		for(j = obj->count-1; j >= 0; j--)
		{
			PSDKShape* shape = obj->shapes[j];
			PSDKActor* psdkActorOther;
			psdkShapeGetActor(shape, &psdkActorOther);
			devassert(psdkActor != psdkActorOther);
		}
	}
#endif
}

U32 height_cache_find, height_cache_query, height_cache_fail_id;

__forceinline static HeightCacheEntry* heightCacheGetEntry(HeightCacheParams* params, U32 hash, Vec3 sourcePos, int update)
{
	HeightCacheEntry* entry;

	entry = &height_cache[hash & HEIGHT_CACHE_MASK];

	if(InterlockedIncrement(&entry->atomic_lock) > 1)
	{
		InterlockedDecrement(&entry->atomic_lock);
		return NULL;
	}

	if(!update)
	{
		if(	CompareStruct(&entry->params,params) ||
			vecY(sourcePos) < entry->height ||
			vecY(sourcePos) > entry->query_height)
		{
			InterlockedDecrement(&entry->atomic_lock);
			return NULL;
		}
		if(entry->valid_id < getValidId(sourcePos))
		{
			height_cache_fail_id++;
			InterlockedDecrement(&entry->atomic_lock);
			return NULL;
		}
	}

	return entry;
}

static F32 getWorldCollHeight(WorldColl* wc, Vec3 sourcePos, int x, int z, F32 yOffset)
{
	WorldCollCollideResults resultsCap;
	Capsule testCap = {{0,0,0}, {0,0.5,0}, 5, 0.5};
	Vec3 rayStart;
	Vec3 rayEnd;

	rayStart[0] = x + 0.5;
	rayStart[1] = sourcePos[1] + yOffset;
	rayStart[2] = z + 0.5;
	copyVec3(rayStart, rayEnd);
	rayEnd[1] -= 20 + yOffset;

	wcCapsuleCollideEx(wc, testCap, rayStart, rayEnd, WC_FILTER_BIT_MOVEMENT, &resultsCap);

	if(resultsCap.hitSomething)
		return vecY(resultsCap.posWorldImpact)+0.01;
	else
		return -FLT_MAX;
}

F32 heightCacheGetHeight(WorldColl* wc, Vec3 sourcePos)
{
	HeightCacheParams params;
	HeightCacheEntry* entry;
	U32 hash;
	F32 height;
	F32 yOffset = 5;

	params.posXZ[0] = floor(sourcePos[0]);
	params.posXZ[1] = floor(sourcePos[2]);
	
	if(coll_cache_disabled || !height_cache_bits)
		return getWorldCollHeight(wc, sourcePos, params.posXZ[0], params.posXZ[1], 0);

	height_cache_query++;

	hash = HASH_STRUCT(&params);

	if(entry = heightCacheGetEntry(&params, hash, sourcePos, false))
	{
		height = entry->height;
		InterlockedDecrement(&entry->atomic_lock);
		height_cache_find++;
		return height;
	}

	height = getWorldCollHeight(wc, sourcePos, params.posXZ[0], params.posXZ[1], yOffset);

	if(height==FLT_MAX || height-vecY(sourcePos)>0.1)
	{
		yOffset = 0;
		height = getWorldCollHeight(wc, sourcePos, params.posXZ[0], params.posXZ[1], yOffset);
	}

	if(entry = heightCacheGetEntry(&params, hash, sourcePos, true))
	{
		// update cache
		entry->wc = wc;
		entry->params = params;
		entry->height = height;
		entry->valid_id = globalid;
		entry->query_height = sourcePos[1]+yOffset;
		InterlockedDecrement(&entry->atomic_lock);
	}

	return height;
}

void heightCacheSetSize(int bits)
{
	SAFE_FREE(height_cache);
	height_cache_bits = bits;
	if (!bits)
		return;
	height_cache = callocStructs(HeightCacheEntry, HEIGHT_CACHE_SIZE);
}


void collCacheInit(const char *mapname, ZoneMapType eMapType)
{
	static	int	init = false;

	if (init)
		return;

	if (GetAppGlobalType() == GLOBALTYPE_GAMESERVER)
	{
		if (stricmp(mapname,"emptymap")==0) 
		{
			// this map always gets loaded first to set some stuff up
			return;
		}

		if(eMapType == ZMTYPE_STATIC)
		{
			collCacheSetSize(16);
			heightCacheSetSize(16);
			objCacheSetSize(14);
		}
		else
		{
			collCacheSetSize(13);
			heightCacheSetSize(13);
			objCacheSetSize(11);
		}
	}

	init = true;
}

AUTO_COMMAND;
void collcache_stats(void)
{
	printf("\n");
	printf("collision cache is %s\n",coll_cache_disabled ? "disabled" : "enabled");
	printf("player motion cache is %s\n",obj_cache_disabled ? "disabled" : "enabled");
	printf("player motion cache size %d (%d bytes)\n",OBJ_CACHE_SIZE, sizeof(*obj_cache) * OBJ_CACHE_SIZE);
	printf("raycast cache size       %d (%d bytes)\n",COLL_CACHE_SIZE, sizeof(*coll_cache) * COLL_CACHE_SIZE);
	printf("height cache size        %d (%d bytes)\n",HEIGHT_CACHE_SIZE, sizeof(*height_cache) * HEIGHT_CACHE_SIZE);
	printf("player motion obj cache  %d / %d (%2.0f%%)\n",obj_cache_count,obj_query_count,100 * (float)obj_cache_count/(1+obj_query_count));
	printf("raycast line cache       %d / %d (%2.0f%%)\n",grid_cache_find,grid_cache_query,100 * (float)grid_cache_find/(1+grid_cache_query));
	printf("height line cache        %d / %d (%2.0f%%)\n",height_cache_find,height_cache_query,100 * (float)height_cache_find/(1+height_cache_query));
	printf("id based cache misses    %d / %d / %d\n",obj_query_count_flags-obj_cache_count,grid_cache_fail_id,height_cache_fail_id);
}

AUTO_COMMAND;
void collcache_toggle(void)
{
	coll_cache_disabled = !coll_cache_disabled;
	collcache_stats();
}

AUTO_COMMAND;
int objCacheVerifyAll(int on)
{
	if(!objCacheDebugTable)
	{
		objCacheDebugTable = stashTableCreateAddress(1024);
		InitializeCriticalSection(&objCacheDebugCS);
	}

	obj_cache_validate_all_shapes = on;

	return obj_cache_validate_all_shapes;
}

#if !PSDK_DISABLED
	PSDKShape* objDebugShapePtr;
#endif

struct {
	WorldColl* wc;
	Vec3 startVec;
	Vec3 endVec;
	int flags;
} objCacheDebug;

static ObjCache* objCacheDebugFind(void)
{
	return findObj(objCacheDebug.wc, objCacheDebug.startVec, objCacheDebug.flags);
}

static U32 objCacheDebugGetPosHash(void)
{
	int ix = objCacheDebug.startVec[0] * (1.f/ID_REGION_SIZE);
	int iz = objCacheDebug.startVec[2] * (1.f/ID_REGION_SIZE);
	int t = ix * iz;

	return HASH_STRUCT(&t) & COLL_CACHE_MASK;
}

static void objCacheFindShape(void)
{
#if !PSDK_DISABLED
	ObjCacheDebugInfo* debugInfo = NULL;
	int i, j;

	printf("Shape 0x%p\n", objDebugShapePtr);
	if(stashAddressFindPointer(objCacheDebugTable, objDebugShapePtr, &debugInfo))
	{
		printf("Last known bounds: " LOC_PRINTF_STR " - " LOC_PRINTF_STR "\n",
			vecParamsXYZ(debugInfo->minBounds), vecParamsXYZ(debugInfo->maxBounds));
		printf("Last known actor: 0x%p\n", debugInfo->psdkActor);
	}

	for(i = eaSize(&destroyedActors)-1; i >= 0; i--)
	{
		ObjCacheDestroyedActor* d = destroyedActors[i];
		int found = false;

		for(j = d->shape_count-1; j >= 0; j--)
		{
			found |= d->psdkShapes[j] == objDebugShapePtr;
		}

		if(debugInfo)
		{
			if(!found && debugInfo->psdkActor == d->psdkActor)
			{
				found = true;
				printf("Found PSDKActor match but no shape or NxActor match!\n");
			}
		}

		if(found)
		{
			printf("Destroyed at %" FORM_LL "d (global id %d) for PSDKActor 0x%p (destrActor #%d)\n",
				d->absTime, d->curId, d->psdkActor, i);
		}
	}
#endif
}