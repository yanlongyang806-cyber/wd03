#pragma once
GCC_SYSTEM

#include "MultiVal.h"

#include "aiStructCommon.h"

#define AID_MAX_ANIMS 10

AUTO_ENUM;
typedef enum AIDebugFlags
{
	AI_DEBUG_FLAG_BASIC_INFO	= 1 << 0,		ENAMES(basicInfo)
	AI_DEBUG_FLAG_STATUS_TABLE	= 1 << 1,		ENAMES(statustable)
	AI_DEBUG_FLAG_STATUS_EXTERN = 1 << 2,		ENAMES(statusextern)
	AI_DEBUG_FLAG_POWERS		= 1 << 3,		ENAMES(powers)
	AI_DEBUG_FLAG_MOVEMENT		= 1 << 4,		ENAMES(movement)
	AI_DEBUG_FLAG_LOC_RATINGS	= 1 << 5,		ENAMES(ratings)
	AI_DEBUG_FLAG_TEAM			= 1 << 6,		ENAMES(team)
	AI_DEBUG_FLAG_COMBATTEAM	= 1 << 7,		ENAMES(cteam)
	AI_DEBUG_FLAG_FORMATION		= 1 << 8,		ENAMES(formation)
	AI_DEBUG_FLAG_HEIGHT_CACHE	= 1 << 9,		ENAMES(heightcache)
	AI_DEBUG_FLAG_VARS			= 1 << 10,		ENAMES(vars)
	AI_DEBUG_FLAG_EXVARS		= 1 << 11,		ENAMES(xvars)
	AI_DEBUG_FLAG_MSGS			= 1 << 12,		ENAMES(msgs)
	AI_DEBUG_FLAG_CONFIG_MODS	= 1 << 13,		ENAMES(configmods)
	AI_DEBUG_FLAG_AVOID			= 1 << 14,		ENAMES(avoid)
	AI_DEBUG_FLAG_AGGRO			= 1 << 15,		ENAMES(aggro)
	AI_DEBUG_FLAG_LOG			= 1 << 16,		ENAMES(logs)
	AI_DEBUG_FLAGS_ALL			= 0xFFFFFFFF,	ENAMES(all)
}AIDebugFlags;


AUTO_STRUCT;
typedef struct AIDebugBasicInfo
{
	char* str;
}AIDebugBasicInfo;

AUTO_STRUCT;
typedef struct AIDebugVarEntry
{
	const char* name;
	char *value;
	const char* origin;		AST(POOL_STRING)
} AIDebugVarEntry;

AUTO_STRUCT;
typedef struct AIDebugMsgEntry
{
	const char* name;
	F32 timeSince;
	int count;
	char *sources;
	char *attachedEnts;
} AIDebugMsgEntry;

AUTO_STRUCT;
typedef struct AIDebugStringStringEntry
{
	char* name;
	char* val;
} AIDebugStringStringEntry;


AUTO_STRUCT;
typedef struct CharacterAITargetInfo
{
	EntityRef entRef;
	F32 totalBaseDangerVal;
	F32 relativeDangerVal; // Divided by the MAX danger
} CharacterAITargetInfo;

extern ParseTable parse_CharacterAITargetInfo[];
#define TYPE_parse_CharacterAITargetInfo CharacterAITargetInfo

AUTO_STRUCT;
typedef struct AIDebugAggroBucket
{
	F32			fValue;				
	F32			fGauge;			
} AIDebugAggroBucket;

extern ParseTable parse_AIDebugAggroBucket[];
#define TYPE_parse_AIDebugAggroBucket AIDebugAggroBucket


AUTO_STRUCT;
typedef struct AIDebugStatusTableEntry
{
	char* name;
	int index;
	EntityRef entRef;
	
	AIDebugAggroBucket	**eaAggroBuckets;
	F32 aggroCounterTotal;
	F32 aggroGaugeTotal;
	F32 totalBaseDangerVal;
	
	U8 inFrontArc : 1;
	U8 legalTarget : 1;
	U8 taunter : 1;
}AIDebugStatusTableEntry;

AUTO_STRUCT;
typedef struct AIDebugAggroTableHeader
{
	const char *pchName;			AST(POOL_STRING)
	U32			isGauge : 1;
} AIDebugAggroTableHeader;

extern ParseTable parse_AIDebugAggroTableHeader[];
#define TYPE_parse_AIDebugAggroTableHeader AIDebugAggroTableHeader

AUTO_ENUM;
typedef enum AIDebugWaypointType
{
	AI_DEBUG_WP_SHORTCUT,
	AI_DEBUG_WP_GROUND,
	AI_DEBUG_WP_JUMP,
	AI_DEBUG_WP_OTHER,
}AIDebugWaypointType;

AUTO_STRUCT;
typedef struct AIDebugWaypoint
{
	AIDebugWaypointType type;
	Vec3 pos;
}AIDebugWaypoint;

// different because I wanted a time in seconds based on the server and I'm not sure how to get
// server ABS_TIME_PARTITION(partitionIdx) on the client if that's possible at all :)
AUTO_STRUCT;
typedef struct AIDebugLogEntryClient
{
	F32 timeInSec;
	char* str;
}AIDebugLogEntryClient;

AUTO_STRUCT;
typedef struct AIDebugPowersInfo
{
	char* powerName;
	F32 rechargeTime;
	F32 curRating;
	F32 aiMinRange;
	F32 aiMaxRange;
	F32 absWeight;
	F32 modifierWeight;
	S64 lastUsed;
	U32 timesUsed;
	char* tags;
	char* aiExpr;
	char* chainTarget;
}AIDebugPowersInfo;

