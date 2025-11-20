#ifndef AICONFIG_H
#define AICONFIG_H

#include "Expression.h"
#include "aiEnums.h"
#include "referencesystem.h"

typedef struct AIAggroDef		AIAggroDef;
typedef struct AIAggroDefBucketOverride AIAggroDefBucketOverride;
typedef struct AIVarsBase		AIVarsBase;
typedef struct BrawlerCombatConfig BrawlerCombatConfig;
typedef struct Entity			Entity;
typedef struct Expression		Expression;
typedef struct ExprContext		ExprContext;	
typedef struct InheritanceData	InheritanceData;
typedef struct ParseTable		ParseTable;

extern ParseTable parse_AIConfig[];
#define TYPE_parse_AIConfig AIConfig

AST_PREFIX(WIKI(AUTO))

AUTO_STRUCT;
typedef struct AIMovModeSafetyTeleportDef
{
	// the distance at which the stranded checks begin
	F32		fStrandedDist;					AST(DEFAULT(90))
	
	// once we cross this distance, auto-teleport
	F32		fStrandedDistMax;				AST(DEFAULT(180))

	// after starting the stranded checking, the timeout before the entity teleports
	F32		fStrandedTimeout;				AST(DEFAULT(7))

	// the average distance per second that the entity is expect to gain on the movement target
	F32		fAvgDistPerSec;					AST(DEFAULT(7.5))

	// amount of time must pass before allowed to teleport
	F32		fMinTimeCheckingStranded;		AST(DEFAULT(2))

} AIMovModeSafetyTeleportDef;

// Defaults for danger factors are stored in data/ai/AggroExpressions.def
AUTO_STRUCT;
typedef struct AIConfigDangerFactors
{
	// The multiplier that controls how heavy to weigh distance from the critter to its target for aggro calculation. Defaults to 0.5
	F32 distFromMe; AST(ADDNAMES("DistFromMe:"))

	// The multiplier that controls how heavy to weigh distance from the critter to its target for aggro calculation. Defaults to 3
	F32 personalSpace; AST(ADDNAMES("PersonalSpace:"))

	// The multiplier that controls how heavy to weigh distance from the critter to its guard point (by default team spawn location) for aggro calculation. Defaults to 0.2
	F32 distFromGuardPoint; AST(ADDNAMES("DistFromGuardPoint:"))

	// The multiplier that controls how heavy to weigh damage done to the critter for aggro calculation. Defaults to 4
	F32 damageToMe; AST(ADDNAMES("DamageToMe:"))

	// The multiplier that controls how heavy to weigh damage done to friends for aggro calculation. Defaults to 1
	F32 damageToFriends; AST(ADDNAMES("DamageToFriends:"))

	// The multiplier that controls how heavy to weigh status effects done to the critter for aggro calculation. Defaults to 2
	F32 statusToMe; AST(ADDNAMES("StatusToMe:"))

	// The multiplier that controls how heavy to weigh status effects done to friends for aggro calculation. Defaults to 1
	F32 statusToFriends; AST(ADDNAMES("StatusToFriends:"))

	// The multiplier that controls how heavy to weigh healing actions against enemies for aggro calculation. Defaults to 1
	F32 healingEnemies; AST(ADDNAMES("HealingEnemies:"))

	// The multiplier that controls how heavy to weigh the target already being your current attack target. Defaults to 0
	F32 targetStickiness; AST(ADDNAMES("TargetStickiness:"))

	// The multiplier that controls how heavy to weigh which target your team assigned you. Defaults to 0.4
	F32 teamOrders; AST(ADDNAMES("TeamOrders:"))

	// The multiplier that controls how heavy to weigh threat done to the critter for aggro calculation. Defaults to 1
	F32 threatToMe; AST(ADDNAMES("threatToMe:"))

	// The multiplier that controls how heavy to weigh threat done to friends for aggro calculation. Defaults to 0
	F32 threatToFriends; AST(ADDNAMES("threatToFriends:"))

	// The multiplier that controls how heavy to weigh the results of the targeting rating expression for aggro calculation. Defaults to 0
	F32 targetingRatingExpr; AST(ADDNAMES("TargetingRatingExpr:"))
	
	U32 usedField[1];  AST(USEDFIELD)
}AIConfigDangerFactors;

// Defaults for danger scalars are stored in data/ai/AggroExpressions.def
AUTO_STRUCT;
typedef struct AIConfigDangerScalars
{
	// The scalar that controls how much to scale the total aggro to making leashing seem more natural. Defaults to [0, 1]
	F32 leashDecayScaleMin; AST(ADDNAMES("LeashDecayScaleMin:"))
	F32 leashDecayScaleMax; AST(ADDNAMES("LeashDecayScaleMax:"))

	U32 usedField[1];  AST(USEDFIELD)
}AIConfigDangerScalars;

AUTO_STRUCT;
typedef struct AIConfigAggro2
{
	// for aggro2, the aggro def that will be used, otherwise will use "default"
	REF_TO(AIAggroDef)	hOverrideAggroDef;		AST(NAME(OverrideAggroDef), REFDICT(AIAggroDef))

	AIAggroDefBucketOverride **eaOverrides;		AST(NAME("override"))
		
} AIConfigAggro2;

