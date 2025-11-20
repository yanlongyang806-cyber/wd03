#ifndef NO_EDITORS

#include "TerrainEditorPrivate.h"

#include "error.h"
#include "estring.h"
#include "StringCache.h"
#include "fileutil.h"
#include "FolderCache.h"
#include "gimmeDLLWrapper.h"
#include "inputMouse.h"
#include "tokenstore.h"
#include "Color.h"
#include "wlTerrainQueue.h"

#include "EditLibGizmos.h"
#include "EditorPrefs.h"

#include "GfxSprite.h"
#include "GfxTexAtlas.h"
#include "GfxTerrain.h"

AUTO_RUN_ANON(memBudgetAddMapping(__FILE__, BUDGET_Editors););

static UISkin *s_skinInactiveExpander = NULL;
static UISkin *s_skinActiveExpander = NULL;
static UISkin *s_skinBlueBack = NULL;
static UISkin *s_skinGreenBack = NULL;
static UISkin *s_skinRedBack = NULL;

//Get bottom or right ending point of a widget
#define widgetNextX(w) (w->widget.x + w->widget.width)
#define widgetNextY(w) (w->widget.y + w->widget.height)
#define TOOL_BAR_HEIGHT 20
#define TOOL_BAR_TOP 2

#endif
AUTO_COMMAND ACMD_CATEGORY(World, Interface) ACMD_NAME("Terrain.SelectBrushChannel");
void terrainSelectBrushChannel(int channel)
{
#ifndef NO_EDITORS
	TerrainDoc *doc = terEdGetDoc();
	if(	!doc ||
		channel < 0 || 
		channel >= TBC_NUM_CHANNELS)
		return;

	ui_ButtonComboSelectNext(doc->terrain_ui->channel_button_list[channel]);
#endif
}
#ifndef NO_EDITORS

AUTO_COMMAND;
void terrainPrintImageBuffers(void)
{
	int i;
	TerrainDoc *doc = terEdGetDoc();
	if (!doc)
		return;
	printf("*** IMAGE BUFFERS: ***\n");
	for (i = 0; i < eaSize(&doc->state.source->brush_images); i++)
		printf("IMAGE %d: %s\n", doc->state.source->brush_images[i]->ref_count, doc->state.source->brush_images[i]->file_name);
	printf("**********************\n");
}

TerrainCommonBrushParams* terEdGetCommonBrushParams(TerrainDoc *doc)
{
	if(doc->state.selected_brush)
		return &doc->state.selected_brush->common;
	else
		return &doc->state.multi_brush_common;
}

void terEdRefreshBrushUI(TerrainDoc *doc)
{
	//Set Common Brush Param Values
	TerrainCommonBrushParams* common = terEdGetCommonBrushParams(doc);
	ui_DropSliderTextEntrySetValueAndCallback(doc->terrain_ui->brush_size_slider, common->brush_diameter);
	ui_DropSliderTextEntrySetValueAndCallback(doc->terrain_ui->brush_hardness_slider, common->brush_hardness);
	ui_DropSliderTextEntrySetValueAndCallback(doc->terrain_ui->brush_strength_slider, common->brush_strength);
	ui_ComboBoxSetSelectedEnum(doc->terrain_ui->brush_shape_combo, common->brush_shape);
	ui_ButtonComboSetSelected(doc->terrain_ui->brush_falloff_combo, common->falloff_type);
}

#endif
AUTO_COMMAND ACMD_CATEGORY(World, Interface) ACMD_NAME("Terrain.IncreaseBrushSize");
void terrainIncreaseBrushSize()
{
#ifndef NO_EDITORS
	int i;
	int steps[6] = {1, 5, 10, 25, 100, 250};
	int current_size;
	TerrainCommonBrushParams* common;
	TerrainDoc *doc = terEdGetDoc();
	if(!doc)
		return;
	common = terEdGetCommonBrushParams(doc);

	current_size = common->brush_diameter;
	for(i=0; i < 5; i++)
	{
		if(steps[i]*10 > current_size)
			break;
	}
	current_size = (current_size - current_size%steps[i]) + steps[i];
	ui_DropSliderTextEntrySetValueAndCallback(doc->terrain_ui->brush_size_slider, current_size);
#endif
}

AUTO_COMMAND ACMD_CATEGORY(World, Interface) ACMD_NAME("Terrain.DecreaseBrushSize");
void terrainDecreaseBrushSize()
{
#ifndef NO_EDITORS
	int i;
	int steps[7] = {0, 1, 5, 10, 25, 100, 250};
	int current_size;
	TerrainCommonBrushParams* common;
	TerrainDoc *doc = terEdGetDoc();
	if(!doc)
		return;
	common = terEdGetCommonBrushParams(doc);

	current_size = common->brush_diameter;
	for(i=6; i > 1; i--)
	{
		if(steps[i-1]*10 < current_size)
			break;
	}
	current_size -= steps[i];
	current_size = current_size - current_size%steps[i];
	current_size = MAX(current_size, steps[i-1]*10);
	ui_DropSliderTextEntrySetValueAndCallback(doc->terrain_ui->brush_size_slider, current_size);
#endif
}

AUTO_COMMAND ACMD_CATEGORY(World, Interface) ACMD_NAME("Terrain.IncreaseBrushHardness");
void terrainIncreaseBrushHardness()
{
#ifndef NO_EDITORS
	int current_hardness;
	TerrainCommonBrushParams* common;
	TerrainDoc *doc = terEdGetDoc();
	if(!doc)
		return;
	common = terEdGetCommonBrushParams(doc);

	current_hardness = common->brush_hardness*100.0f + 0.2f;
	current_hardness = current_hardness - current_hardness%25 + 25;
	ui_DropSliderTextEntrySetValueAndCallback(doc->terrain_ui->brush_hardness_slider, current_hardness/100.0f);
#endif
}

AUTO_COMMAND ACMD_CATEGORY(World, Interface) ACMD_NAME("Terrain.DecreaseBrushHardness");
void terrainDecreaseBrushHardness()
{
#ifndef NO_EDITORS
	int current_hardness;
	TerrainCommonBrushParams* common;
	TerrainDoc *doc = terEdGetDoc();
	if(!doc)
		return;
	common = terEdGetCommonBrushParams(doc);

	current_hardness = common->brush_hardness*100.0f + 0.2f;
	current_hardness--;
	current_hardness = current_hardness - current_hardness%25;
	ui_DropSliderTextEntrySetValueAndCallback(doc->terrain_ui->brush_hardness_slider, current_hardness/100.0f);
#endif
}

AUTO_COMMAND ACMD_CATEGORY(World, Interface) ACMD_NAME("Terrain.SetBrushStrength");
void terrainSetBrushStrength(int strength)
{
#ifndef NO_EDITORS
	TerrainDoc *doc = terEdGetDoc();
	if(	!doc ||
		strength < 1 ||
		strength > 10)
		return;

	ui_DropSliderTextEntrySetValueAndCallback(doc->terrain_ui->brush_strength_slider, strength/10.0f);
#endif
}

AUTO_COMMAND ACMD_CATEGORY(World, Interface) ACMD_NAME("Terrain.SelectBrush");
void terrainSelectBrush(const char *brush_name)
{
#ifndef NO_EDITORS
	int i;
	RefDictIterator it;
	TerrainDefaultBrush *brush;
	TerrainDoc *doc = terEdGetDoc();
	if(!doc || !brush_name)
		return;
	
	RefSystem_InitRefDictIterator(DEFAULT_BRUSH_DICTIONARY, &it);
	while(brush = (TerrainDefaultBrush*)RefSystem_GetNextReferentFromIterator(&it)) 
	{
		if(stricmp(brush->name, brush_name) == 0)
		{
			if(brush->button)
				ui_ButtonClick(brush->button);
			for(i=0; i < TBC_NUM_CHANNELS; i++)
			{
				ui_ButtonComboSetActive(doc->terrain_ui->channel_button_list[i], (brush->brush_template.channel == i ? true : false));
			}
			return;
		}
	}
#endif
}

AUTO_COMMAND ACMD_CATEGORY(World, Interface) ACMD_NAME("Terrain.SelectEyeDropper");
void terrainSelectEyeDropper()
{
#ifndef NO_EDITORS
	TerrainDoc *doc = terEdGetDoc();
	if(!doc || !doc->terrain_ui->brush_eye_dropper)
		return;

	ui_ButtonClick(doc->terrain_ui->brush_eye_dropper);
#endif
}
#ifndef NO_EDITORS

void terEdRefreshDefaultBrushUI(TerrainDefaultBrush *selected_brush)
{
	int i;
	if(!selected_brush)
		return;

	for(i=0; i < eaSize(&selected_brush->brush_template.params); i++)
	{
		TerrainBrushTemplateParam *param = selected_brush->brush_template.params[i];
		if(param->refresh_func)
			param->refresh_func(param->widget_ptr, param->data);
	}
}

void terEdRefreshDefaultBrushesUI()
{
	RefDictIterator it;
	TerrainDefaultBrush *brush;

	RefSystem_InitRefDictIterator(DEFAULT_BRUSH_DICTIONARY, &it);
	while(brush = (TerrainDefaultBrush*)RefSystem_GetNextReferentFromIterator(&it)) 
	{
		terEdRefreshDefaultBrushUI(brush);
	}
}

#endif
AUTO_COMMAND ACMD_CATEGORY(World, Interface) ACMD_NAME("Terrain.SwapBrushColors");
void terrainSwapBrushColors()
{
#ifndef NO_EDITORS
	Color temp_color;
	TerrainDefaultBrush *selected_brush;
	TerrainDoc *doc = terEdGetDoc();
	if(!doc)
		return;
	selected_brush = doc->state.selected_brush;
	if(!selected_brush)
		return;
	if(stricmp(selected_brush->name, "Color") != 0)
		return;

	copyVec4(selected_brush->default_values.color_1.rgba, temp_color.rgba);
	copyVec4(selected_brush->default_values.color_2.rgba, selected_brush->default_values.color_1.rgba);
	copyVec4(temp_color.rgba, selected_brush->default_values.color_2.rgba);
	terEdRefreshDefaultBrushUI(selected_brush);
#endif
}

AUTO_COMMAND ACMD_CATEGORY(World, Interface) ACMD_NAME("Terrain.SetImageTransMode");
void terrainSetImageTransMode(int mode)
{
#ifndef NO_EDITORS
	TerrainBrushValues *seleted_image;
	TerrainDoc *doc = terEdGetDoc();
	if(!doc || mode > 1 || mode < 0)
		return;
	seleted_image = doc->state.seleted_image;
	if(!seleted_image)
		return;
	ui_ComboBoxSetSelectedEnumAndCallback(doc->terrain_ui->image_mode_combo, mode);
#endif
}
#ifndef NO_EDITORS

void terEdSetBrushHeight(TerrainEditorState *state, TerrainDefaultBrush *selected_brush, F32 x, F32 z)
{
	F32 height;

	if (!terrainSourceGetHeight(state->source, x, z, &height, NULL))
		return;

	selected_brush->default_values.float_1 = height;
	terEdRefreshDefaultBrushUI(selected_brush);
}

void terEdSetBrushColor(TerrainEditorState *state, TerrainDefaultBrush *selected_brush, F32 x, F32 z)
{
	Color color;

	if (!terrainSourceGetColor(state->source, x, z, &color, NULL))
		return;

	copyVec4(color.rgba, selected_brush->default_values.color_1.rgba);
 	terEdRefreshDefaultBrushUI(selected_brush);
}

void terEdSetBrushMaterial(TerrainEditorState *state, TerrainDefaultBrush *selected_brush, F32 x, F32 z)
{
	int i;
	int material_count=0;
	const char *material_names[NUM_MATERIALS];
	TerrainMaterialWeight *mat_weights;
	F32 largest_weight = 0.0f;
	int largest_weight_idx = 0;

	if (!(mat_weights = terrainSourceGetMaterialWeights(state->source, x, z, material_names, &material_count)))
		return;

	for( i=0; i < material_count; i++ )
	{
		if(mat_weights->weights[i] > largest_weight)
		{
			largest_weight_idx = i;
			largest_weight = mat_weights->weights[i];
		}
	}

	if(selected_brush->default_values.string_1)
		StructFreeString(selected_brush->default_values.string_1);

	selected_brush->default_values.string_1 = StructAllocString(material_names[largest_weight_idx]);
	terEdRefreshDefaultBrushUI(selected_brush);
}

void terEdSetBrushObject(TerrainEditorState *state, TerrainDefaultBrush *selected_brush, F32 x, F32 z)
{
	int i;
	TerrainObjectRef *obj_ref = &selected_brush->default_values.object_1;
	TerrainObjectEntry **entries = NULL;
	U32 *densities = NULL;
	F32 largest_density = 0.0f;
	int object_idx = 0;

	terrainSourceGetObjectDensities(state->source, x, z, &entries, &densities);

	if(eaiSize(&densities) == 0)
		return;

	for( i=0; i < eaiSize(&densities); i++ )
	{
		if(densities[i] > largest_density)
		{
			object_idx = i;
			largest_density = densities[i];
		}
	}

	
	if(obj_ref->name_str)
		StructFreeString(obj_ref->name_str);

	obj_ref->name_str = StructAllocString(entries[object_idx]->objInfo.name_str);
	obj_ref->name_uid = entries[object_idx]->objInfo.name_uid;

	terEdRefreshDefaultBrushUI(selected_brush);
	eaDestroy(&entries);
	eaiDestroy(&densities);
}

void terEdSetBrushSoilDepth(TerrainEditorState *state, TerrainDefaultBrush *selected_brush, F32 x, F32 z)
{
	F32 depth;

	if (!terrainSourceGetSoilDepth(state->source, x, z, &depth, NULL))
		return;

	selected_brush->default_values.float_1 = depth;
	terEdRefreshDefaultBrushUI(selected_brush);
}

static void terEdImageGizmoChanged(TerrainDoc *doc)
{
	Vec3 pyr;
	TerrainBrushValues *values = doc->state.seleted_image;
	if(values == NULL)
		return;

	pyr[0] = values->float_4;//P
	pyr[1] = values->float_5;//Y
	pyr[2] = values->float_6;//R
	createMat3YPR(doc->state.gizmo_matrix, pyr);
	doc->state.gizmo_matrix[3][0] = values->float_1;//X
	doc->state.gizmo_matrix[3][1] = values->float_2;//Y
	doc->state.gizmo_matrix[3][2] = values->float_3;//Z

	TranslateGizmoSetMatrix(doc->state.image_translate_gizmo, doc->state.gizmo_matrix);
	RotateGizmoSetMatrix(doc->state.image_rotate_gizmo, doc->state.gizmo_matrix);
}

void terEdPanelExpanderReflowCB(UIExpanderGroup *expander_group, EMPanel *panel)
{
	emPanelSetHeight(panel, expander_group->totalHeight + 5);	
}

static void terEdTemplateSliderTextEntryCB(UISliderTextEntry *slider_entry, bool bFinished, TerrainTokenStorageData *token_data)
{
	float value;
	TerrainDoc *doc = terEdGetDoc();
	if(!doc)
		return;

	value = ui_SliderTextEntryGetValue(slider_entry);

	if(token_data->brush_name)
		EditorPrefStoreFloat(TER_ED_NAME, token_data->brush_name, token_data->value_name, value);
	TokenStoreSetF32(token_data->tpi, token_data->column, token_data->structptr, 0, value, NULL, NULL);
	terEdCompileBrush(doc);
}

static void terEdTemplateDropSliderTextEntryCB(UIDropSliderTextEntry *drop_slider_entry, TerrainTokenStorageData *token_data)
{
	float value;
	TerrainDoc *doc = terEdGetDoc();
	if(!doc)
		return;

	value = ui_DropSliderTextEntryGetValue(drop_slider_entry);

	if(token_data->brush_name)
		EditorPrefStoreFloat(TER_ED_NAME, token_data->brush_name, token_data->value_name, value);
	TokenStoreSetF32(token_data->tpi, token_data->column, token_data->structptr, 0, value, NULL, NULL);
	terEdCompileBrush(doc);
}

static void terEdTemplateDropSliderRefreshCB(UIDropSliderTextEntry *drop_slider_entry, TerrainTokenStorageData *token_data)
{
	float value;
	TerrainDoc *doc = terEdGetDoc();
	if(!doc)
		return;

	value = TokenStoreGetF32(token_data->tpi, token_data->column, token_data->structptr, 0, NULL);

	ui_DropSliderTextEntrySetValueAndCallback(drop_slider_entry, value);
}

