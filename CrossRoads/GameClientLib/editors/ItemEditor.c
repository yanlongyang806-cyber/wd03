 //
// ItemEditor.c
//

#ifndef NO_EDITORS

#include "ItemCommon.h"
#include "itemCommon_h_ast.h"
#include "ItemEnums.h"
#include "itemEnums_h_ast.h"
#include "ItemEditor.h"
#include "mission_common.h"
#include "MultiEditTable.h"
#include "MultiEditWindow.h"
#include "Powers.h"
#include "powertree.h"
#include "PowerVars.h"
#include "ResourceSearch.h"
#include "StringCache.h"
#include "tokenstore.h"
#include "estring.h"
#include "CharacterAttribs.h"
#include "GameBranch.h"
#include "cmdparse.h"
#include "MicroTransactions.h"
#include "SuperCritterPet.h"
#include "CharacterClass.h"

#include "AutoGen/CostumeCommonEnums_h_ast.h"
#include "AutoGen/CostumeCommon_h_ast.h"
#include "AutoGen/CharacterClass_h_ast.h"
#include "AutoGen/CharacterAttribs_h_ast.h"
#include "AutoGen/PowerTree_h_ast.h"
#include "Autogen/microtransactions_h_ast.h"

#define IE_GROUP_CRAFT       "Crafting"
#define IE_GROUP_DISPLAY     "Display"
#define IE_GROUP_INFO        "Info"
#define IE_GROUP_MAIN        "Main"
#define IE_GROUP_MISSION     "Mission"
#define IE_GROUP_RESTRICTION "Restrictions"
#define IE_GROUP_EQUIPLIMIT	 "EquipLimit"
#define IE_GROUP_TIMING      "Timing"
#define IE_GROUP_VALUES      "Values"
#define IE_GROUP_WEAPON		 "Weapon"
#define IE_ATTRIBMODIFY_VALUES "Attrib Modify Rules"

#define IE_SUBGROUP_CRAFT			"Craft"
#define IE_SUBGROUP_POWERS			"Powers"
#define IE_SUBGROUP_COSTUME			"Costume"
#define IE_SUBGROUP_VANITYPET		"VanityPet"
#define IE_SUBGROUP_TRAINABLENODE	"TrainableNode"
#define IE_SUBGROUP_BONUS_NUMERIC	"BonusNumeric"


// This magic line is required if you run in Full Debug to avoid assert
AUTO_RUN_ANON(memBudgetAddMapping(__FILE__, BUDGET_Editors););


//-----------------------------------------------------------------------------------
// Global Data
//-----------------------------------------------------------------------------------

static MEWindow *ieWindow = NULL;

static int ieTrainableNodesId = 0;
static int iePowersId = 0;
static int ieGemHolderID = 0;
static int ieInfuseId = 0;
static int ieComponentId = 0;
static int ieCostumeId = 0;
static int ieAttribId = 0;
static int ieVanityPetId = 0;
static int ieBonusNumericId = 0;

extern ExprContext *g_pItemContext;


//---------------------------------------------------------------------------------------------------
// Callbacks
//---------------------------------------------------------------------------------------------------


static int ie_compareStrings(const char** left, const char** right)
{
	return stricmp(*left,*right);
}

static void *ie_createAttrib(METable *pTable, ItemDef *pItemDef, TempAttribute *pAttribToClone, TempAttribute *pBeforeAttrib, TempAttribute *pAfterAttrib)
{
	TempAttribute *pNewAttrib = NULL;

	if(pAttribToClone) {
		pNewAttrib = StructClone(parse_TempAttribute, pAttribToClone);
	} else {
		pNewAttrib = StructCreate(parse_TempAttribute);
	}

	if(!pNewAttrib)
		return NULL;

	return pNewAttrib;
}

static void *ie_createTrainableNode(METable *pTable, ItemDef *pItemDef, ItemTrainablePowerNode *pNodeToClone, ItemTrainablePowerNode *pBeforeNode, ItemTrainablePowerNode *pAfterNode)
{
	ItemTrainablePowerNode *pNewNode = NULL;

	// Allocate the object
	if (pNodeToClone) {
		pNewNode = (ItemTrainablePowerNode*)StructClone(parse_ItemTrainablePowerNode, pNodeToClone);
	} else {
		pNewNode = (ItemTrainablePowerNode*)StructCreate(parse_ItemTrainablePowerNode);
	}
	if (!pNewNode) {
		return NULL;
	}

	return pNewNode;
}

static void *ie_createPower(METable *pTable, ItemDef *pItemDef, ItemPowerDefRef *pPowerToClone, ItemPowerDefRef *pBeforePower, ItemPowerDefRef *pAfterPower)
{
	ItemPowerDefRef *pNewPower = NULL;

	// Allocate the object
	if (pPowerToClone) {
		pNewPower = (ItemPowerDefRef*)StructClone(parse_ItemPowerDefRef, pPowerToClone);
	} else {
		pNewPower = (ItemPowerDefRef*)StructCreate(parse_ItemPowerDefRef);
	}
	if (!pNewPower) {
		return NULL;
	}

	if (g_ItemConfig.bUseUniqueIDsForItemPowerDefRefs)
	{
		NOCONST(ItemPowerDefRef) *pNoConstNewPower = CONTAINER_NOCONST(ItemPowerDefRef, pNewPower);
		pNoConstNewPower->uID = pItemDef->uNewItemPowerDefRefID;
		++pItemDef->uNewItemPowerDefRefID;
	}

	return pNewPower;
}

static void *ie_createGemSlot(METable *pTable, ItemDef *pItemDef, ItemGemSlotDef *pDefToClone, ItemGemSlotDef *pBeforeDef, ItemGemSlotDef *pAfterPower)
{
	ItemGemSlotDef *pNewDef = NULL;

	// Allocate the object
	if (pDefToClone) {
		pNewDef = (ItemGemSlotDef*)StructClone(parse_ItemGemSlotDef, pDefToClone);
	} else {
		pNewDef = (ItemGemSlotDef*)StructCreate(parse_ItemGemSlotDef);
	}
	if (!pNewDef) {
		return NULL;
	}

	return pNewDef;
}

static void *ie_createComponent(METable *pTable, ItemDef *pItemDef, ItemCraftingComponent *pComponentToClone, ItemCraftingComponent *pBeforeComponent, ItemCraftingComponent *pAfterComponent)
{
	ItemCraftingComponent *pNewComponent;

	// Allocate the object
	if (pComponentToClone) {
		pNewComponent = (ItemCraftingComponent*)StructClone(parse_ItemCraftingComponent, pComponentToClone);
	} else {
		pNewComponent = (ItemCraftingComponent*)StructCreate(parse_ItemCraftingComponent);
	}

	return pNewComponent;
}

static void *ie_createItemVanityPet(METable *pTable, ItemDef *pItemDef, ItemVanityPet *pVanityPetToClone, ItemVanityPet *pBeforeVanityPet, ItemVanityPet *pAfterVanityPet)
{
	ItemVanityPet *pNewVanityPet;

	// Allocate the object
	if (pVanityPetToClone) {
		pNewVanityPet = (ItemVanityPet*)StructClone(parse_ItemVanityPet, pVanityPetToClone);
	} else {
		pNewVanityPet = (ItemVanityPet*)StructCreate(parse_ItemVanityPet);
	}

	return pNewVanityPet;
}

static void *ie_createBonusNumeric(METable *pTable, ItemDef *pItemDef, ItemDefBonus *pNumericToClone, ItemDefBonus *pBeforeNumeric, ItemDefBonus *pAfterNumeric)
{
	ItemDefBonus *pNewNumeric;

	// Allocate the object
	if (pNumericToClone) {
		pNewNumeric = (ItemDefBonus*)StructClone(parse_ItemDefBonus, pNumericToClone);
	} else {
		pNewNumeric = (ItemDefBonus*)StructCreate(parse_ItemDefBonus);
	}

	return pNewNumeric;
}

static void *ie_createItemCostume(METable *pTable, ItemDef *pItemDef, ItemCostume *pCostumeToClone, ItemCostume *pBeforeCostume, ItemCostume *pAfterCostume)
{
	ItemCostume *pNewCostume;

	// Allocate the object
	if (pCostumeToClone) {
		pNewCostume = (ItemCostume*)StructClone(parse_ItemCostume, pCostumeToClone);
	} else {
		pNewCostume = (ItemCostume*)StructCreate(parse_ItemCostume);
	}

	return pNewCostume;
}

void ie_clearNotApplicableColumns(METable *pTable, ItemDef *pItemDef)
{
	int i, j;
	for (i = eaSize(&pTable->eaRows)-1; i >= 0; i--)
	{
		const char *pcObjectName = met_getObjectName(pTable, i);
		if(stricmp(pcObjectName, pItemDef->pchName)==0)
		{
			METableRow *pRow = pTable->eaRows[i];

			for (j = eaSize(&pRow->eaFields)-1; j >= 0; j--)
			{
				MEField *pField = pRow->eaFields[j];
				if (SAFE_MEMBER(pField, bNotApplicable))
				{
					char achPath[256] = ".";
					char *pchPath = pTable->eaCols[j]->pcPTName;
					void *pData;

					if (pchPath[0] != '.')
						strncpy_s(&achPath[1], 255, pchPath, 255);

					if(ParserResolvePath(pchPath[0] == '.' ? pchPath : achPath, parse_ItemDef, pItemDef, NULL, NULL, &pData, NULL, NULL, NULL, 0))
					{ 
						FieldClear(pField->pTable, pField->column, pData, 0);
					}
				}
			}
		}
	}
}


static int ie_validateCallback(METable *pTable, ItemDef *pItemDef, void *pUserData)
{
	char buf[1024];
	int i;

	if (pItemDef->pchName[0] == '_') {
		sprintf(buf, "The item '%s' cannot have a name starting with an underscore.", pItemDef->pchName);
		ui_DialogPopup("Validation Error", buf);
		return 0;
	}

	if(strStartsWith(pItemDef->pchScope,"Itemgen"))
	{
		sprintf(buf, "The item '%s' is either an item that was generated by the Item Gen system, or is tring to be saved into the Item Gen directory. This is not allowed!", pItemDef->pchName);
		ui_DialogPopup("Saving a file in the Itemgen Scope",buf);
		return 0;
	}

	// Ensure only have components or powers but not both
	if ( !item_IsRecipe(pItemDef) &&
		 (eaSize(&pItemDef->pCraft->ppPart) > 0) ) 
	{
		for(i=eaSize(&pItemDef->pCraft->ppPart)-1; i>=0; --i) {
			StructDestroy(parse_ItemCraftingComponent, pItemDef->pCraft->ppPart[i]);
		}
		eaClear(&pItemDef->pCraft->ppPart);

	} 

	if (pItemDef->eType!=kItemType_Weapon)
	{
		if (pItemDef->pItemWeaponDef)
		{
			StructDestroySafe(parse_ItemWeaponDef, &pItemDef->pItemWeaponDef);
		}
		if (pItemDef->pItemDamageDef)
		{
			StructDestroySafe(parse_ItemDamageDef, &pItemDef->pItemDamageDef);
		}		
	}

	if (pItemDef->pEquipLimit &&
		!pItemDef->pEquipLimit->iMaxEquipCount &&
		!pItemDef->pEquipLimit->eCategory)
	{
		StructDestroySafe(parse_ItemEquipLimit, &pItemDef->pEquipLimit);
	}

	if (pItemDef->pRewardPackInfo && pItemDef->eType!=kItemType_RewardPack)
	{
		StructDestroySafe(parse_RewardPackInfo,&pItemDef->pRewardPackInfo);
	}

	// Destroy ItemPowerDefs if the item changed to a type that shouldn't have them
	switch (pItemDef->eType)
	{
		xcase kItemType_RewardPack:
		acase kItemType_Component:
		acase kItemType_ItemRecipe:
		acase kItemType_ItemValue:
		acase kItemType_ItemPowerRecipe:
		acase kItemType_Mission:
		acase kItemType_MissionGrant:
		acase kItemType_Numeric:
		acase kItemType_SavedPet:
		acase kItemType_AlgoPet:
		acase kItemType_ModifyAttribute:
		acase kItemType_Bag:
		acase kItemType_Lore:
		acase kItemType_GrantMicroSpecial:
		acase kItemType_ExperienceGift:
		acase kItemType_Coupon:
		{
			eaDestroyStruct(&pItemDef->ppItemPowerDefRefs, parse_ItemPowerDefRef);
		}
	}
	
	return item_Validate(pItemDef);
}

static void ie_postOpenCallback(METable *pTable, ItemDef *pItemDef, ItemDef *pOrigItemDef)
{
	item_FixMessages(pItemDef);
	if (pOrigItemDef) {
		item_FixMessages(pOrigItemDef);
	}
}


static void ie_preSaveCallback(METable *pTable, ItemDef *pItemDef)
{
	// Remove empty restriction block
	if (pItemDef->pRestriction && 
			(pItemDef->pRestriction->iMaxLevel == 0) &&
			(pItemDef->pRestriction->iMinLevel == 0) &&
			(!pItemDef->pRestriction->pRequires) &&
			(pItemDef->pRestriction->iSkillLevel == 0) &&
			(pItemDef->pRestriction->eSkillType == kSkillType_None) &&
			(pItemDef->pRestriction->eUICategory == UsageRestrictionCategory_None) &&
			(eaSize(&pItemDef->pRestriction->ppCharacterClassesAllowed) == 0) &&
			(eaSize(&pItemDef->pRestriction->ppCharacterPathsAllowed) == 0) &&
			(eaiSize(&pItemDef->pRestriction->peClassCategoriesAllowed) == 0) &&
			(eaSize(&pItemDef->pRestriction->eaRequiredAllegiances) == 0)) {
		int col;
		StructDestroy(parse_UsageRestriction, pItemDef->pRestriction);
		ParserFindColumn(parse_ItemDef, "Restriction", &col);
		TokenStoreSetPointer(parse_ItemDef, col, pItemDef, 0, NULL, NULL);
	}

	// Remove craft struct if not a recipe
	if (pItemDef->pCraft && !item_IsRecipe(pItemDef))
	{
		int col;
		StructDestroy(parse_ItemCraftingTable, pItemDef->pCraft);
		ParserFindColumn(parse_ItemDef, "Craft", &col);
		TokenStoreSetPointer(parse_ItemDef, col, pItemDef, 0, NULL, NULL);

	}

	// Remove empty ItemAttribModifyValues
	if(pItemDef->pAttribModifyValues
		&& !pItemDef->pAttribModifyValues->eSavedPetClassType
		&& (!pItemDef->pAttribModifyValues->pTempAttribs
			|| !eaSize(&pItemDef->pAttribModifyValues->pTempAttribs->ppAttributes)))
	{
		StructDestroySafe(parse_ItemAttribModifyValues,&pItemDef->pAttribModifyValues);
	}

	// Remove unused pJournalData
	if(pItemDef->pJournalData
		&& (pItemDef->pJournalData->eType == kLoreJournalType_None || pItemDef->eType != kItemType_Lore))
	{
		StructDestroySafe(parse_LoreJournalData,&pItemDef->pJournalData);
	}

	ie_clearNotApplicableColumns(pTable, pItemDef);

	// Fix up display name
	item_FixMessages(pItemDef);
}


static void *ie_createObject(METable *pTable, ItemDef *pObjectToClone, char *pcNewName, bool bCloneKeepsKeys)
{
	ItemDef *pNewDef = NULL;
	const char *pcBaseName;
	const char *pcDisplayName = NULL;
	const char *pcDescription = NULL;
	const char *pcDescShort = NULL;
	const char *pcDisplayNameUnIDed = NULL;
	const char *pcDescriptionUnIDed = NULL;
	const char *pcCallout = NULL;
	const char *pcFSM = NULL;
	char *pcBuffer = NULL;
	Message *pMessage = NULL;
	char *tmpS = NULL;

	estrStackCreate(&tmpS);

	// Create the object
	if (pObjectToClone) {
		pNewDef = StructClone(parse_ItemDef, pObjectToClone);
		pcBaseName = pObjectToClone->pchName;
		pcDisplayName = pObjectToClone->displayNameMsg.pEditorCopy ? pObjectToClone->displayNameMsg.pEditorCopy->pcDefaultString : NULL;
		pcDescription = pObjectToClone->descriptionMsg.pEditorCopy ? pObjectToClone->descriptionMsg.pEditorCopy->pcDefaultString : NULL;
		pcDescShort = pObjectToClone->descShortMsg.pEditorCopy ? pObjectToClone->descShortMsg.pEditorCopy->pcDefaultString : NULL;
		pcDisplayNameUnIDed = pObjectToClone->displayNameMsgUnidentified.pEditorCopy ? pObjectToClone->displayNameMsgUnidentified.pEditorCopy->pcDefaultString : NULL;
		pcDescriptionUnIDed = pObjectToClone->descriptionMsgUnidentified.pEditorCopy ? pObjectToClone->descriptionMsgUnidentified.pEditorCopy->pcDefaultString : NULL;
		pcCallout = pObjectToClone->calloutMsg.pEditorCopy ? pObjectToClone->calloutMsg.pEditorCopy->pcDefaultString : NULL;
		pcFSM = pObjectToClone->calloutFSM ? pObjectToClone->calloutFSM : NULL;
	} else {
		pNewDef = StructCreate(parse_ItemDef);

		pcBaseName = "_New_Item";
		pcDisplayName = NULL;
		pcDescription = NULL;
		pcCallout = NULL;
		pcFSM = NULL;
	}
	// Use provided name if available
	if (pcNewName) {
		pcBaseName = pcNewName;
		pcDisplayName = NULL;
		pcDescription = NULL;
		pcCallout = NULL;
		pcFSM = NULL;
	}

	assertmsg(pNewDef, "Failed to create item");

	// Assign a new name
	pNewDef->pchName = (char*)METableMakeNewNameShared(pTable, pcBaseName, true);

	// Create display name message
	pMessage = langCreateMessage("", "", "", pcDisplayName);
	pNewDef->displayNameMsg.pEditorCopy = pMessage;

	// Create description message
	pMessage = langCreateMessage("", "", "", pcDescription);
	pNewDef->descriptionMsg.pEditorCopy = pMessage;

	// Create short description message
	pMessage = langCreateMessage("", "", "", pcDescShort);
	pNewDef->descShortMsg.pEditorCopy = pMessage;

	// Create UnIDed display name message
	pMessage = langCreateMessage("", "", "", pcDisplayNameUnIDed);
	pNewDef->displayNameMsgUnidentified.pEditorCopy = pMessage;

	// Create UnIDed description message
	pMessage = langCreateMessage("", "", "", pcDescriptionUnIDed);
	pNewDef->descriptionMsgUnidentified.pEditorCopy = pMessage;

	// Create callout message
	pMessage = langCreateMessage("", "", "", pcCallout);
	pNewDef->calloutMsg.pEditorCopy = pMessage;

	// Assign a file
	estrPrintf(&tmpS, 
		FORMAT_OK(GameBranch_FixupPath(&pcBuffer, "defs/items/%s.item", true, false)),
		pNewDef->pchName);
	pNewDef->pchFileName = (char*)allocAddString(tmpS);

	estrDestroy(&tmpS);
	estrDestroy(&pcBuffer);

	return pNewDef;
}


static void *ie_tableCreateCallback(METable *pTable, ItemDef *pObjectToClone, bool bCloneKeepsKeys)
{
	return ie_createObject(pTable, pObjectToClone, NULL, bCloneKeepsKeys);
}


static void *ie_windowCreateCallback(MEWindow *pWindow, ItemDef *pObjectToClone)
{
	return ie_createObject(pWindow->pTable, pObjectToClone, NULL, false);
}


