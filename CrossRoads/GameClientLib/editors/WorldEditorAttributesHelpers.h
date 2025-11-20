#ifndef __WORLDEDITORATTRIBUTESHELPERS_H__
#define __WORLDEDITORATTRIBUTESHELPERS_H__
GCC_SYSTEM

#include "WorldEditorAttributes.h"
#include "UIAutoWidget.h"
#include "Message.h"
#include "WorldVariable.h"

typedef struct TrackerHandle TrackerHandle;
typedef struct EditorObject EditorObject;
typedef struct GroupTracker GroupTracker;
typedef struct GroupDef GroupDef;
typedef struct Expression Expression;
typedef struct DisplayMessage DisplayMessage;
typedef struct EMPicker EMPicker;
typedef struct UIMessageEntry UIMessageEntry;
typedef struct UIGameActionEditButton UIGameActionEditButton;
typedef struct Message Message;
typedef struct WorldGameActionBlock WorldGameActionBlock;
typedef struct WleCriterion WleCriterion;
typedef struct ParseTable ParseTable;
typedef struct WorldVariableDef WorldVariableDef;
typedef struct UITextEntry UITextEntry;
typedef struct ZoneMapInfo ZoneMapInfo;

typedef struct WleAEParamBool WleAEParamBool;
typedef struct WleAEParamInt WleAEParamInt;
typedef struct WleAEParamFloat WleAEParamFloat;
typedef struct WleAEParamText WleAEParamText;
typedef struct WleAEParamCombo WleAEParamCombo;
typedef struct WleAEParamDictionary WleAEParamDictionary;
typedef struct WleAEParamVec3 WleAEParamVec3;
typedef struct WleAEParamHSV WleAEParamHSV;
typedef struct WleAEParamHue WleAEParamHue;
typedef struct WleAEParamExpression WleAEParamExpression;
typedef struct WleAEParamTexture WleAEParamTexture;
typedef struct WleAEParamPicker WleAEParamPicker;
typedef struct WleAEParamMessage WleAEParamMessage;
typedef struct WleAEParamGameAction WleAEParamGameAction;
typedef struct WleAEParamWorldVariableDef WleAEParamWorldVariableDef;

// forward decl of function types
typedef void (*WleAEParamBoolUpdateFunc)(WleAEParamBool *param, void *func_data, EditorObject *obj);
typedef void (*WleAEParamIntUpdateFunc)(WleAEParamInt *param, void *func_data, EditorObject *obj);
typedef void (*WleAEParamFloatUpdateFunc)(WleAEParamFloat *param, void *func_data, EditorObject *obj);
typedef void (*WleAEParamTextUpdateFunc)(WleAEParamText *param, void *func_data, EditorObject *obj);
typedef void (*WleAEParamComboUpdateFunc)(WleAEParamCombo *param, void *func_data, EditorObject *obj);
typedef void (*WleAEParamDictionaryUpdateFunc)(WleAEParamDictionary *param, void *func_data, EditorObject *obj);
typedef void (*WleAEParamVec3UpdateFunc)(WleAEParamVec3 *param, void *func_data, EditorObject *obj);
typedef void (*WleAEParamHSVUpdateFunc)(WleAEParamHSV *param, void *func_data, EditorObject *obj);
typedef void (*WleAEParamHueUpdateFunc)(WleAEParamHue *param, void *func_data, EditorObject *obj);
typedef void (*WleAEParamExpressionUpdateFunc)(WleAEParamExpression *param, void *func_data, EditorObject *obj);
typedef void (*WleAEParamTextureUpdateFunc)(WleAEParamTexture *param, void *func_data, EditorObject *obj);
typedef void (*WleAEParamPickerUpdateFunc)(WleAEParamPicker *param, void *func_data, EditorObject *obj);
typedef void (*WleAEParamMessageUpdateFunc)(WleAEParamMessage *param, void *func_data, EditorObject *obj);
typedef void (*WleAEParamGameActionUpdateFunc)(WleAEParamGameAction *param, void *func_data, EditorObject *obj);
typedef void (*WleAEParamWorldVariableDefUpdateFunc)(WleAEParamWorldVariableDef *param, void *func_data, EditorObject *obj);

//Generic version
typedef void (*WleAEParamUpdateFunc)(void *param, void *func_data, EditorObject *obj);

// forward decl of function types
typedef void (*WleAEParamBoolApplyFunc)(WleAEParamBool *param, void *func_data, EditorObject **objs);
typedef void (*WleAEParamIntApplyFunc)(WleAEParamInt *param, void *func_data, EditorObject **objs);
typedef void (*WleAEParamFloatApplyFunc)(WleAEParamFloat *param, void *func_data, EditorObject **objs);
typedef void (*WleAEParamTextApplyFunc)(WleAEParamText *param, void *func_data, EditorObject **objs);
typedef void (*WleAEParamComboApplyFunc)(WleAEParamCombo *param, void *func_data, EditorObject **objs);
typedef void (*WleAEParamDictionaryApplyFunc)(WleAEParamDictionary *param, void *func_data, EditorObject **objs);
typedef void (*WleAEParamVec3ApplyFunc)(WleAEParamVec3 *param, void *func_data, EditorObject **objs);
typedef void (*WleAEParamHSVApplyFunc)(WleAEParamHSV *param, void *func_data, EditorObject **objs);
typedef void (*WleAEParamHueApplyFunc)(WleAEParamHue *param, void *func_data, EditorObject **objs);
typedef void (*WleAEParamExpressionApplyFunc)(WleAEParamExpression *param, void *func_data, EditorObject **objs);
typedef void (*WleAEParamTextureApplyFunc)(WleAEParamTexture *param, void *func_data, EditorObject **objs);
typedef void (*WleAEParamPickerApplyFunc)(WleAEParamPicker *param, void *func_data, EditorObject **objs);
typedef void (*WleAEParamMessageApplyFunc)(WleAEParamMessage *param, void *func_data, EditorObject **objs);
typedef void (*WleAEParamGameActionApplyFunc)(WleAEParamGameAction *param, void *func_data, EditorObject **objs);
typedef void (*WleAEParamWorldVariableDefApplyFunc)(WleAEParamWorldVariableDef *param, void *func_data, EditorObject **objs);