static void terEdTemplateColorButtonRefreshCB(UIColorButton *color_button, TerrainTokenStorageData *token_data)
{
	Vec4 value;
	TerrainDoc *doc = terEdGetDoc();
	if(!doc)
		return;

	value[0] = TokenStoreGetU8(token_data->tpi, token_data->column, token_data->structptr, 0, NULL);
	value[1] = TokenStoreGetU8(token_data->tpi, token_data->column, token_data->structptr, 1, NULL);
	value[2] = TokenStoreGetU8(token_data->tpi, token_data->column, token_data->structptr, 2, NULL);
	value[3] = TokenStoreGetU8(token_data->tpi, token_data->column, token_data->structptr, 3, NULL);
	scaleVec4(value, 1.0f/255.0f, value);

	ui_ColorButtonSetColorAndCallback(color_button, value);
}

static void terEdTemplateColorButtonCB(UIColorButton *color_button, bool finished, TerrainTokenStorageData *token_data)
{
	Vec4 value;
	TerrainDoc *doc = terEdGetDoc();
	if(!doc)
		return;

	ui_ColorButtonGetColor(color_button, value);
	if(token_data->brush_name)
		EditorPrefStorePosition(TER_ED_NAME, token_data->brush_name, token_data->value_name, value[0], value[1], value[2], value[3]);
	scaleVec4(value, 255, value);

	TokenStoreSetU8(token_data->tpi, token_data->column, token_data->structptr, 0, value[0], NULL, NULL);
	TokenStoreSetU8(token_data->tpi, token_data->column, token_data->structptr, 1, value[1], NULL, NULL);
	TokenStoreSetU8(token_data->tpi, token_data->column, token_data->structptr, 2, value[2], NULL, NULL);
	TokenStoreSetU8(token_data->tpi, token_data->column, token_data->structptr, 3, value[3], NULL, NULL);
	terEdCompileBrush(doc);
}

static void terEdTemplateCheckButtonCB(UICheckButton *check_button, TerrainTokenStorageData *token_data)
{
	bool value;
	TerrainDoc *doc = terEdGetDoc();
	if(!doc)
		return;

	value = ui_CheckButtonGetState(check_button);

	if(token_data->brush_name)
		EditorPrefStoreInt(TER_ED_NAME, token_data->brush_name, token_data->value_name, value);
	TokenStoreSetU8(token_data->tpi, token_data->column, token_data->structptr, 0, value, NULL, NULL);
	terEdCompileBrush(doc);
}

static void terEdTemplateReseedButtonCB(UIButton *button, TerrainTokenStorageData *token_data)
{
	TerrainDoc *doc = terEdGetDoc();
	if(!doc)
		return;

	TokenStoreSetInt(token_data->tpi, token_data->column, token_data->structptr, 0, rand(), NULL, NULL);
	terEdCompileBrush(doc);
}

static void terEdTemplateTextEntryCB(UITextEntry *entry, TerrainTokenStorageData *token_data)
{
	const char *value;
	TerrainDoc *doc = terEdGetDoc();
	if(!doc)
		return;

	value = ui_TextEntryGetText(entry);

	TokenStoreSetString(token_data->tpi, token_data->column, token_data->structptr, 0, value, NULL, NULL, NULL, NULL);
	terEdCompileBrush(doc);
}

static void terEdTemplateMaterialComboCB(UIComboBox *entry, TerrainTokenStorageData *token_data)
{
	int i;
	int mat;
	const char *value;
	TerrainDoc *doc = terEdGetDoc();
	if(!doc)
		return;

	value = ui_ComboBoxGetSelectedObject(entry);

	TokenStoreSetString(token_data->tpi, token_data->column, token_data->structptr, 0, value, NULL, NULL, NULL, NULL);
	terEdCompileBrush(doc);

	//Update Visualization
	mat = terrainSourceGetMaterialIndex(doc->state.source, value, false);
	if(mat != UNDEFINED_MAT)
	{
		if (terrain_state.view_params->material_type != mat)
		{
			terrain_state.view_params->material_type = mat;
			for(i=0; i < eaSize(&doc->state.source->material_table); i++)
			{
				if(stricmp(doc->state.source->material_table[i], value) == 0)
				{
					ui_ComboBoxSetSelected(doc->terrain_ui->material_vis_combo, i);
					break;
				}
			}
			if(terrain_state.view_params->view_mode == TE_Material_Weight)
				terrainSourceRefreshHeightmaps(doc->state.source, true);
		}
	}
}

static void terEdTemplateMaterialComboRefreshCB(UIComboBox *combo, TerrainTokenStorageData *token_data)
{
	int i;
	const char *value;
	TerrainDoc *doc = terEdGetDoc();
	if(!doc)
		return;

	value = TokenStoreGetString(token_data->tpi, token_data->column, token_data->structptr, 0, NULL);
	
	if(value)
	{
		for(i=0; i < eaSize(&doc->state.persistent->materialsList); i++)
		{
			if(stricmp(doc->state.persistent->materialsList[i], value) == 0)
			{
				ui_ComboBoxSetSelectedAndCallback(combo, i);
				return;
			}
		}
	}
}

static bool terEdTemplateMaterialPickerCB(EMPicker *picker, EMPickerSelection **selections, TerrainTokenStorageData *token_data)
{
	char material_name[ MAX_PATH ];
	int idx, i;
	TerrainDoc *doc = terEdGetDoc();
	if(!doc)
		return false;
	if(eaSize(&selections) != 1)
		return false;

	getFileNameNoExt( material_name, selections[0]->doc_name );
	idx = terrainSourceGetMaterialIndex(doc->state.source, material_name, true);

	// Update UI & brush
	if(idx != UNDEFINED_MAT)
	{
		// Update materials list
		eaDestroyEx(&doc->state.persistent->materialsList, NULL);
		for (i = 0; i < eaSize(&doc->state.source->material_table); ++i)
		{
			eaPush(&doc->state.persistent->materialsList, strdup(doc->state.source->material_table[i]));
		}

		ui_ComboBoxSetSelectedAndCallback(token_data->linked_widget, idx);
	}

	terEdCompileBrush(doc);
	return true;
}

static void terEdTemplateMaterialButtonCB(UIButton *button, TerrainTokenStorageData *token_data)
{
	EMPicker *picker;
	TerrainDoc *doc = terEdGetDoc();
	if(!doc || !doc->terrain_ui || !doc->terrain_ui->material_picker)
		return;
	picker = doc->terrain_ui->material_picker;

	emPickerShow(picker, "Select", false, terEdTemplateMaterialPickerCB, token_data);
}

static void terEdTemplateImageFileRefreshCB(UIFileNameEntry *file_entry, TerrainTokenStorageData *token_data)
{
	const char *value;
	TerrainDoc *doc = terEdGetDoc();
	if(!doc)
		return;

	value = TokenStoreGetString(token_data->tpi, token_data->column, token_data->structptr, 0, NULL);

	if(value)
	{
		ui_FileNameEntrySetFileName(file_entry, value);
		terrainLoadBrushImage(doc->state.source, token_data->structptr);
	}
}

static void terEdTemplateImageFileCB(UIFileNameEntry *file_entry, TerrainTokenStorageData *token_data)
{
	const char *value_string;
	TerrainDoc *doc = terEdGetDoc();
	if(!doc)
		return;

	value_string = ui_FileNameEntryGetFileName(file_entry);

	//If string in the really old format, convert to the new format
	if(fileIsAbsolutePath(value_string))
	{
		while(value_string[0] != '\0' && !strStartsWith(value_string, "src"))
			value_string++;
		if(value_string[0] == '\0')
			return;
		value_string += 3;
	}
	//Strip the old or new format directory
	if(strStartsWith(value_string, "/texture_library/editor/Terrain_Editor/"))
		value_string += strlen("/texture_library/editor/Terrain_Editor/");
	if(strStartsWith(value_string, "editor/terrain/brush_images/"))
		value_string += strlen("editor/terrain/brush_images/");
	if(strStartsWith(value_string, "editors/terrain/brush_images/"))
		value_string += strlen("editors/terrain/brush_images/");

	TokenStoreSetString(token_data->tpi, token_data->column, token_data->structptr, 0, value_string, NULL, NULL, NULL, NULL);

	terrainLoadBrushImage(doc->state.source, token_data->structptr);
	terEdCompileBrush(doc);
}

static bool terEdTemplateObjectPickerCB(EMPicker *picker, EMPickerSelection **selections, TerrainTokenStorageData *token_data)
{
	int idx;
	ResourceInfo *in_data;
	TerrainObjectRef *object_pointer;
	EMPickerSelection *selection;
	GroupDef *child_def;
	TerrainDoc *doc = terEdGetDoc();
	if(!doc)
		return false;

	if(eaSize(&selections) != 1)
		return false;

	selection = selections[0];
	if(selection->table != parse_ResourceInfo)
		return false;
	in_data = selection->data;

	child_def = objectLibraryGetGroupDefFromResource(in_data, false);

	if(!child_def || !child_def->name_str)
		return false;

	if(!child_def->property_structs.terrain_properties.bTerrainObject)
	{
		Alertf("This object has not been flagged so that it can be used with the Object Brush.");
		return false;
	}

	object_pointer = TokenStoreGetPointer(token_data->tpi, token_data->column, token_data->structptr, 0, NULL);
	if(!object_pointer)
		return false;

	if(object_pointer->name_str)
		StructFreeString(object_pointer->name_str);
	object_pointer->name_uid = in_data->resourceID;

	idx = terrainSourceGetObjectIndex(doc->state.source, object_pointer->name_uid, -1, true);
	if(idx != UNDEFINED_OBJ)
		ui_ComboBoxSetSelected(token_data->linked_widget, idx);

	terEdCompileBrush(doc);
	return true;
}

static void terEdTemplateObjectButtonCB(UIButton *button, TerrainTokenStorageData *token_data)
{
	EMPicker *picker;
	picker = emPickerGetByName("Object Picker");
	if(!picker)
		return;

	emPickerShow(picker, "Select", false, terEdTemplateObjectPickerCB, token_data);
}

static void terEdTemplateObjectComboCB(UIComboBox *combo, TerrainTokenStorageData *token_data)
{
	int idx;
	TerrainObjectEntry *object = ui_ComboBoxGetSelectedObject(combo);
	TerrainObjectRef *object_pointer;
	TerrainDoc *doc = terEdGetDoc();
	if(!doc)
		return;
	object_pointer = TokenStoreGetPointer(token_data->tpi, token_data->column, token_data->structptr, 0, NULL);
	if(!object_pointer)
		return;

	if(object_pointer->name_str)
		StructFreeString(object_pointer->name_str);

	if(object)
	{
		object_pointer->name_uid = object->objInfo.name_uid;
	}
	else
	{
		object_pointer->name_uid = 0;
	}

	//Update Visualization
	idx = terrainSourceGetObjectIndex(doc->state.source, object_pointer->name_uid, -1, false);
	if(idx != UNDEFINED_OBJ)
	{
		if(terrain_state.view_params->object_type != idx)
		{
			terrain_state.view_params->object_type = idx;
			ui_ComboBoxSetSelected(doc->terrain_ui->object_vis_combo, idx);
			if(terrain_state.view_params->view_mode == TE_Object_Density)
				terrainSourceRefreshHeightmaps(doc->state.source, true);
		}
	}
	terEdCompileBrush(doc);
}

static void terEdTemplateObjectComboRefreshCB(UIComboBox *combo, TerrainTokenStorageData *token_data)
{
	int i;
	TerrainObjectEntry *last_object = NULL;
	TerrainObjectRef *object_pointer;
	TerrainDoc *doc = terEdGetDoc();
	if(!doc)
		return;
	object_pointer = TokenStoreGetPointer(token_data->tpi, token_data->column, token_data->structptr, 0, NULL);
	if(!object_pointer)
		return;

	for( i=0; i < eaSize(&doc->state.source->object_table); i++ )
	{
		TerrainObjectEntry *object = doc->state.source->object_table[i];
		if( object->objInfo.name_uid == object_pointer->name_uid &&
			stricmp(object->objInfo.name_str, object_pointer->name_str) == 0)
		{
			last_object = object;
			break;
		}
	}

	ui_ComboBoxSetModel(combo, NULL, &doc->state.source->object_table);
	if(last_object)
		ui_ComboBoxSetSelectedObjectAndCallback(combo, last_object);
}

static void terEdCopyJustTranslation(TerrainBrushValues *src, TerrainBrushValues *dest)
{
	dest->float_1 = src->float_1;
	dest->float_2 = src->float_2;
	dest->float_3 = src->float_3;
	dest->float_4 = src->float_4;
	dest->float_5 = src->float_5;
	dest->float_6 = src->float_6;
	dest->float_7 = src->float_7;
}

static void terEdTemplateImageTransCB(UIButton *button, TerrainTokenStorageData *token_data)
{
	char buf[256];
	TerrainDoc *doc = terEdGetDoc();
	if(!doc)
		return;
	
	if(doc->state.seleted_image == token_data->structptr)
	{
		ui_ButtonClick(doc->terrain_ui->image_done_button);
		return;
	}

	doc->state.seleted_image = token_data->structptr;
	doc->base_doc.edit_undo_stack = doc->state.image_undo_stack;
	terEdCopyJustTranslation(doc->state.seleted_image, doc->state.image_orig_vals);

	sprintf(buf, "%g", doc->state.seleted_image->float_1);
	ui_TextEntrySetText(doc->terrain_ui->image_pos_x_text, buf);
	sprintf(buf, "%g", doc->state.seleted_image->float_2);
	ui_TextEntrySetText(doc->terrain_ui->image_pos_y_text, buf);
	sprintf(buf, "%g", doc->state.seleted_image->float_3);
	ui_TextEntrySetText(doc->terrain_ui->image_pos_z_text, buf);
	sprintf(buf, "%g", doc->state.seleted_image->float_4*180.0f/PI);
	ui_TextEntrySetText(doc->terrain_ui->image_rot_p_text, buf);
	sprintf(buf, "%g", doc->state.seleted_image->float_5*180.0f/PI);
	ui_TextEntrySetText(doc->terrain_ui->image_rot_y_text, buf);
	sprintf(buf, "%g", doc->state.seleted_image->float_6*180.0f/PI);
	ui_TextEntrySetText(doc->terrain_ui->image_rot_r_text, buf);
	sprintf(buf, "%g", doc->state.seleted_image->float_7);
	ui_TextEntrySetText(doc->terrain_ui->image_scale_text, buf);

	terEdImageGizmoChanged(doc);
	terEdRefreshUI(doc);
}

void terrainObjectsComboMakeText(UIComboBox *combo, S32 row, bool inBox, void *unused, char **output)
{
	if (row >= 0 && row < eaSize(combo->model))
	{
		TerrainObjectEntry *entry = ((TerrainObjectEntry**) *combo->model)[row];
		if (entry->objInfo.name_uid != 0)
		{
			GroupDef *def = objectLibraryGetGroupDef(entry->objInfo.name_uid, false);
			if (def)
				estrPrintf(output, "%s",  def->name_str);
			else
				estrPrintf(output, "<INVALID GROUP>");
		}
		else
			estrPrintf(output, "");
	}
}

