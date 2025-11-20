#include "WorldEditorAttributesHelpers.h"

#ifndef NO_EDITORS

#include "StringCache.h"
#include "Expression.h"
#include "gameaction_common.h"
#include "GameActionEditor.h"
#include "EditorObject.h"
#include "WorldGrid.h"
#include "WorldEditorOperations.h"
#include "WorldEditorOptions.h"
#include "WorldEditorClientMain.h"
#include "EString.h"
#include "EditorManager.h"
#include "WorldEditorOptions.h"
#include "WorldEditorAttributes.h"
#include "tokenstore.h"
#include "structInternals.h"
#include "Color.h"
#include "ChoiceTable_common.h"
#include "StringUtil.h"
#include "mission_common.h"
#include "UIDictionaryEntry.h"

#include "WorldEditorAttributesHelpers_h_ast.h"
#include "structinternals_h_ast.h"

AUTO_RUN_ANON(memBudgetAddMapping(__FILE__, BUDGET_Editors););

/********************
* UTIL
********************/
/******
* This function can be used to differentiate between encounter object panels and normal object panels.
* PARAMS:
*   def - GroupDef to test
* RETURNS:
*   bool indicating whether the specified GroupDef is an encounter object
******/
bool wleNeedsEncounterPanels(GroupDef *def)
{
	if (!def)
		return false;
	return (def->property_structs.encounter_hack_properties || def->property_structs.encounter_properties || def->property_structs.patrol_properties ||
		def->property_structs.spawn_properties || def->property_structs.trigger_condition_properties || def->property_structs.layer_fsm_properties ||
		def->property_structs.physical_properties.bNamedPoint);
}

/******
 * This function returns the skin used to skin diff widgets.
 *****/
static UISkin* wleDiffSkin(void)
{
	static UISkin *grayed_skin = NULL;

	if (!grayed_skin)
	{
		grayed_skin = ui_SkinCreate(NULL);
		grayed_skin->entry[0] = colorFromRGBA(0xFFFF88FF);
		grayed_skin->button[0] = colorFromRGBA(0xFFFF88FF);
	}

	return grayed_skin;
}

/******
* This function skins a widget to signify that its corresponding parameter's value is different across
* multiple selections.
* PARAMS:
*   widget - UIWidget to skin
*   diff - if true, use the diff skin
******/
void wleSkinDiffWidget(UIWidget *widget, bool diff)
{
	ui_WidgetSkin(widget, diff ? wleDiffSkin() : NULL);
}

/******
* This function skins a widget to signify that its corresponding parameter's value is different across
* multiple selections.
* PARAMS:
*   widget - UIWidget to skin
*   diff - if true, use the diff skin
*   source - the source, if any
******/
void wleSkinDiffOrSourcedWidget(UIWidget *widget, bool diff, void* source)
{
	static UISkin *inherited_skin = NULL;

	if (!inherited_skin) {
		inherited_skin = ui_SkinCreate(NULL);
		SET_HANDLE_FROM_STRING(g_ui_FontDict, "WorldEditor_Inherited", inherited_skin->hNormal);
	}
	
	if (diff)
		ui_WidgetSkin(widget, wleDiffSkin());
	else if (source)
		ui_WidgetSkin(widget, inherited_skin);
	else
		ui_WidgetSkin(widget, NULL);
}

/******
* This function populates an EArray with all EditorObjects that are being edited according to the current
* selection in the editing selector.
* PARAMS:
*   objects - EArray handle of EditorObjects that will be populated with the EditorObjects being edited currently
******/
void wleAEGetSelectedObjects(EditorObject ***objects)
{
	EditorObject *selected = wleAEGetSelected();

	if (!selected)
		return;

	if (selected->type->objType == EDTYPE_DUMMY)
		edObjSelectionGetAll(objects);
	else
		eaPush(objects, selected);
}

/******
* This function is the button clicked callback for sourced parameters.
* PARAMS:
*   button - UIButton clicked
*   source - EditorObject to select
******/
static void wleAESelectSourceClicked(UIButton *button, EditorObject *source)
{
	edObjSelect(source, true, false);
}

/******
* This function is the spinner start spin callback.
* PARAMS:
*   spinner - UISpinner dragged
******/
static void wleAESpinnerSpinStart(UISpinner *spinner, UserData unused)
{
	EditUndoBeginGroup(edObjGetUndoStack());
}

/******
* This function is the spinner stop spin callback.
* PARAMS:
*   spinner - UISpinner dragged
******/
static void wleAESpinnerSpinStop(UISpinner *spinner, UserData unused)
{
	EditUndoEndGroup(edObjGetUndoStack());
}

/******
* This function calls one of the wleAE*Update functions depending on the parseTable passed in.
* PARAMS:
*   propstruct the struct containing all the params
*   tpi the parsetable for propstruct
*   column the specific parameter to decode
******/
void wleAEUpdate(void *propstruct, ParseTable *tpi, int column)
{
	ParseTable *subtable = NULL;
	void *wleaeparam = TokenStoreGetPointer(tpi, column, propstruct , -1, NULL);
	assert(wleaeparam);
	assert(tpi);
	assert(tpi[column].subtable);

	if (tpi[column].subtable == parse_WleAEParamBool)
		wleAEBoolUpdate(wleaeparam); 
	else if (tpi[column].subtable == parse_WleAEParamInt)
		wleAEIntUpdate(wleaeparam); 
	else if (tpi[column].subtable == parse_WleAEParamFloat)
		wleAEFloatUpdate(wleaeparam); 
	else if (tpi[column].subtable == parse_WleAEParamText)
		wleAETextUpdate(wleaeparam); 
	else if (tpi[column].subtable == parse_WleAEParamCombo)
		wleAEComboUpdate(wleaeparam); 
	else if (tpi[column].subtable == parse_WleAEParamDictionary)
		wleAEDictionaryUpdate(wleaeparam); 
	else if (tpi[column].subtable == parse_WleAEParamVec3)
		wleAEVec3Update(wleaeparam); 
	else if (tpi[column].subtable == parse_WleAEParamHSV)
		wleAEHSVUpdate(wleaeparam); 
	else if (tpi[column].subtable == parse_WleAEParamHue)
		wleAEHueUpdate(wleaeparam); 
	else if (tpi[column].subtable == parse_WleAEParamExpression)
		wleAEExpressionUpdate(wleaeparam); 
	else if (tpi[column].subtable == parse_WleAEParamTexture)
		wleAETextureUpdate(wleaeparam); 
	else if (tpi[column].subtable == parse_WleAEParamPicker)
		wleAEPickerUpdate(wleaeparam); 
	else if (tpi[column].subtable == parse_WleAEParamMessage)
		wleAEMessageUpdate(wleaeparam); 
	else if (tpi[column].subtable == parse_WleAEParamGameAction)
		wleAEGameActionUpdate(wleaeparam); 
	else if (tpi[column].subtable == parse_WleAEParamWorldVariableDef)
		wleAEWorldVariableDefUpdate(wleaeparam); 
	else if (tpi[column].subtable == parse_WleAEParamFormatNewLine)
		0;
	else
		Errorf("Unknown parsetable passed to wleTPIUpdate for editor UI parameter"); 
}

/******
* This function calls wleAEUpdate on every applicable column of the parsetable passed in.
* PARAMS:
*   propstruct the struct containing all the params
*   tpi the parsetable for propstruct
******/
void wleAEUpdateAll(void *propstruct, ParseTable *tpi, U64 exclude_columns)
{
	int i;
	FORALL_PARSETABLE(tpi, i)
	{
		if (!tpi[i].name || !tpi[i].name[0]) continue;
		if (tpi[i].type & TOK_REDUNDANTNAME) continue;
		if (TOK_GET_TYPE(tpi[i].type) == TOK_START) continue;
		if (TOK_GET_TYPE(tpi[i].type) == TOK_END) continue;
		if (TOK_GET_TYPE(tpi[i].type) == TOK_IGNORE) continue;
		if (wleCheckBit(exclude_columns, i)) continue;
		wleAEUpdate(propstruct, tpi, i);
	}
}

/******
* This function calls one of the wleAE*AddWidgetEx functions depending on the parseTable passed in.
* PARAMS:
*   auto_widget_node the node to add the widget to
*   propstruct the struct containing all the params
*   tpi the parsetable for propstruct
*   column the specific parameter to decode
******/
void wleAEAddWidgetEx(UIRebuildableTreeNode *auto_widget_node, void *propstruct, ParseTable *tpi, int column)
{
	ParseTable *subtable = NULL;
	void *wleaeparam = TokenStoreGetPointer(tpi, column, propstruct , -1, NULL);
	const char *paramname = tpi[column].name;
	const char *ui_name = "";
	const char *tooltip = "";
	assert(wleaeparam);
	assert(tpi);
	assert(tpi[column].subtable);

	GetStringFromTPIFormatString(&tpi[column],"UI_NAME",&ui_name);
	GetStringFromTPIFormatString(&tpi[column],"UI_TOOLTIP",&tooltip);

	if (tpi[column].subtable == parse_WleAEParamBool)
		wleAEBoolAddWidgetEx(auto_widget_node, ui_name, tooltip, paramname, wleaeparam);
	else if (tpi[column].subtable == parse_WleAEParamInt)
	{
		int min = INT_MIN;
		int max = INT_MAX;
		int step = 1;
		GetIntFromTPIFormatString(&tpi[column],"UI_PROP_MIN",&min);
		GetIntFromTPIFormatString(&tpi[column],"UI_PROP_MAX",&max);
		GetIntFromTPIFormatString(&tpi[column],"UI_PROP_STEP",&step);
		wleAEIntAddWidgetEx(auto_widget_node, ui_name, tooltip, paramname, wleaeparam, min, max, step); 
	}
	else if (tpi[column].subtable == parse_WleAEParamFloat)
	{
		float min = FLT_MIN; const char *strmin = "";
		float max = FLT_MAX; const char *strmax = "";
		float step = 1.0f; const char *strstep = "";
		GetStringFromTPIFormatString(&tpi[column],"UI_PROP_MIN",&strmin); StringToFloat(strmin, &min);
		GetStringFromTPIFormatString(&tpi[column],"UI_PROP_MAX",&strmax); StringToFloat(strmax, &max);
		GetStringFromTPIFormatString(&tpi[column],"UI_PROP_STEP",&strstep); StringToFloat(strstep, &step);
		wleAEFloatAddWidgetEx(auto_widget_node, ui_name, tooltip, paramname, wleaeparam, min, max, step); 
	}
	else if (tpi[column].subtable == parse_WleAEParamText)
		wleAETextAddWidgetEx(auto_widget_node->root, ui_name, tooltip, paramname, wleaeparam); 
	else if (tpi[column].subtable == parse_WleAEParamCombo)
		wleAEComboAddWidgetEx(auto_widget_node, ui_name, tooltip, paramname, wleaeparam); 
	else if (tpi[column].subtable == parse_WleAEParamDictionary)
		wleAEDictionaryAddWidgetEx(auto_widget_node, ui_name, tooltip, paramname, wleaeparam,true); 
	else if (tpi[column].subtable == parse_WleAEParamVec3)
	{
		Vec3 min = {FLT_MIN,FLT_MIN,FLT_MIN}; const char *strmin = "";
		Vec3 max = {FLT_MAX,FLT_MAX,FLT_MAX}; const char *strmax = "";
		Vec3 step = {1.0f,1.0f,1.0f}; const char *strstep = "";
		GetStringFromTPIFormatString(&tpi[column],"UI_PROP_MIN",&strmin); StringToVec3(strmin, &min);
		GetStringFromTPIFormatString(&tpi[column],"UI_PROP_MAX",&strmax); StringToVec3(strmax, &max);
		GetStringFromTPIFormatString(&tpi[column],"UI_PROP_STEP",&strstep); StringToVec3(strstep, &step);
		wleAEVec3AddWidgetEx(auto_widget_node, ui_name, tooltip, paramname, wleaeparam, min, max, step); 
	}
	else if (tpi[column].subtable == parse_WleAEParamHSV)
		wleAEHSVAddWidgetEx(auto_widget_node, ui_name, tooltip, paramname, wleaeparam); 
	else if (tpi[column].subtable == parse_WleAEParamHue)
	{
		float min = FLT_MIN; const char *strmin = "";
		float max = FLT_MAX; const char *strmax = "";
		float step = 1.0; const char *strstep = "";
		GetStringFromTPIFormatString(&tpi[column],"UI_PROP_MIN",&strmin); StringToFloat(strmin, &min);
		GetStringFromTPIFormatString(&tpi[column],"UI_PROP_MAX",&strmax); StringToFloat(strmax, &max);
		GetStringFromTPIFormatString(&tpi[column],"UI_PROP_STEP",&strstep); StringToFloat(strstep, &step);
		wleAEHueAddWidgetEx(auto_widget_node, ui_name, tooltip, paramname, wleaeparam, min, max, step); 
	}
	else if (tpi[column].subtable == parse_WleAEParamExpression)
		wleAEExpressionAddWidgetEx(auto_widget_node, ui_name, tooltip, paramname, wleaeparam); 
	else if (tpi[column].subtable == parse_WleAEParamTexture)
		wleAETextureAddWidgetEx(auto_widget_node, ui_name, tooltip, paramname, wleaeparam); 
	else if (tpi[column].subtable == parse_WleAEParamPicker)
		wleAEPickerAddWidgetEx(auto_widget_node, ui_name, tooltip, paramname, wleaeparam); 
	else if (tpi[column].subtable == parse_WleAEParamMessage)
		wleAEMessageAddWidgetEx(auto_widget_node, ui_name, tooltip, paramname, wleaeparam, true); 
	else if (tpi[column].subtable == parse_WleAEParamGameAction)
		wleAEGameActionAddWidgetEx(auto_widget_node, ui_name, tooltip, paramname, wleaeparam); 
	else if (tpi[column].subtable == parse_WleAEParamWorldVariableDef)
	{
		const char *inheritedText = "";
		GetStringFromTPIFormatString(&tpi[column],"UI_INHERIT_TEXT",&inheritedText);
		wleAEWorldVariableDefAddWidgetEx(auto_widget_node, ui_name, tooltip, inheritedText, paramname, wleaeparam); 
	}
	else if (tpi[column].subtable == parse_WleAEParamFormatNewLine)
		ui_RebuildableTreeAddLabelKeyed(auto_widget_node, "", paramname, NULL, true);
	else
	{
		//We'll ignore all other fields.
	}
}

/******
* This function calls wleAEAddWidgetEx for each bit column in columns of the parsetable passed in.
* PARAMS:
*   auto_widget_node the node to add the widget to
*   propstruct the struct containing all the params
*   tpi the parsetable for propstruct
*   exclude_columns the specific parameters to exclude (each bit for a column)
******/
void wleAEAddAllWidgets(UIRebuildableTreeNode *auto_widget_node, void *propstruct, ParseTable *tpi, U64 exclude_columns)
{
	int i;
	FORALL_PARSETABLE(tpi, i)
	{
		if (!tpi[i].name || !tpi[i].name[0]) continue;
		if (tpi[i].type & TOK_REDUNDANTNAME) continue;
		if (TOK_GET_TYPE(tpi[i].type) == TOK_START) continue;
		if (TOK_GET_TYPE(tpi[i].type) == TOK_END) continue;
		if (TOK_GET_TYPE(tpi[i].type) == TOK_IGNORE) continue;
		if (wleCheckBit(exclude_columns, i)) continue;
		wleAEAddWidgetEx(auto_widget_node, propstruct, tpi, i);
	}
}

/******
* This function sets the name and entry alignment of a property depending on the parseTable passed in.
* PARAMS:
*   auto_widget_node the node to add the widget to
*   propstruct the struct containing all the params
*   tpi the parsetable for propstruct
*   column the specific parameter to decode
******/
void wleAESetupProperty(void *propstruct, ParseTable *tpi, int col, U32 entry_align)
{
	void *param = TokenStoreGetPointer(tpi, col, propstruct, -1, NULL);
	if (param)
	{
		const char *filter;
		const char *width;
		F32 entry_width = 1.0;
		if (!GetStringFromTPIFormatString(&tpi[col], "UI_FILTER", &filter))
			filter = NULL;

		if (!GetStringFromTPIFormatString(&tpi[col], "UI_WIDTH", &width))
			width = NULL;
		else
			StringToFloat(width, &entry_width);
		
		if (tpi[col].subtable == parse_WleAEParamBool)
		{
			((WleAEParamBool *)param)->property_name = tpi[col].name;
			((WleAEParamBool *)param)->entry_align = entry_align;
			if (filter) wleAEBoolRegisterFilter(param, filter);
		}
		else if (tpi[col].subtable == parse_WleAEParamInt)
		{
			((WleAEParamInt *)param)->property_name = tpi[col].name;
			((WleAEParamInt *)param)->entry_align = entry_align;
			if (width) ((WleAEParamInt *)param)->entry_width = entry_width;
			if (filter) wleAEIntRegisterFilter(param, filter);
		}
		else if (tpi[col].subtable == parse_WleAEParamFloat)
		{
			const char *default_value;
			((WleAEParamFloat *)param)->property_name = tpi[col].name;
			((WleAEParamFloat *)param)->entry_align = entry_align;
			if (width) ((WleAEParamFloat *)param)->entry_width = entry_width;
			if (GetStringFromTPIFormatString(&tpi[col], "UI_PROP_DEFAULT", &default_value))
			{
				if (!StringToFloat(default_value,&((WleAEParamFloat *)param)->default_value))
					((WleAEParamFloat *)param)->default_value = 0.0f;
			}
			if (filter) wleAEFloatRegisterFilter(param, filter);
		}
		else if (tpi[col].subtable == parse_WleAEParamText)
		{
			((WleAEParamText *)param)->property_name = tpi[col].name;
			((WleAEParamText *)param)->entry_align = entry_align;
			if (width) ((WleAEParamText *)param)->entry_width = entry_width;
			if (filter) wleAETextRegisterFilter(param, filter);
		}
		else if (tpi[col].subtable == parse_WleAEParamCombo)
		{
			const char *combo_enum;
			WleAEParamCombo *p = (WleAEParamCombo *)param;
			p->property_name = tpi[col].name;
			p->entry_align = entry_align;
			if (width) p->entry_width = entry_width;
			if (filter) wleAEComboRegisterFilter(param, filter);

			if (GetStringFromTPIFormatString(&tpi[col], "UI_PROP_ENUM", &combo_enum))
			{
				StaticDefineInt *defines;
				defines = FindNamedStaticDefine(combo_enum);
				if (defines)
				{
					intptr_t current_val = INT_MIN;
					bool visible_vals = false;
					int val_count = 0;
					while (defines->key)
					{
						if (PTR_TO_U32(defines->key) > DM_DEFINE_MAX) //skip the AUTO_ENUM internal flags
						{
							if (current_val != defines->value)
							{
								char *aVal = NULL;
								char *vVal = NULL;
								estrPrintf(&aVal, "%s", defines->key);
								if (visible_vals || !visible_vals && val_count++ == 0)
								{
									if (vVal = strchr(aVal, '|'))
									{
										char *v = vVal+1;
										*vVal = '\0';
										vVal = NULL;
										estrPrintf(&vVal, "%s", v);
										estrTrimLeadingAndTrailingWhitespace(&vVal);
										estrSetSize(&aVal, (int)strlen(aVal));
										visible_vals = true;
									}
									if (visible_vals && !vVal)
										estrPrintf(&vVal, "");
								}
								eaPush(&p->available_values, aVal);
								if (visible_vals)
									eaPush(&p->visible_values, vVal);
								current_val = defines->value;
							}
						}
						defines++;
					}
				}
			}
		}
		else if (tpi[col].subtable == parse_WleAEParamDictionary)
		{
			((WleAEParamDictionary *)param)->property_name = tpi[col].name;
			((WleAEParamDictionary *)param)->entry_align = entry_align;
			if (width) ((WleAEParamDictionary *)param)->entry_width = entry_width;
		}
		else if (tpi[col].subtable == parse_WleAEParamVec3)
		{
			((WleAEParamVec3 *)param)->property_name = tpi[col].name;
			((WleAEParamVec3 *)param)->entry_align = entry_align;
			if (width) ((WleAEParamVec3 *)param)->entry_width = entry_width;
		}
		else if (tpi[col].subtable == parse_WleAEParamHSV)
		{
			((WleAEParamHSV *)param)->property_name = tpi[col].name;
			((WleAEParamHSV *)param)->entry_align = entry_align;
			if (width) ((WleAEParamHSV *)param)->entry_width = entry_width;
		}
		else if (tpi[col].subtable == parse_WleAEParamHue)
		{
			((WleAEParamHue *)param)->property_name = tpi[col].name;
			((WleAEParamHue *)param)->entry_align = entry_align;
			if (width) ((WleAEParamHue *)param)->entry_width = entry_width;
		}
		else if (tpi[col].subtable == parse_WleAEParamExpression)
		{
			((WleAEParamExpression *)param)->property_name = tpi[col].name;
			((WleAEParamExpression *)param)->entry_align = entry_align;
			if (width) ((WleAEParamExpression *)param)->entry_width = entry_width;
		}
		else if (tpi[col].subtable == parse_WleAEParamTexture)
		{
			((WleAEParamTexture *)param)->property_name = tpi[col].name;
			if (width) ((WleAEParamTexture *)param)->entry_align = entry_align;
		}
		else if (tpi[col].subtable == parse_WleAEParamPicker)
		{
			//((WleAEParamPicker *)param)->property_name = tpi[col].name;
			((WleAEParamPicker *)param)->entry_align = entry_align;
		}
		else if (tpi[col].subtable == parse_WleAEParamMessage)
		{
			//((WleAEParamMessage *)param)->property_name = tpi[col].name;
			((WleAEParamMessage *)param)->entry_align = entry_align;
			if (width) ((WleAEParamMessage *)param)->entry_width = entry_width;
		}
		else if (tpi[col].subtable == parse_WleAEParamGameAction)
		{
			//((WleAEParamGameAction *)param)->property_name = tpi[col].name;
			((WleAEParamGameAction *)param)->entry_align = entry_align;
			if (width) ((WleAEParamGameAction *)param)->entry_width = entry_width;
		}
		else if (tpi[col].subtable == parse_WleAEParamWorldVariableDef)
		{
			//((WleAEParamWorldVariableDef *)param)->property_name = tpi[col].name;
			((WleAEParamWorldVariableDef *)param)->entry_align = entry_align;
			if (width) ((WleAEParamWorldVariableDef *)param)->entry_width = entry_width;
		}
		else
		{
			//We'll ignore all other fields.
		}
	}
}

/******
* This function sets the name and entry alignment of all properties in the struct/parseTable passed in.
* PARAMS:
*   auto_widget_node the node to add the widget to
*   propstruct the struct containing all the params
*   tpi the parsetable for propstruct
******/
void wleAESetupAllProperties(void *propstruct, ParseTable *tpi, U32 entry_align)
{
	int i;
	FORALL_PARSETABLE(tpi, i)
	{
		wleAESetupProperty(propstruct, tpi, i, entry_align);
	}
}

void wleAESetApplyFunction(void *propstruct, ParseTable *tpi, int col, WleAEParamApplyFunc func)
{
	void *param = TokenStoreGetPointer(tpi, col, propstruct, -1, NULL);
	if (param)
	{
		if (tpi[col].subtable == parse_WleAEParamBool)
			((WleAEParamBool *)param)->apply_func = func;
		else if (tpi[col].subtable == parse_WleAEParamInt)
			((WleAEParamInt *)param)->apply_func = func;
		else if (tpi[col].subtable == parse_WleAEParamFloat)
			((WleAEParamFloat *)param)->apply_func = func;
		else if (tpi[col].subtable == parse_WleAEParamText)
			((WleAEParamText *)param)->apply_func = func;
		else if (tpi[col].subtable == parse_WleAEParamCombo)
			((WleAEParamCombo *)param)->apply_func = func;
		else if (tpi[col].subtable == parse_WleAEParamDictionary)
			((WleAEParamCombo *)param)->apply_func = func;
		else if (tpi[col].subtable == parse_WleAEParamVec3)
			((WleAEParamVec3 *)param)->apply_func = func;
		else if (tpi[col].subtable == parse_WleAEParamHSV)
			((WleAEParamHSV *)param)->apply_func = func;
		else if (tpi[col].subtable == parse_WleAEParamHue)
			((WleAEParamHue *)param)->apply_func = func;
		else if (tpi[col].subtable == parse_WleAEParamExpression)
			((WleAEParamExpression *)param)->apply_func = func;
		else if (tpi[col].subtable == parse_WleAEParamTexture)
			((WleAEParamTexture *)param)->apply_func = func;
		else if (tpi[col].subtable == parse_WleAEParamPicker)
			((WleAEParamPicker *)param)->apply_func = func;
		else if (tpi[col].subtable == parse_WleAEParamMessage)
			((WleAEParamMessage *)param)->apply_func = func;
		else if (tpi[col].subtable == parse_WleAEParamGameAction)
			((WleAEParamGameAction *)param)->apply_func = func;
		else if (tpi[col].subtable == parse_WleAEParamWorldVariableDef)
			((WleAEParamWorldVariableDef *)param)->apply_func = func;
		else
		{
			Errorf("Could not set WLE apply func, unknown property type.");
		}
	}
}

void wleAESetUpdateFunction(void *propstruct, ParseTable *tpi, int col, WleAEParamUpdateFunc func)
{
	void *param = TokenStoreGetPointer(tpi, col, propstruct, -1, NULL);
	if (param)
	{
		if (tpi[col].subtable == parse_WleAEParamBool)
			((WleAEParamBool *)param)->update_func = func;
		else if (tpi[col].subtable == parse_WleAEParamInt)
			((WleAEParamInt *)param)->update_func = func;
		else if (tpi[col].subtable == parse_WleAEParamFloat)
			((WleAEParamFloat *)param)->update_func = func;
		else if (tpi[col].subtable == parse_WleAEParamText)
			((WleAEParamText *)param)->update_func = func;
		else if (tpi[col].subtable == parse_WleAEParamCombo)
			((WleAEParamCombo *)param)->update_func = func;
		else if (tpi[col].subtable == parse_WleAEParamDictionary)
			((WleAEParamCombo *)param)->update_func = func;
		else if (tpi[col].subtable == parse_WleAEParamVec3)
			((WleAEParamVec3 *)param)->update_func = func;
		else if (tpi[col].subtable == parse_WleAEParamHSV)
			((WleAEParamHSV *)param)->update_func = func;
		else if (tpi[col].subtable == parse_WleAEParamHue)
			((WleAEParamHue *)param)->update_func = func;
		else if (tpi[col].subtable == parse_WleAEParamExpression)
			((WleAEParamExpression *)param)->update_func = func;
		else if (tpi[col].subtable == parse_WleAEParamTexture)
			((WleAEParamTexture *)param)->update_func = func;
		else if (tpi[col].subtable == parse_WleAEParamPicker)
			((WleAEParamPicker *)param)->update_func = func;
		else if (tpi[col].subtable == parse_WleAEParamMessage)
			((WleAEParamMessage *)param)->update_func = func;
		else if (tpi[col].subtable == parse_WleAEParamGameAction)
			((WleAEParamGameAction *)param)->update_func = func;
		else if (tpi[col].subtable == parse_WleAEParamWorldVariableDef)
			((WleAEParamWorldVariableDef *)param)->update_func = func;
		else
		{
			Errorf("Could not set WLE update func, unknown property type.");
		}
	}
}

/********************
* BOOL
********************/
// callbacks
static void wleAEBoolChanged(WleAEParamBool *param)
{
	EditorObject **objects = NULL;
	int i;

	wleAEGetSelectedObjects(&objects);
	wleAESetApplyingData(true);

	EditUndoBeginGroup(edObjGetUndoStack());
	for (i = 0; i < eaSize(&objects); i++)
	{
		if (objects[i]->type->objType == EDTYPE_TRACKER && (param->property_name || (param->struct_offset && param->struct_pti && param->struct_fieldname)))
		{
			GroupTracker *tracker = wleOpPropsBegin(objects[i]->obj);
			GroupDef *def = tracker ? tracker->def : NULL;
				
			if (def && param->property_name)
			{
				if (param->is_specified && (param->can_unspecify || param->boolvalue))
					groupDefAddPropertyBool(def, param->property_name, param->boolvalue);
				else
					groupDefRemoveProperty(def, param->property_name);
			}
			else if (def && param->struct_offset && param->struct_pti && param->struct_fieldname)
			{
				void *property_struct = *(void**)((size_t) def + param->struct_offset);
				int col;

				if (ParserFindColumn(param->struct_pti, param->struct_fieldname, &col))
				{
					if (!property_struct)
						property_struct = *(void**)((size_t) def + param->struct_offset) = StructCreateVoid(param->struct_pti);
					if (!param->can_unspecify || param->is_specified)
						TokenStoreSetIntAuto(param->struct_pti, col, property_struct, 0, param->boolvalue, NULL, NULL);
					else
						initstruct_autogen(param->struct_pti, col, property_struct, 0);
				}				
			}
			if (tracker)
			{
				wleOpPropsEndNoUIUpdate();
			}
		}
	}
	if (param->apply_func)
	{
		param->apply_func(param, param->apply_data, objects);
	}
	EditUndoEndGroup(edObjGetUndoStack());
	wleAESetApplyingData(false);
	wleAERefresh();
	eaDestroy(&objects);
}

static void wleAEBoolValueChanged(UIRTNode *node, WleAEParamBool *param)
{
	param->is_specified = true;
	wleAEBoolChanged(param);
}

static void wleAEBoolSpecifiedChanged(UIRTNode *node, WleAEParamBool *param)
{
	assert(param->can_unspecify);
	if (!param->is_specified)
		param->boolvalue = false;
	wleAEBoolChanged(param);
}

// main
static bool wleAEBoolGetValue(EditorObject *obj, WleAEParamBool *param, bool *output)
{
	if (obj->type->objType == EDTYPE_TRACKER && param->property_name && !param->update_func)
	{
		GroupTracker *tracker = trackerFromTrackerHandle(obj->obj);
		GroupDef *def = tracker ? tracker->def : NULL;
		const char *property_val = def ? groupDefFindProperty(def, param->property_name) : NULL;

		if (property_val)
		{
			*output = (strcmp(property_val, "1") == 0);
			return true;
		}
		else
		{
			*output = false;
			return false;
		}
	}
	else if (obj->type->objType == EDTYPE_TRACKER && param->struct_offset && param->struct_pti && param->struct_fieldname)
	{
		GroupTracker *tracker = trackerFromTrackerHandle(obj->obj);
		GroupDef *def = tracker ? tracker->def : NULL;
		void *property_struct = def ? *(void**)((size_t) def + param->struct_offset) : NULL;
		int col;

		if (ParserFindColumn(param->struct_pti, param->struct_fieldname, &col) && property_struct)
		{
			*output = TokenStoreGetIntAuto(param->struct_pti, col, property_struct, 0, 0);
			return !!(*output);
		}
		else
		{
			*output = false;
			return false;
		}
	}
	else
	{
		param->update_func(param, param->update_data, obj);
		*output = param->boolvalue;
		return param->is_specified;
	}
}

