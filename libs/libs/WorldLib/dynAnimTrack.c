

#include "fileutil.h"
#include "FolderCache.h"
#include "strings_opt.h"
#include "MemoryPool.h"
#include "ThreadSafeMemoryPool.h"
#include "endian.h"
#include "stringCache.h"
#include "zutils.h"
#include "Wavelet.h"
#include "ScratchStack.h"
#include "ThreadSafePriorityCache.h"
#include "TimedCallback.h"
#include "FileLoader.h"
#include "qsortg.h"


#include "wlState.h"
#include "dynFxManager.h"
#include "dynMove.h"
#include "dynSeqData.h"
#include "dynNodeInline.h"
#include "dynAnimTrack.h"
#include "MemoryMonitor.h"

#include "dynAnimTrackPrivate.h"

#include "dynAnimTrack_h_ast.h"


AUTO_RUN_ANON(memBudgetAddMapping(__FILE__, BUDGET_Animation););

#define INIT_MAX_ANIMTRACKS 64
#define INIT_MAX_ANIMTRACKS_QUEUE 8 

#define DEFAULT_DYN_ANIMTRACK_COUNT 1024
#define DEFAULT_DYN_BONE_NAME_COUNT 512
#define DYN_ANIM_MAX_FRAMES 1024


StashTable stAnimTrackHeaders = NULL;

StashTable boneNameTable;
TSMP_DEFINE(DynAnimTrack);
MP_DEFINE(DynAnimTrackHeader);
TSMP_DEFINE(DynAnimTrackUncompressed);

#define MAX_NEW_HEADERS_PER_FRAME 64
#define MAX_BUFFERED_HEADERS 128

static DynAnimTrackHeader** eaLoadedTracks = NULL;
static DynAnimTrackHeader* aNewHeaders[MAX_NEW_HEADERS_PER_FRAME];
static DynAnimTrackHeader* aBuffHeaders[MAX_BUFFERED_HEADERS];
volatile int iNumNewHeaders = 0;
volatile int iNumBuffHeaders = 0;
static U32 uiTestLatency = 0;

static U32 uiAnimTrackMemFrames = 1;
AUTO_COMMAND ACMD_COMMANDLINE ACMD_ACCESSLEVEL(9) ACMD_CATEGORY(dynAnimation);
void danimFramesToKeepAnimTracksInMemoryWhenNotActive(int n)
{
	uiAnimTrackMemFrames = CLAMP(n,0,30);
}

static U32 dynBoneTrackDecompressGetSize(DynBoneTrackCompressed* pCompresed, U32 uiNumFrames);

static void dynAnimTrackInitTables(void)
{
	if ( !boneNameTable )
		boneNameTable = stashTableCreateWithStringKeys( DEFAULT_DYN_BONE_NAME_COUNT, StashDeepCopyKeys_NeverRelease );

	stAnimTrackHeaders = stashTableCreateWithStringKeys(DEFAULT_DYN_ANIMTRACK_COUNT, StashDefault);
}

static void dynAnimTrackUncompressedFree( DynAnimTrackUncompressed* pAnimTrack)
{
	free(pAnimTrack->bonesUncompressed);
	stashTableDestroy(pAnimTrack->boneTable);
	TSMP_FREE(DynAnimTrackUncompressed, pAnimTrack);
}

AUTO_RUN;
void dynAnimTrackInitMemPool(void)
{
	MP_CREATE(DynAnimTrackHeader, DEFAULT_DYN_ANIMTRACK_COUNT);
	TSMP_CREATE(DynAnimTrack, DEFAULT_DYN_ANIMTRACK_COUNT);
	TSMP_CREATE(DynAnimTrackUncompressed, DEFAULT_DYN_ANIMTRACK_COUNT);
}


AUTO_COMMAND ACMD_CATEGORY(dynAnimation);
void danimClearAnimCache(void)
{
	FOR_EACH_IN_EARRAY(eaLoadedTracks, DynAnimTrackHeader, pHeader)
	{
		if (pHeader->bLoaded)
		{
			dynAnimTrackHeaderUnloadTrack(pHeader);
			eaRemoveFast(&eaLoadedTracks, ipHeaderIndex);
		}
	}
	FOR_EACH_END;
}

static const U8 animCacheSettings[3][2] =
{
	{ 16, 4 },
	{ 16, 8 },
	{ 32, 8 },
};

// Should return > 0 if A is higher priority than B
int dynAnimTrackCacheCompareTimestamps(const DynAnimTrackUncompressed* pAnimTrackA, const DynAnimTrackUncompressed* pAnimTrackB)
{
	int iCmp = (int)pAnimTrackA->uiLastUsedFrameStamp - (int)pAnimTrackB->uiLastUsedFrameStamp;
	if (!iCmp)
		return (*((int*)(&pAnimTrackA))) - (*((int*)(&pAnimTrackB))); // This is for the extremely unlikely, but still possible, secnario where every anim track has the same timestamp, and therefore can cause a stack overflow
	return iCmp;
}

AUTO_COMMAND ACMD_CATEGORY(dynAnimation);
void danimCacheSize(U32 uiCacheSize) // cache size in KB
{
	dynDebugState.uiAnimCacheSize = uiCacheSize * 1024;
}

AUTO_COMMAND ACMD_CATEGORY(dynAnimation);
void danimTestLoadLatency(U32 uiLatencyMS) // cache size in KB
{
	uiTestLatency = uiLatencyMS;
}

static F32 dynAnimFramesToTime(U32 uiFrames)
{
	return uiFrames * 0.033333333333f;
}

static F32 dynAnimTimeToFractionalFrames(F32 fAnimTime)
{
	return fAnimTime * 30.0f;
}

static U32 dynAnimTimeToFrames(F32 fAnimTime)
{
	return (U32)floor(fAnimTime * 30.0f);
}

static char* pLoadScratchBlock;
static char* pLoadScratchBlockCursor;
static const char *pLoadScratchBlockFilename;
static int iLoadScratchBlockSize;
static int iLoadScratchBlockPeak;

static void* loadScratchBlockAlloc( int iSize )
{
	char* pAlloc = pLoadScratchBlockCursor;
	pLoadScratchBlockCursor += iSize;
	if (pLoadScratchBlockCursor - pLoadScratchBlock > iLoadScratchBlockSize)
	{
		if (pLoadScratchBlockFilename)
			FatalErrorf("DynAnimTrack Load Scratch Block overrun!  %s is likely too large (expected < 500k)", pLoadScratchBlockFilename);
		else
			FatalErrorf("DynAnimTrack Load Scratch Block overrun!");
	}
	if (pLoadScratchBlockCursor - pLoadScratchBlock > iLoadScratchBlockPeak)
		iLoadScratchBlockPeak = pLoadScratchBlockCursor - pLoadScratchBlock;
	return pAlloc;
}

static void loadScratchBlockCreate(void)
{
	iLoadScratchBlockSize = 1024 * 1024 * 2;
	pLoadScratchBlockCursor = pLoadScratchBlock = ScratchAlloc(iLoadScratchBlockSize);
}

static void loadScratchBlockDestroy(void)
{
	ScratchFree(pLoadScratchBlock);
	pLoadScratchBlockCursor = pLoadScratchBlock = NULL;
	iLoadScratchBlockSize = 0;
}

static void loadScratchBlockReset(void)
{
	pLoadScratchBlockCursor = pLoadScratchBlock;
	// PARANOID
	//memset(pLoadScratchBlock, 0, iLoadScratchBlockSize);
}

static void loadScratchBlockSetFilename(const char *filename)
{
	pLoadScratchBlockFilename = filename;
}

static char* pMemBlock;
static char* pMemBlockCursor;
static int iMemBlockSize;

static void* dynAnimTrackAllocateBlock( int iSize )
{
	char* pAlloc = pMemBlockCursor;
	pMemBlockCursor += iSize;
	if (pMemBlockCursor - pMemBlock > iMemBlockSize)
	{
		FatalErrorf("DynAnimTrack allocation block overrun!");
	}
	return pAlloc;
}


static U32 dynLoadBoneTrackCompressed( const char** ppcFileData, DynBoneTrackCompressed* pBoneTrack, int iVersion)
{
    U32 uiAllocSize = 0;
	U32 uiNumStaticTracks, uiNumWaveletTracks, uiTrackIndex;
	// First read length of name, then string
	{
		char pcName[MAX_PATH];
		U32 uiLen;
		fa_read(&uiLen, *ppcFileData, sizeof(uiLen));
		xbEndianSwapU32(uiLen);

		fa_read_string(pcName, *ppcFileData, uiLen+1);

        pBoneTrack->pcBoneName = allocAddString(pcName);

		// add it to our bone name table, to save space on strings
		stashAddInt(boneNameTable, pBoneTrack->pcBoneName, 1, false); // add it if it's not already there
		stashGetKey(boneNameTable, pBoneTrack->pcBoneName, &pBoneTrack->pcBoneName); // set internal pointer
	}

	// Read the flags indicating which (and how many) of both kinds of compressed tracks there are
	fa_read(&pBoneTrack->uiStaticTracks, *ppcFileData, sizeof(pBoneTrack->uiStaticTracks));
	fa_read(&pBoneTrack->uiWaveletTracks, *ppcFileData, sizeof(pBoneTrack->uiWaveletTracks));
	xbEndianSwapU16(pBoneTrack->uiStaticTracks);
	xbEndianSwapU16(pBoneTrack->uiWaveletTracks);

	uiNumStaticTracks = countBitsSparse(pBoneTrack->uiStaticTracks);
	uiNumWaveletTracks = countBitsSparse(pBoneTrack->uiWaveletTracks);

	// Allocate arrays
	if (uiNumStaticTracks > 0)
	{
		pBoneTrack->pStaticTracks = loadScratchBlockAlloc(sizeof(DynBoneStaticTrack) * uiNumStaticTracks);
		uiAllocSize += (sizeof(DynBoneStaticTrack) * uiNumStaticTracks);
	}
	else
		pBoneTrack->pStaticTracks = NULL;

	if (uiNumWaveletTracks > 0)
	{
		pBoneTrack->pWaveletTracks = loadScratchBlockAlloc(sizeof(DynBoneWaveletTrack) * uiNumWaveletTracks);
		uiAllocSize += (sizeof(DynBoneWaveletTrack) * uiNumWaveletTracks);
	}
	else
		pBoneTrack->pWaveletTracks = NULL;

	// Read static tracks (just 1 float)
	for (uiTrackIndex=0; uiTrackIndex<uiNumStaticTracks; ++uiTrackIndex)
	{
		fa_read(&pBoneTrack->pStaticTracks[uiTrackIndex].fValue, *ppcFileData, sizeof(F32));
		xbEndianSwapF32(pBoneTrack->pStaticTracks[uiTrackIndex].fValue);
	}

	// Now read wavelet tracks
	for (uiTrackIndex=0; uiTrackIndex<uiNumWaveletTracks; ++uiTrackIndex)
	{
		DynBoneWaveletTrack* pWavelet = &pBoneTrack->pWaveletTracks[uiTrackIndex];
		fa_read(&pWavelet->fMinCoef, *ppcFileData, sizeof(F32));
		xbEndianSwapF32(pWavelet->fMinCoef);
		fa_read(&pWavelet->fRange, *ppcFileData, sizeof(F32));
		xbEndianSwapF32(pWavelet->fRange);
		fa_read(&pWavelet->uiZipLength, *ppcFileData, sizeof(U32));
		xbEndianSwapU32(pWavelet->uiZipLength);
		pWavelet->zippedCoefs = loadScratchBlockAlloc(pWavelet->uiZipLength);
		uiAllocSize += pWavelet->uiZipLength;
		fa_read(pWavelet->zippedCoefs, *ppcFileData, pWavelet->uiZipLength);
#if PLATFORM_CONSOLE
		// Fixup alignment
		if (pWavelet->uiZipLength & 0x3)
		{
			U32 uiPadding = (4 - (pWavelet->uiZipLength & 0x3));
			uiAllocSize += uiPadding;
			loadScratchBlockAlloc(uiPadding);
		}
#endif
	}
	return uiAllocSize;
}

