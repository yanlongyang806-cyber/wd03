/* mathutil.h
 *	This file has been split up to General Math util and 3D Math Util.
 */

#ifndef _FMATH_H
#define _FMATH_H
#pragma once
GCC_SYSTEM

#include "stdtypes.h"

#ifndef assert
#error
#endif

#include "fpmacros.h"
#include "FloatBranch.h"
#include <float.h>

// Undefine potential macro definitions from math.h before including it
#ifdef round
#undef round
#endif
#ifdef log2
#undef log2
#endif

#include <math.h>
#include <stdlib.h>
#include "UtilitiesLibEnums.h"

C_DECLARATIONS_BEGIN

#define USE_EXCEPTION_SAFE_ROUND 0

#define VEC_ALIGNMENT 16

#define VECALIGN __ALIGN(VEC_ALIGNMENT)

__forceinline static void * AlignPointerUpPow2(void *pBasePointer, size_t granularity)
{
	return UINT_TO_PTR( (PTR_TO_UINT(pBasePointer) + granularity - 1) & ~(granularity - 1) );
}

__forceinline static void assertVecAligned(const void * pointer)
{
	assert(!(PTR_TO_UINT(pointer) % VEC_ALIGNMENT));
}

#define FINITEVEC2(x) (FINITE(x[0]) && FINITE(x[1]))
#define FINITEVEC3(x) (FINITEVEC2(x) && FINITE(x[2]))
#define FINITEVEC4(x) (FINITEVEC3(x) && FINITE(x[3]))
#define FINITEQUAT(x) (FINITEVEC4(x))
#define FINITEMAT3(x) (FINITEVEC3(x[0]) && FINITEVEC3(x[1]) && FINITEVEC3(x[2]))
#define FINITEMAT4(x) (FINITEMAT3(x) && FINITEVEC3(x[3]))
#define FINITEMAT3_4(x) (FINITEVEC4(x[0]) && FINITEVEC4(x[1]) && FINITEVEC4(x[2]))
#define FINITEMAT4_4(x) (FINITEMAT3_4(x) && FINITEVEC4(x[3]))

// Most of the math code uses the gcc single-precision version of the standard
// math functions. These defines can be commented out for any function the
// particular compiler you're using supports in single-precision format
U32 getFPExceptionMask(void);
void setFPExceptionMask(U32 mask);
void disableFPExceptions(S32 disabled);

typedef U32 FPExceptionMask;
void clearFPExceptionMaskForThisThread(FPExceptionMask* maskOut);
void setFPExceptionMaskForThisThread(FPExceptionMask mask);

#define FP_NO_EXCEPTIONS_BEGIN {										\
			FPExceptionMask backupFPExceptionsMask;						\
			clearFPExceptionMaskForThisThread(&backupFPExceptionsMask);{\
			void akljdsflkjsdkfjslkjf(void)

#define FP_NO_EXCEPTIONS_END }											\
			setFPExceptionMaskForThisThread(backupFPExceptionsMask);	\
		}((void)0)

__forceinline static U32 getCombinedFPUExceptionStatus()
{
#if _M_IX86
	// check FPU and SSE units
	U32 overflow_flags_x86 = 0, overflow_flags_sse = 0;
	_statusfp2(&overflow_flags_x86, &overflow_flags_sse); 
	return overflow_flags_x86 | overflow_flags_sse;
#else
	// only SSE under x64
	return _statusfp();
#endif
}

#define fsqrt(a)	(float)sqrt(a)
#define fsin(a)		sin(a)
#define fcos(a)		cos(a)
#define ftan(a)		tan(a)
#define fasin(a)	asin(a)
#define facos(a)	acos(a)
#define fatan(a)	atan(a)
#define fatan2(y,x)	atan2(y,x)

extern  const Mat4 zeromat;		// Zero matrix
extern  const Mat4 unitmat;		// Unit matrix
extern  const Mat4 lrmat;			// L-hand to R-hand transform
extern  const Vec3 zerovec3;
extern  const Vec3 onevec3;
extern  const Vec4 upvec;
extern  const Vec4 sidevec;
extern  const Vec4 forwardvec;
extern  const Vec3 unitvec3; // same as onevec
extern  const Vec4 zerovec4;
extern  const Vec4 onevec4;
extern  const Vec4 unitvec4;
extern	const Mat44 unitmat44;
extern  F32 sintable[];
extern  F32 costable[];


#define NEGNORMALIZE(a) CLAMP((a+1.0f) * 0.5f, 0.0f, 1.0f)

#define NEXTMULTIPLE(v, m) (((v%m)==0)?v:((v/m + 1)*m))

#define DEGVEC3(v) (v[0] = DEG(v[0]), v[1] = DEG(v[1]), v[2] = DEG(v[2]))
#define RADVEC3(v) (v[0] = RAD(v[0]), v[1] = RAD(v[1]), v[2] = RAD(v[2]))


__forceinline static void CLAMPVEC4(SA_PRE_NN_ELEMS(4) SA_POST_OP_VALID Vec4 v, F32 min, F32 max)
{
	v[0] = CLAMPF32(v[0], min, max);
	v[1] = CLAMPF32(v[1], min, max);
	v[2] = CLAMPF32(v[2], min, max);
	v[3] = CLAMPF32(v[3], min, max);
}
__forceinline static void CLAMPVEC3(SA_PRE_NN_ELEMS(3) SA_POST_OP_VALID Vec3 v, F32 min, F32 max)
{
	v[0] = CLAMPF32(v[0], min, max);
	v[1] = CLAMPF32(v[1], min, max);
	v[2] = CLAMPF32(v[2], min, max);
}

__forceinline static void CLAMPVEC2(SA_PRE_NN_ELEMS(2) SA_POST_OP_VALID Vec2 v, F32 min, F32 max)
{
	v[0] = CLAMPF32(v[0], min, max);
	v[1] = CLAMPF32(v[1], min, max);
}

__forceinline F32 floatmin(F32 a, F32 b);
__forceinline F32 floatmax(F32 a, F32 b);

#define MINVEC2(v1,v2,r) (r[0] = MIN((v1)[0],(v2)[0]),r[1] = MIN((v1)[1],(v2)[1]))
#define MAXVEC2(v1,v2,r) (r[0] = MAX((v1)[0],(v2)[0]),r[1] = MAX((v1)[1],(v2)[1]))

#define MINVEC3(v1,v2,r) (r[0] = MIN((v1)[0],(v2)[0]),r[1] = MIN((v1)[1],(v2)[1]),r[2] = MIN((v1)[2],(v2)[2]))
#define MAXVEC3(v1,v2,r) (r[0] = MAX((v1)[0],(v2)[0]),r[1] = MAX((v1)[1],(v2)[1]),r[2] = MAX((v1)[2],(v2)[2]))

// no guarantees of proper edge-case handling, but won't call ceil_default on x86
__forceinline F32 Ceilf(F32 fIn)
{
	return -floor(-fIn);
}

__forceinline static void CLAMPVEC3VEC3(SA_PRE_NN_ELEMS(3) SA_POST_OP_VALID Vec3 v1, SA_PRE_NN_ELEMS(3) SA_POST_OP_VALID Vec3 v2, SA_PRE_NN_ELEMS(3) SA_POST_OP_VALID Vec3 v3)
{
	v1[0] = MIN(MAX(v1[0], v2[0]), v3[0]);
	v1[1] = MIN(MAX(v1[1], v2[1]), v3[1]);
	v1[2] = MIN(MAX(v1[2], v2[2]), v3[2]);
}

// Math macros
#define SIMPLEANGLE(ang)			((ang) >   PI ? (ang)-TWOPI : \
									 (ang) <= -PI ? (ang)+TWOPI : (ang))

#define nearf(a,b) ((a)-(b) >= -0.00001f && (a)-(b) <= 0.00001f)

__forceinline static bool nearz( float val )
{
	return nearf( val, 0 );
}

__forceinline static bool near_multiple( float val, float mult )
{
	float mod = fabs( fmod( val, mult ));

	if( mod > mult / 2 ) {
		mod = mult - mod;
	}

	return mod < 0.001f;
}

#define vecXidx 0
#define vecYidx 1
#define vecZidx 2
#define vecX(v) ((v)[vecXidx])
#define vecY(v) ((v)[vecYidx])
#define vecZ(v) ((v)[vecZidx])
#define vecParamsXYZHex(pos) *(U32*)&vecX(pos), *(U32*)&vecY(pos), *(U32*)&vecZ(pos)
#define vecParamsXYZ(pos) vecX(pos), vecY(pos), vecZ(pos)
#define vecParamsXY(pos) vecX(pos), vecY(pos)
#define vecParamsXZ(pos) vecX(pos), vecZ(pos)

#define copyVec2(src,dst)			(((dst)[0]) = ((src)[0]), ((dst)[1]) = ((src)[1]))
#define copyVec3(src,dst)			(((dst)[0]) = ((src)[0]), ((dst)[1]) = ((src)[1]), ((dst)[2]) = ((src)[2])) 
#define copyVec4(src,dst)			(((dst)[0]) = ((src)[0]), ((dst)[1]) = ((src)[1]), ((dst)[2]) = ((src)[2]), ((dst)[3]) = ((src)[3]))
#define copyVec3XZ(src,dst)			(((dst)[0]) = ((src)[0]), ((dst)[2]) = ((src)[2])) 
#define copyVec3Vec3i(src,dst)		((dst)[0] = qtrunc((src)[0]), (dst)[1] = qtrunc((src)[1]), (dst)[2] = qtrunc((src)[2]))
#define setVec2same(dst,a)			((dst)[0] = (a), (dst)[1] = (a))
#define setVec3same(dst,a)			((dst)[0] = (a), (dst)[1] = (a), (dst)[2] = (a))
#define setVec4same(dst,a)			((dst)[0] = (a), (dst)[1] = (a), (dst)[2] = (a), (dst)[3] = (a))
#define zeroVec2(v1)				setVec2same(v1, 0)
#define zeroVec3(v1)				setVec3same(v1, 0)
#define zeroVec4(v1)				setVec4same(v1, 0)
#define zeroVec3XZ(v1)				((v1)[0] = 0, (v1)[2] = 0)
#define unitVec2(v1)				setVec2same(v1, 1)
#define unitVec3(v1)				setVec3same(v1, 1)
#define unitVec4(v1)				setVec4same(v1, 1)
#define vec2IsZero(v)				((v)[0] == 0 && (v)[1] == 0)
#define vec3IsZero(v)				((v)[0] == 0 && (v)[1] == 0 && (v)[2] == 0)
#define vec3IsZeroXZ(v)				((v)[0] == 0 && (v)[2] == 0)
#define vec4IsZero(v)				((v)[0] == 0 && (v)[1] == 0 && (v)[2] == 0 && (v)[3] == 0)
#define setVec2(dst,a,b)			((dst)[0] = (a), (dst)[1] = (b))
#define setVec3(dst,a,b,c)			((dst)[0] = (a), (dst)[1] = (b), (dst)[2] = (c))
#define setVec4(dst,a,b,c,d)		((dst)[0] = (a), (dst)[1] = (b), (dst)[2] = (c), (dst)[3] = (d))
#define setVec5(dst,a,b,c,d,e)		((dst)[0] = (a), (dst)[1] = (b), (dst)[2] = (c), (dst)[3] = (d), (dst)[4] = (e))

// This is a function version of setVec4. Call this version to avoid 
// reevaluating the address expression, if the expression is complex or
// has side effects. Also, use this function to avoid potential pointer
// aliasing forcing the compiler to recalculate the destination address.
__forceinline static void setVec4F(F32 *pVec4, F32 x, F32 y, F32 z, F32 w)
{
	pVec4[ 0 ] = x;
	pVec4[ 1 ] = y;
	pVec4[ 2 ] = z;
	pVec4[ 3 ] = w;
}


