#include "rand.h"
#include "Error.h"
#include "ScratchStack.h"

AUTO_RUN_ANON(memBudgetAddMapping(__FILE__, BUDGET_EngineMisc););


// PLEASE, never touch these seeds! They get initialized once, and should never be changed. If you want to generate numbers with a repeatable seed, then pass in your own seed pointer.
static U32 uiGlobalLCGSeed = 0x8f8f8f8f;
static U32 uiGlobalBLORNSeed = 0x8f8f8f8f;
static U32 uiGlobalStaticBLORNSeed = 0x8f8f8f8f;



struct MersenneTable
{
	U32  table[624];
	U32  uiCount;
	U32  uiSeed;
	bool bInitialized;
};

static MersenneTable globalMersenneTable;
static MersenneTable globalStaticMersenneTable;


void testRandomNumberGenerators()
{
	/*
	U32 uiTestNum = randomU32();
	U32 uiNum = randomU32();
	U32 uiCount = 0;
	while (uiNum != uiTestNum)
	{
		uiCount++;
		uiNum = randomU32();
	}

	printf("Period is %u numbers\n", uiCount);
	*/
}

U32* puiBLORN;
U32* puiStaticBLORN;

void generateBLORN()
{
	U32 uiBLORNSize = 0x1 << (BLORN_BITS-1);
	U32 uiIndex;
	puiBLORN = malloc(sizeof(U32) * uiBLORNSize);
	for (uiIndex=0; uiIndex<uiBLORNSize; ++uiIndex)
	{
		puiBLORN[uiIndex] = randomU32Seeded(NULL, RandType_Mersenne);
	}
	uiGlobalBLORNSeed = randomU32Seeded(NULL, RandType_Mersenne);

	puiStaticBLORN = malloc(sizeof(U32) * uiBLORNSize);
	for (uiIndex=0; uiIndex<uiBLORNSize; ++uiIndex)
	{
		puiStaticBLORN[uiIndex] = randomU32Seeded(NULL, RandType_Mersenne_Static);
	}
	uiGlobalStaticBLORNSeed = randomU32Seeded(NULL, RandType_Mersenne_Static);
}


// Noise calculation
#define NOISE_PATCH_SIZE 64
static int noise_permutation[NOISE_PATCH_SIZE];
static F32 noise_field[NOISE_PATCH_SIZE];
static bool noise_initialized = false;
static U32 noise_seed = 0;

static void initializeNoise( void )
{
	int i;
	for (i = 0; i < NOISE_PATCH_SIZE; i++)
	{
		noise_permutation[i] = (int)(randomPositiveF32Seeded( &noise_seed, RandType_LCG) * NOISE_PATCH_SIZE);
		noise_field[i] = randomF32Seeded( &noise_seed, RandType_LCG);
	}
	noise_initialized = true;
}

__forceinline int getNoisePermutation2D( int x, int y )
{
	return noise_permutation[(noise_permutation[y & (NOISE_PATCH_SIZE-1)] + x) & (NOISE_PATCH_SIZE-1)];
}

__forceinline int getNoisePermutation3D( int x, int y, int z )
{
	int perm;
	perm = noise_permutation[z          & (NOISE_PATCH_SIZE-1)];
	perm = noise_permutation[(perm + y) & (NOISE_PATCH_SIZE-1)];
	perm = noise_permutation[(perm + x) & (NOISE_PATCH_SIZE-1)];
	return perm;
}

__forceinline F32 getNoiseInterp( F32 t )
{
	return t * t * t * (t * (t * 6 - 15) + 10);
}

__forceinline F32 bilinearInterp( F32 values[4], F32 alpha, F32 beta )
{
	F32 lerpa = values[0]*(1.0f - alpha) + values[1]*alpha;
	F32 lerpb = values[2]*(1.0f - alpha) + values[3]*alpha;
	return lerpa*(1.0f - beta) + lerpb*beta;
}

__forceinline F32 trilinearInterp( F32 values[8], F32 alpha, F32 beta, F32 gamma )
{
	return bilinearInterp(values, alpha, beta)*(1.0f - gamma) + 
		   bilinearInterp(&(values[4]), alpha, beta)*gamma;
}

