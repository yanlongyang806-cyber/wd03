#define GENESIS_ALLOW_OLD_HEADERS
#ifndef NO_EDITORS

#include "wlTerrainQueue.h"

#include "error.h"
#include "wlEditorIncludes.h"
#include "wlGenesisExteriorNode.h"
#include "wlModelInline.h"
#include "wlTerrain.h"
#include "wlTerrainBrush.h"
#include "wlTerrainSource.h"

AUTO_RUN_ANON(memBudgetAddMapping(__FILE__, BUDGET_Editors););

static CRITICAL_SECTION terrain_editor_critical_section;
static CRITICAL_SECTION terrain_editor_modify_queue_critical_section;
static TerrainCompiledMultiBrush *pCurrentCompiledBrush = NULL;

void terrainQueueLock()
{
	EnterCriticalSection(&terrain_editor_critical_section);
}

void terrainQueueUnlock()
{
	LeaveCriticalSection(&terrain_editor_critical_section);
}

void terrainQueueModifyQueueLock()
{
	EnterCriticalSection(&terrain_editor_modify_queue_critical_section);
}

void terrainQueueModifyQueueUnlock()
{
	LeaveCriticalSection(&terrain_editor_modify_queue_critical_section);
}

AUTO_RUN;
void terrainQueueInit(void)
{
	InitializeCriticalSection(&terrain_editor_critical_section);
	InitializeCriticalSection(&terrain_editor_modify_queue_critical_section);
}

void terrainQueueClearMouseUp(TerrainTaskQueue *queue)
{
    queue->waiting_for_mouse_up = false;
}

bool terrainQueueNeedsMouseUp(TerrainTaskQueue *queue)
{
	return queue->waiting_for_mouse_up;
}

void terrainQueueClearUpdate(TerrainTaskQueue *queue)
{
	queue->needs_update = false;
}

bool terrainQueueNeedsUpdate(TerrainTaskQueue *queue)
{
	return queue->needs_update;
}

TerrainTaskQueue *terrainQueueCreate()
{
	return calloc(1, sizeof(TerrainTaskQueue));
}

void terrainQueueAddSubtask(TerrainTaskQueue *queue, TerrainSubtask* subtask,
							TerrainCompiledMultiBrush *multibrush, TerrainCommonBrushParams *params,
							int visible_lod, bool keep_high_res, int flags, bool priority)
{
    TerrainTask *task;
    terrainQueueLock();
	terrainQueueModifyQueueLock();
    if (queue->active_task == NULL)
    {
        task = calloc(1, sizeof(TerrainTask));
		task->queue = queue;
        task->lod = visible_lod;
		task->common_params = params;
		task->brush = multibrush;
		task->flags = flags;

        task->change_list = calloc(1, sizeof(TerrainChangeList));
        task->change_list->draw_lod = visible_lod;
        task->change_list->keep_high_res = keep_high_res;
		task->change_list->ref_count = 1;

		if (priority && eaSize(&queue->edit_tasks) > 2)
		{
			eaInsert(&queue->edit_tasks, task, 1);
		}
		else
		{
	        eaPush(&queue->edit_tasks, task);
		}

        queue->active_task = task;

		if (queue->new_task_cb)
			queue->new_task_cb(task, subtask, flags);
    }
    else
	{
        task = queue->active_task;
		
		if (!task->common_params)
			task->common_params = params;

		if (task->brush)
		{
			if (multibrush)
				terEdDestroyCompiledMultiBrush(multibrush);
		}
		else
			task->brush = multibrush;

		if (task->lod < 0)
			task->lod = visible_lod;
	}

	if (task->change_list->draw_lod == -1)
		task->change_list->draw_lod = visible_lod;

	if (queue->new_subtask_cb)
		queue->new_subtask_cb(task, subtask, flags);

	eaPush(&task->subtasks, subtask);
	subtask->task = task;
	terrainQueueModifyQueueUnlock();
    terrainQueueUnlock();
}

