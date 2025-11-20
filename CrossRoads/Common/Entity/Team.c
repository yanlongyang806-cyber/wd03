/***************************************************************************
*     Copyright (c) 2005-2006, Cryptic Studios
*     All Rights Reserved
*     Confidential Property of Cryptic Studios
***************************************************************************/

#include "EntityLib.h"
#include "Team.h"
#include "EString.h"
#include "chatCommon.h"
#include "utilitiesLib.h"
#include "ResourceManager.h"
#include "AutoTransDefs.h"
#include "TeamPetsCommonStructs.h"
#include "entCritter.h"
#include "AutoGen/Team_h_ast.h"

#include "interaction_common.h"
#include "interaction_common_h_ast.h"
#include "Entity.h"
#include "EntitySavedData.h"

AUTO_RUN_ANON(memBudgetAddMapping(__FILE__, BUDGET_GameSystems););

AUTO_FIXUPFUNC;
TextParserResult team_fixup(Team *pTeam, enumTextParserFixupType eFixupType, void *pExtraData)
{
	switch (eFixupType)
	{
	case FIXUPTYPE_DESTRUCTOR:
		{
			// Clean up the non-persist stuff that is added to subscribed copies
			eaDestroy(&pTeam->eaRevealedInteractables);
			//Clean up the cached door destination if its still around
			StructDestroySafe(parse_CachedDoorDestination, &pTeam->pCachedDestination);
		}
	}

	return true;
}

bool team_IsTeamLeader(const Entity *pEnt)
{
	const Team *pTeam = team_GetTeam(pEnt);
	if (pTeam) {
		return pTeam->pLeader && entGetContainerID(pEnt) == pTeam->pLeader->iEntID;
	}
	return false;
}

bool team_IsTeamSpokesman(const Entity *pEnt)
{
	const Team *pTeam = team_GetTeam(pEnt);

	if (pTeam)
	{
		if (pTeam->iTeamSpokesmanEntID == 0)
			return entGetContainerID(pEnt) == pTeam->pLeader->iEntID;
		else
			return entGetContainerID(pEnt) == pTeam->iTeamSpokesmanEntID;
	}

	return false;
}

bool team_IsTeamSpokesmanBySelfTeam(const Entity *pEntSelf, const Entity *pEntToCheck)
{
	const Team *pTeam = team_GetTeam(pEntSelf);

	if (pTeam && pEntToCheck)
	{
		if (pTeam->iTeamSpokesmanEntID == 0)
			return entGetContainerID(pEntToCheck) == pTeam->pLeader->iEntID;
		else
			return entGetContainerID(pEntToCheck) == pTeam->iTeamSpokesmanEntID;
	}

	return false;
}

AUTO_TRANS_HELPER;
bool team_trh_IsTeamChampionID(U32 uiEntID, ATH_ARG NOCONST(Team) *pTeam)
{
	if (NONNULL(pTeam)) {
		if (NONNULL(pTeam->pChampion) && pTeam->pChampion->iEntID) {
			return uiEntID == pTeam->pChampion->iEntID;
		} else if (NONNULL(pTeam->pLeader)) {
			return uiEntID == pTeam->pLeader->iEntID;
		}
	}
	return false;
}

TeamMember *team_FindMemberID(const Team *pTeam, U32 iEntID)
{
	S32 i;
	if (!pTeam) {
		return NULL;
	}
	
	for (i = eaSize(&pTeam->eaMembers)-1; i >= 0; i--) {
		if (pTeam->eaMembers[i]->iEntID == iEntID) {
			return pTeam->eaMembers[i];
		}
	}
	
	return NULL;
}

StubTeamMember *team_FindDisconnectedStubMemberID(const Team *pTeam, U32 iEntID)
{
	S32 i;
	if (!pTeam) {
		return NULL;
	}
	
	for (i = eaSize(&pTeam->eaDisconnecteds)-1; i >= 0; i--) {
		if (pTeam->eaDisconnecteds[i]->iEntID == iEntID) {
			return pTeam->eaDisconnecteds[i];
		}
	}
	return NULL;
}

StubTeamMember *team_FindDisconnectedStubMemberAccountAndName(const Team *pTeam, const char* pcAccount, const char* pcCharName)
{
	S32 i;
	if (!pTeam) {
		return NULL;
	}
	
	for (i = eaSize(&pTeam->eaDisconnecteds)-1; i >= 0; i--)
	{
		if (stricmp(pTeam->eaDisconnecteds[i]->pcName,pcCharName)==0 &&
			stricmp(pTeam->eaDisconnecteds[i]->pcAccountHandle,pcAccount)==0)
		{
			return pTeam->eaDisconnecteds[i];
		}
	}
	return NULL;
}