#define TER_UI_TB_WIDTH 75
#define TER_UI_SP_WIDTH 150
static int terEdMakeUIFromTemplate(TerrainDoc *doc, TerrainTokenStorageData ***token_data, char *brush_name, UIAnyWidget ***widgets, 
									TerrainBrushTemplateParam *param, TerrainBrushValues *values, F32 x, F32 y, bool toolbar, bool is_default)
{
	char buf[MAX_PATH];
	int column;
	TerrainTokenStorageData *token_datum;
	UITextEntry *text_entry;
	UISliderTextEntry *slider_entry;
	UIDropSliderTextEntry *drop_slider_entry;
	UIButton *button;
	UIFileNameEntry *file_entry;
	UICheckButton *check_button;
	UIColorButton *color_button;
	UILabel *label;
	UIComboBox *combo;
	Color color_data;
	Vec4 vec_data;
	const char *string_data;
	F32 float_data;
	int int_data;
	const TerrainObjectRef *object_pointer;

	int width = (toolbar ? TER_UI_TB_WIDTH : TER_UI_SP_WIDTH);

	if (!ParserFindColumn(parse_TerrainBrushValues, param->value_name, &column)) 
	{
		Alertf("Terrain UI: Cannot find \"%s\" in brush values.", param->value_name);
		return 0;
	}

	token_datum = calloc(1, sizeof(TerrainTokenStorageData));
	token_datum->tpi = parse_TerrainBrushValues;
	token_datum->column = column;
	token_datum->structptr = values;
	token_datum->linked_widget = NULL;
	token_datum->brush_name = brush_name;
	token_datum->value_name = param->value_name;
	eaPush(token_data, token_datum);

	if(param->display_name && param->display_name[0] != '\0')
	{
		sprintf(buf, "%s: ", param->display_name);
		label = ui_LabelCreate(buf, x, y);
		ui_WidgetSetHeight(UI_WIDGET(label), 20);
		eaPush(widgets, label);
		if(toolbar)
			x = widgetNextX(label)+2;
		else
			x+=90;
	}
	else if(!toolbar)
	{
		x+=90;	
	}

	switch(param->ui_type)
	{
	case TUI_SliderTextEntry:
		if(toolbar)
		{
			drop_slider_entry = ui_DropSliderTextEntryCreate("", param->min_val, param->max_val, param->step, x, y, width, TOOL_BAR_HEIGHT, 100, TOOL_BAR_HEIGHT);
			ui_SliderSetBias(drop_slider_entry->pSlider, param->bias, param->bias_offset);
			ui_DropSliderTextEntrySetChangedCallback(drop_slider_entry, terEdTemplateDropSliderTextEntryCB, token_datum);
			float_data = TokenStoreGetF32(token_datum->tpi, token_datum->column, token_datum->structptr, 0, NULL);
			if(token_datum->brush_name)
				float_data = EditorPrefGetFloat(TER_ED_NAME, token_datum->brush_name, token_datum->value_name, float_data);
			ui_DropSliderTextEntrySetValueAndCallback(drop_slider_entry, float_data);
			ui_WidgetSetTooltipString(UI_WIDGET(drop_slider_entry), param->tool_tip);
			eaPush(widgets, drop_slider_entry);
			x = widgetNextX(drop_slider_entry);
			//Refresh callback
			param->widget_ptr = drop_slider_entry;
			param->refresh_func = terEdTemplateDropSliderRefreshCB;
			param->data = token_datum;
		}
		else
		{
			slider_entry = ui_SliderTextEntryCreate("", param->min_val, param->max_val, x, y, width);
			ui_WidgetSetPaddingEx(UI_WIDGET(slider_entry), 0, 10, 0, 0);
			ui_WidgetSetPosition(UI_WIDGET(slider_entry), x, y);
			ui_WidgetSetHeight(UI_WIDGET(slider_entry), 20);
			ui_WidgetSetWidthEx(UI_WIDGET(slider_entry), 1.0f, UIUnitPercentage);
			ui_SliderTextEntrySetRange(slider_entry, param->min_val, param->max_val, param->step);
			ui_SliderSetBias(slider_entry->pSlider, param->bias, param->bias_offset);
			ui_SliderTextEntrySetPolicy(slider_entry, UISliderContinuous);
			ui_SliderTextEntrySetChangedCallback(slider_entry, terEdTemplateSliderTextEntryCB, token_datum);
			float_data = TokenStoreGetF32(token_datum->tpi, token_datum->column, token_datum->structptr, 0, NULL);
			if(token_datum->brush_name)
				float_data = EditorPrefGetFloat(TER_ED_NAME, token_datum->brush_name, token_datum->value_name, float_data);
			sprintf(buf, "%g", float_data);
			ui_SliderTextEntrySetTextAndCallback(slider_entry, buf);
			ui_WidgetSetTooltipString(UI_WIDGET(slider_entry), param->tool_tip);
			eaPush(widgets, slider_entry);
		}
		break;
	case TUI_ReseedButton:
		button = ui_ButtonCreate("Reseed", x, y, terEdTemplateReseedButtonCB, token_datum);
		if(!toolbar)
		{
			ui_WidgetSetPaddingEx(UI_WIDGET(button), 0, 10, 0, 0);
			ui_WidgetSetWidthEx(UI_WIDGET(button), 1.0f, UIUnitPercentage);
		}
		ui_WidgetSetHeight(UI_WIDGET(button), 20);
		ui_WidgetSetTooltipString(UI_WIDGET(button), param->tool_tip);
		eaPush(widgets, button);
		x = widgetNextX(button);
		break;
	case TUI_TextEntry:
		text_entry = ui_TextEntryCreate("", x, y);
		ui_TextEntrySetFinishedCallback(text_entry, terEdTemplateTextEntryCB, token_datum);
		ui_TextEntrySetText(text_entry, TokenStoreGetString(token_datum->tpi, token_datum->column, token_datum->structptr, 0, NULL));
		ui_WidgetSetTooltipString(UI_WIDGET(text_entry), param->tool_tip);
		eaPush(widgets, text_entry);
		x = widgetNextX(text_entry);
		break;
	case TUI_CheckButton:
		check_button = ui_CheckButtonCreate(x, y, "", false);
		ui_CheckButtonSetToggledCallback(check_button, terEdTemplateCheckButtonCB, token_datum);
		int_data = TokenStoreGetU8(token_datum->tpi, token_datum->column, token_datum->structptr, 0, NULL);
		if(token_datum->brush_name)
			int_data = EditorPrefGetInt(TER_ED_NAME, token_datum->brush_name, token_datum->value_name, int_data);
		ui_CheckButtonSetStateAndCallback(check_button, int_data);
		ui_WidgetSetTooltipString(UI_WIDGET(check_button), param->tool_tip);
		eaPush(widgets, check_button);
		x = widgetNextX(check_button);
		break;
	case TUI_ColorButton:
		color_data.r = TokenStoreGetU8(token_datum->tpi, token_datum->column, token_datum->structptr, 0, NULL);
		color_data.g = TokenStoreGetU8(token_datum->tpi, token_datum->column, token_datum->structptr, 1, NULL);
		color_data.b = TokenStoreGetU8(token_datum->tpi, token_datum->column, token_datum->structptr, 2, NULL);
		color_data.a = TokenStoreGetU8(token_datum->tpi, token_datum->column, token_datum->structptr, 3, NULL);
		colorToVec4(vec_data, color_data);
		if(token_datum->brush_name)
			EditorPrefGetPosition(TER_ED_NAME, token_datum->brush_name, token_datum->value_name, vec_data, vec_data+1, vec_data+2, vec_data+3);
		color_button = ui_ColorButtonCreate(x, y, vec_data);
		color_button->noAlpha = 1;
		ui_ColorButtonSetChangedCallback(color_button, terEdTemplateColorButtonCB, token_datum);
		ui_ColorButtonSetColorAndCallback(color_button, vec_data);
		color_button->widget.width = 25;
		ui_WidgetSetTooltipString(UI_WIDGET(color_button), param->tool_tip);
		eaPush(widgets, color_button);
		x = widgetNextX(color_button);
		if(toolbar)
		{
			//Refresh callback
			param->widget_ptr = color_button;
			param->refresh_func = terEdTemplateColorButtonRefreshCB;
			param->data = token_datum;
		}
		break;
	case TUI_MaterialPicker:
		string_data = TokenStoreGetString(token_datum->tpi, token_datum->column, token_datum->structptr, 0, NULL);
		//List of materials on layer
		combo = ui_ComboBoxCreate(x, y, TER_UI_SP_WIDTH-20, NULL, &doc->state.persistent->materialsList, NULL);
		combo->allowOutOfBoundsSelected = 1;
		ui_WidgetSetHeight(UI_WIDGET(combo), 20);
		if(!toolbar)
		{
			ui_WidgetSetPaddingEx(UI_WIDGET(combo), 0, 30, 0, 0);
			ui_WidgetSetWidthEx(UI_WIDGET(combo), 1.0f, UIUnitPercentage);
		}
		ui_ComboBoxSetSelectedCallback(combo, terEdTemplateMaterialComboCB, token_datum);
		int_data = terrainSourceGetMaterialIndex(doc->state.source, string_data, true);
		if(int_data != UNDEFINED_OBJ)
			ui_ComboBoxSetSelected(combo, int_data);
		ui_WidgetSetTooltipString(UI_WIDGET(combo), param->tool_tip);
		eaPush(widgets, combo);
		//Add material to layer button
		token_datum->linked_widget = combo;
		button = ui_ButtonCreateImageOnly("eui_button_plus", x+TER_UI_SP_WIDTH-20, y, terEdTemplateMaterialButtonCB, token_datum);
		if(!toolbar)
			ui_WidgetSetPositionEx(UI_WIDGET(button), 10, y, 0, 0, UITopRight);
		ui_WidgetSetDimensions(UI_WIDGET(button), 20, 20);
		ui_ButtonSetImageStretch(button, true);
		ui_WidgetSetTooltipString(UI_WIDGET(button), param->tool_tip);
		eaPush(widgets, button);
		x = widgetNextX(button);
		if(toolbar)
		{
			//Refresh callback
			param->widget_ptr = combo;
			param->refresh_func = terEdTemplateMaterialComboRefreshCB;
			param->data = token_datum;
		}
		break;
	case TUI_ObjectPicker:
		object_pointer = TokenStoreGetPointer(token_datum->tpi, token_datum->column, token_datum->structptr, 0, NULL);
		//List of objects on layer
		combo = ui_ComboBoxCreate(x, y, TER_UI_SP_WIDTH-20, NULL, &doc->state.persistent->objectList, NULL);
		combo->allowOutOfBoundsSelected = 1;
		ui_WidgetSetHeight(UI_WIDGET(combo), 20);
		if(!toolbar)
		{
			ui_WidgetSetPaddingEx(UI_WIDGET(combo), 0, 30, 0, 0);
			ui_WidgetSetWidthEx(UI_WIDGET(combo), 1.0f, UIUnitPercentage);
		}
		ui_ComboBoxSetSelectedCallback(combo, terEdTemplateObjectComboCB, token_datum);
		ui_ComboBoxSetTextCallback(combo, terrainObjectsComboMakeText, NULL);
		int_data = terrainSourceGetObjectIndex(doc->state.source, object_pointer->name_uid, -1, true);
		if(int_data != UNDEFINED_OBJ)
			ui_ComboBoxSetSelected(combo, int_data);
		ui_WidgetSetTooltipString(UI_WIDGET(combo), param->tool_tip);
		eaPush(widgets, combo);
		//Add object to layer button
		token_datum->linked_widget = combo;
		button = ui_ButtonCreateImageOnly("eui_button_plus", x+TER_UI_SP_WIDTH-20, y, terEdTemplateObjectButtonCB, token_datum);
		if(!toolbar)
			ui_WidgetSetPositionEx(UI_WIDGET(button), 10, y, 0, 0, UITopRight);
		ui_WidgetSetDimensions(UI_WIDGET(button), 20, 20);
		ui_ButtonSetImageStretch(button, true);
		ui_WidgetSetTooltipString(UI_WIDGET(button), param->tool_tip);
		eaPush(widgets, button);
		x = widgetNextX(button);
		if(toolbar)
		{
			//Refresh callback
			param->widget_ptr = combo;
			param->refresh_func = terEdTemplateObjectComboRefreshCB;
			param->data = token_datum;
        }
		break;
	case TUI_ImageFilePicker:
		string_data = TokenStoreGetString(token_datum->tpi, token_datum->column, token_datum->structptr, 0, NULL);
		sprintf(buf,"editors/terrain/brush_images");
		file_entry = ui_FileNameEntryCreate("", "Select an Image", buf, buf, ".tga", UIBrowseExisting);
		ui_WidgetSetPosition(UI_WIDGET(file_entry), x, y);
		ui_WidgetSetDimensions(UI_WIDGET(file_entry), TER_UI_SP_WIDTH, 20);
		if(!toolbar)
		{
			ui_WidgetSetPaddingEx(UI_WIDGET(file_entry), 0, 10, 0, 0);
			ui_WidgetSetWidthEx(UI_WIDGET(file_entry), 1.0f, UIUnitPercentage);
		}
		ui_FileNameEntrySetChangedCallback(file_entry, terEdTemplateImageFileCB, token_datum);
		ui_FileNameEntrySetFileNameAndCallback(file_entry, string_data);
		ui_WidgetSetTooltipString(UI_WIDGET(file_entry), param->tool_tip);
		eaPush(widgets, file_entry);
		x = widgetNextX(file_entry);
		if(is_default)
		{
			//Refresh callback
			param->widget_ptr = file_entry;
			param->refresh_func = terEdTemplateImageFileRefreshCB;
			param->data = token_datum;
		}
		break;
	case TUI_ImageTransButton:
		button = ui_ButtonCreate("Edit Transformation", x, y, terEdTemplateImageTransCB, token_datum);
		if(!toolbar)
		{
			ui_WidgetSetPaddingEx(UI_WIDGET(button), 0, 10, 0, 0);
			ui_WidgetSetWidthEx(UI_WIDGET(button), 1.0f, UIUnitPercentage);
		}
		ui_WidgetSetHeight(UI_WIDGET(button), 20);
		ui_WidgetSetTooltipString(UI_WIDGET(button), param->tool_tip);
		eaPush(widgets, button);
		x = widgetNextX(button);
		break;
	}

	return (toolbar ? x : y+20);
}

char* terEdGetSelectedBrushName()
{
	TerrainDoc *doc = terEdGetDoc();
	if(!doc)
		return NULL;

	if(!doc->state.selected_brush)
		return NULL;
	return doc->state.selected_brush->name;
}

static void terEdBrushDiameterDropSliderCB(UIDropSliderTextEntry *drop_slider, void *unused)
{
	TerrainDefaultBrush *brush;
	TerrainCommonBrushParams* common;
	TerrainDoc *doc = terEdGetDoc();
	if(!doc)
		return;
	brush = doc->state.selected_brush;

	common = terEdGetCommonBrushParams(doc);
	common->brush_diameter = ui_DropSliderTextEntryGetValue(drop_slider);
	EditorPrefStoreFloat(TER_ED_NAME, (brush ? brush->name : MULTI_BRUSH_NAME), "Diameter", common->brush_diameter);
}

static void terEdBrushHardnessDropSliderCB(UIDropSliderTextEntry *drop_slider, void *unused)
{
	TerrainDefaultBrush *brush;
	TerrainCommonBrushParams* common;
	TerrainDoc *doc = terEdGetDoc();
	if(!doc)
		return;
	brush = doc->state.selected_brush;

	common = terEdGetCommonBrushParams(doc);
	common->brush_hardness = ui_DropSliderTextEntryGetValue(drop_slider);
	EditorPrefStoreFloat(TER_ED_NAME, (brush ? brush->name : MULTI_BRUSH_NAME), "Hardness", common->brush_hardness);
}

static void terEdBrushStrengthDropSliderCB(UIDropSliderTextEntry *drop_slider, void *unused)
{
	TerrainDefaultBrush *brush;
	TerrainCommonBrushParams* common;
	TerrainDoc *doc = terEdGetDoc();
	if(!doc)
		return;
	brush = doc->state.selected_brush;

	common = terEdGetCommonBrushParams(doc);
	common->brush_strength = ui_DropSliderTextEntryGetValue(drop_slider);
	EditorPrefStoreFloat(TER_ED_NAME, (brush ? brush->name : MULTI_BRUSH_NAME), "Strength", common->brush_strength);
}

static void terEdBrushShapeComboCB(UIComboBox *combo, int i, void *unused)
{
	TerrainDefaultBrush *brush;
	TerrainCommonBrushParams* common;
	TerrainDoc *doc = terEdGetDoc();
	if(!doc)
		return;
	brush = doc->state.selected_brush;

	common = terEdGetCommonBrushParams(doc);
	common->brush_shape = i;
	EditorPrefStoreInt(TER_ED_NAME, (brush ? brush->name : MULTI_BRUSH_NAME), "Shape", common->brush_shape);
}

static void terEdBrushBrushFalloffTypeCB(TerrainBrushFalloffTypes falloff_type)
{
	TerrainDefaultBrush *brush;
	TerrainCommonBrushParams* common;
	TerrainDoc *doc = terEdGetDoc();
	if(!doc)
		return;
	brush = doc->state.selected_brush;

	common = terEdGetCommonBrushParams(doc);
	common->falloff_type = falloff_type;
	EditorPrefStoreInt(TER_ED_NAME, (brush ? brush->name : MULTI_BRUSH_NAME), "FalloffType", common->falloff_type);
}

static void terEdBrushBrushFalloffSCurveCB(UIDropSliderTextEntry *drop_slider, void *unused)
{
	terEdBrushBrushFalloffTypeCB(TE_FALLOFF_SCURVE);
}
static void terEdBrushBrushFalloffLinearCB(UIDropSliderTextEntry *drop_slider, void *unused)
{
	terEdBrushBrushFalloffTypeCB(TE_FALLOFF_LINEAR);
}
static void terEdBrushBrushFalloffConvexCB(UIDropSliderTextEntry *drop_slider, void *unused)
{
	terEdBrushBrushFalloffTypeCB(TE_FALLOFF_CONVEX);
}
static void terEdBrushBrushFalloffConcaveCB(UIDropSliderTextEntry *drop_slider, void *unused)
{
	terEdBrushBrushFalloffTypeCB(TE_FALLOFF_CONCAVE);
}

