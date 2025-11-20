/***************************************************************************
*     Copyright (c) 2006, Cryptic Studios
*     All Rights Reserved
*     Confidential Property of Cryptic Studios
***************************************************************************/

#pragma once
GCC_SYSTEM

#include "Encounter_Enums.h"
#include "EntEnums.h"
#include "entityinteraction.h"
#include "itemEnums.h"
#include "Message.h"
#include "multival.h"
#include "powers.h"
#include "referencesystem.h"
#include "WorldLibEnums.h"


typedef struct AIJobDesc AIJobDesc;
typedef struct ContactDef ContactDef;
typedef struct Critter Critter;
typedef struct CritterDef CritterDef;
typedef struct CritterFaction CritterFaction;
typedef struct CritterGroup CritterGroup;
typedef struct CritterVar CritterVar;
typedef struct DisplayMessage DisplayMessage;
typedef struct EncounterLayer EncounterLayer;
typedef struct EventTracker EventTracker;
typedef struct Expression Expression;
typedef struct FSM FSM;
typedef struct FSMContext FSMContext;
typedef struct GameEvent GameEvent;
typedef struct InventoryBag InventoryBag;
typedef struct Message Message;
typedef struct MultiVal MultiVal;
typedef struct OldEncounter OldEncounter;
typedef struct OldEncounterVariable OldEncounterVariable;
typedef struct OldInteractionProperties OldInteractionProperties;
typedef struct OldStaticEncounterGroup OldStaticEncounterGroup;
typedef struct RewardTable RewardTable;
typedef struct WorldActorProperties WorldActorProperties;
typedef struct WorldEncounterActionProperties WorldEncounterActionProperties;
typedef struct WorldEncounterActorSharedProperties WorldEncounterActorSharedProperties;
typedef struct WorldEncounterAIProperties WorldEncounterAIProperties;
typedef struct WorldEncounterEventProperties WorldEncounterEventProperties;
typedef struct WorldEncounterJobProperties WorldEncounterJobProperties;
typedef struct WorldEncounterLevelProperties WorldEncounterLevelProperties;
typedef struct WorldEncounterSpawnProperties WorldEncounterSpawnProperties;
typedef struct WorldEncounterWaveProperties WorldEncounterWaveProperties;
typedef struct WorldGameActionProperties WorldGameActionProperties;
typedef struct WorldScopeNamePair WorldScopeNamePair;
typedef struct ZoneMap ZoneMap;

// Not the right place for this, but putting here anyways
// If you change this, please change GENESIS_MAX_TEAM_SIZE in world lib as well
#define MAX_TEAM_SIZE 5


// Encounter Variable that can be filled out by the encounters to be polled from other systems
AUTO_STRUCT;
typedef struct OldEncounterVariable
{
	// Name of the variable, referenced outside of the encounter system
	const char* varName;				AST( POOL_STRING )

	// Value of the variable, currently can be any type, verifies on lookup
	MultiVal varValue;

	// Post processed - parses any string variables into multiples using the '|' separator
	MultiVal** parsedStrVals;			AST( NAME("") )

	// Display messages used during editing for Message vars
	DisplayMessage message;  AST( STRUCT(parse_DisplayMessage) NO_TEXT_SAVE)
} OldEncounterVariable;
extern ParseTable parse_OldEncounterVariable[];
#define TYPE_parse_OldEncounterVariable OldEncounterVariable