//Generic version
typedef void (*WleAEParamApplyFunc)(void *param, void *func_data, EditorObject **objs);

typedef void (*WleAEObjGetTrackerFunc)(EditorObject *obj, TrackerHandle** tracker);


/********************
* Note: Be careful when choosing parameters to convert to search filters automatically.  Such parameters
* must have static data.  In other words, you shouldn't try to convert parameters whose list of available
* values is changing upon AE reloads, or parameters whose callbacks are being changed on the fly.
********************/

#ifndef NO_EDITORS

typedef enum WleAEEditable
{
	WLE_AE_EDITABLE = 0,				// standard rendering
	WLE_AE_GRAYED,						// gray background (to signify editing multiple things that have different values)
	WLE_AE_DISABLED,					// disabled entirely
} WleAEEditable;

/********************
* UTIL
********************/
void wleAEGetSelectedObjects(EditorObject ***objects);
bool wleNeedsEncounterPanels(GroupDef *def);
void wleSkinDiffWidget(UIWidget *widget, bool diff);
void wleSkinDiffOrSourcedWidget(UIWidget *widget, bool diff, void* source);

//TPI based functions
void wleAEUpdateAll(void *propstruct, ParseTable *tpi, U64 exclude_columns);
void wleAEUpdate(void *propstruct, ParseTable *tpi, int column);

void wleAEAddAllWidgets(UIRebuildableTreeNode *auto_widget_node, void *propstruct, ParseTable *tpi, U64 exclude_columns);
void wleAEAddWidgetEx(UIRebuildableTreeNode *auto_widget_node, void *propstruct, ParseTable *tpi, int column);

void wleAESetupAllProperties(void *propstruct, ParseTable *tpi, U32 entry_align);
void wleAESetupProperty(void *propstruct, ParseTable *tpi, int col, U32 entry_align);

void wleAESetApplyFunction(void *propstruct, ParseTable *tpi, int col, WleAEParamApplyFunc func);
void wleAESetUpdateFunction(void *propstruct, ParseTable *tpi, int col, WleAEParamUpdateFunc func);

//*If we ever need more than 60 fields in the data struct, the we'll need to swap these bit field out with something bigger (ie bitfield.c)
#define wleSetBit(field, bit) (field |= (U64)1 << (bit - 1))
#define wleCheckBit(field, bit) (field & ((U64)1 << (bit - 1)))
#define wleUnsetBit(field, bit) (field &= ~((U64)1 << (bit - 1)))

AUTO_STRUCT;
typedef struct WleAEParamFormatNewLine
{
	int dummy_value;
} WleAEParamFormatNewLine;
extern ParseTable parse_WleAEParamFormatNewLine[];
#define TYPE_parse_WleAEParamFormatNewLine WleAEParamFormatNewLine

/********************
* BOOL
********************/
#endif
AUTO_STRUCT;
typedef struct WleAEParamBool
{
	// primary data
	bool is_specified;				AST(BOOLFLAG)	// flag that is updated according to the checkbox created when can_unspecify is true
	bool boolvalue;					AST(BOOLFLAG)	// main data

	AST_STOP

#ifndef NO_EDITORS
	// functional/visual parameters
	bool can_unspecify;								// indicates whether the parameter can be unspecified (with a checkbox)
	bool disabled;									// indicates whether to disable input into this field
	U32 entry_align;								// width to which the first input field will be aligned
	U32 left_pad;									// width to which the descriptive text will be aligned

	// source parameters
	EditorObject *source;							// holds source (if not the object itself)
	const char *property_name;						// the property to update with the contents of this parameter
	size_t struct_offset;							// if property name not set, this can be set to the address offset of a struct hanging off of the GroupDef
	ParseTable *struct_pti;							// if property name not set, ParseTable for the struct
	const char *struct_fieldname;					// if property name not set, name of the struct's field that this parameter is modifying
	int index;										// Usable by caller

	// custom callbacks - supply these if a property name is not specified
	WleAEParamBoolUpdateFunc update_func;					// this callback should populate the param's data from actual data
	void *update_data;								// this is passed to update_func
	WleAEParamBoolApplyFunc apply_func;					// this callback should apply data from the param to actual data
	void *apply_data;								// this is passed to apply_func

	// internal data - READING ALLOWED, BUT NO TOUCHING!
	bool spec_diff;									// stores whether this parameter's is_specified value is different across multiple selections
	bool diff;										// stores whether this value is different across multiple selections
#endif
} WleAEParamBool;
extern ParseTable parse_WleAEParamBool[];
#define TYPE_parse_WleAEParamBool WleAEParamBool
#ifndef NO_EDITORS

void wleAEBoolUpdate(WleAEParamBool *param);
void wleAEBoolAddWidgetEx(UIRebuildableTreeNode *auto_widget_node, const char *name, const char *tooltip, const char *param_name, WleAEParamBool *param);
#define wleAEBoolAddWidget(auto_widget_tree, name,tooltip,param_name,param) wleAEBoolAddWidgetEx(auto_widget_tree->root, name,tooltip,param_name,param)
WleCriterion *wleAEBoolRegisterFilter(WleAEParamBool *param, const char *propertyName);


