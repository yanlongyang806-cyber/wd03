/***************************************************************************
*     Copyright (c) 2008, Cryptic Studios
*     All Rights Reserved
*     Confidential Property of Cryptic Studios
***************************************************************************/

#include "ItemCommon.h"
#include "itemCommon_h_ast.h"
#include "ItemEnums.h"
#include "itemEnums_h_ast.h"
#include "UIGen.h"
#include "Expression.h"
#include "Mission_Common.h"
#include "CraftingUI_c_ast.h"
#include "GameAccountDataCommon.h"
#include "gclEntity.h"
#include "stringCache.h"
#include "Player.h"
#include "GlobalTypes.h"
#include "Autogen/GameServerLib_autogen_ServerCmdWrappers.h"
#include "qsortG.h"
#include "timing.h"
#include "EString.h"
#include "contact_common.h"


AUTO_STRUCT;
typedef struct ItemQualityLevel
{
	char *pcDisplayName;
	U32 iValue;
} ItemQualityLevel;

AUTO_STRUCT;
typedef struct CraftTagInfo
{
	ItemTag eTag;
	const char *pchDisplayName;				AST(UNOWNED)
	U32 iCount;
} CraftTagInfo;

AUTO_STRUCT;
typedef struct CraftRecipeInfo
{
	const char *pcInternalName;				AST(POOL_STRING)
	const char *pcIconName;					AST(POOL_STRING)
	const char *pcDisplayName;				AST(UNOWNED)
	const char *pcDescription;				AST(UNOWNED)
	const char *pcTargetDescription;				AST(UNOWNED)
	bool bIsAlgo;
	int iSkillRequirement;
} CraftRecipeInfo;

AUTO_STRUCT;
typedef struct CraftComponentInfo
{
	const char *pcName;						AST(UNOWNED)
	const char *pcDisplayName;				AST(UNOWNED)
	Item *pItem;
	U32 iNeed;
	U32 iHave;
} CraftComponentInfo;

AUTO_STRUCT;
typedef struct StaticCraftingData
{
	const char *pcBaseRecipeName;			AST(POOL_STRING)
	const char **eaPowerRecipeNames;		AST(POOL_STRING)
	ItemQuality eQuality;
	
	CraftComponentInfo **eaComponents;
	bool bComponentsDirty;

	// sorting parameters
	U32 uiSortingGroupMask;
	bool bSortingDescend;
} StaticCraftingData;

AUTO_STRUCT;
typedef struct CraftingSkillRange
{
	S32	iSkillMin;						AST(STRUCTPARAM)
	S32 iSkillMax;						AST(STRUCTPARAM)
	REF_TO(Message) hMessage;			AST(NAME(Message))
} CraftingSkillRange;

AUTO_STRUCT;
typedef struct CraftingSkillRangeSet
{
	CraftingSkillRange **eaSkillRanges;	AST(NAME(CraftingSkillRange))
} CraftingSkillRangeSet;

AUTO_ENUM;
typedef enum CraftingUISortOrder
{
	craftingui_Alphabetical = 0,
	craftingui_Skill,
	craftingui_Slot,
	craftingui_Tag,
} CraftingUISortOrder;

AUTO_STRUCT;
typedef struct SkillSubspec
{
	char *pchName; AST(POOL_STRING)
	int iMajorSkill;
	int iMinorSkill;

} SkillSubspec;

static StaticCraftingData s_LastCraftingData;
static StaticCraftingData s_CraftingData;
static StaticCraftingData *s_BufferedCraftingData = NULL;
static Item *s_CraftingPreviewItem;
static CraftingSkillRangeSet s_pCraftingSkillRanges;
static U32 s_iTimerIndex;

// holds the skillup skill level ranges fetched from the server; should have the minimum value for each range in the range table
static int *s_eaiCraftingSkillupRanges = NULL;

extern bool gbNoGraphics;

AUTO_RUN_ANON(memBudgetAddMapping(__FILE__, BUDGET_GameSystems););

/*****************
* HELPER FUNCTIONS
*****************/
static Item *craftingui_FindRecipeInBag(const char *pcInternalName, GameAccountDataExtract *pExtract)
{
	Entity *pEnt = entActivePlayerPtr();
	BagIterator *iter = invbag_IteratorFromEnt(pEnt,InvBagIDs_Recipe, pExtract);

	for (; !bagiterator_Stopped(iter); bagiterator_Next(iter))
	{
		Item *pItem = (Item*)bagiterator_GetItem(iter);
		ItemDef *pItemDef = bagiterator_GetDef(iter);

		if (pItemDef)
		{
			if (pItemDef->pchName == pcInternalName)
			{
				bagiterator_Destroy(iter);
				return pItem;
			}
		}
	}
	bagiterator_Destroy(iter);

	return NULL;
}

// This function converts a power group slot (0=primary, 1=secondary, 2=tertiary, etc) to the power group mask corresponding
// to the power groups of the base recipe def (i.e. the position of the 1st/2nd/3rd/etc set bit on the base
// recipe's Group value)
// eg: base recipe def's Group value = 0x0019
//     CraftBaseRecipeToPowerGroupMask(0) = 0x0001
//     CraftBaseRecipeToPowerGroupMask(2) = 0x0010
static U32 CraftBaseRecipeToPowerGroupMask(U32 uiPowerGroupSlot, GameAccountDataExtract *pExtract)
{
	Item *pBaseRecipeItem = craftingui_FindRecipeInBag(s_CraftingData.pcBaseRecipeName, pExtract);
	ItemDef *pBaseRecipeItemDef = SAFE_GET_REF(pBaseRecipeItem, hItem);
	int i;

	// no power group is returned if the base recipe def is not set or if it is not an algo item
	if (!pBaseRecipeItemDef || pBaseRecipeItemDef->Group == 0)
		return 0;
	
	for (i = 0; i < ITEM_POWER_GROUP_COUNT; i++)
	{
		U32 uiPowerGroupMask = (1 << i);
		if (uiPowerGroupMask & pBaseRecipeItemDef->Group)
		{
			if (uiPowerGroupSlot)
				uiPowerGroupSlot--;
			else
				return uiPowerGroupMask;
		}
	}

	return 0;
}

static int CraftComponentInfoCompareNeed(const CraftComponentInfo **ppInfo1, const CraftComponentInfo **ppInfo2)
{
	return (*ppInfo2)->iNeed - (*ppInfo1)->iNeed;
}