// Everything related to how an actor spawns, but not where
AUTO_STRUCT AST_IGNORE(displayNameMsg)	AST_IGNORE(critterSubRank);
typedef struct OldActorInfo
{
	// The type of actor
	Actor1CritterType eCritterType;		AST(NAME(CritterType))

	// Reference to a critterDef, only used if type is ActorType_Critter
	REF_TO(CritterDef) critterDef;		AST( REFDICT(CritterDef) NAME(CritterName) NON_NULL_REF__ERROR_ONLY)

	// Group, Rank and SubRank used to spawn a random critter if this type is ActorType_Critter
	// Only used if the critterDef reference does not exist
	REF_TO(CritterGroup) critterGroup;	AST( REFDICT(CritterGroup) NON_NULL_REF__ERROR_ONLY)
	const char *pcCritterRank;			AST(POOL_STRING)
	const char *pcCritterSubRank;		AST(NAME(Strength) POOL_STRING)
	// Note that there was a field named "critterSubRank" that was obsoleted and is AST_IGNORED

	// name of animlist to play when critter spawns
	const char* pchSpawnAnim;			AST(POOL_STRING)
	F32 fSpawnLockdownTime;

	// Contact interaction script that will be used when a player interacts with this actor
	REF_TO(ContactDef) contactScript;	AST(REFDICT(Contact) NON_NULL_REF__ERROR_ONLY)

	// Faction override to use instead of the critter's default faction
	REF_TO(CritterFaction) critterFaction; AST(REFDICT(CritterFaction) NON_NULL_REF__ERROR_ONLY)

	// Condition that determines whethot the actor is spawned
	// Evaluated once when the encounter spawns
	Expression* spawnCond;					AST( NAME(SpawnConditionBlock) REDUNDANT_STRUCT(SpawnCondition, parse_Expression_StructParam) LATEBIND )

	// Interaction properties that override those on the critter def.
	// Currently only the interact condition is actually used
	OldInteractionProperties oldActorInteractProps;	AST( NAME(InteractionProperties))
} OldActorInfo;

// Everything related to how a critter behaves once it spawns
AUTO_STRUCT;
typedef struct OldActorAIInfo
{
	// State machine override to use instead of the one on the critter
	REF_TO(FSM) hFSM;						AST( NAME(FSM) NAME(aiFSMName) )

	// List of actor variables that can be looked up from the FSM
	OldEncounterVariable** actorVars;			AST( NAME(Variable) )
} OldActorAIInfo;

// Everything related to where an actor spawns, but not how
AUTO_STRUCT;
typedef struct OldActorPosition
{
	// Orientation of the actor in the world
	Quat rotQuat;							AST( NAME(RotateOffset) )

	// Offset from the center of the encounter this actor is a part of
	Vec3 posOffset;							AST( NAME(PosOffset) )

	// If this is an Actor override, the position and rotation are overwritten instead of accumulated
	bool absoluteOverride;					AST( NAME(AbsoluteOverride) )
} OldActorPosition;

// This contains information about how to spawn a critter at a different scaling
// Which size is contained by the array index
AUTO_STRUCT;
typedef struct OldActorScaling
{
	// Info override to be used instead of the base actor info
	OldActorInfo* info;

	// AI info override.  This can be overridden while the ActorInfo is inherited or vice versa
	OldActorAIInfo* aiInfo;

	// Position override to be used instead of the base actor position
	OldActorPosition* position;
} OldActorScaling;

// Describes an actor that is part of an encounter def
// An actor can be a critter, objective, etc.
AUTO_STRUCT AST_IGNORE(hasFlavor) AST_IGNORE_STRUCT(TeamSize2) AST_IGNORE_STRUCT(TeamSize3) AST_IGNORE_STRUCT(TeamSize4) AST_IGNORE_STRUCT(TeamSize5);
typedef struct OldActor
{
	// Internal name, used to uniquely reference actors within an encounterdef
	const char* name;						AST(POOL_STRING)

	// Critter Group Display name override
	DisplayMessage critterGroupDisplayNameMsg;		AST(STRUCT(parse_DisplayMessage))

	// Display name override
	DisplayMessage displayNameMsg;		AST(STRUCT(parse_DisplayMessage))

	// Unique ID used internally only by the encounter system to match actors
	int uniqueID; AST( NAME(ID) )

	// The details of the actor
	OldActorScaling details;				AST( NAME(Default) )

	// If we ever want to do spawn scaling for more than 32 team sizes
	// You will need to make this field bigger, and you are crazy
	// Made this an enum so that the text file is readable
	ActorScalingFlag disableSpawn;		AST( FLAGS DEF(ActorScalingFlag_Inherited))
	ActorScalingFlag useBossBar;		AST( FLAGS DEF(ActorScalingFlag_Inherited))

	// Whether or not this actor has been fully or partially overridden by an instanced def
	bool overridden;					NO_AST
} OldActor;

