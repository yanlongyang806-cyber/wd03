
#ifndef NO_EDITORS

AUTO_RUN_ANON(memBudgetAddMapping(__FILE__, BUDGET_Editors););


#include "AnimEditorCommon.h"

#include "dynMoveTransition.h"
#include "dynAnimTemplate.h"

#include "EditorManager.h"
#include "quat.h"
#include "CostumeCommon.h"
#include "gclCostumeView.h"
#include "gclCostumeCameraUI.h"
#include "gclCostumeUI.h"
#include "GfxCamera.h"
#include "GraphicsLib.h"
#include "WorldGrid.h"
#include "GameClientLib.h"
#include "TimedCallback.h"
#include "StringCache.h"
#include "CostumeCommonLoad.h"
#include "dynFxManager.h"
#include "Prefs.h"
#include "wlState.h"
#include "dynFxInfo.h"
#include "dynAnimChart.h"
#include "EditorManagerUtils.h"

#include "AutoGen/CostumeCommon_h_ast.h"

#include "AnimEditorCommon_h_ast.h"

#define STANDARD_ROW_HEIGHT	26
#define LABEL_ROW_HEIGHT	20
#define SEPARATOR_HEIGHT	11

#define X_OFFSET_CONTROL 125

const char *AnimEditor_SearchText = NULL;

UIWindow *AnimEditor_CheckoutWindow = NULL;

// In radians per second
#define ROTATION_SPEED PI
static F32 gYawSpeed = 0;

void AnimEditor_DrawCostume(AnimEditor_CostumePickerData* pData, F32 fDeltaTime)
{
	Vec3 v3Rot = {0, 0, 0};
	Quat qRot;

	if (!pData->pGraphics)
		return;
	// update the rotation value based on the current yaw speed
	quatToPYR(pData->pGraphics->costume.qSkelRot, v3Rot);
	v3Rot[1] += ROTATION_SPEED * gYawSpeed * fDeltaTime;
	PYRToQuat(v3Rot, qRot);

	costumeView_SetRot(pData->pGraphics, qRot);

	costumeView_Draw(pData->pGraphics);
}

void AnimEditor_DrawCostumeGhosts(CostumeViewGraphics *pGraphics, F32 fDeltaTime)
{
	GfxCameraController *gCamera = costumeView_GetCamera();
	DynNode *gRootNode = costumeView_GetRootNode();


	if (pGraphics->costume.bReset) {
		costumeView_ReinitDrawSkeleton(&pGraphics->costume);
		pGraphics->costume.bReset = 0;
	}

	if (!pGraphics->costume.pSkel) {
		// Can't draw anything useful if there's no skeleton
		return;
	}

	if (!pGraphics->costume.pCamOffset) {
		pGraphics->costume.pCamOffset = dynNodeAlloc();
	}

	// Deal with overrides
	if (pGraphics->bOverrideTime) {
		gCamera->override_time = (pGraphics->fTime > 0);
		gCamera->time_override = pGraphics->fTime;
	}
	if (pGraphics->bOverrideSky) {
		gfxCameraControllerSetSkyOverride(gCamera, pGraphics->pcSkyOverride, __FILE__);
	}

	dynNodeParent(pGraphics->costume.pCamOffset, gRootNode);
	dynNodeParent(pGraphics->costume.pSkel->pRoot, pGraphics->costume.pCamOffset);
	dynNodeSetPos(pGraphics->costume.pCamOffset, pGraphics->costume.v3SkelPos);
	dynNodeSetRot(pGraphics->costume.pCamOffset, pGraphics->costume.qSkelRot);

	if (pGraphics->costume.bPositionInCameraSpace)
	{
		Mat4 mCameraMatrix, mSkelMatrix, mWorldMatrix;
		Vec3 vPYR;
		copyVec3(gCamera->campyr, vPYR);
		vPYR[0] = vPYR[2] = 0;
		createMat3YPR(mCameraMatrix, vPYR);
		copyVec3(gCamera->camcenter, mCameraMatrix[3]);
		dynNodeGetWorldSpaceMat(pGraphics->costume.pCamOffset, mSkelMatrix, false);
		mulMat4(mCameraMatrix, mSkelMatrix, mWorldMatrix);
		dynNodeSetFromMat4(pGraphics->costume.pCamOffset, mWorldMatrix);
	}

	if (pGraphics->costume.pFxManager)
	{
		if (pGraphics->costume.bResetFX) {
			dynFxManagerUpdate(pGraphics->costume.pFxManager, DYNFXTIME(1.0));
			pGraphics->costume.bResetFX = false;
		} else {
			dynFxManagerUpdate(pGraphics->costume.pFxManager, DYNFXTIME(fDeltaTime));
		}
	}


	pGraphics->costume.pDrawSkel->bBodySock = 0;

	gfxQueueSingleDynDrawSkeleton(pGraphics->costume.pDrawSkel, pGraphics->bOverrideSky ? worldGetEditorWorldRegion() : NULL, true, true);
	FOR_EACH_IN_EARRAY(pGraphics->costume.pDrawSkel->eaSubDrawSkeletons, DynDrawSkeleton, pSub)
		gfxQueueSingleDynDrawSkeleton(pSub, pGraphics->bOverrideSky ? worldGetEditorWorldRegion() : NULL, false, true);
	FOR_EACH_END;
}


void AnimEditor_InitCostume(AnimEditor_CostumePickerData* pData)
{
	CostumeViewGraphics *pGraphics;
	Vec3 pyr = {0, 0, 0};

	pGraphics = costumeView_CreateGraphics();
	PYRToQuat(pyr, pGraphics->costume.qSkelRot);

	// Set up graphics
	if (pData->pGraphics)
	{
		costumeView_StopFx(pData->pGraphics);
		costumeView_FreeGraphics(pData->pGraphics);
	}
	pData->pGraphics = pGraphics;

	costumeView_StopFx(pData->pGraphics);
	pGraphics->costume.bReset = true;
	costumeView_RegenCostume(pData->pGraphics, (PlayerCostume*)GET_REF(pData->hCostume), NULL); // this should take a const playercostume, but i don't have time to make the whole costume system use const appropriately

	AnimEditor_UICenterCamera(NULL,pData->getCostumePickerData);
	AnimEditor_DrawCostume(pData, 0.0001f);
	pData->uiFrameCreated = wl_state.frame_count;

	if (pGraphics->costume.pFxManager) {
		FOR_EACH_IN_EARRAY(pData->eaAddedFx, const char, fxName) {
			dynFxManAddMaintainedFX(pGraphics->costume.pFxManager, fxName, NULL, 0.0f, 0, eDynFxSource_Test);
		} FOR_EACH_END;
	}

	if (pData->postCostumeChange)
		pData->postCostumeChange();
}

void AnimEditor_SetCostumeTick(TimedCallback *pTimedCallback, F32 pTimeSinceCallback, AnimEditor_CostumePickerData* pData)
{
	if (GET_REF(pData->hCostume))
	{
		pTimedCallback->remove = true;	// don't need to keep calling this function
		AnimEditor_InitCostume(pData);
	}
}

AUTO_COMMAND;
void danimClearEditorCostume(void)
{
	GamePrefClear("AnimEditorCommon\\Preview\\LastCostume");
}

void AnimEditor_SetCostume(const char* pcCostumeName, AnimEditor_GetCostumePickerData getCostumePickerData)
{
	AnimEditor_CostumePickerData* pData = getCostumePickerData?getCostumePickerData():NULL;
	assert(pData);

	if (REF_HANDLE_IS_ACTIVE(pData->hCostume))
	{
		REF_HANDLE_REMOVE(pData->hCostume);
	}

	REF_HANDLE_SET_FROM_STRING(g_hPlayerCostumeDict, pcCostumeName, pData->hCostume);

	GamePrefStoreString("AnimEditorCommon\\Preview\\LastCostume",  pcCostumeName);

	eaClear(&pData->eaAddedFx);

	if (GET_REF(pData->hCostume))
	{
		AnimEditor_InitCostume(pData);
	}
	else if (pcCostumeName)
	{
		// Wait for object to show up so we can open it
		resRequestOpenResource("PlayerCostume", pcCostumeName);
		TimedCallback_Run(AnimEditor_SetCostumeTick, pData, 0.25);
	}
}

static bool AnimEditor_CostumePicked(EMPicker* picker, EMPickerSelection** selections, AnimEditor_GetCostumePickerData getCostumePickerData)
{
	char name[256];

	if (eaSize(&selections) != 0 && selections[0]->table == parse_ResourceInfo)
	{
		ResourceInfo* entry = (ResourceInfo*)selections[0]->data;
		AnimEditor_SetCostume(entry->resourceName, getCostumePickerData);
		return true;
	}
	else if (eaSize(&selections) != 0)
	{
		getFileNameNoExt(name, selections[0]->doc_name);
		AnimEditor_SetCostume(name, getCostumePickerData);
		return true;
	}

	return false;
}

void AnimEditor_CostumePicker(UIButton* button, AnimEditor_GetCostumePickerData getCostumePickerData)
{	
	EMPicker* pPicker = emPickerGetByName("Costume Library");
	assert(pPicker);
	resSetDictionaryEditMode(g_hPlayerCostumeDict, true);
	emPickerShow(pPicker, "Select", false, AnimEditor_CostumePicked, getCostumePickerData);
}

void AnimEditor_LastCostume(UIButton* button, AnimEditor_GetCostumePickerData getCostumePickerData)
{	
	const char* pcCostumeName = GamePrefGetString("AnimEditorCommon\\Preview\\LastCostume",  NULL);
	if (pcCostumeName)
	{
		resSetDictionaryEditMode(g_hPlayerCostumeDict, true);
		AnimEditor_SetCostume(pcCostumeName, getCostumePickerData);
	}
}



void AnimEditor_UICenterCamera(UIButton *pButton, AnimEditor_GetCostumePickerData getCostumePickerData)
{
	Vec3 zeropyr = {0, 0, 0};
	GfxCameraController *pCamera;
	const PlayerCostume* pCostume = GET_REF(getCostumePickerData()->hCostume);

	if (pCostume)
	{
		pCamera = costumeView_GetCamera();
		pCamera->camdist = pCostume->fHeight/6.0 * 10.0;
		pCamera->camcenter[0] = 0;
		pCamera->camcenter[1] = pCostume->fHeight * 0.66666666;
		pCamera->camcenter[2] = 0;
		copyVec3(zeropyr,pCamera->campyr);
	}
}

void AnimEditor_UIFitCameraToPane(UIButton *pButton, AnimEditor_GetCostumePickerData getCostumePickerData)
{
	AnimEditor_CostumePickerData* pData = getCostumePickerData();
	if (pData)
	{
		const PlayerCostume* pCostume = GET_REF(pData->hCostume);
		CBox box;
		CostumeViewGraphics* pGraphics = pData->pGraphics;

		if (pCostume && pData->getPaneBox && pGraphics)
		{
			pData->getPaneBox(&box);
			// FIXME Actually fit skeleton to box.
			costumeCameraUI_FitCameraToBox(&box, false, false, false, (PlayerCostume*)pCostume, NULL, pGraphics);
		}
	}
}



static bool AnimEditor_FXPicked(EMPicker* picker, EMPickerSelection** selections, AnimEditor_GetCostumePickerData getCostumePickerData)
{
	char name[256];
	AnimEditor_CostumePickerData* pData = getCostumePickerData();
	if (pData && eaSize(&selections) > 0)
	{
		CostumeViewGraphics* pGraphics = pData->pGraphics;

		if (pGraphics && pGraphics->costume.pFxManager)
		{
			getFileNameNoExt(name, selections[0]->doc_name);
			dynFxManAddMaintainedFX(pGraphics->costume.pFxManager, name, NULL, 0.0f, 0, eDynFxSource_Test);
			eaPush(&pData->eaAddedFx,allocFindString(name));
			return true;
		}
	}
	return false;
}

void AnimEditor_UIAddTestFX(UIButton *pButton, AnimEditor_GetCostumePickerData getCostumePickerData)
{
	AnimEditor_CostumePickerData *pData = getCostumePickerData();
	if (pData && (!pData->bMoveMultiEditor || pButton->pressed))
	{
		EMPicker* pPicker = emPickerGetByName("DynFxInfo");
		assert(pPicker);
		resSetDictionaryEditMode(g_hPlayerCostumeDict, true);
		emPickerShow(pPicker, "Select", false, AnimEditor_FXPicked, getCostumePickerData);
	}
}

void AnimEditor_UIClearTestFX(UIButton *pButton, AnimEditor_GetCostumePickerData getCostumePickerData)
{
	AnimEditor_CostumePickerData* pData = getCostumePickerData();
	if (pData && (!pData->bMoveMultiEditor || pButton->pressed))
	{	
		CostumeViewGraphics* pGraphics = pData->pGraphics;
		if (pGraphics && pGraphics->costume.pFxManager)
		{
			dynFxManClearAllMaintainedFX(pGraphics->costume.pFxManager, true);
		}
		eaClear(&pData->eaAddedFx);
	}
}


/////////////////////////////////////////////


void AnimEditor_MoveMEFieldDestroy(MoveMEField* pMoveField)
{
	MEFieldDestroy(pMoveField->pField);
	free(pMoveField);
}

void AnimEditor_MoveFxMEFieldDestroy(MoveFxMEField* pMoveFxField)
{
	MEFieldDestroy(pMoveFxField->pField);
	free(pMoveFxField);
}

void AnimEditor_PathMEFieldDestroy(PathMEField *pPathField)
{
	MEFieldDestroy(pPathField->pField);
	free(pPathField);
}

static void AnimEditor_AddFieldToParent(MEField* pField, UIWidget* pParent, F32 x, F32 y, F32 xPercent, F32 w, UIUnitType wUnit, F32 padRight, void* pDoc, MEFieldPreChangeCallback FieldPreChangedCB, MEFieldChangeCallback FieldChangedCB)
{
	MEFieldAddToParent(pField, pParent, x, y);
	ui_WidgetSetPositionEx(pField->pUIWidget, x, y, xPercent, 0, UITopLeft);
	ui_WidgetSetWidthEx(pField->pUIWidget, w, wUnit);
	ui_WidgetSetPaddingEx(pField->pUIWidget, 0, padRight, 0, 0);
	MEFieldSetChangeCallback(pField, FieldChangedCB, pDoc);
	MEFieldSetPreChangeCallback(pField, FieldPreChangedCB, pDoc);
}

