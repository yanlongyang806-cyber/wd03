#include "GameAccountDataCommon.h"
#include "GameAccountDataCommon_h_ast.h"

#include "accountnet.h"
#include "accountnet_h_ast.h"
#include "Alerts.h"
#include "AutoTransDefs.h"
#include "Entity.h"
#include "Entity_h_ast.h"
#include "fastAtoi.h"
#include "file.h"
#include "fileutil.h"
#include "FolderCache.h"
#include "GameAccountData\GameAccountData.h"
#include "GameAccountData_h_ast.h"
#include "GamePermissionsCommon.h"
#include "itemCommon.h"
#include "inventoryCommon.h"
#include "LoginCommon.h"
#include "Player.h"
#include "Player_h_ast.h"
#include "objSchema.h"
#include "ResourceManager.h"
#include "chatCommon.h"
#include "AutoGen/chatCommon_h_ast.h"
#include "Expression.h"
#include "StashTable.h"
#include "Login2Common.h"
#include "ShardCommon.h"

#ifndef GAMECLIENT
#include "objTransactions.h"
#else //#ifdef GAMECLIENT
#include "ContinuousBuilderSupport.h"
#include "gclLogin.h"
#include "gclEntity.h"
#include "gclAccountProxy.h"
#include "LoginCommon.h"
#include "UIGen.h"
#include "AutoGen/GameServerLib_autogen_ServerCmdWrappers.h"
#endif

#ifdef GAMESERVER
#include "gslGameAccountData.h"
#include "WebRequestServer/wrContainerSubs.h"
#include "gateway/gslGatewayServer.h"
#endif

#ifdef APPSERVER
#include "aslLoginServer.h"
#include "aslLogin2_StateMachine.h"
#endif

AUTO_RUN_ANON(memBudgetAddMapping(__FILE__, BUDGET_GameSystems););


// ----------------------------------------------------------------------------------
// Static Data Initialization
// ----------------------------------------------------------------------------------

extern const char *g_PlayerVarName;

RequiredPowersAtCreation g_RequiredPowersAtCreation;

U32 g_uGameAccountModifyCount;

STRING_EARRAY g_eaDisallowedWarpMaps = NULL;

DefineContext* g_pGameAccountDataNumericPurchaseCategories = NULL;
GameAccountDataNumericPurchaseDefs g_GameAccountDataNumericPurchaseDefs = {0};

// ----------------------------------------------------------------------------------
// Game Account Data Startup Logic
// ----------------------------------------------------------------------------------

void GameAccountDictChanged(enumResourceEventType eType, const char *pDictName, const char *pRefData, Referent pReferent, void *pUserData)
{
	GameAccountData *pData = (GameAccountData*) pReferent;

#ifdef GAMESERVER
	if (eType == RESEVENT_RESOURCE_ADDED)
	{
		if(GetAppGlobalType() == GLOBALTYPE_WEBREQUESTSERVER)
		{
			wrCSub_GADSubscribed(pData);
		}
		else if(GetAppGlobalType() == GLOBALTYPE_GATEWAYSERVER)
		{
			gslGatewayServer_GameAccountDataSubscribed(pData);
		}
	}
#endif

#ifdef GAMECLIENT
	if (entity_GetGameAccount(entActivePlayerPtr()) == pData)
	{
		if (eType == RESEVENT_RESOURCE_ADDED || eType == RESEVENT_RESOURCE_MODIFIED)
		{
			AccountProxyKeyValueInfoListContainer *pList = StructCreate(parse_AccountProxyKeyValueInfoListContainer);
			eaCopyStructs(&pData->eaAccountKeyValues, &pList->ppList, parse_AccountProxyKeyValueInfoContainer);
			gclAPCacheSetAllKeyContainerValues(pList);
			StructDestroy(parse_AccountProxyKeyValueInfoListContainer, pList);
		}
	}
#endif

	// Modify this each time the dictionary changes
	++g_uGameAccountModifyCount;
}

AUTO_RUN_LATE;
int RegisterGameContainer(void)
{
	//Register the game account data
	objRegisterNativeSchema(GLOBALTYPE_GAMEACCOUNTDATA, parse_GameAccountData, NULL, NULL, NULL, NULL, NULL);

	RefSystem_RegisterSelfDefiningDictionary(GlobalTypeToCopyDictionaryName(GLOBALTYPE_GAMEACCOUNTDATA), false, parse_GameAccountData, false, false, NULL);
#ifdef GAMECLIENT
	resDictRequestMissingResources(GlobalTypeToCopyDictionaryName(GLOBALTYPE_GAMEACCOUNTDATA), RES_DICT_KEEP_ALL, true, resClientRequestSendReferentCommand);
#endif
#if defined(GAMESERVER)
	resDictRequestMissingResources(GlobalTypeToCopyDictionaryName(GLOBALTYPE_GAMEACCOUNTDATA), RES_DICT_KEEP_NONE, false, objCopyDictHandleRequest);
#endif
#if defined(APPSERVER)
	resDictRequestMissingResources(GlobalTypeToCopyDictionaryName(GLOBALTYPE_GAMEACCOUNTDATA), RES_DICT_KEEP_NONE, true, objCopyDictHandleRequest);
#endif

	resDictRegisterEventCallback(GlobalTypeToCopyDictionaryName(GLOBALTYPE_GAMEACCOUNTDATA),GameAccountDictChanged, NULL);
	resDictProvideMissingResources(GlobalTypeToCopyDictionaryName(GLOBALTYPE_GAMEACCOUNTDATA));
	g_uGameAccountModifyCount = 1; // Initialize counter
	return 1;
}


// ----------------------------------------------------------------------------------
// Powers and Warp Startup Logic
// ----------------------------------------------------------------------------------

AUTO_STARTUP(RequiredPowersAtCreation);
void LoadRequiredPowersAtCreation(void)
{
	loadstart_printf("Loading Required Powers at Creation... ");
	ParserLoadFiles(NULL, "defs/config/RequiredPowersAtCreation.def", "RequiredPowersAtCreation.bin", PARSER_OPTIONALFLAG, parse_RequiredPowersAtCreation, &g_RequiredPowersAtCreation);
	loadend_printf(" done.");
}

AUTO_STARTUP(WarpRestrictions);
void LoadWarpRestrictions(void)
{
	DisallowedWarpMaps WarpMaps = {0};
	int i;

	loadstart_printf("Loading Warp Restrictions... ");
	
	ParserLoadFiles(NULL, "defs/config/WarpRestrictions.def", "WarpRestrictions.bin", PARSER_OPTIONALFLAG, parse_DisallowedWarpMaps, &WarpMaps);

	for(i = 0; i < eaSize(&WarpMaps.eaDisallowedWarpMaps); i++)
	{
		eaPush(&g_eaDisallowedWarpMaps, StructAllocString(WarpMaps.eaDisallowedWarpMaps[i]->pDisallowedWarpMap));
	}

	loadend_printf(" done (%d  Warp Restrictions).", i);
	
	StructDeInit(parse_DisallowedWarpMaps, &WarpMaps);
}

