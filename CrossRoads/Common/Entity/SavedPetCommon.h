#ifndef SAVEDPET_COMMON_H
#define SAVEDPET_COMMON_H
GCC_SYSTEM

#include "referencesystem.h"
#include "GlobalTypeEnum.h"
#include "EntEnums.h"
#include "CombatEnums.h"
#include "message.h"
#include "PowersEnums.h"

typedef U32 ContainerID;
typedef U32 EntityRef;
typedef struct AllegianceDef AllegianceDef; 
typedef struct PetRelationship PetRelationship;
typedef struct PuppetEntity PuppetEntity;
typedef struct CritterDef CritterDef;
typedef struct CritterPetRelationship CritterPetRelationship;
typedef struct Entity Entity;
typedef struct GameAccountDataExtract GameAccountDataExtract;
typedef struct ItemChangeReason ItemChangeReason;
typedef struct PetDiag PetDiag;
typedef struct SavedPetCBData SavedPetCBData;
typedef struct TransactionReturnVal TransactionReturnVal;
typedef struct Power Power;
typedef struct PowerDef PowerDef;
typedef struct PowerTree PowerTree;
typedef struct PetDef PetDef;
typedef struct PetDefRefCont PetDefRefCont;
typedef struct PTNodeDef PTNodeDef;
typedef struct NOCONST(AlwaysPropSlot) NOCONST(AlwaysPropSlot);
typedef struct NOCONST(PTNodeDefRefCont) NOCONST(PTNodeDefRefCont);
typedef struct NOCONST(Entity) NOCONST(Entity);
typedef struct NOCONST(Power) NOCONST(Power);
typedef struct NOCONST(PowerTree) NOCONST(PowerTree);
typedef struct NOCONST(PetRelationship) NOCONST(PetRelationship);
typedef struct NOCONST(PuppetEntity) NOCONST(PuppetEntity);
typedef struct NOCONST(GameAccountData) NOCONST(GameAccountData);

typedef void (*EntityUserDataCallback)(int iPartitionIdx, Entity* pEnt, void *userdata);

extern StaticDefineInt PowerPurposeEnum[];

AUTO_ENUM;
typedef enum AddSavedPetErrorType
{
	kAddSavedPetErrorType_None = 0,
	kAddSavedPetErrorType_InvalidAllegiance,
	kAddSavedPetErrorType_UniqueCheck,
	kAddSavedPetErrorType_NotAPuppet,
	kAddSavedPetErrorType_MaxPets,
	kAddSavedPetErrorType_MaxPuppets,
	kAddSavedPetErrorType_AcquireLimit,
} AddSavedPetErrorType;

AUTO_STRUCT;
typedef struct AlwaysPropSlotCategories
{
	const char **ppCategories; AST(NAME(CategoryName))
} AlwaysPropSlotCategories;

extern DefineContext *g_pDefineAlwaysPropSlotCategories;
AUTO_ENUM AEN_EXTEND_WITH_DYNLIST(g_pDefineAlwaysPropSlotCategories);
typedef enum AlwaysPropSlotCategory
{
	kAlwaysPropSlotCategory_Default = 0,
	// Data-defined...
} AlwaysPropSlotCategory;

extern DefineContext *g_pDefineAlwaysPropSlotClassRestrict;
AUTO_ENUM AEN_EXTEND_WITH_DYNLIST(g_pDefineAlwaysPropSlotClassRestrict);
typedef enum AlwaysPropSlotClassRestrictType
{
	kAlwaysPropSlotClassRestrictType_None = 0,
	// Data-defined...
} AlwaysPropSlotClassRestrictType;

extern DefineContext *g_pDefinePetAcquireLimit;
AUTO_ENUM AEN_EXTEND_WITH_DYNLIST(g_pDefinePetAcquireLimit);
typedef enum PetAcquireLimit
{
	kPetAcquireLimit_None =  0,
	// Data-defined...
} PetAcquireLimit;
extern StaticDefineInt PetAcquireLimitEnum[];

