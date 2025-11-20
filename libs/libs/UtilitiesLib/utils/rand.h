#pragma once
GCC_SYSTEM

#include "mathutil.h"

C_DECLARATIONS_BEGIN

#ifdef __cplusplus
// cplusplus does work to convert a U8 to an intrinsic bool, so just use U8s.
#define RAND_BOOL U8
#else
#define RAND_BOOL bool
#endif

// Here's the basic way this rand lib works. You can specify which type of PRNG to use, 
// which are listed and explained in the RandType enum
// You can also specify a seed, which will be used instead of the global seed. The global seed is per-RandType
// The Mersenne Twister is not easily reseeded, so do not use it with a seed unless you intend to generate a large number of
// numbers. The default choice is LCG with no seed, so the simplest functions call through to those settings.


typedef struct MersenneTable MersenneTable;


typedef enum RandType
{
	RandType_LCG, // Linear Congruential Generator: Fastest, least well distributed
	RandType_Mersenne, // Mersenne Twister: Relatively slow, every 624th call must reinit table, but is otherwise fast. Extremely good distribution
	RandType_BLORN, // Big List of Random Numbers... generated from mersenne, but is just a table lookup so always fast. Worst periodicity, but otherwise very well distributed.
	RandType_Mersenne_Static, // Same as Mersenne, but constant between runs
	RandType_BLORN_Static, // Same as BLORN, but constant between runs
} RandType;

void testRandomNumberGenerators(void);


void initRand(void);

#define BLORN_BITS 15
#define BLORN_MASK ((1<<(BLORN_BITS-1))-1)
extern U32* puiBLORN;


MersenneTable* mersenneTableCreate( U32 uiSeed );
void mersenneTableFree(MersenneTable* pTable);
void seedMersenneTable(MersenneTable* pTable, U32 uiSeed);


U32 randomStepSeed(U32* uiSeed, RandType eRandType);

// returns from 0 to 2^32-1
__forceinline static U32 randomU32Seeded(U32* uiSeed, RandType eRandType)
{
	return randomStepSeed(uiSeed, eRandType);
}

// returns from -2^31 to 2^31-1
__forceinline static int randomIntSeeded(U32* uiSeed, RandType eRandType)
{
	return (int)randomStepSeed(uiSeed, eRandType);
}

// returns from min (inclusive) to max (inclusive)
__forceinline static int randomIntRangeSeeded(U32* uiSeed, RandType eRandType, int min, int max)
{
	U32 randInt = randomU32Seeded(uiSeed, eRandType);
	randInt = randInt % (max + 1 - min);
	return (int)(randInt + min);
}

// returns between -1.0 and 1.0
__forceinline static F32 randomF32Seeded(U32* uiSeed, RandType eRandType)
{
	return ((S32)randomStepSeed(uiSeed, eRandType)) * (1.f / (F32)0x7fffffffUL);
}

// returns between -1.0 and 1.0
__forceinline static F32 randomF32BlornFixedSeed(U32 uiSeed)
{
	return ((S32)puiBLORN[uiSeed & BLORN_MASK]) * (1.f / (F32)0x7fffffffUL);
}

__forceinline static RAND_BOOL randomBoolBlornFixedSeed(U32 uiSeed)
{
	return (puiBLORN[uiSeed & BLORN_MASK] & 0x80);
}


// returns number in [0.0, 1.0)
__forceinline static F32 randomPositiveF32Seeded(U32* uiSeed, RandType eRandType)
{
	return (randomStepSeed(uiSeed, eRandType) * 0.999999f / (F32)0xffffffffUL);
}


// returns between 0.0 and 99.9999999
__forceinline static F32 randomPctSeeded(U32* uiSeed, RandType eRandType)
{
	return (randomStepSeed(uiSeed, eRandType) * 1.f / (F32)0x28f5c29UL);
}

// Returns a uniform distribution w.r.t. volume of a cube from -1,-1,-1 to 1,1,1
__forceinline static void randomVec3Seeded(U32* uiSeed, RandType eRandType, Vec3 vResult)
{
	vResult[0] = randomF32Seeded(uiSeed, eRandType);
	vResult[1] = randomF32Seeded(uiSeed, eRandType);
	vResult[2] = randomF32Seeded(uiSeed, eRandType);
}

