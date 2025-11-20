#ifndef NO_EDITORS



#include "GfxPrimitive.h"

#include "WorldLib.h"
#include "WorldGrid.h"
#include "WorldColl.h"
#include "WorldEditorOperations.h"
#include "WorldEditorUtil.h"
#include "MultiEditFieldContext.h"

#include "partition_enums.h"
#include "CurveEditor.h"
#include "autogen/CurveEditorUI_c_ast.h"


AUTO_RUN_ANON(memBudgetAddMapping(__FILE__, BUDGET_Editors););

void undo_parent_matrix(Mat4 in, Mat4 parent, Mat4 out)
{
	Mat4 temp_matrix, inv_parent_rotation;
	copyMat4(in, temp_matrix);
	subVec3(in[3], parent[3], temp_matrix[3]);
	copyMat3(parent, inv_parent_rotation);
	transposeMat3(inv_parent_rotation);
	setVec3(inv_parent_rotation[3], 0, 0, 0);
	mulMat4(inv_parent_rotation, temp_matrix, out); 
}

// This function makes sure all constraints on this curve are met after an edit
void curveApplyConstraints(GroupTracker *tracker)
{
	if (!tracker || !tracker->def || !tracker->def->property_structs.curve)
		return;
}

EditorObject *curveCPCreateEditorObject(TrackerHandle *handle, int index, ZoneMapLayer *layer)
{
	char name_buf[64];
	CurveControlPoint *new_point = calloc(1, sizeof(CurveControlPoint));
	sprintf(name_buf, "Point %d", index/3);
	new_point->handle = trackerHandleCopy(handle);
	new_point->index = index;
	return editorObjectCreate(new_point, name_buf, layer, EDTYPE_CURVE_CP);
}

void curveCPFree(EditorObject *object)
{
	CurveControlPoint *point = (CurveControlPoint*)object->obj;
	SAFE_FREE(point);
}

bool curveCPSelectFunction(EditorObject *object)
{
	return true;
}

bool curveCPDeselectFunction(EditorObject *object)
{
	return true;
}

void curveCPSelectionChangedFunction(EditorObject **selection, bool in_undo)
{
}

int curveCPCompare(EditorObject *obj1, EditorObject *obj2)
{
	int ret;
	CurveControlPoint *point1, *point2;
	if (!obj1 || !obj2) return -1;
	if (obj1->type != obj2->type) return -1;

	point1 = (CurveControlPoint*)obj1->obj;
	point2 = (CurveControlPoint*)obj2->obj;
	ret = trackerHandleComp(point1->handle, point2->handle);
	if (ret != 0)
		return ret;

	return point2->index - point1->index;
}

void curveCPContextMenu(EditorObject *edObj, UIMenuItem ***outItems)
{
}

bool curveCPMovementEnabled(EditorObject *obj, EditorObjectGizmoMode mode)
{
	// TomY TODO - locked object library, etc?
	CurveControlPoint *point = obj->obj;
	WorldCurve *curve;
	if (layerGetLocked((ZoneMapLayer*)obj->context) != 3)
		return false;
	if (point->index == 0)
		return false;

	curve = curveFromTrackerHandle(point->handle);
	if (!curve)
		return false;

	return true;
}

void curveCPGetMatFunction(EditorObject *obj, Mat4 mat)
{
	identityMat4(mat);
    if (obj->obj)
    {
		CurveControlPoint *point = (CurveControlPoint*)obj->obj;
		GroupTracker *tracker = trackerFromTrackerHandle(point->handle);
		int index = point->index;
		if (tracker && tracker->def && tracker->def->property_structs.curve &&
			eafSize(&tracker->def->property_structs.curve->spline.spline_points) > index+2)
		{
			WorldCurve *curve = tracker->def->property_structs.curve;
			Mat4 local_mat, parent_mat;
            Vec3 dir_norm;
			assert(index < eafSize(&curve->spline.spline_points)-2);
            copyVec3(&curve->spline.spline_deltas[index], dir_norm);
            normalVec3(dir_norm);
            orientMat3Yvec(local_mat, dir_norm, &curve->spline.spline_up[index]);
            copyVec3(&curve->spline.spline_points[index], local_mat[3]);

			trackerGetMat(tracker, parent_mat);
			mulMat4(parent_mat, local_mat, mat);
        }
    }
}

void curveCPMoveFunction(EditorObject** objects)
{
}

void curveCPEndMoveFunction(EditorObject** objects)
{
	int i;    
	for (i = 0; i < eaSize(&objects); i++)
	{
		Vec3 pos, dir, up;
		CurveControlPoint *point;
		EditorObject *object = objects[i];
		GroupTracker *tracker;
		int index;

		assert(object->type->objType == EDTYPE_CURVE_CP);
		point = (CurveControlPoint*)object->obj;
		index = point->index;
		if (tracker = wleOpPropsBegin(point->handle))
		{
			if (tracker->def && tracker->def->property_structs.curve &&
				eafSize(&tracker->def->property_structs.curve->spline.spline_points) > index+2)
			{
				Mat4 parent_matrix;
				trackerGetMat(tracker, parent_matrix);
				if (splineUIDoMatrixUpdate(&tracker->def->property_structs.curve->spline, index, parent_matrix, object->mat, pos, dir, up))
				{
					copyVec3(pos, &tracker->def->property_structs.curve->spline.spline_points[index]);
					copyVec3(dir, &tracker->def->property_structs.curve->spline.spline_deltas[index]);
					copyVec3(up, &tracker->def->property_structs.curve->spline.spline_up[index]);
				}
				curveApplyConstraints(tracker);
				wleOpPropsUpdate();
			}
			wleOpPropsEnd();
		}
	}
}

