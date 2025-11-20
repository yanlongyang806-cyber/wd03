//
// PowersEditor.c
//

#ifndef NO_EDITORS

#include "cmdparse.h"
#include "dynAnimChart.h"
#include "dynfxinfo.h"
#include "StringCache.h"
#include "TextParserInheritance.h"

#include "aiStructCommon.h"
#include "AttribModFragility.h"
#include "CharacterAttribs.h"
#include "CombatConfig.h"
#include "CombatEval.h"
#include "CombatSensitivity.h"
#include "Conversions.h"
#include "CSVExport.h"
#include "Entity.h"
#include "entcritter.h"
#include "MultiEditTable.h"
#include "MultiEditWindow.h"
#include "ObjPath.h"
#include "Powers.h"
#include "PowersEnums_h_ast.h"
#include "PowersEditor.h"
#include "PowerModes.h"
#include "PowerVars.h"
#include "RegionRules.h"
#include "ResourceSearch.h"
#include "sysutil.h"
#include "ItemPowerEditor.h"


#include "AutoGen/Powers_h_ast.h"
#include "AutoGen/AttribMod_h_ast.h"
#include "AttribModFragility_h_ast.h"
#include "AutoGen/CombatPool_h_ast.h"
#include "AutoGen/GameServerLib_autogen_ServerCmdWrappers.h"

#define PE_GROUP_CHARGE   "Charge"
#define PE_GROUP_COST     "Cost"
#define PE_GROUP_EFFECT   "Effect"
#define PE_GROUP_MAIN     "Main"
#define PE_GROUP_ACTIVATION     "Activation"
#define PE_GROUP_AI	      "AI"
#define PE_GROUP_TARGET   "Target"
#define PE_GROUP_TIMING   "Timing"
#define PE_GROUP_USELIMITS "UseLimits"
#define PE_GROUP_UI       "UI"

#define PE_SUBGROUP_COMBO        "Combo"
#define PE_SUBGROUP_FRAGILITY    "Fragility"
#define PE_SUBGROUP_MAIN         "Main"
#define PE_SUBGROUP_STACKING     "Stacking"
#define PE_SUBGROUP_REQUIRES     "Requires"
#define PE_SUBGROUP_SENSITIVITY  "Sensitivity"
#define PE_SUBGROUP_STRENGTH     "Strength"
#define PE_SUBGROUP_TIMING       "Timing"
#define PE_SUBGROUP_EVENTS	     "Events"
#define PE_SUBGROUP_ART          "Art"
#define PE_SUBGROUP_UI           "UI"
#define PE_SUBGROUP_NONE         NULL


// This magic line is required if you run in Full Debug to avoid assert
AUTO_RUN_ANON(memBudgetAddMapping(__FILE__, BUDGET_Editors););

typedef struct AttribTypeInfo {
	AttribType eType;
	char *pcColGroup;
	ParseTable *pParseTable;
} AttribTypeInfo;


//-----------------------------------------------------------------------------------
// Global Data
//-----------------------------------------------------------------------------------

static MEWindow *peWindow = NULL;

static int peAttribModId = 0;
static int peCombosId = 0;
static char **peTempEArray;
static AttribTypeInfo **eaTypeInfos = NULL;
static char **geaItempowerScopes = NULL;

extern StaticDefineInt CombatTrackerFlagEditorEnum[];

//---------------------------------------------------------------------------------------------------
// Callbacks
//---------------------------------------------------------------------------------------------------

static int pe_compareStrings(const char** left, const char** right)
{
	return stricmp(*left,*right);
}


static int pe_compareMods(const AttribModDef **def1, const AttribModDef **def2)
{
	if ((*def1)->fApplyPriority > (*def2)->fApplyPriority) {
		return 1;
	} else if ((*def1)->fApplyPriority < (*def2)->fApplyPriority) {
		return -1;
	}
	return 0;
}


static int pe_compareCombos(const PowerCombo **combo1, const PowerCombo **combo2)
{
	if ((*combo1)->fOrder > (*combo2)->fOrder) {
		return 1;
	} else if ((*combo1)->fOrder < (*combo2)->fOrder) {
		return -1;
	}
	return 0;
}


static bool pe_doesInheritMod(PowerDef *pPowerDef, AttribModDef *pModDef)
{
	char buf[128];
	int iType;

	// Not inherited if no parent power
	if (!pPowerDef->pInheritance) {
		return false;
	}

	// Look if this mod is inherited
	sprintf(buf,".AttribMod[\"%d\"]",pModDef->iKey);
	iType = StructInherit_GetOverrideType(parse_PowerDef, pPowerDef, buf);
	if (iType == OVERRIDE_ADD) {
		// Only added fields are local.  All others are from the parent.
		return false;
	}
	return true;
}


static bool pe_doesInheritCombo(PowerDef *pPowerDef, PowerCombo *pCombo)
{
	char buf[128];
	int iType;

	// Not inherited if no parent power
	if (!pPowerDef->pInheritance) {
		return false;
	}

	// Look if this mod is inherited
	sprintf(buf,".Combo[\"%d\"]",pCombo->iKey);
	iType = StructInherit_GetOverrideType(parse_PowerDef, pPowerDef, buf);
	if (iType == OVERRIDE_ADD) {
		// Only added fields are local.  All others are from the parent.
		return false;
	}
	return true;
}


static void pe_orderAttribModDefs(METable *pTable, void ***peaOrigMods, void ***peaOrderedMods)
{
	int i, count;

	// Copy data to the ordered list
	count = eaSize(peaOrigMods);
	for(i=0; i<count; ++i) {
		eaPush(peaOrderedMods,(*peaOrigMods)[i]);
	}

	// QSort the list
	eaQSort(*peaOrderedMods,pe_compareMods);
}


// Returns 1 if reordering succeeds and 0 if not
static int pe_reorderAttribModDefs(METable *pTable, PowerDef *pPowerDef, void ***peaMods, int pos1, int pos2)
{
	AttribModDef **eaMods = (AttribModDef**)*peaMods;
	AttribModDef *pTempMod;
	float fTempPriority;
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
		assertmsg(0,"Power editor cannot reorder subrows by more than one row at a time");
	}
	
	// Get inheritance info
	bInheritPos1 = pe_doesInheritMod(pPowerDef,eaMods[pos1]);
	bInheritPos2 = pe_doesInheritMod(pPowerDef,eaMods[pos2]);

	if (bInheritPos1 && bInheritPos2) {
		// Cannot reorder fields if both are inherited
		ui_DialogPopup("Notice", "Cannot re-order two inherited attrib mods!");
		return 0;

	} else if (!bInheritPos1 && !bInheritPos2) {
		// Not inheriting, so simply swap priority
		fTempPriority = eaMods[pos1]->fApplyPriority;
		eaMods[pos1]->fApplyPriority = eaMods[pos2]->fApplyPriority;
		eaMods[pos2]->fApplyPriority = fTempPriority;

	} else if (bInheritPos1) {
		// Cannot change priority of pos1, so change pos2 to be halfway between
		// the pos1 value and the prior row (0.0 if no prior row)
		float fLow = 0.0;

		if (pos1 > 0) {
			fLow = eaMods[pos1-1]->fApplyPriority;
		}
		eaMods[pos2]->fApplyPriority = (fLow + eaMods[pos1]->fApplyPriority) / 2;
		
	} else { // (bInheritPos2)
		// Cannot change priority of pos2, so change pos1 to be halfway between
		// the pos2 value and the following row (1.0 more than pos 2 if no following row)
		if (pos2 < eaSize(&eaMods) - 1) {
			eaMods[pos1]->fApplyPriority = (eaMods[pos2]->fApplyPriority + eaMods[pos2+1]->fApplyPriority) / 2;
		} else {
			eaMods[pos1]->fApplyPriority = eaMods[pos2]->fApplyPriority + 1.0;
		}
	}

	// Swap position
	pTempMod = eaMods[pos1];
	eaMods[pos1] = eaMods[pos2];
	eaMods[pos2] = pTempMod;

	return 1;
}


static void *pe_createAttribMod(METable *pTable, PowerDef *pPowerDef, AttribModDef *pModToClone, AttribModDef *pBeforeMod, AttribModDef *pAfterMod)
{
	AttribModDef *pNewMod;
	F32 fLow = 0.0;
	PowerTarget *pTargetAffected;

	// Allocate the object
	if (pModToClone) {
		pNewMod = (AttribModDef*)StructClone(parse_AttribModDef, pModToClone);
	} else {
		pNewMod = (AttribModDef*)StructCreate(parse_AttribModDef);
	}
	if (!pNewMod) {
		return NULL;
	}

	// Make sure it has the right parent
	pNewMod->pPowerDef = pPowerDef;

	// Update the priority
	if (pBeforeMod) {
		fLow = pBeforeMod->fApplyPriority;
	}
	if (pAfterMod) {
		pNewMod->fApplyPriority = (fLow + pAfterMod->fApplyPriority) / 2;
	} else {
		pNewMod->fApplyPriority = fLow + 1.0;
	}

	// Update the key
	pNewMod->iKey = powerdef_GetNextAttribKey(pPowerDef, 0);

	// Generate best guess defaults
	if (!pModToClone) {
		pNewMod->eType = kModType_None;
		pNewMod->offAttrib = 0;
		pNewMod->offAspect = 0;
		pNewMod->fVariance = 0.1;
		pTargetAffected = GET_REF(pPowerDef->hTargetAffected);
		if (pPowerDef->eType == kPowerType_Enhancement) {
			// If the power is an enhancement, this is probably an enhance def attrib
		} else if ((pTargetAffected && pTargetAffected->bRequireSelf) ||
					pPowerDef->eType == kPowerType_Passive ||
					pPowerDef->eType == kPowerType_Innate) {
			// Reasonable defaults for passive and innate
			pNewMod->eTarget = kModTarget_Self;
		} else {
			pNewMod->eTarget = kModTarget_Target;
		}
	}

	return pNewMod;
}


static void pe_orderCombos(METable *pTable, void ***peaOrigCombos, void ***peaOrderedCombos)
{
	int i, count;

	// Copy data to the ordered list
	count = eaSize(peaOrigCombos);
	for(i=0; i<count; ++i) {
		eaPush(peaOrderedCombos,(*peaOrigCombos)[i]);
	}

	// QSort the list
	eaQSort(*peaOrderedCombos,pe_compareCombos);
}


// Returns 1 if reordering succeeds and 0 if not
static int pe_reorderCombos(METable *pTable, PowerDef *pPowerDef, void ***peaCombos, int pos1, int pos2)
{
	PowerCombo **eaCombos = (PowerCombo**)*peaCombos;
	PowerCombo *pTempCombo;
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
		assertmsg(0,"Power editor cannot reorder subrows by more than one row at a time");
	}
	
	// Get inheritance info
	bInheritPos1 = pe_doesInheritCombo(pPowerDef,eaCombos[pos1]);
	bInheritPos2 = pe_doesInheritCombo(pPowerDef,eaCombos[pos2]);

	if (bInheritPos1 && bInheritPos2) {
		// Cannot reorder fields if both are inherited
		ui_DialogPopup("Notice", "Cannot re-order two inherited combos!");
		return 0;

	} else if (!bInheritPos1 && !bInheritPos2) {
		// Not inheriting, so simply swap priority
		fTempOrder = eaCombos[pos1]->fOrder;
		eaCombos[pos1]->fOrder = eaCombos[pos2]->fOrder;
		eaCombos[pos2]->fOrder = fTempOrder;

	} else if (bInheritPos1) {
		// Cannot change priority of pos1, so change pos2 to be halfway between
		// the pos1 value and the prior row (0.0 if no prior row)
		float fLow = 0.0;

		if (pos1 > 0) {
			fLow = eaCombos[pos1-1]->fOrder;
		}
		eaCombos[pos2]->fOrder = (fLow + eaCombos[pos1]->fOrder) / 2;
		
	} else { // (bInheritPos2)
		// Cannot change priority of pos2, so change pos1 to be halfway between
		// the pos2 value and the following row (1.0 more than pos 2 if no following row)
		if (pos2 < eaSize(&eaCombos) - 1) {
			eaCombos[pos1]->fOrder = (eaCombos[pos2]->fOrder + eaCombos[pos2+1]->fOrder) / 2;
		} else {
			eaCombos[pos1]->fOrder = eaCombos[pos2]->fOrder + 1.0;
		}
	}

	// Swap position
	pTempCombo = eaCombos[pos1];
	eaCombos[pos1] = eaCombos[pos2];
	eaCombos[pos2] = pTempCombo;

	return 1;
}


static void *pe_createCombo(METable *pTable, PowerDef *pPowerDef, PowerCombo *pComboToClone, PowerCombo *pBeforeCombo, PowerCombo *pAfterCombo)
{
	PowerCombo *pNewCombo;
	F32 fLow = 0.0;

	// Allocate the object
	if (pComboToClone) {
		pNewCombo = (PowerCombo*)StructClone(parse_PowerCombo, pComboToClone);
	} else {
		pNewCombo = (PowerCombo*)StructCreate(parse_PowerCombo);
	}
	if (!pNewCombo) {
		return NULL;
	}

	// Set up the order
	if (pBeforeCombo) {
		fLow = pBeforeCombo->fOrder;
	}
	if (pAfterCombo) {
		pNewCombo->fOrder = (fLow + pAfterCombo->fOrder) / 2;
	} else {
		pNewCombo->fOrder = fLow + 1.0;
	}

	// Update the key
	pNewCombo->iKey = powerdef_GetNextComboKey(pPowerDef, 0);

	return pNewCombo;
}

static void PEFixMessage(Message **ppmsg, const char *pchPowerName, const char *pchKey, const char *pchDesc, const char *pchScope)
{
	char buf[1024];
	Message *pmsg = NULL;

	if(!*ppmsg) *ppmsg = StructCreate(parse_Message);
	pmsg = *ppmsg;

	sprintf(buf, "PowerDef.%s.%s", pchKey, pchPowerName);
	//langFixupMessageWithTerseKey(pmsg, MKP_POWER, buf, pchDesc, pchScope);

	pmsg->pcMessageKey = allocAddString(buf);

	StructFreeString(pmsg->pcDescription);
	pmsg->pcDescription = StructAllocString(pchDesc);

	pmsg->pcScope = allocAddString(pchScope);

	// Leave pcFilename alone
}

static void PEFixModMessage(Message **ppmsg, int iModKey, const char *pchPowerName, const char *pchKey, const char *pchDesc, const char *pchScope)
{
	char buf[1024];
	Message *pmsg = NULL;

	if(!*ppmsg) *ppmsg = StructCreate(parse_Message);
	pmsg = *ppmsg;

	sprintf(buf, "PowerDef.%s.%s.%d", pchKey, pchPowerName, iModKey);
	pmsg->pcMessageKey = allocAddString(buf);

	StructFreeString(pmsg->pcDescription);
	pmsg->pcDescription = StructAllocString(pchDesc);

	pmsg->pcScope = allocAddString(pchScope);

	// Leave pcFilename alone
}