F32 AnimEditor_ReflowNormalNode(
	DynAnimGraph *pGraph,
	void* pDoc, int pDocFocusIndex, EMPanel* pPanel,
	MEField ***eaNodeFields, MoveMEField ***eaMoveFields, MoveFxMEField ***eaMoveFxFields, UIWidget ***eaInactiveWidgets,
	DynAnimGraphNode *pOrigNode, DynAnimGraphNode *pEditNode,
	MEFieldPreChangeCallback FieldPreChangedCB, MEFieldChangeCallback FieldChangedCB, MEFieldChangeCallback InheritFieldChangedCB,
	UIActivationFunc AddFxCB, UIActivationFunc AddFxMessageCB, UIActivationFunc ChooseFxEventCB, UIActivationFunc OpenFxEventCB, UIActivationFunc RemoveFxEventCB,
	UIActivationFunc AddImpactCB, UIActivationFunc RemoveImpactCB,
	UIActivationFunc AddMoveCB, UIActivationFunc ChooseMoveCB, MEFieldChangeCallback MoveChangeChanceCB, UIActivationFunc OpenMoveCB, UIActivationFunc RemoveMoveCB,
	UISkin *badSkin,
	bool bCacheMoveFxFields,
	F32 y
	)
{
	UILabel *pLabel;
	MEField *pField;
	UIButton* pButton;
	UISeparator* pSeparator;
	F32 x;

	x = 0.0f;
	if (!pGraph->bPartialGraph)
	{
		pField = MEFieldCreateSimple(kMEFieldType_Check, pOrigNode ? pOrigNode->pInheritBits : NULL, pEditNode->pInheritBits, parse_DynAnimGraphNodeInheritBits, "InheritInterpolation");
		AnimEditor_AddFieldToParent(pField, UI_WIDGET(emPanelGetExpander(pPanel)), x, y, 0, 20.0f, UIUnitFixed, 0, pDoc, FieldPreChangedCB, InheritFieldChangedCB);
		eaPush(eaNodeFields, pField);
		x += 20.0;
	}
	pLabel = ui_LabelCreate("Interp. Frames", x, y);
	emPanelAddChild(pPanel, pLabel, false);
	x += ui_WidgetGetWidth(UI_WIDGET(pLabel)) + 10.0f;
	pField = MEFieldCreateSimple(kMEFieldType_SliderText, pOrigNode, pEditNode, parse_DynAnimGraphNode, "Interpolation");
	AnimEditor_AddFieldToParent(pField, UI_WIDGET(emPanelGetExpander(pPanel)), x, y, 0, 1.0, UIUnitPercentage, 21, pDoc, FieldPreChangedCB, FieldChangedCB);
	ui_SliderTextEntrySetRange(pField->pUISliderText, 0, 30, 1.0f);
	MEFieldSetAndRefreshFromData(pField, pOrigNode, pEditNode);
	if (!pGraph->bPartialGraph && pEditNode->pInheritBits && pEditNode->pInheritBits->bInheritInterpolation)
		eaPush(eaInactiveWidgets, pField->pUIWidget);
	eaPush(eaNodeFields, pField);

	y += STANDARD_ROW_HEIGHT;

	x = 20.0f;
	if (!pGraph->bPartialGraph)
	{
		pField = MEFieldCreateSimple(kMEFieldType_Check, pOrigNode ? pOrigNode->pInheritBits : NULL, pEditNode->pInheritBits, parse_DynAnimGraphNodeInheritBits, "InheritEaseIn");
		AnimEditor_AddFieldToParent(pField, UI_WIDGET(emPanelGetExpander(pPanel)), x, y, 0, 20.0f, UIUnitFixed, 0, pDoc, FieldPreChangedCB, InheritFieldChangedCB);
		eaPush(eaNodeFields, pField);
		x += 20.0;
	}
	pLabel = ui_LabelCreate("Curve-In", x, y);
	emPanelAddChild(pPanel, pLabel, false);
	x += ui_WidgetGetWidth(UI_WIDGET(pLabel)) + 10.0f;
	pField = MEFieldCreateSimpleEnum(kMEFieldType_Combo, pOrigNode, pEditNode, parse_DynAnimGraphNode, "EaseIn", eEaseTypeEnum);
	AnimEditor_AddFieldToParent(pField, UI_WIDGET(emPanelGetExpander(pPanel)), x, y, 0, 1.0f, UIUnitPercentage, 21, pDoc, FieldPreChangedCB, FieldChangedCB);
	if (!pGraph->bPartialGraph && pEditNode->pInheritBits && pEditNode->pInheritBits->bInheritEaseIn)
		eaPush(eaInactiveWidgets, pField->pUIWidget);
	eaPush(eaNodeFields, pField);

	y += STANDARD_ROW_HEIGHT;

	x = 20.0f;
	if (!pGraph->bPartialGraph)
	{
		pField = MEFieldCreateSimple(kMEFieldType_Check, pOrigNode ? pOrigNode->pInheritBits : NULL, pEditNode->pInheritBits, parse_DynAnimGraphNodeInheritBits, "InheritEaseOut");
		AnimEditor_AddFieldToParent(pField, UI_WIDGET(emPanelGetExpander(pPanel)), x, y, 0, 20.0f, UIUnitFixed, 0, pDoc, FieldPreChangedCB, InheritFieldChangedCB);
		eaPush(eaNodeFields, pField);
		x += 20.0;
	}
	pLabel = ui_LabelCreate("Curve-Out", x, y);
	emPanelAddChild(pPanel, pLabel, false);
	x += ui_WidgetGetWidth(UI_WIDGET(pLabel)) + 10.0f;
	pField = MEFieldCreateSimpleEnum(kMEFieldType_Combo, pOrigNode, pEditNode, parse_DynAnimGraphNode, "EaseOut", eEaseTypeEnum);
	AnimEditor_AddFieldToParent(pField, UI_WIDGET(emPanelGetExpander(pPanel)), x, y, 0, 1.0f, UIUnitPercentage, 21, pDoc, FieldPreChangedCB, FieldChangedCB);
	if (!pGraph->bPartialGraph && pEditNode->pInheritBits && pEditNode->pInheritBits->bInheritEaseOut)
		eaPush(eaInactiveWidgets, pField->pUIWidget);
	eaPush(eaNodeFields, pField);

	y += STANDARD_ROW_HEIGHT;

	x = 0.0f;
	if (!pGraph->bPartialGraph)
	{
		pField = MEFieldCreateSimple(kMEFieldType_Check, pOrigNode ? pOrigNode->pInheritBits : NULL, pEditNode->pInheritBits, parse_DynAnimGraphNodeInheritBits, "InheritNoSelfInterp");
		AnimEditor_AddFieldToParent(pField, UI_WIDGET(emPanelGetExpander(pPanel)), x, y, 0, 20.0f, UIUnitFixed, 0, pDoc, FieldPreChangedCB, InheritFieldChangedCB);
		eaPush(eaNodeFields, pField);
		x += 20.0;
	}
	pLabel = ui_LabelCreate("No Self Interpolation", x, y);
	emPanelAddChild(pPanel, pLabel, false);
	x += ui_WidgetGetWidth(UI_WIDGET(pLabel)) + 10.0f;
	pField = MEFieldCreateSimple(kMEFieldType_Check, pOrigNode, pEditNode, parse_DynAnimGraphNode, "NoSelfInterp");
	AnimEditor_AddFieldToParent(pField, UI_WIDGET(emPanelGetExpander(pPanel)), x, y, 0, 1.0, UIUnitPercentage, 21, pDoc, FieldPreChangedCB, FieldChangedCB);
	if (!pGraph->bPartialGraph && pEditNode->pInheritBits && pEditNode->pInheritBits->bInheritNoSelfInterp)
		eaPush(eaInactiveWidgets, pField->pUIWidget);
	eaPush(eaNodeFields, pField);

	y += STANDARD_ROW_HEIGHT;

	x = 0.0f;
	if (!pGraph->bPartialGraph)
	{
		pField = MEFieldCreateSimple(kMEFieldType_Check, pOrigNode ? pOrigNode->pInheritBits : NULL, pEditNode->pInheritBits, parse_DynAnimGraphNodeInheritBits, "InheritForceEndFreeze");
		AnimEditor_AddFieldToParent(pField, UI_WIDGET(emPanelGetExpander(pPanel)), x, y, 0, 20.0f, UIUnitFixed, 0, pDoc, FieldPreChangedCB, InheritFieldChangedCB);
		eaPush(eaNodeFields, pField);
		x += 20.0;
	}
	pLabel = ui_LabelCreate("Force End Freeze", x, y);
	emPanelAddChild(pPanel, pLabel, false);
	x += ui_WidgetGetWidth(UI_WIDGET(pLabel)) + 10.0f;
	pField = MEFieldCreateSimple(kMEFieldType_Check, pOrigNode, pEditNode, parse_DynAnimGraphNode, "ForceEndFreeze");
	AnimEditor_AddFieldToParent(pField, UI_WIDGET(emPanelGetExpander(pPanel)), x, y, 0, 1.0, UIUnitPercentage, 21, pDoc, FieldPreChangedCB, FieldChangedCB);
	if (!pGraph->bPartialGraph && pEditNode->pInheritBits && pEditNode->pInheritBits->bInheritForceEndFreeze)
		eaPush(eaInactiveWidgets, pField->pUIWidget);
	eaPush(eaNodeFields, pField);

	y += STANDARD_ROW_HEIGHT;

	x = 0.0f;
	if (!pGraph->bPartialGraph)
	{
		pField = MEFieldCreateSimple(kMEFieldType_Check, pOrigNode ? pOrigNode->pInheritBits : NULL, pEditNode->pInheritBits, parse_DynAnimGraphNodeInheritBits, "InheritOverrideAllBones");
		AnimEditor_AddFieldToParent(pField, UI_WIDGET(emPanelGetExpander(pPanel)), x, y, 0, 20.0f, UIUnitFixed, 0, pDoc, FieldPreChangedCB, InheritFieldChangedCB);
		eaPush(eaNodeFields, pField);
		x += 20.0;
	}
	pLabel = ui_LabelCreate("Override All Bones (All Sequencers)", x, y);
	emPanelAddChild(pPanel, pLabel, false);
	x += ui_WidgetGetWidth(UI_WIDGET(pLabel)) + 10.0f;
	pField = MEFieldCreateSimple(kMEFieldType_Check, pOrigNode, pEditNode, parse_DynAnimGraphNode, "OverrideAllBones");
	AnimEditor_AddFieldToParent(pField, UI_WIDGET(emPanelGetExpander(pPanel)), x, y, 0, 1.0, UIUnitPercentage, 21, pDoc, FieldPreChangedCB, FieldChangedCB);
	ui_WidgetSetTooltipString(pField->pUIWidget,"Causes the animation graph node on the sequencer that set this flag to overwrite all other graph & movement based animation");
	x += 30.0;
	if (!pGraph->bPartialGraph && pEditNode->pInheritBits && pEditNode->pInheritBits->bInheritOverrideAllBones)
		eaPush(eaInactiveWidgets, pField->pUIWidget);
	eaPush(eaNodeFields, pField);
	pLabel = ui_LabelCreate("Snap", x, y);
	emPanelAddChild(pPanel, pLabel, false);
	x += ui_WidgetGetWidth(UI_WIDGET(pLabel)) + 10.0f;
	pField = MEFieldCreateSimple(kMEFieldType_Check, pOrigNode, pEditNode, parse_DynAnimGraphNode, "SnapOverrideAllBones");
	AnimEditor_AddFieldToParent(pField, UI_WIDGET(emPanelGetExpander(pPanel)), x, y, 0, 1.0, UIUnitPercentage, 21, pDoc, FieldPreChangedCB, FieldChangedCB);
	ui_WidgetSetTooltipString(pField->pUIWidget,"Instantly snap to 100% ON instead of blending");
	if (!pGraph->bPartialGraph && pEditNode->pInheritBits && pEditNode->pInheritBits->bInheritOverrideAllBones)
		eaPush(eaInactiveWidgets, pField->pUIWidget);
	eaPush(eaNodeFields, pField);

	y += STANDARD_ROW_HEIGHT;

	x = 0.0f;
	if (!pGraph->bPartialGraph)
	{
		pField = MEFieldCreateSimple(kMEFieldType_Check, pOrigNode ? pOrigNode->pInheritBits : NULL, pEditNode->pInheritBits, parse_DynAnimGraphNodeInheritBits, "InheritOverrideMovement");
		AnimEditor_AddFieldToParent(pField, UI_WIDGET(emPanelGetExpander(pPanel)), x, y, 0, 20.0f, UIUnitFixed, 0, pDoc, FieldPreChangedCB, InheritFieldChangedCB);
		eaPush(eaNodeFields, pField);
		x += 20.0;
	}
	pLabel = ui_LabelCreate("Override Movement (Legs)", x, y);
	emPanelAddChild(pPanel, pLabel, false);
	x += ui_WidgetGetWidth(UI_WIDGET(pLabel)) + 10.0f;
	pField = MEFieldCreateSimple(kMEFieldType_Check, pOrigNode, pEditNode, parse_DynAnimGraphNode, "OverrideMovement");
	AnimEditor_AddFieldToParent(pField, UI_WIDGET(emPanelGetExpander(pPanel)), x, y, 0, 1.0, UIUnitPercentage, 21, pDoc, FieldPreChangedCB, FieldChangedCB);
	ui_WidgetSetTooltipString(pField->pUIWidget,"Causes the animation for this graph node to show-up on the legs (blendinfo movement tagged bones) while moving");
	x += 30.0f;
	if (!pGraph->bPartialGraph && pEditNode->pInheritBits && pEditNode->pInheritBits->bInheritOverrideMovement)
		eaPush(eaInactiveWidgets, pField->pUIWidget);
	eaPush(eaNodeFields, pField);
	pLabel = ui_LabelCreate("Snap", x, y);
	emPanelAddChild(pPanel, pLabel, false);
	x += ui_WidgetGetWidth(UI_WIDGET(pLabel)) + 10.0f;
	pField = MEFieldCreateSimple(kMEFieldType_Check, pOrigNode, pEditNode, parse_DynAnimGraphNode, "SnapOverrideMovement");
	AnimEditor_AddFieldToParent(pField, UI_WIDGET(emPanelGetExpander(pPanel)), x, y, 0, 1.0, UIUnitPercentage, 21, pDoc, FieldPreChangedCB, FieldChangedCB);
	ui_WidgetSetTooltipString(pField->pUIWidget,"Instantly snap to 100% ON instead of blending");
	if (!pGraph->bPartialGraph && pEditNode->pInheritBits && pEditNode->pInheritBits->bInheritOverrideMovement)
		eaPush(eaInactiveWidgets, pField->pUIWidget);
	eaPush(eaNodeFields, pField);

	y += STANDARD_ROW_HEIGHT;

	x = 0.0f;
	if (!pGraph->bPartialGraph)
	{
		pField = MEFieldCreateSimple(kMEFieldType_Check, pOrigNode ? pOrigNode->pInheritBits : NULL, pEditNode->pInheritBits, parse_DynAnimGraphNodeInheritBits, "InheritOverrideDefaultMove");
		AnimEditor_AddFieldToParent(pField, UI_WIDGET(emPanelGetExpander(pPanel)), x, y, 0, 20.0f, UIUnitFixed, 0, pDoc, FieldPreChangedCB, InheritFieldChangedCB);
		eaPush(eaNodeFields, pField);
		x += 20.0;
	}
	pLabel = ui_LabelCreate("Override Default Movement (Torso)", x, y);
	emPanelAddChild(pPanel, pLabel, false);
	x += ui_WidgetGetWidth(UI_WIDGET(pLabel)) + 10.0f;
	pField = MEFieldCreateSimple(kMEFieldType_Check, pOrigNode, pEditNode, parse_DynAnimGraphNode, "OverrideDefaultMove");
	AnimEditor_AddFieldToParent(pField, UI_WIDGET(emPanelGetExpander(pPanel)), x, y, 0, 1.0, UIUnitPercentage, 21, pDoc, FieldPreChangedCB, FieldChangedCB);
	ui_WidgetSetTooltipString(pField->pUIWidget,"Causes a default graph node's animation (your basic idles) to show-up on the upper-body (non-blendinfo movement tagged bones) while moving");
	x += 30.0f;
	if (!pGraph->bPartialGraph && pEditNode->pInheritBits && pEditNode->pInheritBits->bInheritOverrideDefaultMove)
		eaPush(eaInactiveWidgets, pField->pUIWidget);
	eaPush(eaNodeFields, pField);
	pLabel = ui_LabelCreate("Snap", x, y);
	emPanelAddChild(pPanel, pLabel, false);
	x += ui_WidgetGetWidth(UI_WIDGET(pLabel)) + 10.0f;
	pField = MEFieldCreateSimple(kMEFieldType_Check, pOrigNode, pEditNode, parse_DynAnimGraphNode, "SnapOverrideDefaultMove");
	AnimEditor_AddFieldToParent(pField, UI_WIDGET(emPanelGetExpander(pPanel)), x, y, 0, 1.0, UIUnitPercentage, 21, pDoc, FieldPreChangedCB, FieldChangedCB);
	ui_WidgetSetTooltipString(pField->pUIWidget,"Instantly snap to 100% ON instead of blending");
	if (!pGraph->bPartialGraph && pEditNode->pInheritBits && pEditNode->pInheritBits->bInheritOverrideDefaultMove)
		eaPush(eaInactiveWidgets, pField->pUIWidget);
	eaPush(eaNodeFields, pField);

	y += STANDARD_ROW_HEIGHT;

	x = 0.0f;
	if (!pGraph->bPartialGraph)
	{
		pField = MEFieldCreateSimple(kMEFieldType_Check, pOrigNode ? pOrigNode->pInheritBits : NULL, pEditNode->pInheritBits, parse_DynAnimGraphNodeInheritBits, "InheritAllowRandomMoveRepeats");
		AnimEditor_AddFieldToParent(pField, UI_WIDGET(emPanelGetExpander(pPanel)), x, y, 0, 20.0f, UIUnitFixed, 0, pDoc, FieldPreChangedCB, InheritFieldChangedCB);
		eaPush(eaNodeFields, pField);
		x += 20.0;
	}
	pLabel = ui_LabelCreate("Allow Random Move Repeats", x, y);
	emPanelAddChild(pPanel, pLabel, false);
	x += ui_WidgetGetWidth(UI_WIDGET(pLabel)) + 10.0f;
	pField = MEFieldCreateSimple(kMEFieldType_Check, pOrigNode, pEditNode, parse_DynAnimGraphNode, "AllowRandomMoveRepeats");
	AnimEditor_AddFieldToParent(pField, UI_WIDGET(emPanelGetExpander(pPanel)), x, y, 0, 1.0, UIUnitPercentage, 21, pDoc, FieldPreChangedCB, FieldChangedCB);
	x += 30.0f;
	if (!pGraph->bPartialGraph && pEditNode->pInheritBits && pEditNode->pInheritBits->bInheritAllowRandomMoveRepeats)
		eaPush(eaInactiveWidgets, pField->pUIWidget);
	eaPush(eaNodeFields, pField);

	y += STANDARD_ROW_HEIGHT;

	pSeparator = ui_SeparatorCreate(UIHorizontal);
	ui_WidgetSetPosition(UI_WIDGET(pSeparator), 0, y);
	emPanelAddChild(pPanel, pSeparator, false);

	y += SEPARATOR_HEIGHT;

	y += 20;

	pSeparator = ui_SeparatorCreate(UIHorizontal);
	ui_WidgetSetPosition(UI_WIDGET(pSeparator), 0, y);
	emPanelAddChild(pPanel, pSeparator, false);

	y += SEPARATOR_HEIGHT;

	if (!pGraph->bPartialGraph)
	{
		//Bone Vis Sets, only display on graphs not templates
		x = 0;
		pLabel = ui_LabelCreate("Bone Visibility Set", x, y);
		emPanelAddChild(pPanel, pLabel, false);
		x += ui_WidgetGetWidth(UI_WIDGET(pLabel)) + 10.0f;
		pField = MEFieldCreateSimpleEnum(kMEFieldType_Combo, pOrigNode, pEditNode, parse_DynAnimGraphNode, "BoneVisSet", SkelBoneVisibilitySetEnum);
		AnimEditor_AddFieldToParent(pField, UI_WIDGET(emPanelGetExpander(pPanel)), x, y, 0, 1.0, UIUnitPercentage, 0, pDoc, FieldPreChangedCB, FieldChangedCB);
		eaPush(eaNodeFields, pField);
		y += STANDARD_ROW_HEIGHT;

		pSeparator = ui_SeparatorCreate(UIHorizontal);
		ui_WidgetSetPosition(UI_WIDGET(pSeparator), 0, y);
		emPanelAddChild(pPanel, pSeparator, false);

		y += SEPARATOR_HEIGHT;
		y += 20;

		pSeparator = ui_SeparatorCreate(UIHorizontal);
		ui_WidgetSetPosition(UI_WIDGET(pSeparator), 0, y);
		emPanelAddChild(pPanel, pSeparator, false);
	}

	y += SEPARATOR_HEIGHT;
	x = 0.0f;
	if (!pGraph->bPartialGraph)
	{
		pField = MEFieldCreateSimple(kMEFieldType_Check, pOrigNode ? pOrigNode->pInheritBits : NULL, pEditNode->pInheritBits, parse_DynAnimGraphNodeInheritBits, "InheritDisableTorsoPointing");
		AnimEditor_AddFieldToParent(pField, UI_WIDGET(emPanelGetExpander(pPanel)), x, y, 0, 20.0f, UIUnitFixed, 0, pDoc, FieldPreChangedCB, InheritFieldChangedCB);
		eaPush(eaNodeFields, pField);
		x += 20.0;
	}
	pLabel = ui_LabelCreate("Timeout: Disable Torso Pointing", x, y);
	emPanelAddChild(pPanel, pLabel, false);
	x += ui_WidgetGetWidth(UI_WIDGET(pLabel)) + 10.0f;
	pField = MEFieldCreateSimple(kMEFieldType_TextEntry, pOrigNode, pEditNode, parse_DynAnimGraphNode, "DisableTorsoPointingTimeout");
	AnimEditor_AddFieldToParent(pField, UI_WIDGET(emPanelGetExpander(pPanel)), x, y, 0, 1.0, UIUnitPercentage, 21, pDoc, FieldPreChangedCB, FieldChangedCB);
	x += 30.0f;
	if (!pGraph->bPartialGraph && pEditNode->pInheritBits && pEditNode->pInheritBits->bInheritDisableTorsoPointing)
		eaPush(eaInactiveWidgets, pField->pUIWidget);
	eaPush(eaNodeFields, pField);

	y += STANDARD_ROW_HEIGHT;

	x = 0.0f;
	if (!pGraph->bPartialGraph)
	{
		pField = MEFieldCreateSimple(kMEFieldType_Check, pOrigNode ? pOrigNode->pInheritBits : NULL, pEditNode->pInheritBits, parse_DynAnimGraphNodeInheritBits, "InheritDisableGroundReg");
		AnimEditor_AddFieldToParent(pField, UI_WIDGET(emPanelGetExpander(pPanel)), x, y, 0, 20.0f, UIUnitFixed, 0, pDoc, FieldPreChangedCB, InheritFieldChangedCB);
		eaPush(eaNodeFields, pField);
		x += 20.0;
	}
	pLabel = ui_LabelCreate("Timeout: Disable Ground Reg.", x, y);
	emPanelAddChild(pPanel, pLabel, false);
	x += ui_WidgetGetWidth(UI_WIDGET(pLabel)) + 10.0f;
	pField = MEFieldCreateSimple(kMEFieldType_TextEntry, pOrigNode, pEditNode, parse_DynAnimGraphNode, "DisableGroundRegTimeout");
	AnimEditor_AddFieldToParent(pField, UI_WIDGET(emPanelGetExpander(pPanel)), x, y, 0, 1.0, UIUnitPercentage, 21, pDoc, FieldPreChangedCB, FieldChangedCB);
	x += 30.0f;
	if (!pGraph->bPartialGraph && pEditNode->pInheritBits && pEditNode->pInheritBits->bInheritDisableTorsoPointing)
		eaPush(eaInactiveWidgets, pField->pUIWidget);
	eaPush(eaNodeFields, pField);

	x = 0;
	y += 1.5*STANDARD_ROW_HEIGHT;
	pLabel = ui_LabelCreate("Note: Timeouts are measured in seconds from start of graph node", x, y);
	emPanelAddChild(pPanel, pLabel, false);
	y += STANDARD_ROW_HEIGHT;
	pLabel = ui_LabelCreate("and are only active when greater than Zero", x, y);
	emPanelAddChild(pPanel, pLabel, false);

	y += STANDARD_ROW_HEIGHT;

	pSeparator = ui_SeparatorCreate(UIHorizontal);
	ui_WidgetSetPosition(UI_WIDGET(pSeparator), 0, y);
	emPanelAddChild(pPanel, pSeparator, false);

	y += SEPARATOR_HEIGHT;

	y += 20;

	pSeparator = ui_SeparatorCreate(UIHorizontal);
	ui_WidgetSetPosition(UI_WIDGET(pSeparator), 0, y);
	emPanelAddChild(pPanel, pSeparator, false);

	y += SEPARATOR_HEIGHT;

	x = 0.0f;
	if (!pGraph->bPartialGraph)
	{
		pField = MEFieldCreateSimple(kMEFieldType_Check, pOrigNode ? pOrigNode->pInheritBits : NULL, pEditNode->pInheritBits, parse_DynAnimGraphNodeInheritBits, "InheritGraphImpact");
		AnimEditor_AddFieldToParent(pField, UI_WIDGET(emPanelGetExpander(pPanel)), x, y, 0, 20.0f, UIUnitFixed, 0, pDoc, FieldPreChangedCB, InheritFieldChangedCB);
		eaPush(eaNodeFields, pField);
		x += 20.0;
	}
	pLabel = ui_LabelCreate("Impact Triggers", x, y);
	emPanelAddChild(pPanel, pLabel, false);
	x += ui_WidgetGetWidth(UI_WIDGET(pLabel)) + 10.0f;
	pButton = ui_ButtonCreate("Add Impact Trigger", x, y, AddImpactCB, pEditNode);
	emPanelAddChild(pPanel, pButton, false);
	if (!pGraph->bPartialGraph && pEditNode->pInheritBits && pEditNode->pInheritBits->bInheritGraphImpact)
		eaPush(eaInactiveWidgets, UI_WIDGET(pButton));
	x = 0.0f;

	y += STANDARD_ROW_HEIGHT;

	pSeparator = ui_SeparatorCreate(UIHorizontal);
	ui_WidgetSetPosition(UI_WIDGET(pSeparator), 0, y);
	emPanelAddChild(pPanel, pSeparator, false);

	y += SEPARATOR_HEIGHT;

	FOR_EACH_IN_EARRAY_FORWARDS(pEditNode->eaGraphImpact, DynAnimGraphTriggerImpact, pImpact)
	{
		DynAnimGraphTriggerImpact* pOrigImpact = (pOrigNode && ipImpactIndex < eaSize(&pOrigNode->eaGraphImpact))?pOrigNode->eaGraphImpact[ipImpactIndex]:NULL;

		x = 10.0f;
		pButton = ui_ButtonCreate("X", x, y, RemoveImpactCB, pImpact);
		ui_WidgetSetPositionEx(UI_WIDGET(pButton), x, y, 0.0f, 0.0f, UITopRight);
		emPanelAddChild(pPanel, pButton, false);
		x += ui_WidgetGetWidth( UI_WIDGET(pButton) ) + 5;
		if (!pGraph->bPartialGraph && pEditNode->pInheritBits && pEditNode->pInheritBits->bInheritGraphImpact)
			eaPush(eaInactiveWidgets, UI_WIDGET(pButton));

		// Frame entry box
		pLabel = ui_LabelCreate("Frame", 5, y);
		x = ui_WidgetGetWidth( UI_WIDGET(pLabel) ) + 10;
		emPanelAddChild(pPanel, pLabel, false);
		pField = MEFieldCreateSimple(kMEFieldType_TextEntry, pOrigImpact, pImpact, parse_DynAnimGraphTriggerImpact, "Frame");
		AnimEditor_AddFieldToParent(pField, UI_WIDGET(emPanelGetExpander(pPanel)), x, y, 0, 50, UIUnitFixed, 0, pDoc, FieldPreChangedCB, FieldChangedCB);
		if (!pGraph->bPartialGraph && pEditNode->pInheritBits && pEditNode->pInheritBits->bInheritGraphImpact)
			eaPush(eaInactiveWidgets, pField->pUIWidget);
		eaPush(eaNodeFields, pField);

		y += STANDARD_ROW_HEIGHT;

		// Bone entry box
		pLabel = ui_LabelCreate("Bone", 15, y);
		emPanelAddChild(pPanel, pLabel, false);
		x = ui_WidgetGetWidth( UI_WIDGET(pLabel) ) + 20;
		pField = MEFieldCreateSimple(kMEFieldType_TextEntry, pOrigImpact, pImpact, parse_DynAnimGraphTriggerImpact, "Bone");
		AnimEditor_AddFieldToParent(pField, UI_WIDGET(emPanelGetExpander(pPanel)), x, y, 0, 1.0, UIUnitPercentage, 0, pDoc, FieldPreChangedCB, FieldChangedCB);
		if (!pGraph->bPartialGraph && pEditNode->pInheritBits && pEditNode->pInheritBits->bInheritGraphImpact)
			eaPush(eaInactiveWidgets, pField->pUIWidget);
		eaPush(eaNodeFields, pField);

		y += STANDARD_ROW_HEIGHT;

		// Direction entry box
		pLabel = ui_LabelCreate("Direction", 15, y);
		emPanelAddChild(pPanel, pLabel, false);
		x = ui_WidgetGetWidth( UI_WIDGET(pLabel) ) + 20;
		pField = MEFieldCreateSimpleEnum(kMEFieldType_Combo, pOrigImpact, pImpact, parse_DynAnimGraphTriggerImpact, "Direction", DynAnimGraphImpactDirectionEnum);
		AnimEditor_AddFieldToParent(pField, UI_WIDGET(emPanelGetExpander(pPanel)), x, y, 0, 1.0, UIUnitPercentage, 0, pDoc, FieldPreChangedCB, FieldChangedCB);
		if (!pGraph->bPartialGraph && pEditNode->pInheritBits && pEditNode->pInheritBits->bInheritGraphImpact)
			eaPush(eaInactiveWidgets, pField->pUIWidget);
		eaPush(eaNodeFields, pField);

		y += STANDARD_ROW_HEIGHT;

		pSeparator = ui_SeparatorCreate(UIHorizontal);
		ui_WidgetSetPosition(UI_WIDGET(pSeparator), 0, y);
		emPanelAddChild(pPanel, pSeparator, false);

		y += SEPARATOR_HEIGHT;
	}
	FOR_EACH_END;

	y += 20;

	pSeparator = ui_SeparatorCreate(UIHorizontal);
	ui_WidgetSetPosition(UI_WIDGET(pSeparator), 0, y);
	emPanelAddChild(pPanel, pSeparator, false);

	y += SEPARATOR_HEIGHT;

	if (!pGraph->bPartialGraph)
	{
		x = 0.0f;

		pLabel = ui_LabelCreate("Moves", x, y);
		emPanelAddChild(pPanel, pLabel, false);
		x += ui_WidgetGetWidth(UI_WIDGET(pLabel)) + 10.0f;
		pButton = ui_ButtonCreate("Add Move", x, y, AddMoveCB, pEditNode);
		emPanelAddChild(pPanel, pButton, false);

		y += STANDARD_ROW_HEIGHT;

		pSeparator = ui_SeparatorCreate(UIHorizontal);
		ui_WidgetSetPosition(UI_WIDGET(pSeparator), 0, y);
		emPanelAddChild(pPanel, pSeparator, false);

		y += SEPARATOR_HEIGHT;

		x = 0.0f;
		FOR_EACH_IN_EARRAY_FORWARDS(pEditNode->eaMove, DynAnimGraphMove, pMove)
		{
			DynAnimGraphMove* pOrigMove = (pOrigNode && ipMoveIndex < eaSize(&pOrigNode->eaMove))?pOrigNode->eaMove[ipMoveIndex]:NULL;

			x = 10.0f;
			pButton = ui_ButtonCreate("X", x, y, RemoveMoveCB, pMove);
			ui_WidgetSetPositionEx(UI_WIDGET(pButton), x, y, 0.0f, 0.0f, UITopRight);
			emPanelAddChild(pPanel, pButton, false);
			x += ui_WidgetGetWidth( UI_WIDGET(pButton) ) + 5;

			pButton = ui_ButtonCreate("Open", x, y, OpenMoveCB, pMove);
			ui_WidgetSetPositionEx(UI_WIDGET(pButton), x, y, 0.0f, 0.0f, UITopRight);
			emPanelAddChild(pPanel, pButton, false);
			x += ui_WidgetGetWidth( UI_WIDGET(pButton) ) + 5;

			pButton = ui_ButtonCreate(GET_REF(pMove->hMove)?REF_STRING_FROM_HANDLE(pMove->hMove):"Invalid Move", x, y, ChooseMoveCB, pMove); 
			if (!GET_REF(pMove->hMove))
				ui_WidgetSkin(UI_WIDGET(pButton), badSkin);
			emPanelAddChild(pPanel, pButton, false);
			ui_WidgetSetPositionEx(UI_WIDGET(pButton), x, y, 0.0f, 0.0f, UITopRight);
			ui_WidgetSetWidthEx(UI_WIDGET(pButton), 0.98, UIUnitPercentage);

			y += STANDARD_ROW_HEIGHT;

			pLabel = ui_LabelCreate("Chance", x, y);
			emPanelAddChild(pPanel, pLabel, false);
			x += ui_WidgetGetWidth(UI_WIDGET(pLabel)) + 10;
			pField = MEFieldCreateSimple(kMEFieldType_SliderText, pOrigMove, pMove, parse_DynAnimGraphMove, "Chance");
			AnimEditor_AddFieldToParent(pField, UI_WIDGET(emPanelGetExpander(pPanel)), x, y, 0, 1.0, UIUnitPercentage, 21, pDoc, FieldPreChangedCB, FieldChangedCB);
			MEFieldSetChangeCallback(pField, MoveChangeChanceCB, pDoc);
			ui_SliderTextEntrySetRange(pField->pUISliderText, 0, 1, 1/20.0f);
			if (eaSize(&pEditNode->eaMove) == 1)
				eaPush(eaInactiveWidgets, pField->pUIWidget);
			if (GET_REF(pMove->hMove) &&
				!dynAnimGraphGroupMoveVerifyChance(pGraph, pEditNode, false))
				ui_WidgetSkin(pField->pUIWidget, badSkin);
			{
				MoveMEField* pMoveField = calloc(sizeof(MoveMEField), 1);
				pMoveField->pField = pField;
				pMoveField->iNodeIndex = pDocFocusIndex;
				pMoveField->iMoveIndex = ipMoveIndex;
				eaPush(eaMoveFields, pMoveField);
			}

			y += STANDARD_ROW_HEIGHT;

			x = 10.0f;

			pLabel = ui_LabelCreate("FX", x, y);
			emPanelAddChild(pPanel, pLabel, false);
			x += ui_WidgetGetWidth(UI_WIDGET(pLabel)) + 10.0f;

			pButton = ui_ButtonCreate("Add FX Message", x, y, AddFxMessageCB, pMove);
			emPanelAddChild(pPanel, pButton, false);
			x += ui_WidgetGetWidth(UI_WIDGET(pButton)) + 10.0f;
			
			pButton = ui_ButtonCreate("Add FX", x, y, AddFxCB, pMove);
			emPanelAddChild(pPanel, pButton, false);
			
			y += STANDARD_ROW_HEIGHT;

			FOR_EACH_IN_EARRAY_FORWARDS(pMove->eaFxEvent, DynAnimGraphFxEvent, pFxEvent)
			{
				DynAnimGraphFxEvent* pOrigEvent = (pOrigMove && ipFxEventIndex < eaSize(&pOrigMove->eaFxEvent))?pOrigMove->eaFxEvent[ipFxEventIndex]:NULL;
				UIWidget *pOptWidget = NULL;
				bool bValid;
				y += SEPARATOR_HEIGHT;
				x = 10.0f;

				pButton = ui_ButtonCreate("X", x, y, RemoveFxEventCB, pFxEvent);
				ui_WidgetSetPositionEx(UI_WIDGET(pButton), x, y, 0.0f, 0.0f, UITopRight);
				emPanelAddChild(pPanel, pButton, false);
				x += ui_WidgetGetWidth( UI_WIDGET(pButton) ) + 5;

				if (!pFxEvent->bMessage)
				{
					pButton = ui_ButtonCreate("Open", x, y, OpenFxEventCB, pFxEvent);
					ui_WidgetSetPositionEx(UI_WIDGET(pButton), x, y, 0.0f, 0.0f, UITopRight);
					emPanelAddChild(pPanel, pButton, false);
					x += ui_WidgetGetWidth( UI_WIDGET(pButton) ) + 5;

					// FX picker
					pButton = ui_ButtonCreate(pFxEvent->pcFx?pFxEvent->pcFx:"Choose FX", x, y, ChooseFxEventCB, pFxEvent);
					ANALYSIS_ASSUME(pFxEvent->pcFx != NULL); // I'm not sure about this one.
					if (!dynAnimGraphIndividualMoveVerifyFx(pGraph, pEditNode, pMove, pFxEvent, false))
						ui_WidgetSkin(UI_WIDGET(pButton), badSkin);
					emPanelAddChild(pPanel, pButton, false);
					ui_WidgetSetPositionEx(UI_WIDGET(pButton), 20, y, 0.0f, 0.0f, UITopLeft);
					ui_WidgetSetPaddingEx(UI_WIDGET(pButton), 0, x, 0, 0);
					ui_WidgetSetWidthEx(UI_WIDGET(pButton), 1.0, UIUnitPercentage);
					pOptWidget = UI_WIDGET(pButton);
				}
				else
				{
					float xpad = x;
					// Just text entry box
					pLabel = ui_LabelCreate("Message", 20, y);
					x = ui_WidgetGetWidth( UI_WIDGET(pLabel) ) + 25;
					emPanelAddChild(pPanel, pLabel, false);
					pField = MEFieldCreateSimple(kMEFieldType_TextEntry, pOrigEvent, pFxEvent, parse_DynAnimGraphFxEvent, "Fx");
					AnimEditor_AddFieldToParent(pField, UI_WIDGET(emPanelGetExpander(pPanel)), x, y, 0, 1.0, UIUnitPercentage, xpad, pDoc, FieldPreChangedCB, FieldChangedCB);
					if (!dynAnimGraphIndividualMoveVerifyFx(pGraph, pEditNode, pMove, pFxEvent, false))
						ui_WidgetSkin(pField->pUIWidget, badSkin);
					eaPush(eaNodeFields, pField);
					pOptWidget = pField->pUIWidget;
				}

				y += STANDARD_ROW_HEIGHT;

				// Frame entry box
				pLabel = ui_LabelCreate("Frame", 20, y);
				emPanelAddChild(pPanel, pLabel, false);
				x = ui_WidgetGetWidth( UI_WIDGET(pLabel) ) + 25;
				pField = MEFieldCreateSimple(kMEFieldType_TextEntry, pOrigEvent, pFxEvent, parse_DynAnimGraphFxEvent, "Frame");
				AnimEditor_AddFieldToParent(pField, UI_WIDGET(emPanelGetExpander(pPanel)), x, y, 0, 50, UIUnitFixed, 0, pDoc, FieldPreChangedCB, FieldChangedCB);
				if (!dynAnimGraphIndividualMoveVerifyFx(pGraph, pEditNode, pMove, pFxEvent, false)) {
					bValid = false;
					ui_WidgetSkin(pField->pUIWidget, badSkin);
				} else {
					bValid = true;
				}
				if (bCacheMoveFxFields)
				{
					MoveFxMEField* pMoveFxField = calloc(sizeof(MoveFxMEField), 1);
					pMoveFxField->pField = pField;
					pMoveFxField->pOptWidget = pOptWidget;
					pMoveFxField->iNodeIndex = pDocFocusIndex;
					pMoveFxField->iMoveIndex = ipMoveIndex;
					pMoveFxField->iFxIndex = ipFxEventIndex;
					pMoveFxField->bValid = bValid;
					eaPush(eaMoveFxFields, pMoveFxField);
				}

				y += STANDARD_ROW_HEIGHT;
			}
			FOR_EACH_END;

			pSeparator = ui_SeparatorCreate(UIHorizontal);
			ui_WidgetSetPosition(UI_WIDGET(pSeparator), 0, y);
			emPanelAddChild(pPanel, pSeparator, false);

			y += SEPARATOR_HEIGHT;
		}
		FOR_EACH_END;
	}

	return y;
}

