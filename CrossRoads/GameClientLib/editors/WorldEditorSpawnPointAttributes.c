#ifndef NO_EDITORS

#include "WorldEditorSpawnPointAttributes.h"
#include "WorldEditorAttributesPrivate.h"
#include "WorldEditorAttributesHelpers.h"
#include "WorldEditorAttributes.h"
#include "WorldEditorOperations.h"
#include "WorldGrid.h"
#include "WorldEditorUtil.h"
#include "EditLibUIUtil.h"
#include "EditorManager.h"
#include "Expression.h"
#include "StringCache.h"
#include "wlEncounter.h"
#include "wlGroupPropertyStructs.h"
#include "MultiEditFieldContext.h"

#include "autogen/WorldEditorSpawnPointAttributes_c_ast.h"

AUTO_RUN_ANON(memBudgetAddMapping(__FILE__, BUDGET_Editors););

AUTO_STRUCT;
typedef struct wleAESpawnPointUIData
{
	char *pchNewVolume;				AST(NAME("NewVolume"))

	WorldScope *pClosestScope;		NO_AST
	bool bSameScope;				NO_AST
	int iVolumeCount;				NO_AST
} wleAESpawnPointUIData;
wleAESpawnPointUIData g_SpawnPointUI = {0};

static bool wleAESpawnPointExclude(GroupTracker *pTracker)
{
	GroupDef *pDef = pTracker->def;

	if(!pDef->property_structs.spawn_properties)
		return true;

	if (!g_SpawnPointUI.pClosestScope)
		g_SpawnPointUI.pClosestScope = pTracker->closest_scope;
	else if (g_SpawnPointUI.pClosestScope != pTracker->closest_scope)
		g_SpawnPointUI.bSameScope = false;

	if(g_SpawnPointUI.iVolumeCount == WL_VAL_UNSET)
		g_SpawnPointUI.iVolumeCount = eaSize(&pDef->property_structs.spawn_properties->source_volume_names);
	else if (g_SpawnPointUI.iVolumeCount != eaSize(&pDef->property_structs.spawn_properties->source_volume_names))
		g_SpawnPointUI.iVolumeCount = WL_VAL_DIFF;

	return false;
}

static void wleAESpawnPointCpyStrings(char ***pppDst, char **ppSrc)
{
	int i;
	eaClearEx(pppDst, StructFreeString);
	for ( i=0; i < eaSize(&ppSrc); i++ ) {
		if(ppSrc[i]) {
			eaPush(pppDst, StructAllocString(ppSrc[i]));
		}
	}
}

static void wleAESpawnPointChanged(MEField *pField, GroupTracker *pTracker, const GroupProperties *pOldProps, GroupProperties *pNewProps)
{
	if(g_SpawnPointUI.pchNewVolume) 
		eaPush(&pNewProps->spawn_properties->source_volume_names, StructAllocString(g_SpawnPointUI.pchNewVolume));
}

static void wleAESpawnPointDeleteVol(MEField *pField, GroupTracker *pTracker, const GroupProperties *pOldProps, GroupProperties *pNewProps)
{
	int iIdx = pField->arrayIndex;
	char *pchString;
	assert(iIdx >= 0 && iIdx < eaSize(&pNewProps->spawn_properties->source_volume_names));
	pchString = pNewProps->spawn_properties->source_volume_names[iIdx];
	eaRemove(&pNewProps->spawn_properties->source_volume_names, iIdx);
	StructFreeString(pchString);
}

static void wleAESpawnPointDeleteVolButtonCB(UIButton *pButton, MEFieldContextEntry *pEntry)
{
	wleAECallFieldChangedCallback(ENTRY_FIELD(pEntry), wleAESpawnPointDeleteVol);
}