const char *craftingui_GetRecipeDisplayName(const ItemDef *pRecipeDef, U32 uiPowerGroupMask, GameAccountDataExtract *pExtract)
{
	if (pRecipeDef)
	{
		if (pRecipeDef->eType == kItemType_ItemRecipe)
			return item_GetDefLocalName(pRecipeDef, 0);
		else if (pRecipeDef->eType == kItemType_ItemPowerRecipe)
		{
			Item *pBaseRecipe = craftingui_FindRecipeInBag(s_CraftingData.pcBaseRecipeName,pExtract);
			ItemDef *pBaseRecipeDef = pBaseRecipe ? GET_REF(pBaseRecipe->hItem) : NULL;
			ItemPowerDef *pProduct = GET_REF(pRecipeDef->pCraft->hItemPowerResult);

			if (pProduct)
			{
				int iGroup = uiPowerGroupMask > 0 ? log2_floor(uiPowerGroupMask) : -1;
				if (pBaseRecipeDef && iGroup >= 0)
				{

					// These funky bitwise operators result in the first iGroup bits being set
					// to 1, and the rest to 0. This allows me to mask out the flags for iGroup
					// and all higher groups, to check if there are any groups below iGroup
					// which are allowed on this recipe. Since the first item power uses the
					// noun version of it's display name, and the rest use the adjective
					// version, we need to know whether or not this is the first item power.
					if ((~(0xFF << iGroup) & pBaseRecipeDef->Group) == 0)
						return TranslateDisplayMessage(pProduct->displayNameMsg);
					else
						return TranslateDisplayMessage(pProduct->displayNameMsg2);
				}
				else
					return TranslateDisplayMessage(pProduct->displayNameMsg);
			}
		}
	}

	return "";
}

static int craftingui_CraftTagInfoCmp(const CraftTagInfo **ppTagInfo1, const CraftTagInfo **ppTagInfo2)
{
	return (*ppTagInfo1)->iCount - (*ppTagInfo2)->iCount;
}

// Executes crafting of selected item
static void CraftingExecute(void)
{
	Entity *pEnt = entActivePlayerPtr();

	if (s_BufferedCraftingData &&
		pEnt && pEnt->pPlayer &&
		pEnt->pPlayer->InteractStatus.bInteracting &&
		pEnt->pPlayer->pInteractInfo &&
		pEnt->pPlayer->pInteractInfo->eCraftingTable != kSkillType_None &&
		(pEnt->pPlayer->pInteractInfo->eCraftingTable & pEnt->pPlayer->SkillType))
	{
		GameAccountDataExtract *pExtract;
		Item *pBaseRecipe = NULL;
		ItemDef *pBaseRecipeDef = NULL;

		if (!s_BufferedCraftingData->pcBaseRecipeName || !s_BufferedCraftingData->pcBaseRecipeName[0]) {
			return;
		}

		pExtract = entity_GetCachedGameAccountDataExtract(pEnt);
		pBaseRecipe = craftingui_FindRecipeInBag(s_BufferedCraftingData->pcBaseRecipeName, pExtract);

		pBaseRecipeDef = pBaseRecipe ? GET_REF(pBaseRecipe->hItem) : NULL;
		if (!pBaseRecipeDef) {
			return;
		}

		if (pBaseRecipeDef->Group == 0x00)
			ServerCmd_item_CraftCustom(s_BufferedCraftingData->pcBaseRecipeName);
		else
		{
			CraftData *pCraftData;
			int i;

			pCraftData = StructAlloc(parse_CraftData);
			pCraftData->eQuality = s_BufferedCraftingData->eQuality;
			pCraftData->pcBaseItemRecipeName = StructAllocString(s_BufferedCraftingData->pcBaseRecipeName);
			for (i = 0; i < eaSize(&s_BufferedCraftingData->eaPowerRecipeNames); i++)
				eaPush(&pCraftData->eaItemPowerRecipes, StructAllocString(s_BufferedCraftingData->eaPowerRecipeNames[i]));

			ServerCmd_item_CraftAlgo(pCraftData);
			StructDestroy(parse_CraftData, pCraftData);
		}
	}
	StructDestroySafe(parse_StaticCraftingData, &s_BufferedCraftingData);
}

/*****
* MAIN
*****/
AUTO_STARTUP(CraftingUI) ASTRT_DEPS(InventoryUI CraftingSkillRanges);
void craftingUIInit(void)
{
}

AUTO_EXPR_FUNC(UIGen) ACMD_NAME(craft_ResetCraftingData);
void craftingui_ResetCraftingData(void)
{
	StructReset(parse_StaticCraftingData, &s_CraftingData);
	StructDestroySafe(parse_StaticCraftingData, &s_BufferedCraftingData);
	StructDestroySafe(parse_Item, &s_CraftingPreviewItem);
}

AUTO_EXPR_FUNC(UIGen) ACMD_NAME(craft_SetBaseRecipe);
void craftingui_SetBaseRecipe(const char *pcRecipeName)
{
	if (pcRecipeName != s_CraftingData.pcBaseRecipeName)
		eaClear(&s_CraftingData.eaPowerRecipeNames);
	s_CraftingData.pcBaseRecipeName = allocAddString(pcRecipeName);
	s_CraftingData.bComponentsDirty = true;
}

AUTO_EXPR_FUNC(UIGen) ACMD_NAME(craft_SetPowerRecipe);
void craftingui_SetPowerRecipe(const char *pcRecipeName, U32 uiPowerGroupSlot)
{
	GameAccountDataExtract *pExtract = NULL; // No entity
	U32 uiPowerGroupMask = CraftBaseRecipeToPowerGroupMask(uiPowerGroupSlot,pExtract);
	int iPowerRecipeNameIdx = 0;

	// return if the base recipe doesn't support the power being set
	if (uiPowerGroupMask == 0)
		return;

	// expand power recipe name list to have at least one entry for each bit below
	// the 1-bit in the power group mask
	do
	{
		iPowerRecipeNameIdx++;
		if (eaSize(&s_CraftingData.eaPowerRecipeNames) < iPowerRecipeNameIdx)
			eaPush(&s_CraftingData.eaPowerRecipeNames, NULL);
		uiPowerGroupMask = uiPowerGroupMask >> 1;
	} while (uiPowerGroupMask != 0);

	// set the power recipe to the appropriate power group index
	if (eaSize(&s_CraftingData.eaPowerRecipeNames) > iPowerRecipeNameIdx - 1)
		s_CraftingData.eaPowerRecipeNames[iPowerRecipeNameIdx - 1] = allocAddString(pcRecipeName);

	s_CraftingData.bComponentsDirty = true;
}

AUTO_EXPR_FUNC(UIGen) ACMD_NAME(craft_SetQuality);
void craftingui_SetQuality(const char *pcQuality)
{
	s_CraftingData.eQuality = StaticDefineIntGetInt(ItemQualityEnum, pcQuality);
	s_CraftingData.bComponentsDirty = true;
}

AUTO_EXPR_FUNC(UIGen) ACMD_NAME(craft_SetQualityVal);
void craftingui_SetQualityVal(int Quality)
{
	//assuming that Quality passed in is a number from 0 to the number of quality enums -1
	//this assumes that the quality enum is ordered starting at kItemQuality_White through kItemQuality_Purple
	//requested to be like this by David M.

	s_CraftingData.eQuality = min(Quality, g_ItemQualities.iMaxStandardQuality);
	s_CraftingData.bComponentsDirty = true;
}

AUTO_EXPR_FUNC(UIGen) ACMD_NAME(craft_GetAlgoQuality);
const char *craftingui_GetAlgoQuality(void)
{
	return StaticDefineIntRevLookup(ItemQualityEnum, s_CraftingData.eQuality);
}

