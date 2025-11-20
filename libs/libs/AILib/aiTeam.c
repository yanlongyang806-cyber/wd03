#include "aiTeam.h"

#include "aiAggro.h"
#include "aiAnimList.h"
#include "aiCombatJob.h"
#include "aiCombatRoles.h"
#include "aiConfig.h"
#include "aiDebug.h"
#include "aiExtern.h"
#include "aiFCExprFunc.h"
#include "aiFormation.h"
#include "aiGroupCombat.h"
#include "aiLib.h"
#include "aiMovement.h"
#include "aiJobs.h"
#include "aiPowers.h"
#include "aiStruct.h"

#include "AutoTransDefs.h"
#include "Character.h"
#include "Character_target.h"
#include "CommandQueue.h"
#include "entCritter.h"
#include "EntityLib.h"
#include "EntityMovementManager.h"
#include "EntityMovementDefault.h"
#include "Estring.h"
#include "gslEncounter.h"
#include "gslOldEncounter.h"
#include "gslMapState.h"
#include "gslMapVariable.h"
#include "MemoryPool.h"
#include "rand.h"
#include "RegionRules.h"
#include "StateMachine.h"
#include "StringCache.h"
#include "team.h"
#include "WorldGrid.h"
#include "gslEncounterLog.h"

#include "encounter_common.h"
#include "oldencounter_common.h"
#include "CombatConfig.h"

int aiTeamReinforceParanoid = 0;
AUTO_CMD_INT(aiTeamReinforceParanoid, aiTeamReinforceParanoid);

#define TEAM_DEBUG_PRINTF(str, ...)

static void aiTeamUpdateAssignments(SA_PARAM_NN_VALID AITeam *team);
static void aiTeamMember_AmbientStateEnter(Entity* e, AITeam* team);

#define VALIDATE_FORMATIONS 0

MP_DEFINE(AITeam);
MP_DEFINE(AITeamMemberAssignment);
MP_DEFINE(AITeamMember);

static void aiTeamReportAssignment(AITeam *team, AITeamMember *member, F32 pointPercent, 
								   AITeamAssignmentType assignmentType, F32 weight);

AITeamMemberAssignment* aiTeamMemberAssignmentCreate(AITeamMember *pTarget, AITeamAssignmentType type)
{
	AITeamMemberAssignment *pAssign;
	MP_CREATE(AITeamMemberAssignment, 16);
	pAssign = MP_ALLOC(AITeamMemberAssignment);

	pAssign->target = pTarget;
	pAssign->validAssignment = true;
	pAssign->type = type;

	return pAssign;
}

void aiTeamMemberAssignmentDestroy(AITeamMemberAssignment *p)
{
	MP_FREE(AITeamMemberAssignment, p);
}

AITeamMember* AITeamMember_Create()
{
	MP_CREATE(AITeamMember, 32);
	return MP_ALLOC(AITeamMember);
}

void AITeamMember_Free(AITeamMember *pMember)
{
	if (pMember->pCombatRole)
		aiTeamMemberCombatRole_Free(pMember->pCombatRole);

	MP_FREE(AITeamMember, pMember);
}


static void aiTeamCalcRoamingLeashPointFromTargets(AITeam *team);
static void aiTeamClearReinforcements(AITeam *team, int resetReinforced);

AITeam* aiTeamCreate(int partitionIdx, Entity* teamOwner, int combat)
{
	AITeam* team;
	MP_CREATE(AITeam, 4);
	team = MP_ALLOC(AITeam);
	team->statusHashTable = stashTableCreateInt(4);
	team->teamOwner = teamOwner;
	team->minSpawnAggroTime = INT_MAX;
	team->combatTeam = !!combat;
	team->partitionIdx = partitionIdx;
	// Setting it to time-1 means the first AITick will tick it.  
	// But it needs to be near current time to have a valid elapsed time on the first tick
	team->time.lastTick = ABS_TIME_PARTITION(partitionIdx) > 0 ? ABS_TIME_PARTITION(partitionIdx) - 1 : 0;

	if (teamOwner)
	{
		team->teamLeaderRef = teamOwner->myRef;
	}
	else
	{
		team->collId = mmCollisionSetGetNextID();
	}

	return team;
}

static void aiTeamStatusEntryDestroy(AITeamStatusEntry* status);

static AITeamStatusEntry* aiTeamAddLegalTargetInternal(SA_PARAM_NN_VALID AITeam* team, SA_PARAM_NN_VALID Entity* target);

void aiTeamDestroy(AITeam* team)
{
	if(team->dontDestroy)
		return;

	TEAM_DEBUG_PRINTF("Destroying team %p\n", team);	

	if(team->teamActions)
		stashTableDestroy(team->teamActions);

	REMOVE_HANDLE(team->aigcSettings);

	aiTeamClearReinforcements(team, 0);

	eaDestroyEx(&team->jobs, aiJobDestroy);
	eaDestroyEx(&team->statusTable, aiTeamStatusEntryDestroy);
	eaDestroyEx(&team->healAssignments, aiTeamMemberAssignmentDestroy);
	stashTableDestroy(team->statusHashTable);
	eaDestroy(&team->activeCombatants);
	eaDestroy(&team->members);
	SAFE_FREE(team->offsetPatrolName);
	
	aiCombatRole_TeamReleaseCombatRolesDef(team);
	
	if (team->pTeamFormation)
	{
		aiFormation_Destroy(&team->pTeamFormation);
	}

	MP_FREE(AITeam, team);
}

// transfers all the members of pNewMember's aiTeam to pTeam
static void aiTeamMerge(int iPartitionIdx, Team * pTeam,Entity * pNewMember)
{
	Entity * pLeader = entFromContainerID(iPartitionIdx,GLOBALTYPE_ENTITYPLAYER, pTeam->pLeader->iEntID);

#if VALIDATE_FORMATIONS
	if (pNewMember->aibase->team)
	{
		int i;
		AITeam * team = pNewMember->aibase->team;

		for(i=eaSize(&team->members)-1; i>=0; i--)
		{
			Entity * pEnt = team->members[i]->memberBE;
			if (pEnt->aibase->pFormationData && pEnt->aibase->pFormationData->pFormation->erLeader == pEnt->myRef)
			{
				devassert(team == pEnt->aibase->pFormationData->pFormation->pTeam);
			}
		}
	}

	if (pLeader->aibase->team)
	{
		int i;
		AITeam * team = pLeader->aibase->team;

		for(i=eaSize(&team->members)-1; i>=0; i--)
		{
			Entity * pEnt = team->members[i]->memberBE;
			if (pEnt->aibase->pFormationData && pEnt->aibase->pFormationData->pFormation->erLeader == pEnt->myRef)
			{
				devassert(team == pEnt->aibase->pFormationData->pFormation->pTeam);
			}
		}
	}
#endif

	// merge the aiTeams (assume players are not using combatTeam.  right?)
	if (pLeader != pNewMember)
	{
		if (pNewMember->aibase->team == NULL)
		{
			// nothing to transfer
			return;
		}

		if (pNewMember->aibase->team->pTeamFormation)
		{
			// Transferring ownership of this formation away from this team, which is about to be destroyed.
			// Ownership will implicitly belong to the player
			devassert(pLeader->aibase->team);
			pNewMember->aibase->team->pTeamFormation->pTeam = pLeader->aibase->team;

			// This AI team will be destroyed, but its formation should remain unmolested
			pNewMember->aibase->team->pTeamFormation = NULL;
		}

		if (pLeader->aibase->team == NULL)
		{
			aiTeamAdd(pNewMember->aibase->team,pLeader);

			// just hand the team over
			pLeader->aibase->team->teamOwner = pLeader;
			pLeader->aibase->team->teamLeaderRef = pLeader->myRef;
		}
		else
		{
			AITeam * pOldTeam = pNewMember->aibase->team;
			devassert(pOldTeam != pLeader->aibase->team);
			FOR_EACH_IN_EARRAY(pOldTeam->members, AITeamMember, pMember)
				aiTeamAdd(pLeader->aibase->team,pMember->memberBE);
			FOR_EACH_END
		}
	}

	// validate team members
#if VALIDATE_FORMATIONS
	{
		int i;
		AITeam * team = pNewMember->aibase->team;

		for(i=eaSize(&team->members)-1; i>=0; i--)
		{
			Entity * pEnt = team->members[i]->memberBE;
			if (pEnt->aibase->pFormationData && pEnt->aibase->pFormationData->pFormation->erLeader == pEnt->myRef)
			{
				devassert(team == pEnt->aibase->pFormationData->pFormation->pTeam);
			}
		}
	}
#endif
}

static void aiTeamFixupLeadership(AITeam * pAITeam)
{
	if (pAITeam)
	{
		if (pAITeam->teamOwner)
		{
			Team * pTeam = team_GetTeam(pAITeam->teamOwner);
			Entity * pTeamLeader = NULL;
			if (pTeam && pTeam->pLeader)
			{
				pTeamLeader = entFromContainerID(entGetPartitionIdx(pAITeam->teamOwner),GLOBALTYPE_ENTITYPLAYER, pTeam->pLeader->iEntID);
			}

			// If the AITeam has the wrong leader, fix it.
			if (pTeamLeader && pAITeam->teamOwner != pTeamLeader && pTeamLeader->aibase->team == pAITeam)
			{
				int i;
				pAITeam->teamOwner = pTeamLeader;
				pAITeam->teamLeaderRef = pTeamLeader->myRef;

				pAITeam->pTeamFormation = pTeamLeader->aibase->pFormationData ? pTeamLeader->aibase->pFormationData->pFormation : NULL;
			
				// migrate all NPCs over to the new leader
				for(i = eaSize(&pAITeam->members)-1; i >= 0; i--)
				{
					Entity * pMemberEnt = pAITeam->members[i]->memberBE;
					if (!pMemberEnt->pPlayer && pMemberEnt->erOwner == 0)
					{
						// this is not a player and not a pet.  We will only put him in a new formation if he had an old one, to maintain behavior,
						// though this is probably already inconsistent.  NPCs will get added to player pet formations when they join the AI team,
						// or none if the player doesn't have a pet out, most likely.
						if (pMemberEnt->aibase->pFormationData)
						{
							aiFormationData_Free(pMemberEnt->aibase->pFormationData);
							pMemberEnt->aibase->pFormationData = NULL;
							aiFormation_TryToAddToFormation(entGetPartitionIdx(pMemberEnt),pAITeam,pMemberEnt);
						}
					}
				}
			}
		}
	}
}

void aiTeamUpdatePlayerMembership(Entity * pEnt)
{
	Team * pTeam = NULL;
	Entity * pMyTeamLeader = NULL;
	Entity * pOtherTeamLeader = NULL;

	devassert(pEnt->pPlayer);

	if (team_IsMember(pEnt))
	{
		pTeam = GET_REF(pEnt->pTeam->hTeam);
		if (pTeam && pTeam->pLeader)
		{
			pMyTeamLeader = entFromContainerID(entGetPartitionIdx(pEnt),GLOBALTYPE_ENTITYPLAYER, pTeam->pLeader->iEntID);
		}
	}

	// See if either the AITeam I am currently on, or the AITeam that belongs to my Team leader has had an ownership change that needs fixing up
	aiTeamFixupLeadership(pEnt->aibase->team);
	if (pMyTeamLeader)
		aiTeamFixupLeadership(pMyTeamLeader->aibase->team);

	// Figure out if there's a better AITeam I should be on, and get on it
	if (pTeam && pMyTeamLeader)
	{
		if ((pMyTeamLeader != pEnt) && (pEnt->aibase->team != pMyTeamLeader->aibase->team))
		{
			if (pEnt->pTeam->eState == TeamState_Member)
			{
				// put all my guys onto the AITeam that matches pTeam
				aiTeamMerge(entGetPartitionIdx(pEnt),pTeam,pEnt);
			}
		}
	}

	// If there was no AITeam I needed to be on and I'm still somewhere I shouldn't be, just split off
	if ((pTeam == NULL && pEnt->aibase->team->teamLeaderRef != pEnt->myRef) || (pMyTeamLeader && pEnt->aibase->team->teamLeaderRef != pMyTeamLeader->myRef))
	{
		// I need to branch off my own AITeam, since I'm on a different Team
		int i;

		AITeam * pCurrentAITeam = pEnt->aibase->team;

		// this call can delete the AITeam
		aiTeamRemove(&pCurrentAITeam, pEnt);

		// create and set up an appropriate team
		aiInitTeam(pEnt,NULL);

		if (pEnt->aibase->pFormationData && pEnt->aibase->pFormationData->pFormation->erLeader == pEnt->myRef)
		{
			// I'm taking a formation with me (cause I have pets, presumably)
			pEnt->aibase->team->pTeamFormation = pEnt->aibase->pFormationData->pFormation;
		}

		if (pCurrentAITeam)
		{
			// the AITeam is still valid, take my pets with me
			for(i = eaSize(&pCurrentAITeam->members)-1; i >= 0; i--)
			{
				Entity * pMemberEnt = pCurrentAITeam->members[i]->memberBE;
				if (pMemberEnt->erOwner == pEnt->myRef)
				{
					devassert(pMemberEnt->aibase->team == pCurrentAITeam);

					aiTeamRemove(&pCurrentAITeam,pMemberEnt);
					aiTeamAdd(pEnt->aibase->team,pMemberEnt);
				}
			}
		}
		
	}

#if VALIDATE_FORMATIONS
	// validate aiteam members
	if (team_IsMember(pEnt))
	{
		// make sure all of my aiTeam members have the same aiTeam as my not-ai Team leader
		pTeam = GET_REF(pEnt->pTeam->hTeam);
		if (pTeam && pTeam->pLeader)
		{
			int i;
			pMyTeamLeader = entFromContainerID(entGetPartitionIdx(pEnt),GLOBALTYPE_ENTITYPLAYER, pTeam->pLeader->iEntID);
			if (pMyTeamLeader)
			{
				for(i = eaSize(&pEnt->aibase->team->members)-1; i >= 0; i--)
				{
					devassert(pEnt->aibase->team->members[i]->memberBE->aibase->team == pMyTeamLeader->aibase->team);
				}
			}
		}
	}
#endif
}

// I wrote this function so we could avoid merging AITeams later, but at the time I would call it, I don't have my Team container [RMARR - 5/25/11]
/*
AITeam * aiTeamFindTeamForPlayer(Entity * pEnt)
{
	devassert(pEnt->pPlayer);

	if (team_IsMember(pEnt))
	{
		Team * pTeam = GET_REF(pEnt->pTeam->hTeam);
		if (pTeam)
		{
			Entity * pLeaderEnt = entFromContainerID(GLOBALTYPE_ENTITYPLAYER, pTeam->pLeader->iEntID);
			if (pLeaderEnt)
			{
				return pLeaderEnt->aibase->team;
			}
		}
	}

	return NULL;
}*/

AITeam* aiTeamGetCombatTeam(Entity* e, AIVarsBase* aib)
{
	if(aiGlobalSettings.enableCombatTeams && aib->combatTeam)
		return aib->combatTeam;

	return aib->team;
}

AITeam* aiTeamGetAmbientTeam(Entity* e, AIVarsBase* aib)
{
	return aib->team;
}

AITeamStatusEntry* aiGetTeamStatus(Entity* e, AIVarsBase* aib, AIStatusTableEntry *status)
{
	if(!status) return NULL;

	return aib->combatTeam ? status->combatStatus : status->ambientStatus;
}

