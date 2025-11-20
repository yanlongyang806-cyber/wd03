#ifndef NO_EDITORS

#include "WorldEditorAttributesPrivate.h"

#include "WorldGrid.h"
#include "WorldEditorClientMain.h"
#include "WorldEditorOperations.h"
#include "WorldEditorUtil.h"
#include "ObjectLibrary.h"
#include "EditorManager.h"
#include "EditLibUIUtil.h"
#include "MultiEditFieldContext.h"

#include "WorldEditorAttributesHelpers.h"
#include "autogen/WorldEditorBuildingGen_c_ast.h"

AUTO_RUN_ANON(memBudgetAddMapping(__FILE__, BUDGET_Editors););

#define MAX_LAYERS 10
#define MAX_HEIGHT 50

AUTO_STRUCT;
typedef struct wleAEBuildingLayerUIData
{
	bool bSeed;											AST(NAME("SeedPerGroup"))
} wleAEBuildingLayerUIData;

AUTO_STRUCT;
typedef struct wleAEBuildingUIData
{
	int iLayerCount;									AST(NAME("LayerCount"))
	wleAEBuildingLayerUIData aLayers[10];				AST(AUTO_INDEX(Layers))
} wleAEBuildingUIData;

static wleAEBuildingUIData g_BuildingUI = {0};

static bool wleAEBuildingGenExclude(GroupTracker *pTracker)
{
	if(!pTracker->def->property_structs.building_properties)
		return true;
	return false;
}

static void wleAEBuildingPropsChangedCB(MEField *pField, GroupTracker *pTracker, const GroupProperties *pOldProps, GroupProperties *pNewProps)
{
	int i;
	WorldBuildingProperties *pBuildingProps = pNewProps->building_properties;

	// delete extra layers
	for (i = eaSize(&pBuildingProps->layers) - 1; i >= g_BuildingUI.iLayerCount; --i)
	{
		WorldBuildingLayerProperties *layer = eaPop(&pBuildingProps->layers);
		StructDestroy(parse_WorldBuildingLayerProperties, layer);
	}

	// add new layers
	for (i = eaSize(&pBuildingProps->layers); i < g_BuildingUI.iLayerCount; ++i)
	{
		WorldBuildingLayerProperties *layer = StructCreate(parse_WorldBuildingLayerProperties);
		GroupDef *pPrevLayerDef;
		layer->height = 1;
		layer->seed = rand();
		assert(i >= 0 && i < MAX_LAYERS);
		g_BuildingUI.aLayers[i].bSeed = false;
		pPrevLayerDef = GET_REF(pBuildingProps->layers[i-1]->group_ref);
		if(pPrevLayerDef)
			SET_HANDLE_FROM_REFERENT(OBJECT_LIBRARY_DICT, pPrevLayerDef, layer->group_ref);
		eaPush(&pBuildingProps->layers, layer);
	}

	for ( i=0; i < g_BuildingUI.iLayerCount; i++ ) {
		WorldBuildingLayerProperties *layer = pBuildingProps->layers[i];
		if(layer->seed_delta && !g_BuildingUI.aLayers[i].bSeed) {
			layer->seed_delta = 0;
		} else if (!layer->seed_delta && g_BuildingUI.aLayers[i].bSeed) {
			layer->seed_delta = rand();
		}
	}
}

