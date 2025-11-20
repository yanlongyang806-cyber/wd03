#ifndef _WLGROUPPROPERTYSTRUCTS_H_
#define _WLGROUPPROPERTYSTRUCTS_H_
#pragma once
GCC_SYSTEM

#include "WorldLibEnums.h"
#include "WorldLibStructs.h"
#include "wlSky.h"
#include "wlCurve.h"
#include "WorldVariable.h"
#include "GlobalEnums.h"
#include "message.h"
#include "referencesystem.h"
#include "GenericMeshRemesh.h"

typedef struct AIAnimList AIAnimList;
typedef struct AllegianceDef AllegianceDef;
typedef struct AutoLODTemplate AutoLODTemplate;
typedef struct ContactDef ContactDef;
typedef struct DoorTransitionSequenceDef DoorTransitionSequenceDef;
typedef struct PlayerCostume PlayerCostume;
typedef struct PetContactList PetContactList;
typedef struct CritterDef CritterDef;
typedef struct CritterFaction CritterFaction;
typedef struct CritterGroup CritterGroup;
typedef struct CritterOverrideDef CritterOverrideDef;
typedef struct CutsceneDef CutsceneDef;
typedef struct DynFxInfo DynFxInfo;
typedef struct EncLayerFSM EncLayerFSM;
typedef struct EncounterDef EncounterDef;
typedef struct EncounterTemplate EncounterTemplate;
typedef struct EncounterWaveProperties EncounterWaveProperties;
typedef struct Expression Expression;
typedef struct FSM FSM;
typedef struct GroupDef GroupDef;
typedef struct InteractionDef InteractionDef;
typedef struct ItemAssignmentDef ItemAssignmentDef;
typedef struct ItemDef ItemDef;
typedef struct MissionDef MissionDef;
typedef struct PowerDef PowerDef;
typedef struct QueueDef QueueDef;
typedef struct RewardTable RewardTable;
typedef struct TriggerCondition TriggerCondition;
typedef struct WorldPathEdge WorldPathEdge;
typedef struct WorldFXCondition WorldFXCondition;

//////////////////////////////////////////////////////////////////////////
// To add new property structs to a groupdef, search for GroupProps and follow the instructions.
// Also do any binning-specific changes needed and add UI.
// NOTE: AVOID DEFAULTS! There's a deeply-rooted issue with textparser and ParserSend that prevents
// the editor from clearing defaulted values.
//////////////////////////////////////////////////////////////////////////

#define GROUP_CHILD_MAX_PARAMETERS 48

AUTO_STRUCT;
typedef struct GroupChildParameter
{
	const char *parameter_name;		AST( NAME(ParameterName) STRUCTPARAM POOL_STRING )
	int int_value;					AST( NAME(IntValue) )
	const char *string_value;		AST( NAME(StringValue) POOL_STRING )
	const char *inherit_value_name; AST( NAME(InheritValue) POOL_STRING )
} GroupChildParameter;
extern ParseTable parse_GroupChildParameter[];
#define TYPE_parse_GroupChildParameter GroupChildParameter

// Data that should be simplely copied when the child is moved around the tree
AUTO_STRUCT;
typedef struct GroupChildSimpleData
{
	// Child parameters
	GroupChildParameter **params; AST( NAME(Param) )
} GroupChildSimpleData;
extern ParseTable parse_GroupChildSimpleData[];
#define TYPE_parse_GroupChildSimpleData GroupChildSimpleData

// Each GroupDef instance gets one of these 
AUTO_STRUCT AST_STARTTOK("") AST_ENDTOK(End) AST_STRIP_UNDERSCORES;
typedef struct GroupChild
{
	// StructParser-only fields
	int			name_uid;		AST( STRUCTPARAM )
	const char *name;			AST( NAME(Name) POOL_STRING ) // Deprecated; only use name_uid for references
	const char *debug_name;		AST( NAME(DebugName) POOL_STRING ) // The group's name when last updated, for debugging *only*
	Vec3		pos;			AST( NAME(Pos) )
	Vec3		rot;			AST( NAME(Rot) )

	// Common fields
	F32			scale;			AST( NAME(Scale) )
	U32			seed;			AST( NAME(Seed) )
	U32			uid_in_parent;	AST( NAME(UidInParent) )
	bool		always_use_seed; AST( NAME(UseSeed) )
	F32			weight;			AST( NAME(Weight) ) //For random select

	GroupChildSimpleData simpleData; AST( EMBEDDED_FLAT )

	// Runtime-only fields
	Mat4		mat;			AST( NO_TEXT_SAVE )
	U32			child_mod_time;	AST( NO_TEXT_SAVE )	// last time anything in this struct was changed (used for tracker updating)

} GroupChild;
extern ParseTable parse_GroupChild[];
#define TYPE_parse_GroupChild GroupChild


AUTO_STRUCT AST_STARTTOK("") AST_ENDTOK("End") AST_STRIP_UNDERSCORES;
typedef struct GlobalGAELayerDef
{
	const char* name;									AST(STRUCTPARAM POOL_STRING)

	//GameAudioEventMap *game_audio_event_map;
} GlobalGAELayerDef;

extern ParseTable parse_GlobalGAELayerDef[];
#define TYPE_parse_GlobalGAELayerDef GlobalGAELayerDef


// TODO: replace game-specific enums that are commented out

AUTO_STRUCT AST_STARTTOK("") AST_ENDTOK("End") AST_STRIP_UNDERSCORES;
typedef struct WorldGrantMissionActionProperties
{
	WorldMissionActionType eType;						AST( NAME("MissionFrom") )
	REF_TO(MissionDef) hMissionDef;						AST( NAME("MissionDef") REFDICT(MissionDef) )
	const char *pcVariableName;							AST( NAME("VariableName") )
} WorldGrantMissionActionProperties;

AUTO_STRUCT AST_STARTTOK("") AST_ENDTOK("End") AST_STRIP_UNDERSCORES;
typedef struct WorldGrantSubMissionActionProperties
{
	const char *pcSubMissionName;						AST( NAME("SubMissionName") POOL_STRING )
} WorldGrantSubMissionActionProperties;

AUTO_STRUCT;
typedef struct WorldGameActionHeadshotProperties 
{
	const char* pchHeadshotStyleDef;							AST( POOL_STRING )

	WorldGameActionHeadshotType eType;							AST(NAME("Type"))
	
	// Costume override
	REF_TO(PlayerCostume) hCostume;								AST(NAME("Costume"))

	// Use a PetContactList to determine the costume
	REF_TO(PetContactList) hPetContactList;						AST(NAME("PetContactList"))


	//-- Use a critter group (or critter group/def from map var) and identifier to determine the costume --//

	// Whether the critter group is specified or gathered from a map variable
	WorldHeadshotMapVarOverrideType eCritterGroupType;			AST(NAME("CritterGroupType"))

	// Critter group
	REF_TO(CritterGroup) hCostumeCritterGroup;					AST(NAME("CostumeCritterGroup"))

	// Map variable of critter group/def to generate costume from
	const char* pchCritterGroupMapVar;							AST(NAME("CritterGroupMapVar") POOL_STRING)

	// Identifier
	const char* pchCritterGroupIdentifier;						AST(NAME("CritterGroupIdentifier") POOL_STRING)

} WorldGameActionHeadshotProperties;

AUTO_STRUCT AST_STARTTOK("") AST_ENDTOK("End") AST_STRIP_UNDERSCORES;
typedef struct WorldMissionOfferActionProperties
{
	WorldMissionActionType eType;						AST( NAME("MissionFrom") )
	REF_TO(MissionDef) hMissionDef;						AST( NAME("MissionDef") REFDICT(MissionDef) )
	const char *pcVariableName;							AST( NAME("VariableName") )
	WorldGameActionHeadshotProperties *pHeadshotProps;
	DisplayMessage headshotNameMsg;						AST(STRUCT(parse_DisplayMessage))  
} WorldMissionOfferActionProperties;

AUTO_STRUCT AST_STARTTOK("") AST_ENDTOK("End") AST_STRIP_UNDERSCORES;
typedef struct WorldDropMissionActionProperties
{
	const char *pcMissionName;						AST( NAME("MissionName") POOL_STRING )
} WorldDropMissionActionProperties;

AUTO_STRUCT AST_STARTTOK("") AST_ENDTOK("End") AST_STRIP_UNDERSCORES;
typedef struct WorldGiveItemActionProperties
{
	REF_TO(ItemDef) hItemDef;							AST( NAME("ItemDef") REFDICT(ItemDef) )
	// Must be greater than zero
	S32 iCount;											AST( NAME("Count") )
} WorldGiveItemActionProperties;

AUTO_STRUCT AST_STARTTOK("") AST_ENDTOK("End") AST_STRIP_UNDERSCORES;
typedef struct WorldGiveDoorKeyItemActionProperties
{
	REF_TO(ItemDef) hItemDef;							AST( NAME("ItemDef") REFDICT(ItemDef) )
	CONST_STRING_POOLED pchDoorKey;						AST( NAME("DoorKey") POOL_STRING)	// Tag used to identify the door
	WorldVariableDef* pDestinationMap;					AST( NAME("DestinationMap") )
	WorldVariableDef** eaVariableDefs;					AST( NAME("VariableDefs") )
} WorldGiveDoorKeyItemActionProperties;
extern ParseTable parse_WorldGiveDoorKeyItemActionProperties[];
#define TYPE_parse_WorldGiveDoorKeyItemActionProperties WorldGiveDoorKeyItemActionProperties

AUTO_STRUCT AST_STARTTOK("") AST_ENDTOK("End") AST_STRIP_UNDERSCORES;
typedef struct WorldNPCSendEmailActionProperties
{
	REF_TO(ItemDef) hItemDef;							AST( NAME("ItemDef") REFDICT(ItemDef) )								// item ref to send (optional)
	
	DisplayMessage dFromName;							AST( NAME("EmailFromName") 	STRUCT(parse_DisplayMessage) )			// What Name this email is from
	DisplayMessage dSubject;							AST( NAME("EmailSubject") 	STRUCT(parse_DisplayMessage) )			// Subject of the email
	DisplayMessage dBody;								AST( NAME("EmailBody") 		STRUCT(parse_DisplayMessage) )			// Body of the email
	
	U32	uFutureSendTime;								AST( NAME("FutureSendTime") )										// seconds from now for the email to arrive (can be zero i.e. now)
} WorldNPCSendEmailActionProperties;

AUTO_STRUCT AST_STARTTOK("") AST_ENDTOK("End") AST_STRIP_UNDERSCORES;
typedef struct WorldActivityLogActionProperties
{
	S32 eEntryType;										AST( NAME("EntryType") SUBTABLE(ActivityLogEntryTypeEnum) )
	DisplayMessage dArgString;							AST( NAME("ArgString") STRUCT(parse_DisplayMessage) )
} WorldActivityLogActionProperties;

AUTO_STRUCT AST_STARTTOK("") AST_ENDTOK("End") AST_STRIP_UNDERSCORES;
typedef struct WorldGADAttribValueActionProperties
{
	char *pcAttribKey;									AST( NAME("Attrib") )		//The attribute in the game account data to change
	char *pcValue;										AST( NAME("Value") )		//The value to set the attrib to
	WorldVariableActionType eModifyType;				AST( NAME("ModifyType") )	// How to modify the attribute
} WorldGADAttribValueActionProperties;

AUTO_STRUCT AST_STARTTOK("") AST_ENDTOK("End") AST_STRIP_UNDERSCORES;
typedef struct WorldShardVariableActionProperties
{
	char *pcVarName;									AST( NAME("VariableName") POOL_STRING)
	WorldVariableActionType eModifyType;				AST( NAME("ModifyType") )

	// One of the following may be set depending on the type
	WorldVariable *pVarValue;							AST( NAME("VariableValue") )
	int iIntIncrement;									AST( NAME("IntIncrement") )
	int fFloatIncrement;								AST( NAME("FloatIncrement") )
} WorldShardVariableActionProperties;

AUTO_STRUCT AST_STARTTOK("") AST_ENDTOK("End") AST_STRIP_UNDERSCORES;
typedef struct WorldTakeItemActionProperties
{
	REF_TO(ItemDef) hItemDef;							AST( NAME("ItemDef") REFDICT(ItemDef) )

	// Must be 0 or greater.  This the min number to make the action succeed
	S32 iCount;											AST( NAME("Count") )

	// If true, it will take all of the items, even if there are more than the count
	bool bTakeAll;										AST( NAME("TakeAll") )
} WorldTakeItemActionProperties;

AUTO_STRUCT AST_STARTTOK("") AST_ENDTOK("End") AST_STRIP_UNDERSCORES;
typedef struct WorldSendFloaterActionProperties
{
	DisplayMessage floaterMsg;							AST( NAME("FloaterMsg") STRUCT(parse_DisplayMessage) )
	Vec3 vColor;										AST( NAME("Color") )
} WorldSendFloaterActionProperties;

AUTO_STRUCT AST_STARTTOK("") AST_ENDTOK("End") AST_STRIP_UNDERSCORES AST_FIXUPFUNC( fixupWorldSendNotificationActionProperties );
typedef struct WorldSendNotificationActionProperties
{
	const char* pchNotifyType;							AST( NAME("NotifyType") POOL_STRING )
	char *pchLogicalString;								AST(NAME("LogicalString"))
	char *pchSound_Deprecated;							AST(POOL_STRING NAME(Sound))
	DisplayMessageWithVO notifyMsg;						AST( NAME("NotifyMsg") STRUCT(parse_DisplayMessageWithVO) )
	const char* astrSoundOverride;						AST( NAME(SoundOverride) POOL_STRING )
	WorldGameActionHeadshotProperties *pHeadshotProperties;
	char *pchSplatFX;									AST(NAME(SplatFX))
} WorldSendNotificationActionProperties;

AUTO_STRUCT AST_STARTTOK("") AST_ENDTOK("End") AST_STRIP_UNDERSCORES;
typedef struct WorldChangeNemesisStateActionProperties
{
	NemesisState eNewNemesisState;						AST( NAME("NewNemesisState"))
} WorldChangeNemesisStateActionProperties;

AUTO_STRUCT AST_STARTTOK("") AST_ENDTOK("End") AST_STRIP_UNDERSCORES AST_FIXUPFUNC( fixupWorldWarpActionProperties );
typedef struct WorldWarpActionProperties
{
	WorldVariableDef warpDest;							AST( NAME("WarpDest") STRUCT( parse_WorldVariableDef ))
	char *pcOldMapName;									AST( NAME("MapName") )
	char *pcOldSpawnTargetName;							AST( NAME("SpawnTargetName") )
	
	REF_TO(DoorTransitionSequenceDef) hTransSequence;	AST( NAME("TransitionOverride") )

	WorldVariableDef **eaVariableDefs;					AST( NAME("VariableDef") )
	WorldVariable **eaOldVariables;						AST( NAME("Variable") )
	bool bIncludeTeammates;								AST( NAME("IncludeTeammates") )
	bool bSinglePlayer;									AST( NAME("SinglePlayer") )
} WorldWarpActionProperties;

AUTO_STRUCT AST_STARTTOK("") AST_ENDTOK("End") AST_STRIP_UNDERSCORES;
typedef struct WorldContactActionProperties
{
	REF_TO(ContactDef) hContactDef;						AST( NAME("ContactDef") REFDICT(Contact) )
	char *pcDialogName;									AST( NAME("DialogName") )
} WorldContactActionProperties;

AUTO_STRUCT AST_STARTTOK("") AST_ENDTOK("End") AST_STRIP_UNDERSCORES;
typedef struct WorldExpressionActionProperties
{
	Expression *pExpression;							AST( NAME("ExprBlock") LATEBIND )
} WorldExpressionActionProperties;

AUTO_STRUCT AST_STARTTOK("") AST_ENDTOK("End") AST_STRIP_UNDERSCORES;
typedef struct WorldGuildStatUpdateActionProperties
{
	// The name of the stat
	const char* pchStatName;							AST(NAME("StatName") POOL_STRING)

	// The operation to be applied to the stat (GuildStatUpdateOperation)
	S32 eOperation;										AST(NAME("Operation"))

	// The value for the operation
	S32 iValue;											AST(NAME("Value"))
} WorldGuildStatUpdateActionProperties;

