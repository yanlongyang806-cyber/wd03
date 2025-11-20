#pragma once
GCC_SYSTEM

#include "StashTable.h"
#include "DynFxEnums.h"

typedef struct DynFx DynFx;

typedef struct DynFxTracker
{
	U32 uiNumFx;
	U32 uiNumFxPeak;
	U32 uiNumFxObjects;
	U32 uiNumPhysicsObjects;
	int iMemUsage;
	int iPriorityLevel;
	const char* pcFxName; AST(POOL_STRING)
	F32 fTimeSinceZero;
	bool bAddedSinceUpdate;
} DynFxTracker;


AUTO_STRUCT;
typedef struct DynDrawTracker
{
	const char* pcName; AST(POOL_STRING)
	eDynFxType eType;
	U32 uiNum;
	U32 uiSubObjects;
	eDynPriority ePriority;
} DynDrawTracker;

typedef struct DynFxRecording
{
	U32 uiNumDynFxCreated;
	U32 uiNumDynFxCreatedPeak;
	U32 uiNumDynFxCreatedTotal;

	U32 uiNumDynFxUpdated;
	U32 uiNumDynFxUpdatedPeak;
	U32 uiNumDynFxUpdatedTotal;

	U32 uiNumDynFxDrawn;
	U32 uiNumDynFxDrawnPeak;
	U32 uiNumDynFxDrawnTotal;
} DynFxRecording;

extern DynFxTracker** eaDynFxTrackers;
extern DynDrawTracker** eaDynDrawTrackers;
extern DynFxRecording fxRecording;
extern const char** eaDynDrawOnly;
extern StashTable stFxRecordNames;
 

void dynFxDebugLog(DynFx* pFx, FORMAT_STR const char* format, ...);
#define dynFxDebugLog(pFx, format, ...) dynFxDebugLog(pFx, FORMAT_STRING_CHECKED(format), __VA_ARGS__)


#if !PLATFORM_CONSOLE
#define DYNFX_LOGGING 1
#endif

#ifdef DYNFX_LOGGING
#define dynFxLog(fx, format, ...) { if (dynDebugState.bFxLogging && fx->bDebug) dynFxDebugLog(fx, format, __VA_ARGS__); }
#else
#define dynFxLog(fx, format, ...) {}
#endif

void dynFxDebugPushTracker( const char* pFxInfoName );
void dynFxDebugRemoveTracker( const char* pcFxInfoName );
StashTable dynFxDebugGetTrackerTable(void);
void dynFxDebugSort(eDebugSortMode eSortMode);
void dfxKillAll(void);
void dfxPauseAll(int pause);
void dynFxDebugClearEmpty(void);
DynFxTracker* dynFxGetTracker( const char* pcFxInfoName );
void dfxEdit(const char* fx_name);

void dynFxLogTriggerImpact(Vec3 vPos, Vec3 vDir);
void dynFxTriggerImpactLogUpdate(F32 fDeltaTime);
typedef void (*dynFxDrawTriggerImpact)(Vec3 vPos, Vec3 vDir, U32 uiARGB);

void dynFxTriggerImpactDrawEach(dynFxDrawTriggerImpact drawFunc);


void dynDrawTrackersClear(void);
DynDrawTracker* dynDrawTrackerPush(void);
DynDrawTracker* dynDrawTrackerFind(const char* pcName);
void dynDrawTrackersPostProcess(void);


typedef void (*gclUpdateFunc)(F32 fDeltaTime);
typedef void (*gclDisplayFunc)(void);
void dynFxDebugSetCallbacks(gclUpdateFunc func, gclDisplayFunc displayFunc);

void dynFxRecordInfo(const char* pcName);
void dynFxDebugTick(F32 fDeltaTime);

void dfxNoCostumeFX(int bNoFx);
void dfxNoUIFX(int bNoFx);
void dfxNoEnvironmentFX(int bNoFx);
void dfxRecord(void);