static U32 dynLoadBoneTrack( const char** ppcFileData, DynBoneTrackOnDisk* pBoneTrack, int iVersion)
{
	U32 uiAllocSize = 0;
	// First read length of name, then string
	{
		char pcName[MAX_PATH];
		U32 uiLen;
		fa_read(&uiLen, *ppcFileData, sizeof(uiLen));
		xbEndianSwapU32(uiLen);

		fa_read_string(pcName, *ppcFileData, uiLen+1);

		pBoneTrack->pcBoneName = allocAddString(pcName);

		// add it to our bone name table, to save space on strings
		stashAddInt(boneNameTable, pBoneTrack->pcBoneName, 1, false); // add it if it's not already there
		stashGetKey(boneNameTable, pBoneTrack->pcBoneName, &pBoneTrack->pcBoneName); // set internal pointer
	}

	// Read the pos key count and the rot key count
	fa_read(&pBoneTrack->uiPosKeyCount, *ppcFileData, sizeof(pBoneTrack->uiPosKeyCount));
	xbEndianSwapU32(pBoneTrack->uiPosKeyCount);

	if ( pBoneTrack->uiPosKeyCount > DYN_ANIM_MAX_FRAMES )
		FatalErrorf("Invalid number of Position keys in bone %s", pBoneTrack->pcBoneName);
	fa_read(&pBoneTrack->uiRotKeyCount, *ppcFileData, sizeof(pBoneTrack->uiRotKeyCount));
	xbEndianSwapU32(pBoneTrack->uiRotKeyCount);
	if ( pBoneTrack->uiRotKeyCount > DYN_ANIM_MAX_FRAMES )
		FatalErrorf("Invalid number of Rotation keys in bone %s", pBoneTrack->pcBoneName);

	// If 
	if (iVersion > 0)
	{
		fa_read(&pBoneTrack->uiScaleKeyCount, *ppcFileData, sizeof(pBoneTrack->uiScaleKeyCount));
		xbEndianSwapU32(pBoneTrack->uiScaleKeyCount);
		if ( pBoneTrack->uiScaleKeyCount > DYN_ANIM_MAX_FRAMES )
			FatalErrorf("Invalid number of Scale keys in bone %s", pBoneTrack->pcBoneName);
	}
	else
		pBoneTrack->uiScaleKeyCount = 0;

	// Allocate them
	if ( pBoneTrack->uiPosKeyCount )
	{
		pBoneTrack->posKeys = loadScratchBlockAlloc(sizeof(DynPosKeyOnDisk)*pBoneTrack->uiPosKeyCount);
		uiAllocSize += (sizeof(DynPosKey) + sizeof(DynPosKeyFrame)) * pBoneTrack->uiPosKeyCount;
	}
	if ( pBoneTrack->uiRotKeyCount )
	{
		pBoneTrack->rotKeys = loadScratchBlockAlloc(sizeof(DynRotKeyOnDisk)*pBoneTrack->uiRotKeyCount);
		uiAllocSize += (sizeof(DynRotKey) + sizeof(DynRotKeyFrame)) * pBoneTrack->uiRotKeyCount;
	}
	if ( pBoneTrack->uiScaleKeyCount )
	{
		pBoneTrack->scaleKeys = loadScratchBlockAlloc(sizeof(DynScaleKeyOnDisk)*pBoneTrack->uiScaleKeyCount);
		uiAllocSize += (sizeof(DynScaleKey) + sizeof(DynScaleKeyFrame)) * pBoneTrack->uiScaleKeyCount;
	}

	// Now read the pos keys
	{
		U32 uiPosKeyIndex;

		for (uiPosKeyIndex=0; uiPosKeyIndex<pBoneTrack->uiPosKeyCount; ++uiPosKeyIndex)
		{
			DynPosKeyOnDisk* pKey = &pBoneTrack->posKeys[uiPosKeyIndex];

			// read the key time
			fa_read(&pKey->uiFrame, *ppcFileData, sizeof(U32));
			xbEndianSwapU32(pKey->uiFrame);

			// read the pos vec
			fa_read(&pKey->vPos, *ppcFileData, sizeof(Vec3));
			xbEndianSwapVec3(pKey->vPos, pKey->vPos);

			// cache some data
			/*
			if (pPrevKey)
			{
				subVec3(pKey->vPos, pPrevKey->vPos, pPrevKey->vPosDelta);
				pPrevKey->fInvFrameDelta = 1.f / ((F32)(pKey->uiFrame - pPrevKey->uiFrame));
			}
			*/
		}
	}
	
	// Now read the rot keys
	{
		U32 uiRotKeyIndex;

		for (uiRotKeyIndex=0; uiRotKeyIndex<pBoneTrack->uiRotKeyCount; ++uiRotKeyIndex)
		{
			DynRotKeyOnDisk* pKey = &pBoneTrack->rotKeys[uiRotKeyIndex];

			// read the key time
			fa_read(&pKey->uiFrame, *ppcFileData, sizeof(U32));
			xbEndianSwapU32(pKey->uiFrame);

			// read the pos vec
			fa_read(&pKey->qRot, *ppcFileData, sizeof(Quat));
			xbEndianSwapQuat(pKey->qRot, pKey->qRot);

			// cache some data
			/*
			if (pPrevKey)
			{
				quatDiff(pKey->qRot, pPrevKey->qRot, pPrevKey->qRotDelta);
				pPrevKey->fInvFrameDelta = 1.f / ((F32)(pKey->uiFrame - pPrevKey->uiFrame));
			}
			*/

		}
	}

	// Now read the scale keys
	{
		U32 uiScaleKeyIndex;

		for (uiScaleKeyIndex=0; uiScaleKeyIndex<pBoneTrack->uiScaleKeyCount; ++uiScaleKeyIndex)
		{
			DynScaleKeyOnDisk* pKey = &pBoneTrack->scaleKeys[uiScaleKeyIndex];

			// read the key time
			fa_read(&pKey->uiFrame, *ppcFileData, sizeof(U32));
			xbEndianSwapU32(pKey->uiFrame);

			// read the pos vec
			fa_read(&pKey->vScale, *ppcFileData, sizeof(Vec3));
			xbEndianSwapVec3(pKey->vScale, pKey->vScale);

			// cache some data
			/*
			if (pPrevKey)
			{
				subVec3(pKey->vScale, pPrevKey->vScale, pPrevKey->vScaleDelta);
				pPrevKey->fInvFrameDelta = 1.f / ((F32)(pKey->uiFrame - pPrevKey->uiFrame));
			}
			*/

		}
	}

	return uiAllocSize;
}

static void dynCopyCompressedBoneTrackToSingleBlock(DynBoneTrackCompressed* pSrc, DynBoneTrackCompressed* pDst, int iVersion)
{
	U32 uiNumStaticTracks, uiNumWaveletTracks, uiTrackIndex;
	pDst->pcBoneName = pSrc->pcBoneName;
	pDst->uiStaticTracks = pSrc->uiStaticTracks;
	pDst->uiWaveletTracks = pSrc->uiWaveletTracks;

	uiNumStaticTracks = countBitsSparse(pSrc->uiStaticTracks);
	uiNumWaveletTracks = countBitsSparse(pSrc->uiWaveletTracks);

	if (pSrc->pStaticTracks)
	{
		pDst->pStaticTracks = dynAnimTrackAllocateBlock(sizeof(DynBoneStaticTrack) * uiNumStaticTracks);
		for (uiTrackIndex=0; uiTrackIndex<uiNumStaticTracks; ++uiTrackIndex)
		{
			pDst->pStaticTracks[uiTrackIndex].fValue = pSrc->pStaticTracks[uiTrackIndex].fValue;
		}
	}

	if (pSrc->pWaveletTracks)
	{
		pDst->pWaveletTracks = dynAnimTrackAllocateBlock(sizeof(DynBoneWaveletTrack) * uiNumWaveletTracks);
		for (uiTrackIndex=0; uiTrackIndex<uiNumWaveletTracks; ++uiTrackIndex)
		{
			pDst->pWaveletTracks[uiTrackIndex].fMinCoef = pSrc->pWaveletTracks[uiTrackIndex].fMinCoef;
			pDst->pWaveletTracks[uiTrackIndex].fRange = pSrc->pWaveletTracks[uiTrackIndex].fRange;
			pDst->pWaveletTracks[uiTrackIndex].uiZipLength = pSrc->pWaveletTracks[uiTrackIndex].uiZipLength;
			pDst->pWaveletTracks[uiTrackIndex].zippedCoefs = dynAnimTrackAllocateBlock(pDst->pWaveletTracks[uiTrackIndex].uiZipLength);
			memcpy(pDst->pWaveletTracks[uiTrackIndex].zippedCoefs, pSrc->pWaveletTracks[uiTrackIndex].zippedCoefs, pDst->pWaveletTracks[uiTrackIndex].uiZipLength);

#if PLATFORM_CONSOLE
			// Alignment fixup
			{
				U32 uiPadding = pDst->pWaveletTracks[uiTrackIndex].uiZipLength & 0x3;
				if (uiPadding > 0)
				{
					dynAnimTrackAllocateBlock(4 - uiPadding);
				}
			}
#endif
		}
	}
}

