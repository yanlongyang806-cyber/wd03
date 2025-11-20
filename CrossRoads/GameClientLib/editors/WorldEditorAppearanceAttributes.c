#ifndef NO_EDITORS

#include "WorldEditorAppearanceAttributes.h"
#include "WorldEditorOperations.h"
#include "WorldEditorUtil.h"
#include "WorldEditorOptions.h"
#include "EditLibUIUtil.h"
#include "WorldGrid.h"
#include "Materials.h"
#include "groupdbmodify.h"
#include "rgb_hsv.h"
#include "StringCache.h"
#include "wlAutoLOD.h"
#include "wlEncounter.h"
#include "UIFXButton.h"

#include "CurveEditor.h"

// Needed for FX parameters.
#include "dynFxInfo.h"
#include "dynFxEnums.h"
#include "ScratchStack.h"
#include "autogen/dynFxInfo_h_ast.h"

#include "WorldEditorAppearanceAttributes_h_ast.h"

AUTO_RUN_ANON(memBudgetAddMapping(__FILE__, BUDGET_Editors););

/********************
* DEFINITIONS
********************/
#define wleAEAppearanceUpdateInit()\
	GroupTracker *tracker;\
	GroupDef *def;\
	assert(obj->type->objType == EDTYPE_TRACKER);\
	tracker = trackerFromTrackerHandle(obj->obj);\
	def = tracker ? tracker->def : NULL

#define wleAEAppearanceApplyInit()\
	GroupTracker *tracker;\
	GroupDef *def;\
	assert(obj->type->objType == EDTYPE_TRACKER);\
	tracker = wleOpPropsBegin(obj->obj);\
	if (!tracker)\
		return;\
	def = tracker ? tracker->def : NULL;\
	if (!def)\
	{\
		wleOpPropsEnd();\
		return;\
	}

#define wleAEAppearanceApplyInitAt(i)\
	GroupTracker *tracker;\
	GroupDef *def;\
	assert(objs[i]->type->objType == EDTYPE_TRACKER);\
	tracker = wleOpPropsBegin(objs[i]->obj);\
	if (!tracker)\
		continue;\
	def = tracker ? tracker->def : NULL;\
	if (!def)\
	{\
		wleOpPropsEndNoUIUpdate();\
		continue;\
	}

/********************
* GLOBALS
********************/
// This is the grouptree global appearance UI struct
static WleAEAppearanceUI wleAEGlobalAppearanceUI;

/********************
* UTIL
********************/

/******
* This function does the same thing as stashTableMerge, except that this allows collisions and it assumes
* string key/pointer elements.
******/
static void wleAEAppearanceStashMerge(StashTable table1, StashTable table2)
{
	StashTableIterator iter;
	StashElement el;

	stashGetIterator(table2, &iter);
	while (stashGetNextElement(&iter, &el))	
		stashAddPointer(table1, stashElementGetStringKey(el), stashElementGetPointer(el), true);
}

/******
* This function takes a material and material/texture swap lists and returns an association structure
* that contains that material and all of its textures, including swap information generated from the swap lists.
* PARAMS:
*   mat - Material to process
*   materialSwaps - EArray of MaterialSwaps to use in determining which textures to fetch
*   textureSwaps - EArray of TextureSwaps to use in determining which textures to add
*   applySwaps - bool indicating whether to replace orig_name fields entirely (true) or to populate replace_name fields (false)
* RETURNS:
*   MaterialTextureAssoc containing the material name (post-swap) and its textures (post-swap)
******/
static MaterialTextureAssoc *wleGetMaterialTexs(Material *mat, const MaterialSwap **materialSwaps, const TextureSwap **textureSwaps, bool applySwaps)
{
	const char *origName = mat->material_name;
	const char *replaceName = NULL;
	StashTable matTexs = stashTableCreateWithStringKeys(16, StashDefault);
	StashTableIterator iter;
	StashElement el;
	MaterialTextureAssoc *newAssoc = NULL;
	int i;

	for (i = 0; i < eaSize(&materialSwaps); ++i)
	{
		if (stricmp(materialSwaps[i]->orig_name, origName)==0)
		{
			replaceName = materialSwaps[i]->replace_name;
			mat = materialFind(replaceName, WL_FOR_UTIL);
			break;
		}
	}

	newAssoc = StructCreate(parse_MaterialTextureAssoc);
	newAssoc->orig_name = applySwaps && replaceName ? replaceName : origName;
	newAssoc->replace_name = applySwaps ? NULL : replaceName;

	materialGetTextureNames(mat, matTexs, NULL);
	stashGetIterator(matTexs, &iter);
	while (stashGetNextElement(&iter, &el))
	{
		TextureSwap *newTexSwap = StructCreate(parse_TextureSwap);
		origName = stashElementGetPointer(el);
		replaceName = NULL;

		for (i = 0; i < eaSize(&textureSwaps); ++i)
		{
			if (stricmp(textureSwaps[i]->orig_name, origName) == 0)
			{
				replaceName = textureSwaps[i]->replace_name;
				break;
			}
		}

		newTexSwap->orig_name = applySwaps && replaceName ? replaceName : origName;
		newTexSwap->replace_name = applySwaps ? NULL : replaceName;
		eaPush(&newAssoc->textureSwaps, newTexSwap);
	}

	stashTableDestroy(matTexs);

	return newAssoc;
}

/******
* This function retrieves a specified model's materials and textures in a list of associations.
* PARAMS:
*   model - Model whose material/texture info is to be retrieved
*   lodIdx - index of the LOD to use; -1 will use the lowest LOD
*   assocsOut - EArray of MaterialTextureAssoc structures that will hold the material/texture data
*   applySwaps - bool indicating whether to replace orig_name fields entirely (true) or to populate replace_name fields (false)
******/
void wleGetModelTexMats(Model *model, int lodIdx, MaterialTextureAssoc ***assocsOut, bool applySwaps)
{
	int i, j;
	int material_count;

	for (j = 0; j < eaSize(&model->model_lods); j++)
	{
		ModelLOD *model_lod;
		AutoLOD *auto_lod = NULL;

		if (lodIdx >= 0 && lodIdx != j)
			continue;

		model_lod = modelLODWaitForLoad(model, j);

		if (!model_lod || !model_lod->data)
			continue;

		material_count = model_lod->data->tex_count;

		if (model->lod_info && j < eaSize(&model->lod_info->lods))
			auto_lod = model->lod_info->lods[j];

		for (i = 0; i < material_count; i++)
		{
			if (model_lod->materials[i])
				eaPush(assocsOut, wleGetMaterialTexs(model_lod->materials[i], SAFE_MEMBER(auto_lod, material_swaps), SAFE_MEMBER(auto_lod, texture_swaps), applySwaps));
		}
	}
}

/******
* This function retrieves a specified planet's materials and textures in a list of associations.
* PARAMS:
*   planet - WorldPlanetProperties whose material/texture info is to be retrieved
*   assocsOut - EArray of MaterialTextureAssoc structures that will hold the material/texture data
******/
void wleGetPlanetTexMats(WorldPlanetProperties *planet, MaterialTextureAssoc ***assocsOut)
{
	Material *material;

	if (planet->has_atmosphere)
	{
		material = materialFindNoDefault("Planet_Generic_With_Atmosphere", WL_FOR_WORLD);
		if (material)
			eaPush(assocsOut, wleGetMaterialTexs(material, NULL, NULL, true));

		material = materialFindNoDefault("Planet_Atmosphere", WL_FOR_WORLD);
		if (material)
			eaPush(assocsOut, wleGetMaterialTexs(material, NULL, NULL, true));
	}
	else
	{
		material = materialFindNoDefault("Planet_Generic", WL_FOR_WORLD);
		if (material)
			eaPush(assocsOut, wleGetMaterialTexs(material, NULL, NULL, true));
	}
}

