#pragma once
GCC_SYSTEM

#include "CostumeCommon.h"
#include "AutoGen/CostumeCommonEnums_h_ast.h"
#include "AutoGen/CostumeCommon_h_ast.h"

#include "Entity.h"
#include "EntitySavedData.h"
#include "nemesis_common.h"
#include "species_common.h"
#include "UIColor.h"
#include "dynFxInfo.h"

typedef struct ItemDefRef ItemDefRef;

typedef struct MersenneTable MersenneTable;
typedef struct BasicTexture BasicTexture;

typedef struct CostumeEditLine CostumeEditLine;
typedef struct CostumeSubListRow CostumeSubListRow;
typedef struct CostumeUIScaleGroup CostumeUIScaleGroup;
typedef struct UICostumeSlot UICostumeSlot;
typedef struct PetCostumeList PetCostumeList;
typedef struct PetCostumeSlotList PetCostumeSlotList;
typedef struct CostumeCreatorCostumeRef CostumeCreatorCostumeRef;
typedef struct UnlockMetaData UnlockMetaData;
typedef struct UnlockedCostumePart UnlockedCostumePart;
typedef struct CostumeSourceList CostumeSourceList;
typedef struct CostumeViewGraphics CostumeViewGraphics;
typedef struct CharacterClass CharacterClass;
typedef struct MicroTransactionUIProduct MicroTransactionUIProduct;
typedef enum CostumeEditLineType CostumeEditLineType;

#define COSTUME_LIST_CACHEDCOSTUMES "CachedCostumes"
#define COSTUME_LIST_DEFAULTCOSTUMES "CachedCostumes"
#define COSTUME_LIST_MINIONCOSTUMES "MinionCostumes"
#define COSTUME_LIST_HISTORY "History"

AUTO_ENUM;
typedef enum CostumeLockCheckState
{
	CostumeLockCheckState_Unchecked, ENAMES(Unchecked)
	CostumeLockCheckState_Partial,   ENAMES(Partial)
	CostumeLockCheckState_Checked,   ENAMES(Checked)
} CostumeLockCheckState;

AUTO_ENUM;
typedef enum CostumeSourceFlag
{
	kCostumeSourceFlag_None					= 0,		EIGNORE
	kCostumeSourceFlag_PersistOnCostumeExit	= 1 << 2,
} CostumeSourceFlag;

AUTO_STRUCT;
typedef struct CostumeBoneValidValues
{
	REF_TO(PCBoneDef) hBone; AST(KEY)
	PCGeometryDef **eaGeos; AST(UNOWNED)
	PCMaterialDef **eaMats; AST(UNOWNED)
	PCTextureDef **eaDetails; AST(UNOWNED)
	PCTextureDef **eaPatterns; AST(UNOWNED)
	PCTextureDef **eaSpeculars; AST(UNOWNED)
	PCTextureDef **eaDiffuses; AST(UNOWNED)
	PCTextureDef **eaMovables; AST(UNOWNED)
} CostumeBoneValidValues;

__forceinline void CostumeBoneValidValues_ResetLists(CostumeBoneValidValues *pValues)
{
	if (pValues)
	{
		eaDestroy(&pValues->eaGeos);
		eaDestroy(&pValues->eaMats);
		eaDestroy(&pValues->eaDetails);
		eaDestroy(&pValues->eaPatterns);
		eaDestroy(&pValues->eaSpeculars);
		eaDestroy(&pValues->eaDiffuses);
		eaDestroy(&pValues->eaMovables);
	}
}