static void ie_typeChangeCallback(METable *pTable, ItemDef *pItemDef, void *pUserData, bool bInitNotify)
{
	//hide any optional fields

	METableHideSubTable(pTable, pItemDef, ieVanityPetId, (pItemDef->eType!=kItemType_VanityPet));

	METableSetFieldNotApplicable(pTable, pItemDef, "Wpn Dmg Mag", (pItemDef->eType != kItemType_Weapon));
	METableSetFieldNotApplicable(pTable, pItemDef, "Wpn Dmg Variance", (pItemDef->eType != kItemType_Weapon));
	METableSetFieldNotApplicable(pTable, pItemDef, "Wpn Dmg Table", (pItemDef->eType != kItemType_Weapon));
	METableSetFieldNotApplicable(pTable, pItemDef, "SuperCritterPet Def", (pItemDef->eType != kItemType_SuperCritterPet));

	METableSetFieldNotApplicable(pTable, pItemDef, "Reward Table", (pItemDef->eType!=kItemType_RewardPack));
	METableSetFieldNotApplicable(pTable, pItemDef, "Reward Pack Open Message", (pItemDef->eType!=kItemType_RewardPack));
	METableSetFieldNotApplicable(pTable, pItemDef, "Reward Pack Open Failed Message", (pItemDef->eType!=kItemType_RewardPack));
	METableSetFieldNotApplicable(pTable, pItemDef, "Reward Pack Required Item", (pItemDef->eType!=kItemType_RewardPack));
	METableSetFieldNotApplicable(pTable, pItemDef, "Reward Pack Required Item Count", (pItemDef->eType!=kItemType_RewardPack));
	METableSetFieldNotApplicable(pTable, pItemDef, "Reward Pack Consume Required Items", (pItemDef->eType!=kItemType_RewardPack));
	METableSetFieldNotApplicable(pTable, pItemDef, "Reward Pack Required Item Product", (pItemDef->eType!=kItemType_RewardPack));
	METableSetFieldNotApplicable(pTable, pItemDef, "Species", (pItemDef->eType!=kItemType_Device));
	METableSetFieldNotApplicable(pTable, pItemDef, "Power Factor", (pItemDef->eType!=kItemType_Weapon && pItemDef->eType!=kItemType_Upgrade &&
																	pItemDef->eType!=kItemType_Device && pItemDef->eType!=kItemType_PowerFactorLevelUp));
	METableSetFieldNotApplicable(pTable, pItemDef, "Gem Added Costume FX", pItemDef->eType!=kItemType_Gem);
	METableSetFieldNotApplicable(pTable, pItemDef, "Gem Added Costume Bone", pItemDef->eType!=kItemType_Gem);
	METableSetFieldNotApplicable(pTable, pItemDef, "Special Part", (pItemDef->eType!=kItemType_GrantMicroSpecial));
	METableSetFieldNotApplicable(pTable, pItemDef, "Special Part count", (pItemDef->eType!=kItemType_GrantMicroSpecial));
	METableSetFieldNotApplicable(pTable, pItemDef, "Permission", (pItemDef->eType!=kItemType_GrantMicroSpecial));
	METableSetFieldNotApplicable(pTable, pItemDef, "Experience", (pItemDef->eType!=kItemType_ExperienceGift));
	METableSetFieldNotApplicable(pTable, pItemDef, "Activate failure message", (pItemDef->eType!=kItemType_Device));
	METableSetFieldNotApplicable(pTable, pItemDef, "Coupon uses item lvl", (pItemDef->eType!=kItemType_Coupon));
	METableSetFieldNotApplicable(pTable, pItemDef, "Coupon Discount", (pItemDef->eType!=kItemType_Coupon));
	METableSetFieldNotApplicable(pTable, pItemDef, "MT Categories", (pItemDef->eType!=kItemType_Coupon));
	METableSetFieldNotApplicable(pTable, pItemDef, "Microtransaction Item", (pItemDef->eType!=kItemType_Coupon));
	METableSetFieldNotApplicable(pTable, pItemDef, "ScaleUI", (pItemDef->eType!=kItemType_Numeric));
	METableSetFieldNotApplicable(pTable, pItemDef, "Dye Color 1", 1);
	METableSetFieldNotApplicable(pTable, pItemDef, "Dye Color 2", 1);
	METableSetFieldNotApplicable(pTable, pItemDef, "Dye Color 3", 1);
	METableSetFieldNotApplicable(pTable, pItemDef, "Dye Color 0", 1);
	METableSetFieldNotApplicable(pTable, pItemDef, "Dye Material", 1);
	METableSetFieldNotApplicable(pTable, pItemDef, "Gem Type", 1);
	METableHideSubTable(pTable, pItemDef, ieBonusNumericId, pItemDef->eType!=kItemType_Numeric);
	METableSetFieldNotApplicable(pTable, pItemDef, "Bonus Percent", (pItemDef->eType!=kItemType_Numeric));
    METableSetFieldNotApplicable(pTable, pItemDef, "Spending Numeric", (pItemDef->eType!=kItemType_Numeric));

	switch (pItemDef->eType)
	{
	case kItemType_Gem:
		METableSetFieldNotApplicable(pTable, pItemDef, "Gem Type", 0);
		break;
	case kItemType_Upgrade:
		METableHideSubTable(pTable, pItemDef, ieTrainableNodesId, 1);
		METableHideSubTable(pTable, pItemDef, iePowersId, 0);
		METableHideSubTable(pTable, pItemDef, ieComponentId, 1);
		METableHideSubTable(pTable, pItemDef, ieCostumeId, 0);
		METableHideSubTable(pTable, pItemDef, ieAttribId, 1);
		METableSetFieldNotApplicable(pTable, pItemDef, "Flags", 0);
		METableSetFieldNotApplicable(pTable, pItemDef, "Description", 0);
		METableSetFieldNotApplicable(pTable, pItemDef, "Icon", 0);
		METableSetFieldNotApplicable(pTable, pItemDef, "ItemArt", 0);
		METableSetFieldNotApplicable(pTable, pItemDef, "Mission", 0);
		METableSetFieldNotApplicable(pTable, pItemDef, "Slot", 0);
		METableSetFieldNotApplicable(pTable, pItemDef, "SlotIDType", 1);
		METableSetFieldNotApplicable(pTable, pItemDef, "SkillType", 0);
		METableSetFieldNotApplicable(pTable, pItemDef, "Subtarget Type", 0);
		METableSetFieldNotApplicable(pTable, pItemDef, "StackLimit", 0);
		METableSetFieldNotApplicable(pTable, pItemDef, "Costume Mode", 0);
		METableSetFieldNotApplicable(pTable, pItemDef, "Costume Priority",0);
		METableSetFieldNotApplicable(pTable, pItemDef, "Economy Points", 0);
		METableSetFieldNotApplicable(pTable, pItemDef, "Craft Recipe", 0);
		METableSetFieldNotApplicable(pTable, pItemDef, "Value Recipe", 0);
		METableSetFieldNotApplicable(pTable, pItemDef, "May Buy in Bulk", 0);
		METableSetFieldNotApplicable(pTable, pItemDef, "Level", (pItemDef->flags & (kItemDefFlag_LevelFromSource | kItemDefFlag_ScaleWhenBought)) == 0 ? 0 : 1);
		METableSetFieldNotApplicable(pTable, pItemDef, "Quality", !!(pItemDef->flags & kItemDefFlag_RandomAlgoQuality));
		METableSetFieldNotApplicable(pTable, pItemDef, "Group", 1);
		METableSetFieldNotApplicable(pTable, pItemDef, "Min Value", 1);
		METableSetFieldNotApplicable(pTable, pItemDef, "Max Value", 1);
		METableSetFieldNotApplicable(pTable, pItemDef, "Bag Size", 1);
		METableSetFieldNotApplicable(pTable, pItemDef, "Recipe Cost", 1);
		METableSetFieldNotApplicable(pTable, pItemDef, "Recipe Result Item", 1);
		METableSetFieldNotApplicable(pTable, pItemDef, "Recipe Result ItemPower", 1);
		METableSetFieldNotApplicable(pTable, pItemDef, "Recipe Result Count", 1);
		METableSetFieldNotApplicable(pTable, pItemDef, "Req Skill Type", 0);
		METableSetFieldNotApplicable(pTable, pItemDef, "Req Skill Level", 0);
		METableSetFieldNotApplicable(pTable, pItemDef, "Req Min Level", 0);
		METableSetFieldNotApplicable(pTable, pItemDef, "Req Max Level", 0);
		METableSetFieldNotApplicable(pTable, pItemDef, "Req Expr", 0);
		METableSetFieldNotApplicable(pTable, pItemDef, "Callout", 1);
		METableSetFieldNotApplicable(pTable, pItemDef, "CalloutFSM", 1);
		METableSetFieldNotApplicable(pTable, pItemDef, "Important Callout", 1);
		METableSetFieldNotApplicable(pTable, pItemDef, "Lore Category", 1);
		METableSetFieldNotApplicable(pTable, pItemDef, "Class Type",1);
		METableSetFieldNotApplicable(pTable, pItemDef, "Pet Def",1);
		METableSetFieldNotApplicable(pTable, pItemDef, "Algo Pet Def", 1);
		METableSetFieldNotApplicable(pTable, pItemDef, "Unidentified Description", !itemdef_IsUnidentified(pItemDef));
		METableSetFieldNotApplicable(pTable, pItemDef, "Unidentified Display Name", !itemdef_IsUnidentified(pItemDef));
		METableSetFieldNotApplicable(pTable, pItemDef, "Lore Journal Type", 1);
		METableSetFieldNotApplicable(pTable, pItemDef, "Lore Textures", 1);
		METableSetFieldNotApplicable(pTable, pItemDef, "Lore Critter Def", 1);
		METableSetFieldNotApplicable(pTable, pItemDef, "Equip Limit Max Count", 0);
		METableSetFieldNotApplicable(pTable, pItemDef, "Equip Limit Category", 0);
		break;

	case kItemType_Component:
		METableHideSubTable(pTable, pItemDef, ieTrainableNodesId, 1);
		METableHideSubTable(pTable, pItemDef, iePowersId, 1);
		METableHideSubTable(pTable, pItemDef, ieComponentId, 1);
		METableHideSubTable(pTable, pItemDef, ieCostumeId, 1);
		METableHideSubTable(pTable, pItemDef, ieAttribId, 1);
		METableSetFieldNotApplicable(pTable, pItemDef, "Flags", 0);
		METableSetFieldNotApplicable(pTable, pItemDef, "Description", 0);
		METableSetFieldNotApplicable(pTable, pItemDef, "Icon", 0);
		METableSetFieldNotApplicable(pTable, pItemDef, "ItemArt", 1);
		METableSetFieldNotApplicable(pTable, pItemDef, "Mission", 1);
		METableSetFieldNotApplicable(pTable, pItemDef, "Slot", 1);
		METableSetFieldNotApplicable(pTable, pItemDef, "SlotIDType", 1);
		METableSetFieldNotApplicable(pTable, pItemDef, "SkillType", 0);
		METableSetFieldNotApplicable(pTable, pItemDef, "Subtarget Type", 1);
		METableSetFieldNotApplicable(pTable, pItemDef, "StackLimit", 0);
		METableSetFieldNotApplicable(pTable, pItemDef, "Costume Mode", 1);
		METableSetFieldNotApplicable(pTable, pItemDef, "Costume Priority", 1);
		METableSetFieldNotApplicable(pTable, pItemDef, "Economy Points", 0);
		METableSetFieldNotApplicable(pTable, pItemDef, "Craft Recipe", 0);
		METableSetFieldNotApplicable(pTable, pItemDef, "Value Recipe", 0);
		METableSetFieldNotApplicable(pTable, pItemDef, "May Buy in Bulk", 0);
		METableSetFieldNotApplicable(pTable, pItemDef, "Group", 1);
		METableSetFieldNotApplicable(pTable, pItemDef, "Level", 1);
		METableSetFieldNotApplicable(pTable, pItemDef, "Quality", 0);
		METableSetFieldNotApplicable(pTable, pItemDef, "Min Value", 1);
		METableSetFieldNotApplicable(pTable, pItemDef, "Max Value", 1);
		METableSetFieldNotApplicable(pTable, pItemDef, "Bag Size", 1);
		METableSetFieldNotApplicable(pTable, pItemDef, "Recipe Cost", 1);
		METableSetFieldNotApplicable(pTable, pItemDef, "Recipe Result Item", 1);
		METableSetFieldNotApplicable(pTable, pItemDef, "Recipe Result ItemPower", 1);
		METableSetFieldNotApplicable(pTable, pItemDef, "Recipe Result Count", 1);
		METableSetFieldNotApplicable(pTable, pItemDef, "Req Skill Type", 0);
		METableSetFieldNotApplicable(pTable, pItemDef, "Req Skill Level", 0);
		METableSetFieldNotApplicable(pTable, pItemDef, "Req Min Level", 0);
		METableSetFieldNotApplicable(pTable, pItemDef, "Req Max Level", 0);
		METableSetFieldNotApplicable(pTable, pItemDef, "Req Expr", 0);
		METableSetFieldNotApplicable(pTable, pItemDef, "Callout", 1);
		METableSetFieldNotApplicable(pTable, pItemDef, "CalloutFSM", 1);
		METableSetFieldNotApplicable(pTable, pItemDef, "Important Callout", 1);
		METableSetFieldNotApplicable(pTable, pItemDef, "Lore Category", 1);
		METableSetFieldNotApplicable(pTable, pItemDef, "Class Type",1);
		METableSetFieldNotApplicable(pTable, pItemDef, "Pet Def",1);
		METableSetFieldNotApplicable(pTable, pItemDef, "Algo Pet Def", 1);
		METableSetFieldNotApplicable(pTable, pItemDef, "Unidentified Description", !itemdef_IsUnidentified(pItemDef));
		METableSetFieldNotApplicable(pTable, pItemDef, "Unidentified Display Name", !itemdef_IsUnidentified(pItemDef));
		METableSetFieldNotApplicable(pTable, pItemDef, "Lore Journal Type", 1);
		METableSetFieldNotApplicable(pTable, pItemDef, "Lore Textures", 1);
		METableSetFieldNotApplicable(pTable, pItemDef, "Lore Critter Def", 1);
		METableSetFieldNotApplicable(pTable, pItemDef, "Equip Limit Max Count", 1);
		METableSetFieldNotApplicable(pTable, pItemDef, "Equip Limit Category", 1);
		break;

	case kItemType_ItemRecipe:
		METableHideSubTable(pTable, pItemDef, ieTrainableNodesId, 1);
		METableHideSubTable(pTable, pItemDef, iePowersId, 1);
		METableHideSubTable(pTable, pItemDef, ieComponentId, 0);
		METableHideSubTable(pTable, pItemDef, ieCostumeId, 1);
		METableHideSubTable(pTable, pItemDef, ieAttribId, 1);
		METableSetFieldNotApplicable(pTable, pItemDef, "Flags", 0);
		METableSetFieldNotApplicable(pTable, pItemDef, "Description", 0);
		METableSetFieldNotApplicable(pTable, pItemDef, "Icon", 0);
		METableSetFieldNotApplicable(pTable, pItemDef, "ItemArt", 1);
		METableSetFieldNotApplicable(pTable, pItemDef, "Mission", 1);
		METableSetFieldNotApplicable(pTable, pItemDef, "Slot", 1);
		METableSetFieldNotApplicable(pTable, pItemDef, "SlotIDType", 1);
		METableSetFieldNotApplicable(pTable, pItemDef, "SkillType", 0);
		METableSetFieldNotApplicable(pTable, pItemDef, "Subtarget Type", 1);
		METableSetFieldNotApplicable(pTable, pItemDef, "StackLimit", 1);
		METableSetFieldNotApplicable(pTable, pItemDef, "Costume Mode", 1);
		METableSetFieldNotApplicable(pTable, pItemDef, "Costume Priority", 1);
		METableSetFieldNotApplicable(pTable, pItemDef, "Economy Points", 0);
		METableSetFieldNotApplicable(pTable, pItemDef, "Craft Recipe", 1);
		METableSetFieldNotApplicable(pTable, pItemDef, "Value Recipe", 0);
		METableSetFieldNotApplicable(pTable, pItemDef, "May Buy in Bulk", 0);
		METableSetFieldNotApplicable(pTable, pItemDef, "Level", 0);
		METableSetFieldNotApplicable(pTable, pItemDef, "Group", 0);
		METableSetFieldNotApplicable(pTable, pItemDef, "Quality", 1);
		METableSetFieldNotApplicable(pTable, pItemDef, "Min Value", 1);
		METableSetFieldNotApplicable(pTable, pItemDef, "Max Value", 1);
		METableSetFieldNotApplicable(pTable, pItemDef, "Bag Size", 1);
		METableSetFieldNotApplicable(pTable, pItemDef, "Recipe Cost", 0);
		METableSetFieldNotApplicable(pTable, pItemDef, "Recipe Result Item", 0);
		METableSetFieldNotApplicable(pTable, pItemDef, "Recipe Result ItemPower", 1);
		METableSetFieldNotApplicable(pTable, pItemDef, "Recipe Result Count", 0);
		METableSetFieldNotApplicable(pTable, pItemDef, "Req Skill Type", 0);
		METableSetFieldNotApplicable(pTable, pItemDef, "Req Skill Level", 0);
		METableSetFieldNotApplicable(pTable, pItemDef, "Req Min Level", 0);
		METableSetFieldNotApplicable(pTable, pItemDef, "Req Max Level", 0);
		METableSetFieldNotApplicable(pTable, pItemDef, "Req Expr", 0);
		METableSetFieldNotApplicable(pTable, pItemDef, "Callout", 1);
		METableSetFieldNotApplicable(pTable, pItemDef, "CalloutFSM", 1);
		METableSetFieldNotApplicable(pTable, pItemDef, "Important Callout", 1);
		METableSetFieldNotApplicable(pTable, pItemDef, "Lore Category", 1);
		METableSetFieldNotApplicable(pTable, pItemDef, "Class Type",1);
		METableSetFieldNotApplicable(pTable, pItemDef, "Pet Def",1);
		METableSetFieldNotApplicable(pTable, pItemDef, "Algo Pet Def", 1);
		METableSetFieldNotApplicable(pTable, pItemDef, "Unidentified Description", !itemdef_IsUnidentified(pItemDef));
		METableSetFieldNotApplicable(pTable, pItemDef, "Unidentified Display Name", !itemdef_IsUnidentified(pItemDef));
		METableSetFieldNotApplicable(pTable, pItemDef, "Lore Journal Type", 1);
		METableSetFieldNotApplicable(pTable, pItemDef, "Lore Textures", 1);
		METableSetFieldNotApplicable(pTable, pItemDef, "Lore Critter Def", 1);
		METableSetFieldNotApplicable(pTable, pItemDef, "Equip Limit Max Count", 1);
		METableSetFieldNotApplicable(pTable, pItemDef, "Equip Limit Category", 1);
		break;

	case kItemType_ItemValue:
		METableHideSubTable(pTable, pItemDef, ieTrainableNodesId, 1);
		METableHideSubTable(pTable, pItemDef, iePowersId, 1);
		METableHideSubTable(pTable, pItemDef, ieComponentId, 0);
		METableHideSubTable(pTable, pItemDef, ieCostumeId, 1);
		METableHideSubTable(pTable, pItemDef, ieAttribId, 1);
		METableSetFieldNotApplicable(pTable, pItemDef, "Flags", 0);
		METableSetFieldNotApplicable(pTable, pItemDef, "Description", 0);
		METableSetFieldNotApplicable(pTable, pItemDef, "Icon", 0);
		METableSetFieldNotApplicable(pTable, pItemDef, "ItemArt", 1);
		METableSetFieldNotApplicable(pTable, pItemDef, "Mission", 1);
		METableSetFieldNotApplicable(pTable, pItemDef, "Slot", 1);
		METableSetFieldNotApplicable(pTable, pItemDef, "SlotIDType", 1);
		METableSetFieldNotApplicable(pTable, pItemDef, "SkillType", 0);
		METableSetFieldNotApplicable(pTable, pItemDef, "Subtarget Type", 1);
		METableSetFieldNotApplicable(pTable, pItemDef, "StackLimit", 1);
		METableSetFieldNotApplicable(pTable, pItemDef, "Costume Mode", 1);
		METableSetFieldNotApplicable(pTable, pItemDef, "Costume Priority", 1);
		METableSetFieldNotApplicable(pTable, pItemDef, "Economy Points", 0);
		METableSetFieldNotApplicable(pTable, pItemDef, "Craft Recipe", 1);
		METableSetFieldNotApplicable(pTable, pItemDef, "Value Recipe", 0);
		METableSetFieldNotApplicable(pTable, pItemDef, "May Buy in Bulk", 1);
		METableSetFieldNotApplicable(pTable, pItemDef, "Level", 0);
		METableSetFieldNotApplicable(pTable, pItemDef, "Group", 1);
		METableSetFieldNotApplicable(pTable, pItemDef, "Quality", 1);
		METableSetFieldNotApplicable(pTable, pItemDef, "Min Value", 1);
		METableSetFieldNotApplicable(pTable, pItemDef, "Max Value", 1);
		METableSetFieldNotApplicable(pTable, pItemDef, "Bag Size", 1);
		METableSetFieldNotApplicable(pTable, pItemDef, "Recipe Cost", 0);
		METableSetFieldNotApplicable(pTable, pItemDef, "Recipe Result Item", 0);
		METableSetFieldNotApplicable(pTable, pItemDef, "Recipe Result ItemPower", 1);
		METableSetFieldNotApplicable(pTable, pItemDef, "Recipe Result Count", 0);
		METableSetFieldNotApplicable(pTable, pItemDef, "Req Skill Type", 0);
		METableSetFieldNotApplicable(pTable, pItemDef, "Req Skill Level", 0);
		METableSetFieldNotApplicable(pTable, pItemDef, "Req Min Level", 0);
		METableSetFieldNotApplicable(pTable, pItemDef, "Req Max Level", 0);
		METableSetFieldNotApplicable(pTable, pItemDef, "Req Expr", 0);
		METableSetFieldNotApplicable(pTable, pItemDef, "Callout", 1);
		METableSetFieldNotApplicable(pTable, pItemDef, "CalloutFSM", 1);
		METableSetFieldNotApplicable(pTable, pItemDef, "Important Callout", 1);
		METableSetFieldNotApplicable(pTable, pItemDef, "Lore Category", 1);
		METableSetFieldNotApplicable(pTable, pItemDef, "Class Type",1);
		METableSetFieldNotApplicable(pTable, pItemDef, "Pet Def",1);
		METableSetFieldNotApplicable(pTable, pItemDef, "Algo Pet Def", 1);
		METableSetFieldNotApplicable(pTable, pItemDef, "Unidentified Description", !itemdef_IsUnidentified(pItemDef));
		METableSetFieldNotApplicable(pTable, pItemDef, "Unidentified Display Name", !itemdef_IsUnidentified(pItemDef));
		METableSetFieldNotApplicable(pTable, pItemDef, "Lore Journal Type", 1);
		METableSetFieldNotApplicable(pTable, pItemDef, "Lore Textures", 1);
		METableSetFieldNotApplicable(pTable, pItemDef, "Lore Critter Def", 1);
		METableSetFieldNotApplicable(pTable, pItemDef, "Equip Limit Max Count", 1);
		METableSetFieldNotApplicable(pTable, pItemDef, "Equip Limit Category", 1);
		break;

	case kItemType_ItemPowerRecipe:
		METableHideSubTable(pTable, pItemDef, ieTrainableNodesId, 1);
		METableHideSubTable(pTable, pItemDef, iePowersId, 1);
		METableHideSubTable(pTable, pItemDef, ieComponentId, 0);
		METableHideSubTable(pTable, pItemDef, ieCostumeId, 1);
		METableHideSubTable(pTable, pItemDef, ieAttribId, 1);
		METableSetFieldNotApplicable(pTable, pItemDef, "Flags", 0);
		METableSetFieldNotApplicable(pTable, pItemDef, "Description", 0);
		METableSetFieldNotApplicable(pTable, pItemDef, "Icon", 0);
		METableSetFieldNotApplicable(pTable, pItemDef, "ItemArt", 1);
		METableSetFieldNotApplicable(pTable, pItemDef, "Mission", 1);
		METableSetFieldNotApplicable(pTable, pItemDef, "Slot", 1);
		METableSetFieldNotApplicable(pTable, pItemDef, "SlotIDType", 1);
		METableSetFieldNotApplicable(pTable, pItemDef, "SkillType", 0);
		METableSetFieldNotApplicable(pTable, pItemDef, "Subtarget Type", 1);
		METableSetFieldNotApplicable(pTable, pItemDef, "StackLimit", 1);
		METableSetFieldNotApplicable(pTable, pItemDef, "Costume Mode", 1);
		METableSetFieldNotApplicable(pTable, pItemDef, "Costume Priority", 1);
		METableSetFieldNotApplicable(pTable, pItemDef, "Economy Points", 0);
		METableSetFieldNotApplicable(pTable, pItemDef, "Craft Recipe", 1);
		METableSetFieldNotApplicable(pTable, pItemDef, "Value Recipe", 0);
		METableSetFieldNotApplicable(pTable, pItemDef, "May Buy in Bulk", 0);
		METableSetFieldNotApplicable(pTable, pItemDef, "Level", 0);
		METableSetFieldNotApplicable(pTable, pItemDef, "Group", 0);
		METableSetFieldNotApplicable(pTable, pItemDef, "Quality", 1);
		METableSetFieldNotApplicable(pTable, pItemDef, "Min Value", 1);
		METableSetFieldNotApplicable(pTable, pItemDef, "Max Value", 1);
		METableSetFieldNotApplicable(pTable, pItemDef, "Bag Size", 1);
		METableSetFieldNotApplicable(pTable, pItemDef, "Recipe Cost", 0);
		METableSetFieldNotApplicable(pTable, pItemDef, "Recipe Result Item", 1);
		METableSetFieldNotApplicable(pTable, pItemDef, "Recipe Result ItemPower", 0);
		METableSetFieldNotApplicable(pTable, pItemDef, "Recipe Result Count", 0);
		METableSetFieldNotApplicable(pTable, pItemDef, "Req Skill Type", 0);
		METableSetFieldNotApplicable(pTable, pItemDef, "Req Skill Level", 0);
		METableSetFieldNotApplicable(pTable, pItemDef, "Req Min Level", 0);
		METableSetFieldNotApplicable(pTable, pItemDef, "Req Max Level", 0);
		METableSetFieldNotApplicable(pTable, pItemDef, "Req Expr", 0);
		METableSetFieldNotApplicable(pTable, pItemDef, "Callout", 1);
		METableSetFieldNotApplicable(pTable, pItemDef, "CalloutFSM", 1);
		METableSetFieldNotApplicable(pTable, pItemDef, "Important Callout", 1);
		METableSetFieldNotApplicable(pTable, pItemDef, "Lore Category", 1);
		METableSetFieldNotApplicable(pTable, pItemDef, "Class Type",1);
		METableSetFieldNotApplicable(pTable, pItemDef, "Pet Def",1);
		METableSetFieldNotApplicable(pTable, pItemDef, "Algo Pet Def", 1);
		METableSetFieldNotApplicable(pTable, pItemDef, "Unidentified Description", !itemdef_IsUnidentified(pItemDef));
		METableSetFieldNotApplicable(pTable, pItemDef, "Unidentified Display Name", !itemdef_IsUnidentified(pItemDef));
		METableSetFieldNotApplicable(pTable, pItemDef, "Lore Journal Type", 1);
		METableSetFieldNotApplicable(pTable, pItemDef, "Lore Textures", 1);
		METableSetFieldNotApplicable(pTable, pItemDef, "Lore Critter Def", 1);
		METableSetFieldNotApplicable(pTable, pItemDef, "Equip Limit Max Count", 1);
		METableSetFieldNotApplicable(pTable, pItemDef, "Equip Limit Category", 1);
		break;
		
	case kItemType_Mission:
		METableHideSubTable(pTable, pItemDef, ieTrainableNodesId, 1);
		METableHideSubTable(pTable, pItemDef, iePowersId, 1);
		METableHideSubTable(pTable, pItemDef, ieComponentId, 1);
		METableHideSubTable(pTable, pItemDef, ieCostumeId, 1);
		METableHideSubTable(pTable, pItemDef, ieAttribId, 1);
		METableSetFieldNotApplicable(pTable, pItemDef, "Flags", 0);
		METableSetFieldNotApplicable(pTable, pItemDef, "Description", 0);
		METableSetFieldNotApplicable(pTable, pItemDef, "Icon", 0);
		METableSetFieldNotApplicable(pTable, pItemDef, "ItemArt", 1);
		METableSetFieldNotApplicable(pTable, pItemDef, "Mission", 0);
		METableSetFieldNotApplicable(pTable, pItemDef, "Slot", 1);
		METableSetFieldNotApplicable(pTable, pItemDef, "SlotIDType", 1);
		METableSetFieldNotApplicable(pTable, pItemDef, "SkillType", 1);
		METableSetFieldNotApplicable(pTable, pItemDef, "Subtarget Type", 1);
		METableSetFieldNotApplicable(pTable, pItemDef, "StackLimit", 0);
		METableSetFieldNotApplicable(pTable, pItemDef, "Costume Mode", 1);
		METableSetFieldNotApplicable(pTable, pItemDef, "Costume Priority", 1);
		METableSetFieldNotApplicable(pTable, pItemDef, "Economy Points", 1);
		METableSetFieldNotApplicable(pTable, pItemDef, "Craft Recipe", 1);
		METableSetFieldNotApplicable(pTable, pItemDef, "Value Recipe", 1);
		METableSetFieldNotApplicable(pTable, pItemDef, "May Buy in Bulk", 0);
		METableSetFieldNotApplicable(pTable, pItemDef, "Level", 1);
		METableSetFieldNotApplicable(pTable, pItemDef, "Group", 1);
		METableSetFieldNotApplicable(pTable, pItemDef, "Quality", 0);
		METableSetFieldNotApplicable(pTable, pItemDef, "Min Value", 1);
		METableSetFieldNotApplicable(pTable, pItemDef, "Max Value", 1);
		METableSetFieldNotApplicable(pTable, pItemDef, "Bag Size", 1);
		METableSetFieldNotApplicable(pTable, pItemDef, "Recipe Cost", 1);
		METableSetFieldNotApplicable(pTable, pItemDef, "Recipe Result Item", 1);
		METableSetFieldNotApplicable(pTable, pItemDef, "Recipe Result ItemPower", 1);
		METableSetFieldNotApplicable(pTable, pItemDef, "Recipe Result Count", 1);
		METableSetFieldNotApplicable(pTable, pItemDef, "Req Skill Type", 0);
		METableSetFieldNotApplicable(pTable, pItemDef, "Req Skill Level", 0);
		METableSetFieldNotApplicable(pTable, pItemDef, "Req Min Level", 0);
		METableSetFieldNotApplicable(pTable, pItemDef, "Req Max Level", 0);
		METableSetFieldNotApplicable(pTable, pItemDef, "Req Expr", 0);
		METableSetFieldNotApplicable(pTable, pItemDef, "Callout", 0);
		METableSetFieldNotApplicable(pTable, pItemDef, "CalloutFSM", 0);
		METableSetFieldNotApplicable(pTable, pItemDef, "Important Callout", 0);
		METableSetFieldNotApplicable(pTable, pItemDef, "Lore Category", 1);
		METableSetFieldNotApplicable(pTable, pItemDef, "Class Type",1);
		METableSetFieldNotApplicable(pTable, pItemDef, "Pet Def",1);
		METableSetFieldNotApplicable(pTable, pItemDef, "Algo Pet Def", 1);
		METableSetFieldNotApplicable(pTable, pItemDef, "Unidentified Description", !itemdef_IsUnidentified(pItemDef));
		METableSetFieldNotApplicable(pTable, pItemDef, "Unidentified Display Name", !itemdef_IsUnidentified(pItemDef));
		METableSetFieldNotApplicable(pTable, pItemDef, "Lore Journal Type", 1);
		METableSetFieldNotApplicable(pTable, pItemDef, "Lore Textures", 1);
		METableSetFieldNotApplicable(pTable, pItemDef, "Lore Critter Def", 1);
		METableSetFieldNotApplicable(pTable, pItemDef, "Equip Limit Max Count", 1);
		METableSetFieldNotApplicable(pTable, pItemDef, "Equip Limit Category", 1);
		break;
		
	case kItemType_MissionGrant:
		METableHideSubTable(pTable, pItemDef, ieTrainableNodesId, 1);
		METableHideSubTable(pTable, pItemDef, iePowersId, 1);
		METableHideSubTable(pTable, pItemDef, ieComponentId, 1);
		METableHideSubTable(pTable, pItemDef, ieCostumeId, 1);
		METableHideSubTable(pTable, pItemDef, ieAttribId, 1);
		METableSetFieldNotApplicable(pTable, pItemDef, "Flags", 0);
		METableSetFieldNotApplicable(pTable, pItemDef, "Description", 0);
		METableSetFieldNotApplicable(pTable, pItemDef, "Icon", 0);
		METableSetFieldNotApplicable(pTable, pItemDef, "ItemArt", 1);
		METableSetFieldNotApplicable(pTable, pItemDef, "Mission", 0);
		METableSetFieldNotApplicable(pTable, pItemDef, "Slot", 1);
		METableSetFieldNotApplicable(pTable, pItemDef, "SlotIDType", 1);
		METableSetFieldNotApplicable(pTable, pItemDef, "SkillType", 1);
		METableSetFieldNotApplicable(pTable, pItemDef, "Subtarget Type", 1);
		METableSetFieldNotApplicable(pTable, pItemDef, "StackLimit", 1);
		METableSetFieldNotApplicable(pTable, pItemDef, "Costume Mode", 1);
		METableSetFieldNotApplicable(pTable, pItemDef, "Costume Priority", 1);
		METableSetFieldNotApplicable(pTable, pItemDef, "Economy Points", 0);
		METableSetFieldNotApplicable(pTable, pItemDef, "Craft Recipe", 0);
		METableSetFieldNotApplicable(pTable, pItemDef, "Value Recipe", 0);
		METableSetFieldNotApplicable(pTable, pItemDef, "May Buy in Bulk", 0);
		METableSetFieldNotApplicable(pTable, pItemDef, "Level", 1);
		METableSetFieldNotApplicable(pTable, pItemDef, "Group", 1);
		METableSetFieldNotApplicable(pTable, pItemDef, "Quality", 1);
		METableSetFieldNotApplicable(pTable, pItemDef, "Min Value", 1);
		METableSetFieldNotApplicable(pTable, pItemDef, "Max Value", 1);
		METableSetFieldNotApplicable(pTable, pItemDef, "Bag Size", 1);
		METableSetFieldNotApplicable(pTable, pItemDef, "Recipe Cost", 1);
		METableSetFieldNotApplicable(pTable, pItemDef, "Recipe Result Item", 1);
		METableSetFieldNotApplicable(pTable, pItemDef, "Recipe Result ItemPower", 1);
		METableSetFieldNotApplicable(pTable, pItemDef, "Recipe Result Count", 1);
		METableSetFieldNotApplicable(pTable, pItemDef, "Req Skill Type", 0);
		METableSetFieldNotApplicable(pTable, pItemDef, "Req Skill Level", 0);
		METableSetFieldNotApplicable(pTable, pItemDef, "Req Min Level", 0);
		METableSetFieldNotApplicable(pTable, pItemDef, "Req Max Level", 0);
		METableSetFieldNotApplicable(pTable, pItemDef, "Req Expr", 0);
		METableSetFieldNotApplicable(pTable, pItemDef, "Callout", 1);
		METableSetFieldNotApplicable(pTable, pItemDef, "CalloutFSM", 1);
		METableSetFieldNotApplicable(pTable, pItemDef, "Important Callout", 1);
		METableSetFieldNotApplicable(pTable, pItemDef, "Lore Category", 1);
		METableSetFieldNotApplicable(pTable, pItemDef, "Class Type",1);
		METableSetFieldNotApplicable(pTable, pItemDef, "Pet Def",1);
		METableSetFieldNotApplicable(pTable, pItemDef, "Algo Pet Def", 1);
		METableSetFieldNotApplicable(pTable, pItemDef, "Unidentified Description", !itemdef_IsUnidentified(pItemDef));
		METableSetFieldNotApplicable(pTable, pItemDef, "Unidentified Display Name", !itemdef_IsUnidentified(pItemDef));
		METableSetFieldNotApplicable(pTable, pItemDef, "Lore Journal Type", 1);
		METableSetFieldNotApplicable(pTable, pItemDef, "Lore Textures", 1);
		METableSetFieldNotApplicable(pTable, pItemDef, "Lore Critter Def", 1);
		METableSetFieldNotApplicable(pTable, pItemDef, "Equip Limit Max Count", 1);
		METableSetFieldNotApplicable(pTable, pItemDef, "Equip Limit Category", 1);
		break;
		
	case kItemType_Boost:
		METableHideSubTable(pTable, pItemDef, ieTrainableNodesId, 1);
		METableHideSubTable(pTable, pItemDef, iePowersId, 0);
		METableHideSubTable(pTable, pItemDef, ieComponentId, 1);
		METableHideSubTable(pTable, pItemDef, ieCostumeId, 1);
		METableHideSubTable(pTable, pItemDef, ieAttribId, 1);
		METableSetFieldNotApplicable(pTable, pItemDef, "Flags", 0);
		METableSetFieldNotApplicable(pTable, pItemDef, "Description", 0);
		METableSetFieldNotApplicable(pTable, pItemDef, "Icon", 0);
		METableSetFieldNotApplicable(pTable, pItemDef, "ItemArt", 1);
		METableSetFieldNotApplicable(pTable, pItemDef, "Mission", 1);
		METableSetFieldNotApplicable(pTable, pItemDef, "Slot", 1);
		METableSetFieldNotApplicable(pTable, pItemDef, "SlotIDType", 1);
		METableSetFieldNotApplicable(pTable, pItemDef, "SkillType", 0);
		METableSetFieldNotApplicable(pTable, pItemDef, "Subtarget Type", 1);
		METableSetFieldNotApplicable(pTable, pItemDef, "StackLimit", 1);
		METableSetFieldNotApplicable(pTable, pItemDef, "Costume Mode", 1);
		METableSetFieldNotApplicable(pTable, pItemDef, "Costume Priority", 1);
		METableSetFieldNotApplicable(pTable, pItemDef, "Economy Points", 1);
		METableSetFieldNotApplicable(pTable, pItemDef, "Craft Recipe", 0);
		METableSetFieldNotApplicable(pTable, pItemDef, "Value Recipe", 0);
		METableSetFieldNotApplicable(pTable, pItemDef, "May Buy in Bulk", 1);
		METableSetFieldNotApplicable(pTable, pItemDef, "Level", 1);
		METableSetFieldNotApplicable(pTable, pItemDef, "Group", 1);
		METableSetFieldNotApplicable(pTable, pItemDef, "Quality", 1);
		METableSetFieldNotApplicable(pTable, pItemDef, "Min Value", 1);
		METableSetFieldNotApplicable(pTable, pItemDef, "Max Value", 1);
		METableSetFieldNotApplicable(pTable, pItemDef, "Bag Size", 1);
		METableSetFieldNotApplicable(pTable, pItemDef, "Recipe Cost", 1);
		METableSetFieldNotApplicable(pTable, pItemDef, "Recipe Result Item", 1);
		METableSetFieldNotApplicable(pTable, pItemDef, "Recipe Result ItemPower", 1);
		METableSetFieldNotApplicable(pTable, pItemDef, "Recipe Result Count", 1);
		METableSetFieldNotApplicable(pTable, pItemDef, "Req Skill Type", 0);
		METableSetFieldNotApplicable(pTable, pItemDef, "Req Skill Level", 0);
		METableSetFieldNotApplicable(pTable, pItemDef, "Req Min Level", 0);
		METableSetFieldNotApplicable(pTable, pItemDef, "Req Max Level", 0);
		METableSetFieldNotApplicable(pTable, pItemDef, "Req Expr", 0);
		METableSetFieldNotApplicable(pTable, pItemDef, "Callout", 1);
		METableSetFieldNotApplicable(pTable, pItemDef, "CalloutFSM", 1);
		METableSetFieldNotApplicable(pTable, pItemDef, "Important Callout", 1);
		METableSetFieldNotApplicable(pTable, pItemDef, "Lore Category", 1);
		METableSetFieldNotApplicable(pTable, pItemDef, "Class Type",1);
		METableSetFieldNotApplicable(pTable, pItemDef, "Pet Def",1);
		METableSetFieldNotApplicable(pTable, pItemDef, "Algo Pet Def", 1);
		METableSetFieldNotApplicable(pTable, pItemDef, "Unidentified Description", !itemdef_IsUnidentified(pItemDef));
		METableSetFieldNotApplicable(pTable, pItemDef, "Unidentified Display Name", !itemdef_IsUnidentified(pItemDef));
		METableSetFieldNotApplicable(pTable, pItemDef, "Lore Journal Type", 1);
		METableSetFieldNotApplicable(pTable, pItemDef, "Lore Textures", 1);
		METableSetFieldNotApplicable(pTable, pItemDef, "Lore Critter Def", 1);
		METableSetFieldNotApplicable(pTable, pItemDef, "Equip Limit Max Count", 1);
		METableSetFieldNotApplicable(pTable, pItemDef, "Equip Limit Category", 1);
		break;
		
	case kItemType_Device:
		METableHideSubTable(pTable, pItemDef, ieTrainableNodesId, 0);
		METableHideSubTable(pTable, pItemDef, iePowersId, 0);
		METableHideSubTable(pTable, pItemDef, ieComponentId, 1);
		METableHideSubTable(pTable, pItemDef, ieCostumeId, 1);
		METableHideSubTable(pTable, pItemDef, ieAttribId, 1);
		METableSetFieldNotApplicable(pTable, pItemDef, "Flags", 0);
		METableSetFieldNotApplicable(pTable, pItemDef, "Description", 0);
		METableSetFieldNotApplicable(pTable, pItemDef, "Icon", 0);
		METableSetFieldNotApplicable(pTable, pItemDef, "ItemArt", 1);
		METableSetFieldNotApplicable(pTable, pItemDef, "Mission", 0);
		METableSetFieldNotApplicable(pTable, pItemDef, "Slot", 0);
		METableSetFieldNotApplicable(pTable, pItemDef, "SlotIDType", 0);
		METableSetFieldNotApplicable(pTable, pItemDef, "Subtarget Type", 0);
		METableSetFieldNotApplicable(pTable, pItemDef, "SkillType", 0);
		METableSetFieldNotApplicable(pTable, pItemDef, "StackLimit", 0);
		METableSetFieldNotApplicable(pTable, pItemDef, "Costume Mode", 0);
		METableSetFieldNotApplicable(pTable, pItemDef, "Costume Priority", 0);
		METableSetFieldNotApplicable(pTable, pItemDef, "Economy Points", 0);
		METableSetFieldNotApplicable(pTable, pItemDef, "Craft Recipe", 0);
		METableSetFieldNotApplicable(pTable, pItemDef, "Value Recipe", 0);
		METableSetFieldNotApplicable(pTable, pItemDef, "May Buy in Bulk", 0);
		METableSetFieldNotApplicable(pTable, pItemDef, "Level", 0);
		METableSetFieldNotApplicable(pTable, pItemDef, "Group", 1);
		METableSetFieldNotApplicable(pTable, pItemDef, "Quality", 0);
		METableSetFieldNotApplicable(pTable, pItemDef, "Min Value", 1);
		METableSetFieldNotApplicable(pTable, pItemDef, "Max Value", 1);
		METableSetFieldNotApplicable(pTable, pItemDef, "Bag Size", 1);
		METableSetFieldNotApplicable(pTable, pItemDef, "Recipe Cost", 1);
		METableSetFieldNotApplicable(pTable, pItemDef, "Recipe Result Item", 1);
		METableSetFieldNotApplicable(pTable, pItemDef, "Recipe Result ItemPower", 1);
		METableSetFieldNotApplicable(pTable, pItemDef, "Recipe Result Count", 1);
		METableSetFieldNotApplicable(pTable, pItemDef, "Req Skill Type", 0);
		METableSetFieldNotApplicable(pTable, pItemDef, "Req Skill Level", 0);
		METableSetFieldNotApplicable(pTable, pItemDef, "Req Min Level", 0);
		METableSetFieldNotApplicable(pTable, pItemDef, "Req Max Level", 0);
		METableSetFieldNotApplicable(pTable, pItemDef, "Req Expr", 0);
		METableSetFieldNotApplicable(pTable, pItemDef, "Callout", 0);
		METableSetFieldNotApplicable(pTable, pItemDef, "CalloutFSM", 0);
		METableSetFieldNotApplicable(pTable, pItemDef, "Important Callout", 0);
		METableSetFieldNotApplicable(pTable, pItemDef, "Lore Category", 1);
		METableSetFieldNotApplicable(pTable, pItemDef, "Class Type",1);
		METableSetFieldNotApplicable(pTable, pItemDef, "Pet Def",1);
		METableSetFieldNotApplicable(pTable, pItemDef, "Algo Pet Def", 1);
		METableSetFieldNotApplicable(pTable, pItemDef, "Unidentified Description", !itemdef_IsUnidentified(pItemDef));
		METableSetFieldNotApplicable(pTable, pItemDef, "Unidentified Display Name", !itemdef_IsUnidentified(pItemDef));
		METableSetFieldNotApplicable(pTable, pItemDef, "Lore Journal Type", 1);
		METableSetFieldNotApplicable(pTable, pItemDef, "Lore Textures", 1);
		METableSetFieldNotApplicable(pTable, pItemDef, "Lore Critter Def", 1);
		METableSetFieldNotApplicable(pTable, pItemDef, "Equip Limit Max Count", 0);
		METableSetFieldNotApplicable(pTable, pItemDef, "Equip Limit Category", 0);
		break;
		
	case kItemType_Numeric:
		METableHideSubTable(pTable, pItemDef, ieTrainableNodesId, 1);
		METableHideSubTable(pTable, pItemDef, iePowersId, 1);
		METableHideSubTable(pTable, pItemDef, ieComponentId, 1);
		METableHideSubTable(pTable, pItemDef, ieCostumeId, 1);
		METableHideSubTable(pTable, pItemDef, ieAttribId, 1);
		METableSetFieldNotApplicable(pTable, pItemDef, "Flags", 0);
		METableSetFieldNotApplicable(pTable, pItemDef, "Description", 0);
		METableSetFieldNotApplicable(pTable, pItemDef, "Icon", 0);
		METableSetFieldNotApplicable(pTable, pItemDef, "ItemArt", 1);
		METableSetFieldNotApplicable(pTable, pItemDef, "Mission", 1);
		METableSetFieldNotApplicable(pTable, pItemDef, "Slot", 1);
		METableSetFieldNotApplicable(pTable, pItemDef, "SlotIDType", 1);
		METableSetFieldNotApplicable(pTable, pItemDef, "SkillType", 1);
		METableSetFieldNotApplicable(pTable, pItemDef, "Subtarget Type", 1);
		METableSetFieldNotApplicable(pTable, pItemDef, "StackLimit", 1);
		METableSetFieldNotApplicable(pTable, pItemDef, "Costume Mode", 1);
		METableSetFieldNotApplicable(pTable, pItemDef, "Costume Priority", 1);
		METableSetFieldNotApplicable(pTable, pItemDef, "Economy Points", 0);
		METableSetFieldNotApplicable(pTable, pItemDef, "Craft Recipe", 0);
		METableSetFieldNotApplicable(pTable, pItemDef, "Value Recipe", 0);
		METableSetFieldNotApplicable(pTable, pItemDef, "May Buy in Bulk", 1);
		METableSetFieldNotApplicable(pTable, pItemDef, "Level", 1);
		METableSetFieldNotApplicable(pTable, pItemDef, "Group", 1);
		METableSetFieldNotApplicable(pTable, pItemDef, "Quality", 1);
		METableSetFieldNotApplicable(pTable, pItemDef, "Min Value", 0);
		METableSetFieldNotApplicable(pTable, pItemDef, "Max Value", 0);
		METableSetFieldNotApplicable(pTable, pItemDef, "Bag Size", 1);
		METableSetFieldNotApplicable(pTable, pItemDef, "Recipe Cost", 1);
		METableSetFieldNotApplicable(pTable, pItemDef, "Recipe Result Item", 1);
		METableSetFieldNotApplicable(pTable, pItemDef, "Recipe Result ItemPower", 1);
		METableSetFieldNotApplicable(pTable, pItemDef, "Recipe Result Count", 1);
		METableSetFieldNotApplicable(pTable, pItemDef, "Req Skill Type", 0);
		METableSetFieldNotApplicable(pTable, pItemDef, "Req Skill Level", 0);
		METableSetFieldNotApplicable(pTable, pItemDef, "Req Min Level", 0);
		METableSetFieldNotApplicable(pTable, pItemDef, "Req Max Level", 0);
		METableSetFieldNotApplicable(pTable, pItemDef, "Req Expr", 0);
		METableSetFieldNotApplicable(pTable, pItemDef, "Callout", 1);
		METableSetFieldNotApplicable(pTable, pItemDef, "CalloutFSM", 1);
		METableSetFieldNotApplicable(pTable, pItemDef, "Important Callout", 1);
		METableSetFieldNotApplicable(pTable, pItemDef, "Lore Category", 1);
		METableSetFieldNotApplicable(pTable, pItemDef, "Class Type",1);
		METableSetFieldNotApplicable(pTable, pItemDef, "Pet Def",1);
		METableSetFieldNotApplicable(pTable, pItemDef, "Algo Pet Def", 1);
		METableSetFieldNotApplicable(pTable, pItemDef, "Lore Journal Type", 1);
		METableSetFieldNotApplicable(pTable, pItemDef, "Lore Textures", 1);
		METableSetFieldNotApplicable(pTable, pItemDef, "Lore Critter Def", 1);
		METableSetFieldNotApplicable(pTable, pItemDef, "Equip Limit Max Count", 1);
		METableSetFieldNotApplicable(pTable, pItemDef, "Equip Limit Category", 1);
		break;
		
	case kItemType_Weapon:
		METableHideSubTable(pTable, pItemDef, ieTrainableNodesId, 1);
		METableHideSubTable(pTable, pItemDef, iePowersId, 0);
		METableHideSubTable(pTable, pItemDef, ieComponentId, 1);
		METableHideSubTable(pTable, pItemDef, ieCostumeId, 0);
		METableHideSubTable(pTable, pItemDef, ieAttribId, 1);
		METableSetFieldNotApplicable(pTable, pItemDef, "Flags", 0);
		METableSetFieldNotApplicable(pTable, pItemDef, "Description", 0);
		METableSetFieldNotApplicable(pTable, pItemDef, "Icon", 0);
		METableSetFieldNotApplicable(pTable, pItemDef, "ItemArt", 0);
		METableSetFieldNotApplicable(pTable, pItemDef, "Mission", 0);
		METableSetFieldNotApplicable(pTable, pItemDef, "Slot", 0);
		METableSetFieldNotApplicable(pTable, pItemDef, "SlotIDType", 0);
		METableSetFieldNotApplicable(pTable, pItemDef, "SkillType", 0);
		METableSetFieldNotApplicable(pTable, pItemDef, "Subtarget Type", 0);
		METableSetFieldNotApplicable(pTable, pItemDef, "StackLimit", 1);
		METableSetFieldNotApplicable(pTable, pItemDef, "Costume Mode", 0);
		METableSetFieldNotApplicable(pTable, pItemDef, "Costume Priority", 0);
		METableSetFieldNotApplicable(pTable, pItemDef, "Economy Points", 0);
		METableSetFieldNotApplicable(pTable, pItemDef, "Craft Recipe", 0);
		METableSetFieldNotApplicable(pTable, pItemDef, "Value Recipe", 0);
		METableSetFieldNotApplicable(pTable, pItemDef, "May Buy in Bulk", 0);
		METableSetFieldNotApplicable(pTable, pItemDef, "Level", 0);
		METableSetFieldNotApplicable(pTable, pItemDef, "Group", 1);
		METableSetFieldNotApplicable(pTable, pItemDef, "Quality", 0);
		METableSetFieldNotApplicable(pTable, pItemDef, "Min Value", 1);
		METableSetFieldNotApplicable(pTable, pItemDef, "Max Value", 1);
		METableSetFieldNotApplicable(pTable, pItemDef, "Bag Size", 1);
		METableSetFieldNotApplicable(pTable, pItemDef, "Recipe Cost", 1);
		METableSetFieldNotApplicable(pTable, pItemDef, "Recipe Result Item", 1);
		METableSetFieldNotApplicable(pTable, pItemDef, "Recipe Result ItemPower", 1);
		METableSetFieldNotApplicable(pTable, pItemDef, "Recipe Result Count", 1);
		METableSetFieldNotApplicable(pTable, pItemDef, "Req Skill Type", 0);
		METableSetFieldNotApplicable(pTable, pItemDef, "Req Skill Level", 0);
		METableSetFieldNotApplicable(pTable, pItemDef, "Req Min Level", 0);
		METableSetFieldNotApplicable(pTable, pItemDef, "Req Max Level", 0);
		METableSetFieldNotApplicable(pTable, pItemDef, "Req Expr", 0);
		METableSetFieldNotApplicable(pTable, pItemDef, "Callout", 1);
		METableSetFieldNotApplicable(pTable, pItemDef, "CalloutFSM", 1);
		METableSetFieldNotApplicable(pTable, pItemDef, "Important Callout", 1);
		METableSetFieldNotApplicable(pTable, pItemDef, "Lore Category", 1);
		METableSetFieldNotApplicable(pTable, pItemDef, "Class Type",1);
		METableSetFieldNotApplicable(pTable, pItemDef, "Pet Def",1);
		METableSetFieldNotApplicable(pTable, pItemDef, "Algo Pet Def", 1);
		METableSetFieldNotApplicable(pTable, pItemDef, "Unidentified Description", !itemdef_IsUnidentified(pItemDef));
		METableSetFieldNotApplicable(pTable, pItemDef, "Unidentified Display Name", !itemdef_IsUnidentified(pItemDef));
		METableSetFieldNotApplicable(pTable, pItemDef, "Lore Journal Type", 1);
		METableSetFieldNotApplicable(pTable, pItemDef, "Lore Textures", 1);
		METableSetFieldNotApplicable(pTable, pItemDef, "Lore Critter Def", 1);
		METableSetFieldNotApplicable(pTable, pItemDef, "Equip Limit Max Count", 0);
		METableSetFieldNotApplicable(pTable, pItemDef, "Equip Limit Category", 0);
		break;

	case kItemType_SuperCritterPet:
		METableHideSubTable(pTable, pItemDef, ieTrainableNodesId, 1);
		METableHideSubTable(pTable, pItemDef, iePowersId, 0);
		METableHideSubTable(pTable, pItemDef, ieComponentId, 1);
		METableHideSubTable(pTable, pItemDef, ieCostumeId, 1);
		METableHideSubTable(pTable, pItemDef, ieAttribId, 1);
		METableSetFieldNotApplicable(pTable, pItemDef, "Unidentified Description", 1);
		METableSetFieldNotApplicable(pTable, pItemDef, "Unidentified Display Name", 1);
		METableSetFieldNotApplicable(pTable, pItemDef, "ItemArt", 1);
		METableSetFieldNotApplicable(pTable, pItemDef, "ItemSets", 1);
		METableSetFieldNotApplicable(pTable, pItemDef, "StackLimit", 1);
		METableSetFieldNotApplicable(pTable, pItemDef, "AutoDesc Disabled", 1);
		METableSetFieldNotApplicable(pTable, pItemDef, "AutoDesc Message", 1);
		METableSetFieldNotApplicable(pTable, pItemDef, "May Buy In Bulk", 1);
		METableSetFieldNotApplicable(pTable, pItemDef, "Overflow Numeric", 1);
		METableSetFieldNotApplicable(pTable, pItemDef, "Overflow Multiplier", 1);
		METableSetFieldNotApplicable(pTable, pItemDef, "Mission", 1);
		METableSetFieldNotApplicable(pTable, pItemDef, "Slot", 1);
		METableSetFieldNotApplicable(pTable, pItemDef, "SlotIDType", 1);
		METableSetFieldNotApplicable(pTable, pItemDef, "SkillType", 1);
		METableSetFieldNotApplicable(pTable, pItemDef, "Subtarget Type", 1);
		METableSetFieldNotApplicable(pTable, pItemDef, "StackLimit", 1);
		METableSetFieldNotApplicable(pTable, pItemDef, "Costume Mode", 1);
		METableSetFieldNotApplicable(pTable, pItemDef, "Costume Priority", 1);
		METableSetFieldNotApplicable(pTable, pItemDef, "Economy Points", 0);
		METableSetFieldNotApplicable(pTable, pItemDef, "Craft Recipe", 1);
		METableSetFieldNotApplicable(pTable, pItemDef, "Value Recipe", 1);
		METableSetFieldNotApplicable(pTable, pItemDef, "Group", 1);
		METableSetFieldNotApplicable(pTable, pItemDef, "Min Value", 1);
		METableSetFieldNotApplicable(pTable, pItemDef, "Max Value", 1);
		METableSetFieldNotApplicable(pTable, pItemDef, "Bag Size", 1);
		METableSetFieldNotApplicable(pTable, pItemDef, "Recipe Cost", 1);
		METableSetFieldNotApplicable(pTable, pItemDef, "ItemSet Members Unique", 1);
		METableSetFieldNotApplicable(pTable, pItemDef, "Req Min Level", 1);
		METableSetFieldNotApplicable(pTable, pItemDef, "Req Max Level", 1);
		METableSetFieldNotApplicable(pTable, pItemDef, "Req Expr", 1);
		METableSetFieldNotApplicable(pTable, pItemDef, "Recipe Result Item", 1);
		METableSetFieldNotApplicable(pTable, pItemDef, "Recipe Result ItemPower", 1);
		METableSetFieldNotApplicable(pTable, pItemDef, "Recipe Result Count", 1);
		METableSetFieldNotApplicable(pTable, pItemDef, "Req UICategory", 1);
		METableSetFieldNotApplicable(pTable, pItemDef, "Classes Allowed", 1);
		METableSetFieldNotApplicable(pTable, pItemDef, "Paths Allowed", 1);
		METableSetFieldNotApplicable(pTable, pItemDef, "Class Categories Allowed", 1);
		METableSetFieldNotApplicable(pTable, pItemDef, "Required Allegiances", 1);
		METableSetFieldNotApplicable(pTable, pItemDef, "Item Tag", 1);
		METableSetFieldNotApplicable(pTable, pItemDef, "Make Puppet", 1);
		METableSetFieldNotApplicable(pTable, pItemDef, "Delete After Unlock", 1);
		METableSetFieldNotApplicable(pTable, pItemDef, "Training Destroys Item", 1);
		METableSetFieldNotApplicable(pTable, pItemDef, "InteriorUnlock", 1);
		METableSetFieldNotApplicable(pTable, pItemDef, "Req Skill Type", 1);
		METableSetFieldNotApplicable(pTable, pItemDef, "Req Skill Level", 1);
		METableSetFieldNotApplicable(pTable, pItemDef, "Callout", 1);
		METableSetFieldNotApplicable(pTable, pItemDef, "CalloutFSM", 1);
		METableSetFieldNotApplicable(pTable, pItemDef, "Important Callout", 1);
		METableSetFieldNotApplicable(pTable, pItemDef, "Lore Category", 1);
		METableSetFieldNotApplicable(pTable, pItemDef, "Class Type",1);
		METableSetFieldNotApplicable(pTable, pItemDef, "Pet Def",1);
		METableSetFieldNotApplicable(pTable, pItemDef, "Algo Pet Def", 1);
		METableSetFieldNotApplicable(pTable, pItemDef, "Lore Journal Type", 1);
		METableSetFieldNotApplicable(pTable, pItemDef, "Lore Textures", 1);
		METableSetFieldNotApplicable(pTable, pItemDef, "Lore Critter Def", 1);
		METableSetFieldNotApplicable(pTable, pItemDef, "Equip Limit Max Count", 1);
		METableSetFieldNotApplicable(pTable, pItemDef, "Equip Limit Category", 1);
		break;
	case kItemType_SavedPet:
		METableHideSubTable(pTable, pItemDef, ieTrainableNodesId, 1);
		METableHideSubTable(pTable, pItemDef, iePowersId, 1);
		METableHideSubTable(pTable, pItemDef, ieComponentId, 1);
		METableHideSubTable(pTable, pItemDef, ieCostumeId, 1);
		METableHideSubTable(pTable, pItemDef, ieAttribId, 1);
		METableSetFieldNotApplicable(pTable, pItemDef, "Pet Def",0);
		METableSetFieldNotApplicable(pTable, pItemDef, "Algo Pet Def", 1);
		break;

	case kItemType_AlgoPet:
		METableHideSubTable(pTable, pItemDef, ieTrainableNodesId, 1);
		METableHideSubTable(pTable, pItemDef, iePowersId, 1);
		METableHideSubTable(pTable, pItemDef, ieComponentId, 1);
		METableHideSubTable(pTable, pItemDef, ieCostumeId, 1);
		METableHideSubTable(pTable, pItemDef, ieAttribId, 1);
		METableSetFieldNotApplicable(pTable, pItemDef, "Flags", 0);
		METableSetFieldNotApplicable(pTable, pItemDef, "Description", 0);
		METableSetFieldNotApplicable(pTable, pItemDef, "Icon", 0);
		METableSetFieldNotApplicable(pTable, pItemDef, "ItemArt", 1);
		METableSetFieldNotApplicable(pTable, pItemDef, "Mission", 1);
		METableSetFieldNotApplicable(pTable, pItemDef, "Slot", 1);
		METableSetFieldNotApplicable(pTable, pItemDef, "SlotIDType", 1);
		METableSetFieldNotApplicable(pTable, pItemDef, "SkillType", 1);
		METableSetFieldNotApplicable(pTable, pItemDef, "Subtarget Type", 1);
		METableSetFieldNotApplicable(pTable, pItemDef, "StackLimit", 0);
		METableSetFieldNotApplicable(pTable, pItemDef, "Costume Mode", 1);
		METableSetFieldNotApplicable(pTable, pItemDef, "Costume Priority", 1);
		METableSetFieldNotApplicable(pTable, pItemDef, "Craft Recipe", 1);
		METableSetFieldNotApplicable(pTable, pItemDef, "Value Recipe", 1);
		METableSetFieldNotApplicable(pTable, pItemDef, "May Buy in Bulk", 0);
		METableSetFieldNotApplicable(pTable, pItemDef, "Level", 0);
		METableSetFieldNotApplicable(pTable, pItemDef, "Group", 0);
		METableSetFieldNotApplicable(pTable, pItemDef, "Quality", 0);
		METableSetFieldNotApplicable(pTable, pItemDef, "Min Value", 1);
		METableSetFieldNotApplicable(pTable, pItemDef, "Max Value", 1);
		METableSetFieldNotApplicable(pTable, pItemDef, "Bag Size", 1);
		METableSetFieldNotApplicable(pTable, pItemDef, "Recipe Cost", 1);
		METableSetFieldNotApplicable(pTable, pItemDef, "Recipe Result Item", 1);
		METableSetFieldNotApplicable(pTable, pItemDef, "Recipe Result ItemPower", 1);
		METableSetFieldNotApplicable(pTable, pItemDef, "Recipe Result Count", 1);
		METableSetFieldNotApplicable(pTable, pItemDef, "Req Skill Type", 0);
		METableSetFieldNotApplicable(pTable, pItemDef, "Req Skill Level", 0);
		METableSetFieldNotApplicable(pTable, pItemDef, "Req Min Level", 0);
		METableSetFieldNotApplicable(pTable, pItemDef, "Req Max Level", 0);
		METableSetFieldNotApplicable(pTable, pItemDef, "Req Expr", 0);
		METableSetFieldNotApplicable(pTable, pItemDef, "Callout", 1);
		METableSetFieldNotApplicable(pTable, pItemDef, "CalloutFSM", 1);
		METableSetFieldNotApplicable(pTable, pItemDef, "Important Callout", 1);
		METableSetFieldNotApplicable(pTable, pItemDef, "Lore Category", 1);
		METableSetFieldNotApplicable(pTable, pItemDef, "Class Type",1);
		METableSetFieldNotApplicable(pTable, pItemDef, "Pet Def",0);
		METableSetFieldNotApplicable(pTable, pItemDef, "Algo Pet Def", 0);
		METableSetFieldNotApplicable(pTable, pItemDef, "Lore Journal Type", 1);
		METableSetFieldNotApplicable(pTable, pItemDef, "Lore Textures", 1);
		METableSetFieldNotApplicable(pTable, pItemDef, "Lore Critter Def", 1);
		METableSetFieldNotApplicable(pTable, pItemDef, "Equip Limit Max Count", 1);
		METableSetFieldNotApplicable(pTable, pItemDef, "Equip Limit Category", 1);
		break;

	case kItemType_ModifyAttribute:
		METableHideSubTable(pTable, pItemDef, ieTrainableNodesId, 1);
		METableHideSubTable(pTable, pItemDef, iePowersId, 1);
		METableHideSubTable(pTable, pItemDef, ieComponentId, 1);
		METableHideSubTable(pTable, pItemDef, ieCostumeId, 1);
		METableHideSubTable(pTable, pItemDef, ieAttribId, 0);
		METableSetFieldNotApplicable(pTable, pItemDef, "Flags", 0);
		METableSetFieldNotApplicable(pTable, pItemDef, "Description", 0);
		METableSetFieldNotApplicable(pTable, pItemDef, "Icon", 0);
		METableSetFieldNotApplicable(pTable, pItemDef, "ItemArt", 1);
		METableSetFieldNotApplicable(pTable, pItemDef, "Mission", 1);
		METableSetFieldNotApplicable(pTable, pItemDef, "Slot", 1);
		METableSetFieldNotApplicable(pTable, pItemDef, "SlotIDType", 1);
		METableSetFieldNotApplicable(pTable, pItemDef, "SkillType", 1);
		METableSetFieldNotApplicable(pTable, pItemDef, "Subtarget Type", 1);
		METableSetFieldNotApplicable(pTable, pItemDef, "StackLimit", 0);
		METableSetFieldNotApplicable(pTable, pItemDef, "Costume Mode", 1);
		METableSetFieldNotApplicable(pTable, pItemDef, "Costume Priority", 1);
		METableSetFieldNotApplicable(pTable, pItemDef, "Craft Recipe", 1);
		METableSetFieldNotApplicable(pTable, pItemDef, "Value Recipe", 1);
		METableSetFieldNotApplicable(pTable, pItemDef, "May Buy in Bulk", 1);
		METableSetFieldNotApplicable(pTable, pItemDef, "Level", 1);
		METableSetFieldNotApplicable(pTable, pItemDef, "Group", 1);
		METableSetFieldNotApplicable(pTable, pItemDef, "Quality", 1);
		METableSetFieldNotApplicable(pTable, pItemDef, "Min Value", 1);
		METableSetFieldNotApplicable(pTable, pItemDef, "Max Value", 1);
		METableSetFieldNotApplicable(pTable, pItemDef, "Bag Size", 1);
		METableSetFieldNotApplicable(pTable, pItemDef, "Recipe Cost", 1);
		METableSetFieldNotApplicable(pTable, pItemDef, "Recipe Result Item", 1);
		METableSetFieldNotApplicable(pTable, pItemDef, "Recipe Result ItemPower", 1);
		METableSetFieldNotApplicable(pTable, pItemDef, "Recipe Result Count", 1);
		METableSetFieldNotApplicable(pTable, pItemDef, "Req Skill Type", 0);
		METableSetFieldNotApplicable(pTable, pItemDef, "Req Skill Level", 0);
		METableSetFieldNotApplicable(pTable, pItemDef, "Req Min Level", 0);
		METableSetFieldNotApplicable(pTable, pItemDef, "Req Max Level", 0);
		METableSetFieldNotApplicable(pTable, pItemDef, "Req Expr", 0);
		METableSetFieldNotApplicable(pTable, pItemDef, "Callout", 1);
		METableSetFieldNotApplicable(pTable, pItemDef, "CalloutFSM", 1);
		METableSetFieldNotApplicable(pTable, pItemDef, "Important Callout", 1);
		METableSetFieldNotApplicable(pTable, pItemDef, "Lore Category", 1);
		METableSetFieldNotApplicable(pTable, pItemDef, "Class Type",0);
		METableSetFieldNotApplicable(pTable, pItemDef, "Pet Def",1);
		METableSetFieldNotApplicable(pTable, pItemDef, "Algo Pet Def", 1);
		METableSetFieldNotApplicable(pTable, pItemDef, "Lore Journal Type", 1);
		METableSetFieldNotApplicable(pTable, pItemDef, "Lore Textures", 1);
		METableSetFieldNotApplicable(pTable, pItemDef, "Lore Critter Def", 1);
		METableSetFieldNotApplicable(pTable, pItemDef, "Equip Limit Max Count", 1);
		METableSetFieldNotApplicable(pTable, pItemDef, "Equip Limit Category", 1);
		break;
		
	case kItemType_Bag:
		METableHideSubTable(pTable, pItemDef, ieTrainableNodesId, 1);
		METableHideSubTable(pTable, pItemDef, iePowersId, 1);
		METableHideSubTable(pTable, pItemDef, ieComponentId, 1);
		METableHideSubTable(pTable, pItemDef, ieCostumeId, 1);
		METableHideSubTable(pTable, pItemDef, ieAttribId, 1);
		METableSetFieldNotApplicable(pTable, pItemDef, "Flags", 0);
		METableSetFieldNotApplicable(pTable, pItemDef, "Description", 0);
		METableSetFieldNotApplicable(pTable, pItemDef, "Icon", 0);
		METableSetFieldNotApplicable(pTable, pItemDef, "ItemArt", 1);
		METableSetFieldNotApplicable(pTable, pItemDef, "Mission", 1);
		METableSetFieldNotApplicable(pTable, pItemDef, "Slot", 1);
		METableSetFieldNotApplicable(pTable, pItemDef, "SlotIDType", 1);
		METableSetFieldNotApplicable(pTable, pItemDef, "SkillType", 0);
		METableSetFieldNotApplicable(pTable, pItemDef, "Subtarget Type", 1);
		METableSetFieldNotApplicable(pTable, pItemDef, "StackLimit", 1);
		METableSetFieldNotApplicable(pTable, pItemDef, "Costume Mode", 1);
		METableSetFieldNotApplicable(pTable, pItemDef, "Costume Priority", 1);
		METableSetFieldNotApplicable(pTable, pItemDef, "Economy Points", 0);
		METableSetFieldNotApplicable(pTable, pItemDef, "Craft Recipe", 0);
		METableSetFieldNotApplicable(pTable, pItemDef, "Value Recipe", 0);
		METableSetFieldNotApplicable(pTable, pItemDef, "May Buy in Bulk", 1);
		METableSetFieldNotApplicable(pTable, pItemDef, "Level", 0);
		METableSetFieldNotApplicable(pTable, pItemDef, "Group", 1);
		METableSetFieldNotApplicable(pTable, pItemDef, "Quality", 0);
		METableSetFieldNotApplicable(pTable, pItemDef, "Min Value", 1);
		METableSetFieldNotApplicable(pTable, pItemDef, "Max Value", 1);
		METableSetFieldNotApplicable(pTable, pItemDef, "Bag Size", 0);
		METableSetFieldNotApplicable(pTable, pItemDef, "Recipe Cost", 1);
		METableSetFieldNotApplicable(pTable, pItemDef, "Recipe Result Item", 1);
		METableSetFieldNotApplicable(pTable, pItemDef, "Recipe Result ItemPower", 1);
		METableSetFieldNotApplicable(pTable, pItemDef, "Recipe Result Count", 1);
		METableSetFieldNotApplicable(pTable, pItemDef, "Req Skill Type", 0);
		METableSetFieldNotApplicable(pTable, pItemDef, "Req Skill Level", 0);
		METableSetFieldNotApplicable(pTable, pItemDef, "Req Min Level", 0);
		METableSetFieldNotApplicable(pTable, pItemDef, "Req Max Level", 0);
		METableSetFieldNotApplicable(pTable, pItemDef, "Req Expr", 0);
		METableSetFieldNotApplicable(pTable, pItemDef, "Callout", 1);
		METableSetFieldNotApplicable(pTable, pItemDef, "CalloutFSM", 1);
		METableSetFieldNotApplicable(pTable, pItemDef, "Important Callout", 1);
		METableSetFieldNotApplicable(pTable, pItemDef, "Lore Category", 1);
		METableSetFieldNotApplicable(pTable, pItemDef, "Class Type",1);
		METableSetFieldNotApplicable(pTable, pItemDef, "Pet Def",1);
		METableSetFieldNotApplicable(pTable, pItemDef, "Algo Pet Def", 1);
		METableSetFieldNotApplicable(pTable, pItemDef, "Lore Journal Type", 1);
		METableSetFieldNotApplicable(pTable, pItemDef, "Lore Textures", 1);
		METableSetFieldNotApplicable(pTable, pItemDef, "Lore Critter Def", 1);
		METableSetFieldNotApplicable(pTable, pItemDef, "Equip Limit Max Count", 1);
		METableSetFieldNotApplicable(pTable, pItemDef, "Equip Limit Category", 1);
		break;
		
	case kItemType_Callout:
		METableHideSubTable(pTable, pItemDef, ieTrainableNodesId, 1);
		METableHideSubTable(pTable, pItemDef, iePowersId, 0);
		METableHideSubTable(pTable, pItemDef, ieComponentId, 1);
		METableHideSubTable(pTable, pItemDef, ieCostumeId, 1);
		METableHideSubTable(pTable, pItemDef, ieAttribId, 1);
		METableSetFieldNotApplicable(pTable, pItemDef, "Flags", 0);
		METableSetFieldNotApplicable(pTable, pItemDef, "Description", 1);
		METableSetFieldNotApplicable(pTable, pItemDef, "Icon", 1);
		METableSetFieldNotApplicable(pTable, pItemDef, "ItemArt", 1);
		METableSetFieldNotApplicable(pTable, pItemDef, "Mission", 1);
		METableSetFieldNotApplicable(pTable, pItemDef, "Slot", 1);
		METableSetFieldNotApplicable(pTable, pItemDef, "SlotIDType", 1);
		METableSetFieldNotApplicable(pTable, pItemDef, "SkillType", 1);
		METableSetFieldNotApplicable(pTable, pItemDef, "Subtarget Type", 1);
//		METableSetFieldNotApplicable(pTable, pItemDef, "StackLimit", 1);
		METableSetFieldNotApplicable(pTable, pItemDef, "Costume Mode", 1);
		METableSetFieldNotApplicable(pTable, pItemDef, "Costume Priority", 1);
		METableSetFieldNotApplicable(pTable, pItemDef, "Economy Points", 1);
		METableSetFieldNotApplicable(pTable, pItemDef, "Craft Recipe", 1);
		METableSetFieldNotApplicable(pTable, pItemDef, "Value Recipe", 1);
		METableSetFieldNotApplicable(pTable, pItemDef, "May Buy in Bulk", 1);
		METableSetFieldNotApplicable(pTable, pItemDef, "Level", 1);
		METableSetFieldNotApplicable(pTable, pItemDef, "Group", 1);
		METableSetFieldNotApplicable(pTable, pItemDef, "Quality", 1);
		METableSetFieldNotApplicable(pTable, pItemDef, "Min Value", 1);
		METableSetFieldNotApplicable(pTable, pItemDef, "Max Value", 1);
		METableSetFieldNotApplicable(pTable, pItemDef, "Bag Size", 1);
		METableSetFieldNotApplicable(pTable, pItemDef, "Recipe Cost", 1);
		METableSetFieldNotApplicable(pTable, pItemDef, "Recipe Result Item", 1);
		METableSetFieldNotApplicable(pTable, pItemDef, "Recipe Result ItemPower", 1);
		METableSetFieldNotApplicable(pTable, pItemDef, "Recipe Result Count", 1);
		METableSetFieldNotApplicable(pTable, pItemDef, "Req Skill Type", 0);
		METableSetFieldNotApplicable(pTable, pItemDef, "Req Skill Level", 0);
		METableSetFieldNotApplicable(pTable, pItemDef, "Req Min Level", 0);
		METableSetFieldNotApplicable(pTable, pItemDef, "Req Max Level", 0);
		METableSetFieldNotApplicable(pTable, pItemDef, "Req Expr", 0);
		METableSetFieldNotApplicable(pTable, pItemDef, "Callout", 0);
		METableSetFieldNotApplicable(pTable, pItemDef, "CalloutFSM", 0);
		METableSetFieldNotApplicable(pTable, pItemDef, "Important Callout", 0);
		METableSetFieldNotApplicable(pTable, pItemDef, "Lore Category", 1);
		METableSetFieldNotApplicable(pTable, pItemDef, "Class Type",1);
		METableSetFieldNotApplicable(pTable, pItemDef, "Pet Def",1);
		METableSetFieldNotApplicable(pTable, pItemDef, "Algo Pet Def", 1);
		METableSetFieldNotApplicable(pTable, pItemDef, "Lore Journal Type", 1);
		METableSetFieldNotApplicable(pTable, pItemDef, "Lore Textures", 1);
		METableSetFieldNotApplicable(pTable, pItemDef, "Lore Critter Def", 1);
		METableSetFieldNotApplicable(pTable, pItemDef, "Equip Limit Max Count", 1);
		METableSetFieldNotApplicable(pTable, pItemDef, "Equip Limit Category", 1);
		break;

	case kItemType_Lore:
		METableHideSubTable(pTable, pItemDef, ieTrainableNodesId, 1);
		METableHideSubTable(pTable, pItemDef, iePowersId, 1);
		METableHideSubTable(pTable, pItemDef, ieComponentId, 1);
		METableHideSubTable(pTable, pItemDef, ieCostumeId, 1);
		METableHideSubTable(pTable, pItemDef, ieAttribId, 1);
		METableSetFieldNotApplicable(pTable, pItemDef, "Flags", 1);
		METableSetFieldNotApplicable(pTable, pItemDef, "Description", 0);
		METableSetFieldNotApplicable(pTable, pItemDef, "Icon", 1);
		METableSetFieldNotApplicable(pTable, pItemDef, "ItemArt", 1);
		METableSetFieldNotApplicable(pTable, pItemDef, "Mission", 1);
		METableSetFieldNotApplicable(pTable, pItemDef, "Slot", 1);
		METableSetFieldNotApplicable(pTable, pItemDef, "SlotIDType", 1);
		METableSetFieldNotApplicable(pTable, pItemDef, "SkillType", 1);
		METableSetFieldNotApplicable(pTable, pItemDef, "Subtarget Type", 1);
		METableSetFieldNotApplicable(pTable, pItemDef, "StackLimit", 1);
		METableSetFieldNotApplicable(pTable, pItemDef, "Costume Mode", 1);
		METableSetFieldNotApplicable(pTable, pItemDef, "Costume Priority", 1);
		METableSetFieldNotApplicable(pTable, pItemDef, "Economy Points", 1);
		METableSetFieldNotApplicable(pTable, pItemDef, "Craft Recipe", 1);
		METableSetFieldNotApplicable(pTable, pItemDef, "Value Recipe", 1);
		METableSetFieldNotApplicable(pTable, pItemDef, "May Buy in Bulk", 1);
		METableSetFieldNotApplicable(pTable, pItemDef, "Level", 1);
		METableSetFieldNotApplicable(pTable, pItemDef, "Group", 1);
		METableSetFieldNotApplicable(pTable, pItemDef, "Quality", 1);
		METableSetFieldNotApplicable(pTable, pItemDef, "Min Value", 1);
		METableSetFieldNotApplicable(pTable, pItemDef, "Max Value", 1);
		METableSetFieldNotApplicable(pTable, pItemDef, "Bag Size", 1);
		METableSetFieldNotApplicable(pTable, pItemDef, "Recipe Cost", 1);
		METableSetFieldNotApplicable(pTable, pItemDef, "Recipe Result Item", 1);
		METableSetFieldNotApplicable(pTable, pItemDef, "Recipe Result ItemPower", 1);
		METableSetFieldNotApplicable(pTable, pItemDef, "Recipe Result Count", 1);
		METableSetFieldNotApplicable(pTable, pItemDef, "Req Skill Type", 1);
		METableSetFieldNotApplicable(pTable, pItemDef, "Req Skill Level", 1);
		METableSetFieldNotApplicable(pTable, pItemDef, "Req Min Level", 1);
		METableSetFieldNotApplicable(pTable, pItemDef, "Req Max Level", 1);
		METableSetFieldNotApplicable(pTable, pItemDef, "Req Expr", 1);
		METableSetFieldNotApplicable(pTable, pItemDef, "Callout", 1);
		METableSetFieldNotApplicable(pTable, pItemDef, "CalloutFSM", 1);
		METableSetFieldNotApplicable(pTable, pItemDef, "Important Callout", 1);
		METableSetFieldNotApplicable(pTable, pItemDef, "Lore Category", 0);
		METableSetFieldNotApplicable(pTable, pItemDef, "Class Type",1);
		METableSetFieldNotApplicable(pTable, pItemDef, "Pet Def",1);
		METableSetFieldNotApplicable(pTable, pItemDef, "Algo Pet Def", 1);
		METableSetFieldNotApplicable(pTable, pItemDef, "Unidentified Description", !itemdef_IsUnidentified(pItemDef));
		METableSetFieldNotApplicable(pTable, pItemDef, "Unidentified Display Name", !itemdef_IsUnidentified(pItemDef));
		METableSetFieldNotApplicable(pTable, pItemDef, "Lore Journal Type", 0);
		METableSetFieldNotApplicable(pTable, pItemDef, "Lore Textures", 0);
		METableSetFieldNotApplicable(pTable, pItemDef, "Lore Critter Def", 0);
		METableSetFieldNotApplicable(pTable, pItemDef, "Equip Limit Max Count", 1);
		METableSetFieldNotApplicable(pTable, pItemDef, "Equip Limit Category", 1);
		break;

	case kItemType_Token:
		METableHideSubTable(pTable, pItemDef, ieTrainableNodesId, 1);
		METableHideSubTable(pTable, pItemDef, iePowersId, 0);
		METableHideSubTable(pTable, pItemDef, ieComponentId, 1);
		METableHideSubTable(pTable, pItemDef, ieCostumeId, 1);
		METableHideSubTable(pTable, pItemDef, ieAttribId, 1);
		METableSetFieldNotApplicable(pTable, pItemDef, "Flags", 0);
		METableSetFieldNotApplicable(pTable, pItemDef, "Description", 1);
		METableSetFieldNotApplicable(pTable, pItemDef, "Icon", 1);
		METableSetFieldNotApplicable(pTable, pItemDef, "ItemArt", 1);
		METableSetFieldNotApplicable(pTable, pItemDef, "Mission", 1);
		METableSetFieldNotApplicable(pTable, pItemDef, "Slot", 1);
		METableSetFieldNotApplicable(pTable, pItemDef, "SlotIDType", 1);
		METableSetFieldNotApplicable(pTable, pItemDef, "SkillType", 1);
		METableSetFieldNotApplicable(pTable, pItemDef, "Subtarget Type", 1);
		METableSetFieldNotApplicable(pTable, pItemDef, "StackLimit", 0);
		METableSetFieldNotApplicable(pTable, pItemDef, "Costume Mode", 1);
		METableSetFieldNotApplicable(pTable, pItemDef, "Costume Priority", 1);
		METableSetFieldNotApplicable(pTable, pItemDef, "Economy Points", 1);
		METableSetFieldNotApplicable(pTable, pItemDef, "Craft Recipe", 1);
		METableSetFieldNotApplicable(pTable, pItemDef, "Value Recipe", 1);
		METableSetFieldNotApplicable(pTable, pItemDef, "May Buy in Bulk", 1);
		METableSetFieldNotApplicable(pTable, pItemDef, "Level", 1);
		METableSetFieldNotApplicable(pTable, pItemDef, "Group", 1);
		METableSetFieldNotApplicable(pTable, pItemDef, "Quality", 1);
		METableSetFieldNotApplicable(pTable, pItemDef, "Min Value", 1);
		METableSetFieldNotApplicable(pTable, pItemDef, "Max Value", 1);
		METableSetFieldNotApplicable(pTable, pItemDef, "Bag Size", 1);
		METableSetFieldNotApplicable(pTable, pItemDef, "Recipe Cost", 1);
		METableSetFieldNotApplicable(pTable, pItemDef, "Recipe Result Item", 1);
		METableSetFieldNotApplicable(pTable, pItemDef, "Recipe Result ItemPower", 1);
		METableSetFieldNotApplicable(pTable, pItemDef, "Recipe Result Count", 1);
		METableSetFieldNotApplicable(pTable, pItemDef, "Req Skill Type", 0);
		METableSetFieldNotApplicable(pTable, pItemDef, "Req Skill Level", 0);
		METableSetFieldNotApplicable(pTable, pItemDef, "Req Min Level", 0);
		METableSetFieldNotApplicable(pTable, pItemDef, "Req Max Level", 0);
		METableSetFieldNotApplicable(pTable, pItemDef, "Req Expr", 0);
		METableSetFieldNotApplicable(pTable, pItemDef, "Callout", 1);
		METableSetFieldNotApplicable(pTable, pItemDef, "CalloutFSM", 1);
		METableSetFieldNotApplicable(pTable, pItemDef, "Important Callout", 1);
		METableSetFieldNotApplicable(pTable, pItemDef, "Lore Category", 1);
		METableSetFieldNotApplicable(pTable, pItemDef, "Class Type",1);
		METableSetFieldNotApplicable(pTable, pItemDef, "Pet Def",1);
		METableSetFieldNotApplicable(pTable, pItemDef, "Algo Pet Def", 1);
		METableSetFieldNotApplicable(pTable, pItemDef, "Lore Journal Type", 1);
		METableSetFieldNotApplicable(pTable, pItemDef, "Lore Textures", 1);
		METableSetFieldNotApplicable(pTable, pItemDef, "Lore Critter Def", 1);
		METableSetFieldNotApplicable(pTable, pItemDef, "Equip Limit Max Count", 1);
		METableSetFieldNotApplicable(pTable, pItemDef, "Equip Limit Category", 1);
		break;

	case kItemType_Title:
		METableHideSubTable(pTable, pItemDef, ieTrainableNodesId, 1);
		METableHideSubTable(pTable, pItemDef, iePowersId, 0);
		METableHideSubTable(pTable, pItemDef, ieComponentId, 1);
		METableHideSubTable(pTable, pItemDef, ieCostumeId, 1);
		METableHideSubTable(pTable, pItemDef, ieAttribId, 1);
		METableSetFieldNotApplicable(pTable, pItemDef, "Flags", 0);
		METableSetFieldNotApplicable(pTable, pItemDef, "Description", 1);
		METableSetFieldNotApplicable(pTable, pItemDef, "Icon", 0);
		METableSetFieldNotApplicable(pTable, pItemDef, "ItemArt", 1);
		METableSetFieldNotApplicable(pTable, pItemDef, "Mission", 1);
		METableSetFieldNotApplicable(pTable, pItemDef, "Slot", 1);
		METableSetFieldNotApplicable(pTable, pItemDef, "SlotIDType", 1);
		METableSetFieldNotApplicable(pTable, pItemDef, "SkillType", 1);
		METableSetFieldNotApplicable(pTable, pItemDef, "Subtarget Type", 1);
		METableSetFieldNotApplicable(pTable, pItemDef, "StackLimit", 1);
		METableSetFieldNotApplicable(pTable, pItemDef, "Costume Mode", 1);
		METableSetFieldNotApplicable(pTable, pItemDef, "Costume Priority", 1);
		METableSetFieldNotApplicable(pTable, pItemDef, "Economy Points", 1);
		METableSetFieldNotApplicable(pTable, pItemDef, "Craft Recipe", 1);
		METableSetFieldNotApplicable(pTable, pItemDef, "Value Recipe", 1);
		METableSetFieldNotApplicable(pTable, pItemDef, "May Buy in Bulk", 1);
		METableSetFieldNotApplicable(pTable, pItemDef, "Level", 1);
		METableSetFieldNotApplicable(pTable, pItemDef, "Group", 1);
		METableSetFieldNotApplicable(pTable, pItemDef, "Quality", 1);
		METableSetFieldNotApplicable(pTable, pItemDef, "Min Value", 1);
		METableSetFieldNotApplicable(pTable, pItemDef, "Max Value", 1);
		METableSetFieldNotApplicable(pTable, pItemDef, "Bag Size", 1);
		METableSetFieldNotApplicable(pTable, pItemDef, "Recipe Cost", 1);
		METableSetFieldNotApplicable(pTable, pItemDef, "Recipe Result Item", 1);
		METableSetFieldNotApplicable(pTable, pItemDef, "Recipe Result ItemPower", 1);
		METableSetFieldNotApplicable(pTable, pItemDef, "Recipe Result Count", 1);
		METableSetFieldNotApplicable(pTable, pItemDef, "Req Skill Type", 0);
		METableSetFieldNotApplicable(pTable, pItemDef, "Req Skill Level", 0);
		METableSetFieldNotApplicable(pTable, pItemDef, "Req Min Level", 0);
		METableSetFieldNotApplicable(pTable, pItemDef, "Req Max Level", 0);
		METableSetFieldNotApplicable(pTable, pItemDef, "Req Expr", 0);
		METableSetFieldNotApplicable(pTable, pItemDef, "Callout", 1);
		METableSetFieldNotApplicable(pTable, pItemDef, "CalloutFSM", 1);
		METableSetFieldNotApplicable(pTable, pItemDef, "Important Callout", 1);
		METableSetFieldNotApplicable(pTable, pItemDef, "Lore Category", 1);
		METableSetFieldNotApplicable(pTable, pItemDef, "Class Type",1);
		METableSetFieldNotApplicable(pTable, pItemDef, "Pet Def",1);
		METableSetFieldNotApplicable(pTable, pItemDef, "Algo Pet Def", 1);
		METableSetFieldNotApplicable(pTable, pItemDef, "Lore Journal Type", 1);
		METableSetFieldNotApplicable(pTable, pItemDef, "Lore Textures", 1);
		METableSetFieldNotApplicable(pTable, pItemDef, "Lore Critter Def", 1);
		METableSetFieldNotApplicable(pTable, pItemDef, "Equip Limit Max Count", 1);
		METableSetFieldNotApplicable(pTable, pItemDef, "Equip Limit Category", 1);
		break;

	case kItemType_RewardPack:
		METableHideSubTable(pTable, pItemDef, ieTrainableNodesId, 1);
		METableHideSubTable(pTable, pItemDef, iePowersId, 1);
		METableHideSubTable(pTable, pItemDef, ieComponentId, 1);
		METableHideSubTable(pTable, pItemDef, ieCostumeId, 1);
		METableHideSubTable(pTable, pItemDef, ieAttribId, 1);
		METableSetFieldNotApplicable(pTable, pItemDef, "Flags", 0);
		METableSetFieldNotApplicable(pTable, pItemDef, "Description", 0);
		METableSetFieldNotApplicable(pTable, pItemDef, "Icon", 0);
		METableSetFieldNotApplicable(pTable, pItemDef, "ItemArt", 1);
		METableSetFieldNotApplicable(pTable, pItemDef, "Mission", 1);
		METableSetFieldNotApplicable(pTable, pItemDef, "Slot", 1);
		METableSetFieldNotApplicable(pTable, pItemDef, "SlotIDType", 1);
		METableSetFieldNotApplicable(pTable, pItemDef, "SkillType", 1);
		METableSetFieldNotApplicable(pTable, pItemDef, "Subtarget Type", 1);
		METableSetFieldNotApplicable(pTable, pItemDef, "StackLimit", 0);
		METableSetFieldNotApplicable(pTable, pItemDef, "Costume Mode", 1);
		METableSetFieldNotApplicable(pTable, pItemDef, "Costume Priority", 1);
		METableSetFieldNotApplicable(pTable, pItemDef, "Economy Points", 0);
		METableSetFieldNotApplicable(pTable, pItemDef, "Craft Recipe", 1);
		METableSetFieldNotApplicable(pTable, pItemDef, "Value Recipe", 1);
		METableSetFieldNotApplicable(pTable, pItemDef, "May Buy in Bulk", 0);
		METableSetFieldNotApplicable(pTable, pItemDef, "Level", 0);
		METableSetFieldNotApplicable(pTable, pItemDef, "Group", 1);
		METableSetFieldNotApplicable(pTable, pItemDef, "Quality", 0);
		METableSetFieldNotApplicable(pTable, pItemDef, "Min Value", 1);
		METableSetFieldNotApplicable(pTable, pItemDef, "Max Value", 1);
		METableSetFieldNotApplicable(pTable, pItemDef, "Bag Size", 1);
		METableSetFieldNotApplicable(pTable, pItemDef, "Recipe Cost", 1);
		METableSetFieldNotApplicable(pTable, pItemDef, "Recipe Result Item", 1);
		METableSetFieldNotApplicable(pTable, pItemDef, "Recipe Result ItemPower", 1);
		METableSetFieldNotApplicable(pTable, pItemDef, "Recipe Result Count", 1);
		METableSetFieldNotApplicable(pTable, pItemDef, "Req Skill Type", 0);
		METableSetFieldNotApplicable(pTable, pItemDef, "Req Skill Level", 0);
		METableSetFieldNotApplicable(pTable, pItemDef, "Req Min Level", 0);
		METableSetFieldNotApplicable(pTable, pItemDef, "Req Max Level", 0);
		METableSetFieldNotApplicable(pTable, pItemDef, "Req Expr", 0);
		METableSetFieldNotApplicable(pTable, pItemDef, "Callout", 1);
		METableSetFieldNotApplicable(pTable, pItemDef, "CalloutFSM", 1);
		METableSetFieldNotApplicable(pTable, pItemDef, "Important Callout", 1);
		METableSetFieldNotApplicable(pTable, pItemDef, "Lore Category", 1);
		METableSetFieldNotApplicable(pTable, pItemDef, "Class Type",1);
		METableSetFieldNotApplicable(pTable, pItemDef, "Pet Def",1);
		METableSetFieldNotApplicable(pTable, pItemDef, "Algo Pet Def", 1);
		METableSetFieldNotApplicable(pTable, pItemDef, "Lore Journal Type", 1);
		METableSetFieldNotApplicable(pTable, pItemDef, "Lore Textures", 1);
		METableSetFieldNotApplicable(pTable, pItemDef, "Lore Critter Def", 1);
		METableSetFieldNotApplicable(pTable, pItemDef, "Equip Limit Max Count", 1);
		METableSetFieldNotApplicable(pTable, pItemDef, "Equip Limit Category", 1);
		break;

	case kItemType_GrantMicroSpecial:
		METableHideSubTable(pTable, pItemDef, ieTrainableNodesId, 1);
		METableHideSubTable(pTable, pItemDef, iePowersId, 1);
		METableHideSubTable(pTable, pItemDef, ieComponentId, 1);
		METableHideSubTable(pTable, pItemDef, ieCostumeId, 1);
		METableHideSubTable(pTable, pItemDef, ieAttribId, 1);
		METableSetFieldNotApplicable(pTable, pItemDef, "Flags", 0);
		METableSetFieldNotApplicable(pTable, pItemDef, "Description", 0);
		METableSetFieldNotApplicable(pTable, pItemDef, "Icon", 0);
		METableSetFieldNotApplicable(pTable, pItemDef, "ItemArt", 1);
		METableSetFieldNotApplicable(pTable, pItemDef, "Mission", 1);
		METableSetFieldNotApplicable(pTable, pItemDef, "Slot", 1);
		METableSetFieldNotApplicable(pTable, pItemDef, "SlotIDType", 1);
		METableSetFieldNotApplicable(pTable, pItemDef, "SkillType", 1);
		METableSetFieldNotApplicable(pTable, pItemDef, "Subtarget Type", 1);
		METableSetFieldNotApplicable(pTable, pItemDef, "StackLimit", 0);
		METableSetFieldNotApplicable(pTable, pItemDef, "Costume Mode", 1);
		METableSetFieldNotApplicable(pTable, pItemDef, "Costume Priority", 1);
		METableSetFieldNotApplicable(pTable, pItemDef, "Economy Points", 1);
		METableSetFieldNotApplicable(pTable, pItemDef, "Craft Recipe", 1);
		METableSetFieldNotApplicable(pTable, pItemDef, "Value Recipe", 1);
		METableSetFieldNotApplicable(pTable, pItemDef, "May Buy in Bulk", 0);
		METableSetFieldNotApplicable(pTable, pItemDef, "Level", 1);
		METableSetFieldNotApplicable(pTable, pItemDef, "Group", 1);
		METableSetFieldNotApplicable(pTable, pItemDef, "Quality", 1);
		METableSetFieldNotApplicable(pTable, pItemDef, "Min Value", 1);
		METableSetFieldNotApplicable(pTable, pItemDef, "Max Value", 1);
		METableSetFieldNotApplicable(pTable, pItemDef, "Bag Size", 1);
		METableSetFieldNotApplicable(pTable, pItemDef, "Recipe Cost", 1);
		METableSetFieldNotApplicable(pTable, pItemDef, "Recipe Result Item", 1);
		METableSetFieldNotApplicable(pTable, pItemDef, "Recipe Result ItemPower", 1);
		METableSetFieldNotApplicable(pTable, pItemDef, "Recipe Result Count", 1);
		METableSetFieldNotApplicable(pTable, pItemDef, "Req Skill Type", 1);
		METableSetFieldNotApplicable(pTable, pItemDef, "Req Skill Level", 1);
		METableSetFieldNotApplicable(pTable, pItemDef, "Req Min Level", 0);
		METableSetFieldNotApplicable(pTable, pItemDef, "Req Max Level", 0);
		METableSetFieldNotApplicable(pTable, pItemDef, "Req Expr", 0);
		METableSetFieldNotApplicable(pTable, pItemDef, "Callout", 1);
		METableSetFieldNotApplicable(pTable, pItemDef, "CalloutFSM", 1);
		METableSetFieldNotApplicable(pTable, pItemDef, "Important Callout", 1);
		METableSetFieldNotApplicable(pTable, pItemDef, "Lore Category", 1);
		METableSetFieldNotApplicable(pTable, pItemDef, "Class Type",1);
		METableSetFieldNotApplicable(pTable, pItemDef, "Pet Def",1);
		METableSetFieldNotApplicable(pTable, pItemDef, "Algo Pet Def", 1);
		METableSetFieldNotApplicable(pTable, pItemDef, "Lore Journal Type", 1);
		METableSetFieldNotApplicable(pTable, pItemDef, "Lore Textures", 1);
		METableSetFieldNotApplicable(pTable, pItemDef, "Lore Critter Def", 1);
		METableSetFieldNotApplicable(pTable, pItemDef, "Equip Limit Max Count", 1);
		METableSetFieldNotApplicable(pTable, pItemDef, "Equip Limit Category", 1);
		break;

	case kItemType_ExperienceGift:
		METableHideSubTable(pTable, pItemDef, ieTrainableNodesId, 1);
		METableHideSubTable(pTable, pItemDef, iePowersId, 1);
		METableHideSubTable(pTable, pItemDef, ieComponentId, 1);
		METableHideSubTable(pTable, pItemDef, ieCostumeId, 1);
		METableHideSubTable(pTable, pItemDef, ieAttribId, 1);
		METableSetFieldNotApplicable(pTable, pItemDef, "Flags", 0);
		METableSetFieldNotApplicable(pTable, pItemDef, "Description", 0);
		METableSetFieldNotApplicable(pTable, pItemDef, "Icon", 0);
		METableSetFieldNotApplicable(pTable, pItemDef, "ItemArt", 1);
		METableSetFieldNotApplicable(pTable, pItemDef, "Mission", 1);
		METableSetFieldNotApplicable(pTable, pItemDef, "Slot", 1);
		METableSetFieldNotApplicable(pTable, pItemDef, "SlotIDType", 1);
		METableSetFieldNotApplicable(pTable, pItemDef, "SkillType", 1);
		METableSetFieldNotApplicable(pTable, pItemDef, "Subtarget Type", 1);
		METableSetFieldNotApplicable(pTable, pItemDef, "StackLimit", 0);
		METableSetFieldNotApplicable(pTable, pItemDef, "Costume Mode", 1);
		METableSetFieldNotApplicable(pTable, pItemDef, "Costume Priority", 1);
		METableSetFieldNotApplicable(pTable, pItemDef, "Economy Points", 1);
		METableSetFieldNotApplicable(pTable, pItemDef, "Craft Recipe", 1);
		METableSetFieldNotApplicable(pTable, pItemDef, "Value Recipe", 1);
		METableSetFieldNotApplicable(pTable, pItemDef, "May Buy in Bulk", 0);
		METableSetFieldNotApplicable(pTable, pItemDef, "Level", 1);
		METableSetFieldNotApplicable(pTable, pItemDef, "Group", 1);
		METableSetFieldNotApplicable(pTable, pItemDef, "Quality", 1);
		METableSetFieldNotApplicable(pTable, pItemDef, "Min Value", 1);
		METableSetFieldNotApplicable(pTable, pItemDef, "Max Value", 1);
		METableSetFieldNotApplicable(pTable, pItemDef, "Bag Size", 1);
		METableSetFieldNotApplicable(pTable, pItemDef, "Recipe Cost", 1);
		METableSetFieldNotApplicable(pTable, pItemDef, "Recipe Result Item", 1);
		METableSetFieldNotApplicable(pTable, pItemDef, "Recipe Result ItemPower", 1);
		METableSetFieldNotApplicable(pTable, pItemDef, "Recipe Result Count", 1);
		METableSetFieldNotApplicable(pTable, pItemDef, "Req Skill Type", 1);
		METableSetFieldNotApplicable(pTable, pItemDef, "Req Skill Level", 1);
		METableSetFieldNotApplicable(pTable, pItemDef, "Req Min Level", 0);
		METableSetFieldNotApplicable(pTable, pItemDef, "Req Max Level", 0);
		METableSetFieldNotApplicable(pTable, pItemDef, "Req Expr", 0);
		METableSetFieldNotApplicable(pTable, pItemDef, "Callout", 1);
		METableSetFieldNotApplicable(pTable, pItemDef, "CalloutFSM", 1);
		METableSetFieldNotApplicable(pTable, pItemDef, "Important Callout", 1);
		METableSetFieldNotApplicable(pTable, pItemDef, "Lore Category", 1);
		METableSetFieldNotApplicable(pTable, pItemDef, "Class Type",1);
		METableSetFieldNotApplicable(pTable, pItemDef, "Pet Def",1);
		METableSetFieldNotApplicable(pTable, pItemDef, "Algo Pet Def", 1);
		METableSetFieldNotApplicable(pTable, pItemDef, "Lore Journal Type", 1);
		METableSetFieldNotApplicable(pTable, pItemDef, "Lore Textures", 1);
		METableSetFieldNotApplicable(pTable, pItemDef, "Lore Critter Def", 1);
		METableSetFieldNotApplicable(pTable, pItemDef, "Equip Limit Max Count", 1);
		METableSetFieldNotApplicable(pTable, pItemDef, "Equip Limit Category", 1);
		break;

	case kItemType_Coupon:
		METableHideSubTable(pTable, pItemDef, ieTrainableNodesId, 1);
		METableHideSubTable(pTable, pItemDef, iePowersId, 1);
		METableHideSubTable(pTable, pItemDef, ieComponentId, 1);
		METableHideSubTable(pTable, pItemDef, ieCostumeId, 1);
		METableHideSubTable(pTable, pItemDef, ieAttribId, 1);
		METableSetFieldNotApplicable(pTable, pItemDef, "Flags", 0);
		METableSetFieldNotApplicable(pTable, pItemDef, "Description", 0);
		METableSetFieldNotApplicable(pTable, pItemDef, "Icon", 0);
		METableSetFieldNotApplicable(pTable, pItemDef, "ItemArt", 1);
		METableSetFieldNotApplicable(pTable, pItemDef, "Mission", 1);
		METableSetFieldNotApplicable(pTable, pItemDef, "Slot", 1);
		METableSetFieldNotApplicable(pTable, pItemDef, "SlotIDType", 1);
		METableSetFieldNotApplicable(pTable, pItemDef, "SkillType", 1);
		METableSetFieldNotApplicable(pTable, pItemDef, "Subtarget Type", 1);
		METableSetFieldNotApplicable(pTable, pItemDef, "StackLimit", 0);
		METableSetFieldNotApplicable(pTable, pItemDef, "Costume Mode", 1);
		METableSetFieldNotApplicable(pTable, pItemDef, "Costume Priority", 1);
		METableSetFieldNotApplicable(pTable, pItemDef, "Economy Points", 1);
		METableSetFieldNotApplicable(pTable, pItemDef, "Craft Recipe", 1);
		METableSetFieldNotApplicable(pTable, pItemDef, "Value Recipe", 1);
		METableSetFieldNotApplicable(pTable, pItemDef, "May Buy in Bulk", 0);
		METableSetFieldNotApplicable(pTable, pItemDef, "Level", 1);
		METableSetFieldNotApplicable(pTable, pItemDef, "Group", 1);
		METableSetFieldNotApplicable(pTable, pItemDef, "Quality", 1);
		METableSetFieldNotApplicable(pTable, pItemDef, "Min Value", 1);
		METableSetFieldNotApplicable(pTable, pItemDef, "Max Value", 1);
		METableSetFieldNotApplicable(pTable, pItemDef, "Bag Size", 1);
		METableSetFieldNotApplicable(pTable, pItemDef, "Recipe Cost", 1);
		METableSetFieldNotApplicable(pTable, pItemDef, "Recipe Result Item", 1);
		METableSetFieldNotApplicable(pTable, pItemDef, "Recipe Result ItemPower", 1);
		METableSetFieldNotApplicable(pTable, pItemDef, "Recipe Result Count", 1);
		METableSetFieldNotApplicable(pTable, pItemDef, "Req Skill Type", 1);
		METableSetFieldNotApplicable(pTable, pItemDef, "Req Skill Level", 1);
		METableSetFieldNotApplicable(pTable, pItemDef, "Req Min Level", 1);
		METableSetFieldNotApplicable(pTable, pItemDef, "Req Max Level", 1);
		METableSetFieldNotApplicable(pTable, pItemDef, "Req Expr", 1);
		METableSetFieldNotApplicable(pTable, pItemDef, "Callout", 1);
		METableSetFieldNotApplicable(pTable, pItemDef, "CalloutFSM", 1);
		METableSetFieldNotApplicable(pTable, pItemDef, "Important Callout", 1);
		METableSetFieldNotApplicable(pTable, pItemDef, "Lore Category", 1);
		METableSetFieldNotApplicable(pTable, pItemDef, "Class Type",1);
		METableSetFieldNotApplicable(pTable, pItemDef, "Pet Def",1);
		METableSetFieldNotApplicable(pTable, pItemDef, "Algo Pet Def", 1);
		METableSetFieldNotApplicable(pTable, pItemDef, "Lore Journal Type", 1);
		METableSetFieldNotApplicable(pTable, pItemDef, "Lore Textures", 1);
		METableSetFieldNotApplicable(pTable, pItemDef, "Lore Critter Def", 1);
		METableSetFieldNotApplicable(pTable, pItemDef, "Equip Limit Max Count", 1);
		METableSetFieldNotApplicable(pTable, pItemDef, "Equip Limit Category", 1);
		break;
	case kItemType_DyePack:
		METableSetFieldNotApplicable(pTable, pItemDef, "Dye Color 1", 0);
		METableSetFieldNotApplicable(pTable, pItemDef, "Dye Color 2", 0);
		METableSetFieldNotApplicable(pTable, pItemDef, "Dye Color 3", 0);

	case kItemType_DyeBottle:
		METableSetFieldNotApplicable(pTable, pItemDef, "Dye Color 0", 0);
		METableSetFieldNotApplicable(pTable, pItemDef, "Dye Material", 0);
		METableHideSubTable(pTable, pItemDef, ieTrainableNodesId, 1);
		METableHideSubTable(pTable, pItemDef, iePowersId, 1);
		METableHideSubTable(pTable, pItemDef, ieComponentId, 1);
		METableHideSubTable(pTable, pItemDef, ieCostumeId, 1);
		METableHideSubTable(pTable, pItemDef, ieAttribId, 1);
		METableSetFieldNotApplicable(pTable, pItemDef, "Flags", 0);
		METableSetFieldNotApplicable(pTable, pItemDef, "Description", 0);
		METableSetFieldNotApplicable(pTable, pItemDef, "Icon", 0);
		METableSetFieldNotApplicable(pTable, pItemDef, "ItemArt", 1);
		METableSetFieldNotApplicable(pTable, pItemDef, "Mission", 1);
		METableSetFieldNotApplicable(pTable, pItemDef, "Slot", 1);
		METableSetFieldNotApplicable(pTable, pItemDef, "SlotIDType", 1);
		METableSetFieldNotApplicable(pTable, pItemDef, "SkillType", 1);
		METableSetFieldNotApplicable(pTable, pItemDef, "Subtarget Type", 1);
		METableSetFieldNotApplicable(pTable, pItemDef, "StackLimit", 0);
		METableSetFieldNotApplicable(pTable, pItemDef, "Costume Mode", 1);
		METableSetFieldNotApplicable(pTable, pItemDef, "Costume Priority", 1);
		METableSetFieldNotApplicable(pTable, pItemDef, "Economy Points", 0);
		METableSetFieldNotApplicable(pTable, pItemDef, "Craft Recipe", 1);
		METableSetFieldNotApplicable(pTable, pItemDef, "Value Recipe", 1);
		METableSetFieldNotApplicable(pTable, pItemDef, "May Buy in Bulk", 0);
		METableSetFieldNotApplicable(pTable, pItemDef, "Level", 1);
		METableSetFieldNotApplicable(pTable, pItemDef, "Group", 1);
		METableSetFieldNotApplicable(pTable, pItemDef, "Quality", 0);
		METableSetFieldNotApplicable(pTable, pItemDef, "Min Value", 1);
		METableSetFieldNotApplicable(pTable, pItemDef, "Max Value", 1);
		METableSetFieldNotApplicable(pTable, pItemDef, "Bag Size", 1);
		METableSetFieldNotApplicable(pTable, pItemDef, "Recipe Cost", 1);
		METableSetFieldNotApplicable(pTable, pItemDef, "Recipe Result Item", 1);
		METableSetFieldNotApplicable(pTable, pItemDef, "Recipe Result ItemPower", 1);
		METableSetFieldNotApplicable(pTable, pItemDef, "Recipe Result Count", 1);
		METableSetFieldNotApplicable(pTable, pItemDef, "Req Skill Type", 1);
		METableSetFieldNotApplicable(pTable, pItemDef, "Req Skill Level", 1);
		METableSetFieldNotApplicable(pTable, pItemDef, "Req Min Level", 1);
		METableSetFieldNotApplicable(pTable, pItemDef, "Req Max Level", 1);
		METableSetFieldNotApplicable(pTable, pItemDef, "Req Expr", 1);
		METableSetFieldNotApplicable(pTable, pItemDef, "Callout", 1);
		METableSetFieldNotApplicable(pTable, pItemDef, "CalloutFSM", 1);
		METableSetFieldNotApplicable(pTable, pItemDef, "Important Callout", 1);
		METableSetFieldNotApplicable(pTable, pItemDef, "Lore Category", 1);
		METableSetFieldNotApplicable(pTable, pItemDef, "Class Type",1);
		METableSetFieldNotApplicable(pTable, pItemDef, "Pet Def",1);
		METableSetFieldNotApplicable(pTable, pItemDef, "Algo Pet Def", 1);
		METableSetFieldNotApplicable(pTable, pItemDef, "Lore Journal Type", 1);
		METableSetFieldNotApplicable(pTable, pItemDef, "Lore Textures", 1);
		METableSetFieldNotApplicable(pTable, pItemDef, "Lore Critter Def", 1);
		METableSetFieldNotApplicable(pTable, pItemDef, "Equip Limit Max Count", 1);
		METableSetFieldNotApplicable(pTable, pItemDef, "Equip Limit Category", 1);
		break;

	}
}


