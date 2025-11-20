#include "MicroTransactions.h"

#include "AccountDataCache.h"
#include "accountnet.h"
#include "Alerts.h"
#include "allegiance.h"
#include "BlockEarray.h"
#include "entCritter.h"
#include "entEnums.h"
#include "Entity.h"
#include "EntityLib.h"
#include "error.h"
#include "Expression.h"
#include "ExpressionFunc.h"
#include "ExpressionPrivate.h"
#include "fileutil.h"
#include "FolderCache.h"
#include "GameStringFormat.h"
#include "GlobalTypes.h"
#include "GameAccountDataCommon.h"
#include "GamePermissionsCommon.h"
#include "itemCommon.h"
#include "logging.h"
#include "microtransactions_common.h"
#include "Player.h"
#include "Powers.h"
#include "ResourceManager.h"
#include "SavedPetCommon.h"
#include "species_common.h"
#include "StringCache.h"
#include "StringUtil.h"
#include "Money.h"
#include "inventoryCommon.h"

#include "GamePermissionsCommon_h_ast.h"
#include "MicroTransactions_h_ast.h"

AUTO_RUN_ANON(memBudgetAddMapping(__FILE__, BUDGET_GameSystems););

DictionaryHandle g_hMicroTransDefDict;
DictionaryHandle g_hMicroTransCategoryDict;

MicroTransactionCategory *g_pMainMTCategory = NULL;

MicroTransConfig g_MicroTransConfig = {0};

MicroTrans_ShardConfig *g_pMicroTrans_ShardConfig = NULL;

int g_bPointBuyDebugging = false;
AUTO_CMD_INT(g_bPointBuyDebugging, Microtrans_PointBuyDebugging) ACMD_CMDLINE ACMD_ACCESSLEVEL(9);

// This looks a little messy, but it's sane - the idea is that, on shard startup, we set an MT shard category we want to use
// However, the config may not support that category, or the current shard mode might not allow it
// We want to make sure that if we reload the config, we have the ability to revert to the correct originally-specified category
// We also only want the currently-active shard category to be publicly exposed to other code files
static MicroTrans_ShardCategory s_eMicroTrans_ShardCategory_internal = kMTShardCategory_Off;
MicroTrans_ShardCategory g_eMicroTrans_ShardCategory = kMTShardCategory_Off;

AUTO_COMMAND ACMD_NAME(MicroTrans_ShardCategory) ACMD_CMDLINE ACMD_ACCESSLEVEL(0) ACMD_HIDE;
void MicroTrans_SetShardCategory_CmdLine(const char *pCategory)
{
	s_eMicroTrans_ShardCategory_internal = StaticDefineIntGetInt(MicroTrans_ShardCategoryEnum, pCategory);
	if(s_eMicroTrans_ShardCategory_internal < 0)
	{
		s_eMicroTrans_ShardCategory_internal = kMTShardCategory_Off;

		if(isProductionMode() && GetAppGlobalType() == GLOBALTYPE_ACCOUNTPROXYSERVER)
		{
			TriggerAlertf("MICROTRANS_INVALID_CATEGORY", ALERTLEVEL_CRITICAL, ALERTCATEGORY_NETOPS, 0, GetAppGlobalType(), GetAppGlobalID(),
				GetAppGlobalType(), GetAppGlobalID(), getHostName(), 0, "Microtransactions have been disabled due to an invalid category! "
				"This shard was started with a microtransaction shard category of \"%s\", which is not a recognized microtransaction shard category. "
				"Microtransactions will remain off until the shard is restarted with a valid microtransaction shard category.", pCategory);
		}
	}
}

static void MicroTrans_ChooseConfig(void)
{
	g_eMicroTrans_ShardCategory = s_eMicroTrans_ShardCategory_internal;

	if(s_eMicroTrans_ShardCategory_internal == kMTShardCategory_Off && isDevelopmentMode())
	{
		// In development mode, if shard category is off, default to Dev
		g_eMicroTrans_ShardCategory = kMTShardCategory_Dev;
	}
	else if(s_eMicroTrans_ShardCategory_internal == kMTShardCategory_Dev && isProductionMode())
	{
		// In production mode, Dev category is disallowed, turn off
		g_eMicroTrans_ShardCategory = kMTShardCategory_Off;

		// The Proxy should specifically be in charge of alerting here, otherwise there will be massive alert spam
		if(GetAppGlobalType() == GLOBALTYPE_ACCOUNTPROXYSERVER)
		{
			TriggerAlertf("MICROTRANS_DISALLOWED_CONFIG", ALERTLEVEL_CRITICAL, ALERTCATEGORY_NETOPS, 0, GetAppGlobalType(), GetAppGlobalID(),
				GetAppGlobalType(), GetAppGlobalID(), getHostName(), 0, "Microtransactions have been disabled due to a disallowed configuration! "
				"This shard was started with a microtransaction shard category of \"%s\", which is not permitted in production mode. "
				"Microtransactions will remain off until the shard is restarted with a permitted microtransaction shard category.",
				StaticDefineIntRevLookup(MicroTrans_ShardCategoryEnum, s_eMicroTrans_ShardCategory_internal));
		}
	}

	if(g_eMicroTrans_ShardCategory != kMTShardCategory_Off)
	{
		MicroTrans_ShardConfig *pConfig = eaIndexedGetUsingInt(&g_MicroTransConfig.ppShardConfigs, g_eMicroTrans_ShardCategory);
		if(!pConfig && g_eMicroTrans_ShardCategory == kMTShardCategory_Dev)
		{
			// We can only get here in development mode
			// If there's no config for Dev category, then use Live as a reasonable default
			pConfig = eaIndexedGetUsingInt(&g_MicroTransConfig.ppShardConfigs, kMTShardCategory_Live);
		}

		if(!pConfig)
		{
			// No matter what, if we get here with no config, just turn off for safety, alert if production
			g_eMicroTrans_ShardCategory = kMTShardCategory_Off;
			if(isProductionMode() && GetAppGlobalType() == GLOBALTYPE_ACCOUNTPROXYSERVER)
			{
				TriggerAlertf("MICROTRANS_MISSING_CONFIG", ALERTLEVEL_CRITICAL, ALERTCATEGORY_NETOPS, 0, GetAppGlobalType(), GetAppGlobalID(),
					GetAppGlobalType(), GetAppGlobalID(), getHostName(), 0, "Microtransactions have been disabled due to a missing configuration! "
					"This shard was started with a microtransaction shard category of \"%s\", which does not have a corresponding configuration. "
					"Microtransactions will remain off until the shard is restarted with a category that does have a corresponding configuration.",
					StaticDefineIntRevLookup(MicroTrans_ShardCategoryEnum, s_eMicroTrans_ShardCategory_internal));
			}
		}
		else
		{
			loadupdate_printf("Using the [%s] config for the C-Store. Prefix=[%s] Currency=[%s]\n",
				StaticDefineIntRevLookup(MicroTrans_ShardCategoryEnum, g_eMicroTrans_ShardCategory),
				pConfig->pchCategoryPrefix,
				pConfig->pchCurrency);

			loadupdate_printf("Message of the Day (%s)...\n", 
 				(pConfig->pMOTD 
 				&& pConfig->pMOTD->pchName 
 				&& pConfig->pMOTD->pchCategory) ? 
 				("On") : ("Off") );
		}

		g_pMicroTrans_ShardConfig = pConfig;
	}
}

void MicroTrans_SetShardCategory(MicroTrans_ShardCategory eCategory)
{
	s_eMicroTrans_ShardCategory_internal = eCategory;
	MicroTrans_ChooseConfig();
}

void MicroTrans_ConfigLoad(void)
{
	
	loadstart_printf("Loading MicroTransaction Configuration... ");

	ParserLoadFiles(NULL, "defs/config/MTConfig.def", "MTConfig.bin", PARSER_OPTIONALFLAG, parse_MicroTransConfig, &g_MicroTransConfig);

	if(eaSize(&g_MicroTransConfig.eaSpecialKeys))
	{
		loadupdate_printf("\n Special Account Keys (%d)...", 
			eaSize(&g_MicroTransConfig.eaSpecialKeys) );
	}
	else
	{
		loadupdate_printf("\n Special Account Keys (Off)...");
	}

	MicroTrans_ChooseConfig();

    loadend_printf("\n... MicroTransaction Configuration loaded");
}

static void MicroTransactionConfig_ReloadCB(const char *pchRelPath, int unused)
{
	loadstart_printf("Reloading Microtransaction Config...");

	fileWaitForExclusiveAccess(pchRelPath);
	errorLogFileIsBeingReloaded(pchRelPath);

	MicroTrans_ConfigLoad();

	loadend_printf(" done Reloading Microtransaction Config");
}

AUTO_STARTUP(MicroTransactionConfig);
void MicroTrans_ConfigLoadOnce(void)
{
    MicroTrans_ConfigLoad();

    if (isDevelopmentMode())
    {
        FolderCacheSetCallback(FOLDER_CACHE_CALLBACK_UPDATE|FOLDER_CACHE_CALLBACK_DELETE, "defs/config/MTConfig.def", MicroTransactionConfig_ReloadCB);
    }
}

static int microtranspart_Validate(MicroTransactionDef *pDef, MicroTransactionPart *pPart)
{
	int iValid = 1;

	if(!pPart)
	{
		iValid = 0;
		return iValid;
	}

	switch(pPart->ePartType)
	{
	case kMicroPart_Item:
		{
			if(!GET_REF(pPart->hItemDef))
			{
				ErrorFilenamef(pDef->pchFile, "MicroTransaction %s type %s: Invalid item specified %s.",
					pDef->pchName,
					StaticDefineIntRevLookup(MicroPartTypeEnum, pPart->ePartType),
					REF_STRING_FROM_HANDLE(pPart->hItemDef));
				iValid = 0;
			}
			break;
		}
		
	case kMicroPart_Costume:
		{
			if(!GET_REF(pPart->hItemDef))
			{
				ErrorFilenamef(pDef->pchFile, "MicroTransaction %s type %s: Invalid costume item specified %s.",
					pDef->pchName,
					StaticDefineIntRevLookup(MicroPartTypeEnum, pPart->ePartType),
					REF_STRING_FROM_HANDLE(pPart->hItemDef));
				iValid = 0;
			}
			break;
		}
	case kMicroPart_CostumeRef:
		{
			if(!GET_REF(pPart->hCostumeDef))
			{
				ErrorFilenamef(pDef->pchFile, "MicroTransaction %s type %s: Invalid costume specified %s.",
					pDef->pchName,
					StaticDefineIntRevLookup(MicroPartTypeEnum, pPart->ePartType),
					REF_STRING_FROM_HANDLE(pPart->hCostumeDef));
				iValid = 0;
			}
			break;
		}
	case kMicroPart_VanityPet:
		{
			if(!GET_REF(pPart->hPowerDef))
			{
				ErrorFilenamef(pDef->pchFile, "MicroTransaction %s type %s: Invalid power specified %s.",
					pDef->pchName,
					StaticDefineIntRevLookup(MicroPartTypeEnum, pPart->ePartType),
					REF_STRING_FROM_HANDLE(pPart->hPowerDef));
				iValid = 0;
			}
			break;
		}
	case kMicroPart_Species:
		if(!GET_REF(pPart->hSpeciesDef))
		{
			ErrorFilenamef(pDef->pchFile, "MicroTransaction %s type %s: Invalid species specified %s.",
				pDef->pchName,
				StaticDefineIntRevLookup(MicroPartTypeEnum, pPart->ePartType),
				REF_STRING_FROM_HANDLE(pPart->hSpeciesDef));
			iValid = 0;
		}
		break;
	case kMicroPart_Permission:
		{
			if(entIsServer())
			{
				if(!pPart->pchPermission || !eaIndexedGetUsingString(&g_GamePermissions.eaPermissions, pPart->pchPermission))
				{
					ErrorFilenamef(pDef->pchFile, "MicroTransaction %s type %s: Invalid permission specified %s.",
						pDef->pchName,
						StaticDefineIntRevLookup(MicroPartTypeEnum, pPart->ePartType),
						pPart->pchPermission);
					iValid = 0;
				}
			}
		}
		break;
	case kMicroPart_RewardTable:
		if (IsServer() && !GET_REF(pPart->hRewardTable))
		{
			ErrorFilenamef(pDef->pchFile, "MicroTransaction %s type %s: Invalid reward table specified %s.",
				pDef->pchName,
				StaticDefineIntRevLookup(MicroPartTypeEnum, pPart->ePartType),
				REF_STRING_FROM_HANDLE(pPart->hRewardTable));
			iValid = 0;
		}
		break;
	case kMicroPart_Special:
		break;
	case kMicroPart_Attrib:
		{
			if(!pPart->pPairChange)
			{
				ErrorFilenamef(pDef->pchFile, "MicroTransaction %s type %s: Invalid attribute.",
					pDef->pchName,
					StaticDefineIntRevLookup(MicroPartTypeEnum, pPart->ePartType));
				iValid = 0;
				break;
			}
			if(!pPart->pPairChange->pchAttribute)
			{
				ErrorFilenamef(pDef->pchFile, "MicroTransaction %s type %s: No attribute/key specified.",
					pDef->pchName,
					StaticDefineIntRevLookup(MicroPartTypeEnum, pPart->ePartType));
				iValid = 0;
			}

			if(pPart->pPairChange->eType != kAVChangeType_IntIncrement && pPart->pPairChange->iMinVal != 0)
			{
				ErrorFilenamef(pDef->pchFile, "MicroTransaction %s type %s change type %s: Minimum Integer value specified %d.",
					pDef->pchName,
					StaticDefineIntRevLookup(MicroPartTypeEnum, pPart->ePartType),
					StaticDefineIntRevLookup(AVChangeTypeEnum, pPart->pPairChange->eType),
					pPart->pPairChange->iMinVal);
				iValid = 0;
			}
			if(pPart->pPairChange->eType != kAVChangeType_IntIncrement && pPart->pPairChange->iMaxVal != 0)
			{
				ErrorFilenamef(pDef->pchFile, "MicroTransaction %s type %s change type %s: Maximum Integer value specified %d.",
					pDef->pchName,
					StaticDefineIntRevLookup(MicroPartTypeEnum, pPart->ePartType),
					StaticDefineIntRevLookup(AVChangeTypeEnum, pPart->pPairChange->eType),
					pPart->pPairChange->iMaxVal);
				iValid = 0;
			}
			if(pPart->pPairChange->eType != kAVChangeType_String && pPart->pPairChange->pchStringVal)
			{
				ErrorFilenamef(pDef->pchFile, "MicroTransaction %s type %s change type %s: String value specified %s.",
					pDef->pchName,
					StaticDefineIntRevLookup(MicroPartTypeEnum, pPart->ePartType),
					StaticDefineIntRevLookup(AVChangeTypeEnum, pPart->pPairChange->eType),
					pPart->pPairChange->pchStringVal);
				iValid = 0;
			}
			switch(pPart->pPairChange->eType)
			{
			case kAVChangeType_BooleanNoFail:
			case kAVChangeType_Boolean:
				{
					if(pPart->pPairChange->iVal != 0 && pPart->pPairChange->iVal != 1)
					{
						ErrorFilenamef(pDef->pchFile, "MicroTransaction %s type %s change type %s: Integer value specified %d that should be 0 or 1.",
							pDef->pchName,
							StaticDefineIntRevLookup(MicroPartTypeEnum, pPart->ePartType),
							StaticDefineIntRevLookup(AVChangeTypeEnum, pPart->pPairChange->eType),
							pPart->pPairChange->iVal);
						iValid = 0;
					}		
				}
				break;
			case kAVChangeType_IntSetNoFail:
			case kAVChangeType_IntSet:
				{
				}
				break;
			case kAVChangeType_IntIncrement:
				{
					if(pPart->pPairChange->bClampValues)
					{
						if(pPart->pPairChange->iMinVal > pPart->pPairChange->iMaxVal)
						{
							ErrorFilenamef(pDef->pchFile, "MicroTransaction %s type %s change type %s: Minimum value is greater than Maximum value %d > %d.",
								pDef->pchName,
								StaticDefineIntRevLookup(MicroPartTypeEnum, pPart->ePartType),
								StaticDefineIntRevLookup(AVChangeTypeEnum, pPart->pPairChange->eType),
								pPart->pPairChange->iMinVal,
								pPart->pPairChange->iMaxVal);
							iValid = 0;
						}
					}
				}
				break;
			case kAVChangeType_None:
				{
					ErrorFilenamef(pDef->pchFile, "MicroTransaction %s type %s: Invalid change type %s specified.",
						pDef->pchName,
						StaticDefineIntRevLookup(MicroPartTypeEnum, pPart->ePartType),
						StaticDefineIntRevLookup(AVChangeTypeEnum, pPart->pPairChange->eType));
					iValid = 0;
				}
				break;
			case kAVChangeType_String:
				{
					if(!pPart->pPairChange->pchStringVal)
					{
						ErrorFilenamef(pDef->pchFile, "MicroTransaction %s type %s change type %s: No string value specified.",
							pDef->pchName,
							StaticDefineIntRevLookup(MicroPartTypeEnum, pPart->ePartType),
							StaticDefineIntRevLookup(AVChangeTypeEnum, pPart->pPairChange->eType));
						iValid = 0;
					}
					else if(pPart->pPairChange->iVal != 0)
					{
						ErrorFilenamef(pDef->pchFile, "MicroTransaction %s type %s change type %s: Integer value specified %d.",
							pDef->pchName,
							StaticDefineIntRevLookup(MicroPartTypeEnum, pPart->ePartType),
							StaticDefineIntRevLookup(AVChangeTypeEnum, pPart->pPairChange->eType),
							pPart->pPairChange->iVal);
						iValid = 0;
					}
				}
				break;
			}
			break;
		}
	}

	return iValid;
}

