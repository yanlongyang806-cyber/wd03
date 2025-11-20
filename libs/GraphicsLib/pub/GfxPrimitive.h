#pragma once
GCC_SYSTEM

#include "mathutil.h"
#include "Color.h"
#include "RdrEnums.h"
#include "UtilitiesLibEnums.h"

typedef struct DynClothMesh DynClothMesh;
typedef struct DynTransform DynTransform;
typedef struct CBox CBox;


//////////////////////////////////////////////////////////////////////////
// 3D primitives

void gfxSetPrimZTest(int ztest);		// sets ztesting (and writing) on all following 3D primitives
void gfxSetPrimToneMap(int tonemap);	// sets tonemapping on all following 3D primitives
void gfxSetPrimIgnoreMax(bool ignore);	// tells the primitive queueing to ignore the max prim count limit; should be used only temporarily while drawing important things

void gfxDrawLine3DWidth(const Vec3 p1, const Vec3 p2, Color color1, Color color2, F32 width);
void gfxDrawLine3DTime(const Vec3 p1, const Vec3 p2, Color color1, Color color2, F32 time);
void gfxDrawLine3DWidthTime(const Vec3 p1, const Vec3 p2, Color color1, Color color2, F32 width, F32 time);

__forceinline static void gfxDrawLine3D(const Vec3 p1, const Vec3 p2, Color color) { gfxDrawLine3DWidth(p1,p2,color,color,1); }
__forceinline static void gfxDrawLine3D_2(const Vec3 p1, const Vec3 p2, Color color1, Color color2) { gfxDrawLine3DWidth(p1,p2,color1,color2,1); }

void gfxDrawBezier3D(const Vec3 controlPoints[4], Color color1, Color color2, F32 width);
void gfxDrawTriangle3D_3(const Vec3 p1, const Vec3 p2, const Vec3 p3, Color color1, Color color2, Color color3);
void gfxDrawTriangle3D_3Ex(const Vec3 p1, const Vec3 p2, const Vec3 p3, Color color1, Color color2, Color color3, F32 width, bool filled);
void gfxDrawTriangle3D_3vc(const Vec3 p1, const Vec4 c1, const Vec3 p2, const Vec4 c2, const Vec3 p3, const Vec4 c3, bool filled);
void gfxDrawQuad3D(const Vec3 a, const Vec3 b, const Vec3 c, const Vec3 d, Color color, F32 line_width); // 0 line_width draws filled
void gfxDrawBox3DEx(const Vec3 min, const Vec3 max, const Mat4 mat, Color color, F32 line_width, VolumeFaces faceBits); // 0 line_width draws filled
#define gfxDrawBox3D(min, max, mat, color, line_width) gfxDrawBox3DEx(min, max, mat, color, line_width, 0)
void gfxDrawVerticalHash(const Vec3 pt, F32 len, Color color, F32 line_width);	// Just draws a small tick mark at pt.  Basically debugging only.
void gfxDrawHemisphere(const Vec3 center, const Vec3 top, F32 rad, int nsegs, int capped, Color color, F32 line_width);		// Capped = 0 doesn't draw cap
void gfxDrawCylinder3D(const Vec3 pt1, const Vec3 pt2, F32 rad, int nsegs, int capped, Color color, F32 line_width);	// Capped = 0 doesn't draw endcaps
void gfxDrawCapsule3D(const Vec3 bottom, const Vec3 top, F32 rad, int nsegs, Color color, F32 line_width);
void gfxDrawSphere3D(const Vec3 mid, F32 rad, int nsegs, Color color, F32 line_width); // 0 line_width draws filled
void gfxDrawSphere3DTime(const Vec3 mid, F32 rad, int nsegs, Color color, F32 line_width, F32 time); // 0 line_width draws filled

void gfxDrawCone3DEx(const Mat4 mat, F32 radius, F32 halfAngle, F32 startingRadius, int nsegs, Color c);
#define gfxDrawCone3D(mat,radius,halfAngle,nsegs,c) gfxDrawCone3DEx((mat),(radius),(halfAngle),(nsegs),0.f,(c))

