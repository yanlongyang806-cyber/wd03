//
// CostumeDefEditor.h
//

#pragma once
GCC_SYSTEM

#ifndef NO_EDITORS

#include "EditorManager.h"
#include "CostumeCommon.h"
#include "MultiEditField.h"

typedef struct CostumeEditDoc CostumeEditDoc;
typedef struct CostumeEditDefDoc CostumeEditDefDoc;
typedef struct EMEditor EMEditor;
typedef struct UIButton UIButton;
typedef struct UIExpander UIExpander;
typedef struct UIFilteredList UIFilteredList;
typedef struct UIGimmeButton UIGimmeButton;
typedef struct UILabel UILabel;
typedef struct UIMenu UIMenu;
typedef struct UISeparator UISeparator;
typedef struct UISprite UISprite;
typedef struct UIWindow UIWindow;

typedef enum CostumeSaveType {
	SaveType_None = 0,
	SaveType_GeoDef,
	SaveType_GeoAdd,
	SaveType_MatDef,
	SaveType_MatAdd,
	SaveType_TexDef,
	SaveType_NumSaveTypes,
} CostumeSaveType;

typedef struct CostumeAddSaveData {
	CostumeEditDefDoc *pDefDoc;
	char *pcName;         // Def or DefAdd name
	char *pcFileName;     // Def or DefAdd file name
	CostumeSaveType eSaveType;
} CostumeAddSaveData;

typedef struct CostumeExtraTextureGroup {
	CostumeEditDefDoc *pDefDoc;
	int index;
	MEField *pOldTexField;
	MEField *pNewTexField;
	MEField *pTypeField;
	MEField *pTexWordsKeyField;
	MEField *pTexWordsCapsField;

	UIButton *pRemoveButton;
	UISprite *pSprite;

	UILabel *pOldLabel;
	UILabel *pNewLabel;
	UILabel *pTypeLabel;
	UILabel *pTexWordsKeyLabel;
	UILabel *pTexWordsCapsLabel;

	UISeparator *pSeparator;
} CostumeExtraTextureGroup;

typedef struct CostumeDefFxGroup {
	CostumeEditDefDoc* pDefDoc;
	MEField *pFxField;
	MEField *pHueField;
	UIButton *pRemoveButton;
	UISeparator *pSeparator;
} CostumeDefFxGroup;

typedef struct CostumeDefFxSwapGroup {
	CostumeEditDefDoc* pDefDoc;
	MEField *pFxOldField;
	MEField *pFxNewField;
	UIButton *pRemoveButton;
	UISeparator *pSeparator;
} CostumeDefFxSwapGroup;


typedef struct CostumeMatConstantGroup {
	CostumeEditDefDoc* pDoc;

	const char *pcName;
	bool bIsSet;
	bool bIsColor;
	Vec4 currentValue;

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
} CostumeMatConstantGroup;

typedef struct CostumeGeoChildGeoGroup {
	CostumeEditDefDoc* pDefDoc;
	
	const char* pcGeoName;
	int index;
	bool bIsDefault;
	
	MEField* pGeoField;
	UIButton* pRemoveButton;
	UILabel* pIndexLabel;
} CostumeGeoChildGeoGroup;

typedef struct CostumeGeoChildBoneGroup {
	CostumeEditDefDoc* pDefDoc;

	const char* pcBoneName;
	int index;

	UILabel* pGeoTitleLabel;
	UILabel* pGeoChildRequiredLabel;
	UILabel* pGeoChildDefaultLabel;
	UILabel* pGeoChildEmptyLabel;
	UIButton* pGeoChildAddButton;

	MEField *pDefGeoChildRequiredField;
	MEField *pDefGeoChildDefaultField;

	char **eaGeoChildNames;

	CostumeGeoChildGeoGroup **eaChildGroups;
} CostumeGeoChildBoneGroup;

