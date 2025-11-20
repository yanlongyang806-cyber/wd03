#include "aiMastermind.h"
#include "aiMastermindHeat.h"
#include "aiMastermindExpose.h"

#include "aiExtern.h"
#include "aiLib.h"
#include "aiMovement.h"
#include "aiStruct.h"
#include "aiTeam.h"
#include "Entity.h"
#include "entCritter.h"
#include "gslSpawnPoint.h"
#include "MemoryPool.h"
#include "PowerModes.h"
#include "rand.h"
#include "RoomConn.h"
#include "StateMachine.h"
#include "StringCache.h"
#include "wlEncounter.h"
#include "WorldColl.h"
#include "WorldGrid.h"
#include "WorldLib.h"
#include "beaconAStar.h"
#include "gslInteractable.h"
#include "EntityIterator.h"
#include "beaconPath.h"
#include "aiMastermind_h_ast.h"

int g_bMastermindDebug = false;
AUTO_CMD_INT(g_bMastermindDebug, aiMastermindDebug);

void aiMastermind_DebugPrint(const char *format, ...);
// #define MastermindDebug(format, ...) if(g_bMastermindDebug) aiMastermind_DebugPrintWithTime(format,##__VA_ARGS__)
static void aiMastermind_SetCurrentDef(const char *pcMMDef);


// -----------------------------------------------
typedef struct AIMMManager
{
	REF_TO(AIMastermindDef)	hCurDef;
	
	ExprContext		*exprContext;

	// timers
	S64			timeLastUpdate;
	S64			timeStarted;

	U32			isEnabled : 1;
	U32			bFirstTickInit : 1;
} AIMMManager;

static AIMMManager s_masterMind = {0};
static ExprFuncTable* s_mmFuncTable = NULL;

static AIMMHeatLevelDef* aiMastermind_GetCurrentHeatLevelDef(AIMastermindDef *pDef);


// ------------------------------------------------------------------------------------------------------------------
MP_DEFINE(AIMastermindAIVars);

static AIMastermindAIVars* aiMastermindAIVars_Alloc()
{
	MP_CREATE(AIMastermindAIVars, 16);
	return MP_ALLOC(AIMastermindAIVars);
}

// ------------------------------------------------------------------------------------------------------------------
void aiMastermindAIVars_Free(AIMastermindAIVars* vars)
{
	if (vars)
		MP_FREE(AIMastermindAIVars, vars);
}

// ------------------------------------------------------------------------------------------------------------------
void aiMastermind_DestroyAIVars(Entity *e)
{
	if (e->aibase->mastermindVars)
	{
		AIMastermindAIVars *vars = e->aibase->mastermindVars;
		if (IS_HANDLE_ACTIVE(vars->hPrevFSM))
		{
			REMOVE_HANDLE(vars->hPrevFSM);
		}
		if (vars->configMods)
		{
			aiConfigMods_RemoveConfigMods(e, vars->configMods);
			eaiDestroy(&(vars->configMods));
		}
		aiMastermindAIVars_Free(vars);
		e->aibase->mastermindVars = NULL;
	}
}


// ------------------------------------------------------------------------------------------------------------------
AIMastermindDef *aiMastermind_GetDef()
{
	return GET_REF(s_masterMind.hCurDef);
}


// ------------------------------------------------------------------------------------------------------------------
void aiMastermind_DebugPrint(const char *format, ...)
{
	if (g_bMastermindDebug)
	{
		char *str = NULL;

		F32 fTimeSince = ABS_TIME_TO_SEC(ABS_TIME_SINCE(s_masterMind.timeStarted));

		S32 iTimeSince = (S32)floor(fTimeSince);
		S32 iSeconds = iTimeSince % 60;
		S32 iMinutes = (iTimeSince / 60) % 60;
		S32 iHours = iMinutes / 60; 

		VA_START(args, format);
		estrConcatfv(&str, format, args);
		VA_END();

		printf("MM@%dm:%ds - %s\n", iMinutes, iSeconds, str);

		estrDestroy(&str);
	}
	
}