static void GameAccountDataNumericPurchase_ValidateRequiredKeyValue(GameAccountDataNumericPurchaseDef* pDef, GameAccountDataRequiredKeyValue* pRequiredKeyValue)
{
	if (pRequiredKeyValue->iMaxIntValue >= 0 && 
		pRequiredKeyValue->iMinIntValue > pRequiredKeyValue->iMaxIntValue)
	{
		Errorf("GameAccountDataNumericPurchaseDef %s has a required key value %s that has a min value that is greater than its max", 
			pDef->pchName, pRequiredKeyValue->pchKey);
	}
}

static void GameAccountDataNumericPurchase_Validate(GameAccountDataNumericPurchaseDef* pDef)
{
	int i, j;

	if (IS_HANDLE_ACTIVE(pDef->msgDisplayName.hMessage) && !GET_REF(pDef->msgDisplayName.hMessage))
	{
		Errorf("GameAccountDataNumericPurchaseDef %s references non-existent DisplayMessage %s", 
			pDef->pchName, REF_STRING_FROM_HANDLE(pDef->msgDisplayName.hMessage));
	}

	if (IS_HANDLE_ACTIVE(pDef->msgDescription.hMessage) && !GET_REF(pDef->msgDescription.hMessage))
	{
		Errorf("GameAccountDataNumericPurchaseDef %s references non-existent DecriptionMessage %s", 
			pDef->pchName, REF_STRING_FROM_HANDLE(pDef->msgDescription.hMessage));
	}

	if (!pDef->pchNumericItemDef)
	{
		Errorf("GameAccountDataNumericPurchaseDef %s must reference a numeric", pDef->pchName);
	}
	else
	{
		ItemDef* pItemDef = item_DefFromName(pDef->pchNumericItemDef);
		if (!pItemDef)
		{
			Errorf("GameAccountDataNumericPurchaseDef %s references a non-existent ItemDef %s", 
				pDef->pchName, pDef->pchNumericItemDef);
		}
		else if (pItemDef->eType != kItemType_Numeric)
		{
			Errorf("GameAccountDataNumericPurchaseDef %s references a non-numeric ItemDef %s", 
				pDef->pchName, pDef->pchNumericItemDef);
		}
	}

	// Validate required data
	if (pDef->pRequire)
	{
		for (i = eaSize(&pDef->pRequire->eaKeyValues)-1; i >= 0; i--)
		{
			GameAccountDataNumericPurchase_ValidateRequiredKeyValue(pDef, pDef->pRequire->eaKeyValues[i]);
		}
	}
	// Validate required or pairs
	for (i = eaSize(&pDef->eaOrRequire)-1; i >= 0; i--)
	{
		for (j = eaSize(&pDef->eaOrRequire[i]->eaKeyValues)-1; j >= 0; j--)
		{
			GameAccountDataNumericPurchase_ValidateRequiredKeyValue(pDef, pDef->eaOrRequire[i]->eaKeyValues[j]);
		}
	}

	// Validate purchase key values
	for (i = eaSize(&pDef->eaPurchaseKeyValues)-1; i >= 0; i--)
	{
		GameAccountDataPurchaseKeyValue* pPurchaseKeyValue = pDef->eaPurchaseKeyValues[i];
		if (!pPurchaseKeyValue->iIntValue &&
			(!pPurchaseKeyValue->pchStringValue || !pPurchaseKeyValue->pchStringValue[0]))
		{
			Errorf("GameAccountDataNumericPurchaseDef %s has an invalid purchase key value %s", 
				pDef->pchName, pPurchaseKeyValue->pchKey);
		}
	}
}

static void GameAccountDataNumericPurchaseCategories_LoadInternal(const char *pchPath, S32 iWhen)
{
	if (g_pGameAccountDataNumericPurchaseCategories)
	{
		DefineDestroy(g_pGameAccountDataNumericPurchaseCategories);
	}
	g_pGameAccountDataNumericPurchaseCategories = DefineCreate();

	loadstart_printf("Loading GameAccountDataNumericPurchaseCategories... ");

	DefineLoadFromFile(g_pGameAccountDataNumericPurchaseCategories, 
					   "Category", 
					   "Category", 
					   NULL, 
					   "defs/config/GameAccountNumericPurchaseCategories.def",
					   "GameAccountNumericPurchaseCategories.bin",
					   kGameAccountDataNumericPurchaseCategory_FirstDataDefined);

#ifdef GAMECLIENT
	ui_GenInitStaticDefineVars(GameAccountDataNumericPurchaseCategoryEnum, "GameAccountNumericPurchaseCategory_");
#endif
	loadend_printf(" done.");
}

AUTO_STARTUP(GameAccountDataNumericPurchaseCategories);
void GameAccountDataNumericPurchaseCategories_Load(void)
{
	GameAccountDataNumericPurchaseCategories_LoadInternal(NULL, 0);
	FolderCacheSetCallback(FOLDER_CACHE_CALLBACK_UPDATE, 
						   "defs/config/GameAccountNumericPurchaseCategories.def", 
						   GameAccountDataNumericPurchaseCategories_LoadInternal);
}

static void GameAccountDataNumericPurchase_LoadInternal(const char* pchPath, S32 iWhen)
{
	StructReset(parse_GameAccountDataNumericPurchaseDefs, &g_GameAccountDataNumericPurchaseDefs);

	if (pchPath)
	{
		fileWaitForExclusiveAccess(pchPath);
		errorLogFileIsBeingReloaded(pchPath);
	}

	loadstart_printf("Loading GameAccountDataNumericPurchaseDefs... ");

	ParserLoadFiles(NULL, 
					"defs/config/GameAccountNumericPurchase.def", 
					"GameAccountNumericPurchase.bin", 
					PARSER_OPTIONALFLAG, 
					parse_GameAccountDataNumericPurchaseDefs, 
					&g_GameAccountDataNumericPurchaseDefs);

	// Do validation in development mode on the server
	if (isDevelopmentMode() && IsGameServerBasedType())
	{
		int i;
		for (i = eaSize(&g_GameAccountDataNumericPurchaseDefs.eaDefs)-1; i >= 0; i--)
		{
			GameAccountDataNumericPurchase_Validate(g_GameAccountDataNumericPurchaseDefs.eaDefs[i]);
		}
	}
	loadend_printf(" done.");
}

AUTO_STARTUP(GameAccountNumericPurchase) ASTRT_DEPS(Items, GameAccountDataNumericPurchaseCategories);
void GameAccountDataNumericPurchase_Load(void)
{
	GameAccountDataNumericPurchase_LoadInternal(NULL, 0);
	FolderCacheSetCallback(FOLDER_CACHE_CALLBACK_UPDATE, 
						   "defs/config/GameAccountNumericPurchase.def", 
						   GameAccountDataNumericPurchase_LoadInternal);
}


// ----------------------------------------------------------------------------------
// Game Account Access Functions
// ----------------------------------------------------------------------------------

