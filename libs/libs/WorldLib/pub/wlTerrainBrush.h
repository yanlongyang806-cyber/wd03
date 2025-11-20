#pragma once
GCC_SYSTEM

//To add a new brush:
//Add Enum Val to TerrainFunctionName
//Create a function: examples can be found at "terEdApply" + enum name; for example terEdApplyHeightAdd
//Add your function and enum to the terEdGetFunctionFromName function

#include "ReferenceSystem.h"
#include "structDefines.h"

#define DEFAULT_BRUSH_DICTIONARY "DefaultBrush"
#define MULTI_BRUSH_DICTIONARY "MultiBrush"

typedef struct EMToolbar EMToolbar;
typedef struct UIExpander UIExpander;
typedef void UIAnyWidget;
typedef struct UIButton UIButton;
typedef struct TerrainBrush TerrainBrush;
typedef struct HeightMapCache HeightMapCache;
typedef struct TerrainBrushTemplateParam TerrainBrushTemplateParam;
typedef struct TerrainImageBuffer TerrainImageBuffer;
typedef struct TerrainTokenStorageData TerrainTokenStorageData;
typedef struct TerrainEditorSource TerrainEditorSource;
typedef struct TerrainSlopeBrushParams TerrainSlopeBrushParams;
typedef struct TerrainEditorSource TerrainEditorSource;
typedef struct TerrainFilterBlobSphere TerrainFilterBlobSphere;
typedef struct TerrainCompiledBrushOp TerrainCompiledBrushOp;
typedef struct Spline Spline;
typedef struct TerrainBrushFilterCache TerrainBrushFilterCache;

typedef void (*TerrainUIRefreshFunc)(UIAnyWidget *, TerrainTokenStorageData *);

typedef enum TerrainBrushFalloffTypes {
	TE_FALLOFF_SCURVE=0,
	TE_FALLOFF_LINEAR,
	TE_FALLOFF_CONVEX,
	TE_FALLOFF_CONCAVE,
} TerrainBrushFalloffTypes;

AUTO_ENUM;
typedef enum TerrainBrushShape {
	TBS_Circle = 0,
	TBS_Square,
} TerrainBrushShape;
extern StaticDefineInt TerrainBrushShapeEnum[];

typedef struct TerrainCommonBrushParams
{
	F32 brush_diameter;
	F32 brush_hardness;
	F32 brush_strength;
	TerrainBrushShape brush_shape;
    bool invert_filters;
	bool lock_edges;
	TerrainBrushFalloffTypes falloff_type;
} TerrainCommonBrushParams;

AUTO_ENUM;
typedef enum TerrainEditorImageGizmoMode {
	TGM_Translate=0,
	TGM_Rotate,
} TerrainEditorImageGizmoMode;
extern StaticDefineInt TerrainEditorImageGizmoModeEnum[];

AUTO_ENUM;
typedef enum TerrainUIType {
	TUI_UNDEFINED = 0,
	TUI_SliderTextEntry,
	TUI_CheckButton,
	TUI_ColorButton,
	TUI_MaterialPicker,
	TUI_ObjectPicker,
	TUI_ImageFilePicker,
	TUI_ImageTransButton,
	TUI_TextEntry,
	TUI_ReseedButton,
} TerrainUIType;

AUTO_ENUM;
typedef enum TerrainBrushChannel {
	TBC_Height = 0,		//WARNING:	If you change the size of this list, 
	TBC_Color,			//			you need to set STATE_NUM_CHANNELS to be TBC_NUM_CHANNELS-1 (in TerrainEditorState.h)
	TBC_Material,
	TBC_Object,
	TBC_Soil,
	TBC_Alpha,
    TBC_Select,
	TBC_Custom,			//Custom must be last and is assumed to be (TBC_NUM_CHANNELS-1)
	TBC_NUM_CHANNELS,	
} TerrainBrushChannel;
extern StaticDefineInt TerrainBrushChannelEnum[];

