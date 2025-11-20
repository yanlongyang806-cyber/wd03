
#include "gslTeamUp.h"
#include "TeamUpCommon.h"
#include "Entity.h"

AUTO_COMMAND ACMD_ACCESSLEVEL(0) ACMD_NAME(TeamUp_Join) ACMD_HIDE;
void gslTeamUpCMD_JoinTeamUp(Entity *pEntity)
{
	if(pEntity->pTeamUpRequest)
	{
		int iPartitionIdx = entGetPartitionIdx(pEntity);
		TeamUpInstance *pInstance = gslTeamUpInstance_FromKey(iPartitionIdx,pEntity->pTeamUpRequest->uTeamID);
		
		if(pInstance)
		{
			gslTeamUp_EntityJoinTeam(iPartitionIdx, pEntity, pInstance);
		}
	}
}

AUTO_COMMAND ACMD_ACCESSLEVEL(0) ACMD_NAME(TeamUp_Leave) ACMD_HIDE;
void gslTeamUpCMD_LeaveTeamUp(Entity *pEntity)
{
	if(pEntity->pTeamUpRequest)
	{
		gslTeamUp_LeaveTeam(pEntity,false);
	}
}

AUTO_COMMAND ACMD_ACCESSLEVEL(0) ACMD_NAME(TeamUp_GroupRequest) ACMD_HIDE;
void gslTeamUpCMD_GroupRequest(Entity *pEntity, U32 iGroupIdx)
{
	if(pEntity->pTeamUpRequest)
		gslTeamUp_GroupChangeRequest(pEntity,iGroupIdx);
}

AUTO_COMMAND ACMD_ACCESSLEVEL(0) ACMD_NAME(TeamUp_NewGroupRequest) ACMD_HIDE;
void gslTeamUpCMD_NewGroupRequest(Entity *pEntity)
{
	if(pEntity->pTeamUpRequest)
		gslTeamUp_GroupChangeRequest(pEntity,-1);
}