int microtransdef_Validate(MicroTransactionDef *pDef)
{
	int iValid = 1;
	int i;
	bool bAllFound = false;

	for(i = eaSize(&pDef->ppchCategories)-1; i>= 0; i--)
	{
		MicroTransactionCategory *pCategory = RefSystem_ReferentFromString(g_hMicroTransCategoryDict,pDef->ppchCategories[i]);
		if(!pCategory && !pDef->bDeprecated)
		{
			ErrorFilenamef(pDef->pchFile,"Microtransaction %s: Invalid category %s",
				pDef->pchName,
				pDef->ppchCategories[i]);
			iValid = 0;
		}
		else
		{
			if(pCategory == g_pMainMTCategory)
			{
				bAllFound = true;
			}
			if(pCategory && pCategory->pchParentCategory && eaFindString(&pDef->ppchCategories, pCategory->pchParentCategory) != -1)
			{
				ErrorFilenamef(pDef->pchFile,"Microtransaction %s: Is in both the parent category %s and the child category %s",
					pDef->pchName,
					pCategory->pchParentCategory,
					pDef->ppchCategories[i]);
			}
		}
	}

	if(!pDef->bDeprecated)
	{
		if(!bAllFound && g_pMainMTCategory)
		{
			Alertf("Microtransaction %s: Did not find the main category attached to this Microtransaction 'Main is named [%s]'",
				pDef->pchName,
				g_pMainMTCategory->pchName);
		}
		else if(eaSize(&pDef->ppchCategories) <= 1)
		{
			Alertf("Microtransaction %s: Needs to have atleast 2 categories (including the main category) to appear in the CStore window",
				pDef->pchName);
		}
	}
	else
	{
		if(bAllFound && g_pMainMTCategory)
		{
			iValid = 0;
			ErrorFilenamef(pDef->pchFile, "Microtransaction %s: Is both deprecated and in the main category.  Please remove it from the '%s' category.",
				pDef->pchName,
				g_pMainMTCategory->pchName);
		}

		return iValid;
	}

	if(pDef->bOnePerAccount && pDef->bOnePerCharacter)
	{
		ErrorFilenamef(pDef->pchFile, "Microtransaction %s: Cannot be both once per character and once per account.  Please remove one of those designations.",
			pDef->pchName);
		iValid = 0;
	}

	if(IsServer()) 
	{
		if(!GET_REF(pDef->displayNameMesg.hMessage))
		{
			ErrorFilenamef(pDef->pchFile,"Microtransaction %s: Invalid or non-existent display name message [%s]",
				pDef->pchName,
				REF_STRING_FROM_HANDLE(pDef->displayNameMesg.hMessage));
			iValid = 0;
		}
		if(!GET_REF(pDef->descriptionShortMesg.hMessage))
		{
			ErrorFilenamef(pDef->pchFile,"Microtransaction %s: Invalid or non-existent short description message [%s]",
				pDef->pchName,
				REF_STRING_FROM_HANDLE(pDef->descriptionShortMesg.hMessage));
			iValid = 0;
		}
		if(REF_STRING_FROM_HANDLE(pDef->descriptionLongMesg.hMessage) && !GET_REF(pDef->descriptionLongMesg.hMessage))
		{
			ErrorFilenamef(pDef->pchFile,"Microtransaction %s: Invalid or non-existent long description message [%s]",
				pDef->pchName,
				REF_STRING_FROM_HANDLE(pDef->descriptionLongMesg.hMessage));
			iValid = 0;
		}
	}

	if(!pDef->pchProductName)
	{
		ErrorFilenamef(pDef->pchFile,"Microtransaction %s: Account Server Name must be specified.",
			pDef->pchName);
		iValid = 0;
	}

	if(!pDef->bOldProductName)
	{
		if(!pDef->pchProductIdentifier)
		{
			ErrorFilenameGroupRetroactivef(pDef->pchFile, "Design", 14, 2,24,2011, "Microtransaction %s: Product Suffix (Identifier) must be specified.",
				pDef->pchName);
			iValid = 0;
		}
		else if(strchr(pDef->pchProductIdentifier, '-'))
		{
			ErrorFilenameGroupRetroactivef(pDef->pchFile, "Design", 14, 2,24,2011, "Microtransaction %s: ** Invalid Product Suffix (Identifier) ** The Suffix cannot contain any dashes [%s].",
				pDef->pchName,
				pDef->pchProductIdentifier);
			iValid = 0;
		}
	}

	if(!pDef->pchIconSmall)
	{
		ErrorFilenamef(pDef->pchFile,"Microtransaction %s: Small Icon must be specified.",
			pDef->pchName);
		iValid = 0;
	}

	for(i=eaSize(&pDef->eaParts)-1; i>=0; i--)
	{
		iValid &= microtranspart_Validate(pDef, pDef->eaParts[i]);
	}

	return iValid;
}

static bool microtransdef_ValidateRefs(MicroTransactionDef *pDef)
{
	S32 bValid = 1;
	MicroTransactionDef **ppCircularTest = NULL;
	//int i;

	if(REF_STRING_FROM_HANDLE(pDef->hRequiredPurchase) && !GET_REF(pDef->hRequiredPurchase))
	{
		ErrorFilenamef(pDef->pchFile,"Microtransaction %s: Invalid! The required purchase is set but invalid [%s]",
			pDef->pchName,
			REF_STRING_FROM_HANDLE(pDef->hRequiredPurchase));
		bValid = 0;
	}

	eaDestroy(&ppCircularTest);

	return !!bValid;
}

static ExprContext *s_ExprContextCanBuy;
static ExprContext *s_ExprContextCanBuyNoEnt;

void microtrans_BuyContextSetup(int iPartitionIdx, Entity *pPlayerEnt, const GameAccountData *pData, bool bNoEnt)
{
	if (bNoEnt) {
		exprContextSetGAD(s_ExprContextCanBuyNoEnt, pData);
	} else {
		if (pPlayerEnt) {
			exprContextSetSelfPtr(s_ExprContextCanBuy, pPlayerEnt);
			exprContextSetPartition(s_ExprContextCanBuy, iPartitionIdx);
		} else {
			exprContextClearSelfPtrAndPartition(s_ExprContextCanBuy);
		}

		// set game account data
		exprContextSetGAD(s_ExprContextCanBuy, pData);

		exprContextSetPointerVar(s_ExprContextCanBuy, "Player", pPlayerEnt, parse_Entity, true, false);
	}
}

ExprContext *microtrans_GetBuyContext(bool bNoEnt)
{
	return bNoEnt ? s_ExprContextCanBuyNoEnt : s_ExprContextCanBuy;
}

static ExprFuncTable * microtrans_CanBuyExprFuncTable(bool bNoEnt)
{
	static const char * sachCommonTags[] = { "util" };
	static ExprFuncTable * stFuncs = NULL;
	static ExprFuncTable * stFuncsNoEnt = NULL;
	S32 i;

	if (!stFuncs) {
		stFuncs = exprContextCreateFunctionTable();
		for (i = 0; i < ARRAY_SIZE(sachCommonTags); i++) {
			exprContextAddFuncsToTableByTag(stFuncs, sachCommonTags[i]);
		}
		exprContextAddFuncsToTableByTag(stFuncs, "player");
	}

	if (!stFuncsNoEnt) {
		stFuncsNoEnt = exprContextCreateFunctionTable();
		for (i = 0; i < ARRAY_SIZE(sachCommonTags); i++) {
			exprContextAddFuncsToTableByTag(stFuncsNoEnt, sachCommonTags[i]);
		}
	}

	return bNoEnt ? stFuncsNoEnt : stFuncs;
}

static void microtrans_InitContextCanBuy(void)
{
	if(!s_ExprContextCanBuy)
	{
		s_ExprContextCanBuy = exprContextCreate();
		exprContextSetFuncTable(s_ExprContextCanBuy, microtrans_CanBuyExprFuncTable(false));
		exprContextSetAllowRuntimeSelfPtr(s_ExprContextCanBuy);
		exprContextSetAllowRuntimePartition(s_ExprContextCanBuy);

		s_ExprContextCanBuyNoEnt = exprContextCreate();
		exprContextSetFuncTable(s_ExprContextCanBuyNoEnt, microtrans_CanBuyExprFuncTable(true));
	}
}

static void microtransdef_Generate(MicroTransactionDef *pDef)
{
	S32 i, s;
	for(i = eaSize(&pDef->ppchCategories)-1; i>= 0; i--)
	{
		MicroTransactionCategory *pCategory = RefSystem_ReferentFromString(g_hMicroTransCategoryDict,pDef->ppchCategories[i]);
		if(pCategory)
		{
			if(IsServer() && REF_STRING_FROM_HANDLE(pCategory->hAllegiance) && GET_REF(pCategory->hAllegiance))
			{
				pDef->bAllegianceRestriction = true;
			}
		}
	}
	
	if(pDef->pExprCanBuy)
	{
		// safe to do this again as it checks to see if already initialized
		microtrans_InitContextCanBuy();
		
		microtrans_BuyContextSetup(0, NULL, NULL, false);
		
		//Once the generate succeeds
		if(exprGenerate(pDef->pExprCanBuy, s_ExprContextCanBuy))
		{
			//Then set the "buy expr requires entity" flag
			bool bRequired = false;
			
			s=beaSize(&pDef->pExprCanBuy->postfixEArray);

			for(i=0; i<s; i++)
			{
				MultiVal *pVal = &pDef->pExprCanBuy->postfixEArray[i];
				if(pVal->type==MULTIOP_PAREN_OPEN || pVal->type==MULTIOP_PAREN_CLOSE)
					continue;
				if(pVal->type==MULTIOP_FUNCTIONCALL)
				{
					ExprFuncDesc* pFuncDesc = NULL;
					int j;

					stashAddressFindPointer(globalFuncTable, allocFindString(pVal->str), &pFuncDesc);

					if(!pFuncDesc)
						continue;

					for(j=0; j<ARRAY_SIZE(pFuncDesc->tags) && pFuncDesc->tags[j].str != NULL;j++)
					{
						bRequired |= 
							(exprTagUsingSelfPtr(pFuncDesc->tags[j].str) 
							 || exprTagUsingPartition(pFuncDesc->tags[j].str));
					}
				}
			}

			pDef->bBuyExprRequiresEntity = bRequired;
		}
	}
}

