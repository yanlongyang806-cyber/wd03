//
// CostumeEditor.h
//

#pragma once
GCC_SYSTEM

#include "gclCostumeView.h"
#include "dynDraw.h"
#include "dynSkeleton.h"
#include "EditorManager.h"
#include "CostumeCommonTailor.h"
#include "wlCostume.h"
#include "UIFXButton.h"

typedef struct CostumeEditDoc CostumeEditDoc;

typedef struct GfxCameraController GfxCameraController;
typedef struct MEField MEField;
typedef struct UIButton UIButton;
typedef struct UIFilteredList UIFilteredList;
typedef struct UILabel UILabel;
typedef struct UIMenu UIMenu;
typedef struct UISeparator UISeparator;
typedef struct UIWindow UIWindow;
typedef struct UIGimmeButton UIGimmeButton;
typedef struct ItemDef ItemDef;

#ifndef NO_EDITORS

#define COSTUME_EDITOR "Costume Editor"

// ---- Public Interface ------------------------------------------------------

// This is called to open a costume edit document
// If the name does not exist, then a new costume is created
CostumeEditDoc *costumeEdit_OpenCostume(EMEditor *pEditor, const char *pcName);

// This is called to open a costume edit document
// If the name does not exist, returns NULL
CostumeEditDoc *costumeEdit_OpenCloneOfCostume(EMEditor *pEditor, const char *pcName);

// This is called to save the costume being edited
EMTaskStatus costumeEdit_SaveCostume(CostumeEditDoc *pDoc, bool bSaveAsNew);

// This is called to close the costume being edited
void costumeEdit_CloseCostume(CostumeEditDoc *pDoc);

// This is called prior to close to see if the costume can be closed
bool costumeEdit_CloseCheck(CostumeEditDoc *pDoc);

// These are called for focus changes
void costumeEdit_GotFocus(CostumeEditDoc *pDoc);
void costumeEdit_LostFocus(CostumeEditDoc *pDoc);

// This is called once to initialize global data
void costumeEdit_InitData(EMEditor *pEditor);

// Tells if a given costume is already open
bool costumeEditorEMIsDocOpen(char *pcName);

// Gets a document if it is open and NULL otherwise
CostumeEditDoc *costumeEditorEMGetOpenDoc(const char *pcName);

// Gets all open documents
void costumeEditorEMGetAllOpenDocs(CostumeEditDoc ***peaDocs);

// Gets the editor
EMEditor *costumeEditorEMGetEditor(void);


// ---- Graphics Control ------------------------------------------------------

// This is called during the per-frame drawing of ghosts
void costumeEdit_DrawGhosts(CostumeEditDoc *pDoc);

// This is called during the per-frame drawing
void costumeEdit_Draw(CostumeEditDoc *pDoc);


// ---- Internal --------------------------------------------------------------

void costumeEdit_CostumeRevert(CostumeEditDoc *pDoc);
void costumeEdit_SelectBone(CostumeEditDoc *pDoc, PCBoneDef *pBoneDef);
void costumeEdit_SetPartGeometry(CostumeEditDoc *pDoc, const char *pcBone, const char *pcGeo);
void costumeEdit_SetPartMaterial(CostumeEditDoc *pDoc, const char *pcBone, const char *pcMat, const char *pcGeo);
void costumeEdit_SetPartTexture(CostumeEditDoc *pDoc, const char *pcBone, const char *pcGeo, const char *pcMat, const char *pcTex, PCTextureType eTexType);
void costumeEdit_UICostumeClone(CostumeEditDoc *pDoc);
void costumeEdit_UICenterCamera(UIButton *pButton, EMEditor *pEditor);
void costumeEdit_UICameraRotateLeftStart(UIButton *pButton, EMEditor *pEditor);
void costumeEdit_UICameraRotateRightStart(UIButton *pButton, EMEditor *pEditor);
void costumeEdit_UICameraRotateStop(UIButton *pButton, EMEditor *pEditor);


// ---- Misc Stuff------ ------------------------------------------------------

#endif

// Need to have auto-structs present even if NO_EDITORS defined

// Structure for storing the animations list
AUTO_STRUCT;
typedef struct CostumeAnim {
	char * pcName;
	char * pcBits;
	char * pcAnimStanceWords;
	char * pcAnimKeyword;
	char * pcAnimMove;
} CostumeAnim;

AUTO_STRUCT;
typedef struct CostumeAnimList {
	CostumeAnim **ppAnims;	AST(NAME("Animation"))
} CostumeAnimList;