AUTO_STRUCT;
typedef struct PetAcquireLimitDef
{
	const char* pchName; AST(STRUCTPARAM)
		// The internal name

	S32 iMaxCount;
		// The maximum number of pets of this type that the player can acquire

	S32 eAcquireLimitType; NO_AST
		// The enum constant associated with this def. Set at load time.
} PetAcquireLimitDef;

AUTO_STRUCT;
typedef struct AlwaysPropSlotClassRestrictDef
{
	const char* pchName; AST(STRUCTPARAM)
		// The internal name

	DisplayMessage msgDisplayName; AST(NAME(DisplayName) STRUCT(parse_DisplayMessage))
		// The display name

	CharClassTypes *peClassTypes; AST(NAME(Type) SUBTABLE(CharClassTypesEnum))
		// The classes that are allowed to be slotted

	AlwaysPropSlotClassRestrictType eClassRestrictType; NO_AST
		// The class restrict type
} AlwaysPropSlotClassRestrictDef;

AUTO_STRUCT;
typedef struct AlwaysPropSlotClassRestrictDefs
{
	AlwaysPropSlotClassRestrictDef** eaRestrictDefs; AST(NAME(ClassRestrictDef))
} AlwaysPropSlotClassRestrictDefs;

AUTO_STRUCT;
typedef struct AlwaysPropSlotDef
{
	char *pchName;						AST(NAME(Name) KEY STRUCTPARAM)
		// The internal name of this slot
	char *pchFileName;					AST(CURRENTFILE)
		// The file name
	DisplayMessage *pDisplayMessage;	AST(NAME(DisplayName))
		// Display message for this prop slot
	AlwaysPropSlotClassRestrictType eClassRestrictType; AST(NAME(ClassRestrictType))
		// The class restriction type
	S32 eAllowPowerCategory;			AST(NAME(AllowPowerCategory) SUBTABLE(PowerCategoriesEnum) DEFAULT(-1))
		// Only propagate powers that have this power category 
	int iMaxPropPowers;					AST(NAME(MaxPropPowers, PowerMax))
		// The maximum number of powers to propagate for this slot
	int iMinPropPowers;					AST(NAME(MinPropPowers))
		// The minimum number of powers that the entity must have in order to be slotted
	PowerPurpose eIgnorePurposeForMax;	AST(NAME(IgnorePurposeForMax) SUBTABLE(PowerPurposeEnum) DEFAULT(kPowerPurpose_None))
		// Exclude powers with this purpose from the max count
	AlwaysPropSlotCategory eCategory;	AST(NAME(Category) SUBTABLE(AlwaysPropSlotCategoryEnum))
		// Each category is processed separately for slotting restrictions
	int iImportanceRank;				AST(NAME(ImportanceRank))
		// The higher the rank, the more 'important' the slot is. Used for ranking pets to choose for PetContactLists.
}AlwaysPropSlotDef;

AUTO_STRUCT;
typedef struct AlwaysPropSlotDefRef
{
	REF_TO(AlwaysPropSlotDef) hDef;
	S32 iCount;
	U32 uiPuppetID;
} AlwaysPropSlotDefRef;

AUTO_STRUCT;
typedef struct AlwaysPropSlotDefRefs
{
	AlwaysPropSlotDefRef** eaRefs;
} AlwaysPropSlotDefRefs;

AUTO_STRUCT AST_CONTAINER;
typedef struct AlwaysPropSlot
{
	const U32 iSlotID;						AST(SUBSCRIBE PERSIST)
	CONST_REF_TO(AlwaysPropSlotDef) hDef;	AST(SUBSCRIBE REFDICT(AlwaysPropSlotDef) PERSIST)
	const U32 iPetID;						AST(SUBSCRIBE PERSIST)
	const U32 iPuppetID;					AST(SUBSCRIBE PERSIST)
}AlwaysPropSlot;

AUTO_STRUCT;
typedef struct PuppetRequestChoice
{
	char *pcAllegiance;					AST(NAME(Allegiance) RESOURCEDICT(Allegiance))
	char *pcCritterDef;					AST(NAME(CritterDef) RESOURCEDICT(CritterDef))
} PuppetRequestChoice;