static void terEdBrushEyeDropperCB(UIButton *button, void *unused)
{
	TerrainDoc *doc = terEdGetDoc();
	if(!doc)
		return;

	doc->state.using_eye_dropper = !doc->state.using_eye_dropper;
}

static void terEdStopImageTranslation(TerrainDoc *doc)
{
	doc->state.seleted_image = NULL;
	EditUndoStackClear(doc->state.image_undo_stack);
	doc->base_doc.edit_undo_stack = doc->state.undo_stack;
    terEdCompileBrush(doc);
}

static void terEdMultiBrushButtonCB(UIButton *button, TerrainDefaultBrush *selected_brush)
{
	TerrainDoc *doc = terEdGetDoc();
	if(!doc)
		return;
	terEdStopImageTranslation(doc);
	doc->state.selected_brush = NULL;
	terEdCompileBrush(doc);
	terEdRefreshUI(doc);
}

void terEdDefaultBrushButtonCB(UIButton *button, TerrainDefaultBrush *selected_brush)
{
	TerrainDoc *doc = terEdGetDoc();
	if(!doc)
		return;
	terEdStopImageTranslation(doc);
	doc->state.selected_brush = selected_brush;
	doc->state.persistent->last_selected_brush[selected_brush->brush_template.channel] = selected_brush;
	terEdCompileBrush(doc);
	terEdRefreshUI(doc);
}

EMToolbar *terEdCreateGlobalBrushToolBar(TerrainDoc *doc)
{
	char buf[256];
	EMToolbar *brush_tool_bar = emToolbarCreate(10);
	UILabel *label;
	UIComboBox *combo;
	UIDropSliderTextEntry *drop_slider;
	UIButton *button;
	UIButtonCombo *button_combo;

	label = ui_LabelCreate("Size:", 0, TOOL_BAR_TOP);
	ui_WidgetSetTooltipString(UI_WIDGET(label), buf);
	emToolbarAddChild(brush_tool_bar, label, true);
	drop_slider = ui_DropSliderTextEntryCreate("0", 1, 2500, 1, widgetNextX(label)+1, TOOL_BAR_TOP, 75, TOOL_BAR_HEIGHT, 125, TOOL_BAR_HEIGHT);
	ui_DropSliderTextEntrySetChangedCallback(drop_slider, terEdBrushDiameterDropSliderCB, NULL);
	ui_SliderSetBias(drop_slider->pSlider, 2.0f, 0.0f);
	sprintf(buf, "Diameter of the brush in feet.");
	ui_WidgetSetTooltipString(UI_WIDGET(drop_slider), buf);
	doc->terrain_ui->brush_size_slider = drop_slider;
	emToolbarAddChild(brush_tool_bar, drop_slider, true);

	label = ui_LabelCreate("Hardness:", widgetNextX(drop_slider)+10, TOOL_BAR_TOP);
	emToolbarAddChild(brush_tool_bar, label, true);
	drop_slider = ui_DropSliderTextEntryCreate("0%", 0.0f, 1.0f, 0.01f, widgetNextX(label)+1, TOOL_BAR_TOP, 65, TOOL_BAR_HEIGHT, 100, TOOL_BAR_HEIGHT);
	ui_DropSliderTextEntrySetAsPercentage(drop_slider, true);
	ui_DropSliderTextEntrySetChangedCallback(drop_slider, terEdBrushHardnessDropSliderCB, NULL);
	sprintf(buf, "Percentage of the radius before the paint starts to fall off.");
	ui_WidgetSetTooltipString(UI_WIDGET(drop_slider), buf);
	doc->terrain_ui->brush_hardness_slider = drop_slider;
	emToolbarAddChild(brush_tool_bar, drop_slider, true);

	label = ui_LabelCreate("Strength:", widgetNextX(drop_slider)+10, TOOL_BAR_TOP);
	emToolbarAddChild(brush_tool_bar, label, true);
	drop_slider = ui_DropSliderTextEntryCreate("0%", 0.01f, 1.0f, 0.01f, widgetNextX(label)+1, TOOL_BAR_TOP, 65, TOOL_BAR_HEIGHT, 100, TOOL_BAR_HEIGHT);
	ui_DropSliderTextEntrySetAsPercentage(drop_slider, true);
	ui_DropSliderTextEntrySetChangedCallback(drop_slider, terEdBrushStrengthDropSliderCB, NULL);
	sprintf(buf, "How fast paint is applied.");
	ui_WidgetSetTooltipString(UI_WIDGET(drop_slider), buf);
	doc->terrain_ui->brush_strength_slider = drop_slider;
	emToolbarAddChild(brush_tool_bar, drop_slider, true);
	
	combo = ui_ComboBoxCreate(widgetNextX(drop_slider)+10, TOOL_BAR_TOP, 1, NULL, NULL, NULL);
	ui_WidgetSetDimensions(UI_WIDGET(combo), 75, TOOL_BAR_HEIGHT);
	ui_ComboBoxSetEnum(combo, TerrainBrushShapeEnum, terEdBrushShapeComboCB, NULL);
	sprintf(buf, "Shape of the brush.");
	ui_WidgetSetTooltipString(UI_WIDGET(combo), buf);
	doc->terrain_ui->brush_shape_combo = combo;
	emToolbarAddChild(brush_tool_bar, combo, true);

	button_combo = ui_ButtonComboCreate(widgetNextX(combo)+10, TOOL_BAR_TOP, TOOL_BAR_HEIGHT, TOOL_BAR_HEIGHT, POP_DOWN, false);
	ui_ButtonComboSetChangedCallback(button_combo, NULL, NULL);
	button = ui_ButtonComboAddItem(button_combo, "eui_button_scurve.tga", NULL, NULL, terEdBrushBrushFalloffSCurveCB, NULL);
	sprintf(buf, "S-Curve");
	ui_WidgetSetTooltipString(UI_WIDGET(button), buf);
	button = ui_ButtonComboAddItem(button_combo, "eui_button_linear.tga", NULL, NULL, terEdBrushBrushFalloffLinearCB, NULL);
	sprintf(buf, "Linear");
	ui_WidgetSetTooltipString(UI_WIDGET(button), buf);
	button = ui_ButtonComboAddItem(button_combo, "eui_button_convex.tga", NULL, NULL, terEdBrushBrushFalloffConvexCB, NULL);
	sprintf(buf, "Convex");
	ui_WidgetSetTooltipString(UI_WIDGET(button), buf);
	button = ui_ButtonComboAddItem(button_combo, "eui_button_concave.tga", NULL, NULL, terEdBrushBrushFalloffConcaveCB, NULL);
	sprintf(buf, "Concave");
	ui_WidgetSetTooltipString(UI_WIDGET(button), buf);
	ui_ButtonComboSetActive(button_combo, true);
	doc->terrain_ui->brush_falloff_combo = button_combo;
	emToolbarAddChild(brush_tool_bar, button_combo, true);

	button = ui_ButtonCreateImageOnly("eui_button_eyedropper", widgetNextX(button_combo)+10, TOOL_BAR_TOP, terEdBrushEyeDropperCB, NULL);
	ui_WidgetSetDimensions(UI_WIDGET(button), TOOL_BAR_HEIGHT, TOOL_BAR_HEIGHT);
	ui_ButtonSetImageStretch(button, true);
	sprintf(buf, "Get values from the terrain and <br>put them into the parameters of a brush.  <br>Only works with some brushes.");
	ui_WidgetSetTooltipString(UI_WIDGET(button), buf);
	doc->terrain_ui->brush_eye_dropper = button;
	emToolbarAddChild(brush_tool_bar, button, true);

	doc->terrain_ui->tool_bar_brush = brush_tool_bar;
	return brush_tool_bar;
}

static void terEdChannelButtonClickCB(UIButtonCombo *pButtonCombo, void *channel)
{
	U16 i;
	TerrainDoc *doc = terEdGetDoc();
	if(!doc)
		return;

	for(i=0; i < TBC_NUM_CHANNELS; i++)
	{
		if(((U16)channel) != i)
			ui_ButtonComboSetActive(doc->terrain_ui->channel_button_list[i], false);
	}
}

static void terEdBrushPaneMoved(UIPane *pane, UIButtonComboDirection direction)
{
	int i;
	bool vertical = (direction == POP_RIGHT || direction == POP_LEFT) ? true : false;
	TerrainDoc *doc = terEdGetDoc();
	if(!doc)
		return;

	EditorPrefStoreInt(TER_ED_NAME, "Brush Pane", "PopDir", direction);

	for(i=0; i < TBC_NUM_CHANNELS; i++)
	{
		UIButtonCombo *button_combo = doc->terrain_ui->channel_button_list[i];
		ui_ButtonComboSetDirection(button_combo, direction);
		if(	(UI_WIDGET(button_combo)->x != 0 && vertical) ||
			(UI_WIDGET(button_combo)->y != 0 && !vertical)	)
		{
			ui_WidgetSetPosition(UI_WIDGET(button_combo), UI_WIDGET(button_combo)->y, UI_WIDGET(button_combo)->x);
		}
	}
}

static void terEdBrushPaneDrag(UIPane *pane, bool *dragging)
{
	(*dragging) = true;
	ui_PaneSetInvisible(pane, false);
}

static void terEdBrushPaneTick(UIPane *pane, UI_PARENT_ARGS)
{
	if(pane->widget.dragData && *((bool*)pane->widget.dragData))
	{
		if (mouseIsDown(MS_LEFT))
		{
			if(g_ui_State.mouseX < (g_ui_State.viewportMax[1]/2.0f))
			{
				//Left
				pane->widget.offsetFrom = UITopLeft;
				terEdBrushPaneMoved(pane, POP_RIGHT);
			}
			else
			{
				//Right
				pane->widget.offsetFrom = UITopRight;
				terEdBrushPaneMoved(pane, POP_LEFT);
			}
		}
		else
		{
			*((bool*)pane->widget.dragData) = false;
			ui_PaneSetInvisible(pane, true);
		}
	}

	ui_PaneTick(pane, UI_PARENT_VALUES);
}

UIPane *terEdCreateBrushPane(TerrainDoc *doc)
{
	char buf[256];
	RefDictIterator it;
	TerrainDefaultBrush *brush;
	UIButtonCombo *button_combo;
	UIButton *button;
	UIPane *pane;
	int y=BRUSH_PANE_OFFSET;
	U16 i;
	UIButtonCombo *highest_button_combo = NULL;
	U16 highest_priority = 0xFF;
	int pane_pop_dir = EditorPrefGetInt(TER_ED_NAME, "Brush Pane", "PopDir", POP_RIGHT);

	pane = ui_PaneCreate(0, BRUSH_PANE_OFFSET+3, BRUSH_PANE_DEPTH, BRUSH_PANE_OFFSET+BRUSH_PANE_DEPTH*TBC_NUM_CHANNELS, UIUnitFixed, UIUnitFixed, 0);
	pane->widget.offsetFrom = UILeft;
	ui_WidgetSetDragCallback(UI_WIDGET(pane), terEdBrushPaneDrag, &doc->terrain_ui->dragging_brush_pane);
	pane->widget.tickF = terEdBrushPaneTick;
	ui_PaneSetInvisible(pane, true);

	if(pane_pop_dir == POP_RIGHT)
		pane->widget.offsetFrom = UITopLeft;
	else
		pane->widget.offsetFrom = UITopRight;

	for(i=0; i < TBC_NUM_CHANNELS; i++)
	{
		doc->terrain_ui->channel_button_list[i] = ui_ButtonComboCreate(0, y, BRUSH_PANE_DEPTH-8, BRUSH_PANE_DEPTH-8, POP_RIGHT, false);
		ui_WidgetAddChild(UI_WIDGET(pane), UI_WIDGET(doc->terrain_ui->channel_button_list[i]));
		ui_ButtonComboSetChangedCallback(doc->terrain_ui->channel_button_list[i], terEdChannelButtonClickCB, ((void*)(intptr_t)i));
		ui_ButtonComboSetActive(doc->terrain_ui->channel_button_list[i], false);
		y += (BRUSH_PANE_DEPTH - 5);
	}

	RefSystem_InitRefDictIterator(DEFAULT_BRUSH_DICTIONARY, &it);
	while(brush = (TerrainDefaultBrush*)RefSystem_GetNextReferentFromIterator(&it)) 
	{
		if(	brush->brush_template.bucket == TBK_OptimizedBrush ||
			brush->brush_template.bucket == TBK_RegularBrush )
		{
			sprintf(buf, "%s_active", brush->brush_template.image);
			
			if(doc->state.persistent->last_selected_brush[brush->brush_template.channel] == NULL)
				doc->state.persistent->last_selected_brush[brush->brush_template.channel] = brush;

			button_combo = doc->terrain_ui->channel_button_list[brush->brush_template.channel];
			button = ui_ButtonComboAddOrderedItem(button_combo, brush->brush_template.image, buf, NULL, brush->order, terEdDefaultBrushButtonCB, brush);

			if(brush->display_name)
				sprintf(buf, "%s Brush:<br>%s", brush->display_name, brush->tool_tip);
			else
				sprintf(buf, "%s Brush:<br>%s", brush->name, brush->tool_tip);
			ui_WidgetSetTooltipString(UI_WIDGET(button), buf);

			brush->button = button;

			if(brush->order < highest_priority)
			{
				highest_priority = brush->order;
				highest_button_combo = button_combo;
			}
		}
		
	}

	button_combo = doc->terrain_ui->channel_button_list[TBC_Custom];
	button = ui_ButtonComboAddItem(button_combo, "eui_button_custom_brush", "eui_button_custom_brush_active", NULL, terEdMultiBrushButtonCB, NULL);
	sprintf(buf, "Multi Brush:<br>Custom brushes that can be applied at the same time.");
	ui_WidgetSetTooltipString(UI_WIDGET(button), buf);

	if(highest_button_combo)
		ui_ButtonClick(highest_button_combo->pButton);

	doc->terrain_ui->brush_pane = pane;
	terEdBrushPaneMoved(pane, pane_pop_dir);
	return pane;
}


static void terEdExpanderInactiveCB(UIExpander *pExpander, void *unused)
{
	if(!UI_WIDGET(pExpander)->childrenInactive)
	{
		pExpander->expandF = NULL;
		ui_ExpanderToggle(pExpander);
		pExpander->expandF = terEdExpanderInactiveCB;
	}
}

static void terEdExpanderCheckButtonCB(UICheckButton *pCheck, UIExpander *pExpander)
{
	TerrainDoc *doc = terEdGetDoc();
	if(pExpander->expandData && doc)
	{
		bool state = ui_CheckButtonGetState(pCheck);
		*((bool*)pExpander->expandData) = state;
		if(state == true)
		{
			ui_WidgetSkin(UI_WIDGET(pExpander), NULL);
			UI_WIDGET(pExpander)->pOverrideSkin = s_skinActiveExpander;
			pExpander->expandF = NULL;
			if(UI_WIDGET(pExpander)->childrenInactive)
				ui_ExpanderToggle(pExpander);
		}
		else
		{
			ui_WidgetSkin(UI_WIDGET(pExpander), s_skinInactiveExpander);
			pExpander->expandF = terEdExpanderInactiveCB;
			if(!UI_WIDGET(pExpander)->childrenInactive)
				ui_ExpanderToggle(pExpander);
		}
		terEdCompileBrush(doc);
	}
}

void terEdExpanderDraw(UIExpander *expand, UI_PARENT_ARGS)
{
	UI_GET_COORDINATES(expand);
	UIStyleFont *font;
	F32 headerHeight;

	if (!UI_GET_SKIN(expand))
		font = GET_REF(g_ui_State.font);
	else if (!ui_IsActive(UI_WIDGET(expand)))
		font = GET_REF(UI_GET_SKIN(expand)->hNormal);
	else
		font = GET_REF(UI_GET_SKIN(expand)->hNormal);

	headerHeight = (ui_StyleFontLineHeight(font, scale) + UI_STEP_SC);

	if(!expand->widget.childrenInactive)
	{
		display_sprite(white_tex_atlas, x, y+headerHeight, z, w / white_tex_atlas->width, scale / white_tex_atlas->height, 0x000000FF);
		display_sprite(white_tex_atlas, x, y+headerHeight, z, w / white_tex_atlas->width, expand->openedHeight*scale / white_tex_atlas->height, 0x00000022);
	}

	ui_ExpanderDraw(expand, UI_PARENT_VALUES);
}

