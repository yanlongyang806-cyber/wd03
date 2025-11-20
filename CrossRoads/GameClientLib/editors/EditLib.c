#include "pub/EditLib.h"
#include "EditLibState.h"
#include "EditLibBudgets.h"
#include "EditLibTimingLog.h"
#include "WorldEditorClientMain.h"
#include "TerrainEditor.h"
#include "TexWordsEditor.h"
#include "TexOptEditor.h"
#include "utilitiesLib.h"
#include "file.h"
#include "net.h"
#include "GfxCamera.h"
#include "WorldLib.h"
#include "EditorManager.h"
#include "gclCommandParse.h"
#include "GlobalStateMachine.h"
#include "gclBaseStates.h"
#include "UGCEditorMain.h"
#include "bounds.h"

void lodedOncePerFrame(void);

EditLibState el_state;

void editLibStartup(int editCmd)
{
	utilitiesLibStartup();
#ifndef NO_EDITORS
	if (areEditorsPossible()) {
		worldEditorInit(editCmd);

		worldLibSetTerrainEditorCallbacks(terrainEditorLayerModeChanged,
										  terrainEditorUpdateSourceDataFilenames, terrainEditorSaveSourceData,
										  terrainEditorAddSourceData);
	}
#endif
	editLibBudgetsStartup();	
	eltlStartup();
}

void editLibSetLink(NetLink **pLink)
{
#ifndef NO_EDITORS
	if (areEditorsPossible())
		worldEditorSetLink(pLink);
#endif
}
void editLibOncePerFrame(F32 fFrameTime)
{
	if(gbNoGraphics)
	{
		return;
	}
	
	PERFINFO_AUTO_START_FUNC();
#ifndef NO_EDITORS
	if (areEditorsAllowed() || g_ui_State.bInUGCEditor)
	{	
		// If we're editing while connected to a server, and we lose the connection,
		// close all docs and leave the editor.
		if (g_ui_State.bInEditor && editState.link &&
			(!(*editState.link) || linkDisconnected(*editState.link)))
		{
			emCloseAllDocs();
			CommandEditMode(0);
		}
		else
		{
			terEdOncePerFrame(fFrameTime);
			worldEditorClientOncePerFrame(false);
			tweOncePerFrame();
			toeOncePerFrame();
			lodedOncePerFrame();
		}

		if (editState.link &&
			(!(*editState.link) || linkDisconnected(*editState.link)))
		{
			ugcEditorShutdown();

			// Can't be in ugcShutdownEditor, since this code path is
			// run during login after a login, logout
			if( !GSM_IsStateActiveOrPending( GCL_LOGIN_WAITING_UGC_EDIT_PERMISSION )) {
				ugcLoadingUpdateState( UGC_LOAD_NONE, 0 );
			}
		}
		else
		{
			ugcEditorOncePerFrame();
		}
	}
#endif
	editLibBudgetsOncePerFrame();
	eltlOncePerFrame();
	PERFINFO_AUTO_STOP_FUNC();
}

void editLibDrawGhosts(void)
{
	if (isProductionEditMode())
	{
		ugcEditorDrawGhosts();
	}
}

// converts screen coords into a start position and a direction
void editLibCursorRayEx(Mat4 cammat, F32 x,F32 y,Vec3 start,Vec3 dir)
{
	int		w,h;
	Vec3	dv,startdv;
	F32		aspect;
	F32		fovy = gfxGetActiveCameraFOV();
	F32		fHalfHeight;
	F32		znear = gfxGetActiveCameraNearPlaneDist();

	gfxGetActiveDeviceSize(&w,&h);
	aspect = gfxGetAspectRatio();

	fHalfHeight = ftan(RAD(fovy)*0.5f);

	dv[0] = -((x-w*0.5f)/(w*0.5f))*aspect*fHalfHeight;
	dv[1] = -((y-h*0.5f)/(h*0.5f))*fHalfHeight;
	dv[2] = -1.0f;

	// calculate a vector along the line from the camera to the cursor that will go to the nearplane
	scaleVec3(dv,-znear,startdv);

	// transform our vector into world space
	mulVecMat3(startdv,cammat,dir);

	// move the start position to the near plane
	copyVec3(cammat[3],start);
	addVec3(start,dir,start);

	// normalize our direction vector
	normalVec3(dir); 
}

