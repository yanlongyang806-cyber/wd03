#pragma once
GCC_SYSTEM
/***************************************************************************
*     Copyright (c) 2006, Cryptic Studios
*     All Rights Reserved
*     Confidential Property of Cryptic Studios
***************************************************************************/

#if !defined(GENESIS_ALLOW_OLD_HEADERS)
#error Including this file was probably a mistake.  UGC should include the appropriate gslUGC*.h files.
#endif

#ifndef NO_EDITORS

#include "EditorManager.h"
#include "wlGenesis.h"

typedef struct GEMissionLevelDefGroup GEMissionLevelDefGroup;
typedef struct GEVariableDefGroup GEVariableDefGroup;
typedef struct GMDObjectRefGroup GMDObjectRefGroup;
typedef struct GMDObjectTagGroup GMDObjectTagGroup;
typedef struct GenesisLayoutPath GenesisLayoutPath;
typedef struct GenesisLayoutRoom GenesisLayoutRoom;
typedef struct GenesisMapDescription GenesisMapDescription;
typedef struct GenesisMissionChallenge GenesisMissionChallenge;
typedef struct GenesisMissionObjective GenesisMissionObjective;
typedef struct GenesisMissionPortal GenesisMissionPortal;
typedef struct GenesisMissionPrompt GenesisMissionPrompt;
typedef struct GenesisMissionPromptAction GenesisMissionPromptAction;
typedef struct GenesisWhen GenesisWhen;
typedef struct MEField MEField;
typedef struct MapDescEditDoc MapDescEditDoc;
typedef struct SSLibObj SSLibObj;
typedef struct SSTagObj SSTagObj;
typedef struct ShoeboxPoint ShoeboxPoint;
typedef struct ShoeboxPointList ShoeboxPointList;
typedef struct UIButton UIButton;
typedef struct UIComboBox UIComboBox;
typedef struct UIExpander UIExpander;
typedef struct UIExpanderGroup UIExpanderGroup;
typedef struct UIGameActionEditButton UIGameActionEditButton;
typedef struct UIGimmeButton UIGimmeButton;
typedef struct UILabel UILabel;
typedef struct UITextEntry UITextEntry;
typedef struct UIWindow UIWindow;

#define MAPDESC_EDITOR "Map Description Editor"
#define DEFAULT_MAPDESC_NAME     "New_Map_Description"

typedef void (*GMDEmbeddedCallback)(MapDescEditDoc *pDoc, GenesisMapDescription *pMapDesc, bool bSeedLayout, bool bSeedDetail);

typedef struct GMDUndoData 
{
	GenesisMapDescription *pPreMapDesc;
	GenesisMapDescription *pPostMapDesc;
} GMDUndoData;

AUTO_STRUCT;
typedef struct GMDRoomLayoutPair
{
	char* layout;				AST(NAME("Layout"))
	char* room;					AST(NAME("Room"))
} GMDRoomLayoutPair;
extern ParseTable parse_GMDRoomLayoutPair[];
#define TYPE_parse_GMDRoomLayoutPair GMDRoomLayoutPair

typedef struct GMDLayoutDetailKitInfo
{
	MEField *pLayoutDetailSpecField;
	UILabel *pLayoutDetailSpecLabel;
	
	MEField *pLayoutDetailTagsField;
	UILabel *pLayoutDetailTagsLabel;
	UIPane  *pLayoutDetailTagsErrorPane;
	
	MEField *pLayoutDetailNameField;
	UILabel *pLayoutDetailNameLabel;
	
	MEField *pLayoutVaryPerRoomField;
	UILabel *pLayoutVaryPerRoomLabel;
	
	MEField *pLayoutDetailDensityField;
	UILabel *pLayoutDetailDensityLabel;
} GMDLayoutDetailKitInfo;

typedef struct GMDRoomDetailKitInfo
{
	MEField *pDetailSpecField;
	MEField *pDetailTagsField;
	MEField *pDetailNameField;
	MEField *pDetailCustomDensityField;
	MEField *pDetailDensityField;

	UILabel *pDetailSpecLabel;
	UILabel *pDetailTagsLabel;
	UIPane  *pDetailTagsErrorPane;
	UILabel *pDetailNameLabel;
	UILabel *pDetailCustomDensityLabel;
	UILabel *pDetailDensityLabel;
} GMDRoomDetailKitInfo;

