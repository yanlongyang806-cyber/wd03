#ifndef _FRUSTUM_H_
#define _FRUSTUM_H_
#pragma once
GCC_SYSTEM

#include "stdtypes.h"
#include "mathutil.h"
#include "GenericPoly.h"

enum
{
	FRUSTUM_CLIP_TOP =		(1 << 0),
	FRUSTUM_CLIP_BOTTOM =	(1 << 1),
	FRUSTUM_CLIP_LEFT =		(1 << 2),
	FRUSTUM_CLIP_RIGHT =	(1 << 3),
	FRUSTUM_CLIP_NEAR =		(1 << 4),
	FRUSTUM_CLIP_FAR =		(1 << 5),
	FRUSTUM_CLIP_NONE =		(1 << 6),

	FRUSTUM_CLIP_SPHERE_PARTIAL = (1 << 7),

	// NOTE: this enum cannot go beyond 8 bits without changes to GfxWorld.c!  (-CD)
};

// Keeping two copies of the frustum data around lets us use for one
// drawing/culling while preparing the other for the next frame, in a
// separate thread.
typedef struct Frustum
{
	F32	znear,zfar;
	F32 hvam,vvam;

	F32 hcos,vcos;
	F32 fovy, fovx;

	F32 htan,vtan;

	// 3 ints
	GConvexHull hull; // in cameraspace

	F32 sphere_radius, sphere_radius_sqrd;

	U32 use_hull:1;
	U32 use_sphere:1;

	Mat4 cammat, viewmat, inv_viewmat;

	Vec4 world_min, world_max; // bounds of stuff actually drawn, update by calling frustumUpdateBounds
} Frustum;

#define FRUSTUM_INLINE_ENABLE 1

#if FRUSTUM_INLINE_ENABLE
#define FRUSTUM_INLINE __inline
#define FRUSTUM_FORCE_INLINE __forceinline
#else
#define FRUSTUM_INLINE
#define FRUSTUM_FORCE_INLINE
#endif

FRUSTUM_INLINE int frustumCheckSphere(const Frustum *w, const Vec3 pos_cameraspace, F32 rad);
FRUSTUM_INLINE int frustumCheckSphereWorld(const Frustum *w, const Vec3 pos_worldspace, F32 rad);
FRUSTUM_INLINE int frustumCheckPoint(const Frustum *w, const Vec3 pos_cameraspace);
FRUSTUM_INLINE int frustumCheckBoxNearClipped(const Frustum *w, const Vec4 bounds_cameraspace[8]);
FRUSTUM_INLINE int frustumCheckBoxWorld(const Frustum *w, int clip, const Vec3 min, const Vec3 max, const Mat4 local_to_world_mat, bool bReturnFullClip);
FRUSTUM_INLINE int frustumCheckBoundingBox(const Frustum *w, const Vec3 vmin, const Vec3 vmax, const Mat4 local_to_world_mat, bool bReturnFullClip);

int frustumCheckBoundingBoxNonInline(const Frustum *w, const Vec3 vmin, const Vec3 vmax, const Mat4 local_to_world_mat, bool bReturnFullClip);

// This version checks if the bounding box actually penetrates the near view plane
FRUSTUM_INLINE bool frustumCheckBoxNearClippedInView(const Frustum *w, const Vec3 vmin, const Vec3 vmax, const Mat4 tocameramat);

FRUSTUM_FORCE_INLINE void frustumResetBounds(Frustum *w);
FRUSTUM_FORCE_INLINE void frustumUpdateBounds(Frustum *w, const Vec3 world_min, const Vec3 world_max);

void frustumGetBounds(const Frustum *w, Vec3 min, Vec3 max);

// Gets an ABBB in the space specified by the Matrix of a slice of the frustum
void frustumGetSliceAABB(const Frustum *w, const Mat4 dest_space, F32 znear, F32 zfar, Vec3 vMin, Vec3 vMax);

void frustumSet(Frustum *w, F32 fovy, F32 aspect, F32 znear, F32 zfar);
void frustumSetOrtho(Frustum *w, F32 aspect, F32 ortho_zoom, F32 near_dist, F32 far_dist);
void frustumSetSphere(Frustum *w, F32 radius);

void frustumGetScreenPosition(const Frustum *w, int screen_width, int screen_height, const Vec3 pos_cameraspace, Vec2 screen_pos);
void frustumGetWorldRay(const Frustum *w, int screen_width, int screen_height, const Vec2 screen_pos, F32 len, Vec3 start, Vec3 end);

void makeViewMatrix(const Mat4 camera_matrix, Mat4 viewmat, Mat4 inv_viewmat);
void frustumSetCameraMatrix(Frustum *f, const Mat4 camera_matrix);

void frustumCopy(Frustum *dst, const Frustum *src);

#if FRUSTUM_INLINE_ENABLE
#include "FrustumInl.h"
#endif

#endif //_FRUSTUM_H_
