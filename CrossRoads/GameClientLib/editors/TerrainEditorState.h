#pragma once
GCC_SYSTEM

#include "referencesystem.h"
#include "wlEditorIncludes.h"
#include "wlTerrainBrush.h"

typedef struct TerrainDefaultBrush TerrainDefaultBrush;
typedef struct TerrainCompiledBrush TerrainCompiledBrush;
typedef struct TerrainCompiledMultiBrush TerrainCompiledMultiBrush;
typedef struct TerrainBrush TerrainBrush;
typedef struct TerrainBrushStringRef TerrainBrushStringRef;
typedef struct TerrainUndoEvent TerrainUndoEvent;
typedef struct ZoneMapLayer ZoneMapLayer;
typedef struct TerrainBuffer TerrainBuffer;
typedef struct EditorObject EditorObject;
typedef struct Curve Curve;
typedef struct River River;
typedef struct TerrainMultiBrush TerrainMultiBrush;
typedef struct TerrainBrushValues TerrainBrushValues;
typedef enum TerrainBrushShape TerrainBrushShape;
typedef enum TerrainEditorImageGizmoMode TerrainEditorImageGizmoMode;
typedef struct GfxTerrainViewMode GfxTerrainViewMode;
typedef struct EditUndoStack EditUndoStack;
typedef struct RotateGizmo RotateGizmo;
typedef struct TranslateGizmo TranslateGizmo;
typedef struct TerrainExportInfo TerrainExportInfo;
typedef struct TerrainEditorSource TerrainEditorSource;
typedef struct TerrainEditorSourceLayer TerrainEditorSourceLayer;
typedef struct GenesisDesignData GenesisDesignData;
typedef struct TerrainSlopeBrushParams TerrainSlopeBrushParams;
typedef struct TerrainImageBuffer TerrainImageBuffer;
typedef struct TerrainChangeList TerrainChangeList;
typedef struct GenesisZoneNodeLayout GenesisZoneNodeLayout;
typedef struct GenesisEcotype GenesisEcotype;
typedef struct TerrainTaskQueue TerrainTaskQueue;

#ifndef NO_EDITORS

#define STATE_NUM_CHANNELS 7	//should always equal (TBC_NUM_CHANNELS-1)

#endif
#ifndef NO_EDITORS

typedef struct RiverCurve {
	TerrainEditorSourceLayer *layer;
	bool enabled;
	bool selected;
    /*River *river;
	Spline *curve;
	F32 *points;
	F32 *l_points;
	F32 *r_points;
	F32 *widths;
    S32 *point_indices;*/
} RiverCurve;

typedef struct TerrainEditorPersistentState {

	//Multi Brush
	TerrainMultiBrush *expanded_multi_brush;
	char **multi_brush_filtered_list;
	char *multi_brush_filter;
	TerrainBrush *copied_brush;

	//Default Brushes
	TerrainDefaultBrush *last_selected_brush[STATE_NUM_CHANNELS];

	//Other
	char **materialsList;
	TerrainObjectEntry **objectList;

} TerrainEditorPersistentState;

typedef struct TerrainEditorChannelOpsList {
	TerrainBrushStringRef **op_refs;
} TerrainEditorChannelOpsList;

typedef struct TerrainEditorState {
	// Layer data
    TerrainEditorSource*		source;

    TerrainDefaultBrush *		selected_brush;
    TerrainCompiledMultiBrush *	compiled_multibrush;
	TerrainEditorPersistentState *persistent;
	TerrainCommonBrushParams	multi_brush_common;

	//Slope Brush
    TerrainSlopeBrushParams *	slope_brush_params;

	//Image Gizmo
	RotateGizmo *				image_rotate_gizmo;
	TranslateGizmo *			image_translate_gizmo;
	TerrainBrushValues *		seleted_image;
	Mat4 						gizmo_matrix;
	TerrainEditorImageGizmoMode gizmo_mode;
	EditUndoStack*				image_undo_stack;		//undo stack for the image
	TerrainBrushValues	*		image_orig_vals;

	TerrainEditorChannelOpsList	channel_ops[STATE_NUM_CHANNELS];

	EditUndoStack*				undo_stack;
    
	bool 						keep_high_res;  //When upsampling do we preserve higher res data
    F32							river_max_width;
    RiverCurve **				river_curves;
    S32 						river_point_selected;

    TerrainExportInfo *			import_info;
    char *						color_import; // When non-NULL, we are currently importing a color TIFF
    char *						height_import; // When non-NULL, we are currently importing a height TIFF
    bool 						color_needs_reload;
    bool 						height_needs_reload;

	bool						editable;
	bool						painting;

	bool						has_focus;

	bool						lock_edges;
	bool						invert_filters;
	bool						hide_edges;
	bool						show_erode;

	HeightMap*					last_cursor_heightmap;
	Vec3						last_cursor_position;
	Vec2						mouse_pos;				//Updated instead when cursor position is locked
	Vec2						last_mouse_pos;

	bool						using_eye_dropper;

	bool						genesis_create_detail_objects;

	bool						new_block_mode;
    bool						remove_block_mode;
	bool						split_block_mode;
	F32							select_block_dims[4];

	bool						memory_limit;

	bool						genesis_preview_flag;

	TerrainTaskQueue*			task_queue;

} TerrainEditorState;

typedef struct TerrainDoc TerrainDoc;

#endif
