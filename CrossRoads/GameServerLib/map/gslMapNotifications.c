#include "Entity.h"
#include "EntityLib.h"
#include "Team.h"
#include "MapNotificationsCommon.h"

#include "AutoGen/GameClientLib_autogen_ClientCmdWrappers.h"

AUTO_RUN_ANON(memBudgetAddMapping(__FILE__, BUDGET_GameSystems););

AUTO_COMMAND ACMD_NAME(PushEntityMapNotification) ACMD_ACCESSLEVEL(0) ACMD_SERVERCMD ACMD_PRIVATE;
void gslMapNotifications_cmdPushEntityMapNotification(Entity *pPlayerEnt, S32 eNotificationType)
{
	const MapNotificationDef *pNotificationDef = mapNotification_DefFromType((MapNotificationType)eNotificationType);

	// Get the player's team
	Team *pTeam = team_GetTeam(pPlayerEnt);

	if (pNotificationDef && pTeam)
	{
		FOR_EACH_IN_CONST_EARRAY_FORWARDS(pTeam->eaMembers, TeamMember, pTeamMember)
		{
			if (pTeamMember->iEntID != entGetContainerID(pPlayerEnt))
			{
				Entity *pEntTeamMember = entFromContainerIDAnyPartition(GLOBALTYPE_ENTITYPLAYER, pTeamMember->iEntID);
				if (pEntTeamMember)
				{
					// Register the notification with the other team member
					ClientCmd_ReceiveEntityMapNotification(pEntTeamMember, entGetType(pPlayerEnt), entGetContainerID(pPlayerEnt), eNotificationType);
				}
			}		
		}
		FOR_EACH_END
	}
}