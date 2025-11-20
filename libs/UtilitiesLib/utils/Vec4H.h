/* Vec4H.h
 *	This file defines native hardware-supported four-element vector, Vec4H.
 */

#ifndef _VEC4H_H
#define _VEC4H_H
#pragma once
GCC_SYSTEM

#include "stdtypes.h"

#ifndef assert
#error
#endif


#if _PS3
#include "Vec4H_PS3.h" 
#elif _XBOX
#include "Vec4H_Xbox.h" 
#else
#include "Vec4H_PC.h" 
#endif

#include "mathutil.h"

//-----------------------------------------------------------------------------
//- Useful constants, Globals
//-----------------------------------------------------------------------------

__forceinline void vec4Hcpy(Vec4H * dest, const Vec4H * source, int count)
{
	for (; count; --count, ++dest, ++source)
		copyVec4H(*source, *dest);
}

// single source, dual destination SSE/VMX move
__forceinline void vec4Hcpy_dual(Vec4H * __restrict dest1, Vec4H * __restrict dest2,
	const Vec4H * __restrict source, int count)
{
	for (; count; --count, ++dest1, ++dest2, ++source)
	{
		Vec4H temp = *source;
		copyVec4H(temp, *dest1);
		copyVec4H(temp, *dest2);
	}
}

#define vec4HRunningMinMax(V, vMin, vMax) ((vMin = minVecExp4H(vMin, V)),(vMax = maxVecExp4H(vMax, V)))
#ifndef _XBOX
#define vecClampExp4H(V, vMin, vMax)	maxVecExp4H(minVecExp4H(V, vMax), vMin)
#define vecSaturateExp4H(V)				maxVecExp4H(minVecExp4H(V, g_vec4UnitH), g_vec4ZeroH)
#endif

// Multiply a 3-vector (x,y,z,1) by projection matrix using different source vectors for x, y, and z
// Also does perspective divide
// Useful for computing on-screen coordinates
__forceinline Vec4H mulVec3HProjMat44H(const Vec4H x_comp, const Vec4H y_comp, const Vec4H z_comp, const Mat44H * mat)
{
	Vec4H temp = vecAddExp4H(
		vecAddExp4H(
		vecMulExp4H(vecSplatXExp4H(x_comp), (*mat)[0]),
		vecMulExp4H(vecSplatYExp4H(y_comp), (*mat)[1])),
		vecAddExp4H(
		vecMulExp4H(vecSplatZExp4H(z_comp), (*mat)[2]),
		(*mat)[3]));
	Vec4H denom = vecRecip4H(temp);
	return vecMulExp4H(temp, vecSplatWExp4H(denom));
}

__forceinline void mulVec3HPProjMat44H(const Vec4H *vecs, const Mat44H * mat, Vec4H *vecsOut)
{
	Vec4H denom;
	Vec4H v0, v1, v2, v3;
	v0 = vecAddExp4H(
		vecAddExp4H(
		vecMulExp4H(vecSplatXExp4H(vecs[0]), mat[0][0]),
		vecMulExp4H(vecSplatYExp4H(vecs[0]), mat[0][1])),
		vecAddExp4H(
		vecMulExp4H(vecSplatZExp4H(vecs[0]), mat[0][2]),
		mat[0][3]));
	v1 = vecAddExp4H(
		vecAddExp4H(
		vecMulExp4H(vecSplatXExp4H(vecs[1]), mat[0][0]),
		vecMulExp4H(vecSplatYExp4H(vecs[1]), mat[0][1])),
		vecAddExp4H(
		vecMulExp4H(vecSplatZExp4H(vecs[1]), mat[0][2]),
		mat[0][3]));
	v2 = vecAddExp4H(
		vecAddExp4H(
		vecMulExp4H(vecSplatXExp4H(vecs[2]), mat[0][0]),
		vecMulExp4H(vecSplatYExp4H(vecs[2]), mat[0][1])),
		vecAddExp4H(
		vecMulExp4H(vecSplatZExp4H(vecs[2]), mat[0][2]),
		mat[0][3]));
	v3 = vecAddExp4H(
		vecAddExp4H(
		vecMulExp4H(vecSplatXExp4H(vecs[3]), mat[0][0]),
		vecMulExp4H(vecSplatYExp4H(vecs[3]), mat[0][1])),
		vecAddExp4H(
		vecMulExp4H(vecSplatZExp4H(vecs[3]), mat[0][2]),
		mat[0][3]));
	
	denom = vecRecip4H(vecMixExp4H(vecMixExp4H(v0, v1, S_WWWW), vecMixExp4H(v2, v3, S_WWWW), S_XZXZ));
	vecsOut[0] = vecMulExp4H(v0, vecSplatXExp4H(denom));
	vecsOut[1] = vecMulExp4H(v1, vecSplatYExp4H(denom));
	vecsOut[2] = vecMulExp4H(v2, vecSplatZExp4H(denom));
	vecsOut[3] = vecMulExp4H(v3, vecSplatWExp4H(denom));
}


