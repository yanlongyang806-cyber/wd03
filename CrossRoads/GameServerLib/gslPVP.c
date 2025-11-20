#include "gslPVP.h"
#include "pvp_common.h"

#include "Character.h"
#include "CharacterAttribs.h"
#include "dynFxManager.h"
#include "Entity.h"
#include "EntityLib.h"
#include "EString.h"
#include "gslEventSend.h"
#include "gslMapState.h"
#include "GameStringFormat.h"
#include "gslCritter.h"
#include "NotifyCommon.h"
#include "Player.h"
#include "RegionRules.h"
#include "referencesystem.h"
#include "ResourceManager.h"
#include "stdtypes.h"
#include "wlVolumes.h"
#include "WorldGrid.h"
#include "WorldLib.h"

#include "AutoGEn/pvp_common_h_ast.h"
#include "AutoGen/WorldLib_autogen_ClientCmdWrappers.h"
#include "AutoGen/GameClientLib_autogen_ClientCmdWrappers.h"

AUTO_RUN_ANON(memBudgetAddMapping(__FILE__, BUDGET_GameSystems););

#define DUEL_START_FLOATERS 5
#define DUEL_REQUEST_LENGTH 30
#define DUEL_MATCH_LENGTH 30*60
#define DUEL_START_WARNING "Duel_Start_Warning"
#define DUEL_START "Duel_Start"
#define DUEL_LOSS_DEFEAT "Duel_Loss_Defeat"
#define DUEL_LOSS_OUTOFAREA "Duel_Loss_Outofarea"
#define DUEL_LOSS_INVALIDAREA "Duel_Loss_Invalidarea"
#define DUEL_WIN_DEFEAT "Duel_Win_Defeat"
#define DUEL_WIN_OUTOFAREA "Duel_Win_Outofarea"
#define DUEL_WIN_INVALIDAREA "Duel_Win_Invalidarea"

typedef struct PVPState {
	PVPDuel **duels;

	PVPInfect **infects;
} PVPState;

typedef struct PVPSide {
	PVPFlag *flag;

	EntityRef *members;
} PVPSide;

typedef struct PVPDuel {
	int iPartitionIdx;

	Vec3 origin;
	S64 timeRequest;	// Invite sent
	S64 timeAccept;		// Invite accepted
	S64 timeStart;		// Last warning sent, flags set
	S64 timeLastMemberCheck;
	EntityRef flagCritter;

	PVPSide **sides;
	PVPSide *challenger;

	S32 nextStartFloater;
} PVPDuel;

typedef struct PVPGroupInst {
	const char **subGroups;
	PVPGroup *group;
} PVPGroupInst;

typedef struct PVPTeamDuels{
	PVPTeamDuel **ppDuels;
}PVPTeamDuels;

PVPState g_pvp_state = {0};
StashTable g_pvp_groups = {0};
PVPTeamDuels g_pvp_teamDuels = {0};

static PVPDuelState* duelStateAlloc(void)
{
	return calloc(1, sizeof(PVPDuelState));
}

static void duelStateFree(PVPDuelState *state)
{
	free(state);
}

int gslPVPGetNewGroup(void)
{
	static int group_id = 0;

	return ++group_id;
}

void gslPVPDuelCleanup(Entity *e, PVPSide *side)
{
	static char *estr = NULL;
	eaiFindAndRemoveFast(&side->members, entGetRef(e));

	ClientCmd_dynRemoveManagedFx(e, e->pChar->pvpDuelState->fxBoundaryGuid);
	duelStateFree(e->pChar->pvpDuelState);
	e->pChar->pvpDuelState = NULL;
	entity_SetDirtyBit(e, parse_Character, e->pChar, false);
	StructDestroySafe(parse_PVPFlag, &e->pChar->pvpFlag);
}

void gslPVPSideDestroy(int iPartitionIdx, PVPSide *side)
{
	int i;
	for(i=eaiSize(&side->members)-1; i>=0; i--)
	{
		EntityRef er = side->members[i];
		Entity *e = entFromEntityRef(iPartitionIdx, er);

		if(e)
			gslPVPDuelCleanup(e, side);
	}
	eaiDestroy(&side->members);
	free(side->flag);
	free(side);
}

void gslPVPDuelDestroy(PVPDuel *duel)
{
	int i;
	for(i=eaSize(&duel->sides)-1; i>=0; --i) {
		gslPVPSideDestroy(duel->iPartitionIdx, duel->sides[i]);
	}
	eaDestroy(&duel->sides);

	entDie(entFromEntityRef(duel->iPartitionIdx, duel->flagCritter), 0, 0, 0, NULL);
	eaFindAndRemoveFast(&g_pvp_state.duels, duel);
}

void gslPVPGroupAddSubGroup(PVPGroup *group, PVPFlag *flag)
{
	flag->group = group->group;
	flag->subgroup = group->subgroups++;
}

static F32 gslPVPSideDistSquared(PVPDuel *duel, PVPSide *side)
{
	int i, count = 0;
	Vec3 geo_avg_pos = {0};

	for(i=eaiSize(&side->members)-1; i>=0; i--)
	{
		Entity *e = entFromEntityRef(duel->iPartitionIdx, side->members[i]);

		if(e)
		{
			Vec3 pos;
			entGetPos(e, pos);
			addVec3(pos, geo_avg_pos, geo_avg_pos);
			count++;
		}
	}
	if(!count)
		return FLT_MAX;

	scaleVec3(geo_avg_pos, 1.0/count, geo_avg_pos);
	return distance3Squared(geo_avg_pos, duel->origin);
}

void gslPVPTeamDuelSendStartFloater(PVPTeamDuel *pDuel)
{
	int i;
	static char *estr = NULL;
	for(i=0;i<eaSize(&pDuel->ppTeams);i++)
	{
		int j;

		for(j=0;j<eaSize(&pDuel->ppTeams[i]->ppMembers);j++)
		{
			Entity *e = entFromContainerID(pDuel->iPartitionIdx,GLOBALTYPE_ENTITYPLAYER,pDuel->ppTeams[i]->ppMembers[j]->iEntID);
			
			if(e)
			{
				estrClear(&estr);
				entFormatGameMessageKey(e, &estr, DUEL_START_WARNING, 
					STRFMT_INT("TimeRemaining", DUEL_START_FLOATERS-pDuel->uNextTimeFloater),
					STRFMT_END);
				ClientCmd_NotifySend(e, kNotifyType_PvPCountdown, estr, NULL, NULL);
			}
		}
	}
}