static void ie_dictChangeCallback(enumResourceEventType eType, const char *pDictName, ConstReferenceData pRefData, Referent pReferent, METable *pTable)
{
	METableDictChanged(pTable, eType, pReferent, pRefData);
}


static void ie_messageDictChangeCallback(enumResourceEventType eType, const char *pDictName, const char *pcMessageKey, Referent pReferent, METable *pTable)
{
	if ((eType == RESEVENT_RESOURCE_MODIFIED) ||
		(eType == RESEVENT_RESOURCE_REMOVED) ||
		(eType == RESEVENT_RESOURCE_ADDED)) {

		METableMessageChangedRefresh(pTable, pcMessageKey);
	}
}


static void ie_initCallbacks(MEWindow *pWindow, METable *pTable)
{
	// General Window callbacks
	MEWindowSetCreateCallback(pWindow, ie_windowCreateCallback);

	// General table callbacks
	METableSetValidateCallback(pTable, ie_validateCallback, pTable);
	METableSetPostOpenCallback(pTable, ie_postOpenCallback);
	METableSetPreSaveCallback(pTable, ie_preSaveCallback);
	METableSetCreateCallback(pTable, ie_tableCreateCallback);

	METableSetColumnChangeCallback(pTable, "Type", ie_typeChangeCallback, NULL);
	METableSetColumnChangeCallback(pTable, "Flags", ie_typeChangeCallback, NULL);

	// We need this registered here instead of by METable because the dictionary will 
	// only allow each callback function to be registered once and there may be multiple
	// METable instances.  Our local callback just passes through to the METable.
	resDictRegisterEventCallback(g_hItemDict, ie_dictChangeCallback, pTable);
	resDictRegisterEventCallback(gMessageDict, ie_messageDictChangeCallback, pTable);
}

