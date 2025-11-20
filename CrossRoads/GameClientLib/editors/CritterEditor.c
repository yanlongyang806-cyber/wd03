//
// CritterEditor.c
//

#ifndef NO_EDITORS

#include "cmdparse.h"
#include "dynfxinfo.h"
#include "entCritter.h"
#include "EntityMovementRequesterDefs.h"
#include "CostumeCommonTailor.h"
#include "CSVExport.h"
#include "MultiEditTable.h"
#include "MultiEditWindow.h"
#include "Powers.h"
#include "ResourceSearch.h"
#include "stringcache.h"
#include "TextParserInheritance.h"
#include "WorldGrid.h"
#include "aiStructCommon.h"
#include "ItemCommon.h"
#include "itemCommon_h_ast.h"
#include "ItemEnums.h"
#include "itemEnums_h_ast.h"
#include "inventoryCommon.h"

#include "AutoGen/entCritter_h_ast.h"
#include "AutoGen/EntityInteraction_h_ast.h"
#include "entEnums_h_ast.h"

#include "soundLib.h"

AUTO_RUN_ANON(memBudgetAddMapping(__FILE__, BUDGET_Editors););

#define CE_GROUP_NONE           NULL
#define CE_GROUP_MAIN			"Main"
#define CE_GROUP_IDENTITY		"Identity"
#define CE_GROUP_DISPLAY		"Display"
#define CE_GROUP_INTERACTION	"Interaction"
#define CE_GROUP_AI				"AI"
#define CE_GROUP_COMBAT         "Combat"
#define CE_GROUP_REWARDS		"Rewards"
#define CE_GROUP_UNDERLINGS		"Underling"
#define CE_GROUP_RIDING			"Riding"
#define CE_GROUP_WORLDINTERACTION "WorldInteraction"

#define CE_SUBGROUP_NONE        NULL
#define CE_SUBGROUP_COSTUME		"Costume"
#define CE_SUBGROUP_CHANCE		"Chance"
#define CE_SUBGROUP_AI			"AI"
#define CE_SUBGROUP_VAR			"Var"
#define CE_SUBGROUP_ITEM         "Item"

//-----------------------------------------------------------------------------------
// Global Data
//-----------------------------------------------------------------------------------

static MEWindow *ceWindow = NULL;
static int s_CritterCostumeConfigId;
static int s_CritterPowerConfigId;
static int s_CritterVarConfigId;
static int s_CritterLoreConfigId;
static int s_CritterItemConfigId;


//---------------------------------------------------------------------------------------------------
// Callbacks
//---------------------------------------------------------------------------------------------------


static int ce_compareStrings(const char** left, const char** right)
{
	return stricmp(*left,*right);
}


static int ce_compareCostumes(const CritterCostume **costume1, const CritterCostume **costume2)
{
	if ((*costume1)->fOrder > (*costume2)->fOrder) {
		return 1;
	} else if ((*costume1)->fOrder < (*costume2)->fOrder) {
		return -1;
	}
	return 0;
}


static int ce_comparePowers(const CritterPowerConfig **power1, const CritterPowerConfig **power2)
{
	if ((*power1)->fOrder > (*power2)->fOrder) {
		return 1;
	} else if ((*power1)->fOrder < (*power2)->fOrder) {
		return -1;
	}
	return 0;
}

static int ce_compareVars(const CritterVar **var1, const CritterVar **var2)
{
	if ((*var1)->fOrder > (*var2)->fOrder) {
		return 1;
	} else if ((*var1)->fOrder < (*var2)->fOrder) {
		return -1;
	}
	return 0;
}

static int ce_compareLore(const CritterLore **lore1, const CritterLore **lore2)
{
	if ((*lore1)->fOrder > (*lore2)->fOrder) {
		return 1;
	} else if ((*lore1)->fOrder < (*lore2)->fOrder) {
		return -1;
	}
	return 0;
}

static int ce_compareItems(const DefaultItemDef **item1, const DefaultItemDef **item2)
{
	if ((*item1)->fOrder > (*item2)->fOrder) {
		return 1;
	} else if ((*item1)->fOrder < (*item2)->fOrder) {
		return -1;
	}
	return 0;
}

static bool ce_doesInheritCostume(CritterDef *pCritterDef, CritterCostume *pCostume)
{
	char buf[128];
	int iType;

	// Not inherited if no parent power
	if (!pCritterDef->pInheritance) {
		return false;
	}

	// Look if this mod is inherited
	sprintf(buf,".Costume[\"%d\"]",pCostume->iKey);
	iType = StructInherit_GetOverrideType(parse_CritterDef, pCritterDef, buf);
	if (iType == OVERRIDE_ADD) {
		// Only added fields are local.  All others are from the parent.
		return false;
	}
	return true;
}


static bool ce_doesInheritPower(CritterDef *pCritterDef, CritterPowerConfig *pPower)
{
	char buf[128];
	int iType;

	// Not inherited if no parent power
	if (!pCritterDef->pInheritance) {
		return false;
	}

	// Look if this mod is inherited
	sprintf(buf,".PowerConfigs[\"%d\"]",pPower->iKey);
	iType = StructInherit_GetOverrideType(parse_CritterDef, pCritterDef, buf);
	if (iType == OVERRIDE_ADD) {
		// Only added fields are local.  All others are from the parent.
		return false;
	}
	return true;
}

static bool ce_doesInheritVar(CritterDef *pCritterDef, CritterVar *pVar)
{
	char buf[128];
	int iType;

	// Not inherited if no parent power
	if (!pCritterDef->pInheritance) {
		return false;
	}

	// Look if this mod is inherited
	sprintf(buf,".CritterVars[\"%d\"]",pVar->iKey);
	iType = StructInherit_GetOverrideType(parse_CritterDef, pCritterDef, buf);
	if (iType == OVERRIDE_ADD) {
		// Only added fields are local.  All others are from the parent.
		return false;
	}
	return true;
}

static bool ce_doesInheritLore(CritterDef *pCritterDef, CritterLore *pLore)
{
	char buf[128];
	int iType;

	// Not inherited if no parent power
	if (!pCritterDef->pInheritance) {
		return false;
	}

	// Look if this mod is inherited
	sprintf(buf,".CritterLoreEntries[\"%d\"]",pLore->iKey);
	iType = StructInherit_GetOverrideType(parse_CritterDef, pCritterDef, buf);
	if (iType == OVERRIDE_ADD) {
		// Only added fields are local.  All others are from the parent.
		return false;
	}
	return true;
}


static bool ce_doesInheritItem(CritterDef *pCritterDef, DefaultItemDef *pItem)
{
	char buf[128];
	int iType;

	// Not inherited if no parent power
	if (!pCritterDef->pInheritance) {
		return false;
	}

	// Look if this mod is inherited
	sprintf(buf,".CritterItems[\"%d\"]",pItem->iKey);
	iType = StructInherit_GetOverrideType(parse_CritterDef, pCritterDef, buf);
	if (iType == OVERRIDE_ADD) {
		// Only added fields are local.  All others are from the parent.
		return false;
	}
	return true;
}


static void ce_orderCostumes(METable *pTable, void ***peaOrigCostumes, void ***peaOrderedCostumes)
{
	int i, count;

	// Copy data to the ordered list
	count = eaSize(peaOrigCostumes);
	for(i=0; i<count; ++i) {
		eaPush(peaOrderedCostumes,(*peaOrigCostumes)[i]);
	}

	// QSort the list
	eaQSort(*peaOrderedCostumes,ce_compareCostumes);
}


static void ce_orderPowers(METable *pTable, void ***peaOrigPowers, void ***peaOrderedPowers)
{
	int i, count;

	// Copy data to the ordered list
	count = eaSize(peaOrigPowers);
	for(i=0; i<count; ++i) {
		eaPush(peaOrderedPowers,(*peaOrigPowers)[i]);
	}

	// QSort the list
	eaQSort(*peaOrderedPowers,ce_comparePowers);
}

static void ce_orderVars(METable *pTable, void ***peaOrigVars, void ***peaOrderedVars)
{
	int i, count;

	// Copy data to the ordered list
	count = eaSize(peaOrigVars);
	for(i=0; i<count; ++i) {
		eaPush(peaOrderedVars,(*peaOrigVars)[i]);
	}

	// QSort the list
	eaQSort(*peaOrderedVars,ce_compareVars);
}

static void ce_orderLore(METable *pTable, void ***peaOrigLore, void ***peaOrderedLore)
{
	int i, count;

	// Copy data to the ordered list
	count = eaSize(peaOrigLore);
	for(i=0; i<count; ++i) {
		eaPush(peaOrderedLore,(*peaOrigLore)[i]);
	}

	// QSort the list
	eaQSort(*peaOrderedLore,ce_compareLore);
}


static void ce_orderItems(METable *pTable, void ***peaOrigItems, void ***peaOrderedItems)
{
	int i, count;

	// Copy data to the ordered list
	count = eaSize(peaOrigItems);
	for(i=0; i<count; ++i) {
		eaPush(peaOrderedItems,(*peaOrigItems)[i]);
	}

	// QSort the list
	eaQSort(*peaOrderedItems,ce_compareItems);
}


// Returns 1 if reordering succeeds and 0 if not
static int ce_reorderCostumes(METable *pTable, CritterDef *pCritterDef, void ***peaCostumes, int pos1, int pos2)
{
	CritterCostume **eaCostumes = (CritterCostume**)*peaCostumes;
	CritterCostume *pTempCostume;
	float fTempOrder;
	bool bInheritPos1, bInheritPos2;

	// Swap so pos1 is always less than pos2
	if (pos1 > pos2) {
		int temp = pos1;
		pos1 = pos2;
		pos2 = temp;
	}
	if (pos1 != pos2 - 1) {
		// This code only supports shifting by one row right now
		// but it could be expanded to support multirow shifting later
		assertmsg(0,"Critter editor cannot reorder subrows by more than one row at a time");
	}
	
	// Get inheritance info
	bInheritPos1 = ce_doesInheritCostume(pCritterDef,eaCostumes[pos1]);
	bInheritPos2 = ce_doesInheritCostume(pCritterDef,eaCostumes[pos2]);

	if (bInheritPos1 && bInheritPos2) {
		// Cannot reorder fields if both are inherited
		ui_DialogPopup("Notice", "Cannot re-order two inherited costumes!");
		return 0;

	} else if (!bInheritPos1 && !bInheritPos2) {
		// Not inheriting, so simply swap priority
		fTempOrder = eaCostumes[pos1]->fOrder;
		eaCostumes[pos1]->fOrder = eaCostumes[pos2]->fOrder;
		eaCostumes[pos2]->fOrder = fTempOrder;

	} else if (bInheritPos1) {
		// Cannot change priority of pos1, so change pos2 to be halfway between
		// the pos1 value and the prior row (0.0 if no prior row)
		float fLow = 0.0;

		if (pos1 > 0) {
			fLow = eaCostumes[pos1-1]->fOrder;
		}
		eaCostumes[pos2]->fOrder = (fLow + eaCostumes[pos1]->fOrder) / 2;
		
	} else { // (bInheritPos2)
		// Cannot change priority of pos2, so change pos1 to be halfway between
		// the pos2 value and the following row (1.0 more than pos 2 if no following row)
		if (pos2 < eaSize(&eaCostumes) - 1) {
			eaCostumes[pos1]->fOrder = (eaCostumes[pos2]->fOrder + eaCostumes[pos2+1]->fOrder) / 2;
		} else {
			eaCostumes[pos1]->fOrder = eaCostumes[pos2]->fOrder + 1.0;
		}
	}

	// Swap position
	pTempCostume = eaCostumes[pos1];
	eaCostumes[pos1] = eaCostumes[pos2];
	eaCostumes[pos2] = pTempCostume;

	return 1;
}


