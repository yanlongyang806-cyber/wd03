// Only included by Vec4H.h
#include "ssemath.h"
#include <xmmintrin.h>

typedef __m128 Vec4H;
typedef Vec4H *Vec4HP;
typedef Vec4H Mat34H[3];
typedef Vec4H Mat44H[4];
#define PVP(P) (&P)
#define RVP(PV) (*PV)

#define Vec4HToVec4(R) ((R).m128_f32)

#define setVec4H( R, X, Y, Z, W )		R = _mm_set_ps(W, Z, Y, X)
#define setIVec4H( R, X, Y, Z, W )		( R.m128_u32[0] = (X), R.m128_u32[1] = (Y), R.m128_u32[2] = (Z), R.m128_u32[3] = (W) )//( *(__m128i*)&R.m = _mm_setr_epi32( (X),(Y),(Z),(W) ) )
#define setVec4sameH( R, S )				R = _mm_set_ps1(S)
#define vecZero4H(R)						R = _mm_xor_ps(R, R)

#define vecRecip4H(V1)							_mm_rcp_ps(V1)

#define vecAddExp4H( V1, V2 )					_mm_add_ps(V1, V2)
#define vecSubExp4H( V1, V2 )					_mm_sub_ps(V1, V2)
#define vecMulExp4H( V1, V2 )					_mm_mul_ps(V1, V2)
#define vecOrExp4H( V1, V2 )					_mm_or_ps(V1, V2)
#define vecAndExp4H( V1, V2 )					_mm_and_ps(V1, V2)
#define vecSwizzleExp4H( V1, Swizzle )			_mm_shuffle_ps(V1, V1, Swizzle)
#define vecSplatXExp4H( V1 )					_mm_shuffle_ps(V1, V1, S_XXXX)
#define vecSplatYExp4H( V1 )					_mm_shuffle_ps(V1, V1, S_YYYY)
#define vecSplatZExp4H( V1 )					_mm_shuffle_ps(V1, V1, S_ZZZZ)
#define vecSplatWExp4H( V1 )					_mm_shuffle_ps(V1, V1, S_WWWW)

#define vecSwizWXYZExp4H( V1 )			vecSwizzleExp4H( V1, S_WXYZ )
#define vecSwizZWXYExp4H( V1 )			vecSwizzleExp4H( V1, S_ZWXY )
#define vecSwizYZWXExp4H( V1 )			vecSwizzleExp4H( V1, S_YZWX )

#define minVecExp4H( V1, V2 )			_mm_min_ps((V1), (V2))
#define maxVecExp4H( V1, V2 )			_mm_max_ps((V1), (V2))
#define addVec4H( V1, V2, R )			R = _mm_add_ps((V1), (V2))
#define subVec4H( V1, V2, R )			R = _mm_sub_ps((V1), (V2))
#define vecVecMul4H( V1, V2, R )		R = _mm_mul_ps((V1), (V2))
#define scaleAddVec4H( V1, VS, V2, R )	(R) = _mm_add_ps((V2), _mm_mul_ps((V1), (VS)))
#define scaleAddVec4SH( V1, S, V2, R )	{ Vec4H VS = { S, S, S, S }; R = _mm_add_ps((V2), _mm_mul_ps((V1), (VS))); }
#define scaleSubVec4H( V1, V2, VS, R )	(R) = _mm_sub_ps((V1), _mm_mul_ps((V2), (VS)))
#define vecNegate4H( V1, R )			subVec4H(g_vec4ZeroH, V1, R)

#define copyVec4H( V1, R )				(R) = (V1)
#define vecSelect4H( V1, V2, VS, R )	( R = _mm_or_ps( _mm_andnot_ps((VS), (V1)), _mm_and_ps((V2), (VS)) ) )
#define vecVecAnd4H( V1, V2, R )		R = _mm_and_ps( V1, V2 )
#define vecVecAndC4H( V1, V2, R )		R = _mm_andnot_ps( V2, V1 )
#define vecVecOr4H( V1, V2, R )					R = _mm_or_ps(V1, V2)
#define vecVecXor4H( V1, V2, R )				R = _mm_xor_ps(V1, V2)

#define vecCmpGreaterEqExp4H( V1, V2)			_mm_cmpge_ps(V1, V2)
#define vecCmpLessEqExp4H( V1, V2 )				_mm_cmple_ps(V1, V2)
#define vecCmpEq4H( V1, V2, R )					R = _mm_cmpeq_ps(V1, V2)
#define vecCmpNEq4H( V1, V2, R )				R = _mm_cmpneq_ps(V1, V2)

#define vecSplatX( V1, R )				R = _mm_shuffle_ps( V1, V1, _MM_SHUFFLE( 0, 0, 0, 0 ) )
#define vecSplatY( V1, R )				R = _mm_shuffle_ps( V1, V1, _MM_SHUFFLE( 1, 1, 1, 1 ) )
#define vecSplatZ( V1, R )				R = _mm_shuffle_ps( V1, V1, _MM_SHUFFLE( 2, 2, 2, 2 ) )
#define vecSplatW( V1, R )				R = _mm_shuffle_ps( V1, V1, _MM_SHUFFLE( 3, 3, 3, 3 ) )

static const union absMask
{
       S32 i[4];
       __m128 m;
} absMask = {0x7fffffff,0x7fffffff,0x7fffffff,0x7fffffff};

#define absVec4H( V1, R ) R = _mm_and_ps(V1,absMask.m)


__forceinline Vec4H vecFromScalarIntMem4H(const int * Scalar)
{
	Vec4H temp;
	temp = _mm_load_ss((const F32*)Scalar);
	return temp;
}