GameAccountDataNumericPurchaseDef* GAD_NumericPurchaseDefFromName(const char* pchDefName)
{
	return eaIndexedGetUsingString(&g_GameAccountDataNumericPurchaseDefs.eaDefs, pchDefName);
}

int GAD_NumericPurchaseCountByCategory(GameAccountDataNumericPurchaseCategory eCategory)
{
	int i, iCount = 0;
	for (i = eaSize(&g_GameAccountDataNumericPurchaseDefs.eaDefs)-1; i >= 0; i--)
	{
		if (g_GameAccountDataNumericPurchaseDefs.eaDefs[i]->eCategory == eCategory)
		{
			iCount++;
		}
	}
	return iCount;
}

bool GAD_NumericPurchaseGetRequiredNumerics(GameAccountDataNumericPurchaseCategory eCategory, const char*** pppchNumerics)
{
	int i, iCount = 0;
	for (i = eaSize(&g_GameAccountDataNumericPurchaseDefs.eaDefs)-1; i >= 0; i--)
	{
		GameAccountDataNumericPurchaseDef* pDef = g_GameAccountDataNumericPurchaseDefs.eaDefs[i];
		if (pDef->eCategory == eCategory)
		{
			eaPushUnique(pppchNumerics, pDef->pchNumericItemDef);
		}
	}
	return eaSize(pppchNumerics) > 0;
}

// Returns the minimum numeric cost of all valid defs
// Returns 0 on failure
int GAD_NumericPurchaseGetMinimumCostForAccount(GameAccountData* pData, const char* pchNumeric, GameAccountDataNumericPurchaseCategory eCategory, const char** ppchNumericOut)
{
	int i, iMinimumCost = 0;
	if (pData)
	{
		for (i = 0; i < eaSize(&g_GameAccountDataNumericPurchaseDefs.eaDefs); i++)
		{
			GameAccountDataNumericPurchaseDef* pDef = g_GameAccountDataNumericPurchaseDefs.eaDefs[i];
		
			if (eCategory > kGameAccountDataNumericPurchaseCategory_None && pDef->eCategory != eCategory)
			{
				continue;
			}
			if (!GAD_CanMakeNumericPurchaseCheckKeyValues(pData, pDef))
			{
				continue;
			}
			if (!pchNumeric || !pchNumeric[0])
			{
				pchNumeric = pDef->pchNumericItemDef;
			}
			if (stricmp(pDef->pchNumericItemDef, pchNumeric) == 0)
			{
				if (!iMinimumCost || pDef->iNumericCost < iMinimumCost)
				{
					iMinimumCost = pDef->iNumericCost;
				}
			}
		}
	}
	if (ppchNumericOut)
	{
		(*ppchNumericOut) = pchNumeric;
	}
	return iMinimumCost;
}

AUTO_TRANS_HELPER;
GameAccountData *entity_trh_GetGameAccount(ATH_ARG NOCONST(Entity) *pEnt)
{
	// *** THIS FUNCTION IS EVIL AND WRONG ***
	// *** EVERYTHING CALLING IT MUST BE REWRITTEN TO NOT DO SO ***
	GameAccountData *pData = NULL;
	if(NONNULL(pEnt) && NONNULL(pEnt->pPlayer))
	{
		pData = GET_REF(pEnt->pPlayer->pPlayerAccountData->hData);
	}

#ifdef GAMECLIENT
    if(ISNULL(pData) && g_characterSelectionData)
	{
		pData = GET_REF(g_characterSelectionData->hGameAccountData);
	}
#endif
#ifdef APPSERVER
	if(ISNULL(pData) && NONNULL(pEnt) && NONNULL(pEnt->pPlayer))
	{
		Login2State *loginState = aslLogin2_GetActiveLoginStateByAccountID(pEnt->pPlayer->accountID);
		if(loginState)
		{
			pData = GET_REF(loginState->hGameAccountData);
		}
	}
#endif
	
	return(pData);
}

GameAccountData *entity_GetGameAccount(Entity *pEnt)
{
	GameAccountData *pData = NULL;
	if (pEnt && pEnt->pPlayer && pEnt->pPlayer->pPlayerAccountData)
	{
		pData = GET_REF(pEnt->pPlayer->pPlayerAccountData->hData);
	}

#ifdef GAMECLIENT
	if (!pData && g_characterSelectionData)
	{
        pData = GET_REF(g_characterSelectionData->hGameAccountData);
	}
#endif
#ifdef APPSERVER
	if (!pData && pEnt && pEnt->pPlayer)
	{
        Login2State *loginState = aslLogin2_GetActiveLoginStateByAccountID(pEnt->pPlayer->accountID);
        if (loginState)
        {
            pData = GET_REF(loginState->hGameAccountData);
        }
	}
#endif
#ifdef GAMESERVER
	if (!pData && (GetAppGlobalType() == GLOBALTYPE_WEBREQUESTSERVER || GetAppGlobalType() == GLOBALTYPE_GATEWAYSERVER) && pEnt && pEnt->pPlayer && pEnt->pPlayer->pPlayerAccountData)
	{
		pData = GET_REF(pEnt->pPlayer->pPlayerAccountData->hTempData);
	}
#endif
	
	return pData;
}


// ----------------------------------------------------------------------------------
// Game Account Extract Functions
// ----------------------------------------------------------------------------------

void GAD_UpdateExtract(const GameAccountData *pData, GameAccountDataExtract *pExtract)
{
	if (pData && pExtract)
	{
		PERFINFO_AUTO_START_FUNC();

		eaCopyStructs(&pData->eaKeys, &pExtract->eaKeys, parse_AttribValuePair);
		eaCopyStructs(&pData->eaCostumeKeys, &pExtract->eaCostumeKeys, parse_AttribValuePair);
		eaCopyStructs(&pData->eaTokens, &pExtract->eaTokens, parse_GameToken);
		eaCopyStructs(&pData->eaAccountKeyValues, &pExtract->eaAccountKeyValues, parse_AccountProxyKeyValueInfoContainer);

		pExtract->iDaysSubscribed = pData->iDaysSubscribed;

		PERFINFO_AUTO_STOP();
	}
}


GameAccountDataExtract *GAD_CreateExtract(const GameAccountData *pData)
{
	GameAccountDataExtract *pExtract = NULL;
	if (pData)
	{
		PERFINFO_AUTO_START_FUNC();

		pExtract = StructCreate(parse_GameAccountDataExtract);

		eaCopyStructs(&pData->eaKeys, &pExtract->eaKeys, parse_AttribValuePair);
		eaCopyStructs(&pData->eaCostumeKeys, &pExtract->eaCostumeKeys, parse_AttribValuePair);
		eaCopyStructs(&pData->eaTokens, &pExtract->eaTokens, parse_GameToken);
		eaCopyStructs(&pData->eaAccountKeyValues, &pExtract->eaAccountKeyValues, parse_AccountProxyKeyValueInfoContainer);

		pExtract->iDaysSubscribed = pData->iDaysSubscribed;

		PERFINFO_AUTO_STOP();
	}
	return pExtract;
}


