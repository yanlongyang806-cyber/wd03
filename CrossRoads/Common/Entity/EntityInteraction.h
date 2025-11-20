/***************************************************************************
*     Copyright (c) 2006-2007, Cryptic Studios
*     All Rights Reserved
*     Confidential Property of Cryptic Studios
***************************************************************************/

#ifndef ENTITYINTERACTION_H
#define ENTITYINTERACTION_H
GCC_SYSTEM

#include "GlobalTypeEnum.h"
#include "contact_enums.h"
#include "Message.h"

typedef struct AIAnimList AIAnimList;
typedef struct CommandQueue CommandQueue;
typedef struct ContactDef ContactDef;
typedef struct Expression Expression;
typedef struct ExprContext ExprContext;
typedef struct WorldInteractionNode WorldInteractionNode;
typedef struct WorldInteractionPropertyEntry WorldInteractionPropertyEntry;
typedef struct Entity Entity;
typedef struct WorldGameActionBlock WorldGameActionBlock;
typedef struct NodeSummary NodeSummary;
typedef struct InventoryBag InventoryBag;
typedef U32 EntityRef;

// This is the distance used as the interact distance if not specified
// on the interactable or in the region rules
#define DEFAULT_NODE_INTERACT_DIST			10
#define DEFAULT_NODE_TARGET_DIST			100
#define DEFAULT_NODE_INTERACT_DIST_CUTOFF	100
#define INTERACT_RANGE 10
#define INTERACT_PICKUP_RANGE 5
#define INTERACT_RANGE_FAR 40

AUTO_ENUM;
typedef enum InteractType
{
	InteractType_Uninterruptible =		  0,
	InteractType_BreakOnMove =		(1 << 0),
	InteractType_BreakOnDamage =	(1 << 1),
	InteractType_BreakOnPower =		(1 << 2),
	InteractType_ConsumeOnUse =		(1 << 3),
	InteractType_NoRespawn =		(1 << 4),
	InteractType_Count, EIGNORE
} InteractType;

// Door targets are only used on door clickables (so far)
AUTO_STRUCT;
typedef struct DoorTarget
{
	const char* mapName;		AST( POOL_STRING )
	const char* spawnTarget;	AST( POOL_STRING )
	const char* queueName;		AST( POOL_STRING )
	ContainerID mapContainerID;
	U32 mapPartitionID;
} DoorTarget;

AUTO_STRUCT;
typedef struct InteractionChoice
{
	char *pchChoiceName;
	Expression* interactSuccessCond; AST( NAME(SuccessConditionBlock) REDUNDANT_STRUCT(SuccessCondition, parse_Expression_StructParam) LATEBIND )
	Expression* interactAction; AST( NAME(InteractActionBlock) REDUNDANT_STRUCT(InteractAction, parse_Expression_StructParam) LATEBIND )

	DoorTarget target;
} InteractionChoice;

AUTO_STRUCT AST_IGNORE_STRUCT(InteractGameActions) AST_IGNORE(InteractPlayerFX);
typedef struct OldInteractionProperties
{
	// If InteractCond is true, the player will be allowed to pull the trigger try to interact with this thing
	Expression* interactCond;				AST( NAME(InteractConditionBlock) REDUNDANT_STRUCT(InteractCondition, parse_Expression_StructParam) LATEBIND)

	// If interactSuccessCond is true, the player will successfully interact with this thing after pulling the trigger
	Expression* interactSuccessCond;		AST( NAME(SuccessConditionBlock) REDUNDANT_STRUCT(SuccessCondition, parse_Expression_StructParam) LATEBIND)
	Expression* interactAction;				AST( NAME(InteractActionBlock) REDUNDANT_STRUCT(InteractAction, parse_Expression_StructParam) LATEBIND)

	// These are actions that happen On Interact that can perform transactions safely.
	WorldGameActionBlock *interactGameActions;	AST( NAME(InteractGameActionBlock) STRUCT(parse_WorldGameActionBlock) )

	InteractType eInteractType;				AST( NAME(InteractType) FLAGS DEF(5) )	// Defaults to break on move or power

	U32 uInteractTime;						AST( NAME(uInteractTime) )
	U32 uInteractActiveFor;					AST( NAME(uInteractActiveFor) )
	U32 uInteractCoolDown;					AST( NAME(uInteractCoolDown) ) // Clickables default to coolDown = 300.  Other interactable things (critters) default to no cooldown

	DisplayMessage interactText;            AST( NAME(interactTextMsg) STRUCT(parse_DisplayMessage) )
	DisplayMessage interactFailedText;      AST( NAME(interactFailedTextMsg) STRUCT(parse_DisplayMessage) )

	REF_TO(AIAnimList) hInteractAnim;		AST( NAME(InteractAnim) REFDICT(AIAnimList))
} OldInteractionProperties;

typedef struct InteractTarget
{
	EntityRef entRef;	// safe if entity dies, can be passed to client
	bool bLoot;
	REF_TO(WorldInteractionNode) hInteractionNode;
	const char *pcVolumeNamePooled;
	int iInteractionIndex;
	GlobalType eTeammateType;
	ContainerID uTeammateID;
	Vec3 vNodeNearPoint;

} InteractTarget;