static void pe_fixDisplayMessages(PowerDef *pdef)
{
	PowerDef *pParentDef = NULL;
	int bInherited = !!StructInherit_GetParentName(parse_PowerDef, pdef);
	int i;
	char *buf = NULL;
	char *pchScope = NULL;
	char *pchModAutoDescMessage = NULL;
	char *pchModAutoDescEditorCopy = NULL;

	if (bInherited) {
		pParentDef = RefSystem_ReferentFromString(g_hPowerDefDict, StructInherit_GetParentName(parse_PowerDef, pdef));
	}

	estrStackCreate(&buf);
	estrStackCreate(&pchScope);
	estrStackCreate(&pchModAutoDescMessage);
	estrStackCreate(&pchModAutoDescEditorCopy);

	estrPrintf(&pchScope,"PowerDef");
	if(pdef->pchGroup)
	{
		char *p = NULL;
		estrConcatf(&pchScope,"/%s",pdef->pchGroup);
		while((p = strchr(pchScope,'.')) != NULL)
		{
			*p = '/';
		}
	}

	if(!(bInherited && pdef->msgDisplayName.pEditorCopy &&
		StructInherit_GetOverrideType(parse_PowerDef, pdef, ".msgDisplayName.Message") == OVERRIDE_NONE &&
		StructInherit_GetOverrideType(parse_PowerDef, pdef, ".msgDisplayName.EditorCopy") == OVERRIDE_NONE))
	{
		if (!pdef->msgDisplayName.pEditorCopy) 
			pdef->msgDisplayName.pEditorCopy = StructCreate(parse_Message);
		estrPrintf(&buf, "PowerDef.%s.%s", "Name", pdef->pchName);
		langFixupMessageWithTerseKey(pdef->msgDisplayName.pEditorCopy, MKP_POWERNAME, buf, "Power name", pchScope);
	}

	if(!(bInherited && pdef->msgDescription.pEditorCopy &&
		StructInherit_GetOverrideType(parse_PowerDef, pdef, ".msgDescription.Message") == OVERRIDE_NONE &&
		StructInherit_GetOverrideType(parse_PowerDef, pdef, ".msgDescription.EditorCopy") == OVERRIDE_NONE))
	{
		if (!pdef->msgDisplayName.pEditorCopy) 
			pdef->msgDisplayName.pEditorCopy = StructCreate(parse_Message);
		estrPrintf(&buf, "PowerDef.%s.%s", "Desc", pdef->pchName);
		langFixupMessageWithTerseKey(pdef->msgDescription.pEditorCopy, MKP_POWERDESC, buf, "Power short description", pchScope);
	}

	if(!(bInherited && pdef->msgDescriptionLong.pEditorCopy &&
		StructInherit_GetOverrideType(parse_PowerDef, pdef, ".msgDescriptionLong.Message") == OVERRIDE_NONE &&
		StructInherit_GetOverrideType(parse_PowerDef, pdef, ".msgDescriptionLong.EditorCopy") == OVERRIDE_NONE))
	{
		if (!pdef->msgDisplayName.pEditorCopy) 
			pdef->msgDisplayName.pEditorCopy = StructCreate(parse_Message);
		estrPrintf(&buf, "PowerDef.%s.%s", "DescLong", pdef->pchName);
		langFixupMessageWithTerseKey(pdef->msgDescriptionLong.pEditorCopy, MKP_POWERDESCLONG, buf, "Power long description", pchScope);
	}

	if(!(bInherited && pdef->msgDescriptionFlavor.pEditorCopy &&
		StructInherit_GetOverrideType(parse_PowerDef, pdef, ".msgDescriptionFlavor.Message") == OVERRIDE_NONE &&
		StructInherit_GetOverrideType(parse_PowerDef, pdef, ".msgDescriptionFlavor.EditorCopy") == OVERRIDE_NONE))
	{
		if (!pdef->msgDisplayName.pEditorCopy) 
			pdef->msgDisplayName.pEditorCopy = StructCreate(parse_Message);
		estrPrintf(&buf, "PowerDef.%s.%s", "DescFlavor", pdef->pchName);
		langFixupMessageWithTerseKey(pdef->msgDescriptionFlavor.pEditorCopy, MKP_POWERFLAV, buf, "Power flavor description", pchScope);
	}

	if(!(bInherited && pdef->msgAttribOverride.pEditorCopy &&
		StructInherit_GetOverrideType(parse_PowerDef, pdef, ".msgAttribOverride.Message") == OVERRIDE_NONE &&
		StructInherit_GetOverrideType(parse_PowerDef, pdef, ".msgAttribOverride.EditorCopy") == OVERRIDE_NONE))
	{
		PEFixMessage(&pdef->msgAttribOverride.pEditorCopy,pdef->pchName,"AttribOverride","Attribute Autodesc Override description",pchScope);
	}

	if(!(bInherited && pdef->msgAutoDesc.pEditorCopy &&
		StructInherit_GetOverrideType(parse_PowerDef, pdef, ".msgAutoDesc.Message") == OVERRIDE_NONE &&
		StructInherit_GetOverrideType(parse_PowerDef, pdef, ".msgAutoDesc.EditorCopy") == OVERRIDE_NONE))
	{
		PEFixMessage(&pdef->msgAutoDesc.pEditorCopy,pdef->pchName,"AutoDesc","Custom AutoDesc description",pchScope);
	}

	if(!(bInherited && pdef->msgRankChange.pEditorCopy &&
		StructInherit_GetOverrideType(parse_PowerDef, pdef, ".msgRankChange.Message") == OVERRIDE_NONE &&
		StructInherit_GetOverrideType(parse_PowerDef, pdef, ".msgRankChange.EditorCopy") == OVERRIDE_NONE))
	{
		PEFixMessage(&pdef->msgRankChange.pEditorCopy,pdef->pchName,"RankChange","Rank Change description",pchScope);
	}

	for(i=eaSize(&pdef->ppMods)-1; i>=0; i--)
	{
		AttribModDef *pmoddef = pdef->ppMods[i];
		int bInheritedMod = (bInherited && pParentDef && eaIndexedGetUsingInt(&pParentDef->ppMods,pmoddef->iKey));
		estrPrintf(&pchModAutoDescMessage,".AttribMod[\"%d\"].msgAutoDesc.Message",pmoddef->iKey);
		estrPrintf(&pchModAutoDescEditorCopy,".AttribMod[\"%d\"].msgAutoDesc.EditorCopy",pmoddef->iKey);
		if(!(bInheritedMod && pmoddef->msgAutoDesc.pEditorCopy &&
			StructInherit_GetOverrideType(parse_PowerDef, pdef, pchModAutoDescMessage) == OVERRIDE_NONE &&
			StructInherit_GetOverrideType(parse_PowerDef, pdef, pchModAutoDescEditorCopy) == OVERRIDE_NONE))
		{
			PEFixModMessage(&pmoddef->msgAutoDesc.pEditorCopy,pmoddef->iKey,pdef->pchName,"ModAutoDesc","Custom AttribMod description",pchScope);
		}
	}

	estrDestroy(&buf);
	estrDestroy(&pchScope);
	estrDestroy(&pchModAutoDescMessage);
	estrDestroy(&pchModAutoDescEditorCopy);
}

static void pe_postOpenCallback(METable *pTable, PowerDef *pDef, PowerDef *pOrigDef)
{
	pe_fixDisplayMessages(pDef);
	if (pOrigDef) {
		pe_fixDisplayMessages(pOrigDef);
	}
}

static void pe_preSaveCallback(METable *pTable, PowerDef *pDef)
{
	eaDestroyStruct(&pDef->ppOrderedModsClient, parse_AttribModDef);
	powerdef_SortArrays(pDef);
	pe_fixDisplayMessages(pDef);
}

static void PEFixMessageInheritance(PowerDef *pdefChild, Message *pmsgParent, Message *pmsgChild, char *path)
{
	enumOverrideType eTypeCurrent = StructInherit_GetOverrideType(parse_PowerDef,pdefChild,path);
	enumOverrideType eTypeCorrect = OVERRIDE_NONE;

	if(!pmsgParent || !pmsgParent->pcDefaultString || !pmsgParent->pcDefaultString[0])
	{
		if(!pmsgChild || !pmsgChild->pcDefaultString || !pmsgChild->pcDefaultString[0])
		{
			eTypeCorrect = OVERRIDE_NONE;
		}
		else
		{
			eTypeCorrect = OVERRIDE_SET;
		}
	}
	else
	{
		if(!pmsgChild || !pmsgChild->pcDefaultString || !pmsgChild->pcDefaultString[0])
		{
			eTypeCorrect = OVERRIDE_REMOVE;
		}
		else
		{
			if(!strcmp(pmsgParent->pcDefaultString,pmsgChild->pcDefaultString))
			{
				eTypeCorrect = OVERRIDE_NONE;
			}
			else
			{
				eTypeCorrect = OVERRIDE_SET;
			}
		}
	}

	if(eTypeCurrent!=eTypeCorrect)
	{
		if(eTypeCurrent!=OVERRIDE_NONE)
		{
			StructInherit_DestroyOverride(parse_PowerDef,pdefChild,path);
		}

		if(eTypeCorrect==OVERRIDE_SET)
		{
			StructInherit_CreateFieldOverride(parse_PowerDef,pdefChild,path);
		}
		else if(eTypeCorrect==OVERRIDE_REMOVE)
		{
			StructInherit_CreateRemoveStructOverride(parse_PowerDef,pdefChild,path);
		}
	}
}

static int pe_validateCallback(METable *pTable, PowerDef *pPowerDef, void *pUserData)
{
	int i;
	PowerDef *pParentDef = NULL;
	const char *pcParentName;

	// Quick check for inheritance data, may be used later
	pcParentName = StructInherit_GetParentName(parse_PowerDef, pPowerDef);
	if (pcParentName) {
		pParentDef = RefSystem_ReferentFromString(g_hPowerDefDict, pcParentName);
	}

	// Clean up temp storage of mods and combos
	if ((pPowerDef->eType == kPowerType_Combo) && (eaSize(&pPowerDef->ppMods) > 0)) {
		for(i=eaSize(&pPowerDef->ppMods)-1; i>=0; --i) {
			StructDestroy(parse_AttribModDef, pPowerDef->ppMods[i]);
		}
		eaClear(&pPowerDef->ppMods);

	} else if ((pPowerDef->eType != kPowerType_Combo) && (eaSize(&pPowerDef->ppCombos) > 0)) {
		for(i=eaSize(&pPowerDef->ppCombos)-1; i>=0; --i) {
			StructDestroy(parse_PowerCombo, pPowerDef->ppCombos[i]);
		}
		eaClear(&pPowerDef->ppCombos);
	}
	
	// Power needs to be cleaned up
	powerdef_FillMissingAttribKeys(pPowerDef);
	powerdef_FillMissingComboKeys(pPowerDef);

	if(pParentDef)
	{
		char buf[1024];

		PEFixMessageInheritance(pPowerDef,GET_REF(pParentDef->msgDisplayName.hMessage),pPowerDef->msgDisplayName.pEditorCopy,".msgDisplayName.EditorCopy");
		PEFixMessageInheritance(pPowerDef,GET_REF(pParentDef->msgDescription.hMessage),pPowerDef->msgDescription.pEditorCopy,".msgDescription.EditorCopy");
		PEFixMessageInheritance(pPowerDef,GET_REF(pParentDef->msgDescriptionLong.hMessage),pPowerDef->msgDescriptionLong.pEditorCopy,".msgDescriptionLong.EditorCopy");
		PEFixMessageInheritance(pPowerDef,GET_REF(pParentDef->msgDescriptionFlavor.hMessage),pPowerDef->msgDescriptionFlavor.pEditorCopy,".msgDescriptionFlavor.EditorCopy");
		PEFixMessageInheritance(pPowerDef,GET_REF(pParentDef->msgAttribOverride.hMessage),pPowerDef->msgAttribOverride.pEditorCopy,".msgAttribOverride.EditorCopy");
		PEFixMessageInheritance(pPowerDef,GET_REF(pParentDef->msgAutoDesc.hMessage),pPowerDef->msgAutoDesc.pEditorCopy,".msgAutoDesc.EditorCopy");
		PEFixMessageInheritance(pPowerDef,GET_REF(pParentDef->msgRankChange.hMessage),pPowerDef->msgRankChange.pEditorCopy,".msgRankChange.EditorCopy");

		for(i=eaSize(&pPowerDef->ppMods)-1; i>=0; i--)
		{
			AttribModDef *pmoddef = pPowerDef->ppMods[i];
			AttribModDef *pmoddefParent = eaIndexedGetUsingInt(&pParentDef->ppMods,pmoddef->iKey);
			if(pmoddefParent)
			{
				sprintf(buf, ".AttribMod[\"%d\"].msgAutoDesc.EditorCopy",pmoddef->iKey);
				PEFixMessageInheritance(pPowerDef, GET_REF(pmoddefParent->msgAutoDesc.hMessage), pmoddef->msgAutoDesc.pEditorCopy, buf);
			}
		}
	}

	if(pPowerDef->pAIPowerConfigDefInst)
	{
		static AIPowerConfigDef *pAIPCD = NULL;

		if(!pAIPCD)
			pAIPCD = StructCreate(parse_AIPowerConfigDef);

		pAIPCD->filename = pPowerDef->pAIPowerConfigDefInst->filename;

		if(!StructCompare(parse_AIPowerConfigDef, pPowerDef->pAIPowerConfigDefInst, pAIPCD, 0, 0, TOK_KEY | TOK_USEDFIELD | TOK_INHERITANCE_STRUCT))
		{
			char buf[1024];
			//StructDestroySafe(parse_AIPowerConfigDef, &pPowerDef->pAIPowerConfigDefInst);

			if(pParentDef)
			{
				sprintf(buf, ".AIPowerConfigDefInst");

				if(StructInherit_GetOverrideType(parse_PowerDef, pPowerDef, buf) != OVERRIDE_NONE)
					StructInherit_DestroyOverride(parse_PowerDef, pPowerDef, buf);

				if(pParentDef->pAIPowerConfigDefInst)
				{
					pAIPCD->filename = pParentDef->pAIPowerConfigDefInst->filename;

					if(StructCompare(parse_AIPowerConfigDef, pParentDef->pAIPowerConfigDefInst, pAIPCD, 0, 0, TOK_KEY | TOK_USEDFIELD | TOK_INHERITANCE_STRUCT))
						StructInherit_CreateRemoveStructOverride(parse_PowerDef, pPowerDef, buf);
				}
			}
		}
	}

	// Cleanup of optional substructs on AttribMods
	for(i=eaSize(&pPowerDef->ppMods)-1; i>=0; i--)
	{
		char buf[1024];
		ParseTable *pUnusedTable;
		AttribModDef *pmodDef = pPowerDef->ppMods[i];
		int bInherit = pParentDef && pe_doesInheritMod(pPowerDef, pmodDef);
		
		if(pmodDef->pExpiration && !REF_STRING_FROM_HANDLE(pmodDef->pExpiration->hDef))
		{
			StructDestroySafe(parse_ModExpiration,&pmodDef->pExpiration);

			// Since this happens after inheritance is fixed up, need to correct inheritance data
			// This logic determines if the parent had a value, then we need to have a 
			// remove override.  Otherwise, the system is okay as it is.
			if (bInherit) {
				ModExpiration *pParentValue = NULL;

				sprintf(buf, ".AttribMod[\"%d\"].expiration", pmodDef->iKey);
				objPathGetStruct(buf, parse_PowerDef, pParentDef, &pUnusedTable, &pParentValue);

				if (StructInherit_GetOverrideType(parse_PowerDef, pPowerDef, buf) != OVERRIDE_NONE) {
					StructInherit_DestroyOverride(parse_PowerDef, pPowerDef, buf);
				}
				if (pParentValue) {
					StructInherit_CreateRemoveStructOverride(parse_PowerDef, pPowerDef, buf);
				}
			}
		}

		if(pmodDef->pFragility
			&& !pmodDef->pFragility->pExprHealth 
			&& !pmodDef->pFragility->bMagnitudeIsHealth)
		{
			StructDestroySafe(parse_ModDefFragility,&pmodDef->pFragility);

			// Copy of above code for ModDefFragility
			if (bInherit) {
				ModDefFragility *pParentValue = NULL;

				sprintf(buf, ".AttribMod[\"%d\"].fragility", pmodDef->iKey);
				objPathGetStruct(buf, parse_PowerDef, pParentDef, &pUnusedTable, &pParentValue);

				if (pParentValue) {
					if (StructInherit_GetOverrideType(parse_PowerDef, pPowerDef, buf) != OVERRIDE_NONE) {
						StructInherit_DestroyOverride(parse_PowerDef, pPowerDef, buf);
					}
					if (pParentValue->pExprHealth || pParentValue->bMagnitudeIsHealth) {
						StructInherit_CreateRemoveStructOverride(parse_PowerDef, pPowerDef, buf);
					}
				}
			}
		}
	}

	i = powerdef_Validate(pPowerDef,true);
	if(!powerdef_ValidateReferences(PARTITION_STATIC_CHECK, pPowerDef))
	{
		ui_DialogPopup("Error", "Reference validation failed.");
		i = false;
	}
	return i;
}