#define mulVecVec2(v1,v2,r)			((r)[0] = (v1)[0]*(v2)[0], (r)[1] = (v1)[1]*(v2)[1])
#define mulVecVec3(v1,v2,r)			((r)[0] = (v1)[0]*(v2)[0], (r)[1] = (v1)[1]*(v2)[1], (r)[2] = (v1)[2]*(v2)[2])
#define mulVecVec4(v1,v2,r)			((r)[0] = (v1)[0]*(v2)[0], (r)[1] = (v1)[1]*(v2)[1], (r)[2] = (v1)[2]*(v2)[2], (r)[3] = (v1)[3]*(v2)[3])
#define subVec2(v1,v2,r)			((r)[0] = (v1)[0]-(v2)[0], (r)[1] = (v1)[1]-(v2)[1])
#define subVec3(v1,v2,r)			((r)[0] = (v1)[0]-(v2)[0], (r)[1] = (v1)[1]-(v2)[1], (r)[2] = (v1)[2]-(v2)[2])
#define subVec3XZ(v1,v2,r)			((r)[0] = (v1)[0]-(v2)[0], (r)[2] = (v1)[2]-(v2)[2])
#define subVec4(v1,v2,r)			((r)[0] = (v1)[0]-(v2)[0], (r)[1] = (v1)[1]-(v2)[1], (r)[2] = (v1)[2]-(v2)[2], (r)[3] = (v1)[3]-(v2)[3])
#define subVec2same(v1,a,r)			((r)[0] = (v1)[0]-(a), (r)[1] = (v1)[1]-(a))
#define subVec3same(v1,a,r)			((r)[0] = (v1)[0]-(a), (r)[1] = (v1)[1]-(a), (r)[2] = (v1)[2]-(a))
#define subVec4same(v1,a,r)			((r)[0] = (v1)[0]-(a), (r)[1] = (v1)[1]-(a), (r)[2] = (v1)[2]-(a), (r)[3] = (v1)[3]-(a))
#define subFromVec2(v,r)			((r)[0] -= (v)[0], (r)[1] -= (v)[1])
#define subFromVec3(v,r)			((r)[0] -= (v)[0], (r)[1] -= (v)[1], (r)[2] -= (v)[2])
#define subFromVec4(v,r)			((r)[0] -= (v)[0], (r)[1] -= (v)[1], (r)[2] -= (v)[2], (r)[3] -= (v)[3])
#define subS32(a,b)					((S32)((a)-(b)))
#define addVec2(v1,v2,r)			((r)[0] = (v1)[0]+(v2)[0], (r)[1] = (v1)[1]+(v2)[1])
#define addVec3(v1,v2,r)			((r)[0] = (v1)[0]+(v2)[0], (r)[1] = (v1)[1]+(v2)[1], (r)[2] = (v1)[2]+(v2)[2])
#define addVec3XZ(v1,v2,r)			((r)[0] = (v1)[0]+(v2)[0], (r)[2] = (v1)[2]+(v2)[2])
#define addVec4(v1,v2,r)			((r)[0] = (v1)[0]+(v2)[0], (r)[1] = (v1)[1]+(v2)[1], (r)[2] = (v1)[2]+(v2)[2], (r)[3] = (v1)[3]+(v2)[3])
#define addVec2same(v1,a,r)			((r)[0] = (v1)[0]+(a), (r)[1] = (v1)[1]+(a))
#define addVec3same(v1,a,r)			((r)[0] = (v1)[0]+(a), (r)[1] = (v1)[1]+(a), (r)[2] = (v1)[2]+(a))
#define addVec4same(v1,a,r)			((r)[0] = (v1)[0]+(a), (r)[1] = (v1)[1]+(a), (r)[2] = (v1)[2]+(a), (r)[3] = (v1)[3]+(a))
#define addToVec2(v,r)				((r)[0] += (v)[0], (r)[1] += (v)[1])
#define addToVec3(v,r)				((r)[0] += (v)[0], (r)[1] += (v)[1], (r)[2] += (v)[2])
#define addToVec4(v,r)				((r)[0] += (v)[0], (r)[1] += (v)[1], (r)[2] += (v)[2], (r)[3] += (v)[3])
#define scaleVec2(v1,s,r)			((r)[0] = (v1)[0]*(s), (r)[1] = (v1)[1]*(s))
#define scaleVec3(v1,s,r)			((r)[0] = (v1)[0]*(s), (r)[1] = (v1)[1]*(s), (r)[2] = (v1)[2]*(s))
#define scaleVec3XZ(v1,s,r)			((r)[0] = (v1)[0]*(s), (r)[2] = (v1)[2]*(s))
#define scaleVec4(v1,s,r)			((r)[0] = (v1)[0]*(s), (r)[1] = (v1)[1]*(s), (r)[2] = (v1)[2]*(s), (r)[3] = (v1)[3]*(s))
#define scaleByVec2(r,s)			((r)[0] *= (s), (r)[1] *= (s))
#define scaleByVec3(r,s)			((r)[0] *= (s), (r)[1] *= (s), (r)[2] *= (s))
#define scaleByVec4(r,s)			((r)[0] *= (s), (r)[1] *= (s), (r)[2] *= (s), (r)[3] *= (s))
#define scaleAddVec2(a,s,b,r)		((r)[0] = ((a)[0]*(s)+(b)[0]), (r)[1] = ((a)[1]*(s)+(b)[1]))
#define scaleAddVec3(a,s,b,r)		((r)[0] = ((a)[0]*(s)+(b)[0]), (r)[1] = ((a)[1]*(s)+(b)[1]), (r)[2] = ((a)[2]*(s)+(b)[2]))
#define scaleAddVecVec3(a,s,b,r)	((r)[0] = ((a)[0]*(s)[0]+(b)[0]), (r)[1] = ((a)[1]*(s)[1]+(b)[1]), (r)[2] = ((a)[2]*(s)[2]+(b)[2]))
#define scaleAddVec4(a,s,b,r)		((r)[0] = ((a)[0]*(s)+(b)[0]), (r)[1] = ((a)[1]*(s)+(b)[1]), (r)[2] = ((a)[2]*(s)+(b)[2]), (r)[3] = ((a)[3]*(s)+(b)[3]))
#define scaleAddVec3XZ(a,s,b,r)		((r)[0] = ((a)[0]*(s)+(b)[0]), (r)[2] = ((a)[2]*(s)+(b)[2]))
#define lerpVec2(a,t,b,r)			((r)[0] = ((a)[0]*(t)+(b)[0]*(1-(t))), (r)[1] = ((a)[1]*(t)+(b)[1]*(1-(t))))
#define lerpVec3(a,t,b,r)			((r)[0] = ((a)[0]*(t)+(b)[0]*(1-(t))), (r)[1] = ((a)[1]*(t)+(b)[1]*(1-(t))), (r)[2] = ((a)[2]*(t)+(b)[2]*(1-(t))))
#define lerpVec4(a,t,b,r)			((r)[0] = ((a)[0]*(t)+(b)[0]*(1-(t))), (r)[1] = ((a)[1]*(t)+(b)[1]*(1-(t))), (r)[2] = ((a)[2]*(t)+(b)[2]*(1-(t))), (r)[3] = ((a)[3]*(t)+(b)[3]*(1-(t))))
#define scaleSubVec3(a,s,b,r)		((r)[0] = ((a)[0]*(s)-(b)[0]), (r)[1] = ((a)[1]*(s)-(b)[1]), (r)[2] = ((a)[2]*(s)-(b)[2]))
#define crossVec2(v1,v2)			((v1)[0]*(v2)[1] - (v1)[1]*(v2)[0])
#define crossVec3XY(v1,v2)			crossVec2(v1,v2)
#define crossVec3XZ(v1,v2)			((v1)[0]*(v2)[2] - (v1)[2]*(v2)[0])
#define crossVec3ZX(v1,v2)			((v1)[2]*(v2)[0] - (v1)[0]*(v2)[2])
#define crossVec3YZ(v1,v2)			((v1)[1]*(v2)[2] - (v1)[2]*(v2)[1])
#define crossVec3(v1,v2,r)			((r)[0] = (v1)[1]*(v2)[2] - (v1)[2]*(v2)[1], \
									(r)[1] = (v1)[2]*(v2)[0] - (v1)[0]*(v2)[2], \
									(r)[2] = (v1)[0]*(v2)[1] - (v1)[1]*(v2)[0])
#define crossVec3Up(v2,r)			((r)[0] = (v2)[2], \
									(r)[1] = 0.0f, \
									(r)[2] = -(v2)[0])
#define dotVec2(v1,v2)				((v1)[0]*(v2)[0] + (v1)[1]*(v2)[1])
#define dotVec3(v1,v2)				((v1)[0]*(v2)[0] + (v1)[1]*(v2)[1] + (v1)[2]*(v2)[2])
#define dotVec3XZ(v1,v2)			((v1)[0]*(v2)[0] + (v1)[2]*(v2)[2])
#define dotVec4(v1,v2)				((v1)[0]*(v2)[0] + (v1)[1]*(v2)[1] + (v1)[2]*(v2)[2] + (v1)[3]*(v2)[3])
#define dotVec32D(v1,v2)			((v1)[0]*(v2)[0] + (v1)[2]*(v2)[2])
#define sameVec2(v1,v2)				(((v1)[0]==(v2)[0]) && ((v1)[1]==(v2)[1]))
#define sameVec3(v1,v2)				(((v1)[0]==(v2)[0]) && ((v1)[1]==(v2)[1]) && ((v1)[2]==(v2)[2]))
#define sameVec4(v1,v2)				(((v1)[0]==(v2)[0]) && ((v1)[1]==(v2)[1]) && ((v1)[2]==(v2)[2]) && ((v1)[3]==(v2)[3]))
#define sameVecN(v1,v2,n)           ((0>=(n) || (v1)[0]==(v2)[0]) && (1>=(n) || (v1)[1]==(v2)[1]) && (2>=(n) || (v1)[2]==(v2)[2]) && (3>=(n) || (v1)[3]==(v2)[3]))
#define sameMat3(m1,m2)				(sameVec3((m1)[0], (m2)[0]) && sameVec3((m1)[1], (m2)[1]) && sameVec3((m1)[2], (m2)[2]))
#define sameMat4(m1,m2)				(sameMat3(m1, m2) && sameVec3((m1)[3], (m2)[3]))
#define centerVec2(min,max,r)		((r)[0] = (((min)[0]+(max)[0])*0.5f), (r)[1] = (((min)[1]+(max)[1])*0.5f))
#define centerVec3(min,max,r)		((r)[0] = (((min)[0]+(max)[0])*0.5f), (r)[1] = (((min)[1]+(max)[1])*0.5f), (r)[2] = (((min)[2]+(max)[2])*0.5f))
#define centerVec4(min,max,r)		((r)[0] = (((min)[0]+(max)[0])*0.5f), (r)[1] = (((min)[1]+(max)[1])*0.5f), (r)[2] = (((min)[2]+(max)[2])*0.5f), (r)[3] = (((min)[3]+(max)[3])*0.5f))
#define powVec2(src,power,dst)		((dst)[0] = pow((src)[0], (power)), (dst)[1] = pow((src)[1], (power)))
#define powVec3(src,power,dst)		((dst)[0] = pow((src)[0], (power)), (dst)[1] = pow((src)[1], (power)), (dst)[2] = pow((src)[2], (power)))
#define powVec4(src,power,dst)		((dst)[0] = pow((src)[0], (power)), (dst)[1] = pow((src)[1], (power)), (dst)[2] = pow((src)[2], (power)), (dst)[3] = pow((src)[3], (power)))

#define lengthVec2Squared(v1)		(SQR((v1)[0]) + SQR((v1)[1]))
#define lengthVec2(v1)				(fsqrt(lengthVec2Squared(v1)))
#define lengthVec3Squared(v1)		(SQR((v1)[0]) + SQR((v1)[1]) + SQR((v1)[2]))
#define lengthVec3(v1)				(fsqrt(lengthVec3Squared(v1)))
#define lengthVec4Squared(v1)		(SQR((v1)[0]) + SQR((v1)[1]) + SQR((v1)[2]) + SQR((v1)[3]))
#define lengthVec4(v1)				(fsqrt(lengthVec4Squared(v1)))

#define lengthVec3SquaredXZ(v1)		(SQR((v1)[0]) + SQR((v1)[2]))
#define lengthVec3XZ(v1)			(fsqrt(lengthVec3SquaredXZ(v1)))

#define maxAbsElemVec3(v)			(MAX(fabs(v[0]), MAX( fabs(v[1]), fabs(v[2])) ))

#define distanceY(v1,v2)			(fabs((v1)[1] - (v2)[1]))

#define moveVec3(p,v,amt)			(p[0] += (v)[0]*(amt), p[1] += (v)[1]*(amt), p[2] += (v)[2]*(amt))
#define moveinX(mat,amt)			(moveVec3((mat)[3],(mat)[0],amt))
#define moveinY(mat,amt)			(moveVec3((mat)[3],(mat)[1],amt))
#define moveinZ(mat,amt)			(moveVec3((mat)[3],(mat)[2],amt))
#define moveVinX(p,mat,amt)			(moveVec3(p,(mat)[0],amt))
#define moveVinY(p,mat,amt)			(moveVec3(p,(mat)[1],amt))
#define moveVinZ(p,mat,amt)			(moveVec3(p,(mat)[2],amt))

#define getVec3Yaw(vec)				(fatan2((vec)[0],(vec)[2]))
#define getVec3Roll(vec)			(fatan2((vec)[1],(vec)[0]))

#define expandVec2(vec)				(vec)[0], (vec)[1]
#define expandVec3(vec)				(vec)[0], (vec)[1], (vec)[2]
#define expandVec4(vec)				(vec)[0], (vec)[1], (vec)[2], (vec)[3]

#define printVec3(vec)				printf("( %.2f, %.2f, %.2f )", (vec)[0], (vec)[1], (vec)[2] )
#define printVec4(vec)				printf("( %.2f, %.2f, %.2f, %.2f )", (vec)[0], (vec)[1], (vec)[2], (vec)[3] )

#define validateVec3(vec)			(FINITE((vec)[0]) && FINITE((vec)[1]) && FINITE((vec)[2]))

#define validateMat3(mat)			(validateVec3((mat)[0]) && validateVec3((mat)[1]) && validateVec3((mat)[2]))
#define validateMat4(mat)			(validateVec3((mat)[0]) && validateVec3((mat)[1]) && validateVec3((mat)[2]) && validateVec3((mat)[3]))

#define getMat3Row(mat, row, vec)	(setVec3((vec), (mat)[0][(row)], (mat)[1][(row)], (mat)[2][(row)]))
#define setMat3Row(mat, row, vec)	((mat)[0][(row)] = (vec)[0], (mat)[1][(row)] = (vec)[1], (mat)[2][(row)] = (vec)[2])

#define getMatRow(mat, row, vec)	(setVec4((vec), (mat)[0][(row)], (mat)[1][(row)], (mat)[2][(row)], (mat)[3][(row)]))
#define setMatRow(mat, row, vec)	((mat)[0][(row)] = (vec)[0], (mat)[1][(row)] = (vec)[1], (mat)[2][(row)] = (vec)[2], (mat)[3][(row)] = (vec)[3])

__forceinline static void getMat34Col(const Vec4 * __restrict mat, int col, F32 * __restrict vec3)
{
	setVec3(vec3, mat[0][col], mat[1][col], mat[2][col]);
}

__forceinline static void setMat34Col(Vec4 * __restrict mat, int col, const F32 * __restrict vec3)
{
	mat[0][col] = vec3[0];
	mat[1][col] = vec3[1];
	mat[2][col] = vec3[2];
}

// This compares two IVec4s to see if they are identical.
__forceinline static int sameIVec4(const IVec4 pVecA, const IVec4 pVecB)
{
	return pVecA[0] == pVecB[0] && pVecA[1] == pVecB[1] &&
		pVecA[2] == pVecB[2] && pVecA[3] == pVecB[3];
}

__forceinline static int sameIVec3(const IVec3 pVecA, const IVec3 pVecB)
{
	return pVecA[0] == pVecB[0] && pVecA[1] == pVecB[1] &&
		pVecA[2] == pVecB[2];
}

// This compares two Vec4s to see if they are identical. Use this only on
// Vec4's that are in memory, not already loaded into registers. That means
// if you have just calculated or modified the Vec4, it is probably at
// least partially in registers, and this method may not be faster. 
// Additionally, this function considers negative-zero different than zero.
__forceinline static int sameVec4InMem(const Vec4 pVecA, const Vec4 pVecB)
{
	return sameIVec4((const int*)pVecA, (const int*)pVecB);
}

__forceinline static int sameVec3InMem(const Vec3 pVecA, const Vec3 pVecB)
{
	return sameIVec3((const int*)pVecA, (const int*)pVecB);
}

#define CMP_VEC_COMPONENT(v1, v2, p) { float t = (v1)[(p)] - (v2)[(p)]; if (t) return SIGN(t); }
#define CMP_IVEC_COMPONENT(v1, v2, p) { int t = (v1)[(p)] - (v2)[(p)]; if (t) return SIGN(t); }