AUTO_ENUM;
typedef enum InteractOptionType
{
	InteractOptionType_Undefined,
	InteractOptionType_Node,
	InteractOptionType_CritterEntity,
	InteractOptionType_Volume,
} InteractOptionType;

/********************************************************************
*
*	IF YOU MAKE CHANGES TO THIS STRUCT,
*    you must also update cmpInteractOption()!
*    Otherwise the comparison in interaction_OncePerFrameScanTick()
*    won't know the struct has changed and you'll get dirty-bit errors.
*
***********************************************************************/
AUTO_STRUCT;
typedef struct InteractOption
{
	// Node, entity, or volume
	InteractOptionType eInteractOptionType;				AST( NAME(InteractOptionType))	
	
	// Any interact option has either a node, an entity ref, or a volume name
	REF_TO(WorldInteractionNode) hNode;
	U32	entRef;
	const char* pcVolumeName;							AST( POOL_STRING )

	// This index should be passed back with the interact attempt
	// It is meaningful in combination with the node, entity ref, or volume name
	int iIndex;

	// If single click loot is on, the server will inform the client of what loot is in the bag
	InventoryBag** eaLootBags;

	// This is the translated interaction offer string
	char *pcInteractString;

	// This is the translated interaction usability requirements string. For use if the Usable/Attemptable expression is a compound expression, for instance.
	char *pcUsabilityString;

	// This is the translated interaction detail string
	char *pcDetailString;

	// This is the interaction detail texture
	const char *pcInteractTexture;						AST( POOL_STRING )

	// If the interaction is a "per player" interaction, the extra info is here
	GlobalType iTeammateType;
	ContainerID iTeammateID;

	// Category and priority are UI hints
	const char *pcCategory;								AST( POOL_STRING )
	int iPriority;

	// The minimum interaction distance for this node (set by the server, read by the client)
	U32 uNodeInteractDist;

	// The node's location.  Passed down to the client from the server.  Only used if the client hasn't loaded the node's world cell
	Vec3 vNodePosFallback;

	// The node's radius.  Passed down to the client from the server.  Only used if the client hasn't loaded the node's world cell
	F32 fNodeRadiusFallback;

	// An option can be disabled but visible
	U32 bDisabled : 1;

	// Whether it should auto-execute
	U32 bAutoExecute : 1;

	// Nodes can also have "can pickup" enabled for them
	U32 bCanPickup : 1;

	// "can throw" is a special case when holding something
	U32 bCanThrow : 1;

	// This option is currently usable/attemptable based on the Attemptable expression in the related interactable
	U32 bAttemptable : 1;
	
} InteractOption;
extern ParseTable parse_InteractOption[];
#define TYPE_parse_InteractOption InteractOption

AUTO_STRUCT;
typedef struct InteractOptions
{
	InteractOption **eaOptions;		// All possible options
} InteractOptions;

AUTO_STRUCT;
typedef struct TargetableNode
{
	REF_TO(WorldInteractionNode) hNode;
	const char *pcDetailTexture;						AST(POOL_STRING)
	const char **eaCategories;							AST(POOL_STRING)
	const char **eaTags;								AST(POOL_STRING)
	const char *pchRequirementName;						AST(POOL_STRING)		// A skill requirement for this interactable. Parsed from the SuccessCondition 
	bool bIsAttemptable;								AST( DEFAULT(true) )	// We have a chance of success at this interaction node.
																				// If false we may not have an appropriate skill or attribute or something.
																				// Determined by AttemptableCond on the WorldInteractionPropertyEntry
}TargetableNode;
extern ParseTable parse_TargetableNode[];
#define TYPE_parse_TargetableNode TargetableNode

AUTO_STRUCT;
typedef struct TooltipNode
{
	REF_TO(WorldInteractionNode) hNode;								
}TooltipNode;
extern ParseTable parse_TooltipNode[];
#define TYPE_parse_TooltipNode TooltipNode

AUTO_STRUCT;
typedef struct VisibleOverrideNode
{
	REF_TO(WorldInteractionNode) hNode;
}VisibleOverrideNode;
extern ParseTable parse_VisibleOverrideNode[];
#define TYPE_parse_VisibleOverrideNode VisibleOverrideNode

AUTO_STRUCT;
typedef struct InteractedQueueDef
{
	STRING_POOLED		pchQueueName;			AST(KEY POOL_STRING)
		// Which queue was it
	U32					iInteractTime;
		// When did the interaction occur?
} InteractedQueueDef;