F32 AnimEditor_ReflowRandomizerNode(
	DynAnimGraph *pGraph,
	void* pDoc, int pDocFocusIndex, EMPanel* pPanel,
	MEField ***eaNodeFields, PathMEField ***eaPathFields, UIWidget ***eaInactiveWidgets,
	DynAnimGraphNode *pOrigNode, DynAnimGraphNode *pEditNode,
	MEFieldPreChangeCallback FieldPreChangedCB, MEFieldChangeCallback FieldChangedCB, MEFieldChangeCallback InheritFieldChangedCB,
	UIActivationFunc AddPathCB, MEFieldChangeCallback PathChangeChanceCB, UIActivationFunc RemovePathCB,
	UISkin *badSkin,
	F32 y
	)
{
	UILabel *pLabel;
	MEField *pField;
	UIButton* pButton;
	UISeparator* pSeparator;
	F32 x;

	x = 0.0f;
	if (!pGraph->bPartialGraph)
	{
		pField = MEFieldCreateSimple(kMEFieldType_Check, pOrigNode ? pOrigNode->pInheritBits : NULL, pEditNode->pInheritBits, parse_DynAnimGraphNodeInheritBits, "InheritPath");
		AnimEditor_AddFieldToParent(pField, UI_WIDGET(emPanelGetExpander(pPanel)), x, y, 0, 20.0f, UIUnitFixed, 0, pDoc, FieldPreChangedCB, InheritFieldChangedCB);
		eaPush(eaNodeFields, pField);
		x += 20.0;
	}
	pLabel = ui_LabelCreate("Paths", x, y);
	emPanelAddChild(pPanel, pLabel, false);
	x += ui_WidgetGetWidth(UI_WIDGET(pLabel)) + 10.0f;

	y += STANDARD_ROW_HEIGHT;

	pSeparator = ui_SeparatorCreate(UIHorizontal);
	ui_WidgetSetPosition(UI_WIDGET(pSeparator), 0, y);
	emPanelAddChild(pPanel, pSeparator, false);

	y += SEPARATOR_HEIGHT;

	FOR_EACH_IN_EARRAY_FORWARDS(pEditNode->eaPath, DynAnimGraphPath, pPath)
	{
		DynAnimGraphPath *pOrigPath = (pOrigNode && ipPathIndex < eaSize(&pOrigNode->eaPath))?pOrigNode->eaPath[ipPathIndex]:NULL;
		char cChanceName[16];
		F32 x1, x2;
		
		sprintf(cChanceName, "Path %d", ipPathIndex+1);

		x1 = 10.0f;
		pLabel = ui_LabelCreate(cChanceName, x1, y);
		ui_WidgetSetPositionEx(UI_WIDGET(pLabel), x1, y, 0.0f, 0.0f, UITopLeft);
		emPanelAddChild(pPanel, pLabel, false);
		x1 += ui_WidgetGetWidth(UI_WIDGET(pLabel)) + 5;

		x2 = 0.0f;
		if (pGraph->bPartialGraph)
		{
			pButton = ui_ButtonCreate("X", x2, y, RemovePathCB, pPath);
			ui_WidgetSetPositionEx(UI_WIDGET(pButton), x2, y, 0.0f, 0.0f, UITopRight);
			emPanelAddChild(pPanel, pButton, false);
			x2 += ui_WidgetGetWidth( UI_WIDGET(pButton) ) + 5;
			//if (!pGraph->bPartialGraph && pEditNode->pInheritBits && pEditNode->pInheritBits->bInheritPath)
			//	eaPush(eaInactiveWidgets, UI_WIDGET(pButton));
		}

		x = 10.0f;
		pField = MEFieldCreateSimple(kMEFieldType_SliderText, pOrigPath, pPath, parse_DynAnimGraphPath, "Chance");
		AnimEditor_AddFieldToParent(pField, UI_WIDGET(emPanelGetExpander(pPanel)), x, y, 0, 1.0, UIUnitPercentage, 21, pDoc, FieldPreChangedCB, InheritFieldChangedCB);
		ui_WidgetSetPaddingEx(pField->pUIWidget, x1, x2, 0, 0);
		MEFieldSetChangeCallback(pField, PathChangeChanceCB, pDoc);
		ui_SliderTextEntrySetRange(pField->pUISliderText, 0, 1, 1/20.0f);
		if (eaSize(&pEditNode->eaPath) == 1 || (!pGraph->bPartialGraph && pEditNode->pInheritBits && pEditNode->pInheritBits->bInheritPath))
			eaPush(eaInactiveWidgets, pField->pUIWidget);
		if (!dynAnimGraphGroupPathVerify(pGraph, pEditNode, false))
			//!dynAnimGraphIndividualPathVerify(pGraph, pEditNode, pPath, false))
			ui_WidgetSkin(pField->pUIWidget, badSkin);
		{
			PathMEField* pPathField = calloc(sizeof(PathMEField), 1);
			pPathField->pField = pField;
			pPathField->iNodeIndex = pDocFocusIndex;
			pPathField->iPathIndex = ipPathIndex;
			eaPush(eaPathFields, pPathField);
		}

		y += STANDARD_ROW_HEIGHT;

		pSeparator = ui_SeparatorCreate(UIHorizontal);
		ui_WidgetSetPosition(UI_WIDGET(pSeparator), 0, y);
		emPanelAddChild(pPanel, pSeparator, false);

		y += SEPARATOR_HEIGHT;
	}
	FOR_EACH_END;

	if (pGraph->bPartialGraph)
	{
		pButton = ui_ButtonCreate("Add Path", x, y, AddPathCB, pEditNode);
		ui_WidgetSetPositionEx(UI_WIDGET(pButton), x, y, 0.0f, 0.0f, UITopRight);
		emPanelAddChild(pPanel, pButton, false);
		//if (!pGraph->bPartialGraph && pEditNode->pInheritBits && pEditNode->pInheritBits->bInheritPath)
		//	eaPush(eaInactiveWidgets, UI_WIDGET(pButton));
	}

	y += STANDARD_ROW_HEIGHT;

	return y;
}