void find_cp_click_helper(Vec3 start, Vec3 end, GroupTracker *tracker, EditorObject ***edObjList, bool attachable)
{
	int i;
	if (!tracker || !tracker->def || tracker->invisible)
		return;
	if (tracker->def->property_structs.curve_gaps && tracker->subObjectEditing)
	{
		for (i = 0; i < eaSize(&tracker->def->property_structs.curve_gaps->gaps); i++)
		{
			Vec3 intersect, min, max;
			WorldCurveGap *gap = tracker->def->property_structs.curve_gaps->gaps[i];
			setVec3(max, gap->radius, gap->radius, gap->radius);
			addVec3(max, gap->position, max);
			setVec3(min, -gap->radius, -gap->radius, -gap->radius);
			addVec3(min, gap->position, min);
			if (lineBoxCollision( start, end, min, max, intersect ))
			{
				TrackerHandle *handle = trackerHandleCreate(tracker);
				eaPush(edObjList, curveGapCreateEditorObject(handle, i, tracker->parent_layer));
			}
		}
	}
	if ((tracker->def->property_structs.curve && (attachable || tracker->subObjectEditing)) ||
		(attachable && tracker->def->property_structs.child_curve && tracker->def->property_structs.child_curve->attachable))
	{
		F32 collide_offset;
		Vec3 collide_pos;
		Mat4 parent_matrix;
		//Vec3 intersect;
		int coll_index = -1;
		trackerGetMat(tracker, parent_matrix);
		if (splineCollideFull(start, end, parent_matrix, &tracker->def->property_structs.curve->spline, &collide_offset, collide_pos, 10))
		{
			coll_index = splineCollide(start, end, parent_matrix, &tracker->def->property_structs.curve->spline);
			if (coll_index >= 0)
			{
				TrackerHandle *handle = trackerHandleCreate(tracker);
				eaPush(edObjList, curveCPCreateEditorObject(handle, coll_index, tracker->parent_layer));
			}
		}
		if (attachable)
		{
			Spline xformed_spline = { 0 };
			Mat4 parent_mat;
			trackerGetMat(tracker, parent_mat);
			StructCopyAll(parse_Spline, &tracker->def->property_structs.curve->spline, &xformed_spline);
			splineTransformMatrix(&xformed_spline, parent_mat);
			for (i = 0; i < eafSize(&xformed_spline.spline_points); i += 3)
				splineUIDrawControlPoint(&xformed_spline, i, false, 
					(i == coll_index) && (eaSize(edObjList) == 1), false, false);
			splineDestroy(&xformed_spline);
		}
	}
	for (i = 0; i < tracker->child_count; i++)
		find_cp_click_helper(start, end, tracker->children[i], edObjList, attachable);
}

float curveCPClickFunction(int mouseX, int mouseY, EditorObject ***edObjList)
{
	Mat4 cam;
	Vec3 start, end;
	int l;

	gfxGetActiveCameraMatrix(cam);
	editLibCursorSegment(cam, mouseX, mouseY, 10000, start, end);

	for (l = 0; l < zmapGetLayerCount(NULL); l++)
	{
		ZoneMapLayer *layer = zmapGetLayer(NULL, l);
		GroupTracker *root_tracker;
		assert(layer);
		root_tracker = layerGetTracker(layer);
		find_cp_click_helper(start, end, root_tracker, edObjList, false);
	}
	return 1.f;
}

void find_cp_marquee_helper(int mouseX, int mouseY, int mouseX2, int mouseY2, GroupTracker *tracker,
							int depth, Mat4 cam, Mat44 scrProjMat, EditorObject ***edObjs)
{
	int i, j;
	if (!tracker)
		return;
	if (tracker->def && tracker->def->property_structs.curve &&
		!tracker->invisible && tracker->subObjectEditing)
	{
		Mat4 parent_matrix;
		Vec3 marqueeMin, marqueeMax;
		setVec3(marqueeMin, mouseX, mouseY, 0);
		setVec3(marqueeMax, mouseX2, mouseY2, depth);
		trackerGetMat(tracker, parent_matrix);
		for (j = 0; j < eafSize(&tracker->def->property_structs.curve->spline.spline_points); j += 3)
		{
			Vec3 min, max;
			Vec3 bottomLeft, topRight;
			Mat4 world_mat;

			identityMat3(world_mat);
			mulVecMat4(&tracker->def->property_structs.curve->spline.spline_points[j], parent_matrix, world_mat[3]);

			setVec3(max, 3, 3, 3);
			setVec3(min, -3, -3, -3);

			// project the tracker's bounding box onto the screen and get the axis-aligned bounding box of the
			// projected vertices
			editLibFindScreenCoords(min, max, world_mat, scrProjMat, bottomLeft, topRight);

			if (wleBoxContainsAA(marqueeMin, marqueeMax, bottomLeft, topRight) &&
				distance3(world_mat[3], cam[3]) <= depth)
			{
				TrackerHandle *handle = trackerHandleCreate(tracker);
				eaPush(edObjs, curveCPCreateEditorObject(handle, j, tracker->parent_layer));
			}
		}
	}
	for (i = 0; i < tracker->child_count; i++)
		find_cp_marquee_helper(mouseX, mouseY, mouseX2, mouseY2, tracker->children[i], depth, cam, scrProjMat, edObjs);
}

void curveCPMarqueeFunction(int mouseX, int mouseY, int mouseX2, int mouseY2, int depth, Mat4 cam, Mat44 scrProjMat, 
							EditorObject ***edObjs, bool crossingMode)
{
	int l;

	for (l = 0; l < zmapGetLayerCount(NULL); l++)
	{
		ZoneMapLayer *layer = zmapGetLayer(NULL, l);
		GroupTracker *root_tracker;
		assert(layer);
		root_tracker = layerGetTracker(layer);
		find_cp_marquee_helper(mouseX, mouseY, mouseX2, mouseY2, root_tracker, depth, cam, scrProjMat, edObjs);
	}
}

