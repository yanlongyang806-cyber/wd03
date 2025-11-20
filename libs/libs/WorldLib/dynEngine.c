
#include "timing.h"
#include "EventTimingLog.h"



#include "dynThread.h"
#include "dynSkeleton.h"
#include "dynSkeletonMovement.h"
#include "dynAnimTrack.h"
#include "dynMove.h"
#include "dynMoveTransition.h"
#include "dynAnimOverride.h"
#include "dynAnimTemplate.h"
#include "dynAnimGraph.h"
#include "dynAnimChart.h"
#include "dynSeqData.h"
#include "dynFx.h"
#include "dynFxInfo.h"
#include "dynFxDebug.h"
#include "dynFxFastParticle.h"
#include "dynFxInterface.h"
#include "dynCloth.h"
#include "dynRagdollData.h"
#include "dynStrand.h"
#include "dynGroundReg.h"
#include "dynAnimExpression.h"
#include "dynCriticalNodes.h"
#include "dynAnimNodeAlias.h"
#include "dynAnimNodeAuxTransform.h"
#include "dynAnimPhysInfo.h"
#include "dynWind.h"
#include "wlState.h"
#include "wlPerf.h"

static EventOwner* pEventTimer;
static void dynFxRunEveryFxUpdater(F32 fDeltaTime);

AUTO_STARTUP(DynAnimStances);
void dynAnimStancesStartup(void)
{
	// Note: these get loaded even if no other animation data is loaded so we can validate powers, etc
	if (gConf.bNewAnimationSystem)
	{
		dynLoadSkelBoneVisibilitySets();
		dynAnimStancesLoadAll();
	}
}


void dynStartup()
{
	loadstart_printf("Dynamics startup...");
	if ( (wl_state.load_flags & WL_LOAD_DYNFX_NAMES_FOR_VERIFICATION) ||
		!(wl_state.load_flags & WL_NO_LOAD_DYNANIMATIONS))
	{
		dynFxLoadFileNamesOnly();
	}
	if ((wl_state.load_flags & WL_NO_LOAD_DYNFX) && (wl_state.load_flags & WL_NO_LOAD_DYNANIMATIONS))
	{
		dynDebugState.bNoNewFx = true;
		dynDebugState.bNoAnimation = true;
		dtInitSys(); // Init dynNodes

		if (IsGameServerBasedType())
		{		
			// We still need to load the animation graphs so
			// the power art can access animation based lurches
			dynAnimGraphLoadAll();

			// We still need to load the animation node alias and aux transforms
			// since they can also be referenced by costume parts on WLCostumes
			dynAnimNodeAliasLoadAll();
			dynAnimNodeAuxTransformLoadAll();
		}

		loadend_printf("Dynamics startup done.");
		return;
	}	
	if (!pEventTimer)
		pEventTimer = etlCreateEventOwner("Dynamics Engine", "Dynamics", "WorldLib");
	if (!(wl_state.load_flags & WL_NO_LOAD_DYNMISC))
		dynBitListLoad();
	dynIRQGroupListLoad();
	if (!(wl_state.load_flags & WL_NO_LOAD_DYNFX))
		dynFxSysInit();
	if (!(wl_state.load_flags & WL_NO_LOAD_DYNANIMATIONS))
	{
		dynLoadAllBaseSkeletons();
		dynPreloadAnimTrackInfo();
		dynMoveLoadAll();
		//dynPreloadActionInfo();
		if (!gConf.bNewAnimationSystem)
		{
			if (!(wl_state.load_flags & WL_NO_LOAD_DYNMISC))
				dynAnimOverrideListLoadAll();
			if (!(wl_state.load_flags & WL_NO_LOAD_DYNMISC))
				dynSeqDataLoadAll();
			dynSeqDebugInitDebugBits();
			dynSeqFindNOLODBitIndex();
		}
		else
		{
			dynMovementSetLoadAll();
			dynMoveTransitionLoadAll();
			dynAnimTemplateLoadAll();
			dynAnimGraphLoadAll();
			dynAnimChartLoadAll();
		}
		dynClothInfoLoadAll();
		dynBouncerInfoLoadAll();
		dynRagdollDataLoadAll();
		dynStrandDataSetLoadAll();
		dynGroundRegDataLoadAll();
		dynAnimExpressionSetLoadAll();
		dynCriticalNodeListLoadAll();
		dynAnimNodeAliasLoadAll();
		dynAnimNodeAuxTransformLoadAll();
	}
	else if (IsGameServerBasedType())
	{
		// We still need to load the animation graphs so
		// the power art can access animation based lurches
		dynAnimGraphLoadAll();

		// We still need to load the animation node alias and aux transforms
		// since they can also be referenced by costume parts on WLCostumes
		dynAnimNodeAliasLoadAll();
		dynAnimNodeAuxTransformLoadAll();
	}

	dynDebugState.cloth.iMaxLOD = 3;
	dynDebugState.cloth.fMaxMovementToSkinnedPos = 0.1f;

	dynWindStartup();
	dtInitSys();
	if (GetAppGlobalType() == GLOBALTYPE_GAMESERVER)
	{
		dynDebugState.bNoNewFx = true;
	}
	loadend_printf("Dynamics startup done.");
}