AUTO_ENUM;
typedef enum TerrainFunctionName {
	TFN_HeightAdd = 0,
	TFN_HeightFlatten,
	TFN_HeightFlattenBlob,
	TFN_HeightWeather,
	TFN_HeightSmooth,
	TFN_HeightRoughen,
	TFN_HeightErode,
	TFN_HeightGrab,
	TFN_HeightSlope,
	TFN_HeightSmudge,
	TFN_HeightTerrace,
	TFN_ColorSet,
	TFN_ColorBlend,
	TFN_ColorImage,
	TFN_MaterialSet,
	TFN_MaterialReplace,
	TFN_ObjectSet,
	TFN_ObjectEraseAll,
	TFN_AlphaCut,
    TFN_Select,
	TFN_FilterAngle,
	TFN_FilterAltitude,
	TFN_FilterObject,
	TFN_FilterMaterial,
	TFN_FilterPerlinNoise,
	TFN_FilterRidgedNoise,
	TFN_FilterFBMNoise,
	TFN_FilterImage,
    TFN_FilterSelection,
    TFN_FilterPath,
    TFN_FilterVolumeBlob,
    TFN_FilterShadow,
} TerrainFunctionName;

AUTO_ENUM;
typedef enum TerrainBrushBucket {
	TBK_OptimizedFilter = 0,
	TBK_RegularFilter,
	TBK_OptimizedBrush,
	TBK_RegularBrush,
	TBK_NUM_BRUSH_BUCKETS,
} TerrainBrushBucket;

typedef struct TerrainTokenStorageData
{
	ParseTable *tpi;
	int column;
	void* structptr;
	UIAnyWidget *linked_widget;
	const char *brush_name;
	const char *value_name;
} TerrainTokenStorageData;

AUTO_STRUCT;
typedef struct TerrainBrushTemplateParam {
	const char *display_name;					AST( NAME("DisplayName") )
	const char *tool_tip;						AST( NAME("ToolTip") )
	const char *value_name;						AST( NAME("ValueName") POOL_STRING )
	TerrainUIType ui_type;						AST( NAME("UIType") )
	F32 min_val;								AST( NAME("MinVal") )
	F32 max_val;								AST( NAME("MaxVal") )
	F32 step;									AST( NAME("Step") )
	F32 bias;									AST( NAME("Bias") )
	F32 bias_offset;							AST( NAME("BiasOffset") )
	TerrainUIRefreshFunc refresh_func;			NO_AST	//Only for default brush toolbar widgets
	UIAnyWidget *widget_ptr;					NO_AST	
	TerrainTokenStorageData *data;				NO_AST
} TerrainBrushTemplateParam;
extern ParseTable parse_TerrainBrushTemplateParam[];
#define TYPE_parse_TerrainBrushTemplateParam TerrainBrushTemplateParam

AUTO_STRUCT;
typedef struct TerrainBrushTemplate {
	const char *image;							AST( NAME("Image") )
	TerrainFunctionName function;				AST( NAME("Function") )
	TerrainBrushChannel channel;				AST( NAME("Channel") )
	TerrainBrushBucket bucket;					AST( NAME("Bucket") )
	TerrainBrushTemplateParam **params;			AST( NAME("Param") )
} TerrainBrushTemplate;
extern ParseTable parse_TerrainBrushTemplate[];
#define TYPE_parse_TerrainBrushTemplate TerrainBrushTemplate

AUTO_STRUCT;
typedef struct TerrainObjectRef {
	int name_uid;								AST( NAME(UID)	)
	char *name_str;								AST( NAME(Name) )
} TerrainObjectRef;
extern ParseTable parse_TerrainObjectRef[];
#define TYPE_parse_TerrainObjectRef TerrainObjectRef

typedef struct TerrainImageBuffer {
	TerrainEditorSource *source;
	char *file_name;
	U8 *buffer;
	U32 width;
	U32 height;
	U32 ref_count;
	U32 needs_reload;
} TerrainImageBuffer;