AUTO_STRUCT AST_STARTTOK("") AST_ENDTOK("End") AST_STRIP_UNDERSCORES;
typedef struct WorldGuildThemeSetActionProperties
{
	// The name of the theme
	const char* pchThemeName;							AST(NAME("ThemeName") POOL_STRING)

} WorldGuildThemeSetActionProperties;

AUTO_STRUCT AST_STARTTOK("") AST_ENDTOK("End") AST_STRIP_UNDERSCORES;
typedef struct WorldItemAssignmentActionProperties
{
	// The ItemAssignmentDef
	REF_TO(ItemAssignmentDef) hAssignmentDef;			AST(NAME("AssignmentName"))

	// The operation to be applied to the ItemAssignment (ex: Add, Remove)
	S32 eOperation;										AST(NAME("Operation"))
} WorldItemAssignmentActionProperties;

AUTO_STRUCT AST_STARTTOK("") AST_ENDTOK("End") AST_STRIP_UNDERSCORES;
typedef struct WorldGameActionProperties
{
	WorldGameActionType eActionType;					AST( NAME("ActionType") )

	// Mission Actions
	WorldGrantMissionActionProperties *pGrantMissionProperties;
	WorldGrantSubMissionActionProperties *pGrantSubMissionProperties;
	WorldMissionOfferActionProperties *pMissionOfferProperties;
	WorldDropMissionActionProperties *pDropMissionProperties;

	// Item Actions
	WorldGiveItemActionProperties *pGiveItemProperties;
	WorldTakeItemActionProperties *pTakeItemProperties;
	WorldGiveDoorKeyItemActionProperties *pGiveDoorKeyItemProperties;

	// E-mail Actions
	WorldNPCSendEmailActionProperties *pNPCSendEmailProperties;

	// Variable Actions
	WorldShardVariableActionProperties *pShardVariableProperties;

	// Game Account Data Actions
	WorldGADAttribValueActionProperties *pGADAttribValueProperties;

	// Champions Nemesis Actions
	WorldChangeNemesisStateActionProperties *pNemesisStateProperties;

	// Activity Logging
	WorldActivityLogActionProperties *pActivityLogProperties;

	// Non-transactional actions are listed below.  These are generally
	// for notifying or interacting with the player

	// Player Notification Actions
	WorldSendFloaterActionProperties *pSendFloaterProperties;
	WorldSendNotificationActionProperties *pSendNotificationProperties;
	WorldContactActionProperties *pContactProperties;

	// Move a player
	WorldWarpActionProperties *pWarpProperties;

	// General escape valve for other non-transactional things to do
	WorldExpressionActionProperties *pExpressionProperties;

	// Guild stat updates
	WorldGuildStatUpdateActionProperties *pGuildStatUpdateProperties;

	// Guild theme set action
	WorldGuildThemeSetActionProperties *pGuildThemeSetProperties;

	// ItemAssignment updates
	WorldItemAssignmentActionProperties *pItemAssignmentProperties;

} WorldGameActionProperties;

AUTO_STRUCT AST_STARTTOK("") AST_ENDTOK("End") AST_STRIP_UNDERSCORES;
typedef struct WorldGameActionBlock
{
	WorldGameActionProperties **eaActions;				AST( NAME("Action") )
} WorldGameActionBlock;

AUTO_STRUCT AST_STARTTOK("") AST_ENDTOK("End") AST_STRIP_UNDERSCORES;
typedef struct WorldActionInteractionProperties
{
	// --- These properties apply to nodes, encounter actors, and volumes ---

	Expression *pAttemptExpr;							AST( NAME("AttemptExprBlock") REDUNDANT_STRUCT("AttemptExpr", parse_Expression_StructParam) LATEBIND )
	Expression *pSuccessExpr;							AST( NAME("SuccessExprBlock") REDUNDANT_STRUCT("SuccessExpr", parse_Expression_StructParam) LATEBIND )
	Expression *pFailureExpr;							AST( NAME("FailureExprBlock") REDUNDANT_STRUCT("FailureExpr", parse_Expression_StructParam) LATEBIND )
	Expression *pInterruptExpr;							AST( NAME("InterruptExprBlock") REDUNDANT_STRUCT("InterruptExpr", parse_Expression_StructParam) LATEBIND )
	Expression *pNoLongerActiveExpr;					AST( NAME("NoLongerActiveExprBlock") REDUNDANT_STRUCT("NoLongerActiveExpr", parse_Expression_StructParam) LATEBIND )
	Expression *pCooldownExpr;							AST( NAME("CooldownExprBlock") REDUNDANT_STRUCT("CooldownExpr", parse_Expression_StructParam) LATEBIND )

	WorldGameActionBlock successActions;				AST( NAME("SuccessActions") STRUCT(parse_WorldGameActionBlock) )
	WorldGameActionBlock failureActions;				AST( NAME("FailureActions") STRUCT(parse_WorldGameActionBlock) )
} WorldActionInteractionProperties;

AUTO_STRUCT AST_STARTTOK("") AST_ENDTOK("End") AST_STRIP_UNDERSCORES;
typedef struct WorldSoundInteractionProperties
{
	// --- These properties apply to nodes, encounter actors, and volumes ---

	char *pchAttemptSound;						AST( NAME("AttemptSound") )
	char *pchSuccessSound;						AST( NAME("SuccessSound") )
	char *pchFailureSound;						AST( NAME("FailureSound") )
	char *pchInterruptSound;					AST( NAME("InterruptSound") )

	// --- These properties apply to nodes
	char *pchMovementTransStartSound;			AST( NAME("MovementTransStartSound") )
	char *pchMovementTransEndSound;				AST( NAME("MovementTransEndSound") )
	char *pchMovementReturnStartSound;			AST( NAME("MovementReturnStartSound") )
	char *pchMovementReturnEndSound;			AST( NAME("MovementReturnEndSound") )

} WorldSoundInteractionProperties;

AUTO_STRUCT AST_STARTTOK("") AST_ENDTOK("End") AST_STRIP_UNDERSCORES;
typedef struct WorldInteractLocationProperties
{
	const char	**eaAnims;				AST( NAME("Anim") ADDNAMES("JobAnim") POOL_STRING)

	REF_TO(FSM) hFsm;					AST( NAME("FSM") )
	// Combat job locations use the field below as the combat FSM
	REF_TO(FSM) hSecondaryFsm;			AST( NAME("SecondaryFSM") NAME("CombatFSM") )
	Expression *pIgnoreCond;			AST( NAME("IgnoreConditionBlock") REDUNDANT_STRUCT("IgnoreCondition", parse_Expression_StructParam) LATEBIND )
	S32 iPriority;						AST( NAME("Priority"))

	Vec3 vPos;							AST( NAME("Position"))
	Quat qOrientation;					AST( NAME("Orientation"))
	
} WorldInteractLocationProperties;

AUTO_STRUCT AST_STARTTOK("") AST_ENDTOK("End") AST_STRIP_UNDERSCORES;
typedef struct WorldAmbientJobInteractionProperties
{
	S32 iPriority;						AST(NAME("Priority"))

	U32 isForCitters : 1;				AST(NAME("isForCritters") DEFAULT(1))
	U32 isForCivilians : 1;				AST(NAME("isForCivilians") DEFAULT(1))

	U32 initialJob : 1;					AST(ADDNAMES("initialJob") DEFAULT(0))
} WorldAmbientJobInteractionProperties;

AUTO_STRUCT AST_STARTTOK("") AST_ENDTOK("End") AST_STRIP_UNDERSCORES;
typedef struct WorldAnimationInteractionProperties
{
	// --- These properties apply to nodes, encounter actors, and volumes ---

	// The AnimList for the player to execute while performing the interaction
	REF_TO(AIAnimList) hInteractAnim;					AST( NAME("InteractAnim") REFDICT(AIAnimList))
} WorldAnimationInteractionProperties;

AUTO_STRUCT AST_STARTTOK("") AST_ENDTOK("End") AST_STRIP_UNDERSCORES;
typedef struct WorldChildInteractionProperties
{
	// --- These properties apply only to nodes ---

	int iChildIndex;									AST( NAME("ChildIndex") )
	Expression *pChildSelectExpr;						AST( NAME("ChildSelectExpressionBlock") REDUNDANT_STRUCT("ChildSelectExpression", parse_Expression_StructParam) LATEBIND )
} WorldChildInteractionProperties;

AUTO_STRUCT AST_STARTTOK("") AST_ENDTOK("End") AST_STRIP_UNDERSCORES;
typedef struct WorldContactInteractionProperties
{
	// --- These properties apply to nodes encounter actors, and volumes ---

	REF_TO(ContactDef) hContactDef;						AST( NAME("ContactDef") REFDICT(Contact) )
	const char *pcDialogName;							AST( NAME("DialogName") POOL_STRING )
} WorldContactInteractionProperties;

AUTO_STRUCT AST_STARTTOK("") AST_ENDTOK("End") AST_STRIP_UNDERSCORES;
typedef struct WorldCraftingInteractionProperties
{
	// --- These properties apply to nodes, encounter actors, and volumes ---

	WorldSkillType eSkillFlags;							AST( NAME("SkillTypes") )
	int iMaxSkill;										AST( NAME("MaxSkill") )
	REF_TO(RewardTable) hCraftRewardTable;				AST( NAME("CraftRewardTable") REFDICT(RewardTable) )
	REF_TO(RewardTable) hDeconstructRewardTable;		AST( NAME("DeconstructRewardTable") REFDICT(RewardTable) )
	REF_TO(RewardTable) hExperimentRewardTable;			AST( NAME("ExperimentRewardTable") REFDICT(RewardTable) )
} WorldCraftingInteractionProperties;

AUTO_STRUCT AST_STARTTOK("") AST_ENDTOK("End") AST_STRIP_UNDERSCORES;
typedef struct WorldDestructibleInteractionProperties
{
	// --- These properties apply only to nodes ---

	// Information on the critter to spawn
	REF_TO(CritterDef) hCritterDef;						AST( NAME("CritterDef") REFDICT(CritterDef) )
	REF_TO(CritterOverrideDef) hCritterOverrideDef;	AST( NAME("CritterOverrideDef") REFDICT(CritterOverrideDef) )
	U32 uCritterLevel;									AST( NAME("CritterLevel") )
	DisplayMessage displayNameMsg;						AST( NAME("DisplayNameMsg") STRUCT(parse_DisplayMessage) )

	// The entity's name for purposes of logging and internal tracking
	char *pcEntityName;									AST( NAME("EntityName") )

	// The destructible's respawn time
	F32 fRespawnTime;

	// Power performed on death
	REF_TO(PowerDef) hOnDeathPowerDef;					AST( NAME("OnDeathPower") )
} WorldDestructibleInteractionProperties;

AUTO_STRUCT AST_STARTTOK("") AST_ENDTOK("End") AST_STRIP_UNDERSCORES AST_FIXUPFUNC(fixupWorldDoorInteractionProperties);
typedef struct WorldDoorInteractionProperties
{
	// --- These properties apply to nodes, encounter actors, and volumes ---

	WorldDoorType eDoorType;							AST( NAME("DoorType") DEFAULT(WorldDoorType_MapMove) )

	// If eDoorType is QueuedInstance, then the hQueueDef is populated
	REF_TO(QueueDef) hQueueDef;							AST( NAME("QueueDef") REFDICT(QueueDef) )

	// If eDoorType is MapMove, then these are available
	WorldVariableDef doorDest;							AST( NAME("DoorDest"), STRUCT(parse_WorldVariableDef))
	WorldVariable **eaOldVariables;						AST( NAME("Variable") )
	WorldVariableDef **eaVariableDefs;					AST( NAME("VariableDef") )
	bool bPerPlayer;									AST( NAME("PerPlayer") )
	bool bSinglePlayer;									AST( NAME("SinglePlayer") )
	bool bIncludeTeammates;								AST( NAME("IncludeTeammates") )
	bool bCollectDestStatus;							AST( NAME("CollectDestinationStatus") )
    bool bDestinationSameOwner;							AST( NAME("DestinationSameOwner") )  // If true then request destination map with the same owner as the current map.

	// Old format for doorDest
	bool bOldUseChoiceTable;							AST( NAME("UseChoiceTable") )
	char *pcOldMapName;									AST( NAME("MapName") )
	char *pcOldSpawnTargetName;							AST( NAME("SpawnTargetName") )
	REF_TO(ChoiceTable) hOldChoiceTable;				AST( NAME("ChoiceTable") )
	char *pcOldChoiceName;								AST( NAME("ChoiceName") )

	// These are always available
	REF_TO(DoorTransitionSequenceDef) hTransSequence;	AST( NAME("TransitionOverride") )
	const char *pcDoorIdentifier;						AST( NAME("DoorIdentifier") POOL_STRING )

	// If this door has a key associated with it
	char* pcDoorKey;									AST( NAME("DoorKey") )

	// --- These properties apply only to nodes ---

	// Motion properties
	bool bCustomMotion;									AST( NAME("CustomMotion") )
	Vec3 vRot;											AST( NAME("Rotation") )
	Vec3 vPos;											AST( NAME("Postion") )
	F32 fTime;											AST( NAME("TranslationTime") )
} WorldDoorInteractionProperties;

AUTO_STRUCT AST_STARTTOK("") AST_ENDTOK("End") AST_STRIP_UNDERSCORES;
typedef struct WorldGateInteractionProperties
{
	// --- These properties apply to nodes ---

	Expression *pCritterUseCond;						AST( NAME("CritterUseConditionBlock") REDUNDANT_STRUCT("CritterUseCondition", parse_Expression_StructParam) LATEBIND )
	bool bVolumeTriggered;								AST( NAME("VolumeTriggered") )
	bool bStartState;									AST( NAME("StartState") )

} WorldGateInteractionProperties;

AUTO_STRUCT AST_STARTTOK("") AST_ENDTOK("End") AST_STRIP_UNDERSCORES;
typedef struct WorldChairInteractionProperties
{
	// --- These properties apply to nodes ---

	const char **eaBitHandlesPre;						AST( POOL_STRING NAME("BitHandlesPre") )
	const char **eaBitHandlesHold;						AST( POOL_STRING NAME("BitHandlesHold") )
	float fTimeToMove;									AST( NAME("TimeToMove") )
	float fTimePostHold;								AST( NAME("TimePostHold") )

} WorldChairInteractionProperties;

AUTO_STRUCT AST_STARTTOK("") AST_ENDTOK("End") AST_STRIP_UNDERSCORES;
typedef struct WorldRewardInteractionProperties
{
	// --- These properties apply to nodes, encounter actors, and volumes ---
	
	REF_TO(RewardTable) hRewardTable;					AST( NAME("RewardTable") REFDICT(RewardTable) )
	REF_TO(RewardTable) hOnceOnlyRewardTable_Deprecated;	AST( NAME("OnceOnlyRewardTable") REFDICT(RewardTable) )
	
	WorldRewardLevelType eRewardLevelType;				AST( NAME("RewardLevelType") )
	U32 uCustomRewardLevel;								AST( NAME("CustomRewardLevel") )
	const char *pcMapVarName;							AST( NAME("MapVarName") )
	
} WorldRewardInteractionProperties;

AUTO_STRUCT AST_STARTTOK("") AST_ENDTOK("End") AST_STRIP_UNDERSCORES;
typedef struct WorldMoveDescriptorProperties
{
	int iStartChildIdx;										AST( NAME("StartChildIndex") )
	int iDestChildIdx;										AST( NAME("DestChildIndex") )
	Vec3 vDestPos;											AST( NAME("Position") )
	Vec3 vDestRot;											AST( NAME("Rotation") )
} WorldMoveDescriptorProperties;

AUTO_STRUCT AST_STARTTOK("") AST_ENDTOK("End") AST_STRIP_UNDERSCORES;
typedef struct WorldMotionInteractionProperties
{
	// --- These properties apply only to nodes ---

	WorldMoveDescriptorProperties **eaMoveDescriptors;		AST( NAME("MoveDescriptor") )
	F32 fTransitionTime;									AST( NAME("TransitionTime") )
	F32 fDestinationTime;									AST( NAME("DestinationTime") )
	F32 fReturnTime;										AST( NAME("ReturnTime") )
	bool bTransDuringUse;									AST( NAME("TransDuringUse") )

} WorldMotionInteractionProperties;

