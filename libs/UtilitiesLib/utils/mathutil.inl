#pragma once

#include "Vec4H.h"

__forceinline float sqrtf_clamped(float x)
{
	Vec4H xh = _mm_load_ss(&x);
	// xh = <x, 0, 0, 0>
	Vec4H zeroh;
	float out;

	vecSplatW(xh, zeroh);
	_mm_store_ss(&out, _mm_sqrt_ss(_mm_max_ss(zeroh, xh)));
	return out;
}

__forceinline float acosf_clamped(float x)
{
	x = CLAMPF32(x, -1.0f, 1.0f);
	return acosf(x);
}

__forceinline float asinf_clamped(float x)
{
	x = CLAMPF32(x, -1.0f, 1.0f);
	return asinf(x);
}

// Note that logf_clamped does not exist, because the limit at the edge of the domain is -Inf.
// Any clamping will just produce -Inf.
__forceinline float logf_clamped(float x)
{
	return logf(x);
}

__inline void vec3RunningMinMaxWithRadius(const Vec3 v, Vec3 min, Vec3 max, F32 radius)
{
	Vec4H vRadius;
	Vec4H vMinPad,vMaxPad;
	Vec4H vMinResult,vMaxResult;
	setVec4sameH(vRadius, radius);
	setVec4H(vMinPad,v[0],v[1],v[2],0.0f);
	addVec4H(vMinPad,vRadius,vMaxPad);
	subVec4H(vMinPad,vRadius,vMinPad);
	setVec4H(vMinResult,min[0],min[1],min[2],0.0f);
	setVec4H(vMaxResult,max[0],max[1],max[2],0.0f);
	vMinResult = minVecExp4H(vMinResult,vMinPad);
	vMaxResult = maxVecExp4H(vMaxResult,vMaxPad);

	copyVec3(Vec4HToVec4(vMinResult),min);
	copyVec3(Vec4HToVec4(vMaxResult),max);
}

__forceinline F32 floatmin(F32 a, F32 b)
{
	Vec4H vA,vB,vResult;
	setVec4sameH(vA,a);
	setVec4sameH(vB,b);
	vResult = minVecExp4H(vA,vB);
	return Vec4HToVec4(vResult)[0];
}

__forceinline F32 floatmax(F32 a, F32 b)
{
	Vec4H vA,vB,vResult;
	setVec4sameH(vA,a);
	setVec4sameH(vB,b);
	vResult = maxVecExp4H(vA,vB);
	return Vec4HToVec4(vResult)[0];
}