static void *pe_createObject(METable *pTable, PowerDef *pObjectToClone, char *pcNewName, bool bCloneKeepsKeys)
{
	#define PE_DEFAULT_POWER_PATH "Editors/DefaultDefs/Default.Powers"
	PowerDef *pDefaultDef = NULL;
	PowerDef *pNewDef;
	char buf[128];
	const char *pcBaseName;
	const char *pcBaseDisplayName = NULL;
	int i;
	
	if(!pObjectToClone) {
		pDefaultDef = StructCreate(parse_PowerDef);
		if(ParserLoadSingleDictionaryStruct(PE_DEFAULT_POWER_PATH, g_hPowerDefDict, pDefaultDef, 0)) {
			if(pDefaultDef->pInheritance) {
				ErrorFilenamef(PE_DEFAULT_POWER_PATH, "The default def can not have inheritance.");
			} else if(langHasActiveMessage(parse_PowerDef, pDefaultDef)) {
				ErrorFilenamef(PE_DEFAULT_POWER_PATH, "The default def can not have any messages in it.");
			} else {
				pObjectToClone = pDefaultDef;
			}
		}
	}

	// Create the object
	if (pObjectToClone) {
		Message *pmsg;
		pNewDef = StructClone(parse_PowerDef, pObjectToClone);
		assert(pNewDef);
		if(pObjectToClone == pDefaultDef)
			pcBaseName = "_New_Power";
		else
			pcBaseName = pObjectToClone->pchName;
		pmsg = GET_REF(pObjectToClone->msgDisplayName.hMessage);
		if(pmsg)
		{
			pcBaseDisplayName = pmsg->pcDefaultString;
		}
		if (!pcBaseDisplayName) {
			pcBaseDisplayName = "New Power";
		}
		pNewDef->iAttribKeyBlock = 0; // Force new key block

		if (!bCloneKeepsKeys) {
			PowerCombo **eaCombos = NULL;
			AttribModDef **eaMods= NULL;

			eaIndexedEnable(&eaCombos, parse_PowerCombo);
			for(i=0; i<eaSize(&pNewDef->ppCombos); ++i) {
				if (!pe_doesInheritCombo(pNewDef,pNewDef->ppCombos[i])) {
					pNewDef->ppCombos[i]->iKey = powerdef_GetNextComboKey(pNewDef, 0);
				}
				eaPush(&eaCombos, pNewDef->ppCombos[i]);
			}
			eaDestroy(&pNewDef->ppCombos);
			pNewDef->ppCombos = eaCombos;

			eaIndexedEnable(&eaMods, parse_AttribModDef);
			for(i=0; i<eaSize(&pNewDef->ppMods); ++i) {
				if (!pe_doesInheritMod(pNewDef,pNewDef->ppMods[i])) {
					pNewDef->ppMods[i]->iKey = powerdef_GetNextAttribKey(pNewDef, 0);
				}
				eaPush(&eaMods, pNewDef->ppMods[i]);
			}
			eaDestroy(&pNewDef->ppMods);
			pNewDef->ppMods = eaMods;
		}
	} else {
		pNewDef = StructCreate(parse_PowerDef);

		// Assign some defaults
		SET_HANDLE_FROM_STRING("PowerTarget", "Self", pNewDef->hTargetMain);
		SET_HANDLE_FROM_STRING("PowerTarget", "Self", pNewDef->hTargetAffected);

		pcBaseName = "_New_Power";
		pcBaseDisplayName = "New Power";
	}
	// Use provided name if available
	if (pcNewName) {
		pcBaseName = pcNewName;
		pcBaseDisplayName = pcNewName;
	}

	assertmsg(pNewDef, "Failed to create power");

	// Assign a new name
	pNewDef->pchName = (char*)METableMakeNewNameShared(pTable,pcBaseName, true);

	// Assign a file
	sprintf(buf,"defs/powers/%s.powers",pNewDef->pchName);
	pNewDef->pchFile = (char*)allocAddString(buf);

	// Prep display name and all the messages
	langMakeEditorCopy(parse_PowerDef, pNewDef, true);
	pNewDef->msgDisplayName.pEditorCopy->pcDefaultString = StructAllocString(pcBaseDisplayName);

	StructDestroySafe(parse_PowerDef, &pDefaultDef);

	return pNewDef;
}


static void *pe_tableCreateCallback(METable *pTable, PowerDef *pObjectToClone, bool bCloneKeepsKeys)
{
	return pe_createObject(pTable, pObjectToClone, NULL, bCloneKeepsKeys);
}


static void *pe_windowCreateCallback(MEWindow *pWindow, PowerDef *pObjectToClone)
{
	return pe_createObject(pWindow->pTable, pObjectToClone, NULL, false);
}

static void pe_setFieldApplicableHitChanceOneTime(METable *pTable, PowerDef *pPowerDef)
{
	// Only applicable in games with HitChance system enabled, on single target toggles and maintaineds
	S32 bApplicable = (g_CombatConfig.pHitChance!=NULL
		&& pPowerDef->eEffectArea==kEffectArea_Character
		&& (pPowerDef->eType==kPowerType_Toggle
			|| pPowerDef->eType==kPowerType_Maintained));

	METableSetFieldNotApplicable(pTable, pPowerDef, "OneTime HitChance", !bApplicable);
}

static void pe_setFieldApplicableHitChanceIgnore(METable *pTable, PowerDef *pPowerDef)
{
	METableSetFieldNotApplicable(pTable, pPowerDef, "Ignore HitChance", g_CombatConfig.pHitChance==NULL);
}

static void pe_setFieldApplicableActivateWhileMundane(METable *pTable, PowerDef *pPowerDef)
{
	METableSetFieldNotApplicable(pTable, pPowerDef, "Use While Mundane", g_CombatConfig.pBattleForm==NULL);
}

static void pe_setFieldsApplicableRecharge(METable *pTable, PowerDef *pPowerDef)
{
	S32 bShowRechargeOptions = POWERTYPE_ACTIVATABLE(pPowerDef->eType) && pPowerDef->eType!=kPowerType_Combo;
	S32 bShowTimeRecharge = !!pPowerDef->fTimeRecharge || (bShowRechargeOptions && !pPowerDef->bRechargeDisabled);
	S32 bShowRechargeFlags = pPowerDef->bRechargeRequiresHit || (bShowRechargeOptions && (pPowerDef->fTimeRecharge || pPowerDef->bRechargeDisabled));

	METableSetFieldNotApplicable(pTable, pPowerDef, "Recharge Time", !bShowTimeRecharge);
	METableSetFieldNotApplicable(pTable, pPowerDef, "Recharge Disabled", !bShowRechargeOptions);
	METableSetFieldNotApplicable(pTable, pPowerDef, "Recharge Requires Hit", !bShowRechargeFlags);
	METableSetFieldNotApplicable(pTable, pPowerDef, "Recharge Requires Combat", !bShowRechargeFlags);
}

static void pe_setFieldApplicableCooldownGlobal(METable *pTable, PowerDef *pPowerDef)
{
	S32 bApplicable = POWERTYPE_ACTIVATABLE(pPowerDef->eType) && g_CombatConfig.fCooldownGlobal!=0;
	METableSetFieldNotApplicable(pTable, pPowerDef, "GCD Not Checked", !bApplicable);
	METableSetFieldNotApplicable(pTable, pPowerDef, "GCD Not Applied", !bApplicable);
}


static void pe_parentChangeCallback(METable *pTable, PowerDef *pPowerDef, void *pUserData, bool bInitNotify)
{
	const char *pcParentName;

	// Check for invalid type value
	pcParentName = StructInherit_GetParentName(parse_PowerDef, pPowerDef);
	if (pcParentName) {
		PowerDef *pParentDef = (PowerDef*)RefSystem_ReferentFromString(g_hPowerDefDict, pcParentName);
		if (pParentDef) {
			if ((pParentDef->eType == kPowerType_Combo) && (pPowerDef->eType != kPowerType_Combo)) {
				pPowerDef->eType = kPowerType_Combo;
				METableRefreshRow(pTable, pPowerDef);
				METableHideSubTable(pTable, pPowerDef, peAttribModId, 1);
				METableHideSubTable(pTable, pPowerDef, peCombosId, 0);
				ui_DialogPopup("Notice", "The power's parent is a Combo.  This power's type has been reset to Combo.");

			} else if ((pParentDef->eType != kPowerType_Combo) && (pPowerDef->eType == kPowerType_Combo)) {
				pPowerDef->eType = kPowerType_Click;
				METableRefreshRow(pTable, pPowerDef);
				METableHideSubTable(pTable, pPowerDef, peAttribModId, 0);
				METableHideSubTable(pTable, pPowerDef, peCombosId, 1);
				ui_DialogPopup("Notice", "The power's parent is not a Combo.  This power's type has been reset to Click.");
			}
		}
	}
}

static void pe_powerTypeChangeCallback(METable *pTable, PowerDef *pPowerDef, void *pUserData, bool bInitNotify)
{
	const char *pcParentName;

	// Check for invalid type value
	pcParentName = StructInherit_GetParentName(parse_PowerDef, pPowerDef);
	if (pcParentName) {
		PowerDef *pParentDef = (PowerDef*)RefSystem_ReferentFromString(g_hPowerDefDict, pcParentName);
		if (pParentDef) {
			if ((pParentDef->eType == kPowerType_Combo) && (pPowerDef->eType != kPowerType_Combo)) {
				pPowerDef->eType = kPowerType_Combo;
				METableRefreshRow(pTable, pPowerDef);
				ui_DialogPopup("Error", "A power cannot be changed away from the Combo type if its parent is a Combo.  This power's type has been reset to Combo.");

			} else if ((pParentDef->eType != kPowerType_Combo) && (pPowerDef->eType == kPowerType_Combo)) {
				pPowerDef->eType = kPowerType_Click;
				METableRefreshRow(pTable, pPowerDef);
				ui_DialogPopup("Error", "A power cannot be changed to the Combo type if its parent is not a Combo.  This power's type has been reset to Click.");
			}
		}
	}

	// Hide the appropriate sublist for this power
	if (pPowerDef->eType == kPowerType_Combo) {
		METableHideSubTable(pTable, pPowerDef, peAttribModId, 1);
		METableHideSubTable(pTable, pPowerDef, peCombosId, 0);
	} else {
		METableHideSubTable(pTable, pPowerDef, peAttribModId, 0);
		METableHideSubTable(pTable, pPowerDef, peCombosId, 1);
	}

	// The type doesn't support disabling via events
	if (pPowerDef->eType == kPowerType_Toggle) {
		METableSetFieldNotApplicable(pTable, pPowerDef, "Disabling Events", 0);
		METableSetFieldNotApplicable(pTable, pPowerDef, "Event Time", 0);
	} else {
		METableSetFieldNotApplicable(pTable, pPowerDef, "Disabling Events", 1);
		METableSetFieldNotApplicable(pTable, pPowerDef, "Event Time", 1);
	}

	METableSetFieldNotApplicable(pTable, pPowerDef, "Auto Reapply", !(pPowerDef->bAutoReapply || pPowerDef->eType==kPowerType_Passive || pPowerDef->eType==kPowerType_Toggle));

	METableSetFieldNotApplicable(pTable, pPowerDef, "Queue Expr", !(pPowerDef->pExprRequiresQueue || (POWERTYPE_ACTIVATABLE(pPowerDef->eType) && pPowerDef->eType!=kPowerType_Combo)));
	METableSetFieldNotApplicable(pTable, pPowerDef, "Enhance Attach Expr", !(pPowerDef->pExprEnhanceAttach || pPowerDef->eType==kPowerType_Enhancement));
	METableSetFieldNotApplicable(pTable, pPowerDef, "Enhance Attach Unowned", pPowerDef->eType!=kPowerType_Enhancement);
	METableSetFieldNotApplicable(pTable, pPowerDef, "Enhance Apply Expr", !(pPowerDef->pExprEnhanceApply || pPowerDef->eType==kPowerType_Enhancement));
	METableSetFieldNotApplicable(pTable, pPowerDef, "Enhance Copy Level", !(pPowerDef->bEnhanceCopyLevel || pPowerDef->eType==kPowerType_Enhancement));
	METableSetFieldNotApplicable(pTable, pPowerDef, "Enhance For EntCreate", !(pPowerDef->pExprEnhanceEntCreate || pPowerDef->eType==kPowerType_Enhancement));
	METableSetFieldNotApplicable(pTable, pPowerDef, "Enhance Power Fields", !(pPowerDef->pExprEnhanceAttach || pPowerDef->eType==kPowerType_Enhancement));

	METableSetFieldNotApplicable(pTable, pPowerDef, "Check Combo Before Toggle", pPowerDef->eType!=kPowerType_Combo);

	pe_setFieldApplicableHitChanceOneTime(pTable, pPowerDef);
	pe_setFieldApplicableHitChanceIgnore(pTable, pPowerDef);
	pe_setFieldApplicableActivateWhileMundane(pTable, pPowerDef);
	pe_setFieldsApplicableRecharge(pTable, pPowerDef);
	pe_setFieldApplicableCooldownGlobal(pTable, pPowerDef);
}


static void pe_effectAreaChangeCallback(METable *pTable, PowerDef *pPowerDef, void *pUserData, bool bInitNotify)
{
	S32 bShowRadius = !!pPowerDef->pExprRadius;
	S32 bShowInnerRadius = !!pPowerDef->pExprInnerRadius;
	S32 bShowArc = !!pPowerDef->pExprArc;
	S32 bShowMaxTargetsHit = !!pPowerDef->iMaxTargetsHit;
	S32 bShowYaw = !!pPowerDef->fYaw;

	if(pPowerDef->eEffectArea == kEffectArea_Sphere)
	{
		bShowRadius = true;
		bShowInnerRadius = true;
		bShowMaxTargetsHit = true;
	}
	else if(pPowerDef->eEffectArea == kEffectArea_Cone)
	{
		bShowArc = true;
		bShowMaxTargetsHit = true;
		bShowYaw = true;
	}
	else if(pPowerDef->eEffectArea == kEffectArea_Team)
	{
		bShowRadius = true;
		bShowMaxTargetsHit = true;
	}
	else if(pPowerDef->eEffectArea == kEffectArea_Cylinder)
	{
		bShowRadius = true;
		bShowMaxTargetsHit = true;
		bShowYaw = true;
	}
	else if(pPowerDef->eEffectArea == kEffectArea_Volume)
	{
		bShowMaxTargetsHit = true;
	}
	else if(pPowerDef->eEffectArea == kEffectArea_Map)
	{
		bShowMaxTargetsHit = true;
	}

	METableSetFieldNotApplicable(pTable, pPowerDef, "Radius Expression", !bShowRadius);
	METableSetFieldNotApplicable(pTable, pPowerDef, "Inner Radius Expression", !bShowInnerRadius);
	METableSetFieldNotApplicable(pTable, pPowerDef, "Arc Expression", !bShowArc);
	METableSetFieldNotApplicable(pTable, pPowerDef, "Max Targets Hit", !bShowMaxTargetsHit);
	METableSetFieldNotApplicable(pTable, pPowerDef, "Effect Area Sort", !bShowMaxTargetsHit);

	pe_setFieldApplicableHitChanceOneTime(pTable, pPowerDef);
}

static void pe_categoryChangeCallback(METable *pTable, PowerDef *pPowerDef, void *pUserData, bool bInitNotify)
{
	WorldRegionType eType = powerdef_GetBestRegionType(pPowerDef);
	RegionRules *pRegionRules = getRegionRulesFromRegionType(eType);

	if(pRegionRules)
	{
		F32 fConversion = 1.0f;

		fConversion = BaseToMeasurement(fConversion,pRegionRules->eDefaultMeasurement,pRegionRules->eMeasurementSize) * pRegionRules->fDefaultDistanceScale;

		if(fConversion != 1.0f)
		{
			METableSetFieldScale(pTable,pPowerDef,"Range (UI)", fConversion);
		}
	}
	else
	{
		METableSetFieldScale(pTable,pPowerDef,"Range (UI)", 1.0f);
	}
}

static void pe_costAttribChangeCallback(METable *pTable, PowerDef *pPowerDef, void *pUserData, bool bInitNotify)
{
	S32 bCostAttrib = ATTRIB_NOT_DEFAULT(pPowerDef->eAttribCost);
	S32 bShowCostSecondary = bCostAttrib || !!pPowerDef->pExprCostSecondary || pPowerDef->pExprCostPeriodicSecondary;

	METableSetFieldNotApplicable(pTable, pPowerDef, "Secondary Cost", !bShowCostSecondary);
	METableSetFieldNotApplicable(pTable, pPowerDef, "Secondary Periodic Cost", !bShowCostSecondary);
}

static void pe_rechargeChangeCallback(METable *pTable, PowerDef *pPowerDef, void *pUserData, bool bInitNotify)
{
	pe_setFieldsApplicableRecharge(pTable, pPowerDef);
}