static int microtransdef_ValidateCB(enumResourceValidateType eType, const char *pDictName, const char *pResourceName, MicroTransactionDef *pDef, U32 userID)
{
	switch(eType)
	{
		case RESVALIDATE_POST_TEXT_READING:
		{
			microtransdef_Generate(pDef);
			return VALIDATE_HANDLED;
		}
		case RESVALIDATE_POST_BINNING:
		{
			microtransdef_Validate(pDef);
			return VALIDATE_HANDLED;
		}
		case RESVALIDATE_FIX_FILENAME:
		{
			resFixPooledFilename(&pDef->pchFile, MICROTRANSACTIONS_BASE_DIR, pDef->pchScope, pDef->pchName, MICROTRANSACTIONS_EXTENSION);
			return VALIDATE_HANDLED;
		}
		case RESVALIDATE_CHECK_REFERENCES:
		{
			microtransdef_ValidateRefs(pDef);
			return VALIDATE_NOT_HANDLED;
		}
	}

	return VALIDATE_NOT_HANDLED;
}

static void MicroTransactionCategories_Validate(void)
{
	MicroTransactionCategory *pCategory = NULL;
	MicroTransactionCategory *pBonusCategory = NULL;
	RefDictIterator iter;

	RefSystem_InitRefDictIterator(g_hMicroTransCategoryDict, &iter);

	while(pCategory = (MicroTransactionCategory*)RefSystem_GetNextReferentFromIterator(&iter))
	{
		if(pCategory->eType == kMTCategory_Main)
		{
			if(g_pMainMTCategory)
			{
				Errorf("More than one \"Main\" type MicroTransaction category.  [%s] previous [%s] current",
					g_pMainMTCategory->pchName,
					pCategory->pchName);
			}

			g_pMainMTCategory = pCategory;
		}
		else if(pCategory->eType == kMTCategory_Bonus && !pCategory->pchParentCategory)
		{
			if(pBonusCategory)
			{
				Errorf("More than one top-level \"Bonus\" type MicroTransaction category.  [%s] previous [%s] current",
					pBonusCategory->pchName,
					pCategory->pchName);
			}

			pBonusCategory = pCategory;
		}
		else if(pCategory->eType == kMTCategory_Featured && !pCategory->pchParentCategory)
		{
			if(pCategory->iSortIndex)
			{
				Errorf("Top-level featured category [%s] has a sort value [%d], please remove it!",
					pCategory->pchName,
					pCategory->iSortIndex);
				pCategory->iSortIndex = 0;
			}
		}

		if(pCategory->pchParentCategory )
		{
			MicroTransactionCategory *pParent = RefSystem_ReferentFromString(g_hMicroTransCategoryDict, pCategory->pchParentCategory);
			if(!pParent)
			{
				Errorf("MicroTransactionCategory [%s] has an invalid or non-existent parent category [%s]",
					pCategory->pchName,
					pCategory->pchParentCategory);
			}
			else if(pParent->pchParentCategory)
			{
				Errorf("MicroTransactionCategory [%s] has a parent [%s] that also has a parent [%s]!  This is in-correct.  Categories can only go 1 deep.",
					pCategory->pchName,
					pCategory->pchParentCategory,
					pParent->pchParentCategory);
			}
			else if (pParent->eType == kMTCategory_Hidden)
			{
				Errorf("MicroTransactionCategory [%s] has a parent [%s] that is hidden!  Hidden categories cannot be a parent.",
					pCategory->pchName,
					pCategory->pchParentCategory);
			}
		}
	}
}

static void MicroTransactionCategories_ReloadCB(const char *pchRelPath, int UNUSED_when)
{
	loadstart_printf("Reloading Microtransaction Categories...");

	fileWaitForExclusiveAccess(pchRelPath);
	errorLogFileIsBeingReloaded(pchRelPath);

	ParserReloadFileToDictionaryWithFlags(pchRelPath, g_hMicroTransCategoryDict, PARSER_OPTIONALFLAG);
	
	g_pMainMTCategory = NULL;
	MicroTransactionCategories_Validate();

	loadend_printf(" done (%d Microtransaction Categories)", RefSystem_GetDictionaryNumberOfReferents(g_hMicroTransCategoryDict));
}

static void MicroTransactionCategories_Load(void)
{
	loadstart_printf("Loading Microtransaction Categories...");
	
	resLoadResourcesFromDisk(g_hMicroTransCategoryDict, NULL, "defs/config/MTCategories.def", NULL, PARSER_OPTIONALFLAG);
	
	MicroTransactionCategories_Validate();

	if (isDevelopmentMode())
	{
		FolderCacheSetCallback(FOLDER_CACHE_CALLBACK_UPDATE|FOLDER_CACHE_CALLBACK_DELETE, "defs/config/MTCategories.def", MicroTransactionCategories_ReloadCB);
	}
	loadend_printf(" done (%d Microtransaction Categories)", RefSystem_GetDictionaryNumberOfReferents(g_hMicroTransCategoryDict));
}

AUTO_STARTUP(MicroTransactions) ASTRT_DEPS(AS_Messages, Items, EntityCostumes, Powers, Allegiance, Species, GamePermissions, MicroTransactionConfig);
void MicroTransactions_Load(void)
{
	// init the context for CanBuy
	microtrans_InitContextCanBuy();
	
	//Load the categories
	MicroTransactionCategories_Load();

	if(IsServer())
	{
		loadstart_printf("Loading Microtransactions...");
		resLoadResourcesFromDisk(g_hMicroTransDefDict, MICROTRANSACTIONS_BASE_DIR, ".microtrans", NULL, PARSER_OPTIONALFLAG | RESOURCELOAD_SHAREDMEMORY );

		loadend_printf(" done (%d Microtransactions)", RefSystem_GetDictionaryNumberOfReferents(g_hMicroTransDefDict));
	}
}

AUTO_RUN;
void MicroTransactionsDefDictionary_Register(void)
{
	g_hMicroTransDefDict = RefSystem_RegisterSelfDefiningDictionary("MicroTransactionDef", false, parse_MicroTransactionDef, true, true, NULL);
	resDictManageValidation(g_hMicroTransDefDict, microtransdef_ValidateCB);

	resDictSetDisplayName(g_hMicroTransDefDict, "MicroTransaction", "MicroTransactions", RESCATEGORY_DESIGN);

	g_hMicroTransCategoryDict = RefSystem_RegisterSelfDefiningDictionary("MicroTransactionCategory", false, parse_MicroTransactionCategory, true, true, NULL);

	if (IsServer())
	{
		resDictProvideMissingResources(g_hMicroTransDefDict);
		if (isDevelopmentMode() || isProductionEditMode()) {
			resDictMaintainInfoIndex(g_hMicroTransDefDict, ".displayNameMesg.Message", ".Scope", NULL, NULL, ".SmallIcon");
		}
	}
	else
	{
		// Client loading from the server
		resDictRequestMissingResources(g_hMicroTransDefDict, RES_DICT_KEEP_ALL, false, resClientRequestSendReferentCommand);
	}
}

MicroTransactionCategory *microtrans_CategoryFromStr(const char *pchCategory)
{
	char *pchName = NULL;
	MicroTransactionCategory *pCategory = NULL;
	
	if(!pchCategory)
		return NULL;
	
	PERFINFO_AUTO_START_FUNC();
	pchName = strchr(pchCategory, '.');

	if(pchName && pchName[0])
	{
		pchName++;
		if(!pchName || !pchName[0])
			pchName = NULL;
	}
	
	pCategory = RefSystem_ReferentFromString(g_hMicroTransCategoryDict, ((pchName && pchName[0]) ? (pchName) : (pchCategory)));
	PERFINFO_AUTO_STOP();

	return pCategory;
}

MicroTransactionDef *microtrans_findDefFromAPProd(const AccountProxyProduct *pProduct)
{
	MicroTransactionDef *pDef = NULL;
	RefDictIterator iter;
	static StashTable sMicrotransLookupTable = NULL;

	if(!pProduct)
		return pDef;

	PERFINFO_AUTO_START_FUNC();
	if (sMicrotransLookupTable)
	{
		stashFindPointer(sMicrotransLookupTable, pProduct->pName, &pDef);
	}

	if (!pDef)
	{
		PERFINFO_AUTO_START("Cache miss", 1);
		RefSystem_InitRefDictIterator(g_hMicroTransDefDict, &iter);
		while (pDef = (MicroTransactionDef*)RefSystem_GetNextReferentFromIterator(&iter))
		{
			if (!stricmp_safe(pDef->pchProductName, pProduct->pName))
				break;
			if (pDef->pProductConfig && !stricmp_safe(pDef->pProductConfig->pchName, pProduct->pName))
				break;
			if (pDef->pReclaimProductConfig && !stricmp_safe(pDef->pReclaimProductConfig->pchName, pProduct->pName))
				break;
		}

		if (isProductionMode())
		{
			if (!sMicrotransLookupTable)
				sMicrotransLookupTable = stashTableCreateWithStringKeys(256, StashDeepCopyKeys);

			stashAddPointer(sMicrotransLookupTable, pProduct->pName, pDef, true);
		}
		PERFINFO_AUTO_STOP();
	}
	PERFINFO_AUTO_STOP();

	return pDef;
}

static const char *microtrans_GetMTGameTitle()
{
	const char *pchShortProduct = GetShortProductName();
	if(!stricmp(pchShortProduct, "FC"))
		return "CO";
	else if(!stricmp(pchShortProduct, "ST"))
		return "STO";
	else if(!stricmp(pchShortProduct, "NW"))
		return "NW";
	else if(!stricmp(pchShortProduct, "BA"))
		return "BA";
	else if(!stricmp(pchShortProduct, "CN"))
		return "CN";
	else
		return "UNK";
}

static const char *microtrans_ASProductCategoryToStr(ProductCategory eCategory)
{
	switch(eCategory)
	{
		case kProductCategory_ActionFigure:
			return "AF";
			break;
		case kProductCategory_AdventurePack:
			return "AP";
			break;
		case kProductCategory_Archetype:
			return "AT";
			break;
		case kProductCategory_BridgePack:
			return "BRG";
			break;
		case kProductCategory_CostumePack:
			return "CP";
			break;
		case kProductCategory_EmotePack:
			return "EM";
			break;
		case kProductCategory_EmblemPack:
			return "EP";
			break;
		case kProductCategory_FunctionalItem:
			return "FI";
			break;
		case kProductCategory_Hideout:
			return "HO";
			break;
		case kProductCategory_Item:
			return "IT";
			break;
		case kProductCategory_Power:
			return "PO";
			break;
		case kProductCategory_Promo:
			return "PR";
			break;
		case kProductCategory_PlayableSpecies:
			return "PS";
			break;
		case kProductCategory_Pet:
			return "PT";
			break;
		case kProductCategory_Ship:
			return "S";
			break;
		case kProductCategory_ShipCostume:
			return "SC";
			break;
		case kProductCategory_Service:
			return "SV";
			break;
		case kProductCategory_Title:
			return "TI";
			break;
		case kProductCategory_Token:
			return "TK";
			break;
		case kProductCategory_Healing:
			return "HEA";
			break;
		case kProductCategory_Buff:
			return "BUF";
			break;
		case kProductCategory_ResurrectionScroll:
			return "RES";
			break;
		case kProductCategory_Identification:
			return "IDN";
			break;
		case kProductCategory_Booster:
			return "BST";
			break;
		case kProductCategory_Skillcraft:
			return "SKC";
			break;
		case kProductCategory_Skin:
			return "SKN";
			break;
		case kProductCategory_Inscription:
			return "INS";
			break;
		case kProductCategory_CompanionBuff:
			return "CBF";
			break;
		case kProductCategory_Mount:
			return "MNT";
			break;
		case kProductCategory_CraftingTier1:
			return "CT1";
			break;
		case kProductCategory_CraftingTier2:
			return "CT2";
			break;
		case kProductCategory_CraftingTier3:
			return "CT3";
			break;
		case kProductCategory_Enchanting:
			return "ENC";
			break;
		case kProductCategory_Bag:
			return "BAG";
			break;
		case kProductCategory_Dye:
			return "DYE";
			break;
		default:
			return "UNK";
			break;
	}
}

