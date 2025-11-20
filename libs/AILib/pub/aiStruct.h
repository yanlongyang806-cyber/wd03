#pragma once
GCC_SYSTEM

#include "aiEnums.h"
#include "aiConfig.h" // needed for AITeamConfig
#include "aiCombatRoles.h"
#include "aiStructCommon.h"
#include "Entity.h" // needed for EntityRef

typedef struct AICivCalloutInfo			AICivCalloutInfo;
typedef struct AICombatMovementConfig	AICombatMovementConfig;
typedef struct AICombatRoleAIVars		AICombatRoleAIVars;
typedef struct AIConfig					AIConfig;
typedef struct AIDebugWaypoint			AIDebugWaypoint;
typedef struct AIDebugLocRating			AIDebugLocRating;
typedef struct AIDebugLogEntry			AIDebugLogEntry;
typedef struct AIEntityBucket			AIEntityBucket;
typedef struct AIFormationData			AIFormationData;
typedef struct AIGroupCombatSettings	AIGroupCombatSettings;
typedef struct AIOfftickConfig			AIOfftickConfig;
typedef struct AIJob					AIJob;
typedef struct AIMastermindAIVars		AIMastermindAIVars;
typedef struct AIMovementModeManager	AIMovementModeManager;
typedef struct AIPowerEntityInfo		AIPowerEntityInfo;
typedef struct AIRelativeLocList		AIRelativeLocList;
typedef struct AIVolumeEntry			AIVolumeEntry;
typedef struct AIVolumeInstance			AIVolumeInstance;
typedef struct AttribModDef				AttribModDef;
typedef struct Beacon					Beacon;
typedef struct BeaconAvoidNode			BeaconAvoidNode;
typedef struct BrawlerCombatData		BrawlerCombatData;
typedef struct BrawlerCombatGlobalConfig BrawlerCombatGlobalConfig;
typedef struct CommandQueue				CommandQueue;
typedef struct CritterDef				CritterDef;
typedef struct CritterPowerConfig		CritterPowerConfig;
typedef struct EntAndDist				EntAndDist;
typedef struct Entity					Entity;
typedef struct ExprContext				ExprContext;
typedef struct FSMContext				FSMContext;
typedef struct MovementRequester		MovementRequester;
typedef struct Power					Power;
typedef struct StashTableImp			StashTableImp;
typedef struct StructMod				StructMod;
typedef struct AIFormation				AIFormation;
typedef struct GameInteractable			GameInteractable;
typedef struct GameInteractLocation		GameInteractLocation;

#define AI_MAX_AGGRO_HISTORY_LENGTH 10

AUTO_ENUM;
typedef enum AITeamAssignmentType
{
	AITEAM_ASSIGNMENT_TYPE_NULL = -1,
	AITEAM_ASSIGNMENT_TYPE_HEAL = 0,
	AITEAM_ASSIGNMENT_TYPE_SHIELD_HEAL,
	AITEAM_ASSIGNMENT_TYPE_CURE,
	AITEAM_ASSIGNMENT_TYPE_BUFF,
	AITEAM_ASSIGNMENT_TYPE_RESSURECT,
	AITEAM_ASSIGNMENT_TYPE_COUNT
} AITeamAssignmentType;

AUTO_STRUCT;
typedef struct AITeamTimes
{
	S64 lastTick;
	S64 lastDamaged;
	S64 lastBothered;
	S64 lastCheckedSpawnPosDist;
	S64 lastCheckedReinforce;
	S64 lastStartedCombat;
	S64 lastHadLegalTarget;
	S64 lastStartedLeashing;
	S64 lastUpdatedRoamingLeashPoint; // only for updating roaming leash point during waitforcombat
	S64 timeLeashWait;
	S64 lastBuff;
}AITeamTimes;

AUTO_STRUCT;
typedef struct AITeamMember
{
	Entity* memberBE;						AST(UNOWNED)

	EntityRef assignedTarget;

	S64 timeLastActedOn[AITEAM_ASSIGNMENT_TYPE_COUNT];
	S64 timeLastResed;

	F32 healthPct;
	F32 shieldPct;
	
	AICombatRoleTeamMember *pCombatRole;	NO_AST

	// When group combat is enabled, this specifies how long the AI has been active/inactive
	S64 timeCombatActiveStarted;
	S64 timeCombatActiveEnded;
	// When attack tokens create active parties, this specifies when it will expire
	//   If active-state was not attack token generated, this will be 0
	S64 timeCombatActiveExpires;

	// Specifies when this critter can become active again
	S64 timeCombatInactiveExpires;

	// When group combat is enabled, this is an accumulator for power usage
	F32 numCombatTokens;
}AITeamMember;

AUTO_STRUCT;
typedef struct AITeamStatusEntry
{
	EntityRef entRef;
	
	AITeam* team;								AST(STRUCT_NORECURSE)
	AITeamMember** assignedTeamMembers;
	
	S64 timeLastStatusUpdate;	// last time a critter evaluated this in aiUpdateStatusList
	S64 timeLastAggressiveAction;  // Last time a critter transgressed against us
	Vec3 lastKnownPos;

	U32 legalTarget : 1;
}AITeamStatusEntry;

AUTO_ENUM;
typedef enum AITeamCombatState
{
	AITEAM_COMBAT_STATE_AMBIENT,
	AITEAM_COMBAT_STATE_WAITFORFIGHT,
	AITEAM_COMBAT_STATE_STAREDOWN,
	AITEAM_COMBAT_STATE_FIGHT,
	AITEAM_COMBAT_STATE_LEASH,
}AITeamCombatState;