AUTO_STRUCT;
typedef struct CostumeEditState
{
	// The player costume being edited.  NO_AST so it isn't cleaned up on DeInit call.
	union
	{
		PlayerCostume *pConstCostume;			NO_AST
		NOCONST(PlayerCostume) *pCostume;		NO_AST
	};

	union
	{
		PlayerCostume *pConstHoverCostume;		NO_AST
		NOCONST(PlayerCostume) *pHoverCostume;	NO_AST
	};

	NOCONST(PlayerCostume) *pStartCostume;		NO_AST

	NOCONST(PlayerCostume) **eaCachedCostumes;	NO_AST

	PCCostumeStorageType eCostumeStorageType;
	U32 uCostumeEntContainerID;
	S32 iCostumeIndex;

	PCStanceInfo *pStance;						AST(NAME(Stance) UNOWNED)
	PCStanceInfo **eaStances;					AST(UNOWNED)

	PCVoice *pVoice;							AST(NAME(Voice) UNOWNED)
	PCVoice **eaVoices;							AST(UNOWNED)

//	PCMood *pMood;								AST(NAME(Mood) UNOWNED)
	REF_TO(PCMood) hMood;						AST(NAME(Mood) REFDICT(CostumeMood))
	PCMood **eaMoods;							AST(UNOWNED)

	REF_TO(CharacterClass) hClass;				AST(REFDICT(CharacterClass))
	PCBodyScaleInfo **eaBodyScales;				AST(UNOWNED)
	PCBodyScaleInfo **eaBodyScalesInclude;		AST(UNOWNED)
	PCBodyScaleInfo **eaBodyScalesExclude;		AST(UNOWNED)

	char *pcBoneScaleGroup;						NO_AST
	PCScaleInfo **eaBoneScales;					AST(UNOWNED)

	// The current resource cost of the costume, in numeric and string formats
	S32 currentCost;

	// Nemesis info
	ContainerID uNemesisID;
	NemesisState state;
	NemesisMotivation motivation;
	NemesisPersonality personality;
	char *pcNemesisName;						// Note freed on exiting tailor
	char *pcNemesisDescription;					// Note freed on exiting tailor
	const char *pchNemesisPowerSet;				AST(POOL_STRING)
	const char *pchMinionPowerSet;				AST(POOL_STRING)
	const char *pchMinionCostumeSet;			AST(POOL_STRING)
	F32 fNemesisPowerHue;
	F32 fMinionPowerHue;
	Nemesis *pStartNemesis;

	// The skeleton currently being edited, if any.
	REF_TO(PCSkeletonDef) hSkeleton;			AST(NAME(Skeleton) REFDICT(CostumeSkeleton))
	PCSkeletonDef **eaSkeletons;				AST(UNOWNED)
	REF_TO(SpeciesDef) hSpecies;				AST(NAME(Species) REFDICT(Species))

	// The slot types
	const char *pcSlotSet;						AST(UNOWNED)
	S32 iSlotID;
	bool bExtraSlot;
	PCSlotDef *pSlotDef;						AST(UNOWNED)
	PCSlotType **eaSlotTypes;					AST(UNOWNED)
	PCSlotType *pSlotType;						AST(UNOWNED)

	CostumePreset **eaPresets;					AST(UNOWNED)

	// The region currently being edited, if any.
	REF_TO(PCRegion) hRegion;					AST(NAME(Region) REFDICT(CostumeRegion))
	PCRegion **eaRegions;						AST(UNOWNED)

	// The region gives us the categories.
	// The category currently being edited, if any.
	REF_TO(PCCategory) hCategory;				AST(NAME(Category) REFDICT(CostumeCategory))
	PCCategory **eaCategories;					AST(UNOWNED)

	// The category gives us the bones.
	// The bone currently being edited, if any.
	REF_TO(PCBoneDef) hBone;					AST(NAME(Bone) REFDICT(CostumeBone))
	PCBoneDef **eaBones;						AST(UNOWNED)
	PCBoneDef **eaAllBones;						AST(UNOWNED)

	char **eaExcludeBones;						AST(UNOWNED)
	char **eaIncludeBones;						AST(UNOWNED)

	UIColor color;								NO_AST
	UIColor sharedColor0;						NO_AST
	UIColor sharedColor1;						NO_AST
	UIColor sharedColor2;						NO_AST
	UIColor sharedColor3;						NO_AST
	U8 sharedGlowScale[4];						NO_AST

	// Randomizer
	MersenneTable *pRandTable;					NO_AST
	int seedPos;								NO_AST
	int *eaiSeeds;								NO_AST

	// The bone gives us the part.
	// The part currently being edited, if any.
	union
	{
		PCPart *pConstPart;						AST(NAME(Part) UNOWNED)
		NOCONST(PCPart) *pPart;					NO_AST
	};

	PCLayer *pCurrentLayer;						AST(UNOWNED)
	PCLayer *pClothLayer;						AST(UNOWNED)
	PCLayer **eaLayers;							AST(UNOWNED)

	REF_TO(PCGeometryDef) hGeometry;			AST(NAME(Geo) REFDICT(CostumeGeometry))
	PCGeometryDef **eaGeos;						AST(UNOWNED)

	REF_TO(PCMaterialDef) hMaterial;			AST(NAME(Material) REFDICT(CostumeMaterial))
	PCMaterialDef **eaMats;						AST(UNOWNED)

	REF_TO(PCTextureDef) hDetail;				AST(NAME(Detail) REFDICT(CostumeTexture))
	PCTextureDef **eaDetailTex;					AST(UNOWNED)

	REF_TO(PCTextureDef) hPattern;				AST(NAME(Pattern) REFDICT(CostumeTexture))
	PCTextureDef **eaPatternTex;				AST(UNOWNED)

	REF_TO(PCTextureDef) hSpecular;				AST(NAME(Specular) REFDICT(CostumeTexture))
	PCTextureDef **eaSpecularTex;				AST(UNOWNED)

	REF_TO(PCTextureDef) hDiffuse;				AST(NAME(Diffuse) REFDICT(CostumeTexture))
	PCTextureDef **eaDiffuseTex;				AST(UNOWNED)

	REF_TO(PCTextureDef) hMovable;				AST(NAME(Movable) REFDICT(CostumeTexture))
	PCTextureDef **eaMovableTex;				AST(UNOWNED)

	PCStyle **eaStyles;							AST(UNOWNED)
	const char **eaRandomStyles;				AST(UNOWNED)

	CostumeBoneValidValues **eaBoneValidValues;

	CostumeEditLineType	eFindTypes;				AST(FLAGS)
	PCRegionRef **eaFindRegions;
	CostumeUIScaleGroup **eaFindScaleGroup;
	CostumeEditLine **eaCostumeEditLine;
	CostumeEditLine **eaBufferedEditLine;
	//CostumeEditLine **eaCostumeEditLineSave;
	int iBodyScalesRule;						//0 = No show; 1 = Top of list; 2 = Between Pickers and Sliders; 3 = Bottom of List
	const char *pchCostumeSet;					AST(POOL_STRING)

	bool bUpdateLines;							NO_AST
	bool bUpdateLists;							NO_AST
	bool bUpdateUnlockedRefs;					NO_AST

	PlayerCostume **eaCostumes;					AST(UNOWNED)
	PetCostumeList **eaPetCostumeList;
	UICostumeSlot **eaSlots;					AST(UNOWNED)
	PetCostumeSlotList **eaPetSlots;			AST(UNOWNED)

	PlayerCostume **eaUnlockedCostumes;			AST(UNOWNED)
		// The list of all unlocked costumes that players can try out on their costume
	PlayerCostumeRef **eaUnlockedCostumeRefs;
		// The references to all the costumes that players can try out on their costume
	PlayerCostume **eaOwnedUnlockedCostumes;	AST(UNOWNED)
		// The list of all owned costumes, players can only save costumes if their costume only contains unlocked parts in this list
	PlayerCostumeRef **eaOwnedUnlockedCostumeRefs;
		// The references to all the costumes that player current owns (optimization)

	PlayerCostume FlatUnlockedGeos;

	UnlockedCostumePart **eaUnlockedCostumeParts;
		// A special list of unlocked costume parts
	UnlockedCostumePart **eaFilteredUnlockedCostumeParts;	AST(UNOWNED)
		// The unlocked costume parts limited by the filter
	char *pchUnlockedCostumeFilter;
		// The filter for the unlocked costume parts

	bool bUnlockMetaIncomplete;
	StashTable stashGeoUnlockMeta;				NO_AST
	StashTable stashMatUnlockMeta;				NO_AST
	StashTable stashTexUnlockMeta;				NO_AST

	bool bOwnedCostumeValid;

	const char **eaPowerFXBones;				AST(UNOWNED)

	CostumeCreatorCostumeRef **eaDefaultCostumes;	AST(NAME(DefaultCostumes) UNOWNED)

	// The array of locked region categories
	PCRegion **eaLockedRegions;					AST(UNOWNED)

	PCControlledRandomLock eSharedColorLocks;

	PCBoneDef *pSelectedBone;					AST(UNOWNED)
	bool bValidSelectedBone;

	int iAutoEditIndex;							AST(DEFAULT(-1))
		// Which Index to automatically start editing when the tailor UI opens

	ItemDefRef **eaShowItems;					AST(UNOWNED)

	bool bLineListHideMirrorBones;
	bool bTextureLinesForCurrentPartValuesOnly;
	bool bCombineLines;

	bool bAllowSelectFromAllBones;

	bool bUnlockAll;							NO_AST
	bool bTailorReady;							NO_AST
	bool bEnableTailor;							NO_AST
	
	// The costume change is free
	bool bCostumeChangeIsFree;

	bool bCostumeChanged;

	PCFXTemp **eaFXArray;

} CostumeEditState;