// Functions for quicksorting points
__forceinline static S32 cmpVec3XYZ(const Vec3 in1, const Vec3 in2)
{
	CMP_VEC_COMPONENT(in1,in2,0);
	CMP_VEC_COMPONENT(in1,in2,1);
	CMP_VEC_COMPONENT(in1,in2,2);
	return 0;	
}

__forceinline static S32 cmpVec3XZY(const Vec3 in1, const Vec3 in2)
{
	CMP_VEC_COMPONENT(in1,in2,0);
	CMP_VEC_COMPONENT(in1,in2,2);
	CMP_VEC_COMPONENT(in1,in2,1);
	return 0;	
}

__forceinline static S32 cmpVec3ABC(const Vec3 in1, const Vec3 in2, int a, int b, int c)
{
	if(a<0 || a>3)
		return 0;
	CMP_VEC_COMPONENT(in1,in2,a);

	if(b<0 || b>3)
		return 0;
	CMP_VEC_COMPONENT(in1,in2,b);

	if(c<0 || c>3)
		return 0;
	CMP_VEC_COMPONENT(in1,in2,c);

	return 0;	
}

__forceinline static S32 cmpVec3ZX(const Vec3 in1, const Vec3 in2)
{
	CMP_VEC_COMPONENT(in1,in2,2);
	CMP_VEC_COMPONENT(in1,in2,0);
	return 0;
}

__forceinline static S32 cmpVec2(const Vec2 in1, const Vec2 in2)
{
	CMP_VEC_COMPONENT(in1,in2,0);
	CMP_VEC_COMPONENT(in1,in2,1);
	return 0;	
}

__forceinline static S32 cmpVec4(const Vec4 in1, const Vec4 in2)
{
	CMP_VEC_COMPONENT(in1,in2,0);
	CMP_VEC_COMPONENT(in1,in2,1);
	CMP_VEC_COMPONENT(in1,in2,2);
	CMP_VEC_COMPONENT(in1,in2,3);
	return 0;	
}

__forceinline static S32 cmpIVec4(const IVec4 in1, const IVec4 in2)
{
	CMP_IVEC_COMPONENT(in1,in2,0);
	CMP_IVEC_COMPONENT(in1,in2,1);
	CMP_IVEC_COMPONENT(in1,in2,2);
	CMP_IVEC_COMPONENT(in1,in2,3);
	return 0;	
}

__forceinline static void leftShiftIVec3(IVec3 v)
{
	int temp = v[1];
	v[1] = v[2];
	v[2] = v[0];
	v[0] = temp;
}

__forceinline static void negateVec3(const Vec3 in, Vec3 out)
{
	out[0] = -in[0];
	out[1] = -in[1];
	out[2] = -in[2];
}

__forceinline static void negateVec4(const Vec4 in, Vec4 out)
{
	out[0] = -in[0];
	out[1] = -in[1];
	out[2] = -in[2];
	out[3] = -in[3];
}

__forceinline static void recipVec3(Vec3 out)
{
	out[0] = 1.0f / out[0];
	out[1] = 1.0f / out[1];
	out[2] = 1.0f / out[2];
}

__forceinline static int roundx87(float a) { //return a;
#if _XBOX
	double b = __fctiw(a);
	return (int)(*(U64*)&b);
#elif defined(_WIN32) && !defined(_WIN64) 
	int i;
	#if !USE_EXCEPTION_SAFE_ROUND
		__asm {
			fld		a
			fistp	i
			fnclex			// Clear the invalid op exception if it happened.
		};
	#else
		// This is the exception-safe version of round.
		// If you have invalid-op exceptions enabled and pass a huge float to round then it
		// will generate an FPU stack leak.
		U16 cw;
		__asm {
			fld		a
			fistp	i
			fnstsw	ax		// Get the exception flags to see if the float was too big to fit.
			fnclex			// Clear the invalid op exception if it happened.
			test	ax, 1	// Check if the invalid op flag is set.
			jz		done	// Not an invalid op.
			fnstcw	cw		// Load the control word register into ax.
			mov		ax, cw	
			test	ax, 1	// Check if the invalid op exception is enabled (0).
			jnz		done	// Invalid op exception is NOT enabled (1).
			or		ax, 1	// Disable the invalid op exception.
			mov		cw, ax
			fldcw	cw
			fistp	i		// Run it again with the exception disabled.
			fnclex			// Clear the invalid op exception.
			and		ax, ~1	// Re-enable the invalid op exception.
			mov		cw, ax
			fldcw	cw
			done:
		}
	#endif
	return i;
#elif defined(_WIN64)
	return (int)(a + (a >= 0.0f ? 0.5f : -0.5f));
#else
	return rint(a); // just hope it's an intrinsic.
#endif
}

__forceinline static int roundTiesUp(float a)
{
#if defined(_WIN32)
	static const __m128 a_mmSignMask = { { -0.0f, 0, 0, 0 } };
	static const float a_fOneHalf = 0.5f;

	__m128 mm_a;
	// load x to a SSE register
	mm_a = _mm_load_ss( &a );
	// add a signed bias (+/- 0.5f) to X
	mm_a = _mm_add_ss( mm_a, 
		// extract sign bit from X, apply to 0.5f bias to make the signed bias
		_mm_or_ps( _mm_load_ss( &a_fOneHalf ), _mm_and_ps( mm_a, a_mmSignMask ) ) );
	// convert back to int
	return _mm_cvtt_ss2si( mm_a );
#else
	return (int)(a + (a >= 0.0f ? 0.5f : -0.5f));
#endif
}

// This rounds with ties (0.5) going to even numbers. The PC (SSE) version is dependent on the SSE unit rounding mode, which
// must be even.
__forceinline static int round(float a)
{
#if _XBOX
	double b = __fctiw(a);
	return (int)(*(U64*)&b);
#elif defined(_WIN32)
	return _mm_cvt_ss2si(_mm_load_ss(&a));
#else
	return rint(a); // just hope it's an intrinsic.
#endif
}

__forceinline static S64 round64(F64 a) { return (S64)(a + (a >= 0.0f ? 0.5f : -0.5f)); }

__forceinline static F32 interpF32( F32 scale, F32 val1, F32 val2 )
{
	return val2 * scale + val1 * (1-scale);
}

__forceinline static U8 interpU8( F32 scale, U8 val1, U8 val2 )
{
	return (U8)round((F32)val2*scale + (F32)val1 * (1.0f-scale));
}

__forceinline static S16 interpS16( F32 scale, S16 val1, S16 val2 )
{
	return (S16)round((F32)val2*scale + (F32)val1 * (1.0f-scale));
}

__forceinline static int interpInt( F32 scale, int val1, int val2 )
{
	return round((F32)val2*scale + (F32)val1 * (1.0f-scale));
}

__forceinline static int interpBilinearInt( F32 alpha, F32 beta, int values[4] )
{
	int lerpa = interpInt( alpha, values[0], values[1] );
	int lerpb = interpInt( alpha, values[3], values[2] );
	return interpInt( beta, lerpa, lerpb ); 
}

__forceinline static U32 interpColor( F32 alpha, U32 color1, U32 color2)
{
	int ar[2] = { (color1 >> 24) & 0xFF, (color2 >> 24) & 0xFF };
	int ag[2] = { (color1 >> 16) & 0xFF, (color2 >> 16) & 0xFF };
	int ab[2] = { (color1 >> 8) & 0xFF, (color2 >> 8) & 0xFF };
	int aa[2] = { (color1 >> 0) & 0xFF, (color2 >> 0) & 0xFF };

	int r = CLAMP( interpInt( alpha, ar[0], ar[1] ), 0, 255 );
	int g = CLAMP( interpInt( alpha, ag[0], ag[1] ), 0, 255 );
	int b = CLAMP( interpInt( alpha, ab[0], ab[1] ), 0, 255 );
	int a = CLAMP( interpInt( alpha, aa[0], aa[1] ), 0, 255 );

	return (r << 24) | (g << 16) | (b << 8) | a;
}

__forceinline static U32 interpBilinearColor( F32 alpha, F32 beta, U32 colors[4] )
{
	int ar[4] = { (colors[0] >> 24) & 0xFF, (colors[1] >> 24) & 0xFF, (colors[2] >> 24) & 0xFF, (colors[3] >> 24) & 0xFF };
	int ag[4] = { (colors[0] >> 16) & 0xFF, (colors[1] >> 16) & 0xFF, (colors[2] >> 16) & 0xFF, (colors[3] >> 16) & 0xFF };
	int ab[4] = { (colors[0] >> 8) & 0xFF, (colors[1] >> 8) & 0xFF, (colors[2] >> 8) & 0xFF, (colors[3] >> 8) & 0xFF };
	int aa[4] = { (colors[0] >> 0) & 0xFF, (colors[1] >> 0) & 0xFF, (colors[2] >> 0) & 0xFF, (colors[3] >> 0) & 0xFF };

	int r = CLAMP( interpBilinearInt( alpha, beta, ar ), 0, 255 );
	int g = CLAMP( interpBilinearInt( alpha, beta, ag ), 0, 255 );
	int b = CLAMP( interpBilinearInt( alpha, beta, ab ), 0, 255 );
	int a = CLAMP( interpBilinearInt( alpha, beta, aa ), 0, 255 );

	return (r << 24) | (g << 16) | (b << 8) | a;
}

__forceinline static S64 interpS64( F32 scale, S64 val1, S64 val2 )
{
	return round64((F64)val2*scale + (F64)val1 * (1.0f-scale));
}

__forceinline static void interpVec2( F32 scale, const Vec2 val1, const Vec2 val2, Vec3 r )
{
	F32 invscale = 1-scale;
	r[0] = val2[0] * scale + val1[0] * invscale;
	r[1] = val2[1] * scale + val1[1] * invscale;
}

__forceinline static void interpVec3( F32 scale, const Vec3 val1, const Vec3 val2, Vec3 r )
{
	F32 invscale = 1-scale;
	r[0] = val2[0] * scale + val1[0] * invscale;
	r[1] = val2[1] * scale + val1[1] * invscale;
	r[2] = val2[2] * scale + val1[2] * invscale;
}

__forceinline static void interpVec4( F32 scale, const Vec4 val1, const Vec4 val2, Vec4 r )
{
	F32 invscale = 1-scale;
	r[0] = val2[0] * scale + val1[0] * invscale;
	r[1] = val2[1] * scale + val1[1] * invscale;
	r[2] = val2[2] * scale + val1[2] * invscale;
	r[3] = val2[3] * scale + val1[3] * invscale;
}

// If you want an interp param (0.0 - 1.0) given the two extents and the result
// The inverse of the interpF32 calc above.
__forceinline static F32 calcInterpParam( F32 r, F32 val1, F32 val2 )
{
	F32 fDenom = (val2 - val1);
	if ( !fDenom )
		return 0.0f;
	return (r - val1) / fDenom;
}


__forceinline static float dotVec4Vec3( const Vec4 p, const Vec3 q )
{
	return (p[0] * q[0] + p[1] * q[1] + p[2] * q[2] + p[3]);
}

__forceinline static int qtrunc(F32 a)
{
	__m128i result = _mm_cvttps_epi32(_mm_load_ss(&a));
	return result.m128i_i32[0];
#if 0
	return a;

int		t,i,sign,exponent,mantissa,x;

	x = *((int *)&a);
	exponent = (127+23) - ((x >> 23)&255);
	t = exponent & (1<<30);						// for exponent > 23 - gives wrong, but large answer
	exponent |= (31 - exponent) >> 31;			// for a < 1
	mantissa = (x & ((1<<23)-1)) + (1 << 23);
	i = (mantissa >> exponent) | t;
	sign = x>>31;
	i = i ^ sign;
	i += sign & 1;

	return i;
#endif
}

extern U32 oldControlState;

#if _XBOX || defined(_WIN64)
	__forceinline static void qtruncVec3(const Vec3 flt, int ilt[3])
	{
		ilt[0] = (int)flt[0];
		ilt[1] = (int)flt[1];
		ilt[2] = (int)flt[2];
	}
#else
	// On a K7 this runs about 50% faster than the integer-only version
	// On a P3 it's about 10% faster
	__forceinline static void qtruncVec3(const Vec3 flt,int ilt[3])
	{
		_controlfp_s(&oldControlState, _RC_CHOP, _MCW_RC);
		_asm{
			mov eax,dword ptr[flt]
			mov ecx,dword ptr[ilt]
			fld dword ptr[eax]
			fistp dword ptr[ecx]
			fld dword ptr[eax+4]
			fistp dword ptr[ecx+4]
			fld dword ptr[eax+8]
			fistp dword ptr[ecx+8]
		}
		_controlfp_s(&oldControlState, FP_DEFAULT_ROUND_MODE, _MCW_RC);
	}
#endif

__forceinline static void qtruncVec3NoFPCWChange(const Vec3 flt, int ilt[3])
{
	ilt[0] = (int)flt[0];
	ilt[1] = (int)flt[1];
	ilt[2] = (int)flt[2];
}

__forceinline static bool nearSameF32(F32 a, F32 b)
{
	return fabs( a - b ) < 0.001f;
}

__forceinline static bool nearSameF32Tol(F32 a, F32 b, F32 tol)
{
	return fabs( a - b ) < tol;
}

__forceinline static bool nearSameDouble(double a, double b)
{
	return fabs( a - b ) < 0.001;
}

__forceinline static bool nearSameDoubleTol(double a, double b, double tol)
{
	return fabs( a - b ) < tol;
}

__forceinline static F64 roundFloatSignificantDigits(F64 n, U32 d)
{
	if(n == 0.f){
		return 0.f;
	}else{
		U32 p = (d>0?d-1:0) - (U32)log10(n);
		U32 r = pow((F64)10, (S32)p);
		return floor(n * r + 0.5f) / r;
	}
}

__forceinline static F64 roundFloatWithPrecision(F64 x, F64 precision)
{
	return round(x / precision) * precision;
}

__forceinline static void roundVec2WithPrecision(Vec2 v, F64 precision)
{
	v[0] = roundFloatWithPrecision(v[0], precision);
	v[1] = roundFloatWithPrecision(v[1], precision);
}

__forceinline static void roundVec3WithPrecision(Vec3 v, F64 precision)
{
	v[0] = roundFloatWithPrecision(v[0], precision);
	v[1] = roundFloatWithPrecision(v[1], precision);
	v[2] = roundFloatWithPrecision(v[2], precision);
}

