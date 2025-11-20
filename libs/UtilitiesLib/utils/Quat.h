#pragma once
GCC_SYSTEM


#include "mathutil.h"
#include "Vec4H.h"

extern const Quat unitquat;


#define quatXidx 0
#define quatYidx 1
#define quatZidx 2
#define quatWidx 3
#define quatX(q) ((q)[quatXidx])
#define quatY(q) ((q)[quatYidx])
#define quatZ(q) ((q)[quatZidx])
#define quatW(q) ((q)[quatWidx])
#define quatParamsXYZW(q) quatX(q), quatY(q), quatZ(q), quatW(q)
#define copyQuat(src,dst)			copyVec4(src,dst)
#define copyQuatAligned(src,dst)	copyVec4H(*(Vec4H *)src,*(Vec4H *)dst)
#define zeroQuat(q)					zeroVec4(q)
#define quatIsZero(q)				vec4IsZero(q)
#define unitQuat(q)					((q)[0] = 0.0f, (q)[1] = 0.0f, (q)[2] = 0.0f, (q)[3] = -1.0f)
#define printQuat(quat)				printVec4(quat)
#define sameQuat(q1,q2)				(((q1)[0]==(q2)[0]) && ((q1)[1]==(q2)[1]) && ((q1)[2]==(q2)[2]) && ((q1)[3]==(q2)[3]))


//
// Quat creation
//
void mat3ToQuat(const Mat3 R, Quat q);
void PYRToQuat(const Vec3 pyr, Quat q);
void PYToQuat(const Vec2 py, Quat q);
bool axisAngleToQuat(const Vec3 axis, F32 angle, Quat quat);
void yawQuat(F32 yaw, Quat q);
void pitchQuat(F32 pitch, Quat q);
void rollQuat(F32 roll, Quat q);



//
// Quat conversion
//
void quatToMat(const Quat q,Mat3 R);
void quatToMat3_0(const Quat q, Vec3 vec);
void quatToMat3_1(const Quat q, Vec3 vec);
void quatToMat3_2(const Quat q, Vec3 vec);
void quatToPYR(const Quat q, Vec3 pyr);
void quatToAxisAngle(const Quat quat, Vec3 axis, F32* angle);
void quatVecToMat4(Quat inpQuat, Vec3 inpVec, Mat4 outMat);



//
// Quat utils
//
void quatForceWPositive(Quat q);
void quatCalcWFromXYZ(Quat q);
bool quatIsValid(const Quat q);
void quatDiff(const Quat a, const Quat b, Quat q);
F32 quatGetAngle(const Quat quat);
F32 quatAngleBetweenQuats(const Quat a, const Quat b);
bool quatIsIdentity(const Quat quat, F32 fErrorMargin);

// Finds the angle between a and b between -PI and PI and compares to the angle passed in
bool quatWithinAngle(const Quat a, const Quat b, F32 angle);

__forceinline void quatNormalize(Quat q)
{
	F32		mag2,mag,invmag;
	mag2 = SQR(quatX(q)) + SQR(quatY(q)) + SQR(quatZ(q)) + SQR(quatW(q));

#if SPU
	if ( mag2 <= 0.0f )
	{
        // make it a unit quat...
        copyVec4(unitquat, q);
		return;
	}

    vec_float4 t = spu_splats(mag2 + 1.e-12f);
    invmag = spu_extract(rsqrtf4(t), 0);
	
    scaleVec4(q,invmag,q);
#else
	if ( mag2 <= 0.0f )
	{
		quatX(q) = 1.0f; // make it a unit quat...
		return;
	}
	mag = fsqrt(mag2);
	invmag = 1.f/mag;
	scaleVec4(q,invmag,q);
#endif
}

//
// Quat multiplication
//
//F32 quatInterp(F32 alpha, const Quat a, const Quat b, Quat q);
void quatInverse(const Quat q, Quat qi);
void quatRotateVec3(const Quat q, const Vec3 vecIn, Vec3 vecOut);  // Assumes vecIn and vecOut are NOT THE SAME VECTOR
void quatRotateVec3ZOnly(const Quat q, const Vec3 vecIn, Vec3 vecOut);
void quatMultiply(const Quat q1, const Quat q2, Quat qOut); // qOut = q2 * q1 (note the inverted order)

