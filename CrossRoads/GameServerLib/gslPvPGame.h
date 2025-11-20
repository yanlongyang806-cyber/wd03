/***************************************************************************
*     Copyright (c) 2006-2011, Cryptic Studios
*     All Rights Reserved
*     Confidential Property of Cryptic Studios
***************************************************************************/

#include "PvPGameCommon.h"
#include "Message.h" // For DisplayMessage
#include "stdtypes.h"

#ifndef GSLPVPGAME_H
#define GSLPVPGAME_H

typedef struct QueuePartitionInfo QueuePartitionInfo;
typedef struct KillCreditTeam KillCreditTeam;

AUTO_STRUCT;
typedef struct PVPPointDef
{
	int iPointValue;				AST(STRUCTPARAM)
	DisplayMessage displayMessage;	AST(STRUCTPARAM STRUCT(parse_DisplayMessage))
	const char*	pchNotificationTag;	AST(STRUCTPARAM)
}PVPPointDef;

AUTO_STRUCT;
typedef struct PVPCritterKillPointDef
{
	PVPPointDef killCredit;			
	PVPPointDef assistCredit;
	S32 *critterTags;				AST(NAME(CritterTag) SUBTABLE(CritterTagsEnum))
}PVPCritterKillPointDef;

AUTO_STRUCT;
typedef struct PVPGamePointsDef
{
	PVPPointDef iKillCredit;
	PVPPointDef iAssistCredit;

	PVPCritterKillPointDef** CritterKillCredit;

	//CAPTURE THE FLAG
	PVPPointDef iCTF_PickupCredit;
	PVPPointDef iCTF_ReturnCredit;
	PVPPointDef iCTF_CaptureCredit;

	PVPPointDef iCTF_ShortPassCredit;
	PVPPointDef iCTF_MediumPassCredit;
	PVPPointDef iCTF_LongPassCredit;

	PVPPointDef iCTF_ShortCatchCredit;
	PVPPointDef iCTF_MediumCatchCredit;
	PVPPointDef iCTF_LongCatchCredit;

	PVPPointDef iCTF_Interception;

	//DOMINATION
	PVPPointDef iDOM_CapturePoint;
	PVPPointDef iDOM_DefendPoint;
	PVPPointDef iDOM_AttackPoint;
}PVPGamePointsDef;

PVPCurrentGameDetails * gslPvPGameDetailsFromIdx(int iPartitionIdx);

void gslPVPGame_CreateGameData(QueuePartitionInfo *pInfo, PVPGameType eGameType);

void pvpGame_InitGroups(PVPCurrentGameDetails *pGameDetails);

void gslPVPGame_PlayerNearDeathEnter(Entity *pKilled, Entity *pKiller);

void gslPVPGame_PlayerKilled(Entity *pKilled, Entity *pKiller);

void gslPVPGame_KillCredit(KillCreditTeam ***eaTeams, Entity *pKilled);

void gslPVPGame_awardPoints(Entity *pEntity, int iPoints, const char *pchMessage);

int gslPVPGame_CanArmamentSwap(int iPartitionIdx);

void gslPVPGame_ThrowFlag(PVPCurrentGameDetails * pGameDetails, Entity *pPlayer, Vec3 vecTarget);

void gslPVPGame_DropFlag(PVPCurrentGameDetails * pGameDetails, Entity *pPlayer);

void gslPVPGame_endWithWinner(int iPartitionIdx, int iWinningGroup, int iHighScore, bool bAllowTies);
void gslPVPGame_end(PVPCurrentGameDetails * pGameDetails);

void gslPVPGame_TickGames(F32 fTime);

void gslPVPGame_MapLoad(void);
void gslPVPGame_MapUnload(void);

void gslPVPGame_PartitionLoad(int iPartitionIdx);
void gslPVPGame_PartitionUnload(int iPartitionIdx);

#endif