void curveEditorDrawFunction(EditorObject **selection)
{
	int i, j;
	Vec3 start, end;
	EditorObject **cp_selection = edObjSelectionGet(EDTYPE_CURVE_CP);
	EditorObject **gap_selection = edObjSelectionGet(EDTYPE_CURVE_GAP);
	bool snapping_to_curve = false;
	GroupDef *unselected_geo = objectLibraryGetGroupDefByName("CurveBox_01", true);
	GroupDef *selected_geo = objectLibraryGetGroupDefByName("CurveBox_02", true);
	int iPartitionIdx = PARTITION_CLIENT;

	for (i = 0; i < eaSize(&editState.curveTrackers); i++)
	{
		Mat4 mat;
		GroupTracker *curve_tracker = trackerFromTrackerHandle(editState.curveTrackers[i]);
		if (curve_tracker && !curve_tracker->invisible)
		{
			GroupDef *curve_def = curve_tracker->def;
			Mat4 spline_mat;
			trackerGetMat(curve_tracker, spline_mat);

			if (curve_def && curve_def->property_structs.curve)
			{
				Spline *spline = &curve_def->property_structs.curve->spline;
				GroupDef *object_def = unselected_geo;
				for (j = 0; j < eaSize(&selection); j++)
				{
					TrackerHandle *handle = selection[j]->obj;
					GroupTracker *tracker = trackerFromTrackerHandle(handle);
					if (tracker && tracker->def && tracker->def == curve_def)
						object_def = selected_geo;
				}
				identityMat4(mat);
				for (j = 0; j < eafSize(&curve_def->property_structs.curve->spline.spline_points)-3; j += 3)
				{
					TempGroupParams tgparams = {0};
					tgparams.editor_only = true;
					tgparams.no_culling = true;
					tgparams.spline_params = calloc(1, sizeof(GroupSplineParams));
					splineGetMatrices(spline_mat, zerovec3, 1.f, spline, j, tgparams.spline_params->spline_matrices, false);
					tgparams.spline_params->spline_matrices[1][2][0] = 10.f; // Length
					if (spline->spline_widths)
						tgparams.spline_params->spline_matrices[1][2][1] = 0.1f*MAX(1,spline->spline_widths[j/3]); // Scale
					else
						tgparams.spline_params->spline_matrices[1][2][1] = 1;
					tgparams.spline_params->spline_matrices[1][3][0] = distance3(&spline->spline_points[j], &spline->spline_points[j+3]);
					tgparams.spline_params->spline_matrices[1][3][1] = 1.f;

					worldAddTempGroup(object_def, mat, &tgparams, true);
				}
			}
		}
	}

	// Draw editable curves
	for (i = 0; i < eaSize(&editState.editingTrackers); i++)
	{
		int idx;
		bool selected = false;
		Mat4 tracker_mat;
		GroupTracker *tracker = trackerFromTrackerHandle(editState.editingTrackers[i]);

		if (tracker && eafSize(&tracker->inherited_spline.spline_points) > 0)
		{
			for (j = 0; j < eaSize(&selection); j++)
			{
				TrackerHandle *handle = selection[j]->obj;
				if (trackerHandleComp(handle, editState.editingTrackers[i]) == 0)
				{
					selected = true;
					break;
				}
			}
			splineUIDrawCurve(&tracker->inherited_spline, selected);

			for (idx = 0; idx < eafSize(&tracker->inherited_spline.spline_points); idx += 3)
			{
				bool cp_selected = false, cp_attached = false, cp_welded = false;
				Mat4 cp_matrix;
				for (j = 0; j < eaSize(&cp_selection); j++)
				{
					CurveControlPoint *point = cp_selection[j]->obj;
					if (trackerHandleComp(point->handle, editState.editingTrackers[i]) == 0 &&
						point->index == idx)
					{
						cp_selected = true;
						copyMat4(cp_selection[j]->mat, cp_matrix);
						break;
					}
				}
				if (cp_selected)
					splineUIDrawControlPointVectors(cp_matrix[3], cp_matrix[2], cp_matrix[1], true, false, false, false);
				else
					splineUIDrawControlPoint(&tracker->inherited_spline, idx, false, false, false, false);
			}
		}

		if (tracker && tracker->def && tracker->def->property_structs.curve_gaps)
		{
			trackerGetMat(tracker, tracker_mat);
			for (idx = 0; idx < eaSize(&tracker->def->property_structs.curve_gaps->gaps); idx++)
			{
				Color color1;
				Vec3 gap_pos;
				bool gap_selected = false;
				WorldCurveGap *gap = tracker->def->property_structs.curve_gaps->gaps[idx];
				for (j = 0; j < eaSize(&gap_selection); j++)
				{
					CurveControlPoint *point = gap_selection[j]->obj;
					if (trackerHandleComp(point->handle, editState.editingTrackers[i]) == 0 &&
						point->index == idx)
					{
						gap_selected = true;
						break;
					}
				}
				mulVecMat4(gap->position, tracker_mat, gap_pos);
				color1.b = gap_selected ? 0xFF : 0x00; color1.r = color1.g = color1.a = 0xFF;
				gfxDrawSphere3D(gap_pos, gap->radius, 12, color1, 1);
			}
		}
	}

	// Draw non-editable, selected curves
	for (i = 0; i < eaSize(&selection); i++)
	{
		TrackerHandle *handle = selection[i]->obj;
		GroupTracker *tracker = trackerFromTrackerHandle(handle);

		if (tracker && !tracker->subObjectEditing && 
			eafSize(&tracker->inherited_spline.spline_points) > 0)
		{
			splineUIDrawCurve(&tracker->inherited_spline, (tracker->def && tracker->def->property_structs.curve));
		}
	}

	editLibCursorRay(start, end);

	if (editState.splineMode == SplineUICreate || editState.splineMode == SplineUICreateGap)
	{
		bool collided = false;
		bool found_attach_point = false;
		Vec3 collision_pt, collision_normal;
		Vec3 top, bottom;
		Vec3 collide_pos;
		F32 collide_offset;
		bool snap_enabled = TranslateGizmoIsSnapEnabled(edObjHarnessGetTransGizmo());
		F32 snap_size = GizmoGetSnapWidth(TranslateGizmoGetSnapResolution(edObjHarnessGetTransGizmo()));
		bool some_curves_selected = false;
		EditorObject *attach_point = NULL;

		// Find intersecting point
		if (editState.splineDrawOnTerrain)
		{
			// Terrain intersection
			worldCollideRay(iPartitionIdx, start, end, WC_QUERY_BITS_EDITOR_ALL, editState.rayCollideInfo.results);
			
			if(wcoGetUserPointer(	editState.rayCollideInfo.results->wco,
									heightMapCollObjectMsgHandler,
									NULL))
			{
				copyVec3(editState.rayCollideInfo.results->posWorldImpact, collision_pt);
				collision_pt[1] += editState.splineDrawOffset;
				copyVec3(editState.rayCollideInfo.results->normalWorld, collision_normal);
				collided = true;
			}
			else
			{
				worldCollideRay(iPartitionIdx, start, end, WC_FILTER_BIT_TERRAIN, editState.rayCollideInfo.results);
				if(wcoGetUserPointer(	editState.rayCollideInfo.results->wco,
										entryCollObjectMsgHandler,
										NULL))
				{
					copyVec3(editState.rayCollideInfo.results->posWorldImpact, collision_pt);
					collision_pt[1] += editState.splineDrawOffset;
					copyVec3(editState.rayCollideInfo.results->normalWorld, collision_normal);
					collided = true;
				}
			}
		}
		else
		{
			// Grid intersection
			Mat4 gridmat;
			copyMat4(unitmat, gridmat);
			gridmat[3][1] = editState.splineDrawOffset;
			if (planeIntersect(start, end, gridmat, collision_pt))
			{
				collided = true;
				setVec3(collision_normal, 0, 1, 0);
			}
		}

		for (i = 0; i < eaSize(&selection); i++)
		{
			TrackerHandle *handle = selection[i]->obj;
			GroupTracker *tracker;
			if (handle &&
				(tracker = trackerFromTrackerHandle(handle)) &&
				tracker->def && tracker->def->property_structs.curve &&
				tracker->subObjectEditing &&
				selection[i]->type->movementEnableFunc(selection[i], edObjHarnessGetGizmo()))
			{
				Mat4 parent_mat;
				trackerGetMat(tracker, parent_mat);
				some_curves_selected = true;
				if (!found_attach_point && 
					splineCollideFull(start, end, parent_mat, &tracker->def->property_structs.curve->spline, 
									&collide_offset, collide_pos, 10))
				{
					copyVec3(collide_pos, top);
					copyVec3(collide_pos, bottom);
					top[0] += 10;
					top[1] += 15;

					// Todo: Snap to grid?

					snapping_to_curve = true;
					gfxDrawLine3D_2ARGB(top, bottom, 0xffffffff, 0xffff0000);

					if (mouseClick(MS_LEFT))
					{
						if (wleOpPropsBegin(handle))
						{
							if (editState.splineMode == SplineUICreateGap)
							{
								WorldCurveGap *new_gap = StructCreate(parse_WorldCurveGap);
								copyVec3(collide_pos, new_gap->position);
								new_gap->radius = 10.f;
								new_gap->inherited = true;
								if (!tracker->def->property_structs.curve_gaps)
									tracker->def->property_structs.curve_gaps = StructCreate(parse_WorldCurveGaps);
								eaPush(&tracker->def->property_structs.curve_gaps->gaps, new_gap);
							}
							else
							{
								Spline *spline = &tracker->def->property_structs.curve->spline;
								Vec3 pos, delta, up, in = { 0, 0, 0 };
								int index = ((int)collide_offset);
								F32 width = 0;
								splineTransform(spline, index, collide_offset-index, in, pos, up, delta, false);
								if (spline->spline_widths)
									width = spline->spline_widths[index/3];
								splineInsertCP(spline, index+3, pos, up, delta, width);
							}
							curveApplyConstraints(tracker);

							wleOpPropsUpdate();
							wleOpPropsEnd();
						}
						inpHandled();
					}
				}
			}
		}

		if (some_curves_selected && collided && !snapping_to_curve)
		{
			Vec3 pos;

			copyVec3(collision_pt, pos);

			/*if (snap_enabled)
			{
				splineUIDrawGrid(pos, snap_size);
			}*/

			copyVec3(pos, top);
			copyVec3(pos, bottom);
			top[1] += 15;

			gfxDrawLine3D_2ARGB(top, bottom, 0xffffffff, 0xff00ff00);

			if (mouseClick(MS_LEFT))
			{
				for (i = 0; i < eaSize(&selection); i++)
				{
					TrackerHandle *handle = selection[i]->obj;
					GroupTracker *tracker;
					if (handle &&
						(tracker = trackerFromTrackerHandle(handle)) &&
						tracker->def && tracker->def->property_structs.curve &&
						tracker->subObjectEditing &&
						selection[i]->type->movementEnableFunc(selection[i], edObjHarnessGetGizmo()))
					{
						if (wleOpPropsBegin(handle))
						{
							Mat4 inv_matrix;
							Vec3 temp, local_pos, local_up;

							trackerGetMat(tracker, inv_matrix);
							transposeMat3(inv_matrix);
							subVec3(pos, inv_matrix[3], temp);
							mulVecMat3(temp, inv_matrix, local_pos);

							if (editState.splineMode == SplineUICreateGap)
							{
								// Create a gap
								WorldCurveGap *new_gap = StructCreate(parse_WorldCurveGap);
								copyVec3(local_pos, new_gap->position);
								new_gap->radius = 10.f;
								new_gap->inherited = true;
								if (!tracker->def->property_structs.curve_gaps)
									tracker->def->property_structs.curve_gaps = StructCreate(parse_WorldCurveGaps);
								eaPush(&tracker->def->property_structs.curve_gaps->gaps, new_gap);
							}
							else
							{
								// Create a new point
								int new_index = eafSize(&tracker->def->property_structs.curve->spline.spline_points);
								F32 width = 0;

								mulVecMat3(collision_normal, inv_matrix, local_up);

								if (new_index > 0)
								{
									if (tracker->def->property_structs.curve->spline.spline_widths)
										width = tracker->def->property_structs.curve->spline.spline_widths[(new_index/3)-1];
									else
										width = 0;
								}

								splineAppendAutoCP(&tracker->def->property_structs.curve->spline, local_pos, local_up, false, 0, width);
							}
							curveApplyConstraints(tracker);

							wleOpPropsUpdate();
							wleOpPropsEnd();
						}
					}
				}

				inpHandled();
			}
		}

		if (attach_point)
		{
			editorObjectRef(attach_point);
			editorObjectDeref(attach_point);
		}
	}
}