AUTO_STRUCT AST_STARTTOK("") AST_ENDTOK("End") AST_STRIP_UNDERSCORES;
typedef struct WorldTextInteractionProperties
{
	// --- These properties apply to nodes, encounter actors, and volumes ---

	// The text to show on the interaction option if it's interactable but has usability requirements. (e.g. "Needs Key or Thievery", "Requires Sembia Rep")
	DisplayMessage usabilityOptionText;					AST( NAME("UsabilityText") STRUCT(parse_DisplayMessage) )
	
	// The text to show on the interaction option. (e.g. "Talk To", or "Pick up")
	// If not set, default text is used
	DisplayMessage interactOptionText;					AST( NAME("InteractText") STRUCT(parse_DisplayMessage) )

	// Text that can be used by game UIs for auxiliary information about this interaction
	DisplayMessage interactDetailText;					AST( NAME("InteractDetailText") STRUCT(parse_DisplayMessage) )

	// A texture that can be used by game UIs in conjunction with this interaction option
	const char *interactDetailTexture;					AST( NAME("InteractDetailTexture") POOL_STRING )

	// Text sent to the console on success
	DisplayMessage successConsoleText;					AST( NAME("SuccessConsoleText") STRUCT(parse_DisplayMessage) )

	// Text sent to the console on failure
	DisplayMessage failureConsoleText;					AST( NAME("FailureConsoleText") STRUCT(parse_DisplayMessage) )
} WorldTextInteractionProperties;

AUTO_STRUCT AST_STARTTOK("") AST_ENDTOK("End") AST_STRIP_UNDERSCORES;
typedef struct WorldTimeInteractionProperties
{
	// --- These properties apply to nodes, encounter actors, and volumes ---

	// The time (in secs) required to perform interaction.  0 means instantly.
	F32 fUseTime;										AST( NAME("UseTime") )

	// Text to display for the interact use timer (e.g. "Interacting..."). Only valid if fUseTime is set.
	DisplayMessage msgUseTimeText;						AST( NAME("UseTimeText") STRUCT(parse_DisplayMessage) )

	// The time (in secs) the interaction is active before cooldown starts.  0 means no time.
	F32 fActiveTime;									AST( NAME("ActiveTime") )

	// The "custom_cooldown_time" only applies if the cooldown_time is Custom.
	// A value of zero means no cooldown.
	WorldCooldownTime eCooldownTime;					AST( NAME("CooldownTime"))
	F32 fCustomCooldownTime;							AST( NAME("CustomCooldownTime") )

	// Whether this interactable should adjust its cooldown time based on its surroundings
	WorldDynamicSpawnType	eDynamicCooldownType;		AST( NAME("DynamicCooldownType") )

	// If the use time is greater than zero, interrupt use on the following cases
	U32 bInterruptOnPower  : 1;							AST( NAME("InterruptOnPower") )
	U32 bInterruptOnDamage : 1;							AST( NAME("InterruptOnDamage") )
	U32 bInterruptOnMove   : 1;							AST( NAME("InterruptOnMove") )

	// If true, the interaction should never respawn
	// Setting this true on a non-instanced map is a bad idea
	U32 bNoRespawn          : 1;						AST( NAME("NoRespawn") )

	// --- These properties apply only to nodes ---

	// If true, teammates can still interact with this while it is in the active state
	U32 bTeamUsableWhenActive : 1;						AST( NAME("TeamUsableWhenActive") )

	// If true, the geometry should hide during cooldown
	bool bHideDuringCooldown;							AST( NAME("HideDuringCooldown") )

} WorldTimeInteractionProperties;

AUTO_STRUCT AST_STARTTOK("") AST_ENDTOK("End") AST_IGNORE(InteractionCategory) AST_STRIP_UNDERSCORES;
typedef struct WorldInteractionPropertyEntry
{
	// --- These properties apply to nodes, encounter actors, and volumes ---

	// This is the type of interaction
	// Values: CLICKABLE, CONTACT, CRAFTINGSTATION, DESTRUCTIBLE, DOOR, FROMDEFINITION, NAMEDOBJECT, GATE, CHAIR
	const char *pcInteractionClass;						AST( NAME("InteractionClass") POOL_STRING )

	// This is set for "FROMDEFINITION" type
	REF_TO(InteractionDef) hInteractionDef;				AST( NAME("InteractionDef") )

	// When "FROMDEFINITION" which top-level fields are overridden
	U32 bOverrideInteract : 1;							AST( NAME("OverrideInteract") )         // pInteractCond + pSuccessCond + pAttemptableCond
	U32 bOverrideVisibility : 1;						AST( NAME("OverrideVisibility") )       // pVisibleExpr + bEvalVisExprPerEnt
	U32 bOverrideCategoryPriority : 1;					AST( NAME("OverrideCategoryPriority") ) // pcCategory + iPriority + pcInteractionTypeTag

	// Determines whether this interaction is exclusive or not (UseExclusionFlag must be set before the exclusive flag is actually applied)
	U32 bExclusiveInteraction : 1;
	U32 bUseExclusionFlag : 1;

	// Makes this interaction auto-execute
	U32 bAutoExecute : 1;

	// This will prevent powers from being interrupted when interacting with this entry
	U32 bDisablePowersInterrupt : 1;

	// Allow this interactable to be used during combat
	U32 bAllowDuringCombat : 1;

	// When this is true, interaction should be offered.  When false, no interaction allowed.
	Expression *pInteractCond;							AST( NAME("InteractConditionBlock") REDUNDANT_STRUCT("InteractCondition", parse_Expression_StructParam) LATEBIND )
	// When this is true, we are allowed to try interacting. When false, interact bar will short-circuit and the fail message will display.
	//  an alternate interact FX will be shown and a different tooltip message will be shown.
	Expression *pAttemptableCond;						AST( NAME("AttemptableConditionBlock") REDUNDANT_STRUCT("AttemptableCondition", parse_Expression_StructParam) LATEBIND )
	
	// When this is true, interaction is successful, otherwise it fails
	Expression *pSuccessCond;							AST( NAME("SuccessConditionBlock") REDUNDANT_STRUCT("SuccessCondition", parse_Expression_StructParam) LATEBIND )

	// The category of the interaction (if any).  See WorldOptionalActionCategoryDef
	const char *pcCategoryName;							AST( NAME("Category") POOL_STRING )

	// The priority of the interaction (if any).  Defaults to "Normal".
	WorldOptionalActionPriority iPriority;				AST( NAME("Priority") DEF(5) )

	// This is used to specify time values for the interaction
	WorldTimeInteractionProperties *pTimeProperties;

	// This is used to specify expression actions to perform during interaction
	WorldActionInteractionProperties *pActionProperties;	AST( NAME("ActionProperties" ) )

	// This is used to specify animations and FX for the interaction
	WorldAnimationInteractionProperties *pAnimationProperties;

	// This is used to specify sounds played from the interactable during this interaction
	WorldSoundInteractionProperties *pSoundProperties;

	// This is used to specify text applies to the interaction
	WorldTextInteractionProperties *pTextProperties;

	// This is used to specify rewards given on successful interact
	WorldRewardInteractionProperties *pRewardProperties;

	// This is used to specify custom geometry motion during interaction
	WorldMotionInteractionProperties *pMotionProperties;

	// This is only set for the "CONTACT" type
	WorldContactInteractionProperties *pContactProperties;

	// This is only set for the "CRAFTINGSTATION" type
	WorldCraftingInteractionProperties *pCraftingProperties;

	// This is only set for the "DOOR" type
	WorldDoorInteractionProperties *pDoorProperties;

	// This is set for the "AMBIENTJOB" and "COMBATJOB" type
	WorldAmbientJobInteractionProperties *pAmbientJobProperties; 

	// This is only set for the "GATE" type
	WorldGateInteractionProperties *pGateProperties;

	// This is only set for the "CHAIR" type
	WorldChairInteractionProperties *pChairProperties;

	// --- These properties apply only to nodes ---

	// When this is true, the geometry should be visible
	Expression *pVisibleExpr;							AST( NAME("VisibleExpressionBlock") REDUNDANT_STRUCT("VisibleExpression", parse_Expression_StructParam) LATEBIND )

	// This is only set for the "DESTRUCTIBLE" type
	WorldDestructibleInteractionProperties *pDestructibleProperties;

} WorldInteractionPropertyEntry;

AUTO_STRUCT AST_STARTTOK("") AST_ENDTOK("End") AST_STRIP_UNDERSCORES;
typedef struct WorldInteractionProperties
{
	// --- These properties apply to both nodes and encounter actors ---

	// The distance the player must be from this object in order to interact with it
	U32 uInteractDist;									AST( NAME("InteractDist") )
	U32 uInteractDistCached;							NO_AST // Only nodes set this. Actors track it in EncounterActorProperties


	// --- These properties apply only to nodes ---

	// This is used to specify a child geometry that is the visible component of the interaction
	// It is NULL if the whole geometry is applicable
	// This is shared by all interaction properties for the group
	WorldChildInteractionProperties *pChildProperties;

	WorldInteractLocationProperties **peaInteractLocations;	AST( NAME("InteractLocation") )

	// Set to true to allow the node to be hidden using an expression
	U32 bAllowExplicitHide : 1;							AST( NAME("AllowExplicitHide") )

	// Set to true to start the node hidden
	U32 bStartsHidden : 1;								AST( NAME("StartsHidden") )

	// When this is true, the geometry is not visible on the server, but
	// it may be visible on client with visible expression evaluated per entity
	U32 bEvalVisExprPerEnt : 1;							AST( NAME("EvalVisPerEnt") )

	// Can the player use tab-selection to target this node
	U32 bTabSelect : 1;									AST( NAME("TabSelect") )

	// Decides whether or not this node should be added to the targetable nodes list and given a reticle
	U32 bUntargetable : 1;								AST( NAME("Untargetable") )

	// Stores whether this node's interact dist or target dist are beyond the cutoff, so the
	// node should be put in the global list to always check against
	U32 bPastDistCutoff : 1;							NO_AST

	// The distance the player must be from this object in order for it to receive targeting info/be clicked on
	U32 uTargetDist;									AST( NAME("TargetDist") )
	U32 uTargetDistCached;								NO_AST

	DisplayMessage displayNameMsg;						AST( NAME("DisplayNameMsg") STRUCT(parse_DisplayMessage) )

	DisplayMessage overrideDisplayNameMsg;				AST( NAME("OverrideDisplayNameMsg") STRUCT(parse_DisplayMessage) )

	Expression *pOverrideDisplayNameExpr;				AST( NAME("OverrideDisplayNameExpr") LATEBIND )

	// This is a generic tag to describe the interaction; used by AI and for overriding
	char **eaInteractionTypeTag;						AST( NAME("InteractionTypeTag") POOL_STRING )
	
	const char* pchOverrideFX;							AST( NAME("OverrideFX") POOL_STRING )

	// If specified, override the default FX for this node
	const char* pchAdditionalUniqueFX;					AST( NAME("AdditionalUniqueFX") POOL_STRING )

	// --- These properties apply to nodes, encounter actors, and volumes ---

	WorldInteractionPropertyEntry **eaEntries;			AST( NAME("Entry") )

} WorldInteractionProperties;

// This is the subset of interaction properties available on the client
AUTO_STRUCT AST_STARTTOK("") AST_ENDTOK("End") AST_STRIP_UNDERSCORES;
typedef struct WorldBaseInteractionProperties
{
	// NOTE: CHANGING ANY ASPECT OF THE PARSE TABLE OF THIS STRUCTURE 
	//       WILL CAUSE ALL MAPS TO RE-BIN.  DON'T CHANGE THIS WITHOUT APPROVAL.

	// The class may have more than one bit set
	U32 eInteractionClass;								AST( NAME("InteractionClass") )

	// Display name to use if destructible
	REF_TO(Message) hDisplayNameMsg;					AST( NAME("DisplayNameMsg") REFDICT(Message) )	

	// If specified, override the default FX for this node
	const char* pchOverrideFX;							AST( NAME("OverrideFX") POOL_STRING )

	// If specified, override the default FX for this node
	const char* pchAdditionalUniqueFX;					AST( NAME("AdditionalUniqueFX") POOL_STRING )

	// Set to true to start the node hidden
	U32 bStartsHidden : 1;

	// Does this node evaluate visibilty per entity?
	U32 bVisiblePerEnt : 1;

	// Can the player use tab-selection to target this entity?
	U32 bTabSelect : 1;

	// The farthest interaction distace for all the interaction properties
	U32 uInteractDist;

	// The farthest targetable distance for this object
	U32 uTargetDist;
} WorldBaseInteractionProperties;


AUTO_STRUCT AST_STARTTOK("") AST_ENDTOK("End") AST_STRIP_UNDERSCORES AST_IGNORE("AerosolAtmosphereThickness") AST_IGNORE("RayleighScattering") AST_IGNORE("MieScattering") AST_IGNORE("MiePhaseAsymmetry") AST_IGNORE("IntensityMultiplier") AST_IGNORE("ParticleHSV") AST_IGNORE("AerosolParticleHSV") AST_IGNORE("NormalizedViewHeight");
typedef struct WorldAtmosphereProperties
{
	F32 planet_radius;									AST( NAME("PlanetRadius") )
	F32 atmosphere_thickness;							AST( NAME("AtmosphereThickness") )
} WorldAtmosphereProperties;
extern ParseTable parse_WorldAtmosphereProperties[];
#define TYPE_parse_WorldAtmosphereProperties WorldAtmosphereProperties

AUTO_STRUCT AST_STARTTOK("") AST_ENDTOK("End") AST_STRIP_UNDERSCORES AST_IGNORE("AerosolAtmosphereThickness") AST_IGNORE("RayleighScattering") AST_IGNORE("MieScattering") AST_IGNORE("MiePhaseAsymmetry") AST_IGNORE("IntensityMultiplier") AST_IGNORE("ParticleHSV") AST_IGNORE("AerosolParticleHSV") AST_IGNORE("NormalizedViewHeight");
typedef struct WorldPlanetProperties
{
	F32 geometry_radius;								AST( NAME("GeometryRadius") )
	F32 collision_radius;								AST( NAME("CollisionRadius") )

	bool has_atmosphere;								AST( NAME("HasAtmosphere") )
	WorldAtmosphereProperties atmosphere;				AST( EMBEDDED_FLAT )
} WorldPlanetProperties;

AUTO_STRUCT AST_STARTTOK("") AST_ENDTOK("End") AST_STRIP_UNDERSCORES;
typedef struct WorldSkyVolumeProperties
{
	SkyInfoGroup sky_group;								AST( EMBEDDED_FLAT NAME(SkyGroup))
	F32 weight;											AST( NAME("SkyWeight") )
	F32 fade_in_rate;									AST( NAME("FadeInRate") )
	F32 fade_out_rate;									AST( NAME("FadeOutRate") )
	bool positional_fade;								AST( NAME("PositionalFade") )
} WorldSkyVolumeProperties;

AUTO_STRUCT AST_STARTTOK("") AST_ENDTOK("End") AST_STRIP_UNDERSCORES;
typedef struct WorldSoundVolumeProperties
{
	const char *event_name;								AST( NAME("EventName") POOL_STRING )
	const char *event_name_override_param;				AST( NAME("EventNameOverrideParam") POOL_STRING )
	const char *music_name;								AST( NAME("MusicName") POOL_STRING )
	const char *dsp_name;								AST( NAME("DSPName") POOL_STRING )
	const char *dsp_name_override_param;				AST( NAME("DSPNameOverrideParam") POOL_STRING )

	F32 multiplier;										AST( NAME("Multiplier") )
	F32 priority;										AST( NAME("Priority") )
} WorldSoundVolumeProperties;

AUTO_STRUCT AST_STARTTOK("") AST_ENDTOK("End") AST_STRIP_UNDERSCORES;
typedef struct WorldSoundConnProperties
{
	F32 min_range;										AST( NAME("MinRange") )
	F32 max_range;										AST( NAME("MaxRange") )

	const char *dsp_name;								AST( NAME("DSPName") POOL_STRING )
} WorldSoundConnProperties;