__forceinline static void roundVec4WithPrecision(Vec4 v, F64 precision)
{
	v[0] = roundFloatWithPrecision(v[0], precision);
	v[1] = roundFloatWithPrecision(v[1], precision);
	v[2] = roundFloatWithPrecision(v[2], precision);
	v[3] = roundFloatWithPrecision(v[3], precision);
}

#if _XBOX && 0
#include "windefinclude.h"
#include <xboxmath.h>
// This is sometimes twice as fast, often 1/4th the speed - probably related to if it needs to move out of vector registers
__forceinline static void sincosf( float angle, float* sinPtr, float* cosPtr )
{
	XMVECTOR vangle;
	XMVECTOR vs, vc;
	vangle.x = angle;
	XMVectorSinCos(&vs, &vc, vangle);
	*sinPtr = vs.x;
	*cosPtr = vc.x;
}
#elif _XBOX || defined(_WIN64)
	__forceinline static void sincosf( float angle, float* sinPtr, float* cosPtr )
	{
		*sinPtr = sinf(angle);
		*cosPtr = cosf(angle);
	}
#else
	__forceinline static void sincosf( float angle, float* sinPtr, float* cosPtr )
	{
		__asm
		{
			fld	angle
				mov	ecx, [cosPtr]
				mov	edx, [sinPtr]
				fsincos
					fstp [ecx]
					fstp [edx]
		}
	}
#endif

void initQuickTrig();
// MAKE SURE THESE MATCH: ENTRIES IS POWER OF 2, MASK is ENTRIES - 1
#define TRIG_TABLE_ENTRIES  4096
#define TRIG_TABLE_MASK 4095
// This is TRIG_TABLE_ENTRIES / 2PI
#define TRIG_TABLE_TRANSFORM 651.89864690440329530934789477382f
// Does a linear interpolation between entries in sintable
__forceinline static F32 qsin(F32 theta)
{
	return sintable[((int)(theta * TRIG_TABLE_TRANSFORM))&TRIG_TABLE_MASK];
}
__forceinline static F32 qcos(F32 theta)
{
	return costable[((int)(theta * TRIG_TABLE_TRANSFORM))&TRIG_TABLE_MASK];
}

// Generated by mkproto
int finiteVec3(const Vec3 y); 
int finiteVec4(const Vec4 y); 
float distance3( const Vec3 a, const Vec3 b );
//float distance3Squared( const Vec3 a, const Vec3 b );
//float distance3XZ( const Vec3 a, const Vec3 b );
float distance3SquaredXZ( const Vec3 a, const Vec3 b );
void getScale(const Mat3 mat, Vec3 scale ); // doesn't change the matrix
void extractScale(Mat3 mat,Vec3 scale); // also normalizes the matrix
void normalMat3(Mat3 mat); // just normalizes the matrix
void setNearSameVec3Tolerance(F32 tol);
int nearSameDVec2(const DVec2 a,const DVec2 b);
//void copyMat4(const Mat4 a,Mat4 b);
void copyMat3(const Mat3 a,Mat3 b);
void transposeMat3(Mat3 uv);
int invertMat3Copy(const Mat3 a, Mat3 b);
S32 invertMat4Copy(const Mat4 mat,Mat4 inv);
void invertMat4ScaledCopy(const Mat4 mat,Mat4 inv);
int invertMat44Copy(const Mat44 m, Mat44 r);
void transposeMat3Copy(const Mat3 uv,Mat3 uv2);
void transposeMat4Copy(const Mat4 mat,Mat4 inv);
void transposeMat44(Mat44 mat);
void transposeMat44Copy(const Mat44 mIn, Mat44 mOut);
F32 mat3Determinant(const Mat3 a);
F32 mat4Determinant(const Mat4 a);
F32 mat44Determinant(const Mat44 a);
F64 dmat3Determinant(const DMat3 a);
F64 dmat4Determinant(const DMat4 a);
F64 dmat44Determinant(const DMat44 a);
void scaleMat44(const Mat44 a,Mat44 b,F32 sfactor);
void scaleMat3(const Mat3 mIn,Mat3 mOut,F32 sfactor);
void scaleMat3Vec3(Mat3 a, const Vec3 sfactor);
void scaleMat3Vec3Xfer(const Mat3 a, const Vec3 sfactor,Mat3 b);
void rotateMat3(const F32 *rpy, Mat3 uvs);
void yawMat3World(F32 angle, Mat3 uv);
void pitchMat3World(F32 angle, Mat3 uv);
void rollMat3World(F32 angle, Mat3 uv);
void yawMat3(F32 angle, Mat3 uv);
void pitchMat3(F32 angle, Mat3 uv);
void rollMat3(F32 angle, Mat3 uv);
void orientMat3(Mat3 mat, const Vec3 dir);
void orientMat3ToNormalAndForward(Mat3 mat, const Vec3 norm, const Vec3 forward);
// void orientPYR(Vec3 pyr, const Vec3 dir); // if you really need to apply rotations in PYR order, talk to conor first
void orientYPR(Vec3 pyr, const Vec3 dir); // Note that the parameters are still pyr not ypr
void orientMat3Yvec(Mat3 mat, const Vec3 dir, const Vec3 yvec);

// If a vector is multiplied by mOut, m2 will be applied first.
void mulMat4(const Mat4 m1,const Mat4 m2,Mat4 mOut);
void mulMat3(const Mat3 m1,const Mat3 m2,Mat3 mOut);
void mulVecMat3Transpose(const Vec3 vIn,const Mat3 mIn,Vec3 vOut);
//void mulVecMat3(const Vec3 vIn,const Mat3 mIn,Vec3 vOut);
//void mulVecMat4(const Vec3 vIn,const Mat4 mIn,Vec3 vOut);
void mulVecMat4Transpose(const Vec3 vIn, const Mat4 mIn, Vec3 vOut);
F32 normalVec3XZ(Vec3 v);
double normalDVec3(DVec3 v);
F32 normalVec2(Vec2 v);
double normalDVec2(DVec2 v);
F32 normalVec4(Vec4 v);
void getVec3YP(const Vec3 dvec,F32 *yawp,F32 *pitp);
void getVec3PY(const Vec3 dvec,F32 *pitp,F32 *yawp);

// Gets the pitch in respect to the XZ plane. a pitch of (PI/2) is straight up 
__forceinline static F32 getVec3Pitch(const Vec3 v)
{
	F32	dist = fsqrt(SQR(v[0]) + SQR(v[2]));
	return (F32)fatan2(v[1],dist);
}

__forceinline static void setVec3FromYaw(Vec3 v, F32 yaw)
{
	setVec3(v, sinf(yaw), 0, cosf(yaw));
}

__forceinline static F32 getVec2Yaw(Vec2 v)
{
	return fatan2(v[1], v[0]);
}

__forceinline static void setVec2FromYaw(Vec2 v, F32 yaw)
{
	setVec2(v, cosf(yaw), sinf(yaw));
}

F32 fixAngle(F32 a);
F32 subAngle(F32 a, F32 b);
F32 addAngle(F32 a, F32 b);
F32 fixAngleDeg(F32 a);
F32 subAngleDeg(F32 a, F32 b);
F32 addAngleDeg(F32 a, F32 b);
__forceinline static void rotateXZ( F32 angle, F32 *x, F32 *y )
{
	F32 c, s;
	F32 tx = *x, ty = *y;
	sincosf(angle, &s, &c);

	*x = tx*c - ty*s;
	*y = ty*c + tx*s;
}
void createScaleMat(Mat3 mat, const Vec3 scale);
void createScaleTranslateFitMat(Mat44 output, const Vec3 vMin, const Vec3 vMax, F32 xMinOut, F32 xMaxOut, F32 yMinOut, F32 yMaxOut, F32 zMinOut, F32 zMaxOut);
// void createMat3PYR(Mat3 mat,const F32 *pyr); // if you really need to apply rotations in PYR order, talk to conor first
// void createMat3RYP(Mat3 mat,const F32 *pyr); // if you really need to apply rotations in PYR order, talk to conor first
void createMat3YPR(Mat3 mat,const Vec3 pyr); // Note that the parameters are still pyr not ypr
void createMat3DegYPR(Mat3 mat, const Vec3 degPYR);
void createMat3YP(Mat3 mat,const Vec2 py); // Note that the parameters are still py not yp
void createMat3_0_YPR(Vec3 mat0,const Vec3 pyr);
void createMat3_1_YPR(Vec3 mat1,const Vec3 pyr);
void createMat3_2_YP(Vec3 mat2,const Vec2 py);
// void getMat3PYR(const Mat3 mat,F32 *pyr); // if you really need to apply rotations in PYR order, talk to conor first
void getMat3YPR(const Mat3 mat,F32 *pyr); // Note that the parameters are still pyr not ypr
//void mat43to44(const Mat4 in,Mat44 out);

F16 F32toF16(F32 f);
F32 F16toF32(F16 y);
void F32toF16Vector(F16 *dst, F32 *src, int count);
const extern F16 U8toF16[256];

#if PLATFORM_CONSOLE
#define MakePacked_11_11_10(x,y,z) ((x) + ((y)<<11) + ((z)<<22))
// Macro slightly faster in Debug, way faster in Full Debug
// __forceinline static Vec3_Packed Vec3toPacked(const Vec3 vec) {
// 	int x = vec[0] * 1023;
// 	int y = vec[1] * 1023;
// 	int z = vec[2] * 511;
// 	x = CLAMP(x, -1023, 1023) & 0x7FF;
// 	y = CLAMP(y, -1023, 1023) & 0x7FF;
// 	z = CLAMP(z, -511, 511) & 0x3FF;
// 	return MakePacked_11_11_10(x, y, z);
// }
#define Vec3toPackedInternal(x, y, z) MakePacked_11_11_10(\
    ((S32)(CLAMPF((x), -1, 1) * 1023.f)) & 0x7FF,\
    ((S32)(CLAMPF((y), -1, 1) * 1023.f)) & 0x7FF,\
    ((S32)(CLAMPF((z), -1, 1) * 511.f)) & 0x3FF)
#else
#define MakePacked_16_16_16(x,y,z) ((U64)((x) & 0xFFFF) + (((U64)((y) & 0xFFFF))<<16LL) + (((U64)((z) & 0xFFFF))<<32LL))
#define Vec3toPackedInternal(x, y, z) MakePacked_16_16_16(((S16)(CLAMP((x), -1, 1) * 32767.f)), ((S16)(CLAMP((y), -1, 1) * 32767.f)), ((S16)(CLAMP((z), -1, 1) * 32767.f)))
#endif

#define Vec3toPacked(vec) Vec3toPackedInternal((vec)[0], (vec)[1], (vec)[2])
#define setVec3Packed(vec, x, y, z) ((vec) = Vec3toPackedInternal(x, y, z))
#define copyVec3Packed(src, dest) ((dest) = Vec3toPacked(src))

void getRandomPointOnLimitedSphereSurface(F32* theta, F32* phi, F32 phiDeflectionFromNorthPole);
void getRandomPointOnSphereSurface(F32* theta, F32* phi );
void getRandomAngularDeflection(Vec3 vOutput, F32 fAngle);
void getQuickRandomPointOnCylinder(const Vec3 start_pos, F32 radius, F32 height, Vec3 final_pos );
void getRandomPointOnLine(const Vec3 vStart, const Vec3 vEnd, Vec3 vResult);
int getCappedRandom(int max);
void rule30Seed(U32 c);
U32 rule30Rand();
U32 verySlowRandomNumber();

#define safe_ftoa(f, buf) safe_ftoa_s(f, SAFESTR(buf))
char *safe_ftoa_s(F32 f, char *buf, size_t buf_size);

// Note: phi of 0 is straight up
void sphericalCoordsToVec3(Vec3 vOut, F32 theta, F32 phi, F32 radius);

void posInterp(F32 t, const Vec3 T0, const Vec3 T1, Vec3 T);
bool planeIntersect(const Vec3 start,const Vec3 end,const Mat4 pmat,Vec3 cpos);
F32	pointPlaneDist(const Vec3 v,const Vec3 n,const Vec3 pt);
bool triangleLineIntersect(const Vec3 orig, const Vec3 end, const Vec3 vert0, const Vec3 vert1, const Vec3 vert2, Vec3 collision_point);
bool triangleLineIntersect2(const Vec3 line_p0, const Vec3 line_p1, const Vec3 tri_verts[3], Vec3 intersection);
void closestPointOnTriangle( const Vec3 v0,const Vec3 v1,const Vec3 v2, const Vec3 vPos, Vec3 vOut );
void camLookAt(const Vec3 lookdir, Mat4 mat);
void mat3FromUpVector(const Vec3 upVec, Mat3 mat);
void mat3FromFwdVector(const Vec3 fwdVec, Mat3 mat);

F32 interpAngle(  F32 scale_a_to_b, F32 a, F32 b );
void interpPYR( F32 scale_a_to_b, const Vec3 a, const Vec3 b, Vec3 result );
void interpPY( F32 scale_a_to_b, const Vec2 a, const Vec2 b, Vec2 result );
void circleDeltaVec3( const Vec3 org, const Vec3 dest, Vec3 delta );
F32	circleDelta( F32 org, F32 dest );
F32 quick_fsqrt( F32 x );
float graphInterp(float x, const Vec3* interpPoints);	

int sphereLineCollision( F32 sphere_radius, const Vec3 sphere_mid, const Vec3 line_start, const Vec3 line_end );
bool sphereLineCollisionWithHitPoint(const Vec3 start, const Vec3 end, const Vec3 mid, F32 radius, Vec3 hit);

bool cylinderLineCollisionWithHitPoint(const Vec3 linePos, const Vec3 lineDir, F32 lineLen, const Vec3 cylPos, const Vec3 cylDir, F32 cylLen, F32 radius, Vec3 hit);

// simple axis aligned collision tests
bool lineBoxCollision(const Vec3 start, const Vec3 end, const Vec3 min, const Vec3 max, Vec3 intersect);
bool lineOrientedBoxCollision( const Vec3 start, const Vec3 end, const Mat4 local_to_world_mat, const Mat4 world_to_local_mat, const Vec3 local_min, const Vec3 local_max, Vec3 intersect);
bool lineBoxCollisionHollow(const Vec3 start, const Vec3 end, const Vec3 min, const Vec3 max, Vec3 intersect, VolumeFaces face_bits);
bool boxSphereCollision(const Vec3 min1, const Vec3 max1, const Vec3 mid2, F32 radius2);
F32 distanceToBoxSquared(const Vec3 boxmin, const Vec3 boxmax, const Vec3 point);

