/***************************************************************************



***************************************************************************/

#ifndef _WLCURVE_H_
#define _WLCURVE_H_
GCC_SYSTEM

#include "referencesystem.h"

typedef struct BaseCurveType BaesCurveType;
typedef struct GroupDef GroupDef;
typedef struct TextureSwap TextureSwap;
typedef struct MaterialSwap MaterialSwap;
typedef struct GroupSplineParams GroupSplineParams;
typedef struct WorldChildCurve WorldChildCurve;
typedef struct WorldCurveGap WorldCurveGap;

extern ParseTable parse_Spline[];
#define TYPE_parse_Spline Spline

#define SPLINE_MAX_CONTROL_POINTS 1800

// Normal Curves

AUTO_STRUCT;
typedef struct Spline {
	F32* spline_points;
	F32* spline_up;
	F32* spline_deltas;
	F32* spline_widths;
	S32* spline_geom;
} Spline;

//////////////////////////////////////////////////////////////////////////
// NOTE: Any integer variable named "index" is an index into an F32
// array, therefore it begins at ZERO and increases in increments of 
// THREE. (i.e. the third control point has index 6).
//
// Any float variable named "offset" begins at ONE and increases in
// increments of ONE, with fractional part being an interpolant between
// two control points. (i.e. halfway between control points two and three
// has offset 2.5.
//
// offset = (index/3) + 1 + interpolant
//
// Any deviation from this should be fixed to make things as clear as
// possible.

//////////////////////////////////////////////////////////////////////////
// Spline functions
void splineDestroy(Spline *spline);

void splineAppendAutoCP(Spline *spline, const Vec3 new_point, const Vec3 up, bool improper, S32 geometry, F32 width);
void splineAppendCP(Spline *spline, const Vec3 new_point, const Vec3 up, const Vec3 direction, S32 geometry, F32 width);
void splineInsertCP(Spline *spline, int index, const Vec3 new_point, const Vec3 up, const Vec3 dir, F32 width); // New index for this control point
void splineDeleteCP(Spline *spline, int index);
void splineRedrawPoints(Spline *spline);
void splineRotateNEW(Spline *spline, const Mat3 rotate_matrix);
void splineTransformMatrix(Spline *spline, const Mat4 matrix);

F32 splineGetWidth(Spline *spline, int index, F32 t);

void splineGetControlPoints(Spline *spline, int index, Vec3 control_points[4]);
void splineGetMatrices(const Mat4 parent_mat, const Vec3 curve_center, const F32 curve_scale,
					   Spline *spline, int index, Mat4 *matrices, bool linearize);
void splineTransform(Spline *spline, int index, F32 weight, const Vec3 in, Vec3 out, Vec3 up, Vec3 tangent, bool linearize);
int splineCollide(Vec3 start, Vec3 end, Mat4 parent_mat, Spline *spline); // Returns index of colliding control point, or -1
bool splineCollideFull(Vec3 start, Vec3 end, Mat4 parent_mat, Spline *spline, F32 *collide_offset, Vec3 collide_pos, F32 tolerance); // Returns point on spline
bool splineCheckCollision(Spline *spline1, Spline *spline2, F32 start_offset1, F32 start_offset2,
						  F32 *offset1, F32 *offset2, 
						  Vec3 collide_pos, Vec3 collide_dir1, Vec3 collide_dir2, F32 tol); // Returns closest point
F32 splineGetNearestPoint(Spline *spline, Vec3 in_point, Vec3 out_point, S32 *out_index, F32 *out_t, F32 max_dist, bool flatten); // returns distance

void splineGetNextPointEx(Spline *in, bool linearize, const Vec3 offset, int begin_pt, int end_pt, F32 begin_t, F32 end_t, F32 length, int *new_index, F32 *new_t, F32 *exact_length, F32 *length_remainder, Vec3 exact_point, Vec3 exact_up, Vec3 exact_tangent);

F32 splineGetTotalLength(Spline *in);

// Rotates the entire spline so that the first point is at (0,0,0) facing down Z-axis, and returns the object matrix.
void splineResetRotation(Spline *spline, Mat4 out_rot);

typedef struct SplineCurrentPoint
{
	int index;
	F32 t;
	Vec3 position;
	Vec3 up;
	Vec3 tangent;
	F32 remainder;
} SplineCurrentPoint;

// splineGetNextPoint should be called as follows:
/*

    SplineCurrentPoint it = { 0 };
	while (splineGetNextPoint(spline, &it, distance) > 0.001f)
	{
		printf("At %f,%f,%f\n", it->position[0],it->position[1],it->position[2]);
	}

*/
F32 splineGetNextPoint(Spline *in, SplineCurrentPoint *iterator, F32 distance);

//////////////////////////////////////////////////////////////////////////
// Curve functions

void curveCalculateChild(Spline *in, Spline *out, WorldChildCurve *child, U32 seed, GroupDef *group_def, const Mat4 group_matrix);

GroupDef *curveGetGeometry(const Mat4 parent_mat, GroupDef *def, Spline *spline, WorldCurveGap **gaps, Mat4 curve_matrix,
						   F32 uv_scale, F32 uv_rot, F32 stretch, int index, Mat4 out_mat, 
						   GroupSplineParams **params, F32 *distance_offset, F32 curve_scale);

void splineGetBoundingBox(Vec3 control_points[4], Vec3 min, Vec3 max, F32 tol);

#endif //_WLCURVE_H_