static void wleGetTrackerTexMatsRecurse(GroupTracker *tracker, MaterialTextureAssoc ***matTexAssocsOut, bool applySwaps)
{
	MaterialTextureAssoc **matTexAssocs = NULL;
	int i, j, k;

	trackerOpen(tracker);

	// recurse through children
	for (i = 0; i < tracker->child_count; i++)
		wleGetTrackerTexMatsRecurse(tracker->children[i], &matTexAssocs, true);
	if (tracker->def)
	{
		Model *model = tracker->def->model;
		WorldPlanetProperties *planet = tracker->def->property_structs.planet_properties;
		MaterialTextureAssoc **swappedAssocs = NULL;

		// first get unswapped materials and textures (except for models that have autoLOD swaps, which are accounted
		// for in the returned material/texture values)
		if (model)
			wleGetModelTexMats(model, -1, &matTexAssocs, true);
		if (planet)
			wleGetPlanetTexMats(planet, &matTexAssocs);

		// if we're applying the swaps on the tracker def, we immediately replace swapped materials/textures with their
		// replacements; otherwise, we just note them in the replace_name fields
		for (i = 0; i < eaSize(&tracker->def->material_swaps); i++)
		{
			for (j = eaSize(&matTexAssocs) - 1; j >= 0; j--)
			{
				MaterialTextureAssoc *swappedAssoc;

				if (matTexAssocs[j]->orig_name != tracker->def->material_swaps[i]->orig_name)
					continue;

				// get swapped textures regardless of whether swaps are applied
				swappedAssoc = wleGetMaterialTexs(materialFind(tracker->def->material_swaps[i]->replace_name, WL_FOR_UTIL), NULL, NULL, true);

				// if we find a matching entry corresponding to the material swap, we apply the swap as necessary
				if (applySwaps)
				{
					StructDestroy(parse_MaterialTextureAssoc, eaRemove(&matTexAssocs, j));
					eaPush(&swappedAssocs, swappedAssoc);
				}
				else
				{
					// make sure that only the swapped textures get picked up during aggregation
					eaDestroyStruct(&matTexAssocs[j]->textureSwaps, parse_TextureSwap);
					eaCopyStructs(&swappedAssoc->textureSwaps, &matTexAssocs[j]->textureSwaps, parse_TextureSwap);
					matTexAssocs[j]->replace_name = tracker->def->material_swaps[i]->replace_name;
					StructDestroy(parse_MaterialTextureAssoc, swappedAssoc);
				}
			}
		}
		eaPushEArray(&matTexAssocs, &swappedAssocs);

		for (i = 0; i < eaSize(&tracker->def->texture_swaps); i++)
		{
			for (j = 0; j < eaSize(&matTexAssocs); j++)
			{
				MaterialTextureAssoc *assoc = matTexAssocs[j];
				for (k = eaSize(&assoc->textureSwaps) - 1; k >= 0; k--)
				{
					TextureSwap *assocTexSwap = assoc->textureSwaps[k];
					if (assocTexSwap->orig_name != tracker->def->texture_swaps[i]->orig_name)
						continue;

					// if we find a matching entry corresponding to the texture swap, we apply the swap as necessary
					if (applySwaps)
					{
						TextureSwap *newAssocTex = StructCreate(parse_TextureSwap);
						StructDestroy(parse_TextureSwap, eaRemove(&assoc->textureSwaps, k));
						eaPush(&assoc->textureSwaps, newAssocTex);
						newAssocTex->orig_name = tracker->def->texture_swaps[i]->replace_name;
						newAssocTex->replace_name = NULL;
					}
					else
						assocTexSwap->replace_name = tracker->def->texture_swaps[i]->replace_name;
				}
			}
		}
	}

	eaPushEArray(matTexAssocsOut, &matTexAssocs);
	eaDestroy(&matTexAssocs);
}

/******
* This function recurses through a tracker, getting its (and its children's) materials and textures.
* PARAMS:
*   tracker - GroupTracker to recursively search for materials and textures
*   matStash - StashTable output of unique materials (i.e. original name->swapped name (NULL if unswapped))
*   texStash - StashTable output of unique textures (i.e. original name->swapped name (NULL if unswapped))
*   matTexAssocsOut - EArray of MaterialTextureAssoc structures holding each material and its child textures, including any
*                     swap data; these are ensured to be unique (though multiple copies of a material may appear if the child textures
*                     have different swaps applied).
*   applySwaps - bool indicating whether to replace orig_name fields entirely (true) or to populate replace_name fields (false)
******/
void wleGetTrackerTexMats(GroupTracker *tracker, StashTable matStash, StashTable texStash, MaterialTextureAssoc ***matTexAssocsOut, bool applySwaps)
{
	MaterialTextureAssoc **assocs = NULL;
	int i, j, k;
	bool outputtingAssocs = true;

	if (!matTexAssocsOut)
	{
		outputtingAssocs = false;
		matTexAssocsOut = &assocs;
	}

	wleGetTrackerTexMatsRecurse(tracker, matTexAssocsOut, applySwaps);

	// delete duplicate associations; n^2, but the data set should be small
	if (outputtingAssocs)
	{
		for (i = eaSize(matTexAssocsOut) - 1; i >= 0; i--)
		{
			MaterialTextureAssoc *assoc1 = (*matTexAssocsOut)[i];
			for (j = i - 1; j >= 0; j--)
			{
				MaterialTextureAssoc *assoc2 = (*matTexAssocsOut)[j];

				// check for matching textures
				if (assoc1->orig_name == assoc2->orig_name && assoc1->replace_name == assoc2->replace_name &&
					eaSize(&assoc1->textureSwaps) == eaSize(&assoc2->textureSwaps))
				{
					for (k = 0; k < eaSize(&assoc1->textureSwaps); k++)
					{
						TextureSwap *swap1 = assoc1->textureSwaps[k];
						TextureSwap *swap2 = assoc2->textureSwaps[k];
						if (swap1->orig_name != swap2->orig_name || swap1->replace_name != swap2->replace_name)
							break;
					}

					// if the j'th assoc is the same as the i'th, remove the j'th
					if (k == eaSize(&assoc1->textureSwaps))
					{
						eaRemove(matTexAssocsOut, j);
						i--;
					}
				}
			}
		}
	}

	// aggregate results into the stashes
	if (!matStash && !texStash)
		return;

	for (i = 0; i < eaSize(matTexAssocsOut); i++)
	{
		MaterialTextureAssoc *assoc = (*matTexAssocsOut)[i];
		if (matStash)
			stashAddPointer(matStash,
				applySwaps ? assoc->replace_name : assoc->orig_name,
				applySwaps ? NULL : assoc->replace_name, true);
		if (texStash)
		{
			for (j = 0; j < eaSize(&assoc->textureSwaps); j++)
				stashAddPointer(texStash,
					applySwaps ? assoc->textureSwaps[j]->replace_name : assoc->textureSwaps[j]->orig_name, 
					applySwaps ? NULL : assoc->textureSwaps[j]->replace_name, true);
		}
	}

	// cleanup
	eaDestroy(&assocs);
}

/******
* This function tells the server to add texture and material swaps to a specified tracker.
* PARAMS:
*   texSwaps - an EArray of strings that denote the texture swaps; the array consists of 
*              an even number of strings, with each pair consisting of the old texture
*              name (first) and the new texture name (second)
*   matSwaps - an EArray of strings (in a similar format as texSwaps) that contains the
*              material names to swap.
******/
// TODO: fix journaling of swaps
static bool wleAEAppearanceSetSwaps(const char * const *texSwaps, const char * const *matSwaps, void *userData)
{
	EditorObject **objects = NULL;
	int k;

	if (!wleAEGetSelected())
		return false;

	wleAEGetSelectedObjects(&objects);
	EditUndoBeginGroup(edObjGetUndoStack());
	for (k = 0; k < eaSize(&objects); k++)
	{
		TrackerHandle *handle = objects[k]->obj;
		GroupTracker *tracker = wleOpPropsBegin(handle);
		bool foundSwap = false;
		int i, j;

		if (!tracker)
			return true;

		for (i = 0; i < eaSize(&texSwaps) / 2; i++)
		{
			for (j = eaSize(&tracker->def->texture_swaps) - 1; j >= 0; j--)
			{
				TextureSwap *currSwap = tracker->def->texture_swaps[j];
				if (texSwaps[i * 2] == currSwap->orig_name)
				{
					freeTextureSwap(eaRemove(&tracker->def->texture_swaps, j));
					tracker->def->group_flags |= GRP_HAS_TEXTURE_SWAPS;
					foundSwap = true;
				}
			}
			if (texSwaps[i * 2 + 1] && texSwaps[i * 2 + 1][0])
			{
				eaPush(&tracker->def->texture_swaps, createTextureSwap(NULL, texSwaps[i * 2], texSwaps[i * 2 + 1]));
				tracker->def->group_flags |= GRP_HAS_TEXTURE_SWAPS;
				foundSwap = true;
			}
		}
		for (i = 0; i < eaSize(&matSwaps) / 2; i++)
		{
			for (j = eaSize(&tracker->def->material_swaps) - 1; j >= 0; j--)
			{
				MaterialSwap *currSwap = tracker->def->material_swaps[j];
				if (matSwaps[i * 2] == currSwap->orig_name)
				{
					freeMaterialSwap(eaRemove(&tracker->def->material_swaps, j));
					tracker->def->group_flags |= GRP_HAS_MATERIAL_SWAPS;
					foundSwap = true;
				}
			}
			if (matSwaps[i * 2 + 1] && matSwaps[i * 2 + 1][0])
			{
				eaPush(&tracker->def->material_swaps, createMaterialSwap(matSwaps[i *  2], matSwaps[i * 2 + 1]));
				tracker->def->group_flags |= GRP_HAS_MATERIAL_SWAPS;
				foundSwap = true;
			}
		}
		if (foundSwap)
			groupdbDirtyTracker(tracker, UPDATE_GROUP_PROPERTIES);
		wleOpPropsEnd();
	}
	EditUndoEndGroup(edObjGetUndoStack());

	return true;
}


static void wleAEMiscPropFxApply(WleAEParamCombo *param, void *unused, EditorObject **objs)
{
	int i;
	for (i = 0; i < eaSize(&objs); i++)
	{
		GroupTracker *tracker;
		GroupDef *def;

		assert(objs[i]->type->objType == EDTYPE_TRACKER);
		tracker = wleOpPropsBegin(objs[i]->obj);
		if (!tracker)
		{
			continue;
		}
		def = tracker ? tracker->def : NULL;
		if (!def)
		{
			wleOpPropsEndNoUIUpdate();
			continue;
		}

		if (!param->stringvalue || !param->stringvalue[0])
		{
			StructDestroySafe(parse_WorldFXProperties, &def->property_structs.fx_properties);
		}

		wleOpPropsEndNoUIUpdate();
	}
	wleOpRefreshUI();
}