typedef struct GMDLayoutInfoGroup
{
	UIExpander *pExpander;

	// Exterior Stuff
	GMDLayoutDetailKitInfo ExtDetailKit1;
	GMDLayoutDetailKitInfo ExtDetailKit2;

	MEField *pLayoutExtNameField;
	MEField *pLayoutExtTemplateNameField;
	MEField *pLayoutSideTrailMinField;
	MEField *pLayoutSideTrailMaxField;
	MEField *pLayoutGeoTypeSpecField;
	MEField *pLayoutGeoTypeTagsField;
	MEField *pLayoutGeoTypeNameField;
	MEField *pLayoutEcosystemSpecField;
	MEField *pLayoutEcosystemTagsField;
	MEField *pLayoutEcosystemNameField;
	MEField *pLayoutPlayAreaMinXField;
	MEField *pLayoutPlayAreaMinZField;
	MEField *pLayoutPlayAreaMaxXField;
	MEField *pLayoutPlayAreaMaxZField;
	MEField *pLayoutBufferField;
	MEField *pLayoutColorShiftField;
	MEField *pLayoutExtVertDirField;
	MEField *pLayoutExtShapeField;
	MEField *pLayoutMaxRoadAngleField;
	MEField *pLayoutExtDetailNoSharingField;

	UILabel *pLayoutExtTemplateNameLabel;
	UILabel *pLayoutSideTrailLengthLabel;
	UILabel *pLayoutSideTrailLengthLabel2;
	UILabel *pLayoutSideTrailMinLabel;
	UILabel *pLayoutSideTrailMaxLabel;
	UILabel *pLayoutGeoTypeSpecLabel;
	UILabel *pLayoutGeoTypeTagsLabel;
	UIPane  *pLayoutGeoTypeTagsErrorPane;
	UILabel *pLayoutGeoTypeNameLabel;
	UILabel *pLayoutEcosystemSpecLabel;
	UILabel *pLayoutEcosystemTagsLabel;
	UIPane  *pLayoutEcosystemTagsErrorPane;
	UILabel *pLayoutEcosystemNameLabel;
	UILabel *pLayoutPlayAreaMinXLabel;
	UILabel *pLayoutPlayAreaMinZLabel;
	UILabel *pLayoutPlayAreaMaxXLabel;
	UILabel *pLayoutPlayAreaMaxZLabel;
	UILabel *pLayoutBufferLabel;
	UILabel *pLayoutColorShiftLabel;
	UILabel *pLayoutExtVertDirLabel;
	UILabel *pLayoutExtShapeLabel;
	UILabel *pLayoutMaxRoadAngleLabel;
	UILabel *pLayoutExtDetailNoSharingLabel;

	// Interior Stuff
	GMDLayoutDetailKitInfo IntDetailKit1;
	GMDLayoutDetailKitInfo IntDetailKit2;

	MEField *pLayoutIntNameField;
	MEField *pLayoutIntTemplateNameField;
	MEField *pLayoutRoomKitSpecField;
	MEField *pLayoutRoomKitTagsField;
	MEField *pLayoutRoomKitNameField;
	MEField *pLayoutLightKitSpecField;
	MEField *pLayoutLightKitTagsField;
	MEField *pLayoutLightKitNameField;
	MEField *pLayoutIntVertDirField;
	MEField *pLayoutIntDetailNoSharingField;

	UILabel *pLayoutIntTemplateNameLabel;
	UILabel *pLayoutRoomKitSpecLabel;
	UILabel *pLayoutRoomKitTagsLabel;
	UIPane  *pLayoutRoomKitTagsErrorPane;
	UILabel *pLayoutRoomKitNameLabel;
	UILabel *pLayoutLightKitSpecLabel;
	UILabel *pLayoutLightKitTagsLabel;
	UIPane  *pLayoutLightKitTagsErrorPane;
	UILabel *pLayoutLightKitNameLabel;
	UILabel *pLayoutIntVertDirLabel;
	UILabel *pLayoutIntDetailNoSharingLabel;

	// Solar System Stuff
	UILabel *pLayoutSolarSystemEnvTagsLabel;
	UIPane *pLayoutSolarSystemEnvTagsErrorPane;
	MEField *pLayoutSolarSystemEnvTagsField;
	MEField *pLayoutSolarSystemNameField;

	// Interior and Exterior Stuff
	MEField *pLayoutIntLayoutInfoSpecifierField;
	MEField *pLayoutExtLayoutInfoSpecifierField;
	MEField *pLayoutEncJitterTypeField;
	MEField *pLayoutEncJitterPosField;
	MEField *pLayoutEncJitterRotField;

	UILabel *pLayoutIntLayoutInfoSpecifierLabel;
	UILabel *pLayoutExtLayoutInfoSpecifierLabel;
	UILabel *pLayoutEncJitterTypeLabel;
	UILabel *pLayoutEncJitterPosLabel;
	UILabel *pLayoutEncJitterRotLabel;

	// Interior, Exterior, and Solar System Stuff
	MEField *pLayoutBackdropSpecField;
	MEField *pLayoutBackdropTagsField;
	MEField *pLayoutBackdropNameField;

	UILabel *pLayoutName;
	UILabel *pLayoutBackdropSpecLabel;
	UILabel *pLayoutBackdropTagsLabel;
	UIPane  *pLayoutBackdropTagsErrorPane;
	UILabel *pLayoutBackdropNameLabel;
} GMDLayoutInfoGroup;