void dynSystemUpdate(F32 fDeltaTime)
{
	if (GetAppGlobalType() == GLOBALTYPE_GAMESERVER &&
		TRUE_THEN_RESET(dynDebugState.bReloadServerAnimData))
	{
		//required for lurching during powers on game servers
		dynAnimGraphServerReload();
	}

	if (wl_state.dynEnabled)
	{
		PERFINFO_AUTO_START_FUNC_PIX();

		CHECK_FX_COUNT;

		if ((wl_state.load_flags & WL_NO_LOAD_DYNFX) && (wl_state.load_flags & WL_NO_LOAD_DYNANIMATIONS))
		{
			dynDebugState.bNoNewFx = true;
			dynDebugState.bNoAnimation = true;
			PERFINFO_AUTO_STOP_FUNC_PIX();
			return;
		}
		if (GetAppGlobalType() == GLOBALTYPE_GAMESERVER)
		{
			PERFINFO_AUTO_START_PIX("Server Dynamics Update", 1);
			if (!(wl_state.load_flags & WL_NO_LOAD_DYNANIMATIONS))
			{
				etlAddEvent(pEventTimer, "Update skeletons", ELT_CODE, ELTT_BEGIN);
				dynServerUpdateAllSkeletons(fDeltaTime);
				etlAddEvent(pEventTimer, "Update skeletons", ELT_CODE, ELTT_END);
			}
			PERFINFO_AUTO_STOP_CHECKED_PIX("Server Dynamics Update");
		}
		else
		{
			if (!(wl_state.load_flags & WL_NO_LOAD_DYNANIMATIONS))
			{
				wlPerfStartAnimBudget();
				etlAddEvent(pEventTimer, "Update skeletons", ELT_CODE, ELTT_BEGIN);
				dynSkeletonUpdateAll(fDeltaTime);
				etlAddEvent(pEventTimer, "Update skeletons", ELT_CODE, ELTT_END);
				wlPerfEndAnimBudget();
			}

			wlPerfSortOutAnimTime();

			// currently wind is in FX
			wlPerfStartFXBudget();
			if (!(wl_state.load_flags & WL_NO_LOAD_DYNFX))
			{
				etlAddEvent(pEventTimer, "Update FX", ELT_CODE, ELTT_BEGIN);
				dynFxRunEveryFxUpdater(fDeltaTime);
				dynFxManUpdateAll(fDeltaTime);
				dynFxFastParticleSetOrphansUpdate(fDeltaTime);
				dynFxDebugTick(fDeltaTime);
				dynFxTriggerImpactLogUpdate(fDeltaTime);
				etlAddEvent(pEventTimer, "Update FX", ELT_CODE, ELTT_END);
			}

			dynWindUpdate(fDeltaTime);
			dynWindWaitUpdateComplete();			// TODO: move this somewhere else to hide approx 1.4ms of latenency on PS3
			wlPerfEndFXBudget();

			dynClothUpdateCache(fDeltaTime);
		}
		PERFINFO_AUTO_STOP_FUNC_PIX();
	}
}


// Debugging tools 

void dynEngineSetTestManager(DynFxManager* pTest)
{
	dynDebugState.pDefaultTestManager = pTest;
}

// Start the named fx on the your entity.
// Used for testing fx.

static dtFx lastTestedFx = 0;

void dfxTestFxHelper(const char* fx_name, DynFxManager* pTestManager)
{
	const char* pcSwappedName;
	if (!pTestManager)
		return;
	pcSwappedName = dynFxManSwapFX(pTestManager, fx_name, false);
	if (!dynFxInfoExists(pcSwappedName))
	{
		Errorf("Can't find FX %s", pcSwappedName);
		return;
	}
	{
		DynAddFxParams params = {0};
		DynFx *pFx;
		params.pTargetRoot = dynFxManGetTestTargetNode(pTestManager);
		params.fHue = dynDebugState.fTestHue;
		params.fSaturation = dynDebugState.fTestSaturation;
		params.fValue = dynDebugState.fTestValue;
		params.eSource = eDynFxSource_Test;
		pFx = dynAddFx(pTestManager, pcSwappedName, &params);

		if(pFx) {
			lastTestedFx = dtFxGetFxGuid(pFx);
		}
	}
}

// Takes the name of an FX to test, on the local player.
AUTO_COMMAND ACMD_CATEGORY(dynFx);
void dfxTestFx( const char* fx_name ACMD_NAMELIST("DynFxInfo", RESOURCEINFO) )
{
	DynFxManager* pTestManager = dynDebugState.pTestManager?dynDebugState.pTestManager:dynDebugState.pDefaultTestManager;
	dfxTestFxHelper(fx_name, pTestManager);
}