static bool wleAEMiscPropFxCritCheck(EditorObject *obj, const char *propertyName, WleCriterionCond cond, const char *val, void *data)
{
	if (obj->type->objType == EDTYPE_TRACKER)
	{
		GroupTracker *tracker = trackerFromTrackerHandle(obj->obj);
		const char *fxName = (tracker && tracker->def && tracker->def->property_structs.fx_properties) ? tracker->def->property_structs.fx_properties->pcName : NULL;
		bool ret;

		if (wleCriterionStringTest(fxName ? fxName : "", val, cond, &ret))
			return ret;
	}
	return false;
}

static void wleAESetFxSelectTargetNode(void *unused, void *unused2)
{
	if (edObjSelectionGetCount(EDTYPE_NONE) == 1)
	{
		EditorObject **selection = edObjSelectionGet(EDTYPE_TRACKER);
		if(eaSize(&selection) == 1)
		{
			GroupTracker *tracker = (GroupTracker*)selection[0]->obj;
			wleTrackerDeselectAll();
			edObjSelect(editorObjectCreate(tracker, "FXTargetNode", NULL, EDTYPE_FX_TARGET_NODE), false, false);
		}
	}
}

/********************
* FX PARAMETERS
********************/

// Functions to get/update FX param values...

// Get the value for a given FX parameter that's been set in the GroupDef.
MultiVal *wlaAEGetFXParamFromDef(GroupDef *def, const char *paramName) {

	char *propString = SAFE_MEMBER(def->property_structs.fx_properties, pcParams);
	MultiVal *ret = NULL;

	// Create the param block from the set value.
	DynParamBlock *block = StructCreateFromString(parse_DynParamBlock, propString);

	if(block) {
		ret = dynFxInfoGetParamValueFromBlock(paramName, block);
		StructDestroy(parse_DynParamBlock, block);
	}

	return ret;
}

static void wleAEUpdateFXParamVec3(WleAEParamVec3 *param, void *func_data, EditorObject *obj) {

	WleAEFXParam *fxParam = (WleAEFXParam *)func_data;
	WleAEParamCombo *fxCombo = fxParam->fx;
	const char *fxName = fxCombo->stringvalue;
	const char *paramName = fxParam->name;
	MultiVal *paramVal;
	Vec3 *vec;

	wleAEAppearanceUpdateInit();

	paramVal = wlaAEGetFXParamFromDef(def, paramName);
	
	if(paramVal) {
		fxParam->paramVec3.is_specified = true;
	} else {
		paramVal = dynFxInfoGetParamValue(fxName, paramName);
		fxParam->paramVec3.is_specified = false;
	}

	vec = MultiValGetVec3(paramVal, NULL);
	if(vec) {
		copyVec3(*vec, fxParam->paramVec3.vecvalue);
	}

	MultiValDestroy(paramVal);
}

static void wleAEUpdateFXParamString(WleAEParamText *param, void *func_data, EditorObject *obj) {

	WleAEFXParam *fxParam = (WleAEFXParam *)func_data;
	WleAEParamCombo *fxCombo = fxParam->fx;
	const char *fxName = fxCombo->stringvalue;
	const char *paramName = fxParam->name;
	MultiVal *paramVal;
	const char *strVal;

	wleAEAppearanceUpdateInit();

	if(fxParam->paramText.stringvalue) {
		StructFreeString(fxParam->paramText.stringvalue);
	}

	paramVal = wlaAEGetFXParamFromDef(def, paramName);

	if(paramVal) {
		fxParam->paramText.is_specified = true;
	} else {
		paramVal = dynFxInfoGetParamValue(fxName, paramName);
		fxParam->paramText.is_specified = false;
	}
	
	strVal = MultiValGetString(paramVal, NULL);
	if(strVal) {
		fxParam->paramText.stringvalue = StructAllocString(strVal);
	}	

	MultiValDestroy(paramVal);
}

static void wleAEUpdateFXParamDict(WleAEParamDictionary *param, void *func_data, EditorObject *obj) {

	WleAEFXParam *fxParam = (WleAEFXParam *)func_data;
	WleAEParamCombo *fxCombo = fxParam->fx;
	const char *fxName = fxCombo->stringvalue;
	const char *paramName = fxParam->name;
	MultiVal *paramVal;
	const char *strVal;

	wleAEAppearanceUpdateInit();

	if(fxParam->paramDict.refvalue) {
		StructFreeString(fxParam->paramDict.refvalue);
	}

	paramVal = wlaAEGetFXParamFromDef(def, paramName);

	if(paramVal) {
		fxParam->paramDict.is_specified = true;
	} else {
		paramVal = dynFxInfoGetParamValue(fxName, paramName);
		fxParam->paramDict.is_specified = false;
	}

	strVal = MultiValGetString(paramVal, NULL);
	if(strVal) {
		fxParam->paramDict.refvalue = StructAllocString(strVal);
	}	

	MultiValDestroy(paramVal);
}

static void wleAEUpdateFXParamFloat(WleAEParamFloat *param, void *func_data, EditorObject *obj) {

	WleAEFXParam *fxParam = (WleAEFXParam *)func_data;
	WleAEParamCombo *fxCombo = fxParam->fx;
	const char *fxName = fxCombo->stringvalue;
	const char *paramName = fxParam->name;
	MultiVal *paramVal;

	wleAEAppearanceUpdateInit();

	paramVal = wlaAEGetFXParamFromDef(def, paramName);

	if(paramVal) {
		fxParam->paramFloat.is_specified = true;
	} else {
		paramVal = dynFxInfoGetParamValue(fxName, paramName);
		fxParam->paramFloat.is_specified = false;
	}

	{
		float f = MultiValGetFloat(paramVal, NULL);
		fxParam->paramFloat.floatvalue = f;
	}

	MultiValDestroy(paramVal);
}

// Functions to set/apply FX param values...

static void wleAESetFXParamInDef(GroupDef *def, const char *paramName, MultiVal *paramValue) {

	char *propString = SAFE_MEMBER(def->property_structs.fx_properties, pcParams);
	DynParamBlock *block = StructCreateFromString(parse_DynParamBlock, propString);
	int i;
	char *parserStr = NULL;

	if(!block) {
		block = StructCreate(parse_DynParamBlock);
	}

	// Remove the property from the param block.
	for(i = 0; i < eaSize(&block->eaDefineParams); i++) {
		if(stricmp(block->eaDefineParams[i]->pcParamName, paramName) == 0) {
			StructDestroy(parse_DynDefineParam, block->eaDefineParams[i]);
			eaRemoveFast(&(block->eaDefineParams), i);
			break;
		}
	}

	// Add in the new value, if it exists.
	if(paramValue) {
		DynDefineParam *param = StructCreate(parse_DynDefineParam);
		param->pcParamName = allocAddString(paramName);
		MultiValCopy(&param->mvVal, paramValue);
		eaPush(&block->eaDefineParams, param);
	}

	if(eaSize(&block->eaDefineParams)) {

		ParserWriteText(&parserStr, parse_DynParamBlock, block, 0, 0, 0);
		if(!def->property_structs.fx_properties)
			def->property_structs.fx_properties = StructCreate(parse_WorldFXProperties);
		StructCopyString(&def->property_structs.fx_properties->pcParams, parserStr);
		estrDestroy(&parserStr);

	} else {

		// No properties in the block? Just remove it entirely.
		if(def->property_structs.fx_properties)
			StructFreeStringSafe(&def->property_structs.fx_properties->pcParams);
	}

	StructDestroy(parse_DynParamBlock, block);

}

static void wleAEApplyFXParamVec3(WleAEParamVec3 *param, void *func_data, EditorObject *obj) {

	WleAEFXParam *fxParam = (WleAEFXParam *)func_data;
	WleAEParamCombo *fxCombo = fxParam->fx;
	const char *fxName = fxCombo->stringvalue;
	const char *paramName = fxParam->name;
	MultiVal *paramValue = NULL;

	wleAEAppearanceApplyInit();

	if(param->is_specified) {
		paramValue = MultiValCreate();
		MultiValSetVec3(paramValue, &param->vecvalue);
	}

	wleAESetFXParamInDef(def, paramName, paramValue);

	if(paramValue) {
		MultiValDestroy(paramValue);
	}

	wleOpPropsEnd();
}

static void wleAEApplyFXParamFloat(WleAEParamFloat *param, void *func_data, EditorObject *obj) {

	WleAEFXParam *fxParam = (WleAEFXParam *)func_data;
	WleAEParamCombo *fxCombo = fxParam->fx;
	const char *fxName = fxCombo->stringvalue;
	const char *paramName = fxParam->name;
	MultiVal *paramValue = NULL;

	wleAEAppearanceApplyInit();

	if(param->is_specified) {
		paramValue = MultiValCreate();
		MultiValSetFloat(paramValue, param->floatvalue);
	}

	wleAESetFXParamInDef(def, paramName, paramValue);

	if(paramValue) {
		MultiValDestroy(paramValue);
	}

	wleOpPropsEnd();
}

static void wleAEApplyFXParamString(WleAEParamText *param, void *func_data, EditorObject *obj) {

	WleAEFXParam *fxParam = (WleAEFXParam *)func_data;
	WleAEParamCombo *fxCombo = fxParam->fx;
	const char *fxName = fxCombo->stringvalue;
	const char *paramName = fxParam->name;
	MultiVal *paramValue = NULL;

	wleAEAppearanceApplyInit();

	if(param->is_specified) {
		paramValue = MultiValCreate();
		MultiValSetString(paramValue, param->stringvalue ? param->stringvalue : "");
	}

	wleAESetFXParamInDef(def, paramName, paramValue);

	if(paramValue) {
		MultiValDestroy(paramValue);
	}

	wleOpPropsEnd();
}