TeamMember *team_FindChampion(const Team *pTeam)
{
	S32 i;
	if (pTeam) {
		for (i = eaSize(&pTeam->eaMembers)-1; i >= 0; i--) {
			TeamMember* pMember = pTeam->eaMembers[i];
			if (team_IsTeamChampionID(pMember->iEntID, pTeam)) {
				return pMember;
			}
		}
	}
	return NULL;
}

// Checks to see if a pet is owned by the player or any member on the player's team
bool team_IsPetOwnedByPlayerOrTeam(SA_PARAM_OP_VALID Entity *pPlayer, SA_PARAM_OP_VALID Entity *pPet)
{
	// return quickly for the 99% case
	if (pPet == NULL || pPet->erOwner == 0)
	{
		return false;
	}

	if (pPlayer &&
		(entGetType(pPet) == GLOBALTYPE_ENTITYCRITTER || 
		 entGetType(pPet) == GLOBALTYPE_ENTITYSAVEDPET))
	{
		Team *pTeam;
		if (pPet->erOwner == entGetRef(pPlayer))
		{
			return true;
		}
		if (pTeam = team_GetTeam(pPlayer))
		{
			int iPartitionIdx = entGetPartitionIdx(pPlayer);
			FOR_EACH_IN_EARRAY_FORWARDS(pTeam->eaMembers, TeamMember, pTeamMember)
			{
				if (pTeamMember && pTeamMember->iEntID != entGetContainerID(pPlayer))
				{
					Entity *pTeamMemberEnt = entFromContainerID(iPartitionIdx, GLOBALTYPE_ENTITYPLAYER, pTeamMember->iEntID);
					if (pTeamMemberEnt && entGetRef(pTeamMemberEnt) == pPet->erOwner)
					{
						return true;
					}
				}
			}
			FOR_EACH_END
		}
	}
	return false;
}

// If pTeamMate is a pet, this function checks to see if the pet is owned by a team member
bool team_IsPlayerOrPetOnTeam(SA_PARAM_OP_VALID Entity *pPlayer, SA_PARAM_OP_VALID Entity *pTeamMate)
{
	if (team_OnSameTeam(pPlayer, pTeamMate) || team_IsPetOwnedByPlayerOrTeam(pPlayer, pTeamMate))
	{
		return true;
	}
	return false;
}

AUTO_RUN_LATE;
int RegisterTeamContainer(void)
{
	objRegisterNativeSchema(GLOBALTYPE_TEAM, parse_Team, NULL, NULL, NULL, NULL, NULL);
	return 1;
}

void team_GetOnMapEntRefs(int iPartitionIdx, EntityRef **res_refs, Team *team)
{
    int i;
    if(!res_refs || !team)
        return;
    
    ea32ClearFast(res_refs);
    for(i = eaSize(&team->eaMembers)-1; i >= 0; --i)
    {
        TeamMember *m = team->eaMembers[i];
        Entity *e;
        if(!m)
            continue;
        e = entFromContainerID(iPartitionIdx,GLOBALTYPE_ENTITYPLAYER,m->iEntID);
        if(!e)
            continue;
        ea32Push(res_refs,e->myRef);
    }
}

void team_GetOnMapEntIds(int iPartitionIdx, int **res_ids, Team *team)
{
    int i;
    if(!res_ids || !team)
        return;
    
    ea32ClearFast(res_ids);
    for(i = eaSize(&team->eaMembers)-1; i >= 0; --i)
    {
        TeamMember *m = team->eaMembers[i];
        Entity *e;
        if(!m)
            continue;
        e = entFromContainerID(iPartitionIdx,GLOBALTYPE_ENTITYPLAYER,m->iEntID);
        if(!e)
            continue;
        ea32Push(res_ids,e->myContainerID);
    }
}