__forceinline static bool pointBoxCollision(const Vec3 point, const Vec3 min, const Vec3 max)
{
	if (point[0] < min[0] || point[0] > max[0])
		return false;
	if (point[1] < min[1] || point[1] > max[1])
		return false;
	if (point[2] < min[2] || point[2] > max[2])
		return false;
	return true;
}

__forceinline static bool pointBoxCollisionXZ(const Vec3 point, const Vec3 min, const Vec3 max)
{
	if (point[0] < min[0] || point[0] > max[0])
		return false;
	if (point[2] < min[2] || point[2] > max[2])
		return false;
	return true;
}

__forceinline static bool boxBoxCollision(const Vec3 min1, const Vec3 max1, const Vec3 min2, const Vec3 max2)
{
	if (max1[0] < min2[0] || max2[0] < min1[0])
		return false;
	if (max1[1] < min2[1] || max2[1] < min1[1])
		return false;
	if (max1[2] < min2[2] || max2[2] < min1[2])
		return false;
	return true;
}


// If you do not provide the inverse matrices, they will be calculated for you.
bool sphereOrientBoxCollision(const Vec3 point, F32 radius, 
							  const Vec3 min, const Vec3 max, const Mat4 mat, const Mat4 inv_mat);

typedef struct OrientedBoundingBox
{
	Mat3 m;
	Vec3 vPos;
	Vec3 vHalfSize;
} OrientedBoundingBox;

//bool orientBoxBoxCollision(const Vec3 min1, const Vec3 max1, const Mat4 mat1, const Mat4 inv_mat1,
	//					   const Vec3 min2, const Vec3 max2, const Mat4 mat2, const Mat4 inv_mat2);

bool orientBoxBoxCollision(const Vec3 min1, const Vec3 max1, const Mat4 mat1,
						   const Vec3 min2, const Vec3 max2, const Mat4 mat2);

// collides an OBB with a single bounded plane quad.  The quad matrix is assumed to be identity and the z value of min2 and max2 is assumed to be the same
bool orientBoxRectCollision(const Vec3 min1, const Vec3 max1, const Mat4 mat1,
						   const Vec3 min2, const Vec3 max2);

// This should be generally faster than orientBoxBoxCollision, but is not yet thoroughly tested
bool orientBoxBoxCollisionFast(const OrientedBoundingBox* box1, const OrientedBoundingBox* box2);

// Determines if box 1 completely encloses box 2
bool orientBoxBoxContained(const Vec3 min1, const Vec3 max1, const Mat4 mat1, const Mat4 inv_mat1,
						   const Vec3 min2, const Vec3 max2, const Mat4 mat2, const Mat4 inv_mat2);


int randInt(int max);
int randIntGivenSeed(int * seed);
int randUIntGivenSeed(int * seed);

//* Quick random numbers of various sorts*/

extern F32 floatseed;
extern long holdrand;

/* seed the random numberator */
__forceinline static void qsrand ( int seed )
{
    holdrand = (long)seed;
}

/*quick unsigned int between 0 and ~32000 (same as rand()) */
__forceinline static int qrand()
{
	return(((holdrand = holdrand * 214013L + 2531011L) >> 16) & 0x7fff);
}

/*quick signed rand int*/
__forceinline static int qsirand()
{
	return(holdrand = (holdrand * 214013L + 2531011L));
}

/* quick F32 between -1.0 and 1.0*/// 
__forceinline static F32 qfrand()
{
	return (F32)(((F32)(holdrand = (holdrand * 214013L + 2531011L))) * ((F32)(1.f/(F32)(0x7fffffff))));
}

/* quick F32 between -1.0 and 1.0*/// 
__forceinline static F32 qfrandFromSeed( int * seed )
{
	return (F32)(((F32)((*seed) = ((*seed) * 214013L + 2531011L))) * ((F32)(1.f/(F32)(0x7fffffff))));
}

extern U32 rule30Float_c;
// returns between -1.0 and 1.0
__forceinline static F32 rule30Float()
{
      U32 l,r;
      l = r = rule30Float_c;
      l = _lrotr(l, 1);/* bit i of l equals bit just left of bit i in c */
      r = _lrotl(r, 1);/* bit i of r euqals bit just right of bit i in c */
      rule30Float_c |= r;
      rule30Float_c ^= l;           /* c = l xor (c or r), aka rule 30, named by wolfram */
      return -1.f + (rule30Float_c * 1.f / (F32)0x7fffffffUL);
}

// returns between 0.0 and 99.999999
__forceinline static F32 rule30FloatPct()
{
	F32 r = rule30Float();
	if (r < 0.0) r *= -1;
	return r * 99.99999f;
}

/* quick F32 between -1.0 and 1.0 
__forceinline static F32 qfrand()
{
	int s;
	s = (0x3f800000 | ( 0x007fffff & qsirand() ) );
	return ( (1.5 - (*((F32 * )&s))) * 2.0 ) ;
}*/

/* quick F32 between 0 and 1 TO DO re__forceinline */
/* quick F32 between 0 and 1 */
__forceinline static F32 qufrand()
{
	int s;
	s = (0x3f800000 | ( 0x007fffff & qsirand() ) );
	return ( (*((F32 * )&s))) - 1 ;
}

	
/* quick true or false *///
__forceinline static int qbrand()
{
	return qsirand() & 1;
}

/* quick 1 or -1 *///
__forceinline static int qnegrand()
{
	return (qsirand() >> 31)|1;
}

__forceinline static bool isPower2(unsigned int x)
{
	if ( x < 1 )
		return false;

	if ( x & (x-1) )
		return false;

	return true;
}

int log2_floor(int val);

__forceinline static U32 highBitIndex(U32 value)
{
	unsigned long index;
	if (_BitScanReverse(&index, value))
	{
		return index;
	}
	return -1;
}

__forceinline static U32 lowBitIndex(U32 value)
{
	U32 bits = sizeof(U32) * 8 / 2;
	U32 bitMask = (1 << bits) - 1;
	U32 count = 0;
	
	if(!value){
		return 32;
	}
	
	while(bits)
	{
		if(!(value & bitMask))
		{
			value >>= bits;
			count += bits;
		}
		
		bits >>= 1;
		bitMask >>= bits;
	}

	return count;
}

// optimized bit counting for sparse bits
__forceinline static U32 countBitsSparse(U32 value)
{
	int count = 0;
	while (value)
	{
		++count;
		value &= (value - 1);
	}
	return count;
}

__forceinline int countBitsFast(int i)
{
    i = i - ((i >> 1) & 0x55555555);
    i = (i & 0x33333333) + ((i >> 2) & 0x33333333);
    return ((i + (i >> 4) & 0xF0F0F0F) * 0x1010101) >> 24;
}

void interpMat3(F32 t, const Mat3 a, const Mat3 b, Mat3 r);
void interpMat4(F32 t, const Mat4 a, const Mat4 b, Mat4 r);

__forceinline static void mat43to44(const Mat4 in,Mat44 out)
{
	copyVec3(in[0],out[0]);
	copyVec3(in[1],out[1]);
	copyVec3(in[2],out[2]);
	copyVec3(in[3],out[3]);
	out[0][3] = out[1][3] = out[2][3] = 0;
	out[3][3] = 1;
}

__forceinline static void mat44to43(const Mat44 in,Mat4 out)
{
	copyVec3(in[0],out[0]);
	copyVec3(in[1],out[1]);
	copyVec3(in[2],out[2]);
	copyVec3(in[3],out[3]);
}

__forceinline static void mat43to34(const Mat4 in, Mat34 out)
{
	setVec4(out[0], in[0][0], in[1][0], in[2][0], in[3][0]);
	setVec4(out[1], in[0][1], in[1][1], in[2][1], in[3][1]);
	setVec4(out[2], in[0][2], in[1][2], in[2][2], in[3][2]);
}

__forceinline static void mat34to43(const Mat34 in, Mat4 out)
{
	setVec3(out[0], in[0][0], in[1][0], in[2][0]);
	setVec3(out[1], in[0][1], in[1][1], in[2][1]);
	setVec3(out[2], in[0][2], in[1][2], in[2][2]);
	setVec3(out[3], in[0][3], in[1][3], in[2][3]);
}

__forceinline static void mat4toSkinningMat4(const Mat4 in, SkinningMat4 out)
{
	mat43to34(in, out);
}

__forceinline static void skinningMat4toMat4(const SkinningMat4 in, Mat4 out)
{
	mat34to43(in, out);
}

// This function transposes the left 3-by-3 submatrix of the 3-by-4 matrix.
__forceinline static void transposeMat34_33(Mat34 mat)
{
	F32 temp = mat[1][0];
	mat[1][0] = mat[0][1];
	mat[0][1] = temp;

	temp = mat[2][0];
	mat[2][0] = mat[0][2];
	mat[0][2] = temp;

	temp = mat[2][1];
	mat[2][1] = mat[1][2];
	mat[1][2] = temp;
}

// This function transposes the upper-left 3-by-3 submatrix of the 4-by-4 matrix.
__forceinline static void transposeMat44_33(Mat44 mat)
{
	transposeMat34_33(mat);
}

void transposeMat44Copy(const Mat44 mIn, Mat44 mOut);


__forceinline static void mulMat4Inline(const Mat4 m1,const Mat4 m2,Mat4 mOut)
{
#ifdef _FULLDEBUG
	assert(m1!=mOut && m2!=mOut);
#endif
	// This is the fastest way to do it in C for x86
	(mOut)[0][0] = (m2)[0][0] * (m1)[0][0] + (m2)[0][1] * (m1)[1][0] + (m2)[0][2] * (m1)[2][0];
	(mOut)[0][1] = (m2)[0][0] * (m1)[0][1] + (m2)[0][1] * (m1)[1][1] + (m2)[0][2] * (m1)[2][1];
	(mOut)[0][2] = (m2)[0][0] * (m1)[0][2] + (m2)[0][1] * (m1)[1][2] + (m2)[0][2] * (m1)[2][2];
	(mOut)[1][0] = (m2)[1][0] * (m1)[0][0] + (m2)[1][1] * (m1)[1][0] + (m2)[1][2] * (m1)[2][0];
	(mOut)[1][1] = (m2)[1][0] * (m1)[0][1] + (m2)[1][1] * (m1)[1][1] + (m2)[1][2] * (m1)[2][1];
	(mOut)[1][2] = (m2)[1][0] * (m1)[0][2] + (m2)[1][1] * (m1)[1][2] + (m2)[1][2] * (m1)[2][2];
	(mOut)[2][0] = (m2)[2][0] * (m1)[0][0] + (m2)[2][1] * (m1)[1][0] + (m2)[2][2] * (m1)[2][0];
	(mOut)[2][1] = (m2)[2][0] * (m1)[0][1] + (m2)[2][1] * (m1)[1][1] + (m2)[2][2] * (m1)[2][1];
	(mOut)[2][2] = (m2)[2][0] * (m1)[0][2] + (m2)[2][1] * (m1)[1][2] + (m2)[2][2] * (m1)[2][2];
	(mOut)[3][0] = (m2)[3][0] * (m1)[0][0] + (m2)[3][1] * (m1)[1][0] + (m2)[3][2] * (m1)[2][0] + (m1)[3][0];
	(mOut)[3][1] = (m2)[3][0] * (m1)[0][1] + (m2)[3][1] * (m1)[1][1] + (m2)[3][2] * (m1)[2][1] + (m1)[3][1];
	(mOut)[3][2] = (m2)[3][0] * (m1)[0][2] + (m2)[3][1] * (m1)[1][2] + (m2)[3][2] * (m1)[2][2] + (m1)[3][2];
}

__forceinline static void mulMat44Inline(const Mat44 m1,const Mat44 m2,Mat44 mOut)
{
#ifdef _FULLDEBUG
	assert(m1!=mOut && m2!=mOut);
#endif
	(mOut)[0][0] = (m2)[0][0] * (m1)[0][0] + (m2)[0][1] * (m1)[1][0] + (m2)[0][2] * (m1)[2][0] + (m2)[0][3] * (m1)[3][0];
	(mOut)[0][1] = (m2)[0][0] * (m1)[0][1] + (m2)[0][1] * (m1)[1][1] + (m2)[0][2] * (m1)[2][1] + (m2)[0][3] * (m1)[3][1];
	(mOut)[0][2] = (m2)[0][0] * (m1)[0][2] + (m2)[0][1] * (m1)[1][2] + (m2)[0][2] * (m1)[2][2] + (m2)[0][3] * (m1)[3][2];
	(mOut)[0][3] = (m2)[0][0] * (m1)[0][3] + (m2)[0][1] * (m1)[1][3] + (m2)[0][2] * (m1)[2][3] + (m2)[0][3] * (m1)[3][3];

	(mOut)[1][0] = (m2)[1][0] * (m1)[0][0] + (m2)[1][1] * (m1)[1][0] + (m2)[1][2] * (m1)[2][0] + (m2)[1][3] * (m1)[3][0];
	(mOut)[1][1] = (m2)[1][0] * (m1)[0][1] + (m2)[1][1] * (m1)[1][1] + (m2)[1][2] * (m1)[2][1] + (m2)[1][3] * (m1)[3][1];
	(mOut)[1][2] = (m2)[1][0] * (m1)[0][2] + (m2)[1][1] * (m1)[1][2] + (m2)[1][2] * (m1)[2][2] + (m2)[1][3] * (m1)[3][2];
	(mOut)[1][3] = (m2)[1][0] * (m1)[0][3] + (m2)[1][1] * (m1)[1][3] + (m2)[1][2] * (m1)[2][3] + (m2)[1][3] * (m1)[3][3];

	(mOut)[2][0] = (m2)[2][0] * (m1)[0][0] + (m2)[2][1] * (m1)[1][0] + (m2)[2][2] * (m1)[2][0] + (m2)[2][3] * (m1)[3][0];
	(mOut)[2][1] = (m2)[2][0] * (m1)[0][1] + (m2)[2][1] * (m1)[1][1] + (m2)[2][2] * (m1)[2][1] + (m2)[2][3] * (m1)[3][1];
	(mOut)[2][2] = (m2)[2][0] * (m1)[0][2] + (m2)[2][1] * (m1)[1][2] + (m2)[2][2] * (m1)[2][2] + (m2)[2][3] * (m1)[3][2];
	(mOut)[2][3] = (m2)[2][0] * (m1)[0][3] + (m2)[2][1] * (m1)[1][3] + (m2)[2][2] * (m1)[2][3] + (m2)[2][3] * (m1)[3][3];

	(mOut)[3][0] = (m2)[3][0] * (m1)[0][0] + (m2)[3][1] * (m1)[1][0] + (m2)[3][2] * (m1)[2][0] + (m2)[3][3] * (m1)[3][0];
	(mOut)[3][1] = (m2)[3][0] * (m1)[0][1] + (m2)[3][1] * (m1)[1][1] + (m2)[3][2] * (m1)[2][1] + (m2)[3][3] * (m1)[3][1];
	(mOut)[3][2] = (m2)[3][0] * (m1)[0][2] + (m2)[3][1] * (m1)[1][2] + (m2)[3][2] * (m1)[2][2] + (m2)[3][3] * (m1)[3][2];
	(mOut)[3][3] = (m2)[3][0] * (m1)[0][3] + (m2)[3][1] * (m1)[1][3] + (m2)[3][2] * (m1)[2][3] + (m2)[3][3] * (m1)[3][3];
}

