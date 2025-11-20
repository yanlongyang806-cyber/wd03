 //
// CritterGroupEditor.c
//

#ifndef NO_EDITORS

#include "cmdparse.h"
#include "entCritter.h"
#include "MultiEditTable.h"
#include "MultiEditWindow.h"
#include "Powers.h"
#include "ResourceSearch.h"
#include "stringcache.h"
#include "rand.h"
#include "ChatBubbles.h"

#include "ItemCommon.h"
#include "itemCommon_h_ast.h"
#include "ItemEnums.h"
#include "itemEnums_h_ast.h"

#include "AutoGen/entCritter_h_ast.h"
#include "AutoGen/ChatBubbles_h_ast.h"

#include "soundLib.h"

#define CGE_GROUP_NONE        NULL
#define CGE_GROUP_MAIN        "Main"

#define CGE_SUBGROUP_NONE     NULL
#define CGE_SUBGROUP_VAR	  "Var"

static int s_CritterVarConfigId;
static int s_CritterLoreConfigId;

// This magic line is required if you run in Full Debug to avoid assert
AUTO_RUN_ANON(memBudgetAddMapping(__FILE__, BUDGET_Editors););


//-----------------------------------------------------------------------------------
// Global Data
//-----------------------------------------------------------------------------------

static MEWindow *cgeWindow = NULL;



//---------------------------------------------------------------------------------------------------
// Callbacks
//---------------------------------------------------------------------------------------------------


static int cge_compareStrings(const char** left, const char** right)
{
	return stricmp(*left,*right);
}

static int cge_compareVars(const CritterVar **var1, const CritterVar **var2)
{
	if ((*var1)->fOrder > (*var2)->fOrder) {
		return 1;
	} else if ((*var1)->fOrder < (*var2)->fOrder) {
		return -1;
	}
	return 0;
}

static int cge_compareLore(const CritterLore **lore1, const CritterLore **lore2)
{
	if ((*lore1)->fOrder > (*lore2)->fOrder) {
		return 1;
	} else if ((*lore1)->fOrder < (*lore2)->fOrder) {
		return -1;
	}
	return 0;
}

static void cge_orderVars(METable *pTable, void ***peaOrigVars, void ***peaOrderedVars)
{
	int i, count;

	// Copy data to the ordered list
	count = eaSize(peaOrigVars);
	for(i=0; i<count; ++i) {
		eaPush(peaOrderedVars,(*peaOrigVars)[i]);
	}

	// QSort the list
	eaQSort(*peaOrderedVars,cge_compareVars);
}

static void cge_orderLore(METable *pTable, void ***peaOrigLore, void ***peaOrderedLore)
{
	int i, count;

	// Copy data to the ordered list
	count = eaSize(peaOrigLore);
	for(i=0; i<count; ++i) {
		eaPush(peaOrderedLore,(*peaOrigLore)[i]);
	}

	// QSort the list
	eaQSort(*peaOrderedLore,cge_compareLore);
}


// Returns 1 if reordering succeeds and 0 if not
static int cge_reorderVars(METable *pTable, CritterGroup *pCritterGroup, void ***peaVars, int pos1, int pos2)
{
	CritterVar **eaVars = (CritterVar**)*peaVars;
	CritterVar *pTempVar;
	float fTempOrder;

	// Swap so pos1 is always less than pos2
	if (pos1 > pos2) {
		int temp = pos1;
		pos1 = pos2;
		pos2 = temp;
	}
	if (pos1 != pos2 - 1) {
		// This code only supports shifting by one row right now
		// but it could be expanded to support multirow shifting later
		assertmsg(0,"Critter group editor cannot reorder subrows by more than one row at a time");
	}

	// Not inheriting, so simply swap priority
	fTempOrder = eaVars[pos1]->fOrder;
	eaVars[pos1]->fOrder = eaVars[pos2]->fOrder;
	eaVars[pos2]->fOrder = fTempOrder;

	// Swap position
	pTempVar = eaVars[pos1];
	eaVars[pos1] = eaVars[pos2];
	eaVars[pos2] = pTempVar;

	return 1;
}