AUTO_TRANS_HELPER;
GameAccountDataExtract *entity_trh_CreateGameAccountDataExtract(ATH_ARG NOCONST(GameAccountData) *pData)
{
	GameAccountDataExtract *pExtract = NULL;
	PERFINFO_AUTO_START_FUNC();
	if (NONNULL(pData))
	{
		pExtract = StructCreate(parse_GameAccountDataExtract);

		eaCopyStructsReConst(&pData->eaKeys, &pExtract->eaKeys, parse_AttribValuePair);
		eaCopyStructsReConst(&pData->eaCostumeKeys, &pExtract->eaCostumeKeys, parse_AttribValuePair);
		eaCopyStructsReConst(&pData->eaTokens, &pExtract->eaTokens, parse_GameToken);
		eaCopyStructsReConst(&pData->eaAccountKeyValues, &pExtract->eaAccountKeyValues, parse_AccountProxyKeyValueInfoContainer);

		pExtract->iDaysSubscribed = pData->iDaysSubscribed;
		pExtract->bLinkedAccount = pData->bLinkedAccount;
		pExtract->bShadowAccount = pData->bShadowAccount;
	}
	PERFINFO_AUTO_STOP();
	return pExtract;
}


AUTO_TRANS_HELPER;
GameAccountDataExtract *entity_trh_CreateShallowGameAccountDataExtract(ATH_ARG NOCONST(GameAccountData) *pData)
{
	GameAccountDataExtract *pExtract = NULL;
	PERFINFO_AUTO_START_FUNC();
	if (NONNULL(pData))
	{
		pExtract = StructCreate(parse_GameAccountDataExtract);

		pExtract->eaKeys = CONTAINER_RECONST2(AttribValuePair, pData->eaKeys);
		pExtract->eaCostumeKeys = CONTAINER_RECONST2(AttribValuePair, pData->eaCostumeKeys);
		pExtract->eaTokens = CONTAINER_RECONST2(GameToken, pData->eaTokens);
		pExtract->eaAccountKeyValues = CONTAINER_RECONST2(AccountProxyKeyValueInfoContainer, pData->eaAccountKeyValues);

		pExtract->iDaysSubscribed = pData->iDaysSubscribed;
		pExtract->bLinkedAccount = pData->bLinkedAccount;
		pExtract->bShadowAccount = pData->bShadowAccount;
	}
	PERFINFO_AUTO_STOP();
	return pExtract;
}


AUTO_TRANS_HELPER_SIMPLE;
void entity_trh_DestroyGameAccountDataExtract(GameAccountDataExtract **ppExtract)
{
	PERFINFO_AUTO_START_FUNC();
	StructDestroySafe(parse_GameAccountDataExtract, ppExtract);
	PERFINFO_AUTO_STOP();
}


AUTO_TRANS_HELPER_SIMPLE;
void entity_trh_DestroyShallowGameAccountDataExtract(GameAccountDataExtract **ppExtract)
{
	PERFINFO_AUTO_START_FUNC();
	(*ppExtract)->eaKeys = NULL;
	(*ppExtract)->eaCostumeKeys = NULL;
	(*ppExtract)->eaTokens = NULL;
	(*ppExtract)->eaAccountKeyValues = NULL;
	StructDestroySafe(parse_GameAccountDataExtract, ppExtract);
	PERFINFO_AUTO_STOP();
}


GameAccountDataExtract *entity_CreateLocalGameAccountDataExtract(const GameAccountData *pData)
{
	if (pData)
	{
		return GAD_CreateExtract(pData);
	}
	return NULL;
}


void entity_DestroyLocalGameAccountDataExtract(GameAccountDataExtract **ppExtract)
{
	StructDestroySafe(parse_GameAccountDataExtract, ppExtract);
}


GameAccountDataExtract *entity_GetCachedGameAccountDataExtract(Entity *pEnt)
{
	if (pEnt && pEnt->pPlayer) {
		CONST_OPTIONAL_STRUCT(PlayerAccountData) pPlayerAccountData = pEnt->pPlayer->pPlayerAccountData;
		if (pPlayerAccountData) {
			PERFINFO_AUTO_START_FUNC();

			// Don't allow this to be used inside a transaction
			devassertmsgf(!exprCurAutoTrans, "Cannot access cached game account data extracts from an autotransaction (%s)", exprCurAutoTrans);

			if (pPlayerAccountData->uExtractLastUpdated != g_uGameAccountModifyCount || !pPlayerAccountData->pExtract) {
				// Extract is out of date and needs updating.  First update modify marker; Note that in a very rare case the marker can be the same for the player but the extract is NULL
				// The null check above fixes that case. I was unable to repeat this case but it did happen once KDB
				pPlayerAccountData->uExtractLastUpdated = g_uGameAccountModifyCount;

				// Then update the extract
				if (!pPlayerAccountData->pExtract) {
					pPlayerAccountData->pExtract = GAD_CreateExtract(entity_GetGameAccount(pEnt));
				} else {
					GAD_UpdateExtract(entity_GetGameAccount(pEnt), pPlayerAccountData->pExtract);
				}
			}
			PERFINFO_AUTO_STOP();

			return pEnt->pPlayer->pPlayerAccountData->pExtract;
		}
	}
	return NULL;
}


// ----------------------------------------------------------------------------------
// Expression Functions
// ----------------------------------------------------------------------------------

// Return the value associated with a GameAccountData attribute.
AUTO_EXPR_FUNC(Entity, Player, UIGen, Mission) ACMD_NAME(EntGetAccountDataValue);
const char *gad_expr_EntGetAccountDataValue(SA_PARAM_OP_VALID Entity *pEnt, const char *pchAttrib)
{
	GameAccountData *pData;
	AttribValuePair *pPair;
	pData = entity_GetGameAccount(pEnt);
	if (!pData)
		return "";
	pPair = eaIndexedGetUsingString(&pData->eaKeys, pchAttrib);
	return (pPair && pPair->pchValue) ? pPair->pchValue : "";
}

// Return the value associated with a GameAccountData attribute.
AUTO_EXPR_FUNC(Player, Mission) ACMD_NAME(PlayerGetAccountDataValue);
const char *gad_expr_PlayerGetAccountDataValue(ExprContext* pContext, const char *pchAttrib)
{
	Entity* pPlayerEnt = exprContextGetVarPointerUnsafePooled(pContext, g_PlayerVarName);
	return gad_expr_EntGetAccountDataValue(pPlayerEnt, pchAttrib);
}

// Set the value associated with a GameAccountData attribute.
AUTO_EXPR_FUNC(entityutil, Mission) ACMD_NAME(EntSetAccountDataValue);
void gad_expr_EntSetAccountDataValue(SA_PARAM_OP_VALID Entity *pEnt, const char *pchAttrib, const char *pchValue)
{
#ifdef GAMESERVER
	gslGAD_SetAttrib(pEnt, pchAttrib, pchValue);
#elif GAMECLIENT
	ServerCmd_gslGAD_SetAttrib(pchAttrib, pchValue);
#else
	assertmsg(false, "You can only call EntSetAccountDataValue on a GameServer or GameClient");
#endif
}