typedef struct GMDLayoutShoeboxGroup
{
	MapDescEditDoc *pDoc;
	UIExpander *pExpander;

	//Detail Objects
	GMDObjectRefGroup **eaDetailRefGroups;
	GMDObjectTagGroup **eaDetailTagGroups;

	UILabel *pDetailObjLabel;
	UIButton *pAddDetailRefButton;
	UIButton *pAddDetailTagButton;

} GMDLayoutShoeboxGroup;


typedef struct GMDRoomGroup
{
	MapDescEditDoc *pDoc;
	int index;
	GenesisLayoutRoom ***peaRooms;

	UIExpander *pExpander;

	UIExpanderGroup *pAdvancedExpanderGroup;
	UIExpander *pAdvancedExpander;

	GMDRoomDetailKitInfo DetailKit1;
	GMDRoomDetailKitInfo DetailKit2;

	UILabel *pNameLabel;
	UILabel *pRoomSpecLabel;
	UILabel *pRoomTagsLabel;
	UIPane  *pRoomTagsErrorPane;
	UILabel *pRoomNameLabel;
	UILabel *pOffMapLabel;
	UIButton *pRemoveButton;
	UIButton *pUpButton;
	UIButton *pDownButton;
	UIMenuButton *pPopupMenuButton;

	MEField *pNameField;
	MEField *pRoomSpecField;
	MEField *pRoomTagsField;
	MEField *pRoomNameField;
	MEField *pOffMapField;

} GMDRoomGroup;

typedef struct GMDPathGroup
{
	MapDescEditDoc *pDoc;
	int index;
	GenesisLayoutPath ***peaPaths;

	UIExpander *pExpander;

	UIExpanderGroup *pAdvancedExpanderGroup;
	UIExpander *pAdvancedExpander;
	
	GMDRoomDetailKitInfo DetailKit1;
	GMDRoomDetailKitInfo DetailKit2;

	UILabel *pNameLabel;
	UILabel *pPathSpecLabel;
	UILabel *pPathTagsLabel;
	UIPane  *pPathTagsErrorPane;
	UILabel *pPathNameLabel;
	UILabel *pLengthLabel;
	UILabel *pMinLengthLabel;
	UILabel *pMaxLengthLabel;
	UILabel *pStartLabel;
	UILabel *pEndLabel;
	UIButton *pRemoveButton;
	UIButton *pUpButton;
	UIButton *pDownButton;
	UIMenuButton *pPopupMenuButton;

	MEField *pNameField;
	MEField *pPathSpecField;
	MEField *pPathTagsField;
	MEField *pPathNameField;
	MEField *pMinLengthField;
	MEField *pMaxLengthField;
	MEField *pStartField;
	MEField *pEndField;
} GMDPathGroup;

typedef struct GMDPointGroup GMDPointGroup;

typedef struct GMDObjectRefGroup
{
	MapDescEditDoc *pDoc;
	UIExpander *pExpander;
	int index;
	SSLibObj ***peaObjects;

	UILabel *pObjectLabel;
	UITextEntry *pObjectEntry;
	UIButton *pRemoveButton;

	UILabel *pMinCountLabel;
	UILabel *pMaxCountLabel;
	UILabel *pMinDistLabel;
	UILabel *pMaxDistLabel;
	UILabel *pMinHorizLabel;
	UILabel *pMaxHorizLabel;
	UILabel *pMinVertLabel;
	UILabel *pMaxVertLabel;
	UILabel *pDetachedLabel;

	MEField *pMinCountField;
	MEField *pMaxCountField;
	MEField *pMinDistField;
	MEField *pMaxDistField;
	MEField *pMinHorizField;
	MEField *pMaxHorizField;
	MEField *pMinVertField;
	MEField *pMaxVertField;
	MEField *pDetachedField;

} GMDObjectRefGroup;

