
#include "AnimList_Common.h"
#include "ClientTargeting.h"
#include "cutscene_common.h"
#include "EditLibUIUtil.h"
#include "EditLibGizmosToolbar.h"
#include "UIGimmeButton.h"
#include "EditorManager.h"
#include "EditorPrefs.h"
#include "Entity.h"
#include "oldencounter_common.h"
#include "CostumeCommonLoad.h"
#include "dynFxInfo.h"
#include "file.h"
#include "InputKeyBind.h"
#include "InputLib.h"
#include "gclCommandParse.h"
#include "gclCutscene.h"
#include "gclDemo.h"
#include "gclEntity.h"
#include "gfxPrimitive.h"
#include "GfxTexAtlas.h"
#include "GfxSprite.h"
#include "GraphicsLib.h"
#include "SplineEditUI.h"
#include "StringCache.h"
#include "tokenstore.h"
#include "WorldEditorUI.h"
#include "wlCurve.h"
#include "cutscene_common.h"
#include "WorldGrid.h"
#include "soundLib.h"
#include "dynAnimChart.h"
#include "StringUtil.h"
#include "EditorShared.h"

#include "CutsceneEditorGlue.h"

#include "cutscene_common_h_ast.h"

#include "AutoGen/CutsceneEditorCommon_h_ast.c"
#include "AutoGen/GameServerLib_autogen_ServerCmdWrappers.h"

AUTO_RUN_ANON(memBudgetAddMapping(__FILE__, BUDGET_Editors););

#ifndef NO_EDITORS

#define CSE_POINT_COL_SIZE 2
#define CSE_UI_SIDE_PERCENT 0.05
#define CSE_UI_LEFT_IND 7
#define CSE_UI_LARGE_IND 110
#define CSE_UI_PLAY_BUTTON_HEIGHT 20
#define CSE_UI_PLAY_BUTTON_WIDTH 25
#define CSE_UI_PLAY_IMAGE_HEIGHT 15
#define CSE_UI_PLAY_IMAGE_WIDTH 15

#define CSE_SetTextFromFloat(txt, flt) {char buf[64]; sprintf(buf, "%g", (flt)); ui_TextEntrySetText((txt), buf);}
#define CSE_SetTextFromFloatPercent(txt, flt) {char buf[64]; sprintf(buf, "%g%%", (flt)*100.0f); ui_TextEntrySetText((txt), buf);}
#define CSE_SetTextFromInt(txt, i)   {char buf[64]; sprintf(buf, "%d", (i)); ui_TextEntrySetText((txt), buf);}

#define CSE_TrackTimeToCut(x) ((x)/1000.0f)
#define CSE_CutTimeToTrack(x) ((x)*1000.0f)

#define CSE_GetSelected(x) (eaSize(&(x)) == 1 ? (x)[0]->pGenPnt : NULL)
#define CSE_GetSelectedList(x) (eaSize(&(x)) == 1 ? (x)[0]->pCGT : NULL)

//////////////////////////////////////////////////////////////////////////
// Lists for combo boxes
//////////////////////////////////////////////////////////////////////////
static char **s_eaEncounterList = NULL;

//////////////////////////////////////////////////////////////////////////
// Hotkey Callbacks
//////////////////////////////////////////////////////////////////////////

static bool cutEdIsCirclePath(CutscenePath *pPath)
{
	if(!pPath)
		return false;
	return (pPath->type == CutscenePathType_Orbit || pPath->type == CutscenePathType_LookAround);
}

static CutscenePathPoint** cutEdGetPointsFromType(CutscenePath *pPath, CutsceneEditType type)
{
	CutscenePathPoint **ppPoints;
	if(type == CutsceneEditType_CameraPath)
		ppPoints = pPath->ppPositions;
	else if(type == CutsceneEditType_LookAtPath)
		ppPoints = pPath->ppTargets;
	else
		assert(false);
	return ppPoints;
}

static CutscenePathPoint* cutEdGetSelectedPoint(CutsceneEditorState *pState, CutsceneEditType type)
{
	CutscenePathPoint **ppPoints;
	if(!pState->pSelectedPath || pState->selectedPoint < 0)
		return NULL;

	ppPoints = cutEdGetPointsFromType(pState->pSelectedPath, type);
	if(pState->selectedPoint >= eaSize(&ppPoints))
		return NULL;

	return ppPoints[pState->selectedPoint];
}

static CutsceneGenericTrackInfo* cutEdGetSelectedInfo(CutsceneEditorState *pState)
{
	int i;
	CutsceneGenericTrackInfo *pSelectedInfo = NULL;
	for ( i=0; i < eaSize(&pState->ppTrackInfos); i++ ) {
		CutsceneGenericTrackInfo *pInfo = pState->ppTrackInfos[i];
		if(eaSize(&pInfo->ppSelected) > 0) {
			if(pSelectedInfo)
				return NULL;
			pSelectedInfo = pInfo;
		}
	}
	return pSelectedInfo;
}

// Even if setting the matrix may be modified
static bool cutEdGetOrSetGenPntMatrix(CutsceneEditorState *pState, CutsceneGenericTrackInfo *pInfo, void *pCGT, void *pGenPnt, Mat4 mMatrixInOut, bool bSet)
{
	Vec3 vTempPos;
	Mat4 mChildMat, mParentMat;

	if(bSet && pInfo->pSetGenPntMatFunc) {
		Mat4 mTransParentMat;

		gclCutsceneCGTParentMat(pState->pCutsceneDef, pCGT, mParentMat);
		copyMat4(mParentMat, mTransParentMat);
		transposeMat3(mTransParentMat);

		subVec3(mMatrixInOut[3], mTransParentMat[3], vTempPos);
		mulVecMat3(vTempPos, mTransParentMat, mChildMat[3]);
		mulMat3(mMatrixInOut, mTransParentMat, mChildMat);

		if(pInfo->pSetGenPntMatFunc(pInfo, pGenPnt, mChildMat)) {
			mulMat3(mChildMat, mParentMat, mMatrixInOut);
			mulVecMat3(mChildMat[3], mParentMat, vTempPos);
			addVec3(vTempPos, mParentMat[3], mMatrixInOut[3]);
			return true;
		}
	} else if (!bSet && pInfo->pGetGenPntMatFunc) {
		copyMat4(mMatrixInOut, mChildMat);
		if(pInfo->pGetGenPntMatFunc(pInfo, pGenPnt, mChildMat)) {
			gclCutsceneCGTParentMat(pState->pCutsceneDef, pCGT, mParentMat);
			mulMat3(mChildMat, mParentMat, mMatrixInOut);
			mulVecMat3(mChildMat[3], mParentMat, vTempPos);
			addVec3(vTempPos, mParentMat[3], mMatrixInOut[3]);
			return true;
		}
	}
	return false;
}

// Even if setting the matrix may be modified
static bool cutEdGetOrSetSelectedMatrix(CutsceneEditorState *pState, Mat4 mMatrixInOut, bool bSet)
{
	CutsceneGenericTrackInfo *pSelectedInfo = cutEdGetSelectedInfo(pState);
	if(pSelectedInfo) {
		const int iCount = eaSize(&pSelectedInfo->ppSelected);
		if(0 && !bSet && iCount > 1)
		{
			int i;
			Vec3 position;
			identityMat4(mMatrixInOut);
			for(i = 0; i < iCount; i++)
			{
				CutsceneDummyTrack *pCGT = pSelectedInfo->ppSelected[i]->pCGT;
				CutsceneCommonTrackData *pGenPnt = pSelectedInfo->ppSelected[i]->pGenPnt;
				Mat4 mSingleMat;
				bool bResult = cutEdGetOrSetGenPntMatrix(pState, pSelectedInfo, pCGT, pGenPnt, mSingleMat, bSet);
				if(bResult)
				{
					if(i == 0)
						copyVec3(mSingleMat[3], position);
					else
						addVec3(mSingleMat[3], position, position);
				}
				else
					return false;
			}
			scaleVec3(position, 1.0f / iCount, position);
			copyVec3(position, mMatrixInOut[3]);
			return true;
		}
		else if(iCount == 1)
		{
			CutsceneDummyTrack *pCGT = pSelectedInfo->ppSelected[0]->pCGT;
			CutsceneCommonTrackData *pGenPnt = pSelectedInfo->ppSelected[0]->pGenPnt;
			return cutEdGetOrSetGenPntMatrix(pState, pSelectedInfo, pCGT, pGenPnt, mMatrixInOut, bSet);
		}
	}
	return false;
}

static void cutEdFocusCamera(CutsceneEditorState *pState)
{
	if(pState) {
		GfxCameraController *pCamera = gfxGetActiveCameraController();
		GfxCameraView *pCameraView = gfxGetActiveCameraView();
		CutscenePathPoint *pPoint;
		Mat4 objectMat;
		Mat4 camera_matrix;
		Vec3 cam_pos;

		if(pState->editType == CutsceneEditType_LookAtPath)
			pPoint = cutEdGetSelectedPoint(pState, CutsceneEditType_LookAtPath);
		else
			pPoint = cutEdGetSelectedPoint(pState, CutsceneEditType_CameraPath);

		if(pPoint) {
			copyVec3(pPoint->pos, cam_pos);
		} else if(cutEdGetOrSetSelectedMatrix(pState, objectMat, false)) {
			copyVec3(objectMat[3], cam_pos);
		} else {
			return;
		}
		
		// Create a camera matrix from camcenter and campyr
		createMat3YPR(camera_matrix, pCamera->campyr);
		copyVec3(camera_matrix[2], camera_matrix[3]);
		scaleVec3(camera_matrix[3], 50, camera_matrix[3]);
		addVec3(cam_pos, camera_matrix[3], camera_matrix[3]);
		copyVec3(camera_matrix[3], pCamera->camcenter);

		frustumSetCameraMatrix(&pCameraView->new_frustum, camera_matrix);
	}
}

static bool cutEdCameraModeAllowed(CutscenePath *pPath)
{
	if(	pPath &&
		(pPath->type == CutscenePathType_EasyPath ||
		pPath->type == CutscenePathType_ShadowEntity ||
		pPath->type == CutscenePathType_Orbit ||
		pPath->type == CutscenePathType_LookAround ))
		return true;
	return false;
}

static bool cutEdEditingCamPos(CutsceneEditorState *pState)
{
	return (cutEdCameraModeAllowed(pState->pSelectedPath) || pState->editType == CutsceneEditType_CameraPath);
}

static bool cutEdEditingLookAtPos(CutsceneEditorState *pState)
{
	return (cutEdCameraModeAllowed(pState->pSelectedPath) || pState->editType == CutsceneEditType_LookAtPath);
}

static int cutEdGetSelectedPointAbsIdx(CutsceneEditorState *pState, CutsceneEditType iEditType)
{
	int i;
	int idx = 0;
	CutscenePathList *pList = pState->pCutsceneDef->pPathList;

	if(!pState->pSelectedPath || pState->selectedPoint < 0)
		return -1;

	for ( i=0; i < eaSize(&pList->ppPaths); i++ ) {
		CutscenePath *pPath = pList->ppPaths[i];
		if(pPath == pState->pSelectedPath)
			break;
		if(iEditType == CutsceneEditType_CameraPath)
			idx += eaSize(&pPath->ppPositions);
		else
			idx += eaSize(&pPath->ppTargets);
	}
	return idx + pState->selectedPoint;
}

static bool cutEdGetMatFromCam(CutscenePath *pPath, int selected, Mat4 newMat)
{
	if(	selected < eaSize(&pPath->ppPositions) && 
		selected < eaSize(&pPath->ppTargets) &&
		selected >= 0)
	{
		Vec3 lookDir;
		CutscenePathPoint *pCamPoint = pPath->ppPositions[selected];
		CutscenePathPoint *pLookPoint = pPath->ppTargets[selected];

		copyVec3(pCamPoint->pos, newMat[3]);

		subVec3(pLookPoint->pos, pCamPoint->pos, lookDir);
		normalVec3(lookDir);
		copyVec3(lookDir, newMat[2]);
		copyVec3(upvec, newMat[1]);
		crossVec3(upvec, lookDir, newMat[0]);
		normalVec3(newMat[0]);
		crossVec3(newMat[2], newMat[0], newMat[1]);
		return true;
	}
	return false;
}

static bool cutEdGetMatFromPoint(CutscenePathPoint **ppPoints, int selected, Mat4 newMat)
{
	if(selected < eaSize(&ppPoints) && selected >= 0)
	{
		CutscenePathPoint *pPoint = ppPoints[selected];
		copyVec3(pPoint->pos, newMat[3]);
		normalVec3(pPoint->tangent);
		copyVec3(pPoint->tangent, newMat[2]);
		crossVec3(pPoint->up, pPoint->tangent, newMat[0]);
		normalVec3(newMat[0]);
		crossVec3(newMat[2], newMat[0], newMat[1]);
		return true;
	}
	return false;
}

static bool cutEdGetParentMat(CutsceneEditorState *pState, CutsceneOffsetData *pOffset, Mat4 parentMat,bool bUseBoneRotation)
{
	CutsceneOffsetType offsetType = pOffset->offsetType;
	const char *pchCutsceneEntName = pOffset->pchCutsceneEntName;
	EntityRef entRef = pOffset->entRef;
	const char *pchBoneName = pOffset->pchBoneName;
	Entity *pEntity=NULL;

	if(offsetType == CutsceneOffsetType_Actor) {
		pEntity = entFromEntityRefAnyPartition(entRef);
	} else if (offsetType == CutsceneOffsetType_CutsceneEntity) {
		pEntity = gclCutsceneGetCutsceneEntByName(pState->pCutsceneDef, pchCutsceneEntName);
	} else {
		pEntity = entActivePlayerPtr();
	}
	if(pEntity) {
		entGetBodyMat(pEntity, parentMat);

		if (bUseBoneRotation)
		{
			// try to stomp the rotation we just composed
			entGetBoneMat(pEntity, pchBoneName, parentMat);
		}
		else
		{
			// just grab the position
			entGetBonePos(pEntity,pchBoneName,parentMat[3]);
		}
		return true;
	}
	copyMat4(unitmat, parentMat);
	return false;
}

// this code is sort of duplicated in gclCutscene.c.  If you change it here, you should change it there
static void cutEdGetOffsetMat(CutsceneEditorState *pState, CutsceneCommonTrackData *pCTD, Mat4 parentMat,bool bUseBoneRotation)
{
	//pCTD->bRelativePos can not be checked inside this function because camera paths dont have it set

	cutEdGetParentMat(pState, &pCTD->main_offset, parentMat,bUseBoneRotation);
	if(pCTD->bTwoRelativePos) {
		Vec3 dirVec;
		Mat4 tempMat;
		if(cutEdGetParentMat(pState, &pCTD->second_offset, tempMat,bUseBoneRotation)) {
			subVec3(tempMat[3], parentMat[3], dirVec);
			mat3FromFwdVector(dirVec, parentMat);
			addVec3(parentMat[3], tempMat[3], parentMat[3]);
			scaleVec3(parentMat[3], 0.5, parentMat[3]);
		}
	}
}

static bool cutEdEditingWatchPoint(CutsceneEditorState *pState, bool bForceSelected, CutsceneEditType editType, Mat4 mat /*out*/)
{
	CutscenePath *pSelectedPath = pState->pSelectedPath;
	if(!pSelectedPath)
		return false;
	if(pSelectedPath->type != CutscenePathType_WatchEntity && pSelectedPath->type != CutscenePathType_ShadowEntity)
		return false;
	if(pSelectedPath->type == CutscenePathType_WatchEntity && editType != CutsceneEditType_LookAtPath)
		return false;
	if(!bForceSelected && pState->selectedPoint < 0)
		return false;
	cutEdGetOffsetMat(pState, &pSelectedPath->common, mat, pSelectedPath->type != CutscenePathType_ShadowEntity);
	return true;
}

static void cutEdReposFromPointToGizmo(CutsceneEditorState *pState, bool bForceSelected, CutsceneEditType editType, Mat4 pointMat /*out*/)
{
	Mat4 entMat;
	if(cutEdEditingWatchPoint(pState, bForceSelected, editType, entMat))
	{
		Vec3 offset;
		mulVecMat3(pointMat[3], entMat, offset);
		addVec3(entMat[3], offset, pointMat[3]);
		if(pState->editType == CutsceneEditType_Camera)
		{
			Mat3 newMat;
			mulMat3(entMat, pointMat, newMat);
			copyMat3(newMat, pointMat);
		}
	}
}

static void cutEdSetTransMats(CutsceneEditorState *pState)
{
	if(pState->pSelectedPath && pState->selectedPoint >= 0) {
		Mat4 newMat;
		bool found = false;
		CutscenePath *pPath = pState->pSelectedPath;
		switch(pState->editType)
		{
		case CutsceneEditType_Camera:
			found = cutEdGetMatFromCam(pPath, pState->selectedPoint, newMat);
			break;
		case CutsceneEditType_CameraPath:
			found = cutEdGetMatFromPoint(pPath->ppPositions, pState->selectedPoint, newMat);
			break;
		case CutsceneEditType_LookAtPath:
			found = cutEdGetMatFromPoint(pPath->ppTargets, pState->selectedPoint, newMat);
			break;
		}

		if(found)
		{
			cutEdReposFromPointToGizmo(pState, false, pState->editType, newMat);
			RotateGizmoSetMatrix(cutEdRotateGizmo(), newMat);
			copyMat3(unitmat, newMat);
			TranslateGizmoSetMatrix(cutEdTranslateGizmo(), newMat);
		}
	} else {
		Mat4 newMat;
		if(cutEdGetOrSetSelectedMatrix(pState, newMat, false)) {
			RotateGizmoSetMatrix(cutEdRotateGizmo(), newMat);
			copyMat3(unitmat, newMat);
			TranslateGizmoSetMatrix(cutEdTranslateGizmo(), newMat);
		}
	}
}

static void cutEdSetSelectedPoint(CutsceneEditorState *pState, int selected)
{
	int i;
	int iAbsIdx;

	for ( i=0; i < eaSize(&pState->ppTrackInfos); i++ ) {
		CutsceneGenericTrackInfo *pInfo = pState->ppTrackInfos[i];
		eaClearEx(&pInfo->ppSelected, NULL);
	}

	eaiClear(&pState->piSelectedCamPosPoints);
	eaiClear(&pState->piSelectedLookAtPoints);

	pState->selectedPoint = selected;
	
	if(!pState->pSelectedPath)
		return;

	if(cutEdEditingCamPos(pState)) {
		ui_ListSetSelectedRow(pState->pUIListPosPoints, selected);
		iAbsIdx = cutEdGetSelectedPointAbsIdx(pState, CutsceneEditType_CameraPath);
		if(iAbsIdx >= 0)
			eaiPush(&pState->piSelectedCamPosPoints, iAbsIdx);
	} else {
		ui_ListSetSelectedRow(pState->pUIListPosPoints, -1);
	}

	if(cutEdEditingLookAtPos(pState)) {
		ui_ListSetSelectedRow(pState->pUIListLookAtPoints, selected);
		iAbsIdx = cutEdGetSelectedPointAbsIdx(pState, CutsceneEditType_LookAtPath);
		if(iAbsIdx >= 0)
			eaiPush(&pState->piSelectedLookAtPoints, iAbsIdx);
	} else {
		ui_ListSetSelectedRow(pState->pUIListLookAtPoints, -1);
	}

	if(selected >= 0)
		cutEdSetTransMats(pState);
}

static void cutEdUnselectAll(CutsceneEditorState *pState)
{
	if(pState) {
		cutEdSetSelectedPoint(pState, -1);
		cutEdRefreshUICommon(pState);
	}
}

static void cutEdSetEditMode(CutsceneEditMode mode)
{
	if(mode != CutsceneEditMode_Translate && mode != CutsceneEditMode_Rotate)
		return;

	cutEdCutsceneEditorState()->pUISetEditModeFunc(mode);
}

static bool cutEdDeletePointInternal(CutsceneEditorState *pState)
{
	CutsceneDef *pCutsceneDef = pState->pCutsceneDef;
	CutscenePath *pSelectedPath = pState->pSelectedPath;
	int selected = pState->selectedPoint;

	if(!cutEdIsEditable(pState))
		return false;

	if(!pSelectedPath || selected < 0)
		return false;
	if(pSelectedPath->type == CutscenePathType_WatchEntity && pState->editType == CutsceneEditType_LookAtPath)
		return false;

	if(	selected < eaSize(&pSelectedPath->ppPositions) && cutEdEditingCamPos(pState) )
	{
		CutscenePathPoint *pPoint = eaRemove(&pSelectedPath->ppPositions, selected);
		StructDestroy(parse_CutscenePathPoint, pPoint);
		if(pSelectedPath->pCamPosSpline)
			splineDeleteCP(pSelectedPath->pCamPosSpline, selected*3);
	}
	if(	selected < eaSize(&pSelectedPath->ppTargets) && cutEdEditingLookAtPos(pState) )
	{
		CutscenePathPoint *pPoint = eaRemove(&pSelectedPath->ppTargets, selected);
		StructDestroy(parse_CutscenePathPoint, pPoint);
		if(pSelectedPath->pCamTargetSpline)
			splineDeleteCP(pSelectedPath->pCamTargetSpline, selected*3);
	}

	cutEdSetSelectedPoint(pState, -1);
	cutEdRefreshUICommon(pState);
	cutEdSetUnsaved(pState);
	return true;
}

static bool cutEdIsGenPntSelected(CutsceneGenericTrackInfo *pInfo, void *pGenPnt, bool bRemove)
{
	int i;
	for ( i=0; i < eaSize(&pInfo->ppSelected); i++ ) {
		CutsceneGenPntRef *pRef = pInfo->ppSelected[i];
		if(pRef->pGenPnt == pGenPnt) {
			if(bRemove) {
				eaRemove(&pInfo->ppSelected, i);
				free(pRef);
			}
			return true;
		}
	}
	return false;
}

static void cutEdDeleteCGTSelectedGenPnts(CutsceneGenericTrackInfo *pInfo, CutsceneDummyTrack *pCGT)
{
	int k;
	for ( k=eaSize(&pCGT->ppGenPnts)-1; k >= 0 ; k-- ) {
		CutsceneCommonPointData *pGenPnt = pCGT->ppGenPnts[k];
		if(cutEdIsGenPntSelected(pInfo, pGenPnt, true)) {
			eaRemove(&pCGT->ppGenPnts, k);
			StructDestroyVoid(pInfo->pGenPntPti, pGenPnt);
		}
	}
}

static void cutEdDeleteSelectedGenPnts(CutsceneEditorState *pState)
{
	int i, j;
	CutsceneDef *pCutsceneDef = pState->pCutsceneDef;
	for ( i=0; i < eaSize(&pState->ppTrackInfos); i++ ) {
		CutsceneGenericTrackInfo *pInfo = pState->ppTrackInfos[i];
		if(pInfo->bMulti) {
			CutsceneDummyTrack ***pppCGTs = (CutsceneDummyTrack***)TokenStoreGetEArray(parse_CutsceneDef, pInfo->iCGTColumn, pCutsceneDef, NULL);
			for ( j=0; j < eaSize(pppCGTs); j++ ) {
				CutsceneDummyTrack *pCGT = (*pppCGTs)[j];
				cutEdDeleteCGTSelectedGenPnts(pInfo, pCGT);
			}
		} else {
			CutsceneDummyTrack *pCGT = TokenStoreGetPointer(parse_CutsceneDef, pInfo->iCGTColumn, pCutsceneDef, 0, NULL);
			if(pCGT) {
				cutEdDeleteCGTSelectedGenPnts(pInfo, pCGT);
			}
		}
	}
}

static void cutEdDeletePoint(CutsceneEditorState *pState)
{
	if(pState)
	{
		bool bDelete = true;
		bool bUIRefreshed = false;

		if(!cutEdIsEditable(pState))
			return;

		if(pState->pSelectedPath) {
			if(cutEdIsCirclePath(pState->pSelectedPath))
				bDelete = false;
			if(	pState->pSelectedPath->type == CutscenePathType_ShadowEntity && 
				(eaSize(&pState->pSelectedPath->ppPositions) <= 1 || eaSize(&pState->pSelectedPath->ppTargets) <= 1))
				bDelete = false;
		} else
			bDelete = false;

		cutEdDeleteSelectedGenPnts(pState);

		if(bDelete && cutEdDeletePointInternal(pState))
			bUIRefreshed = true;

		if(!bUIRefreshed) {
			cutEdRefreshUICommon(pState);
			cutEdSetUnsaved(pState);
		}
	}
}

static CutscenePathPoint* cutEdFindFirstPointAfterPath(CutsceneDef *pCutsceneDef, CutsceneEditType type, CutscenePath *pSelectedPath, CutscenePath **pNextPathOut)
{
	CutscenePathList *pPathList = pCutsceneDef->pPathList;
	int i, path_idx;

	assert(pPathList);

	path_idx = eaFind(&pPathList->ppPaths, pSelectedPath);
	for( i=path_idx+1; i < eaSize(&pPathList->ppPaths); i++ )
	{
		CutscenePath *pPath = pPathList->ppPaths[i];
		CutscenePathPoint **ppPoints = cutEdGetPointsFromType(pPath, type);
		int size = eaSize(&ppPoints);
		if(size > 0)
		{
			if(pNextPathOut)
				*pNextPathOut = pPath;
			return ppPoints[0];
		}
	}
	return NULL;
}

static CutscenePathPoint* cutEdGetNextPoint(CutsceneDef *pCutsceneDef, CutsceneEditType type, CutscenePath *pSelectedPath, int selectedPoint, CutscenePath **pNextPathOut)
{
	CutscenePathPoint *pNextPointRet = NULL;
	CutscenePath *pNextPathRet = NULL;
	CutscenePathPoint **ppPoints;

	if(pSelectedPath && selectedPoint >= 0)
	{
		ppPoints = cutEdGetPointsFromType(pSelectedPath, type);
		if(selectedPoint < eaSize(&ppPoints))
		{
			if(selectedPoint+1 < eaSize(&ppPoints))
			{
				pNextPointRet = ppPoints[selectedPoint+1];
				pNextPathRet = pSelectedPath;
			}
			else
			{
				pNextPointRet = cutEdFindFirstPointAfterPath(pCutsceneDef, type, pSelectedPath, &pNextPathRet);
			}
		}
	}

	if(pNextPathOut)
		*pNextPathOut = pNextPathRet;
	return pNextPointRet;
}

static Spline* cutEdPathHasSpline(CutscenePath *pPath, CutsceneEditType type)
{
	if(type == CutsceneEditType_CameraPath)
	{
		return pPath->pCamPosSpline;
	}
	else if(type == CutsceneEditType_LookAtPath)
	{
		return pPath->pCamTargetSpline;
	}
	else 
		assert(false);
	return false;
}

static F32 cutEdGetPointDist(CutsceneEditType type, CutscenePath *pPointPath1, CutscenePathPoint *pPoint1, CutscenePath *pPointPath2, CutscenePathPoint *pPoint2)
{
	F32 ret = -1;
	Spline *pSpline = cutEdPathHasSpline(pPointPath1, type);
	//If they are on the same path and we are doing spline interp
	if(pPointPath1 == pPointPath2 && pSpline)
	{
		if(nearSameVec3(pPoint1->pos, pPoint2->pos)) {
			ret = 0;
		} else {
			int idx1, idx2;
			CutscenePathPoint **ppPoints;
			ppPoints = cutEdGetPointsFromType(pPointPath1, type);
			idx1 = eaFind(&ppPoints, pPoint1);
			idx2 = eaFind(&ppPoints, pPoint2);
			assert(idx1 >= 0 && idx2 >= 0);

			{
				F32 length;
				int newIndex;
				F32 newT, remainder;
				Vec3 newPos, newUp, newTan;
				splineGetNextPointEx(pSpline, false, zerovec3, idx1*3, idx2*3, 0, 0, 1000000000, &newIndex, &newT, &length, &remainder, newPos, newUp, newTan);
				ret = length;
			}
		}
	}
	//If this is a circle path
	else if(pPointPath1 == pPointPath2 && cutEdIsCirclePath(pPointPath1))
	{
		F32 xDiff = pPoint1->pos[0] - pPoint2->pos[0];
		F32 zDiff = pPoint1->pos[2] - pPoint2->pos[2];
		F32 radius = sqrt(SQR(xDiff)+SQR(zDiff));
		ret = 2*radius*pPointPath1->angle;
		ret = ABS(ret);
	}
	//If they are on different paths or linear interp
	else
	{
		ret = distance3(pPoint1->pos, pPoint2->pos);
	}
	return ret;
}

static CutscenePathPoint* cutEdFindLastPointBeforePath(CutsceneDef *pCutsceneDef, CutsceneEditType type, CutscenePath *pSelectedPath, CutscenePath **pPrevPathOut)
{
	CutscenePathList *pPathList = pCutsceneDef->pPathList;
	int i, path_idx;

	assert(pPathList);

	path_idx = eaFind(&pPathList->ppPaths, pSelectedPath);
	for( i=path_idx-1; i >= 0 ; i-- )
	{
		CutscenePath *pPath = pPathList->ppPaths[i];
		CutscenePathPoint **ppPoints = cutEdGetPointsFromType(pPath, type);
		int size = eaSize(&ppPoints);
		if(size > 0)
		{
			if(pPrevPathOut)
				*pPrevPathOut = pPath;
			return ppPoints[size-1];
		}
	}
	return NULL;
}

static F32 cutEdGetCurrentSpeed(CutsceneDef *pCutsceneDef, CutsceneEditType type, CutscenePath *pSelectedPath, CutscenePathPoint *pSelectedPoint)
{
	CutscenePathPoint **ppPoints;
	CutscenePathPoint *pNextPoint;
	CutscenePath *pNextPath;
	int selectedPointIdx;
	F32 prevDist;
	F32 curTime;

	if(!pSelectedPoint)
	{
		int size;
		ppPoints = cutEdGetPointsFromType(pSelectedPath, type);
		size = eaSize(&ppPoints);
		if(size > 0)
		{
			pSelectedPoint = ppPoints[size-1];
		}
		else
		{
			CutscenePath *pPrevPath;
			pSelectedPoint = cutEdFindLastPointBeforePath(pCutsceneDef, type, pSelectedPath, &pPrevPath);
			if(!pSelectedPoint)//ShawnF TODO: we may want to look forward for the speed
				return CSE_DEFAULT_SPEED;
			pSelectedPath = pPrevPath;
		}
	}

	ppPoints = cutEdGetPointsFromType(pSelectedPath, type);
	selectedPointIdx = eaFind(&ppPoints, pSelectedPoint);
	assert(selectedPointIdx >= 0);

	pNextPoint = cutEdGetNextPoint(pCutsceneDef, type, pSelectedPath, selectedPointIdx, &pNextPath);
	if(!pNextPoint)
		return -1;
	curTime = pNextPoint->common.time - (pSelectedPoint->common.time + pSelectedPoint->common.length);
	if(curTime <= 0)
		return 0;
	prevDist = cutEdGetPointDist(type, pSelectedPath, pSelectedPoint, pNextPath, pNextPoint);
	return prevDist / curTime;
}

static void cutEdFixupUpVec(CutscenePathPoint *pPoints)
{
	Vec3 xVec;
	setVec3(pPoints->up, 0, 1, 0);
	crossVec3(pPoints->up, pPoints->tangent, xVec);
	normalVec3(xVec);
	crossVec3(pPoints->tangent, xVec, pPoints->up);
}

static void cutEdFixupTangent(CutscenePathPoint **ppPoints, int idx)
{
	Vec3 point1, point2;
	if(idx < 0 || idx >= eaSize(&ppPoints))
		return;

	if(idx-1 >= 0)
		copyVec3(ppPoints[idx-1]->pos, point1);
	else
		copyVec3(ppPoints[idx]->pos, point1);

	if(idx+1 < eaSize(&ppPoints))
		copyVec3(ppPoints[idx+1]->pos, point2);
	else
		copyVec3(ppPoints[idx]->pos, point2);

	subVec3(point2, point1, ppPoints[idx]->tangent);
	if(sameVec3(ppPoints[idx]->tangent, zerovec3))
		setVec3(ppPoints[idx]->tangent, 0, 0, 1);
	else
		normalVec3(ppPoints[idx]->tangent);

	cutEdFixupUpVec(ppPoints[idx]);
}

static int cutEdFixupAndInsertPoint(CutscenePathPoint ***pppPoints, CutscenePathPoint *pPoint, int selected)
{
	//If nothing selected, insert at end
	if(selected < 0 || selected >= eaSize(pppPoints))
		selected = eaSize(pppPoints)-1;

	//If size is 0, just add the point
	if(selected < 0)
	{
		eaPush(pppPoints, pPoint);
		selected = 0;
	}
	else
	{
		//If not adding to the end, place between other points
		if(selected < eaSize(pppPoints)-1)
		{
			Vec3 tempVec;
			subVec3((*pppPoints)[selected+1]->pos, (*pppPoints)[selected]->pos, tempVec);
			scaleVec3(tempVec, 0.5, tempVec);
			addVec3((*pppPoints)[selected]->pos, tempVec, pPoint->pos);
		}
		selected++;//we want to insert after, not before
		eaInsert(pppPoints, pPoint, selected);

		//Fixup Tangents for points only on the same path
		cutEdFixupTangent(*pppPoints, selected-1);
		cutEdFixupTangent(*pppPoints, selected);
		cutEdFixupTangent(*pppPoints, selected+1);
	}
	return selected;
}

static void cutEdPathReloadSplines(CutscenePath *pPath)
{
	if(pPath->pCamPosSpline)
		StructDestroy(parse_Spline, pPath->pCamPosSpline);
	pPath->pCamPosSpline = NULL;
	if(pPath->pCamTargetSpline)
		StructDestroy(parse_Spline, pPath->pCamTargetSpline);
	pPath->pCamTargetSpline = NULL;
	gclCutscenePathLoadSplines(pPath);
}

static CutscenePathPoint* cutEdGetPrevPoint(CutsceneDef *pCutsceneDef, CutsceneEditType type, CutscenePath *pSelectedPath, int selectedPoint, CutscenePath **pPrevPathOut)
{
	CutscenePathPoint *pPrevPointRet = NULL;
	CutscenePath *pPrevPathRet = NULL;
	CutscenePathPoint **ppPoints;

	if(pSelectedPath && selectedPoint >= 0)
	{
		ppPoints = cutEdGetPointsFromType(pSelectedPath, type);
		if(selectedPoint < eaSize(&ppPoints))
		{
			if(selectedPoint-1 >= 0)
			{
				pPrevPointRet = ppPoints[selectedPoint-1];
				pPrevPathRet = pSelectedPath;
			}
			else
			{
				pPrevPointRet = cutEdFindLastPointBeforePath(pCutsceneDef, type, pSelectedPath, &pPrevPathRet);
			}
		}
	}

	if(pPrevPathOut)
		*pPrevPathOut = pPrevPathRet;
	return pPrevPointRet;
}

static void cutEdFixupNewPoint(CutsceneDef *pCutsceneDef, CutsceneEditType type, CutscenePath *pSelectedPath, CutscenePathPoint *pNewPoint)
{
	CutscenePathPoint **ppPoints;
	int newPointIdx, selectedPathIdx;
	CutscenePath *pNextPath = NULL;
	CutscenePathPoint *pNextPoint;

	if(!pSelectedPath)
		return;

	ppPoints = cutEdGetPointsFromType(pSelectedPath, type);
	newPointIdx = eaFind(&ppPoints, pNewPoint);
	selectedPathIdx = eaFind(&pCutsceneDef->pPathList->ppPaths, pSelectedPath);
	assert(newPointIdx >= 0 && selectedPathIdx >= 0);

	pNextPoint = cutEdGetNextPoint(pCutsceneDef, type, pSelectedPath, newPointIdx, &pNextPath);
	//If we added to the end of the very last path
	if(!pNextPoint)
	{
		//We only want the new point to be the same speed as the previous two points
		CutscenePath *pPrevPath = NULL, *pTwoBackPath = NULL;
		CutscenePathPoint *pPrevPoint, *pTwoBackPoint;
		pPrevPoint = cutEdGetPrevPoint(pCutsceneDef, type, pSelectedPath, newPointIdx, &pPrevPath);
		if(pPrevPoint)
		{
			F32 newDist;
			CutscenePathPoint **ppPrevPoints = cutEdGetPointsFromType(pPrevPath, type);
			int prevIdx = eaFind(&ppPrevPoints, pPrevPoint);
			assert(prevIdx >= 0);

			newDist = cutEdGetPointDist(type, pPrevPath, pPrevPoint, pSelectedPath, pNewPoint);
			assert(newDist >= 0.0f);

			if(!nearSameF32(newDist, 0.0f))
			{
				F32 prevMoveTime = 0;
				pTwoBackPoint = cutEdGetPrevPoint(pCutsceneDef, type, pPrevPath, prevIdx, &pTwoBackPath);
				if(pTwoBackPoint)
					prevMoveTime = (pPrevPoint->common.time - (pTwoBackPoint->common.time + pTwoBackPoint->common.length));
				if(prevMoveTime > 0)
				{
					F32 prevDist = cutEdGetPointDist(type, pTwoBackPath, pTwoBackPoint, pPrevPath, pPrevPoint);
					assert(prevDist >= 0.0f);
					if(!nearSameF32(prevDist, 0.0f))
					{
						//Use Previous Speed
						F32 prevSpeed = prevDist / prevMoveTime;
						pNewPoint->common.time = pPrevPoint->common.time + pPrevPoint->common.length + (newDist / prevSpeed);
					}
					else
					{
						//Use Default Speed
						pNewPoint->common.time = pPrevPoint->common.time + pPrevPoint->common.length + (newDist / CSE_DEFAULT_SPEED);
					}
				}
				//If this is the second point being added to the cut scene
				else
				{
					//Use Default Speed
					pNewPoint->common.time = pPrevPoint->common.time + pPrevPoint->common.length + (newDist / CSE_DEFAULT_SPEED);
				}
			}
			else
			{
				pNewPoint->common.time = pPrevPoint->common.time + pPrevPoint->common.length;
			}
		}
		//If this is the first point being added to the cut scene
		else
		{
			pNewPoint->common.time = 0;
		}
	}
	//If we added to the middle of a path
	else
	{
		//We need to fix up the next point's speed as well as our own to keep the speed the same as it was.
		CutscenePath *pPrevPath = NULL;
		CutscenePathPoint *pPrevPoint;
		//TODO fixup times were here
		pPrevPoint = cutEdGetPrevPoint(pCutsceneDef, type, pSelectedPath, newPointIdx, &pPrevPath);
		if(!pPrevPoint) {
			pNewPoint->common.time = pNextPoint->common.time/2.0f;
		} else {
			F32 prevTime = (pPrevPoint->common.time + pPrevPoint->common.length);
			pNewPoint->common.time = prevTime + (pNextPoint->common.time-prevTime)/2.0f;
		}
	}
}

static void cutEdFixupCirclePos(CutscenePath *pPath)
{
	CutscenePathPoint *pDummyPoint;
	CutscenePathPoint *pMovePoint;
	CutscenePathPoint *pHoldPoint;
	bool bClockwise = (pPath->angle >= 0);
	Vec3 dirVec, newPosVec;
	F32 sint, cost;
	if(!cutEdIsCirclePath(pPath))
		return;

	if(pPath->type == CutscenePathType_Orbit)
	{
		pDummyPoint = pPath->ppPositions[1];
		pMovePoint = pPath->ppPositions[0];
		pHoldPoint = pPath->ppTargets[0];
		copyVec3(pPath->ppTargets[0]->pos, pPath->ppTargets[1]->pos);
		copyVec3(pPath->ppTargets[0]->tangent, pPath->ppTargets[1]->tangent);
		copyVec3(pPath->ppTargets[0]->up, pPath->ppTargets[1]->up);
		setVec3(pPath->ppPositions[0]->up, 0, 1, 0);
		setVec3(pPath->ppPositions[1]->up, 0, 1, 0);
	}
	else if(pPath->type == CutscenePathType_LookAround)
	{
		pDummyPoint = pPath->ppTargets[1];
		pMovePoint = pPath->ppTargets[0];
		pHoldPoint = pPath->ppPositions[0];
		copyVec3(pPath->ppPositions[0]->pos, pPath->ppPositions[1]->pos);
		copyVec3(pPath->ppPositions[0]->tangent, pPath->ppPositions[1]->tangent);
		copyVec3(pPath->ppPositions[0]->up, pPath->ppPositions[1]->up);
		setVec3(pPath->ppTargets[0]->up, 0, 1, 0);
		setVec3(pPath->ppTargets[1]->up, 0, 1, 0);
	}
	else 
	{
		assert(false);
	}

	sincosf(pPath->angle, &sint, &cost);
	subVec3(pMovePoint->pos, pHoldPoint->pos, dirVec);
	newPosVec[0] = dirVec[0]*cost - dirVec[2]*sint;
	newPosVec[1] = dirVec[1];
	newPosVec[2] = dirVec[2]*cost + dirVec[0]*sint;
	addVec3(newPosVec, pHoldPoint->pos, pDummyPoint->pos);

	pMovePoint->tangent[0] = dirVec[2] * (bClockwise ? -1.0f :  1.0f);
	pMovePoint->tangent[1] = 0;
	pMovePoint->tangent[2] = dirVec[0] * (bClockwise ?  1.0f : -1.0f);
	normalVec3(pMovePoint->tangent);

	subVec3(pDummyPoint->pos, pHoldPoint->pos, dirVec);
	pDummyPoint->tangent[0] = dirVec[2] * (bClockwise ? -1.0f :  1.0f);
	pDummyPoint->tangent[1] = 0;
	pDummyPoint->tangent[2] = dirVec[0] * (bClockwise ?  1.0f : -1.0f);
	normalVec3(pDummyPoint->tangent);
}

static void cutEdAddPointCB(void *unused, CutsceneEditorState *pState)
{
	CutsceneDef *pCutsceneDef = pState->pCutsceneDef;
	GfxCameraController *pCamera = gfxGetActiveCameraController();
	GfxCameraView *pCameraView = gfxGetActiveCameraView();
	CutscenePathPoint *pPos = NULL;
	CutscenePathPoint *pLookAt = NULL;
	int selected = pState->selectedPoint;
	int newlySelected = -1;
	F32 prevSpeed = 0;
	Mat4 cameraMatrix;
	Vec3 tempVec;
	Mat4 entMat, entMatInv;
	CutscenePath *pSelectedPath = pState->pSelectedPath;
	bool bEasyAdd = cutEdCameraModeAllowed(pSelectedPath);

	assert(pState);

	if(!cutEdIsEditable(pState))
		return;

	if(!pSelectedPath)
		return;

	if(pSelectedPath->type == CutscenePathType_WatchEntity)
	{
		if(eaSize(&pSelectedPath->ppTargets) == 0)
		{
			pState->editType = CutsceneEditType_LookAtPath;
		}
		else if(pState->editType != CutsceneEditType_CameraPath)
		{
			pState->editType = CutsceneEditType_CameraPath;
			pState->selectedPoint = selected = -1;
		}
	}

	eaiClear(&pState->piSelectedCamPosPoints);
	eaiClear(&pState->piSelectedLookAtPoints);
	if(pState->selectedPoint >= 0 && cutEdEditingCamPos(pState))
		eaiPush(&pState->piSelectedCamPosPoints, pState->selectedPoint);
	if(pState->selectedPoint >= 0 && cutEdEditingLookAtPos(pState))
		eaiPush(&pState->piSelectedLookAtPoints, pState->selectedPoint);

	if(pSelectedPath->type == CutscenePathType_ShadowEntity) {
		cutEdGetOffsetMat(pState, &pSelectedPath->common, entMat,false);
		transposeMat4Copy(entMat, entMatInv);
	}

	createMat3YPR(cameraMatrix, pCamera->campyr);

	if(	selected < eaSize(&pSelectedPath->ppPositions) && cutEdEditingCamPos(pState))
	{
		pPos = StructCreate(parse_CutscenePathPoint);
		copyVec3(pCamera->camcenter, pPos->pos);
		if(pSelectedPath->type == CutscenePathType_ShadowEntity)
		{
			Vec3 temp;
			mulVecMat4(pPos->pos, entMatInv, temp);
			copyVec3(temp, pPos->pos);
		}
		setVec3(pPos->tangent, 0, 0, 1);
		setVec3(pPos->up, 0, 1, 0);
		prevSpeed = cutEdGetCurrentSpeed(pCutsceneDef, CutsceneEditType_CameraPath, pSelectedPath, (selected >= 0) ? pSelectedPath->ppPositions[selected] : NULL);
		newlySelected = cutEdFixupAndInsertPoint(&pSelectedPath->ppPositions, pPos, selected);
	}

	if(	selected < eaSize(&pSelectedPath->ppTargets) && cutEdEditingLookAtPos(pState))
	{
		pLookAt = StructCreate(parse_CutscenePathPoint);
		scaleVec3(cameraMatrix[2], cutEdCameraModeAllowed(pSelectedPath) ? -100 : -1, tempVec);//camera faces -z
		addVec3(pCamera->camcenter, tempVec, pLookAt->pos);
		if(pSelectedPath->type == CutscenePathType_ShadowEntity)
		{
			Vec3 temp;
			mulVecMat4(pLookAt->pos, entMatInv, temp);
			copyVec3(temp, pLookAt->pos);
		}
		setVec3(pLookAt->tangent, 0, 0, 1);
		setVec3(pLookAt->up, 0, 1, 0);
		prevSpeed = cutEdGetCurrentSpeed(pCutsceneDef, CutsceneEditType_LookAtPath, pSelectedPath, (selected >= 0) ? pSelectedPath->ppTargets[selected] : NULL);
		newlySelected = cutEdFixupAndInsertPoint(&pSelectedPath->ppTargets, pLookAt, selected);
	}

	cutEdPathReloadSplines(pSelectedPath);

	if(bEasyAdd)
	{
		assert(pLookAt && pPos);
		if(pSelectedPath->type == CutscenePathType_ShadowEntity && eaSize(&pSelectedPath->ppPositions)==1)
		{
			setVec3same(pLookAt->pos, 0);
			setVec3(pPos->pos, 0, 25, -25);
			pPos->common.time = pLookAt->common.time = cutscene_GetPathStartTime(pCutsceneDef->pPathList, pSelectedPath)+1;
			cutEdPathReloadSplines(pSelectedPath);
		}
		else
		{
			cutEdFixupNewPoint(pCutsceneDef, CutsceneEditType_CameraPath, pSelectedPath, pPos);
			cutEdFixupNewPoint(pCutsceneDef, CutsceneEditType_LookAtPath, pSelectedPath, pLookAt);

			pPos->common.time = pLookAt->common.time = MAX(pPos->common.time, pLookAt->common.time);

			if(cutEdIsCirclePath(pSelectedPath))
			{
				CutscenePathPoint *pPos2 = StructCreate(parse_CutscenePathPoint);
				CutscenePathPoint *pLookAt2 = StructCreate(parse_CutscenePathPoint);
				F32 xDiff = pPos->pos[0] - pLookAt->pos[0];
				F32 zDiff = pPos->pos[2] - pLookAt->pos[2];
				F32 radius = sqrt(SQR(xDiff)+SQR(zDiff));
				if(prevSpeed <= 0)
					prevSpeed = CSE_DEFAULT_SPEED;
				pSelectedPath->angle = -TWOPI;
				pPos2->common.time = pLookAt2->common.time = pPos->common.time + (TWOPI*radius/prevSpeed);
				eaPush(&pSelectedPath->ppPositions, pPos2);
				eaPush(&pSelectedPath->ppTargets, pLookAt2);
				cutEdFixupCirclePos(pSelectedPath);
			}
		}
	}
	else if (pPos)
	{
		cutEdFixupNewPoint(pCutsceneDef, CutsceneEditType_CameraPath, pSelectedPath, pPos);
		if(	pSelectedPath->type == CutscenePathType_WatchEntity &&
			eaSize(&pSelectedPath->ppPositions) == 1)
		{
			pSelectedPath->ppTargets[0]->common.time = pSelectedPath->ppPositions[0]->common.time;
		}
	}
	else if (pLookAt)
	{
		if(pSelectedPath->type == CutscenePathType_WatchEntity) {
			setVec3same(pLookAt->pos, 0);
			pLookAt->common.time = cutscene_GetPathStartTime(pCutsceneDef->pPathList, pSelectedPath);
		} else {
			cutEdFixupNewPoint(pCutsceneDef, CutsceneEditType_LookAtPath, pSelectedPath, pLookAt);
		}
	}

	cutEdSetSelectedPoint(pState, newlySelected);
	ui_SetFocus(NULL);
	cutEdRefreshUICommon(pState);
	cutEdSetUnsaved(pState);
}

static void cutEdAddPoint(CutsceneEditorState *pState)
{
	if(pState)
	{
		if(cutEdIsCirclePath(pState->pSelectedPath))
			return;

		cutEdAddPointCB(NULL, pState);
	}
}

#endif

AUTO_COMMAND ACMD_CATEGORY(World, Interface) ACMD_NAME("CutsceneEditor.FocusCamera") ACMD_ACCESSLEVEL(0) ACMD_HIDE;
void cutEdFocusCameraAC()
{
#ifndef NO_EDITORS
	cutEdFocusCamera(cutEdCutsceneEditorState());
#endif
}

AUTO_COMMAND ACMD_CATEGORY(World, Interface) ACMD_NAME("CutsceneEditor.UnselectAll") ACMD_ACCESSLEVEL(0) ACMD_HIDE;
void cutEdUnselectAllAC()
{
#ifndef NO_EDITORS
	cutEdUnselectAll(cutEdCutsceneEditorState());
#endif
}

AUTO_COMMAND ACMD_CATEGORY(World, Interface) ACMD_NAME("CutsceneEditor.TransRotModeToggle") ACMD_ACCESSLEVEL(0) ACMD_HIDE;
void cutEdTransRotModeToggleAC()
{
#ifndef NO_EDITORS
	cutEdSetEditMode(cutEdCutsceneEditMode() == CutsceneEditMode_Translate ? CutsceneEditMode_Rotate : CutsceneEditMode_Translate);
#endif
}

AUTO_COMMAND ACMD_CATEGORY(World, Interface) ACMD_NAME("CutsceneEditor.TransRotMode") ACMD_ACCESSLEVEL(0) ACMD_HIDE;
void cutEdTransRotModeAC(int mode)
{
#ifndef NO_EDITORS
	cutEdSetEditMode(mode);
#endif
}

AUTO_COMMAND ACMD_CATEGORY(World, Interface) ACMD_NAME("CutsceneEditor.DeletePoint") ACMD_ACCESSLEVEL(0) ACMD_HIDE;
void cutEdDeletePointAC()
{
#ifndef NO_EDITORS
	cutEdDeletePoint(cutEdCutsceneEditorState());
#endif
}

AUTO_COMMAND ACMD_CATEGORY(World, Interface) ACMD_NAME("CutsceneEditor.AddPoint") ACMD_ACCESSLEVEL(0) ACMD_HIDE;
void cutEdAddPointAC()
{
#ifndef NO_EDITORS
	cutEdAddPoint(cutEdCutsceneEditorState());
#endif
}

//////////////////////////////////////////////////////////////////////////
// Helpers
//////////////////////////////////////////////////////////////////////////

static void cutEdUpdateSplineFromPoint(Spline *pSpline, CutscenePathPoint *pPoint, int idx)
{
	assert(	idx*3+2 < eafSize(&pSpline->spline_points) &&
			idx*3+2 < eafSize(&pSpline->spline_up) &&
			idx*3+2 < eafSize(&pSpline->spline_deltas) );
	pSpline->spline_points[idx*3 + 0] = pPoint->pos[0];
	pSpline->spline_points[idx*3 + 1] = pPoint->pos[1];
	pSpline->spline_points[idx*3 + 2] = pPoint->pos[2];
	pSpline->spline_up[idx*3 + 0] = pPoint->up[0];
	pSpline->spline_up[idx*3 + 1] = pPoint->up[1];
	pSpline->spline_up[idx*3 + 2] = pPoint->up[2];
	pSpline->spline_deltas[idx*3 + 0] = pPoint->tangent[0];
	pSpline->spline_deltas[idx*3 + 1] = pPoint->tangent[1];
	pSpline->spline_deltas[idx*3 + 2] = pPoint->tangent[2];
}

static void cutEdReposFromGizmoToPoint(CutsceneEditorState *pState, bool bForceSelected, CutsceneEditType editType, Mat4 camMat /*out*/)
{
	Mat4 entMat;
	if(cutEdEditingWatchPoint(pState, bForceSelected, editType, entMat))
	{
		Vec3 offset;
		subVec3(camMat[3], entMat[3], offset);
		transposeMat3(entMat);
		mulVecMat3(offset, entMat, camMat[3]);
		if(pState->editType == CutsceneEditType_Camera)
		{
			Mat3 newCamMat;
			mulMat3(entMat, camMat, newCamMat);
			copyMat3(newCamMat, camMat);
		}
	}
}

static void cutEdClearTimelineSelection(CutsceneEditorState *pState)
{
	int i;
	ui_TimelineClearSelection(pState->pTimeline);
	for ( i=0; i < eaSize(&pState->ppTrackInfos); i++ ) {
		CutsceneGenericTrackInfo *pInfo = pState->ppTrackInfos[i];
		eaClearEx(&pInfo->ppSelected, NULL);
	}
}

static CutscenePath* cutEdGetPathFromPoint(CutsceneEditorState *pState, CutscenePathPoint *pPoint)
{
	int i;
	for ( i=0; i < eaSize(&pState->pCutsceneDef->pPathList->ppPaths); i++ ) {
		CutscenePath *pPath = pState->pCutsceneDef->pPathList->ppPaths[i];
		if(eaFind(&pPath->ppPositions, pPoint) >= 0)
			 return pPath;
		if(eaFind(&pPath->ppTargets, pPoint) >= 0)
			return pPath;
	}
	return NULL;
}

static CutscenePathPoint* cutEdGetSelectedPointDataFromCamera(CutscenePath *pPath, int selected, Vec3 Xyz, Vec3 Pyr)
{
	Mat4 mat;
	if(!cutEdGetMatFromCam(pPath, selected, mat))
		return NULL;
	copyVec3(mat[3], Xyz);
	getMat3YPR(mat, Pyr);
	return pPath->ppPositions[selected];
}

static CutscenePathPoint* cutEdGetSelectedPointDataFromPath(CutscenePathPoint **ppPoints, int selected, Vec3 Xyz, Vec3 Pyr)
{
	Mat4 mat;
	if(!cutEdGetMatFromPoint(ppPoints, selected, mat))
		return NULL;
	copyVec3(mat[3], Xyz);
	getMat3YPR(mat, Pyr);
	return ppPoints[selected];
}

static CutscenePathPoint* cutEdGetSelectedPointData(CutsceneEditorState *pState, Vec3 Xyz, Vec3 Pyr)
{
	if(pState->pSelectedPath)
	{
		switch(pState->editType)
		{
		case CutsceneEditType_Camera:
			return cutEdGetSelectedPointDataFromCamera(pState->pSelectedPath, pState->selectedPoint, Xyz, Pyr);
		case CutsceneEditType_CameraPath:
			return cutEdGetSelectedPointDataFromPath(pState->pSelectedPath->ppPositions, pState->selectedPoint, Xyz, Pyr);
		case CutsceneEditType_LookAtPath:
			return cutEdGetSelectedPointDataFromPath(pState->pSelectedPath->ppTargets, pState->selectedPoint, Xyz, Pyr);
		}
	}
	return NULL;
}

static CutscenePathPoint* cutEdGetPointFromTypeAndIdx(CutscenePath *pPath, CutsceneEditType type, int idx)
{
	CutscenePathPoint **ppPoints = cutEdGetPointsFromType(pPath, type);
	if(idx >= 0 && idx < eaSize(&ppPoints))
		return ppPoints[idx];
	return NULL;
}

//////////////////////////////////////////////////////////////////////////
// Common UI Callbacks
//////////////////////////////////////////////////////////////////////////

static UITimelineTrack* cutEdCreateTrack(const char *pcName, CutsceneGenericTrackInfo *pInfo)
{
	UITimelineTrack *pTrack = ui_TimelineTrackCreate(pcName);
	
	if(pInfo)
	{
		pTrack->prevent_order_changes = pInfo->bPreventOrderChanges;
		pTrack->allow_overlap = pInfo->bAllowOverlap;
		pTrack->allow_resize = pInfo->bAllowResize;
	}
	else
	{
		pTrack->allow_resize = true;
		pTrack->prevent_order_changes = true;
	}

	pTrack->draw_lines = true;
	pTrack->dont_sort_frames = true;
	pTrack->draw_background = true;

	return pTrack;
}

static void cutEdApplyToCGT(CutsceneEditorState *pState, CutsceneGenericTrackInfo *pInfo, cutEdCGTApplyFunc pFunc, UserData pData)
{
	int i;
	if(pInfo->bMulti) {
		CutsceneDummyTrack ***pppCGTs = (CutsceneDummyTrack***)TokenStoreGetEArray(parse_CutsceneDef, pInfo->iCGTColumn, pState->pCutsceneDef, NULL);
		for ( i=0; i < eaSize(pppCGTs); i++ ) {
			pFunc(pState, pInfo, (*pppCGTs)[i], pData);
		}
	} else {
		CutsceneDummyTrack *pCGT = TokenStoreGetPointer(parse_CutsceneDef, pInfo->iCGTColumn, pState->pCutsceneDef, 0, NULL);
		if(pCGT)
			pFunc(pState, pInfo, pCGT, pData);
	}
}

static bool cutEdHasLocation(const CutsceneGenericTrackInfo* pInfo)
{
	return SAFE_MEMBER(pInfo, pSetGenPntMatFunc) && SAFE_MEMBER(pInfo, pGetGenPntMatFunc);
}

static bool cutEdHasBounds(const CutsceneGenericTrackInfo* pInfo)
{
	return cutEdHasLocation(pInfo) && SAFE_MEMBER(pInfo, pGetGenPntBoundsFunc);
}

static void cutEdTimelineInitGenPntPos(	CutsceneGenericTrackInfo *pInfo, 
										CutsceneCommonPointData *pNew, 
										CutsceneCommonPointData *pPrev, 
										CutsceneCommonPointData *pNext)
{	
	if(	!cutEdHasLocation(pInfo) || 
		!pInfo->pSetGenPntMatFunc || 
		!pInfo->pGetGenPntMatFunc)
	{
		return;
	}
	else
	{
		Mat4 newMat;
		Mat4 prevMat, nextMat;

		// Make sure the previous point is a point which we can grab a position from
		if (pPrev && !pInfo->pGetGenPntMatFunc(pInfo, pPrev, prevMat))
		{
			pPrev = NULL;
		}

		// Make sure the next point is a point which we can grab a position from
		if (pNext && !pInfo->pGetGenPntMatFunc(pInfo, pNext, nextMat))
		{
			pNext = NULL;
		}

		if((!pPrev && !pNext) || pInfo->bAlwaysCameraPlace)
		{
			GfxCameraController *pCamera = gfxGetActiveCameraController();
			Mat4 camMat;

			//Get position from Camera
			copyMat3(unitmat, newMat);
			createMat3YPR(camMat, pCamera->campyr);
			scaleVec3(camMat[2], -10, camMat[2]);//camera faces -z
			addVec3(pCamera->camcenter, camMat[2], newMat[3]);
		}
		else if (!pNext || !pPrev)
		{
			if (pNext)
			{
				copyMat4(nextMat, newMat);
			}
			else
			{
				copyMat4(prevMat, newMat);
			}
		}
		else
		{
			F32 scale = 0, startTime, totalTime;

			startTime = pPrev->time + pPrev->length;
			totalTime = pNext->time - startTime;
			if(totalTime);
			scale = (pNew->time - startTime) / totalTime;
			scale = CLAMP(scale, 0, 1);
			interpMat4(scale, prevMat, nextMat, newMat);
		}

		pInfo->pSetGenPntMatFunc(pInfo, pNew, newMat);
	}
}

static void cutEdAddGenPntToSelection(CutsceneEditorState *pState, CutsceneGenericTrackInfo *pInfo, CutsceneDummyTrack *pCGT, void *pGenPnt)
{
	if(eaFind(&pCGT->ppGenPnts, pGenPnt) >= 0) {
		CutsceneGenPntRef *pRef = calloc(1, sizeof(CutsceneGenPntRef));
		pRef->pCGT = pCGT;
		pRef->pGenPnt = pGenPnt;

		if(pInfo->bMulti) {
			CutsceneDummyTrack ***pppCGTs = (CutsceneDummyTrack***)TokenStoreGetEArray(parse_CutsceneDef, pInfo->iCGTColumn, pState->pCutsceneDef, NULL);
			pRef->pTrack = eaGet(&pInfo->ppTracks, eaFind(pppCGTs, pCGT));
		} else {
			pRef->pTrack = pInfo->pTrack;
		}

		eaPush(&pInfo->ppSelected, pRef);
	}
}

static void cutEdTimelineAddGenPntCB(UITimelineTrack *pTrack, int iTime, CutsceneGenericTrackInfo *pInfo)
{
	int i;
	CutsceneDummyTrack *pCGT;
	CutsceneCommonPointData *pGenPnt = NULL;
	CutsceneDef *pCutsceneDef = pInfo->pState->pCutsceneDef;
	CutsceneCommonPointData *pNewGenPnt = NULL;
	CutsceneEditorState *pState = pInfo->pState;

	if(!cutEdIsEditable(pState))
		return;

	pNewGenPnt = StructCreateVoid(pInfo->pGenPntPti);

	pNewGenPnt->time = CSE_TrackTimeToCut(iTime);

	if(pInfo->bMulti) {
		int track_idx = eaFind(&pInfo->ppTracks, pTrack);
		CutsceneDummyTrack ***pppCGTs = (CutsceneDummyTrack***)TokenStoreGetEArray(parse_CutsceneDef, pInfo->iCGTColumn, pCutsceneDef, NULL);
		assert(track_idx >= 0 && eaSize(pppCGTs) && eaSize(pppCGTs) == eaSize(&pInfo->ppTracks));
		pCGT = (*pppCGTs)[track_idx];
	} else {
		pCGT = TokenStoreGetPointer(parse_CutsceneDef, pInfo->iCGTColumn, pCutsceneDef, 0, NULL);
	}

	for ( i=0; i < eaSize(&pCGT->ppGenPnts); i++ ) {

		if (pInfo->pGenPntValidReferencePntFunc && 
			!pInfo->pGenPntValidReferencePntFunc(pInfo, pCGT->ppGenPnts[i]))
		{
			// This point cannot be used as a reference point
			continue;
		}

		pGenPnt = pCGT->ppGenPnts[i];
		if(pGenPnt->time > pNewGenPnt->time) {
			cutEdTimelineInitGenPntPos(pInfo, pNewGenPnt, (i > 0 ? pCGT->ppGenPnts[i-1] : NULL), pGenPnt);
			eaInsert(&pCGT->ppGenPnts, pNewGenPnt, i);
			if(pInfo->pInitGenPntFunc)
				pInfo->pInitGenPntFunc(pInfo, pNewGenPnt);
			cutEdClearTimelineSelection(pState);
			cutEdAddGenPntToSelection(pState, pInfo, pCGT, pNewGenPnt);
			cutEdRefreshUICommon(pState);
			cutEdSetUnsaved(pState);
			return;
		}
	}
	cutEdTimelineInitGenPntPos(pInfo, pNewGenPnt, pGenPnt, NULL);
	eaPush(&pCGT->ppGenPnts, pNewGenPnt);
	if(pInfo->pInitGenPntFunc)
		pInfo->pInitGenPntFunc(pInfo, pNewGenPnt);
	cutEdClearTimelineSelection(pState);
	cutEdAddGenPntToSelection(pState, pInfo, pCGT, pNewGenPnt);
	cutEdRefreshUICommon(pState);
	cutEdSetUnsaved(pState);
}

static void cutEdTimelineSelectAll(void *unused, UITimelineTrack *pTrack)
{
	if(pTrack)
		ui_TimelineTrackSelectAllKeyFrames(pTrack, true);
}

static void cutEdTimelineDelete(void *unused, UITimelineTrack *pTrack)
{
	CutsceneGenericTrackInfo *pInfo = pTrack->rc_label_data;
	CutsceneEditorState *pState = pInfo->pState;

	if(!cutEdIsEditable(pState))
		return;

	if(pInfo->bMulti) {
		CutsceneDummyTrack ***pppCGTs = (CutsceneDummyTrack***)TokenStoreGetEArray(parse_CutsceneDef, pInfo->iCGTColumn, pState->pCutsceneDef, NULL);
		CutsceneDummyTrack *pCGT;
		int idx = eaFind(&pInfo->ppTracks, pTrack);
		assert(idx >= 0 && idx < eaSize(pppCGTs));
		pCGT = eaRemove(pppCGTs, idx);
		StructDestroyVoid(pInfo->pCGTPti, pCGT);
	} else {
		CutsceneDummyTrack *pCGT = TokenStoreGetPointer(parse_CutsceneDef, pInfo->iCGTColumn, pState->pCutsceneDef, 0, NULL);
		assert(pCGT);
		StructDestroyVoid(pInfo->pCGTPti, pCGT);
		TokenStoreSetPointer(parse_CutsceneDef, pInfo->iCGTColumn, pState->pCutsceneDef, 0, NULL, NULL);
	}
	cutEdReInitCGTs(pState);
	cutEdRefreshUICommon(pState);
	cutEdSetUnsaved(pState);
}

//////////////////////////////////////////////////////////////////////////
// Timeline Track Clipboard for Copy, Paste, Clone functionality
//////////////////////////////////////////////////////////////////////////

static ParseTable *g_pCopyBufParse;
static void *g_pCopyBuf;

static void cutEdTimelineCopy(void *unused, UITimelineTrack *pTrack)
{
	CutsceneGenericTrackInfo *pInfo = pTrack->rc_label_data;
	CutsceneEditorState *pState = pInfo->pState;
	CutsceneDummyTrack *pCGT;
	if(pInfo->bMulti) {
		CutsceneDummyTrack ***pppCGTs = (CutsceneDummyTrack***)TokenStoreGetEArray(parse_CutsceneDef, pInfo->iCGTColumn, pState->pCutsceneDef, NULL);
		int idx = eaFind(&pInfo->ppTracks, pTrack);
		assert(idx >= 0 && idx < eaSize(pppCGTs));
		pCGT = (*pppCGTs)[idx];
	} else {
		pCGT = TokenStoreGetPointer(parse_CutsceneDef, pInfo->iCGTColumn, pState->pCutsceneDef, 0, NULL);
	}
	StructDestroySafeVoid(g_pCopyBufParse, &g_pCopyBuf);
	g_pCopyBufParse = pInfo->pCGTPti;
	g_pCopyBuf = StructCloneVoid(pInfo->pCGTPti, pCGT);
}

static void cutEdGetCGTSelectedIdx(CutsceneEditorState *pState, CutsceneGenericTrackInfo *pInfo, int *iCGTIdx, int *iGenPntIdx);
static void cutEdAttemptToSelect(CutsceneEditorState *pState, CutsceneGenericTrackInfo *pInfo, int CGTIdx, int GenPntIdx);

static void cutEdTimelinePaste(void *unused, UITimelineTrack *pTrack)
{
	CutsceneGenericTrackInfo *pInfo = pTrack->rc_label_data;
	CutsceneEditorState *pState = pInfo->pState;
	int prevSelectedCGTIdx = -1;
	int prevSelectedGenPntIdx = -1;
	CutsceneGenericTrackInfo *pPrevSelectedInfo = cutEdGetSelectedInfo(pState);
	cutEdGetCGTSelectedIdx(pState, pPrevSelectedInfo, &prevSelectedCGTIdx, &prevSelectedGenPntIdx);

	if(!g_pCopyBuf || g_pCopyBufParse != pInfo->pCGTPti)
		return;

	if(!cutEdIsEditable(pState))
		return;

	if(pInfo->bMulti) {
		CutsceneDummyTrack ***pppCGTs = (CutsceneDummyTrack***)TokenStoreGetEArray(parse_CutsceneDef, pInfo->iCGTColumn, pState->pCutsceneDef, NULL);
		CutsceneDummyTrack *pCGT;
		int idx = eaFind(&pInfo->ppTracks, pTrack);
		assert(idx >= 0 && idx < eaSize(pppCGTs));
		pCGT = eaRemove(pppCGTs, idx);
		StructDestroyVoid(pInfo->pCGTPti, pCGT);
		pCGT = StructCloneVoid(g_pCopyBufParse, g_pCopyBuf);
		eaInsert(pppCGTs, pCGT, idx);
	} else {
		CutsceneDummyTrack *pCGT = TokenStoreGetPointer(parse_CutsceneDef, pInfo->iCGTColumn, pState->pCutsceneDef, 0, NULL);
		assert(pCGT);
		StructDestroyVoid(pInfo->pCGTPti, pCGT);
		pCGT = StructCloneVoid(g_pCopyBufParse, g_pCopyBuf);
		TokenStoreSetPointer(parse_CutsceneDef, pInfo->iCGTColumn, pState->pCutsceneDef, 0, pCGT, NULL);
	}

	// Reselect GenPnt
	cutEdClearTimelineSelection(pState);
	cutEdAttemptToSelect(pState, pPrevSelectedInfo, prevSelectedCGTIdx, prevSelectedGenPntIdx);

	cutEdRefreshUICommon(pState);
	cutEdSetUnsaved(pState);
}

static void cutEdTimelineClone(void *unused, UITimelineTrack *pTrack)
{
	CutsceneGenericTrackInfo *pInfo = pTrack->rc_label_data;
	CutsceneEditorState *pState = pInfo->pState;

	if(!cutEdIsEditable(pState))
		return;

	if(pInfo->bMulti) {
		CutsceneDummyTrack ***pppCGTs = (CutsceneDummyTrack***)TokenStoreGetEArray(parse_CutsceneDef, pInfo->iCGTColumn, pState->pCutsceneDef, NULL);
		int idx = eaFind(&pInfo->ppTracks, pTrack);

		assert(idx >= 0 && idx < eaSize(pppCGTs));
		eaInsert(pppCGTs, StructCloneVoid(pInfo->pCGTPti, (*pppCGTs)[idx]), idx+1);

		cutEdReInitCGTs(pState);
		cutEdRefreshUICommon(pState);
		cutEdSetUnsaved(pState);
	}
}
static void cutEdTimelineCameraTrackLabelCB(UITimelineTrack *pTrack, CutsceneEditorState *pState)
{
	if(!pState->pMenuTimeline)
		pState->pMenuTimeline = ui_MenuCreate("TimelineTrackLabelRightClick");
	else 
		ui_MenuClearAndFreeItems(pState->pMenuTimeline);

	ui_MenuAppendItem(pState->pMenuTimeline, ui_MenuItemCreate("Select All", UIMenuCallback, cutEdTimelineSelectAll, pTrack, NULL));

	ui_MenuPopupAtCursor(pState->pMenuTimeline);
}

static void cutEdTimelineGenericTrackLabelCB(UITimelineTrack *pTrack, CutsceneGenericTrackInfo *pInfo)
{
	CutsceneEditorState *pState = pInfo->pState;

	if(!pState->pMenuTimeline)
		pState->pMenuTimeline = ui_MenuCreate("TimelineTrackLabelRightClick");
	else 
		ui_MenuClearAndFreeItems(pState->pMenuTimeline);

	ui_MenuAppendItem(pState->pMenuTimeline, ui_MenuItemCreate("Delete", UIMenuCallback, cutEdTimelineDelete, pTrack, NULL));
	ui_MenuAppendItem(pState->pMenuTimeline, ui_MenuItemCreate("Copy", UIMenuCallback, cutEdTimelineCopy, pTrack, NULL));
	if(g_pCopyBuf && g_pCopyBufParse == pInfo->pCGTPti)
		ui_MenuAppendItem(pState->pMenuTimeline, ui_MenuItemCreate("Paste", UIMenuCallback, cutEdTimelinePaste, pTrack, NULL));
	if(pInfo->bMulti)
		ui_MenuAppendItem(pState->pMenuTimeline, ui_MenuItemCreate("Clone", UIMenuCallback, cutEdTimelineClone, pTrack, NULL));

	ui_MenuAppendItem(pState->pMenuTimeline, ui_MenuItemCreate("[Separator]", UIMenuSeparator, NULL, NULL, NULL));

	ui_MenuAppendItem(pState->pMenuTimeline, ui_MenuItemCreate("Select All", UIMenuCallback, cutEdTimelineSelectAll, pTrack, NULL));

	ui_MenuPopupAtCursor(pState->pMenuTimeline);
}

static void cutEdGetCGTSelectedIdx(CutsceneEditorState *pState, CutsceneGenericTrackInfo *pInfo, int *iCGTIdx, int *iGenPntIdx)
{
	if(pInfo) {
		CutsceneDummyTrack *pCGT;
		if(pInfo->bMulti) {
			CutsceneDummyTrack ***pppCGTs = (CutsceneDummyTrack***)TokenStoreGetEArray(parse_CutsceneDef, pInfo->iCGTColumn, pState->pCutsceneDef, NULL);
			(*iCGTIdx) = eaFind(pppCGTs, pInfo->ppSelected[0]->pCGT);
		}
		pCGT = pInfo->ppSelected[0]->pCGT;
		(*iGenPntIdx) = eaFind(&pCGT->ppGenPnts, pInfo->ppSelected[0]->pGenPnt);
	}
}

static void cutEdAttemptToSelect(CutsceneEditorState *pState, CutsceneGenericTrackInfo *pInfo, int CGTIdx, int GenPntIdx)
{
	if(GenPntIdx >= 0) {
		CutsceneDummyTrack *pCGT = NULL;
		if(pInfo->bMulti) {
			CutsceneDummyTrack ***pppCGTs = (CutsceneDummyTrack***)TokenStoreGetEArray(parse_CutsceneDef, pInfo->iCGTColumn, pState->pCutsceneDef, NULL);
			if(CGTIdx >= 0 && CGTIdx < eaSize(pppCGTs))
				pCGT = (*pppCGTs)[0];
		} else {
			pCGT = TokenStoreGetPointer(parse_CutsceneDef, pInfo->iCGTColumn, pState->pCutsceneDef, 0, NULL);
		}
		if(pCGT && GenPntIdx < eaSize(&pCGT->ppGenPnts)) {
			CutsceneCommonPointData *pGenPnt = pCGT->ppGenPnts[GenPntIdx];
			cutEdAddGenPntToSelection(pState, pInfo, pCGT, pGenPnt);
		}
	}
}

static bool cutEdTimlineFramePreChangedCB(UITimelineKeyFrame *pFrame, CutsceneEditorState *pState)
{
	return cutEdIsEditable(pState);
}

static bool cutEdTimlinePreChangedCB(UITimeline *pTimeline, CutsceneEditorState *pState)
{
	return cutEdIsEditable(pState);
}

static void cutEdRefreshPointUI(CutsceneEditorState *pState, CutscenePathPoint *pSelectedPoint, Vec3 Xyz, Vec3 Pyr)
{
	if(!pSelectedPoint)
	{
		pSelectedPoint = cutEdGetSelectedPointData(pState, Xyz, Pyr);
		if(!pSelectedPoint)
			return;
	}

	CSE_SetTextFromFloat(pState->pUITextPointXPos, Xyz[0]);
	CSE_SetTextFromFloat(pState->pUITextPointYPos, Xyz[1]);
	CSE_SetTextFromFloat(pState->pUITextPointZPos, Xyz[2]);
	CSE_SetTextFromFloat(pState->pUITextPointPRot, DEG(Pyr[0]));
	CSE_SetTextFromFloat(pState->pUITextPointYRot, DEG(Pyr[1]));
	CSE_SetTextFromFloat(pState->pUITextTime, pSelectedPoint->common.time);
	CSE_SetTextFromFloat(pState->pUITextLength, pSelectedPoint->common.length);

	ui_CheckButtonSetState(pState->pUICheckEaseIn, pSelectedPoint->easeIn);
	ui_CheckButtonSetState(pState->pUICheckEaseOut, pSelectedPoint->easeOut);
}

static void cutEdEditTypeCB(void *unused, int selected, CutsceneEditorState *pState)
{
	if(selected < 0)
		return;
	pState->editType = selected;
	cutEdSetSelectedPoint(pState, -1);
	cutEdRefreshUICommon(pState);
}

static void cutEdOffsetPathTimes(CutscenePath *pPath, F32 offset)
{
	int j;
	for( j=0; j < eaSize(&pPath->ppPositions); j++ ) {
		CutscenePathPoint *pPoint = pPath->ppPositions[j];
		pPoint->common.time += offset;
	}
	for( j=0; j < eaSize(&pPath->ppTargets); j++ ) {
		CutscenePathPoint *pPoint = pPath->ppTargets[j];
		pPoint->common.time += offset;
	}
	
}

static void cutEdScalePathTimes(CutscenePath *pPath, F32 scale, F32 offset, bool holds)
{
	int j;
	F32 posOffset = offset;
	F32 lookOffset = offset;
	for( j=0; j < eaSize(&pPath->ppPositions); j++ ) {
		CutscenePathPoint *pPoint = pPath->ppPositions[j];
		F32 newTime = pPoint->common.time;
		newTime -= posOffset;
		newTime *= scale;
		newTime += posOffset;
		pPoint->common.time = newTime;
		if(holds)
			pPoint->common.length *= scale;
		else
			posOffset += pPoint->common.length;
	}
	for( j=0; j < eaSize(&pPath->ppTargets); j++ ) {
		CutscenePathPoint *pPoint = pPath->ppTargets[j];
		F32 newTime = pPoint->common.time;
		newTime -= lookOffset;
		newTime *= scale;
		newTime += lookOffset;
		pPoint->common.time = newTime;
		if(holds)
			pPoint->common.length *= scale;
		else
			lookOffset += pPoint->common.length;
	}
}

static void cutEdScaleGenPntTimes(CutsceneEditorState *pState, CutsceneGenericTrackInfo *pInfo, CutsceneDummyTrack *pCGT, F32 *pData)
{
	int i;
	F32 scale = pData[0];
	F32 start = pData[1]; 
	F32 end = pData[2];
	F32 offset = (end-start)-(end-start)*scale;
	for ( i=0; i < eaSize(&pCGT->ppGenPnts); i++ ) {
		CutsceneCommonPointData *pGenPnt = pCGT->ppGenPnts[i];
		if(pGenPnt->time > start) {
			if(end > 0 && pGenPnt->time > end) {
				pGenPnt->time -= offset;
			} else  {
				pGenPnt->time -= start;
				pGenPnt->time *= scale;
				pGenPnt->time += start;
			}
		}
	}
}

static void cutEdScaleTimes(CutsceneEditorState *pState, F32 scale, bool holds)
{
	int i;
	F32 time = 0;
	CutsceneDef *pCutsceneDef = pState->pCutsceneDef;
	for( i=0; i < eaSize(&pCutsceneDef->pPathList->ppPaths); i++ )
	{
		CutscenePath *pPath = pCutsceneDef->pPathList->ppPaths[i];
		cutEdScalePathTimes(pPath, scale, time, holds);
		time += cutscene_GetPathLength(pCutsceneDef->pPathList, i, holds);
	}
	for ( i=0; i < eaSize(&pState->ppTrackInfos); i++ ) {
		CutsceneGenericTrackInfo *pInfo = pState->ppTrackInfos[i];
		F32 pData[3] = {scale, 0, 0};
		cutEdApplyToCGT(pState, pInfo, cutEdScaleGenPntTimes, pData);
	}
}

static void cutEdTimeToFadeCB(UITextEntry *pTextEntry, CutsceneEditorState *pState)
{
	F32 fFadeTime;

	if(!cutEdIsEditable(pState))
		return;

	fFadeTime = atof(ui_TextEntryGetText(pTextEntry));
	if(fFadeTime < 0)
		fFadeTime = 0;
	pState->pCutsceneDef->fTimeToFadePlayers = fFadeTime;
	cutEdRefreshUICommon(pState);
	cutEdSetUnsaved(pState);
}

static void cutEdBlendRateCB(UITextEntry *pTextEntry, CutsceneEditorState *pState)
{
	F32 rate;

	if(!cutEdIsEditable(pState))
		return;

	rate = atof(ui_TextEntryGetText(pTextEntry));
	rate = CLAMP(rate, 0.0f, 0.9f);
	pState->pCutsceneDef->fBlendRate = rate;
	cutEdRefreshUICommon(pState);
	cutEdSetUnsaved(pState);
}

static void cutEdEntsInRangeCB(UITextEntry *pTextEntry, CutsceneEditorState *pState)
{
	F32 range;

	if(!cutEdIsEditable(pState))
		return;

	range = atof(ui_TextEntryGetText(pTextEntry));
	pState->pCutsceneDef->fMinCutSceneSendRange = range;
	cutEdRefreshUICommon(pState);
	cutEdSetUnsaved(pState);
}

static void cutEdPlayOnceOnlyCB(UICheckButton *pCheck, CutsceneEditorState *pState)
{
	if(!cutEdIsEditable(pState))
		return;

	pState->pCutsceneDef->bPlayOnceOnly = ui_CheckButtonGetState(pCheck);
	cutEdRefreshUICommon(pState);
	cutEdSetUnsaved(pState);
}

static void cutEdSinglePlayerCB(UICheckButton *pCheck, CutsceneEditorState *pState)
{
	if(!cutEdIsEditable(pState))
		return;

	pState->pCutsceneDef->bSinglePlayer = ui_CheckButtonGetState(pCheck);
	cutEdRefreshUICommon(pState);
	cutEdSetUnsaved(pState);
}

static void cutEdSkippableCB(UICheckButton *pCheck, CutsceneEditorState *pState)
{
	if(!cutEdIsEditable(pState))
		return;

	pState->pCutsceneDef->bUnskippable = !ui_CheckButtonGetState(pCheck);
	cutEdRefreshUICommon(pState);
	cutEdSetUnsaved(pState);
}

static void cutEdHideAllPlayersCB(UICheckButton *pCheck, CutsceneEditorState *pState)
{
	if(!cutEdIsEditable(pState))
		return;

	pState->pCutsceneDef->bHideAllPlayers = ui_CheckButtonGetState(pCheck);
	cutEdRefreshUICommon(pState);
	cutEdSetUnsaved(pState);
}

static void cutEdMakePlayersUntargetableCB(UICheckButton *pCheck, CutsceneEditorState *pState)
{
	if(!cutEdIsEditable(pState))
		return;

	pState->pCutsceneDef->bPlayersAreUntargetable = ui_CheckButtonGetState(pCheck);
	cutEdRefreshUICommon(pState);
	cutEdSetUnsaved(pState);
}

static void cutEdDisableCamLightCB(UICheckButton *pCheck, CutsceneEditorState *pState)
{
	bool disable;

	if(!cutEdIsEditable(pState))
		return;

	disable = ui_CheckButtonGetState(pCheck);
	pState->pCutsceneDef->bDisableCamLight = disable;

	gfxEnableCameraLight(!disable);

	cutEdRefreshUICommon(pState);
	cutEdSetUnsaved(pState);
}

static void cutEdPathScaleTime(CutsceneEditorState *pState, F32 prevTime, F32 newTime, bool bHold)
{
	CutscenePathList* pPathList = pState->pCutsceneDef->pPathList;
	if(prevTime > 0 && newTime > 0) {
		int i;
		bool foundPath = false;
		F32 startTime;
		F32 prevEndTime, newEndTime;
		F32 scale = newTime/prevTime;

		startTime = cutscene_GetPathStartTime(pPathList, pState->pSelectedPath);
		prevEndTime = cutscene_GetPathEndTime(pPathList, pState->pSelectedPath);
		cutEdScalePathTimes(pState->pSelectedPath, scale, startTime, bHold);
		for ( i=0; i < eaSize(&pState->ppTrackInfos); i++ ) {
			CutsceneGenericTrackInfo *pInfo = pState->ppTrackInfos[i];
			F32 pData[3] = {scale, startTime, prevEndTime};
			cutEdApplyToCGT(pState, pInfo, cutEdScaleGenPntTimes, pData);
		}
		newEndTime = cutscene_GetPathEndTime(pPathList, pState->pSelectedPath);

		for ( i=0; i < eaSize(&pPathList->ppPaths); i++ ) {
			if(!foundPath && pPathList->ppPaths[i] == pState->pSelectedPath) {
				foundPath = true;
			} else if (foundPath) {
				cutEdOffsetPathTimes(pPathList->ppPaths[i], newEndTime-prevEndTime);
			}
		}
	}
}

static void cutEdPathTimeCB(UITextEntry *pTextEntry, CutsceneEditorState *pState)
{
	CutscenePathList* pPathList = pState->pCutsceneDef->pPathList;
	F32 prevTime = pState->cutscenePathTime;
	F32 newTime;

	if(!cutEdIsEditable(pState))
		return;

	newTime = atof(ui_TextEntryGetText(pTextEntry));

	cutEdPathScaleTime(pState, prevTime, newTime, true);

	cutEdRefreshUICommon(pState);//This will set correct time
	cutEdSetUnsaved(pState);
}

static void cutEdPathMoveTimeCB(UITextEntry *pTextEntry, CutsceneEditorState *pState)
{
	CutscenePathList* pPathList = pState->pCutsceneDef->pPathList;
	F32 prevTime = pState->cutscenePathMoveTime;
	F32 newTime;

	if(!cutEdIsEditable(pState))
		return;

	newTime = atof(ui_TextEntryGetText(pTextEntry));

	cutEdPathScaleTime(pState, prevTime, newTime, false);

	cutEdRefreshUICommon(pState);//This will set correct time
	cutEdSetUnsaved(pState);
}

static void cutEdTotalTimeCB(UITextEntry *pTextEntry, CutsceneEditorState *pState)
{
	F32 prevTime = pState->cutsceneTime;
	F32 newTime;

	if(!cutEdIsEditable(pState))
		return;

	newTime = atof(ui_TextEntryGetText(pTextEntry));

	if(prevTime > 0 && newTime > 0)
	{
		F32 scale = newTime/prevTime;
		cutEdScaleTimes(pState, scale, true);
	}
	cutEdRefreshUICommon(pState);//This will set correct time
	cutEdSetUnsaved(pState);
}

static void cutEdTotalMoveTimeCB(UITextEntry *pTextEntry, CutsceneEditorState *pState)
{
	F32 prevTime = pState->cutsceneMoveTime;
	F32 newTime;

	if(!cutEdIsEditable(pState))
		return;

	newTime = atof(ui_TextEntryGetText(pTextEntry));

	if(prevTime > 0 && newTime > 0)
	{
		F32 scale = newTime/prevTime;
		cutEdScaleTimes(pState, scale, false);
	}
	cutEdRefreshUICommon(pState);//This will set correct time
	cutEdSetUnsaved(pState);
}

static bool cutEdTimeIsBetween(F32 time, F32 start, F32 end)
{
	if(time > start && time <= end)
		return true;
	if(time == 0 && start ==0 && time <= end)
		return true;
	return false;
}

static void cutEdFixupCGT(CutsceneEditorState *pState, CutsceneGenericTrackInfo *pInfo, CutsceneDummyTrack *pCGT, CutscenePathList **ppLists)
{
	int i, j;
	int pntIdx = 0;
	F32 lastPreTime = 0;
	F32 lastPostTime = 0;
	CutscenePathList *preMove = ppLists[0];
	CutscenePathList *postMove = ppLists[1];
	assert(eaSize(&preMove->ppPaths) == eaSize(&postMove->ppPaths));

	for ( i=0; i < eaSize(&preMove->ppPaths); i++ ) {
		CutscenePath *prePath = preMove->ppPaths[i];
		CutscenePath *postPath = postMove->ppPaths[i];
		assert(eaSize(&prePath->ppPositions) == eaSize(&postPath->ppPositions));
		for ( j=0; j < eaSize(&prePath->ppPositions); j++ ) {
			CutscenePathPoint *prePoint = prePath->ppPositions[j];
			CutscenePathPoint *postPoint = postPath->ppPositions[j];
			for ( ; pntIdx < eaSize(&pCGT->ppGenPnts); pntIdx++ ) {
				CutsceneCommonPointData *pGenPnt = pCGT->ppGenPnts[pntIdx];
				if(cutEdTimeIsBetween(pGenPnt->time, lastPreTime, prePoint->common.time)) {
					F32 preLength = prePoint->common.time - lastPreTime;
					F32 postLength = postPoint->common.time - lastPostTime;
					F32 preTime = pGenPnt->time - lastPreTime;
					F32 postTime = 0;
					if(preLength != 0) {
						postTime = postLength * preTime/preLength;
					}
					pGenPnt->time = postTime + lastPostTime;
				} else {
					break;
				}
			}
			if(pntIdx >= eaSize(&pCGT->ppGenPnts))
				return;
			lastPreTime = prePoint->common.time;
			lastPostTime = postPoint->common.time;
		}
	}

	{
		F32 postDiff = lastPostTime-lastPreTime;
		for ( ; pntIdx < eaSize(&pCGT->ppGenPnts); pntIdx++ ) {
			pCGT->ppGenPnts[pntIdx]->time += postDiff;
			MAX1(pCGT->ppGenPnts[pntIdx]->time, 0.0f);
		}
	}
}

static void cutEdFixupCGTsAfterTimeChanges(CutsceneEditorState *pState, CutscenePathList *preMove, CutscenePathList *postMove)
{
	int i;
	CutscenePathList *ppLists[2] = {preMove, postMove};
	for ( i=0; i < eaSize(&pState->ppTrackInfos); i++ ) {
		cutEdApplyToCGT(pState, pState->ppTrackInfos[i], cutEdFixupCGT, ppLists);
	}
}

static void cutEdResetSpeeds(CutsceneEditorState *pState, CutscenePath *pOnlyPath)
{
	int i,j;
	CutscenePathList *pPrevPathList;
	CutsceneDef *pCutsceneDef = pState->pCutsceneDef;
	CutscenePath *pLastPosPath = NULL;
	CutscenePathPoint *pLastPos = NULL;
	CutscenePath *pLastTargetPath = NULL;
	CutscenePathPoint *pLastTarget = NULL;
	F32 prevEndTime = 0, newEndTime = 0;
	bool bPostOnlyPath = false;

	if(!cutEdIsEditable(pState))
		return;

	pPrevPathList = StructClone(parse_CutscenePathList, pCutsceneDef->pPathList);

	if(pOnlyPath)
		prevEndTime = cutscene_GetPathEndTime(pCutsceneDef->pPathList, pOnlyPath);

	for( i=0; i < eaSize(&pState->pCutsceneDef->pPathList->ppPaths); i++ )
	{
		CutscenePath *pPath = pState->pCutsceneDef->pPathList->ppPaths[i];
		bool bSync = cutEdCameraModeAllowed(pPath);
		bool editingPath = true;
		if(pOnlyPath && pOnlyPath != pPath)
			editingPath = false;
		if(bSync)
		{
			assert(eaSize(&pPath->ppPositions) == eaSize(&pPath->ppTargets));
		}
		else
		{
			for( j=0; j < eaSize(&pPath->ppTargets); j++ )
			{
				CutscenePathPoint *pPoint = pPath->ppTargets[j];
				if(pLastTarget && editingPath)
				{
					F32 dist = cutEdGetPointDist(CutsceneEditType_LookAtPath, pLastTargetPath, pLastTarget, pPath, pPoint);
					pPoint->common.time = pLastTarget->common.time + pLastTarget->common.length + (dist/CSE_DEFAULT_SPEED);
				}
				pLastTarget = pPoint;
				pLastTargetPath = pPath;
			}
		}
		for( j=0; j < eaSize(&pPath->ppPositions); j++ )
		{
			CutscenePathPoint *pPoint = pPath->ppPositions[j];
			if(pLastPos && editingPath)
			{
				F32 dist = cutEdGetPointDist(CutsceneEditType_CameraPath, pLastPosPath, pLastPos, pPath, pPoint);
				pPoint->common.time = pLastPos->common.time + pLastPos->common.length + (dist/CSE_DEFAULT_SPEED);
			}
			pLastPos = pPoint;
			pLastPosPath = pPath;
			if(bSync)
			{
				CutscenePathPoint *pLookPoint = pPath->ppTargets[j];
				if(editingPath) {
					pLookPoint->common.time = pPoint->common.time;
					pLookPoint->common.length = pPoint->common.length;
				}
				pLastTarget = pLookPoint;
				pLastTargetPath = pPath;
			}
		}

		if(pOnlyPath && editingPath) {
			newEndTime = cutscene_GetPathEndTime(pCutsceneDef->pPathList, pState->pSelectedPath);
			bPostOnlyPath = true;
		} else if (bPostOnlyPath) {
			cutEdOffsetPathTimes(pPath, newEndTime-prevEndTime);
		}
	}

	cutEdFixupCGTsAfterTimeChanges(pState, pPrevPathList, pCutsceneDef->pPathList);
	StructDestroy(parse_CutscenePathList, pPrevPathList);

	cutEdRefreshUICommon(pState);
	cutEdSetUnsaved(pState);
}

static void cutEdResetSpeedsCB(void *unused, CutsceneEditorState *pState)
{
	cutEdResetSpeeds(pState, NULL);
}

static void cutEdResetPathSpeedsCB(void *unused, CutsceneEditorState *pState)
{
	cutEdResetSpeeds(pState, pState->pSelectedPath);
}

static void cutEdTimlinePathTimeCB(UITimelineKeyFrame *pFrame, CutsceneEditorState *pState)
{
	CutscenePathPoint *pPoint = pFrame->data;
	pPoint->common.time = CSE_TrackTimeToCut(pFrame->time);
	pPoint->common.length = CSE_TrackTimeToCut(pFrame->length);
}

static UITimelineKeyFrame* cutEdCreateFrame(UITimelineTrack *pTrack, Color oColor)
{
	UITimelineKeyFrame *pFrame = ui_TimelineKeyFrameCreate();
	pFrame->color = oColor;
	ui_TimelineTrackAddFrame(pTrack, pFrame);	
	return pFrame;
}

static UITimelineKeyFrame* cutEdAddFrameFromPoint(CutsceneEditorState *pState, UITimelineTrack *pTrack, CutscenePathPoint *pPoint, CutsceneEditType type)
{
	Color color;
	UITimelineKeyFrame *pFrame = NULL;

	if(type == CutsceneEditType_CameraPath) {
		color.b = color.a = 0xFF;
		color.r = color.g = 0x7F; 
	} else {
		color.r = color.a = 0xFF;
		color.b = color.g = 0x6F; 
	}

	pFrame = cutEdCreateFrame(pTrack, color);
	pFrame->data = pPoint;
	pFrame->time = CSE_CutTimeToTrack(pPoint->common.time);
	pFrame->length = CSE_CutTimeToTrack(pPoint->common.length);
	pFrame->allow_resize = true;
	ui_TimelineKeyFrameSetPreChangedCallback(pFrame, cutEdTimlineFramePreChangedCB, pState);
	ui_TimelineKeyFrameSetChangedCallback(pFrame, cutEdTimlinePathTimeCB, pState);
	return pFrame;
}

static void cutEdGenPntChangedCB(UITimelineKeyFrame *pFrame, CutsceneEditorState *pState)
{
	CutsceneCommonPointData *pGenPnt = pFrame->data;
	pGenPnt->time = CSE_TrackTimeToCut(pFrame->time);
	pGenPnt->length = CSE_TrackTimeToCut(pFrame->length);
}

static void cutEdRefreshCGTData(CutsceneEditorState *pState, CutsceneGenericTrackInfo *pInfo, CutsceneDummyTrack *pCGT, UITimelineTrack *pTrack)
{
	int i;
	CutsceneDef *pCutsceneDef = pState->pCutsceneDef;
	UITimelineKeyFrame *pFrame;

	for ( i=0; i < eaSize(&pCGT->ppGenPnts); i++ ) {
		CutsceneCommonPointData *pGenPnt = pCGT->ppGenPnts[i];

		pFrame = ui_TimelineTrackGetFrame(pTrack, i);
		if(!pFrame) {
			pFrame = cutEdCreateFrame(pTrack, pInfo->color);
		}

		pFrame->data = pGenPnt;
		pFrame->time = CSE_CutTimeToTrack(pGenPnt->time);
		pFrame->length = CSE_CutTimeToTrack(pGenPnt->length);

		if(strcmp(pInfo->pcCGTName, "Entity") == 0)
			pFrame->allow_resize =
				((CutsceneEntityPoint *)pGenPnt)->actionType != CutsceneEntAction_Spawn && ((CutsceneEntityPoint *)pGenPnt)->actionType != CutsceneEntAction_AddStance;
		else if(strcmp(pInfo->pcCGTName, "Sounds") == 0)
			pFrame->allow_resize = !((CutsceneSoundPoint *)pGenPnt)->common.fixedLength;
		else
			pFrame->allow_resize = pTrack->allow_resize;

		ui_TimelineKeyFrameSetSelected(pFrame, cutEdIsGenPntSelected(pInfo, pGenPnt, false));
		ui_TimelineKeyFrameSetPreChangedCallback(pFrame, cutEdTimlineFramePreChangedCB, pState);
		ui_TimelineKeyFrameSetChangedCallback(pFrame, cutEdGenPntChangedCB, pState);
	}

	while(pFrame = ui_TimelineTrackGetFrame(pTrack, i)) {
		ui_TimelineTrackRemoveFrame(pTrack, pFrame);
		ui_TimelineKeyFrameFree(pFrame);
	}

	langMakeEditorCopy(parse_CutsceneDef, pCutsceneDef, true);
}

static void cutEdRefreshCGTRelPosData(CutsceneEditorState *pState)
{
	CutsceneGenericTrackInfo *pInfo = cutEdGetSelectedInfo(pState);
	if(pInfo && cutEdHasLocation(pInfo) && !demo_playingBack()) {
		CutsceneDummyTrack *pCGT = pInfo->ppSelected[0]->pCGT;

		pState->pAddContFunc(pState, pState->pRelativePosCont);

		if(!demo_playingBack()) {
			bool bSecondActive;

			ui_CheckButtonSetState(pState->pUICheckRelPosOn, pCGT->common.bRelativePos);
			ui_ComboBoxSetSelectedEnum(pState->pUIComboRelPosType, pCGT->common.main_offset.offsetType);
			ui_TextEntrySetText(pState->pUITextRelPosCutsceneEntName, pCGT->common.main_offset.pchCutsceneEntName);
			ui_TextEntrySetText(pState->pUITextRelPosEncounterName, pCGT->common.main_offset.pchStaticEncName);
			ui_TextEntrySetText(pState->pUITextRelPosActorName, pCGT->common.main_offset.pchActorName);
			ui_TextEntrySetText(pState->pUITextRelPosBoneName, pCGT->common.main_offset.pchBoneName);

			ui_SetActive(UI_WIDGET(pState->pUIComboRelPosType), pCGT->common.bRelativePos);
			ui_SetActive(UI_WIDGET(pState->pUITextRelPosCutsceneEntName), pCGT->common.bRelativePos && pCGT->common.main_offset.offsetType == CutsceneOffsetType_CutsceneEntity);
			ui_SetActive(UI_WIDGET(pState->pUITextRelPosEncounterName), pCGT->common.bRelativePos && pCGT->common.main_offset.offsetType == CutsceneOffsetType_Actor);
			ui_SetActive(UI_WIDGET(pState->pUITextRelPosActorName), pCGT->common.bRelativePos && pCGT->common.main_offset.offsetType == CutsceneOffsetType_Actor);

			ui_CheckButtonSetState(pState->pUICheckTwoRelPos, pCGT->common.bTwoRelativePos);
			ui_ComboBoxSetSelectedEnum(pState->pUIComboRelPosType2, pCGT->common.second_offset.offsetType);
			ui_TextEntrySetText(pState->pUITextRelPosCutsceneEntName2, pCGT->common.second_offset.pchCutsceneEntName);
			ui_TextEntrySetText(pState->pUITextRelPosEncounterName2, pCGT->common.second_offset.pchStaticEncName);
			ui_TextEntrySetText(pState->pUITextRelPosActorName2, pCGT->common.second_offset.pchActorName);
			ui_TextEntrySetText(pState->pUITextRelPosBoneName2, pCGT->common.second_offset.pchBoneName);

			bSecondActive = (pCGT->common.bTwoRelativePos && pCGT->common.bRelativePos);
			ui_SetActive(UI_WIDGET(pState->pUICheckTwoRelPos), pCGT->common.bRelativePos);
			ui_SetActive(UI_WIDGET(pState->pUIComboRelPosType2), bSecondActive);
			ui_SetActive(UI_WIDGET(pState->pUITextRelPosCutsceneEntName2), bSecondActive&& pCGT->common.second_offset.offsetType == CutsceneOffsetType_CutsceneEntity);
			ui_SetActive(UI_WIDGET(pState->pUITextRelPosEncounterName2), bSecondActive && pCGT->common.second_offset.offsetType == CutsceneOffsetType_Actor);
			ui_SetActive(UI_WIDGET(pState->pUITextRelPosActorName2), bSecondActive && pCGT->common.second_offset.offsetType == CutsceneOffsetType_Actor);
			ui_SetActive(UI_WIDGET(pState->pUITextRelPosBoneName2), bSecondActive);

			editor_FillEncounterActorNames(ui_TextEntryGetText(pState->pUITextRelPosEncounterName), &pState->eaRelPosActorNameModel);
			eaClear(&pState->eaRelPosBoneNameModel);
			switch(pCGT->common.main_offset.offsetType)
			{
				case CutsceneOffsetType_Actor:
				{
					EntityRef entref;
					if(editor_GetEncounterActorEntityRef(ui_TextEntryGetText(pState->pUITextRelPosEncounterName), ui_TextEntryGetText(pState->pUITextRelPosActorName), &entref))
						editor_FillEntityBoneNames(entFromEntityRefAnyPartition(entref), &pState->eaRelPosBoneNameModel);
				}
				xcase CutsceneOffsetType_Player:
				{
					editor_FillEntityBoneNames(entActivePlayerPtr(), &pState->eaRelPosBoneNameModel);
				}
				xcase CutsceneOffsetType_CutsceneEntity:
				{
					editor_FillEntityBoneNames(gclCutsceneGetCutsceneEntByName(pState->pCutsceneDef, ui_TextEntryGetText(pState->pUITextRelPosCutsceneEntName)), &pState->eaRelPosBoneNameModel);
				}
			}

			editor_FillEncounterActorNames(ui_TextEntryGetText(pState->pUITextRelPosEncounterName2), &pState->eaRelPosActorNameModel2);
			eaClear(&pState->eaRelPosBoneNameModel2);
			switch(pCGT->common.second_offset.offsetType)
			{
				case CutsceneOffsetType_Actor:
				{
					EntityRef entref;
					if(editor_GetEncounterActorEntityRef(ui_TextEntryGetText(pState->pUITextRelPosEncounterName), ui_TextEntryGetText(pState->pUITextRelPosActorName), &entref))
						editor_FillEntityBoneNames(entFromEntityRefAnyPartition(entref), &pState->eaRelPosBoneNameModel);
				}
				xcase CutsceneOffsetType_Player:
				{
					editor_FillEntityBoneNames(entActivePlayerPtr(), &pState->eaRelPosBoneNameModel);
				}
				xcase CutsceneOffsetType_CutsceneEntity:
				{
					editor_FillEntityBoneNames(gclCutsceneGetCutsceneEntByName(pState->pCutsceneDef, ui_TextEntryGetText(pState->pUITextRelPosCutsceneEntName2)), &pState->eaRelPosBoneNameModel2);
				}
			}
		}
	}	
}

static void cutEdRefreshGenPntData(CutsceneEditorState *pState)
{
	CutsceneGenericTrackInfo *pInfo = cutEdGetSelectedInfo(pState);
	if(pInfo)
		pState->pAddContFunc(pState, pState->pGenPntCont);
}

static void cutEdRefreshGenCGTData(CutsceneEditorState *pState)
{
	CutsceneGenericTrackInfo *pInfo = cutEdGetSelectedInfo(pState);
	if(pInfo) {
		CutsceneDummyTrack *pCGT = pInfo->ppSelected[0]->pCGT;
		UITimelineTrack *pTrack = pInfo->ppSelected[0]->pTrack;

		pState->pAddContFunc(pState, pState->pGenCGTCont);
		ui_TextEntrySetText(pState->pUITextTrackName, pCGT->common.pcName);

		if(pTrack->name)
			StructFreeString(pTrack->name);
		pTrack->name = StructAllocString(pCGT->common.pcName);
	}
}

static void cutEdRefreshTimeline(CutsceneEditorState *pState)
{
	int i, j;
	int pos_idx = 0;
	int look_idx = 0;
	CutsceneDef *pCutsceneDef = pState->pCutsceneDef;

	ui_TimelineClearLinks(pState->pTimeline);
	ui_TimelineClearGroups(pState->pTimeline);
	ui_TimelineTrackClearSelection(pState->pTrackCamPos);
	ui_TimelineTrackClearSelection(pState->pTrackCamLookAt);

	for ( i=0; i < eaSize(&pCutsceneDef->pPathList->ppPaths); i++ ) {
		CutscenePath *pPath = pCutsceneDef->pPathList->ppPaths[i];
		UITimelineKeyFrame **ppPathFrames = NULL;
		if(cutEdCameraModeAllowed(pPath)) {
			for ( j=0; j < eaSize(&pPath->ppPositions); j++ ) {
				CutscenePathPoint *pCamPoint = pPath->ppPositions[j];
				CutscenePathPoint *pLookPoint = pPath->ppTargets[j];
				UITimelineKeyFrame *pCamFrame = ui_TimelineTrackGetFrame(pState->pTrackCamPos, pos_idx);
				UITimelineKeyFrame *pLookFrame = ui_TimelineTrackGetFrame(pState->pTrackCamLookAt, look_idx);
				if(pCamFrame) {
					pCamFrame->time = CSE_CutTimeToTrack(pCamPoint->common.time);
					pCamFrame->length = CSE_CutTimeToTrack(pCamPoint->common.length);
					pCamFrame->data = pCamPoint;
				} else {
					pCamFrame = cutEdAddFrameFromPoint(pState, pState->pTrackCamPos, pCamPoint, CutsceneEditType_CameraPath);
				}
				if(pLookFrame) {
					pLookFrame->time = CSE_CutTimeToTrack(pLookPoint->common.time);
					pLookFrame->length = CSE_CutTimeToTrack(pLookPoint->common.length);
					pLookFrame->data = pLookPoint;
				} else {
					pLookFrame = cutEdAddFrameFromPoint(pState, pState->pTrackCamLookAt, pLookPoint, CutsceneEditType_LookAtPath);
				}
				{
					UITimelineKeyFrame **ppFrames = NULL;
					eaPush(&ppFrames, pCamFrame);
					eaPush(&ppFrames, pLookFrame);
					ui_TimelineLinkFrames(pState->pTimeline, ppFrames);
					eaDestroy(&ppFrames);
				}
				eaPush(&ppPathFrames, pCamFrame);
				eaPush(&ppPathFrames, pLookFrame);
				pos_idx++;
				look_idx++;
			}
		} else {
			for ( j=0; j < eaSize(&pPath->ppPositions); j++ ) {
				CutscenePathPoint *pPoint = pPath->ppPositions[j];
				UITimelineKeyFrame *pFrame = ui_TimelineTrackGetFrame(pState->pTrackCamPos, pos_idx);
				if(pFrame) {
					pFrame->time = CSE_CutTimeToTrack(pPoint->common.time);
					pFrame->length = CSE_CutTimeToTrack(pPoint->common.length);
					pFrame->data = pPoint;
				} else {
					pFrame = cutEdAddFrameFromPoint(pState, pState->pTrackCamPos, pPoint, CutsceneEditType_CameraPath);
				}
				eaPush(&ppPathFrames, pFrame);
				pos_idx++;
			}
			for ( j=0; j < eaSize(&pPath->ppTargets); j++ ) {
				CutscenePathPoint *pPoint = pPath->ppTargets[j];
				UITimelineKeyFrame *pFrame = ui_TimelineTrackGetFrame(pState->pTrackCamLookAt, look_idx);
				if(pFrame) {
					pFrame->time = CSE_CutTimeToTrack(pPoint->common.time);
					pFrame->length = CSE_CutTimeToTrack(pPoint->common.length);
					pFrame->data = pPoint;
				} else {
					pFrame = cutEdAddFrameFromPoint(pState, pState->pTrackCamLookAt, pPoint, CutsceneEditType_LookAtPath);
				}
				eaPush(&ppPathFrames, pFrame);
				look_idx++;
			}
		}
		ui_TimelineGroupFrames(pState->pTimeline, ppPathFrames);
		eaDestroy(&ppPathFrames);
	}
	{
		UITimelineKeyFrame *pFrame;
		while(pFrame = ui_TimelineTrackGetFrame(pState->pTrackCamPos, pos_idx)) {
			ui_TimelineTrackRemoveFrame(pState->pTrackCamPos, pFrame);
			ui_TimelineKeyFrameFree(pFrame);
		}
		while(pFrame = ui_TimelineTrackGetFrame(pState->pTrackCamLookAt, look_idx)) {
			ui_TimelineTrackRemoveFrame(pState->pTrackCamLookAt, pFrame);
			ui_TimelineKeyFrameFree(pFrame);
		}
	}

	for ( i=0; i < eaiSize(&pState->piSelectedCamPosPoints); i++ ) {
		int idx = pState->piSelectedCamPosPoints[i];
		if(idx >= 0 && idx < eaSize(&pState->pTrackCamPos->frames)) {
			ui_TimelineKeyFrameSetSelected(pState->pTrackCamPos->frames[idx], true);
		}
	}
	for ( i=0; i < eaiSize(&pState->piSelectedLookAtPoints); i++ ) {
		int idx = pState->piSelectedLookAtPoints[i];
		if(idx >= 0 && idx < eaSize(&pState->pTrackCamLookAt->frames)) {
			ui_TimelineKeyFrameSetSelected(pState->pTrackCamLookAt->frames[idx], true);
		}
	}

	//Refresh all non camera tracks
	for ( i=0; i < eaSize(&pState->ppTrackInfos); i++ ) {
		CutsceneGenericTrackInfo *pInfo = pState->ppTrackInfos[i];

		if(pInfo->bMulti) {
			CutsceneDummyTrack ***pppCGTs = (CutsceneDummyTrack***)TokenStoreGetEArray(parse_CutsceneDef, pInfo->iCGTColumn, pState->pCutsceneDef, NULL);
			assert(eaSize(&pInfo->ppTracks) == eaSize(pppCGTs));
			for ( j=0; j < eaSize(&pInfo->ppTracks); j++ ) {
				cutEdRefreshCGTData(pState, pInfo, (*pppCGTs)[j], pInfo->ppTracks[j]);
			}
		} else {
			CutsceneDummyTrack *pCGT = TokenStoreGetPointer(parse_CutsceneDef, pInfo->iCGTColumn, pState->pCutsceneDef, 0, NULL);
			if(pCGT) {
				cutEdRefreshCGTData(pState, pInfo, pCGT, pInfo->pTrack);
			}
		}
		if(eaSize(&pInfo->ppSelected)==1) {
			Mat4 transMat;

			pState->pAddContFunc(pState, pInfo->pGenPntCont);
			pInfo->pRefreshFunc(pInfo);

			if(pState->pSelectedPath && pState->selectedPoint >= 0) {
				//Do Nothing
			} else if (cutEdGetOrSetSelectedMatrix(pState, transMat, false)) {
				RotateGizmoSetMatrix(cutEdRotateGizmo(), transMat);
				copyMat3(unitmat, transMat);
				TranslateGizmoSetMatrix(cutEdTranslateGizmo(), transMat);				
			}
		}
	}
}

static F32 cutEdGetPathLength(CutscenePathList *pPathList, CutscenePath *pPath, bool bIncludeHold)
{
	int idx = eaFind(&pPathList->ppPaths, pPath);
	assert(idx >= 0);
	return cutscene_GetPathLength(pPathList, idx, bIncludeHold);
}

static void cutEdRefreshFadeUI(CutsceneGenericTrackInfo *pInfo)
{
	CutsceneEditorState *pState = pInfo->pState;
	CutsceneFadePoint *pPoint = CSE_GetSelected(pInfo->ppSelected);
	if(!pPoint)
		return;

	CSE_SetTextFromFloatPercent(pState->pUITextFadeAmount, pPoint->vFadeValue[3]);
	ui_ColorButtonSetColor(pState->pUIColorFade, pPoint->vFadeValue);

	ui_CheckButtonSetState(pState->pUIFadeAdditive, pPoint->bAdditive);
}

static void cutEdRefreshDOFUI(CutsceneGenericTrackInfo *pInfo)
{
	CutsceneEditorState *pState = pInfo->pState;
	CutsceneDOFPoint *pPoint = CSE_GetSelected(pInfo->ppSelected);
	if(!pPoint)
		return;

	ui_CheckButtonSetState(pState->pUICheckPointUseDoF, pPoint->bDOFIsOn);
	if(pPoint->bDOFIsOn) {
		CSE_SetTextFromFloat(pState->pUITextPointDoFBlur, pPoint->fDOFBlur);
		CSE_SetTextFromFloat(pState->pUITextPointDoFDist, pPoint->fDOFDist);
		ui_SetActive(UI_WIDGET(pState->pUITextPointDoFBlur), true);
		ui_SetActive(UI_WIDGET(pState->pUITextPointDoFDist), true);
	} else {
		CSE_SetTextFromFloat(pState->pUITextPointDoFBlur, 0.0f);
		CSE_SetTextFromFloat(pState->pUITextPointDoFDist, 0.0f);
		ui_SetActive(UI_WIDGET(pState->pUITextPointDoFBlur), false);
		ui_SetActive(UI_WIDGET(pState->pUITextPointDoFDist), false);
	}
}

static void cutEdRefreshFOVUI(CutsceneGenericTrackInfo *pInfo)
{
	CutsceneEditorState *pState = pInfo->pState;
	CutsceneFOVPoint *pPoint = CSE_GetSelected(pInfo->ppSelected);
	if(!pPoint)
		return;

	CSE_SetTextFromFloat(pState->pUITextFOV, pPoint->fFOV);
}

static void cutEdRefreshShakeUI(CutsceneGenericTrackInfo *pInfo)
{
	CutsceneEditorState *pState = pInfo->pState;
	CutsceneShakePoint *pPoint = CSE_GetSelected(pInfo->ppSelected);
	if(!pPoint)
		return;

	CSE_SetTextFromFloat(pState->pUIShakeTime, pPoint->common.time);
	CSE_SetTextFromFloat(pState->pUIShakeLength, pPoint->common.length);
	CSE_SetTextFromFloat(pState->pUIShakeMagnitude, pPoint->fMagnitude);
	CSE_SetTextFromFloat(pState->pUIShakeVertical, pPoint->fVertical);
	CSE_SetTextFromFloat(pState->pUIShakePan, pPoint->fPan);
}

static void cutEdRefreshObjectUI(CutsceneGenericTrackInfo *pInfo)
{
	CutsceneEditorState *pState = pInfo->pState;
	CutsceneObjectPoint *pPoint = CSE_GetSelected(pInfo->ppSelected);
	CutsceneObjectList *pList = CSE_GetSelectedList(pInfo->ppSelected);
	if(!pPoint || !pList)
		return;

	if(pList->pcObjectName && pList->pcObjectName[0])
		ui_ButtonSetText(pState->pUIObjectPickerButton, pList->pcObjectName);
	else 
		ui_ButtonSetText(pState->pUIObjectPickerButton, "Not Selected");

	CSE_SetTextFromFloatPercent(pState->pUIObjectAlpha, pPoint->fAlpha);

	CSE_SetTextFromFloat(pState->pUIObjectXPos, pPoint->vPosition[0]);
	CSE_SetTextFromFloat(pState->pUIObjectYPos, pPoint->vPosition[1]);
	CSE_SetTextFromFloat(pState->pUIObjectZPos, pPoint->vPosition[2]);
	CSE_SetTextFromFloat(pState->pUIObjectPRot, DEG(pPoint->vRotation[0]));
	CSE_SetTextFromFloat(pState->pUIObjectYRot, DEG(pPoint->vRotation[1]));
	CSE_SetTextFromFloat(pState->pUIObjectRRot, DEG(pPoint->vRotation[2]));
}

static void cutEdUpdateEntityOverlayCostumes(CutsceneEditorState *pState, CutsceneGenericTrackInfo *pInfo, void *pCont);

static void cutEdRefreshEntityUI(CutsceneGenericTrackInfo *pInfo)
{
	CutsceneEditorState *pState = pInfo->pState;
	CutsceneEntityPoint *pPoint = CSE_GetSelected(pInfo->ppSelected);
	CutsceneEntityList *pList = CSE_GetSelectedList(pInfo->ppSelected);

	if(!pPoint)
		return;

	ui_ComboBoxSetSelectedEnum(pState->pUIEntityType, pList->entityType);

	ui_ComboBoxSetSelectedsAsString(pState->pUIEntityCostume, REF_STRING_FROM_HANDLE(pList->hCostume));
	ui_SetActive(UI_WIDGET(pState->pUIEntityCostume), (pList->entityType == CutsceneEntityType_Custom));

	ui_CheckButtonSetState(pState->pUIEntityPreserveMovementFX, pList->bPreserveMovementFX);
	ui_SetActive(UI_WIDGET(pState->pUIEntityPreserveMovementFX), (pList->entityType != CutsceneEntityType_Custom));

	ui_ComboBoxSetSelectedEnum(pState->pUIEntityClassType, pList->charClassType);
	ui_SetActive(UI_WIDGET(pState->pUIEntityClassType), (pList->entityType == CutsceneEntityType_Player || pList->entityType == CutsceneEntityType_TeamSpokesman));

	CSE_SetTextFromInt(pState->pUIEntityTeamIdx, pList->EntityIdx+1);
	ui_SetActive(UI_WIDGET(pState->pUIEntityTeamIdx), (pList->entityType == CutsceneEntityType_TeamMember));
	
	ui_TextEntrySetText(pState->pUIEntityEncounterName, pList->pchStaticEncName);
	ui_TextEntrySetText(pState->pUIEntityActorName, pList->pchActorName);
	ui_SetActive(UI_WIDGET(pState->pUIEntityEncounterName), pList->entityType == CutsceneEntityType_Actor);
	ui_SetActive(UI_WIDGET(pState->pUIEntityActorName), pList->entityType == CutsceneEntityType_Actor);

	ui_ComboBoxSetSelectedEnum(pState->pUIEntityActionType, pPoint->actionType);

	CSE_SetTextFromFloat(pState->pUIEntityLength, pPoint->common.length);
	ui_SetActive(UI_WIDGET(pState->pUIEntityLength), pPoint->actionType != CutsceneEntAction_Spawn && pPoint->actionType != CutsceneEntAction_AddStance);

	ui_TextEntrySetText(pState->pUIEntityAnimation, REF_STRING_FROM_HANDLE(pPoint->hAnimList));

	CSE_SetTextFromFloat(pState->pUIEntityXPos, pPoint->vPosition[0]);
	CSE_SetTextFromFloat(pState->pUIEntityYPos, pPoint->vPosition[1]);
	CSE_SetTextFromFloat(pState->pUIEntityZPos, pPoint->vPosition[2]);
	CSE_SetTextFromFloat(pState->pUIEntityYRot, DEG(pPoint->vRotation[1]));

	ui_ComboBoxSetSelectedsAsString(pState->pUIEntityStance, pPoint->pchStance);
	ui_SetActive(UI_WIDGET(pState->pUIEntityStance), (pPoint->actionType == CutsceneEntAction_AddStance));

	ui_ComboBoxSetSelectedsAsString(pState->pUIEntityFX, pPoint->pchFXName);
	ui_CheckButtonSetState(pState->pUIEntityFXFlash, pPoint->bFlashFx);
	ui_SetActive(UI_WIDGET(pState->pUIEntityFX), (pPoint->actionType == CutsceneEntAction_PlayFx));
	ui_SetActive(UI_WIDGET(pState->pUIEntityFXFlash), (pPoint->actionType == CutsceneEntAction_PlayFx));

	ui_SetActive(UI_WIDGET(pState->pUIEntityAnimation), pPoint->actionType == CutsceneEntAction_Animation || pPoint->actionType == CutsceneEntAction_Waypoint);

	if(pPoint->actionType == CutsceneEntAction_Animation) {
		ui_SetActive(UI_WIDGET(pState->pUIEntityXPos), false);
		ui_SetActive(UI_WIDGET(pState->pUIEntityYPos), false);
		ui_SetActive(UI_WIDGET(pState->pUIEntityZPos), false);
		ui_SetActive(UI_WIDGET(pState->pUIEntityYRot), false);
	} else {
		ui_SetActive(UI_WIDGET(pState->pUIEntityXPos), true);
		ui_SetActive(UI_WIDGET(pState->pUIEntityYPos), true);
		ui_SetActive(UI_WIDGET(pState->pUIEntityZPos), true);
		ui_SetActive(UI_WIDGET(pState->pUIEntityYRot), true);
	}

	editor_FillEncounterActorNames(ui_TextEntryGetText(pState->pUIEntityEncounterName), &pState->eaEntityActorNameModel);
	cutEdUpdateEntityOverlayCostumes(pState, pInfo, pInfo->pGenPntCont);
}

static void cutEdRefreshTextureUI(CutsceneGenericTrackInfo *pInfo)
{
	CutsceneEditorState *pState = pInfo->pState;
	CutsceneTexturePoint *pPoint = CSE_GetSelected(pInfo->ppSelected);
	CutsceneTextureList *pList = CSE_GetSelectedList(pInfo->ppSelected);
	if(!pPoint)
		return;

	ui_TextEntrySetText(pState->pUITextureVariable, pList->pcTextureVariable); 
	ui_TextureEntrySetTextureName(pState->pUITextureName, (pList->pcTextureName ? pList->pcTextureName: "white"));
	ui_ComboBoxSetSelectedEnum(pState->pUITextureXAlign, pList->eXAlign);
	ui_ComboBoxSetSelectedEnum(pState->pUITextureYAlign, pList->eYAlign);

	CSE_SetTextFromFloat(pState->pUITextureScale, pPoint->fScale);
	CSE_SetTextFromFloatPercent(pState->pUITextureAlpha, pPoint->fAlpha);
	CSE_SetTextFromFloat(pState->pUITextureXPos, pPoint->vPosition[0]);
	CSE_SetTextFromFloat(pState->pUITextureYPos, pPoint->vPosition[1]);
}

static void cutEdRefreshFXUI(CutsceneGenericTrackInfo *pInfo)
{
	CutsceneEditorState *pState = pInfo->pState;
	CutsceneFXPoint *pPoint = CSE_GetSelected(pInfo->ppSelected);
	if(!pPoint)
		return;

	ui_ComboBoxSetSelectedsAsString(pState->pUIComboFX, pPoint->pcFXName);

	CSE_SetTextFromFloat(pState->pUIFXLength, pPoint->common.length);

	CSE_SetTextFromFloat(pState->pUIFXXPos, pPoint->vPosition[0]);
	CSE_SetTextFromFloat(pState->pUIFXYPos, pPoint->vPosition[1]);
	CSE_SetTextFromFloat(pState->pUIFXZPos, pPoint->vPosition[2]);
}

static void cutEdRefreshSoundUI(CutsceneGenericTrackInfo *pInfo)
{
	CutsceneEditorState *pState = pInfo->pState;
	CutsceneSoundPoint *pPoint = CSE_GetSelected(pInfo->ppSelected);
	bool bActive;
	if(!pPoint)
		return;

	ui_TextEntrySetText(pState->pUISoundVariable, pPoint->pcSoundVariable); 
	ui_ComboBoxSetSelectedsAsString(pState->pUIComboSound, pPoint->pSoundPath);

	CSE_SetTextFromFloat(pState->pUITextSoundLength, pPoint->common.length);
	ui_SetActive(UI_WIDGET(pState->pUITextSoundLength), !pPoint->common.fixedLength);

	ui_TextEntrySetText(pState->pUITextSndCutEntName, pPoint->pchCutsceneEntName);
	bActive = (!pPoint->pchCutsceneEntName || !pPoint->pchCutsceneEntName[0]);

	ui_CheckButtonSetState(pState->pUICheckSoundCamPos, pPoint->bUseCamPos);
	ui_SetActive(UI_WIDGET(pState->pUICheckSoundCamPos), bActive);
	if(pPoint->bUseCamPos)
		bActive = false;

	CSE_SetTextFromFloat(pState->pUISoundXPos, pPoint->vPosition[0]);
	CSE_SetTextFromFloat(pState->pUISoundYPos, pPoint->vPosition[1]);
	CSE_SetTextFromFloat(pState->pUISoundZPos, pPoint->vPosition[2]);
	ui_SetActive(UI_WIDGET(pState->pUISoundXPos), bActive);
	ui_SetActive(UI_WIDGET(pState->pUISoundYPos), bActive);
	ui_SetActive(UI_WIDGET(pState->pUISoundZPos), bActive);
}

static void cutEdRefreshSubtitleUI(CutsceneGenericTrackInfo *pInfo)
{
	CutsceneEditorState *pState = pInfo->pState;
	CutsceneSubtitlePoint *pPoint = CSE_GetSelected(pInfo->ppSelected);
	if(!pPoint)
		return;

	ui_TextEntrySetText(pState->pUISubtitleVariable, pPoint->pcSubtitleVariable);
	ui_MessageEntrySetMessage(pState->pUISubtitleMessage, pPoint->displaySubtitle.pEditorCopy);

	CSE_SetTextFromFloat(pState->pUISubtitleLength, pPoint->common.length);
	ui_TextEntrySetText(pState->pUISubtitleCutEntName, pPoint->pchCutsceneEntName);
}

static void cutEdRefreshUIGenUI(CutsceneGenericTrackInfo *pInfo)
{
	CutsceneEditorState *pState = pInfo->pState;
	CutsceneUIGenPoint *pPoint = CSE_GetSelected(pInfo->ppSelected);
	if(!pPoint)
		return;

	CSE_SetTextFromFloat(pState->pUIUIGenTime, pPoint->common.time);
	ui_ComboBoxSetSelectedEnum(pState->pUIUIGenActionType, pPoint->actionType);
	ui_TextEntrySetText(pState->pUIUIGenMessage, pPoint->pcMessage);
	ui_TextEntrySetText(pState->pUIUIGenVariable, pPoint->pcVariable);
	ui_TextEntrySetText(pState->pUIUIGenStringValue, pPoint->pcStringValue);
	ui_MessageEntrySetMessage(pState->pUIUIGenMessageValue, pPoint->messageValue.pEditorCopy);
	ui_TextEntrySetText(pState->pUIUIGenMessageValueVariable, pPoint->pcMessageValueVariable);
	CSE_SetTextFromFloat(pState->pUIUIGenFloatValue, pPoint->fFloatValue);
}

static void cutEdInitDOFPoint(CutsceneGenericTrackInfo *pInfo, CutsceneDOFPoint *pNewPoint)
{
	pNewPoint->bDOFIsOn = true;
}

static void cutEdInitFOVPoint(CutsceneGenericTrackInfo *pInfo, CutsceneFOVPoint *pNewPoint)
{
	pNewPoint->fFOV = gfxGetDefaultFOV();
}

static void cutEdInitShakePoint(CutsceneGenericTrackInfo *pInfo, CutsceneShakePoint *pNewPoint)
{
	pNewPoint->common.length = 0.125f;
	pNewPoint->fMagnitude = 0.2f;
}

static void cutEdInitEntityPoint(CutsceneGenericTrackInfo *pInfo, CutsceneEntityPoint *pNewPoint)
{
	pNewPoint->actionType = CutsceneEntAction_Waypoint;
}

static bool cutEdEntityPointIsValidReferencePoint(CutsceneGenericTrackInfo *pInfo, CutsceneEntityPoint *pNewPoint)
{
	return pNewPoint->actionType != CutsceneEntAction_Animation && pNewPoint->actionType != CutsceneEntAction_AddStance;
}

static void cutEdInitSoundPoint(CutsceneGenericTrackInfo *pInfo, CutsceneSoundPoint *pNewPoint)
{
	pNewPoint->bUseCamPos = true;
}

static bool cutEdSetObjectMatrix(CutsceneGenericTrackInfo *pInfo, CutsceneObjectPoint *pPoint, Mat4 mMatrix)
{
	getMat3YPR(mMatrix, pPoint->vRotation);
	copyVec3(mMatrix[3], pPoint->vPosition);
	return true;
}

static bool cutEdGetObjectMatrix(CutsceneGenericTrackInfo *pInfo, CutsceneObjectPoint *pPoint, Mat4 mMatrix)
{
	createMat3YPR(mMatrix, pPoint->vRotation);
	copyVec3(pPoint->vPosition, mMatrix[3]);
	return true;
}

static bool cutEdGetObjectBounds(CutsceneEditorState *pState, CutsceneObjectList *pList, CutsceneObjectPoint *pPoint, Vec3 pointPos, Vec3 minBounds, Vec3 maxBounds)
{
	GroupDef *pEntityDef = objectLibraryGetGroupDefByName(pList->pcObjectName, false);
	groupPostLoad(pEntityDef);

	copyVec3(pPoint->vPosition, pointPos);

	copyVec3(pEntityDef->bounds.min, minBounds);
	copyVec3(pEntityDef->bounds.max, maxBounds);

	addVec3(minBounds, pointPos, minBounds);
	addVec3(maxBounds, pointPos, maxBounds);

	return true;
}

static bool cutEdSetEntityMatrix(CutsceneGenericTrackInfo *pInfo, CutsceneEntityPoint *pPoint, Mat4 mMatrix)
{
	if(pPoint->actionType == CutsceneEntAction_Animation)
		return false;
	if(pPoint->actionType == CutsceneEntAction_PlayFx)
		return false;
	if(pPoint->actionType == CutsceneEntAction_AddStance)
		return false;
	getMat3YPR(mMatrix, pPoint->vRotation);
	pPoint->vRotation[0] = pPoint->vRotation[2] = 0;
	createMat3YPR(mMatrix, pPoint->vRotation);
	copyVec3(mMatrix[3], pPoint->vPosition);
	return true;
}

static bool cutEdGetEntityMatrix(CutsceneGenericTrackInfo *pInfo, CutsceneEntityPoint *pPoint, Mat4 mMatrix)
{
	if(pPoint->actionType == CutsceneEntAction_Animation)
		return false;
	if(pPoint->actionType == CutsceneEntAction_PlayFx)
		return false;
	if(pPoint->actionType == CutsceneEntAction_AddStance)
		return false;
	createMat3YPR(mMatrix, pPoint->vRotation);
	copyVec3(pPoint->vPosition, mMatrix[3]);
	return true;
}

static bool cutEdGetEntityBounds(CutsceneEditorState *pState, CutsceneObjectList *pList, CutsceneEntityPoint *pPoint, Vec3 pointPos, Vec3 minBounds, Vec3 maxBounds)
{
	GroupDef *pEntityDef = objectLibraryGetGroupDefByName("core_icons_humanoid", false);
	groupPostLoad(pEntityDef);

	if(pPoint->actionType == CutsceneEntAction_Animation)
		return false;
	if(pPoint->actionType == CutsceneEntAction_PlayFx)
		return false;
	if(pPoint->actionType == CutsceneEntAction_AddStance)
		return false;

	copyVec3(pPoint->vPosition, pointPos);

	copyVec3(pEntityDef->bounds.min, minBounds);
	copyVec3(pEntityDef->bounds.max, maxBounds);

	addVec3(minBounds, pointPos, minBounds);
	addVec3(maxBounds, pointPos, maxBounds);

	return true;
}

static bool cutEdSetFXMatrix(CutsceneGenericTrackInfo *pInfo, CutsceneFXPoint *pPoint, Mat4 mMatrix)
{
	copyMat3(unitmat, mMatrix);
	copyVec3(mMatrix[3], pPoint->vPosition);
	return true;
}

static bool cutEdGetFXMatrix(CutsceneGenericTrackInfo *pInfo, CutsceneFXPoint *pPoint, Mat4 mMatrix)
{
	copyMat3(unitmat, mMatrix);
	copyVec3(pPoint->vPosition, mMatrix[3]);
	return true;
}

static bool cutEdGetFXBounds(CutsceneEditorState *pState, CutsceneFXList *pList, CutsceneFXPoint *pPoint, Vec3 pointPos, Vec3 minBounds, Vec3 maxBounds)
{
	const F32 half_size[3] = { 4, 4, 4 };

	copyVec3(pPoint->vPosition, pointPos);
	subVec3(pointPos, half_size, minBounds);
	addVec3(pointPos, half_size, maxBounds);

	return true;
}

static bool cutEdSetSoundMatrix(CutsceneGenericTrackInfo *pInfo, CutsceneSoundPoint *pPoint, Mat4 mMatrix)
{
	if(pPoint->bUseCamPos)
		return false;
	copyMat3(unitmat, mMatrix);
	copyVec3(mMatrix[3], pPoint->vPosition);
	return true;
}

static bool cutEdGetSoundMatrix(CutsceneGenericTrackInfo *pInfo, CutsceneSoundPoint *pPoint, Mat4 mMatrix)
{
	if(pPoint->bUseCamPos)
		return false;
	copyMat3(unitmat, mMatrix);
	copyVec3(pPoint->vPosition, mMatrix[3]);
	return true;
}

static bool cutEdGetSoundBounds(CutsceneEditorState *pState, CutsceneSoundList *pList, CutsceneSoundPoint *pPoint, Vec3 pointPos, Vec3 minBounds, Vec3 maxBounds)
{
	const F32 half_size[3] = { 4, 4, 4 };

	if(pPoint->bUseCamPos)
		return false;

	copyVec3(pPoint->vPosition, pointPos);
	subVec3(pointPos, half_size, minBounds);
	addVec3(pointPos, half_size, maxBounds);

	return true;
}

typedef struct CSNewPathWindowUI 
{
	UIWindow *pWindow;
	UIComboBox *pComboType;
	CutsceneEditorState *pState;
	bool bOpen;
} CSNewPathWindowUI;

static bool cutEdNewPathWindowCancel(UIButton *pButton, CSNewPathWindowUI *pUI)
{
	elUIWindowClose(NULL, pUI->pWindow);
	pUI->bOpen = false;
	return true;
}

static void cutEdNewPathWindowOk(UIButton *pButton, CSNewPathWindowUI *pUI)
{
	CutscenePath *pPath = StructCreate(parse_CutscenePath);
	pPath->pCamPosSpline = StructCreate(parse_Spline);
	pPath->type = ui_ComboBoxGetSelectedEnum(pUI->pComboType);
	eaPush(&pUI->pState->pCutsceneDef->pPathList->ppPaths, pPath);
	pUI->pState->pSelectedPath = pPath;
	cutEdSetSelectedPoint(pUI->pState, -1);
	if(cutEdIsCirclePath(pPath))
	{
		cutEdAddPointCB(NULL, pUI->pState);
		assert(eaSize(&pPath->ppPositions) == 2 && eaSize(&pPath->ppTargets) == 2);
	}
	else if(pPath->type == CutscenePathType_ShadowEntity)
	{
		pPath->smoothPositions = true;
		pPath->smoothTargets = true;
		cutEdAddPointCB(NULL, pUI->pState);
		assert(eaSize(&pPath->ppPositions) == 1 && eaSize(&pPath->ppTargets) == 1);
	}
	else if(pPath->type == CutscenePathType_WatchEntity)
	{
		pPath->smoothPositions = true;
		cutEdAddPointCB(NULL, pUI->pState);
		assert(eaSize(&pPath->ppPositions) == 0 && eaSize(&pPath->ppTargets) == 1);
	}
	else
	{
		pPath->smoothPositions = true;
		pPath->smoothTargets = true;
		cutEdRefreshUICommon(pUI->pState);
		cutEdSetUnsaved(pUI->pState);
	}
	elUIWindowClose(NULL, pUI->pWindow);
	pUI->bOpen = false;
}

static void cutEdNewPathWindow(CutsceneEditorState *pState)
{
	static CSNewPathWindowUI ui = {0};
	UIWindow *pWin;
	UIButton *pButton;
	UIComboBox *pCombo;
	int y = 5;

	if(ui.bOpen)
		return;

	if(!cutEdIsEditable(pState))
		return;

	ui.bOpen = true;

	pWin = ui_WindowCreate("New Path", 100, 100, 225, 90);
	ui.pWindow = pWin;
	ui.pState = pState;

	pCombo = ui_ComboBoxCreateWithEnum(0, y, 1, CutscenePathTypeEnum, NULL, NULL);
	pCombo->widget.leftPad = pCombo->widget.rightPad = 5;
	pCombo->widget.widthUnit = UIUnitPercentage;
	ui.pComboType = pCombo;
	ui_ComboBoxSetSelectedEnum(pCombo, 0);
	ui_WindowAddChild(pWin, pCombo);
	y = ui_WidgetGetNextY(UI_WIDGET(pCombo));

	pButton = elUIAddCancelOkButtons(pWin, cutEdNewPathWindowCancel, &ui, cutEdNewPathWindowOk, &ui);
	ui_WindowSetCloseCallback(pWin, cutEdNewPathWindowCancel, &ui);
	pWin->widget.height = y + elUINextY(pButton) + 5;
	ui_WidgetSkin(UI_WIDGET(pWin), pState->pSkinWindow);
	elUICenterWindow(pWin);
	ui_WindowSetModal(pWin, true);
	ui_WindowShow(pWin);
}

static void cutEdPostMoveFixupCircle(CutsceneDef *pPrevDef, CutsceneDef *pNewDef, CutscenePath *pSelectedPath, int selectedPoint)
{
	CutscenePathPoint *pNewPos0, *pNewPos1, *pNewLook0, *pNewLook1;

	//Get all the points we will need
	pNewPos0 = cutEdGetPointFromTypeAndIdx(pSelectedPath, CutsceneEditType_CameraPath, 0);
	pNewLook0 = cutEdGetPointFromTypeAndIdx(pSelectedPath, CutsceneEditType_LookAtPath, 0);
	pNewPos1 = cutEdGetPointFromTypeAndIdx(pSelectedPath, CutsceneEditType_CameraPath, 1);
	pNewLook1 = cutEdGetPointFromTypeAndIdx(pSelectedPath, CutsceneEditType_LookAtPath, 1);

	//If second point moved
	if(selectedPoint == 1)
	{
		SWAPVEC3(pNewPos0->pos, pNewPos1->pos);
		SWAPVEC3(pNewLook0->pos, pNewLook1->pos);
		pSelectedPath->angle *= -1;
		cutEdFixupCirclePos(pSelectedPath);
		SWAPVEC3(pNewPos0->pos, pNewPos1->pos);
		SWAPVEC3(pNewLook0->pos, pNewLook1->pos);
		pSelectedPath->angle *= -1;
	}

	//Fixup placements and tangents
	cutEdFixupCirclePos(pSelectedPath);

	//TODO fixup times were here
}

static void cutEdPostMoveFixup(CutsceneEditorState *pState)
{
	CutsceneDef *pPrevDef = pState->pNextUndoCutsceneDef;
	CutsceneDef *pNewDef = pState->pCutsceneDef;
	CutscenePath *pPath = pState->pSelectedPath;

	if(!pPath)
		return;

	if(pState->editType == CutsceneEditType_Camera)
	{
		//Fixup tangents that are on the same path
		cutEdFixupTangent(pPath->ppPositions, pState->selectedPoint-1);
		cutEdFixupTangent(pPath->ppPositions, pState->selectedPoint);
		cutEdFixupTangent(pPath->ppPositions, pState->selectedPoint+1);
		cutEdFixupTangent(pPath->ppTargets, pState->selectedPoint-1);
		cutEdFixupTangent(pPath->ppTargets, pState->selectedPoint);
		cutEdFixupTangent(pPath->ppTargets, pState->selectedPoint+1);
		cutEdPathReloadSplines(pPath);
	}

	//TODO fixup times were here

	if(cutEdIsCirclePath(pPath))
		cutEdPostMoveFixupCircle(pPrevDef, pNewDef, pPath, pState->selectedPoint);

	if(cutEdCameraModeAllowed(pState->pSelectedPath))
	{
		CutscenePathPoint *pCamPoint, *pLookAtPoint;
		pCamPoint = cutEdGetPointFromTypeAndIdx(pPath, CutsceneEditType_CameraPath, pState->selectedPoint);
		pLookAtPoint = cutEdGetPointFromTypeAndIdx(pPath, CutsceneEditType_LookAtPath, pState->selectedPoint);
		pLookAtPoint->common.time = pCamPoint->common.time;
		pCamPoint = cutEdGetNextPoint(pNewDef, CutsceneEditType_CameraPath, pPath, pState->selectedPoint, NULL);
		if(pCamPoint)
		{
			pLookAtPoint = cutEdGetNextPoint(pNewDef, CutsceneEditType_LookAtPath, pPath, pState->selectedPoint, NULL);
			assert(pLookAtPoint);
			pLookAtPoint->common.time = pCamPoint->common.time;
		}
	}
}

static bool cutEdPositionsHaveChanged(CutsceneDef *pPrevDef, CutsceneDef *pNewDef, CutscenePath *pSelectedPath, CutsceneEditType type, int selectedPoint)
{
	int idx;
	CutscenePathPoint *pPrevPoint, *pNewPoint;
	CutscenePath *pPrevPath;

	idx = eaFind(&pNewDef->pPathList->ppPaths, pSelectedPath);
	assert(idx >= 0 && idx < eaSize(&pPrevDef->pPathList->ppPaths));
	pPrevPath = pPrevDef->pPathList->ppPaths[idx];
	pPrevPoint = cutEdGetPointFromTypeAndIdx(pPrevPath, type, selectedPoint);
	pNewPoint = cutEdGetPointFromTypeAndIdx(pSelectedPath, type, selectedPoint);
	assert(pNewPoint && pPrevPoint);

	return (!nearSameVec3(pPrevPoint->pos, pNewPoint->pos) || !nearSameVec3(pPrevPoint->up, pNewPoint->up) || !nearSameVec3(pPrevPoint->tangent, pNewPoint->tangent));
}

static void cutEdUpdatePointMatrix(CutscenePathPoint **ppPoints, Spline *pSpline, CutsceneEditorState *pState, Mat4 transMat)
{
	if(pState->selectedPoint < eaSize(&ppPoints) && pState->selectedPoint >= 0)
	{
		CutscenePathPoint *pPoint = ppPoints[pState->selectedPoint];
		copyVec3(transMat[1], pPoint->up);
		copyVec3(transMat[2], pPoint->tangent);
		copyVec3(transMat[3], pPoint->pos);
		if(pSpline)
			cutEdUpdateSplineFromPoint(pSpline, pPoint, pState->selectedPoint);
	}
}

static void cutEdUpdateCameraMatrix(CutscenePath *pPath, CutsceneEditorState *pState, Mat4 transMat)
{
	if(	pState->selectedPoint < eaSize(&pPath->ppPositions) &&
		pState->selectedPoint < eaSize(&pPath->ppTargets) &&
		pState->selectedPoint >= 0)
	{
		F32 prevDist;
		CutscenePathPoint *pCamPoint = pPath->ppPositions[pState->selectedPoint];
		CutscenePathPoint *pLookPoint = pPath->ppTargets[pState->selectedPoint];

		prevDist = distance3(pCamPoint->pos, pLookPoint->pos);
		copyVec3(transMat[3], pCamPoint->pos);
		copyVec3(transMat[2], pLookPoint->pos);
		scaleVec3(pLookPoint->pos, prevDist, pLookPoint->pos);
		addVec3(pLookPoint->pos, pCamPoint->pos, pLookPoint->pos);

		if(pPath->pCamPosSpline)
			cutEdUpdateSplineFromPoint(pPath->pCamPosSpline, pCamPoint, pState->selectedPoint);
		if(pPath->pCamTargetSpline)
			cutEdUpdateSplineFromPoint(pPath->pCamTargetSpline, pLookPoint, pState->selectedPoint);
	}
}

static void cutEdUpdateFromMatrix(CutsceneEditorState *pState, Mat4 transMat)
{
	CutscenePath *pPath = pState->pSelectedPath;

	if(!pPath)
		return;

	switch(pState->editType)
	{
	case CutsceneEditType_Camera:
		cutEdUpdateCameraMatrix(pPath, pState, transMat);
		break;
	case CutsceneEditType_CameraPath:
		cutEdUpdatePointMatrix(pPath->ppPositions, pPath->pCamPosSpline, pState, transMat);
		break;
	case CutsceneEditType_LookAtPath:
		cutEdUpdatePointMatrix(pPath->ppTargets, pPath->pCamTargetSpline, pState, transMat);
		break;
	}
}

static void cutEdTimelineTimeChangedCB(UITimeline *timeline, int time_in, CutsceneEditorState *pState)
{
	GfxCameraController *pCamera = gfxGetActiveCameraController();
	GfxCameraView *pCameraView = gfxGetActiveCameraView();
	CutsceneDef *pCutsceneDef = pState->pCutsceneDef;
	F32 time = CSE_TrackTimeToCut(time_in);

	if(pState->bDetachedCamera) {
		gclGetCutsceneCameraPathListPosPyr(pCutsceneDef, time, pState->cameraPos, pState->cameraPyr, NULL, !pState->bPlaying, pState->playingTimestep);
	} else if(gclGetCutsceneCameraPathListPosPyr(pCutsceneDef, time, pCamera->camcenter, pCamera->campyr, NULL, !pState->bPlaying, pState->playingTimestep)) {
		Mat4 camera_matrix;

		// Create a camera matrix from camcenter and campyr
		createMat3YPR(camera_matrix, pCamera->campyr);
		copyVec3(pCamera->camcenter, camera_matrix[3]);

		frustumSetCameraMatrix(&pCameraView->new_frustum, camera_matrix);
	}
}

static void cutEdPlayCB(UIButton *pButton, CutsceneEditorState *pState)
{
	if(!demo_playingBack())
		ui_AddActiveFamilies(UI_FAMILY_CUTSCENE); // must be at the top to make sure "Cutscene_Root" UIGen gets ticked once before sending of messages.

	ui_TimelineSetTimeAndCallback(pState->pTimeline, ui_TimelineGetTime(pState->pTimeline)-0.001f);
	pState->bPlaying = true;
	pState->playingTimestep = -1;	
}

static void cutEdPauseCB(UIButton *pButton, CutsceneEditorState *pState)
{
	pState->bPlaying = false;
}

static void cutEdCutsceneFinishedCB(CutsceneEditorState *pState)//If we ever decide to use this info we will need to make sure it is not freed while playing
{
	demo_record_stop(NULL);
	CommandEditMode(1);
	gclCutsceneSetFinishedCB(NULL, NULL);
}

static void cutEdRecordCB(UIButton *pButton, CutsceneEditorState *pState)
{
	DemoRecording *pDemo;
	const char *record_name;
	if(demo_playingBack() || isProductionMode())
		return;
	if(cutEdIsUnsaved(pState))
	{
		Alertf("You must save your cut scene before recording.");
		return;
	}
	cutEdStop(pState);
	record_name = ui_TextEntryGetText(pState->pUITextRecordFileName);
	CommandEditMode(0);
	demo_record_cutscene(NULL, record_name, pState->pCutsceneDef->name);
	pDemo = demo_GetInfo(NULL);
	if(pDemo)
	{
		if(pDemo->cutsceneDef)
			StructDestroy(parse_CutsceneDef, pDemo->cutsceneDef);
		pDemo->cutsceneDef = StructClone(parse_CutsceneDef, pState->pCutsceneDef);
	}
	gclCutsceneSetFinishedCB(cutEdCutsceneFinishedCB, pState);
}

static void cutEdToggleStateCB(UICheckButton *pCheckButton, bool *pValue)
{
	*pValue = ui_CheckButtonGetState(pCheckButton);
}

static void cutEdAddPathCB(UIButton *pButton, CutsceneEditorState *pState)
{
	cutEdNewPathWindow(pState);
}

static void cutEdDeletePathCB(UIButton *pButton, CutsceneEditorState *pState)
{
	int selected = ui_ListGetSelectedRow(pState->pUIListPaths);
	CutsceneDef *pCutsceneDef = pState->pCutsceneDef;

	if(!cutEdIsEditable(pState))
		return;

	if(selected < eaSize(&pCutsceneDef->pPathList->ppPaths) && selected >= 0)
	{
		CutscenePath *pPath = eaRemove(&pCutsceneDef->pPathList->ppPaths, selected);
		StructDestroy(parse_CutscenePath, pPath);
		pState->pSelectedPath = NULL;
		cutEdSetSelectedPoint(pState, -1);
	}
	
	cutEdRefreshUICommon(pState);
	cutEdSetUnsaved(pState);
}

static void cutEdSaveDemoCB(UIButton *pButton, CutsceneEditorState *pState)
{
	char *pcDemoName;
	DemoRecording *pDemo;
	pDemo = demo_GetInfo(&pcDemoName);
	assert(pDemo);
	if(pDemo->cutsceneDef)
		StructDestroy(parse_CutsceneDef, pDemo->cutsceneDef);
	pDemo->cutsceneDef = StructClone(parse_CutsceneDef, pState->pCutsceneDef);
	if(ParserWriteTextFile(pcDemoName, parse_DemoRecording, pDemo, 0, 0))
	{
		pState->bUnsaved = 0;
		ui_ButtonSetText(pButton, "Save");

		// Clear the undo stack on revert
		EditUndoStackClear(pState->edit_undo_stack);
		StructDestroy(parse_CutsceneDef, pState->pNextUndoCutsceneDef);
		pState->pNextUndoCutsceneDef = StructClone(parse_CutsceneDef, pState->pCutsceneDef);
	}
}

static void cutEdMapNameCB(UITextEntry *pTextEntry, CutsceneEditorState *pState)
{
	CutsceneDef *pCutsceneDef = pState->pCutsceneDef;
	const unsigned char *pucNewMapName = ui_TextEntryGetText(pTextEntry);

	if(strcmp_safe(pCutsceneDef->pcMapName, pucNewMapName) == 0)
		return;

	if(!cutEdIsEditable(pState))
		return;

	if(pCutsceneDef->pcMapName)
		StructFreeString(pCutsceneDef->pcMapName);
	pCutsceneDef->pcMapName = StructAllocString(ui_TextEntryGetText(pTextEntry));
	cutEdRefreshUICommon(pState);
	cutEdSetUnsaved(pState);
}

static void cutEdPathListChangedCB(UIList *pList, CutsceneEditorState *pState)
{
	int selected = ui_ListGetSelectedRow(pList);
	CutsceneDef *pCutsceneDef = pState->pCutsceneDef;

	if(selected < eaSize(&pCutsceneDef->pPathList->ppPaths) && selected >= 0)
		pState->pSelectedPath = pCutsceneDef->pPathList->ppPaths[selected];

	cutEdSetSelectedPoint(pState, -1);
	cutEdRefreshUICommon(pState);
}

static int cutEdSortGenPnts(const CutsceneCommonPointData **left, const CutsceneCommonPointData **right)
{
	return ((*left)->time > (*right)->time) ? 1 : -1;
}

static void cutEdCGTSwap(CutsceneDummyTrack *pCGT, F32 startTime1, F32 endTime1, F32 offset1, F32 startTime2, F32 endTime2, F32 offset2)
{
	int i;
	for ( i=0; i < eaSize(&pCGT->ppGenPnts); i++ ) {
		CutsceneCommonPointData *pGenPnt = pCGT->ppGenPnts[i];
		if(pGenPnt->time > startTime1 && pGenPnt->time <= endTime1)
			pGenPnt->time += offset1;
		else if(pGenPnt->time > startTime2 && pGenPnt->time <= endTime2)
			pGenPnt->time += offset2;
	}
	eaQSort(pCGT->ppGenPnts, cutEdSortGenPnts);
}

static void cutEdPathOffset(CutscenePath *pPath, F32 offset)
{
	int i;
	for ( i=0; i < eaSize(&pPath->ppPositions); i++ ) {
		pPath->ppPositions[i]->common.time += offset;
	}
	for ( i=0; i < eaSize(&pPath->ppTargets); i++ ) {
		pPath->ppTargets[i]->common.time += offset;
	}
}

static void cutEdSwapPaths(CutsceneEditorState *pState, int idx1, int idx2)
{
	int i, j;
	CutsceneDef *pCutsceneDef = pState->pCutsceneDef;
	CutscenePath *pPath1 = pCutsceneDef->pPathList->ppPaths[idx1];
	CutscenePath *pPath2 = pCutsceneDef->pPathList->ppPaths[idx2];
	F32 startTime1;
	F32 endTime1;
	F32 startTime2;
	F32 endTime2;
	F32 offset1;
	F32 offset2;

	if(!cutEdIsEditable(pState))
		return;

	startTime1 = cutscene_GetPathStartTime(pCutsceneDef->pPathList, pPath1);
	endTime1 = cutscene_GetPathEndTime(pCutsceneDef->pPathList, pPath1);
	startTime2 = cutscene_GetPathStartTime(pCutsceneDef->pPathList, pPath2);
	endTime2 = cutscene_GetPathEndTime(pCutsceneDef->pPathList, pPath2);
	offset1 = endTime2-startTime2;
	offset2 = startTime1-startTime2;

	cutEdPathOffset(pPath1, offset1);
	cutEdPathOffset(pPath2, offset2);
	eaSwap(&pCutsceneDef->pPathList->ppPaths, idx1, idx2);

	for ( i=0; i < eaSize(&pState->ppTrackInfos); i++ ) {
		CutsceneGenericTrackInfo *pInfo = pState->ppTrackInfos[i];
		if(pInfo->bMulti) {
			CutsceneDummyTrack ***pppCGTs = (CutsceneDummyTrack***)TokenStoreGetEArray(parse_CutsceneDef, pInfo->iCGTColumn, pState->pCutsceneDef, NULL);
			for ( j=0; j < eaSize(pppCGTs); j++ ) {
				CutsceneDummyTrack *pCGT = (*pppCGTs)[j];
				cutEdCGTSwap(pCGT, startTime1, endTime1, offset1, startTime2, endTime2, offset2);
			}
		} else {
			CutsceneDummyTrack *pCGT = TokenStoreGetPointer(parse_CutsceneDef, pInfo->iCGTColumn, pState->pCutsceneDef, 0, NULL);
			if(pCGT) {
				cutEdCGTSwap(pCGT, startTime1, endTime1, offset1, startTime2, endTime2, offset2);
			}
		}
	}	

	cutEdSetSelectedPoint(pState, -1);
	cutEdRefreshUICommon(pState);
	cutEdSetUnsaved(pState);
}

static void cutEdPathListMoveUp(void *unused, CutsceneEditorState *pState)
{
	int selected = ui_ListGetSelectedRow(pState->pUIListPaths);
	CutsceneDef *pCutsceneDef = pState->pCutsceneDef;

	if(selected < eaSize(&pCutsceneDef->pPathList->ppPaths) && selected > 0) {
		cutEdSwapPaths(pState, selected-1, selected);
	}
}

static void cutEdPathListMoveDown(void *unused, CutsceneEditorState *pState)
{
	int selected = ui_ListGetSelectedRow(pState->pUIListPaths);
	CutsceneDef *pCutsceneDef = pState->pCutsceneDef;

	if(selected < eaSize(&pCutsceneDef->pPathList->ppPaths)-1 && selected >= 0) {
		cutEdSwapPaths(pState, selected, selected+1);
	}
}

static void cutEdPathListRClickCB(UIList *pList, CutsceneEditorState *pState)
{
	int selected = ui_ListGetSelectedRow(pState->pUIListPaths);
	CutsceneDef *pCutsceneDef = pState->pCutsceneDef;

	if(eaSize(&pCutsceneDef->pPathList->ppPaths) < 2 || selected < 0)
		return;

	if(pState->pUIMenuPathRClick)
		ui_WidgetQueueFree(UI_WIDGET(pState->pUIMenuPathRClick));

	pState->pUIMenuPathRClick = ui_MenuCreate("");
	if(selected > 0) {
		ui_MenuAppendItems(pState->pUIMenuPathRClick,
			ui_MenuItemCreate("Move Up", UIMenuCallback, cutEdPathListMoveUp, pState, NULL),
			NULL);
	}
	if(selected < eaSize(&pCutsceneDef->pPathList->ppPaths)-1) {
		ui_MenuAppendItems(pState->pUIMenuPathRClick,
			ui_MenuItemCreate("Move Down", UIMenuCallback, cutEdPathListMoveDown, pState, NULL),
			NULL);
	}

	ui_MenuPopupAtCursor(pState->pUIMenuPathRClick);		
}

static CutscenePathPoint* cutEdPointCopyBasicData(CutscenePathPoint *pDestPoint, const CutscenePathPoint *pSrcPoint)
{
	pDestPoint->common.time = pSrcPoint->common.time;
	pDestPoint->common.length = pSrcPoint->common.length;
	pDestPoint->easeIn = pSrcPoint->easeIn;
	pDestPoint->easeOut = pSrcPoint->easeOut;
	return pDestPoint;
}

static void cutEdSetTargetCB(UIButton *pButton, CutsceneEditorState *pState)
{
	ui_SetFocus(NULL);
	pState->bPickingEnt = true;
	ui_TextEntrySetText(pState->pUITextWatchTarget, "Selecting");
}

static void cutEdDeletePointCB(void *unused, CutsceneEditorState *pState)
{
	cutEdDeletePointInternal(pState);
}

static void cutEdAngleCB(UITextEntry *pTextEntry, CutsceneEditorState *pState)
{
	F32 val;

	if(!cutEdIsEditable(pState))
		return;

	val = atof(ui_TextEntryGetText(pTextEntry));

	pState->pSelectedPath->angle = -1.0f*RAD(val);
	cutEdPostMoveFixupCircle(pState->pNextUndoCutsceneDef, pState->pCutsceneDef, pState->pSelectedPath, -1);
	cutEdRefreshUICommon(pState);
	cutEdSetUnsaved(pState);
}

static void cutEdPathRelPathChanged(void *unused, CutsceneEditorState *pState)
{
	CutscenePath *pPath = pState->pSelectedPath;

	if(!cutEdIsEditable(pState))
		return;

	if(!pPath)
		return;

	pPath->common.main_offset.offsetType = ui_ComboBoxGetSelectedEnum(pState->pUIComboWatchType);

	StructFreeStringSafe(&pPath->common.main_offset.pchCutsceneEntName);
	pPath->common.main_offset.pchCutsceneEntName = StructAllocString(ui_TextEntryGetText(pState->pUITextWatchCutsceneEntName));

	StructFreeStringSafe(&pPath->common.main_offset.pchStaticEncName);
	pPath->common.main_offset.pchStaticEncName = StructAllocString(ui_TextEntryGetText(pState->pUITextWatchEncounterName));

	StructFreeStringSafe(&pPath->common.main_offset.pchActorName);
	pPath->common.main_offset.pchActorName = StructAllocString(ui_TextEntryGetText(pState->pUITextWatchActorName));

	StructFreeStringSafe(&pPath->common.main_offset.pchBoneName);
	pPath->common.main_offset.pchBoneName = StructAllocString(ui_TextEntryGetText(pState->pUITextWatchBoneName));

	pPath->common.bTwoRelativePos = ui_CheckButtonGetState(pState->pUICheckPathTwoRelPos);
	pPath->common.second_offset.offsetType = ui_ComboBoxGetSelectedEnum(pState->pUIComboWatchType2);

	StructFreeStringSafe(&pPath->common.second_offset.pchStaticEncName);
	pPath->common.second_offset.pchCutsceneEntName = StructAllocString(ui_TextEntryGetText(pState->pUITextWatchCutsceneEntName2));

	StructFreeStringSafe(&pPath->common.second_offset.pchStaticEncName);
	pPath->common.second_offset.pchStaticEncName = StructAllocString(ui_TextEntryGetText(pState->pUITextWatchEncounterName2));

	StructFreeStringSafe(&pPath->common.second_offset.pchActorName);
	pPath->common.second_offset.pchActorName = StructAllocString(ui_TextEntryGetText(pState->pUITextWatchActorName2));

	StructFreeStringSafe(&pPath->common.second_offset.pchBoneName);
	pPath->common.second_offset.pchBoneName = StructAllocString(ui_TextEntryGetText(pState->pUITextWatchBoneName2));

	if(pPath->common.main_offset.offsetType != CutsceneOffsetType_CutsceneEntity)
		StructFreeStringSafe(&pPath->common.main_offset.pchCutsceneEntName);
	if(pPath->common.main_offset.offsetType != CutsceneOffsetType_Actor)
		StructFreeStringSafe(&pPath->common.main_offset.pchActorName);
	if(pPath->common.main_offset.offsetType != CutsceneOffsetType_Actor)
		StructFreeStringSafe(&pPath->common.main_offset.pchStaticEncName);

	if(!pPath->common.bTwoRelativePos || pPath->common.second_offset.offsetType != CutsceneOffsetType_CutsceneEntity)
		StructFreeStringSafe(&pPath->common.second_offset.pchCutsceneEntName);
	if(!pPath->common.bTwoRelativePos || pPath->common.second_offset.offsetType != CutsceneOffsetType_Actor)
		StructFreeStringSafe(&pPath->common.second_offset.pchActorName);
	if(!pPath->common.bTwoRelativePos || pPath->common.second_offset.offsetType != CutsceneOffsetType_Actor)
		StructFreeStringSafe(&pPath->common.second_offset.pchStaticEncName);

	cutEdRefreshUICommon(pState);
	cutEdSetUnsaved(pState);
}

static void cutEdPathRelPathChangedWrapper(void *unused, int val, CutsceneEditorState *pState)
{
	cutEdPathRelPathChanged(unused, pState);
}

static void cutEdCGTRelPosChanged(void *unused, CutsceneEditorState *pState)
{
	CutsceneGenericTrackInfo *pInfo = cutEdGetSelectedInfo(pState);
	CutsceneDummyTrack *pCGT = pInfo->ppSelected[0]->pCGT;

	if(!cutEdIsEditable(pState))
		return;

	if(!demo_playingBack()) {
		
		pCGT->common.bRelativePos = ui_CheckButtonGetState(pState->pUICheckRelPosOn); 
		pCGT->common.main_offset.offsetType = ui_ComboBoxGetSelectedEnum(pState->pUIComboRelPosType);

		StructFreeStringSafe(&pCGT->common.main_offset.pchCutsceneEntName);
		pCGT->common.main_offset.pchCutsceneEntName = StructAllocString(ui_TextEntryGetText(pState->pUITextRelPosCutsceneEntName));

		StructFreeStringSafe(&pCGT->common.main_offset.pchStaticEncName);
		pCGT->common.main_offset.pchStaticEncName = StructAllocString(ui_TextEntryGetText(pState->pUITextRelPosEncounterName));

		StructFreeStringSafe(&pCGT->common.main_offset.pchActorName);
		pCGT->common.main_offset.pchActorName = StructAllocString(ui_TextEntryGetText(pState->pUITextRelPosActorName));

		StructFreeStringSafe(&pCGT->common.main_offset.pchBoneName);
		pCGT->common.main_offset.pchBoneName = StructAllocString(ui_TextEntryGetText(pState->pUITextRelPosBoneName));

		pCGT->common.bTwoRelativePos = ui_CheckButtonGetState(pState->pUICheckTwoRelPos); 
		pCGT->common.second_offset.offsetType = ui_ComboBoxGetSelectedEnum(pState->pUIComboRelPosType2);

		StructFreeStringSafe(&pCGT->common.second_offset.pchCutsceneEntName);
		pCGT->common.second_offset.pchCutsceneEntName = StructAllocString(ui_TextEntryGetText(pState->pUITextRelPosCutsceneEntName2));

		StructFreeStringSafe(&pCGT->common.second_offset.pchStaticEncName);
		pCGT->common.second_offset.pchStaticEncName = StructAllocString(ui_TextEntryGetText(pState->pUITextRelPosEncounterName2));

		StructFreeStringSafe(&pCGT->common.second_offset.pchActorName);
		pCGT->common.second_offset.pchActorName = StructAllocString(ui_TextEntryGetText(pState->pUITextRelPosActorName2));

		StructFreeStringSafe(&pCGT->common.second_offset.pchBoneName);
		pCGT->common.second_offset.pchBoneName = StructAllocString(ui_TextEntryGetText(pState->pUITextRelPosBoneName2));


		if(!pCGT->common.bRelativePos || pCGT->common.main_offset.offsetType != CutsceneOffsetType_CutsceneEntity)
			StructFreeStringSafe(&pCGT->common.main_offset.pchCutsceneEntName);
		if(!pCGT->common.bRelativePos || pCGT->common.main_offset.offsetType != CutsceneOffsetType_Actor)
			StructFreeStringSafe(&pCGT->common.main_offset.pchActorName);
		if(!pCGT->common.bRelativePos || pCGT->common.main_offset.offsetType != CutsceneOffsetType_Actor)
			StructFreeStringSafe(&pCGT->common.main_offset.pchStaticEncName);

		if(!pCGT->common.bTwoRelativePos || pCGT->common.second_offset.offsetType != CutsceneOffsetType_CutsceneEntity)
			StructFreeStringSafe(&pCGT->common.second_offset.pchCutsceneEntName);
		if(!pCGT->common.bTwoRelativePos || pCGT->common.second_offset.offsetType != CutsceneOffsetType_Actor)
			StructFreeStringSafe(&pCGT->common.second_offset.pchActorName);
		if(!pCGT->common.bTwoRelativePos || pCGT->common.second_offset.offsetType != CutsceneOffsetType_Actor)
			StructFreeStringSafe(&pCGT->common.second_offset.pchStaticEncName);
	}

	cutEdRefreshUICommon(pState);
	cutEdSetUnsaved(pState);
}

static void cutEdCloneGenPntCB(void *unused, CutsceneEditorState *pState)
{
	int i, idx;
	CutsceneGenericTrackInfo *pInfo = cutEdGetSelectedInfo(pState);
	CutsceneCommonPointData *pGenPnt = pInfo->ppSelected[0]->pGenPnt;
	CutsceneCommonPointData *pNewGenPnt;
	CutsceneDummyTrack *pCGT = pInfo->ppSelected[0]->pCGT;

	if(!cutEdIsEditable(pState))
		return;

	idx = eaFind(&pCGT->ppGenPnts, pGenPnt);
	assert(idx >= 0);

	if(pGenPnt->length) {
		for ( i=idx+1; i < eaSize(&pCGT->ppGenPnts); i++ ) {
			pCGT->ppGenPnts[i]->time += pGenPnt->length;
		}
	}

	pNewGenPnt = StructCloneVoid(pInfo->pGenPntPti, pGenPnt);
	assert(pNewGenPnt);
	pNewGenPnt->time += pGenPnt->length;
	pInfo->ppSelected[0]->pGenPnt = pNewGenPnt;
	eaInsert(&pCGT->ppGenPnts, pNewGenPnt, idx+1);

	cutEdRefreshUICommon(pState);
	cutEdSetUnsaved(pState);
}

static void cutEdGenCGTChanged(void *unused, CutsceneEditorState *pState)
{
	CutsceneGenericTrackInfo *pInfo = cutEdGetSelectedInfo(pState);
	CutsceneDummyTrack *pCGT = (pInfo ? pInfo->ppSelected[0]->pCGT : NULL);
	
	if(!cutEdIsEditable(pState))
		return;

	if(!pCGT)
		return;

	if(pCGT->common.pcName)
		StructFreeString(pCGT->common.pcName);
	pCGT->common.pcName = StructAllocString(ui_TextEntryGetText(pState->pUITextTrackName));

	cutEdRefreshUICommon(pState);
	cutEdSetUnsaved(pState);
}

static void cutEdTweenToGameCameraCB(UITextEntry* pTextEntry, CutsceneEditorState *pState)
{
	F32 time;

	if(!cutEdIsEditable(pState))
		return;

	time = atof(ui_TextEntryGetText(pTextEntry));

	time = CLAMP(time, 0.0f, 10.0f);
	pState->pCutsceneDef->fGameCameraTweenTime = time;
	cutEdRefreshUICommon(pState);
	cutEdSetUnsaved(pState);
}

static void cutEdCamPosPointListChangedCB(UIList *pList, CutsceneEditorState *pState)
{
	int selected = ui_ListGetSelectedRow(pList);

	assert(pState->pSelectedPath);

	if(selected < eaSize(&pState->pSelectedPath->ppPositions) && selected >= 0)
	{
		if(pState->editType == CutsceneEditType_LookAtPath)
			cutEdEditTypeCB(NULL, CutsceneEditType_CameraPath, pState);
		cutEdSetSelectedPoint(pState, selected);
		cutEdRefreshUICommon(pState);
	}
}

static void cutEdLookAtPointListChangedCB(UIList *pList, CutsceneEditorState *pState)
{
	int selected = ui_ListGetSelectedRow(pList);

	assert(pState->pSelectedPath);

	if(selected < eaSize(&pState->pSelectedPath->ppTargets) && selected >= 0)
	{
		if(pState->editType == CutsceneEditType_CameraPath)
			cutEdEditTypeCB(NULL, CutsceneEditType_LookAtPath, pState);
		cutEdSetSelectedPoint(pState, selected);
		cutEdRefreshUICommon(pState);
	}
}

static void cutEdCamPathSmoothCB(UICheckButton *pCheck, CutsceneEditorState *pState)
{
	if(!cutEdIsEditable(pState))
		return;

	if(!pState->pSelectedPath)
		return;
	pState->pSelectedPath->smoothPositions = ui_CheckButtonGetState(pCheck);
	cutEdPathReloadSplines(pState->pSelectedPath);
	cutEdSetUnsaved(pState);
}

static void cutEdLookAtPathSmoothCB(UICheckButton *pCheck, CutsceneEditorState *pState)
{
	if(!cutEdIsEditable(pState))
		return;

	if(!pState->pSelectedPath)
		return;
	pState->pSelectedPath->smoothTargets = ui_CheckButtonGetState(pCheck);
	cutEdPathReloadSplines(pState->pSelectedPath);
	cutEdSetUnsaved(pState);
}

static void cutEdFadeChangedCB(void *unused, CutsceneGenericTrackInfo *pInfo)
{
	CutsceneEditorState *pState = pInfo->pState;
	CutsceneFadePoint *pPoint = CSE_GetSelected(pInfo->ppSelected);

	if(!cutEdIsEditable(pState))
		return;

	if(!pPoint)
		return;

	pPoint->vFadeValue[3] = atof(ui_TextEntryGetText(pState->pUITextFadeAmount))/100.0f;
	pPoint->vFadeValue[3] = CLAMP(pPoint->vFadeValue[3], 0.0f, 1.0f);
	{
		Vec4 temp;
		ui_ColorButtonGetColor(pState->pUIColorFade, temp);
		copyVec3(temp, pPoint->vFadeValue);
	}

	pPoint->bAdditive = ui_CheckButtonGetState(pState->pUIFadeAdditive);

	cutEdRefreshUICommon(pState);
	cutEdSetUnsaved(pState);
}

static void cutEdDOFChangedCB(void *unused, CutsceneGenericTrackInfo *pInfo)
{
	CutsceneEditorState *pState = pInfo->pState;
	CutsceneDOFPoint *pPoint = CSE_GetSelected(pInfo->ppSelected);

	if(!cutEdIsEditable(pState))
		return;

	if(!pPoint)
		return;

	pPoint->bDOFIsOn = ui_CheckButtonGetState(pState->pUICheckPointUseDoF);

	pPoint->fDOFBlur = atof(ui_TextEntryGetText(pState->pUITextPointDoFBlur));
	pPoint->fDOFBlur = CLAMP(pPoint->fDOFBlur, 0.0f, 1.0f);

	pPoint->fDOFDist = atof(ui_TextEntryGetText(pState->pUITextPointDoFDist));
	pPoint->fDOFDist = MAX(pPoint->fDOFDist, 0.0f);

	cutEdRefreshUICommon(pState);
	cutEdSetUnsaved(pState);
}

static void cutEdFOVChangedCB(void *unused, CutsceneGenericTrackInfo *pInfo)
{
	CutsceneEditorState *pState = pInfo->pState;
	CutsceneFOVPoint *pPoint = CSE_GetSelected(pInfo->ppSelected);

	if(!cutEdIsEditable(pState))
		return;

	if(!pPoint)
		return;

	pPoint->fFOV = atof(ui_TextEntryGetText(pState->pUITextFOV));
	pPoint->fFOV = CLAMP(pPoint->fFOV, 1, 178.99);

	cutEdRefreshUICommon(pState);
	cutEdSetUnsaved(pState);
}

static void cutEdShakeChangedCB(void *unused, CutsceneGenericTrackInfo *pInfo)
{
	CutsceneEditorState *pState = pInfo->pState;
	CutsceneShakePoint *pPoint = CSE_GetSelected(pInfo->ppSelected);

	if(!cutEdIsEditable(pState))
		return;

	if(!pPoint)
		return;

	pPoint->common.time = atof(ui_TextEntryGetText(pState->pUIShakeTime));
	pPoint->common.length = atof(ui_TextEntryGetText(pState->pUIShakeLength));
	pPoint->common.length = MAX(pPoint->common.length, 0.0f);
	pPoint->fMagnitude = atof(ui_TextEntryGetText(pState->pUIShakeMagnitude));
	pPoint->fMagnitude = MAX(pPoint->fMagnitude, 0.0f);
	pPoint->fVertical = atof(ui_TextEntryGetText(pState->pUIShakeVertical));
	pPoint->fVertical = CLAMP(pPoint->fVertical, -1.0f, 1.0f);
	pPoint->fPan = atof(ui_TextEntryGetText(pState->pUIShakePan));
	pPoint->fPan = CLAMP(pPoint->fPan, 0.0f, 1.0f);

	cutEdRefreshUICommon(pState);
	cutEdSetUnsaved(pState);	
}

static void cutEdObjectChangedCB(void *unused, CutsceneGenericTrackInfo *pInfo)
{
	CutsceneEditorState *pState = pInfo->pState;
	CutsceneObjectPoint *pPoint = CSE_GetSelected(pInfo->ppSelected);
	CutsceneObjectList *pList = CSE_GetSelectedList(pInfo->ppSelected);

	if(!cutEdIsEditable(pState))
		return;

	if(!pPoint)
		return;

	pPoint->fAlpha = atof(ui_TextEntryGetText(pState->pUIObjectAlpha))/100.0f;
	pPoint->fAlpha = CLAMP(pPoint->fAlpha, 0.0f, 1.0f);

	pPoint->vPosition[0] = atof(ui_TextEntryGetText(pState->pUIObjectXPos));
	pPoint->vPosition[1] = atof(ui_TextEntryGetText(pState->pUIObjectYPos));
	pPoint->vPosition[2] = atof(ui_TextEntryGetText(pState->pUIObjectZPos));
	pPoint->vRotation[0] = atof(ui_TextEntryGetText(pState->pUIObjectPRot));
	pPoint->vRotation[0] = RAD(pPoint->vRotation[0]);
	pPoint->vRotation[1] = atof(ui_TextEntryGetText(pState->pUIObjectYRot));
	pPoint->vRotation[1] = RAD(pPoint->vRotation[1]);
	pPoint->vRotation[2] = atof(ui_TextEntryGetText(pState->pUIObjectRRot));
	pPoint->vRotation[2] = RAD(pPoint->vRotation[2]);

	cutEdRefreshUICommon(pState);
	cutEdSetUnsaved(pState);
}



static void cutEdEntityChangedCB(void *unused, CutsceneGenericTrackInfo *pInfo);

static void cutEdComboBoxChangedEnumWrapperCB(void *unused, int val, CutsceneGenericTrackInfo *pInfo)
{
	cutEdEntityChangedCB(NULL, pInfo);
}

static void cutEdUpdateEntityOverlayCostumes(CutsceneEditorState *pState, CutsceneGenericTrackInfo *pInfo, void *pCont)
{
	UITextEntry *pTextEntry;
	int y = pState ? pState->iOverlayCostumesStartAtY : 0;
	if (pInfo && pState)
	{
		CutsceneEntityPoint *pPoint = CSE_GetSelected(pInfo->ppSelected);
		CutsceneEntityList *pList = CSE_GetSelectedList(pInfo->ppSelected);
		int i;
		//remove unused
		if (pList)
		{
			for (i = eaSize(&pState->eaUIEntityEquipment)-1; i > eaSize(&pList->eaOverrideEquipment); i--)
			{
				UIComboBox* pComboBox = eaGet(&pState->eaUIEntityEquipmentSlots, i);
				pTextEntry = eaGet(&pState->eaUIEntityEquipment, i);
				if (pTextEntry)
				{
					emPanelRemoveChild(pCont, UI_WIDGET(pTextEntry), true);
					ui_WidgetQueueFree(UI_WIDGET(pTextEntry));
					eaRemove(&pState->eaUIEntityEquipment, i);
				}

				if (pComboBox)
				{
					emPanelRemoveChild(pCont, UI_WIDGET(pComboBox), true);
					ui_WidgetQueueFree(UI_WIDGET(pComboBox));
					eaRemove(&pState->eaUIEntityEquipmentSlots, i);
				}
			}
			//update all
			for (i = 0; i < eaSize(&pList->eaOverrideEquipment)+1; i++)
			{
				pTextEntry = eaGet(&pState->eaUIEntityEquipment, i);
				if (!pTextEntry)
				{
					if(!demo_playingBack()) 
						resRequestAllResourcesInDictionary(g_hItemDict);
					pTextEntry = ui_TextEntryCreateWithGlobalDictionaryCombo("", 0, 0, g_hItemDict, "resourceName", /*bAutoComplete=*/true, /*bDrawSelected=*/true, /*bValidate=*/true, /*bFiltered=*/!demo_playingBack());
					
					ui_TextEntrySetChangedCallback(pTextEntry, cutEdEntityChangedCB, pInfo);
					ui_WidgetSetWidthEx(UI_WIDGET(pTextEntry), 0.75f-2*CSE_UI_SIDE_PERCENT, UIUnitPercentage);
					eaPush(&pState->eaUIEntityEquipment, pTextEntry);
					ui_WidgetSetPositionEx(UI_WIDGET(pTextEntry), CSE_UI_LARGE_IND, y, CSE_UI_SIDE_PERCENT, 0, UITopLeft);
					pState->pInsertIntoContFunc(pCont, pTextEntry);
				}
				else
				{
					ui_WidgetSetPositionEx(UI_WIDGET(pTextEntry), CSE_UI_LARGE_IND, y, CSE_UI_SIDE_PERCENT, 0, UITopLeft);
				}

				if (eaGet(&pList->eaOverrideEquipment, i))
				{
					UIComboBox* pComboBox = eaGet(&pState->eaUIEntityEquipmentSlots, i);
					
					ui_TextEntrySetText(pTextEntry, REF_STRING_FROM_HANDLE(pList->eaOverrideEquipment[i]->hItem));
					
					if (!pComboBox)
					{
						pComboBox = ui_ComboBoxCreateWithEnum(CSE_UI_LARGE_IND + 0.75,y,0, SlotTypeEnum, cutEdComboBoxChangedEnumWrapperCB, pInfo);
						ui_WidgetSetWidthEx(UI_WIDGET(pComboBox), 0.20, UIUnitPercentage);
						ui_WidgetSetPositionEx(UI_WIDGET(pComboBox), 0, y, CSE_UI_SIDE_PERCENT/2 + 0.75f, 0, UITopLeft);
						pState->pInsertIntoContFunc(pCont, pComboBox);
						eaPush(&pState->eaUIEntityEquipmentSlots, pComboBox);
					}
					else
					{
						ui_WidgetSetPositionEx(UI_WIDGET(pComboBox), 0, y,CSE_UI_SIDE_PERCENT/2 + 0.75f, 0, UITopLeft);
					}
					ui_ComboBoxSetSelectedsAsString(pComboBox, StaticDefineInt_FastIntToString(SlotTypeEnum, pList->eaOverrideEquipment[i]->eSlot));
				}
				else
				{
					const char* pchText = ui_TextEntryGetText(pTextEntry);
					UIComboBox* pComboBox = eaGet(&pState->eaUIEntityEquipmentSlots, i);

					if (pchText && pchText[0])
						ui_TextEntrySetText(pTextEntry, "");

					if (pComboBox)
					{
						emPanelRemoveChild(pCont, UI_WIDGET(pComboBox), true);
						ui_WidgetQueueFree(UI_WIDGET(pComboBox));
						eaRemove(&pState->eaUIEntityEquipmentSlots, i);
					}
				}
				y = ui_WidgetGetNextY(UI_WIDGET(pTextEntry)) + CSE_UI_GAP;

			}
		}
	}
}

static void cutEdEntityChangedCB(void *unused, CutsceneGenericTrackInfo *pInfo)
{
	CutsceneEditorState *pState = pInfo->pState;
	CutsceneEntityPoint *pPoint = CSE_GetSelected(pInfo->ppSelected);
	CutsceneEntityList *pList = CSE_GetSelectedList(pInfo->ppSelected);
	char *pcCostumeName = NULL;
	char *pcTempString = NULL;
	F32 fNewLength = 0;
	int i;

	if(!cutEdIsEditable(pState))
		return;

	if(!pPoint)
		return;

	pList->entityType = ui_ComboBoxGetSelectedEnum(pState->pUIEntityType);

	ui_ComboBoxGetSelectedAsString(pState->pUIEntityCostume, &pcCostumeName);
	SET_HANDLE_FROM_STRING(g_hPlayerCostumeDict, pcCostumeName, pList->hCostume);
	estrDestroy(&pcCostumeName);

	pList->bPreserveMovementFX = ui_CheckButtonGetState(pState->pUIEntityPreserveMovementFX);

	pList->charClassType = ui_ComboBoxGetSelectedEnum(pState->pUIEntityClassType);

	pList->EntityIdx = atoi(ui_TextEntryGetText(pState->pUIEntityTeamIdx)) - 1;
	pList->EntityIdx = CLAMP(pList->EntityIdx, 0, MAX_TEAM_SIZE-1);

	if(pList->pchStaticEncName)
		StructFreeString(pList->pchStaticEncName);
	pList->pchStaticEncName = StructAllocString(ui_TextEntryGetText(pState->pUIEntityEncounterName));

	if(pList->pchActorName)
		StructFreeString(pList->pchActorName);
	pList->pchActorName = StructAllocString(ui_TextEntryGetText(pState->pUIEntityActorName));


	pPoint->actionType = ui_ComboBoxGetSelectedEnum(pState->pUIEntityActionType);

	if(pPoint->actionType == CutsceneEntAction_Spawn || pPoint->actionType == CutsceneEntAction_AddStance) {
		fNewLength = 0;
	} else {
		fNewLength = atof(ui_TextEntryGetText(pState->pUIEntityLength));
		MAX1(fNewLength, 0);
	}

	if(fNewLength != pPoint->common.length) {
		bool bFound = false;
		F32 fDiff = fNewLength - pPoint->common.length;
		for ( i=0; i < eaSize(&pList->ppEntityPoints); i++ ) {
			CutsceneEntityPoint *pMovedPoint = pList->ppEntityPoints[i];
			if(bFound) {
				pMovedPoint->common.time += fDiff;
			} else if(pMovedPoint == pPoint) {
				bFound = true;
			}
		}
		pPoint->common.length = fNewLength;
	}

	SET_HANDLE_FROM_STRING(g_AnimListDict, ui_TextEntryGetText(pState->pUIEntityAnimation), pPoint->hAnimList);

	ui_ComboBoxGetSelectedAsString(pState->pUIEntityStance, &pcTempString);
	pPoint->pchStance = allocAddString(pcTempString);

	ui_ComboBoxGetSelectedAsString(pState->pUIEntityFX, &pcTempString);
	pPoint->pchFXName = allocAddString(pcTempString);
	estrDestroy(&pcTempString);
	pPoint->bFlashFx = ui_CheckButtonGetState(pState->pUIEntityFXFlash);

	pPoint->vPosition[0] = atof(ui_TextEntryGetText(pState->pUIEntityXPos));
	pPoint->vPosition[1] = atof(ui_TextEntryGetText(pState->pUIEntityYPos));
	pPoint->vPosition[2] = atof(ui_TextEntryGetText(pState->pUIEntityZPos));
	pPoint->vRotation[1] = atof(ui_TextEntryGetText(pState->pUIEntityYRot));
	pPoint->vRotation[1] = RAD(pPoint->vRotation[1]);

	for (i = 0; i < eaSize(&pState->eaUIEntityEquipment); i++)
	{
		CutsceneEntityOverrideEquipment* pEquip = eaGet(&pList->eaOverrideEquipment, i);
		UITextEntry* pTextEntry = eaGet(&pState->eaUIEntityEquipment, i);
		if (pTextEntry)
		{
			const char* pchText = ui_TextEntryGetText(pTextEntry);
			if (RefSystem_ReferentFromString(g_hItemDict, pchText))
			{
				UIComboBox* pComboBox = eaGet(&pState->eaUIEntityEquipmentSlots, i);
				if (!pEquip)
				{
					pEquip = StructCreate(parse_CutsceneEntityOverrideEquipment);
					eaPush(&pList->eaOverrideEquipment, pEquip);
				}
				SET_HANDLE_FROM_STRING(g_hItemDict, pchText, pEquip->hItem);
				if (pComboBox)
					pEquip->eSlot = ui_ComboBoxGetSelectedEnum(pComboBox);
			}
			else if (pEquip)
			{
				StructDestroy(parse_CutsceneEntityOverrideEquipment, pEquip);
				pList->eaOverrideEquipment[i] = NULL;
			}
		}
	}
	for (i = eaSize(&pList->eaOverrideEquipment)-1; i >= 0; i--)
	{
		if (!pList->eaOverrideEquipment[i])
			eaRemove(&pList->eaOverrideEquipment, i);
	}
	cutEdRefreshUICommon(pState);
	cutEdSetUnsaved(pState);
}

static void cutEdTextureChangedCB(void *unused, CutsceneGenericTrackInfo *pInfo)
{
	CutsceneEditorState *pState = pInfo->pState;
	CutsceneTexturePoint *pPoint = CSE_GetSelected(pInfo->ppSelected);
	CutsceneTextureList *pList = CSE_GetSelectedList(pInfo->ppSelected);

	if(!cutEdIsEditable(pState))
		return;

	if(!pPoint)
		return;

	pList->pcTextureVariable = allocAddString(ui_TextEntryGetText(pState->pUITextureVariable));
	pList->pcTextureName = allocAddString(ui_TextureEntryGetTextureName(pState->pUITextureName));
	pList->eXAlign = ui_ComboBoxGetSelectedEnum(pState->pUITextureXAlign);
	pList->eYAlign = ui_ComboBoxGetSelectedEnum(pState->pUITextureYAlign);

	pPoint->fAlpha = atof(ui_TextEntryGetText(pState->pUITextureAlpha))/100.0f;
	pPoint->fAlpha = CLAMP(pPoint->fAlpha, 0.0f, 1.0f);
	pPoint->fScale = atof(ui_TextEntryGetText(pState->pUITextureScale));
	pPoint->fScale = MAX(pPoint->fScale, 0.01f);
	pPoint->vPosition[0] = atof(ui_TextEntryGetText(pState->pUITextureXPos));
	pPoint->vPosition[1] = atof(ui_TextEntryGetText(pState->pUITextureYPos));

	cutEdRefreshUICommon(pState);
	cutEdSetUnsaved(pState);
}

static void cutEdTextureChangedEnumWrapperCB(void *unused, int val, CutsceneGenericTrackInfo *pInfo)
{
	cutEdTextureChangedCB(NULL, pInfo);
}

static void cutEdFXChangedCB(void *unused, CutsceneGenericTrackInfo *pInfo)
{
	CutsceneEditorState *pState = pInfo->pState;
	CutsceneFXPoint *pPoint = CSE_GetSelected(pInfo->ppSelected);
	char *pcCurValue = NULL;

	if(!cutEdIsEditable(pState))
		return;

	if(!pPoint)
		return;

	ui_ComboBoxGetSelectedAsString(pState->pUIComboFX, &pcCurValue);
	pPoint->pcFXName = StructAllocString(pcCurValue);
	estrDestroy(&pcCurValue);

	pPoint->common.length = atof(ui_TextEntryGetText(pState->pUIFXLength));

	pPoint->vPosition[0] = atof(ui_TextEntryGetText(pState->pUIFXXPos));
	pPoint->vPosition[1] = atof(ui_TextEntryGetText(pState->pUIFXYPos));
	pPoint->vPosition[2] = atof(ui_TextEntryGetText(pState->pUIFXZPos));

	cutEdRefreshUICommon(pState);
	cutEdSetUnsaved(pState);
}

static void cutEdSoundChangedCB(void *unused, CutsceneGenericTrackInfo *pInfo)
{
	CutsceneEditorState *pState = pInfo->pState;
	CutsceneSoundPoint *pPoint = CSE_GetSelected(pInfo->ppSelected);
	char *pcCurValue = NULL;
	F32 fNewLength;

	if(!cutEdIsEditable(pState))
		return;

	if(!pPoint)
		return;

	pPoint->pcSoundVariable = allocAddString(ui_TextEntryGetText(pState->pUISoundVariable));
	ui_ComboBoxGetSelectedAsString(pState->pUIComboSound, &pcCurValue);
	pPoint->pSoundPath = allocAddString(pcCurValue);
	estrDestroy(&pcCurValue);

	fNewLength = sndEventGetLength(pPoint->pSoundPath)/1000.0f;
	if(fNewLength < 0) {
		if(pPoint->common.length <= 0)
			pPoint->common.length = 1.0f;
		pPoint->common.fixedLength = false;
	} else {
		if(!sndEventIsOneShot(pPoint->pSoundPath)) {
			Alertf("%s - This sounds is fixed length and not a one shot which is invalid", pPoint->pSoundPath);
		}
		pPoint->common.length = fNewLength;
		pPoint->common.fixedLength = true;
	}

	if(!pPoint->common.fixedLength) {
		pPoint->common.length = atof(ui_TextEntryGetText(pState->pUITextSoundLength));
	}

	StructFreeStringSafe(&pPoint->pchCutsceneEntName);
	pPoint->pchCutsceneEntName = StructAllocString(ui_TextEntryGetText(pState->pUITextSndCutEntName));

	pPoint->bUseCamPos = ui_CheckButtonGetState(pState->pUICheckSoundCamPos);

	pPoint->vPosition[0] = atof(ui_TextEntryGetText(pState->pUISoundXPos));
	pPoint->vPosition[1] = atof(ui_TextEntryGetText(pState->pUISoundYPos));
	pPoint->vPosition[2] = atof(ui_TextEntryGetText(pState->pUISoundZPos));

	cutEdRefreshUICommon(pState);
	cutEdSetUnsaved(pState);
}

static void cutEdSubtitleChangedCB(void *unused, CutsceneGenericTrackInfo *pInfo)
{
	CutsceneEditorState *pState = pInfo->pState;
	CutsceneSubtitlePoint *pPoint = CSE_GetSelected(pInfo->ppSelected);
	const Message *pNewMessage = NULL;

	if(!cutEdIsEditable(pState))
		return;

	if(!pPoint)
		return;

	pPoint->pcSubtitleVariable = allocAddString(ui_TextEntryGetText(pState->pUISubtitleVariable));
	pNewMessage = ui_MessageEntryGetMessage(pState->pUISubtitleMessage);
	StructCopyAll(parse_Message, pNewMessage, pPoint->displaySubtitle.pEditorCopy);

	pPoint->common.length = atof(ui_TextEntryGetText(pState->pUISubtitleLength));
	MAX1(pPoint->common.length, 0);

	StructFreeStringSafe(&pPoint->pchCutsceneEntName);
	pPoint->pchCutsceneEntName = StructAllocString(ui_TextEntryGetText(pState->pUISubtitleCutEntName));

	cutEdRefreshUICommon(pState);
	cutEdSetUnsaved(pState);
}

static void cutEdUIGenChangedCB(void *unused, CutsceneGenericTrackInfo *pInfo)
{
	CutsceneEditorState *pState = pInfo->pState;
	CutsceneUIGenPoint *pPoint = CSE_GetSelected(pInfo->ppSelected);
	const Message *pNewMessage = NULL;

	if(!cutEdIsEditable(pState))
		return;

	if(!pPoint)
		return;

	pPoint->common.time = atof(ui_TextEntryGetText(pState->pUIUIGenTime));

	pPoint->actionType = ui_ComboBoxGetSelectedEnum(pState->pUIUIGenActionType);

	StructFreeStringSafe(&pPoint->pcMessage);
	pPoint->pcMessage = StructAllocString(ui_TextEntryGetText(pState->pUIUIGenMessage));

	StructFreeStringSafe(&pPoint->pcVariable);
	pPoint->pcVariable = StructAllocString(ui_TextEntryGetText(pState->pUIUIGenVariable));

	StructFreeStringSafe(&pPoint->pcStringValue);
	pPoint->pcStringValue = StructAllocString(ui_TextEntryGetText(pState->pUIUIGenStringValue));

	pPoint->fFloatValue = atof(ui_TextEntryGetText(pState->pUIUIGenFloatValue));

	pNewMessage = ui_MessageEntryGetMessage(pState->pUIUIGenMessageValue);
	StructCopyAll(parse_Message, pNewMessage, pPoint->messageValue.pEditorCopy);

	StructFreeStringSafe(&pPoint->pcMessageValueVariable);
	pPoint->pcMessageValueVariable = StructAllocString(ui_TextEntryGetText(pState->pUIUIGenMessageValueVariable));

	cutEdRefreshUICommon(pState);
	cutEdSetUnsaved(pState);
}

static F32 cutEdGetPrevPointTime(CutsceneDef *pCutsceneDef, CutsceneEditType type, CutscenePath *pSelectedPath, int selectedPoint)
{
	int otherType;
	F32 time1, time2;
	CutscenePath *pPrevPath = NULL;
	CutscenePathPoint *pPrevPoint = cutEdGetPrevPoint(pCutsceneDef, type, pSelectedPath, selectedPoint, &pPrevPath);
	if(pPrevPoint && pPrevPath == pSelectedPath) {
		return pPrevPoint->common.time + pPrevPoint->common.length;
	}
	time1 = pPrevPoint ? (pPrevPoint->common.time + pPrevPoint->common.length) : 0;

	otherType = (type==CutsceneEditType_CameraPath ? CutsceneEditType_LookAtPath : CutsceneEditType_CameraPath);
	pPrevPoint = cutEdGetPrevPoint(pCutsceneDef, otherType, pSelectedPath, selectedPoint, &pPrevPath);
	time2 = pPrevPoint ? (pPrevPoint->common.time + pPrevPoint->common.length) : 0;

	return MAX(time1, time2);
}

static F32 cutEdGetNextPointTime(CutsceneDef *pCutsceneDef, CutsceneEditType type, CutscenePath *pSelectedPath, int selectedPoint)
{
	int otherType;
	F32 time1, time2;
	CutscenePath *pNextPath = NULL;
	CutscenePathPoint *pNextPoint = cutEdGetNextPoint(pCutsceneDef, type, pSelectedPath, selectedPoint, &pNextPath);
	if(pNextPoint && pNextPath == pSelectedPath) {
		return pNextPoint->common.time;
	}
	time1 = pNextPoint ? pNextPoint->common.time : -1;

	otherType = (type==CutsceneEditType_CameraPath ? CutsceneEditType_LookAtPath : CutsceneEditType_CameraPath);
	pNextPoint = cutEdGetNextPoint(pCutsceneDef, otherType, pSelectedPath, selectedPoint, &pNextPath);
	time2 = pNextPoint ? pNextPoint->common.time : -1;

	if(time1 >= 0 && time2 >= 0)
		return MIN(time1, time2);
	else if(time1 >= 0)
		return time1;
	else if(time2 >= 0)
		return time2;
	return -1;
}

static void curEdOffsetAfterSelected(CutsceneEditorState *pState, F32 prevPathEnd, F32 fOffset)
{
	int i;
	F32 newPathEnd = 0;
	bool bFound = false;

	assert(pState->pSelectedPath && pState->selectedPoint >= 0);

	for ( i=0; i < eaSize(&pState->pSelectedPath->ppPositions); i++ ) {
		F32 fLength;
		CutscenePathPoint *pPoint = pState->pSelectedPath->ppPositions[i];
		if(cutEdEditingCamPos(pState) && i > pState->selectedPoint) {
			pPoint->common.time += fOffset;
		}
		fLength = pPoint->common.length;
		if(cutEdEditingCamPos(pState) && i == pState->selectedPoint)
			fLength += fOffset;
		MAX1(newPathEnd, pPoint->common.time + fLength);
	}

	for ( i=0; i < eaSize(&pState->pSelectedPath->ppTargets); i++ ) {
		F32 fLength;
		CutscenePathPoint *pPoint = pState->pSelectedPath->ppTargets[i];
		if(cutEdEditingLookAtPos(pState) && i > pState->selectedPoint) {
			pPoint->common.time += fOffset;
		}
		fLength = pPoint->common.length;
		if(cutEdEditingLookAtPos(pState) && i == pState->selectedPoint)
			fLength += fOffset;
		MAX1(newPathEnd, pPoint->common.time + fLength);
	}

	fOffset = newPathEnd - prevPathEnd;

	for ( i=0; i < eaSize(&pState->pCutsceneDef->pPathList->ppPaths); i++ ) {
		CutscenePath *pPath = pState->pCutsceneDef->pPathList->ppPaths[i];
		if(!bFound) {
			if(pPath == pState->pSelectedPath)
				bFound = true;
			continue;
		}
		cutEdPathOffset(pPath, fOffset);
	}
}

static void cutEdPointFillFromUI(CutsceneEditorState *pState, CutscenePathPoint *pPoint, CutsceneEditType type, Mat4 newMat)
{
	Vec3 Xyz, Pyr;
	F32 newLength;
	F32 prevPointTime = cutEdGetPrevPointTime(pState->pCutsceneDef, type, pState->pSelectedPath, pState->selectedPoint);
	F32 nextPointTime = cutEdGetNextPointTime(pState->pCutsceneDef, type, pState->pSelectedPath, pState->selectedPoint);
	Xyz[0] = atof(ui_TextEntryGetText(pState->pUITextPointXPos));
	Xyz[1] = atof(ui_TextEntryGetText(pState->pUITextPointYPos));
	Xyz[2] = atof(ui_TextEntryGetText(pState->pUITextPointZPos));
	Pyr[0] = atof(ui_TextEntryGetText(pState->pUITextPointPRot));
	Pyr[1] = atof(ui_TextEntryGetText(pState->pUITextPointYRot));
	Pyr[2] = 0.0f;
	Pyr[0] = RAD(Pyr[0]);
	Pyr[1] = RAD(Pyr[1]);
	createMat3YPR(newMat, Pyr);
	copyVec3(Xyz, newMat[3]);
	pPoint->common.time = atof(ui_TextEntryGetText(pState->pUITextTime));
	MAX1(pPoint->common.time, prevPointTime);
	if(nextPointTime >= 0)
		MIN1(pPoint->common.time, nextPointTime);

	newLength = atof(ui_TextEntryGetText(pState->pUITextLength));
	MAX1(newLength, 0);
	if(newLength != pPoint->common.length)
	{
		F32 timeDiff = newLength - pPoint->common.length;
		F32 prevPathEnd = cutscene_GetPathEndTimeUnsafe(pState->pSelectedPath);
		curEdOffsetAfterSelected(pState, prevPathEnd, timeDiff);
		pPoint->common.length = newLength;
	}
	pPoint->easeIn = ui_CheckButtonGetState(pState->pUICheckEaseIn);
	pPoint->easeOut = ui_CheckButtonGetState(pState->pUICheckEaseOut);
}

static void cutEdPointUIChangedCameraCB(CutsceneEditorState *pState)
{
	CutscenePathPoint *pCamPos = cutEdGetSelectedPoint(pState, CutsceneEditType_CameraPath);
	CutscenePathPoint *pLookAtPos = cutEdGetSelectedPoint(pState, CutsceneEditType_LookAtPath);

	if(!cutEdIsEditable(pState))
		return;

	if(pCamPos && pLookAtPos)
	{
		Mat4 newMat;
		CutscenePathPoint pTempPoint = {0};
		F32 prevDist = distance3(pCamPos->pos, pLookAtPos->pos);
		cutEdPointCopyBasicData(&pTempPoint, pCamPos);
		cutEdPointFillFromUI(pState, &pTempPoint, CutsceneEditType_CameraPath, newMat);
		cutEdPointCopyBasicData(pCamPos, &pTempPoint);
		cutEdPointCopyBasicData(pLookAtPos, &pTempPoint);
		cutEdUpdateFromMatrix(pState, newMat);
		cutEdSetTransMats(pState);
		if(cutEdPositionsHaveChanged(pState->pNextUndoCutsceneDef, pState->pCutsceneDef, pState->pSelectedPath, CutsceneEditType_LookAtPath, pState->selectedPoint))
			cutEdPostMoveFixup(pState);
		cutEdRefreshUICommon(pState);
		cutEdSetUnsaved(pState);
	}
}

static void cutEdPointUIChangedPathCB(CutsceneEditorState *pState, CutsceneEditType mainType, CutsceneEditType secondaryType)
{
	CutscenePathPoint *pMainPos = cutEdGetSelectedPoint(pState, mainType);
	CutscenePathPoint *pSecondPos = cutEdGetSelectedPoint(pState, secondaryType);
	bool editing_both = cutEdCameraModeAllowed(pState->pSelectedPath);

	if(!cutEdIsEditable(pState))
		return;

	if(	pMainPos && (!editing_both || pSecondPos))
	{
		Mat4 newMat;
		cutEdPointFillFromUI(pState, pMainPos, mainType, newMat);
		if(editing_both)
			cutEdPointCopyBasicData(pSecondPos, pMainPos);
		cutEdUpdateFromMatrix(pState, newMat);
		cutEdSetTransMats(pState);
		if(cutEdPositionsHaveChanged(pState->pNextUndoCutsceneDef, pState->pCutsceneDef, pState->pSelectedPath, mainType, pState->selectedPoint))
			cutEdPostMoveFixup(pState);
		cutEdRefreshUICommon(pState);
		cutEdSetUnsaved(pState);
	}
}

static void cutEdPointUIChangedCB(void *unused, CutsceneEditorState *pState)
{
	switch(pState->editType)
	{
	case CutsceneEditType_Camera:
		cutEdPointUIChangedCameraCB(pState);
		break;
	case CutsceneEditType_CameraPath:
		cutEdPointUIChangedPathCB(pState, CutsceneEditType_CameraPath, CutsceneEditType_LookAtPath);
		break;
	case CutsceneEditType_LookAtPath:
		cutEdPointUIChangedPathCB(pState, CutsceneEditType_LookAtPath, CutsceneEditType_CameraPath);
		break;
	}
}

static void cutEdTimlineFrameTimeChangedCB(UITimeline *pTimeline, CutsceneEditorState *pState)
{
	int i, j;
	CutsceneDef *pCutsceneDef = pState->pCutsceneDef;

	if(!cutEdIsEditable(pState))
		return;

	for ( i=0; i < eaSize(&pState->ppTrackInfos); i++ ) {
		CutsceneGenericTrackInfo *pInfo = pState->ppTrackInfos[i];
		if(pInfo->bMulti) {
			CutsceneDummyTrack ***pppCGTs = (CutsceneDummyTrack***)TokenStoreGetEArray(parse_CutsceneDef, pInfo->iCGTColumn, pCutsceneDef, NULL);
			for ( j=0; j < eaSize(pppCGTs); j++ ) {
				eaQSort((*pppCGTs)[j]->ppGenPnts, cutEdSortGenPnts);
			}
		} else {
			CutsceneDummyTrack *pCGT = TokenStoreGetPointer(parse_CutsceneDef, pInfo->iCGTColumn, pCutsceneDef, 0, NULL);
			if(pCGT) {
				eaQSort(pCGT->ppGenPnts, cutEdSortGenPnts);
			}
		}
	}
	cutEdRefreshUICommon(pState);
	cutEdSetUnsaved(pState);
}

static void cutEdTimelineSelectionChangedCB(UITimeline *pTimeline, CutsceneEditorState *pState)
{
	int i, j;
	UITimelineKeyFrame **ppSelectionList = pTimeline->selection;

	cutEdSetSelectedPoint(pState, -1);

	if(eaSize(&ppSelectionList) == 1) {
		void *pData = ppSelectionList[0]->data;
		CutscenePath *pPath = cutEdGetPathFromPoint(pState, pData);
		if(pPath) {
			int idx;

			pState->pSelectedPath = pPath;

			idx = eaFind(&pPath->ppPositions, pData);
			if(idx >= 0) {
				if(!cutEdCameraModeAllowed(pState->pSelectedPath) || pState->editType != CutsceneEditType_Camera)
					pState->editType = CutsceneEditType_CameraPath;
				cutEdSetSelectedPoint(pState, idx);
			} else {
				idx = eaFind(&pPath->ppTargets, pData);
				if(idx >= 0) {
					if(!cutEdCameraModeAllowed(pState->pSelectedPath) || pState->editType != CutsceneEditType_Camera)
						pState->editType = CutsceneEditType_LookAtPath;
					cutEdSetSelectedPoint(pState, idx);
				}
			}
		}
	} else if (eaSize(&ppSelectionList) == 2) {
		CutscenePathPoint *pPoint1 = ppSelectionList[0]->data;
		CutscenePathPoint *pPoint2 = ppSelectionList[1]->data;
		CutscenePath *pPath1 = cutEdGetPathFromPoint(pState, pPoint1);
		CutscenePath *pPath2 = cutEdGetPathFromPoint(pState, pPoint2);
		if(pPath1 && pPath2 && pPath1 == pPath2 && cutEdCameraModeAllowed(pPath1)) {
			int idx1 = eaFind(&pPath1->ppPositions, pPoint1);
			int idx2 = eaFind(&pPath1->ppTargets, pPoint2);
			if(idx1 < 0) {
				idx1 = eaFind(&pPath1->ppPositions, pPoint2);
				idx2 = eaFind(&pPath1->ppTargets, pPoint1);
			}
			if(idx1 >=0 && idx1 == idx2) {

				pState->pSelectedPath = pPath1;

				if(pState->editType == CutsceneEditType_LookAtPath) {
					cutEdSetSelectedPoint(pState, idx2);
				} else {
					cutEdSetSelectedPoint(pState, idx1);
				}
			} 
		}
	}

	for ( i=0; i < eaSize(&ppSelectionList); i++ ) {
		UITimelineKeyFrame *pFrame = ppSelectionList[i];
		if(pFrame->track == pState->pTrackCamPos) {
			int idx = eaFind(&pState->pTrackCamPos->frames, pFrame);
			assert(idx >= 0);
			eaiPush(&pState->piSelectedCamPosPoints, idx);
		} else if (pFrame->track == pState->pTrackCamLookAt) {
			int idx = eaFind(&pState->pTrackCamLookAt->frames, pFrame);
			assert(idx >= 0);
			eaiPush(&pState->piSelectedLookAtPoints, idx);		
		} else {
			for ( j=0; j < eaSize(&pState->ppTrackInfos); j++ ) {
				CutsceneGenericTrackInfo *pInfo = pState->ppTrackInfos[j];

				if(pInfo->bMulti && eaFind(&pInfo->ppTracks, pFrame->track) >= 0)
					cutEdApplyToCGT(pState, pInfo, cutEdAddGenPntToSelection, pFrame->data);
				else if(pInfo->pTrack == pFrame->track)
					cutEdApplyToCGT(pState, pInfo, cutEdAddGenPntToSelection, pFrame->data);
			}
		}
	}

	cutEdRefreshUICommon(pState);
}

static void cutEdTimelineAddCGTCB(void *unused, CutsceneGenericTrackInfo *pInfo)
{
	int i;
	UITimelineTrack *pTrack;
	CutsceneEditorState *pState = pInfo->pState;
	CutsceneDef *pCutsceneDef = pInfo->pState->pCutsceneDef;
	CutsceneDummyTrack *pCGT = NULL;
	int track_type_idx;

	if(!cutEdIsEditable(pState))
		return;

	pCGT = StructCreateVoid(pInfo->pCGTPti);
	track_type_idx = eaFind(&pState->ppTrackInfos, pInfo);

	if(pInfo->bMulti) {
		char buf[256];
		CutsceneDummyTrack ***pppCGTs = (CutsceneDummyTrack***)TokenStoreGetEArray(parse_CutsceneDef, pInfo->iCGTColumn, pState->pCutsceneDef, NULL);
		if(eaSize(pppCGTs) > 0)
			sprintf(buf, "%s %d", pInfo->pcCGTName, eaSize(pppCGTs) + 1);
		else 
			sprintf(buf, "%s", pInfo->pcCGTName);
		pTrack = cutEdCreateTrack(buf, pInfo);
		eaPush(&pInfo->ppTracks, pTrack);
		for ( i=0; i < eaSize(&pInfo->ppTracks); i++ )
			pInfo->ppTracks[i]->order = track_type_idx*100 + i;
		eaPush(pppCGTs, pCGT);
		assert(eaSize(pppCGTs) == eaSize(&pInfo->ppTracks));

	} else {
		assert(!pInfo->pTrack);
		pTrack = cutEdCreateTrack(pInfo->pcCGTName, pInfo);
		pInfo->pTrack = pTrack;
		pInfo->pTrack->order = track_type_idx*100;
		TokenStoreSetPointer(parse_CutsceneDef, pInfo->iCGTColumn, pState->pCutsceneDef, 0, pCGT, NULL);
	}
	pCGT->common.pcName = StructAllocString(pTrack->name);
	ui_TimelineAddTrack(pState->pTimeline, pTrack);
	ui_TimelineTrackSetRightClickCallback(pTrack, cutEdTimelineAddGenPntCB, pInfo);
	ui_TimelineTrackSetLabelRightClickCallback(pTrack, cutEdTimelineGenericTrackLabelCB, pInfo);
	cutEdRefreshUICommon(pState);
	cutEdSetUnsaved(pState);
}

static void cutEdMoveToPointCB(void *unused, CutsceneEditorState *pState)
{
	CutscenePath *pPath = pState->pSelectedPath;
	CutscenePathPoint **ppPoints = cutEdGetPointsFromType(pPath, (pState->editType == CutsceneEditType_Camera) ? CutsceneEditType_CameraPath : pState->editType);
	F32 time = ppPoints[pState->selectedPoint]->common.time;
	ui_TimelineSetTimeAndCallback(pState->pTimeline, CSE_CutTimeToTrack(time));
}

static void cutEdGizmoDeactivate(const Mat4 matrix, CutsceneEditorState *pState)
{
	Mat4 newMat;
	Vec3 Pyr;

	if(!cutEdIsEditable(pState))
		return;

	if(pState->pSelectedPath && pState->selectedPoint >= 0) {
		RotateGizmoGetMatrix(cutEdRotateGizmo(), newMat);
		getMat3YPR(newMat, Pyr);
		Pyr[2] = 0.0f;
		createMat3YPR(newMat, Pyr);
		RotateGizmoSetMatrix(cutEdRotateGizmo(), newMat);

		cutEdReposFromGizmoToPoint(pState, false, pState->editType, newMat);
		cutEdUpdateFromMatrix(pState, newMat);
		cutEdPostMoveFixup(pState);
	} else {
		RotateGizmoGetMatrix(cutEdRotateGizmo(), newMat);
		if(cutEdGetOrSetSelectedMatrix(pState, newMat, true)) {
			RotateGizmoSetMatrix(cutEdRotateGizmo(), newMat);
			copyMat3(unitmat, newMat);
			TranslateGizmoSetMatrix(cutEdTranslateGizmo(), newMat);
		}
	}

	cutEdRefreshUICommon(pState);
	cutEdSetUnsaved(pState);
}

static void cutEdResetPointCB(void *unused, CutsceneEditorState *pState)
{
	Mat4 cameraMatrix;
	GfxCameraController *pCamera = gfxGetActiveCameraController();
	GfxCameraView *pCameraView = gfxGetActiveCameraView();

	createMat3YPR(cameraMatrix, pCamera->campyr);
	copyVec3(pCamera->camcenter, cameraMatrix[3]);
	scaleVec3(cameraMatrix[0], -1.0f, cameraMatrix[0]);//camera is -z facing
	scaleVec3(cameraMatrix[2], -1.0f, cameraMatrix[2]);

	RotateGizmoSetMatrix(cutEdRotateGizmo(), cameraMatrix);
	copyMat3(unitmat, cameraMatrix);
	TranslateGizmoSetMatrix(cutEdTranslateGizmo(), cameraMatrix);
	cutEdGizmoDeactivate(cameraMatrix, pState);
}

static void cutEdClonePointCB(void *unused, CutsceneEditorState *pState)
{
	CutscenePath *pPath = pState->pSelectedPath;

	if(!cutEdIsEditable(pState))
		return;

	if(!pPath || pState->selectedPoint < 0)
		return;

	if(cutEdCameraModeAllowed(pPath)) {
		CutscenePathPoint *pCamPos = cutEdGetSelectedPoint(pState, CutsceneEditType_CameraPath);
		CutscenePathPoint *pLookAt = cutEdGetSelectedPoint(pState, CutsceneEditType_LookAtPath);
		if(pCamPos && pLookAt) {
			eaInsert(&pPath->ppPositions, StructClone(parse_CutscenePathPoint, pCamPos), pState->selectedPoint);
			eaInsert(&pPath->ppTargets, StructClone(parse_CutscenePathPoint, pLookAt), pState->selectedPoint);
			cutEdSetSelectedPoint(pState, pState->selectedPoint+1);
		}
	} else {
		CutscenePathPoint *pPoint = cutEdGetSelectedPoint(pState, pState->editType);
		if(pPoint) {
			if(pState->editType == CutsceneEditType_CameraPath) {
				eaInsert(&pPath->ppPositions, StructClone(parse_CutscenePathPoint, pPoint), pState->selectedPoint);
			} else {
				eaInsert(&pPath->ppTargets, StructClone(parse_CutscenePathPoint, pPoint), pState->selectedPoint);
			}
			cutEdSetSelectedPoint(pState, pState->selectedPoint+1);
		}
	}
	cutEdPathReloadSplines(pPath);
	cutEdRefreshUICommon(pState);
	cutEdSetUnsaved(pState);
}

//////////////////////////////////////////////////////////////////////////
// Validate
//////////////////////////////////////////////////////////////////////////

static void cutEdValidatePoints(CutscenePathPoint **ppPoints)
{
	int i;
	for( i=0; i < eaSize(&ppPoints); i++ )
	{
		if(sameVec3(ppPoints[i]->tangent, zerovec3))
		{
			if(i==0)
			{
				if(eaSize(&ppPoints) > 1)
					subVec3(ppPoints[i+1]->pos, ppPoints[i]->pos, ppPoints[i]->tangent);
			}
			else if(i==eaSize(&ppPoints)-1)
			{
				subVec3(ppPoints[i]->pos, ppPoints[i-1]->pos, ppPoints[i]->tangent);
			}
			else
			{
				subVec3(ppPoints[i+1]->pos, ppPoints[i-1]->pos, ppPoints[i]->tangent);
			}
		}
		// If still zero
		if(sameVec3(ppPoints[i]->tangent, zerovec3))
			setVec3(ppPoints[i]->tangent, 0, 0, 1);
		normalVec3(ppPoints[i]->tangent);
		if(sameVec3(ppPoints[i]->up, zerovec3))
			setVec3(ppPoints[i]->up, 0, 1, 0);
		normalVec3(ppPoints[i]->up);
	}
}

static void cutEdValidate(CutsceneDef *pCutsceneDef)
{
	int i;

	if(!pCutsceneDef->pPathList)
	{
		//ShawnF TODO: Try Importing old data
		pCutsceneDef->pPathList = StructCreate(parse_CutscenePathList);
		pCutsceneDef->fBlendRate = CSE_DEFAULT_BLEND_RATE;
	}

	for( i=0; i < eaSize(&pCutsceneDef->pPathList->ppPaths); i++ )
	{
		CutscenePath *pPath = pCutsceneDef->pPathList->ppPaths[i];
		cutEdValidatePoints(pPath->ppPositions);
		cutEdValidatePoints(pPath->ppTargets);
	}
}

//////////////////////////////////////////////////////////////////////////
// Render Code
//////////////////////////////////////////////////////////////////////////

static void cutEdDrawCamera(CutsceneEditorState *pState, Vec3 loc, Vec3 look, Color color, F32 scale)
{
	Mat4 mat;
	Vec3 min = {-4,-4,-6};
	Vec3 max = { 4, 4, 5};
	Color alpha_color;
	Vec3 dir, end, pyr;
	GroupDef *pCameraDef;
	setVec4same(alpha_color.rgba, 100);
	scaleVec3(min, scale, min);
	scaleVec3(max, scale, max);

	subVec3(look, loc, dir);
	normalVec3(dir);
	mat3FromFwdVector(dir, mat);
	getMat3YPR(mat, pyr);
	pyr[2] = 0;
	createMat3YPR(mat, pyr);
	copyVec3(loc, mat[3]);
	scaleVec3(dir, 30*scale, end);
	addVec3(end, loc, end);
	gfxDrawLine3D_2(loc, end, color, alpha_color);
	scaleVec3(dir, 7.5*scale, end);
	addVec3(end, loc, end);

	pCameraDef = objectLibraryGetGroupDefByName("EditorCameraIcon", true);
	if(pCameraDef) {
		TempGroupParams tgparams = {0};
		GfxCameraController *pCamera = gfxGetActiveCameraController();
		F32 alpha = (distance3Squared(pCamera->camcenter, loc) / 100.0f) - 0.1f;
		Vec3 vTempColor;
		copyVec3(color.rgb, vTempColor);
		scaleVec3(vTempColor, 1/255.f, vTempColor);
		tgparams.tint_color0 = vTempColor;
		tgparams.alpha = CLAMP(alpha, 0.0001f, 1.0f);
		tgparams.no_culling = true;
		tgparams.editor_only = true;
		tgparams.dont_cast_shadows = true;
		worldAddTempGroup(pCameraDef, mat, &tgparams, true);
	} else {
		gfxDrawCylinder3D(loc, end, 3*scale, 16, true, color, 1);
		gfxDrawBox3D(min, max, mat, color, 0);
	}
}

static void cutEdDrawMainCamera(CutsceneEditorState *pState, Color color, F32 scale)
{
	Mat3 mat;
	Vec3 pos, look;

	if(!pState->bDetachedCamera || !pState->bPlaying)
		return;

	createMat3YPR(mat, pState->cameraPyr);
	subVec3(pState->cameraPos, mat[2], look); 
	copyVec3(pState->cameraPos, pos);

	cutEdDrawCamera(pState, pos, look, color, scale); 
}

static void cutEdDrawCircle(Vec3 moveVec, Vec3 holdVec, F32 inAngle, Color color)
{
	Vec3 lastPos, newPos, posVec;
	F32 angle;
	F32 angleStep = PI/16.0f;
	F32 sint, cost;
	F32 temp;

	if(inAngle == 0)
		return;
	if(inAngle < 0)
		angleStep *= -1.0f;
	copyVec3(moveVec, lastPos);

	for( angle=angleStep; ABS(angle) < ABS(inAngle); angle += angleStep )
	{
		sincosf(angle, &sint, &cost);
		subVec3(moveVec, holdVec, posVec);
		temp = posVec[0]*cost - posVec[2]*sint;
		posVec[2] = posVec[2]*cost + posVec[0]*sint;
		posVec[0] = temp;
		addVec3(posVec, holdVec, newPos);
		gfxDrawLine3D(lastPos, newPos, color);
		copyVec3(newPos, lastPos);
	}
	angle = inAngle;
	sincosf(angle, &sint, &cost);
	subVec3(moveVec, holdVec, posVec);
	temp = posVec[0]*cost - posVec[2]*sint;
	posVec[2] = posVec[2]*cost + posVec[0]*sint;
	posVec[0] = temp;
	addVec3(posVec, holdVec, newPos);
	gfxDrawLine3D(lastPos, newPos, color);
}

static void cutEdDrawTightPath(CutsceneEditorState *pState, CutscenePath *pPath, CutscenePathPoint **ppPoints, Color color1)
{
	int i;
	Mat4 entMat;
	if(pPath->type == CutscenePathType_ShadowEntity)
		cutEdGetOffsetMat(pState, &pPath->common, entMat, false);
	for( i=0; i < eaSize(&ppPoints)-1; i++ )
	{
		if(pPath->type == CutscenePathType_ShadowEntity)
		{
			Vec3 start, end;
			mulVecMat4(ppPoints[i]->pos, entMat, start);
			mulVecMat4(ppPoints[i+1]->pos, entMat, end);
			gfxDrawLine3D(start, end, color1);
		}
		else
		{
			gfxDrawLine3D(ppPoints[i]->pos, ppPoints[i+1]->pos, color1);
		}
	}
}

static void cutEdDrawCurve(CutsceneEditorState *pState, CutscenePath *pPath, Spline *pSpline, Color color1)
{
	int i, j;
	Mat4 entMat;
	if (!pSpline)
		return;
	if(pPath->type == CutscenePathType_ShadowEntity)
		cutEdGetOffsetMat(pState, &pPath->common, entMat, false);
	for (i = 0; i < eafSize(&pSpline->spline_points)-3; i+=3)
	{
		Vec3 controlPoints[4];
		splineGetControlPoints(pSpline, i, controlPoints);
		if(pPath->type == CutscenePathType_ShadowEntity)
		{
			for ( j=0; j < 4; j++ )
			{
				Vec3 temp;
				mulVecMat3(controlPoints[j], entMat, temp);
				addVec3(entMat[3], temp, controlPoints[j]);
			}
		}
		gfxDrawBezier3D(controlPoints, color1, color1, 5);
	}
}

static void cutEdDrawObjects(CutsceneEditorState *pState, CutsceneGenericTrackInfo *pInfo, CutsceneObjectList *pList, void *unused)
{
	int j;
	GroupDef *pObjectDef;
	TempGroupParams tgparams = {0};

	if(!pList->pcObjectName || !pList->pcObjectName[0])
		return;

	pObjectDef = objectLibraryGetGroupDefByName(pList->pcObjectName, true);
	if(!pObjectDef)
		return;

	tgparams.no_culling = true;
	tgparams.alpha = (pState->bPlaying ? 0.1 : 0.5);

	for ( j=0; j < eaSize(&pList->ppObjectPoints); j++ ) {
		CutsceneObjectPoint *pPoint = pList->ppObjectPoints[j];
		bool bSelected = cutEdIsGenPntSelected(pInfo, pPoint, false);
		Mat4 mMat;

		if(cutEdGetOrSetGenPntMatrix(pState, pInfo, pList, pPoint, mMat, false)) {
			tgparams.wireframe = bSelected;
			worldAddTempGroup(pObjectDef, mMat, &tgparams, true);		
		}
	}
}

static void cutEdDrawTextures(CutsceneEditorState *pState, CutsceneGenericTrackInfo *pInfo, CutsceneTextureList *pList, void *unused)
{
	int j;
	AtlasTex *pTexture;
	bool bFound = false;
	int uiWidth, uiHeight;
	int fullWidth, fullHeight, letterBox;
	int xStart, yStart;
	F32 xScale, yScale;
	#define CUT_ED_TEXTURE_SCALE 0.2f
	
	if(!pList->pcTextureName || !pList->pcTextureName[0])
		return;

	pTexture = atlasLoadTexture(pList->pcTextureName);
	if(!pTexture)
		return;

	uiWidth = g_ui_State.viewportMax[0] - g_ui_State.viewportMin[0];
	uiHeight = g_ui_State.viewportMax[1] - g_ui_State.viewportMin[1];
	gclCutsceneGetScreenSize(&fullWidth, &fullHeight, &letterBox, FLT_MAX);
	
	if(!fullWidth || !fullHeight)
		return;

	xScale = ((F32)uiWidth/fullWidth)*CUT_ED_TEXTURE_SCALE;
	yScale = ((F32)uiHeight/fullHeight)*CUT_ED_TEXTURE_SCALE;
	xStart = g_ui_State.viewportMin[0] + uiWidth/2.0f - fullWidth*CUT_ED_TEXTURE_SCALE/2.0f;
	yStart = g_ui_State.viewportMin[1] + uiHeight/2.0f - fullHeight*CUT_ED_TEXTURE_SCALE/2.0f;
	
	for ( j=0; j < eaSize(&pList->ppTexPoints); j++ ) {
		CutsceneTexturePoint *pPoint = pList->ppTexPoints[j];
		bool bSelected = cutEdIsGenPntSelected(pInfo, pPoint, false);
		F32 fScale = pPoint->fScale;
		F32 x = 0, y = 0;
		U8 iAlpha;

		if(!bSelected)
			continue;
	
		switch(pList->eXAlign) {
		xcase CutsceneAlignX_Left:
			x = pPoint->vPosition[0];
		xcase CutsceneAlignX_Right:
			x = fullWidth - pPoint->vPosition[0] - pTexture->width*fScale;
		xcase CutsceneAlignX_Center:
			x = fullWidth/2.0f + pPoint->vPosition[0] - pTexture->width*fScale/2.0f;
		}
		switch(pList->eYAlign) {
		xcase CutsceneAlignY_Top:
			y = pPoint->vPosition[1] + letterBox;
		xcase CutsceneAlignY_Bottom:
			y = fullHeight - pPoint->vPosition[1] - pTexture->height*fScale - letterBox;
		xcase CutsceneAlignY_Center:
			y = fullHeight/2.0f + pPoint->vPosition[1]- pTexture->height*fScale/2.0f;
		}

		x = xStart + x*xScale;
		y = yStart + y*yScale;

		iAlpha = 255-(pPoint->fAlpha*255.0f);
		display_sprite(pTexture, x, y, UI_UI2LIB_Z-3.1, fScale*xScale, fScale*yScale, 0xFFFFFF00 + iAlpha);
		gfxDrawBox(x, y, x+pTexture->width*fScale*xScale, y+pTexture->height*fScale*yScale, UI_UI2LIB_Z-3.1, colorFromRGBA(0xFFFF00FF));
		bFound = true;
	}

	if(bFound) {
		gfxDrawBox(xStart, yStart, xStart+fullWidth*xScale, yStart+fullHeight*yScale, UI_UI2LIB_Z-3.1, colorFromRGBA(0xAAAA00FF));
		gfxDrawBox(xStart, yStart, xStart+fullWidth*xScale, yStart+letterBox*yScale, UI_UI2LIB_Z-3.1, colorFromRGBA(0x777700FF));
		gfxDrawBox(xStart, yStart+(fullHeight-letterBox)*yScale, xStart+fullWidth*xScale, yStart+fullHeight*yScale, UI_UI2LIB_Z-3.1, colorFromRGBA(0x777700FF));
	}
}

static void cutEdDrawEntitys(CutsceneEditorState *pState, CutsceneGenericTrackInfo *pInfo, CutsceneEntityList *pList, void *unused)
{
	int j;
	GroupDef *pEntityDef = objectLibraryGetGroupDefByName("core_icons_humanoid", true);
	TempGroupParams tgparams = {0};

	if(!pEntityDef)
		return;

	tgparams.no_culling = true;
	tgparams.editor_only = true;
	tgparams.dont_cast_shadows = true;
	if(pState->bPlaying)
		tgparams.alpha = 0.1;

	for ( j=0; j < eaSize(&pList->ppEntityPoints); j++ ) {
		CutsceneEntityPoint *pPoint = pList->ppEntityPoints[j];
		bool bSelected = cutEdIsGenPntSelected(pInfo, pPoint, false);
		Mat4 mMat;

		if(pPoint->actionType == CutsceneEntAction_Animation)
			continue;
		if(pPoint->actionType == CutsceneEntAction_PlayFx)
			continue;
		if(pPoint->actionType == CutsceneEntAction_AddStance)
			continue;

		if(cutEdGetOrSetGenPntMatrix(pState, pInfo, pList, pPoint, mMat, false)) {
			tgparams.wireframe = bSelected;
			worldAddTempGroup(pEntityDef, mMat, &tgparams, true);		
		}
	}
}

static void cutEdDrawFX(CutsceneEditorState *pState, CutsceneGenericTrackInfo *pInfo, CutsceneFXList *pList, void *unused)
{
	int j;
	for ( j=0; j < eaSize(&pList->ppFXPoints); j++ ) {
		CutsceneFXPoint *pPoint = pList->ppFXPoints[j];
		bool bSelected = cutEdIsGenPntSelected(pInfo, pPoint, false);
		Color cColor = pInfo->color;
		Mat4 mMat;

		if(pState->bPlaying)
			cColor.a = 25;

		if(cutEdGetOrSetGenPntMatrix(pState, pInfo, pList, pPoint, mMat, false)) {
			if(bSelected)
				cColor = ColorLighten(cColor, 25);
			gfxDrawSphere3D(mMat[3], 4, 4, cColor, 0);
		}
	}
}

static void cutEdDrawSounds(CutsceneEditorState *pState, CutsceneGenericTrackInfo *pInfo, CutsceneSoundList *pList, void *unused)
{
	int j;
	for ( j=0; j < eaSize(&pList->ppSoundPoints); j++ ) {
		CutsceneSoundPoint *pPoint = pList->ppSoundPoints[j];
		bool bSelected = cutEdIsGenPntSelected(pInfo, pPoint, false);
		Color cColor = pInfo->color;
		Mat4 mMat;

		if(pPoint->bUseCamPos)
			continue;

		if(pState->bPlaying)
			cColor.a = 25;

		if(cutEdGetOrSetGenPntMatrix(pState, pInfo, pList, pPoint, mMat, false)) {
			if(bSelected)
				cColor = ColorLighten(cColor, 25);
			gfxDrawSphere3D(mMat[3], 4, 4, cColor, 0);
		}
	}
}

static void cutEdDrawTracks(CutsceneEditorState *pState)
{
	int i;
	for ( i=0; i < eaSize(&pState->ppTrackInfos); i++ ) {
		CutsceneGenericTrackInfo *pInfo = pState->ppTrackInfos[i];
		if(pInfo->pDrawFunc) {
			cutEdApplyToCGT(pState, pInfo, pInfo->pDrawFunc, NULL);
		}
	}
}

static int cutEdGetMouseOverPoint(CutsceneEditorState *pState, CutscenePathPoint **ppPosPoints, CutscenePathPoint **ppLookAtPoints, CutsceneEditType *pType)
{
	int j;
	Vec3 hit;
	F32 prevDist = 0;
	Vec3 start, end;
	int selected = -1;
	Mat4 tempMat;
	copyMat4(unitmat, tempMat);

	editLibCursorRay(start, end);

	for( j=0; j < eaSize(&ppPosPoints); j++ )
	{
		CutscenePathPoint *pPoint = ppPosPoints[j];
		Vec3 camPos;
		copyVec3(ppPosPoints[j]->pos, tempMat[3]);
		cutEdReposFromPointToGizmo(pState, true, CutsceneEditType_CameraPath, tempMat);
		copyVec3(tempMat[3], camPos);
		if(sphereLineCollisionWithHitPoint(start, end, camPos, CSE_POINT_COL_SIZE, hit))
		{
			F32 dist = distance3(camPos, start);
			if(selected < 0 || dist < prevDist)
			{
				prevDist = dist;
				selected = j;
				*pType = CutsceneEditType_CameraPath;
			}
		}
	}
	for( j=0; j < eaSize(&ppLookAtPoints); j++ )
	{
		CutscenePathPoint *pPoint = ppLookAtPoints[j];
		Vec3 lookAtPos;
		copyVec3(ppLookAtPoints[j]->pos, tempMat[3]);
		cutEdReposFromPointToGizmo(pState, true, CutsceneEditType_LookAtPath, tempMat);
		copyVec3(tempMat[3], lookAtPos);
		if(sphereLineCollisionWithHitPoint(start, end, lookAtPos, CSE_POINT_COL_SIZE, hit))
		{
			F32 dist = distance3(lookAtPos, start);
			if(selected < 0 || dist < prevDist)
			{
				prevDist = dist;
				selected = j;
				*pType = CutsceneEditType_LookAtPath;
			}
		}
	}
	return selected;
}

//////////////////////////////////////////////////////////////////////////
// Common Init
//////////////////////////////////////////////////////////////////////////

static void cutEdMakeMainCont(CutsceneEditorState *pState, CutsceneDef *pCutsceneDef)
{
	int y = 0;
	UILabel *pLabel;
	UITextEntry *pTextEntry;
	UICheckButton *pCheckButton;

	if(!demo_playingBack())
	{
		pState->pFileButton = ui_GimmeButtonCreate(CSE_UI_LEFT_IND, y, "Cutscene", pCutsceneDef->name, pCutsceneDef);
		pState->pInsertIntoContFunc(pState->pFileCont, pState->pFileButton);
		pState->pFilenameLabel = ui_LabelCreate(pCutsceneDef->filename, CSE_UI_LEFT_IND+20, y);
		pState->pInsertIntoContFunc(pState->pFileCont, pState->pFilenameLabel);
		ui_WidgetSetWidthEx(UI_WIDGET(pState->pFilenameLabel), 1.0, UIUnitPercentage);
		ui_WidgetSetPaddingEx(UI_WIDGET(pState->pFilenameLabel), 0, 21, 0, 0);

		pState->pAddContFunc(pState, pState->pFileCont);

		y = 0;

		pLabel = ui_LabelCreate("Demo File :", CSE_UI_LEFT_IND, y);
		pState->pInsertIntoContFunc(pState->pPreviewCont, pLabel);
		pTextEntry = ui_TextEntryCreate("last_recording", 0, y);
		ui_WidgetSetPositionEx(UI_WIDGET(pTextEntry), CSE_UI_LARGE_IND, y, CSE_UI_SIDE_PERCENT, 0, UITopLeft);
		ui_WidgetSetWidthEx(UI_WIDGET(pTextEntry), 1.0f-2*CSE_UI_SIDE_PERCENT, UIUnitPercentage);
		pState->pUITextRecordFileName = pTextEntry;
		pState->pInsertIntoContFunc(pState->pPreviewCont, pTextEntry);
		y = ui_WidgetGetNextY(UI_WIDGET(pTextEntry)) + CSE_UI_GAP;
	}

	pCheckButton = ui_CheckButtonCreate(CSE_UI_LEFT_IND, y, "Hide Paths", false);
	ui_CheckButtonSetToggledCallback(pCheckButton, cutEdToggleStateCB, &pState->bHidePaths);
	pState->pInsertIntoContFunc(pState->pPreviewCont, pCheckButton);
	y = ui_WidgetGetNextY(UI_WIDGET(pCheckButton)) + CSE_UI_GAP;

	pCheckButton = ui_CheckButtonCreate(CSE_UI_LEFT_IND, y, "Detach Camera", false);
	ui_CheckButtonSetToggledCallback(pCheckButton, cutEdToggleStateCB, &pState->bDetachedCamera);
	pState->pInsertIntoContFunc(pState->pPreviewCont, pCheckButton);
	y = ui_WidgetGetNextY(UI_WIDGET(pCheckButton)) + CSE_UI_GAP;

	pState->pAddContFunc(pState, pState->pPreviewCont);
}

static void cutEdPathListFillName(UIList *pList, UIListColumn *pColumn, S32 row, UserData unused, char **ppOutput)
{
	estrPrintf(ppOutput, "Path %d", row+1);
}

static void cutEdPathListFillType(UIList *pList, UIListColumn *pColumn, S32 row, UserData unused, char **ppOutput)
{
	CutscenePathType type = ((CutscenePath**) (*pList->peaModel))[row]->type;
	estrPrintf(ppOutput, "%s", StaticDefineIntRevLookup(CutscenePathTypeEnum, type));
}

static void cutEdEditModeCB(void *unused, int selected, CutsceneEditorState *pState)
{
	if(selected < 0)
		return;

	cutEdSetCutsceneEditMode(selected);
}

static void cutEdMakePathListCont(CutsceneEditorState *pState, CutsceneDef *pCutsceneDef)
{
	int y = 0;
	UIList *pList;
	UIListColumn *pCol;
	UIButton *pButton;
	UILabel *pLabel;
	UIComboBox *pCombo;
	UITextEntry *pTextEntry;
	UICheckButton *pCheckButton;

	if(demo_playingBack())
	{
		pButton = ui_ButtonCreate("Save", 0, y, cutEdSaveDemoCB, pState);
		ui_WidgetSetPositionEx(UI_WIDGET(pButton), 0, y, CSE_UI_SIDE_PERCENT, 0, UITopLeft);
		ui_WidgetSetWidthEx(UI_WIDGET(pButton), 1.0f-2*CSE_UI_SIDE_PERCENT, UIUnitPercentage);
		pState->pUIButtonSaveDemo = pButton;
		pState->pInsertIntoContFunc(pState->pPathListCont, pButton);
		y = ui_WidgetGetNextY(UI_WIDGET(pButton)) + CSE_UI_GAP;
	}
	else
	{
		pLabel = ui_LabelCreate("Map Name:", CSE_UI_LEFT_IND, y);
		pState->pInsertIntoContFunc(pState->pPathListCont, pLabel);
		pTextEntry = ui_TextEntryCreate("", 0, y);
		ui_WidgetSetPositionEx(UI_WIDGET(pTextEntry), CSE_UI_LARGE_IND, y, CSE_UI_SIDE_PERCENT, 0, UITopLeft);
		ui_WidgetSetWidthEx(UI_WIDGET(pTextEntry), 1.0f-2*CSE_UI_SIDE_PERCENT, UIUnitPercentage);
		ui_TextEntrySetFinishedCallback(pTextEntry, cutEdMapNameCB, pState);
		pState->pUITextMapName = pTextEntry;
		pState->pInsertIntoContFunc(pState->pPathListCont, pTextEntry);
		y = ui_WidgetGetNextY(UI_WIDGET(pTextEntry)) + CSE_UI_GAP;
	}

	pList = ui_ListCreate(parse_CutscenePath, &pCutsceneDef->pPathList->ppPaths, 15);
	ui_ListSetSelectedCallback(pList, cutEdPathListChangedCB, pState);
	ui_WidgetSetContextCallback(UI_WIDGET(pList), cutEdPathListRClickCB, pState);
	pCol = ui_ListColumnCreate(UIListTextCallback, "Path", (intptr_t)cutEdPathListFillName, NULL);
	pCol->fWidth = 100;
	ui_ListAppendColumn(pList, pCol);
	pCol = ui_ListColumnCreate(UIListTextCallback, "Type", (intptr_t)cutEdPathListFillType, NULL);
	pCol->fWidth = 100;
	ui_ListAppendColumn(pList, pCol);
	ui_WidgetSetPositionEx(UI_WIDGET(pList), 0, y, CSE_UI_SIDE_PERCENT, 0, UITopLeft);
	ui_WidgetSetHeight(UI_WIDGET(pList), 125);
	ui_WidgetSetWidthEx(UI_WIDGET(pList), 1.0f-2*CSE_UI_SIDE_PERCENT, UIUnitPercentage);
	pState->pUIListPaths = pList;
	pState->pInsertIntoContFunc(pState->pPathListCont, pList);
	y = ui_WidgetGetNextY(UI_WIDGET(pList)) + CSE_UI_GAP;

	pButton = ui_ButtonCreate("Add Path", 0, y, cutEdAddPathCB, pState);
	ui_WidgetSetPositionEx(UI_WIDGET(pButton), 0, y, CSE_UI_SIDE_PERCENT, 0, UITopLeft);
	ui_WidgetSetWidthEx(UI_WIDGET(pButton), 1.0f-2*CSE_UI_SIDE_PERCENT, UIUnitPercentage);
	pState->pInsertIntoContFunc(pState->pPathListCont, pButton);
	y = ui_WidgetGetNextY(UI_WIDGET(pButton)) + CSE_UI_GAP;

	pButton = ui_ButtonCreate("Delete Path", 0, y, cutEdDeletePathCB, pState);
	ui_WidgetSetPositionEx(UI_WIDGET(pButton), 0, y, CSE_UI_SIDE_PERCENT, 0, UITopLeft);
	ui_WidgetSetWidthEx(UI_WIDGET(pButton), 1.0f-2*CSE_UI_SIDE_PERCENT, UIUnitPercentage);
	pState->pInsertIntoContFunc(pState->pPathListCont, pButton);
	y = ui_WidgetGetNextY(UI_WIDGET(pButton)) + CSE_UI_GAP;

	pLabel = ui_LabelCreate("Edit Type:", CSE_UI_LEFT_IND, y);
	pState->pInsertIntoContFunc(pState->pPathListCont, pLabel);
	pCombo = ui_ComboBoxCreateWithEnum(0, y, 0, CutsceneEditTypeEnum, cutEdEditTypeCB, pState);
	ui_WidgetSetPositionEx(UI_WIDGET(pCombo), CSE_UI_LARGE_IND, y, CSE_UI_SIDE_PERCENT, 0, UITopLeft);
	ui_WidgetSetWidthEx(UI_WIDGET(pCombo), 1.0f-2*CSE_UI_SIDE_PERCENT, UIUnitPercentage);
	ui_ComboBoxSetSelectedEnum(pCombo, 0);
	pState->pUIComboEditType = pCombo;
	pState->pInsertIntoContFunc(pState->pPathListCont, pCombo);
	y = ui_WidgetGetNextY(UI_WIDGET(pCombo)) + CSE_UI_GAP;

	if(!demo_playingBack())
	{
		pCheckButton = ui_CheckButtonCreate(CSE_UI_LEFT_IND, y, "Play Once Only", false);
		ui_CheckButtonSetToggledCallback(pCheckButton, cutEdPlayOnceOnlyCB, pState);
		pState->pUICheckPlayOnceOnly = pCheckButton;
		pState->pInsertIntoContFunc(pState->pPathListCont, pCheckButton);
		y = ui_WidgetGetNextY(UI_WIDGET(pCheckButton)) + CSE_UI_GAP;

		pCheckButton = ui_CheckButtonCreate(CSE_UI_LEFT_IND, y, "Single Player Only", false);
		ui_CheckButtonSetToggledCallback(pCheckButton, cutEdSinglePlayerCB, pState);
		pState->pUICheckSinglePlayer = pCheckButton;
		pState->pInsertIntoContFunc(pState->pPathListCont, pCheckButton);
		y = ui_WidgetGetNextY(UI_WIDGET(pCheckButton)) + CSE_UI_GAP;
		
		pCheckButton = ui_CheckButtonCreate(CSE_UI_LEFT_IND, y, "Skippable", true);
		ui_CheckButtonSetToggledCallback(pCheckButton, cutEdSkippableCB, pState);
		pState->pUICheckSkippable = pCheckButton;
		pState->pInsertIntoContFunc(pState->pPathListCont, pCheckButton);
		y = ui_WidgetGetNextY(UI_WIDGET(pCheckButton)) + CSE_UI_GAP;

		pCheckButton = ui_CheckButtonCreate(CSE_UI_LEFT_IND, y, "Hide All Players", false);
		ui_CheckButtonSetToggledCallback(pCheckButton, cutEdHideAllPlayersCB, pState);
		pState->pUICheckHideAllPlayers = pCheckButton;
		pState->pInsertIntoContFunc(pState->pPathListCont, pCheckButton);
		y = ui_WidgetGetNextY(UI_WIDGET(pCheckButton)) + CSE_UI_GAP;

		pCheckButton = ui_CheckButtonCreate(CSE_UI_LEFT_IND, y, "Players Are Untargetable", false);
		ui_CheckButtonSetToggledCallback(pCheckButton, cutEdMakePlayersUntargetableCB, pState);
		pState->pUICheckMakePlayersUntargetable = pCheckButton;
		pState->pInsertIntoContFunc(pState->pPathListCont, pCheckButton);
		y = ui_WidgetGetNextY(UI_WIDGET(pCheckButton)) + CSE_UI_GAP;

		pCheckButton = ui_CheckButtonCreate(CSE_UI_LEFT_IND, y, "Disable Camera Light", false);
		ui_CheckButtonSetToggledCallback(pCheckButton, cutEdDisableCamLightCB, pState);
		pState->pUICheckDisableCamLight = pCheckButton;
		pState->pInsertIntoContFunc(pState->pPathListCont, pCheckButton);
		y = ui_WidgetGetNextY(UI_WIDGET(pCheckButton)) + CSE_UI_GAP;

		pLabel = ui_LabelCreate("Player fade time:", CSE_UI_LEFT_IND, y);
		pState->pInsertIntoContFunc(pState->pPathListCont, pLabel);
		pTextEntry = ui_TextEntryCreate("0.0", 0, y);
		ui_WidgetSetPositionEx(UI_WIDGET(pTextEntry), CSE_UI_LARGE_IND, y, CSE_UI_SIDE_PERCENT, 0, UITopLeft);
		ui_WidgetSetWidthEx(UI_WIDGET(pTextEntry), 1.0f-2*CSE_UI_SIDE_PERCENT, UIUnitPercentage);
		ui_TextEntrySetFinishedCallback(pTextEntry, cutEdTimeToFadeCB, pState);
		pState->pUITextTimeToFade = pTextEntry;
		pState->pInsertIntoContFunc(pState->pPathListCont, pTextEntry);
		y = ui_WidgetGetNextY(UI_WIDGET(pTextEntry)) + CSE_UI_GAP;
	}
	else
	{
		pLabel = ui_LabelCreate("Edit Mode:", CSE_UI_LEFT_IND, y);
		pState->pInsertIntoContFunc(pState->pPathListCont, pLabel);
		pCombo = ui_ComboBoxCreateWithEnum(0, y, 0, CutsceneEditModeEnum, cutEdEditModeCB, pState);
		ui_WidgetSetPositionEx(UI_WIDGET(pCombo), CSE_UI_LARGE_IND, y, CSE_UI_SIDE_PERCENT, 0, UITopLeft);
		ui_WidgetSetWidthEx(UI_WIDGET(pCombo), 1.0f-2*CSE_UI_SIDE_PERCENT, UIUnitPercentage);
		ui_ComboBoxSetSelectedEnum(pCombo, 0);
		pState->pUIComboEditMode = pCombo;
		pState->pInsertIntoContFunc(pState->pPathListCont, pCombo);
		y = ui_WidgetGetNextY(UI_WIDGET(pCombo)) + CSE_UI_GAP;
	}

	pLabel = ui_LabelCreate("Blend Amount:", CSE_UI_LEFT_IND, y);
	pState->pInsertIntoContFunc(pState->pPathListCont, pLabel);
	pTextEntry = ui_TextEntryCreate("0.0", 0, y);
	ui_WidgetSetPositionEx(UI_WIDGET(pTextEntry), CSE_UI_LARGE_IND, y, CSE_UI_SIDE_PERCENT, 0, UITopLeft);
	ui_WidgetSetWidthEx(UI_WIDGET(pTextEntry), 1.0f-2*CSE_UI_SIDE_PERCENT, UIUnitPercentage);
	ui_TextEntrySetFinishedCallback(pTextEntry, cutEdBlendRateCB, pState);
	pState->pUITextBlendRate = pTextEntry;
	pState->pInsertIntoContFunc(pState->pPathListCont, pTextEntry);
	y = ui_WidgetGetNextY(UI_WIDGET(pTextEntry)) + CSE_UI_GAP;

	if(!demo_playingBack())
	{
		pLabel = ui_LabelCreate("Tween At End:", CSE_UI_LEFT_IND, y);
		pState->pInsertIntoContFunc(pState->pPathListCont, pLabel);
		pTextEntry = ui_TextEntryCreate("0.0", 0, y);
		ui_WidgetSetPositionEx(UI_WIDGET(pTextEntry), CSE_UI_LARGE_IND, y, CSE_UI_SIDE_PERCENT, 0, UITopLeft);
		ui_WidgetSetWidthEx(UI_WIDGET(pTextEntry), 1.0f-2*CSE_UI_SIDE_PERCENT, UIUnitPercentage);
		ui_TextEntrySetFinishedCallback(pTextEntry, cutEdTweenToGameCameraCB, pState);
		pState->pUITextTweenToGameCamera = pTextEntry;
		pState->pInsertIntoContFunc(pState->pPathListCont, pTextEntry);
		y = ui_WidgetGetNextY(UI_WIDGET(pTextEntry)) + CSE_UI_GAP;
	}

	pLabel = ui_LabelCreate("Total Time:", CSE_UI_LEFT_IND, y);
	pState->pInsertIntoContFunc(pState->pPathListCont, pLabel);
	pTextEntry = ui_TextEntryCreate("0.0", 0, y);
	ui_WidgetSetPositionEx(UI_WIDGET(pTextEntry), CSE_UI_LARGE_IND, y, CSE_UI_SIDE_PERCENT, 0, UITopLeft);
	ui_WidgetSetWidthEx(UI_WIDGET(pTextEntry), 1.0f-2*CSE_UI_SIDE_PERCENT, UIUnitPercentage);
	ui_TextEntrySetFinishedCallback(pTextEntry, cutEdTotalTimeCB, pState);
	pState->pUITextTotalTime = pTextEntry;
	pState->pInsertIntoContFunc(pState->pPathListCont, pTextEntry);
	y = ui_WidgetGetNextY(UI_WIDGET(pTextEntry)) + CSE_UI_GAP;

	pLabel = ui_LabelCreate("Total Move Time:", CSE_UI_LEFT_IND, y);
	pState->pInsertIntoContFunc(pState->pPathListCont, pLabel);
	pTextEntry = ui_TextEntryCreate("0.0", 0, y);
	ui_WidgetSetPositionEx(UI_WIDGET(pTextEntry), CSE_UI_LARGE_IND, y, CSE_UI_SIDE_PERCENT, 0, UITopLeft);
	ui_WidgetSetWidthEx(UI_WIDGET(pTextEntry), 1.0f-2*CSE_UI_SIDE_PERCENT, UIUnitPercentage);
	ui_TextEntrySetFinishedCallback(pTextEntry, cutEdTotalMoveTimeCB, pState);
	pState->pUITextTotalMoveTime = pTextEntry;
	pState->pInsertIntoContFunc(pState->pPathListCont, pTextEntry);
	y = ui_WidgetGetNextY(UI_WIDGET(pTextEntry)) + CSE_UI_GAP;

	pButton = ui_ButtonCreate("Reset Speeds", 0, y, cutEdResetSpeedsCB, pState);
	ui_WidgetSetPositionEx(UI_WIDGET(pButton), 0, y, CSE_UI_SIDE_PERCENT, 0, UITopLeft);
	ui_WidgetSetWidthEx(UI_WIDGET(pButton), 1.0f-2*CSE_UI_SIDE_PERCENT, UIUnitPercentage);
	pState->pInsertIntoContFunc(pState->pPathListCont, pButton);
	y = ui_WidgetGetNextY(UI_WIDGET(pButton)) + CSE_UI_GAP;

	pLabel = ui_LabelCreate("All Ents in Range:", CSE_UI_LEFT_IND, y);
	pState->pInsertIntoContFunc(pState->pPathListCont, pLabel);
	pTextEntry = ui_TextEntryCreate("0.0", 0, y);
	ui_WidgetSetPositionEx(UI_WIDGET(pTextEntry), CSE_UI_LARGE_IND, y, CSE_UI_SIDE_PERCENT, 0, UITopLeft);
	ui_WidgetSetWidthEx(UI_WIDGET(pTextEntry), 1.0f-2*CSE_UI_SIDE_PERCENT, UIUnitPercentage);
	ui_TextEntrySetFinishedCallback(pTextEntry, cutEdEntsInRangeCB, pState);
	pState->pUITextSendRange = pTextEntry;
	pState->pInsertIntoContFunc(pState->pPathListCont, pTextEntry);
	y = ui_WidgetGetNextY(UI_WIDGET(pTextEntry)) + CSE_UI_GAP;

	pState->pAddContFunc(pState, pState->pPathListCont);
}

static void cutEdPointListFillName(UIList *pList, UIListColumn *pColumn, S32 row, UserData unused, char **ppOutput)
{
	estrPrintf(ppOutput, "Point %d", row+1);
}

static void cutEdMakePointListCont(CutsceneEditorState *pState, CutsceneDef *pCutsceneDef)
{
	int y = 0;
	UIList *pList;
	UIListColumn *pCol;
	UILabel *pLabel;
	UICheckButton *pCheckButton;
	UITextEntry *pTextEntry;
	UIButton *pButton;

	pCheckButton = ui_CheckButtonCreate(CSE_UI_LEFT_IND, y, "Smooth Camera Path", false);
	ui_CheckButtonSetToggledCallback(pCheckButton, cutEdCamPathSmoothCB, pState);
	pState->pUICheckSmoothCamPath = pCheckButton;
	pState->pInsertIntoContFunc(pState->pPointListCont, pCheckButton);
	y = ui_WidgetGetNextY(UI_WIDGET(pCheckButton)) + CSE_UI_GAP;

	pLabel = ui_LabelCreate("Camera Positions:", CSE_UI_LEFT_IND, y);
	pState->pInsertIntoContFunc(pState->pPointListCont, pLabel);
	y = ui_WidgetGetNextY(UI_WIDGET(pLabel)) + CSE_UI_GAP;

	pList = ui_ListCreate(parse_CutscenePathPoint, NULL, 15);
	ui_ListSetSelectedCallback(pList, cutEdCamPosPointListChangedCB, pState);
	pCol = ui_ListColumnCreate(UIListTextCallback, "Point", (intptr_t)cutEdPointListFillName, NULL);
	pCol->fWidth = 75;
	ui_ListAppendColumn(pList, pCol);
	pCol = ui_ListColumnCreate(UIListPTName, "Time", (intptr_t)"Time", NULL);
	pCol->fWidth = 75;
	ui_ListAppendColumn(pList, pCol);
	pCol = ui_ListColumnCreate(UIListPTName, "Hold", (intptr_t)"Length", NULL);
	pCol->fWidth = 75;
	ui_ListAppendColumn(pList, pCol);
	ui_WidgetSetPositionEx(UI_WIDGET(pList), 0, y, CSE_UI_SIDE_PERCENT, 0, UITopLeft);
	ui_WidgetSetHeight(UI_WIDGET(pList), 125);
	ui_WidgetSetWidthEx(UI_WIDGET(pList), 1.0f-2*CSE_UI_SIDE_PERCENT, UIUnitPercentage);
	pState->pUIListPosPoints = pList;
	pState->pInsertIntoContFunc(pState->pPointListCont, pList);
	y = ui_WidgetGetNextY(UI_WIDGET(pList)) + CSE_UI_GAP;

	pCheckButton = ui_CheckButtonCreate(CSE_UI_LEFT_IND, y, "Smooth Look At Path", false);
	ui_CheckButtonSetToggledCallback(pCheckButton, cutEdLookAtPathSmoothCB, pState);
	pState->pUICheckSmoothLookAtPath = pCheckButton;
	pState->pInsertIntoContFunc(pState->pPointListCont, pCheckButton);
	y = ui_WidgetGetNextY(UI_WIDGET(pCheckButton)) + CSE_UI_GAP;

	pLabel = ui_LabelCreate("Look At Positions:", CSE_UI_LEFT_IND, y);
	pState->pInsertIntoContFunc(pState->pPointListCont, pLabel);
	y = ui_WidgetGetNextY(UI_WIDGET(pLabel)) + CSE_UI_GAP;

	pList = ui_ListCreate(parse_CutscenePathPoint, NULL, 15);
	ui_ListSetSelectedCallback(pList, cutEdLookAtPointListChangedCB, pState);
	pCol = ui_ListColumnCreate(UIListTextCallback, "Point", (intptr_t)cutEdPointListFillName, NULL);
	pCol->fWidth = 75;
	ui_ListAppendColumn(pList, pCol);
	pCol = ui_ListColumnCreate(UIListPTName, "Time", (intptr_t)"Time", NULL);
	pCol->fWidth = 75;
	ui_ListAppendColumn(pList, pCol);
	pCol = ui_ListColumnCreate(UIListPTName, "Hold", (intptr_t)"Length", NULL);
	pCol->fWidth = 75;
	ui_ListAppendColumn(pList, pCol);
	ui_WidgetSetPositionEx(UI_WIDGET(pList), 0, y, CSE_UI_SIDE_PERCENT, 0, UITopLeft);
	ui_WidgetSetHeight(UI_WIDGET(pList), 125);
	ui_WidgetSetWidthEx(UI_WIDGET(pList), 1.0f-2*CSE_UI_SIDE_PERCENT, UIUnitPercentage);
	pState->pUIListLookAtPoints = pList;
	pState->pInsertIntoContFunc(pState->pPointListCont, pList);
	y = ui_WidgetGetNextY(UI_WIDGET(pList)) + CSE_UI_GAP;

	pLabel = ui_LabelCreate("Path Time:", CSE_UI_LEFT_IND, y);
	pState->pInsertIntoContFunc(pState->pPointListCont, pLabel);
	pTextEntry = ui_TextEntryCreate("0.0", 0, y);
	ui_WidgetSetPositionEx(UI_WIDGET(pTextEntry), CSE_UI_LARGE_IND, y, CSE_UI_SIDE_PERCENT, 0, UITopLeft);
	ui_WidgetSetWidthEx(UI_WIDGET(pTextEntry), 1.0f-2*CSE_UI_SIDE_PERCENT, UIUnitPercentage);
	ui_TextEntrySetFinishedCallback(pTextEntry, cutEdPathTimeCB, pState);
	pState->pUITextPathTime = pTextEntry;
	pState->pInsertIntoContFunc(pState->pPointListCont, pTextEntry);
	y = ui_WidgetGetNextY(UI_WIDGET(pTextEntry)) + CSE_UI_GAP;

	pLabel = ui_LabelCreate("Path Move Time:", CSE_UI_LEFT_IND, y);
	pState->pInsertIntoContFunc(pState->pPointListCont, pLabel);
	pTextEntry = ui_TextEntryCreate("0.0", 0, y);
	ui_WidgetSetPositionEx(UI_WIDGET(pTextEntry), CSE_UI_LARGE_IND, y, CSE_UI_SIDE_PERCENT, 0, UITopLeft);
	ui_WidgetSetWidthEx(UI_WIDGET(pTextEntry), 1.0f-2*CSE_UI_SIDE_PERCENT, UIUnitPercentage);
	ui_TextEntrySetFinishedCallback(pTextEntry, cutEdPathMoveTimeCB, pState);
	pState->pUITextPathMoveTime = pTextEntry;
	pState->pInsertIntoContFunc(pState->pPointListCont, pTextEntry);
	y = ui_WidgetGetNextY(UI_WIDGET(pTextEntry)) + CSE_UI_GAP;

	pButton = ui_ButtonCreate("Reset Path Speeds", 0, y, cutEdResetPathSpeedsCB, pState);
	ui_WidgetSetPositionEx(UI_WIDGET(pButton), 0, y, CSE_UI_SIDE_PERCENT, 0, UITopLeft);
	ui_WidgetSetWidthEx(UI_WIDGET(pButton), 1.0f-2*CSE_UI_SIDE_PERCENT, UIUnitPercentage);
	pState->pInsertIntoContFunc(pState->pPointListCont, pButton);
	y = ui_WidgetGetNextY(UI_WIDGET(pButton)) + CSE_UI_GAP;
}

static void cutEdMakeBasicPathCont(CutsceneEditorState *pState, CutsceneDef *pCutsceneDef)
{
	int y = 0;
	UIButton *pButton;

	pButton = ui_ButtonCreate("Add Point", 0, y, cutEdAddPointCB, pState);
	ui_WidgetSetPositionEx(UI_WIDGET(pButton), 0, y, CSE_UI_SIDE_PERCENT, 0, UITopLeft);
	ui_WidgetSetWidthEx(UI_WIDGET(pButton), 1.0f-2*CSE_UI_SIDE_PERCENT, UIUnitPercentage);
	pState->pInsertIntoContFunc(pState->pBasicPathCont, pButton);
	y = ui_WidgetGetNextY(UI_WIDGET(pButton)) + CSE_UI_GAP;

	pButton = ui_ButtonCreate("Delete Point", 0, y, cutEdDeletePointCB, pState);
	ui_WidgetSetPositionEx(UI_WIDGET(pButton), 0, y, CSE_UI_SIDE_PERCENT, 0, UITopLeft);
	ui_WidgetSetWidthEx(UI_WIDGET(pButton), 1.0f-2*CSE_UI_SIDE_PERCENT, UIUnitPercentage);
	pState->pInsertIntoContFunc(pState->pBasicPathCont, pButton);
	y = ui_WidgetGetNextY(UI_WIDGET(pButton)) + CSE_UI_GAP;
}

static void cutEdMakeCirclePathCont(CutsceneEditorState *pState, CutsceneDef *pCutsceneDef)
{
	int y = 0;
	UILabel *pLabel;
	UITextEntry *pTextEntry;

	pLabel = ui_LabelCreate("Rotation:", CSE_UI_LEFT_IND, y);
	pState->pInsertIntoContFunc(pState->pCirclePathCont, pLabel);
	pTextEntry = ui_TextEntryCreate("0.0", 0, y);
	ui_WidgetSetPositionEx(UI_WIDGET(pTextEntry), CSE_UI_LARGE_IND, y, CSE_UI_SIDE_PERCENT, 0, UITopLeft);
	ui_WidgetSetWidthEx(UI_WIDGET(pTextEntry), 1.0f-2*CSE_UI_SIDE_PERCENT, UIUnitPercentage);
	ui_TextEntrySetFinishedCallback(pTextEntry, cutEdAngleCB, pState);
	pState->pUITextAngle = pTextEntry;
	pState->pInsertIntoContFunc(pState->pCirclePathCont, pTextEntry);
	y = ui_WidgetGetNextY(UI_WIDGET(pTextEntry)) + CSE_UI_GAP;
}

static void cutEdCGTRelPosChangedComboWrapper(void *unused, int i, CutsceneEditorState *pState)
{
	cutEdCGTRelPosChanged(NULL, pState);
}

static void cutEdMakeCGTRelPosCont(CutsceneEditorState *pState)
{
	int y = 0;
	UILabel *pLabel;
//	UIButton *pButton;
	UITextEntry *pTextEntry;
	UIComboBox *pComboBox;
	UICheckButton *pCheckButton;

	if(!demo_playingBack())
	{
		pCheckButton = ui_CheckButtonCreate(CSE_UI_LEFT_IND, y, "Relative Positions", false);
		ui_CheckButtonSetToggledCallback(pCheckButton, cutEdCGTRelPosChanged, pState);
		pState->pUICheckRelPosOn = pCheckButton;
		pState->pInsertIntoContFunc(pState->pRelativePosCont, pCheckButton);
		y = ui_WidgetGetNextY(UI_WIDGET(pCheckButton)) + CSE_UI_GAP;

		pLabel = ui_LabelCreate("Follow Type:", CSE_UI_LEFT_IND, y);
		pState->pInsertIntoContFunc(pState->pRelativePosCont, pLabel);
		pComboBox = ui_ComboBoxCreateWithEnum(0,y,1, CutsceneOffsetTypeEnum, cutEdCGTRelPosChangedComboWrapper, pState);
		ui_WidgetSetPositionEx(UI_WIDGET(pComboBox), CSE_UI_LARGE_IND, y, CSE_UI_SIDE_PERCENT, 0, UITopLeft);
		ui_WidgetSetWidthEx(UI_WIDGET(pComboBox), 1.0f-2*CSE_UI_SIDE_PERCENT, UIUnitPercentage);
		pState->pUIComboRelPosType = pComboBox;
		pState->pInsertIntoContFunc(pState->pRelativePosCont, pComboBox);
		y = ui_WidgetGetNextY(UI_WIDGET(pComboBox)) + CSE_UI_GAP;

		pLabel = ui_LabelCreate("Entity Name:", CSE_UI_LEFT_IND, y);
		pState->pInsertIntoContFunc(pState->pRelativePosCont, pLabel);
		pTextEntry = ui_TextEntryCreate("", 0, y);
		ui_WidgetSetPositionEx(UI_WIDGET(pTextEntry), CSE_UI_LARGE_IND, y, CSE_UI_SIDE_PERCENT, 0, UITopLeft);
		ui_WidgetSetWidthEx(UI_WIDGET(pTextEntry), 1.0f-2*CSE_UI_SIDE_PERCENT, UIUnitPercentage);
		ui_TextEntrySetFinishedCallback(pTextEntry, cutEdCGTRelPosChanged, pState);
		pState->pUITextRelPosCutsceneEntName = pTextEntry;
		pState->pInsertIntoContFunc(pState->pRelativePosCont, pTextEntry);
		y = ui_WidgetGetNextY(UI_WIDGET(pTextEntry)) + CSE_UI_GAP;

		pLabel = ui_LabelCreate("Encounter Name:", CSE_UI_LEFT_IND, y);
		pState->pInsertIntoContFunc(pState->pRelativePosCont, pLabel);
		pTextEntry = ui_TextEntryCreateWithStringCombo("", 0, y, &s_eaEncounterList, true, true, false, true);
		ui_WidgetSetPositionEx(UI_WIDGET(pTextEntry), CSE_UI_LARGE_IND, y, CSE_UI_SIDE_PERCENT, 0, UITopLeft);
		ui_WidgetSetWidthEx(UI_WIDGET(pTextEntry), 1.0f-2*CSE_UI_SIDE_PERCENT, UIUnitPercentage);
		ui_TextEntrySetFinishedCallback(pTextEntry, cutEdCGTRelPosChanged, pState);
		pState->pUITextRelPosEncounterName = pTextEntry;
		pState->pInsertIntoContFunc(pState->pRelativePosCont, pTextEntry);
		y = ui_WidgetGetNextY(UI_WIDGET(pTextEntry)) + CSE_UI_GAP;

		pLabel = ui_LabelCreate("Actor Name:", CSE_UI_LEFT_IND, y);
		pState->pInsertIntoContFunc(pState->pRelativePosCont, pLabel);
		pTextEntry = ui_TextEntryCreateWithStringCombo("", 0, y, &pState->eaRelPosActorNameModel, true, true, false, true);
		ui_WidgetSetPositionEx(UI_WIDGET(pTextEntry), CSE_UI_LARGE_IND, y, CSE_UI_SIDE_PERCENT, 0, UITopLeft);
		ui_WidgetSetWidthEx(UI_WIDGET(pTextEntry), 1.0f-2*CSE_UI_SIDE_PERCENT, UIUnitPercentage);
		ui_TextEntrySetFinishedCallback(pTextEntry, cutEdCGTRelPosChanged, pState);
		pState->pUITextRelPosActorName = pTextEntry;
		pState->pInsertIntoContFunc(pState->pRelativePosCont, pTextEntry);
		y = ui_WidgetGetNextY(UI_WIDGET(pTextEntry)) + CSE_UI_GAP;

		pLabel = ui_LabelCreate("Bone Name:", CSE_UI_LEFT_IND, y);
		pState->pInsertIntoContFunc(pState->pRelativePosCont, pLabel);
		pTextEntry = ui_TextEntryCreateWithStringCombo("", 0, y, &pState->eaRelPosBoneNameModel, true, true, false, true);
		ui_WidgetSetPositionEx(UI_WIDGET(pTextEntry), CSE_UI_LARGE_IND, y, CSE_UI_SIDE_PERCENT, 0, UITopLeft);
		ui_WidgetSetWidthEx(UI_WIDGET(pTextEntry), 1.0f-2*CSE_UI_SIDE_PERCENT, UIUnitPercentage);
		ui_TextEntrySetFinishedCallback(pTextEntry, cutEdCGTRelPosChanged, pState);
		pState->pUITextRelPosBoneName = pTextEntry;
		pState->pInsertIntoContFunc(pState->pRelativePosCont, pTextEntry);
		y = ui_WidgetGetNextY(UI_WIDGET(pTextEntry)) + CSE_UI_GAP;


		pCheckButton = ui_CheckButtonCreate(CSE_UI_LEFT_IND, y, "Second Offset", false);
		ui_CheckButtonSetToggledCallback(pCheckButton, cutEdCGTRelPosChanged, pState);
		pState->pUICheckTwoRelPos = pCheckButton;
		pState->pInsertIntoContFunc(pState->pRelativePosCont, pCheckButton);
		y = ui_WidgetGetNextY(UI_WIDGET(pCheckButton)) + CSE_UI_GAP;

		pLabel = ui_LabelCreate("Follow Type 2:", CSE_UI_LEFT_IND, y);
		pState->pInsertIntoContFunc(pState->pRelativePosCont, pLabel);
		pComboBox = ui_ComboBoxCreateWithEnum(0,y,1, CutsceneOffsetTypeEnum, cutEdCGTRelPosChangedComboWrapper, pState);
		ui_WidgetSetPositionEx(UI_WIDGET(pComboBox), CSE_UI_LARGE_IND, y, CSE_UI_SIDE_PERCENT, 0, UITopLeft);
		ui_WidgetSetWidthEx(UI_WIDGET(pComboBox), 1.0f-2*CSE_UI_SIDE_PERCENT, UIUnitPercentage);
		pState->pUIComboRelPosType2 = pComboBox;
		pState->pInsertIntoContFunc(pState->pRelativePosCont, pComboBox);
		y = ui_WidgetGetNextY(UI_WIDGET(pComboBox)) + CSE_UI_GAP;

		pLabel = ui_LabelCreate("Entity Name 2:", CSE_UI_LEFT_IND, y);
		pState->pInsertIntoContFunc(pState->pRelativePosCont, pLabel);
		pTextEntry = ui_TextEntryCreate("", 0, y);
		ui_WidgetSetPositionEx(UI_WIDGET(pTextEntry), CSE_UI_LARGE_IND, y, CSE_UI_SIDE_PERCENT, 0, UITopLeft);
		ui_WidgetSetWidthEx(UI_WIDGET(pTextEntry), 1.0f-2*CSE_UI_SIDE_PERCENT, UIUnitPercentage);
		ui_TextEntrySetFinishedCallback(pTextEntry, cutEdCGTRelPosChanged, pState);
		pState->pUITextRelPosCutsceneEntName2 = pTextEntry;
		pState->pInsertIntoContFunc(pState->pRelativePosCont, pTextEntry);
		y = ui_WidgetGetNextY(UI_WIDGET(pTextEntry)) + CSE_UI_GAP;

		pLabel = ui_LabelCreate("Encounter Name 2:", CSE_UI_LEFT_IND, y);
		pState->pInsertIntoContFunc(pState->pRelativePosCont, pLabel);
		pTextEntry = ui_TextEntryCreateWithStringCombo("", 0, y, &s_eaEncounterList, true, true, false, true);
		ui_WidgetSetPositionEx(UI_WIDGET(pTextEntry), CSE_UI_LARGE_IND, y, CSE_UI_SIDE_PERCENT, 0, UITopLeft);
		ui_WidgetSetWidthEx(UI_WIDGET(pTextEntry), 1.0f-2*CSE_UI_SIDE_PERCENT, UIUnitPercentage);
		ui_TextEntrySetFinishedCallback(pTextEntry, cutEdCGTRelPosChanged, pState);
		pState->pUITextRelPosEncounterName2 = pTextEntry;
		pState->pInsertIntoContFunc(pState->pRelativePosCont, pTextEntry);
		y = ui_WidgetGetNextY(UI_WIDGET(pTextEntry)) + CSE_UI_GAP;

		pLabel = ui_LabelCreate("Actor Name 2:", CSE_UI_LEFT_IND, y);
		pState->pInsertIntoContFunc(pState->pRelativePosCont, pLabel);
		pTextEntry = ui_TextEntryCreateWithStringCombo("", 0, y, &pState->eaRelPosActorNameModel2, true, true, false, true);
		ui_WidgetSetPositionEx(UI_WIDGET(pTextEntry), CSE_UI_LARGE_IND, y, CSE_UI_SIDE_PERCENT, 0, UITopLeft);
		ui_WidgetSetWidthEx(UI_WIDGET(pTextEntry), 1.0f-2*CSE_UI_SIDE_PERCENT, UIUnitPercentage);
		ui_TextEntrySetFinishedCallback(pTextEntry, cutEdCGTRelPosChanged, pState);
		pState->pUITextRelPosActorName2 = pTextEntry;
		pState->pInsertIntoContFunc(pState->pRelativePosCont, pTextEntry);
		y = ui_WidgetGetNextY(UI_WIDGET(pTextEntry)) + CSE_UI_GAP;

		pLabel = ui_LabelCreate("Bone Name 2:", CSE_UI_LEFT_IND, y);
		pState->pInsertIntoContFunc(pState->pRelativePosCont, pLabel);
		pTextEntry = ui_TextEntryCreateWithStringCombo("", 0, y, &pState->eaRelPosBoneNameModel2, true, true, false, true);
		ui_WidgetSetPositionEx(UI_WIDGET(pTextEntry), CSE_UI_LARGE_IND, y, CSE_UI_SIDE_PERCENT, 0, UITopLeft);
		ui_WidgetSetWidthEx(UI_WIDGET(pTextEntry), 1.0f-2*CSE_UI_SIDE_PERCENT, UIUnitPercentage);
		ui_TextEntrySetFinishedCallback(pTextEntry, cutEdCGTRelPosChanged, pState);
		pState->pUITextRelPosBoneName2 = pTextEntry;
		pState->pInsertIntoContFunc(pState->pRelativePosCont, pTextEntry);
		y = ui_WidgetGetNextY(UI_WIDGET(pTextEntry)) + CSE_UI_GAP;
	}
	else
	{
// 		pButton = ui_ButtonCreate("Set Target", CSE_UI_LEFT_IND, y, cutEdSetRelPosTargetCB, pState);
// 		pState->pInsertIntoContFunc(pState->pRelativePosCont, pButton);
// 		pTextEntry = ui_TextEntryCreate("", 0, y);
// 		ui_WidgetSetPositionEx(UI_WIDGET(pTextEntry), CSE_UI_LARGE_IND, y, CSE_UI_SIDE_PERCENT, 0, UITopLeft);
// 		ui_WidgetSetWidthEx(UI_WIDGET(pTextEntry), 1.0f-2*CSE_UI_SIDE_PERCENT, UIUnitPercentage);
// 		ui_SetActive(UI_WIDGET(pTextEntry), false);
// 		pState->pUITextRelPosTarget = pTextEntry;
// 		pState->pInsertIntoContFunc(pState->pRelativePosCont, pTextEntry);
// 		y = ui_WidgetGetNextY(UI_WIDGET(pTextEntry)) + CSE_UI_GAP;
	}
}

static void cutEdMakeGenPntCont(CutsceneEditorState *pState)
{
	int y = 0;
	UIButton *pButton;

	pButton = ui_ButtonCreate("Clone Point", 0, y, cutEdCloneGenPntCB, pState);
	ui_WidgetSetPositionEx(UI_WIDGET(pButton), 0, y, CSE_UI_SIDE_PERCENT, 0, UITopLeft);
	ui_WidgetSetWidthEx(UI_WIDGET(pButton), 1.0f-2*CSE_UI_SIDE_PERCENT, UIUnitPercentage);
	pState->pInsertIntoContFunc(pState->pGenPntCont, pButton);
	y = ui_WidgetGetNextY(UI_WIDGET(pButton)) + CSE_UI_GAP;
}

static void cutEdMakeGenCGTCont(CutsceneEditorState *pState)
{
	int y = 0;
	UILabel *pLabel;
	UITextEntry *pTextEntry;

	pLabel = ui_LabelCreate("Track Name:", CSE_UI_LEFT_IND, y);
	pState->pInsertIntoContFunc(pState->pGenCGTCont, pLabel);
	pTextEntry = ui_TextEntryCreate("", 0, y);
	ui_WidgetSetPositionEx(UI_WIDGET(pTextEntry), CSE_UI_LARGE_IND, y, CSE_UI_SIDE_PERCENT, 0, UITopLeft);
	ui_WidgetSetWidthEx(UI_WIDGET(pTextEntry), 1.0f-2*CSE_UI_SIDE_PERCENT, UIUnitPercentage);
	ui_TextEntrySetFinishedCallback(pTextEntry, cutEdGenCGTChanged, pState);
	pState->pUITextTrackName = pTextEntry;
	pState->pInsertIntoContFunc(pState->pGenCGTCont, pTextEntry);
	y = ui_WidgetGetNextY(UI_WIDGET(pTextEntry)) + CSE_UI_GAP;

}

static void cutEdMakeWatchPathCont(CutsceneEditorState *pState, CutsceneDef *pCutsceneDef)
{
	int y = 0;
	UILabel *pLabel;
	UIButton *pButton;
	UITextEntry *pTextEntry;
	UIComboBox *pComboBox;
	UICheckButton *pCheckButton;

	if(!demo_playingBack())
	{
		pLabel = ui_LabelCreate("Follow Type:", CSE_UI_LEFT_IND, y);
		pState->pInsertIntoContFunc(pState->pWatchPathCont, pLabel);
		pComboBox = ui_ComboBoxCreateWithEnum(0,y,1, CutsceneOffsetTypeEnum, cutEdPathRelPathChangedWrapper, pState);
		ui_WidgetSetPositionEx(UI_WIDGET(pComboBox), CSE_UI_LARGE_IND, y, CSE_UI_SIDE_PERCENT, 0, UITopLeft);
		ui_WidgetSetWidthEx(UI_WIDGET(pComboBox), 1.0f-2*CSE_UI_SIDE_PERCENT, UIUnitPercentage);
		pState->pUIComboWatchType = pComboBox;
		pState->pInsertIntoContFunc(pState->pWatchPathCont, pComboBox);
		y = ui_WidgetGetNextY(UI_WIDGET(pComboBox)) + CSE_UI_GAP;

		pLabel = ui_LabelCreate("Entity Name:", CSE_UI_LEFT_IND, y);
		pState->pInsertIntoContFunc(pState->pWatchPathCont, pLabel);
		pTextEntry = ui_TextEntryCreate("", 0, y);
		ui_WidgetSetPositionEx(UI_WIDGET(pTextEntry), CSE_UI_LARGE_IND, y, CSE_UI_SIDE_PERCENT, 0, UITopLeft);
		ui_WidgetSetWidthEx(UI_WIDGET(pTextEntry), 1.0f-2*CSE_UI_SIDE_PERCENT, UIUnitPercentage);
		ui_TextEntrySetFinishedCallback(pTextEntry, cutEdPathRelPathChanged, pState);
		pState->pUITextWatchCutsceneEntName = pTextEntry;
		pState->pInsertIntoContFunc(pState->pWatchPathCont, pTextEntry);
		y = ui_WidgetGetNextY(UI_WIDGET(pTextEntry)) + CSE_UI_GAP;

		pLabel = ui_LabelCreate("Encounter Name:", CSE_UI_LEFT_IND, y);
		pState->pInsertIntoContFunc(pState->pWatchPathCont, pLabel);
		pTextEntry = ui_TextEntryCreateWithStringCombo("", 0, y, &s_eaEncounterList, true, true, false, true);
		ui_WidgetSetPositionEx(UI_WIDGET(pTextEntry), CSE_UI_LARGE_IND, y, CSE_UI_SIDE_PERCENT, 0, UITopLeft);
		ui_WidgetSetWidthEx(UI_WIDGET(pTextEntry), 1.0f-2*CSE_UI_SIDE_PERCENT, UIUnitPercentage);
		ui_TextEntrySetFinishedCallback(pTextEntry, cutEdPathRelPathChanged, pState);
		pState->pUITextWatchEncounterName = pTextEntry;
		pState->pInsertIntoContFunc(pState->pWatchPathCont, pTextEntry);
		y = ui_WidgetGetNextY(UI_WIDGET(pTextEntry)) + CSE_UI_GAP;

		pLabel = ui_LabelCreate("Actor Name:", CSE_UI_LEFT_IND, y);
		pState->pInsertIntoContFunc(pState->pWatchPathCont, pLabel);
		pTextEntry = ui_TextEntryCreateWithStringCombo("", 0, y, &pState->eaWatchActorNameModel, true, true, false, true);
		ui_WidgetSetPositionEx(UI_WIDGET(pTextEntry), CSE_UI_LARGE_IND, y, CSE_UI_SIDE_PERCENT, 0, UITopLeft);
		ui_WidgetSetWidthEx(UI_WIDGET(pTextEntry), 1.0f-2*CSE_UI_SIDE_PERCENT, UIUnitPercentage);
		ui_TextEntrySetFinishedCallback(pTextEntry, cutEdPathRelPathChanged, pState);
		pState->pUITextWatchActorName = pTextEntry;
		pState->pInsertIntoContFunc(pState->pWatchPathCont, pTextEntry);
		y = ui_WidgetGetNextY(UI_WIDGET(pTextEntry)) + CSE_UI_GAP;

		pLabel = ui_LabelCreate("Bone Name:", CSE_UI_LEFT_IND, y);
		pState->pInsertIntoContFunc(pState->pWatchPathCont, pLabel);
		pTextEntry = ui_TextEntryCreateWithStringCombo("", 0, y, &pState->eaWatchBoneNameModel, true, true, false, true);
		ui_WidgetSetPositionEx(UI_WIDGET(pTextEntry), CSE_UI_LARGE_IND, y, CSE_UI_SIDE_PERCENT, 0, UITopLeft);
		ui_WidgetSetWidthEx(UI_WIDGET(pTextEntry), 1.0f-2*CSE_UI_SIDE_PERCENT, UIUnitPercentage);
		ui_TextEntrySetFinishedCallback(pTextEntry, cutEdPathRelPathChanged, pState);
		pState->pUITextWatchBoneName = pTextEntry;
		pState->pInsertIntoContFunc(pState->pWatchPathCont, pTextEntry);
		y = ui_WidgetGetNextY(UI_WIDGET(pTextEntry)) + CSE_UI_GAP;

		pCheckButton = ui_CheckButtonCreate(CSE_UI_LEFT_IND, y, "Use Two Points", false);
		ui_CheckButtonSetToggledCallback(pCheckButton, cutEdPathRelPathChanged, pState);
		pState->pUICheckPathTwoRelPos = pCheckButton;
		pState->pInsertIntoContFunc(pState->pWatchPathCont, pCheckButton);
		y = ui_WidgetGetNextY(UI_WIDGET(pCheckButton)) + CSE_UI_GAP;

		pLabel = ui_LabelCreate("Follow Type 2:", CSE_UI_LEFT_IND, y);
		pState->pInsertIntoContFunc(pState->pWatchPathCont, pLabel);
		pComboBox = ui_ComboBoxCreateWithEnum(0,y,1, CutsceneOffsetTypeEnum, cutEdPathRelPathChangedWrapper, pState);
		ui_WidgetSetPositionEx(UI_WIDGET(pComboBox), CSE_UI_LARGE_IND, y, CSE_UI_SIDE_PERCENT, 0, UITopLeft);
		ui_WidgetSetWidthEx(UI_WIDGET(pComboBox), 1.0f-2*CSE_UI_SIDE_PERCENT, UIUnitPercentage);
		pState->pUIComboWatchType2 = pComboBox;
		pState->pInsertIntoContFunc(pState->pWatchPathCont, pComboBox);
		y = ui_WidgetGetNextY(UI_WIDGET(pComboBox)) + CSE_UI_GAP;

		pLabel = ui_LabelCreate("Entity Name 2:", CSE_UI_LEFT_IND, y);
		pState->pInsertIntoContFunc(pState->pWatchPathCont, pLabel);
		pTextEntry = ui_TextEntryCreate("", 0, y);
		ui_WidgetSetPositionEx(UI_WIDGET(pTextEntry), CSE_UI_LARGE_IND, y, CSE_UI_SIDE_PERCENT, 0, UITopLeft);
		ui_WidgetSetWidthEx(UI_WIDGET(pTextEntry), 1.0f-2*CSE_UI_SIDE_PERCENT, UIUnitPercentage);
		ui_TextEntrySetFinishedCallback(pTextEntry, cutEdPathRelPathChanged, pState);
		pState->pUITextWatchCutsceneEntName2 = pTextEntry;
		pState->pInsertIntoContFunc(pState->pWatchPathCont, pTextEntry);
		y = ui_WidgetGetNextY(UI_WIDGET(pTextEntry)) + CSE_UI_GAP;

		pLabel = ui_LabelCreate("Encounter Name 2:", CSE_UI_LEFT_IND, y);
		pState->pInsertIntoContFunc(pState->pWatchPathCont, pLabel);
		pTextEntry = ui_TextEntryCreateWithStringCombo("", 0, y, &s_eaEncounterList, true, true, false, true);
		ui_WidgetSetPositionEx(UI_WIDGET(pTextEntry), CSE_UI_LARGE_IND, y, CSE_UI_SIDE_PERCENT, 0, UITopLeft);
		ui_WidgetSetWidthEx(UI_WIDGET(pTextEntry), 1.0f-2*CSE_UI_SIDE_PERCENT, UIUnitPercentage);
		ui_TextEntrySetFinishedCallback(pTextEntry, cutEdPathRelPathChanged, pState);
		pState->pUITextWatchEncounterName2 = pTextEntry;
		pState->pInsertIntoContFunc(pState->pWatchPathCont, pTextEntry);
		y = ui_WidgetGetNextY(UI_WIDGET(pTextEntry)) + CSE_UI_GAP;

		pLabel = ui_LabelCreate("Actor Name 2:", CSE_UI_LEFT_IND, y);
		pState->pInsertIntoContFunc(pState->pWatchPathCont, pLabel);
		pTextEntry = ui_TextEntryCreateWithStringCombo("", 0, y, &pState->eaWatchActorNameModel2, true, true, false, true);
		ui_WidgetSetPositionEx(UI_WIDGET(pTextEntry), CSE_UI_LARGE_IND, y, CSE_UI_SIDE_PERCENT, 0, UITopLeft);
		ui_WidgetSetWidthEx(UI_WIDGET(pTextEntry), 1.0f-2*CSE_UI_SIDE_PERCENT, UIUnitPercentage);
		ui_TextEntrySetFinishedCallback(pTextEntry, cutEdPathRelPathChanged, pState);
		pState->pUITextWatchActorName2 = pTextEntry;
		pState->pInsertIntoContFunc(pState->pWatchPathCont, pTextEntry);
		y = ui_WidgetGetNextY(UI_WIDGET(pTextEntry)) + CSE_UI_GAP;

		pLabel = ui_LabelCreate("Bone Name 2:", CSE_UI_LEFT_IND, y);
		pState->pInsertIntoContFunc(pState->pWatchPathCont, pLabel);
		pTextEntry = ui_TextEntryCreateWithStringCombo("", 0, y, &pState->eaWatchBoneNameModel2, true, true, false, true);
		ui_WidgetSetPositionEx(UI_WIDGET(pTextEntry), CSE_UI_LARGE_IND, y, CSE_UI_SIDE_PERCENT, 0, UITopLeft);
		ui_WidgetSetWidthEx(UI_WIDGET(pTextEntry), 1.0f-2*CSE_UI_SIDE_PERCENT, UIUnitPercentage);
		ui_TextEntrySetFinishedCallback(pTextEntry, cutEdPathRelPathChanged, pState);
		pState->pUITextWatchBoneName2 = pTextEntry;
		pState->pInsertIntoContFunc(pState->pWatchPathCont, pTextEntry);
		y = ui_WidgetGetNextY(UI_WIDGET(pTextEntry)) + CSE_UI_GAP;
	}
	else
	{
		pButton = ui_ButtonCreate("Set Target", CSE_UI_LEFT_IND, y, cutEdSetTargetCB, pState);
		pState->pInsertIntoContFunc(pState->pWatchPathCont, pButton);
		pTextEntry = ui_TextEntryCreate("", 0, y);
		ui_WidgetSetPositionEx(UI_WIDGET(pTextEntry), CSE_UI_LARGE_IND, y, CSE_UI_SIDE_PERCENT, 0, UITopLeft);
		ui_WidgetSetWidthEx(UI_WIDGET(pTextEntry), 1.0f-2*CSE_UI_SIDE_PERCENT, UIUnitPercentage);
		ui_SetActive(UI_WIDGET(pTextEntry), false);
		pState->pUITextWatchTarget = pTextEntry;
		pState->pInsertIntoContFunc(pState->pWatchPathCont, pTextEntry);
		y = ui_WidgetGetNextY(UI_WIDGET(pTextEntry)) + CSE_UI_GAP;
	}
}

static void cutEdMakePointCont(CutsceneEditorState *pState, CutsceneDef *pCutsceneDef)
{
	int y = 0;
	UILabel *pLabel;
	UITextEntry *pTextEntry;
	UICheckButton *pCheckButton;
	UIButton *pButton;

	pLabel = ui_LabelCreate("Position: ", CSE_UI_LEFT_IND, y);
	pState->pInsertIntoContFunc(pState->pPointCont, pLabel);
	y = ui_WidgetGetNextY(UI_WIDGET(pLabel));

	pTextEntry = ui_TextEntryCreate("0.0", 0, y);
	ui_WidgetSetPositionEx(UI_WIDGET(pTextEntry), 0, y, CSE_UI_SIDE_PERCENT, 0, UITopLeft);
	ui_WidgetSetWidthEx(UI_WIDGET(pTextEntry), 1.0f-4*CSE_UI_SIDE_PERCENT-0.6f, UIUnitPercentage);
	ui_WidgetSkin(UI_WIDGET(pTextEntry), pState->pSkinRed);
	ui_TextEntrySetFinishedCallback(pTextEntry, cutEdPointUIChangedCB, pState);
	pState->pUITextPointXPos = pTextEntry;
	pState->pInsertIntoContFunc(pState->pPointCont, pTextEntry);
	pTextEntry = ui_TextEntryCreate("0.0", 0, y);
	ui_WidgetSetPositionEx(UI_WIDGET(pTextEntry), 0, y, 2*CSE_UI_SIDE_PERCENT+0.3f, 0, UITopLeft);
	ui_WidgetSetWidthEx(UI_WIDGET(pTextEntry), 1.0f-4*CSE_UI_SIDE_PERCENT-0.6f, UIUnitPercentage);
	ui_WidgetSkin(UI_WIDGET(pTextEntry), pState->pSkinGreen);
	ui_TextEntrySetFinishedCallback(pTextEntry, cutEdPointUIChangedCB, pState);
	pState->pUITextPointYPos = pTextEntry;
	pState->pInsertIntoContFunc(pState->pPointCont, pTextEntry);
	pTextEntry = ui_TextEntryCreate("0.0", 0, y);
	ui_WidgetSetPositionEx(UI_WIDGET(pTextEntry), 0, y, 3*CSE_UI_SIDE_PERCENT+0.6f, 0, UITopLeft);
	ui_WidgetSetWidthEx(UI_WIDGET(pTextEntry), 1.0f-4*CSE_UI_SIDE_PERCENT-0.6f, UIUnitPercentage);
	ui_WidgetSkin(UI_WIDGET(pTextEntry), pState->pSkinBlue);
	ui_TextEntrySetFinishedCallback(pTextEntry, cutEdPointUIChangedCB, pState);
	pState->pUITextPointZPos = pTextEntry;
	pState->pInsertIntoContFunc(pState->pPointCont, pTextEntry);
	y = ui_WidgetGetNextY(UI_WIDGET(pTextEntry)) + CSE_UI_GAP;

	pLabel = ui_LabelCreate("Rotation: ", CSE_UI_LEFT_IND, y);
	pState->pInsertIntoContFunc(pState->pPointCont, pLabel);
	y = ui_WidgetGetNextY(UI_WIDGET(pLabel));

	pTextEntry = ui_TextEntryCreate("0.0", 0, y);
	ui_WidgetSetPositionEx(UI_WIDGET(pTextEntry), 0, y, CSE_UI_SIDE_PERCENT, 0, UITopLeft);
	ui_WidgetSetWidthEx(UI_WIDGET(pTextEntry), 1.0f-4*CSE_UI_SIDE_PERCENT-0.6f, UIUnitPercentage);
	ui_WidgetSkin(UI_WIDGET(pTextEntry), pState->pSkinRed);
	ui_TextEntrySetFinishedCallback(pTextEntry, cutEdPointUIChangedCB, pState);
	pState->pUITextPointPRot = pTextEntry;
	pState->pInsertIntoContFunc(pState->pPointCont, pTextEntry);
	pTextEntry = ui_TextEntryCreate("0.0", 0, y);
	ui_WidgetSetPositionEx(UI_WIDGET(pTextEntry), 0, y, 2*CSE_UI_SIDE_PERCENT+0.3f, 0, UITopLeft);
	ui_WidgetSetWidthEx(UI_WIDGET(pTextEntry), 1.0f-4*CSE_UI_SIDE_PERCENT-0.6f, UIUnitPercentage);
	ui_WidgetSkin(UI_WIDGET(pTextEntry), pState->pSkinGreen);
	ui_TextEntrySetFinishedCallback(pTextEntry, cutEdPointUIChangedCB, pState);
	pState->pUITextPointYRot = pTextEntry;
	pState->pInsertIntoContFunc(pState->pPointCont, pTextEntry);
	y = ui_WidgetGetNextY(UI_WIDGET(pTextEntry)) + CSE_UI_GAP;

	pLabel = ui_LabelCreate("Time:", CSE_UI_LEFT_IND, y);
	pState->pInsertIntoContFunc(pState->pPointCont, pLabel);
	pTextEntry = ui_TextEntryCreate("0.0", 0, y);
	ui_WidgetSetPositionEx(UI_WIDGET(pTextEntry), CSE_UI_LARGE_IND, y, CSE_UI_SIDE_PERCENT, 0, UITopLeft);
	ui_WidgetSetWidthEx(UI_WIDGET(pTextEntry), 1.0f-2*CSE_UI_SIDE_PERCENT, UIUnitPercentage);
	ui_TextEntrySetFinishedCallback(pTextEntry, cutEdPointUIChangedCB, pState);
	pState->pUITextTime = pTextEntry;
	pState->pInsertIntoContFunc(pState->pPointCont, pTextEntry);
	y = ui_WidgetGetNextY(UI_WIDGET(pTextEntry)) + CSE_UI_GAP;

	pLabel = ui_LabelCreate("Hold:", CSE_UI_LEFT_IND, y);
	pState->pInsertIntoContFunc(pState->pPointCont, pLabel);
	pTextEntry = ui_TextEntryCreate("0.0", 0, y);
	ui_WidgetSetPositionEx(UI_WIDGET(pTextEntry), CSE_UI_LARGE_IND, y, CSE_UI_SIDE_PERCENT, 0, UITopLeft);
	ui_WidgetSetWidthEx(UI_WIDGET(pTextEntry), 1.0f-2*CSE_UI_SIDE_PERCENT, UIUnitPercentage);
	ui_TextEntrySetFinishedCallback(pTextEntry, cutEdPointUIChangedCB, pState);
	pState->pUITextLength = pTextEntry;
	pState->pInsertIntoContFunc(pState->pPointCont, pTextEntry);
	y = ui_WidgetGetNextY(UI_WIDGET(pTextEntry)) + CSE_UI_GAP;

	pCheckButton = ui_CheckButtonCreate(CSE_UI_LEFT_IND, y, "Ease In", false);
	ui_CheckButtonSetToggledCallback(pCheckButton, cutEdPointUIChangedCB, pState);
	pState->pUICheckEaseIn = pCheckButton;
	pState->pInsertIntoContFunc(pState->pPointCont, pCheckButton);
	y = ui_WidgetGetNextY(UI_WIDGET(pCheckButton)) + CSE_UI_GAP;

	pCheckButton = ui_CheckButtonCreate(CSE_UI_LEFT_IND, y, "Ease Out", false);
	ui_CheckButtonSetToggledCallback(pCheckButton, cutEdPointUIChangedCB, pState);
	pState->pUICheckEaseOut = pCheckButton;
	pState->pInsertIntoContFunc(pState->pPointCont, pCheckButton);
	y = ui_WidgetGetNextY(UI_WIDGET(pCheckButton)) + CSE_UI_GAP;

	pButton = ui_ButtonCreate("Move To Point", 0, y, cutEdMoveToPointCB, pState);
	ui_WidgetSetPositionEx(UI_WIDGET(pButton), 0, y, CSE_UI_SIDE_PERCENT, 0, UITopLeft);
	ui_WidgetSetWidthEx(UI_WIDGET(pButton), 1.0f-2*CSE_UI_SIDE_PERCENT, UIUnitPercentage);
	pState->pInsertIntoContFunc(pState->pPointCont, pButton);
	y = ui_WidgetGetNextY(UI_WIDGET(pButton)) + CSE_UI_GAP;

	pButton = ui_ButtonCreate("Reset Point", 0, y, cutEdResetPointCB, pState);
	ui_WidgetSetPositionEx(UI_WIDGET(pButton), 0, y, CSE_UI_SIDE_PERCENT, 0, UITopLeft);
	ui_WidgetSetWidthEx(UI_WIDGET(pButton), 1.0f-2*CSE_UI_SIDE_PERCENT, UIUnitPercentage);
	pState->pInsertIntoContFunc(pState->pPointCont, pButton);
	y = ui_WidgetGetNextY(UI_WIDGET(pButton)) + CSE_UI_GAP;

	pButton = ui_ButtonCreate("Clone Point", 0, y, cutEdClonePointCB, pState);
	ui_WidgetSetPositionEx(UI_WIDGET(pButton), 0, y, CSE_UI_SIDE_PERCENT, 0, UITopLeft);
	ui_WidgetSetWidthEx(UI_WIDGET(pButton), 1.0f-2*CSE_UI_SIDE_PERCENT, UIUnitPercentage);
	pState->pInsertIntoContFunc(pState->pPointCont, pButton);
}

static void cutEdFadeChangedWrapperCB(void *unused, bool bFinished, CutsceneGenericTrackInfo *pInfo)
{
	cutEdFadeChangedCB(unused, pInfo);
}
static void cutEdMakeFadeCont(CutsceneEditorState *pState, CutsceneGenericTrackInfo *pInfo, void *pCont)
{
	int y = 0;
	UILabel *pLabel;
	UITextEntry *pTextEntry;
	UIColorButton *pColorButton;
	UICheckButton *pCheckButton;

	pLabel = ui_LabelCreate("Fade:", CSE_UI_LEFT_IND, y);
	pState->pInsertIntoContFunc(pCont, pLabel);
	pTextEntry = ui_TextEntryCreate("", 0, y);
	ui_WidgetSetPositionEx(UI_WIDGET(pTextEntry), CSE_UI_LARGE_IND, y, CSE_UI_SIDE_PERCENT, 0, UITopLeft);
	ui_WidgetSetWidthEx(UI_WIDGET(pTextEntry), 0.65f, UIUnitPercentage);
	ui_TextEntrySetFinishedCallback(pTextEntry, cutEdFadeChangedCB, pInfo);
	pState->pUITextFadeAmount = pTextEntry;
	pState->pInsertIntoContFunc(pCont, pTextEntry);
	pColorButton = ui_ColorButtonCreate(0, y, zerovec4);
	ui_WidgetSetPositionEx(UI_WIDGET(pColorButton), CSE_UI_LEFT_IND, y, CSE_UI_SIDE_PERCENT, 0, UITopRight);
	ui_ColorButtonSetChangedCallback(pColorButton, cutEdFadeChangedWrapperCB, pInfo);
	pColorButton->noAlpha = 1;
	pColorButton->noHSV = 1;
	pColorButton->min = 0;
	pColorButton->max = 1;
	pState->pUIColorFade = pColorButton;
	pState->pInsertIntoContFunc(pCont, pColorButton);
	y = ui_WidgetGetNextY(UI_WIDGET(pTextEntry)) + CSE_UI_GAP;

	pCheckButton = ui_CheckButtonCreate(CSE_UI_LEFT_IND, y, "Additive", false);
	ui_CheckButtonSetToggledCallback(pCheckButton, cutEdFadeChangedCB, pInfo);
	pState->pUIFadeAdditive = pCheckButton;
	pState->pInsertIntoContFunc(pCont, pCheckButton);
	y = ui_WidgetGetNextY(UI_WIDGET(pCheckButton)) + CSE_UI_GAP;
}

static void cutEdMakeDOFCont(CutsceneEditorState *pState, CutsceneGenericTrackInfo *pInfo, void *pCont)
{
	int y = 0;
	UILabel *pLabel;
	UITextEntry *pTextEntry;
	UICheckButton *pCheckButton;

	pCheckButton = ui_CheckButtonCreate(CSE_UI_LEFT_IND, y, "Use Depth of Field", false);
	ui_CheckButtonSetToggledCallback(pCheckButton, cutEdDOFChangedCB, pInfo);
	pState->pUICheckPointUseDoF = pCheckButton;
	pState->pInsertIntoContFunc(pCont, pCheckButton);
	y = ui_WidgetGetNextY(UI_WIDGET(pCheckButton)) + CSE_UI_GAP;

	pLabel = ui_LabelCreate("Blur Amount:", CSE_UI_LEFT_IND, y);
	pState->pInsertIntoContFunc(pCont, pLabel);
	pTextEntry = ui_TextEntryCreate("0.0", 0, y);
	ui_WidgetSetPositionEx(UI_WIDGET(pTextEntry), CSE_UI_LARGE_IND, y, CSE_UI_SIDE_PERCENT, 0, UITopLeft);
	ui_WidgetSetWidthEx(UI_WIDGET(pTextEntry), 1.0f-2*CSE_UI_SIDE_PERCENT, UIUnitPercentage);
	ui_TextEntrySetFinishedCallback(pTextEntry, cutEdDOFChangedCB, pInfo);
	pState->pUITextPointDoFBlur = pTextEntry;
	pState->pInsertIntoContFunc(pCont, pTextEntry);
	y = ui_WidgetGetNextY(UI_WIDGET(pTextEntry)) + CSE_UI_GAP;

	pLabel = ui_LabelCreate("Blur Distance:", CSE_UI_LEFT_IND, y);
	pState->pInsertIntoContFunc(pCont, pLabel);
	pTextEntry = ui_TextEntryCreate("0.0", 0, y);
	ui_WidgetSetPositionEx(UI_WIDGET(pTextEntry), CSE_UI_LARGE_IND, y, CSE_UI_SIDE_PERCENT, 0, UITopLeft);
	ui_WidgetSetWidthEx(UI_WIDGET(pTextEntry), 1.0f-2*CSE_UI_SIDE_PERCENT, UIUnitPercentage);
	ui_TextEntrySetFinishedCallback(pTextEntry, cutEdDOFChangedCB, pInfo);
	pState->pUITextPointDoFDist = pTextEntry;
	pState->pInsertIntoContFunc(pCont, pTextEntry);
	y = ui_WidgetGetNextY(UI_WIDGET(pTextEntry)) + CSE_UI_GAP;
}

static void cutEdMakeFOVCont(CutsceneEditorState *pState, CutsceneGenericTrackInfo *pInfo, void *pCont)
{
	int y = 0;
	UILabel *pLabel;
	UITextEntry *pTextEntry;

	pLabel = ui_LabelCreate("Field of View:", CSE_UI_LEFT_IND, y);
	pState->pInsertIntoContFunc(pCont, pLabel);
	pTextEntry = ui_TextEntryCreate("0.0", 0, y);
	ui_WidgetSetPositionEx(UI_WIDGET(pTextEntry), CSE_UI_LARGE_IND, y, CSE_UI_SIDE_PERCENT, 0, UITopLeft);
	ui_WidgetSetWidthEx(UI_WIDGET(pTextEntry), 1.0f-2*CSE_UI_SIDE_PERCENT, UIUnitPercentage);
	ui_TextEntrySetFinishedCallback(pTextEntry, cutEdFOVChangedCB, pInfo);
	pState->pUITextFOV = pTextEntry;
	pState->pInsertIntoContFunc(pCont, pTextEntry);
	y = ui_WidgetGetNextY(UI_WIDGET(pTextEntry)) + CSE_UI_GAP;
}

static void cutEdMakeShakeCont(CutsceneEditorState *pState, CutsceneGenericTrackInfo *pInfo, void *pCont)
{
	int y = 0;
	UILabel *pLabel;
	UITextEntry *pTextEntry;

	pLabel = ui_LabelCreate("Time:", CSE_UI_LEFT_IND, y);
	pState->pInsertIntoContFunc(pCont, pLabel);
	pTextEntry = ui_TextEntryCreate("0.0", 0, y);
	ui_WidgetSetPositionEx(UI_WIDGET(pTextEntry), CSE_UI_LARGE_IND, y, CSE_UI_SIDE_PERCENT, 0, UITopLeft);
	ui_WidgetSetWidthEx(UI_WIDGET(pTextEntry), 1.0f-2*CSE_UI_SIDE_PERCENT, UIUnitPercentage);
	ui_TextEntrySetFinishedCallback(pTextEntry, cutEdShakeChangedCB, pInfo);
	pState->pUIShakeTime = pTextEntry;
	pState->pInsertIntoContFunc(pCont, pTextEntry);
	y = ui_WidgetGetNextY(UI_WIDGET(pTextEntry)) + CSE_UI_GAP;

	pLabel = ui_LabelCreate("Length:", CSE_UI_LEFT_IND, y);
	pState->pInsertIntoContFunc(pCont, pLabel);
	pTextEntry = ui_TextEntryCreate("0.0", 0, y);
	ui_WidgetSetPositionEx(UI_WIDGET(pTextEntry), CSE_UI_LARGE_IND, y, CSE_UI_SIDE_PERCENT, 0, UITopLeft);
	ui_WidgetSetWidthEx(UI_WIDGET(pTextEntry), 1.0f-2*CSE_UI_SIDE_PERCENT, UIUnitPercentage);
	ui_TextEntrySetFinishedCallback(pTextEntry, cutEdShakeChangedCB, pInfo);
	pState->pUIShakeLength = pTextEntry;
	pState->pInsertIntoContFunc(pCont, pTextEntry);
	y = ui_WidgetGetNextY(UI_WIDGET(pTextEntry)) + CSE_UI_GAP;

	pLabel = ui_LabelCreate("Magnitude:", CSE_UI_LEFT_IND, y);
	pState->pInsertIntoContFunc(pCont, pLabel);
	pTextEntry = ui_TextEntryCreate("0.0", 0, y);
	ui_WidgetSetPositionEx(UI_WIDGET(pTextEntry), CSE_UI_LARGE_IND, y, CSE_UI_SIDE_PERCENT, 0, UITopLeft);
	ui_WidgetSetWidthEx(UI_WIDGET(pTextEntry), 1.0f-2*CSE_UI_SIDE_PERCENT, UIUnitPercentage);
	ui_TextEntrySetFinishedCallback(pTextEntry, cutEdShakeChangedCB, pInfo);
	pState->pUIShakeMagnitude = pTextEntry;
	pState->pInsertIntoContFunc(pCont, pTextEntry);
	y = ui_WidgetGetNextY(UI_WIDGET(pTextEntry)) + CSE_UI_GAP;

	pLabel = ui_LabelCreate("Direction:", CSE_UI_LEFT_IND, y);
	pState->pInsertIntoContFunc(pCont, pLabel);
	pTextEntry = ui_TextEntryCreate("0.0", 0, y);
	ui_WidgetSetPositionEx(UI_WIDGET(pTextEntry), CSE_UI_LARGE_IND, y, CSE_UI_SIDE_PERCENT, 0, UITopLeft);
	ui_WidgetSetWidthEx(UI_WIDGET(pTextEntry), 1.0f-2*CSE_UI_SIDE_PERCENT, UIUnitPercentage);
	ui_TextEntrySetFinishedCallback(pTextEntry, cutEdShakeChangedCB, pInfo);
	ui_WidgetSetTooltipString(UI_WIDGET(pTextEntry), "-1 is just horizontal.<br>1 is just vertical.");
	pState->pUIShakeVertical = pTextEntry;
	pState->pInsertIntoContFunc(pCont, pTextEntry);
	y = ui_WidgetGetNextY(UI_WIDGET(pTextEntry)) + CSE_UI_GAP;

	pLabel = ui_LabelCreate("Motion:", CSE_UI_LEFT_IND, y);
	pState->pInsertIntoContFunc(pCont, pLabel);
	pTextEntry = ui_TextEntryCreate("0.0", 0, y);
	ui_WidgetSetPositionEx(UI_WIDGET(pTextEntry), CSE_UI_LARGE_IND, y, CSE_UI_SIDE_PERCENT, 0, UITopLeft);
	ui_WidgetSetWidthEx(UI_WIDGET(pTextEntry), 1.0f-2*CSE_UI_SIDE_PERCENT, UIUnitPercentage);
	ui_TextEntrySetFinishedCallback(pTextEntry, cutEdShakeChangedCB, pInfo);
	ui_WidgetSetTooltipString(UI_WIDGET(pTextEntry), "0 is just rotation.<br>1 is just position.");
	pState->pUIShakePan = pTextEntry;
	pState->pInsertIntoContFunc(pCont, pTextEntry);
	y = ui_WidgetGetNextY(UI_WIDGET(pTextEntry)) + CSE_UI_GAP;
}

static bool cutEdObjectPickerCB(EMPicker *picker, EMPickerSelection **selections, CutsceneGenericTrackInfo *pInfo)
{
	GroupDef *child_def;
	ResourceInfo *in_data;
	EMPickerSelection *selection;
	CutsceneEditorState *pState = pInfo->pState;
	CutsceneObjectPoint *pPoint = CSE_GetSelected(pInfo->ppSelected);
	CutsceneObjectList *pList = CSE_GetSelectedList(pInfo->ppSelected);

	if(!cutEdIsEditable(pState))
		return false;

	if(!pPoint)
		return false;

	if(eaSize(&selections) != 1)
		return false;

	selection = selections[0];
	if(selection->table != parse_ResourceInfo)
		return false;
	in_data = selection->data;

	child_def = objectLibraryGetGroupDefFromResource(in_data, false);

	if(!child_def || !child_def->name_str)
		return false;

	if(pList->pcObjectName)
		StructFreeString(pList->pcObjectName);
	pList->pcObjectName = StructAllocString(child_def->name_str);
	pList->iObjectID = in_data->resourceID;

	cutEdRefreshUICommon(pState);
	cutEdSetUnsaved(pState);
	return true;	
}

static void cutEdObjectButtonCB(void *unused, CutsceneGenericTrackInfo *pInfo)
{
	EMPicker *picker;
	picker = emPickerGetByName("Object Picker");
	if(!picker)
		return;

	emPickerShow(picker, "Select", false, cutEdObjectPickerCB, pInfo);
}

static void cutEdMakeObjectCont(CutsceneEditorState *pState, CutsceneGenericTrackInfo *pInfo, void *pCont)
{
	int y = 0;
	UILabel *pLabel;
	UITextEntry *pTextEntry;
	UIButton *pButton;

	pLabel = ui_LabelCreate("Track Data:", CSE_UI_LEFT_IND, y);
	pState->pInsertIntoContFunc(pCont, pLabel);
	y = ui_WidgetGetNextY(UI_WIDGET(pLabel));

	pLabel = ui_LabelCreate("Direction:", CSE_UI_LEFT_IND, y);
	pState->pInsertIntoContFunc(pCont, pLabel);
	pButton = ui_ButtonCreate("", 0, y, cutEdObjectButtonCB, pInfo);
	ui_WidgetSetPositionEx(UI_WIDGET(pButton), CSE_UI_LARGE_IND, y, CSE_UI_SIDE_PERCENT, 0, UITopLeft);
	ui_WidgetSetWidthEx(UI_WIDGET(pButton), 1.0f-2*CSE_UI_SIDE_PERCENT, UIUnitPercentage);
	pState->pUIObjectPickerButton = pButton;
	pState->pInsertIntoContFunc(pCont, pButton);
	y = ui_WidgetGetNextY(UI_WIDGET(pButton)) + CSE_UI_GAP;	

	y += UI_DSTEP;
	pLabel = ui_LabelCreate("Point Data:", CSE_UI_LEFT_IND, y);
	pState->pInsertIntoContFunc(pCont, pLabel);
	y = ui_WidgetGetNextY(UI_WIDGET(pLabel));

	pLabel = ui_LabelCreate("Alpha:", CSE_UI_LEFT_IND, y);
	pState->pInsertIntoContFunc(pCont, pLabel);
	pTextEntry = ui_TextEntryCreate("0.0", 0, y);
	ui_WidgetSetPositionEx(UI_WIDGET(pTextEntry), CSE_UI_LARGE_IND, y, CSE_UI_SIDE_PERCENT, 0, UITopLeft);
	ui_WidgetSetWidthEx(UI_WIDGET(pTextEntry), 1.0f-2*CSE_UI_SIDE_PERCENT, UIUnitPercentage);
	ui_TextEntrySetFinishedCallback(pTextEntry, cutEdObjectChangedCB, pInfo);
	pState->pUIObjectAlpha = pTextEntry;
	pState->pInsertIntoContFunc(pCont, pTextEntry);
	y = ui_WidgetGetNextY(UI_WIDGET(pTextEntry)) + CSE_UI_GAP;

	pLabel = ui_LabelCreate("Position: ", CSE_UI_LEFT_IND, y);
	pState->pInsertIntoContFunc(pCont, pLabel);
	y = ui_WidgetGetNextY(UI_WIDGET(pLabel));

	pTextEntry = ui_TextEntryCreate("0.0", 0, y);
	ui_WidgetSetPositionEx(UI_WIDGET(pTextEntry), 0, y, CSE_UI_SIDE_PERCENT, 0, UITopLeft);
	ui_WidgetSetWidthEx(UI_WIDGET(pTextEntry), 1.0f-4*CSE_UI_SIDE_PERCENT-0.6f, UIUnitPercentage);
	ui_WidgetSkin(UI_WIDGET(pTextEntry), pState->pSkinRed);
	ui_TextEntrySetFinishedCallback(pTextEntry, cutEdObjectChangedCB, pInfo);
	pState->pUIObjectXPos = pTextEntry;
	pState->pInsertIntoContFunc(pCont, pTextEntry);
	pTextEntry = ui_TextEntryCreate("0.0", 0, y);
	ui_WidgetSetPositionEx(UI_WIDGET(pTextEntry), 0, y, 2*CSE_UI_SIDE_PERCENT+0.3f, 0, UITopLeft);
	ui_WidgetSetWidthEx(UI_WIDGET(pTextEntry), 1.0f-4*CSE_UI_SIDE_PERCENT-0.6f, UIUnitPercentage);
	ui_WidgetSkin(UI_WIDGET(pTextEntry), pState->pSkinGreen);
	ui_TextEntrySetFinishedCallback(pTextEntry, cutEdObjectChangedCB, pInfo);
	pState->pUIObjectYPos = pTextEntry;
	pState->pInsertIntoContFunc(pCont, pTextEntry);
	pTextEntry = ui_TextEntryCreate("0.0", 0, y);
	ui_WidgetSetPositionEx(UI_WIDGET(pTextEntry), 0, y, 3*CSE_UI_SIDE_PERCENT+0.6f, 0, UITopLeft);
	ui_WidgetSetWidthEx(UI_WIDGET(pTextEntry), 1.0f-4*CSE_UI_SIDE_PERCENT-0.6f, UIUnitPercentage);
	ui_WidgetSkin(UI_WIDGET(pTextEntry), pState->pSkinBlue);
	ui_TextEntrySetFinishedCallback(pTextEntry, cutEdObjectChangedCB, pInfo);
	pState->pUIObjectZPos = pTextEntry;
	pState->pInsertIntoContFunc(pCont, pTextEntry);
	y = ui_WidgetGetNextY(UI_WIDGET(pTextEntry)) + CSE_UI_GAP;

	pLabel = ui_LabelCreate("Rotation: ", CSE_UI_LEFT_IND, y);
	pState->pInsertIntoContFunc(pCont, pLabel);
	y = ui_WidgetGetNextY(UI_WIDGET(pLabel));

	pTextEntry = ui_TextEntryCreate("0.0", 0, y);
	ui_WidgetSetPositionEx(UI_WIDGET(pTextEntry), 0, y, CSE_UI_SIDE_PERCENT, 0, UITopLeft);
	ui_WidgetSetWidthEx(UI_WIDGET(pTextEntry), 1.0f-4*CSE_UI_SIDE_PERCENT-0.6f, UIUnitPercentage);
	ui_WidgetSkin(UI_WIDGET(pTextEntry), pState->pSkinRed);
	ui_TextEntrySetFinishedCallback(pTextEntry, cutEdObjectChangedCB, pInfo);
	pState->pUIObjectPRot = pTextEntry;
	pState->pInsertIntoContFunc(pCont, pTextEntry);
	pTextEntry = ui_TextEntryCreate("0.0", 0, y);
	ui_WidgetSetPositionEx(UI_WIDGET(pTextEntry), 0, y, 2*CSE_UI_SIDE_PERCENT+0.3f, 0, UITopLeft);
	ui_WidgetSetWidthEx(UI_WIDGET(pTextEntry), 1.0f-4*CSE_UI_SIDE_PERCENT-0.6f, UIUnitPercentage);
	ui_WidgetSkin(UI_WIDGET(pTextEntry), pState->pSkinGreen);
	ui_TextEntrySetFinishedCallback(pTextEntry, cutEdObjectChangedCB, pInfo);
	pState->pUIObjectYRot = pTextEntry;
	pState->pInsertIntoContFunc(pCont, pTextEntry);
	pTextEntry = ui_TextEntryCreate("0.0", 0, y);
	ui_WidgetSetPositionEx(UI_WIDGET(pTextEntry), 0, y, 3*CSE_UI_SIDE_PERCENT+0.6f, 0, UITopLeft);
	ui_WidgetSetWidthEx(UI_WIDGET(pTextEntry), 1.0f-4*CSE_UI_SIDE_PERCENT-0.6f, UIUnitPercentage);
	ui_WidgetSkin(UI_WIDGET(pTextEntry), pState->pSkinBlue);
	ui_TextEntrySetFinishedCallback(pTextEntry, cutEdObjectChangedCB, pInfo);
	pState->pUIObjectRRot = pTextEntry;
	pState->pInsertIntoContFunc(pCont, pTextEntry);
	y = ui_WidgetGetNextY(UI_WIDGET(pTextEntry)) + CSE_UI_GAP;
}

static void cutEdEntityChangedComboWrapper(void *unused, int i, CutsceneGenericTrackInfo *pInfo)
{
	cutEdEntityChangedCB(NULL, pInfo);
}

static void cutEdMakeEntityCont(CutsceneEditorState *pState, CutsceneGenericTrackInfo *pInfo, void *pCont)
{
	int y = 0;
	UILabel *pLabel;
	UITextEntry *pTextEntry;
	UIComboBox *pComboBox;
	UICheckButton *pCheckButton;

	pLabel = ui_LabelCreate("Track Data:", CSE_UI_LEFT_IND, y);
	pState->pInsertIntoContFunc(pCont, pLabel);
	y = ui_WidgetGetNextY(UI_WIDGET(pLabel));

	pLabel = ui_LabelCreate("Entity Type:", CSE_UI_LEFT_IND, y);
	pState->pInsertIntoContFunc(pCont, pLabel);
	pComboBox = ui_ComboBoxCreateWithEnum(0,y,1, CutsceneEntityTypeEnum, cutEdEntityChangedComboWrapper, pInfo);
	ui_WidgetSetPositionEx(UI_WIDGET(pComboBox), CSE_UI_LARGE_IND, y, CSE_UI_SIDE_PERCENT, 0, UITopLeft);
	ui_WidgetSetWidthEx(UI_WIDGET(pComboBox), 1.0f-2*CSE_UI_SIDE_PERCENT, UIUnitPercentage);
	pState->pUIEntityType = pComboBox;
	pState->pInsertIntoContFunc(pCont, pComboBox);
	y = ui_WidgetGetNextY(UI_WIDGET(pComboBox)) + CSE_UI_GAP;

	pLabel = ui_LabelCreate("Costume:", CSE_UI_LEFT_IND, y);
	pState->pInsertIntoContFunc(pCont, pLabel);
	if(demo_playingBack()) {
		pComboBox = ui_ComboBoxCreateWithGlobalDictionary(0, 0, 0, g_hPlayerCostumeDict, "resourceName");
	} else {
		resRequestAllResourcesInDictionary(g_hPlayerCostumeDict);
		pComboBox = ui_FilteredComboBoxCreateWithGlobalDictionary(0, 0, 0, g_hPlayerCostumeDict, "resourceName");
	}
	ui_ComboBoxSetSelectedCallback(pComboBox, cutEdEntityChangedCB, pInfo);
	ui_WidgetSetPositionEx(UI_WIDGET(pComboBox), CSE_UI_LARGE_IND, y, CSE_UI_SIDE_PERCENT, 0, UITopLeft);
	ui_WidgetSetWidthEx(UI_WIDGET(pComboBox), 1.0f-2*CSE_UI_SIDE_PERCENT, UIUnitPercentage);
	pState->pUIEntityCostume = pComboBox;
	pState->pInsertIntoContFunc(pCont, pComboBox);
	y = ui_WidgetGetNextY(UI_WIDGET(pComboBox)) + CSE_UI_GAP;

	pCheckButton = ui_CheckButtonCreate(CSE_UI_LEFT_IND, y, "Preserve Movement FX", true);
	ui_CheckButtonSetToggledCallback(pCheckButton, cutEdEntityChangedCB, pInfo);
	pState->pUIEntityPreserveMovementFX = pCheckButton;
	pState->pInsertIntoContFunc(pCont, pCheckButton);
	y = ui_WidgetGetNextY(UI_WIDGET(pCheckButton)) + CSE_UI_GAP;

	pLabel = ui_LabelCreate("Character Class:", CSE_UI_LEFT_IND, y);
	pState->pInsertIntoContFunc(pCont, pLabel);
	pComboBox = ui_ComboBoxCreateWithEnum(0,y,1, CharClassTypesEnum, cutEdEntityChangedComboWrapper, pInfo);
	ui_WidgetSetPositionEx(UI_WIDGET(pComboBox), CSE_UI_LARGE_IND, y, CSE_UI_SIDE_PERCENT, 0, UITopLeft);
	ui_WidgetSetWidthEx(UI_WIDGET(pComboBox), 1.0f-2*CSE_UI_SIDE_PERCENT, UIUnitPercentage);
	ui_WidgetSetTooltipString(UI_WIDGET(pComboBox), "Currently only used by STO to force the ground or space costume.");
	pState->pUIEntityClassType = pComboBox;
	pState->pInsertIntoContFunc(pCont, pComboBox);
	y = ui_WidgetGetNextY(UI_WIDGET(pComboBox)) + CSE_UI_GAP;

	pLabel = ui_LabelCreate("Index:", CSE_UI_LEFT_IND, y);
	pState->pInsertIntoContFunc(pCont, pLabel);
	pTextEntry = ui_TextEntryCreate("0", 0, y);
	ui_WidgetSetPositionEx(UI_WIDGET(pTextEntry), CSE_UI_LARGE_IND, y, CSE_UI_SIDE_PERCENT, 0, UITopLeft);
	ui_WidgetSetWidthEx(UI_WIDGET(pTextEntry), 1.0f-2*CSE_UI_SIDE_PERCENT, UIUnitPercentage);
	ui_TextEntrySetFinishedCallback(pTextEntry, cutEdEntityChangedCB, pInfo);
	pState->pUIEntityTeamIdx = pTextEntry;
	pState->pInsertIntoContFunc(pCont, pTextEntry);
	y = ui_WidgetGetNextY(UI_WIDGET(pTextEntry)) + CSE_UI_GAP;

	pLabel = ui_LabelCreate("Encounter Name:", CSE_UI_LEFT_IND, y);
	pState->pInsertIntoContFunc(pCont, pLabel);
	pTextEntry = ui_TextEntryCreateWithStringCombo("", 0, y, &s_eaEncounterList, true, true, false, true);
	ui_WidgetSetPositionEx(UI_WIDGET(pTextEntry), CSE_UI_LARGE_IND, y, CSE_UI_SIDE_PERCENT, 0, UITopLeft);
	ui_WidgetSetWidthEx(UI_WIDGET(pTextEntry), 1.0f-2*CSE_UI_SIDE_PERCENT, UIUnitPercentage);
	ui_TextEntrySetFinishedCallback(pTextEntry, cutEdEntityChangedCB, pInfo);
	pState->pUIEntityEncounterName = pTextEntry;
	pState->pInsertIntoContFunc(pCont, pTextEntry);
	y = ui_WidgetGetNextY(UI_WIDGET(pTextEntry)) + CSE_UI_GAP;

	pLabel = ui_LabelCreate("Actor Name:", CSE_UI_LEFT_IND, y);
	pState->pInsertIntoContFunc(pCont, pLabel);
	pTextEntry = ui_TextEntryCreateWithStringCombo("", 0, y, &pState->eaEntityActorNameModel, true, true, false, true);
	ui_WidgetSetPositionEx(UI_WIDGET(pTextEntry), CSE_UI_LARGE_IND, y, CSE_UI_SIDE_PERCENT, 0, UITopLeft);
	ui_WidgetSetWidthEx(UI_WIDGET(pTextEntry), 1.0f-2*CSE_UI_SIDE_PERCENT, UIUnitPercentage);
	ui_TextEntrySetFinishedCallback(pTextEntry, cutEdEntityChangedCB, pInfo);
	pState->pUIEntityActorName = pTextEntry;
	pState->pInsertIntoContFunc(pCont, pTextEntry);
	y = ui_WidgetGetNextY(UI_WIDGET(pTextEntry)) + CSE_UI_GAP;


	y += UI_DSTEP;
	pLabel = ui_LabelCreate("Point Data:", CSE_UI_LEFT_IND, y);
	pState->pInsertIntoContFunc(pCont, pLabel);
	y = ui_WidgetGetNextY(UI_WIDGET(pLabel));

	pLabel = ui_LabelCreate("Action:", CSE_UI_LEFT_IND, y);
	pState->pInsertIntoContFunc(pCont, pLabel);
	pComboBox = ui_ComboBoxCreateWithEnum(0,y,1, CutsceneEntityActionTypeEnum, cutEdEntityChangedComboWrapper, pInfo);
	ui_WidgetSetPositionEx(UI_WIDGET(pComboBox), CSE_UI_LARGE_IND, y, CSE_UI_SIDE_PERCENT, 0, UITopLeft);
	ui_WidgetSetWidthEx(UI_WIDGET(pComboBox), 1.0f-2*CSE_UI_SIDE_PERCENT, UIUnitPercentage);
	pState->pUIEntityActionType = pComboBox;
	pState->pInsertIntoContFunc(pCont, pComboBox);
	y = ui_WidgetGetNextY(UI_WIDGET(pComboBox)) + CSE_UI_GAP;

	pLabel = ui_LabelCreate("Action Length:", CSE_UI_LEFT_IND, y);
	pState->pInsertIntoContFunc(pCont, pLabel);
	pTextEntry = ui_TextEntryCreate("0.0", 0, y);
	ui_WidgetSetPositionEx(UI_WIDGET(pTextEntry), CSE_UI_LARGE_IND, y, CSE_UI_SIDE_PERCENT, 0, UITopLeft);
	ui_WidgetSetWidthEx(UI_WIDGET(pTextEntry), 1.0f-2*CSE_UI_SIDE_PERCENT, UIUnitPercentage);
	ui_TextEntrySetFinishedCallback(pTextEntry, cutEdEntityChangedCB, pInfo);
	pState->pUIEntityLength = pTextEntry;
	pState->pInsertIntoContFunc(pCont, pTextEntry);
	y = ui_WidgetGetNextY(UI_WIDGET(pTextEntry)) + CSE_UI_GAP;

	pLabel = ui_LabelCreate("Animation:", CSE_UI_LEFT_IND, y);
	pState->pInsertIntoContFunc(pCont, pLabel);
	if(!demo_playingBack())
		resRequestAllResourcesInDictionary(g_AnimListDict);
	pTextEntry = ui_TextEntryCreateWithGlobalDictionaryCombo("", 0, 0, g_AnimListDict, "resourceName", /*bAutoComplete=*/true, /*bDrawSelected=*/true, /*bValidate=*/true, /*bFiltered=*/!demo_playingBack());
	ui_TextEntrySetFinishedCallback(pTextEntry, cutEdEntityChangedCB, pInfo);
	ui_WidgetSetPositionEx(UI_WIDGET(pTextEntry), CSE_UI_LARGE_IND, y, CSE_UI_SIDE_PERCENT, 0, UITopLeft);
	ui_WidgetSetWidthEx(UI_WIDGET(pTextEntry), 1.0f-2*CSE_UI_SIDE_PERCENT, UIUnitPercentage);
	pState->pUIEntityAnimation = pTextEntry;
	pState->pInsertIntoContFunc(pCont, pTextEntry);
	y = ui_WidgetGetNextY(UI_WIDGET(pTextEntry)) + CSE_UI_GAP;

	pLabel = ui_LabelCreate("Stance:", CSE_UI_LEFT_IND, y);
	pState->pInsertIntoContFunc(pCont, pLabel);
	if(demo_playingBack()) {
		pComboBox = ui_ComboBoxCreateWithGlobalDictionary(0,y,1, STANCE_DICTIONARY, "resourceName");
	} else {
		resRequestAllResourcesInDictionary(STANCE_DICTIONARY);
		pComboBox = ui_FilteredComboBoxCreateWithGlobalDictionary(0,y,1, STANCE_DICTIONARY, "resourceName");
	}
	ui_ComboBoxSetSelectedCallback(pComboBox, cutEdEntityChangedCB, pInfo);
	ui_WidgetSetPositionEx(UI_WIDGET(pComboBox), CSE_UI_LARGE_IND, y, CSE_UI_SIDE_PERCENT, 0, UITopLeft);
	ui_WidgetSetWidthEx(UI_WIDGET(pComboBox), 1.0f-2*CSE_UI_SIDE_PERCENT, UIUnitPercentage);
	pState->pUIEntityStance = pComboBox;
	pState->pInsertIntoContFunc(pCont, pComboBox);
	y = ui_WidgetGetNextY(UI_WIDGET(pComboBox)) + CSE_UI_GAP;

	pLabel = ui_LabelCreate("FX:", CSE_UI_LEFT_IND, y);
	pState->pInsertIntoContFunc(pCont, pLabel);
	if(demo_playingBack()) {
		pComboBox = ui_ComboBoxCreateWithGlobalDictionary(0,y,1, hDynFxInfoDict, "resourceName");
	} else {
		resRequestAllResourcesInDictionary(hDynFxInfoDict);
		pComboBox = ui_FilteredComboBoxCreateWithGlobalDictionary(0,y,1, hDynFxInfoDict, "resourceName");
	}
	ui_ComboBoxSetSelectedCallback(pComboBox, cutEdEntityChangedCB, pInfo);
	ui_WidgetSetPositionEx(UI_WIDGET(pComboBox), CSE_UI_LARGE_IND, y, CSE_UI_SIDE_PERCENT, 0, UITopLeft);
	ui_WidgetSetWidthEx(UI_WIDGET(pComboBox), 1.0f-2*CSE_UI_SIDE_PERCENT, UIUnitPercentage);
	pState->pUIEntityFX = pComboBox;
	pState->pInsertIntoContFunc(pCont, pComboBox);
	y = ui_WidgetGetNextY(UI_WIDGET(pComboBox)) + CSE_UI_GAP;

	pCheckButton = ui_CheckButtonCreate(CSE_UI_LEFT_IND, y, "Persistent FX", true);
	ui_CheckButtonSetToggledCallback(pCheckButton, cutEdEntityChangedCB, pInfo);
	pState->pUIEntityFXFlash = pCheckButton;
	pState->pInsertIntoContFunc(pCont, pCheckButton);
	y = ui_WidgetGetNextY(UI_WIDGET(pCheckButton)) + CSE_UI_GAP;

	pLabel = ui_LabelCreate("Position: ", CSE_UI_LEFT_IND, y);
	pState->pInsertIntoContFunc(pCont, pLabel);
	y = ui_WidgetGetNextY(UI_WIDGET(pLabel));

	pTextEntry = ui_TextEntryCreate("0.0", 0, y);
	ui_WidgetSetPositionEx(UI_WIDGET(pTextEntry), 0, y, CSE_UI_SIDE_PERCENT, 0, UITopLeft);
	ui_WidgetSetWidthEx(UI_WIDGET(pTextEntry), 1.0f-4*CSE_UI_SIDE_PERCENT-0.6f, UIUnitPercentage);
	ui_WidgetSkin(UI_WIDGET(pTextEntry), pState->pSkinRed);
	ui_TextEntrySetFinishedCallback(pTextEntry, cutEdEntityChangedCB, pInfo);
	pState->pUIEntityXPos = pTextEntry;
	pState->pInsertIntoContFunc(pCont, pTextEntry);
	pTextEntry = ui_TextEntryCreate("0.0", 0, y);
	ui_WidgetSetPositionEx(UI_WIDGET(pTextEntry), 0, y, 2*CSE_UI_SIDE_PERCENT+0.3f, 0, UITopLeft);
	ui_WidgetSetWidthEx(UI_WIDGET(pTextEntry), 1.0f-4*CSE_UI_SIDE_PERCENT-0.6f, UIUnitPercentage);
	ui_WidgetSkin(UI_WIDGET(pTextEntry), pState->pSkinGreen);
	ui_TextEntrySetFinishedCallback(pTextEntry, cutEdEntityChangedCB, pInfo);
	pState->pUIEntityYPos = pTextEntry;
	pState->pInsertIntoContFunc(pCont, pTextEntry);
	pTextEntry = ui_TextEntryCreate("0.0", 0, y);
	ui_WidgetSetPositionEx(UI_WIDGET(pTextEntry), 0, y, 3*CSE_UI_SIDE_PERCENT+0.6f, 0, UITopLeft);
	ui_WidgetSetWidthEx(UI_WIDGET(pTextEntry), 1.0f-4*CSE_UI_SIDE_PERCENT-0.6f, UIUnitPercentage);
	ui_WidgetSkin(UI_WIDGET(pTextEntry), pState->pSkinBlue);
	ui_TextEntrySetFinishedCallback(pTextEntry, cutEdEntityChangedCB, pInfo);
	pState->pUIEntityZPos = pTextEntry;
	pState->pInsertIntoContFunc(pCont, pTextEntry);
	y = ui_WidgetGetNextY(UI_WIDGET(pTextEntry)) + CSE_UI_GAP;

	pLabel = ui_LabelCreate("Rotation: ", CSE_UI_LEFT_IND, y);
	pState->pInsertIntoContFunc(pCont, pLabel);
	y = ui_WidgetGetNextY(UI_WIDGET(pLabel));

	pTextEntry = ui_TextEntryCreate("0.0", 0, y);
	ui_WidgetSetPositionEx(UI_WIDGET(pTextEntry), 0, y, CSE_UI_SIDE_PERCENT, 0, UITopLeft);
	ui_WidgetSetWidthEx(UI_WIDGET(pTextEntry), 1.0f-4*CSE_UI_SIDE_PERCENT-0.6f, UIUnitPercentage);
	ui_WidgetSkin(UI_WIDGET(pTextEntry), pState->pSkinGreen);
	ui_TextEntrySetFinishedCallback(pTextEntry, cutEdEntityChangedCB, pInfo);
	pState->pUIEntityYRot = pTextEntry;
	pState->pInsertIntoContFunc(pCont, pTextEntry);
	y = ui_WidgetGetNextY(UI_WIDGET(pTextEntry)) + CSE_UI_GAP;

	pLabel = ui_LabelCreate("Equipment:", CSE_UI_LEFT_IND, y);
	pState->pInsertIntoContFunc(pCont, pLabel);
	pState->iOverlayCostumesStartAtY = y;
	cutEdUpdateEntityOverlayCostumes(pState, pInfo, pCont);
}

static void cutEdMakeTextureCont(CutsceneEditorState *pState, CutsceneGenericTrackInfo *pInfo, void *pCont)
{
	int y = 0;
	UILabel *pLabel;
	UITextEntry *pTextEntry;
	UITextureEntry *pTextureEntry;
	UIComboBox *pCombo;
	
	pLabel = ui_LabelCreate("Track Data:", CSE_UI_LEFT_IND, y);
	pState->pInsertIntoContFunc(pCont, pLabel);
	y = ui_WidgetGetNextY(UI_WIDGET(pLabel));

	pLabel = ui_LabelCreate("Override Texture Var:", CSE_UI_LEFT_IND, y);
	pState->pInsertIntoContFunc(pCont, pLabel);
	pTextEntry = ui_TextEntryCreate("", 0, y);
	ui_WidgetSetPositionEx(UI_WIDGET(pTextEntry), CSE_UI_LARGE_IND, y, CSE_UI_SIDE_PERCENT, 0, UITopLeft);
	ui_WidgetSetWidthEx(UI_WIDGET(pTextEntry), 1.0f-2*CSE_UI_SIDE_PERCENT, UIUnitPercentage);
	ui_TextEntrySetFinishedCallback(pTextEntry, cutEdTextureChangedCB, pInfo);
	pState->pUITextureVariable = pTextEntry;
	pState->pInsertIntoContFunc(pCont, pTextEntry);
	y = ui_WidgetGetNextY(UI_WIDGET(pTextEntry)) + CSE_UI_GAP;

	pLabel = ui_LabelCreate("Texture:", CSE_UI_LEFT_IND, y);
	pState->pInsertIntoContFunc(pCont, pLabel);
	pTextureEntry = ui_TextureEntryCreate("white", NULL, false);
	ui_TextureEntrySetChangedCallback(pTextureEntry, cutEdTextureChangedCB, pInfo);
	ui_WidgetSetPositionEx(UI_WIDGET(pTextureEntry), CSE_UI_LARGE_IND, y, CSE_UI_SIDE_PERCENT, 0, UITopLeft);
	ui_WidgetSetWidthEx(UI_WIDGET(pTextureEntry), 1.0f-2*CSE_UI_SIDE_PERCENT, UIUnitPercentage);
	pState->pUITextureName = pTextureEntry;
	pState->pInsertIntoContFunc(pCont, pTextureEntry);
	y = ui_WidgetGetNextY(UI_WIDGET(pTextureEntry)) + CSE_UI_GAP;	

	pLabel = ui_LabelCreate("X Alignment:", CSE_UI_LEFT_IND, y);
	pState->pInsertIntoContFunc(pCont, pLabel);
	pCombo = ui_ComboBoxCreateWithEnum(CSE_UI_LARGE_IND, y, 1, CutsceneXScreenAlignmentEnum, cutEdTextureChangedEnumWrapperCB, pInfo);
	ui_WidgetSetPositionEx(UI_WIDGET(pCombo), CSE_UI_LARGE_IND, y, CSE_UI_SIDE_PERCENT, 0, UITopLeft);
	ui_WidgetSetWidthEx(UI_WIDGET(pCombo), 1.0f-2*CSE_UI_SIDE_PERCENT, UIUnitPercentage);
	pState->pUITextureXAlign = pCombo;
	pState->pInsertIntoContFunc(pCont, pCombo);
	y = ui_WidgetGetNextY(UI_WIDGET(pCombo)) + CSE_UI_GAP;	

	pLabel = ui_LabelCreate("Y Alignment:", CSE_UI_LEFT_IND, y);
	pState->pInsertIntoContFunc(pCont, pLabel);
	pCombo = ui_ComboBoxCreateWithEnum(CSE_UI_LARGE_IND, y, 1, CutsceneYScreenAlignmentEnum, cutEdTextureChangedEnumWrapperCB, pInfo);
	ui_WidgetSetPositionEx(UI_WIDGET(pCombo), CSE_UI_LARGE_IND, y, CSE_UI_SIDE_PERCENT, 0, UITopLeft);
	ui_WidgetSetWidthEx(UI_WIDGET(pCombo), 1.0f-2*CSE_UI_SIDE_PERCENT, UIUnitPercentage);
	pState->pUITextureYAlign = pCombo;
	pState->pInsertIntoContFunc(pCont, pCombo);
	y = ui_WidgetGetNextY(UI_WIDGET(pCombo)) + CSE_UI_GAP;	


	y += UI_DSTEP;
	pLabel = ui_LabelCreate("Point Data:", CSE_UI_LEFT_IND, y);
	pState->pInsertIntoContFunc(pCont, pLabel);
	y = ui_WidgetGetNextY(UI_WIDGET(pLabel));

	pLabel = ui_LabelCreate("Alpha:", CSE_UI_LEFT_IND, y);
	pState->pInsertIntoContFunc(pCont, pLabel);
	pTextEntry = ui_TextEntryCreate("0.0", 0, y);
	ui_WidgetSetPositionEx(UI_WIDGET(pTextEntry), CSE_UI_LARGE_IND, y, CSE_UI_SIDE_PERCENT, 0, UITopLeft);
	ui_WidgetSetWidthEx(UI_WIDGET(pTextEntry), 1.0f-2*CSE_UI_SIDE_PERCENT, UIUnitPercentage);
	ui_TextEntrySetFinishedCallback(pTextEntry, cutEdTextureChangedCB, pInfo);
	pState->pUITextureAlpha = pTextEntry;
	pState->pInsertIntoContFunc(pCont, pTextEntry);
	y = ui_WidgetGetNextY(UI_WIDGET(pTextEntry)) + CSE_UI_GAP;

	pLabel = ui_LabelCreate("Scale:", CSE_UI_LEFT_IND, y);
	pState->pInsertIntoContFunc(pCont, pLabel);
	pTextEntry = ui_TextEntryCreate("0.0", 0, y);
	ui_WidgetSetPositionEx(UI_WIDGET(pTextEntry), CSE_UI_LARGE_IND, y, CSE_UI_SIDE_PERCENT, 0, UITopLeft);
	ui_WidgetSetWidthEx(UI_WIDGET(pTextEntry), 1.0f-2*CSE_UI_SIDE_PERCENT, UIUnitPercentage);
	ui_TextEntrySetFinishedCallback(pTextEntry, cutEdTextureChangedCB, pInfo);
	pState->pUITextureScale = pTextEntry;
	pState->pInsertIntoContFunc(pCont, pTextEntry);
	y = ui_WidgetGetNextY(UI_WIDGET(pTextEntry)) + CSE_UI_GAP;

	pLabel = ui_LabelCreate("Position: ", CSE_UI_LEFT_IND, y);
	pState->pInsertIntoContFunc(pCont, pLabel);
	y = ui_WidgetGetNextY(UI_WIDGET(pLabel));

	pTextEntry = ui_TextEntryCreate("0.0", 0, y);
	ui_WidgetSetPositionEx(UI_WIDGET(pTextEntry), 0, y, CSE_UI_SIDE_PERCENT, 0, UITopLeft);
	ui_WidgetSetWidthEx(UI_WIDGET(pTextEntry), 1.0f-4*CSE_UI_SIDE_PERCENT-0.6f, UIUnitPercentage);
	ui_WidgetSkin(UI_WIDGET(pTextEntry), pState->pSkinRed);
	ui_TextEntrySetFinishedCallback(pTextEntry, cutEdTextureChangedCB, pInfo);
	pState->pUITextureXPos = pTextEntry;
	pState->pInsertIntoContFunc(pCont, pTextEntry);
	pTextEntry = ui_TextEntryCreate("0.0", 0, y);
	ui_WidgetSetPositionEx(UI_WIDGET(pTextEntry), 0, y, 2*CSE_UI_SIDE_PERCENT+0.3f, 0, UITopLeft);
	ui_WidgetSetWidthEx(UI_WIDGET(pTextEntry), 1.0f-4*CSE_UI_SIDE_PERCENT-0.6f, UIUnitPercentage);
	ui_WidgetSkin(UI_WIDGET(pTextEntry), pState->pSkinGreen);
	ui_TextEntrySetFinishedCallback(pTextEntry, cutEdTextureChangedCB, pInfo);
	pState->pUITextureYPos = pTextEntry;
	pState->pInsertIntoContFunc(pCont, pTextEntry);
	y = ui_WidgetGetNextY(UI_WIDGET(pTextEntry)) + CSE_UI_GAP;
}

static void cutEdMakeFXCont(CutsceneEditorState *pState, CutsceneGenericTrackInfo *pInfo, void *pCont)
{
	int y = 0;
	UILabel *pLabel;
	UIComboBox *pComboBox;
	UITextEntry *pTextEntry;

	pLabel = ui_LabelCreate("FX:", CSE_UI_LEFT_IND, y);
	pState->pInsertIntoContFunc(pCont, pLabel);
	if(demo_playingBack()) {
		pComboBox = ui_ComboBoxCreateWithGlobalDictionary(0,y,1, hDynFxInfoDict, "resourceName");
	} else {
		resRequestAllResourcesInDictionary(hDynFxInfoDict);
		pComboBox = ui_FilteredComboBoxCreateWithGlobalDictionary(0,y,1, hDynFxInfoDict, "resourceName");
	}
	ui_ComboBoxSetSelectedCallback(pComboBox, cutEdFXChangedCB, pInfo);
	ui_WidgetSetPositionEx(UI_WIDGET(pComboBox), CSE_UI_LARGE_IND, y, CSE_UI_SIDE_PERCENT, 0, UITopLeft);
	ui_WidgetSetWidthEx(UI_WIDGET(pComboBox), 1.0f-2*CSE_UI_SIDE_PERCENT, UIUnitPercentage);
	pState->pUIComboFX = pComboBox;
	pState->pInsertIntoContFunc(pCont, pComboBox);
	y = ui_WidgetGetNextY(UI_WIDGET(pComboBox)) + CSE_UI_GAP;

	pLabel = ui_LabelCreate("FX Length:", CSE_UI_LEFT_IND, y);
	pState->pInsertIntoContFunc(pCont, pLabel);
	pTextEntry = ui_TextEntryCreate("0.0", 0, y);
	ui_WidgetSetPositionEx(UI_WIDGET(pTextEntry), CSE_UI_LARGE_IND, y, CSE_UI_SIDE_PERCENT, 0, UITopLeft);
	ui_WidgetSetWidthEx(UI_WIDGET(pTextEntry), 1.0f-2*CSE_UI_SIDE_PERCENT, UIUnitPercentage);
	ui_TextEntrySetFinishedCallback(pTextEntry, cutEdFXChangedCB, pInfo);
	pState->pUIFXLength = pTextEntry;
	pState->pInsertIntoContFunc(pCont, pTextEntry);
	y = ui_WidgetGetNextY(UI_WIDGET(pTextEntry)) + CSE_UI_GAP;

	pLabel = ui_LabelCreate("Position: ", CSE_UI_LEFT_IND, y);
	pState->pInsertIntoContFunc(pCont, pLabel);
	y = ui_WidgetGetNextY(UI_WIDGET(pLabel));

	pTextEntry = ui_TextEntryCreate("0.0", 0, y);
	ui_WidgetSetPositionEx(UI_WIDGET(pTextEntry), 0, y, CSE_UI_SIDE_PERCENT, 0, UITopLeft);
	ui_WidgetSetWidthEx(UI_WIDGET(pTextEntry), 1.0f-4*CSE_UI_SIDE_PERCENT-0.6f, UIUnitPercentage);
	ui_WidgetSkin(UI_WIDGET(pTextEntry), pState->pSkinRed);
	ui_TextEntrySetFinishedCallback(pTextEntry, cutEdFXChangedCB, pInfo);
	pState->pUIFXXPos = pTextEntry;
	pState->pInsertIntoContFunc(pCont, pTextEntry);
	pTextEntry = ui_TextEntryCreate("0.0", 0, y);
	ui_WidgetSetPositionEx(UI_WIDGET(pTextEntry), 0, y, 2*CSE_UI_SIDE_PERCENT+0.3f, 0, UITopLeft);
	ui_WidgetSetWidthEx(UI_WIDGET(pTextEntry), 1.0f-4*CSE_UI_SIDE_PERCENT-0.6f, UIUnitPercentage);
	ui_WidgetSkin(UI_WIDGET(pTextEntry), pState->pSkinGreen);
	ui_TextEntrySetFinishedCallback(pTextEntry, cutEdFXChangedCB, pInfo);
	pState->pUIFXYPos = pTextEntry;
	pState->pInsertIntoContFunc(pCont, pTextEntry);
	pTextEntry = ui_TextEntryCreate("0.0", 0, y);
	ui_WidgetSetPositionEx(UI_WIDGET(pTextEntry), 0, y, 3*CSE_UI_SIDE_PERCENT+0.6f, 0, UITopLeft);
	ui_WidgetSetWidthEx(UI_WIDGET(pTextEntry), 1.0f-4*CSE_UI_SIDE_PERCENT-0.6f, UIUnitPercentage);
	ui_WidgetSkin(UI_WIDGET(pTextEntry), pState->pSkinBlue);
	ui_TextEntrySetFinishedCallback(pTextEntry, cutEdFXChangedCB, pInfo);
	pState->pUIFXZPos = pTextEntry;
	pState->pInsertIntoContFunc(pCont, pTextEntry);
	y = ui_WidgetGetNextY(UI_WIDGET(pTextEntry)) + CSE_UI_GAP;
}

static void cutEdMakeSoundCont(CutsceneEditorState *pState, CutsceneGenericTrackInfo *pInfo, void *pCont)
{
	int y = 0;
	UILabel *pLabel;
	UIComboBox *pComboBox;
	UITextEntry *pTextEntry;
	UICheckButton *pCheckButton;

	pLabel = ui_LabelCreate("Override Sound Var:", CSE_UI_LEFT_IND, y);
	pState->pInsertIntoContFunc(pCont, pLabel);
	pTextEntry = ui_TextEntryCreate("", 0, y);
	ui_WidgetSetPositionEx(UI_WIDGET(pTextEntry), CSE_UI_LARGE_IND, y, CSE_UI_SIDE_PERCENT, 0, UITopLeft);
	ui_WidgetSetWidthEx(UI_WIDGET(pTextEntry), 1.0f-2*CSE_UI_SIDE_PERCENT, UIUnitPercentage);
	ui_TextEntrySetFinishedCallback(pTextEntry, cutEdSoundChangedCB, pInfo);
	pState->pUISoundVariable = pTextEntry;
	pState->pInsertIntoContFunc(pCont, pTextEntry);
	y = ui_WidgetGetNextY(UI_WIDGET(pTextEntry)) + CSE_UI_GAP;

	pLabel = ui_LabelCreate("Sound:", CSE_UI_LEFT_IND, y);
	pState->pInsertIntoContFunc(pCont, pLabel);
	pComboBox = ui_FilteredComboBoxCreate(0,y,1, NULL, sndGetEventListStatic(), NULL);
	ui_ComboBoxSetSelectedCallback(pComboBox, cutEdSoundChangedCB, pInfo);
	ui_WidgetSetPositionEx(UI_WIDGET(pComboBox), CSE_UI_LARGE_IND, y, CSE_UI_SIDE_PERCENT, 0, UITopLeft);
	ui_WidgetSetWidthEx(UI_WIDGET(pComboBox), 1.0f-2*CSE_UI_SIDE_PERCENT, UIUnitPercentage);
	pState->pUIComboSound = pComboBox;
	pState->pInsertIntoContFunc(pCont, pComboBox);
	y = ui_WidgetGetNextY(UI_WIDGET(pComboBox)) + CSE_UI_GAP;

	pLabel = ui_LabelCreate("Sound Length:", CSE_UI_LEFT_IND, y);
	pState->pInsertIntoContFunc(pCont, pLabel);
	pTextEntry = ui_TextEntryCreate("0.0", 0, y);
	ui_WidgetSetPositionEx(UI_WIDGET(pTextEntry), CSE_UI_LARGE_IND, y, CSE_UI_SIDE_PERCENT, 0, UITopLeft);
	ui_WidgetSetWidthEx(UI_WIDGET(pTextEntry), 1.0f-2*CSE_UI_SIDE_PERCENT, UIUnitPercentage);
	ui_TextEntrySetFinishedCallback(pTextEntry, cutEdSoundChangedCB, pInfo);
	pState->pUITextSoundLength = pTextEntry;
	pState->pInsertIntoContFunc(pCont, pTextEntry);
	y = ui_WidgetGetNextY(UI_WIDGET(pTextEntry)) + CSE_UI_GAP;

	pLabel = ui_LabelCreate("On Entity:", CSE_UI_LEFT_IND, y);
	pState->pInsertIntoContFunc(pCont, pLabel);
	pTextEntry = ui_TextEntryCreate("0.0", 0, y);
	ui_WidgetSetPositionEx(UI_WIDGET(pTextEntry), CSE_UI_LARGE_IND, y, CSE_UI_SIDE_PERCENT, 0, UITopLeft);
	ui_WidgetSetWidthEx(UI_WIDGET(pTextEntry), 1.0f-2*CSE_UI_SIDE_PERCENT, UIUnitPercentage);
	ui_TextEntrySetFinishedCallback(pTextEntry, cutEdSoundChangedCB, pInfo);
	pState->pUITextSndCutEntName = pTextEntry;
	pState->pInsertIntoContFunc(pCont, pTextEntry);
	y = ui_WidgetGetNextY(UI_WIDGET(pTextEntry)) + CSE_UI_GAP;	

	pCheckButton = ui_CheckButtonCreate(CSE_UI_LEFT_IND, y, "Use Camera Position", true);
	ui_CheckButtonSetToggledCallback(pCheckButton, cutEdSoundChangedCB, pInfo);
	pState->pUICheckSoundCamPos = pCheckButton;
	pState->pInsertIntoContFunc(pCont, pCheckButton);
	y = ui_WidgetGetNextY(UI_WIDGET(pCheckButton)) + CSE_UI_GAP;

	pLabel = ui_LabelCreate("Position: ", CSE_UI_LEFT_IND, y);
	pState->pInsertIntoContFunc(pCont, pLabel);
	y = ui_WidgetGetNextY(UI_WIDGET(pLabel));

	pTextEntry = ui_TextEntryCreate("0.0", 0, y);
	ui_WidgetSetPositionEx(UI_WIDGET(pTextEntry), 0, y, CSE_UI_SIDE_PERCENT, 0, UITopLeft);
	ui_WidgetSetWidthEx(UI_WIDGET(pTextEntry), 1.0f-4*CSE_UI_SIDE_PERCENT-0.6f, UIUnitPercentage);
	ui_WidgetSkin(UI_WIDGET(pTextEntry), pState->pSkinRed);
	ui_TextEntrySetFinishedCallback(pTextEntry, cutEdSoundChangedCB, pInfo);
	pState->pUISoundXPos = pTextEntry;
	pState->pInsertIntoContFunc(pCont, pTextEntry);
	pTextEntry = ui_TextEntryCreate("0.0", 0, y);
	ui_WidgetSetPositionEx(UI_WIDGET(pTextEntry), 0, y, 2*CSE_UI_SIDE_PERCENT+0.3f, 0, UITopLeft);
	ui_WidgetSetWidthEx(UI_WIDGET(pTextEntry), 1.0f-4*CSE_UI_SIDE_PERCENT-0.6f, UIUnitPercentage);
	ui_WidgetSkin(UI_WIDGET(pTextEntry), pState->pSkinGreen);
	ui_TextEntrySetFinishedCallback(pTextEntry, cutEdSoundChangedCB, pInfo);
	pState->pUISoundYPos = pTextEntry;
	pState->pInsertIntoContFunc(pCont, pTextEntry);
	pTextEntry = ui_TextEntryCreate("0.0", 0, y);
	ui_WidgetSetPositionEx(UI_WIDGET(pTextEntry), 0, y, 3*CSE_UI_SIDE_PERCENT+0.6f, 0, UITopLeft);
	ui_WidgetSetWidthEx(UI_WIDGET(pTextEntry), 1.0f-4*CSE_UI_SIDE_PERCENT-0.6f, UIUnitPercentage);
	ui_WidgetSkin(UI_WIDGET(pTextEntry), pState->pSkinBlue);
	ui_TextEntrySetFinishedCallback(pTextEntry, cutEdSoundChangedCB, pInfo);
	pState->pUISoundZPos = pTextEntry;
	pState->pInsertIntoContFunc(pCont, pTextEntry);
	y = ui_WidgetGetNextY(UI_WIDGET(pTextEntry)) + CSE_UI_GAP;
}


static void cutEdMakeSubtitleCont(CutsceneEditorState *pState, CutsceneGenericTrackInfo *pInfo, void *pCont)
{
	int y = 0;
	UILabel *pLabel;
	UITextEntry *pTextEntry;
	UIMessageEntry *pMessage;

	pLabel = ui_LabelCreate("Override Subtitle Var:", CSE_UI_LEFT_IND, y);
	pState->pInsertIntoContFunc(pCont, pLabel);
	pTextEntry = ui_TextEntryCreate("", 0, y);
	ui_WidgetSetPositionEx(UI_WIDGET(pTextEntry), CSE_UI_LARGE_IND, y, CSE_UI_SIDE_PERCENT, 0, UITopLeft);
	ui_WidgetSetWidthEx(UI_WIDGET(pTextEntry), 1.0f-2*CSE_UI_SIDE_PERCENT, UIUnitPercentage);
	ui_TextEntrySetFinishedCallback(pTextEntry, cutEdSubtitleChangedCB, pInfo);
	pState->pUISubtitleVariable = pTextEntry;
	pState->pInsertIntoContFunc(pCont, pTextEntry);
	y = ui_WidgetGetNextY(UI_WIDGET(pTextEntry)) + CSE_UI_GAP;

	pLabel = ui_LabelCreate("Text:", CSE_UI_LEFT_IND, y);
	pState->pInsertIntoContFunc(pCont, pLabel);
	pMessage = ui_MessageEntryCreate(NULL, 0, y, 1);
	ui_WidgetSetPositionEx(UI_WIDGET(pMessage), CSE_UI_LARGE_IND, y, CSE_UI_SIDE_PERCENT, 0, UITopLeft);
	ui_WidgetSetWidthEx(UI_WIDGET(pMessage), 1.0f-2*CSE_UI_SIDE_PERCENT, UIUnitPercentage);
	ui_MessageEntrySetCanEditKey(pMessage, false);
	ui_MessageEntrySetChangedCallback(pMessage, cutEdSubtitleChangedCB, pInfo);
	pState->pUISubtitleMessage = pMessage;
	pState->pInsertIntoContFunc(pCont, pMessage);
	y = ui_WidgetGetNextY(UI_WIDGET(pMessage)) + CSE_UI_GAP;

	pLabel = ui_LabelCreate("Length:", CSE_UI_LEFT_IND, y);
	pState->pInsertIntoContFunc(pCont, pLabel);
	pTextEntry = ui_TextEntryCreate("0.0", 0, y);
	ui_WidgetSetPositionEx(UI_WIDGET(pTextEntry), CSE_UI_LARGE_IND, y, CSE_UI_SIDE_PERCENT, 0, UITopLeft);
	ui_WidgetSetWidthEx(UI_WIDGET(pTextEntry), 1.0f-2*CSE_UI_SIDE_PERCENT, UIUnitPercentage);
	ui_TextEntrySetFinishedCallback(pTextEntry, cutEdSubtitleChangedCB, pInfo);
	pState->pUISubtitleLength = pTextEntry;
	pState->pInsertIntoContFunc(pCont, pTextEntry);
	y = ui_WidgetGetNextY(UI_WIDGET(pTextEntry)) + CSE_UI_GAP;	

	pLabel = ui_LabelCreate("Chat Bubble Entity:", CSE_UI_LEFT_IND, y);
	pState->pInsertIntoContFunc(pCont, pLabel);
	pTextEntry = ui_TextEntryCreate("0.0", 0, y);
	ui_WidgetSetPositionEx(UI_WIDGET(pTextEntry), CSE_UI_LARGE_IND, y, CSE_UI_SIDE_PERCENT, 0, UITopLeft);
	ui_WidgetSetWidthEx(UI_WIDGET(pTextEntry), 1.0f-2*CSE_UI_SIDE_PERCENT, UIUnitPercentage);
	ui_TextEntrySetFinishedCallback(pTextEntry, cutEdSubtitleChangedCB, pInfo);
	pState->pUISubtitleCutEntName = pTextEntry;
	pState->pInsertIntoContFunc(pCont, pTextEntry);
	y = ui_WidgetGetNextY(UI_WIDGET(pTextEntry)) + CSE_UI_GAP;	
}

static void cutEdUIGenChangedComboWrapper(void *unused, int i, CutsceneGenericTrackInfo *pInfo)
{
	cutEdUIGenChangedCB(NULL, pInfo);
}

static void cutEdMakeUIGenCont(CutsceneEditorState *pState, CutsceneGenericTrackInfo *pInfo, void *pCont)
{
	int y = 0;
	UILabel *pLabel;
	UITextEntry *pTextEntry;
	UIComboBox *pComboBox;
	UIMessageEntry *pMessage;

	pLabel = ui_LabelCreate("Time:", CSE_UI_LEFT_IND, y);
	pState->pInsertIntoContFunc(pCont, pLabel);
	pTextEntry = ui_TextEntryCreate("0.0", 0, y);
	ui_WidgetSetPositionEx(UI_WIDGET(pTextEntry), CSE_UI_LARGE_IND, y, CSE_UI_SIDE_PERCENT, 0, UITopLeft);
	ui_WidgetSetWidthEx(UI_WIDGET(pTextEntry), 1.0f-2*CSE_UI_SIDE_PERCENT, UIUnitPercentage);
	ui_TextEntrySetFinishedCallback(pTextEntry, cutEdUIGenChangedCB, pInfo);
	pState->pUIUIGenTime = pTextEntry;
	pState->pInsertIntoContFunc(pCont, pTextEntry);
	y = ui_WidgetGetNextY(UI_WIDGET(pTextEntry)) + CSE_UI_GAP;

	pLabel = ui_LabelCreate("Action:", CSE_UI_LEFT_IND, y);
	pState->pInsertIntoContFunc(pCont, pLabel);
	pComboBox = ui_ComboBoxCreateWithEnum(0, y, 1, CutsceneUIGenActionTypeEnum, cutEdUIGenChangedComboWrapper, pInfo);
	ui_WidgetSetPositionEx(UI_WIDGET(pComboBox), CSE_UI_LARGE_IND, y, CSE_UI_SIDE_PERCENT, 0, UITopLeft);
	ui_WidgetSetWidthEx(UI_WIDGET(pComboBox), 1.0f-2*CSE_UI_SIDE_PERCENT, UIUnitPercentage);
	pState->pUIUIGenActionType = pComboBox;
	pState->pInsertIntoContFunc(pCont, pComboBox);
	y = ui_WidgetGetNextY(UI_WIDGET(pComboBox)) + CSE_UI_GAP;

	pLabel = ui_LabelCreate("Message:", CSE_UI_LEFT_IND, y);
	pState->pInsertIntoContFunc(pCont, pLabel);
	pTextEntry = ui_TextEntryCreate("", 0, y);
	ui_WidgetSetPositionEx(UI_WIDGET(pTextEntry), CSE_UI_LARGE_IND, y, CSE_UI_SIDE_PERCENT, 0, UITopLeft);
	ui_WidgetSetWidthEx(UI_WIDGET(pTextEntry), 1.0f-2*CSE_UI_SIDE_PERCENT, UIUnitPercentage);
	ui_TextEntrySetFinishedCallback(pTextEntry, cutEdUIGenChangedCB, pInfo);
	pState->pUIUIGenMessage = pTextEntry;
	pState->pInsertIntoContFunc(pCont, pTextEntry);
	y = ui_WidgetGetNextY(UI_WIDGET(pTextEntry)) + CSE_UI_GAP;

	pLabel = ui_LabelCreate("Variable:", CSE_UI_LEFT_IND, y);
	pState->pInsertIntoContFunc(pCont, pLabel);
	pTextEntry = ui_TextEntryCreate("", 0, y);
	ui_WidgetSetPositionEx(UI_WIDGET(pTextEntry), CSE_UI_LARGE_IND, y, CSE_UI_SIDE_PERCENT, 0, UITopLeft);
	ui_WidgetSetWidthEx(UI_WIDGET(pTextEntry), 1.0f-2*CSE_UI_SIDE_PERCENT, UIUnitPercentage);
	ui_TextEntrySetFinishedCallback(pTextEntry, cutEdUIGenChangedCB, pInfo);
	pState->pUIUIGenVariable = pTextEntry;
	pState->pInsertIntoContFunc(pCont, pTextEntry);
	y = ui_WidgetGetNextY(UI_WIDGET(pTextEntry)) + CSE_UI_GAP;

	pLabel = ui_LabelCreate("String Value:", CSE_UI_LEFT_IND, y);
	pState->pInsertIntoContFunc(pCont, pLabel);
	pTextEntry = ui_TextEntryCreate("", 0, y);
	ui_WidgetSetPositionEx(UI_WIDGET(pTextEntry), CSE_UI_LARGE_IND, y, CSE_UI_SIDE_PERCENT, 0, UITopLeft);
	ui_WidgetSetWidthEx(UI_WIDGET(pTextEntry), 1.0f-2*CSE_UI_SIDE_PERCENT, UIUnitPercentage);
	ui_TextEntrySetFinishedCallback(pTextEntry, cutEdUIGenChangedCB, pInfo);
	pState->pUIUIGenStringValue = pTextEntry;
	pState->pInsertIntoContFunc(pCont, pTextEntry);
	y = ui_WidgetGetNextY(UI_WIDGET(pTextEntry)) + CSE_UI_GAP;

	pLabel = ui_LabelCreate("Float Value:", CSE_UI_LEFT_IND, y);
	pState->pInsertIntoContFunc(pCont, pLabel);
	pTextEntry = ui_TextEntryCreate("0.0", 0, y);
	ui_WidgetSetPositionEx(UI_WIDGET(pTextEntry), CSE_UI_LARGE_IND, y, CSE_UI_SIDE_PERCENT, 0, UITopLeft);
	ui_WidgetSetWidthEx(UI_WIDGET(pTextEntry), 1.0f-2*CSE_UI_SIDE_PERCENT, UIUnitPercentage);
	ui_TextEntrySetFinishedCallback(pTextEntry, cutEdUIGenChangedCB, pInfo);
	pState->pUIUIGenFloatValue = pTextEntry;
	pState->pInsertIntoContFunc(pCont, pTextEntry);
	y = ui_WidgetGetNextY(UI_WIDGET(pTextEntry)) + CSE_UI_GAP;

	pLabel = ui_LabelCreate("Message Value:", CSE_UI_LEFT_IND, y);
	pState->pInsertIntoContFunc(pCont, pLabel);
	pMessage = ui_MessageEntryCreate(NULL, 0, y, 1);
	ui_WidgetSetPositionEx(UI_WIDGET(pMessage), CSE_UI_LARGE_IND, y, CSE_UI_SIDE_PERCENT, 0, UITopLeft);
	ui_WidgetSetWidthEx(UI_WIDGET(pMessage), 1.0f-2*CSE_UI_SIDE_PERCENT, UIUnitPercentage);
	ui_MessageEntrySetCanEditKey(pMessage, false);
	ui_MessageEntrySetChangedCallback(pMessage, cutEdUIGenChangedCB, pInfo);
	pState->pUIUIGenMessageValue = pMessage;
	pState->pInsertIntoContFunc(pCont, pMessage);
	y = ui_WidgetGetNextY(UI_WIDGET(pMessage)) + CSE_UI_GAP;

	pLabel = ui_LabelCreate("Message Value Variable:", CSE_UI_LEFT_IND, y);
	pState->pInsertIntoContFunc(pCont, pLabel);
	pTextEntry = ui_TextEntryCreate("", 0, y);
	ui_WidgetSetPositionEx(UI_WIDGET(pTextEntry), CSE_UI_LARGE_IND, y, CSE_UI_SIDE_PERCENT, 0, UITopLeft);
	ui_WidgetSetWidthEx(UI_WIDGET(pTextEntry), 1.0f-2*CSE_UI_SIDE_PERCENT, UIUnitPercentage);
	ui_TextEntrySetFinishedCallback(pTextEntry, cutEdUIGenChangedCB, pInfo);
	pState->pUIUIGenMessageValueVariable = pTextEntry;
	pState->pInsertIntoContFunc(pCont, pTextEntry);
	y = ui_WidgetGetNextY(UI_WIDGET(pTextEntry)) + CSE_UI_GAP;
}

static void cutEdStopCB(void *unused, CutsceneEditorState *pState)
{
	cutEdStop(pState);
}

static void cutEdNewTrackCB(UIButton *unused, CutsceneEditorState *pState)
{
	int i;

	if(!pState->pMenuTimeline)
		pState->pMenuTimeline = ui_MenuCreate("TimelineNewTrackClick");
	else 
		ui_MenuClearAndFreeItems(pState->pMenuTimeline);

	for ( i=0; i < eaSize(&pState->ppTrackInfos); i++ ) {
		CutsceneGenericTrackInfo *pInfo = pState->ppTrackInfos[i];
		if(pInfo->bMulti || !TokenStoreGetPointer(parse_CutsceneDef, pInfo->iCGTColumn, pInfo->pState->pCutsceneDef, 0, NULL)) {
			char buf[256];
			sprintf(buf, "Add %s Track", pInfo->pcCGTName);
			ui_MenuAppendItem(pState->pMenuTimeline, ui_MenuItemCreate(buf, UIMenuCallback, cutEdTimelineAddCGTCB, pInfo, NULL));
		}
	}

	if(eaSize(&pState->pMenuTimeline->items) > 0) {
		ui_MenuPopupAtCursor(pState->pMenuTimeline);
	}
}

static void cutEdInitTimelinePane(CutsceneEditorState *pState)
{
	int y = 4;
	UIPane *pPane;
	UITimeline *pTimeline;
	UITimelineTrack *pTrack;
	UIButton *pButton;

	pPane = ui_PaneCreate( 0, 0, 1, 200, UIUnitPercentage, UIUnitFixed, UI_PANE_VP_TOP );
	ui_PaneSetResizable(pPane, UITop, 0, 110);
	pPane->widget.offsetFrom = UIBottomLeft;
	pState->pTimelinePane = pPane;
	
	pTimeline = ui_TimelineCreate(0,0,1);
	pTimeline->track_label_width = 150.0f;
	ui_WidgetSetDimensionsEx( UI_WIDGET( pTimeline ), 1, 1, UIUnitPercentage, UIUnitPercentage );
	ui_TimelineSetTimeChangedCallback(pTimeline, cutEdTimelineTimeChangedCB, pState);
	ui_TimelineSetFramePreChangedCallback(pTimeline, cutEdTimlinePreChangedCB, pState);
	ui_TimelineSetFrameChangedCallback(pTimeline, cutEdTimlineFrameTimeChangedCB, pState);
	ui_TimelineSetSelectionChangedCallback(pTimeline, cutEdTimelineSelectionChangedCB, pState);
	pTimeline->continuous = true;
	pState->pTimeline = pTimeline;
	ui_PaneAddChild(pState->pTimelinePane, pTimeline);

	pTrack = cutEdCreateTrack("Camera Position", NULL);
	pState->pTrackCamPos = pTrack;
	ui_TimelineAddTrack(pTimeline, pTrack);
	ui_TimelineTrackSetLabelRightClickCallback(pTrack, cutEdTimelineCameraTrackLabelCB, pState);

	pTrack = cutEdCreateTrack("Look At Position", NULL);
	pState->pTrackCamLookAt = pTrack;
	ui_TimelineAddTrack(pTimeline, pTrack);
	ui_TimelineTrackSetLabelRightClickCallback(pTrack, cutEdTimelineCameraTrackLabelCB, pState);

	pButton = ui_ButtonCreateImageOnly("play_icon", 0, y, cutEdPlayCB, pState);
	ui_ButtonSetImageStretch(pButton, true);
	ui_WidgetSetDimensions(UI_WIDGET(pButton), CSE_UI_PLAY_BUTTON_WIDTH, CSE_UI_PLAY_BUTTON_HEIGHT);
	ui_WidgetGroupAdd(&UI_WIDGET(pTimeline)->children, (UIWidget *)pButton);
	pButton = ui_ButtonCreateImageOnly("pause_icon", ui_WidgetGetNextX(UI_WIDGET(pButton)) + CSE_UI_GAP, y, cutEdPauseCB, pState);
	ui_ButtonSetImageStretch(pButton, true);
	ui_WidgetSetDimensions(UI_WIDGET(pButton), CSE_UI_PLAY_BUTTON_WIDTH, CSE_UI_PLAY_BUTTON_HEIGHT);
	ui_WidgetGroupAdd(&UI_WIDGET(pTimeline)->children, (UIWidget *)pButton);
	pButton = ui_ButtonCreateImageOnly("stop_icon", ui_WidgetGetNextX(UI_WIDGET(pButton)) + CSE_UI_GAP, y, cutEdStopCB, pState);
	ui_ButtonSetImageStretch(pButton, true);
	ui_WidgetSetDimensions(UI_WIDGET(pButton), CSE_UI_PLAY_BUTTON_WIDTH, CSE_UI_PLAY_BUTTON_HEIGHT);
	pState->pUIButtonStop = pButton;
	ui_WidgetGroupAdd(&UI_WIDGET(pTimeline)->children, (UIWidget *)pButton);

	if(!demo_playingBack())
	{
		pButton = ui_ButtonCreateImageOnly("record_icon", ui_WidgetGetNextX(UI_WIDGET(pButton)) + CSE_UI_GAP, y, cutEdRecordCB, pState);
		ui_ButtonSetImageStretch(pButton, true);
		ui_WidgetSetDimensions(UI_WIDGET(pButton), CSE_UI_PLAY_BUTTON_WIDTH, CSE_UI_PLAY_BUTTON_HEIGHT);
		ui_WidgetGroupAdd(&UI_WIDGET(pTimeline)->children, (UIWidget *)pButton);
	}

	pButton = ui_ButtonCreateImageOnly("eui_button_plus", ui_WidgetGetNextX(UI_WIDGET(pButton)) + CSE_UI_GAP, y, cutEdNewTrackCB, pState);
	ui_ButtonSetImageStretch(pButton, true);
	ui_WidgetSetDimensions(UI_WIDGET(pButton), CSE_UI_PLAY_BUTTON_WIDTH, CSE_UI_PLAY_BUTTON_HEIGHT);
	ui_WidgetGroupAdd(&UI_WIDGET(pTimeline)->children, (UIWidget *)pButton);
}

static void cutEdRegisterCGTInfo(CutsceneEditorState *pState, const char *pcDisplayName, const char *pcCGTParseTableName,
								U8 bMulti, U8 bAlwaysCameraPlace, U8 bPreventOrderChanges, U8 bAllowResize, Color color,
								ParseTable *pCGTPti, ParseTable *pGenPntPti, 
								cutEdUICreateCGTFunc pCreateFunc, cutEdUIRefreshCGTFunc pRefreshFunc,
								cutEdUIGenPntFunc pInitGenPntFunc, cutEdUIGenPntPredicateFunc pGenPntValidReferencePntFunc,
								cutEdUIGenPntMatFunc pGetGenPntMatFunc, cutEdUIGenPntMatFunc pSetGenPntMatFunc,
								cutEdUIGenPntBoundsFunc pGetGenPntBoundsFunc, cutEdCGTApplyFunc pDrawFunc)
{
	CutsceneGenericTrackInfo *pInfo = calloc(1, sizeof(CutsceneGenericTrackInfo));
	bool bColumnFound;
	
	assert(pcDisplayName);

	pInfo->pState = pState;
	pInfo->pcCGTName = StructAllocString(pcDisplayName);
	bColumnFound = ParserFindColumn(parse_CutsceneDef, pcCGTParseTableName, &pInfo->iCGTColumn);
	assert(bColumnFound);
	pInfo->bMulti = bMulti;
	pInfo->bAllowOverlap = false;
	pInfo->bAllowResize = bAllowResize;
	pInfo->bAlwaysCameraPlace = bAlwaysCameraPlace;
	pInfo->bPreventOrderChanges = bPreventOrderChanges;
	pInfo->color = color;
	pInfo->pCGTPti = pCGTPti;
	pInfo->pGenPntPti = pGenPntPti;

	if(pState->pParentDoc) {
		pInfo->pGenPntCont = emPanelCreate("Cut Scene", pcDisplayName, 0);
	} else {
		pInfo->pGenPntCont = ui_ExpanderCreate(pcDisplayName, 0);
		ui_ExpanderSetOpened(pState->pPointCont, true);
	}
	pCreateFunc(pState, pInfo, pInfo->pGenPntCont);

	pInfo->pRefreshFunc = pRefreshFunc;

	pInfo->pInitGenPntFunc = pInitGenPntFunc;
	pInfo->pGenPntValidReferencePntFunc = pGenPntValidReferencePntFunc;
	pInfo->pGetGenPntMatFunc = pGetGenPntMatFunc;
	pInfo->pSetGenPntMatFunc = pSetGenPntMatFunc;
	pInfo->pGetGenPntBoundsFunc = pGetGenPntBoundsFunc;
	pInfo->pDrawFunc = pDrawFunc;

	eaPush(&pState->ppTrackInfos, pInfo);
}

//////////////////////////////////////////////////////////////////////////
// Exported functions (and their state variables) used by both
// Cutscene Editor and Cutscene Demo Play Editor
//////////////////////////////////////////////////////////////////////////

void cutEdInitCommon(CutsceneEditorState *pState, CutsceneDef *pCutsceneDef)
{
	cutEdValidate(pCutsceneDef);
	pState->cutsceneTime = cutscene_GetLength(pCutsceneDef, true);
	pState->cutsceneMoveTime = cutscene_GetLength(pCutsceneDef, false);

	if(pCutsceneDef->pPathList && eaSize(&pCutsceneDef->pPathList->ppPaths) > 0)
		pState->pSelectedPath = pCutsceneDef->pPathList->ppPaths[0];
	pState->selectedPoint = -1;

	if(!pState->bInited)
	{
		pState->pSkinBlue = ui_SkinCreate(NULL);
		pState->pSkinBlue->entry[0] = colorFromRGBA(0xAAAAFFFF);
		pState->pSkinGreen = ui_SkinCreate(NULL);
		pState->pSkinGreen->entry[0] = colorFromRGBA(0xAAFFAAFF);
		pState->pSkinRed = ui_SkinCreate(NULL);
		pState->pSkinRed->entry[0] = colorFromRGBA(0xFFAAAAFF);
		pState->pSkinWindow = ui_SkinCreate(NULL);

		pState->bInited = true;
	}
}

void cutEdInitUICommon(CutsceneEditorState *pState, CutsceneDef *pCutsceneDef)
{
	cutEdInitTimelinePane(pState);
	cutEdInitGizmos(pState);
	
	cutEdMakeMainCont(pState, pCutsceneDef);
	cutEdMakePathListCont(pState, pCutsceneDef);
	cutEdMakePointListCont(pState, pCutsceneDef);
	cutEdMakeBasicPathCont(pState, pCutsceneDef);
	cutEdMakeCirclePathCont(pState, pCutsceneDef);
	cutEdMakeWatchPathCont(pState, pCutsceneDef);
	cutEdMakePointCont(pState, pCutsceneDef);
	cutEdMakeCGTRelPosCont(pState);
	cutEdMakeGenPntCont(pState);
	cutEdMakeGenCGTCont(pState);

	//////////////////////////////////////////////////////////////////////////
	// CutsceneEffectsAndEvents
	// Register your track with the editor here.
	// Make appropriate function callbacks, look at others for examples.

	cutEdRegisterCGTInfo(pState, "Camera Fade", "FadeList",
		/*bMulti=*/false, /*bAlwaysCameraPlace=*/false, /*bPreventOrderChanges=*/false, /*bAllowResize=*/false, colorFromRGBA(0x000000FF),
		parse_CutsceneFadeList, parse_CutsceneFadePoint, 
		cutEdMakeFadeCont, cutEdRefreshFadeUI,
		/*pInitGenPntFunc=*/NULL, /*pGenPntValidReferencePntFunc=*/NULL,
		/*pGetGenPntMatFunc=*/NULL, /*pSetGenPntMatFunc=*/NULL,
		/*pGetGenPntBoundsFunc=*/NULL, /*pDrawFunc=*/NULL);

	cutEdRegisterCGTInfo(pState, "Depth of Field", "DOFList",
		/*bMulti=*/false, /*bAlwaysCameraPlace=*/false, /*bPreventOrderChanges=*/false, /*bAllowResize=*/false, colorFromRGBA(0x009696FF),
		parse_CutsceneDOFList, parse_CutsceneDOFPoint, 
		cutEdMakeDOFCont, cutEdRefreshDOFUI,
		cutEdInitDOFPoint, /*pGenPntValidReferencePntFunc=*/NULL,
		/*pGetGenPntMatFunc=*/NULL, /*pSetGenPntMatFunc=*/NULL,
		/*pGetGenPntBoundsFunc=*/NULL, /*pDrawFunc=*/NULL);

	cutEdRegisterCGTInfo(pState, "Field of View", "FOVList",
		/*bMulti=*/false, /*bAlwaysCameraPlace=*/false, /*bPreventOrderChanges=*/false, /*bAllowResize=*/false, colorFromRGBA(0x2F3699FF),
		parse_CutsceneFOVList, parse_CutsceneFOVPoint, 
		cutEdMakeFOVCont, cutEdRefreshFOVUI,
		cutEdInitFOVPoint, /*pGenPntValidReferencePntFunc=*/NULL,
		/*pGetGenPntMatFunc=*/NULL, /*pSetGenPntMatFunc=*/NULL,
		/*pGetGenPntBoundsFunc=*/NULL, /*pDrawFunc=*/NULL);

	cutEdRegisterCGTInfo(pState, "Camera Shake", "ShakeList",
		/*bMulti=*/false, /*bAlwaysCameraPlace=*/false, /*bPreventOrderChanges=*/false, /*bAllowResize=*/true, colorFromRGBA(0x546D8EFF),
		parse_CutsceneShakeList, parse_CutsceneShakePoint, 
		cutEdMakeShakeCont, cutEdRefreshShakeUI,
		cutEdInitShakePoint, /*pGenPntValidReferencePntFunc=*/NULL,
		/*pGetGenPntMatFunc=*/NULL, /*pSetGenPntMatFunc=*/NULL,
		/*pGetGenPntBoundsFunc=*/NULL, /*pDrawFunc=*/NULL);

	if(!demo_playingBack()) {
		cutEdRegisterCGTInfo(pState, "Object", "ObjectList",
			/*bMulti=*/true, /*bAlwaysCameraPlace=*/false, /*bPreventOrderChanges=*/false, /*bAllowResize=*/false, colorFromRGBA(0x0000FFFF),
			parse_CutsceneObjectList, parse_CutsceneObjectPoint, 
			cutEdMakeObjectCont, cutEdRefreshObjectUI,
			/*pInitGenPntFunc=*/NULL, /*pGenPntValidReferencePntFunc=*/NULL,
			cutEdGetObjectMatrix, cutEdSetObjectMatrix,
			cutEdGetObjectBounds, cutEdDrawObjects);
	}

	if(!demo_playingBack()) {
		cutEdRegisterCGTInfo(pState, "Entity", "EntityList",
			/*bMulti=*/true, /*bAlwaysCameraPlace=*/false, /*bPreventOrderChanges=*/true, /*bAllowResize=*/true, colorFromRGBA(0xFF0000FF),
			parse_CutsceneEntityList, parse_CutsceneEntityPoint, 
			cutEdMakeEntityCont, cutEdRefreshEntityUI,
			cutEdInitEntityPoint, cutEdEntityPointIsValidReferencePoint,
			cutEdGetEntityMatrix, cutEdSetEntityMatrix,
			cutEdGetEntityBounds, cutEdDrawEntitys);
	}

	cutEdRegisterCGTInfo(pState, "Texture", "TextureList",
		/*bMulti=*/true, /*bAlwaysCameraPlace=*/false, /*bPreventOrderChanges=*/false, /*bAllowResize=*/false, colorFromRGBA(0x7F007FFF),
		parse_CutsceneTextureList, parse_CutsceneTexturePoint, 
		cutEdMakeTextureCont, cutEdRefreshTextureUI,
		/*pInitGenPntFunc=*/NULL, /*pGenPntValidReferencePntFunc=*/NULL,
		/*pGetGenPntMatFunc=*/NULL, /*pSetGenPntMatFunc=*/NULL,
		/*pGetGenPntBoundsFunc=*/NULL, cutEdDrawTextures);
	
	cutEdRegisterCGTInfo(pState, "FX", "FXLists",
		/*bMulti=*/true, /*bAlwaysCameraPlace=*/true, /*bPreventOrderChanges=*/false, /*bAllowResize=*/true, colorFromRGBA(0x008040FF),
		parse_CutsceneFXList, parse_CutsceneFXPoint, 
		cutEdMakeFXCont, cutEdRefreshFXUI,
		/*pInitGenPntFunc=*/NULL, /*pGenPntValidReferencePntFunc=*/NULL,
		cutEdGetFXMatrix, cutEdSetFXMatrix,
		cutEdGetFXBounds, cutEdDrawFX);
	
	cutEdRegisterCGTInfo(pState, "Sounds", "SoundList",
		/*bMulti=*/true, /*bAlwaysCameraPlace=*/true, /*bPreventOrderChanges=*/false, /*bAllowResize=*/true, colorFromRGBA(0xFF7E00FF),
		parse_CutsceneSoundList, parse_CutsceneSoundPoint, 
		cutEdMakeSoundCont, cutEdRefreshSoundUI,
		cutEdInitSoundPoint, /*pGenPntValidReferencePntFunc=*/NULL,
		cutEdGetSoundMatrix, cutEdSetSoundMatrix,
		cutEdGetSoundBounds, cutEdDrawSounds);

	if(!demo_playingBack()) {
		cutEdRegisterCGTInfo(pState, "Subtitles", "SubtitleList",
			/*bMulti=*/true, /*bAlwaysCameraPlace=*/false, /*bPreventOrderChanges=*/false, /*bAllowResize=*/true, colorFromRGBA(0x6F3198FF),
			parse_CutsceneSubtitleList, parse_CutsceneSubtitlePoint, 
			cutEdMakeSubtitleCont, cutEdRefreshSubtitleUI,
			/*pInitGenPntFunc=*/NULL, /*pGenPntValidReferencePntFunc=*/NULL,
			/*pGetGenPntMatFunc=*/NULL, /*pSetGenPntMatFunc=*/NULL,
			/*pGetGenPntBoundsFunc=*/NULL, /*pDrawFunc=*/NULL);

		cutEdRegisterCGTInfo(pState, "UI Gen", "UIGenList",
			/*bMulti=*/false, /*bAlwaysCameraPlace=*/false, /*bPreventOrderChanges=*/false, /*bAllowResize=*/false, colorFromRGBA(0x00FF00FF),
			parse_CutsceneUIGenList, parse_CutsceneUIGenPoint,
			cutEdMakeUIGenCont, cutEdRefreshUIGenUI,
			/*pInitGenPntFunc=*/NULL, /*pGenPntValidReferencePntFunc=*/NULL,
			/*pGetGenPntMatFunc=*/NULL, /*pSetGenPntMatFunc=*/NULL,
			/*pGetGenPntBoundsFunc=*/NULL, /*pDrawFunc=*/NULL);
	}

	cutEdReInitCGTs(pState);
	cutEdRefreshUICommon(pState);
}

void cutEdFixupMessages(CutsceneDef *pDef)
{
	int i, j;
	char buf1[RESOURCE_NAME_MAX_SIZE];
	char buf2[RESOURCE_NAME_MAX_SIZE];
	char baseMessageKey[RESOURCE_NAME_MAX_SIZE];

	sprintf(baseMessageKey, "CutsceneDef.%s", pDef->name);

	sprintf(buf1, "%s.Subtitles", baseMessageKey);
	sprintf(buf2, "This is the text for the CutsceneDef %s subtitles.", pDef->name);
	langFixupMessageWithTerseKey(pDef->Subtitles.pEditorCopy, MKP_CUTSCENESUBTITLE, buf1, buf2, NULL);

	for ( i=0; i < eaSize(&pDef->ppSubtitleLists); i++ ) {
		CutsceneSubtitleList *pList = pDef->ppSubtitleLists[i];
		for ( j=0; j < eaSize(&pList->ppSubtitlePoints); j++ ) {
			CutsceneSubtitlePoint *pPoint = pDef->ppSubtitleLists[i]->ppSubtitlePoints[j];

			sprintf(buf1, "%s.Subtitles.%s.%d", baseMessageKey, pList->common.pcName, j);
			sprintf(buf2, "This is the text for the CutsceneDef %s, subtitle track %s, subtitle %d.", pDef->name, pList->common.pcName, j);

			langFixupMessageWithTerseKey(pPoint->displaySubtitle.pEditorCopy, MKP_CUTSCENESUBTITLE, buf1, buf2, NULL);
		}
	}

	if(pDef->pUIGenList)
		for ( i=0; i < eaSize(&pDef->pUIGenList->ppUIGenPoints); i++ ) {
			CutsceneUIGenPoint *pPoint = pDef->pUIGenList->ppUIGenPoints[i];

			sprintf(buf1, "%s.Subtitles.%s.%d", baseMessageKey, pDef->pUIGenList->common.pcName, i);
			sprintf(buf2, "This is the text for the CutsceneDef %s, UIGen track %s, message %d.", pDef->name, pDef->pUIGenList->common.pcName, i);

			langFixupMessageWithTerseKey(pPoint->messageValue.pEditorCopy, MKP_CUTSCENESUBTITLE, buf1, buf2, NULL);
		}
}

void cutEdRefreshUICommon(CutsceneEditorState *pState)
{
	int i;
	CutsceneDef *pCutsceneDef = pState->pCutsceneDef;
	CutscenePath *pSelectedPath;

	editor_FillEncounterNames(&s_eaEncounterList);

	langMakeEditorCopy(parse_CutsceneDef, pCutsceneDef, true);

	pState->cutsceneTime = cutscene_GetLength(pCutsceneDef, true);
	pState->cutsceneMoveTime = cutscene_GetLength(pCutsceneDef, false);
	if(pState->pParentDoc && !demo_playingBack())
	{
		ui_TextEntrySetText(pState->pUITextMapName, pCutsceneDef->pcMapName);
		ui_CheckButtonSetState(pState->pUICheckPlayOnceOnly, pState->pCutsceneDef->bPlayOnceOnly);
		ui_CheckButtonSetState(pState->pUICheckSinglePlayer, pState->pCutsceneDef->bSinglePlayer);
		if ( ui_CheckButtonGetState(pState->pUICheckSinglePlayer) == true )
		{
			ui_CheckButtonSetState(pState->pUICheckSkippable, !pState->pCutsceneDef->bUnskippable);
			ui_SetActive(UI_WIDGET(pState->pUICheckSkippable), true);
		}
		else
		{
			ui_CheckButtonSetState(pState->pUICheckSkippable, false);
			ui_SetActive(UI_WIDGET(pState->pUICheckSkippable), false);
		}
		ui_CheckButtonSetState(pState->pUICheckHideAllPlayers, pState->pCutsceneDef->bHideAllPlayers);
		ui_CheckButtonSetState(pState->pUICheckMakePlayersUntargetable, pState->pCutsceneDef->bPlayersAreUntargetable);
		ui_CheckButtonSetState(pState->pUICheckDisableCamLight, pState->pCutsceneDef->bDisableCamLight);
		CSE_SetTextFromFloat(pState->pUITextTweenToGameCamera, pCutsceneDef->fGameCameraTweenTime);
		CSE_SetTextFromFloat(pState->pUITextTimeToFade, pCutsceneDef->fTimeToFadePlayers);
	}
	CSE_SetTextFromFloat(pState->pUITextBlendRate, pCutsceneDef->fBlendRate);
	CSE_SetTextFromFloat(pState->pUITextTotalTime, pState->cutsceneTime);
	CSE_SetTextFromFloat(pState->pUITextTotalMoveTime, pState->cutsceneMoveTime);
	CSE_SetTextFromFloat(pState->pUITextSendRange, pCutsceneDef->fMinCutSceneSendRange);

	//If we where picking an entity and anything changes we don't want to be picking one any more
	pState->bPickingEnt = 0;

	//Remove all the containers
	pState->pRemoveContFunc(pState, pState->pPointListCont);
	pState->pRemoveContFunc(pState, pState->pBasicPathCont);
	pState->pRemoveContFunc(pState, pState->pCirclePathCont);
	pState->pRemoveContFunc(pState, pState->pWatchPathCont);
	pState->pRemoveContFunc(pState, pState->pPointCont);
	pState->pRemoveContFunc(pState, pState->pRelativePosCont);
	pState->pRemoveContFunc(pState, pState->pGenPntCont);
	pState->pRemoveContFunc(pState, pState->pGenCGTCont);

	for ( i=0; i < eaSize(&pState->ppTrackInfos); i++ ) {
		CutsceneGenericTrackInfo *pInfo = pState->ppTrackInfos[i];
		pState->pRemoveContFunc(pState, pInfo->pGenPntCont);
	}

	//Reset the model for the path list in case the cut scene has changed
	ui_ListSetModel(pState->pUIListPaths, parse_CutscenePath, &pCutsceneDef->pPathList->ppPaths);

	//Refresh Common TrackData
	cutEdRefreshCGTRelPosData(pState);
	cutEdRefreshGenPntData(pState);
	cutEdRefreshGenCGTData(pState);

	//Refresh Timeline
	cutEdRefreshTimeline(pState);

	//If we have a selected path
	pSelectedPath = pState->pSelectedPath;
	if(pSelectedPath)
	{
		Vec3 Xyz, Pyr;
		CutscenePathPoint *pSelectedPoint = cutEdGetSelectedPointData(pState, Xyz, Pyr);

		pState->cutscenePathTime = cutEdGetPathLength(pCutsceneDef->pPathList, pSelectedPath, true);
		pState->cutscenePathMoveTime = cutEdGetPathLength(pCutsceneDef->pPathList, pSelectedPath, false);
		CSE_SetTextFromFloat(pState->pUITextPathTime, pState->cutscenePathTime);
		CSE_SetTextFromFloat(pState->pUITextPathMoveTime, pState->cutscenePathMoveTime);

		//Update the path list to display the correctly selected item
		ui_ListSetSelectedObject(pState->pUIListPaths, pSelectedPath);

		//Remove any invalid edit types
		ui_ComboBoxSetSelectedEnumCallback(pState->pUIComboEditType, NULL, NULL);
		ui_ComboBoxSetEnum(pState->pUIComboEditType, CutsceneEditTypeEnum, NULL, NULL);
		ui_ComboBoxSetSelectedEnumCallback(pState->pUIComboEditType, cutEdEditTypeCB, pState);
		if(!cutEdCameraModeAllowed(pSelectedPath))
		{
			if(pState->editType == CutsceneEditType_Camera)
				pState->editType = CutsceneEditType_CameraPath;
			ui_ComboBoxEnumRemoveValueInt(pState->pUIComboEditType, CutsceneEditType_Camera);
		}
		ui_ComboBoxSetSelectedEnum(pState->pUIComboEditType, pState->editType);

		//Update the point lists
		pState->pAddContFunc(pState, pState->pPointListCont);
		ui_ListSetModel(pState->pUIListPosPoints, parse_CutscenePathPoint, &pSelectedPath->ppPositions);
		ui_ListSetModel(pState->pUIListLookAtPoints, parse_CutscenePathPoint, &pSelectedPath->ppTargets);

		//Update the smoothed check boxes
		ui_CheckButtonSetState(pState->pUICheckSmoothCamPath, pSelectedPath->smoothPositions);
		ui_CheckButtonSetState(pState->pUICheckSmoothLookAtPath, pSelectedPath->smoothTargets);
		switch(pSelectedPath->type)
		{
		case CutscenePathType_Orbit:
		case CutscenePathType_LookAround:
			ui_SetActive(UI_WIDGET(pState->pUICheckSmoothCamPath), false);
			ui_SetActive(UI_WIDGET(pState->pUICheckSmoothLookAtPath), false);
			break;
		case CutscenePathType_WatchEntity:
			ui_SetActive(UI_WIDGET(pState->pUICheckSmoothCamPath), true);
			ui_SetActive(UI_WIDGET(pState->pUICheckSmoothLookAtPath), false);
			break;
		default:
			ui_SetActive(UI_WIDGET(pState->pUICheckSmoothCamPath), true);
			ui_SetActive(UI_WIDGET(pState->pUICheckSmoothLookAtPath), true);
		}

		//Insert the corresponding container for the path type
		switch(pSelectedPath->type)
		{
		case CutscenePathType_EasyPath:
		case CutscenePathType_NormalPath:
			pState->pAddContFunc(pState, pState->pBasicPathCont);
			break;
		case CutscenePathType_Orbit:
		case CutscenePathType_LookAround:
			pState->pAddContFunc(pState, pState->pCirclePathCont);
			CSE_SetTextFromFloat(pState->pUITextAngle, -1.0f*DEG(pSelectedPath->angle));
			break;
		case CutscenePathType_ShadowEntity:
		case CutscenePathType_WatchEntity:
			pState->pAddContFunc(pState, pState->pBasicPathCont);
			pState->pAddContFunc(pState, pState->pWatchPathCont);
			break;
		}
		if(	pSelectedPath->type == CutscenePathType_ShadowEntity || 
			pSelectedPath->type == CutscenePathType_WatchEntity)
		{
			if(pState->pParentDoc && !demo_playingBack())
			{
				ui_SetActive( UI_WIDGET(pState->pUITextWatchCutsceneEntName), pSelectedPath->common.main_offset.offsetType == CutsceneOffsetType_CutsceneEntity );
				ui_SetActive( UI_WIDGET(pState->pUITextWatchEncounterName), pSelectedPath->common.main_offset.offsetType == CutsceneOffsetType_Actor );
				ui_SetActive( UI_WIDGET(pState->pUITextWatchActorName), pSelectedPath->common.main_offset.offsetType == CutsceneOffsetType_Actor );
				ui_TextEntrySetText(pState->pUITextWatchCutsceneEntName, pSelectedPath->common.main_offset.pchCutsceneEntName);
				ui_TextEntrySetText(pState->pUITextWatchEncounterName, pSelectedPath->common.main_offset.pchStaticEncName);
				ui_TextEntrySetText(pState->pUITextWatchActorName, pSelectedPath->common.main_offset.pchActorName);
				ui_TextEntrySetText(pState->pUITextWatchBoneName, pSelectedPath->common.main_offset.pchBoneName);
				ui_ComboBoxSetSelectedEnum(pState->pUIComboWatchType, pSelectedPath->common.main_offset.offsetType);

				ui_CheckButtonSetState(pState->pUICheckPathTwoRelPos, pSelectedPath->common.bTwoRelativePos);
				ui_SetActive( UI_WIDGET(pState->pUIComboWatchType2), pSelectedPath->common.bTwoRelativePos );
				ui_SetActive( UI_WIDGET(pState->pUITextWatchBoneName2), pSelectedPath->common.bTwoRelativePos );
				ui_SetActive( UI_WIDGET(pState->pUITextWatchCutsceneEntName2), pSelectedPath->common.bTwoRelativePos && pSelectedPath->common.second_offset.offsetType == CutsceneOffsetType_CutsceneEntity );
				ui_SetActive( UI_WIDGET(pState->pUITextWatchEncounterName2), pSelectedPath->common.bTwoRelativePos && pSelectedPath->common.second_offset.offsetType == CutsceneOffsetType_Actor );
				ui_SetActive( UI_WIDGET(pState->pUITextWatchActorName2), pSelectedPath->common.bTwoRelativePos && pSelectedPath->common.second_offset.offsetType == CutsceneOffsetType_Actor );

				ui_TextEntrySetText(pState->pUITextWatchCutsceneEntName2, pSelectedPath->common.second_offset.pchCutsceneEntName);
				ui_TextEntrySetText(pState->pUITextWatchEncounterName2, pSelectedPath->common.second_offset.pchStaticEncName);
				ui_TextEntrySetText(pState->pUITextWatchActorName2, pSelectedPath->common.second_offset.pchActorName);
				ui_TextEntrySetText(pState->pUITextWatchBoneName2, pSelectedPath->common.second_offset.pchBoneName);
				ui_ComboBoxSetSelectedEnum(pState->pUIComboWatchType2, pSelectedPath->common.second_offset.offsetType);

				editor_FillEncounterActorNames(ui_TextEntryGetText(pState->pUITextWatchEncounterName), &pState->eaWatchActorNameModel);
				eaClear(&pState->eaWatchBoneNameModel);
				switch(pSelectedPath->common.main_offset.offsetType)
				{
					case CutsceneOffsetType_Actor:
					{
						EntityRef entref;
						if(editor_GetEncounterActorEntityRef(ui_TextEntryGetText(pState->pUITextWatchEncounterName), ui_TextEntryGetText(pState->pUITextWatchActorName), &entref))
							editor_FillEntityBoneNames(entFromEntityRefAnyPartition(entref), &pState->eaWatchBoneNameModel);
					}
					xcase CutsceneOffsetType_Player:
					{
						editor_FillEntityBoneNames(entActivePlayerPtr(), &pState->eaWatchBoneNameModel);
					}
					xcase CutsceneOffsetType_CutsceneEntity:
					{
						editor_FillEntityBoneNames(gclCutsceneGetCutsceneEntByName(pState->pCutsceneDef, ui_TextEntryGetText(pState->pUITextWatchCutsceneEntName)), &pState->eaWatchBoneNameModel);
					}
				}
				
				editor_FillEncounterActorNames(ui_TextEntryGetText(pState->pUITextWatchEncounterName2), &pState->eaWatchActorNameModel2);

				eaClear(&pState->eaWatchBoneNameModel2);
				switch(pSelectedPath->common.second_offset.offsetType)
				{
					case CutsceneOffsetType_Actor:
					{
						EntityRef entref;
						if(editor_GetEncounterActorEntityRef(ui_TextEntryGetText(pState->pUITextWatchEncounterName2), ui_TextEntryGetText(pState->pUITextWatchActorName2), &entref))
							editor_FillEntityBoneNames(entFromEntityRefAnyPartition(entref), &pState->eaWatchBoneNameModel2);
					}
					xcase CutsceneOffsetType_Player:
					{
						editor_FillEntityBoneNames(entActivePlayerPtr(), &pState->eaWatchBoneNameModel2);
					}
					xcase CutsceneOffsetType_CutsceneEntity:
					{
						editor_FillEntityBoneNames(gclCutsceneGetCutsceneEntByName(pState->pCutsceneDef, ui_TextEntryGetText(pState->pUITextWatchCutsceneEntName2)), &pState->eaWatchBoneNameModel2);
					}
				}
			}
			else
			{
				if(pSelectedPath->common.main_offset.entRef)
					CSE_SetTextFromInt(pState->pUITextWatchTarget, pSelectedPath->common.main_offset.entRef)
				else
					ui_TextEntrySetText(pState->pUITextWatchTarget, "None");
			}
		}

		//If we have a selected point
		if(pSelectedPoint)
		{
			//Insert the point editing container and refresh it
			pState->pAddContFunc(pState, pState->pPointCont);
			cutEdRefreshPointUI(pState, pSelectedPoint, Xyz, Pyr);
		}
	}
	else
	{
		ui_ListSetModel(pState->pUIListPosPoints, parse_CutscenePathPoint, NULL);
		ui_ListSetModel(pState->pUIListLookAtPoints, parse_CutscenePathPoint, NULL);
	}

	if(pState->pParentDoc)
	{
		ui_GimmeButtonSetName(pState->pFileButton, pState->pCutsceneDef->name);
		ui_GimmeButtonSetReferent(pState->pFileButton, pState->pCutsceneDef);
		ui_LabelSetText(pState->pFilenameLabel, pState->pCutsceneDef->filename);
	}
}

void cutEdReInitCGTs(CutsceneEditorState *pState)
{
	int i, j;
	cutEdClearTimelineSelection(pState);
	for ( i=0; i < eaSize(&pState->ppTrackInfos); i++ ) {
		CutsceneGenericTrackInfo *pInfo = pState->ppTrackInfos[i];

		if(pInfo->bMulti) {
			CutsceneDummyTrack ***pppLists = (CutsceneDummyTrack***)TokenStoreGetEArray(parse_CutsceneDef, pInfo->iCGTColumn, pState->pCutsceneDef, NULL);
			for ( j=0; j < eaSize(&pInfo->ppTracks); j++ ) {
				UITimelineTrack *pTrack = pInfo->ppTracks[j];
				ui_TimelineRemoveTrack(pState->pTimeline, pTrack);
				ui_TimelineTrackFree(pTrack);
			}
			eaClear(&pInfo->ppTracks);
			for ( j=0; j < eaSize(pppLists); j++ ) {
				CutsceneDummyTrack *pCGT = (*pppLists)[j];
				UITimelineTrack *pTrack = cutEdCreateTrack(pCGT->common.pcName, pInfo);
				pTrack->order = i*100 + j;
				ui_TimelineAddTrack(pState->pTimeline, pTrack);
				ui_TimelineTrackSetRightClickCallback(pTrack, cutEdTimelineAddGenPntCB, pInfo);
				ui_TimelineTrackSetLabelRightClickCallback(pTrack, cutEdTimelineGenericTrackLabelCB, pInfo);
				eaPush(&pInfo->ppTracks, pTrack);
			}
		} else {
			CutsceneDummyTrack *pCGT = TokenStoreGetPointer(parse_CutsceneDef, pInfo->iCGTColumn, pState->pCutsceneDef, 0, NULL);
			if(pInfo->pTrack) {
				ui_TimelineRemoveTrack(pState->pTimeline, pInfo->pTrack);
				ui_TimelineTrackFree(pInfo->pTrack);
				pInfo->pTrack = NULL;
			}
			if(pCGT) {
				UITimelineTrack *pTrack = cutEdCreateTrack(pCGT->common.pcName, pInfo);
				pTrack->order = i*100;
				ui_TimelineAddTrack(pState->pTimeline, pTrack);
				ui_TimelineTrackSetRightClickCallback(pTrack, cutEdTimelineAddGenPntCB, pInfo);
				ui_TimelineTrackSetLabelRightClickCallback(pTrack, cutEdTimelineGenericTrackLabelCB, pInfo);
				pInfo->pTrack = pTrack;
			}
		}
	}
}

static RotateGizmo *s_pRotateGizmo = NULL;
static TranslateGizmo *s_pTranslateGizmo = NULL;

void cutEdInitGizmos(CutsceneEditorState *pState)
{
	if(!s_pTranslateGizmo)
	{
		s_pTranslateGizmo = TranslateGizmoCreate();
		TranslateGizmoSetSpecSnap(s_pTranslateGizmo, EditSnapNone);
		TranslateGizmoSetHideGrid(s_pTranslateGizmo, true);
		TranslateGizmoSetDeactivateCallback(s_pTranslateGizmo, cutEdGizmoDeactivate);
		TranslateGizmoSetCallbackContext(s_pTranslateGizmo, pState);
	}

	if(!s_pRotateGizmo)
	{
		s_pRotateGizmo = RotateGizmoCreate();
		RotateGizmoSetDeactivateCallback(s_pRotateGizmo, cutEdGizmoDeactivate);
		RotateGizmoSetCallbackContext(s_pRotateGizmo, pState);
	}
}

RotateGizmo *cutEdRotateGizmo()
{
	return s_pRotateGizmo;
}

TranslateGizmo *cutEdTranslateGizmo()
{
	return s_pTranslateGizmo;
}

void cutEdStop(CutsceneEditorState *pState)
{
	if(!pState)
		return;

	gclCleanDynamicData(pState->pCutsceneDef);
	pState->bPlaying = false;
	ui_TimelineSetTime(pState->pTimeline, 0);

	if(!demo_playingBack())
		ui_RemoveActiveFamilies(UI_FAMILY_CUTSCENE);
}

static CutsceneEditMode gCutsceneEditMode;

CutsceneEditMode cutEdCutsceneEditMode()
{
	return gCutsceneEditMode;
}

void cutEdSetCutsceneEditMode(CutsceneEditMode mode)
{
	gCutsceneEditMode = mode;
}

void cutEdTick(CutsceneEditorState *pState)
{
	Mat4 entMat, transMat;
	CutsceneGenericTrackInfo *pInfo;
	static bool sbSelectedNew = false;
	bool bUpdating = false;
	CutscenePath *pSelectedPath;

	if(pState->bPlaying && pState->cutsceneTime)
	{
		F32 totalTime = CSE_TrackTimeToCut(ui_TimelineGetTotalTime(pState->pTimeline));
		F32 newTime = CSE_TrackTimeToCut(ui_TimelineGetTime(pState->pTimeline));
		pState->playingTimestep = gfxGetFrameTime();
		newTime += pState->playingTimestep;
		newTime = MAX(newTime, 0);
		if(newTime >= totalTime+0.5f)
			ui_ButtonClick(pState->pUIButtonStop);
		else
			ui_TimelineSetTimeAndCallback(pState->pTimeline, CSE_CutTimeToTrack(newTime));
	}
	else
	{
		pState->playingTimestep = -1;
	}

	pSelectedPath = pState->pSelectedPath;
	if(	pState->bPickingEnt && pSelectedPath && 
		(pSelectedPath->type == CutscenePathType_WatchEntity || pSelectedPath->type == CutscenePathType_ShadowEntity))
	{
		if(mouseDown(MS_LEFT) && !inpCheckHandled())
		{
			Entity *pTarget;
			bool prevSelectState;

			if(!cutEdIsEditable(pState))
				return;

			prevSelectState = g_bSelectAnyEntity;
			g_bSelectAnyEntity = true;
			pTarget = target_SelectUnderMouse(NULL, 0, 0, NULL, false, false, false);
			g_bSelectAnyEntity = prevSelectState;
			if(pTarget)
			{
				pSelectedPath->common.main_offset.entRef = pTarget->myRef;
				cutEdSetUnsaved(pState);
			}
			cutEdRefreshUICommon(pState);
			pState->bPickingEnt = false;
		}
		return;
	}

	if(sbSelectedNew && (mouseIsDown(MS_LEFT) || mouseUp(MS_LEFT)))
		inpHandled();
	else
		sbSelectedNew = false;

	pInfo = cutEdGetSelectedInfo(pState);
	if(	(pSelectedPath && pState->selectedPoint >= 0) || (pInfo && cutEdHasLocation(pInfo) && cutEdGetOrSetSelectedMatrix(pState, transMat, false)) )
	{
		if(cutEdCutsceneEditMode() == CutsceneEditMode_Translate)
		{
			Mat4 rotMat;
			TranslateGizmoUpdate(cutEdTranslateGizmo());
			TranslateGizmoDraw(cutEdTranslateGizmo());
			TranslateGizmoGetMatrix(cutEdTranslateGizmo(), transMat);
			RotateGizmoGetMatrix(cutEdRotateGizmo(), rotMat);
			copyVec3(transMat[3], rotMat[3]);
			RotateGizmoSetMatrix(cutEdRotateGizmo(), rotMat);
			bUpdating = TranslateGizmoIsActive(cutEdTranslateGizmo());
		}
		else
		{
			RotateGizmoUpdate(cutEdRotateGizmo());
			RotateGizmoDraw(cutEdRotateGizmo());
			RotateGizmoGetMatrix(cutEdRotateGizmo(), transMat);
			copyMat3(unitmat, transMat);
			TranslateGizmoSetMatrix(cutEdTranslateGizmo(), transMat);
			bUpdating = RotateGizmoIsActive(cutEdRotateGizmo());
		}
	}

	if(bUpdating)
	{
		if(!cutEdIsEditable(pState))
			bUpdating = false;
	}

	if(pSelectedPath && pState->selectedPoint >= 0) {
		if(bUpdating) {
			RotateGizmoGetMatrix(cutEdRotateGizmo(), transMat);
			cutEdReposFromGizmoToPoint(pState, false, pState->editType, transMat);
			cutEdUpdateFromMatrix(pState, transMat);
		} else if(cutEdEditingWatchPoint(pState, false, pState->editType, entMat)) {
			cutEdSetTransMats(pState);
		}
	} else if (cutEdGetOrSetSelectedMatrix(pState, transMat, false)) {
		if(!bUpdating) {
			RotateGizmoSetMatrix(cutEdRotateGizmo(), transMat);
			copyMat3(unitmat, transMat);
			TranslateGizmoSetMatrix(cutEdTranslateGizmo(), transMat);				
		} else {
			RotateGizmoGetMatrix(cutEdRotateGizmo(), transMat);
			cutEdGetOrSetSelectedMatrix(pState, transMat, true);
		}
	}

	if (!bUpdating && mouseDown(MS_LEFT) && !inpCheckHandled())
	{
		bool selectedCameraPoint = false;
		if(pSelectedPath)
		{
			int newlySelected;
			CutsceneEditType type=0;
			CutscenePath *pPath = pSelectedPath;

			newlySelected = cutEdGetMouseOverPoint(pState, pPath->ppPositions, pPath->ppTargets, &type);
			selectedCameraPoint = (newlySelected >= 0);
			if(pState->selectedPoint != newlySelected) {
				if(newlySelected >= 0 && type == CutsceneEditType_CameraPath)
					ui_ListSetSelectedRowAndCallback(pState->pUIListPosPoints, newlySelected);
				else if(newlySelected >= 0 && type == CutsceneEditType_LookAtPath)
					ui_ListSetSelectedRowAndCallback(pState->pUIListLookAtPoints, newlySelected);
				else
					cutEdSetSelectedPoint(pState, -1);

				cutEdSetTransMats(pState);
				cutEdRefreshUICommon(pState);

				if(newlySelected >= 0)
					sbSelectedNew = true;
			}
		}

		if(!selectedCameraPoint)
		{
			Vec3 start, end;
			Vec3 hit;
			F32 prevDist = 0;
			CutsceneGenericTrackInfo *pSelectedInfo = NULL;
			CutsceneDummyTrack *pSelectedCGT = NULL;
			CutsceneCommonPointData *pSelectedPoint = NULL;
			int i;

			editLibCursorRay(start, end);

			for(i = 0; i < eaSize(&pState->ppTrackInfos); i++)
			{
				pInfo = pState->ppTrackInfos[i];
				if(cutEdHasBounds(pInfo))
				{
					if(pInfo->bMulti)
					{
						CutsceneDummyTrack ***pppCGTs = (CutsceneDummyTrack***)TokenStoreGetEArray(parse_CutsceneDef, pInfo->iCGTColumn, pState->pCutsceneDef, NULL);
						int j;
						for(j = 0; j < eaSize(pppCGTs); j++)
						{
							CutsceneDummyTrack *pCGT = (*pppCGTs)[j];
							int k;
							for(k = eaSize(&pCGT->ppGenPnts) - 1; k >= 0 ; k--)
							{
								CutsceneCommonPointData *pGenPnt = pCGT->ppGenPnts[k];
								Vec3 minBounds, maxBounds, pointPos;
								if(pInfo->pGetGenPntBoundsFunc(pState, pCGT, pGenPnt, pointPos, minBounds, maxBounds))
								{
									if(lineBoxCollision(start, end, minBounds, maxBounds, hit))
									{
										F32 dist = distance3(pointPos, start);
										if(!pSelectedInfo || dist < prevDist)
										{
											prevDist = dist;
											pSelectedInfo = pInfo;
											pSelectedCGT = pCGT;
											pSelectedPoint = pGenPnt;
										}
									}
								}
							}
						}
					}
					else
					{
						CutsceneDummyTrack *pCGT = TokenStoreGetPointer(parse_CutsceneDef, pInfo->iCGTColumn, pState->pCutsceneDef, 0, NULL);
						if(pCGT)
						{
							int k;
							for(k = eaSize(&pCGT->ppGenPnts)-1; k >= 0 ; k--)
							{
								CutsceneCommonPointData *pGenPnt = pCGT->ppGenPnts[k];
								Vec3 minBounds, maxBounds, pointPos;
								if(pInfo->pGetGenPntBoundsFunc(pState, pCGT, pGenPnt, pointPos, minBounds, maxBounds))
								{
									if(lineBoxCollision(start, end, minBounds, maxBounds, hit))
									{
										F32 dist = distance3(pointPos, start);
										if(!pSelectedInfo || dist < prevDist)
										{
											prevDist = dist;
											pSelectedInfo = pInfo;
											pSelectedCGT = pCGT;
											pSelectedPoint = pGenPnt;
										}
									}
								}
							}
						}
					}
				}
			}

			cutEdClearTimelineSelection(pState);
			if(pSelectedInfo)
				cutEdAddGenPntToSelection(pState, pSelectedInfo, pSelectedCGT, pSelectedPoint);
			cutEdRefreshUICommon(pState);
		}
	}
}

void cutEdDraw(CutsceneEditorState *pState)
{
	int i, j;
	Color color1;
	CutsceneDef *pCutsceneDef = pState->pCutsceneDef;

	if(!pCutsceneDef || pState->bHidePaths)
		return;

	color1.b = color1.a = 0xFF;
	color1.r = color1.g = 0xAF;
	cutEdDrawMainCamera(pState, color1, 0.5);
	cutEdDrawTracks(pState);

	if(!pCutsceneDef->pPathList)
		return;

	for( i=0; i < eaSize(&pCutsceneDef->pPathList->ppPaths); i++ )
	{
		CutscenePath *pPath = pCutsceneDef->pPathList->ppPaths[i];

		//Draw Cam Path
		color1.b = color1.a = 0xFF;
		color1.r = color1.g = 0x7F; 
		if(pPath != pState->pSelectedPath)
			color1.a = 0x55;

		if(pPath->pCamPosSpline)
			cutEdDrawCurve(pState, pPath, pPath->pCamPosSpline, color1);
		else if(pPath->type == CutscenePathType_Orbit)
			cutEdDrawCircle(pPath->ppPositions[0]->pos, pPath->ppTargets[0]->pos, pPath->angle, color1);
		else
			cutEdDrawTightPath(pState, pPath, pPath->ppPositions, color1);

		//Draw Look At Path
		color1.r = color1.a = 0xFF;
		color1.b = color1.g = 0x6F; 
		if(pPath != pState->pSelectedPath)
			color1.a = 0x55;

		if(pPath->pCamTargetSpline)
			cutEdDrawCurve(pState, pPath, pPath->pCamTargetSpline, color1);
		else if(pPath->type == CutscenePathType_LookAround)
			cutEdDrawCircle(pPath->ppTargets[0]->pos, pPath->ppPositions[0]->pos, pPath->angle, color1);
		else
			cutEdDrawTightPath(pState, pPath, pPath->ppTargets, color1);

		color1.a = 0x77;
		if(pPath == pState->pSelectedPath)
		{
			Mat4 tempMat;
			Mat4 mat;
			Vec3 min = {-1,-1,-1};
			Vec3 max = { 1, 1, 1};
			copyMat4(unitmat, mat);
			copyMat4(unitmat, tempMat);
			color1.b = 0xFF;
			for( j=0; j < eaSize(&pPath->ppPositions); j++ )
			{
				CutscenePathPoint *pPoint = pPath->ppPositions[j];
				Vec3 camPos;
				copyVec3(pPoint->pos, tempMat[3]);
				cutEdReposFromPointToGizmo(pState, true, CutsceneEditType_CameraPath, tempMat);
				copyVec3(tempMat[3], camPos);

				if(pState->editType == CutsceneEditType_Camera && j < eaSize(&pPath->ppTargets))
				{
					Vec3 lookAtPos;
					copyVec3(pPath->ppTargets[j]->pos, tempMat[3]);
					cutEdReposFromPointToGizmo(pState, true, CutsceneEditType_LookAtPath, tempMat);
					copyVec3(tempMat[3], lookAtPos);

					if(j==pState->selectedPoint)
						color1.r = color1.g = 0xAF; 
					else
						color1.r = color1.g = 0x7F; 
					cutEdDrawCamera(pState, camPos, lookAtPos, color1, 0.50);
				}

				if(j==pState->selectedPoint && cutEdEditingCamPos(pState))
					color1.r = color1.g = 0xAF; 
				else
					color1.r = color1.g = 0x7F; 

				if(pState->editType == CutsceneEditType_CameraPath)
				{
					splineUIDrawControlPointWidget(camPos, pPoint->tangent, pPoint->up, color1, true, 0.50);
				}
				else if (pState->editType != CutsceneEditType_Camera)
				{
					copyVec3(camPos, mat[3]);
					gfxDrawBox3D(min, max, mat, color1, 0);
				}
			}
			color1.r = 0xFF;
			for( j=0; j < eaSize(&pPath->ppTargets); j++ )
			{
				CutscenePathPoint *pPoint = pPath->ppTargets[j];
				Vec3 lookAtPos;
				copyVec3(pPoint->pos, tempMat[3]);
				cutEdReposFromPointToGizmo(pState, true, CutsceneEditType_LookAtPath, tempMat);
				copyVec3(tempMat[3], lookAtPos);
				if(j==pState->selectedPoint && cutEdEditingLookAtPos(pState))
					color1.b = color1.g = 0xAF; 
				else
					color1.b = color1.g = 0x6F; 
				if(pState->editType == CutsceneEditType_LookAtPath)
					splineUIDrawControlPointWidget(lookAtPos, pPoint->tangent, pPoint->up, color1, true, 0.50);
				else
					gfxDrawSphere3D(lookAtPos, 1, 4, color1, 0);
			}
		}
	}
}

void cutEdUndo(CutsceneEditorState *pState, CSEUndoData *pData)
{
	int idx;
	int prevSelectedCGTIdx = -1;
	int prevSelectedGenPntIdx = -1;
	CutsceneGenericTrackInfo *pPrevSelectedInfo = cutEdGetSelectedInfo(pState);
	cutEdGetCGTSelectedIdx(pState, pPrevSelectedInfo, &prevSelectedCGTIdx, &prevSelectedGenPntIdx);

	idx = eaFind(&pState->pCutsceneDef->pPathList->ppPaths, pState->pSelectedPath);
	pState->pSelectedPath = NULL;

	// Put the undo cutscene into the editor
	StructDestroy(parse_CutsceneDef, pState->pCutsceneDef);
	pState->pCutsceneDef = StructClone(parse_CutsceneDef, pData->pPreCutsceneDef);
	if (pState->pNextUndoCutsceneDef) {
		StructDestroy(parse_CutsceneDef, pState->pNextUndoCutsceneDef);
	}
	pState->pNextUndoCutsceneDef = StructClone(parse_CutsceneDef, pState->pCutsceneDef);

	assert(pState->pCutsceneDef && pState->pCutsceneDef->pPathList);

	cutEdReInitCGTs(pState);

	//Restore previously selected path
	if(idx >= 0 && idx < eaSize(&pState->pCutsceneDef->pPathList->ppPaths))
		pState->pSelectedPath = pState->pCutsceneDef->pPathList->ppPaths[idx];

	//Ensure we still have a selected point
	if(pState->pSelectedPath)
	{
		CutscenePathPoint **ppPoints = cutEdGetPointsFromType(pState->pSelectedPath, (pState->editType == CutsceneEditType_Camera) ? CutsceneEditType_CameraPath : pState->editType);
		if(pState->selectedPoint >= eaSize(&ppPoints))
			pState->selectedPoint = -1;		
	}
	else
	{
		pState->selectedPoint = -1;			
	}

	eaiClear(&pState->piSelectedCamPosPoints);
	eaiClear(&pState->piSelectedLookAtPoints);
	if(pState->selectedPoint >= 0 && cutEdEditingCamPos(pState))
		eaiPush(&pState->piSelectedCamPosPoints, pState->selectedPoint);
	if(pState->selectedPoint >= 0 && cutEdEditingLookAtPos(pState))
		eaiPush(&pState->piSelectedLookAtPoints, pState->selectedPoint);

	// Reselect GenPnt
	cutEdClearTimelineSelection(pState);
	cutEdAttemptToSelect(pState, pPrevSelectedInfo, prevSelectedCGTIdx, prevSelectedGenPntIdx);

	// Update the UI
	cutEdRefreshUICommon(pState);
	cutEdSetTransMats(pState);
}

void cutEdRedo(CutsceneEditorState *pState, CSEUndoData *pData)
{
	int idx;
	int prevSelectedCGTIdx = -1;
	int prevSelectedGenPntIdx = -1;
	CutsceneGenericTrackInfo *pPrevSelectedInfo = cutEdGetSelectedInfo(pState);
	cutEdGetCGTSelectedIdx(pState, pPrevSelectedInfo, &prevSelectedCGTIdx, &prevSelectedGenPntIdx);

	idx = eaFind(&pState->pCutsceneDef->pPathList->ppPaths, pState->pSelectedPath);
	pState->pSelectedPath = NULL;

	// Put the undo cutscene into the editor
	StructDestroy(parse_CutsceneDef, pState->pCutsceneDef);
	pState->pCutsceneDef = StructClone(parse_CutsceneDef, pData->pPostCutsceneDef);
	if (pState->pNextUndoCutsceneDef) {
		StructDestroy(parse_CutsceneDef, pState->pNextUndoCutsceneDef);
	}
	pState->pNextUndoCutsceneDef = StructClone(parse_CutsceneDef, pState->pCutsceneDef);

	assert(pState->pCutsceneDef && pState->pCutsceneDef->pPathList);

	cutEdReInitCGTs(pState);

	//Restore previously selected path
	if(idx >= 0 && idx < eaSize(&pState->pCutsceneDef->pPathList->ppPaths))
		pState->pSelectedPath = pState->pCutsceneDef->pPathList->ppPaths[idx];

	//Ensure we still have a selected point
	if(pState->pSelectedPath)
	{
		CutscenePathPoint **ppPoints = cutEdGetPointsFromType(pState->pSelectedPath, (pState->editType == CutsceneEditType_Camera) ? CutsceneEditType_CameraPath : pState->editType);
		if(pState->selectedPoint >= eaSize(&ppPoints))
			pState->selectedPoint = -1;		
	}
	else
	{
		pState->selectedPoint = -1;			
	}

	eaiClear(&pState->piSelectedCamPosPoints);
	eaiClear(&pState->piSelectedLookAtPoints);
	if(pState->selectedPoint >= 0 && cutEdEditingCamPos(pState))
		eaiPush(&pState->piSelectedCamPosPoints, pState->selectedPoint);
	if(pState->selectedPoint >= 0 && cutEdEditingLookAtPos(pState))
		eaiPush(&pState->piSelectedLookAtPoints, pState->selectedPoint);

	// Reselect GenPnt
	cutEdClearTimelineSelection(pState);
	cutEdAttemptToSelect(pState, pPrevSelectedInfo, prevSelectedCGTIdx, prevSelectedGenPntIdx);

	// Update the UI
	cutEdRefreshUICommon(pState);
	cutEdSetTransMats(pState);
}
