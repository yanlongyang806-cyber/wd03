#include "gclPVP.h"
#include "pvp_common.h"

#include "Character.h"
#include "Player.h"
#include "Entity.h"
#include "entEnums.h"
#include "Expression.h"
#include "gclEntity.h"
#include "UIGen.h"
#include "ClientTargeting.h"
#include "GameStringFormat.h"
#include "Character_Target.h"
#include "WorldGrid.h"
#include "NotifyCommon.h"

#include "AutoGen/GameServerLib_autogen_ServerCmdWrappers.h"
#include "gclPVP_eval_c_ast.h"
#include "AutoGen/pvp_common_h_ast.h"

AUTO_RUN_ANON(memBudgetAddMapping(__FILE__, BUDGET_UISystem););

#define PVP_PLAYER_KILLED_MSG			"PvP_PlayerKilled"
#define PVP_PLAYER_KILLED_NOSOURCE_MSG	"PvP_PlayerKilledNoSource"

AUTO_STRUCT;
typedef struct PVPMemberData {
	Entity *ent;				AST(UNOWNED)
	const char *name;			AST(UNOWNED)
	PVPDuelEntityState state;
} PVPMemberData;

AUTO_COMMAND ACMD_CLIENTCMD ACMD_ACCESSLEVEL(0) ACMD_PRIVATE;
void gclPVPTeamDuelUpdateStruct(PVPTeamDuel *pNewStruct)
{
	Entity *e = entActivePlayerPtr();

	if(!SAFE_MEMBER2(e, pChar, pvpTeamDuelFlag))
		return;

	if(e->pChar->pvpTeamDuelFlag->team_duel)
		StructDestroy(parse_PVPTeamDuel,e->pChar->pvpTeamDuelFlag->team_duel);

	if(pNewStruct)
		e->pChar->pvpTeamDuelFlag->team_duel = StructClone(parse_PVPTeamDuel,pNewStruct);
}

AUTO_COMMAND ACMD_CLIENTCMD ACMD_ACCESSLEVEL(0) ACMD_HIDE;
void gclPVPDuelRequestAck(Entity *other)
{

}

AUTO_COMMAND ACMD_CLIENTCMD ACMD_ACCESSLEVEL(0) ACMD_HIDE;
void gclPVPDuelBeginAck(Entity *other)
{

}

AUTO_COMMAND ACMD_NAME(pvp_DuelAccept) ACMD_ACCESSLEVEL(0) ACMD_HIDE;
void gclPVPDuelAccept(void)
{
	ServerCmd_gslPVPDuelAcceptCmd();
}

AUTO_COMMAND ACMD_NAME(pvp_DuelDecline) ACMD_ACCESSLEVEL(0) ACMD_HIDE;
void gclPVPDuelDecline(void)
{
	ServerCmd_gslPVPDuelDeclineCmd();
}

AUTO_COMMAND ACMD_NAME(pvp_TeamDuelAccept) ACMD_ACCESSLEVEL(0) ACMD_HIDE;
void gclPVPTeamDuelAccept(void)
{
	ServerCmd_gslPVPTeamDuelAcceptCmd();
}

AUTO_COMMAND ACMD_NAME(pvp_TeamDuelDecline) ACMD_ACCESSLEVEL(0) ACMD_HIDE;
void gclPVPTeamDuelDecline(void)
{
	ServerCmd_gslPVPTeamDuelDeclineCmd();
}

AUTO_COMMAND ACMD_NAME(pvp_TeamDuelSurrender) ACMD_ACCESSLEVEL(0) ACMD_HIDE;
void gclPVPTeamDuelSurrnder(void)
{
	ServerCmd_gslPVPTeamDuelSurrenderCmd();
}

/*
AUTO_COMMAND ACMD_NAME(pvp_SpecialAction) ACMD_ACCESSLEVEL(0);
void gclPVPSpecialAction(void)
{
	Vec3 vTarget;
	Entity *pTarget = NULL;
	Entity *pPlayer = entActivePlayerPtr();

	pTarget = target_SelectUnderMouseEx(pPlayer,NULL, 0, 0, vTarget, true, false, false);

	if(pTarget)
	{
		entGetCombatPosDir(pTarget,NULL,vTarget,NULL);
	}

	ServerCmd_gslPVPGame_SpecialAction(vTarget);
}
*/

