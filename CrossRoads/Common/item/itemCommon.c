#include "itemCommon.h"
#include "itemGenCommon.h"
#include "AlgoItemCommon.h"
#include "AlgoPet.h"
#include "NotifyCommon.h"

#include "EntityLib.h"
#include "StringCache.h"

#include "aiStructCommon.h"
#include "CharacterAttribs.h"
#include "CharacterClass.h"
#include "ChatData.h"
#include "CombatConfig.h"
#include "Expression.h"
#include "ExpressionPrivate.h"
#include "estring.h"
#include "entCritter.h"
#include "EntitySavedData.h"
#include "ItemAssignments.h"
#include "CostumeCommonLoad.h"
#include "CostumeCommonTailor.h"
#include "MemoryPool.h"
#include "Message.h"
#include "Player.h"
#include "PowerActivation.h"
#include "PowerAnimFX.h"
#include "PowersAutoDesc.h"
#include "PowerHelpers.h"
#include "PowerReplace.h"
#include "PowerSubtarget.h"
#include "PowerModes.h"
#include "PowerVars.h"
#include "SavedPetCommon.h"
#include "SuperCritterPet.h"
#include "Character.h"
#include "Guild.h"
#include "oldencounter_common.h"
#include "ResourceManager.h"
#include "rewardCommon.h"
#include "mission_common.h"
#include "OfficerCommon.h"
#include "contact_common.h"
#include "fileutil.h"
#include "FolderCache.h"
#include "rand.h"
#include "GameStringFormat.h"
#include "GlobalExpressions.h"
#include "StoreCommon.h"
#include "GameBranch.h"
#include "tradeCommon.h"
#include "combatEval.h"
#include "GamePermissionsCommon.h"
#include "SharedBankCommon.h"
#include "dynFxInfo.h"
#include "ResourceSystem_Internal.h"

#include "AutoTransDefs.h"

#include "AutoGen/Character_h_ast.h"
#include "AutoGen/Entity_h_ast.h"
#include "AutoGen/EntitySavedData_h_ast.h"
#include "AutoGen/Player_h_ast.h"
#include "AutoGen/Powers_h_ast.h"
#include "AutoGen/PowerSubtarget_h_ast.h"
#include "AutoGen/GameServerLib_autogen_ServerCmdWrappers.h"
#include "AutoGen/GameClientLib_autogen_ClientCmdWrappers.h"
#include "AutoGen/itemEnums_h_ast.h"
#include "AutoGen/inventoryCommon_h_ast.h"
#include "AutoGen/rewardCommon_h_ast.h"
#include "AutoGen/team_h_ast.h"
#include "AutoGen/itemCommon_h_ast.h"
#include "Autogen/microtransactions_h_ast.h"
#include "Autogen/costumecommon_h_ast.h"


#ifdef GAMESERVER
#include "aiPowers.h"
#include "aiLib.h"
#include "gslContact.h"
#include "inventoryTransactions.h"
#include "WebRequestServer/wrContainerSubs.h"
#endif

#ifdef GAMECLIENT
#include "GameClientLib.h"
#include "gclEntity.h"
#endif

AUTO_RUN_ANON(memBudgetAddMapping(__FILE__, BUDGET_GameSystems););
AUTO_RUN_ANON(memBudgetAddMapping("ItemTagInfo", BUDGET_GameSystems););
AUTO_RUN_ANON(memBudgetAddMapping("ItemTagData", BUDGET_GameSystems););



DictionaryHandle g_hItemDict;
DictionaryHandle g_hItemPowerDict;
DictionaryHandle g_hInfuseSlotDict;
DictionaryHandle g_hPlayerCostumeClonesForItemsDict = NULL;
S32 g_bItemSets = false;

extern ExprContext *g_pItemContext;

typedef struct NOCONST(Item) NOCONST(Item);
typedef struct Expression Expression;

// context for data-defined item tags
DefineContext *s_pDefineItemTags = NULL;

// Context for data-defined ItemCategories
DefineContext *s_pDefineItemCategories = NULL;

// Context to track all data-defined Lore Categories
DefineContext *g_pDefineLoreCategories = NULL;
DefineContext *g_pDefineLoreJournalTypes = NULL;
LoreCategories g_LoreCategories = {0};

DefineContext *g_pDefineItemQualities = NULL;
ItemQualities g_ItemQualities = {0, 0};

// Default deconstruction recipes
static ItemDeconstructDefaults gItemDeconstructDefaults;

static ItemTagInfo gItemTagInfo = {0};
DefineContext *g_pUsageRestrictionCategories = NULL;
int g_iNumUsageRestrictionCategories;
DefineContext *g_pItemEquipLimitCategories = NULL;
static ItemEquipLimitCategories s_EquipLimitCategories = {0};
static ItemPowerCategoryNames s_ItemPowerCategoryNames;
DefineContext *g_pDefineItemPowerCategories = NULL;
DefineContext *g_pDefineItemPowerArtCategories = NULL;

DefineContext *g_pDefineItemGemTypes = NULL;

static ItemHandlingConfig s_ItemHandlingConfig = {0};

ItemTagNames g_ItemTagNames = {0};
ItemCategoryNames g_ItemCategoryNames = {0};
ItemGemConfig s_ItemGemConfig = {0};

bool g_bDisplayItemDebugInfo = false;

// Global settings for the item system
ItemConfig g_ItemConfig = { 0 };

StaticDefineInt LoreCategoryEnum[] =
{
	DEFINE_INT
	DEFINE_EMBEDDYNAMIC_INT(g_pDefineLoreCategories)
	DEFINE_END
};

#define NUM_HARDCODED_ITEMQUALITIES 6
static const ItemQualityInfo s_pHardCodedItemQualities[NUM_HARDCODED_ITEMQUALITIES] = {
	{"White", 15, 0},
	{"Yellow", 30, 0},
	{"Green", 45, 0},
	{"Blue", 60, 0},
	{"Purple", 120, kItemQualityFlag_ReportToSocialNetworks},
	{"Special", 60, kItemQualityFlag_HideFromUILists}};

MP_DEFINE(UsageRestriction);
MP_DEFINE(ItemPowerDefRef);

AUTO_RUN;
void RegisterItemMemoryPools(void)
{
	MP_CREATE_COMPACT(UsageRestriction, 1024, 1024, 0.8);
	MP_CREATE_COMPACT(ItemPowerDefRef, 1024, 1024, 0.8);
}

// Load data-defined Lore JournalTypes
AUTO_STARTUP(ItemQualities) ASTRT_DEPS(PowerVars);
void itemqualities_load(void)
{
	int i, s;
	g_pDefineItemQualities = DefineCreate();

	loadstart_printf("Loading ItemQualities...");
	ParserLoadFiles(NULL, "defs/config/ItemQualities.def", "ItemQualities.bin", PARSER_OPTIONALFLAG, parse_ItemQualities, &g_ItemQualities);
	s = eaSize(&g_ItemQualities.ppQualities);
	for(i=0; i<s; i++)
	{
		DefineAddInt(g_pDefineItemQualities,g_ItemQualities.ppQualities[i]->pchName, i+kItemQuality_FIRST_DATA_DEFINED);
		if (g_ItemQualities.ppQualities[i]->pExprEPValue)
			exprGenerate(g_ItemQualities.ppQualities[i]->pExprEPValue, g_pItemContext); 
	}
	//If no def file was found, load up the hard-coded ones and pretend they came from data.
	if (s == 0)
	{
		for (i = 0; i < NUM_HARDCODED_ITEMQUALITIES; i++)
		{
			DefineAddInt(g_pDefineItemQualities, s_pHardCodedItemQualities[i].pchName, i+kItemQuality_FIRST_DATA_DEFINED);
			eaPush(&g_ItemQualities.ppQualities, StructClone(parse_ItemQualityInfo,&s_pHardCodedItemQualities[i]));
		}
		s = NUM_HARDCODED_ITEMQUALITIES;
	}
	for (i = 0; i < s; i++)
	{
		if (g_ItemQualities.ppQualities[i]->flags & kItemQualityFlag_HideFromUILists)
			break;
	}
	g_ItemQualities.iMaxStandardQuality = i-1;

	loadend_printf("done");
}

// Load data-defined Lore Categories
AUTO_STARTUP(LoreCategories) ASTRT_DEPS(AS_Messages);
void lorecategories_Load(void)
{
	int i,s;
	const char *pchMessageFail = NULL;

	loadstart_printf("Loading LoreCategories...");
	ParserLoadFiles(NULL, "defs/config/LoreCategories.def", "LoreCategories.bin", PARSER_OPTIONALFLAG, parse_LoreCategories, &g_LoreCategories);
	g_pDefineLoreCategories = DefineCreate();
	s = eaSize(&g_LoreCategories.ppCategories);
	for(i=0; i<s; i++)
	{
		DefineAddInt(g_pDefineLoreCategories,g_LoreCategories.ppCategories[i]->pchName, i);
	}
	loadend_printf(" done (%d LoreCategories).", s);

	RegisterNamedStaticDefine(LoreCategoryEnum, "LoreCategory");
	if (pchMessageFail = StaticDefineVerifyMessages(LoreCategoryEnum))
		Errorf("Not all LoreCategory messages were found: %s", pchMessageFail);	
}

// Load data-defined Lore JournalTypes
AUTO_STARTUP(LoreJournalTypes) ASTRT_DEPS(AS_Messages);
void lorejournaltypes_Load(void)
{
	g_pDefineLoreJournalTypes = DefineCreate();
	DefineLoadFromFile(g_pDefineLoreJournalTypes, "LoreJournalType", "LoreJournalType", NULL,  "defs/config/LoreJournalTypes.def", "LoreJournalTypes.bin", kLoreJournalType_FIRST_DATA_DEFINED);

}


AUTO_STARTUP(ItemHandlingConfig) ASTRT_DEPS(InventoryBags);
void ItemHandlingConfig_Load(void)
{
	loadstart_printf("Loading ItemHandlingConfig...");
	StructInit(parse_ItemHandlingConfig, &s_ItemHandlingConfig);
	ParserLoadFiles(NULL, "defs/config/ItemHandlingConfig.def", "ItemHandlingConfig.bin", PARSER_OPTIONALFLAG, parse_ItemHandlingConfig, &s_ItemHandlingConfig);

	// if no tradeable bags specified, default to just main inventory
	if ( ea32Size(&s_ItemHandlingConfig.tradeableBags) == 0 )
	{
		ea32Push(&s_ItemHandlingConfig.tradeableBags, InvBagIDs_Inventory);
		ea32Push(&s_ItemHandlingConfig.tradeableBags, InvBagIDs_PlayerBag1);
		ea32Push(&s_ItemHandlingConfig.tradeableBags, InvBagIDs_PlayerBag2);
		ea32Push(&s_ItemHandlingConfig.tradeableBags, InvBagIDs_PlayerBag3);
		ea32Push(&s_ItemHandlingConfig.tradeableBags, InvBagIDs_PlayerBag4);
		ea32Push(&s_ItemHandlingConfig.tradeableBags, InvBagIDs_PlayerBag5);
		ea32Push(&s_ItemHandlingConfig.tradeableBags, InvBagIDs_PlayerBag6);
		ea32Push(&s_ItemHandlingConfig.tradeableBags, InvBagIDs_PlayerBag7);
		ea32Push(&s_ItemHandlingConfig.tradeableBags, InvBagIDs_PlayerBag8);
		ea32Push(&s_ItemHandlingConfig.tradeableBags, InvBagIDs_PlayerBag9);
	}
	loadend_printf("done.");
}

bool
itemHandling_IsBagTradeable(InvBagIDs bagID)
{
	return ( ea32Find(&s_ItemHandlingConfig.tradeableBags, bagID) >= 0 );
}

const char *
itemHandling_GetErrorMessage(ItemDef *itemDef)
{
	ItemAcquireOverride *itemAcquireOverride;

	itemAcquireOverride = eaIndexedGetUsingInt(&s_ItemHandlingConfig.overrideList, itemDef->eType);

	if ( itemAcquireOverride != NULL )
	{
		if(IS_HANDLE_ACTIVE(itemAcquireOverride->bagFullError))
		{
			return REF_STRING_FROM_HANDLE(itemAcquireOverride->bagFullError);
		}
	}

	return NULL;
}

static InvBagIDs itemAcquireOverride_GetPreferredRestrictBagID(ItemAcquireOverride *itemAcquireOverride, ItemDef *itemDef, bool bReturnFirstIfNotFound)
{
	int iPreferredIdx = -1;
	if (itemAcquireOverride && 
		itemAcquireOverride->ePreferredRestrictBag)
	{
		iPreferredIdx = eaiFind(&itemDef->peRestrictBagIDs, itemAcquireOverride->ePreferredRestrictBag);
	}
	if (iPreferredIdx < 0 && bReturnFirstIfNotFound)
	{
		iPreferredIdx = 0;
	}
	return eaiGet(&itemDef->peRestrictBagIDs, iPreferredIdx);
}

InvBagIDs
itemAcquireOverride_FromStore(ItemDef *itemDef)
{
	ItemAcquireOverride *itemAcquireOverride;

	itemAcquireOverride = eaIndexedGetUsingInt(&s_ItemHandlingConfig.overrideList, itemDef->eType);

	if ((itemAcquireOverride && itemAcquireOverride->fromStore) || 
		(itemDef->flags & kItemDefFlag_LockToRestrictBags))
	{
		return itemAcquireOverride_GetPreferredRestrictBagID(itemAcquireOverride, itemDef, true);
	}

	return InvBagIDs_None;
}

InvBagIDs
itemAcquireOverride_FromMail(ItemDef *itemDef)
{
	ItemAcquireOverride *itemAcquireOverride;

	itemAcquireOverride = eaIndexedGetUsingInt(&s_ItemHandlingConfig.overrideList, itemDef->eType);

	if ((itemAcquireOverride && itemAcquireOverride->fromMail) || 
		(itemDef->flags & kItemDefFlag_LockToRestrictBags))
	{
		return itemAcquireOverride_GetPreferredRestrictBagID(itemAcquireOverride, itemDef, true);
	}

	return InvBagIDs_None;
}

AUTO_TRANS_HELPER_SIMPLE;
InvBagIDs
itemAcquireOverride_FromTrade(ItemDef *itemDef)
{
	ItemAcquireOverride *itemAcquireOverride;

	itemAcquireOverride = eaIndexedGetUsingInt(&s_ItemHandlingConfig.overrideList, itemDef->eType);

	if ((itemAcquireOverride && itemAcquireOverride->fromTrade) || 
		(itemDef->flags & kItemDefFlag_LockToRestrictBags))
	{
		return itemAcquireOverride_GetPreferredRestrictBagID(itemAcquireOverride, itemDef, true);
	}

	return InvBagIDs_None;
}

InvBagIDs
itemAcquireOverride_FromAuction(ItemDef *itemDef)
{
	ItemAcquireOverride *itemAcquireOverride;

	itemAcquireOverride = eaIndexedGetUsingInt(&s_ItemHandlingConfig.overrideList, itemDef->eType);

	if ((itemAcquireOverride && itemAcquireOverride->fromAuction) || 
		(itemDef->flags & kItemDefFlag_LockToRestrictBags))
	{
		return itemAcquireOverride_GetPreferredRestrictBagID(itemAcquireOverride, itemDef, true);
	}

	return InvBagIDs_None;
}

InvBagIDs
itemAcquireOverride_FromGameAction(ItemDef *itemDef)
{
	ItemAcquireOverride *itemAcquireOverride;

	itemAcquireOverride = eaIndexedGetUsingInt(&s_ItemHandlingConfig.overrideList, itemDef->eType);

	if ((itemAcquireOverride && itemAcquireOverride->fromGameAction) || 
		(itemDef->flags & kItemDefFlag_LockToRestrictBags))
	{
		return itemAcquireOverride_GetPreferredRestrictBagID(itemAcquireOverride, itemDef, true);
	}

	return InvBagIDs_None;
}

InvBagIDs
itemAcquireOverride_FromMissionReward(ItemDef *itemDef)
{
	ItemAcquireOverride *itemAcquireOverride;

	itemAcquireOverride = eaIndexedGetUsingInt(&s_ItemHandlingConfig.overrideList, itemDef->eType);

	if ((itemAcquireOverride && itemAcquireOverride->fromMissionReward) || 
		(itemDef->flags & kItemDefFlag_LockToRestrictBags))
	{
		return itemAcquireOverride_GetPreferredRestrictBagID(itemAcquireOverride, itemDef, true);
	}

	return InvBagIDs_None;
}

InvBagIDs
itemAcquireOverride_GetPreferredRestrictBag(ItemDef *itemDef)
{
	ItemAcquireOverride *itemAcquireOverride;
	itemAcquireOverride = eaIndexedGetUsingInt(&s_ItemHandlingConfig.overrideList, itemDef->eType);
	if (itemAcquireOverride)
	{
		return itemAcquireOverride_GetPreferredRestrictBagID(itemAcquireOverride, itemDef, false);
	}
	return InvBagIDs_None;
}

AUTO_STARTUP(UsageRestrictionCategories);
void UsageRestrictionCategories_Load(void)
{
	UsageRestrictionCategories categories = {0};
	S32 i;

	g_pUsageRestrictionCategories = DefineCreate();

	loadstart_printf("Loading Usage Restriction UI Categories... ");

	ParserLoadFiles(NULL, "defs/config/usagerestrictioncategories.def", "usagerestrictioncategories.bin", PARSER_OPTIONALFLAG, parse_UsageRestrictionCategories, &categories);

	for (i = 0; i < eaSize(&categories.pchNames); i++)
		DefineAddInt(g_pUsageRestrictionCategories, categories.pchNames[i], i+1);
	g_iNumUsageRestrictionCategories = i+1;

	StructDeInit(parse_UsageRestrictionCategories, &categories);

	loadend_printf(" done (%d UI Categories).", i);
}

AUTO_STARTUP(UsageRestrictionCategoriesMsgCheck) ASTRT_DEPS(UsageRestrictionCategories);
void UsageRestrictionCategories_MsgCheck(void)
{
	const char* pchMessageFail;

	if (pchMessageFail = StaticDefineVerifyMessages(UsageRestrictionCategoryEnum))
		Errorf("Not all item usage restriction ui category messages were found: %s", pchMessageFail);
}

AUTO_STARTUP(ItemEquipLimitCategories);
void ItemEquipLimitCategories_Load(void)
{
	S32 i;

	g_pItemEquipLimitCategories = DefineCreate();

	loadstart_printf("Item Equip Limit Categories... ");

	ParserLoadFiles(NULL, 
		"defs/config/ItemEquipLimitCategories.def", 
		"ItemEquipLimitCategories.bin", 
		PARSER_OPTIONALFLAG, 
		parse_ItemEquipLimitCategories, 
		&s_EquipLimitCategories);

	for (i = 0; i < eaSize(&s_EquipLimitCategories.eaData); i++)
	{
		ItemEquipLimitCategoryData* pData = s_EquipLimitCategories.eaData[i];
		pData->eCategory = i+1;
		DefineAddInt(g_pItemEquipLimitCategories, pData->pchName, pData->eCategory);
	}

	loadend_printf(" done (%d Categories).", i);
}

ItemEquipLimitCategoryData* item_GetEquipLimitCategory(ItemEquipLimitCategory eCategory)
{
	S32 i;
	for (i = eaSize(&s_EquipLimitCategories.eaData)-1; i >= 0; i--)
	{
		if (s_EquipLimitCategories.eaData[i]->eCategory == eCategory)
		{
			return s_EquipLimitCategories.eaData[i];
		}
	}
	return NULL;
}

// if the power category is out of this range, then it doesn't correspond to a power, but rather it's something
// special that we are just going to handle, like the power factor
bool itempower_IsRealPower(ItemPowerDef * pItemPowerDef)
{
	return !(pItemPowerDef->eItemPowerCategories & kItemPowerCategory_PowerFactor);
}

bool itempower_ValidateReferences(ItemPowerDef *pDef)
{
	bool retcode = true;

	if (!GET_REF(pDef->displayNameMsg.hMessage) && REF_STRING_FROM_HANDLE(pDef->displayNameMsg.hMessage))
	{
		ErrorFilenamef( pDef->pchFileName, "Item power refers to non-existent message '%s'", REF_STRING_FROM_HANDLE(pDef->displayNameMsg.hMessage));
		retcode = false;
	}

	if (!GET_REF(pDef->displayNameMsg2.hMessage) && REF_STRING_FROM_HANDLE(pDef->displayNameMsg2.hMessage))
	{
		ErrorFilenamef( pDef->pchFileName, "Item power refers to non-existent message '%s'", REF_STRING_FROM_HANDLE(pDef->displayNameMsg2.hMessage));
		retcode = false;
	}

	if (!GET_REF(pDef->descriptionMsg.hMessage) && REF_STRING_FROM_HANDLE(pDef->descriptionMsg.hMessage))
	{
		ErrorFilenamef( pDef->pchFileName, "Item power refers to non-existent message '%s'", REF_STRING_FROM_HANDLE(pDef->descriptionMsg.hMessage));
		retcode = false;
	}

	if (itempower_IsRealPower(pDef))
	{
		// Ensure power ref is set
		if (!REF_DATA_FROM_HANDLE(pDef->hPower)) 
		{
			ErrorFilenamef( pDef->pchFileName, "ItemPower has no power" );
			retcode = false;
		}

		if (!GET_REF(pDef->hPower) && REF_STRING_FROM_HANDLE(pDef->hPower))
		{
			ErrorFilenamef( pDef->pchFileName, "Item power refers to non-existent power '%s'", REF_STRING_FROM_HANDLE(pDef->hPower));
			retcode = false;
		}
	}

	if (!GET_REF(pDef->hPowerReplace) && REF_STRING_FROM_HANDLE(pDef->hPowerReplace))
	{
		ErrorFilenamef( pDef->pchFileName, "Item power refers to non-existent power replace '%s'", REF_STRING_FROM_HANDLE(pDef->hPowerReplace));
		retcode = false;
	}

	if (!GET_REF(pDef->hCraftRecipe) && REF_STRING_FROM_HANDLE(pDef->hCraftRecipe))
	{
		ErrorFilenamef( pDef->pchFileName, "Item power refers to non-existent craft recipe '%s'", REF_STRING_FROM_HANDLE(pDef->hCraftRecipe));
		retcode = false;
	}

	if(pDef->flags & kItemPowerFlag_LocalEnhancement && GET_REF(pDef->hPower) && GET_REF(pDef->hPower)->eType!=kPowerType_Enhancement)
	{
		ErrorFilenamef( pDef->pchFileName, "Item power flagged as local enhancement but power '%s' is not an Enhancement", REF_STRING_FROM_HANDLE(pDef->hPower));
		retcode = false;
	}
	
	{
		ItemDef *pRecipe = GET_REF(pDef->hCraftRecipe);
		if (pRecipe)
		{
			ItemCraftingTable *pCraft = pRecipe->pCraft;

			if (pRecipe->eType != kItemType_ItemPowerRecipe)
			{
				ErrorFilenamef( pDef->pchFileName, "Item power craft recipe has invalid type");
				retcode = false;
			}

			if (!pCraft)
			{
				ErrorFilenamef( pDef->pchFileName, "Item power craft recipe has no crafting table");
				retcode = false;
			}
		}
	}

	if (!GET_REF(pDef->hValueRecipe) && REF_STRING_FROM_HANDLE(pDef->hValueRecipe))
	{
		ErrorFilenamef( pDef->pchFileName, "Item power refers to non-existent value recipe '%s'", REF_STRING_FROM_HANDLE(pDef->hValueRecipe));
		retcode = false;
	}
	
	return retcode;
}

bool itempower_Validate(ItemPowerDef *pDef)
{
	const char *pchTempFileName;
	char *pchPath = NULL;
	bool retcode = true;

	if( !resIsValidName(pDef->pchName) )
	{
		ErrorFilenamef( pDef->pchFileName, "ItemPower name is illegal: '%s'", pDef->pchName );
		retcode = false;
	}

	pchTempFileName = pDef->pchFileName;
	if (resFixPooledFilename(&pchTempFileName, GameBranch_GetDirectory(&pchPath,ITEMPOWERS_BASE_DIR), pDef->pchScope, pDef->pchName, ITEMPOWERS_EXTENSION)) 
	{
		if (IsServer()) {
			ErrorFilenamef( pDef->pchFileName, "Item power filename does not match name '%s' scope '%s'", pDef->pchName, pDef->pchScope);
			retcode = false;
		}
	}

	if( !resIsValidScope(pDef->pchScope) )
	{
		ErrorFilenamef( pDef->pchFileName, "ItemPower scope is illegal: '%s'", pDef->pchScope );
		retcode = false;
	}

	//generate the requires expression
	if ( pDef->pRestriction &&
		pDef->pRestriction->pRequires )
	{
		if ( !exprGenerate(pDef->pRestriction->pRequires, g_pItemContext) )
		{
			retcode = false;
		}
	}

	// generate the econ points expression
	if (pDef->pExprEconomyPoints)
	{
		if (!exprGenerate(pDef->pExprEconomyPoints, g_pItemContext))
		{
			retcode = false;
		}
	}

	if (itempower_IsRealPower(pDef))
	{
		if (!IS_HANDLE_ACTIVE(pDef->hPower))
		{
			ErrorFilenamef(pDef->pchFileName, "ItemPower does not have a power ref");
			retcode = false;
		}

#ifdef GAMESERVER

		if (!GET_REF(pDef->hPower))
		{
			ErrorFilenamef(pDef->pchFileName, "ItemPower references unknown power def: '%s'", REF_STRING_FROM_HANDLE(pDef->hPower));
			retcode = false;
		}
		if(pDef->pPowerConfig)
		{
			if(pDef->pPowerConfig->pExprAIRequires)
				aiPowersGenerateConfigExpression(pDef->pPowerConfig->pExprAIRequires);
			if(pDef->pPowerConfig->pExprAIEndCondition)
				aiPowersGenerateConfigExpression(pDef->pPowerConfig->pExprAIEndCondition);
			if(pDef->pPowerConfig->pExprAIChainRequires)
				aiPowersGenerateConfigExpression(pDef->pPowerConfig->pExprAIChainRequires);
			if(pDef->pPowerConfig->pExprAITargetOverride)
				aiPowersGenerateConfigExpression(pDef->pPowerConfig->pExprAITargetOverride);
			if(pDef->pPowerConfig->pExprAIWeightModifier)
				aiPowersGenerateConfigExpression(pDef->pPowerConfig->pExprAIWeightModifier);
			if(pDef->pPowerConfig->aiPowerConfigDefInst)
				aiPowerConfigDefGenerateExprs(pDef->pPowerConfig->aiPowerConfigDefInst);
			if(pDef->pPowerConfig->pExprAICureRequires)
				aiPowersGenerateConfigExpression(pDef->pPowerConfig->pExprAICureRequires);
		}
#endif
	}

	estrDestroy(&pchPath);

	return retcode;
}

AUTO_TRANS_HELPER;
void item_trh_FillInPreSlottedGems(ATH_ARG NOCONST(Item) *pItem, int iCharacterLevel, const char *pcRank, AllegianceDef *pAllegiance, U32 *pSeed)
{
	int i;
	ItemDef *pItemDef;

	if(ISNULL(pItem))
		return;

	pItemDef = GET_REF(pItem->hItem);

	for(i=0;i<eaSize(&pItemDef->ppItemGemSlots);i++)
	{
		NOCONST(Item) *pGemItem = inv_ItemInstanceFromDefName(REF_STRING_FROM_HANDLE(pItemDef->ppItemGemSlots[i]->hPreSlottedGem),1,0,NULL,NULL,NULL,false,NULL);

		if(pGemItem)
		{
			inv_trh_GemItem(pItem,pGemItem,i);
		}
	}
}

AUTO_TRANS_HELPER;
void item_trh_FixupAlgoProps(ATH_ARG NOCONST(Item)* pItem)
{
	ItemDef* pItemDef = SAFE_GET_REF(pItem, hItem);
	if (NONNULL(pItem) && NONNULL(pItem->pAlgoProps) && pItemDef)
	{
		if (ISNULL(pItem->pAlgoProps->pDyeData) &&
			pItem->pAlgoProps->iPowerFactor == pItemDef->iPowerFactor &&
			pItem->pAlgoProps->Level_UseAccessor == pItemDef->iLevel &&
			((pItemDef->pRestriction && pItem->pAlgoProps->MinLevel_UseAccessor == pItemDef->pRestriction->iMinLevel) ||
			pItem->pAlgoProps->MinLevel_UseAccessor <= 1) &&
			pItem->pAlgoProps->Quality == pItemDef->Quality &&
			eaSize(&pItem->pAlgoProps->ppItemPowerDefRefs) <= 0)
		{
			StructDestroyNoConst(parse_AlgoItemProps, pItem->pAlgoProps);
			pItem->pAlgoProps = NULL;
		}
	}
}

//Returns the number of slotted gems.
AUTO_TRANS_HELPER;
int item_trh_FixupItemGemSlots(ATH_ARG NOCONST(Item)* pItem)
{
	ItemDef* pItemDef = SAFE_GET_REF(pItem, hItem);
	int iValidGems = 0;
	int i;
	if (NONNULL(pItem) && NONNULL(pItem->pSpecialProps) && NONNULL(pItem->pSpecialProps->ppItemGemSlots))
	{
		//Delete gem slots if we can
		for (i = eaSize(&pItem->pSpecialProps->ppItemGemSlots)-1; i >= 0; i--)
		{
			if (GET_REF(pItem->pSpecialProps->ppItemGemSlots[i]->hSlottedItem))
				iValidGems++;
			else if (iValidGems == 0)//if we haven't found any valid gems behind us, we can delete empty slots.
			{
				StructDestroyNoConst(parse_ItemGemSlot, pItem->pSpecialProps->ppItemGemSlots[i]);
				eaRemoveFast(&pItem->pSpecialProps->ppItemGemSlots, i);
			}
		}
		if (iValidGems <= 0)
		{
			eaDestroyStructNoConst(&pItem->pSpecialProps->ppItemGemSlots, parse_ItemGemSlot);
			pItem->pSpecialProps->ppItemGemSlots = NULL;
		}
	}
	
	return iValidGems;
}

AUTO_TRANS_HELPER;
void item_trh_FixupSpecialProps(ATH_ARG NOCONST(Item)* pItem)
{
	ItemDef* pItemDef = SAFE_GET_REF(pItem, hItem);
	if (NONNULL(pItem) && NONNULL(pItem->pSpecialProps) && pItemDef)
	{
		if (item_trh_FixupItemGemSlots(pItem) <= 0)
		{
			if (!GET_REF(pItem->pSpecialProps->hCostumeRef) &&
				ISNULL(pItem->pSpecialProps->pAlgoPet) &&
				ISNULL(pItem->pSpecialProps->pContainerInfo) &&
				ISNULL(pItem->pSpecialProps->pDoorKey) &&
				ISNULL(pItem->pSpecialProps->pSuperCritterPet) &&
				ISNULL(pItem->pSpecialProps->pTransmutationProps) &&
				!GET_REF(pItem->pSpecialProps->hIdentifiedItemDef))
			{
				StructDestroyNoConst(parse_SpecialItemProps, pItem->pSpecialProps);
				pItem->pSpecialProps = NULL;
			}
		}
	}
}

AUTO_TRANS_HELPER
ATR_LOCKS(pItem, ".Hitem, .Pppowers, .Pspecialprops, .Palgoprops.Ppitempowerdefrefs, .Palgoprops.Level_Useaccessor");
bool item_trh_FixupPowers(ATH_ARG NOCONST(Item) *pItem)
{
	int iItemPowerDef, iPower, iPowerOffset = 0;
	int iNumItemPowerDefs;
	int iGemPowerDefsStart;
	ItemDef *pItemDef;
	bool retval = true;
	NOCONST(Power) ***pppCurrentPowersList;

	if(ISNULL(pItem))
		return false;

	pItemDef = GET_REF(pItem->hItem);

	if(!pItemDef)
	{
		// This item doesn't even have an ItemDef anymore.  In theory we could delete all the Powers,
		//  but the Character won't get the Powers anyway (since there's no ItemDef), so they're
		//  deadweight but not harmful.
		return false;
	}

	// Loop over all the ItemPowerDefs
	iNumItemPowerDefs = item_trh_GetNumItemPowerDefs(pItem, true);
	iGemPowerDefsStart = item_trh_GetNumItemPowerDefs(pItem, true) - item_trh_GetNumGemsPowerDefs(pItem);
	pppCurrentPowersList = &pItem->ppPowers;
	for(iItemPowerDef = iPower = 0; iItemPowerDef<iNumItemPowerDefs; iItemPowerDef++)
	{
		ItemPowerDefRef * pItemPowerDefRef = item_trh_GetItemPowerDefRef(pItem, iItemPowerDef);
		ItemPowerDef *pItemPowerDef = pItemPowerDefRef ? GET_REF(pItemPowerDefRef->hItemPowerDef) : NULL;
		S32 iCurrentItemGemSlot = -1;
		bool bRollFailed = false;
		PowerDef *pPowerDef;

		if(iItemPowerDef >= iGemPowerDefsStart && pItem->pSpecialProps)
		{
			//Find which gem slot we are in
			int iSlot = 0;

			iPowerOffset = iGemPowerDefsStart;

			for(iSlot=0;iSlot<eaSize(&pItem->pSpecialProps->ppItemGemSlots);iSlot++)
			{
				ItemDef *pGemDef = GET_REF(pItem->pSpecialProps->ppItemGemSlots[iSlot]->hSlottedItem);

				if(pGemDef)
				{
					int iGemPowers = eaSize(&pGemDef->ppItemPowerDefRefs);

					if(iGemPowers + iPowerOffset > iItemPowerDef)
					{
						iCurrentItemGemSlot = iSlot;
						pppCurrentPowersList = &pItem->pSpecialProps->ppItemGemSlots[iSlot]->ppPowers;
						break;
					}
					else
					{
						iPowerOffset+=iGemPowers;
					}
				}

			}
		}

		if (!pItemPowerDef)
		{
			retval = false;
			continue;
		}

		pPowerDef = GET_REF(pItemPowerDef->hPower);

		if (!pPowerDef)
		{
			retval = false;
			continue;
		}
		if (iItemPowerDef >= iGemPowerDefsStart && // Gem power
			pItemPowerDefRef &&
			iCurrentItemGemSlot >= 0 &&
			pItemPowerDefRef->fGemSlottingApplyChance < 1.f)
		{
			S32 iRollIndex;
			if ((ISNULL(pItem->pSpecialProps->ppItemGemSlots[iCurrentItemGemSlot]->pRollData) || 
				eaIndexedFindUsingInt(&pItem->pSpecialProps->ppItemGemSlots[iCurrentItemGemSlot]->pRollData->ppRollResults, pItemPowerDefRef->uID) == -1)) // Requires roll
			{
				// Roll and persist the result
				NOCONST(ItemGemSlotRollResult) * pResult = StructCreateNoConst(parse_ItemGemSlotRollResult);
				pResult->uItemPowerDefRefID = pItemPowerDefRef->uID;
				pResult->bSuccess = pItemPowerDefRef->fGemSlottingApplyChance - randomPositiveF32() <= 0.f;

				if (ISNULL(pItem->pSpecialProps->ppItemGemSlots[iCurrentItemGemSlot]->pRollData))
				{
					pItem->pSpecialProps->ppItemGemSlots[iCurrentItemGemSlot]->pRollData = StructCreateNoConst(parse_ItemGemSlotRollData);
				}
				eaIndexedAdd(&pItem->pSpecialProps->ppItemGemSlots[iCurrentItemGemSlot]->pRollData->ppRollResults, pResult);
			}

			iRollIndex = eaIndexedFindUsingInt(&pItem->pSpecialProps->ppItemGemSlots[iCurrentItemGemSlot]->pRollData->ppRollResults, pItemPowerDefRef->uID);

			if (iRollIndex >= 0)
			{
				bRollFailed = !pItem->pSpecialProps->ppItemGemSlots[iCurrentItemGemSlot]->pRollData->ppRollResults[iRollIndex]->bSuccess;
			}
		}

		if(pPowerDef && pPowerDef->eType!=kPowerType_Innate && !bRollFailed)
		{
			// ItemPowerDef that refers to a non-Innate Power, so the next actual
			//  Power instance should match
			NOCONST(Power) *pPower;
			S32 bMatchCheck = true;
			S32 bMatchFound = false;

			while(bMatchCheck)
			{
				bMatchCheck = false;
				pPower = eaGet(pppCurrentPowersList, iPower - iPowerOffset);
				if(pPower && GET_REF(pPower->hDef)==pPowerDef)
				{
					// The next Power in the list matches the ItemPowerDef, note that
					//  and break from match checking
					bMatchFound = true;
					break;
				}
				else if(pPower)
				{
					// The next Power in the list wasn't a match, see if a later ItemPowerDef matches
					int iItemPowerDefLater;
					for(iItemPowerDefLater = iItemPowerDef+1; iItemPowerDefLater<iNumItemPowerDefs; iItemPowerDefLater++)
					{
						ItemPowerDef *pItemPowerDefLater = item_trh_GetItemPowerDef(pItem, iItemPowerDefLater);
						if(pItemPowerDefLater)
						{
							PowerDef *pPowerDefLater = GET_REF(pItemPowerDefLater->hPower);
							if(pPowerDefLater && pPowerDefLater->eType!=kPowerType_Innate && GET_REF(pPower->hDef)==pPowerDefLater)
								break;
						}
					}

					if(iItemPowerDefLater>=iNumItemPowerDefs)
					{
						// The non-matching Power we found on the Item doesn't have a match
						//  in any later ItemPowerDefs, so we're just going to delete it
						//  and try matching again
						eaRemove(pppCurrentPowersList, iPower - iPowerOffset);
						StructDestroyNoConst(parse_Power, pPower);
						bMatchCheck = true;
					}
				}
			}

			if(!bMatchFound)
			{
				// At this point we're either at the end of the list of Powers on the Item, or
				//  the Power at iPower on the Item didn't match but will show up later.  Either
				//  way we want to insert a new Power at iPower.
				pPower = CONTAINER_NOCONST(Power, power_Create(pPowerDef->pchName));
				power_InitHelper(pPower,item_trh_GetLevel(pItem));
				eaInsert(pppCurrentPowersList,pPower,iPower - iPowerOffset);

				// This is stupid
				if(IS_HANDLE_ACTIVE(pItemPowerDef->hPowerReplace))
				{
					pPower->bHideInUI = true;
				}
			}

			iPower++;
		}
		
	}

	// We've made sure the first iPower Powers on the Item match the entirety of the Item's ItemPowerDefs.
	//  If we have any Powers left over, we need to delete them.
	while(eaSize(&pItem->ppPowers) > iPower)
	{
		NOCONST(Power) *pPower = eaPop(&pItem->ppPowers);
		StructDestroyNoConst(parse_Power, pPower);
	}
	return retval;
}

AUTO_TRANS_HELPER
ATR_LOCKS(pItem, ".Pppowers");
void item_trh_RemovePowers(ATH_ARG NOCONST(Item)* pItem)
{
	if(NONNULL(pItem))
	{
		eaDestroyStructNoConst(&pItem->ppPowers, parse_Power);
	}
}

AUTO_TRANS_HELPER
ATR_LOCKS(pItem, ".Pppowers");
void item_trh_ResetPowerLifetimes(ATH_ARG NOCONST(Item)* pItem)
{
	int i;
	for (i = 0; i < eaSize(&pItem->ppPowers); i++)
	{
		NOCONST(Power) *pPower = eaGet(&pItem->ppPowers, i);
		if (pPower)
			pPower->uiTimeCreated = timeSecondsSince2000();
	}
}


/* These two functions have inv_lite_trh_ counterparts. */
AUTO_TRANS_HELPER
	ATR_LOCKS(pItem, ".count, .Hitem");
bool item_trh_SimpleClampLow(ATH_ARG NOCONST(Item)* pItem, ItemDef *pDef)
{
	if(!pDef)
	{
		pDef = NONNULL(pItem) ? GET_REF(pItem->hItem) : NULL;
	}

	if(ISNULL(pItem) || !pDef)
		return false;

	// do a low limit check on the numeric
	if (pItem->count < pDef->MinNumericValue)
	{
		if (pDef->flags & kItemDefFlag_TransFailonLowLimit)
			return false;
		else
			pItem->count = pDef->MinNumericValue;
	}

	return true;
}

AUTO_TRANS_HELPER
	ATR_LOCKS(pItem, ".count, .Hitem");
bool item_trh_SimpleClampHigh(ATH_ARG NOCONST(Item)* pItem, ItemDef *pDef, int *piOverflow)
{
	if(!pDef)
	{
		pDef = NONNULL(pItem) ? GET_REF(pItem->hItem) : NULL;
	}

	if(ISNULL(pItem) || !pDef)
		return false;

	if(pItem->count > pDef->MaxNumericValue)
	{
		if(piOverflow)
			(*piOverflow) = pItem->count - pDef->MaxNumericValue;
		if (pDef->flags & kItemDefFlag_TransFailonHighLimit)
			return false;
		else
			pItem->count = pDef->MaxNumericValue;
	}

	return true;
}

AUTO_TRANS_HELPER_SIMPLE;
ItemDef* item_DefFromName(const char *pchName)
{
	if (pchName)
		return RefSystem_ReferentFromString(g_hItemDict, pchName);
	return NULL;
}

bool item_MatchAnyRestrictBagIDs(SA_PARAM_NN_VALID ItemDef* pItemDef, InvBagIDs* peRestrictBagIDs)
{
	int i;
	for (i = 0; i < eaiSize(&peRestrictBagIDs); i++)
	{
		if (eaiFind(&pItemDef->peRestrictBagIDs, peRestrictBagIDs[i]) >= 0)
		{
			return true;
		}
	}
	return false;
}

// Returns a Power owned by the Player's inventory, otherwise returns NULL
AUTO_TRANS_HELPER
	ATR_LOCKS(pEnt, ".pInventoryV2.Ppinventorybags, pInventoryV2.pplitebags[], pPlayer.pPlayerAccountData.eaGamePermissionMaxValueNumerics[]");
NOCONST(Power)* item_trh_FindPowerByID(ATH_ARG NOCONST(Entity)* pEnt, U32 uiID, InvBagIDs *pBagID, int *pSlotIdx, Item **ppItem, S32 *piItemPowerIdx)
{
	int iBag;

	if ( ISNULL(pEnt) || ISNULL(pEnt->pInventoryV2) )
		return NULL;

	// Look over all items in all bags on this ent
	// This basically locks every Item in every InventoryBag, so no point in using NOCONST stuff afterwords
	for(iBag = eaSize(&pEnt->pInventoryV2->ppInventoryBags)-1; iBag >=0; iBag--)
	{
		InventoryBag *bag = (InventoryBag*)pEnt->pInventoryV2->ppInventoryBags[iBag];
		BagIterator *iter;
		
		if(!bag)
			continue;
		
		iter = invbag_trh_IteratorFromEnt(ATR_EMPTY_ARGS, pEnt,bag->BagID, NULL);
		for(; !bagiterator_Stopped(iter); bagiterator_Next(iter))
		{
			NOCONST(Item) *pItem = bagiterator_GetItem(iter);
			ItemDef *pItemDef = bagiterator_GetDef(iter);
			S32 iPower;
			int NumPowers = item_GetNumItemPowerDefsNoConst(pItem, true);

			if ( !pItemDef )
				continue;

			for(iPower=NumPowers-1; iPower>=0; iPower--)
			{
				ItemPowerDef *pItemPowerDef = item_GetItemPowerDefNoConst(pItem, iPower);
				NOCONST(Power)* ppow = item_trh_GetPower(pItem, iPower);

				if ( !pItemPowerDef || !ppow )
					continue;

				if(ppow->uiID == uiID)
				{
					if(pBagID) *pBagID = bag->BagID;
					if(pSlotIdx) *pSlotIdx = iter->i_cur; //eaIndexedFindUsingString(&bag->ppIndexedInventorySlots, pEntry->pSlot->pchName); AB NOTE: check this line
					if(ppItem) *ppItem = (Item*)pItem;
					if(piItemPowerIdx) *piItemPowerIdx = iPower;

					bagiterator_Destroy(iter);
					return ppow;
				}
			}
		}
		bagiterator_Destroy(iter);
	}

	return NULL;
}


static void ItemPowerReplaceHelper(SA_PARAM_NN_VALID Character *pchar,
								   SA_PARAM_NN_VALID Item *pItem,
								   SA_PARAM_NN_VALID ItemDef *pItemDef,
								   SA_PARAM_NN_VALID Power *ppow, 
								   int bRemove)
{
	int i;
	U32 uiReplaceID;
	PowerDef *ppowdef;
	int NumPowers;

	if ( !pchar ||
		 !pItem || 
		 !pItemDef )
		 return;

	uiReplaceID = bRemove ? 0 : ppow->uiID;
	ppowdef = GET_REF(ppow->hDef);
	NumPowers = item_GetNumItemPowerDefs(pItem, true);

	for(i=NumPowers-1; i>=0; i--)
	{
		ItemPowerDef *pItemPowerDef = item_GetItemPowerDef(pItem, i);

		if (!pItemPowerDef)
			continue;

		if( ppowdef == GET_REF(pItemPowerDef->hPower) && 
			IS_HANDLE_ACTIVE(pItemPowerDef->hPowerReplace))
		{
			int j;
			PowerReplaceDef *pSlot = GET_REF(pItemPowerDef->hPowerReplace);
			for(j=eaSize(&pchar->pEntParent->ppPowerReplaces)-1;j>=0;j--)
			{
				if(pSlot == GET_REF(pchar->pEntParent->ppPowerReplaces[j]->hDef))
				{
					Power *pPower = character_FindPowerByID(pchar,pchar->pEntParent->ppPowerReplaces[j]->uiBasePowerID);

					if(pPower)
					{
						if(ppowdef && ppowdef->eType == kPowerType_Enhancement)
						{
							if(bRemove)
								eaFindAndRemove(&pchar->pEntParent->ppPowerReplaces[j]->ppEnhancements,ppow);
							else
								eaPush(&pchar->pEntParent->ppPowerReplaces[j]->ppEnhancements,ppow);
						}else{
							pchar->pEntParent->ppPowerReplaces[j]->uiReplacePowerID = uiReplaceID;
							power_SetPowerReplacementID(pPower,uiReplaceID);
						}
						ppow->bIsReplacing = !bRemove;
						break;
					}
					else
					{
						// TODO(JW): PowerReplace: Not real sure what this code is doing anymore,
						//  so I'm not sure what the proper response is to this being NULL
					}
				}
			}
		}
	}
}


/* This function in here to help debug an issue where an entity ends up with
two items that have the same id. 
Stephen sez: there is a key part of this function that must not be called on every item transaction of every sort (but we don't have to remove it)
*/

AUTO_TRANS_HELPER
ATR_LOCKS(pEnt, ".pInventoryV2.Ppinventorybags, pInventoryV2.ppLiteBags[], pPlayer.pPlayerAccountData.eaGamePermissionMaxValueNumerics[]")
ATR_LOCKS(pItem, ".*");
bool item_idCheck(ATH_ARG NOCONST(Entity) *pEnt, ATH_ARG NOCONST(Item) *pItem, GameAccountDataExtract *pExtract)
{
	U64 uiItemID = pItem->id;
	int ii, NumBags;

	if(ISNULL(pEnt) || ISNULL(pItem))
		return true;

	NumBags = eaSize(&pEnt->pInventoryV2->ppInventoryBags);

	for (ii=0; ii<NumBags; ii++)
	{
		InventoryBag *bag = (InventoryBag*)pEnt->pInventoryV2->ppInventoryBags[ii];
		BagIterator *iter;

		if(!bag)
			continue;
		
		iter = invbag_trh_IteratorFromEnt(ATR_EMPTY_ARGS, pEnt,bag->BagID, pExtract);
		for(; !bagiterator_Stopped(iter); bagiterator_Next(iter))
		{
			NOCONST(Item)* item = bagiterator_GetItem(iter);
			ItemDef *pItemDef = bagiterator_GetDef(iter);

			if ( item && item->id == uiItemID)
			{
				if (item != pItem) {
					ErrorDetailsf("Entity %s already has ID %lld, ItemDefA %s, ItemDefB %s", pEnt->debugName,uiItemID,
						REF_STRING_FROM_HANDLE(pItem->hItem),REF_STRING_FROM_HANDLE(item->hItem));
					Errorf("devassert: New item for entity shares same ID as one that already exists!  Entity may be corrupted!");
					if (isDevelopmentMode()) {
						assertmsgf(item == pItem,"New item for entity %s, but shares the same ID as one that already exists! (itemID=%lld)",pEnt->debugName,uiItemID);
					}
				}

				if(item != pItem)
				{
					bagiterator_Destroy(iter);
					return false;
				}
			}
		}
		bagiterator_Destroy(iter);
	}

	return true;
}

// Set a unique ID on an item instance
AUTO_TRANS_HELPER
	ATR_LOCKS(pEnt, ".Itemidmax")
	ATR_LOCKS(pItem, ".Id");
void item_trh_SetItemID(ATH_ARG NOCONST(Entity) *pEnt, ATH_ARG NOCONST(Item) *pItem)
{
	U64 tmpID = 0;
	U32 IDtype = 0;

	if ( ISNULL(pEnt) ||
		 ISNULL(pItem) )
		 return;

	//an ID is already set, 
	if(pItem->id)
		return;

	switch (pEnt->myEntityType)
	{
	case GLOBALTYPE_ENTITYPLAYER:
		IDtype = kItemIDType_Player;
		break;

	case GLOBALTYPE_ENTITYSAVEDPET:
		IDtype = kItemIDType_SavedPet;
		break;

	case GLOBALTYPE_ENTITYPUPPET:
		IDtype = kItemIDType_Puppet;
		break;

	case GLOBALTYPE_ENTITYCRITTER:
		IDtype = kItemIDType_Critter;
		break;

	case GLOBALTYPE_ENTITYSHAREDBANK:
		IDtype = kItemIDType_SharedBank;
		break;

	default:
		return;  //unsupported entity type
	}
	
	// Items generated for critters should never have a container ID set
	if (pEnt->myEntityType != GLOBALTYPE_ENTITYCRITTER)
	{
		tmpID = pEnt->myContainerID;
	}
	tmpID <<= 32;  //shift container ID into upper 32 bits of ID

	assertmsg((IDtype & 7) == IDtype, "ID type must fit in three bits");
	IDtype <<= 29; //shift container type (subset) into upper 3 bits of lower 32 bits
	tmpID |= IDtype;

	pEnt->ItemIDMax++;  //bump the max ID
	tmpID |= (pEnt->ItemIDMax & 0x1fffffff);

	pItem->id = tmpID;
}

ItemIDType item_GetIDTypeFromID(U64 uItemID)
{
	ItemIDType uType = (uItemID >> 29);
	uType = (uType & 7);
	return uType;
}

void DEFAULT_LATELINK_GameSpecific_HandleInventoryChangeNumeric(Entity* pEnt, const char* pchNumeric, bool bPreCommit)
{
}

void item_HandleInventoryChangeNumeric(Entity* pEnt, const char* pchNumeric, bool bPreCommit)
{
	GameSpecific_HandleInventoryChangeNumeric(pEnt, pchNumeric, bPreCommit);

	if(pEnt->pChar)
	{
		// Numeric items can now drive PowerStats, which are accrued directly into the final innate accrual
		character_DirtyInnateAccrual(pEnt->pChar);
		character_DirtyItems(pEnt->pChar);
	}
}

void item_HandleInventoryChangeLevelNumeric(Entity* pEnt, bool bPreCommit)
{
#ifdef GAMESERVER
	if(pEnt->pChar)
	{
		//Update the character's remote contact list
		contact_entRefreshRemoteContactList(pEnt);
	}
#endif
}

//clear all item power IDs 
AUTO_TRANS_HELPER
ATR_LOCKS(pItem, ".Pppowers");
void item_trh_ClearPowerIDs( ATH_ARG NOCONST(Item)*pItem )
{
	int iPower;

	if ( ISNULL(pItem))
		return;

	for(iPower=eaSize(&pItem->ppPowers)-1; iPower>=0; iPower--)
	{
		NOCONST(Power)* ppow = pItem->ppPowers[iPower];

		if (!ppow) continue;  //just to be safe

		//set the power ID to 0
		power_SetIDHelper(ppow, 0);
	}
}


// Make sure all Powers on the Item have non-zero IDs
AUTO_TRANS_HELPER
ATR_LOCKS(pEnt, ".Pchar.Uipoweridmax, .Psaved.Ipetid, .Psaved.Ppuppetmaster.Curid, .Psaved.Ppuppetmaster.Curtempid, .Psaved.Ppuppetmaster.Curtype, .Psaved.Ppownedcontainers, .Psaved.Ppallowedcritterpets")
ATR_LOCKS(pItem, ".Pppowers, .pSpecialProps.ppitemgemslots");
void item_trh_FixupPowerIDs(ATH_ARG NOCONST(Entity) *pEnt, ATH_ARG NOCONST(Item)*pItem)
{
	int i, s, g, gs;

	if(ISNULL(pEnt) || ISNULL(pEnt->pChar) || ISNULL(pItem))
		return;

	s=eaSize(&pItem->ppPowers);
	for(i=0; i<s; i++)
	{
		NOCONST(Power)* ppow = pItem->ppPowers[i];

		if(!ppow)
			continue;  //just to be safe

		if(!ppow->uiID)
		{
			U32 uiID = entity_GetNewPowerIDHelper(pEnt);
			power_SetIDHelper(ppow, uiID);
		}
	}

	gs = NONNULL(pItem->pSpecialProps) ? eaSize(&pItem->pSpecialProps->ppItemGemSlots) : 0;

	for(g=0;g<gs;g++)
	{
		s=eaSize(&pItem->pSpecialProps->ppItemGemSlots[g]->ppPowers);
		for(i=0;i<s;i++)
		{
			NOCONST(Power)* ppow = pItem->pSpecialProps->ppItemGemSlots[g]->ppPowers[i];

			if(!ppow)
				continue;  //just to be safe

			if(!ppow->uiID)
			{
				U32 uiID = entity_GetNewPowerIDHelper(pEnt);
				power_SetIDHelper(ppow, uiID);
			}
		}
	}
}


// Walks the Players's inventory and adds all the available Powers to the Character's general Powers list
//  Returns false if it had trouble because of missing references.
// See the function AccrueBag if you want to see how Innates are handled on items
int item_AddPowersFromItems(int iPartitionIdx, Entity *pEnt, GameAccountDataExtract *pExtract)
{
	Character* pChar = NULL;
	int iBag;
	int iCharLevelControlled = 0;
	int r = true;
	PowerDef *pdefStance = NULL;
	int iLevelXP = 0;
	int iLevelAdjustmentGlobal = 0;
	int bItemLevelShift = false;
	
	if (!pEnt || !pEnt->pInventoryV2)
		return r;
	
	pChar = pEnt->pChar;

	if (!pChar)
		return r;

	if (pEnt->pSaved && pEnt->pSaved->conOwner.containerID)
	{
		GlobalType eOwnerType = pEnt->pSaved->conOwner.containerType;
		ContainerID uOwnerID = pEnt->pSaved->conOwner.containerID;
		Entity* pOwner = entFromContainerID(iPartitionIdx, eOwnerType, uOwnerID);
		if (pOwner)
		{
			iLevelXP = entity_GetSavedExpLevel(pOwner);
		}
	}
	else
	{
		iLevelXP = entity_GetSavedExpLevel(pEnt);
	}

	if(pChar->pLevelCombatControl)
	{
		iCharLevelControlled = pChar->iLevelCombat;
		if(g_CombatConfig.pLevelCombatControl && g_CombatConfig.pLevelCombatControl->bItemLevelShift)
		{
			iLevelAdjustmentGlobal = iCharLevelControlled - iLevelXP;
			bItemLevelShift = true;
		}
	}

	for(iBag = eaSize(&pEnt->pInventoryV2->ppInventoryBags)-1; iBag>=0; iBag--)
	{
		InventoryBag *bag = pEnt->pInventoryV2->ppInventoryBags[iBag];
		BagIterator *iter;

		if(!bag)
			continue;
		
		iter = invbag_IteratorFromEnt(pEnt,bag->BagID,pExtract);
		for(; !bagiterator_Stopped(iter); bagiterator_Next(iter))
		{
			Item* pItem = (Item*)bagiterator_GetItem(iter);
			ItemDef *pItemDef = pItem ? bagiterator_GetDef(iter) : NULL;
			S32 iPower;
			int NumPowers = item_GetNumItemPowerDefs(pItem, true);
			int iLevelAdjustment = 0;
			int iMinItemLevel = item_GetMinLevel(pItem);

			if (!pItemDef)
			{
				// If we had an item instance, but no item def, we need to try again later
				if(pItem)
					r = false;

				continue;
			}

			// Check to make sure that the player can actually use this item, if it's not in a special bag
			if (!(invbag_flags(bag) & InvBagFlag_SpecialBag) && (!(pItemDef->flags & kItemDefFlag_CanUseUnequipped) ||
				!itemdef_VerifyUsageRestrictions(iPartitionIdx, pEnt, pItemDef, iMinItemLevel, NULL, -1)))
			{
				// at the very least, make sure every power on the item has source set up correctly
				for(iPower = 0; iPower < NumPowers; iPower++)
				{
					Power* ppow = item_GetPower(pItem, iPower);
					if (ppow)
					{
						ppow->eSource = kPowerSource_Item;
						ppow->pSourceItem = pItem;
						ppow->uiSrcEquipSlot = iter->i_cur;
					}
				}

				continue;
			}

			// If your combat level is being controlled, we might adjust the level of the
			//  Powers on the item
			if(iCharLevelControlled)
			{
				if(bItemLevelShift)
				{
					// Using the global level shift (may be positive or negative)
					iLevelAdjustment = iLevelAdjustmentGlobal;
				}
				else if (item_GetMinLevel(pItem) > iCharLevelControlled)
				{
					// The item requires a higher level, the item applies, so it
					//  applies an appropriate negative adjustment
					iLevelAdjustment = iCharLevelControlled - item_GetMinLevel(pItem);
				}
			}

			for(iPower = 0; iPower < NumPowers; iPower++)
			{
				ItemPowerDef *pItemPowerDef = item_GetItemPowerDef(pItem, iPower);
				Power* ppow = item_GetPower(pItem, iPower);
				PowerDef *pPowerDef;

				if ( !pItemPowerDef )
				{
					// No ItemPowerDef, try again later
					r = false;
					continue;
				}

				if(!ppow)
					continue;

				// Make sure every power on every item has source set up correctly
				ppow->eSource = kPowerSource_Item;
				ppow->pSourceItem = pItem;
				ppow->uiSrcEquipSlot = iter->i_cur;


				if( !GET_REF(pItemPowerDef->hPower) )
				{
					// No PowerDef, try again later
					r = false;
					continue;
				}

				// Apply the level adjustment.  If we're using the global shift, or the
				//  Power is above level 0 after adjustment, keep it, otherwise get rid
				//  of it.
				ppow->iLevelAdjustment = 0;
				if(ppow->iLevel)
				{
					if(bItemLevelShift)
					{
						// MAX here to keep the effective level at least 1
						ppow->iLevelAdjustment = MAX(iLevelAdjustment,(1-ppow->iLevel));
					}
					else if(ppow->iLevel+iLevelAdjustment > 0)
					{
						ppow->iLevelAdjustment = iLevelAdjustment;
					}
					else
					{
						continue;
					}
				}

				pPowerDef = GET_REF(ppow->hDef);
				assert(pPowerDef);
				
				// Local enhancements aren't added to the global powers array
				if(pPowerDef->eType == kPowerType_Enhancement && (pItemPowerDef->flags&kItemPowerFlag_LocalEnhancement))
					continue;

				// Expired Powers (probably) aren't added
				if(pPowerDef->bLimitedUse && power_IsExpired(ppow, true))
				{
					if(!ppow->bActive || !(pPowerDef->iCharges
							&& 0!=power_GetLifetimeRealLeft(ppow)
							&& 0!=power_GetLifetimeGameLeft(ppow)
							&& 0!=power_GetLifetimeUsageLeft(ppow)))
					{
						// It's inactive, or it's active but expired due to a lifetime
						continue;
					}
				}

				//make sure power is active (equipped or flagged)
				if ( !item_ItemPowerActive(pEnt, bag, pItem, iPower) )
					continue;

				ppow->fYaw = bag ? RAD(invbag_def(bag)->power_yaw) : 0;

				if(!IS_HANDLE_ACTIVE(pItemPowerDef->hPowerReplace) || GET_REF(ppow->hDef)->eType != kPowerType_Enhancement)
				{
					if(IS_HANDLE_ACTIVE(pItemPowerDef->hPowerReplace))
					{
						PowerReplace *pReplace = Entity_FindPowerReplace(pEnt, GET_REF(pItemPowerDef->hPowerReplace));

						if(!pReplace || !pReplace->uiBasePowerID)
							continue;
					}
					character_AddPower(iPartitionIdx, pChar, ppow, kPowerSource_Item, pExtract);

					if(pItemPowerDef->flags & kItemPowerFlag_DefaultStance)
					{
						pdefStance = GET_REF(ppow->hDef);
					}
#ifdef GAMESERVER
					if(pEnt->aibase && pEnt->pCritter)
					{
						// item_AddPowersFromItems is assumed to be called from character_ResetPowersArray
						// if this needs to change, please contact team AI 
						devassert(aiPowersResetPowersIsInReset());
					}
#endif

				}

				ItemPowerReplaceHelper(pChar, pItem, pItemDef, ppow, false);
			}
		}
		bagiterator_Destroy(iter);
	}


	if(entIsServer() && (pdefStance || pEnt->pChar->bStanceDefaultItem))
	{
		character_SetDefaultStance(iPartitionIdx,pEnt->pChar,pdefStance);
		pEnt->pChar->bStanceDefaultItem = !!pdefStance;
	}

	return r;
}

// Walks the inventory looking for Powers that have the limited use flag
void item_UpdateLimitedUsePowers(Entity *pEnt, GameAccountDataExtract *pExtract)
{
	Character* pChar = NULL;
	int iBag;

	if (!pEnt || !pEnt->pInventoryV2 || !pEnt->pChar)
		return;

	for(iBag = eaSize(&pEnt->pInventoryV2->ppInventoryBags)-1; iBag>=0; iBag--)
	{
		S32 iBagID = SAFE_MEMBER(pEnt->pInventoryV2->ppInventoryBags[iBag],BagID);

		// Ignore powers in loot bags
		if (iBagID != InvBagIDs_Loot)
		{
			BagIterator *iter = invbag_IteratorFromEnt(pEnt, iBagID, pExtract);

			for (; !bagiterator_Stopped(iter); bagiterator_Next(iter))
			{
				Item *pItem = (Item*)bagiterator_GetItem(iter);
				ItemDef *pItemDef = bagiterator_GetDef(iter);
				S32 iPower;
				int NumPowers = pItem ? eaSize(&pItem->ppPowers) : 0;

				for(iPower=NumPowers-1; iPower>=0; iPower--)
				{
					Power* ppow = pItem->ppPowers[iPower];
					PowerDef *pdef = ppow ? GET_REF(ppow->hDef) : NULL;

					if(pdef && pdef->bLimitedUse)
					{
						eaPush(&pEnt->pChar->ppPowersLimitedUse,ppow);
					}
				}
			}
			bagiterator_Destroy(iter);
		}
	}
}

S32 item_GetComponentCount(ItemCraftingComponent *pComponent)
{
	return (S32)pComponent->fCount;
}

// DO NOT CALL THIS IN A TRANSACTION!!!
// It does expression evaluation, which isn't transaction safe
S32 item_GetDefEPValue(int iPartitionIdx, Entity *pEnt, ItemDef *pDef, S32 iLevel, ItemQuality eQuality)
{
	Expression *pExpr;
	const char *pcFilename;
	MultiVal mvExprResult = {0};
	ItemQualityInfo* pQuality = eaGet(&g_ItemQualities.ppQualities, pDef->Quality);
	S32 iResult;
	
	if (!pDef) {
		return 0;
	}
	
	if (pDef->pExprEconomyPoints) 
	{
		pExpr = pDef->pExprEconomyPoints;
		pcFilename = pDef->pchFileName;
	} 
	else if (pQuality && pQuality->pExprEPValue)
	{
		pExpr = pQuality->pExprEPValue;
		pcFilename = "ItemQualities.def";
	}
	else 
	{
		pExpr = g_GlobalExpressions.pExprItemEPValue;
		pcFilename = "GlobalExpressions.def";
	}

	itemeval_Eval(iPartitionIdx, pExpr, pDef, NULL, NULL, pEnt, iLevel, eQuality, 0, pcFilename, -1, &mvExprResult);
	iResult = itemeval_GetIntResult(&mvExprResult, pcFilename, pExpr);
	
	// If the above expression didn't execute, or resolved to 0, calculate an EP value from the item's powers
	if (!iResult) {
		S32 i;
		
		for (i = eaSize(&pDef->ppItemPowerDefRefs)-1; i >= 0; i--) {
			ItemPowerDef *pItemPowerDef = GET_REF(pDef->ppItemPowerDefRefs[i]->hItemPowerDef);
			if (pItemPowerDef) {
				iResult += itempower_GetEPValue(iPartitionIdx, pEnt, pItemPowerDef, pDef, iLevel, eQuality);
			}
		}
	}
	
	return iResult;
}

// DO NOT CALL THIS IN A TRANSACTION!!!
// It does expression evaluation, which isn't transaction safe
S32 item_GetEPValue(int iPartitionIdx, Entity *pEnt, Item *pItem)
{
	ItemDef *pDef = pItem ? GET_REF(pItem->hItem) : NULL;
	Expression *pExpr;
	const char *pcFilename;
	MultiVal mvExprResult = {0};
	ItemQualityInfo* pQuality = eaGet(&g_ItemQualities.ppQualities, item_GetQuality(pItem));
	S32 iResult = 0;
	
	if (!pDef) {
		return 0;
	}

	if (pDef->pExprEconomyPoints) 
	{
		pExpr = pDef->pExprEconomyPoints;
		pcFilename = pDef->pchFileName;
	} 
	else if (pQuality && pQuality->pExprEPValue)
	{
		pExpr = pQuality->pExprEPValue;
		pcFilename = "ItemQualities.def";
	}
	else
	{
		pExpr = g_GlobalExpressions.pExprItemEPValue;
		pcFilename = "GlobalExpressions.def";
	}
	itemeval_Eval(iPartitionIdx, pExpr, pDef, NULL, pItem, pEnt, item_GetMinLevel(pItem), item_GetQuality(pItem), 0, pcFilename, -1, &mvExprResult);
	iResult = itemeval_GetIntResult(&mvExprResult, pcFilename, pExpr);
	
	// If the above expression didn't execute, or resolved to 0, calculate an EP value from the item's powers
	if (!iResult && pItem->pAlgoProps) {
		S32 i;
		
		for (i = eaSize(&pItem->pAlgoProps->ppItemPowerDefRefs)-1; i >= 0; i--) {
			ItemPowerDef *pItemPowerDef = GET_REF(pItem->pAlgoProps->ppItemPowerDefRefs[i]->hItemPowerDef);
			if (pItemPowerDef) {
				iResult += itempower_GetEPValue(entGetPartitionIdx(pEnt), pEnt, pItemPowerDef, pDef, item_GetMinLevel(pItem), item_GetQuality(pItem));
			}
		}
	}

	//sell unidentified things for 1/10 resources.  Probably punchlist-temporary.  Later maybe a data scalar or expression.
	if(item_IsUnidentified(pItem))
	{
		iResult *= 0.1;
	}
	
	// Get the value from it's def
	return iResult;
}

// DO NOT CALL THIS IN A TRANSACTION!!!
// It does expression evaluation, which isn't transaction safe
S32 item_GetStoreEPValue(int iPartitionIdx, Entity *pEnt, Item *pItem, StoreDef *pStore)
{
	ItemDef *pDef = pItem ? GET_REF(pItem->hItem) : NULL;
	S32 iBaseEP;
	MultiVal mvExprResult = {0};
	S32 iResult = 0;
	const char *pcFilename;
	Expression *pExpr;
	
	if (!pDef) {
		return 0;
	}
	
	iBaseEP = item_GetEPValue(iPartitionIdx, pEnt, pItem);
	if (pStore && pStore->pExprEPConversion) {
		pExpr = pStore->pExprEPConversion;
		pcFilename = pStore->filename;
	} else {
		pExpr = g_GlobalExpressions.pExprStoreEPConversion;
		pcFilename = "GlobalExpressions.def";
	}
	itemeval_Eval(iPartitionIdx, pExpr, pDef, NULL, pItem, pEnt, item_GetMinLevel(pItem), item_GetQuality(pItem), iBaseEP, pcFilename, -1, &mvExprResult);
	iResult = itemeval_GetIntResult(&mvExprResult, pcFilename, pExpr);
	
	// If the above expression didn't execute, or resolved to 0, default to the base EP value
	if (!iResult) {
		return iBaseEP;
	}
	
	// Get the value from it's def
	return iResult;
}

// This function returns the value of an item converted to the specified numeric resource
int item_GetResourceValue(int iPartitionIdx, Entity* pEnt, Item *pItem, const char *pchResources)
{
	int iResult = 0;
	int iResourcesValue = 0;

	if (pchResources && pchResources[0])
	{
		ItemDef *pResourceDef = item_DefFromName(pchResources);
		iResourcesValue = pResourceDef ? item_GetDefEPValue(iPartitionIdx, pEnt, pResourceDef, pResourceDef->iLevel, pResourceDef->Quality) : iResourcesValue;
	}

	if (pItem)
	{
		iResult = item_GetStoreEPValue(iPartitionIdx, pEnt, pItem, NULL);
		if (iResourcesValue != 0)
			iResult /= iResourcesValue;
	}

	return iResult;
}


bool item_Validate(ItemDef *pDef)
{
	bool retcode = true;

	if ( !item_ValidateAllButRefs(pDef) )
		retcode = false;

	if ( !item_ValidateRefs(pDef) )
		retcode = false;

	return retcode;
}

static int CmpItemSetMember(const ItemDefRef** a, const ItemDefRef** b)
{
	int i;
	ItemDef* pItemDefA = GET_REF((*a)->hDef);
	ItemDef* pItemDefB = GET_REF((*b)->hDef);

	if (!pItemDefB)
		return -1;
	if (!pItemDefA)
		return 1;

	i = eaiGet(&pItemDefA->peRestrictBagIDs,0) - eaiGet(&pItemDefB->peRestrictBagIDs,0);
	if(i)
		return i;
	return stricmp(pItemDefA->pchName,pItemDefB->pchName);
}

static bool RebuildItemSetMembers(ItemDef *pdefSet)
{
	int s=0;
	ItemDef *pdefDict;
	ResourceIterator resIterator;

	eaDestroyStruct(&pdefSet->ppItemSetMembers,parse_ItemDefRef);

	resInitIterator(g_hItemDict, &resIterator);
	while (resIteratorGetNext(&resIterator, NULL, &pdefDict))
	{
		S32 iItemSet = 0;
		for (iItemSet = eaSize(&pdefDict->ppItemSets)-1; iItemSet >= 0; iItemSet--)
		{
			ItemDef* pdefDictSet = GET_REF(pdefDict->ppItemSets[iItemSet]->hDef);
			if(pdefDictSet==pdefSet)
			{
				ItemDefRef *pref = StructCreate(parse_ItemDefRef);
				SET_HANDLE_FROM_REFERENT(g_hItemDict,pdefDict,pref->hDef);
				eaPush(&pdefSet->ppItemSetMembers,pref);
				s++;
				break;
			}
		}
	}
	resFreeIterator(&resIterator);
	eaQSort(pdefSet->ppItemSetMembers,CmpItemSetMember);

	if(s>ITEMSET_MAX_SIZE)
	{
		ErrorFilenamef( pdefSet->pchFileName, "ItemSet has %d members, the max is %d",s,ITEMSET_MAX_SIZE);
		return false;
	}
	return true;
}

bool item_ValidateAllButRefs(ItemDef *pDef)
{
	const char *pchTempFileName;
	int i;
	bool retcode = true;
	ItemSortType *pSortType;
	char *pchPath = NULL;
	S32 bProbableItemSet = false;

	if( !resIsValidName(pDef->pchName) )
	{
		ErrorFilenamef( pDef->pchFileName, "Item name is illegal: '%s'", pDef->pchName );
		retcode = false;
	}

	pchTempFileName = pDef->pchFileName;
	if (!(pDef->pchScope && resIsInDirectory(pDef->pchScope, "itemgen/")) && resFixPooledFilename(&pchTempFileName, pDef->pchScope && resIsInDirectory(pDef->pchScope, "maps/") ? NULL : GameBranch_GetDirectory(&pchPath, ITEMS_BASE_DIR), pDef->pchScope, pDef->pchName, ITEMS_EXTENSION)) 
	{
		if (IsServer()) {
			ErrorFilenamef( pDef->pchFileName, "Item filename does not match name '%s' scope '%s'", pDef->pchName, pDef->pchScope);
			retcode = false;
		}
	}

	if( !resIsValidScope(pDef->pchScope) )
	{
		ErrorFilenamef( pDef->pchFileName, "Item scope is illegal: '%s'", pDef->pchScope );
		retcode = false;
	}

	if(pDef->flags & kItemDefFlag_BindOnEquip && pDef->iStackLimit > 1)
	{
		ErrorFilenamef( pDef->pchFileName, "Item stacking is illegal for Bind on Equip items: '%d' stack limit", pDef->iStackLimit );
	}

	if((pDef->flags & kItemDefFlag_UniqueEquipOnePerBag) != 0 && (pDef->flags & kItemDefFlag_Unique) != 0)
	{
		ErrorFilenamef( pDef->pchFileName, "Items should not have both UniqueEquipOnePerBag and Unique flags.");
	}

	if (pDef->pRewardPackInfo)
	{
		if (IS_HANDLE_ACTIVE(pDef->pRewardPackInfo->hRewardTable) && pDef->eType != kItemType_RewardPack)
		{
			ErrorFilenamef(pDef->pchFileName, "Item has a RewardTable specified, but is not of type %s", 
				StaticDefineIntRevLookup(ItemTypeEnum, kItemType_RewardPack));
			retcode = false;
		}
		if (IS_HANDLE_ACTIVE(pDef->pRewardPackInfo->hRequiredItem))
		{
			if (!GET_REF(pDef->pRewardPackInfo->hRequiredItem))
			{
				ErrorFilenamef(pDef->pchFileName, "Reward pack item references a non-existent required item %s", 
					REF_STRING_FROM_HANDLE(pDef->pRewardPackInfo->hRequiredItem));
				retcode = false;
			}
			else if (pDef->pRewardPackInfo->iRequiredItemCount <= 0)
			{
				ErrorFilenamef(pDef->pchFileName, "Reward pack item has a required item but specifies an invalid count %d", 
					pDef->pRewardPackInfo->iRequiredItemCount);
				retcode = false;
			}
		}
	}

	if(pDef->eType == kItemType_GrantMicroSpecial && pDef->uSpecialPartCount < 1 && pDef->eSpecialPartType != kSpecialPartType_None)
	{
		ErrorFilenamef(pDef->pchFileName, "Item is kItemType_GrantMicroSpecial and not type none, but has SpecialPartCount < 1");
		retcode = false;
	}

	if(pDef->eType == kItemType_GrantMicroSpecial && pDef->eSpecialPartType == kSpecialPartType_None && !pDef->pchPermission)
	{
		ErrorFilenamef(pDef->pchFileName, "Item is kItemType_GrantMicroSpecial (none) and doesn't have a Permission.");
		retcode = false;
	}

	if(pDef->eType == kItemType_Coupon && eaSize(&pDef->ppchMTCategories) > 0 && eaSize(&pDef->ppchMTItems) > 0)
	{
		ErrorFilenamef(pDef->pchFileName, "Item is kItemType_Coupon and has both MT categories and MT Items, you may only use one of these.");
		retcode = false;
	}

	if(pDef->eType == kItemType_Coupon && !pDef->bCouponUsesItemLevel && pDef->uCouponDiscount < 1)
	{
		ErrorFilenamef(pDef->pchFileName, "Item is kItemType_Coupon and isn't a use item level but doesn't have a coupon discount.");
		retcode = false;
	}

	if(pDef->eType == kItemType_Coupon && !pDef->bCouponUsesItemLevel && pDef->uCouponDiscount > g_MicroTransConfig.uMaximumCouponDiscount)
	{
		ErrorFilenamef(pDef->pchFileName, "Item is kItemType_Coupon and the discount level (%d) is greater than the config (%d).", pDef->uCouponDiscount, g_MicroTransConfig.uMaximumCouponDiscount);
		retcode = false;
	}

	// ItemPowerDefRefs
	for(i=eaSize(&pDef->ppItemPowerDefRefs)-1; i>=0; --i) 
	{
		ItemPowerDefRef *pref = pDef->ppItemPowerDefRefs[i];
		if (!pref) 
		{
			ErrorFilenamef( pDef->pchFileName, "Item has no ItemPowerDef set for entry %d.", i+1 );
			retcode = false;
		}
		else
		{
			if (pDef->eType != kItemType_Gem && pref->fGemSlottingApplyChance != 1.f)
			{
				ErrorFilenamef( pDef->pchFileName, "ItemPower gem slotting apply chance should only be set for gems.");
				retcode = false;
			}
			else if (pref->fGemSlottingApplyChance < 0.f || pref->fGemSlottingApplyChance > 1.f)
			{
				ErrorFilenamef( pDef->pchFileName, "ItemPower gem slotting apply chance value is outside the accepted range of [0 - 1] for entry %d.", i+1);
				retcode = false;
			}
			if(pref->uiSetMin)
			{
				if(i && pDef->ppItemPowerDefRefs[i-1]->uiSetMin)
				{
					if(pref->uiSetMin < pDef->ppItemPowerDefRefs[i-1]->uiSetMin)
					{
						ErrorFilenamef( pDef->pchFileName, "ItemPowers not sorted by set min at entries %d-%d.", i,i+1 );
						retcode = false;
					}
				}
				bProbableItemSet = true;
			}
		}
	}

	// Ensure PlayerCostume ref on each ItemCostume struct
	for(i=eaSize(&pDef->ppCostumes)-1; i>=0; --i) 
	{
		if (!pDef->ppCostumes[i]) 
		{
			ErrorFilenamef( pDef->pchFileName, "Item has no ItemCostume at entry %d.", i+1);
			retcode = false;
		}
		else if (!GET_REF(pDef->ppCostumes[i]->hCostumeRef))
		{
			ErrorFilenamef( pDef->pchFileName, "Item refers to non-existent costume %s at entry %d.", REF_STRING_FROM_HANDLE(pDef->ppCostumes[i]->hCostumeRef), i+1);
			retcode = false;
		}
		else if ((pDef->eCostumeMode == kCostumeDisplayMode_Unlock) && (GET_REF(pDef->ppCostumes[i]->hCostumeRef)->eCostumeType != kPCCostumeType_Item))
		{
			ErrorFilenamef( pDef->pchFileName, "Item refers costume %s at entry %d but this costume is not of type item.  All unlocked costumes must be of type Item.", REF_STRING_FROM_HANDLE(pDef->ppCostumes[i]->hCostumeRef), i+1);
			retcode = false;
		}

		// generate the requires expression for the costume
		if (pDef->ppCostumes[i]->pExprRequires)
		{
			if (!exprGenerate(pDef->ppCostumes[i]->pExprRequires, g_pItemContext))
			{
				retcode = false;
			}
		}
	}

	// Ensure that an item that has costumes can be bound
	if ((pDef->eCostumeMode == kCostumeDisplayMode_Unlock) && eaSize(&pDef->ppCostumes) && !(pDef->flags & (kItemDefFlag_BindOnEquip | kItemDefFlag_BindOnPickup)) && !gConf.bIgnoreUnboundItemCostumes &&
		pDef->eType != kItemType_CostumeUnlock)
	{
		ErrorFilenamef( pDef->pchFileName, "ItemCostume in unlock mode has no effect for Item that cannot be bound and is not a CostumeUnlock item type." );
		retcode = false;
	}
	
	// If this is a kItemType_CostumeUnlock item it needs to have a costume unlock
	if (pDef->eType == kItemType_CostumeUnlock && (pDef->eCostumeMode != kCostumeDisplayMode_Unlock || eaSize(&pDef->ppCostumes) < 1) )
	{
		ErrorFilenamef( pDef->pchFileName, "Item with CostumeUnlock item type must have custume unlock and costumes." );
		retcode = false;
	}

	// If this has costume unlocks it must be the correct type of item
	if(pDef->eType != kItemType_CostumeUnlock && (pDef->eCostumeMode == kCostumeDisplayMode_Unlock && eaSize(&pDef->ppCostumes) >0) && 
		pDef->bDeleteAfterUnlock && (pDef->flags & kItemDefFlag_BindOnPickup) == 0)
	{
		ErrorFilenamef( pDef->pchFileName, "Items with Costumes that are bDeleteAfterUnlock and not kItemDefFlag_BindOnPickup must be item type kItemType_CostumeUnlock." );
		retcode = false;
	}

	if(pDef->bDeleteAfterUnlock && !eaSize(&pDef->ppCostumes))
	{
		ErrorFilenamef( pDef->pchFileName, "Item is marked to delete itself after unlocking, but has nothing on it to unlock.");
		retcode = false;
	}

	if (eaSize(&pDef->ppItemSets))
	{
		for (i = eaSize(&pDef->ppItemSets)-1; i >= 0; i--)
		{
			ItemDefRef* pSetRef = pDef->ppItemSets[i];
			if(IS_HANDLE_ACTIVE(pSetRef->hDef))
			{
				ItemDef *pdefSet = GET_REF(pSetRef->hDef);
				if(!pdefSet)
				{
					ErrorFilenamef(pDef->pchFileName, "Item is marked to belong to an invalid ItemSet \"%s\"",REF_STRING_FROM_HANDLE(pSetRef->hDef));
					retcode = false;
				}
				else if (pdefSet->bItemSetMembersUnique && 
					!(pDef->flags & (kItemDefFlag_Unique|kItemDefFlag_UniqueEquipOnePerBag)) && 
					(!pDef->pEquipLimit || pDef->pEquipLimit->iMaxEquipCount != 1))
				{
					ErrorFilenamef(pDef->pchFileName, "Item must be unique or have an equip limit in order to belong to ItemSet \"%s\"",REF_STRING_FROM_HANDLE(pSetRef->hDef));
					retcode = false;
				}
			}
		}
	}
	else if(bProbableItemSet)
	{
		// Rebuild the member list if it's probably an ItemSet itself (rather than belonging to one)
		if(!RebuildItemSetMembers(pDef))
			retcode = false;
	}

	// generate the requires expression
	if (pDef->pRestriction && pDef->pRestriction->pRequires)
	{
		if (!exprGenerate(pDef->pRestriction->pRequires, g_pItemContext))
		{
			retcode = false;
		}
	}

	// Validate item equip limit
	if (pDef->pEquipLimit)
	{
		if (!pDef->pEquipLimit->iMaxEquipCount && !pDef->pEquipLimit->eCategory)
		{
			ErrorFilenamef(pDef->pchFileName, "Item equip limit has an unset max equip count and unset category");
			retcode = false;
		}
	}
	
	// generate the econ points expression
	if (pDef->pExprEconomyPoints)
	{
		if (!exprGenerate(pDef->pExprEconomyPoints, g_pItemContext))
		{
			retcode = false;
		}
	}

	if (pDef->pItemWeaponDef && pDef->pItemWeaponDef->pAdditionalDamageExpr)
	{
		// I'm generating this expression in the CombatEval context, because that's where it's evaluated.
		combateval_Generate(pDef->pItemWeaponDef->pAdditionalDamageExpr,kCombatEvalContext_Simple);
	}

	if (pDef->pItemDamageDef)
	{
		if (pDef->pItemDamageDef->pExprMagnitude)
		{
			// I'm generating this expression in the CombatEval context, because that's where it's evaluated.
			combateval_Generate(pDef->pItemDamageDef->pExprMagnitude, kCombatEvalContext_Simple);
		}
		else if (pDef->pItemDamageDef->pchTableName && pDef->pItemDamageDef->pchTableName[0])
		{
			ErrorFilenamef(pDef->pchFileName, "Since you chose a power table for the weapon damage, you must define the magnitude.");
		}

		// Validate variance
		if(pDef->pItemDamageDef->fVariance < 0.0f || pDef->pItemDamageDef->fVariance > 1.0f)
		{
			ErrorFilenamef(pDef->pchFileName, "Invalid variance for weapon damage %f (must be [0.0..1.0])", pDef->pItemDamageDef->fVariance);
		}
	}

	pSortType = item_GetSortTypeForTypes(pDef->eType, pDef->peRestrictBagIDs, pDef->eRestrictSlotType, pDef->peCategories);
	if (pSortType)
	{
		pDef->iSortID = pSortType->iSortID;
	}

	estrDestroy(&pchPath);

	return retcode;
}

bool item_ValidateRefs(ItemDef *pDef)
{
	int i,j;
	bool retcode = true;
	S32 bLimitedUse = false;


	if (!GET_REF(pDef->displayNameMsg.hMessage) && REF_STRING_FROM_HANDLE(pDef->displayNameMsg.hMessage)) 
	{
		ErrorFilenamef( pDef->pchFileName, "Item references non-existent message '%s'", REF_STRING_FROM_HANDLE(pDef->displayNameMsg.hMessage));
		retcode = false;
	}

	if (!GET_REF(pDef->descriptionMsg.hMessage) && REF_STRING_FROM_HANDLE(pDef->descriptionMsg.hMessage)) 
	{
		ErrorFilenamef( pDef->pchFileName, "Item references non-existent message '%s'", REF_STRING_FROM_HANDLE(pDef->descriptionMsg.hMessage));
		retcode = false;
	}

	if (!GET_REF(pDef->descShortMsg.hMessage) && REF_STRING_FROM_HANDLE(pDef->descShortMsg.hMessage))
	{
		ErrorFilenamef(pDef->pchFileName, "Item referencesnon-existent message '%s'", REF_STRING_FROM_HANDLE(pDef->descShortMsg.hMessage));
		retcode = false;
	}

	//if the unidentified flag is set, validate the messages.
	if (itemdef_IsUnidentified(pDef) && !GET_REF(pDef->displayNameMsgUnidentified.hMessage) && REF_STRING_FROM_HANDLE(pDef->displayNameMsgUnidentified.hMessage)) 
	{
		ErrorFilenamef( pDef->pchFileName, "Item references non-existent message '%s'", REF_STRING_FROM_HANDLE(pDef->displayNameMsgUnidentified.hMessage));
		retcode = false;
	}

	if (itemdef_IsUnidentified(pDef) && !GET_REF(pDef->descriptionMsgUnidentified.hMessage) && REF_STRING_FROM_HANDLE(pDef->descriptionMsgUnidentified.hMessage)) 
	{
		ErrorFilenamef( pDef->pchFileName, "Item references non-existent message '%s'", REF_STRING_FROM_HANDLE(pDef->descriptionMsgUnidentified.hMessage));
		retcode = false;
	}

	// Validate Slot ID
	if(IS_HANDLE_ACTIVE(pDef->hSlotID) && !GET_REF(pDef->hSlotID))
	{
		ErrorFilenamef( pDef->pchFileName, "Item references non-existent SlotID '%s'", REF_STRING_FROM_HANDLE(pDef->hSlotID));
		retcode = false;
	}

#ifdef GAMESERVER
	// Validate Mission
	if(IS_HANDLE_ACTIVE(pDef->hMission) && !GET_REF(pDef->hMission))
	{
		ErrorFilenamef( pDef->pchFileName, "Item %s references non-existent Mission '%s'", pDef->pchName, REF_STRING_FROM_HANDLE(pDef->hMission));
		retcode = false;
	}
#endif

	//!!!! hold off until existing errors can be fixed up
#if 0
	if( !IS_HANDLE_ACTIVE(pDef->hMission) )
	{
		switch (pDef->eType)
		{
		case kItemType_Mission:
			ErrorFilenamef( pDef->pchFileName, "Mission Item does not specify mission");
			retcode = false;
			break;

		case kItemType_MissionGrant:
			ErrorFilenamef( pDef->pchFileName, "MissionGrant Item does not specify mission");
			retcode = false;
			break;
		}
	}
#endif

	// Ensure ItemPowerDef ref on each ItemPowerDefRef struct
	for(i=eaSize(&pDef->ppItemPowerDefRefs)-1; i>=0; --i)
	{
		if (pDef->ppItemPowerDefRefs[i]) 
		{
			ItemPowerDef *pItemPowerDef = GET_REF(pDef->ppItemPowerDefRefs[i]->hItemPowerDef);
			if(!pItemPowerDef)
			{
				ErrorFilenamef( pDef->pchFileName, "Item references non-existent itempower '%s'", REF_STRING_FROM_HANDLE(pDef->ppItemPowerDefRefs[i]->hItemPowerDef));
				retcode = false;
			}
			else if (!gConf.bAllowMultipleItemPowersWithCharges)
			{
				// Right now the Item system doesn't really handle multiple limited use Powers on the same item,
				//  so we're going to call that an error until someone needs it to work.
				PowerDef *pPowerDef = GET_REF(pItemPowerDef->hPower);
				if(pPowerDef && pPowerDef->bLimitedUse)
				{
					if(bLimitedUse)
					{
						ErrorFilenamef( pDef->pchFileName, "Item references multiple limited use powers, which is not currently supported");
						retcode = false;
					}
					bLimitedUse = true;
				}
			}
		}
	}

	for(i=eaSize(&pDef->ppItemGemSlots)-1;i>=0;--i)
	{
		if(pDef->ppItemGemSlots[i] && IS_HANDLE_ACTIVE(pDef->ppItemGemSlots[i]->hPreSlottedGem))
		{
			ItemDef *pSlottedDef = GET_REF(pDef->ppItemGemSlots[i]->hPreSlottedGem);
			
			if(!pSlottedDef)
			{
				ErrorFilenamef(pDef->pchFileName, "Item Gem Slot Def references non-existent item '%s'", REF_STRING_FROM_HANDLE(pDef->ppItemGemSlots[i]->hPreSlottedGem));
				retcode = false;
			}
			else if(pSlottedDef->eType != kItemType_Gem)
			{
				ErrorFilenamef(pDef->pchFileName, "Item Gem Slot Def references item of wrong gem type '%s'", REF_STRING_FROM_HANDLE(pDef->ppItemGemSlots[i]->hPreSlottedGem));
				retcode = false;
			}
			else if(pSlottedDef->eGemType != kItemGemType_Any && 
				pDef->ppItemGemSlots[i]->eType != kItemGemType_Any && 
				(pDef->ppItemGemSlots[i]->eType & pSlottedDef->eGemType) == 0)
			{
				ErrorFilenamef(pDef->pchFileName, "Item Gem Slot Def references item with wrong gem type '%s'", REF_STRING_FROM_HANDLE(pDef->ppItemGemSlots[i]->hPreSlottedGem));
				retcode = false;
			}
		}
	}

	// Validate Subtarget
	if(IS_HANDLE_ACTIVE(pDef->hSubtarget) && !GET_REF(pDef->hSubtarget))
	{
		ErrorFilenamef( pDef->pchFileName, "Item references non-existent subtarget '%s'", REF_STRING_FROM_HANDLE(pDef->hSubtarget));
		retcode = false;
	}

	// Validate RewardPackInfo
#ifdef GAMESERVER
	if (pDef->pRewardPackInfo && IS_HANDLE_ACTIVE(pDef->pRewardPackInfo->hRewardTable) && !GET_REF(pDef->pRewardPackInfo->hRewardTable))
	{
		ErrorFilenamef(pDef->pchFileName, "Item references non-existent RewardTable '%s'", REF_STRING_FROM_HANDLE(pDef->pRewardPackInfo->hRewardTable));
		retcode = false;
	}
#endif

	if (pDef->pRewardPackInfo)
	{
		if (!GET_REF(pDef->pRewardPackInfo->msgUnpackMessage.hMessage) && REF_STRING_FROM_HANDLE(pDef->pRewardPackInfo->msgUnpackMessage.hMessage)) 
		{
			ErrorFilenamef(pDef->pchFileName, "Item references non-existent RewardPack message '%s'", REF_STRING_FROM_HANDLE(pDef->pRewardPackInfo->msgUnpackMessage.hMessage));
			retcode = false;
		}
		if (!GET_REF(pDef->pRewardPackInfo->msgUnpackFailedMessage.hMessage) && REF_STRING_FROM_HANDLE(pDef->pRewardPackInfo->msgUnpackFailedMessage.hMessage)) 
		{
			ErrorFilenamef(pDef->pchFileName, "Item references non-existent RewardPack failure message '%s'", REF_STRING_FROM_HANDLE(pDef->pRewardPackInfo->msgUnpackFailedMessage.hMessage));
			retcode = false;
		}
	}

	// Ensure PlayerCostume ref on each ItemCostume struct
	for(i=eaSize(&pDef->ppCostumes)-1; i>=0; --i) 
	{
		if (pDef->ppCostumes[i]) 
		{
			if (!IS_HANDLE_ACTIVE(pDef->ppCostumes[i]->hCostumeRef) )
			{
				ErrorFilenamef( pDef->pchFileName, "Item has no costume set for entry %d.", i+1);
				retcode = false;
			}
			else if(IS_HANDLE_ACTIVE(pDef->ppCostumes[i]->hCostumeRef) && !GET_REF(pDef->ppCostumes[i]->hCostumeRef))
			{
				ErrorFilenamef( pDef->pchFileName, "Item references non-existent costume '%s'", REF_STRING_FROM_HANDLE(pDef->ppCostumes[i]->hCostumeRef));
				retcode = false;
			}
			if (pDef->eCostumeMode != kCostumeDisplayMode_Unlock) {
				costumeLoad_ValidateCostumeForApply(GET_REF(pDef->ppCostumes[i]->hCostumeRef), pDef->pchFileName);
			}
		}
	}

	// Validate Species
	if(IS_HANDLE_ACTIVE(pDef->hSpecies) && !GET_REF(pDef->hSpecies))
	{
		ErrorFilenamef(pDef->pchFileName, "Item references non-existent species '%s'", REF_STRING_FROM_HANDLE(pDef->hSpecies));
		retcode = false;
	}

	if(pDef->eType == kItemType_VanityPet && !eaSize(&pDef->ppItemVanityPetRefs))
	{
		ErrorFilenamef( pDef->pchFileName, "Item is marked as a 'vanity pet' item but has no vanity pets.");
		retcode = false;
	}
	else if(pDef->eType != kItemType_VanityPet && eaSize(&pDef->ppItemVanityPetRefs))
	{
		ErrorFilenamef( pDef->pchFileName, "Item is not marked as a 'vanity pet' item but has %d vanity pets.",
			eaSize(&pDef->ppItemVanityPetRefs));
		retcode = false;
	}

	// Ensure PlayerCostume ref on each ItemCostume struct
	for(i=eaSize(&pDef->ppItemVanityPetRefs)-1; i>=0; --i) 
	{
		if (pDef->ppItemVanityPetRefs[i]) 
		{
			if (!IS_HANDLE_ACTIVE(pDef->ppItemVanityPetRefs[i]->hPowerDef) )
			{
				ErrorFilenamef( pDef->pchFileName, "Item has no vanity pet set for entry %d.", i+1);
				retcode = false;
			}
			else if(IS_HANDLE_ACTIVE(pDef->ppItemVanityPetRefs[i]->hPowerDef) && !GET_REF(pDef->ppItemVanityPetRefs[i]->hPowerDef))
			{
				ErrorFilenamef( pDef->pchFileName, "Item references non-existent vanity pet power '%s'", REF_STRING_FROM_HANDLE(pDef->ppItemVanityPetRefs[i]->hPowerDef));
				retcode = false;
			}
		}
	}



	//validate recipe specific data
	switch (pDef->eType)
	{
	case kItemType_ItemValue:
		if (pDef->pCraft) {
			for (i = 0; i < eaSize(&pDef->pCraft->ppPart); i++) {
				ItemDef *pPartDef = GET_REF(pDef->pCraft->ppPart[i]->hItem);
				if (pPartDef && pPartDef->eType == kItemType_Numeric) {
					if (pPartDef->MinNumericValue < 0) {
						ErrorFilenamef( pDef->pchFileName, "Item value recipe ingredient '%s' is a numeric with a MinNumericValue below 0. This allows people to spend into the negative.",
							REF_STRING_FROM_HANDLE(pDef->pCraft->ppPart[i]->hItem));
						retcode = false;
					}
					if (!(pPartDef->flags & kItemDefFlag_TransFailonLowLimit)) {
						ErrorFilenamef( pDef->pchFileName, "Item value recipe ingredient '%s' is a numeric but does not have TransFailOnLowLimit set. This allows people to buy the item when they don't have enough currency.",
							REF_STRING_FROM_HANDLE(pDef->pCraft->ppPart[i]->hItem));
						retcode = false;
					}
				}
			}
		}
	case kItemType_ItemRecipe:
	case kItemType_ItemPowerRecipe:
		if (pDef->pCraft)
		{
			if (pDef->eType == kItemType_ItemRecipe && !gConf.bAllowNoResultRecipes)
			{
				ItemDef *pResult = GET_REF(pDef->pCraft->hItemResult);
				if (!pResult)
				{
					const char *pchResult = REF_DATA_FROM_HANDLE(pDef->pCraft->hItemResult);
					ErrorFilenamef(pDef->pchFileName, "Item refers to unknown crafting result %s", pchResult);
					retcode = false;
				}
			}
			
			if (pDef->eType == kItemType_ItemPowerRecipe && !gConf.bAllowNoResultRecipes)
			{
				ItemPowerDef *pResult = GET_REF(pDef->pCraft->hItemPowerResult);
				if (!pResult)
				{
					const char *pchResult = REF_DATA_FROM_HANDLE(pDef->pCraft->hItemPowerResult);
					ErrorFilenamef(pDef->pchFileName, "Item refers to unknown crafting result %s", pchResult);
					retcode = false;
				}
			}
			
			for (j = 0; j < eaSize(&pDef->pCraft->ppPart); j++)
			{
				ItemCraftingComponent *pPart = pDef->pCraft->ppPart[j];
				ItemDef *pPartDef = GET_REF(pPart->hItem);
				if (!pPartDef && !IsClient())
				{
					const char *pchComponent = REF_DATA_FROM_HANDLE(pPart->hItem);
					ErrorFilenamef(pDef->pchFileName, "Item refers to unknown crafting component %s", pchComponent);
					retcode = false;
				}
			}
		}
		break;
	case kItemType_Gem:
		if (pDef->eGemType == kItemGemType_None)
		{
			ErrorFilenamef(pDef->pchFileName, "Gem type must be set for gems.");
			retcode = false;
		}
		break;
	}
	
	if (!GET_REF(pDef->hCraftRecipe) && REF_STRING_FROM_HANDLE(pDef->hCraftRecipe))
	{
		ErrorFilenamef( pDef->pchFileName, "Item refers to non-existent craft recipe '%s'", REF_STRING_FROM_HANDLE(pDef->hCraftRecipe));
		retcode = false;
	}
	
	{
		ItemDef *pRecipe = GET_REF(pDef->hCraftRecipe);
		if (pRecipe)
		{
			ItemDef *pProduct = SAFE_GET_REF(pRecipe->pCraft,hItemResult);

			if (!pProduct)
			{
				ErrorFilenamef( pDef->pchFileName, "Item craft recipe has NULL product");
				retcode = false;
			}
			else if (pRecipe->eType != kItemType_ItemRecipe)
			{
				ErrorFilenamef( pDef->pchFileName, "Item craft recipe has invalid type");
				retcode = false;
			}

// 			if ( !pProduct ||
// 				 ( stricmp(pProduct->pchName,pDef->pchName) != 0 ) ) 
// 			{ 
// 				ErrorFilenamef( pDef->pchFileName, "Item craft recipe has non matching product '%s'", REF_STRING_FROM_HANDLE(pRecipe->pCraft->hItemResult)); 
// 				retcode = false; 
// 			} 
		}
	}

	for (i = 0; i < eaSize(&pDef->ppValueRecipes); i++)
	{
		if (!SAFE_GET_REF(pDef->ppValueRecipes[i], hDef) && REF_STRING_FROM_HANDLE(pDef->ppValueRecipes[i]->hDef))
		{
			ErrorFilenamef( pDef->pchFileName, "Item refers to non-existent value recipe '%s'", REF_STRING_FROM_HANDLE(pDef->ppValueRecipes[i]->hDef));
			retcode = false;
		}
	}
	
	if (!GET_REF(pDef->calloutMsg.hMessage) && REF_STRING_FROM_HANDLE(pDef->calloutMsg.hMessage)) 
	{
		ErrorFilenamef( pDef->pchFileName, "Item references non-existent message '%s'", REF_STRING_FROM_HANDLE(pDef->calloutMsg.hMessage));
		retcode = false;
	}

	// validate all BonusNumerics
	if(pDef->eType == kItemType_Numeric)
	{
		U32 uLarge = 999;

		for(i = 0; i < eaSize(&pDef->eaBonusNumerics); ++i)
		{
			ItemDef *pBDef = GET_REF(pDef->eaBonusNumerics[i]->hItem);

			if (!pBDef)
			{
				ErrorFilenamef( pDef->pchFileName, "Numeric Item %s is pointing to null bonusNumeric.", pDef->pchName);
				retcode = false;
				break;
			}
			if(pBDef->eType != kItemType_Numeric)
			{
				ErrorFilenamef( pDef->pchFileName, "Numeric Item is pointing to non-Numeric bonusNumeric %s.", pBDef->pchName);
				retcode = false;
				break;
			}

			// check for valid bonus, currently hard coded 1% to 300%
			if(pBDef->uBonusPercent < 1 || pBDef->uBonusPercent > 300)
			{
				ErrorFilenamef( pDef->pchFileName, "Numeric Item is pointing to Numeric item %s that does not have a valid uBonusPercent (1 to 300).", pBDef->pchName);
				retcode = false;
				break;
			}

			if(pBDef->uBonusPercent > uLarge)
			{
				ErrorFilenamef( pDef->pchFileName, "Numeric Item eaBonusNumerics needs to have numeric items sorted from large to small. %s is larger than the last item.", pBDef->pchName);
				retcode = false;
				break;
			}
			else
			{
				uLarge = pBDef->uBonusPercent;
			}
		}
	}
	
	return retcode;
}

bool item_ValidateIngredientValue(const ItemCraftingComponent **eaComponents, S32 iResultValue, S32 *iConstructEP, S32 *iDeconstructEP)
{
	S32 i;
	
	for (i = eaSize(&eaComponents)-1; i >= 0; i--) {
		ItemDef *pComponentDef = GET_REF(eaComponents[i]->hItem);
		if (pComponentDef) {
			S32 iComponentValue = item_GetDefEPValue(PARTITION_UNINITIALIZED, NULL, pComponentDef, pComponentDef->iLevel, pComponentDef->Quality);
			(*iDeconstructEP) += eaComponents[i]->fWeight * iComponentValue;
			(*iConstructEP) += iComponentValue;
		}
	}
	if ((*iDeconstructEP) >= iResultValue || (*iConstructEP) <= iResultValue) {
		return false;
	}
	return true;
}

// Ensure that average deconstruct value of an item is less than it's value, and that the
// construction cost is greater than it's value
// This prevents people from buying an item, deconstructing it, and selling it back for profit,
// and from increasing the total value of the economy through crafting
bool item_ValidateRecipe(SA_PARAM_NN_VALID ItemDef *pDef)
{
	ItemCraftingComponent **eaComponents = NULL;
	S32 iResultEP, iConstructEP, iDeconstructEP, i, j;
	bool bReturn = true;
	
	if (!pDef->pCraft) {
		return false;
	}
	
	if (pDef->eType == kItemType_ItemRecipe) {
		if (pDef->Group == 0) {
			ItemDef *pResultDef = GET_REF(pDef->pCraft->hItemResult);
			if (pResultDef) {
				iResultEP = item_GetDefEPValue(PARTITION_UNINITIALIZED, NULL, pResultDef, pResultDef->iLevel, pResultDef->Quality);
				item_GetAlgoIngredients(pDef, &eaComponents, 0, pDef->iLevel, pDef->Quality);
				if (!item_ValidateIngredientValue(eaComponents, iResultEP, &iConstructEP, &iDeconstructEP)) {
					ErrorFilenamef(pDef->pchFileName, "Recipe %s makes %s, valued at %d EP. This is not between it's average deconstruction value of %d EP and it's construction value of %d EP.",
						pDef->pchName, pResultDef->pchName, iResultEP, iDeconstructEP, iConstructEP);
					bReturn = false;
				}
				eaDestroy(&eaComponents);
				return bReturn;
			}
		}
	}
	if (pDef->eType == kItemType_ItemPowerRecipe) {
		ItemPowerDef *pResultDef = GET_REF(pDef->pCraft->hItemPowerResult);
		if (pResultDef) {
			for (i = 0; i < ITEM_POWER_GROUP_COUNT; i++) {
				if (pDef->Group & (1<<i)) {
					for (j = 0; j < 50; j++) {
						StaticDefineInt *pQualityDefine = ItemQualityEnum+1;
						while (pQualityDefine->key) {
							iResultEP = itempower_GetEPValue(0, NULL, pResultDef, pDef, j, pQualityDefine->value);
							item_GetAlgoIngredients(pDef, &eaComponents, i, j, pQualityDefine->value);
							if (!item_ValidateIngredientValue(eaComponents, iResultEP, &iConstructEP, &iDeconstructEP)) {
								ErrorFilenamef(pDef->pchFileName, "Recipe %s used for group %d at level %d at quality %s makes %s, valued at %d EP. This is not between it's average deconstruction value of %d EP and it's construction value of %d EP.",
									pDef->pchName, i, j, pQualityDefine->key, pResultDef->pchName, iResultEP, iDeconstructEP, iConstructEP);
								bReturn = false;
							}
							pQualityDefine++;
						}
					}
				}
			}
		}
	}
	return bReturn;
}

const char *item_GetIconName(Item* pItem, ItemDef *pItemDef)
{
	int i;
	const char *pchIconName = NULL;

	if ( !pItem && !pItemDef )
		return "default_item_icon";

	if ( pItem && !pItemDef )
		pItemDef = GET_REF(pItem->hItem);

	if (pItem && pItem->pSpecialProps && pItem->pSpecialProps->pTransmutationProps)
	{
		pItemDef = GET_REF(pItem->pSpecialProps->pTransmutationProps->hTransmutatedItemDef);
	}

	if (pItemDef)
	{
		if (pItem && (pItem->flags & kItemFlag_Algo))
		{
			//algo item

			//icon on algo item comes from 1st item power on the item
			if (pItem->pAlgoProps && eaSize(&pItem->pAlgoProps->ppItemPowerDefRefs) > 0)
			{
				ItemPowerDef *pItemPower = GET_REF(pItem->pAlgoProps->ppItemPowerDefRefs[0]->hItemPowerDef);
				pchIconName = pItemPower ? pItemPower->pchIconName : NULL;

				if (!pchIconName)
					pchIconName = pItemDef->pchIconName;
			}
			else
			{
				pchIconName = pItemDef->pchIconName;
			}
		}
		else
		{
			//not an algo item
			pchIconName = pItemDef->pchIconName;
		}
		
		if (!pchIconName)
		{
			switch (pItemDef->eType)
			{
			case kItemType_Upgrade:
				switch (pItemDef->eRestrictSlotType)
				{
				default:
				case kSlotType_Any:
				case kSlotType_Primary:
					for (i = 0; i < eaiSize(&pItemDef->peRestrictBagIDs); i++)
					{
						InvBagIDs eRestrictBagID = pItemDef->peRestrictBagIDs[i];
						
						if (eRestrictBagID == InvBagIDs_Head)
						{
							pchIconName = "Upgrade_Primary_Head";
						}
						else if (eRestrictBagID == InvBagIDs_Body)
						{
							pchIconName = "Upgrade_Primary_Body";
						}
						else if (eRestrictBagID == InvBagIDs_Offense)
						{
							pchIconName = "Upgrade_Primary_Offense";
						}
						if (pchIconName)
						{
							break;
						}
					}
					if (!pchIconName)
					{
						pchIconName = "Upgrade_Primary_Default";
					}
					break;

				case kSlotType_Secondary:
					for (i = 0; i < eaiSize(&pItemDef->peRestrictBagIDs); i++)
					{
						InvBagIDs eRestrictBagID = pItemDef->peRestrictBagIDs[i];
						
						if (eRestrictBagID == InvBagIDs_Head)
						{
							pchIconName = "Upgrade_Secondary_Head";
						}
						else if (eRestrictBagID == InvBagIDs_Body)
						{
							pchIconName = "Upgrade_Secondary_Body";
						}
						else if (eRestrictBagID == InvBagIDs_Offense)
						{
							pchIconName = "Upgrade_Secondary_Offense";
						}
						if (pchIconName)
						{
							break;
						}
					}
					if (!pchIconName)
					{
						pchIconName = "Upgrade_Secondary_Default";
					}
					break;
				}
				break;

			case kItemType_ItemRecipe:
			case kItemType_ItemValue:
			case kItemType_ItemPowerRecipe:
				switch (pItemDef->kSkillType)
				{
					xcase kSkillType_Arms: pchIconName = "Default_Arms_Recipe";
					xcase kSkillType_Mysticism: pchIconName = "Default_Mysticism_Recipe";
					xcase kSkillType_Science: pchIconName = "Default_Science_Recipe";
					xdefault: pchIconName = "Default_Recipe_Icon";
				}
				break;

			case kItemType_Component:
				pchIconName = "Component_Base_Science_default";
				break;

			case kItemType_Device:
				pchIconName = "Gadget_Item_Default";
				break;

			case kItemType_Weapon:
				{
					switch (pItemDef->eRestrictSlotType)
					{
					default:
					case kSlotType_Any:
					case kSlotType_Primary:
						pchIconName = "Upgrade_Primary_Weapon";
						break;
					case kSlotType_Secondary:
						pchIconName = "Upgrade_Secondary_Weapon";
						break;
					}
				}
				break;

			case kItemType_Mission:
			case kItemType_MissionGrant:
				pchIconName = "Mission_Item_Default";
				break;

			case kItemType_Bag:
				pchIconName = "PlayerBag_Icon";
				break;
			}
		}
	}

	return pchIconName ? pchIconName : "default_item_icon";
}


AUTO_TRANS_HELPER;
bool bag_trh_IsEquipBag(ATH_ARG NOCONST(Entity) * pEnt, InvBagIDs BagID, GameAccountDataExtract *pExtract)
{
	NOCONST(InventoryBag) *pBag = inv_GetBag( pEnt, BagID, pExtract);

	if (!pBag)
		return false;

	return ( (invbag_trh_flags(pBag) & InvBagFlag_EquipBag) != 0 );
}

bool bag_IsEquipBag(const Entity * pEnt, InvBagIDs BagID, GameAccountDataExtract *pExtract)
{
	return bag_trh_IsEquipBag(CONTAINER_NOCONST(Entity, pEnt), BagID, pExtract);
}

AUTO_TRANS_HELPER
ATR_LOCKS(pEnt, "pInventoryV2.ppInventoryBags[], pInventoryV2.ppInventoryBags[], pInventoryV2.ppLiteBags[], pPlayer.pPlayerAccountData.eaGamePermissionMaxValueNumerics[]");
bool bag_trh_IsWeaponBag(ATH_ARG NOCONST(Entity) * pEnt, InvBagIDs BagID, GameAccountDataExtract *pExtract)
{
	NOCONST(InventoryBag) *pBag = inv_trh_GetBag(ATR_EMPTY_ARGS, pEnt, BagID, pExtract);

	if (!pBag)
		return false;

	return ( (invbag_trh_flags(pBag) & (InvBagFlag_WeaponBag|InvBagFlag_ActiveWeaponBag)) != 0 );
}

bool bag_IsWeaponBag(const Entity * pEnt, InvBagIDs BagID, GameAccountDataExtract *pExtract)
{
	return bag_trh_IsWeaponBag(CONTAINER_NOCONST(Entity, pEnt), BagID, pExtract);
}

bool bag_IsNoModifyInCombatBag( const Entity* pEnt, InvBagIDs BagID, GameAccountDataExtract *pExtract)
{
	InventoryBag *pBag = (InventoryBag*)inv_GetBag(CONTAINER_NOCONST(Entity, pEnt), BagID, pExtract);

	if (!pBag)
		return false;

	return ( (invbag_flags(pBag) & InvBagFlag_NoModifyInCombat) != 0 );
}

extern ParseTable parse_CharacterAttribs[];
#define TYPE_parse_CharacterAttribs CharacterAttribs

bool itemdef_CheckRequiresExpression(int iPartitionIdx, Entity *pOwner, Entity *pEnt, ItemDef *pItemDef, const char **ppchDisplayTextMsg, int iDestinationSlot)
{
	if (pOwner && pEnt && SAFE_MEMBER2(pItemDef, pRestriction, pRequires)) {
		MultiVal mval = {0};
		F32 fRet = 0;
		itemeval_Eval(iPartitionIdx, pItemDef->pRestriction->pRequires, pItemDef, NULL, NULL, pEnt, pItemDef->iLevel, pItemDef->Quality, 0, pItemDef->pchFileName, iDestinationSlot, &mval);
		
		if(verify(mval.type != MULTI_INVALID)) {
			fRet = itemeval_GetFloatResult(&mval, pItemDef->pchFileName, pItemDef->pRestriction->pRequires);
		}
		
		if (!fRet) {
			if (ppchDisplayTextMsg) {
				*ppchDisplayTextMsg = "Item.Restriction.Expression";
			}
			return false;
		}
	}
	return true;
}

AUTO_TRANS_HELPER;
bool itemdef_trh_VerifyUsageRestrictionsLevel(ATR_ARGS, UsageRestriction *pRestrict,  ATH_ARG NOCONST(Entity) *pOwner, ItemDef *pItemDef, int iOverrideLevel, const char **ppchDisplayTextMsg) 
{
	int iLevel = entity_trh_GetSavedExpLevelLimited(pOwner);

	if (iOverrideLevel && iOverrideLevel > iLevel && !(pItemDef->flags & kItemDefFlag_NoMinLevel))
	{
		if (ppchDisplayTextMsg)
		{
			*ppchDisplayTextMsg = "Item.MinLevel";
		}
		return false;
	}

	if (!pRestrict) 
		return true;

	if (!iOverrideLevel && pRestrict->iMinLevel > 0 && pRestrict->iMinLevel > iLevel && !(pItemDef->flags & kItemDefFlag_NoMinLevel)) 
	{
		if (ppchDisplayTextMsg)
		{
			*ppchDisplayTextMsg = "Item.Restriction.MinLevel";
		}
		return false;
	}

	if (pRestrict->iMaxLevel > 0 && pRestrict->iMaxLevel < iLevel) 
	{
		if (ppchDisplayTextMsg)
		{
			*ppchDisplayTextMsg = "Item.Restriction.MaxLevel";
		}
		return false;
	}
	return true;
}

AUTO_TRANS_HELPER;
bool itemdef_trh_VerifyUsageRestrictionsSkillLevel(ATR_ARGS, UsageRestriction *pRestrict, ATH_ARG NOCONST(Entity) *pOwner, ItemDef *pItemDef, const char **ppchDisplayTextMsg) 
{
	ItemDef *pResultItemDef;

	if (!pRestrict)
		return true;

	//verify for skill type restriction
	if (pRestrict->eSkillType != kSkillType_None) {
		SkillType ePlayerSkillType = SAFE_MEMBER2(pOwner, pPlayer, SkillType);
		if (ePlayerSkillType != pRestrict->eSkillType) {
			if (ppchDisplayTextMsg) {
				*ppchDisplayTextMsg = "Item.Restriction.SkillType";
			}
			return false;
		}
	}

	//verify for skill level restriction
	if (pRestrict->iSkillLevel > 0) {
		if (inv_trh_GetNumericValue(ATR_PASS_ARGS, pOwner, "SkillLevel") < (S32)pRestrict->iSkillLevel) {
			if (ppchDisplayTextMsg) {
				*ppchDisplayTextMsg = "Item.Restriction.SkillLevel";
			}
			return false;
		}
	}

	// Check skill specialization.
	pResultItemDef = SAFE_GET_REF2(pItemDef, pCraft, hItemResult);

	if(pResultItemDef)
	{
		if(!entity_trh_CraftingCheckTag(pOwner, pResultItemDef->eTag))
		{
			if (ppchDisplayTextMsg)
			{
				*ppchDisplayTextMsg = "Item.Restriction.SkillSpecialization";
			}
			return false;
		}
	}
	return true;
}

AUTO_TRANS_HELPER;
bool itemdef_trh_VerifyUsageRestrictionsClass(ATR_ARGS, UsageRestriction *pRestrict, ATH_ARG NOCONST(Entity) *pEnt, const char **ppchDisplayTextMsg) 
{
	int i;
	// Check class restrictions
	if(pRestrict && (eaSize(&pRestrict->ppCharacterClassesAllowed) || eaiSize(&pRestrict->peClassCategoriesAllowed)))
	{
		CharacterClass *pEntClass = NULL;

		if (NONNULL(pEnt->pChar))
		{
			pEntClass = GET_REF(pEnt->pChar->hClass);
		}
		if(!pEntClass)
		{
			if (ppchDisplayTextMsg)
			{
				*ppchDisplayTextMsg = "Item.Restriction.RequiredClass";
			}
			return false;
		}

		if(!pEntClass->bIgnoreClassRestrictionsOnItems)
		{
			for(i=eaSize(&pRestrict->ppCharacterClassesAllowed)-1;i>=0;i--)
			{
				if(pEntClass == GET_REF(pRestrict->ppCharacterClassesAllowed[i]->hClass))
					break;
			}
			if (i==-1)
			{
				for(i=eaiSize(&pRestrict->peClassCategoriesAllowed)-1;i>=0;i--)
				{
					if(pEntClass->eCategory == pRestrict->peClassCategoriesAllowed[i])
						break;
				}
			}

			if(i==-1)
			{
				if (ppchDisplayTextMsg)
				{
					*ppchDisplayTextMsg = "Item.Restriction.Class";
				}
				return false;
			}
		}
	}
	return true;
}

AUTO_TRANS_HELPER;
bool itemdef_trh_VerifyUsageRestrictionsCharacterPath(ATR_ARGS, UsageRestriction *pRestrict, ATH_ARG NOCONST(Entity) *pEnt, const char **ppchDisplayTextMsg) 
{
	int i;
	// Check path restrictions
	if(pRestrict && eaSize(&pRestrict->ppCharacterPathsAllowed))
	{
		for(i=eaSize(&pRestrict->ppCharacterPathsAllowed)-1;i>=0;i--)
		{
			if (entity_trh_HasCharacterPath(pEnt, REF_STRING_FROM_HANDLE(pRestrict->ppCharacterPathsAllowed[i]->hPath)))
				break;
		}

		if(i==-1)
		{
			if (ppchDisplayTextMsg)
			{
				*ppchDisplayTextMsg = "Item.Restriction.Path";
			}
			return false;
		}
	}
	return true;
}

AUTO_TRANS_HELPER;
bool itemdef_trh_VerifyUsageRestrictionsAllegiance(ATR_ARGS, UsageRestriction *pRestrict, ATH_ARG NOCONST(Entity) *pOwner, ATH_ARG NOCONST(Entity) *pEnt, const char **ppchDisplayTextMsg) 
{
	int i;
	if (pRestrict && eaSize(&pRestrict->eaRequiredAllegiances))
	{
		for (i = eaSize(&pRestrict->eaRequiredAllegiances)-1; i >= 0; i--)
		{
			AllegianceDef* pDef = GET_REF(pRestrict->eaRequiredAllegiances[i]->hDef);

			if (pDef)
			{
				if (NONNULL(pOwner))
				{
					if (pDef == GET_REF(pOwner->hAllegiance) || pDef == GET_REF(pOwner->hSubAllegiance))
					{
						break;
					}
				}
				else
				{
					if (pDef == GET_REF(pEnt->hAllegiance) || pDef == GET_REF(pEnt->hSubAllegiance))
					{
						break;
					}
				}
			}
		}
		if (i < 0)
		{
			return false;
		}
	}
	return true;
}

// Checks all usage restrictions except the requires expression
AUTO_TRANS_HELPER;
bool itemdef_trh_VerifyUsageRestrictions(ATR_ARGS, ATH_ARG NOCONST(Entity) *pOwner, ATH_ARG NOCONST(Entity) *pEnt, ItemDef *pItemDef, int iOverrideLevel, const char **ppchDisplayTextMsg)
{
	UsageRestriction *pRestrict = pItemDef ? pItemDef->pRestriction : NULL;
	
	if (ppchDisplayTextMsg)
		*ppchDisplayTextMsg = NULL;

	if (ISNULL(pEnt) || (!pRestrict && !iOverrideLevel)) 
		return true;

	return 
		itemdef_trh_VerifyUsageRestrictionsLevel(ATR_PASS_ARGS, pRestrict, pOwner, pItemDef, iOverrideLevel, ppchDisplayTextMsg)
		&& itemdef_trh_VerifyUsageRestrictionsSkillLevel(ATR_PASS_ARGS, pRestrict, pOwner, pItemDef, ppchDisplayTextMsg)
		&& itemdef_trh_VerifyUsageRestrictionsClass(ATR_PASS_ARGS, pRestrict, pEnt, ppchDisplayTextMsg)
		&& itemdef_trh_VerifyUsageRestrictionsCharacterPath(ATR_PASS_ARGS, pRestrict, pEnt, ppchDisplayTextMsg)
		&& itemdef_trh_VerifyUsageRestrictionsAllegiance(ATR_PASS_ARGS, pRestrict, pOwner, pEnt, ppchDisplayTextMsg);
}

//OverrideLevel gets passed in if the item's kItemDef_LevelFromSource flag is set, meaning the item takes its min level and restriction level from the entity that drops it
bool itemdef_VerifyUsageRestrictions(int iPartitionIdx, Entity *pEnt, ItemDef *pItemDef, int iOverrideLevel, const char **ppchDisplayTextMsg, int iDestinationSlot)
{
	Entity* pOwner = NULL;
	int iOwnerPartitionIdx;
	if (pEnt && pEnt->pSaved)
	{
		pOwner = entFromContainerID(iPartitionIdx, pEnt->pSaved->conOwner.containerType, pEnt->pSaved->conOwner.containerID);
	}
	if (!pOwner)
	{
		pOwner = pEnt;
	}
	if (!itemdef_trh_VerifyUsageRestrictions(ATR_EMPTY_ARGS, CONTAINER_NOCONST(Entity, pOwner), CONTAINER_NOCONST(Entity, pEnt), pItemDef, iOverrideLevel, ppchDisplayTextMsg))
	{
		return false;
	}
#ifdef GAMESERVER
	if (GetAppGlobalType() == GLOBALTYPE_WEBREQUESTSERVER || GetAppGlobalType() == GLOBALTYPE_GATEWAYSERVER)
		iOwnerPartitionIdx = WEBREQUEST_PARTITION_INDEX;
	else
		iOwnerPartitionIdx = iPartitionIdx;
#else
	iOwnerPartitionIdx = iPartitionIdx;
#endif

	if (!itemdef_CheckRequiresExpression(iOwnerPartitionIdx, pOwner, pEnt, pItemDef, ppchDisplayTextMsg, iDestinationSlot))
	{
		return false;
	}
	return true;
}

bool item_IsUpgrade(ItemDef *pItemDef)
{
	if ( pItemDef && 
		(pItemDef->eType == kItemType_Upgrade) )
		return true;

	return false;
}

bool item_IsPrimaryUpgrade(ItemDef *pItemDef)
{
	if ( pItemDef && 
		 (pItemDef->eType == kItemType_Upgrade) &&
		 (pItemDef->eRestrictSlotType == kSlotType_Primary) )
		return true;

	return false;
}

bool item_IsSecondaryUpgrade(ItemDef *pItemDef)
{
	if ( pItemDef && 
		((pItemDef->eType == kItemType_Upgrade) || (pItemDef->eType == kItemType_Weapon)) &&
		(pItemDef->eRestrictSlotType == kSlotType_Secondary) )
		return true;

	return false;
}

bool item_IsPrimaryEquip(ItemDef *pItemDef)
{
	if ( pItemDef && 
		 (pItemDef->eRestrictSlotType == kSlotType_Primary) )
		return true;

	return false;
}

AUTO_TRANS_HELPER_SIMPLE;
bool item_IsSecondaryEquip(ItemDef *pItemDef)
{
	if ( pItemDef && 
		(pItemDef->eRestrictSlotType == kSlotType_Secondary) )
		return true;

	return false;
}

bool item_IsRecipe(ItemDef *pItemDef)
{
	if (pItemDef)
	{
		switch (pItemDef->eType)
		{
		case kItemType_ItemRecipe: 
		case kItemType_ItemValue: 
		case kItemType_ItemPowerRecipe:
			return true;
		}
	}
		
	return false;
}

bool item_IsMission(ItemDef *pItemDef)
{
	if (pItemDef && (pItemDef->eType == kItemType_Mission) )
		return true;

	return false;
}

bool item_IsMissionGrant(ItemDef *pItemDef)
{
	if (pItemDef && (pItemDef->eType == kItemType_MissionGrant) )
		return true;

	return false;
}

bool item_IsSavedPet(ItemDef *pItemDef)
{
	if (pItemDef && (pItemDef->eType == kItemType_SavedPet))
		return true;

	return false;
}

bool item_isAlgoPet(ItemDef *pItemDef)
{
	if (pItemDef && (pItemDef->eType == kItemType_AlgoPet))
		return true;

	return false;
}

bool item_IsAttributeModify(ItemDef *pItemDef)
{
	if(pItemDef && pItemDef->eType == kItemType_ModifyAttribute)
		return true;

	return false;
}

bool item_IsVanityPet(ItemDef *pItemDef)
{
	if (pItemDef && (pItemDef->eType == kItemType_VanityPet))
		return true;

	return false;
}

bool item_IsCostumeUnlock(ItemDef *pItemDef)
{
	if (pItemDef && (pItemDef->eType == kItemType_CostumeUnlock))
		return true;

	return false;
}

bool item_IsInjuryCureGround(ItemDef *pItemDef)
{
	if ( pItemDef && 
		(pItemDef->eType == kItemType_InjuryCureGround) )
		return true;

	return false;
}

bool item_IsInjuryCureSpace(ItemDef *pItemDef)
{
	if ( pItemDef && 
		(pItemDef->eType == kItemType_InjuryCureSpace) )
		return true;

	return false;
}

bool item_IsRewardPack(ItemDef *pItemDef)
{
	if (pItemDef && pItemDef->eType == kItemType_RewardPack)
	{
		return true;
	}
	return false;
}

bool item_IsMicroSpecial(ItemDef *pItemDef)
{
	if (pItemDef && pItemDef->eType == kItemType_GrantMicroSpecial)
	{
		return true;
	}
	return false;
}

bool item_IsExperienceGift(ItemDef *pItemDef)
{
	if (pItemDef && pItemDef->eType == kItemType_ExperienceGift)
	{
		return true;
	}
	return false;
}



int item_CountOwned(Entity *e, ItemDef *pDef )
{
	if (!pDef)
		return 0;

	return inv_ent_AllBagsCountItems(e, pDef->pchName);
}

AUTO_TRANS_HELPER
ATR_LOCKS(pEntity, ".Hallegiance, .Hsuballegiance");
const char* item_trh_GetDefLocalNameKeyFromEnt(SA_PARAM_NN_VALID const ItemDef* pDef, ATH_ARG NOCONST(Entity)* pEntity)
{
	DisplayMessage pDisplayMessage = entity_GetItemDisplayNameMessage(pEntity, pDef);
	Message *pMsg = GET_REF(pDisplayMessage.hMessage);
	if (pMsg) {
		return pMsg->pcMessageKey;
	}
	return "";
}

const char* item_GetDefLocalNameFromEnt(SA_PARAM_NN_VALID const ItemDef* pDef, Entity* pEntity, Language langID)
{
	const char* pchKey = item_trh_GetDefLocalNameKeyFromEnt(pDef, CONTAINER_NOCONST(Entity, pEntity));
	if (pchKey)
	{
		return langTranslateMessageKey(langID, pchKey);
	}
	return "";
}

static const char* item_GetDefLocalNameKey(const ItemDef* pDef)
{
	Message *pMsg = GET_REF(pDef->displayNameMsg.hMessage);
	if (pMsg) {
		return pMsg->pcMessageKey;
	}
	return "";
}

const char* item_GetDefLocalName(const ItemDef* pDef, Language langID)
{
	const char* pchKey = item_GetDefLocalNameKey(pDef);
	if (pchKey) {
		return langTranslateMessageKey(langID, pchKey);
	}
	return "";
}

AUTO_TRANS_HELPER_SIMPLE;
static const char* item_GetDefLocalUnIDedNameKey(const ItemDef* pDef)
{
	Message *pMsg = GET_REF(pDef->displayNameMsgUnidentified.hMessage);
	if (pMsg) {
		return pMsg->pcMessageKey;
	}
	else
		return "Item.DefaultUnidentifiedName";
}

const char* item_GetTranslatedDescription(Item* pItem, Language langID)
{
	ItemDef* pDef = GET_REF(pItem->hItem);
	if (item_IsUnidentified(pItem))
	{
		Message *pMsg = GET_REF(pDef->descriptionMsgUnidentified.hMessage);
		if (pMsg) {
			return langTranslateMessageKey(langID, pMsg->pcMessageKey);
		}
		else
			return langTranslateMessageKey(langID, "Item.DefaultUnidentifiedDesc");
	}
	else
	{
		Message *pMsg = GET_REF(pDef->descriptionMsg.hMessage);
		if (pMsg) {
			return langTranslateMessageKey(langID, pMsg->pcMessageKey);
		}
		else
			return NULL;
	}
}

//AUTO_TRANS_HELPER;
//const char* item_trh_GetLocalName( ATH_ARG NOCONST(Item)* pItem )
//{
//	ItemDef *pItemDef = GET_REF(pItem->hItem);
//
//	if( pItem->pchDisplayName )
//		return pItem->pchDisplayName;
//
//	return item_GetDefLocalName(pItemDef);
//}

//AUTO_CMD_INT
//int g_item_debug;

static bool item_GetDisplayNameFromCritterDef(Entity* pPlayerEnt,
											  CritterDef* pCritterDef, 
											  S32 iCostumeIndex,
											  char** pestrName)
{
	CritterCostume* pCostume = pCritterDef ? eaGet(&pCritterDef->ppCostume,iCostumeIndex) : NULL;
	if (!pCostume)
		return false;

	if (IS_HANDLE_ACTIVE(pCostume->displaySubNameMsg.hMessage))
	{
		langFormatMessage(entGetLanguage(pPlayerEnt),
						  pestrName,
						  GET_REF(pCostume->displaySubNameMsg.hMessage), 
						  STRFMT_END);
		estrCopy2(pestrName, FormalName_GetFullNameFromSubName(*pestrName));
	}
	else
	{
		langFormatMessage(entGetLanguage(pPlayerEnt),
						  pestrName,
						  GET_REF(pCostume->displayNameMsg.hMessage), 
						  STRFMT_END);
	}
	return true;
}

bool item_GetDisplayNameFromPetCostume(Entity* pPlayerEnt, Item* pItem, char** pestrName, PlayerCostume** ppCostume)
{
	ItemDef* pItemDef = SAFE_GET_REF(pItem, hItem);
	PetDef* pPetDef = SAFE_GET_REF(pItemDef, hPetDef);
	CritterDef* pCritterDef = SAFE_GET_REF(pPetDef, hCritterDef);
	SpecialItemProps* pProps;

	if (!pPlayerEnt || !pPetDef || !pestrName)
		return false;

	pProps = SAFE_MEMBER(pItem, pSpecialProps);

	estrClear(pestrName);
	if(pProps && pProps->pAlgoPet && pProps->pAlgoPet->pCostume)
	{
		if (ppCostume)
		{
			(*ppCostume) = pProps->pAlgoPet->pCostume;
		}
		if (pProps->pAlgoPet->pchPetName)
		{
			 estrConcat(pestrName, pProps->pAlgoPet->pchPetName, (U32)strlen(pProps->pAlgoPet->pchPetName));
		}
		else
		{
			if (!item_GetDisplayNameFromCritterDef(pPlayerEnt, 
												   pCritterDef, 
												   pProps->pAlgoPet->iCostume, 
												   pestrName))
			{
				return false;
			}
		}
	}
	else if(pProps && pProps->pAlgoPet)
	{
		if (ppCostume && eaGet(&pCritterDef->ppCostume, pProps->pAlgoPet->iCostume))
		{
			(*ppCostume) = GET_REF(pCritterDef->ppCostume[pProps->pAlgoPet->iCostume]->hCostumeRef);
		}
		if (pProps->pAlgoPet->pchPetName)
		{
			estrConcat(pestrName, pProps->pAlgoPet->pchPetName, (U32)strlen(pProps->pAlgoPet->pchPetName));
		}
		else
		{
			if (!item_GetDisplayNameFromCritterDef(pPlayerEnt,
												   pCritterDef,
												   pProps->pAlgoPet->iCostume, 
												   pestrName))
			{
				return false;
			}
		}
	} else if(pCritterDef) {
		//just use the first costume for now
		if (ppCostume && eaSize(&pCritterDef->ppCostume))
		{
			(*ppCostume) = GET_REF(pCritterDef->ppCostume[0]->hCostumeRef);
		}
		if (!item_GetDisplayNameFromCritterDef(pPlayerEnt, pCritterDef, 0, pestrName))
		{
			return false;
		}
	} else { 
		//default to the PetDef display name
		if (ppCostume)
		{
			(*ppCostume) = NULL;
		}
		langFormatMessage(entGetLanguage(pPlayerEnt),
						  pestrName,
						  GET_REF(pPetDef->displayNameMsg.hMessage),
						  STRFMT_END);
	}
	return true;
}

void item_GetNameFromUntranslatedStrings(Language eLangID, bool bTranslate, 
										 const char** ppchNames, char** pestrOut)
{
	S32 i;
	for (i = 0; i < eaSize(&ppchNames); i++)
	{
		const char* pchName = ppchNames[i];
		if (bTranslate)
		{
			pchName = langTranslateMessageKey(eLangID, pchName);
		}
		if ((*pestrOut) && (*pestrOut)[0] && pchName && pchName[0])
		{
			estrAppend2(pestrOut, " ");	
		}
		estrAppend2(pestrOut, pchName);
	}
}

// This extended version of the function does the gender translation for you, and also translates any variables
// that are valid to have in item text
void item_GetFormattedNameFromUntranslatedStrings(Entity * pEnt,ItemDef * pItemDef, Item * pItem,Language eLangID, bool bTranslate, 
										 const char** ppchNames, char** pestrOut)
{
	int iPowerFactor = 0;
	if (pItem && pItem->pAlgoProps)
	{
		iPowerFactor = pItem->pAlgoProps->iPowerFactor;
	}
	item_GetNameFromUntranslatedStrings(eLangID,bTranslate,ppchNames,pestrOut);

	if (pestrOut && pestrOut[0])
	{
		char* estrItemName = NULL;
		estrStackCreate(&estrItemName);
		estrAppend2(&estrItemName, *pestrOut);
		estrClear(pestrOut);
		if (pEnt && item_ShouldGetGenderSpecificName(pItemDef))
		{
			langFormatGameString(eLangID, pestrOut, estrItemName, STRFMT_PLAYER(pEnt), STRFMT_INT("ItemPowerFactor",iPowerFactor), STRFMT_END);
		}
		else
		{
			langFormatGameString(eLangID, pestrOut, estrItemName, STRFMT_INT("ItemPowerFactor",iPowerFactor), STRFMT_END);
		}
		estrDestroy(&estrItemName);
	}
}

bool item_ShouldGetGenderSpecificName(ItemDef* pItemDef)
{
	return pItemDef && pItemDef->eType == kItemType_Title;
}

const char* item_GetNameLangInternal(Item* pItem, Language eLangID, Entity* pEnt)
{
	static const char** s_ppchNames = NULL;
	bool bRequiresTranslation = false;
	
	eaClear(&s_ppchNames);
	estrClear(&pItem->pchDisplayName);
	item_trh_GetNameUntranslated(CONTAINER_NOCONST(Item, pItem), 
								CONTAINER_NOCONST(Entity, pEnt),
								&s_ppchNames,
								&bRequiresTranslation);
	item_GetFormattedNameFromUntranslatedStrings(pEnt,GET_REF(pItem->hItem),pItem,eLangID, 
										bRequiresTranslation, 
										s_ppchNames, 
										&pItem->pchDisplayName);

	return pItem->pchDisplayName;
}

const char* itemdef_GetNameLangInternal(char** pestrDisplayName, ItemDef* pItemDef, Language eLangID, Entity* pEnt)
{
	static const char** s_ppchNames = NULL;
	static char *s_estrDisplayName;

	if (!pestrDisplayName)
		pestrDisplayName = &s_estrDisplayName;

	eaClear(&s_ppchNames);
	estrClear(pestrDisplayName);

	// An ItemDef will not have any algo properties so this limited
	// snippet of item_trh_GetNameUntranslated() should be enough.
	if (itemdef_IsUnidentified(pItemDef))
	{
		eaPush(&s_ppchNames, item_GetDefLocalUnIDedNameKey(pItemDef));
	}
	else if(NONNULL(pEnt))
	{
		eaPush(&s_ppchNames, item_trh_GetDefLocalNameKeyFromEnt(pItemDef, CONTAINER_NOCONST(Entity, pEnt)));
	}
	else
	{
		eaPush(&s_ppchNames, item_GetDefLocalNameKey(pItemDef));
	}

	item_GetFormattedNameFromUntranslatedStrings(pEnt,pItemDef,NULL,eLangID, 
										true, 
										s_ppchNames, 
										pestrDisplayName);

	return *pestrDisplayName;
}

AUTO_TRANS_HELPER
	ATR_LOCKS(pItem, ".Flags, .Palgoprops.Ppitempowerdefrefs, .Hitem, .Pspecialprops.Pcontainerinfo.Hsavedpet, .Pspecialprops.Psupercritterpet.Pchname")
	ATR_LOCKS(pEnt, ".Hallegiance, .Hsuballegiance");
void item_trh_GetNameUntranslated(ATH_ARG NOCONST(Item)* pItem, 
								  ATH_ARG NOCONST(Entity)* pEnt, 
								  const char*** peaNames,
								  bool* pbRequiresTranslation)
{
	ItemDef *pItemDef = NONNULL(pItem) ? GET_REF(pItem->hItem) : NULL;
    Message *pMsg;
    ItemPowerDef *pItemPower;
	int ii;

	if (!pItemDef)
		return;
    
	if (pItemDef->eType == kItemType_Container && NONNULL(pItem->pSpecialProps)) {

		Entity* pItemEnt = NONNULL(pItem->pSpecialProps->pContainerInfo) ? GET_REF(pItem->pSpecialProps->pContainerInfo->hSavedPet) : NULL;
		if(pItemEnt) {
			eaPush(peaNames, entGetLangNameUntranslated(pItemEnt, pbRequiresTranslation));
			return;
		}
	}

	(*pbRequiresTranslation) = true;
	if (!(pItem->flags & kItemFlag_Algo))
	{
		if (item_trh_IsUnidentified(pItem))
		{
			eaPush(peaNames, item_GetDefLocalUnIDedNameKey(pItemDef));
			return;
		}
		//if it's a named Super Critter Pet, return the name instead.
		if (NONNULL(pItem) && NONNULL(pItem->pSpecialProps) && NONNULL(pItem->pSpecialProps->pSuperCritterPet))
		{	
			if(pItem->pSpecialProps->pSuperCritterPet->pchName)
			{
				eaPush(peaNames, pItem->pSpecialProps->pSuperCritterPet->pchName);
				(*pbRequiresTranslation) = false;
				return;
			}
		}
		if(NONNULL(pEnt))
		{
			eaPush(peaNames, item_trh_GetDefLocalNameKeyFromEnt(pItemDef, pEnt));
			return;
		}
		eaPush(peaNames, item_GetDefLocalNameKey(pItemDef));
		return;
    }

    //add quality...
    //add magnitude...
    
	if (gConf.bUseNNOAlgoNames)
	{
		if ((pItemDef->eType == kItemType_Upgrade || pItemDef->eType == kItemType_Weapon))
		{
			//add prefixes
			if (NONNULL(pItem->pAlgoProps))
			{
				for (ii = 0; ii < eaSize(&pItem->pAlgoProps->ppItemPowerDefRefs); ii++)
				{
					pItemPower = GET_REF(pItem->pAlgoProps->ppItemPowerDefRefs[ii]->hItemPowerDef);
					pMsg = pItemPower ? GET_REF(pItemPower->displayNameMsg.hMessage) : NULL;
					if (pMsg) 
					{
						eaPush(peaNames, pMsg->pcMessageKey);
					}
				}
			}
			//add prefixes
			for (ii = 0; ii < eaSize(&pItemDef->ppItemPowerDefRefs); ii++)
			{
				pItemPower = GET_REF(pItemDef->ppItemPowerDefRefs[ii]->hItemPowerDef);
				pMsg = pItemPower ? GET_REF(pItemPower->displayNameMsg.hMessage) : NULL;
				if (pMsg) 
				{
					eaPush(peaNames, pMsg->pcMessageKey);
				}
			}
		}
		//base name
		pMsg = pItemDef ? GET_REF(pItemDef->displayNameMsg.hMessage) : NULL;
		if (pMsg) 
		{
			eaPush(peaNames, pMsg->pcMessageKey);
		}

		if (pItemDef->eType == kItemType_Upgrade || pItemDef->eType == kItemType_Weapon)
		{
			//suffixes
			if (NONNULL(pItem->pAlgoProps))
			{
				for (ii = 0; ii < eaSize(&pItem->pAlgoProps->ppItemPowerDefRefs); ii++)
				{
					pItemPower = GET_REF(pItem->pAlgoProps->ppItemPowerDefRefs[ii]->hItemPowerDef);
					pMsg = pItemPower ? GET_REF(pItemPower->displayNameMsg2.hMessage) : NULL;
					if (pMsg) 
					{
						eaPush(peaNames, pMsg->pcMessageKey);
					}
				}
			}
			for (ii = 0; ii < eaSize(&pItemDef->ppItemPowerDefRefs); ii++)
			{
				pItemPower = GET_REF(pItemDef->ppItemPowerDefRefs[ii]->hItemPowerDef);
				pMsg = pItemPower ? GET_REF(pItemPower->displayNameMsg2.hMessage) : NULL;
				if (pMsg) 
				{
					eaPush(peaNames, pMsg->pcMessageKey);
				}
			}
		}
		return;
	}
	if (NONNULL(pItem->pAlgoProps))
	{
		// for more than 2 pItem powers, put a "legendary" prefix instead of prepending 
		// a ton of characteristic words
		if (eaSize(&pItem->pAlgoProps->ppItemPowerDefRefs) > 2)
		{
			eaPush(peaNames, "AlgoItem.LegendaryItem");
		}

		// add characteristic display name
		if (eaSize(&pItem->pAlgoProps->ppItemPowerDefRefs) >= 2)
		{
			pItemPower = GET_REF(pItem->pAlgoProps->ppItemPowerDefRefs[1]->hItemPowerDef);
			pMsg = pItemPower ? GET_REF(pItemPower->displayNameMsg2.hMessage) : NULL;
			if (pMsg) 
			{
				eaPush(peaNames, pMsg->pcMessageKey);
			}
		}
    
		//add base display name
		if (eaSize(&pItem->pAlgoProps->ppItemPowerDefRefs) >= 1)
		{
			pItemPower = GET_REF(pItem->pAlgoProps->ppItemPowerDefRefs[0]->hItemPowerDef);
			pMsg = pItemPower ? GET_REF(pItemPower->displayNameMsg.hMessage) : NULL;
			if (pMsg) 
			{
				eaPush(peaNames, pMsg->pcMessageKey);
			}
		}
	}
    //add costume display name
    pMsg = GET_REF(pItemDef->displayNameMsg.hMessage);
	if (pMsg)
	{
		eaPush(peaNames, pMsg->pcMessageKey);
	}
}



bool entity_HasSkill(Entity *pEnt, SkillType eType)
{
	if ( pEnt &&
		 pEnt->pPlayer )
	{
		if ( pEnt->pPlayer->SkillType == eType )
			return true;
	}

	return false;
}

SkillType entity_GetSkill(Entity *pEnt)
{
	if ( pEnt &&
		pEnt->pPlayer )
	{
		return pEnt->pPlayer->SkillType;
	}

	return 0;
}

S32 entity_GetSkillLevel(const Entity *pEnt)
{
	return inv_GetNumericItemValue(pEnt, "Skilllevel");
}

AUTO_EXPR_FUNC_STATIC_CHECK;
ExprFuncReturnVal exprFuncPlayerSkillLevelLoadVerify(ExprContext *context, ACMD_EXPR_INT_OUT piReturnVal, const char *skillName, ACMD_EXPR_ERRSTRING errEstr)
{
	if (StaticDefineIntGetInt(SkillTypeEnum, skillName) == -1)
	{
		estrPrintf(errEstr, "Unknown skilltype: %s.", skillName);
		return ExprFuncReturnError;
	}

	return ExprFuncReturnFinished;
}

// Returns the player's skill level with the specified skill.  If the player 
// doesn't have the skill, returns -1
AUTO_EXPR_FUNC(player) ACMD_NAME(PlayerSkillLevel) ACMD_EXPR_STATIC_CHECK(exprFuncPlayerSkillLevelLoadVerify);
ExprFuncReturnVal exprFuncPlayerSkillLevel(ExprContext *context, ACMD_EXPR_INT_OUT piReturnVal, const char *skillName, ACMD_EXPR_ERRSTRING errEstr)
{
	Entity* playerEnt = exprContextGetVarPointerUnsafe(context, "Player");

	if (entity_HasSkill(playerEnt, StaticDefineIntGetInt(SkillTypeEnum, skillName)))
		*piReturnVal = entity_GetSkillLevel(playerEnt);
	else
		*piReturnVal = -1;
	return ExprFuncReturnFinished;
}

// Nothing seems to be set up to use this so commenting it out
//AUTO_EXPR_FUNC(Item);
//S32 GetLevel(ExprContext* pContext)
/*
{
	Entity *e = exprContextGetSelfPtr(pContext);

	if ( e && e->pChar )
	{
		return e->pChar->iLevelCombat;
	}
	else
	{
		return 0;
	}
}
*/

//////////////////////////////////////////////////////////////////////////
// ITEM POWER DEF REFS
//
// ItemPowerDefRefs on an Item are treated as a logical list.
//
// This list starts with all of the ItemPowerDefRefs on the ItemDef
// Followed by any ItemsPowerDefRefs on the Item instance:
//  From algo items
//  From Item's list of InfuseSlots
//
// The counting and indexing MUST follow that assumption
//
//////////////////////////////////////////////////////////////////////////

AUTO_TRANS_HELPER;
int item_trh_GetNumGemsPowerDefs(ATH_ARG NOCONST(Item) *pItem)
{
	if(NONNULL(pItem) && NONNULL(pItem->pSpecialProps))
	{
		int i;
		ItemDef *pItemDef = GET_REF(pItem->hItem);
		int iItemGemDefs = 0;

		for(i=eaSize(&pItem->pSpecialProps->ppItemGemSlots)-1;i>=0;i--)
		{
			NOCONST(ItemGemSlot) *pSlot = pItem->pSpecialProps->ppItemGemSlots[i];

			if(IS_HANDLE_ACTIVE(pSlot->hSlottedItem))
			{
				ItemDef *pGemDef = GET_REF(pSlot->hSlottedItem);

				if(pGemDef)
					iItemGemDefs += eaSize(&pGemDef->ppItemPowerDefRefs);
			}
		}

		return iItemGemDefs;
	}

	return 0;
}


// Returns the total number of ItemPowerDefs on the Item instance
AUTO_TRANS_HELPER
ATR_LOCKS(pItem, ".palgoprops.Ppitempowerdefrefs[AO], .pspecialprops.ppitemgemslots, .Hitem");
int item_trh_GetNumItemPowerDefs(ATH_ARG NOCONST(Item) *pItem, bool bIncludeGems)
{
	if(NONNULL(pItem))
	{
		ItemDef *pItemDef = GET_REF(pItem->hItem);
		int iItemDef = pItemDef ? eaSize(&pItemDef->ppItemPowerDefRefs): 0;
		int iItemInstance = NONNULL(pItem->pAlgoProps) ? eaSize(&pItem->pAlgoProps->ppItemPowerDefRefs) : 0;

		return iItemDef + iItemInstance + (bIncludeGems ? item_trh_GetNumGemsPowerDefs(pItem) : 0);
	}

	return 0;
}

ItemGemSlotDef* GetGemSlotDefFromPowerIdx(Item *pItem, int PowerIdx)
{
	int iPowerDefs = item_trh_GetNumItemPowerDefs((NOCONST(Item)*)pItem, true);
	int iGemDefs = item_trh_GetNumGemsPowerDefs((NOCONST(Item)*)pItem);
	ItemDef *pItemDef = GET_REF(pItem->hItem);

	if(PowerIdx < iPowerDefs && PowerIdx >= iPowerDefs - iGemDefs)
	{
		// In one of the Item's gem refs
		int i,s = pItem->pSpecialProps ? eaSize(&pItem->pSpecialProps->ppItemGemSlots) : 0;
		int iPowerIdxGem = PowerIdx-(iPowerDefs-iGemDefs);

		for(i=0;i<s;i++)
		{
			ItemDef *pGemDef = GET_REF(pItem->pSpecialProps->ppItemGemSlots[i]->hSlottedItem);

			if(pGemDef && iPowerIdxGem < eaSize(&pGemDef->ppItemPowerDefRefs))
			{
				return i < eaSize(&pItemDef->ppItemGemSlots) ? pItemDef->ppItemGemSlots[i] : NULL;
			}
			if(pGemDef)
				iPowerIdxGem -= eaSize(&pGemDef->ppItemPowerDefRefs);
		}
	}

	return NULL;
}

// Returns the specific ItemPowerDefRef on the Item instance
AUTO_TRANS_HELPER
ATR_LOCKS(pItem, ".palgoprops.Ppitempowerdefrefs, .pspecialprops.ppitemgemslots, .Hitem");
ItemPowerDefRef* item_trh_GetItemPowerDefRef(ATH_ARG NOCONST(Item)* pItem, int PowerIdx)
{
	ItemPowerDefRef *pItemPowerDefRef = NULL;
	ItemDef *pItemDef = NULL;
	int iNumItemDef = 0;
	int iNumItemInstance = 0;

	if(ISNULL(pItem))
		return NULL;

	pItemDef = GET_REF(pItem->hItem);

	if(!pItemDef)
		return NULL;

	iNumItemDef = eaSize(&pItemDef->ppItemPowerDefRefs);
	iNumItemInstance = NONNULL(pItem->pAlgoProps) ? eaSize(&pItem->pAlgoProps->ppItemPowerDefRefs) : 0;

	if (PowerIdx < 0)
	{
	}
	else if (PowerIdx < iNumItemDef)
	{
		// On ItemDef
		pItemPowerDefRef = pItemDef->ppItemPowerDefRefs[PowerIdx];
	}
	else if (PowerIdx < iNumItemDef+iNumItemInstance)
	{
		// On explicit Item list at index less the number on the ItemDef
		pItemPowerDefRef = (ItemPowerDefRef*)pItem->pAlgoProps->ppItemPowerDefRefs[PowerIdx-iNumItemDef];
	}
	else
	{
		// In one of the Item's gem refs
		int i,s = NONNULL(pItem->pSpecialProps) ? eaSize(&pItem->pSpecialProps->ppItemGemSlots) : 0;
		int iPowerIdxGem = PowerIdx-(iNumItemDef+iNumItemInstance);

		for(i=0;i<s;i++)
		{
			ItemDef *pGemDef = GET_REF(pItem->pSpecialProps->ppItemGemSlots[i]->hSlottedItem);

			if(pGemDef && iPowerIdxGem < eaSize(&pGemDef->ppItemPowerDefRefs))
			{
				pItemPowerDefRef = pGemDef->ppItemPowerDefRefs[iPowerIdxGem];
				break;
			}
			if(pGemDef)
				iPowerIdxGem -= eaSize(&pGemDef->ppItemPowerDefRefs);
		}
	}

	return pItemPowerDefRef;
}

// Returns the specific ItemPowerDef on the Item instance
AUTO_TRANS_HELPER
ATR_LOCKS(pItem, ".palgoprops.Ppitempowerdefrefs, .pspecialprops.ppitemgemslots, .Hitem");
ItemPowerDef* item_trh_GetItemPowerDef(ATH_ARG NOCONST(Item)* pItem, int PowerIdx)
{
	ItemPowerDefRef *pItemPowerDefRef = item_trh_GetItemPowerDefRef(pItem, PowerIdx);

	if(pItemPowerDefRef)
		return GET_REF(pItemPowerDefRef->hItemPowerDef);

	return NULL;
}

F32 item_GetGemSlotPowerPortion(Item* pItem)
{
	int i;
	ItemDef* pDef = GET_REF(pItem->hItem);
	for (i = 0; i < eaiSize(&pDef->peCategories); i++)
	{
		ItemCategory eCat = pDef->peCategories[i];
		if (eCat >= kItemCategory_FIRST_DATA_DEFINED)
		{
			ItemCategoryInfo *pInfo = eaGet(&g_ItemCategoryNames.ppInfo, eCat - kItemCategory_FIRST_DATA_DEFINED);
			if (pInfo->fGemSlotPowerPortion > 0.0)
				return pInfo->fGemSlotPowerPortion;
		}
	}
	return 0.0;
}

ItemCategoryInfo* item_GetItemCategoryInfo(ItemCategory eCat)
{
	if (eCat >= kItemCategory_FIRST_DATA_DEFINED)
		return eaGet(&g_ItemCategoryNames.ppInfo, eCat - kItemCategory_FIRST_DATA_DEFINED);

	return NULL;
}

// Returns the scale factor of the specific ItemPowerDefRef on the Item instance
F32 item_GetItemPowerScale(Item *pItem, int PowerIdx)
{
	ItemPowerDefRef *pItemPowerDefRef = item_GetItemPowerDefRef(pItem, PowerIdx);
	ItemDef* pDef = pItem ? GET_REF(pItem->hItem) : NULL;
	F32 fAdjust = 0;
	if (pItemPowerDefRef && pItemPowerDefRef->bGemSlotsAdjustScaleFactor && pDef && eaSize(&pDef->ppItemGemSlots) > 0)
	{
		/*
		int iPowers = item_GetNumItemPowerDefs(pItem, false);
		int i;
		F32 fScaleFactorSum = 0.0;
		for(i = 0; i < iPowers; i++)
		{
			ItemPowerDefRef* pDefRef = item_GetItemPowerDefRef(pItem, i);
			fScaleFactorSum += (pDefRef && pDefRef->bGemSlotsAdjustScaleFactor) ? pDefRef->ScaleFactor : 0;
		}
		*/
		fAdjust = -(pItemPowerDefRef->ScaleFactor * item_GetGemSlotPowerPortion(pItem) * eaSize(&pDef->ppItemGemSlots));
	}
	return (pItemPowerDefRef ? pItemPowerDefRef->ScaleFactor + fAdjust : 0);
}

// Returns the specific PowerDef on the Item instance (based on the PowerIdx)
PowerDef* item_GetPowerDef(Item *pItem, int PowerIdx)
{
	ItemPowerDef *pItemPowerDef = item_GetItemPowerDef(pItem, PowerIdx);
	return (pItemPowerDef ? GET_REF(pItemPowerDef->hPower) : NULL);
}

// Returns the Power* on an Item associated with a particular ItemPower index
//  NOTE: This is non-trivial due to Innates not being instantiated, plus the fact
//  that an Item is allowed to have multiple copies of the same PowerDef.  It'd
//  be nice if we could cache this mapping somewhere, but for now we just have
//  to do a careful search.
AUTO_TRANS_HELPER;
NOCONST(Power)* item_trh_GetPower(ATH_ARG NOCONST(Item) *pItem, int iItemPowerIdx)
{
	ItemPowerDef *pItemPowerDef = item_GetItemPowerDefNoConst(pItem, iItemPowerIdx);
	PowerDef *pPowerDef = pItemPowerDef ? GET_REF(pItemPowerDef->hPower) : NULL;
	NOCONST(Power) *pPower = NULL;

	if(pPowerDef && pPowerDef->eType!=kPowerType_Innate)
	{
		// In order to properly do an exact match, iterate over the ItemPowerDefs,
		//  and for each one, increment the Power* index if there is a match match.
		//  This should never fail due to item_trh_FixupPowers().
		int i, iPower;
		int iPowers = eaSize(&pItem->ppPowers);
		for(i=0, iPower=0; i<=iItemPowerIdx && iPower<iPowers; i++)
		{
			ItemPowerDef *pItemPowerDefMatch = item_GetItemPowerDefNoConst(pItem, i);
			PowerDef *pPowerDefMatch = pItemPowerDefMatch ? GET_REF(pItemPowerDefMatch->hPower) : NULL;
			if(pPowerDefMatch
				&& pPowerDefMatch->eType!=kPowerType_Innate
				&& pPowerDefMatch==GET_REF(pItem->ppPowers[iPower]->hDef))
			{
				// We found the Power* for the ith ItemPowerDef at index iPower.  If this was the
				//  ItemPowerDef we wanted, set the Power* and break, otherwise increment iPower
				//  and continue.
				if(i==iItemPowerIdx)
				{
					pPower = pItem->ppPowers[iPower];
					break;
				}
				else
				{
					iPower++;
				}
			}
		}

		if(!pPower && pItem->pSpecialProps)
		{
			int iSlot;
			// If the power wasn't found, then it is in one of the gems
			for(iSlot=0;iSlot<eaSize(&pItem->pSpecialProps->ppItemGemSlots);iSlot++)
			{
				NOCONST(ItemGemSlot) *pSlot = pItem->pSpecialProps->ppItemGemSlots[iSlot];
				ItemDef *pGemDef = GET_REF(pSlot->hSlottedItem);
				iPowers = eaSize(&pSlot->ppPowers);

				for(iPower=0; i <=iItemPowerIdx && iPower<iPowers;i++)
				{
					ItemPowerDef *pItemPowerDefMatch = item_GetItemPowerDefNoConst(pItem, i);
					PowerDef *pPowerDefMatch = pItemPowerDefMatch ? GET_REF(pItemPowerDefMatch->hPower) : NULL;
					if(pPowerDefMatch
						&& pPowerDefMatch->eType!=kPowerType_Innate
						&& pPowerDefMatch==GET_REF(pSlot->ppPowers[iPower]->hDef))
					{
						// We found the Power* for the ith ItemPowerDef at index iPower.  If this was the
						//  ItemPowerDef we wanted, set the Power* and break, otherwise increment iPower
						//  and continue.
						if(i==iItemPowerIdx)
						{
							pPower = pSlot->ppPowers[iPower];
							break;
						}
						else
						{
							iPower++;
						}
					}
				}

				if(pPower || i>iItemPowerIdx)
					break;
			}
		}
	}

	return pPower;
}


// Returns the ItemDef's (NOT Item instance) ItemPowerDef
ItemPowerDef* itemdef_GetItemPowerDef(ItemDef *pItemDef, int PowerIdx)
{
	ItemPowerDef *pItemPowerDef = NULL;

	if ( pItemDef &&
		 (PowerIdx>=0) &&
		 (PowerIdx<eaSize(&pItemDef->ppItemPowerDefRefs)) )
	{
		ItemPowerDefRef *pItemPowerDefRef = pItemDef->ppItemPowerDefRefs[PowerIdx];
		if ( pItemPowerDefRef )
		{
			pItemPowerDef = GET_REF(pItemPowerDefRef->hItemPowerDef);
		}
	}

	return pItemPowerDef;
}

// Finds the *first* ItemPowerDef on the Item that provides the given PowerDef
ItemPowerDef* item_GetItemPowerDefByPowerDef(Item* pItem, PowerDef* pPowerDef)
{
	int i, iNumItemPowerDefs;

	iNumItemPowerDefs = item_GetNumItemPowerDefs(pItem, true);
	for(i=0; i<iNumItemPowerDefs; i++)
	{
		ItemPowerDef *pItemPowerDef = item_GetItemPowerDef(pItem,i);
		if(pItemPowerDef && GET_REF(pItemPowerDef->hPower)==pPowerDef)
			return pItemPowerDef;
	}

	return NULL;
}

// Returns true if the passed ItemDef has any of the categories in peItemCategories
bool itemdef_HasItemCategory(ItemDef* pItemDef, ItemCategory* peItemCategories)
{
	if (pItemDef)
	{
		S32 i;
		for (i = eaiSize(&peItemCategories)-1; i >= 0; i--)
		{
			if (eaiFind(&pItemDef->peCategories, peItemCategories[i]) >= 0)
			{
				return true;
			}
		}
	}
	return false;
}

// Returns true if the passed ItemDef has all of the categories in peItemCategories
bool itemdef_HasAllItemCategories(ItemDef* pItemDef, ItemCategory* peItemCategories)
{
	if (pItemDef)
	{
		S32 i;
		for (i = eaiSize(&peItemCategories)-1; i >= 0; i--)
		{
			if (eaiFind(&pItemDef->peCategories, peItemCategories[i]) < 0)
			{
				return false;
			}
		}
		return true;
	}
	return false;
}

// Returns true if the passed ItemDef has all of the restrict bags in peRestrictBagIDs
bool itemdef_HasAllRestrictBagIDs(ItemDef* pItemDef, const InvBagIDs * const peRestrictBagIDs)
{
	if (pItemDef)
	{
		S32 i;
		for (i = eaiSize(&peRestrictBagIDs)-1; i >= 0; i--)
		{
			if (eaiFind(&pItemDef->peRestrictBagIDs, peRestrictBagIDs[i]) < 0)
			{
				return false;
			}
		}
		return true;
	}
	return false;
}

// Returns true if the passed ItemDef has all of the restrict bags in peRestrictBagIDs
bool itemdef_HasRestrictBagID(ItemDef* pItemDef, InvBagIDs eRestrictBagID)
{
	if (pItemDef)
	{
		if (eaiFind(&pItemDef->peRestrictBagIDs, eRestrictBagID) < 0)
		{
			return false;
		}
		return true;
	}
	return false;
}

bool item_ItemPowerActive(Entity* pEnt, InventoryBag* pBag, Item* pItem, int PowerIdx)
{
	ItemDef *pItemDef;
	ItemPowerDefRef *pItemPowerDefRef;
	ItemPowerDef *pItemPowerDef;
	ItemGemSlotDef *pGemSlotDef;

	if(!pItem)
		return false;

	pItemDef = GET_REF(pItem->hItem);
	pItemPowerDefRef = item_trh_GetItemPowerDefRef(CONTAINER_NOCONST(Item, pItem), PowerIdx);
	pItemPowerDef = pItemPowerDefRef ? GET_REF(pItemPowerDefRef->hItemPowerDef) : NULL;
	pGemSlotDef = GetGemSlotDefFromPowerIdx(pItem, PowerIdx);

	if(!pItemDef || !pItemPowerDef)
		return false;

	// Check the requires expression.  I don't know why the other restriction data isn't also checked.
	if(pItemPowerDef->pRestriction && pItemPowerDef->pRestriction->pRequires)
	{
		MultiVal mvExprResult = {0};
		itemeval_Eval(entGetPartitionIdx(pEnt), pItemPowerDef->pRestriction->pRequires, pItemDef, pItemPowerDef, pItem, pEnt, pItemDef->iLevel, pItemDef->Quality, 0, pItemDef->pchFileName, -1, &mvExprResult);
		if(!itemeval_GetIntResult(&mvExprResult, pItemDef->pchFileName, pItemPowerDef->pRestriction->pRequires))
			return false;
	}

	if(pItemPowerDef->pRestriction && pItemPowerDef->pRestriction->eRequiredGemSlotType)
	{
		if(pGemSlotDef && !(pItemPowerDef->pRestriction->eRequiredGemSlotType & pGemSlotDef->eType))
			return false;
	}

	//if we're not checking a bag, we're done here.
	if (!pBag)
		return true;

	//if the ItemPowerDefRef has set restrictions that we don't meet, then it's not active
	if(pItemPowerDefRef->uiSetMin && pItem->uSetCount < pItemPowerDefRef->uiSetMin)
		return false;

	//if the item is in a storage only or inactive weapon bag, then all powers in it are not active
	if ( (invbag_flags(pBag) & (InvBagFlag_StorageOnly)) != 0 )
		return false;
	//if this is an equip bag, then everything in it is active
	if ( (invbag_flags(pBag) & InvBagFlag_EquipBag) != 0 )
		return true;

	//if this is a device bag, then everything in it is active
	if ( (invbag_flags(pBag) & InvBagFlag_DeviceBag) != 0 )
		return true;

	//if this is a weapon bag, then everything in it is active
	if ( (invbag_flags(pBag) & InvBagFlag_WeaponBag) != 0 )
		return true;

	//if the item is marked as can use unequipped, then powers active
	if((pItemDef->flags & kItemDefFlag_CanUseUnequipped) != 0)
		return true;

	//check if the itempower is marked as can use unequipped
	if ( (pItemPowerDef->flags & kItemPowerFlag_CanUseUnequipped) != 0 )
		return true;

	//if this is an active weapon bag, then check to see if the item resides in the active slot
	if ( (invbag_flags(pBag) & InvBagFlag_ActiveWeaponBag) != 0 )
	{
		const InvBagDef* pDef = invbag_def(pBag);
		if (pDef)
		{
			S32 i;
			for (i = invbag_maxActiveSlots(pBag) - 1; i >= 0; --i)
			{
				S32 slot = invbag_GetActiveSlot(pBag, i);
				InventorySlot* pActiveSlot = eaGet(&pBag->ppIndexedInventorySlots, slot);
				if(pActiveSlot && pActiveSlot->pItem && pActiveSlot->pItem->id == pItem->id)
					return true;
			}
		}
		return false;
	}

	return false;
}

bool item_AreAnyPowersInUse(Entity* pEnt, Item* pItem)
{
	S32 i, iSize = pItem ? item_GetNumItemPowerDefs( pItem, true ) : 0;

	if ( pEnt==NULL || pEnt->pChar==NULL )
		return false;

	if ( pEnt->pChar->pPowActCurrent==NULL && pEnt->pChar->pPowActQueued==NULL )
		return false;

	for( i = 0; i < iSize; i++ )
	{
		Power* pItemPower = item_GetPower( pItem, i );
		
		if ( pItemPower )
		{
			if ( pEnt->pChar->pPowActCurrent && pEnt->pChar->pPowActCurrent->ref.uiID == pItemPower->uiID )
			{
				return true;
			}
			if ( pEnt->pChar->pPowActQueued && pEnt->pChar->pPowActQueued->ref.uiID == pItemPower->uiID )
			{
				return true;
			}
		}
	}
	return false;
}

// InfuseSlot helper functions

// Gets the InfuseSlotDef on the ItemDef at the given index
InfuseSlotDef* item_GetInfuseSlotDef(ItemDef *pItemDef, S32 iInfuseSlot)
{
	ANALYSIS_ASSUME(pItemDef->ppInfuseSlotDefRefs); // sigh
	if(eaSize(&pItemDef->ppInfuseSlotDefRefs) > iInfuseSlot)
	{
		InfuseSlotDefRef *pDefRef = pItemDef->ppInfuseSlotDefRefs[iInfuseSlot];
		if(pDefRef)
			return GET_REF(pDefRef->hDef);
	}
	return NULL;
}

// Gets the InfuseOption for the InfuseSlotDef given the option Item
AUTO_TRANS_HELPER_SIMPLE;
InfuseOption* infuse_GetDefOption(InfuseSlotDef *pInfuseDef, ItemDef *pItemOption)
{
	int i;
	for(i=eaSize(&pInfuseDef->ppOptions)-1; i>=0; i--)
	{
		if(GET_REF(pInfuseDef->ppOptions[i]->hItem)==pItemOption)
			return pInfuseDef->ppOptions[i];
	}
	return NULL;
}

// Returns the current InfuseRank of the InfuseSlot
AUTO_TRANS_HELPER
ATR_LOCKS(pInfuseSlot, ".Irank, .Hdef, .Hitem");
InfuseRank* infuse_trh_GetRank(ATH_ARG NOCONST(InfuseSlot) *pInfuseSlot)
{
	InfuseSlotDef *pInfuseSlotDef = GET_REF(pInfuseSlot->hDef);
	ItemDef *pItemOption = GET_REF(pInfuseSlot->hItem);
	if(pInfuseSlotDef && pItemOption)
	{
		InfuseOption *pInfuseOption = infuse_GetDefOption(pInfuseSlotDef,pItemOption);
		if(pInfuseOption)
		{
			ANALYSIS_ASSUME(pInfuseOption->ppRanks); // sigh
			if(pInfuseSlot->iRank < eaSize(&pInfuseOption->ppRanks))
				return pInfuseOption->ppRanks[pInfuseSlot->iRank];
		}
	}
	return NULL;
}

// Returns the icon for the InfuseSlotDef, optionally overriding it with a specific one for the option
//  and rank of an actual InfuseSlot.
const char* infuse_GetIcon(InfuseSlotDef *pInfuseSlotDef, InfuseSlot *pInfuseSlot)
{
	if(pInfuseSlot)
	{
		InfuseRank *pInfuseRank = infuse_trh_GetRank(CONTAINER_NOCONST(InfuseSlot, pInfuseSlot));
		if(pInfuseRank && pInfuseRank->pchIcon)
			return pInfuseRank->pchIcon;
	}
	return pInfuseSlotDef->pchIcon;
}

void item_SetFXParamBlockString(NOCONST(PCFX)* pFXDst, const char* pAtBoneName, const char* pGeoBoneName, F32 fScale)
{
	//create; serialize param block
	char *parserStr = NULL;
	if (pFXDst)
	{
		//create; serialize param block
		DynParamBlock block = {0};
		DynDefineParam params[3] = {0};

		if (pFXDst->pcParams)
			StructFreeStringSafe(&pFXDst->pcParams);

		params[0].pcParamName = allocAddString("AtBoneName");
		params[1].pcParamName = allocAddString("GeoBoneName");
		params[2].pcParamName = allocAddString("ParticleScale");
		MultiValSetString(&params[0].mvVal, pAtBoneName);
		MultiValSetString(&params[1].mvVal, pGeoBoneName);
		MultiValSetFloat(&params[2].mvVal, fScale);
		eaPush(&block.eaDefineParams, &params[0]);
		eaPush(&block.eaDefineParams, &params[1]);
		eaPush(&block.eaDefineParams, &params[2]);
		ParserWriteText(&parserStr, parse_DynParamBlock, &block, 0, 0, 0);
		StructCopyString(&pFXDst->pcParams, parserStr);
		estrDestroy(&parserStr);
		eaDestroy(&block.eaDefineParams);
		MultiValClear(&params[0].mvVal);
		MultiValClear(&params[1].mvVal);
		MultiValClear(&params[2].mvVal);
	}
}

CostumeDisplayData * item_GetCostumeDisplayData( int iPartitionIdx, Entity *pEnt, Item *pItem, ItemDef *pItemDef, SlotType eEquippedSlot, Item* pFakeDye, int iChannel )
{
	CostumeDisplayData *pData;
	const char* pchBaseFXAtBone = NULL;
	const char* pchFXAtBone = NULL;
	int iCat, iCostume;
	NOCONST(PCFX) *pFXToAdd = NULL;
	ItemDef* pGemDefWithFX = NULL;
	F32 fFXScale = 1.0;
	bool bOverrideAtBone = false;
	bool bFXToAddNeedsParams = true;

	pData = calloc(1, sizeof(CostumeDisplayData));

	if (!pData) {
		return NULL;
	}

	if (pItemDef->iCostumePriority) {
		pData->iPriority = pItemDef->iCostumePriority;
	} else if ((pItemDef->eCostumeMode == kCostumeDisplayMode_Replace_Always) ||
		(pItemDef->eCostumeMode == kCostumeDisplayMode_Replace_Match)) {
			pData->iPriority = DEFAULT_ITEM_REPLACE_PRIORITY;
	} else {
		pData->iPriority = DEFAULT_ITEM_OVERLAY_PRIORITY;
	}
	pData->eMode = pItemDef->eCostumeMode;

	if (pItem && pItem->pAlgoProps && pItem->pAlgoProps->pDyeData)
	{
		int i, j;
		//this item has been dyed
		COPY_HANDLE(pData->hDyeMat, pItem->pAlgoProps->pDyeData->hMat);

		//copy colors
		for (i = 0; i < 4; i++)
		{
			for (j = 0; j < 3; j++)
			{
				pData->vDyeColors[i][j] = pItem->pAlgoProps->pDyeData->DyeColors[i*3 + j];
			}
		}
	}
	if (pFakeDye)
	{
		//put fake dye over top of actual colors
		ItemDef* pDyeDef = GET_REF(pFakeDye->hItem);
		COPY_HANDLE(pData->hDyeMat, pDyeDef->hDyeMat);

		if (pDyeDef->eType == kItemType_DyePack)
		{
			int j;
			for (j = 0; j < 3; j++)
			{
				pData->vDyeColors[0][j] = pDyeDef->vDyeColor0[j]*255 + 0.5;
				pData->vDyeColors[1][j] = pDyeDef->vDyeColor1[j]*255 + 0.5;
				pData->vDyeColors[2][j] = pDyeDef->vDyeColor2[j]*255 + 0.5;
				pData->vDyeColors[3][j] = pDyeDef->vDyeColor3[j]*255 + 0.5;
			}
		}
		else if (pDyeDef->eType == kItemType_DyeBottle)
		{
			int j;
			for (j = 0; j < 3; j++)
			{
				pData->vDyeColors[iChannel][j] = pDyeDef->vDyeColor0[j]*255 + 0.5;
			}

		}
	}

	//Handle transmuted items.
	if (pItem && pItem->pSpecialProps && pItem->pSpecialProps->pTransmutationProps)
	{
		ItemDef* pTransmuteDef = GET_REF(pItem->pSpecialProps->pTransmutationProps->hTransmutatedItemDef);
		if (pTransmuteDef)
			pItemDef = pTransmuteDef;
	}

	for (iCat = 0; iCat < eaiSize(&pItemDef->peCategories); iCat++)
	{
		ItemCategory eCat = pItemDef->peCategories[iCat];
		if (eCat >= kItemCategory_FIRST_DATA_DEFINED && g_ItemCategoryNames.ppInfo[eCat-kItemCategory_FIRST_DATA_DEFINED]->pchStanceWords)
			eaPush(&pData->eaStances, g_ItemCategoryNames.ppInfo[eCat-kItemCategory_FIRST_DATA_DEFINED]->pchStanceWords);
		if (eCat >= kItemCategory_FIRST_DATA_DEFINED && g_ItemCategoryNames.ppInfo[eCat-kItemCategory_FIRST_DATA_DEFINED]->fCostumeFXScaleValue > 0)
		{
			fFXScale *= g_ItemCategoryNames.ppInfo[eCat-kItemCategory_FIRST_DATA_DEFINED]->fCostumeFXScaleValue;
		}
	}

	if (pItem && pItem->pSpecialProps)
	{
		int i;
		for (i = 0; i < eaSize(&pItem->pSpecialProps->ppItemGemSlots); i++)
		{
			pGemDefWithFX = SAFE_GET_REF(pItem->pSpecialProps->ppItemGemSlots[i], hSlottedItem);
			if (pGemDefWithFX && pGemDefWithFX->pcGemAddedCostumeFX)
			{
				pFXToAdd = StructCreateNoConst(parse_PCFX);
				pFXToAdd->pcName = pGemDefWithFX->pcGemAddedCostumeFX;
				pchBaseFXAtBone = pGemDefWithFX->pcGemAddedCostumeBone;
			
				//for now, only one fx-on-a-gem is supported at once.
				break;
			}
		}
	}

	for(iCostume=eaSize(&pItemDef->ppCostumes)-1; iCostume>=0; iCostume--)
	{
		S32 bValid = true;

		// ASSUMPTION: If the entity is NULL, we can't run requires expressions, and we don't have to, because this is sort of an override mode.
		// It's being used so that we can temporarily put items onto the character in the NW character creator
		if (pEnt && !pEnt->bFakeEntity && pItemDef->ppCostumes[iCostume]->pExprRequires)
		{
			MultiVal answer = {0};
			itemeval_Eval(iPartitionIdx, pItemDef->ppCostumes[iCostume]->pExprRequires, pItemDef,  NULL, pItem, (Entity*)pEnt, pItemDef->iLevel, pItemDef->Quality, 0, pItemDef->pchFileName, -1, &answer);
			if(answer.type == MULTI_INT)
			{
				bValid = !!QuickGetInt(&answer);
			}
		}

		if(bValid)
		{
			PlayerCostume* pCostume = GET_REF(pItemDef->ppCostumes[iCostume]->hCostumeRef);
			SpeciesDef* pSpecies = pCostume ? GET_REF(pCostume->hSpecies) : NULL;
			CostumeCloneForItemCat* pCostumeClone = NULL;
			char* estrCloneName = NULL;
			estrStackCreate(&estrCloneName);
			if (pCostume && (!gConf.bCheckOverlayCostumeSpecies || !pSpecies || !pEnt || !pEnt->pChar || (pSpecies == GET_REF(pEnt->pChar->hSpecies))))
			{
				if (g_CostumeConfig.bEnableItemCategoryAddedBones)
				{
					int iBone;
					for (iCat = 0; iCat < eaiSize(&pItemDef->peCategories); iCat++)
					{
						if (pItemDef->peCategories[iCat] >= kItemCategory_FIRST_DATA_DEFINED)
						{
							ItemCategoryInfo* pCatInfo = g_ItemCategoryNames.ppInfo[pItemDef->peCategories[iCat] - kItemCategory_FIRST_DATA_DEFINED];
							if (pCatInfo)
							{
								if (eEquippedSlot != kSlotType_Secondary && eaSize(&pCatInfo->eaPrimarySlotAdditionalBones) > 0)
								{
									estrPrintf(&estrCloneName, "%s_CLONE_%s_Primary", pCostume->pcName, pCatInfo->pchName);
									pCostumeClone = RefSystem_ReferentFromString(g_hPlayerCostumeClonesForItemsDict, estrCloneName);
								}
								else if (eaSize(&pCatInfo->eaSecondarySlotAdditionalBones) > 0)
								{
									estrPrintf(&estrCloneName, "%s_CLONE_%s_Secondary", pCostume->pcName, pCatInfo->pchName);
									pCostumeClone = RefSystem_ReferentFromString(g_hPlayerCostumeClonesForItemsDict, estrCloneName);
								}

								if (pCatInfo->pchOverrideBaseCostumeFXAtBone)
								{
									pchBaseFXAtBone = pCatInfo->pchOverrideBaseCostumeFXAtBone;
									bOverrideAtBone = true;
								}

								if (pCostumeClone)
								{
									for (iBone = 0; iBone < eaSize(&pCostumeClone->eaBones); iBone++)
									{
										PCBoneDef* pOldBone = GET_REF(pCostumeClone->eaBones[iBone]->hOldBone);
										PCBoneDef* pNewBone = GET_REF(pCostumeClone->eaBones[iBone]->hNewBone);

										pchFXAtBone = NULL;
										if (pCatInfo->pchOverrideCostumeFXBone)
											pchFXAtBone = pCatInfo->pchOverrideCostumeFXBone;
										else if (pGemDefWithFX)
											pchFXAtBone = pGemDefWithFX->pcGemAddedCostumeBone;

										if (pCostumeClone->eaBones[iBone]->eType == kAdditionalCostumeBoneType_Move)
										{
											
											//may need to adjust FX too
											if (pFXToAdd && pGemDefWithFX && pGemDefWithFX->pcGemAddedCostumeFX && pchFXAtBone == pOldBone->pcBoneName)
											{
												bFXToAddNeedsParams = false;
												item_SetFXParamBlockString(pFXToAdd, pNewBone->pcBoneName, pNewBone->pcBoneName, fFXScale);
											}
										}
										else if (pCostumeClone->eaBones[iBone]->eType == kAdditionalCostumeBoneType_Clone)
										{
											if (pFXToAdd && pGemDefWithFX && pGemDefWithFX->pcGemAddedCostumeFX && pchFXAtBone == pOldBone->pcBoneName)
											{
												NOCONST(PCFX)* pCloneFX = StructCreateNoConst(parse_PCFX);
												pCloneFX->pcName = pGemDefWithFX->pcGemAddedCostumeFX;
												item_SetFXParamBlockString(pCloneFX, pNewBone->pcBoneName, pNewBone->pcBoneName, fFXScale);
												eaPush(&pData->eaAddedFX, CONTAINER_RECONST(PCFX, pCloneFX));
											}
										}
									}
									break;//only one supported at a time for now
								}
							}
						}
					}
				}
				if (pCostumeClone)
					eaPush(&pData->eaCostumes, pCostumeClone->pCostume);
				else if (pCostume)
					eaPush(&pData->eaCostumes, pCostume);
			}
			estrDestroy(&estrCloneName);
		}
	}

	if (pFXToAdd)
	{
		if (bOverrideAtBone || bFXToAddNeedsParams)
			item_SetFXParamBlockString(pFXToAdd, pchBaseFXAtBone, pGemDefWithFX->pcGemAddedCostumeBone, fFXScale);
		eaPush(&pData->eaAddedFX, CONTAINER_RECONST(PCFX, pFXToAdd));
	}

	if (pData->eaCostumes && eaSize(&pData->eaCostumes) == 0)
		eaDestroy(&pData->eaCostumes);

	if (!pData->eaCostumes)
		SAFE_FREE(pData);

	return pData;
}

// Returns true if the entity and the inventory exists and there are no items we are interested that's not completely downloaded from the server
bool item_GetItemCostumeDataToShow(int iPartitionIdx, const Entity *pEnt, CostumeDisplayData ***peaData, GameAccountDataExtract *pExtract)
{
	int iBag;
	bool bReturnVal = true;
	U8 iCurrentBagIndexToShow = 0;

	if (!pEnt || !pEnt->pInventoryV2)
		return false;

	if( pEnt->pSaved )
		iCurrentBagIndexToShow = pEnt->pSaved->iCostumeSetIndexToShow;

	for(iBag = eaSize(&pEnt->pInventoryV2->ppInventoryBags)-1; iBag>=0; iBag--)
	{
		BagIterator *iter;

		// Only show costumes from equip bags that don't have bHideCostumes
		if(!(invbag_flags(pEnt->pInventoryV2->ppInventoryBags[iBag]) & InvBagFlag_EquipBag))
			continue;

		if (pEnt->pInventoryV2->ppInventoryBags[iBag]->bHideCostumes)
			continue;

		// Only show costumes from bags for the currently chosen costume set
		if( invbag_costumesetindex(pEnt->pInventoryV2->ppInventoryBags[iBag]) != iCurrentBagIndexToShow && !(invbag_flags(pEnt->pInventoryV2->ppInventoryBags[iBag]) & InvBagFlag_ShowInAllCostumeSets))
			continue;

		if (!invBagIDs_BagIDCanAffectCostume(pEnt->pInventoryV2->ppInventoryBags[iBag]->BagID))
			continue;

		iter = invbag_IteratorFromEnt(pEnt,SAFE_MEMBER(pEnt->pInventoryV2->ppInventoryBags[iBag],BagID),pExtract);

		for (; !bagiterator_Stopped(iter); bagiterator_Next(iter))
		{
			Item *pItem = (Item*)bagiterator_GetItem(iter);

			// We no longer retrieve item def from the bag iterator.
			// If at one point we decide that item lites should have a visual component
			// we have to rethink this function.
			ItemDef *pItemDef = NULL; 

			// AB NOTE: need to remove this
			InventoryBag *bag = pEnt->pInventoryV2->ppInventoryBags[iBag];
			InventorySlot *slot = bag ? eaGet(&bag->ppIndexedInventorySlots,iter->i_cur) : NULL;
			bool bSlotHideCostume = (slot && slot->bHideCostumes);
			CostumeDisplayData *pData;

			if (pItem)
			{
				if (pItem->pSpecialProps && 
					pItem->pSpecialProps->pTransmutationProps &&
					IS_HANDLE_ACTIVE(pItem->pSpecialProps->pTransmutationProps->hTransmutatedItemDef))
				{
					pItemDef = GET_REF(pItem->pSpecialProps->pTransmutationProps->hTransmutatedItemDef);
				}
				else
				{
					pItemDef = GET_REF(pItem->hItem);
				}
			}

			if (pItemDef == NULL && bagiterator_ItemDefStillLoading(iter))
			{
				bReturnVal = false;
			}

			// Skip if hidden, if no definition, if the definition says not to show, or if has no costumes
			if (bSlotHideCostume 
				|| !pItemDef 
				|| pItemDef->eCostumeMode == kCostumeDisplayMode_Unlock 
				|| !eaSize(&pItemDef->ppCostumes))
			{
				continue;
			}

			if (peaData && (pData = item_GetCostumeDisplayData(iPartitionIdx, (Entity *)pEnt, pItem, pItemDef, iter->i_cur > 0 ? kSlotType_Secondary : kSlotType_Primary ,NULL, 0)))
			{
				eaPush(peaData, pData);
			}
		}
		bagiterator_Destroy(iter);
	}

	return bReturnVal;
}

AUTO_TRANS_HELPER
ATR_LOCKS(pEnt, "pInventoryV2.ppLiteBags[]");
int item_trh_GetLevelingNumeric(ATR_ARGS, ATH_ARG NOCONST(Entity) *pEnt)
{
	return inv_trh_GetNumericValue(ATR_PASS_ARGS, pEnt, gConf.pcLevelingNumericItem);
}

int item_GetLevelingNumeric(const Entity *pEnt)
{
	return inv_GetNumericItemValue(pEnt, gConf.pcLevelingNumericItem);
}

static int itempowerResValidateCB(enumResourceValidateType eType, const char *pDictName, const char *pResourceName, ItemPowerDef *pItemPower, U32 userID)
{
	switch (eType)
	{	
	xcase RESVALIDATE_FIX_FILENAME: // Called for filename check
	{
		char *pchPath = NULL;
		resFixPooledFilename(&pItemPower->pchFileName, GameBranch_GetDirectory(&pchPath,ITEMPOWERS_BASE_DIR), pItemPower->pchScope, pItemPower->pchName, ITEMPOWERS_EXTENSION);
		estrDestroy(&pchPath);
		return VALIDATE_HANDLED;
	}

	xcase RESVALIDATE_POST_TEXT_READING: // Called on all objects in dictionary after any load/reload of this dictionary
		itempower_Validate(pItemPower);
		return VALIDATE_HANDLED;

	xcase RESVALIDATE_CHECK_REFERENCES:
		if (!isProductionMode()) {
			itempower_ValidateReferences(pItemPower);
			return VALIDATE_HANDLED;
		}
	}

	return VALIDATE_NOT_HANDLED;
}


AUTO_RUN;
int itempower_Startup(void)
{
	// Set up reference dictionaries
	g_hItemPowerDict = RefSystem_RegisterSelfDefiningDictionary("ItemPowerDef",false, parse_ItemPowerDef, true, true, NULL);
	
	resDictManageValidation(g_hItemPowerDict, itempowerResValidateCB);
	resDictSetDisplayName(g_hItemPowerDict, "Item Power", "Item Powers", RESCATEGORY_DESIGN);
	
	if (IsServer())
	{
		resDictProvideMissingResources(g_hItemPowerDict);
		if (isDevelopmentMode() || isProductionEditMode()) {
			resDictMaintainInfoIndex(g_hItemPowerDict, ".displayNameMsg.Message", ".Scope", NULL, NULL, ".Icon");
		}
	} 
	else
	{
		resDictRequestMissingResources(g_hItemPowerDict, 8, false, resClientRequestSendReferentCommand);
	}
	
	return 1;
}

static bool itemGem_CanUseCurrencyToUnslot(const char *pchCurrency, Entity *pEnt, Item *pHolderItem, ItemDef *pGemDef)
{
	int i;
	ItemDef *pHolderDef = pHolderItem ? GET_REF(pHolderItem->hItem) : NULL;

	for(i=0;i<eaSize(&s_ItemGemConfig.ppUnslotCosts);i++)
	{
		if(stricmp(pchCurrency,s_ItemGemConfig.ppUnslotCosts[i]->pchCurrency) == 0)
		{
			if(s_ItemGemConfig.ppUnslotCosts[i]->pExprCanUse)
			{
				MultiVal mvResult = {0};
				itemeval_EvalGem(entGetPartitionIdx(pEnt),s_ItemGemConfig.ppUnslotCosts[i]->pExprCanUse,pHolderDef,NULL,pHolderItem,pGemDef,NULL,pEnt,item_GetLevel(pHolderItem), pHolderDef->Quality,0,NULL,-1, &mvResult);

				return itemeval_GetIntResult(&mvResult,pGemDef->pchFileName, s_ItemGemConfig.ppUnslotCosts[i]->pExprCanUse);
			}
			return true;
		}
	}

	return false;
}

//get the cost in pchCurrency to remove the iGemIdx-th gem from pHolderItem.
int itemGem_GetUnslotCostFromGemIdx(const char *pchCurrency, Entity *pEnt, Item *pHolderItem, int iGemIdx)
{
	ItemDef *pGemDef = NULL;

	if(!pHolderItem || !pHolderItem->pSpecialProps || iGemIdx < 0)
		return -1;

	if(iGemIdx >= eaSize(&pHolderItem->pSpecialProps->ppItemGemSlots))
		return -1;

	pGemDef = GET_REF(pHolderItem->pSpecialProps->ppItemGemSlots[iGemIdx]->hSlottedItem);

	return itemGem_GetUnslotCostFromGemDef(pchCurrency, pEnt, pHolderItem, pGemDef);
}

//get the cost in pchCurrency to remove a gem with def pGemDef from pHolderItem.  Exposed so the UI can predict the cost before you try it.
int itemGem_GetUnslotCostFromGemDef(const char *pchCurrency, Entity *pEnt, Item *pHolderItem, SA_PARAM_OP_VALID ItemDef *pGemDef)
{
	int i;
	Item *pGemItem = NULL;
	ItemDef *pHolderDef = pHolderItem ? GET_REF(pHolderItem->hItem) : NULL;

	if(!pGemDef)
		return -1;

	// Check to see if they can use the currency
	if(!itemGem_CanUseCurrencyToUnslot(pchCurrency,pEnt,pHolderItem,pGemDef))
	{
		return -1;
	}

	for(i=0;i<eaSize(&s_ItemGemConfig.ppUnslotCosts);i++)
	{
		if(stricmp(pchCurrency,s_ItemGemConfig.ppUnslotCosts[i]->pchCurrency) == 0)
		{

			if(scp_itemIsSCP(pHolderItem))
			{
				return scp_EvalGemRemoveCost(pEnt, pHolderItem, pGemDef, pchCurrency);
			}
			else if(s_ItemGemConfig.ppUnslotCosts[i]->pExprCost)
			{
				MultiVal mvResult = {0};
				itemeval_EvalGem(entGetPartitionIdx(pEnt),s_ItemGemConfig.ppUnslotCosts[i]->pExprCost,pHolderDef,NULL,pHolderItem,pGemDef,NULL,pEnt,item_GetLevel(pHolderItem), pHolderDef->Quality,0,NULL,-1, &mvResult);

				return itemeval_GetIntResult(&mvResult,pGemDef->pchFileName,s_ItemGemConfig.ppUnslotCosts[i]->pExprCost);
			}
			return 0;
		}
	}

	return -1;
}

ItemDef *item_GetSlottedGemItemDef(SA_PARAM_NN_VALID Item *pHolderItem, S32 iSlot)
{
	if (pHolderItem->pSpecialProps && iSlot < eaSize(&pHolderItem->pSpecialProps->ppItemGemSlots))
	{
		return GET_REF(pHolderItem->pSpecialProps->ppItemGemSlots[iSlot]->hSlottedItem);
	}
	return NULL;
}

static void itemGemTypes_PostLoad(void)
{
	int i;

	if(eaSize(&s_ItemGemConfig.ppchGemType) > 0)
	{
		g_pDefineItemGemTypes = DefineCreate();

		for(i=0;i<eaSize(&s_ItemGemConfig.ppchGemType);i++)
		{
			devassert(s_ItemGemConfig.ppchGemType[i]);

			if(StaticDefineIntGetInt(ItemGemTypeEnum, s_ItemGemConfig.ppchGemType[i])==-1)
			{
				DefineAddInt(g_pDefineItemGemTypes,s_ItemGemConfig.ppchGemType[i],1<<(i));
			}
			else
			{
				ErrorFilenamef("defs/config/ItemGemTypes.def", "Ignoring duplicate Item Gem Type: %s", s_ItemGemConfig.ppchGemType[i]);
			}
		}

		for(i=0;i<eaSize(&s_ItemGemConfig.ppUnslotCosts);i++)
		{
			if(s_ItemGemConfig.ppUnslotCosts[i]->pExprCanUse)
			{
				exprGenerate(s_ItemGemConfig.ppUnslotCosts[i]->pExprCanUse,g_pItemContext);
			}
			if(s_ItemGemConfig.ppUnslotCosts[i]->pExprCost)
			{
				exprGenerate(s_ItemGemConfig.ppUnslotCosts[i]->pExprCost,g_pItemContext);
			}
		}
	}
}

// Reload CombatConfig top level callback, not particularly safe/correct
static void itemGemTypes_Reload(const char *pchRelPath, int UNUSED_when)
{
	loadstart_printf("Reloading %s...","CombatConfig");

	fileWaitForExclusiveAccess(pchRelPath);
	errorLogFileIsBeingReloaded(pchRelPath);
	StructInit(parse_ItemGemConfig, &s_ItemGemConfig);
	ParserLoadFiles(NULL,"defs/config/ItemGemTypes.def", "ItemGemTypes.bin", PARSER_OPTIONALFLAG, parse_ItemGemConfig, &s_ItemGemConfig);
	itemGemTypes_PostLoad();

	loadend_printf(" done.");
}

void itemGemTypes_Load()
{

	loadstart_printf("Loading Item Gem Types... ");

	ParserLoadFiles(NULL, "defs/config/ItemGemTypes.def", "ItemGemTypes.bin", PARSER_OPTIONALFLAG, parse_ItemGemConfig, &s_ItemGemConfig);

	itemGemTypes_PostLoad();

	if(isDevelopmentMode())
	{
		FolderCacheSetCallback(FOLDER_CACHE_CALLBACK_UPDATE, "defs/config/ItemGemTypes.def", itemGemTypes_Reload);
	}
	loadend_printf("done.");
}

void itempowercategories_Load()
{
	int i,s, v;
	char *pchTemp = NULL;
	int codeCategories;

	estrCreate(&pchTemp);

	loadstart_printf("Loading ItemPowerCategories... ");

	ParserLoadFiles(NULL, "defs/config/ItemPowerCategories.def", "ItemPowerCategories.bin", PARSER_OPTIONALFLAG, parse_ItemPowerCategoryNames, &s_ItemPowerCategoryNames);
	g_pDefineItemPowerCategories = DefineCreate();
	s = eaSize(&s_ItemPowerCategoryNames.ppchNames);
	// Make sure we haven't gone past the current limitations of the system
	codeCategories = log2(kItemPowerCategory_CODEMAX);
	if(s>32-codeCategories)
	{
		ErrorFilenamef("defs/config/ItemPowerCategories.def","Too many Itempower categories defined for current system.  %d defined, 32 supported.",s);
		s=32-log2(kItemPowerCategory_CODEMAX);
	}
	v = 1 << codeCategories;
	for(i=0; i<s; i++)
	{
		devassert(s_ItemPowerCategoryNames.ppchNames[i]);

		if(StaticDefineIntGetInt(ItemPowerCategoryEnum, s_ItemPowerCategoryNames.ppchNames[i])==-1)
		{
			v <<= 1;
			estrPrintf(&pchTemp,"%d", v);
			DefineAdd(g_pDefineItemPowerCategories,s_ItemPowerCategoryNames.ppchNames[i],pchTemp);
		}
		else
		{
			ErrorFilenamef("defs/config/ItemPowerCategories.def", "Ignoring duplicate Itempower category: %s", s_ItemPowerCategoryNames.ppchNames[i]);
		}
	}

	loadend_printf(" done (%d ItemPowerCategories).", s);

	estrDestroy(&pchTemp);

	g_pDefineItemPowerArtCategories = DefineCreate();
	s = eaSize(&s_ItemPowerCategoryNames.ppchArtNames);
	for(i=0; i<s; i++)
	{
		devassert(s_ItemPowerCategoryNames.ppchArtNames[i]);

		if(StaticDefineIntGetInt(ItemPowerArtCategoryEnum, s_ItemPowerCategoryNames.ppchArtNames[i])==-1)
		{
			DefineAddInt(g_pDefineItemPowerArtCategories,s_ItemPowerCategoryNames.ppchArtNames[i],i + kItemPowerArtCategory_FIRST_DATA_DEFINED);
		}
		else
		{
			ErrorFilenamef("defs/ItemPowerArtCategories.def", "Ignoring duplicate ItempowerArt category: %s", s_ItemPowerCategoryNames.ppchArtNames[i]);
		}
	}
}

AUTO_STARTUP(ItemPowers) ASTRT_DEPS(Powers, ItemVars, PowerReplaces);
void itempower_Load(void)
{
	itempowercategories_Load();

	//Moved this here to resolve load-order issue.
	itemGemTypes_Load();

	if(!IsClient())
	{
		char *pcBinFile = NULL;
		char *pcPath = NULL;
		
		resLoadResourcesFromDisk(
				g_hItemPowerDict,
				GameBranch_GetDirectory(&pcPath, ITEMPOWERS_BASE_DIR),
				".itempower",
				GameBranch_GetFilename(&pcBinFile, ITEMPOWERS_BIN_FILE),
				PARSER_OPTIONALFLAG | RESOURCELOAD_SHAREDMEMORY);
		
		estrDestroy(&pcBinFile);
		estrDestroy(&pcPath);
	}
}


//////////////////////////////////////////////////////////////////////////
// Infuse Slots

static void InfluseSlotValidate(InfuseSlotDef *pInfuseSlotDef)
{
	int i,j,s,iRanks = 0;

	if(!resIsValidName(pInfuseSlotDef->pchName))
		ErrorFilenamef(pInfuseSlotDef->pchFileName, "InfuseSlotDef name is illegal: '%s'", pInfuseSlotDef->pchName);

	s = eaSize(&pInfuseSlotDef->ppOptions);
	if(!s)
		ErrorFilenamef(pInfuseSlotDef->pchFileName, "InfuseSlotDef has no Options");

	for(i=s-1; i>=0; i--)
	{
		InfuseOption *pInfuseOption = pInfuseSlotDef->ppOptions[i];
		if(iRanks && eaSize(&pInfuseOption->ppRanks)!=iRanks)
		{
			ErrorFilenamef(pInfuseSlotDef->pchFileName, "InfuseSlotDef Options have varying number of Ranks");
		}
		else
		{
			if(!iRanks)
				iRanks = eaSize(&pInfuseOption->ppRanks);

			if(!iRanks)
				ErrorFilenamef(pInfuseSlotDef->pchFileName, "InfuseSlotDef Option %d has no Ranks",i);

			for(j=iRanks-1; j>=0; j--)
			{
				if(!eaSize(&pInfuseOption->ppRanks[j]->ppDefRefs))
					ErrorFilenamef(pInfuseSlotDef->pchFileName, "InfuseSlotDef Option %d Rank %d has no ItemPowerDefRefs",i,j);

				if(pInfuseOption->ppRanks[j]->iCost <= 0)
					ErrorFilenamef(pInfuseSlotDef->pchFileName, "InfuseSlotDef Option %d Rank %d has non-positive cost",i,j);
			}
		}
	}
}

static void InfuseSlotValidateReferences(InfuseSlotDef *pInfuseSlotDef)
{
	int i,j,k;

	// TODO(JW): Validate messages exist

	ANALYSIS_ASSUME(pInfuseSlotDef->ppOptions); // sigh
	for(i=eaSize(&pInfuseSlotDef->ppOptions)-1; i>=0; i--)
	{
		InfuseOption *pInfuseOption = pInfuseSlotDef->ppOptions[i];
		ItemDef *pItemOption = GET_REF(pInfuseOption->hItem);

		if(!pItemOption || pItemOption->eType!=kItemType_Numeric)
			ErrorFilenamef(pInfuseSlotDef->pchFileName, "InfuseSlotDef Option %d has invalid Item",i);

		for(j=eaSize(&pInfuseOption->ppRanks)-1; j>=0; j--)
		{
			InfuseRank *pInfuseRank = pInfuseOption->ppRanks[j];
			for(k=eaSize(&pInfuseRank->ppDefRefs)-1; k>=0; k--)
			{
				if(!GET_REF(pInfuseRank->ppDefRefs[k]->hItemPowerDef))
					ErrorFilenamef(pInfuseSlotDef->pchFileName, "InfuseSlotDef Option %d Rank %d ItemPowerDefRef %d has invalid ItemPowerDef",i,j,k);
			}
		}
	}

}

static int InfuseSlotResValidateCB(enumResourceValidateType eType, const char *pDictName, const char *pResourceName, InfuseSlotDef *pInfuseSlotDef, U32 userID)
{
	switch (eType)
	{	
		xcase RESVALIDATE_FIX_FILENAME:
		{
			char *pchPath = NULL;
			resFixPooledFilename(&pInfuseSlotDef->pchFileName, GameBranch_GetDirectory(&pchPath,INFUSESLOT_BASE_DIR), NULL, pInfuseSlotDef->pchName, INFUSESLOT_EXTENSION);
			estrDestroy(&pchPath);
			return VALIDATE_HANDLED;
		}

		xcase RESVALIDATE_POST_TEXT_READING:
			InfluseSlotValidate(pInfuseSlotDef);
			return VALIDATE_HANDLED;

		xcase RESVALIDATE_CHECK_REFERENCES:
			if (!isProductionMode()) {
				InfuseSlotValidateReferences(pInfuseSlotDef);
				return VALIDATE_HANDLED;
			}
	}
	return VALIDATE_NOT_HANDLED;
}

AUTO_RUN;
int InfuseSlotDefAutoRun(void)
{
	g_hInfuseSlotDict = RefSystem_RegisterSelfDefiningDictionary("InfuseSlotDef", false, parse_InfuseSlotDef, true, true, NULL);

	resDictManageValidation(g_hInfuseSlotDict, InfuseSlotResValidateCB);

	if(IsServer())
	{
		resDictProvideMissingResources(g_hInfuseSlotDict);
		if (isDevelopmentMode() || isProductionEditMode()) {
			resDictMaintainInfoIndex(g_hInfuseSlotDict, ".msgDisplayName.Message", NULL, NULL, NULL, ".Icon");
		}
	} 
	else
	{
		resDictRequestMissingResources(g_hInfuseSlotDict, 8, false, resClientRequestSendReferentCommand);
	}

	return 1;
}

AUTO_STARTUP(InfuseSlots) ASTRT_DEPS(ItemPowers);
void InfuseSlotLoad(void)
{
	if(!IsClient())
	{
		resLoadResourcesFromDisk(
			g_hInfuseSlotDict,
			INFUSESLOT_BASE_DIR,
			"."INFUSESLOT_EXTENSION,
			INFUSESLOT_BASE_DIR,
			PARSER_OPTIONALFLAG | RESOURCELOAD_SHAREDMEMORY);
	}
}

// DO NOT CALL THIS IN A TRANSACTION!!!
// It does expression evaluation, which isn't transaction safe
S32 itempower_GetEPValue(int iPartitionIdx, Entity *pEnt, ItemPowerDef *pDef, ItemDef *pItemDef, S32 iLevel, ItemQuality eQuality)
{
	MultiVal mvExprResult = {0};
	itemeval_Eval(iPartitionIdx, pDef->pExprEconomyPoints, pItemDef, pDef, NULL, pEnt, iLevel, eQuality, 0, pDef->pchFileName, -1, &mvExprResult);
	return itemeval_GetIntResult(&mvExprResult, pDef->pchFileName, pDef->pExprEconomyPoints);
}

ItemSortTypes gSortTypes;
ItemAliasLookup gAliasLookup;

static void item_UpdateItemSetFlag(ItemDef *pDef)
{
	// Note that we've got ItemSets
	if(eaSize(&pDef->ppItemSets))
	{
		g_bItemSets = true;
	}
}

static CostumeCloneForItemCat* item_CreateCostumeCloneInternal(PlayerCostume* pCostume, ItemCategoryInfo* pCatInfo, ItemCategoryAdditionalCostumeBone** eaAddBones)
{
	NOCONST(PlayerCostume)* pCostumeClone = NULL;
	CostumeCloneForItemCat* pCloneStruct = StructCreate(parse_CostumeCloneForItemCat);
	int iCatBone, iPart;
	for (iCatBone = 0; iCatBone < eaSize(&eaAddBones); iCatBone++)
	{
		for (iPart = 0; iPart < eaSize(&pCostume->eaParts); iPart++)
		{
			if (REF_COMPARE_HANDLES(pCostume->eaParts[iPart]->hBoneDef, eaAddBones[iCatBone]->hOldBone))
			{
				PCBoneDef* pOldBone = GET_REF(pCostume->eaParts[iPart]->hBoneDef);
				//found a match
				if (!pCostumeClone)
					pCostumeClone = StructCloneDeConst(parse_PlayerCostume, pCostume);

				if (eaAddBones[iCatBone]->eType == kAdditionalCostumeBoneType_Move)
				{
					COPY_HANDLE(pCostumeClone->eaParts[iPart]->hBoneDef, eaAddBones[iCatBone]->hNewBone);
					pCostumeClone->eaParts[iPart]->pchOrigBone = allocAddString(pOldBone->pcBoneName);
				}
				else if (eaAddBones[iCatBone]->eType == kAdditionalCostumeBoneType_Clone)
				{
					NOCONST(PCPart)* pPartClone = StructCloneNoConst(parse_PCPart, pCostumeClone->eaParts[iPart]);
					pPartClone->pchOrigBone = allocAddString(pOldBone->pcBoneName);
					COPY_HANDLE(pPartClone->hBoneDef, eaAddBones[iCatBone]->hNewBone);
					eaPush(&pCostumeClone->eaParts, pPartClone);
				}
				eaPush(&pCloneStruct->eaBones, StructClone(parse_ItemCategoryAdditionalCostumeBone, eaAddBones[iCatBone]));
			}
		}
	}
	if (pCostumeClone)
	{
		pCostumeClone->pcFileName = NULL;
		pCloneStruct->pCostume = CONTAINER_RECONST(PlayerCostume, pCostumeClone);
	}
	else
		StructDestroySafeVoid(parse_CostumeCloneForItemCat, &pCloneStruct);
	return pCloneStruct;
}

AUTO_STARTUP(ItemCostumeClones) ASTRT_DEPS(Items, EntityCostumes);
void item_CreateItemCategoryCostumeClones(void)
{
	if (g_CostumeConfig.bEnableItemCategoryAddedBones)
	{
		RefDictIterator iter;
		ItemDef* pDef = NULL;
		char* estrCloneName = NULL;
		int iCostume, iCat;
		estrStackCreate(&estrCloneName);

		loadstart_printf("Creating item costume clones for weapons...");
		resEditStartDictionaryModification(g_hPlayerCostumeClonesForItemsDict);

		RefSystem_InitRefDictIterator(g_hItemDict, &iter);

		while(pDef = (ItemDef*)RefSystem_GetNextReferentFromIterator(&iter))
		{
			estrClear(&estrCloneName);
			for (iCat = 0; iCat < eaiSize(&pDef->peCategories); iCat++)
			{
				if (pDef->peCategories[iCat] >= kItemCategory_FIRST_DATA_DEFINED)
				{
					ItemCategoryInfo* pCatInfo = g_ItemCategoryNames.ppInfo[pDef->peCategories[iCat] - kItemCategory_FIRST_DATA_DEFINED];
					if (pCatInfo && ((eaSize(&pCatInfo->eaPrimarySlotAdditionalBones) > 0) || (eaSize(&pCatInfo->eaSecondarySlotAdditionalBones) > 0)))
					{
						for(iCostume = 0; iCostume < eaSize(&pDef->ppCostumes); iCostume++)
						{
							PlayerCostume* pCostume = GET_REF(pDef->ppCostumes[iCostume]->hCostumeRef);
							CostumeCloneForItemCat* pClone = NULL;

							if (!pCostume)
								continue;

							if (eaSize(&pCatInfo->eaPrimarySlotAdditionalBones) > 0)
							{
								estrPrintf(&estrCloneName, "%s_CLONE_%s_Primary", pCostume->pcName, pCatInfo->pchName);
								if (!RefSystem_ReferentFromString(g_hPlayerCostumeClonesForItemsDict, estrCloneName))
								{
									pClone = item_CreateCostumeCloneInternal(pCostume, pCatInfo, pCatInfo->eaPrimarySlotAdditionalBones);
									if (!pClone)
									{
										ErrorFilenamef(pDef->pchFileName, "Item \"%s\" has category \"%s\" that is trying to move or clone costumebones, but costume \"%s\" doesn't have parts set on any matching bones.", pDef->pchName, pCatInfo->pchName, pCostume->pcName);
									}
									else
									{
										CONTAINER_NOCONST(PlayerCostume, pClone->pCostume)->pcName = allocAddString(estrCloneName);
										pClone->pchName = allocAddString(estrCloneName);
										resEditSetWorkingCopy(resGetDictionary(g_hPlayerCostumeClonesForItemsDict), estrCloneName, pClone);
									}
								}
							}
							if (eaSize(&pCatInfo->eaSecondarySlotAdditionalBones) > 0)
							{
								estrPrintf(&estrCloneName, "%s_CLONE_%s_Secondary", pCostume->pcName, pCatInfo->pchName);
								if (!RefSystem_ReferentFromString(g_hPlayerCostumeClonesForItemsDict, estrCloneName))
								{
									pClone = item_CreateCostumeCloneInternal(pCostume, pCatInfo, pCatInfo->eaSecondarySlotAdditionalBones);
									if (!pClone)
									{
										ErrorFilenamef(pDef->pchFileName, "Item \"%s\" has category \"%s\" that is trying to move or clone costumebones, but costume \"%s\" doesn't have parts set on any matching bones.", pDef->pchName, pCatInfo->pchName, pCostume->pcName);
									}
									else
									{
										CONTAINER_NOCONST(PlayerCostume, pClone->pCostume)->pcName = allocAddString(estrCloneName);
										pClone->pchName = allocAddString(estrCloneName);
										resEditSetWorkingCopy(resGetDictionary(g_hPlayerCostumeClonesForItemsDict), estrCloneName, pClone);
									}
								}
							}
						}
					}
				}
			}
		}
		estrDestroy(&estrCloneName);

		resEditCommitAllModifications(g_hPlayerCostumeClonesForItemsDict, true);

		loadend_printf("done.");
	}
}

static int itemResValidateCB(enumResourceValidateType eType, const char *pDictName, const char *pResourceName, ItemDef *pItem, U32 userID)
{
	switch (eType)
	{	
	xcase RESVALIDATE_FIX_FILENAME: // Called for filename check
	{
		char *pchPath = NULL;
		if(pItem->pchScope && resIsInDirectory(pItem->pchScope,"itemgen/"))
		{
			//do nothing, item gen puts the correct filename on it.
		}
		else
		{
			resFixPooledFilename(&pItem->pchFileName, pItem->pchScope && resIsInDirectory(pItem->pchScope, "maps/") ? NULL : GameBranch_GetDirectory(&pchPath, ITEMS_BASE_DIR), pItem->pchScope, pItem->pchName, ITEMS_EXTENSION);
		}
		
		estrDestroy(&pchPath);
		return VALIDATE_HANDLED;
	}

	xcase RESVALIDATE_POST_TEXT_READING: // Called after load/reload but before binning
		resAddFileDep(ITEM_SORT_TYPE_FILE);
		resAddParseTableDep(parse_ItemSortType);
		item_ValidateAllButRefs(pItem);
		return VALIDATE_HANDLED;

	xcase RESVALIDATE_POST_BINNING: // Called after binning or text load/reload
		return VALIDATE_HANDLED;

	xcase RESVALIDATE_CHECK_REFERENCES: // Called when all data has been loaded
		if (IsServer() && isProductionMode())
		{
			if (!gConf.bUserContent)
				RefSystem_LockDictionaryReferents(pDictName);
		}
		else
			item_ValidateRefs(pItem);

		// Special handling of the g_bItemSets flag because item_ValidateRefs isn't called in prod mode
		item_UpdateItemSetFlag(pItem);
		return VALIDATE_HANDLED;
	}

	return VALIDATE_NOT_HANDLED;
}

AUTO_RUN;
int item_Startup(void)
{
	// Set up reference dictionaries
	g_hItemDict = RefSystem_RegisterSelfDefiningDictionary("ItemDef",false, parse_ItemDef, true, true, NULL);

	// Pre-generated clones of costumes created for the Item system
	g_hPlayerCostumeClonesForItemsDict = RefSystem_RegisterSelfDefiningDictionary("CostumeCloneForItemCat",false, parse_CostumeCloneForItemCat, false, true, NULL);

	resDictManageValidation(g_hItemDict, itemResValidateCB);
	resDictSetDisplayName(g_hItemDict, "Item", "Items", RESCATEGORY_DESIGN);
	
	if (IsServer())
	{
		resDictProvideMissingResources(g_hItemDict);
		resDictGetMissingResourceFromResourceDBIfPossible((void*)g_hItemDict);
		
		resDictProvideMissingResources(g_hPlayerCostumeClonesForItemsDict);
		if (isDevelopmentMode() || isProductionEditMode()) {
			resDictMaintainInfoIndex(g_hItemDict, ".displayNameMsg.Message", ".Scope", NULL, ".Notes", ".Icon");
		}
	} 
	else
	{
		resDictRequestMissingResources(g_hPlayerCostumeClonesForItemsDict, 30, false, resClientRequestSendReferentCommand);
		resDictRequestMissingResources(g_hItemDict, 8, false, resClientRequestSendReferentCommand);
	}
	
	return 1;
}

AUTO_FIXUPFUNC;
TextParserResult fixupSortTypes(ItemSortTypes* pSortTypes, enumTextParserFixupType eType, void *pExtraData)
{
	int i, j, n;
	switch (eType)
	{
	case FIXUPTYPE_POST_TEXT_READ:
		n = eaSize(&pSortTypes->ppItemSortType);
		for (i = 0; i < n; i++)
		{
			ItemSortType *pSortType = pSortTypes->ppItemSortType[i];
			int count;
			U32 sortID;

			// scan the array, counting up entries that have the same sortID as this entry, so we can catch entries with duplicate sortIDs
			sortID = pSortType->iSortID;
			count = 0;
			for ( j = 0; j < n; j++ )
			{
				if ( pSortTypes->ppItemSortType[j]->iSortID == sortID )
				{
					count++;
				}
			}
			if (count > 1)
			{
				ErrorFilenamef(pSortTypes->pchFileName, "Duplicate sort type defined");
				continue;
			}
			if (!GET_REF(pSortType->hNameMsg))
			{
				ErrorFilenamef(pSortTypes->pchFileName, "Invalid Message key %s for sort type", REF_STRING_FROM_HANDLE(pSortType->hNameMsg));
				continue;
			}
			if (sortID == 0)
			{
				if (pSortType->eType != kItemType_None)
				{
					ErrorFilenamef(pSortTypes->pchFileName, "Entry with sortID of 0 must be of type None");
				}
			}
			else
			{
				if (pSortType->eType == kItemType_None)
				{
					ErrorFilenamef(pSortTypes->pchFileName, "Only the entry with sortID 0 can be of type None");
				}
			}
		}		
	}

	return 1;
}

void item_FixMessages(ItemDef *pItemDef)
{
#ifndef NO_EDITORS
	char itemNameSansNamespace[RESOURCE_NAME_MAX_SIZE];
	char namespace[ RESOURCE_NAME_MAX_SIZE ];
	char *tmpS = NULL;
	char *tmpKeyPrefix = NULL;
	char *pchScope = NULL;

	estrStackCreate(&tmpS);
	estrStackCreate(&tmpKeyPrefix);
	estrStackCreate(&pchScope);

	estrPrintf(&pchScope,"ItemDef");

	if( resExtractNameSpace( pItemDef->pchName, namespace, itemNameSansNamespace )) {
		estrPrintf(&tmpKeyPrefix, "%s:ItemDef.", namespace );
	} else {
		estrPrintf(&tmpKeyPrefix, "ItemDef." );
	}
	
	if(g_pcGameBranch)
	{
		estrConcat(&tmpKeyPrefix, g_pcGameBranch, (int)strlen(g_pcGameBranch));
		estrConcatChar(&tmpKeyPrefix, '.');
	}

	// Fix up name
	{
		if (!pItemDef->displayNameMsg.pEditorCopy) {
			pItemDef->displayNameMsg.pEditorCopy = StructCreate(parse_Message);
		}

		// Set up key if not exactly
		estrPrintf(&tmpS, "%s%s", tmpKeyPrefix, itemNameSansNamespace);
		if (!pItemDef->displayNameMsg.pEditorCopy->pcMessageKey || 
			(stricmp(tmpS, pItemDef->displayNameMsg.pEditorCopy->pcMessageKey) != 0)) {
				langFixupMessageWithTerseKey(pItemDef->displayNameMsg.pEditorCopy,MKP_ITEMNAME, tmpS, "Item Name", pchScope);
		}
	}

	// Fix up description
	{
		if (!pItemDef->descriptionMsg.pEditorCopy) {
			pItemDef->descriptionMsg.pEditorCopy = StructCreate(parse_Message);
		}

		// Set up key if not exactly
		estrPrintf(&tmpS, "%s%s.Description", tmpKeyPrefix, itemNameSansNamespace);
		if (!pItemDef->descriptionMsg.pEditorCopy->pcMessageKey || 
			(stricmp(tmpS, pItemDef->descriptionMsg.pEditorCopy->pcMessageKey) != 0)) {
				langFixupMessageWithTerseKey(pItemDef->descriptionMsg.pEditorCopy, MKP_ITEMDESC, tmpS, "Item Description", pchScope);
		}
	}

	// Fix up short description
	{
		if (!pItemDef->descShortMsg.pEditorCopy) {
			pItemDef->descShortMsg.pEditorCopy = StructCreate(parse_Message);
		}

		// Set up key if not exactly
		estrPrintf(&tmpS, "%s%s.ShortDescription", tmpKeyPrefix, itemNameSansNamespace);
		if (!pItemDef->descShortMsg.pEditorCopy->pcMessageKey || 
			(stricmp(tmpS, pItemDef->descShortMsg.pEditorCopy->pcMessageKey) != 0)) {
				langFixupMessageWithTerseKey(pItemDef->descShortMsg.pEditorCopy, MKP_ITEMDESCSHORT, tmpS, "Item Short Description", pchScope);
		}
	}

	// Fix up unidentified name
	{
		if (!pItemDef->displayNameMsgUnidentified.pEditorCopy) {
			pItemDef->displayNameMsgUnidentified.pEditorCopy = StructCreate(parse_Message);
		}

		// Set up key if not exactly
		estrPrintf(&tmpS, "%s%s.Unidentified", tmpKeyPrefix, itemNameSansNamespace);
		if (!pItemDef->displayNameMsgUnidentified.pEditorCopy->pcMessageKey || 
			(stricmp(tmpS, pItemDef->displayNameMsgUnidentified.pEditorCopy->pcMessageKey) != 0)) {
				langFixupMessageWithTerseKey(pItemDef->displayNameMsgUnidentified.pEditorCopy, MKP_ITEMNAMEUNIDENTIFIED, tmpS, "Item Name while unidentified", pchScope);
		}
	}

	// Fix up unidentified description
	{
		if (!pItemDef->descriptionMsgUnidentified.pEditorCopy) {
			pItemDef->descriptionMsgUnidentified.pEditorCopy = StructCreate(parse_Message);
		}

		// Set up key if not exactly
		estrPrintf(&tmpS, "%s%s.UnidentifiedDescription", tmpKeyPrefix, itemNameSansNamespace);
		if (!pItemDef->descriptionMsgUnidentified.pEditorCopy->pcMessageKey || 
			(stricmp(tmpS, pItemDef->descriptionMsgUnidentified.pEditorCopy->pcMessageKey) != 0)) {
				langFixupMessageWithTerseKey(pItemDef->descriptionMsgUnidentified.pEditorCopy, MKP_ITEMDESCUNIDENTIFIED, tmpS, "Item Description while Unidentified", pchScope);
		}
	}
	// Fix up callout
	{
		if (!pItemDef->calloutMsg.pEditorCopy) {
			pItemDef->calloutMsg.pEditorCopy = StructCreate(parse_Message);
		}

		// Set up key if not exactly
		estrPrintf(&tmpS, "%s%s.Callout", tmpKeyPrefix, itemNameSansNamespace);
		if (!pItemDef->calloutMsg.pEditorCopy->pcMessageKey || 
			(strcmp(tmpS, pItemDef->calloutMsg.pEditorCopy->pcMessageKey) != 0)) {
				langFixupMessageWithTerseKey(pItemDef->calloutMsg.pEditorCopy, MKP_ITEMCALLOUT, tmpS, "Item call out", pchScope);
		}
	}
	// Fix up AutoDesc
	{
		if (!pItemDef->msgAutoDesc.pEditorCopy) {
			pItemDef->msgAutoDesc.pEditorCopy = StructCreate(parse_Message);
		}

		// Set up key if not exactly
		estrPrintf(&tmpS, "%s%s.AutoDesc", tmpKeyPrefix, itemNameSansNamespace);
		if (!pItemDef->msgAutoDesc.pEditorCopy->pcMessageKey || 
			(strcmp(tmpS, pItemDef->msgAutoDesc.pEditorCopy->pcMessageKey) != 0)) {
				langFixupMessageWithTerseKey(pItemDef->msgAutoDesc.pEditorCopy, MKP_ITEMAUTODESC, tmpS, "Item Auto Description", pchScope);
		}
	}

	if (pItemDef->pRewardPackInfo)
	{
		// Success message
		if (!pItemDef->pRewardPackInfo->msgUnpackMessage.pEditorCopy) {
			pItemDef->pRewardPackInfo->msgUnpackMessage.pEditorCopy = StructCreate(parse_Message);
		}

		// Set up key
		estrPrintf(&tmpS, "%s%s.UnpackMessage", tmpKeyPrefix, itemNameSansNamespace);
		if (!pItemDef->pRewardPackInfo->msgUnpackMessage.pEditorCopy->pcMessageKey || 
			(strcmp(tmpS, pItemDef->pRewardPackInfo->msgUnpackMessage.pEditorCopy->pcMessageKey) != 0)) {
				langFixupMessageWithTerseKey(pItemDef->pRewardPackInfo->msgUnpackMessage.pEditorCopy, MKP_UNPACK, tmpS, "Item Unpack", pchScope);
		}

		//Failure message
		if (!pItemDef->pRewardPackInfo->msgUnpackFailedMessage.pEditorCopy) {
			pItemDef->pRewardPackInfo->msgUnpackFailedMessage.pEditorCopy = StructCreate(parse_Message);
		}

		// Set up key
		estrPrintf(&tmpS, "%s%s.UnpackFailedMessage", tmpKeyPrefix, itemNameSansNamespace);
		if (!pItemDef->pRewardPackInfo->msgUnpackFailedMessage.pEditorCopy->pcMessageKey || 
			(strcmp(tmpS, pItemDef->pRewardPackInfo->msgUnpackFailedMessage.pEditorCopy->pcMessageKey) != 0)) {
				langFixupMessageWithTerseKey(pItemDef->pRewardPackInfo->msgUnpackFailedMessage.pEditorCopy, MKP_UNPACKFAIL, tmpS, "Item Unpack Fail", pchScope);
		}
	}

	estrDestroy(&tmpS);
	estrDestroy(&tmpKeyPrefix);
	estrDestroy(&pchScope);

#endif
}

void itempower_FixMessages(ItemPowerDef* pItemPowerDef)
{
	char *tmpS = NULL;
	char *tmpKeyPrefix = NULL;

	estrStackCreate(&tmpS);
	estrStackCreate(&tmpKeyPrefix);

	estrCopy2(&tmpKeyPrefix, "ItemPowerDef.");
	if(g_pcGameBranch)
	{
		estrConcat(&tmpKeyPrefix, g_pcGameBranch, (int)strlen(g_pcGameBranch));
		estrConcatChar(&tmpKeyPrefix, '.');
	}

	// Fix up name
	{
		if (!pItemPowerDef->displayNameMsg.pEditorCopy) {
			pItemPowerDef->displayNameMsg.pEditorCopy = StructCreate(parse_Message);
		}

		// Set up key if not exactly
		estrPrintf(&tmpS, "%s%s.DisplayName", tmpKeyPrefix, pItemPowerDef->pchName);
		if (!pItemPowerDef->displayNameMsg.pEditorCopy->pcMessageKey || 
			(stricmp(tmpS, pItemPowerDef->displayNameMsg.pEditorCopy->pcMessageKey) != 0)) {
			pItemPowerDef->displayNameMsg.pEditorCopy->pcMessageKey = allocAddString(tmpS);
		}

		estrPrintf(&tmpS, "Display name for an item definition (noun)");
		if (!pItemPowerDef->displayNameMsg.pEditorCopy->pcDescription ||
			(stricmp(tmpS, pItemPowerDef->displayNameMsg.pEditorCopy->pcDescription) != 0)) {
			StructFreeString(pItemPowerDef->displayNameMsg.pEditorCopy->pcDescription);
			pItemPowerDef->displayNameMsg.pEditorCopy->pcDescription = StructAllocString(tmpS);
		}

		estrPrintf(&tmpS, "ItemPowerDef");
		if (!pItemPowerDef->displayNameMsg.pEditorCopy->pcScope ||
			(stricmp(tmpS, pItemPowerDef->displayNameMsg.pEditorCopy->pcScope) != 0)) {
			pItemPowerDef->displayNameMsg.pEditorCopy->pcScope = allocAddString(tmpS);
		}
	}

	// Fix up name2
	{
		if (!pItemPowerDef->displayNameMsg2.pEditorCopy) {
			pItemPowerDef->displayNameMsg2.pEditorCopy = StructCreate(parse_Message);
		}

		// Set up key if not exactly
		estrPrintf(&tmpS, "%s%s.DisplayName2", tmpKeyPrefix, pItemPowerDef->pchName);
		if (!pItemPowerDef->displayNameMsg2.pEditorCopy->pcMessageKey || 
			(stricmp(tmpS, pItemPowerDef->displayNameMsg2.pEditorCopy->pcMessageKey) != 0)) {
				pItemPowerDef->displayNameMsg2.pEditorCopy->pcMessageKey = allocAddString(tmpS);
		}

		estrPrintf(&tmpS, "Display name for an item definition (adj)");
		if (!pItemPowerDef->displayNameMsg2.pEditorCopy->pcDescription ||
			(stricmp(tmpS, pItemPowerDef->displayNameMsg2.pEditorCopy->pcDescription) != 0)) {
				StructFreeString(pItemPowerDef->displayNameMsg2.pEditorCopy->pcDescription);
				pItemPowerDef->displayNameMsg2.pEditorCopy->pcDescription = StructAllocString(tmpS);
		}

		estrPrintf(&tmpS, "ItemPowerDef");
		if (!pItemPowerDef->displayNameMsg2.pEditorCopy->pcScope ||
			(stricmp(tmpS, pItemPowerDef->displayNameMsg2.pEditorCopy->pcScope) != 0)) {
				pItemPowerDef->displayNameMsg2.pEditorCopy->pcScope = allocAddString(tmpS);
		}
	}

	// Fix up description
	{
		if (!pItemPowerDef->descriptionMsg.pEditorCopy) {
			pItemPowerDef->descriptionMsg.pEditorCopy = StructCreate(parse_Message);
		}

		// Set up key if not exactly
		estrPrintf(&tmpS, "%s%s.Description", tmpKeyPrefix, pItemPowerDef->pchName);
		if (!pItemPowerDef->descriptionMsg.pEditorCopy->pcMessageKey || 
			(stricmp(tmpS, pItemPowerDef->descriptionMsg.pEditorCopy->pcMessageKey) != 0)) {
				pItemPowerDef->descriptionMsg.pEditorCopy->pcMessageKey = allocAddString(tmpS);
		}

		estrPrintf(&tmpS, "description for an item using this itempower");
		if (!pItemPowerDef->descriptionMsg.pEditorCopy->pcDescription ||
			(stricmp(tmpS, pItemPowerDef->descriptionMsg.pEditorCopy->pcDescription) != 0)) {
				StructFreeString(pItemPowerDef->descriptionMsg.pEditorCopy->pcDescription);
				pItemPowerDef->descriptionMsg.pEditorCopy->pcDescription = StructAllocString(tmpS);
		}

		estrPrintf(&tmpS, "ItemPowerDef");
		if (!pItemPowerDef->descriptionMsg.pEditorCopy->pcScope ||
			(stricmp(tmpS, pItemPowerDef->descriptionMsg.pEditorCopy->pcScope) != 0)) {
				pItemPowerDef->descriptionMsg.pEditorCopy->pcScope = allocAddString(tmpS);
		}
	}

	estrDestroy(&tmpS);
	estrDestroy(&tmpKeyPrefix);
}

ItemAlias *itemDef_GetAlias(const ItemDef *pItemDef)
{
	int i;

	for(i=0;i<eaSize(&gAliasLookup.ppAlias);i++)
	{
		if(GET_REF(gAliasLookup.ppAlias[i]->hItem) == pItemDef)
		{
			return gAliasLookup.ppAlias[i];
		}
	}

	return NULL;
}

AUTO_TRANS_HELPER
ATR_LOCKS(pEntity, ".Hallegiance, .Hsuballegiance");
bool itemAlias_Eval(ItemAliasDisplay *pAliasChoice,ATH_ARG NOCONST(Entity) *pEntity)
{
	if(IS_HANDLE_ACTIVE(pAliasChoice->hRequiredAllegiance) && GET_REF(pEntity->hAllegiance) != GET_REF(pAliasChoice->hRequiredAllegiance) &&
		GET_REF(pEntity->hSubAllegiance) != GET_REF(pAliasChoice->hRequiredAllegiance))
	{
		return false;
	}

	return true;
}

AUTO_TRANS_HELPER
ATR_LOCKS(pEntity, ".Hallegiance, .Hsuballegiance");
DisplayMessage entity_GetItemDisplayNameMessage(ATH_ARG NOCONST(Entity) *pEntity,const ItemDef *pItemDef)
{
	ItemAlias *pAlias = itemDef_GetAlias(pItemDef);
	int i;

	if(pAlias)
	{
		for(i=0;i<eaSize(&pAlias->ppChoices);i++)
		{
			if(itemAlias_Eval(pAlias->ppChoices[i],pEntity))
				return pAlias->ppChoices[i]->DisplayNameMessage;
		}
	}

	return pItemDef->displayNameMsg;
}

/*
void itemAlias_Generate(ItemAlias *pAlias)
{
	if(pAlias)
	{
		int i;

		for(i=0;i<eaSize(&pAlias->ppChoices);i++)
		{
			if(pAlias->ppChoices[i]->pExprRequires)
				exprGenerate(pAlias->ppChoices[i]->pExprRequires,s_pContextAlias);
		}
	}
}
*/

/*
AUTO_RUN;
void itemAlias_AutoRun(void)
{
	s_pchPlayer = allocAddStaticString("Player");
}
*/

void itemAlias_Load(void)
{
	ParserLoadFiles(NULL, "defs/config/ItemAlias.def","ItemAlias.bin", PARSER_OPTIONALFLAG, parse_ItemAliasLookup,&gAliasLookup);
	/*

	s_pContextAlias = exprContextCreate();

	exprContextSetSelfPtr(s_pContextAlias,NULL);
	exprContextSetPointerVarPooledCached(s_pContextAlias,s_pchPlayer,NULL,parse_Entity,true,true,&s_hPlayer);
	for(i=0;i<eaSize(&gAliasLookup.ppAlias);i++)
	{
		itemAlias_Generate(gAliasLookup.ppAlias[i]);
	}
	*/
}

// Generates the index item sort type array in gSortTypes from the non-indexed list 
static void item_ItemSortTypeFixup(void)
{
	if (gSortTypes.ppIndexedItemSortTypes == NULL)
	{
		// Enable indexing
		eaIndexedEnable(&gSortTypes.ppIndexedItemSortTypes, parse_ItemSortType);
	}
	else
	{
		eaClear(&gSortTypes.ppIndexedItemSortTypes);
	}

	// Add all item sort types to the indexed array
	FOR_EACH_IN_CONST_EARRAY_FORWARDS(gSortTypes.ppItemSortType, ItemSortType, pItemSortType)
	{
		eaIndexedAdd(&gSortTypes.ppIndexedItemSortTypes, pItemSortType);
	}
	FOR_EACH_END
}

// Validates item sort type data
static void item_ItemSortTypeValidate(void)
{
	// Make sure all item sort type categories have valid children
	FOR_EACH_IN_CONST_EARRAY_FORWARDS(gSortTypes.ppItemSortTypeCategories, ItemSortTypeCategory, pItemSortTypeCategory)
	{
		S32 i;
		for (i = 0; i < eaiSize(&pItemSortTypeCategory->eaiItemSortTypes); i++)
		{
			// See if the item sort type exists
			if (eaIndexedFindUsingInt(&gSortTypes.ppIndexedItemSortTypes, pItemSortTypeCategory->eaiItemSortTypes[i]) == -1)
			{
				ErrorFilenamef(gSortTypes.pchFileName, 
					"Item sort type category '%s' has an invalid sort type: %d", 
					pItemSortTypeCategory->pchName, 
					pItemSortTypeCategory->eaiItemSortTypes[i]);
			}
		}
	}
	FOR_EACH_END
}

static void item_LoadConfigInternal(const char* pchPath, S32 iWhen)
{
	StructReset(parse_ItemConfig, &g_ItemConfig);

	loadstart_printf("Loading Item Config... ");

	ParserLoadFiles(NULL, 
		"defs/config/ItemConfig.def", 
		"ItemConfig.bin", 
		PARSER_OPTIONALFLAG, 
		parse_ItemConfig,
		&g_ItemConfig);

	if (g_ItemConfig.pItemTransmuteCost)
	{
		exprGenerate(g_ItemConfig.pItemTransmuteCost, g_pItemContext);
	}

	loadend_printf(" done.");
}

// Load game-specific configuration settings
static void item_LoadConfig(void)
{
	item_LoadConfigInternal(NULL, 0);
	FolderCacheSetCallback(FOLDER_CACHE_CALLBACK_UPDATE, "defs/config/ItemConfig.def", item_LoadConfigInternal);
}

AUTO_STARTUP(Items) ASTRT_DEPS(AS_Messages, Powers, EntityCostumes, InventoryBags, ItemQualities, ItemEval, ItemPowers, InfuseSlots, ItemVars, ItemTags, ItemArt, LoreCategories, LoreJournalTypes, CharacterClasses, UsageRestrictionCategoriesMsgCheck, ItemEquipLimitCategories, ItemHandlingConfig, SharedBank, Species, AS_GuildBankConfig);
void item_Load(void)
{
	const char *pchMessageFail;
	char *pcPath = NULL;
	char *pcBinFile = NULL;

	item_LoadConfig();
	ItemGenRewardsLoad();

	loadstart_printf("Loading Misc Item files...");
	ParserLoadFiles(NULL, ITEM_SORT_TYPE_FILE, "ItemSortTypes.bin", PARSER_OPTIONALFLAG, parse_ItemSortTypes, &gSortTypes);
	item_ItemSortTypeFixup();

	if (!IsClient())
	{
		char pcDirList[512];
		sprintf(pcDirList, "%s;maps", GameBranch_GetDirectory(&pcPath, ITEMS_BASE_DIR));
		resLoadResourcesFromDisk(
				g_hItemDict, 
				pcDirList,
				".item", 
				GameBranch_GetFilename(&pcBinFile, ITEMS_BIN_FILE), 
				PARSER_OPTIONALFLAG | RESOURCELOAD_USERDATA | RESOURCELOAD_SHAREDMEMORY | PARSER_HUGEBUFFERFORBINARYFILE);

		// Now that itemcategories are no longer hard-coded, other projects 
		// might feel the need to do a fixup on all of their itemdefs to convert 
		// them en masse to new qualities. Here's where you can do that.
		// To run the sample fixup, you would need to temporarily set up itemqualities.def
		// to contain the union of your new qualities and the old ones.
		if(0)
		{
			ItemDef *pdef;
			ItemDef **ppdefsFix = NULL;
			RefDictIterator iter;
			RefSystem_InitRefDictIterator(g_hItemDict, &iter);
			while(pdef = (ItemDef*)RefSystem_GetNextReferentFromIterator(&iter))
			{
				// Do stuff here to fix the ItemDef. If something was changed, 
				// push it to the earray to be re-written to disk.
				bool bFixed = false;
				
				// ItemDefRef ID fix-up
				if (g_ItemConfig.bUseUniqueIDsForItemPowerDefRefs)
				{
					U32 uID = 0;
					
					FOR_EACH_IN_EARRAY_FORWARDS(pdef->ppItemPowerDefRefs, ItemPowerDefRef, pItemPowerDefRef)
					{
						NOCONST(ItemPowerDefRef) *pNoConstItemPowerDefRef = CONTAINER_NOCONST(ItemPowerDefRef, pItemPowerDefRef);
						pNoConstItemPowerDefRef->uID = uID++;
						bFixed = true;
					}
					FOR_EACH_END

					pdef->uNewItemPowerDefRefID = uID;
				}

				// Sample fixup operation, converting old qualities into NNO ones.
				/*				
				if (pdef->Quality == StaticDefineIntGetInt(ItemQualityEnum, "White"))
					pdef->Quality = StaticDefineIntGetInt(ItemQualityEnum, "Bronze");
				else if (pdef->Quality == StaticDefineIntGetInt(ItemQualityEnum, "Yellow"))
					pdef->Quality = StaticDefineIntGetInt(ItemQualityEnum, "Bronze");
				else if (pdef->Quality == StaticDefineIntGetInt(ItemQualityEnum, "Green"))
					pdef->Quality = StaticDefineIntGetInt(ItemQualityEnum, "Silver");
				else if (pdef->Quality == StaticDefineIntGetInt(ItemQualityEnum, "Blue"))
					pdef->Quality = StaticDefineIntGetInt(ItemQualityEnum, "Silver");
				else if (pdef->Quality == StaticDefineIntGetInt(ItemQualityEnum, "Purple"))
					pdef->Quality = StaticDefineIntGetInt(ItemQualityEnum, "Gold");
				else
					bFixed = false;
				*/

				if (bFixed)
					eaPush(&ppdefsFix, pdef);					
			}

			if(ppdefsFix)
			{
				int i;
				for(i=eaSize(&ppdefsFix)-1; i>=0; i--)
				{
					pdef = ppdefsFix[i];
					ParserWriteTextFileFromDictionary(pdef->pchFileName,g_hItemDict,0,0);
				}
				eaDestroy(&ppdefsFix);
			}
		}

	}

	// load default deconstruction item data
	ParserLoadFiles(NULL, 
		GameBranch_FixupPath(&pcPath, ITEM_DECONSTRUCT_DEFAULTS_FILE, false, true),
		GameBranch_GetFilename(&pcBinFile, "ItemDeconstructDefaults.bin"),
		PARSER_OPTIONALFLAG, parse_ItemDeconstructDefaults, &gItemDeconstructDefaults);

	// verify that a message is specified for all of the tags
	if (pchMessageFail = StaticDefineVerifyMessages(ItemTagEnum))
	{
		Errorf("Not all ItemTag messages were found: %s", pchMessageFail);
	}

	if (pchMessageFail = StaticDefineVerifyMessages(SkillTypeEnum))
	{
		Errorf("Not all SkillType messages were found: %s", pchMessageFail);
	}

	if (pchMessageFail = StaticDefineVerifyMessages(ItemQualityEnum))
	{
		Errorf("Not all ItemType messages were found: %s", pchMessageFail);
	}

	if (pchMessageFail = StaticDefineVerifyMessages(LootModeEnum))
	{
		Errorf("Not all LootMode messages were found: %s", pchMessageFail);
	}
#if GAMESERVER || GAMECLIENT
	itemAlias_Load();
#endif

	estrDestroy(&pcPath);
	estrDestroy(&pcBinFile);

	loadend_printf("done.");
}

ItemSortType *item_GetSortTypeForTypes(ItemType eType, const InvBagIDs * const peaiRestrictBagIDs, SlotType eRestrictSlotType, const ItemCategory * const peaiItemCategories)
{
	int i;
	for (i = 0; i < eaSize(&gSortTypes.ppItemSortType); i++)
	{
		ItemSortType *pSortType = gSortTypes.ppItemSortType[i];
		if (pSortType->iSortID != 0 && 
			( ( pSortType->eType == eType ) || ( ( pSortType->eType2 != kItemType_None && pSortType->eType2 != kItemType_Upgrade ) && ( pSortType->eType2 == eType ) ) ) &&
			(pSortType->eRestrictSlotType == 0 || pSortType->eRestrictSlotType == eRestrictSlotType))
		{
			S32 j;
			bool bRestrictBagFound = true;

			if (pSortType->eRestrictBagID != InvBagIDs_None)
			{
				bRestrictBagFound = false;
				// Make sure the restrict bag exists in the item				
				for (j = 0; j < ea32Size(&peaiRestrictBagIDs); j++)
				{
					if (peaiRestrictBagIDs[j] == pSortType->eRestrictBagID)
					{
						bRestrictBagFound = true;
						break;
					}
				}				
			}

			if (!bRestrictBagFound)
			{
				continue;
			}

			if (pSortType->eItemCategory != kItemCategory_None)
			{
				// Make sure the category exists in the item				
				for (j = 0; j < ea32Size(&peaiItemCategories); j++)
				{
					if (peaiItemCategories[j] == pSortType->eItemCategory)
					{
						return pSortType;
					}
				}
			}
			else
			{
				return pSortType;
			}			
		}
	}
	return NULL;
}

ItemSortType *item_GetSortTypeForID(U32 itemSortID)
{
	S32 iIndex = eaIndexedFindUsingInt(&gSortTypes.ppIndexedItemSortTypes, itemSortID);

	return iIndex >= 0 ? gSortTypes.ppIndexedItemSortTypes[iIndex] : NULL;
}

void item_GetSearchableSortTypes(ItemSortType ***pppTypes)
{
	int i;
	for (i = 0; i < eaSize(&gSortTypes.ppItemSortType); i++)
	{
		ItemSortType *pSortType = gSortTypes.ppItemSortType[i];
		if (pSortType->bSearchable)
		{
			eaPush(pppTypes, pSortType);
		}
	}
}

// Appends local enhancements on the item to the earray
void item_GetEnhancementsLocal(Item *pItem, Power ***pppPowersAttached)
{
	int iPower;
	int NumPowers;
	NumPowers = item_GetNumItemPowerDefs(pItem, true);

	for(iPower=NumPowers-1; iPower>=0; iPower--)
	{
		PowerDef *pdef;
		Power* ppowItem = item_GetPower(pItem, iPower);

		if(!ppowItem)
			continue;

		pdef = GET_REF(ppowItem->hDef);

		if(pdef && pdef->eType==kPowerType_Enhancement)
		{
			ItemPowerDef *pItemPowerDef = item_GetItemPowerDef(pItem, iPower);
			if(pItemPowerDef && (pItemPowerDef->flags&kItemPowerFlag_LocalEnhancement))
			{
				eaPush(pppPowersAttached,ppowItem);
			}
		}
	}
}


// Attaches enhancements on the same item as the power, if they are "local" enhancements.  Global enhancements
//  would already be attached.
void power_GetEnhancementsLocalItem(Entity *pEnt, Power *ppow, Power ***pppPowersAttached)
{
	Item *pItem = NULL;
	Power *pChildPower = NULL;

	if(ppow->pParentPower)
	{
		pChildPower = ppow;
		ppow = ppow->pParentPower;
	}

	// Find the item
	if (ppow->pSourceItem)
	{
		pItem = ppow->pSourceItem;
	}
	else
	{
		item_FindPowerByID(pEnt,ppow->uiID,NULL,NULL,&pItem,NULL);
	}

	// If the parent power did not have an item, maybe the child power did.
	// This fixes a bug with store UIs not having enhancements on their item tooltips
	if (!pItem && pChildPower)
	{
		// Find the item
		if (pChildPower->pSourceItem)
		{
			pItem = pChildPower->pSourceItem;
		}
		else
		{
			item_FindPowerByID(pEnt,pChildPower->uiID,NULL,NULL,&pItem,NULL);
		}
	}

	if(pItem)
	{
		item_GetEnhancementsLocal(pItem, pppPowersAttached);
	}
}

#ifdef GAMESERVER
#define ServerOnly_SendNotify(pEnt, eType, pchDisplayString, pchLogicalString, pchTexture, bSilent) if (!(bSilent)) ClientCmd_NotifySend(pEnt, eType, pchDisplayString, pchLogicalString, pchTexture)
#else
#define ServerOnly_SendNotify(pEnt, eType, pchDisplayString, pchLogicalString, pchTexture, bSilent) 
#endif


// Guess what we think the actual move amounts are going to be. Possibly inaccurate as the transaction code may decide something different internally.
void item_GuessAtMoveCounts(int iCount, int iSrcStackCount, int iDstStackCount, ItemDef *pSrcItemDef, ItemDef *pDstItemDef, S32 *piSrcMoveCount, S32 *piDstMoveCount)
{
	if (piSrcMoveCount==NULL || piDstMoveCount==NULL)
	{
		return;
	}
	
	*piSrcMoveCount=iCount;
	*piDstMoveCount=0;

	if (*piSrcMoveCount<0)
	{
		*piSrcMoveCount = iSrcStackCount;
	}
	if (pDstItemDef!=NULL)
	{
		*piDstMoveCount = iDstStackCount;
				
		// see if we're moving to the same item
		if (pDstItemDef == pSrcItemDef)
		{
			// See if there is room in the dest stack. (Otherwise it is a swap)
			if (iDstStackCount < pSrcItemDef->iStackLimit)
			{
				*piDstMoveCount=0;
				if (*piSrcMoveCount + iDstStackCount > pSrcItemDef->iStackLimit)
				{
					// Count only what we are going to move to cap off the stack
					*piSrcMoveCount = pSrcItemDef->iStackLimit - iDstStackCount;
				}
			}
		}
	}
}


// Make sure that bank and guild bank bags are only accessed through the correct contacts
static bool item_VerifyBankContacts(Entity *pEnt, InventoryBag *pSrcBag, InventoryBag *pDstBag, bool bSilent)
{
	// Verify that bank bags are only being accessed by the allowed means.
	//   mblattel[19Oct11]: This used to be active only if not ACCESS_DEBUG. This has been removed because it was
	//   causing problems and was not valid across all code paths anyway
	
	if ((invbag_flags(pSrcBag) & (InvBagFlag_BankBag | InvBagFlag_GuildBankBag)) || (invbag_flags(pSrcBag) & (InvBagFlag_BankBag | InvBagFlag_GuildBankBag)))
	{
		ContactDef *pContactDef = NULL;
		ContactDialog *pDialog = NULL;
		bool bIsBank = false;
		bool bIsGuildBank = false;
		if (SAFE_MEMBER3(pEnt, pPlayer, pInteractInfo, pContactDialog)) {
			pContactDef = GET_REF(pEnt->pPlayer->pInteractInfo->pContactDialog->hContactDef);
			pDialog = pEnt->pPlayer->pInteractInfo->pContactDialog;
		}

		// We cannot simply check the ContactDef as the client does not have access to Contact Defs
		if (!pContactDef && !pDialog) {
			return false;
		} else {
			bIsBank = ((pContactDef && contact_IsBank(pContactDef)) || (pDialog && pDialog->screenType == ContactScreenType_Bank));
			bIsGuildBank = ((pContactDef && contact_IsGuildBank(pContactDef)) || (pDialog && pDialog->screenType == ContactScreenType_GuildBank));
		}

		if ((invbag_flags(pSrcBag) & InvBagFlag_BankBag) && !bIsBank) {
			return false;
		}
		if ((invbag_flags(pDstBag) & InvBagFlag_BankBag) && !bIsBank) {
			return false;
		}
		
		if ((invbag_flags(pSrcBag) & InvBagFlag_GuildBankBag) && !bIsGuildBank) {
			return false;
		}
		if ((invbag_flags(pDstBag) & InvBagFlag_GuildBankBag) && !bIsGuildBank) {
			return false;
		}
	}
	return true;
}

AUTO_TRANS_HELPER;
bool item_trh_ItemDefMeetsBagRestriction(ItemDef* pDef, ATH_ARG NOCONST(InventoryBag)* pBag)
{
	bool bItemHasRestrictions = (eaiSize(&pDef->peRestrictBagIDs) != 0);
	bool bBagMissingRestriction = bItemHasRestrictions && (eaiFind(&pDef->peRestrictBagIDs, invbag_trh_bagid(pBag)) < 0);

	// If this item is not allowed in this bag because of a restriction it has, or it is not allowed because it does NOT have a restriction the bag demands
	if (bBagMissingRestriction || ((invbag_trh_flags(pBag) & InvBagFlag_RestrictedOnly) && !bItemHasRestrictions))
	{
		return false;
	}
	return true;
}

// Specifically check if the moving item meets the slot restrictions for the destination slot.
// This is not a complete check if the move is valid, but is used by external functions to see that at
// least the slot will accept the item. The move verify functions will do a complete check if that
// is what is actually needed

static bool item_ItemMoveDestValidInternal(Entity *pEnt, ItemDef *pItemDef, const Item *pItem, Entity *pEntDst, InventoryBag *pBag, S32 iSlot, bool bSilent)
{
	S32 minItemLevel;
	int iFirstEmpty = -1;
	const char* pchErrorKey = NULL;
	if (!pEnt || !pBag || !pItemDef) {
		return false;
	}

	if(pItem && (pItem->flags & kItemFlag_Bound) && pEntDst && pEntDst->myEntityType == GLOBALTYPE_ENTITYSHAREDBANK)
	{
		if(!bSilent)
		{
			ServerOnly_SendNotify(pEnt, kNotifyType_ItemMoveFailed, entTranslateMessageKey(pEnt, "InventoryUI.BoundItemToSharedBank"), NULL, NULL, false);
		}
		return false;
	}
	
	// Numeric items never move, at all, period; Their values are just shifted
	// Nor are items ever moved into or out of the numeric bag; They may be added, but never moved
	if (pItemDef->eType == kItemType_Numeric || invbag_bagid(pBag) == InvBagIDs_Numeric) {
		return false;
	}
	
	if(pItem)	
	{
		minItemLevel = item_GetMinLevel(pItem);	
	}
	else
	{
		minItemLevel = 0;	
	}

	// mblattel [20Oct11] Previously this code assumed that if we are passed in a negative iSlot it means the first empty slot.
	// 	This is possibly a weak assumption.
	//  It is really up to the transaction to deal with a negative slot and unless we exactly replicate the internal code, we
	//  are merely guessing at this point in the code.
	//  For now we should not validate the destination slot itslef at all if the slot is negative. We assume the transaction will
	//  fail at the appropriate juncture with an appropriate error. 
//		iFirstEmpty = (iSlot < 0) ? inv_bag_GetFirstEmptySlot(pEnt, pBag) : iSlot;
	
	// Check usage restrictions, If a level of the item has been changed then use set the override level.
	//  This takes care of source from level etc item (and reward table UsePlayerLevel).
	if ((invbag_flags(pBag) & InvBagFlag_SpecialBag) && !itemdef_VerifyUsageRestrictions(entGetPartitionIdx(pEnt), pEntDst, pItemDef, minItemLevel, &pchErrorKey, iFirstEmpty)) {
		if (pchErrorKey)
		{
			char* pchErrorMsg = NULL;
			entFormatGameMessageKey(pEnt, &pchErrorMsg, pchErrorKey, STRFMT_ITEM(pItem), STRFMT_END);
			ServerOnly_SendNotify(pEnt, kNotifyType_ItemMoveFailed, pchErrorMsg, NULL, NULL, bSilent);
			estrDestroy(&pchErrorMsg);
		}
		return false;
	}
	

	// If this item is marked as 'LockToRestrictBags', then it must go in a restrict bag
	if ((pItemDef->flags & kItemDefFlag_LockToRestrictBags) && eaiFind(&pItemDef->peRestrictBagIDs, pBag->BagID) < 0)
	{
		return false;
	}
	
	// All items allowed into inventory or player bags
	if (pBag->BagID == InvBagIDs_Inventory || (invbag_flags(pBag) & (InvBagFlag_PlayerBag | InvBagFlag_BankBag | InvBagFlag_GuildBankBag))) {
		//...except Boffs
		if ((pItemDef->eType == kItemType_STOBridgeOfficer) || (pItemDef->eType == kItemType_AlgoPet))
		{
			return false;
		}
		else
		{
			return true;
		}
	}

	// Disallow moving items into the overflow bag
	if (pBag->BagID == InvBagIDs_Overflow) {
		return false;
	}
	
	// If this is a bag move into an index bag then verify that the item is a bag
	if ((invbag_flags(pBag) & InvBagFlag_PlayerBagIndex) && pItemDef->eType != kItemType_Bag) {
		ServerOnly_SendNotify(pEnt, kNotifyType_ItemMoveFailed, entTranslateMessageKey(pEnt, "InventoryUI.NotPlayerBag"), NULL, NULL, bSilent);
		return false;
	}
	
	// Do some special validation if this is being moved into a recipe bag
	if (invbag_flags(pBag) & InvBagFlag_RecipeBag) {
		// Make sure the item is a recipe, and that it isn't already known
		return !((pItemDef->eType != kItemType_ItemRecipe && pItemDef->eType != kItemType_ItemPowerRecipe) || inv_bag_GetItemByName(pBag, pItemDef->pchName));
	}
	
	// Make sure that the player has a high enough level to equip any upgrades, weapons or devices
	if (pItem && !(pItemDef->flags & kItemDefFlag_NoMinLevel) && (invbag_flags(pBag) & (InvBagFlag_EquipBag | kItemType_Weapon | kItemType_Device)) && entity_GetSavedExpLevel(pEnt) < item_GetMinLevel(pItem)) {
		ServerOnly_SendNotify(pEnt, kNotifyType_ItemMoveFailed, entTranslateMessageKey(pEnt, "InventoryUI.InsufficientLevel"), NULL, NULL, bSilent);
		return false;
	}
	
	// Validate equip bags and weapon bags
	// If it has slot restrictions, make sure it's going in the right slots
	if ((invbag_flags(pBag) & (InvBagFlag_EquipBag | InvBagFlag_WeaponBag | InvBagFlag_ActiveWeaponBag))) {
		bool bBagAcceptsItem = false;
		const InvBagDef *pBagDef = invbag_def(pBag);
		if (pItem && item_IsUnidentified(pItem))
		{
			ServerOnly_SendNotify(pEnt, kNotifyType_ItemMoveFailed, entTranslateMessageKey(pEnt, "InventoryUI.NotIdentified"), NULL, NULL, bSilent);
			return false;
		}
		if (invbag_flags(pBag) & InvBagFlag_EquipBag) {
			if (pItemDef->eType == kItemType_Upgrade || pItemDef->eType == kItemType_STOBridgeOfficer || pItemDef->eType == kItemType_AlgoPet || pItemDef->eType == kItemType_Injury) {
				bBagAcceptsItem = true;
			}
		}

		if (invbag_flags(pBag) & (InvBagFlag_WeaponBag | InvBagFlag_ActiveWeaponBag)) {
			if (pItemDef->eType == kItemType_Weapon)
			{
				bBagAcceptsItem = true;
			}
		}

		if (!bBagAcceptsItem)
		{
			// I've created a situation here that has a slight flaw.  I created the notion that a bag could be both an equip bag AND a weapon bag,
			// for our ranged slot.  The ideal message in this case might be "Only ranged items can go in that slot."  Currently, we get inconsistent
			// messages depending on what we try to put in the slot.  [RMARR - 11/8/10]
			if (invbag_flags(pBag) & InvBagFlag_EquipBag)
			{
				ServerOnly_SendNotify(pEnt, kNotifyType_ItemMoveFailed, entTranslateMessageKey(pEnt, "InventoryUI.NonUpgradeInEquipBag"), NULL, NULL, bSilent);
			}
			else
			{
				ServerOnly_SendNotify(pEnt, kNotifyType_ItemMoveFailed, entTranslateMessageKey(pEnt, "InventoryUI.NonWeaponInWeaponBag"), NULL, NULL, bSilent);
			}
			return false;
		}

		// iSlot may be negative which indicates not to validate
		if (item_IsSecondaryEquip(pItemDef) && iSlot == 0) {
			ServerOnly_SendNotify(pEnt, kNotifyType_ItemMoveFailed, entTranslateMessageKey(pEnt, "InventoryUI.SecondaryInNonSecondarySlot"), NULL, NULL, bSilent);
			return false;
		} else if (item_IsPrimaryEquip(pItemDef) && iSlot > 0) {
			ServerOnly_SendNotify(pEnt, kNotifyType_ItemMoveFailed, entTranslateMessageKey(pEnt, "InventoryUI.PrimaryInNonPrimarySlot"), NULL, NULL, bSilent);
			return false;
		}

		if (eaiSize(&pBagDef->pePrimaryOnlyCategories))
		{
			// Check to see if item in slot 0 disables secondary slots
			if (iSlot == 0)
			{
				int i;
				for(i=eaiSize(&pBagDef->pePrimaryOnlyCategories)-1; i>=0; i--)
				{
					if (-1 != eaiFind(&pItemDef->peCategories, pBagDef->pePrimaryOnlyCategories[i]))
					{
						int j;
						for (j = 1; j < invbag_maxslots(pEntDst, pBag); j++)
						{						
							Item *pOtherItem = inv_bag_GetItem(pBag, j);
							if (pOtherItem)
							{
								// We're a primary that excludes secondaries, so stop now. This will be replaced by a proper swap
								ServerOnly_SendNotify(pEnt, kNotifyType_ItemMoveFailed, entTranslateMessageKey(pEnt, "InventoryUI.CannotEquipPrimaryOnlyBecauseSecondaryEquipped"), NULL, NULL, bSilent);
								return false;
							}
						}
					}
				}
			}
			else if (iSlot >= 1 && inv_bag_GetItem(pBag, 0))
			{
				int i;
				Item *pOtherItem = inv_bag_GetItem(pBag, 0);
				ItemDef *pOtherItemDef = GET_REF(pOtherItem->hItem);

				if (pOtherItemDef)
				{				
					for(i=eaiSize(&pBagDef->pePrimaryOnlyCategories)-1; i>=0; i--)
					{
						if (-1 != eaiFind(&pOtherItemDef->peCategories, pBagDef->pePrimaryOnlyCategories[i]))
						{
							// First item excludes secondaries, so we can't go here
							ServerOnly_SendNotify(pEnt, kNotifyType_ItemMoveFailed, entTranslateMessageKey(pEnt, "InventoryUI.CannotEquipSecondaryBecausePrimaryOnlyEquipped"), NULL, NULL, bSilent);
							return false;
						}
					}
				}
			}

				
		}
	}

	// If it's a device bag, make sure the item is a device
	if ((invbag_flags(pBag) & InvBagFlag_DeviceBag) && pItemDef->eType != kItemType_Device) {
		ServerOnly_SendNotify(pEnt, kNotifyType_ItemMoveFailed, entTranslateMessageKey(pEnt, "InventoryUI.NonDeviceInDeviceBag"), NULL, NULL, bSilent);
		return false;
	}
	
	// Error check destination bag matched restricted bag if item restriction is set
	// ** Recipes use this field for an alternate purpose, so this check for recipes
	//    doesn't work. For recipes, the bag restrict is the restriction of the product.

	
	if (!item_IsRecipe(pItemDef))
	{
		if (!item_ItemDefMeetsBagRestriction(pItemDef, pBag))
		{
			ServerOnly_SendNotify(pEnt, kNotifyType_ItemMoveFailed, entTranslateMessageKey(pEnt, "InventoryUI.ItemRestrictedFromThatBag"), NULL, NULL, bSilent);
			return false;
		}

	}

	// Special check for items slotted on assignments
	if (pItem && (pItem->flags & kItemFlag_SlottedOnAssignment) && !ItemAssignment_CanSlottedItemResideInBag(pBag))
	{
		ServerOnly_SendNotify(pEnt, kNotifyType_ItemMoveFailed, entTranslateMessageKey(pEnt, "InventoryUI.ItemSlottedOnAssignment"), NULL, NULL, bSilent);
		return false;
	}
	
	return true;
}

// Consider only whether we can move the destination item
bool item_ItemMoveDestValid(Entity *pEnt, ItemDef *pItemDef, const Item *pItem, bool bGuild, S32 iBagID, S32 iSlot, bool bSilent, GameAccountDataExtract *pExtract)
{
	InventoryBag *pBag = bGuild ? inv_guildbank_GetBag(guild_GetGuildBank(pEnt), iBagID) : (InventoryBag*)inv_GetBag(CONTAINER_NOCONST(Entity, pEnt), iBagID, pExtract);
	
	return item_ItemMoveDestValidInternal(pEnt, pItemDef, pItem, pEnt, pBag, iSlot, bSilent);
}


// Consider only whether we can remove the item from the source slot
static bool item_ItemMoveSrcValidInternal(Entity *pEnt, InventoryBag *pBag, S32 iSlot)
{
	Item *pItem = inv_bag_GetItem(pBag, iSlot);
	ItemDef *pItemDef = pItem ? GET_REF(pItem->hItem) : NULL;
	bool bSilent = false;
	
	if (!pEnt || !pBag || !pItemDef) {
		return false;
	}

	// Numeric items never move, at all, period; Their values are just shifted
	// Nor are items ever moved into or out of the numeric bag; They may be added, but never moved
	if (pItemDef->eType == kItemType_Numeric || invbag_bagid(pBag) == InvBagIDs_Numeric) {
		return false;
	}
	
	// If this is a bag move out of an index bag then verify that the bag is empty
	if (pItemDef->eType == kItemType_Bag && (invbag_flags(pBag) & InvBagFlag_PlayerBagIndex) && inv_PlayerBagFail(pEnt, pBag, iSlot)) {
		ServerOnly_SendNotify(pEnt, kNotifyType_ItemMoveFailed, entTranslateMessageKey(pEnt, "InventoryUI.PlayerBagNotEmpty"), NULL, NULL, bSilent);
		return false;
	}
	
	// Things in a recipe bag need to stay there.
	if (invbag_flags(pBag) & InvBagFlag_RecipeBag) {
		return false;
	}
	
	return true;
}



// mblattel[19Oct11] As far as I can tell, it's kind of arbitrary as to which checks are in this function and which are in item_ItemMoveValidCommon.
//   Historically, this function was left untouched when I reworked the item validation. Theoretically this code
//   could be merged into item_ItemMoveValidCommon, and that should be considered at a future date. 
static bool item_ItemMoveValidInternal(Entity *pEnt, ItemDef *pItemDef, InventoryBag *pSrcBag, S32 iSrcSlot, Entity *pEntDst, InventoryBag *pDstBag, S32 iDstSlot)
{
	bool bSilent = false;
	Item *pItem = inv_bag_GetItem(pSrcBag, iSrcSlot);
	
	if (!pSrcBag || !pDstBag || !pItem || !pItemDef) {
		return false;
	}

	// Check if we're trying to move a bound item into a guild bank
	if ((invbag_flags(pDstBag) & InvBagFlag_GuildBankBag) && (pItem->flags & (kItemFlag_Bound | kItemFlag_BoundToAccount))) {
		ServerOnly_SendNotify(pEnt, kNotifyType_ItemMoveFailed, entTranslateMessageKey(pEnt, "InventoryUI.BoundItemToGuildBank"), NULL, NULL, bSilent);
		return false;
	}

	if(pEntDst->myEntityType == GLOBALTYPE_ENTITYSAVEDPET && invbag_flags(pDstBag) & InvBagFlag_NoCopy)
	{
		int i;
		//If its a puppet, and the bag has a puppet no copy flag, don't do the move

		if(pEnt && pEnt->pSaved && pEnt->pSaved->pPuppetMaster)
		{
			for(i=0;i<eaSize(&pEnt->pSaved->pPuppetMaster->ppPuppets);i++)
			{
				if(pEnt->pSaved->pPuppetMaster->ppPuppets[i]->curID	== pEntDst->myContainerID)
					return false;
			}
		}
	}
	
	// Check that the destination is valid
	if (!item_ItemMoveDestValidInternal(pEnt, pItemDef, pItem, pEntDst, pDstBag, iDstSlot, bSilent)) {
		return false;
	}
	
	// Check that the source is valid
	if (!item_ItemMoveSrcValidInternal(pEnt, pSrcBag, iSrcSlot)) {
		return false;
	}
	
	// Make sure that the player has a high enough level to equip any upgrades, weapons or devices
	if (!(pItemDef->flags & kItemDefFlag_NoMinLevel) && (invbag_flags(pDstBag) & (InvBagFlag_EquipBag | kItemType_Weapon | kItemType_Device)) && entity_GetSavedExpLevel(pEnt) < item_GetMinLevel(pItem)) {
		ServerOnly_SendNotify(pEnt, kNotifyType_ItemMoveFailed, entTranslateMessageKey(pEnt, "InventoryUI.InsufficientLevel"), NULL, NULL, bSilent);
		return false;
	}
	
	// If either of the bags is marked InvBagFlag_NoModifyInCombat and the entity is in combat 
	// and the src and dst bags aren't the same bag, don't allow items to be moved
	if (((invbag_flags(pSrcBag) & InvBagFlag_NoModifyInCombat) || (invbag_flags(pDstBag) & InvBagFlag_NoModifyInCombat)) &&
		invbag_bagid(pSrcBag) != invbag_bagid(pDstBag) && character_HasMode(pEnt->pChar, kPowerMode_Combat)) {
		
		ServerOnly_SendNotify(pEnt, kNotifyType_ItemMoveFailed, entTranslateMessageKey(pEnt, "InventoryUI.NoModifyInCombat"), NULL, NULL, bSilent);
		return false;
	}
	
	// If it hasn't hit any problems, return true
	return true;
}

// If the player is controlling a temporary puppet, don't allow movement of items unless it's a NoCopy bag
bool item_ItemMoveValidTemporaryPuppetCheck(Entity* pEnt, InventoryBag* pBag)
{
	if (pEnt &&
		pEnt->pSaved &&
		pEnt->pSaved->pPuppetMaster &&
		pEnt->pSaved->pPuppetMaster->curTempID)
	{
		if (!(invbag_flags(pBag) & InvBagFlag_NoCopy))
		{
			return false;
		}
	}
	return true;
}


// Bottleneck move validation. Most validation should use this function.
// Check various conditions to see if a move is valid

static bool item_ItemMoveValidInternalCommon(Entity *pEnt, ItemDef *pSrcItemDef, int iCount, Entity *pSrcEnt, InventoryBag *pSrcBag, S32 iSrcSlot, Entity *pDstEnt, InventoryBag *pDstBag, S32 iDstSlot)
{
	bool bSilent = false;
	
	Item *pSrcItem;
	Item* pDstItem;
	ItemDef* pDstItemDef;
	S32 iDstStackCount;
	S32 iSrcStackCount;
	bool bSrcGuild;
	bool bDstGuild;
	S32 iSrcMoveCount=0;	// Our guess as to how many items will actually be moved. May differ from code actually inside transaction.
	S32 iDstMoveCount=0;
	bool bItemSwap=false;			// If we think we are swapping two items' locations

	ItemEquipLimitCategoryData* pLimitCategory = NULL;
	S32 iEquipLimit = -1;

	///////////////////////////////////
	// Make sure we have source and dest bags, and that there is a valid item count
	//
	if (!pSrcBag || !pDstBag || !iCount)
	{
		return false;
	}
	
	///////////////////////////////////
	// Get some useful counts and defs

	pSrcItem = inv_bag_GetItem(pSrcBag, iSrcSlot);
	iSrcStackCount = inv_bag_GetSlotItemCount(pSrcBag, iSrcSlot);

	pDstItem = inv_bag_GetItem(pDstBag, iDstSlot);
	pDstItemDef = pDstItem ? GET_REF(pDstItem->hItem) : NULL;
	iDstStackCount = inv_bag_GetSlotItemCount(pDstBag, iDstSlot);

	/////////////////////////////////
	/// Imported from itemServer MoveInternal code

	// Disallow dropping a bag into itself
	if (invbag_bagid(pSrcBag) == InvBagIDs_PlayerBags && iSrcSlot == (invbag_bagid(pDstBag) - InvBagIDs_PlayerBag1)) {
		return false;
	}
	
	// make sure that there are no unique equipped items in dest bag that are the same def
	if(pSrcBag != pDstBag && !inv_CanEquipUniqueCheckSwap(pEnt, pSrcItemDef, pDstItemDef, pSrcBag, pDstBag))
	{
		ServerOnly_SendNotify(pEnt, kNotifyType_ItemMoveFailed, entTranslateMessageKey(pEnt, "InventoryUI.UniqueEquipAlready"), NULL, NULL, bSilent);
		return false;
	}

	// Check the equip limit on the source item
	if (pSrcBag != pDstBag && !inv_ent_EquipLimitCheckSwap(pSrcEnt, pDstEnt, pSrcBag, pDstBag, pSrcItemDef, pDstItemDef, &pLimitCategory, &iEquipLimit))
	{
		char* estrBuffer = NULL;
		const char* pchLimitCategoryDisplayName = NULL;
		if (pLimitCategory)
		{
			pchLimitCategoryDisplayName = entTranslateDisplayMessage(pEnt, pLimitCategory->msgDisplayName);
		}
		entFormatGameMessageKey(pEnt, 
								&estrBuffer, 
								"InventoryUI.EquipLimitCheck", 
								STRFMT_STRING("Category", pchLimitCategoryDisplayName), 
								STRFMT_INT("Limit", iEquipLimit), 
								STRFMT_END);
		ServerOnly_SendNotify(pEnt, kNotifyType_ItemMoveFailed, estrBuffer, NULL, NULL, bSilent);
		estrDestroy(&estrBuffer);
		return false;
	}
	
	/////////////////////////////////////////////////////
	// Contact checks. Used to be in item_ItemMoveDestValidInternal and item_ItemMoveSrcValidInternal

	if (!item_VerifyBankContacts(pEnt, pSrcBag, pDstBag, bSilent))
	{
		return false;
	}

	/////////////////////////////////////////////////////
	// Powers in use. Used to be in item_ItemMoveSrcValidInternal

	if ( gConf.bBlockItemMoveIfInUse && item_AreAnyPowersInUse( pEnt, pSrcItem ) ) {
		return false;
	}

	/////////////////////////////////////////////////////
	// We need to determine if we are swapping items if we are dragging on top of another item.
	//  Also determine how many items we guess we will theoretically be moving.
	//  The internal transaction code will eventually determine the actual move amount.

	item_GuessAtMoveCounts(iCount, iSrcStackCount, iDstStackCount, pSrcItemDef, pDstItemDef, &iSrcMoveCount, &iDstMoveCount);

	// We are swapping if there are dest items to be moved
	bItemSwap = (iDstMoveCount!=0);

	
	/////////////////////////////////////////////////////
	// Guild permission checks

	bSrcGuild = (invbag_flags(pSrcBag) & (InvBagFlag_GuildBankBag)) != 0;
	bDstGuild = (invbag_flags(pDstBag) & (InvBagFlag_GuildBankBag)) != 0;

	// Only check if we're moving to or from a guild bag (or both). Don't bother if we're moving in the same bag.
	if ((bSrcGuild || bDstGuild) && pSrcBag->BagID != pDstBag->BagID) {	
		Guild *pGuild = guild_GetGuild(pEnt);
		GuildMember *pMember = pGuild ? guild_FindMemberInGuild(pEnt, pGuild) : NULL;
		Entity *pGuildBank = guild_GetGuildBank(pEnt);
		if (pGuild && pMember && pGuildBank) {
			S32 iEPValue;
			int iPartitionIdx = entGetPartitionIdx(pEnt);
			
			if (bSrcGuild) {
				// An item is being removed from the guild, check that this is allowed
				if (pMember->iRank >= eaSize(&pSrcBag->pGuildBankInfo->eaPermissions) || !(pSrcBag->pGuildBankInfo->eaPermissions[pMember->iRank]->ePerms & GuildPermission_Withdraw)) {
					ServerOnly_SendNotify(pEnt, kNotifyType_ItemMoveFailed, entTranslateMessageKey(pEnt, "InventoryUI.NoGuildBankTabWithdrawPermission"), NULL, NULL, bSilent);
					return false;
				}

				// mblattel[20Oct11] For guild exchanges, there is the concept of StoreEPValue which is
				//  used to determine withdrawal limits it seems. We don't know the actual number
				//  of items being moved until the transaction actually happens, but the StoreEPValue
				//  function cannot be called within the transaction. So we will use our move guesses
				
				iEPValue = item_GetStoreEPValue(iPartitionIdx, pEnt, pSrcItem, NULL) * iSrcMoveCount;
			
				if (!inv_guildbank_CanWithdrawFromBankTab(pEnt->myContainerID, pGuild, pGuildBank, invbag_bagid(pSrcBag), iSrcMoveCount, iEPValue)) {
					ServerOnly_SendNotify(pEnt, kNotifyType_ItemMoveFailed, entTranslateMessageKey(pEnt, "InventoryUI.WithdrawLimitReached"), NULL, NULL, bSilent);
					return false;
				}

				// If swapping we need to put something back in this bag
				if (bItemSwap)
				{
					if (pMember->iRank >= eaSize(&pSrcBag->pGuildBankInfo->eaPermissions) || !(pSrcBag->pGuildBankInfo->eaPermissions[pMember->iRank]->ePerms & GuildPermission_Deposit)) {
						ServerOnly_SendNotify(pEnt, kNotifyType_ItemMoveFailed, entTranslateMessageKey(pEnt, "InventoryUI.NoGuildBankTabDepositPermission"), NULL, NULL, bSilent);
						return false;
					}
				}
			}
			if (bDstGuild) {

				if (pMember->iRank >= eaSize(&pDstBag->pGuildBankInfo->eaPermissions) || !(pDstBag->pGuildBankInfo->eaPermissions[pMember->iRank]->ePerms & GuildPermission_Deposit)) {
					ServerOnly_SendNotify(pEnt, kNotifyType_ItemMoveFailed, entTranslateMessageKey(pEnt, "InventoryUI.NoGuildBankTabDepositPermission"), NULL, NULL, bSilent);
					return false;
				}

				if (bItemSwap)
				{
					if (pMember->iRank >= eaSize(&pDstBag->pGuildBankInfo->eaPermissions) || !(pDstBag->pGuildBankInfo->eaPermissions[pMember->iRank]->ePerms & GuildPermission_Withdraw)) {
						ServerOnly_SendNotify(pEnt, kNotifyType_ItemMoveFailed, entTranslateMessageKey(pEnt, "InventoryUI.NoGuildBankTabWithdrawPermission"), NULL, NULL, bSilent);
						return false;
					}

					iEPValue = item_GetStoreEPValue(iPartitionIdx, pEnt, pDstItem, NULL) * iDstMoveCount;
					
					if (!inv_guildbank_CanWithdrawFromBankTab(pEnt->myContainerID, pGuild, pGuildBank, invbag_bagid(pDstBag), iDstMoveCount, iEPValue)) {
						ServerOnly_SendNotify(pEnt, kNotifyType_ItemMoveFailed, entTranslateMessageKey(pEnt, "InventoryUI.WithdrawLimitReached"), NULL, NULL, bSilent);
						return false;
					}
				}
			}
		}
	}

	/////////////////////////////////////////////////////
	/// OLD:

	// More puppet checks
	if (!item_ItemMoveValidTemporaryPuppetCheck(pSrcEnt, pSrcBag) ||
		!item_ItemMoveValidTemporaryPuppetCheck(pDstEnt, pDstBag))
	{
		return false;
	}

	// See if we're okay to move the source into the dest
	if (item_ItemMoveValidInternal(pEnt, pSrcItemDef, pSrcBag, iSrcSlot, pDstEnt, pDstBag, iDstSlot))
	{
		//  Now see if we are swapping with an item and check to see if we can move that item from the dest to the src

		// If we are dealing with equip bags. There are special cases
		if (iDstSlot < 0 && (invbag_flags(pDstBag) & (InvBagFlag_EquipBag|InvBagFlag_WeaponBag|InvBagFlag_ActiveWeaponBag|InvBagFlag_DeviceBag)))
		{
			// This replicates functionality inside the transaction in inventoryCommon.c
			S32 bCycleSecondary = false;
			iDstSlot = inv_bag_ItemMoveFindBestEquipSlot(pDstEnt, pDstBag, pSrcItemDef, true, &bCycleSecondary);
			if (iDstSlot < 0) {
				return false;
			} else if (bCycleSecondary) {
				// Secondary items will either go into an empty slot or one item will be bumped out into the inventory.  
				// No swapping is necessary
				return true;
			}
		}

		if (bItemSwap)
		{
			// Must move the entire stack when doing a swap. Otherwise it is invalid
			if (iCount==-1 || iCount == iSrcStackCount)
			{
				// Check the actual reverse move
				return item_ItemMoveValidInternal(pEnt, pDstItemDef, pDstBag, iDstSlot, pSrcEnt, pSrcBag, iSrcSlot);
			}
			return false;
		}
		//If not swapping
		return true;
	}
	
	return false;
}

bool item_ItemMoveValidWithCount(Entity *pEnt, ItemDef *pItemDef, int iCount, bool bSrcGuild, S32 iSrcBagID, S32 iSrcSlot, bool bDstGuild, S32 iDstBagID, S32 iDstSlot, GameAccountDataExtract *pExtract)
{
	Guild *pGuild = guild_GetGuild(pEnt);
	InventoryBag *pSrcBag = bSrcGuild ? inv_guildbank_GetBag(guild_GetGuildBank(pEnt), iSrcBagID) : (InventoryBag*)inv_GetBag(CONTAINER_NOCONST(Entity, pEnt), iSrcBagID, pExtract);
	InventoryBag *pDstBag = bDstGuild ? inv_guildbank_GetBag(guild_GetGuildBank(pEnt), iDstBagID) : (InventoryBag*)inv_GetBag(CONTAINER_NOCONST(Entity, pEnt), iDstBagID, pExtract);
	
	return item_ItemMoveValidInternalCommon(pEnt, pItemDef, iCount, pEnt, pSrcBag, iSrcSlot, pEnt, pDstBag, iDstSlot);
}

bool item_ItemMoveValid(Entity *pEnt, ItemDef *pItemDef, bool bSrcGuild, S32 iSrcBagID, S32 iSrcSlot, bool bDstGuild, S32 iDstBagID, S32 iDstSlot, GameAccountDataExtract *pExtract)
{
	S32 iCount = bSrcGuild ? inv_guildbank_GetSlotItemCount(pEnt, iSrcBagID, iSrcSlot) : inv_ent_GetSlotItemCount(pEnt, iSrcBagID, iSrcSlot, pExtract);
	return item_ItemMoveValidWithCount(pEnt, pItemDef, iCount, bSrcGuild, iSrcBagID, iSrcSlot, bDstGuild, iDstBagID, iDstSlot, pExtract);
}

bool item_ItemMoveValidAcrossSharedBank(Entity* pEnt, GameAccountDataExtract *pExtract)
{
	bool bValid = true;

	if (bValid)
		bValid = SharedBank_CanAccess(pEnt, pExtract);


	if (bValid && g_SharedBankConfig.bRequireContact)
		bValid = contact_IsNearSharedBank(pEnt);

	return bValid;
}

bool item_PlayerMatchesSharedBank(Entity *pEntity, Entity *pSharedBank)
{
	if(pEntity && pSharedBank)
	{
		if(pEntity->myEntityType == GLOBALTYPE_ENTITYPLAYER && pSharedBank->myEntityType == GLOBALTYPE_ENTITYSHAREDBANK &&
			pEntity->pPlayer && pEntity->pPlayer->accountID == pSharedBank->myContainerID)
		{

		}	return true;
	}

	return false;
}

//MULTIPLE ENTITY VERSION - item_ItemMoveValidWithCount
bool item_ItemMoveValidAcrossEntsWithCount(Entity* pEnt, Entity* pEntSrc, ItemDef* pItemDef, int iCount, bool bSrcGuild, S32 SrcBagID, S32 iSrcSlot, Entity* pEntDst, bool bDstGuild, S32 DestBagID, S32 iDestSlot, GameAccountDataExtract *pExtract)
{
	Guild *pGuild = guild_GetGuild(pEnt);
	InventoryBag* pSrcBag;
	InventoryBag* pDstBag;

	if (!pEnt) return false;

	// Make sure that the player can access the inventory of the source and destination entities
	if (!Entity_CanModifyPuppet(pEnt, pEntSrc) || !Entity_CanModifyPuppet(pEnt, pEntDst))
	{
		return false;
	}

	// If pet is being traded, then lock down its inventory
	if (trade_IsPetBeingTraded(pEntDst, pEntSrc))
	{
		return false;
	}

	if (entGetType(pEntSrc) == GLOBALTYPE_ENTITYSHAREDBANK
		|| entGetType(pEntDst) == GLOBALTYPE_ENTITYSHAREDBANK)
	{
		if (!item_ItemMoveValidAcrossSharedBank(pEnt, pExtract))
		{
			return false;
		}

		// check to make sure the correct shared bank is being used
		if(!item_PlayerMatchesSharedBank(pEnt, pEntSrc) && !item_PlayerMatchesSharedBank(pEnt, pEntDst))
		{
			return false;
		}
	}

	pSrcBag = bSrcGuild ? inv_guildbank_GetBag(guild_GetGuildBank(pEnt), SrcBagID) : (InventoryBag*)inv_GetBag(CONTAINER_NOCONST(Entity, pEntSrc), SrcBagID, pExtract);
	pDstBag = bDstGuild ? inv_guildbank_GetBag(guild_GetGuildBank(pEnt), DestBagID) : (InventoryBag*)inv_GetBag(CONTAINER_NOCONST(Entity, pEntDst), DestBagID, pExtract);
	return item_ItemMoveValidInternalCommon(pEnt, pItemDef, iCount, pEntSrc, pSrcBag, iSrcSlot, pEntDst, pDstBag, iDestSlot);
}

//MULTIPLE ENTITY VERSION - item_ItemMoveValid
bool item_ItemMoveValidAcrossEnts(Entity* pEnt, Entity* pEntSrc, ItemDef* pItemDef, bool bSrcGuild, S32 iSrcBagID, S32 iSrcSlot, Entity* pEntDst, bool bDstGuild, S32 iDstBagID, S32 iDstSlot, GameAccountDataExtract *pExtract)
{
	S32 iCount = inv_ent_GetSlotItemCount(pEntSrc, iSrcBagID, iSrcSlot, pExtract);
	return item_ItemMoveValidAcrossEntsWithCount(pEnt, pEntSrc, pItemDef, iCount, bSrcGuild, iSrcBagID, iSrcSlot, pEntDst, bDstGuild, iDstBagID, iDstSlot, pExtract);
}

bool item_GetAlgoIngredients(ItemDef *pRecipe, ItemCraftingComponent ***peaComponents, int iPowerGroup, U32 iLevel, ItemQuality eQuality)
{
	ItemCraftingTable *pTable = pRecipe ? pRecipe->pCraft : NULL;
	int i, j;
	
	if (pTable && pTable->ppPart) {
		S32 iCompListSize = eaSize(&pTable->ppPart);
		
		for (i = 0; i < iCompListSize; i++) {
			F32 fCount = 0;
			ItemCraftingComponent *pComp = pTable->ppPart[i];
			
			if (pComp->CountType == kComponentCountType_Fixed) {
				fCount = pComp->fCount;
			} else if (pComp->CountType == kComponentCountType_LevelAdjust) {
				fCount = pComp->fCount * g_CommonAlgoTables.ppCraftingCountAdjust[0]->level[iLevel-1];
			} else {
				ComponentCountTableEntry *pTableEntry;
				ComponentCountLevelEntry *pLevelEntry;
				ComponentCountQualityEntry *pQualityEntry;
				
				pTableEntry = eaGet(&g_CommonAlgoTables.ppComponentCountTableEntry, iPowerGroup);
				if (!pTableEntry) {
					return false;
				}
				pLevelEntry = eaGet(&pTableEntry->ppComponentCountLevelEntry, iLevel-1);
				if (!pLevelEntry) {
					return false;
				}
				pQualityEntry = eaGet(&pLevelEntry->ppComponentCountQualityEntry, eQuality);
				if (!pQualityEntry) {
					return false;
				}
				
				// Check if the component is required for this level band
				if (pComp && GET_REF(pComp->hItem) && !pComp->bDeconstructOnly && pComp->iMinLevel <= iLevel && pComp->iMaxLevel >= iLevel) {
					switch (pComp->CountType) {
						case kComponentCountType_Common1:
							fCount = pQualityEntry->ppCompCountEntry[0]->count;
							break;
						case kComponentCountType_Common2:
							fCount = pQualityEntry->ppCompCountEntry[1]->count;
							break;
						case kComponentCountType_UnCommon1:
							fCount = pQualityEntry->ppCompCountEntry[2]->count;
							break;
						case kComponentCountType_UnCommon2:
							fCount = pQualityEntry->ppCompCountEntry[3]->count;
							break;
						case kComponentCountType_Rare1:
							fCount = pQualityEntry->ppCompCountEntry[4]->count;
							break;
					}
				}
			}
			
			if (floor(fCount) > 0) {
				// Check if the component is already in the list
				for (j = eaSize(peaComponents)-1; j >= 0; j--) {
					if (GET_REF(pComp->hItem) == GET_REF((*peaComponents)[j]->hItem)) {
						(*peaComponents)[j]->fCount += fCount;
						break;
					}
				}
				if (j < 0) {
					ItemCraftingComponent *pNewComp = StructClone(parse_ItemCraftingComponent, pComp);
					pNewComp->fCount = fCount;
					eaPush(peaComponents, pNewComp);
				}
			}
		}
		return true;
	}
	return false;
}

void item_GetDeconstructionComponents(Item *pItem, ItemCraftingComponent ***peaComponents)
{
	ItemDef *pItemDef = pItem ? GET_REF(pItem->hItem) : NULL;
	ItemDef *pItemRecipeDef = pItemDef ? GET_REF(pItemDef->hCraftRecipe) : NULL;
	bool bFoundPowerRecipe = false;
	int i, iGroup;

	// ensure item exists and that it is not fused
	if (!pItem || (pItemDef && (pItemDef->flags & kItemDefFlag_Fused)))
		return;

	// get itempower ingredients for algo items
	if ((pItem->flags & kItemFlag_Algo) && pItem->pAlgoProps)
	{
		for (i = eaSize(&pItem->pAlgoProps->ppItemPowerDefRefs) - 1; i >= 0; i--)
		{
			ItemPowerDef *pPowerDef = GET_REF(pItem->pAlgoProps->ppItemPowerDefRefs[i]->hItemPowerDef);
			ItemDef *pPowerRecipeDef = pPowerDef ? GET_REF(pPowerDef->hCraftRecipe) : NULL;

			if (!pPowerRecipeDef)
				continue;

			bFoundPowerRecipe = item_GetAlgoIngredients(pPowerRecipeDef, peaComponents, pItem->pAlgoProps->ppItemPowerDefRefs[i]->iPowerGroup, item_GetMinLevel(pItem), item_GetQuality(pItem));
		}
	}

	// use deconstruction defaults if a recipe is not specified
	if (pItemDef && !pItemRecipeDef && !bFoundPowerRecipe)
	{
		for (i = 0; i < eaSize(&gItemDeconstructDefaults.peaSkillDefaults); i++)
		{
			if (gItemDeconstructDefaults.peaSkillDefaults[i]->eSkillType == pItemDef->kSkillType)
			{
				if (pItemDef->eType == kItemType_Upgrade && pItemDef->eRestrictSlotType == kSlotType_Primary)
					pItemRecipeDef = GET_REF(gItemDeconstructDefaults.peaSkillDefaults[i]->hPrimaryRecipe);
				else if (pItemDef->eType == kItemType_Upgrade && pItemDef->eRestrictSlotType == kSlotType_Secondary)
					pItemRecipeDef = GET_REF(gItemDeconstructDefaults.peaSkillDefaults[i]->hSecondaryRecipe);
				else if (pItemDef->eType == kItemType_Device)
					pItemRecipeDef = GET_REF(gItemDeconstructDefaults.peaSkillDefaults[i]->hDeviceRecipe);
				break;
			}
		}
	}

	// get ingredients for base item; use first allowed group as the group input for rarity-based component counts
	if (!pItemRecipeDef || pItemRecipeDef->Group == 0x00)
		iGroup = -1;
	else
		iGroup = log2_floor(pItemRecipeDef->Group);
	item_GetAlgoIngredients(pItemRecipeDef, peaComponents, iGroup, item_GetMinLevel(pItem), item_GetQuality(pItem));
}

TradeErrorType item_GetTradeError(Item *pItem)
{
	ItemDef *pDef;
	if (pItem && (pDef = GET_REF(pItem->hItem)))
	{
		if (pDef->eType == kItemType_Numeric)
		{
			if (!(pDef->flags & kItemDefFlag_Tradeable))
			{
				return kTradeErrorType_InvalidNumeric;
			}
		}
		else
		{
			if (pItem->flags & (kItemFlag_Bound | kItemFlag_BoundToAccount))
			{
				return kTradeErrorType_ItemBound;
			}
		}
		return kTradeErrorType_None;
	}
	return kTradeErrorType_InvalidItem;
}

bool item_CanTrade(Item *pItem)
{
	TradeErrorType eError = item_GetTradeError(pItem);
	return (eError == kTradeErrorType_None);
}

AUTO_TRANS_HELPER;
bool item_trh_CanRemoveItem(ATH_ARG NOCONST(Item)* pItem)
{
	if (NONNULL(pItem))
	{
		if (pItem->flags & (kItemFlag_SlottedOnAssignment | kItemFlag_TrainingFromItem))
		{
			return false;
		}
		return true;
	}
	return false;
}

// Item tags
AUTO_STARTUP(ItemTags);
void itemtags_Load(void)
{
	int i, iNumTags;
	const char *pchMessageFail = NULL;
	char *estrTemp = NULL;

	// Don't load on app servers, other than login server, auction server, queue server and group project server
	if (IsAppServerBasedType() && !IsTeamServer() && !IsLoginServer() && !IsAuctionServer() && !IsGuildServer() && !IsQueueServer() && !IsGroupProjectServer()) {
		return;
	}

	estrStackCreateSize(&estrTemp, 20);

	// load all item tag names from disk
	loadstart_printf("Loading ItemTags...");
	ParserLoadFiles(NULL, ITEM_TAGS_FILE, "ItemTags.bin", PARSER_OPTIONALFLAG, parse_ItemTagNames, &g_ItemTagNames);

	// TODO: support reloading?  I'm not sure tags are modified frequently enough to require this

	// create define context for the data-defined item tag enum values
	s_pDefineItemTags = DefineCreate();

	// add to the define context
	iNumTags = eaSize(&g_ItemTagNames.ppchName);
	for (i = 0; i < iNumTags; i++)
	{
		estrPrintf(&estrTemp, "%d", i + ItemTag_Any + 1);
		DefineAdd(s_pDefineItemTags, g_ItemTagNames.ppchName[i], estrTemp);
	}

	loadend_printf(" done (%d ItemTags).", iNumTags);
	estrDestroy(&estrTemp);

	// Load ItemCategories here as well
	// Load all ItemCategory names from disk
	loadstart_printf("Loading ItemCategories...");
	ParserLoadFiles(NULL, ITEM_CATEGORIES_FILE, "ItemCategories.bin", PARSER_OPTIONALFLAG, parse_ItemCategoryNames, &g_ItemCategoryNames);

	// Create define context for the data-defined ItemCategory enum values
	s_pDefineItemCategories = DefineCreate();

	// add to the define context
	iNumTags = eaSize(&g_ItemCategoryNames.ppInfo);
	for (i = 0; i < iNumTags; i++)
	{
		g_ItemCategoryNames.ppInfo[i]->eCategoryEnum = i + kItemCategory_FIRST_DATA_DEFINED;
		estrPrintf(&estrTemp, "%d", g_ItemCategoryNames.ppInfo[i]->eCategoryEnum);
		DefineAdd(s_pDefineItemCategories, g_ItemCategoryNames.ppInfo[i]->pchName, estrTemp);
	}

	loadend_printf(" done (%d ItemCategories).", iNumTags);
	estrDestroy(&estrTemp);
}

AUTO_STARTUP(ItemTagInfo) ASTRT_DEPS(ItemTags);
void Item_LoadItemTagInfo(void)
{
	S32 i;

	loadstart_printf("Loading ItemTagInfo...");

	ParserLoadFiles(NULL, "defs/config/ItemTagInfo.def","ItemTagInfo.bin", PARSER_OPTIONALFLAG, parse_ItemTagInfo, &gItemTagInfo);
	
	for(i = 0; i < eaSize(&gItemTagInfo.eItemTagData); ++i)
	{
		if(gItemTagInfo.eItemTagData[i]->esItemTagName)
		{
			S32 index = StaticDefineIntGetInt(ItemTagEnum, gItemTagInfo.eItemTagData[i]->esItemTagName);
			if(index >=0 )
			{
				gItemTagInfo.eItemTagData[i]->eItemTag = index;
				// no longer of any use
				estrDestroy(&gItemTagInfo.eItemTagData[i]->esItemTagName);
			}
		}
	}
	loadend_printf("done...");
}

bool Item_ItemTagRequiresSpecialization(ItemTag eItemTag)
{
	int i = 0;
	for(i = 0; i < eaSize(&gItemTagInfo.eItemTagData); i++)
	{
		if(gItemTagInfo.eItemTagData[i]->eItemTag == eItemTag) { 
			return gItemTagInfo.eItemTagData[i]->bSpecializationRequired;
		}
	}
	
	return false;
}

// Checks to see whether a device may be used by the player who owns it
// pEnt is the owner of pItem which resides in pBag
bool item_IsDeviceUsableByPlayer(Entity *pEnt, Item *pItem, InventoryBag *pBag) 
{
	ItemDef *pItemDef;
	S32 iPower;
	int NumPowers;

	if(!pEnt || !pItem || !pBag) {
		return false;
	}

	pItemDef = GET_REF(pItem->hItem);

	if (!pItemDef) {
		return false;
	}

	//Check to make sure the item is a device
	if(pItemDef->eType == kItemType_Device) {
		//If item is unequipped and the canUseUnequipped flag is not marked, then the item cannot be used.
		if(!invbag_def(pBag) || !(invbag_def(pBag)->flags & InvBagFlag_DeviceBag) && !(pItemDef->flags & kItemDefFlag_CanUseUnequipped)) {
			return false;
		}
	} else if(!(pItemDef->flags & kItemDefFlag_CanUseUnequipped)) {
		return false;
	}

	//Check to see if it has usable powers
	NumPowers = item_GetNumItemPowerDefs(pItem, true);
	if(NumPowers < 1) {
		return false;
	}

	for(iPower=NumPowers-1; iPower>=0; iPower--)
	{
		if(item_isPowerUsableByPlayer(pEnt, pItem, pBag, iPower)) {
			return true;
		}
	}

	return false;
}

// Is item power at index iPower usable by the player, pEnt
bool item_isPowerUsableByPlayer(Entity *pEnt, Item *pItem, InventoryBag *pBag, S32 iPower) {
	ItemPowerDef *pItemPowerDef;
	Power* ppow;
	PowerDef *pPowerDef;
	Character* pChar = NULL;
	int iCharLevelControlled = 0;
	int iLevelAdjustment = 0;
	int iMinItemLevel = item_GetMinLevel(pItem);
	ItemDef *pItemDef = pItem ? GET_REF(pItem->hItem) : NULL;

	if(!pEnt || !pItem || !pBag || !pItemDef) {
		return false;
	}

	//Make sure iPower is within bounds
	if(item_GetNumItemPowerDefs(pItem, true) < iPower + 1 || iPower < 0) {
		return false;
	}

	// Check to make sure that the player can actually use this item, if it's not in a special bag
	if (!(invbag_flags(pBag) & InvBagFlag_SpecialBag) && (!(pItemDef->flags & kItemDefFlag_CanUseUnequipped) ||
		!itemdef_VerifyUsageRestrictions(entGetPartitionIdx(pEnt), pEnt, pItemDef, iMinItemLevel, NULL, -1)))
	{
		return false;
	}

	//Get the power
	pItemPowerDef = item_GetItemPowerDef(pItem, iPower);
	ppow = item_GetPower(pItem, iPower);

	//Get level control information in case the power is level restricted
	pChar = pEnt->pChar;
	if(pChar->pLevelCombatControl)
	{
		iCharLevelControlled = pChar->iLevelCombat;
	}

	// If your combat level is being controlled, and the item requires a
	//  higher level, the item applies an appropriate negative adjustment
	//  to the level of the Powers that have levels.
	if(iCharLevelControlled && item_GetMinLevel(pItem) > iCharLevelControlled)
	{
		iLevelAdjustment = iCharLevelControlled - item_GetMinLevel(pItem);
	}

	if ( pItemPowerDef && ppow)
	{
		if( GET_REF(pItemPowerDef->hPower) )
		{
			pPowerDef = GET_REF(ppow->hDef);
			assert(pPowerDef);

			// Make sure the power isn't a local enhancement
			if(pPowerDef->eType != kPowerType_Enhancement || !(pItemPowerDef->flags&kItemPowerFlag_LocalEnhancement)) 
			{

				//make sure power is active (equipped or flagged)
				if ( item_ItemPowerActive(pEnt, pBag, pItem, iPower) )
				{	
					// If power starts at lvl 1 or higher, apply the level adjustment.
					// Return true if the level stays above 0.
					ppow->iLevelAdjustment = 0;
					if(ppow->iLevel)
					{
						if(ppow->iLevel+iLevelAdjustment > 0)
						{
							ppow->iLevelAdjustment = iLevelAdjustment;
							return true;
						}
					} else {
						return true;
					}
				}
			}
		}
	}
	return false;
}

AUTO_TRANS_HELPER_SIMPLE;
const char *item_GetLogString(const Item *pItem)
{
	static char pcItemDesc[1024] = "";
	ItemDef *pItemDef = pItem ? GET_REF(pItem->hItem) : NULL; 

	if (!pItemDef)
		sprintf(pcItemDesc, "[I(NULL)]");
	else if (pItemDef->eType == kItemType_Numeric)
		sprintf(pcItemDesc, "[I(#%s,%i,ID%"FORM_LL"u)]", pItemDef->pchName, pItem->count, pItem->id);
	else if (pItemDef->eType == kItemType_Component)
		sprintf(pcItemDesc, "[I(%s,ID%"FORM_LL"u)]", pItemDef->pchName, pItem->id);
	else
	{
		sprintf(pcItemDesc, "[I(%s,Q%i,L%i,ID%"FORM_LL"u)",
			pItemDef->pchName,
			item_GetQuality(pItem),
			item_GetLevel(pItem),
			pItem->id);
		if ((pItem->flags & kItemFlag_Algo) && pItem->pAlgoProps)
		{
			int i;

			strcat(pcItemDesc, ":");
			for (i = 0; i < eaSize(&pItem->pAlgoProps->ppItemPowerDefRefs); i++)
				strcatf(pcItemDesc, "%s%s", !i ? "" : ",", REF_STRING_FROM_HANDLE(pItem->pAlgoProps->ppItemPowerDefRefs[i]->hItemPowerDef));
		}
		strcat(pcItemDesc, "]");
	}

	return pcItemDesc;
}

ChatLinkInfo *item_CreateChatLinkInfoFromMessage(const char *pchMessage, const char *pchItemText, Item *pItem)
{
	const char *pchIndex = strstr(pchMessage, pchItemText);

	if (pchIndex)
	{
		ChatLinkInfo *pLinkInfo = NULL;
		ChatLink *pLink;
		char *estrEncodedItem = NULL;

		ParserWriteText(&estrEncodedItem, parse_Item, pItem, WRITETEXTFLAG_USEHTMLACCESSLEVEL | WRITETEXTFLAG_DONTWRITEENDTOKENIFNOTHINGELSEWRITTEN, 0, 0);
		pLink = ChatData_CreateLink(kChatLinkType_Item, estrEncodedItem, pchItemText);

		pLinkInfo = StructCreate(parse_ChatLinkInfo);
		pLinkInfo->pLink = pLink;
		pLinkInfo->iStart = pchIndex - pchMessage;
		pLinkInfo->iLength = (S32) strlen(pchItemText);
		
		estrDestroy(&estrEncodedItem);

		return pLinkInfo;
	}

	return NULL;
}

F32 item_GetLongestPowerCooldown(Item* pItem)
{
	int ii;
	int NumPowers = eaSize(&pItem->ppPowers);
	F32 recharge, highestCooldown = 0;
	for(ii=0; ii<NumPowers; ii++)
	{
		Power* pPower = pItem->ppPowers[ii];

		if (!pPower)
			continue;

		recharge = pPower->fTimeRecharge;
		if (recharge > highestCooldown)
			highestCooldown = recharge;
	}
	return highestCooldown;
}

bool item_MoveRequiresUniqueCheck(Entity *pSrcEnt, bool bSrcGuild, S32 iSrcBagID, S32 iSrcSlot, Entity *pDstEnt, bool bDstGuild, S32 iDstBagID, S32 iDstSlot, GameAccountDataExtract *pExtract)
{
	Item* pSrcItem = bSrcGuild ? inv_guildbank_GetItemFromBag(pSrcEnt, iSrcBagID, iSrcSlot) : inv_GetItemFromBag(pSrcEnt, iSrcBagID, iSrcSlot, pExtract);
	ItemDef* pSrcItemDef = pSrcItem ? GET_REF(pSrcItem->hItem) : NULL;
	Item* pDstItem = bDstGuild ? inv_guildbank_GetItemFromBag(pDstEnt, iDstBagID, iDstSlot) : inv_GetItemFromBag(pDstEnt, iDstBagID, iDstSlot, pExtract);
	ItemDef* pDstItemDef = pDstItem ? GET_REF(pDstItem->hItem) : NULL;

	// Are we moving items between pets or the same entity?
	if(pSrcEnt && pDstEnt && entity_IsOwnerSame(CONTAINER_NOCONST(Entity, pSrcEnt), CONTAINER_NOCONST(Entity, pDstEnt)) && !bSrcGuild && !bDstGuild)
	{
		return false;
	} 
	
	// Is the src item non-unique?
	if(pSrcItemDef && !(pSrcItemDef->flags & kItemDefFlag_Unique))
	{
		// Is the dst item non-unique?
		if(pDstItemDef && !(pDstItemDef->flags & kItemDefFlag_Unique))
		{
			return false;
		} else if(iDstSlot == -1 && iDstBagID > -1) {
			// If the dst slot isn't specified, is there an open slot in the dst bag?
			if(bDstGuild && (inv_bag_trh_AvailableSlots(ATR_EMPTY_ARGS, CONTAINER_NOCONST(Entity, pDstEnt), CONTAINER_NOCONST(InventoryBag, inv_guildbank_GetBag(guild_GetGuildBank(pDstEnt), iDstBagID))) > 0) ) {
				return false;
			} else if(inv_ent_AvailableSlots(pDstEnt, iDstBagID, pExtract) > 0) {
				return false;
			}		
		} else if(!pDstItem) {
			// Dst Slot is specified and empty
			return false;
		}
	}

	return true;
}

AUTO_TRANS_HELPER;
int item_trh_GetLevel(ATH_ARG NOCONST(Item)* pItem)
{
	if (NONNULL(pItem))
	{
		if (gConf.bNeverUseItemLevelOnItemInstance || ISNULL(pItem->pAlgoProps))
		{
			ItemDef* pItemDef = GET_REF(pItem->hItem);
			if (pItemDef)
			{
				return pItemDef->iLevel;
			}
		}
		else
		{
			return NONNULL(pItem->pAlgoProps) ? pItem->pAlgoProps->Level_UseAccessor : 0;
		}
	}
	return 0;
}

AUTO_TRANS_HELPER;
int item_trh_GetGemPowerLevel(ATH_ARG NOCONST(Item) *pHolder)
{
	ItemDef *pHolderDef = GET_REF(pHolder->hItem);
	int iLevel = item_trh_GetLevel(pHolder);
	int i;

	if(pHolderDef->flags & kItemDefFlag_LevelFromQuality && pHolder->pAlgoProps)
	{
		int iLevelToSet = item_trh_GetLevel(pHolder);
		AlgoItemLevelsDef *pAlgoItemLevels = eaIndexedGetUsingString(&g_CommonAlgoTables.ppAlgoItemLevels, StaticDefineIntRevLookup(ItemQualityEnum, pHolder->pAlgoProps->Quality));

		if(pAlgoItemLevels)
		{
			for(i=0;i<ALGOITEMLEVELSMAX;i++)
			{
				if(pAlgoItemLevels->level[i] > iLevelToSet)
					break;
			}

			iLevel = i;
		}
	}

	return iLevel;
}

AUTO_TRANS_HELPER;
int item_trh_GetMinLevel(ATH_ARG NOCONST(Item)* pItem)
{
	if (NONNULL(pItem))
	{
		if (gConf.bNeverUseItemLevelOnItemInstance || ISNULL(pItem->pAlgoProps))
		{
			ItemDef* pItemDef = GET_REF(pItem->hItem);
			int iMinLevel = 1;

			if (pItemDef && pItemDef->pRestriction)
			{
				iMinLevel = pItemDef->pRestriction->iMinLevel;
			}
			return MAX(iMinLevel, 1);
		}
		else
		{
			return NONNULL(pItem->pAlgoProps) ? pItem->pAlgoProps->MinLevel_UseAccessor : 0;
		}
	}
	return 0;
}

AUTO_TRANS_HELPER;
void item_trh_SetAlgoPropsLevel(ATH_ARG NOCONST(Item)* pItem, int iLevel)
{
	if (NONNULL(pItem) && !gConf.bNeverUseItemLevelOnItemInstance)
	{
		NOCONST(AlgoItemProps)* pProps = (NOCONST(AlgoItemProps)*)item_trh_GetOrCreateAlgoProperties(pItem);
		pProps->Level_UseAccessor = (S8)iLevel;
	}
}

AUTO_TRANS_HELPER;
void item_trh_SetAlgoPropsMinLevel(ATH_ARG NOCONST(Item)* pItem, int iLevel)
{
	if (NONNULL(pItem) && !gConf.bNeverUseItemLevelOnItemInstance)
	{
		NOCONST(AlgoItemProps)* pProps = (NOCONST(AlgoItemProps)*)item_trh_GetOrCreateAlgoProperties(pItem);
		pProps->MinLevel_UseAccessor = (S8)iLevel;
	}
}

AUTO_TRANS_HELPER;
void item_trh_SetCount(ATH_ARG NOCONST(Item)* pItem, int iCount)
{
	if (NONNULL(pItem))
	{
		pItem->count = iCount;
	}
}

AUTO_TRANS_HELPER;
ItemQuality item_trh_GetQuality(ATH_ARG NOCONST(Item)* pItem)
{
	ItemDef* pDef = NULL;
	if (ISNULL(pItem))
		return kItemQuality_None;
	if (NONNULL(pItem->pAlgoProps))
	{
		return pItem->pAlgoProps->Quality;
	}
	pDef = GET_REF(pItem->hItem);
	return pDef ? pDef->Quality : kItemQuality_None;
}

AUTO_TRANS_HELPER;
void item_trh_SetAlgoPropsQuality(ATH_ARG NOCONST(Item)* pItem, ItemQuality iQuality)
{
	if (NONNULL(pItem))
	{
		NOCONST(AlgoItemProps)* pProps = (NOCONST(AlgoItemProps)*)item_trh_GetOrCreateAlgoProperties(pItem);
		pProps->Quality = iQuality;
	}
}

AUTO_TRANS_HELPER;
int item_trh_GetPowerFactor(ATH_ARG NOCONST(Item)* pItem)
{
	ItemDef* pDef = NULL;
	if (ISNULL(pItem))
		return kItemQuality_None;
	if (NONNULL(pItem->pAlgoProps))
	{
		return pItem->pAlgoProps->iPowerFactor;
	}
	pDef = GET_REF(pItem->hItem);
	return pDef ? pDef->iPowerFactor : 0;
}

AUTO_TRANS_HELPER;
void item_trh_SetAlgoPropsPowerFactor(ATH_ARG NOCONST(Item)* pItem, int iPowerFactor)
{
	if (NONNULL(pItem))
	{
		NOCONST(AlgoItemProps)* pProps = (NOCONST(AlgoItemProps)*)item_trh_GetOrCreateAlgoProperties(pItem);
		pProps->iPowerFactor = iPowerFactor;
	}
}

void itemdef_DescClassesAllowed(char **pestrResult, ItemDef *pItemDef, CharacterClass *pClassMatch)
{
	if(pItemDef && pItemDef->pRestriction && 
		(eaSize(&pItemDef->pRestriction->ppCharacterClassesAllowed) || 
		eaiSize(&pItemDef->pRestriction->peClassCategoriesAllowed)))
	{
		const char* pchSeparator = TranslateMessageKey("CharacterClassCategory.Separator");
		U32 uiLenSep = pchSeparator ? (U32)strlen(pchSeparator) : 0;
		S32 iCategoryMatch = pClassMatch ? pClassMatch->eCategory : CharClassCategory_None;
		int i, iSize;
		S32 bMatch = !pClassMatch;
		S32 bAddSeparator = false;

		estrCreate(pestrResult);

		iSize = eaSize(&pItemDef->pRestriction->ppCharacterClassesAllowed);
		for(i=0; i<iSize; i++)
		{
			CharacterClassRef* pClassRef = pItemDef->pRestriction->ppCharacterClassesAllowed[i];
			CharacterClass* pClass = GET_REF(pClassRef->hClass);
			const char* pchClassName = pClass ? TranslateDisplayMessage(pClass->msgDisplayName) : NULL;
			if(pchClassName && *pchClassName)
			{
				if(uiLenSep && bAddSeparator)
					estrConcatString(pestrResult, pchSeparator, uiLenSep);
				FormatGameMessageKey(pestrResult,(pClassMatch==pClass)?"CharacterClassCategory.ClassName.Match":"CharacterClassCategory.ClassName",STRFMT_STRING("Category",pchClassName),STRFMT_END);
				bMatch |= (pClassMatch==pClass);
				bAddSeparator = true;
			}
		}

		iSize = eaiSize(&pItemDef->pRestriction->peClassCategoriesAllowed);
		for(i=0; i<iSize; i++)
		{
			S32 iCategory = pItemDef->pRestriction->peClassCategoriesAllowed[i];
			const char* pchClassCategory = StaticDefineGetTranslatedMessage(CharClassCategoryEnum, iCategory);
			if(pchClassCategory && *pchClassCategory)
			{
				if(uiLenSep && bAddSeparator)
					estrConcatString(pestrResult, pchSeparator, uiLenSep);
				FormatGameMessageKey(pestrResult,(iCategoryMatch==iCategory)?"CharacterClassCategory.ClassName.Match":"CharacterClassCategory.ClassName",STRFMT_STRING("Category",pchClassCategory),STRFMT_END);
				bMatch |= (iCategoryMatch==iCategory);
				bAddSeparator = true;
			}
		}

		if(!bMatch)
		{
			char *estrTemp = estrStackCreateFromStr(*pestrResult);
			estrClear(pestrResult);
			FormatGameMessageKey(pestrResult,"CharacterClasses.NoMatch",STRFMT_STRING("Classes",estrTemp),STRFMT_END);
			estrDestroy(&estrTemp);
		}
	}
}

void item_PrintDebugText(char ** pestrDevText,Entity * pEnt,Item * pItem,ItemDef * pDef)
{
	estrConcatf(pestrDevText,"<br>----Dev Info----<br>EP Value: %i<br>Item Level: %i<br>Def: %s",item_GetEPValue(entGetPartitionIdx(pEnt), pEnt, pItem),pDef->iLevel,pDef->pchName);
}

//This function creates a quick custom description.  Does not create auto description for non-innate powers.
// NOTE(JW): This is hardcoded to be NORMALIZED. If you don't know what that means,
//  or you need non-normalized descriptions, talk to Jered.
void item_GetNormalizedDescCustom(char** pestrResult, Entity* pEnt, 
								  Item *pItem, ItemDef* pItemDef,
								  const char* pchDescriptionKey, const char* pchPowers, const char* pchItemSetBonus, 
								  const char* pchInnateDescKey, const char* pchResourceType, const char* pchValueKey,
								  S32 iExchangeRate)
{
	/* Information currently available:
	* Item									{Item}
	* ItemDef								{ItemDef}
	* Item Name								{Name}
	* Item Description						{ItemDesc}
	* Item Quality Color					{ItemQuality}
	* Item Quality Enum Value				{ItemQualityInt}
	* Item Quality Translated Display Msg	{ITemQualityMsg}
	* Item Sort Type						{ItemSortType}
	* Current Durability					{ItemDurabilityCurrent}
	* Max Durability						{ItemDurabilityMax}
	* Current Durability Percentage			{ItemDurabilityPct}
	* Quest Item Message					{ItemQuest}
	* Item Bind Status						{ItemBind}
	* Number of Bag Slots					{ItemBagSlots}
	* Innate Mods							{ItemInnateMods}
	* Formatted Item Value					{FormattedItemValue}
	* Item Value							{ItemValue}
	* Consumed on Use						{ItemConsumedOnUse}		1=true 0=false
	* Primary Equip Bag						{EquipBag}
	* Item Tag								{ItemTag}
	*/

	if (pEnt)
	{
		ItemSortType* pSortType = NULL;
		const char* pchBindKey = "";
		const char* pchUniqueKey = "";
		const char* pchDescription = "";
		const char* pchDescShort = "";
		const char* pchExpKey = "";
		char* estrDev = NULL;
		char* estrQualityMsg = NULL;
		const char* pchEquipBag = NULL;
		int iBagSlots = 0;
		char* estrInnateMods = NULL;
		const char* pchUsageRestrictCategory = NULL;
		char* estrClassReqs = NULL;
		int iItemLevel = item_GetLevel(pItem);
		int iLevel = iItemLevel ? iItemLevel : (pEnt && pEnt->pChar) ? pEnt->pChar->iLevelCombat : 1;
		int iConsumedOnUse = 0;
		int iItemValue = 0;
		int iEquipLimit = 0;
		int iEquipLimitCategory = 0;
		const char* pchEquipLimitCategory = NULL;
		char* estrItemValue = NULL;
		Message* pUsageRestrictCategoryMsg = NULL;
		UsageRestrictionCategory eRestrictCategory;

		//ItemDef checks
		if(!pItemDef)
			pItemDef = pItem && IS_HANDLE_ACTIVE(pItem->hItem) ? GET_REF(pItem->hItem) : NULL;

		if(!pItemDef)
			return;

		//Sort Type
		pSortType = item_GetSortTypeForID(pItemDef->iSortID);

		//Bind
		if (pItem && (pItem->flags & kItemFlag_Bound)) 
		{
			pchBindKey = "Item.UI.Bound";
		} 
		else if (pItem && (pItem->flags & kItemFlag_BoundToAccount)) 
		{
			pchBindKey = "Item.UI.BoundToAccount";
		} 
		else if ((pItemDef->flags & kItemDefFlag_BindOnPickup) || (pItem && pItem->bForceBind))
		{
			pchBindKey = "Item.UI.BindOnPickup";
		} 
		else if (pItemDef->flags & kItemDefFlag_BindOnEquip)
		{
			pchBindKey = "Item.UI.BindOnEquip";
		}
		else if ((pItemDef->flags & kItemDefFlag_BindToAccountOnPickup) || (pItem && pItem->bForceBind))
		{
			pchBindKey = "Item.UI.BindToAccountOnPickup";
		} 
		else if (pItemDef->flags & kItemDefFlag_BindToAccountOnEquip)
		{
			pchBindKey = "Item.UI.BindToAccountOnEquip";
		}

		if(item_IsExperienceGift(pItemDef) && pItem && (pItem->flags & kItemFlag_Full) != 0)
		{
			pchExpKey = "Item.UI.ItemFull";
		}

		//Unique display text
		if (pItemDef->flags & kItemDefFlag_Unique)
		{
			pchUniqueKey = "Item.UI.Unique";
		}
		else if (pItemDef->flags & kItemDefFlag_UniqueEquipOnePerBag)
		{
			pchUniqueKey = "Item.UI.UniqueEquip";
		}

		// Equip Limit Count
		if (pItemDef->pEquipLimit)
		{
			ItemEquipLimitCategoryData* pEquipLimitCategory;
			ItemEquipLimitCategory eCategory = pItemDef->pEquipLimit->eCategory;	
			iEquipLimit = pItemDef->pEquipLimit->iMaxEquipCount;
			pEquipLimitCategory = item_GetEquipLimitCategory(pItemDef->pEquipLimit->eCategory);
			if (pEquipLimitCategory)
			{
				iEquipLimitCategory = pEquipLimitCategory->iMaxItemCount;
				pchEquipLimitCategory = TranslateDisplayMessage(pEquipLimitCategory->msgDisplayName);
			}
		}
	
		//Bag Slots
		if (pItemDef->eType == kItemType_Bag)
		{
			iBagSlots = pItemDef->iNumBagSlots;
		}


		//Description
		if(IS_HANDLE_ACTIVE(pItemDef->descriptionMsg.hMessage)) {
			pchDescription = TranslateDisplayMessage(pItemDef->descriptionMsg);
		}

		//Short Description
		if(IS_HANDLE_ACTIVE(pItemDef->descShortMsg.hMessage)) {
			pchDescShort = TranslateDisplayMessage(pItemDef->descShortMsg);
		}

		// Innate Mods and ConsumedOnUse
		if(pItem) {
			PowerDef** ppdefsInnate = NULL;
			F32 *pfScales = NULL;
			int numPowers = item_GetNumItemPowerDefs(pItem, true);
			int i;

			for(i=0; i<numPowers; i++)
			{
				Power *ppow = item_GetPower(pItem,i);
				PowerDef *pdef = item_GetPowerDef(pItem, i);
				ItemPowerDef *pItemPowerDef = item_GetItemPowerDef(pItem, i);

				if(pdef)
				{
					if(!ppow && pdef->eType==kPowerType_Innate) {
						eaPush(&ppdefsInnate,pdef);
						eafPush(&pfScales, item_GetItemPowerScale(pItem, i));
					}
					if(!iConsumedOnUse && pItemPowerDef && !(pItemPowerDef->flags&kItemPowerFlag_Rechargeable) && pdef->iCharges > 0) {
						iConsumedOnUse = 1;
					}
				}

			}
#if GAMESERVER || GAMECLIENT
			if(ppdefsInnate)
			{
				estrStackCreate(&estrInnateMods);
				powerdefs_AutoDescInnateMods(entGetPartitionIdx(pEnt), pItem, ppdefsInnate, pfScales, &estrInnateMods, pchInnateDescKey,NULL,NULL,iLevel,true, 0, entGetPowerAutoDescDetail(pEnt,false));
				eaDestroy(&ppdefsInnate);
				eafDestroy(&pfScales);
			}
#endif
		}

		// Item Quality Message

		estrStackCreate(&estrQualityMsg);
		if(pItem) {
			estrAppend2(&estrQualityMsg, StaticDefineGetTranslatedMessage(ItemQualityEnum, item_GetQuality(pItem)));
		} else {
			estrAppend2(&estrQualityMsg, StaticDefineGetTranslatedMessage(ItemQualityEnum, pItemDef->Quality));
		}

		//Dev
		if (g_bDisplayItemDebugInfo)
		{ 
			estrStackCreate(&estrDev);
			item_PrintDebugText(&estrDev,pEnt,pItem,pItemDef);
		}

		eRestrictCategory = SAFE_MEMBER2(pItemDef, pRestriction, eUICategory);

		if (eRestrictCategory != UsageRestrictionCategory_None &&
			(pUsageRestrictCategoryMsg = UsageRestriction_GetCategoryMessage(pEnt, eRestrictCategory)))
		{
			pchUsageRestrictCategory = entTranslateMessage(pEnt, pUsageRestrictCategoryMsg);
		}

		if (eaiSize(&pItemDef->peRestrictBagIDs))
		{
			InvBagCategory* pBagCategory = inv_GetBagCategoryByBagIDs(pItemDef->peRestrictBagIDs);
			if (pBagCategory)
			{
				pchEquipBag = TranslateDisplayMessage(pBagCategory->msgDisplayName);
			}
			else
			{
				pchEquipBag = StaticDefineGetTranslatedMessage(InvBagIDsEnum, eaiGet(&pItemDef->peRestrictBagIDs,0));
			}
		}

		itemdef_DescClassesAllowed(&estrClassReqs,pItemDef,pEnt&&pEnt->pChar?character_GetClassCurrent(pEnt->pChar):NULL);

		// Item value
		estrStackCreate(&estrItemValue);
		iItemValue = item_GetResourceValue(entGetPartitionIdx(pEnt), pEnt, pItem, pchResourceType);
		item_GetFormattedResourceString(langGetCurrent(), &estrItemValue, iItemValue, pchValueKey, iExchangeRate, false);

		FormatGameMessageKey(pestrResult, pchDescriptionKey,
			STRFMT_ITEM(pItem),
			STRFMT_ITEMDEF(pItemDef),
			STRFMT_STRING("ItemName", NULL_TO_EMPTY(pItem ? item_GetName(pItem, pEnt) : TranslateDisplayMessage(pItemDef->displayNameMsg))),
			STRFMT_STRING("ItemDesc", pchDescription),
			STRFMT_STRING("ItemDescShort", pchDescShort),
			STRFMT_STRING("ItemQuality", NULL_TO_EMPTY(StaticDefineIntRevLookup(ItemQualityEnum, pItem ? item_GetQuality(pItem) : pItemDef->Quality))),
			STRFMT_INT("ItemQualityInt", pItem ? item_GetQuality(pItem) : pItemDef->Quality),
			STRFMT_STRING("ItemQualityMsg", NULL_TO_EMPTY(estrQualityMsg)),
			STRFMT_STRING("ItemSortType", pSortType ? TranslateMessageRef(pSortType->hNameMsg) : ""),
			STRFMT_MESSAGEKEY("ItemQuest", item_IsMissionGrant(pItemDef) ? "Item.UI.QuestStart" : ""),
			STRFMT_MESSAGEKEY("ItemBind", NULL_TO_EMPTY(pchBindKey)),
			STRFMT_MESSAGEKEY("ItemUnique", NULL_TO_EMPTY(pchUniqueKey)),
			STRFMT_MESSAGEKEY("ItemExpFilled", NULL_TO_EMPTY(pchExpKey)),
			STRFMT_INT("ItemBagSlots", iBagSlots),
			STRFMT_STRING("FormattedItemValue", NULL_TO_EMPTY(estrItemValue)),
			STRFMT_INT("ItemValue", iItemValue),
			STRFMT_STRING("ItemPowers", NULL_TO_EMPTY(pchPowers)),
			STRFMT_STRING("ItemSet", NULL_TO_EMPTY(pchItemSetBonus)),
			STRFMT_STRING("ItemInnateMods", NULL_TO_EMPTY(estrInnateMods)),
			STRFMT_STRING("Category", NULL_TO_EMPTY(pchUsageRestrictCategory)),
			STRFMT_INT("CategoryIsRestricted", UsageRestriction_IsRestricted(pEnt, eRestrictCategory)),
			STRFMT_STRING("Classes", NULL_TO_EMPTY(estrClassReqs)),
			STRFMT_INT("ItemConsumedOnUse", iConsumedOnUse),
			STRFMT_STRING("EquipBag", NULL_TO_EMPTY(pchEquipBag)),
			STRFMT_STRING("EquipLimitCategoryName", NULL_TO_EMPTY(pchEquipLimitCategory)),
			STRFMT_INT("EquipLimitCategory", iEquipLimitCategory),
			STRFMT_INT("EquipLimit", iEquipLimit),
			STRFMT_STRING("ItemTag", StaticDefineGetTranslatedMessage(ItemTagEnum, pItemDef->eTag)),
			STRFMT_STRING("Dev", estrDev),
			STRFMT_END);

		if (estrDev)
			estrDestroy(&estrDev);
		if (estrInnateMods)
			estrDestroy(&estrInnateMods);
		if (estrClassReqs)
			estrDestroy(&estrClassReqs);
		if(estrQualityMsg)
			estrDestroy(&estrQualityMsg);
		if(estrItemValue)
			estrDestroy(&estrItemValue);
	}
}

// This converts the specified number of resources into three values based on the passed-in exchange rate, then formats this into a string using the format message key.
void item_GetFormattedResourceString(Language lang, char** pestrResult, int iResources, const char* pchValueFormatKey, S32 iExchangeRate, bool bShowZeros)
{
	char *pchFormat = NULL;
	int iExist = 3;
	S32 iCurrency1 = iResources % iExchangeRate;
	S32 iCurrency2 = (iResources / iExchangeRate) % iExchangeRate;
	S32 iCurrency3 = iResources / (iExchangeRate * iExchangeRate);
	const char* pchValueFormat = TranslateMessageKey(pchValueFormatKey);

	if (pchValueFormat && strchr(pchValueFormat, '^'))
	{
		char *pStart;
		char *pPos;
		char *pStop;
		strdup_alloca(pchFormat, pchValueFormat);
		pStart = pchFormat;
		while (pStart = strchr(pStart, '^'))
		{
			bool bRemove = false;
			pStop = strchr(pStart, '$');
			if (!pStop)
			{
				break;
			}

			if ((pPos = strstri(pStart, "{Currency1}")) && pPos < pStop)
			{
				bRemove = !iCurrency1 && !bShowZeros && (--iExist > 0);
			}
			else if ((pPos = strstri(pStart, "{Currency2}")) && pPos < pStop)
			{
				bRemove = !iCurrency2 && !bShowZeros && (--iExist > 0);
			}
			else if ((pPos = strstri(pStart, "{Currency3}")) && pPos < pStop)
			{
				bRemove = !iCurrency3 && !bShowZeros && (--iExist > 0);
			}

			if (bRemove)
			{
				memmove(pStart, pStop + 1, strlen(pStop + 1) + 1);
			}
			else 
			{
				memmove(pStart, pStart + 1, strlen(pStart + 1) + 1);
				pStop--;
				memmove(pStop, pStop + 1, strlen(pStop + 1) + 1);
				pStop--;
				pStart = pStop;
			}
		}
	}

	langFormatGameString(lang, pestrResult, pchFormat ? pchFormat : pchValueFormat,
		STRFMT_INT("Currency1", iCurrency1),
		STRFMT_INT("Currency2", iCurrency2),
		STRFMT_INT("Currency3", iCurrency3),
		STRFMT_END);
}

Message* UsageRestriction_GetCategoryMessage(Entity* pEnt, UsageRestrictionCategory eRestrictCategory)
{
	Message* pResultMsg = NULL;
	const char* pchCheckRank;
	//Hack to get an officer's rank from usage restriction category
	if (pEnt && eaSize(&g_OfficerRankStruct.eaRanks) > 0 && 
		eRestrictCategory != UsageRestrictionCategory_None &&
		(pchCheckRank = StaticDefineIntRevLookup(UsageRestrictionCategoryEnum, eRestrictCategory)) &&
		strStartsWith(pchCheckRank, "Rank"))
	{
		S32 iRank = atoi(pchCheckRank + 4);
		OfficerRankDef* pRankDef = Officer_GetRankDef(iRank, GET_REF(pEnt->hAllegiance), GET_REF(pEnt->hSubAllegiance));
		if (pRankDef && pRankDef->pDisplayMessage)
		{
			pResultMsg = GET_REF(pRankDef->pDisplayMessage->hMessage);
		}
	}
	else
	{
		pResultMsg = StaticDefineGetMessage(UsageRestrictionCategoryEnum, eRestrictCategory);
	}
	return pResultMsg;
}

bool UsageRestriction_IsRestricted(Entity* pEnt, UsageRestrictionCategory eRestrictCategory)
{
	const char* pchCheckRank;
	//Hack to get an officer's rank from usage restriction category
	if (pEnt && eaSize(&g_OfficerRankStruct.eaRanks) > 0 && 
		eRestrictCategory != UsageRestrictionCategory_None &&
		(pchCheckRank = StaticDefineIntRevLookup(UsageRestrictionCategoryEnum, eRestrictCategory)) &&
		strStartsWith(pchCheckRank, "Rank"))
	{
		S32 iRank = atoi(pchCheckRank + 4);
		S32 iEntRank = Officer_GetRank(pEnt);
		if (iEntRank < iRank)
		{
			return true;
		}
	}
	// Default to unrestricted
	return false;
}

int getItempowerCategoriesForItem(Item* pItem)
{
	int i;
	int ret = 0;
	ItemDef* pDef = GET_REF(pItem->hItem);
	for (i = 0; i < item_GetNumItemPowerDefs(pItem, true); i++)
	{
		ItemPowerDef* pExistingPowerDef = item_GetItemPowerDef(pItem, i);
		//if they share any categories, there's a conflict.
		if (pExistingPowerDef)
		{
			ret |= pExistingPowerDef->eItemPowerCategories;
		}
	}
	if (pDef)
	{
		for (i = 0; i < eaSize(&pDef->ppItemPowerDefRefs); i++)
		{
			ItemPowerDefRef* pPowerDefRef = pDef->ppItemPowerDefRefs[i];
			ItemPowerDef* pPowerDef = pPowerDefRef ? GET_REF(pPowerDefRef->hItemPowerDef) : NULL;
			if (pPowerDef)
			{
				ret |= pPowerDef->eItemPowerCategories;
			}
		}
	}
	return ret;
}

// Returns the first sort type category that contains the given sort type ID
SA_RET_OP_VALID const ItemSortTypeCategory * item_GetSortTypeCategoryBySortTypeID(U32 iSortTypeID)
{
	if (iSortTypeID > 0)
	{
		FOR_EACH_IN_CONST_EARRAY_FORWARDS(gSortTypes.ppItemSortTypeCategories, ItemSortTypeCategory, pItemSortTypeCategory)
		{
			S32 i;
			for (i = 0; i < ea32Size(&pItemSortTypeCategory->eaiItemSortTypes); i++)
			{
				if (pItemSortTypeCategory->eaiItemSortTypes[i] == iSortTypeID)
				{
					return pItemSortTypeCategory;
				}
			}
		}
		FOR_EACH_END
	}

	return NULL;
}

static ItemType* s_eaSafeItemTypes = NULL;

//Registers an item type as "safe" for granting from missions that never get returned and such. 
//This should ONLY be called on item types which are guaranteed to only ever be put in infinite-capacity bags, to prevent transaction weirdness.
void item_RegisterItemTypeAsAlwaysSafeForGranting(ItemType theType)
{
	eaiPush(&s_eaSafeItemTypes, theType);
}

bool item_IsUnsafeGrant(ItemDef * pDef)
{
	// this function considers all invisible items safe for granting, at this time
	return (pDef == NULL) || !(pDef->eType == kItemType_Callout || pDef->eType == kItemType_Lore || pDef->eType == kItemType_Title || pDef->eType == kItemType_Token || pDef->eType == kItemType_Boost || eaiFind(&s_eaSafeItemTypes, pDef->eType) > -1);
}

bool item_CanOpenRewardPack(Entity* pEnt, ItemDef* pItemDef, ItemDef** ppRequiredItemDef, char** pestrError)
{
	if (pEnt && pItemDef)
	{
		if (pItemDef->pRestriction)
		{
			char* pchErrorKey = NULL;
			if (!itemdef_VerifyUsageRestrictions(entGetPartitionIdx(pEnt), pEnt, pItemDef, 0, &pchErrorKey, -1))
			{
				if (pestrError)
				{
					entFormatGameMessageKey(pEnt, pestrError, pchErrorKey, STRFMT_ITEMDEF(pItemDef), STRFMT_END);
				}
				return false;
			}
		}
		if (pItemDef->pRewardPackInfo)
		{
			ItemDef* pRequiredItemDef = GET_REF(pItemDef->pRewardPackInfo->hRequiredItem);
		
			if (pRequiredItemDef)
			{
				S32 iRequiredCount = pItemDef->pRewardPackInfo->iRequiredItemCount;
				if (inv_FindItemCountByDefName(pEnt, pRequiredItemDef->pchName, iRequiredCount) < iRequiredCount)
				{
					if (ppRequiredItemDef)
					{
						(*ppRequiredItemDef) = pRequiredItemDef;
					}
					if (pestrError)
					{
						entFormatGameMessageKey(pEnt, pestrError, "Item.UI.RewardPack.RequiredItem", 
							STRFMT_ITEMDEF(pRequiredItemDef),
							STRFMT_ITEMDEF_KEY("RewardPackItem", pItemDef),
							STRFMT_INT("Count", iRequiredCount),
							STRFMT_END);
					}
					return false;
				}
			}
			return true;
		}
	}
	return false;
}

// enable extended item tooltip info
AUTO_COMMAND ACMD_ACCESSLEVEL(4) ACMD_CLIENTONLY ACMD_CMDLINE;
void itemDebug(bool bEnable)
{
	g_bDisplayItemDebugInfo = bEnable;
}

AUTO_TRANS_HELPER
ATR_LOCKS(pItem, ".pAlgoProps, .hItem");
NOCONST(AlgoItemProps)* item_trh_GetOrCreateAlgoProperties(ATH_ARG NOCONST(Item)* pItem)
{
	if (NONNULL(pItem) && ISNULL(pItem->pAlgoProps))
	{
		ItemDef* pDef = GET_REF(pItem->hItem);
		pItem->pAlgoProps = StructCreateNoConst(parse_AlgoItemProps);
		pItem->pAlgoProps->iPowerFactor = pDef->iPowerFactor;
		pItem->pAlgoProps->Quality = pDef->Quality;
		pItem->pAlgoProps->Level_UseAccessor = pDef->iLevel;
		pItem->pAlgoProps->MinLevel_UseAccessor = pDef->pRestriction ? pDef->pRestriction->iMinLevel : 0;
	}
	return NONNULL(pItem) ? pItem->pAlgoProps : NULL;
}

AUTO_TRANS_HELPER
ATR_LOCKS(pItem, ".pSpecialProps");
NOCONST(SpecialItemProps)* item_trh_GetOrCreateSpecialProperties(ATH_ARG NOCONST(Item)* pItem)
{
	if (NONNULL(pItem) && ISNULL(pItem->pSpecialProps))
		pItem->pSpecialProps = StructCreateNoConst(parse_SpecialItemProps);
	return NONNULL(pItem) ? pItem->pSpecialProps : NULL;
}

// Returns the weapon damage multiplier given the quality
F32 item_GetWeaponDamageMultiplierByQuality(ItemQuality eQuality)
{
	S32 iQualityInfoCount = eaSize(&g_ItemQualities.ppQualities);

	if (eQuality < 0 || eQuality >= iQualityInfoCount)
	{
		return 1.f;
	}
	return g_ItemQualities.ppQualities[eQuality]->fWeaponDamageMultiplier;
}

// Returns the chat link color name given the quality
const char* item_GetChatLinkColorNameByQuality(ItemQuality eQuality)
{
	S32 iQualityInfoCount = eaSize(&g_ItemQualities.ppQualities);
	ItemQualityInfo *pQualityInfo;
	if (eQuality < 0 || eQuality >= iQualityInfoCount)
	{
		return "white";
	}
	pQualityInfo = g_ItemQualities.ppQualities[eQuality];
	return pQualityInfo->pchChatLinkColorName ? pQualityInfo->pchChatLinkColorName : pQualityInfo->pchName;
}

InventorySlot *InvSlotRef_GetInventoySlot(Entity *pEnt, InventorySlotReference *pInvSlotRef, GameAccountDataExtract *pExtract)
{
	InventoryBag* pBag = NULL;
	if (pEnt && pInvSlotRef)
	{
		pBag = (InventoryBag*)inv_GetBag(CONTAINER_NOCONST(Entity, pEnt), pInvSlotRef->eBagID, pExtract);
	}
	return pBag ? inv_GetSlotPtr(pBag, pInvSlotRef->iIndex) : NULL;
}

Item* InvSlotRef_GetItem(Entity *pEnt, InventorySlotReference *pInvSlotRef, GameAccountDataExtract *pExtract)
{
	InventorySlot *pInvSlot = InvSlotRef_GetInventoySlot(pEnt, pInvSlotRef, pExtract);
	return SAFE_MEMBER(pInvSlot, pItem);
}

// returns true if the pTransmutateToItemDef has all the ppCharacterClassesAllowed that the pMainItemDef one has
// each itemDef must have CharacterClassesAllowed defined. False is returned otherwise
static bool itemdef_ContainsAllClassesAllowed(SA_PARAM_NN_VALID ItemDef *pMainItemDef, SA_PARAM_NN_VALID ItemDef *pOtherItemDef)
{
	if (pMainItemDef->pRestriction && pOtherItemDef->pRestriction)
	{
		// if our main has no restricted classes, make sure the thing we are transmuting to has none as well.
		if (eaSize(&pMainItemDef->pRestriction->ppCharacterClassesAllowed) == 0)
		{
			return eaSize(&pOtherItemDef->pRestriction->ppCharacterClassesAllowed) == 0;
		}

		FOR_EACH_IN_EARRAY(pMainItemDef->pRestriction->ppCharacterClassesAllowed, CharacterClassRef, mainClassRef)
		{
			CharacterClass *pMainClass = GET_REF(mainClassRef->hClass);
			bool bFound = false;
			FOR_EACH_IN_EARRAY(pOtherItemDef->pRestriction->ppCharacterClassesAllowed, CharacterClassRef, otherClassRef)
			{
				if (pMainClass == GET_REF(otherClassRef->hClass))
				{
					bFound = true;
					break;
				}
			}
			FOR_EACH_END

			if (!bFound)
				return false;
		}
		FOR_EACH_END
			
		return true;
	}

	return false;
}

// returns true if the pTransmutateToItemDef has all the ppCharacterClassesAllowed that the pMainItemDef one has
// each itemDef must have CharacterClassesAllowed defined. False is returned otherwise
static bool itemdef_ContainsAllCharacterPathsAllowed(SA_PARAM_NN_VALID ItemDef *pMainItemDef, SA_PARAM_NN_VALID ItemDef *pOtherItemDef)
{
	if (pMainItemDef->pRestriction && pOtherItemDef->pRestriction)
	{
		// if our main has no restricted classes, make sure the thing we are transmuting to has none as well.
		if (eaSize(&pMainItemDef->pRestriction->ppCharacterPathsAllowed) == 0)
		{
			return eaSize(&pOtherItemDef->pRestriction->ppCharacterPathsAllowed) == 0;
		}

		FOR_EACH_IN_EARRAY(pMainItemDef->pRestriction->ppCharacterPathsAllowed, CharacterPathRef, mainPathRef)
		{
			CharacterPath *pMainPath = GET_REF(mainPathRef->hPath);
			bool bFound = false;
			FOR_EACH_IN_EARRAY(pOtherItemDef->pRestriction->ppCharacterPathsAllowed, CharacterPathRef, otherPathRef)
			{
				if (pMainPath == GET_REF(otherPathRef->hPath))
				{
					bFound = true;
					break;
				}
			}
			FOR_EACH_END

			if (!bFound)
				return false;
		}
		FOR_EACH_END
			
		return true;
	}

	return false;
}


// Indicates whether the main item can transmutate into the other item
bool item_CanTransMutateTo(SA_PARAM_NN_VALID ItemDef *pMainItemDef, SA_PARAM_NN_VALID ItemDef *pTransmutateToItemDef)
{
	if (pMainItemDef == pTransmutateToItemDef)
	{
		return false;
	}

	// They must have the same item type
	if (pMainItemDef->eType != pTransmutateToItemDef->eType)
	{
		return false;
	}

	// the main def needs to have class restrictions
	if (!itemdef_ContainsAllClassesAllowed(pMainItemDef, pTransmutateToItemDef))
	{
		return false;
	}

	// the main def needs to have path restrictions
	if (!itemdef_ContainsAllCharacterPathsAllowed(pMainItemDef, pTransmutateToItemDef))
	{
		return false;
	}
	

	// They must have the same restrict bags
	if (ea32Size(&pMainItemDef->peRestrictBagIDs) != ea32Size(&pTransmutateToItemDef->peRestrictBagIDs) ||
		!itemdef_HasAllRestrictBagIDs(pMainItemDef, pTransmutateToItemDef->peRestrictBagIDs))
	{
		return false;
	}

	return true;
}

// Indicates whether the item can transmutate into another item
bool item_CanTransMutate(ItemDef *pItemDef)
{
	return pItemDef && 
		(pItemDef->eType == kItemType_Weapon || pItemDef->eType == kItemType_Upgrade) &&
		(IS_HANDLE_ACTIVE(pItemDef->hArt) || eaSize(&pItemDef->ppCostumes) > 0);
}

// Indicates whether the item can be dyed.
// This is a temporary measure until we get weapon textures pretty for dying.
bool item_CanDye(ItemDef *pItemDef)
{
	return pItemDef && 
		(pItemDef->eType == kItemType_Upgrade) &&
		(IS_HANDLE_ACTIVE(pItemDef->hArt) || eaSize(&pItemDef->ppCostumes) > 0);
}

// Function returns the transmutated item def if there is a transmutation on the item
SA_RET_OP_VALID ItemDef * item_GetTransmutation(SA_PARAM_NN_VALID Item * pItem)
{
	if (pItem->pSpecialProps && pItem->pSpecialProps->pTransmutationProps)
	{
		return GET_REF(pItem->pSpecialProps->pTransmutationProps->hTransmutatedItemDef);
	}
	return NULL;
}

const char* item_GetHeadshotStyleDef(Item* pItem)
{
	int i;
	ItemDef* pDef = SAFE_GET_REF(pItem, hItem);
	if (!pItem || !pDef)
		return NULL;

	for (i = 0; i < eaSize(&g_ItemConfig.eaHeadshotStyleConfigs); i++)
	{
		if ((g_ItemConfig.eaHeadshotStyleConfigs[i]->eRestrictBag <= 0 ||
			itemdef_HasRestrictBagID(pDef, g_ItemConfig.eaHeadshotStyleConfigs[i]->eRestrictBag)) &&
			(eaiSize(&g_ItemConfig.eaHeadshotStyleConfigs[i]->eaCategories) <= 0 ||
			itemdef_HasAllItemCategories(pDef, g_ItemConfig.eaHeadshotStyleConfigs[i]->eaCategories)))
			return g_ItemConfig.eaHeadshotStyleConfigs[i]->pchHeadshotStyleDef;
	}
	return NULL;
}

AUTO_TRANS_HELPER;
bool item_trh_IsUnidentified(ATH_ARG NOCONST(Item)* pItem)
{
	ItemDef* pDef = NONNULL(pItem) ? GET_REF(pItem->hItem) : NULL;

	if (NONNULL(pItem) && pDef && ((pDef->eType == kItemType_UnidentifiedWrapper) || (pItem->flags & kItemFlag_Unidentified_Unsafe)))
		return true;

	return false;
}

bool itemdef_IsUnidentified(ItemDef* pDef)
{
	if (pDef && ((pDef->eType == kItemType_UnidentifiedWrapper) || (pDef->flags & kItemDefFlag_Unidentified_Unsafe)))
		return true;

	return false;
}

#include "AutoGen/itemCommon_h_ast.c"
#include "AutoGen/itemEnums_h_ast.c"