/********************
* INT
********************/
#endif
AUTO_STRUCT;
typedef struct WleAEParamInt
{
	// primary data
	bool is_specified;				AST(BOOLFLAG)	// flag that is updated according to the checkbox created when can_unspecify is true
	int intvalue;									// main data

	AST_STOP

#ifndef NO_EDITORS
	// functional/visual parameters
	bool can_unspecify;								// indicates whether the parameter can be unspecified (with a checkbox)
	bool disabled;									// indicates whether to disable input into this field
	F32 entry_width;								// width of the input field
	U32 entry_align;								// width to which the first input field will be aligned
	U32 left_pad;									// width to which the descriptive text will be aligned

	// source parameters
	//EditorObject *source;							// holds source (if not the object itself)
	const char *property_name;						// the property to update with the contents of this parameter
	size_t struct_offset;							// if property name not set, this can be set to the address offset of a struct hanging off of the GroupDef
	ParseTable *struct_pti;							// if property name not set, ParseTable for the struct
	const char *struct_fieldname;					// if property name not set, name of the struct's field that this parameter is modifying
	int index;										// Usable by caller

	// custom callbacks - supply these if a property name is not specified
	WleAEParamIntUpdateFunc update_func;					// this callback should populate the param's data from actual data
	void *update_data;								// this is passed to update_func
	WleAEParamIntApplyFunc apply_func;					// this callback should apply data from the param to actual data
	void *apply_data;								// this is passed to apply_func

	// internal data - READING ALLOWED, BUT NO TOUCHING!
	bool spec_diff;									// stores whether the specified flag is different across multiple selections
	bool diff;										// stores whether a particular value is different across multiple selections
#endif
} WleAEParamInt;
extern ParseTable parse_WleAEParamInt[];
#define TYPE_parse_WleAEParamInt WleAEParamInt
#ifndef NO_EDITORS

void wleAEIntUpdate(WleAEParamInt *param);
void wleAEIntAddWidgetEx(UIRebuildableTreeNode *auto_widget_node, const char *name, const char *tooltip, const char *param_name, WleAEParamInt *param, int min, int max, int step);
#define wleAEIntAddWidget(auto_widget_tree, name,tooltip,param_name,param,min,max,step) wleAEIntAddWidgetEx(auto_widget_tree->root, name,tooltip,param_name,param,min,max,step)
WleCriterion *wleAEIntRegisterFilter(WleAEParamInt *param, const char *propertyName);

/********************
* FLOAT
********************/
#endif
AUTO_STRUCT;
typedef struct WleAEParamFloat
{
	// primary data
	bool is_specified;				AST(BOOLFLAG)	// flag that is updated according to the checkbox created when can_unspecify is true
	float floatvalue;								// main data

	AST_STOP

#ifndef NO_EDITORS
	// functional/visual parameters
	bool can_unspecify;								// indicates whether the parameter can be unspecified (with a checkbox)
	bool disabled;									// indicates whether to disable input into this field
	F32 entry_width;								// width of the input field
	U32 entry_align;								// width to which the first input field will be aligned
	U32 left_pad;									// width to which the descriptive text will be aligned
	U32 precision;									// precision of inputs appearing in the parameter
	float default_value;

	// source parameters
	EditorObject *source;							// holds source (if not the object itself)
	const char *property_name;						// the property to update with the contents of this parameter
	size_t struct_offset;							// if property name not set, this can be set to the address offset of a struct hanging off of the GroupDef
	ParseTable *struct_pti;							// if property name not set, ParseTable for the struct
	const char *struct_fieldname;					// if property name not set, name of the struct's field that this parameter is modifying
	int index;										// Usable by caller

	// custom callbacks - supply these if a property name is not specified
	WleAEParamFloatUpdateFunc update_func;				// this callback should populate the param's data from actual data
	void *update_data;								// this is passed to update_func
	WleAEParamFloatApplyFunc apply_func;					// this callback should apply data from the param to actual data
	void *apply_data;								// this is passed to apply_func

	// internal data - READING ALLOWED, BUT NO TOUCHING!
	bool spec_diff;									// stores whether the specified flag is different across multiple selections
	bool diff;										// stores whether a particular value is different across multiple selections
#endif
} WleAEParamFloat;
extern ParseTable parse_WleAEParamFloat[];
#define TYPE_parse_WleAEParamFloat WleAEParamFloat
#ifndef NO_EDITORS

void wleAEFloatUpdate(WleAEParamFloat *param);
void wleAEFloatAddWidgetEx(UIRebuildableTreeNode *auto_widget_node, const char *name, const char *tooltip, const char *param_name, WleAEParamFloat *param, float min, float max, float step);
#define wleAEFloatAddWidget(auto_widget_tree, name,tooltip,param_name,param,min,max,step) wleAEFloatAddWidgetEx(auto_widget_tree->root, name,tooltip,param_name,param,min,max,step)
WleCriterion *wleAEFloatRegisterFilter(WleAEParamFloat *param, const char *propertyName);

/********************
* TEXT
********************/
#endif
AUTO_STRUCT;
typedef struct WleAEParamText
{
	// primary data
	bool is_specified;				AST(BOOLFLAG)	// flag that is updated according to the checkbox created when can_unspecify is true
	char *stringvalue;								// main data

	AST_STOP

#ifndef NO_EDITORS
	// functional/visual parameters
	bool can_unspecify;								// indicates whether the parameter can be unspecified (with a checkbox)
	bool disabled;									// indicates whether to disable input into this field
	bool is_filtered;								// indicates whether to use a filtered combo box
	F32 entry_width;								// text entry width
	U32 entry_align;								// width to which the first input field will be aligned
	U32 left_pad;									// width to which the descriptive text will be aligned
	char **available_values;						// contents of optional combo box

	// source parameters
	//EditorObject *source;							// holds source (if not the object itself)
	const char *property_name;						// the property to update with the contents of this parameter
	size_t struct_offset;							// if property name not set, this can be set to the address offset of a struct hanging off of the GroupDef
	ParseTable *struct_pti;							// if property name not set, ParseTable for the struct
	const char *struct_fieldname;					// if property name not set, name of the struct's field that this parameter is modifying
	int index;										// Usable by caller

	// custom callbacks - supply these if a property name is not specified
	WleAEParamTextUpdateFunc update_func;					// this callback should populate the param's stringvalue from actual data; use StructAllocString!
	void *update_data;								// this is passed to update_func
	WleAEParamTextApplyFunc apply_func;					// this callback should apply data from the param to actual data
	void *apply_data;								// this is passed to apply_func

	// internal data - READING ALLOWED, BUT NO TOUCHING!
	bool spec_diff;									// stores whether this parameter's is_specified value is different across multiple selections
	bool diff;										// stores whether this value is different across multiple selections
#endif
} WleAEParamText;
extern ParseTable parse_WleAEParamText[];
#define TYPE_parse_WleAEParamText WleAEParamText
#ifndef NO_EDITORS