AUTO_STRUCT;
typedef struct PetRequestChoice
{
	char *pcAllegiance;					AST(NAME(Allegiance) RESOURCEDICT(Allegiance))
	char *pcPetDef;						AST(NAME(PetDef) RESOURCEDICT(PetDef))
} PetRequestChoice;

AUTO_STRUCT;
typedef struct PetIntroductionWarp
{
	bool bPuppet;					AST(NAME(RequirePuppet))
	S32 iRequiredLevel;				AST(NAME(RequiredLevel))
	const char* pchAllegiance;		AST(NAME(RequireAllegiance) POOL_STRING)
	const char* pchMapName;			AST(NAME(MapName) POOL_STRING)
	const char* pchSpawn;			AST(NAME(Spawn) POOL_STRING)
	const char* pchTransSequence;	AST(NAME(TransitionSequence) POOL_STRING)
} PetIntroductionWarp;

//global restrictions on pets
AUTO_STRUCT;
typedef struct PetRestrictions
{
	//Renaming Costs
	S32				iRenameCost;					AST(NAME(RenameCost))
	const char*		pchRenameCostNumeric;			AST(NAME(RenameCostNumeric) POOL_STRING)
	S32				iChangeSubNameCost;				AST(NAME(ChangeSubNameCost))
	const char*		pchChangeSubNameCostNumeric;	AST(NAME(ChangeSubNameCostNumeric) POOL_STRING)

	const char*		pchFreeNameChangeNumeric;		AST(NAME(FreeNameChangeNumeric) POOL_STRING)
	const char*		pchFreeSubNameChangeNumeric;	AST(NAME(FreeSubNameChangeNumeric) POOL_STRING)
	const char*		pchFreeFlexSubNameChangeNumeric;	AST(NAME(FreeFlexSubNameChangeNumeric) POOL_STRING) //For player or pet; stored on the player

	//What headshot style to use for contacts created from "potential pet items"
	const char*		pchPotentialPetHeadshotStyle;	AST(NAME(PotentialPetHeadshotStyle) POOL_STRING)
	
	//Restrictions on how many pets you can own
	S32				iMaxCount;						AST(NAME(MaxCount))
	CharClassTypes* peClassType;					AST(NAME(Type) SUBTABLE(CharClassTypesEnum))

	//Restrictions on how many puppets you can own
	S32				iMaxPuppets;					AST(NAME(MaxPuppets))
	CharClassTypes* pePuppetType;					AST(NAME(PuppetType) SUBTABLE(CharClassTypesEnum))

	//Restrictions on training
	U32				uiMaxSimultaneousTraining;		AST(NAME(MaxSimultaneousTraining))

	//Restrictions on puppet/pet requests
	S32				iRequiredPuppetRequestCount;	AST(NAME(RequiredPuppetRequestCount))
	PuppetRequestChoice** eaAllowedPuppetRequests;	AST(NAME(AllowedPuppetRequestChoice))

	S32				iRequiredPetRequestCount;		AST(NAME(RequiredPetRequestCount))
	PetRequestChoice** eaAllowedPetRequests;		AST(NAME(AllowedPetRequestChoice))

	PetIntroductionWarp** eaPetIntroWarp;			AST(NAME(PetIntroductionWarp))

	PetAcquireLimitDef** eaPetAcquireLimits;		AST(NAME(PetAcquireLimit))

	// Which class categories to exclude from the last puppet stored on the puppet master
	S32* peExcludeLastActivePuppetClassCategories;	AST(NAME(ExcludeLastActivePuppetClassCategory) SUBTABLE(CharClassCategoryEnum))

	// the item that should be used for deceased pets, currently only hooked up for ppAllowedCritterPets
	const char*		pchRequiredItemForDeceasedPets;	AST(NAME(RequiredItemForDeceasedPets))
} PetRestrictions;

AUTO_STRUCT;
typedef struct EntityStruct
{
	Entity *pEntity;
} EntityStruct;

AUTO_STRUCT;
typedef struct PetCreatedInfo
{
	int iPetID;
	int iPetType;
	int iPetIsPuppet;
}PetCreatedInfo;