AUTO_STRUCT;
typedef struct AIMovementConfigSettings
{
	// Overrides critter's movement speed
	F32 overrideMovementSpeed; AST(ADDNAMES("OverrideMovementSpeed:"))

	// Overrides critter's movement turn rate (supported for flight only) (0 means insta-turn)
	F32 overrideMovementTurnRate; AST(ADDNAMES("OverrideMovementTurnRate:") DEFAULT(-1))

	// Overrides critter's movement friction
	F32 overrideMovementFriction; AST(ADDNAMES("OverrideMovementFriction:"))

	// Overrides critter's movement traction
	F32 overrideMovementTraction; AST(ADDNAMES("OverrideMovementTraction:"))

	// Determines how far ahead of a shortcut to attempt pseudospline
	F32 distBeforeWaypointToSpline;

	// Makes movement targets automatically offset by this amount vertically
	F32 movementYOffset; AST(ADDNAMES("CombatYOffset:"))

	// How much the AI dislikes jumping - this is a multiplier on the height to jump.  Default 2
	F32 jumpHeightCostMult; AST(ADDNAMES("JumpHeightCostMult:") DEFAULT(2))

	// How much the AI dislikes jumping - this is 'virtual distance'.  Defaults to 10.
	F32 jumpCostInFeet;	AST(ADDNAMES("JumpCostInFeet:") DEFAULT(10))

	// How much the AI dislikes jumping - this is the multiplier on the XZ dist.  Default is 2.
	F32 jumpDistCostMult; AST(ADDNAMES("JumpDistCostMult:") DEFAULT(2))

	// Forces movement speed to be faster than the given throttle percentage
	F32 minimumThrottlePercentage; AST(ADDNAMES("MinimumThrottlePercentage:"))

	// Disables movement based rotation (independent of powers rotating the critter to face its target)
	U32 dontRotate : 1;	AST(ADDNAMES("DontRotate:"))

	// Complete disables movement for this entity
	U32 immobile : 1; AST(ADDNAMES("Immobile:"))

	// Forces flying to always be used, even if the power is able to be turned off
	U32 alwaysFly : 1; AST(ADDNAMES("AlwaysFly:"))

	// Forces flying to be ignored, even if the power is able to be turned on
	U32 neverFly : 1; AST(ADDNAMES("NeverFly:"))

	// Ignores collision for movement code (does not actually turn the entity noColl)
	U32 collisionlessMovement : 1; AST(ADDNAMES("CollisionlessMovement:"))

	// Ignores world collision
	U32 noWorldColl : 1; AST(ADDNAMES("NoWorldColl:"))

	// Makes the critter always move during combat
	U32 continuousCombatMovement : 1; AST(ADDNAMES("ContinuousCombatMovement:"))

	// When paired with continuous combat movement, disables automatic facing at the end of movement
	// Used for continuous movement for ground critters, fixes a jittery movement bug
	U32 noContinousCombatFacing : 1; AST(ADDNAMES("NoContinuousCombatFacing:"))

	// If set move like a melee critter (either offset movement or follow, depending on whether
	// meleeMovementOffset is set)
	U32 meleeCombatMovement : 1; AST(ADDNAMES("MeleeCombatMovement:"))

	// If set try do a follow offset at the specified range instead of following when
	// doing melee movement
	U32 meleeMovementOffset : 1; AST(ADDNAMES("MeleeMovementOffset:"))

	// if doing meleeCombatMovement If set if following an entity, stop if within the preferred range
	// is trumped by meleeMovementOffset
	U32 meleeMovementUseRange : 1; AST(ADDNAMES("MeleeMovementUseRange:"))

	// Makes the critter pitch up or down when moving vertically
	U32 pitchWhenMoving : 1; AST(ADDNAMES("PitchWhenMoving:"))

	// Makes the critter not bank while flying
	U32 bankWhenMoving : 1; AST(ADDNAMES("BankWhenMoving:"))

	// disables the critter from teleporting due to being stuck
	U32 teleportDisabled : 1; AST(ADDNAMES("TeleportDisabled:"))

	// an aiConfig override, AI ignores all avoid volumes
	U32 ignoreAvoid : 1; AST(ADDNAMES("IgnoreAvoid:"))

} AIMovementConfigSettings;

AUTO_STRUCT;
typedef struct AIOfftickConfig
{
	// The name of the AIOfftickConfig, used for description and inheritance
	const char *name; AST(ADDNAMES("Name:"))

	// Run only once at the first AI tick, after the first FSM tick; use for initialization!
	Expression *initialize; AST(NAME("Initialize"), REDUNDANT_STRUCT("Initialize:", parse_Expression_StructParam), LATEBIND)

	// Determines if the fineCheck should be executed every frame
	Expression *coarseCheck; AST(NAME("CoarseCheck"), REDUNDANT_STRUCT("CoarseCheck:", parse_Expression_StructParam), LATEBIND)

	// Determines if the Action should be executed
	Expression *fineCheck; AST(NAME("FineCheck"), REDUNDANT_STRUCT("FineCheck:", parse_Expression_StructParam), LATEBIND)

	// Expression which is run if fineCheck is true
	Expression *action; AST(NAME("Action"), REDUNDANT_STRUCT("Action:", parse_Expression_StructParam), LATEBIND)

	// Tells the AI to execute the action only N times
	U32 maxCount;  AST(ADDNAMES("MaxCount:"))

	// Tells the AI to execute the action only  N times per combat
	U32 maxPerCombat;  AST(ADDNAMES("MaxPerCombat:"))

	// Once the action is fired N times, don't do the finecheck again till the coarsecheck passes next AI tick
	U32 maxPerThink;  AST(ADDNAMES("MaxPerThink:"))

	// Tells the AI to execute the FSM when the FineCheck goes off, in addition to the action
	U32 runAITick : 1;  AST(ADDNAMES("RunTick:"))

	// Only run action in combat
	U32 combatOnly : 1; AST(ADDNAMES("CombatOnly:"))

	U32 inherited : 1; NO_AST
} AIOfftickConfig;