static void wleAEBuildingGenLayerReload(WorldBuildingProperties **ppProps, int layer_idx)
{
	int i;
	WorldBuildingLayerProperties **ppLayers = NULL;
	WorldBuildingLayerProperties *pLayer;
	char pchContextName[256];
	char pchLayerName[256];

	for ( i=0; i < eaSize(&ppProps); i++ ) {
		assert(eaSize(&ppProps[i]->layers) > layer_idx);
		eaPush(&ppLayers, ppProps[i]->layers[layer_idx]);
	}
	assert(eaSize(&ppLayers) > 0);
	pLayer = ppLayers[0];

	sprintf(pchContextName, "BuildingLayer %d", layer_idx);
	MEContextPushEA(pchContextName, ppLayers, ppLayers, parse_WorldBuildingLayerProperties);

	MEContextAddSpacer();

	sprintf(pchLayerName, "BuildingLayer %d", layer_idx);
	MEContextAddLabel(												"LayerName",			pchLayerName,				NULL);
	MEContextAddMinMax(kMEFieldType_Spinner, 1, MAX_HEIGHT, 1,		"Height",				"Height",					"Height of this layer, in objects.");	
	MEContextAddPicker(OBJECT_LIBRARY_DICT, "Object Picker",		"GroupRef",				"Object",					"The object library piece to place in this layer.");

	g_BuildingUI.aLayers[layer_idx].bSeed = (pLayer->seed_delta != 0);
	MEContextPush("LayerData", &g_BuildingUI.aLayers[layer_idx], &g_BuildingUI.aLayers[layer_idx], parse_wleAEBuildingLayerUIData);
	MEContextAddSimple(kMEFieldType_Check,							"SeedPerGroup",			"Seed Per Group",			"If set, each object in this layer gets a different random seed.");
	MEContextPop("LayerData");

	MEContextPop(pchContextName);

	eaDestroy(&ppLayers);
}

int wleAEBuildingGenReload(EMPanel *panel, EditorObject *edObj)
{
	int i;
	static WleAEPropStructData pPropData = {"Building", parse_WorldBuildingProperties};
	WorldBuildingProperties **ppProps = NULL;
	WorldBuildingProperties *pProp = NULL;
	MEFieldContext *pLayerContext;
	MEFieldContext *pContext;
	bool bLayerCountSimilar;
	U32 iRetFlags;

	ppProps = (WorldBuildingProperties**)wleAEGetSelectedDataFromPath("Building", wleAEBuildingGenExclude, &iRetFlags);
	if(iRetFlags & WleAESelectedDataFlags_Failed)
		return WLE_UI_PANEL_INVALID;

	if(eaSize(&ppProps) > 0 && !(iRetFlags & WleAESelectedDataFlags_SomeMissing)) {
		pProp = ppProps[0]; 
	} else {
		return WLE_UI_PANEL_INVALID;
	}

	pContext = MEContextPushEA("WorldEditor_BuildingProps", ppProps, ppProps, parse_WorldBuildingProperties);
	pContext->pUIContainer = emPanelGetUIContainer(panel);
	wleAEAddFieldChangedCallback(pContext, wleAEBuildingPropsChangedCB);

	MEContextAddSimple(kMEFieldType_Check,							"DisableOcclusion",		"Disable Occlusion",		"Turns off the automatic occlusion volume generation.");
	MEContextAddSpacer();
	MEContextAddSimple(kMEFieldType_Check,							"DisableLOD",			"Disable LOD",				"Turns off the automatic low LOD model.");
	if(!MEContextFieldDiff("DisableLOD") && !pProp->no_lod) {
		MEContextAddPicker(OBJECT_LIBRARY_DICT, "Object Picker",		"LODModelRef",			"LOD Model",				"The model to place as the LOD.");
	}
	MEContextAddSpacer();

	//Find the layer count and ensure entire selection has the same count
	g_BuildingUI.iLayerCount = eaSize(&pProp->layers);
	bLayerCountSimilar = true;
	for ( i=1; i < eaSize(&ppProps); i++ ) {
		if(g_BuildingUI.iLayerCount != eaSize(&ppProps[i]->layers)) {
			bLayerCountSimilar = false;
			break;
		}
	}

	pLayerContext = MEContextPush("BuildingLayers", &g_BuildingUI, &g_BuildingUI, parse_wleAEBuildingUIData);
	MEContextAddMinMax(kMEFieldType_Spinner, 1, MAX_LAYERS, 1,		"LayerCount",			"Layers",					"The number of layers in this building.");
	MEContextPop("BuildingLayers");

	if(bLayerCountSimilar) {
		for ( i=0; i < g_BuildingUI.iLayerCount; i++ ) {
			wleAEBuildingGenLayerReload(ppProps, i);
		}
	}

	MEContextAddSpacer();
	emPanelSetHeight(panel, pContext->iYPos);
	emPanelSetActive(panel, !(iRetFlags & WleAESelectedDataFlags_Inactive));

	MEContextPop("WorldEditor_BuildingProps");

	return WLE_UI_PANEL_OWNED;
}

#include "autogen/WorldEditorBuildingGen_c_ast.c"

#endif