void curveCPActionDelete(EditorObject **selection)
{
	int i, j;
	EditorObject **deselectionList = NULL;
	EditUndoBeginGroup(edObjGetUndoStack());
	for (i = 0; i < eaSize(&selection); i++)
	{
		CurveControlPoint *point = (CurveControlPoint*)selection[i]->obj;
		if (point->index > 0)
		{
			editorObjectRef(selection[i]);
			eaPush(&deselectionList, selection[i]);
		}
	}

	//Splitting this into two separate for loops and calling the list version of deselect saves the headache of refreshing the selection n times. Refresh has n! time and is the biggest slowdown in the selection pipeline.
	if (deselectionList)
	{
		edObjDeselectList(deselectionList);
	}

	for (i = 0; i < eaSize(&deselectionList); i++)
	{
		GroupTracker *edit_tracker;
		CurveControlPoint *point = (CurveControlPoint*)deselectionList[i]->obj;
		if (edit_tracker = wleOpPropsBegin(point->handle))
		{
			splineDeleteCP(&edit_tracker->def->property_structs.curve->spline, point->index);
			for (j = 0; j < eaSize(&selection); j++)
			{
				if (j != i)
				{
					CurveControlPoint *point2 = (CurveControlPoint*)selection[j]->obj;
					if (trackerHandleComp(point->handle, point2->handle) == 0 &&
						point->index < point2->index)
					{
						point2->index -= 3;
					}
				}
			}
			curveApplyConstraints(edit_tracker);

			wleOpPropsUpdate();
			wleOpPropsEnd();
		}
		editorObjectDeref(deselectionList[i]);
	}
	eaDestroy(&deselectionList);
	EditUndoEndGroup(edObjGetUndoStack());
}