AITeamStatusEntry* aiTeamGetTeamStatus(AITeam *team, AIStatusTableEntry *status)
{
	if(team->combatTeam)
	{
		devassert(status->combatStatus);
		return status->combatStatus;
	}

	return status->ambientStatus;
}

S32 aiTeamGetMemberCount(AITeam *team)
{
	return eaSize(&team->members);
}

AITeamMember* aiTeamGetMember(Entity* e, AIVarsBase* aib, AITeam* team)
{
	if(team->combatTeam)
	{
		devassert(aib->combatMember);
		return aib->combatMember;
	}

	return aib->member;
}

AITeamMember* aiGetTeamMember(Entity* e, AIVarsBase* aib)
{
	if(aiGlobalSettings.enableCombatTeams && aib->combatTeam)
	{
		devassert(aib->combatMember);
		return aib->combatMember;
	}
	
	return aib->member;
}

/*
static void aiTeamUpdateRoamingLeashPoint(AITeam* team)
{
	int i, n = eaSize(&team->members);

	devassert(team->members);

	team->roamingLeashPointValid = true;
	zeroVec3(team->roamingLeashPoint);
	for(i = n-1; i >= 0; i--)
	{
		Entity* memberBE = team->members[i]->memberBE;
		Vec3 pos;
		entGetPos(memberBE, pos);
		addVec3(team->roamingLeashPoint, pos, team->roamingLeashPoint);
	}
	team->roamingLeashPoint[0] /= n;
	team->roamingLeashPoint[1] /= n;
	team->roamingLeashPoint[1] += 10;
	team->roamingLeashPoint[2] /= n;

	aiFindGroundDistance(team->roamingLeashPoint, team->roamingLeashPoint);

	if(vecY(team->roamingLeashPoint) == -FLT_MAX)
		entGetPos(team->members[0]->memberBE, team->roamingLeashPoint);
}
*/

static void aiTeamUpdateSettingsFromMember(AITeam* team, Entity* be)
{
	AIVarsBase *aib = be->aibase;
	AIConfig* config = aiGetConfig(be, be->aibase);

	// make sure these get updated correctly when granting/removing of powers is actually supported
	team->powInfo.hasHealPowers |= be->aibase->powers->hasHealPowers;
	team->powInfo.hasShieldHealPowers |= be->aibase->powers->hasShieldHealPowers;
	team->powInfo.hasBuffPowers |= be->aibase->powers->hasBuffPowers;
	team->powInfo.hasCurePower |= be->aibase->powers->hasCurePower;
	team->powInfo.hasResPowers |= be->aibase->powers->hasResPowers;
	aiPowersGetCureTagUnionList(be, aib, &team->powInfo.eaCureTags);
	//
	
	team->dontAggroInAggroRadius |= config->dontAggroInAggroRadius;
	team->dontReinforce |= !!be->pPlayer;

	team->bHasControlledPets |= config->controlledPet;

	team->config.ignoreMaxProtectRadius &= !config->leashOnNonStaticOverride;
	team->config.socialAggroAlwaysAddTeamToCombatTeam |= config->socialAggroAsTeam;

	team->bLeashOnNonStaticOverride |= config->leashOnNonStaticOverride;

	// apply the AITeamConfig flags 
	aiTeamConfigApply(&team->config, &config->teamParams, be);
	
	// roaming leash is true if any critter has a roaming leash
	if(!team->roamingLeash && config->roamingLeash)
		aiTeamSetRoamingLeash(team, true);

	if (config->combatMaxProtectDistTeamPriority > team->leashDistPriority)
	{
		team->leashDistPriority = config->combatMaxProtectDistTeamPriority;
		team->leashDist = config->combatMaxProtectDist;
	}
	else if (config->combatMaxProtectDistTeamPriority == team->leashDistPriority)
	{
		if (team->leashDist < config->combatMaxProtectDist)
		{
			team->leashDist = config->combatMaxProtectDist;
		}
	}
	

	// if anyone on the team has ignoreSocialAggroPulse, the whole team will get it
	if (config->ignoreSocialAggroPulse)
		team->bIgnoreSocialAggroPulse = true;

	if(be->pChar)
		team->teamLevel = MAX(team->teamLevel, be->pChar->iLevelCombat);

	MIN1(team->minSpawnAggroTime, config->spawnAggroTime);

	if(zmapInfoGetMapType(NULL)==ZMTYPE_STATIC || zmapInfoGetMapType(NULL)==ZMTYPE_SHARED || zmapInfoGetMapType(NULL)==ZMTYPE_UNSPECIFIED)
	{
		FSM *fsm = GET_REF(aib->fsmContext->origFSM);
		team->dontReinforce |= config->dontReinforce;
		team->dontReinforce |= config->movementParams.immobile;
		if(be->pCritter && be->pCritter->bIsInteractable)
			team->dontReinforce |= 1;
		if(fsm && fsm->overrides)
		{
			int i;
			int good = 0;
			// Only critters overriding Combat:Ambient can reinforce
			for(i=eaSize(&fsm->overrides)-1; i>=0; i--)
			{
				FSMOverrideMapping *o = fsm->overrides[i];
				if(!stricmp(o->statePath, "Combat:Ambient"))	
				{
					good = 1;
					break;
				}
			}
			if(!good)
				team->dontReinforce = 1;
		}
		if(aib->combatFSMContext)
			team->dontReinforce = 1;
	}
	else
		team->dontReinforce = 1;
}

static void aiTeamUpdateSettingsFromMap(AITeam *team)
{
	ZoneMapType zmt = zmapInfoGetMapType(NULL);
	if(zmt!=ZMTYPE_STATIC && zmt!=ZMTYPE_SHARED)
		team->minSpawnAggroTime = 0;
	team->config.ignoreLevelDifference = (zmt!=ZMTYPE_STATIC && zmt!=ZMTYPE_SHARED);
	team->config.ignoreMaxProtectRadius = aiGlobalSettings.disableLeashingOnNonStaticMaps && zmt!=ZMTYPE_STATIC;
}

void aiTeamRescanSettings(AITeam* team)
{
	int i;

	if(!team)
		return;

	team->powInfo.hasHealPowers = 0;
	team->powInfo.hasShieldHealPowers = 0;
	team->powInfo.hasBuffPowers = 0;
	team->powInfo.hasResPowers = 0;
	eaiClear(&team->powInfo.eaCureTags);

	team->config.ignoreMaxProtectRadius = 1;
	team->config.skipLeashing = 0;
	aiTeamSetRoamingLeash(team, false);
	team->leashDist = 0;
	team->leashDistPriority = 0;
	team->config.ignoreLevelDifference = 0;
	team->config.addLegalTargetWhenTargeted = 0;
	team->config.addLegalTargetWhenMemberAttacks = 0;
	team->config.socialAggroAlwaysAddTeamToCombatTeam = 0;
	team->teamLevel = 0;
	team->dontReinforce = 0;
	team->bLeashOnNonStaticOverride = false;
	team->minSpawnAggroTime = INT_MAX;
	aiTeamUpdateSettingsFromMap(team);
	for(i = eaSize(&team->members)-1; i >= 0; i--)
		aiTeamUpdateSettingsFromMember(team, team->members[i]->memberBE);
}

int aiTeamNeedsRoamingLeash(AITeam* team)
{
	return team->combatState==AITEAM_COMBAT_STATE_WAITFORFIGHT ||
			team->combatState==AITEAM_COMBAT_STATE_FIGHT ||
			team->combatState==AITEAM_COMBAT_STATE_LEASH;
}

void aiTeamSetRoamingLeash(AITeam* team, int on)
{
	team->roamingLeash = on;
	if(on && aiTeamNeedsRoamingLeash(team))
		aiTeamCalcRoamingLeashPointFromTargets(team);
}

void aiTeamGetLeashPosition(const AITeam *team, Vec3 vOut)
{
	if(team->roamingLeash && team->roamingLeashPointValid)
	{
		copyVec3(team->roamingLeashPoint, vOut);
	}
	else 
	{
		copyVec3(team->spawnPos, vOut);
	}
}


static int aiTeamMembersSort(const AITeamMember** lhs, const AITeamMember** rhs)
{
	Entity* l = (*lhs)->memberBE;
	Entity* r = (*rhs)->memberBE;

	if(!l->pCritter && !r->pCritter)
		return entGetRef(r) - entGetRef(l);
	if(l->pCritter && !r->pCritter)
		return 1;
	if(!l->pCritter && r->pCritter)
		return -1;

	if(l->pCritter->pcRank == r->pCritter->pcRank)
	{
		if(l->pCritter->pcSubRank==r->pCritter->pcSubRank)
			return entGetRef(r) - entGetRef(l);

		return critterSubRankGetOrder(r->pCritter->pcSubRank) - critterSubRankGetOrder(l->pCritter->pcSubRank);
	}

	return critterRankGetOrder(r->pCritter->pcRank) - critterRankGetOrder(l->pCritter->pcRank);
}



AITeamMember* aiTeamAdd(AITeam* team, Entity* e)
{
	AITeamMember* member;
	AIVarsBase* aib = e->aibase;
	AITeam *oldTeam = NULL;
	Entity *owner = NULL;
	if(e->erOwner)
	{
		owner = entFromEntityRef(entGetPartitionIdx(e),e->erOwner);
	}
	
	if (!team->combatTeam)
	{
		oldTeam = aib->team;
	}
	else
	{
		oldTeam = aib->combatTeam;
	}

	if(oldTeam==team)
		return aiTeamGetMember(e, aib, team);
	
	if (oldTeam)
		aiTeamRemove(&oldTeam, e);

	member = AITeamMember_Create();

	devassert(e);

	member->memberBE = e;

	eaPush(&team->members, member);
	if(team->combatTeam)
	{
		e->aibase->combatTeam = team;
		e->aibase->combatMember = member;
	}
	else
	{
		e->aibase->team = team;
		e->aibase->member = member;
	}

	eaQSort(team->members, aiTeamMembersSort);

	aiTeamUpdateSettingsFromMap(team);

	aiTeamUpdateSettingsFromMember(team, e);

	
	FOR_EACH_IN_EARRAY(team->statusTable, AITeamStatusEntry, teamStatus)
	{
		Entity* target = entFromEntityRef(team->partitionIdx, teamStatus->entRef);

		if(!target)
			continue;
		
		aiStatusFind(e, aib, target, true);
	}
	FOR_EACH_END;

	FOR_EACH_IN_EARRAY(aib->statusTable, AIStatusTableEntry, status)
	{
		Entity* statusEnt = entFromEntityRef(team->partitionIdx, status->entRef);
		int legalTarget = false;

		if(!statusEnt)
			continue;

		if(oldTeam)
		{
			AITeamStatusEntry* oldTeamStatus = aiTeamStatusFind(oldTeam, statusEnt, false, false);
			// there is a chance this could be NULL
			if(oldTeamStatus)
			{
				legalTarget = oldTeamStatus->legalTarget;
			}
		}

		status->ambientStatus = aiTeamStatusFind(aib->team, statusEnt, true, legalTarget);
		if(aib->combatTeam)
			status->combatStatus = aiTeamStatusFind(aib->combatTeam, statusEnt, true, legalTarget);
	}
	FOR_EACH_END;

	if(team->calculatedSpawnPos)
	{
		if(owner && team==aiTeamGetAmbientTeam(owner, owner->aibase))
			;
		else
		{
			team->calcOffsetOnDemand = 1;

			if(team->combatState!=AITEAM_COMBAT_STATE_AMBIENT && team->roamingLeash && team->roamingLeashPointValid)
			{
				Vec3 curPos;

				entGetPos(e, curPos);
				subVec3(curPos, team->roamingLeashPoint, aib->spawnOffset);
				aib->spawnOffsetDirtied = 1;
			}
		}
	}

	if(team->combatState==AITEAM_COMBAT_STATE_AMBIENT && !team->bHasControlledPets)
		aiTeamMember_AmbientStateEnter(e, team);

	// skip all the formation stuff for combat teams.  People have their formations from their regular teams and that's that
	if (!team->combatTeam)
	{
		// put me on a formation, if there is one (and I'm not already on one)
		aiFormation_TryToAddToFormation(entGetPartitionIdx(e),team,e);

		if (e->aibase->pFormationData && e->aibase->pFormationData->pFormation->erLeader == e->myRef)
		{
			// This case is supposed to be hit for players that are holding onto formations for their pets
			// (only players do this now, cause only players are allowed to NULL out pTeam in aiTeamRemove, because pets can be dismissed.
			// The other way was probably harmless, but harder to keep track of what was going on.  Maybe we should just do this right at the top,
			// before the aiTeamRemove?)
			devassert(e->aibase->pFormationData->pFormation);
			devassert(e->aibase->pFormationData->pFormation->pTeam == NULL || e->aibase->pFormationData->pFormation->pTeam == team);
			e->aibase->pFormationData->pFormation->pTeam = team;

			if (team->teamLeaderRef == e->myRef && team->pTeamFormation == NULL) // not sure I need this
			{
				// This team didn't have a team formation before, but I'm the team leader, and I have a formation, so this is now it.
				team->pTeamFormation = e->aibase->pFormationData->pFormation;
			}
		}

		if (e->aibase->pFormationData && e->aibase->pFormationData->pFormation->erLeader == e->myRef)
			devassert(e->aibase->pFormationData->pFormation->pTeam == team);
	}

	return member;
}

// the team will get deleted if this is the last member of the team 
void aiTeamRemove(AITeam** ppTeam, Entity* e)
{
	AITeam *team = *ppTeam;
	if(team)
	{
		int i;
		AIVarsBase *aib = e->aibase;
		AITeamMember* member = aiTeamGetMember(e, e->aibase, team);

		// remove all assignments where the member is the target
		// or if it was the assignee, clear it
		for(i = eaSize(&team->healAssignments)-1; i >= 0; i--)
		{
			AITeamMemberAssignment *assignment = team->healAssignments[i];
			if (assignment->target == member)
			{
				aiTeamMemberAssignmentDestroy(assignment);
				eaRemove(&team->healAssignments, i);
			}
			else if (assignment->assignee == member)
			{
				assignment->assignee = NULL;
			}
		}

		if(team->reinforceMember == member)
		{
			aiTeamClearReinforceTarget(e, e->aibase, team, team->reinforceTeam, 1, 0);	
			team->reinforceMember = NULL;
		}

		for(i = eaSize(&team->statusTable)-1; i >= 0; i--)
		{
			AITeamStatusEntry* curStatus = team->statusTable[i];
			eaFindAndRemoveFast(&curStatus->assignedTeamMembers, member);
		}

		for(i = eaSize(&team->members)-1; i >= 0; i--)
		{
			if(team->members[i] == member)
			{
				AITeamMember_Free(team->members[i]);
				eaRemove(&team->members, i);
				break;
			}
		}

		// If everything is working right, then if you are a pet and you have reached this code, you are either being dismissed/destroyed, etc.,
		// or else your owner has changed teams, in which case your formation should have gone with him and the data should reflect that
		if (e->aibase->pFormationData)
		{
			// Do I own a formation still associated with this team?
			if (e->aibase->pFormationData->pFormation->pTeam == team && e->aibase->pFormationData->pFormation->erLeader == e->myRef)
			{
				// I do, so clear that
				e->aibase->pFormationData->pFormation->pTeam = NULL;

				// Was that also the team formation?
				if (team->pTeamFormation && team->pTeamFormation->erLeader == e->myRef)
				{
					// Clear that too
					team->pTeamFormation = NULL;
				}
			}
			else if (team->pTeamFormation == e->aibase->pFormationData->pFormation)
			{
				// destroy my formation info, since my formation is still the formation of the old team, but I don't own it
				aiFormationData_Free(e->aibase->pFormationData);
				e->aibase->pFormationData = NULL;
			}
		}

		if(team->combatTeam)
		{
			e->aibase->combatMember = NULL;
			e->aibase->combatTeam = NULL;

			for(i = eaSize(&aib->statusTable)-1; i>=0; i--)
				aib->statusTable[i]->combatStatus = NULL;
		}
		else
		{
			e->aibase->member = NULL;
			e->aibase->team = NULL;

			for(i = eaSize(&aib->statusTable)-1; i>=0; i--)
				aib->statusTable[i]->ambientStatus = NULL;
		}
		
		if(!eaSize(&team->members))
		{
			// If there's a team formation, it's about to be destroyed, so we really do not want to be holding on to it
			devassert(e->aibase->pFormationData == NULL || team->pTeamFormation != e->aibase->pFormationData->pFormation);
			aiTeamDestroy(team);
			*ppTeam = NULL;
		}
		else if(team->teamOwner == e)
		{
			team->teamOwner = team->members[0]->memberBE;
			if (team->teamOwner)
				team->teamLeaderRef = team->teamOwner->myRef;
		}
	}
}

