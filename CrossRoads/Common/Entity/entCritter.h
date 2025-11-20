/***************************************************************************
*     Copyright (c) 2003-2006, Cryptic Studios
*     All Rights Reserved
*     Confidential Property of Cryptic Studios
***************************************************************************/

#ifndef entCritter_H
#define entCritter_H
GCC_SYSTEM

#include "referencesystem.h"
#include "entityinteraction.h"
#include "structDefines.h"
#include "Message.h"
#include "entEnums.h"
#include "contact_enums.h"
#include "CostumeCommon.h"
#include "itemCommon.h"
#include "itemEnums.h"
#include "CombatEnums.h"
#include "RewardCommon.h"
#include "SavedPetCommon.h"
#include "WorldLibEnums.h"
#include "WorldVariable.h"
#include "allegiance.h"
#include "PowersEnums.h"

typedef struct AICivilian AICivilian;
typedef struct AIClientCivilian AIClientCivilian;
typedef struct AIPowerConfigDef AIPowerConfigDef;
typedef struct AlwaysPropSlotDef AlwaysPropSlotDef;
typedef struct ChatBubbleDef ChatBubbleDef;
typedef struct ContactDef ContactDef;
typedef struct CritterFaction CritterFaction;
typedef struct CharacterClass CharacterClass;
typedef struct DefaultItemDef DefaultItemDef;
typedef struct MovementRequesterDef MovementRequesterDef;
typedef struct Entity Entity;
typedef struct FSM FSM;
typedef struct GameEncounter GameEncounter;
typedef struct ItemDef ItemDef;
typedef struct InteractionDef InteractionDef;
typedef struct OldActor OldActor;
typedef struct OldActorInfo OldActorInfo;
typedef struct OldEncounter OldEncounter;
typedef struct PowerDef PowerDef;
typedef struct PowerTreeDef PowerTreeDef;
typedef struct PTNodeDef PTNodeDef;
typedef struct RewardTable RewardTable;
typedef struct SubstituteCritterDef SubstituteCritterDef;
typedef struct WorldInteractionPropertyEntry WorldInteractionPropertyEntry;
typedef struct InteriorDefRef InteriorDefRef;
typedef struct NemesisMinionCostumeSet NemesisMinionCostumeSet;

extern ParseTable parse_Critter[];
#define TYPE_parse_Critter Critter
extern ParseTable parse_CritterLootOwner[];
#define TYPE_parse_CritterLootOwner CritterLootOwner

#define MAX_CRITTER_POWERCONFIG_GROUPS 10

AUTO_STRUCT;
typedef struct CritterRankDifficulty
{
	const char *pcSubRank;				AST( STRUCTPARAM POOL_STRING )
	float fDifficulty;					AST( STRUCTPARAM )
} CritterRankDifficulty;

AUTO_STRUCT;
typedef struct CritterRankDef
{
	const char *pcName;					AST( STRUCTPARAM POOL_STRING )

	bool bIsDefault;				// Default rank for critters
	bool bIsMissionRewardDefault;	// Default rank for mission rewards
	bool bIgnoresFallingDamage;

	int iOrder;
	int iConModifier;

	float fLevelDifficultyMod;		// How much a level modifier effects difficulty

	CritterRankDifficulty **eaDifficulty; AST( NAME("DifficultyValue") )
} CritterRankDef;
extern ParseTable parse_CritterRankDef[];
#define TYPE_parse_CritterRankDef CritterRankDef

// This struct is used for loading
AUTO_STRUCT;
typedef struct CritterRankDefs
{
	CritterRankDef **eaRanks;			AST( NAME("CritterRank") )
} CritterRankDefs;
extern ParseTable parse_CritterRankDefs[];
#define TYPE_parse_CritterRankDefs CritterRankDefs

AUTO_STRUCT;
typedef struct CritterSubRankDef
{
	const char *pcName;					AST( STRUCTPARAM POOL_STRING )

	bool bIsDefault;

	const char *pcClassInfoType;		AST( POOL_STRING )

	int iOrder;
	int iConModifier;
} CritterSubRankDef;
extern ParseTable parse_CritterSubRankDef[];
#define TYPE_parse_CritterSubRankDef CritterSubRankDef

// This struct is used for loading
AUTO_STRUCT;
typedef struct CritterSubRankDefs
{
	CritterSubRankDef **eaRanks;		AST( NAME("CritterSubRank") )
} CritterSubRankDefs;
extern ParseTable parse_CritterSubRankDefs[];
#define TYPE_parse_CritterSubRankDefs CritterSubRankDefs

AUTO_STRUCT;
typedef struct aiModifierDef
{
	F32			fWeightMulti;			AST(DEFAULT(1))
	F32			fMinDistMulti;			AST(DEFAULT(1))
	F32			fMaxDistMulti;			AST(DEFAULT(1))
}aiModifierDef;