void gslPVPDuelSendStartFloater(PVPDuel *duel)
{
	int i;
	static char *estr = NULL;
	for(i=eaSize(&duel->sides)-1; i>=0; i--)
	{
		int j;
		PVPSide *side = duel->sides[i];
		for(j=eaiSize(&side->members)-1; j>=0; j--)
		{
			Entity *e = entFromEntityRef(duel->iPartitionIdx, side->members[j]);

			if(e)
			{
				estrClear(&estr);
				entFormatGameMessageKey(e, &estr, DUEL_START_WARNING, 
					STRFMT_INT("TimeRemaining", DUEL_START_FLOATERS-duel->nextStartFloater),
					STRFMT_END);
				ClientCmd_NotifySend(e, kNotifyType_PvPCountdown, estr, NULL, NULL);
			}
		}
	}
}

void gslPVPSendDefeatFloater(PVPDuel *duel, Entity *defeated)
{
	int i;
	static char *estr;
	Entity *killer = NULL;
	int iPartitionIdx = entGetPartitionIdx(defeated);

	for(i=0; i<eaSize(&duel->sides); i++)
	{
		int j;
		PVPSide *side = duel->sides[i];
		for(j=0; j<eaiSize(&side->members); j++)
		{
			Entity *e = entFromEntityRef(iPartitionIdx, side->members[j]);

			if(e)
			{
				estrClear(&estr);
				if(e==defeated)
				{
					entFormatGameMessageKey(e, &estr, DUEL_LOSS_DEFEAT, STRFMT_END);
					ClientCmd_NotifySend(e, kNotifyType_PvPLoss, estr, NULL, NULL);
				}
				else
				{
					entFormatGameMessageKey(e, &estr, DUEL_WIN_DEFEAT, STRFMT_END);
					ClientCmd_NotifySend(e, kNotifyType_PvPWin, estr, NULL, NULL);
				}
			}
		}
	}

	killer = character_FindKiller(iPartitionIdx, defeated->pChar,NULL);

	if(killer && killer->pPlayer)
		eventsend_RecordDuelVictory(killer, defeated, PVPDuelVictoryType_KO);
}

void gslPVPSendOutofAreaFloater(PVPDuel *duel, Entity *defeated)
{
	int i;
	static char *estr;
	Entity *winner = NULL;

	for(i=0; i<eaSize(&duel->sides); i++)
	{
		int j;
		PVPSide *side = duel->sides[i];
		for(j=0; j<eaiSize(&side->members); j++)
		{
			Entity *e = entFromEntityRef(duel->iPartitionIdx, side->members[j]);

			if(e)
			{
				estrClear(&estr);
				if(e==defeated)
				{
					entFormatGameMessageKey(e, &estr, DUEL_LOSS_OUTOFAREA, STRFMT_END);
					ClientCmd_NotifySend(e, kNotifyType_PvPLoss, estr, NULL, NULL);
				}
				else
				{
					winner = e;
					entFormatGameMessageKey(e, &estr, DUEL_WIN_OUTOFAREA, STRFMT_END);
					ClientCmd_NotifySend(e, kNotifyType_PvPWin, estr, NULL, NULL);
				}
			}
		}
	}

	//Don't record a victory if the duel didn't start
	if(duel->timeStart && winner && winner->pPlayer)
		eventsend_RecordDuelVictory(winner, defeated, PVPDuelVictoryType_RingOut);
}

void gslPVPSendInvalidAreaFloater(PVPDuel *duel, Entity *defeated)
{
	int i;
	static char *estr;
	Entity *winner = NULL;

	for(i=0; i<eaSize(&duel->sides); i++)
	{
		int j;
		PVPSide *side = duel->sides[i];
		for(j=0; j<eaiSize(&side->members); j++)
		{
			Entity *e = entFromEntityRef(duel->iPartitionIdx, side->members[j]);

			if(e)
			{
				estrClear(&estr);
				if(e==defeated)
				{
					entFormatGameMessageKey(e, &estr, DUEL_LOSS_INVALIDAREA, STRFMT_END);
					ClientCmd_NotifySend(e, kNotifyType_PvPLoss, estr, NULL, NULL);
				}
				else
				{
					winner = e;
					entFormatGameMessageKey(e, &estr, DUEL_WIN_INVALIDAREA, STRFMT_END);
					ClientCmd_NotifySend(e, kNotifyType_PvPWin, estr, NULL, NULL);
				}
			}
		}
	}

	//Don't record a victory if the duel didn't start
	if(duel->timeStart && winner && winner->pPlayer)
		eventsend_RecordDuelVictory(winner, defeated, PVPDuelVictoryType_RingOut);
}

static void gslPVPCheckSideMembers(PVPDuel *duel, PVPSide *side)
{
	int i;

	for(i=eaiSize(&side->members)-1; i>=0; i--)
	{
		Entity *e = entFromEntityRef(duel->iPartitionIdx, side->members[i]);
		
		if(!e)
		{
			eaiRemoveFast(&side->members, i);
			continue;
		}
		
		if(pvpCanDuelInArea(e)<0)
		{
			gslPVPSendInvalidAreaFloater(duel, e);
			gslPVPDuelCleanup(e, side);
			continue;
		}
	}
}

void gslPVPDuelStart(PVPDuel *duel)
{
	int i;
	static char *estr = NULL;
	duel->timeStart = ABS_TIME_PARTITION(duel->iPartitionIdx);
	for(i=eaSize(&duel->sides)-1; i>=0; i--)
	{
		int j;
		PVPSide *side = duel->sides[i];
		for(j=eaiSize(&side->members)-1; j>=0; j--)
		{
			Entity *e = entFromEntityRef(duel->iPartitionIdx, side->members[j]);

			if(e)
			{
				e->pChar->pvpFlag = StructClone(parse_PVPFlag, side->flag);
				entity_SetDirtyBit(e, parse_Character, e->pChar, false);

				estrClear(&estr);
				entFormatGameMessageKey(e, &estr, DUEL_START, STRFMT_END);
				ClientCmd_NotifySend(e, kNotifyType_PvPStart, estr, NULL, NULL);
			}
		}
	}
}

void gslPVPTeamMemberDestroy(PVPTeamDuel *pDuel,PVPTeam *pTeam,PVPTeamMember *pMember)
{
	Entity *pEnt = entFromContainerID(pDuel->iPartitionIdx,GLOBALTYPE_ENTITYPLAYER,pMember->iEntID);

	if(SAFE_MEMBER2(pEnt,pChar,pvpTeamDuelFlag))
	{
		pEnt->pChar->pvpTeamDuelFlag->team_duel = NULL;
		StructDestroy(parse_PVPTeamFlag,pEnt->pChar->pvpTeamDuelFlag);
		pEnt->pChar->pvpTeamDuelFlag = NULL;
		entity_SetDirtyBit(pEnt, parse_Character, pEnt->pChar, false);
	}

	if(pTeam)
	{
		eaFindAndRemove(&pTeam->ppMembers,pMember);
	}

	StructDestroy(parse_PVPTeamMember,pMember);
	pDuel->bUpdateClients = true;
}