int wleAESpawnPointReload(EMPanel *panel, EditorObject *edObj)
{
	static const char **ppVolumeNames = NULL;
	WorldSpawnProperties **ppProps = NULL;
	WorldSpawnProperties *pProp;
	MEFieldContext *pContext;
	U32 iRetFlags;

	g_SpawnPointUI.pClosestScope = NULL;
	g_SpawnPointUI.bSameScope = true;
	g_SpawnPointUI.iVolumeCount = WL_VAL_UNSET;
	ppProps = (WorldSpawnProperties**)wleAEGetSelectedDataFromPath("SpawnPoint", wleAESpawnPointExclude, &iRetFlags);
	if(eaSize(&ppProps) == 0 || !g_SpawnPointUI.bSameScope || (iRetFlags & WleAESelectedDataFlags_SomeMissing))
		return WLE_UI_PANEL_INVALID;

	pProp = ppProps[0];

	pContext = MEContextPushEA("WorldEditor_SpawnPointProperties", ppProps, ppProps, parse_WorldSpawnProperties);
	pContext->pUIContainer = emPanelGetUIContainer(panel);
	wleAEAddFieldChangedCallback(pContext, wleAESpawnPointChanged);

	StructFreeStringSafe(&g_SpawnPointUI.pchNewVolume);

	// update available combo values
	eaClear(&ppVolumeNames);
	worldGetObjectNames(WL_ENC_NAMED_VOLUME, &ppVolumeNames, NULL);

	MEContextAddEnum(kMEFieldType_Combo, WorldSpawnPointTypeEnum,					"SpawnType",			"Type",						"There can be only one START point per map.  GOTO points are used by warps and doors.  REPSAWN points are extra places a player can go when killed.");
	MEContextAddExpr(exprContextGetGlobalContext(),									"ActiveCondBlock",		"Spawn When",				"Player can't spawn here unless this is true.");
	MEContextAddSimple(kMEFieldType_Check,											"NeedsActivation",		"Needs Player Activation",	"Spawn point needs to be activated by player first.");
	MEContextAddDict(kMEFieldType_ValidatedTextEntry, "DoorTransitionSequenceDef",	"TransitionOverride",	"Transition Override",		"Transition sequence to play when spawned here.");
	
	assert(g_SpawnPointUI.iVolumeCount != WL_VAL_UNSET);
	if(g_SpawnPointUI.iVolumeCount != WL_VAL_DIFF) {
		int i;
		char pchDisplayText[256];
		assert(g_SpawnPointUI.iVolumeCount >= 0 && g_SpawnPointUI.iVolumeCount <= eaSize(&pProp->source_volume_names));
		sprintf(pchDisplayText, "Respawn From Volume");
		for ( i=0; i < eaSize(&pProp->source_volume_names); i++ ) {
			MEFieldContextEntry *pEntry;
			pEntry = MEContextAddListIdx(kMEFieldType_Combo, &ppVolumeNames,		"SourceVolumeNames", i,	pchDisplayText,	"If the player dies in one of these volumes, this spawn point will be used in preference to the closest one.");
			MEContextEntryAddActionButton(pEntry, "X", NULL, wleAESpawnPointDeleteVolButtonCB, pEntry, 0,					"Delete a \"respawn from\" volume.");
			sprintf(pchDisplayText, "Respawn From Volume %d", i+2);
		}
		
		MEContextPush("Volumes", &g_SpawnPointUI, &g_SpawnPointUI, parse_wleAESpawnPointUIData);
		MEContextAddList(kMEFieldType_Combo, &ppVolumeNames,						"NewVolume",			pchDisplayText,	"If the player dies in one of these volumes, this spawn point will be used in preference to the closest one.");
		MEContextPop("Volumes");
	}

	MEContextAddSpacer();
	emPanelSetHeight(panel, pContext->iYPos);
	emPanelSetActive(panel, !(iRetFlags & WleAESelectedDataFlags_Inactive));

	MEContextPop("WorldEditor_SpawnPointProperties");

	return WLE_UI_PANEL_OWNED;
}

#include "autogen/WorldEditorSpawnPointAttributes_c_ast.c"

#endif