// Used to display power information for pet container items on the UI
AUTO_STRUCT;
typedef struct SavedPetPowerDisplayData
{
	REF_TO(PTNodeDef) hNodeDef;
	S32 iRank;
	bool bEscrow;
} SavedPetPowerDisplayData;
extern ParseTable parse_SavedPetPowerDisplayData[];
#define TYPE_parse_SavedPetPowerDisplayData SavedPetPowerDisplayData

// Used to cache display data for saved pets which are
// unowned by the current player (i.e. a saved pet in a pending trade)
AUTO_STRUCT;
typedef struct SavedPetDisplayData
{
	ContainerID iPetID;
	SavedPetPowerDisplayData** eaPowerData;
	U32 uLastRequestTime; AST(CLIENT_ONLY)
	bool bUpdateRequested;
} SavedPetDisplayData;
extern ParseTable parse_SavedPetDisplayData[];
#define TYPE_parse_SavedPetDisplayData SavedPetDisplayData

AUTO_STRUCT;
typedef struct PropPowerSaveData
{
	U32 uiPowerID;
	F32 fRecharge;
} PropPowerSaveData;

AUTO_STRUCT;
typedef struct PropPowerSaveList
{
	PropPowerSaveData** eaData;
} PropPowerSaveList;
#define TYPE_parse_PropPowerSaveList PropPowerSaveList

AUTO_STRUCT;
typedef struct PropEntIDs
{
	UINT_EARRAY eauiPropEntIDs;
} PropEntIDs;

void PropEntIDs_FillWithActiveEntIDs(PropEntIDs *pIDs, Entity* pEnt);
void PropEntIDs_Destroy(PropEntIDs *pIDs);

AlwaysPropSlotClassRestrictDef* AlwaysPropSlot_GetClassRestrictDef(AlwaysPropSlotClassRestrictType eType);

NOCONST(PetRelationship) *trhSavedPet_GetPetFromContainerID(ATH_ARG NOCONST(Entity)* pEnt, U32 uiID, bool bPetsOnly);
#define SavedPet_GetPetFromContainerID(pEnt, uiID, bPetsOnly) (PetRelationship*)trhSavedPet_GetPetFromContainerID(CONTAINER_NOCONST(Entity, (pEnt)), uiID, bPetsOnly)
NOCONST(PuppetEntity) *trhSavedPet_GetPuppetFromContainerID(ATH_ARG NOCONST(Entity) *pEnt, ContainerID uiID);
#define SavedPet_GetPuppetFromContainerID(pEnt, uiID) (PuppetEntity*)trhSavedPet_GetPuppetFromContainerID(CONTAINER_NOCONST(Entity, (pEnt)), uiID);
Entity *SavedPet_GetEntityEx(int iPartitionIdx, const PetRelationship *pRelationship, S32 bGetOwner);
#define SavedPet_GetEntity(iPartitionIdx, pRelationship) SavedPet_GetEntityEx(iPartitionIdx, pRelationship, true)
Entity *SavedPuppet_GetEntity(int iPartitionIdx, PuppetEntity *pPuppet);
Entity *SavedCritter_GetEntity(int iPartitionIdx, CritterPetRelationship *pCritterRelationship);
NOCONST(PuppetEntity)* trhSavedPet_GetPuppetFromPet(ATH_ARG NOCONST(Entity) *pMasterEnt, ATH_ARG NOCONST(PetRelationship) *pRelationship);
#define SavedPet_GetPuppetFromPet(pMasterEnt, pRelationship) (PuppetEntity*)trhSavedPet_GetPuppetFromPet(CONTAINER_NOCONST(Entity, (pMasterEnt)), CONTAINER_NOCONST(PetRelationship, (pRelationship)))
bool trhSavedPet_IsPetAPuppet(ATH_ARG NOCONST(Entity) *pMasterEnt, ATH_ARG NOCONST(PetRelationship) *pRelationship);
#define SavedPet_IsPetAPuppet(pMasterEnt, pRelationship) trhSavedPet_IsPetAPuppet(CONTAINER_NOCONST(Entity, (pMasterEnt)), CONTAINER_NOCONST(PetRelationship,(pRelationship)))