AUTO_EXPR_FUNC(UIGen) ACMD_NAME(craft_GetAlgoQualityVal);
int craftingui_GetAlgoQualityVal(void)
{
	return s_CraftingData.eQuality;
}

AUTO_EXPR_FUNC(UIGen) ACMD_NAME(craft_GetCustomQuality);
const char *craftingui_GetCustomQuality(void)
{
	GameAccountDataExtract *pExtract = NULL; // No entity
	Item *pBaseRecipe = craftingui_FindRecipeInBag(s_CraftingData.pcBaseRecipeName, pExtract);
	ItemDef *pBaseRecipeDef = pBaseRecipe ? GET_REF(pBaseRecipe->hItem) : NULL;
	if (pBaseRecipeDef) {
		return StaticDefineIntRevLookup(ItemQualityEnum, pBaseRecipeDef->Quality);
	}
	return StaticDefineIntRevLookup(ItemQualityEnum, 0);
}

AUTO_EXPR_FUNC(UIGen) ACMD_NAME(craft_GetCustomQualityVal);
int craftingui_GetCustomQualityVal(void)
{
	GameAccountDataExtract *pExtract = NULL; // No entity
	Item *pBaseRecipe = craftingui_FindRecipeInBag(s_CraftingData.pcBaseRecipeName, pExtract);
	ItemDef *pBaseRecipeDef = pBaseRecipe ? GET_REF(pBaseRecipe->hItem) : NULL;
	if (pBaseRecipeDef) {
		return pBaseRecipeDef->Quality;
	}
	return 0;
}

AUTO_EXPR_FUNC(UIGen) ACMD_NAME(craft_PowerGroupIsActive);
bool craftingui_PowerGroupIsActive(int iPowerGroupSlot)
{
	GameAccountDataExtract *pExtract = NULL; // No entity
	U32 iPowerGroup = CraftBaseRecipeToPowerGroupMask(iPowerGroupSlot, pExtract);
	return !!iPowerGroup;
}

AUTO_EXPR_FUNC(UIGen) ACMD_NAME(craft_IsAlgo);
bool craftingui_IsAlgo(void)
{
	if (s_CraftingData.pcBaseRecipeName) {
		GameAccountDataExtract *pExtract = NULL; // No entity
		Item *pBaseRecipe = craftingui_FindRecipeInBag(s_CraftingData.pcBaseRecipeName, pExtract);
		ItemDef *pBaseRecipeDef = pBaseRecipe ? GET_REF(pBaseRecipe->hItem) : NULL;
		
		if (pBaseRecipeDef) {
			return pBaseRecipeDef->Group != 0x00;
		}
	}
	return false;
}

AUTO_EXPR_FUNC(UIGen);
int CraftingTableGetSkillMax(SA_PARAM_OP_VALID Entity *pEnt)
{
	if (!pEnt || !pEnt->pPlayer)
		return 0;
	else if (pEnt->pPlayer->InteractStatus.bInteracting && pEnt->pPlayer->pInteractInfo && pEnt->pPlayer->pInteractInfo->bCrafting)
		return pEnt->pPlayer->pInteractInfo->iCraftingMaxLevel;
	else
		return 0;
}

AUTO_EXPR_FUNC(UIGen);
U32 CraftingTableGetSkillType(SA_PARAM_OP_VALID Entity *pEnt)
{
	if (!pEnt || !pEnt->pPlayer)
		return 0;
	else if (pEnt->pPlayer->InteractStatus.bInteracting && pEnt->pPlayer->pInteractInfo && pEnt->pPlayer->pInteractInfo->bCrafting)
		return pEnt->pPlayer->pInteractInfo->eCraftingTable;
	else
		return 0;
}

// Returns whether the player is allowed to use the crafting table he is currently interacting with
AUTO_EXPR_FUNC(UIGen);
bool CraftingTableUsable(SA_PARAM_OP_VALID Entity *pEnt)
{
	return (pEnt && pEnt->pPlayer &&
		pEnt->pPlayer->InteractStatus.bInteracting &&
		pEnt->pPlayer->pInteractInfo &&
		pEnt->pPlayer->pInteractInfo->bCrafting &&
		pEnt->pPlayer->pInteractInfo->eCraftingTable != kSkillType_None &&
		(pEnt->pPlayer->pInteractInfo->eCraftingTable & pEnt->pPlayer->SkillType));
}

AUTO_EXPR_FUNC(UIGen) ACMD_NAME("CanCraft");
bool CraftingCanCraft(void)
{
	Entity *pEnt = entActivePlayerPtr();
	S32 i;
	Item *pBaseRecipe;
	ItemDef *pBaseRecipeDef;
	//ItemDef *pItemResult;
	int iTableSkillMax = CraftingTableGetSkillMax(pEnt);
	int iPlayerSkill = inv_GetNumericItemValue(pEnt, "Skilllevel");
	GameAccountDataExtract *pExtract;
	
	if (!s_CraftingData.pcBaseRecipeName || !s_CraftingData.pcBaseRecipeName[0]) {
		return false;
	}

	for (i = eaSize(&s_CraftingData.eaComponents)-1; i >= 0; i--) {
		if (s_CraftingData.eaComponents[i]->iHave < s_CraftingData.eaComponents[i]->iNeed) {
			return false;
		}
	}

	pExtract = entity_GetCachedGameAccountDataExtract(pEnt);
	pBaseRecipe = craftingui_FindRecipeInBag(s_CraftingData.pcBaseRecipeName, pExtract);
	pBaseRecipeDef = pBaseRecipe ? GET_REF(pBaseRecipe->hItem) : NULL;

	if (!pBaseRecipeDef)
		return false;
	if (pBaseRecipeDef->pRestriction && iTableSkillMax >= 0 && (U32) iTableSkillMax < pBaseRecipeDef->pRestriction->iSkillLevel)
		return false;
	if (pBaseRecipeDef->pRestriction && (U32) iPlayerSkill < pBaseRecipeDef->pRestriction->iSkillLevel)
		return false;

	// Check the resulting item's tag against the player's specialization.
	// Commented out because there seems to be a lot of back-and-forth about having this restriction or not.
	//if (pItemResult = GET_REF(pBaseRecipeDef->pCraft->hItemResult))
	//	if (pItemResult && !entity_CraftingCheckTag(pEnt, pItemResult->eTag))
	//		return false;

	for (i = 0; i < ITEM_POWER_GROUP_COUNT; i++) {
		if ((pBaseRecipeDef->Group & (1<<i)) != 0) {
			if (eaSize(&s_CraftingData.eaPowerRecipeNames) < (i+1) || !s_CraftingData.eaPowerRecipeNames[i] || !s_CraftingData.eaPowerRecipeNames[i][0]) {
				return false;
			}
		}
	}
	return true;
}