static void dynCopyDiskBoneTrackToRunTimeBoneTrack(DynBoneTrackOnDisk* pDiskBoneTrack, DynBoneTrack* pBoneTrack, int iVersion)
{
	pBoneTrack->pcBoneName = pDiskBoneTrack->pcBoneName;
	pBoneTrack->uiPosKeyCount = pDiskBoneTrack->uiPosKeyCount;
	pBoneTrack->uiRotKeyCount = pDiskBoneTrack->uiRotKeyCount;
	pBoneTrack->uiScaleKeyCount = pDiskBoneTrack->uiScaleKeyCount;

	if (pBoneTrack->uiPosKeyCount)
	{
		U32 uiKeyIndex;
		/*
		DynPosKey* pPrevKey = NULL;
		DynPosKeyFrame* pPrevKeyFrame = NULL;
		*/
		pBoneTrack->posKeyFrames = dynAnimTrackAllocateBlock(sizeof(DynPosKeyFrame) * pBoneTrack->uiPosKeyCount);
		pBoneTrack->posKeys = dynAnimTrackAllocateBlock(sizeof(DynPosKey) * pBoneTrack->uiPosKeyCount);
		for (uiKeyIndex=0; uiKeyIndex<pBoneTrack->uiPosKeyCount; ++uiKeyIndex)
		{
			DynPosKey* pKey;
			DynPosKeyFrame* pKeyFrame;

			pKey = &pBoneTrack->posKeys[uiKeyIndex];
			pKeyFrame = &pBoneTrack->posKeyFrames[uiKeyIndex];

			copyVec3(pDiskBoneTrack->posKeys[uiKeyIndex].vPos, pKey->vPos);
			pKeyFrame->uiFrame = pDiskBoneTrack->posKeys[uiKeyIndex].uiFrame;

			/*
			if (pPrevKey)
			{
				subVec3(pKey->vPos, pPrevKey->vPos, pPrevKey->vPosDelta);
				pPrevKey->fInvFrameDelta = 1.f / ((F32)(pKeyFrame->uiFrame - pPrevKeyFrame->uiFrame));
			}

			pPrevKey = pKey;
			pPrevKeyFrame = pKeyFrame;
			*/
		}
	}
	if (pBoneTrack->uiRotKeyCount)
	{
		U32 uiKeyIndex;
		/*
		DynRotKey* pPrevKey = NULL;
		DynRotKeyFrame* pPrevKeyFrame = NULL;
		*/
		pBoneTrack->rotKeyFrames = dynAnimTrackAllocateBlock(sizeof(DynRotKeyFrame) * pBoneTrack->uiRotKeyCount);
		pBoneTrack->rotKeys = dynAnimTrackAllocateBlock(sizeof(DynRotKey) * pBoneTrack->uiRotKeyCount);
		for (uiKeyIndex=0; uiKeyIndex<pBoneTrack->uiRotKeyCount; ++uiKeyIndex)
		{
			DynRotKey* pKey;
			DynRotKeyFrame* pKeyFrame;

			pKey = &pBoneTrack->rotKeys[uiKeyIndex];
			pKeyFrame = &pBoneTrack->rotKeyFrames[uiKeyIndex];

			copyQuat(pDiskBoneTrack->rotKeys[uiKeyIndex].qRot, pKey->qRot);
			pKeyFrame->uiFrame = pDiskBoneTrack->rotKeys[uiKeyIndex].uiFrame;

			/*
			if (pPrevKey)
			{
				quatDiff(pPrevKey->qRot, pKey->qRot, pPrevKey->qRotDelta);
				pPrevKey->fInvFrameDelta = 1.f / ((F32)(pKeyFrame->uiFrame - pPrevKeyFrame->uiFrame));
			}

			pPrevKey = pKey;
			pPrevKeyFrame = pKeyFrame;
			*/
		}
	}
	if (pBoneTrack->uiScaleKeyCount)
	{
		U32 uiKeyIndex;
		/*
		DynScaleKey* pPrevKey = NULL;
		DynScaleKeyFrame* pPrevKeyFrame = NULL;
		*/
		pBoneTrack->scaleKeyFrames = dynAnimTrackAllocateBlock(sizeof(DynScaleKeyFrame) * pBoneTrack->uiScaleKeyCount);
		pBoneTrack->scaleKeys = dynAnimTrackAllocateBlock(sizeof(DynScaleKey) * pBoneTrack->uiScaleKeyCount);
		for (uiKeyIndex=0; uiKeyIndex<pBoneTrack->uiScaleKeyCount; ++uiKeyIndex)
		{
			DynScaleKey* pKey;
			DynScaleKeyFrame* pKeyFrame;

			pKey = &pBoneTrack->scaleKeys[uiKeyIndex];
			pKeyFrame = &pBoneTrack->scaleKeyFrames[uiKeyIndex];

			copyVec3(pDiskBoneTrack->scaleKeys[uiKeyIndex].vScale, pKey->vScale);
			pKeyFrame->uiFrame = pDiskBoneTrack->scaleKeys[uiKeyIndex].uiFrame;

			/*
			if (pPrevKey)
			{
				subVec3(pKey->vScale, pPrevKey->vScale, pPrevKey->vScaleDelta);
				pPrevKey->fInvFrameDelta = 1.f / ((F32)(pKeyFrame->uiFrame - pPrevKeyFrame->uiFrame));
			}

			pPrevKey = pKey;
			pPrevKeyFrame = pKeyFrame;
			*/
		}
	}
}

static void dynAnimTrackFree(DynAnimTrack* pToFree)
{
	stashTableDestroy(pToFree->boneTable);
	switch (pToFree->eType)
	{
		xcase eDynAnimTrackType_Uncompressed:
			SAFE_FREE(pToFree->bones);
		xcase eDynAnimTrackType_Compressed:
			SAFE_FREE(pToFree->bonesCompressed);
	}

	TSMP_FREE(DynAnimTrack, pToFree);
}

void dynAnimTrackFinishedLoadCallback(TimedCallback *callback, F32 timeSinceLastCallback, DynAnimTrackHeader* pHeader)
{
	pHeader->bLoading = 0;
	pHeader->bLoaded = true;

	if (pHeader->pAnimTrack->eType == eDynAnimTrackType_Compressed)
		dynAnimTrackDecompress(pHeader->pAnimTrack);
}

void dynAnimTrackLoadFile(const char* pcFileName, DynAnimTrackHeader* pHeader)
{
	PERFINFO_AUTO_START_FUNC();
	errorIsDuringDataLoadingInc(pcFileName);	
	{
	DynAnimTrack* pAnimTrack;
	bool checksum_valid;
	U32 file_size;
	char* const pcOrigFileData = fileAllocWithCRCCheck(pcFileName, &file_size, &checksum_valid);
	const char* pcFileData = pcOrigFileData;
	U32 uiNameLen;
	int iVersion = 0;
	bool bCreatedScratchBlock = false;


	// For testing a false load latency
	if (uiTestLatency)
	{
		Sleep(uiTestLatency);
	}

	if (iLoadScratchBlockSize == 0)
	{
		loadScratchBlockCreate();
		bCreatedScratchBlock = true;
	}
	loadScratchBlockReset();
	loadScratchBlockSetFilename(pcFileName);

	if ( !pcOrigFileData )
	{
		Errorf("Failed to open file %s\n", pcFileName);
		goto cleanup;
	}

	pAnimTrack = TSMP_ALLOC(DynAnimTrack);
	ZeroStructForce(pAnimTrack);
	pAnimTrack->pcFileName = allocAddFilename(pcFileName);

	// Read 32 bits.. if they are all 1, then this is a versioned file, and read the version next, then the string length
	// Otherwise, it's pre-version, and the 32 bits are the name length
	fa_read(&uiNameLen, pcFileData, sizeof(uiNameLen));
	xbEndianSwapU32(uiNameLen);

	if (uiNameLen == 0xffffffff)
	{
		// Is versioned!
		fa_read(&iVersion, pcFileData, sizeof(int));
		xbEndianSwapU32(iVersion);
		fa_read(&uiNameLen, pcFileData, sizeof(uiNameLen));
		xbEndianSwapU32(uiNameLen);
	}

	// Read name
	{
		char cName[MAX_PATH];
		assert(uiNameLen < MAX_PATH); // Bad data?  Prevent buffer overflow
		fa_read(cName, pcFileData, uiNameLen+1);
		pAnimTrack->pcName = allocAddString(cName);
	}

	if (iVersion >= 200)
	{
		fa_read(&pAnimTrack->eType, pcFileData, sizeof(pAnimTrack->eType) )
		xbEndianSwapU32(pAnimTrack->eType);
	}
	else
		pAnimTrack->eType = eDynAnimTrackType_Uncompressed;


	// Read bone count, allocate bone tracks, read them in
	fa_read(&pAnimTrack->uiBoneCount, pcFileData, sizeof(pAnimTrack->uiBoneCount) )
	xbEndianSwapU32(pAnimTrack->uiBoneCount);

	switch (pAnimTrack->eType)
	{

		xcase eDynAnimTrackType_Uncompressed:
		{
			if (iVersion >= 160)
			{
				U32 uiBoneTrackSize;
				fa_read(&uiBoneTrackSize, pcFileData, sizeof(U32));
				xbEndianSwapU32(uiBoneTrackSize);
				// ignored for now
			}
			{
				// Read in bones
				U32 uiBoneTrackIndex;
				DynBoneTrackOnDisk* pDiskBoneTracks = loadScratchBlockAlloc(sizeof(DynBoneTrackOnDisk)*pAnimTrack->uiBoneCount);

				U32 uiAllocSize = sizeof(DynBoneTrack)*pAnimTrack->uiBoneCount;
				for (uiBoneTrackIndex=0; uiBoneTrackIndex<pAnimTrack->uiBoneCount; ++uiBoneTrackIndex)
				{
					uiAllocSize += dynLoadBoneTrack( &pcFileData, &pDiskBoneTracks[uiBoneTrackIndex], iVersion);
				}
				
                iMemBlockSize = uiAllocSize;
				pMemBlockCursor = pMemBlock = malloc(iMemBlockSize);

				pAnimTrack->uiTotalSize += iMemBlockSize;
				
                pAnimTrack->bones = dynAnimTrackAllocateBlock(sizeof(DynBoneTrack)*pAnimTrack->uiBoneCount);
				pAnimTrack->boneTable = stashTableCreateWithStringKeys(2 * pAnimTrack->uiBoneCount, StashDefault);
				for (uiBoneTrackIndex=0; uiBoneTrackIndex<pAnimTrack->uiBoneCount; ++uiBoneTrackIndex)
				{

                    dynCopyDiskBoneTrackToRunTimeBoneTrack(&pDiskBoneTracks[uiBoneTrackIndex], &pAnimTrack->bones[uiBoneTrackIndex], iVersion);
					if (!stashAddPointer(pAnimTrack->boneTable, pDiskBoneTracks[uiBoneTrackIndex].pcBoneName, &pAnimTrack->bones[uiBoneTrackIndex], false))
					{
						FatalErrorf("In anim %s, read the same bone twice: %s", pAnimTrack->pcName, pDiskBoneTracks[uiBoneTrackIndex].pcBoneName);
						goto cleanup;
					}
				}
				if (pMemBlockCursor - pMemBlock != iMemBlockSize)
				{
					FatalErrorf("dynAnimTrackAllocBlock underrun!");
				}

			}

		}
		xcase eDynAnimTrackType_Compressed:
		{
			U32 uiBoneTrackIndex;
			DynBoneTrackCompressed* pDiskBoneTracks = loadScratchBlockAlloc(sizeof(DynBoneTrackCompressed)*pAnimTrack->uiBoneCount);
	        U32 uiAllocSize = sizeof(DynBoneTrackCompressed)*pAnimTrack->uiBoneCount;
			for (uiBoneTrackIndex=0; uiBoneTrackIndex<pAnimTrack->uiBoneCount; ++uiBoneTrackIndex)
			{
				uiAllocSize += dynLoadBoneTrackCompressed(&pcFileData, &pDiskBoneTracks[uiBoneTrackIndex], iVersion);
			}
			
            iMemBlockSize = uiAllocSize;
			pMemBlockCursor = pMemBlock = malloc(iMemBlockSize);
			pAnimTrack->uiTotalSize += iMemBlockSize;

            pAnimTrack->bonesCompressed = dynAnimTrackAllocateBlock(sizeof(DynBoneTrackCompressed)*pAnimTrack->uiBoneCount);
            memset(pAnimTrack->bonesCompressed, 0, sizeof(DynBoneTrackCompressed)*pAnimTrack->uiBoneCount);

			pAnimTrack->boneTable = stashTableCreateWithStringKeys(2 * pAnimTrack->uiBoneCount, StashDefault);
            for (uiBoneTrackIndex=0; uiBoneTrackIndex<pAnimTrack->uiBoneCount; ++uiBoneTrackIndex)
			{

				dynCopyCompressedBoneTrackToSingleBlock(&pDiskBoneTracks[uiBoneTrackIndex], &pAnimTrack->bonesCompressed[uiBoneTrackIndex], iVersion);
				if (!stashAddPointer(pAnimTrack->boneTable, pDiskBoneTracks[uiBoneTrackIndex].pcBoneName, &pAnimTrack->bonesCompressed[uiBoneTrackIndex], false))
				{
					FatalErrorf("In anim %s, read the same bone twice: %s", pAnimTrack->pcName, pDiskBoneTracks[uiBoneTrackIndex].pcBoneName);
					goto cleanup;
				}
			}
            if (pMemBlockCursor - pMemBlock != iMemBlockSize)
			{
				FatalErrorf("dynAnimTrackAllocBlock underrun!");
			}
		}
	}

	// Finally, Read total frame count
	fa_read(&pAnimTrack->uiTotalFrames, pcFileData, sizeof(pAnimTrack->uiTotalFrames));
#if PLATFORM_CONSOLE
	pAnimTrack->uiTotalFrames = endianSwapU32(pAnimTrack->uiTotalFrames);
#endif
	if ( pAnimTrack->uiTotalFrames == 0 || pAnimTrack->uiTotalFrames > DYN_ANIM_MAX_FRAMES )
	{
		AnimFileError(pAnimTrack->pcFileName, "Invalid number of frames for animation %s: %d frames!", pAnimTrack->pcName, pAnimTrack->uiTotalFrames);
		goto cleanup;
	}

	free(pcOrigFileData);

	if (!pAnimTrack->pcName)
	{
		FatalErrorf("Failed to set name for anim track %s\n", pcFileName);
		goto cleanup;
	}

	pHeader->pAnimTrack = pAnimTrack;
	if (pHeader->bPreload)
		dynAnimTrackFinishedLoadCallback(NULL, 0.0f, pHeader);
	else
		TimedCallback_Run(dynAnimTrackFinishedLoadCallback, pHeader, 0.0f);

	cleanup:
	if (bCreatedScratchBlock)
		loadScratchBlockDestroy();
	loadScratchBlockSetFilename(NULL);
	}
	errorIsDuringDataLoadingDec();
	PERFINFO_AUTO_STOP();
}