// VAS 011313 - You would not BELIEVE the asinine crap that requires me to put this in here, instead of in GameAccountData.c
// Seriously, I hate this crap. A lot. A whole lot.
AUTO_TRANS_HELPER
ATR_LOCKS(pData, ".eaAccountKeyValues");
S64 gad_trh_GetAccountValueInt(ATH_ARG NOCONST(GameAccountData) *pData, const char *pchKey)
{
	NOCONST(AccountProxyKeyValueInfoContainer) *pPair = NULL;
	const char *pSubbedKey = NULL;
	if (ISNULL(pData) || !pchKey || !*pchKey)
		return 0;
	pSubbedKey = AccountProxySubstituteKeyTokens(pchKey, AccountGetShardProxyName(), ShardCommon_GetClusterName(), microtrans_GetShardEnvironmentName());
	pPair = eaIndexedGetUsingString(&pData->eaAccountKeyValues, pSubbedKey);
	return NONNULL(pPair) && NONNULL(pPair->pValue) ? atoi64(pPair->pValue) : 0;
}

S64 gad_GetAccountValueIntFromExtract(GameAccountDataExtract *pExtract, const char *pchKey)
{
	AccountProxyKeyValueInfoContainer *pPair = NULL;
	const char *pSubbedKey = NULL;
	if (!pExtract || !pchKey || !*pchKey)
		return 0;
	pSubbedKey = AccountProxySubstituteKeyTokens(pchKey, AccountGetShardProxyName(), ShardCommon_GetClusterName(), microtrans_GetShardEnvironmentName());
	pPair = eaIndexedGetUsingString(&pExtract->eaAccountKeyValues, pSubbedKey);
	return pPair && pPair->pValue ? atoi64(pPair->pValue) : 0;
}

// Return the value associated with a GameAccountData attribute.
AUTO_EXPR_FUNC(Entity, Player, UIGen, Mission) ACMD_NAME(EntGetAccountKeyValue);
S64 gad_expr_EntGetAccountKeyValue(SA_PARAM_OP_VALID Entity *pEnt, const char *pchKey)
{
	GameAccountData *pData = NULL;
	AccountProxyKeyValueInfoContainer *pPair = NULL;
	const char *pSubbedKey = NULL;
	if (!(pEnt && pEnt->pPlayer && pEnt->pPlayer->pPlayerAccountData))
		return 0;
	pData = entity_GetGameAccount(pEnt);
	if (!pData)
		return 0;
	pSubbedKey = AccountProxySubstituteKeyTokens(pchKey, AccountGetShardProxyName(), ShardCommon_GetClusterName(), microtrans_GetShardEnvironmentName());
	pPair = eaIndexedGetUsingString(&pData->eaAccountKeyValues, pSubbedKey);
	return pPair && pPair->pValue ? atoi64(pPair->pValue) : 0;
}


static S32 GAD_DuplicateVanityPets(SA_PARAM_OP_VALID Entity *pEnt, SA_PARAM_OP_VALID ItemDef *pItemDef)
{
	if (pEnt && pEnt->pPlayer && pItemDef && pItemDef->eType == kItemType_VanityPet)
	{
		S32 iAmountFound = 0;
		GameAccountData *pData = entity_GetGameAccount(pEnt);
		if(pData)
		{
			S32 iPetIdx = 0;
			for(iPetIdx = eaSize(&pItemDef->ppItemVanityPetRefs)-1; iPetIdx >= 0; iPetIdx--)
			{
				if(eaFindString((char***)&pData->eaVanityPets,
					REF_STRING_FROM_HANDLE(pItemDef->ppItemVanityPetRefs[iPetIdx]->hPowerDef)) >= 0)
				{
						iAmountFound++;
				}
			}
		}

		//If you already have ALL the pets, return -1 for don't do that
		if(iAmountFound == eaSize(&pItemDef->ppItemVanityPetRefs))
			iAmountFound = -1;

		return(iAmountFound);
	}
	else
		return 0;
}


// Returns the number of duplicate vanity pets on the item.  Returns -1 if ALL pets 
AUTO_EXPR_FUNC(Player) ACMD_NAME(EntGetDupVanityPets);
S32 exprGAD_DuplicateVanityPets(SA_PARAM_OP_VALID Entity *pEnt, SA_PARAM_OP_VALID ItemDef *pItemDef)
{
	if(pItemDef && pItemDef->eType == kItemType_ItemValue && pItemDef->pCraft)
	{
		// recipe, get the recipe
		pItemDef = GET_REF(pItemDef->pCraft->hItemResult);
	}

	return(GAD_DuplicateVanityPets(pEnt, pItemDef));
}


bool entity_LifetimeSubscription(Entity *pEnt)
{
	if(pEnt && pEnt->pPlayer && pEnt->pPlayer->accessLevel >= ACCESS_GM_FULL)
	{
		return true;
	}
	else
	{
		GameAccountData *pData = entity_GetGameAccount(pEnt);

		if(pData)
		{
			return ( !!pData->bLifetimeSubscription );
		}
	}

	//Return false if I cannot find their information
	return false;
}


AUTO_EXPR_FUNC(entityutil) ACMD_NAME(EntIsLifetimeSubscription);
S32 exprEntIsLfetimeSubscription(SA_PARAM_OP_VALID Entity *pEnt)
{
	return entity_LifetimeSubscription(pEnt);
}


AUTO_TRANS_HELPER_SIMPLE;
U32 entity_GetDaysSubscribedFromExtract(GameAccountDataExtract *pExtract)
{
	U32 iDays = 0;
	
	if(pExtract)
	{
		iDays = pExtract->iDaysSubscribed;
	}
	
	return iDays;
}

U32 entity_GetDaysSubscribed(Entity *pEnt)
{
	U32 iDays = 0;
	GameAccountData *pData = entity_GetGameAccount(pEnt);
	
	if(pData)
	{
		iDays = pData->iDaysSubscribed;
	}
	
	return iDays;
}


// return the number of days subscribed
AUTO_EXPR_FUNC(entityutil) ACMD_NAME(EntGetDaysSubscribed);
U32 exprEntGetDaysSubscribed(SA_PARAM_OP_VALID Entity *pEnt)
{
	return entity_GetDaysSubscribed(pEnt);
}

bool entity_PressSubscription(Entity *pEnt)
{
	GameAccountData *pData = entity_GetGameAccount(pEnt);

	if(pData)
	{
		return ( !!pData->bPress );
	}

	//Return false if I cannot find their information
	return false;
}

AUTO_EXPR_FUNC(entityutil) ACMD_NAME(EntIsPressSubscription);
S32 exprEntIsPressSubscription(SA_PARAM_OP_VALID Entity *pEnt)
{
	return entity_PressSubscription(pEnt);
}

