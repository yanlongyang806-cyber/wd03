#include "aiStructCommon.h"
#include "error.h"
#include "fileutil.h"
#include "FolderCache.h"
#include "GlobalTypes.h"
#include "referencesystem.h"
#include "ResourceManager.h"
#include "TextParserSimpleInheritance.h"

#ifdef GAMESERVER
#include "aiPowers.h"
#include "aiLib.h"
#endif

#include "aiStructCommon_h_ast.h"

AUTO_RUN_ANON(memBudgetAddMapping(__FILE__, BUDGET_GameSystems););
AUTO_RUN_ANON(memBudgetAddMapping("AIPowerConfigDef", BUDGET_GameSystems););

const char *g_pcAICombatRolesDictName = "AICombatRolesDef";
const char *g_pcAIMasterMindDefDictName = "AIMastermindDef";
const char *g_pcAICivDefDictName = "AICivilianMapDef";

DictionaryHandle g_AIPowerConfigDefDict = NULL;

AUTO_RUN;
void aiRequestDataFromServer(void)
{
	if(isDevelopmentMode() && IsClient())
	{
		resRegisterIndexOnlyDictionary("AIConfig", RESCATEGORY_INDEX);

		resRegisterIndexOnlyDictionary("AIPowerConfigDef", RESCATEGORY_INDEX);

		resRegisterIndexOnlyDictionary(g_pcAICombatRolesDictName, RESCATEGORY_INDEX);

		resRegisterIndexOnlyDictionary(g_pcAIMasterMindDefDictName, RESCATEGORY_INDEX);

		resRegisterIndexOnlyDictionary(g_pcAICivDefDictName, RESCATEGORY_INDEX);
	}
}

static void aiPowerConfigDefApplyInheritance(AIPowerConfigDef *pcd)
{
	int i;
	for(i=0; i<eaSize(&pcd->inheritData); i++)
	{
		AIPowerConfigDef *parent = RefSystem_ReferentFromString(g_AIPowerConfigDefDict, pcd->inheritData[i]);

		if(!parent)
		{
			ErrorFilenamef(pcd->filename, "Unable to find parent power config to inherit from: %s", pcd->inheritData[i]);
			continue;
		}

		SimpleInheritanceApply(parse_AIPowerConfigDef, pcd, parent, NULL, NULL);
	}
}

AUTO_FIXUPFUNC;
TextParserResult fixupAIPowerConfigDef(AIPowerConfigDef* pcd, enumTextParserFixupType eType, void *pExtraData)
{
	switch (eType)
	{
		xcase FIXUPTYPE_POST_ALL_TEXT_READING_AND_INHERITANCE_DURING_LOADFILES:
		{
			aiPowerConfigDefApplyInheritance(pcd);
		}

		xcase FIXUPTYPE_POST_RELOAD: {
			// Do some magic simple inheritance propagation here
			;
		}
	}

	return 1;
}

static void aiPowerConfigDefReload(const char *path, int UNUSED_when)
{
	loadstart_printf("Reloading PowerConfigDefs...");
	fileWaitForExclusiveAccess(path);
	errorLogFileIsBeingReloaded(path);
	if(!ParserReloadFileToDictionary(path, g_AIPowerConfigDefDict))
		Errorf("Error reloading power config def");

	loadend_printf(" done");
}

int aiPowerConfigDefGenerateExprs(AIPowerConfigDef *pcd)
{
	int success = 1;

#ifdef GAMESERVER
	success &= aiPowersGenerateConfigExpression(pcd->aiRequires);
	success &= aiPowersGenerateConfigExpression(pcd->aiEndCondition);
	success &= aiPowersGenerateConfigExpression(pcd->weightModifier);
	success &= aiPowersGenerateConfigExpression(pcd->targetOverride);
	success &= aiPowersGenerateConfigExpression(pcd->chainRequires);
	success &= aiPowersGenerateConfigExpression(pcd->cureRequires);
#endif

	return success;
}