typedef struct GMDObjectTagGroup
{
	MapDescEditDoc *pDoc;
	UIExpander *pExpander;
	int index;
	SSTagObj ***peaTags;

	UILabel *pTagLabel;
	MEField *pTagField;
	UIPane *pTagErrorPane;
	//UITextEntry *pTagEntry;
	UIButton *pRemoveButton;

	UILabel *pMinCountLabel;
	UILabel *pMaxCountLabel;
	UILabel *pMinDistLabel;
	UILabel *pMaxDistLabel;
	UILabel *pMinHorizLabel;
	UILabel *pMaxHorizLabel;
	UILabel *pMinVertLabel;
	UILabel *pMaxVertLabel;
	UILabel *pDetachedLabel;

	MEField *pMinCountField;
	MEField *pMaxCountField;
	MEField *pMinDistField;
	MEField *pMaxDistField;
	MEField *pMinHorizField;
	MEField *pMaxHorizField;
	MEField *pMinVertField;
	MEField *pMaxVertField;
	MEField *pDetachedField;

} GMDObjectTagGroup;

typedef struct GMDPointListGroup
{
	MapDescEditDoc *pDoc;
	int index;
	ShoeboxPointList ***peaPointLists;

	GMDPointGroup **eaPointGroups;
	UIExpander *pExpander;

	GMDObjectRefGroup **eaOrbitRefGroups;
	GMDObjectTagGroup **eaOrbitTagGroups;
	GMDObjectRefGroup **eaCurveRefGroups;
	GMDObjectTagGroup **eaCurveTagGroups;

	UIButton *pAddChildButton;
	UIButton *pRemoveButton;
	UIButton *pUpButton;
	UIButton *pDownButton;

	UILabel *pNameLabel;
	MEField *pNameField;

	UILabel *pStartLabel;
	MEField *pStartField;
	
	UILabel *pEndLabel;
	MEField *pEndField;

	UILabel *pOrbitLabel;
	MEField *pOrbitField;

	UILabel *pDistLabel;
	MEField *pDistField;
	UILabel *pDist2Label;

	UILabel *pEquiLabel;
	MEField *pEquiField;

	UILabel *pFollowLabel;
	MEField *pFollowField;

	UILabel *pListTypeLabel;
	MEField *pListTypeField;

	UILabel *pOrbitObjLabel;

	UILabel *pMinRadiusLabel;
	MEField *pMinRadiusField;

	UILabel *pMaxRadiusLabel;
	MEField *pMaxRadiusField;

	UILabel *pMinTiltLabel;
	MEField *pMinTiltField;

	UILabel *pMaxTiltLabel;
	MEField *pMaxTiltField;

	UILabel *pMinYawLabel;
	MEField *pMinYawField;

	UILabel *pMaxYawLabel;
	MEField *pMaxYawField;

	UILabel *pMinHorizLabel;
	MEField *pMinHorizField;

	UILabel *pMaxHorizLabel;
	MEField *pMaxHorizField;

	UILabel *pMinVertLabel;
	MEField *pMinVertField;

	UILabel *pMaxVertLabel;
	MEField *pMaxVertField;

	UILabel *pCurveObjLabel;
	UIButton *pAddOrbitRefButton;
	UIButton *pAddOrbitTagButton;
	UIButton *pAddCurveRefButton;
	UIButton *pAddCurveTagButton;
	UIMenuButton *pPopupMenuButton;
} GMDPointListGroup;

typedef struct GMDPointGroup
{
	MapDescEditDoc *pDoc;
	int index;
	
	ShoeboxPoint ***peaPoints;
	UIExpander *pExpander;

	// Shared controls
	UIButton *pRemoveButton;
	UIButton *pUpButton;
	UIButton *pDownButton;
	UIMenuButton *pPopupMenuButton;

	UILabel *pTitleLabel;

	UILabel *pNameLabel;
	MEField *pNameField;

	UILabel *pRadiusLabel;
	MEField *pRadiusField;
	UILabel *pRadius2Label;

	UILabel *pMinClusterDistLabel;
	MEField *pMinClusterDistField;

	UILabel *pMaxClusterDistLabel;
	MEField *pMaxClusterDistField;

	UILabel *pDistFromPrevLabel;
	MEField *pDistFromPrevField;
	UILabel *pDist2FromPrevLabel;

	UILabel *pFacingLabel;
	MEField *pFacingField;

	UILabel *pFacingOffsetLabel;
	MEField *pFacingOffsetField;
} GMDPointGroup;

typedef struct GMDShoeboxGroup
{
	UIExpander *pExpander;
} GMDShoeboxGroup;

typedef struct GMDWhenGroup
{
	MapDescEditDoc* pDoc;
	GenesisWhen* pWhen;
	
	UILabel* pTypeLabel;
	MEField* pTypeField;
 
	UILabel *pNamesLabel;
	UITextEntry *pRoomsText;
	MEField *pChallengeNamesField;
	MEField *pObjectiveNamesField;
	MEField *pPromptNamesField;
	MEField *pContactNamesField;
	MEField *pCritterDefNamesField;
	MEField *pItemDefNamesField;

	UILabel *pChallengeNumToCompleteLabel;
	MEField *pChallengeNumToCompleteField;

	UILabel *pCritterGroupNamesLabel;
	MEField *pCritterGroupNamesField;
	UILabel *pCritterNumToKillLabel;
	MEField *pCritterNumToKillField;

	UILabel *pItemCountLabel;
	MEField *pItemCountField;
} GMDWhenGroup;