// Get team members on same map and in same partition
void team_GetOnMapEntsUnique(int iPartitionIdx, Entity ***peaEnts, Team *team, bool IncludePets)
{
    int i, j;
    if(!peaEnts || !team)
        return;
    
    for(i = eaSize(&team->eaMembers)-1; i >= 0; --i)
	{
		TeamMember *m = team->eaMembers[i];
		Entity *e;
		S32 iOwnedPetsSize;
		if(!m)
			continue;
		e = entFromContainerID(iPartitionIdx,GLOBALTYPE_ENTITYPLAYER,m->iEntID);

		if(e){
			eaPushUnique(peaEnts,e);
			if (IncludePets && e->pSaved)
			{
				iOwnedPetsSize = ea32Size(&e->pSaved->ppAwayTeamPetID);

				for ( j = 0; j < iOwnedPetsSize; j++ )
				{
					Entity *pPetEnt = entFromContainerID(iPartitionIdx,GLOBALTYPE_ENTITYSAVEDPET,e->pSaved->ppAwayTeamPetID[j]);
					if (pPetEnt)
					{
						eaPushUnique(peaEnts,pPetEnt);
					}
				}

				// Add the critter pets
				FOR_EACH_IN_EARRAY_FORWARDS(e->pSaved->ppCritterPets, CritterPetRelationship, pRelationship)
				{
					if (pRelationship->pEntity) 
					{
						eaPushUnique(peaEnts, pRelationship->pEntity);
					}
				}
				FOR_EACH_END
			}
		}
    }
}

static int team_GetOnMapPetList(int iPartitionIdx, Entity* pEnt, Entity*** peaTeamEnts, TeamMember*** peaTeamMembers)
{
	int iCount = 0;
	if (pEnt->pSaved)
	{
		int i;
		int iOwnedPetsSize = ea32Size(&pEnt->pSaved->ppAwayTeamPetID);
		int iCritterPetsSize = eaSize(&pEnt->pSaved->ppCritterPets);
		for (i = 0; i < iOwnedPetsSize; i++)
		{
			Entity* pPetEnt = entFromContainerID(iPartitionIdx,GLOBALTYPE_ENTITYSAVEDPET,pEnt->pSaved->ppAwayTeamPetID[i]);
			if (pPetEnt)
			{
				eaPush(peaTeamEnts, pPetEnt);
				if (peaTeamMembers)
					eaPush(peaTeamMembers, NULL);
				iCount++;
			}
		}
		for (i = 0; i < iCritterPetsSize; i++)
		{
			Entity* pCritEnt = entFromEntityRef(iPartitionIdx,pEnt->pSaved->ppCritterPets[i]->erPet);
			if (pCritEnt)
			{
				eaPush(peaTeamEnts, pCritEnt);
				if (peaTeamMembers)
					eaPush(peaTeamMembers, NULL);
				iCount++;
			}
		}
	}
	return iCount;
}

// Gets all team ents, and optionally pets. Prioritizes self first. Returns the number of entities added to peaTeamEnts.
int team_GetTeamListSelfFirst(Entity* pEnt, Entity*** peaTeamEnts, TeamMember*** peaTeamMembers, bool bIncludeSelf, bool bIncludePets)
{
	Team* pTeam;
	int i, iPartitionIdx;
	int iCount = 0;

	if (!pEnt || !peaTeamEnts)
		return 0;

	iPartitionIdx = entGetPartitionIdx(pEnt);
	pTeam = team_GetTeam(pEnt);
	if (bIncludeSelf)
	{
		eaPush(peaTeamEnts, pEnt);
		
		if (peaTeamMembers)
		{
			if (pTeam && pEnt->pTeam->eState == TeamState_Member)
			{
				for (i = eaSize(&pTeam->eaMembers)-1; i >= 0; i--)
				{
					if (pTeam->eaMembers[i]->iEntID == entGetContainerID(pEnt))
					{
						eaPush(peaTeamMembers, pTeam->eaMembers[i]);
						break;
					}
				}
				if (i < 0)
				{
					eaPush(peaTeamMembers, NULL);
				}
			}
			else
			{
				eaPush(peaTeamMembers, NULL);
			}
		}
		iCount++;
	}
	if (bIncludePets)
	{
		iCount += team_GetOnMapPetList(iPartitionIdx, pEnt, peaTeamEnts, peaTeamMembers);
	}

	if (pTeam && pEnt->pTeam->eState == TeamState_Member)
	{
		int iTeamSize = eaSize(&pTeam->eaMembers);
		for (i = 0; i < iTeamSize; i++)
		{
			TeamMember* pTeamMember = pTeam->eaMembers[i];
			if (pTeamMember->iEntID != entGetContainerID(pEnt))
			{
				Entity* pTeamEnt = entFromContainerID(iPartitionIdx, GLOBALTYPE_ENTITYPLAYER, pTeamMember->iEntID);
				if (!pTeamEnt) 
				{
					pTeamEnt = GET_REF(pTeamMember->hEnt);
				}
				if (pTeamEnt)
				{
					eaPush(peaTeamEnts, pTeamEnt);
					if (peaTeamMembers)
						eaPush(peaTeamMembers, pTeamMember);
					iCount++;

					if (bIncludePets)
					{
						iCount += team_GetOnMapPetList(iPartitionIdx, pTeamEnt, peaTeamEnts, peaTeamMembers);
					}
				}
			}
		}
	}
	return iCount;
}