F32 noiseGetBand2D( F32 x, F32 y, F32 wavelength )
{
	F32 noise_values[4];
	F32 frequency = 1.0f / wavelength;
	int floorx = floorf(fabsf(x) * frequency );
	int floory = floorf(fabsf(y) * frequency );
	F32 alpha = getNoiseInterp(fabsf(x) * frequency - floorx);
	F32 beta = getNoiseInterp(fabsf(y) * frequency - floory);
	int i;
	for (i = 0; i < 4; i++)
	{
		noise_values[i] = noise_field[ getNoisePermutation2D( floorx + i%2, floory + (i>>1)%2) ];
	}
	return bilinearInterp( noise_values, alpha, beta );
}

F32 noiseGetBand3D( F32 x, F32 y, F32 z, F32 wavelength )
{
	F32 noise_values[8];
	F32 frequency = 1.0f / wavelength;
	int floorx = floorf(fabsf(x) * frequency );
	int floory = floorf(fabsf(y) * frequency );
	int floorz = floorf(fabsf(z) * frequency );
	F32 alpha = getNoiseInterp(fabsf(x) * frequency - floorx);
	F32 beta = getNoiseInterp(fabsf(y) * frequency - floory);
	F32 gamma = getNoiseInterp(fabsf(z) * frequency - floorz);
	int i;
	for (i = 0; i < 8; i++)
	{
		noise_values[i] = noise_field[ getNoisePermutation3D( floorx + i%2, floory + (i>>1)%2, floorz + (i>>2)%2 ) ];
	}
	return trilinearInterp( noise_values, alpha, beta, gamma );
}

F32 noiseGetFullSpectrum2D( F32 x, F32 y, F32 min_wavelength, F32 max_wavelength, F32 falloff_rate )
{
	F32 wavelength = max_wavelength;
	F32 sum = 0;
	while (wavelength > min_wavelength)
	{
		F32 band = noiseGetBand2D( x, y, wavelength );
		band *= 1.0f / powf(max_wavelength / wavelength, falloff_rate);
		sum += band;
		wavelength /= 2.0f;
	}	
	return sum;
}

F32 noiseGetFullSpectrum3D( F32 x, F32 y, F32 z, F32 min_wavelength, F32 max_wavelength, F32 falloff_rate )
{
	F32 wavelength = max_wavelength;
	F32 sum = 0;
	while (wavelength > min_wavelength)
	{
		F32 band = noiseGetBand3D( x, y, z, wavelength );
		band *= 1.0f / powf(max_wavelength / wavelength, falloff_rate);
		sum += band;
		wavelength /= 2.0f;
	}	
	return sum;
}

// Mersenne twister initialization


void seedMersenneTable(MersenneTable* pTable, U32 uiSeed)
{
	int i;
	U32 x;

	if (uiSeed)
		pTable->uiSeed = uiSeed;
	if (!pTable->bInitialized)
	{
		pTable->table[0] = pTable->uiSeed;
		for (i = 1; i < 624; i++)
		{
			pTable->table[i] = 69069 * pTable->table[i-1] + 1;
		}
		pTable->bInitialized = true;
	}

	for (i = 0; i < 623; i++)
	{
		x = (0x80000000 & pTable->table[i]) | (0x7fffffff & pTable->table[i+1]);
		if (x & 0x1) // x odd?
			pTable->table[i] = pTable->table[(i + 397) % 624] ^ (x >> 1) ^ (U32)(2567483615);
        else
			pTable->table[i] = pTable->table[(i + 397) % 624] ^ (x >> 1);
	}
	x = (0x80000000 & pTable->table[623]) | (0x7fffffff & pTable->table[0]);
	if (x & 0x1) // x odd?
		pTable->table[623] = pTable->table[396] ^ (x >> 1) ^ (U32)(2567483615);
    else
		pTable->table[623] = pTable->table[396] ^ (x >> 1);

	pTable->uiCount = 1;
}