// This actually generates two product names now
void microtrans_GenerateProductName(MicroTransactionDef *pDef)
{
	char *estrBuffer = NULL;
	estrStackCreate(&estrBuffer);

	StructFreeStringSafe(&pDef->pchProductName);
	StructFreeStringSafe(&pDef->pProductConfig->pchName);
	if (pDef->pReclaimProductConfig)
		StructFreeStringSafe(&pDef->pReclaimProductConfig->pchName);

	estrPrintf(&estrBuffer, "PRD-%s-%s-%s%s%s-%s",
		microtrans_GetMTGameTitle(),
		pDef->bPromoProduct ? "P" : "M",
		microtrans_ASProductCategoryToStr(pDef->eCategory),
		pDef->bIsF2PDuplicate ? "-F2P" : "",
		pDef->bBuyProduct || pDef->bGenerateReclaimProduct ? "-BUY" : "",
		pDef->pchProductIdentifier);
	pDef->pchProductName = StructAllocString(estrBuffer);
	pDef->pProductConfig->pchName = StructAllocString(estrBuffer);

	if (pDef->pReclaimProductConfig)
	{
		estrPrintf(&estrBuffer, "PRD-%s-%s-%s%s-%s",
			microtrans_GetMTGameTitle(),
			pDef->bPromoProduct ? "P" : "M",
			microtrans_ASProductCategoryToStr(pDef->eCategory),
			pDef->bIsF2PDuplicate ? "-F2P" : "",
			pDef->pchProductIdentifier);
		pDef->pReclaimProductConfig->pchName = StructAllocString(estrBuffer);
	}

	estrDestroy(&estrBuffer);
}

static const char *microtrans_GetSpecialPartKeyValueName(MicroTransactionPart *pPart)
{
	switch(pPart->eSpecialPartType)
	{
	case kSpecialPartType_SharedBankSize:
		return MicroTrans_GetSharedBankSlotASKey();
		break;
	case kSpecialPartType_CharSlots:
		return MicroTrans_GetCharSlotsASKey();
		break;
	case kSpecialPartType_SuperPremium:
		return MicroTrans_GetSuperPremiumASKey();
		break;
	case kSpecialPartType_CostumeSlots:
		return MicroTrans_GetCostumeSlotsASKey();
		break;
	case kSpecialPartType_Respec:
		return MicroTrans_GetRespecTokensASKey();
		break;
	case kSpecialPartType_Rename:
		return MicroTrans_GetRenameTokensASKey();
		break;
	case kSpecialPartType_Retrain:
		return MicroTrans_GetRetrainTokensASKey();
		break;
	case kSpecialPartType_OfficerSlots:
		return MicroTrans_GetOfficerSlotsASKey();
		break;
	case kSpecialPartType_CostumeChange:
		return MicroTrans_GetFreeCostumeChangeASKey();
		break;
	case kSpecialPartType_ShipCostumeChange:
		return MicroTrans_GetFreeShipCostumeChangeASKey();
		break;
	case kSpecialPartType_ItemAssignmentCompleteNow:
		return MicroTrans_GetItemAssignmentCompleteNowASKey();
		break;
	case kSpecialPartType_ItemAssignmentUnslotItem:
		return MicroTrans_GetItemAssignmentUnslotTokensASKey();
		break;
	default:
		return NULL;
		break;
	}
}

static void microtrans_PopulateSpecialPartKeyValueChanges(MicroTransactionDef *pDef, char ***peaKeyValueChanges)
{
	EARRAY_FOREACH_BEGIN(pDef->eaParts, iPart);
	{
		MicroTransactionPart *pPart = pDef->eaParts[iPart];
		const char *pSpecialKeyName = NULL;
		char *pSpecialKeyChange = NULL;

		if (pPart->ePartType != kMicroPart_Special)
			continue;

		if (!(pSpecialKeyName = microtrans_GetSpecialPartKeyValueName(pPart)))
			continue;

		estrPrintf(&pSpecialKeyChange, "%s += %d", pSpecialKeyName, pPart->iCount);
		eaPush(peaKeyValueChanges, pSpecialKeyChange);
	}
	EARRAY_FOREACH_END;
}

static void microtrans_KeyValueChangeArrayToString(char **eaKeyValueChanges, char **ppKeyValueChangeString)
{
	estrClear(ppKeyValueChangeString);

	if (eaSize(&eaKeyValueChanges) > 1)
		estrConcatChar(ppKeyValueChangeString, '"');

	EARRAY_FOREACH_BEGIN(eaKeyValueChanges, iChange);
	{
		if (iChange > 0)
			estrAppend2(ppKeyValueChangeString, ", ");
		estrAppend2(ppKeyValueChangeString, eaKeyValueChanges[iChange]);
	}
	EARRAY_FOREACH_END;

	if (eaSize(&eaKeyValueChanges) > 1)
		estrConcatChar(ppKeyValueChangeString, '"');
}

void microtrans_GenerateProductConfigs(MicroTransactionDef *pDef)
{
	char *estrKey = NULL;
	char **eaKeyValueChanges = NULL;
	char *pKeyValueChangeBuffer = NULL;

	if (!pDef->pProductConfig)
	{
		pDef->pProductConfig = StructCreate(parse_MicroTransactionAccountServerConfig);
	}

	if (pDef->bGenerateReclaimProduct && !pDef->pReclaimProductConfig)
	{
		pDef->pReclaimProductConfig = StructCreate(parse_MicroTransactionAccountServerConfig);
		pDef->pReclaimProductConfig->uiOverridePrice = 0;
	}
	else if (!pDef->bGenerateReclaimProduct && pDef->pReclaimProductConfig)
	{
		StructDestroySafe(parse_MicroTransactionAccountServerConfig, &pDef->pReclaimProductConfig);
	}

	pDef->pProductConfig->uiOverridePrice = pDef->uiPrice;
	microtrans_GenerateProductName(pDef);

	StructFreeStringSafe(&pDef->pProductConfig->pchPrerequisites);
	StructFreeStringSafe(&pDef->pProductConfig->pchKeyValueChanges);

	if (pDef->bGenerateReclaimProduct)
	{
		StructFreeStringSafe(&pDef->pReclaimProductConfig->pchPrerequisites);
		StructFreeStringSafe(&pDef->pReclaimProductConfig->pchKeyValueChanges);
	}

	estrStackCreate(&estrKey);

	if (!pDef->bBuyForAllShards)
	{
		if (gConf.bDontAllowGADModification)
			estrCopy2(&estrKey, "$ENV:");
		else
			estrCopy2(&estrKey, "$PROXY:");
	}

	if (gConf.bDontAllowGADModification)
		microtrans_PopulateSpecialPartKeyValueChanges(pDef, &eaKeyValueChanges);

	if (pDef->bGenerateReclaimProduct)
		estrAppend2(&estrKey, pDef->pReclaimProductConfig->pchName);
	else
		estrAppend2(&estrKey, pDef->pProductConfig->pchName);

	// If we're generating a reclaim product, we need the following prereqs/KV changes:
	//		Buy:
	//			Prereqs: [key] == 0
	//			Changes: [key] = 1
	//		Reclaim:
	//			Prereqs: [key] == 1
	// If we're generating a one-per-account product and gConf.bDontAllowGADModification is on, we need:
	//		Buy:
	//			Prereqs: [key] == 0
	//			Changes: [key] = 1
	// We take advantage of the similarities here
	if (pDef->bGenerateReclaimProduct)
	{
		char *estrBuffer = NULL;
		estrStackCreate(&estrBuffer);

		estrPrintf(&estrBuffer, "%s == 1", estrKey);
		pDef->pReclaimProductConfig->pchPrerequisites = StructAllocString(estrBuffer);

		if (eaSize(&eaKeyValueChanges))
		{
			microtrans_KeyValueChangeArrayToString(eaKeyValueChanges, &estrBuffer);
			pDef->pReclaimProductConfig->pchKeyValueChanges = StructAllocString(estrBuffer);
		}

		estrDestroy(&estrBuffer);
	}

	if (pDef->bGenerateReclaimProduct || (pDef->bOnePerAccount && gConf.bDontAllowGADModification))
	{
		char *estrBuffer = NULL;
		estrStackCreate(&estrBuffer);

		estrPrintf(&estrBuffer, "%s == 0", estrKey);
		pDef->pProductConfig->pchPrerequisites = StructAllocString(estrBuffer);

		estrPrintf(&estrBuffer, "%s = 1", estrKey);
		eaPush(&eaKeyValueChanges, estrDupFromEString(&estrBuffer));
		
		estrDestroy(&estrBuffer);
	}

	if (eaSize(&eaKeyValueChanges))
	{
		char *estrBuffer = NULL;
		microtrans_KeyValueChangeArrayToString(eaKeyValueChanges, &estrBuffer);
		pDef->pProductConfig->pchKeyValueChanges = StructAllocString(estrBuffer);
		estrDestroy(&estrBuffer);
	}

	eaDestroyEString(&eaKeyValueChanges);

	estrDestroy(&estrKey);
}

static bool microtrans_AllegianceCheck(Entity *pEnt, MicroTransactionDef *pDef)
{
	S32 iIdx;
	bool bFailure = false;
	AllegianceDef *pPlayerAllegiance = pEnt ? GET_REF(pEnt->hAllegiance) : NULL;
	AllegianceDef *pPlayerSubAllegiance = pEnt ? GET_REF(pEnt->hSubAllegiance) : NULL;
	if(pDef && pDef->bAllegianceRestriction)
	{
		for(iIdx = eaSize(&pDef->ppchCategories)-1; iIdx >=0; iIdx--)
		{
			MicroTransactionCategory *pCategory = RefSystem_ReferentFromString(g_hMicroTransCategoryDict, pDef->ppchCategories[iIdx]);
			if(pCategory && IS_HANDLE_ACTIVE(pCategory->hAllegiance))
			{
				AllegianceDef *pReqAllegiance = GET_REF(pCategory->hAllegiance);
				if(pReqAllegiance && pPlayerAllegiance != pReqAllegiance && pPlayerSubAllegiance != pReqAllegiance)
				{
					bFailure = true;
					break;
				}
			}
		}
	}
	return bFailure;
}

static bool microtrans_MainInvFullCheck(Entity *pEnt, ItemDef *pItemDef, int iCount, bool bAllowOverflowBag)
{
	GameAccountDataExtract *pExtract = entity_GetCachedGameAccountDataExtract(pEnt);
	InventoryBag *pInvBag = (InventoryBag *)inv_GetBag(CONTAINER_NOCONST(Entity, pEnt), InvBagIDs_Inventory, pExtract);
	if (!inv_CanItemDefFitInBag(pEnt, pInvBag, pItemDef, iCount))
	{
		InventoryBag** eaBags = NULL;
		InventoryBag* pPlayerBagsBag =  (InventoryBag*)inv_GetBag(CONTAINER_NOCONST(Entity, pEnt), InvBagIDs_PlayerBags, pExtract);
		S32 i, j, iRemainingStackCount = iCount;

		// Add the inventory bag and player bags to the list of bags to check
		if (pPlayerBagsBag)
		{
			for (i = 0; i < invbag_maxslots(pEnt, pPlayerBagsBag); i++)
			{
				InventoryBag* pBag = inv_PlayerBagFromSlotIdx(pEnt, i);
				if (pBag && GamePermissions_trh_CanAccessBag(CONTAINER_NOCONST(Entity, pEnt), pBag->BagID, pExtract))
				{
					eaPush(&eaBags, pBag);
				}
			}
		}
		// If requested, add the overflow bag to the list of bags
		if (bAllowOverflowBag)
		{
			InventoryBag *pOverflowBag = (InventoryBag *)inv_GetBag(CONTAINER_NOCONST(Entity, pEnt), InvBagIDs_Overflow, pExtract);
			eaPush(&eaBags, pOverflowBag);
		}
		// If the player doesn't have any extra bag space, exit early
		if (!eaSize(&eaBags))
		{
			return false;
		}
		// TODO: Ideally the overflow bag would be placed after the main inventory bag
		eaPush(&eaBags, pInvBag);

		// Check all bags in eaBags for room
		for (i = 0; i < eaSize(&eaBags); i++) 
		{
			InventoryBag* pBag = eaBags[i];
			for (j = 0; j < invbag_maxslots(pEnt, pBag) && iRemainingStackCount > 0; j++)
			{
				InventorySlot* pSlot = inv_GetSlotPtr(pBag, j);
				if (!pSlot)
				{
					iRemainingStackCount -= pItemDef->iStackLimit;
				}
				else if (!pSlot->pItem || 
					inv_MatchingItemInSlot(pBag, j, pItemDef->pchName))
				{
					S32 iSlotCount = inv_bag_GetSlotItemCount(pBag, j);
					S32 iFreeCount = pItemDef->iStackLimit - iSlotCount;
					S32 iNumToAdd = MIN(iRemainingStackCount, iFreeCount);

					if (iNumToAdd > 0)
					{
						iRemainingStackCount -= iNumToAdd;
					}
				}
			}
		}
		eaDestroy(&eaBags);
		return iRemainingStackCount <= 0;
	}
	return true;
}

