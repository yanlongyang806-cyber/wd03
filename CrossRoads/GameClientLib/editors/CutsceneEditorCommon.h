#ifndef __CUTSCENE_EDITOR_COMMON_H__
#define __CUTSCENE_EDITOR_COMMON_H__
GCC_SYSTEM

#ifndef NO_EDITORS

#include "EditorManager.h"

typedef struct CutsceneDef CutsceneDef;
typedef struct CutscenePath CutscenePath;
typedef struct CutsceneEditorState CutsceneEditorState;
typedef struct CutsceneGenericTrackInfo CutsceneGenericTrackInfo;
typedef struct CutsceneEditorDoc CutsceneEditorDoc;
typedef struct EditUndoStack EditUndoStack;
typedef struct RotateGizmo RotateGizmo;
typedef struct TranslateGizmo TranslateGizmo;
typedef struct UISlider UISlider;
typedef struct UIButton UIButton;
typedef struct UIGimmeButton UIGimmeButton;
typedef struct UITextEntry UITextEntry;
typedef struct UILabel UILabel;

AUTO_ENUM;
typedef enum CutsceneEditType
{
	CutsceneEditType_Camera = 0,
	CutsceneEditType_CameraPath,
	CutsceneEditType_LookAtPath,
} CutsceneEditType;
extern StaticDefineInt CutsceneEditTypeEnum[];

AUTO_ENUM;
typedef enum CutsceneEditMode
{
	CutsceneEditMode_Translate = 0,
	CutsceneEditMode_Rotate,
} CutsceneEditMode;
extern StaticDefineInt CutsceneEditModeEnum[];

typedef void (*cutEdUICreateCGTFunc)(CutsceneEditorState *pState, CutsceneGenericTrackInfo *pInfo, void *pCont);
typedef void (*cutEdUIRefreshCGTFunc)(CutsceneGenericTrackInfo *pInfo);
typedef void (*cutEdUIGenPntFunc)(CutsceneGenericTrackInfo *pInfo, void *pPoint);
typedef bool (*cutEdUIGenPntPredicateFunc)(CutsceneGenericTrackInfo *pInfo, void * pPoint);
typedef bool (*cutEdUIGenPntMatFunc)(CutsceneGenericTrackInfo *pInfo, void *pPoint, Mat4 mMatrix);
typedef bool (*cutEdUIGenPntBoundsFunc)(CutsceneEditorState *pState, void *pCGT, void *pPoint, Vec3 pointPos, Vec3 minBounds, Vec3 maxBounds);
typedef void (*cutEdCGTApplyFunc)(CutsceneEditorState *pState, CutsceneGenericTrackInfo *pInfo, void *pCGT, UserData pData);
typedef void (*cutEdUISetEditModeFunc)(CutsceneEditMode mode);

typedef struct CutsceneGenPntRef
{
	void *pGenPnt;
	void *pCGT;
	UITimelineTrack *pTrack;
} CutsceneGenPntRef;

/*
 * Used as registration, storage, and manipulation of a particular type of track info.
 *
 * There exists one of these per track type (e.g. Camera Shake, Entities, Camera FOV, Sounds, Textures, FX, and so on)
 * on each CutsceneEditorState.
 *
 * Registering a new track type is done by calling cutEdRegisterCGTInfo for each created CutsceneEditorState.
 *
 * TODO: Split this structure up into meta data and state data (andrewa)
 */
typedef struct CutsceneGenericTrackInfo
{
	CutsceneEditorState *pState;

	char *pcCGTName;								// Default name to be used for the track
	
	int iCGTColumn;
	ParseTable *pCGTPti;							// Parse table for the list
	ParseTable *pGenPntPti;							// Parse table for the points inside the list

	CutsceneGenPntRef **ppSelected;					// Currently selected points

	void *pGenPntCont;								// Expander or Panel depending on mode

	UITimelineTrack *pTrack;						// Used if only a single track of this type is allowed
	UITimelineTrack **ppTracks;						// Used if multiple tracks of this type are allowed

	cutEdUIRefreshCGTFunc pRefreshFunc;
	cutEdUIGenPntFunc pInitGenPntFunc;
	cutEdUIGenPntPredicateFunc pGenPntValidReferencePntFunc;	// Used to check if a point can be used as a valid reference point when placing a new point
	cutEdUIGenPntMatFunc pGetGenPntMatFunc;			// Getter for gizmo manipulation
	cutEdUIGenPntMatFunc pSetGenPntMatFunc;			// Setter for gizmo manipulation
	cutEdUIGenPntBoundsFunc pGetGenPntBoundsFunc;	// Getter for an individual point bounds
	cutEdCGTApplyFunc pDrawFunc;

	Color color;									// Color for the points

	U8 bMulti				: 1;					// Allows more than one track to be created of this type
	U8 bAlwaysCameraPlace	: 1;					// Places new points close to the camera rather than blending with prev and last
	U8 bPreventOrderChanges : 1;					// UITimelineTrack Flag
	U8 bAllowOverlap		: 1;					// UITimelineTrack Flag
	U8 bAllowResize			: 1;					// UITimelineTrack Flag
} CutsceneGenericTrackInfo;