void wleAEBoolUpdate(WleAEParamBool *param)
{
	EditorObject **objects = NULL;
	EditorObject *all_source = NULL;
	bool all_bool = false, next_bool;
	bool all_spec = false, next_spec;
	int i;

	param->diff = param->spec_diff = false;
	if (param->source)
	{
		editorObjectDeref(param->source);
		param->source = NULL;
	}
	wleAEGetSelectedObjects(&objects);
	for (i = 0; i < eaSize(&objects); i++)
	{
		EditorObject *obj = objects[i];
		param->source = NULL;

		next_spec = wleAEBoolGetValue(obj, param, &next_bool);
		if (param->source)
			editorObjectRef(param->source);

		if (!i)
		{
			all_bool = next_bool;
			all_spec = next_spec;
			all_source = param->source;
		}
		else
		{
			// compare sources
			if (all_source && (!param->source || edObjCompare(param->source, all_source) != 0))
			{
				if (all_source)
					editorObjectDeref(all_source);
				all_source = NULL;
			}
			if (param->source)
				editorObjectDeref(param->source);

			if (!param->diff && all_bool != next_bool)
				param->diff = true;
			if (!param->spec_diff && all_spec != next_spec)
				param->spec_diff = true;
			if (param->spec_diff && param->diff)
				break;
		}
	}

	param->source = all_source;
	param->is_specified = all_spec;
	if (param->spec_diff && param->can_unspecify)
	{
		param->boolvalue = false;
		param->is_specified = false;
		param->diff = true;
	}
	else if (param->diff)
		param->boolvalue = false;
	else
		param->boolvalue = all_bool;

	eaDestroy(&objects);
}

void wleAEBoolAddWidgetEx(UIRebuildableTreeNode *auto_widget_node, const char *name, const char *tooltip, const char *param_name, WleAEParamBool *param)
{
	UIAutoWidgetParams params = {0};
	char full_param_name[1024];
	bool did_newline = false;

	params.NoLabel = true;
	params.alignTo = param->left_pad;
	params.disabled = param->disabled;

	if (param->can_unspecify)
	{
		sprintf(full_param_name, "%s|%d|is_specified", param_name, param->index);
		ui_AutoWidgetAddKeyed(auto_widget_node, parse_WleAEParamBool, "is_specified", full_param_name, param, true, wleAEBoolSpecifiedChanged, param, &params, tooltip);
		did_newline = true;
		params.alignTo += 20;

		// skin specified check box
		if (auto_widget_node->children)
		{
			UIWidget *check_button = auto_widget_node->children[eaSize(&auto_widget_node->children) - 1]->widget1;
			wleSkinDiffWidget(check_button, param->spec_diff);
		}
	}

	if (param->source)
	{
		UIButton *button = ui_ButtonCreateImageOnly("button_center", 0, 0, wleAESelectSourceClicked, param->source);

		sprintf(full_param_name, "%s|%d|source", param_name, param->index);
		ui_WidgetSetDimensions(UI_WIDGET(button), 20, 20);
		ui_ButtonSetImageStretch(button, true);
		ui_WidgetSetTooltipString(UI_WIDGET(button), param->source->name);
		ui_RebuildableTreeAddWidget(auto_widget_node, UI_WIDGET(button), NULL, false, full_param_name, &params);
		params.alignTo += 30;
	}

	sprintf(full_param_name, "%s|%d|label", param_name, param->index);
	ui_RebuildableTreeAddLabelKeyed(auto_widget_node, name, full_param_name, &params, !did_newline);
	if (auto_widget_node->children)
	{
		UIWidget *label = auto_widget_node->children[eaSize(&auto_widget_node->children) - 1]->widget1;
		wleSkinDiffOrSourcedWidget(label, false, param->source);
	}

	params.alignTo += ((param->entry_align ? param->entry_align : 120) + ((!param->can_unspecify && name) ? 20 : 0));
	sprintf(full_param_name, "%s|%d|boolvalue", param_name, param->index);
	ui_AutoWidgetAddKeyed(auto_widget_node, parse_WleAEParamBool, "boolvalue", full_param_name, param, false, wleAEBoolValueChanged, param, &params, tooltip);

	// get last node and skin it as necessary
	if (auto_widget_node->children)
	{
		UIWidget *check_button = auto_widget_node->children[eaSize(&auto_widget_node->children) - 1]->widget1;
		wleSkinDiffWidget(check_button, param->diff);
	}
}

bool wleAEWorldVariableDefDoorHasVarsDisabled(WleAEParamWorldVariableDef *param)
{
	if( param->var_init_from_diff ) {
		return false;
	}

	if (param->var_def.eDefaultType == WVARDEF_SPECIFY_DEFAULT) {
		return !(param->var_def.pSpecificValue
				 && param->var_def.pSpecificValue->pcZoneMap
				 && param->var_def.pSpecificValue->pcZoneMap[0]);
	} else {
		return false;
	}
}

WorldVariable* wleAEWorldVariableCalcVariableNonRandom( WorldVariableDef* varDef )
{
	if( !varDef ) {
		return NULL;
	}
	
	switch( varDef->eDefaultType ) {
		case WVARDEF_SPECIFY_DEFAULT: {
			return varDef->pSpecificValue;
		}
			
		case WVARDEF_CHOICE_TABLE: {
			ChoiceTable* table = GET_REF( varDef->choice_table );
			if( table ) {
				return choice_ChooseValueForClient( table, varDef->choice_name );
			}
		}
		case WVARDEF_MAP_VARIABLE: {
			WorldVariableDef* mapVar = zmapInfoGetVariableDefByName( NULL, varDef->map_variable );
			return wleAEWorldVariableCalcVariableNonRandom( mapVar );
		}
	}

	return NULL;
}

static bool wleAEBoolCriterionCheckCallback(EditorObject *obj, const char *propertyName, WleCriterionCond cond, const char *val, WleAEParamBool *param)
{
	bool returnVal = false;

	wleAEBoolGetValue(obj, param, &returnVal);

	// ignore source
	if (param->source)
		editorObjectDeref(param->source);

	assert(cond == WLE_CRIT_EQUAL);
	return returnVal == (strcmpi(val, "true") == 0);
}

WleCriterion *wleAEBoolRegisterFilter(WleAEParamBool *param, const char *propertyName)
{
	WleCriterion *criterion = StructCreate(parse_WleCriterion);
	criterion->propertyName = StructAllocString(propertyName);
	eaiPush(&criterion->allConds, WLE_CRIT_EQUAL);
	eaPush(&criterion->possibleValues, StructAllocString("true"));
	eaPush(&criterion->possibleValues, StructAllocString("false"));
	criterion->checkCallback = wleAEBoolCriterionCheckCallback;
	criterion->checkData = StructClone(parse_WleAEParamBool, param);
	wleCriterionRegister(criterion);
	return criterion;
}

/********************
* INT
********************/
// callbacks
static void wleAEIntChanged(WleAEParamInt *param)
{
	EditorObject **objects = NULL;
	int i;

	wleAEGetSelectedObjects(&objects);
	wleAESetApplyingData(true);

	EditUndoBeginGroup(edObjGetUndoStack());
	for (i = 0; i < eaSize(&objects); i++)
	{
		if (objects[i]->type->objType == EDTYPE_TRACKER && (param->property_name || (param->struct_offset && param->struct_pti && param->struct_fieldname)))
		{
			GroupTracker *tracker = wleOpPropsBegin(objects[i]->obj);
			GroupDef *def = tracker ? tracker->def : NULL;

			if (def && param->property_name)
			{
				if (param->is_specified && (param->can_unspecify || param->intvalue != 0))
					groupDefAddPropertyF32(def, param->property_name, param->intvalue * 1.0f);
				else
					groupDefRemoveProperty(def, param->property_name);
			}
			else if (def && param->struct_offset && param->struct_pti && param->struct_fieldname)
			{
				void *property_struct = *(void**)((size_t) def + param->struct_offset);
				int col;

				if (ParserFindColumn(param->struct_pti, param->struct_fieldname, &col))
				{
					if (!property_struct)
						property_struct = *(void**)((size_t) def + param->struct_offset) = StructCreateVoid(param->struct_pti);
					if (!param->can_unspecify || param->is_specified)
						TokenStoreSetInt(param->struct_pti, col, property_struct, 0, param->intvalue, NULL, NULL);
					else
						initstruct_autogen(param->struct_pti, col, property_struct, 0);
				}				
			}
			if (tracker)
			{
				wleOpPropsEndNoUIUpdate();
			}
		}
	}
	if (param->apply_func)
	{
		param->apply_func(param, param->apply_data, objects);
	}
	EditUndoEndGroup(edObjGetUndoStack());
	wleAESetApplyingData(false);
	wleAERefresh();
	eaDestroy(&objects);
}

static void wleAEIntValueChanged(UIRTNode *node, WleAEParamInt *param)
{
	param->is_specified = true;
	wleAEIntChanged(param);
}

static void wleAEIntSpecifiedChanged(UIRTNode *node, WleAEParamInt *param)
{
	assert(param->can_unspecify);
	if (!param->is_specified)
		param->intvalue = 0;
	wleAEIntChanged(param);
}

// main
static bool wleAEIntGetValue(EditorObject *obj, WleAEParamInt *param, int *output)
{
	if (obj->type->objType == EDTYPE_TRACKER && param->property_name && !param->update_func)
	{
		GroupTracker *tracker = trackerFromTrackerHandle(obj->obj);
		GroupDef *def = tracker ? tracker->def : NULL;
		const char *property_val = def ? groupDefFindProperty(def, param->property_name) : NULL;

		if (property_val)
		{
			*output = fillF32FromStr(property_val, 0);
			return true;
		}
		else
		{
			*output = 0;
			return false;
		}
	}
	else if (obj->type->objType == EDTYPE_TRACKER && param->struct_offset && param->struct_pti && param->struct_fieldname)
	{
		GroupTracker *tracker = trackerFromTrackerHandle(obj->obj);
		GroupDef *def = tracker ? tracker->def : NULL;
		void *property_struct = def ? *(void**)((size_t) def + param->struct_offset) : NULL;
		int col;

		if (ParserFindColumn(param->struct_pti, param->struct_fieldname, &col) && property_struct)
		{
			*output = TokenStoreGetInt(param->struct_pti, col, property_struct, 0, 0);
			return !!(*output);
		}
		else
		{
			*output = 0;
			return false;
		}
	}
	else
	{
		param->update_func(param, param->update_data, obj);
		*output = param->intvalue;
		return param->is_specified;
	}
}

void wleAEIntUpdate(WleAEParamInt *param)
{
	EditorObject **objects = NULL;
	int all_int = 0, next_int;
	bool all_spec = false, next_spec;
	int i;

	param->diff = param->spec_diff = false;
	wleAEGetSelectedObjects(&objects);
	for (i = 0; i < eaSize(&objects); i++)
	{
		EditorObject *obj = objects[i];

		next_spec = wleAEIntGetValue(obj, param, &next_int);

		if (!i)
		{
			all_int = next_int;
			all_spec = next_spec;
		}
		else
		{
			if (!param->diff && all_int != next_int)
				param->diff = true;
			if (!param->spec_diff && all_spec != next_spec)
				param->spec_diff = true;
			if (param->spec_diff && param->diff)
				break;
		}
	}

	param->is_specified = all_spec;
	if (param->spec_diff && param->can_unspecify)
	{
		param->intvalue = 0;
		param->is_specified = false;
		param->diff = true;
	}
	else if (param->diff)
		param->intvalue = 0;
	else
		param->intvalue = all_int;

	eaDestroy(&objects);
}

void wleAEIntAddWidgetEx(UIRebuildableTreeNode *auto_widget_node, const char *name, const char *tooltip, const char *param_name, WleAEParamInt *param, int min, int max, int step)
{
	UIAutoWidgetParams params = {0};
	char full_param_name[1024];
	bool did_newline = false;

	params.NoLabel = true;
	params.alignTo = param->left_pad;
	params.disabled = param->disabled;

	if (param->can_unspecify)
	{
		sprintf(full_param_name, "%s|%d|is_specified", param_name, param->index);
		ui_AutoWidgetAddKeyed(auto_widget_node, parse_WleAEParamInt, "is_specified", full_param_name, param, true, wleAEIntSpecifiedChanged, param, &params, tooltip);
		did_newline = true;
		params.alignTo += 20;

		// skin specified check box
		if (auto_widget_node->children)
		{
			UIWidget *check_button = auto_widget_node->children[eaSize(&auto_widget_node->children) - 1]->widget1;
			wleSkinDiffWidget(check_button, param->spec_diff);
		}
	}

/*	if (param->source && param->source_handle)
	{
		UIButton *button = ui_ButtonCreateImageOnly("button_center", 0, 0, linkToTrackerHandle, param->source_handle);

		sprintf(full_param_name, "%s|%d|source", param_name, param->index);
		params.type = AWT_Default;
		ui_WidgetSetDimensions(UI_WIDGET(button), 20, 20);
		ui_WidgetSetDimensions(UI_WIDGET(button->sprite), 20, 20);
		ui_WidgetSetTooltipString(UI_WIDGET(button), param->source);
		ui_RebuildableTreeAddWidget(auto_widget_node, UI_WIDGET(button), NULL, false, full_param_name, &params);
		params.alignTo += 30;
	}*/

	sprintf(full_param_name, "%s|%d|label", param_name, param->index);
	ui_RebuildableTreeAddLabelKeyed(auto_widget_node, name, full_param_name, &params, !did_newline);
//	if (param->source && param->source_handle)
//		wleSkinSourcedWidget(auto_widget_node->children[eaSize(&auto_widget_node->children) - 1]->widget1);

	params.alignTo += ((param->entry_align ? param->entry_align : 120) + (!param->can_unspecify ? 20 : 0));
	params.type = AWT_Spinner;
	params.spinnerStartF = wleAESpinnerSpinStart;
	params.spinnerStopF = wleAESpinnerSpinStop;
	params.overrideWidth = param->entry_width;
	setVec3same(params.min, min);
	setVec3same(params.max, max);
	setVec3same(params.step, step);
	sprintf(full_param_name, "%s|%d|intvalue", param_name, param->index);
	ui_AutoWidgetAddKeyed(auto_widget_node, parse_WleAEParamInt, "intvalue", full_param_name, param, false, wleAEIntValueChanged, param, &params, tooltip);

	// skin widget as necessary
	if (auto_widget_node->children)
	{
		UIWidget *text_entry = auto_widget_node->children[eaSize(&auto_widget_node->children) - 1]->widget1;
		wleSkinDiffWidget(text_entry, param->diff);
		if (param->diff)
		{
			ui_TextEntrySetText((UITextEntry*) text_entry, "");
		}
	}
}

static bool wleAEIntCriterionCheckCallback(EditorObject *obj, const char *propertyName, WleCriterionCond cond, const char *val, WleAEParamInt *param)
{
	int iVal = atoi(val);
	int returnVal;
	bool ret;

	wleAEIntGetValue(obj, param, &returnVal);

	if (!wleCriterionNumTest((float) returnVal, (float) iVal, cond, &ret))
		ret = false;

	return ret;
}

WleCriterion *wleAEIntRegisterFilter(WleAEParamInt *param, const char *propertyName)
{
	WleCriterion *criterion = StructCreate(parse_WleCriterion);
	criterion->propertyName = StructAllocString(propertyName);
	eaiPush(&criterion->allConds, WLE_CRIT_EQUAL);
	eaiPush(&criterion->allConds, WLE_CRIT_NOT_EQUAL);
	eaiPush(&criterion->allConds, WLE_CRIT_LESS_THAN);
	eaiPush(&criterion->allConds, WLE_CRIT_GREATER_THAN);
	criterion->checkCallback = wleAEIntCriterionCheckCallback;
	criterion->checkData = StructClone(parse_WleAEParamInt, param);
	wleCriterionRegister(criterion);
	return criterion;
}

/********************
* FLOAT
********************/
// callbacks
static void wleAEFloatChanged(WleAEParamFloat *param)
{
	EditorObject **objects = NULL;
	int i;

	wleAEGetSelectedObjects(&objects);
	wleAESetApplyingData(true);

	EditUndoBeginGroup(edObjGetUndoStack());
	for (i = 0; i < eaSize(&objects); i++)
	{
		if (objects[i]->type->objType == EDTYPE_TRACKER  && (param->property_name || (param->struct_offset && param->struct_pti && param->struct_fieldname)))
		{
			GroupTracker *tracker = wleOpPropsBegin(objects[i]->obj);
			GroupDef *def = tracker ? tracker->def : NULL;

			if (def && param->property_name)
			{
				if (param->is_specified && (param->can_unspecify || param->floatvalue != 0))
					groupDefAddPropertyF32(def, param->property_name, param->floatvalue * 1.0f);
				else
					groupDefRemoveProperty(def, param->property_name);
			}
			else if (def && param->struct_offset && param->struct_pti && param->struct_fieldname)
			{
				void *property_struct = *(void**)((size_t) def + param->struct_offset);
				int col;

				if (ParserFindColumn(param->struct_pti, param->struct_fieldname, &col))
				{
					if (!property_struct)
						property_struct = *(void**)((size_t) def + param->struct_offset) = StructCreateVoid(param->struct_pti);
					if (!param->can_unspecify || param->is_specified)
						TokenStoreSetF32(param->struct_pti, col, property_struct, 0, param->floatvalue, NULL, NULL);
					else
						initstruct_autogen(param->struct_pti, col, property_struct, 0);
				}				
			}
			if (tracker)
			{
				wleOpPropsEndNoUIUpdate();
			}
		}
	}
	if (param->apply_func)
	{
		param->apply_func(param, param->apply_data, objects);
	}
	EditUndoEndGroup(edObjGetUndoStack());
	wleAESetApplyingData(false);
	wleAERefresh();
	eaDestroy(&objects);
}

static void wleAEFloatValueChanged(UIRTNode *node, WleAEParamFloat *param)
{
	param->is_specified = true;
	wleAEFloatChanged(param);
}

static void wleAEFloatSpecifiedChanged(UIRTNode *node, WleAEParamFloat *param)
{
	assert(param->can_unspecify);
	if (!param->is_specified)
		param->floatvalue = 0;
	wleAEFloatChanged(param);
}

// main
static bool wleAEFloatGetValue(EditorObject *obj, WleAEParamFloat *param, float *output)
{
	if (obj->type->objType == EDTYPE_TRACKER && param->property_name && !param->update_func)
	{
		GroupTracker *tracker = trackerFromTrackerHandle(obj->obj);
		GroupDef *def = tracker ? tracker->def : NULL;
		const char *property_val = def ? groupDefFindProperty(def, param->property_name) : NULL;

		if (property_val)
		{
			*output = fillF32FromStr(property_val, 0);
			return true;
		}
		else
		{
			*output = param->default_value;
			return false;
		}
	}
	else if (obj->type->objType == EDTYPE_TRACKER && param->struct_offset && param->struct_pti && param->struct_fieldname)
	{
		GroupTracker *tracker = trackerFromTrackerHandle(obj->obj);
		GroupDef *def = tracker ? tracker->def : NULL;
		void *property_struct = def ? *(void**)((size_t) def + param->struct_offset) : NULL;
		int col;

		if (ParserFindColumn(param->struct_pti, param->struct_fieldname, &col) && property_struct)
		{
			*output = TokenStoreGetF32(param->struct_pti, col, property_struct, 0, 0);
			return !!(*output);
		}
		else
		{
			*output = 0;
			return false;
		}
	}
	else
	{
		param->update_func(param, param->update_data, obj);
		*output = param->floatvalue;
		return param->is_specified;
	}
}

void wleAEFloatUpdate(WleAEParamFloat *param)
{
	EditorObject **objects = NULL;
	EditorObject *all_source = NULL;
	float all_float = 0, next_float;
	bool all_spec = false, next_spec;
	int i;

	param->diff = param->spec_diff = false;
	wleAEGetSelectedObjects(&objects);
	if (param->source)
	{
		editorObjectDeref(param->source);
		param->source = NULL;
	}
	for (i = 0; i < eaSize(&objects); i++)
	{
		EditorObject *obj = objects[i];
		param->source = NULL;

		next_spec = wleAEFloatGetValue(obj, param, &next_float);
		if (param->source)
			editorObjectRef(param->source);

		if (!i)
		{
			all_float = next_float;
			all_spec = next_spec;
			all_source = param->source;
		}
		else
		{
			// compare sources
			if (all_source && (!param->source || edObjCompare(param->source, all_source) != 0))
			{
				if (all_source)
					editorObjectDeref(all_source);
				all_source = NULL;
			}
			if (param->source)
				editorObjectDeref(param->source);

			if (!param->diff && all_float != next_float)
				param->diff = true;
			if (!param->spec_diff && all_spec != next_spec)
				param->spec_diff = true;
			if (param->spec_diff && param->diff)
				break;
		}
	}

	param->source = all_source;
	param->is_specified = all_spec;
	if (param->spec_diff && param->can_unspecify)
	{
		param->floatvalue = 0;
		param->is_specified = false;
		param->diff = true;
	}
	else if (param->diff)
		param->floatvalue = 0;
	else
		param->floatvalue = all_float;

	eaDestroy(&objects);
}

void wleAEFloatAddWidgetEx(UIRebuildableTreeNode *auto_widget_node, const char *name, const char *tooltip, const char *param_name, WleAEParamFloat *param, float min, float max, float step)
{
	UIAutoWidgetParams params = {0};
	char full_param_name[1024];
	bool did_newline = false;

	params.NoLabel = true;
	params.alignTo = param->left_pad;
	params.disabled = param->disabled;

	if (param->can_unspecify)
	{
		sprintf(full_param_name, "%s|%d|is_specified", param_name, param->index);
		ui_AutoWidgetAddKeyed(auto_widget_node, parse_WleAEParamFloat, "is_specified", full_param_name, param, true, wleAEFloatSpecifiedChanged, param, &params, tooltip);
		did_newline = true;
		params.alignTo += 20;

		// skin specified check box
		if (auto_widget_node->children)
		{
			UIWidget *check_button = auto_widget_node->children[eaSize(&auto_widget_node->children) - 1]->widget1;
			wleSkinDiffWidget(check_button, param->spec_diff);
		}
	}

	if (param->source)
	{
		UIButton *button = ui_ButtonCreateImageOnly("button_center", 0, 0, wleAESelectSourceClicked, param->source);

		sprintf(full_param_name, "%s|%d|source", param_name, param->index);
		ui_WidgetSetDimensions(UI_WIDGET(button), 20, 20);
		ui_ButtonSetImageStretch(button, true);
		ui_WidgetSetTooltipString(UI_WIDGET(button), param->source->name);
		ui_RebuildableTreeAddWidget(auto_widget_node, UI_WIDGET(button), NULL, false, full_param_name, &params);
		params.alignTo += 30;
	}

	sprintf(full_param_name, "%s|%d|label", param_name, param->index);
	ui_RebuildableTreeAddLabelKeyed(auto_widget_node, name, full_param_name, &params, !did_newline);
	if (auto_widget_node->children)
	{
		UIWidget *label = auto_widget_node->children[eaSize(&auto_widget_node->children) - 1]->widget1;
		wleSkinDiffOrSourcedWidget(label, false, param->source);
	}

	params.alignTo += ((param->entry_align ? param->entry_align : 120) + (!param->can_unspecify ? 20 : 0));
	params.type = AWT_Spinner;
	params.spinnerStartF = wleAESpinnerSpinStart;
	params.spinnerStopF = wleAESpinnerSpinStop;
	params.precision = param->precision;
	params.overrideWidth = param->entry_width;
	setVec3same(params.min, min);
	setVec3same(params.max, max);
	setVec3same(params.step, step);
	sprintf(full_param_name, "%s|%d|intvalue", param_name, param->index);
	ui_AutoWidgetAddKeyed(auto_widget_node, parse_WleAEParamFloat, "floatvalue", full_param_name, param, false, wleAEFloatValueChanged, param, &params, tooltip);

	// skin widget as necessary
	if (auto_widget_node->children)
	{
		UIWidget *text_entry = auto_widget_node->children[eaSize(&auto_widget_node->children) - 1]->widget1;
		wleSkinDiffWidget(text_entry, param->diff);
		if (param->diff)
		{
			ui_TextEntrySetText((UITextEntry*) text_entry, "");
		}
	}
}

static bool wleAEFloatCriterionCheckCallback(EditorObject *obj, const char *propertyName, WleCriterionCond cond, const char *val, WleAEParamFloat *param)
{
	float fVal = atof(val);
	float returnVal;
	bool ret;

	wleAEFloatGetValue(obj, param, &returnVal);

	// ignore source
	if (param->source)
		editorObjectDeref(param->source);

	if (!wleCriterionNumTest(returnVal, fVal, cond, &ret))
		ret = false;

	return ret;
}

WleCriterion *wleAEFloatRegisterFilter(WleAEParamFloat *param, const char *propertyName)
{
	WleCriterion *criterion = StructCreate(parse_WleCriterion);
	criterion->propertyName = StructAllocString(propertyName);
	eaiPush(&criterion->allConds, WLE_CRIT_EQUAL);
	eaiPush(&criterion->allConds, WLE_CRIT_LESS_THAN);
	eaiPush(&criterion->allConds, WLE_CRIT_GREATER_THAN);
	criterion->checkCallback = wleAEFloatCriterionCheckCallback;
	criterion->checkData = StructClone(parse_WleAEParamFloat, param);
	wleCriterionRegister(criterion);
	return criterion;
}

/********************
* TEXT
********************/
// callbacks
static void wleAETextChanged(WleAEParamText *param)
{
	EditorObject **objects = NULL;
	int i;

	wleAEGetSelectedObjects(&objects);
	wleAESetApplyingData(true);

	EditUndoBeginGroup(edObjGetUndoStack());
	for (i = 0; i < eaSize(&objects); i++)
	{
		if (objects[i]->type->objType == EDTYPE_TRACKER && (param->property_name || (param->struct_offset && param->struct_pti && param->struct_fieldname)))
		{
			GroupTracker *tracker = wleOpPropsBegin(objects[i]->obj);
			GroupDef *def = tracker ? tracker->def : NULL;

			if (def && param->property_name)
			{
				if (param->is_specified && (param->can_unspecify || param->stringvalue))
					groupDefAddProperty(def, param->property_name, param->stringvalue ? param->stringvalue : "");
				else
					groupDefRemoveProperty(def, param->property_name);
			}
			else if (def && param->struct_offset && param->struct_pti && param->struct_fieldname)
			{
				void *property_struct = *(void**)((size_t) def + param->struct_offset);
				int col;

				if (ParserFindColumn(param->struct_pti, param->struct_fieldname, &col))
				{
					if (!property_struct)
						property_struct = *(void**)((size_t) def + param->struct_offset) = StructCreateVoid(param->struct_pti);
					if (!param->can_unspecify || param->is_specified)
						TokenStoreSetString(param->struct_pti, col, property_struct, 0, param->stringvalue, NULL, NULL, NULL, NULL);
					else
						initstruct_autogen(param->struct_pti, col, property_struct, 0);
				}				
			}
			if (tracker)
			{
				wleOpPropsEndNoUIUpdate();
			}
		}
	}
	if (param->apply_func)
	{
		param->apply_func(param, param->apply_data, objects);
	}
	EditUndoEndGroup(edObjGetUndoStack());
	wleAESetApplyingData(false);
	wleAERefresh();
	eaDestroy(&objects);
}

static void wleAETextValueChanged(UIRTNode *node, WleAEParamText *param)
{
	param->is_specified = true;
	wleAETextChanged(param);
}

static void wleAETextSpecifiedChanged(UIRTNode *node, WleAEParamText *param)
{
	assert(param->can_unspecify);
	if (!param->is_specified)
	{
		if (param->stringvalue)
			StructFreeString(param->stringvalue);
		param->stringvalue = NULL;
	}
	wleAETextChanged(param);
}

// main
static bool wleAETextGetValue(EditorObject *obj, WleAEParamText *param, char **output)
{
	if (obj->type->objType == EDTYPE_TRACKER && param->property_name && !param->update_func)
	{
		GroupTracker *tracker = trackerFromTrackerHandle(obj->obj);
		GroupDef *def = tracker ? tracker->def : NULL;
		const char *property_val = def ? groupDefFindProperty(def, param->property_name) : NULL;

		if (property_val)
		{
			*output = StructAllocString(property_val);
			return true;
		}
		else
		{
			*output = NULL;
			return false;
		}
	}
	else if (obj->type->objType == EDTYPE_TRACKER && param->struct_offset && param->struct_pti && param->struct_fieldname)
	{
		GroupTracker *tracker = trackerFromTrackerHandle(obj->obj);
		GroupDef *def = tracker ? tracker->def : NULL;
		void *property_struct = def ? *(void**)((size_t) def + param->struct_offset) : NULL;
		int col;

		if (ParserFindColumn(param->struct_pti, param->struct_fieldname, &col) && property_struct)
		{
			*output = StructAllocString(TokenStoreGetString(param->struct_pti, col, property_struct, 0, 0));
			return !!(*output);
		}
		else
		{
			*output = NULL;
			return false;
		}
	}
	else
	{
		param->update_func(param, param->update_data, obj);
		*output = param->stringvalue;
		return param->is_specified;
	}
}

