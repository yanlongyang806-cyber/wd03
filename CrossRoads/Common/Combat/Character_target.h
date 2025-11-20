#pragma once
GCC_SYSTEM
/***************************************************************************
*     Copyright (c) 2006-2007, Cryptic Studios
*     All Rights Reserved
*     Confidential Property of Cryptic Studios
***************************************************************************/

#include "entEnums.h"
#include "Powers.h"

// Forward declarations
typedef struct Entity				Entity;
typedef struct Character			Character;
typedef struct PowerTarget			PowerTarget;
typedef struct WorldInteractionNode	WorldInteractionNode;
typedef struct CritterFaction		CritterFaction;

AUTO_ENUM AEN_APPEND_TO(TargetType);
enum FightClubTargetType
{
	kTargetType_PlaceHolder = kTargetType_MaxGeneric << 0,
	// Replace this with an actual target type as needed

} FightClubTargetType;

// Gets the relevant Entity for relation testing
SA_RET_NN_VALID Entity* entity_EntityGetRelationEnt(int iPartitionIdx, SA_PARAM_NN_VALID Entity *e);

// Determine relation between two entities, optionally ignores confusion
EntityRelation entity_GetRelationEx(int iPartitionIdx, SA_PARAM_OP_VALID Entity *e1, SA_PARAM_OP_VALID Entity *e2, bool bIgnoreConfusion);

// Determine relation between two entities, takes confusion into account
#define entity_GetRelation(iPartitionIdx, e1, e2) entity_GetRelationEx(iPartitionIdx, (e1), (e2), false)

// Wrappers for entity_GetRelation that converts neutral or unknown relations into friendly
EntityRelation critter_IsKOSEx(int iPartitionIdx, SA_PARAM_OP_VALID Entity *e1, SA_PARAM_OP_VALID Entity *e2, bool bIgnoreConfusion);
#define critter_IsKOS(iPartitionIdx, e1, e2) critter_IsKOSEx(iPartitionIdx, e1, e2, false)

// Check whether e1 considers e2 a Foe from gangs/factions.  Ignores confusion and PVP flag.
S32 critter_IsFactionKOS(int iPartitionIdx, SA_PARAM_NN_VALID Entity *e1, SA_PARAM_NN_VALID Entity *e2);

// Returns the relation the given faction has towards the target faction.  Returns Friend if there is no target
//  faction, the faction doesn't specify a relation towards the target faction, or the relation is defined
//  as Unknown, otherwise returns the actual value.
EntityRelation faction_GetRelation(SA_PARAM_NN_VALID CritterFaction *pFaction, SA_PARAM_OP_VALID CritterFaction *pFactionTarget);


// Function to check if a giving character can see the target character
bool character_CanPerceive(int iPartitionIdx, Character *pchar, Character *pcharTarget);

// Function to get the effective distance at which the given character can target the target
F32 character_GetPerceptionDist(Character *pchar, Character *pcharTarget);

// Function to get effective distance for generic stealth application (so AI uses same math)
F32 character_DistApplyStealth(Entity *target, F32 fDist, F32 fStealth, F32 fStealthSight);


// Return the target mask relating the given characters.
TargetType character_MakeTargetType(int iPartitionIdx, SA_PARAM_OP_VALID Character *pcharSource, SA_PARAM_OP_VALID Character *pcharTarget);

// Return the target mask relating the source Character to target non-entity node
TargetType character_MakeTargetTypeNode(SA_PARAM_OP_VALID Character *pcharSource);



// Returns true if the target and character have all of the relationships specified in the target type
bool character_TargetMatchesTypeRequire(int iPartitionIdx, SA_PARAM_OP_VALID Character *pcharSource, SA_PARAM_OP_VALID Character *pcharTarget, TargetType eType);

// Returns true if the target and character have none of the relationships specified in the target type
bool character_TargetMatchesTypeExclude(int iPartitionIdx, SA_PARAM_OP_VALID Character *pcharSource, SA_PARAM_OP_VALID Character *pcharTarget, TargetType eType);

// Returns true if the target matches the given type for the source
S32 character_TargetMatchesType(int iPartitionIdx, SA_PARAM_OP_VALID Character *pcharSource, SA_PARAM_OP_VALID Character *pcharTarget, TargetType eTypeRequire, TargetType eTypeExclude);

// Returns true if the target matches the given power target type for the source
S32 character_TargetMatchesPowerType(int iPartitionIdx, SA_PARAM_OP_VALID Character *pcharSource, SA_PARAM_OP_VALID Character *pcharTarget, SA_PARAM_NN_VALID PowerTarget *ptarget);

// Returns true if the target node matches the given type for the source
S32 character_TargetMatchesTypeNode(SA_PARAM_OP_VALID Character *pcharSource, U32 eTypeRequire, U32 eTypeExclude);

// Returns true if the target node matches the given power target type for the source
S32 character_TargetMatchesPowerTypeNode(SA_PARAM_OP_VALID Character *pcharSource, SA_PARAM_NN_VALID PowerTarget *ptarget);