void wleAETextUpdate(WleAEParamText *param);
void wleAETextAddWidgetEx(UIRebuildableTree *auto_widget, const char *name, const char *tooltip, const char *param_name, WleAEParamText *param);
#define wleAETextAddWidget(auto_widget, name, tooltip, param_name, param) wleAETextAddWidgetEx(auto_widget, name, tooltip, param_name, param);
WleCriterion *wleAETextRegisterFilter(WleAEParamText *param, const char *propertyName);

/********************
* COMBO
*
* TODO: stringvalue currently points directly into available_values,
* but it should not.  stringvalue should probably be strdup'd/free'd
* so that it can be StructDestroy'd correctly.
********************/
#endif
AUTO_STRUCT;
typedef struct WleAEParamCombo
{
	// primary data
	bool is_specified;				AST(BOOLFLAG)	// flag that is updated according to the checkbox created when can_unspecify is true
	const char *stringvalue;		AST(UNOWNED)	// main data

	AST_STOP

#ifndef NO_EDITORS
	// functional/visual parameters
	bool can_unspecify;								// indicates whether the parameter can be unspecified (with a checkbox)
	bool can_copy;									// indicates whether to add a copy button for this field
	bool disabled;									// indicates whether to disable the combo box
	bool is_filtered;								// indicates whether to use a filtered combo box
	F32 entry_width;								// combo box width
	U32 entry_align;								// width to which the combo box will be aligned
	U32 left_pad;									// width to which the descriptive text will be aligned
	char **available_values;						// possible parameter values
	char **visible_values;							// contents of the combo box (defaults to available_values if not specified)

	// source parameters
	EditorObject *source;							// holds source (if not the object itself)
	const char *property_name;						// the property to update with the contents of this parameter
	size_t struct_offset;							// if property name not set, this can be set to the address offset of a struct hanging off of the GroupDef
	ParseTable *struct_pti;							// if property name not set, ParseTable for the struct
	const char *struct_fieldname;					// if property name not set, name of the struct's field that this parameter is modifying
	int index;										// Usable by caller

	// custom callbacks - supply these if a property name is not specified
	WleAEParamComboUpdateFunc update_func;				// this callback should populate the param's data from actual data; should populate param with a const string (do NOT allocate new memory, as it will not be freed)
	void *update_data;								// this is passed to update_func
	WleAEParamComboApplyFunc apply_func;					// this callback should apply data from the param to actual data
	void *apply_data;								// this is passed to apply_func
	WleAECopyCallback copy_func;					// if specified, this is passed to the copy button instead of the default callback
	void *copy_data;								// if copy_func is specified, this is passed to the copy button instead of the default parameter
	WleAECopyPasteFreeCallback copy_free_func;		// if specified, this is passed to the copy button

	// internal data - READING ALLOWED, BUT NO TOUCHING!
	bool spec_diff;									// stores whether this parameter's is_specified value is different across multiple selections
	bool diff;										// stores whether this value is different across multiple selections
#endif
} WleAEParamCombo;
extern ParseTable parse_WleAEParamCombo[];
#define TYPE_parse_WleAEParamCombo WleAEParamCombo
#ifndef NO_EDITORS

void wleAEComboUpdate(WleAEParamCombo *param);
void wleAEComboAddWidgetEx(UIRebuildableTreeNode *auto_widget_node, const char *name, const char *tooltip, const char *param_name, WleAEParamCombo *param);
#define wleAEComboAddWidget(auto_widget_tree, name,tooltip,param_name,param) wleAEComboAddWidgetEx(auto_widget_tree->root, name,tooltip,param_name,param)
WleCriterion *wleAEComboRegisterFilter(WleAEParamCombo *param, const char *propertyName);

/********************
* DICTIONARY
********************/
#endif
AUTO_STRUCT;
typedef struct WleAEParamDictionary
{
	// primary data
	bool is_specified;				AST(BOOLFLAG)	// flag that is updated according to the checkbox created when can_unspecify is true
	char *refvalue;									// main data

	AST_STOP

#ifndef NO_EDITORS
	// functional/visual parameters
	bool can_unspecify;								// indicates whether the parameter can be unspecified (with a checkbox)
	bool disabled;									// indicates whether to disable the combo box
	F32 entry_width;								// combo box width
	U32 entry_align;								// width to which the combo box will be aligned
	U32 left_pad;									// width to which the descriptive text will be aligned
	DictionaryHandleOrName dictionary;				// dictionary from which to get available values
	char *parse_name_field;							// parse table field used to display combo box contents

	// source parameters
	//EditorObject *source;							// holds source (if not the object itself)
	const char *property_name;						// the property to update with the contents of this parameter
	size_t struct_offset;							// if property name not set, this can be set to the address offset of a struct hanging off of the GroupDef
	ParseTable *struct_pti;							// if property name not set, ParseTable for the struct
	const char *struct_fieldname;					// if property name not set, name of the struct's field that this parameter is modifying
	int index;										// Usable by caller

	// custom callbacks - supply these if a property name is not specified
	WleAEParamDictionaryUpdateFunc update_func;			// this callback should populate the param's data from actual data
	void *update_data;								// this is passed to update_func
	WleAEParamDictionaryApplyFunc apply_func;			// this callback should apply data from the param to actual data
	void *apply_data;								// this is passed to apply_func

	// internal data - READING ALLOWED, BUT NO TOUCHING!
	bool spec_diff;									// stores whether this parameter's is_specified value is different across multiple selections
	bool diff;										// stores whether this value is different across multiple selections
#endif
} WleAEParamDictionary;
extern ParseTable parse_WleAEParamDictionary[];
#define TYPE_parse_WleAEParamDictionary WleAEParamDictionary
#ifndef NO_EDITORS