AUTO_STRUCT;
typedef struct CritterPowerConfig
{
	REF_TO(PowerDef) hPower;					AST(NAME(Power, Name), STRUCTPARAM, RESOURCEDICT(PowerDef))
	int			iKey;							AST(KEY)
	F32			fOrder;

	F32			fAIPreferredMinRange;			AST(NAME(AIPreferredMinRange, AIPreferedMinRange))
	F32			fAIPreferredMaxRange;			AST(NAME(AIPreferredMaxRange, AIPreferedMaxRange))
	F32     	fAIWeight;						AST(DEF(1))
	Expression	*pExprAIWeightModifier;			AST(NAME(AIWeightModifier) REDUNDANT_STRUCT(AIWeightModifier, parse_Expression_StructParam) LATEBIND)

	char		*pchAIChainTarget;				AST(NAME(AIChainTarget), POOL_STRING)
	F32			fAIChainTime;					AST(NAME(AIChainTime))
	Expression	*pExprAIChainRequires;			AST(NAME(AIChainRequiresBlock) REDUNDANT_STRUCT(AIChainRequires, parse_Expression_StructParam) LATEBIND)
	Expression	*pExprAIEndCondition;			AST(NAME(AIEndConditionBlock) REDUNDANT_STRUCT(AIEndCondition, parse_Expression_StructParam) LATEBIND)
	
	Expression	*pExprAIRequires;				AST(NAME(AIRequiresBlock) REDUNDANT_STRUCT(AIRequires, parse_Expression_StructParam) LATEBIND)
	Expression	*pExprAITargetOverride;			AST(NAME(AITargetOverrideBlock) REDUNDANT_STRUCT(AITargetOverride, parse_Expression_StructParam) LATEBIND)
	Expression  *pExprAICureRequires;			AST(NAME(AICureRequiresBlock) REDUNDANT_STRUCT(AICureRequires, parse_Expression_StructParam) LATEBIND)

	const char	*pchAIPowerConfigDef;			AST(NAME(AIPowerConfigDef), RESOURCEDICT(AIPowerConfigDef), POOL_STRING)
	AIPowerConfigDef *aiPowerConfigDefInst;		AST(NAME(AIPowerConfigDefInst), LATEBIND)
	
	S32			iGroup;
	F32			fChance;						AST(DEF(1))
	F32			fWeight;
	S32			iMinLevel;						AST(DEF(-1))
	S32			iMaxLevel;						AST(DEF(-1))
	Expression	*pExprAddPowerRequires;			AST(NAME(AddPowerRequiresBlock), REDUNDANT_STRUCT(AddPowerRequires, parse_Expression_StructParam) LATEBIND)
	bool		bAutoDescDisabled; // Hide this when describing the Critter as a pet in a normal detail Power AutoDesc
	bool		bDisabled;  // Ignore this power config because inheritance does not support removal

}CritterPowerConfig;

typedef struct CritterConfigList
{
	CritterPowerConfig **list;
}CritterConfigList;

typedef struct CritterItemConfigList
{
	DefaultItemDef **list;
}CritterItemConfigList;

AUTO_STRUCT;
typedef struct CritterCostume
{
	REF_TO(PlayerCostume) hCostumeRef;	AST(STRUCTPARAM RESOURCEDICT(PlayerCostume) NAME(Costume) ADDNAMES(Name) )
	F32		fWeight;					AST(STRUCTPARAM)
	int		iKey;						AST(STRUCTPARAM KEY)
	F32		fOrder;						AST(STRUCTPARAM)
	S32		iMinLevel;					AST(STRUCTPARAM DEF(-1))
	S32		iMaxLevel;					AST(STRUCTPARAM DEF(-1))
	S32		iMinTeamSize;				AST(STRUCTPARAM DEF(-1))
	S32		iMaxTeamSize;				AST(STRUCTPARAM DEF(-1))
	DisplayMessage displayNameMsg;      AST(STRUCT(parse_DisplayMessage))
	DisplayMessage displaySubNameMsg;   AST(STRUCT(parse_DisplayMessage))
	const char     *voiceSet;			AST( POOL_STRING )
	
	U8 bCreateIgnoresDisplayName : 1;
		// Created Critter should ignore the displayNameMsg field, even if it's set.  This allows use of the
		//  field for UI purposes without affected the name of the spawned Critter.
}CritterCostume;

AUTO_STRUCT;
typedef struct CritterLore
{
	int iKey;							AST(KEY)
	F32 fOrder;
	REF_TO(ItemDef)	hItem;				AST(RESOURCEDICT(ItemDef) NAME(Item))
	S32 DC;								
	AttribType eAttrib;					AST(NAME(Attrib), SUBTABLE(AttribTypeEnum), DEFAULT(-1))
	REF_TO(PowerDef) hPower;			AST(RESOURCEDICT(PowerDef) NAME(Power))
} CritterLore;

AUTO_ENUM;
typedef enum CritterVarTypeObsolete
{
	CritterVarType_Int, 
	CritterVarType_Float, 
	CritterVarType_String, 
	CritterVarType_Msg, 
}CritterVarTypeObsolete;

