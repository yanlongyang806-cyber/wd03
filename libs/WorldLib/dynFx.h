
#pragma once 
GCC_SYSTEM

#include "stdtypes.h"
#include "referencesystem.h"
#include "SparseGrid.h"

#include "dynFxManager.h"


typedef struct DynFx DynFx;
typedef struct DynFxManager DynFxManager;
typedef struct DynParticle DynParticle;
typedef struct DynMeshTrail DynMeshTrail;
typedef struct DynChildCall DynChildCall;
typedef struct DynChildCallCollection DynChildCallCollection;
typedef struct DynKeyFrame DynKeyFrame;
typedef struct GfxLight GfxLight;
typedef struct DynParamBlock DynParamBlock;
typedef struct DynEventUpdater DynEventUpdater;
typedef struct DynJitterList DynJitterList;
typedef struct DynFxInfo DynFxInfo;
typedef struct DynFxMessage DynFxMessage;
typedef struct DynFxPhysicsInfo DynFxPhysicsInfo;
typedef struct DynParentBhvr DynParentBhvr;
typedef struct SparseGridEntry SparseGridEntry;
typedef struct DynFxFastParticleEmitter DynFxFastParticleEmitter;
typedef struct WorldDrawableList WorldDrawableList;
typedef struct WorldInstanceParamList WorldInstanceParamList;
typedef U32 dtFx;

typedef U32 (*dynJitterListSelectionCallback)(DynJitterList* pJList, void* pUserData);

DynFx* dynFxCreate(DynFxManager* pManager, const char* pcFxInfo, DynFx* pParent, DynParamBlock* pParamBlock, const DynNode* pTargetRoot, const DynNode* pSourceRoot, F32 fHue, F32 fSaturationShift, F32 fValueShift, DynFxSortBucket* pSortBucket, U32 uiHitReactID, eDynPriority ePriorityOverride, dynJitterListSelectionCallback cbJitterListSelectFunc, void* pJitterListSelectData);
bool dynFxUpdate(int iPartitionIdx, DynFx* pFx, DynFxTime uiDeltaTime, F32 fFadeOut, bool bSystemFade, bool bFirstUpdate);

bool dynFxStopIfNameMatches(DynFx* pFx, const char* pcDynFxInfoName, bool bImmediate);
//void dynFxObjPrepForDraw(DynObject* pObject);

//cleanup
void dynFxDelete(SA_PRE_NN_VALID SA_POST_FREE DynFx* pFx, bool bImmediate);
DynFx* dynFxCallChildDyns(DynFx* pFx, DynChildCallCollection* pCollection, DynFx* pPrevSibling);
void dynFxUpdateParentBhvrKeyframe(DynFx* pFx, DynKeyFrame* pKeyFrame);
void dynFxUpdateParentBhvr(SA_PARAM_NN_VALID DynFx* pFx, DynFxTime uiDeltaTime);
DynNode* dynFxGetNode( DynFx* pFx );
const DynParticle* dynFxGetParticleConst( const DynFx* pFx );
const DynFx* dynFxGetParentFxConst( const DynFx* pFx );
void dynFxPushChildFx(SA_PARAM_NN_VALID DynFx* pParentFx, SA_PARAM_OP_VALID DynFx* pSibling, SA_PARAM_NN_VALID DynFx* pChildFx );
void dynEventUpdaterClear(DynEventUpdater* pUpdater, DynFx* pFx);
void dynFxSendMessages(SA_PARAM_NN_VALID DynFx* pFx, SA_PARAM_NN_VALID DynFxMessage*** pppMessages);
void dynParticleFree( SA_PARAM_NN_VALID DynParticle* pParticle, SA_PARAM_OP_VALID DynFxRegion* pFxRegion );
void dynFxChangeManager(SA_PARAM_NN_VALID DynFx* pFx, DynFxManager* pNewManager);
void dynFxAddNodesFromGeometry(DynFx* pFx, const char* pcModelName);
void dynFxSetupCloth(DynFx* pFx, const char* pcClothName, const char *pcClothInfo, const char *pcClothColInfo);
void dynFxClearMissingFiles(void);
void dynFxApplyToCostume( SA_PARAM_NN_VALID DynFx* pFx );
void dynFxGrabCostumeModel(SA_PARAM_NN_VALID DynFx *pFx);
void dynFxUpdateGrid(SA_PARAM_NN_VALID DynFx* pFx);
void dynFxRemoveFromGrid(SA_PARAM_NN_VALID DynFx* pFx);
void dynFxRemoveFromGridRecurse(SA_PARAM_NN_VALID DynFx* pFx);
bool dynFxPriorityBelowDetailSetting(int iPriorityLevel);
bool dynFxDropPriorityAboveDetailSetting(int iPriorityLevel);
bool dynFxExclusionTagMatches(const char* pcExclusionTag);
void dynFxSetExclusionTag(const char* pcExclusionTag, bool bExclude);
DynFxRef* dynFxReferenceCreate(const DynFx *pFx);
void dynFxReferenceFree(DynFxRef *pFxRef);

