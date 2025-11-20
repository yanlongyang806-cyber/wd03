// Only included by Vec4H.h
#include <xtl.h>
#include <xboxmath.h>

typedef XMVECTOR Vec4H;
typedef __declspec(passinreg) Vec4H Vec4HP;
typedef Vec4H Mat34H[3];
typedef Vec4H Mat44H[4];
#define PVP(P) (P)
#define RVP(P) (P)

#define Vec4HToVec4(R) ((R).v)

#define setVec4H( R, X, Y, Z, W )		R = XMVectorSet( (X), (Y), (Z), (W) )
#define setIVec4H( R, X, Y, Z, W )      R = (vec_float4)(vec_uint4){ (X), (Y), (Z), (W) }
#define setVec4sameH( R, S )			R = XMVectorReplicate( (S) )
#define vecSame4H( Scalar, R )					R = XMVectorReplicate(Scalar)
#define vecZero4H(R)					R = __vzero()

#define vecRecip4H(V1)					XMVectorReciprocal(V1)

#define vecAddExp4H( V1, V2 )					XMVectorAdd(V1, V2)
#define vecSubExp4H( V1, V2 )					XMVectorSubtract(V1, V2)
#define vecMulExp4H( V1, V2 )					XMVectorMultiply(V1, V2)
#define vecOrExp4H( V1, V2 )					XMVectorOrInt(V1, V2)
#define vecAndExp4H( V1, V2 )					XMVectorAndInt(V1, V2)
#define vecSwizzleExp4H( V1, Swizzle )			XMVectorPermute((V1), (V1), Swizzle)
#define vecSplatXExp4H( V1 )			XMVectorSplatX( V1 )
#define vecSplatYExp4H( V1 )			XMVectorSplatY( V1 )
#define vecSplatZExp4H( V1 )			XMVectorSplatZ( V1 )
#define vecSplatWExp4H( V1 )			XMVectorSplatW( V1 )

#define minVecExp4H( V1, V2 )			XMVectorMin( V1, V2 )
#define maxVecExp4H( V1, V2 )			XMVectorMax( V1, V2 )
#define vecClampExp4H(V, vMin, vMax)	XMVectorClamp(V, vMin, vMax)
#define vecSaturateExp4H(V)				XMVectorSaturate(V)

#define addVec4H( V1, V2, R )			R = XMVectorAdd( V1, V2 )
#define subVec4H( V1, V2, R )			R = XMVectorSubtract( V1, V2 )
#define vecVecMul4H( V1, V2, R )		R = XMVectorMultiply( V1, V2 )
#define scaleAddVec4H( V1, VS, V2, R )	R = XMVectorMultiplyAdd( V1, VS, V2 )
#define scaleAddVec4SH( V1, S, V2, R )	R = XMVectorAdd( V2, XMVectorScale( V1, S ) )
#define scaleSubVec4H( V1, V2, VS, R )	R = XMVectorNegativeMultiplySubtract(V2, VS, V1)
#define vecNegate4H( V1, R )					R = XMVectorNegate(V1)

#define copyVec4H( V1, R )				R = V1
#define vecSelect4H( V1, V2, VS, R )	R = XMVectorSelect( V1, V2, VS )
#define vecVecAnd4H( V1, V2, R )		R = XMVectorAndInt( V1, V2 )
#define vecVecAndC4H( V1, V2, R )		R = XMVectorAndCInt( V1, V2 )
#define vecVecOr4H( V1, V2, R )					R = XMVectorOrInt(V1, V2)
#define vecVecXor4H( V1, V2, R )				R = XMVectorXorInt(V1, V2)

#define vecCmpGreaterEqExp4H( V1, V2)			XMVectorGreaterOrEqual(V1, V2)
#define vecCmpLessEqExp4H( V1, V2 )				XMVectorLessOrEqual(V1, V2)
#define vecCmpEq4H( V1, V2, R )					R = XMVectorEqual(V1, V2)
#define vecCmpNEq4H( V1, V2, R )				R = XMVectorNotEqual(V1, V2)

#define vecSplatX( V1, R )				R = vecSplatXExp4H(V1)
#define vecSplatY( V1, R )				R = vecSplatYExp4H(V1)
#define vecSplatZ( V1, R )				R = vecSplatZExp4H(V1)
#define vecSplatW( V1, R )				R = vecSplatWExp4H(V1)