AUTO_STRUCT;
typedef struct AITeamMemberAssignment
{
	AITeamMember *target;
	AITeamAssignmentType type;
	AITeamMember *assignee;
	U32	powID;

	// used for curing
	AttribModDef*	pAttribModDef;
	EntityRef		erSource;
	
	F32 importanceHeur;

	S64	assignedTime;

	U32	validAssignment : 1;
	U32 forcedAssignment : 1; // used for ignoring leashing and other restrictions
} AITeamMemberAssignment;


// union of member's ai power data for quick referencing 
AUTO_STRUCT;
typedef struct AITeamMemberPowers
{
	S32 *eaCureTags;

	U32 hasHealPowers : 1;
	U32 hasShieldHealPowers : 1;
	U32 hasResPowers : 1;
	U32 hasBuffPowers : 1;
	U32 hasCurePower : 1;
	U32 hasTeamOrientedPowers : 1;

} AITeamMemberPowers;

extern ParseTable parse_AITeam[];
#define TYPE_parse_AITeam AITeam

AUTO_STRUCT;
typedef struct AITeam
{
	AITeamCombatState combatState;
	StashTable statusHashTable;				NO_AST
	AITeamStatusEntry**	statusTable;
	AITeamMember** members;
	U32 collId;
	int partitionIdx;
	
	AITeamMemberAssignment	**healAssignments;
	
	U32 teamLeaderRef;

	Entity* teamOwner;						AST(UNOWNED)
	AITeam* reinforceTeam;					AST(UNOWNED)
	AITeamMember* reinforceMember;
	
	int teamLevel;

	F32 curHP;
	F32 maxHP;

	F32 leashDist;

	StashTable teamActions;					NO_AST

	AIJob** jobs;

	F32 teamTotalHealth;

	Vec3 spawnPos;

	Vec3 roamingLeashPoint;

	char *offsetPatrolName;

	F32 trackedDamageTeam[AI_NOTIFY_TYPE_TRACKED_COUNT];

	AITeamTimes time;

	// Updated from each team member, instead of using their individual numbers, to make them
	// aggro simultaneously.
	S32 minSpawnAggroTime;

	AITeamConfig		config;
	AITeamMemberPowers	powInfo;	
	
	AICombatRolesTeamInfo		combatRoleInfo;		NO_AST

	// Do not use this for accessing formation data. Get the formation from the entity's
	// aibase->pFormationData instead. Using this formation pointer will give you the team's
	// formation struct, rather than the pet's owner's formation struct
	AIFormation*				pTeamFormation;		NO_AST

	// Set when team enters combat with combat leash pos
	REF_TO(AIGroupCombatSettings) aigcSettings;		NO_AST

	// Updated per frame with contributions from each team member, then doled out for attacking
	F32 combatTokenAccum;

	// The next time someone in the combat will switch out (only for GroupCombat timing)
	S64 nextCombatSwitch;

	// A list of active group-combat combatants (sub-set of members)
	AITeamMember **activeCombatants;

	// allows the team leash distance to be overridden by a given aiConfig, instead of just always taking the max
	// of all the member's combatMaxProtectDist
	S32 leashDistPriority;

	// 
	// Flags

	U32 memberInCombat : 1;
	U32 calculatedSpawnPos : 1;

	// This is set when a team member joins AFTER spawn pos is calc'd, invalidating any
	// offset logic (or the player is the team owner)
	U32 calcOffsetOnDemand : 1;
	
	U32 roamingLeash : 1;
	U32 roamingLeashPointValid : 1;

	U32 dontAggroInAggroRadius : 1;
	
	U32 noUpdate : 1;
	U32 dontDestroy : 1;

	// This team is currently being considered for reinforcing someone, shouldn't be selected
	// as reinforcement for some other team too
	U32 reinforceCandidate : 1;

	// Team has been reinforced already this combat, so don't check reinforcements again
	U32 reinforced : 1;

	// Don't reinforce others and don't be considered a candidate
	U32 dontReinforce : 1;

	// This team is a combat team
	U32 combatTeam : 1;

	U32 combatSetup : 1;

	// this team has player controlled pets 
	U32 bHasControlledPets : 1;

	// inherited from member's leashOnNonStaticOverride, if any are set the whole team will leash on static maps
	U32 bLeashOnNonStaticOverride : 1;

	// everyone on this team ignores all social aggro pulses
	U32 bIgnoreSocialAggroPulse : 1;

}AITeam;

AUTO_STRUCT WIKI("AIStatusTableEntryDangerValues");
typedef struct AIStatusTableEntryDangerValues
{
	F32 distFromMeVal;
	F32 personalSpaceVal;
	F32 distFromGuardPointVal;
	F32 damageToMeVal;
	F32 damageToFriendsVal;
	F32 statusToMeVal;
	F32 statusToFriendsVal;
	F32 healingEnemiesVal;
	F32 targetStickinessVal;
	F32 teamOrdersVal;
	F32 threatToMeVal;
	F32 threatToFriendsVal;

	F32 targetingRatingExprVal;

	F32 leashDecayScaleVal;
}AIStatusTableEntryDangerValues;

AUTO_STRUCT WIKI("AIStatusTableTimes");
typedef struct AIStatusTableTimes
{
	S64 lastCheckedLOS;
	S64 lastVisible;
	S64 becameAttackTarget;
	S64 enteredAggroRadius;
	S64 lastAggressiveAction;
}AIStatusTableTimes;

AUTO_ENUM;
typedef enum EAINotVisibleReason
{
	EAINotVisibleReason_NONE = 0,
	EAINotVisibleReason_LOS,
	EAINotVisibleReason_OUTOF_PERCEPTION,
	EAINotVisibleReason_PERCEPTION_STEALTH,
	EAINotVisibleReason_UNTARGETABLE
} EAINotVisibleReason;