// ---------------------------------------------------------------------------------------------------------------------------------
static int aiMastermind_ValidateDef(enumResourceValidateType eType, const char* pDictName, 
									 const char* pResourceName, void* pResource, U32 userID)
{
	AIMastermindDef* mastermindDef = pResource;

	switch (eType)
	{
		case RESVALIDATE_POST_TEXT_READING:
		{
			if (mastermindDef->pHeatDef)
				aiMastermindHeatDef_Validate(mastermindDef->pHeatDef, mastermindDef->pchFilename);

		} 
		return VALIDATE_HANDLED;

		case RESVALIDATE_FINAL_LOCATION:
		{
			if (mastermindDef->pExposeDef)
			{
				AIMastermindExposeDef *pExposeDef = mastermindDef->pExposeDef;
				pExposeDef->pParentMMDef = mastermindDef;
				if (pExposeDef->pchHiddenPowerMode)
					pExposeDef->iHiddenMode = StaticDefineIntGetInt(PowerModeEnum, pExposeDef->pchHiddenPowerMode);
				else 
					pExposeDef->iHiddenMode = -1;
			}

		}
		return VALIDATE_HANDLED;
	}

	return VALIDATE_NOT_HANDLED;
}

// ------------------------------------------------------------------------------------------------------------------
int fixupExpressionCallbacks(AIMMHeatExprCallbacks *pCB)
{
	int success = true;

	if (pCB->pExprCanSendWave)
		success &= exprGenerate(pCB->pExprCanSendWave, s_masterMind.exprContext);
	if (pCB->pExprOnPlayerDeath)
		success &= exprGenerate(pCB->pExprOnPlayerDeath, s_masterMind.exprContext);
	if (pCB->pExprOnWipe)
		success &= exprGenerate(pCB->pExprOnWipe, s_masterMind.exprContext);
	if (pCB->pExprPerTick)
		success &= exprGenerate(pCB->pExprPerTick, s_masterMind.exprContext);
	if (pCB->pExprShouldForceWave)
		success &= exprGenerate(pCB->pExprShouldForceWave, s_masterMind.exprContext);
	if (pCB->pExprEnteredNewRoom)
		success &= exprGenerate(pCB->pExprEnteredNewRoom, s_masterMind.exprContext);
	
	return success;
}

// ------------------------------------------------------------------------------------------------------------------
AUTO_FIXUPFUNC;
TextParserResult fixupAIMastermindDef(AIMastermindDef* config, enumTextParserFixupType eType, void *pExtraData)
{
	int success = true;
	switch (eType)
	{
		case FIXUPTYPE_POST_TEXT_READ:
		{
			if (config->pHeatDef)
			{
				fixupExpressionCallbacks(&config->pHeatDef->exprCallbacks);

				FOR_EACH_IN_EARRAY(config->pHeatDef->eaHeatLevels, AIMMHeatLevelDef, pMMHeat)
				{
					fixupExpressionCallbacks(&pMMHeat->exprCallbacks);
				}
				FOR_EACH_END
			}
		}

	}
	return success ? PARSERESULT_SUCCESS : PARSERESULT_ERROR;
}


// ------------------------------------------------------------------------------------------------------------------
ExprContext* aiMastermind_GetExprContext()
{
	return s_masterMind.exprContext;
}

// ------------------------------------------------------------------------------------------------------------------
int aiMastermind_IsMastermindMap()
{
	return s_masterMind.isEnabled;
}

void aiMastermindHeat_RoomUpdateCallback(RoomPortal* pPortal);
void aiMastermindHeat_RoomDestroyCallback(RoomPortal* pPortal);

// ------------------------------------------------------------------------------------------------------------------
static void aiMastermind_DefUpdatedCallback(const char *pszDef)
{
	aiMastermind_SetCurrentDef(pszDef);

	if (s_masterMind.isEnabled)
	{
		AIMastermindDef *pDef = aiMastermind_GetDef();
		if (pDef)
		{
			worldLibSetAISpawnerFunctions(	aiMastermind_DefUpdatedCallback, 
											(pDef->pHeatDef) ? aiMastermindHeat_RoomUpdateCallback : NULL, 
											(pDef->pHeatDef) ? aiMastermindHeat_RoomDestroyCallback : NULL);
		}
		
	}
}


// ------------------------------------------------------------------------------------------------------------------
void aiMastermind_Shutdown(AIMMManager *pHeatManager)
{
	aiMastermindExpose_Shutdown();

	aiMastermindHeat_Shutdown();

	if (s_masterMind.exprContext)
		exprContextDestroy(s_masterMind.exprContext);

	ZeroStruct(&s_masterMind);
}