AUTO_STRUCT;
typedef struct OldNamedPointInEncounter
{
	char *pointName;					AST(POOL_STRING)
	// Relative location of the point (in the encounter).  Do not use outside editor.
	Vec3 relLoc;						AST( NAME(loc))	// Deprecated
	Mat4 relLocation;					AST( NAME(Location))

	// Absolute location of this point, set by NamedPointFromName. Use this instead of relLoc.
	// TODO: This is a cumbersome system, since we have to initialize the absLoc all the time.
	// A better solution might be to generate these locations when we create the spawn rule, or always have
	// named points use absolute coordinates.
	Mat4 absLocation;					NO_AST
	bool hasAbsLoc;						NO_AST

	// Unique ID used for static encounter overrides
	int id;								AST( DEF(-1) )
	// Whether the object is frozen in the editor
	bool frozen;						NO_AST
} OldNamedPointInEncounter;

// State action pair that works like a reward specific to encounters
AUTO_STRUCT;
typedef struct OldEncounterAction
{
	// State when this action will occur
	EncounterState state;				AST( STRUCTPARAM )

	// Actions that occur when the above state is transitioned to
	Expression* actionExpr;				AST( NAME("ActionBlock") LATEBIND)
} OldEncounterAction;

// Static definition of an encounter
AUTO_STRUCT AST_IGNORE_STRUCT(TrackedEvents) AST_IGNORE_STRUCT(TrackedEventsSinceSpawn) AST_IGNORE_STRUCT(TrackedEventsSinceComplete);
typedef struct EncounterDef
{
	// Unique name that identifies the encounter. Required.
	char* name;							AST( STRUCTPARAM KEY POOL_STRING )

	// Scope for this encounter, which combines with name to get filename
	char* scope;						AST( POOL_STRING )
	
	// Filename this encounter was loaded from.
	const char* filename;				AST( CURRENTFILE)

	// This is a special encounter that can be used to spawn Nemesis content
	bool bAmbushEncounter;				AST( ADDNAMES(NemesisEncounter) )

	// A Map Variable that contains the Owner of this encounter
	const char *pchOwnerMapVar;			AST(NAME(OwnerMapVar) POOL_STRING)

	// Level range for the encounter, overrides the layer, overridden in world
	int minLevel;
	int maxLevel;

	// If there is a spawning player, spawn at their level instead of the level range above
	bool bUsePlayerLevel;

	// Optional condition that triggers this encounter to spawn.  If not there, encounter will spawn by proximity.
	Expression*	spawnCond;				AST( NAME(SpawnConditionBlock) REDUNDANT_STRUCT(SpawnCondition, parse_Expression_StructParam) LATEBIND )
	bool bCheckSpawnCondPerPlayer;

	// Optional conditions that complete this encounter.  If not there, encounter will complete when all actors are dead.
	Expression*	successCond;			AST( NAME(SuccessConditionBlock) REDUNDANT_STRUCT(SuccessCondition, parse_Expression_StructParam) LATEBIND )
	Expression*	failCond;				AST( NAME(FailureConditionBlock) REDUNDANT_STRUCT(FailureCondition, parse_Expression_StructParam) LATEBIND )

	// Optional condition for wave attacks.  If not present, encounter will not be a wave attack.
	Expression*	waveCond;				AST( NAME(WaveConditionBlock) REDUNDANT_STRUCT(WaveCondition, parse_Expression_StructParam) LATEBIND )

	// Critter Group this def will spawn its critter at unless overridden in the world, or by the actor
	REF_TO(CritterGroup) critterGroup;	AST(REFDICT(CritterGroup) NON_NULL_REF__ERROR_ONLY)

	// Faction this def will spawn its critter at unless overridden in the world, or by the actor
	REF_TO(CritterFaction) faction;		AST(REFDICT(CritterFaction) NON_NULL_REF__ERROR_ONLY)

	// Critter will spawn with this anim unless overwritten by actor
	char * spawnAnim;					AST(POOL_STRING)

	// Distance a player must be before this encounter spawns
	U32 spawnRadius;					AST( NAME("SpawnRadius") DEF(300) )

	// Probability that this encounter will spawn when triggered
	U32 spawnChance;					AST( NAME("SpawnChance") DEF(100) )

	// Time the encounter must wait before respawning
	U32 respawnTimer;					AST( NAME("RespawnTimer") DEF(300) )

	// Whether this encounter should automatically adjust its spawn rate
	WorldEncounterDynamicSpawnType eDynamicSpawnType;   AST( NAME("DynamicSpawnType") )

	// Encounter won't spawn if there's another active encounter within this radius
	U32 lockoutRadius;					AST( NAME("LockoutRadius") DEF(75) )

	// Min time between waves if there is a wave condition
	U32 waveInterval;					AST( NAME("WaveInterval") DEF(0) )

	// Delay on Wave Attacks
	U32 waveDelayMin;					AST( NAME("WaveDelayMin") DEF(0) )
	U32 waveDelayMax;					AST( NAME("WaveDelayMax") DEF(0) )

	// Override to make critters fight or not fight each other
	U32 gangID;							AST( DEF(0) )

	// Actions to be performed on state change of an encounter
	OldEncounterAction** actions;			AST( STRUCT(parse_OldEncounterAction) NAME(Action) )

	// AI Jobs that actors in this encounter can get assigned
	AIJobDesc** encJobs;				AST( NAME(AIJobDesc) )

	// List of actors that make up this encounter.
	OldActor** actors;						AST( NAME(Actor) )

	// Named points only visible to this encounter
	OldNamedPointInEncounter** namedPoints;

	// Post Processed after spawndef is loaded
	GameEvent** eaUnsharedTrackedEvents;				AST(NO_TEXT_SAVE)
	GameEvent** eaUnsharedTrackedEventsSinceSpawn;		AST(NO_TEXT_SAVE)
	GameEvent** eaUnsharedTrackedEventsSinceComplete;	AST(NO_TEXT_SAVE)
} EncounterDef;