bool entity_LinkedAccount(Entity *pEnt)
{
	GameAccountData *pData = entity_GetGameAccount(pEnt);

	if (pData)
	{
		return ( !!pData->bLinkedAccount );
	}

	return false;
}

AUTO_EXPR_FUNC(Player) ACMD_NAME(EntIsLinkedAccount);
S32 exprEntIsLinkedAccount(SA_PARAM_OP_VALID Entity *pEnt)
{
	return entity_LinkedAccount(pEnt);
}

bool entity_ShadowAccount(Entity *pEnt)
{
	GameAccountData *pData = entity_GetGameAccount(pEnt);

	if (pData)
	{
		return ( !!pData->bShadowAccount );
	}

	return false;
}

AUTO_EXPR_FUNC(Player) ACMD_NAME(EntIsShadowAccount);
S32 exprEntIsShadowAccount(SA_PARAM_OP_VALID Entity *pEnt)
{
	return entity_ShadowAccount(pEnt);
}

U32 entity_AccountFirstPlayed(Entity *pEnt)
{
	GameAccountData *pData = entity_GetGameAccount(pEnt);

	if (pData)
	{
		return pData->uFirstPlayedTime;
	}

	return 0;
}

AUTO_EXPR_FUNC(entityutil, Mission) ACMD_NAME(EntGetAccountFirstPlayed);
U32 exprEntGetAccountFirstPlayed(SA_PARAM_OP_VALID Entity *pEnt)
{
	return entity_AccountFirstPlayed(pEnt);
}

U32 entity_AccountTotalPlayed(Entity *pEnt)
{
	GameAccountData *pData = entity_GetGameAccount(pEnt);

	if (pData)
	{
		return pData->uTotalPlayedTime_AccountServer;
	}

	return 0;
}

AUTO_EXPR_FUNC(entityutil, Mission) ACMD_NAME(EntGetAccountTotalPlayed);
U32 exprEntGetAccountTotalPlayed(SA_PARAM_OP_VALID Entity *pEnt)
{
	return entity_AccountTotalPlayed(pEnt);
}

//Search through the game account data for the account id in the recruits list
U32 GAD_AccountIsRecruit(const GameAccountData *pData, U32 iAccountID)
{
	U32 uiRecruitTime = 0;

	int idx;

	RecruitState minRecruitState = RS_Accepted;

    if ( pData == NULL )
    {
        return 0;
    }

	if ( gConf.bRecruitRequiresUpgraded )
	{
		minRecruitState = RS_Upgraded;
	}

	for(idx = eaSize(&pData->eaRecruits)-1; idx >= 0; idx--)
	{
		RecruitContainer *pRecruit = pData->eaRecruits[idx];
		if( pRecruit->uAccountID == iAccountID &&
			pRecruit->eRecruitState >= minRecruitState && 
			pRecruit->eRecruitState < RS_OfferCancelled && 
			pRecruit->uAcceptedTimeSS2000 > 0 &&
			!stricmp(pRecruit->pProductInternalName, GetProductName()))
		{
			uiRecruitTime = pRecruit->uAcceptedTimeSS2000;
			break;
		}
	}

	return uiRecruitTime;
}


//Search through the game account data for the account id in the recruiters list
U32 GAD_AccountIsRecruiter(const GameAccountData *pData, U32 iAccountID)
{
	U32 uiRecruitTime = 0;

	int idx;

    if ( pData == NULL )
    {
        return 0;
    }

	for(idx = eaSize(&pData->eaRecruiters)-1; idx >= 0; idx--)
	{
		RecruiterContainer *pRecruiter = pData->eaRecruiters[idx];
		if(pRecruiter->uAccountID == iAccountID && 
			pRecruiter->uAcceptedTimeSS2000 > 0 &&
			!stricmp(pRecruiter->pProductInternalName, GetProductName()))
		{
			uiRecruitTime = pRecruiter->uAcceptedTimeSS2000;
			break;
		}
	}

	return uiRecruitTime;
}


//Search through the entity's account data for the account id in the recruits list
U32 entity_IsRecruit(Entity *pEntSource, Entity *pEntTarget)
{
	U32 uiRecruitTime = 0;

	if(entGetAccountID(pEntSource) > 0 && entGetAccountID(pEntTarget) > 0)
	{		
		GameAccountData *pData = entity_GetGameAccount(pEntSource);
		if(pData)
		{
			uiRecruitTime = GAD_AccountIsRecruit(pData, entGetAccountID(pEntTarget));
		}
	}

	return uiRecruitTime;
}


//Search through the entity's account data for the account id in the recruiters list
U32 entity_IsRecruiter(Entity *pEntSource, Entity *pEntTarget)
{
	U32 uiRecruitTime = 0;

	if(entGetAccountID(pEntSource) > 0 && entGetAccountID(pEntTarget) > 0)
	{		
		GameAccountData *pData = entity_GetGameAccount(pEntSource);
		if(pData)
		{
			uiRecruitTime = GAD_AccountIsRecruiter(pData, entGetAccountID(pEntTarget));
		}
	}

	return uiRecruitTime;
}


// Is the target entity a recruit of the source AND newly accepted
S32 entity_IsNewRecruit(Entity *pEntSource, Entity *pEntTarget)
{
	U32 uiTimeAccepted = entity_IsRecruit(pEntSource, pEntTarget);
	if(uiTimeAccepted && 
		timeServerSecondsSince2000() - uiTimeAccepted < NEW_RECRUIT_TIME_LENGTH)
	{
		return true;
	}

	return false;
}


// Is the target entity a recruiter of the source AND newly accepted
S32 entity_IsNewRecruiter(Entity *pEntSource, Entity *pEntTarget)
{
	U32 uiTimeAccepted = entity_IsRecruiter(pEntSource, pEntTarget);
	if(uiTimeAccepted && 
		timeServerSecondsSince2000() - uiTimeAccepted < NEW_RECRUIT_TIME_LENGTH)
	{
		return true;
	}

	return false;
}


//Can I warp to/from this map?
S32 entity_IsWarpRestricted(const char *pchMapName)
{
	return -1!=eaFindString(&g_eaDisallowedWarpMaps, pchMapName);
}


// ----------------------------------------------------------------------------------
// Transaction Functions
// ----------------------------------------------------------------------------------

AUTO_TRANSACTION ATR_LOCKS(pData, ".fLongPlayTime");
enumTransactionOutcome GameAccount_tr_LongPlayTime(ATR_ARGS, NOCONST(GameAccountData) *pData, float fLongPlayTime)
{
	if(fLongPlayTime > pData->fLongPlayTime)
	{
		pData->fLongPlayTime = fLongPlayTime;
	}

	return TRANSACTION_OUTCOME_SUCCESS;
}

AUTO_TRANSACTION
ATR_LOCKS(pData, ".Eakeys[]");
enumTransactionOutcome slGAD_tr_SetAttrib(ATR_ARGS, NOCONST(GameAccountData) *pData, const char *pchKey, const char *pchValue)
{
	return(slGAD_trh_SetAttrib(ATR_PASS_ARGS, pData, pchKey, pchValue));
}