void gslPVPTeamDuelDestroy(PVPTeamDuel *pDuel)
{
	int i;
	
	eaFindAndRemove(&g_pvp_teamDuels.ppDuels,pDuel);

	//Remove duel flag
	if(pDuel->eFlagCritter)
		entDie(entFromEntityRef(pDuel->iPartitionIdx,pDuel->eFlagCritter),0,0,0,NULL);

	for(i=0;i<eaSize(&pDuel->ppTeams);i++)
	{
		int j;
		for(j=eaSize(&pDuel->ppTeams[i]->ppMembers)-1;j>=0;j--)
		{
			gslPVPTeamMemberDestroy(pDuel,pDuel->ppTeams[i],pDuel->ppTeams[i]->ppMembers[j]);
		}
	}

	StructDestroy(parse_PVPTeamDuel,pDuel);
}

void gslPVPTeamDuelStart(PVPTeamDuel *pDuel)
{
	int i;
	static char *estr = NULL;

	for(i=0;i<eaSize(&pDuel->ppTeams);i++)
	{
		int n;

		for(n=0;n<eaSize(&pDuel->ppTeams[i]->ppMembers);n++)
		{
			Entity *pEnt = entFromContainerID(pDuel->iPartitionIdx,GLOBALTYPE_ENTITYPLAYER,pDuel->ppTeams[i]->ppMembers[n]->iEntID);

			if(pEnt && (pDuel->ppTeams[i]->ppMembers[n]->eStatus == DuelState_Accepted
				|| pDuel->ppTeams[i]->ppMembers[n]->eStatus == DuelState_Request))
			{
				pDuel->ppTeams[i]->ppMembers[n]->eStatus = DuelState_Active;
				pEnt->pChar->pvpTeamDuelFlag->eState = DuelState_Active;
				entity_SetDirtyBit(pEnt, parse_Character, pEnt->pChar, false);
				
				estrClear(&estr);
				entFormatGameMessageKey(pEnt, &estr, DUEL_START, STRFMT_END);
				ClientCmd_NotifySend(pEnt, kNotifyType_PvPStart, estr, NULL, NULL);
				
			}
		}
	}

	pDuel->uTimeBegin = ABS_TIME_PARTITION(pDuel->iPartitionIdx);
	pDuel->bUpdateClients = true;
}

void gslPVPTeamDuelBegin(PVPTeamDuel *pDuel)
{
	// Make sure there are 2 sides with players that want to duel
	int iTeams = 0;
	int i;
	Entity **ppDuelingEnts = NULL;

	for(i=0;i<eaSize(&pDuel->ppTeams);i++)
	{
		int n;
		bool bTeamAdded = false;
		for(n=0;n<eaSize(&pDuel->ppTeams[i]->ppMembers);n++)
		{
			Entity *pEnt = entFromContainerID(pDuel->iPartitionIdx,GLOBALTYPE_ENTITYPLAYER,pDuel->ppTeams[i]->ppMembers[n]->iEntID);

			if(pEnt && (pDuel->ppTeams[i]->ppMembers[n]->eStatus == DuelState_Accepted
				|| pDuel->ppTeams[i]->ppMembers[n]->eStatus == DuelState_Request))
			{
				if(!bTeamAdded)
				{
					bTeamAdded = true;
					iTeams++;
				}

				eaPush(&ppDuelingEnts,pEnt);
			}
			else
			{
				if(SAFE_MEMBER2(pEnt,pChar,pvpTeamDuelFlag))
				{
					pEnt->pChar->pvpTeamDuelFlag->eState = DuelState_Failed;
					entity_SetDirtyBit(pEnt, parse_Character, pEnt->pChar, false);
				}
				pDuel->ppTeams[i]->ppMembers[n]->eStatus = DuelState_Failed;
				pDuel->bUpdateClients = true;
			}
		}
	}

	if(iTeams < 2)
	{
		gslPVPTeamDuelDestroy(pDuel);
		return;
	}

	pDuel->uTimeCountDown = ABS_TIME_PARTITION(pDuel->iPartitionIdx);
}

void gslPVPTeamDuelMemberCheck(PVPTeamDuel *pDuel)
{
	int i;

	for(i=0;i<eaSize(&pDuel->ppTeams);i++)
	{
		int j;
		for(j=0;j<eaSize(&pDuel->ppTeams[i]->ppMembers);j++)
		{
			PVPDuelEntityState eState = pDuel->ppTeams[i]->ppMembers[j]->eStatus;

			if(eState == DuelState_Decline || eState == DuelState_Accepted || eState == DuelState_Request)
			{
				continue;
			}

			return;
		}
	}

	gslPVPTeamDuelBegin(pDuel);
}

void gslPVPTeamDuelEnd(PVPTeamDuel *pDuel, int iWinningTeamID)
{
	static char *estr;
	int i;

	for(i=0;i<eaSize(&pDuel->ppTeams);i++)
	{
		int n;

		for(n=0;n<eaSize(&pDuel->ppTeams[i]->ppMembers);n++)
		{
			Entity *pEnt = entFromContainerID(pDuel->iPartitionIdx,GLOBALTYPE_ENTITYPLAYER,pDuel->ppTeams[i]->ppMembers[n]->iEntID);

			if(pEnt)
			{
				estrClear(&estr);
				if(pDuel->ppTeams[i]->iTeamID == iWinningTeamID)
				{	
					entFormatGameMessageKey(pEnt, &estr, DUEL_WIN_DEFEAT, STRFMT_END);
				}
				else
				{
					entFormatGameMessageKey(pEnt, &estr, DUEL_LOSS_DEFEAT, STRFMT_END);
				}

				ClientCmd_NotifySend(pEnt, kNotifyType_PvPLoss, estr, NULL, NULL);
			}
		}
	}
	//TODO(MM): make some sort of scoreboard of the duel for after
	gslPVPTeamDuelDestroy(pDuel);
}

void gslPVPTeamDuelTick(PVPTeamDuel *pDuel)
{
	int i,iTeamsAlive=0;
	int iTeamWinning = 0;
	int iTeamHighScore = 0;

	for(i=0;i<eaSize(&pDuel->ppTeams);i++)
	{
		PVPTeam *pTeam = pDuel->ppTeams[i];
		int j;
		int iAlive = 0;
		bool bTeamDead = true;

		for(j=0;j<eaSize(&pTeam->ppMembers);j++)
		{
			Entity *pEnt = entFromContainerID(pDuel->iPartitionIdx,GLOBALTYPE_ENTITYPLAYER,pTeam->ppMembers[j]->iEntID);

			if(!pEnt)
			{
				pTeam->ppMembers[j]->eStatus = DuelState_Dead;
				pDuel->bUpdateClients = true;
			}

			if(pTeam->ppMembers[j]->eStatus == DuelState_Active)
			{
				bTeamDead = false;
				iAlive++;
			}
		}

		if(bTeamDead)
			pTeam->bTeamDead = true;
		else
			iTeamsAlive++;

		if(iAlive>iTeamHighScore)
		{
			iTeamHighScore = iAlive;
			iTeamWinning = pTeam->iTeamID;
		}
	}

	if(iTeamsAlive < 2)
	{
		gslPVPTeamDuelEnd(pDuel,iTeamWinning);
	}
}