void terEdClearUndo(UIButton *button, TerrainDoc *doc)
{
	EditUndoStackClear(doc->state.undo_stack);
}

EMPanel *terEdCreateActionsPanel(TerrainDoc *doc)
{
	EMPanel *panel;
    UIButton *button;
    UISliderTextEntry *slider_entry;
	int y = 0;
    
	panel = emPanelCreate("Brush", "Actions", 0);

    button = ui_ButtonCreate("Apply Brush to Terrain", 10, y, terEdQueueFillWithBrush, NULL);
	doc->terrain_ui->apply_brush_button = button;
    emPanelAddChild(panel, button, true);

    y += 25;
    emPanelAddChild(panel, ui_LabelCreate("Iterations:", 10, y), true);

	slider_entry = ui_SliderTextEntryCreate("1", 0.0, 1.0, 110, y, 140);
	ui_WidgetSetPosition(UI_WIDGET(slider_entry), 110, y);
	ui_WidgetSetHeight(UI_WIDGET(slider_entry), 20);
	ui_SliderTextEntrySetRange(slider_entry, 1.0, 150.0, 1.0);
	ui_SliderTextEntrySetAsPercentage(slider_entry, false);
	ui_WidgetSetTooltipString(UI_WIDGET(slider_entry), "Number of times to apply brush to terrain.");
    
    doc->terrain_ui->apply_iterations_slider = slider_entry;
    emPanelAddChild(panel, slider_entry, true);

	y += 25;
    button = ui_ButtonCreate("Clear Undo Buffer", 10, y, terEdClearUndo, doc);
    emPanelAddChild(panel, button, true);

    y += 25;

    button = ui_ButtonCreate("Stitch Neighbors", 10, y, terEdQueueStitchNeighbors, NULL);
    emPanelAddChild(panel, button, true);

	doc->terrain_ui->panel_actions = panel;
    return panel;
}

EMPanel *terEdCreateGlobalFiltersPanel(TerrainDoc *doc)
{
	RefDictIterator it;
    TerrainDefaultBrush *brush;
	EMPanel *panel;
	UIExpanderGroup *expander_group;

	panel = emPanelCreate("Brush", "Filters", 0);
	expander_group = ui_ExpanderGroupCreate();
	ui_WidgetSetDimensionsEx(UI_WIDGET(expander_group), 1, 1, UIUnitPercentage, UIUnitPercentage);
	RefSystem_InitRefDictIterator(DEFAULT_BRUSH_DICTIONARY, &it);
	while(brush = (TerrainDefaultBrush*)RefSystem_GetNextReferentFromIterator(&it)) 
	{
		if(	brush->brush_template.bucket == TBK_OptimizedFilter ||
			brush->brush_template.bucket == TBK_RegularFilter )
		{
			UIExpander *expander;
			UICheckButton *check_button;
			UIAnyWidget **widgets=NULL;
			int i, y=0;

			if(brush->display_name)
				expander = ui_ExpanderCreate(brush->display_name, 0);
			else
				expander = ui_ExpanderCreate(brush->name, 0);
			ui_WidgetSetPosition(UI_WIDGET(expander), 10, 0);

			check_button = ui_CheckButtonCreate(200, 2, "", false);
			ui_ExpanderAddLabel(expander, (UIWidget*)check_button);
			ui_CheckButtonSetToggledCallback(check_button, terEdExpanderCheckButtonCB, expander);
			ui_ExpanderSetExpandCallback(expander, terEdExpanderInactiveCB, &brush->default_values.active);
			ui_WidgetSkin(UI_WIDGET(expander), s_skinInactiveExpander);
			UI_WIDGET(expander)->drawF = terEdExpanderDraw;

			for(i=0; i < eaSize(&brush->brush_template.params); i++)
			{
				y = terEdMakeUIFromTemplate(doc, &doc->terrain_ui->persistant_token_data, brush->name, &widgets, brush->brush_template.params[i], &brush->default_values, 10, y, false, true);
				y += 5;
			}

			ui_ExpanderSetHeight(expander, y+5);
			for(i=0; i < eaSize(&widgets); i++)
			{
				ui_ExpanderAddChild(expander, widgets[i]);
			}
			eaDestroy(&widgets);
			ui_ExpanderGroupAddExpander(expander_group, expander);
		}
	}
	ui_ExpanderGroupSetReflowCallback(expander_group, terEdPanelExpanderReflowCB, panel);
	ui_ExpanderGroupSetGrow(expander_group, true);
	emPanelAddChild(panel, expander_group, true);
	doc->terrain_ui->panel_global_filters = panel;
	return panel;
}

TerrainBrushOp *terEdExpandBrushOp(TerrainBrush *unexpanded, TerrainBrush *expanded, TerrainDefaultBrush *base_brush, bool channel_ops)
{
	int i;
	bool found_match = false;
	TerrainBrushOp *new_brush_op;

	new_brush_op = StructAlloc(parse_TerrainBrushOp);
	new_brush_op->parent_brush = expanded;

	if(unexpanded != NULL)
	{
		for(i=0; i < eaSize(&unexpanded->ops); i++)
		{
			TerrainDefaultBrush *unexpanded_brush_base = GET_REF(unexpanded->ops[i]->brush_base);

			if(!unexpanded_brush_base)
			{
				Alertf("Corrupted Brush.  Brush Type Missing.");
				continue;
			}

			if(	channel_ops && 
				unexpanded_brush_base->brush_template.bucket != TBK_OptimizedBrush &&
				unexpanded_brush_base->brush_template.bucket != TBK_RegularBrush	)
				continue;

			if(	channel_ops ? 
				(base_brush->brush_template.channel == unexpanded_brush_base->brush_template.channel) :
				(base_brush == unexpanded_brush_base)	)
			{
				SET_HANDLE_FROM_REFERENT(DEFAULT_BRUSH_DICTIONARY, unexpanded_brush_base, new_brush_op->brush_base);
				StructCopyAll(parse_TerrainBrushValues, &unexpanded->ops[i]->values, &new_brush_op->values);
				new_brush_op->values.image_ref = NULL;
				new_brush_op->values.active = true;
				found_match = true;
				break;
			}
		}
	}

	if(!found_match)
	{
		SET_HANDLE_FROM_REFERENT(DEFAULT_BRUSH_DICTIONARY, base_brush, new_brush_op->brush_base);
		StructCopyAll(parse_TerrainBrushValues, &base_brush->default_values, &new_brush_op->values);
		new_brush_op->values.image_ref = NULL;
		new_brush_op->values.active = false;
	}

	return new_brush_op;
}

TerrainBrush* terEdExpandBrush(TerrainDoc *doc, TerrainBrush *active_brush, TerrainMultiBrush *expanded)
{
	int i;
	TerrainDefaultBrush *brush;
	RefDictIterator it;
	TerrainBrush *new_brush = StructAlloc(parse_TerrainBrush);
	if(active_brush)
	{
		new_brush->name = StructAllocString(active_brush->name);
		new_brush->falloff_values.diameter_multi = active_brush->falloff_values.diameter_multi;
		new_brush->falloff_values.hardness_multi = active_brush->falloff_values.hardness_multi;
		new_brush->falloff_values.strength_multi = active_brush->falloff_values.strength_multi;
		new_brush->falloff_values.invert_filters = active_brush->falloff_values.invert_filters;
	}
	else
	{
		new_brush->name = StructAllocString("New Brush");
		new_brush->falloff_values.diameter_multi = 1.0f;
		new_brush->falloff_values.hardness_multi = 1.0f;
		new_brush->falloff_values.strength_multi = 1.0f;
		new_brush->falloff_values.invert_filters = false;
	}

	//Add Channel Ops
	for(i=0; i < (TBC_NUM_CHANNELS-1); i++)
	{
		TerrainBrushOp *new_brush_op;

		brush = doc->state.persistent->last_selected_brush[i];		
		if(!brush)
			continue;

		new_brush_op = terEdExpandBrushOp(active_brush, new_brush, brush, true);
		eaPush(&new_brush->ops, new_brush_op);
	}

	//Add Filter Ops
	RefSystem_InitRefDictIterator(DEFAULT_BRUSH_DICTIONARY, &it);
	while(brush = (TerrainDefaultBrush*)RefSystem_GetNextReferentFromIterator(&it)) 
	{
		TerrainBrushOp *new_brush_op;

		if(	brush->brush_template.bucket != TBK_OptimizedFilter &&
			brush->brush_template.bucket != TBK_RegularFilter )
			continue;

		new_brush_op = terEdExpandBrushOp(active_brush, new_brush, brush, false);
		eaPush(&new_brush->ops, new_brush_op);
	}

	eaPush(&expanded->brushes, new_brush);
	return new_brush;
}

void terEdExpandActiveMultiBrush(TerrainDoc *doc, TerrainMultiBrush *active)
{
	int i;
	TerrainMultiBrush *expanded = doc->state.persistent->expanded_multi_brush;

	eaDestroyEx(&expanded->brushes, terrainDestroyTerrainBrush);
	expanded->filename = NULL;

	if(!active)
		return;

	expanded->filename = allocAddString(active->filename);	
	for(i=0; i < eaSize(&active->brushes); i++)
	{
		terEdExpandBrush(doc, active->brushes[i], expanded);
	}
}

void terEdCreateBrushOpExpanderUI(TerrainDoc *doc, TerrainBrush *brush, TerrainBrushOp *brush_op, int y)
{
	int i;
	TerrainDefaultBrush *brush_op_base = GET_REF(brush_op->brush_base);

	//Delete old widgets
	for(i=0; i < eaSize(&brush_op->widgets); i++)
	{
		ui_WidgetForceQueueFree(brush_op->widgets[i]);
	}
	eaDestroy(&brush_op->widgets);
	//Delete old storage data
	for(i=0; i < eaSize(&brush_op->storage_data); i++)
	{
		eaFindAndRemove(&brush->storage_data, brush_op->storage_data[i]);
		eaFindAndRemove(&doc->terrain_ui->multi_brush_token_data, brush_op->storage_data[i]);
		free(brush_op->storage_data[i]);
	}
	eaDestroy(&brush_op->storage_data);

	//Make new widgets and storage data
	for(i=0; brush_op_base && i < eaSize(&brush_op_base->brush_template.params); i++)
	{
		y = terEdMakeUIFromTemplate(doc, &brush_op->storage_data, NULL, &brush_op->widgets, brush_op_base->brush_template.params[i], &brush_op->values, 5, y, false, false); 
		y += 5;
	}
	ui_ExpanderSetHeight(brush_op->expander, y+5);

	//Add widgets
	for(i=0; i < eaSize(&brush_op->widgets); i++)
	{
		ui_ExpanderAddChild(brush_op->expander, brush_op->widgets[i]);
	}
	//Store storage data
	for(i=0; i < eaSize(&brush_op->storage_data); i++)
	{
		eaPush(&brush->storage_data, brush_op->storage_data[i]);
		eaPush(&doc->terrain_ui->multi_brush_token_data, brush_op->storage_data[i]);
	}
}

void terEdBrushExpanderReflowCB(UIExpanderGroup *expander_group, UIExpander *expander)
{
	TerrainDoc *doc = terEdGetDoc();
	if(!doc)
		return;
	
	ui_ExpanderSetHeight(expander, expander_group->totalHeight + doc->terrain_ui->brush_header_height + 5);	
}

void terEdBrushOpExpanderReflowCB(UIExpanderGroup *expander_group, UIExpander *expander)
{
	TerrainDoc *doc = terEdGetDoc();
	if(!doc)
		return;
	
	ui_ExpanderSetHeight(expander, expander_group->totalHeight + 5);	
}

static void terEdChannelOpChangedCB(UIComboBox *combo, TerrainBrushOp *brush_op)
{
	bool found = false;
  	RefDictIterator it;
	TerrainDefaultBrush *brush;
	TerrainDoc *doc = terEdGetDoc();
	TerrainBrushStringRef *selected = ui_ComboBoxGetSelectedObject(combo);
	if(!doc || !selected)
		return;

	RefSystem_InitRefDictIterator(DEFAULT_BRUSH_DICTIONARY, &it);
	while(brush = (TerrainDefaultBrush*)RefSystem_GetNextReferentFromIterator(&it)) 
	{
		if(stricmp(selected->op_name, brush->name) == 0)
		{
			found = true;
			REMOVE_HANDLE(brush_op->brush_base);
			SET_HANDLE_FROM_REFERENT(DEFAULT_BRUSH_DICTIONARY, brush, brush_op->brush_base);
			StructCopyAll(parse_TerrainBrushValues, &brush->default_values, &brush_op->values);
			brush_op->values.image_ref = NULL;
			brush_op->values.active = true;
		}
	}

	if(found)
	{
		terEdCreateBrushOpExpanderUI(doc, brush_op->parent_brush, brush_op, widgetNextY(combo)+5);
	}
}

void terEdRemoveBrushCB(UIButton *button, TerrainBrush *brush)
{
	int i;
	TerrainDoc *doc = terEdGetDoc();
	if(!doc)
		return;
	
	//Close Rename window if open
	ui_WidgetRemoveFromGroup(UI_WIDGET(doc->terrain_ui->rename_brush_window));

	//Remove and free expander
	ui_ExpanderGroupRemoveExpander(doc->terrain_ui->multi_brush_expanders, brush->expander);
	ui_WidgetForceQueueFree(UI_WIDGET(brush->expander));	

	//Remove from expanded brush
	eaFindAndRemove(&doc->state.persistent->expanded_multi_brush->brushes, brush);

	//Free storage data
	for(i=0; i < eaSize(&brush->storage_data); i++)
	{
		eaFindAndRemove(&doc->terrain_ui->multi_brush_token_data, brush->storage_data[i]);
		free(brush->storage_data[i]);
	}

	//Free Brush
	terrainDestroyTerrainBrush(brush);

	//Recompile
	terEdCompileBrush(doc);
}

static void terEdMoveBrushUpCB(UIButton *button, TerrainBrush *brush)
{
	int idx;
	TerrainDoc *doc = terEdGetDoc();
	if(!doc)
		return;

	//Close Rename window if open
	ui_WidgetRemoveFromGroup(UI_WIDGET(doc->terrain_ui->rename_brush_window));

	//Find Location
	idx = eaFind(&doc->state.persistent->expanded_multi_brush->brushes, brush);
	if(idx < 1)
		return;

	//Move Up
	ui_ExpanderGroupRemoveExpander(doc->terrain_ui->multi_brush_expanders, brush->expander);
	ui_ExpanderGroupInsertExpander(doc->terrain_ui->multi_brush_expanders, brush->expander, idx-1);
	eaSwap(&doc->state.persistent->expanded_multi_brush->brushes, idx, idx-1);

	//Recompile
	terEdCompileBrush(doc);
}

static void terEdMoveBrushDownCB(UIButton *button, TerrainBrush *brush)
{
	int idx;
	TerrainDoc *doc = terEdGetDoc();
	if(!doc)
		return;

	//Close Rename window if open
	ui_WidgetRemoveFromGroup(UI_WIDGET(doc->terrain_ui->rename_brush_window));

	//Find Location
	idx = eaFind(&doc->state.persistent->expanded_multi_brush->brushes, brush);
	if(idx >= eaSize(&doc->state.persistent->expanded_multi_brush->brushes)-1)
		return;

	//Move Up
	ui_ExpanderGroupRemoveExpander(doc->terrain_ui->multi_brush_expanders, brush->expander);
	ui_ExpanderGroupInsertExpander(doc->terrain_ui->multi_brush_expanders, brush->expander, idx+1);
	eaSwap(&doc->state.persistent->expanded_multi_brush->brushes, idx, idx+1);

	//Recompile
	terEdCompileBrush(doc);
}

static void terEdCopyBrushCB(UIButton *button, TerrainBrush *brush)
{
	int i;
	TerrainDoc *doc = terEdGetDoc();
	if(!doc)
		return;

	if(doc->state.persistent->copied_brush)
		terrainDestroyTerrainBrush(doc->state.persistent->copied_brush);
	doc->state.persistent->copied_brush = StructCreate(parse_TerrainBrush);
	StructCopyFields(parse_TerrainBrush, brush, doc->state.persistent->copied_brush, 0, 0);
	assert(eaSize(&brush->ops) == eaSize(&doc->state.persistent->copied_brush->ops));
	for( i=0; i < eaSize(&brush->ops); i++ )
	{
		doc->state.persistent->copied_brush->ops[i]->values.active = brush->ops[i]->values.active;
	}
}