// Takes the name of an FX to test, on the targeted entity.
AUTO_COMMAND ACMD_CATEGORY(dynFx);
void dfxTestFxOnTarget( const char* fx_name ACMD_NAMELIST("DynFxInfo", RESOURCEINFO) )
{
	DynFxManager* pTestManager = dynDebugState.pTestManager?dynDebugState.pTestManager:dynDebugState.pDefaultTestManager;
	if (pTestManager)
	{
		DynFxManager* pTargetManager = dynFxManFromGuid(pTestManager->targetManagerGuid);
		if (pTargetManager)
			dfxTestFxHelper(fx_name, pTargetManager);
		else
			Errorf("Could not find target to play FX on");
	}
}

AUTO_COMMAND ACMD_CATEGORY(dynFx);
void dfxTestFxChangeColor( int index, Vec4 vColor ) {
	DynFx *pFx = dynFxFromGuid(lastTestedFx);
	if(pFx) {
		dynFxSetColor(pFx, index, vColor);
	}
}


const int iNumConcurrentTestFx = 100;
static int iRunEveryFxIndex = -1;

static void dynFxKillIfTooOld(DynFx* pFx, void* pUnused)
{
	if (FLOATTIME(pFx->uiTimeSinceStart) > 3.0f)
	{
		dynFxKill(pFx, true, true, false, eDynFxKillReason_ManualKill);
	}
}

static void dynFxRunEveryFxUpdater(F32 fDeltaTime)
{
	int iSize;
	int iNumToAdd;
	ResourceDictionaryInfo *info;

	if (iRunEveryFxIndex < 0)
		return;

	dynFxForEachFx(dynFxKillIfTooOld, NULL);

	info = resDictGetInfo(hDynFxInfoDict);
	iSize = eaSize(&info->ppInfos);
	iNumToAdd = 2;//iNumConcurrentTestFx - dynFxDebugFxCount();
	{
		int iNumAdded = 0;
		while (iNumAdded < iNumToAdd)
		{
			if (iRunEveryFxIndex >= iSize)
			{
				iRunEveryFxIndex = -1;
				return;
			}
			{
				ResourceInfo* pInfo2 = info->ppInfos[iRunEveryFxIndex++];
				printf("%s\n", pInfo2->resourceName);
				dtTestFx(pInfo2->resourceName);
				++iNumAdded;
			}
		}
	}
}

// Spawns on of every FX
AUTO_COMMAND ACMD_CATEGORY(dynFx);
void dfxRunEveryFX()
{
	printf("\n");
	iRunEveryFxIndex = 0;

	/*
	{
		ResourceDictionaryInfo *info = resDictGetInfo(hDynFxInfoDict);
		FOR_EACH_IN_EARRAY(info->ppInfos, ResourceInfo, info2)
		{
			dtTestFx(info2->resourceName);
		}
		FOR_EACH_END;
	}
	if (0) {
		DictionaryEArrayStruct *pStruct = resDictGetEArrayStruct(hDynFxInfoDict);
		FOR_EACH_IN_EARRAY(pStruct->ppReferents, DynFxInfo, pInfo)
		{
			dtTestFx(pInfo->pcDynName);
		}
		FOR_EACH_END;
	}
	*/
}


// Takes the name of a 2D FX to test, on the UI.
AUTO_COMMAND ACMD_CATEGORY(dynFx);
void dfxTestFx2d(const char* fx_name ACMD_NAMELIST("DynFxInfo", RESOURCEINFO))
{
    DynFxManager* uiManager = dynFxGetUiManager(false);
	if (!uiManager)
		return;
	if (!dynFxInfoExists(fx_name))
	{
		Errorf("Can't find FX %s", fx_name);
		return;
	}
	{
		DynAddFxParams params = {0};
		params.pTargetRoot = dynFxManGetTestTargetNode(uiManager);
		params.fHue = dynDebugState.fTestHue;
		params.fSaturation = dynDebugState.fTestSaturation;
		params.fValue = dynDebugState.fTestValue;
		params.eSource = eDynFxSource_Test;
		dynAddFx(uiManager, fx_name, &params);
	}
}

// Takes a value from 0 to 360. Sets the override test hue. Requires dfxSetHueOverride to work.
AUTO_COMMAND ACMD_CATEGORY(dynFx);
void dfxSetTestHue(F32 fHue)
{
	dynDebugState.fTestHue = CLAMP(fHue, 0.0f, 360.0f);
}

AUTO_COMMAND ACMD_CATEGORY(dynFx);
void dfxSetTestSaturation(F32 fSaturation)
{
	dynDebugState.fTestSaturation = CLAMP(fSaturation, -1.0f, 1.0f);
}

AUTO_COMMAND ACMD_CATEGORY(dynFx);
void dfxSetTestValue(F32 fValue)
{
	dynDebugState.fTestValue = CLAMP(fValue, -1.0f, 1.0f);
}

// Sets whether or not the test hue set in dfxSetTestHue will override any hues from the powers system
AUTO_COMMAND ACMD_CATEGORY(dynFx);
void dfxSetHueOverride(int i)
{
	dynDebugState.bGlobalHueOverride = !!i;
}

