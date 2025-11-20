#ifndef _GFXOCCLUSIONCLIP_H_
#define _GFXOCCLUSIONCLIP_H_

#include "GfxOcclusionTypes.h"
#include "Vec4H.h"

void drawZInitClipProc();

#if ZO_DRAW_INTERLEAVED
void drawZTriangleClip(F32 *zoBufData, int zoBufWidth, ZBufferPoint *p0, ZBufferPoint *p1, ZBufferPoint *p2, int clip_bit, int check_facing, 
	F32 ydim, F32 yoffset, int* trisDrawn, int intervalLine, int intervalSkip);
#else
void drawZTriangleClip(F32 *zoBufData, int zoBufWidth, ZBufferPoint *p0, ZBufferPoint *p1, ZBufferPoint *p2, int clip_bit, int check_facing,
	F32 ydim, F32 yoffset, int* trisDrawn);
#endif

#define CLIP_EPSILON (1E-5f)

enum {CLIP_MINX = 1, CLIP_MAXX = 1<<1, CLIP_MINY = 1<<2, CLIP_MAXY = 1<<3, CLIP_MINZ = 1<<4, CLIP_MAXZ = 1<<5};

__forceinline static int getClipCode(Vec4 v)
{
	float w;

	w = v[3] * (1.0f + CLIP_EPSILON);
	return	(v[0] < -w ? 1 : 0) |
			((v[0] > w ? 1 : 0) << 1) |
			((v[1] < -w ? 1 : 0) << 2) |
			((v[1] > w ? 1 : 0) << 3) |
			((v[2] < 0.0f ? 1 : 0) << 4) | 
			((v[2] > w ? 1 : 0) << 5) ;
}

__forceinline static int truncF32_SSE(F32 X)
{
	return _mm_cvtt_ss2si(_mm_load_ss(&X));
}

__forceinline static int roundF32_SSE(F32 X)
{
	return truncF32_SSE(X + 0.5f);
}

__forceinline static void transformPoint2(ZBufferPoint *transformed, F32 ydim, F32 yoffset)
{
	transformed->clipcode = getClipCode(transformed->hClipPos);

	if (!transformed->clipcode)
	{
		F32 scale;
		F32 x, y;
#if ZO_SAFE
		assert(transformed->hClipPos[3] != 0);
#endif

		scale = 1.f / transformed->hClipPos[3];
		x = 0.5f + transformed->hClipPos[0] * scale * 0.5f;
		y = 0.5f + transformed->hClipPos[1] * scale * 0.5f;

#if _PS3
		transformed->x = (int)((x * (ZB_DIM * ZB_SCAN_RES - 1))+0.5f);
		transformed->y = (int)((y * ydim + yoffset)+0.5f);
#else
		transformed->x = roundF32_SSE(x * (ZB_DIM * ZB_SCAN_RES - 1));
		transformed->y = roundF32_SSE(y * ydim + yoffset);
#endif
		transformed->z = transformed->hClipPos[2] * scale * ZMAX;

#if 1
		if (transformed->x >= ZB_DIM * ZB_SCAN_RES + ZB_HALF_SCAN_RES)
			transformed->x = ZB_DIM * ZB_SCAN_RES + ZB_HALF_SCAN_RES - 1;
		if (transformed->y >= ZB_DIM * ZB_SCAN_RES + ZB_HALF_SCAN_RES)
			transformed->y = ZB_DIM * ZB_SCAN_RES + ZB_HALF_SCAN_RES - 1;
#endif

#if ZO_SAFE
		assert(transformed->x >= -ZB_HALF_SCAN_RES);
		assert(transformed->x < ZB_DIM * ZB_SCAN_RES + ZB_HALF_SCAN_RES - 1);
		assert(transformed->y >= -ZB_HALF_SCAN_RES);
		assert(transformed->y < ZB_DIM * ZB_SCAN_RES + ZB_HALF_SCAN_RES - 1);
#endif
	}
}

__forceinline static void transformPoint(ZBufferPoint *transformed, const Vec3 point, const Mat44 toClipMat, 
	F32 ydim, F32 yoffset)
{
	mulVecMat44(point, toClipMat, transformed->hClipPos);
	transformPoint2(transformed, ydim, yoffset);
}

__forceinline static void transformPointJitter(ZBufferPoint *transformed, const Vec3 point, const Mat44 toClipMat, 
	F32 ydim, F32 yoffset, F32 jitter_amount, F32 projection_far, F32 one_over_projection_far)
{
	float jitter;

	mulVecMat44(point, toClipMat, transformed->hClipPos);

	jitter = transformed->hClipPos[2];
	if (jitter < 0)
		jitter = (projection_far + jitter) * one_over_projection_far;
	else
		jitter = (projection_far - jitter) * one_over_projection_far;
	jitter = jitter_amount * jitter * jitter * 1.1f;
	transformed->hClipPos[0] += jitter;
	transformed->hClipPos[1] += jitter;

	transformPoint2(transformed, ydim, yoffset);
}


#endif //_GFXOCCLUSIONCLIP_H_