AUTO_STRUCT AST_STARTTOK("") AST_ENDTOK("End") AST_STRIP_UNDERSCORES;
typedef struct WorldSoundSphereProperties
{
	const char *pcEventName;							AST( NAME("EventName") POOL_STRING )
	const char *pcDSPName;								AST( NAME("DSPName") POOL_STRING )
	F32 fMultiplier;									AST( NAME("Multiplier") DEFAULT(1) )
	F32 fPriority;										AST( NAME("Priority") )

	char *pcGroup;										AST( NAME("SoundGroup") )
	int iGroupOrd;										AST( NAME("SoundGroupOrd") )

	U32 bExclude : 1;									AST( NAME("Exclude") )

} WorldSoundSphereProperties;
extern ParseTable parse_WorldSoundSphereProperties[];
#define TYPE_parse_WorldSoundSphereProperties WorldSoundSphereProperties

AUTO_STRUCT AST_STARTTOK("") AST_ENDTOK("End") AST_STRIP_UNDERSCORES;
typedef struct WorldBuildingLayerProperties
{
	int height;											AST( NAME("Height") )
	REF_TO(GroupDef) group_ref;							AST( NAME("GroupRef") )
	U32 seed;											AST( NAME("Seed") )
	U32 seed_delta;										AST( NAME("SeedDelta") )
	GroupDef *group_def_cached;							NO_AST

	GroupDefRef group_deprecated;						AST( EMBEDDED_FLAT(Group) )
} WorldBuildingLayerProperties;

AUTO_STRUCT AST_STARTTOK("") AST_ENDTOK("End") AST_IGNORE("LODGroupName") AST_IGNORE("LODGroupNameUID") AST_STRIP_UNDERSCORES;
typedef struct WorldBuildingProperties
{
	WorldBuildingLayerProperties **layers;				AST( NAME("Layer") )

	bool				no_occlusion;					AST( NAME("DisableOcclusion") )
	bool				no_lod;							AST( NAME("DisableLOD") )
	REF_TO(GroupDef)	lod_group_ref;					AST( NAME("LODModelRef") )

	GroupDefRef			lod_model_deprecated;			AST( EMBEDDED_FLAT(LODModel) )
} WorldBuildingProperties;

AUTO_STRUCT AST_STARTTOK("") AST_ENDTOK("End") AST_IGNORE("Seed") AST_STRIP_UNDERSCORES;
typedef struct WorldDebrisFieldProperties
{
	REF_TO(GroupDef) group_ref;							AST( NAME("GroupRef") )
	F32 rand_offset;									AST( NAME("RandomOffset") )
	bool even_distb;									AST( NAME("EvenDistribution") )
	bool is_box;										AST( NAME("Box") )
	bool rotate;										AST( NAME("RandomRotation") )
	F32 density;										AST( NAME("Density") )
	F32 falloff;										AST( NAME("Falloff") )
	F32 center_occluder;								AST( NAME("CenterOccluder") )
	F32 *occluders;										AST( NAME("Occluders") )

	GroupDefRef group_deprecated;						AST( EMBEDDED_FLAT(Group) )
	
} WorldDebrisFieldProperties;

AUTO_STRUCT AST_STARTTOK("") AST_ENDTOK("End") AST_STRIP_UNDERSCORES;
typedef struct WorldAnimationProperties
{
	F32 time_offset;									AST( NAME("TimeOffset") ) // if this parameter is negative then it will be randomized

	Vec3 sway_angle;									AST( NAME("SwayAngle") )
	Vec3 sway_time;										AST( NAME("SwayTime") )

	Vec3 rotation_time;									AST( NAME("RotationTime") )

	Vec3 scale_amount;									AST( NAME("ScaleAmount") )
	Vec3 scale_time;									AST( NAME("ScaleTime") )

	Vec3 translation_amount;							AST( NAME("TranslationAmount") )
	Vec3 translation_time;								AST( NAME("TranslationTime") )
	bool translation_loop;								AST( NAME("TranslationLoop") )

	bool local_space;									AST( NAME("LocalSpace") )

} WorldAnimationProperties;

AUTO_STRUCT AST_STARTTOK("") AST_ENDTOK("End") AST_STRIP_UNDERSCORES;
typedef struct WorldCurve
{
	Spline spline;						AST( NAME("Spline") )			// Main spline
	bool terrain_exclusion;				AST( NAME("TerrainExcluder") )	// Creates excluder volumes for terrain objects
	bool terrain_filter;				AST( NAME("TerrainFilter") ) 	// Usable with the Terrain Path filter
	bool genesis_path;					AST( NAME("GenesisPath") ) 		// This is a path used to paint paths into the terrain
} WorldCurve;

AUTO_STRUCT AST_STRIP_UNDERSCORES;
typedef struct WorldCurveGap
{
	Vec3 position;						AST( NAME("Position") )
	F32 radius;							AST( NAME("Radius") )
	bool inherited;						AST( NAME("Inherited") )
} WorldCurveGap;

AUTO_STRUCT AST_STARTTOK("") AST_ENDTOK("End") AST_STRIP_UNDERSCORES;
typedef struct WorldCurveGaps
{
	WorldCurveGap **gaps;				AST( NAME("Gaps") )
} WorldCurveGaps;

AUTO_STRUCT AST_STARTTOK("") AST_ENDTOK("End") AST_STRIP_UNDERSCORES;
typedef struct WorldChildCurve
{
    // Geometry parameters
    bool deform;					AST( NAME("DeformGeometry") )
	bool normalize;					AST( NAME("NormalizeGeometry") DEFAULT(1) )
	F32 repeat_length;				AST( NAME("RepeatLength") DEFAULT(10000) )
	F32 curve_factor;				AST( NAME("CurveFactor") DEFAULT(1) )
	F32 stretch_factor;				AST( NAME("StretchFactor") DEFAULT(1) )	// How much is the geometry allowed to stretch? (0 = no stretch)
	bool attachable;				AST( NAME("Attachable") )
	bool linearize;					AST( NAME("Linearize") )
	S32 max_cps;					AST( NAME("MaxPointCount") DEFAULT(250) )
	F32 geo_length;					AST( NAME("GeometryLength") )

    // Curve Inheritance Parameters
	CurveChildType child_type;		AST( NAME("ChildType") )		// How the children are instanced
    bool avoid_gaps;				AST( NAME("AvoidGaps") DEFAULT(1) )		// Avoid gap overrides from parent
	bool reverse_curve;				AST( NAME("Reverse") )			// Reverses the order of the points and their tangents
	F32 begin_offset;				AST( NAME("BeginOffset") )		// Address [(IDX+1)*3+T] into the parent's control
                     												//   point array to begin the subcurve (0 = first point)
	F32 end_offset;					AST( NAME("EndOffset") )		// Address [(IDX+1)*3+T] into the parent's control
                     												//   point array to end the subcurve (0 = last point)
	F32 begin_pad;					AST( NAME("BeginPad") )			// Distance (in feet) to reserve before the child's
                     												//   first control point (of the subcurve)
	F32 end_pad;					AST( NAME("EndPad") )			// Distance (in feet) to reserve after the child's
                     												//   last control point (of the subcurve)
	bool reset_up;					AST( NAME("ResetUp") )			// Whether to reset all the up vectors to (0,1,0)
	F32 stretch;					AST( NAME("XScale") DEFAULT(1) )// Stretch factor for x axis
	F32 uv_rot;						AST( NAME("UVRotation") )		// Rotation of planar generated UVs
	F32 uv_scale;					AST( NAME("UVScale") )			// Uniform scale of planar generated UVs
	bool no_bounds_offset;			AST( NAME("NoBoundsOffset") )	// Disables automatic offset of rigid geometry so that 
																	// lower z-bound is at control point
	bool extra_point;				AST( NAME("ExtraPoint") )		// Adds an extra instance of a rigid geometry at the end.

	// TomY TODO
	//U8 occlusion_bits;				AST( NAME("OcclusionBits") )	// Bits to determine which occlusion volume sides
                     												//   active (0 = no volume)
	//F32 occlusion_radius;			AST( NAME("OcclusionRadius") )	// Size of occlusion volume
} WorldChildCurve;

AUTO_STRUCT AST_STARTTOK("") AST_ENDTOK("End") AST_STRIP_UNDERSCORES;
typedef struct WorldLODOverride
{
	AutoLODTemplate **lod_distances;					AST( NAME("LodDistance") )
} WorldLODOverride;

AUTO_STRUCT AST_STARTTOK("") AST_ENDTOK("End") AST_STRIP_UNDERSCORES;
typedef struct WorldEncounterHackProperties
{
	REF_TO(EncounterDef) base_def;						AST( NAME("BaseDef") REFDICT(EncounterDef) VITAL_REF ) // base definition of the encounter placed in the map (before modifications)
	F32 physical_radius;								AST( NAME("PhysicalSize") )
	F32 agro_radius;									AST( NAME("AgroSize") )
} WorldEncounterHackProperties;

AUTO_STRUCT;
typedef struct WorldActorCostumeProperties
{
	REF_TO(PlayerCostume) hCostume;						AST( NAME("Costume") )
	bool bHasErrored;									NO_AST
} WorldActorCostumeProperties;
extern ParseTable parse_WorldActorCostumeProperties[];
#define TYPE_parse_WorldActorCostumeProperties WorldActorCostumeProperties

AUTO_STRUCT;
typedef struct WorldActorCritterProperties
{
	REF_TO(CritterDef) hCritterDef;						AST( NAME("CritterDef") )
} WorldActorCritterProperties;
extern ParseTable parse_WorldActorCritterProperties[];
#define TYPE_parse_WorldActorCritterProperties WorldActorCritterProperties

AUTO_STRUCT;
typedef struct WorldActorFactionProperties 
{
	REF_TO(CritterFaction) hCritterFaction;				AST( NAME("Faction") )
} WorldActorFactionProperties;
extern ParseTable parse_WorldActorFactionProperties[];
#define TYPE_parse_WorldActorFactionProperties WorldActorFactionProperties

AUTO_STRUCT AST_STARTTOK("") AST_ENDTOK("End") AST_STRIP_UNDERSCORES;
typedef struct WorldActorProperties
{
	const char *pcName;									AST( POOL_STRING NAME("Name") )

	// World actor level FSM and FSM Vars
	U32 bOverrideFSM : 1;								AST( NAME("OverrideFSM"))
	REF_TO(FSM) hFSMOverride;							AST( NAME("FSM") )
	WorldVariableDef** eaFSMVariableDefs;				AST( NAME("VariableDefs"))

	// Interaction
	WorldInteractionProperties* pInteractionProperties;	AST( NAME("InteractionProperties") )

	// DisplayName
	DisplayMessage critterGroupDisplayNameMsg;			AST(NAME("CritterGroupDisplayName") STRUCT(parse_DisplayMessage))

	// DisplayName
	DisplayMessage displayNameMsg;						AST(NAME("DisplayName") STRUCT(parse_DisplayMessage))

	// DisplaySubName
	DisplayMessage displaySubNameMsg;					AST(NAME("DisplaySubName") STRUCT(parse_DisplayMessage))

	WorldActorCostumeProperties* pCostumeProperties;	AST( NAME("CostumeProperties") )

	// Overridden CritterDef
	WorldActorCritterProperties* pCritterProperties;	AST( NAME("CritterProperties") )

	// Overridden faction
	WorldActorFactionProperties *pFactionProperties;	AST( NAME("FactionProperties") )

	// Position
	Vec3 vPos;											AST( NAME("Position") )
	Vec3 vRot;											AST( NAME("Rotation") )

	// Used by runtime to cache data
	U32 uInteractDistCached;							NO_AST

	// The index of the template actor associated with this actor. Filled when bFillActorsInOrder is enabled
	int iActorIndex;									NO_AST
	bool bActorIndexSet;								NO_AST

	// for editing
	Mat4 draw_mat;										NO_AST
	U32 moving : 1;										NO_AST
	U32 selected : 1;									NO_AST
} WorldActorProperties;

extern ParseTable parse_WorldActorProperties[];
#define TYPE_parse_WorldActorProperties WorldActorProperties

AUTO_STRUCT AST_STARTTOK("") AST_ENDTOK("End") AST_STRIP_UNDERSCORES;
typedef struct WorldEncounterPointProperties
{
	const char *pcName;									AST( POOL_STRING NAME("Name") )

	Vec3 vPos;											AST( NAME("Position") )
	Vec3 vRot;											AST( NAME("Rotation") )

	// for editing
	Mat4 draw_mat;										NO_AST
	U32 moving : 1;										NO_AST
	U32 selected : 1;									NO_AST
} WorldEncounterPointProperties;
#define wlePatrolPointIsEndpoint(patrol, pointIdx) (eaSize(&patrol->patrol_points) == 1 || (pointIdx == eaSize(&patrol->patrol_points) - 1 && patrol->route_type == PATROL_ONEWAY))

extern ParseTable parse_WorldEncounterPointProperties[];
#define TYPE_parse_WorldEncounterPointProperties WorldEncounterPointProperties

AUTO_STRUCT AST_STARTTOK("") AST_ENDTOK("End") AST_STRIP_UNDERSCORES;
typedef struct WorldEncounterEventProperties
{
	// When this condition becomes true, the encounter succeeds
	Expression *pSuccessCond;							AST( NAME("SuccessCondition") LATEBIND )

	// When this condition becomes true, the encounter fails
	Expression *pFailureCond;							AST( NAME("FailureCondition") LATEBIND )

	// Expression to execute on success
	Expression *pSuccessExpr;							AST( NAME("SuccessAction") LATEBIND )

	// Expression to execute on failure
	Expression *pFailureExpr;							AST( NAME("FailureAction") LATEBIND )
} WorldEncounterEventProperties;

extern ParseTable parse_WorldEncounterEventProperties[];
#define TYPE_parse_WorldEncounterEventProperties WorldEncounterEventProperties

AUTO_STRUCT AST_STARTTOK("") AST_ENDTOK("End") AST_STRIP_UNDERSCORES;
typedef struct WorldEncounterSpawnProperties
{
	// This expression must be true for the encounter to spawn
	WorldEncounterSpawnCondType eSpawnCondType;			AST( NAME("SpawnConditionType") )
	Expression *pSpawnCond;								AST( NAME("SpawnCondition") LATEBIND )

	// The encounter will spawn when players enter this radius
	WorldEncounterRadiusType eSpawnRadiusType;			AST( NAME("SpawnRadiusType") )
	F32 fSpawnRadius;									AST( NAME("SpawnRadius") )

	// The respawn timer
	WorldEncounterTimerType eRespawnTimerType;			AST( NAME("RespawnTimerType") )
	F32 fRespawnTimer;									AST( NAME("RespawnTimer") )

	WorldEncounterDynamicSpawnType eDyamicSpawnType;	AST( NAME("DynamicSpawnType") )

	// After it is valid for the encounter to spawn, how long to wait before spawning
	F32 fSpawnDelay;									AST( NAME("SpawnDelay") )

	// The chance the encounter will spawn each time triggered
	F32 fSpawnChance;									AST( NAME("SpawnChance") DEF(100.0) )

	// Expression that, if true, will force despawn the encounter
	Expression *pDespawnCond;							AST( NAME("DespawnCondition") LATEBIND)

	EncounterWaveProperties *pWaveProps;				AST( NAME("WaveProperites") LATEBIND )

	// if non-zero, if any other spawned encounters are within the radius, it will not spawn the encounter
	F32 fLockoutRadius;									AST( NAME("LockoutRadius") DEF(0) )

	// Explicitly overwritten team size (if set, the encounter will always assume the player's team size is the value set here)
	WorldEncounterSpawnTeamSize eForceTeamSize;			AST( NAME("ForceTeamSize") )

	// type signifies for the use of the mastermind spawning system
	WorldEncounterMastermindSpawnType eMastermindSpawnType;	AST( NAME("MastermindSpawnType"))

	// currently used in conjunction with the mastermind spawning tiers
	const char *pcSpawnTag;								AST( NAME("SpawnTag") POOL_STRING)

	// The option to never despawn
	bool bNoDespawn;									AST( NAME("NoDespawn") )

	// The option to snap to ground
	bool bSnapToGround;									AST( NAME("SnapToGround") DEF(1) )

	// The amount to offset the encounter difficulty by
	S32 iDifficultyOffset;								AST( NAME("DifficultyOffset") )

	//Disables the delay on aggro when the encounter spawns
	bool bDisableAggroDelay;							AST( NAME("DisableAggroDelay"))
} WorldEncounterSpawnProperties;

extern ParseTable parse_WorldEncounterSpawnProperties[];
#define TYPE_parse_WorldEncounterSpawnProperties WorldEncounterSpawnProperties