// Gets all StubTeamMembers on the team that pEnt is on. Returns the number of such members added to peaTeamEnts.
int team_GetTeamStubMembers(Entity* pEnt, StubTeamMember*** peaStubTeamMembers)
{
	Team* pTeam;
	int i, iPartitionIdx;
	int iCount = 0;

	if (!pEnt || !peaStubTeamMembers)
		return 0;

	iPartitionIdx = entGetPartitionIdx(pEnt);
	pTeam = team_GetTeam(pEnt);

	if (pTeam)
	{
		int iDisconnectedsSize = eaSize(&pTeam->eaDisconnecteds);
		for (i = 0; i < iDisconnectedsSize; i++)
		{
			StubTeamMember* pStubTeamMember = pTeam->eaDisconnecteds[i];
			if (peaStubTeamMembers)
			{
				eaPush(peaStubTeamMembers, pStubTeamMember);
				iCount++;
			}
		}
	}
	return iCount;
}



bool team_IsAwayTeamLeader( Entity* pEntity, AwayTeamMembers* pAwayTeamMembers )
{
	if ( pEntity==NULL || pAwayTeamMembers==NULL )
		return false;

	if ( !team_IsMember( pEntity ) )
		return true;
	
	if ( !team_IsTeamLeader( pEntity ) )
	{
		S32 i, iAwayTeamSize = eaSize(&pAwayTeamMembers->eaMembers);
		bool bFound = false;
		int iPartitionIdx = entGetPartitionIdx(pEntity);

		for ( i = 0; i < iAwayTeamSize; i++ )
		{
			Entity* pMemberEnt;
			AwayTeamMember* pMember = pAwayTeamMembers->eaMembers[i];

			if ( pMember->eEntType != GLOBALTYPE_ENTITYPLAYER )
				continue;

			if ( pMember->iEntID == pEntity->myContainerID )
			{
				if ( i > 0 )
					return false;
				
				bFound = true;
				continue;
			}

			pMemberEnt = entFromContainerID( iPartitionIdx, GLOBALTYPE_ENTITYPLAYER, pMember->iEntID );

			if ( team_IsTeamLeader(pMemberEnt) )
				return false;
		}

		return bFound;
	}

	return true;
}

ExprFuncReturnVal exprFuncPlayerGetTeamEntsHelper(Entity* pEntity, ACMD_EXPR_ENTARRAY_OUT eaEntsOut, ACMD_EXPR_ERRSTRING errString)
{
	int i;
	Team *pTeam;

	if(!pEntity)
	{
		estrPrintf(errString, "Cannot call PlayerGetTeamEnts without a valid Entity");
		return ExprFuncReturnError;
	}

	if(!pEntity->pPlayer)
	{
		estrPrintf(errString, "Cannot call PlayerGetTeamEnts on a non Player");
		return ExprFuncReturnError;
	}

	pTeam = pEntity->pTeam ? GET_REF(pEntity->pTeam->hTeam) : NULL;
	if(!pTeam)
	{
		eaPush(eaEntsOut, pEntity);
		return ExprFuncReturnFinished;
	}

	for(i=eaSize(&pTeam->eaMembers)-1; i>=0; i--)
	{
		Entity* pTeamEnt = GET_REF(pTeam->eaMembers[i]->hEnt);

		if(pTeamEnt)
		{
			eaPush(eaEntsOut, pTeamEnt);
		}
	}

	return ExprFuncReturnFinished;
}

AUTO_EXPR_FUNC(Player) ACMD_NAME(PlayerGetTeamEnts);
ExprFuncReturnVal exprFuncPlayerGetTeamEnts(ACMD_EXPR_ENTARRAY_OUT eaEntsOut, SA_PARAM_OP_VALID Entity* pEntity, ACMD_EXPR_ERRSTRING errString)
{
	return exprFuncPlayerGetTeamEntsHelper(pEntity, eaEntsOut, errString);
}