typedef struct DynFx
{
	DynParticle* pParticle;
	DynEventUpdater** eaDynEventUpdaters;
	DynFx**	eaChildFx;
	REF_TO(DynFxInfo) hInfo;

	GfxLight* pLight;
	DynFxManager* pManager;
	dtFx guid;
	const DynParentBhvr* pCurrentParentBhvr;

	const DynFxPhysicsInfo* pPhysicsInfo;
	REF_TO(DynParamBlock) hParamBlock;
	REF_TO(DynFx) hParentFx;
	REF_TO(DynFx) hSiblingFx;

	REF_TO(DynNode) hGoToNode;
	REF_TO(DynNode) hOrientToNode;
	REF_TO(DynNode) hScaleToNode;
	REF_TO(DynNode) hTargetRoot;

	REF_TO(DynNode) hSourceRoot;

	dynJitterListSelectionCallback cbJitterListSelectFunc;
	void* pJitterListSelectData;

	F32 fFadeOutTime;		// For quick fade out support
	F32 fFadeOut;			// For quick fade out support
	F32 fFadeInTime;		// For quick fade in support
	F32 fFadeIn;			// For quick fade in support
	F32 fHue;
	F32 fSaturationShift;
	F32 fValueShift;
	F32 fPlaybackSpeed;

	F32 fEntityFadeSpeedOverride;

	eDynFxSource eSource;

	U32 uiMessages; // a message is 3 bits, so a max of 10 messages
	U32* eaMessageOverflow;

	U32 uiTimeSinceStart;
	DynFxSortBucket* pSortBucket;
	DynFxSortBucket** eaSortBuckets;
	S32 iDrawArrayIndex;


	U32 uiExternalAlpha				: 8;

	U32 uiEventTriggered			: 8;

	U32 bWaitingToSkin				: 1;
	U32 bKill						: 1;
	U32 bSystemFade					: 1;
	U32 bPhysicsEnabled				: 1;
	U32 bHasAutoTriggerEventsLeft	: 1;
	U32 bHasHadEvent				: 1;
	U32 bHasCreatedParticle			: 1;
	U32 bGotoActive					: 1;

	U32 bDebris						: 1;
	U32 bDebug						: 1;
	U32 b2D							: 1;
	U32 iPriorityLevel				: 3;
	U32 bCostumePartsExclusive		: 1;
	U32 bHidden						: 1;

	// [NNO-19698] NW: Archer: String FX are not on stowed bow (floating) when in Melee mode & Idle
	// Normally, for animated alpha, it is automatically propagated to children. We need this flag because there is a little bit of code overriding alpha during dynParticleCategorize
	// (during draw) that needs to propagate to children.
	U32 bZeroAlpha					: 1;

	U8 uiMessageCount;
	U8 uiTriggeredNearEvents;

	// Put unimportant stuff here
	const char** eaEntCostumeParts;
	
	bool bEntMaterialExcludeOptionalParts;

	DynNode** eaAltPivs;
	DynNode** eaRayCastNodes;
	U32 uiFXID;
	U32 uiHitReactID;
	WorldDrawableList*		pDrawableList;
	WorldInstanceParamList*	pInstanceParamList;
	int						iDrawableResetCounter;
} DynFx;

typedef struct DynFxRef
{
	REF_TO(DynFx)		hDynFx;
} DynFxRef;

// Alienware color FX controls.
void dynFxAddAlienColor(float priority, Vec4 color);
void dynFxClearAlienColors(void);
void dynFxDriveAlienColor(void);

// FX system accessors and mutators.
bool dynFxGetColor(const DynFx *pFx, int index, Vec4 color);
bool dynFxSetColor(DynFx *pFx, int index, const Vec4 color);


//STATIC_ASSERT(sizeof(DynFx)==128)