AUTO_STRUCT AST_STARTTOK("") AST_ENDTOK("End") AST_STRIP_UNDERSCORES;
typedef struct WorldEncounterRewardProperties 
{
	WorldEncounterRewardType eRewardType;				AST( NAME("RewardType") )
	REF_TO(RewardTable) hRewardTable;					AST( NAME("RewardTable") )
	WorldEncounterRewardLevelType eRewardLevelType;		AST( NAME("RewardLevelType") )
	S32 iRewardLevel;									AST( NAME("RewardLevel") )
} WorldEncounterRewardProperties;
extern ParseTable parse_WorldEncounterRewardProperties[];
#define TYPE_parse_WorldEncounterRewardProperties WorldEncounterRewardProperties

AUTO_STRUCT AST_STARTTOK("") AST_ENDTOK("End") AST_STRIP_UNDERSCORES;
typedef struct WorldEncounterProperties
{
	// Which encounter template to base this encounter on
	REF_TO(EncounterTemplate) hTemplate;				AST( NAME("Template") )

	// Encounter spawn definition
	WorldEncounterSpawnProperties *pSpawnProperties;	AST( NAME("SpawnProperties") )

	// Success and failure conditions and actions
	WorldEncounterEventProperties *pEventProperties;	AST( NAME("EventProperties") )

	// Reward properties
	WorldEncounterRewardProperties *pRewardProperties;	AST( NAME("RewardProperties") )

	// The patrol route for the encounter
	char *pcPatrolRoute;								AST( NAME("PatrolRoute") )

	// Actors for the encounter
	WorldActorProperties **eaActors;					AST( NAME("Actor") )
	// If true, actors will be filled in the order they are specified on the encounter template, instead of by name
	bool bFillActorsInOrder;

	// Points for the encounter
	WorldEncounterPointProperties **eaPoints;			AST( NAME("Point") )

	// Actors in this encounter will become "not present" when this expression evaluates to false
	Expression *pPresenceCond;							AST( NAME("PresenceCondition") LATEBIND )

	// Actors in this encounter will use a non-default entity send distance
	F32 fOverrideSendDistance;							AST( NAME("OverrideSendDistance") )

} WorldEncounterProperties;

extern ParseTable parse_WorldEncounterProperties[];
#define TYPE_parse_WorldEncounterProperties WorldEncounterProperties

// This struct exists so the world layer dependency logic can check the template filename
// These lines up to "pcFilename" must match the EncounterTemplate struct in "encounter_common.h"
AUTO_STRUCT;
typedef struct WorldEncounterTemplateHeader
{
	const char *pcName;									AST( STRUCTPARAM KEY POOL_STRING )
	const char *pcScope;								AST( POOL_STRING SERVER_ONLY )
	const char *pcFilename;								AST( CURRENTFILE )
} WorldEncounterTemplateHeader;

AUTO_STRUCT;
typedef struct WorldOptionalActionCategoryDef
{
	const char *pcName;									AST( NAME("Name") POOL_STRING )
	const char *pcIcon;									AST( NAME("Icon") POOL_STRING )
	U32 uOverrideTargetDist;							AST( NAME("OverrideTargetDist") )
} WorldOptionalActionCategoryDef;

AUTO_STRUCT;
typedef struct WorldOptionalActionCategoryDefs
{
	WorldOptionalActionCategoryDef **eaCategories;		AST( NAME("OptionalActionCategory") )
} WorldOptionalActionCategoryDefs;

extern ParseTable parse_WorldOptionalActionCategoryDefs[];
#define TYPE_parse_WorldOptionalActionCategoryDefs WorldOptionalActionCategoryDefs

extern WorldOptionalActionCategoryDef **g_eaOptionalActionCategoryDefs;

AUTO_STRUCT AST_STARTTOK("") AST_ENDTOK("End") AST_STRIP_UNDERSCORES;
typedef struct WorldActionVolumeProperties
{
	Expression* entered_action_cond;					AST( NAME("EnteredActionCondBlock") REDUNDANT_STRUCT("EnteredActionCond", parse_Expression_StructParam) LATEBIND)
	Expression* entered_action;							AST( NAME("EnteredActionBlock") REDUNDANT_STRUCT("EnteredAction", parse_Expression_StructParam) LATEBIND)
	Expression* exited_action_cond;						AST( NAME("ExitedActionCondBlock") REDUNDANT_STRUCT("ExitedActionCond", parse_Expression_StructParam) LATEBIND)
	Expression* exited_action;							AST( NAME("ExitedActionBlock") REDUNDANT_STRUCT("ExitedAction", parse_Expression_StructParam) LATEBIND)

	// Note that GameActions are NOT here on purpose.  It is NOT okay to add any actions
	// to volumes that are not lightweight.  GameActions exist for heavyweight actions that
	// can cause transactions and such.
	//
	// The reason is that volume entry and exit are volatile and it's possible to flip flop
	// between states on consecutive frames for extended periods, causing a spam of events.
	// This is okay if what gets called off of volume entry/exit is super inexpensive.
	// 
	// If someone needs a heavyweight action on volume entry, they should consider having an
	// FSM watch for the volume, and change state once as appropriate, and do the action they
	// need to have done.
} WorldActionVolumeProperties;


extern ParseTable parse_WorldPowerVolumePartitionData[];
#define TYPE_parse_WorldPowerVolumePartitionData WorldPowerVolumePartitionData

AUTO_STRUCT AST_STARTTOK("") AST_ENDTOK("End") AST_STRIP_UNDERSCORES;
typedef struct WorldPowerVolumeProperties
{
	REF_TO(PowerDef) power;								AST( NAME("Power") REFDICT(PowerDef) )
	WorldPowerVolumeStrength strength;					AST( NAME("Strength") )
	U32 level;											AST( NAME("Level") )
	F32 repeat_time;									AST( NAME("RepeatTime") )
	Expression* trigger_cond;							AST( NAME("TriggerCondition") LATEBIND )
} WorldPowerVolumeProperties;

AUTO_STRUCT AST_STARTTOK("") AST_ENDTOK("End") AST_STRIP_UNDERSCORES AST_FIXUPFUNC(fixupWorldWarpVolumeProperties);
typedef struct WorldWarpVolumeProperties
{
	WorldVariableDef warpDest;							AST( NAME("WarpDest") STRUCT(parse_WorldVariableDef))

	Expression* warp_cond;								AST( NAME("WarpCondition") LATEBIND )

	// old format for dest
	bool oldUseChoiceTable;								AST( NAME("UseChoiceTable") )
	char* old_map_name;									AST( NAME("MapName") )
	char* old_spawn_target_name;						AST( NAME("SpawnTargetName") )
	REF_TO(ChoiceTable) old_choice_table;				AST( NAME("ChoiceTable") )
	char* old_choice_name;								AST( NAME("ChoiceName") )

	REF_TO(DoorTransitionSequenceDef) hTransSequence;	AST( NAME("TransitionOverride") )

	WorldVariable **oldVariables;						AST( NAME("Variable") )
	WorldVariableDef **variableDefs;					AST( NAME("VariableDef") )
} WorldWarpVolumeProperties;

AUTO_STRUCT AST_STARTTOK("") AST_ENDTOK("End") AST_STRIP_UNDERSCORES;
typedef struct WorldLandmarkVolumeProperties
{
	Expression *visible_cond;							AST( NAME("VisibleCondition") LATEBIND )
	char *icon_name;									AST( NAME("Icon") )
	DisplayMessage display_name_msg;					AST( NAME("DisplayName") STRUCT(parse_DisplayMessage) )
	bool hide_unless_revealed;							AST( NAME("HideUnlessRevealed") )
	bool scale_to_area;									AST( NAME("ScaleToArea") )
	int z_order;										AST( NAME("ZOrder") )
} WorldLandmarkVolumeProperties;

AUTO_STRUCT AST_STARTTOK("") AST_ENDTOK("End") AST_STRIP_UNDERSCORES;
typedef struct WorldNeighborhoodVolumeProperties
{
	DisplayMessage display_name_msg;					AST( NAME("DisplayName") STRUCT(parse_DisplayMessage) )
	char *sound_effect;									AST( NAME("SoundEffect") )
} WorldNeighborhoodVolumeProperties;

AUTO_STRUCT AST_STARTTOK("") AST_ENDTOK("End") AST_STRIP_UNDERSCORES;
typedef struct WorldMapLevelOverrideVolumeProperties
{
	S32 iLevel;											AST( NAME("Level"))
} WorldMapLevelOverrideVolumeProperties;

AUTO_STRUCT AST_STARTTOK("") AST_ENDTOK("End") AST_STRIP_UNDERSCORES;
typedef struct WorldOptionalActionVolumeEntry
{
	Expression *visible_cond;							AST( NAME("VisibleCondition") LATEBIND )
	Expression *enabled_cond;							AST( NAME("EnabledCondition") LATEBIND )
	DisplayMessage display_name_msg;					AST( NAME("DisplayName") STRUCT(parse_DisplayMessage) )

	// The priority defaults to "Normal"
	const char *category_name;							AST( NAME("Category") POOL_STRING )
	WorldOptionalActionPriority priority;				AST( NAME("Priority") DEF(5) )

	bool auto_execute;									AST( NAME("AutoExecute") )
	WorldGameActionBlock actions;						AST( NAME("Actions") STRUCT(parse_WorldGameActionBlock) )
} WorldOptionalActionVolumeEntry;

AUTO_STRUCT AST_STARTTOK("") AST_ENDTOK("End") AST_STRIP_UNDERSCORES;
typedef struct WorldOptionalActionVolumeProperties
{
	WorldOptionalActionVolumeEntry** entries;			AST( NAME("Entry") )
} WorldOptionalActionVolumeProperties;

AUTO_STRUCT AST_STARTTOK("") AST_ENDTOK("End") AST_STRIP_UNDERSCORES;
typedef struct WorldAIVolumeProperties
{
	bool avoid;											AST( NAME("Avoid") )
} WorldAIVolumeProperties;

AUTO_STRUCT AST_STARTTOK("") AST_ENDTOK("End") AST_STRIP_UNDERSCORES;
typedef struct WorldBeaconVolumeProperties
{
	bool nodynconn;											AST( NAME("NoDynConn") )
} WorldBeaconVolumeProperties;

AUTO_STRUCT AST_STARTTOK("") AST_ENDTOK("End") AST_STRIP_UNDERSCORES;
typedef struct WorldEventVolumeProperties
{
	Expression* entered_cond;							AST( NAME("EnterEventCondition") LATEBIND )
	Expression* exited_cond;							AST( NAME("ExitEventCondition") LATEBIND )

	//first entered action is done only the first time a player enters a volume on a map.
	//(will happen again on map reload.) This is to prevent transaction spam.
	WorldGameActionBlock *first_entered_action;			AST( NAME("FirstEnteredAction") STRUCT(parse_WorldGameActionBlock) )
} WorldEventVolumeProperties;

AUTO_STRUCT AST_STARTTOK("") AST_ENDTOK("End") AST_STRIP_UNDERSCORES;
typedef struct WorldSpawnProperties
{
	// Note: Spawn locations also have a name
	WorldSpawnPointType spawn_type;						AST( NAME("SpawnType") ) // what kind of spawn point is this
	Expression* active_cond;							AST( NAME("ActiveCondBlock") REDUNDANT_STRUCT("ActiveCond", parse_Expression_StructParam) LATEBIND ) // expression that determines if this spawn point is active
	char** source_volume_names;							AST( NAME("SourceVolumeNames") ) // volumes that feed into this spawn point
	bool needs_activation;								AST( NAME("NeedsActivation") ) // if true, this respawn point isn't usable until the player gets close to it
	bool y_always_up;									AST( NAME("YAlwaysUp") ) // if set then the spawn point always has up pointing along the Y-axis
	REF_TO(DoorTransitionSequenceDef) hTransSequence;	AST( NAME("TransitionOverride") )
} WorldSpawnProperties;

AUTO_STRUCT AST_STARTTOK("") AST_ENDTOK("End") AST_STRIP_UNDERSCORES;
typedef struct WorldPatrolPointProperties
{
	Vec3 pos;											AST( NAME("WorldMat") )

	// for editing
	Mat4 draw_mat;										NO_AST
	U32 moving : 1;										NO_AST
	U32 selected : 1;									NO_AST
} WorldPatrolPointProperties;

AUTO_STRUCT AST_STARTTOK("") AST_ENDTOK("End") AST_STRIP_UNDERSCORES;
typedef struct WorldPatrolProperties
{
	// Note: Patrol routes also have a name
	WorldPatrolRouteType route_type;					AST( NAME("RouteType") )
	WorldPatrolPointProperties **patrol_points;			AST( NAME("PatrolPoint") )
} WorldPatrolProperties;

AUTO_STRUCT AST_STARTTOK("") AST_ENDTOK("End") AST_STRIP_UNDERSCORES;
typedef struct WorldEncounterLayerProperties
{
	U32 layer_level;									AST( NAME("LayerLevel") )
	U32 force_team_size;								AST( NAME("ForceTeamSize") )
	bool ignore : 1;									AST( NAME("Ignore") )
	bool use_lockout : 1;								AST( NAME("UseLockout") )

// TODO: these should be converted to world library structs
//	TriggerCondition **trigger_conditions;				AST( NAME("TriggerCondition") STRUCT(parse_TriggerCondition) LATEBIND )
//	EncLayerFSM **layer_fsms;							AST( NAME("LayerFSM") STRUCT(parse_EncLayerFSM) LATEBIND )
} WorldEncounterLayerProperties;

AUTO_STRUCT AST_STARTTOK("") AST_ENDTOK("End") AST_STRIP_UNDERSCORES;
typedef struct WorldTriggerConditionProperties
{
	Expression* cond;									AST(NAME(condBlock), REDUNDANT_STRUCT(cond, parse_Expression_StructParam), LATEBIND)
} WorldTriggerConditionProperties;

AUTO_STRUCT AST_STARTTOK("") AST_ENDTOK("End") AST_STRIP_UNDERSCORES;
typedef struct WorldLayerFSMProperties
{
	REF_TO(FSM) hFSM;									AST(NAME(FSM) NAME(fsmName))
	WorldVariable** fsmVars;							AST(NAME(Variable))
} WorldLayerFSMProperties;

AUTO_STRUCT AST_STARTTOK("") AST_ENDTOK("End") AST_STRIP_UNDERSCORES;
typedef struct WorldWaterVolumeProperties
{
	char *water_def;				AST( NAME("WaterDef") )
	const char *water_cond;			AST( NAME("Condition") POOL_STRING )
} WorldWaterVolumeProperties;

AUTO_STRUCT AST_STARTTOK("") AST_ENDTOK("End") AST_STRIP_UNDERSCORES;
typedef struct WorldClusterVolumeProperties
{
	ClusterTargetLOD targetLOD;						AST( NAME("TargetLOD") )
	ClusterMinLevel minLevel;						AST( NAME("MinLevel") )
	ClusterMaxLODLevel maxLODLevel;					AST( NAME("MaxLODLevel") )
	ClusterTextureResolution textureHeight;			AST( NAME("TextureHeight") )
	ClusterTextureResolution textureWidth;			AST( NAME("TextureWidth") )
	ClusterTextureSupersample textureSupersample;	AST( NAME("TextureSupersample") )
	ClusterGeometryResolution geometryResolution;	AST( NAME("GeometryResolution") )
	bool includeNormal;								AST( NAME("IncludeNormal") )
	bool includeSpecular;							AST( NAME("IncludeSpecular") )
} WorldClusterVolumeProperties;

AUTO_STRUCT AST_STARTTOK("") AST_ENDTOK("End") AST_STRIP_UNDERSCORES;
typedef struct WorldIndoorVolumeProperties
{
	Vec3 ambient_hsv;									AST( NAME("AmbientHSV") )
	F32 light_range;									AST( NAME("LightRange") )
	bool can_see_outdoors;								AST( NAME("CanSeeOutdoors") )
} WorldIndoorVolumeProperties;