// Returns 1 if reordering succeeds and 0 if not
static int ce_reorderPowers(METable *pTable, CritterDef *pCritterDef, void ***peaPowers, int pos1, int pos2)
{
	CritterPowerConfig **eaPowers = (CritterPowerConfig**)*peaPowers;
	CritterPowerConfig *pTempPower;
	float fTempOrder;
	bool bInheritPos1, bInheritPos2;

	// Swap so pos1 is always less than pos2
	if (pos1 > pos2) {
		int temp = pos1;
		pos1 = pos2;
		pos2 = temp;
	}
	if (pos1 != pos2 - 1) {
		// This code only supports shifting by one row right now
		// but it could be expanded to support multirow shifting later
		assertmsg(0,"Critter editor cannot reorder subrows by more than one row at a time");
	}
	
	// Get inheritance info
	bInheritPos1 = ce_doesInheritPower(pCritterDef,eaPowers[pos1]);
	bInheritPos2 = ce_doesInheritPower(pCritterDef,eaPowers[pos2]);

	if (bInheritPos1 && bInheritPos2) {
		// Cannot reorder fields if both are inherited
		ui_DialogPopup("Notice", "Cannot re-order two inherited powers!");
		return 0;

	} else if (!bInheritPos1 && !bInheritPos2) {
		// Not inheriting, so simply swap priority
		fTempOrder = eaPowers[pos1]->fOrder;
		eaPowers[pos1]->fOrder = eaPowers[pos2]->fOrder;
		eaPowers[pos2]->fOrder = fTempOrder;

	} else if (bInheritPos1) {
		// Cannot change priority of pos1, so change pos2 to be halfway between
		// the pos1 value and the prior row (0.0 if no prior row)
		float fLow = 0.0;

		if (pos1 > 0) {
			fLow = eaPowers[pos1-1]->fOrder;
		}
		eaPowers[pos2]->fOrder = (fLow + eaPowers[pos1]->fOrder) / 2;
		
	} else { // (bInheritPos2)
		// Cannot change priority of pos2, so change pos1 to be halfway between
		// the pos2 value and the following row (1.0 more than pos 2 if no following row)
		if (pos2 < eaSize(&eaPowers) - 1) {
			eaPowers[pos1]->fOrder = (eaPowers[pos2]->fOrder + eaPowers[pos2+1]->fOrder) / 2;
		} else {
			eaPowers[pos1]->fOrder = eaPowers[pos2]->fOrder + 1.0;
		}
	}

	// Swap position
	pTempPower = eaPowers[pos1];
	eaPowers[pos1] = eaPowers[pos2];
	eaPowers[pos2] = pTempPower;

	return 1;
}



// Returns 1 if reordering succeeds and 0 if not
static int ce_reorderVars(METable *pTable, CritterDef *pCritterDef, void ***peaVars, int pos1, int pos2)
{
	CritterVar **eaVars = (CritterVar**)*peaVars;
	CritterVar *pTempVar;
	float fTempOrder;
	bool bInheritPos1, bInheritPos2;

	// Swap so pos1 is always less than pos2
	if (pos1 > pos2) {
		int temp = pos1;
		pos1 = pos2;
		pos2 = temp;
	}
	if (pos1 != pos2 - 1) {
		// This code only supports shifting by one row right now
		// but it could be expanded to support multirow shifting later
		assertmsg(0,"Critter editor cannot reorder subrows by more than one row at a time");
	}

	// Get inheritance info
	bInheritPos1 = ce_doesInheritVar(pCritterDef,eaVars[pos1]);
	bInheritPos2 = ce_doesInheritVar(pCritterDef,eaVars[pos2]);

	if (bInheritPos1 && bInheritPos2) {
		// Cannot reorder fields if both are inherited
		ui_DialogPopup("Notice", "Cannot re-order two inherited Vars!");
		return 0;

	} else if (!bInheritPos1 && !bInheritPos2) {
		// Not inheriting, so simply swap priority
		fTempOrder = eaVars[pos1]->fOrder;
		eaVars[pos1]->fOrder = eaVars[pos2]->fOrder;
		eaVars[pos2]->fOrder = fTempOrder;

	} else if (bInheritPos1) {
		// Cannot change priority of pos1, so change pos2 to be halfway between
		// the pos1 value and the prior row (0.0 if no prior row)
		float fLow = 0.0;

		if (pos1 > 0) {
			fLow = eaVars[pos1-1]->fOrder;
		}
		eaVars[pos2]->fOrder = (fLow + eaVars[pos1]->fOrder) / 2;

	} else { // (bInheritPos2)
		// Cannot change priority of pos2, so change pos1 to be halfway between
		// the pos2 value and the following row (1.0 more than pos 2 if no following row)
		if (pos2 < eaSize(&eaVars) - 1) {
			eaVars[pos1]->fOrder = (eaVars[pos2]->fOrder + eaVars[pos2+1]->fOrder) / 2;
		} else {
			eaVars[pos1]->fOrder = eaVars[pos2]->fOrder + 1.0;
		}
	}

	// Swap position
	pTempVar = eaVars[pos1];
	eaVars[pos1] = eaVars[pos2];
	eaVars[pos2] = pTempVar;

	return 1;
}

static int ce_reorderLore(METable *pTable, CritterDef *pCritterDef, void ***peaLore, int pos1, int pos2)
{
	CritterLore **eaVars = (CritterLore**)*peaLore;
	CritterLore *pTempVar;
	float fTempOrder;
	bool bInheritPos1, bInheritPos2;

	// Swap so pos1 is always less than pos2
	if (pos1 > pos2) {
		int temp = pos1;
		pos1 = pos2;
		pos2 = temp;
	}
	if (pos1 != pos2 - 1) {
		// This code only supports shifting by one row right now
		// but it could be expanded to support multirow shifting later
		assertmsg(0,"Critter editor cannot reorder subrows by more than one row at a time");
	}

	// Get inheritance info
	bInheritPos1 = ce_doesInheritLore(pCritterDef,eaVars[pos1]);
	bInheritPos2 = ce_doesInheritLore(pCritterDef,eaVars[pos2]);

	if (bInheritPos1 && bInheritPos2) {
		// Cannot reorder fields if both are inherited
		ui_DialogPopup("Notice", "Cannot re-order two inherited Vars!");
		return 0;

	} else if (!bInheritPos1 && !bInheritPos2) {
		// Not inheriting, so simply swap priority
		fTempOrder = eaVars[pos1]->fOrder;
		eaVars[pos1]->fOrder = eaVars[pos2]->fOrder;
		eaVars[pos2]->fOrder = fTempOrder;

	} else if (bInheritPos1) {
		// Cannot change priority of pos1, so change pos2 to be halfway between
		// the pos1 value and the prior row (0.0 if no prior row)
		float fLow = 0.0;

		if (pos1 > 0) {
			fLow = eaVars[pos1-1]->fOrder;
		}
		eaVars[pos2]->fOrder = (fLow + eaVars[pos1]->fOrder) / 2;

	} else { // (bInheritPos2)
		// Cannot change priority of pos2, so change pos1 to be halfway between
		// the pos2 value and the following row (1.0 more than pos 2 if no following row)
		if (pos2 < eaSize(&eaVars) - 1) {
			eaVars[pos1]->fOrder = (eaVars[pos2]->fOrder + eaVars[pos2+1]->fOrder) / 2;
		} else {
			eaVars[pos1]->fOrder = eaVars[pos2]->fOrder + 1.0;
		}
	}

	// Swap position
	pTempVar = eaVars[pos1];
	eaVars[pos1] = eaVars[pos2];
	eaVars[pos2] = pTempVar;

	return 1;
}

// Returns 1 if reordering succeeds and 0 if not
static int ce_reorderItems(METable *pTable, CritterDef *pCritterDef, void ***peaItems, int pos1, int pos2)
{
	DefaultItemDef **eaItems = (DefaultItemDef**)*peaItems;
	DefaultItemDef *pTempItem;
	float fTempOrder;
	bool bInheritPos1, bInheritPos2;

	// Swap so pos1 is always less than pos2
	if (pos1 > pos2) {
		int temp = pos1;
		pos1 = pos2;
		pos2 = temp;
	}
	if (pos1 != pos2 - 1) {
		// This code only supports shifting by one row right now
		// but it could be expanded to support multirow shifting later
		assertmsg(0,"Critter editor cannot reorder subrows by more than one row at a time");
	}

	// Get inheritance info
	bInheritPos1 = ce_doesInheritItem(pCritterDef,eaItems[pos1]);
	bInheritPos2 = ce_doesInheritItem(pCritterDef,eaItems[pos2]);

	if (bInheritPos1 && bInheritPos2) {
		// Cannot reorder fields if both are inherited
		ui_DialogPopup("Notice", "Cannot re-order two inherited Items!");
		return 0;

	} else if (!bInheritPos1 && !bInheritPos2) {
		// Not inheriting, so simply swap priority
		fTempOrder = eaItems[pos1]->fOrder;
		eaItems[pos1]->fOrder = eaItems[pos2]->fOrder;
		eaItems[pos2]->fOrder = fTempOrder;

	} else if (bInheritPos1) {
		// Cannot change priority of pos1, so change pos2 to be halfway between
		// the pos1 value and the prior row (0.0 if no prior row)
		float fLow = 0.0;

		if (pos1 > 0) {
			fLow = eaItems[pos1-1]->fOrder;
		}
		eaItems[pos2]->fOrder = (fLow + eaItems[pos1]->fOrder) / 2;

	} else { // (bInheritPos2)
		// Cannot change priority of pos2, so change pos1 to be halfway between
		// the pos2 value and the following row (1.0 more than pos 2 if no following row)
		if (pos2 < eaSize(&eaItems) - 1) {
			eaItems[pos1]->fOrder = (eaItems[pos2]->fOrder + eaItems[pos2+1]->fOrder) / 2;
		} else {
			eaItems[pos1]->fOrder = eaItems[pos2]->fOrder + 1.0;
		}
	}

	// Swap position
	pTempItem = eaItems[pos1];
	eaItems[pos1] = eaItems[pos2];
	eaItems[pos2] = pTempItem;

	return 1;
}


static void *ce_CreateCritterCostume(METable *pTable, CritterDef *pCritterDef, CritterCostume *pClone, CritterCostume *pBefore, CritterCostume *pAfter)
{
	CritterCostume *pNew;
	F32 fLow = 0.0;

	// Allocate the object
	if (pClone)
		pNew = (CritterCostume*)StructClone(parse_CritterCostume, pClone);
	else
		pNew = (CritterCostume*)StructCreate(parse_CritterCostume);
	
	if (!pNew)
		return NULL;

	// Set up the order
	if (pBefore) {
		fLow = pBefore->fOrder;
	}
	if (pAfter) {
		pNew->fOrder = (fLow + pAfter->fOrder) / 2;
	} else {
		pNew->fOrder = fLow + 1.0;
	}

	// Update the key
	pNew->iKey = critterdef_GetNextCostumeKey(pCritterDef, 0);

	// Generate best guess defaults
	if (!pClone) {
		//pNew->pchName = (char*)allocAddString("TestDummy");
		//SET_HANDLE_FROM_STRING(g_hPlayerCostumeDict, "TestDummy", pNew->hCostumeRef);
		pNew->fWeight = 1.0;
	}

	return pNew;
}


static void *ce_CreateCritterPowerConfig(METable *pTable, CritterDef *pCritterDef, CritterPowerConfig *pClone, CritterPowerConfig *pBefore, CritterPowerConfig *pAfter)
{
	CritterPowerConfig *pNew;
	F32 fLow = 0.0;

	// Allocate the object
	if (pClone)
		pNew = (CritterPowerConfig*)StructClone(parse_CritterPowerConfig, pClone);
	else
		pNew = (CritterPowerConfig*)StructCreate(parse_CritterPowerConfig);
	
	if (!pNew)
		return NULL;

	// Set up the order
	if (pBefore) {
		fLow = pBefore->fOrder;
	}
	if (pAfter) {
		pNew->fOrder = (fLow + pAfter->fOrder) / 2;
	} else {
		pNew->fOrder = fLow + 1.0;
	}

	// Update the key
	pNew->iKey = critterdef_GetNextPowerConfigKey(pCritterDef, 0);

	// Generate best guess defaults
	if (!pClone) {
		SET_HANDLE_FROM_STRING("PowerDef","Choose a Power",pNew->hPower);
		pNew->fWeight = 0.f;
		pNew->fChance = 1.f;
		pNew->fAIWeight = 1.f;
		pNew->iMinLevel = pCritterDef->iMinLevel;
		pNew->iMaxLevel = pCritterDef->iMaxLevel;
	}

	return pNew;
}