static bool microtrans_ItemCheck(int iPartitionIdx, Entity *pEnt, MicroTransactionDef *pDef, MicroPurchaseErrorType* peError)
{
	if(!pEnt)
	{
		if(!pDef)
		{
			return false;
		}
		else
		{
			bool bFound = false;
			S32 iIdx;
			for(iIdx = eaSize(&pDef->eaParts)-1; iIdx >= 0; iIdx--)
			{
				MicroTransactionPart *pPart = pDef->eaParts[iIdx];
				switch(pPart->ePartType)
				{
					case kMicroPart_Item:
						bFound = true;
						break;
				}
			}

			if (bFound)
			{
				(*peError) = kMicroPurchaseErrorType_InvalidTransaction;
			}

			//If there is an item in this transaction, you cannot give it to a null entity
			return bFound;
		}
	}

	if(pDef)
	{
		GameAccountDataExtract *pExtract = NULL;
		S32 iIdx;
		for(iIdx = eaSize(&pDef->eaParts)-1; iIdx >= 0; iIdx--)
		{
			MicroTransactionPart *pPart = pDef->eaParts[iIdx];
			switch(pPart->ePartType)
			{
				xcase kMicroPart_Item:
				{
					ItemDef *pItemDef = GET_REF(pPart->hItemDef);
					InvBagIDs eBagID = InvBagIDs_Inventory;

					//Need to track "best bag" even if the microtrans def doesn't claim to care,
					// just in case this is a lite item and a designer forgot to use bAddToBestBag.
					InvBagIDs eBestBagID = InvBagIDs_None;

					if (!pItemDef)
					{
						(*peError) = kMicroPurchaseErrorType_ItemDoesNotExist;
						return true;
					}

					if (!pExtract) 
					{
						pExtract = entity_GetCachedGameAccountDataExtract(pEnt);
					}

					eBestBagID = GetBestBagForItemDef(pEnt, pItemDef, pPart->iCount, true, pExtract);
					
					if(pItemDef->flags & kItemDefFlag_EquipOnPickup)
					{
						eBagID = GetBestBagForItemDef(pEnt, pItemDef, pPart->iCount, false, pExtract);
					}
					else if (pPart->bAddToBestBag)
					{
						eBagID = eBestBagID;
					}

					if(!pPart->bIgnoreUsageRestrictions && !itemdef_VerifyUsageRestrictions(iPartitionIdx, pEnt, pItemDef, 0, NULL, -1))
					{
						(*peError) = kMicroPurchaseErrorType_UsageRestrictions;
						return true;
					}
					else if(pItemDef && inv_ent_HasUniqueItem(pEnt, pItemDef->pchName))
					{
						//Error on duplicate unique items (includes itemlites)
						(*peError) = kMicroPurchaseErrorType_Unique;
						return true;
					}
					else if (pItemDef && pItemDef->eType == kItemType_Numeric && (pItemDef->flags & kItemDefFlag_TransFailonHighLimit) && inv_GetNumericItemValue(pEnt, pItemDef->pchName) + pPart->iCount > pItemDef->MaxNumericValue)
					{
						(*peError) = kMicroPurchaseErrorType_NumericFull;
						return true;
					}
					else if (!inv_GetLiteBag(pEnt, eBestBagID, pExtract))
					{
						if (eBagID == InvBagIDs_Inventory)
						{
							if(!microtrans_MainInvFullCheck(pEnt, pItemDef, pPart->iCount, pPart->bAllowOverflowBag))
							{
								(*peError) = kMicroPurchaseErrorType_InventoryBagFull;
								return true;
							}
						}
						else 
						{
							NOCONST(InventoryBag) *pBag = inv_GetBag(CONTAINER_NOCONST(Entity, pEnt), eBagID, pExtract);
							if(!inv_CanItemDefFitInBag(pEnt, CONTAINER_RECONST(InventoryBag,pBag), pItemDef, pPart->iCount))
							{
								(*peError) = kMicroPurchaseErrorType_InventoryBagFull;
								return true;
							}
						}
					}
				}
			}
		}
	}
	return false;
}

bool microtrans_AlreadyPurchased(Entity *pEnt, 
										const GameAccountData *pData,
										MicroTransactionDef *pDef)
{
	bool bCannotPurchase = true;
	PlayerMTInfo *pMTInfo = (pEnt && pEnt->pPlayer) ? pEnt->pPlayer->pMicroTransInfo : NULL;

	PERFINFO_AUTO_START_FUNC();
	if(pDef)
	{
		S32 idx;
		if(pDef->bOnePerAccount)
		{
			if(microtrans_HasPurchased(pData, pDef))
			{
				PERFINFO_AUTO_STOP();
				return bCannotPurchase;
			}
		}
		else if(pDef->bOnePerCharacter)
		{
			//If this is a one per character microtransaction, you cannot purchase it without an entity and player
			if(!pEnt || !pEnt->pPlayer)
			{
				PERFINFO_AUTO_STOP();
				return bCannotPurchase;
			}
			
			//Check to see if they've purchased this item or a previous version 1 microtransaction before
			if( pMTInfo 
				&& ( eaFindString((char***)&pMTInfo->eaOneTimePurchases, pDef->pchName) != -1 
					 || ( pDef->pchLegacyItemID //version 1 microtransaction string ItemID
						  && eaFindString((char***)&pMTInfo->eaOneTimePurchases, pDef->pchLegacyItemID) != -1) ) )
			{
				PERFINFO_AUTO_STOP();
				return bCannotPurchase;
			}
		}

		for(idx = eaSize(&pDef->eaParts)-1; bCannotPurchase && idx >= 0; idx--)
		{
			MicroTransactionPart *pPart = pDef->eaParts[idx];
			switch(pPart->ePartType)
			{
			case kMicroPart_Permission:
				PERFINFO_AUTO_START("Permission", 1);
				{
					GamePermissionDef *pPermission = eaIndexedGetUsingString(&g_GamePermissions.eaPermissions,pPart->pchPermission);
					if(pPermission)
					{
						if(microtrans_PermissionCheck(pEnt, pData, pDef))
							bCannotPurchase = false;
					}
				}
				PERFINFO_AUTO_STOP();
				break;
			case kMicroPart_Species:
				PERFINFO_AUTO_START("Species", 1);
				{
					SpeciesDef *pSpecies = GET_REF(pPart->hSpeciesDef);
					if(pSpecies && pSpecies->pcUnlockCode)
					{
						if(!pData || -1 == eaIndexedFindUsingString(&pData->eaKeys, pSpecies->pcUnlockCode))
							bCannotPurchase = false;
					}
				}
				PERFINFO_AUTO_STOP();
				break;
			case kMicroPart_Costume:
				PERFINFO_AUTO_START("Costume", 1);
				{
					ItemDef *pItemDef = GET_REF(pPart->hItemDef);
					S32 iCostumeIdx;
					
					if(!pItemDef)
					{
						PERFINFO_AUTO_STOP();
						break;
					}
					
					for(iCostumeIdx = eaSize(&pItemDef->ppCostumes)-1; iCostumeIdx >= 0; iCostumeIdx-- )
					{
						PlayerCostume *pCostume = GET_REF(pItemDef->ppCostumes[iCostumeIdx]->hCostumeRef);
						if(pCostume)
						{
							char *estrCostumeRef = NULL;
							MicroTrans_FormItemEstr(&estrCostumeRef,
								GetShortProductName(), kMicroItemType_PlayerCostume,
								pCostume->pcName, 1);

							if(!pData || -1 == eaIndexedFindUsingString(&pData->eaCostumeKeys, estrCostumeRef))
							{
								bCannotPurchase = false;
							}

							estrDestroy(&estrCostumeRef);
						}
					}
				}
				PERFINFO_AUTO_STOP();
				break;
			case kMicroPart_CostumeRef:
				PERFINFO_AUTO_START("CostumeRef", 1);
				{
					PlayerCostume *pCostume = GET_REF(pPart->hCostumeDef);
					if(pCostume)
					{
						char *estrCostumeRef = NULL;
						MicroTrans_FormItemEstr(&estrCostumeRef,
							GetShortProductName(), kMicroItemType_PlayerCostume,
							pCostume->pcName, 1);

						if(!pData || -1 == eaIndexedFindUsingString(&pData->eaCostumeKeys, estrCostumeRef))
							bCannotPurchase = false;

						estrDestroy(&estrCostumeRef);
					}
				}
				PERFINFO_AUTO_STOP();
				break;
			case kMicroPart_Attrib:
				PERFINFO_AUTO_START("Attrib", 1);
				{
					AttribValuePairChange *pChange = pPart->pPairChange;
					AttribValuePair *pPair = pData ? eaIndexedGetUsingString(&pData->eaKeys, pChange->pchAttribute) : NULL;
					switch(pChange->eType)
					{
					// Attrib changes that can't fail
					case kAVChangeType_BooleanNoFail:
					case kAVChangeType_IntSetNoFail:
						bCannotPurchase = false;
						break;
						//Boolean change type.  Fails if the key is already set (or already unset depending on pChange->iVal)
					case kAVChangeType_Boolean:
						ADD_MISC_COUNT(1, "Boolean");
						{
							if( !!pChange->iVal && (!pPair || !pPair->pchValue || !atoi(pPair->pchValue)) )
							{
								bCannotPurchase = false;
							}
							else if(!pChange->iVal && pPair && pPair->pchValue && atoi(pPair->pchValue) == 1)
							{
								bCannotPurchase = false;
							}
							else
							{
								PERFINFO_AUTO_STOP();
								PERFINFO_AUTO_STOP();
								return true;
							}
						}
						break;
					case kAVChangeType_IntSet:
						ADD_MISC_COUNT(1, "IntSet");
						{
							if(!pPair || !pPair->pchValue || atoi(pPair->pchValue) != pChange->iVal)
							{
								bCannotPurchase = false;
							}
							else
							{
								PERFINFO_AUTO_STOP();
								PERFINFO_AUTO_STOP();
								return true;
							}
						}
						break;
					case kAVChangeType_String:
						ADD_MISC_COUNT(1, "String");
						{
							if(!pChange->pchStringVal)
							{
								break;
							}
							if(!pPair || !pPair->pchValue || strcmp(pChange->pchStringVal, pPair->pchValue))
							{
								bCannotPurchase = false;
							}
							else
							{
								PERFINFO_AUTO_STOP();
								PERFINFO_AUTO_STOP();
								return true;
							}
						}
						break;
					case kAVChangeType_IntIncrement:
						ADD_MISC_COUNT(1, "IntIncrement");
						{
							int iCurrentValue = (pPair && pPair->pchValue) ? atoi(pPair->pchValue) : 0;
							if(!pChange->bClampValues
								|| ((iCurrentValue + pChange->iVal) < pChange->iMaxVal
								&& (iCurrentValue + pChange->iVal) > pChange->iMinVal) )
							{
								bCannotPurchase = false;
							}
							else
							{
								PERFINFO_AUTO_STOP();
								PERFINFO_AUTO_STOP();
								return true;
							}
						}
						break;
					}
				}
				PERFINFO_AUTO_STOP();
				break;
			case kMicroPart_VanityPet:
				PERFINFO_AUTO_START("VanityPet", 1);
				{
					PowerDef *ppowerDef = GET_REF(pPart->hPowerDef);
					if(ppowerDef)
					{
						if(!pData || -1 == eaFindString((char***)&pData->eaVanityPets, ppowerDef->pchName))
							bCannotPurchase = false;
					}
				}
				PERFINFO_AUTO_STOP();
				break;
			case kMicroPart_Item:
				PERFINFO_AUTO_START("Item", 1);
				{
					//Can always purchase item ones again
					bCannotPurchase = false;
				}
				PERFINFO_AUTO_STOP();
				break;
			case kMicroPart_RewardTable:
				PERFINFO_AUTO_START("RewardTable", 1);
				{
					//Can always purchase reward table ones again
					bCannotPurchase = false;
				}
				PERFINFO_AUTO_STOP();
				break;
			case kMicroPart_Special:
				PERFINFO_AUTO_START("Special", 1);
				{
					//Currently, all specialty parts can be re-purchased
					bCannotPurchase = false;
				}
				PERFINFO_AUTO_STOP();
				break;
			default:
				break;
			}
		}
	}

	PERFINFO_AUTO_STOP();
	return bCannotPurchase;
}

static bool microtrans_PetCheck(Entity* pEnt, MicroTransactionDef *pDef, const GameAccountData *pData, AddSavedPetErrorType* pePetError)
{
	bool bFailure = false;

	if (pDef)
	{
		FOR_EACH_IN_EARRAY(pDef->eaParts, MicroTransactionPart, pPart)
		{
			ItemDef* pItemDef = pPart->ePartType == kMicroPart_Item ? GET_REF(pPart->hItemDef) : NULL;
			if (pItemDef && (pItemDef->flags & kItemDefFlag_EquipOnPickup))
			{
				PetDef* pPetDef = GET_REF(pItemDef->hPetDef);
				if (pPetDef)
				{
					GameAccountDataExtract *pExtract = entity_CreateLocalGameAccountDataExtract(pData);
					if(!Entity_CanAddSavedPet(pEnt,pPetDef,0,pItemDef->bMakeAsPuppet,pExtract,pePetError))
					{
						bFailure = true;
					}
					entity_DestroyLocalGameAccountDataExtract(&pExtract);
				}
			}
			else if (pItemDef)
			{
				PetDef* pPetDef = GET_REF(pItemDef->hPetDef);
				if (pPetDef && Entity_CheckAcquireLimit(pEnt, pPetDef, 0))
				{
					if (pePetError)
					{
						(*pePetError) = kAddSavedPetErrorType_AcquireLimit;
					}
					bFailure = true;
				}
			}
		} FOR_EACH_END;
	}
	return bFailure;
}

bool microtrans_PermissionCheck(Entity *pEnt,
								const GameAccountData *pData, 
								MicroTransactionDef *pDef)
{
	//Check to see if the permission(s) it grants have already all be unlocked through other means
	
	S32 iPartIdx;
	bool bUnlocked = false;

	PERFINFO_AUTO_START_FUNC();

	if(!pData)
		pData = entity_GetGameAccount(pEnt);

	if(!pData)
	{
		PERFINFO_AUTO_STOP();
		return !bUnlocked;
	}

	for(iPartIdx=eaSize(&pDef->eaParts)-1; iPartIdx>=0; iPartIdx--)
	{
		MicroTransactionPart *pPart = pDef->eaParts[iPartIdx];
		int iPermIdx;
		GamePermissionDef *pPermission = NULL;
		if(pPart->ePartType != kMicroPart_Permission)
			continue;

		pPermission = eaIndexedGetUsingString(&g_GamePermissions.eaPermissions,pPart->pchPermission);

		if(!pPermission)
			continue;
		
		bUnlocked = true;
		for(iPermIdx = eaSize(&pPermission->eaTextTokens)-1; iPermIdx>=0; iPermIdx--)
		{
			GameTokenText *pToken = pPermission->eaTextTokens[iPermIdx];
			char *estrBuffer = NULL;
			GenerateGameTokenKey(&estrBuffer,
				pPermission->eaTextTokens[iPermIdx]->eType,
				pPermission->eaTextTokens[iPermIdx]->pchKey,
				pPermission->eaTextTokens[iPermIdx]->pchValue);

			if(-1 == eaIndexedFindUsingString(&pData->eaTokens, estrBuffer))
				bUnlocked = false;

			estrDestroy(&estrBuffer);
		}

		if(!bUnlocked)
		{
			break;
		}
	}

	PERFINFO_AUTO_STOP();
	return !bUnlocked;
}