AUTO_STRUCT AST_STARTTOK("") AST_ENDTOK("End") AST_STRIP_UNDERSCORES;
typedef struct WorldFXVolumeProperties
{
	WorldFXVolumeFilter fx_filter;						AST( NAME("Filter"))
	REF_TO(DynFxInfo) fx_entrance;						AST( NAME("Entrance") REFDICT(DynFxInfo))
	F32 fx_entrance_hue;								AST( NAME("EntranceHue"))
	char *pcEntranceParams;								AST( NAME("EntranceParams"))
	REF_TO(DynFxInfo) fx_exit;							AST( NAME("Exit") REFDICT(DynFxInfo))
	F32 fx_exit_hue;									AST( NAME("ExitHue"))
	char *pcExitParams;									AST( NAME("ExitParams"))
	REF_TO(DynFxInfo) fx_maintained;					AST( NAME("Maintained") REFDICT(DynFxInfo))
	F32 fx_maintained_hue;								AST( NAME("MaintainedHue"))
	char *pcMaintainedParams;							AST( NAME("MaintainedParams"))
} WorldFXVolumeProperties;

AUTO_STRUCT AST_STARTTOK("") AST_ENDTOK("End") AST_STRIP_UNDERSCORES;
typedef struct WorldPathNodeProperties
{
	bool bUGCNode;										AST( NAME("UGC"))
	WorldPathEdge **eaConnections;						AST( NAME("Connection") ADDNAMES("Connections"))
	bool bCanBeObstructed;								AST( NAME("CanBeObstructed"))
	bool bIsSecret;										AST( NAME("IsSecret"))
	S32 iTeleportID;									AST( NAME("TeleportID"))
} WorldPathNodeProperties;
extern ParseTable parse_WorldPathNodeProperties[];
#define TYPE_parse_WorldPathNodeProperties WorldPathNodeProperties

#define PATH_NODE_Y_OFFSET 3

AUTO_STRUCT AST_STARTTOK("") AST_ENDTOK("End") AST_STRIP_UNDERSCORES;
typedef struct AutoPlacementObject
{
	char *resource_name;								AST( NAME("ResourceName") ESTRING) 
	U32 resource_id;									AST( NAME("ResourceID"))
	F32 weight;											AST( NAME("Weight"))
	Expression* fitness_expression;						AST( NAME("PlacementConditionBlock") REDUNDANT_STRUCT("PlacementCondition", parse_Expression_StructParam) LATEBIND )
	Expression* required_condition;						AST( NAME("RequiredConditionBlock") REDUNDANT_STRUCT("RequiredCondition", parse_Expression_StructParam) LATEBIND )
	int snap_to_slope;									AST( NAME("SnapToSlope"))

	F32 target_percentage;						NO_AST
	F32 count;									NO_AST
	F32 priority;								NO_AST
	bool can_place;								NO_AST

} AutoPlacementObject;

extern ParseTable parse_AutoPlacementObject[];
#define TYPE_parse_AutoPlacementObject AutoPlacementObject


AUTO_STRUCT AST_STARTTOK("") AST_ENDTOK("End") AST_STRIP_UNDERSCORES;
typedef struct AutoPlacementGroup
{
	char* group_name;									AST( NAME("GroupName") ESTRING)
	F32	weight;											AST( NAME("Weight"))
	AutoPlacementObject** auto_place_objects;			AST( NAME("ObjectList"))
	Expression* fitness_expression;						AST( NAME("PlacementConditionBlock") REDUNDANT_STRUCT("PlacementCondition", parse_Expression_StructParam) LATEBIND )
	Expression* required_condition;						AST( NAME("RequiredConditionBlock") REDUNDANT_STRUCT("RequiredCondition", parse_Expression_StructParam) LATEBIND )

	F32 target_percentage;						NO_AST
	F32 count;									NO_AST
	F32 priority;								NO_AST
	bool can_place;								NO_AST

} AutoPlacementGroup;

extern ParseTable parse_AutoPlacementGroup[];
#define TYPE_parse_AutoPlacementGroup AutoPlacementGroup

AUTO_STRUCT AST_STARTTOK("") AST_ENDTOK("End") AST_STRIP_UNDERSCORES;
typedef struct AutoPlacementSet
{
	char* set_name;										AST( NAME("SetName") ESTRING)
	F32 proximity;										AST( NAME("Proximity")) 
	F32 variance; 										AST( NAME("Variance")) 
	AutoPlacementGroup** auto_place_group;				AST( NAME("GroupList")) 
} AutoPlacementSet;

extern ParseTable parse_AutoPlacementSet[];
#define TYPE_parse_AutoPlacementSet AutoPlacementSet


AUTO_STRUCT AST_STARTTOK("") AST_ENDTOK("End") AST_STRIP_UNDERSCORES;
typedef struct AutoPlacementOverride
{
	U32			resource_id;							AST( NAME("ResourceID"))
	char*		resource_name;							AST( NAME("ResourceName") ESTRING)
	U32			override_resource_id;					AST( NAME("OverrideResourceID"))
	char*		override_name;							AST( NAME("OverrideResourceName") ESTRING)
} AutoPlacementOverride;

extern ParseTable parse_AutoPlacementOverride[];
#define TYPE_parse_AutoPlacementOverride AutoPlacementOverride

AUTO_STRUCT AST_STARTTOK("") AST_ENDTOK("End") AST_STRIP_UNDERSCORES;
typedef struct WorldAutoPlacementProperties
{
	AutoPlacementSet**	auto_place_set;					AST( NAME("SetList"))
	AutoPlacementOverride** override_list;				AST( NAME("OverrideList"))
} WorldAutoPlacementProperties;

AUTO_STRUCT AST_STARTTOK("") AST_ENDTOK("End") AST_STRIP_UNDERSCORES;
typedef struct RoomInstanceMapSnapAction
{
	U8 action_type;
	Vec2 min_sel;
	Vec2 max_sel;
	F32 near_plane;
	F32 far_plane;
	F32 *points;										AST( NAME(Points) )
} RoomInstanceMapSnapAction;

AUTO_STRUCT AST_STARTTOK("") AST_ENDTOK("End") AST_STRIP_UNDERSCORES;
typedef struct RoomInstanceData
{
	RoomInstanceMapSnapAction **actions;				AST( NAME(Action) )
	bool no_photo;										AST( NAME(NoPhoto) )
	bool texture_override;								AST( NAME(TextureOverride) )
	const char *texture_name;							AST( NAME(TextureOverrideName) POOL_STRING)
} RoomInstanceData;

AUTO_STRUCT AST_STARTTOK("") AST_ENDTOK("End") AST_STRIP_UNDERSCORES;
typedef struct InstanceData
{
	RoomInstanceData *room_data;						AST( NAME(PartitionData) )
} InstanceData;

AUTO_STRUCT AST_STARTTOK("") AST_ENDTOK("End") AST_STRIP_UNDERSCORES;
typedef struct LogicalGroupSpawnProperties {
	LogicalGroupRandomType eRandomType;				AST( NAME(RandomType) )
	LogicalGroupSpawnAmountType eSpawnAmountType;	AST( NAME(SpawnAmountType) )
	U32 uSpawnAmount;								AST( NAME(SpawnAmount) )
	F32 fLockoutRadius;								AST( NAME(LockoutRadius) )
} LogicalGroupSpawnProperties;
extern ParseTable parse_LogicalGroupSpawnProperties[];
#define TYPE_parse_LogicalGroupSpawnProperties LogicalGroupSpawnProperties

AUTO_STRUCT AST_STARTTOK("") AST_ENDTOK("End") AST_STRIP_UNDERSCORES;
typedef struct LogicalGroupProperties
{
	// Spawn behavior for nested groups is unsupported, so this isn't needed yet
	//int group_weight;									AST( NAME(GroupWeight) )

	LogicalGroupSpawnProperties interactableSpawnProperties;	AST( NAME("InteractableSpawnProperties") )
	LogicalGroupSpawnProperties encounterSpawnProperties;		AST( NAME("EncounterSpawnProperties") )

	// Not implemented yet
	//LogicalGroupSpawnProperties nestedgroup_spawn_properties;
} LogicalGroupProperties;

AUTO_STRUCT AST_STARTTOK("") AST_ENDTOK("End") AST_STRIP_UNDERSCORES;
typedef struct LogicalGroup
{
	char *group_name;									AST( STRUCTPARAM )
	LogicalGroupProperties *properties;					AST( NAME(LogicalGroupData) )
	char **child_names;									AST( NAME(ChildName) )
} LogicalGroup;

AUTO_STRUCT AST_STARTTOK("") AST_ENDTOK("End") AST_STRIP_UNDERSCORES;
typedef struct CivilianCritterSpawn
{
	const char*		critter_name;						AST( NAME("CritterName") POOL_STRING)
	F32				spawn_weight;						AST( NAME("SpawnWeight"))
	bool			is_car;								AST( NAME("IsCar"))
	bool			restricted_to_volume;				AST( NAME("RestrictedToVolume"))

} CivilianCritterSpawn;

extern ParseTable parse_CivilianCritterSpawn[];
#define TYPE_parse_CivilianCritterSpawn CivilianCritterSpawn

AUTO_STRUCT AST_STARTTOK("") AST_ENDTOK("End") AST_STRIP_UNDERSCORES;
typedef struct WorldCivilianVolumeProperties
{
	bool		disable_roads;							AST( NAME("DisableRoads"))
	bool		disable_sidewalks;						AST( NAME("DisableSidewalks"))
	
	bool		forced_road;							AST( NAME("ForcedRoad"))
	bool		forced_road_has_median;					AST( NAME("ForcedRoadHasMedian"))
	bool		forced_sidewalk;						AST( NAME("ForcedSidewalk"))
	bool		forced_as_is;							AST( NAME("ForcedAsIs"))
	bool		forced_intersection;					AST( NAME("ForcedIntersection"))
	bool		forced_crosswalk;						AST( NAME("ForcedCrosswalk"))
	
	bool		pedestrian_wander_area;					AST( NAME("PedestrianWanderArea"))

	CivilianCritterSpawn**	critter_spawns;				AST( NAME("CivilianCritterSpawns"))

	const char*	pcLegDefinition;						AST( POOL_STRING)
	
	AST_STOP
	F32			pedestrian_total_weight;
	F32			car_total_weight;

} WorldCivilianVolumeProperties;


AUTO_STRUCT AST_STARTTOK("") AST_ENDTOK("End") AST_STRIP_UNDERSCORES;
typedef struct WorldMastermindVolumeProperties
{
	bool		safe_room_until_exit;					AST( NAME("SafeRoomUntilExit"))

} WorldMastermindVolumeProperties;

AUTO_STRUCT AST_STARTTOK("") AST_ENDTOK("End") AST_STRIP_UNDERSCORES;
typedef struct WorldCivilianPOIProperties
{
	REF_TO(FSM) fsm;									AST( NAME("FSM") )
} WorldCivilianPOIProperties;

AUTO_STRUCT AST_STARTTOK("") AST_ENDTOK("End") AST_STRIP_UNDERSCORES;
typedef struct WorldGenesisChallengeProperties
{
	GenesisChallengeType type;						AST( NAME("Type"))

	// For Kill:	 the encounter to defeat
	// For Clickies: the object to click
	char* complete_name;							AST( NAME("CompleteName"))

	// For combined Clicky/Spawn objects (Portals)
	char* spawn_name;								AST( NAME("SpawnName"))
} WorldGenesisChallengeProperties;

AUTO_STRUCT AST_STARTTOK("") AST_ENDTOK("End") AST_STRIP_UNDERSCORES;
typedef struct WorldGenesisProperties
{
	char *pcGenesisCompleteName;						AST( NAME("GenesisCompleteName") )
	int iNodeType;										AST( NAME("NodeType") )
	U32 bDetail : 1;									AST( NAME("IsDetail") )
	U32 bNode : 1;										AST( NAME("IsNode") )
} WorldGenesisProperties;
extern ParseTable parse_WorldGenesisProperties[];
#define TYPE_parse_WorldGenesisProperties WorldGenesisProperties

AUTO_STRUCT AST_STARTTOK("") AST_ENDTOK("End") AST_STRIP_UNDERSCORES;
typedef struct WorldTerrainExclusionProperties
{
	// See ExclusionVolume in wlExclusionGrid.h for parameter usage
	WorldTerrainExclusionType exclusion_type;			AST ( NAME("ExclusionType"))
	WorldTerrainCollisionType collision_type;			AST ( NAME("CollisionType"))
	WorldPlatformType platform_type;					AST ( NAME("PlatformType") NAME("IsPlatform"))
	bool challenges_only;								AST ( NAME("ChallengesOnly"))
} WorldTerrainExclusionProperties;

AUTO_STRUCT AST_STARTTOK("") AST_ENDTOK("End") AST_STRIP_UNDERSCORES;
typedef struct WorldWindSourceProperties
{
	WorldWindEffectType effect_type;	AST( NAME("EffectType") )
	F32 speed;							AST( NAME("Speed") )
	F32 speed_variation;				AST( NAME("SpeedVariation") )
	Vec3 direction_variation;			AST( NAME("DirectionVariation") )
	F32 turbulence;						AST( NAME("Turbulence") )
	F32 radius;							AST( NAME("Radius") )
	F32 radius_inner;					AST( NAME("RadiusInner") )
} WorldWindSourceProperties;

// GroupProps : add struct definition here and add pointer to the appropriate struct below

AUTO_STRUCT;
typedef struct GroupVolumePropertiesServer
{
	WorldActionVolumeProperties 		*action_volume_properties;		AST( NAME(ActionVolume) )
	WorldPowerVolumeProperties			*power_volume_properties;		AST( NAME(PowerVolume) )
	WorldWarpVolumeProperties			*warp_volume_properties;		AST( NAME(WarpVolume) )
	WorldLandmarkVolumeProperties		*landmark_volume_properties;	AST( NAME(LandmarkVolume) )
	WorldNeighborhoodVolumeProperties	*neighborhood_volume_properties;	AST( NAME(NeighborhoodVolume) )
	WorldInteractionProperties			*interaction_volume_properties;	AST( NAME(InteractionVolume) )
	WorldOptionalActionVolumeProperties *obsolete_optionalaction_properties;	AST( NAME(OptionalActionVolume) )
	WorldAIVolumeProperties				*ai_volume_properties;			AST( NAME(AIVolume) )
	WorldBeaconVolumeProperties			*beacon_volume_properties;		AST( NAME(BcnVolume) )
	WorldEventVolumeProperties			*event_volume_properties;		AST( NAME(EventVolume) )
	WorldCivilianVolumeProperties		*civilian_volume_properties;	AST( NAME(CivilianVolume) )
	WorldMastermindVolumeProperties		*mastermind_volume_properties;	AST( NAME(MastermindVolume) )
	WorldMapLevelOverrideVolumeProperties *map_level_volume_properties;	AST( NAME(MapLevelVolume) )
} GroupVolumePropertiesServer;

AUTO_STRUCT;
typedef struct GroupVolumePropertiesClient
{
	WorldSkyVolumeProperties			*sky_volume_properties;			AST( NAME(SkyVolume) )
	WorldWaterVolumeProperties			*water_volume_properties;		AST( NAME(WaterVolume) )
	WorldClusterVolumeProperties		*cluster_volume_properties;		AST( NAME(ClusterVolume) )
	WorldIndoorVolumeProperties			*indoor_volume_properties;		AST( NAME(IndoorVolume) )
	WorldSoundVolumeProperties			*sound_volume_properties;		AST( NAME(SoundVolume) )
	WorldFXVolumeProperties				*fx_volume_properties;			AST( NAME(FXVolume) )
} GroupVolumePropertiesClient;

AUTO_STRUCT;
typedef struct GroupVolumeProperties
{
	GroupDefVolumeShape eShape;											AST( NAME("Shape") )

	Vec3 vBoxMin;														AST( NAME("BoxMin") )
	Vec3 vBoxMax;														AST( NAME("BoxMax") )
	F32 fSphereRadius;													AST( NAME("SphereRadius") )

	U32 bSubVolume : 1;													AST( NAME("SubVolume") )

	const char **ppcVolumeTypesDeprecated;								AST( NAME("VolumeType") POOL_STRING )

} GroupVolumeProperties;
extern ParseTable parse_GroupVolumeProperties[];
#define TYPE_parse_GroupVolumeProperties GroupVolumeProperties

AUTO_STRUCT;
typedef struct GroupHullProperties
{
	//Used by volumes and rooms
	const char **ppcTypes;												AST( NAME("HullType") POOL_STRING )
} GroupHullProperties;
extern ParseTable parse_GroupHullProperties[];
#define TYPE_parse_GroupHullProperties GroupHullProperties