AUTO_STRUCT;
typedef struct TerrainBrushValues {
	bool active;								NO_AST
	TerrainImageBuffer *image_ref;				NO_AST
	F32 strength;								//Currently, only used for filters
	F32 float_1;
	F32 float_2;
	F32 float_3;
	F32 float_4;
	F32 float_5;
	F32 float_6;
	F32 float_7;
	S32 int_1;
	S32 int_2;
	U8 bool_1;
	U8 bool_2;
	U8 bool_3;
	U8 bool_4;
	Color color_1;								AST( RGBA )
	Color color_2;								AST( RGBA )
	char *string_1;
	char *string_2;
	TerrainObjectRef object_1;
} TerrainBrushValues;
extern ParseTable parse_TerrainBrushValues[];
#define TYPE_parse_TerrainBrushValues TerrainBrushValues

AUTO_STRUCT;
typedef struct TerrainBrushFalloff {
	F32 diameter_multi;
	F32 hardness_multi;
	F32 strength_multi;
	bool invert_filters;
} TerrainBrushFalloff;
extern ParseTable parse_TerrainBrushFalloff[];
#define TYPE_parse_TerrainBrushFalloff TerrainBrushFalloff

typedef struct TerrainCompiledBrushOp
{
	void *draw_func;
	TerrainBrushChannel channel;
	TerrainBrushValues *values_copy;
	//Filters Only:
	TerrainCompiledBrushOp *op_with_cache;
	bool cached_uses_color;
	F32 cached_value;
} TerrainCompiledBrushOp;

typedef struct TerrainCompiledBrushBucket
{
	TerrainCompiledBrushOp **brush_ops;
} TerrainCompiledBrushBucket;

typedef struct TerrainCompiledBrush
{
	bool uses_color;
	TerrainBrushFalloff	falloff_values;
	TerrainCompiledBrushBucket bucket[TBK_NUM_BRUSH_BUCKETS];
} TerrainCompiledBrush;

typedef struct TerrainCompiledMultiBrush
{
    U32 brush_version;
    TerrainCompiledBrush **brushes;
	TerrainCompiledBrushOp **filter_list;
	bool alloced_filter_list;
} TerrainCompiledMultiBrush;

AUTO_STRUCT;
typedef struct TerrainDefaultBrush {
	char *name;									AST( NAME("Name") KEY )
	const char *filename;						AST( CURRENTFILE )
	char *display_name;							AST( NAME("DisplayName") )
	U8 order;									AST( NAME("Order") )
	const char *tool_tip;						AST( NAME("ToolTip") )
	TerrainBrushTemplate brush_template;		AST( NAME("BrushTemplate") )
	TerrainBrushValues default_values;			AST( NAME("BrushValues") )
	EMToolbar *toolbar;							NO_AST
	TerrainCommonBrushParams common;			NO_AST
	UIButton *button;							NO_AST
} TerrainDefaultBrush;
extern ParseTable parse_TerrainDefaultBrush[];
#define TYPE_parse_TerrainDefaultBrush TerrainDefaultBrush

AUTO_STRUCT;
typedef struct TerrainBrushOp {
	REF_TO(TerrainDefaultBrush) brush_base;		AST( NAME("BrushBase") )
	TerrainBrushValues values;					AST( NAME("BrushValues") )
	TerrainTokenStorageData **storage_data;		NO_AST		//NO AST items are for expanded brush only
	UIAnyWidget **widgets;						NO_AST
	UIExpander *expander;						NO_AST
	TerrainBrush *parent_brush;					NO_AST
} TerrainBrushOp;
extern ParseTable parse_TerrainBrushOp[];
#define TYPE_parse_TerrainBrushOp TerrainBrushOp