typedef struct GMDChallengeStartGroup
{
	MapDescEditDoc *pDoc;
	UIExpander *pExpander;

	UILabel *pEntryFromMapLabel;
	UILabel *pEntryFromInteractableLabel;
	UILabel *pStartRoomLabel;
	UILabel *pStartTransitionOverrideLabel;
	UILabel *pHasDoorLabel;

	UISeparator* pStartExitSeparator;
	
	UILabel *pExitTransitionOverrideLabel;
	UILabel *pExitFromLabel;
	UILabel *pExitRoomLabel;
	UISMFView *pExitPromptSMF;
	UILabel *pExitUsePetCostumeLabel;
	UILabel *pExitCostumeLabel;
	UILabel *pExitPetCostumeLabel;

	UISeparator* pExitContinueSeparator;
	
	UILabel *pContinueLabel;
	UILabel *pContinueFromLabel;
	UILabel *pContinueRoomLabel;
	UILabel *pContinueChallengeLabel;
	UILabel *pContinueMapLabel;
	UILabel *pContinueTransitionOverrideLabel;
	UISMFView *pContinuePromptSMF;
	UILabel *pContinuePromptUsePetCostumeLabel;
	UILabel *pContinuePromptCostumeLabel;
	UILabel *pContinuePromptPetCostumeLabel;
	UILabel *pContinuePromptButtonTextLabel;
	UILabel *pContinuePromptCategoryLabel;
	UILabel *pContinuePromptPriorityLabel;
	UILabel *pContinuePromptTitleTextLabel;
	UILabel *pContinuePromptBodyTextLabel;

	MEField *pEntryFromMapField;
	MEField *pEntryFromInteractableField;
	UITextEntry *pStartRoomText;
	MEField *pStartTransitionOverrideField;
	MEField *pHasDoorField;
	MEField *pExitTransitionOverrideField;
	MEField *pExitFromField;
	UITextEntry *pExitRoomText;
	MEField *pExitUsePetCostumeField;
	MEField *pExitCostumeField;
	MEField *pExitPetCostumeField;
	MEField *pContinueField;
	MEField *pContinueFromField;
	UITextEntry *pContinueRoomText;
	MEField *pContinueChallengeField;
	MEField *pContinueMapField;
	MEField *pContinueTransitionOverrideField;
	MEField *pContinuePromptUsePetCostumeField;
	MEField *pContinuePromptCostumeField;
	MEField *pContinuePromptPetCostumeField;
	MEField *pContinuePromptButtonTextField;
	MEField *pContinuePromptCategoryField;
	MEField *pContinuePromptPriorityField;
	MEField *pContinuePromptTitleTextField;
	MEField **eaContinuePromptBodyTextField;
	UIButton **eaContinuePromptBodyTextAddRemoveButtons;
} GMDChallengeStartGroup;

typedef struct GMDChallengeGroup
{
	MapDescEditDoc *pDoc;
	int index;
	GenesisMissionChallenge ***peaChallenges;

	UIExpander *pExpander;

	UILabel *pNameLabel;
	UILabel *pRoomLabel;
	UILabel *pChallengeTypeLabel;
	UILabel *pChallengeSpecLabel;
	UILabel *pChallengeTagsLabel;
	UIPane *pChallengeTagsErrorPane;
	UILabel *pChallengeHeterogenousLabel;
	UILabel *pChallengeNameLabel;
	UILabel *pCountLabel;
	UILabel *pNumSpawnLabel;
	UILabel *pPlacementLabel;
	UILabel *pPlacementPrefabLocationLabel;
	UILabel *pFacingLabel;
	UILabel *pRotationIncrementLabel;
	UILabel *pExcludeDistLabel;
	UILabel *pChallengeRefLabel;
	UIButton *pRemoveButton;
	UIButton *pUpButton;
	UIButton *pDownButton;
	UIButton *pToggleSharedButton;
	UIMenuButton *pPopupMenuButton;

	MEField *pNameField;
	MEField *pRoomField;
	MEField *pChallengeTypeField;
	MEField *pChallengeSpecField;
	MEField *pChallengeTagsField;
	MEField *pChallengeHeterogenousField;
	MEField *pChallengeNameField;
	MEField *pCountField;
	MEField *pNumSpawnField;
	MEField *pPlacementField;
	MEField *pPlacementPrefabLocationField;
	MEField *pFacingField;
	MEField *pRotationIncrementField;
	MEField *pExcludeDistField;
	MEField *pChallengeRefField;

	GMDWhenGroup *pWhenGroup;

	// Encounter UI
	UILabel *pSpacePatrolTypeLabel;
	UILabel *pSpacePatRoomRefLabel;
	UILabel *pSpacePatChallengeRefLabel;
	UILabel *pPatrolTypeLabel;
	UILabel *pPatOtherRoomLabel;
	UILabel *pPatPlacementLabel;
	UILabel *pPatChallengeRefLabel;
	
	MEField *pSpacePatrolTypeField;
	MEField *pSpacePatRoomRefField;
	MEField *pSpacePatChallengeRefField;
	MEField *pPatrolTypeField;
	MEField *pPatOtherRoomField;
	MEField *pPatPlacementField;
	MEField *pPatChallengeRefField;

	// Clickie UI
	UILabel *pClickieInteractionDefLabel;
	UILabel *pClickieInteractTextLabel;
	UILabel *pClickieSuccessTextLabel;
	UILabel *pClickieFailureTextLabel;
	UILabel *pClickieInteractAnimLabel;
	
	MEField *pClickieInteractionDefField;
	MEField *pClickieInteractTextField;
	MEField *pClickieSuccessTextField;
	MEField *pClickieFailureTextField;
	MEField *pClickieInteractAnimField;

	const char** eaPrefabLocations;
} GMDChallengeGroup;