void terEdRenameBrushCB(UIButton *button, TerrainBrush *brush)
{
	TerrainDoc *doc = terEdGetDoc();
	if(!doc)
		return;

	doc->terrain_ui->rename_brush = brush;
	ui_TextEntrySetText(doc->terrain_ui->rename_brush_text, brush->name);
	ui_WindowAddToDevice(doc->terrain_ui->rename_brush_window, NULL);
}

static void terEdSliderTextEntryCB(UISliderTextEntry *slider_entry, bool bFinished, F32 *val)
{
	TerrainDoc *doc = terEdGetDoc();
	if(!doc)
		return;

	(*val) = ui_SliderTextEntryGetValue(slider_entry);
	terEdCompileBrush(doc);	
}

void terEdToggleBrushStateCB(UICheckButton *check, bool *val)
{
	TerrainDoc *doc = terEdGetDoc();
	if(!doc)
		return;

	(*val) = ui_CheckButtonGetState(check);
	terEdCompileBrush(doc);	
}

void terEdDestroyTokenStorageData(TerrainTokenStorageData *data)
{
	free(data);
}

void terEdCreateBrushExpanderUI(TerrainDoc *doc, TerrainBrush *expanded_brush)
{
	int j, k;
	char buf[256];
	UIExpanderGroup *multi_brush_expanders = doc->terrain_ui->multi_brush_expanders;
	UIExpanderGroup *brush_expander_group;
	UIExpanderGroup *channel_ops_expanders;
	UIExpanderGroup *filter_ops_expanders;
	UIExpander *brush_expander;
	UIExpander *ops_expander;
	UILabel *label;
	UIButton *button;
	UICheckButton *check;
	UISliderTextEntry *slider_entry;

	//Create Expander
	brush_expander = ui_ExpanderCreate(expanded_brush->name, 0);
	ui_WidgetSetPosition(UI_WIDGET(brush_expander), 5, 0);
	UI_WIDGET(brush_expander)->pOverrideSkin = s_skinActiveExpander;
	brush_expander->widget.drawF = terEdExpanderDraw;
	ui_ExpanderGroupAddExpander(multi_brush_expanders, brush_expander);
	expanded_brush->expander = brush_expander; 

	//Rename Button
	button = ui_ButtonCreate("Copy", 5, 5, terEdCopyBrushCB, expanded_brush);
	ui_WidgetSetDimensions(UI_WIDGET(button), 75, 20);
	ui_ExpanderAddChild(brush_expander, button);

	//Rename Button
	button = ui_ButtonCreate("Rename", widgetNextX(button) + 5, 5, terEdRenameBrushCB, expanded_brush);
	ui_WidgetSetDimensions(UI_WIDGET(button), 75, 20);
	ui_ExpanderAddChild(brush_expander, button);

	//Remove Button
	button = ui_ButtonCreate("Remove", widgetNextX(button) + 5, 5, terEdRemoveBrushCB, expanded_brush);
	ui_WidgetSetDimensions(UI_WIDGET(button), 75, 20);
	ui_ExpanderAddChild(brush_expander, button);

	//Move Up Button
	button = ui_ButtonCreate("Up", 85, widgetNextY(button)+5, terEdMoveBrushUpCB, expanded_brush);
	ui_WidgetSetDimensions(UI_WIDGET(button), 75, 20);
	ui_ExpanderAddChild(brush_expander, button);

	//Move Down Button
	button = ui_ButtonCreate("Down", widgetNextX(button) + 5, UI_WIDGET(button)->y, terEdMoveBrushDownCB, expanded_brush);
	ui_WidgetSetDimensions(UI_WIDGET(button), 75, 20);
	ui_ExpanderAddChild(brush_expander, button);

	//Size Multiplier
	label = ui_LabelCreate("Size: ", 5, widgetNextY(button)+5);
	ui_ExpanderAddChild(brush_expander, label);
	slider_entry = ui_SliderTextEntryCreate("", 0.0, 1.0, 110, widgetNextY(button)+5, 150);
	ui_WidgetSetPosition(UI_WIDGET(slider_entry), 90, widgetNextY(button)+5);
	ui_WidgetSetHeight(UI_WIDGET(slider_entry), 20);
	ui_SliderTextEntrySetRange(slider_entry, 0.0, 1.0, 0.01);
	ui_SliderTextEntrySetPolicy(slider_entry, UISliderContinuous);
	ui_SliderTextEntrySetAsPercentage(slider_entry, true);
	ui_SliderTextEntrySetChangedCallback(slider_entry, terEdSliderTextEntryCB, &expanded_brush->falloff_values.diameter_multi);
	sprintf(buf, "%g", expanded_brush->falloff_values.diameter_multi);
	ui_SliderTextEntrySetTextAndCallback(slider_entry, buf);
	ui_ExpanderAddChild(brush_expander, slider_entry);

	//Hardness Multiplier
	label = ui_LabelCreate("Hardness: ", 5, widgetNextY(label)+2);
	ui_ExpanderAddChild(brush_expander, label);
	slider_entry = ui_SliderTextEntryCreate("", 0.0, 1.0, 110, label->widget.y, 150);
	ui_WidgetSetPosition(UI_WIDGET(slider_entry), 90, label->widget.y);
	ui_WidgetSetHeight(UI_WIDGET(slider_entry), 20);
	ui_SliderTextEntrySetRange(slider_entry, 0.0, 1.0, 0.01);
	ui_SliderTextEntrySetPolicy(slider_entry, UISliderContinuous);
	ui_SliderTextEntrySetAsPercentage(slider_entry, true);
	ui_SliderTextEntrySetChangedCallback(slider_entry, terEdSliderTextEntryCB, &expanded_brush->falloff_values.hardness_multi);
	sprintf(buf, "%g", expanded_brush->falloff_values.hardness_multi);
	ui_SliderTextEntrySetTextAndCallback(slider_entry, buf);
	ui_ExpanderAddChild(brush_expander, slider_entry);

	//Strength Multiplier
	label = ui_LabelCreate("Strength: ", 5, widgetNextY(label)+2);
	ui_ExpanderAddChild(brush_expander, label);
	slider_entry = ui_SliderTextEntryCreate("", 0.0, 1.0, 110, label->widget.y, 150);
	ui_WidgetSetPosition(UI_WIDGET(slider_entry), 90, label->widget.y);
	ui_WidgetSetHeight(UI_WIDGET(slider_entry), 20);
	ui_SliderTextEntrySetRange(slider_entry, 0.0, 1.0, 0.01);
	ui_SliderTextEntrySetPolicy(slider_entry, UISliderContinuous);
	ui_SliderTextEntrySetAsPercentage(slider_entry, true);
	ui_SliderTextEntrySetChangedCallback(slider_entry, terEdSliderTextEntryCB, &expanded_brush->falloff_values.strength_multi);
	sprintf(buf, "%g", expanded_brush->falloff_values.strength_multi);
	ui_SliderTextEntrySetTextAndCallback(slider_entry, buf);
	ui_ExpanderAddChild(brush_expander, slider_entry);

	//Invert Filters Flag
	check = ui_CheckButtonCreate(10, widgetNextY(label)+5, "Invert Filters", expanded_brush->falloff_values.invert_filters);
	ui_CheckButtonSetToggledCallback(check, terEdToggleBrushStateCB, &expanded_brush->falloff_values.invert_filters);
	ui_ExpanderAddChild(brush_expander, check);

	//Disable Brush Flag
	check = ui_CheckButtonCreate(10, widgetNextY(check)+5, "Disable Brush", expanded_brush->disabled);
	ui_CheckButtonSetToggledCallback(check, terEdToggleBrushStateCB, &expanded_brush->disabled);
	ui_ExpanderAddChild(brush_expander, check);

	//Create Ops Expander Group
	doc->terrain_ui->brush_header_height = widgetNextY(check)+5;
	brush_expander_group = ui_ExpanderGroupCreate();
	ui_WidgetSetPosition(UI_WIDGET(brush_expander_group), 0, widgetNextY(check)+5);
	ui_WidgetSetDimensionsEx(UI_WIDGET(brush_expander_group), 1.f, 1.f, UIUnitPercentage, UIUnitPercentage);
	ui_ExpanderGroupSetReflowCallback(brush_expander_group, terEdBrushExpanderReflowCB, brush_expander);
	ui_ExpanderGroupSetGrow(brush_expander_group, true);
	ui_ExpanderAddChild(brush_expander, brush_expander_group);

	//Create Channel Ops Expander
	ops_expander = ui_ExpanderCreate("Channels", 0);
	ui_WidgetSetPosition(UI_WIDGET(ops_expander), 5, 0);
	UI_WIDGET(ops_expander)->pOverrideSkin = s_skinActiveExpander;
	UI_WIDGET(ops_expander)->drawF = terEdExpanderDraw;
	ui_ExpanderGroupAddExpander(brush_expander_group, ops_expander);
	//Create Channel Ops Expander Group
	channel_ops_expanders = ui_ExpanderGroupCreate();
	ui_WidgetSetPosition(UI_WIDGET(channel_ops_expanders), 0, 0);
	ui_WidgetSetDimensionsEx(UI_WIDGET(channel_ops_expanders), 1.f, 1.f, UIUnitPercentage, UIUnitPercentage);
	ui_ExpanderGroupSetReflowCallback(channel_ops_expanders, terEdBrushOpExpanderReflowCB, ops_expander);
	ui_ExpanderGroupSetGrow(channel_ops_expanders, true);
	ui_ExpanderAddChild(ops_expander, channel_ops_expanders);

	//Create Filter Ops Expander
	ops_expander = ui_ExpanderCreate("Filters", 0);
	ui_WidgetSetPosition(UI_WIDGET(ops_expander), 5, 0);
	UI_WIDGET(ops_expander)->pOverrideSkin = s_skinActiveExpander;
	UI_WIDGET(ops_expander)->drawF = terEdExpanderDraw;
	ui_ExpanderGroupAddExpander(brush_expander_group, ops_expander);
	//Create Filter Ops Expander Group
	filter_ops_expanders = ui_ExpanderGroupCreate();
	ui_WidgetSetPosition(UI_WIDGET(filter_ops_expanders), 0, 0);
	ui_WidgetSetDimensionsEx(UI_WIDGET(filter_ops_expanders), 1.f, 1.f, UIUnitPercentage, UIUnitPercentage);
	ui_ExpanderGroupSetReflowCallback(filter_ops_expanders, terEdBrushOpExpanderReflowCB, ops_expander);
	ui_ExpanderGroupSetGrow(filter_ops_expanders, true);
	ui_ExpanderAddChild(ops_expander, filter_ops_expanders);

	//Fill Op Expander Groups
	for(j=0; j < eaSize(&expanded_brush->ops); j++)
	{
		TerrainBrushOp *brush_op = expanded_brush->ops[j];
		TerrainDefaultBrush *brush_op_base = GET_REF(brush_op->brush_base);
		UIExpander *expander;
		UICheckButton *check_button;
		UIComboBox *combo;
		int y=0;

		if(	brush_op_base->brush_template.bucket == TBK_OptimizedBrush ||
			brush_op_base->brush_template.bucket == TBK_RegularBrush	)
		{
			expander = ui_ExpanderCreate(StaticDefineIntRevLookup(TerrainBrushChannelEnum, brush_op_base->brush_template.channel), 0);
			ui_ExpanderGroupAddExpander(channel_ops_expanders, expander);

			//Create Operation Selector Combo
			if(eaSize(&doc->state.channel_ops[brush_op_base->brush_template.channel].op_refs) > 1)
			{
				TerrainEditorChannelOpsList *channel_ops = &doc->state.channel_ops[brush_op_base->brush_template.channel];
				label = ui_LabelCreate("Operation: ", 5, y);
				ui_ExpanderAddChild(expander, label);
	
				combo = ui_ComboBoxCreate(95, y, 150, parse_TerrainBrushStringRef, &channel_ops->op_refs, "DisplayName");
				ui_WidgetSetPaddingEx(UI_WIDGET(combo), 0, 10, 0, 0);
				ui_WidgetSetWidthEx(UI_WIDGET(combo), 1.0f, UIUnitPercentage);
				ui_WidgetSetHeight(UI_WIDGET(combo), 20);
				for(k=0; k < eaSize(&channel_ops->op_refs); k++)
				{
					TerrainBrushStringRef *str_ref = channel_ops->op_refs[k];
					if(stricmp(str_ref->op_name, brush_op_base->name) == 0)
					{
						ui_ComboBoxSetSelectedObject(combo, str_ref);
						break;
					}
				}
				ui_ComboBoxSetSelectedCallback(combo, terEdChannelOpChangedCB, brush_op);
				ui_ExpanderAddChild(expander, combo);

				y = widgetNextY(label)+5;
			}
		}
		else
		{
			if(brush_op_base->display_name)
				expander = ui_ExpanderCreate(brush_op_base->display_name, 0);
			else
				expander = ui_ExpanderCreate(brush_op_base->name, 0);
			ui_ExpanderGroupAddExpander(filter_ops_expanders, expander);
		}
		brush_op->expander = expander;
		ui_WidgetSetPosition(UI_WIDGET(expander), 5, 0);

		//Create check button for expander
		check_button = ui_CheckButtonCreate(200, 2, "", false);
		ui_ExpanderAddLabel(expander, (UIWidget*)check_button);
		ui_CheckButtonSetToggledCallback(check_button, terEdExpanderCheckButtonCB, expander);
		ui_ExpanderSetExpandCallback(expander, terEdExpanderInactiveCB, &brush_op->values.active);
		ui_WidgetSkin(UI_WIDGET(expander), s_skinInactiveExpander);
		UI_WIDGET(expander)->drawF = terEdExpanderDraw;

		//Make UI for expander
		terEdCreateBrushOpExpanderUI(doc, brush_op->parent_brush, brush_op, y);

		if(brush_op->values.active)
		{
			ui_CheckButtonToggle(check_button);
			ui_ExpanderToggle(expander);
		}
	}
}

void terEdRefreshActiveMultiBrushUI(TerrainDoc *doc, TerrainMultiBrush *expanded)
{
	int i;
	UIExpanderGroup *multi_brush_expanders = doc->terrain_ui->multi_brush_expanders;
	assert(multi_brush_expanders);

	eaDestroyEx(&doc->terrain_ui->multi_brush_token_data, terEdDestroyTokenStorageData);
	ui_WidgetGroupFreeInternal(&multi_brush_expanders->childrenInOrder);
	multi_brush_expanders->childrenInOrder = NULL;
	ui_ExpanderGroupReflow(doc->terrain_ui->multi_brush_expanders);

	if(!expanded)
		return;

	for(i=0; i < eaSize(&expanded->brushes); i++)
	{
		terEdCreateBrushExpanderUI(doc, expanded->brushes[i]);
	}
}

void terEdSelectMultiBrushByName(TerrainDoc *doc, const char *name)
{
	RefDictIterator it;
	TerrainMultiBrush *brush;

	RefSystem_InitRefDictIterator(MULTI_BRUSH_DICTIONARY, &it);
	while(brush = (TerrainMultiBrush*)RefSystem_GetNextReferentFromIterator(&it)) 
	{
		char buff[256];
		getFileNameNoExt(buff, brush->filename);
		if(buff && stricmp(name, buff)==0)
		{
			terEdExpandActiveMultiBrush(doc, brush);
			terEdRefreshActiveMultiBrushUI(doc, doc->state.persistent->expanded_multi_brush);
			terEdCompileBrush(doc);
			return;
		}
	}
}

void terEdSelectMultiBrushCB(UIComboBox *combo, void *unused)
{
	TerrainDoc *doc = terEdGetDoc();
	char *selected = ui_ComboBoxGetSelectedObject(combo);
	if(!doc || !selected)
		return;

	terEdStopImageTranslation(doc);

	//Close Rename window if open.
    if (doc->terrain_ui->rename_brush_window)
        ui_WidgetRemoveFromGroup(UI_WIDGET(doc->terrain_ui->rename_brush_window));

	terEdSelectMultiBrushByName(doc, selected);
}

void terEdMultiBrushExpanderReflowCB(UIExpanderGroup *expander_group, EMPanel *panel)
{
	TerrainDoc *doc = terEdGetDoc();
	if(!doc)
		return;
	
	emPanelSetHeight(panel, expander_group->totalHeight + doc->terrain_ui->multi_brush_header_height + 5);	
}

static int terEdFillFillteredListComparator(const char** left, const char** right)
{
	return stricmp(*left,*right);
}