#if _PS3
// Returns a uniform distribution w.r.t. volume of a cube from -1,-1,-1 to 1,1,1
__forceinline static vec_float4 random3Vec4HFastBlorn(void)
{
	static U32 seed = 0x8f8f8f8f;			// this function maintains its own seed
	vec_float4 r;
	float *f = (float *)&r;

	f[0] = ((S32)puiBLORN[(++seed) & BLORN_MASK]) * (1.f / (F32)0x7fffffffUL);
	f[1] = ((S32)puiBLORN[(++seed) & BLORN_MASK]) * (1.f / (F32)0x7fffffffUL);
	f[2] = ((S32)puiBLORN[(++seed) & BLORN_MASK]) * (1.f / (F32)0x7fffffffUL);

	return (r);
}

// Returns a uniform distribution w.r.t. volume of a cube from -1,-1,-1 to 1,1,1
__forceinline static vec_float4 random3Vec4HFastBlornSeed(U32 *seed)
{
	vec_float4 r;
	float *f = (float *)&r;

	f[0] = ((S32)puiBLORN[(++(*seed)) & BLORN_MASK]) * (1.f / (F32)0x7fffffffUL);
	f[1] = ((S32)puiBLORN[(++(*seed)) & BLORN_MASK]) * (1.f / (F32)0x7fffffffUL);
	f[2] = ((S32)puiBLORN[(++(*seed)) & BLORN_MASK]) * (1.f / (F32)0x7fffffffUL);

	return (r);
}
#endif

// returns true or false equally likely
__forceinline static RAND_BOOL randomBoolSeeded(U32* uiSeed, RandType eRandType)
{
	return (randomStepSeed(uiSeed, eRandType) & 0x80); // I chose this bit to get away from the LSB's, while remaining 1 byte (for 1 byte bools)
}

// Returns a uniform distribution w.r.t. volume of a spherical slice defined by 2 angles, theta and phi, and a radius R
// theta ranges from 0 to 2pi and phi from 0 to pi. If you pass in 2pi and pi, it will be uniformly distributed in the volume
// of the sphere. Another example is passing in pi and pi, which will create a hemisphere.
// Imagine an arc defined by +/- theta swept about the y-axis by phi


// Does not slice up the sphere or sphere shell
__forceinline static void randomSphereSeeded(U32* uiSeed, RandType eRandType, F32 fRadius, Vec3 vResult)
{
	sphericalCoordsToVec3(
		vResult,
		randomPositiveF32Seeded(uiSeed, eRandType) * TWOPI,
		acosf(randomF32Seeded(uiSeed, eRandType)), // Note the arccos, this is to correct for bunching at the poles, since spherical coordinates are not uniform across the sphere
		powf(randomPositiveF32Seeded(uiSeed, eRandType), 0.33333333f) * fRadius
		);
}
__forceinline static void randomSphereShellSeeded(U32* uiSeed, RandType eRandType, F32 fRadius, Vec3 vResult)
{
	sphericalCoordsToVec3(
		vResult,
		randomPositiveF32Seeded(uiSeed, eRandType) * TWOPI,
		acosf(randomF32Seeded(uiSeed, eRandType)), // Note the arccos, this is to correct for bunching at the poles, since spherical coordinates are not uniform across the sphere
		fRadius
		);
}

// The same, but the radius is not random
// normalized phi where 180 degrees is 1.0f.  This is to avoid floating point errors
__forceinline static void randomSphereShellSliceSeeded(U32* uiSeed, RandType eRandType, F32 fTheta, F32 fPhi, F32 fRadius, Vec3 vResult)
{
#ifdef _FULLDEBUG
	assert(fPhi <= (F32)PI && fPhi >= (F32)-PI);
#endif
	sphericalCoordsToVec3(
		vResult,
		randomF32Seeded(uiSeed, eRandType) * fTheta,
		acosf(randomF32Seeded(uiSeed, eRandType) * fPhi), // Note the arccos, this is to correct for bunching at the poles, since spherical coordinates are not uniform across the sphere
		fRadius
		);
}

