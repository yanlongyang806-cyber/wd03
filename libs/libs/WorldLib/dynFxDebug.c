#include "dynFxDebug.h"

#include "MemoryPool.h"
#include "qsortG.h"
#include "dynFxInfo.h"
#include "estring.h"
#include "dynFx.h"
#include "StringCache.h"
#include "dynFxParticle.h"
#include "dynFxPhysics.h"
#include "mathutil.h"
#include "error.h"


#include "dynFxDebug_h_ast.h"
#include "dynFxEnums_h_ast.h"
#include "dynFxManager_h_ast.h"
#include "dynFxParticle_h_ast.h"


AUTO_RUN_ANON(memBudgetAddMapping(__FILE__, BUDGET_FXSystem););


static StashTable stDynFxTrackers;


static void dynFxKillFxFromSource(DynFx* pFxToKill, void* pSource)
{
	eDynFxSource eSource = (eDynFxSource)pSource;
	if (pFxToKill->eSource == eSource)
		dynFxKill(pFxToKill, true, true, false, eDynFxKillReason_ManualKill);
}

AUTO_CMD_INT(dynDebugState.bDebugPhysics, dfxDrawPhysics) ACMD_CATEGORY(dynFx);

AUTO_COMMAND ACMD_CATEGORY(dynFx);
void dfxNoCostumeFX(int bNoFx)
{
	dynDebugState.bNoCostumeFX = bNoFx;
	if (bNoFx)
	{
		dynFxForEachFx(dynFxKillFxFromSource, (void*)eDynFxSource_Costume);
	}
}

AUTO_COMMAND ACMD_CATEGORY(dynFx);
void dfxNoUIFX(int bNoFx)
{
	dynDebugState.bNoUIFX = bNoFx;
	if (bNoFx)
	{
		dynFxForEachFx(dynFxKillFxFromSource, (void*)eDynFxSource_UI);
	}
}

AUTO_COMMAND ACMD_CATEGORY(dynFx);
void dfxNoEnvironmentFX(int bNoFx)
{
	dynDebugState.bNoEnvironmentFX = bNoFx;
	if (bNoFx)
	{
		dynFxForEachFx(dynFxKillFxFromSource, (void*)eDynFxSource_Environment);
		dynFxForEachFx(dynFxKillFxFromSource, (void*)eDynFxSource_Volume);
	}
}

// Writes a breakdown of the current fx and their counts to the console
// A precursor to the fx debugging window
AUTO_COMMAND ACMD_CATEGORY(DynFx);
void dfxWriteTrackers()
{
	StashTableIterator iter;
	StashElement elem;
	stashGetIterator(stDynFxTrackers, &iter);
	while (stashGetNextElement(&iter, &elem))
	{
		DynFxTracker* pFxTracker = stashElementGetPointer(elem);
		if ( pFxTracker->uiNumFx > 0 )
		{
			printf(
				"%s - %d - %d - %d\n",
				(char*)stashElementGetKey(elem),
				pFxTracker->uiNumFx,
				pFxTracker->uiNumFxObjects,
				pFxTracker->uiNumPhysicsObjects);
		}
	}
}


StashTable dynFxDebugGetTrackerTable()
{
	return stDynFxTrackers;
}

U32 dynFxDebugFxCount()
{
	StashTableIterator iter;
	StashElement elem;
	U32 uiCount = 0;
	stashGetIterator(stDynFxTrackers, &iter);
	while (stashGetNextElement(&iter, &elem))
	{
		DynFxTracker* pFxTracker = stashElementGetPointer(elem);
		uiCount += pFxTracker->uiNumFx;
	}
	return uiCount;
}

#define DYN_DEFAULT_MAX_FX_TRACKER_COUNT 256
MP_DEFINE(DynFxTracker);
DynFxTracker** eaDynFxTrackers;