typedef void (*cutEdUIContainerFunc)(UserData, UserData);

typedef struct CutsceneEditorState
{
	CutsceneDef *pCutsceneDef;
	CutsceneDef *pOriginalCutsceneDef;	//ShawnF TODO: I am not using this yet, delete it or use it
	CutsceneDef *pNextUndoCutsceneDef;

	CutscenePath *pSelectedPath;
	int *piSelectedCamPosPoints;
	int *piSelectedLookAtPoints;
	int selectedPoint;
	CutsceneEditType editType;

	EditUndoStack *edit_undo_stack;		//Only set if there is no EM framework

	F32 cutsceneTime;
	F32 cutsceneMoveTime;
	F32 cutscenePathTime;
	F32 cutscenePathMoveTime;
	Vec3 cameraPos;
	Vec3 cameraPyr;

	CutsceneEditorDoc *pParentDoc;

	F32 playingTimestep;

	U8 bInited : 1;
	U8 bUnsaved : 1;					//Only set if there is no EM framework
	U8 bPlaying : 1;
	U8 bPickingEnt : 1;
	bool bHidePaths;					//Not bit fields so that they can be passed by address
	bool bDetachedCamera;					

	CutsceneGenericTrackInfo **ppTrackInfos;	//Effect and event track data

	//////////////////////////////////////////////////////////////////////////
	// UI

	UILabel *pFilenameLabel;
	UIGimmeButton *pFileButton;
	//////
	// Right Click Menus
	UIMenu *pMenuTimeline;
	//////
	// Timeline
	UIPane *pTimelinePane;
	UITimeline *pTimeline;
	UITimelineTrack *pTrackCamPos;
	UITimelineTrack *pTrackCamLookAt;
	//////
	// Preview Container
	UIButton *pUIButtonStop;
	UITextEntry *pUITextRecordFileName;
	UICheckButton *pUICheckPlayOnceOnly;
	UICheckButton *pUICheckSinglePlayer;
	UICheckButton *pUICheckHideAllPlayers;
	UICheckButton *pUICheckMakePlayersUntargetable;
	UICheckButton *pUICheckSkippable;
	UICheckButton *pUICheckDisableCamLight;
	UITextEntry *pUITextTweenToGameCamera;
	UITextEntry *pUITextTimeToFade;
	//////
	// List of Paths Container
	UIButton *pUIButtonSaveDemo;
	UIList *pUIListPaths;
	UIComboBox *pUIComboEditType;
	UIComboBox *pUIComboEditMode;
	UICheckButton *pUICheckSmoothCamPath;
	UICheckButton *pUICheckSmoothLookAtPath;
	UITextEntry *pUITextMapName;
	UITextEntry *pUITextBlendRate;
	UITextEntry *pUITextTotalTime;
	UITextEntry *pUITextTotalMoveTime;
	UITextEntry *pUITextPathTime;
	UITextEntry *pUITextPathMoveTime;
	UIMenu *pUIMenuPathRClick;
	UITextEntry *pUITextSendRange;
	//////
	// Path Info Container
	UIList *pUIListPosPoints;
	UIList *pUIListLookAtPoints;
	UITextEntry *pUITextAngle;
	UITextEntry *pUITextWatchTarget;
	UITextEntry *pUITextWatchCutsceneEntName;
	UITextEntry *pUITextWatchEncounterName;
	UITextEntry *pUITextWatchActorName;
	const char **eaWatchActorNameModel;
	UIComboBox *pUIComboWatchType;
	UITextEntry *pUITextWatchBoneName;
	const char **eaWatchBoneNameModel;
	UICheckButton *pUICheckPathTwoRelPos;
	UITextEntry *pUITextWatchCutsceneEntName2;
	UITextEntry *pUITextWatchEncounterName2;
	UITextEntry *pUITextWatchActorName2;
	const char **eaWatchActorNameModel2;
	UIComboBox *pUIComboWatchType2;
	UITextEntry *pUITextWatchBoneName2;
	const char **eaWatchBoneNameModel2;
	//////
	// Point Info Container
	UITextEntry *pUITextPointXPos;
	UITextEntry *pUITextPointYPos;
	UITextEntry *pUITextPointZPos;
	UITextEntry *pUITextPointPRot;
	UITextEntry *pUITextPointYRot;
	UITextEntry *pUITextTime;
	UITextEntry *pUITextLength;
	UICheckButton *pUICheckEaseIn;
	UICheckButton *pUICheckEaseOut;

	//////
	// Shared CGT UI
	UITextEntry *pUITextTrackName;

	UICheckButton *pUICheckRelPosOn;
	UIComboBox *pUIComboRelPosType;
	UITextEntry *pUITextRelPosTarget;
	UITextEntry *pUITextRelPosCutsceneEntName;
	UITextEntry *pUITextRelPosEncounterName;
	UITextEntry *pUITextRelPosActorName;
	const char **eaRelPosActorNameModel;
	UITextEntry *pUITextRelPosBoneName;
	const char **eaRelPosBoneNameModel;

	UICheckButton *pUICheckTwoRelPos;
	UIComboBox *pUIComboRelPosType2;
	UITextEntry *pUITextRelPosTarget2;
	UITextEntry *pUITextRelPosCutsceneEntName2;
	UITextEntry *pUITextRelPosEncounterName2;
	UITextEntry *pUITextRelPosActorName2;
	const char **eaRelPosActorNameModel2;
	UITextEntry *pUITextRelPosBoneName2;
	const char **eaRelPosBoneNameModel2;

	//////////////////////////////////////////////////////////////////////////
	// CutsceneEffectsAndEvents
	// Label and add any UI Pointers you need here

	//////
	// Camera Fade Info Container
	UITextEntry *pUITextFadeAmount;
	UIColorButton *pUIColorFade;
	UICheckButton *pUIFadeAdditive;
	//////
	// Depth of Field Info Container
	UICheckButton *pUICheckPointUseDoF;
	UITextEntry *pUITextPointDoFBlur;
	UITextEntry *pUITextPointDoFDist;
	//////
	// Field of View Info Container
	UITextEntry *pUITextFOV;
	//////
	// Camera Shake Info Container
	UITextEntry *pUIShakeTime;
	UITextEntry *pUIShakeLength;
	UITextEntry *pUIShakeMagnitude;
	UITextEntry *pUIShakeVertical;
	UITextEntry *pUIShakePan;
	//////
	// Object Info Container
	UIButton *pUIObjectPickerButton;
	UITextEntry *pUIObjectAlpha;
	UITextEntry *pUIObjectXPos;
	UITextEntry *pUIObjectYPos;
	UITextEntry *pUIObjectZPos;
	UITextEntry *pUIObjectPRot;
	UITextEntry *pUIObjectYRot;
	UITextEntry *pUIObjectRRot;
	//////
	// Entity Info Container
	UIComboBox *pUIEntityType;
	UIComboBox *pUIEntityCostume;
	int iOverlayCostumesStartAtY;
	UITextEntry **eaUIEntityEquipment;
	UIComboBox **eaUIEntityEquipmentSlots;
	UICheckButton *pUIEntityPreserveMovementFX;
	UITextEntry *pUIEntityTeamIdx;
	UIComboBox *pUIEntityClassType;
	UITextEntry *pUIEntityEncounterName;
	UITextEntry *pUIEntityActorName;
	const char **eaEntityActorNameModel;
	UIComboBox *pUIEntityActionType;
	UITextEntry *pUIEntityLength;
	UITextEntry *pUIEntityAnimation;
	UIComboBox *pUIEntityStance;
	UIComboBox *pUIEntityFX;
	UICheckButton *pUIEntityFXFlash;
	UITextEntry *pUIEntityXPos;
	UITextEntry *pUIEntityYPos;
	UITextEntry *pUIEntityZPos;
	UITextEntry *pUIEntityYRot;
	//////
	// Texture Info Container
	UITextureEntry *pUITextureName;
	UITextEntry *pUITextureVariable;
	UIComboBox *pUITextureXAlign;
	UIComboBox *pUITextureYAlign;
	UITextEntry *pUITextureXPos;
	UITextEntry *pUITextureYPos;
	UITextEntry *pUITextureAlpha;
	UITextEntry *pUITextureScale;
	//////
	// FX Info Container
	UIComboBox *pUIComboFX;
	UITextEntry *pUIFXLength;
	UITextEntry *pUIFXXPos;
	UITextEntry *pUIFXYPos;
	UITextEntry *pUIFXZPos;
	//////
	// Sound Info Container
	UITextEntry *pUISoundVariable;
	UIComboBox *pUIComboSound;
	UITextEntry *pUITextSoundLength;
	UITextEntry *pUITextSndCutEntName;
	UICheckButton *pUICheckSoundCamPos;
	UITextEntry *pUISoundXPos;
	UITextEntry *pUISoundYPos;
	UITextEntry *pUISoundZPos;
	//////
	// Subtitle Info Container
	UITextEntry *pUISubtitleVariable;
	UIMessageEntry *pUISubtitleMessage;
	UITextEntry *pUISubtitleLength;
	UITextEntry *pUISubtitleCutEntName;
	//////
	// UI Gen Info Container
	UITextEntry *pUIUIGenTime;
	UIComboBox *pUIUIGenActionType;
	UITextEntry *pUIUIGenMessage;
	UITextEntry *pUIUIGenVariable;
	UITextEntry *pUIUIGenStringValue;
	UITextEntry *pUIUIGenFloatValue;
	UIMessageEntry *pUIUIGenMessageValue;
	UITextEntry *pUIUIGenMessageValueVariable;

	//////
	// Skins
	UISkin *pSkinBlue, *pSkinGreen, *pSkinRed, *pSkinWindow;

	//////
	// Containers
	UIExpanderGroup *pExpanderGroup; //If no parent doc, the we are in a window and using expanders
	//Expanders or Panels depending on mode
	void *pFileCont;
	void *pPreviewCont;
	void *pPathListCont;
	void *pPointListCont;
	void *pBasicPathCont;
	void *pCirclePathCont;
	void *pWatchPathCont;
	void *pPointCont;
	void *pRelativePosCont;
	void *pGenPntCont;
	void *pGenCGTCont;
	cutEdUIContainerFunc pInsertIntoContFunc;
	cutEdUIContainerFunc pAddContFunc;
	cutEdUIContainerFunc pRemoveContFunc;
	cutEdUISetEditModeFunc pUISetEditModeFunc;
} CutsceneEditorState;

