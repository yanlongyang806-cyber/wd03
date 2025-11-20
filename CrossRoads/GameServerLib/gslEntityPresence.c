#include "gslEntityPresence.h"

#include "EntityIterator.h"
#include "EntityLib.h"
#include "Expression.h"
#include "StashTable.h"
#include "Team.h"
#include "Player.h"
#include "entCritter.h"
#include "gslEncounter.h"
#include "wlEncounter.h"
#include "wlGroupPropertyStructs.h"

#include "Player_h_ast.h"

bool gbUpdatePresence = false;

void gslEntityPresenceRelease(Player * pPlayer)
{
	pPlayer->pPresenceInfo->iRefCount--;

	if (pPlayer->pPresenceInfo->iRefCount == 0)
	{
		ea32Destroy(&pPlayer->pPresenceInfo->perHidden);
		free(pPlayer->pPresenceInfo);
	}

	pPlayer->pPresenceInfo = NULL;
}

static void ClearStaleInfo(Entity * pEnt,Entity * pTeamLeader)
{
	if (pEnt->pPlayer->pPresenceInfo && pEnt->pPlayer->pPresenceInfo->erOwner != pTeamLeader->myRef)
	{
		gslEntityPresenceRelease(pEnt->pPlayer);
	}
}

static void ClearPresenceInfoForAllPlayers()
{
	EntityIterator* playeriter = entGetIteratorSingleTypeAllPartitions(0, 0, GLOBALTYPE_ENTITYPLAYER);
	Entity* pPlayerEnt;

	while(pPlayerEnt = EntityIteratorGetNext(playeriter))
	{
		Entity * pTeamLeader = pPlayerEnt;
		if (team_IsMember(pPlayerEnt))
		{
			pTeamLeader = team_GetTeamLeader(entGetPartitionIdx(pPlayerEnt), team_GetTeam(pPlayerEnt));
			// this case can happen if the team server is down and the player was on a team (on a team but no leader)
			// Also if team leader is on different partition, act like no team leader present
			if (!pTeamLeader)
			{
				pTeamLeader = pPlayerEnt;
			}
		}

		// disassociate this player from any stale info blocks
		ClearStaleInfo(pPlayerEnt,pTeamLeader);

		// disassociate this player from any stale info blocks
		ClearStaleInfo(pTeamLeader,pTeamLeader);

		if (pTeamLeader == pPlayerEnt && pPlayerEnt->pPlayer->pPresenceInfo)
		{
			// the team leader is currently the one who will make the decisions about entity presence.  This could change
			// if we come up with something more sophisticated
			ea32Clear(&pPlayerEnt->pPlayer->pPresenceInfo->perHidden);
		}
	}

	EntityIteratorRelease(playeriter);
}

static void UpdateTeamPresenceInfos()
{
	EntityIterator* playeriter = entGetIteratorSingleTypeAllPartitions(0, 0, GLOBALTYPE_ENTITYPLAYER);
	Entity* pPlayerEnt;

	while(pPlayerEnt = EntityIteratorGetNext(playeriter))
	{
		Entity * pTeamLeader = pPlayerEnt;
		if (team_IsMember(pPlayerEnt))
		{
			pTeamLeader = team_GetTeamLeader(entGetPartitionIdx(pPlayerEnt), team_GetTeam(pPlayerEnt));
			// this case can happen if the team server is down and the player was on a team (on a team but no leader)
			// Also if team leader is on different partition, act like no team leader present
			if (!pTeamLeader)
			{
				pTeamLeader = pPlayerEnt;
			}
		}

		if (pTeamLeader->pPlayer->pPresenceInfo != NULL)
		{
			// point me at the presence info for the team
			if (pPlayerEnt->pPlayer->pPresenceInfo == NULL)
			{
				pPlayerEnt->pPlayer->pPresenceInfo = pTeamLeader->pPlayer->pPresenceInfo;
				pPlayerEnt->pPlayer->pPresenceInfo->iRefCount++;
			}
		}
	}

	EntityIteratorRelease(playeriter);
}