void wleAEDictionaryUpdate(WleAEParamDictionary *param);
void wleAEDictionaryAddWidgetEx(UIRebuildableTreeNode *auto_widget_node, const char *name, const char *tooltip, const char *param_name, WleAEParamDictionary *param, bool bNewLine);
#define wleAEDictionaryAddWidget(auto_widget_tree, name,tooltip,param_name,param) wleAEDictionaryAddWidgetEx(auto_widget_tree->root, name,tooltip,param_name,param,true)

/********************
* VEC3
********************/
#endif
AUTO_STRUCT;
typedef struct WleAEParamVec3
{
	// primary data
	bool is_specified;				AST(BOOLFLAG)	// flag that is updated according to the checkbox created when can_unspecify is true
	Vec3 vecvalue;									// main data

	AST_STOP

#ifndef NO_EDITORS
	// functional/visual parameters
	bool can_unspecify;								// indicates whether the parameter can be unspecified (with a checkbox)
	bool disabled;									// indicates whether to disable input into this field
	bool can_copy;									// indicates whether to add a copy button for this field
	F32 entry_width;								// width of the input field
	U32 entry_align;								// width to which the first input field will be aligned
	U32 left_pad;									// width to which the descriptive text will be aligned
	U32 precision;									// precision of inputs appearing in the parameter

	// source parameters
	EditorObject *source;							// holds source (if not the object itself)
	const char *property_name;						// the property to update with the contents of this parameter
	size_t struct_offset;							// if property name not set, this can be set to the address offset of a struct hanging off of the GroupDef
	ParseTable *struct_pti;							// if property name not set, ParseTable for the struct
	const char *struct_fieldname;					// if property name not set, name of the struct's field that this parameter is modifying
	int index;										// Usable by caller

	// custom callbacks - supply these if a property name is not specified
	WleAEParamVec3UpdateFunc update_func;					// this callback should populate the param's data from actual data
	void *update_data;								// this is passed to update_func
	WleAEParamVec3ApplyFunc apply_func;					// this callback should apply data from the param to actual data
	void *apply_data;								// this is passed to apply_func
	WleAECopyCallback copy_func;					// if specified, this is passed to the copy button instead of the default callback
	void *copy_data;								// if copy_func is specified, this is passed to the copy button instead of the default parameter
	WleAECopyPasteFreeCallback copy_free_func;		// if specified, this is passed to the copy button
		
	// internal data - READING ALLOWED, BUT NO TOUCHING!
	bool spec_diff;									// stores whether the specified flag is different across multiple selections
	bool diff[3];									// stores whether a particular value is different across multiple selections
#endif
} WleAEParamVec3;
extern ParseTable parse_WleAEParamVec3[];
#define TYPE_parse_WleAEParamVec3 WleAEParamVec3
#ifndef NO_EDITORS

void wleAEVec3Update(WleAEParamVec3 *param);
void wleAEVec3AddWidgetEx(UIRebuildableTreeNode *auto_widget_node, const char *name, const char *tooltip, const char *param_name, WleAEParamVec3 *param, const Vec3 min, const Vec3 max, const Vec3 step);
#define wleAEVec3AddWidget(auto_widget_tree, name,tooltip,param_name,param,min,max,step) wleAEVec3AddWidgetEx(auto_widget_tree->root, name,tooltip,param_name,param,min,max,step)

/********************
* HSV
********************/
#endif
AUTO_STRUCT;
typedef struct WleAEParamHSV
{
	// primary data
	bool is_specified;				AST(BOOLFLAG)	// flag that is updated according to the checkbox created when can_unspecify is true
	Vec4 hsvvalue;					AST(FORMAT_HSV) // main data

	AST_STOP
#ifndef NO_EDITORS
	// functional/visual parameters
	bool can_unspecify;								// indicates whether the parameter can be unspecified (with a checkbox)
	bool disabled;									// indicates whether to disable input into this field
	bool add_alpha;									// indicates whether to allow editing of the alpha field
	F32 entry_width;								// width of the input field
	U32 entry_align;								// width to which the first input field will be aligned
	U32 left_pad;									// width to which the descriptive text will be aligned

	// source parameters
	EditorObject *source;							// holds source (if not the object itself)
	const char *property_name;						// the property to update with the contents of this parameter
	size_t struct_offset;							// if property name not set, this can be set to the address offset of a struct hanging off of the GroupDef
	ParseTable *struct_pti;							// if property name not set, ParseTable for the struct
	const char *struct_fieldname;					// if property name not set, name of the struct's field that this parameter is modifying
	int index;										// Usable by caller

	// custom callbacks - supply these if a property name is not specified
	WleAEParamHSVUpdateFunc update_func;					// this callback should populate the param's data from actual data
	void *update_data;								// this is passed to update_func
	WleAEParamHSVApplyFunc apply_func;					// this callback should apply data from the param to actual data
	void *apply_data;								// this is passed to apply_func

	// internal data - READING ALLOWED, BUT NO TOUCHING!
	bool spec_diff;									// stores whether the specified flag is different across multiple selections
	bool diff;										// stores whether a particular value is different across multiple selections
#endif
} WleAEParamHSV;
extern ParseTable parse_WleAEParamHSV[];
#define TYPE_parse_WleAEParamHSV WleAEParamHSV
#ifndef NO_EDITORS

void wleAEHSVUpdate(WleAEParamHSV *param);
void wleAEHSVAddWidgetEx(UIRebuildableTreeNode *auto_widget_node, const char *name, const char *tooltip, const char *param_name, WleAEParamHSV *param);
#define wleAEHSVAddWidget(auto_widget_tree, name,tooltip,param_name,param) wleAEHSVAddWidgetEx(auto_widget_tree->root, name,tooltip,param_name,param)
void wleAEHueChanged(WleAEParamHue *param);