PVPTeam *gslPVPFindTeamForEnt(PVPTeamDuel *pDuel, Entity *pEnt, PVPTeamMember **ppMemberOut)
{
	int i;

	for(i=0;i<eaSize(&pDuel->ppTeams);i++)
	{
		int j;

		for(j=0;j<eaSize(&pDuel->ppTeams[i]->ppMembers);j++)
		{
			if(pDuel->ppTeams[i]->ppMembers[j]->iEntID == entGetContainerID(pEnt))
			{
				if(ppMemberOut)
					(*ppMemberOut) = pDuel->ppTeams[i]->ppMembers[j];
				return pDuel->ppTeams[i];
			}
		}
	}

	return NULL;
}

void gslPVPTeamDuelDeath(Entity *pEnt)
{
	PVPTeamDuel *pDuel = pEnt->pChar->pvpTeamDuelFlag->team_duel;
	PVPTeamMember *pMember = NULL;
	PVPTeam *pTeam = gslPVPFindTeamForEnt(pDuel,pEnt,&pMember);

	if(!pDuel->uTimeBegin)
		return;
	
	if(pTeam && pMember)
	{
		pMember->eStatus = DuelState_Dead;
	}

	pEnt->pChar->pvpTeamDuelFlag->eState = DuelState_Dead;
	pDuel->bUpdateClients = true;
}

void gslPVPDuelTick(void)
{
	int i;

	for(i=eaSize(&g_pvp_state.duels)-1; i>=0; i--)
	{
		int j;
		PVPDuel *duel = g_pvp_state.duels[i];

		if(!duel->timeAccept && ABS_TIME_SINCE_PARTITION(duel->iPartitionIdx, duel->timeRequest)>SEC_TO_ABS_TIME(60))
		{
			gslPVPDuelDestroy(duel);
			continue;
		}
		if(ABS_TIME_PASSED_PARTITION(duel->iPartitionIdx, duel->timeAccept, 30*60))
		{
			gslPVPDuelDestroy(duel);
			continue;
		}
		if(duel->nextStartFloater>=DUEL_START_FLOATERS && !duel->timeStart)
		{
			gslPVPDuelStart(duel);
		}
		if(!duel->timeStart && duel->nextStartFloater<DUEL_START_FLOATERS && 
			ABS_TIME_PASSED_PARTITION(duel->iPartitionIdx, duel->timeAccept, duel->nextStartFloater))
		{
			gslPVPDuelSendStartFloater(duel);
			duel->nextStartFloater++;
		}
		if(ABS_TIME_SINCE_PARTITION(duel->iPartitionIdx, duel->timeLastMemberCheck)>SEC_TO_ABS_TIME(2))
		{
			duel->timeLastMemberCheck = ABS_TIME_PARTITION(duel->iPartitionIdx);
			for(j=eaSize(&duel->sides)-1; j>=0; j--)
			{
				PVPSide *side = duel->sides[j];

				gslPVPCheckSideMembers(duel, side);
				if(eaiSize(&side->members)<=0)
				{
					gslPVPSideDestroy(duel->iPartitionIdx, side);
					eaRemoveFast(&duel->sides, j);
					continue;
				}
				if(gslPVPSideDistSquared(duel, side)>=SQR(150))
				{
					int k;

					for(k=0; k<eaiSize(&side->members); k++)
					{
						Entity *coward = entFromEntityRef(duel->iPartitionIdx, side->members[k]);

						if(coward)
							gslPVPSendOutofAreaFloater(duel, coward);
					}

					gslPVPSideDestroy(duel->iPartitionIdx, side);
					eaRemoveFast(&duel->sides, j);
					continue;
				}
			}
			if(eaSize(&duel->sides)<=1)
			{
				gslPVPDuelDestroy(duel);
				continue;
			}
		}
	}

	for(i=0;i<eaSize(&g_pvp_teamDuels.ppDuels);i++)
	{
		PVPTeamDuel *pDuel = g_pvp_teamDuels.ppDuels[i];

		if(!pDuel->uTimeCountDown && ABS_TIME_SINCE_PARTITION(pDuel->iPartitionIdx, pDuel->uTimeRequest)>SEC_TO_ABS_TIME(60))
		{
			gslPVPTeamDuelBegin(pDuel);
			continue;
		}
		if(pDuel->uNextTimeFloater>=DUEL_START_FLOATERS && !pDuel->uTimeBegin)
		{
			gslPVPTeamDuelStart(pDuel);
		}
		if(!pDuel->uTimeBegin && pDuel->uTimeCountDown && pDuel->uNextTimeFloater<DUEL_START_FLOATERS && 
			ABS_TIME_PASSED_PARTITION(pDuel->iPartitionIdx, pDuel->uTimeCountDown, pDuel->uNextTimeFloater))
		{
			gslPVPTeamDuelSendStartFloater(pDuel);
			pDuel->uNextTimeFloater++;
		}
		if(pDuel->uTimeBegin && ABS_TIME_PASSED_PARTITION(pDuel->iPartitionIdx,pDuel->uTimeBegin,30*60))
		{
			gslPVPTeamDuelEnd(pDuel,0);
		}
		if(pDuel->uTimeBegin && ABS_TIME_SINCE_PARTITION(pDuel->iPartitionIdx, pDuel->uLastMemberCheck)>SEC_TO_ABS_TIME(2))
		{
			pDuel->uLastMemberCheck = ABS_TIME_PARTITION(pDuel->iPartitionIdx);
			gslPVPTeamDuelTick(pDuel);
		}
	}

	for(i=0;i<eaSize(&g_pvp_teamDuels.ppDuels);i++)
	{
		if(g_pvp_teamDuels.ppDuels[i]->bUpdateClients)
		{
			int t,m;
			PVPTeamDuel *pDuel = g_pvp_teamDuels.ppDuels[i];
			g_pvp_teamDuels.ppDuels[i]->bUpdateClients = false;
			for(t=0;t<eaSize(&pDuel->ppTeams);t++)
			{
					for(m=0;m<eaSize(&pDuel->ppTeams[t]->ppMembers);m++)
					{
						Entity *pEnt = entFromContainerID(pDuel->iPartitionIdx,GLOBALTYPE_ENTITYPLAYER,pDuel->ppTeams[t]->ppMembers[m]->iEntID);

						if(pEnt)
							ClientCmd_gclPVPTeamDuelUpdateStruct(pEnt,pDuel);
					}
			}
		
		}
	}
}