// Returns true if the target is considered a Foe.  Faster than passing kTargetType_Foe to the normal
//  target type functions.
S32 character_TargetIsFoe(int iPartitionIdx, SA_PARAM_OP_VALID Character *pcharSource, SA_PARAM_OP_VALID Character *pcharTarget);

// used for destructible objects, finds the nearest point and returns the distance
F32 character_FindNearestPointForTarget(int iPartitionIdx, SA_PARAM_OP_VALID Character *pChar, SA_PRE_NN_BYTES(sizeof(Vec3)) Vec3 vTargetOut);
F32 character_FindNearestPointForObject(SA_PARAM_OP_VALID Character *pChar, 
										const Vec3 vSourcePos, 
										SA_PARAM_OP_VALID WorldInteractionNode *pTarget, 
										SA_PRE_NN_BYTES(sizeof(Vec3)) Vec3 vTargetOut, 
										bool bAccurate);

// Returns true if the target location is a valid place for the entity
S32 entity_LocationValid(SA_PARAM_NN_VALID Entity *pent, SA_PRE_NN_ELEMS(3) SA_POST_OP_VALID Vec3 vecTarget);

// Returns true if the entity or vector in question is in the given arc of the primary entity at the yaw offset
int entity_TargetInArc(SA_PARAM_NN_VALID Entity *pent,
					   SA_PARAM_OP_VALID Entity *pentTarget,
					   SA_PRE_OP_ELEMS(3) SA_POST_OP_VALID Vec3 vecTarget,
					   F32 fArc,
					   F32 fYawOffset);

// Returns true if the interaction node or vector in question is in the given arc of the primary entity at the yaw offset
int entity_TargetNodeInArc(Entity *pent,
						   WorldInteractionNode *pNodeTarget,
						   Vec3 vecTarget,
						   F32 fArc,
						   F32 fYawOffset);

// Returns true if the target is within range of the power. Uses vTargetPos if pentTarget is NULL.
bool character_TargetInPowerRangeEx(Character *pchar, 
									Power *pPower, 
									PowerDef *pdef, 
									Entity *pentTarget,
									Vec3 vTargetPos,
									S32 *piFailureOut);

// Returns true if the target is within range of the power.
bool character_TargetInPowerRange(Character *pchar, 
								  Power *pPower, 
								  PowerDef *pdef, 
								  Entity *pentTarget,
								  WorldInteractionNode *pnodeTarget);

// Finds a random valid target for the Character, given the PowerDef and
//  an optional different Character for the TargetType, and optional EntityRef
//  to exclude.
EntityRef character_FindRandomTargetForPowerDef(int iPartitionIdx,
												SA_PARAM_NN_VALID Character *pchar,
												SA_PARAM_NN_VALID PowerDef *pdef,
												SA_PARAM_OP_VALID Character *pcharTargetType,
												EntityRef erExclude);

// Finds the closest valid target for the Character, given the PowerDef and
//  an optional different Character for the TargetType, and optional EntityRefs
//  to exclude.
EntityRef character_FindClosestTargetForPowerDef(int iPartitionIdx,
												 SA_PARAM_NN_VALID Character *pchar,
												 SA_PARAM_NN_VALID PowerDef *pdef,
												 SA_PARAM_OP_VALID Character *pcharTargetType,
												 EntityRef erExclude,
												 EntityRef erExclude2);

// Finds the Character's dual target, and ensure's it's valid, and if not returns the Character
EntityRef character_GetTargetDualOrSelfRef(int iPartitionIdx, SA_PARAM_NN_VALID Character *pchar);

// returns true is the target is NOT one of the asked for types
//bool character_TargetTypeMismatch(Character *p, Character *ptarget, TargetType eType);


// Sets then entity's target to the entref.  If called on client it propagates to server.
void entity_SetTarget(SA_PARAM_OP_VALID Entity *e, EntityRef er);

// Sets the entity's focus target to the entref. If called on client it propagates to server.
void entity_SetFocusTarget(Entity *e, U32 er);

// Clears the entity's focus target. If called on client it propagates to server.
void entity_ClearFocusTarget(Entity *e);

// Clears the entity's dual target.  If called on client it propagates to server.
// NOTE: This is only when you must clear the dual target without affecting the main target.
void entity_ClearTargetDual(SA_PARAM_OP_VALID Entity *e);

// Sets then entity's target to the object key.  If called on client it propagates to server.
void entity_SetTargetObject(SA_PARAM_OP_VALID Entity *e, SA_PARAM_OP_STR const char *pchKey);

// The entity matches the target of the erToAssist, if erToAssist is an entity with a target
void entity_AssistTarget(SA_PARAM_OP_VALID Entity *e, EntityRef erToAssist);

// Makes selected facing pretend the entity has no target.Shut  Used for offscreen ents.
void entity_FaceSelectedIgnoreTarget(Entity *e, bool bIgnore);

void character_TouchObject(Entity *e, char *pchKey);