typedef struct GMDPromptGroup GMDPromptGroup;

typedef struct GMDPromptActionGroup
{
	GMDPromptGroup *pGroup;
	int index;
	GenesisMissionPromptAction ***peaActions;
	GenesisMissionPromptAction *pAction;
	GenesisMissionPromptAction *pOrigAction;

	UILabel *pGroupLabel;
	UILabel *pTextLabel;
	UILabel *pNextLabel;
	UILabel *pGrantMissionLabel;
	UILabel *pDismissActionLabel;
	UILabel *pActionsLabel;
	UIGameActionEditButton *pActionButton;
	UIButton *pRemoveButton;

	MEField *pTextField;
	MEField *pGrantMissionField;
	MEField *pDismissActionField;
	MEField *pNextField;

} GMDPromptActionGroup;

typedef struct GMDPromptGroup
{
	MapDescEditDoc *pDoc;
	int index;
	GenesisMissionPrompt ***peaPrompts;
	
	UIMenuButton *pPopupMenuButton;

	UIExpander *pExpander;

	UILabel *pNameLabel;
	MEField *pNameField;

	UILabel *pDialogFlagsLabel;
	MEField *pDialogFlagsField;

	// Costume fields
	UILabel *pCostumeTypeLabel;
	MEField *pCostumeTypeField;
	UILabel *pCostumeSpecifiedLabel;
	MEField *pCostumeSpecifiedField;
	UILabel *pCostumePetLabel;
	MEField *pCostumePetField;
	UILabel *pCostumeCritterGroupTypeLabel;
	MEField *pCostumeCritterGroupTypeField;
	UILabel *pCostumeCritterGroupSpecifiedLabel;
	MEField *pCostumeCritterGroupSpecifiedField;
	UILabel *pCostumeCritterGroupMapVarLabel;
	MEField *pCostumeCritterGroupMapVarField;
	UILabel *pCostumeCritterGroupIDLabel;
	MEField *pCostumeCritterGroupIDField;

	UILabel *pHeadshotStyleLabel;
	MEField *pHeadshotStyleField;
	
	UILabel *pTitleTextLabel;
	MEField *pTitleTextField;
	
	UILabel *pBodyTextLabel;
	MEField **eaBodyTextField;
	UIButton **eaBodyTextAddRemoveButtons;

	UILabel *pPhraseLabel;
	MEField *pPhraseField;

	GMDWhenGroup* pShowWhenGroup;
	
	UILabel *pOptionalLabel;
	MEField *pOptionalField;
	
	UILabel *pOptionalButtonTextLabel;
	MEField *pOptionalButtonTextField;
	
	UILabel *pOptionalCategoryLabel;
	MEField *pOptionalCategoryField;
	
	UILabel *pOptionalPriorityLabel;
	MEField *pOptionalPriorityField;
	
	UILabel *pOptionalAutoExecuteLabel;
	MEField *pOptionalHideOnCompleteField;
	
	UILabel *pOptionalHideOnCompleteLabel;
	MEField *pOptionalHideOnCompletePromptField;
	
	UILabel *pOptionalHideOnCompletePromptLabel;
	MEField *pOptionalAutoExecuteField;
	
	UIButton *pAddActionButton;
	UIButton *pRemoveButton;
	UIButton *pUpButton;
	UIButton *pDownButton;

	GMDPromptActionGroup **eaPromptActions;

} GMDPromptGroup;