extern ParseTable parse_AIStatusTableEntry[];
#define TYPE_parse_AIStatusTableEntry AIStatusTableEntry

AUTO_STRUCT WIKI("AIStatusTableEntry");
typedef struct AIStatusTableEntry
{
	EntityRef entRef;

	AITeamStatusEntry* ambientStatus;
	AITeamStatusEntry* combatStatus;

	F32 totalBaseDangerVal;

	F32 maxDistSQR;
	F32 maxCollRadius;
	S64 maxDistSQRCheckTime;

	AIStatusTableEntryDangerValues dangerVal;

	F32* aggroCounters;
	F32* aggroCounterDecay;
	F32* aggroGauges;
	F32* aggroGaugeValues;
	F32 aggroCounterTotal;
	F32 aggroGaugeTotal;

	F32 dangerValWithPower;
	F32 distanceFromMe;
	F32 distanceFromSpawnPos;

	F32 trackedAggroCounts[AI_NOTIFY_TARGET_COUNT][AI_NOTIFY_TYPE_TRACKED_COUNT];	NO_AST
	S64 lastNotify[AI_NOTIFY_TYPE_TRACKED_COUNT];

	AIStatusTableTimes time;

	EAINotVisibleReason		eNotVisibleReason;

	U32 skipCurTick			: 1;
	U32 isAssignedTarget	: 1;
	U32 inAvoidList			: 1;
	U32 inAttractList		: 1;
	U32 inSoftAvoidList		: 1;
	U32 visible				: 1;
	U32 inFrontArc			: 1;
	U32 lostTrack			: 1;  // Visibility lost for > TargetMemoryDuration
	U32 visitedLastKnown	: 1;  // Visibility lost and reached lastKnownPos
	U32 assignedValues		: 1;
}AIStatusTableEntry;

AUTO_STRUCT;
typedef struct AIVarsTime
{
	S64 refreshedProxEnts;
	S64 lastUsedPower;
	S64 lastActivatedPower;
	S64 lastDamage[AI_NOTIFY_TYPE_TRACKED_COUNT];
	S64 lastInitialDamage[AI_NOTIFY_TYPE_TRACKED_COUNT];
	S64 lastInPreferredRange;
	S64 lastTargetUpdate;
	S64 lastPathFind;
	S64 lastCombatMovement;
	S64 startedRunningAway;
	S64 startedSleeping;
	S64 lastTriedToFollow;
	S64 startedGrievedState;
	S64 endedGrievedState;
	S64 pauseUntil;
	S64 lastEnteredCombat;
	S64 lastNearSpawnPos;
	S64 lastHadStaredownTarget;
	S64 lastChangedCombatYVariance;
	S64 timeSpawned;
	S64 timeCutsceneStart;
	S64 timeLeashWait;
	S64 lastHitProxEntsCap;
	S64 lastCombatCoherencyCheck;
	S64 enterCombatWaitTime;
	S64 lastSocialAggroPulse;
	S64 lastHadCombatTarget;
	S64 lastSentAggroUpdate;
	
	// based on the AIVarsBase flag currentlyMoving, 
	// the time at which the AI thought it stopped moving 
	// note: limited to AI think tick fidelity
	S64 timeStoppedCurrentlyMoving;

	int last_hour;
}AIVarsTime;

AUTO_STRUCT;
typedef struct AIMovementTarget
{
	Entity* entityTarget;					AST(UNOWNED)
	Vec3 posTarget;
}AIMovementTarget;


// -----------------------------------------------------
AUTO_ENUM;
typedef enum AIVolumeType
{
	AIVolumeType_AVOID,
	AIVolumeType_ATTRACT,
	AIVolumeType_SOFT_AVOID,
} AIVolumeType;

AUTO_STRUCT;
typedef struct AIVolumeInstance
{
	AIVolumeType		eType;				AST(SUBTABLE(AIVolumeTypeEnum), POLYPARENTTYPE)
	AIVolumeInstance	*next;
	F32					radius;
	U32					uid;
	// The number of entities referring to this volumeInstance
	S32					refCount;
} AIVolumeInstance;

AUTO_STRUCT;
typedef struct AIVolumeAvoidInstance
{
	AIVolumeInstance	base;					AST(POLYCHILDTYPE(AIVolumeType_AVOID))

	S32					maxLevelDiff;
} AIVolumeAvoidInstance;

AUTO_STRUCT;
typedef struct AIVolumeSoftAvoidInstance
{
	AIVolumeInstance	base;					AST(POLYCHILDTYPE(AIVolumeType_SOFT_AVOID))

	S32					magnitude;
} AIVolumeSoftAvoidInstance;

AUTO_STRUCT;
typedef struct AIVolumeAttractInstance
{
	AIVolumeInstance	base;				AST(POLYCHILDTYPE(AIVolumeType_ATTRACT))

} AIVolumeAttractInstance;


AUTO_STRUCT;
typedef struct AIVolumeInfo
{
	AIVolumeType			eType;				AST(SUBTABLE(AIVolumeTypeEnum), POLYPARENTTYPE)

	// list of volumes that the entity has on itself
	AIVolumeInstance*		list;				NO_AST
	F32						maxRadius;

	// list of volumes that the AI is responding to (on other entities)
	AIVolumeEntry**			volumeEntities;		NO_AST

} AIVolumeInfo;

AUTO_STRUCT;
typedef struct AIAvoidInfoPlaced
{
	Vec3				pos;
	F32					maxRadius;
	BeaconAvoidNode**	avoidNodes;					NO_AST
}AIAvoidInfoPlaced;