void terrainQueueFreeSubtask(TerrainSubtask *subtask)
{
	if (subtask->task->queue->finish_subtask_cb)
	{
		subtask->task->queue->finish_subtask_cb(subtask->task, subtask);
	}
    if (subtask->op_type == TE_ACTION_SLOPE_BRUSH)
    {
        SAFE_FREE(subtask->SlopeBrushOp.params);
    }
    else if (subtask->op_type == TE_ACTION_FILL_PAINT ||
             subtask->op_type == TE_ACTION_FILL_PAINT_OPTIMIZED)
        terEdDestroyCompiledMultiBrush(subtask->FillOp.brush);
    SAFE_FREE(subtask);
}

void terrainQueueTaskCompleted(TerrainTaskQueue *queue, TerrainTask *task)
{
	terrainQueueModifyQueueLock();

	if (task == queue->active_task)
		queue->active_task = NULL;

	if (queue->finish_task_cb)
	{
		queue->finish_task_cb(task);
	}
	if (task->finish_cb)
	{
		task->finish_cb(task, task->finish_data);
	}
    eaDestroyEx(&task->subtasks, terrainQueueFreeSubtask);

	//terrainUpdateTaskMemory();
	if (task->brush)
		terEdDestroyCompiledMultiBrush(task->brush);
	if (task->change_list && (--task->change_list->ref_count == 0))
	{
		//printf("terrainSourceChangeListDestroy %X\n", (int)(intptr_t)task->change_list);
		terrainSourceChangeListDestroy(task->change_list);
	}
	SAFE_FREE(task->common_params);
	SAFE_FREE(task);

	eaRemove(&queue->edit_tasks, 0);

	terrainQueueModifyQueueUnlock();
}


U32 terrainQueueGetTaskMemory(TerrainTask *task)
{
    int i, j;
    U32 size = sizeof(TerrainTask);
	terrainQueueModifyQueueLock();
    for (i = 0; i < eaSize(&task->subtasks); i++)
    {
        TerrainSubtask *subtask = task->subtasks[i]; 
        size += sizeof(TerrainSubtask);
        if (subtask->op_type == TE_ACTION_SLOPE_BRUSH)
        {
            size += sizeof(TerrainSlopeBrushParams);
        }
        else if (subtask->op_type == TE_ACTION_FILL_PAINT ||
                 subtask->op_type == TE_ACTION_FILL_PAINT_OPTIMIZED)
        {
            if (subtask->FillOp.brush)
            	size += terrainBrushGetCompiledMemory(subtask->FillOp.brush);
        }
    }
    if (task->brush)
        size += terrainBrushGetCompiledMemory(task->brush);
    if (task->change_list)
    {
        size += sizeof(TerrainChangeList);
        if (task->change_list->mat_change_list)
            size += eaSize(&task->change_list->mat_change_list->list) * sizeof(TerrainMaterialChange);
        size += eaSize(&task->change_list->added_mat_list.list) * sizeof(TerrainMaterialChange);
        if (task->change_list->map_backup_list)
        {
            for (i = 0; i < eaSize(&task->change_list->map_backup_list->list); i++)
            {
                size += sizeof(HeightMapBackup) +
                    eaSize(&task->change_list->map_backup_list->list[i]->backup_buffers)*sizeof(TerrainBuffer);
                for (j = 0; j < eaSize(&task->change_list->map_backup_list->list[i]->backup_buffers); j++)
                	size += GetTerrainBufferSize(task->change_list->map_backup_list->list[i]->backup_buffers[j]);
            }
        }
    }
	terrainQueueModifyQueueUnlock();
    return size;
}

// Wrappers for adding subtask to queue