AUTO_STRUCT AST_IGNORE(Name) AST_IGNORE(Type) AST_IGNORE(Int) AST_IGNORE(Float) AST_IGNORE(String) AST_IGNORE(Msg);
typedef struct CritterVar
{
	int			   iKey;					AST(KEY)
	F32			   fOrder;

	WorldVariable var;						AST( NAME("Var") STRUCT(parse_WorldVariable) )
}CritterVar;

// Defines how a faction considers another target faction
AUTO_STRUCT;
typedef struct CritterFactionRelationship
{
	REF_TO(CritterFaction) hFactionRef;		AST(STRUCTPARAM)
		// The target faction

	F32	fReputation;						AST(STRUCTPARAM)
		// Granular reputation, currently unused, but due to STRUCTPARAM formatting must remain

	EntityRelation eRelation;				AST(STRUCTPARAM)
		// The actual relation, typically either 0/Friend or 1/Foe, but may also be Neutral

}CritterFactionRelationship;

// This structure is the "KOS"/combat faction, not be be confused with the kind of faction that gives reputation to players
AUTO_STRUCT AST_IGNORE(PlayerFaction);
typedef struct CritterFaction
{
	char* pchName;							AST(STRUCTPARAM POOL_STRING KEY)
	char* pchFileName;						AST( CURRENTFILE ) 
	bool bDefaultEnemyFaction;
	bool bDefaultPlayerFaction;
	bool bCanBeSubFaction;

	CritterFactionRelationship **relationship;

	U32	factionIndex;						NO_AST
}CritterFaction;

#define CFM_MAX_FACTIONS	32	

// 
typedef struct CritterFactionMatrix 
{
	EntityRelation	relations[CFM_MAX_FACTIONS][CFM_MAX_FACTIONS];
} CritterFactionMatrix;


AUTO_STRUCT;
typedef struct CritterGroup
{
	char *pchName;							AST(STRUCTPARAM POOL_STRING KEY)
	char* pchFileName;						AST( CURRENTFILE )  
	char *pchScope;							AST( POOL_STRING )
	char *pchNotes; // Designer notes field

	// This is the default KOS faction that controls critter behavior, not to be confused with factions that give reputation to players
//	REF_TO(CritterFaction)hFaction;			AST(NAME(Faction) REFDICT(CritterFaction))

	DisplayMessage displayNameMsg;			AST(STRUCT(parse_DisplayMessage))
	DisplayMessage descriptionMsg;			AST(STRUCT(parse_DisplayMessage))
	const char *pcIcon;						AST(NAME(Icon) POOL_STRING)

	S32	eSkillType;							AST(SUBTABLE(SkillTypeEnum))

	REF_TO(RewardTable) hRewardTable;		AST(NAME(RewardTable) REFDICT(RewardTable))
	REF_TO(RewardTable) hAddRewardTable;	AST(NAME(AddRewardTable) REFDICT(RewardTable))

	REF_TO(ChatBubbleDef) hChatBubbleDef;	AST(NAME(ChatBubbleDef) REFDICT(ChatBubbleDef))

	const char *maleVoiceSet;				AST(NAME(MaleVoiceSet) POOL_STRING )
	const char *femaleVoiceSet;				AST(NAME(FemaleVoiceSet) POOL_STRING )
	const char *neutralVoiceSet;			AST(NAME(NeutralVoiceSet) POOL_STRING )

	// Spawn animation, used if an anim is not found on the critterDef
	char *pchSpawnAnim;						AST( RESOURCEDICT(AnimationLibrary) POOL_STRING )
	F32 fSpawnLockdownTime;
	// Alternate spawn animation, used if an alternate anim is not found on the critterDef
	char *pchSpawnAnimAlternate;			AST( RESOURCEDICT(AnimationLibrary) POOL_STRING )
	F32 fSpawnLockdownTimeAlternate;

	//Critter FSM Vars
	CritterVar **ppCritterVars;

	CritterLore **ppCritterLoreEntries;
	//Lore which can be obtained about this crittergroup

	//Reference to an alternate critter group which should be used when pulling a headshot for contacts
	REF_TO(CritterGroup) hHeadshotCritterGroup;	AST(NAME(HeadshotCritterGroup) REFDICT(CritterGroup))

}CritterGroup;

typedef struct CritterEditInfo CritterEditInfo;


extern CritterRankDef **g_eaCritterRankDefs;
extern CritterSubRankDef **g_eaCritterSubRankDefs;
extern const char **g_eaCritterRankNames;
extern const char **g_eaCritterSubRankNames;
extern const char *g_pcCritterDefaultRank;
extern const char *g_pcCritterDefaultSubRank;
extern StaticDefineInt CritterTagsEnum[];

typedef struct CritterDef CritterDef;
typedef struct AMEditorDoc AMEditorDoc;
typedef struct InheritanceData	InheritanceData;