void Entity_PuppetCopy_Inventory(ATH_ARG NOCONST(Entity) *pSrc, ATH_ARG NOCONST(Entity) *pDest);
void Entity_PuppetCopy_FixPowerTreeIDs(ATH_ARG NOCONST(Entity) *pEnt, ATH_ARG NOCONST(PowerTree) *pTree);

CritterPetRelationship* Entity_FindSavedCritterByRef(Entity* pOwner, EntityRef erCritter);
CritterPetRelationship* Entity_FindSavedCritterByID(Entity* pOwner, U32 uiCritterPetID);
Entity* Entity_FindSavedCritterEntityByID(Entity* pOwner, U32 uiCritterPetID);
PetDef* Entity_FindAllowedCritterPetDefByID(Entity* pOwner, U32 uiPetID);
PetDefRefCont* Entity_FindAllowedCritterPetByDef(Entity *pOwner, PetDef *pPetDef);

void Entity_GetActivePuppetListByType(SA_PARAM_NN_VALID Entity *pEntity, CharClassTypes eType, PuppetEntity ***peaOut);
Entity *Entity_FindCurrentOrPreferredPuppet(SA_PARAM_NN_VALID Entity *pOwner, CharClassTypes eType);

bool Entity_CanModifyPuppet(Entity* pOwner, Entity* pEntity);

void AlwaysPropSlotLoad(void);
void PetRestrictionsLoad(void);

int CompareAlwaysPropSlot(const AlwaysPropSlot** left, const AlwaysPropSlot** right);

void Entity_getAllPowerTreesFromPuppets(SA_PARAM_NN_VALID Entity *pEnt, PowerTree ***pppPowerTreeOut);
void Entity_GetPetIDList(Entity* pEnt, U32** peaPets);
S32 trhEntity_CountPets(ATH_ARG NOCONST(Entity) *pEntity, bool bCountPets, bool bCountPuppets, bool bCheckClassType);
#define Entity_CountPets(pEntity, bCountPets, bCountPuppets, bCheckClassType) trhEntity_CountPets(CONTAINER_NOCONST(Entity, (pEntity)), bCountPets, bCountPuppets, bCheckClassType)
S32 Entity_CountPetsOfType(SA_PARAM_NN_VALID Entity* pEntity, CharClassTypes eType, bool bCountPuppets);
S32 Entity_CountPetsWithState(SA_PARAM_NN_VALID Entity* pEntity, OwnedContainerState eState, bool bCountPuppets);
bool trhEntity_HasMaxAllowedPuppets(ATH_ARG NOCONST(Entity)* pEnt, GameAccountDataExtract *pExtract);
bool trhEntity_CanAddPuppet(ATH_ARG NOCONST(Entity)* pEnt, const char* pchClass, GameAccountDataExtract *pExtract);
#define Entity_CanAddPuppet(pEnt, pchClass, pExtract) trhEntity_CanAddPuppet(CONTAINER_NOCONST(Entity, (pEnt)), pchClass, pExtract)
bool Entity_HasMaxAllowedPets(SA_PARAM_NN_VALID Entity* pEnt);
bool Entity_CanAddPet(SA_PARAM_NN_VALID Entity* pEnt, const char* pchClass);
bool trhEntity_CheckAcquireLimit(ATH_ARG NOCONST(Entity)* pEnt, ATH_ARG CONST_EARRAY_OF(NOCONST(Entity)) eaPets, PetDef* pPetDef, U64 uSrcItemID);
bool Entity_CheckAcquireLimit(Entity* pEnt, PetDef* pPetDef, U64 uSrcItemID);
bool Entity_CanAddSavedPet(Entity *ent, PetDef *pPetDef, U64 uSrcItemID, bool bAddAsPuppet, GameAccountDataExtract *pExtract, AddSavedPetErrorType* peError);
bool trhEntity_CanAddSavedPet(NOCONST(Entity) *pEnt, CONST_EARRAY_OF(NOCONST(Entity)) eaPets, PetDef *pPetDef, U64 uSrcItemID, bool bAddAsPuppet, GameAccountDataExtract *pExtract);

bool entity_puppetSwapComplete(Entity *pEnt);