typedef struct CSEUndoData 
{
	CutsceneDef *pPreCutsceneDef;
	CutsceneDef *pPostCutsceneDef;
} CSEUndoData;

#endif // NO_EDITORS

typedef struct CutsceneEditorState CutsceneEditorState;
typedef struct CSEUndoData CSEUndoData;

extern void cutEdInitCommon(CutsceneEditorState *pState, CutsceneDef *pCutsceneDef);
extern void cutEdInitUICommon(CutsceneEditorState *pState, CutsceneDef *pCutsceneDef);
extern void cutEdRefreshUICommon(CutsceneEditorState *pState);
extern void cutEdSetUnsaved(CutsceneEditorState *pState);
extern void cutEdReInitCGTs(CutsceneEditorState *pState);

extern void cutEdInitGizmos(CutsceneEditorState *pState);
extern RotateGizmo *cutEdRotateGizmo();
extern TranslateGizmo *cutEdTranslateGizmo();

extern void cutEdStop(CutsceneEditorState *pState);

extern CutsceneEditMode cutEdCutsceneEditMode();
extern void cutEdSetCutsceneEditMode(CutsceneEditMode mode);

extern void cutEdTick(CutsceneEditorState *pState);
extern void cutEdDraw(CutsceneEditorState *pState);

extern void cutEdUndo(CutsceneEditorState *pState, CSEUndoData *pData);
extern void cutEdRedo(CutsceneEditorState *pState, CSEUndoData *pData);

void cutEdFixupMessages(CutsceneDef *pDef);

// Feet per second
#define CSE_DEFAULT_SPEED 32.0f
#define CSE_DEFAULT_BLEND_RATE 0.75f
#define CSE_DEFAULT_SEND_RANGE 0.0f

#define CSE_UI_GAP 5

#endif // __CUTSCENE_EDITOR_COMMON_H__