AUTO_STRUCT
AST_IGNORE_STRUCT(Latebind);
typedef struct CritterDef
{
	// Optional data to make this def inherit from another def
	InheritanceData *pInheritance;		AST(SERVER_ONLY)

	// Name and Group
	//------------------
	char*	pchName;					AST(STRUCTPARAM KEY POOL_STRING)		// Internal name.  NPCs should be referenced by this name.
	char*	pchFileName;				AST( CURRENTFILE )  // For debugging
	char*	pchComment;
	char*   pchScope;					AST( POOL_STRING )
	S32		iMinLevel;					AST(STRUCTPARAM)
	S32		iMaxLevel;					AST(STRUCTPARAM)
	bool	noCrossFade; 

	int iKeyBlock;

	CritterDef *pParent;				NO_AST
	AMEditorDoc *pDoc;					NO_AST
	int iIndex;							AST( DEF(-1) ) 
	bool bRandomCivilianName;				

	// This message overrides the CritterGroup display name message
	DisplayMessage hGroupOverrideDisplayNameMsg;      AST(STRUCT(parse_DisplayMessage))

	// Identity
	//------------------	
	REF_TO(CritterFaction)hFaction;		AST(NAME(Faction)) // Which villain faction am I associated with by default
	REF_TO(SpeciesDef)hSpecies;			AST(NAME(Species)) // Which species am I associated with by default
	int iGangID;						AST(NAME(Gang))
	REF_TO(CritterGroup)hGroup;			AST(NAME(GroupName)) // Which villain faction am I associated with by default

	char* pchClass;						AST( RESOURCEDICT(CharacterClassInfo) POOL_STRING)
	S32 *piTags;						AST( NAME(CritterTags), SUBTABLE(CritterTagsEnum) )
	CritterSpawnLimit iSpawnLimit;
	const char *pcRank;					AST( POOL_STRING )
	const char *pcSubRank;				AST( POOL_STRING ) // If NULL auto pick it
	bool bPvPFlagged;					// PvP-flagged critters don't inflict a death penalty and don't drop rewards
	bool bTemplate;						AST(DEF(false) NO_INHERIT)// Template critters should not be spawnable
	bool bDisabledForContacts;			// If true, then this CritterDef cannot be used to generate a contact's costume.
	S32	eSkillType;						AST(SUBTABLE(SkillTypeEnum))
	
	// Refcount for temporary critter defs.
	//------------------
	int refCount;	// If this is 0, critter def should never be deleted.  Otherwise, delete when it reaches 0

	// Display Data
	//------------------
	DisplayMessage displayNameMsg;      AST(STRUCT(parse_DisplayMessage))
	DisplayMessage displaySubNameMsg;   AST(STRUCT(parse_DisplayMessage))
	DisplayMessage descriptionMsg;      AST(STRUCT(parse_DisplayMessage))
	
	// Costume
	//------------------
	CritterCostume**	ppCostume;				AST(NAME(Costume) ADDNAMES(CostumeWeight))
	U32 bGenerateRandomCostume : 1;
	U32 bRandomDefaultStance : 1;
	REF_TO(PowerDef) hDefaultStanceDef;			AST(NAME(DefaultStanceDef))
	REF_TO(PlayerCostume) hOverrideCostumeRef;	AST(RESOURCEDICT(PlayerCostume) NAME(OverrideCostume) )

	// stance-words applied to this critter
	const char **ppchStanceWords;				AST(NAME(StanceWords), POOL_STRING)

	// Rewards
	//------------------
	REF_TO(RewardTable) hRewardTable;			AST(NAME(RewardTable) REFDICT(RewardTable))
	REF_TO(RewardTable) hAddRewardTable;		AST(NAME(AddRewardTable) REFDICT(RewardTable))

	// AI
	//------------------
	// These two are still here to avoid lots of inheritance errors, should be removed eventually
	F32 fPreferredMinRange_UNUSED;				AST( NAME(PreferredMinRange, PreferedMinRange) NO_WRITE)
	F32 fPreferredMaxRange_UNUSED;				AST( NAME(PreferredMaxRange, PreferedMaxRange) NO_WRITE)
	F32 fLeash_UNUSED;							AST( NAME(Leash) NO_WRITE)
	char *pchAIConfig;							AST( NAME(AIConfig, AI) RESOURCEDICT(AIConfig) POOL_STRING )
	REF_TO(FSM) hFSM;							AST( NAME(FSM) NAME(AIFSM) )
	REF_TO(FSM) hCombatFSM;						AST( NAME(CombatFSM) )
	char *pchSpawnAnim;							AST( RESOURCEDICT(AnimationLibrary) POOL_STRING )
	F32 fSpawnLockdownTime;
	char *pchSpawnAnimAlternate;				AST( RESOURCEDICT(AnimationLibrary) POOL_STRING )
	F32 fSpawnLockdownTimeAlternate;
	F32 fSpawnWeight;							AST( DEF(1) )
	
	// Powers
	//------------------
	CritterPowerConfig **ppPowerConfigs;

	F32 lingerDuration;					AST( DEF(15) )
	F32 fHue;

	// looking like we might want to make flags field
	U32 bUntargetable : 1;
	U32 bUnselectable : 1;
	U32 bInvulnerable : 1;
	U32 bUnstoppable : 1;
	U32 bUnkillable : 1;
	U32 bLevelAdjusting : 1;
	U32 bPseudoPlayer : 1;
	U32 bDisableFaceActivate : 1;		AST( NAME(DisableTurnToFace) )
	U32 bIgnoreCombatMods : 1;
	U32 bNonCombat : 1;
	U32 bNoPowersAllowed : 1;
	U32 bIgnoreExternalInnates : 1;
	U32 bIgnoreEntCreateHue : 1;
	U32 bDropMyInventory : 1;
	U32 bNoInterpAlphaOnSpawn : 1;
	U32 bUseCapsuleForPowerArcChecks : 1;
	U32 bUseClosestPowerAnimNode : 1;
	U32 bSpecialLargeMonster : 1;

	//This bit causes conditional and continuing bits and hit reacts to be ignored if they are from an external source
	U32 bIgnoreExternalAnimBits : 1;

	U32 bAlwaysHaveWeaponsReady : 1;
	
	// Interaction
	//------------------
	OldInteractionProperties oldInteractProps;	AST( NAME(Interaction) )
	REF_TO(InteractionDef) hInteractionDef;		AST( NAME("InteractionDef"))
	U32 uInteractRange;							AST( NAME("InteractRange"))

	//Critter Override Fields
	F32 fMass;
	kCritterOverrideFlag eInteractionFlags; AST(FLAGS, SUBTABLE(kCritterOverrideFlagEnum)) 

	char **ppUnderlings;					AST(RESOURCEDICT(CritterDef))

	// This critter will send to at least this distance and can be spotted regardless of perception out to this distance
	F32 fEntityMinSeeAtDistance;

	// Riding
	//------------------

	Expression* pExprRidable;				AST(NAME("RidableBlock") REDUNDANT_STRUCT("Ridable", parse_Expression_StructParam) LATEBIND)
	char *pchRidingPower;					AST( RESOURCEDICT(PowerDef) POOL_STRING )
	char *pchRidingItem;					AST( RESOURCEDICT(ItemDef) POOL_STRING )
	char *pchRidingBit;						AST( POOL_STRING )


	//Critter FSM Vars
	CritterVar **ppCritterVars;

	DefaultItemDef **ppCritterItems;
	//Items to be equipped upon creation

	CritterLore **ppCritterLoreEntries;
	//Lore which can be obtained about this critter

	// for overriding the default surface requester, right now there is only the dragon requester
	REF_TO(MovementRequesterDef) hOverrideMovementRequesterDef;	AST(NAME(OverrideMovementRequesterDef) RESOURCEDICT(MovementRequesterDef))

	bool bDeprecated;						//flag for content to allow easy flagging of deprecated critters

	Gender	eGender;	AST(SUBTABLE(GenderEnum)) //!! deprecated, wanted to just AST_IGNORE this but it broke because of existing inheritance data

}CritterDef;