AUTO_STRUCT;
typedef struct WorldUGCRoomObjectProperties
{
	WorldUGCRoomObjectType eType;										AST( NAME(Type) FLAGS )
	DisplayMessage dVisibleName;										AST( NAME(VisibleName) STRUCT(parse_DisplayMessage) )
} WorldUGCRoomObjectProperties;
extern ParseTable parse_WorldUGCRoomObjectProperties[];
#define TYPE_parse_WorldUGCRoomObjectProperties WorldUGCRoomObjectProperties

AUTO_STRUCT;
typedef struct WorldWindProperties
{
	F32 fEffectAmount;													AST( NAME("EffectAmount") DEFAULT(1) )
	F32 fBendiness;														AST( NAME("Bendiness") )
	F32 fPivotOffset;													AST( NAME("PivotOffset") )
	F32 fRustling;														AST( NAME("Rustling") DEFAULT(1) )
} WorldWindProperties;
extern ParseTable parse_WorldWindProperties[];
#define TYPE_parse_WorldWindProperties WorldWindProperties

AUTO_ENUM;
typedef enum WleClusterOverride
{
	WLECO_NONE=0,		ENAMES("NoOverride")
	WLECO_EXCLUDE=1,	ENAMES("ExcludeFromClusters")
	WLECO_INCLUDE=2,	ENAMES("IncludeInClusters")
} WleClusterOverride;

AUTO_STRUCT;
typedef struct WorldLODProperties
{
	F32 fLodScale;														AST( NAME("LodScale") DEFAULT(1) )

	U32 bShowPanel : 1;													AST( NAME("ShowPanel") )
	U32 bLowDetail : 1;													AST( NAME("LowDetail") )
	U32 bHighDetail : 1;												AST( NAME("HighDetail") )
	U32 bHighFillDetail : 1;											AST( NAME("HighFillDetail") )
	U32 bFadeNode : 1;													AST( NAME("FadeNode") )
	U32 bIgnoreLODOverride : 1;											AST( NAME("IgnoreLODOverride") )
	WleClusterOverride eClusteringOverride;								AST( NAME("ClusteringOverride") )

} WorldLODProperties;
extern ParseTable parse_WorldLODProperties[];
#define TYPE_parse_WorldLODProperties WorldLODProperties

AUTO_STRUCT AST_IGNORE("AmbientJobLocation") AST_IGNORE("CombatJobLocation");
typedef struct WorldPhysicalProperties
{
	F32 fAlpha;															AST( NAME("Alpha") DEFAULT(1) )

	F32 fBuildingGenBottomOffset;										AST( NAME("BuildingGenBottomOffset") )
	F32 fBuildingGenTopOffset;											AST( NAME("BuildingGenTopOffset") )
	F32 fCurveLengthDeprecated;											AST( NAME("CurveLength") )
	
	int iTagID;															AST( NAME("TagID") )
	int iWeldInstances;													AST( NAME("WeldInstances") )
	int iChildSelectIdx;												AST( NAME("ChildSelectIdx") )
	const char *pcChildSelectParam;										AST( NAME("ChildSelectParameter") POOL_STRING )

	U32 iOccluderFaces;													AST( NAME("OccluderFaces") DEFAULT(VOLFACE_ALL) )
	WorldCarryAnimationMode eCarryAnimationBit;							AST( NAME("CarryAnimationBit") )

	WLCameraCollisionType eCameraCollType;								AST( NAME("CameraCollType") )
	WLGameCollisionType eGameCollType;									AST( NAME("GameCollType") )

	WorldLODProperties oLodProps;										AST( EMBEDDED_FLAT )

	U32 bVisible : 1;													AST( NAME("Visible") DEFAULT(1) )
	U32 bPhysicalCollision : 1;											AST( NAME("PhysicalCollision") DEFAULT(1) )
	U32 bSplatsCollision : 1;											AST( NAME("SplatsCollision") DEFAULT(1) )
	U32 bHeadshotVisible : 1;											AST( NAME("HeadshotVisible") )

	U32 bAxisCameraFacing : 1;											AST( NAME("AxisCameraFacing") )
	U32 bCameraFacing : 1;												AST( NAME("CameraFacing") )
	U32 bCastCloseShadowsOnly : 1;										AST( NAME("CastCloseShadowsOnly") )
	U32 bIsDebris : 1;													AST( NAME("IsDebris") )
	U32 bDontCastShadows : 1;											AST( NAME("DontCastShadows") )
	U32 bDontReceiveShadows : 1;										AST( NAME("DontReceiveShadows") )
	U32 bDoubleSidedOccluder : 1;										AST( NAME("DoubleSidedOccluder") )
	U32 bHideOnPlace : 1;												AST( NAME("HideOnPlace") )
	U32 bHandPivot : 1;													AST( NAME("HandPivot") )
	U32 bMassPivot : 1;													AST( NAME("MassPivot") )
	U32 bInstanceOnPlace : 1;											AST( NAME("InstanceOnPlace") )
	U32 bMapSnapHidden : 1;												AST( NAME("MapSnapHidden") )
	U32 bMapSnapFade : 1;												AST( NAME("MapSnapFade") )
	U32 bNamedPoint : 1;												AST( NAME("NamedPoint") )
	U32 bRoomExcluded : 1;												AST( NAME("RoomExcluded") )
	U32 bNoOcclusion : 1;												AST( NAME("NoOcclusion") )
	U32 bNoVertexLighting : 1;											AST( NAME("NoVertexLighting") )
	U32 bUseCharacterLighting : 1;										AST( NAME("UseCharacterLighting") )
	U32 bOcclusionOnly : 1;												AST( NAME("OcclusionOnly") )
	U32 bOnlyAVolume : 1;												AST( NAME("OnlyAVolume") )
	U32 bRandomSelect : 1;												AST( NAME("RandomSelect") )
	U32 bSubObjectEditOnPlace : 1;										AST( NAME("SubObjectEditOnPlace") )
	U32 bCivilianGenerator : 1;											AST( NAME("CivilianGenerator") )
	U32 bForceTrunkWind : 1;											AST( NAME("ForceTrunkWind") )
	U32 bIsDebrisFieldCont : 1;											AST( NAME("IsDebrisFieldCont") )
	U32 bIsChildSelect : 1;												AST( NAME("IsChildSelect") )
	U32 bNoChildOcclusion : 1;											AST( NAME("NoChildOcclusion") )
	U32 bDummyGroup : 1;												AST( NAME("DummyGroup") )
	U32 bForbiddenPosition : 1;											AST( NAME("ForbiddenPosition") )

	// Deprecated Fields
	U32 bEditorVisibleOnly_Deprecated : 1;								AST( NAME("EditorVisibleOnly") )
	U32 bAlwaysCollide_Deprecated : 1;									AST( NAME("AlwaysCollide") )
	U32 bNoCollision_Deprecated : 1;									AST( NAME("NoCollision") )
	U32 bFullCollision_Deprecated : 1;									AST( NAME("FullCollision") )
	U32 bCameraCollision_Deprecated : 1;								AST( NAME("CameraCollision") )
	U32 bTranslucentWhenCameraCollides_Deprecated : 1;					AST( NAME("TranslucentWhenCameraCollides") )
	U32 bPermeable_Deprecated : 1;										AST( NAME("Permeable") )

} WorldPhysicalProperties;
extern ParseTable parse_WorldPhysicalProperties[];
#define TYPE_parse_WorldPhysicalProperties WorldPhysicalProperties

AUTO_STRUCT;
typedef struct WorldTerrainProperties
{
	int iExcludeOthersBegin;											AST( NAME("ExcludeOthersBegin") )
	int iExcludeOthersEnd;												AST( NAME("ExcludeOthersEnd") )
	F32 fExcludePriority;												AST( NAME("ExcludePriority") )
	F32 fExcludePriorityScale;											AST( NAME("ExcludePriorityScale") DEFAULT(1) )
	F32 fExcludeSame;													AST( NAME("ExcludeSame") )
	F32 fMultiExclusionVolumesDensity;									AST( NAME("MultiExclusionVolumesDensity") DEFAULT(1) )
	F32 fMultiExclusionVolumesRequired;									AST( NAME("MultiExclusionVolumesRequired") )
	GenesisMultiExcludeRotType iMultiExclusionVolumesRotation;			AST( NAME("MultiExclusionVolumesRotation") )
	F32 fScaleMin;														AST( NAME("ScaleMin") DEFAULT(0.9) )
	F32 fScaleMax;														AST( NAME("ScaleMax") DEFAULT(1) )
	F32 fSnapToTerrainNormal;											AST( NAME("SnapToTerrainNormal") )
	char *pcVaccuFormBrush;												AST( NAME("VaccuFormBrush") )
	F32 fVaccuFormFalloff;												AST( NAME("VaccuFormFalloff") DEFAULT(20) )
	F32 fIntensityVariation;											AST( NAME("IntensityVariation") DEFAULT(0.1) )
	char *pcVolumeName;													AST( NAME("VolumeName") )
	
	U32 bShowPanel : 1;													AST( NAME("ShowPanel") )
	U32 bTerrainObject : 1;												AST( NAME("TerrainObject") )
	U32 bSnapToTerrainHeight : 1;										AST( NAME("SnapToTerrainHeight") )
	U32 bSnapToTerrainNormal : 1;										AST( NAME("SnapToNormal") )
	U32 bVaccuFormMe : 1;												AST( NAME("VaccuFormMe") )
	U32 bGetTerrainColor : 1;											AST( NAME("GetTerrainColor") )

} WorldTerrainProperties;
extern ParseTable parse_WorldTerrainProperties[];
#define TYPE_parse_WorldTerrainProperties WorldTerrainProperties

AUTO_STRUCT;
typedef struct WorldRoomProperties
{
	WorldRoomType eRoomType;											AST( NAME("Type") )

	U32 bLimitLights : 1;												AST( NAME("LimitLights") )
	U32 bOccluder : 1;													AST( NAME("occluder") )
	U32 bUseModels : 1;													AST( NAME("UseModels") )

	RoomInstanceData *room_instance_data;								AST( NAME("RoomInstanceData") )
} WorldRoomProperties;
extern ParseTable parse_WorldRoomProperties[];
#define TYPE_parse_WorldRoomProperties WorldRoomProperties

AUTO_STRUCT;
typedef struct WorldLightProperties
{
	WleAELightType eLightType;											AST( NAME("LightType") )
	LightAffectType eAffectType;										AST( NAME("LightAffects") )

	const char *pcProjectedTexture;										AST( NAME("LightProjectedTexture") POOL_STRING )

	const char *pcCloudTexture;											AST( NAME("LightCloudTexture") POOL_STRING )
	F32 fCloudMultiplier1;												AST( NAME("LightCloudMultiplier1") )
	F32 fCloudScale1;													AST( NAME("LightCloudScale1") DEFAULT(100) )
	F32 fCloudScrollX1;													AST( NAME("LightCloudScrollX1") )
	F32 fCloudScrollY1;													AST( NAME("LightCloudScrollY1") )
	F32 fCloudMultiplier2;												AST( NAME("LightCloudMultiplier2") )
	F32 fCloudScale2;													AST( NAME("LightCloudScale2") DEFAULT(100) )
	F32 fCloudScrollX2;													AST( NAME("LightCloudScrollX2") )
	F32 fCloudScrollY2;													AST( NAME("LightCloudScrollY2") )

	Vec3 vAmbientHSV;													AST( NAME("LightAmbientHSV") )
	Vec3 vAmbientMultiplier;											AST( NAME("LightAmbientMultiplier") )
	Vec3 vAmbientOffset;												AST( NAME("LightAmbientOffset") )

	Vec3 vDiffuseHSV;													AST( NAME("LightDiffuseHSV") )
	Vec3 vDiffuseMultiplier;											AST( NAME("LightDiffuseMultiplier") )
	Vec3 vDiffuseOffset;												AST( NAME("LightDiffuseOffset") )

	Vec3 vSecondaryDiffuseHSV;											AST( NAME("LightSecondaryDiffuseHSV") )
	Vec3 vSecondaryDiffuseMultiplier;									AST( NAME("LightSecondaryDiffuseMultiplier") )
	Vec3 vSecondaryDiffuseOffset;										AST( NAME("LightSecondaryDiffuseOffset") )

	Vec3 vSpecularHSV;													AST( NAME("LightSpecularHSV") )
	Vec3 vSpecularMultiplier;											AST( NAME("LightSpecularMultiplier") )
	Vec3 vSpecularOffset;												AST( NAME("LightSpecularOffset") )

	Vec3 vShadowColorHSV;												AST( NAME("LightShadowColorHSV") )
	Vec3 vShadowColorMultiplier;										AST( NAME("LightShadowColorMultiplier") )
	Vec3 vShadowColorOffset;											AST( NAME("LightShadowColorOffset") )

	F32 fConeInner;														AST( NAME("LightConeInner") DEFAULT(45) )
	F32 fConeOuter;														AST( NAME("LightConeOuter") DEFAULT(55) )
	F32 fCone2Inner;													AST( NAME("LightCone2Inner") DEFAULT(45) )
	F32 fCone2Outer;													AST( NAME("LightCone2Outer") DEFAULT(55) )

	F32 fRadius;														AST( NAME("LightRadius") DEFAULT(10) )
	F32 fRadiusInner;													AST( NAME("LightRadiusInner") DEFAULT(1) )
	F32 fShadowNearDist;												AST( NAME("LightShadowNearDist") )

	F32 fVisualLODScale;												AST( NAME("LightVisualLODScale") DEFAULT(1) )

	U32 bCastsShadows : 1;												AST( NAME("LightCastsShadows") )
	U32 bInfiniteShadows : 1;											AST( NAME("LightInfiniteShadows") DEFAULT(1) )
	U32 bIsKey : 1;														AST( NAME("LightIsKey") )
	U32 bIsSun : 1;														AST( NAME("LightIsSun") )

	//This is a list of parser field names of all the values that are set
	const char **ppcSetFields;											AST( NAME("SetField") POOL_STRING ) 

} WorldLightProperties;
extern ParseTable parse_WorldLightProperties[];
#define TYPE_parse_WorldLightProperties WorldLightProperties

AUTO_STRUCT;
typedef struct WorldFXProperties
{
	const char *pcName;													AST( NAME("Name") POOL_STRING )

	char *pcCondition;													AST( NAME("Condition") )
	char *pcParams;														AST( NAME("Params") )
	char *pcFaction;													AST( NAME("Faction") )

	F32 fHue;															AST( NAME("Hue") )

	U32 bHasTarget : 1;													AST( NAME("HasTarget") )
	U32 bTargetNoAnim : 1;												AST( NAME("TargetNoAnim") )
	Vec3 vTargetPos;													AST( NAME("targetPos") )
	Vec3 vTargetPyr;													AST( NAME("TargetPYR") )

} WorldFXProperties;
extern ParseTable parse_WorldFXProperties[];
#define TYPE_parse_WorldFXProperties WorldFXProperties