static void dynAnimTrackHeaderQueueLoad(DynAnimTrackHeader* pHeader)
{
	int iHeaderIndex;
	bool bInterlockedNumHeaders;
	bool bInterlockedNumBuffers;
	assert(!pHeader->bLoaded);
	
	//1. attempt to add the header to the list of loading/loaded animation tracks where we limit our request per frame
	//2. if that is full attempt to add the header to an overflow buffer so it'll have greater priority on the next frame
	//3. if that is full, stop attempting to load the animation.. in the case of forced animations this likely means a t-pose
	//will be used instead, in the case of game character powers and movement and such this likely means the animation will
	//just get re-requested by the sequencer on the next frame while it shows the 1st frame as a place holder

	if (iNumNewHeaders < MAX_NEW_HEADERS_PER_FRAME) {
		iHeaderIndex = InterlockedIncrement(&iNumNewHeaders) - 1;
		bInterlockedNumHeaders = true;
	} else {
		bInterlockedNumHeaders = false;
	}

	if (bInterlockedNumHeaders &&
		iHeaderIndex < MAX_NEW_HEADERS_PER_FRAME) {
		//1.
		//printfColor(COLOR_GREEN,"File loading (orig request): %s\n", pHeader->pcFilename);
		fileLoaderRequestAsyncExec(pHeader->pcFilename, FILE_HIGH_PRIORITY, false, dynAnimTrackLoadFile, pHeader);
		aNewHeaders[iHeaderIndex] = pHeader;
	}
	else
	{
		if (bInterlockedNumHeaders) {
			InterlockedDecrement(&iNumNewHeaders);
		}

		if (iNumBuffHeaders < MAX_BUFFERED_HEADERS) {
			iHeaderIndex = InterlockedIncrement(&iNumBuffHeaders) - 1;
			bInterlockedNumBuffers = true;
		} else {
			bInterlockedNumBuffers = false;
		}
			
		if (bInterlockedNumBuffers &&
			iHeaderIndex < MAX_BUFFERED_HEADERS) {
			//2.
			//printfColor(COLOR_BLUE,"File buffered: %s\n", pHeader->pcFilename);
			aBuffHeaders[iHeaderIndex] = pHeader;
		} else {
			//3.
			//printfColor(COLOR_RED|COLOR_BRIGHT,"Giving up: %s\n", pHeader->pcFilename);
			if (bInterlockedNumBuffers) {
				InterlockedDecrement(&iNumBuffHeaders);
			}
			InterlockedDecrement(&pHeader->bLoading);
			//devassert(iNumBuffHeaders < MAX_BUFFERED_HEADERS);
		}
	}
}

void dynAnimTrackHeaderForceLoadTrack(DynAnimTrackHeader* pHeader)
{
	pHeader->bPreload = true;
	dynAnimTrackLoadFile(pHeader->pcFilename, pHeader);
}

void dynAnimTrackHeaderUnloadTrack(DynAnimTrackHeader* pHeader)
{
	assert(!pHeader->bSharedMemory);
	assert(pHeader->pAnimTrack);
	assert(pHeader->bLoaded);
	if (pHeader->pAnimTrack->pUncompressedTrack)
		dynAnimTrackUncompressedFree(pHeader->pAnimTrack->pUncompressedTrack);
	dynAnimTrackFree(pHeader->pAnimTrack);
	pHeader->pAnimTrack = NULL;
	pHeader->bLoaded = 0;
	pHeader->bLoading = 0;
	pHeader->bPreload = 0;
}

static void dynAnimTrackHeaderFree(DynAnimTrackHeader* pHeader)
{
	assert(!pHeader->bSharedMemory);
	MP_FREE(DynAnimTrackHeader, pHeader);
}

void dynAnimTrackHeaderUnloadPreloads(void)
{
	StashTableIterator iter;
	StashElement elem;
	stashGetIterator(stAnimTrackHeaders, &iter);
	while (stashGetNextElement(&iter, &elem))
	{
		DynAnimTrackHeader* pHeader = stashElementGetPointer(elem);
		if (pHeader->bPreload && !pHeader->iPermanentRefCount)
		{
			dynAnimTrackHeaderUnloadTrack(pHeader);
		}
	}
}

static bool dynAnimTrackReload(const char* fullpath)
{
	char animName[MAX_PATH];
	const char* s;
	const char* pcPrefix = "animation_library/";
	bool bSuccess = false;
	s = strstriConst(fullpath, pcPrefix) + strlen(pcPrefix);
	strcpy(animName, s);
	strstriReplace(animName, ".atrk", "");

	// First, try to find it in the current table
	{
		DynAnimTrackHeader* pOld = dynAnimTrackHeaderFind(animName);
		if (pOld)
		{
			if (pOld->bLoaded)
			{
				// Already loaded, so let's just replace data out from under it
				dynAnimTrackHeaderUnloadTrack(pOld);
				dynAnimTrackHeaderForceLoadTrack(pOld);
			}
			else if (pOld->bLoading)
			{
				// Wait until it's finished loading
				while (!pOld->bLoaded)
					Sleep(1);
				// Already loaded, so let's just replace data out from under it
				dynAnimTrackHeaderUnloadTrack(pOld);
				dynAnimTrackHeaderForceLoadTrack(pOld);
			}
			else
			{
				// Must not be loaded or requested to load, so don't change anything
			}
		}
		else
		{
			// New file: add it to anim trakc table
			DynAnimTrackHeader* pNew = MP_ALLOC(DynAnimTrackHeader);
			pNew->pcName = allocAddString(animName);
			pNew->pcFilename = allocAddString(fullpath);
			stashAddPointer(stAnimTrackHeaders, allocAddString(animName), pNew, false);
		}
	}

	dynMoveManagerReloadedAnimTrack(animName);
	bSuccess = true; // no way to fail right now

	return bSuccess;
}

static U32 uiTrackAnimTrackReload = 0;
AUTO_CMD_INT( uiTrackAnimTrackReload, danimTrackAnimTrackReload ) ACMD_COMMANDLINE ACMD_CATEGORY(dynAnimation);

static void dynAnimTrackReloadCallback(const char *relpath, int when)
{
	int i = 0;

	if (strstr(relpath, "/_"))
	{
		return;
	}

	loadstart_printf("Reloading DynAnimTrack in %s...", relpath);

	if (!fileExists(relpath))
		; // File was deleted, do we care here?

	#define DYNMOVE_MAX_ACCESS_ATTEMPTS 10000
	if (uiTrackAnimTrackReload) {printfColor(COLOR_RED|COLOR_BRIGHT,"\nCheck for access attempt %i\n", i);}
	while (!fileCanGetExclusiveAccess(relpath) && i < DYNMOVE_MAX_ACCESS_ATTEMPTS) {
		if (uiTrackAnimTrackReload) {printfColor(COLOR_RED|COLOR_BRIGHT,"Check for access attempt %i\n", i);}
		Sleep(1);
		i++;
	}

	if (i < DYNMOVE_MAX_ACCESS_ATTEMPTS)
	{
		if (uiTrackAnimTrackReload) {printfColor(COLOR_RED|COLOR_BRIGHT,"Pre-waiting for access\n"); }
		//fileWaitForExclusiveAccess(relpath);
		if (uiTrackAnimTrackReload) {printfColor(COLOR_RED|COLOR_BRIGHT,"Post-waiting for access\n");}
		errorLogFileIsBeingReloaded(relpath);

		if (!dynAnimTrackReload(relpath)) { // reload file
			AnimFileError(relpath, "Error reloading .atrk file: %s", relpath);
			loadend_printf("done. ERROR OCCURED!");
		} else {
			loadend_printf("done!");
		}
	}
	else
	{
		AnimFileError(relpath, "Error reloading DynAnimTrack file %s post-anim.track reload, make sure EditPlus : File -> Lock is disabled!", relpath);
		loadend_printf("done. ERROR OCCURED (make sure the file isn't locked in EditPlus)!");
	}
	#undef DYNMOVE_MAX_ACCESS_ATTEMPTS
}

static bool bSharedMemMode = false;

