#pragma once
GCC_SYSTEM

#include "wlTerrainBrush.h"
#include "wlTerrainSource.h"
#include "TerrainEditorState.h"
#include "TerrainEditorInterface.h"

#ifndef NO_EDITORS

#include "UILib.h"
#include "EditorManager.h"
#include "EditorManagerUtils.h"
#include "WorldGrid.h"

typedef struct TerrainSubtask TerrainSubtask;
typedef struct TerrainBrush TerrainBrush;
typedef enum TerrainBrushOpType TerrainBrushOpType;
typedef struct TerrainEditorSourceLayer TerrainEditorSourceLayer;
typedef struct GenesisToPlaceState GenesisToPlaceState;

#define BRUSH_PANE_OFFSET 24	//Distance to first Brush ButtonCombo //Must not be 0
#define BRUSH_PANE_DEPTH 48

#define MAX_BUTTONS 14
#define MAX_BRUSH_OPS 10

#define TER_ED_NAME "Terrain Editor"
#define MULTI_BRUSH_NAME "Multi Brush"

typedef enum TerrainTaskFlags
{
	TERRAIN_TASK_SHOW_PROGRESS = 1,
	TERRAIN_TASK_UNDOABLE = 2,
	TERRAIN_TASK_CONTROLLERSCRIPT = 4,
} TerrainTaskFlags;

typedef struct TerrainUI {

	//Store
	TerrainTokenStorageData **persistant_token_data;
	TerrainTokenStorageData **multi_brush_token_data;

	//Tool Bars
	EMToolbar *tool_bar_brush;
	EMToolbar **system_tool_bars;

	// Memory bar
	UIProgressBar *mem_progress;

	//Panels
	//EMPanel *panel_layers;
    EMPanel *panel_occlusion;
	EMPanel *panel_options;
	EMPanel *panel_extreme_angle_vis;
	EMPanel *panel_filters_vis;
	EMPanel *panel_materials_vis;
	EMPanel *panel_objects_vis;
	EMPanel *panel_global_filters;
	EMPanel *panel_multi_brush;
	EMPanel *panel_image_trans;
    EMPanel *panel_actions;
    EMPanel *panel_rivers;

	// Actions
	UIButton *apply_brush_button;
    UISliderTextEntry *apply_iterations_slider;

	//Brush Tool Bar
	UIDropSliderTextEntry *brush_size_slider;
	UIDropSliderTextEntry *brush_hardness_slider;
	UIDropSliderTextEntry *brush_strength_slider;
	UIComboBox *brush_shape_combo;
	UIButton *brush_eye_dropper;
	UIButtonCombo *brush_falloff_combo;

    //Rivers Pane
    UIList *river_list;
    UITextEntry *river_name_entry;
    UICheckButton *river_enabled_check;

	//Brush Pane
	UIPane *brush_pane;
	bool dragging_brush_pane;
	UIButtonCombo *channel_button_list[TBC_NUM_CHANNELS];

	//Material Picker
	EMPicker *material_picker;

	//Genesis UI
	EMPanel *genesis_panel;
	UIButton *genesis_import_mapdesc_button;
	UIButton *genesis_import_nodelayout_button;
	UIButton *genesis_export_nodelayout_button;
	UIButton *genesis_move_to_design_button;
	UIButton *genesis_move_to_detail_button;
	EMPicker *ecosystem_picker;
	EMPicker *geotype_picker;

	//Multi Brush UI
	UIWindow *rename_brush_window;
	UITextEntry *rename_brush_text;
	TerrainBrush *rename_brush;
	UIExpanderGroup *multi_brush_expanders;
	UIComboBox *multi_brush_combo;
	int multi_brush_header_height;
	int brush_header_height;

	//Image Gizmo UI
	UIComboBox *image_mode_combo;
	UITextEntry *image_pos_x_text;
	UITextEntry *image_pos_y_text;
	UITextEntry *image_pos_z_text;
	UITextEntry *image_rot_p_text;
	UITextEntry *image_rot_y_text;
	UITextEntry *image_rot_r_text;
	UITextEntry *image_scale_text;
	UIButton *image_done_button;

	//Options Panel
	UIComboBox *view_mode_combo;
	UICheckButton *hide_objects_check;

	//Select Block Window
	UIWindow *select_block_window;
	UISpinner *select_block_x;
	UISpinner *select_block_y;
	UISpinner *select_block_width;
	UISpinner *select_block_height;
	UITextEntry *select_block_x_entry;
	UITextEntry *select_block_y_entry;
	UITextEntry *select_block_width_entry;
	UITextEntry *select_block_height_entry;
    UIButton *select_block_button;
	UILabel *select_block_label;
    
	UICheckButton *new_block_subdivide;

	//Change Resolution Window
	UIWindow *resolution_window;
	UIComboBox *new_granularity_combo;
	UILabel *new_granularity_label;
	UILabel *new_granularity_label_2;

	//Object Vis
	UIComboBox *object_vis_combo;

	//Material Vis
	UIComboBox *material_vis_combo;

	//Filters Vis
	UIButton *filters_vis_refresh_button;

	//Debug Window
	UICheckButton *distributed_remesh_check;
	UICheckButton *remesh_higher_precision_check;

} TerrainUI;