__forceinline Vec4H vecFromScalarIntMem4H(const int * Scalar)
{
	return XMVectorReplicate(*(const F32*)Scalar);
}

__forceinline Vec4H vecFromScalarInt4H(int Scalar)
{
	return vecFromScalarIntMem4H(&Scalar);
}

__forceinline Vec4H vecIntToFloat4H(Vec4H V1)
{
	return XMConvertVectorIntToFloat(V1, 0);
}

__forceinline Vec4H vecFloatRoundZ4H(Vec4H V1)
{
	return XMConvertVectorIntToFloat(V1, 0);
}

#define vecFloatFromScalarInt4H( I, R )			R = vecIntToFloat4H(vecFromScalarInt4H(I))

__forceinline Vec4H vecSameConstantMem4H(const float * Scalar)
{
	return XMVectorReplicate(*Scalar);
}

__forceinline Vec4H vecFromScalar4H(float Scalar)
{
	return XMVectorReplicate(Scalar);
}

__forceinline Vec4H vecSameConstant4H(float Scalar)
{
	return XMVectorReplicate(Scalar);
}

// Mix takes the first two swizzle components form V1, the second two from V2
#define vecMixExp4H( V1, V2, Swizzle )			XMVectorPermute((V1), (V2), Swizzle)
#define vecSwizzle4H( V1, Swizzle, R )			R = vecSwizzleExp4H((V1), Swizzle)

#include "Vec4Hswizzle.h"

#define vecReduceSym4H(V1, vec_symmetric_binary_operator, R)	{ Vec4H _temp_ = vecSwizZWXYExp4H(V1); vec_symmetric_binary_operator(V1, _temp_, _temp_); R = vecSwizYZWXExp4H(_temp_); vec_symmetric_binary_operator(R, _temp_, R); ; }
#define vecReduce4H(V1, vec_binary_operator, R)	vecReduceSym4H(V1, vec_binary_operator, R); vecSplatX(R, R)
#define vecSumComponents4H(V1, R)				vecReduce4H(V1, addVec4H, R)

#define dotVec4H(V1, V2, R)						vecVecMul4H(V1, V2, R); vecSumComponents4H(R, R)

#define vecAnyComponentSet4H(V1, R)				vecReduce4H(V1, vecVecOr4H, R)
#define vecAllComponentSet4H(V1, R)				vecReduce4H(V1, vecVecAnd4H, R)
#define vecTestZero4H(V1, R)					vecCmpEq4H(V1, g_vec4ZeroH, R); vecAllComponentSet4H(R, R)

#define g_vec4UnitH XMVectorSplatOne()

#define vecAnyNormalTo3H( V1, R ) \
{	\
	Vec4H _temp_normal_, _cmp_mask_;	\
	R = vecSubExp4H(vecSwizzleExp4H((V1), S_ZXYW), vecSwizzleExp4H((V1), S_YZXW));	\
	_temp_normal_ = vecMulExp4H(vecSwizzleExp4H((V1), S_YXWW), vecSwizzleExp4H(g_vec4UtilityH, S_XYWW));	\
	vecTestZero4H(R, _cmp_mask_);	\
	vecSelect4H(R, _temp_normal_, _cmp_mask_, R);	\
}

#define crossVec4H(V1, V2, R)					(R) = XMVectorCross((V1), (V2))

__forceinline static Vec4H vec3toVec4H_Unaligned(const Vec3 source)
{
	Vec4H highQuad, lowQuad, controlVect;
	highQuad = __lvx((unsigned char *)source, 0);
	controlVect = __lvsl((unsigned char *)source, 0);
	lowQuad = __lvx((unsigned char *)source, 16); 

	*(__vector4*)&highQuad = __vperm(highQuad, lowQuad, controlVect);
	return highQuad;
}

#define vec4toVec4H_aligned(v) __lvlx(&(v), 0)

#define vecPackS32S16H(V1, V2, R)		*(__vector4*)&(R) = __vpkswss((V1), (V2))
#define vecPackS16S8H(V1, V2, R)		*(__vector4*)&(R) = __vpkshss((V1), (V2))