static void pe_chargesChangeCallback(METable *pTable, PowerDef *pPowerDef, void *pUserData, bool bInitNotify)
{
	S32 bShowChargeOptions = pPowerDef->iCharges > 0;

	METableSetFieldNotApplicable(pTable, pPowerDef, "Charge Refill Interval", !bShowChargeOptions);
	METableSetFieldNotApplicable(pTable, pPowerDef, "Charges Set Cooldown When Empty", !bShowChargeOptions);
}

static void pe_periodChangeCallback(METable *pTable, PowerDef *pPowerDef, AttribModDef *pModDef, void *pUserData, bool bInitNotify)
{
	S32 bPeriodic = pModDef->fPeriod!=0;
	METableSetSubFieldNotApplicable(pTable, pPowerDef, peAttribModId, pModDef, "Ignore First Tick", !pModDef->bIgnoreFirstTick && !bPeriodic);
	METableSetSubFieldNotApplicable(pTable, pPowerDef, peAttribModId, pModDef, "Replace Keeps Timer", !pModDef->bReplaceKeepsTimer && !(bPeriodic && (pModDef->eStack==kStackType_Replace || pModDef->eStack==kStackType_KeepBest)));
}

static void pe_expirationDefChangeCallback(METable *pTable, PowerDef *pPowerDef, AttribModDef *pModDef, void *pUserData, bool bInitNotify)
{
	if (pModDef->pExpiration && REF_STRING_FROM_HANDLE(pModDef->pExpiration->hDef)) {
		METableSetSubFieldNotApplicable(pTable, pPowerDef, peAttribModId, pModDef, "Expiration Target", 0);
		METableSetSubFieldNotApplicable(pTable, pPowerDef, peAttribModId, pModDef, "Expiration Requires", 0);
		METableSetSubFieldNotApplicable(pTable, pPowerDef, peAttribModId, pModDef, "Expiration Periodic", 0);
		if (pModDef->pExpiration && (pModDef->pExpiration->eTarget == 0)) {
			pModDef->pExpiration->eTarget = kModExpirationEntity_ModOwner;
		}
	} else {
		METableSetSubFieldNotApplicable(pTable, pPowerDef, peAttribModId, pModDef, "Expiration Target", 1);
		METableSetSubFieldNotApplicable(pTable, pPowerDef, peAttribModId, pModDef, "Expiration Requires", 1);
		METableSetSubFieldNotApplicable(pTable, pPowerDef, peAttribModId, pModDef, "Expiration Periodic", 1);
	}
}

static void pe_fragilityChangeCallback(METable *pTable, PowerDef *pPowerDef, AttribModDef *pModDef, void *pUserData, bool bInitNotify)
{
	if (pModDef->pFragility 
		&& (pModDef->pFragility->pExprHealth 
			|| pModDef->pFragility->bMagnitudeIsHealth)) {
		S32 bUnset = false;
		METableSetSubFieldNotApplicable(pTable, pPowerDef, peAttribModId, pModDef, "Health Table", 0);
		METableSetSubFieldNotApplicable(pTable, pPowerDef, peAttribModId, pModDef, "Proportion", 0);
		METableSetSubFieldNotApplicable(pTable, pPowerDef, peAttribModId, pModDef, "Tags Excluded", 0);
		METableSetSubFieldNotApplicable(pTable, pPowerDef, peAttribModId, pModDef, "Scale In", 0);
		METableSetSubFieldNotApplicable(pTable, pPowerDef, peAttribModId, pModDef, "Scale Out", 0);
		METableSetSubFieldNotApplicable(pTable, pPowerDef, peAttribModId, pModDef, "Source Only In", 0);
		METableSetSubFieldNotApplicable(pTable, pPowerDef, peAttribModId, pModDef, "Source Only Out", 0);
		METableSetSubFieldNotApplicable(pTable, pPowerDef, peAttribModId, pModDef, "Use Resist In", 0);
		METableSetSubFieldNotApplicable(pTable, pPowerDef, peAttribModId, pModDef, "Use Resist Out", 0);
		METableSetSubFieldNotApplicable(pTable, pPowerDef, peAttribModId, pModDef, "Fragile While Delayed", 0);
		METableSetSubFieldNotApplicable(pTable, pPowerDef, peAttribModId, pModDef, "Fragile to Self", 0);
		METableSetSubFieldNotApplicable(pTable, pPowerDef, peAttribModId, pModDef, "Unkillable", 0);
		METableSetSubFieldNotApplicable(pTable, pPowerDef, peAttribModId, pModDef, "Replace Keeps Health", 0);
		METableSetSubFieldNotApplicable(pTable, pPowerDef, peAttribModId, pModDef, "Init Point", 0);
		if (pModDef->pFragility->pool.eInit == kCombatPoolPoint_Unset) {
			pModDef->pFragility->pool.eInit = kCombatPoolPoint_Max;
			bUnset = true;
		}
		if (pModDef->pFragility->pool.eBound == kCombatPoolBound_Unset) {
			pModDef->pFragility->pool.eBound = kCombatPoolBound_None;
			bUnset = true;
		}
		if (bUnset && pModDef->pFragility->fProportion == 0) {
			pModDef->pFragility->fProportion = 1;
		}
	} else {
		METableSetSubFieldNotApplicable(pTable, pPowerDef, peAttribModId, pModDef, "Health Table", 1);
		METableSetSubFieldNotApplicable(pTable, pPowerDef, peAttribModId, pModDef, "Proportion", 1);
		METableSetSubFieldNotApplicable(pTable, pPowerDef, peAttribModId, pModDef, "Tags Excluded", 1);
		METableSetSubFieldNotApplicable(pTable, pPowerDef, peAttribModId, pModDef, "Scale In", 1);
		METableSetSubFieldNotApplicable(pTable, pPowerDef, peAttribModId, pModDef, "Scale Out", 1);
		METableSetSubFieldNotApplicable(pTable, pPowerDef, peAttribModId, pModDef, "Source Only In", 1);
		METableSetSubFieldNotApplicable(pTable, pPowerDef, peAttribModId, pModDef, "Source Only Out", 1);
		METableSetSubFieldNotApplicable(pTable, pPowerDef, peAttribModId, pModDef, "Use Resist In", 1);
		METableSetSubFieldNotApplicable(pTable, pPowerDef, peAttribModId, pModDef, "Use Resist Out", 1);
		METableSetSubFieldNotApplicable(pTable, pPowerDef, peAttribModId, pModDef, "Fragile While Delayed", 1);
		METableSetSubFieldNotApplicable(pTable, pPowerDef, peAttribModId, pModDef, "Fragile to Self", 1);
		METableSetSubFieldNotApplicable(pTable, pPowerDef, peAttribModId, pModDef, "Unkillable", 1);
		METableSetSubFieldNotApplicable(pTable, pPowerDef, peAttribModId, pModDef, "Replace Keeps Health", 1);
		METableSetSubFieldNotApplicable(pTable, pPowerDef, peAttribModId, pModDef, "Init Point", 1);
	}
}


static void pe_attribTypeAspectChangeCallback(METable *pTable, PowerDef *pPowerDef, AttribModDef *pModDef, void *pUserData, bool bInitNotify)
{
	int i,j;
	bool bFound = false;
	bool bChanged = false;
	bool bSupportsAffectExpr;

	METableSetSubFieldNotApplicable(pTable, pPowerDef, peAttribModId, pModDef, "Hit Test", g_CombatConfig.pHitChance==NULL);
	
	bSupportsAffectExpr = !(IS_NORMAL_ATTRIB(pModDef->offAttrib) && IS_BASIC_ASPECT(pModDef->offAspect) && !POWER_AFFECTOR(pModDef->offAttrib));
	METableSetSubFieldNotApplicable(pTable, pPowerDef, peAttribModId, pModDef, "Affects Expression", !bSupportsAffectExpr);

	for(i=eaSize(&eaTypeInfos)-1; i>=0; --i) {
		if (pModDef->offAttrib == eaTypeInfos[i]->eType) {
			// Unhide this column group;
			METableSetSubGroupNotApplicable(pTable, pPowerDef, peAttribModId, pModDef, eaTypeInfos[i]->pcColGroup, 0);
			bFound = true;

			// Properly set up the params struct
			if (pModDef->pParams) {
				if (pModDef->pParams->eType != pModDef->offAttrib) {
					// The type changed so we have to destroy the old struct using the correct table
					for (j=eaSize(&eaTypeInfos)-1; j>=0; --j) {
						if (pModDef->pParams->eType == eaTypeInfos[j]->eType) {
							StructDestroyVoid(eaTypeInfos[j]->pParseTable, pModDef->pParams);
							pModDef->pParams = NULL;
							bChanged = true;
							break;
						}
					}
				}
			}
			if (!pModDef->pParams) {
				// Need to create the struct
				pModDef->pParams = StructCreateVoid(eaTypeInfos[i]->pParseTable);
				pModDef->pParams->eType = eaTypeInfos[i]->eType;
				bChanged = true;
			}
		} else {
			// Hide this column group
			METableSetSubGroupNotApplicable(pTable, pPowerDef, peAttribModId, pModDef, eaTypeInfos[i]->pcColGroup, 1);
		}
	}

	if (!bFound && pModDef->pParams) {
		// If new value doesn't have a params value and the old value did, then we have to use the
		// correct parse table when struct destroying the struct
		for (i=eaSize(&eaTypeInfos)-1; i>=0; --i) {
			if (pModDef->pParams->eType == eaTypeInfos[i]->eType) {
				StructDestroyVoid(eaTypeInfos[i]->pParseTable, pModDef->pParams);
				pModDef->pParams = NULL;
				bChanged = true;
				break;
			}
		}
	}

	if (bChanged && !bInitNotify) {
		// If we changed the data, we have to regenerate the row
		METableRegenerateRow(pTable, pPowerDef);
	}
}

static void pe_stackTypeChangeCallback(METable *pTable, PowerDef *pPowerDef, AttribModDef *pModDef, void *pUserData, bool bInitNotify)
{
	if (pModDef->eStack==kStackType_Discard
		|| pModDef->eStack==kStackType_Replace
		|| pModDef->eStack==kStackType_KeepBest)
	{
		METableSetSubFieldNotApplicable(pTable, pPowerDef, peAttribModId, pModDef, "Stack Limit", 0);
	}
	else
	{
		METableSetSubFieldNotApplicable(pTable, pPowerDef, peAttribModId, pModDef, "Stack Limit", 1);
	}
	METableSetSubFieldNotApplicable(pTable, pPowerDef, peAttribModId, pModDef, "Stack Entity", pModDef->eStack==kStackType_Stack);
	METableSetSubFieldNotApplicable(pTable, pPowerDef, peAttribModId, pModDef, "Replace Keeps Timer", !pModDef->bReplaceKeepsTimer && !(pModDef->fPeriod && (pModDef->eStack==kStackType_Replace || pModDef->eStack==kStackType_KeepBest)));
}

static void pe_fixInheritanceCallback(METable *pTable, PowerDef *pPowerDef)
{
	int i,j;
	const char *pcParentName;
	PowerDef *pParentDef;

	pcParentName = StructInherit_GetParentName(parse_PowerDef, pPowerDef);
	if (!pcParentName) {
		return;
	}
	pParentDef = RefSystem_ReferentFromString(g_hPowerDefDict, pcParentName);
	if (!pParentDef) {
		return;
	}

	for(i=eaSize(&pPowerDef->ppMods)-1; i>=0; --i) {
		if (pe_doesInheritMod(pPowerDef, pPowerDef->ppMods[i])) {
			char buf[1024];
			AttribModDefParams *pParentValue = NULL;
			ParseTable *pUnusedTable;

			sprintf(buf, ".AttribMod[\"%d\"].params", pPowerDef->ppMods[i]->iKey);
			objPathGetStruct(buf, parse_PowerDef, pParentDef, &pUnusedTable, &pParentValue);

			if (pParentValue && !pPowerDef->ppMods[i]->pParams) {
				// If an inherited modifier has a parent with "params" set to non-NULL and
				// the child wants it set to NULL, we have to hack in an override
				if (StructInherit_GetOverrideType(parse_PowerDef, pPowerDef, buf) != OVERRIDE_NONE) {
					StructInherit_DestroyOverride(parse_PowerDef, pPowerDef, buf);
				}
				StructInherit_CreateRemoveStructOverride(parse_PowerDef, pPowerDef, buf);
			} else if (!pParentValue && pPowerDef->ppMods[i]->pParams) {
				// If an inherited modifier has a parent with no "params" set, and the child
				// has one that does have params set, then we need to have a SET override
				bool bFound = false;
				for(j=eaSize(&eaTypeInfos)-1; j>=0; --j) {
					if (pPowerDef->ppMods[i]->offAttrib == eaTypeInfos[j]->eType) {
						bFound = true;
						break;
					}
				}
				if (StructInherit_GetOverrideType(parse_PowerDef, pPowerDef, buf) != OVERRIDE_NONE) {
					StructInherit_DestroyOverride(parse_PowerDef, pPowerDef, buf);
				}
				if (bFound) {
					StructInherit_CreateFieldOverride(parse_PowerDef, pPowerDef, buf);
				}
			} else if (pParentValue && pPowerDef->ppMods[i]->pParams) {
				if 	(pParentValue->eType != pPowerDef->ppMods[i]->pParams->eType) {
					// If they both have params set, but their types are not the same, then 
					// Need to do a SET override or REMOVE override depending on situation
					bool bFound = false;
					for(j=eaSize(&eaTypeInfos)-1; j>=0; --j) {
						if (pPowerDef->ppMods[i]->offAttrib == eaTypeInfos[j]->eType) {
							bFound = true;
							break;
						}
					}
					if (StructInherit_GetOverrideType(parse_PowerDef, pPowerDef, buf) != OVERRIDE_NONE) {
						StructInherit_DestroyOverride(parse_PowerDef, pPowerDef, buf);
					}
					if (bFound) {
						StructInherit_CreateFieldOverride(parse_PowerDef, pPowerDef, buf);
					} else {
						StructInherit_CreateRemoveStructOverride(parse_PowerDef, pPowerDef, buf);
					}
				} else if (StructInherit_GetOverrideType(parse_PowerDef, pPowerDef, buf) == OVERRIDE_REMOVE) {
					// This happens if the previous setting required a remove and the current no longer
					// requires any override
					StructInherit_DestroyOverride(parse_PowerDef, pPowerDef, buf);
				}
				// else if they both have them and have the same types, then the override set by the
				// base editor will either be NONE or SET and be correct
			} else if (OVERRIDE_NONE != StructInherit_GetOverrideType(parse_PowerDef, pPowerDef, buf)) {
				// Only get here if neither has the struct
				StructInherit_DestroyOverride(parse_PowerDef, pPowerDef, buf);
			}
		}
	}
}


static void pe_dictChangeCallback(enumResourceEventType eType, const char *pDictName, ConstReferenceData pRefData, Referent pReferent, METable *pTable)
{
	METableDictChanged(pTable, eType, pReferent, pRefData);
}


static void pe_messageDictChangeCallback(enumResourceEventType eType, const char *pDictName, const char *pcMessageKey, Referent pReferent, METable *pTable)
{
	if ((eType == RESEVENT_RESOURCE_MODIFIED) ||
		(eType == RESEVENT_RESOURCE_REMOVED) ||
		(eType == RESEVENT_RESOURCE_ADDED)) {

		METableMessageChangedRefresh(pTable, pcMessageKey);
	}
}