static void *ce_CreateCritterVar(METable *pTable, CritterDef *pCritterDef, CritterVar *pClone, CritterVar *pBefore, CritterVar *pAfter)
{
	CritterVar *pNew;
	F32 fLow = 0.0;

	// Allocate the object
	if (pClone)
		pNew = (CritterVar*)StructClone(parse_CritterVar, pClone);
	else
		pNew = (CritterVar*)StructCreate(parse_CritterVar);

	if (!pNew)
		return NULL;

	// Set up the order
	if (pBefore) {
		fLow = pBefore->fOrder;
	}
	if (pAfter) {
		pNew->fOrder = (fLow + pAfter->fOrder) / 2;
	} else {
		pNew->fOrder = fLow + 1.0;
	}

	// Update the key
	pNew->iKey = critterdef_GetNextVarKey(pCritterDef, 0);

	return pNew;
}

static void *ce_CreateCritterLore(METable *pTable, CritterDef *pCritterDef, CritterLore *pClone, CritterLore *pBefore, CritterLore *pAfter)
{
	CritterLore *pNew;
	F32 fLow = 0.0;

	// Allocate the object
	if (pClone)
		pNew = (CritterLore*)StructClone(parse_CritterLore, pClone);
	else
		pNew = (CritterLore*)StructCreate(parse_CritterLore);

	if (!pNew)
		return NULL;

	// Set up the order
	if (pBefore) {
		fLow = pBefore->fOrder;
	}
	if (pAfter) {
		pNew->fOrder = (fLow + pAfter->fOrder) / 2;
	} else {
		pNew->fOrder = fLow + 1.0;
	}

	// Update the key
	pNew->iKey = critterdef_GetNextLoreKey(pCritterDef, 0);

	return pNew;
}


static void *ce_CreateDefaultItemDef(METable *pTable, CritterDef *pCritterDef, DefaultItemDef *pClone, DefaultItemDef *pBefore, DefaultItemDef *pAfter)
{
	DefaultItemDef *pNew;
	F32 fLow = 0.0;

	if(pClone)
		pNew = (DefaultItemDef *)StructClone(parse_DefaultItemDef, pClone);
	else
		pNew = (DefaultItemDef *)StructCreate(parse_DefaultItemDef);

	if(!pNew)
		return NULL;

	// Set up the order
	if (pBefore) {
		fLow = pBefore->fOrder;
	}
	if (pAfter) {
		pNew->fOrder = (fLow + pAfter->fOrder) / 2;
	} else {
		pNew->fOrder = fLow + 1.0;
	}

	// Update the key
	pNew->iKey = critterdef_GetNextItemKey(pCritterDef, 0);

	//Don't care about order, just push it
	return pNew;
}

static int ce_validateCallback(METable *pTable, CritterDef *pCritterDef, void *pUserData)
{
	return critter_DefValidate(pCritterDef, true);
}


static void ce_fixMessages(CritterDef *pCritterDef)
{
	char nameSpace[RESOURCE_NAME_MAX_SIZE];
	char baseObjectName[RESOURCE_NAME_MAX_SIZE];
	int ii;
	char *tmpS = NULL,*tmpS2 = NULL;
	int bInherited = !!StructInherit_GetParentName(parse_CritterDef, pCritterDef);

	estrStackCreate(&tmpS);
	estrStackCreate(&tmpS2);

	// Skip fixup if inheriting from parent
	if (bInherited &&
		pCritterDef->displayNameMsg.pEditorCopy &&
		StructInherit_GetOverrideTypeEx(parse_CritterDef, pCritterDef, ".displayNameMsg.Message", true) == OVERRIDE_NONE &&
		StructInherit_GetOverrideTypeEx(parse_CritterDef, pCritterDef, ".displayNameMsg.EditorCopy", true) == OVERRIDE_NONE )
	{
	}
	else
	{
		if (!pCritterDef->displayNameMsg.pEditorCopy) 
		{
			pCritterDef->displayNameMsg.pEditorCopy = StructCreate(parse_Message);
		}
		if (resExtractNameSpace(pCritterDef->pchName,nameSpace, baseObjectName))
		{
			estrPrintf(&tmpS, "%s:CritterDef.%s", nameSpace, baseObjectName);
		}
		else
		{
			estrPrintf(&tmpS, "CritterDef.%s", pCritterDef->pchName);
		}
		
		langFixupMessage(pCritterDef->displayNameMsg.pEditorCopy, tmpS, "Display name for a critter definition", "CritterDef");
	}

	// Skip fixup if inheriting from parent
	if (bInherited &&
		pCritterDef->displaySubNameMsg.pEditorCopy &&
		StructInherit_GetOverrideTypeEx(parse_CritterDef, pCritterDef, ".displaySubNameMsg.Message", true) == OVERRIDE_NONE &&
		StructInherit_GetOverrideTypeEx(parse_CritterDef, pCritterDef, ".displaySubNameMsg.EditorCopy", true) == OVERRIDE_NONE )
	{
	}
	else
	{
		if (!pCritterDef->displaySubNameMsg.pEditorCopy) 
		{
			pCritterDef->displaySubNameMsg.pEditorCopy = StructCreate(parse_Message);
		}
		if (resExtractNameSpace(pCritterDef->pchName,nameSpace, baseObjectName))
		{
			estrPrintf(&tmpS, "%s:CritterDef.%s.subName", nameSpace, baseObjectName);
		}
		else
		{
			estrPrintf(&tmpS, "CritterDef.%s.subName", pCritterDef->pchName);
		}

		langFixupMessage(pCritterDef->displaySubNameMsg.pEditorCopy, tmpS, "Display subname for a critter definition", "CritterDef");
	}

	if (bInherited &&
		pCritterDef->descriptionMsg.pEditorCopy &&
		StructInherit_GetOverrideTypeEx(parse_CritterDef, pCritterDef, ".descriptionMsg.Message", true) == OVERRIDE_NONE &&
		StructInherit_GetOverrideTypeEx(parse_CritterDef, pCritterDef, ".descriptionMsg.EditorCopy", true) == OVERRIDE_NONE )
	{
	}
	else
	{
		if (!pCritterDef->descriptionMsg.pEditorCopy) 
		{
			pCritterDef->descriptionMsg.pEditorCopy = StructCreate(parse_Message);
		}

		// Set up key if not exactly

		if (resExtractNameSpace(pCritterDef->pchName,nameSpace, baseObjectName))
		{
			estrPrintf(&tmpS, "%s:CritterDef.%s.description", nameSpace, baseObjectName);
		}
		else
		{
			estrPrintf(&tmpS, "CritterDef.%s.description", pCritterDef->pchName);
		}

		langFixupMessage(pCritterDef->descriptionMsg.pEditorCopy, tmpS, "Description for a critter definition", "CritterDef");
	}

	if (bInherited &&
		pCritterDef->hGroupOverrideDisplayNameMsg.pEditorCopy &&
		StructInherit_GetOverrideTypeEx(parse_CritterDef, pCritterDef, ".hGroupOverrideDisplayNameMsg.Message", true) == OVERRIDE_NONE &&
		StructInherit_GetOverrideTypeEx(parse_CritterDef, pCritterDef, ".hGroupOverrideDisplayNameMsg.EditorCopy", true) == OVERRIDE_NONE )
	{
	}
	else
	{
		if (!pCritterDef->hGroupOverrideDisplayNameMsg.pEditorCopy) 
		{
			pCritterDef->hGroupOverrideDisplayNameMsg.pEditorCopy = StructCreate(parse_Message);
		}

		// Set up key if not exactly

		if (resExtractNameSpace(pCritterDef->pchName,nameSpace, baseObjectName))
		{
			estrPrintf(&tmpS, "%s:CritterDef.%s.groupOverrideDisplayName", nameSpace, baseObjectName);
		}
		else
		{
			estrPrintf(&tmpS, "CritterDef.%s.groupOverrideDisplayName", pCritterDef->pchName);
		}

		langFixupMessage(pCritterDef->hGroupOverrideDisplayNameMsg.pEditorCopy, tmpS, "Override for the critter group display name", "CritterDef");
	}

	if (bInherited &&
		pCritterDef->oldInteractProps.interactText.pEditorCopy &&
		StructInherit_GetOverrideTypeEx(parse_CritterDef, pCritterDef, ".Interaction.interactTextMsg.Message", true) == OVERRIDE_NONE &&
		StructInherit_GetOverrideTypeEx(parse_CritterDef, pCritterDef, ".Interaction.interactTextMsg.EditorCopy", true) == OVERRIDE_NONE ) 
	{
	}
	else
	{
		if (!pCritterDef->oldInteractProps.interactText.pEditorCopy) 
		{
			pCritterDef->oldInteractProps.interactText.pEditorCopy = StructCreate(parse_Message);
		}

		// Set up key if not exactly

		if (resExtractNameSpace(pCritterDef->pchName,nameSpace, baseObjectName))
		{
			estrPrintf(&tmpS, "%s:CritterDef.%s.InteractText", nameSpace, baseObjectName);
		}
		else
		{
			estrPrintf(&tmpS, "CritterDef.%s.InteractText", pCritterDef->pchName);
		}
		langFixupMessage(pCritterDef->oldInteractProps.interactText.pEditorCopy, tmpS, "Interact text for a critter definition", "CritterDef");
	}

	if (bInherited &&
		pCritterDef->oldInteractProps.interactFailedText.pEditorCopy &&
		StructInherit_GetOverrideTypeEx(parse_CritterDef, pCritterDef, ".Interaction.interactFailedTextMsg.Message", true) == OVERRIDE_NONE &&
		StructInherit_GetOverrideTypeEx(parse_CritterDef, pCritterDef, ".Interaction.interactFailedTextMsg.EditorCopy", true) == OVERRIDE_NONE ) 
	{
	}
	else
	{
		if (!pCritterDef->oldInteractProps.interactFailedText.pEditorCopy) 
		{
			pCritterDef->oldInteractProps.interactFailedText.pEditorCopy = StructCreate(parse_Message);
		}

		// Set up key if not exactly
		if (resExtractNameSpace(pCritterDef->pchName,nameSpace, baseObjectName))
		{
			estrPrintf(&tmpS, "%s:CritterDef.%s.InteractFailedText", nameSpace, baseObjectName);
		}
		else
		{
			estrPrintf(&tmpS, "CritterDef.%s.InteractFailedText", pCritterDef->pchName);
		}		
		langFixupMessage(pCritterDef->oldInteractProps.interactFailedText.pEditorCopy, tmpS, "Interact failed text for a critter definition", "CritterDef");
	}


	if (bInherited &&
		StructInherit_GetOverrideTypeEx(parse_CritterDef, pCritterDef, ".interactProps.interactGameActions", true) == OVERRIDE_NONE) 
	{
	}
	else
	{
		if (pCritterDef->oldInteractProps.interactGameActions)
		{
			int i, n = eaSize(&pCritterDef->oldInteractProps.interactGameActions->eaActions);
			for (i = 0; i < n; i++)
			{
				WorldGameActionProperties *pAction = pCritterDef->oldInteractProps.interactGameActions->eaActions[i];
				if (pAction->pSendFloaterProperties)
				{
					Message *pMessage = pAction->pSendFloaterProperties->floaterMsg.pEditorCopy;
					if (pMessage)
					{
						if (resExtractNameSpace(pCritterDef->pchName,nameSpace, baseObjectName))
						{
							estrPrintf(&tmpS, "%s:CritterDef.%s.action_%d", nameSpace, baseObjectName, i);
						}
						else
						{
							estrPrintf(&tmpS, "CritterDef.%s.action_%d", pCritterDef->pchName, i);
						}
						langFixupMessage(pMessage, tmpS, "SendFloater text for an interact action on a critter definition", "CritterDef");
					}
				}
				else if (pAction->pSendNotificationProperties)
				{
					Message *pMessage = pAction->pSendNotificationProperties->notifyMsg.pEditorCopy;
					if (pMessage)
					{
						if (resExtractNameSpace(pCritterDef->pchName,nameSpace, baseObjectName))
						{
							estrPrintf(&tmpS, "%s:CritterDef.%s.action_%d", nameSpace, baseObjectName, i);
						}
						else
						{
							estrPrintf(&tmpS, "CritterDef.%s.action_%d", pCritterDef->pchName, i);
						}
						langFixupMessage(pMessage, tmpS, "SendNotification text for an interact action on a critter definition", "CritterDef");
					}
				}
			}
		}	
	}



	for(ii=0; ii<eaSize(&pCritterDef->ppCritterVars); ii++)
	{
		estrPrintf(&tmpS,".CritterVars[\"%d\"].Var.MessageVal.EditorCopy",pCritterDef->ppCritterVars[ii]->iKey);
		estrPrintf(&tmpS2,".CritterVars[\"%d\"].Var.MessageVal.Message",pCritterDef->ppCritterVars[ii]->iKey);

		if (bInherited &&
			pCritterDef->ppCritterVars[ii]->var.messageVal.pEditorCopy &&
			StructInherit_GetOverrideTypeEx(parse_CritterDef, pCritterDef, tmpS, true) == OVERRIDE_NONE &&
			StructInherit_GetOverrideTypeEx(parse_CritterDef, pCritterDef, tmpS2, true) == OVERRIDE_NONE ) 
		{
		}
		else
		{
			//fixup message
			if (!pCritterDef->ppCritterVars[ii]->var.messageVal.pEditorCopy) 
			{
				pCritterDef->ppCritterVars[ii]->var.messageVal.pEditorCopy = StructCreate(parse_Message);
			}

			// Set up key if not exactly
			if (resExtractNameSpace(pCritterDef->pchName,nameSpace, baseObjectName))
			{
				estrPrintf(&tmpS, "%s:CritterDef.%s.Var.%s.Msg", nameSpace, baseObjectName, pCritterDef->ppCritterVars[ii]->var.pcName);
			}
			else
			{
				estrPrintf(&tmpS, "CritterDef.%s.Var.%s.Msg", pCritterDef->pchName, pCritterDef->ppCritterVars[ii]->var.pcName);
			}		

			langFixupMessage(pCritterDef->ppCritterVars[ii]->var.messageVal.pEditorCopy, tmpS, "Msg text for a critter var", "CritterDef");
		}
	}

	for(ii=0; ii<eaSize(&pCritterDef->ppCostume); ii++)
	{
		estrPrintf(&tmpS,".Costume[\"%d\"].displayNameMsg.EditorCopy",pCritterDef->ppCostume[ii]->iKey);
		estrPrintf(&tmpS2,".Costume[\"%d\"].displayNameMsg.Message",pCritterDef->ppCostume[ii]->iKey);

		if (bInherited &&
			pCritterDef->ppCostume[ii]->displayNameMsg.pEditorCopy &&
			StructInherit_GetOverrideTypeEx(parse_CritterDef, pCritterDef, tmpS, true) == OVERRIDE_NONE &&
			StructInherit_GetOverrideTypeEx(parse_CritterDef, pCritterDef, tmpS2, true) == OVERRIDE_NONE ) 
		{
		}
		else
		{
			//fixup message
			if (!pCritterDef->ppCostume[ii]->displayNameMsg.pEditorCopy) 
			{
				pCritterDef->ppCostume[ii]->displayNameMsg.pEditorCopy = StructCreate(parse_Message);
			}

			// Set up key if not exactly
			if (resExtractNameSpace(pCritterDef->pchName,nameSpace, baseObjectName))
			{
				estrPrintf(&tmpS, "%s:CritterDef.%s.Costume.%d.%s.displayNameMsg", nameSpace, baseObjectName, ii, REF_STRING_FROM_HANDLE(pCritterDef->ppCostume[ii]->hCostumeRef) );
			}
			else
			{
				estrPrintf(&tmpS, "CritterDef.%s.Costume.%d.%s.displayNameMsg", pCritterDef->pchName, ii, REF_STRING_FROM_HANDLE(pCritterDef->ppCostume[ii]->hCostumeRef) );
			}		

			langFixupMessage(pCritterDef->ppCostume[ii]->displayNameMsg.pEditorCopy, tmpS, "displayNameMsg for a critter costume", "CritterDef");
		}
	}

	for(ii=0; ii<eaSize(&pCritterDef->ppCostume); ii++)
	{
		estrPrintf(&tmpS,".Costume[\"%d\"].displaySubNameMsg.EditorCopy",pCritterDef->ppCostume[ii]->iKey);
		estrPrintf(&tmpS2,".Costume[\"%d\"].displaySubNameMsg.Message",pCritterDef->ppCostume[ii]->iKey);

		if (bInherited &&
			pCritterDef->ppCostume[ii]->displaySubNameMsg.pEditorCopy &&
			StructInherit_GetOverrideTypeEx(parse_CritterDef, pCritterDef, tmpS, true) == OVERRIDE_NONE &&
			StructInherit_GetOverrideTypeEx(parse_CritterDef, pCritterDef, tmpS2, true) == OVERRIDE_NONE ) 
		{
		}
		else
		{
			//fixup message
			if (!pCritterDef->ppCostume[ii]->displaySubNameMsg.pEditorCopy) 
			{
				pCritterDef->ppCostume[ii]->displaySubNameMsg.pEditorCopy = StructCreate(parse_Message);
			}

			// Set up key if not exactly
			if (resExtractNameSpace(pCritterDef->pchName,nameSpace, baseObjectName))
			{
				estrPrintf(&tmpS, "%s:CritterDef.%s.Costume.%d.%s.displaySubNameMsg", nameSpace, baseObjectName, ii, REF_STRING_FROM_HANDLE(pCritterDef->ppCostume[ii]->hCostumeRef) );
			}
			else
			{
				estrPrintf(&tmpS, "CritterDef.%s.Costume.%d.%s.displaySubNameMsg", pCritterDef->pchName, ii, REF_STRING_FROM_HANDLE(pCritterDef->ppCostume[ii]->hCostumeRef) );
			}		

			langFixupMessage(pCritterDef->ppCostume[ii]->displaySubNameMsg.pEditorCopy, tmpS, "displaySubNameMsg for a critter costume", "CritterDef");
		}
	}

	estrDestroy(&tmpS);
	estrDestroy(&tmpS2);
}