AUTO_STRUCT;
typedef struct AICombatMovementConfig
{
	// Try to pick locations near your current one, but not your current one
	F32 arcDistance; AST(ADDNAMES("ArcDistance:") DEFAULT(0.1))

	// Try to pick locations that are full of others
	F32 clumping; AST(ADDNAMES("ClumpTogether:"))

	// Try to pick locations far from soft avoid volumes
	F32 softAvoid; AST(ADDNAMES("SoftAvoid:"))

	// Try to avoid any combat position which is occupied by some other entity.
	F32 avoidOccupiedPositions; AST(ADDNAMES("AvoidOccupiedPositions:"))

	// Try to pick locations that are near the average position of your own team
	F32 stayTogether;

	// Try to pick locations that are opposite those with enemies
	F32 flanking; AST(ADDNAMES("Flanking:"))

	// Try to move such that you're in the weakest shield arc of your target
	F32 targetShields; AST(ADDNAMES("TargetShields:"))

	// Try to move such that your weakest shield is turned to your target
	F32 turnWeakShieldTo;  AST(ADDNAMES("WeakShieldToEnemy:"))

	// Try to move such that your stronger shields are turned to your target
	F32 turnStrongShieldTo; AST(ADDNAMES("StrongShieldToEnemy:"))

	// Try to move such that your weakest shield is turned to the enemy who has done the most damage
	F32 turnWeakShieldToMostDamage;  AST(ADDNAMES("WeakShieldToMostDamageEnemy:"))

	// Try to move such that your arc-limited powers are usable
	F32 preferArcLimitedLocations; AST(ADDNAMES("PreferInArcLimits:") DEFAULT(0.75))

	// Try to move to spaces opposite, but not behind your opponent (90-135 deg)
	F32 circlingManeuvers;	AST(ADDNAMES("CirclingManeuvers:"))

	// Try to move to the current space
	F32 currentLocation; AST(ADDNAMES("CurrentLocation:"))

	// Try to move to an adjacent space
	F32 neighborLocation; AST(ADDNAMES("NeighborLocation:"))

	// Try to move such that your Y is different (suggested [-5 - 0])
	F32 yOffset; AST(ADDNAMES("YOffset:"))

	// 
	F32 softCoherency; AST(ADDNAMES("SoftCoherency:"))

	// Number to randomize adding to all positions (i.e. [0-#])
	F32 randomWeight;	AST(ADDNAMES("RandomWeight:"))

	// How often to force a combat position re-evaluation
	U32 positionChangeTime; AST(ADDNAMES("PositionChangeTime:"))

	// If true, instead of evaluating the current position to see if it is disliked,
	//  do a move every <PositionChangeTime> seconds, regardless of current position like
	//  Not to be confused with periodicMovementUpdate, which still allows control by dislike.
	U32 timedPositionChanges : 1;

	// When doing combat positioning, respect coherency
	U32 combatPositioningUseCoherency : 1; AST(ADDNAMES("CombatPositioningUseCoherency:"))

	// When doing combat, do not turn to face target
	U32 combatDontFaceTarget : 1; AST(ADDNAMES("CombatDontFaceTarget:"))

	// Periodically force a combat movement update
	//  Not to be confused with timedPositionChanges, which ignores control by dislike.
	U32 periodicMovementUpdate : 1; AST(ADDNAMES("PeriodicMovementUpdate:"))

	// skips the positions reevaluation part of the combat movement. Will mainly only move if out of range
	U32 skipPositionRevaluation : 1;

} AICombatMovementConfig;

AUTO_STRUCT;
typedef struct AITeamConfig
{
	// The weight each shield point is valued at, used to determine importance of shields
	// relative to health points
	F32 shieldHealWeight; AST(ADDNAMES("ShieldPointWeight:"))

	// the weight at which a dead teammate is valued at to determine the importance relative to 
	// someone who needs healing. Healing has a max of ~1 for someone who is almost dead
	F32 ressurectWeight; AST(ADDNAMES("RessurectWeight:") DEFAULT(0.3))

	// When a team goes into combat, the times used to calculate how long it will take before they respond
	F32 initialAggroWaitTimeMin; AST(ADDNAMES("InitialAggroWaitTimeMin:"))

	// When a team goes into combat, the times used to calculate how long it will take before they respond
	F32 initialAggroWaitTimeRange; AST(ADDNAMES("InitialAggroWaitTimeRange:"))

	// Makes this entity ignore the normal "don't attack higher level players" behavior
	U32 ignoreLevelDifference : 1; AST(ADDNAMES("IgnoreLevelDifference:"))

	// Skips the leashing part of combat
	// Disqualify enemies for being outside of leashing range, but
	// don't go into AITEAM_COMBAT_STATE_LEASH
	U32 skipLeashing : 1; AST(ADDNAMES("SkipLeashing:"))

	// Turns off leashing based on CombatMaxProtectDist entirely
	U32 ignoreMaxProtectRadius : 1; AST(ADDNAMES("ignoreMaxProtectRadius:", "DontLeash"))

	// Makes the critter add legal team targets for every opponent critter that targets it
	// NOTE: This setting will apply to everyone on the team if it is turned on for any of the
	// critters on the team at all
	U32 addLegalTargetWhenTargeted : 1; AST(ADDNAMES("AddLegalTargetWhenTargeted:"))

	// Makes the critter add legal team targets for opponents that are targeted BY the team
	// Generally only useful when there is a player on the team to initiate combat
	U32 addLegalTargetWhenMemberAttacks : 1; AST(ADDNAMES("AddLegalTargetWhenMemberAttacks:"))

	// Turned on for team if any member on the team has this set
	// When out of combat, the team will continue to perform healing/buff assignments
	U32 useHealBuffAssignmentsOOC : 1; AST(ADDNAMES("UseHealBuffAssignmentsOOC:"))

	// Whether or not rezzing in combat is allowed 
	U32 dontDoInCombatRezzing : 1; AST(ADDNAMES("DontDoInCombatRezzing:"))

	// using the team attack target assignments, when the team is first aggro'ed force the ai's 
	// attack target to be what the team would assign them
	U32 teamForceAttackTargetOnAggro : 1; AST(ADDNAMES("teamForceAttackTargetOnAggro:"))
	
	// if true, will add the whole aiteam to the combat team. 
	U32 socialAggroAlwaysAddTeamToCombatTeam : 1; AST(ADDNAMES("socialAggroAlwaysAddTeamToCombatTeam:"))
		
} AITeamConfig;