__forceinline static void randomSphereSliceSeeded(U32* uiSeed, RandType eRandType, F32 fTheta, F32 fPhi, F32 fRadius, Vec3 vResult)
{
#ifdef _FULLDEBUG
	assert(fPhi <= (F32)PI && fPhi >= (F32)-PI);
#endif
	sphericalCoordsToVec3(
		vResult,
		randomF32Seeded(uiSeed, eRandType) * fTheta,
		acosf(randomF32Seeded(uiSeed, eRandType) * fPhi * ONEOVERPI), // Note the arccos, this is to correct for bunching at the poles, since spherical coordinates are not uniform across the sphere
		powf(randomPositiveF32Seeded(uiSeed, eRandType), 0.33333333f) * fRadius
		);
}



///
/// The functions below use the global seed and assume LCG, for simplicity
///
// returns from 0 to 2^32-1
__forceinline static U32 randomU32(void)
{
	return randomU32Seeded(NULL, RandType_LCG);
}

// returns from -2^31 to 2^31-1
__forceinline static int randomInt(void)
{
	return randomIntSeeded(NULL, RandType_LCG);
}

// returns from min (inclusive) to max (inclusive)
__forceinline static int randomIntRange(int min, int max)
{
	U32 randInt = randomU32Seeded(NULL, RandType_LCG);
	randInt = randInt % (max + 1 - min);
	return (int)(randInt + min);
}

// returns between -1.0 and 1.0
__forceinline static F32 randomF32(void)
{
	return randomF32Seeded(NULL, RandType_LCG);
}

// returns between [0.0, 1.0)
__forceinline static F32 randomPositiveF32(void)
{
	return randomPositiveF32Seeded(NULL, RandType_LCG);
}


// returns between 0.0 and 99.999999
__forceinline static F32 randomPct(void)
{
	return randomPctSeeded(NULL, RandType_LCG);
}

// Returns a uniform distribution w.r.t. volume of a cube from -1,-1,-1 to 1,1,1
__forceinline static void randomVec3(Vec3 vResult)
{
	randomVec3Seeded(NULL, RandType_LCG, vResult);
}

// returns true or false equally likely
__forceinline static RAND_BOOL randomBool(void)
{
	return randomBoolSeeded(NULL, RandType_LCG);
}

// Returns uniformly distributed (w.r.t. volume) numbers in a sphere
__forceinline static void randomSphere(F32 fRadius, Vec3 vResult)
{
	randomSphereSeeded(NULL, RandType_LCG, fRadius, vResult);
}

// Return uniformly distributed (w.r.t. area) numbers on a spherical shell
__forceinline static void randomSphereShell(F32 fRadius, Vec3 vResult)
{
	randomSphereShellSeeded(NULL, RandType_LCG, fRadius, vResult);
}

// Returns uniformly distributed (w.r.t. volume) numbers in a sphere slice
__forceinline static void randomSphereSlice(F32 fTheta, F32 fPhi, F32 fRadius, Vec3 vResult)
{
	randomSphereSliceSeeded(NULL, RandType_LCG, fTheta, fPhi, fRadius, vResult);
}

// Return uniformly distributed (w.r.t. area) numbers on a spherical shell slice
// normalized phi where 180 degrees is 1.0f.  This is to avoid floating point errors
__forceinline static void randomSphereShellSlice(F32 fTheta, F32 fPhi, F32 fRadius, Vec3 vResult)
{
	randomSphereShellSliceSeeded(NULL, RandType_LCG, fTheta, fPhi, fRadius, vResult);
}


//
// Mersenne functions are a bit different, since you pass in a table
//
U32 randomStepSeedMersenne(MersenneTable* pTable);

// returns from 0 to 2^32-1
__forceinline static U32 randomMersenneU32(MersenneTable* pTable)
{
	return randomStepSeedMersenne(pTable);
}

// returns from -2^31 to 2^31-1
__forceinline static int randomMersenneInt(MersenneTable* pTable)
{
	return (int)randomStepSeedMersenne(pTable);
}