AUTO_STRUCT AST_IGNORE_STRUCT(UGCProperties);
typedef struct GroupProperties
{
	GroupVolumePropertiesServer server_volume;							AST( EMBEDDED_FLAT )
	GroupVolumePropertiesClient client_volume;							AST( EMBEDDED_FLAT )
	GroupVolumeProperties *volume;										AST( NAME(Volume) )
	GroupHullProperties *hull;											AST( NAME(HullProperties) )

	WorldRoomProperties *room_properties;								AST( NAME(RoomProperties) )
	WorldWindSourceProperties *wind_source_properties;					AST( NAME(WindSource) )
	WorldAnimationProperties *animation_properties;						AST( NAME(Animation) )
	WorldSoundConnProperties *sound_conn_properties;					AST( NAME(SoundConn) )
	WorldSoundSphereProperties *sound_sphere_properties;				AST( NAME(SoundSphere) )
	WorldFXProperties *fx_properties;									AST( NAME(FXProperties) )
	WorldLightProperties *light_properties;								AST( NAME(LightProperties) )
	WorldInteractLocationProperties *interact_location_properties;		AST( NAME(InteractLocation) ADDNAMES(AmbientJob) )

	WorldCurve *curve;													AST( NAME(Curve) )
	WorldCurveGaps *curve_gaps;											AST( NAME(CuveGaps) )
	WorldChildCurve *child_curve;										AST( NAME(ChildCurve) )

	WorldLODOverride *lod_override;										AST( NAME(LODOverride) )

	WorldPlanetProperties *planet_properties;							AST( NAME(Planet) )
	WorldBuildingProperties *building_properties;						AST( NAME(Building) )
	WorldDebrisFieldProperties *debris_field_properties;				AST( NAME(DebrisField) )

	WorldEncounterLayerProperties *encounter_layer_properties;			AST( NAME(EncounterLayer) )
	WorldEncounterProperties *encounter_properties;						AST( NAME(EncounterDef) )
	WorldEncounterHackProperties *encounter_hack_properties;			AST( NAME(Encounter) )
	WorldSpawnProperties *spawn_properties;								AST( NAME(SpawnPoint) )
	WorldPatrolProperties *patrol_properties;							AST( NAME(PatrolRoute) )
	WorldTriggerConditionProperties *trigger_condition_properties;		AST( NAME(TriggerCondition) )
	WorldLayerFSMProperties *layer_fsm_properties;						AST( NAME(LayerFSM) )
	WorldAutoPlacementProperties *auto_placement_properties;			AST( NAME(AutoPlacement) )
	WorldPathNodeProperties *path_node_properties;						AST( NAME(PathNode) )
	
	WorldInteractionProperties *interaction_properties;					AST( NAME(InteractProperties) )

	WorldGenesisProperties *genesis_properties;							AST( NAME(GenesisProperties) )
	WorldGenesisChallengeProperties *genesis_challenge_properties;		AST( NAME(GenesisChallenge) )
	WorldTerrainProperties terrain_properties;							AST( NAME(Terrain) )
	WorldTerrainExclusionProperties *terrain_exclusion_properties;		AST( NAME(TerrainExclusion) )
	WorldUGCRoomObjectProperties* ugc_room_object_properties;			AST( NAME(UGCRoomObjectProperties) )

	WorldWindProperties wind_properties;								AST( NAME(Wind) )

	WorldPhysicalProperties physical_properties;						AST( NAME(Physical) )

} GroupProperties;


extern ParseTable parse_WorldEncounterLayerProperties[];
#define TYPE_parse_WorldEncounterLayerProperties WorldEncounterLayerProperties
extern ParseTable parse_WorldInteractionProperties[];
#define TYPE_parse_WorldInteractionProperties WorldInteractionProperties
extern ParseTable parse_WorldBaseInteractionProperties[];
#define TYPE_parse_WorldBaseInteractionProperties WorldBaseInteractionProperties
extern ParseTable parse_WorldInteractionPropertyEntry[];
#define TYPE_parse_WorldInteractionPropertyEntry WorldInteractionPropertyEntry
extern ParseTable parse_WorldActionInteractionProperties[];
#define TYPE_parse_WorldActionInteractionProperties WorldActionInteractionProperties
extern ParseTable parse_WorldSoundInteractionProperties[];
#define TYPE_parse_WorldSoundInteractionProperties WorldSoundInteractionProperties
extern ParseTable parse_WorldAnimationInteractionProperties[];
#define TYPE_parse_WorldAnimationInteractionProperties WorldAnimationInteractionProperties
extern ParseTable parse_WorldChairInteractionProperties[];
#define TYPE_parse_WorldChairInteractionProperties WorldChairInteractionProperties
extern ParseTable parse_WorldChildInteractionProperties[];
#define TYPE_parse_WorldChildInteractionProperties WorldChildInteractionProperties
extern ParseTable parse_WorldContactInteractionProperties[];
#define TYPE_parse_WorldContactInteractionProperties WorldContactInteractionProperties
extern ParseTable parse_WorldCraftingInteractionProperties[];
#define TYPE_parse_WorldCraftingInteractionProperties WorldCraftingInteractionProperties
extern ParseTable parse_WorldAmbientJobInteractionProperties[];
#define TYPE_parse_WorldAmbientJobInteractionProperties WorldAmbientJobInteractionProperties
extern ParseTable parse_WorldInteractLocationProperties[];
#define TYPE_parse_WorldInteractLocationProperties WorldInteractLocationProperties
extern ParseTable parse_WorldDestructibleInteractionProperties[];
#define TYPE_parse_WorldDestructibleInteractionProperties WorldDestructibleInteractionProperties
extern ParseTable parse_WorldDoorInteractionProperties[];
#define TYPE_parse_WorldDoorInteractionProperties WorldDoorInteractionProperties
extern ParseTable parse_WorldGateInteractionProperties[];
#define TYPE_parse_WorldGateInteractionProperties WorldGateInteractionProperties
extern ParseTable parse_WorldMoveDescriptorProperties[];
#define TYPE_parse_WorldMoveDescriptorProperties WorldMoveDescriptorProperties
extern ParseTable parse_WorldMotionInteractionProperties[];
#define TYPE_parse_WorldMotionInteractionProperties WorldMotionInteractionProperties
extern ParseTable parse_WorldRewardInteractionProperties[];
#define TYPE_parse_WorldRewardInteractionProperties WorldRewardInteractionProperties
extern ParseTable parse_WorldTextInteractionProperties[];
#define TYPE_parse_WorldTextInteractionProperties WorldTextInteractionProperties
extern ParseTable parse_WorldTimeInteractionProperties[];
#define TYPE_parse_WorldTimeInteractionProperties WorldTimeInteractionProperties
extern ParseTable parse_WorldGameActionProperties[];
#define TYPE_parse_WorldGameActionProperties WorldGameActionProperties
extern ParseTable parse_WorldGameActionBlock[];
#define TYPE_parse_WorldGameActionBlock WorldGameActionBlock
extern ParseTable parse_WorldGrantMissionActionProperties[];
#define TYPE_parse_WorldGrantMissionActionProperties WorldGrantMissionActionProperties
extern ParseTable parse_WorldGrantSubMissionActionProperties[];
#define TYPE_parse_WorldGrantSubMissionActionProperties WorldGrantSubMissionActionProperties
extern ParseTable parse_WorldMissionOfferActionProperties[];
#define TYPE_parse_WorldMissionOfferActionProperties WorldMissionOfferActionProperties
extern ParseTable parse_WorldGADAttribValueActionProperties[];
#define TYPE_parse_WorldGADAttribValueActionProperties WorldGADAttribValueActionProperties
extern ParseTable parse_WorldGiveItemActionProperties[];
#define TYPE_parse_WorldGiveItemActionProperties WorldGiveItemActionProperties
extern ParseTable parse_WorldNPCSendEmailActionProperties[];
#define TYPE_parse_WorldNPCSendEmailActionProperties WorldNPCSendEmailActionProperties
extern ParseTable parse_WorldTakeItemActionProperties[];
#define TYPE_parse_WorldTakeItemActionProperties WorldTakeItemActionProperties
extern ParseTable parse_WorldShardVariableActionProperties[];
#define TYPE_parse_WorldShardVariableActionProperties WorldShardVariableActionProperties
extern ParseTable parse_WorldSendFloaterActionProperties[];
#define TYPE_parse_WorldSendFloaterActionProperties WorldSendFloaterActionProperties
extern ParseTable parse_WorldSendNotificationActionProperties[];
#define TYPE_parse_WorldSendNotificationActionProperties WorldSendNotificationActionProperties
extern ParseTable parse_WorldWarpActionProperties[];
#define TYPE_parse_WorldWarpActionProperties WorldWarpActionProperties
extern ParseTable parse_WorldContactActionProperties[];
#define TYPE_parse_WorldContactActionProperties WorldContactActionProperties
extern ParseTable parse_WorldExpressionActionProperties[];
#define TYPE_parse_WorldExpressionActionProperties WorldExpressionActionProperties
extern ParseTable parse_WorldChangeNemesisStateActionProperties[];
#define TYPE_parse_WorldChangeNemesisStateActionProperties WorldChangeNemesisStateActionProperties
extern ParseTable parse_WorldAtmosphereProperties[];
#define TYPE_parse_WorldAtmosphereProperties WorldAtmosphereProperties
extern ParseTable parse_WorldPlanetProperties[];
#define TYPE_parse_WorldPlanetProperties WorldPlanetProperties
extern ParseTable parse_WorldSkyVolumeProperties[];
#define TYPE_parse_WorldSkyVolumeProperties WorldSkyVolumeProperties
extern ParseTable parse_WorldSoundVolumeProperties[];
#define TYPE_parse_WorldSoundVolumeProperties WorldSoundVolumeProperties
extern ParseTable parse_WorldSoundConnProperties[];
#define TYPE_parse_WorldSoundConnProperties WorldSoundConnProperties
extern ParseTable parse_WorldBuildingLayerProperties[];
#define TYPE_parse_WorldBuildingLayerProperties WorldBuildingLayerProperties
extern ParseTable parse_WorldDebrisFieldProperties[];
#define TYPE_parse_WorldDebrisFieldProperties WorldDebrisFieldProperties
extern ParseTable parse_WorldNebulaProperties[];
#define TYPE_parse_WorldNebulaProperties WorldNebulaProperties
extern ParseTable parse_WorldSubMapProperties[];
#define TYPE_parse_WorldSubMapProperties WorldSubMapProperties
extern ParseTable parse_WorldMapRepProperties[];
#define TYPE_parse_WorldMapRepProperties WorldMapRepProperties
extern ParseTable parse_WorldSolarSystemPOI[];
#define TYPE_parse_WorldSolarSystemPOI WorldSolarSystemPOI
extern ParseTable parse_WorldBuildingProperties[];
#define TYPE_parse_WorldBuildingProperties WorldBuildingProperties
extern ParseTable parse_WorldAnimationProperties[];
#define TYPE_parse_WorldAnimationProperties WorldAnimationProperties
extern ParseTable parse_WorldCurve[];
#define TYPE_parse_WorldCurve WorldCurve
extern ParseTable parse_WorldCurveGap[];
#define TYPE_parse_WorldCurveGap WorldCurveGap
extern ParseTable parse_WorldCurveGaps[];
#define TYPE_parse_WorldCurveGaps WorldCurveGaps
extern ParseTable parse_WorldChildCurve[];
#define TYPE_parse_WorldChildCurve WorldChildCurve
extern ParseTable parse_WorldLODOverride[];
#define TYPE_parse_WorldLODOverride WorldLODOverride
extern ParseTable parse_WorldEncounterHackProperties[];
#define TYPE_parse_WorldEncounterHackProperties WorldEncounterHackProperties
extern ParseTable parse_WorldEncounterProperties[];
#define TYPE_parse_WorldEncounterProperties WorldEncounterProperties
extern ParseTable parse_WorldActionVolumeProperties[];
#define TYPE_parse_WorldActionVolumeProperties WorldActionVolumeProperties
extern ParseTable parse_WorldPowerVolumeProperties[];
#define TYPE_parse_WorldPowerVolumeProperties WorldPowerVolumeProperties
extern ParseTable parse_WorldWarpVolumeProperties[];
#define TYPE_parse_WorldWarpVolumeProperties WorldWarpVolumeProperties
extern ParseTable parse_WorldLandmarkVolumeProperties[];
#define TYPE_parse_WorldLandmarkVolumeProperties WorldLandmarkVolumeProperties
extern ParseTable parse_WorldNeighborhoodVolumeProperties[];
#define TYPE_parse_WorldNeighborhoodVolumeProperties WorldNeighborhoodVolumeProperties
extern ParseTable parse_WorldMapLevelOverrideVolumeProperties[];
#define TYPE_parse_WorldMapLevelOverrideVolumeProperties WorldMapLevelOverrideVolumeProperties
extern ParseTable parse_WorldOptionalActionVolumeProperties[];
#define TYPE_parse_WorldOptionalActionVolumeProperties WorldOptionalActionVolumeProperties
extern ParseTable parse_WorldOptionalActionVolumeEntry[];
#define TYPE_parse_WorldOptionalActionVolumeEntry WorldOptionalActionVolumeEntry
extern ParseTable parse_WorldAIVolumeProperties[];
#define TYPE_parse_WorldAIVolumeProperties WorldAIVolumeProperties
extern ParseTable parse_WorldBeaconVolumeProperties[];
#define TYPE_parse_WorldBeaconVolumeProperties WorldBeaconVolumeProperties
extern ParseTable parse_WorldEventVolumeProperties[];
#define TYPE_parse_WorldEventVolumeProperties WorldEventVolumeProperties
extern ParseTable parse_WorldSpawnProperties[];
#define TYPE_parse_WorldSpawnProperties WorldSpawnProperties
extern ParseTable parse_WorldPatrolProperties[];
#define TYPE_parse_WorldPatrolProperties WorldPatrolProperties
extern ParseTable parse_WorldPatrolPointProperties[];
#define TYPE_parse_WorldPatrolPointProperties WorldPatrolPointProperties
extern ParseTable parse_LogicalGroup[];
#define TYPE_parse_LogicalGroup LogicalGroup
extern ParseTable parse_LogicalGroupProperties[];
#define TYPE_parse_LogicalGroupProperties LogicalGroupProperties
extern ParseTable parse_InstanceData[];
#define TYPE_parse_InstanceData InstanceData
extern ParseTable parse_RoomInstanceData[];
#define TYPE_parse_RoomInstanceData RoomInstanceData
extern ParseTable parse_RoomInstanceMapSnapAction[];
#define TYPE_parse_RoomInstanceMapSnapAction RoomInstanceMapSnapAction
extern ParseTable parse_WorldTriggerConditionProperties[];
#define TYPE_parse_WorldTriggerConditionProperties WorldTriggerConditionProperties
extern ParseTable parse_WorldLayerFSMProperties[];
#define TYPE_parse_WorldLayerFSMProperties WorldLayerFSMProperties
extern ParseTable parse_WorldWaterVolumeProperties[];
#define TYPE_parse_WorldWaterVolumeProperties WorldWaterVolumeProperties
extern ParseTable parse_WorldClusterVolumeProperties[];
#define TYPE_parse_WorldClusterVolumeProperties WorldClusterVolumeProperties
extern ParseTable parse_WorldIndoorVolumeProperties[];
#define TYPE_parse_WorldIndoorVolumeProperties WorldIndoorVolumeProperties
extern ParseTable parse_WorldFXVolumeProperties[];
#define TYPE_parse_WorldFXVolumeProperties WorldFXVolumeProperties
extern ParseTable parse_WorldAutoPlacementProperties[];
#define TYPE_parse_WorldAutoPlacementProperties WorldAutoPlacementProperties
extern ParseTable parse_WorldCivilianVolumeProperties[];
#define TYPE_parse_WorldCivilianVolumeProperties WorldCivilianVolumeProperties
extern ParseTable parse_WorldMastermindVolumeProperties[];
#define TYPE_parse_WorldMastermindVolumeProperties WorldMastermindVolumeProperties
extern ParseTable parse_WorldGenesisChallengeProperties[];
#define TYPE_parse_WorldGenesisChallengeProperties WorldGenesisChallengeProperties
extern ParseTable parse_WorldTerrainExclusionProperties[];
#define TYPE_parse_WorldTerrainExclusionProperties WorldTerrainExclusionProperties
extern ParseTable parse_InteractionCategoryNames[];
#define TYPE_parse_InteractionCategoryNames InteractionCategoryNames
extern ParseTable parse_WorldCivilianPOIProperties[];
#define TYPE_parse_WorldCivilianPOIProperties WorldCivilianPOIProperties
extern ParseTable parse_WorldWindSourceProperties[];
#define TYPE_parse_WorldWindSourceProperties WorldWindSourceProperties
extern ParseTable parse_GroupVolumePropertiesServer[];
#define TYPE_parse_GroupVolumePropertiesServer GroupVolumePropertiesServer
extern ParseTable parse_GroupVolumePropertiesClient[];
#define TYPE_parse_GroupVolumePropertiesClient GroupVolumePropertiesClient
extern ParseTable parse_GroupProperties[];
#define TYPE_parse_GroupProperties GroupProperties
extern ParseTable parse_WorldActivityLogActionProperties[];
#define TYPE_parse_WorldActivityLogActionProperties WorldActivityLogActionProperties
extern ParseTable parse_WorldGuildStatUpdateActionProperties[];
#define TYPE_parse_WorldGuildStatUpdateActionProperties WorldGuildStatUpdateActionProperties
extern ParseTable parse_WorldItemAssignmentActionProperties[];
#define TYPE_parse_WorldItemAssignmentActionProperties WorldItemAssignmentActionProperties
// GroupProps : add parse table declaration here

#endif //_WLGROUPPROPERTYSTRUCTS_H_