// ------------------------------------------------------------------------------------------------------------------
void aiMastermind_Startup()
{
	if (!s_masterMind.exprContext)
	{
		s_masterMind.exprContext = exprContextCreate();

		s_mmFuncTable = exprContextCreateFunctionTable("AI_MasterMind");
		exprContextAddFuncsToTableByTag(s_mmFuncTable, "util");
		exprContextAddFuncsToTableByTag(s_mmFuncTable, "ai");
		exprContextAddFuncsToTableByTag(s_mmFuncTable, "encounter_action");

		exprContextSetFuncTable(s_masterMind.exprContext, s_mmFuncTable);
	}

	RefSystem_RegisterSelfDefiningDictionary(g_pcAIMasterMindDefDictName, false,
												parse_AIMastermindDef, true, false, NULL);

	resDictManageValidation(g_pcAIMasterMindDefDictName, aiMastermind_ValidateDef);

	if (isDevelopmentMode() || isProductionEditMode()) 
		resDictMaintainInfoIndex(g_pcAIMasterMindDefDictName, ".Name", NULL, NULL, NULL, NULL);
	
	resLoadResourcesFromDisk(g_pcAIMasterMindDefDictName, "ai/mastermind", ".mind", "AIMastermindDef.bin", 
									PARSER_FORCEREBUILD | RESOURCELOAD_SHAREDMEMORY | PARSER_OPTIONALFLAG);

	resDictProvideMissingResources(g_pcAIMasterMindDefDictName);
	resDictProvideMissingRequiresEditMode(g_pcAIMasterMindDefDictName);

	// move this to the place when we know there is a heat manager in the map
	worldLibSetAISpawnerFunctions(aiMastermind_DefUpdatedCallback, NULL, NULL);

	
}

// ------------------------------------------------------------------------------------------------------------------
void aiMastermind_OnMapLoad()
{
	const char *pcMastermindDef = zmapInfoGetMastermindDefKey(NULL);

	aiMastermind_SetCurrentDef(pcMastermindDef);

	if (s_masterMind.isEnabled)
	{
		AIMastermindDef *pDef;
		pDef = aiMastermind_GetDef();

		if (pDef->pExposeDef)
		{
			aiMastermindExpose_OnMapLoad();
		}
		else
		{
			aiMastermindHeat_OnMapLoad();
		}

		s_masterMind.timeStarted = ABS_TIME;
		s_masterMind.timeLastUpdate = ABS_TIME;
	}
}

// ------------------------------------------------------------------------------------------------------------------
void aiMastermind_OnMapUnload()
{
	if (s_masterMind.isEnabled)
	{
		aiMastermindHeat_OnMapUnload();
		aiMastermindExpose_OnMapUnload();
	}
	s_masterMind.timeLastUpdate = ABS_TIME;
	s_masterMind.bFirstTickInit = false;
}

// ------------------------------------------------------------------------------------------------------------------
static void aiMastermind_SetCurrentDef(const char *pcMMDef)
{
	if (IS_HANDLE_ACTIVE(s_masterMind.hCurDef))
	{
		const char *pcCur = REF_STRING_FROM_HANDLE(s_masterMind.hCurDef);
		if (pcCur == pcMMDef)
		{
			return;
		}

		REMOVE_HANDLE(s_masterMind.hCurDef);
	}

	if (pcMMDef)
	{
		AIMastermindDef *pDef;
		SET_HANDLE_FROM_STRING(g_pcAIMasterMindDefDictName, pcMMDef, s_masterMind.hCurDef);

		pDef = aiMastermind_GetDef();
		
		if (pDef && (pDef->pHeatDef || pDef->pExposeDef))
		{
			// once enabled, the system never disables
			s_masterMind.isEnabled = true;
			s_masterMind.timeStarted = ABS_TIME;

			if (pDef->pHeatDef)
			{
				aiMastermindHeat_Initialize();
			}
			else
			{
				aiMastermindExpose_Initialize();
			}
		}
		else
		{
			aiMastermind_SetCurrentDef(NULL);
		}
	}
	else
	{
		s_masterMind.isEnabled = false;
	}
}