static char** ie_getPowerTableNames(METable *pTable, void *pUnused)
{
	char **eaPowerTableNames = NULL;

	powertables_FillAllocdNameEArray(&eaPowerTableNames);

	return eaPowerTableNames;
}

static char** ie_GetAttribNames(METable *pTable, void *pUnused)
{
	char **eaAttribNames = NULL;
	char **eaTempAttribNames = NULL;
	int i;

	DefineFillAllKeysAndValues(AttribTypeEnum,&eaTempAttribNames,NULL);

	for(i=0;i<eaSize(&eaTempAttribNames);i++)
	{
		eaPush(&eaAttribNames,strdup(eaTempAttribNames[i]));
	}

	eaDestroy(&eaTempAttribNames);

	return eaAttribNames;
}


//---------------------------------------------------------------------------------------------------
// UI Init
//---------------------------------------------------------------------------------------------------

static void ie_initItemColumns(METable *pTable)
{
	char *pcPath = NULL;
	GameBranch_GetDirectory(&pcPath, ITEMS_BASE_DIR);
	
	METableAddSimpleColumn(pTable, "Name", "name", 150, NULL, kMEFieldType_TextEntry);

	// Lock in name column
	METableSetNumLockedColumns(pTable, 2);

	METableAddSimpleColumn(pTable,   "Display Name", ".displayNameMsg.EditorCopy", 160, IE_GROUP_MAIN, kMEFieldType_Message);
	METableAddSimpleColumn(pTable,   "Unidentified Display Name", ".displayNameMsgUnidentified.EditorCopy", 160, IE_GROUP_MAIN, kMEFieldType_Message);
	METableAddScopeColumn(pTable,    "Scope",        "Scope",       160, IE_GROUP_MAIN, kMEFieldType_TextEntry); // Not validated on purpose
	METableAddFileNameColumn(pTable, "File Name",    "fileName",    210, IE_GROUP_MAIN, NULL, pcPath, pcPath, ".item", UIBrowseNewOrExisting);
	METableAddSimpleColumn(pTable,   "Notes",        "Notes",       160, IE_GROUP_MAIN, kMEFieldType_MultiText);

	METableAddEnumColumn(pTable,     "Type",         "type",        100, IE_GROUP_INFO, kMEFieldType_Combo, ItemTypeEnum);
	METableAddEnumColumn(pTable,	 "Gem Type",	 "GemType",		100, IE_GROUP_INFO, kMEFieldType_FlagCombo, ItemGemTypeEnum);

	METableAddEnumColumn(pTable,     "Restrict Bag", "RestrictBagID",  100, IE_GROUP_INFO, kMEFieldType_FlagCombo, InvBagIDsEnum);
	METableAddEnumColumn(pTable,     "Slot",         "RestrictSlotType", 100, IE_GROUP_INFO, kMEFieldType_Combo, SlotTypeEnum);
	METableAddDictColumn(pTable, "SlotIDType", "SlotIDType",  100, IE_GROUP_INFO, kMEFieldType_ValidatedTextEntry, "InventorySlotIDDef", parse_InventorySlotIDDef, "Key");
	METableAddGlobalDictColumn(pTable, "Subtarget Type", "Subtarget",  100, IE_GROUP_INFO, kMEFieldType_ValidatedTextEntry, "PowerSubtarget", "resourceName");
	METableAddGlobalDictColumn(pTable, "ItemSets", "ItemSet",  100, IE_GROUP_INFO, kMEFieldType_FlagCombo, "ItemDef", "resourceName");

	METableAddGlobalDictColumn(pTable, "Recipe Result Item",       ".Craft.ItemResult",       140, IE_GROUP_CRAFT, kMEFieldType_ValidatedTextEntry, "ItemDef", "resourceName");
	METableAddGlobalDictColumn(pTable, "Recipe Result ItemPower",  ".Craft.ItemPowerResult",  140, IE_GROUP_CRAFT, kMEFieldType_ValidatedTextEntry, "ItemPowerDef", "resourceName");
	METableAddSimpleColumn(pTable,     "Recipe Result Count",      ".Craft.ResultCount",    140, IE_GROUP_CRAFT, kMEFieldType_TextEntry);

	METableAddEnumColumn(pTable,     "SkillType",    "SkillType",   100, IE_GROUP_INFO, kMEFieldType_Combo, SkillTypeEnum);
	METableAddEnumColumn(pTable,     "Flags",        "flags",       140, IE_GROUP_INFO, kMEFieldType_FlagCombo, ItemDefFlagEnum);
	METableAddSimpleColumn(pTable,   "StackLimit",   "StackLimit",  100, IE_GROUP_INFO, kMEFieldType_TextEntry);

	METableAddSimpleColumn(pTable,   "Description",  ".descriptionMsg.EditorCopy", 160, IE_GROUP_DISPLAY, kMEFieldType_Message);
	METableAddSimpleColumn(pTable,   "Short Description",  ".descShortMsg.EditorCopy", 160, IE_GROUP_DISPLAY, kMEFieldType_Message);
	METableAddSimpleColumn(pTable,   "Unidentified Description",  ".descriptionMsgUnidentified.EditorCopy", 160, IE_GROUP_DISPLAY, kMEFieldType_Message);
	METableAddSimpleColumn(pTable, "AutoDesc Disabled","autoDescDisabled",       140, IE_GROUP_DISPLAY, kMEFieldType_BooleanCombo);
	METableAddSimpleColumn(pTable, "AutoDesc Message", ".msgAutoDesc.EditorCopy", 150, IE_GROUP_DISPLAY, kMEFieldType_Message);
	METableAddColumn(pTable,		"Icon",			"icon",		180, IE_GROUP_DISPLAY, kMEFieldType_Texture, NULL, NULL, NULL, NULL, "texture_library/ui/Icons", NULL, NULL);
	METableAddGlobalDictColumn(pTable,	"ItemArt",		"art",		120, IE_GROUP_DISPLAY, kMEFieldType_ValidatedTextEntry, "ItemArt", "resourceName");
	METableAddGlobalDictColumn(pTable,	"Gem Added Costume FX",		"GemAddedCostumeFX",		120, IE_GROUP_DISPLAY, kMEFieldType_ValidatedTextEntry, "DynFXInfo", "resourceName");
	METableAddSimpleColumn(pTable,	"Gem Added Costume Bone",		"GemAddedCostumeBone",		120, IE_GROUP_DISPLAY, kMEFieldType_TextEntry);
	METableAddSimpleColumn(pTable,   "ScaleUI",   "ScaleUI",  100, IE_GROUP_DISPLAY, kMEFieldType_TextEntry);

	METableAddSimpleColumn(pTable, "Level",            "Level",           100, IE_GROUP_VALUES, kMEFieldType_TextEntry);
	METableAddEnumColumn(pTable,   "Quality",          "Quality",         100, IE_GROUP_DISPLAY, kMEFieldType_Combo, ItemQualityEnum);
	METableAddSimpleColumn(pTable,   "Power Factor",      "PowerFactor",    100, IE_GROUP_VALUES, kMEFieldType_TextEntry);

	METableAddExprColumn(pTable,       "Economy Points", "ExprEconomyPoints", 140, IE_GROUP_VALUES, g_pItemContext);
	METableAddGlobalDictColumn(pTable, "Value Recipe",   "ValueRecipe",       140, IE_GROUP_MAIN, kMEFieldType_ValidatedTextEntry, "ItemDef", "resourceName");
	METableAddSimpleColumn(pTable,	   "May Buy in Bulk","MayBuyInBulk",      100, IE_GROUP_VALUES, kMEFieldType_TextEntry);

	METableAddGlobalDictColumn(pTable, "Craft Recipe", "CraftRecipe",          140, IE_GROUP_MAIN, kMEFieldType_ValidatedTextEntry, "ItemDef", "resourceName");
	METableAddSimpleColumn(pTable,     "Recipe Cost",  ".Craft.Resource",      100, IE_GROUP_CRAFT, kMEFieldType_TextEntry);
	METableAddEnumColumn(pTable,       "Group",        "group",                140, IE_GROUP_CRAFT, kMEFieldType_FlagCombo, ItemPowerGroupEnum);

	METableAddEnumColumn(pTable,     "Costume Mode", "CostumeMode", 100, IE_GROUP_DISPLAY, kMEFieldType_Combo, kCostumeDisplayModeEnum);
	METableAddSimpleColumn(pTable,   "Costume Priority", "CostumePriority", 120, IE_GROUP_DISPLAY, kMEFieldType_TextEntry);
	
	METableAddSimpleColumn(pTable,	"Dye Color 0", "DyeColor0", 80, IE_GROUP_DISPLAY, kMEFieldType_Color);
	METableAddSimpleColumn(pTable,	"Dye Color 1", "DyeColor1", 80, IE_GROUP_DISPLAY, kMEFieldType_Color);
	METableAddSimpleColumn(pTable,	"Dye Color 2", "DyeColor2", 80, IE_GROUP_DISPLAY, kMEFieldType_Color);
	METableAddSimpleColumn(pTable,	"Dye Color 3", "DyeColor3", 80, IE_GROUP_DISPLAY, kMEFieldType_Color);
	METableAddGlobalDictColumn(pTable,	"Dye material", "hDyeMat", 120, IE_GROUP_DISPLAY, kMEFieldType_ValidatedTextEntry, "PCMaterialDef", "resourceName");

	METableAddSimpleColumn(pTable, "Power Hue", "PowerHue", 80, IE_GROUP_DISPLAY, kMEFieldType_TextEntry);

	METableAddSimpleColumn(pTable,   "Min Value",    "MinNumericValue", 100, IE_GROUP_VALUES, kMEFieldType_TextEntry);
	METableAddSimpleColumn(pTable,   "Max Value",    "MaxNumericValue", 100, IE_GROUP_VALUES, kMEFieldType_TextEntry);
	METableAddGlobalDictColumn(pTable, "Overflow Numeric", "NumericOverflow", 140, IE_GROUP_VALUES, kMEFieldType_ValidatedTextEntry, "ItemDef", "resourceName");
	METableAddSimpleColumn(pTable,	 "Overflow Multiplier", "NumericOverflowMulti", 100, IE_GROUP_VALUES, kMEFieldType_TextEntry);
    METableAddGlobalDictColumn(pTable, "Spending Numeric", "SpendingNumeric", 140, IE_GROUP_VALUES, kMEFieldType_ValidatedTextEntry, "ItemDef", "resourceName");

	METableAddGlobalDictColumn(pTable,  "Mission",      "mission",     140, IE_GROUP_MISSION, kMEFieldType_ValidatedTextEntry, "Mission", "resourceName");
	METableAddSimpleColumn(pTable,      "Bag Size",     "NumBagSlots",  100, IE_GROUP_VALUES, kMEFieldType_TextEntry);

	METableAddEnumColumn(pTable,     "Req Skill Type",	".Restriction.SkillType",      100, IE_GROUP_CRAFT, kMEFieldType_Combo, SkillTypeEnum);
	METableAddSimpleColumn(pTable,   "Req Skill Level",	".Restriction.SkillLevel",     100, IE_GROUP_CRAFT, kMEFieldType_TextEntry);
	METableAddSimpleColumn(pTable,   "Req Min Level",	".Restriction.MinLevel",       100, IE_GROUP_RESTRICTION, kMEFieldType_TextEntry);
	METableAddSimpleColumn(pTable,   "Req Max Level",	".Restriction.MaxLevel",       100, IE_GROUP_RESTRICTION, kMEFieldType_TextEntry);
	METableAddExprColumn(pTable,     "Req Expr",		".Restriction.pRequiresBlock", 100, IE_GROUP_RESTRICTION, NULL);
	METableAddEnumColumn(pTable,     "Req UICategory", ".Restriction.UICategory", 100, IE_GROUP_RESTRICTION, kMEFieldType_Combo, UsageRestrictionCategoryEnum);
	METableAddGlobalDictColumn(pTable,  "Classes Allowed",      ".Restriction.ClassAllowed",     140, IE_GROUP_RESTRICTION, kMEFieldType_ValidatedTextEntry, "CharacterClass", "resourceName");
	METableAddEnumColumn(pTable,  "Class Categories Allowed",      ".Restriction.ClassCategoryAllowed", 140, IE_GROUP_RESTRICTION, kMEFieldType_FlagCombo, CharClassCategoryEnum);
	METableAddGlobalDictColumn(pTable,  "Paths Allowed",      ".Restriction.PathAllowed",     140, IE_GROUP_RESTRICTION, kMEFieldType_ValidatedTextEntry, "CharacterPath", "resourceName");
	METableAddGlobalDictColumn(pTable, "Required Allegiances", ".Restriction.RequiredAllegiance", 100, IE_GROUP_RESTRICTION, kMEFieldType_FlagCombo, "Allegiance", "resourceName");

	METableAddSimpleColumn(pTable,   "Equip Limit Max Count", ".EquipLimit.MaxEquipCount", 100, IE_GROUP_EQUIPLIMIT, kMEFieldType_TextEntry);
	METableAddEnumColumn(pTable,     "Equip Limit Category", ".EquipLimit.Category", 100, IE_GROUP_EQUIPLIMIT, kMEFieldType_Combo, ItemEquipLimitCategoryEnum);

	METableAddSimpleColumn(pTable,   "Callout",  ".calloutMsg.EditorCopy", 160, IE_GROUP_DISPLAY, kMEFieldType_Message);
	METableAddSimpleColumn(pTable,   "CalloutFSM", "calloutFSM", 160, IE_GROUP_DISPLAY, kMEFieldType_TextEntry);
	METableAddSimpleColumn(pTable,   "Important Callout", "importantCallout", 100, IE_GROUP_DISPLAY, kMEFieldType_BooleanCombo);

	METableAddEnumColumn(pTable,     "Lore Category",    "LoreCategory",   100, IE_GROUP_DISPLAY, kMEFieldType_Combo, LoreCategoryEnum);
	METableAddEnumColumn(pTable, "Lore Journal Type", ".JournalData.Type", 100, IE_GROUP_DISPLAY, kMEFieldType_Combo, LoreJournalTypeEnum);
	METableAddSimpleColumn(pTable,		"Lore Textures",			".JournalData.Textures",		180, IE_GROUP_DISPLAY, kMEFieldType_MultiTexture);
	METableAddGlobalDictColumn(pTable, "Lore Critter Def", ".JournalData.CritterName", 100, IE_GROUP_DISPLAY, kMEFieldType_ValidatedTextEntry, "CritterDef", "resourceName");
	METableAddEnumColumn(pTable, "Item Tag", "Tag", 100, IE_GROUP_DISPLAY, kMEFieldType_Combo, ItemTagEnum);
	METableAddEnumColumn(pTable, "Item Categories", "Categories", 100, IE_GROUP_DISPLAY, kMEFieldType_FlagCombo, ItemCategoryEnum);

	METableAddGlobalDictColumn(pTable, "Reward Table",".RewardPackInfo.hRewardTable", 120, IE_GROUP_VALUES, kMEFieldType_ValidatedTextEntry, "RewardTable", "resourceName");
	METableAddSimpleColumn(pTable, "Reward Pack Open Message", ".RewardPackInfo.UnpackMessage.EditorCopy", 160, IE_GROUP_DISPLAY, kMEFieldType_Message);
	METableAddSimpleColumn(pTable, "Reward Pack Open Failed Message", ".RewardPackInfo.UnpackFailedMessage.EditorCopy", 160, IE_GROUP_DISPLAY, kMEFieldType_Message);
	METableAddGlobalDictColumn(pTable, "Reward Pack Required Item", ".RewardPackInfo.RequiredItem", 160, IE_GROUP_RESTRICTION, kMEFieldType_ValidatedTextEntry, "ItemDef", "resourceName");
	METableAddSimpleColumn(pTable, "Reward Pack Required Item Count", ".RewardPackInfo.RequiredItemCount", 160, IE_GROUP_RESTRICTION, kMEFieldType_TextEntry);
	METableAddSimpleColumn(pTable, "Reward Pack Consume Required Items", ".RewardPackInfo.ConsumeRequiredItems", 100, IE_GROUP_RESTRICTION, kMEFieldType_BooleanCombo);
	METableAddGlobalDictColumn(pTable, "Reward Pack Required Item Product", ".RewardPackInfo.RequiredItemProduct", 240, IE_GROUP_RESTRICTION, kMEFieldType_ValidatedTextEntry, "MicroTransactionDef", "resourceName");

	METableAddGlobalDictColumn(pTable, "Pet Def", "PetGrant", 160, IE_GROUP_VALUES, kMEFieldType_ValidatedTextEntry, "PetDef", "resourceName");
	METableAddGlobalDictColumn(pTable, "SuperCritterPet Def", "SuperCritterPet", 160, IE_GROUP_VALUES, kMEFieldType_ValidatedTextEntry, "SuperCritterPetDef", "resourceName");
	METableAddGlobalDictColumn(pTable, "Algo Pet Def", "AlgoPet", 160, IE_GROUP_VALUES, kMEFieldType_ValidatedTextEntry, "AlgoPetDef", "resourceName");
	METableAddGlobalDictColumn(pTable, "Species", "Species", 160, IE_GROUP_VALUES, kMEFieldType_ValidatedTextEntry, "Species", "resourceName");
	
	METableAddSimpleColumn(pTable, "Make Puppet", "MakeAsPuppet", 100, IE_GROUP_VALUES, kMEFieldType_BooleanCombo);
	METableAddSimpleColumn(pTable, "Delete After Unlock", "DeleteAfterUnlock", 100, IE_GROUP_VALUES, kMEFieldType_BooleanCombo);
	METableAddSimpleColumn(pTable, "ItemSet Members Unique", "ItemSetMembersUnique", 100, IE_GROUP_VALUES, kMEFieldType_BooleanCombo);
	METableAddSimpleColumn(pTable, "Training Destroys Item", "TrainingDestroysItem", 100, IE_GROUP_VALUES, kMEFieldType_BooleanCombo);

	METableAddGlobalDictColumn(pTable, "InteriorUnlock","Interior",	120, IE_GROUP_DISPLAY, kMEFieldType_ValidatedTextEntry, "InteriorDef", "resourceName");

	METableAddEnumColumn(pTable, "Class Type", ".AttribModifyValues.SavedPetClassType",160,IE_ATTRIBMODIFY_VALUES, kMEFieldType_Combo, CharClassTypesEnum);
	
	METableAddExprColumn(pTable,   "Wpn Dmg Mag",  "@Damage@ExprMagnitude",  150, IE_GROUP_WEAPON, g_pItemContext);
	METableAddColumn(pTable,       "Wpn Dmg Table", "@Damage@TableName", 150, IE_GROUP_WEAPON, kMEFieldType_ValidatedTextEntry, NULL, NULL, NULL, NULL, NULL, NULL, ie_getPowerTableNames);
	METableAddSimpleColumn(pTable, "Wpn Dmg Variance",  "@Damage@Variance",  150, IE_GROUP_WEAPON, kMEFieldType_TextEntry);

	METableAddEnumColumn(pTable, "Special Part", "SpecialPartType", 100, IE_GROUP_INFO, kMEFieldType_Combo, SpecialPartTypeEnum);
	METableAddSimpleColumn(pTable, "Special Part count", "SpecialPartCount",100, IE_GROUP_VALUES, kMEFieldType_TextEntry);
	METableAddSimpleColumn(pTable, "Permission",            "GamePermission",	100, IE_GROUP_VALUES, kMEFieldType_TextEntry);

	METableAddSimpleColumn(pTable, "Experience", "uExperienceGift", 100, IE_GROUP_VALUES, kMEFieldType_TextEntry);

	// For tray item tray failure
	METableAddSimpleColumn(pTable, "Activate failure message", "MessageOnTrayActivateFailure", 170, IE_GROUP_VALUES, kMEFieldType_BooleanCombo);

	// coupon item info
	METableAddSimpleColumn(pTable, "Coupon uses item lvl", "CouponUsesItemLevel", 170, IE_GROUP_VALUES, kMEFieldType_BooleanCombo);
	METableAddSimpleColumn(pTable, "Coupon Discount", "uCouponDiscount", 100, IE_GROUP_VALUES, kMEFieldType_TextEntry);
	METableAddDictColumn(pTable,	"MT Categories", "MTCategories", 150,	IE_GROUP_VALUES,	kMEFieldType_ValidatedTextEntry, "MicroTransactionCategory", parse_MicroTransactionCategory, "Name");
	METableAddDictColumn(pTable,	"Microtransaction Item", "MTItem", 150,	IE_GROUP_VALUES,	kMEFieldType_ValidatedTextEntry, "MicroTransactionDef", parse_MicroTransactionDef, "Name");
	METableAddSimpleColumn(pTable, "UI Extra Safe Discard Prompt", "ExtraSafeRemove", 170, IE_GROUP_VALUES, kMEFieldType_BooleanCombo);

	METableAddSimpleColumn(pTable, "Bonus Percent", "BonusPercent", 160, IE_GROUP_VALUES, kMEFieldType_TextEntry);

	estrDestroy(&pcPath);
}