// Returns 1 if reordering succeeds and 0 if not
static int cge_reorderLore(METable *pTable, CritterGroup *pCritterGroup, void ***peaLore, int pos1, int pos2)
{
	CritterLore **eaLore = (CritterLore**)*peaLore;
	CritterLore *pTempLore;
	float fTempOrder;

	// Swap so pos1 is always less than pos2
	if (pos1 > pos2) {
		int temp = pos1;
		pos1 = pos2;
		pos2 = temp;
	}
	if (pos1 != pos2 - 1) {
		// This code only supports shifting by one row right now
		// but it could be expanded to support multirow shifting later
		assertmsg(0,"Critter group editor cannot reorder subrows by more than one row at a time");
	}

	// Not inheriting, so simply swap priority
	fTempOrder = eaLore[pos1]->fOrder;
	eaLore[pos1]->fOrder = eaLore[pos2]->fOrder;
	eaLore[pos2]->fOrder = fTempOrder;

	// Swap position
	pTempLore = eaLore[pos1];
	eaLore[pos1] = eaLore[pos2];
	eaLore[pos2] = pTempLore;

	return 1;
}

int crittergroup_GetNextVarKey(CritterGroup *pCritterGroup)
{
	int iKey;
	int ii;
	int numVars = eaSize(&pCritterGroup->ppCritterVars);

	for(;;)
	{
		iKey = randomInt();

		for(ii=0;ii<numVars;ii++)
		{
			if (pCritterGroup->ppCritterVars[ii]->iKey == iKey)
				break;
		}
		if (ii >= numVars)
			break;
	}

	return iKey;
}

int crittergroup_GetNextLoreKey(CritterGroup *pCritterGroup)
{
	int iKey;
	int ii;
	int numLore = eaSize(&pCritterGroup->ppCritterLoreEntries);

	for(;;)
	{
		iKey = randomInt();

		for(ii=0;ii<numLore;ii++)
		{
			if (pCritterGroup->ppCritterLoreEntries[ii]->iKey == iKey)
				break;
		}
		if (ii >= numLore)
			break;
	}

	return iKey;
}

static void *cge_CreateCritterVar(METable *pTable, CritterGroup *pCritterGroup, CritterVar *pClone, CritterVar *pBefore, CritterVar *pAfter)
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
	pNew->iKey = crittergroup_GetNextVarKey(pCritterGroup);

	return pNew;
}


static void *cge_CreateCritterLore(METable *pTable, CritterGroup *pCritterGroup, CritterLore *pClone, CritterLore *pBefore, CritterLore *pAfter)
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
	pNew->iKey = crittergroup_GetNextLoreKey(pCritterGroup);

	return pNew;
}

static int cge_validateCallback(METable *pTable, CritterGroup *pCritterGroup, void *pUserData)
{
	char buf[1024];

	if (pCritterGroup->pchName[0] == '_') {
		sprintf(buf, "The CritterGroup '%s' cannot have a name starting with an underscore.", pCritterGroup->pchName);
		ui_DialogPopup("Validation Error", buf);
		return 0;
	}


	
	//return crittergroup_Validate(pCritterGroup);
	return 1;
}

static void cge_fixMessages(CritterGroup *pCritterGroup)
{
	char buf[1024];
	int ii;


	// Fix up name
	{
		if (!pCritterGroup->displayNameMsg.pEditorCopy) {
			pCritterGroup->displayNameMsg.pEditorCopy = StructCreate(parse_Message);
		}

		sprintf(buf, "CritterGroup.%s", pCritterGroup->pchName);
		langFixupMessage(pCritterGroup->displayNameMsg.pEditorCopy, buf, "Display name for a critter group definition", "CritterGroup");
	}

	// Fix description
	{
		if (!pCritterGroup->descriptionMsg.pEditorCopy) {
			pCritterGroup->descriptionMsg.pEditorCopy = StructCreate(parse_Message);
		}

		// Set up key if not exactly
		sprintf(buf, "CritterGroup.%s.description", pCritterGroup->pchName);
		langFixupMessage(pCritterGroup->descriptionMsg.pEditorCopy, buf, "Description for a critter group definition", "CritterGroup");
	}

	for(ii=0; ii<eaSize(&pCritterGroup->ppCritterVars); ii++)
	{
		//fixup message
		if (!pCritterGroup->ppCritterVars[ii]->var.messageVal.pEditorCopy) 
		{
			pCritterGroup->ppCritterVars[ii]->var.messageVal.pEditorCopy = StructCreate(parse_Message);
		}

		// Set up key if not exactly
		sprintf(buf, "CritterGroup.%s.Var.%s.Msg", pCritterGroup->pchName, pCritterGroup->ppCritterVars[ii]->var.pcName);
		langFixupMessage(pCritterGroup->ppCritterVars[ii]->var.messageVal.pEditorCopy, buf, "Msg text for a critter var", "CritterGroup");
	}
}