__forceinline void quatRotateVec3Inline(const F32 * __restrict q, const F32 * __restrict vecIn, F32 * __restrict vecOut)
{
	const F32 Qx = quatX(q), Qy = quatY(q), Qz = quatZ(q), Qw = quatW(q);
	const F32 Vx = vecIn[0], Vy = vecIn[1], Vz = vecIn[2];

#define Qx2 (Qx * Qx)
#define Qxy (Qx * Qy)
#define Qxz (Qx * Qz)
#define Qxw (Qx * Qw)
#define Qy2 (Qy * Qy)
#define Qyz (Qy * Qz)
#define Qyw (Qy * Qw)
#define Qz2 (Qz * Qz)
#define Qzw (Qz * Qw)
#define Qw2 (Qw * Qw)

	vecOut[0] = 
		( Qw2 + Qx2 - Qz2 - Qy2 ) * Vx
		+ ( ( Qzw + Qxy ) * Vy + ( Qxz - Qyw ) * Vz ) * 2.0f
		;

	vecOut[1] = 
		( Qy2 - Qx2 - Qz2 + Qw2 ) * Vy
		+ ( ( Qxy - Qzw ) * Vx+ ( Qyz + Qxw ) * Vz ) * 2.0f
		;

	vecOut[2] = 
		( ( Qxz + Qyw ) * Vx + ( Qyz - Qxw ) * Vy ) * 2.0f
		+ ( Qz2 - Qy2 - Qx2 + Qw2 ) * Vz
		;


#undef Qx2
#undef Qxy
#undef Qxz
#undef Qxw
#undef Qy2
#undef Qyz
#undef Qyw
#undef Qz2
#undef Qzw
#undef Qw2

}

// Note, I assume you want to apply a first, then b, so I handle switching the order for you

// + - + +
// + + - +
// - + + +
// - - - +

static const union kQuatMultSignVals
{
       F32 f[4];
       __m128 m;
} kQuatMultSignVals[4] = {{1.f,-1.f,1.f,1.f},
{1.f,1.f,-1.f,1.f},
{-1.f,1.f,1.f,1.f},
{-1.f,-1.f,-1.f,1.f}};

// Note, I assume you want to apply a first, then b, so I handle switching the order for you
__forceinline void quatMultiplyInlineAligned(const F32 * __restrict a, const F32 * __restrict b, F32 * __restrict c)
{
	// we're going to leave b unmolested, and swizzle a
	Vec4H vSwizzledA;
	Vec4H vA,vB;
	Vec4H vDot[4];

	copyVec4H(*(Vec4H *)a,vA);
	copyVec4H(*(Vec4H *)b,vB);

	vSwizzledA = vecSwizzleExp4H(vA, S_WZYX);
	vecVecMul4H(vSwizzledA,kQuatMultSignVals[0].m,vDot[0]);
	vecVecMul4H(vDot[0],vB,vDot[0]);

	//(2, 3, 0, 1)
	vSwizzledA = vecSwizzleExp4H(vA, S_ZWXY);
	vecVecMul4H(vSwizzledA,kQuatMultSignVals[1].m,vDot[1]);
	vecVecMul4H(vDot[1],vB,vDot[1]);

	//(1, 0, 3, 2)
	vSwizzledA = vecSwizzleExp4H(vA, S_YXWZ);
	vecVecMul4H(vSwizzledA,kQuatMultSignVals[2].m,vDot[2]);
	vecVecMul4H(vDot[2],vB,vDot[2]);

	// no swizzle
	vecVecMul4H(vA,kQuatMultSignVals[3].m,vDot[3]);
	vecVecMul4H(vDot[3],vB,vDot[3]);

	// finish 4 dot products together
	*(Vec4H *)c = sumComponents4Vec4H(&vDot[0],&vDot[1],&vDot[2],&vDot[3]);
}

