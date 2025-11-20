
#pragma once
GCC_SYSTEM

#ifndef NO_EDITORS

#include "referencesystem.h"

#include "EditorManager.h"
#include "MultiEditField.h"
#include "dynAnimGraph.h"

typedef struct PlayerCostume PlayerCostume;
typedef struct UIButton UIButton;
typedef struct CostumeViewGraphics CostumeViewGraphics;
typedef struct AnimEditor_CostumePickerData AnimEditor_CostumePickerData;
typedef struct CBox CBox;

typedef void (*AnimEditor_PostCostumeChangeCB)(void);
typedef AnimEditor_CostumePickerData* (*AnimEditor_GetCostumePickerData)(void);
typedef void (*AnimEditor_GetPaneBox)(CBox* pBox);

extern ParseTable parse_AnimEditorPlaybackInfo[];
#define TYPE_parse_AnimEditorPlaybackInfo AnimEditorPlaybackInfo

extern const char *AnimEditor_SearchText;

extern UIWindow *AnimEditor_CheckoutWindow;

AUTO_STRUCT;
typedef struct AnimEditorPlaybackInfo
{
	F32 fPlaybackRate;
	bool bPaused;
} AnimEditorPlaybackInfo;


typedef struct AnimEditor_CostumePickerData
{
	AnimEditor_GetCostumePickerData getCostumePickerData;
	AnimEditor_PostCostumeChangeCB postCostumeChange;
	AnimEditor_GetPaneBox getPaneBox;
	CostumeViewGraphics* pGraphics;
	REF_TO(const PlayerCostume) hCostume;
	const char **eaAddedFx;
	U32 uiFrameCreated;
	U32 bMoveMultiEditor : 1;
} AnimEditor_CostumePickerData;


typedef struct MoveMEField
{
	MEField* pField;
	int iNodeIndex;
	int iMoveIndex;
} MoveMEField;

typedef struct MoveFxMEField
{
	MEField *pField;
	UIWidget *pOptWidget;
	int iNodeIndex;
	int iMoveIndex;
	int iFxIndex;
	bool bValid;
} MoveFxMEField;

typedef struct PathMEField
{
	MEField *pField;
	int iNodeIndex;
	int iPathIndex;
} PathMEField;


void AnimEditor_UICenterCamera(UIButton *pButton, AnimEditor_GetCostumePickerData getCostumePickerData);
void AnimEditor_UIFitCameraToPane(UIButton *pButton, AnimEditor_GetCostumePickerData getCostumePickerData);
void AnimEditor_CostumePicker(UIButton* button, AnimEditor_GetCostumePickerData getCostumePickerData);
void AnimEditor_LastCostume(UIButton* button, AnimEditor_GetCostumePickerData getCostumePickerData);
void AnimEditor_DrawCostume(AnimEditor_CostumePickerData* pData, F32 fDeltaTime);
void AnimEditor_DrawCostumeGhosts(CostumeViewGraphics *pGraphics, F32 fDeltaTime);
void AnimEditor_UIAddTestFX(UIButton *pButton, AnimEditor_GetCostumePickerData getCostumePickerData);
void AnimEditor_UIClearTestFX(UIButton *pButton, AnimEditor_GetCostumePickerData getCostumePickerData);
void AnimEditor_InitCostume(AnimEditor_CostumePickerData* pData);

void AnimEditor_MoveMEFieldDestroy(MoveMEField* pMoveField);
void AnimEditor_MoveFxMEFieldDestroy(MoveFxMEField *pMoveFxField);
void AnimEditor_PathMEFieldDestroy(PathMEField *pPathField);
F32 AnimEditor_ReflowNormalNode(
	DynAnimGraph *pGraph,
	void* pDoc, int pDocFocusIndex, EMPanel* pPanel,
	MEField ***eaNodeFields, MoveMEField ***eaMoveFields, MoveFxMEField ***eaMoveFxFields, UIWidget ***eaInactiveFields,
	DynAnimGraphNode *pOrigNode, DynAnimGraphNode *pEditNode,
	MEFieldPreChangeCallback FieldPreChangedCB, MEFieldChangeCallback FieldChangedCB, MEFieldChangeCallback InheritFieldChangedCB,
	UIActivationFunc AddFxCB, UIActivationFunc AddFxMessageCB, UIActivationFunc ChooseFxEventCB, UIActivationFunc OpenFxEventCB, UIActivationFunc RemoveFxEventCB,
	UIActivationFunc AddImpactCB, UIActivationFunc RemoveImpactCB,
	UIActivationFunc AddMoveCB, UIActivationFunc ChooseMoveCB, MEFieldChangeCallback MoveChangeChanceCB, UIActivationFunc OpenMoveCB, UIActivationFunc RemoveMoveCB,
	UISkin *badSkin,
	bool bCacheMoveFxFields,
	F32 y
	);
F32 AnimEditor_ReflowRandomizerNode(
	DynAnimGraph *pGraph,
	void* pDoc, int pDocFocusIndex, EMPanel* pPanel,
	MEField ***eaNodeFields, PathMEField ***eaPathFields, UIWidget ***eaInactiveFields,
	DynAnimGraphNode *pOrigNode, DynAnimGraphNode *pEditNode,
	MEFieldPreChangeCallback FieldPreChangedCB, MEFieldChangeCallback FieldChangedCB, MEFieldChangeCallback InheritFieldChangedCB,
	UIActivationFunc AddPathCB, MEFieldChangeCallback PathChangeChanceCB, UIActivationFunc RemovePathCB,
	UISkin *badSkin,
	F32 y
	);
F32 AnimEditor_ReflowExitNode(
	DynAnimGraph *pGraph,
	void *pDoc, int pDocFocusIndex, EMPanel *pPanel,
	MEField ***eaNodeFields, PathMEField ***eaPathFields, UIWidget ***eaInactiveFields,
	DynAnimGraphNode *pOrigNode, DynAnimGraphNode *pEditNode,
	MEFieldPreChangeCallback FieldPreChangedCB, MEFieldChangeCallback FieldChangedCB, MEFieldChangeCallback InheritFieldChangedCB,
	UIActivationFunc ChoosePostIdleCB, UIActivationFunc OpenPostIdleCB, UIActivationFunc RemovePostIdleCB,
	UISkin *badSkin,
	F32 y
	);
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
	);

F32 AnimEditor_Search(
	void *pDoc, EMPanel *pPanel,
	const char *pcSearchText,
	UIActivationFunc SearchTextChangedCB,
	UIActivationFunc RefreshSearchDocsCB
	);

void AnimEditor_AskToCheckout(EMEditorDoc *pDoc, const char *pcFilename);

#endif