static void ce_preSaveCallback(METable *pTable, CritterDef *pCritterDef)
{
	ce_fixMessages(pCritterDef);
}


static void ce_postOpenCallback(METable *pTable, CritterDef *pCritterDef, CritterDef *pOrigCritterDef)
{
	ce_fixMessages(pCritterDef);
	if (pOrigCritterDef) {
		ce_fixMessages(pOrigCritterDef);
	}
}


static void *ce_createObject(METable *pTable, CritterDef *pObjectToClone, char *pcNewName, bool bCloneKeepsKeys)
{
	CritterDef *pNewDef;
	const char *pcBaseName = NULL;
	const char *pcBaseDisplayName = NULL;
	const char *pcBaseDisplaySubName = NULL;
	const char *pcBaseDescription = NULL;
	int count = 0, i;
//	Message *pMessage;
	char *tmpS = NULL;

	estrStackCreate(&tmpS);

	// Create the object
	if (pObjectToClone) {
		// Create it
		pNewDef = StructClone(parse_CritterDef, pObjectToClone);
		assert(pNewDef);
		pNewDef->bTemplate = false; // Turn template flag off to prevent child or cloned critters from also being considered templates
		pcBaseName = pObjectToClone->pchName;
		pcBaseDisplayName = pObjectToClone->displayNameMsg.pEditorCopy ? pObjectToClone->displayNameMsg.pEditorCopy->pcDefaultString : NULL;
		pcBaseDisplaySubName = pObjectToClone->displaySubNameMsg.pEditorCopy ? pObjectToClone->displaySubNameMsg.pEditorCopy->pcDefaultString : NULL;
		pcBaseDescription = pObjectToClone->descriptionMsg.pEditorCopy ? pObjectToClone->descriptionMsg.pEditorCopy->pcDefaultString : NULL;
		if (!pcBaseDisplayName) {
			pcBaseDisplayName = "New Critter";
		}
		pNewDef->iKeyBlock = 0; // Force new key block

		if (!bCloneKeepsKeys) {
			CritterCostume **eaCostumes = NULL;
			CritterPowerConfig **eaConfigs= NULL;
			CritterVar **eaVars= NULL;
			CritterLore **eaLore= NULL;
			DefaultItemDef **eaItems= NULL;

			eaIndexedEnable(&eaCostumes, parse_CritterCostume);
			for(i=0; i<eaSize(&pNewDef->ppCostume); ++i) {
				if (!ce_doesInheritCostume(pNewDef,pNewDef->ppCostume[i])) {
					pNewDef->ppCostume[i]->iKey = critterdef_GetNextCostumeKey(pNewDef, 0);
				}
				eaPush(&eaCostumes, pNewDef->ppCostume[i]);
			}
			eaDestroy(&pNewDef->ppCostume);
			pNewDef->ppCostume = eaCostumes;

			eaIndexedEnable(&eaConfigs, parse_CritterPowerConfig);
			for(i=0; i<eaSize(&pNewDef->ppPowerConfigs); ++i) {
				if (!ce_doesInheritPower(pNewDef,pNewDef->ppPowerConfigs[i])) {
					pNewDef->ppPowerConfigs[i]->iKey = critterdef_GetNextPowerConfigKey(pNewDef, 0);
				}
				eaPush(&eaConfigs, pNewDef->ppPowerConfigs[i]);
			}
			eaDestroy(&pNewDef->ppPowerConfigs);
			pNewDef->ppPowerConfigs = eaConfigs;

			eaIndexedEnable(&eaVars, parse_CritterVar);
			for(i=0; i<eaSize(&pNewDef->ppCritterVars); ++i) {
				if (!ce_doesInheritVar(pNewDef,pNewDef->ppCritterVars[i])) {
					pNewDef->ppCritterVars[i]->iKey = critterdef_GetNextVarKey(pNewDef, 0);
				}
				eaPush(&eaVars, pNewDef->ppCritterVars[i]);
			}
			eaDestroy(&pNewDef->ppCritterVars);
			pNewDef->ppCritterVars = eaVars;

			eaIndexedEnable(&eaLore, parse_CritterLore);
			for(i=0; i<eaSize(&pNewDef->ppCritterLoreEntries); ++i) {
				if (!ce_doesInheritLore(pNewDef,pNewDef->ppCritterLoreEntries[i])) {
					pNewDef->ppCritterLoreEntries[i]->iKey = critterdef_GetNextLoreKey(pNewDef, 0);
				}
				eaPush(&eaLore, pNewDef->ppCritterLoreEntries[i]);
			}
			eaDestroy(&pNewDef->ppCritterLoreEntries);
			pNewDef->ppCritterLoreEntries = eaLore;

			eaIndexedEnable(&eaItems, parse_DefaultItemDef);
			for(i=0; i<eaSize(&pNewDef->ppCritterItems); ++i) {
				if (!ce_doesInheritItem(pNewDef,pNewDef->ppCritterItems[i])) {
					pNewDef->ppCritterItems[i]->iKey = critterdef_GetNextItemKey(pNewDef, 0);
				}
				eaPush(&eaItems, pNewDef->ppCritterItems[i]);
			}
			eaDestroy(&pNewDef->ppCritterItems);
			pNewDef->ppCritterItems = eaItems;

		}
	} else {
		pNewDef = StructCreate(parse_CritterDef);

		// Set some defaults
		pNewDef->iMinLevel = 0;
		pNewDef->iMaxLevel = -1;
		pNewDef->pcRank = g_pcCritterDefaultRank;
		if (gConf.bManualSubRank)
		{
			pNewDef->pcSubRank = g_pcCritterDefaultSubRank;
		}
		// TODO: Would be nice to pick a default class
		// TODO: Would be nice to pick a default costume
		// TODO: Would be nice to pick a default power

		pcBaseName = "_New_Critter";
		pcBaseDisplayName = "New Critter";
	}
	// Use provided name if available
	if (pcNewName) {
		pcBaseName = pcNewName;
		pcBaseDisplayName = pcNewName;
	}

	assertmsg(pNewDef, "Failed to create critter");

	// Assign a new name
	pNewDef->pchName = (char*)METableMakeNewNameShared(pTable,pcBaseName, true);

	//make all the messages
	langMakeEditorCopy(parse_CritterDef, pNewDef, true);
	pNewDef->displayNameMsg.pEditorCopy->pcDefaultString = StructAllocString(pcBaseDisplayName);
	pNewDef->displaySubNameMsg.pEditorCopy->pcDefaultString = StructAllocString(pcBaseDisplaySubName);

	// Create display name message
//	estrPrintf(&tmpS, "CritterDef.%s", pNewDef->pchName);
//	pMessage = langCreateMessage(tmpS, "Display name for a critter definition", "CritterDef", pcBaseDisplayName);
//	pNewDef->displayNameMsg.pEditorCopy = pMessage;

	// Create display name message
//	estrPrintf(&tmpS, "CritterDef.%s", pNewDef->pchName);
//	pMessage = langCreateMessage(tmpS, "Display subname for a critter definition", "CritterDef", pcBaseDisplaySubName);
//	pNewDef->displaySubNameMsg.pEditorCopy = pMessage;

	// Create description name message
//	estrPrintf(&tmpS, "CritterDef.%s.description", pNewDef->pchName);
//	pMessage = langCreateMessage(tmpS, "description for a critter definition", "CritterDef", pcBaseDescription);
//	pNewDef->descriptionMsg.pEditorCopy = pMessage;

	// Assign a file
	estrPrintf(&tmpS, "defs/critters/%s.critter",pNewDef->pchName);
	pNewDef->pchFileName = (char*)allocAddString(tmpS);

	estrDestroy(&tmpS);

	return pNewDef;
}


