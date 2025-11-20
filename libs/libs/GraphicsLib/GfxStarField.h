#pragma once
GCC_SYSTEM
/***************************************************************************



***************************************************************************/

#ifndef _GFXSTARFIELD_H_
#define _GFXSTARFIELD_H_

typedef struct SkyDrawable SkyDrawable;
typedef int GeoHandle;

typedef struct StarData
{
	Vec3 center;
	Vec3 xvec;
	Vec3 yvec;
	F32 size;
	F32 random1, random2;
	Vec3 color;
} StarData;

GeoHandle gfxDemandLoadStarField(SkyDrawable *drawable, F32 fov_y, int *tri_count, Vec4 starfield_param, bool *camera_facing);


#endif //_GFXSTARFIELD_H_