AUTO_STRUCT;
typedef struct TerrainBrush {
	char *name;									AST( NAME("Name") )
	TerrainBrushOp **ops;						AST( NAME("Operation") )
	TerrainBrushFalloff	falloff_values;			AST( NAME("Falloff") )
	bool disabled;								NO_AST
	TerrainTokenStorageData **storage_data;		NO_AST		//NO AST items are for expanded brush only
	UIExpander *expander;						NO_AST		
} TerrainBrush;
extern ParseTable parse_TerrainBrush[];
#define TYPE_parse_TerrainBrush TerrainBrush

AUTO_STRUCT;
typedef struct TerrainMultiBrush {
	const char *name;							AST( POOL_STRING KEY )
	const char *filename;						AST( CURRENTFILE )
	TerrainBrush **brushes;						AST( NAME("Brush") )
} TerrainMultiBrush;
extern ParseTable parse_TerrainMultiBrush[];
#define TYPE_parse_TerrainMultiBrush TerrainMultiBrush

AUTO_STRUCT;
typedef struct TerrainBrushFilterBuffer {
	F32 *buffer;
	U32 lod;
	S32 width;
	S32 height;
	S32 x_offset;
	S32 y_offset;
	F32 rel_x_cntr;
	F32 rel_y_cntr;
	bool invert;
	TerrainBrushFilterCache *optimized_cache;		NO_AST
} TerrainBrushFilterBuffer;
extern ParseTable parse_TerrainBrushFilterBuffer[];
#define TYPE_parse_TerrainBrushFilterBuffer TerrainBrushFilterBuffer

AUTO_STRUCT;
typedef struct TerrainBrushStringRef {
	char *op_name;								AST( NAME("OpName") )
	char *display_name;							AST( NAME("DisplayName") )
} TerrainBrushStringRef;
extern ParseTable parse_TerrainBrushStringRef[];
#define TYPE_parse_TerrainBrushStringRef TerrainBrushStringRef

typedef struct TerrainBrushCurveList {
	Spline **curves;
	F32 *lengths;
} TerrainBrushCurveList;

typedef struct TerrainBrushState {
    F32	vertical_offset; // Only set in the background thread
    F32	brush_center_height;
	U32	per_draw_frame_rand_val;
	int	visible_lod;
	bool cancel_action;

	TerrainBrushCurveList *curve_list;

	bool blob_list_inited;
	TerrainFilterBlobSphere **blob_list;
} TerrainBrushState;
extern ParseTable parse_TerrainBrushState[];
#define TYPE_parse_TerrainBrushState TerrainBrushState

typedef struct TerrainSlopeBrushParams {
	Vec3 brush_start_pos;
	Vec3 brush_end_pos;
} TerrainSlopeBrushParams;
extern ParseTable parse_TerrainSlopeBrushParams[];
#define TYPE_parse_TerrainSlopeBrushParams TerrainSlopeBrushParams

typedef struct UIWindow UIWindow;
typedef struct TerrainBrushUI TerrainBrushUI;
typedef struct TerrainEditorState TerrainEditorState;
typedef struct TerrainDoc TerrainDoc;
typedef struct EMToolbar EMToolbar;
typedef struct HeightMap HeightMap;

#ifndef NO_EDITORS

void terrainBrushInit();