F32 AnimEditor_ReflowExitNode(
	DynAnimGraph *pGraph,
	void* pDoc, int pDocFocusIndex, EMPanel* pPanel,
	MEField ***eaNodeFields, PathMEField ***eaPathFields, UIWidget ***eaInactiveWidgets,
	DynAnimGraphNode *pOrigNode, DynAnimGraphNode *pEditNode,
	MEFieldPreChangeCallback FieldPreChangedCB, MEFieldChangeCallback FieldChangedCB, MEFieldChangeCallback InheritFieldChangedCB,
	UIActivationFunc ChoosePostIdleCB, UIActivationFunc OpenPostIdleCB, UIActivationFunc RemovePostIdleCB,
	UISkin *badSkin,
	F32 y
	)
{
	if (!pGraph->bPartialGraph)
	{
		DynAnimGraph* pPostIdle = GET_REF(pEditNode->hPostIdle);
		UILabel *pLabel;
		UIButton* pButton;
		F32 x;

		x = 0;
		pLabel = ui_LabelCreate("PostIdle ", x, y);
		emPanelAddChild(pPanel, pLabel, false);

		y += STANDARD_ROW_HEIGHT;

		x = 0;
		if (pPostIdle)
		{
			pButton = ui_ButtonCreate("X", x, y, RemovePostIdleCB, pEditNode);
			ui_WidgetSetPositionEx(UI_WIDGET(pButton), x, y, 0.0f, 0.0f, UITopRight);
			emPanelAddChild(pPanel, pButton, false);
			x += ui_WidgetGetWidth(UI_WIDGET(pButton)) + 5;

			pButton = ui_ButtonCreate("Open", x, y, OpenPostIdleCB, pEditNode);
			ui_WidgetSetPositionEx(UI_WIDGET(pButton), x, y, 0.0f, 0.0f, UITopRight);
			emPanelAddChild(pPanel, pButton, false);
			x += ui_WidgetGetWidth(UI_WIDGET(pButton)) + 5;
		}
		else
		{
			pButton = ui_ButtonCreate("Add", x, y, ChoosePostIdleCB, pEditNode);
			ui_WidgetSetPositionEx(UI_WIDGET(pButton), x, y, 0.0f, 0.0f, UITopRight);
			emPanelAddChild(pPanel, pButton, false);
			x += ui_WidgetGetWidth(UI_WIDGET(pButton)) + 5;
		}

		pButton = ui_ButtonCreate(pPostIdle?pPostIdle->pcName:"None", x, y, ChoosePostIdleCB, pEditNode);
		ui_WidgetSetPositionEx(UI_WIDGET(pButton), x, y, 0.0f, 0.0f, UITopRight);
		ui_WidgetSetDimensionsEx(UI_WIDGET(pButton), 1.0, UI_WIDGET(pButton)->height, UIUnitPercentage, UI_WIDGET(pButton)->heightUnit);
		ui_WidgetSetTooltipString(UI_WIDGET(pButton), pPostIdle?pPostIdle->pcName:"None");
		emPanelAddChild(pPanel, pButton, false);

		y += STANDARD_ROW_HEIGHT;
	}

	return y;
}