static bool microtrans_SpecialCheck(int iPartitionIdx, Entity *pEnt, MicroTransactionDef *pDef, MicroPurchaseErrorType* peError)
{
	GameAccountDataExtract *pExtract = entity_GetCachedGameAccountDataExtract(pEnt);
	S32 idx;

	for (idx = eaSize(&pDef->eaParts) - 1; idx >= 0; idx--)
	{
		MicroTransactionPart *pPart = pDef->eaParts[idx];

		if (pPart->ePartType != kMicroPart_Special)
		{
			continue;
		}

		switch(pPart->eSpecialPartType)
		{
		case kSpecialPartType_BankSize:
			{
				if((pPart->iCount > 0) && NONNULL(pEnt))
				{
					ItemDef *pItemDef = RefSystem_ReferentFromString(g_hItemDict, "BankSizeMicrotrans");
					if (pItemDef && (pItemDef->flags & kItemDefFlag_TransFailonHighLimit) && inv_GetNumericItemValue(pEnt, pItemDef->pchName) + pPart->iCount > pItemDef->MaxNumericValue)
					{
						(*peError) = kMicroPurchaseErrorType_NumericFull;
						return true;
					}
				}
				break;
			}
		case kSpecialPartType_SharedBankSize:
			{
				if(pExtract && !gConf.bDontAllowGADModification && !slGAD_CanChangeAttribClampedExtract(pExtract, MicroTrans_GetSharedBankSlotGADKey(), pPart->iCount, 0, 100000))
				{
					(*peError) = kMicroPurchaseErrorType_AttribClamped;
					return true;
				}
				break;
			}
		case kSpecialPartType_InventorySize:
			{
				if((pPart->iCount > 0) && NONNULL(pEnt))
				{
					ItemDef *pItemDef = RefSystem_ReferentFromString(g_hItemDict, "AddInvSlotsMicrotrans");
					if (pItemDef && (pItemDef->flags & kItemDefFlag_TransFailonHighLimit) && inv_GetNumericItemValue(pEnt, pItemDef->pchName) + pPart->iCount > pItemDef->MaxNumericValue)
					{
						(*peError) = kMicroPurchaseErrorType_NumericFull;
						return true;
					}
				}
				break;
			}
		case kSpecialPartType_ItemAssignmentReserveSlots:
			{
				if((pPart->iCount > 0) && NONNULL(pEnt))
				{
					ItemDef *pItemDef = RefSystem_ReferentFromString(g_hItemDict, "Addreservedoffslotsmicrotrans");
					if (pItemDef && (pItemDef->flags & kItemDefFlag_TransFailonHighLimit) && inv_GetNumericItemValue(pEnt, pItemDef->pchName) + pPart->iCount > pItemDef->MaxNumericValue)
					{
						(*peError) = kMicroPurchaseErrorType_NumericFull;
						return true;
					}
				}
				break;
			}
		case kSpecialPartType_CharSlots:
			{
				if(pExtract && !gConf.bDontAllowGADModification && !slGAD_CanChangeAttribClampedExtract(pExtract, MicroTrans_GetCharSlotsGADKey(), pPart->iCount, 0, 100000))
				{
					(*peError) = kMicroPurchaseErrorType_AttribClamped;
					return true;
				}
				break;
			}

		case kSpecialPartType_SuperPremium:
			{
				if(pExtract && !gConf.bDontAllowGADModification && !slGAD_CanChangeAttribClampedExtract(pExtract, MicroTrans_GetSuperPremiumGADKey(), pPart->iCount, 0, 100000))
				{
					(*peError) = kMicroPurchaseErrorType_AttribClamped;
					return true;
				}
				break;
			}

		case kSpecialPartType_CostumeSlots:
			{
				if(pExtract && !gConf.bDontAllowGADModification && !slGAD_CanChangeAttribClampedExtract(pExtract, MicroTrans_GetCostumeSlotsGADKey(), pPart->iCount, 0, 100000))
				{
					(*peError) = kMicroPurchaseErrorType_AttribClamped;
					return true;
				}
				break;
			}
		case kSpecialPartType_Respec:
			{
				if(pExtract && !gConf.bDontAllowGADModification && !slGAD_CanChangeAttribClampedExtract(pExtract, MicroTrans_GetRespecTokensGADKey(), pPart->iCount, 0, 100000))
				{
					(*peError) = kMicroPurchaseErrorType_AttribClamped;
					return true;
				}
				break;
			}
		case kSpecialPartType_Rename:
			{
				if(pExtract && !gConf.bDontAllowGADModification && !slGAD_CanChangeAttribClampedExtract(pExtract, MicroTrans_GetRenameTokensGADKey(), pPart->iCount, 0, 100000))
				{
					(*peError) = kMicroPurchaseErrorType_AttribClamped;
					return true;
				}
				break;
			}
		case kSpecialPartType_Retrain:
			{
				if(pExtract && !gConf.bDontAllowGADModification && !slGAD_CanChangeAttribClampedExtract(pExtract, MicroTrans_GetRetrainTokensGADKey(), pPart->iCount, 0, 100000))
				{
					(*peError) = kMicroPurchaseErrorType_AttribClamped;
					return true;
				}
				break;
			}
		case kSpecialPartType_OfficerSlots:
			{
				if(pExtract && !gConf.bDontAllowGADModification && !slGAD_CanChangeAttribClampedExtract(pExtract, MicroTrans_GetOfficerSlotsGADKey(), pPart->iCount, 0, 100000))
				{
					(*peError) = kMicroPurchaseErrorType_AttribClamped;
					return true;
				}
				break;
			}

		case kSpecialPartType_CostumeChange:
			{
				if(pExtract && !gConf.bDontAllowGADModification && !slGAD_CanChangeAttribClampedExtract(pExtract, MicroTrans_GetFreeCostumeChangeGADKey(), pPart->iCount, 0, 100000))
				{
					(*peError) = kMicroPurchaseErrorType_AttribClamped;
					return true;
				}
				break;
			}
		case kSpecialPartType_ShipCostumeChange:
			{
				if(pExtract && !gConf.bDontAllowGADModification && !slGAD_CanChangeAttribClampedExtract(pExtract, MicroTrans_GetFreeShipCostumeChangeGADKey(), pPart->iCount, 0, 100000))
				{
					(*peError) = kMicroPurchaseErrorType_AttribClamped;
					return true;
				}
				break;
			}
		case kSpecialPartType_ItemAssignmentCompleteNow:
			{
				if(pExtract && !gConf.bDontAllowGADModification && !slGAD_CanChangeAttribClampedExtract(pExtract, MicroTrans_GetItemAssignmentCompleteNowGADKey(), pPart->iCount, 0, 100000))
				{
					(*peError) = kMicroPurchaseErrorType_AttribClamped;
					return true;
				}
				break;
			}
		case kSpecialPartType_ItemAssignmentUnslotItem:
			{
				if(pExtract && !gConf.bDontAllowGADModification && !slGAD_CanChangeAttribClampedExtract(pExtract, MicroTrans_GetItemAssignmentUnslotTokensGADKey(), pPart->iCount, 0, 100000))
				{
					(*peError) = kMicroPurchaseErrorType_AttribClamped;
					return true;
				}
				break;
			}
		}
	}

	return false;
}

bool mictrotrans_HasPurchasedEx(const GameAccountData *pData, const char *pchMictroTransName)
{
	if(eaIndexedGetUsingString(&pData->eaAllPurchases, pchMictroTransName))
		return true;

	return false;
}

bool microtrans_HasPurchased(	const GameAccountData *pData,
								MicroTransactionDef *pDef)
{
	if(pDef && pData && mictrotrans_HasPurchasedEx(pData,pDef->pchName))
	{
		return true;
	}
	return false;
}

static bool microtrans_CanPurchaseExpression(int iPartitionIdx, Entity *pEnt, MicroTransactionDef *pDef, const GameAccountData *pData)
{
	if(pDef->pExprCanBuy)
	{
		if(pDef->bBuyExprRequiresEntity && !pEnt)
		{
			return false;
		}
		else
		{
			MultiVal mvReturn = {0};

			microtrans_BuyContextSetup(iPartitionIdx, pEnt, pData, pEnt == NULL);
			exprContextSetSilentErrors(microtrans_GetBuyContext(pEnt == NULL), false);
			exprEvaluate(pDef->pExprCanBuy, microtrans_GetBuyContext(pEnt == NULL), &mvReturn);

			if(!MultiValGetInt(&mvReturn, NULL))
			{
				return false;
			}
		}
	}
	
	return true;
}

void microtrans_GetCanPurchaseErrorString(MicroPurchaseErrorType eError, MicroTransactionDef *pDef, int iLangID, char** pestrError)
{
	if (pestrError && 
		eError != kMicroPurchaseErrorType_None)
	{
		char *estrTmp = NULL;
		char pchErrorKey[128];
		const char* pchErrorTypeString = StaticDefineIntRevLookup(MicroPurchaseErrorTypeEnum, eError);
		const char *pchProductName = TranslateDisplayMessage(pDef->displayNameMesg);

		estrClear(pestrError);

		if (!pchProductName)
		{
			// Use the def name if the message hasn't loaded on the client yet, since in some cases
			// this is called on demand.
			pchProductName = pDef->pchName;
		}

		sprintf(pchErrorKey, "MicroTrans_CanPurchaseError_%s", pchErrorTypeString);

		//Special handling for cannot purchase again to check for extra special error messages
		if(eError == kMicroPurchaseErrorType_CannotPurchaseAgain)
		{
			if(pDef->bOnePerAccount)
			{
				Message *pMessage = NULL;
				estrPrintf(&estrTmp, "%s_%s",
					pchErrorKey,
					"OnePerAccount");
				pMessage = RefSystem_ReferentFromString(gMessageDict, estrTmp);
				if(pMessage)
					sprintf(pchErrorKey, "%s", estrTmp);
			}
			else if(pDef->bOnePerCharacter)
			{
				Message *pMessage = NULL;
				estrPrintf(&estrTmp, "%s_%s",
					pchErrorKey,
					"OnePerCharacter");
				pMessage = RefSystem_ReferentFromString(gMessageDict, estrTmp);
				if(pMessage)
					sprintf(pchErrorKey, "%s", estrTmp);
			}
		}

		langFormatGameMessageKey(iLangID, pestrError, pchErrorKey,
			STRFMT_STRING("Name",pchProductName),
			STRFMT_END);

		estrDestroy(&estrTmp);
	}
}

MicroPurchaseErrorType microtrans_GetCanPurchaseError(int iPartitionIdx, Entity *pEnt, 
													  const GameAccountData *pData,
												      MicroTransactionDef *pDef,
													  int iLangID,
												      char** pestrError)
{
	AddSavedPetErrorType ePetError = kAddSavedPetErrorType_None;
	MicroPurchaseErrorType eError = kMicroPurchaseErrorType_None; 

	PERFINFO_AUTO_START_FUNC();

	if(!pDef)
	{
		PERFINFO_AUTO_STOP();
		return kMicroPurchaseErrorType_InvalidTransaction;
	}
	if(IS_HANDLE_ACTIVE(pDef->hRequiredPurchase)
		&& !microtrans_HasPurchased(pData, GET_REF(pDef->hRequiredPurchase)))
	{
		PERFINFO_AUTO_STOP();
		return kMicroPurchaseErrorType_RequiredPurchase;
	}
	else if (microtrans_ItemCheck(iPartitionIdx, pEnt, pDef, &eError))
	{
		//Failed the item check (usage restrictions, unique check, bag full)
	}
	else if (microtrans_AllegianceCheck(pEnt, pDef))
	{
		eError = kMicroPurchaseErrorType_Allegiance;
	}
	else if (microtrans_SpecialCheck(iPartitionIdx, pEnt, pDef, &eError))
	{
		// Failed the special check (attrib limits, numeric limits)
	}
	else 
	{
		if (microtrans_PetCheck(pEnt, pDef, pData, &ePetError))
		{
			switch (ePetError)
			{
				xcase kAddSavedPetErrorType_MaxPets:
				{
					eError = kMicroPurchaseErrorType_MaxPets;
				}
				xcase kAddSavedPetErrorType_MaxPuppets:
				{
					eError = kMicroPurchaseErrorType_MaxPuppets;
				}
				xcase kAddSavedPetErrorType_UniqueCheck:
				{
					eError = kMicroPurchaseErrorType_Unique;
				}
				xcase kAddSavedPetErrorType_InvalidAllegiance:
				{
					// This shouldn't happen, but if it passes the above Allegiance check, set it here.
					eError = kMicroPurchaseErrorType_Allegiance;
				}
				xcase kAddSavedPetErrorType_NotAPuppet:
				{
					eError = kMicroPurchaseErrorType_InvalidPet;
				}
				xcase kAddSavedPetErrorType_AcquireLimit:
				{
					eError = kMicroPurchaseErrorType_PetAcquireLimit;
				}
				xdefault:
				{
					// If it gets here, then someone added a new error...
					eError = kMicroPurchaseErrorType_Unknown;
				}
			}
		}
		else if (microtrans_AlreadyPurchased(pEnt, pData, pDef))
		{
			if(microtrans_HasPurchased(pData, pDef))
			{
				eError = kMicroPurchaseErrorType_CannotPurchaseAgain;
			}
			else
			{
				// If it comes back as "already purchased", but they haven't ACTUALLY bought it,
				//   then they've earned it via an entitlement.
				// For example, subscribers have access to things that non-subscribers have
				//   to buy. These things will be AlreadyEntitled.
				eError = kMicroPurchaseErrorType_AlreadyEntitled;
			}
		}
		else if (!microtrans_CanPurchaseExpression(iPartitionIdx, pEnt, pDef, pData))
		{
			eError = kMicroPurchaseErrorType_FailsExpressionRequirement;
		}
	}
	
	microtrans_GetCanPurchaseErrorString(eError, pDef, pEnt ? entGetLanguage(pEnt) : iLangID, pestrError);

	PERFINFO_AUTO_STOP();
	return eError;
}