void* terEdGetFunctionFromName(TerrainFunctionName name);
void terrainBrushCompile(TerrainCompiledMultiBrush *compiled_multibrush, TerrainDefaultBrush *selected_brush, TerrainMultiBrush *expanded_multi_brush);
void terEdUseBrush(TerrainEditorSource *source, TerrainBrushState *state, TerrainCompiledMultiBrush *multibrush, TerrainCommonBrushParams *common_params, F32 x, F32 z, bool reverse, bool start);
void terEdUseBrushFill(TerrainEditorSource *source, TerrainBrushState *state, TerrainCompiledMultiBrush *multibrush, F32 brush_strength, bool invert_filters, bool lock_edges, bool reverse);
void terEdUseBrushFillOptimized(TerrainEditorSource *source, TerrainBrushState *state, TerrainCompiledMultiBrush *multibrush, F32 brush_strength, bool invert_filters, bool lock_edges, bool reverse);
void terEdApplyHeightSlopeBrushUp(TerrainEditorSource *source, TerrainBrushState *state, TerrainSlopeBrushParams *params, TerrainCommonBrushParams *falloff_values, int ter_type);
bool terEdDoesBrushHaveSlope(TerrainCompiledMultiBrush *multibrush);
void terEdDestroyCompiledMultiBrush(TerrainCompiledMultiBrush *multibrush);
void terEdClearCompiledMultiBrush(TerrainCompiledMultiBrush *compiled_multibrush);
TerrainCompiledMultiBrush *terEdCopyCompiledMultiBrush(TerrainCompiledMultiBrush *multibrush);
void terEdCompileMultiBrush(TerrainCompiledMultiBrush *compiled_multibrush, TerrainMultiBrush *multi_brush, bool clear);
F32 terEdPerlinNoise(F32 x, F32 y);
void terEdApplyOptimizedBrush( TerrainEditorSource *source, TerrainBrushState *state, TerrainCompiledBrushOp **brush_ops, TerrainBrushFilterBuffer *filter, 
								TerrainBrushFalloff *falloff_values, F32 cx, F32 cz, bool square, TerrainBrushFalloffTypes falloff_type, 
								bool reverse, bool start, bool uses_color, bool filter_pass);
void terrainGetEditableBounds(TerrainEditorSource *source, Vec2 min_pos, Vec3 max_pos);
TerrainDefaultBrush *terEdSetFilterEnabled(const char *filter, bool enabled);

// Utility function to apply a multibrush at a single point
void terrainBrushApplyOptimized(TerrainEditorSource *source, TerrainBrushState *state, S32 fx, S32 fz, int color_step,
								TerrainBrushFilterBuffer *filter, HeightMapCache *cache,
								TerrainCompiledMultiBrush *multibrush, F32 brush_strength, 
								bool invert_filters, bool reverse);

#define OPTIMIZED_BRUSH_PARAMS TerrainEditorSource *source, TerrainBrushState *state, TerrainBrushValues *values, TerrainBrushFilterBuffer *filter, TerrainBrushChannel channel, F32 x, F32 z, S32 i, S32 j, F32 cx, F32 cz, F32 fall_off, HeightMapCache *cache, bool reverse, bool start, bool frame_start, F32 dist_to_center, bool uses_color
typedef void (*terrainOptimizedBrushFunction)(OPTIMIZED_BRUSH_PARAMS);
#define REGULAR_BRUSH_PARAMS TerrainEditorSource *source, TerrainBrushState *state, TerrainBrushValues *values, TerrainBrushFilterBuffer *filter, TerrainBrushChannel channel, TerrainBrushFalloff *falloff_values, F32 cx, F32 cz, bool square, TerrainBrushFalloffTypes falloff_type, bool reverse, bool start, bool uses_color
typedef void (*terrainRegularBrushFunction)(REGULAR_BRUSH_PARAMS);

U32 terrainBrushGetCompiledMemory(TerrainCompiledMultiBrush *multibrush);

void terrainRefBrushImage(TerrainImageBuffer *image_ref);
void terrainFreeBrushImage(TerrainImageBuffer *image_ref);
bool terrainLoadBrushImage(TerrainEditorSource *source, TerrainBrushValues *values);
int terrainCheckReloadBrushImages(TerrainEditorSource *source);//Must only be called inside a lock
TerrainMultiBrush *terrainGetMultiBrushByName(TerrainEditorSource *source, const char *name);

void terrainDestroyTerrainBrushOp(TerrainBrushOp *brush_op);
void terrainDestroyTerrainBrush(TerrainBrush *brush);
void terrainDestroyMultiBrush(TerrainMultiBrush *multi_brush);

#endif