AUTO_STRUCT;
typedef struct EntInteractStatus
{
	DirtyBit dirtyBit;									AST(NO_NETSEND)

	bool bInteracting : 1;								AST(SELF_ONLY)
	// The player is interacting with something

	bool bSittingInChair : 1;							AST(SELF_ONLY)
	// The player is sitting in an interactable chair
	
	bool bResendInteractLists : 1;						NO_AST
	// Resend the interaction lists (nodes) because the map just changed - we want to make sure client/server sync'd

	bool bMovedSinceInteractTick;						NO_AST
	// True if the entity has moved since the last time it interacted.  Set when ENTITYFLAG_MOVED is set, but cleared less often

	Vec3 interactStartPos;								NO_AST
	// The position of the entity when it started interacting.

	InteractTarget interactTarget;						NO_AST
	// The entity, door, or clicky targeted for interaction (contact, switch, etc.)	

	InteractOptions interactOptions;					AST(SELF_ONLY NAME("InteractOptions"))
	// The list of intractable objects as decided by the server

	WorldInteractionPropertyEntry **eaOverrideEntries;	AST(SERVER_ONLY)
	// A list of dynamically generated interaction entries for this player

	InteractOptions recentAutoExecuteInteractOptions;	NO_AST
	// Interact options that have recently been auto-executed

	const char **eaInVolumes;							NO_AST
	// The list of volumes the player is currently in.  EArray of pooled strings.

	const char **eaInEventVolumes;						NO_AST
	// The list of event volumes that have conditions that the player is currently in.  EArray of pooled strings.

	const char **eaFirstEnterEventVolumes;				NO_AST
	// The list of event volumes with first_entry_actions on this map that the player has entered.  EArray of pooled strings.

	bool bInteractBreakOnDamage	: 1;					NO_AST
	bool bInteractBreakOnMove	: 1;					NO_AST
	bool bInteractBreakOnPower	: 1;					NO_AST
	// Interaction flags

	F32 fTimerInteract;									NO_AST
	// The current time left on interaction (when this reaches the max time, interaction happens)

	F32 fTimerInteractMax;								NO_AST
	// The maximum time for the interaction

	F32 fTimeUntilNextInteract;							NO_AST
	// How much longer until the player is allowed to try to interact again (to prevent interact spamming)

	REF_TO(Message) hInteractUseTimeMsg;				AST(SELF_ONLY)
	// The message to display while interacting with a use timer

	EntityRef overrideRef;								AST(CLIENT_ONLY)
	REF_TO(WorldInteractionNode) hOverrideNode;			AST(CLIENT_ONLY)
	// If set, will override the closest node when setting pInteractEntity/pInteractNode

	InteractOption **eaPromptedInteractOptions;			AST(CLIENT_ONLY UNOWNED)
	// Used on the client to track the currently displayed interaction options

	REF_TO(WorldInteractionNode) hPreferredTargetNode;	AST(CLIENT_ONLY)
	EntityRef preferredTargetEntity;					AST(CLIENT_ONLY)
	// These are used to track the preferred target

	U32					overrideSet : 1;				AST(CLIENT_ONLY)
	// An override has been set

	U32					promptInteraction : 1;			AST(CLIENT_ONLY)
	// True when the player is near an interactive object or character

	U32					promptPickup : 1;				AST(CLIENT_ONLY)
	// True when the player is near an object that can be picked up

	U32					bLockIntoCursorOverrides : 1;	NO_AST
	// If set to true, the cursor overrides do not change until this flag is set to false again.

	char*				pickupTarget;					AST(SELF_ONLY)
	// Target of pickup action

	U32					interactTargetCounter;				NO_AST
	U32					interactCheckCounter;				NO_AST
	// Counter to only update interaction FX once every fifteen frames

	// Used to determine if the player is nearby any contacts (ContactFlags)
	U32					eNearbyContactTypes;			AST(SELF_ONLY)

	InteractedQueueDef**	ppRecentQueueInteractions;	AST(SELF_ONLY)
	// Keeps track of recently used private PvE queues

	TargetableNode**		ppTargetableNodes;			AST(SELF_ONLY)
	TargetableNode**		ppTargetableNodesLast;		NO_AST			//used on client
	// List of clickies nearby that are glowing for the player.  Clickies can start or stop glowing
	// One of these lists is the list for last tick, one of them is for this tick.  They alternate each tick


	TooltipNode**		ppTooltipNodes;			AST(SELF_ONLY)
	//Nodes which are sent to the client for the sole purpose of displaying a mouseover tooltip.

	VisibleOverrideNode**	ppVisibleNodes;				AST(SELF_ONLY)
	VisibleOverrideNode**	ppVisibleNodesLast;			NO_AST
	CommandQueue* pEndInteractCommandQueue;				NO_AST
	// Queue of commands to be run when the player is done interacting (clearing anim bits, etc.)

	NodeSummary** ppDoorStatusNodes;					AST(SELF_ONLY)
	NodeSummary** ppDoorStatusNodesLast;				NO_AST

	// Used by interaction code to reduce the number of interact tests by
	// knowing if the player changed position
	Vec3 vLastInteractTargetPos;						NO_AST
	Vec3 vLastInteractTestPos;							NO_AST
}EntInteractStatus;
extern ParseTable parse_EntInteractStatus[];
#define TYPE_parse_EntInteractStatus EntInteractStatus

#define INTERACTTARGET_EQUAL(x, y) ((x)->entRef == (y)->entRef && GET_REF((x)->hInteractionNode) == GET_REF((y)->hInteractionNode) && (x)->pcVolumeNamePooled == (y)->pcVolumeNamePooled && (x)->iInteractionIndex == (y)->iInteractionIndex)

#endif