static bool craftingui_ItemDefMatchesFilter(Entity *pEnt, ItemDef *pRecipeItemDef, const char *pchTextFilter, ItemType eRecipeType, ItemType eItemType, SkillType eSkillType, InvBagIDs eItemBagID, SlotType eSlotType, U32 uiPowerGroupMask, ItemTag eTag, bool bFilterCraftable, GameAccountDataExtract *pExtract)
{
	const char *pchRecipeName;

	// check for correct recipe type
	if (pRecipeItemDef->eType != eRecipeType)
		return false;

	// check for correct skill type on the recipe
	if ((eSkillType != kSkillType_None ) && (pRecipeItemDef->kSkillType != eSkillType))
		return false;

	if (eRecipeType == kItemType_ItemRecipe) 
	{
		ItemDef *pBaseItemDef = pRecipeItemDef ? GET_REF(pRecipeItemDef->pCraft->hItemResult) : NULL;

		if (!pBaseItemDef)
			return false;

		// check for the correct tag
		if (eTag != ItemTag_Any && eTag != pBaseItemDef->eTag)
			return false;

		// check for the correct item type on the result item
		if ((eItemType != kItemType_None) && (eItemType != pBaseItemDef->eType))
			return false;

		// check for correct BagID on the result item
		if ((eItemBagID != InvBagIDs_None) && (eaiFind(&pBaseItemDef->peRestrictBagIDs, eItemBagID) < 0))
			return false;

		// check for the correct upgrade type on the result item
		if ((eSlotType != kSlotType_Any) && (eSlotType != pBaseItemDef->eRestrictSlotType))
			return false;

		// check whether player has the requisite components (always include algo items)
		if (bFilterCraftable && pRecipeItemDef->Group == 0x00)
		{		
			ItemCraftingComponent **peaComponents = NULL;
			int i;

			item_GetAlgoIngredients(pRecipeItemDef, &peaComponents, 0, pRecipeItemDef->iLevel, 0);
			for (i = 0; i < eaSize(&peaComponents); i++)
			{
				ItemDef *pItemDef = GET_REF(peaComponents[i]->hItem);
				if (pItemDef)
				{
					if (inv_ent_AllBagsCountItems(pEnt, pItemDef->pchName) < floor(peaComponents[i]->fCount))
					{
						eaDestroyStruct(&peaComponents, parse_ItemCraftingComponent);
						return false;
					}
				}
			}
			eaDestroyStruct(&peaComponents, parse_ItemCraftingComponent);
		}
	}
	else if (eRecipeType == kItemType_ItemPowerRecipe) 
	{
		Item *pBaseRecipe = craftingui_FindRecipeInBag(s_CraftingData.pcBaseRecipeName, pExtract);
		ItemDef *pBaseRecipeDef = pBaseRecipe ? GET_REF(pBaseRecipe->hItem) : NULL;
		ItemDef *pBaseItemDef = pBaseRecipeDef ? GET_REF(pBaseRecipeDef->pCraft->hItemResult) : NULL;
		//ItemDef *pItemPowerDef = pRecipeItemDef ? GET_REF(pBaseRecipeDef->pCraft->hItemPowerResult) : NULL;

		if (!pBaseItemDef)
			return false;

		// make sure that the item power BagID matches the base Item BagID
		if (!(eaiSize(&pRecipeItemDef->peRestrictBagIDs)==0 ||
			  eaiFind(&pRecipeItemDef->peRestrictBagIDs, eaiGet(&pBaseItemDef->peRestrictBagIDs,0)) >= 0))
			return false;

		// This item power recipe isn't associated with this group
		if ((pRecipeItemDef->Group & uiPowerGroupMask) == 0)
			return false;
	}
	else
	{
		//invalid recipe type
		return false;
	}

	// check for text filter match
	pchRecipeName = craftingui_GetRecipeDisplayName(pRecipeItemDef, uiPowerGroupMask, pExtract);
	if (strstri(pchRecipeName, pchTextFilter) == 0)
		return false;

	return true;
}

// Sorting
static int craftingui_ItemDefCmpAlphaMain(const ItemDef *pRecipeDef1, const ItemDef *pRecipeDef2, U32 uiPowerGroupMask, bool bDescending)
{
	const char *pchName1 = craftingui_GetRecipeDisplayName(pRecipeDef1, uiPowerGroupMask, NULL);
	const char *pchName2 = craftingui_GetRecipeDisplayName(pRecipeDef2, uiPowerGroupMask, NULL);

	return bDescending ? strcmpi(pchName2, pchName1) : strcmpi(pchName1, pchName2);
}

static int craftingui_ItemDefCmpSkillMain(const ItemDef *pRecipeDef1, const ItemDef *pRecipeDef2, bool bDescending)
{
	int iSkill1 = pRecipeDef1->pRestriction ? pRecipeDef1->pRestriction->iSkillLevel : 0;
	int iSkill2 = pRecipeDef1->pRestriction ? pRecipeDef2->pRestriction->iSkillLevel : 0;
	return bDescending ? iSkill2 - iSkill1 : iSkill1 - iSkill2;
}

static int craftingui_ItemDefCmpSlotMain(const ItemDef *pRecipeDef1, const ItemDef *pRecipeDef2, bool bDescending)
{
	ItemDef *pItemDef1 = pRecipeDef1 && pRecipeDef1->pCraft ? GET_REF(pRecipeDef1->pCraft->hItemResult) : NULL;
	ItemDef *pItemDef2 = pRecipeDef2 && pRecipeDef2->pCraft ? GET_REF(pRecipeDef2->pCraft->hItemResult) : NULL;

	if (pItemDef1 && pItemDef2)
		return bDescending ? pItemDef2->eRestrictSlotType - pItemDef1->eRestrictSlotType : pItemDef1->eRestrictSlotType - pItemDef2->eRestrictSlotType;
	else
		return 0;
}

static int craftingui_ItemDefCmpTagMain(const ItemDef *pRecipeDef1, const ItemDef *pRecipeDef2, bool bDescending)
{
	ItemDef *pItemDef1 = pRecipeDef1 && pRecipeDef1->pCraft ? GET_REF(pRecipeDef1->pCraft->hItemResult) : NULL;
	ItemDef *pItemDef2 = pRecipeDef2 && pRecipeDef2->pCraft ? GET_REF(pRecipeDef2->pCraft->hItemResult) : NULL;

	if (pItemDef1 && pItemDef2)
	{
		const char *pchTag1 = StaticDefineGetTranslatedMessage(ItemTagEnum, pItemDef1->eTag);
		const char *pchTag2 = StaticDefineGetTranslatedMessage(ItemTagEnum, pItemDef2->eTag);

		return bDescending ? strcmpi(pchTag2, pchTag1) : strcmpi(pchTag1, pchTag2);
	}
	else
		return 0;
}

// used when sorting alphabetically
static int craftingui_ItemDefCmpAlpha(const ItemDef **pRecipeDef1, const ItemDef **pRecipeDef2)
{
	int iRet = craftingui_ItemDefCmpAlphaMain(*pRecipeDef1, *pRecipeDef2, s_CraftingData.uiSortingGroupMask, s_CraftingData.bSortingDescend);

	// no need for secondary sorting, as all text should be unique
	return iRet;
}