void gfxDrawCone3D_min(const Mat4 mat, F32 innerRadius, F32 outerRadius, F32 halfAngle, F32 startingRadius, int nsegs, Color c, Color cInner, Color cOuter, bool drawInner, bool drawOuter);
void gfxDrawPyramid3D(const Mat4 mat, F32 innerRadius, F32 outerRadius, F32 angle1, F32 angle2, Color fillColor, Color lineColor);
void gfxDrawAxes3D(const Mat4 mat, F32 fLength);
void gfxDrawAxesFromTransform(const DynTransform* pxTransform, F32 fLength);

// More expensive than DrawEllipse, but easier to use
void gfxDrawCircle3D(const Vec3 mid, const Vec3 norm, const Vec3 zerov, int nsegs, Color color, F32 radius);

__forceinline static void gfxDrawTriangle3D(const Vec3 p1, const Vec3 p2, const Vec3 p3, Color color) { gfxDrawTriangle3D_3(p1, p2, p3, color, color, color); }
__forceinline static void gfxDrawBasis(Mat4 world_matrix, float size)
{
	Vec3 end;
	scaleAddVec3(world_matrix[0], size, world_matrix[3], end);
	gfxDrawLine3D(world_matrix[3], end, ColorRed);
	scaleAddVec3(world_matrix[1], size, world_matrix[3], end);
	gfxDrawLine3D(world_matrix[3], end, ColorGreen);
	scaleAddVec3(world_matrix[2], size, world_matrix[3], end);
	gfxDrawLine3D(world_matrix[3], end, ColorBlue);
}



//////////////////////////////////////////////////////////////////////////
// 2D primitives

// Note: 2D line/quad drawing code is inverted Y like text and sprites (0,0 is
//   in the upper left) this is the opposite of how Cities worked.

// 2D primitives get drawn through the sprite system now

void gfxDrawLine(int x1, int y1, F32 z, int x2, int y2, Color color);
void gfxDrawLine2(int x1, int y1, F32 z, int x2, int y2, Color color1, Color color2);
void gfxDrawLineWidth(int x1, int y1, F32 z, int x2, int y2, Color color, F32 width);
void gfxDrawLineEx(F32 x1, F32 y1, F32 z, F32 x2, F32 y2, Color color1, Color color2, F32 width, bool antialiased);
bool gfxDrawLineExCollides(F32 x1, F32 y1, F32 x2, F32 y2, F32 width, int test_x, int test_y);
void gfxDrawBezier(const Vec2 controlPoints[4], F32 z, Color color1, Color color2, F32 width);
void gfxBezierGetPoint(const Vec2 controlPoints[4], F32 t, Vec2 point);
void gfxBezierGetPoint3D(const Vec3 controlPoints[4], F32 t, Vec3 point);
void gfxBezierGetTangent3D(const Vec3 controlPoints[4], F32 t, Vec3 point);
bool gfxDrawBezierCollides(Vec2 controlPoints[4], F32 width, int test_x, int test_y);

void gfxDrawBox(F32 x1, F32 y1, F32 x2, F32 y2, F32 z, Color color);
void gfxDrawCBox(CBox* pCbox, F32 z, Color color);
void gfxDrawEllipse(F32 x1, F32 y1, F32 x2, F32 y2, F32 z, U32 cnt, Color color);//Draws with lines, greater cnt = more resolution

void gfxDrawQuad(int x1, int y1, int x2, int y2, F32 z, Color color);
void gfxDrawQuad4(int x1, int y1, int x2, int y2, F32 z, Color color_ul, Color color_ur, Color color_ll, Color color_lr);
void gfxDrawClothMeshPrimitive(const Mat4 mat, const Vec4 color, const DynClothMesh* cloth_mesh);



//////////////////////////////////////////////////////////////////////////
// argb versions
__forceinline static void gfxDrawLine3DARGB(const Vec3 p1, const Vec3 p2, int argb)
{
	gfxDrawLine3D(p1, p2, ARGBToColor(argb));
}

__forceinline static void gfxDrawLine3D_2ARGB(const Vec3 p1, const Vec3 p2, int argb1, int argb2)
{
	gfxDrawLine3D_2(p1, p2, ARGBToColor(argb1), ARGBToColor(argb2));
}

__forceinline static void gfxDrawLine3DWidthARGB(const Vec3 p1, const Vec3 p2, int argb1, int argb2, F32 width)
{
	gfxDrawLine3DWidth(p1, p2, ARGBToColor(argb1), ARGBToColor(argb2), width);
}