bool microtrans_CanPurchaseProduct(int iPartitionIdx, Entity *pEnt, const GameAccountData *pData, MicroTransactionDef *pDef)
{
	MicroPurchaseErrorType eErrorType = microtrans_GetCanPurchaseError(iPartitionIdx, pEnt, pData, pDef, entGetLanguage(pEnt), NULL);
	return (eErrorType == kMicroPurchaseErrorType_None);
}

bool microtrans_CannotPurchaseAgain(Entity *pEnt, const MicroTransactionProduct *pMTProduct)
{
	MicroTransactionDef *pDef = GET_REF(pMTProduct->hDef);
	return microtrans_AlreadyPurchased(pEnt, entity_GetGameAccount(pEnt), pDef);
}

bool microtrans_MTDefGrantsPermission(MicroTransactionDef *pMTDef, GameTokenText *pToken)
{
	int iPartIdx, iTokenIdx;
	for(iPartIdx = eaSize(&pMTDef->eaParts)-1; iPartIdx>=0; iPartIdx--)
	{
		GamePermissionDef *pPermDef = NULL;
		if(pMTDef->eaParts[iPartIdx]->ePartType != kMicroPart_Permission)
			continue;

		pPermDef = eaIndexedGetUsingString(&g_GamePermissions.eaPermissions, pMTDef->eaParts[iPartIdx]->pchPermission);
		if(!pPermDef)
			continue;

		for(iTokenIdx = eaSize(&pPermDef->eaTextTokens)-1; iTokenIdx>=0; iTokenIdx--)
		{
			if(!StructCompare(parse_GameTokenText, pPermDef->eaTextTokens[iTokenIdx], pToken, 0, 0, 0))
			{
				return true;
			}
		}
	}
	return false;
}

bool microtrans_GrantsUniqueItem(const MicroTransactionDef *pDef, const MicroTransactionRewards *pRewards)
{
	EARRAY_FOREACH_BEGIN(pDef->eaParts, iPart);
	{
		MicroTransactionPart *pPart = pDef->eaParts[iPart];
		ItemDef *pItemDef = NULL;
		PetDef *pPetDef = NULL;

		if (pPart->ePartType != kMicroPart_Item) continue;

		pItemDef = GET_REF(pPart->hItemDef);

		if (!pItemDef) continue;
		if (pItemDef->flags & kItemDefFlag_Unique) return true;

		if (pItemDef->eType != kItemType_SavedPet && pItemDef->eType != kItemType_STOBridgeOfficer) continue;

		pPetDef = GET_REF(pItemDef->hPetDef);
		if (!pPetDef) continue;
		if (pPetDef->bIsUnique) return true;
	}
	EARRAY_FOREACH_END;

	if (pRewards)
	{
		EARRAY_FOREACH_BEGIN(pRewards->eaRewards, iReward);
		{
			if (inv_CheckUniqueItemsInBags(pRewards->eaRewards[iReward]->eaBags)) return true;
		}
		EARRAY_FOREACH_END;
	}

	return false;
}

void microtrans_GetGameTokenTextFromPermissionExpr(Expression *pExpr, GameTokenText ***peaTokensOut)
{
	eaClearStruct(peaTokensOut, parse_GameTokenText);
	{
		int i;
		int *eaiIndices = NULL;
		exprFindFunctions(pExpr, "PermTokenTypePlayer", &eaiIndices);

		for(i=0; i<eaiSize(&eaiIndices); i++)
		{
			GameTokenText *pToken = StructCreate(parse_GameTokenText);
			const MultiVal *pTokenVal = exprFindFuncParam(pExpr, eaiIndices[i], 1);
			const MultiVal *pTypeVal = exprFindFuncParam(pExpr, eaiIndices[i], 0);
			pToken->eType = StaticDefineIntGetInt(GameTokenTypeEnum, MultiValGetString(pTypeVal,NULL));
			pToken->pchKey = NULL;
			pToken->pchValue = StructAllocString(MultiValGetString(pTokenVal,NULL));
			eaPush(peaTokensOut, pToken);
		}
		eaiDestroy(&eaiIndices);
	}
	{
		int i;
		int *eaiIndices = NULL;
		exprFindFunctions(pExpr, "PermTokenKeyTypePlayer", &eaiIndices);

		for(i=0; i<eaiSize(&eaiIndices); i++)
		{
			GameTokenText *pToken = StructCreate(parse_GameTokenText);
			const MultiVal *pTypeVal = exprFindFuncParam(pExpr, eaiIndices[i], 0);
			const MultiVal *pKeyVal = exprFindFuncParam(pExpr, eaiIndices[i], 1);
			const MultiVal *pValueVal = exprFindFuncParam(pExpr, eaiIndices[i], 2);
			pToken->eType = StaticDefineIntGetInt(GameTokenTypeEnum, MultiValGetString(pTypeVal,NULL));
			pToken->pchKey = StructAllocString(MultiValGetString(pKeyVal,NULL));
			pToken->pchValue = StructAllocString(MultiValGetString(pValueVal,NULL));
			eaPush(peaTokensOut, pToken);
		}
		eaiDestroy(&eaiIndices);
	}
	{
		int i;
		int *eaiIndices = NULL;
		exprFindFunctions(pExpr, "PermTokenPlayer", &eaiIndices);

		for(i=0; i<eaiSize(&eaiIndices); i++)
		{
			const MultiVal *pStringVal = exprFindFuncParam(pExpr, eaiIndices[i], 0);
			GameTokenText *pToken = gamePermission_TokenStructFromString(MultiValGetString(pStringVal, NULL));
			if(pToken)
				eaPush(peaTokensOut, pToken);
		}
		eaiDestroy(&eaiIndices);
	}
	{
		int i;
		int *eaiIndices = NULL;
		exprFindFunctions(pExpr, "HasPermissionToken", &eaiIndices);

		for(i=0; i<eaiSize(&eaiIndices); i++)
		{
			GameTokenText *pToken = StructCreate(parse_GameTokenText);

			const MultiVal *pTypeVal = exprFindFuncParam(pExpr, eaiIndices[i], 0);
			const MultiVal *pKeyVal = exprFindFuncParam(pExpr, eaiIndices[i], 1);
			const MultiVal *pValueVal = exprFindFuncParam(pExpr, eaiIndices[i], 2);
			pToken->eType = StaticDefineIntGetInt(GameTokenTypeEnum, MultiValGetString(pTypeVal,NULL));
			pToken->pchKey = StructAllocString(MultiValGetString(pKeyVal,NULL));
			pToken->pchValue = StructAllocString(MultiValGetString(pValueVal,NULL));

			eaPush(peaTokensOut, pToken);
		}
		eaiDestroy(&eaiIndices);
	}
}

bool microtrans_IsPremiumEntitlement(MicroTransactionDef *pDef)
{
	bool bIsPremium = true;

	PERFINFO_AUTO_START_FUNC();
	if(pDef && g_pPremiumPermission)
	{
		FOR_EACH_IN_EARRAY(pDef->eaParts, MicroTransactionPart, pPart)
		{
			if(bIsPremium && pPart->ePartType == kMicroPart_Permission)
			{
				GamePermissionDef *pPermission = eaIndexedGetUsingString(&g_GamePermissions.eaPermissions, pPart->pchPermission);
				
				if(!pPermission)
					continue;

				FOR_EACH_IN_EARRAY(pPermission->eaTextTokens, GameTokenText, pToken)
					bIsPremium = bIsPremium && GamePermissions_PremiumGivesToken(pToken);
				FOR_EACH_END
			}
			else
			{
				// If we've figured out that we don't actually match
				//   or
				// This is some weird mix of stuff including permissions. Not sure how to handle it.
				//   Don't call it an entitlement for now.
				bIsPremium = false;
				break;
			}
		}
		FOR_EACH_END
	}
	else
	{
		bIsPremium = false;
	}
	PERFINFO_AUTO_STOP();

	return bIsPremium;
}

//
// WARNING! NOTE! DANGER!
//
// This function only operates correctly if there is a single permission in the
// expression and it's not NOTed or the like.
//
MicroTransactionDef *microtrans_FindDefFromPermissionExpr(Expression *pExpr)
{ 
	static GameTokenText **s_eaTokens = NULL;
	MicroTransactionDef *pDef = NULL;
	int iPartIdx, iTokenIdx, iMatchIdx;
	if(!pExpr)
	{
		return NULL;
	}
	microtrans_GetGameTokenTextFromPermissionExpr(pExpr, &s_eaTokens);
	FOR_EACH_IN_REFDICT(g_hMicroTransDefDict, MicroTransactionDef, pMTDef)
	{
		for(iPartIdx = eaSize(&pMTDef->eaParts)-1; iPartIdx>=0 && !pDef; iPartIdx--)
		{
			GamePermissionDef *pPermDef = NULL;
			if(pMTDef->eaParts[iPartIdx]->ePartType != kMicroPart_Permission)
				continue;

			pPermDef = eaIndexedGetUsingString(&g_GamePermissions.eaPermissions, pMTDef->eaParts[iPartIdx]->pchPermission);
			if(!pPermDef)
				continue;

			for(iTokenIdx = eaSize(&pPermDef->eaTextTokens)-1; iTokenIdx>=0 && !pDef; iTokenIdx--)
			{
				for(iMatchIdx = eaSize(&s_eaTokens)-1; iMatchIdx >= 0; iMatchIdx--)
				{
					if(!StructCompare(parse_GameTokenText, pPermDef->eaTextTokens[iTokenIdx], s_eaTokens[iMatchIdx], 0, 0, 0))
					{
						pDef = pMTDef;
						break;
					}
				}
			}
		}

		if(pDef)
			break;
	} FOR_EACH_END;

	return pDef;
}

void microtrans_FindAllMTDefsForPermissionExpr(Expression *pExpr, MicroTransactionDef ***peaMTDefs)
{ 
	static GameTokenText **s_eaTokens = NULL;
	int iPartIdx, iTokenIdx, iMatchIdx;
	if(!pExpr)
		return;

	microtrans_GetGameTokenTextFromPermissionExpr(pExpr, &s_eaTokens);
	FOR_EACH_IN_REFDICT(g_hMicroTransDefDict, MicroTransactionDef, pMTDef)
	{
		for(iPartIdx = eaSize(&pMTDef->eaParts)-1; iPartIdx>=0; iPartIdx--)
		{
			GamePermissionDef *pPermDef = NULL;
			if(pMTDef->eaParts[iPartIdx]->ePartType != kMicroPart_Permission)
				continue;

			pPermDef = eaIndexedGetUsingString(&g_GamePermissions.eaPermissions, pMTDef->eaParts[iPartIdx]->pchPermission);
			if(!pPermDef)
				continue;

			for(iTokenIdx = eaSize(&pPermDef->eaTextTokens)-1; iTokenIdx>=0; iTokenIdx--)
			{
				for(iMatchIdx = eaSize(&s_eaTokens)-1; iMatchIdx >= 0; iMatchIdx--)
				{
					if(!StructCompare(parse_GameTokenText, pPermDef->eaTextTokens[iTokenIdx], s_eaTokens[iMatchIdx], 0, 0, 0))
					{
						eaPush(peaMTDefs, pMTDef);
						continue;
					}
				}
			}
		}
	} FOR_EACH_END;
}

//
// WARNING! NOTE! DANGER!
//
// This function only operates correctly if there are permissions in the expression,
// and those permissions are all anded together. If any fancy booleans are used, or
// nots or whatever, then this won't calculate properly.
//
bool microtrans_PremiumSatisfiesPermissionExpr(Expression *pExpr)
{
	static GameTokenText **s_eaTokens = NULL;
	bool bRet;
	int j;
	microtrans_GetGameTokenTextFromPermissionExpr(pExpr, &s_eaTokens);
	bRet = eaSize(&s_eaTokens) != 0;
	for(j = eaSize(&s_eaTokens)-1; bRet && j>=0; j--)
	{
		if(!GamePermissions_PremiumGivesToken(s_eaTokens[j]))
		{
			bRet = false;
		}
	}

	return bRet;
}

// returns the main cateogry string for the shard
const char *microtrans_GetShardMainCategory(void)
{
	if(g_pMainMTCategory)
	{
		return g_pMainMTCategory->pchName;
	}
	return "";
}

// returns the category prefix used on this shard
const char *microtrans_GetShardCategoryPrefix(void)
{
	if(g_pMicroTrans_ShardConfig)
	{
		return g_pMicroTrans_ShardConfig->pchCategoryPrefix;
	}

	return "";
}

// returns the MT shard category used on this shard
const char *microtrans_GetShardMTCategory(void)
{
	if (g_pMicroTrans_ShardConfig)
	{
		return StaticDefineInt_FastIntToString(MicroTrans_ShardCategoryEnum, g_pMicroTrans_ShardConfig->eShardCategory);
	}

	return "";
}