static void pe_initCallbacks(MEWindow *pWindow, METable *pTable)
{
	// General Window callbacks
	MEWindowSetCreateCallback(pWindow, pe_windowCreateCallback);

	// General table callbacks
	METableSetValidateCallback(pTable, pe_validateCallback, pTable);
	METableSetCreateCallback(pTable, pe_tableCreateCallback);
	METableSetInheritanceFixCallback(pTable, pe_fixInheritanceCallback);
	METableSetPreSaveCallback(pTable, pe_preSaveCallback);
	METableSetPostOpenCallback(pTable, pe_postOpenCallback);

	// Column change callbacks
	METableSetColumnChangeCallback(pTable, "Parent Power", pe_parentChangeCallback, NULL);
	METableSetColumnChangeCallback(pTable, "Type", pe_powerTypeChangeCallback, NULL);
	METableSetColumnChangeCallback(pTable, "Effect Area", pe_effectAreaChangeCallback, NULL);
	METableSetColumnChangeCallback(pTable, "Categories", pe_categoryChangeCallback, NULL);
	METableSetColumnChangeCallback(pTable, "Cost Attrib", pe_costAttribChangeCallback, NULL);
	METableSetColumnChangeCallback(pTable, "Recharge Time", pe_rechargeChangeCallback, NULL);
	METableSetColumnChangeCallback(pTable, "Recharge Disabled", pe_rechargeChangeCallback, NULL);
	METableSetColumnChangeCallback(pTable, "Charges", pe_chargesChangeCallback, NULL);
	METableSetSubColumnChangeCallback(pTable, peAttribModId, "Attribute", pe_attribTypeAspectChangeCallback, NULL);
	METableSetSubColumnChangeCallback(pTable, peAttribModId, "Aspect", pe_attribTypeAspectChangeCallback, NULL);
	METableSetSubColumnChangeCallback(pTable, peAttribModId, "Stacking", pe_stackTypeChangeCallback, NULL);
	METableSetSubColumnChangeCallback(pTable, peAttribModId, "Period", pe_periodChangeCallback, NULL);
	METableSetSubColumnChangeCallback(pTable, peAttribModId, "Expiration Def", pe_expirationDefChangeCallback, NULL);
	METableSetSubColumnChangeCallback(pTable, peAttribModId, "Magnitude Is Health", pe_fragilityChangeCallback, NULL);
	METableSetSubColumnChangeCallback(pTable, peAttribModId, "Health Expression", pe_fragilityChangeCallback, NULL);

	// We need this registered here instead of by METable because the dictionary will 
	// only allow each callback function to be registered once and there may be multiple
	// METable instances.  Our local callback just passes through to the METable.
	resDictRegisterEventCallback(g_hPowerDefDict, pe_dictChangeCallback, pTable);
	resDictRegisterEventCallback(gMessageDict, pe_messageDictChangeCallback, pTable);
}


static char** pe_getPowerTableNames(METable *pTable, void *pUnused)
{
	char **eaPowerTableNames = NULL;

	powertables_FillAllocdNameEArray(&eaPowerTableNames);
	
	return eaPowerTableNames;
}


//---------------------------------------------------------------------------------------------------
// UI Init
//---------------------------------------------------------------------------------------------------

static void pe_initPowerColumns(METable *pTable)
{
	METableAddSimpleColumn(pTable, "Name", "name", 150, NULL, kMEFieldType_TextEntry);
	METableSetColumnState(pTable, "Name", ME_STATE_NOT_PARENTABLE);

	// Lock in the name column
	METableSetNumLockedColumns(pTable, 2);

	METableAddSimpleColumn(pTable,   "Display Name", ".msgDisplayName.EditorCopy", 160, PE_GROUP_MAIN, kMEFieldType_Message);
	METableAddParentColumn(pTable,   "Parent Power",                180, PE_GROUP_MAIN, true);
	METableAddScopeColumn(pTable,    "Scope",        "group",       160, PE_GROUP_MAIN, kMEFieldType_TextEntry); // Not validated on purpose
	METableAddFileNameColumn(pTable, "File Name",    "file",        210, PE_GROUP_MAIN, NULL, "defs/powers", "defs/powers", ".powers", UIBrowseNewOrExisting);
	METableAddSimpleColumn(pTable,   "Notes",        "notes",       160, PE_GROUP_MAIN, kMEFieldType_MultiText);

	METableAddEnumColumn(pTable,   "Type",               "type",                 100, PE_GROUP_ACTIVATION, kMEFieldType_Combo, PowerTypeEnum);
	METableAddEnumColumn(pTable,   "Categories",         "categories",           140, PE_GROUP_ACTIVATION, kMEFieldType_FlagCombo, PowerCategoriesEnum);
	// Skip "Tags" since it is not user editable
	METableAddExprColumn(pTable,   "Queue Expr",          "ExprBlockRequiresQueue", 160, PE_GROUP_ACTIVATION, combateval_ContextGet(kCombatEvalContext_Activate));
	METableAddSimpleColumn(pTable, "Queue Expr Fail Msg Key", "RequiresQueueFailMsgKey", 230, PE_GROUP_ACTIVATION, kMEFieldType_TextEntry);
	
	METableAddEnumColumn(pTable,   "Required Modes",      "PowerModesRequired",     140, PE_GROUP_ACTIVATION, kMEFieldType_FlagCombo, PowerModeEnum);
	METableAddEnumColumn(pTable,   "Disallowed Modes",    "PowerModesDisallowed",   140, PE_GROUP_ACTIVATION, kMEFieldType_FlagCombo, PowerModeEnum);
	METableAddEnumColumn(pTable,   "Disabling Events",    "combatEvents",           100, PE_GROUP_ACTIVATION, kMEFieldType_FlagCombo, CombatEventEnum);
	METableAddSimpleColumn(pTable, "Event Time",          "combatEventTime",        100, PE_GROUP_ACTIVATION, kMEFieldType_TextEntry);
	METableAddEnumColumn(pTable,   "Ignore Attribs",      "AttribIgnore",           140, PE_GROUP_ACTIVATION, kMEFieldType_FlagCombo, AttribTypeEnum);
	METableAddEnumColumn(pTable,   "Activate Rules",      "ActivateRules",			130, PE_GROUP_ACTIVATION, kMEFieldType_FlagCombo, PowerActivateRulesEnum);
	METableAddSimpleColumn(pTable, "Disallow While Rooted",   "disallowWhileRooted",   130, PE_GROUP_ACTIVATION, kMEFieldType_BooleanCombo);
	METableAddSimpleColumn(pTable, "Use While Mundane",   "activateWhileMundane",   130, PE_GROUP_ACTIVATION, kMEFieldType_BooleanCombo);
	METableAddEnumColumn(pTable,   "Enhance Attach Unowned", "EnhancementAttachUnowned", 100, PE_GROUP_ACTIVATION, kMEFieldType_Combo, EnhancementAttachUnownedTypeEnum);
	METableAddExprColumn(pTable,   "Enhance Attach Expr", "ExprBlockEnhanceAttach", 160, PE_GROUP_ACTIVATION, combateval_ContextGet(kCombatEvalContext_Enhance));
	METableAddExprColumn(pTable,   "Enhance Apply Expr",  "ExprBlockEnhanceApply",  160, PE_GROUP_ACTIVATION, NULL);
	METableAddSimpleColumn(pTable, "Enhance Copy Level",  "enhanceCopyLevel",       130, PE_GROUP_ACTIVATION, kMEFieldType_BooleanCombo);
	METableAddExprColumn(pTable,   "Enhance For EntCreate","ExprEnhanceEntCreate",  160, PE_GROUP_ACTIVATION, combateval_ContextGet(kCombatEvalContext_EntCreateEnhancements));
	METableAddSimpleColumn(pTable, "Enhance Power Fields", "EnhancePowerFields",    130, PE_GROUP_ACTIVATION, kMEFieldType_BooleanCombo);
	METableAddSimpleColumn(pTable, "Never Attach Enhancements", "NeverAttachEnhancements", 120, PE_GROUP_ACTIVATION, kMEFieldType_BooleanCombo);


	METableAddSimpleColumn(pTable, "Propagate Power",     ".powerProp.PropPower",   100, PE_GROUP_ACTIVATION, kMEFieldType_BooleanCombo);
	METableAddEnumColumn(pTable,   "Propagate Type",      ".powerProp.CharacterType",100, PE_GROUP_ACTIVATION, kMEFieldType_FlagCombo,CharClassTypesEnum);
	METableAddExprColumn(pTable,   "Periodic Apply Expr",   "ExprBlockRequiresApply", 160, PE_GROUP_ACTIVATION, combateval_ContextGet(kCombatEvalContext_Apply));
	METableAddSimpleColumn(pTable, "Activation Immunity", "ActivationImmunity", 120, PE_GROUP_ACTIVATION, kMEFieldType_BooleanCombo);
	METableAddSimpleColumn(pTable, "Check Combo Before Toggle",   "CheckComboBeforeToggle",   160, PE_GROUP_ACTIVATION, kMEFieldType_BooleanCombo);	
	METableAddSimpleColumn(pTable, "Unowned Unique ApplyID", "GenerateUniqueApplyID", 120, PE_GROUP_ACTIVATION, kMEFieldType_BooleanCombo);

	METableAddEnumColumn(pTable,   "Cost Attrib",   "attribCost",            100, PE_GROUP_COST, kMEFieldType_Combo, AttribTypeEnum);
	METableAddExprColumn(pTable,   "Cost",          "ExprBlockCost",         130, PE_GROUP_COST, combateval_ContextGet(kCombatEvalContext_Activate));
	METableAddExprColumn(pTable,   "Periodic Cost", "ExprBlockCostPeriodic", 130, PE_GROUP_COST, combateval_ContextGet(kCombatEvalContext_Activate));
	METableAddExprColumn(pTable,   "Secondary Cost","ExprBlockCostSecondary",130, PE_GROUP_COST, combateval_ContextGet(kCombatEvalContext_Activate));
	METableAddExprColumn(pTable,   "Secondary Periodic Cost", "ExprBlockCostPeriodicSecondary", 130, PE_GROUP_COST, combateval_ContextGet(kCombatEvalContext_Activate));
	METableAddEnumColumn(pTable,   "Cost PowerMode","CostPowerMode",         140, PE_GROUP_COST, kMEFieldType_ValidatedTextEntry, PowerModeEnum);
	METableAddGlobalDictColumn(	pTable,	"Item Cost Recipe",	"CostRecipe",    140, PE_GROUP_COST, kMEFieldType_ValidatedTextEntry, "ItemDef", "resourceName");

	METableAddDictColumn(pTable,   "Main Target",           "targetMain",               100, PE_GROUP_TARGET, kMEFieldType_ValidatedTextEntry, "PowerTarget", parse_PowerTarget, "name");
	METableAddEnumColumn(pTable,   "Main Visibility",       "targetVisibilityMain",     120, PE_GROUP_TARGET, kMEFieldType_Combo, TargetVisibilityEnum);
	METableAddDictColumn(pTable,   "Affected Target",       "targetAffected",           120, PE_GROUP_TARGET, kMEFieldType_ValidatedTextEntry, "PowerTarget", parse_PowerTarget, "name");
	METableAddEnumColumn(pTable,   "Affected Visibility",   "targetVisibilityAffected", 140, PE_GROUP_TARGET, kMEFieldType_Combo, TargetVisibilityEnum);
	METableAddSimpleColumn(pTable, "Requires Perceivance",	"AffectedRequiresPerceivance", 100, PE_GROUP_TARGET, kMEFieldType_BooleanCombo);
	METableAddSimpleColumn(pTable, "Ignore HitChance",      "hitChanceIgnore",          100, PE_GROUP_TARGET, kMEFieldType_BooleanCombo);
	METableAddSimpleColumn(pTable, "OneTime HitChance",     "hitChanceOneTime",         100, PE_GROUP_TARGET, kMEFieldType_BooleanCombo);
	METableAddEnumColumn(pTable,   "Tracking",              "tracking",                 140, PE_GROUP_TARGET, kMEFieldType_Combo, TargetTrackingEnum);
	METableAddSimpleColumn(pTable, "Target Arc",		    "targetArc",                100, PE_GROUP_TARGET, kMEFieldType_TextEntry);
	METableAddEnumColumn(pTable,   "Require Valid Target",	"RequireValidTarget",		100, PE_GROUP_TARGET, kMEFieldType_Combo, PowerRequireValidTargetEnum);
	METableAddSimpleColumn(pTable, "Simple Projectile Motion","SimpleProjectileMotion", 120, PE_GROUP_TARGET, kMEFieldType_BooleanCombo);
	METableAddSimpleColumn(pTable, "Disable Confuse Targeting", "DisableConfuseTargeting", 120, PE_GROUP_TARGET, kMEFieldType_BooleanCombo);
	METableAddSimpleColumn(pTable, "Ignore Shield Check",	"IgnoreShieldCheck",		120, PE_GROUP_TARGET, kMEFieldType_BooleanCombo);

	METableAddEnumColumn(pTable,   "Effect Area",       "effectArea",    100, PE_GROUP_EFFECT, kMEFieldType_Combo, EffectAreaEnum);
	METableAddEnumColumn(pTable,   "Effect Area Sort",  "effectAreaSort",100, PE_GROUP_EFFECT, kMEFieldType_Combo, EffectAreaSortEnum);
	METableAddSimpleColumn(pTable, "Max Targets Hit",   "maxTargetsHit", 120, PE_GROUP_EFFECT, kMEFieldType_TextEntry);
	METableAddExprColumn(pTable,   "Radius Expression", "ExprBlockRadius",    140, PE_GROUP_EFFECT, combateval_ContextGet(kCombatEvalContext_Target));
	METableAddExprColumn(pTable,   "Inner Radius Expression", "ExprBlockInnerRadius", 140, PE_GROUP_EFFECT, combateval_ContextGet(kCombatEvalContext_Target));
	METableAddExprColumn(pTable,   "Arc Expression",    "ExprBlockArc",       140, PE_GROUP_EFFECT, combateval_ContextGet(kCombatEvalContext_Target));
	METableAddSimpleColumn(pTable, "Effect Area Centered", "EffectAreaCentered", 140, PE_GROUP_EFFECT, kMEFieldType_BooleanCombo);
	
	METableAddSimpleColumn(pTable, "Yaw Offset",		"yaw",           100, PE_GROUP_EFFECT, kMEFieldType_TextEntry);
	METableAddSimpleColumn(pTable, "Pitch Offset",		"pitch",         100, PE_GROUP_EFFECT, kMEFieldType_TextEntry);
	METableAddSimpleColumn(pTable, "Angle Offsets Root Relative", "areaEffectOffsetsRootRelative", 100, PE_GROUP_EFFECT, kMEFieldType_BooleanCombo);
	METableAddSimpleColumn(pTable, "Front Offset",		"frontOffset",   100, PE_GROUP_EFFECT, kMEFieldType_TextEntry);
	METableAddSimpleColumn(pTable, "Right Offset",		"rightOffset",   100, PE_GROUP_EFFECT, kMEFieldType_TextEntry);
	METableAddSimpleColumn(pTable, "Up Offset",			"upOffset",      100, PE_GROUP_EFFECT, kMEFieldType_TextEntry);
	
	METableAddSimpleColumn(pTable, "Range",             "range",         100, PE_GROUP_EFFECT, kMEFieldType_TextEntry);
	METableAddSimpleColumn(pTable, "Range (UI)",		"range",		 100, PE_GROUP_UI, kMEFieldType_TextEntry);
	METableAddSimpleColumn(pTable, "Range Min",         "rangeMin",      100, PE_GROUP_EFFECT, kMEFieldType_TextEntry);
	METableAddSimpleColumn(pTable, "Range Secondary",   "rangeSecondary",100, PE_GROUP_EFFECT, kMEFieldType_TextEntry);
	METableAddSimpleColumn(pTable, "Cone Starting Radius","startingRadius",100, PE_GROUP_EFFECT, kMEFieldType_TextEntry);

	METableAddEnumColumn(		pTable,	"AI Tags",				"AITags",								140, PE_GROUP_AI, kMEFieldType_FlagCombo, PowerAITagsEnum);
	
	// Can't remove these yet for STO
	if(gConf.bExposeDeprecatedPowerConfigVars)
	{
		METableAddSimpleColumn(		pTable,	"AI Min Range (Dep.)",	"AIMinRange",							120, PE_GROUP_AI, kMEFieldType_TextEntry);
		METableAddSimpleColumn(		pTable,	"AI Max Range (Dep.)",	"AIMaxRange",							120, PE_GROUP_AI, kMEFieldType_TextEntry);
	}
	METableAddGlobalDictColumn(	pTable,	"AI PowerConfigDef",	"AIPowerConfigDef",						140, PE_GROUP_AI, kMEFieldType_ValidatedTextEntry, "AIPowerConfigDef", "resourceName");
	METableAddSimpleColumn(		pTable,	"AI Weight",			".AIPowerConfigDefInst.absWeight",		140, PE_GROUP_AI, kMEFieldType_ValidatedTextEntry);
	METableAddExprColumn(		pTable,	"AI Weight Modifier",	".AIPowerConfigDefInst.weightModifier",	140, PE_GROUP_AI, NULL);
	METableAddSimpleColumn(		pTable,	"AI Min Range",			".AIPowerConfigDefInst.minDist",		140, PE_GROUP_AI, kMEFieldType_ValidatedTextEntry);
	METableAddSimpleColumn(		pTable,	"AI Max Range",			".AIPowerConfigDefInst.maxDist",		140, PE_GROUP_AI, kMEFieldType_ValidatedTextEntry);
	METableAddSimpleColumn(		pTable,	"AI Chain Target",		".AIPowerConfigDefInst.chainTarget",	140, PE_GROUP_AI, kMEFieldType_ValidatedTextEntry);
	METableAddSimpleColumn(		pTable,	"AI Chain Time",		".AIPowerConfigDefInst.chainTime",		140, PE_GROUP_AI, kMEFieldType_ValidatedTextEntry);
	METableAddExprColumn(		pTable,	"AI Chain Requires",	".AIPowerConfigDefInst.chainRequires",	140, PE_GROUP_AI, NULL);
	METableAddSimpleColumn(		pTable,	"AI Chain Locks Facing",".AIPowerConfigDefInst.ChainLocksFacing",	140, PE_GROUP_AI, kMEFieldType_BooleanCombo);
	METableAddSimpleColumn(		pTable,	"AI Chain Locks Movement",".AIPowerConfigDefInst.ChainLocksMovement",	140, PE_GROUP_AI, kMEFieldType_BooleanCombo);
	METableAddExprColumn(		pTable,	"AI Requires",			".AIPowerConfigDefInst.aiRequires",		140, PE_GROUP_AI, NULL);
	METableAddExprColumn(		pTable,	"AI End Condition",		".AIPowerConfigDefInst.aiEndCondition",	140, PE_GROUP_AI, NULL);
	METableAddExprColumn(		pTable,	"AI Target Override",	".AIPowerConfigDefInst.targetOverride",	140, PE_GROUP_AI, NULL);
	METableAddExprColumn(		pTable,	"AI Cure Requires",		".AIPowerConfigDefInst.cureRequires",	140, PE_GROUP_AI, NULL);
	METableAddEnumColumn(		pTable,	"AI Cure Tags",			".AIPowerConfigDefInst.curePowerTags",	100, PE_GROUP_AI, kMEFieldType_FlagCombo, PowerTagsEnum);
	METableAddSimpleColumn(		pTable, "AI Max Delay Time",	".AIPowerConfigDefInst.maxRandomQueueTime", 50, PE_GROUP_AI, kMEFieldType_TextEntry);
	METableAddExprColumn(		pTable,	"AI Command Expression","ExprBlockAICommand",					140, PE_GROUP_AI, NULL);

	METableAddSimpleColumn(pTable, "Activate Time",    "timeActivate",       120, PE_GROUP_TIMING, kMEFieldType_TextEntry);
	METableAddSimpleColumn(pTable, "Override Time",    "timeOverride",       120, PE_GROUP_TIMING, kMEFieldType_TextEntry);
	METableAddSimpleColumn(pTable, "Reactive Power Override Time",    "TimeOverrideReactivePower",       140, PE_GROUP_TIMING, kMEFieldType_TextEntry);
	METableAddSimpleColumn(pTable, "Can Override",     "overrides",          120, PE_GROUP_TIMING, kMEFieldType_BooleanCombo);
	METableAddSimpleColumn(pTable, "Activate Period",  "timeActivatePeriod", 120, PE_GROUP_TIMING, kMEFieldType_TextEntry);
	METableAddSimpleColumn(pTable, "Max Periods",      "periodsMax",         120, PE_GROUP_TIMING, kMEFieldType_TextEntry);
	METableAddSimpleColumn(pTable, "Preactivate Time", "timePreactivate",    120, PE_GROUP_TIMING, kMEFieldType_TextEntry);
	METableAddSimpleColumn(pTable, "Post-Maintain Time", "timePostMaintain", 120, PE_GROUP_TIMING, kMEFieldType_TextEntry);
	METableAddSimpleColumn(pTable, "Auto Reapply",     "autoReapply",        120, PE_GROUP_TIMING, kMEFieldType_BooleanCombo);
	METableAddSimpleColumn(pTable, "Recharge Time",    "timeRecharge",       120, PE_GROUP_TIMING, kMEFieldType_TextEntry);
	METableAddSimpleColumn(pTable, "Recharge Disabled","rechargeDisabled",   120, PE_GROUP_TIMING, kMEFieldType_BooleanCombo);
	METableAddSimpleColumn(pTable, "Recharge Requires Hit","rechargeRequiresHit",120, PE_GROUP_TIMING, kMEFieldType_BooleanCombo);
	METableAddSimpleColumn(pTable, "Recharge Requires Combat","rechargeRequiresCombat",120, PE_GROUP_TIMING, kMEFieldType_BooleanCombo);
	METableAddSimpleColumn(pTable, "Recharge While Offline","rechargeWhileOffline",120, PE_GROUP_TIMING, kMEFieldType_BooleanCombo);
	METableAddSimpleColumn(pTable, "Force Recharge On Interrupt","forceRechargeOnInterrupt",120, PE_GROUP_TIMING, kMEFieldType_BooleanCombo);
	METableAddSimpleColumn(pTable, "Allow Queue Time", "timeAllowQueue",     140, PE_GROUP_TIMING, kMEFieldType_TextEntry);
	METableAddSimpleColumn(pTable, "Always Queue",     "alwaysQueue",        120, PE_GROUP_TIMING, kMEFieldType_BooleanCombo);
	METableAddSimpleColumn(pTable, "GCD Not Checked",  "cooldownGlobalNotChecked", 120, PE_GROUP_TIMING, kMEFieldType_BooleanCombo);
	METableAddSimpleColumn(pTable, "GCD Not Applied",  "cooldownGlobalNotApplied", 120, PE_GROUP_TIMING, kMEFieldType_BooleanCombo);	
	METableAddSimpleColumn(pTable, "Leave Mods",   "deactivationLeavesMods", 120, PE_GROUP_TIMING, kMEFieldType_BooleanCombo);
	METableAddSimpleColumn(pTable, "Leave Disabled Mods",   "deactivationDisablesMods", 120, PE_GROUP_TIMING, kMEFieldType_BooleanCombo);
	METableAddSimpleColumn(pTable, "Mods Expire Without Power",   "modsExpireWithoutPower", 120, PE_GROUP_TIMING, kMEFieldType_BooleanCombo);
	METableAddEnumColumn(pTable,   "Source Enter Combat", "SourceEnterCombat", 120, PE_GROUP_TIMING, kMEFieldType_Combo, PowerEnterCombatTypeEnum);
	METableAddSimpleColumn(pTable, "Disable Target Enter Combat", "DisableTargetEnterCombat", 120, PE_GROUP_TIMING, kMEFieldType_BooleanCombo);
	METableAddSimpleColumn(pTable, "Delay Targeting On Queued", "DelayTargetingOnQueuedActivation", 120, PE_GROUP_TIMING, kMEFieldType_BooleanCombo);
	METableAddSimpleColumn(pTable, "Camera Targeting VecTarget Assist", "UseCameraTargetingVecTargetAssist", 120, PE_GROUP_TIMING, kMEFieldType_BooleanCombo);
	METableAddSimpleColumn(pTable, "Do Not Allow Cancel Before HitFrame", "DoNotAllowCancelBeforeHitFrame", 120, PE_GROUP_TIMING, kMEFieldType_BooleanCombo);
	METableAddSimpleColumn(pTable, "Update Charge Target On Deactivate", "UpdateChargeTargetOnDeactivate", 120, PE_GROUP_TIMING, kMEFieldType_BooleanCombo);

	METableAddSimpleColumn(pTable, "Charges",        "charges",       100, PE_GROUP_USELIMITS, kMEFieldType_TextEntry);
	METableAddSimpleColumn(pTable, "Charge Refill Interval",        "ChargeRefillInterval", 100, PE_GROUP_USELIMITS, kMEFieldType_TextEntry);
	METableAddSimpleColumn(pTable, "Charges Set Cooldown When Empty","chargesSetCooldownWhenEmpty",120, PE_GROUP_USELIMITS, kMEFieldType_BooleanCombo);
	METableAddSimpleColumn(pTable, "Real Lifetime",  "lifetimeReal",  100, PE_GROUP_USELIMITS, kMEFieldType_TextEntry);
	METableAddSimpleColumn(pTable, "Game Lifetime",  "lifetimeGame",  100, PE_GROUP_USELIMITS, kMEFieldType_TextEntry);
	METableAddSimpleColumn(pTable, "Usage Lifetime", "lifetimeUsage", 100, PE_GROUP_USELIMITS, kMEFieldType_TextEntry);

	METableAddSimpleColumn(pTable, "Charge Time",         "timeCharge",         100, PE_GROUP_CHARGE, kMEFieldType_TextEntry);
	METableAddSimpleColumn(pTable, "Charge % Require",    "chargeRequire",      100, PE_GROUP_CHARGE, kMEFieldType_TextEntry);
	METableAddExprColumn(pTable,   "Charge Requires Expr", "ExprBlockRequiresCharge", 140, PE_GROUP_CHARGE, combateval_ContextGet(kCombatEvalContext_Activate));
	METableAddSimpleColumn(pTable, "Charge Indefinite Charging", "ChargeAllowIndefiniteCharging", 100, PE_GROUP_CHARGE, kMEFieldType_BooleanCombo);
	METableAddEnumColumn(pTable,   "Interrupted On",      "interrupts",         140, PE_GROUP_CHARGE, kMEFieldType_FlagCombo, PowerInterruptionEnum);

	METableAddSimpleColumn(pTable, "Cursor Location Target Radius", "cursorLocationTargetRadius", 180, PE_GROUP_UI, kMEFieldType_TextEntry);
	METableAddColumn(pTable,       "Icon",              "iconName",        180, PE_GROUP_UI, kMEFieldType_Texture, NULL, NULL, NULL, NULL, "texture_library/ui/Icons/Powers", NULL, NULL);
	METableAddGlobalDictColumn(pTable, "Power Art File","AnimFX",          150, PE_GROUP_UI, kMEFieldType_ValidatedTextEntry, "PowerAnimFX", "resourceName");
	METableAddEnumColumn(pTable,   "Power Purpose",     "Purpose",         150, PE_GROUP_UI, kMEFieldType_Combo, PowerPurposeEnum);
	METableAddSimpleColumn(pTable, "Do Not AutoSlot",   "DoNotAutoSlot",   120, PE_GROUP_UI, kMEFieldType_BooleanCombo);
	METableAddSimpleColumn(pTable, "Hide In UI",		"HideInUI",		   120, PE_GROUP_UI, kMEFieldType_BooleanCombo);
	METableAddSimpleColumn(pTable, "Hue Override",      "hueOverride",     100, PE_GROUP_UI, kMEFieldType_TextEntry);
	METableAddDictColumn(pTable,   "Emit Override",     "emitOverride",    150, PE_GROUP_UI, kMEFieldType_ValidatedTextEntry, "PowerEmit", parse_PowerEmit, "Name");
	METableAddSimpleColumn(pTable, "Never Show Damage Floats",      "ForceHideDamageFloats",     180, PE_GROUP_UI, kMEFieldType_BooleanCombo);
	METableAddGlobalDictColumn(pTable, "Pre-Activate ApplyPower", "PreActivatePowerDef", 210, PE_GROUP_UI, kMEFieldType_ValidatedTextEntry, "PowerDef", "resourceName");
	METableAddSimpleColumn(pTable, "Auto-Cancel Pre-Activate ApplyPower", "CancelPreActivatePower", 120, PE_GROUP_UI, kMEFieldType_BooleanCombo);
	METableAddSimpleColumn(pTable, "Short Description", ".msgDescription.EditorCopy",     150, PE_GROUP_UI, kMEFieldType_Message);
	METableAddSimpleColumn(pTable, "Long Description",  ".msgDescriptionLong.EditorCopy", 230, PE_GROUP_UI, kMEFieldType_Message);
	METableAddSimpleColumn(pTable, "Flavor Description", ".msgDescriptionFlavor.EditorCopy",     150, PE_GROUP_UI, kMEFieldType_Message);
	METableAddSimpleColumn(pTable, "Rank Change Description", ".msgRankChange.EditorCopy",     100, PE_GROUP_UI, kMEFieldType_Message);

	METableAddSimpleColumn(pTable, "Attribute Autodesc Override", ".msgAttribOverride.EditorCopy",     150, PE_GROUP_UI, kMEFieldType_Message);
	METableAddSimpleColumn(pTable, "AutoDesc Override", ".msgAutoDesc.EditorCopy",     150, PE_GROUP_UI, kMEFieldType_Message);
	METableAddGlobalDictColumn(pTable, "Powerdef for damage tooltip", "TooltipDamagePowerDef", 210, PE_GROUP_UI, kMEFieldType_ValidatedTextEntry, "PowerDef", "resourceName");


	// Keyblock needs to be here but hidden and not parentable in order to ensure it ends up
	// overridden in the inheritance structure.  Keyblocks must be overridden.
	METableAddSimpleColumn(pTable, "KeyBlock", "AttribKeyBlock", 50, NULL, kMEFieldType_TextEntry);
	METableSetColumnState(pTable, "KeyBlock", ME_STATE_HIDDEN | ME_STATE_NOT_PARENTABLE);
}


