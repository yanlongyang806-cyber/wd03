//
// SkyEditor2.h
//

#pragma once
GCC_SYSTEM

#include "EditorManager.h"

typedef struct SkyEditorDoc SkyEditorDoc;
typedef struct SkyBlockInfo SkyBlockInfo;
typedef struct EMPanel EMPanel;
typedef struct SkyInfo SkyInfo;
typedef struct MEField MEField;
typedef struct UIGraph UIGraph;
typedef struct UISlider UISlider;
typedef struct UIGraphPoint UIGraphPoint;
typedef struct UIGimmeButton UIGimmeButton;
typedef struct WleAESwapUI WleAESwapUI;

typedef int (*skyEdRefreshPanelFunc)(SkyEditorDoc *pDoc, SkyBlockInfo *pInfo, void *pOrig, void *pData, UIWidget *pParent);
typedef void (*skyEdChangedFunc)(SkyBlockInfo *pInfo, MEField *pField, void *pData);

// ------------------------------------------------------------------
// Functions
// ------------------------------------------------------------------

void skyEdInit(EMEditor *pEditor);
SkyEditorDoc* skyEdNewDoc(const char *pName);
EMTaskStatus skyEdSaveSky(SkyEditorDoc *pDoc, bool saveAsNew);
EMTaskStatus skyEdSaveAsSky(SkyEditorDoc *pDoc);
void skyEdCloseSky(SkyEditorDoc *pDoc);
void skyEdReloadSky(SkyEditorDoc *pDoc);

void skyEdDraw(SkyEditorDoc *pDoc);
void skyEdDrawGhosts(SkyEditorDoc *pDoc);
void skyEdInitGraphics(void);

void skyEdGotFocus(SkyEditorDoc *pDoc);
void skyEdLostFocus(SkyEditorDoc *pDoc);


// ------------------------------------------------------------------
// Editor Document Structure
// ------------------------------------------------------------------

AUTO_ENUM;
typedef enum SkyCCCurveTypes {
	SCCC_Red,
	SCCC_Green,
	SCCC_Blue,
	SCCC_Intensity,
	SCCC_Saturation,
} SkyCCCurveTypes;
extern StaticDefineInt SkyCCCurveTypesEnum[];

AUTO_ENUM;
typedef enum SkyCCLevelTypes {
	SCCL_Red,
	SCCL_Green,
	SCCL_Blue,
	SCCL_Intensity,
} SkyCCLevelTypes;
extern StaticDefineInt SkyCCLevelTypesEnum[];

AUTO_ENUM;
typedef enum SkyViewType {
	SVT_WorldAndEdit,		ENAMES("World and Editing")
	SVT_JustEdit,			ENAMES("Just Editing")
	SVT_JustWorld,			ENAMES("Just World")
} SkyViewType;
extern StaticDefineInt SkyViewTypeEnum[];

typedef struct GenericSkyBlock
{
	F32  time;
} GenericSkyBlock;

typedef struct SkyBlockInfo {

	SkyEditorDoc *pDoc;

	const char *pcDisplayName;

	ParseTable *pBlockPti;
	int iColumn;
	int iBlendedColumn;

	bool bIsSkyDome;
	int iSkyDomeIdx;
	int iSelectedLensFlare;

	GenericSkyBlock **ppSelected;
	int *ipPrevSelected;

	UITimelineTrack *pTrack;
	EMPanel *pPanel;
	UIPane *pPane;
	skyEdRefreshPanelFunc refreshFunc;
	skyEdChangedFunc changedFunc;

} SkyBlockInfo;

typedef struct SkyEditorUIElement {

	UILabel *pLabel;
	MEField *pField;
	UIAnyWidget *pWidget;
	
	const char *pcBlockInfo;
	const char *pcFieldName;
	const char *pcDisplayName;

} SkyEditorUIElement;

typedef struct SkyEditorUI {

	//////
	// Timeline
	UIPane *pTimelinePane;
	UITimeline *pTimeline;
	UIButton *pNewBlockButton;

	//////
	// Sky Info File
	EMPanel *pFilePanel;
	UIGimmeButton *pFileButton;
	UILabel *pFilenameLabel;

	//////
	// Sky Info General
	EMPanel *pGeneralPanel;
	UIPane *pGeneralPane;

	//////
	// Fog
	MEField *pFogHeightLow;
	MEField *pFogHeightHigh;
	F32 fFogHeight;
	int iFogHeightFade;

	//////
	// Color Correction
	UIGraph *pCCGraph;
	UIComboBox *pCCCurvesChanelCombo;
	UIButton *pCCCurvesResetButton;
	UIComboBox *pCCLevelsChanelCombo;
	UIButton *pCCLevelsResetButton;
	UISlider *pCCLevelsInputSlider;
	UISlider *pCCLevelsOutputSlider;
	UITextEntry *pCCLevelsTextEntries[5];
	UIColorButton *pCCAdjustmentButtons[6];

	//////
	// Sky Dome All Types
	EMPanel *pSkyDomeCommonPanel;
	UIPane *pSkyDomeCommonPane;
	UIComboBox *pSkyDomeTypeCombo;
	WleAESwapUI *pSkyDomeTextureSwapsUI;
	//////
	// Sky Dome Luminary
	EMPanel *pSkyDomeLuminaryPanel;
	UIPane *pSkyDomeLuminaryPane;
	//////
	// Sky Dome Object
	EMPanel *pSkyDomeObjectPanel;
	UIPane *pSkyDomeObjectPane;
	//////
	// Sky Dome Star Field
	EMPanel *pSkyDomeStarFieldPanel;
	UIPane *pSkyDomeStarFieldPane;
	//////
	// Sky Dome Atmosphere
	EMPanel *pSkyDomeAtmospherePanel;
	UIPane *pSkyDomeAtmospherePane;
	//////
	// Sky Dome Lens Flare
	EMPanel *pSkyDomeLensFlarePanel;
	UIPane *pSkyDomeLensFlarePane;


	SkyEditorUIElement **ppElementList;

} SkyEditorUI;

typedef struct SkyEditorDoc {
	EMEditorDoc emDoc;	// NOTE: This must be first for EDITOR MANAGER

	SkyInfo *pSkyInfo;
	SkyInfo *pOrigSkyInfo;
	SkyInfo *pNextUndoSkyInfo;
	bool bNewSky;
	bool bSavingAs;
	bool bRefreshingUI;
	F32 fPlaySpeed;

	SkyBlockInfo **ppBlockInfos;
	int iPrevSelectedSkyDomeIdx;
	int *ipPrevSelectedSkyDomeTimes;

	SkyEditorUI UI;

} SkyEditorDoc;