static void ie_initAttribColumns(METable *pTable)
{
	int id;

	ieAttribId = id = METableCreateSubTable(pTable, "Attrib Modify", ".AttribModifyValues.TempAttribs.Attributes", parse_TempAttribute, NULL, NULL, NULL, ie_createAttrib);

	METableAddSimpleSubColumn(pTable, id, "Attrib Modify", NULL, 80, NULL, 0);
	METableSetSubColumnState(pTable, id, "Attrib Modify", ME_STATE_LABEL);

	METableSetNumLockedSubColumns(pTable, id, 2);

	METableAddSubColumn(pTable,id,"Attrib","Attrib",NULL, 160,"Attrib Modify",kMEFieldType_ValidatedTextEntry,NULL,NULL,NULL,NULL,NULL,NULL,ie_GetAttribNames);
	METableAddSimpleSubColumn(pTable,id,"Value","Value",100,"Attrib Modify",kMEFieldType_TextEntry);
}

static void ie_initTrainableNodeColumns(METable *pTable)
{
	int id;

	// Create the subtable and get the ID
	ieTrainableNodesId = id = METableCreateSubTable(pTable, "TrainableNode", "TrainableNode", parse_ItemTrainablePowerNode, NULL,
													NULL, NULL, ie_createTrainableNode);

	METableAddSimpleSubColumn(pTable, id, "Trainable Node", NULL, 80, NULL, 0);
	METableSetSubColumnState(pTable, id, "Trainable Node", ME_STATE_LABEL);
	// Lock in label column
	METableSetNumLockedSubColumns(pTable, id, 4);

	METableAddDictSubColumn(pTable, id, "Trainable Node", "NodeDef", 240, IE_SUBGROUP_TRAINABLENODE, kMEFieldType_ValidatedTextEntry, "PowerTreeNodeDef", parse_PTNodeDef, "NameFull");
	METableAddSimpleSubColumn(pTable, id, "Node Rank", "Rank", 100, IE_SUBGROUP_TRAINABLENODE, kMEFieldType_TextEntry);
}