// Structure that contains the list of encounters
AUTO_STRUCT;
typedef struct EncounterDefList
{
	// List of encounters, loaded on the server, sent to the client when editing
	EncounterDef** encounterDefs;		AST( NAME(EncounterDef) )
} EncounterDefList;

// Shared structure between client and server
extern DictionaryHandle g_EncounterDictionary;

#ifndef SDANGELO_REMOVE_PATROL_ROUTE
AUTO_STRUCT;
typedef struct OldPatrolPoint
{
	Vec3 pointLoc;
	Quat pointRot;

	bool frozen;							NO_AST
} OldPatrolPoint;

AUTO_STRUCT;
typedef struct OldPatrolRoute 
{
	char* routeName;						AST( STRUCTPARAM POOL_STRING )
	OldPatrolRouteType	routeType;
	OldPatrolPoint** patrolPoints;				AST( NAME(Point) )
} OldPatrolRoute;
#endif

// Static encounter placed in the map
AUTO_STRUCT;
typedef struct OldStaticEncounter
{
	// Unique name of a static encounter within a map
	const char* name;						AST( POOL_STRING )

	// Tells the encounter not to automatically despawn
	bool noDespawn;							AST(NAME("NoDespawn") DEF(0))

	// Name of the file where this static encounter lives
	const char* pchFilename;				AST(CURRENTFILE)

	// Def to actually use when spawning
	EncounterDef* spawnRule;				NO_AST

	// Level range for the static encounter, if non-zero, takes precidence over the layer and basedef
	int minLevel;
	int maxLevel;

	// Base definition of the encounter placed in the map (before modifications)
	REF_TO(EncounterDef) baseDef;		AST(REFDICT(EncounterDef) VITAL_REF)

	// Which group to use by default when spawning critters on the map
	REF_TO(CritterGroup) encCritterGroup;	AST(REFDICT(CritterGroup) NON_NULL_REF__ERROR_ONLY)

	// the spawn animlist to play
	char * spawnAnim;						AST( POOL_STRING )

	// Weight of this child used to determine change it will be used in a functional group
	int spawnWeight;

	// Faction that this encounter will spawn the critters by default
	REF_TO(CritterFaction) encFaction;		AST(REFDICT(CritterFaction) NON_NULL_REF__ERROR_ONLY)

	// Override to the basedef in the world
	// if defOverride->name is not NULL, use the properties from defOverride instead of the base def
	// Actors will always be merged regardless of whether or not defOverride is filled out
	EncounterDef* defOverride;

	// List of uniqueIDs of actors that are considered deleted, meaning no longer show up in the world
	int* delActorIDs;						AST( NAME(DeletedActors) )

	// Derived upon load, pointer to the group that contains this static encounter
	OldStaticEncounterGroup* groupOwner;		NO_AST

	// Derived upon load, pointer to the layer that contains this static encounter
	EncounterLayer* layerParent;			NO_AST

	// Patrol route that the entire group will follow
	char* patrolRouteName;					AST( POOL_STRING )

	// Tells the encounter not to snap its actors to the ground when they spawn.
	// Usually, actors should snap to the ground, but sometimes they're supposed to be knee-deep in terrain (?)
	bool bNoSnapToGround;

	// Position and orientation of the encounter in the map relative to 0, 0, 0
	Vec3 encPos;
	Quat encRot;

	// Editor-only fields
	F32 distToGround;	// Distance to ground (to detect when the terrain has changed on us)
	// If we've detected that the distance has changed
	bool bDistToGroundChanged;				NO_AST
	bool frozen;							NO_AST

	// Cached scope names for event matching purposes
	WorldScopeNamePair **eaScopeNames;		AST(SERVER_ONLY)
} OldStaticEncounter;

