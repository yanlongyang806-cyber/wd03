#include "gslPVP.h"

#include "Character.h"
#include "Entity.h"
#include "Expression.h"
#include "Player.h"

#include "AutoGen/GameClientLib_autogen_ClientCmdWrappers.h"

AUTO_COMMAND ACMD_SERVERCMD ACMD_ACCESSLEVEL(0) ACMD_PRIVATE;
void gslPVPDuelRequestCmd(Entity *client, EntityRef challenged)
{
	gslPVPDuelRequest(client, entFromEntityRef(entGetPartitionIdx(client), challenged));
}

AUTO_COMMAND ACMD_SERVERCMD ACMD_ACCESSLEVEL(0) ACMD_PRIVATE;
void gslPVPTeamDuelRequestCmd(Entity *client, EntityRef challenged)
{
	gslPVPTeamDuelRequest(client, entFromEntityRef(entGetPartitionIdx(client), challenged));
}

AUTO_COMMAND ACMD_SERVERCMD ACMD_ACCESSLEVEL(0) ACMD_PRIVATE;
void gslPVPDuelAcceptCmd(Entity *client)
{
	gslPVPDuelAccept(client);
}

AUTO_COMMAND ACMD_SERVERCMD ACMD_ACCESSLEVEL(0) ACMD_PRIVATE;
void gslPVPDuelDeclineCmd(Entity *client)
{
	gslPVPDuelDecline(client);
}

AUTO_COMMAND ACMD_SERVERCMD ACMD_ACCESSLEVEL(0) ACMD_PRIVATE;
void gslPVPTeamDuelAcceptCmd(Entity *client)
{
	gslPVPTeamDuelAccept(client);
}

AUTO_COMMAND ACMD_SERVERCMD ACMD_ACCESSLEVEL(0) ACMD_PRIVATE;
void gslPVPTeamDuelDeclineCmd(Entity *client)
{
	gslPVPTeamDuelDecline(client);
}

AUTO_COMMAND ACMD_SERVERCMD ACMD_ACCESSLEVEL(0) ACMD_PRIVATE;
void gslPVPTeamDuelSurrenderCmd(Entity *client)
{
	gslPVPTeamDuelSurrender(client);
}

AUTO_EXPR_FUNC(ai) ACMD_NAME(PVPInfectStart);
void exprFuncPVPInfectStart(ACMD_EXPR_SELF Entity *e, F32 radius, int allow_heal)
{
	gslPVPInfectEnt(e, radius, allow_heal, 1);
}

AUTO_EXPR_FUNC(ai) ACMD_NAME(PVPInfectStop);
void exprFuncPVPInfectStop(ACMD_EXPR_SELF Entity *e)
{
	gslPVPInfectEnd(e);
}

//Set the Whitelist for duels
AUTO_COMMAND ACMD_SERVERCMD ACMD_ACCESSLEVEL(0) ACMD_NAME(Whitelist_Duels, Whitelist_PvPInvites);
void gslSetWhitelistPvPInvites_cmd(Entity* pEnt, bool enabled)
{
	if (!pEnt || !pEnt->pPlayer)
	{
		return;
	}
	if(enabled) {
		pEnt->pPlayer->eWhitelistFlags |= kPlayerWhitelistFlags_PvPInvites;
	} else {
		pEnt->pPlayer->eWhitelistFlags &= ~kPlayerWhitelistFlags_PvPInvites;
	}
	entity_SetDirtyBit(pEnt, parse_Player, pEnt->pPlayer, false);
}