void terrainQueueUndo(TerrainTaskQueue *queue, TerrainChangeList *change_list, int flags)
{
    TerrainSubtask *action = calloc(sizeof(*action), 1);
    action->op_type = TE_ACTION_UNDO;
	action->UndoOp.change_list = change_list;
	terrainQueueAddSubtask(queue, action, NULL, NULL, -1, true, flags, false);
    terrainQueueFinishTask(queue, NULL, NULL);
}

void terrainQueueRedo(TerrainTaskQueue *queue, TerrainChangeList *change_list, int flags)
{
    TerrainSubtask *action = calloc(sizeof(*action), 1);
    action->op_type = TE_ACTION_REDO;
	action->UndoOp.change_list = change_list;
	terrainQueueAddSubtask(queue, action, NULL, NULL, -1, true, flags, false);
    terrainQueueFinishTask(queue, NULL, NULL);
}

void terrainQueueUpdateNormals(TerrainTaskQueue *queue, int flags)
{
    TerrainSubtask *action = calloc(sizeof(*action), 1);
    action->op_type = TE_ACTION_UPDATE_NORMALS;
	terrainQueueAddSubtask(queue, action, NULL, NULL, -1, true, flags, false);
}

void terrainQueueSlopeBrush(TerrainTaskQueue *queue, TerrainSlopeBrushParams *params, TerrainCommonBrushParams *brush_params, int lod, bool keep_high_res, int flags)
{
	TerrainSubtask *action = calloc(sizeof(*action), 1);
	action->op_type = TE_ACTION_SLOPE_BRUSH;
    action->SlopeBrushOp.params = params;
	terrainQueueAddSubtask(queue, action, NULL, NULL, lod, keep_high_res, flags, false);
	//printf("Queued slope brush.\n");
}

void terrainQueuePaint(TerrainTaskQueue *queue, Vec3 world_pos, 
					   bool reverse, bool painting, 
					   TerrainCompiledMultiBrush *multibrush, 
					   TerrainCommonBrushParams *params,
					   int lod, bool keep_high_res, int flags)
{
	TerrainSubtask *action = calloc(sizeof(*action), 1);
	action->op_type = TE_ACTION_PAINT;
	copyVec3(world_pos, action->PaintOp.world_pos);
	action->PaintOp.reverse = reverse;
	terrainQueueAddSubtask(queue, action, multibrush, params, lod, keep_high_res, flags, false);
}

void terrainQueueFillWithMultiBrush(TerrainTaskQueue *queue, 
									TerrainCompiledMultiBrush *multi_brush, 
									TerrainCommonBrushParams *params, bool optimized, 
									int lod, bool keep_high_res, int flags)
{
	TerrainSubtask *action = calloc(sizeof(*action), 1);
	action->op_type = optimized ? TE_ACTION_FILL_PAINT_OPTIMIZED : TE_ACTION_FILL_PAINT;
	action->FillOp.brush = multi_brush;
	terrainQueueAddSubtask(queue, action, NULL, params, lod, keep_high_res, flags, false);
}

void terrainQueueStitchNeighbors(TerrainTaskQueue *queue, int lod, int flags)
{
    TerrainSubtask *action = calloc(sizeof(*action), 1);
    action->op_type = TE_ACTION_STITCH_NEIGHBORS;
	terrainQueueAddSubtask(queue, action, NULL, NULL, lod, false, flags, false);
}

void terrainQueueGenesisNodesToDesign(TerrainTaskQueue *queue, TerrainEditorSource *source,
									  GenesisZoneNodeLayout *layout,
									  int lod, int flags)
{
	TerrainSubtask *action = calloc(sizeof(*action), 1);
	action->op_type = TE_ACTION_GENESIS_NODE_TO_DESIGN;
	action->GenesisOp.layout = layout;
	terrainQueueAddSubtask(queue, action, NULL, NULL, lod, true, flags, false);
}