EditorObject *curveGapCreateEditorObject(TrackerHandle *handle, int index, ZoneMapLayer *layer)
{
	char name_buf[64];
	CurveControlPoint *new_point = calloc(1, sizeof(CurveControlPoint));
	sprintf(name_buf, "Gap %d", index);
	new_point->handle = trackerHandleCopy(handle);
	new_point->index = index;
	return editorObjectCreate(new_point, name_buf, layer, EDTYPE_CURVE_GAP);
}

void curveGapFree(EditorObject *object)
{
}

bool curveGapMovementEnabled(EditorObject *obj, EditorObjectGizmoMode mode)
{
	// TomY TODO - locked object library, etc?
	CurveControlPoint *point = obj->obj;
	WorldCurve *curve;
	if (layerGetLocked((ZoneMapLayer*)obj->context) != 3)
		return false;

	curve = curveFromTrackerHandle(point->handle);
	if (!curve)
		return false;

	return true;
}

int curveGapCompare(EditorObject *obj1, EditorObject *obj2)
{
	int ret;
	CurveControlPoint *point1, *point2;
	if (!obj1 || !obj2) return -1;
	if (obj1->type != obj2->type) return -1;

	point1 = (CurveControlPoint*)obj1->obj;
	point2 = (CurveControlPoint*)obj2->obj;
	ret = trackerHandleComp(point1->handle, point2->handle);
	if (ret != 0)
		return ret;

	return point2->index - point1->index;
}

void curveGapGetMatFunction(EditorObject *obj, Mat4 mat)
{
	identityMat4(mat);
    if (obj->obj)
    {
		CurveControlPoint *point = (CurveControlPoint*)obj->obj;
		GroupTracker *tracker = trackerFromTrackerHandle(point->handle);
		int index = point->index;
		if (tracker && tracker->def && tracker->def->property_structs.curve_gaps &&
			eaSize(&tracker->def->property_structs.curve_gaps->gaps) > index)
		{
			WorldCurveGap *gap = tracker->def->property_structs.curve_gaps->gaps[index];
			Mat4 local_mat, parent_mat;
			identityMat3(local_mat);
            copyVec3(gap->position, local_mat[3]);

			trackerGetMat(tracker, parent_mat);
			mulMat4(parent_mat, local_mat, mat);
        }
    }
}

void curveGapMoveFunction(EditorObject** objects)
{
}

void curveGapEndMoveFunction(EditorObject** objects)
{
	int i;    
	for (i = 0; i < eaSize(&objects); i++)
	{
		CurveControlPoint *point;
		EditorObject *object = objects[i];
		GroupTracker *tracker;
		int index;

		assert(object->type->objType == EDTYPE_CURVE_GAP);
		point = (CurveControlPoint*)object->obj;
		index = point->index;
		if (tracker = wleOpPropsBegin(point->handle))
		{
			if (tracker->def && tracker->def->property_structs.curve_gaps &&
				eaSize(&tracker->def->property_structs.curve_gaps->gaps) > index)
			{
				Mat4 parent_matrix, local_matrix;
				trackerGetMat(tracker, parent_matrix);
				undo_parent_matrix(object->mat, parent_matrix, local_matrix);

				copyVec3(local_matrix[3], tracker->def->property_structs.curve_gaps->gaps[index]->position);
			}
			wleOpPropsUpdate();
			wleOpPropsEnd();
		}
	}
}


//// Panels

AUTO_STRUCT;
typedef struct CurveCPPanelUI
{
	Vec3 vCPPos;
	Vec3 vCPRot;
	F32 fCPWidth;
} CurveCPPanelUI;

static CurveCPPanelUI g_CP_UI = {0};

static void curveCPPosCB(MEField *pField, bool bFinished, UserData pUnused)
{
	Mat4 object_mat;
    EditorObject *edObj = wleAEGetSelected();
	CurveControlPoint *cp_object;
	GroupTracker *tracker;

	if(!bFinished || MEContextExists())
		return;

	if (!edObj || edObj->type->objType != EDTYPE_CURVE_CP)
		return;
	cp_object = edObj->obj;
	tracker = trackerFromTrackerHandle(cp_object->handle);
	if (!tracker || !tracker->def || !tracker->def->property_structs.curve)
		return;
	assert(cp_object->index >= 0 && cp_object->index+2 < eafSize(&tracker->def->property_structs.curve->spline.spline_points));
	copyVec3(g_CP_UI.vCPPos, object_mat[3]);

	if (wleOpPropsBegin(cp_object->handle))
	{
		Mat4 parent_matrix, local_matrix;
		identityMat3(object_mat);

		trackerGetMat(tracker, parent_matrix);
		undo_parent_matrix(object_mat, parent_matrix, local_matrix);
		copyVec3(local_matrix[3], &tracker->def->property_structs.curve->spline.spline_points[cp_object->index]);

		wleOpPropsUpdate();
		wleOpPropsEnd();
		edObjRefreshMat(edObj);
	}
}