// Multiply a 3-vector (x,y,z,1) by matrix using pre-splatted source vectors
__forceinline Vec4H mulSplattedVec3HMat44H(const Vec4H x_splat, const Vec4H y_comp, const Vec4H z_comp, const Mat44H * mat)
{
	return vecAddExp4H(
		vecAddExp4H(
		vecMulExp4H(x_splat, mat[0][0]),
		vecMulExp4H(y_comp, mat[0][1])),
		vecAddExp4H(
		vecMulExp4H(z_comp, mat[0][2]),
		mat[0][3]));
}

static const Vec4H g_vec4NegUnitH = { -1.0f, -1.0f, -1.0f, -1.0f };
static const Vec4H g_vec4ZeroH = { 0 };
static const Vec4H g_vec4UtilityH = { -1.0f, 1.0f, -0.0f, 0.0f };
static const Vec4H unitmat44H[4] = { {1.0f, 0.0f, 0.0f, 0.0f }, {0.0f, 1.0f, 0.0f, 0.0f }, {0.0f, 0.0f, 1.0f, 0.0f }, {0.0f, 0.0f, 0.0f, 1.0f } };

__forceinline void vec3W0toVec4H(const Vec3 source, Vec4HP dest)
{
	setVec4H(*dest,source[0],source[1],source[2],0.0f);
}

__forceinline void vec3W1toVec4H(const Vec3 source, Vec4HP dest)
{
	setVec4H(*dest,source[0],source[1],source[2],1.0f);
}

__forceinline void vec3WtoVec4H(const Vec3 source, float w, Vec4HP dest)
{
	setVec4H(*dest,source[0],source[1],source[2],w);
}

__forceinline void interpVec4H(F32 fParam,Vec4H * pvA,Vec4H * pvB,Vec4H * pvResult)
{
	Vec4H vTemp,vTemp2,vTemp3;
	setVec4sameH(vTemp,1.0f-fParam);
	vecVecMul4H(*pvA,vTemp,*pvResult);
	setVec4sameH(vTemp2,fParam);
	vecVecMul4H(*pvB,vTemp2,vTemp3);
	addVec4H(vTemp3,*pvResult,*pvResult);
}

__forceinline void mat4to44H(const Mat4 source, Vec4H * dest)
{
	vec3W0toVec4H(source[0], &dest[0]);
	vec3W0toVec4H(source[1], &dest[1]);
	vec3W0toVec4H(source[2], &dest[2]);
	vec3W1toVec4H(source[3], &dest[3]);
}

__forceinline void mat4toInvTranspose44H(const Mat4 source, Vec4H * dest)
{
	copyVec4(Vec4HToVec4(unitmat44H[3]), Vec4HToVec4(dest[3]));
	mulVecMat3Transpose(source[3], source, Vec4HToVec4(dest[3]));
	vec3WtoVec4H(source[0], -Vec4HToVec4(dest[3])[0], &dest[0]);
	vec3WtoVec4H(source[1], -Vec4HToVec4(dest[3])[1], &dest[1]);
	vec3WtoVec4H(source[2], -Vec4HToVec4(dest[3])[2], &dest[2]);
	copyVec4H(unitmat44H[3], dest[3]);
}

