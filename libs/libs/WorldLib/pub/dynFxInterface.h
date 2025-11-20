#pragma once
GCC_SYSTEM

GCC_SYSTEM

#include "dynFxEnums.h"

typedef U32 dtFxManager;
typedef U32 dtNode;
typedef U32 dtFx;
typedef struct DynParamBlock DynParamBlock;
typedef struct DynNode DynNode;
typedef struct DynFxManager DynFxManager;
typedef struct DynFxMessage DynFxMessage;
typedef struct DynFx DynFx;
typedef struct DynJitterList DynJitterList;
typedef struct WorldFXEntry WorldFXEntry;
typedef struct WorldDrawableEntry WorldDrawableEntry;
typedef struct WorldDrawableList WorldDrawableList;
typedef struct WorldInstanceParamList WorldInstanceParamList;

typedef U32 (*dynJitterListSelectionCallback)(DynJitterList* pJList, void* pUserData);

// For dtAddFxEx. All of the other FX creation functions create one of these
// structures, fill it with the parameters given, and call dtAddFxEx with it.
typedef struct DynFxCreateParams {

	dtFxManager guidFxMan;
	const char* pcInfo;

	// FX system will take ownership of whatever is passed in here.
	DynParamBlock* pParamBlock;

	// dtAddFx stuff
	dtNode guidTargetRoot;
	dtNode guidSourceRoot;

	// dtAddFxAtLocation
	const F32 *vSource;
	const F32 *vTarget;
	const F32 *qTarget;

	// JitterList selection
	dynJitterListSelectionCallback cbJitterListSelectFunc;
	void* pJitterListSelectData;

	// HSV shifts. (Not multiply!)
	F32 fHue;
	F32 fSaturation;
	F32 fValue;

	U32 uiHitReactID;
	bool* pbNeedsRetry;
	eDynFxSource eSource;
	bool bAutoRetry;

} DynFxCreateParams;

void dtFxInitSys(void);

DynNode* dynNodeFromGuid(dtNode guidNode);
DynFxManager* dynFxManFromGuid(dtFxManager guidFxMan);
DynFx* dynFxFromGuid(dtFx guidFx);
dtFx dtFxGetFxGuid(DynFx *pFx);

void dynNodeRemoveGuid(dtNode guidNode);
void dynFxManRemoveGuid(dtFxManager guidFxMan);
void dynFxRemoveGuid(dtFx guidFx);
int dynFxNeedsAuxPass(dtFx guid);
void dtFxClearWorldModel(dtFx guid);
bool dtFxUpdateAndCheckModel(dtFx guid, WorldDrawableEntry *entry);

dtFxManager dtFxManCreate( eFxManagerType eType, dtNode nodeGuid, WorldFXEntry* pCellEntry, bool bLocalPlayer, bool bNoSound);
void dtFxManDestroy( dtFxManager guid );
void dtFxManSetTestTargetNode(dtFxManager guid, dtNode target, dtFxManager targetManagerGuid);
void dtFxManStopUsingFxInfo(dtFxManager guid, const char* pcDynFxName, bool bImmediate);
void dtFxManSetDebugFlag( dtFxManager guid );
void dtFxManSetCostumeFXHue( dtFxManager guid, F32 fHue);

dtFx dtAddFxEx(DynFxCreateParams *pCreateParams, bool bAutoRetrying, bool* pbParamsWereFreed);

dtFx dtAddFx(dtFxManager guidFxMan,  const char* pcInfo,  DynParamBlock* pParamBlock,  dtNode guidTargetRoot, dtNode guidSourceRoot, F32 fHue, U32 uiHitReactID, bool* pbNeedsRetry, eDynFxSource eSource, dynJitterListSelectionCallback cbJitterListSelectFunc, void* pJitterListSelectData);
dtFx dtAddFxAutoRetry(dtFxManager guidFxMan,  const char* pcInfo,  DynParamBlock* pParamBlock,  dtNode guidTargetRoot, dtNode guidSourceRoot, F32 fHue, U32 uiHitReactID, eDynFxSource eSource, dynJitterListSelectionCallback cbJitterListSelectFunc, void* pJitterListSelectData);
dtFx dtAddFxAtLocation(dtFxManager guidFxMan,  const char* pcInfo,  DynParamBlock* pParamBlock, const Vec3 vSource, const Vec3 vTarget, const Quat qTarget, F32 fHue, U32 uiHitReactID, bool* pbNeedsRetry, eDynFxSource eSource);
dtFx dtAddFxFromLocation(const char* pcInfo, DynParamBlock* pParamBlock, dtNode guidTargetRoot, const Vec3 vecSource, const Vec3 vecTarget, const Quat quatTarget, F32 fHue, U32 uiHitReactID, eDynFxSource eSource);

void dtFxKillEx(dtFx guid, bool bImmediate, bool bRemoveFromOwner);
#define dtFxKill(guid)	dtFxKillEx((guid), false, false)

void dtFxSendMessage(dtFx guid, const char* pcMessage);
void dtFxKillAll(void);
void dtFxSetInstanceData( dtFx guid, WorldDrawableList* pDrawableList, WorldInstanceParamList* pInstanceParamList, int iDrawableResetCounter );
void dtFxManAddMaintainedFx(dtFxManager guid, const char* pcDynFxName, DynParamBlock *paramblock, F32 fHue, dtNode targetGuid, eDynFxSource eSource);
void dtFxManRemoveMaintainedFx(dtFxManager guid, const char* pcDynFxName);
void dtFxManSendMessageMaintainedFx(dtFxManager guid, const char* pcDynFxName, SA_PARAM_NN_VALID DynFxMessage** ppMessages);
void dtTestFx(const char* pcInfo);

dtNode dtNodeCreate(void);
void dtNodeDestroy(dtNode guid);
void dtNodeSetPos(dtNode guid, const Vec3 vPos);
void dtNodeSetRot(dtNode guid, const Quat qRot);
void dtNodeSetPosAndRot(ACMD_FORCETYPE(U32) dtNode guid, const Vec3 vPos, const Quat qRot);
void dtNodeSetFromMat4(dtNode guid, const Mat4 mat);
void dtNodeSetParent(dtNode guid, dtNode parentGuid);
void dtNodeSetTag(dtNode guid, const char* pcTag);


void dynPhysicsSetCenter(const Vec3 center);

void dtUpdateCameraInfo(void);


// FX system accessors and mutators - public interface version.
bool dtFxGetColor(dtFx guid, int index, Vec4 color);
bool dtFxSetColor(dtFx guid, int index, const Vec4 color);



//so that people outside the dyn system can use ADD_SIMPLE_POINTER_REFERENCE_DYN
typedef const void *DictionaryHandle;
extern DictionaryHandle hDynNullDictHandle;
#define ADD_SIMPLE_POINTER_REFERENCE_DYN(handleName, pReferent) SET_HANDLE_FROM_REFDATA(hDynNullDictHandle, (pReferent), handleName)