PVPTeamFlag *gslPVPSetUpPVPTeamFlag(Entity *pEntity, Entity *pRequester, PVPTeamDuel *pDuel, Team *pTeam)
{
	PVPTeamFlag *pReturn = StructCreate(parse_PVPTeamFlag);
	PVPTeamMember *pTeamMember = StructCreate(parse_PVPTeamMember);
	PVPTeam *pPVPTeam = NULL;
	int i;

	pReturn->team_duel = pDuel;

	if(pEntity == pRequester)
	{
		pReturn->eState = DuelState_Request;
	}else{
		pReturn->eState = DuelState_Invite;
		pReturn->requester_name = StructAllocString(entGetLangName(pRequester, entGetLanguage(pEntity)));
	}

	pEntity->pChar->pvpTeamDuelFlag = pReturn;

	pTeamMember->eStatus = pReturn->eState;
	pTeamMember->pchName = StructAllocString(entGetLangName(pEntity, entGetLanguage(pEntity)));
	pTeamMember->iLevel = pEntity->pChar->iLevelCombat;
	pTeamMember->pchDebugName = StructAllocString(pEntity->debugName);
	pTeamMember->iEntID = entGetContainerID(pEntity);

	for(i=eaSize(&pDuel->ppTeams)-1;i>=0;i--)
	{
		if((pTeam && (ContainerID)pDuel->ppTeams[i]->iTeamID == pTeam->iContainerID)
			|| (!pTeam && pDuel->ppTeams[i]->iTeamID == -1))
		{
			pPVPTeam = pDuel->ppTeams[i];
			break;
		}
	}

	if(!pPVPTeam)
	{
		pPVPTeam = StructCreate(parse_PVPTeam);
		pPVPTeam->iTeamID = pTeam ? pTeam->iContainerID : -1;
		eaPush(&pDuel->ppTeams,pPVPTeam);
	}

	pReturn->team = pPVPTeam->iTeamID;
	pReturn->group = pDuel->group;
	eaPush(&pPVPTeam->ppMembers,pTeamMember);

	return pReturn;
}

bool pvpEntTeamIsDueling(Entity *pEnt)
{
	int iTeamID = pEnt->pTeam && pEnt->pTeam->eState == TeamState_Member ? pEnt->pTeam->iTeamID : 0;

	if(iTeamID)
	{
		int i;

		for(i=0;i<eaSize(&g_pvp_teamDuels.ppDuels);i++)
		{
			int n;

			for(n=0;n<eaSize(&g_pvp_teamDuels.ppDuels[i]->ppTeams);n++)
			{
				if(g_pvp_teamDuels.ppDuels[i]->ppTeams[n]->iTeamID == iTeamID)
					return true;
			}
		}
	}

	return false;
}

void gslPVPTeamDuelRequest(Entity *e1, Entity *e2)
{
	Entity **ppTeam1 = NULL;
	Entity **ppTeam2 = NULL;
	PVPTeamDuel *pDuel = NULL;
	Team *pTeam1;
	Team *pTeam2;
	int i;
	Vec3 pos1,pos2;
	Mat4 orig;
	S32 groundDuel;
	Entity *flagCritter;

	if(!pvpCanTeamDuel(e1, e2))
		return;

	if(pvpEntTeamIsDueling(e1) || pvpEntTeamIsDueling(e2))
	{
		return; //Give some message to e1
	}

	pDuel = StructCreate(parse_PVPTeamDuel);

	pTeam1 = team_GetTeam(e1);
	pTeam2 = team_GetTeam(e2);

	pDuel->iPartitionIdx = entGetPartitionIdx(e1);
	pDuel->group = gslPVPGetNewGroup();

	if(pTeam1)
	{
		for(i=0;i<eaSize(&pTeam1->eaMembers);i++)
		{
			Entity *pEntity = entFromContainerID(entGetPartitionIdx(e1),GLOBALTYPE_ENTITYPLAYER,pTeam1->eaMembers[i]->iEntID);

			if(pEntity && entGetPartitionIdx(pEntity) == pDuel->iPartitionIdx)
			{
				PVPTeamFlag *pFlag = gslPVPSetUpPVPTeamFlag(pEntity,e1,pDuel,pTeam1);		
				entity_SetDirtyBit(pEntity, parse_Character, pEntity->pChar, false);
			}
		}
	}
	else
	{
		PVPTeamFlag *pFlag = gslPVPSetUpPVPTeamFlag(e1,e1,pDuel,NULL);
		entity_SetDirtyBit(e1,parse_Character,e1->pChar,false);
	}
	

	for(i=0;i<eaSize(&pTeam2->eaMembers);i++)
	{
		Entity *pEntity = entFromContainerID(entGetPartitionIdx(e1),GLOBALTYPE_ENTITYPLAYER,pTeam2->eaMembers[i]->iEntID);

		if(pEntity && entGetPartitionIdx(pEntity) == pDuel->iPartitionIdx)
		{
			PVPTeamFlag *pFlag = gslPVPSetUpPVPTeamFlag(pEntity,e1,pDuel,pTeam2);	
			entity_SetDirtyBit(pEntity, parse_Character, pEntity->pChar, false);
		}
	}

	pDuel->uTimeRequest = ABS_TIME_PARTITION(pDuel->iPartitionIdx);
	entGetPos(e1, pos1); entGetPos(e2, pos2);
	centerVec3(pos1, pos2, pDuel->vOrigin);

	groundDuel = 1;
	worldSnapPosToGround(entGetPartitionIdx(e1), pDuel->vOrigin, 40, -80, &groundDuel);

	copyMat4(unitmat, orig);
	copyVec3(pDuel->vOrigin, orig[3]);

	if(!groundDuel)
		flagCritter = critter_Create("PVP_Flag_Critter_Air", NULL, GLOBALTYPE_ENTITYCRITTER, entGetPartitionIdx(e1), NULL, 1, 0, 0, NULL, NULL, NULL, NULL, NULL, 0, NULL, NULL);
	else
		flagCritter = critter_Create("PVP_Flag_Critter_Ground", NULL, GLOBALTYPE_ENTITYCRITTER, entGetPartitionIdx(e1), NULL, 1, 0, 0, NULL, NULL, NULL, NULL, NULL, 0, NULL, NULL);

	if(flagCritter)
	{
		entSetPos(flagCritter, pDuel->vOrigin, 1, "Spawn");
		pDuel->eFlagCritter = entGetRef(flagCritter);
	}

	pDuel->bUpdateClients = true;
	eaPush(&g_pvp_teamDuels.ppDuels,pDuel);
}