F32 AnimEditor_ReflowGraphProperties(
	DynAnimGraph *pOrigGraph, DynAnimGraph *pGraph,
	void* pDoc, EMPanel* pPanel,
	MEField ***eaPropertyFields, UIWidget ***eaInactiveWidgets,
	MEFieldPreChangeCallback FieldPreChangedCB, MEFieldChangeCallback FieldChangedCB, MEFieldChangeCallback InheritFieldChangedCB,
	UIActivationFunc AddDocOnEnterFxCB, UIActivationFunc AddDocOnEnterFxMessageCB,
	UIActivationFunc AddDocOnExitFxCB, UIActivationFunc AddDocOnExitFxMessageCB,
	UIActivationFunc ChooseFxEventCB, UIActivationFunc OpenFxEventCB, UIActivationFunc RemoveFxEventCB,
	UIActivationFunc AddDocSuppressCB, UIActivationFunc RemoveSuppressCB,
	UIActivationFunc AddDocStanceCB, UIActivationFunc ChangeStanceCB, UIActivationFunc RemoveStanceCB,
	UISkin *badSkin,
	F32 y
	)
{
	UILabel *pLabel;
	MEField *pField;
	UIButton* pButton;
	UISeparator* pSeparator;
	F32 x;

	//reset when movement stops
	x = 0;
	if (!pGraph->bPartialGraph)
	{
		pField = MEFieldCreateSimple(kMEFieldType_Check, pOrigGraph ? pOrigGraph->pInheritBits : NULL, pGraph->pInheritBits, parse_DynAnimGraphInheritBits, "InheritResetWhenMovementStops");
		AnimEditor_AddFieldToParent(pField, UI_WIDGET(emPanelGetExpander(pPanel)), x, y, 0, 20.0f, UIUnitFixed, 0, pDoc, FieldPreChangedCB, InheritFieldChangedCB);
		eaPush(eaPropertyFields, pField);
		x += 20.0;
	}
	pLabel = ui_LabelCreate("Reset when Movement Stops", x, y);
	emPanelAddChild(pPanel, pLabel, false);
	x += ui_WidgetGetWidth(UI_WIDGET(pLabel)) + 10.0f;
	pField = MEFieldCreateSimple(kMEFieldType_Check, pOrigGraph, pGraph, parse_DynAnimGraph, "ResetWhenMovementStops");
	AnimEditor_AddFieldToParent(pField, UI_WIDGET(emPanelGetExpander(pPanel)), x, y, 0, 20.0f, UIUnitFixed, 0, pDoc, FieldPreChangedCB, FieldChangedCB);
	x += 30.0f;
	if (!pGraph->bPartialGraph && pGraph->pInheritBits && pGraph->pInheritBits->bInheritResetWhenMovementStops)
		eaPush(eaInactiveWidgets, pField->pUIWidget);
	eaPush(eaPropertyFields, pField);

	y += STANDARD_ROW_HEIGHT;

	//override all bones
	x = 0;
	if (!pGraph->bPartialGraph)
	{
		pField = MEFieldCreateSimple(kMEFieldType_Check, pOrigGraph ? pOrigGraph->pInheritBits : NULL, pGraph->pInheritBits, parse_DynAnimGraphInheritBits, "InheritOverrideAllBones");
		AnimEditor_AddFieldToParent(pField, UI_WIDGET(emPanelGetExpander(pPanel)), x, y, 0, 20.0f, UIUnitFixed, 0, pDoc, FieldPreChangedCB, InheritFieldChangedCB);
		eaPush(eaPropertyFields, pField);
		x += 20.0;
	}
	pLabel = ui_LabelCreate("Override All Bones (All Sequencers)", x, y);
	emPanelAddChild(pPanel, pLabel, false);
	x += ui_WidgetGetWidth(UI_WIDGET(pLabel)) + 10.0f;
	pField = MEFieldCreateSimple(kMEFieldType_Check, pOrigGraph, pGraph, parse_DynAnimGraph, "OverrideAllBones");
	AnimEditor_AddFieldToParent(pField, UI_WIDGET(emPanelGetExpander(pPanel)), x, y, 0, 20.0f, UIUnitFixed, 0, pDoc, FieldPreChangedCB, FieldChangedCB);
	ui_WidgetSetTooltipString(pField->pUIWidget,"Causes the animation graph on the sequencer that set this flag to overwrite all other graph & movement based animation");
	x += 30.0f;
	if (!pGraph->bPartialGraph && pGraph->pInheritBits && pGraph->pInheritBits->bInheritOverrideAllBones)
		eaPush(eaInactiveWidgets, pField->pUIWidget);
	eaPush(eaPropertyFields, pField);
	pLabel = ui_LabelCreate("Snap", x, y);
	emPanelAddChild(pPanel, pLabel, false);
	x += ui_WidgetGetWidth(UI_WIDGET(pLabel)) + 10.0f;
	pField = MEFieldCreateSimple(kMEFieldType_Check, pOrigGraph, pGraph, parse_DynAnimGraph, "SnapOverrideAllBones");
	AnimEditor_AddFieldToParent(pField, UI_WIDGET(emPanelGetExpander(pPanel)), x, y, 0, 20.0f, UIUnitFixed, 0, pDoc, FieldPreChangedCB, FieldChangedCB);
	ui_WidgetSetTooltipString(pField->pUIWidget,"Instantly snap to 100% ON instead of blending");
	if (!pGraph->bPartialGraph && pGraph->pInheritBits && pGraph->pInheritBits->bInheritOverrideAllBones)
		eaPush(eaInactiveWidgets, pField->pUIWidget);
	eaPush(eaPropertyFields, pField);

	y += STANDARD_ROW_HEIGHT;

	//override movement
	x = 0;
	if (!pGraph->bPartialGraph)
	{
		pField = MEFieldCreateSimple(kMEFieldType_Check, pOrigGraph ? pOrigGraph->pInheritBits : NULL, pGraph->pInheritBits, parse_DynAnimGraphInheritBits, "InheritOverrideMovement");
		AnimEditor_AddFieldToParent(pField, UI_WIDGET(emPanelGetExpander(pPanel)), x, y, 0, 20.0f, UIUnitFixed, 0, pDoc, FieldPreChangedCB, InheritFieldChangedCB);
		eaPush(eaPropertyFields, pField);
		x += 20.0;
	}
	pLabel = ui_LabelCreate("Override Movement (Legs)", x, y);
	emPanelAddChild(pPanel, pLabel, false);
	x += ui_WidgetGetWidth(UI_WIDGET(pLabel)) + 10.0f;
	pField = MEFieldCreateSimple(kMEFieldType_Check, pOrigGraph, pGraph, parse_DynAnimGraph, "OverrideMovement");
	AnimEditor_AddFieldToParent(pField, UI_WIDGET(emPanelGetExpander(pPanel)), x, y, 0, 20.0f, UIUnitFixed, 0, pDoc, FieldPreChangedCB, FieldChangedCB);
	ui_WidgetSetTooltipString(pField->pUIWidget,"Causes the animation for this graph to show-up on the legs (blendinfo movement tagged bones) while moving");
	x += 30.0;
	if (!pGraph->bPartialGraph && pGraph->pInheritBits && pGraph->pInheritBits->bInheritOverrideMovement)
		eaPush(eaInactiveWidgets, pField->pUIWidget);
	eaPush(eaPropertyFields, pField);
	pLabel = ui_LabelCreate("Snap", x, y);
	emPanelAddChild(pPanel, pLabel, false);
	x += ui_WidgetGetWidth(UI_WIDGET(pLabel)) + 10.0f;
	pField = MEFieldCreateSimple(kMEFieldType_Check, pOrigGraph, pGraph, parse_DynAnimGraph, "SnapOverrideMovement");
	AnimEditor_AddFieldToParent(pField, UI_WIDGET(emPanelGetExpander(pPanel)), x, y, 0, 20.0f, UIUnitFixed, 0, pDoc, FieldPreChangedCB, FieldChangedCB);
	ui_WidgetSetTooltipString(pField->pUIWidget,"Instantly snap to 100% ON instead of blending");
	if (!pGraph->bPartialGraph && pGraph->pInheritBits && pGraph->pInheritBits->bInheritOverrideMovement)
		eaPush(eaInactiveWidgets, pField->pUIWidget);
	eaPush(eaPropertyFields, pField);

	y += STANDARD_ROW_HEIGHT;

	//override default move
	x = 0;
	if (!pGraph->bPartialGraph)
	{
		pField = MEFieldCreateSimple(kMEFieldType_Check, pOrigGraph ? pOrigGraph->pInheritBits : NULL, pGraph->pInheritBits, parse_DynAnimGraphInheritBits, "InheritOverrideDefaultMove");
		AnimEditor_AddFieldToParent(pField, UI_WIDGET(emPanelGetExpander(pPanel)), x, y, 0, 20.0f, UIUnitFixed, 0, pDoc, FieldPreChangedCB, InheritFieldChangedCB);
		eaPush(eaPropertyFields, pField);
		x += 20.0;
	}
	pLabel = ui_LabelCreate("Override Default Movement (Torso)", x, y);
	emPanelAddChild(pPanel, pLabel, false);
	x += ui_WidgetGetWidth(UI_WIDGET(pLabel)) + 10.0f;
	pField = MEFieldCreateSimple(kMEFieldType_Check, pOrigGraph, pGraph, parse_DynAnimGraph, "OverrideDefaultMove");
	AnimEditor_AddFieldToParent(pField, UI_WIDGET(emPanelGetExpander(pPanel)), x, y, 0, 20.0f, UIUnitFixed, 0, pDoc, FieldPreChangedCB, FieldChangedCB);
	ui_WidgetSetTooltipString(pField->pUIWidget,"Causes a default graph's animation (your basic idles) to show-up on the upper body (non-blendinfo movement tagged bones) while moving");
	x += 30.0f;
	if (!pGraph->bPartialGraph && pGraph->pInheritBits && pGraph->pInheritBits->bInheritOverrideDefaultMove)
		eaPush(eaInactiveWidgets, pField->pUIWidget);
	eaPush(eaPropertyFields, pField);
	pLabel = ui_LabelCreate("Snap", x, y);
	emPanelAddChild(pPanel, pLabel, false);
	x += ui_WidgetGetWidth(UI_WIDGET(pLabel)) + 10.0f;
	pField = MEFieldCreateSimple(kMEFieldType_Check, pOrigGraph, pGraph, parse_DynAnimGraph, "SnapOverrideDefaultMove");
	AnimEditor_AddFieldToParent(pField, UI_WIDGET(emPanelGetExpander(pPanel)), x, y, 0, 20.0f, UIUnitFixed, 0, pDoc, FieldPreChangedCB, FieldChangedCB);
	ui_WidgetSetTooltipString(pField->pUIWidget,"Instantly snap to 100% ON instead of blending");
	if (!pGraph->bPartialGraph && pGraph->pInheritBits && pGraph->pInheritBits->bInheritOverrideDefaultMove)
		eaPush(eaInactiveWidgets, pField->pUIWidget);
	eaPush(eaPropertyFields, pField);

	y += STANDARD_ROW_HEIGHT;

	//force visible
	x = 0;
	if (!pGraph->bPartialGraph)
	{
		pField = MEFieldCreateSimple(kMEFieldType_Check, pOrigGraph ? pOrigGraph->pInheritBits : NULL, pGraph->pInheritBits, parse_DynAnimGraphInheritBits, "InheritForceVisible");
		AnimEditor_AddFieldToParent(pField, UI_WIDGET(emPanelGetExpander(pPanel)), x, y, 0, 20.0f, UIUnitFixed, 0, pDoc, FieldPreChangedCB, InheritFieldChangedCB);
		eaPush(eaPropertyFields, pField);
		x += 20.0;
	}
	pLabel = ui_LabelCreate("Force Visible", x, y);
	emPanelAddChild(pPanel, pLabel, false);
	x += ui_WidgetGetWidth(UI_WIDGET(pLabel)) + 10.0f;
	pField = MEFieldCreateSimple(kMEFieldType_Check, pOrigGraph, pGraph, parse_DynAnimGraph, "ForceVisible");
	AnimEditor_AddFieldToParent(pField, UI_WIDGET(emPanelGetExpander(pPanel)), x, y, 0, 20.0f, UIUnitFixed, 0, pDoc, FieldPreChangedCB, FieldChangedCB);
	if (!pGraph->bPartialGraph && pGraph->pInheritBits && pGraph->pInheritBits->bInheritForceVisible)
		eaPush(eaInactiveWidgets, pField->pUIWidget);
	eaPush(eaPropertyFields, pField);

	y += STANDARD_ROW_HEIGHT;

	//pitch to target
	x = 0;
	if (!pGraph->bPartialGraph)
	{
		pField = MEFieldCreateSimple(kMEFieldType_Check, pOrigGraph ? pOrigGraph->pInheritBits : NULL, pGraph->pInheritBits, parse_DynAnimGraphInheritBits, "InheritPitchToTarget");
		AnimEditor_AddFieldToParent(pField, UI_WIDGET(emPanelGetExpander(pPanel)), x, y, 0, 20.0f, UIUnitFixed, 0, pDoc, FieldPreChangedCB, InheritFieldChangedCB);
		eaPush(eaPropertyFields, pField);
		x += 20.0;
	}
	pLabel = ui_LabelCreate("Pitch to Target", x, y);
	emPanelAddChild(pPanel, pLabel, false);
	x += ui_WidgetGetWidth(UI_WIDGET(pLabel)) + 10.0f;
	pField = MEFieldCreateSimple(kMEFieldType_Check, pOrigGraph, pGraph, parse_DynAnimGraph, "PitchToTarget");
	AnimEditor_AddFieldToParent(pField, UI_WIDGET(emPanelGetExpander(pPanel)), x, y, 0, 20.0f, UIUnitFixed, 0, pDoc, FieldPreChangedCB, FieldChangedCB);
	if (!pGraph->bPartialGraph && pGraph->pInheritBits && pGraph->pInheritBits->bInheritPitchToTarget)
		eaPush(eaInactiveWidgets, pField->pUIWidget);
	eaPush(eaPropertyFields, pField);

	y += STANDARD_ROW_HEIGHT;

	// Generate power movement info
	x = 0;
	if (!pGraph->bPartialGraph)
	{
		pField = MEFieldCreateSimple(kMEFieldType_Check, pOrigGraph ? pOrigGraph->pInheritBits : NULL, pGraph->pInheritBits, parse_DynAnimGraphInheritBits, "InheritGeneratePowerMovementInfo");
		AnimEditor_AddFieldToParent(pField, UI_WIDGET(emPanelGetExpander(pPanel)), x, y, 0, 20.0f, UIUnitFixed, 0, pDoc, FieldPreChangedCB, InheritFieldChangedCB);
		eaPush(eaPropertyFields, pField);
		x += 20.0;
	}
	pLabel = ui_LabelCreate("Generate Power Movement", x, y);
	emPanelAddChild(pPanel, pLabel, false);
	x += ui_WidgetGetWidth(UI_WIDGET(pLabel)) + 10.0f;
	pField = MEFieldCreateSimple(kMEFieldType_Check, pOrigGraph, pGraph, parse_DynAnimGraph, "GeneratePowerMovementInfo");
	AnimEditor_AddFieldToParent(pField, UI_WIDGET(emPanelGetExpander(pPanel)), x, y, 0, 20.0f, UIUnitFixed, 0, pDoc, FieldPreChangedCB, FieldChangedCB);
	if (!pGraph->bPartialGraph && pGraph->pInheritBits && pGraph->pInheritBits->bInheritGeneratePowerMovementInfo)
		eaPush(eaInactiveWidgets, pField->pUIWidget);
	eaPush(eaPropertyFields, pField);

	y += STANDARD_ROW_HEIGHT;

	pSeparator = ui_SeparatorCreate(UIHorizontal);
	ui_WidgetSetPosition(UI_WIDGET(pSeparator), 0, y);
	emPanelAddChild(pPanel, pSeparator, false);

	y += SEPARATOR_HEIGHT;
	y += 20;

	pSeparator = ui_SeparatorCreate(UIHorizontal);
	ui_WidgetSetPosition(UI_WIDGET(pSeparator), 0, y);
	emPanelAddChild(pPanel, pSeparator, false);

	y += SEPARATOR_HEIGHT;

	//disable torso pointing
	x = 0;
	if (!pGraph->bPartialGraph)
	{
		pField = MEFieldCreateSimple(kMEFieldType_Check, pOrigGraph ? pOrigGraph->pInheritBits : NULL, pGraph->pInheritBits, parse_DynAnimGraphInheritBits, "InheritDisableTorsoPointing");
		AnimEditor_AddFieldToParent(pField, UI_WIDGET(emPanelGetExpander(pPanel)), x, y, 0, 20.0f, UIUnitFixed, 0, pDoc, FieldPreChangedCB, InheritFieldChangedCB);
		eaPush(eaPropertyFields, pField);
		x += 20.0;
	}
	pLabel = ui_LabelCreate("Timeout: Disable Torso Pointing", x, y);
	emPanelAddChild(pPanel, pLabel, false);
	x += ui_WidgetGetWidth(UI_WIDGET(pLabel)) + 10.0f;
	pField = MEFieldCreateSimple(kMEFieldType_TextEntry, pOrigGraph, pGraph, parse_DynAnimGraph, "DisableTorsoPointingTimeout");
	AnimEditor_AddFieldToParent(pField, UI_WIDGET(emPanelGetExpander(pPanel)), x, y, 0, 1.0, UIUnitPercentage, 0, pDoc, FieldPreChangedCB, FieldChangedCB);
	if (!pGraph->bPartialGraph && pGraph->pInheritBits && pGraph->pInheritBits->bInheritDisableTorsoPointing)
		eaPush(eaInactiveWidgets, pField->pUIWidget);
	eaPush(eaPropertyFields, pField);
	
	y += STANDARD_ROW_HEIGHT;

	//disable ground registration
	x = 0;
	if (!pGraph->bPartialGraph)
	{
		pField = MEFieldCreateSimple(kMEFieldType_Check, pOrigGraph ? pOrigGraph->pInheritBits : NULL, pGraph->pInheritBits, parse_DynAnimGraphInheritBits, "InheritDisableGroundReg");
		AnimEditor_AddFieldToParent(pField, UI_WIDGET(emPanelGetExpander(pPanel)), x, y, 0, 20.0f, UIUnitFixed, 0, pDoc, FieldPreChangedCB, InheritFieldChangedCB);
		eaPush(eaPropertyFields, pField);
		x += 20.0;
	}
	pLabel = ui_LabelCreate("Timeout: Disable Ground Reg. (All Bones)", x, y);
	emPanelAddChild(pPanel, pLabel, false);
	x += ui_WidgetGetWidth(UI_WIDGET(pLabel)) + 10.0f;
	pField = MEFieldCreateSimple(kMEFieldType_TextEntry, pOrigGraph, pGraph, parse_DynAnimGraph, "DisableGroundRegTimeout");
	AnimEditor_AddFieldToParent(pField, UI_WIDGET(emPanelGetExpander(pPanel)), x, y, 0, 1.0, UIUnitPercentage, 0, pDoc, FieldPreChangedCB, FieldChangedCB);
	if (!pGraph->bPartialGraph && pGraph->pInheritBits && pGraph->pInheritBits->bInheritDisableGroundReg)
		eaPush(eaInactiveWidgets, pField->pUIWidget);
	eaPush(eaPropertyFields, pField);

	y += STANDARD_ROW_HEIGHT;

	//disable ground registration
	x = 0;
	if (!pGraph->bPartialGraph)
	{
		pField = MEFieldCreateSimple(kMEFieldType_Check, pOrigGraph ? pOrigGraph->pInheritBits : NULL, pGraph->pInheritBits, parse_DynAnimGraphInheritBits, "InheritDisableUpperBodyGroundReg");
		AnimEditor_AddFieldToParent(pField, UI_WIDGET(emPanelGetExpander(pPanel)), x, y, 0, 20.0f, UIUnitFixed, 0, pDoc, FieldPreChangedCB, InheritFieldChangedCB);
		eaPush(eaPropertyFields, pField);
		x += 20.0;
	}
	pLabel = ui_LabelCreate("Timeout: Disable Ground Reg. (Upper Body)", x, y);
	emPanelAddChild(pPanel, pLabel, false);
	x += ui_WidgetGetWidth(UI_WIDGET(pLabel)) + 10.0f;
	pField = MEFieldCreateSimple(kMEFieldType_TextEntry, pOrigGraph, pGraph, parse_DynAnimGraph, "DisableUpperBodyGroundRegTimeout");
	AnimEditor_AddFieldToParent(pField, UI_WIDGET(emPanelGetExpander(pPanel)), x, y, 0, 1.0, UIUnitPercentage, 0, pDoc, FieldPreChangedCB, FieldChangedCB);
	if (!pGraph->bPartialGraph && pGraph->pInheritBits && pGraph->pInheritBits->bInheritDisableUpperBodyGroundReg)
		eaPush(eaInactiveWidgets, pField->pUIWidget);
	eaPush(eaPropertyFields, pField);

	y += STANDARD_ROW_HEIGHT;

	//post-idle timeout

	x = 0;
	if (!pGraph->bPartialGraph)
	{
		pField = MEFieldCreateSimple(kMEFieldType_Check, pOrigGraph ? pOrigGraph->pInheritBits : NULL, pGraph->pInheritBits, parse_DynAnimGraphInheritBits, "InheritTimeout");
		AnimEditor_AddFieldToParent(pField, UI_WIDGET(emPanelGetExpander(pPanel)), x, y, 0, 20.0f, UIUnitFixed, 0, pDoc, FieldPreChangedCB, InheritFieldChangedCB);
		eaPush(eaPropertyFields, pField);
		x += 20.0;
	}
	pLabel = ui_LabelCreate("Timeout: Enter Post-Idle", x, y);
	emPanelAddChild(pPanel, pLabel, false);
	x += ui_WidgetGetWidth(UI_WIDGET(pLabel)) + 10.0f;
	pField = MEFieldCreateSimple(kMEFieldType_TextEntry, pOrigGraph, pGraph, parse_DynAnimGraph, "Timeout");
	AnimEditor_AddFieldToParent(pField, UI_WIDGET(emPanelGetExpander(pPanel)), x, y, 0, 1.0, UIUnitPercentage, 0, pDoc, FieldPreChangedCB, FieldChangedCB);
	if (!pGraph->bPartialGraph && pGraph->pInheritBits && pGraph->pInheritBits->bInheritTimeout)
		eaPush(eaInactiveWidgets, pField->pUIWidget);
	eaPush(eaPropertyFields, pField);
	
	x = 0;
	y += 1.5*STANDARD_ROW_HEIGHT;
	pLabel = ui_LabelCreate("Note: Timeouts are measured in seconds from start of graph", x, y);
	emPanelAddChild(pPanel, pLabel, false);
	y += STANDARD_ROW_HEIGHT;
	pLabel = ui_LabelCreate("and are only active when greater than Zero", x, y);
	emPanelAddChild(pPanel, pLabel, false);

	y += STANDARD_ROW_HEIGHT;

	pSeparator = ui_SeparatorCreate(UIHorizontal);
	ui_WidgetSetPosition(UI_WIDGET(pSeparator), 0, y);
	emPanelAddChild(pPanel, pSeparator, false);

	y += SEPARATOR_HEIGHT;


	// Buttons for adding on enter FX events
	y += 20;

	pSeparator = ui_SeparatorCreate(UIHorizontal);
	ui_WidgetSetPosition(UI_WIDGET(pSeparator), 0, y);
	emPanelAddChild(pPanel, pSeparator, false);

	y += SEPARATOR_HEIGHT;

	x = 0;
	if (!pGraph->bPartialGraph)
	{
		pField = MEFieldCreateSimple(kMEFieldType_Check, pOrigGraph ? pOrigGraph->pInheritBits : NULL, pGraph->pInheritBits, parse_DynAnimGraphInheritBits, "InheritOnEnterFxEvent");
		AnimEditor_AddFieldToParent(pField, UI_WIDGET(emPanelGetExpander(pPanel)), x, y, 0, 20.0f, UIUnitFixed, 0, pDoc, FieldPreChangedCB, InheritFieldChangedCB);
		eaPush(eaPropertyFields, pField);
		x += 20.0;
	}
	pLabel = ui_LabelCreate("On-enter FX", x, y);
	emPanelAddChild(pPanel, pLabel, false);
	x += ui_WidgetGetWidth(UI_WIDGET(pLabel)) + 10.0f;
	pButton = ui_ButtonCreate("Add FX Message", x, y, AddDocOnEnterFxMessageCB, pDoc);
	emPanelAddChild(pPanel, pButton, false);
	if (!pGraph->bPartialGraph && pGraph->pInheritBits && pGraph->pInheritBits->bInheritOnEnterFxEvent)
		eaPush(eaInactiveWidgets, UI_WIDGET(pButton));
	x += ui_WidgetGetWidth(UI_WIDGET(pButton)) + 10.0f;
	pButton = ui_ButtonCreate("Add FX", x, y, AddDocOnEnterFxCB, pDoc);
	emPanelAddChild(pPanel, pButton, false);
	if (!pGraph->bPartialGraph && pGraph->pInheritBits && pGraph->pInheritBits->bInheritOnEnterFxEvent)
		eaPush(eaInactiveWidgets, UI_WIDGET(pButton));

	y += STANDARD_ROW_HEIGHT;

	pSeparator = ui_SeparatorCreate(UIHorizontal);
	ui_WidgetSetPosition(UI_WIDGET(pSeparator), 0, y);
	emPanelAddChild(pPanel, pSeparator, false);

	y += SEPARATOR_HEIGHT;

	FOR_EACH_IN_EARRAY_FORWARDS(pGraph->eaOnEnterFxEvent, DynAnimGraphFxEvent, pFxEvent)
	{
		DynAnimGraphFxEvent* pOrigEvent = (pOrigGraph && ipFxEventIndex < eaSize(&pOrigGraph->eaOnEnterFxEvent))?pOrigGraph->eaOnEnterFxEvent[ipFxEventIndex]:NULL;

		x = 20.0f;
		pButton = ui_ButtonCreate("X", x, y, RemoveFxEventCB, pFxEvent);
		ui_WidgetSetPositionEx(UI_WIDGET(pButton), x, y, 0.0f, 0.0f, UITopRight);
		emPanelAddChild(pPanel, pButton, false);
		x += ui_WidgetGetWidth( UI_WIDGET(pButton) ) + 5;
		if (!pGraph->bPartialGraph && pGraph->pInheritBits && pGraph->pInheritBits->bInheritOnEnterFxEvent)
			eaPush(eaInactiveWidgets, UI_WIDGET(pButton));

		if (!pFxEvent->bMessage)
		{
			pButton = ui_ButtonCreate("Open", x, y, OpenFxEventCB, pFxEvent);
			ui_WidgetSetPositionEx(UI_WIDGET(pButton), x, y, 0.0f, 0.0f, UITopRight);
			emPanelAddChild(pPanel, pButton, false);
			x += ui_WidgetGetWidth( UI_WIDGET(pButton) ) + 5;
			if (!pGraph->bPartialGraph && pGraph->pInheritBits && pGraph->pInheritBits->bInheritOnEnterFxEvent)
				eaPush(eaInactiveWidgets, UI_WIDGET(pButton));

			// FX picker
			pButton = ui_ButtonCreate(pFxEvent->pcFx?pFxEvent->pcFx:"Choose FX", x, y, ChooseFxEventCB, pFxEvent); 
			if (!pFxEvent->pcFx || !dynFxInfoExists(pFxEvent->pcFx))
				ui_WidgetSkin(UI_WIDGET(pButton), badSkin);
			emPanelAddChild(pPanel, pButton, false);
			ui_WidgetSetPositionEx(UI_WIDGET(pButton), x, y, 0.0f, 0.0f, UITopRight);
			ui_WidgetSetWidthEx(UI_WIDGET(pButton), 0.98, UIUnitPercentage);
			if (!pGraph->bPartialGraph && pGraph->pInheritBits && pGraph->pInheritBits->bInheritOnEnterFxEvent)
				eaPush(eaInactiveWidgets, UI_WIDGET(pButton));
		}
		else
		{
			float xpad = x;
			// Just text entry box
			pLabel = ui_LabelCreate("Message", 5, y);
			x = ui_WidgetGetWidth( UI_WIDGET(pLabel) ) + 10;
			emPanelAddChild(pPanel, pLabel, false);
			pField = MEFieldCreateSimple(kMEFieldType_TextEntry, pOrigEvent, pFxEvent, parse_DynAnimGraphFxEvent, "Fx");
			AnimEditor_AddFieldToParent(pField, UI_WIDGET(emPanelGetExpander(pPanel)), x, y, 0, 1.0, UIUnitPercentage, xpad, pDoc, FieldPreChangedCB, FieldChangedCB);
			if (!pGraph->bPartialGraph && pGraph->pInheritBits && pGraph->pInheritBits->bInheritOnEnterFxEvent)
				eaPush(eaInactiveWidgets, pField->pUIWidget);
			eaPush(eaPropertyFields, pField);
		}

		y += STANDARD_ROW_HEIGHT;

		pLabel = ui_LabelCreate("Enable Movement Trigger", 5, y);
		x = ui_WidgetGetWidth(UI_WIDGET(pLabel)) + 10;
		emPanelAddChild(pPanel, pLabel, false);
		pField = MEFieldCreateSimple(kMEFieldType_Check, pOrigEvent, pFxEvent, parse_DynAnimGraphFxEvent, "MovementBlendTriggered");
		AnimEditor_AddFieldToParent(pField, UI_WIDGET(emPanelGetExpander(pPanel)), x, y, 0, 1.0, UIUnitPercentage, 0, pDoc, FieldPreChangedCB, FieldChangedCB);
		if (!pGraph->bPartialGraph && pGraph->pInheritBits && pGraph->pInheritBits->bInheritOnEnterFxEvent)
			eaPush(eaInactiveWidgets, pField->pUIWidget);
		eaPush(eaPropertyFields, pField);

		y += STANDARD_ROW_HEIGHT;

		pLabel = ui_LabelCreate("Movement Blend Trigger", 5, y);
		x = ui_WidgetGetWidth(UI_WIDGET(pLabel)) + 10;
		emPanelAddChild(pPanel, pLabel, false);
		pField = MEFieldCreateSimple(kMEFieldType_SliderText, pOrigEvent, pFxEvent, parse_DynAnimGraphFxEvent, "MovementBlendTrigger");
		AnimEditor_AddFieldToParent(pField, UI_WIDGET(emPanelGetExpander(pPanel)), x, y, 0, 1.0, UIUnitPercentage, 0, pDoc, FieldPreChangedCB, FieldChangedCB);
		ui_SliderTextEntrySetRange(pField->pUISliderText, ANIMGRAPH_MIN_MOVEBLENDTRIGGER, ANIMGRAPH_MAX_MOVEBLENDTRIGGER, 0.01);
		if (!pGraph->bPartialGraph && pGraph->pInheritBits && pGraph->pInheritBits->bInheritOnEnterFxEvent)
			eaPush(eaInactiveWidgets, pField->pUIWidget);
		eaPush(eaPropertyFields, pField);

		y += STANDARD_ROW_HEIGHT;

		pSeparator = ui_SeparatorCreate(UIHorizontal);
		ui_WidgetSetPosition(UI_WIDGET(pSeparator), 0, y);
		emPanelAddChild(pPanel, pSeparator, false);

		y += SEPARATOR_HEIGHT;
	}
	FOR_EACH_END;

	if (eaSize(&pGraph->eaOnEnterFxEvent))
	{
		pSeparator = ui_SeparatorCreate(UIHorizontal);
		ui_WidgetSetPosition(UI_WIDGET(pSeparator), 0, y);
		emPanelAddChild(pPanel, pSeparator, false);

		y += SEPARATOR_HEIGHT;
	}

	// Buttons for adding on exit FX events
	y += 20;

	pSeparator = ui_SeparatorCreate(UIHorizontal);
	ui_WidgetSetPosition(UI_WIDGET(pSeparator), 0, y);
	emPanelAddChild(pPanel, pSeparator, false);

	y += SEPARATOR_HEIGHT;

	x = 0;
	if (!pGraph->bPartialGraph)
	{
		pField = MEFieldCreateSimple(kMEFieldType_Check, pOrigGraph ? pOrigGraph->pInheritBits : NULL, pGraph->pInheritBits, parse_DynAnimGraphInheritBits, "InheritOnExitFxEvent");
		AnimEditor_AddFieldToParent(pField, UI_WIDGET(emPanelGetExpander(pPanel)), x, y, 0, 20.0f, UIUnitFixed, 0, pDoc, FieldPreChangedCB, InheritFieldChangedCB);
		eaPush(eaPropertyFields, pField);
		x += 20.0;
	}
	pLabel = ui_LabelCreate("On-exit FX", x, y);
	emPanelAddChild(pPanel, pLabel, false);
	x += ui_WidgetGetWidth(UI_WIDGET(pLabel)) + 10.0f;
	pButton = ui_ButtonCreate("Add FX Message", x, y, AddDocOnExitFxMessageCB, pDoc);
	emPanelAddChild(pPanel, pButton, false);
	if (!pGraph->bPartialGraph && pGraph->pInheritBits && pGraph->pInheritBits->bInheritOnExitFxEvent)
		eaPush(eaInactiveWidgets, UI_WIDGET(pButton));
	x += ui_WidgetGetWidth(UI_WIDGET(pButton)) + 10.0f;
	pButton = ui_ButtonCreate("Add FX", x, y, AddDocOnExitFxCB, pDoc);
	emPanelAddChild(pPanel, pButton, false);
	if (!pGraph->bPartialGraph && pGraph->pInheritBits && pGraph->pInheritBits->bInheritOnExitFxEvent)
		eaPush(eaInactiveWidgets, UI_WIDGET(pButton));

	y += STANDARD_ROW_HEIGHT;

	pSeparator = ui_SeparatorCreate(UIHorizontal);
	ui_WidgetSetPosition(UI_WIDGET(pSeparator), 0, y);
	emPanelAddChild(pPanel, pSeparator, false);

	y += SEPARATOR_HEIGHT;

	FOR_EACH_IN_EARRAY_FORWARDS(pGraph->eaOnExitFxEvent, DynAnimGraphFxEvent, pFxEvent)
	{
		DynAnimGraphFxEvent* pOrigEvent = (pOrigGraph && ipFxEventIndex < eaSize(&pOrigGraph->eaOnExitFxEvent))?pOrigGraph->eaOnExitFxEvent[ipFxEventIndex]:NULL;

		x = 20.0f;
		pButton = ui_ButtonCreate("X", x, y, RemoveFxEventCB, pFxEvent);
		ui_WidgetSetPositionEx(UI_WIDGET(pButton), x, y, 0.0f, 0.0f, UITopRight);
		emPanelAddChild(pPanel, pButton, false);
		x += ui_WidgetGetWidth( UI_WIDGET(pButton) ) + 5;
		if (!pGraph->bPartialGraph && pGraph->pInheritBits && pGraph->pInheritBits->bInheritOnExitFxEvent)
			eaPush(eaInactiveWidgets, UI_WIDGET(pButton));

		if (!pFxEvent->bMessage)
		{
			pButton = ui_ButtonCreate("Open", x, y, OpenFxEventCB, pFxEvent);
			ui_WidgetSetPositionEx(UI_WIDGET(pButton), x, y, 0.0f, 0.0f, UITopRight);
			emPanelAddChild(pPanel, pButton, false);
			x += ui_WidgetGetWidth( UI_WIDGET(pButton) ) + 5;
			if (!pGraph->bPartialGraph && pGraph->pInheritBits && pGraph->pInheritBits->bInheritOnExitFxEvent)
				eaPush(eaInactiveWidgets, UI_WIDGET(pButton));

			// FX picker
			pButton = ui_ButtonCreate(pFxEvent->pcFx?pFxEvent->pcFx:"Choose FX", x, y, ChooseFxEventCB, pFxEvent); 
			if (!pFxEvent->pcFx || !dynFxInfoExists(pFxEvent->pcFx))
				ui_WidgetSkin(UI_WIDGET(pButton), badSkin);
			emPanelAddChild(pPanel, pButton, false);
			ui_WidgetSetPositionEx(UI_WIDGET(pButton), x, y, 0.0f, 0.0f, UITopRight);
			ui_WidgetSetWidthEx(UI_WIDGET(pButton), 0.98, UIUnitPercentage);
			if (!pGraph->bPartialGraph && pGraph->pInheritBits && pGraph->pInheritBits->bInheritOnExitFxEvent)
				eaPush(eaInactiveWidgets, UI_WIDGET(pButton));
		}
		else
		{
			float xpad = x;
			// Just text entry box
			pLabel = ui_LabelCreate("Message", 5, y);
			x = ui_WidgetGetWidth( UI_WIDGET(pLabel) ) + 10;
			emPanelAddChild(pPanel, pLabel, false);
			pField = MEFieldCreateSimple(kMEFieldType_TextEntry, pOrigEvent, pFxEvent, parse_DynAnimGraphFxEvent, "Fx");
			AnimEditor_AddFieldToParent(pField, UI_WIDGET(emPanelGetExpander(pPanel)), x, y, 0, 1.0, UIUnitPercentage, xpad, pDoc, FieldPreChangedCB, FieldChangedCB);
			if (!pGraph->bPartialGraph && pGraph->pInheritBits && pGraph->pInheritBits->bInheritOnExitFxEvent)
				eaPush(eaInactiveWidgets, pField->pUIWidget);
			eaPush(eaPropertyFields, pField);
		}

		y += STANDARD_ROW_HEIGHT;

		pLabel = ui_LabelCreate("Enable Movement Trigger", 5, y);
		x = ui_WidgetGetWidth(UI_WIDGET(pLabel)) + 10;
		emPanelAddChild(pPanel, pLabel, false);
		pField = MEFieldCreateSimple(kMEFieldType_Check, pOrigEvent, pFxEvent, parse_DynAnimGraphFxEvent, "MovementBlendTriggered");
		AnimEditor_AddFieldToParent(pField, UI_WIDGET(emPanelGetExpander(pPanel)), x, y, 0, 1.0, UIUnitPercentage, 0, pDoc, FieldPreChangedCB, FieldChangedCB);
		if (!pGraph->bPartialGraph && pGraph->pInheritBits && pGraph->pInheritBits->bInheritOnExitFxEvent)
			eaPush(eaInactiveWidgets, pField->pUIWidget);
		eaPush(eaPropertyFields, pField);

		y += STANDARD_ROW_HEIGHT;

		pLabel = ui_LabelCreate("Movement Blend Trigger", 5, y);
		x = ui_WidgetGetWidth(UI_WIDGET(pLabel)) + 10;
		emPanelAddChild(pPanel, pLabel, false);
		pField = MEFieldCreateSimple(kMEFieldType_SliderText, pOrigEvent, pFxEvent, parse_DynAnimGraphFxEvent, "MovementBlendTrigger");
		AnimEditor_AddFieldToParent(pField, UI_WIDGET(emPanelGetExpander(pPanel)), x, y, 0, 1.0, UIUnitPercentage, 0, pDoc, FieldPreChangedCB, FieldChangedCB);
		ui_SliderTextEntrySetRange(pField->pUISliderText, ANIMGRAPH_MIN_MOVEBLENDTRIGGER, ANIMGRAPH_MAX_MOVEBLENDTRIGGER, 0.01);
		if (!pGraph->bPartialGraph && pGraph->pInheritBits && pGraph->pInheritBits->bInheritOnExitFxEvent)
			eaPush(eaInactiveWidgets, pField->pUIWidget);
		eaPush(eaPropertyFields, pField);

		y += STANDARD_ROW_HEIGHT;

		pSeparator = ui_SeparatorCreate(UIHorizontal);
		ui_WidgetSetPosition(UI_WIDGET(pSeparator), 0, y);
		emPanelAddChild(pPanel, pSeparator, false);

		y += SEPARATOR_HEIGHT;
	}
	FOR_EACH_END;

	if (eaSize(&pGraph->eaOnExitFxEvent))
	{
		pSeparator = ui_SeparatorCreate(UIHorizontal);
		ui_WidgetSetPosition(UI_WIDGET(pSeparator), 0, y);
		emPanelAddChild(pPanel, pSeparator, false);

		y += SEPARATOR_HEIGHT;
	}

	// Buttons for adding Suppression
	y += 20;

	pSeparator = ui_SeparatorCreate(UIHorizontal);
	ui_WidgetSetPosition(UI_WIDGET(pSeparator), 0, y);
	emPanelAddChild(pPanel, pSeparator, false);

	y += SEPARATOR_HEIGHT;

	x = 0;
	if (!pGraph->bPartialGraph)
	{
		pField = MEFieldCreateSimple(kMEFieldType_Check, pOrigGraph ? pOrigGraph->pInheritBits : NULL, pGraph->pInheritBits, parse_DynAnimGraphInheritBits, "InheritSuppress");
		AnimEditor_AddFieldToParent(pField, UI_WIDGET(emPanelGetExpander(pPanel)), x, y, 0, 20.0f, UIUnitFixed, 0, pDoc, FieldPreChangedCB, InheritFieldChangedCB);
		eaPush(eaPropertyFields, pField);
		x += 20.0;
	}
	pLabel = ui_LabelCreate("Suppression Tags", x, y);
	emPanelAddChild(pPanel, pLabel, false);
	x += ui_WidgetGetWidth(UI_WIDGET(pLabel)) + 10.0f;
	pButton = ui_ButtonCreate("Add Suppression", x, y, AddDocSuppressCB, pDoc);
	emPanelAddChild(pPanel, pButton, false);
	if (!pGraph->bPartialGraph && pGraph->pInheritBits && pGraph->pInheritBits->bInheritSuppress)
		eaPush(eaInactiveWidgets, UI_WIDGET(pButton));

	y += STANDARD_ROW_HEIGHT;

	pSeparator = ui_SeparatorCreate(UIHorizontal);
	ui_WidgetSetPosition(UI_WIDGET(pSeparator), 0, y);
	emPanelAddChild(pPanel, pSeparator, false);

	y += SEPARATOR_HEIGHT;

	FOR_EACH_IN_EARRAY_FORWARDS(pGraph->eaSuppress, DynAnimGraphSuppress, pSuppress)
	{
		float xpad;
		DynAnimGraphSuppress* pOrigEvent = (pOrigGraph && ipSuppressIndex< eaSize(&pOrigGraph->eaSuppress))?pOrigGraph->eaSuppress[ipSuppressIndex]:NULL;

		x = 20.0f;
		pButton = ui_ButtonCreate("X", x, y, RemoveSuppressCB, pSuppress);
		ui_WidgetSetPositionEx(UI_WIDGET(pButton), x, y, 0.0f, 0.0f, UITopRight);
		emPanelAddChild(pPanel, pButton, false);
		x += ui_WidgetGetWidth( UI_WIDGET(pButton) ) + 5;
		if (!pGraph->bPartialGraph && pGraph->pInheritBits && pGraph->pInheritBits->bInheritSuppress)
			eaPush(eaInactiveWidgets, UI_WIDGET(pButton));

		xpad = x;
		// Just text entry box
		pLabel = ui_LabelCreate("Suppress", 5, y);
		x = ui_WidgetGetWidth( UI_WIDGET(pLabel) ) + 10;
		emPanelAddChild(pPanel, pLabel, false);
		pField = MEFieldCreateSimple(kMEFieldType_TextEntry, pOrigEvent, pSuppress, parse_DynAnimGraphSuppress, "SuppressionTag");
		AnimEditor_AddFieldToParent(pField, UI_WIDGET(emPanelGetExpander(pPanel)), x, y, 0, 1.0, UIUnitPercentage, xpad, pDoc, FieldPreChangedCB, FieldChangedCB);
		if (!pGraph->bPartialGraph && pGraph->pInheritBits && pGraph->pInheritBits->bInheritSuppress)
			eaPush(eaInactiveWidgets, pField->pUIWidget);
		eaPush(eaPropertyFields, pField);

		y += STANDARD_ROW_HEIGHT;
	}
	FOR_EACH_END;

	if (eaSize(&pGraph->eaSuppress))
	{
		pSeparator = ui_SeparatorCreate(UIHorizontal);
		ui_WidgetSetPosition(UI_WIDGET(pSeparator), 0, y);
		emPanelAddChild(pPanel, pSeparator, false);

		y += SEPARATOR_HEIGHT;
	}

	y += 20;

	pSeparator = ui_SeparatorCreate(UIHorizontal);
	ui_WidgetSetPosition(UI_WIDGET(pSeparator), 0, y);
	emPanelAddChild(pPanel, pSeparator, false);

	y += SEPARATOR_HEIGHT;

	x = 0;
	if (!pGraph->bPartialGraph)
	{
		pField = MEFieldCreateSimple(kMEFieldType_Check, pOrigGraph ? pOrigGraph->pInheritBits : NULL, pGraph->pInheritBits, parse_DynAnimGraphInheritBits, "InheritStance");
		AnimEditor_AddFieldToParent(pField, UI_WIDGET(emPanelGetExpander(pPanel)), x, y, 0, 20.0f, UIUnitFixed, 0, pDoc, FieldPreChangedCB, InheritFieldChangedCB);
		eaPush(eaPropertyFields, pField);
		x += 20.0;
	}
	pLabel = ui_LabelCreate("Stance while playing", x, y);
	emPanelAddChild(pPanel, pLabel, false);
	x += ui_WidgetGetWidth(UI_WIDGET(pLabel)) + 10.0f;
	pButton = ui_ButtonCreate("Add Stance", x, y, AddDocStanceCB, pDoc);
	emPanelAddChild(pPanel, pButton, false);
	if (!pGraph->bPartialGraph && pGraph->pInheritBits && pGraph->pInheritBits->bInheritStance)
		eaPush(eaInactiveWidgets, UI_WIDGET(pButton));

	y += STANDARD_ROW_HEIGHT;

	pSeparator = ui_SeparatorCreate(UIHorizontal);
	ui_WidgetSetPosition(UI_WIDGET(pSeparator), 0, y);
	emPanelAddChild(pPanel, pSeparator, false);

	y += SEPARATOR_HEIGHT;

	{
		const char **eaOrigStanceWords = NULL;
		eaCreate(&eaOrigStanceWords);
		if (pOrigGraph)
		{
			FOR_EACH_IN_EARRAY(pOrigGraph->eaStance, DynAnimGraphStance, pOrigStance)
			{
				eaPush(&eaOrigStanceWords, pOrigStance->pcStance);
			}
			FOR_EACH_END;
		}

		FOR_EACH_IN_EARRAY_FORWARDS(pGraph->eaStance, DynAnimGraphStance, pStance)
		{
			UIFilteredComboBox *pFilterCombo;
			F32 x1, x2;

			x = 20.0f;
			pButton = ui_ButtonCreate("X", x, y, RemoveStanceCB, pStance);
			ui_WidgetSetPositionEx(UI_WIDGET(pButton), x, y, 0.0f, 0.0f, UITopRight);
			emPanelAddChild(pPanel, pButton, false);
			x1 = x + ui_WidgetGetWidth( UI_WIDGET(pButton) ) + 5;
			if (!pGraph->bPartialGraph && pGraph->pInheritBits && pGraph->pInheritBits->bInheritStance)
				eaPush(eaInactiveWidgets, UI_WIDGET(pButton));

			pLabel = ui_LabelCreate("Stance", 5, y);
			x2 = ui_WidgetGetWidth( UI_WIDGET(pLabel) ) + 10;
			emPanelAddChild(pPanel, pLabel, false);

			pFilterCombo = ui_FilteredComboBoxCreate(x, y, 229, parse_DynAnimStanceData, &stance_list.eaStances, "Name");
			if (!pOrigGraph || eaFind(&eaOrigStanceWords, pStance->pcStance) < 0)
				ui_SetChanged(UI_WIDGET(pFilterCombo), true);
			ui_ComboBoxSetSelectedCallback((UIComboBox*)pFilterCombo, ChangeStanceCB, U32_TO_PTR(ipStanceIndex));
			ui_ComboBoxSetSelected((UIComboBox*)pFilterCombo, dynAnimStanceIndex(pStance->pcStance));
			ui_WidgetSetPaddingEx(UI_WIDGET(pFilterCombo), x1, x2, 0, 0);
			ui_WidgetSetDimensionsEx(UI_WIDGET(pFilterCombo), 1.0, UI_WIDGET(pFilterCombo)->height, UIUnitPercentage, UI_WIDGET(pFilterCombo)->heightUnit);
			emPanelAddChild(pPanel, UI_WIDGET(pFilterCombo), false);
			if (!pGraph->bPartialGraph && pGraph->pInheritBits && pGraph->pInheritBits->bInheritStance)
				eaPush(eaInactiveWidgets, UI_WIDGET(pFilterCombo));

			y += STANDARD_ROW_HEIGHT;
		}
		FOR_EACH_END;

		eaDestroy(&eaOrigStanceWords);
	}

	if (eaSize(&pGraph->eaStance))
	{
		pSeparator = ui_SeparatorCreate(UIHorizontal);
		ui_WidgetSetPosition(UI_WIDGET(pSeparator), 0, y);
		emPanelAddChild(pPanel, pSeparator, false);

		y += SEPARATOR_HEIGHT;
	}

	return y;
}


