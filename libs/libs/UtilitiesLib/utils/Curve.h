#pragma once
GCC_SYSTEM

#include "mathutil.h"

__forceinline static void calcUniformCubicBSplinePoint(const Vec3 vControlPoints[4], F32 t, Vec3 vResult)
{
	Vec4 vBlendingVector =
	{
		( -CUBE(t) + 3 * SQR(t) - 3 * t + 1 ) / 6,
		( 3 * CUBE(t) - 6 * SQR(t) + 4 ) / 6,
		( -3 * CUBE(t) + 3 * SQR(t) + 3 * t + 1 ) / 6,
		CUBE(t) / 6
	};

	scaleVec3(vControlPoints[0], vBlendingVector[0], vResult);
	scaleAddVec3(vControlPoints[1], vBlendingVector[1], vResult, vResult);
	scaleAddVec3(vControlPoints[2], vBlendingVector[2], vResult, vResult);
	scaleAddVec3(vControlPoints[3], vBlendingVector[3], vResult, vResult);
}

__forceinline static void calcUniformCubicBSplineDerivative(const Vec3 vControlPoints[4], F32 t, Vec3 vResult)
{
	Vec4 vBlendingVector =
	{
		( -3 * SQR(t) + 6 * t - 3 ) / 6,
		( 9 * SQR(t) - 12 * t ) / 6,
		( -9 * SQR(t) + 6 * t + 3 ) / 6,
		( 3 * SQR(t) ) / 6
	};

	scaleVec3(vControlPoints[0], vBlendingVector[0], vResult);
	scaleAddVec3(vControlPoints[1], vBlendingVector[1], vResult, vResult);
	scaleAddVec3(vControlPoints[2], vBlendingVector[2], vResult, vResult);
	scaleAddVec3(vControlPoints[3], vBlendingVector[3], vResult, vResult);
}

__forceinline static void calcCubicBezierCurvePoint(const Vec3 vControlPoints[4], F32 t, Vec3 vResult)
{
	Vec4 vBernsteinBasis =
	{
		-CUBE(t) + 3 * SQR(t) - 3 * t + 1,
		3 * CUBE(t) - 6 * SQR(t) + 3 * t,
		-3 * CUBE(t) + 3 * SQR(t),
		CUBE(t)
	};
	int i, j;
	for (i=0; i<3; ++i)
	{
		vResult[i] = 0.0f;
		for (j=0; j<4; ++j)
			vResult[i] += vControlPoints[j][i] * vBernsteinBasis[j];
	}
}

__forceinline static void calcCubicBezierCurveDerivative(const Vec3 vControlPoints[4], F32 t, Vec3 vResult)
{
	F32 omt = 1.0f - t;
	Vec4 vBernsteinBasis =
	{
		-3 * SQR(t) + 6 * t - 3,
		9 * SQR(t) - 12 * t + 3,
		-9 * SQR(t) + 6 * t,
		3 * SQR(t)
	};
	int i, j;
	for (i=0; i<3; ++i)
	{
		vResult[i] = 0.0f;
		for (j=0; j<4; ++j)
			vResult[i] += vControlPoints[j][i] * vBernsteinBasis[j];
	}
}