#pragma once
GCC_SYSTEM

#ifndef NO_EDITORS

#include "wlTerrainBrush.h"

typedef struct GenesisEcotype GenesisEcotype;
typedef struct GenesisZoneNodeLayout GenesisZoneNodeLayout;
typedef struct GroupDef GroupDef;
typedef struct ModelLOD ModelLOD;
typedef struct TerrainChangeList TerrainChangeList;
typedef struct TerrainCommonBrushParams TerrainCommonBrushParams;
typedef struct TerrainCompiledMultiBrush TerrainCompiledMultiBrush;
typedef struct TerrainEditorSource TerrainEditorSource;
typedef struct TerrainEditorSourceLayer TerrainEditorSourceLayer;
typedef struct TerrainSlopeBrushParams TerrainSlopeBrushParams;
typedef struct TerrainSubtask TerrainSubtask;
typedef struct TerrainTask TerrainTask;
typedef struct TerrainTaskQueue TerrainTaskQueue;
typedef struct VaccuformObjectInternal VaccuformObjectInternal;
typedef struct ZoneMapLayer ZoneMapLayer;

typedef void (*terrainTaskCreateCallback)(TerrainTask *new_task, TerrainSubtask *first_subtask, int flags);
typedef void (*terrainSubtaskCreateCallback)(TerrainTask *task, TerrainSubtask *subtask, int flags);
typedef void (*terrainTaskBeginCallback)(TerrainTask *task);
typedef void (*terrainTaskFinishCallback)(TerrainTask *task);
typedef void (*terrainSubtaskFinishCallback)(TerrainTask *task, TerrainSubtask *subtask);

typedef void (*terrainTaskCustomFinishCallback)(TerrainTask *task, void *userdata);

typedef enum TerrainSubtaskType {
    TE_ACTION_UPDATE_NORMALS,
    TE_ACTION_SLOPE_BRUSH,
	TE_ACTION_PAINT,
    TE_ACTION_FILL_PAINT,
    TE_ACTION_FILL_PAINT_OPTIMIZED,
	TE_ACTION_VACCUFORM,
    TE_ACTION_STITCH_NEIGHBORS,
	TE_ACTION_RESAMPLE,
    TE_ACTION_SAVE_LAYER,
	TE_ACTION_GENESIS_NODE_TO_DESIGN,
	TE_ACTION_UNDO,
	TE_ACTION_REDO,
} TerrainSubtaskType;

typedef struct TerrainSubtask {
	TerrainTask *task;
	TerrainSubtaskType op_type;
	union {
		struct {
			Vec3 world_pos;
			bool reverse;
		} PaintOp;
        struct {
            TerrainCompiledMultiBrush *brush;
			int index;
        } FillOp;
		struct {
			VaccuformObjectInternal *object;
			bool terrain_objects;
		} VaccuformOp;
        struct {
            TerrainSlopeBrushParams *params;
        } SlopeBrushOp;
        struct {
            TerrainEditorSourceLayer *layer;
			bool force;
        } SaveOp;
        struct {
            GenesisEcotype *ecotype;
			GenesisZoneNodeLayout *layout;
        } GenesisOp;
		struct {
			U32 lod;
		} ResampleOp;
		struct {
			TerrainChangeList *change_list;
		} UndoOp;
	};
} TerrainSubtask;

typedef struct TerrainTask {
	TerrainTaskQueue *queue;
    int undo_id;
    S32 lod;
	int flags;
    TerrainCommonBrushParams *common_params;
    TerrainCompiledMultiBrush *brush;
    TerrainSubtask **subtasks;
    TerrainChangeList *change_list;
	VaccuformObjectInternal **vaccuform_objects;
	S32	occlusion_draw_pos[2];
	U32 progress_id;

    bool keep_high_res;
    bool started;
    bool completed;
	bool force_completed;

	terrainTaskCustomFinishCallback finish_cb;
	void *finish_data;
} TerrainTask;

typedef struct TerrainTaskQueue {
	// Action queue
	TerrainTask **				edit_tasks;
    TerrainTask *				active_task;

	TerrainBrushState			current_state;

	// Flags
	bool						waiting_for_mouse_up;
	bool						needs_update;
	
	F32							vertical_offset;

	// Callbacks
	terrainTaskCreateCallback new_task_cb;
	terrainSubtaskCreateCallback new_subtask_cb;
	terrainTaskBeginCallback begin_task_cb;
	terrainTaskFinishCallback finish_task_cb;
	terrainSubtaskFinishCallback finish_subtask_cb;
} TerrainTaskQueue;