static void wleAEApplyFXParamDict(WleAEParamDictionary *param, void *func_data, EditorObject *obj) {

	WleAEFXParam *fxParam = (WleAEFXParam *)func_data;
	WleAEParamCombo *fxCombo = fxParam->fx;
	const char *fxName = fxCombo->stringvalue;
	const char *paramName = fxParam->name;
	MultiVal *paramValue = NULL;

	wleAEAppearanceApplyInit();

	if(param->is_specified) {
		paramValue = MultiValCreate();
		MultiValSetString(paramValue, param->refvalue ? param->refvalue : "");
	}

	wleAESetFXParamInDef(def, paramName, paramValue);

	if(paramValue) {
		MultiValDestroy(paramValue);
	}

	wleOpPropsEnd();
}

/********************
* MAIN
********************/

void wleAEAppearanceRebuildUI(WleAEAppearanceUI *ui, StashTable materials, StashTable textures, EditorObject *edObj)
{
	Vec3 offsetMin = {-360, -1, -10};
	Vec3 offsetMax = {360, 1, 10};
	Vec3 offsetStep = {1.f, 0.01f, 0.1f};
	UIButton *button;

	wleAESwapsRebuildUI(&ui->swaps, materials, textures);

	// rebuild tint UI
	ui_RebuildableTreeInit(ui->autoWidget, &ui->scrollArea->widget.children, 0, 0, UIRTOptions_Default);
	wleAEHSVAddWidget(ui->autoWidget, "Tint color 0", "First tint color.", "tint_color0", &ui->data.tint0);
	if (ui->allowOffsets)
		wleAEVec3AddWidget(ui->autoWidget, "Tint offset 0", "Offset to first tint color across all children.", "tint_offset0", &ui->data.tint0Offset, offsetMin, offsetMax, offsetStep);
	wleAEHSVAddWidget(ui->autoWidget, "Tint color 1", "Second tint color, based on the \"Color1\" material property.", "tint_color1", &ui->data.tint1);
	wleAEHSVAddWidget(ui->autoWidget, "Tint color 2", "Third tint color, based on the \"Color2\" material property..", "tint_color2", &ui->data.tint2);
	wleAEHSVAddWidget(ui->autoWidget, "Tint color 3", "Fourth tint color, based on the \"Color3\" material property..", "tint_color3", &ui->data.tint3);
	wleAEFloatAddWidget(ui->autoWidget, "Alpha", "All children get this value multiplied against their alpha value.  Alpha is multiplicative against all children.", "alpha", &ui->data.alpha, 0.01f, 1.f, 0.01f);
	wleAEBoolAddWidget(ui->autoWidget, "No Vertex Lighting", "Disables static vertex lighting to prevent popping when animating.", "noVertexLighting", &ui->data.noVertexLighting);
	wleAEBoolAddWidget(ui->autoWidget, "Use Character Lighting", "Use character lighting for this world object to prevent popping when animating.", "useCharacterLighting", &ui->data.useCharacterLighting);
	wleAEBoolAddWidget(ui->autoWidget, "Animation", "Enables simple world animation (graphics only, collision does not change).", "hasAnimation", &ui->data.hasAnimation);
	
	wleAEComboAddWidget(ui->autoWidget, "FX", "The attached FX.", "fx", &ui->data.fx);
	if (ui->data.fx.stringvalue && ui->data.fx.stringvalue[0] && (edObj->type->objType != EDTYPE_DUMMY))
	{
		GroupTracker *tracker;
		GroupDef *def;
			
		// FX Parameters.
		REF_TO(DynFxInfo) hInfo;

		assert(edObj->type->objType == EDTYPE_TRACKER);
		tracker = trackerFromTrackerHandle(edObj->obj);
		def = tracker ? tracker->def : NULL;

		// Get the info for this FX.
		SET_HANDLE_FROM_STRING(hDynFxInfoDict, ui->data.fx.stringvalue, hInfo);
		if (GET_REF(hInfo))
		{
			if ((ui->data.pFXButton) &&
				ui_WidgetGroupRemove(&ui->autoWidget->old_root->groupWidget->children, UI_WIDGET(ui->data.pFXButton)))	//This line prevent the button from being destroyed if it's going to be reused. Without this, refreshing the UI destroys the window, so it never opens
			{
				ui_FXButtonUpdate(ui->data.pFXButton, GET_REF(hInfo), &ui->data.fxHueShift.huevalue, &def->property_structs.fx_properties->pcParams);
			}
			else
			{
				ui->data.pFXButton = ui_FXButtonCreate(0, 0, GET_REF(hInfo), &ui->data.fxHueShift.huevalue, &def->property_structs.fx_properties->pcParams);
				ui_WidgetSetPositionEx(UI_WIDGET(ui->data.pFXButton), 15, 0, 0, 0, UITopLeft);
				ui_FXButtonSetChangedCallback(ui->data.pFXButton, trackerNotifyOnFXChanged, trackerFromTrackerHandle(edObj->obj));
				ui_FXButtonSetChangedCallbackShort(ui->data.pFXButton, wleAEHueChanged, &ui->data.fxHueShift);
				ui_FXButtonSetStopCallback(ui->data.pFXButton, trackerStopOnFXChanged, trackerFromTrackerHandle(edObj->obj));
			}
			ui_RebuildableTreeAddWidget(ui->autoWidget->root, UI_WIDGET(ui->data.pFXButton), NULL, true, "FX Button", NULL);
		}
		REMOVE_HANDLE(hInfo);

		ui->data.fxCondition.entry_width = 1;
		wleAEComboAddWidget(ui->autoWidget, "Condition", "Only play FX if this condition is met.", "fx_condition", &ui->data.fxCondition);
		ui->data.fxFaction.entry_width = 1;
		wleAEDictionaryAddWidget(ui->autoWidget, "Faction", "The relative faction of the FX", "fx_faction", &ui->data.fxFaction);
		
		wleAEBoolAddWidget(ui->autoWidget, "Has Target", "If set then a target node will be created for the fx", "fx_has_target", &ui->data.fxHasTarget);
		if(ui->data.fxHasTarget.boolvalue) 
		{
			Vec3 targetMin, targetMax, targetStep;

			wleAEBoolAddWidget(ui->autoWidget, "Static Target", "If set then the target node will not use any world animations.  Even those from parent groups will be ignored.", "fx_target_no_anim", &ui->data.fxTargetNoAnim);

			setVec3same(targetMin, -30000);
			setVec3same(targetMax, 30000);
			setVec3same(targetStep, 0.01);
			wleAEVec3AddWidget(ui->autoWidget, "Target Pos", "Position of the target node relative to the FX", "fx_target_pos", &ui->data.fxTargetPos, targetMin, targetMax, targetStep);

			setVec3same(targetMin, -180);
			setVec3same(targetMax, 180);
			setVec3same(targetStep, 0.1);
			wleAEVec3AddWidget(ui->autoWidget, "Target Pyr", "Pyr of the target node relative to the FX", "fx_target_pyr", &ui->data.fxTargetPyr, targetMin, targetMax, targetStep);

			if(edObjSelectionGetCount(EDTYPE_NONE) == 1)
			{
				UIAutoWidgetParams params = {0};
				button = ui_ButtonCreate(" Select Target Node ", 0, 0, wleAESetFxSelectTargetNode, NULL);
				params.alignTo = 150;
				ui_RebuildableTreeAddWidget(ui->autoWidget->root, UI_WIDGET(button), NULL, true, "select_target_node", &params);
			}
		}
	}
	else if (ui->data.pFXButton)
	{
		//It's in the tree... The system will delete it
		ui->data.pFXButton = NULL;
	}

	if (ui->data.hasAnimation.boolvalue)
	{
		Vec3 amountMin, amountMax, amountStep;
		Vec3 rateMin, rateMax, rateStep;

		wleAEBoolAddWidget(ui->autoWidget, "Local Pivot", "Specifies that the world animation should be done around the local pivot of each child.", "localSpace", &ui->data.localSpace);

		setVec3same(rateMin, 0);
		setVec3same(rateMax, 1000);
		setVec3same(rateStep, 0.1);

		setVec3same(amountMin, -10000.f);
		setVec3same(amountMax, 10000.f);
		setVec3same(amountStep, 0.1f);
		wleAEVec3AddWidget(ui->autoWidget, "Translation", "The distance which the object should move along each axis.", "translationAmount", &ui->data.translationAmount, amountMin, amountMax, amountStep);
		wleAEVec3AddWidget(ui->autoWidget, "Translation Time", "The time (in seconds) for the object do complete one cycle of translation.", "translationTime", &ui->data.translationTime, rateMin, rateMax, rateStep);
		wleAEBoolAddWidget(ui->autoWidget, "Loop Translation", "Specifies that the translation should loop instead of ping-pong.", "translationLoop", &ui->data.translationLoop);

		setVec3same(amountMin, 0);
		setVec3same(amountMax, 180);
		setVec3same(amountStep, 1);
		wleAEVec3AddWidget(ui->autoWidget, "Sway Angle", "The angle (in degrees) to which the object should sway.", "swayAngle", &ui->data.swayAngle, amountMin, amountMax, amountStep);
		wleAEVec3AddWidget(ui->autoWidget, "Sway Time", "The time (in seconds) for the object to take to complete one cycle of swaying.", "swayTime", &ui->data.swayTime, rateMin, rateMax, rateStep);

		setVec3same(amountMin, 0.01f);
		setVec3same(amountMax, 2);
		setVec3same(amountStep, 0.01f);
		wleAEVec3AddWidget(ui->autoWidget, "Scale Amount", "The scale to which the object should go.", "scaleAmount", &ui->data.scaleAmount, amountMin, amountMax, amountStep);
		wleAEVec3AddWidget(ui->autoWidget, "Scale Time", "The time (in seconds) for the object do complete one cycle of scaling.", "scaleTime", &ui->data.scaleTime, rateMin, rateMax, rateStep);

		setVec3same(rateMin, -500000);
		setVec3same(rateMax, 500000);
		wleAEVec3AddWidget(ui->autoWidget, "Rotation Time", "The time (in seconds) for the object to complete one rotation.  Negative values make is rotate in the negative direction.", "rotationTime", &ui->data.rotationTime, rateMin, rateMax, rateStep);

		wleAEBoolAddWidget(ui->autoWidget, "Random Time Offset", "Specifies that the time offset for these animations should be randomized.", "randomTimeOffset", &ui->data.randomTimeOffset);
		if (!ui->data.randomTimeOffset.boolvalue)
			wleAEFloatAddWidget(ui->autoWidget, "Time Offset", "Specifies the time offset (in seconds) for these animations.", "timeOffset", &ui->data.timeOffset, 0, 1000, 0.2f);
	}
	ui_RebuildableTreeDoneBuilding(ui->autoWidget);

	// set heights
	ui->scrollArea->widget.height = elUIGetEndY(ui->scrollArea->widget.children[0]->children);
	emPanelUpdateHeight(ui->panel);
}

