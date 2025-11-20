#pragma once
GCC_SYSTEM

/******************

A DynAnimTrack is the in-game representation of what is exported out of 3dsmax: a collection of DynBoneTrack's under one name.

A DynBoneTrack is simply a collection of DynPosKey's and/or DynRotKey's, which are pairs of transform info + frame numbers.

******************/

#include "StashTable.h"

extern StashTable stAnimTrackHeaders;

typedef struct DynNode DynNode;
typedef struct DynMoveAnimTrack DynMoveAnimTrack;
typedef struct DynTransform DynTransform;
typedef struct StashTableImp* StashTable;
typedef struct DynSkeleton DynSkeleton;

typedef struct DynPosKeyOnDisk
{
	Vec3 vPos;
	U32 uiFrame;
} DynPosKeyOnDisk;

typedef struct DynRotKeyOnDisk
{
	Quat qRot;
	U32 uiFrame;
} DynRotKeyOnDisk;

typedef struct DynScaleKeyOnDisk
{
	Vec3 vScale;
	U32 uiFrame;
} DynScaleKeyOnDisk;

typedef struct DynBoneTrackOnDisk
{
	const char*		pcBoneName;			// The name of the corresponding bone
	U32				uiRotKeyCount;		// How many rotation keys there are
	DynRotKeyOnDisk*	rotKeys;			// The rotation keys
	U32				uiPosKeyCount;		// How many position keys there are
	DynPosKeyOnDisk*	posKeys;			// The position keys
	U32				uiScaleKeyCount;	// How many scale keys there are
	DynScaleKeyOnDisk*	scaleKeys;
} DynBoneTrackOnDisk;						// A animation track for a single bone

typedef struct DynBoneTrackOnDiskCompressed
{
	const char*		pcBoneName;			// The name of the corresponding bone
} DynBoneTrackOnDiskCompressed;						// A animation track for a single bone


typedef struct DynPosKey
{
	Vec3			vPos;				// the value of the key
	/*
	F32				fInvFrameDelta;		// 1 over the difference between this frame and the next
	Vec3			vPosDelta;			// the difference in position between this frame and the next
	*/
} DynPosKey;						// An Animation keyframe for position

typedef struct DynPosKeyFrame
{
	U32				uiFrame;			// which frame is this key on
} DynPosKeyFrame;

typedef struct DynScaleKey
{
	Vec3			vScale;				// the value of the key
	/*
	F32				fInvFrameDelta;		// 1 over the difference between this frame and the next
	Vec3			vScaleDelta;		// the difference in scale between this frame and the next
	*/
} DynScaleKey;						// An Animation keyframe for scale

typedef struct DynScaleKeyFrame
{
	U32				uiFrame;			// which frame is this key on
} DynScaleKeyFrame;

typedef struct DynRotKey
{
	Quat			qRot;				// the value of the key
	/*
	F32				fInvFrameDelta;		// 1 over the difference between this frame and the next
	Quat			qRotDelta;
	*/
} DynRotKey;						// An animation keyframe for rotation

typedef struct DynRotKeyFrame
{
	U32				uiFrame;			// which frame is this key on
} DynRotKeyFrame;

typedef struct __ALIGN(16) DynBoneTrackUncompressed
{
	Quat qStaticRot;
	Vec3 vStaticPos;
	Vec3* pvPos;
	Vec3 vStaticScale;
	Vec3* pvScale;
	Quat* pqRot;
	const char* pcBoneName;
} DynBoneTrackUncompressed;

typedef struct __ALIGN(16) DynBoneTrack
{
	const char*		pcBoneName;			// The name of the corresponding bone
	U32				uiRotKeyCount;		// How many rotation keys there are
	DynRotKey*		rotKeys;			// The rotation keys
	DynRotKeyFrame*	rotKeyFrames;			// The rotation keys
	U32				uiPosKeyCount;		// How many position keys there are
	DynPosKey*		posKeys;			// The position keys
	DynPosKeyFrame*	posKeyFrames;
	U32				uiScaleKeyCount;	// How many scale keys there are
	DynScaleKey*	scaleKeys;
	DynScaleKeyFrame*	scaleKeyFrames;
} DynBoneTrack;						// A animation track for a single bone

typedef struct DynBoneWaveletTrack
{
	F32 fMinCoef;
	F32 fRange;
	void* zippedCoefs;
	U32 uiZipLength;
} DynBoneWaveletTrack;

typedef struct DynBoneStaticTrack
{
	F32 fValue;
} DynBoneStaticTrack;