AUTO_STRUCT;
typedef struct AIVolumeAvoidInfo
{
	AIVolumeInfo		base;					AST(POLYCHILDTYPE(AIVolumeType_AVOID))

	AIAvoidInfoPlaced	placed;
}AIVolumeAvoidInfo;

AUTO_STRUCT;
typedef struct AIVolumeSoftAvoidInfo
{
	AIVolumeInfo		base;					AST(POLYCHILDTYPE(AIVolumeType_SOFT_AVOID))

}AIVolumeSoftAvoidInfo;

AUTO_STRUCT;
typedef struct AIVolumeAttractInfo
{
	AIVolumeInfo		base;					AST(POLYCHILDTYPE(AIVolumeType_ATTRACT))

}AIVolumeAttractInfo;
// -----------------------------------------------------

AUTO_STRUCT;
typedef struct AIDebugLogEntry
{
	AILogType logType;
	U32 logLevel;
	U32 logTag;
	S64 time;
	char logEntry[176];
}AIDebugLogEntry;

AUTO_STRUCT;
typedef struct AIDebugAnimSetting {
	int enabled;
	const char* anim;
	int time;
} AIDebugAnimSetting;

AUTO_STRUCT;
typedef struct AIDebugAnimState{
	AIDebugAnimSetting animSettings[10];		NO_AST

	S64 nextSwitch;
	int curSetting;
} AIDebugAnimState;

// The APC is used by the AI to determine how to use a power, generally coming from an APCDef
AUTO_STRUCT;
typedef struct AIPowerConfig{
	REF_TO(PowerDef) powDef;	AST(STRUCTPARAM, REQUIRED, NON_NULL_REF, ADDNAMES("Power:"), REFDICT(PowerDef))
	F32 absWeight;				AST(ADDNAMES("Weight:"))
	Expression* weightModifier;	AST(NAME("WeightModifier"), REDUNDANT_STRUCT("WeightModifier:", parse_Expression_StructParam), LATEBIND)
	F32 minDist;				AST(ADDNAMES("MinDist:"))
	F32 maxDist;				AST(ADDNAMES("MaxDist:"))
	const char* chainTarget;	AST(ADDNAMES("ChainTarget:"), POOL_STRING)
	F32 chainTime;				AST(ADDNAMES("ChainTime:"))

	Expression* aiRequires;		AST(NAME("AIRequires"), REDUNDANT_STRUCT("AIRequires:", parse_Expression_StructParam), LATEBIND)
	Expression* aiEndCondition;	AST(NAME("AIEndCondition"), REDUNDANT_STRUCT("AIEndCondition:", parse_Expression_StructParam), LATEBIND)
	Expression* chainRequires;	AST(NAME("ChainRequires"), REDUNDANT_STRUCT("ChainRequires:", parse_Expression_StructParam), LATEBIND)
	Expression* targetOverride;	AST(NAME("TargetOverride"), REDUNDANT_STRUCT("TargetOverride:", parse_Expression_StructParam), LATEBIND)
	Expression* cureRequires;	AST(NAME("CureRequires"), REDUNDANT_STRUCT("CureRequires:", parse_Expression_StructParam), LATEBIND)
	S32* curePowerTags;			AST(ADDNAMES("CureTags"), SUBTABLE(PowerTagsEnum))	
	F32 maxRandomQueueTime;		AST(ADDNAMES("MaxRandomQueueTime:"))

	bool bChainLocksFacing;		AST(NAME("ChainLocksFacing"))
	bool bChainLocksMovement;

	const char* filename;		AST(CURRENTFILE)
}AIPowerConfig;

AUTO_STRUCT;
typedef struct AIPowerConfigList{
	char* name;						AST(STRUCTPARAM, NAME("Name:"), KEY)
	char* filename;					AST(CURRENTFILE)
	F32 prefMinRange;				AST(NAME("PrefMinRange:"))
	F32 prefMaxRange;				AST(NAME("PrefMaxRange:"))
	F32 outOfPrefRangeTolerance;	AST(NAME("OutOfPrefRangeTolerance:"))
	AIPowerConfig** entries;		AST(NAME("AIPowerConfig:"))
}AIPowerConfigList;

AUTO_ENUM;
typedef enum AIPowerActionType{
	AI_POWER_ACTION_USE_POWINFO = 0,
	AI_POWER_ACTION_HEAL,
	AI_POWER_ACTION_SHIELD_HEAL,
	AI_POWER_ACTION_RES,
	AI_POWER_ACTION_BUFF,
	AI_POWER_ACTION_OUT_OF_COMBAT,
	AI_POWER_ACTION_GOTOPOS,
	AI_POWER_ACTION_ROLL,
	AI_POWER_ACTION_SAFETY_TELEPORT,
}AIPowerActionType;

AUTO_STRUCT;
typedef struct AIPowerInfo{
	Power* power;						AST(LATEBIND)
	CritterPowerConfig* critterPow;
	
	// always call aiGetPowerConfig to get the power config!
	REF_TO(AIPowerConfig) powConfig;
	AIPowerConfig* defaultPowConfig;	// this gets used if there is no entry for this power
										// in the current config
	// This is used when the PowerConfig has been modified at run time
	AIPowerConfig *localModifiedPowConfig;

	// These are the modifiers
	StructMod **powerConfigMods;

	EntityRef curOverrideTargetRef;
	S64 overrideTargetTime;

	F32 curBonusWeight;
	F32 curRating;

	U32 timesUsed;
	S64 lastUsed;
	
	F32 powerTimeRecharge;
	// If the power isAEPower, this is valid 
	F32 areaEffectRange;

	U32 aiTagBits;
	U32 isAttackPower : 1;
	U32 isHealPower : 1;
	U32 isShieldHealPower : 1;
	U32 isBuffPower : 1;
	U32 isCurePower : 1;
	U32 isTargetCreatorPower : 1;
	U32 isTargetOwnerPower : 1;
	U32 isLungePower : 1;
	U32 isResPower : 1;
	U32 isOutOfCombatPower : 1;
	U32 isFlyPower : 1;
	U32 isArcLimitedPower : 1;
	U32 isControlPower : 1;
	U32 isAEPower : 1;

	U32 isSelfTarget : 1;
	U32 isSelfTargetOnly : 1;
	U32 isAfterDeathPower : 1;
	U32 isDeadOrAlivePower : 1;

	U32 isInterruptedOnMovement : 1;
	U32 canAffectSelf : 1;
	U32 resetValid : 1;
}AIPowerInfo;