static void cge_postOpenCallback(METable *pTable, CritterGroup *pCritterGroup, CritterGroup *pOrigCritterGroup)
{
	cge_fixMessages(pCritterGroup);
	if (pOrigCritterGroup) {
		cge_fixMessages(pOrigCritterGroup);
	}
}


static void cge_preSaveCallback(METable *pTable, CritterGroup *pCritterGroup)
{
	// Fix up display name
	cge_fixMessages(pCritterGroup);
}

static void *cge_createObject(METable *pTable, CritterGroup *pObjectToClone, char *pcNewName, bool bCloneKeepsKeys)
{
	CritterGroup *pNewDef = NULL;
	const char *pcBaseName;
	const char *pcBaseDisplayName = NULL;
	const char *pcBaseDescription = NULL;
	Message *pMessage = NULL;
	char *tmpS = NULL;

	estrStackCreate(&tmpS);

	// Create the object
	if (pObjectToClone) 
	{
		pNewDef = StructClone(parse_CritterGroup, pObjectToClone);
		pcBaseName = pObjectToClone->pchName;
		pcBaseDisplayName = pObjectToClone->displayNameMsg.pEditorCopy ? pObjectToClone->displayNameMsg.pEditorCopy->pcDefaultString : NULL;
		pcBaseDescription = pObjectToClone->descriptionMsg.pEditorCopy ? pObjectToClone->descriptionMsg.pEditorCopy->pcDefaultString : NULL;
	} 
	else 
	{
		pNewDef = StructCreate(parse_CritterGroup);

		pcBaseName = "_New_CritterGroup";
		pcBaseDisplayName = "New CritterGroup";
	}
	// Use provided name if available
	if (pcNewName) {
		pcBaseName = pcNewName;
		pcBaseDisplayName = pcNewName;
	}

	assertmsg(pNewDef, "Failed to create CritterGroup");

	// Assign a new name
	pNewDef->pchName = (char*)METableMakeNewNameShared(pTable, pcBaseName, true);

	//make all the messages
	langMakeEditorCopy(parse_CritterGroup, pNewDef, true);
	pNewDef->displayNameMsg.pEditorCopy->pcDefaultString = StructAllocString(pcBaseDisplayName);

	// Assign a file
	estrPrintf(&tmpS, "defs/crittergroups/%s.crittergroup",pNewDef->pchName);
	pNewDef->pchFileName = (char*)allocAddString(tmpS);

	estrDestroy(&tmpS);

	return pNewDef;
}


static void *cge_tableCreateCallback(METable *pTable, CritterGroup *pObjectToClone, bool bCloneKeepsKeys)
{
	return cge_createObject(pTable, pObjectToClone, NULL, bCloneKeepsKeys);
}


static void *cge_windowCreateCallback(MEWindow *pWindow, CritterGroup *pObjectToClone)
{
	return cge_createObject(pWindow->pTable, pObjectToClone, NULL, false);
}


static void cge_dictChangeCallback(enumResourceEventType eType, const char *pDictName, ConstReferenceData pRefData, Referent pReferent, METable *pTable)
{
	METableDictChanged(pTable, eType, pReferent, pRefData);
}


static void cge_messageDictChangeCallback(enumResourceEventType eType, const char *pDictName, const char *pcMessageKey, Referent pReferent, METable *pTable)
{
	if ((eType == RESEVENT_RESOURCE_MODIFIED) ||
		(eType == RESEVENT_RESOURCE_REMOVED) ||
		(eType == RESEVENT_RESOURCE_ADDED)) {

		METableMessageChangedRefresh(pTable, pcMessageKey);
	}
}

static char** cge_getVoiceSetNames(METable *pTable, void *pUnused)
{
	char **eaResult = NULL;

	sndGetGroupNamesFromProject("Nemtact", &eaResult);

	return eaResult;
}