static void ie_initPowerColumns(METable *pTable)
{
	int id;

	// Create the subtable and get the ID
	iePowersId = id = METableCreateSubTable(pTable, "ItemPower", "ItemPowerDefRefs", parse_ItemPowerDefRef, NULL,
											NULL, NULL, ie_createPower);

	METableAddSimpleSubColumn(pTable, id, "Item Power", NULL, 80, NULL, 0);
	METableSetSubColumnState(pTable, id, "Item Power", ME_STATE_LABEL);

	// Lock in label column
	METableSetNumLockedSubColumns(pTable, id, 4);

	METableAddGlobalDictSubColumn(pTable, id, "Item Power Name", "ItemPowerDef", 240, IE_SUBGROUP_POWERS, kMEFieldType_ValidatedTextEntry, "ItemPowerDef", "resourceName");
	METableAddSimpleSubColumn(pTable, id, "Scale", "ScaleFactor", 100, IE_SUBGROUP_POWERS, kMEFieldType_TextEntry);
	METableAddSimpleSubColumn(pTable, id, "Set Min", "setMin", 100, IE_SUBGROUP_POWERS, kMEFieldType_TextEntry);
	METableAddEnumSubColumn(pTable, id,    "Applies To Slot", "AppliesToSlot",  100, IE_SUBGROUP_POWERS, kMEFieldType_Combo, InvBagIDsEnum);
	METableAddEnumSubColumn(pTable, id, "Required Categories", "RequiredCategories", 100, IE_GROUP_DISPLAY, kMEFieldType_FlagCombo, ItemCategoryEnum);
	METableAddSimpleSubColumn(pTable, id, "Chance To Apply", "ChanceToApply", 100, IE_SUBGROUP_POWERS, kMEFieldType_TextEntry);
	METableAddSimpleSubColumn(pTable, id, "Gem Slotting Apply Chance", "GemSlottingApplyChance", 100, IE_SUBGROUP_POWERS, kMEFieldType_TextEntry);
	METableAddSimpleSubColumn(pTable, id, "Scale Adjusted By Gem Slots", "bGemSlotsAdjustScaleFactor", 100, IE_SUBGROUP_POWERS, kMEFieldType_BooleanCombo);
}