/********************
* HUE
********************/
#endif
AUTO_STRUCT;
typedef struct WleAEParamHue
{
	// primary data
	bool is_specified;				AST(BOOLFLAG)	// flag that is updated according to the checkbox created when can_unspecify is true
	float huevalue;									// main data

	AST_STOP

#ifndef NO_EDITORS
	// functional/visual parameters
	bool can_unspecify;								// indicates whether the parameter can be unspecified (with a checkbox)
	bool disabled;									// indicates whether to disable input into this field
	F32 entry_width;								// width of the input field
	U32 slider_width;								// width of the slider
	U32 entry_align;								// width to which the first input field will be aligned
	U32 left_pad;									// width to which the descriptive text will be aligned
	U32 precision;									// precision of inputs appearing in the parameter

	// source parameters
	const char *property_name;						// the property to update with the contents of this parameter
	size_t struct_offset;							// if property name not set, this can be set to the address offset of a struct hanging off of the GroupDef
	ParseTable *struct_pti;							// if property name not set, ParseTable for the struct
	const char *struct_fieldname;					// if property name not set, name of the struct's field that this parameter is modifying
	int index;										// Usable by caller

	// custom callbacks - supply these if a property name is not specified
	WleAEParamHueUpdateFunc update_func;					// this callback should populate the param's data from actual data
	void *update_data;								// this is passed to update_func
	WleAEParamHueApplyFunc apply_func;					// this callback should apply data from the param to actual data
	void *apply_data;								// this is passed to apply_func

	// internal data - READING ALLOWED, BUT NO TOUCHING!
	bool spec_diff;									// stores whether the specified flag is different across multiple selections
	bool diff;										// stores whether a particular value is different across multiple selections
#endif
} WleAEParamHue;
extern ParseTable parse_WleAEParamHue[];
#define TYPE_parse_WleAEParamHue WleAEParamHue
#ifndef NO_EDITORS

void wleAEHueUpdate(WleAEParamHue *param);
void wleAEHueAddWidgetEx(UIRebuildableTreeNode *auto_widget_node, const char *name, const char *tooltip, const char *param_name, WleAEParamHue *param, float min, float max, float step);
#define wleAEHueAddWidget(auto_widget_tree, name,tooltip,param_name,param,min,max,step) wleAEHueAddWidgetEx(auto_widget_tree->root, name,tooltip,param_name,param,min,max,step)

/********************
* EXPRESSION
********************/
#endif
AUTO_STRUCT;
typedef struct WleAEParamExpression
{
	// primary data
	bool is_specified;				AST(BOOLFLAG)	// flag that is updated according to the checkbox created when can_unspecify is true
	Expression *exprvalue;			AST(LATEBIND)	// main data

	AST_STOP

#ifndef NO_EDITORS
	// functional/visual parameters
	ExprContext *context;							// context used for the expression
	bool can_unspecify;								// indicates whether the parameter can be unspecified (with a checkbox)
	bool disabled;									// indicates whether to disable input into this field
	F32 entry_width;								// width of the input field
	U32 entry_align;								// width to which the first input field will be aligned
	U32 left_pad;									// width to which the descriptive text will be aligned

	// source parameters
	//EditorObject *source;							// holds source (if not the object itself)
	const char *property_name;						// the property to update with the contents of this parameter
	size_t struct_offset;							// if property name not set, this can be set to the address offset of a struct hanging off of the GroupDef
	ParseTable *struct_pti;							// if property name not set, ParseTable for the struct
	const char *struct_fieldname;					// if property name not set, name of the struct's field that this parameter is modifying
	int index;										// Usable by caller

	// custom callbacks - supply these if a property name is not specified
	WleAEParamExpressionUpdateFunc update_func;			// this callback should populate the param's data from actual data
	void *update_data;								// this is passed to update_func
	WleAEParamExpressionApplyFunc apply_func;			// this callback should apply data from the param to actual data
	void *apply_data;								// this is passed to apply_func

	// internal data - READING ALLOWED, BUT NO TOUCHING!
	bool spec_diff;									// stores whether the specified flag is different across multiple selections
	bool diff;										// stores whether a particular value is different across multiple selections
#endif
} WleAEParamExpression;
extern ParseTable parse_WleAEParamExpression[];
#define TYPE_parse_WleAEParamExpression WleAEParamExpression
#ifndef NO_EDITORS

void wleAEExpressionUpdate(WleAEParamExpression *param);
void wleAEExpressionAddWidgetEx(UIRebuildableTreeNode *auto_widget_node, const char *name, const char *tooltip, const char *param_name, WleAEParamExpression *param);
#define wleAEExpressionAddWidget(auto_widget_tree, name,tooltip,param_name,param) wleAEExpressionAddWidgetEx(auto_widget_tree->root, name,tooltip,param_name,param)


/********************
* TEXTURE
********************/
#endif
AUTO_STRUCT;
typedef struct WleAEParamTexture
{
	// primary data
	bool is_specified;				AST(BOOLFLAG)	// flag that is updated according to the checkbox created when can_unspecify is true
	char *texturename;								// main data

	AST_STOP

#ifndef NO_EDITORS
	// functional/visual parameters
	bool can_unspecify;								// indicates whether the parameter can be unspecified (with a checkbox)
	bool disabled;									// indicates whether to disable input into this field
	U32 texture_size;								// length of one side of the texture preview box
	U32 entry_align;								// width to which the first input field will be aligned
	U32 left_pad;									// width to which the descriptive text will be aligned

	// source parameters
	EditorObject *source;							// holds source (if not the object itself)
	const char *property_name;						// the property to update with the contents of this parameter
	size_t struct_offset;							// if property name not set, this can be set to the address offset of a struct hanging off of the GroupDef
	ParseTable *struct_pti;							// if property name not set, ParseTable for the struct
	const char *struct_fieldname;					// if property name not set, name of the struct's field that this parameter is modifying
	int index;										// Usable by caller

	// custom callbacks - supply these if a property name is not specified
	WleAEParamTextureUpdateFunc update_func;				// this callback should populate the param's data from actual data
	void *update_data;								// this is passed to update_func
	WleAEParamTextureApplyFunc apply_func;				// this callback should apply data from the param to actual data
	void *apply_data;								// this is passed to apply_func

	// internal data - READING ALLOWED, BUT NO TOUCHING!
	bool spec_diff;									// stores whether the specified flag is different across multiple selections
	bool diff;										// stores whether a particular value is different across multiple selections
#endif
} WleAEParamTexture;
extern ParseTable parse_WleAEParamTexture[];
#define TYPE_parse_WleAEParamTexture WleAEParamTexture
#ifndef NO_EDITORS