static AITeamStatusEntry* aiTeamStatusEntryCreate(void)
{
	return calloc(sizeof(AITeamStatusEntry), 1);
}

static void aiTeamStatusEntryDestroy(AITeamStatusEntry* status)
{
	FOR_EACH_IN_EARRAY(status->assignedTeamMembers, AITeamMember, member)
	{
		devassert(member->assignedTarget == status->entRef);
		// No need to call aiTeamSetAssignedTarget helper because the array is destroyed
		member->assignedTarget = 0;  
	}
	FOR_EACH_END

	eaDestroy(&status->assignedTeamMembers);
	free(status);
}

AITeamStatusEntry* aiTeamStatusFind(AITeam* team, Entity* target, int create, int legalTarget)
{
	AITeamStatusEntry* status;
	int i;
	EntityRef targetRef;
	int partitionIdx = entGetPartitionIdx(target);

	if(!target->pChar)
	{
		devassertmsg(!create, "Can't create status entry for non-combat entities");
		return NULL;
	}

	// check if there are any reasons why this shouldn't be a legal target
	if (legalTarget)
	{
		if(target->aibase && target->aibase->untargetable)
			legalTarget = false;
	}

	targetRef = entGetRef(target);
	if(!stashIntFindPointer(team->statusHashTable, targetRef, &status) && create)
	{
		status = aiTeamStatusEntryCreate();
		status->entRef = targetRef;
		status->team = team;
		stashIntAddPointer(team->statusHashTable, targetRef, status, false);
		eaPush(&team->statusTable, status);
		entGetPos(target, status->lastKnownPos);

		for(i = eaSize(&team->members)-1; i >= 0; i--)
		{
			Entity* memberE = team->members[i]->memberBE;
			AIVarsBase* memberAIB = memberE->aibase;
			AIStatusTableEntry* memberStatus = aiStatusFind(memberE, memberAIB, target, true);

			if(legalTarget)
			{
				memberStatus->visible = true;
				memberStatus->time.lastVisible = ABS_TIME_PARTITION(partitionIdx);
			}

			if(memberAIB->team==team)
			{
				memberStatus->ambientStatus = status;
				if(memberAIB->combatTeam)
					memberStatus->combatStatus = aiTeamStatusFind(memberAIB->combatTeam, target, true, false);
			}
			if(memberAIB->combatTeam==team)
			{
				memberStatus->combatStatus = status;
				memberStatus->ambientStatus = aiTeamStatusFind(memberAIB->team, target, true, false);
			}
		}
	}

	if(status)
	{
		if(!status->legalTarget && legalTarget)
		{
			for(i = eaSize(&team->members)-1; i >= 0; i--)
			{
				Entity* memberE = team->members[i]->memberBE;
				AIStatusTableEntry* memberStatus = aiStatusFind(memberE, memberE->aibase, target, true);

				memberStatus->lostTrack = 0;
				memberStatus->visitedLastKnown = 0;

				if(legalTarget)
					AI_DEBUG_PRINT_TAG(memberE, AI_LOG_COMBAT, 2, AICLT_LEGALTARGET, "%s: Added as legal target", ENTDEBUGNAME(target));
			}
		}

		status->legalTarget |= legalTarget;
	}

	return status;
}


// Clears the status table for the team
void aiTeamClearStatusTable(AITeam* team, const char* reason) 
{	
	S32 n, i;

	n = eaSize(&team->statusTable);
	for(i = n - 1; i >= 0; i--)
	{
		AITeamStatusEntry* status = team->statusTable[i];
		Entity *ent = entFromEntityRef(team->partitionIdx, status->entRef);
		if (ent)
			aiTeamStatusRemove(team, ent, reason);
	}

	// also clear out the tracked damage for the team
	for(n = 0; n < AI_NOTIFY_TYPE_TRACKED_COUNT; n++)
	{
		team->trackedDamageTeam[n] = 0.f;
	}
}

void aiTeamStatusRemove(AITeam* team, Entity* target, const char* reason)
{
	AITeamStatusEntry* status;
	EntityRef targetRef = entGetRef(target);
	int i;

	stashIntRemovePointer(team->statusHashTable, targetRef, &status);

	if(status)
	{
		eaFindAndRemoveFast(&team->statusTable, status);
		aiTeamStatusEntryDestroy(status);
	}

	for(i = eaSize(&team->members)-1; i >= 0; i--)
	{
		Entity* memberBE = team->members[i]->memberBE;
		aiStatusRemove(memberBE, memberBE->aibase, target, reason);

		// No need to call aiTeamSetAssignedTarget helper because the array was destroyed above
		if(team->members[i]->assignedTarget == targetRef)
			team->members[i]->assignedTarget = 0;
	}
}

void aiTeamAddJobs(AITeam* team, AIJobDesc** jobDescs, const char *filename)
{
	int i, n = eaSize(&jobDescs);

	for(i = 0; i < n; i++)
	{
		AIJobDesc* desc = jobDescs[i];
		AIJob* job;
		int j, numSubJobs = eaSize(&desc->subJobDescs);

		job = aiJobAdd(&team->jobs, desc, team->partitionIdx);

		job->filename = filename;

		for(j = 0; j < numSubJobs; j++)
		{
			AIJob *subjob = aiJobAdd(&job->subJobs, desc->subJobDescs[j], team->partitionIdx);

			subjob->filename = filename;
		}
	}
}

void aiTeamClearJobs(AITeam* team, AIJobDesc** jobDescs)
{
	if(jobDescs)
	{
		int i;

		for(i=eaSize(&team->jobs)-1; i>=0; i--)
		{
			int j;

			for(j=eaSize(&jobDescs)-1; j>=0; j--)
			{
				if(team->jobs[i]->desc==jobDescs[j])
				{
					aiJobDestroy(team->jobs[i]);
					eaRemoveFast(&team->jobs, i);
					break;
				}
			}
		}

		if(!eaSize(&team->jobs))
			eaDestroy(&team->jobs);
	}	
	else
		eaDestroyEx(&team->jobs, aiJobDestroy);
}

int aiTeamActionGetCount(AITeam* team, const char* actionDesc)
{
	int count;

	if(!team->teamActions)
		return 0;

	if(stashFindInt(team->teamActions, actionDesc, &count))
		return count;
	else
		return 0;
}

void aiTeamActionIncrementCount(AITeam* team, const char* actionDesc)
{
	U32 count = 0;
	const char* actionAllocStr = allocAddString(actionDesc);

	if(!team->teamActions)
		team->teamActions = stashTableCreateWithStringKeys(1, StashDefault);

	stashFindInt(team->teamActions, actionAllocStr, &count);
	count++;

	stashAddInt(team->teamActions, actionAllocStr, count, true);
}

void aiTeamActionResetCount(AITeam* team, const char* actionDesc)
{
	const char* actionAllocStr;

	if(!team->teamActions)
		return;

	actionAllocStr = allocAddString(actionDesc);

	if(stashFindElement(team->teamActions, actionAllocStr, NULL))
		stashRemoveInt(team->teamActions, actionAllocStr, NULL);
}

void aiTeamDistributeJobs(AITeam* team)
{
	int i, j, numJobs = eaSize(&team->jobs), numMembers = eaSize(&team->members);

	for(i = 0; i < numJobs; i++)
	{
		F32 maxScore = 0;
		F32 score;
		AIJob* job = team->jobs[i];
		Entity* maxScoreE = NULL;
		Entity* assignedEnt = entFromEntityRef(team->partitionIdx, job->assignedBE);
		int allowed = true;
		AIJobDesc* desc = job->desc;

		if(assignedEnt && aiIsEntAlive(assignedEnt))
		{
			AIConfig *assignedConfig = aiGetConfig(assignedEnt, assignedEnt->aibase);

			if(assignedConfig->dontAllowJobs)
				allowed = 0;
			else if(desc->jobRequires)
			{
				MultiVal answer;
				exprEvaluate(desc->jobRequires, assignedEnt->aibase->exprContext, &answer);

				if(answer.type==MULTI_INT)
					allowed = !!answer.intval;
				else
				{
					ErrorFilenamef(job->filename, "Non-int type returned in Job Requires expr for job: %s", job->desc->jobName);
					allowed = 0;
				}
			}

			if(allowed)
				continue;
			else	// Force unassign, otherwise the critter will keep the job
				aiJobUnassign(assignedEnt, job);
		}

		// if assignedEnt isn't alive anymore, the job will get properly unassigned in
		// aiJobAssign() now

		for(j = 0; j < numMembers; j++)
		{
			Entity* e = team->members[j]->memberBE;
			AIVarsBase* aib = e->aibase;
			AIConfig* config = aiGetConfig(e, aib);

			if(e->pPlayer)
				continue;

			if(!aiIsEntAlive(e))
				continue;

			if(config->dontAllowJobs)
				continue;

			if(!aib->job || desc->priority > aib->job->desc->priority)
			{
				if(desc->jobRequires)
				{
					MultiVal answer;
					exprEvaluate(desc->jobRequires, aib->exprContext, &answer);

					if(answer.type==MULTI_INT)
						allowed = !!answer.intval;
					else
					{
						ErrorFilenamef(job->filename, "Non-int type returned in Job Requires expr for job: %s", job->desc->jobName);
						allowed = 0;
					}
				}

				if(allowed)
				{
					// consider this job
					score = -FLT_MAX;
					if(desc->jobRating)
					{
						MultiVal answer;
						exprEvaluate(desc->jobRating, aib->exprContext, &answer);

						if(answer.type==MULTI_FLOAT)
							score = answer.floatval;
						else if(answer.type==MULTI_INT)
							score = answer.intval;
						else
						{
							ErrorFilenamef(job->filename, "Non-float, non-int type returned in Job Rating expr for job: %s", job->desc->jobName);
							allowed = 0;
						}
					}
					else
					{
						if(aib->job)
							score = job->desc->priority - aib->job->desc->priority;
						else
							score = job->desc->priority;
					}

					if(score > maxScore)
					{
						maxScore = score;
						maxScoreE = e;
					}
				}
			}
		}

		if(maxScoreE)
			aiJobAssign(maxScoreE, team->jobs[i]);
	}

	for(i = 0; i < numMembers; i++)
	{
		// go through all sub jobs and assign based on job owners prox ents list
		;
		// if critter A has critter B in his prox ents list and both A and B have a
		// sub job for eachother, allow them to link up? needs job tags?
		// is there a cleaner way to allow things to line up their behavior across teams?
	}
}

void aiTeamCalculateSpawnPos(AITeam* team)
{
	Entity* leader = team->members[0]->memberBE;
	int nummembers = eaSize(&team->members);
	int i;

	team->calculatedSpawnPos = 1;

	// if the leader doesn't have a pCritter, this is a player who only has a team
	// so his pets can do ai stuff normally
	if(leader->pCritter)
	{
		for(i = 0; i < nummembers; i++)
		{
			Entity* memberBE = team->members[i]->memberBE;
			Vec3 vPos;
			GameEncounter *pEncounter = memberBE->pCritter->encounterData.pGameEncounter;
			OldEncounter* pOldEncounter = memberBE->pCritter->encounterData.parentEncounter;

			if (pEncounter)
			{
				encounter_GetActorPosition(pEncounter, memberBE->pCritter->encounterData.iActorIndex, vPos, NULL);
			}
			else if(pOldEncounter && gConf.bAllowOldEncounterData)
			{
				Quat actorQuat;
				oldencounter_GetEncounterActorPosition(pOldEncounter, memberBE->pCritter->encounterData.sourceActor, vPos, actorQuat);
			}
			else
			{
				entGetPos(memberBE, vPos);
			}

			addVec3(team->spawnPos, vPos, team->spawnPos);
			copyVec3(vPos, memberBE->aibase->spawnPos);
			entGetRot(memberBE, memberBE->aibase->spawnRot);
			copyVec3(vPos, memberBE->aibase->ambientPosition);


			memberBE->aibase->determinedSpawnPoint = 1;
		}

		team->spawnPos[0] /= nummembers;
		team->spawnPos[1] /= nummembers;
		team->spawnPos[2] /= nummembers;
	}
	else
		team->calcOffsetOnDemand = 1;
}

static void aiTeamProcessTeammates(AITeam* team)
{
	int i;
	F32 hpPercentThreshold;
	F32 shieldPercentThreshold;
	int inCombat;
	
	// these thresholds might be best on configs

	for(i = eaSize(&team->members) - 1; i >= 0; i--)
	{
		AITeamMember* member = team->members[i];
		Entity* memberBE = member->memberBE;
		AIConfig* config = aiGetConfig(memberBE, memberBE->aibase);

		F32 health, maxHealth;
		int validHealTarget;
		int validResTarget;

		if(!memberBE->pChar || memberBE->aibase->isSummonedAndExpires)
			continue;

		if(team->combatState == AITEAM_COMBAT_STATE_FIGHT)
		{
			hpPercentThreshold = config->inCombatHPHealThreshold;
			shieldPercentThreshold = config->inCombatShieldHealThreshold;
			inCombat = true;
		}
		else
		{
			hpPercentThreshold = config->ooCombatHPHealThreshold;
			shieldPercentThreshold = config->ooCombatShieldHealThreshold;
			inCombat = false;
		}

		aiExternGetHealth(memberBE, &health, &maxHealth);
		
		member->healthPct = maxHealth ? health / maxHealth : 1.f;

		if(health <= 0)
		{
			validHealTarget = false;
			validResTarget = true;
		}
		else
		{
			validHealTarget = true;
			validResTarget = false;
		}

		if(validHealTarget)
		{
			if(team->powInfo.hasShieldHealPowers)
			{
				F32 shield, maxShield;
				aiExternGetShields(memberBE, &shield, &maxShield);
				
				member->shieldPct = maxShield ? shield / maxShield : 1.f;

				if(member->shieldPct < shieldPercentThreshold)
					aiTeamReportAssignment(team, member, member->shieldPct, AITEAM_ASSIGNMENT_TYPE_SHIELD_HEAL, team->config.shieldHealWeight);
			}
			
			if(team->powInfo.hasHealPowers)
			{
				if(member->healthPct < hpPercentThreshold)
					aiTeamReportAssignment(team, member, member->healthPct, AITEAM_ASSIGNMENT_TYPE_HEAL, 1.f);
			}
			
		}
		else if(validResTarget)
		{
			// && ABS_TIME_SINCE_PARTITION(partitionIdx, member->timeLastResed) > SEC_TO_ABS_TIME(3)
			if(team->powInfo.hasResPowers && (!inCombat || (inCombat && !team->config.dontDoInCombatRezzing)))
			{
				aiTeamReportAssignment(team, member, 0.f, AITEAM_ASSIGNMENT_TYPE_RESSURECT, team->config.ressurectWeight);
			}
		}

	}

	

	aiTeamUpdateAssignments(team);
}