static void ie_initGemHolderColumns(METable *pTable)
{
	int id;

	ieGemHolderID = id = METableCreateSubTable(pTable, "ItemGemSlot","ItemGemSlots",parse_ItemGemSlotDef, NULL,NULL,NULL,ie_createGemSlot);

	METableAddSimpleSubColumn(pTable,id,"Item Gem Slot",NULL,80,NULL,0);
	METableSetSubColumnState(pTable, id, "Item Gem Slot",ME_STATE_LABEL);
	METableAddGlobalDictSubColumn(pTable, id, "Base Item Gem", "PreSlottedGem", 120,NULL,kMEFieldType_ValidatedTextEntry, "ItemDef", "resourceName");

	// Lock in label column
	METableSetNumLockedSubColumns(pTable, id, 4);

	METableAddEnumSubColumn(pTable, id, "Gem Type Required","Type",100,NULL,kMEFieldType_FlagCombo,ItemGemTypeEnum);
}

static void ie_initComponentColumns(METable *pTable)
{
	int id;

	// Create the subtable and get the ID
	ieComponentId = id = METableCreateSubTable(pTable, "Component", ".Craft.Part", parse_ItemCraftingComponent, NULL,
											NULL, NULL, ie_createComponent);

	METableAddSimpleSubColumn(pTable, id, "Component", NULL, 80, NULL, 0);
	METableSetSubColumnState(pTable, id, "Component", ME_STATE_LABEL);

	// Lock in label column
	METableSetNumLockedSubColumns(pTable, id, 2);

	METableAddGlobalDictSubColumn(pTable, id, "Item Name", "item", 240, NULL, kMEFieldType_ValidatedTextEntry, "ItemDef", "resourceName");

	METableAddSimpleSubColumn(pTable, id, "Count", "count", 100, IE_SUBGROUP_CRAFT, kMEFieldType_TextEntry);
	METableAddSimpleSubColumn(pTable, id, "Weight", "Weight", 120, IE_SUBGROUP_CRAFT, kMEFieldType_TextEntry);
	METableAddSimpleSubColumn(pTable, id, "Min Level", "MinLevel", 120, IE_SUBGROUP_CRAFT, kMEFieldType_TextEntry);
	METableAddSimpleSubColumn(pTable, id, "Max Level", "MaxLevel", 120, IE_SUBGROUP_CRAFT, kMEFieldType_TextEntry);
	METableAddSimpleSubColumn(pTable, id, "Deconstruct Only", "DeconstructOnly", 160, IE_SUBGROUP_CRAFT, kMEFieldType_BooleanCombo);
	METableAddEnumSubColumn(pTable,  id,  "Count Type", "CountType", 100, IE_GROUP_DISPLAY, kMEFieldType_Combo, ComponentCountTypeEnum);
}


