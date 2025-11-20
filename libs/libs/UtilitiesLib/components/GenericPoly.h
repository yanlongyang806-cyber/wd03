#ifndef _GENERICPOLY_H_
#define _GENERICPOLY_H_
GCC_SYSTEM

#include "stdtypes.h"


typedef struct GPoly
{
	int count, max;
	Vec3 *points;
} GPoly;

typedef struct GPolySet
{
	int count, max;
	GPoly *polys;
} GPolySet;

typedef struct GConvexHull
{
	int count, max;
	Vec4 *planes;
} GConvexHull;

typedef struct GMesh GMesh;

//////////////////////////////////////////////////////////////////////////

void gpolySetSize_dbg(GPoly *poly, int count MEM_DBG_PARMS);
#define gpolySetSize(poly, count) gpolySetSize_dbg(poly, count MEM_DBG_PARMS_INIT)
void gpolyAddPoint_dbg(GPoly *poly, const Vec3 p MEM_DBG_PARMS);
#define gpolyAddPoint(poly, p) gpolyAddPoint_dbg(poly, p MEM_DBG_PARMS_INIT)
void gpolyAddUniquePoint_dbg(GPoly *poly, const Vec3 p MEM_DBG_PARMS);
#define gpolyAddUniquePoint(poly, p) gpolyAddUniquePoint_dbg(poly, p MEM_DBG_PARMS_INIT)
void gpolyRemovePoint(GPoly *poly, int idx);
void gpolyFreeData(GPoly *poly);
void gpolyCopy_dbg(GPoly *dst, const GPoly *src MEM_DBG_PARMS);
#define gpolyCopy(dst, src) gpolyCopy_dbg(dst, src MEM_DBG_PARMS_INIT)

int gpolyClipPlane_dbg(GPoly *poly, const Vec4 clipplane MEM_DBG_PARMS);
#define gpolyClipPlane(poly, clipplane) gpolyClipPlane_dbg(poly, clipplane MEM_DBG_PARMS_INIT)

void gpolyBounds(const GPoly *poly, Vec3 min, Vec3 max);
void gpolyTransform(GPoly *poly, const Mat4 mat);
void gpolyTransformToBounds(const GPoly *poly, const Mat4 mat, Vec3 min, Vec3 max);
void gpolyTransformToBounds44(const GPoly *poly, const Mat44 mat, Vec3 min, Vec3 max);

void gpolyRemoveDuplicates(GPoly *poly);

//////////////////////////////////////////////////////////////////////////

void gpsetSetSize_dbg(GPolySet *set, int count MEM_DBG_PARMS);
#define gpsetSetSize(set, count) gpsetSetSize_dbg(set, count MEM_DBG_PARMS_INIT)
GPoly *gpsetAddPoly_dbg(GPolySet *set MEM_DBG_PARMS);
#define gpsetAddPoly(set) gpsetAddPoly_dbg(set MEM_DBG_PARMS_INIT)
GPoly *gpsetAddPolyCopy_dbg(GPolySet *set, const GPoly *src MEM_DBG_PARMS);
#define gpsetAddPolyCopy(set, src) gpsetAddPolyCopy_dbg(set, src MEM_DBG_PARMS_INIT)
void gpsetRemovePoly(GPolySet *set, int idx);
void gpsetFreeData(GPolySet *set);
void gpsetCopy_dbg(GPolySet *dst, const GPolySet *src MEM_DBG_PARMS);
#define gpsetCopy(set, src) gpsetCopy_dbg(set, src MEM_DBG_PARMS_INIT)

void gpsetMakeBox_dbg(GPolySet *set, const Vec3 boxmin, const Vec3 boxmax MEM_DBG_PARMS);
#define gpsetMakeBox(set, boxmin, boxmax) gpsetMakeBox_dbg(set, boxmin, boxmax MEM_DBG_PARMS_INIT)
void gpsetMakeFrustum_dbg(GPolySet *set, const Mat4 cammat, F32 fovx, F32 fovy, F32 znear, F32 zfar, F32 scale MEM_DBG_PARMS);
#define gpsetMakeFrustum(set, cammat, fovx, fovy, znear, zfar, scale) gpsetMakeFrustum_dbg(set, cammat, fovx, fovy, znear, zfar, scale MEM_DBG_PARMS_INIT)
void gpsetExtrudeConvexHull_dbg(GPolySet *dst, const GPolySet *src, const Vec3 extrude MEM_DBG_PARMS);
#define gpsetExtrudeConvexHull(dst, src, extrude) gpsetExtrudeConvexHull_dbg(dst, src, extrude MEM_DBG_PARMS_INIT)
void gpsetMakeFrustumFromPoints_dbg(GPolySet *set, const Vec3 points[8] MEM_DBG_PARMS);
#define gpsetMakeFrustumFromPoints(set,points) gpsetMakeFrustumFromPoints_dbg(set, points MEM_DBG_PARMS_INIT)