static void curveCPRotCB(MEField *pField, bool bFinished, UserData pUnused)
{
	Vec3 pyr;
    EditorObject *edObj = wleAEGetSelected();
	CurveControlPoint *cp_object;
	GroupTracker *tracker;

	if(!bFinished || MEContextExists())
		return;

	if (!edObj || edObj->type->objType != EDTYPE_CURVE_CP)
		return;
	cp_object = edObj->obj;
	tracker = trackerFromTrackerHandle(cp_object->handle);
	if (!tracker || !tracker->def || !tracker->def->property_structs.curve)
		return;
	assert(cp_object->index >= 0 && cp_object->index+2 < eafSize(&tracker->def->property_structs.curve->spline.spline_points));
	copyVec3(g_CP_UI.vCPRot, pyr);
	{
		Mat4 parent_matrix, local_matrix;
		Mat4 rot_mat;

		pyr[0] = RAD(pyr[0]);
		pyr[1] = RAD(pyr[1]);
		pyr[2] = RAD(pyr[2]);
		createMat3YPR(rot_mat, pyr);
		setVec3(rot_mat[3], 0, 0, 0);

		trackerGetMat(tracker, parent_matrix);
		undo_parent_matrix(rot_mat, parent_matrix, local_matrix);

		if (wleOpPropsBegin(cp_object->handle))
		{
			copyVec3(local_matrix[2], &tracker->def->property_structs.curve->spline.spline_deltas[cp_object->index]);
			copyVec3(local_matrix[1], &tracker->def->property_structs.curve->spline.spline_up[cp_object->index]);

			wleOpPropsUpdate();
			wleOpPropsEnd();
			edObjRefreshMat(edObj);
		}
	}
}

static void curveCPWidthCB(MEField *pField, bool bFinished, UserData pUnused)
{
	F32 width;
	EditorObject *edObj = wleAEGetSelected();
	CurveControlPoint *cp_object;
	GroupTracker *tracker;
	Spline *spline;

	if(!bFinished || MEContextExists())
		return;

	if (!edObj || edObj->type->objType != EDTYPE_CURVE_CP)
		return;
	cp_object = edObj->obj;
	tracker = trackerFromTrackerHandle(cp_object->handle);
	if (!tracker || !tracker->def || !tracker->def->property_structs.curve)
		return;
	spline = &tracker->def->property_structs.curve->spline;
	assert(cp_object->index >= 0 && cp_object->index+2 < eafSize(&spline->spline_points));
	width = g_CP_UI.fCPWidth;
	if (wleOpPropsBegin(cp_object->handle))
	{
		if (!spline->spline_widths)
			eafSetSize(&spline->spline_widths, eafSize(&spline->spline_points)/3);
		spline->spline_widths[cp_object->index/3] = width;
		wleOpPropsUpdate();
		wleOpPropsEnd();
	}
}

static void wleAEUpdateFromData(GroupTracker *pTracker, CurveControlPoint *pControlPoint)
{
	Mat4 parent_mat;
	Mat3 rot_mat;
	Vec3 pyr;
	Vec3 world_pos;
	Mat3 world_rot;
	Spline *pSpline;

	pSpline = &pTracker->def->property_structs.curve->spline;

	trackerGetMat(pTracker, parent_mat);
	mulVecMat4(&pSpline->spline_points[pControlPoint->index], parent_mat, world_pos);

	copyVec3(world_pos, g_CP_UI.vCPPos);

	copyVec3(&pSpline->spline_deltas[pControlPoint->index], rot_mat[2]);
	copyVec3(&pSpline->spline_up[pControlPoint->index], rot_mat[1]);
	crossVec3(rot_mat[2], rot_mat[1], rot_mat[0]);
	mulMat3(parent_mat, rot_mat, world_rot);
	getMat3YPR(world_rot, pyr);

	g_CP_UI.vCPRot[0] = DEG(pyr[0]);
	g_CP_UI.vCPRot[1] = DEG(pyr[1]);
	g_CP_UI.vCPRot[2] = DEG(pyr[2]);

	g_CP_UI.fCPWidth = 0;
	if (pSpline->spline_widths)
		g_CP_UI.fCPWidth = pSpline->spline_widths[pControlPoint->index/3];
}

static int wleAECurveCPReload(EMPanel *panel, EditorObject *edObj)
{
	CurveControlPoint *pControlPoint;
	GroupTracker *pTracker;
	bool bActive, bEditable;
	MEFieldContext *pContext;

	pControlPoint = edObj->obj;
	pTracker = trackerFromTrackerHandle(pControlPoint->handle);
	bActive = (pTracker && pTracker->def && wleTrackerIsEditable(pControlPoint->handle, false, false, false) && pTracker->def->property_structs.curve);
	bEditable = bActive && pControlPoint->index > 0 && (pControlPoint->index+2 < eafSize(&pTracker->def->property_structs.curve->spline.spline_points));
	assert(pControlPoint);
	if(!bActive)
		return WLE_UI_PANEL_INVALID;

	wleAEUpdateFromData(pTracker, pControlPoint);

	pContext = MEContextPush("WorldEditor_CurveCPProps", &g_CP_UI, &g_CP_UI, parse_CurveCPPanelUI);
	pContext->pUIContainer = emPanelGetUIContainer(panel);
	wleAEAddFieldChangedCallback(pContext, NULL);

	pContext->cbChanged = curveCPPosCB;
	MEContextAddMinMax(kMEFieldType_MultiSpinner, -30000, 30000, 0.01,	"CPPos",		"Position",	"Control point position");
	pContext->cbChanged = curveCPRotCB;
	MEContextAddMinMax(kMEFieldType_MultiSpinner, -180, 180, 0.01,		"CPRot",		"Rotation",	"Control point rotation");
	pContext->cbChanged = curveCPWidthCB;
	MEContextAddMinMax(kMEFieldType_SliderText, 0, 10000, 0.01,			"CPWidth",		"Width",	"Control point width");

	MEContextAddSpacer();
	emPanelSetHeight(panel, pContext->iYPos);
	emPanelSetActive(panel, bEditable);

	MEContextPop("WorldEditor_CurveCPProps");

	return WLE_UI_PANEL_OWNED;
}

static void wleAECurveCPCreate(EMPanel *panel)
{
	UIScrollArea *scrollArea;
	scrollArea = ui_ScrollAreaCreate(0, 0, 1, 1, 0, 0, true, false);
	scrollArea->widget.widthUnit = UIUnitPercentage;
	scrollArea->widget.heightUnit = UIUnitPercentage;
	scrollArea->widget.sb->alwaysScrollX = false;
	emPanelAddChild(panel, scrollArea, false);
}

typedef struct curveGapPanelUI
{
	EMPanel *panel;

	UITextEntry *size_entry;
	UITextEntry *pos_entry[3];
	//UIComboBox *type_combo;
} curveGapPanelUI;