S32 trhPet_GetCostToRename(ATR_ARGS, ATH_ARG NOCONST(Entity)* pEnt, ATH_ARG NOCONST(Entity)* pPetEnt, bool bDecrementFreeNameChanges, const ItemChangeReason *pReason, ATH_ARG NOCONST(GameAccountData) *pData);
#define Pet_GetCostToRename(PlayerEnt,PetEnt,Data) trhPet_GetCostToRename(ATR_EMPTY_ARGS, CONTAINER_NOCONST(Entity, PlayerEnt), CONTAINER_NOCONST(Entity, PetEnt), false, NULL, CONTAINER_NOCONST(GameAccountData, (Data)))

S32 trhPet_GetCostToChangeSubName(ATR_ARGS, ATH_ARG NOCONST(Entity)* pEnt, ATH_ARG NOCONST(Entity)* pPetEnt, bool bDecrementFreeNameChanges, const ItemChangeReason *pReason, ATH_ARG NOCONST(GameAccountData) *pData);
#define Pet_GetCostToChangeSubName(PlayerEnt,PetEnt,Data) trhPet_GetCostToChangeSubName(ATR_EMPTY_ARGS, CONTAINER_NOCONST(Entity, PlayerEnt), CONTAINER_NOCONST(Entity, PetEnt), false, NULL, CONTAINER_NOCONST(GameAccountData, (Data)))
S32 trhEnt_GetCostToChangeSubName(ATR_ARGS, ATH_ARG NOCONST(Entity)* pEnt, bool bDecrementFreeNameChanges, const ItemChangeReason *pReason, NOCONST(GameAccountData) *pData);
#define Ent_GetCostToChangeSubName(Ent,Data) trhEnt_GetCostToChangeSubName(ATR_EMPTY_ARGS, CONTAINER_NOCONST(Entity, Ent), false, NULL, CONTAINER_NOCONST(GameAccountData, (Data)))

bool Entity_CanRenamePet(SA_PARAM_NN_VALID Entity* pPlayerEnt, SA_PARAM_NN_VALID Entity* pPetEnt);
bool Entity_CanChangeSubNameOnPet(SA_PARAM_NN_VALID Entity* pPlayerEnt, SA_PARAM_NN_VALID Entity* pPetEnt);
bool Entity_CanChangeSubName(SA_PARAM_NN_VALID Entity* pPlayerEnt);

void Entity_ForEveryPet(int iPartitionIdx, Entity* pEnt, EntityUserDataCallback pCallback, void* pCallbackData, bool bMustBeAlive, bool bRealEntsOnly);

S32 AlwaysPropSlot_trh_FindByPetID(ATH_ARG NOCONST(Entity)* pEntity, U32 uPetID, U32 uPuppetID, AlwaysPropSlotCategory eCategory);
#define AlwaysPropSlot_FindByPetID(pEntity, uPetID, uPuppetID, eCategory) AlwaysPropSlot_trh_FindByPetID(CONTAINER_NOCONST(Entity, (pEntity)), uPetID, uPuppetID, eCategory)
void AlwaysPropSlot_trh_FindAllByPetID(ATH_ARG NOCONST(Entity)* pEntity, U32 uPetID, U32 uPuppetID, S32** piSlots);
#define AlwaysPropSlot_FindAllByPetID(pEntity, uPetID, uPuppetID, piSlots) AlwaysPropSlot_trh_FindAllByPetID(CONTAINER_NOCONST(Entity, (pEntity)), uPetID, uPuppetID, piSlots)
S32 AlwaysPropSlot_trh_FindBySlotID(NOCONST(Entity)* pEntity, U32 uSlotID);
#define AlwaysPropSlot_FindBySlotID(pEntity, uSlotID) AlwaysPropSlot_trh_FindBySlotID(CONTAINER_NOCONST(Entity, (pEntity)), uSlotID)

bool SavedPet_th_AlwaysPropSlotCheckRestrictions(ATH_ARG NOCONST(Entity)* pSavedPet, ATH_ARG NOCONST(PetRelationship)* pRelationShip, AlwaysPropSlotDef* pSlotDef);
#define SavedPet_AlwaysPropSlotCheckRestrictions(pSavedPet, pRelationship, pSlotDef) SavedPet_th_AlwaysPropSlotCheckRestrictions(CONTAINER_NOCONST(Entity, (pSavedPet)), CONTAINER_NOCONST(PetRelationship, (pRelationship)), pSlotDef)