static void cge_VarTypeChangeCallback(METable *pTable, CritterGroup *pCritterGroup, CritterVar *pVar, void *pUserData, bool bInitNotify)
{
	METableSetSubFieldNotApplicable(pTable, pCritterGroup, s_CritterVarConfigId, pVar, "Int Val", (pVar->var.eType != WVAR_INT));
	METableSetSubFieldNotApplicable(pTable, pCritterGroup, s_CritterVarConfigId, pVar, "Float Val", (pVar->var.eType != WVAR_FLOAT));
	METableSetSubFieldNotApplicable(pTable, pCritterGroup, s_CritterVarConfigId, pVar, "String", ((pVar->var.eType != WVAR_STRING) && (pVar->var.eType != WVAR_LOCATION_STRING)));
	METableSetSubFieldNotApplicable(pTable, pCritterGroup, s_CritterVarConfigId, pVar, "Message", (pVar->var.eType != WVAR_MESSAGE));
	METableSetSubFieldNotApplicable(pTable, pCritterGroup, s_CritterVarConfigId, pVar, "Animation", (pVar->var.eType != WVAR_ANIMATION));
	METableSetSubFieldNotApplicable(pTable, pCritterGroup, s_CritterVarConfigId, pVar, "Critter Def", (pVar->var.eType != WVAR_CRITTER_DEF));
	METableSetSubFieldNotApplicable(pTable, pCritterGroup, s_CritterVarConfigId, pVar, "Critter Group", (pVar->var.eType != WVAR_CRITTER_GROUP));
}

static void cge_initCallbacks(MEWindow *pWindow, METable *pTable)
{
	// General Window callbacks
	MEWindowSetCreateCallback(pWindow, cge_windowCreateCallback);

	// General table callbacks
	METableSetValidateCallback(pTable, cge_validateCallback, pTable);
	METableSetPostOpenCallback(pTable, cge_postOpenCallback);
	METableSetPreSaveCallback(pTable, cge_preSaveCallback);
	METableSetCreateCallback(pTable, cge_tableCreateCallback);
	METableSetPreSaveCallback(pTable, cge_preSaveCallback);

	METableSetSubColumnChangeCallback(pTable, s_CritterVarConfigId, "Type", cge_VarTypeChangeCallback, NULL);

	// We need this registered here instead of by METable because the dictionary will 
	// only allow each callback function to be registered once and there may be multiple
	// METable instances.  Our local callback just passes through to the METable.
	resDictRegisterEventCallback(g_hCritterGroupDict, cge_dictChangeCallback, pTable);
	resDictRegisterEventCallback(gMessageDict, cge_messageDictChangeCallback, pTable);
}


//---------------------------------------------------------------------------------------------------
// UI Init
//---------------------------------------------------------------------------------------------------