__forceinline static void mulMat3Inline(const Mat3 m1,const Mat3 m2,Mat3 mOut)
{
#ifdef _FULLDEBUG
	assert(m1!=mOut && m2!=mOut);
#endif
	(mOut)[0][0] = (m2)[0][0] * (m1)[0][0] + (m2)[0][1] * (m1)[1][0] + (m2)[0][2] * (m1)[2][0];
	(mOut)[0][1] = (m2)[0][0] * (m1)[0][1] + (m2)[0][1] * (m1)[1][1] + (m2)[0][2] * (m1)[2][1];
	(mOut)[0][2] = (m2)[0][0] * (m1)[0][2] + (m2)[0][1] * (m1)[1][2] + (m2)[0][2] * (m1)[2][2];
	(mOut)[1][0] = (m2)[1][0] * (m1)[0][0] + (m2)[1][1] * (m1)[1][0] + (m2)[1][2] * (m1)[2][0];
	(mOut)[1][1] = (m2)[1][0] * (m1)[0][1] + (m2)[1][1] * (m1)[1][1] + (m2)[1][2] * (m1)[2][1];
	(mOut)[1][2] = (m2)[1][0] * (m1)[0][2] + (m2)[1][1] * (m1)[1][2] + (m2)[1][2] * (m1)[2][2];
	(mOut)[2][0] = (m2)[2][0] * (m1)[0][0] + (m2)[2][1] * (m1)[1][0] + (m2)[2][2] * (m1)[2][0];
	(mOut)[2][1] = (m2)[2][0] * (m1)[0][1] + (m2)[2][1] * (m1)[1][1] + (m2)[2][2] * (m1)[2][1];
	(mOut)[2][2] = (m2)[2][0] * (m1)[0][2] + (m2)[2][1] * (m1)[1][2] + (m2)[2][2] * (m1)[2][2];
}

// Multiply a vector times a 3*3 matrix into another vector
__forceinline static void mulVecMat3(const Vec3 vIn,const Mat3 mIn,Vec3 vOut)
{
#ifdef _FULLDEBUG
	assert(vIn!=vOut);
#endif
	vOut[0] = vIn[0]*mIn[0][0] + vIn[1]*mIn[1][0] + vIn[2]*mIn[2][0];
	vOut[1] = vIn[0]*mIn[0][1] + vIn[1]*mIn[1][1] + vIn[2]*mIn[2][1];
	vOut[2] = vIn[0]*mIn[0][2] + vIn[1]*mIn[1][2] + vIn[2]*mIn[2][2];
}

// Multiply a vector times a 3*3 matrix into another vector
__forceinline static void mulVecXZMat3(const Vec3 vIn,const Mat3 mIn,Vec3 vOut)
{
#ifdef _FULLDEBUG
	assert(vIn!=vOut);
#endif
	vOut[0] = vIn[0]*mIn[0][0] + vIn[2]*mIn[2][0];
	vOut[1] = vIn[0]*mIn[0][1] + vIn[2]*mIn[2][1];
	vOut[2] = vIn[0]*mIn[0][2] + vIn[2]*mIn[2][2];
}

// Multiply a vector times a 3*3 + position matrix into another vector
__forceinline static void mulVecMat4(const Vec3 vIn,const Mat4 mIn,Vec3 vOut)
{
#ifdef _FULLDEBUG
	assert(vIn!=vOut);
#endif
	vOut[0] = vIn[0]*mIn[0][0]+vIn[1]*mIn[1][0]+vIn[2]*mIn[2][0] + mIn[3][0];
	vOut[1] = vIn[0]*mIn[0][1]+vIn[1]*mIn[1][1]+vIn[2]*mIn[2][1] + mIn[3][1];
	vOut[2] = vIn[0]*mIn[0][2]+vIn[1]*mIn[1][2]+vIn[2]*mIn[2][2] + mIn[3][2];
}

// Multiply a vector times a 3x4 row-major matrix (3x3 + position matrix) into another vector
__forceinline static void mulVecMat34(const Vec3 vIn,const Mat34 mIn,Vec3 vOut)
{
#ifdef _FULLDEBUG
	assert(vIn!=vOut);
#endif
	vOut[0] = vIn[0]*mIn[0][0]+vIn[1]*mIn[0][1]+vIn[2]*mIn[0][2] + mIn[0][3];
	vOut[1] = vIn[0]*mIn[1][0]+vIn[1]*mIn[1][1]+vIn[2]*mIn[1][2] + mIn[1][3];
	vOut[2] = vIn[0]*mIn[2][0]+vIn[1]*mIn[2][1]+vIn[2]*mIn[2][2] + mIn[2][3];
}

// This function multiplies a 3-vector with an implied zero w component by
// a 3x4 row-major matrix.
__forceinline static void mulVecW0Mat34(const Vec3 vIn,const Mat34 mIn,Vec3 vOut)
{
#ifdef _FULLDEBUG
	assert(vIn!=vOut);
#endif
	vOut[0] = vIn[0]*mIn[0][0]+vIn[1]*mIn[0][1]+vIn[2]*mIn[0][2];
	vOut[1] = vIn[0]*mIn[1][0]+vIn[1]*mIn[1][1]+vIn[2]*mIn[1][2];
	vOut[2] = vIn[0]*mIn[2][0]+vIn[1]*mIn[2][1]+vIn[2]*mIn[2][2];
}

__forceinline static void mulVecMat44(const Vec3 vIn,const Mat44 mIn,Vec4 vOut)
{
#ifdef _FULLDEBUG
	assert(vIn!=vOut);
#endif
	vOut[0] = vIn[0]*mIn[0][0]+vIn[1]*mIn[1][0]+vIn[2]*mIn[2][0] + mIn[3][0];
	vOut[1] = vIn[0]*mIn[0][1]+vIn[1]*mIn[1][1]+vIn[2]*mIn[2][1] + mIn[3][1];
	vOut[2] = vIn[0]*mIn[0][2]+vIn[1]*mIn[1][2]+vIn[2]*mIn[2][2] + mIn[3][2];
	vOut[3] = vIn[0]*mIn[0][3]+vIn[1]*mIn[1][3]+vIn[2]*mIn[2][3] + mIn[3][3];
}

__forceinline static void mulVec4Mat44(const Vec4 vIn,const Mat44 mIn,Vec4 vOut)
{
#ifdef _FULLDEBUG
	assert(vIn!=vOut);
#endif
	vOut[0] = vIn[0]*mIn[0][0]+vIn[1]*mIn[1][0]+vIn[2]*mIn[2][0] + vIn[3]*mIn[3][0];
	vOut[1] = vIn[0]*mIn[0][1]+vIn[1]*mIn[1][1]+vIn[2]*mIn[2][1] + vIn[3]*mIn[3][1];
	vOut[2] = vIn[0]*mIn[0][2]+vIn[1]*mIn[1][2]+vIn[2]*mIn[2][2] + vIn[3]*mIn[3][2];
	vOut[3] = vIn[0]*mIn[0][3]+vIn[1]*mIn[1][3]+vIn[2]*mIn[2][3] + vIn[3]*mIn[3][3];
}

// Multiplies, assumes w = 1, also does perspective divide
__forceinline static void mulVec3ProjMat44(const Vec3 vIn,const Mat44 mIn,Vec3 vOut)
{
	F32 denom = (vIn[0]*mIn[0][3]+vIn[1]*mIn[1][3]+vIn[2]*mIn[2][3] + mIn[3][3]);
	if(denom == 0.f){
		setVec3(vOut, 0, 0, 0);
	}else{
		F32 scale = 1.f/denom;
		#ifdef _FULLDEBUG
			assert(vIn!=vOut);
		#endif
		vOut[0] = (vIn[0]*mIn[0][0]+vIn[1]*mIn[1][0]+vIn[2]*mIn[2][0] + mIn[3][0]) * scale;
		vOut[1] = (vIn[0]*mIn[0][1]+vIn[1]*mIn[1][1]+vIn[2]*mIn[2][1] + mIn[3][1]) * scale;
		vOut[2] = (vIn[0]*mIn[0][2]+vIn[1]*mIn[1][2]+vIn[2]*mIn[2][2] + mIn[3][2]) * scale;
	}
}

// This function multiplies a 3-vector with an implied zero w component by
// a 4x4 matrix.
__forceinline static void mulVec3W0Mat44(const Vec3 vIn, const Mat44 mIn, Vec4 vOut)
{
#ifdef _FULLDEBUG
	assert(vIn!=vOut);
#endif
	vOut[0] = vIn[0]*mIn[0][0]+vIn[1]*mIn[1][0]+vIn[2]*mIn[2][0];
	vOut[1] = vIn[0]*mIn[0][1]+vIn[1]*mIn[1][1]+vIn[2]*mIn[2][1];
	vOut[2] = vIn[0]*mIn[0][2]+vIn[1]*mIn[1][2]+vIn[2]*mIn[2][2];
	vOut[3] = vIn[0]*mIn[0][3]+vIn[1]*mIn[1][3]+vIn[2]*mIn[2][3];
}

// mOut (M44) = lhs(M44) * rhs(M4)
__forceinline static void mulMat44Mat4(const Mat44 lhs, const Mat4 rhs, Mat44 mOut)
{
#ifdef _FULLDEBUG
	assert(lhs!=mOut);
#endif
	mulVec3W0Mat44(rhs[0], lhs, mOut[0]);
	mulVec3W0Mat44(rhs[1], lhs, mOut[1]);
	mulVec3W0Mat44(rhs[2], lhs, mOut[2]);
	mulVecMat44(rhs[3], lhs, mOut[3]);
}

__forceinline static void copyMat4(const Mat4 mIn,Mat4 mOut)
{
	copyVec3((mIn)[0],(mOut)[0]);
	copyVec3((mIn)[1],(mOut)[1]);
	copyVec3((mIn)[2],(mOut)[2]);
	copyVec3((mIn)[3],(mOut)[3]);
}

__forceinline static void copyMat34(const Mat34 mIn,Mat34 mOut)
{
	copyVec4((mIn)[0],(mOut)[0]);
	copyVec4((mIn)[1],(mOut)[1]);
	copyVec4((mIn)[2],(mOut)[2]);
}

__forceinline static void copySkinningMat4(const SkinningMat4 in, SkinningMat4 out)
{
	copyMat34(in, out);
}

__forceinline static void copyMat44(const Mat44 mIn,Mat44 mOut)
{
	copyVec4((mIn)[0],(mOut)[0]);
	copyVec4((mIn)[1],(mOut)[1]);
	copyVec4((mIn)[2],(mOut)[2]);
	copyVec4((mIn)[3],(mOut)[3]);
}

__forceinline static void mat3FromAxisAngle(Mat3 matrix, const Vec3 axis, F32 angle)
{
	F32 s, c;

	sincosf(angle, &s, &c);
	c = 1.f - c;

	setVec3(matrix[0],	1 + c * (axis[0] * axis[0] - 1),
						-axis[2] * s + c * axis[0] * axis[1],
						axis[1] * s + c * axis[0] * axis[2]);

	setVec3(matrix[1],	axis[2] * s + c * axis[0] * axis[1],
						1 + c * (axis[1] * axis[1] - 1),
						-axis[0] * s + c * axis[1] * axis[2]);

	setVec3(matrix[2],	-axis[1] * s + c * axis[0] * axis[2],
						axis[0] * s + c * axis[1] * axis[2],
						1 + c * (axis[2] * axis[2] - 1));
}

__forceinline static float distance2Squared(const Vec2 a, const Vec2 b)
{
	return SQR(a[0] - b[0]) + SQR(a[1] - b[1]);
}

__forceinline static float distance3Squared( const Vec3 a, const Vec3 b )
{
	return SQR(a[0] - b[0]) + SQR(a[1] - b[1]) + SQR(a[2] - b[2]);
}

__forceinline static float distance3XZ( const Vec3 a, const Vec3 b )
{
	return (F32)sqrt(SQR(a[0] - b[0]) + SQR(a[2] - b[2]));
}

__forceinline static float distance3XZSquared( const Vec3 a, const Vec3 b )
{
	return SQR(a[0] - b[0]) + SQR(a[2] - b[2]);
}

__forceinline static void vec3MinMax(const Vec3 a, const Vec3 b, Vec3 min, Vec3 max)
{
	if (a[0] < b[0])
	{
		min[0] = a[0];
		max[0] = b[0];
	}
	else
	{
		min[0] = b[0];
		max[0] = a[0];
	}

	if (a[1] < b[1])
	{
		min[1] = a[1];
		max[1] = b[1];
	}
	else
	{
		min[1] = b[1];
		max[1] = a[1];
	}

	if (a[2] < b[2])
	{
		min[2] = a[2];
		max[2] = b[2];
	}
	else
	{
		min[2] = b[2];
		max[2] = a[2];
	}
}

__forceinline static void vec2MinMax(const Vec2 a, const Vec2 b, Vec2 min, Vec2 max)
{
	if (a[0] < b[0])
	{
		min[0] = a[0];
		max[0] = b[0];
	}
	else
	{
		min[0] = b[0];
		max[0] = a[0];
	}

	if (a[1] < b[1])
	{
		min[1] = a[1];
		max[1] = b[1];
	}
	else
	{
		min[1] = b[1];
		max[1] = a[1];
	}
}

__forceinline static void vec2RunningMinMax(const Vec2 v, Vec2 min, Vec2 max)
{
	MIN1F(min[0], v[0]);
	MAX1F(max[0], v[0]);
	MIN1F(min[1], v[1]);
	MAX1F(max[1], v[1]);
}