static void ie_initVanityPetColumns(METable *pTable)
{
	int id;

	// Create the subtable and get the ID
	ieVanityPetId = id = METableCreateSubTable(pTable, "VanityPet", "ItemVanityPetRefs", parse_ItemVanityPet, NULL,
											NULL, NULL, ie_createItemVanityPet);

	METableAddSimpleSubColumn(pTable, id, "VanityPet", NULL, 80, NULL, 0);
	METableSetSubColumnState(pTable, id, "VanityPet", ME_STATE_LABEL);

	// Lock in label column
	METableSetNumLockedSubColumns(pTable, id, 2);

	METableAddGlobalDictSubColumn(pTable, id, "Vanity Pet Name", "VanityPet", 240, IE_SUBGROUP_VANITYPET, kMEFieldType_ValidatedTextEntry, "PowerDef", "resourceName");
}


static void ie_initBonusNumericColumns(METable *pTable)
{
	int id;

	// Create the subtable and get the ID
	ieBonusNumericId = id = METableCreateSubTable(pTable, "Bonus Numerics", "BonusNumerics", parse_ItemDefBonus, NULL,
		NULL, NULL, ie_createBonusNumeric);

	METableAddSimpleSubColumn(pTable, id, "Bonus Numeric", NULL, 80, NULL, 0);
	METableSetSubColumnState(pTable, id, "Bonus Numeric", ME_STATE_LABEL);

	// Lock in label column
	METableSetNumLockedSubColumns(pTable, id, 2);

	METableAddGlobalDictSubColumn(pTable, id, "Numeric Name", "hItem", 240, IE_SUBGROUP_BONUS_NUMERIC, kMEFieldType_ValidatedTextEntry, "ItemDef", "resourceName");
}

static void ie_initCostumeColumns(METable *pTable)
{
	int id;

	// Create the subtable and get the ID
	ieCostumeId = id = METableCreateSubTable(pTable, "Costume", "Costumes", parse_ItemCostume, NULL,
		NULL, NULL, ie_createItemCostume);

	METableAddSimpleSubColumn(pTable, id, "Costume", NULL, 80, NULL, 0);
	METableSetSubColumnState(pTable, id, "Costume", ME_STATE_LABEL);

	// Lock in label column
	METableSetNumLockedSubColumns(pTable, id, 2);

	METableAddGlobalDictSubColumn(pTable, id, "Costume Name", "Costume", 240, IE_SUBGROUP_COSTUME, kMEFieldType_ValidatedTextEntry, "PlayerCostume", "resourceName");
	METableAddExprSubColumn(pTable, id, "Requires Expr", "ExprRequiresBlock", 120, IE_SUBGROUP_COSTUME, g_pItemContext);
}

void ie_GiveItem(METable *pTable, ItemDef *pItemDef, void *pUnused)
{
	globCmdParsef("GiveItem %s", pItemDef->pchName);
}

static void ie_init(MultiEditEMDoc *pEditorDoc)
{

	if (!ieWindow) {
		// Create the editor window
		ieWindow = MEWindowCreate("Item Editor", "Item", "Items", SEARCH_TYPE_ITEM, g_hItemDict, parse_ItemDef, "name", "filename", "scope", pEditorDoc);

		// Add item-specific columns
		ie_initItemColumns(ieWindow->pTable);

		// Add item-specific sub-columns
		ie_initTrainableNodeColumns(ieWindow->pTable);
		ie_initPowerColumns(ieWindow->pTable);
		ie_initCostumeColumns(ieWindow->pTable);
		ie_initComponentColumns(ieWindow->pTable);
		ie_initAttribColumns(ieWindow->pTable);
		ie_initVanityPetColumns(ieWindow->pTable);
		ie_initBonusNumericColumns(ieWindow->pTable);
		ie_initGemHolderColumns(ieWindow->pTable);
		METableFinishColumns(ieWindow->pTable);

		// Init the menus after adding all the columns
		MEWindowInitTableMenus(ieWindow);

		// Set the callbacks
		ie_initCallbacks(ieWindow, ieWindow->pTable);

		METableAddCustomAction(ieWindow->pTable, "Give Item", ie_GiveItem, NULL);
	}

	// Show the window
	ui_WindowPresent(ieWindow->pUIWindow);
}


//---------------------------------------------------------------------------------------------------
// Public Interface
//---------------------------------------------------------------------------------------------------

MEWindow *itemEditor_init(MultiEditEMDoc *pEditorDoc) 
{
	ie_init(pEditorDoc);	
	return ieWindow;
}


void itemEditor_createItem(char *pcName)
{
	// Create a new object since it is not in the dictionary
	// Add the object as a new object with no old
	void *pObject = ie_createObject(ieWindow->pTable, NULL, pcName, false);
	METableAddRowByObject(ieWindow->pTable, pObject, 1, 1);
}

#endif