// callback used when the MTA clears or uses the power
typedef void (*MultiTickActionClearedCallback)(Entity *e, Power *power, int bPowerUsed);

AUTO_STRUCT;
typedef struct AIPowerMultiTickAction
{
	EntityRef targetRef;
	Vec3 targetPos;
	AIPowerActionType type;
	AIPowerInfo* powInfo;
	MultiTickActionClearedCallback completedCB;	NO_AST
	CommandQueue* pAnimQueue;					NO_AST
	S64	timer;

	S32 iForcedActionPriority; // used for player induced or critical actions

	U32 usedPower : 1;
	U32 stoppedMoving : 1;
	U32 hostileTarget : 1;
	U32 forceUseTarget : 1;
	U32 finishedAction : 1;
	U32 combatNoMove : 1;
}AIPowerMultiTickAction;

AUTO_STRUCT;
typedef struct AIQueuedPower
{
	AIPowerInfo* powerInfo;
	EntityRef targetRef;
	EntityRef secondaryTargetRef; 
	AIPowerInfo* chainPowerInfo;
	Vec3 targetPos;					// used when targetRef and secondaryTargetRef are invalid 
	S64 execTime;
	
	U8 validTargetPos : 1;
}AIQueuedPower;

AUTO_STRUCT;
typedef struct AIPowerEntityInfo {
	REF_TO(AIPowerConfigList) powerConfigList;

	AIPowerInfo** powInfos;
	AIPowerInfo* preferredPower;
	//AIPowerInfo* bestInRangePower;
	AIPowerInfo* lastUsedPower;

	AIQueuedPower** queuedPowers;

	AIPowerMultiTickAction **ppMultiTickActionQueue;

	S64 notAllowedToUsePowersUntil;

	U32 totalUses;

	U32 preferredAITagBit;

	// the current range of our healing power(s)
	F32 currHealingRange;
	

	U32 validPowerConfig : 1;	// If any of the current powers are found in the power config, ignore defaults

	U32 hasAttackPowers : 1;
	U32 hasHealPowers : 1;
	U32 hasShieldHealPowers : 1;
	U32 hasBuffPowers : 1;
	U32 hasCurePower : 1;
	U32 hasLungePowers : 1;
	U32 hasResPowers : 1;
	U32 hasFlightPowers : 1;
	U32 alwaysFlight : 1;
	U32 hasAfterDeathPowers : 1;
	U32 hasDeadOrAlivePowers : 1;
	U32 hasArcLimitedPowers : 1;
	U32 hasControlPowers : 1;
}AIPowerEntityInfo;

AUTO_STRUCT;
typedef struct AIRelativeLoc {
	EntityRef entRef;
	F32 range;
	AIRelativeLocList *list;
} AIRelativeLoc;

AUTO_STRUCT;
typedef struct AIRelativeLocList {
	AIRelativeLoc **locs;
} AIRelativeLocList;

AUTO_STRUCT;
typedef struct AIOfftickInstance {
	AIOfftickConfig *otc;

	U32 executed;
	U32 executedThisCombat;
	U32 executedThisTick;
	
	U32 initialized : 1;
	U32 activeFine : 1;
} AIOfftickInstance;

AUTO_STRUCT;
typedef struct AIOutOfCombatVars {
	U32 resOOCTarget : 1;
	U32 healOOCTarget : 1;
	U32 buffOOCTarget : 1;
	S64 timeLastResed;
	S64 timeLastHealed;
	S64 timeLastBuffed;
} AIOutOfCombatVars;

AUTO_ENUM;
typedef enum AILeashState
{
	AI_LEASH_STATE_START,
	AI_LEASH_STATE_FINISH,
	AI_LEASH_STATE_DONE,
}AILeashState;


AUTO_STRUCT;
typedef struct AIStanceConfigMod
{
	U32* configMods;
	U32* powerConfigMods; 
} AIStanceConfigMod;

AUTO_STRUCT;
typedef struct AIAutoCastPowers
{
	U32 configModId;
	U32 powId;
} AIAutoCastPowers;

AUTO_STRUCT;
typedef struct AIOverrideFSM
{
	FSMContext* fsmContext;
	U32			id;
} AIOverrideFSM;

extern ParseTable parse_AIVarsBase[];
#define TYPE_parse_AIVarsBase AIVarsBase