AUTO_ENUM;
typedef enum CostumeEditGlow {
	kGlow_NoGlow = 0,
	kGlow_2x     = 2,
	kGlow_3x     = 3,
	kGlow_4x     = 4,
	kGlow_5x     = 5,
	kGlow_6x     = 6,
	kGlow_7x     = 7,
	kGlow_8x     = 8,
	kGlow_9x     = 9,
} CostumeEditGlow;

AUTO_STRUCT;
typedef struct CostumeEditColorLink {
	PCColorLink eColorLink;
} CostumeEditColorLink;

AUTO_STRUCT;
typedef struct CostumeEditMaterialLink {
	PCColorLink eMaterialLink;
} CostumeEditMaterialLink;

AUTO_STRUCT;
typedef struct CostumeEditGlowStruct {
	CostumeEditDoc *pDoc;  NO_AST
	int index;
	CostumeEditGlow glow;
} CostumeEditGlowStruct;

AUTO_STRUCT;
typedef struct CostumeEditReflectStruct {
	CostumeEditDoc *pDoc;  NO_AST
	int index;
	int reflection;
	int specularity;
} CostumeEditReflectStruct;

AUTO_STRUCT;
typedef struct CostumeEditDyePackStruct {
	const char *pchDyePack;				AST(POOL_STRING)
	const char *pchDisplayName;			AST(POOL_STRING)
} CostumeEditDyePackStruct;

#ifndef NO_EDITORS

// Used for menu callbacks
typedef struct CEMenuData {
	CostumeEditDoc *pDoc;
	char *pcMenuName;
} CEMenuData;

typedef struct CostumeBodyScaleGroup {
	CostumeEditDoc* pDoc;
	int iComboIndex;
	UIComboBox *pScaleComboBox;
	MEField *pScaleSliderField;
	UILabel *pScaleLabel;

	PCBodyScaleValue **eaValues;
} CostumeBodyScaleGroup;

typedef struct CostumeBitsGroup {
	CostumeEditDoc* pDoc;
	MEField *pField;
	UIFilteredComboBox *pComboBox;
	UIButton *pRemoveButton;
} CostumeBitsGroup;

typedef struct CostumeTexWordsGroup {
	CostumeEditDoc* pDoc;
	UILabel *pKeyLabel;
	MEField *pField;
} CostumeTexWordsGroup;

typedef struct CostumeColorGroup {
	CostumeEditDoc* pDoc;

	const char *pcName;
	bool bAvailable;
	bool bIsSet;
	bool bIsColor;
	U8 currentColor[4];
	F32 currentValue[4];

	UICheckButton *pCheck;
	UILabel *pLabel;
	UIColorButton *pColorButton;
	UISliderTextEntry *pSlider1;
	UISliderTextEntry *pSlider2;
	UISliderTextEntry *pSlider3;
	UISliderTextEntry *pSlider4;
	UILabel* pSubLabel1;
	UILabel* pSubLabel2;
	UILabel* pSubLabel3;
	UILabel* pSubLabel4;
} CostumeColorGroup;

typedef struct CostumeFxParamGroup {
	const char *pcName;
	eDynParamType type;
	const char *pcDictName;

	UICheckButton *pParamCheckButton;
	UIWidget *pParamWidget;
} CostumeFxParamGroup;

typedef struct CostumeFxGroup {
	CostumeEditDoc* pDoc;
	MEField *pFxField;
	CostumeFxParamGroup **eaParamGroups;
	UIFXButton *pFXButton;
	UIButton *pRemoveButton;
	UISeparator *pSeparator;
} CostumeFxGroup;

typedef struct CostumeFxSwapGroup {
	CostumeEditDoc* pDoc;
	MEField *pFxOldField;
	MEField *pFxNewField;
	UIButton *pRemoveButton;
	UISeparator *pSeparator;
} CostumeFxSwapGroup;

typedef struct CostumeScaleGroup {
	CostumeEditDoc* pDoc;
	const char *pcName;
	const char *pcDisplayName;
	bool bIsGroup;
	bool bIsChild;
	UILabel *pLabel;
	MEField *pSliderField;
	UIComboBox *pComboBox;
	PCPresetScaleValueGroup **eaPresets;
} CostumeScaleGroup;

typedef struct CostumeTexGroup {
	CostumeEditDoc* pDoc;
	MEField *pField;
	UIButton *pRemoveButton;
} CostumeTexGroup;