AUTO_TRANSACTION
	ATR_LOCKS(pData, ".pchVoiceUsername, .pchVoicePassword, .iVoiceAccountID");
enumTransactionOutcome slGAD_tr_SetVoiceInfo(ATR_ARGS, NOCONST(GameAccountData) *pData, const char* voice_un, const char* voice_pw, int voice_id)
{
	if(!stricmp(pData->pchVoicePassword, voice_pw) && !stricmp(pData->pchVoiceUsername, voice_un) && pData->iVoiceAccountID==voice_id)
		return TRANSACTION_OUTCOME_SUCCESS;

	StructFreeStringSafe(&pData->pchVoicePassword);
	StructFreeStringSafe(&pData->pchVoiceUsername);
	pData->pchVoicePassword = StructAllocString(voice_pw);
	pData->pchVoiceUsername = StructAllocString(voice_un);
	pData->iVoiceAccountID = voice_id;

	return TRANSACTION_OUTCOME_SUCCESS;
}

// jswdeprecated
AUTO_TRANSACTION
ATR_LOCKS(pAccountData, ".Blifetimesubscription, .Bpress, .Iversion, .Eacostumekeys, .Eakeys");
enumTransactionOutcome slGAD_tr_LoginFixup(	ATR_ARGS, NOCONST(GameAccountData) *pAccountData, 
	ParsedAVPList *pAttribList, SpeciesUnlockList *pSpeciesList, S32 bLifetime, S32 bPress)
{
	if(pAttribList)
	{
		EARRAY_CONST_FOREACH_BEGIN(pAttribList->eaPairs, i, s);
		ParsedAVP *pPair = eaGet(&pAttribList->eaPairs, i);
		if(pPair && pPair->pchValue && atoi(pPair->pchValue) > 0)
		{
			switch(pPair->eType)
			{
			default:
				break;
			case kMicroItemType_PlayerCostume:
				{
					slGAD_trh_UnlockCostumeRef_Force(ATR_PASS_ARGS, pAccountData, pPair->pchAttribute);
					break;
				}
			case kMicroItemType_Costume:
				{
					slGAD_trh_UnlockCostumeItem_Force(ATR_PASS_ARGS, pAccountData, pPair->pchAttribute);
					break;
				}
			case kMicroItemType_Species:
				{
					SpeciesUnlock *pSpecies = eaIndexedGetUsingString(&pSpeciesList->eaSpeciesUnlocks,pPair->pchItemIdent);
					if(pSpecies)
						slGAD_trh_UnlockSpecies_Force(ATR_PASS_ARGS, pAccountData, pSpecies, pSpecies->pchSpeciesUnlockCode);
					break;
				}
			case kMicroItemType_AttribValue:
				{
					char *pchAttrib = estrStackCreateFromStr(pPair->pchItemIdent);
					estrReplaceOccurrences(&pchAttrib, " ", ".");
					slGAD_trh_UnlockAttribValue_Force(ATR_PASS_ARGS, pAccountData, pchAttrib);
					estrDestroy(&pchAttrib);
					break;
				}
			}
		}
		EARRAY_FOREACH_END;
	}

	pAccountData->bLifetimeSubscription = bLifetime;
	pAccountData->bPress = bPress;

	return TRANSACTION_OUTCOME_SUCCESS;
}

AUTO_TRANS_HELPER;
bool GAD_trh_CheckRequiredKeyValue(ATR_ARGS, ATH_ARG NOCONST(GameAccountData)* pData, GameAccountDataRequiredKeyValue* pRequiredKeyValue)
{
	if (pRequiredKeyValue->pchSpecificStringValue)
	{
		const char* pchStringValue = gad_trh_GetAttribString(pData, pRequiredKeyValue->pchKey);
		if (stricmp(pRequiredKeyValue->pchSpecificStringValue, pchStringValue) != 0)
		{
			return false;
		}
	}
	else
	{
		S32 iIntValue = gad_trh_GetAttribInt(pData, pRequiredKeyValue->pchKey);
		if (iIntValue < pRequiredKeyValue->iMinIntValue ||
			(pRequiredKeyValue->iMaxIntValue >= 0 && iIntValue > pRequiredKeyValue->iMaxIntValue))
		{
			return false;
		}
	}
	return true;
}

AUTO_TRANS_HELPER;
bool GAD_trh_CheckRequiredTokenValue(ATR_ARGS, ATH_ARG NOCONST(GameAccountData)* pData, GameAccountDataRequiredTokenValue* pToken)
{
	S32 iValue = 0;
	if (GamePermissions_trh_GetPermissionValueUncached(pData, pToken->pchKey, &iValue))
	{
		if (iValue != pToken->iValue)
		{
			return false;
		}
	}
	else if (pToken->iValue)
	{
		return false;
	}
	return true;
}

AUTO_TRANS_HELPER;
bool GAD_trh_HasRequiredData(ATR_ARGS, ATH_ARG NOCONST(GameAccountData)* pData, GameAccountDataRequiredValues* pRequiredData)
{
	int i;

	// Check required keys
	for (i = eaSize(&pRequiredData->eaKeyValues)-1; i >= 0; i--)
	{
		GameAccountDataRequiredKeyValue* pRequiredKeyValue = pRequiredData->eaKeyValues[i];

		if (!GAD_trh_CheckRequiredKeyValue(ATR_PASS_ARGS, pData, pRequiredKeyValue))
		{
			return false;
		}
	}
	// Check required tokens
	for (i = eaSize(&pRequiredData->eaTokenValues)-1; i >= 0; i--)
	{
		GameAccountDataRequiredTokenValue* pToken = pRequiredData->eaTokenValues[i];

		if (!GAD_trh_CheckRequiredTokenValue(ATR_PASS_ARGS, pData, pToken))
		{
			return false;
		}
	}
	return true;
}

AUTO_TRANS_HELPER;
bool GAD_trh_CanMakeNumericPurchaseCheckKeyValues(ATR_ARGS, ATH_ARG NOCONST(GameAccountData)* pData, GameAccountDataNumericPurchaseDef* pPurchaseDef)
{
	int i;

	// Check required AND data
	if (!pPurchaseDef->pRequire || !GAD_trh_HasRequiredData(ATR_PASS_ARGS, pData, pPurchaseDef->pRequire))
	{
		// Check required OR data
		if (eaSize(&pPurchaseDef->eaOrRequire))
		{
			for (i = eaSize(&pPurchaseDef->eaOrRequire)-1; i >= 0; i--)
			{
				if (GAD_trh_HasRequiredData(ATR_PASS_ARGS, pData, pPurchaseDef->eaOrRequire[i]))
				{
					break;
				}
			}
			if (i < 0)
			{
				return false;
			}
		}
		else if (pPurchaseDef->pRequire)
		{
			return false;
		}
	}

	// Check existing key values
	for (i = eaSize(&pPurchaseDef->eaPurchaseKeyValues)-1; i >= 0; i--)
	{
		GameAccountDataPurchaseKeyValue* pPurchaseKeyValue = pPurchaseDef->eaPurchaseKeyValues[i];
		if (pPurchaseKeyValue->pchStringValue)
		{
			const char* pchStringValue = gad_trh_GetAttribString(pData, pPurchaseKeyValue->pchKey);
			if (stricmp(pPurchaseKeyValue->pchStringValue, pchStringValue) != 0)
			{
				break;
			}
		}
		else
		{
			break;
		}
	}
	// If all key values already match the purchase values, then don't purchase this again
	if (i < 0)
	{
		return false;
	}
	return true;
}