static void pe_initAttribColumns(METable *pTable)
{
	int id;

	// Create the subtable and get the ID
	peAttribModId = id = METableCreateSubTable(pTable, "Modifier", "AttribMod", parse_AttribModDef, "key",
												pe_orderAttribModDefs, pe_reorderAttribModDefs, pe_createAttribMod);

	METableAddSimpleSubColumn(pTable, id, "AttribMod", NULL, 80, NULL, 0);
	METableSetSubColumnState(pTable, id, "AttribMod", ME_STATE_LABEL);

	// Lock in the menu and label column only
	METableSetNumLockedSubColumns(pTable, id, 2);

	METableAddEnumSubColumn(pTable, id, "Tags",          "@tags@tags", 100, PE_SUBGROUP_MAIN, kMEFieldType_FlagCombo, PowerTagsEnum);
	METableAddEnumSubColumn(pTable, id, "Target",        "target",     100, PE_SUBGROUP_MAIN, kMEFieldType_Combo, ModTargetEnum);
	METableAddEnumSubColumn(pTable, id, "Attribute",     "attrib",     100, PE_SUBGROUP_MAIN, kMEFieldType_ValidatedTextEntry, AttribTypeEnum);
	METableAddEnumSubColumn(pTable, id, "Aspect",        "aspect",     100, PE_SUBGROUP_MAIN, kMEFieldType_Combo, AttribAspectEnum);
	METableAddEnumSubColumn(pTable, id, "Type",          "type",       100, PE_SUBGROUP_MAIN, kMEFieldType_Combo, ModTypeEnum);
	METableAddSimpleSubColumn(pTable, id, "Personal",    "personal",   100, PE_SUBGROUP_MAIN, kMEFieldType_BooleanCombo);
	METableAddSimpleSubColumn(pTable, id, "Linked",    "AttribLinkToSource",   100, PE_SUBGROUP_MAIN, kMEFieldType_BooleanCombo);
		
	METableAddEnumSubColumn(pTable, id, "Stacking",      "stack",      100, PE_SUBGROUP_STACKING, kMEFieldType_Combo, StackTypeEnum);
	METableAddSimpleSubColumn(pTable, id, "Stack Limit", "stackLimit", 100, PE_SUBGROUP_STACKING, kMEFieldType_TextEntry);
	METableAddEnumSubColumn(pTable, id, "Stack Group",   "stackGroup", 100, PE_SUBGROUP_STACKING, kMEFieldType_Combo, ModStackGroupEnum);
	METableAddEnumSubColumn(pTable, id, "Stack Group Pending",   "stackGroupPending", 100, PE_SUBGROUP_STACKING, kMEFieldType_Combo, ModStackGroupEnum);
	METableAddEnumSubColumn(pTable, id, "Stack Entity",  "stackEntity",100, PE_SUBGROUP_STACKING, kMEFieldType_Combo, StackEntityEnum);
	METableAddSimpleSubColumn(pTable, id, "Power Instance Stacking","PowerInstanceStacking",190, PE_SUBGROUP_STACKING, kMEFieldType_BooleanCombo);

	METableAddExprSubColumn(pTable, id, "Duration Expression",  "ExprBlockDuration",  160, PE_SUBGROUP_STRENGTH, combateval_ContextGet(kCombatEvalContext_Apply));
	METableAddExprSubColumn(pTable, id, "Magnitude Expression", "ExprBlockMagnitude", 160, PE_SUBGROUP_STRENGTH, combateval_ContextGet(kCombatEvalContext_Apply));
	METableAddSubColumn(pTable, id,     "Table",                "tableDefault", NULL,  130, PE_SUBGROUP_STRENGTH, kMEFieldType_ValidatedTextEntry, NULL, NULL, NULL, NULL, NULL, NULL, pe_getPowerTableNames);

	// "Resist" and "Strength" handled through sensitivity struct
	METableAddSimpleSubColumn(pTable, id, "Variance",    "variance",    100, PE_SUBGROUP_SENSITIVITY, kMEFieldType_TextEntry);
	METableAddEnumSubColumn(pTable, id,   "Sensitivity", "Sensitivity", 140, PE_SUBGROUP_SENSITIVITY, kMEFieldType_FlagCombo, SensitivityModsEnum);

	METableAddSimpleSubColumn(pTable, id, "Magnitude Is Health",  "@fragility@magnitudeIsHealth",  150, PE_SUBGROUP_FRAGILITY, kMEFieldType_BooleanCombo);
	METableAddExprSubColumn(pTable, id,   "Health Expression",    "@fragility@exprBlockHealth",    150, PE_SUBGROUP_FRAGILITY, combateval_ContextGet(kCombatEvalContext_Apply));
	METableAddSubColumn(pTable, id,       "Health Table",         "@fragility@tableHealth", NULL,         130, PE_SUBGROUP_FRAGILITY, kMEFieldType_ValidatedTextEntry, NULL, NULL, NULL, NULL, NULL, NULL, pe_getPowerTableNames);
	METableAddSimpleSubColumn(pTable, id, "Proportion",           "@fragility@proportion",         120, PE_SUBGROUP_FRAGILITY, kMEFieldType_TextEntry);
	METableAddEnumSubColumn(pTable, id,   "Tags Excluded",        "@fragility@tagsExclude.tags",   100, PE_SUBGROUP_FRAGILITY, kMEFieldType_FlagCombo, PowerTagsEnum);
	METableAddDictSubColumn(pTable, id,   "Scale In",             "@fragility@scaleIn",            120, PE_SUBGROUP_FRAGILITY, kMEFieldType_ValidatedTextEntry, "FragileScaleSet", parse_FragileScaleSet, "name");
	METableAddDictSubColumn(pTable, id,   "Scale Out",            "@fragility@scaleOut",           120, PE_SUBGROUP_FRAGILITY, kMEFieldType_ValidatedTextEntry, "FragileScaleSet", parse_FragileScaleSet, "name");
	METableAddSimpleSubColumn(pTable, id, "Source Only In",       "@fragility@sourceOnlyIn",       140, PE_SUBGROUP_FRAGILITY, kMEFieldType_BooleanCombo);
	METableAddSimpleSubColumn(pTable, id, "Source Only Out",      "@fragility@sourceOnlyOut",      140, PE_SUBGROUP_FRAGILITY, kMEFieldType_BooleanCombo);
	METableAddSimpleSubColumn(pTable, id, "Use Resist In",        "@fragility@useResistIn",        140, PE_SUBGROUP_FRAGILITY, kMEFieldType_BooleanCombo);
	METableAddSimpleSubColumn(pTable, id, "Use Resist Out",       "@fragility@useResistOut",       140, PE_SUBGROUP_FRAGILITY, kMEFieldType_BooleanCombo);
	METableAddSimpleSubColumn(pTable, id, "Fragile While Delayed","@fragility@fragileWhileDelayed",140, PE_SUBGROUP_FRAGILITY, kMEFieldType_BooleanCombo);
	METableAddSimpleSubColumn(pTable, id, "Fragile to Self",      "@fragility@fragileToSameApply", 120, PE_SUBGROUP_FRAGILITY, kMEFieldType_BooleanCombo);
	METableAddSimpleSubColumn(pTable, id, "Unkillable",           "@fragility@unkillable",         140, PE_SUBGROUP_FRAGILITY, kMEFieldType_BooleanCombo);
	METableAddSimpleSubColumn(pTable, id, "Replace Keeps Health", "@fragility@replaceKeepsHealth", 140, PE_SUBGROUP_FRAGILITY, kMEFieldType_BooleanCombo);
	METableAddEnumSubColumn(pTable, id,   "Init Point",           "@fragility@pool.init",          140, PE_SUBGROUP_FRAGILITY, kMEFieldType_Combo, CombatPoolPointEnum);

	METableAddSimpleSubColumn(pTable, id, "Delay",  "delay",                        100, PE_SUBGROUP_TIMING, kMEFieldType_TextEntry);
	METableAddSimpleSubColumn(pTable, id, "Period", "period",                       100, PE_SUBGROUP_TIMING, kMEFieldType_TextEntry);
	METableAddSimpleSubColumn(pTable, id, "Ignore First Tick",  "ignoreFirstTick",  140, PE_SUBGROUP_TIMING, kMEFieldType_BooleanCombo);
	METableAddSimpleSubColumn(pTable, id, "Replace Keeps Timer","replaceKeepsTimer",140, PE_SUBGROUP_TIMING, kMEFieldType_BooleanCombo);
	METableAddSimpleSubColumn(pTable, id, "Duration Includes Offline Time","ProcessOfflineTimeOnLogin",200, PE_SUBGROUP_TIMING, kMEFieldType_BooleanCombo);

	METableAddEnumSubColumn(pTable, id,   "Hit Test",               "hitTest",              100, PE_SUBGROUP_REQUIRES, kMEFieldType_Combo, ModHitTestEnum);
	METableAddExprSubColumn(pTable, id,   "Requires Expression",    "ExprBlockRequires",    160, PE_SUBGROUP_REQUIRES, combateval_ContextGet(kCombatEvalContext_Apply));
	METableAddExprSubColumn(pTable, id,   "Affects Expression",     "ExprBlockAffects",     140, PE_SUBGROUP_REQUIRES, combateval_ContextGet(kCombatEvalContext_Affects));
	METableAddSimpleSubColumn(pTable, id, "ArcAffects",             "arcAffects",           100, PE_SUBGROUP_REQUIRES, kMEFieldType_TextEntry);
	METableAddSimpleSubColumn(pTable, id, "Affects First Application Only", "AffectsOnlyOnFirstModTick",   100, PE_SUBGROUP_REQUIRES, kMEFieldType_BooleanCombo);
	
	METableAddSimpleSubColumn(pTable, id, "Yaw",                    "yaw",                  100, PE_SUBGROUP_REQUIRES, kMEFieldType_TextEntry);
	METableAddExprSubColumn(pTable, id,	  "Chance",                 "ExprChanceBlock",      100, PE_SUBGROUP_REQUIRES, combateval_ContextGet(kCombatEvalContext_Expiration));
	METableAddSimpleSubColumn(pTable, id, "Normalized Chance",      "chanceNormalized",     140, PE_SUBGROUP_REQUIRES, kMEFieldType_BooleanCombo);
	METableAddSimpleSubColumn(pTable, id, "Cancel on Chance",       "cancelOnChance",       140, PE_SUBGROUP_REQUIRES, kMEFieldType_BooleanCombo);
	METableAddSimpleSubColumn(pTable, id, "Keep When Immune",		"keepWhenImmune",       140, PE_SUBGROUP_REQUIRES, kMEFieldType_BooleanCombo);
	METableAddSimpleSubColumn(pTable, id, "Ignore AttribModExpire",	"ignoreAttribModExpire",140, PE_SUBGROUP_REQUIRES, kMEFieldType_BooleanCombo);
	METableAddSimpleSubColumn(pTable, id, "Enhancement Extension",  "enhancementExtension", 190, PE_SUBGROUP_REQUIRES, kMEFieldType_BooleanCombo);
	METableAddSimpleSubColumn(pTable, id, "Survive Target Death",   "surviveTargetDeath",   190, PE_SUBGROUP_REQUIRES, kMEFieldType_BooleanCombo);
	METableAddSimpleSubColumn(pTable, id, "Ignore During PVP",      "ignoredDuringPVP",     190, PE_SUBGROUP_REQUIRES, kMEFieldType_BooleanCombo);
	METableAddSimpleSubColumn(pTable, id, "UI Show Special",	    "UIShowSpecial",		190, PE_SUBGROUP_REQUIRES, kMEFieldType_BooleanCombo);
	METableAddSimpleSubColumn(pTable, id, "Notify Event On Apply",  "NotifyGameEventOnApplication", 190, PE_SUBGROUP_REQUIRES, kMEFieldType_BooleanCombo);

	
	METableAddEnumSubColumn(pTable, id,   "Event Response", "combatEventResponse", 110, PE_SUBGROUP_EVENTS, kMEFieldType_Combo, CombatEventResponseEnum);
	METableAddEnumSubColumn(pTable, id,   "Events",         "combatEvents",        100, PE_SUBGROUP_EVENTS, kMEFieldType_FlagCombo, CombatEventEnum);
	METableAddSimpleSubColumn(pTable, id, "Event Time",     "combatEventTime",     100, PE_SUBGROUP_EVENTS, kMEFieldType_TextEntry);
	METableAddDictSubColumn(pTable, id,   "Expiration Def",     "@expiration@def",     140, PE_SUBGROUP_EVENTS, kMEFieldType_ValidatedTextEntry, "PowerDef", parse_PowerDef, "Name");
	METableAddEnumSubColumn(pTable, id,   "Expiration Target",  "@expiration@target",  100, PE_SUBGROUP_EVENTS, kMEFieldType_Combo, ModExpirationEntityEnum);
	METableAddExprSubColumn(pTable, id,   "Expiration Requires","@expiration@exprBlockRequiresExpire", 150, PE_SUBGROUP_EVENTS, combateval_ContextGet(kCombatEvalContext_Affects));
	METableAddSimpleSubColumn(pTable, id, "Expiration Periodic","@expiration@periodic", 140, PE_SUBGROUP_EVENTS, kMEFieldType_BooleanCombo);

	if (gConf.bNewAnimationSystem)
	{
		METableAddSimpleSubColumn(pTable, id, "[OLD]Continuing Bits",  "continuingBits",  130, PE_SUBGROUP_ART, kMEFieldType_TextEntry);
		//METableAddSimpleSubColumn(pTable, id, "Stance Word",  "StanceWord",  130, PE_SUBGROUP_ART, kMEFieldType_TextEntry);
		METableAddDictSubColumn(pTable, id, "Stance Word", "StanceWord", 130, PE_SUBGROUP_ART, kMEFieldType_ValidatedTextEntry, STANCE_DICTIONARY, parse_DynAnimStanceData, "Name");
		METableAddSimpleSubColumn(pTable, id, "Anim Keyword",  "AnimKeyword",  130, PE_SUBGROUP_ART, kMEFieldType_TextEntry);
	} else {
		METableAddSimpleSubColumn(pTable, id, "Continuing Bits",  "continuingBits",  130, PE_SUBGROUP_ART, kMEFieldType_TextEntry);
	}
	METableAddDictSubColumn(pTable, id,   "Continuing FX",    "continuingFX",    180, PE_SUBGROUP_ART, kMEFieldType_ValidatedTextEntry, "DynFxInfo", parse_DynFxInfo, "InternalName");
	METableAddSimpleSubColumn(pTable, id, "Continuing FX Use Location", "continuingFXAsLocation", 100, PE_SUBGROUP_ART, kMEFieldType_BooleanCombo);
	METableAddSimpleSubColumn(pTable, id, "Conditional Bits", "conditionalBits", 130, PE_SUBGROUP_ART, kMEFieldType_TextEntry);
	METableAddDictSubColumn(pTable, id,   "Conditional FX",   "conditionalFX",   180, PE_SUBGROUP_ART, kMEFieldType_ValidatedTextEntry, "DynFxInfo", parse_DynFxInfo, "InternalName");

	METableAddSimpleSubColumn(pTable, id, "AutoDesc Disabled","autoDescDisabled",       140, PE_SUBGROUP_UI, kMEFieldType_BooleanCombo);
	METableAddSimpleSubColumn(pTable, id, "AutoDesc Message", ".msgAutoDesc.EditorCopy", 150, PE_SUBGROUP_UI, kMEFieldType_Message);
	METableAddSimpleSubColumn(pTable, id, "AutoDesc Key", "autoDescKey", 100, PE_SUBGROUP_UI, kMEFieldType_TextEntry);
	METableAddSimpleSubColumn(pTable, id, "Include In Estimated Damage", "IncludeInEstimatedDamage", 180, PE_SUBGROUP_UI, kMEFieldType_BooleanCombo);
	
	METableAddEnumSubColumn(pTable, id, "Combat Tracker Flags", "Flags", 180, PE_SUBGROUP_UI, kMEFieldType_FlagCombo, CombatTrackerFlagEditorEnum);


	// Don't want to show the "order" column since it's internal use
	METableAddSimpleSubColumn(pTable, id, "Order", "applyPriority", 50, NULL, kMEFieldType_TextEntry);
	METableSetSubColumnState(pTable, id, "Order", ME_STATE_HIDDEN | ME_STATE_NOT_REVERTABLE);
}