static int aiStatusEntSortActionTime(AITeam *team, const AITeamStatusEntry** lhs, const AITeamStatusEntry** rhs)
{
	const AITeamStatusEntry *l = (*lhs), *r = (*rhs);
	Entity *lEnt = entFromEntityRef(team->partitionIdx, l->entRef);
	Entity *rEnt = entFromEntityRef(team->partitionIdx, r->entRef);
	F32 threatScaleDiff;

	if(!lEnt && !rEnt)
		return (intptr_t)(lhs-rhs);
	if(!lEnt)
		return -1;
	if(!rEnt)
		return 1;

	if(l->timeLastAggressiveAction>team->time.lastStartedCombat && r->timeLastAggressiveAction>team->time.lastStartedCombat)
	{
		int diff = (r->timeLastAggressiveAction - l->timeLastAggressiveAction);
		return diff ? diff : entGetRef(lEnt)-entGetRef(rEnt);
	}

	if(l->timeLastAggressiveAction>team->time.lastStartedCombat)
		return 1;
	if(r->timeLastAggressiveAction>team->time.lastStartedCombat)
		return -1;

	threatScaleDiff = SAFE_MEMBER3(lEnt, pChar, pattrBasic, fAIThreatScale) - SAFE_MEMBER3(rEnt, pChar, pattrBasic, fAIThreatScale);
	return threatScaleDiff ? SIGN(threatScaleDiff) : entGetRef(lEnt)-entGetRef(rEnt);
}

// TODO: This distance needs to be exposed to the aiconfig or region rules
#define ASSIGNMENT_ATTACK_TARGET_DIST_THRESHOLD	100.f

void aiTeamSetAssignedTarget(AITeam *team, AITeamMember *member, AITeamStatusEntry *status, U32 entRef)
{
	if(!status)
	{
		Entity *target = entFromEntityRef(team->partitionIdx, entRef);

		if(target)
			status = aiTeamStatusFind(team, target, false, false);
	}

	// Handle already having an assigned target
	if(member->assignedTarget!=0)
	{
		Entity *target = entFromEntityRef(team->partitionIdx, member->assignedTarget);
		AITeamStatusEntry *curStatus = NULL;

		if(target)
			curStatus = aiTeamStatusFind(team, target, false, false);

		if(curStatus==status)
			return;

		if(curStatus)
		{
			int res = eaFindAndRemoveFast(&curStatus->assignedTeamMembers, member);
			member->assignedTarget = 0;

			assert(res!=-1);
		}
	}
	
	if(!status)
		return;

	member->assignedTarget = status->entRef;
	eaPush(&status->assignedTeamMembers, member);
}

void aiTeamAssignAttackTargets(AITeam* team)
{
	int i;
	int emptyTargets = 0;

	if(!eaSize(&team->statusTable))
		return;

	for(i = eaSize(&team->statusTable)-1; i >= 0; i--)
	{
		AITeamStatusEntry* teamStatus = team->statusTable[i];
		if(teamStatus->legalTarget && !eaSize(&teamStatus->assignedTeamMembers))
			emptyTargets++;
	}

	if (!team->config.teamForceAttackTargetOnAggro)
	{
		for(i = eaSize(&team->members)-1; emptyTargets && i >= 0; i--)
		{
			int j;

			for(j = eaSize(&team->statusTable)-1; emptyTargets && j >= 0; j--)
			{
				AITeamStatusEntry* teamStatus = team->statusTable[j];
				while(emptyTargets && teamStatus->legalTarget && eaSize(&teamStatus->assignedTeamMembers) > i)
				{
					AITeamMember* member = eaTail(&teamStatus->assignedTeamMembers);
					aiTeamSetAssignedTarget(team, member, teamStatus, 0);
					
					assert(eaFind(&team->members, member)!=-1);
					assert(!mpReclaimed(member));
					emptyTargets--;
				}
			}
		}
	}
	
	
	eaQSort_s(team->statusTable, aiStatusEntSortActionTime, team);
	for(i = 0; i < eaSize(&team->members); i++)
	{
		AITeamMember* member = team->members[i];
		Entity *pAssignedTarget = NULL;
		if (entIsPlayer(member->memberBE))
			continue;

		if(!member->assignedTarget ||
			(!team->config.teamForceAttackTargetOnAggro && // if teamForceAttackTargetOnAggro is on, then don't reassign targets if we've already given one
				(!(pAssignedTarget = entFromEntityRef(team->partitionIdx, member->assignedTarget)) ||
			     !aiTeamStatusFind(team, pAssignedTarget, false, false)) ))
		{
			int assignedCount;
			int assigned = false;


			for(assignedCount = 1; !assigned && assignedCount <= eaSize(&team->members); assignedCount++)
			{
				int j;

				for(j = 0; j < eaSize(&team->statusTable); j++)
				{
					AITeamStatusEntry* teamStatus = team->statusTable[j];
					
					if(teamStatus->legalTarget && eaSize(&teamStatus->assignedTeamMembers) < assignedCount)
					{
						AIStatusTableEntry* memberTargetStatus;
						Entity *pTarget = entFromEntityRef(team->partitionIdx, teamStatus->entRef);
						if (!pTarget) 
							continue;
						memberTargetStatus = aiStatusFind(member->memberBE, member->memberBE->aibase, 
															pTarget, false);
						
						if (memberTargetStatus && 
							memberTargetStatus->distanceFromMe < ASSIGNMENT_ATTACK_TARGET_DIST_THRESHOLD)
						{
							assigned = true;
							aiTeamSetAssignedTarget(team, member, teamStatus, 0);

							if (team->config.teamForceAttackTargetOnAggro)
							{
								aiSetAttackTarget(member->memberBE, member->memberBE->aibase, 
													pTarget, memberTargetStatus, true);
							}
							break;
						}
					}
				}
			}
		}
	}
}

#include "aiStruct_h_ast.h"

static void aiTeamLeashStateEnter(AITeam *team)
{
	int i;
	int partitionIdx = team->partitionIdx;
	team->time.lastStartedLeashing = ABS_TIME_PARTITION(partitionIdx);
	for(i = eaSize(&team->members)-1; i >= 0; i--)
	{
		Entity* memberE = team->members[i]->memberBE;
		AIVarsBase* memberAIB = memberE->aibase;
		AIConfig* config = aiGetConfig(memberE, memberAIB);
		Vec3 pos;
		RegionRules *rules;
		Entity* owner;
		int hasNoEncounter = false;

		memberAIB->time.lastNearSpawnPos = 0;
		memberAIB->leashState = AI_LEASH_STATE_DONE;
		entGetPos(memberE, pos);
		if(memberE->pChar)
		{
			aiCombatReset(memberE, memberAIB, true, true, 0);

			if(aiGlobalSettings.untargetableOnLeash)
				entSetCodeFlagBits(memberE, ENTITYFLAG_UNTARGETABLE);
			if(aiGlobalSettings.unselectableOnLeash)
				entSetCodeFlagBits(memberE, ENTITYFLAG_UNSELECTABLE);

			//memberE->pChar->bIsLeashing = 1;
		}

		owner = entGetOwner(memberE);
		hasNoEncounter = !memberE->pCritter ||
						!memberE->pCritter->encounterData.parentEncounter && 
						!memberE->pCritter->encounterData.pGameEncounter;
		if(!config->ignoreLeashDespawn && 
				(
					memberE->pCritter && (!owner || !entIsPlayer(owner)) && hasNoEncounter ||
					config->despawnOnLeash
				)
			)
		{
			entDie(memberE, 5, 0, 0, NULL);
			continue;
		}

		aiSetAttackTarget(memberE, memberAIB, NULL, NULL, true);

		if(memberAIB->inCombat)
		{
			aiClearAllQueuedPowers(memberE, memberAIB);

			aiSayExternMessageVar(memberE, memberE->aibase->exprContext, "Encounter", "Leash_Start", 4);
			
			rules = RegionRulesFromVec3(pos);

			if(!aiIsEntAlive(memberE))
				continue;

			rules = RegionRulesFromVec3(pos);

			if(rules && rules->leashSettings)
			{
				char buf[50];
				if(aiGetSpawnPosDist(memberE, memberAIB)>=rules->leashSettings->fTeleportDist)
				{
					AIAnimList *anim = RefSystem_ReferentFromString("AIAnimList", rules->leashSettings->animListStart);

					if(anim)
					{
						aiAnimListSet(memberE, anim, &memberAIB->leashAnimQueue);
						memberAIB->leashState = AI_LEASH_STATE_START;
					}
					aiMovementSetTargetPosition(memberE, memberAIB, NULL, NULL, 0);
				}
				else
				{
					if(rules->leashSettings->fSpeed)
					{
						sprintf(buf, "%f", rules->leashSettings->fSpeed);
						memberAIB->leashSpeedConfigHandle = 
							aiConfigModAddFromString(memberE, memberAIB, "overrideMovementSpeed", buf, NULL);
					}

					if(rules->leashSettings->fTurnRate!=-1)
					{
						sprintf(buf, "%f", rules->leashSettings->fTurnRate);
						memberAIB->leashTurnRateConfigHandle = 
							aiConfigModAddFromString(memberE, memberAIB, "overrideMovementTurnRate", buf, NULL);
					}

					if(rules->leashSettings->fTraction)
					{
						sprintf(buf, "%f", rules->leashSettings->fTraction);
						memberAIB->leashTractionConfigHandle = 
							aiConfigModAddFromString(memberE, memberAIB, "overrideMovementTraction", buf, NULL);
					}

					if(rules->leashSettings->fFriction)
					{
						sprintf(buf, "%f", rules->leashSettings->fFriction);
						memberAIB->leashFrictionConfigHandle = 
							aiConfigModAddFromString(memberE, memberAIB, "overrideMovementFriction", buf, NULL);
					}

					aiMovementGoToSpawnPos(memberE, memberAIB, AI_MOVEMENT_TARGET_CRITICAL);
				}
			}
			else
				aiMovementGoToSpawnPos(memberE, memberAIB, AI_MOVEMENT_TARGET_CRITICAL);
		}
	}

	for(i = eaSize(&team->statusTable)-1; i >= 0; i--)
		team->statusTable[i]->legalTarget = false;
}

static void aiTeamLeashStateExit(AITeam *team)
{
	int i;

	for(i = eaSize(&team->members)-1; i >= 0; i--)
	{
		Entity* memberE = team->members[i]->memberBE;
		AIVarsBase* memberAIB = memberE->aibase;

		//if(memberE->pChar)
		//	memberE->pChar->bIsLeashing = 0;
		entClearCodeFlagBits(memberE, ENTITYFLAG_UNTARGETABLE);
		entClearCodeFlagBits(memberE, ENTITYFLAG_UNSELECTABLE);

		if(memberAIB->leashSpeedConfigHandle)
			aiConfigModRemove(memberE, memberAIB, memberAIB->leashSpeedConfigHandle);
		if(memberAIB->leashTurnRateConfigHandle)
			aiConfigModRemove(memberE, memberAIB, memberAIB->leashTurnRateConfigHandle);
		if(memberAIB->leashFrictionConfigHandle)
			aiConfigModRemove(memberE, memberAIB, memberAIB->leashFrictionConfigHandle);
		if(memberAIB->leashTractionConfigHandle)
			aiConfigModRemove(memberE, memberAIB, memberAIB->leashTractionConfigHandle);

		memberAIB->leashSpeedConfigHandle = 0;
		memberAIB->leashTurnRateConfigHandle = 0;
		memberAIB->leashFrictionConfigHandle = 0;
		memberAIB->leashTractionConfigHandle = 0;

		aiSayExternMessageVar(memberE, memberAIB->exprContext, "Encounter", "Leash_Finish", 4);
	}

	zeroVec3(team->roamingLeashPoint);
	team->roamingLeashPointValid = false;
	team->combatSetup = false;
}

static void aiTeamCalcRoamingLeashPointFromTargets(AITeam *team)
{
	int i;
	Vec3 center_pos = {0};
	int count = 0;

	if(team->roamingLeashPointValid)
		return;

	for(i=eaSize(&team->statusTable)-1; i>=0; i--)
	{
		int j;
		Entity *closestMember = NULL;
		F32 closestDist = FLT_MAX;
		AITeamStatusEntry *status = team->statusTable[i];
		Entity *statusE;
		Vec3 pos;

		if(!status->legalTarget)
			continue;

		statusE = entFromEntityRef(team->partitionIdx, status->entRef);
		if(!statusE)
			continue;

		for(j=eaSize(&team->members)-1; j>=0; j--)
		{
			AITeamMember *member = team->members[j];
			F32 dist;

			entGetPos(member->memberBE, pos);
			dist = entGetDistance(member->memberBE, NULL, statusE, NULL, NULL);

			if(dist<closestDist)
			{
				closestMember = member->memberBE;
				closestDist = dist;
			}
		}

		if(closestMember)
		{
			entGetPos(closestMember, pos);
			addVec3(center_pos, pos, center_pos);
			count++;
		}
	}

	if(count)
	{
		scaleVec3(center_pos, 1.0/count, team->roamingLeashPoint);
		team->roamingLeashPointValid = true;
	}
	else
	{
		// Only happens if you get a roaming leash addition when you have no targets
		Vec3 pos;
		for(i=0; i<eaSize(&team->members); i++)
		{
			entGetPos(team->members[i]->memberBE, pos);
			addVec3(center_pos, pos, center_pos);
		}
		scaleVec3(center_pos, 1.0/eaSize(&team->members), team->roamingLeashPoint);
		team->roamingLeashPointValid = true;
	}
}

void aiTeamGetAveragePosition(const AITeam *team,Vec3 vPos)
{
	int i;
	Vec3 center_pos = {0};
	int count = 0;

	for(i=0; i<eaSize(&team->members); i++)
	{
		Vec3 pos;
		entGetPos(team->members[i]->memberBE, pos);
		addVec3(center_pos, pos, center_pos);
	}
	scaleVec3(center_pos, 1.0/eaSize(&team->members), vPos);
}

// turn on collision between the entities on the team, and reset the walk/run distances
static void aiTeamMember_AmbientStateLeave(Entity* e, AITeam* team)
{
	if (!e->pPlayer)
	{
		mmCollisionSetHandleDestroyFG(&e->mm.mcsHandle);
		if (g_CombatConfig.bSwitchCapsulesInCombat)
		{
			// -1 means to use second capsule set if available
			mmCollisionSetHandleCreateFG(e->mm.movement, &e->mm.mcsHandle, __FILE__, __LINE__, -1); 
		}

		aiMovementSetWalkRunDist(e, e->aibase, 0, 0, 0);

		mrSurfaceSetPhysicsCheating(e->mm.mrSurface, false);
	}	
}