// ---- Editor Document -------------------------------------------------------

#define DEFAULT_COSTUME_NAME     "New_Costume"
#define DEFAULT_COSTUME_FILE     "NoFileChosen"

typedef struct CostumeEditDoc {
	// NOTE: This must be first for EDITOR MANAGER
	EMEditorDoc emDoc;

	CostumeViewGraphics *pGraphics;

	// The current and original costumes
	NOCONST(PlayerCostume) *pCostume;
	NOCONST(PlayerCostume) *pOrigCostume;
	NOCONST(PlayerCostume) *pHoverCostume;

	// Editor State Flags
	bool bSkelChanged;
	bool bIgnorePrefChanges;
	bool bIgnoreFieldChanges;
	bool bAllowGlowOverride;
	bool bScaleSeperateOverride;
	bool bAllowMovableOverride;
	bool bAllowReflectOverride;
	bool bSaveOverwrite;
	bool bSaveRename;

	// Editor Selected Objects
	PCBoneDef *pPrevSelectedBoneDef;
	PCGeometryDef *pPrevSelectedGeoDef;
	PCGeometryDef *pPrevSelectedOrigGeoDef;
	PCMaterialDef *pPrevSelectedMatDef;
	PCMaterialDef *pPrevSelectedOrigMatDef;

	// Save and Close State Flags
	bool bSaved;

	// UI controls
	UIWindow *pMainWindow;
	UILabel *pFilenameLabel;
	UIGimmeButton *pGimmeButton;
	UIWindow *pDetailWindow;
	UIList *pBoneList;
	UILabel *pColor1Label;
	UILabel *pColor2Label;
	UILabel *pColor3Label;
	UIComboBox *pRegionCombo;
	UIComboBox *pCategoryCombo;
	UIComboBox *pBoneCombo;
	UIComboBox *pLayerCombo;
	UICheckButton *pGeoLinkButton;
	UIColorButton *pCButton0;
	UIColorButton *pCButton1;
	UIColorButton *pCButton2;
	UIColorButton *pCButton3;
	EMPanel *pMainSettingsPanel;
	EMPanel *pBodySettingsPanel;
	UILabel *pHeightLabel;
	UILabel *pMuscleLabel;
	UILabel *pStanceLabel;
	EMPanel *pReflectPanel;
	UILabel *pReflectLabel;
	UILabel *pSpecularLabel;
	UIComboBox *pAnimCombo;
	EMPanel *pBitsPanel;
	UIButton *pBitsAddButton;
	UILabel *pBitsLabel;
	EMPanel *pArtistColorsPanel;
	EMPanel *pFxPanel;
	UIButton *pFxAddButton;
	UILabel *pFxLabel;
	UILabel *pDismountFxLabel;
	EMPanel *pFxSwapPanel;
	UIButton *pFxSwapAddButton;
	UILabel *pFxSwapLabel;
	EMPanel *pScalePanel;
	UILabel *pNoScaleLabel;
	UIButton *pZeroScaleButton;
	EMPanel *pArtistTexPanel;
	UIButton *pTexAddButton;
	UILabel *pTexLabel;
	UILabel* pGeometryTextLabel;
	UILabel* pMaterialTextLabel;
	UILabel* pPatternTextLabel;
	UILabel* pDetailTextLabel;
	UILabel* pSpecularTextLabel;
	UILabel* pDiffuseTextLabel;
	UILabel* pMovableTextLabel;
	UILabel* pPatternValueLabel;
	UILabel* pDetailValueLabel;
	UILabel* pSpecularValueLabel;
	UILabel* pDiffuseValueLabel;
	UILabel* pMovableValueLabel;

	// Costume Fields
	MEField *pNameField;
	MEField *pScopeField;
	MEField *pCostumeTypeField;
	MEField *pSkeletonField;
	MEField *pSpeciesField;
	MEField *pVoiceField;
	MEField *pColorLinkAllField;
	MEField *pMatLinkAllField;
	MEField *pAccountUnlockField;
	MEField *pBodySockDisableField;
	MEField *pCollisionDisableField;
	MEField *pShellDisableField;
	MEField *pRagDollDisableField;
	MEField *pBodySockGeoField;
	MEField *pBodySockPoseField;
	MEField *pBodySockMinXField;
	MEField *pBodySockMinYField;
	MEField *pBodySockMinZField;
	MEField *pBodySockMaxXField;
	MEField *pBodySockMaxYField;
	MEField *pBodySockMaxZField;
	MEField *pCollisionGeoField;
	MEField *pMuscleField;
	MEField *pHeightField;
	MEField *pStanceField;
	UILabel *pMovPosXLabel;
	UILabel *pMovPosYLabel;
	UILabel *pMovRotLabel;
	UILabel *pMovScaleXLabel;
	UILabel *pMovScaleYLabel;
	UICheckButton *pScaleSeperateOption;
	UICheckButton *pMovableOverride;

	// Part Fields
	MEField *pGeoField;
	MEField *pChildGeoField;
	MEField *pMatField;
	MEField *pDetailField;
	MEField *pPatternField;
	MEField *pSpecularField;
	MEField *pDiffuseField;
	MEField *pMovableField;
	MEField *pGlow0Field;
	MEField *pGlow1Field;
	MEField *pGlow2Field;
	MEField *pGlow3Field;
	MEField *pCustomizeReflectField;
	MEField *pReflect0Field;
	MEField *pReflect1Field;
	MEField *pReflect2Field;
	MEField *pReflect3Field;
	MEField *pCustomizeSpecularField;
	MEField *pSpecular0Field;
	MEField *pSpecular1Field;
	MEField *pSpecular2Field;
	MEField *pSpecular3Field;
	MEField *pColorLinkField;
	MEField *pMaterialLinkField;
	MEField *pNoShadowField;
	EMPanel *pCostumePartPanel;
	MEField *pPatternValueField;
	MEField *pDetailValueField;
	MEField *pSpecularValueField;
	MEField *pDiffuseValueField;
	MEField *pMovableValueField;
	MEField *pMovPosXField;
	MEField *pMovPosYField;
	MEField *pMovRotField;
	MEField *pMovScaleXField;
	MEField *pMovScaleYField;
	MEField *pDyePackField;

	// UI Temp Data
	PCRegion *pCurrentRegion;
	PCCategory *pCurrentCategory;
	NOCONST(PCPart) *pCurrentPart;
	NOCONST(PCPart) *pOrigPart;
	NOCONST(PCPart) *pColorButtonCurrentPart;
	PCLayer *pCurrentLayer;
	CostumeEditColorLink currentColorLink;
	CostumeEditColorLink origColorLink;
	CostumeEditMaterialLink currentMaterialLink;
	CostumeEditMaterialLink origMaterialLink;
	CostumeEditGlowStruct origGlow0;
	CostumeEditGlowStruct currentGlow0;
	CostumeEditGlowStruct origGlow1;
	CostumeEditGlowStruct currentGlow1;
	CostumeEditGlowStruct origGlow2;
	CostumeEditGlowStruct currentGlow2;
	CostumeEditGlowStruct origGlow3;
	CostumeEditGlowStruct currentGlow3;
	CostumeEditReflectStruct origReflect0;
	CostumeEditReflectStruct currentReflect0;
	CostumeEditReflectStruct origReflect1;
	CostumeEditReflectStruct currentReflect1;
	CostumeEditReflectStruct origReflect2;
	CostumeEditReflectStruct currentReflect2;
	CostumeEditReflectStruct origReflect3;
	CostumeEditReflectStruct currentReflect3;
	PCSkeletonDef **eaSkels;
	SpeciesDef **eaSpecies;
	PCVoice **eaVoices;
	PCStanceInfo **eaStances;
	PCRegion **eaRegions;
	PCCategory **eaCategories;
	PCBoneDef **eaBones;
	PCLayer **eaLayers;
	PCGeometryDef **eaGeos;
	PCGeometryDef **eaChildGeos;
	PCMaterialDef **eaMats;
	PCTextureDef **eaDetailTex;
	PCTextureDef **eaPatternTex;
	PCTextureDef **eaSpecularTex;
	PCTextureDef **eaDiffuseTex;
	PCTextureDef **eaMovableTex;
	CostumeEditDyePackStruct *pCurrentDyePack;

	CostumeBodyScaleGroup **eaBodyScaleGroups;
	CostumeBitsGroup **eaBitsGroups;
	CostumeColorGroup **eaColorGroups;
	CostumeFxGroup **eaFxGroups;
	CostumeFxGroup *pDismountFXGroup;
	CostumeFxSwapGroup **eaFxSwapGroups;
	CostumeTexGroup **eaTexGroups;
	CostumeScaleGroup **eaScaleGroups;
	CostumeTexWordsGroup **eaTexWordsGroups;
} CostumeEditDoc;

#endif
