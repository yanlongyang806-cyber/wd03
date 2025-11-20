/***************************************************************************
*     Copyright (c) 2007, Cryptic Studios
*     All Rights Reserved
*     Confidential Property of Cryptic Studios
***************************************************************************/

#include "Error.h"
#include "Expression.h"
#include "file.h"
#include "powerStoreCommon.h"
#include "PowerTree.h"
#include "ResourceManager.h"

#include "AutoGen/powerStoreCommon_h_ast.h"

AUTO_RUN_ANON(memBudgetAddMapping(__FILE__, BUDGET_GameSystems););

DictionaryHandle g_PowerStoreDictionary = NULL;
ExprContext *g_pPowerStoreContext = NULL;

#define POWER_STORES_BASE_DIR "defs/powerstores"
#define POWER_STORES_EXTENSION "powerstore"


AUTO_RUN;
int powerstore_CreateExprContext(void)
{
	ExprFuncTable* stFuncs;

	g_pPowerStoreContext = exprContextCreate();
	stFuncs = exprContextCreateFunctionTable();
	exprContextAddFuncsToTableByTag(stFuncs, "util");
	exprContextAddFuncsToTableByTag(stFuncs, "player");
	exprContextSetFuncTable(g_pPowerStoreContext, stFuncs);
	exprContextSetSelfPtr(g_pPowerStoreContext, NULL);

	return 1;
}

// ----------------------------------------------------------------------------------
// Power Store Validation
// ----------------------------------------------------------------------------------

bool powerstore_Validate(PowerStoreDef *pDef)
{
	const char *pcTempFileName;
	int i;

	if( !resIsValidName(pDef->pcName) )
	{
		ErrorFilenamef( pDef->pcFilename, "Power store name is illegal: '%s'", pDef->pcName );
		return 0;
	}

	if( !resIsValidScope(pDef->pcScope) )
	{
		ErrorFilenamef( pDef->pcFilename, "Power store scope is illegal: '%s'", pDef->pcScope );
		return 0;
	}

	pcTempFileName = pDef->pcFilename;
	if (resFixPooledFilename(&pcTempFileName, POWER_STORES_BASE_DIR, pDef->pcScope, pDef->pcName, POWER_STORES_EXTENSION)) {
		if (IsServer()) {
			ErrorFilenamef( pDef->pcFilename, "Power store filename does not match name '%s' scope '%s'", pDef->pcName, pDef->pcScope);
		}
	}

	if (IsServer() && !GET_REF(pDef->hCurrency) && REF_STRING_FROM_HANDLE(pDef->hCurrency)) {
		ErrorFilenamef(pDef->pcFilename, "Power store references non-existent item '%s'", REF_STRING_FROM_HANDLE(pDef->hCurrency));
	}

	for(i=eaSize(&pDef->eaInventory)-1; i>=0; --i)
	{
		PowerStorePowerDef *pPower = pDef->eaInventory[i];

		if (IsServer() && !GET_REF(pPower->hTree)) {
			if (REF_STRING_FROM_HANDLE(pPower->hTree)) {
				ErrorFilenamef(pDef->pcFilename, "Power store references non-existent power tree '%s'", REF_STRING_FROM_HANDLE(pPower->hTree));
			} else {
				ErrorFilenamef(pDef->pcFilename, "Power store has an inventory row that has no power tree defined");
			}
		}
		//if (IsServer() && !GET_REF(pPower->hNode)) {
		//	if (REF_STRING_FROM_HANDLE(pPower->hNode)) {
		//		ErrorFilenamef(pDef->pcFilename, "Power store references non-existent power tree node '%s'", REF_STRING_FROM_HANDLE(pPower->hNode));
		//	} else {
		//		ErrorFilenamef(pDef->pcFilename, "Power store has an inventory row that has no power tree node defined");
		//	}
		//} else if (IsServer() && GET_REF(pPower->hTree) && (GET_REF(pPower->hTree) != powertree_TreeDefFromNodeDef(GET_REF(pPower->hNode)))) {
		//	ErrorFilenamef(pDef->pcFilename, "Power store has an inventory row with a power tree node '%s' that does not belong to power tree '%s", REF_STRING_FROM_HANDLE(pPower->hNode), REF_STRING_FROM_HANDLE(pPower->hTree));
		//}

		if (pPower->pExprCanBuy) {
			if (!exprGenerate(pPower->pExprCanBuy, g_pPowerStoreContext)) {
				ErrorFilenamef(pDef->pcFilename, "Failed to generate Can Buy expression on power store power '%s'", REF_STRING_FROM_HANDLE(pPower->hNode));
			}
		}

		if (IsServer() && !GET_REF(pPower->cantBuyMessage.hMessage) && REF_STRING_FROM_HANDLE(pPower->cantBuyMessage.hMessage)) {
			ErrorFilenamef(pDef->pcFilename, "Power store references non-existent message '%s'", REF_STRING_FROM_HANDLE(pPower->cantBuyMessage.hMessage));
		}
	}

	return 1;
}

// ----------------------------------------------------------------------------------
// Power Store Dictionary
// ----------------------------------------------------------------------------------

// This resource validation callback just does filename fixup
// TODO: add validation to stores
static int powerStoreResValidateCB(enumResourceValidateType eType, const char *pDictName, const char *pResourceName, PowerStoreDef *pPowerStore, U32 userID)
{
	switch(eType)
	{
		xcase RESVALIDATE_POST_TEXT_READING:
			powerstore_Validate(pPowerStore);
			return VALIDATE_HANDLED;
		xcase RESVALIDATE_FIX_FILENAME:
			resFixPooledFilename(&pPowerStore->pcFilename, POWER_STORES_BASE_DIR, pPowerStore->pcScope, pPowerStore->pcName, POWER_STORES_EXTENSION);
			return VALIDATE_HANDLED;
	}
	return VALIDATE_NOT_HANDLED;
}

AUTO_RUN;
int RegisterPowerStoreDictionary(void)
{
	g_PowerStoreDictionary = RefSystem_RegisterSelfDefiningDictionary("PowerStore", false, parse_PowerStoreDef, true, true, NULL);

	if (IsGameServerSpecificallly_NotRelatedTypes())
	{
		resDictManageValidation(g_PowerStoreDictionary, powerStoreResValidateCB);
	}
	if (IsServer()) {
		resDictProvideMissingResources(g_PowerStoreDictionary);
		if (isDevelopmentMode() || isProductionEditMode()) {
			resDictMaintainInfoIndex(g_PowerStoreDictionary, ".Name", NULL, NULL, NULL, NULL);
		}
	} else if (IsClient()) {
		resDictRequestMissingResources(g_PowerStoreDictionary, 8, false, resClientRequestSendReferentCommand);
	}
	resDictProvideMissingRequiresEditMode(g_PowerStoreDictionary);
	return 1;
}

AUTO_STARTUP(PowerStores) ASTRT_DEPS(Powers, PowerTrees, Items);
void powerstore_LoadDefs(void)
{
	resLoadResourcesFromDisk(g_PowerStoreDictionary, "defs/powerstores", ".powerstore", NULL, PARSER_OPTIONALFLAG | RESOURCELOAD_SHAREDMEMORY );
}

PowerStoreDef *powerstore_DefFromName(char *pcStoreName)
{
	if (pcStoreName) {
		return (PowerStoreDef*)RefSystem_ReferentFromString(g_PowerStoreDictionary, pcStoreName);
	}
	return NULL;
}

#include "AutoGen/powerStoreCommon_h_ast.c"
