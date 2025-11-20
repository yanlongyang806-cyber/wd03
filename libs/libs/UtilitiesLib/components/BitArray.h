#pragma once
GCC_SYSTEM
/* File BitArray.h
 *	This file defines a structure with a small memory footprint that can
 *	manage up to 512k bits.
 *
 *
 */


typedef struct BitArrayImp {
	unsigned int	maxSetByte;
	unsigned int	maxSize;	// The max number of bytes this bit array can manage.
	unsigned char	storage[1];	// The first byte of the bit array.
} BitArrayImp;

typedef struct BitArrayImp *BitArray;

// BitArray creation/destruction
BitArray baCreate_dbg(unsigned int maxBitNum MEM_DBG_PARMS);
#define baCreate(maxBitNum) baCreate_dbg(maxBitNum MEM_DBG_PARMS_INIT)
void baDestroy(BitArray ba);

void baSetSize_dbg(BitArray *ba, unsigned int maxBitNum MEM_DBG_PARMS);
#define baSetSize(ba, maxBitNum) baSetSize_dbg(ba, maxBitNum MEM_DBG_PARMS_INIT)
void baSizeToFit_dbg(BitArray *ba, unsigned int maxBitNum MEM_DBG_PARMS);
#define baSizeToFit(ba, maxBitNum) baSizeToFit_dbg(ba, maxBitNum MEM_DBG_PARMS_INIT)
unsigned int baGetCapacity(BitArray ba);

// Bit setting/clearing
int baSetBitValue(BitArray ba, unsigned int bitnum, int value);

int baSetBit(BitArray ba, unsigned int bitnum);
int baClearBit(BitArray ba, unsigned int bitnum);

int baSetBitRange(BitArray ba, unsigned int bitnum_start, unsigned int num_bits);
int baClearBitRange(BitArray ba, unsigned int bitnum_start, unsigned int num_bits);

int baClearBits(BitArray target, BitArray mask);

void baSetAllBits(BitArray ba);
void baClearAllBits(BitArray ba);
void baNotAllBits(BitArray ba);

// Bit testing.
int baIsSet(BitArray ba, unsigned int bitnum);
__forceinline static int baIsSetInline(BitArrayImp* ba, unsigned int bitnum)
{
	unsigned int baIndex = bitnum >> 3;
	if (baIndex > ba->maxSetByte)
		return 0;
	return ba->storage[baIndex] & (1 << (bitnum & 7));
}

int baGetMaxBits(BitArray ba);
int baCountSetBits(BitArray ba);
int baOrArray(BitArray array1, BitArray array2, BitArray output);
int baAndArray(BitArray array1, BitArray array2, BitArray output);
int baCopyArray(BitArray input, BitArray output);
int baBitsAreEqual(BitArray array1, BitArray array2);