// converts screen coords into a start and end position
void editLibCursorSegment(Mat4 cammat, F32 x,F32 y,F32 len,Vec3 start,Vec3 end)
{
	Vec3 dir;

	editLibCursorRayEx(cammat,x,y,start,dir);

	scaleVec3(dir,len,dir);

	addVec3(start,dir,end); 
}

void editLibCursorRay(Vec3 rayStart, Vec3 rayEnd)
{
	Mat4 cam;
	int mx, my;
	PERFINFO_AUTO_START("editLibCursorRay",1);
	gfxGetActiveCameraMatrix(cam);
	mousePos(&mx, &my);
	editLibCursorSegment(cam, mx, my, 10000, rayStart, rayEnd);
	PERFINFO_AUTO_STOP();
}

// Shoot a ray from where the mouse was last clicked, instead of where it is now
void editLibCursorRayClick(Vec3 rayStart, Vec3 rayEnd, MouseButton button)
{
	Mat4 cam;
	int mx, my;
	gfxGetActiveCameraMatrix(cam);
	mouseDownPos(button, &mx, &my);
	editLibCursorSegment(cam, mx, my, 10000, rayStart, rayEnd);
}

void editLibGetScreenPos(Vec3 worldVec, Vec2 screenVec)
{
	Vec3 camVec;
	int w, h;
	GfxCameraView *camera = gfxGetActiveCameraView();
	gfxGetActiveSurfaceSize(&w, &h);
	mulVecMat4(worldVec, camera->frustum.viewmat, camVec);
	frustumGetScreenPosition(&camera->frustum, w, h, camVec, screenVec);
}


void editLibGetScreenPosOthro(Vec3 worldVec, Vec2 screenVec)
{
	Vec3 camVec, nSreenVec;
	int w, h;
	GfxCameraView *camera = gfxGetActiveCameraView();
	gfxGetActiveSurfaceSize(&w, &h);
	mulVecMat4(worldVec, camera->frustum.viewmat, camVec);
	mulVec3ProjMat44(camVec, camera->projection_matrix, nSreenVec);
	screenVec[0] = w/2.0f + nSreenVec[0]*w/2.0f;
	screenVec[1] = h/2.0f + nSreenVec[1]*h/2.0f;
	screenVec[1] = h-screenVec[1];
}

void editLibFindScreenCoords(const Vec3 minbounds, const Vec3 maxbounds, const Mat4 worldMat, Mat44 scrProjMat, Vec3 bottomLeft, Vec3 topRight)
{
	F32 tmpY;
	int scrWd, scrHt;
	Mat44 tempMat, worldAndScrnMat;
	gfxGetActiveSurfaceSize(&scrWd, &scrHt);

	// Create the 2d screen box of the actor's bounding box	
	mat43to44(worldMat, tempMat);
	mulMat44Inline(scrProjMat, tempMat, worldAndScrnMat);
	mulBoundsAA44(minbounds, maxbounds, worldAndScrnMat, bottomLeft, topRight);

	// mulBounds returns a float from -1 to 1, convert to 0 to 1, then to screen coords; 0 the z value
	// We invert the y because mouse coordinates start at 0 from the top, swap the 2 y values
	bottomLeft[0] = 0.5 * scrWd * (bottomLeft[0] + 1.0f);
	bottomLeft[1] = scrHt * (1.0 - 0.5 * (bottomLeft[1] + 1.0f));
	topRight[0] = 0.5 * scrWd * (topRight[0] + 1.0f);
	topRight[1] = scrHt * (1.0 - 0.5 * (topRight[1] + 1.0f));
	tmpY = bottomLeft[1]; bottomLeft[1] = topRight[1]; topRight[1] = tmpY;

	// set z coords to 0
	bottomLeft[2] = topRight[2] = 0;
}