AUTO_STRUCT;
typedef struct HitAndWaitCombatStyleConfig
{
	// The minimum number of seconds the critter needs to wait
	// before going after the target when they use a power
	F32 fMinMovementWaitTime;	AST(DEFAULT(2))

	// The maximum number of seconds the critter needs to wait
	// before going after the target when they use a power
	F32 fMaxMovementWaitTime;	AST(DEFAULT(5))

	// The minimum range to keep while in waiting state
	F32 fMinDistanceToTargetWhileWaiting;	AST(DEFAULT(10))

	// The maximum range to keep while in waiting state
	F32 fMaxDistanceToTargetWhileWaiting;	AST(DEFAULT(20))

	// The distance to back away once the entity hits their target.
	// The value must be between fMinDistanceToTargetWhileWaiting and fMaxDistanceToTargetWhileWaiting
	F32 fBackAwayDistance;					AST(DEFAULT(15))

	// Entity checks if they took damage in last fBackAwayCancelLastDamageTime seconds.
	// If they did they cancel the back away movement
	F32 fBackAwayCancelLastDamageTimespan;	AST(DEFAULT(1))
	
	// The minimum number of attackers using a HitAndWaitCombatStyleConfig config before the ai will 
	// use the HitAndWaitCombatStyleConfig 
	S32 iMinHitAndWaitAttackersToEnable;	AST(DEFAULT(-1))
} HitAndWaitCombatStyleConfig;

AUTO_STRUCT;
typedef struct AITargetedConfig
{
	F32 minRange;
	F32 yMin;
	F32 yMax;
} AITargetedConfig;