__forceinline static void vec3RunningMin(const Vec3 v, Vec3 min)
{
	MIN1F(min[0], v[0]);
	MIN1F(min[1], v[1]);
	MIN1F(min[2], v[2]);
}

__forceinline static void vec3RunningMax(const Vec3 v, Vec3 max)
{
	MAX1F(max[0], v[0]);
	MAX1F(max[1], v[1]);
	MAX1F(max[2], v[2]);
}

__forceinline static void vec3RunningMinMax(const Vec3 v, Vec3 min, Vec3 max)
{
	MIN1F(min[0], v[0]);
	MAX1F(max[0], v[0]);
	MIN1F(min[1], v[1]);
	MAX1F(max[1], v[1]);
	MIN1F(min[2], v[2]);
	MAX1F(max[2], v[2]);
}

__forceinline static F32 vec3MaxComponent(const Vec3 v)
{
	F32 m = MAX(v[0], v[1]);
	return MAX(m, v[2]);
}

__forceinline static F32 vec4MaxComponent(const Vec4 v)
{
	F32 m = MAX(v[0], v[1]);
	MAX1(m, v[2]);
	return MAX(m, v[3]);
}

__forceinline static bool nearSameVec4(const Vec4 vecA, const Vec4 vecB)
{
	extern F32 near_same_vec3_tol_squared;
	Vec4 dv;
	subVec4(vecA,vecB,dv);
	return dotVec4(dv, dv) <= near_same_vec3_tol_squared;
}

__forceinline static bool nearSameVec3(const Vec3 vecA, const Vec3 vecB)
{
	extern F32 near_same_vec3_tol_squared;
	Vec3 dv;
	subVec3(vecA,vecB,dv);
	return dotVec3(dv, dv) <= near_same_vec3_tol_squared;
}

__forceinline static bool nearSameVec3Tol(const Vec3 a,const Vec3 b, F32 tolerance)
{
	Vec3 dv;
	subVec3(a,b,dv);
	return dotVec3(dv, dv) <= SQR(tolerance);
}

__forceinline static bool nearSameVec3XZ(const Vec3 vecA, const Vec3 vecB)
{
	extern F32 near_same_vec3_tol_squared;
	Vec3 dv;
	subVec3XZ(vecA,vecB,dv);
	return dotVec3XZ(dv, dv) <= near_same_vec3_tol_squared;
}

__forceinline static bool nearSameVec3XZTol(const Vec3 vecA, const Vec3 vecB, F32 tolerance)
{
	Vec3 dv;
	subVec3XZ(vecA,vecB,dv);
	return dotVec3XZ(dv, dv) <= SQR(tolerance);
}

// Used for directional vectors, compares the angle between them.  Assumes normalized vectors.
__forceinline static bool nearSameVec3Dir(const Vec3 a, const Vec3 b)
{
	return dotVec3(a, b) >= 0.999f;
}

__forceinline static bool nearSameVec3DirTol(const Vec3 a, const Vec3 b, F32 tolerance)
{
	return dotVec3(a, b) >= tolerance;
}

__forceinline static bool nearSameVec2(const Vec2 a,const Vec2 b)
{
	return nearSameF32(a[0], b[0]) && nearSameF32(a[1], b[1]);
}

bool nearSameMat3Tol(const Mat3 a, const Mat3 b, F32 tolerence);
bool nearSameMat4Tol(const Mat4 a, const Mat4 b, F32 rotation_tolerence, F32 position_tolerence);


#define PLANE_EPSILON (1.0e-5)

__forceinline static void flipPlane(Vec4 plane)
{
	negateVec4(plane, plane);
}

__forceinline static F32 normalVec3(Vec3 v)
{
	F32		mag2,mag=0,invmag;
		
	// Is there some way to let the compiler know to generate an atomic inverse
	// square root if the cpu supports it? (ie; sse, 3dnow, r10k, etc.)
	mag2 = SQR(v[0]) + SQR(v[1]) + SQR(v[2]);
	if (mag2 > FLT_EPSILON)
	{
		mag = sqrtf(mag2);
		invmag = 1.f/mag;
		scaleVec3(v,invmag,v);
	}

	return mag;
}

__forceinline static F32 normalizeCopyVec3(const Vec3 v, Vec3 vOut)
{
	F32		mag2,mag=0,invmag;

	mag2 = SQR(v[0]) + SQR(v[1]) + SQR(v[2]);
	if (mag2 > FLT_EPSILON)
	{
		mag = sqrtf(mag2);
		invmag = 1.f/mag;
		scaleVec3(v,invmag,vOut);
	}
	else
	{
		zeroVec3(vOut);
	}

	return mag;
}

__forceinline static void truncateVec3(Vec3 v, F32 truncatedLen)
{
	F32 scale = lengthVec3Squared(v);
	if (scale > SQR(truncatedLen)) 
	{
		scale = truncatedLen / sqrtf(scale);
		scaleVec3(v, scale, v);
	}
}


// returns the area of the triangle
__forceinline static F32 makePlaneNormal(const Vec3 p0, const Vec3 p1, const Vec3 p2, Vec3 plane_normal)
{
	Vec3 a, b;
	subVec3(p1, p0, a);
	subVec3(p2, p0, b);
	crossVec3(a, b, plane_normal);
	return 0.5f * normalVec3(plane_normal);
}

// returns the area of the triangle
__forceinline static float makePlane(const Vec3 p0, const Vec3 p1, const Vec3 p2, Vec4 plane)
{
	F32 area = makePlaneNormal(p0, p1, p2, plane);
	plane[3] = dotVec3(plane, p0);
	return area;
}

__forceinline static void makePlane2(const Vec3 pos, const Vec3 normal, Vec4 plane)
{
	copyVec3(normal, plane);
	normalVec3(plane);
	plane[3] = dotVec3(plane, pos);
}

__forceinline static float distanceToPlane(const Vec3 pos, const Vec4 plane)
{
	float dist = dotVec3(pos, plane) - plane[3];
	dist = FloatBranchFL(fabsf(dist), PLANE_EPSILON, 0.0f, dist);
	return dist;
}

__forceinline static float distanceToPlaneNoEpsilon(const Vec3 pos, const Vec4 plane)
{
	return dotVec3(pos, plane) - plane[3];
}

__forceinline static void intersectPlane2(const Vec3 p0, const Vec3 p1, const Vec4 plane, F32 dist0, F32 dist1, Vec3 intersect)
{
	F32 ratio;
	Vec3 diff;
	dist0 = fabs(dist0);
	dist1 = fabs(dist1);
	ratio = dist0 / (dist0 + dist1);
	subVec3(p1, p0, diff);
	scaleAddVec3(diff, ratio, p0, intersect);
}

__forceinline static int intersectPlane(const Vec3 p0, const Vec3 p1, const Vec4 plane, Vec3 intersect)
{
	F32 d0 = distanceToPlane(p0, plane);
	F32 d1 = distanceToPlane(p1, plane);
	if (d0 > 0 && d1 > 0)
		return 0;
	if (d0 < 0 && d1 < 0)
		return 0;
	if (d0 == 0)
	{
		copyVec3(p0, intersect);
		return 1;
	}
	if (d1 == 0)
	{
		copyVec3(p1, intersect);
		return 1;
	}
	intersectPlane2(p0, p1, plane, d0, d1, intersect);
	return 1;
}

// |m00 m10|
// |m01 m11|
__forceinline static int invertMat2(F32 m00, F32 m01, F32 m10, F32 m11, Vec2 inv0, Vec2 inv1)
{
	F32 det, detinv;

	// determinant
	det = m00 * m11 - m01 * m10;
	if (nearSameF32(det, 0))
		return 0;

	// invert
	detinv = 1.f / det;
	inv0[0] = m11 * detinv;
	inv0[1] = -m01 * detinv;
	inv1[0] = -m10 * detinv;
	inv1[1] = m00 * detinv;

	return 1;
}

// |m00 m10|
// |m01 m11|
__forceinline static int invertDMat2(double m00, double m01, double m10, double m11, DVec2 inv0, DVec2 inv1)
{
	double det;

	// determinant
	det = m00 * m11 - m01 * m10;
	if (nearSameDoubleTol(det, 0, 1e-12))
		return 0;

	// invert
	inv0[0] = m11 / det;
	inv0[1] = -m01 / det;
	inv1[0] = -m10 / det;
	inv1[1] = m00 / det;

	return 1;
}

__forceinline static void mulVecMat2(const Vec2 m0, const Vec2 m1, const Vec2 v, Vec2 out)
{
#ifdef _FULLDEBUG
	assert(m0!=out && m1!=out && v!=out);
#endif
	out[0] = m0[0] * v[0] + m1[0] * v[1];
	out[1] = m0[1] * v[0] + m1[1] * v[1];
}

__forceinline static void mulDVecMat2(const DVec2 m0, const DVec2 m1, const DVec2 v, DVec2 out)
{
#ifdef _FULLDEBUG
	assert(m0!=out && m1!=out && v!=out);
#endif
	out[0] = m0[0] * v[0] + m1[0] * v[1];
	out[1] = m0[1] * v[0] + m1[1] * v[1];
}

__forceinline static void mulVecMat2_special(const Vec2 m0, const Vec2 m1, F32 v0, F32 v1, F32 *t, F32 *u)
{
	*t = m0[0] * v0 + m1[0] * v1;
	*u = m0[1] * v0 + m1[1] * v1;
}

__forceinline static void mulDVecMat2_special(const DVec2 m0, const DVec2 m1, double v0, double v1, double *t, double *u)
{
	*t = m0[0] * v0 + m1[0] * v1;
	*u = m0[1] * v0 + m1[1] * v1;
}

void calcTransformVectors(const Vec3 pos[3], const Vec2 uv[3], Vec3 utransform, Vec3 vtransform);
void calcTransformVectorsAccurate(const Vec3 pos[3], const Vec2 uv[3], Vec3 utransform, Vec3 vtransform);

__forceinline static float findCenter(const Vec3 p0, const Vec3 p1, const Vec3 p2, Vec3 center)
{
	float radiusSq, temp;
	addVec3(p0, p1, center);
	addVec3(center, p2, center);
	scaleVec3(center, 0.333333f, center);

	radiusSq = distance3Squared(center, p0);
	temp = distance3Squared(center, p1);
	radiusSq = MAX(radiusSq, temp);
	temp = distance3Squared(center, p2);
	radiusSq = MAX(radiusSq, temp);

	return (float)(sqrtf(radiusSq));
}

__forceinline static F32 triArea3Squared(const Vec3 a, const Vec3 b, const Vec3 c)
{
	Vec3 tmp, tmp2, tmp3;
	subVec3(b, a, tmp);
	subVec3(c, a, tmp2);
	crossVec3(tmp, tmp2, tmp3);
	return 0.25f * lengthVec3Squared(tmp3);
}

__forceinline static F32 triArea3(const Vec3 a, const Vec3 b, const Vec3 c)
{
	return sqrtf(triArea3Squared(a, b, c));
}

__forceinline static F32 triArea2(const Vec2 a, const Vec2 b, const Vec2 c)
{
	Vec2 tmp, tmp2;

	subVec2(b, a, tmp);
	subVec2(c, a, tmp2);

	return 0.5f * fabsf(tmp[0]*tmp2[1] - tmp[1]*tmp2[0]);
}

__forceinline static void zeroMat3(Mat3 m)
{
	memset(&m[0][0], 0, sizeof(Mat3));
}

__forceinline static bool isNonZeroMat3(const Mat3 m)
{
	// must have at least one non-zero element in each column
	return (m[0][0] || m[0][1] || m[0][2]) && (m[1][0] || m[1][1] || m[1][2]) && (m[2][0] || m[2][1] || m[2][2]);
}

__forceinline static void zeroMat4(Mat4 m)
{
	memset(&m[0][0], 0, sizeof(Mat4));
}

__forceinline static void zeroMat44(Mat44 m)
{
	memset(&m[0][0], 0, sizeof(Mat44));
}

__forceinline static void identityMat3(Mat3 m)
{
	zeroMat3(m);
	m[0][0] = m[1][1] = m[2][2] = 1;
}

__forceinline static void identityMat4(Mat4 m)
{
	zeroMat4(m);
	m[0][0] = m[1][1] = m[2][2] = 1;
}

__forceinline static void identityMat44(Mat44 m)
{
	zeroMat44(m);
	m[0][0] = m[1][1] = m[2][2] = m[3][3] = 1;
}


__forceinline static void addMat3(const Mat3 m1, const Mat3 m2, Mat3 r)
{
	addVec3(m1[0], m2[0], r[0]);
	addVec3(m1[1], m2[1], r[1]);
	addVec3(m1[2], m2[2], r[2]);
}

__forceinline static void addMat44(const Mat44 m1, const Mat44 m2, Mat44 r)
{
	addVec4(m1[0], m2[0], r[0]);
	addVec4(m1[1], m2[1], r[1]);
	addVec4(m1[2], m2[2], r[2]);
	addVec4(m1[3], m2[3], r[3]);
}

__forceinline static void addToMat3(const Mat3 m, Mat3 r)
{
	addToVec3(m[0], r[0]);
	addToVec3(m[1], r[1]);
	addToVec3(m[2], r[2]);
}

__forceinline static void addToMat44(const Mat44 m, Mat44 r)
{
	addToVec4(m[0], r[0]);
	addToVec4(m[1], r[1]);
	addToVec4(m[2], r[2]);
	addToVec4(m[3], r[3]);
}

__forceinline static void subMat3(const Mat3 a, const Mat3 b, Mat3 r)
{
	subVec3(a[0], b[0], r[0]);
	subVec3(a[1], b[1], r[1]);
	subVec3(a[2], b[2], r[2]);
}

__forceinline static void subMat44(const Mat44 a, const Mat44 b, Mat44 r)
{
	subVec4(a[0], b[0], r[0]);
	subVec4(a[1], b[1], r[1]);
	subVec4(a[2], b[2], r[2]);
	subVec4(a[3], b[3], r[3]);
}

__forceinline static void subFromMat3(const Mat3 m, Mat3 r)
{
	subFromVec3(m[0], r[0]);
	subFromVec3(m[1], r[1]);
	subFromVec3(m[2], r[2]);
}

__forceinline static void subFromMat44(const Mat44 m, Mat44 r)
{
	subFromVec4(m[0], r[0]);
	subFromVec4(m[1], r[1]);
	subFromVec4(m[2], r[2]);
	subFromVec4(m[3], r[3]);
}

