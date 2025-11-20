#include "Vec4H.h"
#include "mathutil.h"

__forceinline int boundsIndex(int x, int y, int z)
{
	return ((!!z)<<2)|((!!y)<<1)|(!!x);
}

extern Vec4H mulBoundMasks[8];

// transformed must be 8 elements long
__inline void mulBounds(const Vec3 min, const Vec3 max, const Mat4 mat, Vec4_aligned * transformed)
{
	int	i;
	Vec4H	extents[2], pos;
	Mat44H   m44;
	Vec4H * pTrans4H = (Vec4H *)transformed;

	vec3W1toVec4H(min, &extents[0]);
	vec3W1toVec4H(max, &extents[1]);
	mat4to44H(mat,m44);

	for(i=0;i<8;i++)
	{
		vecSelect4H(extents[0],extents[1],mulBoundMasks[i],pos);
		pTrans4H[i] = mulVec4HMat44H(pos,&m44);
	}
}

__inline void mulBoundsAA(const Vec3 min, const Vec3 max, const Mat4 mat, Vec3 outmin, Vec3 outmax)
{
	Vec4H transformed[8];
	Vec4H newmin, newmax;

	mulBounds(min, max, mat, (Vec4_aligned *)transformed);
	setVec4sameH(newmin, 8e16);
	setVec4sameH(newmax, -8e16);
	vec4HRunningMinMax(transformed[0], newmin, newmax);
	vec4HRunningMinMax(transformed[1], newmin, newmax);
	vec4HRunningMinMax(transformed[2], newmin, newmax);
	vec4HRunningMinMax(transformed[3], newmin, newmax);
	vec4HRunningMinMax(transformed[4], newmin, newmax);
	vec4HRunningMinMax(transformed[5], newmin, newmax);
	vec4HRunningMinMax(transformed[6], newmin, newmax);
	vec4HRunningMinMax(transformed[7], newmin, newmax);

	copyVec3(Vec4HToVec4(newmin),outmin);
	copyVec3(Vec4HToVec4(newmax),outmax);
}

__forceinline void mulBounds44(const Vec3 min, const Vec3 max, const Mat44 mat, Vec4 transformed[8])
{
	int		x, xbit, y, ybit, z, zbit;
	Vec3	extents[2], pos;
	copyVec3(min, extents[0]);
	copyVec3(max, extents[1]);
	for(x=0,xbit=0;x<2;x++,xbit++)
	{
		for(y=0,ybit=0;y<2;y++,ybit+=2)
		{
			for(z=0,zbit=0;z<2;z++,zbit+=4)
			{
				pos[0] = extents[x][0];
				pos[1] = extents[y][1];
				pos[2] = extents[z][2];
				mulVecMat44(pos,mat,transformed[xbit|ybit|zbit]);
			}
		}
	}
}

__forceinline void mulBoundsAA44(const Vec3 min, const Vec3 max, const Mat44 mat, Vec3 newmin, Vec3 newmax)
{
	Vec4 transformed[8];
#ifdef _FULLDEBUG
	assert(min!=newmin && max!=newmin && min!=newmax && max!=newmax);
#endif
	mulBounds44(min, max, mat, transformed);
	setVec3(newmin, 8e16, 8e16, 8e16);
	setVec3(newmax, -8e16, -8e16, -8e16);

#define minmax4(vec) if ((vec)[3] > 0) { F32 scale = 1.f / (vec)[3]; scaleVec3(vec, scale, vec); vec3RunningMinMax(vec, newmin, newmax); }

	minmax4(transformed[0])
	minmax4(transformed[1])
	minmax4(transformed[2])
	minmax4(transformed[3])
	minmax4(transformed[4])
	minmax4(transformed[5])
	minmax4(transformed[6])
	minmax4(transformed[7])

#undef minmax4

	if (newmin[0] > 8e15)
	{
		setVec3(newmin, 0, 0, 0);
		setVec3(newmax, 0, 0, 0);
	}
}

__forceinline F32 boxCalcMid(const Vec3 min, const Vec3 max, Vec3 mid)
{
	F32 radius;
	subVec3(max, min, mid);
	scaleVec3(mid, 0.5f, mid);
	radius = lengthVec3(mid);
	addVec3(min, mid, mid);
	return radius;
}