void gslPVPDuelRequest(Entity *e1, Entity *e2)
{
	PVPDuel *newDuel = NULL;
	PVPDuelState *state1, *state2;
	PVPFlag *flag1, *flag2;
	PVPSide *side1, *side2;
	PVPGroup *group;
	Entity *flagCritter;
	Vec3 pos1, pos2;
	int groundDuel;
	Mat4 orig;
	
	if(!pvpCanDuel(e1, e2))
		return;

	newDuel = calloc(1, sizeof(PVPDuel));

	newDuel->iPartitionIdx = entGetPartitionIdx(e1);
	
	state1 = duelStateAlloc();
	state2 = duelStateAlloc();

	state1->state = DuelState_Request;
	state2->state = DuelState_Invite;
	state1->duel = newDuel;
	state2->duel = newDuel;
	state2->requester_name = entGetLangName(e1, entGetLanguage(e2));

	e1->pChar->pvpDuelState = state1;
	e2->pChar->pvpDuelState = state2;
	entity_SetDirtyBit(e1, parse_Character, e1->pChar, false);
	entity_SetDirtyBit(e2, parse_Character, e2->pChar, false);

	eaiPush(&state1->members, entGetRef(e1));
	eaiPush(&state1->members, entGetRef(e2));

	eaiPush(&state2->members, entGetRef(e1));
	eaiPush(&state2->members, entGetRef(e2));

	group = calloc(1, sizeof(PVPGroup));
	group->group = gslPVPGetNewGroup();

	side1 = calloc(1, sizeof(PVPSide));
	side2 = calloc(1, sizeof(PVPSide));
	
	flag1 = StructCreate(parse_PVPFlag);
	flag2 = StructCreate(parse_PVPFlag);

	gslPVPGroupAddSubGroup(group, flag1);
	gslPVPGroupAddSubGroup(group, flag2);

	side1->flag = flag1;
	side2->flag = flag2;

	eaiPush(&side1->members, entGetRef(e1));
	eaiPush(&side2->members, entGetRef(e2));

	eaPush(&newDuel->sides, side1);
	eaPush(&newDuel->sides, side2);
	newDuel->challenger = side1;

	newDuel->timeRequest = ABS_TIME_PARTITION(newDuel->iPartitionIdx);
	entGetPos(e1, pos1); entGetPos(e2, pos2);
	centerVec3(pos1, pos2, newDuel->origin);

	groundDuel = 1;
	worldSnapPosToGround(entGetPartitionIdx(e1), newDuel->origin, 40, -80, &groundDuel);
	
	copyMat4(unitmat, orig);
	copyVec3(newDuel->origin, orig[3]);
	state1->fxBoundaryGuid = dynGetManagedFxGUID();
	state2->fxBoundaryGuid = dynGetManagedFxGUID();
	ClientCmd_dynAddFxAtLocationManaged(e1, state1->fxBoundaryGuid, "FX_PVP_OpenWorld_Boundary", orig, "gslPVP.c"); // Don't really have a good blame file
	ClientCmd_dynAddFxAtLocationManaged(e2, state2->fxBoundaryGuid, "FX_PVP_OpenWorld_Boundary", orig, "gslPVP.c");

	if(!groundDuel)
		flagCritter = critter_Create("PVP_Flag_Critter_Air", NULL, GLOBALTYPE_ENTITYCRITTER, entGetPartitionIdx(e1), NULL, 1, 0, 0, NULL, NULL, NULL, NULL, NULL, 0, NULL, NULL);
	else
		flagCritter = critter_Create("PVP_Flag_Critter_Ground", NULL, GLOBALTYPE_ENTITYCRITTER, entGetPartitionIdx(e1), NULL, 1, 0, 0, NULL, NULL, NULL, NULL, NULL, 0, NULL, NULL);
	
	if(flagCritter)
	{
		entSetPos(flagCritter, newDuel->origin, 1, "Spawn");
		newDuel->flagCritter = entGetRef(flagCritter);
	}

	eaPush(&g_pvp_state.duels, newDuel);
}

PVPSide* gslPVPDuelFindSide(PVPDuel *duel, EntityRef er)
{
	int i;
	for(i=eaSize(&duel->sides)-1; i>=0; i--)
		if(eaiFind(&duel->sides[i]->members, er)!=-1)
			return duel->sides[i];
	return NULL;
}

void gslPVPTeamDuelAccept(Entity *pEnt)
{
	PVPTeamFlag *pDuel = pEnt->pChar->pvpTeamDuelFlag;
	PVPTeamMember *pMember = NULL;

	if(!pDuel)
		return;

	if(gslPVPFindTeamForEnt(pDuel->team_duel,pEnt,&pMember))
	{
		if(pMember->eStatus == DuelState_Invite)
		{
			pMember->eStatus = DuelState_Accepted;
			pEnt->pChar->pvpTeamDuelFlag->eState = DuelState_Accepted;
			entity_SetDirtyBit(pEnt, parse_Character, pEnt->pChar, false);
		}
	}

	pDuel->team_duel->bUpdateClients = true;
	gslPVPTeamDuelMemberCheck(pDuel->team_duel);
}

void gslPVPTeamDuelDecline(Entity *pEnt)
{
	PVPTeamFlag *pDuel = pEnt->pChar->pvpTeamDuelFlag;
	PVPTeamDuel *pTeamDuel = NULL;
	PVPTeamMember *pMember = NULL;

	if(!pDuel)
		return;

	pTeamDuel = pDuel->team_duel;

	if(gslPVPFindTeamForEnt(pDuel->team_duel,pEnt,&pMember))
	{
		if(pMember->eStatus == DuelState_Invite)
		{
			pMember->eStatus = DuelState_Decline;
			StructDestroy(parse_PVPTeamFlag,pEnt->pChar->pvpTeamDuelFlag);
			pEnt->pChar->pvpTeamDuelFlag = NULL;

			entity_SetDirtyBit(pEnt, parse_Character, pEnt->pChar, false);
		}

		if(pMember->eStatus == DuelState_Active)
		{
			pMember->eStatus = DuelState_Dead;
			pEnt->pChar->pvpTeamDuelFlag->eState = DuelState_Dead;
		}
	}

	pTeamDuel->bUpdateClients = true;
	gslPVPTeamDuelMemberCheck(pTeamDuel);
}

void gslPVPTeamDuelSurrender(Entity *pEnt)
{
	PVPTeamFlag *pDuel = pEnt->pChar->pvpTeamDuelFlag;
	PVPTeamMember *pMember = NULL;

	if(!pDuel)
		return;

	if(gslPVPFindTeamForEnt(pDuel->team_duel,pEnt,&pMember))
	{
		if(pMember->eStatus == DuelState_Active)
		{
			pMember->eStatus = DuelState_Dead;
			pEnt->pChar->pvpTeamDuelFlag->eState = DuelState_Dead;
			pDuel->team_duel->bUpdateClients = true;
		}
	}
}