static void pe_initComboColumns(METable *pTable)
{
	int id;

	// Create the subtable and get the ID
	peCombosId = id = METableCreateSubTable(pTable, "Combo", "Combo", parse_PowerCombo, "key",
											pe_orderCombos, pe_reorderCombos, pe_createCombo);

	METableAddSimpleSubColumn(pTable, id, "Combo", NULL, 80, NULL, 0);
	METableSetSubColumnState(pTable, id, "Combo", ME_STATE_LABEL);

	// Lock in the menu and label column only
	METableSetNumLockedSubColumns(pTable, id, 2);

	METableAddGlobalDictSubColumn(pTable, id,   "Power",         "power",                       140, PE_SUBGROUP_COMBO, kMEFieldType_ValidatedTextEntry, "PowerDef", "resourceName");
	METableAddSimpleSubColumn(pTable, id, "Charge Time",         "timeChargeRequired",          160, PE_SUBGROUP_COMBO, kMEFieldType_TextEntry);
	METableAddExprSubColumn(pTable, id,   "Requires Expression", "ExprBlockRequires",           140, PE_SUBGROUP_COMBO, combateval_ContextGet(kCombatEvalContext_Activate));
	METableAddEnumSubColumn(pTable, id,	  "Modes Required", "ModeRequire",						100, PE_SUBGROUP_COMBO, kMEFieldType_FlagCombo, PowerModeEnum);
	METableAddEnumSubColumn(pTable, id,	  "Modes Excluded", "ModeExclude",						100, PE_SUBGROUP_COMBO, kMEFieldType_FlagCombo, PowerModeEnum);
	METableAddExprSubColumn(pTable, id,   "Client Target Expression", "ExprBlockTargetClient",  140, PE_SUBGROUP_COMBO, combateval_ContextGet(kCombatEvalContext_Activate));

	// Don't want to show the "order" column since it's internal use
	METableAddSimpleSubColumn(pTable, id, "Order", "ComboOrder", 50, NULL, kMEFieldType_TextEntry);
	METableSetSubColumnState(pTable, id, "Order", ME_STATE_HIDDEN | ME_STATE_NOT_REVERTABLE);
}