__forceinline F32 boxCalcSize(const Vec3 min, const Vec3 max)
{
	return (max[0] - min[0]) * (max[1] - min[1]) * (max[2] - min[2]);
}

__forceinline void boxIntersect(const Vec3 min1, const Vec3 max1, const Vec3 min2, const Vec3 max2, Vec3 minOut, Vec3 maxOut)
{
	minOut[0] = MAX(min1[0], min2[0]);
	minOut[1] = MAX(min1[1], min2[1]);
	minOut[2] = MAX(min1[2], min2[2]);
	maxOut[0] = MIN(max1[0], max2[0]);
	maxOut[1] = MIN(max1[1], max2[1]);
	maxOut[2] = MIN(max1[2], max2[2]);
}

__forceinline F32 sphereDistanceToBoxSquared(const Vec3 boxmin, const Vec3 boxmax, const Vec3 center, F32 radius)
{
	F32 dist = distanceToBoxSquared(boxmin, boxmax, center) - SQR(radius);
	MAX1(dist, 0);
	return dist;
}

// useful for calculating the bounds of a Camera Facing model
// Note - the resulting bounds_min and bounds_max will always be negations of each other.
__inline void calcBoundsForAnyOrientation(const Vec3 bounds_min_in, const Vec3 bounds_max_in, Vec3 bounds_min_out, Vec3 bounds_max_out)
{
	Vec3 corner;
	F32 corner_distsq[8];
	F32 max_corner_distsq = 0.0f;
	S32 i;
	F32 max_corner_dist;

	copyVec3(bounds_min_in, corner);
	corner_distsq[0] = lengthVec3Squared(corner);

	setVec3(corner, bounds_min_in[0], bounds_min_in[1], bounds_max_in[2]);
	corner_distsq[1] = lengthVec3Squared(corner);

	setVec3(corner, bounds_min_in[0], bounds_max_in[1], bounds_min_in[2]);
	corner_distsq[2] = lengthVec3Squared(corner);

	setVec3(corner, bounds_min_in[0], bounds_max_in[1], bounds_max_in[2]);
	corner_distsq[3] = lengthVec3Squared(corner);

	setVec3(corner, bounds_max_in[0], bounds_min_in[1], bounds_min_in[2]);
	corner_distsq[4] = lengthVec3Squared(corner);

	setVec3(corner, bounds_max_in[0], bounds_min_in[1], bounds_max_in[2]);
	corner_distsq[5] = lengthVec3Squared(corner);

	setVec3(corner, bounds_max_in[0], bounds_max_in[1], bounds_min_in[2]);
	corner_distsq[6] = lengthVec3Squared(corner);

	copyVec3(bounds_max_in, corner);
	corner_distsq[7] = lengthVec3Squared(corner);

	for(i = 0; i < 8; i++)
		if(corner_distsq[i] > max_corner_distsq)
			max_corner_distsq = corner_distsq[i];

	max_corner_dist = fsqrt(max_corner_distsq);

	// the actual bounds given the distance to the furthest corner
	setVec3same(bounds_min_out, -max_corner_dist);
	setVec3same(bounds_max_out, max_corner_dist);
}

__inline F32 sphereUnion(Vec3 const vPt0,F32 fRad0,Vec3 const vPt1,F32 fRad1,Vec3 vCenterOut)
{
	Vec3 vDiff;
	F32 fRad,fDist,fFarDist0,fFarDist1;
	subVec3(vPt1,vPt0,vDiff);
	fDist = lengthVec3(vDiff);
	fFarDist0 = MAX(fRad0,fRad1-fDist);
	fFarDist1 = MAX(fRad1,fRad0-fDist);
	fRad = (fFarDist0+fDist+fFarDist1)*0.5f;

	if (fDist > 1e-5f)
	{
		scaleAddVec3(vDiff,(fRad-fFarDist0)/fDist,vPt0,vCenterOut);
	}
	else
	{
		copyVec3(vPt0,vCenterOut);
	}

	return fRad;
}