void terEdFillFillteredList(TerrainDoc *doc)
{
	RefDictIterator it;
	TerrainMultiBrush *brush;

	//Destroy old list
	eaDestroyEx(&doc->state.persistent->multi_brush_filtered_list, StructFreeString);

	//Fill in new list
	RefSystem_InitRefDictIterator(MULTI_BRUSH_DICTIONARY, &it);
	while(brush = (TerrainMultiBrush*)RefSystem_GetNextReferentFromIterator(&it)) 
	{
		char buff[256];
		char *name;

		if(	doc->state.persistent->multi_brush_filter && 
			!strStartsWith(brush->filename, doc->state.persistent->multi_brush_filter))
			continue;

		getFileNameNoExt(buff, brush->filename);
		name = StructAllocString(buff);

		eaPush(&doc->state.persistent->multi_brush_filtered_list, name);
	}
	eaQSort(doc->state.persistent->multi_brush_filtered_list, terEdFillFillteredListComparator);
}

void terEdMultiBrushSave()
{
	int i, j;
	char fullfilename[MAX_PATH];
	TerrainMultiBrush *expanded;
	TerrainMultiBrush *compressed;
	TerrainDoc *doc = terEdGetDoc();
	if(!doc)
		return;

	expanded = doc->state.persistent->expanded_multi_brush;
	if(expanded == NULL)
		return;

	fileLocateWrite(expanded->filename, fullfilename);
	forwardSlashes(fullfilename);

	//Removed Unneeded Data
	compressed = StructAlloc(parse_TerrainMultiBrush);
	for(i=0; i < eaSize(&expanded->brushes); i++)
	{
		TerrainBrush *new_brush = StructAlloc(parse_TerrainBrush);
		
		StructCopyAll(parse_TerrainBrushFalloff, &expanded->brushes[i]->falloff_values, &new_brush->falloff_values);
		new_brush->name = StructAllocString(expanded->brushes[i]->name);

		for(j=0; j < eaSize(&expanded->brushes[i]->ops); j++)
		{
			TerrainBrushOp *brush_op = expanded->brushes[i]->ops[j];
			if(brush_op->values.active)
			{
				TerrainBrushOp *new_brush_op;
				new_brush_op = StructAlloc(parse_TerrainBrushOp);

				SET_HANDLE_FROM_REFERENT(DEFAULT_BRUSH_DICTIONARY, GET_REF(brush_op->brush_base), new_brush_op->brush_base);
				StructCopyAll(parse_TerrainBrushValues, &brush_op->values, &new_brush_op->values);
				new_brush_op->values.image_ref = NULL;

				eaPush(&new_brush->ops, new_brush_op);				
			}
		}
		eaPush(&compressed->brushes, new_brush);
	}

	//Write File
	ParserWriteTextFileFromSingleDictionaryStruct(fullfilename, RefSystem_GetDictionaryHandleFromNameOrHandle(MULTI_BRUSH_DICTIONARY), compressed, 0, 0);

	//Delete Copy
	eaDestroyEx(&compressed->brushes, terrainDestroyTerrainBrush);
	StructDestroy(parse_TerrainMultiBrush, compressed);
}

bool terEdUnlockMessageCB(UIDialog *pDialog, UIDialogButton eButton, void *unused)
{
	if(eButton == kUIDialogButton_Ok)
		terEdMultiBrushSave();
	return true;
}

void terEdMultiBrushUnlockSave(void *unused, void *unused2)
{
	char fullfilename[MAX_PATH];
	TerrainMultiBrush *expanded;
	TerrainDoc *doc = terEdGetDoc();
	if(!doc)
		return;

	expanded = doc->state.persistent->expanded_multi_brush;
	if(expanded == NULL)
		return;

	fileLocateWrite(expanded->filename, fullfilename);
	forwardSlashes(fullfilename);

	if (fileExists(fullfilename)) 
	{
		char buf[1024];
		const char* lastAuthor;
		GimmeErrorValue return_val;
		__time32_t last_changed = fileLastChanged(fullfilename);
		return_val = gimmeDLLDoOperation(fullfilename, GIMME_CHECKOUT, GIMME_QUIET);
		switch(return_val)
		{
		case GIMME_NO_ERROR:
		case GIMME_ERROR_NO_SC:
		case GIMME_ERROR_NO_DLL:
		case GIMME_ERROR_FILENOTFOUND:
		case GIMME_ERROR_NOT_IN_DB:
			break;
		case GIMME_ERROR_ALREADY_CHECKEDOUT:
			lastAuthor = gimmeDLLQueryIsFileLocked(fullfilename);
			sprintf(buf, "\"%s\" already checked out by %s.", fullfilename, lastAuthor ? lastAuthor : "UNKNOWN");
			ui_DialogPopup("Error", buf);
			return;
		default:
			sprintf(buf, "\"%s\" could not be checked out. Code: %d", fullfilename, return_val);
			ui_DialogPopup("Error", buf);
			return;
		}
		emStatusPrintf("File \"%s\" checked out.", fullfilename);

		if(last_changed != fileLastChanged(fullfilename))
		{
			sprintf(buf, "\"%s\" changed while getting latest.  If you save you will overwite someone else's changes.  Are you sure you want to save?", fullfilename);
			ui_WindowShow(UI_WINDOW(ui_DialogCreateEx("Warning", buf, terEdUnlockMessageCB, NULL, NULL,
				"Cancel", kUIDialogButton_Cancel, "Save", kUIDialogButton_Ok, NULL)));
			return;
		}
	}
	terEdMultiBrushSave();
}

void terEdBrushSaveAsCB(const char *path, const char *filename, void *unused)
{
	char fullfilename[MAX_PATH];
	TerrainMultiBrush *expanded;
	TerrainDoc *doc = terEdGetDoc();
	if(!doc || !path || !filename)
		return;

	expanded = doc->state.persistent->expanded_multi_brush;
	if(expanded == NULL)
		return;

	sprintf(fullfilename, "%s\\%s", path, filename);
	expanded->filename = allocAddFilename(fullfilename);

	terEdMultiBrushUnlockSave(NULL, NULL);
}

void terEdMultiBrushSaveAs(void *unused, void *unused2)
{
    UIWindow *window;
	window = ui_FileBrowserCreate(	"Save Preset..", "Save As", UIBrowseNew, UIBrowseFiles, false,
									"editors/terrain/brushes", "editors/terrain/brushes", NULL, ".brush", 
									NULL, NULL, terEdBrushSaveAsCB, NULL);
    ui_WindowShow(window);
}

static void terEdMultiBrushSetFilterCB(const char *path, const char *filename, void *unused)
{
	int i;
	char filename_no_ext[256];
	const char *rel_path;
	TerrainDoc *doc = terEdGetDoc();
	bool found;
	if(!doc || !path || !filename)
		return;

	getFileNameNoExt(filename_no_ext, filename);

	rel_path = path;
	while(!strStartsWith(rel_path, "editor"))
	{
		if(rel_path[0] == '\0')
			return;
		rel_path++;
	}

	if(doc->state.persistent->multi_brush_filter)
		StructFreeString(doc->state.persistent->multi_brush_filter);
	doc->state.persistent->multi_brush_filter = StructAllocString(rel_path);
	terEdFillFillteredList(doc);
	found=false;
	for(i=0; i < eaSize(&doc->state.persistent->multi_brush_filtered_list); i++)
	{
		if(strEndsWith(doc->state.persistent->multi_brush_filtered_list[i], filename_no_ext))
		{
			ui_ComboBoxSetSelectedObjectAndCallback(doc->terrain_ui->multi_brush_combo, doc->state.persistent->multi_brush_filtered_list[i]);
			found = true;
			break;
		}
	}
	if(!found)
		ui_ComboBoxSetSelectedAndCallback(doc->terrain_ui->multi_brush_combo, 0);
}

static void terEdMultiBrushSetFilter(void *unused, void *unused2)
{
	UIWindow *window;
	window = ui_FileBrowserCreate(	"Open Folder..", "Open", UIBrowseExisting, UIBrowseFiles, false,
									"editors/terrain/brushes", "editors/terrain/brushes", NULL, ".brush", 
									NULL, NULL, terEdMultiBrushSetFilterCB, NULL);
	ui_WindowShow(window);
}

void terEdMultiBrushAddBrushCB(UIButton *button, void *unused)
{
	TerrainBrush *new_brush;
	TerrainDoc *doc = terEdGetDoc();
	if(!doc)
		return;

	new_brush = terEdExpandBrush(doc, NULL, doc->state.persistent->expanded_multi_brush);
	terEdCreateBrushExpanderUI(doc, new_brush);
}

void terEdMultiBrushPasteBrushCB(UIButton *button, void *unused)
{
	int i;
	TerrainBrush *new_brush;
	TerrainDoc *doc = terEdGetDoc();
	if(!doc || !doc->state.persistent->copied_brush)
		return;

	new_brush = StructCreate(parse_TerrainBrush);
	StructCopyFields(parse_TerrainBrush, doc->state.persistent->copied_brush, new_brush, 0, 0);
	assert(eaSize(&doc->state.persistent->copied_brush->ops) == eaSize(&new_brush->ops));
	for( i=0; i < eaSize(&doc->state.persistent->copied_brush->ops); i++ )
	{
		new_brush->ops[i]->values.active = doc->state.persistent->copied_brush->ops[i]->values.active;
		new_brush->ops[i]->parent_brush = new_brush;
	}
	eaPush(&doc->state.persistent->expanded_multi_brush->brushes, new_brush);
	terEdCreateBrushExpanderUI(doc, new_brush);
}

EMPanel *terEdCreateMultiBrushPanel(TerrainDoc *doc)
{
	EMPanel *panel;
	UIComboBox *combo;
	UILabel *label;
	UIButton *button;
	UIExpanderGroup *expander_group;

	panel = emPanelCreate("Brush", "Multi Brush", 0);
	doc->state.persistent->expanded_multi_brush	= calloc(1, sizeof(TerrainMultiBrush));

	label = ui_LabelCreate("Presets: ", 10, 5);
	emPanelAddChild(panel, label, true);
	combo = ui_ComboBoxCreate(90, 5, 155, NULL, &doc->state.persistent->multi_brush_filtered_list, NULL);
	ui_ComboBoxSetSelectedCallback(combo, terEdSelectMultiBrushCB, NULL);
	doc->terrain_ui->multi_brush_combo = combo;
	emPanelAddChild(panel, combo, true);

	button = ui_ButtonCreate("Open", 10, widgetNextY(label)+5, terEdMultiBrushSetFilter, NULL);
	ui_WidgetSetDimensions(UI_WIDGET(button), 75, 20);
	emPanelAddChild(panel, button, true);

	button = ui_ButtonCreate("Save", widgetNextX(button)+5, widgetNextY(label)+5, terEdMultiBrushUnlockSave, NULL);
	ui_WidgetSetDimensions(UI_WIDGET(button), 75, 20);
	emPanelAddChild(panel, button, true);

	button = ui_ButtonCreate("Save As", widgetNextX(button)+5, widgetNextY(label)+5, terEdMultiBrushSaveAs, NULL);
	ui_WidgetSetDimensions(UI_WIDGET(button), 75, 20);
	emPanelAddChild(panel, button, true);

	expander_group = ui_ExpanderGroupCreate();
	ui_WidgetSetPosition(UI_WIDGET(expander_group), 0, widgetNextY(button)+5);
	ui_WidgetSetDimensionsEx(UI_WIDGET(expander_group), 1.f, 1.f, UIUnitPercentage, UIUnitPercentage);
	ui_ExpanderGroupSetReflowCallback(expander_group, terEdMultiBrushExpanderReflowCB, panel);
	ui_ExpanderGroupSetGrow(expander_group, true);
	doc->terrain_ui->multi_brush_expanders = expander_group;
	emPanelAddChild(panel, doc->terrain_ui->multi_brush_expanders, true);	

	button = ui_ButtonCreate("Add Brush", 0, widgetNextY(button)+5, terEdMultiBrushAddBrushCB, NULL);
	ui_WidgetSetDimensions(UI_WIDGET(button), 75, 20);
	doc->terrain_ui->multi_brush_header_height = widgetNextY(button)+5;
	ui_WidgetSetPositionEx(UI_WIDGET(button), 170, 0, 0, 0, UIBottomLeft);
	emPanelAddChild(panel, button, true);

	button = ui_ButtonCreate("Paste", 0, UI_WIDGET(button)->y, terEdMultiBrushPasteBrushCB, NULL);
	ui_WidgetSetDimensions(UI_WIDGET(button), 75, 20);
	ui_WidgetSetPositionEx(UI_WIDGET(button), 90, 0, 0, 0, UIBottomLeft);
	emPanelAddChild(panel, button, true);

	emPanelSetHeight(panel, expander_group->totalHeight + doc->terrain_ui->multi_brush_header_height + 5);	
	doc->terrain_ui->panel_multi_brush = panel;

	ui_ComboBoxSetSelectedAndCallback(doc->terrain_ui->multi_brush_combo, 0);
	return panel;
}

static void terEdGizmoModeCB(UIComboBox *combo, int i, void *unused)
{
	TerrainDoc *doc = terEdGetDoc();
	if(!doc)
		return;

	doc->state.gizmo_mode = ui_ComboBoxGetSelectedEnum(combo);
}

static void terEdGizmoHideCB(UIButton *button, void *unused)
{
	TerrainDoc *doc = terEdGetDoc();
	if(!doc)
		return;

	terEdStopImageTranslation(doc);
	terEdRefreshUI(doc);
}

static void terEdUndoImageTransFreeFN(void *context, TerrainBrushValues *undo_values)
{
	StructDestroy(parse_TerrainBrushValues, undo_values);
}

static void terEdUndoImageTransFN(void *context, TerrainBrushValues *undo_values)
{
	char buf[255];
	TerrainBrushValues *values;
	TerrainDoc *doc = terEdGetDoc();
	if(!doc)
		return;
	values = doc->state.seleted_image;
	if(!values)
		return;

	//Copy undo_values to image_orig_vals and seleted_image, Copy those values(should match) to undo_values
	terEdCopyJustTranslation(undo_values, doc->state.image_orig_vals);
	terEdCopyJustTranslation(values, undo_values);
	terEdCopyJustTranslation(doc->state.image_orig_vals, values);
	//Refresh UI Items
	sprintf(buf, "%g", doc->state.seleted_image->float_7);
	ui_TextEntrySetText(doc->terrain_ui->image_scale_text, buf);
	terEdImageGizmoChanged(doc);
	terEdRefreshUI(doc);
}

static void terEdAddToImageTransUndo(const Mat4 mat, void *unused)
{
	TerrainBrushValues *undo_values;
	TerrainBrushValues *values;
	TerrainDoc *doc = terEdGetDoc();
	if(!doc)
		return;
	values = doc->state.seleted_image;
	if(!values)
		return;

	undo_values = StructCreate(parse_TerrainBrushValues);
	terEdCopyJustTranslation(doc->state.image_orig_vals, undo_values);
	terEdCopyJustTranslation(values, doc->state.image_orig_vals);
	EditCreateUndoCustom(doc->state.image_undo_stack, terEdUndoImageTransFN, terEdUndoImageTransFN, terEdUndoImageTransFreeFN, undo_values);
}

static void terEdImageTransTextCB(UITextEntry *text_entry, void *in_val)
{
	Mat4 unused;
	char buf[256];
	const char *pcText = ui_TextEntryGetText(text_entry);
	F32 entry_value = atof(pcText);	
	U16 entry = (U16)in_val;
	TerrainBrushValues *values;
	TerrainDoc *doc = terEdGetDoc();
	if(!doc)
		return;
	values = doc->state.seleted_image;
	if(!values)
		return;

	switch(entry)
	{
	xcase 1:
		values->float_1 = CLAMP(entry_value, -(4096*GRID_BLOCK_SIZE), (4096*GRID_BLOCK_SIZE));
		sprintf(buf, "%g", values->float_1);
		ui_TextEntrySetText(doc->terrain_ui->image_pos_x_text, buf);
	xcase 2:
		values->float_2 = CLAMP(entry_value, -(4096*GRID_BLOCK_SIZE), (4096*GRID_BLOCK_SIZE));
		sprintf(buf, "%g", values->float_2);
		ui_TextEntrySetText(doc->terrain_ui->image_pos_y_text, buf);
	xcase 3:
		values->float_3 = CLAMP(entry_value, -(4096*GRID_BLOCK_SIZE), (4096*GRID_BLOCK_SIZE));
		sprintf(buf, "%g", values->float_3);
		ui_TextEntrySetText(doc->terrain_ui->image_pos_z_text, buf);
	xcase 4:
		entry_value *= (PI/180.0f);
		values->float_4 = CLAMP(entry_value, -PI, PI);
		sprintf(buf, "%g", values->float_4*180.0f/PI);
		ui_TextEntrySetText(doc->terrain_ui->image_rot_p_text, buf);
	xcase 5:
		entry_value *= (PI/180.0f);
		values->float_5 = CLAMP(entry_value, -PI, PI);
		sprintf(buf, "%g", values->float_5*180.0f/PI);
		ui_TextEntrySetText(doc->terrain_ui->image_rot_y_text, buf);
	xcase 6:
		entry_value *= (PI/180.0f);
		values->float_6 = CLAMP(entry_value, -PI, PI);
		sprintf(buf, "%g", values->float_6*180.0f/PI);
		ui_TextEntrySetText(doc->terrain_ui->image_rot_r_text, buf);
	xcase 7:
		values->float_7 = CLAMP(entry_value, 0.0, 100.0);
		sprintf(buf, "%g", values->float_7);
		ui_TextEntrySetText(doc->terrain_ui->image_scale_text, buf);
	xdefault:
		return;
	}
	terEdImageGizmoChanged(doc);
	terEdAddToImageTransUndo(unused, NULL);
}