// used when sorting by recipe skill level
static int craftingui_ItemDefCmpSkill(const ItemDef **pRecipeDef1, const ItemDef **pRecipeDef2)
{
	int iRet = craftingui_ItemDefCmpSkillMain(*pRecipeDef1, *pRecipeDef2, s_CraftingData.bSortingDescend);

	// secondary sort order: slot, tag, alphabetical
	if (!iRet)
		iRet = craftingui_ItemDefCmpSlotMain(*pRecipeDef1, *pRecipeDef2, false);
	if (!iRet)
		iRet = craftingui_ItemDefCmpTagMain(*pRecipeDef1, *pRecipeDef2, false);
	if (!iRet)
		iRet = craftingui_ItemDefCmpAlphaMain(*pRecipeDef1, *pRecipeDef2, s_CraftingData.uiSortingGroupMask, false);
	return iRet;
}

// used when sorting by slot type - i.e. primary, secondary, or any
static int craftingui_ItemDefCmpSlot(const ItemDef **pRecipeDef1, const ItemDef **pRecipeDef2)
{
	int iRet = craftingui_ItemDefCmpSlotMain(*pRecipeDef1, *pRecipeDef2, s_CraftingData.bSortingDescend);

	// secondary sort order: skill, tag, alphabetical
	if (!iRet)
		iRet = craftingui_ItemDefCmpSkillMain(*pRecipeDef1, *pRecipeDef2, true);
	if (!iRet)
		iRet = craftingui_ItemDefCmpTagMain(*pRecipeDef1, *pRecipeDef2, false);
	if (!iRet)
		iRet = craftingui_ItemDefCmpAlphaMain(*pRecipeDef1, *pRecipeDef2, s_CraftingData.uiSortingGroupMask, false);
	return iRet;
}

// used when sorting by item tag
static int craftingui_ItemDefCmpTag(const ItemDef **pRecipeDef1, const ItemDef **pRecipeDef2)
{
	int iRet = craftingui_ItemDefCmpTagMain(*pRecipeDef1, *pRecipeDef2, s_CraftingData.bSortingDescend);
	
	// secondary sort order: slot, skill, alphabetical
	if (!iRet)
		iRet = craftingui_ItemDefCmpSlotMain(*pRecipeDef1, *pRecipeDef2, false);
	if (!iRet)
		iRet = craftingui_ItemDefCmpSkillMain(*pRecipeDef1, *pRecipeDef2, true);
	if (!iRet)
		iRet = craftingui_ItemDefCmpAlphaMain(*pRecipeDef1, *pRecipeDef2, s_CraftingData.uiSortingGroupMask, false);
	return iRet;
}

// Get a list of currently available recipes.
AUTO_EXPR_FUNC(UIGen) ACMD_NAME(craft_GetFilteredRecipeList);
void craftingui_GetFilteredRecipeList(SA_PARAM_NN_VALID UIGen *pGen, const char *pchTextFilter, int eRecipeType, U32 uiPowerGroupSlot, int eItemType, int eItemBagID, int eItemUpgradeType, U32 eTag, const char *pchSortOrder, bool bDescending, bool bFilterCraftable)
{
	static CraftRecipeInfo **s_eaRecipeList;

	int i;
	CraftingUISortOrder eSortOrder = StaticDefineIntGetInt(CraftingUISortOrderEnum, pchSortOrder);
	ItemDef **eaRecipeDefList = NULL;
	Entity *pEnt = entActivePlayerPtr();
	U32 uiPowerGroupMask;
	BagIterator *iter;
	GameAccountDataExtract *pExtract;

	eaClear(&s_eaRecipeList);

	if (!pEnt) 
		return;
	
	pExtract = entity_GetCachedGameAccountDataExtract(pEnt);

	uiPowerGroupMask = CraftBaseRecipeToPowerGroupMask(uiPowerGroupSlot, pExtract);

	// loop for all items in the bag
	iter = invbag_IteratorFromEnt(pEnt,InvBagIDs_Recipe, pExtract);
	for (; !bagiterator_Stopped(iter); bagiterator_Next(iter))
	{
		Item *pRecipeItem  = (Item*)bagiterator_GetItem(iter);
		ItemDef *pRecipeItemDef = bagiterator_GetDef(iter);

		if (!pRecipeItemDef) 
			continue;

		// add the recipe if it meets the requested criteria
		if (craftingui_ItemDefMatchesFilter(pEnt, pRecipeItemDef, pchTextFilter, eRecipeType, eItemType, pEnt->pPlayer->SkillType, eItemBagID, eItemUpgradeType, uiPowerGroupMask, eTag, bFilterCraftable, pExtract))
			eaPush(&eaRecipeDefList, pRecipeItemDef);
	}
	bagiterator_Destroy(iter);

	// sort the contents of the recipe list
	s_CraftingData.uiSortingGroupMask = uiPowerGroupMask;
	s_CraftingData.bSortingDescend = bDescending;
	switch (eSortOrder)
	{
		xcase craftingui_Alphabetical:
			eaQSort(eaRecipeDefList, craftingui_ItemDefCmpAlpha);
		xcase craftingui_Skill:
			eaQSort(eaRecipeDefList, craftingui_ItemDefCmpSkill);
		xcase craftingui_Slot:
			eaQSort(eaRecipeDefList, craftingui_ItemDefCmpSlot);
		xcase craftingui_Tag:
			eaQSort(eaRecipeDefList, craftingui_ItemDefCmpTag);

		// sort alphabetically by default
		xdefault:
			eaQSort(eaRecipeDefList, craftingui_ItemDefCmpAlpha);
	}

	for (i = 0; i < eaSize(&eaRecipeDefList); i++)
	{
		ItemDef *pRecipeItemDef = eaRecipeDefList[i];
		CraftRecipeInfo *pInfo = StructCreate(parse_CraftRecipeInfo);
		ItemPowerDef *pResultantItem = GET_REF(pRecipeItemDef->pCraft->hItemPowerResult);
		
		pInfo->pcInternalName = pRecipeItemDef->pchName;
		pInfo->pcIconName = pRecipeItemDef->pchIconName;
		pInfo->pcDisplayName = craftingui_GetRecipeDisplayName(pRecipeItemDef, uiPowerGroupMask,NULL);
		pInfo->bIsAlgo = !(pRecipeItemDef->Group == 0x00);
		pInfo->pcDescription = TranslateDisplayMessage(pRecipeItemDef->descriptionMsg);
		pInfo->pcTargetDescription = pResultantItem ? TranslateDisplayMessage(pResultantItem->descriptionMsg) : "";
		pInfo->iSkillRequirement = pRecipeItemDef->pRestriction ? pRecipeItemDef->pRestriction->iSkillLevel : 0;
		eaPush(&s_eaRecipeList, pInfo);
	}

	eaDestroy(&eaRecipeDefList);
	ui_GenSetManagedListSafe(pGen, &s_eaRecipeList, CraftRecipeInfo, true);
}