AUTO_STRUCT;
typedef struct ref_PowerDef
{
	REF_TO(PowerDef) hPowerDef;		AST(STRUCTPARAM, NAME(onDeathPower))
}ref_PowerDef;

AUTO_STRUCT;
typedef struct CritterOverrideDef
{
	char*	pchName;					AST( STRUCTPARAM KEY POOL_STRING )	// Internal name.
	char*	pchFileName;				AST( CURRENTFILE )
	char*   pchScope;					AST( POOL_STRING )

	F32		fMass;						AST(DEFAULT(-1))
	char*	pchComment;

	ref_PowerDef **ppOnDeathPowers;		AST(NAME(onDeathPower))

	kCritterOverrideFlag eFlags;		AST(FLAGS, SUBTABLE(kCritterOverrideFlagEnum), DEFAULT(kCritterOverrideFlag_None)) 

}CritterOverrideDef;

AUTO_STRUCT;
typedef struct CritterTags {
	char **critterTags;
} CritterTags;


typedef struct CritterFactionDefaults
{
	REF_TO(CritterFaction) hDefaultEnemyFaction;
	REF_TO(CritterFaction) hDefaultPlayerFaction;
} CritterFactionDefaults;

CritterFactionDefaults* FactionDefaults;

extern DictionaryHandle *g_hPetStoreDict;
extern PetDef **g_ppAutoGrantPets;

AUTO_STRUCT;
typedef struct AlwaysPropSlotRef
{
	REF_TO(AlwaysPropSlotDef) hPropDef; AST(REFDICT(AlwaysPropSlotDef) STRUCTPARAM)
}AlwaysPropSlotRef;

AUTO_STRUCT;
typedef struct PetDiagNode
{
	S32 *piCategories;				AST(NAME(Category), SUBTABLE(PowerCategoriesEnum))
	PowerPurpose ePurpose;			AST(NAME(Purpose))
	int iCount;						AST(NAME(Count) DEFAULT(1))
	PTNodeDefRef **ppReplacements;	AST(NAME(Replacement))
}PetDiagNode;

