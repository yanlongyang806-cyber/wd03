#include "TeamUpCommon.h"
#include "Entity.h"
#include "gclEntity.h"
#include "earray.h"
#include "textparser.h"

#include "AutoGen/TeamUpCommon_h_ast.h"

AUTO_RUN_ANON(memBudgetAddMapping(__FILE__, BUDGET_GameSystems););

AUTO_COMMAND ACMD_CLIENTCMD ACMD_PRIVATE ACMD_ACCESSLEVEL(0);
void gclRemoveTeamUpGroup(int iGroupIdx)
{
	Entity *pEnt = entActivePlayerPtr();
	int i;
	TeamUpGroup *pOldGroup = NULL;
	
	if(!pEnt || !pEnt->pTeamUpRequest)
		return;

	i = eaIndexedFindUsingInt(&pEnt->pTeamUpRequest->ppGroups,iGroupIdx);
	pOldGroup = pEnt->pTeamUpRequest->ppGroups[i];

	eaRemove(&pEnt->pTeamUpRequest->ppGroups,i);

	StructDestroy(parse_TeamUpGroup,pOldGroup);
}

AUTO_COMMAND ACMD_CLIENTCMD ACMD_PRIVATE ACMD_ACCESSLEVEL(0);
void gclUpdateTeamUpGroup(TeamUpGroup *pNewGroup)
{
	Entity *pEnt = entActivePlayerPtr();
	TeamUpGroup *pOldGroup = NULL;

	if(!pEnt || !pEnt->pTeamUpRequest)
		return;

	pOldGroup = eaIndexedGetUsingInt(&pEnt->pTeamUpRequest->ppGroups,pNewGroup->iGroupIndex);

	if(!pOldGroup)
	{
		pOldGroup = StructClone(parse_TeamUpGroup,pNewGroup);

		eaIndexedAdd(&pEnt->pTeamUpRequest->ppGroups,pOldGroup);
	}
	else
	{
		StructCopyAll(parse_TeamUpGroup,pNewGroup,pOldGroup);
	}
}

AUTO_COMMAND ACMD_CLIENTCMD ACMD_PRIVATE ACMD_ACCESSLEVEL(0);
void gclInitTeamUp(TeamUpInit *pInit)
{
	Entity *pEnt = entActivePlayerPtr();
	int i;

	for(i=0;i<eaSize(&pInit->ppGroups);i++)
	{
		gclUpdateTeamUpGroup(pInit->ppGroups[i]);
	}
}