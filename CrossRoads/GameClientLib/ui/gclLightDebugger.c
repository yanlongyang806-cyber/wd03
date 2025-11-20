/***************************************************************************
*     Copyright (c) 2008, Cryptic Studios
*     All Rights Reserved
*     Confidential Property of Cryptic Studios
***************************************************************************/

#include "gclLightDebugger.h"

#include "GfxLightDebugger.h"
#include "GfxPrimitive.h"
#include "GfxCamera.h"
#include "GfxTerrain.h"

#include "WorldLib.h"
#include "WorldColl.h"
#include "WorldGrid.h"
#include "dynAnimInterface.h"
#include "dynSkeleton.h"
#include "dynDraw.h"

#include "EditLib.h"
#include "UILib.h"
#include "UIAutoWidget.h"
#include "InputLib.h"

#include "ClientTargeting.h"
#include "Entity.h"

#include "Prefs.h"
#include "cmdparse.h"


AUTO_RUN_ANON(memBudgetAddMapping(__FILE__, BUDGET_EngineMisc););

AUTO_STRUCT;
typedef struct LightDebugger
{
	F32 alpha;						AST( NAME(Opacity) )

	AST_STOP

	UIWindow *window;
	UIRebuildableTree *uirt;
	UISkin *skin;
	UIList *light_list;
	UICheckButton *lock_list_button;
	bool lock_light_list;

	char selected_name[1024];
	GfxLightDebug **key_lights;
} LightDebugger;

#include "gclLightDebugger_c_ast.c"

static LightDebugger light_debugger;

static void gclLightDebuggerClear()
{
	eaClear(&light_debugger.key_lights);
	gfxLightDebuggerClear();
}

static UISkin *skinForMain(UISkin *base)
{
	if (!light_debugger.skin) {
		light_debugger.skin = ui_SkinCreate(base);
		light_debugger.skin->background[0].a = light_debugger.alpha * 255;
	}
	return light_debugger.skin;
}

static void gclLDOnDataChanged(UIRTNode *node, UserData userData)
{
	if (light_debugger.skin)
		light_debugger.skin->background[0].a = light_debugger.alpha * 255;

	GamePrefStoreFloat("LightDebugger_Opacity", light_debugger.alpha);
}

void gclLDStartup(void)
{
	light_debugger.alpha = GamePrefGetFloat("LightDebugger_Opacity", 170 * U8TOF32_COLOR);
}