MersenneTable* mersenneTableCreate( U32 uiSeed )
{
	MersenneTable* pTable = calloc(1,sizeof(MersenneTable));
	pTable->bInitialized = false;
	seedMersenneTable(pTable, uiSeed);
	return pTable;
}

void mersenneTableFree(MersenneTable* pTable)
{
	free(pTable);
}


// This is the core of the random library... we might want to rearchitect this to be faster at some point
U32 randomStepSeed(U32* uiSeed, RandType eRandType)
{
	MersenneTable *table;
	switch( eRandType )
	{
		case RandType_LCG:
		{
			// A (possibly incorrect) attempt to get around the LSB's being poorly distributed in a linear congruent PRNG, by using the MSB's from one cycle
			// as the LSB of the next, and running two cycles per step, such that one cycle is hidden.
			// For example, without this, seeds would always alternate between even and odd
			U32* uiLCGSeed = uiSeed?uiSeed:&uiGlobalLCGSeed;
			U32 uiTemp = (*uiLCGSeed * 214013L + 2531011L) & 0xFFFFFFFF;
			*uiLCGSeed = ((uiTemp * 214013L + 2531011L) & 0xFFFF0000) >> 16 | (uiTemp & 0xFFFF0000);
			return *uiLCGSeed;
		}
		case RandType_Mersenne:
		case RandType_Mersenne_Static:
		{
			// Mersenne twister implementation
			// every 624th call the function will be somewhat slower, as it generates 624 untempered 
			// values in one shot, then performs some swizzling to get the tempered value
			U32 out;
			if ( uiSeed )
			{
				Errorf("You can't pass a seed with RandType_Mersenne! Instead, use mersenneTableCreate() to create a mersenne table, and then pass that pointer to randomMersenneX()!");
				return 0;
			}

			if (eRandType == RandType_Mersenne)
					table = &globalMersenneTable;
			else
					table = &globalStaticMersenneTable;

			if (table->uiCount == 0)
			{
				seedMersenneTable(table, 0);
			}

			out = table->table[table->uiCount];
			out ^= out >> 11;
			out ^= (out << 7) & (U32)(2636928640);
			out ^= (out << 15) & (U32)(4022730752);
			out ^= out >> 18;
			table->uiCount = (table->uiCount + 1) % 624;
			return out;
		}
		case RandType_BLORN:
		{
			U32* uiBLORNSeed = uiSeed?uiSeed:&uiGlobalBLORNSeed;
			return puiBLORN[(++(*uiBLORNSeed)) & BLORN_MASK];
		}
		case RandType_BLORN_Static:
		{
			U32* uiBLORNSeed = uiSeed?uiSeed:&uiGlobalStaticBLORNSeed;
			return puiStaticBLORN[(++(*uiBLORNSeed)) & BLORN_MASK];
		}
	}
	Errorf("Unsupported RandType: %d\n", eRandType);
	return 0;
}

U32 randomStepSeedMersenne(MersenneTable* pTable)
{
	// Mersenne twister implementation
	// every 624th call the function will be somewhat slower, as it generates 624 untempered 
	// values in one shot, then performs some swizzling to get the tempered value
	U32 out;
	if (!pTable)
		pTable = &globalMersenneTable;


	if (pTable->uiCount == 0)
		seedMersenneTable(pTable, 0);

	out = pTable->table[pTable->uiCount];
	out ^= out >> 11;
	out ^= (out << 7) & (U32)(2636928640);
	out ^= (out << 15) & (U32)(4022730752);
	out ^= out >> 18;
	pTable->uiCount = (pTable->uiCount + 1) % 624;
	return out;
}

// Given uiWeightCount normalized F32s (normalized means they add up to 1.0), this will return a random array index with those weights in its distribution
// if the numbers are not normalized, it will return some non-negative index below uiWeightCount, but it won't be properly distributed. please be careful
U32 randomWeightedArrayIndex(F32* pfNormalizedWeights, U32 uiWeightCount)
{
	F32 fProbabilityTotal = 0.0f;
	F32 fRandomValue = randomPositiveF32();
	U32 uiIndex = 0;
	assert(uiWeightCount > 0);
	// Loop through all weights
	while (uiIndex < uiWeightCount)
	{
		fProbabilityTotal += pfNormalizedWeights[uiIndex]; // keep track of the total fraction of the weight-space we've covered
		if (fRandomValue <= fProbabilityTotal) // the first total that exceeds the uniform 0-1 float we chose is our result
			return uiIndex;
		++uiIndex;
	}
	return uiWeightCount-1;
}