AUTO_STRUCT;
typedef struct AIVarsBase
{
	REF_TO(AIConfig) config_use_accessor;
	AIConfig* localModifiedAiConfig;
	StructMod** configMods;

	S64 lastThinkTick;
	S64 thinkAcc;

	CommandQueue* nextTickCmdQueue;			NO_AST
	StashTableImp* offtickInstances;		NO_AST

	EntAndDist* proxEnts;					NO_AST
	int proxEntsCount;
	int proxEntsMaxCount;

	// updated with the powers value at the start of aiUpdateProxEnts
	F32 awareRatio;
	F32 awareRadius;
	F32 aggroRadius;
	F32 proximityRadius;

	F32 baseDangerFactor; // multiplier for base danger assignments

	F32 totalTrackedDamage[AI_NOTIFY_TYPE_TRACKED_COUNT];

	// attackTargetRef is valid across tick boundaries, where attackTarget
	// gets set at the start of the AI tick and might be invalid after that tick ends
	EntityRef attackTargetRef;
	union {
		Entity*			attackTargetMutable;			AST(UNOWNED NAME("attackTarget"))
		Entity*const	attackTarget;					NO_AST
	};

	F32 attackTargetDistSQR;
	AIStatusTableEntry* attackTargetStatus;
	AIStatusTableEntry* attackTargetStatusOld;
	AIDebugLocRating **ratings;		

	// Preferred target is just whom, if he's a legal target, I will attack, regardless of weights
	EntityRef preferredTargetRef;
	
	// Out of Combat powers stuff
	AIOutOfCombatVars ooc;

	// A list of locations around the entity that critters can target moving to
	AIRelativeLoc *myRelLoc;				NO_AST
	AIRelativeLocList **relLocLists;		NO_AST
	F32 lastLocRating;						NO_AST

	Vec3 spawnPos;
	Quat spawnRot;
	Vec3 spawnOffset;
	Beacon* spawnBeacon;					NO_AST
	Vec3 ambientPosition;

	AILeashState leashState;
	CommandQueue* leashAnimQueue;			NO_AST
	CommandQueue* notVisibleTargetAnimQueue;NO_AST
	U32 leashSpeedConfigHandle;				NO_AST
	U32 leashTurnRateConfigHandle;			NO_AST
	U32 leashTractionConfigHandle;			NO_AST
	U32 leashFrictionConfigHandle;			NO_AST

	AIVarsTime time;

	AITeam* team;
	AITeamMember* member;
	AITeam* combatTeam;						// Only valid when in combat
	AITeamMember* combatMember;
	EntityRef reinforceTarget;
	
	MovementRequester* movement;			NO_AST

	AIStatusTableEntry** statusTable;
	StashTableImp* statusHashTable;			NO_AST

	EntityRef* statusCleanup;				NO_AST

	union {
		Entity**			attackerListMutable;		AST(UNOWNED NAME("attackerList"))
		Entity*const*const	attackerList;				NO_AST
	};

	F32 aggroHistory[AI_MAX_AGGRO_HISTORY_LENGTH];
	U32 aggroHistoryIdx;

	F32 aggroBotheredAvg;

	U32 combatRunAwayCount;
	F32 combatCurYVariance;
	F32 minGrievedHealthLevel;

	FSMContext* fsmContext;					NO_AST
	FSMContext* combatFSMContext;			NO_AST
	FSMContext* currentCombatFSMContext;	NO_AST
	AIOverrideFSM **eaOverrideFSMStack;		NO_AST
	ExprContext* exprContext;				NO_AST
	StashTable fsmMessages;					NO_AST

	AIJob* job;

	CommandQueue* onDeathCleanup;			NO_AST

	AIPowerEntityInfo* powers;
	int *powersModes;

	AIDebugLogEntry** logEntries;
	int curLogEntry;
	AIDebugWaypoint** debugCurPath;
	int debugCurWp;
	S64 timeDebugCurPathUpdated;

	Vec3 lastShortcutCheckPos;
	S64 lastShortcutCheckTime;

	AIStanceConfigMod	**stanceConfigMods;
	U32					*stateConfigMods;
	AIAutoCastPowers	**autocastPowers;
		
	AIVolumeAvoidInfo avoid;	
	AIVolumeSoftAvoidInfo softAvoid;
	AIVolumeAttractInfo attract;
	
	AICivCalloutInfo* calloutInfo;			NO_AST

	const char **messageListens;			NO_AST

	F32 minDynamicPrefRange;
	F32 maxDynamicPrefRange;

	AILeashType	leashType;

	// Used by the designers to override the leash type. If this is set to AI_LEASH_TYPE_DEFAULT, there is no override.
	AILeashType	leashTypeOverride;

	EntityRef	erLeashEntity;
	Vec3		rallyPosition;				// when told to hold a position
	
	// This is only valid if leashTypeOverride is set to AI_LEASH_TYPE_RALLY_POSITION
	Vec3		rallyPositionOverride;
	F32			coherencyCombatDistOverride;
	
	AIMovementModeManager *movementModeManager; NO_AST	

	const char	*pchCombatRole;
	AICombatRoleAIVars *combatRoleVars;			NO_AST
	AIFormationData *pFormationData;			NO_AST

	AIMastermindAIVars *mastermindVars;			NO_AST
		
	U32			hConfigSpeedOverride;

	// what entities I am sharing my aggro with
	EntityRef *eaSharedAggroEnts;

	// list of targets that will get an overridden aggro radius 
	// if aiConfig's seekTargetOverrideAggroRadius is set
	EntityRef *eaSeekTargets;

	// Local data for storing expression metadata
	ExprLocalData** localData;

	// For debugging animlists
	AIDebugAnimState *aidAnimState;

	// Combat Jobs
	GameInteractable *pTargetCombatJobInteractable;					NO_AST	// The combat job interactable critter is occupying
	GameInteractLocation *pTargetCombatJobInteractLocation;			NO_AST	// The combat job location critter is occupying
	Vec3 vecTargetCombatJobPos;												// Position of the target combat job

	// A target position usually set when created through powers entCreate
	Vec3 vecTargetPowersEntCreate;					

	// respawn data 
	U32	uiRespawnApplyID;
	S64 lastRespawnTime;

	AIEntityBucket *entBucket;					NO_AST

	// static random seed
	U32 uiRandomSeed;

	// Hit and wait combat style
	BrawlerCombatData		*pBrawler;			NO_AST

	U32 destroying							: 1;
	U32 settingAttackTarget					: 1;
	U32 useDynamicPrefRange					: 1; // whether to use dynamic preferred range calculations
	U32 doProximity							: 1;
	U32 doBScript							: 1;
	U32 doAttackAI							: 1;
	U32 doDeathTick							: 1;
	U32 disableAI							: 1;
	U32 isNPC								: 1;
	U32 behaviorRequestOffTickProcessing	: 1;
	U32 overriddenCombat					: 1;
	U32 currentlyMoving						: 1;
	U32 leavingAvoid						: 1; // Basically, ignore all movement requests while this is true
	//U32 bothered							: 1;
	U32 dontSleep							: 1; // move to AIConfig after selective overriding
	U32 sleeping							: 1;
	U32 hadFirstTick						: 1;
	U32 confused							: 1;
	U32 grieved								: 1;
	U32 wandering							: 1;
	U32 inCombat							: 1;
	U32 insideCombatFSM						: 1;	
	U32 noCombatMovementInCombatFSM			: 1;
	U32 checkedSpawnBeacon					: 1;
	U32 healing								: 1;
	U32 isHostileToCivilians				: 1;
	U32 failedToLeash						: 1;
	U32 debugPath							: 1;
	U32 skipOnEntry							: 1; // Used to do onEntry before first tick, and first action on first tick
	U32 targetListen						: 1;
	U32 determinedSpawnPoint				: 1;
	U32 calculatedSpawnOffset				: 1;
	U32 spawnOffsetDirtied					: 1;
	U32	hitProxEntsCivCap					: 1;
	U32 forceThinkTick						: 1;
	U32 preferredTargetIsEnemy				: 1;
	U32 isSummonedAndExpires				: 1;
	U32	untargetable						: 1; // updated via aiConfig
	U32 combatRolesDirty					: 1;
	U32 ambientJobFSMDone					: 1; // when using an ambient job FSM, and the job is now done executing
												 // (might be a neater way of doing this)
	U32 disableAmbientTether				: 1;
	U32 lastAttackActionAllowedMovement		: 1;
	U32 thinkTickDebugged					: 1;
	
	// Combat Jobs
	U32 insideCombatJobFSM					: 1; // Indicates whether we are executing inside a combat job FSM
	U32 noCombatMovementInCombatJobFSM		: 1; // Indicates whether the critter should move while executing the combat FSM
	U32 movingToCombatJob					: 1; // Indicates whether the critter is moving towards a combat job
	U32 movementOrderGivenForCombatJob		: 1; // Indicates whether the critter is given the movement order to reach the combat job
	U32 reachedCombatJob					: 1; // Indicates whether the critter reached a combat job
	U32 combatJobFSMDone					: 1; // when using an ambient job FSM, and the job is now done executing
	U32 encounterJobFSMDone					: 1; // when using an encounter job FSM, and the job is now done executing
	U32 chainPowerExecutionActive			: 1; // when set the rotation is enabled when the power execution finishes
	U32 chainLockedFacing					: 1; // Indicates that the chain power has locked facing
	U32 chainLockedMovement					: 1; // Indicates that the chain power has locked facing

}AIVarsBase;