// Note, I assume you want to apply a first, then b, so I handle switching the order for you
__forceinline void quatMultiplyInline(const F32 * __restrict a, const F32 * __restrict b, F32 * __restrict c)
{
	// we're going to leave b unmolested, and swizzle a
	Vec4H vSwizzledA;
	Vec4H vA,vB;
	Vec4H vDot[4];
	Vec4H vResult,vTemp;

	setVec4H(vA,a[0],a[1],a[2],a[3]);
	setVec4H(vB,b[0],b[1],b[2],b[3]);

	vSwizzledA = vecSwizzleExp4H(vA, S_WZYX);
	vecVecMul4H(vSwizzledA,kQuatMultSignVals[0].m,vDot[0]);
	vecVecMul4H(vDot[0],vB,vDot[0]);

	//(2, 3, 0, 1)
	vSwizzledA = vecSwizzleExp4H(vA, S_ZWXY);
	vecVecMul4H(vSwizzledA,kQuatMultSignVals[1].m,vDot[1]);
	vecVecMul4H(vDot[1],vB,vDot[1]);

	//(1, 0, 3, 2)
	vSwizzledA = vecSwizzleExp4H(vA, S_YXWZ);
	vecVecMul4H(vSwizzledA,kQuatMultSignVals[2].m,vDot[2]);
	vecVecMul4H(vDot[2],vB,vDot[2]);

	// no swizzle
	vecVecMul4H(vA,kQuatMultSignVals[3].m,vDot[3]);
	vecVecMul4H(vDot[3],vB,vDot[3]);

	// finish 4 dot products together

	// FIX THIS
	_MM_TRANSPOSE4_PS(vDot[0],vDot[1],vDot[2],vDot[3]);

	addVec4H(vDot[0],vDot[1],vResult);
	addVec4H(vDot[2],vDot[3],vTemp);
	addVec4H(vResult,vTemp,vResult);

	copyVec4(Vec4HToVec4(vResult),c);

//	quatX(c) = (quatW(a) * quatX(b)) - (quatZ(a) * quatY(b)) + (quatY(a) * quatZ(b)) + (quatX(a) * quatW(b));

//	quatY(c) = (quatZ(a) * quatX(b)) + (quatW(a) * quatY(b)) - (quatX(a) * quatZ(b)) + (quatY(a) * quatW(b)); 

//	quatZ(c) = - (quatY(a) * quatX(b)) + (quatX(a) * quatY(b)) + (quatW(a) * quatZ(b)) + (quatZ(a) * quatW(b)); 

//	quatW(c) = - (quatX(a) * quatX(b)) - (quatY(a) * quatY(b)) - (quatZ(a) * quatZ(b)) + (quatW(a) * quatW(b)); 
}

// old implementation
#if 0
// Note, I assume you want to apply a first, then b, so I handle switching the order for you
__forceinline void quatMultiplyInline(const F32 * __restrict a, const F32 * __restrict b, F32 * __restrict c)
{
#if PLATFORM_CONSOLE
	// On Xbox, read into local variables to reduce multiple load instructions
	const F32 Ax = quatX(a), Ay = quatY(a), Az = quatZ(a), Aw = quatW(a);
	const F32 Bx = quatX(b), By = quatY(b), Bz = quatZ(b), Bw = quatW(b);
	F32 Cx, Cy, Cz, Cw;

	Cw = (Aw * Bw) - (Ax * Bx) - (Ay * By) - (Az * Bz); 
	Cx = (Aw * Bx) + (Ax * Bw) + (Ay * Bz) - (Az * By);
	Cy = (Aw * By) + (Ay * Bw) + (Az * Bx) - (Ax * Bz); 
	Cz = (Aw * Bz) + (Az * Bw) + (Ax * By) - (Ay * Bx); 

	quatW(c) = Cw;
	quatX(c) = Cx;
	quatY(c) = Cy;
	quatZ(c) = Cz;
#else

	quatW(c) = (quatW(a) * quatW(b)) - (quatX(a) * quatX(b)) - (quatY(a) * quatY(b)) - (quatZ(a) * quatZ(b)); 

	quatX(c) = (quatW(a) * quatX(b)) + (quatX(a) * quatW(b)) + (quatY(a) * quatZ(b)) - (quatZ(a) * quatY(b));

	quatY(c) = (quatW(a) * quatY(b)) + (quatY(a) * quatW(b)) + (quatZ(a) * quatX(b)) - (quatX(a) * quatZ(b)); 

	quatZ(c) = (quatW(a) * quatZ(b)) + (quatZ(a) * quatW(b)) + (quatX(a) * quatY(b)) - (quatY(a) * quatX(b)); 
#endif
}
#endif

//
// Quat conversion
//
__forceinline void quatToMatInline(const F32 * __restrict q, F32 * __restrict R)
{
	F32		tx, ty, tz, twx, twy, twz, txx, txy, txz, tyy, tyz, tzz;

	tx  = 2.f*quatX(q);
	ty  = 2.f*quatY(q);
	tz  = 2.f*quatZ(q);
	twx = tx*quatW(q);
	twy = ty*quatW(q);
	twz = tz*quatW(q);
	txx = tx*quatX(q);
	txy = ty*quatX(q);
	txz = tz*quatX(q);
	tyy = ty*quatY(q);
	tyz = tz*quatY(q);
	tzz = tz*quatZ(q);

	R[0*3+0] = 1.f-(tyy+tzz);
	R[0*3+1] = txy-twz;
	R[0*3+2] = txz+twy;
	R[1*3+0] = txy+twz;
	R[1*3+1] = 1.f-(txx+tzz);
	R[1*3+2] = tyz-twx;
	R[2*3+0] = txz-twy;
	R[2*3+1] = tyz+twx;
	R[2*3+2] = 1.f-(txx+tyy);
}