typedef enum eDynSubTrackType
{
	eDynSubTrackType_RotP = ( 1 <<  0 ),
	eDynSubTrackType_RotY = ( 1 <<  1 ),
	eDynSubTrackType_RotR = ( 1 <<  2 ),
	eDynSubTrackType_PosX = ( 1 <<  3 ),
	eDynSubTrackType_PosY = ( 1 <<  4 ),
	eDynSubTrackType_PosZ = ( 1 <<  5 ),
	eDynSubTrackType_ScaX = ( 1 <<  6 ),
	eDynSubTrackType_ScaY = ( 1 <<  7 ),
	eDynSubTrackType_ScaZ = ( 1 <<  8 ),
	eDynSubTrackType_Rotation = eDynSubTrackType_RotP | eDynSubTrackType_RotY | eDynSubTrackType_RotR,
	eDynSubTrackType_Position = eDynSubTrackType_PosX | eDynSubTrackType_PosY | eDynSubTrackType_PosZ,
	eDynSubTrackType_Scale = eDynSubTrackType_ScaX | eDynSubTrackType_ScaY | eDynSubTrackType_ScaZ,
} eDynSubTrackType;

typedef struct __ALIGN(16) DynBoneTrackCompressed
{
	const char*		pcBoneName;			// The name of the corresponding bone
	U16 uiWaveletTracks;  // eDynSubTrackType flags
	U16 uiStaticTracks;
	DynBoneWaveletTrack* pWaveletTracks;
	DynBoneStaticTrack* pStaticTracks;
} DynBoneTrackCompressed;

typedef enum eDynAnimTrackType
{
	eDynAnimTrackType_Uncompressed,
	eDynAnimTrackType_Compressed,
} eDynAnimTrackType;

typedef struct DynAnimTrackUncompressed
{
#if !USE_SPU_ANIM
	StashTable					boneTable;
#endif
	U32							uiBoneCount;
	DynBoneTrackUncompressed*	bonesUncompressed; // Also pointer to large block of memory for all dynamically sized sub-data
    const char **boneNames;

	U32							uiTotalFrames;
	U32							uiEntryTime;
	U32							uiLastUsedFrameStamp;
	U32							uiUncompressedSize;
} DynAnimTrackUncompressed;

AUTO_STRUCT;
typedef struct DynAnimTrackFrameSnapshot
{
	U32 uiNumBones;
	DynTransform* pTransforms;
	StashTable stBoneTable;
} DynAnimTrackFrameSnapshot;

typedef struct DynAnimTrack
{
	const char*		pcName;				// The name of this animation track
	eDynAnimTrackType eType;

	StashTable		boneTable;			// A stashtable for quickly finding a bone within the animation track
	U32				uiBoneCount;
	DynBoneTrack*	bones;				// The bone tracks for this animation, Also pointer to large block of memory for all dynamically sized sub-data

	DynBoneTrackCompressed* bonesCompressed;
	U32				uiTotalFrames;		// The maxiumum number of frames in any set of keys for any bone

	DynAnimTrackUncompressed* pUncompressedTrack;

	U32 uiTotalSize; // Total memory usage of this loaded track (including decompressed track if it exists)

    const char*		pcFileName;
} DynAnimTrack;						// A collection of bonetracks that describe an atomic "animation"

typedef struct DynAnimTrackHeader
{
	DynAnimTrack*	pAnimTrack;
	U32 uiTotalFrames;
	const char*		pcName;				// The name of this animation track
	const char*		pcFilename;
	volatile int	bLoading;
	U32				bLoaded;
	U32				uiFramesSinceUsed;
	int				iPermanentRefCount;
	bool			bPreload;
	bool			bSharedMemory;

	U32				uiReportCount;
} DynAnimTrackHeader;


#if SPU
typedef struct DynAnimTrackLocalCache DynAnimTrackLocalCache;
#else
#endif

void dynAnimTrackDecompress(DynAnimTrack* pCompressed);

void dynPreloadAnimTrackInfo();
void dynSetAnimTrackOnSkeleton(DynSkeleton* pSkeleton, const char* pcAnimName);

DynAnimTrackHeader* dynAnimTrackHeaderFind(const char* pcAnimName);
bool dynAnimTrackHeaderRequest(DynAnimTrackHeader* pHeader);
void dynAnimTrackHeaderIncrementPermanentRefCount(DynAnimTrackHeader* pHeader);
void dynAnimTrackHeaderDecrementPermanentRefCount(DynAnimTrackHeader* pHeader);

bool dynBoneTrackUpdate(const DynAnimTrack* pAnimTrack, F32 fFrameTime, DynTransform* pTransform, const char* pcBoneTag, const DynTransform* pxBaseTransform, bool bNoInterp);
void decompressWaveletStream(const DynBoneWaveletTrack* pWavelet, F32* pfOut, U32 uiNumFrames);
void dynAnimTrackLoadFile(const char* pcFileName, DynAnimTrackHeader* pHeader);
void dynAnimTrackHeaderForceLoadTrack(DynAnimTrackHeader* pHeader);
void dynAnimTrackHeaderUnloadTrack(DynAnimTrackHeader* pHeader);
void dynAnimTrackHeaderUnloadPreloads(void);

//void dynAnimTrackUnloadAllUnreferenced(void);
void dynAnimTrackBufferUpdate(void);
void dynAnimTrackCacheUpdate(void);