void wleAEAppearanceUICreate(EMPanel *panel, WleAEAppearanceUI *ui)
{
	F32 startY;

	if (ui->autoWidget)
		return;

	startY = wleAESwapsUICreate(&ui->swaps, panel, NULL, NULL, wleAEAppearanceSetSwaps, &wleAEGlobalAppearanceUI, MATERIAL_SWAP | TEXTURE_SWAP, true, 0, 0);

	ui->panel = panel;

	// initialize auto widget and scroll area
	ui->autoWidget = ui_RebuildableTreeCreate();
	ui->scrollArea = ui_ScrollAreaCreate(0, startY + 5, 1, 0, 0, 0, false, false);
	ui->scrollArea->widget.widthUnit = UIUnitPercentage;
	emPanelAddChild(panel, ui->scrollArea, false);

	// set common parameter settings
	ui->data.tint0.can_unspecify = true;
	ui->data.tint0.entry_align = 75;
	ui->data.tint0Offset.can_unspecify = true;
	ui->data.tint0Offset.entry_align = 75;
	ui->data.tint0Offset.entry_width = 75;
	ui->data.tint0Offset.precision = 2;
	ui->data.tint1.can_unspecify = true;
	ui->data.tint1.entry_align = 75;
	ui->data.tint2.can_unspecify = true;
	ui->data.tint2.entry_align = 75;
	ui->data.tint3.can_unspecify = true;
	ui->data.tint3.entry_align = 75;

	ui->data.alpha.entry_align = 75;
	ui->data.alpha.entry_width = 75;
	ui->data.alpha.default_value = 1;

	ui->data.noVertexLighting.entry_align = 75;
	ui->data.hasAnimation.entry_align = 75;
	ui->data.localSpace.entry_align = 75;
	ui->data.localSpace.left_pad = 20;
	ui->data.translationAmount.entry_align = 75;
	ui->data.translationAmount.left_pad = 20;
	ui->data.translationTime.entry_align = 75;
	ui->data.translationTime.left_pad = 20;
	ui->data.translationLoop.entry_align = 75;
	ui->data.translationLoop.left_pad = 20;
	ui->data.swayAngle.entry_align = 75;
	ui->data.swayAngle.left_pad = 20;
	ui->data.swayTime.entry_align = 75;
	ui->data.swayTime.left_pad = 20;
	ui->data.rotationTime.entry_align = 75;
	ui->data.rotationTime.left_pad = 20;
	ui->data.scaleAmount.entry_align = 75;
	ui->data.scaleAmount.left_pad = 20;
	ui->data.scaleTime.entry_align = 75;
	ui->data.scaleTime.left_pad = 20;
	ui->data.randomTimeOffset.entry_align = 75;
	ui->data.randomTimeOffset.left_pad = 20;
	ui->data.timeOffset.entry_align = 75;
	ui->data.timeOffset.left_pad = 40;



	ui->data.fx.entry_align = 110;
	ui->data.fxHueShift.entry_align = 110;
	ui->data.fxCondition.entry_align = 110;

	ui->data.fx.can_unspecify = true;
	ui->data.fx.is_filtered = true;
	ui->data.fx.entry_width = 1.0;
	ui->data.fx.apply_func = wleAEMiscPropFxApply;
	ui->data.fxHueShift.entry_width = 70;
	ui->data.fxHueShift.slider_width = 100;
	ui->data.fxHueShift.left_pad = 20;
	ui->data.fxHueShift.precision = 1;
	ui->data.fxCondition.left_pad = 20;
	ui->data.fxCondition.entry_width = 130;
	ui->data.fxCondition.can_unspecify = true;
	
	ui->data.fxHasTarget.entry_align = 110;
	ui->data.fxHasTarget.left_pad = 20;

	ui->data.fxTargetNoAnim.entry_align = 110;
	ui->data.fxTargetNoAnim.left_pad = 20;

	ui->data.fxTargetPos.entry_align = 110;
	ui->data.fxTargetPos.left_pad = 20;
	ui->data.fxTargetPyr.entry_align = 110;
	ui->data.fxTargetPyr.left_pad = 20;

	ui->data.fxFaction.entry_align = 110;
	ui->data.fxFaction.entry_width = 130;
	ui->data.fxFaction.left_pad = 20;

}

/********************
* GROUPTREE-SPECIFIC
********************/
static void wleAEAppearanceTint0Update(WleAEParamHSV *param, WleAEAppearanceUI *ui, EditorObject *obj)
{
	wleAEAppearanceUpdateInit();

	if (!def || !(def->group_flags & GRP_HAS_TINT))
	{
		GroupTracker *parent;
		assert(tracker);
		parent = tracker->parent;
		param->is_specified = false;
		while (parent && (!parent->def || !(parent->def->group_flags & GRP_HAS_TINT)))
			parent = parent->parent;

		if (parent)
		{
			param->source = editorObjectCreate(trackerHandleCreate(parent), parent->def->name_str, parent->parent_layer, EDTYPE_TRACKER);
			rgbToHsv(parent->def->tint_color0, param->hsvvalue);
		}
		else
			copyVec3(zerovec3, param->hsvvalue);
	}
	else
	{
		param->is_specified = true;
		rgbToHsv(def->tint_color0, param->hsvvalue);
	}
}

static void wleAEAppearanceTint0Apply(WleAEParamHSV *param, WleAEAppearanceUI *ui, EditorObject **objs)
{
	int i;
	for (i = 0; i < eaSize(&objs); i++)
	{
		wleAEAppearanceApplyInitAt(i);

		if (param->is_specified)
		{
			Vec3 color;
			hsvToRgb(param->hsvvalue, color);
			groupSetTintColor(def, color);
		}
		else
		{
			groupRemoveTintColor(def);
		}
		groupdbDirtyTracker(tracker, UPDATE_GROUP_PROPERTIES);
		wleOpPropsEndNoUIUpdate();
	}
	wleOpRefreshUI();
}

static void wleAEAppearanceFxPyrUpdate(WleAEParamVec3 *param, WleAEAppearanceUI *ui, EditorObject *obj)
{
	wleAEAppearanceUpdateInit();

	copyVec3(zerovec3, param->vecvalue);
	if(def && def->property_structs.fx_properties)
	{
		param->vecvalue[0] = DEG(def->property_structs.fx_properties->vTargetPyr[0]);
		param->vecvalue[1] = DEG(def->property_structs.fx_properties->vTargetPyr[1]);
		param->vecvalue[2] = DEG(def->property_structs.fx_properties->vTargetPyr[2]);
	}
}

static void wleAEAppearanceFxPyrApply(WleAEParamVec3 *param, WleAEAppearanceUI *ui, EditorObject **objs)
{
	int i;
	for (i = 0; i < eaSize(&objs); i++)
	{
		wleAEAppearanceApplyInitAt(i);
		if(def)
		{
			if(!def->property_structs.fx_properties)
				def->property_structs.fx_properties = StructCreate(parse_WorldFXProperties);
			def->property_structs.fx_properties->vTargetPyr[0] = RAD(param->vecvalue[0]);
			def->property_structs.fx_properties->vTargetPyr[1] = RAD(param->vecvalue[1]);
			def->property_structs.fx_properties->vTargetPyr[2] = RAD(param->vecvalue[2]);
		}
		wleOpPropsEndNoUIUpdate();
	}
	wleOpRefreshUI();
}