void terrainQueueResample(TerrainTaskQueue *queue, U32 new_lod, int flags)
{
	TerrainSubtask *action = calloc(sizeof(*action), 1);
	action->op_type = TE_ACTION_RESAMPLE;
	action->ResampleOp.lod = new_lod;
	terrainQueueAddSubtask(queue, action, NULL, NULL, -1, true, flags, false);
    terrainQueueFinishTask(queue, NULL, NULL);
    //printf("Queued resample.\n");
}

void terrainQueueSave(TerrainTaskQueue *queue, TerrainEditorSourceLayer *layer,
					bool force, int flags)
{
    TerrainSubtask *action = calloc(sizeof(*action), 1);
    action->op_type = TE_ACTION_SAVE_LAYER;
    action->SaveOp.layer = layer;
	action->SaveOp.force = force;
	terrainQueueAddSubtask(queue, action, NULL, NULL, -1, true, flags, false);
    terrainQueueFinishTask(queue, NULL, NULL);
}

void terrainQueueVaccuformObject(TerrainTaskQueue *queue, TerrainEditorSource *source, VaccuformObject *object, int flags, bool priority)
{
	int i, count = 0;
	const Vec3 *positions;
	const U32 *tris;
	VaccuformObjectInternal *new_object;

	TerrainSubtask *action = calloc(sizeof(*action), 1);
	action->op_type = TE_ACTION_VACCUFORM;
	new_object = action->VaccuformOp.object = calloc(1, sizeof(VaccuformObjectInternal));

	new_object->falloff = CLAMP(object->falloff, 4, 100)/4;

	if (strcmp(object->brush_name, ""))
	{
		TerrainMultiBrush *multi_brush = terrainGetMultiBrushByName(source, object->brush_name);
		if (multi_brush)
		{
	        new_object->multibrush = calloc(1, sizeof(TerrainCompiledMultiBrush));
			terrainBrushCompile(new_object->multibrush, NULL, multi_brush);
			terrainDestroyMultiBrush(multi_brush);
		}
		else
		{
			Errorf("Could not find brush %s, referenced in vaccuform object.", object->brush_name);
		}
	}

	new_object->vert_count = object->model->vert_count;
	new_object->tri_count = object->model->tri_count;

	modelLockUnpacked(object->model);
	positions = modelGetVerts(object->model);
	tris = modelGetTris(object->model);

	new_object->verts = calloc(new_object->vert_count*3, sizeof(F32));
	new_object->inds = calloc(new_object->tri_count*3, sizeof(U32));

	memcpy(new_object->inds, tris, new_object->tri_count*3*sizeof(U32));

	setVec3(new_object->max, -1e8, -1e8, -1e8);
	setVec3(new_object->min, 1e8, 1e8, 1e8);

	// Transform & find min/max
	for (i = 0; i < new_object->vert_count; i++)
	{
		if (object->is_spline)
		{
			Vec3 in = { positions[i][0], positions[i][1], 0 };
			Vec3 up, tangent;
			splineEvaluate(object->spline_matrices, -positions[i][2]/object->spline_matrices[1][2][0],
				in, &new_object->verts[i*3], up, tangent);
		}
		else
		{
			mulVecMat4(positions[i], object->mat, &new_object->verts[i*3]);
		}
		new_object->verts[i*3+0] *= 0.25f;
		new_object->verts[i*3+2] *= 0.25f;
		vec3RunningMinMax(&new_object->verts[i*3], new_object->min, new_object->max);
	}
	modelUnlockUnpacked(object->model);

	if (new_object->max[0] >= new_object->min[0] && 
		new_object->max[1] >= new_object->min[1] && 
		new_object->max[2] >= new_object->min[2])
		terrainQueueAddSubtask(queue, action, NULL, NULL, -1, true, flags, priority);
	else
	{
		SAFE_FREE(new_object->verts);
		SAFE_FREE(new_object->inds);
		SAFE_FREE(new_object);
		SAFE_FREE(action);
	}
}