static FileScanAction scanAnimTrackFiles(char *dir, struct _finddata32_t *data, void *pUserData)
{
	static char *ext = ".atrk";
	static int ext_len = 5; // strlen(ext);
	char filename[MAX_PATH];
	int len;

	if (data->name[0]=='_') return FSA_NO_EXPLORE_DIRECTORY;
	if (data->attrib & _A_SUBDIR)
		return FSA_EXPLORE_DIRECTORY;
	len = (int)strlen(data->name);
	if (len<ext_len || strnicmp(data->name + len - ext_len, ext, ext_len)!=0) // not a .atrk file
		return FSA_EXPLORE_DIRECTORY;

	STR_COMBINE_SSS(filename, dir, "/", data->name);

	// No longer load the file. Instead just store the filename for lookup
	{
		char cAnimName[MAX_PATH];
		char* pcDot;
		strcpy(cAnimName, filename + 18);
		if (strtok_r(cAnimName, ".", &pcDot))
			pcDot = 0;
		{
			DynAnimTrackHeader* pNew = MP_ALLOC(DynAnimTrackHeader);
			pNew->pcName = allocAddString(cAnimName);
			pNew->pcFilename = allocAddString(filename);
			stashAddPointer(stAnimTrackHeaders, allocAddString(cAnimName), pNew, false);
			if (bSharedMemMode)
			{
				dynAnimTrackHeaderForceLoadTrack(pNew);
				dynAnimTrackHeaderIncrementPermanentRefCount(pNew);
			}
		}
	}



	return FSA_EXPLORE_DIRECTORY;
}

void dynPreloadAnimTrackInfoInternal()
{
	char *pcDir="animation_library";
	char *pcPersistFilename = 0; // "dyninfo.bin"

	dynDebugState.uiAnimCacheSize = 4 * 1024 * 1024;

	loadScratchBlockCreate();

    dynAnimTrackInitTables();

	//dynAnimLoadFilesInDir(pcDir, pcFileType, )
	fileScanAllDataDirs(pcDir, scanAnimTrackFiles, NULL);

	loadScratchBlockDestroy();
}

void dynAnimTrackHeaderMoveToShared(DynAnimTrackHeader **ppHeader, SharedMemoryHandle *sm_handle)
{
	// Move this into shared memory and return the pointer
	DynAnimTrack* pTrack = (*ppHeader)->pAnimTrack;
	DynAnimTrackHeader *sm_header = sharedMemoryAlloc(sm_handle, sizeof(DynAnimTrackHeader));
	DynAnimTrack *sm_track = sharedMemoryAlloc(sm_handle, sizeof(DynAnimTrack));
	StashTable sm_table = NULL;
	StashTableIterator iter = {0};
	StashElement pElem = NULL;
	unsigned int i;
	unsigned int j;


	
	switch(pTrack->eType)
	{
	case eDynAnimTrackType_Uncompressed:
		{
			DynBoneTrack *sm_bones = sharedMemoryAlloc(sm_handle, sizeof(DynBoneTrack) * pTrack->uiBoneCount);
			DynRotKey *sm_rotkeys;
			DynRotKeyFrame *sm_rotkeyframes;
			DynPosKey *sm_poskeys;
			DynPosKeyFrame *sm_poskeyframes;
			DynScaleKey *sm_scalekeys;
			DynScaleKeyFrame *sm_scalekeyframes;
			
			for(i = 0; i < pTrack->uiBoneCount; ++i)
			{
				sm_rotkeys = sharedMemoryAlloc(sm_handle, sizeof(DynRotKey) * pTrack->bones[i].uiRotKeyCount);
				sm_rotkeyframes = sharedMemoryAlloc(sm_handle, sizeof(DynRotKeyFrame) * pTrack->bones[i].uiRotKeyCount);
				sm_poskeys = sharedMemoryAlloc(sm_handle, sizeof(DynPosKey) * pTrack->bones[i].uiPosKeyCount);
				sm_poskeyframes = sharedMemoryAlloc(sm_handle, sizeof(DynPosKeyFrame) * pTrack->bones[i].uiPosKeyCount);
				sm_scalekeys = sharedMemoryAlloc(sm_handle, sizeof(DynScaleKey) * pTrack->bones[i].uiScaleKeyCount);
				sm_scalekeyframes = sharedMemoryAlloc(sm_handle, sizeof(DynScaleKeyFrame) * pTrack->bones[i].uiScaleKeyCount);

				memcpy(sm_rotkeys, pTrack->bones[i].rotKeys, sizeof(DynRotKey) * pTrack->bones[i].uiRotKeyCount);
				memcpy(sm_rotkeyframes, pTrack->bones[i].rotKeyFrames, sizeof(DynRotKeyFrame) * pTrack->bones[i].uiRotKeyCount);
				memcpy(sm_poskeys, pTrack->bones[i].posKeys, sizeof(DynPosKey) * pTrack->bones[i].uiPosKeyCount);
				memcpy(sm_poskeyframes, pTrack->bones[i].posKeyFrames, sizeof(DynPosKeyFrame) * pTrack->bones[i].uiPosKeyCount);
				memcpy(sm_scalekeys, pTrack->bones[i].scaleKeys, sizeof(DynScaleKey) * pTrack->bones[i].uiScaleKeyCount);
				memcpy(sm_scalekeyframes, pTrack->bones[i].scaleKeyFrames, sizeof(DynScaleKeyFrame) * pTrack->bones[i].uiScaleKeyCount);

				pTrack->bones[i].rotKeys = sm_rotkeys;
				pTrack->bones[i].rotKeyFrames = sm_rotkeyframes;
				pTrack->bones[i].posKeys = sm_poskeys;
				pTrack->bones[i].posKeyFrames = sm_poskeyframes;
				pTrack->bones[i].scaleKeys = sm_scalekeys;
				pTrack->bones[i].scaleKeyFrames = sm_scalekeyframes;
			}

			memcpy(sm_bones, pTrack->bones, sizeof(DynBoneTrack) * pTrack->uiBoneCount);

			stashGetIterator(pTrack->boneTable, &iter);

			while(stashGetNextElement(&iter, &pElem))
			{
				DynBoneTrack *pBone = stashElementGetPointer(pElem);
				stashElementSetPointer(pElem, &sm_bones[pBone - pTrack->bones]);
			}

			free(pTrack->bones);
			pTrack->bones = sm_bones;
		}

	xcase eDynAnimTrackType_Compressed:
		{
			DynBoneTrackCompressed *sm_bones = sharedMemoryAlloc(sm_handle, sizeof(DynBoneTrackCompressed) * pTrack->uiBoneCount);
			DynBoneStaticTrack *sm_statictracks;
			DynBoneWaveletTrack *sm_wavelettracks;
			void *sm_zippedcoeffs;
			U32 uiNumStaticTracks;
			U32 uiNumWaveletTracks;

			for(i = 0; i < pTrack->uiBoneCount; ++i)
			{
				uiNumStaticTracks = countBitsSparse(pTrack->bonesCompressed[i].uiStaticTracks);
				uiNumWaveletTracks = countBitsSparse(pTrack->bonesCompressed[i].uiWaveletTracks);

				for(j = 0; j < uiNumWaveletTracks; ++j)
				{
					sm_zippedcoeffs = sharedMemoryAlloc(sm_handle, pTrack->bonesCompressed[i].pWaveletTracks[j].uiZipLength);
					memcpy(sm_zippedcoeffs, pTrack->bonesCompressed[i].pWaveletTracks[j].zippedCoefs, pTrack->bonesCompressed[i].pWaveletTracks[j].uiZipLength);
					pTrack->bonesCompressed[i].pWaveletTracks[j].zippedCoefs = sm_zippedcoeffs;
				}

				sm_statictracks = sharedMemoryAlloc(sm_handle, sizeof(DynBoneStaticTrack) * uiNumStaticTracks);
				sm_wavelettracks = sharedMemoryAlloc(sm_handle, sizeof(DynBoneWaveletTrack) * uiNumWaveletTracks);

				memcpy(sm_statictracks, pTrack->bonesCompressed[i].pStaticTracks, sizeof(DynBoneStaticTrack) * uiNumStaticTracks);
				memcpy(sm_wavelettracks, pTrack->bonesCompressed[i].pWaveletTracks, sizeof(DynBoneWaveletTrack) * uiNumWaveletTracks);

				pTrack->bonesCompressed[i].pStaticTracks = sm_statictracks;
				pTrack->bonesCompressed[i].pWaveletTracks = sm_wavelettracks;
			}

			memcpy(sm_bones, pTrack->bonesCompressed, sizeof(DynBoneTrackCompressed) * pTrack->uiBoneCount);

			stashGetIterator(pTrack->boneTable, &iter);

			while(stashGetNextElement(&iter, &pElem))
			{
				DynBoneTrackCompressed *pBone = stashElementGetPointer(pElem);
				stashElementSetPointer(pElem, &sm_bones[pBone - pTrack->bonesCompressed]);
			}

			free(pTrack->bonesCompressed);
			pTrack->bonesCompressed = sm_bones;
		}
	}

	sm_table = stashTableClone(pTrack->boneTable, sharedMemoryAlloc, sm_handle);
	memcpy(sm_track, pTrack, sizeof(DynAnimTrack));
	sm_track->boneTable = sm_table;
	memcpy(sm_header, *ppHeader, sizeof(*sm_header));
	sm_header->pAnimTrack = sm_track;
	sm_header->bSharedMemory = true;

	if (pTrack->pUncompressedTrack)
		dynAnimTrackUncompressedFree(pTrack->pUncompressedTrack);

	stashTableDestroySafe(&pTrack->boneTable);
	TSMP_FREE(DynAnimTrack, pTrack);
	dynAnimTrackHeaderFree(*ppHeader);

	*ppHeader = sm_header;
}