// Walks the Entity's PowerTrees and returns all the available Powers that have the power propagation flag on
void ent_trh_FindAllPropagatedPowersFromPowerTrees(NOCONST(Entity)* pEntity, S32 eAllowPowerCategory, NOCONST(Power) ***pppPowersOut);
#define ent_FindAllPropagatedPowersFromPowerTrees(pEntity, eAllowPowerCategory, pppPowerOut) ent_trh_FindAllPropagatedPowersFromPowerTrees(CONTAINER_NOCONST(Entity, (pEntity)), eAllowPowerCategory, CONTAINER_NOCONST3(Power, (pppPowersOut)))

void ent_trh_FindAllPropagatePowers(ATH_ARG NOCONST(Entity)* pEntity, ATH_ARG NOCONST(PetRelationship)* pSavedPet, AlwaysPropSlotDef* pPropSlotDef, NOCONST(Power)** ppOldPropPowers, NOCONST(Power)*** pppPowersOut);
void ent_FindAllPropagatePowers(Entity *pEntity, PetRelationship *pSavedPet, AlwaysPropSlotDef *pPropSlotDef, Power **ppOldPropPowers, Power ***pppPowersOut, bool bNoAlloc);

Entity* SavedPet_GetEntityFromPetID(Entity* pOwner, U32 uiPetID);

bool Entity_CanSetAsActivePuppet(Entity* pEnt, PuppetEntity* pNewPuppet);
bool Entity_CanSetAsActivePuppetByID(Entity* pEnt, U32 uiPuppetID);

const char *FormalName_GetFirstName(SA_PARAM_OP_VALID Entity *pEnt);
const char *FormalName_GetLastName(SA_PARAM_OP_VALID Entity *pEnt);
const char *FormalName_GetFullName(SA_PARAM_OP_VALID Entity *pEnt);
const char *FormalName_GetFullNameFromSubName(const char *pcSubName);

bool savedpet_ValidateFormalName(Entity *ent, const char *pcFormalName, char **ppEStringError);

bool entity_CanUsePetDef(Entity *ent, PetDef *pPetDef, AddSavedPetErrorType* peError);
bool trhEntity_CanUsePetDef(ATH_ARG NOCONST(Entity) *pEnt, ATH_ARG CONST_EARRAY_OF(NOCONST(Entity)) eaPets, PetDef *pPetDef);

bool canDestroyPuppet(Entity *pEnt, PuppetEntity *pPuppet);

bool SavedPet_PetDiag_FixupEscrowNodes(SA_PARAM_NN_VALID PetDiag* pPetDiag, NOCONST(PTNodeDefRefCont)*** peaEscrowNodes);

bool Entity_CanInitiatePetTransfer(Entity* pSrcEnt, Entity* pPetEnt, char** pestrError);
bool Entity_CanAcceptPetTransfer(Entity* pDstEnt, Entity* pPetEnt, GameAccountDataExtract *pExtract, char** pestrError);
bool Entity_IsPetTransferValid(Entity* pSrcEnt, Entity* pDstEnt, Entity* pPetEnt, char** pestrError);

U32 PetCommands_GetLowestIndexEntRefByLuckyCharm(Entity* pOwner, S32 eType);

void entGetLuckyCharmInfo(Entity* pOwner, Entity* pTargetEnt, int* pTypeOut, int* pIndexOut);

bool SavedPet_HasRequirementsToResummonDeceasedPet(Entity *pOwner);

PetIntroductionWarp* Entity_GetPetIntroductionWarp(Entity* pEnt, U32 uPetID);
bool Entity_CanUsePetIntroWarp(Entity* pEnt, PetIntroductionWarp* pWarp);

bool trhEntity_CanSetPreferredPet(ATH_ARG NOCONST(Entity)* pEntity, U32 uiPetID, S32 iIndex);
ContainerID SavedPet_GetConIDFromPetID(Entity *pEntity, U32 uiPetID);

extern PetRestrictions g_PetRestrictions;

#endif 