static void wleAEAppearanceTint0OffsetUpdate(WleAEParamVec3 *param, WleAEAppearanceUI *ui, EditorObject *obj)
{
	wleAEAppearanceUpdateInit();

	if (!def || !(def->group_flags & GRP_HAS_TINT_OFFSET))
	{
		param->is_specified = false;
		copyVec3(zerovec3, param->vecvalue);
	}
	else
	{
		param->is_specified = true;
		copyVec3(def->tint_color0_offset, param->vecvalue);
	}
}

static void wleAEAppearanceTint0OffsetApply(WleAEParamVec3 *param, WleAEAppearanceUI *ui, EditorObject **objs)
{
	int i;
	for (i = 0; i < eaSize(&objs); i++)
	{
		wleAEAppearanceApplyInitAt(i);

		if (param->is_specified)
		{
			groupSetTintOffset(def, param->vecvalue);
		}
		else
		{
			groupRemoveTintOffset(def);
		}
		groupdbDirtyTracker(tracker, UPDATE_GROUP_PROPERTIES);
		wleOpPropsEndNoUIUpdate();
	}
	wleOpRefreshUI();
}

static void wleAEAppearanceMaterialTintUpdate(WleAEParamHSV *param, WleAEAppearanceUI *ui, EditorObject *obj)
{
	const char *materialProperty;
	Vec4 color;
	wleAEAppearanceUpdateInit();

	if (param == &ui->data.tint1)
		materialProperty = "Color1";
	else if (param == &ui->data.tint2)
		materialProperty = "Color2";
	else
		materialProperty = "Color3";

	if (!def || !groupGetMaterialPropertyVec4(def, materialProperty, color))
	{
		GroupTracker *parent;
		assert(tracker);
		parent = tracker->parent;
		param->is_specified = false;
		while (parent && (!parent->def || !groupGetMaterialPropertyVec4(parent->def, materialProperty, color)))
			parent = parent->parent;

		if (parent)
		{
			param->source = editorObjectCreate(trackerHandleCreate(parent), parent->def->name_str, parent->parent_layer, EDTYPE_TRACKER);
			rgbToHsv(color, param->hsvvalue);
			param->hsvvalue[3] = color[3];
		}
		else
			copyVec4(zerovec4, param->hsvvalue);
	}
	else
	{
		param->is_specified = true;
		rgbToHsv(color, param->hsvvalue);
		param->hsvvalue[3] = color[3];
	}
}

static void wleAEAppearanceMaterialTintApply(WleAEParamHSV *param, WleAEAppearanceUI *ui, EditorObject **objs)
{
	int i;
	for (i = 0; i < eaSize(&objs); i++)
	{
		const char *materialProperty;
		Vec4 color;
		wleAEAppearanceApplyInitAt(i);

		if (param == &ui->data.tint1)
		{
			materialProperty = "Color1";
		}
		else if (param == &ui->data.tint2)
		{
			materialProperty = "Color2";
		}
		else
		{
			materialProperty = "Color3";
		}
		if (param->is_specified)
		{
			hsvToRgb(param->hsvvalue, color);
			color[3] = param->hsvvalue[3];
			groupSetMaterialPropertyVec4(def, materialProperty, color);
		}
		else
		{
			groupRemoveMaterialProperty(def, materialProperty);
		}
		groupdbDirtyTracker(tracker, UPDATE_GROUP_PROPERTIES);
		wleOpPropsEndNoUIUpdate();
	}
	wleOpRefreshUI();
}

static void wleAEAppearanceNoVertexLightingUpdate(WleAEParamBool *param, void *unused, EditorObject *obj)
{
	wleAEAppearanceUpdateInit();

	if (!def || !def->property_structs.physical_properties.bNoVertexLighting)
		param->boolvalue = false;
	else
		param->boolvalue = true;
}

static void wleAEAppearanceNoVertexLightingApply(WleAEParamBool *param, void *unused, EditorObject **objs)
{
	int i;
	for (i = 0; i < eaSize(&objs); i++)
	{
		wleAEAppearanceApplyInitAt(i);

		if (def)
		{
			def->property_structs.physical_properties.bNoVertexLighting = !!param->boolvalue;
		}
		groupdbDirtyTracker(tracker, UPDATE_GROUP_PROPERTIES);
		wleOpPropsEndNoUIUpdate();
	}
	wleOpRefreshUI();
}

static void wleAEAppearanceUseCharacterLightingUpdate(WleAEParamBool *param, void *unused, EditorObject *obj)
{
	wleAEAppearanceUpdateInit();

	if (!def || !def->property_structs.physical_properties.bUseCharacterLighting)
		param->boolvalue = false;
	else
		param->boolvalue = true;
}

static void wleAEAppearanceUseCharacterLightingApply(WleAEParamBool *param, void *unused, EditorObject **objs)
{
	int i;
	for (i = 0; i < eaSize(&objs); i++)
	{
		wleAEAppearanceApplyInitAt(i);

		if (def)
		{
			def->property_structs.physical_properties.bUseCharacterLighting = !!param->boolvalue;
		}
		groupdbDirtyTracker(tracker, UPDATE_GROUP_PROPERTIES);
		wleOpPropsEndNoUIUpdate();
	}
	wleOpRefreshUI();
}

static void wleAEAppearanceHasAnimationUpdate(WleAEParamBool *param, void *unused, EditorObject *obj)
{
	wleAEAppearanceUpdateInit();

	if (!def || !def->property_structs.animation_properties)
		param->boolvalue = false;
	else
		param->boolvalue = true;
}

static void wleAEAppearanceHasAnimationApply(WleAEParamBool *param, void *unused, EditorObject **objs)
{
	int i;
	for (i = 0; i < eaSize(&objs); i++)
	{
		wleAEAppearanceApplyInitAt(i);

		if (def)
		{
			if (param->boolvalue)
			{
				if (!def->property_structs.animation_properties)
				{
					def->property_structs.animation_properties = StructAlloc(parse_WorldAnimationProperties);
					setVec3same(def->property_structs.animation_properties->scale_amount, 1);
					def->property_structs.animation_properties->time_offset = -1;
					groupdbDirtyTracker(tracker, UPDATE_GROUP_PROPERTIES);
				}
			}
			else if (def->property_structs.animation_properties)
			{
				StructDestroySafe(parse_WorldAnimationProperties, &def->property_structs.animation_properties);
				groupdbDirtyTracker(tracker, UPDATE_GROUP_PROPERTIES);
			}
		}
		wleOpPropsEndNoUIUpdate();
	}
	wleOpRefreshUI();
}

static void wleAEAppearanceAnimationUpdate(void *param, void *unused, EditorObject *obj)
{
	wleAEAppearanceUpdateInit();

	if (def && def->property_structs.animation_properties)
	{
		wleAEGlobalAppearanceUI.data.localSpace.boolvalue = def->property_structs.animation_properties->local_space;
		copyVec3(def->property_structs.animation_properties->translation_amount, wleAEGlobalAppearanceUI.data.translationAmount.vecvalue);
		copyVec3(def->property_structs.animation_properties->translation_time, wleAEGlobalAppearanceUI.data.translationTime.vecvalue);
		wleAEGlobalAppearanceUI.data.translationLoop.boolvalue = def->property_structs.animation_properties->translation_loop;
		wleAEGlobalAppearanceUI.data.swayAngle.vecvalue[0] = DEG(def->property_structs.animation_properties->sway_angle[0]);
		wleAEGlobalAppearanceUI.data.swayAngle.vecvalue[1] = DEG(def->property_structs.animation_properties->sway_angle[1]);
		wleAEGlobalAppearanceUI.data.swayAngle.vecvalue[2] = DEG(def->property_structs.animation_properties->sway_angle[2]);
		copyVec3(def->property_structs.animation_properties->sway_time, wleAEGlobalAppearanceUI.data.swayTime.vecvalue);
		copyVec3(def->property_structs.animation_properties->rotation_time, wleAEGlobalAppearanceUI.data.rotationTime.vecvalue);
		copyVec3(def->property_structs.animation_properties->scale_amount, wleAEGlobalAppearanceUI.data.scaleAmount.vecvalue);
		copyVec3(def->property_structs.animation_properties->scale_time, wleAEGlobalAppearanceUI.data.scaleTime.vecvalue);
		wleAEGlobalAppearanceUI.data.randomTimeOffset.boolvalue = def->property_structs.animation_properties->time_offset < 0;
		wleAEGlobalAppearanceUI.data.timeOffset.floatvalue = wleAEGlobalAppearanceUI.data.randomTimeOffset.boolvalue ? 0 : def->property_structs.animation_properties->time_offset;
	}
	else
	{
		wleAEGlobalAppearanceUI.data.localSpace.boolvalue = false;
		zeroVec3(wleAEGlobalAppearanceUI.data.swayAngle.vecvalue);
		zeroVec3(wleAEGlobalAppearanceUI.data.swayTime.vecvalue);
		zeroVec3(wleAEGlobalAppearanceUI.data.rotationTime.vecvalue);
		setVec3same(wleAEGlobalAppearanceUI.data.scaleAmount.vecvalue, 1);
		zeroVec3(wleAEGlobalAppearanceUI.data.scaleTime.vecvalue);
		wleAEGlobalAppearanceUI.data.randomTimeOffset.boolvalue = false;
		wleAEGlobalAppearanceUI.data.timeOffset.floatvalue = 0;
	}
}