AUTO_STRUCT;
typedef struct CostumeCreatorCostumeRef {
	REF_TO(PlayerCostume) hCostume;	AST(NAME(Costume) REFDICT(PlayerCostume))
	REF_TO(WLCostume) hWLCostume; AST(NAME(WLCostume) REFDICT(Costume))
} CostumeCreatorCostumeRef;

AUTO_STRUCT;
typedef struct CostumeSource
{
	// Tag name. NB: Not pooled
	char *pcTagName;							AST(NAME(Tag))

	// Copy of the costume, if it's not a handle
	PlayerCostume *pCostume;					AST(NAME(Costume))

	// Handle to a PlayerCostume
	REF_TO(PlayerCostume) hPlayerCostume;		AST(NAME(PlayerCostume) REFDICT(PlayerCostume))

	// Inputs for this costume
	const char **eapchCostumeInputs;			AST(NAME(CostumeInput) POOL_STRING)

	// The product associated with this costume source
	MicroTransactionUIProduct *pProduct;
	U32 uiProductID;							AST(NAME(ProductID))

	// The costume set associated with this costume source
	PCCostumeSet *pCostumeSet;					AST(UNOWNED)
	REF_TO(PCCostumeSet) hCostumeSetRef;		AST(NAME(CostumeSetRef))

	// Display name associated with this source (from pProduct or pCostumeSet)
	const char *pchDisplayName;					AST(UNOWNED)

	// Description associated with this source (from pProduct or pCostumeSet)
	const char *pchDescription;					AST(UNOWNED)

	// Icon associated with this source (from pProduct or pCostumeSet)
	const char *pchIcon;						AST(UNOWNED)
} CostumeSource;