static bool MakePresenceInfo(Entity * pPlayerEnt)
{
	Player * pPlayer = pPlayerEnt->pPlayer;

	// make a new info if necessary - this player is the team leader.  The other team members will pick this info up next tick
	if (pPlayer->pPresenceInfo == NULL)
	{
		pPlayer->pPresenceInfo = (EntityPresenceInfo *)malloc(sizeof(EntityPresenceInfo));
		pPlayer->pPresenceInfo->perHidden = NULL;
		pPlayer->pPresenceInfo->erOwner = pPlayerEnt->myRef;
		pPlayer->pPresenceInfo->iRefCount = 1;

		return true;
	}

	return false;
}

void gslEntityPresence_OnEncounterActivate(int iPartitionIdx, GameEncounter * pEncounter)
{
	// we do not clear presence state here, but we can modify it for this encounter

	bool bCreated = false;

	if (pEncounter->pWorldEncounter->properties->pPresenceCond)
	{
		EntityIterator* playeriter = entGetIteratorSingleType(iPartitionIdx, 0, 0, GLOBALTYPE_ENTITYPLAYER);
		Entity* pPlayerEnt;
		while(pPlayerEnt = EntityIteratorGetNext(playeriter))
		{
			if (!team_IsMember(pPlayerEnt) || team_IsTeamLeader(pPlayerEnt))
			{
				int iVal = encPlayer_Evaluate(pPlayerEnt,pEncounter->pWorldEncounter->properties->pPresenceCond,pEncounter->pWorldEncounter->common_data.closest_scope);

				if (iVal == 0)
				{
					GameEncounterPartitionState *pState = encounter_GetPartitionState(entGetPartitionIdx(pPlayerEnt), pEncounter);
					int i;
					for(i=eaSize(&pState->eaEntities)-1; i>=0; --i)
					{
						Entity *pEnt = pState->eaEntities[i];

						if (pEnt->pCritter && pEnt->pCritter->encounterData.pGameEncounter)
						{
							bCreated = MakePresenceInfo(pPlayerEnt);
							ea32PushUnique(&pPlayerEnt->pPlayer->pPresenceInfo->perHidden,pEnt->myRef);
						}
					}
				}
			}
		}

		EntityIteratorRelease(playeriter);
	}

	if (bCreated)
	{
		// notify players of any new presence infos that were created for their team
		UpdateTeamPresenceInfos();
	}
}

void gslEntityPresenceTick(void)
{
	if (gbUpdatePresence)
	{
		bool bCreated = false;

		ClearPresenceInfoForAllPlayers();

		{
			EntityIterator *iter = entGetIteratorSingleTypeAllPartitions(0, 0, GLOBALTYPE_ENTITYCRITTER);
			Entity *pent;

			while(pent = EntityIteratorGetNext(iter))
			{
				if (pent->pCritter && pent->pCritter->encounterData.pGameEncounter)
				{
					GameEncounter * pGameEncounter = pent->pCritter->encounterData.pGameEncounter;
					if (pGameEncounter->pWorldEncounter->properties->pPresenceCond)
					{
						// evaluate the presence of this entity for every team on the partition
						int iPartitionIdx = entGetPartitionIdx(pent);
						EntityIterator* playeriter = entGetIteratorSingleType(iPartitionIdx, 0, 0, GLOBALTYPE_ENTITYPLAYER);
						Entity* pPlayerEnt;

						while(pPlayerEnt = EntityIteratorGetNext(playeriter))
						{
							if (!team_IsMember(pPlayerEnt) || team_IsTeamLeader(pPlayerEnt))
							{
								int iVal;

								iVal = encPlayer_Evaluate(pPlayerEnt,pGameEncounter->pWorldEncounter->properties->pPresenceCond,pGameEncounter->pWorldEncounter->common_data.closest_scope);

								if (iVal == 0)
								{
									// this guy is hidden		
									bCreated = MakePresenceInfo(pPlayerEnt);
									ea32PushUnique(&pPlayerEnt->pPlayer->pPresenceInfo->perHidden,pent->myRef);
								}
							}
						}

						EntityIteratorRelease(playeriter);
					}
				}
			}

			EntityIteratorRelease(iter);
		}

		if (bCreated)
		{
			// notify players of any new presence infos that were created for their team
			UpdateTeamPresenceInfos();
		}

		gbUpdatePresence = false;
	}
}

void gslRequestEntityPresenceUpdate()
{
	gbUpdatePresence = true;
}

AUTO_EXPR_FUNC(player, mission, gameutil) ACMD_NAME(UpdatePresence);
int exprUpdatePresence(ExprContext* context)
{
	gbUpdatePresence = true;

	return 1;
}