// Group of static encounters and their corresponding probabilities
AUTO_STRUCT;
typedef struct OldStaticEncounterGroup
{
	// Display name of this group in the editor
	const char* groupName;					AST( STRUCTPARAM POOL_STRING )

	// Functional Group: Number of children to spawn
	int numToSpawn;

	// Functional Group: Weight this group will have when used by a parent
	int groupWeight;

	OldStaticEncounter** staticEncList;		AST( NAME(StaticEncounter) )

	// List of subgroups within this group
	OldStaticEncounterGroup** childList;		AST( NAME(Group) )

	// Derived on load, points back to the parent group
	OldStaticEncounterGroup* parentGroup;		NO_AST

	// Cached scope names for event matching purposes
	WorldScopeNamePair **eaScopeNames;		AST(SERVER_ONLY)
} OldStaticEncounterGroup;

// Layer containing everything related to encounters and closely related systems
AUTO_STRUCT AST_IGNORE(MissionOnly) AST_IGNORE(needsRefresh);
typedef struct EncounterLayer
{
	// Basic layer info
	char *name;								AST( STRUCTPARAM KEY POOL_STRING )
	const char* pchFilename;				AST( CURRENTFILE )

	// Flags and other layer data
	bool ignore;							AST( NAME("IgnoreLayer") )	// If nonzero, this layer won't be loaded
	bool useLockout : 1;					AST( NAME("UseLockout") )
	bool visible;							// whether this layer is visible in the editor
	U32 layerLevel;							AST( NAME("Level:") )
	U32 forceTeamSize;
	U32 iNextClickableUniqueID;				// Used to assign unique IDs to Clickables

	// The encounters and clickables in the layer are within the root group
	OldStaticEncounterGroup rootGroup;			AST( NAME(StaticEncRoot) )

	// The following are tracked at the layer level and are not grouped
	OldPatrolRoute** oldNamedRoutes;		AST( NAME(NamedRoute) )

	// Post processing on load generates these unsaved lists of the contents of the layer
	// from the real data above
	OldStaticEncounter** staticEncounters;		NO_AST

	// in the editor, needs spawn rules updated on next draw frame
	bool bNeedsRefresh;						NO_AST 

	// Fixup code tag
	bool bRemoveOnSave;						NO_AST
} EncounterLayer;

// Contains a list of all layers associated the map and the filename for this map
AUTO_STRUCT;
typedef struct OldEncounterMasterLayer
{
	char* mapFileName;						NO_AST
	const char* mapPublicName;				NO_AST  // Pooled string
	EncounterLayer** encLayers;				AST(NAME("EncounterLayer"))
	EncounterLayer** ignoredLayers;			NO_AST	// Layers that aren't being loaded
} OldEncounterMasterLayer;

extern OldEncounterMasterLayer* g_EncounterMasterLayer;
extern DictionaryHandle g_EncounterLayerDictionary;


int oldencounter_LoadEncounterDefs(void);

// Initializes all actor var messages for the specified def (needed for editing)
void oldencounter_InitFSMVarMessages(EncounterDef *def, EncounterDef *baseDef);

// Finds an actor by it's internal Unique ID
OldActor* oldencounter_FindDefActorByID(EncounterDef* def, int uniqueID, bool remove);

// Finds an actor by it's internal name
OldActor* oldencounter_FindDefActorByName(EncounterDef* def, const char* name);

