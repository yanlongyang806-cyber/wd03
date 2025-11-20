#pragma once
GCC_SYSTEM

AUTO_STRUCT;
typedef struct ByteBlock
{
	int iStartByteOffset;
	int iSize;
} ByteBlock;

AUTO_STRUCT;
typedef struct ByteBlockList
{
	ByteBlock **ppBlocks;
} ByteBlockList;

AUTO_STRUCT;
typedef struct SingleBit
{
	int iByteOffset;
	int iBitOffset; //0-7
} SingleBit;

AUTO_STRUCT;
typedef struct MaskedByte
{
	int iByteOffset;
	U8 iMask;
} MaskedByte;

AUTO_STRUCT;
typedef struct RawByteCopyGroup
{
	int iTotalSize;
	bool bDefaultToOn;
	ByteBlockList onBlocks;
	ByteBlockList offBlocks;
	SingleBit **ppOnBits;
	SingleBit **ppOffBits;	
} RawByteCopyGroup;

//uses alloced arrays not earrays because one of these is never destroyed once created,
//and it needs to be iterated over very quickly so we don't want cache misses.
AUTO_STRUCT;
typedef struct ByteCopyGroup
{
	int iNumByteBlocks;
	ByteBlock *pByteBlocks;

	int iNumMaskedBytes;
	MaskedByte *pMaskedBytes;
} ByteCopyGroup;


//given a list of byte blocks, sorts and groups that list into as few blocks as possible
void GroupByteBlocks(ByteBlockList *pList);
void CompileByteCopyGroup(ByteCopyGroup *pOutGroup, RawByteCopyGroup *pInGroup);
void CopyMemoryWithByteCopyGroup(void *pDest, void *pSrc, ByteCopyGroup *pGroup);