bool dynPreloadAnimTrackInfoShared()
{
	SharedMemoryHandle *sm_handle = NULL;
	SM_AcquireResult sm_result;
	StashTable *sm_stashes;

	sm_result = sharedMemoryAcquire(&sm_handle, "SM_DynAnimTrackInfo");

	if(sm_result == SMAR_FirstCaller)
	{
		StashTableIterator iter = {0};
		StashElement pElem = NULL;
		size_t iAllocSize = 0;
		unsigned int i;

		// Do first caller stuff
		bSharedMemMode = true;
		dynPreloadAnimTrackInfoInternal();

		// We have to figure out how much space we need first
		iAllocSize += sizeof(StashTable) * 2;
		iAllocSize += stAnimTrackHeaders ? stashGetMemoryUsage(stAnimTrackHeaders) : 0;
		iAllocSize += boneNameTable ? stashGetMemoryUsage(boneNameTable) : 0;

		stashGetIterator(stAnimTrackHeaders, &iter);

		while(stashGetNextElement(&iter, &pElem))
		{
			DynAnimTrackHeader *pHeader = stashElementGetPointer(pElem);
			DynAnimTrack* pTrack = pHeader->pAnimTrack;
			assert(pHeader->bLoaded);
			iAllocSize += sizeof(*pHeader);
			iAllocSize += sizeof(*pTrack);
			iAllocSize += pTrack->boneTable ? stashGetMemoryUsage(pTrack->boneTable) : 0;
			
			for(i = 0; i < pTrack->uiBoneCount; ++i)
			{
				switch(pTrack->eType)
				{
				case eDynAnimTrackType_Uncompressed:
					iAllocSize += sizeof(DynBoneTrack);
					iAllocSize += pTrack->bones[i].uiRotKeyCount * (sizeof(DynRotKey) + sizeof(DynRotKeyFrame));
					iAllocSize += pTrack->bones[i].uiPosKeyCount * (sizeof(DynPosKey) + sizeof(DynPosKeyFrame));
					iAllocSize += pTrack->bones[i].uiScaleKeyCount * (sizeof(DynScaleKey) + sizeof(DynScaleKeyFrame));

				xcase eDynAnimTrackType_Compressed:
					{
						unsigned int j;
						U32 uiNumStaticTracks = countBitsSparse(pTrack->bonesCompressed[i].uiStaticTracks);
						U32 uiNumWaveletTracks = countBitsSparse(pTrack->bonesCompressed[i].uiWaveletTracks);

						iAllocSize += sizeof(DynBoneTrackCompressed);
						iAllocSize += uiNumStaticTracks * sizeof(DynBoneStaticTrack);
						iAllocSize += uiNumWaveletTracks * sizeof(DynBoneWaveletTrack);

						for(j = 0; j < uiNumWaveletTracks; ++j)
						{
							iAllocSize += pTrack->bonesCompressed[i].pWaveletTracks[j].zippedCoefs ? pTrack->bonesCompressed[i].pWaveletTracks[j].uiZipLength : 0;
						}
					}
					break;
				}
			}
		}

		// Set the shared memory size
		// Also reserve two blocks for StashTableImps at the beginning
		sm_stashes = sharedMemorySetSize(sm_handle, iAllocSize);
		sharedMemorySetBytesAlloced(sm_handle, sizeof(StashTable)*2);

		// Move the bone name table first so we can fixup our pointers to its string keys
		if(boneNameTable)
		{
			sm_stashes[1] = stashTableClone(boneNameTable, sharedMemoryAlloc, sm_handle);

			// Really super inefficient thing I'll have to do here to walk the stAnimTrackHeaders AGAIN
			// And make sure that I fix up all of the bone names before destroying the stash table BUT after moving it to shared memory
			stashGetIterator(stAnimTrackHeaders, &iter);

			while(stashGetNextElement(&iter, &pElem))
			{
				DynAnimTrackHeader *pHeader = stashElementGetPointer(pElem);
				DynAnimTrack* pTrack = pHeader->pAnimTrack;

				switch(pTrack->eType)
				{
				case eDynAnimTrackType_Uncompressed:
					for(i = 0; i < pTrack->uiBoneCount; ++i)
					{
						stashGetKey(sm_stashes[1], pTrack->bones[i].pcBoneName, &pTrack->bones[i].pcBoneName);
					}
				xcase eDynAnimTrackType_Compressed:
					for(i = 0; i < pTrack->uiBoneCount; ++i)
					{
						stashGetKey(sm_stashes[1], pTrack->bonesCompressed[i].pcBoneName, &pTrack->bonesCompressed[i].pcBoneName);
					}
				}
			}

			stashTableDestroySafe(&boneNameTable);
			boneNameTable = calloc(1, stashGetTableImpSize());
			memcpy(boneNameTable, sm_stashes[1], stashGetTableImpSize());
		}

		// Now move the anim tracks to shared memory while fixing up the stash
		stashGetIterator(stAnimTrackHeaders, &iter);

		while(stashGetNextElement(&iter, &pElem))
		{
			DynAnimTrackHeader *pHeader = stashElementGetPointer(pElem);
			dynAnimTrackHeaderMoveToShared(&pHeader, sm_handle);
			stashElementSetPointer(pElem, pHeader);
		}

		// Now move the stashes to shared memory
		if(stAnimTrackHeaders)
		{
			sm_stashes[0] = stashTableClone(stAnimTrackHeaders, sharedMemoryAlloc, sm_handle);
			stashTableDestroySafe(&stAnimTrackHeaders);
			stAnimTrackHeaders = sm_stashes[0];
		}

		// Ostensibly, we're done
		sharedMemoryUnlock(sm_handle);
		return true;
	}
	else if(sm_result == SMAR_DataAcquired)
	{
		sm_stashes = sharedMemoryGetDataPtr(sm_handle);

		// Copy the base StashTableImps out of shared memory
		stAnimTrackHeaders = calloc(1, stashGetTableImpSize());
		boneNameTable = calloc(1, stashGetTableImpSize());

		memcpy(stAnimTrackHeaders, sm_stashes[0], stashGetTableImpSize());
		memcpy(boneNameTable, sm_stashes[1], stashGetTableImpSize());

		// That should be it, these should point to everything we wanted
		// Cross your fingers!
		return true;
	}
	else
	{
		// Failed, fall back to original version
		return false;
	}
}

void dynPreloadAnimTrackInfo()
{
	loadstart_printf("Loading anim tracks...");

	//if(GetAppGlobalType() != GLOBALTYPE_GAMESERVER || !stringCacheSharingEnabled() || !dynPreloadAnimTrackInfoShared())
	{
		dynPreloadAnimTrackInfoInternal();
		FolderCacheSetCallback(FOLDER_CACHE_CALLBACK_UPDATE, "animation_library/*.atrk", dynAnimTrackReloadCallback);
	}

	loadend_printf(" done (%d anim track headers).", stashGetCount(stAnimTrackHeaders));

	//pAnimTrackCache = tspCacheCreate(INIT_MAX_ANIMTRACKS, INIT_MAX_ANIMTRACKS_QUEUE, StashDefault, StashKeyTypeStrings, 0, dynAnimTrackCacheCompareTimestamps, NULL, dynAnimTrackUncompressedFree);

}

DynAnimTrackHeader* dynAnimTrackHeaderFind(const char* pcAnimName)
{
	DynAnimTrackHeader* pHeader;
	if (stashFindPointer(stAnimTrackHeaders, pcAnimName, &pHeader))
		return pHeader;
	return NULL;
}

bool dynAnimTrackHeaderRequest(DynAnimTrackHeader* pHeader)
{
	pHeader->uiFramesSinceUsed = 0;
	if (pHeader->bLoaded)
		return true; // ready
	if (!pHeader->bLoading)
	{
		if (InterlockedIncrement(&pHeader->bLoading) == 1) // Check to see if we are the first thread to request a load
		{
			// We're responsible for loading it.
			dynAnimTrackHeaderQueueLoad(pHeader);
		}
	}
	return false; // not ready
}

// Only use this for special-use animations like scale animations that need to stick around
void dynAnimTrackHeaderIncrementPermanentRefCount(DynAnimTrackHeader* pHeader)
{
	if (pHeader && !pHeader->bSharedMemory)
		++pHeader->iPermanentRefCount;
}

void dynAnimTrackHeaderDecrementPermanentRefCount(DynAnimTrackHeader* pHeader)
{
	if (pHeader && !pHeader->bSharedMemory)
		--pHeader->iPermanentRefCount;
}


void decompressWaveletStream(const DynBoneWaveletTrack* pWavelet, F32* pfOut, U32 uiNumFrames)
{
	U32 uiNumFramesPow2 = 2 * pow2(uiNumFrames);
	U32 uiCoeffsSize = sizeof(U16) * uiNumFramesPow2;
	U32 uiZipOutSize = uiCoeffsSize;
	U16* puiWaveletCoeffs = _alloca(uiCoeffsSize);
	F32* pfCoeffs = _alloca(sizeof(F32) * uiNumFramesPow2);
	F32* pfTempOut = _alloca(sizeof(F32) * uiNumFramesPow2);
	U32 uiFrame;
	assert(uiZipOutSize < 100000);
	unzipDataEx((void*)puiWaveletCoeffs, &uiZipOutSize, pWavelet->zippedCoefs, pWavelet->uiZipLength,true);
	for (uiFrame=0; uiFrame<uiNumFramesPow2; ++uiFrame)
	{
		xbEndianSwapU16(puiWaveletCoeffs[uiFrame]);
		if (puiWaveletCoeffs[uiFrame] == 0)
			pfCoeffs[uiFrame] = 0.0f;
		else
			pfCoeffs[uiFrame] = ((F32)(puiWaveletCoeffs[uiFrame]-1) * pWavelet->fRange) + pWavelet->fMinCoef;
	}
	inverseWaveletTransform(pfCoeffs, uiNumFramesPow2, pfTempOut);
	memcpy(pfOut, pfTempOut, sizeof(F32) * uiNumFrames);
}

void dynAnimTrackPYRToQuat(const Vec3 pyr, Quat q)
{
	// Assuming the angles are in radians.
	F32 c1 = cosf(pyr[1]/-2.0f);
	F32 s1 = sinf(pyr[1]/-2.0f);
	F32 c2 = cosf(pyr[2]/2.0f);
	F32 s2 = sinf(pyr[2]/2.0f);
	F32 c3 = cosf(pyr[0]/2.0f);
	F32 s3 = sinf(pyr[0]/2.0f);
	F32 c1c2 = c1*c2;
	F32 s1s2 = s1*s2;
	quatW(q) =c1c2*c3 - s1s2*s3;
	quatX(q) =c1c2*s3 + s1s2*c3;
	quatY(q) =s1*c2*c3 + c1*s2*s3;
	quatZ(q) =c1*s2*c3 - s1*c2*s3;
}


U32 dynBoneTrackDecompressGetSize(DynBoneTrackCompressed* pCompresed, U32 uiNumFrames)
{
	// Rotation first
	U32 ret=0;
	if (pCompresed->uiWaveletTracks & eDynSubTrackType_Rotation)
	{
		ret += uiNumFrames * sizeof(Quat);
	}

	// Now position
	if (pCompresed->uiWaveletTracks & eDynSubTrackType_Position)
	{
		ret += uiNumFrames * sizeof(Vec3);
	}

	// Finally scale
	if (pCompresed->uiWaveletTracks & eDynSubTrackType_Scale)
	{
		ret += uiNumFrames * sizeof(Vec3);
	}
	return ret;
}