void wleAETextureUpdate(WleAEParamTexture *param);
void wleAETextureAddWidgetEx(UIRebuildableTreeNode *auto_widget_node, const char *name, const char *tooltip, const char *param_name, WleAEParamTexture *param);
#define wleAETextureAddWidget(auto_widget_tree, name,tooltip,param_name,param) wleAETextureAddWidgetEx(auto_widget_tree->root, name,tooltip,param_name,param)

/********************
* PICKER
********************/
#endif
AUTO_STRUCT;
typedef struct WleAEParamPicker
{
	// primary data
	bool is_specified;				AST(BOOLFLAG)	// flag that is updated according to the checkbox created when can_unspecify is true

	AST_STOP
#ifndef NO_EDITORS
	void *object;									// main data selected from 
	ParseTable *object_parse_table;					// parse table for main data
	char *object_name;								// name of main data, found from object_parse_table and parse_name_field

	// functional/visual parameters
	bool can_unspecify;								// indicates whether the parameter can be unspecified (with a checkbox)
	bool disabled;									// indicates whether to disable input into this field
	U32 entry_align;								// width to which the first input field will be aligned
	U32 left_pad;									// width to which the descriptive text will be aligned
	EMPicker *picker;								// the picker to pop up when the button is pressed
	const char *parse_name_field;					// the field name in the parse table that is used to display a name in the button

	// source parameters
	EditorObject *source;							// holds source (if not the object itself)
	int index;										// Usable by caller

	// custom callbacks
	WleAEParamPickerUpdateFunc update_func;				// this callback should populate the param's data from actual data
	void *update_data;								// this is passed to update_func
	WleAEParamPickerApplyFunc apply_func;				// this callback should apply data from the param to actual data
	void *apply_data;								// this is passed to apply_func

	// internal data - READING ALLOWED, BUT NO TOUCHING!
	bool spec_diff;									// stores whether the specified flag is different across multiple selections
	bool diff;										// stores whether a particular value is different across multiple selections
#endif
} WleAEParamPicker;
extern ParseTable parse_WleAEParamPicker[];
#define TYPE_parse_WleAEParamPicker WleAEParamPicker
#ifndef NO_EDITORS

void wleAEPickerUpdate(WleAEParamPicker *param);
void wleAEPickerAddWidgetEx(UIRebuildableTreeNode *auto_widget_node, const char *name, const char *tooltip, const char *param_name, WleAEParamPicker *param);
#define wleAEPickerAddWidget(auto_widget_tree, name,tooltip,param_name,param) wleAEPickerAddWidgetEx(auto_widget_tree->root, name,tooltip,param_name,param)

/********************
* MESSAGE
********************/
#endif
AUTO_STRUCT;
typedef struct WleAEParamMessage
{
	// primary data
	bool is_specified;				AST(BOOLFLAG)	// flag that is updated according to the checkbox created when can_unspecify is true

	AST_STOP

#ifndef NO_EDITORS
	// functional/visual parameters
	bool can_unspecify;								// indicates whether the parameter can be unspecified (with a checkbox)
	bool disabled;									// indicates whether to disable input into this field
	F32 entry_width;								// width of the input field
	U32 entry_align;								// width to which the first input field will be aligned
	U32 left_pad;									// width to which the descriptive text will be aligned

	// source parameters
	size_t struct_offset;							// if property name not set, this can be set to the address offset of a struct hanging off of the GroupDef
	ParseTable *struct_pti;							// if property name not set, ParseTable for the struct
	const char *struct_fieldname;					// if property name not set, name of the struct's field that this parameter is modifying
	int index;										// Usable by caller

	// custom callbacks - supply these if a property name is not specified
	WleAEParamMessageUpdateFunc update_func;				// this callback should populate param.message's data from actual data
	void *update_data;								// this is passed to update_func
	WleAEParamMessageApplyFunc apply_func;				// this callback should apply data from param.message to actual data
	void *apply_data;								// this is passed to apply_func

	// internal data - READING ALLOWED, BUT NO TOUCHING!
	UIMessageEntry *pMsgEntry;
	Message message;
	const char *source_key;

	bool spec_diff;									// stores whether the specified flag is different across multiple selections
	bool key_diff;									// stores whether the message key is different across multiple selections
	bool scope_diff;								// stores whether the scope is different across mulitple selections
	bool desc_diff;									// stores whether the message description is different across multiple selections
	bool default_str_diff;							// stores whether the default string is different across multiple selections
#endif
} WleAEParamMessage;
extern ParseTable parse_WleAEParamMessage[];
#define TYPE_parse_WleAEParamMessage WleAEParamMessage
#ifndef NO_EDITORS

void wleAEMessageUpdate(WleAEParamMessage *param);
void wleAEMessageAddWidgetEx(UIRebuildableTreeNode *auto_widget_node, const char *name, const char *tooltip, const char *param_name, WleAEParamMessage *param, bool bNewLine);
#define wleAEMessageAddWidget(auto_widget_tree, name,tooltip,param_name,param) wleAEMessageAddWidgetEx(auto_widget_tree->root, name,tooltip,param_name,param,true)