static void cge_initCritterGroupColumns(METable *pTable)
{
	METableAddSimpleColumn(pTable, "Name", "name", 150, NULL, kMEFieldType_TextEntry);

	// Lock in name column
	METableSetNumLockedColumns(pTable, 2);

	METableAddScopeColumn(pTable,    "Scope",        "Scope",       160, CGE_GROUP_MAIN, kMEFieldType_TextEntry); // Not validated on purpose
	METableAddFileNameColumn(pTable, "File Name",    "fileName",    210, CGE_GROUP_MAIN, NULL, "defs/crittergroups", "defs/crittergroups", ".crittergroup", UIBrowseNewOrExisting);
	METableAddSimpleColumn(pTable,   "Notes",        "Notes",       160, CGE_GROUP_MAIN, kMEFieldType_MultiText);

	METableAddSimpleColumn(pTable,   "Display Name", ".DisplayNameMsg.EditorCopy",   160, CGE_GROUP_MAIN, kMEFieldType_Message);
	METableAddSimpleColumn(pTable,   "Description",  ".DescriptionMsg.EditorCopy",   160, CGE_GROUP_MAIN, kMEFieldType_Message);
	METableAddColumn(pTable,         "Icon",         "Icon",        180, CGE_GROUP_MAIN, kMEFieldType_Texture, NULL, NULL, NULL, NULL, "texture_library/ui/Icons", NULL, NULL);

	//	METableAddGlobalDictColumn(pTable,"Faction",      "Faction",    150, CGE_GROUP_MAIN, kMEFieldType_ValidatedTextEntry, "CritterFaction", "resourceName");
	METableAddEnumColumn(pTable,   "Skill Type",      "Skilltype",  100, CGE_GROUP_MAIN, kMEFieldType_Combo, SkillTypeEnum);

	METableAddGlobalDictColumn(pTable, "Override Reward Table", "RewardTable",      160, CGE_GROUP_MAIN, kMEFieldType_ValidatedTextEntry, "RewardTable", "resourceName");
	METableAddGlobalDictColumn(pTable, "Additional Reward Table", "AddRewardTable", 160, CGE_GROUP_MAIN, kMEFieldType_ValidatedTextEntry, "RewardTable", "resourceName");

	METableAddDictColumn(pTable, "Chat Bubble", "ChatBubbleDef", 160, CGE_GROUP_MAIN, kMEFieldType_ValidatedTextEntry, "ChatBubbleDef", parse_ChatBubbleDef, "Name");

	METableAddColumn(pTable, "Male VoiceSet", "MaleVoiceSet",  130, CGE_GROUP_MAIN, kMEFieldType_ValidatedTextEntry, NULL, NULL, NULL, NULL, NULL, NULL, cge_getVoiceSetNames);
	METableAddColumn(pTable, "Female VoiceSet", "FemaleVoiceSet",  130, CGE_GROUP_MAIN, kMEFieldType_ValidatedTextEntry, NULL, NULL, NULL, NULL, NULL, NULL, cge_getVoiceSetNames);
	METableAddColumn(pTable, "Neutral VoiceSet", "NeutralVoiceSet",  130, CGE_GROUP_MAIN, kMEFieldType_ValidatedTextEntry, NULL, NULL, NULL, NULL, NULL, NULL, cge_getVoiceSetNames);

	METableAddGlobalDictColumn(pTable, "Spawn Anim", "SpawnAnim", 150, CGE_GROUP_MAIN, kMEFieldType_TextEntry, "AIAnimList", "resourceName");
	METableAddSimpleColumn(pTable, "Spawn Anim Time", "SpawnLockdownTime", 150, CGE_GROUP_MAIN, kMEFieldType_TextEntry); 
	METableAddGlobalDictColumn(pTable, "Alt Spawn Anim", "SpawnAnimAlternate", 150, CGE_GROUP_MAIN, kMEFieldType_TextEntry, "AIAnimList", "resourceName");
	METableAddSimpleColumn(pTable, "Alt Spawn Anim Time", "SpawnLockdownTimeAlternate", 150, CGE_GROUP_MAIN, kMEFieldType_TextEntry); 
	METableAddGlobalDictColumn(pTable, "Headshot Group", "HeadshotCritterGroup", 150, CGE_GROUP_MAIN, kMEFieldType_TextEntry, "CritterGroup", "resourceName");

}


static void cge_initCritterVars(METable *pTable)
{
	int id;

	// Create the subtable and get the ID
	id = METableCreateSubTable(pTable, "Vars", "CritterVars", parse_CritterVar, "key",
		cge_orderVars, cge_reorderVars, cge_CreateCritterVar);
	s_CritterVarConfigId = id;

	METableAddSimpleSubColumn(pTable, id, "Var", NULL, 80, NULL, 0);
	METableSetSubColumnState(pTable, id, "Var", ME_STATE_LABEL);
	METableAddSimpleSubColumn(pTable, id, "Var Name", ".Var.Name", 160, CGE_SUBGROUP_NONE, kMEFieldType_TextEntry);

	// Lock the label column in
	METableSetNumLockedSubColumns(pTable, id, 3);

	METableAddEnumSubColumn(pTable, id, "Type", ".Var.Type", 100, CGE_SUBGROUP_VAR, kMEFieldType_Combo, WorldVariableTypeEnum);
	METableAddSimpleSubColumn(pTable, id, "Int Val", ".Var.IntVal", 100, CGE_SUBGROUP_VAR, kMEFieldType_TextEntry);
	METableAddSimpleSubColumn(pTable, id, "Float Val", ".Var.FloatVal", 100, CGE_SUBGROUP_VAR, kMEFieldType_TextEntry);
	METableAddSimpleSubColumn(pTable, id, "String", ".Var.StringVal", 100, CGE_SUBGROUP_VAR, kMEFieldType_TextEntry);
	METableAddSimpleSubColumn(pTable, id, "Message",".Var.MessageVal.EditorCopy",     160, CGE_SUBGROUP_VAR, kMEFieldType_Message);
	METableAddGlobalDictSubColumn(pTable, id,   "Animation", ".Var.StringVal",        210, CGE_SUBGROUP_VAR, kMEFieldType_ValidatedTextEntry, "AIAnimList", "resourceName");
	METableAddGlobalDictSubColumn(pTable, id,   "Critter Def", ".Var.CritterDef",     210, CGE_SUBGROUP_VAR, kMEFieldType_ValidatedTextEntry, "CritterDef", "resourceName");
	METableAddGlobalDictSubColumn(pTable, id,   "Critter Group", ".Var.CritterGroup", 210, CGE_SUBGROUP_VAR, kMEFieldType_ValidatedTextEntry, "CritterGroup", "resourceName");

	// Order needs to be here but hidden in order to detect when it changes so we know object is dirty
	METableAddSimpleSubColumn(pTable, id, "Order", "order", 50, NULL, kMEFieldType_TextEntry);
	METableSetSubColumnState(pTable, id, "Order", ME_STATE_HIDDEN | ME_STATE_NOT_REVERTABLE);

}