void dynFxDebugPushTracker( const char* pFxInfoName ) 
{
	// Tracking code
	{
		DynFxTracker* pFxTracker;

		if ( !stDynFxTrackers )
			stDynFxTrackers = stashTableCreateWithStringKeys(1024, StashDefault);

		if ( !stashFindPointer(stDynFxTrackers, pFxInfoName, &pFxTracker) )
		{
			MP_CREATE(DynFxTracker, DYN_DEFAULT_MAX_FX_TRACKER_COUNT);
			pFxTracker = MP_ALLOC(DynFxTracker);
			pFxTracker->pcFxName = pFxInfoName;
			stashAddPointer(stDynFxTrackers, pFxInfoName, pFxTracker, false );
			eaPush(&eaDynFxTrackers, pFxTracker);
		}
		++pFxTracker->uiNumFx;
		pFxTracker->bAddedSinceUpdate = true;
		if (pFxTracker->uiNumFx > pFxTracker->uiNumFxPeak)
			pFxTracker->uiNumFxPeak = pFxTracker->uiNumFx;

		{
			pFxTracker->iPriorityLevel = dynFxInfoGetPriorityLevel(pFxInfoName);
		}
		/* Disabled for now, too many false positives
		if (pFxTracker->uiNumFx > 300)
		{
			REF_TO(DynFxInfo) hInfo;
			DynFxInfo* pInfo;
			SET_HANDLE_FROM_STRING(hDynFxInfoDict, pFxTracker->pcFxName, hInfo);
			pInfo = GET_REF(hInfo);
			if (pInfo && !pInfo->bDontLeakTest)
			{
				FxFileError(pInfo->pcFileName, "FX count of %d exceeds 300: probable leak!", pFxTracker->uiNumFx);
				pInfo->bVerified = false;
				dynFxManStopUsingFxInfo(0, pFxTracker->pcFxName, false);
			}
			REMOVE_HANDLE(hInfo);
		}
		*/
	}
}

DynFxTracker* dynFxGetTracker( const char* pcFxInfoName )
{
	if (stDynFxTrackers)
	{
		DynFxTracker* pFxTracker;
		if (stashFindPointer(stDynFxTrackers, pcFxInfoName, &pFxTracker))
			return pFxTracker;

	}
	return NULL;
}

void dynFxDebugRemoveTracker( const char* pcFxInfoName ) 
{
	DynFxTracker* pFxTracker = dynFxGetTracker(pcFxInfoName);
	if (pFxTracker)
	{
		--pFxTracker->uiNumFx;
	}
}

int dynFxDebugCompareNum(const DynFxTracker** a, const DynFxTracker** b)
{
	return (*b)->uiNumFx - (*a)->uiNumFx;
}

int dynFxDebugCompareNumReverse(const DynFxTracker** a, const DynFxTracker** b)
{
	return (*a)->uiNumFx - (*b)->uiNumFx;
}

int dynFxDebugComparePeak(const DynFxTracker** a, const DynFxTracker** b)
{
	return (*b)->uiNumFxPeak - (*a)->uiNumFxPeak;
}

int dynFxDebugComparePeakReverse(const DynFxTracker** a, const DynFxTracker** b)
{
	return (*a)->uiNumFxPeak - (*b)->uiNumFxPeak;
}

int dynFxDebugCompareMem(const DynFxTracker** a, const DynFxTracker** b)
{
	return (*b)->iMemUsage - (*a)->iMemUsage;
}

int dynFxDebugCompareMemReverse(const DynFxTracker** a, const DynFxTracker** b)
{
	return (*a)->iMemUsage - (*b)->iMemUsage;
}

int dynFxDebugCompareLevel(const DynFxTracker** a, const DynFxTracker** b)
{
	return (*b)->iPriorityLevel - (*a)->iPriorityLevel;
}

int dynFxDebugCompareLevelReverse(const DynFxTracker** a, const DynFxTracker** b)
{
	return (*a)->iPriorityLevel - (*b)->iPriorityLevel;
}

int dynFxDebugComparePhysics(const DynFxTracker** a, const DynFxTracker** b)
{
	return (*b)->uiNumPhysicsObjects - (*a)->uiNumPhysicsObjects;
}