// Determines whether the player knows a recipe matching the passed in criteria
AUTO_EXPR_FUNC(UIGen) ACMD_NAME(CraftingHasFilteredRecipe);
bool craftingui_HasFilteredRecipe(SA_PARAM_NN_VALID UIGen *pGen, const char *pchTextFilter, int eRecipeType, U32 uiPowerGroupSlot, int eItemType, int eItemBagID, int eItemUpgradeType, U32 eTag, const char *pchSortOrder, bool bDescending, bool bFilterCraftable)
{
	Entity *pEnt = entActivePlayerPtr();
	U32 uiPowerGroupMask;
	BagIterator *iter;
	GameAccountDataExtract *pExtract;

	if (!pEnt) 
		return false;

	pExtract = entity_GetCachedGameAccountDataExtract(pEnt);

	uiPowerGroupMask = CraftBaseRecipeToPowerGroupMask(uiPowerGroupSlot, pExtract);

	// loop for all items in the bag
	iter = invbag_IteratorFromEnt(pEnt,InvBagIDs_Recipe,pExtract);
	for (; !bagiterator_Stopped(iter); bagiterator_Next(iter))
	{
		Item *pRecipeItem  = (Item*)bagiterator_GetItem(iter);
		ItemDef *pRecipeItemDef = bagiterator_GetDef(iter);

		if (!pRecipeItemDef) 
			continue;

		// add the recipe if it meets the requested criteria
		if (craftingui_ItemDefMatchesFilter(pEnt, pRecipeItemDef, pchTextFilter, eRecipeType, eItemType, pEnt->pPlayer->SkillType, eItemBagID, eItemUpgradeType, uiPowerGroupMask, eTag, bFilterCraftable, pExtract))
		{
			bagiterator_Destroy(iter);
			return true;
		}
	}
	bagiterator_Destroy(iter);

	return false;
}

// Get a list of ingredients to a recipe. 
AUTO_EXPR_FUNC(UIGen) ACMD_NAME(craft_GetComponentList);
void craftingui_GetComponentList(SA_PARAM_NN_VALID UIGen *pGen)
{
	Entity *pEnt = entActivePlayerPtr();
	if (pEnt)
	{
		S32 i;

		if (s_CraftingData.bComponentsDirty)
		{
			ItemDef *pBaseRecipe = (s_CraftingData.pcBaseRecipeName && s_CraftingData.pcBaseRecipeName[0]) ? (ItemDef*)RefSystem_ReferentFromString(g_hItemDict, s_CraftingData.pcBaseRecipeName) : NULL;
			ItemDef *pResultItemDef = pBaseRecipe && pBaseRecipe->pCraft ? GET_REF(pBaseRecipe->pCraft->hItemResult) : NULL;
			ItemDef *pPowerRecipe = NULL;
			ItemPowerDef *pPower = NULL;
			ItemCraftingComponent **eaComponents = NULL;
			S32 iLevel = pResultItemDef ? pResultItemDef->iLevel : 0;
			GameAccountDataExtract *pExtract = entity_GetCachedGameAccountDataExtract(pEnt);

			if (pBaseRecipe)
				item_GetAlgoIngredients(pBaseRecipe, &eaComponents, 0, iLevel, s_CraftingData.eQuality);

			for (i = 0; i < eaSize(&s_CraftingData.eaPowerRecipeNames); i++)
			{
				if (s_CraftingData.eaPowerRecipeNames[i] && s_CraftingData.eaPowerRecipeNames[i][0])
				{
					pPowerRecipe = (ItemDef*) RefSystem_ReferentFromString(g_hItemDict, s_CraftingData.eaPowerRecipeNames[i]);
					if (pPowerRecipe)
						item_GetAlgoIngredients(pPowerRecipe, &eaComponents, i, iLevel, s_CraftingData.eQuality);
				}
			}

			eaClearStruct(&s_CraftingData.eaComponents, parse_CraftComponentInfo);
			for (i = 0; i < eaSize(&eaComponents); i++)
			{
				ItemDef *pItemDef = GET_REF(eaComponents[i]->hItem);
				if (pItemDef)
				{
					CraftComponentInfo *pInfo = StructAlloc(parse_CraftComponentInfo);
					pInfo->pItem = item_FromEnt( CONTAINER_NOCONST(Entity, pEnt),pItemDef->pchName,0,NULL,0);
					pInfo->pcName = pItemDef->pchName;
					pInfo->pcDisplayName = TranslateDisplayMessage(pItemDef->displayNameMsg);
					pInfo->iNeed = (U32)eaComponents[i]->fCount;
					eaPush(&s_CraftingData.eaComponents, pInfo);
				}
			}
			eaDestroyStruct(&eaComponents, parse_ItemCraftingComponent);

			// sort components by need
			eaQSort(s_CraftingData.eaComponents, CraftComponentInfoCompareNeed);

			s_CraftingData.bComponentsDirty = false;
		}

		// update have values
		for (i = 0; i < eaSize(&s_CraftingData.eaComponents); i++)
			s_CraftingData.eaComponents[i]->iHave = inv_ent_AllBagsCountItems(pEnt, s_CraftingData.eaComponents[i]->pcName);
		
		ui_GenSetManagedListSafe(pGen, &s_CraftingData.eaComponents, CraftComponentInfo, false);
	}
}

// Get the list of item quality colors
AUTO_EXPR_FUNC(UIGen) ACMD_NAME(craft_GetQualityList);
void craftingui_GetQualityList(SA_PARAM_NN_VALID UIGen *pGen)
{
	static ItemQualityLevel **eaQualityList = NULL;
	if (!eaQualityList) {
		int i = 0;
		while (ItemQualityEnum[i].value != 0 || ItemQualityEnum[i].key != U32_TO_PTR(DM_END)) {
			if (ItemQualityEnum[i].value != 0 || ItemQualityEnum[i].key != U32_TO_PTR(DM_INT)) {
				ItemQualityLevel *pQuality = StructCreate(parse_ItemQualityLevel);
				pQuality->pcDisplayName = StructAllocString(ItemQualityEnum[i].key);
				pQuality->iValue = ItemQualityEnum[i].value;
				eaPush(&eaQualityList, pQuality);
			}
			i++;
		}
	}
	ui_GenSetManagedListSafe(pGen, &eaQualityList, ItemQualityLevel, false);
}

AUTO_EXPR_FUNC(UIGen) ACMD_NAME("CraftingInteract");
bool craftingui_CraftingInteract(SA_PARAM_OP_VALID Entity *pEnt)
{
	if ( pEnt &&
		 pEnt->pPlayer &&
		 pEnt->pPlayer->InteractStatus.bInteracting &&
		 pEnt->pPlayer->pInteractInfo &&
		 pEnt->pPlayer->pInteractInfo->bCrafting)
		return true;

	return false;
}