__forceinline static void scaleByMat3(Mat3 m, float scale)
{
	scaleByVec3(m[0], scale);
	scaleByVec3(m[1], scale);
	scaleByVec3(m[2], scale);
}

__forceinline static void scaleByMat44(Mat44 m, float scale)
{
	scaleByVec4(m[0], scale);
	scaleByVec4(m[1], scale);
	scaleByVec4(m[2], scale);
	scaleByVec4(m[3], scale);
}

__forceinline static int invertMat3(const Mat3 a, Mat3 b)  
{
#ifdef _FULLDEBUG
	assert(a!=b);
#endif
	return invertMat3Copy(a, b);
}

__forceinline static int invertMat4(const Mat4 a, Mat4 b)  
{
#ifdef _FULLDEBUG
	assert(a!=b);
#endif
	invertMat4Copy(a, b);
	return 1;
}

__forceinline static int invertMat44(const Mat44 a, Mat44 b)  
{
#ifdef _FULLDEBUG
	assert(a!=b);
#endif
	invertMat44Copy(a, b);
	return 1;
}

__forceinline static F32 det3Vec3(const Vec3 v0,const Vec3 v1,const Vec3 v2)
{
	F32	a,b,c,d,e,f;

	a = v0[0] * v1[1] * v2[2];
	b = v0[1] * v1[2] * v2[0];
	c = v0[2] * v1[0] * v2[1];
	d = v2[0] * v1[1] * v0[2];
	e = v2[1] * v1[2] * v0[0];
	f = v2[2] * v1[0] * v0[1];
	return(a + b + c - d - e - f);
}

__forceinline static F32 saturate(F32 val)
{
    return CLAMPF(val, 0.f, 1.f);
}

__forceinline static F32 lerp(F32 a, F32 b, F32 t)
{
    return a * (1 - t) + b * t;
}

// Returns vector in the plane determined by [in]; [in] should be normalized
__forceinline static int getPerpVec3(const Vec3 in, Vec3 out)
{	
#ifdef _FULLDEBUG
	assert(in!=out);
#endif
	if(!out || vec3IsZero(in))
	{
		return 0;
	}

	if(fabs(dotVec3(in, upvec))>0.7)
	{
		Vec3 temp = {0, 0, 1};
		crossVec3(in, temp, out);
	} else
	{
		crossVec3(in, upvec, out);
	}

	return 1;
}

__forceinline static void outerProductVec3(const Vec3 a, const Vec3 b, Mat3 r)
{
	int x,y;
	for (y = 0; y < 3; y++)
	{
		for (x = 0; x < 3; x++)
			r[x][y] = a[y] * b[x];
	}
}

__forceinline static void outerProductVec4(const Vec4 a, const Vec4 b, Mat44 r)
{
	int x,y;
	for (y = 0; y < 4; y++)
	{
		for (x = 0; x < 4; x++)
			r[x][y] = a[y] * b[x];
	}
}

void tangentBasis(Mat3 basis, const Vec3 pv0, const Vec3 pv1, const Vec3 pv2, const Vec2 t0, const Vec2 t1, const Vec2 t2, const Vec3 n);

bool calcBarycentricCoordsXZProjected(const Vec3 v0_in, const Vec3 v1_in, const Vec3 v2_in, const Vec3 pos_in, Vec3 barycentric_coords);

__forceinline static void splineEvaluateEx(const Mat4 *matrix, F32 weight, const Vec3 in, Vec3 out, Vec3 up, Vec3 tangent, const Vec3 norm_in, Vec3 norm_out)
{
	Vec3 spline_pos, spline_tan, spline_binorm, temp;
	Mat3 spline_rotation;
	Vec3 scaled_in;

	scaled_in[0] = in[0] * matrix[1][2][1];
	scaled_in[1] = in[1] * matrix[1][2][1];
	scaled_in[2] = in[2];

	// Compute position
	scaleVec3(matrix[0][0], (1-weight)*(1-weight)*(1-weight), temp);
	copyVec3(temp, spline_pos);
	scaleVec3(matrix[0][1], weight*weight*weight, temp);
	addVec3(temp, spline_pos, spline_pos);
	scaleVec3(matrix[0][2], 3*weight*(1-weight)*(1-weight), temp);
	addVec3(temp, spline_pos, spline_pos);
	scaleVec3(matrix[0][3], 3*weight*weight*(1-weight), temp);
	addVec3(temp, spline_pos, spline_pos);

	// Compute tangent (derivative)
	scaleVec3(matrix[0][0], (-3*weight*weight+6*weight-3), temp);
	copyVec3(temp, tangent);
	scaleVec3(matrix[0][1], (3*weight*weight), temp);
	addVec3(temp, tangent, tangent);
	scaleVec3(matrix[0][2], (9*weight*weight-12*weight+3), temp);
	addVec3(temp, tangent, tangent);
	scaleVec3(matrix[0][3], (-9*weight*weight+6*weight), temp);
	addVec3(temp, tangent, tangent);

	copyVec3(tangent, spline_tan);
	normalVec3(spline_tan);

	scaleVec3(matrix[1][0], (1-weight), temp);
	scaleVec3(matrix[1][1], (weight), up);
	addVec3(temp, up, up);
	normalVec3(up);

	crossVec3(spline_tan, up, spline_binorm);
	crossVec3(spline_binorm, spline_tan, up);

	copyVec3(spline_binorm,		spline_rotation[0]);
	copyVec3(up,				spline_rotation[1]);
	copyVec3(spline_tan,		spline_rotation[2]);
	
	mulVecMat3(scaled_in, spline_rotation, out);

	mulVecMat3(norm_in, spline_rotation, norm_out);

	addVec3(out, spline_pos, out);
}

__forceinline static void splineEvaluate(const Mat4 *matrix, F32 weight, const Vec3 in, Vec3 out, Vec3 up, Vec3 tangent)
{
	Vec3 norm;
	splineEvaluateEx(matrix, weight, in, out, up, tangent, unitvec3, norm);
}

// normalized phi where 180 degrees is 1.0f.  This is to avoid floating point errors
__forceinline static void uniformSphereShellSlice(F32 fUniformValue, F32 fTheta, F32 fNormalizedPhi, F32 fRadius, Vec3 vResult)
{
	sphericalCoordsToVec3(
		vResult,
		fUniformValue * fTheta,
		acosf(fUniformValue * fNormalizedPhi), // Note the arccos, this is to correct for bunching at the poles, since spherical coordinates are not uniform across the sphere
		fRadius
		);
}

__forceinline static bool sphereSphereCollision(const Vec3 mid1, F32 radius1, const Vec3 mid2, F32 radius2)
{
	F32 distsqr = distance3Squared(mid1, mid2);
	F32 radius = radius1 + radius2;
	return distsqr <= SQR(radius);
}

void bezierGetPoint(const Vec2 controlPoints[4], F32 t, Vec2 point);
void bezierGetPoint3D_fast(const Vec3 controlPoints[4], F32 t, Vec3 point, Vec3 tangent, Vec3 deriv2);
void bezierGetPoint3D(const Vec3 controlPoints[4], F32 t, Vec3 point);
void bezierGetTangent3D(const Vec3 controlPoints[4], F32 t, Vec3 point);
void bezierGet2ndDeriv3D(const Vec3 controlPoints[4], F32 t, Vec3 point);

__forceinline static int RoundUpToGranularity(int amount, int granularity)
{
	return ( amount + granularity - 1 ) / granularity * granularity;
}

//returns amount rounded to the nearest multiple of multipleOf
__forceinline static int RoundToNearestMultiple_positive(int amount, int multipleOf)
{
	return ((amount + (multipleOf) / 2) / multipleOf ) * multipleOf;
}

__forceinline static int RoundToNearestMultiple(int amount, int multipleOf)
{
	return amount >= 0 ? RoundToNearestMultiple_positive(amount, multipleOf) : -RoundToNearestMultiple_positive(-amount, multipleOf);
}

F32 findClippedArea2D(const Vec2 p0, const Vec2 p1, const Vec2 p2, const Vec2 clip_min, const Vec2 clip_max);

bool isFacingDirection( Vec3 vFacing, Vec3 vDir );
bool isFacingDirectionEx( Vec3 vFacing, Vec3 vDir, F32 fDirLength, F32 fAllowedDeviationInRadians );

void pointProjectOnLine( const Vec3 lnA, const Vec3 lnB, const Vec3 pt, Vec3 out );

void projectVecOntoPlane(	const Vec3 vecToProject,
							const Vec3 vecUnitPlaneNormal,
							Vec3 vecProjOut);

void projectYOntoPlane(	const F32 y,
						const Vec3 vecUnitPlaneNormal,
						Vec3 vecProjOut);

void unitDirVec3ToMat3(const Vec3 vecUnitDir, Mat3 mat3Out);

void rotateUnitVecTowardsUnitVec(	const Vec3 vecUnitSource,
									const Vec3 vecUnitTarget,
									const F32 scale,
									Vec3 vecOut);

// a is assumed normalized
__forceinline static void projectBOntoNormalizedVec3(const Vec3 a, const Vec3 b, Vec3 out)
{
	F32 fAdotB = dotVec3(a,b);
	scaleVec3(a, fAdotB, out);
}

// a is assumed normalized
__forceinline static void perpedicularProjectionNormalizedAOntoVec3(const Vec3 a, const Vec3 b, Vec3 out)
{
	Vec3 vProj;
	projectBOntoNormalizedVec3(a, b, vProj);
	subVec3(b, vProj, out);
}


void rotateVecAboutAxis( F32 fAngle, const Vec3 vToRotAbout, const Vec3 vToRot, Vec3 vResult );
void rotateVecAboutAxisEx( F32 c, F32 s, const Vec3 vToRotAbout, const Vec3 vToRot, Vec3 vResult );

// returns the angle between two normalized vectors
F32 getAngleBetweenNormalizedVec3(const Vec3 v1, const Vec3 v2);

// returns the angle between the vector and the UP vec (0, 1, 0)
__forceinline static F32 getAngleBetweenNormalizedUpVec3(const Vec3 v1)
{
	return acosf(CLAMP(v1[1], -1.f, 1.f));
}

// returns the angle between two vectors
F32 getAngleBetweenVec3(const Vec3 v1, const Vec3 v2);

#if _XBOX || _M_X64
#define x87_stack_empty() 1
#define is_fp_default_control_word() 1
#else
bool x87_stack_empty(void);
bool is_fp_default_control_word(void);
#endif

//NOT cryptographically secure in any meaningful way. Just useful for obfuscating "1 2 3 4 5"
U32 reversibleHash(U32 iIn, bool bDirection);

bool isSphereInsideCone(Vec3 vConeApex, Vec3 vConeDir, F32 fConeLength, F32 fTanConeAngle, Vec3 vSphereCenter, F32 fSphereRadius);

// Define to 1 to replace math functions with limited domains with asserting version, to track
// bad inputs. A future revision will include separate versions that automatically clamp, when
// a computed input is expected to possibly go out-of-domain due numerical accuracy issues.
// Example: The dot product of two normalized vectors may be slightly over 1 in absolute value,
// but this causes an acos domain error, returning #IND instead of a usable result.
#define SAFE_TRANSCENDENTAL_FUNCTIONS 0

#if SAFE_TRANSCENDENTAL_FUNCTIONS

double safe_sqrt(_In_ double x);
double safe_acos(_In_ double x);
double safe_asin(_In_ double x);
double safe_log(_In_ double x);
float safe_logf(_In_ float x);
double safe_log10(_In_ double x);

#ifdef logf
#undef logf
#endif

#define sqrt( _X )	safe_sqrt( _X )
#define acos( _X )	safe_acos( _X )
#define asin( _X )	safe_asin( _X )
#define log( _X )	safe_log( _X )
#define logf( _X )	safe_logf( _X )
#define log10( _X )	safe_log10( _X )

#endif

__forceinline float log2f(float x)
{
	return 1.44269504f * logf(x);
}

// Clamped transcendentals. These versions efficiently clamp inputs under the assumption
// that float calculations may generate slightly out-of-domain inputs, such as acos on the dot
// product of two normalized float vectors. These functions do not test
// for undefined out-of-domain inputs like NaN or infinity, nor significantly out-of-domain
// inputs that may indicate a bug in calling code. Use safe transcendental functions to validate inputs
// by assertion, even when FPU/SSE exceptions are disabled.

__forceinline float sqrtf_clamped(float x);
__forceinline float acosf_clamped(float x);
__forceinline float asinf_clamped(float x);


static __forceinline int fastIntToString_inline(int iValueIn, char outBuf[16])
{
	bool bNeg = false;
	char *pWriteHead;
	U32 iInternalValue;

	if (!iValueIn)
	{
		outBuf[15] = '0';
		return 1;
	}

	if (iValueIn < 0)
	{
		bNeg = true;
		if (iValueIn == INT_MIN)
		{
			iInternalValue = (U32)INT_MAX+1;
		}
		else
		{
			iInternalValue = -iValueIn;
		}
	}
	else
	{
		iInternalValue = iValueIn;
	}

	pWriteHead = outBuf + 15;
	while (iInternalValue)
	{
		*pWriteHead = iInternalValue % 10 + '0';
		iInternalValue /= 10;
		pWriteHead--;
	}

	if (bNeg)
	{
		*pWriteHead = '-';
		pWriteHead--;
	}

	return (int)(15 - (pWriteHead - outBuf));
}


__forceinline static int fastFloatToString_inline(F32 f,char *buf, int buf_size)
{
	int pos=0,dp,num;

	if (buf_size < 2)
	{
		return buf_size;
	}

	if (f<0)
	{
		buf[pos++]='-';
		f = -f;
	}
	dp=0;
	while (f>=10.0) 
	{
		f=f/10.0;
		dp++;
	}
	// output a max of 6 decimal digits, as per normal %f
	while (dp >= -6 && pos < buf_size)
	{
		num = f;
		f=f-num;
		buf[pos++]='0'+num;
		if (dp==0 && pos < buf_size)
		{
			buf[pos++]='.';
		}
		f=f*10.0;
		dp--;
	}
	return pos;
}

__inline F32 F32_infinity()
{
	U32 value = 0x7f800000; 
	return *(F32 *)&value;
}

__inline F32 F32_negative_infinity()
{
	U32 value = 0xff800000;
	return *(F32 *)&value;
}

C_DECLARATIONS_END

#endif