// Finds a named point in an encounter by ID
OldNamedPointInEncounter* oldencounter_FindDefPointByID(EncounterDef* def, int uniqueID, bool remove);

// Finds the first action in the def matching the given state
OldEncounterAction* oldencounter_GetDefFirstAction(EncounterDef* def, EncounterState state);

// Returns the current quat/vec for the actor
void oldencounter_GetActorPositionOffset(OldActor* actor, Quat outQuat, Vec3 outPos);

// Returns the info to use for an actor
OldActorInfo* oldencounter_GetActorInfo(OldActor* actor);

// Returns the AI info to use for an actor
OldActorAIInfo* oldencounter_GetActorAIInfo(OldActor* actor);

// Gets the FSM for the actor.
// Requires actorInfo from the SpawnRule, if it's a staticenc
FSM* oldencounter_GetActorFSM(const OldActorInfo* actorInfo, const OldActorAIInfo* actorAIInfo);
const char* oldencounter_GetFSMName(const OldActorInfo* actorInfo, const OldActorAIInfo* actorAIInfo);

// Writes the actor name into the passed in eString
void oldencounter_GetActorName(OldActor* actor, char** dstStr);

// Returns true if this actor will be enabled at the given team size
bool oldencounter_IsEnabledAtTeamSize(OldActor* actor, U32 teamSize);

// Returns true if this actor holds no information (if it inherits all of its data)
bool oldencounter_ActorIsEmpty(SA_PARAM_NN_VALID OldActor* actor);

// Clean up unneeded instances in the actor scaling info
bool oldencounter_TryCleanupActors(SA_PARAM_OP_OP_VALID OldActor*** baseDefActors, SA_PARAM_NN_OP_VALID OldActor*** overrideActors);

// Returns true if this actor should use the boss bar when spawned
bool oldencounter_ShouldUseBossBar(OldActor* actor, U32 teamSize);

// True if this Actor has interaction properties attached
bool oldencounter_HasInteractionProperties(OldActor *pActor);

// Creates a display name message for the specified actorinfo
Message *oldencounter_CreateDisplayNameMessageForEncDefActor(EncounterDef *def, OldActor *actor);

// Creates a Message for an externvar on the actor
Message *oldencounter_CreateVarMessageForEncounterDefActor(EncounterDef* def, OldActor* actor, const char *varName, const char *fsmName);

// Gets the encounter definition from the unique encounter name
EncounterDef* oldencounter_DefFromName(const char* encounterName);

// Finds a variable from the encounter that matches the name and type
OldEncounterVariable* oldencounter_LookupActorVariable(SA_PARAM_NN_VALID OldActorAIInfo* actorAIInfo, SA_PARAM_NN_STR const char* variableName);

bool oldencounter_IsDefDynamicSpawn(EncounterDef *pDef);

// Gets all variables for this actor
void oldencounter_GetAllActorVariables(OldActorAIInfo *actorAIInfo, OldEncounterVariable ***peaVars);

// Called when a map is loaded, opens the master layer and loads all encounters
void oldencounter_LoadLayers(ZoneMap* zmap);

// Warning!
// Using oldencounter_LoadLayersForMapByName to load more than one map at a time may do bad things; there are still a few global
// lists that are shared between all open master layers (encounter dictionary), and any collisions
// aren't handled.  Don't do it.
void oldencounter_LoadLayersForMapByName(const char *mapname, const char *publicname);

// Returns TRUE if the specified EncounterLayer belongs to the same map as the Master Layer
bool oldencounter_MatchesMasterLayer(const EncounterLayer *pEncounterLayer, const OldEncounterMasterLayer *pMasterLayer);

// This initializes the specified encounterlayer and adds a copy to the global master layer
void oldencounter_LoadToMasterLayer(const EncounterLayer *pEncounterLayer);

// Allocates a clones of the Encounter Layer
EncounterLayer* oldencounter_SafeCloneLayer(const EncounterLayer *pEncounterLayer);

// Copies all data to the target Encounter Layer
void oldencounter_SafeLayerCopyAll(const EncounterLayer* pSource, EncounterLayer *pDest);

// Called when the map is unloaded, removes all encounters
void oldencounter_UnloadLayers(void);