AUTO_STRUCT;
typedef struct AIDebugMovementInfo
{
	Vec3 curPos;
	Vec3 targetPos;
	int curWp;
	AIDebugWaypoint** curPath;
	Vec3 splineTarget;
}AIDebugMovementInfo;

AUTO_STRUCT;
typedef struct AIDebugTeamMember
{
	EntityRef ref;
	Vec3 pos;
	char* job_name;
	char* critter_name;
	char* role_name;
	F32 combatTokens;
	F32 combatTokenRateSelf;
	F32 combatTokenRateSocial;
} AIDebugTeamMember;

AUTO_ENUM;
typedef enum AIDebugTeamAssignmentType
{
	AI_DEBUG_TEAM_ASSIGNMENT_TYPE_HEAL,
	AI_DEBUG_TEAM_ASSIGNMENT_TYPE_SHIELD_HEAL,
} AIDebugTeamAssignmentType;

AUTO_STRUCT;
typedef struct AIDebugTeamMemberAssignment
{
	char* targetName;
	AIDebugTeamAssignmentType	type;
	char* assigneeName;
	char* powerName; 
} AIDebugTeamMemberAssignment;

AUTO_STRUCT;
typedef struct AIDebugTeamInfo
{
	AIDebugTeamMember** members;
	AIDebugBasicInfo** teamBasicInfo;
	AIDebugTeamMemberAssignment** healingAssignments;
} AIDebugTeamInfo;


AUTO_STRUCT;
typedef struct AIDebugLocRating
{
	F32 arcDistance;
	F32 clumping;
	F32 softAvoid;
	F32 avoidOccupiedPositions;
	F32 stayTogether;
	F32 flanking;
	F32 targetShields;
	F32 turnWeakShieldTo;
	F32 turnStrongShieldTo;
	F32 turnWeakShieldToMostDamage;
	F32 preferArcLimitedLocations;
	F32 yOffset;

	F32 sameLocPenalty;
	U32 rayCollResult;

	U32 bestThisTick : 1;

	// Only valid when combat position slots are used
	S32 combatPosSlotIndex;
	Vec3 vCombatPosSlot;
} AIDebugLocRating;

extern ParseTable parse_AIDebugLocRating[];
#define TYPE_parse_AIDebugLocRating AIDebugLocRating

AUTO_STRUCT;
typedef struct AIDebugFormationPosition
{
	Vec3 offset;
	EntityRef assignee;

	U32 blocked : 1;
} AIDebugFormationPosition;

AUTO_STRUCT;
typedef struct AIDebugFormation
{
	Vec3 formationPos;
	AIDebugFormationPosition **positions;
} AIDebugFormation;

AUTO_STRUCT;
typedef struct AIDebugAvoidBcn
{
	Vec3 pos;
	U32 avoid : 1;			AST(NAME("Avoid"))
} AIDebugAvoidBcn;

AUTO_STRUCT;
typedef struct AIDebugAvoidVolume
{
	Vec3	vPos;
	Mat4	mtxBox;
	Vec3	vBoxMin;
	Vec3	vBoxMax;
	F32		fRadius;
} AIDebugAvoidVolume;

AUTO_STRUCT;
typedef struct AIDebugAvoid
{
	AIDebugAvoidBcn **bcns;
	AIDebugAvoidVolume **volumes;
} AIDebugAvoid;

AUTO_STRUCT;
typedef struct AIDebugAggro
{
	F32 defAggro;
	F32 defAware;
	F32 defSocPrim;
	F32 defSocSec;
	
	U32 socialEnabled : 1;
} AIDebugAggro;

AUTO_STRUCT;
typedef struct AIDebugPerEntity
{
	EntityRef myRef;

	F32 aggro;
	F32 aware;
	F32 socPrim;
	F32 socSec;
} AIDebugPerEntity;

AUTO_STRUCT;
typedef struct AIDebugSettings 
{
	AIDebugFlags flags;
	U32 logSettings[AI_LOG_COUNT];

	EntityRef debugEntRef;
	const char* layerFSMName;						AST(POOL_STRING)
	const char* pfsmName;							AST(POOL_STRING)

	U32 updateSelected : 2;
} AIDebugSettings;
extern ParseTable parse_AIDebugSettings[];
#define TYPE_parse_AIDebugSettings AIDebugSettings

AUTO_STRUCT;
typedef struct AIDebug
{
	AIDebugSettings				settings;

	EntityRef attackTargetRef;

	AIDebugBasicInfo**			basicInfo;

	AIDebugAggroTableHeader**	eaAggroTableHeaders;
	AIDebugStatusTableEntry**	debugStatusEntries;
	AIDebugStatusTableEntry**	debugStatusExternEntries;

	AIDebugPerEntity**			entInfo;
	AIDebugLogEntryClient**		logEntries;
	AIDebugBasicInfo**			powerBasicInfo;
	AIDebugPowersInfo**			powersInfo;
	AIDebugTeamInfo*			teamInfo;
	AIDebugTeamInfo*			combatTeamInfo;
	AIDebugVarEntry**			varInfo;
	AIDebugVarEntry**			exVarInfo;
	AIDebugMsgEntry**			msgInfo;
	AIDebugMovementInfo*		movementInfo;
	AIDebugLocRating**			locRatings;
	AIDebugFormation*			formation;
	AIDebugStringStringEntry**	configMods;
	AIDebugAvoid*				avoidInfo;
	AIDebugAggro*				aggroInfo;
}AIDebug;