static void ce_powerChangeCallback(METable *pTable, CritterDef *pCritterDef, CritterPowerConfig *pPowerConfig, void *pUserData, bool bInitNotify)
{
	// When changing the power, initialize the min/max range from the power itself
	PowerDef *pPowerDef;
	
	if (!bInitNotify && REF_STRING_FROM_HANDLE(pPowerConfig->hPower)) {
		pPowerDef = GET_REF(pPowerConfig->hPower);
		if (pPowerDef) {
			pPowerConfig->fAIPreferredMaxRange = pPowerDef->iAIMaxRange;
			pPowerConfig->fAIPreferredMinRange = pPowerDef->iAIMinRange;
		}
	}
}


static void ce_powerGroupChangeCallback(METable *pTable, CritterDef *pCritterDef, CritterPowerConfig *pPowerConfig, void *pUserData, bool bInitNotify)
{
	int iGroup = pPowerConfig->iGroup;
	F32 fWeight = pPowerConfig->fWeight;
	F32 fChance = pPowerConfig->fChance;
	int i;
	bool bChanged = false;

	// Scan all rows in the same group
	for(i=eaSize(&pCritterDef->ppPowerConfigs)-1; i>=0; --i) {
		if ((pCritterDef->ppPowerConfigs[i] != pPowerConfig) && (pCritterDef->ppPowerConfigs[i]->iGroup == iGroup)) {
			// Force all the others in the same group to have the same weight as this one
			if (pCritterDef->ppPowerConfigs[i]->fWeight != fWeight) {
				pCritterDef->ppPowerConfigs[i]->fWeight = fWeight;
				bChanged = true;
			}

			// If this group is weight is zero, then all others in this group must have same chance
			if ((fWeight == 0.0) && (pCritterDef->ppPowerConfigs[i]->fChance != fChance)) {
				pCritterDef->ppPowerConfigs[i]->fChance = fChance;
				bChanged = true;
			}
		}
	}
	if (bChanged) {
		METableRefreshRow(pTable, pCritterDef);
	}
}


static void ce_disabledChangeCallback(METable *pTable, CritterDef *pCritterDef, CritterPowerConfig *pPowerConfig, void *pUserData, bool bInitNotify)
{
	if (pPowerConfig->bDisabled) {
		METableSetSubGroupNotApplicable(pTable, pCritterDef, s_CritterPowerConfigId, pPowerConfig, CE_SUBGROUP_CHANCE, 1);
		METableSetSubGroupNotApplicable(pTable, pCritterDef, s_CritterPowerConfigId, pPowerConfig, CE_SUBGROUP_AI, 1);
	} else {
		METableSetSubGroupNotApplicable(pTable, pCritterDef, s_CritterPowerConfigId, pPowerConfig, CE_SUBGROUP_CHANCE, 0);
		METableSetSubGroupNotApplicable(pTable, pCritterDef, s_CritterPowerConfigId, pPowerConfig, CE_SUBGROUP_AI, 0);
	}
}

static void ce_ridableChangeCallback(METable *pTable, CritterDef *pCritterDef, void *pUserData, bool bInitNotify)
{
	if (!pCritterDef->pExprRidable) {
		METableSetFieldNotApplicable(pTable, pCritterDef, "Ride Anim Bit", 1);
		METableSetFieldNotApplicable(pTable, pCritterDef, "Ride Power", 1);
	} else {
		METableSetFieldNotApplicable(pTable, pCritterDef, "Ride Anim Bit", 0);
		METableSetFieldNotApplicable(pTable, pCritterDef, "Ride Power", 0);
	}
}

static void ce_VarTypeChangeCallback(METable *pTable, CritterDef *pCritterDef, CritterVar *pVar, void *pUserData, bool bInitNotify)
{
	METableSetSubFieldNotApplicable(pTable, pCritterDef, s_CritterVarConfigId, pVar, "Int Val", (pVar->var.eType != WVAR_INT));
	METableSetSubFieldNotApplicable(pTable, pCritterDef, s_CritterVarConfigId, pVar, "Float Val", (pVar->var.eType != WVAR_FLOAT));
	METableSetSubFieldNotApplicable(pTable, pCritterDef, s_CritterVarConfigId, pVar, "String", ((pVar->var.eType != WVAR_STRING) && (pVar->var.eType != WVAR_LOCATION_STRING)));
	METableSetSubFieldNotApplicable(pTable, pCritterDef, s_CritterVarConfigId, pVar, "Message", (pVar->var.eType != WVAR_MESSAGE));
	METableSetSubFieldNotApplicable(pTable, pCritterDef, s_CritterVarConfigId, pVar, "Animation", (pVar->var.eType != WVAR_ANIMATION));
	METableSetSubFieldNotApplicable(pTable, pCritterDef, s_CritterVarConfigId, pVar, "Critter Def", (pVar->var.eType != WVAR_CRITTER_DEF));
	METableSetSubFieldNotApplicable(pTable, pCritterDef, s_CritterVarConfigId, pVar, "Critter Group", (pVar->var.eType != WVAR_CRITTER_GROUP));
}

static void *ce_tableCreateCallback(METable *pTable, CritterDef *pObjectToClone, bool bCloneKeepsKeys)
{
	return ce_createObject(pTable, pObjectToClone, NULL, bCloneKeepsKeys);
}


static void *ce_windowCreateCallback(MEWindow *pWindow, CritterDef *pObjectToClone)
{
	return ce_createObject(pWindow->pTable, pObjectToClone, NULL, false);
}

static void ce_dictChangeCallback(enumResourceEventType eType, const char *pDictName, ConstReferenceData pRefData, Referent pReferent, METable *pTable)
{
	METableDictChanged(pTable, eType, pReferent, pRefData);
}


static void ce_messageDictChangeCallback(enumResourceEventType eType, const char *pDictName, const char *pcMessageKey, Referent pReferent, METable *pTable)
{
	if ((eType == RESEVENT_RESOURCE_MODIFIED) ||
		(eType == RESEVENT_RESOURCE_REMOVED) ||
		(eType == RESEVENT_RESOURCE_ADDED)) {

		METableMessageChangedRefresh(pTable, pcMessageKey);
	}
}


static char** ce_getRankNames(METable *pTable, void *pUnused)
{
	char **eaResult = NULL;

	critterRankGetNameCopies(&eaResult);

	return eaResult;
}

static char** ce_getSubRankNames(METable *pTable, void *pUnused)
{
	char **eaResult = NULL;

	eaPush(&eaResult, NULL);
	critterSubRankGetNameCopies(&eaResult);
	
	return eaResult;
}


static void ce_initCallbacks(MEWindow *pWindow, METable *pTable)
{
	// General Window callbacks
	MEWindowSetCreateCallback(pWindow, ce_windowCreateCallback);

	// General table callbacks
	METableSetValidateCallback(pTable, ce_validateCallback, pTable);
	METableSetPostOpenCallback(pTable, ce_postOpenCallback);
	METableSetPreSaveCallback(pTable, ce_preSaveCallback);
	METableSetCreateCallback(pTable, ce_tableCreateCallback);

	// Column change callbacks
	METableSetSubColumnChangeCallback(pTable, s_CritterPowerConfigId, "Power Name", ce_powerChangeCallback, NULL);
	METableSetSubColumnChangeCallback(pTable, s_CritterPowerConfigId, "Group", ce_powerGroupChangeCallback, NULL);
	METableSetSubColumnChangeCallback(pTable, s_CritterPowerConfigId, "Weight", ce_powerGroupChangeCallback, NULL);
	METableSetSubColumnChangeCallback(pTable, s_CritterPowerConfigId, "Chance", ce_powerGroupChangeCallback, NULL);
	METableSetSubColumnChangeCallback(pTable, s_CritterPowerConfigId, "Disabled", ce_disabledChangeCallback, NULL);
	METableSetColumnChangeCallback(pTable, "Ridable", ce_ridableChangeCallback, NULL);

	METableSetSubColumnChangeCallback(pTable, s_CritterVarConfigId, "Type", ce_VarTypeChangeCallback, NULL);

	// We need this registered here instead of by METable because the dictionary will 
	// only allow each callback function to be registered once and there may be multiple
	// METable instances.  Our local callback just passes through to the METable.
	resDictRegisterEventCallback(g_hCritterDefDict, ce_dictChangeCallback, pTable);
	resDictRegisterEventCallback(gMessageDict, ce_messageDictChangeCallback, pTable);
}


//---------------------------------------------------------------------------------------------------
// UI Init
//---------------------------------------------------------------------------------------------------