typedef struct DeleteInfoStruct {
	void *pDef;
	char *pcName;
	DictionaryHandle hDict;
} DeleteInfoStruct;

#endif

// Need to have AUTO_STRUCTs defined even if NO_EDITORS

AUTO_STRUCT;
typedef struct CostumeEditDefaultsStruct {
	bool bAllowGlow0;
	bool bAllowGlow1;
	bool bAllowGlow2;
	bool bAllowGlow3;

	bool bAllowReflect0;
	bool bAllowReflect1;
	bool bAllowReflect2;
	bool bAllowReflect3;
	F32 fDefaultReflect0;
	F32 fDefaultReflect1;
	F32 fDefaultReflect2;
	F32 fDefaultReflect3;

	bool bAllowSpecular0;
	bool bAllowSpecular1;
	bool bAllowSpecular2;
	bool bAllowSpecular3;
	F32 fDefaultSpecular0;
	F32 fDefaultSpecular1;
	F32 fDefaultSpecular2;
	F32 fDefaultSpecular3;

	bool bCustomMuscle0;
	bool bCustomMuscle1;
	bool bCustomMuscle2;
	bool bCustomMuscle3;
} CostumeEditDefaultsStruct;

#ifndef NO_EDITORS

typedef struct CostumeEditDefDoc {
	// NOTE: This must be first for EDITOR MANAGER
	EMEditorSubDoc emSubDoc;

	EMEditor *pEditor;

	// Windows
	UIWindow *pGeoEditWin;
	UIWindow *pMatEditWin;
	UIWindow *pTexEditWin;
	UIWindow *pPopupWindow;

	// State flags
	bool bIgnoreDefFieldChanges;
	char *pcSkeleton;

	// Save and Close State Flags
	bool bGeoDefSaved;
	bool bMatDefSaved;
	bool bTexDefSaved;
	bool bPopupCloseMode;
	bool bForceClose;
	bool bPromptSaveAsNew;

	// Def UI Controls
	UIExpanderGroup *pTextureExpanderGroup;
	UIComboBox *pDefBoneCombo;
	UIComboBox *pDefGeoCombo;
	UIComboBox *pDefMatCombo;
	UIList *pDefGeoList;
	UIList *pDefMatList;
	UIList *pDefTexList;
	UIButton *pGeoNewButton;
	UIButton *pGeoRevertButton;
	UIButton *pGeoSaveButton;
	UIList *pDefCatList;
	UIList *pDupForBonesList;
	UIButton *pMatNewButton;
	UIButton *pMatRevertButton;
	UIButton *pMatSaveButton;
	UIExpander *pReflectExpander;
	UILabel *pReflectLabel;
	UIExpander *pSpecularExpander;
	UILabel *pSpecularLabel;
	UIButton *pTexNewButton;
	UIButton *pTexRevertButton;
	UIButton *pTexSaveButton;
	UISprite *pDefTexSprite;
	UIExpander *pMovableConstantsExpander;
	UIExpander *pExtraTexturesExpander;
	UILabel *pExtraTexturesLabel;
	UIButton *pExtraTexturesAddButton;
	UIFilteredList *pAddList;
	UIGimmeButton *pGeoGimmeButton;
	UIGimmeButton *pMatGimmeButton;
	UIGimmeButton *pTexGimmeButton;
	UILabel *pDefGeoFileNameLabel;
	UILabel *pDefMatFileNameLabel;
	UILabel *pDefTexFileNameLabel;
	UIExpander* pMatConstantExpander;
	UILabel* pMatConstantLabel;
	UIExpander* pGeoAdvancedExpander;
	UILabel* pGeoAdvAnimatedLabel;
	UILabel* pGeoAdvClothGroupLabel;
	UIExpander* pGeoOptionsExpander;
	UILabel* pGeoOptionsColor0Label;
	UILabel* pGeoOptionsColor1Label;
	UILabel* pGeoOptionsColor2Label;
	UILabel* pGeoOptionsColor3Label;
	UILabel* pGeoOptionsColorQuadLabel;
	UILabel* pGeoIsClothLabel;
	UILabel *pGeoClothBackLabel;
	UILabel *pGeoClothInfoLabel;
	UILabel *pGeoClothCollisionLabel;
	UILabel* pGeoSubSkeletonLabel;
	UILabel* pGeoSubBoneLabel;
	UIExpander* pGeoFxExpander;
	UIExpander* pGeoFxSwapExpander;
	UILabel* pGeoFxSwapEmptyLabel;
	UIButton* pGeoFxAddButton;
	UIButton* pGeoFxSwapAddButton;
	UIExpander* pMatFxSwapExpander;
	UILabel* pGeoFxEmptyLabel;
	UILabel* pMatFxSwapEmptyLabel;
	UIButton* pMatFxAddButton;
	UILabel* pGeoGeometryTextLabel;
	UILabel* pGeoModelTextLabel;
	UILabel* pMatGeometryTextLabel;
	UILabel* pMatMaterialTextLabel;
	UILabel* pMatDefPatternTextLabel;
	UILabel* pMatDefDetailTextLabel;
	UILabel* pMatDefSpecularTextLabel;
	UILabel* pMatDefDiffuseTextLabel;
	UILabel* pMatDefMovableTextLabel;
	UILabel* pMatReqPatternTextLabel;
	UILabel* pMatReqDetailTextLabel;
	UILabel* pMatReqSpecularTextLabel;
	UILabel* pMatReqDiffuseTextLabel;
	UILabel* pMatReqMovableTextLabel;
	UILabel* pTexMaterialTextLabel;

	// Menus
	UIMenu *pGeoContextMenu;
	UIMenu *pMatContextMenu;
	UIMenu *pTexContextMenu;
	int iGeoContextRow;
	int iMatContextRow;
	int iTexContextRow;

	// Def Fields
	MEField *pDefGeoNameField;
	MEField *pDefGeoDispNameField;
	MEField *pDefGeoScopeField;
	MEField *pDefGeoGeometryField;
	MEField *pDefGeoModelField;
	MEField *pDefGeoMirrorField;
	MEField *pDefGeoMatField;
	MEField *pDefGeoRandomWeightField;
	MEField *pDefGeoOrderField;
	MEField *pDefGeoStyleField;
	MEField *pDefGeoCostumeGroupsField;
	MEField *pDefGeoColorRestrictionField;
	MEField *pDefColorSet0Field;
	MEField *pDefColorSet1Field;
	MEField *pDefColorSet2Field;
	MEField *pDefColorSet3Field;
	MEField *pDefColorQuadField;
	MEField *pDefGeoLODField;
	MEField *pDefGeoRestrictionField;
	MEField *pDefGeoSubSkeletonField;
	MEField *pDefGeoSubBoneField;
	MEField *pDefGeoIsClothField;
	MEField *pDefGeoClothBackField;
	MEField *pDefGeoClothInfoField;
	MEField *pDefGeoClothCollisionField;
	MEField *pDefMatNameField;
	MEField *pDefMatDispNameField;
	MEField *pDefMatScopeField;
	MEField *pDefMatMaterialField;
	MEField *pDefMatSkinField;
	MEField *pAllowGlow0Field;
	MEField *pAllowGlow1Field;
	MEField *pAllowGlow2Field;
	MEField *pAllowGlow3Field;
	MEField *pCustomizeReflectField;
	MEField *pAllowReflect0Field;
	MEField *pAllowReflect1Field;
	MEField *pAllowReflect2Field;
	MEField *pAllowReflect3Field;
	MEField *pDefaultReflect0Field;
	MEField *pDefaultReflect1Field;
	MEField *pDefaultReflect2Field;
	MEField *pDefaultReflect3Field;
	MEField *pCustomizeSpecularField;
	MEField *pAllowSpecular0Field;
	MEField *pAllowSpecular1Field;
	MEField *pAllowSpecular2Field;
	MEField *pAllowSpecular3Field;
	MEField *pDefaultSpecular0Field;
	MEField *pDefaultSpecular1Field;
	MEField *pDefaultSpecular2Field;
	MEField *pDefaultSpecular3Field;
	MEField *pCustomMuscle0Field;
	MEField *pCustomMuscle1Field;
	MEField *pCustomMuscle2Field;
	MEField *pCustomMuscle3Field;
	MEField *pDefMatRandomWeightField;
	MEField *pDefMatOrderField;
	MEField *pDefMatColorRestrictionField;
	MEField *pDefMatRestrictionField;
	MEField *pDefMatPatternField;
	MEField *pDefMatDetailField;
	MEField *pDefMatSpecularField;
	MEField *pDefMatDiffuseField;
	MEField *pDefMatMovableField;
	MEField *pDefMatReqPatternField;
	MEField *pDefMatReqDetailField;
	MEField *pDefMatReqSpecularField;
	MEField *pDefMatReqDiffuseField;
	MEField *pDefMatReqMovableField;
	MEField *pDefMatCostumeGroupsField;
	MEField *pDefTexNameField;
	MEField *pDefTexDispNameField;
	MEField *pDefTexScopeField;
	MEField *pDefTexOldField;
	MEField *pDefTexNewField;
	MEField *pDefTexSkinField;
	MEField *pDefTexTypeField;
	MEField *pDefTexMovMinXField;
	MEField *pDefTexMovMaxXField;
	MEField *pDefTexMovDefaultXField;
	MEField *pDefTexMovMinYField;
	MEField *pDefTexMovMaxYField;
	MEField *pDefTexMovDefaultYField;
	MEField *pDefTexMovMinScaleXField;
	MEField *pDefTexMovMaxScaleXField;
	MEField *pDefTexMovDefaultScaleXField;
	MEField *pDefTexMovMinScaleYField;
	MEField *pDefTexMovMaxScaleYField;
	MEField *pDefTexMovDefaultScaleYField;
	MEField *pDefTexMovDefaultRotField;
	MEField *pDefTexMovCanEditPosField;
	MEField *pDefTexMovCanEditRotField;
	MEField *pDefTexMovCanEditScaleField;
	MEField *pDefTexWordsKeyField;
	MEField *pDefTexWordsCapsField;
	MEField *pDefMaterialConstantName;
	MEField *pDefMaterialConstantIndex;
	MEField *pDefMaterialConstantDefault;
	MEField *pDefMaterialConstantMin;
	MEField *pDefMaterialConstantMax;
	MEField *pDefTexRandomWeightField;
	MEField *pDefTexOrderField;
	MEField *pDefTexColorRestrictionField;
	MEField *pDefTexColorSwap0Field;
	MEField *pDefTexColorSwap1Field;
	MEField *pDefTexColorSwap2Field;
	MEField *pDefTexColorSwap3Field;
	MEField *pDefTexRestrictionField;
	MEField *pDefTexCostumeGroupsField;

	// UI Temp data for definition editors
	PCBoneDef *pCurrentBoneDef;
	PCGeometryDef *pCurrentGeoDef;
	PCGeometryDef *pOrigGeoDef;
	PCMaterialDef *pCurrentMatDef;
	PCMaterialDef *pOrigMatDef;
	PCTextureDef *pCurrentTexDef;
	PCTextureDef *pOrigTexDef;
	PCBoneDef **eaDefBones;
	PCGeometryDef **eaDefGeos;
	PCGeometryDef **eaDefMirrorGeos;
	PCCategory **eaCategories;
	PCMaterialDef **eaDefMats;
	PCTextureDef **eaDefTexs;
	PCTextureDef **eaDefPatternTex;
	PCTextureDef **eaDefDetailTex;
	PCTextureDef **eaDefSpecularTex;
	PCTextureDef **eaDefDiffuseTex;
	PCTextureDef **eaDefMovableTex;
	const char **eaModelNames;
	char **eaOldTexNames;
	CostumeExtraTextureGroup **eaTexGroups;
	CostumeMatConstantGroup **eaMatConstantGroups;
	CostumeGeoChildBoneGroup **eaGeoChildBoneGroups;
	CostumeDefFxGroup** eaGeoFxGroups;
	CostumeDefFxSwapGroup** eaGeoFxSwapGroups;
	CostumeDefFxSwapGroup** eaMatFxSwapGroups;
	CostumeEditDefaultsStruct origDefaults;
	CostumeEditDefaultsStruct currentDefaults;
	char *pcLastGeoChangeName;
	char *pcLastMatChangeName;
	char *pcLastTexChangeName;

	// UI Save state machine data
	bool saveRequested[SaveType_NumSaveTypes];
	CostumeSaveType eSavingPromptWindowType;
	char *pcSaveName;
	bool bSaveAsNew;
	bool bSaveOverwrite;
	bool bSaveRename;
	char *pcBoneForGeosToAdd;
	char **eaGeosToAdd;
	char *pcGeoForMatsToAdd;
	char **eaMatsToAdd;
	char *pcMatForTexsToAdd;
	char **eaTexsToAdd;
	CostumeAddSaveData **eaPromptAddData;
	UIFileNameEntry *pAddSaveFileEntry;
	PCGeometryDef *pGeoDefToSave;
	PCGeometryDef *pGeoDefToSaveOrig;
	PCMaterialDef *pMatDefToSave;
	PCMaterialDef *pMatDefToSaveOrig;
	PCTextureDef *pTexDefToSave;
	PCTextureDef *pTexDefToSaveOrig;
	PCGeometryAdd *pGeoAddToSave;
	PCMaterialAdd *pMatAddToSave;
} CostumeEditDefDoc;