AUTO_EXPR_FUNC(UIGen) ACMD_NAME(pvp_CanDuelRef);
int exprFuncPVPCanDuelRef(EntityRef target)
{
	Entity *e = entActivePlayerPtr();
	return pvpCanDuel(e, entFromEntityRefAnyPartition(target));
}

AUTO_EXPR_FUNC(UIGen) ACMD_NAME(pvp_CanTeamDuelRef);
int exprFuncPVPCanTeamDuelRef(EntityRef target)
{
	Entity *e = entActivePlayerPtr();
	return pvpCanTeamDuel(e,entFromEntityRefAnyPartition(target));
}

AUTO_EXPR_FUNC(UIGen) ACMD_NAME(pvp_DuelRequestRef);
void exprFuncPVPDuelRequestRef(EntityRef target)
{
	ServerCmd_gslPVPDuelRequestCmd(target);
}

AUTO_EXPR_FUNC(UIGen) ACMD_NAME(pvp_TeamDuelRequestRef);
void exprFuncPVPTeamDuelRequestRef(EntityRef target)
{
	ServerCmd_gslPVPTeamDuelRequestCmd(target);
}

AUTO_EXPR_FUNC(UIGen) ACMD_NAME(pvp_HasDuelInvite);
int exprFuncPVPHasDuelInvite(EntityRef er)
{
	Entity *e = entFromEntityRefAnyPartition(er);
	if(!SAFE_MEMBER2(e, pChar, pvpDuelState))
		return 0;
	return e->pChar->pvpDuelState->state==DuelState_Invite;
}

AUTO_EXPR_FUNC(UIGen) ACMD_NAME(pvp_HasDuelRequest);
int exprFuncPVPHasDuelRequest(EntityRef er)
{
	Entity *e = entFromEntityRefAnyPartition(er);
	if(!SAFE_MEMBER2(e, pChar, pvpDuelState))
		return 0;
	return e->pChar->pvpDuelState->state==DuelState_Request;
}

AUTO_EXPR_FUNC(UIGen) ACMD_NAME(pvp_HasTeamDuelAccept);
int exprFuncPVPHasDuelAccept(EntityRef er)
{
	Entity *e = entFromEntityRefAnyPartition(er);
	if(!SAFE_MEMBER2(e, pChar, pvpTeamDuelFlag))
		return 0;
	return e->pChar->pvpTeamDuelFlag->eState==DuelState_Accepted;
}

AUTO_EXPR_FUNC(UIGen) ACMD_NAME(pvp_HasTeamDuelInvite);
int exprFuncPVPHasTeamDuelInvite(EntityRef er)
{
	Entity *e = entFromEntityRefAnyPartition(er);
	if(!SAFE_MEMBER2(e, pChar, pvpTeamDuelFlag))
		return 0;
	return  e->pChar->pvpTeamDuelFlag->eState==DuelState_Invite;
}

AUTO_EXPR_FUNC(UIGen) ACMD_NAME(pvp_HasActiveTeamDuel);
int exprFuncPVPHasActiveTeamDuel(EntityRef er)
{
	Entity *e = entFromEntityRefAnyPartition(er);
	if(SAFE_MEMBER2(e, pChar, pvpTeamDuelFlag))
		return 1;
	return  0;
}

AUTO_EXPR_FUNC(UIGen) ACMD_NAME(pvp_HasDuelAccept);
int exprFuncPVPHasTeamDuelAccept(EntityRef er)
{
	Entity *e = entFromEntityRefAnyPartition(er);
	if(!SAFE_MEMBER2(e, pChar, pvpDuelState))
		return 0;
	return e->pChar->pvpDuelState->state==DuelState_Accepted;
}