static void aiTeamMember_AmbientStateEnter(Entity* e, AITeam* team)
{
	// Do not set a collision set for players
	if (!e->pPlayer)
	{
		// turn off collision between the team members and then set the walk/run distance

		if(team->collId)
		{
			mmCollisionSetHandleDestroyFG(&e->mm.mcsHandle);
			mmCollisionSetHandleCreateFG(e->mm.movement, &e->mm.mcsHandle, __FILE__, __LINE__, team->collId);
		}

		aiMovementSetWalkRunDist(e, e->aibase, 3, 10, 1);

		mrSurfaceSetPhysicsCheating(e->mm.mrSurface, true);
	}
}

static void aiTeamAmbientStateEnter(AITeam *team, const char* reason)
{
	zeroVec3(team->roamingLeashPoint);
	team->roamingLeashPointValid = false;
	team->combatSetup = false;

	if (! team->bHasControlledPets)
	{
		FOR_EACH_IN_EARRAY(team->members, AITeamMember, pMember)
			aiTeamMember_AmbientStateEnter(pMember->memberBE, team);
		FOR_EACH_END
	}
	
	aiTeamClearStatusTable(team, STACK_SPRINTF("Ambient Enter: %s", reason));
}


static void aiTeamStaredownStateExit(AITeam *team)
{
	aiCombatRole_CleanupFormation(team);
}

static void aiTeamStaredownStateEnter(AITeam *team)
{
	aiCombatRole_SetupStartCombat(team);
	aiCombatRole_SetupTeamFormation(team->partitionIdx, team);
	team->combatSetup = true;
}

static void aiTeamWaitForFightStateEnter(AITeam *team)
{
	// If we're using a roaming leash, figure out where to put it
	if(team->roamingLeash)
		aiTeamCalcRoamingLeashPointFromTargets(team);
}

static void aiTeamFightStateEnter(AITeam *team)
{
	bool bWasAnyoneDamaged = false;
	int partitionIdx = team->partitionIdx;

	if (!eaSize(&team->members))
		return; // analyze was complaining

	if (!team->bHasControlledPets)
	{
		FOR_EACH_IN_EARRAY(team->members, AITeamMember, pMember)
			aiTeamMember_AmbientStateLeave(pMember->memberBE, team);
		FOR_EACH_END
	}
	

	team->time.lastStartedCombat = ABS_TIME_PARTITION(partitionIdx);
	
	if (gConf.bLogEncounterSummary)
	{	
		int i;
		Entity *pLeader = aiTeamGetLeader(team);

		if (pLeader && pLeader->pCritter && encounter_GetTemplate(pLeader->pCritter->encounterData.pGameEncounter))
		{
			gslEncounterLog_Register(team->collId, pLeader->pCritter->encounterData.activeTeamLevel, pLeader->pCritter->encounterData.activeTeamSize,
				encounter_GetTemplate(pLeader->pCritter->encounterData.pGameEncounter)->pcName);

			FOR_EACH_IN_EARRAY(team->members, AITeamMember, pTeamMember)
				gslEncounterLog_AddEntity(team->collId, pTeamMember->memberBE, true);
			FOR_EACH_END

			for(i = eaSize(&team->statusTable)-1; i >= 0; i--)
			{
				AITeamStatusEntry* teamStatus = team->statusTable[i];
				if(teamStatus->legalTarget)
				{
					gslEncounterLog_AddEntity(team->collId, entFromEntityRef(team->partitionIdx, teamStatus->entRef), false);
				}
			}
		}
	}

	if (! team->combatSetup)
	{
		aiCombatRole_SetupStartCombat(team);
		team->combatSetup = true;
	}
	if (team->config.teamForceAttackTargetOnAggro)
	{
		aiTeamAssignAttackTargets(team);
	}


	if(team->roamingLeash)
		aiTeamCalcRoamingLeashPointFromTargets(team);

	ANALYSIS_ASSUME(team->members);
	if(team->calcOffsetOnDemand && team->members[0]->memberBE->pCritter)
	{
		int i;
		Vec3 center = {0};
		int count = 0;

		for(i=eaSize(&team->members)-1; i>=0; i--)
		{
			Vec3 pos;
			Entity *memberE = team->members[i]->memberBE;

			entGetPos(memberE, pos);
			if(team->roamingLeash && team->roamingLeashPointValid)
			{
				subVec3(pos, team->roamingLeashPoint, memberE->aibase->spawnOffset);
				memberE->aibase->spawnOffsetDirtied = 1;
			}
			else
			{
				addVec3(pos, center, center);
				count++;
			}
		}

		if(!team->roamingLeash || !team->roamingLeashPointValid)
		{
			scaleVec3(center, 1.0/count, center);

			for(i=eaSize(&team->members)-1; i>=0; i--)
			{
				Vec3 pos;
				Entity *memberE = team->members[i]->memberBE;

				entGetPos(memberE, pos);

				subVec3(pos, center, memberE->aibase->spawnOffset);
				memberE->aibase->spawnOffsetDirtied = 1;
			}
		}

	}

	// if we have any initial aggro wait time
	if (team->config.initialAggroWaitTimeMin || team->config.initialAggroWaitTimeRange)
	{
		// Check if anyone was damaged on the team in the last second
		FOR_EACH_IN_EARRAY(team->members, AITeamMember, pTeamMember)
			if (pTeamMember->memberBE && pTeamMember->memberBE->aibase)
			{
				AIVarsBase *aib = pTeamMember->memberBE->aibase;
				if (ABS_TIME_SINCE_PARTITION(partitionIdx, aib->time.lastDamage[AI_NOTIFY_TYPE_DAMAGE]) < SEC_TO_ABS_TIME(1.f))
				{
					bWasAnyoneDamaged = true;
					break;
				}
			}
		FOR_EACH_END

		if (bWasAnyoneDamaged == false)
		{
			// no one was damaged, use half of the aggro wait time
			FOR_EACH_IN_EARRAY(team->members, AITeamMember, pTeamMember)
				if (pTeamMember->memberBE && pTeamMember->memberBE->aibase && !pTeamMember->memberBE->pPlayer)
				{
					AIVarsBase *aib = pTeamMember->memberBE->aibase;
					F32 fMin = team->config.initialAggroWaitTimeMin * 0.5f;
					F32 fRange = team->config.initialAggroWaitTimeRange * 0.5f;
					aib->time.enterCombatWaitTime = ABS_TIME_PARTITION(partitionIdx) + SEC_TO_ABS_TIME(fMin + randomPositiveF32() * fRange);
				}
			FOR_EACH_END
		}
		else
		{
			FOR_EACH_IN_EARRAY(team->members, AITeamMember, pTeamMember)
				if (pTeamMember->memberBE && pTeamMember->memberBE->aibase && !pTeamMember->memberBE->pPlayer)
				{
					AIVarsBase *aib = pTeamMember->memberBE->aibase;
					bool bLegalTargetIsVisible = false;
					bool bWasDamaged = false;

					aib->time.enterCombatWaitTime = ABS_TIME_PARTITION(partitionIdx);
					
					bWasDamaged = ABS_TIME_SINCE_PARTITION(partitionIdx, aib->time.lastDamage[AI_NOTIFY_TYPE_DAMAGE]) < SEC_TO_ABS_TIME(1.f);
					if (! bWasDamaged)
					{ // hasn't taken damage that initiated combat, increase the wait time
						F32 fMin = team->config.initialAggroWaitTimeMin;
						F32 fRange = team->config.initialAggroWaitTimeRange;
						aib->time.enterCombatWaitTime += SEC_TO_ABS_TIME(fMin + randomPositiveF32() * fRange);
					}

					// check if we have any legal target visible 
					FOR_EACH_IN_EARRAY(aib->statusTable, AIStatusTableEntry, status)
					{
						AITeamStatusEntry *teamStatus = aiGetTeamStatus(pTeamMember->memberBE, aib, status);
						if(teamStatus && teamStatus->legalTarget && status->visible && status->inFrontArc)
						{
							bLegalTargetIsVisible = true;
							break;
						}
					}
					FOR_EACH_END;

					if (!bLegalTargetIsVisible)
					{	// no legal target is visible, add a fraction to our wait time 
						F32 fMin = team->config.initialAggroWaitTimeMin * 0.25f;
						F32 fRange = team->config.initialAggroWaitTimeRange * 0.25f;
						if (bWasDamaged) 
							fRange = 0.f;
						aib->time.enterCombatWaitTime += SEC_TO_ABS_TIME(fMin + randomPositiveF32() * fRange);
					}

				}
			FOR_EACH_END
		}
	}

	aigcTeamEnterCombat(team);

	{
		int count = 1;
		static AITeamMember **memberCopy = NULL;

		eaCopy(&memberCopy, &team->members);
		while(count>0 && eaSize(&memberCopy))
		{
			int idx = eaRandIndex(&memberCopy);
			AITeamMember *chosen = memberCopy[idx];

			aiSayVoiceMessage(chosen->memberBE, chosen->memberBE->aibase->exprContext, "Combat_Enter");

			eaRemoveFast(&memberCopy, idx);			
			count--;
		}
	}
}

static void aiTeamClearReinforcements(AITeam *team, int resetReinforced)
{
	AITeam* rTeam = team->reinforceTeam;

	if(rTeam)
	{
		rTeam->reinforceTeam = NULL;
		rTeam->reinforceCandidate = false;
		team->reinforceTeam = NULL;
		team->reinforceCandidate = false;

		if(resetReinforced)
		{
			team->reinforced = 0;
			rTeam->reinforced = 0;
		}
	}
}

static void aiTeamSwitchCombatState(AITeam* team, AITeamCombatState newState, const char* reason); 

static void aiTeamCombatStateExit(AITeam *team)
{
	AITeam* rTeam = team->reinforceTeam;
	if(rTeam)
	{
		devassert(rTeam->reinforceTeam==team);

		if(team->reinforced)
		{
			team->reinforced = false;
			rTeam->reinforced = false;
			aiTeamSwitchCombatState(rTeam, AITEAM_COMBAT_STATE_LEASH, "Reinforced team leashed");	
		}
	}

	aiTeamClearReinforceTarget(NULL, NULL, team, NULL, 1, 1);
	team->reinforceMember = NULL;

	// clear all the team member's preferredTargetRefs if they are enemies and exit combat jobs properly
	FOR_EACH_IN_EARRAY(team->members, AITeamMember, pTeamMember);
		if (pTeamMember->memberBE && pTeamMember->memberBE->aibase)
		{
			// Exit combat job properly
			aiCombatJob_TeamExitFightState(pTeamMember->memberBE, pTeamMember->memberBE->aibase);

			if (pTeamMember->memberBE->aibase->preferredTargetIsEnemy)
			{
				aiClearPreferredAttackTarget(pTeamMember->memberBE, pTeamMember->memberBE->aibase);
			}
		}
	FOR_EACH_END;

	// clear out the assignments when we exit combat, if there is out of combat healing/buff
	// it will recreate the assignments after
	eaDestroyEx(&team->healAssignments, aiTeamMemberAssignmentDestroy); 

	aigcTeamExitCombat(team);
}

static void aiTeamSwitchCombatState(AITeam* team, AITeamCombatState newState, const char* reason)
{
	int i;

	if(team->config.skipLeashing && newState == AITEAM_COMBAT_STATE_LEASH)
		newState = AITEAM_COMBAT_STATE_AMBIENT;
	else if(newState == AITEAM_COMBAT_STATE_FIGHT && !team->memberInCombat)
		newState = AITEAM_COMBAT_STATE_WAITFORFIGHT;

	if(team->combatState == newState)
		return;
	
	TEAM_DEBUG_PRINTF("Team %p changed from %s to %s\n", team,
		StaticDefineIntRevLookup(AITeamCombatStateEnum, team->combatState),
		StaticDefineIntRevLookup(AITeamCombatStateEnum, newState));

	if(newState==AITEAM_COMBAT_STATE_LEASH && 
		(team->combatState==AITEAM_COMBAT_STATE_AMBIENT ||
		 team->combatState==AITEAM_COMBAT_STATE_STAREDOWN))
	{
		Entity *ldr = aiTeamGetLeader(team);
		Entity *rldr = team->reinforceTeam ? aiTeamGetLeader(team->reinforceTeam) : NULL;
		ErrorDetailsf("%p->%p | Team Leader: %s, RTeam Leader: %s, State: %s",
					team,
					team->reinforceTeam,
					ENTDEBUGNAME(ldr), 
					rldr ? ENTDEBUGNAME(rldr) : "None",
					StaticDefineIntRevLookup(AITeamCombatStateEnum, team->combatState));
		ErrorfForceCallstack("Switching to leash state from invalid state.");
	}

	// exit state logic
	switch(team->combatState)
	{
		xcase AITEAM_COMBAT_STATE_AMBIENT:
		xcase AITEAM_COMBAT_STATE_STAREDOWN: {
			aiTeamStaredownStateExit(team);
		}
		xcase AITEAM_COMBAT_STATE_WAITFORFIGHT:
		xcase AITEAM_COMBAT_STATE_FIGHT: {
			aiTeamCombatStateExit(team);
		}
		xcase AITEAM_COMBAT_STATE_LEASH: {
			aiTeamLeashStateExit(team);
		}
	}

	if(newState==AITEAM_COMBAT_STATE_LEASH || newState==AITEAM_COMBAT_STATE_AMBIENT)
	{
		for(i = eaSize(&team->members)-1; i >= 0; i--)
		{
			Entity* memberBE = team->members[i]->memberBE;
			AIVarsBase *aib = memberBE->aibase;
			if(stashGetCount(aib->offtickInstances))
			{
				StashTableIterator iter;
				StashElement elem;

				stashGetIterator(aib->offtickInstances, &iter);
				while(stashGetNextElement(&iter, &elem))
				{
					AIOfftickInstance *inst = stashElementGetPointer(elem);

					inst->executedThisCombat = 0;
				}
			}
		}
	}

	switch(newState)
	{
		xcase AITEAM_COMBAT_STATE_AMBIENT: {
			aiTeamAmbientStateEnter(team, reason);
		}
		xcase AITEAM_COMBAT_STATE_STAREDOWN: {
			aiTeamStaredownStateEnter(team);
		}
		xcase AITEAM_COMBAT_STATE_WAITFORFIGHT: {
			aiTeamWaitForFightStateEnter(team);
		}
		xcase AITEAM_COMBAT_STATE_FIGHT:{
			aiTeamFightStateEnter(team);
		}
		xcase AITEAM_COMBAT_STATE_LEASH:
			aiTeamLeashStateEnter(team);	
	}

	team->combatState = newState;
}