static void updateData(bool ctrl_this_frame)
{
	GfxLightCacheBase *light_cache = gfx_light_debugger.cur_cache;

	eaDestroyStruct(&light_debugger.key_lights, parse_GfxLightDebug);

	if (ctrl_this_frame)
	{
		WorldCollCollideResults results;
		Vec3 start, end;
		DynDrawSkeleton *draw_skeleton;
		DynSkeleton *skeleton;
		Entity *ent;
		WorldCollisionEntry *coll_entry;

		light_cache = NULL;
		sprintf(light_debugger.selected_name, "No hit");
		
		editLibCursorRay(start, end);
		worldCollideRay(PARTITION_CLIENT, start, end, WC_FILTER_BIT_EDITOR | WC_FILTER_BITS_TERRAIN, &results);

		if (results.hitSomething &&
			wcoGetUserPointer(results.wco, entryCollObjectMsgHandler, &coll_entry))
		{
			GroupTracker *tracker = trackerFromTrackerHandle(SAFE_MEMBER(coll_entry, tracker_handle));
			if (tracker)
			{
				int i;
				for (i = 0; i < eaSize(&tracker->cell_entries); ++i)
				{
					WorldCellEntry *cell_entry = tracker->cell_entries[i];
					if (cell_entry->type > WCENT_BEGIN_DRAWABLES)
					{
						light_cache = (GfxLightCacheBase *)((WorldDrawableEntry *)cell_entry)->light_cache;
						if (light_cache)
						{
							char * estrRoomList = gfxLightDebuggerGetCacheRoomList(light_cache);
							sprintf(light_debugger.selected_name, "Selected: GroupDef(%s) %s Rooms: %s", tracker->def?tracker->def->name_str:"Unknown GroupDef", 
								gfxLightDebuggerIsCacheIndoors(light_cache) ? "Indoor" : "Outdoor", estrRoomList);
							estrDestroy(&estrRoomList);
						}
						break;
					}
				}
			}
			else
			if (coll_entry)
			{
				// when no trackers are available (i.e. not in edit/view layer mode), search world cell 
				// tree for corresponding drawable model entry
				WorldDrawableEntry * worldDrawable = worldCollisionEntryToWorldDrawable(coll_entry);
				if (worldDrawable)
				{
					light_cache = (GfxLightCacheBase *)worldDrawable->light_cache;
					if (light_cache)
					{
						char * estrRoomList = gfxLightDebuggerGetCacheRoomList(light_cache);
						sprintf(light_debugger.selected_name, "Selected: Model(%s) %s Rooms: %s", 
							worldDrawable->base_entry.shared_bounds->model?worldDrawable->base_entry.shared_bounds->model->name:"No model?",
							gfxLightDebuggerIsCacheIndoors(light_cache) ? "Indoor" : "Outdoor",
							estrRoomList);
						estrDestroy(&estrRoomList);
					}
				}
				else
				{
					light_cache = gfxFindTerrainLightCache(coll_entry->model, coll_entry);
					if (light_cache)
					{
						char * estrRoomList = gfxLightDebuggerGetCacheRoomList(light_cache);
						sprintf(light_debugger.selected_name, "Selected: Terrain Atlas(%p) ( %f %f %f ) %s Rooms: %s", coll_entry, 
							coll_entry->base_entry.bounds.world_matrix[3][0],
							coll_entry->base_entry.bounds.world_matrix[3][1],
							coll_entry->base_entry.bounds.world_matrix[3][2],
							gfxLightDebuggerIsCacheIndoors(light_cache) ? "Indoor" : "Outdoor",
							estrRoomList);
						estrDestroy(&estrRoomList);
					}
				}
			}
		}

		ent = getEntityUnderMouse(false);
		skeleton = ent?dynSkeletonFromGuid(ent->dyn.guidSkeleton):NULL;
		draw_skeleton = skeleton?skeleton->pDrawSkel:NULL;
		if (draw_skeleton && draw_skeleton->pLightCache)
		{
			light_cache = (GfxLightCacheBase*)draw_skeleton->pLightCache;
			sprintf(light_debugger.selected_name, "Selected: Entity (%s)", ent->debugName);
		}

		if (light_cache)
			light_debugger.lock_light_list = true;
	}

	if (!light_cache)
		sprintf(light_debugger.selected_name, "Selected: All (tap Ctrl to view lights on targeted models or entities)");

	light_debugger.key_lights = gfxGetKeyLights(light_cache);
	gfx_light_debugger.cur_cache = light_cache;
}

static void gotoLight(UIList *list, void *unused)
{
	GfxLightDebug *selected_light = NULL;
	if (selected_light = ui_ListGetSelectedObject(light_debugger.light_list))
	{
		Mat3 mat;
		Vec3 pyr;

		if (selected_light->simple_light_type == RDRLIGHT_DIRECTIONAL)
		{
			orientMat3(mat, selected_light->direction);
		}
		else
		{
			Mat4 cammat;
			Vec3 dirvec;
			gfxGetActiveCameraMatrix(cammat);
			subVec3(cammat[3], selected_light->position, dirvec);
			normalVec3(dirvec);
			orientMat3(mat, dirvec);
		}

		getMat3YPR(mat, pyr);
		globCmdParsef("setcampyr %f %f %f", DEG(pyr[0]), DEG(pyr[1]), DEG(pyr[2]));
	}
}