typedef struct VECALIGN Vec4PH
{
	Vec4H v[ 4 ];
} Vec4PH;

__forceinline void transposeVec44PH(Vec4PH * vecs)
{
	transposeMat44((Vec4*)vecs->v);
}

// Doesn't work
// // 4 vector parallel vector-matrix multiply
// __forceinline void mulVec4Mat44PH( Vec4PH * vecs, const Mat44H * mat )
// {
// 	Vec4H v0, v1, v2, v3;
// 	v0 = mulVec4HMat44H( vecs->v+0, mat );
// 	v1 = mulVec4HMat44H( vecs->v+1, mat );
// 	v2 = mulVec4HMat44H( vecs->v+2, mat );
// 	v3 = mulVec4HMat44H( vecs->v+3, mat );
// }

// In: 1 point, 4 plane parallel distance computation
// Out: 1 XYZW vector of four point-plane distances
__forceinline Vec4H vec4PlaneDistancePH( const Vec4H * point, const Vec4PH * planes )
{
	Vec4H t0, t1, t2, t3;
	// t0 = xxxx, etc
	vecSplatX(point[0], t0);
	vecSplatY(point[0], t1);
	vecSplatZ(point[0], t2);
	vecSplatW(point[0], t3);

	// t0 = [x*a0  x*a1  x*a2  x*a3]
	vecVecMul4H(t0, planes->v[0], t0);
	vecVecMul4H(t1, planes->v[1], t1);
	vecVecMul4H(t2, planes->v[2], t2);
	vecVecMul4H(t3, planes->v[3], t3);

	// combine
	addVec4H(t0, t1, t0);
	addVec4H(t2, t3, t2);
	addVec4H(t0, t2, t0);

	return t0;
}

__inline Vec4H mulVec4HMat44H(const Vec4H v, const Mat44H * mat)
{
	Vec4H t, t3;
	vecSplatX(v, t3);
	vecVecMul4H(t3, (*mat)[0], t);
	vecSplatY(v, t3);
	scaleAddVec4H(t3, (*mat)[1], t, t);
	vecSplatZ(v, t3);
	scaleAddVec4H(t3, (*mat)[2], t, t);
	vecSplatW(v, t3);
	scaleAddVec4H(t3, (*mat)[3], t, t);
	return t;
}

// assumes last "row" of matrix is 0,0,0,1
__inline Vec4H rotateVec4HTransformMat44H(const Vec4H v, const Mat44H * mat)
{
	Vec4H t, t3;
	vecSplatX(v, t3);
	vecVecMul4H(t3, (*mat)[0], t);
	vecSplatY(v, t3);
	scaleAddVec4H(t3, (*mat)[1], t, t);
	vecSplatZ(v, t3);
	scaleAddVec4H(t3, (*mat)[2], t, t);
	return t;
}

// as I wrote this, it assumes that if this is a transform, the translation is stored in mat[X][3]
// our naming convention can't really convey that yet
__forceinline Vec4H mulVec4HMat34H(const Vec4H v, const Mat34H * mat)
{
	Vec4H vDot[4], vResult;

	vecVecMul4H(v,(*mat)[0],vDot[0]);
	vecVecMul4H(v,(*mat)[1],vDot[1]);
	vecVecMul4H(v,(*mat)[2],vDot[2]);
	setVec4sameH(vDot[3],0.25f);

	vResult = sumComponents4Vec4H(&vDot[0],&vDot[1],&vDot[2],&vDot[3]);

	return vResult;
}

// Multiply a 3-vector (x,y,z,1) by matrix
__inline Vec4H mulVec3Mat44H(const Vec3 v3, const Mat44H * mat)
{
	Vec4H v,t, t3;

	setVec4H(v,v3[0],v3[1],v3[2],1.0f);
	vecSplatX(v, t3);
	vecVecMul4H(t3, (*mat)[0], t);
	vecSplatY(v, t3);
	scaleAddVec4H(t3, (*mat)[1], t, t);
	vecSplatZ(v, t3);
	scaleAddVec4H(t3, (*mat)[2], t, t);
	vecSplatW(v, t3);
	scaleAddVec4H(t3, (*mat)[3], t, t);
	return t;
}