const char *microtrans_GetShardEnvironmentName(void)
{
	static char *name = NULL;

	if (!name && g_pMicroTrans_ShardConfig)
		estrPrintf(&name, "%s.%s", GetShortProductName(), StaticDefineIntRevLookup(MicroTrans_ShardCategoryEnum, g_pMicroTrans_ShardConfig->eShardCategory));

	return name;
}

// returns the billing URL used on this shard
AUTO_EXPR_FUNC(UIGen) ACMD_NAME(microtrans_GetBillingURL);
const char *microtrans_GetBillingURL(void)
{
	// TODO: this should be replaced with a value that is per-game, and per-shard (e.g. live vs. pts vs. qa)
	// Likely, the value will come from the server environment.  UIGens will retrieve that value here.

	// I'm not quite sure this is the completely correct way to select which billing URL to use.
	// LocaleIDs or WindowsLocale may be a better enum to be keying off of.
	Language lang = locGetLanguage(getCurrentLocale());

	if (lang == LANGUAGE_FRENCH)
	{
		return "https://billing.fr.perfectworld.eu/"; // french
	}
	else if (lang == LANGUAGE_GERMAN)
	{
		return "https://billing.de.perfectworld.eu/"; // german
	}
	else // default to the US billing site
//	if (lang == LANGUAGE_ENGLISH) 
	{
		return "https://billing.perfectworld.com/charge"; // us english
	}
}

// returns the currency used on this shard
AUTO_EXPR_FUNC(UIGen) ACMD_NAME(microtrans_GetCurrency);
const char *microtrans_GetShardCurrency(void)
{
	if(g_pMicroTrans_ShardConfig)
	{
		return g_pMicroTrans_ShardConfig->pchCurrency;
	}

	return "";
}

// returns the currency used on this shard
AUTO_EXPR_FUNC(UIGen) ACMD_NAME(microtrans_GetExchangeWithdrawCurrency);
const char *microtrans_GetExchangeWithdrawCurrency(void)
{
    if(g_pMicroTrans_ShardConfig)
    {
        return g_pMicroTrans_ShardConfig->pchExchangeWithdrawCurrency;
    }

    return "";
}

const char *microtrans_GetShardForSaleBucketKey(void)
{
    if(g_pMicroTrans_ShardConfig)
    {
        return g_pMicroTrans_ShardConfig->pchCurrencyExchangeForSaleBucket;
    }

    return "";
}

const char *microtrans_GetShardReadyToClaimBucketKey(void)
{
    if(g_pMicroTrans_ShardConfig)
    {
        return g_pMicroTrans_ShardConfig->pchCurrencyExchangeReadyToClaimBucket;
    }

    return "";
}

const char *microtrans_GetPromoGameCurrencyKey(void)
{
    if(g_pMicroTrans_ShardConfig)
    {
        return g_pMicroTrans_ShardConfig->pchPromoGameCurrencyBucket;
    }

    return "";
}

const char *microtrans_GetPromoGameCurrencyWithdrawNumericName(void)
{
    return g_MicroTransConfig.pchPromoGameCurrencyWithdrawNumeric;
}


const char *microtrans_GetShardFoundryTipBucketKey(void)
{
	if(g_pMicroTrans_ShardConfig)
	{
		return g_pMicroTrans_ShardConfig->pchFoundryTipBucket;
	}

	return "";
}

const char *microtrans_GetPromoGameCurrencyBucketKey(void)
{
	if (g_pMicroTrans_ShardConfig)
	{
		return g_pMicroTrans_ShardConfig->pchPromoGameCurrencyBucket;
	}

	return "";
}

const char *microtrans_GetPromoGameCurrencyNumeric(void)
{
	return g_MicroTransConfig.pchPromoGameCurrencyWithdrawNumeric ? g_MicroTransConfig.pchPromoGameCurrencyWithdrawNumeric : "";
}

const char *microtrans_GetShardCurrencyExactName(void)
{
	static char shardCurrency[64] = "";
	if(!shardCurrency[0] && g_pMicroTrans_ShardConfig)
	{
		sprintf(shardCurrency, "_%s", g_pMicroTrans_ShardConfig->pchCurrency);
	}
	return shardCurrency;
}

SA_RET_OP_STR const char *microtrans_GetGlobalPointBuyCategory(void)
{
	return g_MicroTransConfig.pchGlobalPointBuyCategory;
}

SA_RET_OP_STR const char *microtrans_GetShardPointBuyCategory(void)
{
	static char *pchGamePointBuyCategory = NULL;

	if (!pchGamePointBuyCategory)
	{
		if (SAFE_DEREF(g_MicroTransConfig.pchPointBuyCategory))
		{
			estrPrintf(&pchGamePointBuyCategory, "%s.%s", microtrans_GetShardCategoryPrefix(), g_MicroTransConfig.pchPointBuyCategory);
		}
		else
		{
			estrCopy2(&pchGamePointBuyCategory, "");
		}
	}

	return pchGamePointBuyCategory;
}

SA_RET_OP_STR const char *microtrans_GetShardPointBuySteamCategory(void)
{
	static char *pchGamePointBuySteamCategory = NULL;

	if (!pchGamePointBuySteamCategory)
	{
		if (SAFE_DEREF(g_MicroTransConfig.pchPointBuySteamCategory))
		{
			estrPrintf(&pchGamePointBuySteamCategory, "%s.%s", microtrans_GetShardCategoryPrefix(), g_MicroTransConfig.pchPointBuySteamCategory);
		}
		else
		{
			estrCopy2(&pchGamePointBuySteamCategory, "");
		}
	}

	return pchGamePointBuySteamCategory;
}

bool microtrans_PointBuyingEnabled()
{
	if(SAFE_DEREF(g_MicroTransConfig.pchGlobalPointBuyCategory) && SAFE_DEREF(g_MicroTransConfig.pchPointBuyCategory)
		&& (isDevelopmentMode() || SAFE_MEMBER(g_pMicroTrans_ShardConfig, bPointBuyEnabled) || g_bPointBuyDebugging ))
		return true;
	
	return false;
}

bool microtrans_SteamWalletEnabled()
{
	if(SAFE_DEREF(g_MicroTransConfig.pchGlobalPointBuyCategory) && SAFE_DEREF(g_MicroTransConfig.pchPointBuySteamCategory)
		&& ( isDevelopmentMode() || SAFE_MEMBER(g_pMicroTrans_ShardConfig, bSteamWalletEnabled) || g_bPointBuyDebugging ))
		return true;

	return false;
}

U32 MicroTrans_GetItemDiscount(Entity *pEntity, Item *pItem)
{
	if(pEntity && pItem)
	{
		ItemDef * pDef = GET_REF(pItem->hItem);
		if(pDef && pDef->eType == kItemType_Coupon)
		{
			U32 uDiscount;

			if(pDef->bCouponUsesItemLevel)
			{
				uDiscount = item_GetLevel(pItem);
			}
			else
			{
				uDiscount = pDef->uCouponDiscount;
			}

			// each game has its own max coupon value to prevent bad item def
			uDiscount = min(g_MicroTransConfig.uMaximumCouponDiscount, uDiscount);

			// each percent is really 100 on account server
			return uDiscount * MT_DISCOUNT_PER_PERCENT;
		}
	}

	return 0;
}

// test to see if coupun item is valid for this product
bool MicroTrans_ValidDiscountItem(Entity *pEntity, U32 uProductID, Item *pItem, MicroTransactionProduct ***ppProducts)
{
	if(!pItem)
	{
		// no discount item
		return false;
	}

	if(pEntity)
	{
		S32 i;
		MicroTransactionProduct *pMTProduct = NULL;
		ItemDef * pDef;
		U32 uDiscount = MicroTrans_GetItemDiscount(pEntity, pItem);

		if(uDiscount < 1)
		{
			return false;
		}

		for(i = 0; i < eaSize(ppProducts); ++i)
		{
			AccountProxyProduct *pAPProductCheck = (*ppProducts)[i]->pProduct;
			if(pAPProductCheck && pAPProductCheck->uID == uProductID)
			{
				// Only allow coupon if the server discount is less than the coupon
				S64 iPrice = microtrans_GetPrice(pAPProductCheck);
				S64 iFullPrice = microtrans_GetFullPrice(pAPProductCheck);
				if(iPrice < iFullPrice)
				{
					U32 uServerDiscount = ( (1.0f - ((F32)iPrice / (F32)iFullPrice) )  * 10000.0f + 0.5f);	// included x 100 
					if(uServerDiscount >=  uDiscount)
					{
						return false;
					}
				}
				pMTProduct = (*ppProducts)[i];
				break;
			}
		}

		if(!pMTProduct)
		{
			return false;
		}

		pDef = GET_REF(pItem->hItem);
		if(pDef && pDef->eType == kItemType_Coupon)
		{
			if(eaSize(&pDef->ppchMTCategories) > 0)
			{
				// check to see if we have a match
				S32 j, k;
				for(j = 0; j < eaSize(&pDef->ppchMTCategories); ++j)
				{
					for(k = 0; k < eaSize(&pMTProduct->ppchCategories); ++k)
					{
						if(stricmp(pDef->ppchMTCategories[j], pMTProduct->ppchCategories[k]) == 0)
						{
							return true;
						}
					}
				}
			} // end if ppchMTCategories
			else if(eaSize(&pDef->ppchMTItems) > 0)
			{
				// check to see if we have a match
				S32 j;
				MicroTransactionDef *pMTDef = GET_REF(pMTProduct->hDef);
				if(pMTDef)
				{
					for(j = 0; j < eaSize(&pDef->ppchMTItems); ++j)
					{
						if(stricmp(pDef->ppchMTItems[j], pMTDef->pchName) == 0)
						{
							return true;
						}
					}
				}
			}
			else
			{
				// no entries, usable on anything
				return true;
			}
		}
	}

	return false;
}

// test for MT coupon item
U64 MicroTrans_GetBestCouponItemID(Entity *pEntity, U32 uProductID, MicroTransactionProduct ***ppProducts, MicroUiCoupon ***ppCoupons)
{
	S32 i;
	AccountProxyProduct *pAPProductFound = NULL;

	if(!pEntity)
	{
		return 0;
	}

	if(!pEntity->pInventoryV2)
	{
		return 0;
	}

	if(!ppProducts)
	{
		return 0;
	}

	for(i = 0; i < eaSize(ppProducts); ++i)
	{
		AccountProxyProduct *pAPProduct = (*ppProducts)[i]->pProduct;
		if(pAPProduct && pAPProduct->uID == uProductID)
		{
			pAPProductFound = pAPProduct;
			break;
		}
	}

	if(pAPProductFound)
	{
		S32 NumBags = eaSize(&pEntity->pInventoryV2->ppInventoryBags);
		S32 ii,i2;
		U64 uBestItemID = 0;
		U32 uBestDiscount = 0;

		for(ii=0;ii<NumBags;ii++)
		{
			InventoryBag* pBag = pEntity->pInventoryV2->ppInventoryBags[ii];

			// check for best mt coupon item

			for(i2 =0; i2 < eaSize(&pBag->ppIndexedInventorySlots); ++i2)
			{
				InventorySlot*pSlot  = pBag->ppIndexedInventorySlots[i2];
				if(pSlot && pSlot->pItem && MicroTrans_ValidDiscountItem(pEntity, uProductID, pSlot->pItem, ppProducts))
				{
					U32 uDiscount = MicroTrans_GetItemDiscount(pEntity, pSlot->pItem);
					ItemDef *pItemDef = GET_REF(pSlot->pItem->hItem);
					if(uDiscount > uBestDiscount)
					{
						uBestDiscount = uDiscount;
						uBestItemID = pSlot->pItem->id;
					}

					// If a list is passed in add any matching coupon to it
					if(uDiscount > 0 && ppCoupons)
					{
						MicroUiCoupon *pCoupon = StructCreate(parse_MicroUiCoupon);
						pCoupon->uCouponItemID = pSlot->pItem->id;
						eaPush(ppCoupons, pCoupon);
					}

//					else if(uDiscount == uBestDiscount)
//					{
			// ties go to itemdefs that are by mt product over category over non (all)
//					}
				}
			}
		}

		return uBestItemID;

	}

	return 0;
}

S64 microtrans_GetPrice(AccountProxyProduct *pProduct)
{
	S64 iReturnPrice = -1;
	S32 iPrice;
	const char *pchCurrency = microtrans_GetShardCurrency();
	for(iPrice = eaSize(&pProduct->ppMoneyPrices)-1; iPrice >= 0; iPrice--)
	{
		if(pProduct->ppMoneyPrices[iPrice] && 
			!isRealCurrency(moneyCurrency(pProduct->ppMoneyPrices[iPrice])) &&
			!stricmp(moneyKeyName(pProduct->ppMoneyPrices[iPrice]), pchCurrency))
		{
			iReturnPrice = moneyCountPoints(pProduct->ppMoneyPrices[iPrice]);
		}
	}
	return iReturnPrice;
}

S64 microtrans_GetFullPrice(AccountProxyProduct *pProduct)
{
	S64 iReturnPrice = -1;
	S32 iPrice;
	const char *pchCurrency = microtrans_GetShardCurrency();
	for(iPrice = eaSize(&pProduct->ppMoneyPrices)-1; iPrice >= 0; iPrice--)
	{
		if(pProduct->ppFullMoneyPrices[iPrice] && 
			!isRealCurrency(moneyCurrency(pProduct->ppFullMoneyPrices[iPrice])) &&
			!stricmp(moneyKeyName(pProduct->ppFullMoneyPrices[iPrice]), pchCurrency))
		{
			iReturnPrice = moneyCountPoints(pProduct->ppFullMoneyPrices[iPrice]);
		}
	}
	return iReturnPrice;
}


#include "MicroTransactions_h_ast.c"