AUTO_STRUCT;
typedef struct PetDiag
{
	char *pchName;							AST(KEY STRUCTPARAM)
	const char *pchFilename;				AST( CURRENTFILE )
	PetDiagNode **ppNodes;					AST(NAME(PetDiagNode))
}PetDiag;

AUTO_STRUCT AST_IGNORE(Type);
typedef struct PetDef
{
	char *pchPetName;						AST(NAME(PetName) STRUCTPARAM KEY)
	const char *pchFilename;				AST( CURRENTFILE )

	REF_TO(CritterDef) hCritterDef;			AST(NAME(CritterDef) REFDICT(CritterDef))
	DisplayMessage displayNameMsg;			AST(NAME(DisplayName) STRUCT(parse_DisplayMessage))
	REF_TO(AllegianceDef) hAllegiance;		AST(NAME(Allegiance) REFDICT(Allegiance))
	AlwaysPropSlotRef **ppAlwaysPropSlot;	AST(NAME(AlwaysPropSlot))
	PTNodeDefRef **ppEscrowPowers;			AST(NAME(EscrowPower))
	InteriorDefRef **ppInteriorDefs;		AST(NAME(InteriorDef))
	REF_TO(ItemDef) hTradableItem;			AST(NAME(TradeableItem))
	S32 iMinActivePuppetLevel;				AST(NAME(MinActivePuppetLevel))
	U32 bCanBePuppet : 1;					AST(NAME(CanBePuppet))
	U32 bCanRemove : 1;						AST(NAME(CanRemove))
	U32 bCritterPet : 1;					AST(NAME(CritterPet))
	U32 bChooseRandomName : 1;				AST(NAME(ChooseRandomName))
	U32 bIsUnique : 1;						AST(NAME(IsUnique))
	U32 bAutoGrant : 1;						AST(NAME(AutoGrant))
		//If turned on, this pet will be granted during character creation
	U32 bDisableTrainingFromPet : 1;		AST(NAME(DisableTrainingFromPet))
		// If set, the training option on this pet will be disabled

	REF_TO(PetDiag) hPetDiag;				AST(NAME(PetDiag))
		// Diagnostic to run on the pet, to keep it up to date with current data

	REF_TO(CharacterClass) hClass;			AST(NAME(CharacterClass) REFDICT(CharacterClass))
		// This is filled in from the critter def

	PetAcquireLimit eAcquireLimit;			AST(NAME(AcquireLimit) SUBTABLE(PetAcquireLimitEnum))
		// Acquire limit on this pet
}PetDef;

AUTO_STRUCT;
typedef struct PetDefRef
{
	REF_TO(PetDef) hPet;				AST(REFDICT(PetDef))
}PetDefRef;

AUTO_STRUCT AST_CONTAINER;
typedef struct PetDefRefCont
{
	CONST_REF_TO(PetDef) hPet;			AST(PERSIST)
	const U32 uiPetID;					AST(PERSIST)
	S32 bPetIsDeceased;					AST(PERSIST NO_TRANSACT)
}PetDefRefCont;

void crittertags_Load(void);
void critter_Load(void);
void critterFaction_Load(void);
bool critterOverride_Validate(CritterOverrideDef *pOverride);
void CritterOverride_Load(void);

extern DictionaryHandle g_hCritterDefDict;
extern DictionaryHandle g_hCritterFactionDict;
extern DictionaryHandle g_hCritterGroupDict;
extern DictionaryHandle g_hCritterOverrideDict;

//---------------------------------------------------------------
/////////////////////////////////////////////////////////////////
//---------------------------------------------------------------

CritterDef* critter_DefGetByName( const char * pchName );
CritterOverrideDef* critter_OverrideDefGetByName(const char * pchName);
CritterFaction* critter_FactionGetByName( const char * pchName );
CritterGroup* critter_GroupGetByName( char * pchName );

CritterDef **critter_GetMatchingGroupList( CritterDef *** search_list, CritterGroup * pGroup );

bool critter_DefValidate( CritterDef *def, bool bFullData );

bool critter_FactionVerify(CritterFaction *faction);

AUTO_STRUCT;
typedef struct InteractionLootTracker
{
	// State kept when a player is interacting
	// Loot bags for the interaction
	InventoryBag **eaLootBags;			AST(NO_INDEX)

} InteractionLootTracker;
extern ParseTable parse_InteractionLootTracker[];
#define TYPE_parse_InteractionLootTracker InteractionLootTracker