int dynFxDebugComparePhysicsReverse(const DynFxTracker** a, const DynFxTracker** b)
{
	return (*a)->uiNumPhysicsObjects - (*b)->uiNumPhysicsObjects;
}

int dynFxDebugCompareName(const DynFxTracker** a, const DynFxTracker** b)
{
	return stricmp((*a)->pcFxName, (*b)->pcFxName); 
}

int dynFxDebugCompareNameReverse(const DynFxTracker** a, const DynFxTracker** b)
{
	return stricmp((*b)->pcFxName, (*a)->pcFxName); 
}


static void dynFxDebugUpdateTrackerList(F32 fDeltaTime)
{
	U32 uiNumTrackers = eaSize(&eaDynFxTrackers);
	U32 uiTracker;
	for (uiTracker=0; uiTracker<uiNumTrackers; ++uiTracker)
	{
		if (eaDynFxTrackers[uiTracker]->uiNumFx == 0 && !eaDynFxTrackers[uiTracker]->bAddedSinceUpdate)
		{
			eaDynFxTrackers[uiTracker]->fTimeSinceZero += fDeltaTime;
			if (dynDebugState.bAutoClearTrackers && eaDynFxTrackers[uiTracker]->fTimeSinceZero > 5.0f)
			{
				DynFxTracker* pRemoved = eaRemove(&eaDynFxTrackers, uiTracker);
				--uiTracker;
				--uiNumTrackers;
				stashRemovePointer(stDynFxTrackers, pRemoved->pcFxName, NULL);
				MP_FREE(DynFxTracker, pRemoved);
			}
		}
		else
		{
			eaDynFxTrackers[uiTracker]->fTimeSinceZero = 0.0f;
			eaDynFxTrackers[uiTracker]->bAddedSinceUpdate = false;
		}
	}
}

void dynFxDebugSort(eDebugSortMode eSortMode)
{
	switch (eSortMode)
	{
		xcase eDebugSortMode_Num:
			eaQSortG(eaDynFxTrackers, dynFxDebugCompareNum);
		xcase eDebugSortMode_NumReverse:
			eaQSortG(eaDynFxTrackers, dynFxDebugCompareNumReverse);

		xcase eDebugSortMode_Name:
			eaQSortG(eaDynFxTrackers, dynFxDebugCompareName);
		xcase eDebugSortMode_NameReverse:
			eaQSortG(eaDynFxTrackers, dynFxDebugCompareNameReverse);

		xcase eDebugSortMode_Peak:
			eaQSortG(eaDynFxTrackers, dynFxDebugComparePeak);
		xcase eDebugSortMode_PeakReverse:
			eaQSortG(eaDynFxTrackers, dynFxDebugComparePeakReverse);

		xcase eDebugSortMode_Mem:
			eaQSortG(eaDynFxTrackers, dynFxDebugCompareMem);
		xcase eDebugSortMode_MemReverse:
			eaQSortG(eaDynFxTrackers, dynFxDebugCompareMemReverse);

		xcase eDebugSortMode_Level:
			eaQSortG(eaDynFxTrackers, dynFxDebugCompareLevel);
		xcase eDebugSortMode_LevelReverse:
			eaQSortG(eaDynFxTrackers, dynFxDebugCompareLevelReverse);

		xcase eDebugSortMode_PhysicsObjects:
			eaQSortG(eaDynFxTrackers, dynFxDebugComparePhysics);
		xcase eDebugSortMode_PhysicsObjectsReverse:
			eaQSortG(eaDynFxTrackers, dynFxDebugComparePhysicsReverse);

		xcase eDebugSortMode_None:
		default:
			{

			}
	};
}


void dynFxDebugClearEmpty()
{
	U32 uiNumTrackers = eaSize(&eaDynFxTrackers);
	U32 uiTracker;
	for (uiTracker=0; uiTracker<uiNumTrackers; ++uiTracker)
	{
		if (eaDynFxTrackers[uiTracker]->uiNumFx == 0 )//&& eaDynFxTrackers[uiTracker]->fTimeSinceZero > 5.0f)
		{
			DynFxTracker* pRemoved = eaRemove(&eaDynFxTrackers, uiTracker);
			--uiTracker;
			--uiNumTrackers;
			stashRemovePointer(stDynFxTrackers, pRemoved->pcFxName, NULL);
			MP_FREE(DynFxTracker, pRemoved);
		}
	}
}