/********************
* GAME ACTION
********************/
#endif
AUTO_STRUCT;
typedef struct WleAEParamGameAction
{
	// primary data
	bool is_specified;				AST(BOOLFLAG)	// flag that is updated according to the checkbox created when can_unspecify is true
	WorldGameActionBlock *action_block;			AST(LATEBIND)	// main data

	AST_STOP

#ifndef NO_EDITORS

	// functional/visual parameters
	bool can_unspecify;								// indicates whether the parameter can be unspecified (with a checkbox)
	bool disabled;									// indicates whether to disable input into this field
	F32 entry_width;								// width of the input field
	U32 entry_align;								// width to which the first input field will be aligned
	U32 left_pad;									// width to which the descriptive text will be aligned

	// source parameters
	EditorObject *source;							// holds source (if not the object itself)
	const char *property_name;						// the property to update with the contents of this parameter
	size_t struct_offset;							// if property name not set, this can be set to the address offset of a struct hanging off of the GroupDef
	ParseTable *struct_pti;							// if property name not set, ParseTable for the struct
	const char *struct_fieldname;					// if property name not set, name of the struct's field that this parameter is modifying
	int index;										// Usable by caller

	// custom callbacks - supply these if a property name is not specified
	WleAEParamGameActionUpdateFunc update_func;			// this callback should populate the param's data from actual data
	void *update_data;								// this is passed to update_func
	WleAEParamGameActionApplyFunc apply_func;			// this callback should apply data from the param to actual data
	void *apply_data;								// this is passed to apply_func

	// internal data - READING ALLOWED, BUT NO TOUCHING!
	UIGameActionEditButton *pActionButton;
	WorldGameActionBlock *action_block_temp;		// data used by internal logic
	const char *source_key;
	bool spec_diff;									// stores whether the specified flag is different across multiple selections
	bool diff;										// stores whether a particular value is different across multiple selections
#endif
} WleAEParamGameAction;
extern ParseTable parse_WleAEParamGameAction[];
#define TYPE_parse_WleAEParamGameAction WleAEParamGameAction
#ifndef NO_EDITORS


void wleAEFixupGameActionMessageKey(WorldGameActionBlock* block, GroupDef* def, const char* pcMessageScope);
void wleAEGameActionUpdate(WleAEParamGameAction *param);
void wleAEGameActionAddWidgetEx(UIRebuildableTreeNode *auto_widget_node, const char *name, const char *tooltip, const char *param_name, WleAEParamGameAction *param);
#define wleAEGameActionAddWidget(auto_widget_tree, name,tooltip,param_name,param) wleAEGameActionAddWidgetEx(auto_widget_tree->root, name,tooltip,param_name,param)

/********************
* WORLD VARIABLE DEF
********************/
#endif
AUTO_STRUCT;
typedef struct WleAEParamWorldVariableDef
{
	// primary data
	bool is_specified;				AST(BOOLFLAG)	// flag that is updated according to the checkbox created when can_unspecify is true
	WorldVariableDef var_def;		AST(STRUCT(parse_WorldVariableDef))

	bool display_if_unspecified;	AST(BOOLFLAG)	// if true, the current var_def will be displayed as uneditable labels if is_specified is false;

	AST_STOP

#ifndef NO_EDITORS
	int index;										// Usable by caller
	const char* key;								// Usable by caller (var def name)

	// functional/visual parameters
	bool can_unspecify;								// indicates whether the parameter can be unspecified (with a checkbox)
	bool disabled;									// indicates whether to disable input into this field
	F32 entry_width;								// width of the input field
	U32 entry_align;								// width to which the first input field will be aligned
	U32 left_pad;									// width to which the descriptive text will be aligned
	const char* source_map_name;					// map the WorldVariableDef is on -- used to figure out which map variables are avaialable
	const char* dest_map_name;						// map the WorldVariableDef will be setting for -- used to figure out what map variables are available
	WorldVariableDef** eaDefList;					// list of WorldVariableDefs used if no dest_map_name is specified
	const char* scope;								// specified scope for message variables
	
	bool no_name;									// don't show the var name field

	// custom callbacks - supply these if a property name is not specified
	WleAEParamWorldVariableDefUpdateFunc update_func;		// this callback should populate the param's data from actual data
	void *update_data;								// this is passed to update_func
	WleAEParamWorldVariableDefApplyFunc apply_func;		// this callback should apply data from the param to actual data
	void *apply_data;								// this is passed to apply_func
	WleAEObjGetTrackerFunc tracker_func;			// If specified, this function is used to get the tracker handle

	// internal data - READING ALLOWED, BUT NO TOUCHING!
	WleAEParamInt int_param;
	WleAEParamFloat float_param;
	WleAEParamText string_param;
	WleAEParamDictionary dict_param;
	WleAEParamMessage message_param;
	
	char** source_map_variables;
	const char** dest_map_variables;
	char** choice_table_names;
	const char** mission_variables;
	char** map_point_spawn_points;
	int choice_table_index_max;
	
	bool spec_diff;									// stores whether the specified flag is different across multiple selections
	bool var_name_diff;								// stores whether this parameter's variable name is different across multiple selections
	bool var_init_from_diff;
	bool var_value_diff;							// stores whether this parameter is inited in different ways across multiple selections
	bool var_type_diff;								// stores whether this parameter has a different type across multiple sections
#endif
} WleAEParamWorldVariableDef;
extern ParseTable parse_WleAEParamWorldVariableDef[];
#define TYPE_parse_WleAEParamWorldVariableDef WleAEParamWorldVariableDef
#ifndef NO_EDITORS

void wleAEWorldVariableDefUpdate(WleAEParamWorldVariableDef *param);
void wleAEWorldVariableDefAddWidgetEx(UIRebuildableTreeNode *auto_widget_node, const char *name, const char *tooltip, const char *inheritedText, const char *param_name, WleAEParamWorldVariableDef *param);
#define wleAEWorldVariableDefAddWidget(auto_widget_tree, name,tooltip,param_name,param) wleAEWorldVariableDefAddWidgetEx(auto_widget_tree->root, name,tooltip,NULL,param_name,param)
bool wleAEWorldVariableDefDoorHasVarsDisabled(WleAEParamWorldVariableDef *param);
WorldVariable* wleAEWorldVariableCalcVariableNonRandom( WorldVariableDef* varDef );

#endif // NO_EDITORS

#endif // __WORLDEDITORATTRIBUTESHELPERS_H__