typedef struct TerrainLayerUI
{
	EMPanel *panel;
	//UIList *layer_list;
	UIComboBox *mode_combo;
	UIComboBox *granularity_combo;
	char **granularity_model;
    UIButton *new_button;
    UIButton *delete_button;
    UIButton *add_terrain_button;
    UIButton *remove_terrain_button;
	UIButton *debug_terrain_button;
    UIButton *resample_button;
    UIButton *combine_button;
    UIButton *split_button;
    UIButton *export_button;
    UIButton *import_button;
    UICheckButton *playable_check;
    UIComboBox *exclusion_combo;
	UISliderTextEntry *hue_shift_slider;
} TerrainLayerUI;

typedef struct TerrainDoc
{
	EMEditorDoc base_doc;

	TerrainUI *terrain_ui;
	TerrainEditorState state;
	U32 lock_counter;
} TerrainDoc;

void terEdDoActions(TerrainDoc *doc, bool background_thread);
TerrainDoc *terEdGetDoc();
void terEdRefreshUI(TerrainDoc *doc);
void terEdUpdateSubDocs(TerrainDoc *doc);
bool terrainIsOverlappingBlock(IVec2 offset, IVec2 size);
EMPanel *terEdCreateLayersPanel(TerrainDoc *doc);
void terEdCreateChangeResolutionWindow(TerrainDoc *doc);
void terEdUpdateLayerButtons(TerrainDoc *doc);
bool terrainUnloadSourceLayer(TerrainDoc *doc, TerrainEditorSourceLayer *source);
void terEdLayerSelected(UIList *list, void *userdata);
void terrainFinishAssociateLayer(TerrainDoc *doc, TerrainEditorSourceLayer *source);
void terEdCreateBlockSelectWindow(TerrainDoc *doc);
void terEdDoImportReload(TerrainDoc *doc);
bool terEdCheckForLayerUpdates(TerrainDoc *doc);
void terEdQueueFillWithBrush(UIButton *button, void *unused);
void terEdQueueStitchNeighbors(UIButton *button, void *unused);
void terEdQueueDrawObject( TerrainDoc *doc, F32 *buffer, F32 falloff, S32 x_offset, U32 x_size, S32 z_offset, U32 z_size);
F32 terEdGetFilterVisValue(TerrainEditorState* state, F32 x, F32 y);
void terEdCompileBrush(TerrainDoc *doc);
void terEdUseEyeDropper(TerrainEditorState* state, F32 x, F32 y);
TerrainCompiledMultiBrush *terEdGetMultibrush(TerrainDoc *doc, TerrainCompiledMultiBrush *multibrush);
TerrainCommonBrushParams *terEdGetBrushParams(TerrainDoc *doc, TerrainCommonBrushParams *params);

// TerrainEditorRiver.c
void deinit_rivers(TerrainDoc *doc);
void terrainRiversRefreshDoc(TerrainDoc *doc);
void terrainUIDrawCurve(RiverCurve *curve, bool river_mode);
void terrainUpdateRiverPoints(RiverCurve *curve);
EMPanel *terEdCreateRiversPanel(TerrainDoc *doc);

// TerrainEditorBrushUI.c
void terEdMultiBrushInit();
void terEdInitImageGizmo(TerrainDoc *doc);
void terEdInitBrushUI(TerrainDoc *doc);
void terEdFillOpNamesList(TerrainDoc *doc);
void terEdSetBrushHeight(TerrainEditorState *state, TerrainDefaultBrush *selected_brush, F32 x, F32 z);
void terEdSetBrushColor(TerrainEditorState *state, TerrainDefaultBrush *selected_brush, F32 x, F32 z);
void terEdSetBrushMaterial(TerrainEditorState *state, TerrainDefaultBrush *selected_brush, F32 x, F32 z);
void terEdSetBrushObject(TerrainEditorState *state, TerrainDefaultBrush *selected_brush, F32 x, F32 z);
void terEdSetBrushSoilDepth(TerrainEditorState *state, TerrainDefaultBrush *selected_brush, F32 x, F32 z);
void terEdRefreshBrushUI(TerrainDoc *doc);
void terEdRefreshDefaultBrushesUI();
void terEdRefreshActiveMultiBrushUI(TerrainDoc *doc, TerrainMultiBrush *expanded);
void terEdExpandActiveMultiBrush(TerrainDoc *doc, TerrainMultiBrush *active);
void terrainObjectsComboMakeText(UIComboBox *combo, S32 row, bool inBox, void *unused, char **output);
void terEdFillCommonBrushParams(TerrainCommonBrushParams *common, const char *brush_name);
void terEdSelectMultiBrushByName(TerrainDoc *doc, const char *name);
TerrainMultiBrush *terEdGetMultiBrushByName(const char *name);
void terrainSelectBrush(const char *brush_name);
void terEdRefreshDefaultBrushUI(TerrainDefaultBrush *selected_brush);
TerrainCommonBrushParams* terEdGetCommonBrushParams(TerrainDoc *doc);

//TerrainEditor
void terEdWaitForQueuedEvents(TerrainDoc *doc);
void terEdDeleteAndPopulateGroup(TerrainEditorSource *source, GenesisToPlaceState *to_place, bool nodes, bool detail);

// TerrainEditorLayer.c
int terEdPopulateMapPanel(EMPanel *panel);
TerrainLayerUI *terEdGetLayerUI();
void terEdHighlightBlocks(HeightMapTracker *tracker);
bool terrainLoadSourceLayer(TerrainDoc *doc, ZoneMapLayer *layer, bool asynchronous);

// EditorManagerUIPanels.c
void emPanelsGetMapSelectedLayers(ZoneMapLayer ***layer_list);

#endif