AUTO_STRUCT WIKI("AIConfig");
typedef struct AIConfig
{
	// AIConfigs that this inherits from - applied in order, so later ones override earlier
	const char **inheritConfigs;  AST(ADDNAMES("InheritConfig:"), SIMPLE_INHERITANCE)

	// Expression that returns either true or false. If this expression returns false the target
	// will be skipped for targeting (i.e. be effectively untargetable)
	Expression* targetingRequires; AST(NAME("TargetingRequires"), REDUNDANT_STRUCT("TargetingRequires:", parse_Expression_StructParam), LATEBIND)


	// Expression that returns a number (which will then get normalized between the min and max.
	// After clamping and scaling according to the danger factors, this number gets included in
	// aggro calculations exactly like the other aggro factors
	Expression* targetingRating; AST(NAME("TargetingRating"), REDUNDANT_STRUCT("TargetingRating:", parse_Expression_StructParam), LATEBIND)

	// Cascading checks to allow expressions to be run off tick
	AIOfftickConfig** offtickActions;  AST(ADDNAMES("OfftickActions:"))

	// Expression that returns true or false, which determinse 

	// The minimum number the targetingRating expression's result will get normalized to. If the
	// min is 2, a value of 2 will mean a 0 getting passed in to the danger factors for scaling
	F32 targetingRatingMin; AST(ADDNAMES("TargetingRatingMin:"))

	// The maximum number the targetingRating expression's result will get normalized to. If the
	// max is 4, a value of 4 will mean a 1 getting passed in to the danger factors for scaling
	F32 targetingRatingMax; AST(ADDNAMES("TargetingRatingMax:"))

	// The minimum range that a critter will try to stay at
	F32 prefMinRange; AST(ADDNAMES("PrefMinRange:"))

	// The maximum range that a critter will try to stay at
	F32 prefMaxRange; AST(ADDNAMES("PrefMaxRange:"))

	// NOT IMPLEMENTED: The time until a critter will move in from a range farther than his
	// preferred range
	F32 prefMinRangeMoveTime; AST(ADDNAMES("PrefMinRangeLeashTime:"))

	// The time until a character will run away when closer than his preferred range
	F32 prefMaxRangeMoveTime; AST(ADDNAMES("PrefMaxRangeLeashTime:"), DEFAULT(20))

	// When a critter leashes, it will heal up to the next ratio defined in this array
	//  E.g., 33, 66, 100 will make a critter at 50% health heal to 66%
	FLOAT_EARRAY grievedHealingLevels; AST(ADDNAMES("GrievedHealingLevel:"))

	F32 prefMinGroundRange; AST(ADDNAMES("PrefMinGroundRange:"))
	F32 prefMaxGroundRange; AST(ADDNAMES("PrefMaxGroundRange:"))
	F32 prefMinGroundRangeMoveTime; AST(ADDNAMES("PrefMinGroundRangeTime:"))
	F32 prefMaxGroundRangeMoveTime; AST(ADDNAMES("PrefMaxGroundRangeTime:"))

	// The maximum range that a team member will look for reinforcements
	F32 combatMaxReinforceDist; AST(ADDNAMES("CombatMaxReinforceDist:"), DEFAULT(150))

	// This is the "leash" range that controls how far away a foe is allowed to be before
	// he becomes an invalid attack target. Default is 300 ft
	F32 combatMaxProtectDist; AST(ADDNAMES("CombatMaxProtectDist:"), DEFAULT(300))

	// if non-zero, this will make the team use the combatMaxProtectDist of the highest priority
	// if there is a tie, will use the greatest combatMaxProtectDist
	S32 combatMaxProtectDistTeamPriority; 

	// This is the number of times the critter will back away to range per fight
	U32 combatMaxRunAwayCount; AST(ADDNAMES("CombatMaxRunAwayCount:"))

	// If an enemy gets within this radius, stop trying to run away
	F32 combatEngagedRange; AST(ADDNAMES("CombatEngagedRange:"))

	// The minimum throttle percentage the critter should maintain during combat
	F32 combatMinimumThrottlePercentage; AST(ADDNAMES("CombatMinimumThrottlePercentage:"))

	// The maximum Y range this critter will pick to offset itself from the player
	F32 combatMaximumYVariance; AST(ADDNAMES("CombatMaximumYVariance:"))

	// The maximum distance a team member will be allowed to move from their leash distance
	F32 coherencyCombatHoldDist; AST(ADDNAMES("CoherencyCombatHoldDist:"), DEFAULT(20))
	F32 coherencyCombatFollowDist; AST(ADDNAMES("CoherencyCombatFollowDist:"), DEFAULT(60))

	// This is the angle centered directly away from the attack target a critter will look
	// for paths to run away (so that cornering someone actually corners them). Default is 180 degrees
	F32 maxRunAwayAngle; AST(ADDNAMES("MaxRunAwayAngle:"), DEFAULT(180))

	// The minimum weight for a healing action relative to buffing or attacking. Only applies if
	// this critter has usable healing powers. The healing factor is scaled between min and max
	// based on health of the target. Default is 0.5
	F32 healingActionWeightMin; AST(ADDNAMES("HealingActionWeightMin:"), DEFAULT(0.5))

	// The maximum weight for a healing action relative to buffing or attacking. Only applies if
	// this critter has usable healing powers. The healing factor is scaled between min and max
	// based on health of the target. Default is 2
	F32 healingActionWeightMax; AST(ADDNAMES("HealingActionWeightMax:"), DEFAULT(2))

	// threshold when deciding to heal in combat
	F32 inCombatHPHealThreshold; AST(ADDNAMES("InCombatHPHealThreshold:"), DEFAULT(0.75))

	// threshold when deciding to heal shield in combat
	F32 inCombatShieldHealThreshold; AST(ADDNAMES("InCombatShieldHealThreshold:"), DEFAULT(0.5))

	// threshold when deciding to heal when out of combat
	F32 ooCombatHPHealThreshold; AST(ADDNAMES("OOCombatHPHealThreshold:"), DEFAULT(0.9))

	// threshold when deciding to heal shield when out of combat
	F32 ooCombatShieldHealThreshold; AST(ADDNAMES("OOCombatShieldHealThreshold:"), DEFAULT(0.9))


	// 
	F32 teamAssignmentActionWeight; AST(ADDNAMES("TeamAssignmentActionWeight:") DEFAULT(1))

	// The weight for attacking as opposed to healing or buffing. Default is 1
	F32 attackActionWeight; AST(ADDNAMES("AttackActionWeight:"), DEFAULT(1))

	// The weight for buffing as opposed to healing or attacking. Default 1
	F32 buffActionWeight; AST(ADDNAMES("BuffActionWeight:"), DEFAULT(1))

	// The weight for doing a control power as opposed to buffing, attacking, etc. Default is 1
	F32 controlPowerActionWeight; AST(ADDNAMES("ControlPowerActionWeight:"), DEFAULT(1))

	// The weight for searching for a combat job as opposed to bugging, attacking, etc. Default is 1
	F32 combatJobSearchWeight; AST(ADDNAMES("CombatJobSearchWeight:"), DEFAULT(0))

	// Preferred power type - critter will use powers tagged with this before other powers
	const char *preferredAITag;	AST(POOL_STRING)

	// Field of view of the critter, controlling the angle (centered directly forward) in which
	// the critter can see. Note there is also the 10 feet "360 fov" area in which critters are
	// guaranteed to be able to notice you
	F32 fov; AST(ADDNAMES("FOV:") DEFAULT(270))


	// Time to calculate average aggro over to control aggroing attacktargets. Default is 4 sec
	U32 botheredAggroHistoryTime; AST(ADDNAMES("BotheredAggroHistoryTime:"), DEFAULT(4))

	// Threshold average calculated aggro has to be over to consider an enemy a worthy attack
	// target (unless you're force bothered by other means). Default is 0.054
	F32 botheredAggroHistoryThreshold; AST(ADDNAMES("BotheredAggroHistoryThreshold:"), DEFAULT(0.054))

	// Threshold average calculated aggro has to be below to lose aggro. Default is 0.03
	F32 botheredDropAggroHistoryThreshold; AST(ADDNAMES("BotheredDropAggroHistoryThreshold:"), DEFAULT(0.03))


	// Time after which the critter will give up trying to get into range and start doing ranged
	// attacks instead. Default is 10 sec
	F32 grievedRangedAttackTime; AST(ADDNAMES("GrievedRangedAttackTime:"), DEFAULT(0))

	// Time to do ranged attacks when you feel grieved. After this runs out the critter will
	// start chasing the player again. Default is 10 sec
	F32 grievedRecoveryTime; AST(ADDNAMES("GrievedRecoveryTime:"), DEFAULT(10))

	// Time until you will aggro people
	F32 spawnAggroTime; AST(ADDNAMES("SpawnAggroTime:") DEFAULT(10))

	// Time from when someone enters your perception radius until you consider them a valid target
	F32 stareDownTime; AST(ADDNAMES("StareDownTime:"), DEFAULT(8))

	// Amount of aggro that is instantly transferred to the pet's owner when a pet attacks something
	F32 ownerAggroDistributionInitial; AST(ADDNAMES("OwnerAggroDistributionInitial:"))

	// How long after your target goes invisible that you allow for running to his last known pos
	F32 invisibleAttackTargetDuration; AST(ADDNAMES("InvisibleAttackTargetDuration:"))

	// Number of seconds the critter will "remember" a target it can't see anymore
	F32 targetMemoryDuration; AST(ADDNAMES("TargetMemoryDuration:"), DEFAULT(10))

	// Maximum y difference to allow possible targets to be evaluated for initial aggro
	F32 aggroYDiffCap; AST(ADDNAMES("AggroYDiffCap:"))

	// TODO: Remove the old names
	// Sets the ratio of aware radius to aggro radius, default = 2/3rds
	F32 awareRatio; AST(ADDNAMES("AwareRatio:"), DEFAULT(0.66))

	// Specific override to the powers perception radius number. Default is 0 feet (off)
	F32 overrideAggroRadius; AST(ADDNAMES("OverrideAggroRadius:"))

	// Specific override to the powers proximity radius number. Default is 0 feet (off)
	F32 overrideProximityRadius; AST(ADDNAMES("OverrideProximityRadius:"))

	// Overrides regionrules to specify the primary social aggro dist
	F32 socialAggroPrimaryDist; AST(ADDNAMES("SocialAggroPrimaryDist:") DEFAULT(-1))

	// Overrides regionrules to specify the primary social aggro dist
	F32 socialAggroSecondaryDist; AST(ADDNAMES("SocialAggroSecondaryDist:") DEFAULT(-1))

	// When in staredown, an additional distance that it added to the aggro radius so that the aggroing entity
	// must step back the aggro radius + this radius to get out of staredown
	F32 staredownAdditiveAggroRadius; AST(ADDNAMES("StaredownAdditiveAggroRadius:"))

	// if non-zero, will apply this override aggro radius to critters vs specificly set entities 
	F32 seekTargetOverrideAggroRadius; AST(ADDNAMES("SeekTargetOverrideAggroRadius:"))

	// The range that is used to add to the leash range if the target is the current target
	F32 leashRangeCurrentTargetDistAdd; AST(ADDNAMES("LeashRangeCurrentTargetDistAdd:"))

	// The additional distance added to our leash when the target is our preferred target
	F32 leashRangePreferredTargetDistAdd; AST(ADDNAMES("LeashRangePreferredTargetDistAdd:"))

	// the range when out of FOV that the critter will aggro. Defaults to 10
	F32 outOfFOVAggroRadius; AST(ADDNAMES("OutOfFOVAggroRadius:") DEFAULT(10))

	// Time to wait between executing powers. Default is 0 sec (off)
	// the time is measured from when the ai first queues the power
	F32 globalPowerRecharge; AST(ADDNAMES("GlobalPowerRecharge:"))

	// Time randomized power delay - when AI decides to use a power, it adds [0,this] to the time.  
	// Default is 0.75.
	F32 randomizedPowerDelay; AST(ADDNAMES("RandomizedPowerDelay:") DEFAULT(0.75))

	// Fraction of an combat token generated per second for myself (Default=0)
	F32 combatTokenRateSelf;

	// Fraction of an combat token generated per second for team (including self) (Default = 1/6)
	F32 combatTokenRateSocial;		AST(DEFAULT(0.16666))

	// Factors controlling how much to scale individual aggro factors to decide total aggro
	AIConfigDangerFactors dangerFactors;

	// Scalars controlling how much to scale overall aggro
	AIConfigDangerScalars dangerScalars;

	// aggro2 related config
	AIConfigAggro2 aggro;

	// Values controlling movement
	AIMovementConfigSettings movementParams; AST(EMBEDDED_FLAT)

	// Values controlling combat movement
	AICombatMovementConfig combatMovementParams; AST(EMBEDDED_FLAT)

	AITeamConfig teamParams; AST(EMBEDDED_FLAT)

	// A list of movement mode types available to this ai
	AIMovementModeType *peMovementModeTypes; AST(ADDNAMES("MovementModeTypes:") SUBTABLE(AIMovementModeTypeEnum))

	// safety teleport movement mode data
	AIMovModeSafetyTeleportDef movModeSafetyTeleport; 

	AITargetedConfig* targetedConfig;

	// when sharing damage/threat how much the damage gets scaled before sending it
	F32 sharedAggroScale; AST(ADDNAMES("SharedAggroScale:") DEFAULT(0.5))

	// the default combat role definition that will be used.
	// note that the first entity with a valid combatRole def that is added to the team will set the 
	// combat role def for the team.
	const char *pchCombatRoleDef; AST(ADDNAMES("CombatRoleDef:") POOL_STRING)

	const char *pchFormationDef; AST(ADDNAMES("FormationDef:") POOL_STRING)

	// The role within the combat role def that this will use
	// this is overridden by any role passed into aiInit
	const char *pchCombatRoleName;  AST(ADDNAMES("CombatRoleName:") POOL_STRING)
		
	// This critter won't be given any jobs
	U32 dontAllowJobs : 1;
	
	// Keeps this entity from changing attack targets unless forcibly assigned
	U32 dontChangeAttackTarget : 1; AST(ADDNAMES("DontChangeAttackTarget:"))

	// Determines whether the critter will consider or be considered reinforcement
	U32 dontReinforce : 1; AST(ADDNAMES("DontReinforce:"))

	// Tells the critter not to randomize power activation time - normally will add up to 0.75s variation to all powers
	// This should generally be only on unique critters, otherwise they will tend to sync power usage
	U32 dontRandomizePowerUse : 1; AST(ADDNAMES("DontRandomizePowerUse:"))

	// Move to set up but don't actually execute attack powers. This is useful for temporarily running away or something related
	U32 dontUsePowers : 1; AST(ADDNAMES("DontUsePowers:"))
	
	// Turns off the warning about excessively low perception/awareness radii
	U32 dontErrorOnLowPerception : 1; AST(ADDNAMES("DontErrorOnLowPerception:"))

	// Turns off aggroing things based on perception radius
	U32 dontAggroInAggroRadius : 1; AST(ADDNAMES("DontAggroInAggroRadius:"))

	// Completely turn off aggro, saves CPU on totally passive objects
	U32 dontAggroAtAll : 1; AST(ADDNAMES("DontAggroAtAll:"))

	// Controls whether the critter does not do run away healing
	U32 dontDoGrievedHealing : 1; AST(ADDNAMES("DontDoGrievedHealing:"))

	// Controls whether the critter uses the combat position slots
	U32 useCombatPositionSlots : 1; AST(ADDNAMES("UseCombatPositionSlots:"))
	
	// uses the coherency values for the targeting restriction
	// this only applies if the leash type is set to something other than AI_LEASH_TYPE_DEFAULT
	U32 useCoherencyTargetRestriction : 1; AST(ADDNAMES("UseCoherencyTargetRestriction:"))

	// adds the powers range (preffered distances in hostile targeting) when computing leash distance
	U32 addPowersRangeToLeashDist : 1; AST(ADDNAMES("AddPowersRangeToLeashDist:"))

	// Makes the critter leash off of his current position when entering combat instead of
	// his spawn point
	U32 roamingLeash : 1; AST(ADDNAMES("RoamingLeash:"))
	
	// Allows targeting of enemies while the critter itself is untargetable
	U32 allowCombatWhileUntargetable : 1; AST(ADDNAMES("AllowCombatWhileUntargetable:"))

	// Makes this pet controllable through the UI
	U32 controlledPet : 1; AST(ADDNAMES("ControlledPet:"))

	// Marks critters to always scare pedestrians; not just only when they are in combat
	U32 alwaysScarePedestrians : 1; AST(ADDNAMES("AlwaysScarePedestrians:"))

	U32 doesCivilianBehavior : 1; AST(ADDNAMES("DoesCivilianBehavior:"))

	// Marks the critter to ignore the leash despawn that happens when a critter without
	//  an encounter leashes.
	U32 ignoreLeashDespawn : 1; AST(ADDNAMES("IgnoreLeashDespawn:"))

	// Tells the critter to ignore the aiglobalsetting to not leash on non static maps
	U32 leashOnNonStaticOverride : 1; AST(ADDNAMES("LeashOnNonStaticOverride:"))

	// Marks the critter to leash when despawning, even if part of an encounter
	//  Redundant for any sort of summoned critter
	U32 despawnOnLeash : 1; AST(ADDNAMES("DespawnOnLeash:"))

	// Determines whether the NPC will use the dynamic preferred range calculations (if not set, it will use standard preferred range)
	U32 useDynamicPrefRange : 1; AST(ADDNAMES("UseDynamicPreferredRange:"))

	// A temporary flag to initialize on controlled pet rallying until I can find a better place to know when to turn it on
	// (or maybe controlled pets always use rally points)
	U32 controlledPetUseRally : 1; AST(ADDNAMES("ControlledPetUseRally:"))

	// A flag that specifies that pets do not dump aggro on owner when the pet dies
	//  Useful for pets that are truly independent.
	U32 dontTransferAggroOnDeath : 1; AST(ADDNAMES("DontTransferAggroOnDeath:"))

	U32 dontAllowFriendlyPreferredTargets : 1; AST(ADDNAMES("DontAllowFriendlyPreferredTargets:"))

	// an override to have the AI immediately do any team assignments (like healing, curing, etc.) as they come up
	U32 doTeamAssignmentActionImmediately : 1; AST(ADDNAMES("DoTeamAssignmentActionImmediately:"))

	// When this is set the critter does not look for combat jobs automatically.
	U32 dontSearchForCombatJobs : 1; AST( ADDNAMES("DontSearchForCombatJobs:") )

	// an aiConfig override, does not allow any other AI to target
	U32 untargetable : 1; AST(ADDNAMES("Untargetable:"))

	// an aiConfig override, does not allow any other AI to target
	U32 disableCombatFSM : 1; AST(ADDNAMES("disableCombatFSM:"))

	// when in ambient behavior, keeps the entity to where it spawned at or last moved to
	// defaults to on
	U32 tetherToAmbientPosition : 1; AST(ADDNAMES("TetherToAmbientPosition:") DEFAULT(1))

	// whether the ai will do and/or accept social aggro pulses
	U32 ignoreSocialAggroPulse : 1;

	// whether the AI still grabs its non-combat team even in social aggro
	U32 socialAggroAsTeam : 1;

	// if set, the AI will ignore immobile target restrictions where it disqualifies targets if it has no powers to cast on them
	U32 ignoreImmobileTargetRestriction : 1;
	
	// if set, the AI will stop leashing if they are attacked while they are leashing		
	U32 respondToAttacksWhileLeashing : 1;	AST(ADDNAMES("RespondToAttacksWhileLeashing:") DEFAULT(1))
		
	// Sets the pet to the currently selected state when spawned (to keep all pets in sync)
	U32 controlledPetStartInCurState; AST(ADDNAMES("ControlledPetStartInCurState:"))

	// Sets the pet to the currently selected stance when spawned (to keep all pets in sync)
	U32 controlledPetStartInCurStance; AST(ADDNAMES("ControlledPetStartInCurStance:"))

	// Sets the distance at which critters will scare nearby civilians when NOT in combat
	F32 pedestrianScareDistance; AST(ADDNAMES("PedestrianScareDistance:"), DEFAULT(20))
	
	// The distance at which critters in combat will scare nearby civilians
	F32 pedestrianScareDistanceInCombat; AST(ADDNAMES("PedestrianScareDistanceInCombat:"), DEFAULT(40))

	// The distance used in determining which players should see the messages said by the NPCs. 
	// This value cannot be greater than overrideProximityRadius (or the default proximity radius if the overrideProximityRadius is not defined).
	// This value is ignored if it's equal to or less than 0.
	F32 sayMessageDistance;	AST(ADDNAMES("SayMessageDistance:"))

	// Hit and wait combat style config
	HitAndWaitCombatStyleConfig *pHitAndWaitCombatStyleConfig;

	BrawlerCombatConfig *pBrawlerCombatConfig;

	const char* name; AST(KEY, STRUCTPARAM)
	const char* filename; AST(CURRENTFILE)

	U32 usedFields[11];  AST(USEDFIELD)
}AIConfig;