// Finish the active task
void terrainQueueFinishTask(TerrainTaskQueue *queue, terrainTaskCustomFinishCallback callback, void *userdata)
{
    if (queue->active_task)
    {
        queue->active_task->completed = true;
		queue->active_task->finish_cb = callback;
		queue->active_task->finish_data = userdata;
        queue->active_task = NULL;
    }
}

// Clear the current task queue
void terrainQueueClear(TerrainTaskQueue *queue)
{
    int i, j;
    terrainQueueLock();
	terrainQueueModifyQueueLock();
    for (i = 0; i < eaSize(&queue->edit_tasks); i++)
    {
		TerrainTask *task = queue->edit_tasks[i];
		for (j = 0; j < eaSize(&task->subtasks); j++)
		{
			if (task->subtasks[j]->op_type != TE_ACTION_UNDO &&
				task->subtasks[j]->op_type != TE_ACTION_REDO)
			{
				terrainQueueFreeSubtask(task->subtasks[j]);
			}
		}
		eaDestroy(&task->subtasks);
        queue->edit_tasks[i]->completed = true;
		queue->edit_tasks[i]->force_completed = true;
   }
	queue->current_state.cancel_action++;
	terrainQueueModifyQueueUnlock();
	terrainQueueUnlock();
}

// Are all the tasks completed and destroyed?
bool terrainQueueIsEmpty(TerrainTaskQueue *queue)
{
	return (eaSize(&queue->edit_tasks) == 0);
}

void terrainQueueSetVerticalOffset(TerrainTaskQueue *queue, F32 offset)
{
    terrainQueueLock();
	queue->vertical_offset += offset;
    terrainQueueUnlock();
}