// returns from min (inclusive) to max (inclusive)
__forceinline static int randomMersenneIntRange(MersenneTable* pTable, int min, int max)
{
	U32 randInt = randomMersenneU32(pTable);
	randInt = randInt % (max + 1 - min);
	return (int)(randInt + min);
}

// returns between -1.0 and 1.0
__forceinline static F32 randomMersenneF32(MersenneTable* pTable)
{
	return -1.f + (randomStepSeedMersenne(pTable) * 1.f / (F32)0x7fffffffUL);
}

// returns number in [0.0, 1.0)
__forceinline static F32 randomMersennePositiveF32(MersenneTable* pTable)
{
	return (randomStepSeedMersenne(pTable) * 0.999999f / (F32)0xffffffffUL);
}


// returns between 0.0 and 99.9999999
__forceinline static F32 randomMersennePct(MersenneTable* pTable)
{
	return (randomStepSeedMersenne(pTable) * 1.f / (F32)0x28f5c29UL);
}

// Returns a uniform distribution w.r.t. volume of a cube from -1,-1,-1 to 1,1,1
__forceinline static void randomMersenneVec3(MersenneTable* pTable, Vec3 vResult)
{
	vResult[0] = randomMersenneF32(pTable);
	vResult[1] = randomMersenneF32(pTable);
	vResult[2] = randomMersenneF32(pTable);
}


// returns true or false equally likely
__forceinline static RAND_BOOL randomMersenneBool(MersenneTable* pTable)
{
	return (randomStepSeedMersenne(pTable) & 0x80); // I chose this bit to get away from the LSB's, while remaining 1 byte (for 1 byte bools)
}

// Returns a uniform distribution w.r.t. volume of a spherical slice defined by 2 angles, theta and phi, and a radius R
// theta ranges from 0 to 2pi and phi from 0 to pi. If you pass in 2pi and pi, it will be uniformly distributed in the volume
// of the sphere. Another example is passing in pi and pi, which will create a hemisphere.
// Imagine an arc defined by +/- theta swept about the y-axis by phi


// Does not slice up the sphere or sphere shell
__forceinline static void randomMersenneSphere(MersenneTable* pTable, F32 fRadius, Vec3 vResult)
{
	sphericalCoordsToVec3(
		vResult,
		randomMersennePositiveF32(pTable) * TWOPI,
		acosf(randomMersenneF32(pTable)), // Note the arccos, this is to correct for bunching at the poles, since spherical coordinates are not uniform across the sphere
		powf(randomMersennePositiveF32(pTable), 0.33333333f) * fRadius
		);
}
__forceinline static void randomMersenneSphereShell(MersenneTable* pTable, F32 fRadius, Vec3 vResult)
{
	sphericalCoordsToVec3(
		vResult,
		randomMersennePositiveF32(pTable) * TWOPI,
		acosf(randomMersenneF32(pTable)), // Note the arccos, this is to correct for bunching at the poles, since spherical coordinates are not uniform across the sphere
		fRadius
		);
}

// The same, but the radius is not random
__forceinline static void randomMersenneSphereShellSlice(MersenneTable* pTable, F32 fTheta, F32 fPhi, F32 fRadius, Vec3 vResult)
{
#ifdef _FULLDEBUG
	assert(fPhi <= (F32)PI && fPhi >= (F32)-PI);
#endif
	sphericalCoordsToVec3(
		vResult,
		randomMersenneF32(pTable) * fTheta,
		acosf(randomMersenneF32(pTable) * fPhi * ONEOVERPI), // Note the arccos, this is to correct for bunching at the poles, since spherical coordinates are not uniform across the sphere
		fRadius
		);
}

__forceinline static void randomMersenneSphereSlice(MersenneTable* pTable, F32 fTheta, F32 fPhi, F32 fRadius, Vec3 vResult)
{
#ifdef _FULLDEBUG
	assert(fPhi <= (F32)PI && fPhi >= (F32)-PI);
#endif
	sphericalCoordsToVec3(
		vResult,
		randomMersenneF32(pTable) * fTheta,
		acosf(randomMersenneF32(pTable) * fPhi * ONEOVERPI), // Note the arccos, this is to correct for bunching at the poles, since spherical coordinates are not uniform across the sphere
		powf(randomMersennePositiveF32(pTable), 0.33333333f) * fRadius
		);
}