// Loads all AIConfigs from data\ai\config
void aiConfigLoad(void);

// Adds an AIConfig change. These changes will get reapplied even if other specific changes are
// removed, so make sure you're either removing all changes at once, or you hold on to the handle
// you get back to be able to remove your change when you want to
int aiConfigModAddFromString(Entity* e, AIVarsBase* aib, const char* relObjPath, const char* val, ACMD_EXPR_ERRSTRING errString);

// Expression function version of aiConfigModAddFromString that will check if it's set on execution
ExprFuncReturnVal exprFuncAddConfigMod(ACMD_EXPR_SELF Entity* be, ExprContext* context, const char* objPath, const char* value, ACMD_EXPR_ERRSTRING errString);

// Expression function version of aiConfigModAddFromString that will remove itself after the current state
ExprFuncReturnVal exprFuncAddConfigModCurStateOnly(ACMD_EXPR_SELF Entity* e, ExprContext* context, const char* objPath, const char* value, ACMD_EXPR_ERRSTRING errString);

// Removes the StructMod with the given handle
void aiConfigModRemove(Entity* e, AIVarsBase* aib, int handle);

// Removes all AIConfigMods that match the passed in parameters
void aiConfigModRemoveAllMatching(Entity* e, AIVarsBase* aib, const char* relObjPath, const char* val);