// Do the actual tasks
void terrainQueueDoActions(TerrainTaskQueue *queue, bool background_thread,
						   TerrainEditorSource *source, bool terrain_occlusion)
{
	memset(&queue->current_state, 0, sizeof(TerrainBrushState));
	queue->current_state.cancel_action = 0;

    terrainQueueLock();
	terrainQueueModifyQueueLock();
	terrainCheckReloadBrushImages(source);
    queue->current_state.vertical_offset = queue->vertical_offset;
	queue->vertical_offset = 0;
	if (eaSize(&queue->edit_tasks) > 0) 
	{
        bool first_frame = false;
        bool last_frame = false;
		TerrainTask *task = queue->edit_tasks[0];
        TerrainSubtask *subtask = NULL;

		queue->current_state.visible_lod = task->lod;

		if (eaSize(&task->subtasks) > 0)
            subtask = eaRemove(&task->subtasks, 0);
        if (!task->started)
        {
            first_frame = true;
            task->started = true;
			if (!task->force_completed && queue->begin_task_cb)
				queue->begin_task_cb(task);
        }
        if (!subtask && task->completed)
            last_frame = true;

		terrainQueueModifyQueueUnlock();
        terrainQueueUnlock();
        
        if (first_frame && task->brush)
        {
            pCurrentCompiledBrush = task->brush;
        }

        if (subtask)
        {
            switch (subtask->op_type) 
            {
				xcase TE_ACTION_UNDO:
				{
           			terrainUndoMaterialChanges(NULL, subtask->UndoOp.change_list->mat_change_list, true); // "Post" Undo
					terrainSourceUndoBackupBuffer(source, subtask->UndoOp.change_list->map_backup_list);
					terrainUndoMaterialChanges(NULL, &subtask->UndoOp.change_list->added_mat_list, true); // "Pre" Undo
				}
				xcase TE_ACTION_REDO:
				{
					terrainUndoMaterialChanges(NULL, &subtask->UndoOp.change_list->added_mat_list, false); // "Pre" Redo
					terrainSourceUndoBackupBuffer(source, subtask->UndoOp.change_list->map_backup_list);
					terrainUndoMaterialChanges(NULL, subtask->UndoOp.change_list->mat_change_list, false); // "Post" Redo
				}
                xcase TE_ACTION_UPDATE_NORMALS:
                {
                    int i;
                    for (i = 0; i < eaSize(&source->layers); i++)
                    {
                        if (source->layers[i]->effective_mode == LAYER_MODE_EDITABLE)
                            terrainSourceUpdateNormals(source->layers[i], false);
                    }
                }
                xcase TE_ACTION_FILL_PAINT:
                {
					assert(task->common_params);
					assert(task->lod >= 0);
                    if (subtask->FillOp.brush)
                        pCurrentCompiledBrush = subtask->FillOp.brush;
                    terEdUseBrushFill( source, &queue->current_state, pCurrentCompiledBrush, task->common_params->brush_strength, task->common_params->invert_filters, task->common_params->lock_edges, subtask->PaintOp.reverse);
                }
                xcase TE_ACTION_FILL_PAINT_OPTIMIZED:
                {
					assert(task->common_params);
					assert(task->lod >= 0);
                    if (subtask->FillOp.brush)
                        pCurrentCompiledBrush = subtask->FillOp.brush;
                    terEdUseBrushFillOptimized( source, &queue->current_state, pCurrentCompiledBrush, task->common_params->brush_strength, task->common_params->invert_filters, task->common_params->lock_edges, subtask->PaintOp.reverse);
                }
                xcase TE_ACTION_PAINT:
                {         
					assert(task->lod >= 0);
                    if (terrain_occlusion)
                    {
						if (first_frame)
							task->occlusion_draw_pos[0] = task->occlusion_draw_pos[1] = -1e8;
						terrainSourceDrawOcclusion(source, subtask->PaintOp.world_pos[0], subtask->PaintOp.world_pos[2], subtask->PaintOp.reverse, task->occlusion_draw_pos);
                    }
                    else
                    {
						assert(task->common_params);
						terEdUseBrush( source, &queue->current_state, pCurrentCompiledBrush, task->common_params,
                                       subtask->PaintOp.world_pos[0], subtask->PaintOp.world_pos[2],
                                       subtask->PaintOp.reverse, first_frame);
                    }
                }
				xcase TE_ACTION_VACCUFORM:
				{
					terrainSourceDoVaccuform(source, subtask->VaccuformOp.object);
					eaPush(&task->vaccuform_objects, subtask->VaccuformOp.object);
					subtask->VaccuformOp.object = NULL;
				}
                xcase TE_ACTION_STITCH_NEIGHBORS:
                {
                    terrainSourceStitchNeighbors(source);
                }
                xcase TE_ACTION_SLOPE_BRUSH:
                {
					assert(task->common_params);
                    terEdApplyHeightSlopeBrushUp(source, &queue->current_state, subtask->SlopeBrushOp.params, task->common_params, -1);
                }
				xcase TE_ACTION_RESAMPLE:
				{
					int i;
					for (i = 0; i < eaSize(&source->layers); i++)
						if (source->layers[i]->effective_mode == LAYER_MODE_EDITABLE)
						{
							terrainSourceLayerResample(source->layers[i], subtask->ResampleOp.lod);
							terrainSourceLayerSetUnsaved(source->layers[i]);
						}
					terrainSourceSetVisibleLOD(source, subtask->ResampleOp.lod);
				}
                xcase TE_ACTION_SAVE_LAYER:
                {
                    if (terrainSourceSaveLayer(subtask->SaveOp.layer, subtask->SaveOp.force))
                    {
                        terrainQueueLock();
                        eaPush(&source->finish_save_layers, subtask->SaveOp.layer);
                        terrainQueueUnlock();
                    }
                }
				xcase TE_ACTION_GENESIS_NODE_TO_DESIGN:
				{
					genesisDoNodesToDesign(source, subtask->GenesisOp.layout);
				}
            }
			terrainQueueFreeSubtask(subtask);
        }

		if (last_frame)
        {
            int l;

			if (eaSize(&task->vaccuform_objects) > 0)
			{
				terrainSourceFinishVaccuform(source, task->vaccuform_objects);
				eaDestroyEx(&task->vaccuform_objects, NULL);
			}

            terrainSourceBrushMouseUp(source, task->change_list);

            for (l = 0; l < eaSize(&source->layers); l++)
            {
                terrainSourceUpdateColors(source->layers[l], false);
                terrainSourceUpdateNormals(source->layers[l], false);
            }

            queue->waiting_for_mouse_up = true;
            if (background_thread)
            {
                // Wait for mouse up to be handled in the main thread
                do
                {
                    SleepEx(100, TRUE);
                } while (queue->waiting_for_mouse_up);
            }
        }

		terrainQueueLock();
		terrainQueueModifyQueueLock();

        if (last_frame)
        {
			terrainQueueTaskCompleted(queue, task);
        }
        
        queue->needs_update = true;
	}
	terrainQueueModifyQueueUnlock();
    terrainQueueUnlock();
}