static void ce_initCritterColumns(METable *pTable)
{
	// Main
	METableAddSimpleColumn(pTable, "Name", "name", 160, CE_GROUP_NONE, kMEFieldType_TextEntry);
	METableSetColumnState(pTable, "Name", ME_STATE_NOT_PARENTABLE);

	// Lock the name column in
	METableSetNumLockedColumns(pTable, 2);

	METableAddSimpleColumn(pTable,   "Display Name",    ".DisplayNameMsg.EditorCopy",   160, CE_GROUP_MAIN, kMEFieldType_Message);
	METableAddSimpleColumn(pTable,   "Display SubName", ".DisplaySubNameMsg.EditorCopy",   160, CE_GROUP_MAIN, kMEFieldType_Message);
	METableAddParentColumn(pTable,   "Parent Critter",               160, CE_GROUP_MAIN, true);
	METableAddScopeColumn(pTable,    "Scope",           "Scope",     160, CE_GROUP_MAIN, kMEFieldType_TextEntry); // Not validated on purpose
	METableAddFileNameColumn(pTable, "File Name",    "fileName",     210, CE_GROUP_MAIN, NULL, "defs/critters", "defs/critters", ".critter", UIBrowseNewOrExisting);
	METableAddSimpleColumn(pTable,   "Deprecated",   "Deprecated",   100, CE_GROUP_IDENTITY, kMEFieldType_BooleanCombo);
	METableAddSimpleColumn(pTable,   "Notes",        "Comment",      160, CE_GROUP_MAIN, kMEFieldType_MultiText);
	METableAddSimpleColumn(pTable,   "Description",  ".DescriptionMsg.EditorCopy",   160, CE_GROUP_MAIN, kMEFieldType_Message);
	METableAddSimpleColumn(pTable,   "Group Name Override",  ".hGroupOverrideDisplayNameMsg.EditorCopy",   160, CE_GROUP_MAIN, kMEFieldType_Message);
	METableAddGlobalDictColumn(pTable, "Override Movement Requester", "OverrideMovementRequesterDef", 150, CE_GROUP_MAIN, kMEFieldType_ValidatedTextEntry, "MovementRequesterDef", "resourceName");

		// Not showing index field

	// Identity 
	METableAddSimpleColumn(pTable,   "Min Level",     "minLevel",         100, CE_GROUP_IDENTITY, kMEFieldType_TextEntry);
	METableAddSimpleColumn(pTable,   "Max Level",     "maxLevel",         100, CE_GROUP_IDENTITY, kMEFieldType_TextEntry);
	METableAddSimpleColumn(pTable,   "No Cross Fade", "noCrossFade",      120, CE_GROUP_IDENTITY, kMEFieldType_BooleanCombo);
	METableAddGlobalDictColumn(pTable,"Faction",      "Faction",          150, CE_GROUP_IDENTITY, kMEFieldType_ValidatedTextEntry, "CritterFaction", "resourceName");
	METableAddGlobalDictColumn(pTable,"Species",      "Species",          150, CE_GROUP_IDENTITY, kMEFieldType_ValidatedTextEntry, "Species", "resourceName");
	METableAddGlobalDictColumn(pTable,   "Group",     "GroupName",        120, CE_GROUP_IDENTITY, kMEFieldType_ValidatedTextEntry, "CritterGroup", "resourceName");
	METableAddEnumColumn(pTable,   "Critter Tags",    "CritterTags",      120, CE_GROUP_IDENTITY, kMEFieldType_FlagCombo, CritterTagsEnum);
	METableAddEnumColumn(pTable,   "Spawn Limit",     "SpawnLimit",       100, CE_GROUP_IDENTITY, kMEFieldType_Combo, CritterSpawnLimitEnum);
	METableAddSimpleColumn(pTable, "Spawn Weighting", "SpawnWeight",      100, CE_GROUP_IDENTITY, kMEFieldType_TextEntry);
	METableAddGlobalDictColumn(pTable,"Class",        "Class",            160, CE_GROUP_IDENTITY, kMEFieldType_ValidatedTextEntry, "CharacterClassInfo", "resourceName");
	METableAddColumn(pTable,       "Rank",            "Rank",             100, CE_GROUP_IDENTITY, kMEFieldType_Combo, NULL, NULL, NULL, NULL, NULL, NULL, ce_getRankNames);

	if (gConf.bManualSubRank)
		METableAddColumn(pTable,       "Sub Rank",            "SubRank",             100, CE_GROUP_IDENTITY, kMEFieldType_Combo, NULL, NULL, NULL, NULL, NULL, NULL, ce_getSubRankNames);

	METableAddEnumColumn(pTable,   "Skill Type",      "Skilltype",        100, CE_GROUP_IDENTITY, kMEFieldType_Combo, SkillTypeEnum);
	METableAddSimpleColumn(pTable, "Template",		  "template",		  100, CE_GROUP_IDENTITY, kMEFieldType_BooleanCombo);
	METableSetColumnState(pTable,  "Template", ME_STATE_NOT_PARENTABLE);
	METableAddSimpleColumn(pTable, "Disabled For Contacts", "DisabledForContacts", 100, CE_GROUP_IDENTITY, kMEFieldType_BooleanCombo);
	METableAddSimpleColumn(pTable, "PvP Flag",		  "PvPFlagged",		  100, CE_GROUP_IDENTITY, kMEFieldType_BooleanCombo);
	METableAddSimpleColumn(pTable, "Random Civilian Name", "randomCivilianName", 100, CE_GROUP_IDENTITY, kMEFieldType_BooleanCombo);

	// Display Data
	METableAddGlobalDictColumn(pTable, "Default Stance Power", "DefaultStanceDef",     210, CE_GROUP_DISPLAY, kMEFieldType_ValidatedTextEntry, "PowerDef", "resourceName");
	METableAddSimpleColumn(pTable, "Random Default Stance", "RandomDefaultStance",     100, CE_GROUP_DISPLAY, kMEFieldType_BooleanCombo);
	METableAddSimpleColumn(pTable, "Random Costume", "GenerateRandomCostume",		   100, CE_GROUP_DISPLAY, kMEFieldType_BooleanCombo);
	METableAddGlobalDictColumn(pTable,   "Stances",    "StanceWords", 200, CE_GROUP_DISPLAY, kMEFieldType_ValidatedTextEntry, "Stance", "resourceName");
	METableAddSimpleColumn(pTable, "Always Have Weapons Ready",    "AlwaysHaveWeaponsReady",    160, CE_GROUP_COMBAT, kMEFieldType_BooleanCombo);

	// AI CE_GROUP_AI
	METableAddGlobalDictColumn(	pTable,	"AI Config", "AIConfig", 160, CE_GROUP_AI, kMEFieldType_ValidatedTextEntry, "AIConfig", "resourceName"); 
	//METableAddColumnAlternatePath(pTable, "AI Config", ".AI");
	METableAddGlobalDictColumn(pTable,"AI FSM", "FSM",                200, CE_GROUP_AI, kMEFieldType_ValidatedTextEntry, "FSM", "resourceName");
	METableAddGlobalDictColumn(pTable,"Combat FSM","CombatFSM",       200, CE_GROUP_AI, kMEFieldType_ValidatedTextEntry, "FSM", "resourceName");
	METableAddGlobalDictColumn(pTable, "Spawn Anim", "SpawnAnim",         150, CE_GROUP_AI, kMEFieldType_TextEntry, "AIAnimList", "resourceName");
	METableAddSimpleColumn(pTable, "Spawn Anim Time", "SpawnLockdownTime", 150, CE_GROUP_AI, kMEFieldType_TextEntry); 
	METableAddGlobalDictColumn(pTable, "Alt Spawn Anim", "SpawnAnimAlternate", 150, CE_GROUP_AI, kMEFieldType_TextEntry, "AIAnimList", "resourceName");
	METableAddSimpleColumn(pTable, "Alt Spawn Anim Time", "SpawnLockdownTimeAlternate", 150, CE_GROUP_AI, kMEFieldType_TextEntry);

		
	METableAddGlobalDictColumn(pTable, "Interaction Def", "InteractionDef", 200, CE_GROUP_INTERACTION, kMEFieldType_ValidatedTextEntry, "InteractionDef", "resourceName");
	METableAddSimpleColumn(pTable, "Interact Def Range", "InteractRange", 100, CE_GROUP_INTERACTION, kMEFieldType_TextEntry);
	METableAddSimpleColumn(pTable, "Minimum see at distance", "EntityMinSeeAtDistance", 160, CE_GROUP_INTERACTION, kMEFieldType_TextEntry);

	if (gConf.bAllowOldEncounterData) {
		// Interaction CE_GROUP_INTERACTION
		METableAddExprColumn(pTable,      "Interact Condition", ".Interaction.InteractConditionBlock",    140, CE_GROUP_INTERACTION, NULL);
		METableAddExprColumn(pTable,      "Success Condition",  ".Interaction.SuccessConditionBlock",     140, CE_GROUP_INTERACTION, NULL);
		METableAddGameActionBlockColumn(pTable, "Safe Actions", ".Interaction.InteractGameActionBlock",  100, CE_GROUP_INTERACTION, NULL);
		METableAddExprColumn(pTable,      "Action Expr",        ".Interaction.InteractActionBlock",       120, CE_GROUP_INTERACTION, NULL);

		METableAddEnumColumn(pTable,      "Interact Type",      ".Interaction.InteractType",         140, CE_GROUP_INTERACTION, kMEFieldType_FlagCombo, InteractTypeEnum);
		METableAddSimpleColumn(pTable,    "Interact Time",      ".Interaction.uInteractTime",        120, CE_GROUP_INTERACTION, kMEFieldType_TextEntry);
		METableAddSimpleColumn(pTable,    "Active For Time",    ".Interaction.uInteractActiveFor",   120, CE_GROUP_INTERACTION, kMEFieldType_TextEntry);
		METableAddSimpleColumn(pTable,    "Cool Down Time",     ".Interaction.uInteractCoolDown",    120, CE_GROUP_INTERACTION, kMEFieldType_TextEntry);
		METableAddSimpleColumn(pTable,    "Interact Message",   ".Interaction.InteractTextMsg.EditorCopy",       120, CE_GROUP_INTERACTION, kMEFieldType_Message);
		METableAddSimpleColumn(pTable,    "Failed Message",     ".Interaction.InteractFailedTextMsg.EditorCopy", 120, CE_GROUP_INTERACTION, kMEFieldType_Message);
		METableAddGlobalDictColumn(pTable,"Interact Anim",      ".Interaction.InteractAnim",         200, CE_GROUP_INTERACTION, kMEFieldType_ValidatedTextEntry, "AIAnimList", "resourceName");
	}

    // Combat CE_GROUP_COMBAT
	METableAddSimpleColumn(pTable, "Untargetable",       "untargetable",     100, CE_GROUP_COMBAT, kMEFieldType_BooleanCombo);
	METableAddSimpleColumn(pTable, "Unselectable",       "unselectable",     100, CE_GROUP_COMBAT, kMEFieldType_BooleanCombo);
	METableAddSimpleColumn(pTable, "Invulnerable",       "invulnerable",     100, CE_GROUP_COMBAT, kMEFieldType_BooleanCombo);
	METableAddSimpleColumn(pTable, "Unstoppable",        "unstoppable",      100, CE_GROUP_COMBAT, kMEFieldType_BooleanCombo);
	METableAddSimpleColumn(pTable, "Unkillable",         "unkillable",       100, CE_GROUP_COMBAT, kMEFieldType_BooleanCombo);
	METableAddSimpleColumn(pTable, "Level Adjusting",    "levelAdjusting",   100, CE_GROUP_COMBAT, kMEFieldType_BooleanCombo);
	METableAddSimpleColumn(pTable, "Pseudo Player",      "pseudoPlayer",     100, CE_GROUP_COMBAT, kMEFieldType_BooleanCombo);
	METableAddSimpleColumn(pTable, "Disable Face",       "disableTurnToFace",100, CE_GROUP_COMBAT, kMEFieldType_BooleanCombo);
	METableAddSimpleColumn(pTable, "Non Combat",         "nonCombat",        100, CE_GROUP_COMBAT, kMEFieldType_BooleanCombo);
	METableAddSimpleColumn(pTable, "Ignore Combat Mods", "ignoreCombatMods", 160, CE_GROUP_COMBAT, kMEFieldType_BooleanCombo);
	METableAddSimpleColumn(pTable, "Special Large Monster", "SpecialLargeMonster", 120, CE_GROUP_COMBAT, kMEFieldType_BooleanCombo);
	METableAddSimpleColumn(pTable, "Ignore External Innates", "ignoreExternalInnates", 160, CE_GROUP_COMBAT, kMEFieldType_BooleanCombo);
	METableAddSimpleColumn(pTable, "Ignore EntCreate Hue",    "ignoreEntCreateHue",    160, CE_GROUP_COMBAT, kMEFieldType_BooleanCombo);
	METableAddSimpleColumn(pTable, "Drop Inventory On Death",    "dropMyInventory",    160, CE_GROUP_COMBAT, kMEFieldType_BooleanCombo);
	METableAddSimpleColumn(pTable, "No Alpha Interp On Spawn","noInterpAlphaOnSpawn",  160, CE_GROUP_COMBAT, kMEFieldType_BooleanCombo);
	METableAddSimpleColumn(pTable, "Ignore External Anim Bits",    "IgnoreExternalAnimBits",    160, CE_GROUP_COMBAT, kMEFieldType_BooleanCombo);
	METableAddSimpleColumn(pTable, "Use Capsule For Power Arc Checks", "UseCapsuleForPowerArcChecks", 160, CE_GROUP_COMBAT, kMEFieldType_BooleanCombo);
	METableAddSimpleColumn(pTable, "Use Closest Node for Power Anims", "UseClosestPowerAnimNode", 160, CE_GROUP_COMBAT, kMEFieldType_BooleanCombo);
	METableAddSimpleColumn(pTable, "Hue",                "hue",              120, CE_GROUP_COMBAT, kMEFieldType_TextEntry);
	METableAddSimpleColumn(pTable, "Linger Duration",    "lingerDuration",   120, CE_GROUP_COMBAT, kMEFieldType_TextEntry);
	
	
	// Rewards CE_GROUP_REWARDS
	METableAddGlobalDictColumn(pTable, "Override Reward Table", "RewardTable", 160, CE_GROUP_REWARDS, kMEFieldType_ValidatedTextEntry, "RewardTable", "resourceName");
	METableAddGlobalDictColumn(pTable, "Additional Reward Table", "AddRewardTable", 160, CE_GROUP_REWARDS, kMEFieldType_ValidatedTextEntry, "RewardTable", "resourceName");

	// Underling CE_GROUP_UNDERLING
	METableAddGlobalDictColumn(pTable,   "Underlings",    "Underlings", 200, CE_GROUP_UNDERLINGS, kMEFieldType_ValidatedTextEntry, "CritterDef", "resourceName");

	// Riding CE_GROUP_RIDING -BZ commented out for now, riding only vaguely works
/*	METableAddExprColumn(pTable, "Ridable", "Ridable", 100, CE_GROUP_RIDING, NULL);
	METableAddSimpleColumn(pTable, "Ride Anim Bit", "RidingBit",   100, CE_GROUP_RIDING, kMEFieldType_TextEntry);
	METableAddGlobalDictColumn(pTable,   "Ride Power",    "RidingPower", 210, CE_GROUP_RIDING, kMEFieldType_ValidatedTextEntry, "PowerDef", "resourceName");
	METableAddGlobalDictColumn(pTable, "Ride Item", "RidingItem", 210, CE_GROUP_RIDING, kMEFieldType_ValidatedTextEntry, "ItemDef", "resourceName");*/

	// Interaction Object CE_GROUP_WORLDINTERACTION
	METableAddSimpleColumn(pTable, "Mass",             "Mass",             100, CE_GROUP_WORLDINTERACTION, kMEFieldType_TextEntry);
	METableAddEnumColumn(pTable,   "InteractionFlags", "InteractionFlags", 150, CE_GROUP_WORLDINTERACTION, kMEFieldType_FlagCombo, kCritterOverrideFlagEnum);

	METableAddGlobalDictColumn(pTable,"Costume Override", "OverrideCostume", 150, CE_GROUP_IDENTITY, kMEFieldType_ValidatedTextEntry, "PlayerCostume", "resourceName");

	// Keyblock needs to be here but hidden and not parentable in order to ensure it ends up
	// overridden in the inheritance structure.  Keyblocks must be overridden.
	METableAddSimpleColumn(pTable, "KeyBlock", "KeyBlock", 50, NULL, kMEFieldType_TextEntry);
	METableSetColumnState(pTable, "KeyBlock", ME_STATE_HIDDEN | ME_STATE_NOT_PARENTABLE);
}