__forceinline void quatToMat34Inline(const F32 * __restrict q, F32 * __restrict R)
{
	F32		tx, ty, tz, twx, twy, twz, txx, txy, txz, tyy, tyz, tzz;

	tx  = 2.f*quatX(q);
	ty  = 2.f*quatY(q);
	tz  = 2.f*quatZ(q);
	twx = tx*quatW(q);
	twy = ty*quatW(q);
	twz = tz*quatW(q);
	txx = tx*quatX(q);
	txy = ty*quatX(q);
	txz = tz*quatX(q);
	tyy = ty*quatY(q);
	tyz = tz*quatY(q);
	tzz = tz*quatZ(q);

	R[0+0*4] = 1.f-(tyy+tzz);
	R[1+0*4] = txy+twz;
	R[2+0*4] = txz-twy;
	R[3] = 0.0f;

	R[0+1*4] = txy-twz;
	R[1+1*4] = 1.f-(txx+tzz);
	R[2+1*4] = tyz+twx;
	R[3+1*4] = 0.0f;

	R[0+2*4] = txz+twy;
	R[1+2*4] = tyz-twx;
	R[2+2*4] = 1.f-(txx+tyy);
	R[3+2*4] = 0.0f;
}

__forceinline void quatToMat44Inline(const F32 * __restrict q, F32 * __restrict R)
{
	F32		tx, ty, tz, twx, twy, twz, txx, txy, txz, tyy, tyz, tzz;

	tx  = 2.f*quatX(q);
	ty  = 2.f*quatY(q);
	tz  = 2.f*quatZ(q);
	twx = tx*quatW(q);
	twy = ty*quatW(q);
	twz = tz*quatW(q);
	txx = tx*quatX(q);
	txy = ty*quatX(q);
	txz = tz*quatX(q);
	tyy = ty*quatY(q);
	tyz = tz*quatY(q);
	tzz = tz*quatZ(q);

	R[0*4+0] = 1.f-(tyy+tzz);
	R[0*4+1] = txy-twz;
	R[0*4+2] = txz+twy;
	R[0*4+3] = 0.0f;
	R[1*4+0] = txy+twz;
	R[1*4+1] = 1.f-(txx+tzz);
	R[1*4+2] = tyz-twx;
	R[1*4+3] = 0.0f;
	R[2*4+0] = txz-twy;
	R[2*4+1] = tyz+twx;
	R[2*4+2] = 1.f-(txx+tyy);
	R[2*4+3] = 0.0f;

	setVec4(&R[3*4],0.f,0.f,0.f,1.f);
}

__forceinline F32 quatInterp(F32 alpha, const Quat a, const Quat b, Quat q)
{
	F32		beta;
	F32		cos_theta;

	beta = 1.f - alpha;

	// cosine theta = dot product of A and B
	cos_theta = (quatX(a)*quatX(b) + quatY(a)*quatY(b) + quatZ(a)*quatZ(b) + quatW(a)*quatW(b));

	if(cos_theta < 0.f)
		alpha = -alpha;

	// interpolate
	quatX(q) = beta*quatX(a) + alpha*quatX(b);
	quatY(q) = beta*quatY(a) + alpha*quatY(b);
	quatZ(q) = beta*quatZ(a) + alpha*quatZ(b);
	quatW(q) = beta*quatW(a) + alpha*quatW(b);
	quatNormalize(q);

	return cos_theta;
}

__forceinline F32 quatInterpAligned(F32 alpha, const Quat a, const Quat b, Quat q)
{
	F32		beta;
	Vec4H	cos_theta;
	Vec4H	vAlpha,vBeta;
	Vec4H	vTemp,vTemp2;

	beta = 1.f - alpha;

	// cosine theta = dot product of A and B
	dotVec4H(*(Vec4H *)a,*(Vec4H *)b,cos_theta);

	if(Vec4HToVec4(cos_theta)[0] < 0.f)
		alpha = -alpha;

	setVec4sameH(vAlpha,alpha);
	setVec4sameH(vBeta,beta);

	// interpolate
	vecVecMul4H(vBeta,*(Vec4H *)a,vTemp);
	vecVecMul4H(vAlpha,*(Vec4H *)b,vTemp2);
	addVec4H(vTemp,vTemp2,*(Vec4H *)q);

	quatNormalize(q);

	return Vec4HToVec4(cos_theta)[0];
}

__forceinline void quatLookAt(const Vec3 vStart, const Vec3 vTarget, Quat qResult)
{
	Vec3 vTowardTarget;
	Mat3 mat;
	subVec3(vTarget, vStart, vTowardTarget);

	orientMat3(mat, vTowardTarget);
	mat3ToQuat(mat, qResult);
}

__forceinline S32 quatIsNormalized(const Quat q)
{
	F32 lenSQR = lengthVec4Squared(q);
	
	return	lenSQR >= SQR(1.0f - 0.001f) &&
			lenSQR <= SQR(1.0f + 0.001f);
}
