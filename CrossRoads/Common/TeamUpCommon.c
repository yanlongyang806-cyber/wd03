#include "TeamUpCommon.h"
#include "Entity.h"
#include "Message.h"
#include "EntityLib.h"


#include "autogen/entity_h_ast.h"
#include "autogen/Message_h_ast.h"
#include "AutoGen/TeamUpCommon_h_ast.h"

AUTO_RUN_ANON(memBudgetAddMapping(__FILE__, BUDGET_GameSystems););

int TeamUp_GetTeamListFromGroupSelfFirst(Entity *pEnt, Entity*** peaTeamEnts, TeamUpMember*** peaTeamMembers, int iGroupIdx, bool bIncludeSelf, bool bIncludePets)
{
	TeamUpGroup *pGroup = NULL;
	int iCount = 0;
	int iPartitionIdx;

	if(!pEnt || !pEnt->pTeamUpRequest)
		return 0;

	pGroup = eaIndexedGetUsingInt(&pEnt->pTeamUpRequest->ppGroups,iGroupIdx);
	iPartitionIdx = entGetPartitionIdx(pEnt);

	if(pGroup)
	{
		int i;
		if(bIncludeSelf)
		{
			for(i=0;i<eaSize(&pGroup->ppMembers);i++)
			{
				if(pGroup->ppMembers[i]->iEntID == pEnt->myContainerID)
				{
					eaPush(peaTeamMembers,pGroup->ppMembers[i]);
					eaPush(peaTeamEnts,pEnt);
					iCount++;
				}
			}
		}
		if(bIncludePets)
		{
			//Deal with pets in a bit
		}
		for(i=0;i<eaSize(&pGroup->ppMembers);i++)
		{
			Entity *pTeamEnt = NULL;

			if(pGroup->ppMembers[i]->iEntID == pEnt->myContainerID)
				continue;

			pTeamEnt = entFromContainerID(iPartitionIdx,GLOBALTYPE_ENTITYPLAYER,pGroup->ppMembers[i]->iEntID);
			if(!pTeamEnt)
			{
				pTeamEnt = GET_REF(pGroup->ppMembers[i]->hEnt);
			}
			if(pTeamEnt)
			{
				eaPush(peaTeamEnts,pTeamEnt);
				eaPush(peaTeamMembers,pGroup->ppMembers[i]);
				iCount++;

				if(bIncludePets)
				{
					//Deal with pets in a bit
				}
			}
		}
	}

	return iCount;
}

// Gets all team ents, and optionally pets. Prioritizes self first. Returns the number of entities added to peaTeamEnts.
int TeamUp_GetTeamListSelfFirst(Entity* pEnt, Entity*** peaTeamEnts, TeamUpMember*** peaTeamMembers, int iGroupIndex, bool bIncludeSelf, bool bIncludePets)
{
	int iCount = 0;

	if (!pEnt || !peaTeamEnts || !peaTeamMembers)
		return 0;

	if(!pEnt->pTeamUpRequest)
		return 0;

	if(iGroupIndex < -1)
		return 0;

	if(iGroupIndex == -1)
	{
		int i;

		for(i=0;i<eaSize(&pEnt->pTeamUpRequest->ppGroups);i++)
		{
			iCount += TeamUp_GetTeamListFromGroupSelfFirst(pEnt,peaTeamEnts,peaTeamMembers,pEnt->pTeamUpRequest->ppGroups[i]->iGroupIndex,bIncludeSelf,bIncludePets);
		}
	}
	else
	{
		iCount = TeamUp_GetTeamListFromGroupSelfFirst(pEnt,peaTeamEnts,peaTeamMembers,iGroupIndex,bIncludeSelf,bIncludePets);
	}
	
	return iCount;
}

// Adds all members of this team-up to the passed in entity list. Will not add an entity if it already exists in the list.
void TeamUp_AddTeamToEntityList(Entity* pEnt, TeamUpGroup **ppGroups, Entity*** peaEntityList, Entity *pExcludeEntity, bool bIncludeSelf)
{
	int i;
	int iTeamSize;

	if (!pEnt || !ppGroups || !peaEntityList)
	{
		return;
	}

	iTeamSize = eaSize(peaEntityList);

	for (i = 0; i < eaSize(&ppGroups); i++)
	{
		TeamUpGroup *pGroup = eaIndexedGetUsingInt(&ppGroups, i);
		int iPartitionIdx = entGetPartitionIdx(pEnt);
		if (pGroup)
		{
			int j;
			for (j = 0; j < eaSize(&pGroup->ppMembers); j++)
			{
				Entity *pTeamEnt = NULL;

				if (!bIncludeSelf && pGroup->ppMembers[j]->iEntID == pEnt->myContainerID)
					continue;

				if (pExcludeEntity && pGroup->ppMembers[j]->iEntID == pExcludeEntity->myContainerID)
					continue;

				pTeamEnt = entFromContainerID(iPartitionIdx, GLOBALTYPE_ENTITYPLAYER, pGroup->ppMembers[j]->iEntID);
				if (!pTeamEnt)
				{
					pTeamEnt = GET_REF(pGroup->ppMembers[j]->hEnt);
				}

				if (pTeamEnt)
				{
					int k;
					bool bFound = false;

					for(k = 0; k < iTeamSize; k++)
					{
						if((*peaEntityList)[k] == pTeamEnt)
						{
							bFound = true;
							break;
						}
					}

					if(!bFound)
					{
						eaPush(peaEntityList, pTeamEnt);
					}
				}
			}
		}
	}
}

#include "AutoGen/TeamUpCommon_h_ast.c"