__forceinline Vec4H vecFromScalarInt4H(int Scalar)
{
	return vecFromScalarIntMem4H(&Scalar);
}

#define vecFloatFromScalarInt4H( I, R )		R = _mm_cvt_si2ss( R, I )

__forceinline Vec4H vecSameConstantMem4H(const float * Scalar)
{
	Vec4H temp;
	temp = _mm_load_ss(Scalar);
	vecSplatX(temp, temp);
	return temp;
}

__forceinline Vec4H vecFromScalar4H(float Scalar)
{
	return vecSameConstantMem4H(&Scalar);
}

__forceinline Vec4H vecSameConstant4H(float Scalar )
{
	Vec4H temp;
	temp = _mm_load_ss(&Scalar);
	vecSplatX(temp, temp);
	return temp;
}


// Mix takes the first two swizzle components form V1, the second two from V2
#define vecMixExp4H( V1, V2, Swizzle )			_mm_shuffle_ps((V1), (V2), Swizzle)
#define vecSwizzle4H( V1, Swizzle, R )			R = _mm_shuffle_ps((V1), (V1), Swizzle)

#include "Vec4Hswizzle.h"

#define vecReduceSym4H(V1, vec_symmetric_binary_operator, R)	{ Vec4H _temp_ = vecSwizZWXYExp4H(V1); vec_symmetric_binary_operator(V1, _temp_, _temp_); R = vecSwizYZWXExp4H(_temp_); vec_symmetric_binary_operator(R, _temp_, R); ; }
#define vecReduce4H(V1, vec_binary_operator, R)	vecReduceSym4H(V1, vec_binary_operator, R); vecSplatX(R, R)
#define vecSumComponents4H(V1, R)				vecReduce4H(V1, addVec4H, R)

#define dotVec4H(V1, V2, R)						vecVecMul4H(V1, V2, R); vecSumComponents4H(R, R)

#define vecAnyComponentSet4H(V1, R)				vecReduce4H(V1, vecVecOr4H, R)
#define vecAllComponentSet4H(V1, R)				vecReduce4H(V1, vecVecAnd4H, R)
#define vecTestZero4H(V1, R)					vecCmpEq4H(V1, g_vec4ZeroH, R); vecAllComponentSet4H(R, R)

static const Vec4H g_vec4UnitH = { 1.0f, 1.0f, 1.0f, 1.0f };

#define vecAnyNormalTo3H( V1, R ) \
{	\
	Vec4H _temp_normal_, _cmp_mask_;	\
	R = vecSubExp4H(vecSwizzleExp4H((V1), S_ZXYW), vecSwizzleExp4H((V1), S_YZXW));	\
	_temp_normal_ = vecMulExp4H(vecSwizzleExp4H((V1), S_YXWW), vecSwizzleExp4H(g_vec4UtilityH, S_XYWW));	\
	vecTestZero4H(R, _cmp_mask_);	\
	vecSelect4H(R, _temp_normal_, _cmp_mask_, R);	\
}

#define crossVec4H(V1, V2, R)					(R) = vecSubExp4H(	\
	vecMulExp4H(vecSwizzleExp4H((V1), S_YZXW), vecSwizzleExp4H((V2), S_ZXYW)),	\
	vecMulExp4H(vecSwizzleExp4H((V1), S_ZXYW), vecSwizzleExp4H((V2), S_YZXW)) )

// Consider vec3W0toVec4H
#define vec3toVec4H_Unaligned(source) _mm_loadu_ps(source)
#define vec4toVec4H_aligned(source) _mm_load_ps(source)

// SSE2:
// #define vecPackS32S16H(V1, V2, R)		*(__m128i*)&(R) = _mm_packs_epi32(*(__m128i*)&(V1), *(__m128i*)&(V2))
// #define vecPackS16S8H(V1, V2, R)		*(__m128i*)&(R) = _mm_packs_epi16(*(__m128i*)&(V1), *(__m128i*)&(V2))

#define vecPackS32S16H(V1, V2, R)	\
	((*(__m64*)&(R).m128_u16[0] = _mm_packs_pi32(*(__m64*)&(V1).m128_u32[0], *(__m64*)&(V1).m128_u32[2])), \
	(*(__m64*)&(R).m128_u16[4] = _mm_packs_pi32(*(__m64*)&(V2).m128_u32[0], *(__m64*)&(V2).m128_u32[2])))

#define vecPackS16S8H(V1, V2, R)	\
	((*(__m64*)&(R).m128_u8[0] = _mm_packs_pi16(*(__m64*)&(V1).m128_u16[0], *(__m64*)&(V1).m128_u16[4])), \
	(*(__m64*)&(R).m128_u8[8] = _mm_packs_pi16(*(__m64*)&(V2).m128_u16[0], *(__m64*)&(V2).m128_u16[4])))

__forceinline void transpose4Vec4H(Vec4HP const a,Vec4HP const b,Vec4HP const c,Vec4HP const d)
{
	_MM_TRANSPOSE4_PS(*a,*b,*c,*d);
}

// finishes 4 dot products simultaneously
__forceinline Vec4H sumComponents4Vec4H(Vec4HP a,Vec4HP b,Vec4HP c,Vec4HP d)
{
	Vec4H vResult;

	_MM_TRANSPOSE4_PS(*a,*b,*c,*d);
	addVec4H(*a,*b,vResult);
	addVec4H(vResult,*c,vResult);
	addVec4H(vResult,*d,vResult);

	return vResult;
}