curveGapPanelUI curve_gap_ui;

static int wleAECurveGapReload(EMPanel *panel, EditorObject *edObj)
{
	char buf[64];
	CurveControlPoint *gap_object;
	GroupTracker *tracker;
	bool active;

	if (edObj->type->objType == EDTYPE_DUMMY)
		return WLE_UI_PANEL_INVALID;

	gap_object = edObj->obj;
	tracker = trackerFromTrackerHandle(gap_object->handle);
	active = (tracker && wleTrackerIsEditable(gap_object->handle, false, false, false) && tracker->def &&
			tracker->def->property_structs.curve_gaps);

	assert(gap_object);

	emPanelSetActive(curve_gap_ui.panel, active);

	if (active)
	{
		WorldCurveGap *gap;
		Vec3 world_pos;
		Mat4 parent_mat;

		assert(gap_object->index >= 0 && gap_object->index < eaSize(&tracker->def->property_structs.curve_gaps->gaps));
		gap = tracker->def->property_structs.curve_gaps->gaps[gap_object->index];

		trackerGetMat(tracker, parent_mat);
		mulVecMat4(gap->position, parent_mat, world_pos);

		sprintf(buf, "%.02f", world_pos[0]);
		ui_TextEntrySetText(curve_gap_ui.pos_entry[0], buf);
		sprintf(buf, "%.02f", world_pos[1]);
		ui_TextEntrySetText(curve_gap_ui.pos_entry[1], buf);
		sprintf(buf, "%.02f", world_pos[2]);
		ui_TextEntrySetText(curve_gap_ui.pos_entry[2], buf);

		sprintf(buf, "%0.2f", gap->radius);
		ui_TextEntrySetText(curve_gap_ui.size_entry, buf);
	}

	return WLE_UI_PANEL_OWNED;
}

static TrackerHandle *getSelectedHandle()
{
    EditorObject *edObj = wleAEGetSelected();
	if (!edObj || edObj->type->objType != EDTYPE_CURVE_GAP)
		return NULL;
	return ((CurveControlPoint *)edObj->obj)->handle;	
}

static WorldCurveGap *getSelectedGap()
{
    EditorObject *edObj = wleAEGetSelected();
	CurveControlPoint *gap_object;
	GroupTracker *tracker;
	if (!edObj || edObj->type->objType != EDTYPE_CURVE_GAP)
		return NULL;
	gap_object = edObj->obj;
	tracker = trackerFromTrackerHandle(gap_object->handle);
	if (!tracker || !tracker->def || !tracker->def->property_structs.curve_gaps)
		return NULL;
	assert(gap_object->index >= 0 && gap_object->index < eaSize(&tracker->def->property_structs.curve_gaps->gaps));
	return tracker->def->property_structs.curve_gaps->gaps[gap_object->index];
}

static void curveGapTypeCB(UIComboBox *combo, int value, void *userdata)
{
	/*CurveGap *gap = getSelectedGap();
	if (gap && value != gap->type)
	{
		if (wleOpPropsBegin(getSelectedHandle()))
		{
			gap->type = value;

			wleOpPropsUpdate();
			wleOpPropsEnd();
		}
	}*/
}


static void curveGapPosCB(UITextEntry *entry, UserData data)
{
	Mat4 object_mat;
	const char *x_str = ui_TextEntryGetText(curve_gap_ui.pos_entry[0]);
	const char *y_str = ui_TextEntryGetText(curve_gap_ui.pos_entry[1]);
	const char *z_str = ui_TextEntryGetText(curve_gap_ui.pos_entry[2]);
    EditorObject *edObj = wleAEGetSelected();
	CurveControlPoint *gap_object;
	GroupTracker *tracker;

	if (!edObj || edObj->type->objType != EDTYPE_CURVE_GAP)
		return;
	gap_object = edObj->obj;
	tracker = trackerFromTrackerHandle(gap_object->handle);
	if (!tracker || !tracker->def || !tracker->def->property_structs.curve_gaps)
		return;
	assert(gap_object->index >= 0 && gap_object->index < eaSize(&tracker->def->property_structs.curve_gaps->gaps));
	if (sscanf(x_str, "%f", &object_mat[3][0]) > 0 &&
		sscanf(y_str, "%f", &object_mat[3][1]) > 0 &&
		sscanf(z_str, "%f", &object_mat[3][2]) > 0)
	{
		if (wleOpPropsBegin(gap_object->handle))
		{
			Mat4 parent_matrix, local_matrix;
			identityMat3(object_mat);

			trackerGetMat(tracker, parent_matrix);
			undo_parent_matrix(object_mat, parent_matrix, local_matrix);
			copyVec3(local_matrix[3], tracker->def->property_structs.curve_gaps->gaps[gap_object->index]->position);

			wleOpPropsUpdate();
			wleOpPropsEnd();
			edObjRefreshMat(edObj);
		}
	}
}

static void curveGapSizeCB(UITextEntry *text, void *userdata)
{
	WorldCurveGap *gap = getSelectedGap();
	F32 val;
	if (!gap)
		return;
    if (sscanf(ui_TextEntryGetText(text), "%f", &val) &&
		val != gap->radius)
	{
		if (wleOpPropsBegin(getSelectedHandle()))
		{
			gap->radius = val;

			wleOpPropsUpdate();
			wleOpPropsEnd();
		}
	}
    else
    {
        char buf[64];
        sprintf(buf, "%0.2f", gap->radius);
        ui_TextEntrySetText(curve_gap_ui.size_entry, buf);
    }
}