typedef struct GMDPortalGroup
{
	MapDescEditDoc *pDoc;
	int index;
	GenesisMissionPortal ***peaPortals;
	
	UIMenuButton *pPopupMenuButton;

	UIExpander *pExpander;

	UILabel *pNameLabel;
	MEField *pNameField;

	UILabel *pTypeLabel;
	MEField *pTypeField;

	UILabel *pUseTypeLabel;
	MEField *pUseTypeField;

	UILabel *pStartRoomLabel;
	MEField *pStartRoomField;
	UITextEntry *pStartRoomText;

	UILabel *pStartDoorLabel;
	MEField *pStartDoorField;

	UILabel *pWarpToStartTextLabel;
	MEField *pWarpToStartTextField;

	UILabel *pEndZmapLabel;
	MEField *pEndZmapField;
	
	UILabel *pEndRoomLabel;
	MEField *pEndRoomField;
	UITextEntry *pEndRoomText;
	
	UILabel *pEndDoorLabel;
	MEField *pEndDoorField;

	UILabel *pWarpToEndTextLabel;
	MEField *pWarpToEndTextField;

	GMDWhenGroup* pWhenGroup;
	GEVariableDefGroup** eaEndVariablesGroup;
	UIButton *pEndVariableAddButton;
	
	UIButton *pAddActionButton;
	UIButton *pRemoveButton;
	UIButton *pUpButton;
	UIButton *pDownButton;
} GMDPortalGroup;

typedef struct GMDMissionInfoGroup
{
	UIExpander *pExpander;

	UILabel *pNameLabel;
	UILabel *pDisplayNameLabel;
	UILabel *pShortTextLabel;
	UILabel *pDescriptionTextLabel;
	UILabel *pSummaryTextLabel;
	UILabel *pCategoryLabel;
	UILabel *pShareableLabel;
	UILabel *pRewardLabel;
	UILabel *pRewardScaleLabel;
	UILabel *pGenerationTypeLabel;
	UILabel *pOpenMissionNameLabel;
	UILabel *pOpenMissionShortTextLabel;
	UILabel *pDropRewardTableLabel;
	UILabel *pDropChallengeNamesLabel;

	MEField *pNameField;
	MEField *pDisplayNameField;
	MEField *pShortTextField;
	MEField *pDescriptionTextField;
	MEField *pSummaryTextField;
	MEField *pCategoryField;
	MEField *pShareableField;
	MEField *pRewardField;
	MEField *pRewardScaleField;
	MEField *pGenerationTypeField;
	MEField *pOpenMissionNameField;
	MEField *pOpenMissionShortTextField;
	MEField *pDropRewardTableField;
	MEField *pDropChallengeNamesField;

	GEMissionLevelDefGroup* pLevelDefGroup;
} GMDMissionInfoGroup;

typedef struct GMDMissionStartGroup
{
	UIExpander *pExpander;

	UILabel *pGrantLabel;
	MEField *pGrantField;
	
	UILabel *pGrantContactOfferLabel;
	MEField *pGrantContactOfferField;
	
	UILabel *pGrantContactInProgressLabel;
	MEField *pGrantContactInProgressField;

	UILabel *pTurnInLabel;
	MEField *pTurnInField;

	UILabel *pTurnInContactCompletedLabel;
	MEField *pTurnInContactCompletedField;

	UILabel *pTurnInContactMissionReturnLabel;
	MEField *pTurnInContactMissionReturnField;

	UILabel *pFailTimeoutSecondsLabel;
	MEField *pFailTimeoutSecondsField;

	UILabel *pCanRepeatLabel;
	MEField *pCanRepeatField;

	UILabel *pRepeatCooldownsLabel;

	UILabel *pRepeatCooldownHoursLabel;
	MEField *pRepeatCooldownHoursField;

	UILabel *pRepeatCooldownHoursFromStartLabel;
	MEField *pRepeatCooldownHoursFromStartField;

	UILabel *pRepeatRepeatCooldownCountLabel;
	MEField *pRepeatRepeatCooldownCountField;

	UILabel *pRepeatCooldownBlockTimeLabel;
	MEField *pRepeatCooldownBlockTimeField;

	UILabel *pRequiresMissionsLabel;
	MEField *pRequiresMissionsField;
} GMDMissionStartGroup;

typedef struct GMDObjectiveGroup GMDObjectiveGroup;

