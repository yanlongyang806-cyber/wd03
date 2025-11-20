#pragma once
GCC_SYSTEM


#ifndef NO_EDITORS
typedef struct Room Room;
typedef struct RoomPartition RoomPartition;
typedef struct BasicTexture BasicTexture;

// typedef enum mapSnapSelectionState
// {
// 	MSNAP_Rectangle=0,
// 	MSNAP_Ellipse,
// 	MSNAP_Free,
// } mapSnapSelectionState;

typedef enum mapSnapViewMode
{
	MSNAP_Preview=0,
	MSNAP_Image,
	MSNAP_AllImages,
} mapSnapViewMode;

typedef struct mapSnapUIPicture
{
	UIWidget widget;
	AtlasTex *atlas_tex;
	BasicTexture *basic_tex;
} mapSnapUIPicture;

// #endif
// AUTO_STRUCT;
// typedef struct mapSnapFOW
// {
// 	char *display_name;									AST( NAME("DisplayName") )
// 	Vec2 world_pos;										NO_AST
// 	IVec2 pixel_pos;									NO_AST
// 	BasicTexture *texture;								NO_AST
// 	U8 *buffer;											NO_AST
// //	bool unsaved;										NO_AST
// } mapSnapFOW;
// #ifndef NO_EDITORS

#endif
AUTO_STRUCT;
typedef struct mapSnapPartition
{
	char *display_name;					AST(NAME("Name"))
	char *layer_name;					AST(NAME("LayerName"))
	char *unique_name;					AST(NAME("UniqueName"))
	const char *region_name;			NO_AST
	Vec3 bounds_min;					NO_AST
	Vec3 bounds_mid;					NO_AST
	Vec3 bounds_max;					NO_AST
	U8 *buffer;							NO_AST
	bool unsaved;						NO_AST
//	mapSnapFOW **fow_data;				NO_AST
} mapSnapPartition;
#ifndef NO_EDITORS

typedef enum mapSnapUndoType
{
	MSNAP_UndoSelection=0,
	MSNAP_UndoSnap,
} mapSnapUndoType;

typedef struct mapSnapUndoData
{
	mapSnapUndoType undo_type;
	mapSnapPartition *partition;
	U8 *buffer;
	int buf_w, buf_h;
} mapSnapUndoData;

typedef struct mapSnapDoc {
	// NOTE: This must be first for EDITOR MANAGER
	EMEditorDoc base_doc;

	const char **region_names;
	char **actions;
	mapSnapPartition **master_partition_list;
	mapSnapPartition **current_partition_list;
	mapSnapPartition *selected_partition;
//	mapSnapFOW **selected_fow_data;

	//Preview
	mapRoomPhoto *room_photo_being_taken;
	mapRoomPhotoAction action;
	int picture_frame_count;

	//Options
	Vec2 position_offset;
	F32 padding;
	mapSnapViewMode view_mode;
	//Zmap Options
	F32 last_near_offset;
	F32 last_far_offset;

	//State Data
	mapSnapSelectionType selection_state;
	BasicTexture *scene_tex;
	Vec3 reg_min;
	Vec3 reg_mid;
	Vec3 reg_max;

	//Selection Data
	Vec2 selection_min;			//As percentage of bounds
	Vec2 selection_max;
	F32 *point_list;
	bool point_list_closed;
//	BasicTexture *sel_tex;
// 	U8 *sel_buffer;
	U8 key_shift : 1;
	U8 key_ctrl : 1;
	U8 key_near_add : 1;
	U8 key_near_sub : 1;
	U8 key_far_add : 1;
	U8 key_far_sub : 1;

	//UI
	EMPanel *view_panel;
	EMPanel *action_panel;
	EMPanel *global_panel;
	EMPanel *preview_panel;
//	EMPanel *fow_panel;
	UIComboBox *region_combo;
	UICheckButton *whole_region_check;
	UIList *room_list_box;
//	UIList *fow_list;
//	UITextEntry *fow_name_entry;
//	UIButton *fow_cut_button;
//	UIButton *fow_merge_button;
	UISliderTextEntry *near_plane_slider;
	UISliderTextEntry *far_plane_slider;
	UISliderTextEntry *g_near_plane_slider;
	UISliderTextEntry *g_far_plane_slider;
	UIList *action_list_box;
	UIButton *action_up_button;
	UIButton *action_down_button;
	UIButton *action_add_button;
	UIButton *action_remove_button;
	mapSnapUIPicture *picture_ui;

} mapSnapDoc;

#endif