AUTO_STRUCT;
typedef struct CritterEncounterData {
	// Old Encounter System Data
	// The encounter that this entity is a part of
	OldEncounter* parentEncounter;					NO_AST
	OldActor* sourceActor;							NO_AST
	// An optional contact override
	REF_TO(ContactDef) hContactDefOverride;		AST(SERVER_ONLY)

	// New Encounter System Data
	// The encounter that this entity is a part of
	GameEncounter *pGameEncounter;				NO_AST
	int iActorIndex;							NO_AST

	// Team size and level at time of spawning
	U8 activeTeamSize;
	U8 activeTeamLevel;
	bool bNonCombatant;							NO_AST

	// Tracks loot for interactable critters
	InteractionLootTracker *pLootTracker;		AST(SERVER_ONLY)
	int iPlayerOwnerID;							NO_AST

	// Used for fall-through-world check
	Vec3 origPos;								NO_AST

	// The list of container IDs for all players interacting with this critter
	U32 *perInteractingPlayers;					AST(SERVER_ONLY)

	// The number of interaction entries cached for quick lookup
	int iNumInteractionEntries;                 NO_AST

	//This is set to true when a critter has spawned normally and should wait before aggro
	bool bEnableSpawnAggroDelay;
} CritterEncounterData;

AUTO_ENUM;
typedef enum CritterSubType
{
	CritterSubType_UNKNOWN,
	CritterSubType_CIVILIAN_CAR,
	CritterSubType_COUNT,
} CritterSubType;

// Info attached to entities containing critter related information
AUTO_STRUCT AST_CONTAINER;
typedef struct Critter
{
	DirtyBit dirtyBit;								AST(NO_NETSEND)

	// Display name to use.  Not a message, but a StructAllocString.  Used for nemesis names
	char* displayNameOverride;

	// The def that created the critter
	CONST_REF_TO(CritterDef) critterDef;			AST(PERSIST SUBSCRIBE SERVER_ONLY REFDICT(CritterDef))

	CONST_REF_TO(PetDef) petDef;					AST(PERSIST SUBSCRIBE SELF_ONLY REFDICT(PetDef))

	// The def of the critter override being used on this critter TODO(MM): Do I really need this?
	REF_TO(CritterOverrideDef) critterOverrideDef;	AST(SERVER_ONLY REFDICT(CritterOverrideDef))

	// Rewards
	F32 fNumericRewardScale;						AST(SERVER_ONLY DEFAULT(1.0))
	RewardTableRef** eaAdditionalRewards;			AST(SERVER_ONLY)
	WorldEncounterRewardType eRewardType;			AST(SERVER_ONLY)
	WorldEncounterRewardLevelType eRewardLevelType; AST(SERVER_ONLY)
	S32 iRewardLevel;								AST(SERVER_ONLY)

	// Time after death until this critter gets removed(How long it lingers for)
	F32	timeToLinger;						NO_AST
	F32	StartingTimeToLinger;				NO_AST

	// Override the default send distance for this critter
	F32 fOverrideSendDistance;				AST(SERVER_ONLY)

	// Data used by encounter system for tracking this critter
	CritterEncounterData encounterData;

	union {
		AICivilian *civInfo;					
		AIClientCivilian *clientCivInfo;
	};										NO_AST

	// The player who caused this critter to spawn
	EntityRef spawningPlayer;				NO_AST

	// The Saved Pet this critter was spawned from (e.g. Nemesis)
	REF_TO(Entity) hSavedPet;				AST(SERVER_ONLY COPYDICT(EntitySavedPet))
	
	// The Saved Pet that owns this critter (e.g. Nemesis Minions)
	REF_TO(Entity) hSavedPetOwner;			AST(SERVER_ONLY COPYDICT(EntitySavedPet))

	//---------------------------------------------------------------

	// Data Client cares about
	REF_TO(Message) hDisplayNameMsg;
	REF_TO(Message) hDisplaySubNameMsg;
	REF_TO(Message) hGroupDisplayNameMsg;
	const char *pcGroupIcon;				AST( POOL_STRING )

	const char *voiceSet;					AST( POOL_STRING )

	// Encounter name hashed as a U32, unique to server.  Sent to client for use in UI
	U32	iEncounterKey;

	// These are derived from RewardBagInfo (and team looting modes)
	REF_TO(PlayerCostume) hYoursCostumeRef;		AST(REFDICT(PlayerCostume))
	REF_TO(PlayerCostume) hNotYoursCostumeRef;	AST(REFDICT(PlayerCostume))

	EARRAY_OF(InventoryBag) eaLootBags;	
	const char*	pcLootGlowFX;				AST(POOL_STRING CLIENT_ONLY)
	
	const char **ppchStanceWords;			AST(NAME(StanceWords), POOL_STRING)
		// stance-words applied to this critter

	U32 bAutoInteract : 1;					NO_AST
	U32 bDoNotAutoSetLootCostume : 1; // if false, the client will set the costume based on the ownership of the loot.
	U32 bKilled : 1;						AST(SERVER_ONLY)

	// Tells if the critter has interaction properties
	U32 bIsInteractable : 1;

	U32 bRidable : 1; // If this is ridable. This is here for client use
	U32 bPseudoPlayer : 1; // If this is a pseudo player.  This is here for client use
	
    U32 bBoss : 1;							NO_AST
	U32 bEncounterFar : 1;					NO_AST //Is this critter part of an encounter that is so far away that it cannot be targeted?
	U32 bDeathRewardsGiven : 1;				AST(SERVER_ONLY)	// This critter has already given rewards out from dying
	U32 bIgnoreExternalAnimBits : 1;  // The client needs access to the CritterDef's ignore hit react flag so it can predict whether the critter will ignore a hit reaction

	U32 bUseCapsuleForPowerArcChecks : 1; // If this is set, use the entire capsule for power arc checks instead of just a point
	U32 bUseClosestPowerAnimNode : 1; // If this is set, use the closest power anim node for power anims instead of a random node
	
	U32 bAutoLootMe : 1; //If set, players that loot me should treat AutoLoot as being on. As opposed to bAutoLootInteract, which is "rollover" behavior, this just means that starting the interaction manually will also run "takeall"

	U32 bSetSpawnAnim : 1;

	U32 bSetStance : 1;						NO_AST

	const char *pcRank;						AST(POOL_STRING)
	const char *pcSubRank;					AST(POOL_STRING)

	// Interaction properties
	kCritterOverrideFlag eInteractionFlag;	AST(FLAGS SUBTABLE(kCritterOverrideFlagEnum))
	F32 fMass;
	U32 uInteractDist;

	CritterSubType	eCritterSubType;		AST(FLAGS SUBTABLE(CritterSubTypeEnum))
		 
	// The nemesis minion costume set that is only on the server and is used for sending gameevents about the costume (type of) of the nemesis minion
	const char *pcNemesisMinionCostumeSet;	AST(SERVER_ONLY POOL_STRING)

} Critter;