static char** ce_getVoiceSetNames(METable *pTable, void *pUnused)
{
	char **eaResult = NULL;

	sndGetGroupNamesFromProject("Nemtact", &eaResult);
	
	return eaResult;
}


void ce_ExportOkayClicked(CSVConfig *pCSVConfig)
{
	pCSVConfig->pchDictionary = StructAllocString("CritterDef");
	pCSVConfig->pchScopeColumnName = StructAllocString("Scope");
	pCSVConfig->pchStructName = StructAllocString("CritterDef");
}

AUTO_COMMAND ACMD_NAME("ce_Export");
void ce_Export()
{
	static CSVConfigWindow *pConfigWindow = NULL;

	if(pConfigWindow == NULL)
	{
		pConfigWindow = (CSVConfigWindow*)malloc(sizeof(CSVConfigWindow));
		pConfigWindow->pWindow = ui_WindowCreate("CSV Export Config", 150, 200,450,300);
		pConfigWindow->eDefaultExportColumns = kColumns_Visible;
		pConfigWindow->eDefaultExportType = kCSVExport_Open;
		pConfigWindow->pchBaseFilename = StructAllocString("CrittersExport");
		initCSVConfigWindow(pConfigWindow);
	}

	//Populate the datas
	setupCSVConfigWindow(pConfigWindow, ceWindow, ce_ExportOkayClicked);

	ui_WindowSetModal(pConfigWindow->pWindow, true);
	ui_WindowShow(pConfigWindow->pWindow);
}

static void ce_initCritterCostumes(METable *pTable)
{
	int id;

	// Create the subtable and get the ID
	id = METableCreateSubTable(pTable, "Costume", "Costume", parse_CritterCostume, "key",
		ce_orderCostumes, ce_reorderCostumes, ce_CreateCritterCostume);
	s_CritterCostumeConfigId = id;

	METableAddSimpleSubColumn(pTable, id, "Costume", NULL, 80, NULL, 0);
	METableSetSubColumnState(pTable, id, "Costume", ME_STATE_LABEL);
	METableAddGlobalDictSubColumn(pTable, id, "Costume Name", "Costume", 210, CE_SUBGROUP_NONE, kMEFieldType_ValidatedTextEntry, "PlayerCostume", "resourceName");

	// Lock the label column in
	METableSetNumLockedSubColumns(pTable, id, 3);

	METableAddSimpleSubColumn(pTable, id, "Min Level",  "MinLevel", 100, CE_SUBGROUP_CHANCE, kMEFieldType_TextEntry);
	METableAddSimpleSubColumn(pTable, id, "Max Level",  "MaxLevel", 100, CE_SUBGROUP_CHANCE, kMEFieldType_TextEntry);
	METableAddSimpleSubColumn(pTable, id, "Min Team Size",  "MinTeamSize", 100, CE_SUBGROUP_CHANCE, kMEFieldType_TextEntry);
	METableAddSimpleSubColumn(pTable, id, "Max Team Size",  "MaxTeamSize", 100, CE_SUBGROUP_CHANCE, kMEFieldType_TextEntry);
	METableAddSimpleSubColumn(pTable, id, "Weight",     "Weight", 100, CE_SUBGROUP_CHANCE, kMEFieldType_TextEntry);

	METableAddSimpleSubColumn(pTable, id, "DisplayName",".displayNameMsg.EditorCopy",   160, CE_SUBGROUP_VAR, kMEFieldType_Message);
	METableAddSimpleSubColumn(pTable, id, "FormalName",".displaySubNameMsg.EditorCopy",   160, CE_SUBGROUP_VAR, kMEFieldType_Message);
	METableAddSimpleSubColumn(pTable, id, "CreateIgnoresDisplayName","CreateIgnoresDisplayName",   160, CE_SUBGROUP_VAR, kMEFieldType_BooleanCombo);

	//METableAddSimpleSubColumn(pTable, id, "VoiceSet", "VoiceSet", 160, CE_SUBGROUP_VAR, kMEFieldType_Combo);
	METableAddSubColumn(pTable, id, "VoiceSet", "VoiceSet", NULL,  130, CE_SUBGROUP_COSTUME, kMEFieldType_ValidatedTextEntry, NULL, NULL, NULL, NULL, NULL, NULL, ce_getVoiceSetNames);

	// Order needs to be here but hidden in order to detect when it changes so we know object is dirty
	METableAddSimpleSubColumn(pTable, id, "Order", "order", 50, NULL, kMEFieldType_TextEntry);
	METableSetSubColumnState(pTable, id, "Order", ME_STATE_HIDDEN | ME_STATE_NOT_REVERTABLE);
	
}


static void ce_initCritterPowers(METable *pTable)
{
	int id;

	// Create the subtable and get the ID
	id = METableCreateSubTable(pTable, "Power", "PowerConfigs", parse_CritterPowerConfig, "key",
		ce_orderPowers, ce_reorderPowers, ce_CreateCritterPowerConfig);
	s_CritterPowerConfigId = id;

	METableAddSimpleSubColumn(pTable, id, "Power", NULL, 80, NULL, 0);
	METableSetSubColumnState(pTable, id, "Power", ME_STATE_LABEL);
	METableAddGlobalDictSubColumn(pTable, id,   "Power Name", "Power",     210, CE_SUBGROUP_NONE, kMEFieldType_ValidatedTextEntry, "PowerDef", "resourceName");

	// Lock the power column in
	METableSetNumLockedSubColumns(pTable, id, 3);

	METableAddSimpleSubColumn(pTable, id, "Disabled",   "Disabled", 100, CE_SUBGROUP_NONE, kMEFieldType_BooleanCombo);
	METableAddSimpleSubColumn(pTable, id, "AutoDescDisabled","AutoDescDisabled", 100, CE_SUBGROUP_NONE, kMEFieldType_BooleanCombo);

	METableAddSimpleSubColumn(pTable, id, "Min Level",  "MinLevel", 100, CE_SUBGROUP_CHANCE, kMEFieldType_TextEntry);
	METableAddSimpleSubColumn(pTable, id, "Max Level",  "MaxLevel", 100, CE_SUBGROUP_CHANCE, kMEFieldType_TextEntry);
	METableAddSimpleSubColumn(pTable, id, "Group",      "Group",    100, CE_SUBGROUP_CHANCE, kMEFieldType_TextEntry);
	METableAddSimpleSubColumn(pTable, id, "Weight",     "Weight",   100, CE_SUBGROUP_CHANCE, kMEFieldType_TextEntry);
	METableAddSimpleSubColumn(pTable, id, "Chance",     "Chance",   100, CE_SUBGROUP_CHANCE, kMEFieldType_TextEntry);
	METableAddExprSubColumn(pTable, id,   "Power Requires",          "AddPowerRequiresBlock",120, CE_SUBGROUP_CHANCE, NULL);

	if (gConf.bExposeDeprecatedPowerConfigVars)
	{
		METableAddExprSubColumn(pTable, id,   "AI Requires (Dep)",       "AIRequiresBlock",     120, CE_SUBGROUP_AI, NULL);
		METableAddExprSubColumn(pTable, id,   "AI End Condition (Dep)",  "AIEndConditionBlock", 120, CE_SUBGROUP_AI, NULL);
		METableAddSimpleSubColumn(pTable, id, "AI Min Range (Dep)",      "AIPreferredMinRange", 100, CE_SUBGROUP_AI, kMEFieldType_TextEntry);
		METableAddSimpleSubColumn(pTable, id, "AI Max Range (Dep)",      "AIPreferredMaxRange", 100, CE_SUBGROUP_AI, kMEFieldType_TextEntry);
		METableAddSimpleSubColumn(pTable, id, "AI Weight (Dep)",         "AIWeight",            100, CE_SUBGROUP_AI, kMEFieldType_TextEntry);
		METableAddExprSubColumn(pTable, id,   "AI Weight Modifier (Dep)","AIWeightModifier",    120, CE_SUBGROUP_AI, NULL);
		METableAddSimpleSubColumn(pTable, id, "AI Chain Target (Dep)",   "AIChainTarget",       100, CE_SUBGROUP_AI, kMEFieldType_TextEntry);
		METableAddSimpleSubColumn(pTable, id, "AI Chain Time (Dep)",     "AIChainTime",         100, CE_SUBGROUP_AI, kMEFieldType_TextEntry);
		METableAddExprSubColumn(pTable, id,   "AI Chain Requires (Dep)", "AIChainRequiresBlock",120, CE_SUBGROUP_AI, NULL);
		METableAddExprSubColumn(pTable, id,   "AI TargetOverride (Dep)", "AITargetOverrideBlock",120, CE_SUBGROUP_AI, NULL);
	}

	METableAddGlobalDictSubColumn(	pTable, id,	"AI PowerConfigDef",	"AIPowerConfigDef",						100, CE_SUBGROUP_AI, kMEFieldType_Combo, "AIPowerConfigDef", "resourceName");
	METableAddSimpleSubColumn(		pTable, id,	"AI Weight",			".AIPowerConfigDefInst.absWeight",		100, CE_SUBGROUP_AI, kMEFieldType_ValidatedTextEntry);
	METableAddExprSubColumn(		pTable, id,	"AI Weight Modifier",	".AIPowerConfigDefInst.weightModifier",	120, CE_SUBGROUP_AI, NULL);
	METableAddSimpleSubColumn(		pTable, id,	"AI Min Range",			".AIPowerConfigDefInst.minDist",		100, CE_SUBGROUP_AI, kMEFieldType_ValidatedTextEntry);
	METableAddSimpleSubColumn(		pTable, id,	"AI Max Range",			".AIPowerConfigDefInst.maxDist",		100, CE_SUBGROUP_AI, kMEFieldType_ValidatedTextEntry);
	METableAddSimpleSubColumn(		pTable, id,	"AI Chain Target",		".AIPowerConfigDefInst.chainTarget",	100, CE_SUBGROUP_AI, kMEFieldType_ValidatedTextEntry);
	METableAddSimpleSubColumn(		pTable, id,	"AI Chain Time",		".AIPowerConfigDefInst.chainTime",		100, CE_SUBGROUP_AI, kMEFieldType_ValidatedTextEntry);
	METableAddExprSubColumn(		pTable, id,	"AI Chain Requires",	".AIPowerConfigDefInst.chainRequires",	120, CE_SUBGROUP_AI, NULL);
	METableAddSimpleSubColumn(		pTable, id,	"AI Chain Locks Facing",".AIPowerConfigDefInst.ChainLocksFacing",	120, CE_SUBGROUP_AI, kMEFieldType_BooleanCombo);
	METableAddSimpleSubColumn(		pTable, id,	"AI Chain Locks Movement",".AIPowerConfigDefInst.ChainLocksMovement",	120, CE_SUBGROUP_AI, kMEFieldType_BooleanCombo);
	METableAddExprSubColumn(		pTable, id,	"AI Requires",			".AIPowerConfigDefInst.airequires",		120, CE_SUBGROUP_AI, NULL);
	METableAddExprSubColumn(		pTable, id,	"AI Target Override",	".AIPowerConfigDefInst.targetOverride",	120, CE_SUBGROUP_AI, NULL);
	METableAddSimpleSubColumn(		pTable, id, "AI Max Delay Time",	".AIPowerConfigDefInst.maxRandomQueueTime", 50, CE_SUBGROUP_AI, kMEFieldType_TextEntry);
#if 0 // Jira STO-1490, canceled?
	METableAddExprSubColumn(		pTable, id,	"AI EndCondition",		".AIPowerConfigDefInst.aiEndCondition",	120, CE_SUBGROUP_AI, NULL);
#endif
	
	// Order needs to be here but hidden in order to detect when it changes so we know object is dirty
	METableAddSimpleSubColumn(pTable, id, "Order", "order", 50, CE_SUBGROUP_NONE, kMEFieldType_TextEntry);
	METableSetSubColumnState(pTable, id, "Order", ME_STATE_HIDDEN | ME_STATE_NOT_REVERTABLE);
}