int aiTeamTargetWithinLeash(AITeamMember* member, AITeam* team, Entity* target, F32* outDist)
{
	F32 dist;
	Entity *owner = NULL;

	if(team->config.ignoreMaxProtectRadius)
		return true;
	
	if(member && (owner = entGetOwner(member->memberBE)) && entIsPlayer(owner))
		dist = entGetDistance(owner, NULL, target, NULL, NULL);
	else if(team->teamOwner)
	{
		if (entIsPlayer(team->teamOwner))
		{
			S32 i, n = eaSize(&team->members);
			dist = FLT_MAX;
			for (i = 0; dist >= team->leashDist && i < n; i++)
			{
				Entity *ent = team->members[i]->memberBE;
				if (ent && entIsPlayer(ent))
				{
					F32 entDist = entGetDistance(ent, NULL, target, NULL, NULL);
					dist = MIN(dist, entDist);
				}
			}
		}
		else
		{
			dist = entGetDistance(team->teamOwner, NULL, target, NULL, NULL);
		}
	}
	else if(team->roamingLeash)
	{
		// If not in combat, use member's position to determine this
		if(!team->roamingLeashPointValid)
		{
			devassert(member);
			dist = entGetDistance(member->memberBE, NULL, target, NULL, NULL);
		}
		else
		{
			dist = entGetDistance(NULL, team->roamingLeashPoint, target, NULL, NULL);
		}
	}
	else
		dist = entGetDistance(NULL, team->spawnPos, target, NULL, NULL);

	if(outDist)
		*outDist = dist;

	return dist < team->leashDist;
}

static int aiTeamLostTrack(AITeam *team, Entity *target)
{
	int i;

	// Sorta not what this is named for, but it is part of leashing
	if(team->config.ignoreMaxProtectRadius)
		return false;

	for(i=eaSize(&team->members)-1; i>=0; i--)
	{
		AITeamMember *member = team->members[i];
		AIStatusTableEntry *status = aiStatusFind(member->memberBE, member->memberBE->aibase, target, 0);

		if(!status)
			return false;

		if(!status->lostTrack)
			return false;
	}

	return true;
}

void aiTeamUpdateStateFromStatusList(AITeam* team, int foundPerceptionRangeTarget)
{
	int i;
	int foundLegalTarget = false;
	RegionRules *rules = NULL;
	Entity *leader = aiTeamGetLeader(team);
	Vec3 leaderPos;
	int partitionIdx = team->partitionIdx;

	for(i = eaSize(&team->statusTable)-1; i >= 0; i--)
	{
		AITeamStatusEntry* teamStatus = team->statusTable[i];
		Entity* target = entFromEntityRef(partitionIdx, teamStatus->entRef);

		if(!target)
		{
			teamStatus->legalTarget = 0;
			continue;
		}

		if(teamStatus->legalTarget &&
			ABS_TIME_SINCE_PARTITION(partitionIdx, teamStatus->timeLastStatusUpdate) > SEC_TO_ABS_TIME(AI_STATUS_NO_PROCESS_DROP))
		{
			teamStatus->legalTarget = 0;
		}

		if(teamStatus->legalTarget && aiTeamLostTrack(team, target))
			teamStatus->legalTarget = 0;

		if(teamStatus->legalTarget && (entIsPlayer(target) ? entIsAlive(target) : aiIsEntAlive(target)) &&
			aiIsValidTarget(leader, team, target, teamStatus))
		{
			int result = aiTeamTargetWithinLeash(NULL, team, target, NULL);
			teamStatus->legalTarget = result || (aiGlobalSettings.disableLeashingOnNonStaticMaps && zmapInfoGetMapType(NULL) != ZMTYPE_STATIC && !team->bLeashOnNonStaticOverride);
			foundLegalTarget |= teamStatus->legalTarget;
		}
	}

	if (!foundLegalTarget && entIsPlayer(leader))
	{
		for(i = eaiSize(&leader->aibase->statusCleanup)-1; i >= 0; i--)
		{
			Entity* trackingEnt = entFromEntityRef(partitionIdx, leader->aibase->statusCleanup[i]);
			AIStatusTableEntry* trackingStatus;
			AITeamStatusEntry* teamStatus;
			if(!trackingEnt || !critter_IsKOS(partitionIdx, trackingEnt, leader) || !aiIsEntAlive(trackingEnt))
				continue;

			trackingStatus = aiStatusFind(trackingEnt, trackingEnt->aibase, leader, false);
			teamStatus = aiGetTeamStatus(trackingEnt, trackingEnt->aibase, trackingStatus);
			if(trackingStatus && teamStatus && teamStatus->legalTarget)
			{
				foundLegalTarget = true;
				break;
			}
		}
	}

	entGetPos(leader, leaderPos);
	rules = RegionRulesFromVec3(leaderPos); 

	if(foundLegalTarget)
		team->time.lastHadLegalTarget = ABS_TIME_PARTITION(partitionIdx);

	switch(team->combatState)
	{
		// this function should only get called in this state from an inCombat team that has
		// now reached its leash point, or from a team that is not in combat, so the team should
		// leave leash either way
		xcase AITEAM_COMBAT_STATE_LEASH:
		{
			if(foundLegalTarget)
				// SwitchCombatState will check whether this needs to be FIGHT or WAITFORFIGHT
				aiTeamSwitchCombatState(team, AITEAM_COMBAT_STATE_FIGHT, "Leashing but found legal target");
			else if(foundPerceptionRangeTarget && !team->dontAggroInAggroRadius)
				aiTeamSwitchCombatState(team, AITEAM_COMBAT_STATE_STAREDOWN, "Leashing but found target in perc range");
			else
				aiTeamSwitchCombatState(team, AITEAM_COMBAT_STATE_AMBIENT, "Leash completed");
		}
		xcase AITEAM_COMBAT_STATE_STAREDOWN:
			devassertmsg(0, "Shouldn't be checking this for staredown");
		xcase AITEAM_COMBAT_STATE_WAITFORFIGHT:
			if(!foundLegalTarget)
				aiTeamSwitchCombatState(team, AITEAM_COMBAT_STATE_AMBIENT, "WaitForFight: No legal target");
		xcase AITEAM_COMBAT_STATE_FIGHT:
			if(!foundLegalTarget)
			{
				if(!rules || !rules->leashSettings ||
					ABS_TIME_SINCE_PARTITION(partitionIdx, team->time.lastHadLegalTarget)>=SEC_TO_ABS_TIME(rules->leashSettings->fNoTargetWaitTime))
				{
					aiTeamLeash(team);
				}
			}
		xcase AITEAM_COMBAT_STATE_AMBIENT:
			devassertmsg(0, "Shouldn't be checking this for ambient");
	}
}

int aiTeamForceReinforcements = false;
AUTO_CMD_INT(aiTeamForceReinforcements, aiTeamForceReinforcements);

S32 aiTeamReinforcementCheckMapVar(int iPartitionIdx)
{
	MapVariable *var = mapvariable_GetByName(iPartitionIdx, "ReinforceAllow");

	if(!var)
		return -1;

	if(var->pDef->eType!=WVAR_INT)
	{
		ErrorFilenamef(zmapGetFilename(NULL), "ReinforceAllow variable was not an int!");
		return -1;
	}

	return !!var->pVariable->iIntVal;
}

int aiTeamReinforceIgnoreMapType = false;
AUTO_CMD_INT(aiTeamReinforceIgnoreMapType, aiTeamReinforceIgnoreMapType);

S32 aiTeamNeedsReinforcement(AITeam *team)
{
	int i;
	int playerCount = 0;
	F32 ratio;
	S32 mapVar;

	if(aiGlobalSettings.enableCombatTeams)
		return 0;

	if(aiTeamForceReinforcements || aiGlobalSettings.forceReinforcement)
		return 1;

	mapVar = aiTeamReinforcementCheckMapVar(entGetPartitionIdx(team->members[0]->memberBE));

	// If specified, ignore map type requirement
	if(mapVar==-1)
	{
		if(zmapInfoGetMapType(NULL)!=ZMTYPE_STATIC && zmapInfoGetMapType(NULL)!=ZMTYPE_SHARED && !aiTeamReinforceIgnoreMapType)
			return 0;
	}
	if(!mapVar)
		return 0;

	for(i=eaSize(&team->statusTable)-1; i>=0; i--)
	{
		AITeamStatusEntry *status = team->statusTable[i];
		Entity *ent = entFromEntityRef(team->partitionIdx, status->entRef);
		if(status->legalTarget && ent && ent->pPlayer)
		{
			Vec3 pos;

			entGetPos(ent, pos);

			if(team->roamingLeash && team->roamingLeashPointValid)
			{
				if(distance3Squared(pos, team->roamingLeashPoint)>SQR(aiGlobalSettings.reinforceTeamDist))
					continue;
			}
			else if(distance3Squared(pos, team->spawnPos)>SQR(aiGlobalSettings.reinforceTeamDist))
				continue;
			playerCount++;
		}
	}

	if(playerCount>1)
	{
		int index = playerCount-2;
		MINMAX1(index, 0, ARRAY_SIZE(aiGlobalSettings.reinforceLevels));

		ratio = team->curHP/team->maxHP;
		if(ratio < aiGlobalSettings.reinforceLevels[index])
			return 1;
	}

	return 0;
}

AITeamMember* aiTeamGetReinforceMember(AITeam* team)
{
	int i;
	int minRankOrder = INT_MAX;
	static AITeamMember **localMembers = NULL;

	eaCopy(&localMembers, &team->members);

	for(i=eaSize(&localMembers)-1; i>=0; i--)
	{
		Entity *member = localMembers[i]->memberBE;
		if(!member->pCritter || !member->aibase->inCombat || !member->aibase->attackTargetRef)
		{
			eaRemoveFast(&localMembers, i);
			continue;
		}

		MIN1(minRankOrder, critterRankGetOrder(member->pCritter->pcRank));
	}

	for(i=eaSize(&localMembers)-1; i>=0; i--)
	{
		Entity *memberE = localMembers[i]->memberBE;

		ANALYSIS_ASSUME(memberE->pCritter);
		if(critterRankGetOrder(memberE->pCritter->pcRank)!=minRankOrder)
			eaRemoveFast(&localMembers, i);
	}

	if(!eaSize(&localMembers))
		return NULL;

	return eaRandChoice(&localMembers);
}