static void cge_initCritterLore(METable *pTable)
{
	int id;

	// Create the subtable and get the ID
	id = METableCreateSubTable(pTable, "Lore", "CritterLoreEntries", parse_CritterLore, "key",
		cge_orderLore, cge_reorderLore, cge_CreateCritterLore);
	s_CritterLoreConfigId = id;

	METableAddSimpleSubColumn(pTable, id, "Lore", NULL, 80, NULL, 0);
	METableSetSubColumnState(pTable, id, "Lore", ME_STATE_LABEL);

	// Lock the label column in
	METableSetNumLockedSubColumns(pTable, id, 3);

	METableAddEnumSubColumn(pTable, id, "Attrib", "Attrib", 100, CGE_SUBGROUP_VAR, kMEFieldType_Combo, AttribTypeEnum);
	METableAddSimpleSubColumn(pTable, id, "DC", "DC", 100, CGE_SUBGROUP_VAR, kMEFieldType_TextEntry);
	METableAddGlobalDictSubColumn(pTable, id,   "Item", "Item",        210, CGE_SUBGROUP_VAR, kMEFieldType_ValidatedTextEntry, "ItemDef", "resourceName");
	METableAddGlobalDictSubColumn(pTable, id,   "Power", "Power",        210, CGE_SUBGROUP_VAR, kMEFieldType_ValidatedTextEntry, "PowerDef", "resourceName");

	// Order needs to be here but hidden in order to detect when it changes so we know object is dirty
	METableAddSimpleSubColumn(pTable, id, "Order", "order", 50, NULL, kMEFieldType_TextEntry);
	METableSetSubColumnState(pTable, id, "Order", ME_STATE_HIDDEN | ME_STATE_NOT_REVERTABLE);

}

static void cge_init(MultiEditEMDoc *pEditorDoc)
{

	if (!cgeWindow) {
		// Create the editor window
		cgeWindow = MEWindowCreate("CritterGroup Editor", "CritterGroup", "CritterGroups", SEARCH_TYPE_CRITTERGROUP, g_hCritterGroupDict, parse_CritterGroup, "name", "filename", "scope", pEditorDoc);

		// Add item-specific columns
		cge_initCritterGroupColumns(cgeWindow->pTable);
		cge_initCritterVars(cgeWindow->pTable);
		cge_initCritterLore(cgeWindow->pTable);

		METableFinishColumns(cgeWindow->pTable);

		// Init the menus after adding all the columns
		MEWindowInitTableMenus(cgeWindow);

		// Set the callbacks
		cge_initCallbacks(cgeWindow, cgeWindow->pTable);
	}

	// Show the window
	ui_WindowPresent(cgeWindow->pUIWindow);
}


//---------------------------------------------------------------------------------------------------
// Public Interface
//---------------------------------------------------------------------------------------------------

MEWindow *crittergroupEditor_init(MultiEditEMDoc *pEditorDoc) 
{
	cge_init(pEditorDoc);	
	return cgeWindow;
}


void crittergroupEditor_createGroup(char *pcName)
{
	// Create a new object since it is not in the dictionary
	// Add the object as a new object with no old
	void *pObject = cge_createObject(cgeWindow->pTable, NULL, pcName, false);
	METableAddRowByObject(cgeWindow->pTable, pObject, 1, 1);
}

#endif