void wleAETextUpdate(WleAEParamText *param)
{
	EditorObject **objects = NULL;
	char *all_string = NULL, *next_string;
	bool all_spec = false, next_spec;
	int i;

	if (param->stringvalue)
	{
		StructFreeString(param->stringvalue);
		param->stringvalue = NULL;
	}
	param->diff = param->spec_diff = false;
	wleAEGetSelectedObjects(&objects);
	for (i = 0; i < eaSize(&objects); i++)
	{
		EditorObject *obj = objects[i];

		next_spec = wleAETextGetValue(obj, param, &next_string);

		if (!i)
		{
			all_string = StructAllocString(next_string);
			all_spec = next_spec;
		}
		else
		{
			if (!param->diff)
			{
				if ((!all_string && next_string) || (all_string && !next_string))
					param->diff = true;
				else if (all_string && next_string && strcmp(all_string, next_string) != 0)
					param->diff = true;
			}
			if (!param->spec_diff && all_spec != next_spec)
				param->spec_diff = true;
		}

		if (next_string)
			StructFreeString(next_string);
		if (param->spec_diff && param->diff)
			break;
	}

	param->is_specified = all_spec;
	if (param->spec_diff && param->can_unspecify)
	{
		param->stringvalue = NULL;
		param->is_specified = false;
		param->diff = true;
	}
	else if (param->diff)
		param->stringvalue = NULL;
	else
		param->stringvalue = StructAllocString(all_string);

	if (all_string)
		StructFreeString(all_string);
	eaDestroy(&objects);
}

void wleAETextAddWidgetEx(UIRebuildableTree *auto_widget, const char *name, const char *tooltip, const char *param_name, WleAEParamText *param)
{
	UIAutoWidgetParams params = {0};
	UITextEntry *entry;
	UIComboBox *combo;
	char full_param_name[1024];
	bool did_newline = false;

	params.NoLabel = true;
	params.alignTo = param->left_pad;
	params.disabled = param->disabled;

	if (param->can_unspecify)
	{
		sprintf(full_param_name, "%s|%d|is_specified", param_name, param->index);
		ui_AutoWidgetAddKeyed(auto_widget->root, parse_WleAEParamText, "is_specified", full_param_name, param, true, wleAETextSpecifiedChanged, param, &params, tooltip);
		did_newline = true;
		params.alignTo += 20;

		// skin specified check box
		if (auto_widget->root->children)
		{
			UIWidget *check_button = auto_widget->root->children[eaSize(&auto_widget->root->children) - 1]->widget1;
			wleSkinDiffWidget(check_button, param->spec_diff);
		}
	}

/*	if (param->source && param->source_handle)
	{
		UIButton *button = ui_ButtonCreateImageOnly("button_center", 0, 0, linkToTrackerHandle, param->source_handle);

		sprintf(full_param_name, "%s|%d|source", param_name, param->index);
		params.type = AWT_Default;
		ui_WidgetSetDimensions(UI_WIDGET(button), 20, 20);
		ui_WidgetSetDimensions(UI_WIDGET(button->sprite), 20, 20);
		ui_WidgetSetTooltipString(UI_WIDGET(button), param->source);
		ui_RebuildableTreeAddWidget(auto_widget->root, UI_WIDGET(button), NULL, false, full_param_name, &params);
		params.alignTo += 30;
	}*/

	sprintf(full_param_name, "%s|%d|label", param_name, param->index);
	ui_RebuildableTreeAddLabelKeyed(auto_widget->root, name, full_param_name, &params, !did_newline);
//	if (param->source && param->source_handle)
//		wleSkinSourcedWidget(auto_widget->root->children[eaSize(&auto_widget->root->children) - 1]->widget1);

	params.alignTo += ((param->entry_align ? param->entry_align : 120) + (!param->can_unspecify ? 20 : 0));
	params.overrideWidth = param->entry_width;
	sprintf(full_param_name, "%s|%d|strvalue", param_name, param->index);
	ui_AutoWidgetAddKeyed(auto_widget->root, parse_WleAEParamText, "stringvalue", full_param_name, param, false, wleAETextValueChanged, param, &params, tooltip);
	entry = (UITextEntry*)ui_RebuildableTreeGetWidgetByName(auto_widget, full_param_name);
	if (entry)
	{
		if (param->available_values)
		{
			entry->comboFinishOnSelect = true;
			combo = entry->cb;
			if (!param->is_filtered)
			{
				if (!combo)
					combo = ui_ComboBoxCreate(0, 0, 0, NULL, &param->available_values, NULL);
				else
					ui_ComboBoxSetModelNoCallback(combo, NULL, &param->available_values);
			}
			else
			{
				if (!combo)
					combo = (UIComboBox*) ui_FilteredComboBoxCreate(0, 0, 0, NULL, &param->available_values, NULL);
				else
					ui_ComboBoxSetModelNoCallback(combo, NULL, &param->available_values);
			}
			ui_TextEntrySetComboBox(entry, combo);
		}
		else
			ui_TextEntrySetComboBox(entry, NULL);
	}

	if (auto_widget->root->children)
	{
		UIWidget *text_entry = auto_widget->root->children[eaSize(&auto_widget->root->children) - 1]->widget1;
		wleSkinDiffWidget(text_entry, param->diff);
	}
}

static bool wleAETextCriterionCheckCallback(EditorObject *obj, const char *propertyName, WleCriterionCond cond, const char *val, WleAEParamText *param)
{
	char *returnVal;
	bool condRet;

	wleAETextGetValue(obj, param, &returnVal);
	if (!returnVal)
		returnVal = StructAllocString("");

	if (!wleCriterionStringTest(returnVal, val, cond, &condRet))
		condRet = false;

	StructFreeString(returnVal);
	return condRet;
}

WleCriterion *wleAETextRegisterFilter(WleAEParamText *param, const char *propertyName)
{
	WleCriterion *criterion = StructCreate(parse_WleCriterion);
	criterion->propertyName = StructAllocString(propertyName);
	eaiPush(&criterion->allConds, WLE_CRIT_EQUAL);
	eaiPush(&criterion->allConds, WLE_CRIT_NOT_EQUAL);
	eaiPush(&criterion->allConds, WLE_CRIT_BEGINS_WITH);
	eaiPush(&criterion->allConds, WLE_CRIT_ENDS_WITH);
	eaiPush(&criterion->allConds, WLE_CRIT_CONTAINS);
	criterion->checkCallback = wleAETextCriterionCheckCallback;
	criterion->checkData = StructClone(parse_WleAEParamText, param);
	wleCriterionRegister(criterion);
	return criterion;
}

/********************
* COMBO BOX
********************/
// util
static void wleAEComboSetSelected(UIComboBox *combo, WleAEParamCombo *param)
{
	int i;
	const char *visible_value = NULL;

	if (!param->stringvalue || param->stringvalue[0] == 0)
	{
		ui_ComboBoxSetSelected(combo, -1);
		return;
	}

	if (param->visible_values)
	{
		for (i = 0; i < eaSize(&param->available_values); i++)
			if (stricmp(param->available_values[i], param->stringvalue) == 0)
			{
				visible_value = param->visible_values[i];
				break;
			}
	}
	else
	{
		visible_value = param->stringvalue;
	}
	for (i = 0; i < eaSize(combo->model); i++)
	{
		if (strcmpi(visible_value, (char*) (*combo->model)[i]) == 0)
		{
			ui_ComboBoxSetSelected(combo, i);
			break;
		}
	}
}

// callbacks
static void wleAEComboChanged(WleAEParamCombo *param)
{
	EditorObject **objects = NULL;
	int i;

	wleAEGetSelectedObjects(&objects);
	wleAESetApplyingData(true);

	EditUndoBeginGroup(edObjGetUndoStack());
	for (i = 0; i < eaSize(&objects); i++)
	{
		if (objects[i]->type->objType == EDTYPE_TRACKER && (param->property_name || (param->struct_offset && param->struct_pti && param->struct_fieldname)))
		{
			GroupTracker *tracker = wleOpPropsBegin(objects[i]->obj);
			GroupDef *def = tracker ? tracker->def : NULL;

			if (def && param->property_name)
			{
				if (param->is_specified && (param->can_unspecify || param->stringvalue))
					groupDefAddProperty(def, param->property_name, param->stringvalue ? param->stringvalue : "");
				else
					groupDefRemoveProperty(def, param->property_name);
			}
			else if (def && param->struct_offset && param->struct_pti && param->struct_fieldname)
			{
				void *property_struct = *(void**)((size_t) def + param->struct_offset);
				int col;

				if (ParserFindColumn(param->struct_pti, param->struct_fieldname, &col))
				{
					if (!property_struct)
						property_struct = *(void**)((size_t) def + param->struct_offset) = StructCreateVoid(param->struct_pti);
					if (!param->can_unspecify || param->is_specified)
						TokenStoreSetString(param->struct_pti, col, property_struct, 0, param->stringvalue, NULL, NULL, NULL, NULL);
					else
						initstruct_autogen(param->struct_pti, col, property_struct, 0);
				}				
			}
			if (tracker)
			{
				wleOpPropsEndNoUIUpdate();
			}
		}
	}
	if (param->apply_func)
	{
		param->apply_func(param, param->apply_data, objects);
	}
	EditUndoEndGroup(edObjGetUndoStack());
	wleAESetApplyingData(false);
	wleAERefresh();
	eaDestroy(&objects);
}

static void wleAEComboSelectedChanged(UIComboBox *combo, WleAEParamCombo *param)
{
	param->stringvalue = ui_ComboBoxGetSelectedObject(combo);
	if (param->visible_values)
	{
		int i;
		for (i = 0; i < eaSize(&param->visible_values); i++)
			if (!stricmp(param->visible_values[i], param->stringvalue))
			{
				param->stringvalue = param->available_values[i];
				break;
			}
	}
	param->is_specified = true;
	wleAEComboChanged(param);
}

static void wleAEComboSpecifiedChanged(UIRTNode *node, WleAEParamCombo *param)
{
	assert(param->can_unspecify);
	if (param->is_specified && !param->stringvalue && eaSize(&param->available_values) > 0)
		param->stringvalue = param->available_values[0];
	else if (!param->is_specified)
		param->stringvalue = NULL;
	wleAEComboChanged(param);
}

// main
static bool wleAEComboGetValue(EditorObject *obj, WleAEParamCombo *param, const char **output)
{
	if (obj->type->objType == EDTYPE_TRACKER && param->property_name && !param->update_func)
	{
		GroupTracker *tracker = trackerFromTrackerHandle(obj->obj);
		GroupDef *def = tracker ? tracker->def : NULL;
		const char *property_val = def ? groupDefFindProperty(def, param->property_name) : NULL;

		if (property_val)
		{
			*output = property_val;
			return true;
		}
		else
		{
			*output = NULL;
			return false;
		}
	}
	else if (obj->type->objType == EDTYPE_TRACKER && param->struct_offset && param->struct_pti && param->struct_fieldname)
	{
		GroupTracker *tracker = trackerFromTrackerHandle(obj->obj);
		GroupDef *def = tracker ? tracker->def : NULL;
		void *property_struct = def ? *(void**)((size_t) def + param->struct_offset) : NULL;
		int col;

		if (ParserFindColumn(param->struct_pti, param->struct_fieldname, &col) && property_struct)
		{
			*output = TokenStoreGetString(param->struct_pti, col, property_struct, 0, 0);
			return !!(*output);
		}
		else
		{
			*output = NULL;
			return false;
		}
	}
	else
	{
		param->update_func(param, param->update_data, obj);
		*output = param->stringvalue;
		return param->is_specified;
	}
}

void wleAEComboUpdate(WleAEParamCombo *param)
{
	EditorObject **objects = NULL;
	EditorObject *all_source = NULL;
	const char *all_string = NULL, *next_string;
	bool all_spec = false, next_spec;
	int i;

	param->diff = param->spec_diff = false;
	if (param->source)
	{
		editorObjectDeref(param->source);
		param->source = NULL;
	}
	wleAEGetSelectedObjects(&objects);
	for (i = 0; i < eaSize(&objects); i++)
	{
		EditorObject *obj = objects[i];
		param->source = NULL;

		next_spec = wleAEComboGetValue(obj, param, &next_string);
		if (param->source)
			editorObjectRef(param->source);

		if (!i)
		{
			all_string = next_string;
			all_spec = next_spec;
			all_source = param->source;
		}
		else
		{
			// compare sources
			if (all_source && (!param->source || edObjCompare(param->source, all_source) != 0))
			{
				if (all_source)
					editorObjectDeref(all_source);
				all_source = NULL;
			}
			if (param->source)
				editorObjectDeref(param->source);

			if (!param->diff)
			{
				if ((!all_string && next_string) || (all_string && !next_string))
					param->diff = true;
				else if (all_string && next_string && strcmpi(all_string, next_string) != 0)
					param->diff = true;	
			}
			if (!param->spec_diff && all_spec != next_spec)
				param->spec_diff = true;
			if (param->spec_diff && param->diff)
				break;
		}
	}

	param->source = all_source;
	param->is_specified = all_spec;
	if (param->spec_diff && param->can_unspecify)
	{
		param->stringvalue = NULL;
		param->is_specified = false;
		param->diff = true;
	}
	else if (param->diff)
		param->stringvalue = NULL;
	else if (!all_string)
		param->stringvalue = NULL;
	else
	{
		for (i = 0; i < eaSize(&param->available_values); i++)
		{
			if (all_string && strcmpi(param->available_values[i], all_string) == 0)
			{
				param->stringvalue = param->available_values[i];
				break;
			}
		}
	}

	eaDestroy(&objects);
}

static void wleAEComboPasteDataFree(WleAEParamCombo *param_copy)
{
	StructDestroy(parse_WleAEParamCombo, param_copy);
}

static void wleAEComboPaste(const EditorObject **selection, WleAEParamCombo *param_copy)
{
	wleAEComboChanged(param_copy);
}

static WleAEPasteData *wleAEComboCopy(const EditorObject *object, WleAEParamCombo *orig_param)
{
	WleAEParamCombo *param_copy = StructClone(parse_WleAEParamCombo, orig_param);
	WleAEPasteData *data = wleAEPasteDataCreate(param_copy, wleAEComboPaste, wleAEComboPasteDataFree);
	return data;
}

void wleAEComboAddWidgetEx(UIRebuildableTreeNode *auto_widget_node, const char *name, const char *tooltip, const char *param_name, WleAEParamCombo *param)
{
	UIComboBox *combo;
	UIAutoWidgetParams params = {0};
	char full_param_name[1024];
	bool did_newline = false;

	params.NoLabel = true;
	params.alignTo = param->left_pad;
	params.disabled = param->disabled;

	if (param->can_copy)
	{
		UIButton *button = wleAECopyButtonCreate(param->copy_func ? param->copy_func : wleAEComboCopy, param->copy_func ? param->copy_data : param, NULL);

		sprintf(full_param_name, "%s|%d|copy", param_name, param->index);
		ui_WidgetSetTooltipString(UI_WIDGET(button), "Select for copying");
		ui_RebuildableTreeAddWidget(auto_widget_node, UI_WIDGET(button), NULL, true, full_param_name, &params);
		did_newline = true;
		params.alignTo += 20;
	}

	if (param->can_unspecify)
	{
		sprintf(full_param_name, "%s|%d|is_specified", param_name, param->index);
		ui_AutoWidgetAddKeyed(auto_widget_node, parse_WleAEParamCombo, "is_specified", full_param_name, param, !did_newline, wleAEComboSpecifiedChanged, param, &params, tooltip);
		did_newline = true;
		params.alignTo += 20;

		// skin specified check box
		if (auto_widget_node->children)
		{
			UIWidget *check_button = auto_widget_node->children[eaSize(&auto_widget_node->children) - 1]->widget1;
			wleSkinDiffWidget(check_button, param->spec_diff);
		}
	}

	if (param->source)
	{
		UIButton *button = ui_ButtonCreateImageOnly("button_center", 0, 0, wleAESelectSourceClicked, param->source);

		sprintf(full_param_name, "%s|%d|source", param_name, param->index);
		ui_WidgetSetDimensions(UI_WIDGET(button), 20, 20);
		ui_ButtonSetImageStretch(button, true);
		if (param->source->name)
			ui_WidgetSetTooltipString(UI_WIDGET(button), param->source->name);
		ui_RebuildableTreeAddWidget(auto_widget_node, UI_WIDGET(button), NULL, !did_newline, full_param_name, &params);
		params.alignTo += 30;
	}

	sprintf(full_param_name, "%s|%d|label", param_name, param->index);
	ui_RebuildableTreeAddLabelKeyed(auto_widget_node, name, full_param_name, &params, !did_newline);
	if (auto_widget_node->children)
	{
		UIWidget *label = auto_widget_node->children[eaSize(&auto_widget_node->children) - 1]->widget1;
		wleSkinDiffOrSourcedWidget(label, false, param->source);
	}

	params.alignTo += ((param->entry_align ? param->entry_align : 120) + (!param->can_unspecify ? 20 : 0));
	sprintf(full_param_name, "%s|%d|boolvalue", param_name, param->index);
	if (!param->is_filtered)
		combo = ui_ComboBoxCreate(0, 0, param->entry_width > 0 ? param->entry_width : 300, NULL, param->visible_values ? &param->visible_values : &param->available_values, NULL);
	else
		combo = (UIComboBox*) ui_FilteredComboBoxCreate(0, 0, param->entry_width > 0 ? param->entry_width : 300, NULL, param->visible_values ? &param->visible_values : &param->available_values, NULL);
	if ((param->entry_width > 0.0) && (param->entry_width <= 1.0))
		ui_WidgetSetWidthEx(UI_WIDGET(combo), param->entry_width, UIUnitPercentage);
	ui_WidgetSetTooltipString(UI_WIDGET(combo), tooltip);
	wleAEComboSetSelected(combo, param);
	ui_ComboBoxSetSelectedCallback(combo, wleAEComboSelectedChanged, param);
	ui_RebuildableTreeAddWidget(auto_widget_node, UI_WIDGET(combo), NULL, false, full_param_name, &params);

	// skin widget as necessary
	wleSkinDiffWidget(UI_WIDGET(combo), param->diff);
}

static bool wleAEComboCriterionCheckCallback(EditorObject *obj, const char *propertyName, WleCriterionCond cond, const char *val, WleAEParamCombo *param)
{
	char *returnVal;
	bool condRet;

	wleAEComboGetValue(obj, param, &returnVal);
	if (!returnVal)
		returnVal = "";

	condRet = (strcmpi(returnVal, val) == 0);
	if (cond == WLE_CRIT_NOT_EQUAL)
		condRet = !condRet;

	return condRet;
}

WleCriterion *wleAEComboRegisterFilter(WleAEParamCombo *param, const char *propertyName)
{
	WleCriterion *criterion = StructCreate(parse_WleCriterion);
	criterion->propertyName = StructAllocString(propertyName);
	eaiPush(&criterion->allConds, WLE_CRIT_EQUAL);
	eaiPush(&criterion->allConds, WLE_CRIT_NOT_EQUAL);
	eaCopy(&criterion->possibleValues, &param->available_values);
	criterion->checkCallback = wleAEComboCriterionCheckCallback;
	criterion->checkData = StructClone(parse_WleAEParamCombo, param);
	wleCriterionRegister(criterion);
	return criterion;
}

/********************
* DICTIONARY
********************/
// callbacks
static const char *wleAEDictionaryGetDictionaryNameForEditor(WleAEParamDictionary *param)
{
	const char *pDictName = param->dictionary;
	ResourceDictionaryInfo *pDictInfo = resDictGetInfo(pDictName);
	if(pDictInfo)
	{
		pDictName = pDictInfo->pDictName;
		if(emGetEditorForType(pDictName))
			return pDictName;
	}

	return NULL;
}

static bool wleAEDictionaryCanOpenInEditor(WleAEParamDictionary *param)
{
	return param->refvalue && param->refvalue[0] != '\0' && wleAEDictionaryGetDictionaryNameForEditor(param);
}

static void wleAEDictionaryAttemptOpenInEditor(UIButton *unused, WleAEParamDictionary *param)
{
	const char *pDictName = wleAEDictionaryGetDictionaryNameForEditor(param);
	if(param->refvalue && param->refvalue[0] != '\0' && pDictName)
		emOpenFileEx(param->refvalue, pDictName);
}

static void wleAEDictionaryChanged(WleAEParamDictionary *param)
{
	EditorObject **objects = NULL;
	int i;

	wleAEGetSelectedObjects(&objects);
	wleAESetApplyingData(true);

	EditUndoBeginGroup(edObjGetUndoStack());
	for (i = 0; i < eaSize(&objects); i++)
	{
		if (objects[i]->type->objType == EDTYPE_TRACKER && (param->property_name || (param->struct_offset && param->struct_pti && param->struct_fieldname)))
		{
			GroupTracker *tracker = wleOpPropsBegin(objects[i]->obj);
			GroupDef *def = tracker ? tracker->def : NULL;

			if (def && param->property_name)
			{
				if (param->is_specified && (param->can_unspecify || param->refvalue))
					groupDefAddProperty(def, param->property_name, param->refvalue ? param->refvalue : "");
				else
					groupDefRemoveProperty(def, param->property_name);
			}
			else if (def && param->struct_offset && param->struct_pti && param->struct_fieldname)
			{
				void *property_struct = *(void**)((size_t) def + param->struct_offset);
				int col;

				if (ParserFindColumn(param->struct_pti, param->struct_fieldname, &col))
				{
					if (!property_struct)
						property_struct = *(void**)((size_t) def + param->struct_offset) = StructCreateVoid(param->struct_pti);
					if (!param->can_unspecify || param->is_specified)
						TokenStoreSetRef(param->struct_pti, col, property_struct, 0, param->refvalue, NULL, NULL);
					else
						initstruct_autogen(param->struct_pti, col, property_struct, 0);
				}				
			}
			if (tracker)
			{
				wleOpPropsEndNoUIUpdate();
			}
		}
	}
	if (param->apply_func)
	{
		param->apply_func(param, param->apply_data, objects);
	}
	EditUndoEndGroup(edObjGetUndoStack());
	wleAESetApplyingData(false);
	wleAERefresh();
	eaDestroy(&objects);
}

static void wleAEDictionarySelectedChanged(UIRTNode *node, WleAEParamDictionary *param)
{
	if (param->refvalue && param->refvalue[0])
		param->is_specified = true;
	else
		param->is_specified = false;
	wleAEDictionaryChanged(param);
}

static void wleAEDictionarySpecifiedChanged(UIRTNode *node, WleAEParamDictionary *param)
{
	assert(param->can_unspecify && param->dictionary);
	if (param->is_specified && !param->refvalue)
	{
		DictionaryEArrayStruct *dict_earray = resDictGetEArrayStruct(param->dictionary);
		ParseTable *pti = RefSystem_GetDictionaryParseTable(param->dictionary);
		void *selected = dict_earray->ppReferents[0];
		int i;
		char *field_name = param->parse_name_field ? param->parse_name_field : "Name";

		FORALL_PARSETABLE(pti, i)
		{
			if (pti[i].name && strcmpi(pti[i].name, field_name) == 0)
			{
				char *estr = NULL;
				TokenWriteText(pti, i, selected, &estr, true);
				param->refvalue = StructAllocString(estr);
				estrDestroy(&estr);
				break;
			}
		}
	}
	else if (!param->is_specified)
	{
		if (param->refvalue)
			StructFreeString(param->refvalue);
		param->refvalue = NULL;
	}
	wleAEDictionaryChanged(param);
}

// main
void wleAEDictionaryUpdate(WleAEParamDictionary *param)
{
	EditorObject **objects = NULL;
	char *all_ref = NULL, *next_ref;
	bool all_spec = false, next_spec;
	int i;

	param->diff = param->spec_diff = false;
	if (param->refvalue)
	{
		StructFreeString(param->refvalue);
		param->refvalue = NULL;
	}
	wleAEGetSelectedObjects(&objects);
	for (i = 0; i < eaSize(&objects); i++)
	{
		EditorObject *obj = objects[i];
		if (obj->type->objType == EDTYPE_TRACKER && param->property_name && !param->update_func)
		{
			GroupTracker *tracker = trackerFromTrackerHandle(obj->obj);
			GroupDef *def = tracker ? tracker->def : NULL;
			const char *property_val = def ? groupDefFindProperty(def, param->property_name) : NULL;

			if (property_val)
			{
				next_ref = StructAllocString(property_val);
				next_spec = true;
			}
			else
			{
				next_ref = NULL;
				next_spec = false;
			}
		}
		else if (obj->type->objType == EDTYPE_TRACKER && param->struct_offset && param->struct_pti && param->struct_fieldname)
		{
			GroupTracker *tracker = trackerFromTrackerHandle(obj->obj);
			GroupDef *def = tracker ? tracker->def : NULL;
			void *property_struct = def ? *(void**)((size_t) def + param->struct_offset) : NULL;
			int col;

			if (ParserFindColumn(param->struct_pti, param->struct_fieldname, &col) && property_struct)
			{
				next_ref = StructAllocString(TokenStoreGetRefString(param->struct_pti, col, property_struct, 0, 0));
				next_spec = !!next_ref;
			}
			else
			{
				next_ref = NULL;
				next_spec = false;
			}
		}
		else
		{
			param->update_func(param, param->update_data, obj);
			next_ref = param->refvalue;
			next_spec = param->is_specified;
		}

		if (!i)
		{
			all_ref = StructAllocString(next_ref);
			all_spec = next_spec;
		}
		else
		{
			if (!param->diff)
			{
				if ((!all_ref && next_ref) || (all_ref && !next_ref))
					param->diff = true;
				else if (all_ref && next_ref && strcmpi(all_ref, next_ref) != 0)
					param->diff = true;	
			}
			if (!param->spec_diff && all_spec != next_spec)
				param->spec_diff = true;
		}

		if (next_ref)
			StructFreeString(next_ref);
		if (param->spec_diff && param->diff)
			break;
	}

	param->is_specified = all_spec;
	if (param->spec_diff && param->can_unspecify)
	{
		param->refvalue = NULL;
		param->is_specified = false;
		param->diff = true;
	}
	else if (param->diff)
		param->refvalue = NULL;
	else
		param->refvalue = StructAllocString(all_ref);

	if (all_ref)
		StructFreeString(all_ref);
	eaDestroy(&objects);
}

void wleAEDictionaryAddWidgetEx(UIRebuildableTreeNode *auto_widget_node, const char *name, const char *tooltip, const char *param_name, WleAEParamDictionary *param, bool bNewLine)
{
	UIAutoWidgetParams params = {0};
	char full_param_name[1024];
	bool did_newline = !bNewLine;

	assert(param->dictionary);
	params.NoLabel = true;
	params.alignTo = param->left_pad;
	params.disabled = param->disabled;

	if (param->can_unspecify)
	{
		sprintf(full_param_name, "%s|%d|is_specified", param_name, param->index);
		ui_AutoWidgetAddKeyed(auto_widget_node, parse_WleAEParamDictionary, "is_specified", full_param_name, param, !did_newline, wleAEDictionarySpecifiedChanged, param, &params, tooltip);
		did_newline = true;
		params.alignTo += 20;

		// skin specified check box
		if (auto_widget_node->children)
		{
			UIWidget *check_button = auto_widget_node->children[eaSize(&auto_widget_node->children) - 1]->widget1;
			wleSkinDiffWidget(check_button, param->spec_diff);
		}
	}

/*	if (param->source && param->source_handle)
	{
		UIButton *button = ui_ButtonCreateImageOnly("button_center", 0, 0, linkToTrackerHandle, param->source_handle);

		sprintf(full_param_name, "%s|%d|source", param_name, param->index);
		params.type = AWT_Default;
		ui_WidgetSetDimensions(UI_WIDGET(button), 20, 20);
		ui_WidgetSetDimensions(UI_WIDGET(button->sprite), 20, 20);
		ui_WidgetSetTooltipString(UI_WIDGET(button), param->source);
		ui_RebuildableTreeAddWidget(auto_widget_node, UI_WIDGET(button), NULL, false, full_param_name, &params);
		params.alignTo += 30;
	}*/

	sprintf(full_param_name, "%s|%d|label", param_name, param->index);
	ui_RebuildableTreeAddLabelKeyed(auto_widget_node, name, full_param_name, &params, !did_newline);

	params.alignTo += ((param->entry_align ? param->entry_align : 120) + (!param->can_unspecify ? 20 : 0));
	params.overrideWidth = param->entry_width ? param->entry_width : 200;
	params.type = AWT_DictionaryTextEntry;
	params.dictionary = param->dictionary;
	params.parseNameField = param->parse_name_field;
	params.filterable = true;
	params.editable = !!wleAEDictionaryGetDictionaryNameForEditor(param);
	sprintf(full_param_name, "%s|%d|refvalue", param_name, param->index);
	ui_AutoWidgetAddKeyed(auto_widget_node, parse_WleAEParamDictionary, "refvalue", full_param_name, param, false, wleAEDictionarySelectedChanged, param, &params, tooltip);

	if (auto_widget_node->children)
	{
		UIWidget *dict_entry = auto_widget_node->children[eaSize(&auto_widget_node->children) - 1]->widget1;
		wleSkinDiffWidget(dict_entry, param->diff);

		if(wleAEDictionaryCanOpenInEditor(param))
			ui_DictionaryEntrySetOpenCallback((UIDictionaryEntry *)dict_entry, wleAEDictionaryAttemptOpenInEditor, param);
		else
			ui_DictionaryEntrySetOpenCallback((UIDictionaryEntry *)dict_entry, NULL, param);
	}
}

/********************
* VEC3
********************/
// callbacks
static void wleAEVec3Changed(WleAEParamVec3 *param)
{
	EditorObject **objects = NULL;
	int i;

	wleAEGetSelectedObjects(&objects);
	wleAESetApplyingData(true);

	EditUndoBeginGroup(edObjGetUndoStack());
	for (i = 0; i < eaSize(&objects); i++)
	{
		if (objects[i]->type->objType == EDTYPE_TRACKER && (param->property_name || (param->struct_offset && param->struct_pti && param->struct_fieldname)))
		{
			GroupTracker *tracker = wleOpPropsBegin(objects[i]->obj);
			GroupDef *def = tracker ? tracker->def : NULL;

			if (def && param->property_name)
			{
				if (param->is_specified && (param->can_unspecify || param->vecvalue[0] != 0 || param->vecvalue[1] != 0 || param->vecvalue[2] != 0))
					groupDefAddPropertyVec3(def, param->property_name, param->vecvalue);
				else
					groupDefRemoveProperty(def, param->property_name);
			}
			else if (def && param->struct_offset && param->struct_pti && param->struct_fieldname)
			{
				void *property_struct = *(void**)((size_t) def + param->struct_offset);
				int col;

				if (ParserFindColumn(param->struct_pti, param->struct_fieldname, &col))
				{
					if (!property_struct)
						property_struct = *(void**)((size_t) def + param->struct_offset) = StructCreateVoid(param->struct_pti);
					if (!param->can_unspecify || param->is_specified)
					{
						TokenStoreSetF32(param->struct_pti, col, property_struct, 0, param->vecvalue[0], NULL, NULL);
						TokenStoreSetF32(param->struct_pti, col, property_struct, 1, param->vecvalue[1], NULL, NULL);
						TokenStoreSetF32(param->struct_pti, col, property_struct, 2, param->vecvalue[2], NULL, NULL);
					}
					else
						initstruct_autogen(param->struct_pti, col, property_struct, 0);
				}				
			}
			if (tracker)
			{
				wleOpPropsEndNoUIUpdate();
			}		}
	}
	if (param->apply_func)
	{
		param->apply_func(param, param->apply_data, objects);
	}
	EditUndoEndGroup(edObjGetUndoStack());
	wleAESetApplyingData(false);
	wleAERefresh();
	eaDestroy(&objects);
}