AUTO_STRUCT;
typedef struct CostumeSourceList
{
	// Name of this costume list
	const char *pcName;					AST(KEY POOL_STRING)

	// The list of source costumes
	CostumeSource **eaCostumes;

	// Options for this list
	CostumeSourceFlag eFlags;			AST(FLAGS)

	// The maximum size this list may be
	S32 iMaxSize;
} CostumeSourceList;

AUTO_STRUCT;
typedef struct UICostumeSlot
{
	// The entity this slot belongs to
	Entity *pEntity;					AST(UNOWNED)

	// The key information for storing to this costume slot
	PCCostumeStorageType eStorageType;
	U32 uContainerID;
	S32 iIndex;

	// Information about this slot
	bool bIsHeader;
	S32 iSlotID;
	PlayerCostume *pCostume;			AST(UNOWNED)
	PCSlotDef *pSlotDef;				AST(UNOWNED)
	PCSlotType *pSlotType;				AST(UNOWNED)
	bool bIsUnlocked;
} UICostumeSlot;

extern CostumeEditState g_CostumeEditState;
extern CostumeViewGraphics *g_pCostumeView;

extern NOCONST(PlayerCostume) *CharacterCreation_MakePlainCostumeFromSkeleton(PCSkeletonDef *pSkel, SpeciesDef *pSpecies);
extern void CharacterCreation_BuildPlainCostume(void);
extern void CostumeUI_ClearSelections(void);