AUTO_EXPR_FUNC(UIGen) ACMD_NAME("CraftingGenGetRecipeTags");
void craftingui_CraftingGenGetRecipeTags(SA_PARAM_NN_VALID UIGen *pGen, SA_PARAM_OP_VALID Entity *pEnt)
{
	CraftTagInfo **peaCraftingTagList = NULL;

	if (pEnt)
	{
		int j;
		GameAccountDataExtract *pExtract = entity_GetCachedGameAccountDataExtract(pEnt);
		BagIterator *iter = invbag_IteratorFromEnt(pEnt,InvBagIDs_Recipe,pExtract);
		for (; !bagiterator_Stopped(iter); bagiterator_Next(iter))
		{
			Item *pRecipeItem  = (Item*)bagiterator_GetItem(iter);
			ItemDef *pRecipeItemDef = bagiterator_GetDef(iter);
			ItemDef *pResultItemDef = pRecipeItemDef && pRecipeItemDef->eType == kItemType_ItemRecipe && pRecipeItemDef->pCraft ? GET_REF(pRecipeItemDef->pCraft->hItemResult) : NULL;
			bool bFoundTag = false;

			// ignore item power recipes and recipes without results
			if (!pResultItemDef)
				continue;
			// ignore recipes of a different skill type than the player's current skill
			if (pEnt->pPlayer->SkillType != pRecipeItemDef->kSkillType)
				continue;

			// if the item's tag is already in the list, increment the count
			for (j = 0; j < eaSize(&peaCraftingTagList) && !bFoundTag; j++)
			{
				if (peaCraftingTagList[j]->eTag == pResultItemDef->eTag)
				{
					peaCraftingTagList[j]->iCount++;
					bFoundTag = true;
					break;
				}
			}

			// if this is a new tag, add it to the list
			if (!bFoundTag)
			{
				CraftTagInfo *pNewTag = StructCreate(parse_CraftTagInfo);
				pNewTag->iCount = 1;
				pNewTag->eTag = pResultItemDef->eTag;
				pNewTag->pchDisplayName = StaticDefineGetTranslatedMessage(ItemTagEnum, pResultItemDef->eTag);
				eaPush(&peaCraftingTagList, pNewTag);
			}
		}
		bagiterator_Destroy(iter);
	}

	// sort the list from greatest to least in terms of recipe count
	eaQSort(peaCraftingTagList, craftingui_CraftTagInfoCmp);

	// set the list onto the gen
	ui_GenSetManagedListSafe(pGen, &peaCraftingTagList, CraftTagInfo, true);
}

/******
* TIMER
******/
AUTO_EXPR_FUNC(UIGen);
void CraftingStartTimer(void)
{
	// buffer current crafting data so they can browse during crafting operation
	// without affecting what they're crafting
	StructDestroySafe(parse_StaticCraftingData, &s_BufferedCraftingData);
	s_BufferedCraftingData = StructClone(parse_StaticCraftingData, &s_CraftingData);

	s_iTimerIndex = timerAlloc();
	timerStart(s_iTimerIndex);
}

AUTO_EXPR_FUNC(UIGen);
void CraftingEndTimer(void)
{
	if (!!s_iTimerIndex)
		timerFree(s_iTimerIndex);
	s_iTimerIndex = 0;
}

AUTO_EXPR_FUNC(UIGen);
bool CraftingShowTimer(void)
{
	if (!!s_iTimerIndex && timerElapsed(s_iTimerIndex) > (ITEM_CRAFT_DELAY_SECS + ITEM_CRAFT_CLIENT_DELAY_SECS))
	{
		CraftingEndTimer();
		CraftingExecute();
	}
	return !!s_iTimerIndex;
}

AUTO_EXPR_FUNC(UIGen);
int CraftingGetTimeElapsedMs(void)
{
	// This is a fix for crashing when logging in after logging out while crafting.
	if (!s_iTimerIndex)
		return ITEM_CRAFT_DELAY_SECS * 1000;

	return MIN(timerElapsed(s_iTimerIndex) * 1000, ITEM_CRAFT_DELAY_SECS * 1000);
}

AUTO_EXPR_FUNC(UIGen);
int CraftingGetTotalTimeMs(void)
{
	return (ITEM_CRAFT_DELAY_SECS * 1000);
}

/*********
* PREVIEWS
*********/
// Used by the item gen to set its pointer to the current preview item
AUTO_EXPR_FUNC(UIGen) ACMD_NAME("GenSetDataCraftingPreviewItem");
void ui_GenSetDataCraftingPreviewItem(SA_PARAM_NN_VALID UIGen *pGen)
{
	if (pGen)
		ui_GenSetPointer(pGen, s_CraftingPreviewItem, parse_Item);
}

// Invoked whenever the selected crafting item is changed to fetch the preview item data from the server 
AUTO_EXPR_FUNC(UIGen) ACMD_NAME(craft_PreviewCraftItem);
void craftingui_PreviewCraftItem(void)
{
	CraftData *pCraftData;
	Item *pBaseRecipe = NULL;
	ItemDef *pBaseRecipeDef = NULL;
	GameAccountDataExtract *pExtract = NULL; // No entity
	static U32 sLastTimeSeconds = 0;

	// once per second maximum
	if(timeSecondsSince2000() <= sLastTimeSeconds)
	{
		return;
	}

	if (!s_CraftingData.pcBaseRecipeName || !s_CraftingData.pcBaseRecipeName[0])
		return;

	pBaseRecipe = craftingui_FindRecipeInBag(s_CraftingData.pcBaseRecipeName, pExtract);
	pBaseRecipeDef = pBaseRecipe ? GET_REF(pBaseRecipe->hItem) : NULL;
	if (!pBaseRecipeDef)
		return;

	// Don't keep sending messages to the server if its the same item
	if(s_CraftingPreviewItem && s_CraftingData.pcBaseRecipeName == s_LastCraftingData.pcBaseRecipeName)
	{
		if(pBaseRecipeDef->Group != 0x00)
		{
			if(s_LastCraftingData.eQuality == s_CraftingData.eQuality && eaSize(&s_LastCraftingData.eaPowerRecipeNames) == eaSize(&s_CraftingData.eaPowerRecipeNames))
			{
				S32 i;
				bool bTheSame = true;
				for (i = 0; i < eaSize(&s_CraftingData.eaPowerRecipeNames); ++i)
				{
					if(s_CraftingData.eaPowerRecipeNames[i] != s_LastCraftingData.eaPowerRecipeNames[i])
					{
						bTheSame = false;
						break;
					}
				}

				if(bTheSame)
				{
					return;
				}
			}
		}
		else
		{
			// basic recipe already created
			return;
		}
	}

	pCraftData = StructAlloc(parse_CraftData);
	pCraftData->pcBaseItemRecipeName = StructAllocString(s_CraftingData.pcBaseRecipeName);

	// always push powers onto crafting data for algo items, even if powers are NULL
	if (pBaseRecipeDef->Group != 0x00)
	{
		int i;

		pCraftData->eQuality = s_CraftingData.eQuality;
		for (i = 0; i < eaSize(&s_CraftingData.eaPowerRecipeNames); i++)
			eaPush(&pCraftData->eaItemPowerRecipes, StructAllocString(s_CraftingData.eaPowerRecipeNames[i]));
	}

	s_LastCraftingData.eQuality = s_CraftingData.eQuality;
	s_LastCraftingData.pcBaseRecipeName = s_CraftingData.pcBaseRecipeName;
	eaCopy(&s_LastCraftingData.eaPowerRecipeNames, &s_CraftingData.eaPowerRecipeNames);

	ServerCmd_item_CraftPreview(pCraftData);
	StructDestroy(parse_CraftData, pCraftData);

	sLastTimeSeconds = timeSecondsSince2000();

}

