#ifndef _GRIDCOLL_H
#define _GRIDCOLL_H
GCC_SYSTEM

#include "stdtypes.h"
#include "ctri.h"

typedef struct WorldColl WorldColl;
typedef struct PSDKScene PSDKScene;
typedef struct PSDKActor PSDKActor;
typedef struct Capsule Capsule;

typedef enum
{
	COLL_DISTFROMSTART		= BIT(0), // Find point closest to start of line
	COLL_DISTFROMCENTER		= BIT(1), // Find point closest to center of line
	COLL_BOTHSIDES			= BIT(2), // Include collisions with backfaces of polygons
	COLL_CYLINDER			= BIT(3), // Line segments with radius imply rounded endcaps, this forces endcaps to be flat
	COLL_NORMALTRI			= BIT(4), // Include collisions with COLL_NORMALTRI polygons. This is on by default
	COLL_DISTFROMSTARTEXACT = BIT(5), // COLL_DISTFROMSTART is an approximation when the line has a radius, this is slower, but precise
	COLL_TIMEOFIMPACT		= BIT(6), // Will use a different variant of sphere sweep triangle to get the actual point of collision along with the actual time of impact
									  // this flag superceeds all other flags
	COLL_IGNOREONEWAY		= BIT(7),
} CollFlags;

typedef S32 (*CollInfoActorIgnoredCB)(	void* userPointer,
										const PSDKActor* psdkActor);

typedef struct CollInfo
{
	WorldColl*					wc;
	PSDKScene*					psdkScene;
	Vec3						sceneOffset;
	void*						userPointer;
	CollInfoActorIgnoredCB		actorIgnoredCB;
	U32							filterBits;
	Mat4						mat;
	F32							sqdist;
	CTri*						ctri;
	CTri						ctri_copy; //bfixme - shouldn't need this if we cache ctris
	CollFlags					flags;
	int							tri_idx;
	int							backside;
	F32							radius;
	Vec3						start,end;
	Vec3						dir;
	Vec3						inv_dir;
	Vec3						motion_dir;
	F32							line_len_squared;
	F32							line_len;
	int							valid_id;				// for caching
	CtriState					tri_state;

	int							coll_count,coll_max;

	F32							max_density;
	Vec3						aabb_lo;
	Vec3						aabb_hi;
	F32							toi;
	Vec3						vHitPos;
	
	U32							hasOneWayCollision : 1;
} CollInfo;


void collideBuildAccelerator(const Vec3 start, const Vec3 end,const Vec3 vPadding,CollInfo *coll);
void collideClearAccelerator();
void collideDisableAccelerator();

// This makes a lot of assumptions right now.  Don't call this function unless you are using the exact same filterBits, etc, as when you created the accelerator
int collideWithAccelerator(const Vec3 start,const Vec3 end,CollInfo *coll,F32 radius,int flags);

// Collide capsule against the world
int collide(const Vec3 start,const Vec3 end,CollInfo *coll,F32 radius,int flags);

// Collide only against a single shape
int collideShape(const Vec3 start, const Vec3 end, CollInfo *coll, F32 radius, CollFlags flags, const void *shape);

// Collide normally, but ignore this shape
int collideIgnoreActor(const Vec3 start, const Vec3 end, CollInfo *coll, F32 radius, CollFlags flags, const PSDKActor *actorIgnore);

// Helper function to convert from Capsule to start/end/radius
int collideCap(const Vec3 pos, const Capsule *cap, CollInfo *coll, CollFlags flags);

// Same but with shape only
int collideShapeCap(const Vec3 pos, const Capsule *cap, CollInfo *coll, CollFlags flags, const void *shape);

// Same as above but ignore
int collideIgnoreActorCap(const Vec3 pos, const Capsule *cap, CollInfo *coll, CollFlags flags, const PSDKActor *actorIgnore);

// Sweep function that tests and moves fRadius along dir
int collideSweepIgnoreActorCap(	const Vec3 start, 
								const Vec3 end, 
								const Capsule *cap, 
								CollInfo *coll, 
								CollFlags flags, 
								const PSDKActor *actorIgnore);

F32 boxLineNearestPoint(const Vec3 local_min, const Vec3 local_max, const Mat4 world_mat, const Mat4 inv_mat, 
						const Vec3 start, const Vec3 end, Vec3 isect);

F32 boxPointNearestPoint(	const Vec3 local_min, const Vec3 local_max, const Mat4 world_mat, const Mat4 inv_mat, 
							const Vec3 pt, Vec3 isect);

F32 triLineNearestPoint(const Vec3 start, const Vec3 end, Vec3 verts[3], Vec3 isect);

#endif