// Cleans up any unnecessary instancing
bool oldencounter_TryCleanupEncDefs(SA_PARAM_NN_VALID OldStaticEncounter* staticEnc);

// Get a static encounter's distance from the ground, to verify that the ground hasn't moved
F32 oldencounter_GetStaticEncounterHeight(int iPartitionIdx, SA_PARAM_NN_VALID OldStaticEncounter* staticEnc, SA_PARAM_OP_VALID S32* foundFloor);

// Updates the spawn rule for the static encounter
void oldencounter_UpdateStaticEncounterSpawnRule(OldStaticEncounter* staticEnc, EncounterLayer* encLayer);

// Turn off loading for this encounter layer
void oldencounter_IgnoreLayer(const char* layerName);
// Turn off loading for all other encounter layers
void oldencounter_IgnoreAllLayersExcept(const char* layerName);
// Clear the list of ignored layers
void oldencounter_UnignoreAllLayers();

// Finds a sublayer within the master layer
EncounterLayer* oldencounter_FindSubLayer(OldEncounterMasterLayer* masterLayer, const char* layerFileName);

// Create a list of all static encounters and/or encounter defs
void oldencounter_MakeMasterStaticEncounterNameList(OldEncounterMasterLayer* masterLayer, const char*** staticEncounters, const char*** encounterDefs);

#ifndef SDANGELO_REMOVE_PATROL_ROUTE
// Returns a patrol route from it's unique name within the layer
OldPatrolRoute* oldencounter_OldPatrolRouteFromName(EncounterLayer* encLayer, const char* name);
#endif

// Returns true if the static encounter group is "functional" and not just for UI
bool oldencounter_StaticEncounterGroupIsFunctional(OldStaticEncounterGroup* staticEncGroup);

// Returns true if the static encounter group is a "weighted" group and spawns some of its children
bool oldencounter_StaticEncounterGroupIsWeighted(OldStaticEncounterGroup* staticEncGroup);

// Returns the first functional parent above the group
OldStaticEncounterGroup* oldencounter_StaticEncounterGroupFindWeightedParent(OldStaticEncounterGroup* staticEncGroup);

// Returns the functional parent that owns this static encounter, if one exists
OldStaticEncounterGroup* oldencounter_StaticEncounterFindWeightedParent(OldStaticEncounter* staticEnc);

// Returns all functional static encounter groups under this functional group
void oldencounter_FunctionalGroupGetFunctionalGroupChildren(OldStaticEncounterGroup* staticEncGroup, OldStaticEncounterGroup*** groupListPtr);

// Returns all static encounter children that this functional group can spawn
void oldencounter_FunctionalGroupGetStaticEncounterChildren(OldStaticEncounterGroup* staticEncGroup, OldStaticEncounter*** encListPtr);

// Removes a static encounter reference from the dictionary
void oldencounter_RemoveStaticEncounterReference(OldStaticEncounter* staticEnc);

// Adds a static encounter reference to the dictionary
void oldencounter_AddStaticEncounterReference(OldStaticEncounter* staticEnc);

// Creates a Message for an externvar on the actor
Message *oldencounter_CreateVarMessageForStaticEncounterActor(OldStaticEncounter* staticEnc, OldActor* actor, const char *varName, const char *fsmName);

// Creates a display name message for the specified actorinfo
Message *oldencounter_CreateDisplayNameMessageForStaticEncActor(OldStaticEncounter* staticEnc, OldActor* actor);

// Finds a static encounter from it's unique name
OldStaticEncounter* oldencounter_StaticEncounterFromName(const char* name);

// Called to notify the encounter system that it should be loading the layers
bool oldencounter_AllowLayerLoading(void);

// Add map load and unload callbacks for the encounter system
typedef void(*EncounterMapChangeCallback)(OldEncounterMasterLayer *encMasterLayer);
void oldencounter_RegisterLayerChangeCallbacks(EncounterMapChangeCallback mapLoadCB, EncounterMapChangeCallback mapUnloadCB );
void oldencounter_RegisterEncounterLayerDictionary(void);

// Gets the path and scope for Messages in this Encounter Layer
void oldencounter_GetLayerScopePath(char *dest, SA_PARAM_NN_VALID const char *filename);
void oldencounter_GetLayerKeyPath(char *dest, SA_PARAM_NN_VALID const char *filename);