static void AnimEditor_OpenDynMoveSearchDoc		(UIButton *bButton, DynMove					*pObj) {if (pObj) emOpenFileEx(pObj->pcName, "DynMove");}
static void AnimEditor_OpenMoveTransitionDoc	(UIButton *bButton, DynMoveTransition		*pObj) {if (pObj) emOpenFileEx(pObj->pcName, "MoveTransition");}
static void AnimEditor_OpenAnimTemplateSearchDoc(UIButton *bButton, DynAnimTemplate			*pObj) {if (pObj) emOpenFileEx(pObj->pcName, "AnimTemplate");}
static void AnimEditor_OpenAnimGraphSearchDoc	(UIButton *bButton, DynAnimGraph			*pObj) {if (pObj) emOpenFileEx(pObj->pcName, "AnimGraph");}
static void AnimEditor_OpenAnimChartSearchDoc	(UIButton *bButton, DynAnimChartLoadTime	*pObj) {if (pObj) emOpenFileEx(pObj->pcName, "AnimChart");}

F32 AnimEditor_Search(	void *pDoc, EMPanel *pPanel,
						const char *pcSearchText,
						UIActivationFunc SearchTextChangedCB,
						UIActivationFunc RefreshSearchDocsCB)
{
	UILabel *pLabel;
	UIButton *pButton;
	UISeparator *pSeparator;
	UITextEntry *pTextEntry;
	F32 y = 0.0f;
	int numMatches = 0;

	//search text

	pSeparator = ui_SeparatorCreate(UIHorizontal);
	ui_WidgetSetPosition(UI_WIDGET(pSeparator), 0, y);
	ui_WidgetSetDimensionsEx(UI_WIDGET(pSeparator), 1.0, 5, UIUnitPercentage, UIUnitFixed);
	emPanelAddChild(pPanel, pSeparator, false);
	y += SEPARATOR_HEIGHT/2;

	pLabel = ui_LabelCreate("Search For:", 0, y);
	emPanelAddChild(pPanel, pLabel, false);
	pTextEntry = ui_TextEntryCreate((const unsigned char*)pcSearchText, 70, y);
	ui_WidgetSetWidth(UI_WIDGET(pTextEntry), 195);
	ui_TextEntrySetFinishedCallback(pTextEntry, SearchTextChangedCB, pDoc);
	emPanelAddChild(pPanel, pTextEntry, false);
	y+= STANDARD_ROW_HEIGHT;

	pButton = ui_ButtonCreate( "Refresh", 0, y, RefreshSearchDocsCB, pDoc );
	ui_WidgetSetWidthEx( UI_WIDGET( pButton ), 1, UIUnitPercentage );
	emPanelAddChild(pPanel, pButton, false);
	y += STANDARD_ROW_HEIGHT;

	pSeparator = ui_SeparatorCreate(UIHorizontal);
	ui_WidgetSetPosition(UI_WIDGET(pSeparator), 0, y);
	ui_WidgetSetDimensionsEx(UI_WIDGET(pSeparator), 1.0, 5, UIUnitPercentage, UIUnitFixed);
	emPanelAddChild(pPanel, pSeparator, false);
	y += 30;

	//moves

	pSeparator = ui_SeparatorCreate(UIHorizontal);
	ui_WidgetSetPosition(UI_WIDGET(pSeparator), 0, y);
	ui_WidgetSetDimensionsEx(UI_WIDGET(pSeparator), 1.0, 5, UIUnitPercentage, UIUnitFixed);
	emPanelAddChild(pPanel, pSeparator, false);
	y += SEPARATOR_HEIGHT;

	pLabel = ui_LabelCreate("Moves", 0, y);
	emPanelAddChild(pPanel, pLabel, false);
	y += STANDARD_ROW_HEIGHT;

	{
		DictionaryEArrayStruct *pDynMoveArray = resDictGetEArrayStruct(hDynMoveDict);
		FOR_EACH_IN_EARRAY(pDynMoveArray->ppReferents, DynMove, pMove)
		{
			int count = pcSearchText ? (strlen(pcSearchText)?dynMoveGetSearchStringCount(pMove,pcSearchText):0) : 0;
			numMatches += count;
			if (0 < count &&  numMatches <= 5000) {
				char cBuff[MAX_PATH];				
				itoa(count, cBuff, 10);
				strcat(cBuff, " x ");

				pLabel = ui_LabelCreate(cBuff, 0, y);
				emPanelAddChild(pPanel, pLabel, false);

				pButton = ui_ButtonCreate(pMove->pcName,30, y, AnimEditor_OpenDynMoveSearchDoc, pMove);
				ui_WidgetSetWidthEx( UI_WIDGET( pButton ), 1, UIUnitPercentage );
				emPanelAddChild(pPanel, pButton, false);

				y += STANDARD_ROW_HEIGHT;
			}
			else if (5000 < numMatches) {
				pLabel = ui_LabelCreate("Stopping, found over 5000 matches!", 0, y);
				emPanelAddChild(pPanel, pLabel, false);
				y += STANDARD_ROW_HEIGHT;
				break;
			}
		}
		FOR_EACH_END;
	}

	pSeparator = ui_SeparatorCreate(UIHorizontal);
	ui_WidgetSetPosition(UI_WIDGET(pSeparator), 0, y);
	ui_WidgetSetDimensionsEx(UI_WIDGET(pSeparator), 1.0, 5, UIUnitPercentage, UIUnitFixed);
	emPanelAddChild(pPanel, pSeparator, false);
	y += 30;

	//movement transitions

	pSeparator = ui_SeparatorCreate(UIHorizontal);
	ui_WidgetSetPosition(UI_WIDGET(pSeparator), 0, y);
	ui_WidgetSetDimensionsEx(UI_WIDGET(pSeparator), 1.0, 5, UIUnitPercentage, UIUnitFixed);
	emPanelAddChild(pPanel, pSeparator, false);
	y += SEPARATOR_HEIGHT;

	pLabel = ui_LabelCreate("Movement Transitions", 0, y);
	emPanelAddChild(pPanel, pLabel, false);
	y += STANDARD_ROW_HEIGHT;

	{
		DictionaryEArrayStruct *pMoveTransitionArray = resDictGetEArrayStruct(hMoveTransitionDict);
		FOR_EACH_IN_EARRAY(pMoveTransitionArray->ppReferents, DynMoveTransition, pMoveTransition)
		{
			int count = pcSearchText ? (strlen(pcSearchText)?dynMoveTransitionGetSearchStringCount(pMoveTransition,pcSearchText):0) : 0;
			numMatches += count;
			if (0 < count && numMatches <= 5000) {
				char cBuff[MAX_PATH];
				itoa(count, cBuff, 10);
				strcat(cBuff, " x ");

				pLabel = ui_LabelCreate(cBuff, 0, y);
				emPanelAddChild(pPanel, pLabel, false);

				pButton = ui_ButtonCreate(pMoveTransition->pcName,30, y, AnimEditor_OpenMoveTransitionDoc, pMoveTransition);
				ui_WidgetSetWidthEx( UI_WIDGET( pButton ), 1, UIUnitPercentage );
				emPanelAddChild(pPanel, pButton, false);

				y += STANDARD_ROW_HEIGHT;
			}
			else if (5000 < numMatches) {
				pLabel = ui_LabelCreate("Stopping, found over 5000 matches!", 0, y);
				emPanelAddChild(pPanel, pLabel, false);
				y += STANDARD_ROW_HEIGHT;
				break;
			}
		}
		FOR_EACH_END;
	}

	pSeparator = ui_SeparatorCreate(UIHorizontal);
	ui_WidgetSetPosition(UI_WIDGET(pSeparator), 0, y);
	ui_WidgetSetDimensionsEx(UI_WIDGET(pSeparator), 1.0, 5, UIUnitPercentage, UIUnitFixed);
	emPanelAddChild(pPanel, pSeparator, false);
	y += 30;

	//templates

	pSeparator = ui_SeparatorCreate(UIHorizontal);
	ui_WidgetSetPosition(UI_WIDGET(pSeparator), 0, y);
	ui_WidgetSetDimensionsEx(UI_WIDGET(pSeparator), 1.0, 5, UIUnitPercentage, UIUnitFixed);
	emPanelAddChild(pPanel, pSeparator, false);
	y += SEPARATOR_HEIGHT;

	pLabel = ui_LabelCreate("Templates", 0, y);
	emPanelAddChild(pPanel, pLabel, false);
	y += STANDARD_ROW_HEIGHT;

	{
		DictionaryEArrayStruct *pAnimTemplateArray = resDictGetEArrayStruct(hAnimTemplateDict);
		FOR_EACH_IN_EARRAY(pAnimTemplateArray->ppReferents, DynAnimTemplate, pTemplate)
		{
			int count = pcSearchText ? (strlen(pcSearchText)?dynAnimTemplateGetSearchStringCount(pTemplate,pcSearchText):0) : 0;
			numMatches += count;
			if (0 < count && numMatches <= 5000) {
				char cBuff[MAX_PATH];
				itoa(count, cBuff, 10);
				strcat(cBuff, " x ");

				pLabel = ui_LabelCreate(cBuff, 0, y);
				emPanelAddChild(pPanel, pLabel, false);

				pButton = ui_ButtonCreate(pTemplate->pcName,30, y, AnimEditor_OpenAnimTemplateSearchDoc, pTemplate);
				ui_WidgetSetWidthEx( UI_WIDGET( pButton ), 1, UIUnitPercentage );
				emPanelAddChild(pPanel, pButton, false);

				y += STANDARD_ROW_HEIGHT;
			}
			else if (5000 < numMatches) {
				pLabel = ui_LabelCreate("Stopping, found over 5000 matches!", 0, y);
				emPanelAddChild(pPanel, pLabel, false);
				y += STANDARD_ROW_HEIGHT;
				break;
			}
		}
		FOR_EACH_END;
	}

	pSeparator = ui_SeparatorCreate(UIHorizontal);
	ui_WidgetSetPosition(UI_WIDGET(pSeparator), 0, y);
	ui_WidgetSetDimensionsEx(UI_WIDGET(pSeparator), 1.0, 5, UIUnitPercentage, UIUnitFixed);
	emPanelAddChild(pPanel, pSeparator, false);
	y += 30;

	//graphs

	pSeparator = ui_SeparatorCreate(UIHorizontal);
	ui_WidgetSetPosition(UI_WIDGET(pSeparator), 0, y);
	ui_WidgetSetDimensionsEx(UI_WIDGET(pSeparator), 1.0, 5, UIUnitPercentage, UIUnitFixed);
	emPanelAddChild(pPanel, pSeparator, false);
	y += SEPARATOR_HEIGHT;

	pLabel = ui_LabelCreate("Graphs", 0, y);
	emPanelAddChild(pPanel, pLabel, false);
	y += STANDARD_ROW_HEIGHT;

	{
		DictionaryEArrayStruct *pAnimGraphArray = resDictGetEArrayStruct(hAnimGraphDict);
		FOR_EACH_IN_EARRAY(pAnimGraphArray->ppReferents, DynAnimGraph, pGraph)
		{
			int count = pcSearchText ? (strlen(pcSearchText)?dynAnimGraphGetSearchStringCount(pGraph, pcSearchText):0) : 0;
			numMatches += count;
			if (0 < count && numMatches <= 5000) {
				char cBuff[MAX_PATH];
				itoa(count, cBuff, 10);
				strcat(cBuff, " x ");

				pLabel = ui_LabelCreate(cBuff, 0, y);
				emPanelAddChild(pPanel, pLabel, false);

				pButton = ui_ButtonCreate(pGraph->pcName,30, y, AnimEditor_OpenAnimGraphSearchDoc, pGraph);
				ui_WidgetSetWidthEx( UI_WIDGET( pButton ), 1, UIUnitPercentage );
				emPanelAddChild(pPanel, pButton, false);

				y += STANDARD_ROW_HEIGHT;
			}
			else if (5000 < numMatches) {
				pLabel = ui_LabelCreate("Stopping, found over 5000 matches!", 0, y);
				emPanelAddChild(pPanel, pLabel, false);
				y += STANDARD_ROW_HEIGHT;
				break;
			}
		}
		FOR_EACH_END;
	}

	pSeparator = ui_SeparatorCreate(UIHorizontal);
	ui_WidgetSetPosition(UI_WIDGET(pSeparator), 0, y);
	ui_WidgetSetDimensionsEx(UI_WIDGET(pSeparator), 1.0, 5, UIUnitPercentage, UIUnitFixed);
	emPanelAddChild(pPanel, pSeparator, false);
	y += 30;

	//charts

	pSeparator = ui_SeparatorCreate(UIHorizontal);
	ui_WidgetSetPosition(UI_WIDGET(pSeparator), 0, y);
	ui_WidgetSetDimensionsEx(UI_WIDGET(pSeparator), 1.0, 5, UIUnitPercentage, UIUnitFixed);
	emPanelAddChild(pPanel, pSeparator, false);
	y += SEPARATOR_HEIGHT;

	pLabel = ui_LabelCreate("Charts", 0, y);
	emPanelAddChild(pPanel, pLabel, false);
	y += STANDARD_ROW_HEIGHT;

	{
		DictionaryEArrayStruct *pAnimChartArray = resDictGetEArrayStruct(hAnimChartDictLoadTime);
		FOR_EACH_IN_EARRAY(pAnimChartArray->ppReferents, DynAnimChartLoadTime, pChart)
		{
			int count = pcSearchText ? (strlen(pcSearchText)?dynAnimChartGetSearchStringCount(pChart,pcSearchText):0) : 0;
			numMatches += count;
			if (0 < count && numMatches <= 5000) {
				char cBuff[MAX_PATH];
				itoa(count, cBuff, 10);
				strcat(cBuff, " x ");

				pLabel = ui_LabelCreate(cBuff, 0, y);
				emPanelAddChild(pPanel, pLabel, false);

				pButton = ui_ButtonCreate(pChart->pcName,30, y, AnimEditor_OpenAnimChartSearchDoc, pChart);
				ui_WidgetSetWidthEx( UI_WIDGET( pButton ), 1, UIUnitPercentage );
				emPanelAddChild(pPanel, pButton, false);

				y += STANDARD_ROW_HEIGHT;
			}
			else if (5000 < numMatches) {
				pLabel = ui_LabelCreate("Stopping, found over 5000 matches!", 0, y);
				emPanelAddChild(pPanel, pLabel, false);
				y += STANDARD_ROW_HEIGHT;
				break;
			}
		}
		FOR_EACH_END;
	}

	pSeparator = ui_SeparatorCreate(UIHorizontal);
	ui_WidgetSetPosition(UI_WIDGET(pSeparator), 0, y);
	ui_WidgetSetDimensionsEx(UI_WIDGET(pSeparator), 1.0, 5, UIUnitPercentage, UIUnitFixed);
	emPanelAddChild(pPanel, pSeparator, false);
	y += SEPARATOR_HEIGHT;

	return y;
}