typedef struct GMDObjectiveGroup
{
	MapDescEditDoc *pDoc;
	int index;
	GenesisMissionObjective ***peaObjectives;

	GMDObjectiveGroup **eaSubGroups;
	int iIndent;

	UIExpander *pExpander;
	UILabel *pTitleLabel;

	UILabel *pNameLabel;
	MEField *pNameField;

	UILabel *pOptionalLabel;
	MEField *pOptionalField;
	
	UILabel *pShortTextLabel;
	MEField *pShortTextField;
	
	UILabel *pLongTextLabel;
	MEField *pLongTextField;

	GMDWhenGroup *pWhenGroup;
	
	UILabel *pTimeoutLabel;
	MEField *pTimeoutField;

	UILabel *pShowWaypointsLabel;
	MEField *pShowWaypointsField;
	
	UIButton *pRemoveButton;
	UIButton *pUpButton;
	UIButton *pDownButton;
	UIButton *pAddChildButton;
	UIMenuButton *pPopupMenuButton;

} GMDObjectiveGroup;

typedef struct MapDescEditDoc
{
	EMEditorDoc emDoc;
	bool bEmbeddedMode;

	UIComboBox *pTypeCombo;
	UIComboBox *pLayoutCombo;
	GenesisMapType EditingMapType;
	int iEditingLayoutIdx;
	char *pcEditingLayoutName;

	GenesisMapDescription *pMapDesc;
	GenesisSolSysLayout *pEditingSolSys;
	GenesisInteriorLayout *pEditingInterior;
	GenesisExteriorLayout *pEditingExterior;

	GenesisMapDescription *pOrigMapDesc;
	GenesisSolSysLayout *pOrigEditingSolSys;
	GenesisInteriorLayout *pOrigEditingInterior;
	GenesisExteriorLayout *pOrigEditingExterior;

	GenesisMapDescription *pNextUndoMapDesc;
	int iCurrentMission;

	bool bIgnoreFieldChanges;
	bool bIgnoreFilenameChanges;

	// shared controls
	UIComboBox *pMissionCombo;

	// Standalone main window controls
	UIWindow *pMainWindow;
	UILabel *pFilenameLabel;
	UIGimmeButton *pFileButton;
	UIButton *pAddLayout1Button;
	UIButton *pAddLayout2Button;

	// Embedded mode
	UIButton *pSaveButton;
	UIButton *pCloseButton;
	UIButton *pReseedButton;
	GMDEmbeddedCallback callbackFunc;

	// Expanders
	UIExpanderGroup *pLayoutExpanderGroup;
	UIExpanderGroup *pChallengeExpanderGroup;
	UIExpanderGroup *pObjectiveExpanderGroup;

	// Simple fields
	MEField **eaDocFields;
	
	// Groups
	GMDLayoutInfoGroup *pLayoutInfoGroup;
	GMDLayoutShoeboxGroup *pLayoutShoeboxGroup;
	GMDPointListGroup **eaPointListGroups;
	GMDRoomGroup **eaRoomGroups;
	GMDPathGroup **eaPathGroups;
	GMDChallengeStartGroup *pChallengeStartGroup;
	GMDChallengeGroup **eaSharedChallengeGroups;
	GMDChallengeGroup **eaChallengeGroups;
	GMDPromptGroup **eaPromptGroups;
	GMDPortalGroup **eaPortalGroups;
	GMDMissionInfoGroup *pMissionInfoGroup;
	GMDMissionStartGroup *pMissionStartGroup;
	GMDObjectiveGroup **eaObjectiveGroups;

	// Data
	char **eaRoomNames;
	GMDRoomLayoutPair **eaAllRooms;
	char **eaRoomAndOrbitNames;
	char **eaRoomAndPathAndOrbitNames;
	char **eaChallengeLocNames;
	char **eaChallengeNames;
	char **eaPromptNames;
	char **eaObjectiveNames;
	char **eaLayoutNames;
	char **eaMissionNames;
	char **eaShoeboxNames;
} MapDescEditDoc;

MapDescEditDoc *GMDOpenMapDesc(EMEditor *pEditor, char *pcName);
bool GMDEmbeddedMapDescHasChanges();
void GMDSetEmbeddedMapDescEnabled(bool bEnabled);
MapDescEditDoc *GMDOpenEmbeddedMapDesc(GenesisMapDescription *pMapDesc, GMDEmbeddedCallback callbackFunc);
MapDescEditDoc *GMDEmbeddedMapDesc(void);
void GMDCloseEmbeddedMapDesc(void);
void GMDRevertMapDesc(MapDescEditDoc *pDoc);
void GMDCloseMapDesc(MapDescEditDoc *pDoc);
EMTaskStatus GMDSaveMapDesc(MapDescEditDoc* pDoc, bool bSaveAsNew);
void GMDInitData(EMEditor *pEditor);

#endif