AUTO_TRANS_HELPER;
bool GAD_trh_EntCanMakeNumericPurchase(ATR_ARGS, ATH_ARG NOCONST(Entity)* pEnt, ATH_ARG NOCONST(GameAccountData)* pData, GameAccountDataNumericPurchaseDef* pPurchaseDef, bool bCheckKeyValues)
{
	if (NONNULL(pEnt) && pPurchaseDef)
	{
		S32 iNumericValue = inv_trh_GetNumericValue(ATR_PASS_ARGS, pEnt, pPurchaseDef->pchNumericItemDef);
		if (iNumericValue < pPurchaseDef->iNumericCost)
		{
			return false;
		}
		else if (bCheckKeyValues)
		{
			if (!GAD_trh_CanMakeNumericPurchaseCheckKeyValues(ATR_PASS_ARGS, pData, pPurchaseDef))
			{
				return false;
			}
		}
		return true;
	}
	return false;
}

bool GAD_PossibleCharacterCanMakeNumericPurchase(PossibleCharacterNumeric **eaNumerics, ContainerID iVirtualShardID, GameAccountData* pData, GameAccountDataNumericPurchaseDef* pPurchaseDef, bool bCheckKeyValues)
{
	if (!iVirtualShardID && pPurchaseDef)
	{
		PossibleCharacterNumeric* pNumeric = eaIndexedGetUsingString(&eaNumerics, pPurchaseDef->pchNumericItemDef);
		S32 iNumericValue = SAFE_MEMBER(pNumeric, iNumericValue);

		if (iNumericValue < pPurchaseDef->iNumericCost)
		{
			return false;
		}
		else if (bCheckKeyValues)
		{
			if (!GAD_CanMakeNumericPurchaseCheckKeyValues(pData, pPurchaseDef))
			{
				return false;
			}
		}
		return true;
	}
	return false;
}

AUTO_TRANSACTION
ATR_LOCKS(pEnt, ".Psaved.pSCPdata.Isummonedscp, .Psaved.pSCPdata.Fcachedpetbonusxppct, .Psaved.pSCPdata.erSCP, .pInventoryV2.ppInventoryBags[], .Pplayer.Pugckillcreditlimit, .Pchar.Ilevelexp, .Pplayer.Pplayeraccountdata.Eagamepermissionmaxvaluenumerics, .Hallegiance, .Hsuballegiance, pInventoryV2.ppLiteBags[]")
ATR_LOCKS(pData, ".Eakeys, .Eatokens");
enumTransactionOutcome GameAccount_tr_EntNumericPurchase(ATR_ARGS, NOCONST(Entity) *pEnt, NOCONST(GameAccountData) *pData, const char* pchPurchaseDefName, const ItemChangeReason *pReason)
{
	GameAccountDataNumericPurchaseDef* pDef = GAD_NumericPurchaseDefFromName(pchPurchaseDefName);

	if (!GAD_trh_EntCanMakeNumericPurchase(ATR_PASS_ARGS, pEnt, pData, pDef, true))
	{
		return TRANSACTION_OUTCOME_FAILURE;
	}
	else
	{
		S32 iNumericCost = pDef->iNumericCost;
		S32 iNumericValue = inv_trh_GetNumericValue(ATR_PASS_ARGS, pEnt, pDef->pchNumericItemDef);
		if (!inv_ent_trh_SetNumeric(ATR_PASS_ARGS, pEnt, false, pDef->pchNumericItemDef, iNumericValue - iNumericCost, pReason))
		{
			return TRANSACTION_OUTCOME_FAILURE;
		}
		else
		{
			int i;
			for (i = eaSize(&pDef->eaPurchaseKeyValues)-1; i >= 0; i--)
			{
				GameAccountDataPurchaseKeyValue* pPurchaseKeyValue = pDef->eaPurchaseKeyValues[i];
				if (pPurchaseKeyValue->pchStringValue && pPurchaseKeyValue->pchStringValue[0])
				{
					if (slGAD_trh_SetAttrib(ATR_PASS_ARGS, pData, pPurchaseKeyValue->pchKey, pPurchaseKeyValue->pchStringValue) == TRANSACTION_OUTCOME_FAILURE)
					{
						return TRANSACTION_OUTCOME_FAILURE;
					}
				}
				else
				{
					char pchNewIntValue[32];
					S32 iCurrIntValue = gad_trh_GetAttribInt(pData, pPurchaseKeyValue->pchKey);
					sprintf(pchNewIntValue, "%d", iCurrIntValue + pPurchaseKeyValue->iIntValue);
					
					if (slGAD_trh_SetAttrib(ATR_PASS_ARGS, pData, pPurchaseKeyValue->pchKey, pchNewIntValue) == TRANSACTION_OUTCOME_FAILURE)
					{
						return TRANSACTION_OUTCOME_FAILURE;
					}
				}
			}
		}
	}

	return TRANSACTION_OUTCOME_SUCCESS;
}

AUTO_TRANSACTION ATR_LOCKS(pData, ".iLastChatAccessLevel");
enumTransactionOutcome GameAccount_tr_LastChatAccessLevel(ATR_ARGS, NOCONST(GameAccountData) *pData, int iLastChatAccessLevel)
{
	pData->iLastChatAccessLevel = iLastChatAccessLevel + 1;
	return TRANSACTION_OUTCOME_SUCCESS;
}

//////////////////////////////////////////////////////////////////////////////////
//
//  Game Account transaction for changing foundry tips


AUTO_TRANSACTION ATR_LOCKS(pData, ".iFoundryTipBalance");
enumTransactionOutcome GameAccount_tr_AddToUGCTips(ATR_ARGS, NOCONST(GameAccountData) *pData, int iAddAmount)
{
	RETURN_IF_GAD_MODIFICATION_DISALLOWED(TRANSACTION_OUTCOME_FAILURE);

	if (ISNULL(pData))
	{
		return TRANSACTION_OUTCOME_FAILURE;
	}

	pData->iFoundryTipBalance += iAddAmount;
	if (pData->iFoundryTipBalance<0)
	{
		return TRANSACTION_OUTCOME_FAILURE;
	}
	return TRANSACTION_OUTCOME_SUCCESS;
}


#include "GameAccountDataCommon_h_ast.c"