void aiTeamUpdate(AITeam* team)
{
	int i;
	int teamBotheredCount = 0;
	int everyoneAtSpawnPos = true;
	int everyoneLeashStateDone = true;
	int checkStatusRadii = false;
	int checkSpawnPosDist = false;
	int foundPerceptionRangeTarget = false;
	int memberAlive = false;
	int teamHealed = true;
	int readyForUpdate = false;
	int respondToAttacksWhileLeashing = false;
	F32 timeElapsed;
	RegionRules *rules = NULL;
	int partitionIdx = team->partitionIdx;

	PERFINFO_AUTO_START_FUNC();

	if(team->noUpdate)
	{
		PERFINFO_AUTO_STOP();
		return;
	}

	if(team->combatTeam && (team->combatState==AITEAM_COMBAT_STATE_AMBIENT || team->combatState==AITEAM_COMBAT_STATE_STAREDOWN))
	{
		for(i=eaSize(&team->members)-1; i>=0; i--)
		{
			aiTeamRemove(&team, team->members[i]->memberBE);
		}

		PERFINFO_AUTO_STOP();
		return;
	}		

	// Update all the owned formations for the team.  It's possible for the team itself to not have a formation if all of the
	// AIs on the team are pets owned by players who aren't the team leader.
	for(i=eaSize(&team->members)-1; i>=0; i--)
	{
		Entity * pEnt = team->members[i]->memberBE;
		if (pEnt->aibase->pFormationData)
		{
			U32 isLeader = pEnt->aibase->pFormationData->pFormation->erLeader == pEnt->myRef;
			U32 noLeader = entFromEntityRef(entGetPartitionIdx(pEnt), pEnt->aibase->pFormationData->pFormation->erLeader)==NULL;

			if (isLeader || noLeader)
			{
				aiFormation_UpdateFormation(team->partitionIdx,pEnt->aibase->pFormationData->pFormation);
			}
		}
	}

	timeElapsed = ABS_TIME_TO_SEC(ABS_TIME_SINCE_PARTITION(partitionIdx, team->time.lastTick));
	team->time.lastTick = ABS_TIME_PARTITION(partitionIdx);

	if(!team->calculatedSpawnPos)
		aiTeamCalculateSpawnPos(team);

	aigcTick(team, timeElapsed);
	aiTeamDistributeJobs(team);

	if(team->combatState == AITEAM_COMBAT_STATE_LEASH &&
		ABS_TIME_SINCE_PARTITION(partitionIdx, team->time.lastCheckedSpawnPosDist) > SEC_TO_ABS_TIME(0.5))
	{
		checkSpawnPosDist = true;
		team->time.lastCheckedSpawnPosDist = ABS_TIME_PARTITION(partitionIdx);
	}
	else
		everyoneAtSpawnPos = false; // we're not checking this tick

	checkStatusRadii = team->combatState != AITEAM_COMBAT_STATE_AMBIENT &&
		team->combatState != AITEAM_COMBAT_STATE_STAREDOWN;

	team->memberInCombat = false;

	for(i = eaSize(&team->members)-1; i >= 0; i--)
	{
		Entity* memberBE = team->members[i]->memberBE;
		AIVarsBase* memberAIB = memberBE->aibase;
		AIConfig *memberConfig = aiGetConfig(memberBE, memberAIB);
		int healed = 0;
		Vec3 pos;

		entGetPos(memberBE, pos);

		if (!aiIsEntAlive(memberBE))
			continue;
			 
		memberAlive = true;

		if(team->combatState==AITEAM_COMBAT_STATE_LEASH)
		{
			rules = RegionRulesFromVec3NoOverride(pos);

			if(!rules || !rules->leashSettings)
				memberAIB->leashState = AI_LEASH_STATE_DONE;
			switch(memberAIB->leashState)
			{
				xcase AI_LEASH_STATE_START: {
					if(ABS_TIME_PASSED(team->time.lastStartedLeashing, rules->leashSettings->fDurationStart) &&
						memberAIB->leashAnimQueue)
					{
						Vec3 spawnPos;
						AIAnimList *anim = RefSystem_ReferentFromString("AIAnimList", rules->leashSettings->animListFinish);
						
						CommandQueue_ExecuteAllCommands(memberAIB->leashAnimQueue);
						aiGetSpawnPos(memberBE, memberAIB, spawnPos);
						entSetPos(memberBE, spawnPos, 1, "LeashTeleport");
						entSetRot(memberBE, memberAIB->spawnRot, 1, "LeashTeleport");
						if(anim)
						{
							aiAnimListSet(memberBE, anim, &memberAIB->leashAnimQueue);
							memberAIB->time.timeLeashWait = ABS_TIME_PARTITION(partitionIdx);
							memberAIB->leashState = AI_LEASH_STATE_FINISH;
						}
						else
							memberAIB->leashState = AI_LEASH_STATE_DONE;
					}
				}
				xcase AI_LEASH_STATE_FINISH: {
					if(rules->leashSettings && 
						ABS_TIME_PASSED(memberAIB->time.timeLeashWait, rules->leashSettings->fDurationFinish))
					{
						CommandQueue_ExecuteAllCommands(memberAIB->leashAnimQueue);
						memberAIB->leashState = AI_LEASH_STATE_DONE;
					}
					else if(!rules->leashSettings)
						memberAIB->leashState = AI_LEASH_STATE_DONE;
				}
				xcase AI_LEASH_STATE_DONE: {
					;
				}
			}
			everyoneLeashStateDone &= memberAIB->leashState==AI_LEASH_STATE_DONE;

			if(memberAIB->attackTargetRef && memberConfig->respondToAttacksWhileLeashing)
			{
				respondToAttacksWhileLeashing = true;
			}
		}

		if(checkSpawnPosDist)
		{
			const int iLeashTimeOut = 60;
			int atSpawnPos = aiCloseEnoughToSpawnPos(memberBE, memberAIB);
			if(!atSpawnPos && 
				ABS_TIME_SINCE_PARTITION(partitionIdx, team->time.lastStartedLeashing)>SEC_TO_ABS_TIME(iLeashTimeOut) && 
				!memberAIB->failedToLeash &&
				!aiGlobalSettings.disableLeashMessage)
			{
				Vec3 spawnPos;
				if(memberBE->pCritter->encounterData.pGameEncounter)
				{
					GameEncounter *pEncounter = memberBE->pCritter->encounterData.pGameEncounter;
					if(pEncounter)
					{
						ErrorDetailsf("Critter: %s, Pos: "LOC_PRINTF_STR, memberBE->debugName, vecParamsXYZ(pos));
						ErrorFilenamef(encounter_GetFilename(pEncounter), 
							"Critter failed to leash to spawn point for >%ds",iLeashTimeOut);
					}
				}
				else if(gConf.bAllowOldEncounterData && memberBE->pCritter->encounterData.parentEncounter)
				{
					OldStaticEncounter *staticEnc = GET_REF(memberBE->pCritter->encounterData.parentEncounter->staticEnc);

					if(staticEnc)
					{
						ErrorFilenamef(staticEnc->pchFilename, 
										"Critter %s failed to leash to spawn point for >%ds, at "LOC_PRINTF_STR,
										memberBE->debugName, iLeashTimeOut, vecParamsXYZ(pos));
					}
				}
				memberAIB->failedToLeash = 1;

				aiGetSpawnPos(memberBE, memberAIB, spawnPos);
				entSetPos(memberBE, spawnPos, true, "Failed to leash within timeout");
				atSpawnPos = true;
			}

			if(memberAIB->inCombat)
				everyoneAtSpawnPos &= atSpawnPos;
		}

		if(memberAIB->inCombat)
		{
			team->memberInCombat = true;

			foundPerceptionRangeTarget |=
				ABS_TIME_SINCE_PARTITION(partitionIdx, memberAIB->time.lastHadStaredownTarget) < SEC_TO_ABS_TIME(1);
		}

		//if(memberAIB->bothered)
			//teamBotheredCount++;
		if(memberBE->pChar)
		{
			F32 curHP = memberBE->pChar->pattrBasic->fHitPoints;
			F32 maxHP = memberBE->pChar->pattrBasic->fHitPointsMax;

			if(!memberConfig->dontDoGrievedHealing)
			{
				if(eafSize(&memberConfig->grievedHealingLevels))
				{
					int j, n;
					F32 ratio = curHP/maxHP;

					n = eafSize(&memberConfig->grievedHealingLevels);
					for(j=0; j<n; j++)
					{
						F32 level = memberConfig->grievedHealingLevels[j];

						if(ratio<level)
							break;
					}
					if(j<n && memberConfig->grievedHealingLevels[j]<memberAIB->minGrievedHealthLevel)
						memberAIB->minGrievedHealthLevel = memberConfig->grievedHealingLevels[j];
				}
				
				if(memberAIB->healing && aiGlobalSettings.leashHealRate)
				{
					curHP += aiGlobalSettings.leashHealRate * maxHP;
					if(curHP > memberAIB->minGrievedHealthLevel * maxHP)
					{
						curHP = maxHP * memberAIB->minGrievedHealthLevel;
						memberAIB->healing = 0;
					}
					memberBE->pChar->pattrBasic->fHitPoints = curHP;

					character_DirtyAttribs(memberBE->pChar);
				}
			}

			team->curHP += memberBE->pChar->pattrBasic->fHitPoints;
			team->maxHP += memberBE->pChar->pattrBasic->fHitPointsMax;
			teamHealed &= !memberAIB->healing;
		}
	}

	if(!memberAlive)
	{
		PERFINFO_AUTO_STOP();
		return;
	}

	if(team->combatState == AITEAM_COMBAT_STATE_STAREDOWN && !foundPerceptionRangeTarget)
		aiTeamSwitchCombatState(team, AITEAM_COMBAT_STATE_AMBIENT, "Staredown - Lost targets");
	else if(team->combatState == AITEAM_COMBAT_STATE_FIGHT && !team->memberInCombat)
		aiTeamSwitchCombatState(team, AITEAM_COMBAT_STATE_WAITFORFIGHT, "Fight - No one in combat");

	readyForUpdate = false;
	if(team->combatState == AITEAM_COMBAT_STATE_FIGHT || team->combatState == AITEAM_COMBAT_STATE_WAITFORFIGHT)
		readyForUpdate = true;
	else if(team->combatState == AITEAM_COMBAT_STATE_LEASH)
	{
		if(ABS_TIME_SINCE_PARTITION(partitionIdx, team->time.lastStartedLeashing)>SEC_TO_ABS_TIME(35))
			readyForUpdate = true;
		else if(everyoneLeashStateDone &&
				(everyoneAtSpawnPos || !team->memberInCombat) && 
				teamHealed)
		{
			readyForUpdate = true;
		}
		else if(respondToAttacksWhileLeashing)
		{
			readyForUpdate = true;
		}
	}

	if(readyForUpdate)
	{
		aiTeamUpdateStateFromStatusList(team, foundPerceptionRangeTarget);
	}

	
	if((team->combatState == AITEAM_COMBAT_STATE_FIGHT && team->memberInCombat) || 
		(team->combatState == AITEAM_COMBAT_STATE_AMBIENT && team->config.useHealBuffAssignmentsOOC))
	{
		if(team->powInfo.hasHealPowers || team->powInfo.hasBuffPowers || 
			team->powInfo.hasResPowers || team->powInfo.hasShieldHealPowers || 
			team->powInfo.hasCurePower )
		{
			aiTeamProcessTeammates(team);
		}
	}

	if(team->combatState == AITEAM_COMBAT_STATE_FIGHT && team->memberInCombat)
	{
		aiTeamAssignAttackTargets(team);
		aiCombatRole_CombatTick(team);
	}

	if(team->combatState == AITEAM_COMBAT_STATE_FIGHT && team->memberInCombat)
	{
		int shouldReinforce = 0;
		if(team->reinforceMember)
		{
			Entity* memberBE = team->reinforceMember->memberBE;
			AIVarsBase* memberAIB = memberBE->aibase;
			if(team->dontReinforce || !memberAIB->inCombat || !memberAIB->attackTargetRef)
			{
				if(team->reinforceTeam)
					aiTeamClearReinforceTarget(memberBE, memberAIB, team, team->reinforceTeam, 1, 0);
				team->reinforceMember = NULL;
			}
		}

		shouldReinforce = aiTeamNeedsReinforcement(team);

		if(shouldReinforce && !team->dontReinforce && !team->reinforceCandidate && !team->reinforced &&
			!team->reinforceMember && eaSize(&team->members) > 1 &&
			ABS_TIME_SINCE_PARTITION(partitionIdx, team->time.lastCheckedReinforce) > ABS_TIME_TO_SEC(1))
		{
			team->time.lastCheckedReinforce = ABS_TIME_PARTITION(partitionIdx);

			team->reinforceMember = aiTeamGetReinforceMember(team);
		}
	}

	/*
	if(teamBotheredCount >= eaSize(&team->members) / 2)
		team->bothered = true;
	else
		team->bothered = false;
		*/

	PERFINFO_AUTO_STOP();
}

void aiTeamLeash(AITeam* team)
{
	aiTeamSwitchCombatState(team, AITEAM_COMBAT_STATE_LEASH, "TeamLeash called");
}

void aiTeamEnterCombat(AITeam* team)
{
	// not sure if this assertion is valid anymore? removing it for now
	// devassert(team->combatState == AITEAM_COMBAT_STATE_WAITFORFIGHT);
	aiTeamSwitchCombatState(team, AITEAM_COMBAT_STATE_FIGHT, "Enter combat called");
}

int aiTeamInCombat(SA_PARAM_NN_VALID AITeam* team)
{
	return team->combatState==AITEAM_COMBAT_STATE_FIGHT ||
			team->combatState==AITEAM_COMBAT_STATE_WAITFORFIGHT;
}

void aiTeamTriggerStaredown(AITeam* team)
{
	aiTeamSwitchCombatState(team, AITEAM_COMBAT_STATE_STAREDOWN, "Staredown triggered");
}

void aiTeamRequestReinforcements(Entity* e, AIVarsBase* aib, AITeam* sourceTeam, AITeam* targetTeam)
{
	if(!targetTeam)
	{
		Vec3 pos;
		entGetPos(e, pos);

		ErrorDetailsf("Critter: %s, Pos: "LOC_PRINTF_STR, ENTDEBUGNAME(e), vecParamsXYZ(pos));
		Errorf("aiTeamRequestReinforcements has NULL targetTeam, this information is for the AI team.");

		return;
	}
	else
	{
		int i;
		Entity *reinforceTarget = entFromEntityRef(targetTeam->partitionIdx, aib->reinforceTarget);

		TEAM_DEBUG_PRINTF("Team %p requesting reinforcements from %p", sourceTeam, targetTeam);

		aiSayExternMessageVar(e, aib->exprContext, "Encounter", "Reinforce_Finish", 4);
		if(reinforceTarget)
			aiSayExternMessageVar(reinforceTarget, reinforceTarget->aibase->exprContext, "Encounter", "Reinforce_Acknowledge", 4);
	
		// This is the only location that needs to clear the target but KEEP the reinforceTeam relationship
		aiTeamClearReinforceTarget(e, aib, sourceTeam, targetTeam, 0, 0);
		sourceTeam->reinforceMember = NULL;

		sourceTeam->reinforced = true;
		targetTeam->reinforced = true;

		for(i = eaSize(&sourceTeam->statusTable)-1; i >= 0; i--)
		{
			AITeamStatusEntry* teamStatus = sourceTeam->statusTable[i];
			Entity* teamStatusEnt;
			if(teamStatus->legalTarget && (teamStatusEnt = entFromEntityRef(sourceTeam->partitionIdx, teamStatus->entRef)))
			{
				aiTeamAddLegalTargetInternal(targetTeam, teamStatusEnt);
			}
		}
		aiTeamSwitchCombatState(targetTeam, AITEAM_COMBAT_STATE_FIGHT, "Reinforcements requested");
	}
}

void aiTeamSetReinforceTarget(Entity* e, AIVarsBase* aib, AITeam* team, AITeam* reinforceTeam, Entity* reinforceTarget)
{
	TEAM_DEBUG_PRINTF("%s(%d) setting %s(%d) as reinforcetarget (team %p and %p)\n", e->debugName, e->myRef, reinforceTarget->debugName, reinforceTarget->myRef, team, reinforceTeam);

	aib->reinforceTarget = entGetRef(reinforceTarget);

	aiSayExternMessageVar(e, aib->exprContext, "Encounter", "Reinforce_Start", 4);

	if(aiTeamReinforceParanoid)
	{
		assert(!reinforceTeam->reinforceCandidate);
		assert(!reinforceTeam->reinforceTeam);
		assert(!team->reinforceTeam);
	}
	else
	{
		devassert(!reinforceTeam->reinforceCandidate);
		devassert(!reinforceTeam->reinforceTeam);
		devassert(!team->reinforceTeam);
	}

	reinforceTeam->reinforceCandidate = true;
	reinforceTeam->reinforceTeam = team;

	team->reinforceTeam = reinforceTeam;
}

void aiTeamClearReinforceTarget(Entity* e, AIVarsBase* aib, AITeam* team, AITeam* reinforceTeam, int clearReinforcements, int resetReinforce)
{
	TEAM_DEBUG_PRINTF("%s(%d) clearing reinforcetarget (team %p and %p)\n", e ? e->debugName : NULL, e ? e->myRef : -1, team, reinforceTeam);

	if(aib)
		aib->reinforceTarget = 0;
	else if(team->reinforceMember)
		team->reinforceMember->memberBE->aibase->reinforceTarget = 0;

	if(clearReinforcements)
		aiTeamClearReinforcements(team, resetReinforce);
}

bool aiTeamCanMemberPerceiveTarget(AITeam *team, Character *targetChar)
{
	S32 i;
	for(i = eaSize(&team->members)-1; i >= 0; i--)
	{
		AITeamMember *teamMember = team->members[i];
				
		if(!teamMember->memberBE || !teamMember->memberBE->pChar || !aiIsEntAlive(teamMember->memberBE))
			continue;
		if (character_CanPerceive(team->partitionIdx, teamMember->memberBE->pChar, targetChar))
			return true;
	}
	
	return false;
}

static AITeamStatusEntry* aiTeamAddLegalTargetInternal(AITeam* team, Entity* target)
{
	AITeamStatusEntry* teamStatus = NULL;
	AITeamStatusEntry* teamMemberStatus;
	Team *teamContainer;
	AITeam* targetTeam;
	int i;
	int partitionIdx = team->partitionIdx;

	if (target->aibase && target->aibase->untargetable)
		return NULL;

	if(GET_REF(target->hCreatorNode))
	{
		// Only add a destructible object if it already exists on the status table
		teamStatus = aiTeamStatusFind(team, target, false, true);

		if(!teamStatus)
			return NULL;
	}

	if(!teamStatus)
		teamStatus = aiTeamStatusFind(team, target, true, true);

	teamStatus->legalTarget = 1;
	teamStatus->timeLastStatusUpdate = ABS_TIME_PARTITION(partitionIdx);

	if(!aiGlobalSettings.dontAutoLegalTargetTeammates)
	{
		teamContainer = team_GetTeam(target);
		if(teamContainer)
		{
			for(i = eaSize(&teamContainer->eaMembers)-1; i >= 0; i--)
			{
				TeamMember* teamMember = teamContainer->eaMembers[i];
				Entity *pMemberEnt = entFromContainerID(partitionIdx, GLOBALTYPE_ENTITYPLAYER, teamMember->iEntID);

				if(pMemberEnt && pMemberEnt->pChar && aiTeamCanMemberPerceiveTarget(team, pMemberEnt->pChar))
				{
					teamMemberStatus = aiTeamStatusFind(team, pMemberEnt, true, true);
					teamMemberStatus->legalTarget = true;
					teamMemberStatus->timeLastStatusUpdate = ABS_TIME_PARTITION(partitionIdx);
				}
			}
		}
	}

	// if called from powers notify, this isn't guaranteed to have a team
	targetTeam = aiTeamGetCombatTeam(target, target->aibase);
	if(targetTeam)
	{
		if(!aiGlobalSettings.enableCombatTeams)
		{
			for(i = eaSize(&targetTeam->members)-1; i >= 0; i--)
			{
				Entity* targetMember = targetTeam->members[i]->memberBE;

				if(aiGlobalSettings.dontAutoLegalTargetTeammates && 
					entIsPlayer(targetMember) && 
					entGetOwner(target)!=targetMember)
				{
					// Dont auto include other players (unless, of course, the player is the owner of the target)
					continue;
				}

				if(targetMember->pChar)
				{
					teamMemberStatus = aiTeamStatusFind(team, targetMember, true, true);
					teamMemberStatus->legalTarget = true;
					teamMemberStatus->timeLastStatusUpdate = ABS_TIME_PARTITION(partitionIdx);
				}
			}
		}
	}
	
	if(team->combatState == AITEAM_COMBAT_STATE_AMBIENT || team->combatState == AITEAM_COMBAT_STATE_STAREDOWN)
	{
		aiAggro_DoInitialPullAggro(team, target);

		// aiTeamSwitchCombatState now checks for whether this should be FIGHT or WAITFORFIGHT
		aiTeamSwitchCombatState(team, AITEAM_COMBAT_STATE_FIGHT, "Add legal target internal");
	}

	return teamStatus;
}