static void wleAEAppearanceAnimationApply(void *param, void *unused, EditorObject **objs)
{
	int i;
	for (i = 0; i < eaSize(&objs); i++)
	{
		wleAEAppearanceApplyInitAt(i);

		if (def && def->property_structs.animation_properties)
		{
			def->property_structs.animation_properties->local_space = wleAEGlobalAppearanceUI.data.localSpace.boolvalue;
			copyVec3(wleAEGlobalAppearanceUI.data.translationAmount.vecvalue, def->property_structs.animation_properties->translation_amount);
			copyVec3(wleAEGlobalAppearanceUI.data.translationTime.vecvalue, def->property_structs.animation_properties->translation_time);
			def->property_structs.animation_properties->translation_loop = wleAEGlobalAppearanceUI.data.translationLoop.boolvalue;
			def->property_structs.animation_properties->sway_angle[0] = RAD(wleAEGlobalAppearanceUI.data.swayAngle.vecvalue[0]);
			def->property_structs.animation_properties->sway_angle[1] = RAD(wleAEGlobalAppearanceUI.data.swayAngle.vecvalue[1]);
			def->property_structs.animation_properties->sway_angle[2] = RAD(wleAEGlobalAppearanceUI.data.swayAngle.vecvalue[2]);
			copyVec3(wleAEGlobalAppearanceUI.data.swayTime.vecvalue, def->property_structs.animation_properties->sway_time);
			copyVec3(wleAEGlobalAppearanceUI.data.rotationTime.vecvalue, def->property_structs.animation_properties->rotation_time);
			copyVec3(wleAEGlobalAppearanceUI.data.scaleAmount.vecvalue, def->property_structs.animation_properties->scale_amount);
			copyVec3(wleAEGlobalAppearanceUI.data.scaleTime.vecvalue, def->property_structs.animation_properties->scale_time);
			def->property_structs.animation_properties->time_offset = wleAEGlobalAppearanceUI.data.randomTimeOffset.boolvalue ? -1 : wleAEGlobalAppearanceUI.data.timeOffset.floatvalue;
			groupdbDirtyTracker(tracker, UPDATE_GROUP_PROPERTIES);
		}
		wleOpPropsEndNoUIUpdate();
	}
	wleOpRefreshUI();
}

static bool wleAEAppearanceHasProps(GroupDef *def)
{
	Vec3 color;

	return (def->property_structs.animation_properties ||
		!!(def->group_flags & (GRP_HAS_TINT | GRP_HAS_TINT_OFFSET)) ||
		groupGetMaterialPropertyVec3(def, "Color1", color) ||
		(def->property_structs.fx_properties && def->property_structs.fx_properties->pcName));
}

int wleAEAppearanceReload(EMPanel *panel, EditorObject *obj)
{
	StashTable allMaterials = stashTableCreateWithStringKeys(16, StashDefault);
	StashTable allTextures = stashTableCreateWithStringKeys(16, StashDefault);
	ResourceDictionaryInfo *resDictInfo;
	EditorObject **objects = NULL;
	WorldScope *closest_scope = NULL;
	bool common_scope = false;
	bool panelActive = true;
	bool hide = false;
	bool hasProps = false;
	int i;

	wleAEGetSelectedObjects(&objects);
	for (i = 0; i < eaSize(&objects); i++)
	{
		StashTable materials = stashTableCreateWithStringKeys(16, StashDefault);
		StashTable textures = stashTableCreateWithStringKeys(16, StashDefault);
		StashTableIterator iter;
		StashElement el;
		GroupTracker *tracker;

		assert(objects[i]->type->objType == EDTYPE_TRACKER);
		tracker = trackerFromTrackerHandle(objects[i]->obj);
		if (!tracker || !tracker->def || wleNeedsEncounterPanels(tracker->def))
		{
			hide = true;
			break;
		}

		if (!wleTrackerIsEditable(objects[i]->obj, false, false, false))
			panelActive = false;

		// get closest scope for trigger conditions
		if (!closest_scope)
		{
			common_scope = true;
			closest_scope = tracker->closest_scope;
		}
		else if (closest_scope != tracker->closest_scope)
			common_scope = false;

		// get tracker's materials and textures
		wleGetTrackerTexMats(tracker, materials, textures, NULL, false);

		// find the current tracker's swaps and set them in the stash
		if (tracker->def)
		{
			hasProps = (eaSize(&tracker->def->material_swaps) > 0) || (eaSize(&tracker->def->texture_swaps) > 0);

			if (!hasProps && wleAEAppearanceHasProps(tracker->def))
				hasProps = true;
		}

		// find diffs with overall mat/tex stashes
		if (!i)
		{
			wleAEAppearanceStashMerge(allMaterials, materials);
			wleAEAppearanceStashMerge(allTextures, textures);
		}
		else
		{
			stashGetIterator(allMaterials, &iter);
			while (stashGetNextElement(&iter, &el))
			{
				char *origName = stashElementGetStringKey(el);
				char *replaceName = stashElementGetPointer(el);
				char *localReplace;

				if (!stashFindPointer(materials, origName, &localReplace))
					stashRemovePointer(allMaterials, origName, NULL);
				else if (replaceName != localReplace)
					stashElementSetPointer(el, (void*) allocAddString("--DIFFERENT--"));
			}
			stashGetIterator(allTextures, &iter);
			while (stashGetNextElement(&iter, &el))
			{
				char *origName = stashElementGetStringKey(el);
				char *replaceName = stashElementGetPointer(el);
				char *localReplace;

				if (!stashFindPointer(textures, origName, &localReplace))
					stashRemovePointer(allTextures, origName, NULL);
				else if (replaceName != localReplace)					
					stashElementSetPointer(el, (void*) allocAddString("--DIFFERENT--"));
			}
		}

		stashTableDestroy(materials);
		stashTableDestroy(textures);
	}
	eaDestroy(&objects);

	if (hide)
		return WLE_UI_PANEL_INVALID;

	// update combo contents
	eaDestroy(&wleAEGlobalAppearanceUI.triggerConditionNames);
	eaPush(&wleAEGlobalAppearanceUI.triggerConditionNames, "");
	if (common_scope && closest_scope && closest_scope->name_to_obj)
		worldGetObjectNames(WL_ENC_TRIGGER_CONDITION, &wleAEGlobalAppearanceUI.triggerConditionNames, closest_scope);
	wleAEGlobalAppearanceUI.data.fxCondition.available_values = wleAEGlobalAppearanceUI.triggerConditionNames;

	eaDestroyEx(&wleAEGlobalAppearanceUI.data.fx.available_values, NULL);
	resDictInfo = resDictGetInfo("DynFxInfo");
	for (i = 0; i < eaSize(&resDictInfo->ppInfos); i++)
	{
		ResourceInfo *resInfo = resDictInfo->ppInfos[i];
		if (resInfo->resourceName &&
			(strstri(resInfo->resourceName, "FX_") == resInfo->resourceName || strstri(resInfo->resourceName, "CFX_") == resInfo->resourceName))
			eaPush(&wleAEGlobalAppearanceUI.data.fx.available_values, strdup(resInfo->resourceName));
	}

	// fill data
	wleAEHSVUpdate(&wleAEGlobalAppearanceUI.data.tint0);
	wleAEVec3Update(&wleAEGlobalAppearanceUI.data.tint0Offset);
	wleAEHSVUpdate(&wleAEGlobalAppearanceUI.data.tint1);
	wleAEHSVUpdate(&wleAEGlobalAppearanceUI.data.tint2);
	wleAEHSVUpdate(&wleAEGlobalAppearanceUI.data.tint3);
	wleAEFloatUpdate(&wleAEGlobalAppearanceUI.data.alpha);
	wleAEBoolUpdate(&wleAEGlobalAppearanceUI.data.noVertexLighting);
	wleAEBoolUpdate(&wleAEGlobalAppearanceUI.data.useCharacterLighting);
	wleAEBoolUpdate(&wleAEGlobalAppearanceUI.data.hasAnimation);
	wleAEBoolUpdate(&wleAEGlobalAppearanceUI.data.localSpace);
	wleAEVec3Update(&wleAEGlobalAppearanceUI.data.translationAmount);
	wleAEVec3Update(&wleAEGlobalAppearanceUI.data.translationTime);
	wleAEBoolUpdate(&wleAEGlobalAppearanceUI.data.translationLoop);
	wleAEVec3Update(&wleAEGlobalAppearanceUI.data.swayAngle);
	wleAEVec3Update(&wleAEGlobalAppearanceUI.data.swayTime);
	wleAEVec3Update(&wleAEGlobalAppearanceUI.data.rotationTime);
	wleAEVec3Update(&wleAEGlobalAppearanceUI.data.scaleAmount);
	wleAEVec3Update(&wleAEGlobalAppearanceUI.data.scaleTime);
	wleAEBoolUpdate(&wleAEGlobalAppearanceUI.data.randomTimeOffset);
	wleAEFloatUpdate(&wleAEGlobalAppearanceUI.data.timeOffset);

	wleAEComboUpdate(&wleAEGlobalAppearanceUI.data.fx);
	wleAEHueUpdate(&wleAEGlobalAppearanceUI.data.fxHueShift);
	wleAEComboUpdate(&wleAEGlobalAppearanceUI.data.fxCondition);
	wleAEBoolUpdate(&wleAEGlobalAppearanceUI.data.fxHasTarget);
	wleAEBoolUpdate(&wleAEGlobalAppearanceUI.data.fxTargetNoAnim);
	wleAEVec3Update(&wleAEGlobalAppearanceUI.data.fxTargetPos);
	wleAEVec3Update(&wleAEGlobalAppearanceUI.data.fxTargetPyr);
	wleAEDictionaryUpdate(&wleAEGlobalAppearanceUI.data.fxFaction);


	// rebuild UI
	wleAEAppearanceRebuildUI(&wleAEGlobalAppearanceUI, allMaterials, allTextures, obj);
	emPanelSetActive(wleAEGlobalAppearanceUI.panel, panelActive);

	ui_SetActive(UI_WIDGET(wleAEGlobalAppearanceUI.swaps.matSwapList), panelActive);
	ui_SetActive(UI_WIDGET(wleAEGlobalAppearanceUI.swaps.texSwapList), panelActive);
	ui_SetActive(UI_WIDGET(wleAEGlobalAppearanceUI.swaps.swapType), true);

	return WLE_UI_PANEL_OWNED;
}