static void wleAEVec3ValueChanged(UIRTNode *node, WleAEParamVec3 *param)
{
	size_t nameLen = strlen(node->name);

	param->is_specified = true;

	// reset diff on the changed value
	if (node->name[nameLen - 1] == '1')
		param->diff[1] = false;
	else if (node->name[nameLen - 1] == '2')
		param->diff[2] = false;
	else
		param->diff[0] = false;
	wleAEVec3Changed(param);
}

static void wleAEVec3SpecifiedChanged(UIRTNode *node, WleAEParamVec3 *param)
{
	assert(param->can_unspecify);
	if (!param->is_specified)
		copyVec3(zerovec3, param->vecvalue);
	wleAEVec3Changed(param);
}

// main
void wleAEVec3Update(WleAEParamVec3 *param)
{
	EditorObject **objects = NULL;
	EditorObject *all_source = NULL;
	Vec3 all_vec, next_vec;
	bool all_spec = false, next_spec;
	int i, j;
	F32 tol = param->precision ? pow(10, -((int) param->precision)) : 0.001f;

	param->spec_diff = false;
	for (i = 0; i < 3; i++)
		param->diff[i] = false;

	wleAEGetSelectedObjects(&objects);
	copyVec3(zerovec3, all_vec);
	if (param->source)
	{
		editorObjectDeref(param->source);
		param->source = NULL;
	}
	for (i = 0; i < eaSize(&objects); i++)
	{
		EditorObject *obj = objects[i];
		param->source = NULL;
		if (obj->type->objType == EDTYPE_TRACKER && param->property_name && !param->update_func)
		{
			GroupTracker *tracker = trackerFromTrackerHandle(obj->obj);
			GroupDef *def = tracker ? tracker->def : NULL;
			const char *property_val = def ? groupDefFindProperty(def, param->property_name) : NULL;

			if (property_val)
			{
				fillVec3sFromStr(property_val, &next_vec, 1);
				next_spec = true;
			}
			else
			{
				copyVec3(zerovec3, next_vec);
				next_spec = false;
			}
		}
		else if (obj->type->objType == EDTYPE_TRACKER && param->struct_offset && param->struct_pti && param->struct_fieldname)
		{
			GroupTracker *tracker = trackerFromTrackerHandle(obj->obj);
			GroupDef *def = tracker ? tracker->def : NULL;
			void *property_struct = def ? *(void**)((size_t) def + param->struct_offset) : NULL;
			int col;

			if (ParserFindColumn(param->struct_pti, param->struct_fieldname, &col) && property_struct)
			{
				next_vec[0] = TokenStoreGetF32(param->struct_pti, col, property_struct, 0, 0);
				next_vec[1] = TokenStoreGetF32(param->struct_pti, col, property_struct, 1, 0);
				next_vec[2] = TokenStoreGetF32(param->struct_pti, col, property_struct, 2, 0);
				next_spec = (!!next_vec[0] || !!next_vec[1] || !!next_vec[2]);
			}
			else
			{
				copyVec3(zerovec3, next_vec);
				next_spec = false;
			}
		}
		else
		{
			param->update_func(param, param->update_data, obj);
			copyVec3(param->vecvalue, next_vec);
			next_spec = param->is_specified;
			if (param->source)
				editorObjectRef(param->source);
		}

		if (!i)
		{
			copyVec3(next_vec, all_vec);
			all_spec = next_spec;
			all_source = param->source;
		}
		else
		{
			// compare sources
			if (all_source && (!param->source || edObjCompare(param->source, all_source) != 0))
			{
				if (all_source)
					editorObjectDeref(all_source);
				all_source = NULL;
			}
			if (param->source)
				editorObjectDeref(param->source);

			for (j = 0; j < 3; j++)
			{
				if (param->diff[j])
					continue;

				if (fabs(next_vec[j] - all_vec[j]) > tol)
					param->diff[j] = true;
			}

			if (all_spec != next_spec)
				param->spec_diff = true;
		}

		if (param->diff[0] && param->diff[1] && param->diff[2] && param->spec_diff)
			break;
	}

	param->source = all_source;
	param->is_specified = all_spec;
	if (param->spec_diff && param->can_unspecify)
	{
		param->is_specified = false;
		copyVec3(zerovec3, param->vecvalue);
		param->diff[0] = param->diff[1] = param->diff[2] = true;
	}
	else
	{
		copyVec3(all_vec, param->vecvalue);
		for (i = 0; i < 3; i++)
		{
			if (param->diff[i])
				param->vecvalue[i] = 0.0f;
		}
	}

	eaDestroy(&objects);
}

static void wleAEVec3PasteDataFree(WleAEParamVec3 *param_copy)
{
	StructDestroy(parse_WleAEParamVec3, param_copy);
}

static void wleAEVec3Paste(const EditorObject **objects, WleAEParamVec3 *param_copy)
{
	wleAEVec3Changed(param_copy);
}

static WleAEPasteData *wleAEVec3Copy(const EditorObject *object, WleAEParamVec3 *orig_param)
{
	WleAEParamVec3 *param_copy = StructClone(parse_WleAEParamVec3, orig_param);
	WleAEPasteData *data = wleAEPasteDataCreate(param_copy, wleAEVec3Paste, wleAEVec3PasteDataFree);
	return data;
}

void wleAEVec3AddWidgetEx(UIRebuildableTreeNode *auto_widget_node, const char *name, const char *tooltip, const char *param_name, WleAEParamVec3 *param, const Vec3 min, const Vec3 max, const Vec3 step)
{
	UIAutoWidgetParams params = {0};
	char full_param_name[1024];
	bool did_newline = false;
	int i, j;

	params.NoLabel = true;
	params.alignTo = param->left_pad;
	params.disabled = param->disabled;

	if (param->can_copy)
	{
		UIButton *button = wleAECopyButtonCreate(param->copy_func ? param->copy_func : wleAEVec3Copy, param->copy_func ? param->copy_data : param, NULL);

		sprintf(full_param_name, "%s|%d|copy", param_name, param->index);
		ui_WidgetSetTooltipString(UI_WIDGET(button), "Select for copying");
		ui_RebuildableTreeAddWidget(auto_widget_node, UI_WIDGET(button), NULL, true, full_param_name, &params);
		did_newline = true;
		params.alignTo += 20;
	}

	if (param->can_unspecify)
	{
		sprintf(full_param_name, "%s|%d|is_specified", param_name, param->index);
		ui_AutoWidgetAddKeyed(auto_widget_node, parse_WleAEParamVec3, "is_specified", full_param_name, param, !did_newline, wleAEVec3SpecifiedChanged, param, &params, tooltip);
		did_newline = true;
		params.alignTo += 20;

		// skin specified check box
		if (auto_widget_node->children)
		{
			UIWidget *check_button = auto_widget_node->children[eaSize(&auto_widget_node->children) - 1]->widget1;
			wleSkinDiffWidget(check_button, param->spec_diff);
		}
	}

	if (param->source)
	{
		UIButton *button = ui_ButtonCreateImageOnly("button_center", 0, 0, wleAESelectSourceClicked, param->source);

		sprintf(full_param_name, "%s|%d|source", param_name, param->index);
		ui_WidgetSetDimensions(UI_WIDGET(button), 20, 20);
		ui_ButtonSetImageStretch(button, true);
		ui_WidgetSetTooltipString(UI_WIDGET(button), param->source->name);
		ui_RebuildableTreeAddWidget(auto_widget_node, UI_WIDGET(button), NULL, !did_newline, full_param_name, &params);
		params.alignTo += 30;
	}

	sprintf(full_param_name, "%s|%d|label", param_name, param->index);
	ui_RebuildableTreeAddLabelKeyed(auto_widget_node, name, full_param_name, &params, !did_newline);
	if (auto_widget_node->children)
	{
		UIWidget *label = auto_widget_node->children[eaSize(&auto_widget_node->children) - 1]->widget1;
		wleSkinDiffOrSourcedWidget(label, false, param->source);
	}

	copyVec3(min, params.min);
	copyVec3(max, params.max);
	copyVec3(step, params.step);
	params.alignTo += ((param->entry_align ? param->entry_align : 120) + (!param->can_unspecify ? 20 : 0));
	params.type = AWT_Spinner;
	params.spinnerStartF = wleAESpinnerSpinStart;
	params.spinnerStopF = wleAESpinnerSpinStop;
	sprintf(full_param_name, "%s|%d|vecvalue", param_name, param->index);
	params.precision = param->precision;
	params.overrideWidth = param->entry_width;
	ui_AutoWidgetAddKeyed(auto_widget_node, parse_WleAEParamVec3, "vecvalue", full_param_name, param, false, wleAEVec3ValueChanged, param, &params, tooltip);

	// get last three nodes and override entry widget widths
	if (auto_widget_node->children)
	{
		for (i = eaSize(&auto_widget_node->children) - 3, j = 0; i < eaSize(&auto_widget_node->children); i++, j++)
		{
			wleSkinDiffWidget(auto_widget_node->children[i]->widget1, param->diff[j]);
			if (param->diff[j])
			{
				// TODO: use something out of the range of values likely to be used on Vec3s to represent a non-value
				
				ui_TextEntrySetText((UITextEntry*) auto_widget_node->children[i]->widget1, "");
			}
		}
	}
}

/********************
* HSV
********************/
// callbacks
static void wleAEHSVChanged(WleAEParamHSV *param)
{
	EditorObject **objects = NULL;
	int i;

	wleAEGetSelectedObjects(&objects);
	wleAESetApplyingData(true);

	EditUndoBeginGroup(edObjGetUndoStack());
	for (i = 0; i < eaSize(&objects); i++)
	{
		if (objects[i]->type->objType == EDTYPE_TRACKER && (param->property_name || (param->struct_offset && param->struct_pti && param->struct_fieldname)))
		{
			GroupTracker *tracker = wleOpPropsBegin(objects[i]->obj);
			GroupDef *def = tracker ? tracker->def : NULL;

			if (def && param->property_name)
			{
				if (param->is_specified)
					groupDefAddPropertyVec3(def, param->property_name, param->hsvvalue);
				else
					groupDefRemoveProperty(def, param->property_name);
			}
			else if (def && param->struct_offset && param->struct_pti && param->struct_fieldname)
			{
				void *property_struct = *(void**)((size_t) def + param->struct_offset);
				int col;

				if (ParserFindColumn(param->struct_pti, param->struct_fieldname, &col))
				{
					if (!property_struct)
						property_struct = *(void**)((size_t) def + param->struct_offset) = StructCreateVoid(param->struct_pti);
					if (!param->can_unspecify || param->is_specified)
					{
						TokenStoreSetF32(param->struct_pti, col, property_struct, 0, param->hsvvalue[0], NULL, NULL);
						TokenStoreSetF32(param->struct_pti, col, property_struct, 1, param->hsvvalue[1], NULL, NULL);
						TokenStoreSetF32(param->struct_pti, col, property_struct, 2, param->hsvvalue[2], NULL, NULL);
					}
					else
						initstruct_autogen(param->struct_pti, col, property_struct, 0);
				}				
			}
			if (tracker)
			{
				wleOpPropsEndNoUIUpdate();
			}
		}
	}
	if (param->apply_func)
	{
		param->apply_func(param, param->apply_data, objects);
	}
	EditUndoEndGroup(edObjGetUndoStack());
	wleAESetApplyingData(false);
	wleAERefresh();
	eaDestroy(&objects);
}

static void wleAEHSVColorChanged(UIRTNode *node, WleAEParamHSV *param)
{
	// this ensures that the color window, when closed because the selection changes, will not revert the color
	if (node->root->old_scrollArea || node->root->old_root)
		return;

	param->is_specified = true;
	wleAEHSVChanged(param);
}

static void wleAEHSVSpecifiedChanged(UIRTNode *node, WleAEParamHSV *param)
{
	assert(param->can_unspecify);
	if (!param->is_specified)
		copyVec3(zerovec3, param->hsvvalue);
	wleAEHSVChanged(param);
}

// main
void wleAEHSVUpdate(WleAEParamHSV *param)
{
	EditorObject **objects = NULL;
	EditorObject *all_source = NULL;
	Vec3 all_hsv, next_hsv;
	bool all_spec = false, next_spec;
	int i, j;
	F32 tol = 0.0001f;

	param->diff = param->spec_diff = false;
	wleAEGetSelectedObjects(&objects);
	copyVec3(zerovec3, all_hsv);
	if (param->source)
	{
		editorObjectDeref(param->source);
		param->source = NULL;
	}
	for (i = 0; i < eaSize(&objects); i++)
	{
		EditorObject *obj = objects[i];
		param->source = NULL;
		if (obj->type->objType == EDTYPE_TRACKER && param->property_name && !param->update_func)
		{
			GroupTracker *tracker = trackerFromTrackerHandle(obj->obj);
			GroupDef *def = tracker ? tracker->def : NULL;
			const char *property_val = def ? groupDefFindProperty(def, param->property_name) : NULL;

			if (property_val)
			{
				fillVec3sFromStr(property_val, &next_hsv, 1);
				next_spec = true;
			}
			else
			{
				copyVec3(zerovec3, next_hsv);
				next_spec = false;
			}
		}
		else if (obj->type->objType == EDTYPE_TRACKER && param->struct_offset && param->struct_pti && param->struct_fieldname)
		{
			GroupTracker *tracker = trackerFromTrackerHandle(obj->obj);
			GroupDef *def = tracker ? tracker->def : NULL;
			void *property_struct = def ? *(void**)((size_t) def + param->struct_offset) : NULL;
			int col;

			if (ParserFindColumn(param->struct_pti, param->struct_fieldname, &col) && property_struct)
			{
				next_hsv[0] = TokenStoreGetF32(param->struct_pti, col, property_struct, 0, 0);
				next_hsv[1] = TokenStoreGetF32(param->struct_pti, col, property_struct, 1, 0);
				next_hsv[2] = TokenStoreGetF32(param->struct_pti, col, property_struct, 2, 0);
				next_spec = (!!next_hsv[0] || !!next_hsv[1] || !!next_hsv[2]);
			}
			else
			{
				copyVec3(zerovec3, next_hsv);
				next_spec = false;
			}
		}
		else
		{
			param->update_func(param, param->update_data, obj);
			copyVec3(param->hsvvalue, next_hsv);
			next_spec = param->is_specified;
			if (param->source)
				editorObjectRef(param->source);
		}

		if (!i)
		{
			copyVec3(next_hsv, all_hsv);
			all_spec = next_spec;
			all_source = param->source;
		}
		else
		{
			// compare sources
			if (all_source && (!param->source || edObjCompare(param->source, all_source) != 0))
			{
				if (all_source)
					editorObjectDeref(all_source);
				all_source = NULL;
			}
			if (param->source)
				editorObjectDeref(param->source);

			for (j = 0; j < 3; j++)
			{
				if (fabs(next_hsv[j] - all_hsv[j]) > tol)
				{
					param->diff = true;
					break;
				}
			}

			if (all_spec != next_spec)
				param->spec_diff = true;
		}

		if (param->diff && param->spec_diff)
			break;
	}

	param->source = all_source;
	param->is_specified = all_spec;
	if (param->spec_diff && param->can_unspecify)
	{
		param->is_specified = false;
		copyVec3(zerovec3, param->hsvvalue);
		param->diff = true;
	}
	else if (param->diff)
		copyVec3(zerovec3, param->hsvvalue);
	else
		copyVec3(all_hsv, param->hsvvalue);

	eaDestroy(&objects);
}

void wleAEHSVAddWidgetEx(UIRebuildableTreeNode *auto_widget_node, const char *name, const char *tooltip, const char *param_name, WleAEParamHSV *param)
{
	UIAutoWidgetParams params = {0};
	char full_param_name[1024];
	bool did_newline = false;

	params.NoLabel = true;
	params.alignTo = param->left_pad;
	params.disabled = param->disabled;
	params.hsvAddAlpha = param->add_alpha;

	if (param->can_unspecify)
	{
		sprintf(full_param_name, "%s|%d|is_specified", param_name, param->index);
		ui_AutoWidgetAddKeyed(auto_widget_node, parse_WleAEParamHSV, "is_specified", full_param_name, param, true, wleAEHSVSpecifiedChanged, param, &params, tooltip);
		did_newline = true;
		params.alignTo += 20;

		// skin specified check box
		if (auto_widget_node->children)
		{
			UIWidget *check_button = auto_widget_node->children[eaSize(&auto_widget_node->children) - 1]->widget1;
			wleSkinDiffWidget(check_button, param->spec_diff);
		}
	}

	if (param->source)
	{
		UIButton *button = ui_ButtonCreateImageOnly("button_center", 0, 0, wleAESelectSourceClicked, param->source);

		sprintf(full_param_name, "%s|%d|source", param_name, param->index);
		ui_WidgetSetDimensions(UI_WIDGET(button), 20, 20);
		ui_ButtonSetImageStretch(button, true);
		ui_WidgetSetTooltipString(UI_WIDGET(button), param->source->name);
		ui_RebuildableTreeAddWidget(auto_widget_node, UI_WIDGET(button), NULL, false, full_param_name, &params);
		params.alignTo += 30;
	}

	sprintf(full_param_name, "%s|%d|label", param_name, param->index);
	ui_RebuildableTreeAddLabelKeyed(auto_widget_node, name, full_param_name, &params, !did_newline);

	// skin label if parameter is different
	if (auto_widget_node->children)
	{
		UIWidget *label = auto_widget_node->children[eaSize(&auto_widget_node->children) - 1]->widget1;
		wleSkinDiffOrSourcedWidget(label, param->diff, param->source);
	}

	params.alignTo += ((param->entry_align ? param->entry_align : 120) + (!param->can_unspecify ? 20 : 0));
	sprintf(full_param_name, "%s|%d|hsvvalue", param_name, param->index);
	params.overrideWidth = param->entry_width;
	ui_AutoWidgetAddKeyed(auto_widget_node, parse_WleAEParamHSV, "hsvvalue", full_param_name, param, false, wleAEHSVColorChanged, param, &params, tooltip);
}

/********************
* HUE
********************/
// callbacks
void wleAEHueChanged(WleAEParamHue *param)
{
	EditorObject **objects = NULL;
	int i;

	wleAEGetSelectedObjects(&objects);
	wleAESetApplyingData(true);

	EditUndoBeginGroup(edObjGetUndoStack());
	for (i = 0; i < eaSize(&objects); i++)
	{
		if (objects[i]->type->objType == EDTYPE_TRACKER && (param->property_name || (param->struct_offset && param->struct_pti && param->struct_fieldname)))
		{
			GroupTracker *tracker = wleOpPropsBegin(objects[i]->obj);
			GroupDef *def = tracker ? tracker->def : NULL;
			if (def && param->property_name)
			{
				if (param->is_specified && (param->can_unspecify || param->huevalue))
					groupDefAddPropertyF32(def, param->property_name, param->huevalue * 1.0f);
				else
					groupDefRemoveProperty(def, param->property_name);
			}
			else if (def && param->struct_offset && param->struct_pti && param->struct_fieldname)
			{
				void *property_struct = *(void**)((size_t) def + param->struct_offset);
				int col;

				if (ParserFindColumn(param->struct_pti, param->struct_fieldname, &col))
				{
					if (!property_struct)
						property_struct = *(void**)((size_t) def + param->struct_offset) = StructCreateVoid(param->struct_pti);
					if (!param->can_unspecify || param->is_specified)
						TokenStoreSetF32(param->struct_pti, col, property_struct, 0, param->huevalue * 1.0f, NULL, NULL);
					else
						initstruct_autogen(param->struct_pti, col, property_struct, 0);
				}				
			}
			if (tracker)
			{
				wleOpPropsEndNoUIUpdate();
			}
		}
	}
	if (param->apply_func)
	{
		param->apply_func(param, param->apply_data, objects);
	}
	EditUndoEndGroup(edObjGetUndoStack());
	wleAESetApplyingData(false);
	wleAERefresh();
	eaDestroy(&objects);
}

static void wleAEHueValueChanged(UIRTNode *node, WleAEParamHue *param)
{
	param->is_specified = true;
	wleAEHueChanged(param);
}

static void wleAEHueSliderChanged(UIColorSlider *slider, WleAEParamHue *param)
{
	param->huevalue = slider->current[0];
	param->is_specified = true;
	wleAEHueChanged(param);
}

static void wleAEHueSpecifiedChanged(UIRTNode *node, WleAEParamHue *param)
{
	assert(param->can_unspecify);
	if (!param->is_specified)
		param->huevalue = 0;
	wleAEHueChanged(param);
}

// main
void wleAEHueUpdate(WleAEParamHue *param)
{
	EditorObject **objects = NULL;
	float all_float = 0, next_float;
	bool all_spec = false, next_spec;
	int i;

	param->diff = param->spec_diff = false;
	wleAEGetSelectedObjects(&objects);
	for (i = 0; i < eaSize(&objects); i++)
	{
		EditorObject *obj = objects[i];
		if (obj->type->objType == EDTYPE_TRACKER && param->property_name && !param->update_func)
		{
			GroupTracker *tracker = trackerFromTrackerHandle(obj->obj);
			GroupDef *def = tracker ? tracker->def : NULL;
			const char *property_val = def ? groupDefFindProperty(def, param->property_name) : NULL;

			if (property_val)
			{
				next_float = fillF32FromStr(property_val, 0);
				next_spec = true;
			}
			else
			{
				next_float = 0;
				next_spec = false;
			}
		}
		else if (obj->type->objType == EDTYPE_TRACKER && param->struct_offset && param->struct_pti && param->struct_fieldname)
		{
			GroupTracker *tracker = trackerFromTrackerHandle(obj->obj);
			GroupDef *def = tracker ? tracker->def : NULL;
			void *property_struct = def ? *(void**)((size_t) def + param->struct_offset) : NULL;
			int col;

			if (ParserFindColumn(param->struct_pti, param->struct_fieldname, &col) && property_struct)
			{
				next_float = TokenStoreGetF32(param->struct_pti, col, property_struct, 0, 0);
				next_spec = true;
			}
			else
			{
				next_float = 0;
				next_spec = false;
			}
		}
		else
		{
			param->update_func(param, param->update_data, obj);
			next_float = param->huevalue;
			next_spec = param->is_specified;
		}

		if (!i)
		{
			all_float = next_float;
			all_spec = next_spec;
		}
		else
		{
			if (!param->diff && all_float != next_float)
				param->diff = true;
			if (!param->spec_diff && all_spec != next_spec)
				param->spec_diff = true;
			if (param->spec_diff && param->diff)
				break;
		}
	}

	param->is_specified = all_spec;
	if (param->spec_diff && param->can_unspecify)
	{
		param->huevalue = 0;
		param->is_specified = false;
		param->diff = true;
	}
	else if (param->diff)
		param->huevalue = 0;
	else
		param->huevalue = all_float;

	eaDestroy(&objects);
}

void wleAEHueAddWidgetEx(UIRebuildableTreeNode *auto_widget_node, const char *name, const char *tooltip, const char *param_name, WleAEParamHue *param, F32 min, F32 max, F32 step)
{
	Vec3 slider_vec_min = {0.0f, 1.0f, 1.0f}, slider_vec_max = {360.0f, 1.0f, 1.0f}, slider_vec_start = {0.0f, 1.0f, 1.0f};
	UIAutoWidgetParams params = {0};
	UIColorSlider *color_slider;
	char full_param_name[1024];
	bool did_newline = false;

	params.NoLabel = true;
	params.alignTo = param->left_pad;
	params.disabled = param->disabled;

	if (param->can_unspecify)
	{
		sprintf(full_param_name, "%s|%d|is_specified", param_name, param->index);
		ui_AutoWidgetAddKeyed(auto_widget_node, parse_WleAEParamHue, "is_specified", full_param_name, param, true, wleAEHueSpecifiedChanged, param, &params, tooltip);
		did_newline = true;
		params.alignTo += 20;

		// skin specified check box
		if (auto_widget_node->children)
		{
			UIWidget *check_button = auto_widget_node->children[eaSize(&auto_widget_node->children) - 1]->widget1;
			wleSkinDiffWidget(check_button, param->spec_diff);
		}
	}

	sprintf(full_param_name, "%s|%d|label", param_name, param->index);
	ui_RebuildableTreeAddLabelKeyed(auto_widget_node, name, full_param_name, &params, !did_newline);

	params.alignTo += ((param->entry_align ? param->entry_align : 120) + (!param->can_unspecify ? 20 : 0));
	sprintf(full_param_name, "%s|%d|fvalue_dup", param_name, param->index);
	color_slider = ui_ColorSliderCreate(0, 0, (param->slider_width ? param->slider_width : 100), slider_vec_min, slider_vec_max, true);
	ui_ColorSliderSetChangedCallback(color_slider, wleAEHueSliderChanged, param);
	slider_vec_start[0] = param->huevalue;
	ui_ColorSliderSetValue(color_slider, slider_vec_start);
	ui_RebuildableTreeAddWidget(auto_widget_node, UI_WIDGET(color_slider), NULL, false, full_param_name, &params);

	params.alignTo += (color_slider->widget.width + 5);
	params.type = AWT_Spinner;
	params.spinnerStartF = wleAESpinnerSpinStart;
	params.spinnerStopF = wleAESpinnerSpinStop;
	setVec3same(params.min, min);
	setVec3same(params.max, max);
	setVec3same(params.step, step);
	sprintf(full_param_name, "%s|%d|fvalue", param_name, param->index);
	params.precision = param->precision;
	params.overrideWidth = param->entry_width;
	ui_AutoWidgetAddKeyed(auto_widget_node, parse_WleAEParamHue, "huevalue", full_param_name, param, false, wleAEHueValueChanged, param, &params, tooltip);

	// skin widget as necessary
	if (auto_widget_node->children)
	{
		UIWidget *text_entry = auto_widget_node->children[eaSize(&auto_widget_node->children) - 1]->widget1;
		wleSkinDiffWidget(text_entry, param->diff);
	}
}

/********************
* EXPRESSION
********************/
// callbacks
static void wleAEExpressionChanged(WleAEParamExpression *param)
{
	EditorObject **objects = NULL;
	int i;

	wleAEGetSelectedObjects(&objects);
	wleAESetApplyingData(true);

	EditUndoBeginGroup(edObjGetUndoStack());
	for (i = 0; i < eaSize(&objects); i++)
	{
		if (objects[i]->type->objType == EDTYPE_TRACKER && (param->property_name || (param->struct_offset && param->struct_pti && param->struct_fieldname)))
		{
			GroupTracker *tracker = wleOpPropsBegin(objects[i]->obj);
			GroupDef *def = tracker ? tracker->def : NULL;

			if (def && param->property_name)
			{	
				char *str = exprGetCompleteString(param->exprvalue);
				if (param->is_specified && (param->can_unspecify || str))
					groupDefAddProperty(def, param->property_name, str ? str : "");
				else
					groupDefRemoveProperty(def, param->property_name);
			}
			else if (def && param->struct_offset && param->struct_pti && param->struct_fieldname)
			{
				void *property_struct = *(void**)((size_t) def + param->struct_offset);
				int col;

				if (ParserFindColumn(param->struct_pti, param->struct_fieldname, &col))
				{
					void *expr;

					if (!property_struct)
						property_struct = *(void**)((size_t) def + param->struct_offset) = StructCreateVoid(param->struct_pti);

					expr = TokenStoreGetPointer(param->struct_pti, col, property_struct, 0, NULL);
					if (expr)
						exprDestroy(expr);

					if (!param->can_unspecify || param->is_specified)
					{
						expr = exprClone(param->exprvalue);
						TokenStoreSetPointer(param->struct_pti, col, property_struct, 0, expr, NULL);
					}
					else
						initstruct_autogen(param->struct_pti, col, property_struct, 0);
				}				
			}
			if (tracker)
			{
				wleOpPropsEndNoUIUpdate();
			}
		}
	}
	if (param->apply_func)
	{
		param->apply_func(param, param->apply_data, objects);
	}
	EditUndoEndGroup(edObjGetUndoStack());
	wleAESetApplyingData(false);
	wleAERefresh();
	eaDestroy(&objects);
}

static void wleAEExpressionValueChanged(UIRTNode *node, WleAEParamExpression *param)
{
	const char *expr_str = exprGetCompleteString(param->exprvalue);
	if (expr_str && expr_str[0])
		param->is_specified = true;
	else
		param->is_specified = false;
	wleAEExpressionChanged(param);
}

static void wleAEExpressionSpecifiedChanged(UIRTNode *node, WleAEParamExpression *param)
{
	assert(param->can_unspecify);
	if (param->exprvalue)
		exprDestroy(param->exprvalue);
	if (param->is_specified)
		param->exprvalue = exprCreate();
	else
		param->exprvalue = NULL;
	wleAEExpressionChanged(param);
}