// Removes all AIConfigMods from the passed in Entity (i.e. restores him to his critterdef AIConfig completely)
void aiConfigModRemoveAll(Entity* e, AIVarsBase* aib);

// Reapplies all AIConfigMods that are on the passed in entity (called after removing entries)
void aiConfigModReapplyAll(Entity* e, AIVarsBase* aib);

// Destroys all AIConfigMods, the earray and the extra AIConfig struct
void aiConfigModDestroyAll(Entity* e, AIVarsBase* aib);

// deletes the local config if there is one, and there are no config mods
void aiConfigLocalCleanup(AIVarsBase * aib);

// Really I just want to somehow automatically reload everything, but oh well...
void aiConfigReloadAll(void);

// Returns true if the setting exists
int aiConfigCheckSettingName(const char* setting);

// Clean up offtick instances
void aiConfigDestroyOfftickInstances(Entity *e, AIVarsBase *aib);

void aiTeamConfigApply(AITeamConfig *dst, const AITeamConfig *src, const Entity *srcEnt);

// Removes the given config mods
void aiConfigMods_RemoveConfigMods(Entity *e, SA_PARAM_NN_VALID S32 *peaTrackedConfigMods);

// will first release peaTrackedConfigMods. 
// applies the given config mods to the entity and saves the applied into peaTrackedConfigMods
void aiConfigMods_ApplyConfigMods(Entity *e, SA_PARAM_OP_VALID const AIConfigMod** eaConfigMods, SA_PARAM_OP_VALID S32 **peaTrackedConfigMods);



#endif