void gslPVPDuelAccept(Entity *e1)
{
	int i;
	Entity *e2 = NULL;
	PVPSide *s1, *s2;
	PVPDuel *duel;
	PVPDuelState *state = e1->pChar->pvpDuelState;
	int iPartitionIdx;

	if(!state)
		return;

	iPartitionIdx = entGetPartitionIdx(e1);
	for(i=0; i<eaiSize(&state->members); i++)
	{
		if(state->members[i]!=entGetRef(e1))
		{
			e2 = entFromEntityRef(iPartitionIdx, state->members[i]);
			break;
		}
	}
	if(!e2)
		return;

	duel = state->duel;

	s1 = gslPVPDuelFindSide(duel, entGetRef(e1));
	s2 = gslPVPDuelFindSide(duel, entGetRef(e2));

	if(!s1 || !s2)
		return;

	if(s1==duel->challenger)				// Only challenged can accept
		return;

	duel->timeAccept = ABS_TIME_PARTITION(duel->iPartitionIdx);
	e1->pChar->pvpDuelState->state = DuelState_Active;
	e2->pChar->pvpDuelState->state = DuelState_Active;
	entity_SetDirtyBit(e1, parse_Character, e1->pChar, false);
	entity_SetDirtyBit(e2, parse_Character, e2->pChar, false);
}
void gslPVPDuelDecline(Entity *e)
{
	PVPDuelState *state = SAFE_MEMBER2(e, pChar, pvpDuelState);
	if(!state)
		return;

	entity_SetDirtyBit(e, parse_Character, e->pChar, false);
	gslPVPDuelDestroy(state->duel);
}

void gslPVPDuelEnd(Entity *e)
{
	PVPDuel *duel = e->pChar->pvpDuelState->duel;
	PVPSide *side = gslPVPDuelFindSide(duel, entGetRef(e));

	gslPVPSendDefeatFloater(duel, e);

	gslPVPDuelCleanup(e, side);

	if(eaiSize(&side->members)==0)
	{
		eaFindAndRemoveFast(&duel->sides, side);
		gslPVPSideDestroy(duel->iPartitionIdx, side);

		if(eaSize(&duel->sides)<=1)
			gslPVPDuelDestroy(duel);
	}
}

void gslPVPDuelInit(void)
{
	/*
	if(!msgExists(DUEL_START_WARNING))
	ErrorFilenamef("messages/pvp/PVP_Duel.ms", "Unable to find msg:"DUEL_START_WARNING);
	if(!msgExists(DUEL_START))
	ErrorFilenamef("messages/pvp/PVP_Duel.ms", "Unable to find msg:"DUEL_START);
	if(!critter_DefGetByName("PVP_Flag_Critter"))
	Errorf("Unable to find critter: PVP_Flag_Critter, tell Adam");
	*/
	//if(!dynFxInfoExists("PVP_Glowy_Dome"))
	//	Errorf("Unable to find FX: PVP_Glowy_Dome for PVP, tell Adam");
}

// INFECTIOUS PVP!!! --------------------------

void gslPVPInfect(Entity *source, Entity *target, int enemy)
{
	PVPFlag *target_flag;
	if(!source)
		return;

	target_flag = target->pChar->pvpFlag;

	if(!target_flag)
		return;

	if(source->pChar->pvpFlag)
		return;

	if(target_flag->infectious)
	{
		devassert(target_flag->infect);
		if(enemy)
		{
			PVPFlag *source_flag = StructCreate(parse_PVPFlag);
			gslPVPGroupAddSubGroup(target_flag->infect->group, source_flag);

			source->pChar->pvpFlag = source_flag;
			entity_SetDirtyBit(source, parse_Character, source->pChar, false);

			source_flag->infect = target_flag->infect;  // Transmit
			source_flag->infectious = target_flag->infectious;
			source_flag->infect_heal = target_flag->infect_heal;
			eaiPush(&source_flag->infect->members, entGetRef(source));
		}
		else if(target_flag->infect_heal && !enemy)
		{
			PVPFlag *source_flag = StructCreate(parse_PVPFlag);

			source_flag->group = target_flag->group;
			source_flag->subgroup = target_flag->subgroup;

			source->pChar->pvpFlag = source_flag;
			entity_SetDirtyBit(source, parse_Character, source->pChar, false);

			source_flag->infect = target_flag->infect;  // Transmit
			source_flag->infectious = target_flag->infectious;
			source_flag->infect_heal = target_flag->infect_heal;
			eaiPush(&source_flag->infect->members, entGetRef(source));
		}
	}
}

void gslPVPModNotify(Entity *source, Entity *target, AttribMod* mod, AttribModDef* moddef)
{
	gslPVPInfect(source, target, IS_DAMAGE_ATTRIBASPECT(moddef->offAttrib, moddef->offAspect));
}

void gslPVPInfectEnt(Entity *e, F32 radius, U32 allowHeal, U32 allowCombatOut)
{
	PVPInfect *infect;
	PVPFlag *flag;
	if(!e->pChar)
		return;

	infect = calloc(1, sizeof(PVPInfect));
	infect->iPartitionIdx = entGetPartitionIdx(e);
	infect->origin_ent = entGetRef(e);
	infect->type = PIT_Entity;
	infect->infect_dist = radius;
	infect->group = calloc(1, sizeof(PVPGroup));
	infect->group->group = gslPVPGetNewGroup();

	flag = StructCreate(parse_PVPFlag);
	flag->infectious = 1;
	flag->infect_heal = allowHeal;
	flag->infect = infect;
	flag->combat_out = allowCombatOut;
	e->pChar->pvpFlag = flag;	
	entity_SetDirtyBit(e, parse_Character, e->pChar, false);
	gslPVPGroupAddSubGroup(infect->group, flag);
	eaiPush(&flag->infect->members, entGetRef(e));

	eaPush(&g_pvp_state.infects, flag->infect);
}

void gslPVPInfectDestroy(PVPInfect *infect)
{
	// The origin is gone - clean up all members
	int i;

	for(i=eaiSize(&infect->members)-1; i>=0; i--)
	{
		Entity *member = entFromEntityRef(infect->iPartitionIdx, infect->members[i]);

		if(member)
			gslPVPInfectEnd(member);
	}
	eaiDestroy(&infect->members);
	free(infect->group);

	eaFindAndRemoveFast(&g_pvp_state.infects, infect);
	free(infect);
}

void gslPVPInfectEnd(Entity *e)
{
	PVPFlag *flag = e->pChar->pvpFlag;
	EntityRef er = entGetRef(e);

	if(!flag || !flag->infect)
		return;

	// MUST REMOVE FIRST or this will infinitely recurse when origin dies
	eaiFindAndRemoveFast(&flag->infect->members, er);
	
	if(flag->infect->type==PIT_Entity && flag->infect->origin_ent==er)
		gslPVPInfectDestroy(flag->infect);

	StructDestroySafe(parse_PVPFlag, &e->pChar->pvpFlag);
	entity_SetDirtyBit(e, parse_Character, e->pChar, false);
}