// Costume edit list API
extern CostumeSourceList *CostumeEditList_GetSourceList(const char *pcName, bool bCreate);
extern CostumeSource *CostumeEditList_GetCostume(const char *pcSourceList, const char *pcCostumeName);
extern CostumeSource *CostumeEditList_GetCostumeByIndex(const char *pcSourceList, S32 iIndex);
extern CostumeSource *CostumeEditList_GetCostumeBySpeciesAndSkeleton(const char *pcSourceList, SpeciesDef *pSpecies, PCSkeletonDef *pSkeleton);
extern CostumeSource *CostumeEditList_AddCostume(const char *pcSourceList, PlayerCostume *pCostume);
extern CostumeSource *CostumeEditList_AddCostumeRef(const char *pcSourceList, const char *pcCostumeName);
extern CostumeSource *CostumeEditList_AddNamedCostume(const char *pcSourceList, const char *pcTagName, PlayerCostume *pCostume);
extern CostumeSource *CostumeEditList_AddNamedCostumeRef(const char *pcSourceList, const char *pcTagName, const char *pcCostumeName);
extern void CostumeEditList_ClearCostumeSourceList(const char *pcSourceList, bool bForce);

// Costume edit list functions to provide backward compatibility
extern void gclCostumeEditListExpr_LoadCostume(const char *pcList, const char *pcTag, const char *pcCostume);
extern bool gclCostumeEditListExpr_PushCostume(const char *pcList, const char *pcTag);
extern bool gclCostumeEditListExpr_SelectCostume(const char *pcList, int iIndex);
extern bool gclCostumeEditListExpr_SelectNamedCostume(const char *pcList, const char *pcTag);
extern bool gclCostumeEditListExpr_CacheCostume(const char *pcList);
extern void gclCostumeEditListExpr_ChooseCostume(const char *pcList);
extern void gclCostumeEditListExpr_ChooseCostumeFromSelections(const char *pcList);
extern void gclCostumeEditListExpr_LoadCostumeList(const char *pcList, const char *pchCostumeNames);
extern S32 gclCostumeEditListExpr_ListSize(const char *pcList);
extern bool gclCostumeEditListExpr_ClearAfter(const char *pcList, S32 iIndex);
extern bool gclCostumeEditListExpr_PreviewCostume(const char *pcList, S32 iIndex);
extern bool gclCostumeEditListExpr_PreviewNamedCostume(const char *pcList, const char *pcTag);

// Call with bUpdateReferences set to false if you did not do anything
// that requires changing part information (e.g. muscle or bone scale).
extern void CostumeUI_RegenCostumeEx(bool bUpdateReferences, bool bValidateSafeMode);
#define CostumeUI_RegenCostume(bUpdateReferences) CostumeUI_RegenCostumeEx((bUpdateReferences), false)

extern void CostumeUI_costumeView_RegenCostume(CostumeViewGraphics *pGraphics, PlayerCostume *pCostume, const PCSlotType *pSlotType, PCMood *pMood, CharacterClass* pClass, ItemDefRef **eaShowItems);

// Magical utility debugging for Costume UI code, should only apply to "API" that causes
// state changes. Internal functions should be excluded.
typedef struct CostumeUITrace {
	const char *pchFunction;
	U32 uiTimestamp;
	bool bUpdateLists;
	bool bUpdateLines;
} CostumeUITrace;

extern CostumeUITrace g_CostumeUITraceState[];
extern S32 g_iNextCostumeTraceState;
extern U32 g_uiLastCostumeRegenTime;
extern U32 g_uiLastCostumeDictTime;
extern U32 g_uiLastCostumeLineTime;
extern bool g_bHideUnownedCostumes;
#define MAX_COSTUME_UI_TRACE_STATE 1000
#define COSTUME_UI_TRACE_FUNC()	{	\
		S32 iSet = g_iNextCostumeTraceState; \
		g_iNextCostumeTraceState = (g_iNextCostumeTraceState + 1) % MAX_COSTUME_UI_TRACE_STATE;	\
		g_CostumeUITraceState[iSet].pchFunction = __FUNCTION__;	\
		g_CostumeUITraceState[iSet].uiTimestamp = gGCLState.totalElapsedTimeMs;	\
		g_CostumeUITraceState[iSet].bUpdateLists = g_CostumeEditState.bUpdateLists;	\
		g_CostumeUITraceState[iSet].bUpdateLines = g_CostumeEditState.bUpdateLines;	\
	}