static void pe_initDynamicColumns(METable *pTable)
{
	ParseTable *pParseTable;
	AttribTypeInfo *pTypeInfo;
	char **eaKeys = NULL;
	int* eaValues = NULL;
	int i,j;
	int id = peAttribModId;
	char groupBuf[260];
	char parseBuf[260];
	char nameBuf[260];

	DefineFillAllKeysAndValues(AttribTypeEnum, &eaKeys, &eaValues);

	for(i=0; i<eaSize(&eaKeys); ++i) {
		pParseTable = characterattribs_GetSpecialParseTable(eaValues[i]);
		if (pParseTable) {
			// Create the type info for this special type
			sprintf(groupBuf, "Mod %s", eaKeys[i]);
			pTypeInfo = (AttribTypeInfo*)calloc(1, sizeof(AttribTypeInfo));
			pTypeInfo->eType = eaValues[i];
			pTypeInfo->pcColGroup = strdup(groupBuf);
			pTypeInfo->pParseTable = pParseTable;
			eaPush(&eaTypeInfos, pTypeInfo);

			// Create the columns
			FORALL_PARSETABLE(pParseTable,j) {
				MEFieldType eType;
				StaticDefineInt *pEnum;
				char *pDictName, *pcDictNamePTName, *pGlobalDictName;
				ParseTable *pDictParseTable;
				ExprContext *pExprContext = combateval_ContextGet(kCombatEvalContext_Apply);

				if (!stricmp(pParseTable[j].name,"{") || !stricmp(pParseTable[j].name,"}")
					|| !stricmp(pParseTable[j].name,"Type")
					|| pParseTable[j].type & TOK_PARSETABLE_INFO
					|| pParseTable[j].type & TOK_REDUNDANTNAME) {
					continue;
				}
				sprintf(nameBuf, "%s %s", eaKeys[i], pParseTable[j].name);
				sprintf(parseBuf, "@params@%s", pParseTable[j].name);

				// Try to figure out what field to make from this parse table entry
				if (MEFieldGetParseTableInfo(pParseTable, j, &eType, &pDictName, &pDictParseTable, &pcDictNamePTName, &pGlobalDictName, &pEnum)) {
					// Special hack
					if(strstr(pParseTable[j].name,"FX"))
					{
						pDictName = "DynFxInfo";
						pDictParseTable = parse_DynFxInfo;
						pcDictNamePTName = "InternalName";
					}

					if(stricmp(pParseTable[0].name,"AttribModShareParams") == 0) {
						pExprContext = combateval_ContextGet(kCombatEvalContext_Affects);
					}
					else if(stricmp(pParseTable[0].name,"AICommandParams") == 0) {
						pExprContext = NULL;
					} 
					else if(stricmp(pParseTable[0].name,"TeleportParams") == 0) {
						pExprContext = combateval_ContextGet(kCombatEvalContext_Teleport);
					}


					// Create the column
					METableAddSubColumn(pTable, id, strdup(nameBuf), strdup(parseBuf), pParseTable, 120, pTypeInfo->pcColGroup,
									eType, pEnum, pExprContext, pDictName, pDictParseTable, pcDictNamePTName, pGlobalDictName, NULL);
				}
			}
		}
	}

	eaDestroy(&eaKeys);
	eaiDestroy(&eaValues);
}


void pe_ExportOkayClicked(CSVConfig *pCSVConfig)
{
	pCSVConfig->pchDictionary = StructAllocString("PowerDef");
	pCSVConfig->pchScopeColumnName = StructAllocString("Group");
	pCSVConfig->pchStructName = StructAllocString("PowerDef");
}

AUTO_COMMAND ACMD_NAME("pe_Export");
void pe_Export()
{
	static CSVConfigWindow *pConfigWindow = NULL;

	if(pConfigWindow == NULL)
	{
		pConfigWindow = (CSVConfigWindow*)malloc(sizeof(CSVConfigWindow));
		pConfigWindow->pWindow = ui_WindowCreate("CSV Export Config", 150, 200,450,300);
		pConfigWindow->eDefaultExportColumns = kColumns_Visible;
		pConfigWindow->eDefaultExportType = kCSVExport_Open;
		pConfigWindow->pchBaseFilename = StructAllocString("PowersExport");
		initCSVConfigWindow(pConfigWindow);
	}

	//Populate the datas
	setupCSVConfigWindow(pConfigWindow, peWindow, pe_ExportOkayClicked);

	ui_WindowSetModal(pConfigWindow->pWindow, true);
	ui_WindowShow(pConfigWindow->pWindow);
}


//Copies the current open powers, and visible columns to the clipboard.  If there are and selected cells, it will instead only copy the
// fully-selected columns
void pe_CopyToClipboard()
{
	char *copyString = NULL;
	

	char **eaPowerDefList = NULL;
	CSVColumn **eaColumns = NULL;

	CSVExportType eExportType = kCSVExport_Open;
	ColumnsExport eColumns = kColumns_Visible;
	
	int rowIdx, colIdx;
	const int numCols = eaSize(&peWindow->pTable->eaCols);
	const int numRows = eaSize(&peWindow->pTable->eaRows);

	for(rowIdx = 0; rowIdx < numRows; rowIdx++)
	{
		for(colIdx=0; colIdx<numCols; ++colIdx)
		{
			if(ui_ListIsSelected(peWindow->pTable->pList, peWindow->pTable->eaCols[colIdx]->pListColumn, rowIdx))
			{
				eColumns = kColumns_Selected;
				break;
			}
		}
		if(eColumns == kColumns_Selected)
			break;
	}

	csvExportSetup(	peWindow,
					&eaPowerDefList, 
					&eaColumns,
					eExportType,
					eColumns);

	estrStackCreate(&copyString);


	{
		S32 i;
		PowerDef *pDef;

		referent_CSVHeader(eaColumns, &copyString, false);

		for(i = 0; i < eaSize(&eaPowerDefList); i++)
		{
			pDef = RefSystem_ReferentFromString(g_hPowerDefDict, eaPowerDefList[i]);
			if(pDef)
			{
				referent_CSV(pDef, parse_PowerDef, eaColumns, &copyString, false);
			}
		}
	}

	if(estrLength(&copyString))
	{
		winCopyUTF8ToClipboard(copyString);
	}
	
	estrDestroy(&copyString);
	eaDestroy(&eaPowerDefList);
	eaDestroy(&eaColumns);
}

//GenerateItemPowers functions

void peGenerateItempowerOkayClicked(UIButton *pButton, UserData data)
{
	PEGenerateItempowerWindow *pItempowerWindow = (PEGenerateItempowerWindow*)data;
	PowerDef** eaPowers = NULL;
	ResourceInfo** ppResInfos;

	ppResInfos = resDictGetInfo(g_hItemPowerDict)->ppInfos;
	METableGetAllObjectsWithSelectedFields(peWindow->pTable, &eaPowers);
	FOR_EACH_IN_EARRAY_FORWARDS(eaPowers, PowerDef, pPower)
		char buffer[512];
		sprintf(buffer, "%s%s%s", ui_TextEntryGetText(pItempowerWindow->pPrefixEntry), pPower->pchName, ui_TextEntryGetText(pItempowerWindow->pSuffixEntry));
		itemPowerEditorEMNewDocFromPowerDef(NULL, pPower, buffer, ui_TextEntryGetText(pItempowerWindow->pScopeEntry));
	FOR_EACH_END

	ui_WindowClose(pItempowerWindow->pWindow);
}

void peGenerateItempowerCancelClicked(UIButton *pButton, UserData data)
{
	ui_WindowClose(((PEGenerateItempowerWindow*)data)->pWindow);
}

void pe_GenerateItemPowerFilenameChangedCallback(UIAnyWidget* pWidget, UserData pData)
{
	char pBuffer[512];
	sprintf(pBuffer, "Name Preview: %s<POWER_NAME>%s", ui_TextEntryGetText(((PEGenerateItempowerWindow*)pData)->pPrefixEntry), ui_TextEntryGetText(((PEGenerateItempowerWindow*)pData)->pSuffixEntry));
	ui_LabelSetText(((PEGenerateItempowerWindow*)pData)->pFilenamePreview, pBuffer);
}
//Puts in all the UI elements
void initGenerateItempowerWindow(PEGenerateItempowerWindow *pItempowerWindow)
{
	F32 y = 0;
	F32 x = 0;

	UITextEntry *pScope, *pFilenamePrefix, *pFilenameSuffix;
	UIButton *pOkayButton;
	UIButton *pCancelButton;
	UILabel *pScopeLabel, *pPrefixLabel, *pSuffixLabel, *pPreviewLabel;

	F32 fBorderWidth = 8;

	resGetUniqueScopes(g_hItemPowerDict, &geaItempowerScopes);

	//START scope entry
	y+= 10;
	pScopeLabel = ui_LabelCreate("Scope:", x, y);
	ui_WidgetSetWidthEx(UI_WIDGET(pScopeLabel), 0.12, UIUnitPercentage);
	ui_WindowAddChild(pItempowerWindow->pWindow, pScopeLabel);

	pScope = ui_TextEntryCreateWithStringCombo("", 0, 0, &geaItempowerScopes, true, true, false, true);
	ui_WidgetSetPositionEx(UI_WIDGET(pScope), 0, y, 0.12, 0, UITopLeft);
	ui_WidgetSetWidthEx(UI_WIDGET(pScope), 0.88, UIUnitPercentage);
	ui_WindowAddChild(pItempowerWindow->pWindow, pScope);
	x=0; y+= UI_WIDGET(pScope)->height + 5;
	pItempowerWindow->pScopeEntry = pScope;
	//END scope entry

	//START prefix/suffix entry
	y+= 10;
	pPrefixLabel = ui_LabelCreate("Name Prefix:", x, y);
	ui_WidgetSetWidthEx(UI_WIDGET(pPrefixLabel), 0.25, UIUnitPercentage);
	ui_WindowAddChild(pItempowerWindow->pWindow, pPrefixLabel);

	pFilenamePrefix = ui_TextEntryCreate("", 0, 0);
	ui_TextEntrySetChangedCallback(pFilenamePrefix, pe_GenerateItemPowerFilenameChangedCallback, pItempowerWindow);
	ui_WidgetSetPositionEx(UI_WIDGET(pFilenamePrefix), 0, y, 0.25, 0, UITopLeft);
	ui_WidgetSetWidthEx(UI_WIDGET(pFilenamePrefix), 0.4, UIUnitPercentage);
	ui_WindowAddChild(pItempowerWindow->pWindow, pFilenamePrefix);
	x=0; y+= UI_WIDGET(pFilenamePrefix)->height + 5;
	pItempowerWindow->pPrefixEntry = pFilenamePrefix;

	y+= 10;
	pSuffixLabel = ui_LabelCreate("Name Suffix:", x, y);
	ui_WidgetSetWidthEx(UI_WIDGET(pSuffixLabel), 0.25, UIUnitPercentage);
	ui_WindowAddChild(pItempowerWindow->pWindow, pSuffixLabel);

	pFilenameSuffix = ui_TextEntryCreate("", 0, 0);
	ui_TextEntrySetChangedCallback(pFilenameSuffix, pe_GenerateItemPowerFilenameChangedCallback, pItempowerWindow);
	ui_WidgetSetPositionEx(UI_WIDGET(pFilenameSuffix), 0, y, 0.25, 0, UITopLeft);
	ui_WidgetSetWidthEx(UI_WIDGET(pFilenameSuffix), 0.4, UIUnitPercentage);
	ui_WindowAddChild(pItempowerWindow->pWindow, pFilenameSuffix);
	x=0; y+= UI_WIDGET(pFilenameSuffix)->height + 5;
	pItempowerWindow->pSuffixEntry = pFilenameSuffix;

	y+= 10;
	pPreviewLabel = ui_LabelCreate("Name Preview:    <POWER_NAME>", x, y);
	ui_WidgetSetWidthEx(UI_WIDGET(pPreviewLabel), 0.25, UIUnitPercentage);
	ui_WindowAddChild(pItempowerWindow->pWindow, pPreviewLabel);
	pItempowerWindow->pFilenamePreview = pPreviewLabel;
	x=0; y+= UI_WIDGET(pPreviewLabel)->height + 5;
	//END prefix/suffix entry

	x=0; y+= 3;


	//Add the okay/cancel buttons
	pCancelButton = ui_ButtonCreate("Cancel", 0, 0, peGenerateItempowerCancelClicked, pItempowerWindow);
	ui_WidgetSetPositionEx(UI_WIDGET(pCancelButton), 0, 0, 0, 0,UIBottomRight);
	ui_WindowAddChild(pItempowerWindow->pWindow, pCancelButton);
	pOkayButton = ui_ButtonCreate("Okay", 0, 0, peGenerateItempowerOkayClicked, pItempowerWindow);
	ui_WidgetSetPositionEx(UI_WIDGET(pOkayButton), pCancelButton->widget.width+2, 0, 0, 0,UIBottomRight);
	ui_WindowAddChild(pItempowerWindow->pWindow, pOkayButton);
}

void setupGenerateItempowerWindow(PEGenerateItempowerWindow *pItempowerWindow, MEWindow *pMEWindow)
{
	pItempowerWindow->pMEWindow = pMEWindow;

}

void pe_ShowGenerateItemPowersWindow()
{	
	static PEGenerateItempowerWindow *pItempowerWindow = NULL;
	EMEditorDoc *pDoc = (EMEditorDoc*)emGetActiveEditorDoc();
	if(pItempowerWindow == NULL)
	{
		pItempowerWindow = (PEGenerateItempowerWindow*)malloc(sizeof(PEGenerateItempowerWindow));
		pItempowerWindow->pWindow = ui_WindowCreate("Generate Itempower Configuration", 150, 200,400,200);
		initGenerateItempowerWindow(pItempowerWindow);
	}

	//Populate the data
	setupGenerateItempowerWindow(pItempowerWindow, peWindow);

	ui_WindowSetModal(pItempowerWindow->pWindow, true);
	ui_WindowShow(pItempowerWindow->pWindow);
}

bool pe_ItemPowerWarningCB(UIDialog *pDialog, UIDialogButton eButton, UserData pResponseData)
{
	pe_ShowGenerateItemPowersWindow();

	return true;
}

AUTO_COMMAND ACMD_NAME("pe_GenerateItemPowers");
 void pe_GenerateItemPowers()
 {
	 PowerDef** eaPowers = NULL;
	 ResourceInfo** ppResInfos;
	 bool duplicate = false;
	 char* estrWarning = NULL;

	 estrAppend2(&estrWarning, "WARNING: The following powers you have selected already have itempowers which appear to match. Please double-check that you aren't creating unnecessary duplicates before generating these itempowers: \n\n");
	 ppResInfos = resDictGetInfo(g_hItemPowerDict)->ppInfos;
	 METableGetAllObjectsWithSelectedFields(peWindow->pTable, &eaPowers);
	 FOR_EACH_IN_EARRAY_FORWARDS(eaPowers, PowerDef, pPower)
	 	FOR_EACH_IN_EARRAY(ppResInfos, ResourceInfo, pInfo)
	 		if (strstr(pInfo->resourceName, pPower->pchName))
	 		{	
				estrAppend2(&estrWarning, pPower->pchName);
				estrAppend2(&estrWarning, "\n");
	 			duplicate = true;
	 		}
	 	FOR_EACH_END
	FOR_EACH_END

	if (duplicate) 
		ui_WindowShow(UI_WINDOW(ui_DialogCreate("Avast! Data bloat off the port bow!", estrWarning, pe_ItemPowerWarningCB, NULL)));
	else
		pe_ShowGenerateItemPowersWindow();
}

void pe_AddPower(METable *pTable, PowerDef *pPowerDef, void *pUnused)
{
	globCmdParsef("ec me AddPower %s", pPowerDef->pchName);
}

static void pe_init(MultiEditEMDoc *pEditorDoc)
{
	if (!peWindow) {
		char buf[256];
		// Create the editor window
		peWindow = MEWindowCreate("Powers Editor", "Power", "Powers", SEARCH_TYPE_POWER, g_hPowerDefDict, parse_PowerDef, "name", "file", "group", pEditorDoc);

		sprintf(buf, "Export %s", peWindow->pcDisplayNamePlural);
		emMenuItemCreate(peWindow->pEditorDoc->emDoc.editor, "pe_export", buf, NULL, NULL, "pe_Export");
		
		emMenuItemCreate(peWindow->pEditorDoc->emDoc.editor, "pe_generateitempowers", "Generate Itempowers", NULL, NULL, "pe_GenerateItempowers");
		
		emMenuRegister(peWindow->pEditorDoc->emDoc.editor, emMenuCreate(peWindow->pEditorDoc->emDoc.editor, "Tools", "pe_export", "pe_generateitempowers", NULL));

		METableAddCustomAction(peWindow->pTable, "Add Power", pe_AddPower, NULL);

		// Add power-specific columns
		pe_initPowerColumns(peWindow->pTable);

		// Add power-specific sub-columns
		pe_initAttribColumns(peWindow->pTable);
		pe_initComboColumns(peWindow->pTable);
		pe_initDynamicColumns(peWindow->pTable);
		METableFinishColumns(peWindow->pTable);

		// Init the menus after adding all the columns
		MEWindowInitTableMenus(peWindow);

		// Set the callbacks
		pe_initCallbacks(peWindow, peWindow->pTable);

		aiRequestEditingData();
	}

	// Show the window
	ui_WindowPresent(peWindow->pUIWindow);
}


//---------------------------------------------------------------------------------------------------
// Public Interface
//---------------------------------------------------------------------------------------------------

MEWindow *powersEditor_init(MultiEditEMDoc *pEditorDoc) 
{
	pe_init(pEditorDoc);

	return peWindow;
}


void powersEditor_createPower(char *pcName)
{
	// Create a new object since it is not in the dictionary
	// Add the object as a new object with no old
	void *pObject = pe_createObject(peWindow->pTable, NULL, pcName, false);
	METableAddRowByObject(peWindow->pTable, pObject, 1, 1);
}

#endif