typedef struct terrain_queue_def_info
{
	VaccuformObject*** objects;
	GroupDef *parent_def;
} terrain_queue_def_info;

bool terrain_queue_def_callback(terrain_queue_def_info *queue_info, GroupDef *def, GroupInfo *info, GroupInheritedInfo *inherited_info, bool needs_entry)
{
	if (queue_info->parent_def)
	{
		int i;
		bool found = false;
		for (i = 0; i < eaSize(&inherited_info->parent_defs); i++)
			if (inherited_info->parent_defs[i] == queue_info->parent_def)
			{
				found = true;
				break;
			}
		if (!found)
			return true;
	}
	if (needs_entry && def->property_structs.terrain_properties.bVaccuFormMe)
	{
		char *prop_val;
		VaccuformObject *obj = calloc(1, sizeof(VaccuformObject));
		F32 falloff = 0;
		if (info->spline)
		{
			memcpy(obj->spline_matrices, info->spline, sizeof(GroupSplineParams));
			obj->is_spline = true;
		}
		else
			obj->is_spline = false;
		falloff = def->property_structs.terrain_properties.fVaccuFormFalloff;
		if ((prop_val = def->property_structs.terrain_properties.pcVaccuFormBrush) != NULL)
			strcpy(obj->brush_name, prop_val);

		printf("Vaccuforming %s - %f (%s)\n", def->name_str, falloff, obj->brush_name);
		obj->model = modelLODLoadAndMaybeWait(def->model, 0, true);
		if (obj->model)
		{
			copyMat4(info->world_matrix, obj->mat);
			obj->falloff = falloff;
			eaPush(queue_info->objects, obj);
		}
		else
		{
			SAFE_FREE(obj);
		}
	}
	return true;
}

void terrainQueueFindObjectsToVaccuformInDef(ZoneMapLayer *layer, GroupDef *def, Mat4 mat, VaccuformObject*** objects)
{
	terrain_queue_def_info info = { 0 };
	info.parent_def = def;
	info.objects = objects;
	layerGroupTreeTraverse(layer, terrain_queue_def_callback, &info, true, true);
}

void terrainQueueFindObjectsToVaccuform(TerrainEditorSourceLayer *source_layer, VaccuformObject*** objects, bool terrain_objects)
{
	terrain_queue_def_info info = { 0 };
	info.objects = objects;
	layerGroupTreeTraverse(source_layer->layer, terrain_queue_def_callback, &info, true, terrain_objects);
}

const char *terrainQueueGetSubtaskLabel(TerrainSubtask *subtask)
{
	const char *label = "Painting...";
	switch (subtask->op_type)
	{
	case TE_ACTION_VACCUFORM:
		label = "Vaccuforming...";
		break;
	case TE_ACTION_STITCH_NEIGHBORS:
		label = "Stitching...";
		break;
	case TE_ACTION_RESAMPLE:
		label = "Resampling...";
		break;
	case TE_ACTION_SAVE_LAYER:
		label = "Saving...";
		break;
	};
	return label;
}

#endif