static int aiPowerConfigDefProcess(AIPowerConfigDef *pcd)
{
	int success = 1;

	success &= aiPowerConfigDefGenerateExprs(pcd);	

	return success;
}

static int aiPowerConfigDefValidate(enumResourceValidateType eType, const char* pDictName, const char* pResourceName, void* pResource, U32 userID)
{
	AIPowerConfigDef* pcd = pResource;

	switch (eType)
	{
	case RESVALIDATE_POST_TEXT_READING:
		aiPowerConfigDefProcess(pcd);
		return VALIDATE_HANDLED;
	}

	return VALIDATE_NOT_HANDLED;
}

AUTO_STARTUP(AIEarly);
void aiEarlyDummy(void)
{
	
}

AUTO_STARTUP(AIBeforePowers);
void aiBeforePowersDummy(void)
{

}

AUTO_STARTUP(AIPowerConfigDefs) ASTRT_DEPS(AIEarly);
void aiPowerConfigDefLoad(void)
{
	static int initted = false;
	if(!initted)
	{
		initted = true;
		if(IsGameServerBasedType())
		{
			g_AIPowerConfigDefDict = RefSystem_RegisterSelfDefiningDictionary("AIPowerConfigDef", false, parse_AIPowerConfigDef, true, false, NULL);
#ifdef GAMESERVER
			resDictManageValidation(g_AIPowerConfigDefDict, aiPowerConfigDefValidate);
#endif

			if (isDevelopmentMode() || isProductionEditMode()) {
				resDictMaintainInfoIndex(g_AIPowerConfigDefDict, ".name", NULL, NULL, NULL, NULL);
			}
			resLoadResourcesFromDisk(g_AIPowerConfigDefDict, "ai/PowerConfigDef/", ".pcd", "AIPowerConfigDefs.bin", PARSER_OPTIONALFLAG | PARSER_SERVERSIDE);

			FolderCacheSetCallback(FOLDER_CACHE_CALLBACK_UPDATE, "ai/PowerConfigDef/*.pcd", aiPowerConfigDefReload);

			resDictProvideMissingResources(g_AIPowerConfigDefDict);
		}
	}
}

void aiRequestEditingData(void)
{
	static int requested = 0;

	if(!requested)
	{
		requested = 1;

		resDictRequestMissingResources("AIConfig", 16, false, resClientRequestSendReferentCommand);
		resSubscribeToInfoIndex("AIConfig", true);

		resDictRequestMissingResources("AIPowerConfigDef", 16, false, resClientRequestSendReferentCommand);
		resSubscribeToInfoIndex("AIPowerConfigDef", true);		

		resDictRequestMissingResources(g_pcAICombatRolesDictName, 16, false, resClientRequestSendReferentCommand);
		resSubscribeToInfoIndex(g_pcAICombatRolesDictName, true);		

		resDictRequestMissingResources(g_pcAIMasterMindDefDictName, 16, false, resClientRequestSendReferentCommand);
		resSubscribeToInfoIndex(g_pcAIMasterMindDefDictName, true);		

		resDictRequestMissingResources(g_pcAICivDefDictName, 16, false, resClientRequestSendReferentCommand);
		resSubscribeToInfoIndex(g_pcAICivDefDictName, true);		
	}
}

static int StaticCheckPatrolRoute(ExprContext *context, MultiVal *pMV, char **estrError)
{
	return true;
}

AUTO_STARTUP(ExpressionSCRegister);
void aiRegisterExprArgTypes(void)
{
	exprRegisterStaticCheckArgumentType("PatrolRoute", "FSM.ArgType.PatrolRoute", StaticCheckPatrolRoute);
	exprRegisterStaticCheckArgumentType("Bool", "FSM.ArgType.Bool", StaticCheckPatrolRoute);
	exprRegisterStaticCheckArgumentType("AIAnimList", "Ref.AIAnimList", NULL);
}

#include "aiStructCommon_h_ast.c"