__forceinline static void randomMersennePermuteN(MersenneTable *random_table, int *vec, int N, int stride)
{
	int i, j;
	for (i = 0; i < N-1; i++)
	{
		int idx = ((randomMersenneF32(random_table)*0.5f)+0.5f) * (N-i);
		for (j = 0; j < stride; j++)
		{
			int swp = vec[(idx+i)*stride+j];
			vec[(idx+i)*stride+j] = vec[i*stride+j];
			vec[i*stride+j] = swp;
		}
	}
}

__forceinline static void randomMersennePermuteFloatN(MersenneTable *random_table, F32 *vec, int N, int stride)
{
	int i, j;
	for (i = 0; i < N-1; i++)
	{
		int idx = ((randomMersenneF32(random_table)*0.5f)+0.5f) * (N-i);
		for (j = 0; j < stride; j++)
		{
			F32 swp = vec[(idx+i)*stride+j];
			vec[(idx+i)*stride+j] = vec[i*stride+j];
			vec[i*stride+j] = swp;
		}
	}
}

__forceinline static void randomMersennePermuteArray(MersenneTable *random_table, void **vec, int N)
{
	int i;
	for (i = 0; i < N-1; i++)
	{
		int idx = ((randomMersenneF32(random_table)*0.5f)+0.5f) * (N-i);
		void *swp = vec[idx+i];
		vec[idx+i] = vec[i];
		vec[i] = swp;
	}
}

__forceinline static void randomMersennePermutation4(MersenneTable *random_table, int vec[4])
{
	setVec4(vec, 0, 1, 2, 3);
	randomMersennePermuteN(random_table, vec, 4, 1);
}

// Two gaussian random numbers are generated with each call. The second one can optionally be used as well. 
__forceinline static F32 randomGaussian(MersenneTable *random_table, F32 *pfGauss, F32 fMode, F32 fSigma)
{
	F32 x, y, r2;
	do
	{
		x = randomMersenneF32(random_table);
		y = randomMersenneF32(random_table);
		r2 = x * x + y * y;
	} while (r2 > 1.0 || r2 == 0);

	if (pfGauss)
	{
		*pfGauss = (x * sqrt(-2.0 * log(r2) / r2)) * fSigma + fMode;
	}

	return (y * sqrt(-2.0 * log(r2) / r2)) * fSigma + fMode;
}










// Implementation of improved perlin noise
// noise has range -1 to 1

// get noise at a single frequency band
F32 noiseGetBand2D( F32 x, F32 y, F32 wavelength );
F32 noiseGetBand3D( F32 x, F32 y, F32 z, F32 wavelength );

// get a combination of wavelength bands from min_wavelength to max_wavelength
// amplitude of each frequency is 1 / f^falloff_rate, so falloff_rate = 0 is white noise
F32 noiseGetFullSpectrum2D( F32 x, F32 y, F32 min_wavelength, F32 max_wavelength, F32 falloff_rate );
F32 noiseGetFullSpectrum3D( F32 x, F32 y, F32 z, F32 min_wavelength, F32 max_wavelength, F32 falloff_rate );


U32 randomWeightedArrayIndex(F32* pfNormalizedWeights, U32 uiWeightCount);



void randomPoissonSphereShellSeeded(U32* uiSeed, RandType eRandType, F32 fRadius, U32 uiCount, F32 fQuality, Vec3 *pPoints);
void randomPoissonSphereShellSpatialSeeded(U32* uiSeed, RandType eRandType, F32 fRadius, U32 uiWidth, U32 uiHeight, Vec3 *pPoints);

// Returns the sum of iDieCount random values in the range [1-iDieSize]
int randomDieRolls(int iDieCount, int iDieSize);
int randomDieRollsSBLORNSeeded(U32* uiSeed, int iDieCount, int iDieSize);

C_DECLARATIONS_END