// Called every frame to determine whether to show the preview item sprite
AUTO_EXPR_FUNC(UIGen) ACMD_NAME("craft_PreviewReady");
bool crafting_PreviewReady(void)
{
	return !!s_CraftingPreviewItem;
}

// Receives the preview item data from the server
AUTO_COMMAND ACMD_ACCESSLEVEL(0) ACMD_CLIENTCMD ACMD_PRIVATE;
void CraftingUpdatePreview(Entity *pEnt, Item *pPreviewItem)
{
	StructDestroySafe(parse_Item, &s_CraftingPreviewItem);
	s_CraftingPreviewItem = StructClone(parse_Item, pPreviewItem);
}

// Sends command to the server to fetch the skillup ranges
AUTO_EXPR_FUNC(UIGen);
void CraftingFetchSkillupRanges(void)
{
	ServerCmd_CraftingGetSkillupRanges();
}

// Returns an index of skillup percentage given a skill difference value, where 0 means that
// the highest chance of a skillup will occur; -1 will be returned if the skill diff value 
// lies below the first entry's range
AUTO_EXPR_FUNC(UIGen);
int CraftingGetSkillupIndex(int iSkillDiff)
{
	int i;
	for (i = 0; i < eaiSize(&s_eaiCraftingSkillupRanges); i++)
	{
		if (iSkillDiff < s_eaiCraftingSkillupRanges[i])
			return i - 1;
	}

	return eaiSize(&s_eaiCraftingSkillupRanges) - 1;
}

// Receives the skillup ranges from the server (only needs to be done once)
AUTO_COMMAND ACMD_ACCESSLEVEL(0) ACMD_CLIENTCMD ACMD_PRIVATE;
void CraftingUpdateSkillupRanges(CraftSkillupRanges *pRanges)
{
	eaiCopy(&s_eaiCraftingSkillupRanges, &pRanges->eaiRanges);
	eaiQSortG(s_eaiCraftingSkillupRanges, intCmp);
}

/**********************
* CRAFTING SKILL RANGES
**********************/
// Crafting skill ranges
AUTO_STARTUP(CraftingSkillRanges) ASTRT_DEPS(AS_Messages);
void craftingSkillRanges_Load(void)
{
	if (IsClient())
	{
		// load all skill ranges from disk
		loadstart_printf("Loading CraftingSkillRanges...");
		ParserLoadFiles(NULL, "defs/config/CraftingSkillRanges.def", "CraftingSkillRanges.bin", PARSER_OPTIONALFLAG, parse_CraftingSkillRangeSet, &s_pCraftingSkillRanges);
		loadend_printf("done.");
	}
}

AUTO_EXPR_FUNC(UIGen) ACMD_NAME("CraftingGetSkillRangeName");
const char *craftingui_CraftingGetSkillRangeName(int iSkillLevel)
{
	int i;

	for (i = 0; i < eaSize(&s_pCraftingSkillRanges.eaSkillRanges); i++)
	{
		CraftingSkillRange *pRange = s_pCraftingSkillRanges.eaSkillRanges[i];
		if (iSkillLevel >= pRange->iSkillMin && iSkillLevel <= pRange->iSkillMax)
			return langTranslateMessageRef(locGetLanguage(getCurrentLocale()), pRange->hMessage);
	}

	return "";
}

AUTO_EXPR_FUNC(UIGen) ACMD_NAME("CraftingGetSkillBaseForTrainer");
const char *craftingui_GetSkillBaseForTrainer(ExprContext *pContext, SA_PARAM_OP_VALID Entity *pEnt) {
	static char *pchSkillBase = NULL;

	if(pchSkillBase) {
		estrDestroy(&pchSkillBase);
	}

	if(pEnt && pEnt->pChar && pEnt->pPlayer && pEnt->pPlayer->pInteractInfo && pEnt->pPlayer->pInteractInfo && pEnt->pPlayer->pInteractInfo->pContactDialog) {
		estrPrintf(&pchSkillBase, "%s", StaticDefineIntRevLookup(SkillTypeEnum, pEnt->pPlayer->pInteractInfo->pContactDialog->iSkillType));
		return pchSkillBase;
	} else {
		return "";
	}
}

AUTO_EXPR_FUNC(UIGen) ACMD_NAME("CraftingGetSubspecs");
bool craftingui_GetSubspecs(ExprContext *pContext, SA_PARAM_NN_VALID UIGen *pGen, SA_PARAM_OP_VALID Entity *pEnt)
{
	SkillSubspec ***peaSkills = ui_GenGetManagedListSafe(pGen, SkillSubspec);
	InteractInfo *pInfo = SAFE_MEMBER2(pEnt, pPlayer, pInteractInfo);
	int i = 0;
	int oldSkillsSize = eaSize(peaSkills);

	// Clean up whatever was there from the last time this was called.
	for(i = 0; i < oldSkillsSize; i++) {
		StructDestroy(parse_SkillSubspec, (*peaSkills)[i]);
	}
	eaClear(peaSkills);

	if(pEnt && pEnt->pChar && pEnt->pPlayer && pEnt->pPlayer->pInteractInfo && pEnt->pPlayer->pInteractInfo && pEnt->pPlayer->pInteractInfo->pContactDialog) {

		const char *skillName = NULL;
		const char *skillBase = StaticDefineIntRevLookup(SkillTypeEnum, pEnt->pPlayer->pInteractInfo->pContactDialog->iSkillType);
		i = 2; // The actual tags start after the None and Any tags in the enum

		while(skillName = StaticDefineIntRevLookup(ItemTagEnum, i)) {
			if(!strncmp(skillBase, skillName, strlen(skillBase))) {
				eaPush(peaSkills, StructCreate(parse_SkillSubspec));
				(*peaSkills)[eaSize(peaSkills) - 1]->iMajorSkill = pEnt->pPlayer->pInteractInfo->pContactDialog->iSkillType;
				(*peaSkills)[eaSize(peaSkills) - 1]->iMinorSkill = i;
				estrPrintf(&((*peaSkills)[eaSize(peaSkills) - 1]->pchName), "%s", skillName);
			}
			i++;
		}
	}

	ui_GenSetManagedListSafe(pGen, peaSkills, SkillSubspec, true);

	return true;
}

AUTO_EXPR_FUNC(UIGen) ACMD_NAME("GetSkillType");
int exprGetSkillType(Entity *pEnt)
{
	return entity_GetSkill(pEnt);
}

AUTO_EXPR_FUNC(UIGen) ACMD_NAME("GetSkillLevel");
int exprGetSkillLevel(Entity *pEnt)
{
	return entity_GetSkillLevel(pEnt);
}

AUTO_EXPR_FUNC(UIGen) ACMD_NAME("SetSkillSpecialization");
void exprSetSkillSpecialization(Entity *pEnt, int iMajorSkill, int iMinorSkill) {
	ServerCmd_SetSkillAndSpecializationType(iMajorSkill, iMinorSkill);
}

#include "CraftingUI_c_ast.c"




















