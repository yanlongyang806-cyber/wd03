/***************************************************************************
*     Copyright (c) 2006-2011, Cryptic Studios
*     All Rights Reserved
*     Confidential Property of Cryptic Studios
***************************************************************************/
#include "Message.h"
#include "ReferenceSystem.h"

#ifndef __GSLTeamUp_H_
#define __GSLTeamUp_H_

typedef struct DisplayMessage DisplayMessage;
typedef struct TeamUpGroup TeamUpGroup;
typedef struct DisplayMessage DisplayMessage;
typedef struct Entity Entity;
typedef struct TeamUpMember TeamUpMember;

AUTO_ENUM;
typedef enum TeamUpFlags
{
	kTeamUpFlags_None = 1 << 0,
	kTeamUpFlags_Locked = 1 << 1, //Players are added and removed automatically by the owning system, and cannot leave by choice.
}TeamUpFlags;

AUTO_STRUCT;
typedef struct TeamUpInstance
{
	const char *pchName;			AST(POOL_STRING)
	U32 iTeamKey;					AST(KEY)

	TeamUpFlags eFlags;
	DisplayMessage msgDisplayName;	AST(STRUCT(parse_DisplayMessage))
	TeamUpGroup **ppGroups;
	U32 iGroupIdxMax;
}TeamUpInstance;

AUTO_STRUCT;
typedef struct TeamUpPartition
{
	S32 iPartitionIdx;				AST(KEY)

	TeamUpInstance **ppTeamUps;
	U32 sTeamUpKey;
	U32 uRewardIndex;				// The reward index for this partition
	bool bRewardIndexInited;
}TeamUpPartition;

void gslTeamUp_PartitionLoad(S32 iPartitionIdx);
void gslTeamUp_PartitionUnload(S32 iPartitionIdx);

TeamUpInstance *gslTeamUpInstance_FromKey(S32 iPartitionIdx, U32 sKey);

void gslTeamUp_EntityJoinTeamWithoutRequest(int iPartitionIdx, Entity *pEntity, TeamUpInstance *pInstance);
bool gslTeamUp_EntityJoinTeam(int iPartitionIdx, Entity *pEntity, TeamUpInstance *pInstance);
bool gslTeamUp_LeaveTeam(Entity *pEntity, bool bForced);
bool gslTeamUp_EntitiesJoinTeam(Entity ***pppEntites, TeamUpInstance *pInstance);
void gslTeamUp_RemoveInstanceByName(S32 iPartitionIdx, const char *pchKey);
TeamUpInstance *gslTeamUp_CreateNewInstance(S32 iPartitionIdx, const char *pchKey, DisplayMessage *pMessage, TeamUpFlags eFlags);

bool gslTeamUp_GroupChangeRequest(Entity *pEntity, int iGroupIdx);


void gslTeamUp_HandleTeamUpRequest(Entity *pEntity, const char *pchTeamName);

void gslTeamUp_CreateInstanceWithName(S32 iPartitionIdx, SA_PARAM_NN_STR const char *pchName);

void teamup_GetOnMapEntIds(int iPartitionIdx, int **res_ids, int teamInstanceID);

void teamup_getMembers(Entity *pEntity, TeamUpMember ***pppMembers);

U32 teamup_GetRewardIndex(int iPartitionIdx, int iTeamSize);
void teamup_SetRewardIndex(int iPartitionIdx, U32 uRewardIndex);

#endif