// ------------------------------------------------------------------------------------------------------------------
void aiMastermind_OncePerFrame()
{
	AIMastermindDef *pDef = NULL;

	if (!s_masterMind.isEnabled)
		return;
	
	devassertmsg(0, "The Mastermind system is not partition ready! Please ask a RobP to convert it over if you wish to start using it sooner!");

	pDef = aiMastermind_GetDef();
	if (!pDef)
		return;

	if (!s_masterMind.bFirstTickInit)
	{
		if (pDef->pHeatDef)
		{
			aiMastermindHeat_FirstTickInit();
		}
		else 
		{
			aiMastermindExpose_FirstTickInit(pDef);
		}
		s_masterMind.bFirstTickInit = true;
		s_masterMind.timeStarted = ABS_TIME;
	}

	if (ABS_TIME_SINCE(s_masterMind.timeLastUpdate) < SEC_TO_ABS_TIME(1.f))
		return;
	s_masterMind.timeLastUpdate = ABS_TIME;


	// check what type of mastermind we need to update as
	if (pDef->pExposeDef)
	{
		aiMastermindExpose_Update(pDef);
	}
	else
	{
		aiMastermindHeat_Update(pDef);
	}
	
}

AUTO_COMMAND ACMD_LIST(gEntConCmdList) ACMD_GLOBAL;
void aiMastermindTestExpr(ACMD_SENTENCE testStr)
{
	MultiVal answer;
	Expression* expr = exprCreate();

	if (!s_masterMind.exprContext)
	{
		return;
	}

	if(exprGenerateFromString(expr, s_masterMind.exprContext, testStr, NULL))
	{
		exprEvaluate(expr, s_masterMind.exprContext, &answer);
	}

	exprDestroy(expr);
}


// -----------------------------------------------------------------------------------------------------------

// adds the eSendAtEnt as a seek target for the entity
void aiMastermind_PrimeEntityForSending(Entity *e, Entity *sendAtEnt, AIMastermindDef *pDef, bool bSaveOldFSM)
{
	FSM *pOverrideFSM = NULL;
	AIVarsBase *aib = e->aibase;
	AIMastermindAIVars *mmvars = NULL;
	AIConfig* pConfig = NULL;
	const char *pchFSMOVerride = pDef->pchSentEncounterFSMOverride ? pDef->pchSentEncounterFSMOverride : "Combat_Noambient";

	if (pchFSMOVerride)
		pOverrideFSM = fsmGetByName(pchFSMOVerride);
	if (!aib->mastermindVars)
	{
		aib->mastermindVars = aiMastermindAIVars_Alloc();
		
		if (!aib->mastermindVars)
			return;
	}
	mmvars = aib->mastermindVars;

	aiConfigMods_ApplyConfigMods(e, pDef->configMods, &mmvars->configMods);
	
	pConfig = aiGetConfig(e, aib);
	if (pConfig)
	{
		S32 configMod;
		if (pConfig->stareDownTime >= 0)
		{
			configMod = aiConfigModAddFromString(e, aib, "stareDownTime", "-1", NULL);
			if (configMod)		
				eaiPush(&(mmvars->configMods), configMod);
		}
		if (!pConfig->roamingLeash)
		{
			configMod = aiConfigModAddFromString(e, aib, "roamingLeash", "1", NULL);
			if (configMod)		
				eaiPush(&(mmvars->configMods), configMod);
		}
		if (!pConfig->roamingLeash)
		{
			configMod = aiConfigModAddFromString(e, aib, "roamingLeash", "1", NULL);
			if (configMod)		
				eaiPush(&(mmvars->configMods), configMod);
		}
	}
	
	if (pOverrideFSM)
	{
		if (bSaveOldFSM)
		{
			// todo: should maybe check if we are already running this FSM?
			// might need to save the old FSM context and make a new one
			// for now just swap the FSM and save the old FSM
			aiCopyCurrentFSMHandle(e, REF_HANDLEPTR(mmvars->hPrevFSM));
		}
		
		aiSetFSM(e, pOverrideFSM);
	}
		
	if (sendAtEnt)
		aiAddSeekTarget(e, sendAtEnt);
	
	aib->dontSleep = true;
}

// undos the things aiMastermind_PrimeEntityForSending did
void aiMastermind_UndoMastermindPriming(Entity *e, EntityRef erSendAtEnt, AIMastermindDef *pDef)
{
	AIVarsBase *aib = e->aibase;
	
	if (aib->mastermindVars)
	{
		if (IS_HANDLE_ACTIVE(aib->mastermindVars->hPrevFSM))
		{
			FSM* prevFSM = GET_REF(aib->mastermindVars->hPrevFSM);
			if (prevFSM)
				aiSetFSM(e, prevFSM);
			REMOVE_HANDLE(aib->mastermindVars->hPrevFSM);
		}

		aiMastermind_DestroyAIVars(e);
	}
	
	aib->dontSleep = false;

	aiRemoveSeekTarget(e, erSendAtEnt);
	// 
}

#include "aiMastermind_h_ast.c"