void costumeDefEdit_DefRefresh(CostumeEditDefDoc *pDefDoc);
void costumeDefEdit_DefSetGeo(CostumeEditDefDoc *pDefDoc, PCGeometryDef *pGeo);
void costumeDefEdit_DefSetMat(CostumeEditDefDoc *pDefDoc, PCMaterialDef *pMat);
void costumeDefEdit_DefSetTex(CostumeEditDefDoc *pDefDoc, PCTextureDef *pTex);
void costumeDefEdit_DefUpdateLists(CostumeEditDefDoc *pDefDoc);
void costumeDefEdit_DefUpdateState(CostumeEditDefDoc *pDefDoc);
void costumeDefEdit_InitData(EMEditor *pEditor);
void costumeDefEdit_InitDisplay(CostumeEditDefDoc *pDefDoc);
void costumeDefEdit_SavePrefs(CostumeEditDefDoc *pDefDoc);
void costumeDefEdit_SelectDefBone(CostumeEditDefDoc *pDefDoc, PCBoneDef *pBoneDef);
void costumeDefEdit_SetSkeleton(CostumeEditDefDoc *pDefDoc, PCSkeletonDef *pSkeleton);
void costumeDefEdit_UpdateDisplayNameStatus(CostumeEditDefDoc *pDefDoc);
void costumeDefEdit_UpdateMaterialConstants(CostumeEditDefDoc* pDefDoc);
void costumeDefEdit_UpdateMaterialFxSwaps(CostumeEditDefDoc* pDefDoc);
void costumeDefEdit_UpdateGeoAdvancedOptions(CostumeEditDefDoc* pDefDoc);
void costumeDefEdit_UpdateGeoFxSwaps(CostumeEditDefDoc* pDefDoc);

void costumeDefEdit_RemoveCurrentPartRefs();
EMTaskStatus costumeDefEdit_SaveDef(CostumeEditDoc *pDoc, CostumeEditDefDoc *pDefDoc);

#endif