__forceinline static void gfxDrawTriangle3D_3ARGB(const Vec3 p1, const Vec3 p2, const Vec3 p3, int argb1, int argb2, int argb3)
{
	gfxDrawTriangle3D_3(p1, p2, p3, ARGBToColor(argb1), ARGBToColor(argb2), ARGBToColor(argb3));
}

__forceinline static void gfxDrawTriangle3D_3ARGBEx(const Vec3 p1, const Vec3 p2, const Vec3 p3, int argb1, int argb2, int argb3, F32 width, bool filled)
{
	gfxDrawTriangle3D_3Ex(p1, p2, p3, ARGBToColor(argb1), ARGBToColor(argb2), ARGBToColor(argb3), width, filled);
}

__forceinline static void gfxDrawLineARGB(int x1, int y1, F32 z, int x2, int y2, int argb)
{
	gfxDrawLine(x1, y1, z, x2, y2, ARGBToColor(argb));
}

__forceinline static void gfxDrawLineARGB2(int x1, int y1, F32 z, int x2, int y2, int argb1, int argb2)
{
	gfxDrawLine2(x1, y1, z, x2, y2, ARGBToColor(argb1), ARGBToColor(argb2));
}

__forceinline static void gfxDrawQuadARGB(int x1, int y1, int x2, int y2, F32 z, int argb)
{
	gfxDrawQuad(x1, y1, x2, y2, z, ARGBToColor(argb));
}

__forceinline static void gfxDrawQuad4ARGB(int x1, int y1, int x2, int y2, int z, int argb_ul, int argb_ur, int argb_ll, int argb_lr)
{
	gfxDrawQuad4(x1, y1, x2, y2, z, ARGBToColor(argb_ul), ARGBToColor(argb_ur), ARGBToColor(argb_ll), ARGBToColor(argb_lr));
}

__forceinline static void gfxDrawBox3DARGB(const Vec3 min, const Vec3 max, const Mat4 mat, int argb, F32 line_width)
{
	gfxDrawBox3D(min, max, mat, ARGBToColor(argb), line_width);
}

__forceinline static void gfxDrawSphere3DARGB(const Vec3 mid, F32 rad, int nsegs, int argb, F32 line_width)
{
	gfxDrawSphere3D(mid, rad, nsegs, ARGBToColor(argb), line_width);
}

__forceinline static void gfxDrawCircle3DARGB(const Vec3 mid, const Vec3 norm, const Vec3 zerov, int nsegs, int argb, F32 radius)
{
	gfxDrawCircle3D(mid, norm, zerov, nsegs, ARGBToColor(argb), radius);
}


//////////////////////////////////////////////////////////////////////////
// direct draw calls

void gfxQueueBox(const Vec3 vMin, const Vec3 vMax, const Mat4 mWorldMatrix, int frustum_visible, RdrObjectCategory object_category, RdrMaterialFlags add_material_flags, const Vec4 tint_color, bool tonemap);
void gfxQueueSphere(const Vec3 vMid, F32 fRadius, const Mat4 mWorldMatrix, int frustum_visible, RdrObjectCategory object_category, RdrMaterialFlags add_material_flags, const Vec4 tint_color, bool tonemap);
void gfxQueueCylinder(const Vec3 vMid, F32 fHeightRadius, F32 fRadialRadius, const Mat4 mWorldMatrix, int frustum_visible, RdrObjectCategory object_category, RdrMaterialFlags add_material_flags, const Vec4 tint_color, bool tonemap);
void gfxQueueHemisphere(const Vec3 vMid, F32 fRadius, const Mat4 mWorldMatrix, int frustum_visible, RdrObjectCategory object_category, RdrMaterialFlags add_material_flags, const Vec4 tint_color, bool tonemap);
void gfxQueueCapsuleExact(F32 fHeightMin, F32 fHeightMax, F32 fRadius, const Mat4 mWorldMatrix, int frustum_visible, RdrObjectCategory object_category, RdrMaterialFlags add_material_flags, const Vec4 tint_color, bool tonemap);
void gfxQueueCapsule(F32 fHeightMin, F32 fHeightMax, F32 fRadius, const Mat4 mWorldMatrix, int frustum_visible, RdrObjectCategory object_category, RdrMaterialFlags add_material_flags, const Vec4 tint_color, bool tonemap, int wireframe);