static void updateUI(void)
{
	UIRTNode *group;

	PERFINFO_AUTO_START_FUNC();

	ui_RebuildableTreeInit(light_debugger.uirt, &light_debugger.window->widget.children, 0, 5, UIRTOptions_YScroll);

	if (!light_debugger.lock_list_button)
	{
		light_debugger.lock_list_button = ui_CheckButtonCreate(5, 5, "Lock Light List", false);
		light_debugger.lock_list_button->statePtr = &light_debugger.lock_light_list;
	}

	ui_WidgetRemoveFromGroup(UI_WIDGET(light_debugger.lock_list_button));
	ui_RebuildableTreeAddWidget(light_debugger.uirt->root, UI_WIDGET(light_debugger.lock_list_button), NULL, true, "LockLights", NULL);

	group = ui_RebuildableTreeAddGroup(light_debugger.uirt->root, "Key Lights", "KeyLights", true, NULL);
	{
		GfxLightDebug *selected_light = NULL;

		ui_RebuildableTreeAddLabel(group, light_debugger.selected_name, NULL, true);

		if (!light_debugger.light_list)
		{
			UIListColumn *column;

			light_debugger.light_list = ui_ListCreate(parse_GfxLightDebug, &light_debugger.key_lights, 14);
			light_debugger.light_list->widget.width = 1;
			light_debugger.light_list->widget.widthUnit = UIUnitPercentage;
			light_debugger.light_list->widget.leftPad = 5;
			light_debugger.light_list->widget.rightPad = 5;
			light_debugger.light_list->widget.height = 300;
			light_debugger.light_list->widget.heightUnit = UIUnitFixed;
			light_debugger.light_list->widget.topPad = 5;
			light_debugger.light_list->widget.bottomPad = 5;
			light_debugger.light_list->cbActivated = gotoLight;

			column = ui_ListColumnCreate(UIListPTName, "Type", (intptr_t)"light_type", NULL);
			column->fWidth = 75;
			ui_ListColumnSetSortable(column, true);
			ui_ListAppendColumn(light_debugger.light_list, column);

			column = ui_ListColumnCreate(UIListPTName, "Affects", (intptr_t)"affects", NULL);
			column->fWidth = 75;
			ui_ListColumnSetSortable(column, true);
			ui_ListAppendColumn(light_debugger.light_list, column);

			column = ui_ListColumnCreate(UIListPTName, "Dist", (intptr_t)"Distance", NULL);
			column->fWidth = 50;
			ui_ListColumnSetSortable(column, true);
			ui_ListAppendColumn(light_debugger.light_list, column);

			column = ui_ListColumnCreate(UIListPTName, "Rad", (intptr_t)"Radius", NULL);
			column->fWidth = 50;
			ui_ListColumnSetSortable(column, true);
			ui_ListAppendColumn(light_debugger.light_list, column);

			column = ui_ListColumnCreate(UIListPTName, "Used", (intptr_t)"UseCount", NULL);
			column->fWidth = 50;
			ui_ListColumnSetSortable(column, true);
			ui_ListAppendColumn(light_debugger.light_list, column);

			column = ui_ListColumnCreate(UIListPTName, "Dyn?", (intptr_t)"is_dynamic", NULL);
			column->fWidth = 50;
			ui_ListColumnSetSortable(column, true);
			ui_ListAppendColumn(light_debugger.light_list, column);

			column = ui_ListColumnCreate(UIListPTName, "Shad?", (intptr_t)"casts_shadows", NULL);
			column->fWidth = 50;
			ui_ListColumnSetSortable(column, true);
			ui_ListAppendColumn(light_debugger.light_list, column);

			column = ui_ListColumnCreate(UIListPTName, "ShadCur?", (intptr_t)"casts_shadows_this_frame", NULL);
			column->fWidth = 75;
			ui_ListColumnSetSortable(column, true);
			ui_ListAppendColumn(light_debugger.light_list, column);

			column = ui_ListColumnCreate(UIListPTName, "Occl?", (intptr_t)"occluded", NULL);
			column->fWidth = 50;
			ui_ListColumnSetSortable(column, true);
			ui_ListAppendColumn(light_debugger.light_list, column);

			column = ui_ListColumnCreate(UIListPTName, "Name", (intptr_t)"name", NULL);
			column->fWidth = 125;
			ui_ListColumnSetSortable(column, true);
			ui_ListAppendColumn(light_debugger.light_list, column);

			column = ui_ListColumnCreate(UIListPTName, "Room", (intptr_t)"Room", NULL);
			column->fWidth = 125;
			ui_ListColumnSetSortable(column, true);
			ui_ListAppendColumn(light_debugger.light_list, column);
			

			column = ui_ListColumnCreate(UIListPTName, "Indoor", (intptr_t)"indoors", NULL);
			column->fWidth = 50;
			ui_ListColumnSetSortable(column, true);
			ui_ListAppendColumn(light_debugger.light_list, column);


			column = ui_ListColumnCreate(UIListPTName, "Shadow Sort Val", (intptr_t)"shadow_sort_val", NULL);
			column->fWidth = 125;
			ui_ListColumnSetSortable(column, true);
			ui_ListAppendColumn(light_debugger.light_list, column);

			// sort by distance
			ui_ListSetSortedColumn(light_debugger.light_list, 2);
		}

		ui_WidgetRemoveFromGroup(UI_WIDGET(light_debugger.light_list));
		ui_ListSortEx(light_debugger.light_list, false);
		ui_RebuildableTreeAddWidget(group, UI_WIDGET(light_debugger.light_list), NULL, true, "LightList", NULL);

		if (selected_light = ui_ListGetSelectedObject(light_debugger.light_list))
		{
			UIAutoWidgetParams params = {0};
			params.disabled = true;
			ui_AutoWidgetAdd(group, parse_GfxLightDebug, "Position", selected_light, true, NULL, NULL, &params, NULL);
			ui_AutoWidgetAdd(group, parse_GfxLightDebug, "Direction", selected_light, true, NULL, NULL, &params, NULL);
			ui_AutoWidgetAdd(group, parse_GfxLightDebug, "AmbientColor", selected_light, true, NULL, NULL, &params, NULL);
			ui_AutoWidgetAdd(group, parse_GfxLightDebug, "DiffuseColor", selected_light, true, NULL, NULL, &params, NULL);
			ui_AutoWidgetAdd(group, parse_GfxLightDebug, "SpecularColor", selected_light, true, NULL, NULL, &params, NULL);

			if (selected_light->shadowed)
			{
				ui_AutoWidgetAdd(group, parse_GfxShadowLightDebug, "ShadowCasterObjs", selected_light->shadowed, true, NULL, NULL, &params, NULL);
				ui_AutoWidgetAdd(group, parse_GfxShadowLightDebug, "ShadowCasterTris", selected_light->shadowed, true, NULL, NULL, &params, NULL);
				ui_AutoWidgetAdd(group, parse_GfxShadowLightDebug, "ShadowmapQuality", selected_light->shadowed, true, NULL, NULL, &params, NULL);
			}

			ui_AutoWidgetAdd(group, parse_GfxLightDebug, "Room", selected_light, true, NULL, NULL, &params, NULL);
			ui_AutoWidgetAdd(group, parse_GfxLightDebug, "RoomLimitsLight", selected_light, true, NULL, NULL, &params, NULL);

			gfxSetDebugLight(selected_light->light_id);

			gfxSetPrimZTest(false);

			gfxDrawBasis(selected_light->world_matrix, 10.0f);
			gfxDrawBox3D(selected_light->bound_min, selected_light->bound_max, unitmat, ColorWhite, 1);
			gfxDrawBox3D(selected_light->moving_bound_min, selected_light->moving_bound_max, unitmat, ColorGreen, 1);
			switch (selected_light->simple_light_type)
			{
				xcase RDRLIGHT_SPOT:
					gfxDrawCone3D(selected_light->world_matrix, selected_light->radius_float, selected_light->angle1, -1, ColorWhite);

				xcase RDRLIGHT_PROJECTOR:
					gfxDrawPyramid3D(selected_light->world_matrix, 0, selected_light->radius_float, selected_light->angle1, selected_light->angle2, CreateColor(127, 127, 127, 80), ColorWhite);

				xcase RDRLIGHT_POINT:
					gfxDrawSphere3D(selected_light->position, selected_light->radius_float, -1, CreateColorRGB(255, 255, 255), 1);

				xdefault:
					break;
			}

			gfxSetPrimZTest(true);
		}
		
		if (gfx_light_debugger.cur_cache)
		{
			Vec3 cache_pos;
			gfxGetLightCacheMidPoint(gfx_light_debugger.cur_cache, cache_pos);
			gfxSetPrimZTest(false);
			FOR_EACH_IN_EARRAY(light_debugger.key_lights, GfxLightDebug, light)
			{
				gfxDrawLine3D(light->position, cache_pos, ColorYellow);
			}
			FOR_EACH_END;
			gfxSetPrimZTest(true);
		}
	}

	group = ui_RebuildableTreeAddGroup(light_debugger.uirt->root, "Options", "Options", false, NULL);
	{
		UIAutoWidgetParams params = {0};

		params.type = AWT_Slider;
		params.min[0] = 0;
		params.max[0] = 1;
		ui_AutoWidgetAdd(group, parse_LightDebugger, "Opacity", &light_debugger, true, gclLDOnDataChanged, NULL, &params, NULL);
	}

	ui_RebuildableTreeDoneBuilding(light_debugger.uirt);

	PERFINFO_AUTO_STOP_FUNC();
}

