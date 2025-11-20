#ifndef _GRIDCACHE_H
#define _GRIDCACHE_H

#include "GlobalEnums.h"

typedef struct CollInfo CollInfo;
typedef struct WorldColl WorldColl;
typedef struct WorldCollCollideResults WorldCollCollideResults;
typedef struct PSDKActor PSDKActor;


typedef struct CollCacheParams {
	Vec3	start;
	Vec3	end;
	F32		radius;
	int		flags;
} CollCacheParams;

int collCacheSetDisabled(int disabled);
int collCacheFind(const CollCacheParams *params,WorldCollCollideResults *coll);
void collCacheSet(const CollCacheParams *params,const WorldCollCollideResults *coll);
void collCacheSetSize(int bits);
void collCacheReset(void);
void collCacheInvalidate(PSDKActor* psdkActor, const Vec3 start_f, const Vec3 end_f);
void collCacheInit(const char *mapname, ZoneMapType eMapType);

int objCacheFind(WorldColl* wc,const Vec3 start,const Vec3 end,F32 radius,U32 flags,void** shapesOut,U32* countOut);
void objCacheSet(WorldColl* wc,const Vec3 start,const Vec3 end,U32 flags,void *shapes[],int count);
int objCacheFitBlock(Vec3 start,Vec3 end,F32 *radius);
void objCacheSetSize(int bits);

F32 heightCacheGetHeight(WorldColl* wc, Vec3 sourcePos);

#endif