static U8 *dynBoneTrackDecompress(
    DynBoneTrackCompressed* pCompresed, 
	DynBoneWaveletTrack* pWaveletTracks,
	DynBoneStaticTrack* pStaticTracks,
    DynBoneTrackUncompressed* pUncompressed, U32 uiNumFrames, U8 *pMemory
) {
#define ALLOC(num, type) (type*)pMemory; pMemory += num * sizeof(type);

	// Rotation first
	U32 uiWaveletTrackIndex = 0;
	U32 uiStaticTrackIndex = 0;
	pUncompressed->pcBoneName = pCompresed->pcBoneName;
	pUncompressed->pqRot = NULL;
	if (pCompresed->uiWaveletTracks & eDynSubTrackType_Rotation)
	{
		F32* pfPYR[3];
		U32 uiFrame;
		int i;
		F32 *temp = _alloca(sizeof(F32) * uiNumFrames * 3);
		for (i=0; i<3; ++i)
			pfPYR[i] = &temp[i*uiNumFrames];
		pUncompressed->pqRot = ALLOC(uiNumFrames, Quat);

		//Pitch
		if (pCompresed->uiWaveletTracks & eDynSubTrackType_RotP)
			decompressWaveletStream(&pWaveletTracks[uiWaveletTrackIndex++], pfPYR[0], uiNumFrames);
		else if (pCompresed->uiStaticTracks & eDynSubTrackType_RotP)
		{
			for (uiFrame=0; uiFrame<uiNumFrames; ++uiFrame)
				pfPYR[0][uiFrame] = pStaticTracks[uiStaticTrackIndex].fValue;
			++uiStaticTrackIndex;
		}
		else
		{
			for (uiFrame=0; uiFrame<uiNumFrames; ++uiFrame)
				pfPYR[0][uiFrame] = 0.0f;
		}

		//Yaw
		if (pCompresed->uiWaveletTracks & eDynSubTrackType_RotY)
			decompressWaveletStream(&pWaveletTracks[uiWaveletTrackIndex++], pfPYR[1], uiNumFrames);
		else if (pCompresed->uiStaticTracks & eDynSubTrackType_RotY)
		{
			for (uiFrame=0; uiFrame<uiNumFrames; ++uiFrame)
				pfPYR[1][uiFrame] = pStaticTracks[uiStaticTrackIndex].fValue;
			++uiStaticTrackIndex;
		}
		else
		{
			for (uiFrame=0; uiFrame<uiNumFrames; ++uiFrame)
				pfPYR[1][uiFrame] = 0.0f;
		}

		//Roll
		if (pCompresed->uiWaveletTracks & eDynSubTrackType_RotR)
			decompressWaveletStream(&pWaveletTracks[uiWaveletTrackIndex++], pfPYR[2], uiNumFrames);
		else if (pCompresed->uiStaticTracks & eDynSubTrackType_RotR)
		{
			for (uiFrame=0; uiFrame<uiNumFrames; ++uiFrame)
				pfPYR[2][uiFrame] = pStaticTracks[uiStaticTrackIndex].fValue;
			++uiStaticTrackIndex;
		}
		else
		{
			for (uiFrame=0; uiFrame<uiNumFrames; ++uiFrame)
				pfPYR[2][uiFrame] = 0.0f;
		}

		// Convert eulers to quat stream
		for (uiFrame=0; uiFrame<uiNumFrames; ++uiFrame)
		{
			Vec3 vPYR;
			for (i=0; i<3; ++i)
				vPYR[i] = pfPYR[i][uiFrame];
			dynAnimTrackPYRToQuat(vPYR, pUncompressed->pqRot[uiFrame]);
		}
	}
	else if (pCompresed->uiStaticTracks & eDynSubTrackType_Rotation )
	{
		Vec3 vPYR = {0};
		if (pCompresed->uiStaticTracks & eDynSubTrackType_RotP)
			vPYR[0] = pStaticTracks[uiStaticTrackIndex++].fValue;
		if (pCompresed->uiStaticTracks & eDynSubTrackType_RotY)
			vPYR[1] = pStaticTracks[uiStaticTrackIndex++].fValue;
		if (pCompresed->uiStaticTracks & eDynSubTrackType_RotR)
			vPYR[2] = pStaticTracks[uiStaticTrackIndex++].fValue;
		dynAnimTrackPYRToQuat(vPYR, pUncompressed->qStaticRot);
	}
	else
	{
		unitQuat(pUncompressed->qStaticRot);
	}


	// Now position
	pUncompressed->pvPos = NULL;
	if (pCompresed->uiWaveletTracks & eDynSubTrackType_Position)
	{
		F32* pfPos[3];
		U32 uiFrame;
		int i;
		F32 *temp = _alloca(sizeof(F32) * uiNumFrames * 3);
		for (i=0; i<3; ++i)
			pfPos[i] = &temp[i*uiNumFrames];
		pUncompressed->pvPos = ALLOC(uiNumFrames, Vec3);

		// Position X
		if (pCompresed->uiWaveletTracks & eDynSubTrackType_PosX)
			decompressWaveletStream(&pWaveletTracks[uiWaveletTrackIndex++], pfPos[0], uiNumFrames);
		else if (pCompresed->uiStaticTracks & eDynSubTrackType_PosX)
		{
			for (uiFrame=0; uiFrame<uiNumFrames; ++uiFrame)
				pfPos[0][uiFrame] = pStaticTracks[uiStaticTrackIndex].fValue;
			++uiStaticTrackIndex;
		}
		else
		{
			for (uiFrame=0; uiFrame<uiNumFrames; ++uiFrame)
				pfPos[0][uiFrame] = 0.0f;
		}

		// Position Y
		if (pCompresed->uiWaveletTracks & eDynSubTrackType_PosY)
			decompressWaveletStream(&pWaveletTracks[uiWaveletTrackIndex++], pfPos[1], uiNumFrames);
		else if (pCompresed->uiStaticTracks & eDynSubTrackType_PosY)
		{
			for (uiFrame=0; uiFrame<uiNumFrames; ++uiFrame)
				pfPos[1][uiFrame] = pStaticTracks[uiStaticTrackIndex].fValue;
			++uiStaticTrackIndex;
		}
		else
		{
			for (uiFrame=0; uiFrame<uiNumFrames; ++uiFrame)
				pfPos[1][uiFrame] = 0.0f;
		}

		// Position Z
		if (pCompresed->uiWaveletTracks & eDynSubTrackType_PosZ)
			decompressWaveletStream(&pWaveletTracks[uiWaveletTrackIndex++], pfPos[2], uiNumFrames);
		else if (pCompresed->uiStaticTracks & eDynSubTrackType_PosZ)
		{
			for (uiFrame=0; uiFrame<uiNumFrames; ++uiFrame)
				pfPos[2][uiFrame] = pStaticTracks[uiStaticTrackIndex].fValue;
			++uiStaticTrackIndex;
		}
		else
		{
			for (uiFrame=0; uiFrame<uiNumFrames; ++uiFrame)
				pfPos[2][uiFrame] = 0.0f;
		}

		// Convert F32 streams to vec3 stream
		for (uiFrame=0; uiFrame<uiNumFrames; ++uiFrame)
		{
			for (i=0; i<3; ++i)
				pUncompressed->pvPos[uiFrame][i] = pfPos[i][uiFrame];
		}
	}
	else if (pCompresed->uiStaticTracks & eDynSubTrackType_Position )
	{
		if (pCompresed->uiStaticTracks & eDynSubTrackType_PosX)
			pUncompressed->vStaticPos[0] = pStaticTracks[uiStaticTrackIndex++].fValue;
		if (pCompresed->uiStaticTracks & eDynSubTrackType_PosY)
			pUncompressed->vStaticPos[1] = pStaticTracks[uiStaticTrackIndex++].fValue;
		if (pCompresed->uiStaticTracks & eDynSubTrackType_PosZ)
			pUncompressed->vStaticPos[2] = pStaticTracks[uiStaticTrackIndex++].fValue;
	}
	else
	{
		zeroVec3(pUncompressed->vStaticPos);
	}

	// Finally scale
	pUncompressed->pvScale = NULL;
	if (pCompresed->uiWaveletTracks & eDynSubTrackType_Scale)
	{
		F32* pfScale[3];
		U32 uiFrame;
		int i;
		F32 *temp = _alloca(sizeof(F32) * uiNumFrames * 3);
		for (i=0; i<3; ++i)
			pfScale[i] = &temp[i*uiNumFrames];
		pUncompressed->pvScale = ALLOC(uiNumFrames, Vec3)

		// Scale X
		if (pCompresed->uiWaveletTracks & eDynSubTrackType_ScaX)
			decompressWaveletStream(&pWaveletTracks[uiWaveletTrackIndex++], pfScale[0], uiNumFrames);
		else if (pCompresed->uiStaticTracks & eDynSubTrackType_ScaX)
		{
			for (uiFrame=0; uiFrame<uiNumFrames; ++uiFrame)
				pfScale[0][uiFrame] = pStaticTracks[uiStaticTrackIndex].fValue;
			++uiStaticTrackIndex;
		}
		else
		{
			for (uiFrame=0; uiFrame<uiNumFrames; ++uiFrame)
				pfScale[0][uiFrame] = 1.0f;
		}

		// Scale Y
		if (pCompresed->uiWaveletTracks & eDynSubTrackType_ScaY)
			decompressWaveletStream(&pWaveletTracks[uiWaveletTrackIndex++], pfScale[1], uiNumFrames);
		else if (pCompresed->uiStaticTracks & eDynSubTrackType_ScaY)
		{
			for (uiFrame=0; uiFrame<uiNumFrames; ++uiFrame)
				pfScale[1][uiFrame] = pStaticTracks[uiStaticTrackIndex].fValue;
			++uiStaticTrackIndex;
		}
		else
		{
			for (uiFrame=0; uiFrame<uiNumFrames; ++uiFrame)
				pfScale[1][uiFrame] = 1.0f;
		}

		// Scale Z
		if (pCompresed->uiWaveletTracks & eDynSubTrackType_ScaZ)
			decompressWaveletStream(&pWaveletTracks[uiWaveletTrackIndex++], pfScale[2], uiNumFrames);
		else if (pCompresed->uiStaticTracks & eDynSubTrackType_ScaZ)
		{
			for (uiFrame=0; uiFrame<uiNumFrames; ++uiFrame)
				pfScale[2][uiFrame] = pStaticTracks[uiStaticTrackIndex].fValue;
			++uiStaticTrackIndex;
		}
		else
		{
			for (uiFrame=0; uiFrame<uiNumFrames; ++uiFrame)
				pfScale[2][uiFrame] = 1.0f;
		}

		// Convert F32 streams to vec3 stream
		for (uiFrame=0; uiFrame<uiNumFrames; ++uiFrame)
		{
			for (i=0; i<3; ++i)
				pUncompressed->pvScale[uiFrame][i] = pfScale[i][uiFrame];
		}
	}
	else if (pCompresed->uiStaticTracks & eDynSubTrackType_Scale )
	{
		copyVec3(onevec3, pUncompressed->vStaticScale);
		if (pCompresed->uiStaticTracks & eDynSubTrackType_ScaX)
			pUncompressed->vStaticScale[0] = pStaticTracks[uiStaticTrackIndex++].fValue;
		if (pCompresed->uiStaticTracks & eDynSubTrackType_ScaY)
			pUncompressed->vStaticScale[1] = pStaticTracks[uiStaticTrackIndex++].fValue;
		if (pCompresed->uiStaticTracks & eDynSubTrackType_ScaZ)
			pUncompressed->vStaticScale[2] = pStaticTracks[uiStaticTrackIndex++].fValue;
	}
	else
	{
		copyVec3(onevec3, pUncompressed->vStaticScale);
	}

	return pMemory;
#undef ALLOC
}