void initRand()
{
	time_t iTime;
	globalMersenneTable.bInitialized = false;
	seedMersenneTable(&globalMersenneTable, (U32)time(&iTime));
	seedMersenneTable(&globalStaticMersenneTable, 0x1459ABCD);

	generateBLORN();
	initializeNoise();
	uiGlobalBLORNSeed = (U32)time(&iTime);
	uiGlobalLCGSeed = (U32)time(&iTime);
}


// This function calculates a set of points on a sphere that fit a spherical shell Poisson distribution.
// All points are guaranteed to be no closer than some arc length away from each other, determined by the quality factor.
// CD: there are definitely faster algorithms out there for this, but this is all I need for now, so...
void randomPoissonSphereShellSeeded(U32* uiSeed, RandType eRandType, F32 fRadius, U32 uiCount, F32 fQuality, Vec3 *pPoints)
{
	U32 i, j;
	F32 fCosMinArcLength;

	// derivation of the min arc length:
	// 1. divide area of unit sphere by the number of points to get how much area each point should take up = 4 * PI / uiCount
	// 2. assume that is the area of a unit disk and convert to radius = sqrtf(4 / uiCount)
	// 3. multiply by two to get the diameter = 2 * sqrtf(4 / uiCount)
	// 4. multiply by quality for some wiggle room = fQuality * 2 * sqrtf(4 / uiCount)

	fQuality = CLAMP(fQuality, 0.01f, 0.9f);
	fCosMinArcLength = cosf(fQuality * 2.f * sqrtf(4.f / uiCount));

	for (i = 0; i < uiCount; ++i)
	{
		U32 uiRetryCounter = 0;
		bool bValid = false;

		while (!bValid)
		{
			uiRetryCounter++;

			randomSphereShellSeeded(uiSeed, eRandType, 1, pPoints[i]); // use radius of 1 to simplify calculations, points will be scaled later
			bValid = true;

			// if the retry counter gets too high, give up and accept the latest value
			if (uiRetryCounter < 60000)
			{
				for (j = 0; j < i; ++j)
				{
					F32 fCosArcLength = dotVec3(pPoints[j], pPoints[i]);
					if (fCosArcLength > fCosMinArcLength)
					{
						bValid = false;
						break;
					}
				}
			}
		}
	}

	if (fRadius != 1)
	{
		// scale to desired radius
		for (i = 0; i < uiCount; ++i)
		{
			scaleVec3(pPoints[i], fRadius, pPoints[i]);
		}
	}
}