void dynFxClearLog(void)
{
	eaClearStruct(&fxLog.eaLines, parse_DynFxLogLine);
}


#undef dynFxDebugLog
void dynFxDebugLog(DynFx* pFx, const char* format, ...)
{
	DynFxLogLine* pLine = StructCreate(parse_DynFxLogLine);
	eaPush(&fxLog.eaLines, pLine);
	pLine->uiGuid = pFx->uiFXID;
	pLine->fTime = FLOATTIME(pFx->uiTimeSinceStart>5?pFx->uiTimeSinceStart:0);
	pLine->pcFXInfo = allocAddString(REF_STRING_FROM_HANDLE(pFx->hInfo));
	estrConcatf(&pLine->pcFXID, "%5d", pLine->uiGuid);
	VA_START(va, format);
		estrConcatf(&pLine->esLine, "%5d- ", eaSize(&fxLog.eaLines));
		estrConcatfv(&pLine->esLine, format, va);
	VA_END();

	if (dynDebugState.bFxLogState && pFx->pParticle)
	{
		pLine->pState = StructAlloc(parse_DynFxLogState);
		pLine->pState->pDraw = StructClone(parse_DynDrawParticle, pFx->pParticle->pDraw);
		dynNodeGetWorldSpaceTransform(dynFxGetNode(pFx), &pLine->pState->xform);
	}
	while (eaSize(&fxLog.eaLines) > 3000)
	{
		DynFxLogLine* pFreeLine = eaRemove(&fxLog.eaLines, 0);
		StructDestroy(parse_DynFxLogLine, pFreeLine);
	}
}

typedef struct TriggerImpactLog
{
	Vec3 vPos;
	Vec3 vDir;
	F32 fTimeLeft;
} TriggerImpactLog;

MP_DEFINE(TriggerImpactLog);
TriggerImpactLog** eaLog;

void dynFxLogTriggerImpact(Vec3 vPos, Vec3 vDir)
{
	TriggerImpactLog* pLog;
	MP_CREATE(TriggerImpactLog, 128);

	pLog = MP_ALLOC(TriggerImpactLog);
	copyVec3(vPos, pLog->vPos);
	copyVec3(vDir, pLog->vDir);
	pLog->fTimeLeft = 5.0f;
	eaPush(&eaLog, pLog);
}

void dynFxTriggerImpactLogUpdate(F32 fDeltaTime)
{
	FOR_EACH_IN_EARRAY(eaLog, TriggerImpactLog, pLog)
	{
		pLog->fTimeLeft -= fDeltaTime;
		if (pLog->fTimeLeft <= 0.0f)
		{
			MP_FREE(TriggerImpactLog, pLog);
			eaRemove(&eaLog, ipLogIndex);
		}
	}
	FOR_EACH_END;
}


void dynFxTriggerImpactDrawEach(dynFxDrawTriggerImpact drawFunc)
{
	FOR_EACH_IN_EARRAY(eaLog, TriggerImpactLog, pLog)
	{
		U32 uiARGB = 0x00FF0000;
		U32 uiAlpha;
		F32 fAlphaPercent = (pLog->fTimeLeft / 3.0f);
		fAlphaPercent = CLAMP(fAlphaPercent, 0.0f, 1.0f);
		uiAlpha = (U32)((F32)0xFF * fAlphaPercent) & 0xFF;
		uiARGB |= (uiAlpha << 24);
		drawFunc(pLog->vPos, pLog->vDir, uiARGB);
	}
	FOR_EACH_END;
}

MP_DEFINE(DynDrawTracker);

DynDrawTracker** eaDynDrawTrackers;
const char** eaDynDrawOnly = NULL;

static void dynDrawTrackerFree(DynDrawTracker* pToFree)
{
	MP_FREE(DynDrawTracker, pToFree);
}