static void ce_initCritterVars(METable *pTable)
{
	int id;

	// Create the subtable and get the ID
	id = METableCreateSubTable(pTable, "Vars", "CritterVars", parse_CritterVar, "key",
		ce_orderVars, ce_reorderVars, ce_CreateCritterVar);
	s_CritterVarConfigId = id;

	METableAddSimpleSubColumn(pTable, id, "Var", NULL, 80, NULL, 0);
	METableSetSubColumnState(pTable, id, "Var", ME_STATE_LABEL);
	METableAddSimpleSubColumn(pTable, id, "Var Name", ".Var.Name", 160, CE_SUBGROUP_NONE, kMEFieldType_TextEntry);

	// Lock the label column in
	METableSetNumLockedSubColumns(pTable, id, 3);

	METableAddEnumSubColumn(pTable, id, "Type", ".Var.Type", 100, CE_SUBGROUP_VAR, kMEFieldType_Combo, WorldVariableTypeEnum);
	METableAddSimpleSubColumn(pTable, id, "Int Val", ".Var.IntVal", 100, CE_SUBGROUP_VAR, kMEFieldType_TextEntry);
	METableAddSimpleSubColumn(pTable, id, "Float Val", ".Var.FloatVal", 100, CE_SUBGROUP_VAR, kMEFieldType_TextEntry);
	METableAddSimpleSubColumn(pTable, id, "String", ".Var.StringVal", 100, CE_SUBGROUP_VAR, kMEFieldType_TextEntry);
	METableAddSimpleSubColumn(pTable, id, "Message",".Var.MessageVal.EditorCopy",     160, CE_SUBGROUP_VAR, kMEFieldType_Message);
	METableAddGlobalDictSubColumn(pTable, id,   "Animation", ".Var.StringVal",        210, CE_SUBGROUP_VAR, kMEFieldType_ValidatedTextEntry, "AIAnimList", "resourceName");
	METableAddGlobalDictSubColumn(pTable, id,   "Critter Def", ".Var.CritterDef",     210, CE_SUBGROUP_VAR, kMEFieldType_ValidatedTextEntry, "CritterDef", "resourceName");
	METableAddGlobalDictSubColumn(pTable, id,   "Critter Group", ".Var.CritterGroup", 210, CE_SUBGROUP_VAR, kMEFieldType_ValidatedTextEntry, "CritterGroup", "resourceName");

	// Order needs to be here but hidden in order to detect when it changes so we know object is dirty
	METableAddSimpleSubColumn(pTable, id, "Order", "order", 50, NULL, kMEFieldType_TextEntry);
	METableSetSubColumnState(pTable, id, "Order", ME_STATE_HIDDEN | ME_STATE_NOT_REVERTABLE);

}


static void ce_initCritterLore(METable *pTable)
{
	int id;

	// Create the subtable and get the ID
	id = METableCreateSubTable(pTable, "Lore", "CritterLoreEntries", parse_CritterLore, "key",
		ce_orderLore, ce_reorderLore, ce_CreateCritterLore);
	s_CritterLoreConfigId = id;

	METableAddSimpleSubColumn(pTable, id, "Lore", NULL, 80, NULL, 0);
	METableSetSubColumnState(pTable, id, "Lore", ME_STATE_LABEL);

	// Lock the label column in
	METableSetNumLockedSubColumns(pTable, id, 3);

	METableAddEnumSubColumn(pTable, id, "Attrib", "Attrib", 100, CE_SUBGROUP_VAR, kMEFieldType_Combo, AttribTypeEnum);
	METableAddSimpleSubColumn(pTable, id, "DC", "DC", 100, CE_SUBGROUP_VAR, kMEFieldType_TextEntry);
	METableAddGlobalDictSubColumn(pTable, id,   "Item", "Item",        210, CE_SUBGROUP_VAR, kMEFieldType_ValidatedTextEntry, "ItemDef", "resourceName");
	METableAddGlobalDictSubColumn(pTable, id,   "Power", "Power",        210, CE_SUBGROUP_VAR, kMEFieldType_ValidatedTextEntry, "PowerDef", "resourceName");
	
	// Order needs to be here but hidden in order to detect when it changes so we know object is dirty
	METableAddSimpleSubColumn(pTable, id, "Order", "order", 50, NULL, kMEFieldType_TextEntry);
	METableSetSubColumnState(pTable, id, "Order", ME_STATE_HIDDEN | ME_STATE_NOT_REVERTABLE);

}

static void ce_initCritterItems(METable *pTable)
{
	int id;

	id = METableCreateSubTable(pTable, "Items", "CritterItems", parse_DefaultItemDef, "key", 
		ce_orderItems, ce_reorderItems, ce_CreateDefaultItemDef);

	s_CritterItemConfigId = id;

	METableAddSimpleSubColumn(pTable, id, "Item", NULL, 80, NULL, 0);
	METableSetSubColumnState(pTable, id, "Item", ME_STATE_LABEL);

	// Lock the label column in
	METableSetNumLockedSubColumns(pTable, id, 2);

	METableAddGlobalDictSubColumn(pTable, id, "Item", "Item", 100, CE_SUBGROUP_ITEM, kMEFieldType_ValidatedTextEntry, "ItemDef","resourceName");

	METableAddSimpleSubColumn(pTable, id, "Disabled",   "Disabled", 100, CE_SUBGROUP_NONE, kMEFieldType_BooleanCombo);

	METableAddEnumSubColumn(pTable, id, "Bag ID", "BagID", 100, CE_SUBGROUP_ITEM, kMEFieldType_Combo, InvBagIDsEnum);
	METableAddSimpleSubColumn(pTable, id, "Slot",  "Slot", 100, CE_SUBGROUP_ITEM, kMEFieldType_TextEntry);
	METableAddSimpleSubColumn(pTable, id, "Min Level",  "MinLevel", 100, CE_SUBGROUP_CHANCE, kMEFieldType_TextEntry);
	METableAddSimpleSubColumn(pTable, id, "Max Level",  "MaxLevel", 100, CE_SUBGROUP_CHANCE, kMEFieldType_TextEntry);
	METableAddSimpleSubColumn(pTable, id, "Group",      "Group",    100, CE_SUBGROUP_CHANCE, kMEFieldType_TextEntry);
	METableAddSimpleSubColumn(pTable, id, "Weight",     "Weight",   100, CE_SUBGROUP_CHANCE, kMEFieldType_TextEntry);
	METableAddSimpleSubColumn(pTable, id, "Chance",     "Chance",   100, CE_SUBGROUP_CHANCE, kMEFieldType_TextEntry);
	METableAddSimpleSubColumn(pTable, id, "AI Weight Multiplier",".ModifierInfo.WeightMulti",100,CE_SUBGROUP_ITEM,kMEFieldType_TextEntry);
	METableAddSimpleSubColumn(pTable, id, "Min Dist Multiplier",".ModifierInfo.MinDistMulti",100,CE_SUBGROUP_ITEM,kMEFieldType_TextEntry);
	METableAddSimpleSubColumn(pTable, id, "Max Dist Multiplier",".ModifierInfo.MaxDistMulti",100,CE_SUBGROUP_ITEM,kMEFieldType_TextEntry);

}

void ce_SpawnCritter(METable *pTable, CritterDef *pCritterDef, void *pUnused)
{
	globCmdParsef("SpawnCritter %s", pCritterDef->pchName);
}

static void ce_init(MultiEditEMDoc *pEditorDoc)
{
	if (!ceWindow) {
		char buf[256];
		// Create the editor window
		ceWindow = MEWindowCreate("Critter Editor", "Critter", "Critters", SEARCH_TYPE_CRITTER, g_hCritterDefDict, parse_CritterDef, "Name", "FileName", "scope", pEditorDoc);

		sprintf(buf, "Export %s", ceWindow->pcDisplayNamePlural);
		emMenuItemCreate(ceWindow->pEditorDoc->emDoc.editor, "ce_export", buf, NULL, NULL, "ce_Export");
		emMenuRegister(ceWindow->pEditorDoc->emDoc.editor, emMenuCreate(ceWindow->pEditorDoc->emDoc.editor, "Tools", "ce_export", NULL));

		METableAddCustomAction(ceWindow->pTable, "Spawn Critter", ce_SpawnCritter, NULL);

		// Add power-specific columns
		ce_initCritterColumns(ceWindow->pTable);

		// Add power-specific sub-columns
		ce_initCritterCostumes(ceWindow->pTable);
		ce_initCritterPowers(ceWindow->pTable);
		ce_initCritterVars(ceWindow->pTable);
		ce_initCritterItems(ceWindow->pTable);
		ce_initCritterLore(ceWindow->pTable);
		METableFinishColumns(ceWindow->pTable);

		// Init the menus after adding all the columns
		MEWindowInitTableMenus(ceWindow);

		// Set the callbacks
		ce_initCallbacks(ceWindow, ceWindow->pTable);

		aiRequestEditingData();
	}

	// Show the window
	ui_WindowPresent(ceWindow->pUIWindow);
}


//---------------------------------------------------------------------------------------------------
// Public Interface
//---------------------------------------------------------------------------------------------------

MEWindow *critterEditor_init(MultiEditEMDoc *pEditorDoc) 
{
	//resRequestAllResourcesInDictionary(g_hDragonMovementDefDict);
	
	ce_init(pEditorDoc);
	
	return ceWindow;
}


void critterEditor_createCritter(char *pcName)
{
	// Create a new object
	// Add the object as a new object with no old	
	void *pObject = ce_createObject(ceWindow->pTable, NULL, pcName, false);
	METableAddRowByObject(ceWindow->pTable, pObject, 1, 1);
}

#endif