typedef struct VaccuformObject
{
	ModelLOD *model;
	Mat4 mat;
	F32 falloff;
	bool is_spline;
	Mat4 spline_matrices[2];
	char brush_name[64];
} VaccuformObject;

// Functions

void terrainQueueLock();
void terrainQueueUnlock();
void terrainQueueModifyQueueLock();
void terrainQueueModifyQueueUnlock();
void terrainQueueClearMouseUp(TerrainTaskQueue *queue);
bool terrainQueueNeedsMouseUp(TerrainTaskQueue *queue);
void terrainQueueClearUpdate(TerrainTaskQueue *queue);
bool terrainQueueNeedsUpdate(TerrainTaskQueue *queue);

TerrainTaskQueue *terrainQueueCreate();
void terrainQueueAddSubtask(TerrainTaskQueue *queue, TerrainSubtask* subtask,
							TerrainCompiledMultiBrush *multibrush, TerrainCommonBrushParams *params,
							int visible_lod, bool keep_high_res, int flags, bool priority);
void terrainQueueFreeSubtask(TerrainSubtask *subtask);
void terrainQueueTaskCompleted(TerrainTaskQueue *queue, TerrainTask *task);
U32 terrainQueueGetTaskMemory(TerrainTask *task);

void terrainQueueUndo(TerrainTaskQueue *queue, TerrainChangeList *change_list, int flags);
void terrainQueueRedo(TerrainTaskQueue *queue, TerrainChangeList *change_list, int flags);
void terrainQueueUpdateNormals(TerrainTaskQueue *queue, int flags);
void terrainQueueSlopeBrush(TerrainTaskQueue *queue, TerrainSlopeBrushParams *params, 
							TerrainCommonBrushParams *brush_params, int lod, 
							bool keep_high_res, int flags);
void terrainQueuePaint(TerrainTaskQueue *queue, Vec3 world_pos, 
					   bool reverse, bool painting, 
					   TerrainCompiledMultiBrush *multibrush, 
					   TerrainCommonBrushParams *params,
					   int lod, bool keep_high_res, int flags);
void terrainQueueFillWithMultiBrush(TerrainTaskQueue *queue, 
									TerrainCompiledMultiBrush *multi_brush, 
									TerrainCommonBrushParams *params, bool optimized, 
									int lod, bool keep_high_res, int flags);
void terrainQueueStitchNeighbors(TerrainTaskQueue *queue, int lod, int flags);
void terrainQueueGenesisNodesToDesign(TerrainTaskQueue *queue, TerrainEditorSource *source,
									  GenesisZoneNodeLayout *layout,
									  int lod, int flags);

//void terrainQueueVaccuformAllObjects(TerrainTaskQueue *queue, int flags, bool terrain_objects);
void terrainQueueVaccuformObject(TerrainTaskQueue *queue, TerrainEditorSource *source, VaccuformObject *object, int flags, bool priority);
void terrainQueueResample(TerrainTaskQueue *queue, U32 new_lod, int flags);
void terrainQueueSave(TerrainTaskQueue *queue, TerrainEditorSourceLayer *layer,
					bool force, int flags);

void terrainQueueFinishTask(TerrainTaskQueue *queue, terrainTaskCustomFinishCallback callback, void *userdata);

void terrainQueueClear(TerrainTaskQueue *queue);
bool terrainQueueIsEmpty(TerrainTaskQueue *queue);

void terrainQueueSetVerticalOffset(TerrainTaskQueue *queue, F32 offset);

void terrainQueueDoActions(TerrainTaskQueue *queue, bool background_thread,
						   TerrainEditorSource *source, bool terrain_occlusion);

void terrainQueueFindObjectsToVaccuformInDef(ZoneMapLayer *layer, GroupDef *def, Mat4 mat, VaccuformObject*** objects);
void terrainQueueFindObjectsToVaccuform(TerrainEditorSourceLayer *source_layer, VaccuformObject*** objects, bool terrain_objects);

const char *terrainQueueGetSubtaskLabel(TerrainSubtask *subtask);

#endif