void dynDrawTrackersClear(void)
{
	eaClearEx(&eaDynDrawTrackers, dynDrawTrackerFree);
}

DynDrawTracker* dynDrawTrackerPush(void)
{
	DynDrawTracker* pNew;
	MP_CREATE(DynDrawTracker, 128);
	pNew = MP_ALLOC(DynDrawTracker);
	eaPush(&eaDynDrawTrackers, pNew);
	return pNew;
}

// linear search for now
DynDrawTracker* dynDrawTrackerFind(const char* pcName)
{
	FOR_EACH_IN_EARRAY(eaDynDrawTrackers, DynDrawTracker, pTracker)
	{
		if (pTracker->pcName == pcName)
			return pTracker;
	}
	FOR_EACH_END;
	return NULL;
}

void dynDrawTrackersPostProcess(void)
{
	DynDrawTracker* pPrevTracker = NULL;
	FOR_EACH_IN_EARRAY_FORWARDS(eaDynDrawTrackers, DynDrawTracker, pTracker)
	{
		if  (pPrevTracker && pPrevTracker->pcName == pTracker->pcName)
		{
			pPrevTracker->uiNum += pTracker->uiNum;
			pPrevTracker->uiSubObjects += pTracker->uiSubObjects;
			dynDrawTrackerFree(pTracker);
			eaRemove(&eaDynDrawTrackers, ipTrackerIndex);
			--ipTrackerIndex;
			--cipTrackerNum;
		}
		else
			pPrevTracker = pTracker;

	}
	FOR_EACH_END;
}

static gclUpdateFunc GCLUpdateFunc = NULL;
static gclDisplayFunc GCLDisplayFunc = NULL;

void dynFxDebugSetCallbacks(gclUpdateFunc func, gclDisplayFunc displayFunc)
{
	GCLUpdateFunc = func;
	GCLDisplayFunc = displayFunc;
}



DynFxRecording fxRecording;
StashTable stFxRecordNames;

AUTO_COMMAND ACMD_CATEGORY(DynFx);
void dfxRecord(void)
{
	if (dynDebugState.bRecordFXProfile)
	{
		// Stop recording
		dynDebugState.bRecordFXProfile = false;
		if (GCLDisplayFunc)
			GCLDisplayFunc();
	}
	else
	{
		if (!stFxRecordNames)
			stFxRecordNames = stashTableCreateWithStringKeys(16, StashDefault);
		else
			stashTableClear(stFxRecordNames);
		dynDebugState.bRecordFXProfile = true;
		ZeroStruct(&fxRecording);
	}
}


void dynFxRecordInfo(const char* pcName)
{
	stashAddInt(stFxRecordNames, pcName, 1, false);
}


void dynFxDebugTick(F32 fDeltaTime)
{
	dynFxDebugUpdateTrackerList(fDeltaTime);

	if (GCLUpdateFunc)
		GCLUpdateFunc(fDeltaTime);

	if (dynDebugState.bRecordFXProfile)
	{
		MAX1(fxRecording.uiNumDynFxCreatedPeak, fxRecording.uiNumDynFxCreated);
		fxRecording.uiNumDynFxCreatedTotal += fxRecording.uiNumDynFxCreated;
		fxRecording.uiNumDynFxCreated = 0;

		MAX1(fxRecording.uiNumDynFxUpdatedPeak, fxRecording.uiNumDynFxUpdated);
		fxRecording.uiNumDynFxUpdatedTotal += fxRecording.uiNumDynFxUpdated;
		fxRecording.uiNumDynFxUpdated = 0;

		MAX1(fxRecording.uiNumDynFxDrawnPeak, fxRecording.uiNumDynFxDrawn);
		fxRecording.uiNumDynFxDrawnTotal += fxRecording.uiNumDynFxDrawn;
		fxRecording.uiNumDynFxDrawn = 0;
	}

	if (dynDebugState.bDebugPhysics)
	{
		dynFxPhysicsDrawScene();
	}
}

#include "dynFxDebug_h_ast.c"
