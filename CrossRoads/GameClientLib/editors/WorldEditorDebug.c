#ifndef NO_EDITORS

#include "WorldEditorClientMain.h"
#include "WorldGrid.h"
#include "wlVolumes.h"
#include "EditorManager.h"
#include "partition_enums.h"
#include "RoomConn.h"
#include "GenericMesh.h"
#include "GfxSpriteText.h"
#include "GfxPrimitive.h"
#include "GfxDebug.h"

#define WLE_DEBUG_SPRITE_SIZE 30
#define WLE_DEBUG_SPRITE_VISDIST 200

AUTO_RUN_ANON(memBudgetAddMapping(__FILE__, BUDGET_Editors););

/********************
* COMMANDS
********************/
AUTO_COMMAND ACMD_CATEGORY(Debug) ACMD_NAME("roomDebug");
void wleCmdDebugRooms(int debug)
{
	editState.debugInfo.debugRooms = debug;
	if (!debug)
	{
		gmeshFreeData(editState.debugInfo.activeMesh);
		SAFE_FREE(editState.debugInfo.activeMesh);
		hullFreeData(editState.debugInfo.activeHull);
		SAFE_FREE(editState.debugInfo.activeHull);
		eaiDestroy(&editState.debugInfo.activeTriToPlane);
	}
}

AUTO_COMMAND ACMD_CATEGORY(Debug) ACMD_NAME("roomFindLocation");
void wleCmdFindCamRoom(void)
{
	const WorldVolume **volumes;
	WorldVolumeQueryCache *queryCache = wlVolumeQueryCacheCreate(PARTITION_CLIENT, 1, NULL); 
	Mat4 camMat;
	Vec3 localMin = {-1,-1,-1};
	Vec3 localMax = {1,1,1};
	int i, volumetypes = 0;

	gfxGetActiveCameraMatrix(camMat);
	volumetypes |= wlVolumeTypeNameToBitMask("RoomVolume");
	volumetypes |= wlVolumeTypeNameToBitMask("RoomPortal");
	volumes = wlVolumeCacheQueryBoxByType(queryCache, camMat, localMin, localMax, volumetypes);
	emStatusPrintf("Camera in %i volume%s", eaSize(&volumes), eaSize(&volumes) > 0 ? "s:" : "");
	for (i = 0; i < eaSize(&volumes); i++)
	{
		WorldVolumeEntry *volume_entry = wlVolumeGetVolumeData(volumes[i]);
		emStatusPrintf("  volume%i=[%s](type %i)", i, (volume_entry && volume_entry->room) ? volume_entry->room->def_name : "NULL", wlVolumeGetVolumeType(volumes[i]));
	}
}

/********************
* VISUALIZATIONS
********************/
void wleDebugDraw(void)
{
	// draw room debugging visualization
	if (editState.debugInfo.debugRooms && editState.debugInfo.activeMesh)
	{
		GMesh *mesh = editState.debugInfo.activeMesh;
		Vec3 start, end;
		Vec2 screen_vec;
		Mat4 campos;
		int i;

		gfxGetActiveCameraMatrix(campos);

		// draw mesh
		gfxDrawGMesh(mesh, ColorYellow, false);

		// draw mesh tri normals for non-degenerate tris
		for (i = 0; i < mesh->tri_count; i++)
		{
			if (editState.debugInfo.activeTriToPlane[i] >= 0)
			{
				addVec3(mesh->positions[mesh->tris[i].idx[0]], mesh->positions[mesh->tris[i].idx[1]], start);
				addVec3(start, mesh->positions[mesh->tris[i].idx[2]], start);
				scaleVec3(start, 0.3333333, start);
				makePlaneNormal(mesh->positions[mesh->tris[i].idx[0]], mesh->positions[mesh->tris[i].idx[1]], mesh->positions[mesh->tris[i].idx[2]], end);
				scaleVec3(end, 5, end);
				addVec3(start, end, end);
				gfxDrawLine3D_2(start, end, ColorRed, ColorGreen);
			}
		}

		// label all verts by mesh index
		gfxfont_SetFont(&g_font_Sans);
		for (i = 0; i < mesh->vert_count; i++)
		{
			Vec3 temp;
			float dist = distance3(campos[3], mesh->positions[i]);
			subVec3(mesh->positions[i], campos[3], temp);
			if (dotVec3(campos[2], temp) < 0 && dist < WLE_DEBUG_SPRITE_VISDIST)
			{
				editLibGetScreenPos(mesh->positions[i], screen_vec);
				gfxfont_Printf(screen_vec[0], screen_vec[1], 10000, WLE_DEBUG_SPRITE_SIZE / dist, WLE_DEBUG_SPRITE_SIZE / dist, CENTER_XY, "%i", i);
			}
		}
	}
}

/********************
* MISC
********************/
void wleDebugCacheRoomData(RoomPartition *partition)
{
	if (!editState.debugInfo.debugRooms)
		return;

	if (!editState.debugInfo.activeMesh)
		editState.debugInfo.activeMesh = calloc(1, sizeof(*editState.debugInfo.activeMesh));
	if (!editState.debugInfo.activeHull)
		editState.debugInfo.activeHull = calloc(1, sizeof(*editState.debugInfo.activeHull));

	gmeshFreeData(editState.debugInfo.activeMesh);
	hullFreeData(editState.debugInfo.activeHull);
	eaiClear(&editState.debugInfo.activeTriToPlane);
	gmeshCopy(editState.debugInfo.activeMesh, partition->mesh, true);
	hullCopy(editState.debugInfo.activeHull, partition->hull);
	eaiCopy(&editState.debugInfo.activeTriToPlane, &partition->tri_to_plane);
}

#endif