//
// Return true if the two players are on factions that are compatible for teaming purposes
//
bool
team_OnCompatibleFaction(Entity *playerEnt, Entity *targetEnt)
{
	CritterFaction *playerFaction;
	CritterFaction *targetFaction;

	if ( ( playerEnt == NULL ) || ( targetEnt == NULL ) )
	{
		return false;
	}

	playerFaction = entGetFaction(playerEnt);
	targetFaction = entGetFaction(targetEnt);

	if ( ( playerFaction == NULL ) || ( playerFaction->pchName == NULL ) || ( targetFaction == NULL ) || ( targetFaction->pchName == NULL ) )
	{
		// can't find one of the factions or a faction is unnamed.
		return false;
	}

	// faction names are pooled strings, so just compare pointers
	if ( playerFaction->pchName != targetFaction->pchName )
	{
		if ( gConf.bAllowSuballegianceTeaming && ( playerFaction->bCanBeSubFaction || targetFaction->bCanBeSubFaction ) )
		{
			CritterFaction *playerSubFaction = entGetSubFaction(playerEnt);
			CritterFaction *targetSubFaction = entGetSubFaction(targetEnt);

			if ( ( playerSubFaction && playerSubFaction->pchName && playerSubFaction->pchName == targetFaction->pchName ) ||
				 ( targetSubFaction && targetSubFaction->pchName && targetSubFaction->pchName == playerFaction->pchName ) )
			{
				return true;
			}
		}

		return false;
	}

	return true;
}

//
// Return true if the target player can be invited to the source player's team.
// Checks each teammate for compatibility with the target player.
//
bool
team_TeamOnCompatibleFaction(Entity *playerEnt, Entity *targetEnt)
{
	Team *team = team_GetTeam(playerEnt);
	bool compatible = true;
	int i;

	if (!team)
	{
		return team_OnCompatibleFaction(playerEnt, targetEnt);
	}

	for (i = 0; i < eaSize(&team->eaMembers); i++)
	{
		// Only check players. We don't care about pet compatibility
		TeamMember *member = team->eaMembers[i];
		Entity *teamEnt = entFromContainerIDAnyPartition(GLOBALTYPE_ENTITYPLAYER, member->iEntID);
		if (teamEnt)
		{
			compatible = team_OnCompatibleFaction(teamEnt, targetEnt);
		}

		if (!compatible)
			break;
	}

	return compatible;
}

#if GAMECLIENT
bool team_IsTeamMemberTalking(SA_PARAM_NN_VALID TeamMember *pTeamMember)
{
#if _XBOX
	U32 iTicksElapsed;
	U32 iTicksNow;

	// Verify input
	assert(pTeamMember);

	// Get the current tick count
	iTicksNow = GetTickCount();

	// Did this member ever talk?
	if (pTeamMember->iLastVoicePacketRcvTime == 0)
	{
		return false;
	}

	iTicksElapsed = iTicksNow - pTeamMember->iLastVoicePacketRcvTime;

	if (iTicksElapsed >= 0 && iTicksElapsed <= 250)
	{
		return true;
	}
	return false;
#else
	return false;
#endif
}
#endif


#ifdef GAMESERVER 
Team *gslTeam_GetTeam(ContainerID iTeamID)
{
	Container *pContainer = objGetContainer(GLOBALTYPE_TEAM, iTeamID);
	if (pContainer) {
		return (Team *)pContainer->containerData;
	} else {
		return NULL;
	}
}
#endif

///////////////////////////////////////////////////

// Members on the team who are not disconnected	
int team_NumPresentMembers(const Team *pTeam)
{
	if (pTeam)
	{
		return(eaSize(&pTeam->eaMembers));
	}
	return(0);
}

// Members on the team including disconnecteds
int team_NumTotalMembers(const Team *pTeam)
{
	if (pTeam)
	{
		return(eaSize(&pTeam->eaMembers) + eaSize(&pTeam->eaDisconnecteds));
	}
	return(0);
}

// Members on this particular partition on this server. Do not include disconnecteds
//  "This Server" is implicit in fetching the entity from the container ID. 
int team_NumMembersThisServerAndPartition(const Team *pTeam, int iPartitionIdx)	
{
	int iNumMembers=0;
	int i;
	if (pTeam)
	{
		//Get active players for that team
		for(i = 0; i<eaSize(&pTeam->eaMembers); i++)
		{
			Entity *pTeamMember = entFromContainerID(iPartitionIdx,GLOBALTYPE_ENTITYPLAYER,pTeam->eaMembers[i]->iEntID);

			if(pTeamMember)
			{
				iNumMembers++;
			}
		}
	}
	return(iNumMembers);
}


#include "AutoGen/Team_h_ast.c"