//---------------------------------------------------------------------------------------------------
// Checkout
//---------------------------------------------------------------------------------------------------

static void AnimEditor_PerformCheckout(UIButton *button, EMEditorDoc *pDoc)
{
	FOR_EACH_IN_EARRAY(pDoc->all_files, EMFileAssoc, pFileAssoc) {
		emuCheckoutFile(pFileAssoc->file);
	} FOR_EACH_END;
	
	if (!emDocIsEditable(pDoc, false))
		Errorf("Anim Editor: Document Checkout Failed!\n");

	if (AnimEditor_CheckoutWindow) {
		ui_WindowHide(AnimEditor_CheckoutWindow);
		ui_WidgetQueueFree(UI_WIDGET(AnimEditor_CheckoutWindow));
		AnimEditor_CheckoutWindow = NULL;
	}
}

static void AnimEditor_CancelCheckout(UIButton *button, EMEditorDoc *pDoc)
{
	if (AnimEditor_CheckoutWindow) {
		ui_WindowHide(AnimEditor_CheckoutWindow);
		ui_WidgetQueueFree(UI_WIDGET(AnimEditor_CheckoutWindow));
		AnimEditor_CheckoutWindow = NULL;
	}
}

void AnimEditor_AskToCheckout(EMEditorDoc *pDoc, const char *pcFilename)
{
	UILabel *pLabel;
	UIButton *pButton;
	char buf[1024];

	AnimEditor_CheckoutWindow = ui_WindowCreate("Save (or Delete) Failed", 200, 200, 400, 60);

	sprintf(buf, "Save (or Delete) Failed: You must first Checkout '%s' for editing!", pcFilename ? pcFilename : (pDoc->orig_doc_name ? pDoc->orig_doc_name : "UNNAMED RESOURCE"));
	pLabel = ui_LabelCreate(buf, 10, 0);
	ui_WindowAddChild(AnimEditor_CheckoutWindow, pLabel);

	pButton = ui_ButtonCreate("Checkout", 0, 0, AnimEditor_PerformCheckout, pDoc);
	ui_WidgetSetWidth(UI_WIDGET(pButton), 80);
	ui_WidgetSetPositionEx(UI_WIDGET(pButton), -90, 28, 0.5, 0, UITopLeft);
	ui_WindowAddChild(AnimEditor_CheckoutWindow, pButton);

	pButton = ui_ButtonCreate("Cancel", 0, 0, AnimEditor_CancelCheckout, pDoc);
	ui_WidgetSetWidth(UI_WIDGET(pButton), 80);
	ui_WidgetSetPositionEx(UI_WIDGET(pButton), 10, 28, 0.5, 0, UITopLeft);
	ui_WindowAddChild(AnimEditor_CheckoutWindow, pButton);

	AnimEditor_CheckoutWindow->widget.width = 20 + pLabel->widget.width;

	ui_WindowSetModal(AnimEditor_CheckoutWindow, true);
	ui_WindowSetClosable(AnimEditor_CheckoutWindow, false);
	ui_WindowShow(AnimEditor_CheckoutWindow);
}

#endif



AUTO_RUN_LATE;
void AnimEditor_RegisterFXPicker(void)
{
#ifndef NO_EDITORS
	EMPicker* fxPicker = emEasyPickerCreate( "DynFxInfo", ".dfx", "dyn/fx/", NULL );
	emPickerRegister(fxPicker);
#endif
}

#include "AnimEditorCommon_h_ast.c"