CritterDef* critter_DefFind( CritterGroup *pGroup, const char *pcRank, const char *pcSubRank, int iLevel, int *totalFailure, CritterDef ***excludeDefs);
// Searches for a matching critter. Search precedence Group > Rank > Level 
// Will return a critter def even if exact matches are not found

void critter_FindAllWithPower( char * pchPower, char ***pppchCritterNames);
// Finds all combats that use the power, and fills the earray with their names

int critterdef_GetNextPowerConfigKey(CritterDef *pDef, int iPreviousKey);
int critterdef_GetNextCostumeKey(CritterDef *pDef, int iPreviousKey);
int critterdef_GetNextVarKey(CritterDef *pDef, int iPreviousKey);
int critterdef_GetNextItemKey(CritterDef *pDef, int iPreviousKey);
int critterdef_GetNextLoreKey(CritterDef *pDef, int iPreviousKey);
bool critterdef_HasOldInteractProps(CritterDef *pDef);

// Add or remove ref-counted critter defs (used for nemesis content)
CritterDef* critterdef_AddSubstituteDefToDictionary(SA_PARAM_NN_STR const char* defName, SA_PARAM_NN_VALID SubstituteCritterDef* substDef, SA_PARAM_OP_VALID Entity* playerEnt);
// This is safe to use on any critter, even those without substitute defs
void critterdef_TryRemoveSubstituteFromDictionary(SA_PARAM_NN_VALID Critter* deletedCritter);

bool critterRankExists(const char *pcRank);
bool critterRankEquals(const char *pcRank1, const char *pcRank2);
bool critterRankIgnoresFallingDamage(const char *pcRank);
void critterRankGetNameCopies(char ***peaNames);
float critterRankGetDifficultyValue(const char *pcRank, const char *pcSubRank, int levelMod);
int critterRankGetConModifier(const char *pcRank, const char *pcSubRank);
int critterRankGetOrder(const char *pcRank);
const char *critterRankGetMissionDefault(void);

bool critterSubRankExists(const char *pcSubRank);
void critterSubRankGetNameCopies(char ***peaNames);
const char *critterSubRankGetClassInfoType(const char *pcSubRank);
int critterSubRankGetOrder(const char *pcSubRank);

CritterGroup* entGetCritterGroup(SA_PARAM_NN_VALID Entity* e);

void critterdef_CostumeSort(CritterDef *pDef, CritterCostume ***pppCostumesSorted);
int critterdef_GetCostumeKeyFromIndex(CritterDef *pDef, int iIndex);

//Accessor functions for interaction entries defined on the critter def
int critter_GetNumInteractionEntries(Critter* pCritter);
int critterdef_GetNumInteractionEntries(CritterDef* pDef);
WorldInteractionPropertyEntry *critter_GetInteractionEntry(Critter* pCritter, int iInteractionIndex);
WorldInteractionPropertyEntry *critterdef_GetInteractionEntry(CritterDef* pDef, int iInteractionIndex);

#define LOOT_CRITTER_LINGER (300.0)

CharClassTypes petdef_GetCharacterClassType(PetDef* pPetDef);

void critterFactionMatrix_SetFactionRelation(CritterFactionMatrix *factionMtx, U32 f1Idx, U32 f2Idx, EntityRelation relation);

bool killreward_Validate(RewardTable *rw,char const *fn);

void critter_AddCombat( Entity *be, CritterDef *pCritter, int iLevel, int iTeamSize, const char *pcSubrank, 
						F32 fRandom, bool bAddPowers,bool bFullReset, CharacterClass* pClass, bool bPowersEntCreatedEnt);

#endif