void dynAnimTrackDecompress(DynAnimTrack* pCompressed)
{
	DynAnimTrackUncompressed* pNew;
	U32 uiTrackIndex;
	U32 total_memory_size;
	U8 *pMemory;
	PERFINFO_AUTO_START("Wavelet Decompression", 1);
	pNew = TSMP_ALLOC(DynAnimTrackUncompressed);
	ZeroStructForce(pNew);

	pNew->uiTotalFrames = pCompressed->uiTotalFrames;
	pNew->uiBoneCount = pCompressed->uiBoneCount;

//printf("dynAnimTrackDecompress %s (%d bones)\n", pCompressed->pcName, pCompressed->uiBoneCount);

	// Allocate new bone tracks and room for sub data
	total_memory_size = sizeof(DynBoneTrackUncompressed)*pNew->uiBoneCount;
	for (uiTrackIndex=0; uiTrackIndex<pCompressed->uiBoneCount; ++uiTrackIndex)
	{
		total_memory_size += dynBoneTrackDecompressGetSize(&pCompressed->bonesCompressed[uiTrackIndex], pCompressed->uiTotalFrames);
	}
	pMemory = (U8 *) malloc(total_memory_size);

    pNew->bonesUncompressed = (DynBoneTrackUncompressed *)pMemory;
    memset(pMemory, 0, sizeof(DynBoneTrackUncompressed)*pNew->uiBoneCount);
    pMemory += sizeof(DynBoneTrackUncompressed)*pNew->uiBoneCount;

    pNew->boneTable = stashTableCreateWithStringKeys(pNew->uiBoneCount, StashDefault);

    // Decompress bone tracks
	for (uiTrackIndex=0; uiTrackIndex<pCompressed->uiBoneCount; ++uiTrackIndex)
	{
        DynBoneTrackCompressed *const pCompressedBone = &pCompressed->bonesCompressed[uiTrackIndex];
	    DynBoneWaveletTrack* pWaveletTracks = pCompressedBone->pWaveletTracks;
	    DynBoneStaticTrack* pStaticTracks = pCompressedBone->pStaticTracks;
        pMemory = dynBoneTrackDecompress(pCompressedBone, pWaveletTracks, pStaticTracks, 
            &pNew->bonesUncompressed[uiTrackIndex], pCompressed->uiTotalFrames, pMemory
        );
		stashAddPointer(pNew->boneTable, pNew->bonesUncompressed[uiTrackIndex].pcBoneName, &pNew->bonesUncompressed[uiTrackIndex], false);
	}

    assert(pMemory == ((U8*)pNew->bonesUncompressed + total_memory_size));

    PERFINFO_AUTO_STOP_CHECKED("Wavelet Decompression");

	pCompressed->uiTotalSize += total_memory_size;
	
	pCompressed->pUncompressedTrack = pNew;
}

int animTrackAgeComparator(const DynAnimTrackHeader** pA, const DynAnimTrackHeader** pB)
{
	int iFrames = (int)((*pA)->uiFramesSinceUsed) - (int)((*pB)->uiFramesSinceUsed);
	if (iFrames != 0)
		return iFrames;
	return (intptr_t)(*pA) - (intptr_t)(*pB);
}

//this shouldn't be called on a thread
void dynAnimTrackBufferUpdate(void)
{
	int iNumToAdd = MIN(iNumBuffHeaders, MAX_NEW_HEADERS_PER_FRAME - iNumNewHeaders);
	int i;

	assert(iNumNewHeaders <= MAX_NEW_HEADERS_PER_FRAME);
	assert(iNumBuffHeaders <= MAX_BUFFERED_HEADERS);

	for (i = 0; i < iNumToAdd; i++) {
		//if (i == 0) printfColor(COLOR_GREEN,"BEGIN_BUFFER_REQUEST_FRAME\n");
		//printfColor(COLOR_GREEN,"File loading (from buffer): %s\n", aBuffHeaders[i]->pcFilename);
		fileLoaderRequestAsyncExec(aBuffHeaders[i]->pcFilename, FILE_HIGH_PRIORITY, false, dynAnimTrackLoadFile, aBuffHeaders[i]);
		aNewHeaders[i+iNumNewHeaders] = aBuffHeaders[i];
	}

	for (i = iNumToAdd; i < MAX_BUFFERED_HEADERS; i++) {
		aBuffHeaders[i - iNumToAdd] = aBuffHeaders[i];
	}

	iNumNewHeaders += iNumToAdd;
	iNumBuffHeaders -= iNumToAdd;
}

//this shouldn't be called on a thread
void dynAnimTrackCacheUpdate(void)
{
	int iNumToAdd = MIN(iNumNewHeaders, MAX_NEW_HEADERS_PER_FRAME);
	int i;
	bool bHitLimit = false;
	dynDebugState.uiAnimCacheSizeUsed = 0;
	for (i=iNumToAdd-1; i>=0; --i)
	{
		eaPush(&eaLoadedTracks, aNewHeaders[i]);
		aNewHeaders[i] = NULL;
	}
	iNumNewHeaders = 0;

	// Now sort by age
	eaQSortG(eaLoadedTracks, animTrackAgeComparator);


	// Now loop through, keeping track of size, and eliminate those that are over size and over age
	FOR_EACH_IN_EARRAY_FORWARDS(eaLoadedTracks, DynAnimTrackHeader, pHeader)
	{
		if (!bHitLimit) {
			dynDebugState.uiAnimCacheSizeUsed += pHeader->bLoaded?pHeader->pAnimTrack->uiTotalSize:0;
		}

		if (pHeader->bLoaded									&&
			!pHeader->iPermanentRefCount						&&
			pHeader->uiFramesSinceUsed > uiAnimTrackMemFrames	&&
			(	bHitLimit ||
				dynDebugState.uiAnimCacheSizeUsed > dynDebugState.uiAnimCacheSize))
		{
			if (!bHitLimit)
				dynDebugState.uiAnimCacheSizeUsed -= pHeader->pAnimTrack->uiTotalSize;
			bHitLimit = true;
			//printfColor(COLOR_BRIGHT,"Unloading: %s\n", pHeader->pcName);
			dynAnimTrackHeaderUnloadTrack(pHeader);
			eaRemoveFast(&eaLoadedTracks, ipHeaderIndex); 
			ipHeaderIndex--;
		}
		++pHeader->uiFramesSinceUsed;
	}
	FOR_EACH_END;
}

AUTO_COMMAND;
void danimDebugListUncompressedAnimTrackBones(
	const char *pcAnimTrackHeader ACMD_NAMELIST(stAnimTrackHeaders, STASHTABLE)
	)
{
	StashElement stElement = NULL;
	if (stashFindElement(stAnimTrackHeaders, pcAnimTrackHeader, &stElement))
	{
		DynAnimTrackHeader *pAnimTrackHeader = stashElementGetPointer(stElement);

		if (pAnimTrackHeader->bLoading) {
			while (!pAnimTrackHeader->bLoaded)
				Sleep(1);
		}
		else if (!pAnimTrackHeader->bLoaded)
		{
			dynAnimTrackHeaderForceLoadTrack(pAnimTrackHeader);
		}

		{
			DynAnimTrack *pAnimTrack = SAFE_MEMBER(pAnimTrackHeader,pAnimTrack);
			if (pAnimTrack)
			{
				U32 i;
				printf("AnimTrack Header: %s\n", pcAnimTrackHeader);
				for (i = 0; i < pAnimTrack->uiBoneCount; i++)
					printf("Has Bone: %s\n", pAnimTrack->bones[i].pcBoneName);
			} else printf("Unable to find anim track for header %s!\n", pcAnimTrackHeader);	
		}
	} else printf("Unable to find anim track header %s!\n", pcAnimTrackHeader);
}

AUTO_COMMAND;
void danimDebugListUncompressedAnimTrackData(
	const char *pcAnimTrackHeader ACMD_NAMELIST(stAnimTrackHeaders, STASHTABLE),
	const char *pcBoneName ACMD_NAMELIST(boneNameTable, STASHTABLE))
{
	StashElement stElement = NULL;
	if (stashFindElement(stAnimTrackHeaders, pcAnimTrackHeader, &stElement))
	{
		DynAnimTrackHeader *pAnimTrackHeader = stashElementGetPointer(stElement);
		
		if (pAnimTrackHeader->bLoading) {
			while (!pAnimTrackHeader->bLoaded)
				Sleep(1);
		}
		else if (!pAnimTrackHeader->bLoaded)
		{
			dynAnimTrackHeaderForceLoadTrack(pAnimTrackHeader);
		}

		{
			DynAnimTrack *pAnimTrack = SAFE_MEMBER(pAnimTrackHeader,pAnimTrack);
			DynBoneTrack *pBoneTrack = NULL;

			if (pAnimTrack) {
				U32 i;

				printf("AnimTrack Header: %s\n", pcAnimTrackHeader);

				for (i = 0; i < pAnimTrack->uiBoneCount; i++) {
					if (!strcmpi(pAnimTrack->bones[i].pcBoneName, pcBoneName)) {
						pBoneTrack = &(pAnimTrack->bones[i]);
						break;
					}
				}

				if (pBoneTrack) {
					U32 j;
					printf("Bone: %s\n", pcBoneName);
					if (pBoneTrack->uiPosKeyCount)
						printf("Positions\n");
					for (j = 0; j < pBoneTrack->uiPosKeyCount; j++) {
						printf("(%u: %f, %f, %f)\n",
							pBoneTrack->posKeyFrames[j].uiFrame,
							pBoneTrack->posKeys[j].vPos[0],
							pBoneTrack->posKeys[j].vPos[1],
							pBoneTrack->posKeys[j].vPos[2]);
					}
					if (pBoneTrack->uiRotKeyCount)
						printf("Rotations (as Euler degs) : (as quarternions) \n");
					for (j = 0; j < pBoneTrack->uiRotKeyCount; j++) {
						Vec3 pyr;
						quatToPYR(pBoneTrack->rotKeys[j].qRot, pyr);
						printf("(%u: %f, %f, %f) : ",
							pBoneTrack->rotKeyFrames[j].uiFrame,
							180.0*pyr[0]/3.14265,
							180.0*pyr[1]/3.14265,
							180.0*pyr[2]/3.14265);
						printf("(%u: %f, %f, %f, %f)\n",
							pBoneTrack->rotKeyFrames[j].uiFrame,
							pBoneTrack->rotKeys[j].qRot[0],
							pBoneTrack->rotKeys[j].qRot[1],
							pBoneTrack->rotKeys[j].qRot[2],
							pBoneTrack->rotKeys[j].qRot[3]);
					}
					if (pBoneTrack->uiScaleKeyCount)
						printf("Scaling\n");
					for (j = 0; j < pBoneTrack->uiScaleKeyCount; j++) {
						printf("(%u: %f %f %f)\n",
							pBoneTrack->scaleKeyFrames[j].uiFrame,
							pBoneTrack->scaleKeys[j].vScale[0],
							pBoneTrack->scaleKeys[j].vScale[1],
							pBoneTrack->scaleKeys[j].vScale[2]);
					}
				} else printf("Unable to find bone %s!\n", pcBoneName);
			} else printf("Unable to find anim track for header %s!\n", pcAnimTrackHeader);	
		}
	} else printf("Unable to find anim track header %s!\n", pcAnimTrackHeader);
}

#include "dynAnimTrack_h_ast.c"