void wleAEAppearanceCreate(EMPanel *panel)
{
	WleCriterion *crit;

	wleAEAppearanceUICreate(panel, &wleAEGlobalAppearanceUI);
	wleAEGlobalAppearanceUI.allowOffsets = 1;

	// set parameter settings
	wleAEGlobalAppearanceUI.data.tint0.update_func = wleAEAppearanceTint0Update;
	wleAEGlobalAppearanceUI.data.tint0.update_data = &wleAEGlobalAppearanceUI;
	wleAEGlobalAppearanceUI.data.tint0.apply_func = wleAEAppearanceTint0Apply;
	wleAEGlobalAppearanceUI.data.tint0.apply_data = &wleAEGlobalAppearanceUI;

	wleAEGlobalAppearanceUI.data.tint0Offset.can_unspecify = true;
	wleAEGlobalAppearanceUI.data.tint0Offset.update_func = wleAEAppearanceTint0OffsetUpdate;
	wleAEGlobalAppearanceUI.data.tint0Offset.update_data = &wleAEGlobalAppearanceUI;
	wleAEGlobalAppearanceUI.data.tint0Offset.apply_func = wleAEAppearanceTint0OffsetApply;
	wleAEGlobalAppearanceUI.data.tint0Offset.apply_data = &wleAEGlobalAppearanceUI;

	wleAEGlobalAppearanceUI.data.tint1.can_unspecify = true;
	wleAEGlobalAppearanceUI.data.tint1.add_alpha = true;
	wleAEGlobalAppearanceUI.data.tint1.update_func = wleAEAppearanceMaterialTintUpdate;
	wleAEGlobalAppearanceUI.data.tint1.update_data = &wleAEGlobalAppearanceUI;
	wleAEGlobalAppearanceUI.data.tint1.apply_func = wleAEAppearanceMaterialTintApply;
	wleAEGlobalAppearanceUI.data.tint1.apply_data = &wleAEGlobalAppearanceUI;

	wleAEGlobalAppearanceUI.data.tint2.can_unspecify = true;
	wleAEGlobalAppearanceUI.data.tint2.add_alpha = true;
	wleAEGlobalAppearanceUI.data.tint2.update_func = wleAEAppearanceMaterialTintUpdate;
	wleAEGlobalAppearanceUI.data.tint2.update_data = &wleAEGlobalAppearanceUI;
	wleAEGlobalAppearanceUI.data.tint2.apply_func = wleAEAppearanceMaterialTintApply;
	wleAEGlobalAppearanceUI.data.tint2.apply_data = &wleAEGlobalAppearanceUI;

	wleAEGlobalAppearanceUI.data.tint3.can_unspecify = true;
	wleAEGlobalAppearanceUI.data.tint3.add_alpha = true;
	wleAEGlobalAppearanceUI.data.tint3.update_func = wleAEAppearanceMaterialTintUpdate;
	wleAEGlobalAppearanceUI.data.tint3.update_data = &wleAEGlobalAppearanceUI;
	wleAEGlobalAppearanceUI.data.tint3.apply_func = wleAEAppearanceMaterialTintApply;
	wleAEGlobalAppearanceUI.data.tint3.apply_data = &wleAEGlobalAppearanceUI;

	wleAEGlobalAppearanceUI.data.alpha.property_name = "Alpha";

	wleAEGlobalAppearanceUI.data.fx.property_name = "Fx";
	wleAEGlobalAppearanceUI.data.fxHueShift.property_name = "FX_Hue";
	wleAEGlobalAppearanceUI.data.fxCondition.property_name = "FX_condition";
	wleAEGlobalAppearanceUI.data.fxHasTarget.property_name = "FX_Has_Target";
	wleAEGlobalAppearanceUI.data.fxTargetNoAnim.property_name = "FX_Target_No_Anim";
	wleAEGlobalAppearanceUI.data.fxTargetPos.property_name = "FX_Target_Pos";
	wleAEGlobalAppearanceUI.data.fxTargetPyr.update_func = wleAEAppearanceFxPyrUpdate;
	wleAEGlobalAppearanceUI.data.fxTargetPyr.update_data = &wleAEGlobalAppearanceUI;
	wleAEGlobalAppearanceUI.data.fxTargetPyr.apply_func = wleAEAppearanceFxPyrApply;
	wleAEGlobalAppearanceUI.data.fxTargetPyr.apply_data = &wleAEGlobalAppearanceUI;

	wleAEGlobalAppearanceUI.data.fxFaction.property_name = "FX_faction";
	wleAEGlobalAppearanceUI.data.fxFaction.dictionary = "CritterFaction";

	crit = StructCreate(parse_WleCriterion);
	eaiPush(&crit->allConds, WLE_CRIT_EQUAL);
	eaiPush(&crit->allConds, WLE_CRIT_NOT_EQUAL);
	eaiPush(&crit->allConds, WLE_CRIT_CONTAINS);
	eaiPush(&crit->allConds, WLE_CRIT_BEGINS_WITH);
	eaiPush(&crit->allConds, WLE_CRIT_ENDS_WITH);
	crit->checkCallback = wleAEMiscPropFxCritCheck;
	crit->propertyName = StructAllocString("FX");
	wleCriterionRegister(crit);


	wleAEGlobalAppearanceUI.data.noVertexLighting.update_func = wleAEAppearanceNoVertexLightingUpdate;
	wleAEGlobalAppearanceUI.data.noVertexLighting.apply_func = wleAEAppearanceNoVertexLightingApply;

	wleAEGlobalAppearanceUI.data.useCharacterLighting.update_func = wleAEAppearanceUseCharacterLightingUpdate;
	wleAEGlobalAppearanceUI.data.useCharacterLighting.apply_func = wleAEAppearanceUseCharacterLightingApply;

	wleAEGlobalAppearanceUI.data.hasAnimation.update_func = wleAEAppearanceHasAnimationUpdate;
	wleAEGlobalAppearanceUI.data.hasAnimation.apply_func = wleAEAppearanceHasAnimationApply;

	wleAEGlobalAppearanceUI.data.localSpace.update_func = wleAEAppearanceAnimationUpdate;
	wleAEGlobalAppearanceUI.data.localSpace.apply_func = wleAEAppearanceAnimationApply;

	wleAEGlobalAppearanceUI.data.translationAmount.update_func = wleAEAppearanceAnimationUpdate;
	wleAEGlobalAppearanceUI.data.translationAmount.apply_func = wleAEAppearanceAnimationApply;

	wleAEGlobalAppearanceUI.data.translationTime.update_func = wleAEAppearanceAnimationUpdate;
	wleAEGlobalAppearanceUI.data.translationTime.apply_func = wleAEAppearanceAnimationApply;

	wleAEGlobalAppearanceUI.data.translationLoop.update_func = wleAEAppearanceAnimationUpdate;
	wleAEGlobalAppearanceUI.data.translationLoop.apply_func = wleAEAppearanceAnimationApply;

	wleAEGlobalAppearanceUI.data.swayAngle.update_func = wleAEAppearanceAnimationUpdate;
	wleAEGlobalAppearanceUI.data.swayAngle.apply_func = wleAEAppearanceAnimationApply;

	wleAEGlobalAppearanceUI.data.swayTime.update_func = wleAEAppearanceAnimationUpdate;
	wleAEGlobalAppearanceUI.data.swayTime.apply_func = wleAEAppearanceAnimationApply;

	wleAEGlobalAppearanceUI.data.rotationTime.update_func = wleAEAppearanceAnimationUpdate;
	wleAEGlobalAppearanceUI.data.rotationTime.apply_func = wleAEAppearanceAnimationApply;

	wleAEGlobalAppearanceUI.data.scaleAmount.update_func = wleAEAppearanceAnimationUpdate;
	wleAEGlobalAppearanceUI.data.scaleAmount.apply_func = wleAEAppearanceAnimationApply;

	wleAEGlobalAppearanceUI.data.scaleTime.update_func = wleAEAppearanceAnimationUpdate;
	wleAEGlobalAppearanceUI.data.scaleTime.apply_func = wleAEAppearanceAnimationApply;

	wleAEGlobalAppearanceUI.data.randomTimeOffset.update_func = wleAEAppearanceAnimationUpdate;
	wleAEGlobalAppearanceUI.data.randomTimeOffset.apply_func = wleAEAppearanceAnimationApply;

	wleAEGlobalAppearanceUI.data.timeOffset.update_func = wleAEAppearanceAnimationUpdate;
	wleAEGlobalAppearanceUI.data.timeOffset.apply_func = wleAEAppearanceAnimationApply;
}

#endif

#include "WorldEditorAppearanceAttributes_h_ast.c"