// I don't know how to name these.  This is for a standard 3x3 represented as a 3x4 for speed
__forceinline void scale3Mat34HVec4H(Mat34H * a, const Vec4H sfactor)
{
	Vec4H vScaleTemp;
	vecSplatX(sfactor, vScaleTemp);
	vecVecMul4H((*a)[0], vScaleTemp, (*a)[0]);
	vecSplatY(sfactor, vScaleTemp);
	vecVecMul4H((*a)[1], vScaleTemp, (*a)[1]);
	vecSplatZ(sfactor, vScaleTemp);
	vecVecMul4H((*a)[2], vScaleTemp, (*a)[2]);
}

// This is for a transposed transform matrix (such as a skinning matrix, currently)
__forceinline void scaleMat34HVec4H2(Mat34H * a, const Vec4H sfactor)
{
	vecVecMul4H((*a)[0], sfactor, (*a)[0]);
	vecVecMul4H((*a)[1], sfactor, (*a)[1]);
	vecVecMul4H((*a)[2], sfactor, (*a)[2]);
}

__forceinline void mat34HtoSkinningMat4(Mat34H * a, SkinningMat4 b)
{
	// Hope to align SkinningMat
	copyVec4H((*a)[0],*(Vec4H *)&b[0]);
	copyVec4H((*a)[1],*(Vec4H *)&b[1]);
	copyVec4H((*a)[2],*(Vec4H *)&b[2]);
}

__forceinline void copyMat34H(Mat34H * src,Mat34H *dst)
{
	copyVec4H((*src)[0],(*dst)[0]);
	copyVec4H((*src)[1],(*dst)[1]);
	copyVec4H((*src)[2],(*dst)[2]);
}

__forceinline void transformMat44HtoSkinningMat4(Mat44H * a, SkinningMat4 b)
{
	transpose4Vec4H(&(*a)[0],&(*a)[1],&(*a)[2],&(*a)[3]);
	copyVec4H((*a)[0],*(Vec4H *)&b[0]);
	copyVec4H((*a)[1],*(Vec4H *)&b[1]);
	copyVec4H((*a)[2],*(Vec4H *)&b[2]);
}

// rename this if you feel the need to use it
#if 0
// Multiply a 3-vector (x,y,z,1) by matrix using different source vectors for x, y, and z
// Useful for generating transformed points on bounding box
static __inline Vec4H mulVec3HMat44H(const Vec4H x_comp, const Vec4H y_comp, const Vec4H z_comp, const Mat44H * mat)
{
	return vecAddExp4H(
		vecAddExp4H(
		vecMulExp4H(vecSplatXExp4H(x_comp), mat[0][0]),
		vecMulExp4H(vecSplatYExp4H(y_comp), mat[0][1])),
		vecAddExp4H(
		vecMulExp4H(vecSplatZExp4H(z_comp), mat[0][2]),
		mat[0][3]));
}
#endif

// Fills an arbitrary memory address with integers
__inline void fillIVec4H(int * piBuffer, int iVal, int iCount)
{
    Vec4H vVal;

	setIVec4H(vVal,iVal,iVal,iVal,iVal);

	while (((int)piBuffer & 0xf) && iCount)
	{
		*piBuffer = iVal;
		piBuffer++;
		iCount--;
	}

    while (iCount >= 4)
    {
        copyVec4H(vVal,*(Vec4HP)piBuffer);
        piBuffer += 4;
        iCount -= 4;
    }

    // Fill remaining LONGs.
    switch (iCount & 0x3)
    {
        case 0x3: *piBuffer++ = iVal;
        case 0x2: *piBuffer++ = iVal;
        case 0x1: *piBuffer++ = iVal;
    }
}

// _VEC4H_H
#endif