// Combat position slot relative to a target
AUTO_STRUCT;
typedef struct CombatPositionSlot
{
	// positive X is to the right
	F32 fXOffset;

	// positive Z is forward
	F32	fZOffset;

	// The radius of the slot. Used to check if an entity is occupying this slot
	F32 fRadius;
} CombatPositionSlot;


AUTO_STRUCT;
typedef struct AIGlobalSettings {
	F32 reinforceLevels[4];  AST(INDEX(0,reinforce2) INDEX(1,reinforce3) INDEX(2,reinforce4) INDEX(3,reinforce5))

	F32 reinforceTeamDist;  

	F32 leashHealRate;

	// About to remove this setting in favor of flags on the AIConfig
	F32 DEPRECATED_meleeCombatMovementDistance; AST(ADDNAMES(meleeCombatMovementDistance))

	// if defined, will force the AI to use melee combat positioning if the desired range to be in with the target is less than 
	// the AIGlobalSettings forceMeleeCombatMovementThreshold, see below.
	U32 forceMeleeCombatMovement : 1;			AST(ADDNAMES(forceFiveFootMeleeRange))
	
	// If forceMeleeCombatMovement is defined, if the target is within this range do melee movement
	// todo: instead change other projects data to define this to 0 if not needed
	F32 forceMeleeCombatMovementThreshold;		AST(DEFAULT(5))


	// Maximum melee distance
	F32 meleeMaximumDistance;					AST(DEFAULT(10))
	
	// if set, the speed at which the team members will move at to get into formation during the staredown
	F32 combatRoleFormationOverrideMovespeed;

	// for the dynamic preferred range calculation, if the recharge time for the power exceeds this value
	// power will not be counted towards the dynamic preferred range
	F32 dynPrefRange_powerRechargeTimeThreshold;	AST(DEFAULT(5))

	// The time an AI critter ignores players when they spawn
	S32 iIgnorePlayerAtSpawnTimeout;				AST(DEFAULT(10))

	// Should we use aggroexpressions
	bool useAggroExprFile;
	
	// default ambient settings
	char *aiAmbientDefaults;		AST(NAME(AIAmbientDefaults) DEFAULT("Default"))

	// default combat job settings
	char *aiCombatJobDefaults;		AST(NAME(AICombatJobDefaults) DEFAULT("Default"))

	// default group combat settings
	const char* aiGroupCombatDefaults;		AST(NAME("AIGroupCombatDefaults:"))

	const char* pchDefaultPlayerAIConfig;	AST(NAME("DefaultPlayerAIConfig:"))

	// if set, when an AI's target has become invisible due to perception stealth, play this AnimList on the critter
	const char* pchStealthReactAnimList;	AST(NAME("StealthReactAnimList:"))
	

	BrawlerCombatGlobalConfig *pBrawlerConfig;

	// Possible combat position slots
	CombatPositionSlot	**eaCombatPositionSlots;			AST(NAME("CombatPositionSlot"))

	F32 fCombatPositionOccupancyCheckSensitivity;			AST(NAME("OccupancyCheckSensitivity"))

	// How many angle graduations there are around a character, used by the AI and in NNO
	S32 iCombatAngleGranularity;							AST(DEFAULT(10))

	// Range from target to care about for positional checks, used by the AI and in NNO
	F32 fCombatPositionRangeSensitivity;					AST(DEFAULT(30))

	// how many levels higher than me must a player be for me to ignore it?
	int iAggroIgnoreLevelDelta;								AST(DEFAULT(5))

	// the time since the critter has used its last power that it will stop facing its target
	F32 fCombatInactionFaceTargetTime;						AST(DEFAULT(3))
	
	// Use aggro2 instead of old aggro
	U32 useAggro2 : 1;

	U32 leashDontClearMods : 1;
	U32 leashDontRechargePowers : 1;
	U32 leashDontRemoveStatusEffects : 1;

	U32 forceReinforcement : 1;

	// Flags to disable random things
	U32 disableLeashMessage : 1;

	// Adds a configmod to turn off leashing on non-static maps
	U32 disableLeashingOnNonStaticMaps : 1;

	// Use only credited damage/healing for aggro (i.e. ignore overkilling and overhealing)
	U32 disableOverageAggro : 1;				AST(DEFAULT(1))


	// Determines whether the global recharge time is based off when the 
	//  power begins (used) or ends (activated) activation
	U32 globalRechargeFromStart : 1;			AST(DEFAULT(1))

	// Tells the AI not to check for whether it should use powers between AI ticks
	//  This is for CO-backwards compatibility
	U32 disableInfraTickPowerUsage : 1;

	// Tells the AI not to check for whether powers should be used when a power notification comes through
	U32 disableOnExecutePowerUsage : 1;

	// Tells the AI to ignore disableOnExecutePowerUsage when the entity is a player-owned entity
	U32 overrideOnExecutePowerUsageForPlayerPets : 1;

	// Determines whether powers without critter power configs should just use
	//  a default power config
	U32 allowPowersWithoutConfigs : 1;

	// Specifies that the system should use combat teams
	U32 enableCombatTeams : 1;

	// Tells the system that aggro should be pulsed periodically for critters in combat
	U32 enableSocialAggroPulse : 1;

	// if within the aggro range, this makes critters aggro on the entity 
	// not being visible due to Perception stealth (only if the perception radius goes to 0)
	U32 aggroOnPerceptionStealth : 1;

	// Determines whether a critter goes powers-untargetable (unattackable) when leashing
	U32 untargetableOnLeash : 1;

	// Determines whether a critter goes unselectable (UI-untargetable) when leashing
	U32 unselectableOnLeash : 1;

	// Tells aggro system that healing oneself should count as aggro
	U32 selfHealingCountsAsAggro : 1;

	// Implements bad timing that Champs /release had; delete when possible
	U32 useFCRPowerTiming : 1;

	U32 useCombatRoleDamageSharing : 1;

	U32 stealthAffectsOutOfFOVRange : 1;

	// if set, when inside a combatFSM, entering/exiting a state with Combat() will not trigger exit handlers
	U32 combatFSMKeepsInCombat : 1;

	// If set, when someone does damage, do not automatically add the player teammates as legal targets
	U32 dontAutoLegalTargetTeammates : 1;

	//If true, the spawn aggro timeout will be applied to critters in all situations
	U32 alwaysUseSpawnAggroDelay : 1;

	// if true, untargetable/unselectable entities that were legal targets already are treated as not visible
	U32 untargetableIsTreatedNotVisible : 1;
		
} AIGlobalSettings;