// main
void wleAEExpressionUpdate(WleAEParamExpression *param)
{
	EditorObject **objects = NULL;
	Expression *all_expr = NULL, *next_expr;
	bool all_spec = false, next_spec;
	int i;

	param->diff = param->spec_diff = false;
	if (param->exprvalue)
	{
		exprDestroy(param->exprvalue);
		param->exprvalue = NULL;
	}
	wleAEGetSelectedObjects(&objects);
	for (i = 0; i < eaSize(&objects); i++)
	{
		EditorObject *obj = objects[i];
		if (obj->type->objType == EDTYPE_TRACKER && param->property_name && !param->update_func)
		{
			GroupTracker *tracker = trackerFromTrackerHandle(obj->obj);
			GroupDef *def = tracker ? tracker->def : NULL;
			const char *property_val = def ? groupDefFindProperty(def, param->property_name) : NULL;

			if (property_val)
			{
				next_expr = exprCreate();
				if (property_val[0])
					exprGenerateFromString(next_expr, param->context, property_val, NULL);
				next_spec = true;
			}
			else
			{
				next_expr = NULL;
				next_spec = false;
			}
		}
		else if (obj->type->objType == EDTYPE_TRACKER && param->struct_offset && param->struct_pti && param->struct_fieldname)
		{
			GroupTracker *tracker = trackerFromTrackerHandle(obj->obj);
			GroupDef *def = tracker ? tracker->def : NULL;
			void *property_struct = def ? *(void**)((size_t) def + param->struct_offset) : NULL;
			int col;

			if (ParserFindColumn(param->struct_pti, param->struct_fieldname, &col) && property_struct)
			{
				next_expr = exprClone(TokenStoreGetPointer(param->struct_pti, col, property_struct, 0, 0));
				next_spec = !!next_expr;
			}
			else
			{
				next_expr = NULL;
				next_spec = false;
			}
		}
		else
		{
			param->update_func(param, param->update_data, obj);
			next_expr = param->exprvalue;
			next_spec = param->is_specified;
		}

		if (!i)
		{
			if (next_expr)
				all_expr = exprClone(next_expr);
			all_spec = next_spec;
		}
		else
		{
			if (!param->diff && exprCompare(all_expr, next_expr) != 0)
				param->diff = true;
			if (!param->spec_diff && all_spec != next_spec)
				param->spec_diff = true;
		}

		if (next_expr)
			exprDestroy(next_expr);
		if (param->spec_diff && param->diff)
			break;
	}

	param->is_specified = all_spec;
	if (param->spec_diff && param->can_unspecify)
	{
		param->exprvalue = NULL;
		param->is_specified = false;
		param->diff = true;
	}
	else if (param->diff)
		param->exprvalue = NULL;
	else if (all_expr)
	{
		char *expr_str = exprGetCompleteString(all_expr);
		if (expr_str && expr_str[0])
			param->exprvalue = exprClone(all_expr);
		else
			param->exprvalue = NULL;
	}

	if (all_expr)
		exprDestroy(all_expr);
	eaDestroy(&objects);
}

void wleAEExpressionAddWidgetEx(UIRebuildableTreeNode *auto_widget_node, const char *name, const char *tooltip, const char *param_name, WleAEParamExpression *param)
{
	UIAutoWidgetParams params = {0};
	char full_param_name[1024];
	bool did_newline = false;

	params.NoLabel = true;
	params.alignTo = param->left_pad;
	params.disabled = param->disabled;

	if (param->can_unspecify)
	{
		sprintf(full_param_name, "%s|%d|is_specified", param_name, param->index);
		ui_AutoWidgetAddKeyed(auto_widget_node, parse_WleAEParamExpression, "is_specified", full_param_name, param, true, wleAEExpressionSpecifiedChanged, param, &params, tooltip);
		did_newline = true;
		params.alignTo += 20;

		// skin specified check box
		if (auto_widget_node->children)
		{
			UIWidget *check_button = auto_widget_node->children[eaSize(&auto_widget_node->children) - 1]->widget1;
			wleSkinDiffWidget(check_button, param->spec_diff);
		}
	}

/*	if (param->source && param->source_handle)
	{
		UIButton *button = ui_ButtonCreateImageOnly("button_center", 0, 0, linkToTrackerHandle, param->source_handle);

		sprintf(full_param_name, "%s|%d|source", param_name, param->index);
		params.type = AWT_Default;
		ui_WidgetSetDimensions(UI_WIDGET(button), 20, 20);
		ui_WidgetSetDimensions(UI_WIDGET(button->sprite), 20, 20);
		ui_WidgetSetTooltipString(UI_WIDGET(button), param->source);
		ui_RebuildableTreeAddWidget(auto_widget_node, UI_WIDGET(button), NULL, false, full_param_name, &params);
		params.alignTo += 30;
	}*/

	sprintf(full_param_name, "%s|%d|label", param_name, param->index);
	ui_RebuildableTreeAddLabelKeyed(auto_widget_node, name, full_param_name, &params, !did_newline);

	params.alignTo += ((param->entry_align ? param->entry_align : 120) + (!param->can_unspecify ? 20 : 0));
	params.overrideWidth = param->entry_width ? param->entry_width : 200;
	params.exprContext = param->context;
	sprintf(full_param_name, "%s|%d|exprvalue", param_name, param->index);
	ui_AutoWidgetAddKeyed(auto_widget_node, parse_WleAEParamExpression, "exprvalue", full_param_name, param, false, wleAEExpressionValueChanged, param, &params, tooltip);

	// skin widget as necessary
	if (auto_widget_node->children)
	{
		UIExpressionEntry *expr_entry = (UIExpressionEntry*) auto_widget_node->children[eaSize(&auto_widget_node->children) - 1]->widget1;
		wleSkinDiffWidget(UI_WIDGET(expr_entry->pEntry), param->diff);
	}
}

/********************
* TEXTURE
********************/
// callbacks
static void wleAETextureChanged(WleAEParamTexture *param)
{
	EditorObject **objects = NULL;
	int i;

	wleAEGetSelectedObjects(&objects);
	wleAESetApplyingData(true);

	EditUndoBeginGroup(edObjGetUndoStack());
	for (i = 0; i < eaSize(&objects); i++)
	{
		if (objects[i]->type->objType == EDTYPE_TRACKER && (param->property_name || (param->struct_offset && param->struct_pti && param->struct_fieldname)))
		{
			GroupTracker *tracker = wleOpPropsBegin(objects[i]->obj);
			GroupDef *def = tracker ? tracker->def : NULL;

			if (def && param->property_name)
			{
				if (param->is_specified && (param->can_unspecify || param->texturename))
					groupDefAddProperty(def, param->property_name, param->texturename ? param->texturename : "");
				else
					groupDefRemoveProperty(def, param->property_name);
			}
			else if (def && param->struct_offset && param->struct_pti && param->struct_fieldname)
			{
				void *property_struct = *(void**)((size_t) def + param->struct_offset);
				int col;

				if (ParserFindColumn(param->struct_pti, param->struct_fieldname, &col))
				{
					if (!property_struct)
						property_struct = *(void**)((size_t) def + param->struct_offset) = StructCreateVoid(param->struct_pti);
					if (!param->can_unspecify || param->is_specified)
						TokenStoreSetString(param->struct_pti, col, property_struct, 0, param->texturename, NULL, NULL, NULL, NULL);
					else
						initstruct_autogen(param->struct_pti, col, property_struct, 0);
				}				
			}
			if (tracker)
			{
				wleOpPropsEndNoUIUpdate();
			}
		}
	}
	if (param->apply_func)
	{
		param->apply_func(param, param->apply_data, objects);
	}
	EditUndoEndGroup(edObjGetUndoStack());
	wleAESetApplyingData(false);
	wleAERefresh();
	eaDestroy(&objects);
}

static bool wleAETextureSelectedCallback(EMPicker *picker, EMPickerSelection **selections, WleAEParamTexture *param)
{
	int width = param->texture_size ? param->texture_size : 32;
	char texture_name[1024];

	if (!eaSize(&selections))
	{
		return false;
	}

	getFileNameNoExt(texture_name, selections[0]->doc_name);
	param->is_specified = true;
	StructFreeString(param->texturename);
	param->texturename = StructAllocString(texture_name);
	wleAETextureChanged(param);
	
	return true;
}

static void wleAETextureSelected(UIButton * button, WleAEParamTexture *param)
{
	EMPicker* picker = emPickerGetByName( "Texture Picker" );

	if (picker)
		emPickerShow(picker, NULL, false, wleAETextureSelectedCallback, param);
}

static void wleAETextureSpecifiedChanged(UIRTNode *node, WleAEParamTexture *param)
{
	assert(param->can_unspecify);
	if (!param->is_specified)
	{
		if (param->texturename)
			StructFreeString(param->texturename);
		param->texturename = NULL;
	}
	wleAETextureChanged(param);
}

static void wleAETextureNameDropped(UIWidget *source, UIButton *button, UIDnDPayload *payload, WleAEParamTexture *param)
{
	if (stricmp(payload->type, UI_DND_ASSET "wtex") == 0 || stricmp(payload->type, UI_DND_ASSET ".wtex") ==0 ||
		stricmp(payload->type, UI_DND_ASSET "TexWord") == 0 || stricmp(payload->type, UI_DND_ASSET ".TexWord") ==0)
	{
		char *texture_name = (char*) payload->payload;
		int width = param->texture_size ? param->texture_size : 32;

		StructFreeString(param->texturename);
		param->texturename = StructAllocString(texture_name);
		ui_ButtonSetImage(button, texture_name ? texture_name : "white");
		ui_ButtonSetImageStretch(button, true);
		ui_WidgetSetDimensions(UI_WIDGET(button), width, width);
		wleAETextureChanged(param);
	}
}

// main
void wleAETextureUpdate(WleAEParamTexture *param)
{
	EditorObject **objects = NULL;
	EditorObject *all_source = NULL;
	char *all_texture = NULL, *next_texture;
	bool all_spec = false, next_spec;
	int i;

	if (param->texturename)
	{
		StructFreeString(param->texturename);
		param->texturename = NULL;
	}
	param->diff = param->spec_diff = false;
	wleAEGetSelectedObjects(&objects);
	if (param->source)
	{
		editorObjectDeref(param->source);
		param->source = NULL;
	}
	for (i = 0; i < eaSize(&objects); i++)
	{
		EditorObject *obj = objects[i];
		param->source = NULL;
		if (obj->type->objType == EDTYPE_TRACKER && param->property_name && !param->update_func)
		{
			GroupTracker *tracker = trackerFromTrackerHandle(obj->obj);
			GroupDef *def = tracker ? tracker->def : NULL;
			const char *property_val = def ? groupDefFindProperty(def, param->property_name) : NULL;

			if (property_val)
			{
				next_texture = StructAllocString(property_val);
				next_spec = true;
			}
			else
			{
				next_texture = NULL;
				next_spec = false;
			}
		}
		else if (obj->type->objType == EDTYPE_TRACKER && param->struct_offset && param->struct_pti && param->struct_fieldname)
		{
			GroupTracker *tracker = trackerFromTrackerHandle(obj->obj);
			GroupDef *def = tracker ? tracker->def : NULL;
			void *property_struct = def ? *(void**)((size_t) def + param->struct_offset) : NULL;
			int col;

			if (ParserFindColumn(param->struct_pti, param->struct_fieldname, &col) && property_struct)
			{
				next_texture = StructAllocString(TokenStoreGetString(param->struct_pti, col, property_struct, 0, NULL));
				next_spec = !!next_texture;
			}
			else
			{
				next_texture = NULL;
				next_spec = false;
			}
		}
		else
		{
			param->update_func(param, param->update_data, obj);
			next_texture = param->texturename;
			next_spec = param->is_specified;
			if (param->source)
				editorObjectRef(param->source);
		}

		if (!i)
		{
			all_texture = StructAllocString(next_texture);
			all_spec = next_spec;
			all_source = param->source;
		}
		else
		{
			// compare sources
			if (all_source && (!param->source || edObjCompare(param->source, all_source) != 0))
			{
				if (all_source)
					editorObjectDeref(all_source);
				all_source = NULL;
			}
			if (param->source)
				editorObjectDeref(param->source);

			if (!param->diff)
			{
				if ((!all_texture && next_texture) || (all_texture && !next_texture))
					param->diff = true;
				else if (all_texture && next_texture && strcmpi(all_texture, next_texture) != 0)
					param->diff = true;
			}
			if (!param->spec_diff && all_spec != next_spec)
				param->spec_diff = true;
		}

		if (next_texture)
			StructFreeString(next_texture);
		if (param->spec_diff && param->diff)
			break;
	}

	param->source = all_source;
	param->is_specified = all_spec;
	if (param->spec_diff && param->can_unspecify)
	{
		param->texturename = NULL;
		param->is_specified = false;
		param->diff = true;
	}
	else if (param->diff)
		param->texturename = NULL;
	else
		param->texturename = StructAllocString(all_texture);

	if (all_texture)
		StructFreeString(all_texture);
	eaDestroy(&objects);
}

void wleAETextureAddWidgetEx(UIRebuildableTreeNode *auto_widget_node, const char *name, const char *tooltip, const char *param_name, WleAEParamTexture *param)
{
	UIAutoWidgetParams params = {0};
	UIButton *button;
	char full_param_name[1024];
	bool did_newline = false;
	int width = param->texture_size ? param->texture_size : 32;

	params.NoLabel = true;
	params.alignTo = param->left_pad;
	params.disabled = param->disabled;

	if (param->can_unspecify)
	{
		sprintf(full_param_name, "%s|%d|is_specified", param_name, param->index);
		ui_AutoWidgetAddKeyed(auto_widget_node, parse_WleAEParamTexture, "is_specified", full_param_name, param, true, wleAETextureSpecifiedChanged, param, &params, tooltip);
		did_newline = true;
		params.alignTo += 20;

		// skin specified check box
		if (auto_widget_node->children)
		{
			UIWidget *check_button = auto_widget_node->children[eaSize(&auto_widget_node->children) - 1]->widget1;
			wleSkinDiffWidget(check_button, param->spec_diff);
		}
	}

	if (param->source)
	{
		button = ui_ButtonCreateImageOnly("button_center", 0, 0, wleAESelectSourceClicked, param->source);

		sprintf(full_param_name, "%s|%d|source", param_name, param->index);
		params.type = AWT_Default;
		ui_WidgetSetDimensions(UI_WIDGET(button), 20, 20);
		ui_ButtonSetImageStretch(button, true);
		ui_WidgetSetTooltipString(UI_WIDGET(button), param->source->name);
		ui_RebuildableTreeAddWidget(auto_widget_node, UI_WIDGET(button), NULL, false, full_param_name, &params);
		params.alignTo += 30;
	}

	sprintf(full_param_name, "%s|%d|label", param_name, param->index);
	ui_RebuildableTreeAddLabelKeyed(auto_widget_node, name, full_param_name, &params, !did_newline);

	// skin label if parameter is different
	if (auto_widget_node->children)
	{
		UIWidget *label = auto_widget_node->children[eaSize(&auto_widget_node->children) - 1]->widget1;
		wleSkinDiffOrSourcedWidget(label, param->diff, param->source);
	}

	params.alignTo += ((param->entry_align ? param->entry_align : 120) + (!param->can_unspecify ? 20 : 0));
	sprintf(full_param_name, "%s|%d|texture_name", param_name, param->index);

	button = ui_ButtonCreateImageOnly(param->texturename ? param->texturename : "white", 0, 0, wleAETextureSelected, param);
	ui_WidgetSetDimensions(UI_WIDGET(button), width, width);
	ui_ButtonSetImageStretch(button, true);
	ui_RebuildableTreeAddWidget(auto_widget_node, UI_WIDGET(button), NULL, false, full_param_name, &params);
	ui_WidgetSetDropCallback(UI_WIDGET(button), wleAETextureNameDropped, param);
}

/********************
* PICKER
********************/
// callbacks
static void wleAEPickerObjectChanged(WleAEParamPicker *param)
{
	EditorObject **objects = NULL;

	wleAEGetSelectedObjects(&objects);
	wleAESetApplyingData(true);

	EditUndoBeginGroup(edObjGetUndoStack());
	if (param->apply_func)
	{
		param->apply_func(param, param->apply_data, objects);
	}
	EditUndoEndGroup(edObjGetUndoStack());
	wleAESetApplyingData(false);
	wleAERefresh();
	eaDestroy(&objects);
}

static void wleAESetObjectNameFromObject(WleAEParamPicker *param)
{
	const char *field_name = param->parse_name_field ? param->parse_name_field : "Name";
	int i;
	StructFreeString(param->object_name);
	param->object_name = NULL;
	if (param->object_parse_table && param->object)
	{
		FORALL_PARSETABLE(param->object_parse_table, i)
		{
			if (param->object_parse_table[i].name && strcmpi(param->object_parse_table[i].name, field_name) == 0)
			{
				char *estr = NULL;
				TokenWriteText(param->object_parse_table, i, param->object, &estr, true);
				param->object_name = StructAllocString(estr);
				estrDestroy(&estr);
				break;
			}
		}
	}
}

static bool wleAEPickerObjectSelected(EMPicker *picker, EMPickerSelection **selections, WleAEParamPicker *param)
{
	if (eaSize(&selections) == 0)
		return false;

	param->object = selections[0]->data;
	param->object_parse_table = selections[0]->table;
	wleAESetObjectNameFromObject(param);
	wleAEPickerObjectChanged(param);

	return true;
}

static void wleAEPickerButtonClicked(UIButton *button, WleAEParamPicker *param)
{
	emPickerShow(param->picker, "Choose", false, wleAEPickerObjectSelected, param);
}

static void wleAEPickerSpecifiedChanged(UIRTNode *node, WleAEParamPicker *param)
{
	assert(param->can_unspecify);
	if (!param->is_specified)
	{
		param->object = NULL;
		param->object_parse_table = NULL;
		StructFreeString(param->object_name);
		param->object_name = NULL;
	}
	wleAEPickerObjectChanged(param);
}

// main
void wleAEPickerUpdate(WleAEParamPicker *param)
{
	EditorObject **objects = NULL;
	EditorObject *all_source = NULL;
	void *all_object = NULL, *next_object;
	char *all_object_name = NULL, *next_object_name;
	ParseTable *all_object_parse_table = NULL, *next_object_parse_table;
	bool all_spec = false, next_spec;
	int i;

	if (param->object_name)
	{
		StructFreeString(param->object_name);
		param->object_name = NULL;
	}
	param->object_name = NULL;
	param->object = NULL;
	param->object_parse_table = NULL;
	param->diff = param->spec_diff = false;
	wleAEGetSelectedObjects(&objects);
	if (param->source)
	{
		editorObjectDeref(param->source);
		param->source = NULL;
	}

	assert(param->update_func);

	for (i = 0; i < eaSize(&objects); i++)
	{
		EditorObject *obj = objects[i];
		param->source = NULL;
		param->update_func(param, param->update_data, obj);
		wleAESetObjectNameFromObject(param);
		next_object = param->object;
		next_object_parse_table = param->object_parse_table;
		next_object_name = StructAllocString(param->object_name);
		next_spec = param->is_specified;
		if (param->source)
			editorObjectRef(param->source);

		if (!i)
		{
			all_object = next_object;
			all_object_parse_table = next_object_parse_table;
			all_object_name = StructAllocString(next_object_name);
			all_spec = next_spec;
			all_source = param->source;
		}
		else
		{
			// compare sources
			if (all_source && (!param->source || edObjCompare(param->source, all_source) != 0))
			{
				if (all_source)
					editorObjectDeref(all_source);
				all_source = NULL;
			}
			if (param->source)
				editorObjectDeref(param->source);

			if (!param->diff)
			{
				if ((!all_object && next_object) || (all_object && !next_object))
					param->diff = true;
				else if (all_object && next_object && all_object != next_object)
					param->diff = true;
			}
			if (!param->spec_diff && all_spec != next_spec)
				param->spec_diff = true;
		}

		if (next_object_name)
		{
			StructFreeString(next_object_name);
			next_object_name = NULL;
		}
		if (param->spec_diff && param->diff)
			break;
	}

	param->source = all_source;
	param->is_specified = all_spec;
	if (param->spec_diff && param->can_unspecify)
	{
		param->object = NULL;
		param->object_parse_table = NULL;
		param->object_name = NULL;
		param->is_specified = false;
		param->diff = true;
	}
	else if (param->diff)
	{
		param->object = NULL;
		param->object_parse_table = NULL;
		param->object_name = NULL;
	}
	else
	{
		param->object = all_object;
		param->object_parse_table = all_object_parse_table;
		param->object_name = StructAllocString(all_object_name);
	}

	if (all_object_name)
		StructFreeString(all_object_name);
	eaDestroy(&objects);
}

void wleAEPickerAddWidgetEx(UIRebuildableTreeNode *auto_widget_node, const char *name, const char *tooltip, const char *param_name, WleAEParamPicker *param)
{
	UIAutoWidgetParams params = {0};
	UIButton *button;
	char full_param_name[1024];
	bool did_newline = false;

	params.NoLabel = true;
	params.alignTo = param->left_pad;
	params.disabled = param->disabled;

	if (param->can_unspecify)
	{
		sprintf(full_param_name, "%s|%d|is_specified", param_name, param->index);
		ui_AutoWidgetAddKeyed(auto_widget_node, parse_WleAEParamTexture, "is_specified", full_param_name, param, true, wleAEPickerSpecifiedChanged, param, &params, tooltip);
		did_newline = true;
		params.alignTo += 20;

		// skin specified check box
		if (auto_widget_node->children)
		{
			UIWidget *check_button = auto_widget_node->children[eaSize(&auto_widget_node->children) - 1]->widget1;
			wleSkinDiffWidget(check_button, param->spec_diff);
		}
	}

	if (param->source)
	{
		button = ui_ButtonCreateImageOnly("button_center", 0, 0, wleAESelectSourceClicked, param->source);

		sprintf(full_param_name, "%s|%d|source", param_name, param->index);
		params.type = AWT_Default;
		ui_WidgetSetDimensions(UI_WIDGET(button), 20, 20);
		ui_ButtonSetImageStretch(button, true);
		ui_WidgetSetTooltipString(UI_WIDGET(button), param->source->name);
		ui_RebuildableTreeAddWidget(auto_widget_node, UI_WIDGET(button), NULL, false, full_param_name, &params);
		params.alignTo += 30;
	}

	sprintf(full_param_name, "%s|%d|label", param_name, param->index);
	ui_RebuildableTreeAddLabelKeyed(auto_widget_node, name, full_param_name, &params, !did_newline);

	// skin label if parameter is different
	if (auto_widget_node->children)
	{
		UIWidget *label = auto_widget_node->children[eaSize(&auto_widget_node->children) - 1]->widget1;
		wleSkinDiffOrSourcedWidget(label, param->diff, param->source);
	}

	params.alignTo += ((param->entry_align ? param->entry_align : 120) + (!param->can_unspecify ? 20 : 0));
	sprintf(full_param_name, "%s|%d|object", param_name, param->index);

	button = ui_ButtonCreate(param->object_name ? param->object_name : "none", 0, 0, wleAEPickerButtonClicked, param);
	ui_RebuildableTreeAddWidget(auto_widget_node, UI_WIDGET(button), NULL, false, full_param_name, &params);
//	ui_WidgetSetDropCallback(UI_WIDGET(button), wleAEPickerObjectDropped, param);
}

/********************
* MESSAGE
********************/
// callbacks
static void wleAEMessageChanged(WleAEParamMessage *param)
{
	EditorObject **objects = NULL;
	int i;

	wleAEGetSelectedObjects(&objects);
	wleAESetApplyingData(true);

	EditUndoBeginGroup(edObjGetUndoStack());
	for (i = 0; i < eaSize(&objects); i++)
	{
		if (objects[i]->type->objType == EDTYPE_TRACKER && (param->struct_offset && param->struct_pti && param->struct_fieldname))
		{
			GroupTracker *tracker = wleOpPropsBegin(objects[i]->obj);
			GroupDef *def = tracker ? tracker->def : NULL;

			if (def && param->struct_offset && param->struct_pti && param->struct_fieldname)
			{
				void *property_struct = *(void**)((size_t) def + param->struct_offset);
				int col;

				if (ParserFindColumn(param->struct_pti, param->struct_fieldname, &col))
				{
					if (!property_struct)
						property_struct = *(void**)((size_t) def + param->struct_offset) = StructCreateVoid(param->struct_pti);
					if (!param->can_unspecify || param->is_specified)
					{
						DisplayMessage *display_message = TokenStoreGetPointer(param->struct_pti, col, property_struct, 0, NULL);

						if (display_message)
						{
							StructCopyAll(parse_Message, &param->message, display_message->pEditorCopy);
							groupDefFixupMessageKey( &display_message->pEditorCopy->pcMessageKey, def, param->source_key, NULL );
							if( !display_message->pEditorCopy->pcScope || !display_message->pEditorCopy->pcScope[0] ) {
								display_message->pEditorCopy->pcScope = allocAddString(param->source_key);
							}
						}
					}
					else
						initstruct_autogen(param->struct_pti, col, property_struct, 0);
				}
			}
			if (tracker)
			{
				wleOpPropsEndNoUIUpdate();
			}
		}
	}
	if (param->apply_func)
	{
		param->apply_func(param, param->apply_data, objects);
	}
	EditUndoEndGroup(edObjGetUndoStack());
	wleAESetApplyingData(false);
	wleAERefresh();
	eaDestroy(&objects);
}

static void wleAEMessageValueChanged(UIMessageEntry *entry, WleAEParamMessage *param)
{
	Message* message = &param->message;
	Message* newMessage = entry->pMessage;

	if (!message || !newMessage)
		return;

	if (  strcmp_safe(message->pcDefaultString, newMessage->pcDefaultString) == 0
		  && stricmp(message->pcDescription, newMessage->pcDescription) == 0
		  && stricmp(message->pcScope, newMessage->pcScope) == 0)
	{
		// No change, so do nothing
		return;
	}

	StructCopyAll(parse_Message, newMessage, message);
	
	param->is_specified = true;
	wleAEMessageChanged(param);
}

static void wleAEMessageSpecifiedChanged(UIRTNode *node, WleAEParamMessage *param)
{
	assert(param->can_unspecify);
	if (!param->is_specified)
	{
		StructFreeString( param->message.pcDefaultString );
		param->message.pcDefaultString = StructAllocString( "" );
	}
	wleAEMessageChanged(param);
}

// main
void wleAEMessageUpdate(WleAEParamMessage *param)
{
	EditorObject **objects = NULL;
	int i;
	Message accum;

	param->spec_diff = param->key_diff = param->scope_diff = param->desc_diff = param->default_str_diff = false;

	wleAEGetSelectedObjects(&objects);
	if( eaSize(&objects) == 0 ) {
		eaDestroy(&objects);
		return;
	}

	StructInit(parse_Message, &accum);
	for( i = 0; i != eaSize(&objects); ++i ) {
		EditorObject *obj = objects[i];

		if( obj->type->objType == EDTYPE_TRACKER && param->struct_offset && param->struct_pti && param->struct_fieldname)
		{
			GroupTracker *tracker = trackerFromTrackerHandle(obj->obj);
			GroupDef *def = tracker ? tracker->def : NULL;
			void *property_struct = NULL;
			int col;

			if (def)
			{
				property_struct = *(void**)((size_t) def + param->struct_offset);
			}
			
			if (ParserFindColumn(param->struct_pti, param->struct_fieldname, &col) && property_struct)
			{
				DisplayMessage *display_message = TokenStoreGetPointer(param->struct_pti, col, property_struct, 0, NULL);
				langMakeEditorCopy(parse_DisplayMessage, display_message, true);
				groupDefFixupMessageKey( &display_message->pEditorCopy->pcMessageKey, def, param->source_key, NULL );
				if( !display_message->pEditorCopy->pcScope || !display_message->pEditorCopy->pcScope[ 0 ]) {
					display_message->pEditorCopy->pcScope = allocAddString(param->source_key);
				}

				StructCopyAll(parse_Message, display_message->pEditorCopy, &param->message);
			}
		}
		else
		{
			param->update_func(param, param->update_data, obj);
		}

		// update diff
		if( i == 0 ) {
			StructCopyAll(parse_Message, &param->message, &accum);
		} else {
			param->key_diff = param->key_diff || stricmp( accum.pcMessageKey, param->message.pcMessageKey ) != 0;
			param->scope_diff = param->scope_diff || stricmp( accum.pcScope, param->message.pcScope ) != 0;
			param->desc_diff = param->desc_diff || stricmp( accum.pcDescription, param->message.pcDescription ) != 0;
			param->default_str_diff = param->default_str_diff || strcmp_safe( accum.pcDefaultString, param->message.pcDefaultString ) != 0;
		}
	}
	StructDeInit(parse_Message, &accum);
	eaDestroy(&objects);

	if( param->key_diff ) {
		param->message.pcMessageKey = NULL;
	}
	if( param->scope_diff ) {
		param->message.pcScope = NULL;
	}
	if( param->desc_diff ) {
		param->message.pcDescription = NULL;
	}
	if( param->default_str_diff ) {
		param->message.pcDefaultString = NULL;
	}
}

static bool forceKeyDiff;
static bool forceScopeDiff;
static bool forceDescDiff;
static bool forceDefaultStringDiff;
AUTO_CMD_INT(forceKeyDiff, forceKeyDiff);
AUTO_CMD_INT(forceScopeDiff, forceScopeDiff);
AUTO_CMD_INT(forceDescDiff, forceDescDiff);
AUTO_CMD_INT(forceDefaultStringDiff, forceDefaultStringDiff);

void wleAEMessageAddWidgetEx(UIRebuildableTreeNode *auto_widget_node, const char *name, const char *tooltip, const char *param_name, WleAEParamMessage *param, bool bNewLine)
{
	UIAutoWidgetParams params = {0};
	char full_param_name[1024];
	bool did_newline = !bNewLine;

	params.NoLabel = true;
	params.alignTo = param->left_pad;
	params.disabled = param->disabled;

	if (param->can_unspecify)
	{
		sprintf(full_param_name, "%s|%d|is_specified", param_name, param->index);
		ui_AutoWidgetAddKeyed(auto_widget_node, parse_WleAEParamMessage, "is_specified", full_param_name, param, !did_newline, wleAEMessageSpecifiedChanged, param, &params, tooltip);
		did_newline = true;
		params.alignTo += 20;

		// skin specified check box
		if (auto_widget_node->children)
		{
			UIWidget *check_button = auto_widget_node->children[eaSize(&auto_widget_node->children) - 1]->widget1;
			wleSkinDiffWidget(check_button, param->spec_diff);
		}
	}

	sprintf(full_param_name, "%s|%d|label", param_name, param->index);
	ui_RebuildableTreeAddLabelKeyed(auto_widget_node, name, full_param_name, &params, !did_newline);
	params.alignTo += ((param->entry_align ? param->entry_align : 120) + (!param->can_unspecify ? 20 : 0));
	sprintf(full_param_name, "%s|%d|strvalue:", param_name, param->index);
	
	param->pMsgEntry = ui_MessageEntryCreate(&param->message, 0, 0, param->entry_width);
	ui_MessageEntrySetCanEditKey( param->pMsgEntry, false );
	if ((param->entry_width > 0.0) && (param->entry_width <= 1.0))
		ui_WidgetSetWidthEx(UI_WIDGET(param->pMsgEntry), param->entry_width, UIUnitPercentage);

	ui_MessageEntrySetChangedCallback(param->pMsgEntry, wleAEMessageValueChanged, param);
	ui_RebuildableTreeAddWidget(auto_widget_node, UI_WIDGET(param->pMsgEntry), NULL, false, full_param_name, &params);

	// skin widget as necessary
	ui_MessageEntrySetSkin(param->pMsgEntry, wleDiffSkin(),
						   param->key_diff || forceKeyDiff,
						   param->scope_diff || forceScopeDiff,
						   param->desc_diff || forceDescDiff,
						   param->default_str_diff || forceDefaultStringDiff);
}