void gpsetInvertNormals(GPolySet *set);

void gpsetClipPlane_dbg(GPolySet *set, const Vec4 clipplane MEM_DBG_PARMS);
#define gpsetClipPlane(set, clipplane) gpsetClipPlane_dbg(set, clipplane MEM_DBG_PARMS_INIT)
void gpsetClipBox_dbg(GPolySet *set, const Vec3 clipmin, const Vec3 clipmax MEM_DBG_PARMS);
#define gpsetClipBox(set, clipmin, clipmax) gpsetClipBox_dbg(set, clipmin, clipmax MEM_DBG_PARMS_INIT)
void gpsetClipBoxDebug_dbg(GPolySet *set, const Vec3 clipmin, const Vec3 clipmax, GPolySet * debugOutput, int nPlaneIndex MEM_DBG_PARMS);
#define gpsetClipBoxDebug(set, clipmin, clipmax, debugOutput, nPlaneIndex) gpsetClipBoxDebug_dbg(set, clipmin, clipmax, debugOutput, nPlaneIndex MEM_DBG_PARMS_INIT)

void gpsetBounds(const GPolySet *set, Vec3 min, Vec3 max);
void gpsetTransform(GPolySet *set, const Mat4 mat);
void gpsetTransformToBounds(const GPolySet *set, const Mat4 mat, Vec3 min, Vec3 max);
void gpsetTransformToBounds44(const GPolySet *set, const Mat44 mat, Vec3 min, Vec3 max);

void gpsetCollapse_dbg(const GPolySet *set, GPoly *pointlist MEM_DBG_PARMS);
#define gpsetCollapse(set, pointlist) gpsetCollapse_dbg(set, pointlist MEM_DBG_PARMS_INIT)

void gpsetToConvexHull_dbg(const GPolySet *set, GConvexHull *hull, int eyespace MEM_DBG_PARMS);
#define gpsetToConvexHull(set, hull, eyespace) gpsetToConvexHull_dbg(set, hull, eyespace MEM_DBG_PARMS_INIT)
void gpsetToConvexHullTransformed_dbg(const GPolySet *set, GConvexHull *hull, int eyespace, Mat4 transform MEM_DBG_PARMS);
#define gpsetToConvexHullTransformed(set, hull, eyespace, transform) gpsetToConvexHullTransformed_dbg(set, hull, eyespace, transform MEM_DBG_PARMS_INIT)

//////////////////////////////////////////////////////////////////////////

void hullSetSize_dbg(GConvexHull *hull, int count MEM_DBG_PARMS);
#define hullSetSize(hull, count) hullSetSize_dbg(hull, count, MEM_DBG_PARMS_INIT)
void hullAddPlane_dbg(GConvexHull *hull, const Vec4 plane MEM_DBG_PARMS);
#define hullAddPlane(hull, plane) hullAddPlane_dbg(hull, plane MEM_DBG_PARMS_INIT)
void hullFreeData(SA_PARAM_OP_VALID GConvexHull *hull);
void hullDestroy(GConvexHull *hull);	// freedata() then free()
void hullCopy_dbg(GConvexHull *dst, const GConvexHull *src MEM_DBG_PARMS);
#define hullCopy(dst, src) hullCopy_dbg(dst, src MEM_DBG_PARMS_INIT)

int hullIsPointInside(const GConvexHull *hull, const Vec3 point);
int hullIsSphereInside(const GConvexHull *hull, const Vec3 center, F32 radius);
bool hullBoxCollision(const GConvexHull *hull, const GMesh *mesh, const Vec3 local_min, const Vec3 local_max, const Mat4 world_mat, const Mat4 inv_world_mat);
int hullCapsuleCollision(GConvexHull *hull, Vec3 cap_start, Vec3 cap_dir, F32 length, F32 radius, Mat4 world_mat);


#endif//_GENERICPOLY_H_