EMPanel *terEdCreateImageTransPanel(TerrainDoc *doc)
{
	EMPanel *panel;
	UIComboBox *combo;
	UILabel *label;
	UIButton *button;
	UITextEntry *text_entry;

	panel = emPanelCreate("Brush", "Image Transformation", 0);

	label = ui_LabelCreate("Mode: ", 10, 5);
	emPanelAddChild(panel, label, true);
	combo = ui_ComboBoxCreate(90, 5, 155, NULL, NULL, NULL);
	ui_ComboBoxSetEnum(combo, TerrainEditorImageGizmoModeEnum, terEdGizmoModeCB, NULL);
	ui_ComboBoxSetSelectedEnum(combo, TGM_Translate);
	ui_WidgetSetDimensions(UI_WIDGET(combo), 155, 20);
	emPanelAddChild(panel, combo, true);
	doc->terrain_ui->image_mode_combo = combo;

	label = ui_LabelCreate("Scale: ", 10, widgetNextY(label));
	emPanelAddChild(panel, label, true);
	text_entry = ui_TextEntryCreate("1.0", 90, UI_WIDGET(label)->y);
	ui_TextEntrySetFinishedCallback(text_entry, terEdImageTransTextCB, (void*)(intptr_t)(7));
	ui_WidgetSetDimensions(UI_WIDGET(text_entry), 75, 20);
	emPanelAddChild(panel, text_entry, true);
	doc->terrain_ui->image_scale_text = text_entry;

	label = ui_LabelCreate("Position: ", 10, widgetNextY(label));
	emPanelAddChild(panel, label, true);
	text_entry = ui_TextEntryCreate("0.0", 10, widgetNextY(label));
	ui_TextEntrySetFinishedCallback(text_entry, terEdImageTransTextCB, (void*)(intptr_t)(1));
	ui_WidgetSetDimensions(UI_WIDGET(text_entry), 75, 20);
	ui_WidgetSkin(UI_WIDGET(text_entry), s_skinBlueBack);
	emPanelAddChild(panel, text_entry, true);
	doc->terrain_ui->image_pos_x_text = text_entry;
	text_entry = ui_TextEntryCreate("0.0", widgetNextX(text_entry)+5, widgetNextY(label));
	ui_TextEntrySetFinishedCallback(text_entry, terEdImageTransTextCB, (void*)(intptr_t)(2));
	ui_WidgetSetDimensions(UI_WIDGET(text_entry), 75, 20);
	ui_WidgetSkin(UI_WIDGET(text_entry), s_skinGreenBack);
	emPanelAddChild(panel, text_entry, true);
	doc->terrain_ui->image_pos_y_text = text_entry;
	text_entry = ui_TextEntryCreate("0.0", widgetNextX(text_entry)+5, widgetNextY(label));
	ui_TextEntrySetFinishedCallback(text_entry, terEdImageTransTextCB, (void*)(intptr_t)(3));
	ui_WidgetSetDimensions(UI_WIDGET(text_entry), 75, 20);
	ui_WidgetSkin(UI_WIDGET(text_entry), s_skinRedBack);
	emPanelAddChild(panel, text_entry, true);
	doc->terrain_ui->image_pos_z_text = text_entry;
	
	label = ui_LabelCreate("Rotation: ", 10, widgetNextY(text_entry));
	emPanelAddChild(panel, label, true);
	text_entry = ui_TextEntryCreate("0.0", 10, widgetNextY(label));
	ui_TextEntrySetFinishedCallback(text_entry, terEdImageTransTextCB, (void*)(intptr_t)(4));
	ui_WidgetSetDimensions(UI_WIDGET(text_entry), 75, 20);
	ui_WidgetSkin(UI_WIDGET(text_entry), s_skinBlueBack);
	emPanelAddChild(panel, text_entry, true);
	doc->terrain_ui->image_rot_p_text = text_entry;
	text_entry = ui_TextEntryCreate("0.0", widgetNextX(text_entry)+5, widgetNextY(label));
	ui_TextEntrySetFinishedCallback(text_entry, terEdImageTransTextCB, (void*)(intptr_t)(5));
	ui_WidgetSetDimensions(UI_WIDGET(text_entry), 75, 20);
	ui_WidgetSkin(UI_WIDGET(text_entry), s_skinGreenBack);
	emPanelAddChild(panel, text_entry, true);
	doc->terrain_ui->image_rot_y_text = text_entry;
	text_entry = ui_TextEntryCreate("0.0", widgetNextX(text_entry)+5, widgetNextY(label));
	ui_TextEntrySetFinishedCallback(text_entry, terEdImageTransTextCB, (void*)(intptr_t)(6));
	ui_WidgetSetDimensions(UI_WIDGET(text_entry), 75, 20);
	ui_WidgetSkin(UI_WIDGET(text_entry), s_skinRedBack);
	emPanelAddChild(panel, text_entry, true);
	doc->terrain_ui->image_rot_r_text = text_entry;

	button = ui_ButtonCreate("Done", 170, widgetNextY(text_entry)+5, terEdGizmoHideCB, NULL);
	ui_WidgetSetDimensions(UI_WIDGET(button), 75, 20);
	emPanelAddChild(panel, button, true);
	doc->terrain_ui->image_done_button = button;

	doc->terrain_ui->panel_image_trans = panel;
	return panel;
}

void terEdRenameWindowOkCB(UIButton *button, void *unused)
{
	TerrainDoc *doc = terEdGetDoc();
	if(!doc)
		return;		

	if(doc->terrain_ui->rename_brush)
	{
		StructFreeString(doc->terrain_ui->rename_brush->name);
		doc->terrain_ui->rename_brush->name = StructAllocString(ui_TextEntryGetText(doc->terrain_ui->rename_brush_text));
		ui_WidgetSetTextString(UI_WIDGET(doc->terrain_ui->rename_brush->expander), doc->terrain_ui->rename_brush->name);
	}

	ui_WidgetRemoveFromGroup(UI_WIDGET(doc->terrain_ui->rename_brush_window));
}

void terEdRenameWindowCancelCB(UIButton *button, void *unused)
{
	TerrainDoc *doc = terEdGetDoc();
	if(!doc)
		return;		

	ui_WidgetRemoveFromGroup(UI_WIDGET(doc->terrain_ui->rename_brush_window));
}

UIWindow* terEdCreateBrushRenameWindow(TerrainDoc *doc)
{
	UIWindow *window;
	UILabel *label;
	UITextEntry *entry;
	UIButton *button;

	window = ui_WindowCreate("Rename Brush", 500, 400, 0, 0);

	label = ui_LabelCreate("Name: ", 5, 5);
	ui_WindowAddChild(window, label);

	entry = ui_TextEntryCreate("", 60, 5);
	ui_WidgetSetDimensions(UI_WIDGET(entry), 155, 20);
	doc->terrain_ui->rename_brush_text = entry;
	ui_WindowAddChild(window, entry);

	button = ui_ButtonCreate("OK", 60, widgetNextY(entry)+5, terEdRenameWindowOkCB, NULL);
	ui_WidgetSetDimensions(UI_WIDGET(button), 75, 20);
	ui_WindowAddChild(window, button);

	button = ui_ButtonCreate("Cancel", widgetNextX(button)+5, widgetNextY(entry)+5, terEdRenameWindowCancelCB, NULL);
	ui_WidgetSetDimensions(UI_WIDGET(button), 75, 20);
	ui_WindowAddChild(window, button);

	ui_WindowSetResizable(window, false);
	ui_WindowSetDimensions(window, widgetNextX(button)+5, widgetNextY(button)+5, 10, 10);

	doc->terrain_ui->rename_brush_window = window;
	return window;
}


void terEdFillCommonBrushParams(TerrainCommonBrushParams *common, const char *brush_name)
{
	//TER_ED_NAME
	common->brush_diameter = EditorPrefGetFloat( TER_ED_NAME, brush_name, "Diameter",    200.0f);
	common->brush_hardness = EditorPrefGetFloat( TER_ED_NAME, brush_name, "Hardness",    0.5f);
	common->brush_strength = EditorPrefGetFloat( TER_ED_NAME, brush_name, "Strength",    1.0f);
	common->brush_shape    = EditorPrefGetInt(   TER_ED_NAME, brush_name, "Shape",       0);
	common->falloff_type   = EditorPrefGetInt(   TER_ED_NAME, brush_name, "FalloffType", TE_FALLOFF_SCURVE);
}

static void terEdInitializeDefaultBrushes(TerrainDoc *doc)
{
	RefDictIterator it;
	TerrainDefaultBrush *brush;

	RefSystem_InitRefDictIterator(DEFAULT_BRUSH_DICTIONARY, &it);
	while(brush = (TerrainDefaultBrush*)RefSystem_GetNextReferentFromIterator(&it)) 
	{
		if( brush->brush_template.bucket == TBK_OptimizedBrush || 
			brush->brush_template.bucket == TBK_RegularBrush)
		{
			UIAnyWidget **widgets=NULL;
			int i, x=0;

			//Set Common Params
			terEdFillCommonBrushParams(&brush->common, brush->name);

			//Add Toolbar
			if(eaSize(&brush->brush_template.params) < 1)
				continue;

			brush->toolbar = emToolbarCreate(10);

			for(i=0; i < eaSize(&brush->brush_template.params); i++)
			{
				x = terEdMakeUIFromTemplate(doc, &doc->terrain_ui->persistant_token_data, brush->name, &widgets, brush->brush_template.params[i], &brush->default_values, x, TOOL_BAR_TOP, true, true); 
				x += 10;
			}

			for(i=0; i < eaSize(&widgets); i++)
			{
				emToolbarAddChild(brush->toolbar, widgets[i], true);
			}
			eaDestroy(&widgets);
		}
	}
}

void terEdFillOpNamesList(TerrainDoc *doc)
{
	RefDictIterator it;
	TerrainDefaultBrush *brush;

	RefSystem_InitRefDictIterator(DEFAULT_BRUSH_DICTIONARY, &it);
	while(brush = (TerrainDefaultBrush*)RefSystem_GetNextReferentFromIterator(&it)) 
	{
		if( brush->brush_template.bucket == TBK_OptimizedBrush || 
		    brush->brush_template.bucket == TBK_RegularBrush)
		{
			TerrainBrushStringRef *str_ref = StructCreate(parse_TerrainBrushStringRef);
			str_ref->op_name = StructAllocString(brush->name);
			if(brush->display_name)
				str_ref->display_name = StructAllocString(brush->display_name);
			else
				str_ref->display_name = StructAllocString(brush->name);
			eaPush(&doc->state.channel_ops[brush->brush_template.channel].op_refs, str_ref);
		}
	}
}

void terEdReloadMultiBrushImages(const char* relpath, int when)
{
	int i;
	TerrainDoc *doc = terEdGetDoc();

	loadstart_printf( "Reloading Multi Brush Images..." );

	if(doc && doc->state.source)
	{
		terrainQueueLock();
		for( i=0; i < eaSize(&doc->state.source->brush_images) ; i++ )
		{
			TerrainImageBuffer *brush_image = doc->state.source->brush_images[i];
			if(stricmp(relpath, brush_image->file_name) == 0)
			{
				brush_image->needs_reload++;
				break;
			}
		}
		terrainQueueUnlock();
	}

	loadend_printf( "done" );
}

void terEdReloadMultiBrushes(const char* relpath, int when)
{
	int i;
	TerrainDoc *doc = terEdGetDoc();
	char *selected_name = NULL;
	bool found = false;

	loadstart_printf( "Reloading Multi Brushes..." );

	if(	doc && 
		doc->state.persistent && 
		doc->state.persistent->expanded_multi_brush &&
		doc->state.persistent->expanded_multi_brush->filename )
	{
		char buf[64];
		getFileNameNoExt(buf, doc->state.persistent->expanded_multi_brush->filename);
		selected_name = StructAllocString(buf);
	}

	fileWaitForExclusiveAccess( relpath );
	ParserReloadFileToDictionary( relpath, RefSystem_GetDictionaryHandleFromNameOrHandle(MULTI_BRUSH_DICTIONARY) );
	if(doc)
	{
		terEdFillFillteredList(doc);
		if(selected_name)
		{
			for(i=0; i < eaSize(&doc->state.persistent->multi_brush_filtered_list); i++)
			{
				if(stricmp(doc->state.persistent->multi_brush_filtered_list[i], selected_name) == 0)
				{
					ui_ComboBoxSetSelectedObject(doc->terrain_ui->multi_brush_combo, doc->state.persistent->multi_brush_filtered_list[i]);
					found = true;
					break;
				}
			}
			if(!found)
				ui_ComboBoxSetSelectedObject(doc->terrain_ui->multi_brush_combo, NULL);
			StructFreeString(selected_name);
		}
	}

	loadend_printf( "done" );
}

void terEdMultiBrushInit()
{
	terrainBrushInit();
	FolderCacheSetCallback( FOLDER_CACHE_CALLBACK_UPDATE, "editors/terrain/brushes/*.brush", terEdReloadMultiBrushes );
	FolderCacheSetCallback( FOLDER_CACHE_CALLBACK_UPDATE, "editors/terrain/brush_images/*.tga", terEdReloadMultiBrushImages );
}

void terEdInitImageGizmo(TerrainDoc *doc)
{
	doc->state.image_rotate_gizmo = RotateGizmoCreate();
	RotateGizmoSetDeactivateCallback(doc->state.image_rotate_gizmo, terEdAddToImageTransUndo);
	doc->state.image_translate_gizmo = TranslateGizmoCreate();
	TranslateGizmoSetSpecSnap(doc->state.image_translate_gizmo, EditSnapNone);
	TranslateGizmoSetHideGrid(doc->state.image_translate_gizmo, true);
	TranslateGizmoSetDeactivateCallback(doc->state.image_translate_gizmo, terEdAddToImageTransUndo);
	doc->state.image_orig_vals = StructCreate(parse_TerrainBrushValues);
}

void terEdInitBrushUI(TerrainDoc *doc)
{
	//Init Font
	UISkin invis_skin = {0};
	s_skinBlueBack = ui_SkinCreate(NULL);
	s_skinBlueBack->entry[0] = colorFromRGBA(0xAAAAFFFF);
	s_skinGreenBack = ui_SkinCreate(NULL);
	s_skinGreenBack->entry[0] = colorFromRGBA(0xAAFFAAFF);
	s_skinRedBack = ui_SkinCreate(NULL);
	s_skinRedBack->entry[0] = colorFromRGBA(0xFFAAAAFF);
	s_skinInactiveExpander = ui_SkinCreate(&invis_skin);
	s_skinActiveExpander = ui_SkinCreate(NULL);
	SET_HANDLE_FROM_STRING(g_ui_FontDict, "TerrainEditor_Active", s_skinActiveExpander->hNormal);

	//Fill Multibush List
	terEdFillFillteredList(doc);

	//Create Toolbars
	terEdInitializeDefaultBrushes(doc);
	terEdCreateGlobalBrushToolBar(doc);

	//Create Brush Pane
	terEdCreateBrushPane(doc);

	//Create Persistent Panels
    terEdCreateActionsPanel(doc);
	terEdCreateGlobalFiltersPanel(doc);
	terEdCreateMultiBrushPanel(doc);
	terEdCreateImageTransPanel(doc);

	//Create Windows
	terEdCreateBrushRenameWindow(doc);
}

#endif