/********************
* GAME ACTION
********************/
// callbacks
static void wleAEGameActionChanged(WleAEParamGameAction *param)
{
	EditorObject **objects = NULL;

	wleAEGetSelectedObjects(&objects);
	wleAESetApplyingData(true);

	EditUndoBeginGroup(edObjGetUndoStack());
	if (param->apply_func)
	{
		param->apply_func(param, param->apply_data, objects);
	}
	EditUndoEndGroup(edObjGetUndoStack());
	wleAESetApplyingData(false);
	wleAERefresh();
	eaDestroy(&objects);
}

static void wleAEGameActionValueChanged(UIGameActionEditButton *button, WleAEParamGameAction *param)
{
	if ( gameactionblock_Compare(param->action_block, button->pActionBlock) )
	{
		// No change, so do nothing
		return;
	}

	if (param->action_block)
	{
		StructDestroy(parse_WorldGameActionBlock, param->action_block);
		param->action_block = NULL;
	}
	if (button->pActionBlock && eaSize(&button->pActionBlock->eaActions))
	{
		param->action_block = StructClone(parse_WorldGameActionBlock, button->pActionBlock);
		param->is_specified = true;
	}
	else
		param->is_specified = false;

	wleAEGameActionChanged(param);
}

typedef struct GameActionMessageFixupData {
	int it;
	WleAEParamGameAction* param;
} GameActionMessageFixupData;

static void wleAEGameActionMessageFixup1(DisplayMessage* pDispMsg, GameActionMessageFixupData *data)
{
	EditorObject **objects = NULL;
	wleAEGetSelectedObjects(&objects);
	
	langMakeEditorCopy(parse_DisplayMessage, pDispMsg, true);

	pDispMsg->pEditorCopy->pcMessageKey = NULL;
	if (eaSize(&objects) == 1 && objects[0]->type->objType == EDTYPE_TRACKER)
	{
		GroupTracker *tracker = trackerFromTrackerHandle(objects[0]->obj);
		GroupDef *def = tracker ? tracker->def : NULL;

		if (def)
			groupDefFixupMessageKey(&pDispMsg->pEditorCopy->pcMessageKey, def, data->param->source_key, &data->it);
	}
	
	if (!pDispMsg->pEditorCopy->pcScope || !pDispMsg->pEditorCopy->pcScope[ 0 ]) {
		pDispMsg->pEditorCopy->pcScope = allocAddString( data->param->source_key );
	}

	eaDestroy(&objects);

	++data->it;
}

static void wleAEGameActionMessageFixup(WorldGameActionBlock *pActionBlock, WleAEParamGameAction *param)
{
	GameActionMessageFixupData data;
	data.it = 0;
	data.param = param;
	langForEachDisplayMessage(parse_WorldGameActionBlock, pActionBlock, wleAEGameActionMessageFixup1, &data);
}

static void wleAEGameActionSpecifiedChanged(UIRTNode *node, WleAEParamGameAction *param)
{
	assert(param->can_unspecify);
	if (param->action_block)
	{
		StructDestroy(parse_WorldGameActionBlock, param->action_block);
		param->action_block = NULL;
	}
	if (param->is_specified)
		param->action_block = StructCreate(parse_WorldGameActionBlock);
	wleAEGameActionChanged(param);
}


//handle messages in the WorldGameActionBlock:
void wleAEFixupGameActionMessageKey(WorldGameActionBlock* block, GroupDef* def, const char* pcMessageScope)
{
	int i;
	langMakeEditorCopy( parse_WorldGameActionBlock, block, true );
	for (i=0; i<eaSize(&block->eaActions); i++)
	{
		WorldGameActionProperties *action = block->eaActions[i];
		DisplayMessage *pDispMsg = NULL;
		if (action->eActionType == WorldGameActionType_SendFloaterMsg && action->pSendFloaterProperties)
		{
			pDispMsg = &action->pSendFloaterProperties->floaterMsg;
		}
		else if (action->eActionType == WorldGameActionType_SendNotification && action->pSendNotificationProperties)
		{
			pDispMsg = &action->pSendNotificationProperties->notifyMsg;
		}
		else
		{
			continue;
		}
		groupDefFixupMessageKey( &pDispMsg->pEditorCopy->pcMessageKey, def, pcMessageScope, &i );
		if( !pDispMsg->pEditorCopy->pcScope || !pDispMsg->pEditorCopy->pcScope[ 0 ]) {
			pDispMsg->pEditorCopy->pcScope = allocAddString( pcMessageScope );
		}
	}

}

// main
void wleAEGameActionUpdate(WleAEParamGameAction *param)
{
	EditorObject **objects = NULL;
	WorldGameActionBlock *all_block = NULL, *next_block = NULL;
	bool all_spec = false, next_spec = false;
	int i;

	param->diff = param->spec_diff = false;
	StructDestroySafe(parse_WorldGameActionBlock, &param->action_block);
	StructDestroySafe(parse_WorldGameActionBlock, &param->action_block_temp);

	wleAEGetSelectedObjects(&objects);
	for (i = 0; i < eaSize(&objects); i++)
	{
		EditorObject *obj = objects[i];
		if (param->update_func)
		{
			param->update_func(param, param->update_data, obj);
			next_block = param->action_block;
			next_spec = param->is_specified;
		}

		if (!i)
		{
			if (next_block)
				all_block = StructClone(parse_WorldGameActionBlock, next_block);
			all_spec = next_spec;
		}
		else
		{
			if (!param->diff && !gameactionblock_Compare(all_block, next_block))
				param->diff = true;
			if (!param->spec_diff && all_spec != next_spec)
				param->spec_diff = true;
		}

		if (next_block)
			StructDestroy(parse_WorldGameActionBlock, next_block);
		if (param->spec_diff && param->diff)
			break;
	}

	param->is_specified = all_spec;
	if (param->spec_diff && param->can_unspecify)
	{
		param->action_block = NULL;
		param->is_specified = false;
		param->diff = true;
	}
	else if (param->diff)
		param->action_block = NULL;
	else if (all_block)
	{
		if (all_block && eaSize(&all_block->eaActions))
			param->action_block = StructClone(parse_WorldGameActionBlock, all_block);
		else
			param->action_block = NULL;
	}

	if (all_block)
		StructDestroy(parse_WorldGameActionBlock, all_block);

	// Store copy of data into the temp value
	param->action_block_temp = StructClone(parse_WorldGameActionBlock, param->action_block);

	eaDestroy(&objects);
}

void wleAEGameActionAddWidgetEx(UIRebuildableTreeNode *auto_widget_node, const char *name, const char *tooltip, const char *param_name, WleAEParamGameAction *param)
{
	UIAutoWidgetParams params = {0};
	char full_param_name[1024];
	bool did_newline = false;

	params.NoLabel = true;
	params.alignTo = param->left_pad;
	params.disabled = param->disabled;

	if (param->can_unspecify)
	{
		sprintf(full_param_name, "%s|%d|is_specified", param_name, param->index);
		ui_AutoWidgetAddKeyed(auto_widget_node, parse_WleAEParamGameAction, "is_specified", full_param_name, param, true, wleAEGameActionSpecifiedChanged, param, &params, tooltip);
		did_newline = true;
		params.alignTo += 20;

		// skin specified check box
		if (auto_widget_node->children)
		{
			UIWidget *check_button = auto_widget_node->children[eaSize(&auto_widget_node->children) - 1]->widget1;
			wleSkinDiffWidget(check_button, param->spec_diff);
		}
	}

	sprintf(full_param_name, "%s|%d|label", param_name, param->index);
	ui_RebuildableTreeAddLabelKeyed(auto_widget_node, name, full_param_name, &params, !did_newline);
	params.alignTo += ((param->entry_align ? param->entry_align : 120) + (!param->can_unspecify ? 20 : 0));
	sprintf(full_param_name, "%s|%d|strvalue:", param_name, param->index);

	param->pActionButton = ui_GameActionEditButtonCreate((char*)zmapInfoGetPublicName(NULL), param->action_block, param->action_block_temp, wleAEGameActionValueChanged, wleAEGameActionMessageFixup, param);
	if(tooltip){
		ui_WidgetSetTooltipString(UI_WIDGET(param->pActionButton), tooltip);
	}
	if ((param->entry_width > 0.0) && (param->entry_width <= 1.0))
		ui_WidgetSetWidthEx(UI_WIDGET(param->pActionButton), param->entry_width, UIUnitPercentage);
	else
		ui_WidgetSetWidth(UI_WIDGET(param->pActionButton), param->entry_width);

	ui_RebuildableTreeAddWidget(auto_widget_node, UI_WIDGET(param->pActionButton), NULL, false, full_param_name, &params);

	// skin widget as necessary
	wleSkinDiffWidget(UI_WIDGET(param->pActionButton), param->spec_diff || param->diff);
}

/********************
* WORLD VARIABLE DEF
********************/
static void wleAEWorldVariableDefChanged(WleAEParamWorldVariableDef *param)
{
	EditorObject **objects = NULL;
	int i;

	// make sure the eType is correct, if possible
	if (!param->no_name) {
		WorldVariableDef* varDef = NULL;
		if(param->dest_map_name) {
			varDef = zmapInfoGetVariableDefByName(zmapInfoGetByPublicName(param->dest_map_name), param->var_def.pcName);
		} else if(param->eaDefList) {
			bool found = false;
			for(i = 0; i < eaSize(&param->eaDefList) && !found; i++) {
				if(param->eaDefList[i]->pcName && !stricmp(param->eaDefList[i]->pcName, param->var_def.pcName)) {
					varDef = param->eaDefList[i];
					found = true;
				}
			}
		}
		if (varDef)
			param->var_def.eType = varDef->eType;
	}

	if (param->var_def.pSpecificValue)
		param->var_def.pSpecificValue->eType = param->var_def.eType;

	wleAEGetSelectedObjects(&objects);
	wleAESetApplyingData(true);

	EditUndoBeginGroup(edObjGetUndoStack());
	if (param->apply_func)
	{
		param->apply_func(param, param->apply_data, objects);
	}
	EditUndoEndGroup(edObjGetUndoStack());
	wleAESetApplyingData(false);
	wleAERefresh();
	eaDestroy(&objects);
}

static void wleAEWorldVariableDefSpecifiedChanged(UIRTNode *node, WleAEParamWorldVariableDef *param)
{
	assert(param->can_unspecify);
	wleAEWorldVariableDefChanged(param);
}

void wleAEWorldVariableDefNameChanged(UITextEntry* widget, WleAEParamWorldVariableDef* param)
{
	if (stricmp(param->var_def.pcName, ui_TextEntryGetText( widget )) == 0)
		return;
	
	param->var_def.pcName = allocAddString(ui_TextEntryGetText( widget ));
	wleAEWorldVariableDefChanged(param);
}

void wleAEWorldVariableDefInitFromChanged(UIComboBox* widget, int val, WleAEParamWorldVariableDef* param)
{
	if (param->var_def.eDefaultType == val)
		return;
	
	param->var_def.eDefaultType = val;
	wleAEWorldVariableDefChanged(param);
}

void wleAEWorldVariableDefTypeChanged(UIComboBox* widget, int val, WleAEParamWorldVariableDef* param)
{
	if (param->var_def.eType == val)
		return;

	param->var_def.eType = val;
	if(param->var_def.pSpecificValue) {
		param->var_def.pSpecificValue->eType = val;
	}
	wleAEWorldVariableDefChanged(param);
}


void wleAEWorldVariableDefIntUpdate(WleAEParamInt* int_param, WleAEParamWorldVariableDef* param, EditorObject *obj)
{
	bool changed = false;

	// If value is diff NULL out the field and mark it as diff
	if(param->var_value_diff) {
		int_param->intvalue = 0;
		int_param->diff = true;
		return;
	}

	if (!param->var_def.pSpecificValue)
		return;
	
	int_param->intvalue = param->var_def.pSpecificValue->iIntVal;
}

void wleAEWorldVariableDefIntApply(WleAEParamInt* int_param, WleAEParamWorldVariableDef* param, EditorObject **objs)
{
	bool changed = false;

	if (param->var_def.pSpecificValue && param->var_def.pSpecificValue->iIntVal == int_param->intvalue)
		return;
	
	if (!param->var_def.pSpecificValue)
		param->var_def.pSpecificValue = StructCreate( parse_WorldVariable );
	param->var_def.pSpecificValue->iIntVal = int_param->intvalue;
	wleAEWorldVariableDefChanged(param);
}

void wleAEWorldVariableDefFloatUpdate(WleAEParamFloat* float_param, WleAEParamWorldVariableDef* param, EditorObject *obj)
{
	bool changed = false;

	// If value is diff NULL out the field and mark it as diff
	if(param->var_value_diff) {
		float_param->floatvalue = 0.f;
		float_param->diff = true;
		return;
	}

	if (!param->var_def.pSpecificValue)
		return;
	
	float_param->floatvalue = param->var_def.pSpecificValue->fFloatVal;
}

void wleAEWorldVariableDefFloatApply(WleAEParamFloat* float_param, WleAEParamWorldVariableDef* param, EditorObject **objs)
{
	bool changed = false;

	if (param->var_def.pSpecificValue && param->var_def.pSpecificValue->fFloatVal == float_param->floatvalue)
		return;
	
	if (!param->var_def.pSpecificValue)
		param->var_def.pSpecificValue = StructCreate( parse_WorldVariable );
	param->var_def.pSpecificValue->fFloatVal = float_param->floatvalue;
	wleAEWorldVariableDefChanged(param);
}

void wleAEWorldVariableDefStringUpdate(WleAEParamText* string_param, WleAEParamWorldVariableDef* param, EditorObject *obj)
{
	bool changed = false;

	// If value is diff NULL out the field and mark it as diff
	if(param->var_value_diff) {
		string_param->stringvalue = NULL;
		string_param->diff = true;
		return;
	}

	if (!param->var_def.pSpecificValue)
		return;

	if(EMPTY_TO_NULL(param->var_def.pSpecificValue->pcStringVal))
		string_param->stringvalue = StructAllocString( param->var_def.pSpecificValue->pcStringVal );
	else
		string_param->stringvalue = NULL;
}

void wleAEWorldVariableDefStringApply(WleAEParamText* string_param, WleAEParamWorldVariableDef* param, EditorObject **objs)
{
	bool changed = false;

	if (param->var_def.pSpecificValue && stricmp(param->var_def.pSpecificValue->pcStringVal, string_param->stringvalue) == 0)
		return;
	
	if (!param->var_def.pSpecificValue)
		param->var_def.pSpecificValue = StructCreate( parse_WorldVariable );
	StructFreeStringSafe(&param->var_def.pSpecificValue->pcStringVal);
	param->var_def.pSpecificValue->pcStringVal = StructAllocString(string_param->stringvalue);
	wleAEWorldVariableDefChanged(param);
}

void wleAEWorldVariableDefMessageUpdate(WleAEParamMessage* message_param, WleAEParamWorldVariableDef* param, EditorObject *obj)
{
	GroupTracker *tracker = NULL;
	GroupDef *def = NULL;
	DisplayMessage* display_name_msg = NULL;

	// If value is diff NULL out the field and mark it as diff
	if(param->var_value_diff) {
		StructReset(parse_Message, &message_param->message);
		message_param->default_str_diff = true;
		message_param->desc_diff = true;
		message_param->key_diff = true;
		message_param->scope_diff = true;
		return;
	}

	// If no message present, then do nothing
	if(!param->var_def.pSpecificValue) {
		StructReset(parse_Message, &message_param->message);
		return;
	}

	// Get the tracker
	if(param->tracker_func)
	{
		TrackerHandle* tracker_handle = NULL;
		param->tracker_func(obj, &tracker_handle);
		if(tracker_handle)
			tracker = trackerFromTrackerHandle(tracker_handle);
	} else if (obj->type->objType == EDTYPE_TRACKER) {
		tracker = trackerFromTrackerHandle(obj->obj);
	} 
		
	def	= tracker ? tracker->def : NULL;

	if(def) {
		// Build the message
		char* estrScope = NULL;
		estrStackCreate(&estrScope);
		estrPrintf(&estrScope, "%s_%s", message_param->source_key, param->var_def.pcName);
		display_name_msg = &param->var_def.pSpecificValue->messageVal;
		langMakeEditorCopy(parse_DisplayMessage, display_name_msg, true);
		display_name_msg->pEditorCopy->pcMessageKey = groupDefMessageKeyRaw(def->filename, def->name_str, estrScope, NULL, false);
		estrDestroy(&estrScope);
		// Set scope if not already set
		if( (!display_name_msg->pEditorCopy->pcScope || !display_name_msg->pEditorCopy->pcScope[ 0 ]) && message_param->source_key) {
			display_name_msg->pEditorCopy->pcScope = allocAddString(groupDefMessageKeyRaw(def->filename, message_param->source_key, def->name_str, NULL, false));
		}
		StructCopyAll(parse_Message, display_name_msg->pEditorCopy, &message_param->message);
		return;
	}
	StructReset(parse_Message, &message_param->message);
}

void wleAEWorldVariableDefMessageApply(WleAEParamMessage* message_param, WleAEParamWorldVariableDef* param, EditorObject **objs)
{
	DisplayMessage* display_name_msg = NULL;
	GroupTracker *tracker = NULL;
	GroupDef *def = NULL;
	int i;

	// If the same message already exists, do nothing
	if(param->var_def.pSpecificValue && StructCompare(parse_Message, param->var_def.pSpecificValue->messageVal.pEditorCopy, &message_param->message, 0, 0, 0) == 0)
		return;

	if(!param->var_def.pSpecificValue)
		param->var_def.pSpecificValue = StructCreate(parse_WorldVariable);		

	for (i = 0; i < eaSize(&objs); i++)
	{
		// Get the tracker
		if(param->tracker_func)
		{
			TrackerHandle* tracker_handle = NULL;
			param->tracker_func(objs[i], &tracker_handle);
			if(tracker_handle)
			{
				tracker = trackerFromTrackerHandle(tracker_handle);
			}
		}
		else if(objs[i]->type->objType == EDTYPE_TRACKER)
		{
			tracker = trackerFromTrackerHandle(objs[i]->obj);
		} 
	
		def = tracker ? tracker->def : NULL;

		if(def)
		{
			// Build the message
			char* estrScope = NULL;
			estrStackCreate(&estrScope);
			estrPrintf(&estrScope, "%s_%s", message_param->source_key, param->var_def.pcName);
			display_name_msg = &param->var_def.pSpecificValue->messageVal;
			langMakeEditorCopy(parse_DisplayMessage, display_name_msg, true);
			StructCopyAll(parse_Message, &message_param->message, display_name_msg->pEditorCopy);
			display_name_msg->pEditorCopy->pcMessageKey = groupDefMessageKeyRaw(def->filename, def->name_str, estrScope, NULL, false);
			estrDestroy(&estrScope);
			// Set scope if not already set
			if( !display_name_msg->pEditorCopy->pcScope || !display_name_msg->pEditorCopy->pcScope[ 0 ])
			{
				display_name_msg->pEditorCopy->pcScope = allocAddString(groupDefMessageKeyRaw(def->filename, message_param->source_key, def->name_str, NULL, false));
			}
		}
	}

	wleAEWorldVariableDefChanged(param);
}

void wleAEWorldVariableDefAnimationUpdate(WleAEParamDictionary* animation_param, WleAEParamWorldVariableDef* param, EditorObject *obj)
{
	bool changed = false;

	// If value is diff NULL out the field and mark it as diff
	if(param->var_value_diff) {
		animation_param->refvalue = NULL;
		animation_param->diff = true;
		return;
	}

	if (!param->var_def.pSpecificValue)
		return;

 	if(EMPTY_TO_NULL(param->var_def.pSpecificValue->pcStringVal))
 		animation_param->refvalue = StructAllocString( param->var_def.pSpecificValue->pcStringVal );
	else
		animation_param->refvalue = NULL;
}

void wleAEWorldVariableDefAnimationApply(WleAEParamDictionary* animation_param, WleAEParamWorldVariableDef* param, EditorObject **objs)
{
	bool changed = false;

	if (param->var_def.pSpecificValue && stricmp(param->var_def.pSpecificValue->pcStringVal, animation_param->refvalue) == 0)
		return;
	
	if (!param->var_def.pSpecificValue)
		param->var_def.pSpecificValue = StructCreate( parse_WorldVariable );
	StructFreeStringSafe(&param->var_def.pSpecificValue->pcStringVal);
	param->var_def.pSpecificValue->pcStringVal = StructAllocString(animation_param->refvalue);
	wleAEWorldVariableDefChanged(param);
}

void wleAEWorldVariableDefCritterDefUpdate(WleAEParamDictionary* critterdef_param, WleAEParamWorldVariableDef* param, EditorObject *obj)
{
	bool changed = false;

	// If value is diff NULL out the field and mark it as diff
	if(param->var_value_diff) {
		critterdef_param->refvalue = NULL;
		critterdef_param->diff = true;
		return;
	}

	if (!param->var_def.pSpecificValue)
		return;

	if(GET_REF(param->var_def.pSpecificValue->hCritterDef))
		critterdef_param->refvalue = StructAllocString( REF_STRING_FROM_HANDLE( param->var_def.pSpecificValue->hCritterDef ));
	else
		critterdef_param->refvalue = NULL;
}

void wleAEWorldVariableDefCritterDefApply(WleAEParamDictionary* critterdef_param, WleAEParamWorldVariableDef* param, EditorObject **objs)
{
	bool changed = false;

	if (param->var_def.pSpecificValue && stricmp(REF_STRING_FROM_HANDLE(param->var_def.pSpecificValue->hCritterDef), critterdef_param->refvalue) == 0)
		return;
	
	if (!param->var_def.pSpecificValue)
		param->var_def.pSpecificValue = StructCreate( parse_WorldVariable );
	SET_HANDLE_FROM_STRING("CritterDef", critterdef_param->refvalue, param->var_def.pSpecificValue->hCritterDef);
	wleAEWorldVariableDefChanged(param);
}

void wleAEWorldVariableDefCritterGroupUpdate(WleAEParamDictionary* crittergroup_param, WleAEParamWorldVariableDef* param, EditorObject *obj)
{
	bool changed = false;

	// If value is diff NULL out the field and mark it as diff
	if(param->var_value_diff) {
		crittergroup_param->refvalue = NULL;
		crittergroup_param->diff = true;
		return;
	}

	if (!param->var_def.pSpecificValue)
		return;

	if(GET_REF(param->var_def.pSpecificValue->hCritterGroup))
		crittergroup_param->refvalue = StructAllocString( REF_STRING_FROM_HANDLE( param->var_def.pSpecificValue->hCritterGroup ));
	else
		crittergroup_param->refvalue = NULL;
}

void wleAEWorldVariableDefCritterGroupApply(WleAEParamDictionary* crittergroup_param, WleAEParamWorldVariableDef* param, EditorObject **objs)
{
	bool changed = false;

	if (param->var_def.pSpecificValue && stricmp(REF_STRING_FROM_HANDLE(param->var_def.pSpecificValue->hCritterGroup), crittergroup_param->refvalue) == 0)
		return;
	
	if (!param->var_def.pSpecificValue)
		param->var_def.pSpecificValue = StructCreate( parse_WorldVariable );
	SET_HANDLE_FROM_STRING("CritterGroup", crittergroup_param->refvalue, param->var_def.pSpecificValue->hCritterGroup);
	wleAEWorldVariableDefChanged(param);
}

void wleAEWorldVariableDefMapPointZoneUpdate(WleAEParamDictionary* zone_param, WleAEParamWorldVariableDef* param, EditorObject *obj)
{
	bool changed = false;

	// If value is diff NULL out the field and mark it as diff
	if(param->var_value_diff) {
		zone_param->refvalue = NULL;
		zone_param->diff = true;
		return;
	}

	if (!param->var_def.pSpecificValue)
		return;

	if(EMPTY_TO_NULL(param->var_def.pSpecificValue->pcZoneMap))
		zone_param->refvalue = StructAllocString( param->var_def.pSpecificValue->pcZoneMap );
	else
		zone_param->refvalue = NULL;
}

void wleAEWorldVariableDefMapPointZoneApply(WleAEParamDictionary* zone_param, WleAEParamWorldVariableDef* param, EditorObject **objs)
{
	bool changed = false;

	if (param->var_def.pSpecificValue && stricmp(param->var_def.pSpecificValue->pcZoneMap, zone_param->refvalue) == 0)
		return;
	
	if (!param->var_def.pSpecificValue)
		param->var_def.pSpecificValue = StructCreate( parse_WorldVariable );
	StructFreeStringSafe(&param->var_def.pSpecificValue->pcZoneMap);
	param->var_def.pSpecificValue->pcZoneMap = StructAllocString(zone_param->refvalue);
	wleAEWorldVariableDefChanged(param);
}

void wleAEWorldVariableDefMapPointSpawnUpdate(WleAEParamText* spawn_param, WleAEParamWorldVariableDef* param, EditorObject *obj)
{
	bool changed = false;

	// If value is diff NULL out the field and mark it as diff
	if(param->var_value_diff) {
		spawn_param->stringvalue = NULL;
		spawn_param->diff = true;
		return;
	}

	if (!param->var_def.pSpecificValue)
		return;

	if(EMPTY_TO_NULL(param->var_def.pSpecificValue->pcStringVal))
		spawn_param->stringvalue = StructAllocString( param->var_def.pSpecificValue->pcStringVal );
	else
		spawn_param->stringvalue = NULL;
}

void wleAEWorldVariableDefMapPointSpawnApply(WleAEParamText* spawn_param, WleAEParamWorldVariableDef* param, EditorObject **objs)
{
	bool changed = false;

	if (param->var_def.pSpecificValue && stricmp(param->var_def.pSpecificValue->pcZoneMap, spawn_param->stringvalue) == 0)
		return;
	
	if (!param->var_def.pSpecificValue)
		param->var_def.pSpecificValue = StructCreate( parse_WorldVariable );
	StructFreeStringSafe(&param->var_def.pSpecificValue->pcStringVal);
	param->var_def.pSpecificValue->pcStringVal = StructAllocString(spawn_param->stringvalue);
	wleAEWorldVariableDefChanged(param);
}

void wleAEWorldVariableDefItemDefUpdate(WleAEParamDictionary* itemdef_param, WleAEParamWorldVariableDef* param, EditorObject *obj)
{
	bool changed = false;

	// If value is diff NULL out the field and mark it as diff
	if(param->var_value_diff) {
		itemdef_param->refvalue = NULL;
		itemdef_param->diff = true;
		return;
	}

	if (!param->var_def.pSpecificValue)
		return;

	if(EMPTY_TO_NULL(param->var_def.pSpecificValue->pcStringVal))
		itemdef_param->refvalue = StructAllocString( param->var_def.pSpecificValue->pcStringVal );
	else
		itemdef_param->refvalue = NULL;
}

void wleAEWorldVariableDefItemDefUpdateApply(WleAEParamDictionary* itemdef_param, WleAEParamWorldVariableDef* param, EditorObject **objs)
{
	wleAEWorldVariableDefItemDefUpdate(itemdef_param, param, (eaSize(&objs) > 0) ? objs[0] : NULL);
}

void wleAEWorldVariableDefItemDefApply(WleAEParamDictionary* itemdef_param, WleAEParamWorldVariableDef* param, EditorObject **objs)
{
	bool changed = false;

	if (param->var_def.pSpecificValue && stricmp(param->var_def.pSpecificValue->pcStringVal, itemdef_param->refvalue) == 0)
		return;
	
	if (!param->var_def.pSpecificValue)
		param->var_def.pSpecificValue = StructCreate( parse_WorldVariable );
	StructFreeStringSafe(&param->var_def.pSpecificValue->pcStringVal);
	param->var_def.pSpecificValue->pcStringVal = StructAllocString(itemdef_param->refvalue);
	wleAEWorldVariableDefChanged(param);
}

void wleAEWorldVariableDefChoiceTableChanged(UITextEntry* widget, WleAEParamWorldVariableDef* param)
{
	if(stricmp(REF_STRING_FROM_HANDLE(param->var_def.choice_table), ui_TextEntryGetText(widget)) == 0)
		return;
	
	SET_HANDLE_FROM_STRING(g_hChoiceTableDict, ui_TextEntryGetText(widget), param->var_def.choice_table);
	wleAEWorldVariableDefChanged(param);
}

void wleAEWorldVariableDefChoiceNameChanged(UITextEntry* widget, WleAEParamWorldVariableDef* param)
{
	if(stricmp(param->var_def.choice_name, ui_TextEntryGetText(widget)) == 0)
		return;
	
	StructFreeStringSafe(&param->var_def.choice_name);
	param->var_def.choice_name = StructAllocString( ui_TextEntryGetText(widget));
	wleAEWorldVariableDefChanged(param);
}

void wleAEWorldVariableDefChoiceIndexChanged(UISpinnerEntry* widget, WleAEParamWorldVariableDef* param)
{
	int val = ui_SpinnerEntryGetValue(widget);
	if(param->var_def.choice_index == val)
		return;

	param->var_def.choice_index = val;
	wleAEWorldVariableDefChanged(param);
}

void wleAEWorldVariableDefMapVariableChanged(UITextEntry* widget, WleAEParamWorldVariableDef* param)
{
	if(stricmp(param->var_def.map_variable, ui_TextEntryGetText(widget)) == 0)
		return;

	StructFreeStringSafe(&param->var_def.map_variable);
	param->var_def.map_variable = StructAllocString( ui_TextEntryGetText(widget));
	wleAEWorldVariableDefChanged(param);
}

void wleAEWorldVariableDefMissionChanged(UITextEntry* widget, WleAEParamWorldVariableDef* param)
{
	const char *missionRefString = param->var_def.mission_refstring;

	if (stricmp(missionRefString, ui_TextEntryGetText(widget)) == 0)
		return;

	StructFreeStringSafe(&param->var_def.mission_refstring);
	param->var_def.mission_refstring = StructAllocString(ui_TextEntryGetText(widget));
	wleAEWorldVariableDefChanged(param);
}