static void curveGapActionDelete(EditorObject **selection)
{
	int i;
	EditorObject **deselectionList = NULL;
	EditUndoBeginGroup(edObjGetUndoStack());
	for (i = 0; i < eaSize(&selection); i++)
	{
		GroupTracker *tracker;
		CurveControlPoint *gap_object;
		if (!selection[i] || selection[i]->type->objType != EDTYPE_CURVE_GAP)
			continue;
		gap_object = selection[i]->obj;
		tracker = trackerFromTrackerHandle(gap_object->handle);
		if (!tracker || !tracker->def || !tracker->def->property_structs.curve_gaps)
			continue;
		assert(gap_object->index >= 0 && gap_object->index < eaSize(&tracker->def->property_structs.curve_gaps->gaps));

		editorObjectRef(selection[i]);

		eaPush(&deselectionList, selection[i]);
	}

	//Splitting this for loop & calling the list version of deselection saves the headache of refreshing the selection n times. Refresh has n! time and is the biggest slowdown in the selection pipeline.
	if (deselectionList)
	{
		edObjDeselectList(deselectionList);
		eaDestroy(&deselectionList);
	}

	for (i = 0; i < eaSize(&selection); i++)
	{
		GroupTracker *tracker;
		CurveControlPoint *gap_object;
		if (!selection[i] || selection[i]->type->objType != EDTYPE_CURVE_GAP)
			continue;
		gap_object = selection[i]->obj;
		tracker = trackerFromTrackerHandle(gap_object->handle);
		if (!tracker || !tracker->def || !tracker->def->property_structs.curve_gaps)
			continue;

		if (wleOpPropsBegin(gap_object->handle))
		{
			StructDestroy(parse_WorldCurveGap, tracker->def->property_structs.curve_gaps->gaps[gap_object->index]);
			eaRemove(&tracker->def->property_structs.curve_gaps->gaps, gap_object->index);

			wleOpPropsUpdate();
			wleOpPropsEnd();
		}

		editorObjectDeref(selection[i]);
	}
	EditUndoEndGroup(edObjGetUndoStack());
}

static void wleAECurveGapCreate(EMPanel *panel)
{
	int y = 0;
	UILabel *label;
	UITextEntry *text;

	curve_gap_ui.panel = panel;

	label = ui_LabelCreate("Pos", 0, y);
	emPanelAddChild(panel, label, false);

	text = ui_TextEntryCreate("0", 30, y);
	ui_WidgetSetWidth(&text->widget, 100);
	ui_TextEntrySetFinishedCallback(text, curveGapPosCB, NULL);
	ui_TextEntrySetFloatOnly(text);
	emPanelAddChild(panel, text, false);
	curve_gap_ui.pos_entry[0] = text;

	text = ui_TextEntryCreate("0", 130, y);
	ui_WidgetSetWidth(&text->widget, 100);
	ui_TextEntrySetFinishedCallback(text, curveGapPosCB, NULL);
	ui_TextEntrySetFloatOnly(text);
	emPanelAddChild(panel, text, false);
	curve_gap_ui.pos_entry[1] = text;

	text = ui_TextEntryCreate("0", 230, y);
	ui_WidgetSetWidth(&text->widget, 100);
	ui_TextEntrySetFinishedCallback(text, curveGapPosCB, NULL);
	ui_TextEntrySetFloatOnly(text);
	emPanelAddChild(panel, text, false);
	curve_gap_ui.pos_entry[2] = text;

	y += 25;

	label = ui_LabelCreate("Rad", 0, y);
	emPanelAddChild(panel, label, false);

	text = ui_TextEntryCreate("0", 30, y);
	ui_WidgetSetWidth(&text->widget, 80);
	ui_TextEntrySetFinishedCallback(text, curveGapSizeCB, NULL);
	ui_TextEntrySetFloatOnly(text);
	emPanelAddChild(panel, text, false);
	curve_gap_ui.size_entry = text;
}

//// Keybinds

#endif
AUTO_COMMAND ACMD_CATEGORY(World) ACMD_NAME("SplineEditor.AddControlPoint");
void curveCPToggleCreate(int enable)
{
#ifndef NO_EDITORS
	if (enable)
		editState.splineMode = SplineUICreate;
	else
		editState.splineMode = SplineUISelectMove;
	edObjHarnessEnableSelection(!(enable > 0));
#endif
}

AUTO_COMMAND ACMD_CATEGORY(World) ACMD_NAME("SplineEditor.AddGap");
void curveGapToggleCreate(int enable)
{
#ifndef NO_EDITORS
	if (enable)
		editState.splineMode = SplineUICreateGap;
	else
		editState.splineMode = SplineUISelectMove;
	edObjHarnessEnableSelection(!(enable > 0));
#endif
}

#ifndef NO_EDITORS

//// Registration

#endif
AUTO_RUN_EARLY;
void editorObjectTypeRegisterCurves(void)
{
#ifndef NO_EDITORS
	EditorObjectType **types = NULL;

	/////
	// NEW STUFF
	/////

	editorObjectTypeRegister(EDTYPE_CURVE_CP, NULL, curveCPFree, NULL, NULL,
											NULL, curveCPClickFunction, curveCPMarqueeFunction,
											NULL, curveCPMovementEnabled);

	edObjTypeSetSelectionCallbacks(EDTYPE_CURVE_CP, curveCPSelectFunction, curveCPDeselectFunction, curveCPSelectionChangedFunction);
	edObjTypeSetMovementCallbacks(EDTYPE_CURVE_CP, curveCPGetMatFunction, NULL, curveCPMoveFunction, curveCPEndMoveFunction);
	edObjTypeSetCompCallback(EDTYPE_CURVE_CP, curveCPCompare, NULL);
	edObjTypeSetMenuCallbacks(EDTYPE_CURVE_CP, curveCPContextMenu);

	edObjTypeActionRegister(EDTYPE_CURVE_CP, "delete", curveCPActionDelete);

	eaClear(&types);
	eaPush(&types, editorObjectTypeGet(EDTYPE_CURVE_CP));
	wleAERegisterPanel("Curve Control Point", wleAECurveCPReload, wleAECurveCPCreate, NULL, NULL, NULL, NULL, types);


	editorObjectTypeRegister(EDTYPE_CURVE_GAP, NULL, curveGapFree, NULL, NULL,
											NULL, NULL, NULL,
											NULL, curveGapMovementEnabled);
	edObjTypeSetCompCallback(EDTYPE_CURVE_GAP, curveGapCompare, NULL);
	edObjTypeSetMovementCallbacks(EDTYPE_CURVE_GAP, curveGapGetMatFunction, NULL, curveGapMoveFunction, curveGapEndMoveFunction);

	// curve
	eaClear(&types);
	eaPush(&types, editorObjectTypeGet(EDTYPE_CURVE_GAP));
	wleAERegisterPanel("Curve Gap", wleAECurveGapReload, wleAECurveGapCreate, NULL, NULL, NULL, NULL, types);

	edObjTypeActionRegister(EDTYPE_CURVE_GAP, "delete", curveGapActionDelete);
#endif
}

#include "autogen/CurveEditorUI_c_ast.c"