void aiTeamAddLegalTarget(AITeam* team, Entity* target)
{
	aiTeamAddLegalTargetInternal(team, target);

	if(team->reinforceTeam && team->reinforceTeam->combatState == AITEAM_COMBAT_STATE_FIGHT && team->reinforced)
	{
		aiTeamAddLegalTargetInternal(team->reinforceTeam, target);
	}
}

int aiTeamIsTargetLegalTarget(AITeam* team, Entity *target)
{
	EntityRef erTarget = entGetRef(target);

	FOR_EACH_IN_EARRAY(team->statusTable, AITeamStatusEntry, entry)
	{
		if (entry->entRef == erTarget)
			return true;
	}
	FOR_EACH_END

	return false;
}

void aiTeamCopyTeamSettingsToCombatTeam(AITeam* team, AITeam *combatTeam)
{
	combatTeam->roamingLeash = team->roamingLeash;
	copyVec3(team->roamingLeashPoint, combatTeam->roamingLeashPoint);
	combatTeam->roamingLeashPointValid = team->roamingLeashPointValid;
}

static __forceinline int aiTeamIsValidLeader(AITeam *team, Entity *e)
{
	// Apparently, as soon as I get a combat team, I'm no longer a valid leader for my regular team.  Not sure what that does.
	return e && aiIsEntAlive(e) && ((e->aibase->team==team && !e->aibase->combatTeam) || team == e->aibase->combatTeam);
}

Entity* aiTeamGetLeader(AITeam *team)
{
	int i;
	Entity *curLeader, *pNewLeader = NULL;
	if(!team || !eaSize(&team->members))
		return NULL;

	curLeader = entFromEntityRef(team->partitionIdx, team->teamLeaderRef);

	if (curLeader && curLeader->pPlayer)
	{
		// Don't do any of these checks for player teams
		return curLeader;
	}

	if(aiTeamIsValidLeader(team, curLeader))
		return curLeader;

	for(i=0; i<eaSize(&team->members); i++)
	{
		Entity* e = team->members[i]->memberBE;
		if(aiTeamIsValidLeader(team, e))
		{
			pNewLeader = e;
			break;
		}
	}

	// Use the current leader if we have to, even if he's not valid
	if(curLeader && pNewLeader == NULL)
		pNewLeader = curLeader;

	// If there's no appropriate leader, and the current leader is gone, fall back to the first guy in the list
	if (pNewLeader == NULL)
		pNewLeader = team->members[0]->memberBE;

	team->teamLeaderRef = entGetRef(pNewLeader);
	if (team->pTeamFormation)
	{
		team->pTeamFormation->erLeader = team->teamLeaderRef;
	}
	return pNewLeader;
}


// given the member to heal, pick out the best healer for him
typedef struct AITeamAssignmentOutput
{
	AITeamMember	*assignee;
	Power			*targetPower;
} AITeamAssignmentOutput;



// Checks if a member has any assignments assigned to them
static int aiTeamIsMemberAssigned(SA_PARAM_NN_VALID AITeam* team, SA_PARAM_NN_VALID const AITeamMember *member)
{
	// check if we are already assigned a heal target
	FOR_EACH_IN_EARRAY_FORWARDS(team->healAssignments, AITeamMemberAssignment, pAssign)
	{
		if (pAssign->assignee == member)
			return true;
	}
	FOR_EACH_END

	return false;
}

static int aiTeamGetBestHealerForTarget(SA_PARAM_NN_VALID AITeam* team, AITeamMemberAssignment *pAssignment)
{
	F32 healerRating = -FLT_MAX;
	U32 aiPowerTag = kPowerAITag_Heal;
	AITeamMember *assignee = NULL;
	Power *targetPower = NULL;

	switch(pAssignment->type)
	{
		case AITEAM_ASSIGNMENT_TYPE_HEAL:
			aiPowerTag = kPowerAITag_Heal;
		xcase AITEAM_ASSIGNMENT_TYPE_SHIELD_HEAL:
			aiPowerTag = kPowerAITag_Shield_Heal;
		xcase AITEAM_ASSIGNMENT_TYPE_RESSURECT:
			aiPowerTag = kPowerAITag_Resurrect;
		xdefault:
			devassert(0);
	}
	

	FOR_EACH_IN_EARRAY(team->members, AITeamMember, pMember)
		if(aiIsEntAlive(pMember->memberBE) && !entIsPlayer(pMember->memberBE))
		{
			AIPowerRateOutput powerRating = {0};
			F32 curRating = -1.f;
			AIVarsBase *aib = pMember->memberBE->aibase;
			
			switch(pAssignment->type)
			{
				case AITEAM_ASSIGNMENT_TYPE_HEAL:
					if(!aib->powers->hasHealPowers)
						continue;
				xcase AITEAM_ASSIGNMENT_TYPE_SHIELD_HEAL:
					if(!aib->powers->hasShieldHealPowers)
						continue;
				xcase AITEAM_ASSIGNMENT_TYPE_RESSURECT:
					if(!aib->powers->hasResPowers)
						continue;
			}

			if (aiTeamIsMemberAssigned(team, pMember))
				continue;
			
			if (!aiPowersGetBestPowerForTarget(pMember->memberBE, pAssignment->target->memberBE, 
												aiPowerTag, pAssignment->forcedAssignment, &powerRating))
				continue;
			
			// what other heuristics for the rating?
			curRating = powerRating.rating;

			if (curRating > healerRating)
			{
				healerRating = curRating;
				assignee = pMember;
				targetPower = powerRating.targetPower->power;
			}
		}
	FOR_EACH_END

	pAssignment->assignee = assignee;
	if (targetPower)
	{
		pAssignment->powID = targetPower->uiID;
	}
	else
	{
		pAssignment->powID = 0;
	}
	
	
	return pAssignment->assignee != NULL;
}



static int aiTeamGetCurerForTarget(SA_PARAM_NN_VALID AITeam* team, AITeamMemberAssignment *pAssignment)
{	
	AttribMod *pMod;
	AttribModDef *pModDef;
	Entity *target = pAssignment->target->memberBE;

	// find the AttribMod
	if (!aiIsEntAlive(target))
	{
		pAssignment->validAssignment = false;
		return false;
	}
	
	pMod = modarray_Find(&target->pChar->modArray, pAssignment->pAttribModDef, 0, pAssignment->erSource);
	pModDef = mod_GetDef(pMod);
	if (!pMod || !pModDef)
	{
		pAssignment->validAssignment = false;
		return false;
	}

	
	pAssignment->validAssignment = true;
	
	FOR_EACH_IN_EARRAY(team->members, AITeamMember, pMember)
		if(aiIsEntAlive(pMember->memberBE) && !entIsPlayer(pMember->memberBE))
		{
			AIVarsBase *aib = pMember->memberBE->aibase;
			AIPowerInfo *pPower;

			if (!aib->powers->hasCurePower)
				continue;
			if (aiTeamIsMemberAssigned(team, pMember))
				continue;
			
			pPower = aiPowersGetCurePowerForAttribMod(pMember->memberBE, aib, target, pMod, pModDef);
			if (pPower)
			{
				pAssignment->assignee = pMember;
				if (pPower->power)
					pAssignment->powID = pPower->power->uiID;
				return true;
			}
		}
	FOR_EACH_END
	
	return false;
}

static int teamAssignmentsSort(const AITeamMemberAssignment **left, const AITeamMemberAssignment **right)
{
	return (*left)->importanceHeur > (*right)->importanceHeur ? -1 : 1;
}

static void aiTeamUpdateAssignments(SA_PARAM_NN_VALID AITeam *team)
{
	bool invalidAssignment = false;
	int partitionIdx = team->partitionIdx;
	
	// go through our assignments and remove any of the ones that no longer valid
	{
		S32 i;

		for(i = eaSize(&team->healAssignments) - 1; i >= 0; i--)
		{
			AITeamMemberAssignment *pAssignment = team->healAssignments[i];

			if (! pAssignment->validAssignment && !pAssignment->forcedAssignment)
			{
				aiTeamMemberAssignmentDestroy(pAssignment);
				eaRemove(&team->healAssignments, i);
				continue;
			}

			pAssignment->validAssignment = false;
		}
	}
	
	// sort the healing list by those who need it most!
	eaQSort(team->healAssignments, teamAssignmentsSort);

	FOR_EACH_IN_EARRAY_FORWARDS(team->healAssignments, AITeamMemberAssignment, pAssign)
	{
		devassert(pAssign->type > AITEAM_ASSIGNMENT_TYPE_NULL && pAssign->type < AITEAM_ASSIGNMENT_TYPE_COUNT);

		if (ABS_TIME_SINCE_PARTITION(partitionIdx, pAssign->target->timeLastActedOn[pAssign->type]) < SEC_TO_ABS_TIME(3))
		{
			pAssign->assignee = NULL;
			pAssign->powID = 0;
			pAssign->validAssignment = false;
			continue;
		}

		// we have an assignee already, waiting for him to pick up the assignment, 
		// if enough time has passed try looking for another person to pick it up
		if (pAssign->assignee && !ABS_TIME_PASSED_PARTITION(partitionIdx, pAssign->assignedTime, 3))
			continue;

		switch (pAssign->type)
		{
			case AITEAM_ASSIGNMENT_TYPE_HEAL:
			case AITEAM_ASSIGNMENT_TYPE_SHIELD_HEAL:
			case AITEAM_ASSIGNMENT_TYPE_RESSURECT:
				if (aiTeamGetBestHealerForTarget(team, pAssign))
				{
					pAssign->assignedTime = ABS_TIME_PARTITION(partitionIdx);
				}

			xcase AITEAM_ASSIGNMENT_TYPE_CURE:
				if (aiTeamGetCurerForTarget(team, pAssign))
				{
					pAssign->assignedTime = ABS_TIME_PARTITION(partitionIdx);
				}
		}
	}
	FOR_EACH_END



}

static AITeamMemberAssignment* aiTeamFindAssignment(const AITeam* team,  const AITeamMember* member, 
													AITeamAssignmentType type)
{
	FOR_EACH_IN_EARRAY(team->healAssignments, AITeamMemberAssignment, pAssign)
		if(pAssign->target == member && pAssign->type == type)
			return pAssign;
	FOR_EACH_END
	
	return NULL;
}

static AITeamMemberAssignment* aiTeamFindAssignmentCure(const AITeam* team,  const AITeamMember* member, 
														AttribModDef* pAttribModDef, EntityRef erSource)
{
	FOR_EACH_IN_EARRAY(team->healAssignments, AITeamMemberAssignment, pAssign)
		if(pAssign->target == member && pAssign->type == AITEAM_ASSIGNMENT_TYPE_CURE && 
			pAssign->pAttribModDef == pAttribModDef && pAssign->erSource == erSource)
			return pAssign;
	FOR_EACH_END

	return NULL;
}

static void aiTeamReportAssignment(AITeam *team, AITeamMember *member, F32 pointPercent, 
								   AITeamAssignmentType assignmentType, F32 weight)
{
	int partitionIdx = team->partitionIdx;
	// cures should not be using this function 
	devassert(assignmentType != AITEAM_ASSIGNMENT_TYPE_CURE); 
	devassert(assignmentType > AITEAM_ASSIGNMENT_TYPE_NULL && assignmentType < AITEAM_ASSIGNMENT_TYPE_COUNT);

	if (ABS_TIME_SINCE_PARTITION(partitionIdx, member->timeLastActedOn[assignmentType]) < ABS_TIME_TO_SEC(3))
		return; // too soon since they received this last heal type

	{
		AITeamMemberAssignment *pAssignment;
		pAssignment = aiTeamFindAssignment(team, member, assignmentType);

		if (! pAssignment)
		{	// we don't have a healing assignment for this guy
			pAssignment = aiTeamMemberAssignmentCreate(member, assignmentType);
			eaPush(&team->healAssignments, pAssignment);
		}
		else
		{
			pAssignment->validAssignment = true;
		}

		pAssignment->importanceHeur = 1.f - pointPercent;
		if (entIsPlayer(member->memberBE))
			pAssignment->importanceHeur += 0.1f;

		pAssignment->importanceHeur *= weight;
	}
}

void aiTeamRequestResurrectForMember(AITeam *team, Entity *e)
{
	if (! aiIsEntAlive(e) && team && team->powInfo.hasResPowers)
	{
		AITeamMember *member = aiTeamFindMemberByEntity(team, e);
		
		if (member)
		{
			AITeamMemberAssignment *pAssignment;
			aiTeamReportAssignment(team, member, 0.f, AITEAM_ASSIGNMENT_TYPE_RESSURECT, 10.f);
			pAssignment = aiTeamFindAssignment(team, member, AITEAM_ASSIGNMENT_TYPE_RESSURECT);
			if (pAssignment)
			{
				pAssignment->forcedAssignment = true;
			}
		}
	}
}

AITeamMemberAssignment* aiTeamGetAssignmentForMember(SA_PARAM_NN_VALID AITeam* team, Entity* e)
{
	// go through the healing assignments and see if we have any healing assignment for 
	FOR_EACH_IN_EARRAY(team->healAssignments, AITeamMemberAssignment, pAssign)
		if (pAssign->assignee && pAssign->assignee->memberBE == e)
			return pAssign;
	FOR_EACH_END

	return NULL;
}


AITeamMember* aiTeamFindMemberByEntity(SA_PARAM_NN_VALID AITeam* team, Entity* e)
{
	FOR_EACH_IN_EARRAY(team->members, AITeamMember, pMember)
		if(pMember->memberBE == e)
		{
			return pMember;
		}
	FOR_EACH_END

	return NULL;
}

void aiTeamNotifyNewAttribMod(AITeam* team, Entity* e, AttribMod* mod, AttribModDef* moddef)
{
	S32 i;
	AITeamMember* pMember;
	bool bCheckForCure = false;
	if(!team->powInfo.hasCurePower)
		return;	

	pMember = aiTeamFindMemberByEntity(team, e);
	if (!pMember)
		return;
	
	// go through all the curetags that we have on the team and 
	for(i = eaiSize(&team->powInfo.eaCureTags) - 1; i >= 0; i--)
	{
		if(powertags_Check(&moddef->tags, team->powInfo.eaCureTags[i]))
		{
			bCheckForCure = true;
			break;
		}
	}

	if (bCheckForCure)
	{
		AITeamMemberAssignment *pAssignment;

		pAssignment = aiTeamFindAssignmentCure(team, pMember, moddef, mod->erSource);
		if (!pAssignment)
		{
			pAssignment = aiTeamMemberAssignmentCreate(pMember, AITEAM_ASSIGNMENT_TYPE_CURE);
			#define CURE_HEURISTIC 0.75f
			pAssignment->importanceHeur = CURE_HEURISTIC;

			pAssignment->pAttribModDef = moddef;
			pAssignment->erSource = mod->erSource;
			
			eaPush(&team->healAssignments, pAssignment);
		}
	}
}