void wleAEWorldVariableDefMissionVariableChanged(UITextEntry* widget, WleAEParamWorldVariableDef* param)
{
	if (stricmp(param->var_def.mission_variable, ui_TextEntryGetText(widget)) == 0)
		return;

	StructFreeStringSafe(&param->var_def.mission_variable);
	param->var_def.mission_variable = StructAllocString(ui_TextEntryGetText(widget));
	wleAEWorldVariableDefChanged(param);
}

void wleAEWorldVariableDefExpressionChanged(UIExpressionEntry *widget, WleAEParamWorldVariableDef* param)
{
	const char *exprString = exprGetCompleteString(param->var_def.pExpression);

	if (stricmp(exprString, ui_ExpressionEntryGetText(widget)) == 0)
		return;

	exprDestroy(param->var_def.pExpression);
	param->var_def.pExpression = exprCreateFromString(ui_ExpressionEntryGetText(widget), zmapGetFilename(NULL));
	wleAEWorldVariableDefChanged(param);
}

void wleAEWorldVariableDefUpdate(WleAEParamWorldVariableDef *param)
{
	MissionDef *missionDef = NULL;
	EditorObject **objects = NULL;
	WorldVariableDef* all_var_def = NULL, *next_var_def = NULL;
	bool all_spec = false, next_spec = false;
	int i;

	param->var_value_diff = param->var_init_from_diff = param->var_name_diff = param->spec_diff = param->var_type_diff = false;
	StructReset(parse_WorldVariableDef, &param->var_def);

	wleAEGetSelectedObjects(&objects);
	for (i = 0; i < eaSize(&objects); i++)
	{
		EditorObject *obj = objects[i];
		if (param->update_func)
		{
			param->update_func(param, param->update_data, obj);
			next_var_def = StructClone(parse_WorldVariableDef, &param->var_def);
			next_spec = param->is_specified;
		}

		if (!i)
		{
			if (next_var_def)
				all_var_def = StructClone(parse_WorldVariableDef, next_var_def);
			all_spec = next_spec;
		}
		else
		{
			if ((all_var_def == NULL) != (next_var_def == NULL))
			{
				param->var_name_diff = true;
				param->var_init_from_diff = true;
				param->var_value_diff = true;
				param->var_type_diff = true;
			}
			else if (all_var_def == NULL)
				; // do nothing -- both are NULL
			else
			{
				if (!param->var_name_diff && stricmp(SAFE_MEMBER(all_var_def, pcName), SAFE_MEMBER(next_var_def, pcName)) != 0)
					param->var_name_diff = true;
				if (!param->var_init_from_diff && all_var_def->eDefaultType != next_var_def->eDefaultType)
					param->var_init_from_diff = true;
				if (!param->var_type_diff && all_var_def->eType != next_var_def->eType)
					param->var_type_diff = true;
				if (!param->var_value_diff)
				{
					switch( all_var_def->eDefaultType ) {
						case WVARDEF_SPECIFY_DEFAULT:
							if (!worldVariableEquals( all_var_def->pSpecificValue, next_var_def->pSpecificValue ))
								param->var_value_diff = true;
							
						xcase WVARDEF_CHOICE_TABLE:
							if (  stricmp(REF_STRING_FROM_HANDLE(all_var_def->choice_table), REF_STRING_FROM_HANDLE(next_var_def->choice_table)) != 0
								  || stricmp(all_var_def->choice_name, next_var_def->choice_name) != 0
								  || all_var_def->choice_index != next_var_def->choice_index)
								param->var_value_diff = true;
							
						xcase WVARDEF_MAP_VARIABLE:
							if (stricmp(all_var_def->map_variable, next_var_def->map_variable) != 0)
								param->var_value_diff = true;

						xcase WVARDEF_MISSION_VARIABLE:
							if (stricmp(all_var_def->mission_refstring, next_var_def->mission_refstring) != 0
								|| stricmp(all_var_def->mission_variable, next_var_def->mission_variable) != 0)
								param->var_value_diff = true;
						xcase WVARDEF_EXPRESSION:
							if (exprCompare(all_var_def->pExpression, next_var_def->pExpression) != 0)
								param->var_value_diff = true;

						xcase WVARDEF_ACTIVITY_VARIABLE:
							if (stricmp(all_var_def->activity_name, next_var_def->activity_name) != 0
								|| stricmp(all_var_def->activity_variable_name, next_var_def->activity_variable_name) != 0
								|| !worldVariableEquals( all_var_def->pSpecificValue, next_var_def->pSpecificValue ))
								param->var_value_diff = true;
					}
				}
			}
			if (!param->spec_diff && all_spec != next_spec)
				param->spec_diff = true;
		}

		if (next_var_def) {
			StructDestroy(parse_WorldVariableDef, next_var_def);
			StructReset(parse_WorldVariableDef, &param->var_def);
		}
		if (param->spec_diff && param->var_name_diff && param->var_value_diff)
			break;
	}
	eaDestroy(&objects);

	param->is_specified = all_spec;
	if ((!param->spec_diff || !param->no_name) && all_var_def)
	{
		if (all_var_def)
			StructCopyAll(parse_WorldVariableDef, all_var_def, &param->var_def);
	}
	if (param->spec_diff && param->can_unspecify)
	{
		param->is_specified = false;
		if(param->no_name) {
			param->var_value_diff = true;
			param->var_init_from_diff = true;
			param->var_name_diff = true;
			param->var_type_diff = true;
		}
	}
	if (param->var_name_diff)
		param->var_def.pcName = NULL;
	if (param->var_init_from_diff)
		param->var_def.eDefaultType = -1;
	if (param->var_value_diff)
	{
		StructDestroySafe(parse_WorldVariable, &param->var_def.pSpecificValue);
		REMOVE_HANDLE(param->var_def.choice_table);
		StructFreeStringSafe(&param->var_def.choice_name);
		param->var_def.choice_index = 1;
		StructFreeStringSafe(&param->var_def.map_variable);
		StructFreeStringSafe(&param->var_def.mission_refstring);
		StructFreeStringSafe(&param->var_def.mission_variable);
		exprDestroy(param->var_def.pExpression);
		param->var_def.pExpression = NULL;
	}
	if (param->var_type_diff)
	{
		param->var_def.eType = -1;
	}
	
	StructDestroySafe(parse_WorldVariableDef, &all_var_def);

	if (param->var_def.eDefaultType == WVARDEF_SPECIFY_DEFAULT) {
		// VARIABLE_TYPES: Add code below if add to the available variable types
		switch(param->var_def.eType) {
			case WVAR_INT:
				param->int_param.update_func = wleAEWorldVariableDefIntUpdate;
				param->int_param.update_data = param;
				param->int_param.apply_func = wleAEWorldVariableDefIntApply;
				param->int_param.apply_data = param;
				wleAEIntUpdate(&param->int_param);

			xcase WVAR_FLOAT:
				param->float_param.update_func = wleAEWorldVariableDefFloatUpdate;
				param->float_param.update_data = param;
				param->float_param.apply_func = wleAEWorldVariableDefFloatApply;
				param->float_param.apply_data = param;
				wleAEFloatUpdate(&param->float_param);

			xcase WVAR_STRING: case WVAR_LOCATION_STRING:
				param->string_param.update_func = wleAEWorldVariableDefStringUpdate;
				param->string_param.update_data = param;
				param->string_param.apply_func = wleAEWorldVariableDefStringApply;
				param->string_param.apply_data = param;
				wleAETextUpdate(&param->string_param);

			xcase WVAR_MESSAGE:
				param->message_param.update_func = wleAEWorldVariableDefMessageUpdate;
				param->message_param.update_data = param;
				param->message_param.apply_func = wleAEWorldVariableDefMessageApply;
				param->message_param.apply_data = param;
				if(param->scope) {
					param->message_param.source_key = strdup(param->scope);
				}
				wleAEMessageUpdate(&param->message_param);

			xcase WVAR_ANIMATION:
				param->dict_param.update_func = wleAEWorldVariableDefAnimationUpdate;
				param->dict_param.update_data = param;
				param->dict_param.apply_func = wleAEWorldVariableDefAnimationApply;
				param->dict_param.apply_data = param;
				wleAEDictionaryUpdate(&param->dict_param);

			xcase WVAR_CRITTER_DEF:
				param->dict_param.update_func = wleAEWorldVariableDefCritterDefUpdate;
				param->dict_param.update_data = param;
				param->dict_param.apply_func = wleAEWorldVariableDefCritterDefApply;
				param->dict_param.apply_data = param;
				wleAEDictionaryUpdate(&param->dict_param);

			xcase WVAR_CRITTER_GROUP:
				param->dict_param.update_func = wleAEWorldVariableDefCritterGroupUpdate;
				param->dict_param.update_data = param;
				param->dict_param.apply_func = wleAEWorldVariableDefCritterGroupApply;
				param->dict_param.apply_data = param;
				wleAEDictionaryUpdate(&param->dict_param);

			xcase WVAR_MAP_POINT:
				param->dict_param.update_func = wleAEWorldVariableDefMapPointZoneUpdate;
				param->dict_param.update_data = param;
				param->dict_param.apply_func = wleAEWorldVariableDefMapPointZoneApply;
				param->dict_param.apply_data = param;
				
				param->string_param.update_func = wleAEWorldVariableDefMapPointSpawnUpdate;
				param->string_param.update_data = param;
				param->string_param.apply_func = wleAEWorldVariableDefMapPointSpawnApply;
				param->string_param.apply_data = param;
				
				wleAEDictionaryUpdate(&param->dict_param);
				wleAETextUpdate(&param->string_param);

			xcase WVAR_ITEM_DEF:
				param->dict_param.update_func = wleAEWorldVariableDefItemDefUpdate;
				param->dict_param.update_data = param;
				param->dict_param.apply_func = wleAEWorldVariableDefItemDefApply;
				param->dict_param.apply_data = param;
				wleAEDictionaryUpdate(&param->dict_param);

			xcase WVAR_MISSION_DEF:
				param->dict_param.update_func = wleAEWorldVariableDefItemDefUpdate;
				param->dict_param.update_data = param;
				param->dict_param.apply_func = wleAEWorldVariableDefItemDefUpdateApply;
				param->dict_param.apply_data = param;
				wleAEDictionaryUpdate(&param->dict_param);
		}
	}
	
	// update cached values
	eaDestroyEx(&param->source_map_variables, NULL);
	if (param->source_map_name)
		param->source_map_variables = zmapInfoGetVariableNames(zmapInfoGetByPublicName(param->source_map_name));
	
	eaDestroy(&param->dest_map_variables);
	if (param->dest_map_name) {
		param->dest_map_variables = zmapInfoGetVariableNames(zmapInfoGetByPublicName(param->dest_map_name));
	} else if(param->eaDefList) {
		eaCreate(&param->dest_map_variables);
		for(i = 0; i < eaSize(&param->eaDefList); i++) {
			if(param->eaDefList[i]->pcName)
				eaPush(&param->dest_map_variables, param->eaDefList[i]->pcName);
		}
	}

	eaDestroy(&param->mission_variables);
	missionDef = param->var_def.mission_refstring ? missiondef_DefFromRefString(param->var_def.mission_refstring) : NULL;
	if (param->var_def.eDefaultType == WVARDEF_MISSION_VARIABLE && missionDef)
	{
		MissionDef *rootMissionDef = GET_REF(missionDef->parentDef);
		for (i = 0; i < eaSize(&missionDef->eaVariableDefs); i++)
			eaPush(&param->mission_variables, missionDef->eaVariableDefs[i]->pcName);
		if (rootMissionDef)
		{
			for (i = 0; i < eaSize(&rootMissionDef->eaVariableDefs); i++)
				eaPush(&param->mission_variables, rootMissionDef->eaVariableDefs[i]->pcName);
		}
	}
	
	eaDestroyEx(&param->choice_table_names, NULL);
	if (param->var_def.eDefaultType == WVARDEF_CHOICE_TABLE && GET_REF(param->var_def.choice_table)) {
		ChoiceTable *choiceTable = GET_REF(param->var_def.choice_table);
		param->choice_table_names = choice_ListNames( REF_STRING_FROM_HANDLE( param->var_def.choice_table ));
		param->choice_table_index_max = choice_TimedRandomValuesPerInterval(choiceTable);
	}

	eaDestroyEx(&param->map_point_spawn_points, NULL);
	if (param->var_def.eDefaultType == WVARDEF_SPECIFY_DEFAULT && param->var_def.eType == WVAR_MAP_POINT) {
		param->map_point_spawn_points = NULL;	  //< TODO: figure out how to get a list of spawn points
	}
}