typedef struct AIEntityBucket {
	S64 timeLastUpdate;

	StashTable entsByRef;
} AIEntityBucket;

typedef struct AIPartitionState {
	int idx;
	bool bCheckForFrozenEnts;

	AIVolumeEntry** envAvoidEntries;
	AIVolumeEntry** envAvoidEntriesOld; // EArray to put entries when they get reloaded
	BeaconAvoidNode** envAvoidNodes;

	// 30 Hz per frame, half second check = 15 buckets
	AIEntityBucket entBuckets[15];

	// These ents must be updated every frame until something changes.  Conditions that can make this occur:
	// offTick updates, forceThinkTick, having a power queued
	AIEntityBucket priorityEntBucket;
} AIPartitionState;

typedef struct AIMapState {
	AIPartitionState **partitions;

	EntityRef *needsInit;
} AIMapState;

extern AIMapState aiMapState;
extern AIGlobalSettings aiGlobalSettings;

// Gets the current active AIConfig for the specified critter. Needed to allow base AIConfigs
// and overridden AIConfigs to coexist peacefully
static __forceinline AIConfig* aiGetConfig(Entity* be, AIVarsBase* aib)
{
	if(aib->localModifiedAiConfig)
		return aib->localModifiedAiConfig;
	else
	{
		AIConfig* config = GET_REF(aib->config_use_accessor);

		if(config)
			return config;

		config = RefSystem_ReferentFromString("AIConfig", "Default");
		devassertmsg(config, "Could not find default config to fall back on (and current critter's aiconfig is invalid)");
		return config;
	}
}

