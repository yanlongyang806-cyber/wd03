#ifndef PVP_COMMON_H
#define PVP_COMMON_H

GCC_SYSTEM

#include "stdtypes.h"
#include "GlobalTypes.h"
#include "entEnums.h"
#include "Message.h"
#include "accountCommon.h"

typedef struct Entity Entity;
typedef struct PVPDuel PVPDuel;

AUTO_STRUCT;
typedef struct ScoreboardEntity
{
	ContainerID iEntID;			AST(KEY)
	EntityRef entRef;
	char *pchName;
	char *pchAccountName;
	const char *pchFactionName;	AST(POOL_STRING)
	S32 iPoints;
	S32 iPlayerKills;
	S32 iPlayerAssists;
	S32 iPlayerHealing;
	S32 iPlayerDamage;
	S32 iDeathsToPlayers;
	S32 iPlayerTime;
	S32 iBossKills;
	S32 iDeathsToBosses;
	S32 iTotalKills;
	S32 iDeathsTotal;
	S32 iPlayerAssaultTeams;
	U32 bActive : 1;

	//Used to show class icons
	char *pchCharacterPathName;
} ScoreboardEntity;

AUTO_STRUCT;
typedef struct ScoreboardMetricEntry
{
	ContainerID iEntID;
	char pchName[MAX_NAME_LEN];
	char pchAccountName[MAX_ACCOUNTNAME];
	S32 iMetricValue;
	S32 iRank;
} ScoreboardMetricEntry;

AUTO_STRUCT;
typedef struct ScoreboardGroup
{
	const char* pchFactionName; AST(POOL_STRING)
	REF_TO(Message) hDisplayMessage; AST(NAME(DisplayName))
	const char* pchGroupTexture; AST(POOL_STRING)
} ScoreboardGroup;

AUTO_STRUCT;
typedef struct ScoreboardEntityList
{
	ScoreboardGroup **eaGroupList;
	ScoreboardEntity **eaScoresList;
} ScoreboardEntityList;

typedef struct PVPGroup {
	int group;
	int subgroups;
} PVPGroup;

typedef enum PVPInfectType {
	PIT_Entity,
	PIT_Point,
	PIT_Volume,
	PIT_Global,
} PVPInfectType;

typedef struct PVPInfect {
	union {
		EntityRef origin_ent;				
		const char *origin_vol_name;	
		Vec3 origin_point;			
	};

	int iPartitionIdx;
	F32 infect_dist;
	PVPGroup *group;
	EntityRef *members;

	PVPInfectType type;
} PVPInfect;

AUTO_ENUM;
typedef enum PVPTeamStatus {
	PVPTeam_Invited,
	PVPTeam_Accepted,
	PVPTeam_Alive,
	PVPTeam_Dead
}PVPTeamStatus;

AUTO_STRUCT;
typedef struct PVPTeamMember {
	ContainerID iEntID;			AST(KEY)

	const char *pchDebugName;
	PVPDuelEntityState eStatus;
	const char *pchName;
	int iLevel;
	
}PVPTeamMember;

AUTO_STRUCT;
typedef struct PVPTeam {
	int iTeamID;				AST(KEY)
	PVPTeamMember **ppMembers;
	bool bTeamDead;
}PVPTeam;

AUTO_STRUCT;
typedef struct PVPTeamDuel {
	PVPTeam **ppTeams;

	Vec3 vOrigin;			
	EntityRef eFlagCritter;		

	int group;					NO_AST
	int iPartitionIdx;			NO_AST
	U32 uTimeRequest;			NO_AST
	U32 uTimeBegin;				NO_AST
	U32 uTimeCountDown;			NO_AST
	U32 uNextTimeFloater;		NO_AST
	U32 uLastMemberCheck;		NO_AST
	U32 bUpdateClients;			NO_AST
}PVPTeamDuel;

AUTO_STRUCT;
typedef struct PVPTeamFlag {
	PVPTeamDuel *team_duel;		AST(UNOWNED)

	int group;
	int team;
	const char *requester_name;	AST(SELF_ONLY)
	const char *failed_reason;	AST(SELF_ONLY)
	PVPDuelEntityState eState;
}PVPTeamFlag;

AUTO_STRUCT;
typedef struct PVPFlag {
	const char *tag;			AST(POOL_STRING)
	int group;
	int subgroup;

	PVPInfect *infect;			NO_AST	

	U32 infectious		: 1;	// Attacking something with this flag will flag the attacker into a different subgroup
	U32 infect_push		: 1;	// An entity with this flag can attack someone and PUSH this flag onto them
	U32 infect_heal		: 1;	// Healing something with this flag will flag the healer into the same subgroup
	U32 combat_out		: 1;	// Allows combat with non-flagged entities, as per normal relation
} PVPFlag;

AUTO_STRUCT;
typedef struct PVPSubGroup {
	const char* group_name;		AST(POOL_STRING STRUCTPARAM)
} PVPSubGroup;

AUTO_STRUCT;
typedef struct PVPGroupDef {
	const char *name;			AST(KEY STRUCTPARAM)

	PVPSubGroup **sub_groups;	AST(NAME("SubGroup:") ADDNAMES("subgroup"))
	// A PVP group without subgroups implies that it's every man for himself
} PVPGroupDef;

AUTO_STRUCT;
typedef struct PVPDuelState {
	PVPDuel *duel;					NO_AST
	int fxBoundaryGuid;				NO_AST
	PVPDuelEntityState state;
	const char *failed_reason;		AST(SELF_ONLY)
	const char *requester_name;		AST(SELF_ONLY)

	EntityRef *members;				AST(SELF_ONLY)
} PVPDuelState;

EntityRelation entity_PVP_GetRelation(Entity *e1, Entity *e2);
int pvpCanDuelInArea(Entity *e);  // -1 = no, 0 = unspecified, 1 = allowed
int pvpCanTeamDuel(Entity *e1, Entity *e2);
int pvpCanDuel(Entity *e1, Entity *e2);
int pvpIsAccepted(Entity *e);
int pvpIsInvitee(Entity *e);
int pvpIsRequester(Entity *e);

#endif //PVP_COMMON_H