void wleAEWorldVariableDefAddWidgetEx(UIRebuildableTreeNode *auto_widget_node, const char *name, const char *tooltip, const char *inheritedText, const char *param_name, WleAEParamWorldVariableDef *param)
{
	UIAutoWidgetParams params = {0};
	char full_param_name[1024];
	UIUnitType widthType = (param->entry_width <= 1.0 ? UIUnitPercentage : UIUnitFixed);

	params.NoLabel = true;
	params.alignTo = param->left_pad;
	params.disabled = param->disabled;

	if (param->can_unspecify)
	{
		sprintf(full_param_name, "%s|%d|is_specified", param_name, param->index);
		ui_AutoWidgetAddKeyed(auto_widget_node, parse_WleAEParamWorldVariableDef, "is_specified", full_param_name, param, true, wleAEWorldVariableDefSpecifiedChanged, param, &params, tooltip);

		params.alignTo += 20;

		// skin specified check box
		if (auto_widget_node->children)
		{
			UIWidget *check_button = auto_widget_node->children[eaSize(&auto_widget_node->children) - 1]->widget1;
			wleSkinDiffWidget(check_button, param->spec_diff);
		}
	}

	{
		int labelAlign = params.alignTo;
		int labelAlignIndent = labelAlign + 15;
		int entryAlign = params.alignTo + param->entry_align;
		int entryAlignIndent = entryAlign + 15;

		// add the widgets
		if (!param->no_name) {
			UITextEntry *pVarNameWidget;

			sprintf(full_param_name, "%s|%d|varname_label", param_name, param->index);
			params.alignTo = labelAlign;
			ui_RebuildableTreeAddLabelKeyed(auto_widget_node, name, full_param_name, &params, !param->can_unspecify);

			if(param->display_if_unspecified) {
				// Special case if display_if_unspecified is true
				// Will display the name as an uneditable label
				sprintf(full_param_name, "%s|%d|varname_value", param_name, param->index);
				params.alignTo = labelAlign;
				ui_RebuildableTreeAddLabelKeyed(auto_widget_node, param->var_def.pcName, full_param_name, &params, false);
				if(EMPTY_TO_NULL(inheritedText)) {
					sprintf(full_param_name, "%s|%d|inherited_label", param_name, param->index);
					params.alignTo = entryAlignIndent;  
					ui_RebuildableTreeAddLabelKeyed(auto_widget_node, inheritedText, full_param_name, &params, false);					
				}
			} else {
				sprintf(full_param_name, "%s|%d|varname:", param_name, param->index);
				params.alignTo = entryAlign;
				pVarNameWidget = ui_TextEntryCreateWithStringCombo(
						param->var_def.pcName, 0, 0,
						&param->dest_map_variables, true, true, false, true);
				ui_TextEntrySetFinishedCallback(pVarNameWidget, wleAEWorldVariableDefNameChanged, param);
				ui_WidgetSetWidthEx(UI_WIDGET(pVarNameWidget), param->entry_width, widthType);
				ui_RebuildableTreeAddWidget(auto_widget_node, UI_WIDGET(pVarNameWidget), NULL, false, full_param_name, &params);
				wleSkinDiffWidget(UI_WIDGET(pVarNameWidget), param->var_name_diff);
			}		
		}

		if (!param->is_specified && !param->display_if_unspecified) {
			return;
		} else if(param->is_specified || !param->can_unspecify) {
			// Editable Fields
			UIComboBox *combo;
			
			// Init from label
			sprintf(full_param_name, "%s|%d|initfrom_label", param_name, param->index);
			params.alignTo = labelAlignIndent;
			ui_RebuildableTreeAddLabelKeyed(auto_widget_node,
				(param->no_name ? name : "Init From"),
				full_param_name, &params, true);

			sprintf(full_param_name, "%s|%d|initfrom:", param_name, param->index);
			params.alignTo = entryAlignIndent;
			combo = ui_ComboBoxCreateWithEnum(0, 0, 0, WorldVariableDefaultValueTypeEnum, wleAEWorldVariableDefInitFromChanged, param );
			ui_ComboBoxSetSelectedEnum(combo, param->var_def.eDefaultType);
			ui_WidgetSetWidthEx( UI_WIDGET( combo ), param->entry_width, widthType );
			ui_WidgetSetTooltipString( UI_WIDGET( combo ), "Where the value comes from" );
			ui_RebuildableTreeAddWidget(auto_widget_node, UI_WIDGET(combo), NULL, false, full_param_name, &params );
			wleSkinDiffWidget( UI_WIDGET(combo), param->var_init_from_diff );

			// Type label and field
			sprintf(full_param_name, "%s|%d|type_label", param_name, param->index);
			params.alignTo = labelAlignIndent;
			ui_RebuildableTreeAddLabelKeyed(auto_widget_node,
				"Type",	full_param_name, &params, true);

			if(!param->eaDefList) {
				sprintf(full_param_name, "%s|%d|type:", param_name, param->index);
				params.alignTo = entryAlignIndent;
				combo = ui_ComboBoxCreateWithEnum(0, 0, 0, WorldVariableTypeEnum, wleAEWorldVariableDefTypeChanged, param );
				ui_ComboBoxSetSelectedEnum(combo, param->var_def.eType);
				ui_WidgetSetWidthEx( UI_WIDGET( combo ), param->entry_width, widthType );
				ui_WidgetSetTooltipString( UI_WIDGET( combo ), "The variable's type" );
				ui_RebuildableTreeAddWidget(auto_widget_node, UI_WIDGET(combo), NULL, false, full_param_name, &params );
				wleSkinDiffWidget( UI_WIDGET(combo), param->var_type_diff );
			} else {
				sprintf(full_param_name, "%s|%d|type:", param_name, param->index);
				params.alignTo = entryAlignIndent;
				ui_RebuildableTreeAddLabelKeyed(auto_widget_node,
					StaticDefineIntRevLookup(WorldVariableTypeEnum, param->var_def.eType),
					full_param_name, &params, false);
			}

			if(!param->scope) {
				if(param->dest_map_name) {
					param->scope = strdup(param->dest_map_name);
				} else {
					param->scope = strdup(param_name);
				}
			} 

#define WE_FSM_VAR_MAX 100000
			// specify default
			if (param->var_def.eDefaultType == WVARDEF_SPECIFY_DEFAULT) {
				// VARIABLE_TYPES: Add code below if add to the available variable types
				switch(param->var_def.eType) {
					case WVAR_INT:
						param->int_param.entry_width = 100;
						param->int_param.entry_align = entryAlignIndent - labelAlignIndent - 20;
						param->int_param.left_pad = labelAlignIndent;
						param->int_param.index = param->index;
						sprintf(full_param_name, "%s_int", param_name);
						wleAEIntAddWidget(auto_widget_node->root, "Int Value", NULL, full_param_name, &param->int_param, -WE_FSM_VAR_MAX, WE_FSM_VAR_MAX, 1);

					xcase WVAR_FLOAT:
						param->float_param.entry_width = 100;
						param->float_param.entry_align = entryAlignIndent - labelAlignIndent - 20;
						param->float_param.left_pad = labelAlignIndent;
						param->float_param.index = param->index;
						sprintf(full_param_name, "%s_float", param_name);
						wleAEFloatAddWidget(auto_widget_node->root, "Float Value", NULL, full_param_name, &param->float_param, -WE_FSM_VAR_MAX, WE_FSM_VAR_MAX, 1);

					xcase WVAR_STRING:
						param->string_param.entry_width = 1.0;
						param->string_param.entry_align = entryAlignIndent - labelAlignIndent - 20;
						param->string_param.left_pad = labelAlignIndent;
						param->string_param.index = param->index;
						sprintf(full_param_name, "%s_string", param_name);
						wleAETextAddWidget(auto_widget_node->root, "String Value", NULL, full_param_name, &param->string_param);

					xcase WVAR_LOCATION_STRING:
						param->string_param.entry_width = 1.0;
						param->string_param.entry_align = entryAlignIndent - labelAlignIndent - 20;
						param->string_param.left_pad = labelAlignIndent;
						param->string_param.index = param->index;
						sprintf(full_param_name, "%s_locstring", param_name);
						wleAETextAddWidget(auto_widget_node->root, "Loc. String", NULL, full_param_name, &param->string_param);
						
					xcase WVAR_MESSAGE:
						param->message_param.entry_width = 1.0;
						param->message_param.entry_align = entryAlignIndent - labelAlignIndent - 20;
						param->message_param.left_pad = labelAlignIndent;
						param->message_param.index = param->index;
						if(param->scope) {
							param->message_param.source_key = strdup(param->scope);
						} 
						sprintf(full_param_name, "%s_message", param_name);
						wleAEMessageAddWidget(auto_widget_node->root, "Message", NULL, full_param_name, &param->message_param);

					xcase WVAR_ANIMATION:
						param->dict_param.entry_width = 1.0;
						param->dict_param.entry_align = entryAlignIndent - labelAlignIndent - 20;
						param->dict_param.left_pad = labelAlignIndent;
						param->dict_param.index = param->index;
						param->dict_param.dictionary = "AIAnimList";
						sprintf(full_param_name, "%s_animation", param_name);
						wleAEDictionaryAddWidget(auto_widget_node->root, "Animation", NULL, full_param_name, &param->dict_param);

					xcase WVAR_CRITTER_DEF:
						param->dict_param.entry_width = 1.0;
						param->dict_param.entry_align = entryAlignIndent - labelAlignIndent - 20;
						param->dict_param.left_pad = labelAlignIndent;
						param->dict_param.index = param->index;
						param->dict_param.dictionary = "CritterDef";
						sprintf(full_param_name, "%s_critterdef", param_name);
						wleAEDictionaryAddWidget(auto_widget_node->root, "Critter Def", NULL, full_param_name, &param->dict_param);

					xcase WVAR_CRITTER_GROUP:
						param->dict_param.entry_width = 1.0;
						param->dict_param.entry_align = entryAlignIndent - labelAlignIndent - 20;
						param->dict_param.left_pad = labelAlignIndent;
						param->dict_param.index = param->index;
						param->dict_param.dictionary = "CritterGroup";
						sprintf(full_param_name, "%s_crittergroup", param_name);
						wleAEDictionaryAddWidget(auto_widget_node->root, "Critter Group", NULL, full_param_name, &param->dict_param);

					xcase WVAR_MAP_POINT:
						param->dict_param.entry_width = 1.0;
						param->dict_param.entry_align = entryAlignIndent - labelAlignIndent - 20;
						param->dict_param.left_pad = labelAlignIndent;
						param->dict_param.index = param->index;
						param->dict_param.dictionary = "ZoneMap";
						sprintf(full_param_name, "%s_mappoint_zone", param_name);
						wleAEDictionaryAddWidget(auto_widget_node->root, "Zone Map", "The target zonemap.  If left empty, the current map.", full_param_name, &param->dict_param);
						
						param->string_param.entry_width = 1.0;
						param->string_param.entry_align = entryAlignIndent - labelAlignIndent - 20;
						param->string_param.left_pad = labelAlignIndent;
						param->string_param.index = param->index;
						param->string_param.available_values = param->map_point_spawn_points;
						param->string_param.is_filtered = true;
						sprintf(full_param_name, "%s_mappoint_spawn", param_name);
						wleAETextAddWidget(auto_widget_node->root, "Spawn Point", "The target spawn point on the zonemap.  Can also be MissionReturn to go back to how this map was entered.", full_param_name, &param->string_param);

					xcase WVAR_ITEM_DEF:
						param->dict_param.entry_width = 1.0;
						param->dict_param.entry_align = entryAlignIndent - labelAlignIndent - 20;
						param->dict_param.left_pad = labelAlignIndent;
						param->dict_param.index = param->index;
						param->dict_param.dictionary = "ItemDef";
						sprintf(full_param_name, "%s_itemdef", param_name);
						wleAEDictionaryAddWidget(auto_widget_node->root, "ItemDef", NULL, full_param_name, &param->dict_param);

					xcase WVAR_MISSION_DEF:
						param->dict_param.entry_width = 1.0;
						param->dict_param.entry_align = entryAlignIndent - labelAlignIndent - 20;
						param->dict_param.left_pad = labelAlignIndent;
						param->dict_param.index = param->index;
						param->dict_param.dictionary = "Mission";
						sprintf(full_param_name, "%s_missiondef", param_name);
						wleAEDictionaryAddWidget(auto_widget_node->root, "MissionDef", NULL, full_param_name, &param->dict_param);
				}
			}

			// choice table
			else if (param->var_def.eDefaultType == WVARDEF_CHOICE_TABLE) {
				UITextEntry* choiceTableEntry;
				UITextEntry* choiceNameEntry;
				UISpinnerEntry *choiceIndexEntry;
			
				sprintf(full_param_name, "%s|%d|choicetable_label", param_name, param->index);
				params.alignTo = labelAlignIndent;
				ui_RebuildableTreeAddLabelKeyed(auto_widget_node, "Choice Table", full_param_name, &params, true);

				sprintf(full_param_name, "%s|%d|choicetable:", param_name, param->index);
				params.alignTo = entryAlignIndent;
				choiceTableEntry = ui_TextEntryCreateWithGlobalDictionaryCombo(
						REF_STRING_FROM_HANDLE(param->var_def.choice_table), 0, 0,
						g_hChoiceTableDict, "ResourceName", true, true, true, true);
				ui_TextEntrySetFinishedCallback(choiceTableEntry, wleAEWorldVariableDefChoiceTableChanged, param);
				ui_WidgetSetWidthEx( UI_WIDGET( choiceTableEntry ), param->entry_width, widthType );
				ui_WidgetSetTooltipString( UI_WIDGET(choiceTableEntry), "Value comes from this choice table." );
				ui_RebuildableTreeAddWidget(auto_widget_node, UI_WIDGET(choiceTableEntry), NULL, false, full_param_name, &params );
				wleSkinDiffWidget( UI_WIDGET(choiceTableEntry), param->var_value_diff );

				sprintf(full_param_name, "%s|%d|choicename_label", param_name, param->index);
				params.alignTo = labelAlignIndent;
				ui_RebuildableTreeAddLabelKeyed(auto_widget_node, "Choice Value", full_param_name, &params, true);

				sprintf(full_param_name, "%s|%d|choicename:", param_name, param->index);
				params.alignTo = entryAlignIndent;
				choiceNameEntry = ui_TextEntryCreateWithStringCombo(
						param->var_def.choice_name, 0, 0,
						&param->choice_table_names, true, true, false, true);
				ui_TextEntrySetFinishedCallback(choiceNameEntry, wleAEWorldVariableDefChoiceNameChanged, param);
				ui_WidgetSetWidthEx( UI_WIDGET( choiceNameEntry ), param->entry_width, widthType );
				ui_WidgetSetTooltipString( UI_WIDGET(choiceNameEntry), "Value comes from this value in the choice table use.  If two places specify the same choice table, the values will come from the same row." );
				ui_RebuildableTreeAddWidget(auto_widget_node, UI_WIDGET(choiceNameEntry), NULL, false, full_param_name, &params );
				wleSkinDiffWidget( UI_WIDGET(choiceNameEntry), param->var_value_diff );

				if (param->choice_table_index_max > 0)
				{
					sprintf(full_param_name, "%s|%d|choiceindex_label", param_name, param->index);
					params.alignTo = labelAlignIndent;
					ui_RebuildableTreeAddLabelKeyed(auto_widget_node, "Choice Index", full_param_name, &params, true);

					sprintf(full_param_name, "%s|%d|choiceindex:", param_name, param->index);
					params.alignTo = entryAlignIndent;
					choiceIndexEntry = ui_SpinnerEntryCreate(1, param->choice_table_index_max, 1, param->var_def.choice_index, false);
					ui_SpinnerEntrySetCallback(choiceIndexEntry, wleAEWorldVariableDefChoiceIndexChanged, param);
					ui_WidgetSetWidthEx( UI_WIDGET( choiceIndexEntry ), param->entry_width, widthType );
					ui_WidgetSetTooltipString( UI_WIDGET(choiceIndexEntry), "Value uses this unique entry per time interval for a timed random choice table.");
					ui_RebuildableTreeAddWidget(auto_widget_node, UI_WIDGET(choiceIndexEntry), NULL, false, full_param_name, &params );
					wleSkinDiffWidget( UI_WIDGET(choiceIndexEntry), param->var_value_diff );
				}
			}

			// map variable
			else if (param->var_def.eDefaultType == WVARDEF_MAP_VARIABLE) {
				UITextEntry* mapVariableEntry;
			
				sprintf(full_param_name, "%s|%d|mapvariable_label", param_name, param->index);
				params.alignTo = labelAlignIndent;
				ui_RebuildableTreeAddLabelKeyed(auto_widget_node, "Map Variable", full_param_name, &params, true);

				sprintf(full_param_name, "%s|%d|mapvariable", param_name, param->index);
				params.alignTo = entryAlignIndent;
				mapVariableEntry = ui_TextEntryCreateWithStringCombo(
						param->var_def.map_variable, 0, 0,
						&param->source_map_variables, true, true, false, true);
				ui_TextEntrySetFinishedCallback(mapVariableEntry, wleAEWorldVariableDefMapVariableChanged, param);
				ui_WidgetSetWidthEx( UI_WIDGET( mapVariableEntry ), param->entry_width, widthType );
				ui_WidgetSetTooltipString( UI_WIDGET(mapVariableEntry), "Value comes from this map variable." );
				ui_RebuildableTreeAddWidget(auto_widget_node, UI_WIDGET(mapVariableEntry), NULL, false, full_param_name, &params );
				wleSkinDiffWidget( UI_WIDGET(mapVariableEntry), param->var_value_diff );
			}

			// mission variable
			else if (param->var_def.eDefaultType == WVARDEF_MISSION_VARIABLE) {
				UITextEntry *missionEntry;
				UITextEntry *missionVariableEntry;

				sprintf(full_param_name, "%s|%d|mission_label", param_name, param->index);
				params.alignTo = labelAlignIndent;
				ui_RebuildableTreeAddLabelKeyed(auto_widget_node, "Mission", full_param_name, &params, true);

				sprintf(full_param_name, "%s|%d|mission", param_name, param->index);
				params.alignTo = entryAlignIndent;
				missionEntry = ui_TextEntryCreateWithGlobalDictionaryCombo(
					param->var_def.mission_refstring, 0, 0,
					"Mission", "ResourceName", true, true, false, true);
				ui_TextEntrySetFinishedCallback(missionEntry, wleAEWorldVariableDefMissionChanged, param);
				ui_WidgetSetWidthEx(UI_WIDGET(missionEntry), param->entry_width, widthType);
				ui_WidgetSetTooltipString(UI_WIDGET(missionEntry), "Value comes from this map variable.");
				ui_RebuildableTreeAddWidget(auto_widget_node, UI_WIDGET(missionEntry), NULL, false, full_param_name, &params);
				wleSkinDiffWidget(UI_WIDGET(missionEntry), param->var_value_diff);

				sprintf(full_param_name, "%s|%d|mission_var_label", param_name, param->index);
				params.alignTo = labelAlignIndent;
				ui_RebuildableTreeAddLabelKeyed(auto_widget_node, "Mission Variable", full_param_name, &params, true);

				sprintf(full_param_name, "%s|%d|mission_var", param_name, param->index);
				params.alignTo = entryAlignIndent;
				missionVariableEntry = ui_TextEntryCreateWithStringCombo(
					param->var_def.mission_variable, 0, 0,
					&param->mission_variables, true, true, false, true);
				ui_TextEntrySetFinishedCallback(missionVariableEntry, wleAEWorldVariableDefMissionVariableChanged, param);
				ui_WidgetSetWidthEx(UI_WIDGET(missionVariableEntry), param->entry_width, widthType);
				ui_WidgetSetTooltipString(UI_WIDGET(missionVariableEntry), "Value comes from this map variable.");
				ui_RebuildableTreeAddWidget(auto_widget_node, UI_WIDGET(missionVariableEntry), NULL, false, full_param_name, &params);
				wleSkinDiffWidget(UI_WIDGET(missionVariableEntry), param->var_value_diff);
			}

			// expression
			else if (param->var_def.eDefaultType == WVARDEF_EXPRESSION) {
				UIExpressionEntry *expressionEntry;

				sprintf(full_param_name, "%s|%d|expr_label", param_name, param->index);
				params.alignTo = labelAlignIndent;
				ui_RebuildableTreeAddLabelKeyed(auto_widget_node, "Expression", full_param_name, &params, true);

				sprintf(full_param_name, "%s|%d|expr", param_name, param->index);
				params.alignTo = entryAlignIndent;
				expressionEntry = ui_ExpressionEntryCreate(exprGetCompleteString(param->var_def.pExpression), worldVariableGetExprContext());
				ui_ExpressionEntrySetChangedCallback(expressionEntry, wleAEWorldVariableDefExpressionChanged, param);
				ui_WidgetSetWidthEx(UI_WIDGET(expressionEntry), param->entry_width, widthType);
				ui_WidgetSetTooltipString(UI_WIDGET(expressionEntry), "Value comes from this expression.");
				ui_RebuildableTreeAddWidget(auto_widget_node, UI_WIDGET(expressionEntry), NULL, false, full_param_name, &params);
				wleSkinDiffWidget(UI_WIDGET(expressionEntry), param->var_value_diff);
			}

			// Activity Variable
			else if (param->var_def.eDefaultType == WVARDEF_ACTIVITY_VARIABLE) {
				UITextEntry* activityVariableEntry;

				sprintf(full_param_name, "%s|%d|activityvariable_label", param_name, param->index);
				params.alignTo = labelAlignIndent;
				ui_RebuildableTreeAddLabelKeyed(auto_widget_node, "Activity Name", full_param_name, &params, true);

				sprintf(full_param_name, "%s|%d|activityvariable", param_name, param->index);
				params.alignTo = entryAlignIndent;
				activityVariableEntry = ui_TextEntryCreate(param->var_def.map_variable, 0, 0);
				ui_TextEntrySetFinishedCallback(activityVariableEntry, wleAEWorldVariableDefMapVariableChanged, param);
				ui_WidgetSetWidthEx( UI_WIDGET( activityVariableEntry ), param->entry_width, widthType );
				ui_WidgetSetTooltipString( UI_WIDGET(activityVariableEntry), "Value comes from this varaible on an activity." );
				ui_RebuildableTreeAddWidget(auto_widget_node, UI_WIDGET(activityVariableEntry), NULL, false, full_param_name, &params );
				wleSkinDiffWidget( UI_WIDGET(activityVariableEntry), param->var_value_diff );

				// VARIABLE_TYPES: Add code below if add to the available variable types
				switch(param->var_def.eType) {
				case WVAR_INT:
					param->int_param.entry_width = 100;
					param->int_param.entry_align = entryAlignIndent - labelAlignIndent - 20;
					param->int_param.left_pad = labelAlignIndent;
					param->int_param.index = param->index;
					sprintf(full_param_name, "%s_int", param_name);
					wleAEIntAddWidget(auto_widget_node->root, "Int Value", NULL, full_param_name, &param->int_param, -WE_FSM_VAR_MAX, WE_FSM_VAR_MAX, 1);

				xcase WVAR_FLOAT:
					param->float_param.entry_width = 100;
					param->float_param.entry_align = entryAlignIndent - labelAlignIndent - 20;
					param->float_param.left_pad = labelAlignIndent;
					param->float_param.index = param->index;
					sprintf(full_param_name, "%s_float", param_name);
					wleAEFloatAddWidget(auto_widget_node->root, "Float Value", NULL, full_param_name, &param->float_param, -WE_FSM_VAR_MAX, WE_FSM_VAR_MAX, 1);

				xcase WVAR_STRING:
					param->string_param.entry_width = 1.0;
					param->string_param.entry_align = entryAlignIndent - labelAlignIndent - 20;
					param->string_param.left_pad = labelAlignIndent;
					param->string_param.index = param->index;
					sprintf(full_param_name, "%s_string", param_name);
					wleAETextAddWidget(auto_widget_node->root, "String Value", NULL, full_param_name, &param->string_param);

				xcase WVAR_LOCATION_STRING:
					param->string_param.entry_width = 1.0;
					param->string_param.entry_align = entryAlignIndent - labelAlignIndent - 20;
					param->string_param.left_pad = labelAlignIndent;
					param->string_param.index = param->index;
					sprintf(full_param_name, "%s_locstring", param_name);
					wleAETextAddWidget(auto_widget_node->root, "Loc. String", NULL, full_param_name, &param->string_param);

				xcase WVAR_MESSAGE:
					param->message_param.entry_width = 1.0;
					param->message_param.entry_align = entryAlignIndent - labelAlignIndent - 20;
					param->message_param.left_pad = labelAlignIndent;
					param->message_param.index = param->index;
					if(param->scope) {
						param->message_param.source_key = strdup(param->scope);
					} 
					sprintf(full_param_name, "%s_message", param_name);
					wleAEMessageAddWidget(auto_widget_node->root, "Message", NULL, full_param_name, &param->message_param);

				xcase WVAR_ANIMATION:
					param->dict_param.entry_width = 1.0;
					param->dict_param.entry_align = entryAlignIndent - labelAlignIndent - 20;
					param->dict_param.left_pad = labelAlignIndent;
					param->dict_param.index = param->index;
					param->dict_param.dictionary = "AIAnimList";
					sprintf(full_param_name, "%s_animation", param_name);
					wleAEDictionaryAddWidget(auto_widget_node->root, "Animation", NULL, full_param_name, &param->dict_param);

				xcase WVAR_CRITTER_DEF:
					param->dict_param.entry_width = 1.0;
					param->dict_param.entry_align = entryAlignIndent - labelAlignIndent - 20;
					param->dict_param.left_pad = labelAlignIndent;
					param->dict_param.index = param->index;
					param->dict_param.dictionary = "CritterDef";
					sprintf(full_param_name, "%s_critterdef", param_name);
					wleAEDictionaryAddWidget(auto_widget_node->root, "Critter Def", NULL, full_param_name, &param->dict_param);

				xcase WVAR_CRITTER_GROUP:
					param->dict_param.entry_width = 1.0;
					param->dict_param.entry_align = entryAlignIndent - labelAlignIndent - 20;
					param->dict_param.left_pad = labelAlignIndent;
					param->dict_param.index = param->index;
					param->dict_param.dictionary = "CritterGroup";
					sprintf(full_param_name, "%s_crittergroup", param_name);
					wleAEDictionaryAddWidget(auto_widget_node->root, "Critter Group", NULL, full_param_name, &param->dict_param);

				xcase WVAR_MAP_POINT:
					param->dict_param.entry_width = 1.0;
					param->dict_param.entry_align = entryAlignIndent - labelAlignIndent - 20;
					param->dict_param.left_pad = labelAlignIndent;
					param->dict_param.index = param->index;
					param->dict_param.dictionary = "ZoneMap";
					sprintf(full_param_name, "%s_mappoint_zone", param_name);
					wleAEDictionaryAddWidget(auto_widget_node->root, "Zone Map", "The target zonemap.  If left empty, the current map.", full_param_name, &param->dict_param);

					param->string_param.entry_width = 1.0;
					param->string_param.entry_align = entryAlignIndent - labelAlignIndent - 20;
					param->string_param.left_pad = labelAlignIndent;
					param->string_param.index = param->index;
					param->string_param.available_values = param->map_point_spawn_points;
					param->string_param.is_filtered = true;
					sprintf(full_param_name, "%s_mappoint_spawn", param_name);
					wleAETextAddWidget(auto_widget_node->root, "Spawn Point", "The target spawn point on the zonemap.  Can also be MissionReturn to go back to how this map was entered.", full_param_name, &param->string_param);

				xcase WVAR_ITEM_DEF:
					param->dict_param.entry_width = 1.0;
					param->dict_param.entry_align = entryAlignIndent - labelAlignIndent - 20;
					param->dict_param.left_pad = labelAlignIndent;
					param->dict_param.index = param->index;
					param->dict_param.dictionary = "ItemDef";
					sprintf(full_param_name, "%s_itemdef", param_name);
					wleAEDictionaryAddWidget(auto_widget_node->root, "ItemDef", NULL, full_param_name, &param->dict_param);

				xcase WVAR_MISSION_DEF:
					param->dict_param.entry_width = 1.0;
					param->dict_param.entry_align = entryAlignIndent - labelAlignIndent - 20;
					param->dict_param.left_pad = labelAlignIndent;
					param->dict_param.index = param->index;
					param->dict_param.dictionary = "Mission";
					sprintf(full_param_name, "%s_missiondef", param_name);
					wleAEDictionaryAddWidget(auto_widget_node->root, "MissionDef", NULL, full_param_name, &param->dict_param);
				}
			}
		
		} else if(!param->is_specified && param->display_if_unspecified){
			// Uneditable labels
			char pchLabel[1024];
			char pchValue[1024];
			char pchValueParam[1024];
			char pchInitParam[1024];
			sprintf(pchLabel, "");
			sprintf(pchValue, "");
			sprintf(pchValueParam, "");
			sprintf(pchInitParam, "");
			sprintf(full_param_name, "");

			// specify default
			if (param->var_def.eDefaultType == WVARDEF_SPECIFY_DEFAULT && param->var_def.pSpecificValue) {
				// VARIABLE_TYPES: Add code below if add to the available variable types
				switch(param->var_def.eType) {
						case WVAR_INT:
							sprintf(full_param_name, "%s|%d|int_label", param_name, param->index);
							sprintf(pchLabel, "Int Value");

							sprintf(pchValueParam, "%s|%d|int_value:", param_name, param->index);
							sprintf(pchValue, "%d", param->var_def.pSpecificValue->iIntVal);

						xcase WVAR_FLOAT:
							sprintf(full_param_name, "%s|%d|float_label", param_name, param->index);
							sprintf(pchLabel, "Float Value");

							sprintf(pchValueParam, "%s|%d|float_value:", param_name, param->index);
							sprintf(pchValue, "%.5f", param->var_def.pSpecificValue->fFloatVal);

						xcase WVAR_STRING:
							if(EMPTY_TO_NULL(param->var_def.pSpecificValue->pcStringVal)) {
								sprintf(full_param_name, "%s|%d|string_label", param_name, param->index);
								sprintf(pchLabel, "String Value");

								sprintf(pchValueParam, "%s|%d|string_value:", param_name, param->index);
								sprintf(pchValue, "%s", param->var_def.pSpecificValue->pcStringVal);
							}

						xcase WVAR_LOCATION_STRING:
							if(EMPTY_TO_NULL(param->var_def.pSpecificValue->pcStringVal)) {
								sprintf(full_param_name, "%s|%d|loc_label", param_name, param->index);
								sprintf(pchLabel, "Loc. String");

								sprintf(pchValueParam, "%s|%d|loc_value:", param_name, param->index);
								sprintf(pchValue, "%s", param->var_def.pSpecificValue->pcStringVal);
							}

						xcase WVAR_MESSAGE:
							if(EMPTY_TO_NULL(TranslateDisplayMessage(param->var_def.pSpecificValue->messageVal)) || 
								EMPTY_TO_NULL(langTranslateMessage(locGetLanguage(getCurrentLocale()), param->var_def.pSpecificValue->messageVal.pEditorCopy)))
							{
								sprintf(full_param_name, "%s|%d|msg_label", param_name, param->index);
								sprintf(pchLabel, "Message");

								sprintf(pchValueParam, "%s|%d|msg_value:", param_name, param->index);
								sprintf(pchValue, "%s", NULL_TO_EMPTY(TranslateDisplayMessage(param->var_def.pSpecificValue->messageVal)));
								if(!EMPTY_TO_NULL(pchValue))
									sprintf(pchValue, "%s", langTranslateMessage(locGetLanguage(getCurrentLocale()), param->var_def.pSpecificValue->messageVal.pEditorCopy));
							}

						xcase WVAR_ANIMATION:
							if(EMPTY_TO_NULL(param->var_def.pSpecificValue->pcStringVal)) {
								sprintf(full_param_name, "%s|%d|anim_label", param_name, param->index);
								sprintf(pchLabel, "Animation");

								sprintf(pchValueParam, "%s|%d|anim_value:", param_name, param->index);
								sprintf(pchValue, "%s", param->var_def.pSpecificValue->pcStringVal);
							}

						xcase WVAR_CRITTER_DEF:
							if(EMPTY_TO_NULL(REF_STRING_FROM_HANDLE(param->var_def.pSpecificValue->hCritterDef))) {
								sprintf(full_param_name, "%s|%d|critter_label", param_name, param->index);
								sprintf(pchLabel, "CritterDef");

								sprintf(pchValueParam, "%s|%d|critter_value:", param_name, param->index);
								sprintf(pchValue, "%s", NULL_TO_EMPTY(REF_STRING_FROM_HANDLE(param->var_def.pSpecificValue->hCritterDef)));
							}

						xcase WVAR_CRITTER_GROUP:
							if(EMPTY_TO_NULL(REF_STRING_FROM_HANDLE(param->var_def.pSpecificValue->hCritterDef))) {
								sprintf(full_param_name, "%s|%d|crittergroup_label", param_name, param->index);
								sprintf(pchLabel, "CritterGroup");

								sprintf(pchValueParam, "%s|%d|crittergroup_value:", param_name, param->index);
								sprintf(pchValue, "%s", NULL_TO_EMPTY(REF_STRING_FROM_HANDLE(param->var_def.pSpecificValue->hCritterGroup)));
							}

						xcase WVAR_MAP_POINT:
							if(EMPTY_TO_NULL(param->var_def.pSpecificValue->pcZoneMap) || EMPTY_TO_NULL(param->var_def.pSpecificValue->pcStringVal)) {
								// Init from label
								sprintf(full_param_name, "%s|%d|initfrom_label", param_name, param->index);
								params.alignTo = labelAlignIndent;
								ui_RebuildableTreeAddLabelKeyed(auto_widget_node,
									"Init From",
									full_param_name, &params, true);

								sprintf(full_param_name, "%s|%d|initfrom_value:", param_name, param->index);
								params.alignTo = entryAlignIndent;
								ui_RebuildableTreeAddLabelKeyed(auto_widget_node,
									StaticDefineIntRevLookup(WorldVariableDefaultValueTypeEnum, param->var_def.eDefaultType),
									full_param_name, &params, false);

								// Type
								sprintf(full_param_name, "%s|%d|type_label", param_name, param->index);
								params.alignTo = labelAlignIndent;
								ui_RebuildableTreeAddLabelKeyed(auto_widget_node,
									"Type",	full_param_name, &params, true);

								sprintf(full_param_name, "%s|%d|type:", param_name, param->index);
									params.alignTo = entryAlignIndent;
									ui_RebuildableTreeAddLabelKeyed(auto_widget_node,
										StaticDefineIntRevLookup(WorldVariableTypeEnum, param->var_def.eType),
										full_param_name, &params, false);

								// Zone Map
								sprintf(full_param_name, "%s|%d|mappoint_zone_label", param_name, param->index);
								params.alignTo = labelAlignIndent;
								ui_RebuildableTreeAddLabelKeyed(auto_widget_node,
									"Zone Map",
									full_param_name, &params, true);

								sprintf(full_param_name, "%s|%d|mappoint_zone_value:", param_name, param->index);
								params.alignTo = entryAlignIndent;
								ui_RebuildableTreeAddLabelKeyed(auto_widget_node,
									strdup(NULL_TO_EMPTY(param->var_def.pSpecificValue->pcZoneMap)),
									full_param_name, &params, false);
								
								// Spawn Point
								sprintf(full_param_name, "%s|%d|mappoint_spawn_label", param_name, param->index);
								params.alignTo = labelAlignIndent;
								ui_RebuildableTreeAddLabelKeyed(auto_widget_node,
									"Spawn Point",
									full_param_name, &params, true);

								sprintf(full_param_name, "%s|%d|mappoint_spawn_value:", param_name, param->index);
								params.alignTo = entryAlignIndent;
								ui_RebuildableTreeAddLabelKeyed(auto_widget_node,
									strdup(NULL_TO_EMPTY(param->var_def.pSpecificValue->pcStringVal)),
									full_param_name, &params, false);
							}

						xcase WVAR_ITEM_DEF:
							if(EMPTY_TO_NULL(param->var_def.pSpecificValue->pcStringVal)) {
								sprintf(full_param_name, "%s|%d|itemdef_label", param_name, param->index);
								sprintf(pchLabel, "ItemDef");

								sprintf(pchValueParam, "%s|%d|itemdef_value:", param_name, param->index);
								sprintf(pchValue, "%s", param->var_def.pSpecificValue->pcStringVal);
							}
							break;
						xcase WVAR_MISSION_DEF:
							if(EMPTY_TO_NULL(param->var_def.pSpecificValue->pcStringVal)) {
								sprintf(full_param_name, "%s|%d|missiondef_label", param_name, param->index);
								sprintf(pchLabel, "MissionDef");

								sprintf(pchValueParam, "%s|%d|missiondef_value:", param_name, param->index);
								sprintf(pchValue, "%s", param->var_def.pSpecificValue->pcStringVal);
							}
							break;
				}

				// Display if value exists
				if(param->var_def.eType != WVAR_MAP_POINT && EMPTY_TO_NULL(pchValue)) {
					// Init from label
					sprintf(pchInitParam, "%s|%d|initfrom_label", param_name, param->index);
					params.alignTo = labelAlignIndent;
					ui_RebuildableTreeAddLabelKeyed(auto_widget_node,
						"Init From",
						pchInitParam, &params, true);

					sprintf(pchInitParam, "%s|%d|initfrom_value:", param_name, param->index);
					params.alignTo = entryAlignIndent;
					ui_RebuildableTreeAddLabelKeyed(auto_widget_node,
						StaticDefineIntRevLookup(WorldVariableDefaultValueTypeEnum, param->var_def.eDefaultType),
						pchInitParam, &params, false);

					// Type
					sprintf(pchInitParam, "%s|%d|type_label", param_name, param->index);
					params.alignTo = labelAlignIndent;
					ui_RebuildableTreeAddLabelKeyed(auto_widget_node,
						"Type",	pchInitParam, &params, true);

					sprintf(pchInitParam, "%s|%d|type:", param_name, param->index);
					params.alignTo = entryAlignIndent;
					ui_RebuildableTreeAddLabelKeyed(auto_widget_node,
						StaticDefineIntRevLookup(WorldVariableTypeEnum, param->var_def.eType),
						pchInitParam, &params, false);

					// Value
					params.alignTo = labelAlignIndent;
					ui_RebuildableTreeAddLabelKeyed(auto_widget_node,
						NULL_TO_EMPTY(pchLabel),
						full_param_name, &params, true);

					params.alignTo = entryAlignIndent;
					ui_RebuildableTreeAddLabelKeyed(auto_widget_node,
						EMPTY_TO_NULL(pchValue) ? pchValue : "NOT SET",
						pchValueParam, &params, false);
				}


			}

			// choice table
			if (param->var_def.eDefaultType == WVARDEF_CHOICE_TABLE && EMPTY_TO_NULL(REF_STRING_FROM_HANDLE(param->var_def.choice_table))) {
				// Init from label
				sprintf(pchInitParam, "%s|%d|initfrom_label", param_name, param->index);
				params.alignTo = labelAlignIndent;
				ui_RebuildableTreeAddLabelKeyed(auto_widget_node,
					"Init From",
					pchInitParam, &params, true);

				sprintf(pchInitParam, "%s|%d|initfrom_value:", param_name, param->index);
				params.alignTo = entryAlignIndent;
				ui_RebuildableTreeAddLabelKeyed(auto_widget_node,
					StaticDefineIntRevLookup(WorldVariableDefaultValueTypeEnum, param->var_def.eDefaultType),
					pchInitParam, &params, false);

				// Type
				sprintf(full_param_name, "%s|%d|type_label", param_name, param->index);
				params.alignTo = labelAlignIndent;
				ui_RebuildableTreeAddLabelKeyed(auto_widget_node,
					"Type",	full_param_name, &params, true);

				sprintf(full_param_name, "%s|%d|type:", param_name, param->index);
				params.alignTo = entryAlignIndent;
				ui_RebuildableTreeAddLabelKeyed(auto_widget_node,
					StaticDefineIntRevLookup(WorldVariableTypeEnum, param->var_def.eType),
					full_param_name, &params, false);

				// Choice Table
				sprintf(full_param_name, "%s|%d|choicetable_label", param_name, param->index);
				params.alignTo = labelAlignIndent;
				ui_RebuildableTreeAddLabelKeyed(auto_widget_node, "Choice Table", full_param_name, &params, true);

				sprintf(full_param_name, "%s|%d|choicetable_value:", param_name, param->index);
				params.alignTo = entryAlignIndent;
				ui_RebuildableTreeAddLabelKeyed(auto_widget_node,
					strdup(NULL_TO_EMPTY(REF_STRING_FROM_HANDLE(param->var_def.choice_table))),
					full_param_name, &params, false);

				// Choice Name
				sprintf(full_param_name, "%s|%d|choicename_label", param_name, param->index);
				params.alignTo = labelAlignIndent;
				ui_RebuildableTreeAddLabelKeyed(auto_widget_node, "Choice Value", full_param_name, &params, true);

				sprintf(full_param_name, "%s|%d|choicename_value:", param_name, param->index);
				params.alignTo = entryAlignIndent;
				ui_RebuildableTreeAddLabelKeyed(auto_widget_node,
					strdup(NULL_TO_EMPTY(param->var_def.choice_name)),
					full_param_name, &params, false);

				// Choice Index
				if (param->choice_table_index_max > 0)
				{
					sprintf(full_param_name, "%s|%d|choiceindex_label", param_name, param->index);
					params.alignTo = labelAlignIndent;
					ui_RebuildableTreeAddLabelKeyed(auto_widget_node, "Choice Value", full_param_name, &params, true);

					sprintf(full_param_name, "%s|%d|choiceindex_value:", param_name, param->index);
					params.alignTo = entryAlignIndent;
					ui_RebuildableTreeAddLabelKeyed(auto_widget_node,
						strdupf("%i", param->var_def.choice_index),
						full_param_name, &params, false);
				}
			}

			// map variable
			if (param->var_def.eDefaultType == WVARDEF_MAP_VARIABLE && EMPTY_TO_NULL(param->var_def.map_variable)) {
				// Init from label
				sprintf(pchInitParam, "%s|%d|initfrom_label", param_name, param->index);
				params.alignTo = labelAlignIndent;
				ui_RebuildableTreeAddLabelKeyed(auto_widget_node,
					"Init From",
					pchInitParam, &params, true);

				sprintf(pchInitParam, "%s|%d|initfrom_value:", param_name, param->index);
				params.alignTo = entryAlignIndent;
				ui_RebuildableTreeAddLabelKeyed(auto_widget_node,
					StaticDefineIntRevLookup(WorldVariableDefaultValueTypeEnum, param->var_def.eDefaultType),
					pchInitParam, &params, false);

				// Type
				sprintf(full_param_name, "%s|%d|type_label", param_name, param->index);
				params.alignTo = labelAlignIndent;
				ui_RebuildableTreeAddLabelKeyed(auto_widget_node,
					"Type",	full_param_name, &params, true);

				sprintf(full_param_name, "%s|%d|type:", param_name, param->index);
				params.alignTo = entryAlignIndent;
				ui_RebuildableTreeAddLabelKeyed(auto_widget_node,
					StaticDefineIntRevLookup(WorldVariableTypeEnum, param->var_def.eType),
					full_param_name, &params, false);

				// Map Variable
				sprintf(full_param_name, "%s|%d|mapvariable_label", param_name, param->index);
				params.alignTo = labelAlignIndent;
				ui_RebuildableTreeAddLabelKeyed(auto_widget_node, "Map Variable", full_param_name, &params, true);

				sprintf(full_param_name, "%s|%d|mapvariable_value:", param_name, param->index);
				params.alignTo = entryAlignIndent;
				ui_RebuildableTreeAddLabelKeyed(auto_widget_node,
					strdup(NULL_TO_EMPTY(param->var_def.map_variable)),
					full_param_name, &params, false);
			}

			// mission variable
			if (param->var_def.eDefaultType == WVARDEF_MISSION_VARIABLE && EMPTY_TO_NULL(param->var_def.mission_refstring) && EMPTY_TO_NULL(param->var_def.mission_variable)) {
				// Init from label
				sprintf(pchInitParam, "%s|%d|initfrom_label", param_name, param->index);
				params.alignTo = labelAlignIndent;
				ui_RebuildableTreeAddLabelKeyed(auto_widget_node,
					"Init From",
					pchInitParam, &params, true);

				sprintf(pchInitParam, "%s|%d|initfrom_value:", param_name, param->index);
				params.alignTo = entryAlignIndent;
				ui_RebuildableTreeAddLabelKeyed(auto_widget_node,
					StaticDefineIntRevLookup(WorldVariableDefaultValueTypeEnum, param->var_def.eDefaultType),
					pchInitParam, &params, false);

				// Type
				sprintf(full_param_name, "%s|%d|type_label", param_name, param->index);
				params.alignTo = labelAlignIndent;
				ui_RebuildableTreeAddLabelKeyed(auto_widget_node,
					"Type",	full_param_name, &params, true);

				sprintf(full_param_name, "%s|%d|type:", param_name, param->index);
				params.alignTo = entryAlignIndent;
				ui_RebuildableTreeAddLabelKeyed(auto_widget_node,
					StaticDefineIntRevLookup(WorldVariableTypeEnum, param->var_def.eType),
					full_param_name, &params, false);

				// Mission
				sprintf(full_param_name, "%s|%d|mission_label", param_name, param->index);
				params.alignTo = labelAlignIndent;
				ui_RebuildableTreeAddLabelKeyed(auto_widget_node, "Mission", full_param_name, &params, true);

				sprintf(full_param_name, "%s|%d|mission_value:", param_name, param->index);
				params.alignTo = entryAlignIndent;
				ui_RebuildableTreeAddLabelKeyed(auto_widget_node,
					NULL_TO_EMPTY(param->var_def.mission_refstring),
					full_param_name, &params, false);

				// Mission Variable
				sprintf(full_param_name, "%s|%d|missionvariable_label", param_name, param->index);
				params.alignTo = labelAlignIndent;
				ui_RebuildableTreeAddLabelKeyed(auto_widget_node, "Mission Variable", full_param_name, &params, true);

				sprintf(full_param_name, "%s|%d|missionvariable_value:", param_name, param->index);
				params.alignTo = entryAlignIndent;
				ui_RebuildableTreeAddLabelKeyed(auto_widget_node,
					NULL_TO_EMPTY(param->var_def.mission_variable),
					full_param_name, &params, false);
			}

			// expression
			if (param->var_def.eDefaultType == WVARDEF_EXPRESSION && EMPTY_TO_NULL(exprGetCompleteString(param->var_def.pExpression))) {
				// Init from label
				sprintf(pchInitParam, "%s|%d|initfrom_label", param_name, param->index);
				params.alignTo = labelAlignIndent;
				ui_RebuildableTreeAddLabelKeyed(auto_widget_node,
					"Init From",
					pchInitParam, &params, true);

				sprintf(pchInitParam, "%s|%d|initfrom_value:", param_name, param->index);
				params.alignTo = entryAlignIndent;
				ui_RebuildableTreeAddLabelKeyed(auto_widget_node,
					StaticDefineIntRevLookup(WorldVariableDefaultValueTypeEnum, param->var_def.eDefaultType),
					pchInitParam, &params, false);

				// Type
				sprintf(full_param_name, "%s|%d|type_label", param_name, param->index);
				params.alignTo = labelAlignIndent;
				ui_RebuildableTreeAddLabelKeyed(auto_widget_node,
					"Type",	full_param_name, &params, true);

				sprintf(full_param_name, "%s|%d|type:", param_name, param->index);
				params.alignTo = entryAlignIndent;
				ui_RebuildableTreeAddLabelKeyed(auto_widget_node,
					StaticDefineIntRevLookup(WorldVariableTypeEnum, param->var_def.eType),
					full_param_name, &params, false);

				// Expression
				sprintf(full_param_name, "%s|%d|expression_label", param_name, param->index);
				params.alignTo = labelAlignIndent;
				ui_RebuildableTreeAddLabelKeyed(auto_widget_node, "Expression", full_param_name, &params, true);

				sprintf(full_param_name, "%s|%d|expression_value:", param_name, param->index);
				params.alignTo = entryAlignIndent;
				ui_RebuildableTreeAddLabelKeyed(auto_widget_node,
					NULL_TO_EMPTY(exprGetCompleteString(param->var_def.pExpression)),
					full_param_name, &params, false);
			}
		}
	}
}

#endif

#include "WorldEditorAttributesHelpers_h_ast.c"