void gclLDOncePerFrame(void)
{
	if(!light_debugger.window){
		return;
	}

	PERFINFO_AUTO_START_FUNC();
	if (ui_WindowIsVisible(light_debugger.window))
	{
		bool ctrl_this_frame = inpLevel(INP_CONTROL);

		PERFINFO_AUTO_START("Window saving", 1);
		GamePrefStoreInt("LightDebugger_X", light_debugger.window->widget.x);
		GamePrefStoreInt("LightDebugger_Y", light_debugger.window->widget.y);
		GamePrefStoreInt("LightDebugger_W", light_debugger.window->widget.width);
		GamePrefStoreInt("LightDebugger_H", light_debugger.window->widget.height);
		PERFINFO_AUTO_STOP();

		if (!light_debugger.lock_light_list || ctrl_this_frame)
			updateData(ctrl_this_frame);
		updateUI();
	}
	PERFINFO_AUTO_STOP_FUNC();
}

AUTO_COMMAND ACMD_NAME(lightDebugger);
void gclLDShow(void)
{
	if (light_debugger.window)
	{
		if (!ui_WindowIsVisible(light_debugger.window))
		{
			ui_WindowShow(light_debugger.window);
			updateUI();
		}
		else
		{
			ui_WindowHide(light_debugger.window);
		}
		gclLightDebuggerClear();
		return;
	}

	light_debugger.window = ui_WindowCreate("Light Debugger",
		GamePrefGetInt("LightDebugger_X", 0),
		GamePrefGetInt("LightDebugger_Y", 100),
		GamePrefGetInt("LightDebugger_W", 500),
		GamePrefGetInt("LightDebugger_H", 450));
	light_debugger.window->widget.pOverrideSkin = skinForMain(UI_GET_SKIN(light_debugger.window));
	ui_WindowShow(light_debugger.window);

	light_debugger.uirt = ui_RebuildableTreeCreate();
	updateUI();
}