AUTO_EXPR_FUNC(UIGen) ACMD_NAME(pvp_GetMemberList);
void exprFuncPVPGetMemberList(UIGen *gen, Entity *e)
{
	static PVPMemberData **data = NULL;

	eaClearStruct(&data, parse_PVPMemberData);

	if(SAFE_MEMBER2(e, pChar, pvpDuelState))
	{
		int i;
		PVPDuelState *duelstate = e->pChar->pvpDuelState;
		for(i=eaiSize(&duelstate->members)-1; i>=0; i--)
		{
			PVPMemberData *inst = StructCreate(parse_PVPMemberData);

			inst->ent = entFromEntityRefAnyPartition(duelstate->members[i]);
			inst->name = entGetLocalName(inst->ent);
			eaPush(&data, inst);
		}
	}

	ui_GenSetManagedListSafe(gen, &data, PVPMemberData, false);
}

AUTO_EXPR_FUNC(UIGen) ACMD_NAME(pvp_GetTeamDuelList);
void exprFuncPVPGetTeamDuelList(UIGen *gen, int iTeam)
{
	PVPTeamMember **data = NULL;
	Entity *e = entActivePlayerPtr();

	if(SAFE_MEMBER2(e, pChar, pvpTeamDuelFlag))
	{
		if(e->pChar->pvpTeamDuelFlag->team_duel && iTeam < eaSize(&e->pChar->pvpTeamDuelFlag->team_duel->ppTeams))
			eaCopyStructs(&e->pChar->pvpTeamDuelFlag->team_duel->ppTeams[iTeam]->ppMembers,&data,parse_PVPTeamMember);

	}

	ui_GenSetManagedList(gen, &data, parse_PVPTeamMember, true);
}

AUTO_EXPR_FUNC(UIGen) ACMD_NAME(pvp_TeamMemberHasState);
int exprFunPVPTeamMemberHasState(PVPTeamMember *pMember, const char *pchState)
{
	PVPDuelEntityState eState = StaticDefineIntGetInt(PVPDuelEntityStateEnum,pchState);

	if(pMember && pMember->eStatus == eState)
		return 1;

	return 0;
}

AUTO_EXPR_FUNC(UIGen) ACMD_NAME(pvp_GetEstimatedWaveTimer);
int exprFuncPvpGetEstimatedWaveTimer()
{
	U32 uiWaveRespawnTime = zmapInfoGetRespawnWaveTime(NULL);
	return uiWaveRespawnTime - (timeSecondsSince2000() % uiWaveRespawnTime);
}

// the server is telling us the source killed the target.
AUTO_COMMAND ACMD_CLIENTCMD ACMD_ACCESSLEVEL(0) ACMD_PRIVATE ACMD_HIDE;
void gclPvP_NotifyKilled(EntityRef erSource, EntityRef erTarget)
{
	Entity *eFactionEnt = entActivePlayerPtr();
	Entity *eSource = erSource ? entFromEntityRefAnyPartition(erSource) : NULL;
	Entity *eTarget = erTarget ? entFromEntityRefAnyPartition(erTarget) : NULL;
	char *estrBuffer = NULL;
	const char *pchMessage = PVP_PLAYER_KILLED_MSG;
	S32 bSourceEnemy = 1, bTargetEnemy = 0;
	int iPartitionIdx;

	if (!eTarget || !eFactionEnt)
		return;

	iPartitionIdx = entGetPartitionIdx(eTarget);
	bTargetEnemy = critter_IsKOS(iPartitionIdx, eFactionEnt, eTarget);

	if (!eSource)
	{
		pchMessage = PVP_PLAYER_KILLED_NOSOURCE_MSG;
	}
	else
	{
		bSourceEnemy = critter_IsKOS(iPartitionIdx, eFactionEnt, eSource);
	}
	
	entFormatGameMessageKey(eFactionEnt, &estrBuffer, pchMessage,
							STRFMT_ENTITY_KEY("Source", eSource),
							STRFMT_ENTITY_KEY("Target", eTarget),
							STRFMT_INT("SourceEnemy",bSourceEnemy),
							STRFMT_INT("TargetEnemy",bTargetEnemy),
							STRFMT_END);
	
	notify_NotifySend(eFactionEnt, kNotifyType_PVPKill, estrBuffer, pchMessage, NULL);
}


#include "gclPVP_eval_c_ast.c"