// This function calculates a set of points on a sphere that fit a spherical shell Poisson distribution.
// All points are guaranteed to be no closer than some arc length away from each other, determined by the quality factor.
// This function differs from the non-spatial version in that it places the calculated points in a 2d texture,
// trying to maintain the additional constraint that points falling close to each other in the texture should themselves fit
// in a Poisson distribution.  It does this by varying the maximum arc length by the distance between to texture coordinates.
void randomPoissonSphereShellSpatialSeeded(U32* uiSeed, RandType eRandType, F32 fRadius, U32 uiWidth, U32 uiHeight, Vec3 *pPoints)
{
	U32 i, curX, curY, j, x, y, uiCount, uiMaxDistance, uiHalfWidth = uiWidth / 2, uiHalfHeight = uiHeight / 2;
	F32 *pfCosMinArcLengths;
	F32 fQuality = 0.5f, fMaxDistance;

	fMaxDistance = sqrtf(SQR(uiHalfWidth) + SQR(uiHalfHeight));
	uiMaxDistance = round(ceil(fMaxDistance));
	MAX1(uiMaxDistance, 1);

	pfCosMinArcLengths = ScratchAlloc(uiMaxDistance * sizeof(F32));

	for (i = 0; i < uiMaxDistance; ++i)
	{
		// derivation of the min arc length:
		// 1. divide area of unit sphere by the number of points to get how much area each point should take up = 4 * PI / uiCount
		// 2. assume that is the area of a unit disk and convert to radius = sqrtf(4 / uiCount)
		// 3. multiply by two to get the diameter = 2 * sqrtf(4 / uiCount)
		// 4. multiply by quality for some wiggle room = fQuality * 2 * sqrtf(4 / uiCount)

		if (i == uiMaxDistance - 1 || i >= 8)
		{
			// let these always go through
			pfCosMinArcLengths[i] = 1;
		}
		else
		{
			uiCount = round(ceil(8 * PI * (i+0.5f))); // the uiCount used for this is 4 times the number of samples in a circle of radius i+0.5
			MIN1(uiCount, uiWidth * uiHeight);
			pfCosMinArcLengths[i] = cosf(fQuality * 2.f * sqrtf(4.f / uiCount));
		}
	}

	uiCount = uiWidth * uiHeight;

	for (curY = 0, i = 0; curY < uiHeight; ++curY)
	{
		for (curX = 0; curX < uiWidth; ++curX, ++i)
		{
			U32 uiRetryCounter = 0;
			bool bValid = false;

			while (!bValid)
			{
				U32 uiDistanceOffset;
				uiRetryCounter++;

				// bias the distance up if we keep failing to find a valid position
				uiDistanceOffset = uiRetryCounter / 1000;

				randomSphereShellSeeded(uiSeed, eRandType, 1, pPoints[i]); // use radius of 1 to simplify calculations, points will be scaled later
				bValid = true;

				// if the retry counter gets too high, give up and accept the latest value
				if (uiRetryCounter < 60000)
				{
					for (j = 0, y = 0; j < i && y < uiHeight; ++y)
					{
						for (x = 0; j < i && x < uiWidth; ++x, ++j)
						{
							F32 fCosArcLength = dotVec3(pPoints[j], pPoints[i]);
							U32 uiWidthDiff = abs(curX - x), uiHeightDiff = abs(curY - y), uiDistance;

							// deal with texture coordinate wrapping
							if (uiWidthDiff > uiHalfWidth)
								uiWidthDiff = uiWidth - uiWidthDiff;
							if (uiHeightDiff > uiHalfHeight)
								uiHeightDiff -= uiHeight - uiHeightDiff;

							// calculate integer distance
							uiDistance = uiDistanceOffset + round(ceil(sqrtf(SQR(uiWidthDiff) + SQR(uiHeightDiff))));
							MIN1(uiDistance, uiMaxDistance - 1);

							// check arc length to see if these points are too close together
							if (fCosArcLength > pfCosMinArcLengths[uiDistance])
							{
								bValid = false;
								break;
							}
						}
						if (!bValid)
							break;
					}
				}
			}
		}
	}

	ScratchFree(pfCosMinArcLengths);

	if (fRadius != 1)
	{
		// scale to desired radius
		for (i = 0; i < uiCount; ++i)
		{
			scaleVec3(pPoints[i], fRadius, pPoints[i]);
		}
	}
}


// Returns the sum of iDieCount random values in the range [1-iDieSize]
int randomDieRolls(int iDieCount, int iDieSize)
{
	int iResult = 0;
	if(iDieCount>0 && iDieSize>0)
	{
		while(iDieCount)
		{
			int iRoll = randInt(iDieSize)+1;
			iResult += iRoll;
			iDieCount--;
		}
	}
	return iResult;
}

int randomDieRollsSBLORNSeeded(U32* uiSeed, int iDieCount, int iDieSize)
{
	int iResult = 0;
	if(iDieCount>0 && iDieSize>0)
	{
		while(iDieCount)
		{
			int iRoll = randomIntRangeSeeded(uiSeed,RandType_BLORN_Static,1,iDieSize);
			iResult += iRoll;
			iDieCount--;
		}
	}
	return iResult;
}
