#pragma once
GCC_SYSTEM

#ifndef _SSEMATH_H_
#define _SSEMATH_H_

#if _PS3
#define isSSEavailable() 0
#define isSSE2available() 0
#else
// safe to call often, returns cached value
int isSSEavailable(void);
int isSSE2available(void);
// not safe to call often (for now)
int isSSE3available(void);
int isSSE4available(void);
extern int sseAvailable; // set after calling isSSEavailable or isSSE2available once
extern int sse2Available; // set after calling isSSEavailable or isSSE2available once
#endif

#if !PLATFORM_CONSOLE

#define _INC_MALLOC
#include <xmmintrin.h>
#undef _INC_MALLOC

typedef struct _sseVec3
{
	union
	{
		__m128 m;
		Vec3 v;
	};
} _sseVec3;

typedef struct _sseVec4
{
	union
	{
		__m128 m;
		Vec4 v;
	};
} _sseVec4;

#define sseVec3 __declspec(align(16)) _sseVec3
#define sseVec4 __declspec(align(16)) _sseVec4


#define declSSEop(op,num) \
__forceinline void sse_ ## op ## num (const _sseVec ## num *a, const _sseVec ## num *b, _sseVec ## num *res) \
{ \
	res->m = _mm_ ## op ## _ps(a->m, b->m); \
}

declSSEop(min,3)
declSSEop(min,4)
declSSEop(max,3)
declSSEop(max,4)
declSSEop(add,3)
declSSEop(add,4)
declSSEop(sub,3)
declSSEop(sub,4)
declSSEop(mul,3)
declSSEop(mul,4)

static __forceinline __m128 mulVecMat3SSE(__m128 v, const __m128 *mat)
{
	__m128 t, t2, t3;
	t3 = _mm_shuffle_ps(v, v, _MM_SHUFFLE(0, 0, 0, 0));
	t = _mm_mul_ps(t3, mat[0]);
	t3 = _mm_shuffle_ps(v, v, _MM_SHUFFLE(1, 1, 1, 1));
	t2 = _mm_mul_ps(t3, mat[1]);
	t = _mm_add_ps(t, t2);
	t3 = _mm_shuffle_ps(v, v, _MM_SHUFFLE(2, 2, 2, 2));
	t2 = _mm_mul_ps(t3, mat[2]);
	return _mm_add_ps(t, t2);
}

#undef declSSEop

#endif// _XBOX

#endif//_SSEMATH_H_