void gslPVPInfectTick(void)
{
	int i;

	for(i=eaSize(&g_pvp_state.infects)-1; i>=0; i--)
	{
		int j;
		PVPInfect *infect = g_pvp_state.infects[i];
		Vec3 origin_pos;
		
		if(infect->type==PIT_Entity)
		{
			Entity *e = entFromEntityRef(infect->iPartitionIdx, infect->origin_ent);

			if(!e)
			{
				gslPVPInfectDestroy(infect);
				continue;
			}

			entGetPos(e, origin_pos);
		}

		for(j=eaiSize(&infect->members)-1; j>=0; j--)
		{
			Entity *member = entFromEntityRef(infect->iPartitionIdx, infect->members[j]);
			Vec3 pos;

			if(!member)
			{
				eaiRemoveFast(&infect->members, j);
				continue;
			}

			switch(infect->type)
			{
				xcase PIT_Entity: {
					entGetPos(member, pos);
					if(distance3Squared(pos, origin_pos)>SQR(infect->infect_dist))
					{
						gslPVPInfectEnd(member);
						continue;
					}
				}
			}
		}
	}
}

// General PVP funcs ----------------------------

void gslPVPJoinGroupEnt(Entity *e, Entity *source, PVPFlagParams *params)
{
	PVPFlag *flag;
	PVPGroupInst *inst = NULL;
	int subgroup = 0;
	if(!e->pChar)
		return;

	if(!stashAddressFindPointer(g_pvp_groups, params->pchGroupName, &inst))
	{
		inst = calloc(1, sizeof(PVPGroupInst));
		inst->group = calloc(1, sizeof(PVPGroup));
		inst->group->group = gslPVPGetNewGroup();
		stashAddressAddPointer(g_pvp_groups, params->pchGroupName, inst, 1);		
	}

	subgroup = eaFind(&inst->subGroups, params->pchSubGroupName);
	if(subgroup==-1)
		subgroup = eaPush(&inst->subGroups, params->pchSubGroupName);

	flag = StructCreate(parse_PVPFlag);
	flag->combat_out = params->bAllowExternCombat;
	flag->group = inst->group->group;
	flag->subgroup = subgroup;
	e->pChar->pvpFlag = flag;	
	entity_SetDirtyBit(e, parse_Character, e->pChar, false);
}

void gslPVPEnd(Entity *e)
{
	StructDestroySafe(parse_PVPFlag, &e->pChar->pvpFlag);
	entity_SetDirtyBit(e, parse_Character, e->pChar, false);
}

void gslPVPCleanup(Entity *e)
{
	if (!e || !e->pChar)
		return;

	if(e->pChar->pvpDuelState)
		gslPVPDuelEnd(e);
	else if(e->pChar->pvpFlag && e->pChar->pvpFlag->infect)
		gslPVPInfectEnd(e);
	else if(e->pChar->pvpFlag)
		gslPVPEnd(e);
	else if(e->pChar->pvpTeamDuelFlag)
		gslPVPTeamDuelDeath(e);
}

AUTO_STARTUP(PVP) ASTRT_DEPS(Critters, WorldLibMain, PVPCommon);
void gslPVPInit(void)
{
	int inited = 0;

	if(!inited)
	{
		inited = 1;

		gslPVPDuelInit();

		g_pvp_groups = stashTableCreateAddress(10);
	}
}

void gslPVPTick(void)
{
	gslPVPDuelTick();

	gslPVPInfectTick();
}

AUTO_COMMAND ACMD_NAME(PVPFillFakeTeamInfo);
void exprFuncFillFakeTeamPVPInfo(Entity *pEnt)
{
	PVPTeamFlag *pFlag = NULL;
	PVPTeamDuel *pDuel = NULL;
	PVPTeam *pTeam = NULL;
	PVPTeamMember *pMember = NULL;


	if(!pEnt || !pEnt->pPlayer)
	{
		return;
	}

	pFlag = pEnt->pChar->pvpTeamDuelFlag = StructCreate(parse_PVPTeamFlag);

	pFlag->eState = DuelState_Invite;
	pFlag->requester_name = StructAllocString("Requester Name");
	pFlag->group = 1;
	pFlag->team = 1;
	
	pDuel = pFlag->team_duel = StructCreate(parse_PVPTeamDuel);

	pDuel->group = 1;
	pDuel->iPartitionIdx = entGetPartitionIdx(pEnt);
	
	//team 1
	pTeam = StructCreate(parse_PVPTeam);
	pTeam->iTeamID = 1;
	eaPush(&pDuel->ppTeams,pTeam);

	//Member 1, myself
	pMember = StructCreate(parse_PVPTeamMember);
	pMember->eStatus = DuelState_Request;
	pMember->iEntID = pEnt->myContainerID;
	pMember->iLevel = pEnt->pChar->iLevelCombat;
	pMember->pchName = StructAllocString(entGetLangName(pEnt, entGetLanguage(pEnt)));
	eaPush(&pTeam->ppMembers,pMember);
	//Member 2, friendly who hasn't accepted yet
	pMember = StructCreate(parse_PVPTeamMember);
	pMember->eStatus = DuelState_Invite;
	pMember->iEntID = -1;
	pMember->iLevel = 23;
	pMember->pchName = StructAllocString("Lazy Teammate");
	eaPush(&pTeam->ppMembers,pMember);
	//Member 3, friendly who has accepted
	pMember = StructCreate(parse_PVPTeamMember);
	pMember->eStatus = DuelState_Accepted;
	pMember->iEntID = -2;
	pMember->iLevel = 26;
	pMember->pchName = StructAllocString("Teammate who likes you");
	eaPush(&pTeam->ppMembers,pMember);
	
	
	//Team 2
	pTeam = StructCreate(parse_PVPTeam);
	pTeam->iTeamID = 2;
	eaPush(&pDuel->ppTeams,pTeam);

	//Member 1, foe who hasn't accepted yet
	pMember = StructCreate(parse_PVPTeamMember);
	pMember->eStatus = DuelState_Invite;
	pMember->iEntID = -3;
	pMember->iLevel = 28;
	pMember->pchName = StructAllocString("Lazy Foe");
	eaPush(&pTeam->ppMembers,pMember);
	//Member 2, foe who has accepted
	pMember = StructCreate(parse_PVPTeamMember);
	pMember->eStatus = DuelState_Accepted;
	pMember->iEntID = -4;
	pMember->iLevel = 12;
	pMember->pchName = StructAllocString("Foe who wants to fight");
	eaPush(&pTeam->ppMembers,pMember);
	//Member 3, foe who has refused to fight
	pMember = StructCreate(parse_PVPTeamMember);
	pMember->eStatus = DuelState_Decline;
	pMember->iEntID = -5;
	pMember->iLevel = 34;
	pMember->pchName = StructAllocString("Foe who is afraid");
	eaPush(&pTeam->ppMembers,pMember);

	ClientCmd_gclPVPTeamDuelUpdateStruct(pEnt,pDuel);
}
