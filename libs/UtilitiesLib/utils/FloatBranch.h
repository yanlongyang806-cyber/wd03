#pragma once
GCC_SYSTEM

#ifndef FLOATBRANCH_H
#define FLOATBRANCH_H

#define fseli( COMPARAND, GEZ, LZ ) ( (COMPARAND) >= 0.0f ? (GEZ) : (LZ) )
#define fsel2i( LHS, RHS, GEZ, LZ ) ( (LHS) >= (RHS) ? (GEZ) : (LZ) )

#if _XBOX
#include <ppcintrinsics.h>

typedef F32 FloatBranchCount;
#define FloatBranch FloatBranchFGE
#define fsel( Comparand, GEZ, LZ ) __fself( (Comparand), (GEZ), (LZ) )
#define fsel2( LHS, RHS, GEZ, LZ ) __fself( (LHS) - (RHS), (GEZ), (LZ) )
#else
typedef int FloatBranchCount;
#define FloatBranch FloatBranchIGE
#define fsel( Comparand, GEZ, LZ ) ( (Comparand) >= 0.0f ? (GEZ) : (LZ) )
#define fsel2( LHS, RHS, GEZ, LZ ) ( (LHS) - (RHS) >= 0.0f ? (GEZ) : (LZ) )
#endif

/*
The float branch functions choose between two outputs based on the result of
a comparison of two floating-point values. In the basic case, the test is if
a float variable is greater-than or equal-to zero. On platforms (SSE, Xbox) 
that support branch-less float operations, this can be a significant speed 
up.

float FloatOutput = FloatVar >= 0.0f ? FloatResultGEZ : FloatResultLZ;

Example - min component of a vector:

Vec3 v;
// minimum = v[ 0 ] <= v[ 1 ] ? v[ 0 ] : v[ 1 ];
float minimum = FloatBranchLE( v[ 0 ], v[ 1 ], v[ 0 ], v[ 1 ] );
// minimum = minimum <= v[ 2 ] ? minimum : v[ 2 ];
minimum = FloatBranchLE( minimum, v[ 2 ], minimum, v[ 2 ] );

This translates into the load instructions to access the vector elements, plus
two subtract instructions to get convert the <=  comparison to a comparison against zero,
and two fsel instructions to choose the lesser value depending on the result of the 
comparison. This may be more or less instructions than the branch, but there is a 
30-cycle latency for the result of the float compare instruction.

A simpler alternative is using the MINF wrapper:

float minimum = MINF( v[ 0 ], MINF( v[ 1 ], v[ 2 ] ) );

Using the branch-less float operations is only an optmization if you can keep 
the results in a float variable. You can still get integer results at the end 
of a series of computations. However, floats can only exactly represent integers
up to 24 bits, so there are some limitations.
*/

// This function selects the first float value, t, if the test value, Comparand, is greater-than or equal-to zero,
// or the value f if Comparand is less-than zero.
// float result = Comparand >= 0.0f ? t : f;
__forceinline static F32 FloatBranchFGEZ(F32 Comparand, F32 t, F32 f)
{
	return fsel(Comparand, t, f);
}

// int Result = Comparand >= 0.0f ? t : f;
__forceinline static int FloatBranchIGEZ(F32 Comparand, int t, int f)
{
	return Comparand >= 0.0f ? t : f;
}

// lhs >= rhs ? t : f
__forceinline static F32 FloatBranchFGE(F32 lhs, F32 rhs, F32 t, F32 f)
{
	return fsel2(lhs, rhs, t, f);
}

__forceinline static int FloatBranchIGE(F32 lhs, F32 rhs, int t, int f)
{
	// lhs >= rhs ? t : f
	return fsel2i(lhs, rhs, t, f);
}

__forceinline static F32 FloatBranchFG(F32 lhs, F32 rhs, F32 t, F32 f)
{
	// lhs > rhs ? t : f  <==> rhs >= lhs ? f : t
	// lhs <= rhs ? f : t
	// rhs >= lhs ? f : t
	return fsel2(rhs, lhs, f, t);
}

__forceinline static int FloatBranchIG(F32 lhs, F32 rhs, int t, int f)
{
	// lhs > rhs ? t : f  <==> rhs >= lhs ? f : t
	return fsel2i(rhs, lhs, f, t);
}

__forceinline static F32 FloatBranchFE(F32 lhs, F32 rhs, F32 t, F32 f)
{
	// lhs == rhs ? t : f  <==> lhs > rhs ? f : ( rhs > lhs ? f : t )
	return FloatBranchFG( lhs, rhs, f, FloatBranchFG( rhs, lhs, f, t ) );
}

__forceinline static int FloatBranchIE(F32 lhs, F32 rhs, int t, int f)
{
	// lhs > rhs ? t : f  <==> rhs >= lhs ? f : t
	return FloatBranchIG( lhs, rhs, f, FloatBranchIG( rhs, lhs, f, t ) );
}

__forceinline static F32 FloatBranchFL(F32 lhs, F32 rhs, F32 t, F32 f)
{
	// lhs < rhs ? t : f <==> lhs >= rhs ? f : t
	return fsel2(lhs, rhs, f, t);
}

__forceinline static int FloatBranchIL(F32 lhs, F32 rhs, int t, int f)
{
	// lhs < rhs ? t : f <==> rhs >= lhs ? t : f
	return fsel2i(lhs, rhs, f, t);
}

__forceinline static F32 FloatBranchFLE(F32 lhs, F32 rhs, F32 t, F32 f)
{
	// lhs <= rhs ? t : f <==> rhs >= lhs ? t : f
	return fsel2(rhs, lhs, t, f);
}

__forceinline static int FloatBranchILE(F32 lhs, F32 rhs, int t, int f)
{
	// lhs <= rhs ? t : f <==> rhs >= lhs ? t : f
	return fsel2i(rhs, lhs, t, f);
}

// !FLOATBRANCH_H
#endif
