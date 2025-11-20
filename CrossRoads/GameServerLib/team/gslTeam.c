#include "gslTeam.h"
//GSLTEAM includes above this

//Other includes here (Sorted pseudo-alphabetically)
#include "EntityLib.h"
#include "GlobalTypes_h_ast.h" //for IntEarrayWrapper
#include "gslPartition.h"
#include "gslQueue.h"
#include "qsortG.h"
#include "queue_common.h"
#include "queue_common_structs.h"
#include "stdtypes.h"
#include "Team.h"
#include "Team_h_ast.h"
#include "WorldGrid.h"

#include "AutoGen/AppServerLib_autogen_RemoteFuncs.h"
#include "AutoGen/gslQueue_h_ast.h"
#include "AutoGen/gslTeam_c_ast.h"

#define QTEAM_START (1<<12)
#define LOCAL_TEAM_REQUEST_TIMEOUT 30


//Only works for the teamserver
AUTO_COMMAND_REMOTE ACMD_IFDEF(APPSERVER);
void gslTeam_FindOwnedTeam(U32 iTeamID)
{
	LocalTeam *pLocalTeam = gslQueue_FindLocalTeamByID(iTeamID);

	RemoteCommand_aslTeam_OwnedTeamAcknowledge(GLOBALTYPE_TEAMSERVER, 0, iTeamID, pLocalTeam != NULL);
}

AUTO_COMMAND_REMOTE ACMD_IFDEF(APPSERVER);
void gslTeam_NotifyOwnedTeamDestroyed(U32 iTeamID)
{
	LocalTeam *pLocalTeam = gslQueue_FindLocalTeamByID(iTeamID);
	if (pLocalTeam)
	{
		pLocalTeam->iTeamID = 0;
	}
}

AUTO_COMMAND_REMOTE ACMD_IFDEF(APPSERVER);
void gslTeam_CreateOwnedCallback(U32 iPartitionID, U32 iTeamID, U32 iLocalID)
{
	S32 iPartitionIdx = partition_IdxFromID(iPartitionID);
	QueuePartitionInfo* pInfo = gslQueuePartitionInfoFromIdx(iPartitionIdx);
	if (pInfo)
	{
		LocalTeam* pLocalTeam = eaIndexedGetUsingInt(&pInfo->ppLocalTeams, iLocalID);

		if (pLocalTeam)
		{
			pLocalTeam->uNextTeamUpdateRequestTime = 0;	// We can request stuff again

			// We don't have a team yet. This is it. Assign it
			if (!pLocalTeam->iTeamID)
			{
				char buffer[32];
	
				pLocalTeam->iTeamID = iTeamID;
	
				sprintf(buffer, "%d", iTeamID);
				SET_HANDLE_FROM_STRING(GlobalTypeToCopyDictionaryName(GLOBALTYPE_TEAM), buffer, pLocalTeam->hTeam);
				pLocalTeam->uLastUpdateRequestID = 0;
			}
			else if (pLocalTeam->iTeamID != iTeamID)
			{
				// The local team already has a team container assigned to it. Destroy the new one.
				RemoteCommand_aslTeam_DestroyWithReason(GLOBALTYPE_TEAMSERVER, 0, iTeamID, "Local team not found or invalid during create");
			}
		}
		else 
		{
			// Our local team went away. We don't need a Team. Destroy the new one.
			RemoteCommand_aslTeam_DestroyWithReason(GLOBALTYPE_TEAMSERVER, 0, iTeamID, "Local team not found or invalid during create");
		}
	}
}

static LocalTeam* gslTeam_CreateLocalTeam(QueuePartitionInfo *pInfo, QueueGroup* pGroup)
{
	LocalTeam *pLocalTeam = StructCreate(parse_LocalTeam);	
	pInfo->uMaxLocalTeamID++;
	pLocalTeam->iLocalID = QTEAM_START + pInfo->uMaxLocalTeamID;
	pLocalTeam->iGroupIndex = SAFE_MEMBER(pGroup, iGroupIndex);
	eaIndexedEnable(&pInfo->ppLocalTeams, parse_LocalTeam);
	eaPush(&pInfo->ppLocalTeams, pLocalTeam);
	return pLocalTeam;
}

static LocalTeam* gslTeam_FindLocalTeamByGroupIndex(QueuePartitionInfo *pInfo, S32 iGroupIdx)
{
	FOR_EACH_IN_EARRAY(pInfo->ppLocalTeams, LocalTeam, pLocalTeam)
	{
		if (iGroupIdx == pLocalTeam->iGroupIndex)
			return pLocalTeam;
	}
	FOR_EACH_END

	return NULL;
}


static void gslTeam_AddEntLocalTeamPending(QueuePartitionInfo* pInfo, U32 iMemberID)
{
	EntLocalTeamPending* pPending = eaIndexedGetUsingInt(&pInfo->ppEntsLocalTeamPending, iMemberID);
	if (!pPending)
	{
		pPending = StructCreate(parse_EntLocalTeamPending);
		pPending->iEntID = iMemberID;
		pPending->iTimestamp = timeSecondsSince2000();
		if (!pInfo->ppEntsLocalTeamPending)
			eaIndexedEnable(&pInfo->ppEntsLocalTeamPending, parse_EntLocalTeamPending);
		eaPush(&pInfo->ppEntsLocalTeamPending, pPending);
	}
}


AUTO_STRUCT;
typedef struct PlacementGrouping
{
	U32 iPreviousTeamID; AST(KEY)
	U32* piMemberIDs;
} PlacementGrouping;


static void gslTeam_AddToPendingList(QueuePartitionInfo* pInfo, PlacementGrouping*** peaPlacementGroupsToSetPending, int** ppiMembersToSetPending)
{
	S32 iTeamIdx, iMemberIdx;
	for (iTeamIdx = eaSize(peaPlacementGroupsToSetPending)-1; iTeamIdx >= 0; iTeamIdx--)
	{
		PlacementGrouping* pPlacementGrouping = (*peaPlacementGroupsToSetPending)[iTeamIdx];
		for (iMemberIdx = eaiSize(&pPlacementGrouping->piMemberIDs)-1; iMemberIdx >= 0; iMemberIdx--)
		{
			gslTeam_AddEntLocalTeamPending(pInfo, pPlacementGrouping->piMemberIDs[iMemberIdx]);
		}
		StructDestroy(parse_PlacementGrouping, eaRemove(peaPlacementGroupsToSetPending, iTeamIdx));
	}
	for (iMemberIdx = eaiSize(ppiMembersToSetPending)-1; iMemberIdx >= 0; iMemberIdx--)
	{
		gslTeam_AddEntLocalTeamPending(pInfo, eaiRemove(ppiMembersToSetPending,iMemberIdx));
	}
}


void gslTeam_RemoveMemberFromPendingList(QueuePartitionInfo* pInfo, int iMemberID)
{
	if (iMemberID)
	{
		int i = eaIndexedFindUsingInt(&pInfo->ppEntsLocalTeamPending, iMemberID);
		if (i >= 0)
		{
			StructDestroy(parse_EntLocalTeamPending, eaRemove(&pInfo->ppEntsLocalTeamPending, i));
		}
	}
}

static void gslTeam_RemoveGroupingFromPendingList(QueuePartitionInfo* pInfo, PlacementGrouping* pPlacementGrouping)
{
	if (pPlacementGrouping)
	{
		S32 iMemberIdx;
		for (iMemberIdx = eaiSize(&pPlacementGrouping->piMemberIDs)-1; iMemberIdx >= 0; iMemberIdx--)
		{
			gslTeam_RemoveMemberFromPendingList(pInfo, pPlacementGrouping->piMemberIDs[iMemberIdx]);
		}
	}
}

static S32 gslTeam_CountLocalTeamsForGroup(QueuePartitionInfo *pInfo, QueueGroup* pGroup)
{
	S32 iTeamIdx, iCount = 0;
	for (iTeamIdx = eaSize(&pInfo->ppLocalTeams)-1; iTeamIdx >= 0; iTeamIdx--)
	{
		LocalTeam* pLocalTeam = pInfo->ppLocalTeams[iTeamIdx];
		if (pLocalTeam->iGroupIndex == pGroup->iGroupIndex)
		{
			iCount++;
		}
	}
	return iCount;
}

// Go through the available groupings and individual members available for placement and try to fill this localTeam
//  by adding to the piNewMemberList. (It may already have contents)
static void gslTeam_FillLocalTeam(QueuePartitionInfo* pInfo, LocalTeam* pLocalTeam, PlacementGrouping*** peaPlacementGroupingsToPlace, int** ppiMembersToPlace)
{
	S32 i;
	if (peaPlacementGroupingsToPlace)
	{
		for (i = eaSize(peaPlacementGroupingsToPlace)-1; i >= 0 && eaiSize(&pLocalTeam->piNewMemberList) < TEAM_MAX_SIZE; i--)
		{
			PlacementGrouping* pPlacementGrouping = (*peaPlacementGroupingsToPlace)[i];
			int iLocalTeamSize = eaiSize(&pLocalTeam->piNewMemberList);

			if (iLocalTeamSize + eaiSize(&pPlacementGrouping->piMemberIDs) <= TEAM_MAX_SIZE)
			{
				eaiPushEArray(&pLocalTeam->piNewMemberList, &pPlacementGrouping->piMemberIDs);
				gslTeam_RemoveGroupingFromPendingList(pInfo, pPlacementGrouping);
				StructDestroy(parse_PlacementGrouping, eaRemove(peaPlacementGroupingsToPlace, i));
			}
		}
	}
	if (ppiMembersToPlace)
	{
		for (i = eaiSize(ppiMembersToPlace)-1; i >= 0 && eaiSize(&pLocalTeam->piNewMemberList) < TEAM_MAX_SIZE; i--)
		{
			gslTeam_RemoveMemberFromPendingList(pInfo, (*ppiMembersToPlace)[i]);
			eaiPush(&pLocalTeam->piNewMemberList, eaiRemove(ppiMembersToPlace,i));
		}
	}
}


static void gslTeam_UpdateLocalTeamMembers(QueuePartitionInfo *pInfo)
{
	S32 iTeamIdx, iMemberIdx;
	for (iTeamIdx = eaSize(&pInfo->ppLocalTeams)-1; iTeamIdx >= 0; iTeamIdx--)
	{
		LocalTeam *pLocalTeam = pInfo->ppLocalTeams[iTeamIdx];

		eaiClearFast(&pLocalTeam->piOnlineMembers);		

		for (iMemberIdx = 0; iMemberIdx < eaiSize(&pLocalTeam->piNewMemberList); iMemberIdx++)
		{
			int iMemberID = pLocalTeam->piNewMemberList[iMemberIdx];
			Entity* pEnt = entFromContainerID(pInfo->iPartitionIdx, GLOBALTYPE_ENTITYPLAYER, iMemberID);	
			if (pEnt)
			{
				eaiPush(&pLocalTeam->piOnlineMembers, iMemberID);
			}
		}

		// Copy the NewMemberList (which is now complete) to the piMembers
		eaiCopy(&pLocalTeam->piMembers, &pLocalTeam->piNewMemberList);
		eaiClearFast(&pLocalTeam->piNewMemberList);		
	}
}

// A slightly different auto-team mechanic used to form teams for Champions' "Nemesis Confrontation" Queue.
static void gslTeam_CreateMergedTeamsFromGroups(QueuePartitionInfo *pInfo, bool bUpdateGroups)
{
	QueueMatch *pMatch = pInfo->pMatch;
	S32 iGroupIdx, iNumGroups = eaSize(&pMatch->eaGroups);
	U32 iTeamIdx;
	static INT_EARRAY s_piTeamMembers = NULL;
	//How many teams do I need for this group
	U32 iNumTeams = pMatch->iMatchSize / TEAM_MAX_SIZE 
		+ (((pMatch->iMatchSize % TEAM_MAX_SIZE) != 0) ? 1 : 0);
	int iMemberIdx = 0;

	iGroupIdx = 0;

	if (bUpdateGroups)
	{
		//For each team
		for(iTeamIdx = 0; iTeamIdx < iNumTeams; iTeamIdx++)
		{
			eaiClearFast(&s_piTeamMembers);

			for (;iGroupIdx < iNumGroups; iGroupIdx++, iMemberIdx=0)
			{
				QueueGroup *pGroup = pMatch->eaGroups[iGroupIdx];

				for(;//Nothing
				eaiSize(&s_piTeamMembers)<TEAM_MAX_SIZE && iMemberIdx<eaSize(&pGroup->eaMembers);
					iMemberIdx++)
				{
					QueueMatchMember *pMember = pGroup->eaMembers[iMemberIdx];
					
					eaiPush(&s_piTeamMembers, pGroup->eaMembers[iMemberIdx]->uEntID);
				}

				if(eaiSize(&s_piTeamMembers)>=TEAM_MAX_SIZE)
					break;
			}

			if (eaiSize(&s_piTeamMembers))
			{
				U32 iLocalTeamID = QTEAM_START + iTeamIdx + 1;
				LocalTeam *pLocalTeam = eaIndexedGetUsingInt(&pInfo->ppLocalTeams, iLocalTeamID);
				if (!pLocalTeam)
				{
					pLocalTeam = gslTeam_CreateLocalTeam(pInfo, NULL);
					devassert((U32)pLocalTeam->iLocalID == iLocalTeamID);
				}
				gslTeam_FillLocalTeam(pInfo, pLocalTeam, NULL, &s_piTeamMembers);
			}
		}
	}
	else
	{
		//For each team
		for(iTeamIdx = 0; iTeamIdx < iNumTeams; iTeamIdx++)
		{
			LocalTeam *pLocalTeam = pInfo->ppLocalTeams[iTeamIdx];
			eaiCopy(&pLocalTeam->piNewMemberList, &pLocalTeam->piMembers);
		}

	}
	gslTeam_UpdateLocalTeamMembers(pInfo);
}


static int SortPlacementGrouping(const PlacementGrouping **ppA, const PlacementGrouping **ppB)
{
	const PlacementGrouping *pA = (*ppA);
	const PlacementGrouping *pB = (*ppB);
	int iSizeA = eaiSize(&pA->piMemberIDs);
	int iSizeB = eaiSize(&pB->piMemberIDs);

	if (iSizeA != iSizeB)
		return iSizeA - iSizeB;

	return (int)(pA->iPreviousTeamID - pB->iPreviousTeamID);
}

//The standard auto-team mechanic.  QueueGroups are treated as separate "pools" from which to form teams.
static void gslTeam_CreateSeparateTeamsFromGroups(QueuePartitionInfo *pInfo, bool bUpdateGroups)
{
	QueueMatch *pMatch = pInfo->pMatch;
	S32 iGroupIdx, iNumGroups = eaSize(&pMatch->eaGroups);
	S32 iTeamIdx, iMemberIdx;
	
	if (bUpdateGroups)
	{
		INT_EARRAY piMembersToPlace = NULL;
		PlacementGrouping **eaPlacementGroupsToPlace = NULL;
		
		// Assume cleared out piNewMemberLists. We may add to these piecemeal as we go through the groups.
			
		// Update Groups. Go through each and place or set to pending every member in that group before moving to the next.
		for(iGroupIdx = 0; iGroupIdx < iNumGroups; iGroupIdx++)
		{
			QueueGroup *pGroup = pMatch->eaGroups[iGroupIdx];
		
			eaiClear(&piMembersToPlace);
			eaClearStruct(&eaPlacementGroupsToPlace, parse_PlacementGrouping);

			// Go through each member in the group.
			//  If they are already on a local team associated with this group, make sure they stay on that local team.
			//  by adding to the local teams NewMemberList.
			for (iMemberIdx = eaSize(&pGroup->eaMembers)-1; iMemberIdx >= 0; iMemberIdx--)
			{
				QueueMatchMember *pMember = pGroup->eaMembers[iMemberIdx];
				
				for(iTeamIdx = eaSize(&pInfo->ppLocalTeams)-1; iTeamIdx >= 0; iTeamIdx--)
				{
					LocalTeam *pLocalTeam = pInfo->ppLocalTeams[iTeamIdx];

					if (pLocalTeam->iGroupIndex == pGroup->iGroupIndex)
					{
						int iFoundIdx = eaiFind(&pLocalTeam->piMembers, pMember->uEntID);
						if(iFoundIdx >= 0)
						{
							eaiPush(&pLocalTeam->piNewMemberList, pMember->uEntID);
							break;
						}
					}
				}

				//If the member is already on a local team, then no further processing is required
				if (iTeamIdx >= 0)
					continue;


				// This group member is not on a local team associated with the group.
				//  If they are already on a different team (one that has no local team on any partition)
				//  prepare a new team for them or add them to the eaPlacementGroupsToPlace already created for them
				// Otherwise (they are not on a team or their team already is in 'use' elsewhere.
				//  We are not going to keep them with their team.
				
				
				//Keep track of existing teams so that team members aren't split up
				if (pMember->uTeamID && !gslQueue_FindLocalTeamByID(pMember->uTeamID))
				{
					PlacementGrouping *pGrouping = NULL;
					for (iTeamIdx = eaSize(&eaPlacementGroupsToPlace)-1; iTeamIdx >= 0; iTeamIdx--)
					{
						if (eaPlacementGroupsToPlace[iTeamIdx]->iPreviousTeamID == pMember->uTeamID)
						{
							pGrouping = eaPlacementGroupsToPlace[iTeamIdx];
							break;
						}
					}
					if (!pGrouping)
					{
						pGrouping = StructCreate(parse_PlacementGrouping);
						pGrouping->iPreviousTeamID = pMember->uTeamID;
						eaPush(&eaPlacementGroupsToPlace, pGrouping);
					}
					if (eaiSize(&pGrouping->piMemberIDs) < TEAM_MAX_SIZE)
					{
						eaiPush(&pGrouping->piMemberIDs, pMember->uEntID);
					}
				}
				else
				{
					eaiPush(&piMembersToPlace, pMember->uEntID);
				}
			}

			eaQSort(eaPlacementGroupsToPlace, SortPlacementGrouping);

			// Okay. Now we have a list of loose MemberToPlace and a list PlacementGroups that can be added to teams for this group.

			// Fill existing local teams first. Respect that we may have already have members on the NewMemberList because we want to keep them on that team.
			for(iTeamIdx = 0; iTeamIdx < eaSize(&pInfo->ppLocalTeams); iTeamIdx++)
			{
				LocalTeam *pLocalTeam = pInfo->ppLocalTeams[iTeamIdx];

				if (pLocalTeam->iGroupIndex == pGroup->iGroupIndex)
				{
					gslTeam_FillLocalTeam(pInfo, pLocalTeam, &eaPlacementGroupsToPlace, &piMembersToPlace);
				}
			}

			// Create new local teams for the leftovers and fill them
			while (eaSize(&eaPlacementGroupsToPlace) || eaiSize(&piMembersToPlace))
			{
				// For some reason we were 'sent' more teams, members than fit in this map. Add them to a pending list
				if (pInfo->iMaxLocalTeamsPerGroup && gslTeam_CountLocalTeamsForGroup(pInfo, pGroup) >= pInfo->iMaxLocalTeamsPerGroup)
				{
					gslTeam_AddToPendingList(pInfo, &eaPlacementGroupsToPlace, &piMembersToPlace);
				}
				else
				{
					LocalTeam *pLocalTeam = gslTeam_CreateLocalTeam(pInfo, pGroup);

					gslTeam_FillLocalTeam(pInfo, pLocalTeam, &eaPlacementGroupsToPlace, &piMembersToPlace);
				}
			}
		}

		eaiDestroy(&piMembersToPlace);
		eaDestroyStruct(&eaPlacementGroupsToPlace, parse_PlacementGrouping);
	}
	else
	{
		// No updateGroups. Just use our current members
		for(iTeamIdx = 0; iTeamIdx < eaSize(&pInfo->ppLocalTeams); iTeamIdx++)
		{
			LocalTeam *pLocalTeam = pInfo->ppLocalTeams[iTeamIdx];
			eaiCopy(&pLocalTeam->piNewMemberList, &pLocalTeam->piMembers);
		}
	}

	gslTeam_UpdateLocalTeamMembers(pInfo);  // Destroys piNewMemberList
	
}

static void gslTeam_UpdateFromQueueMatch(QueuePartitionInfo *pInfo)
{
	bool bUpdateGroups = false;
	
	if(!pInfo->pMatch)
		return ;

	bUpdateGroups = pInfo->pMatch->bHasNewGroupData;

	// If there are players that haven't been added to a local team, force the matching logic to run
	if (eaSize(&pInfo->ppEntsLocalTeamPending))
	{
		bUpdateGroups = true;
	}

	if(!pInfo->bMergeGroups)
	{
		gslTeam_CreateSeparateTeamsFromGroups(pInfo, bUpdateGroups);
	}
	//Use for nemesis confrontation in Champs (Split-teams flag on the queue def)
	else
	{
		gslTeam_CreateMergedTeamsFromGroups(pInfo, bUpdateGroups);
	}

	pInfo->pMatch->bHasNewGroupData=false;
}

// Returns true if the members if the pList do not match what's in the pTeam.
static bool gslTeam_TeamServerNeedsUpdate(Team *pTeam, IntEarrayWrapper* pList)
{
	// Check the Send list against the team membership

	if (pTeam==NULL)
	{
		// There is no team to send changes to. We might be waiting for it to be created or resolved? We have an ID but the GETREF failed
		// In any case, we can't update anything.
		return(false);
	}
	else
	{
		int iListIndex;
//		int iMemberIndex;
							
		// Check if members in List not on Team
		for (iListIndex=0;iListIndex<eaiSize(&(pList->eaInts));iListIndex++)
		{
			if (team_FindMemberID(pTeam,(pList->eaInts[iListIndex]))==NULL && team_FindDisconnectedStubMemberID(pTeam,(pList->eaInts[iListIndex]))==NULL)
			{
				return(true);
			}
		}
		
		// Check if members of Team not in List
		if (team_NumTotalMembers(pTeam) != eaiSize(&(pList->eaInts)))
		{
			// We know all the eaInts are in eaMembers. So if the sizes are the same,
			//   all of eaMembers and eaDisconnectedsare in eaInts. (We assume uniqueness)
			return(true);
		}
//		for (iMemberIndex=0;iMemberIndex<eaSize(&pTeam->eaMembers);iMemberIndex++)
//		{
//			int iMemberID = pTeam->eaMembers[iMemberIndex]->iEntID;
//			bool bFound=false;
//								
//			for (iListIndex=0;iListIndex<eaiSize(&(pList->eaInts));iListIndex+=2)
//			{
//				if (pList->eaInts[iListIndex]==iMemberID)
//				{
//					bFound=true;
//					break;
//				}
//			}
//			if (!bFound)
//			{
//				return(true);
//			}
//		}
	}
	return(false);
}


// Check for autoReleasing and Auto-Teamed team.
// We must request the release and continue to update as if we were
// still a regular auto-team. Otherwise we will shutdown the team too
// early or boot people. The object is to keep the team intact, so that
// would be bad.
void gslTeam_CheckForAutoRelease(QueuePartitionInfo *pInfo, LocalTeam *pLocalTeam)
{
	if (mapState_GetScoreboardState(mapState_FromPartitionIdx(pInfo->iPartitionIdx)) == kScoreboardState_Final)
	{
		if (eaiSize(&pLocalTeam->piOnlineMembers) == 0)
		{
			// Also make sure we are still owned, and that we have an ID
			if (pLocalTeam->iTeamID)
			{
				Team *pTeam = GET_REF(pLocalTeam->hTeam);
				if (pTeam!=NULL && pTeam->iGameServerOwnerID != 0)
				{
					RemoteCommand_aslTeam_ReleaseOwned(GLOBALTYPE_TEAMSERVER, 0, pTeam->iContainerID);
				}
			}
		}
	}
}


void gslTeam_AutoTeamUpdate(QueuePartitionInfo *pInfo)
{
	S32 i;
	
	//Update from the queue match (PvP or PvE)
	gslTeam_UpdateFromQueueMatch(pInfo);

	for(i=eaSize(&pInfo->ppLocalTeams)-1; i>=0; i--)
	{	
		LocalTeam *pLocalTeam = pInfo->ppLocalTeams[i];
		bool bUpdated=false;

		if (pInfo->bAutoTeamMembersCanBeOnOtherMap)
		{
			bUpdated = (eaiSize(&pLocalTeam->piMembers) > 0);

			if (g_QueueConfig.bAutoReleaseOnFinishedMapExit)
			{
				// Kind of hacky place to put this. But probably the best place. Bolt-on. We need to auto-release the team from the instance if
				//  the map is finished and everyone is off the map.
				gslTeam_CheckForAutoRelease(pInfo,pLocalTeam);
			}
		}
		else
		{
			bUpdated = (eaiSize(&pLocalTeam->piOnlineMembers) > 0);
		}
		
		//If this team didn't get updated this tick, delete it
		if(!bUpdated)
		{
			//If I made this team...
			if(pLocalTeam->iTeamID)
			{
				// WOLF[28Feb13]:  We run into trouble here if we do an immediate destroy. In an actual team release situation the queue may 
				//   stop sending us information and we would destroy the team even though we want it to go on existing. (see gslTeam_cmd_ReleaseOwned)
				// Theoretically we could ask if the team was still owned, but there's no way of synchronizing the transaction setting the team to unowned
				//   and this information being propagated locally, and the queue server finding out that everyone has abandoned and it stops sending us
				//   group updates. 
				// By doing "release owned" we rely on aslValidateTeams to eventually shut the team down due to no members. It has an up to 40 second delay.
				// Things will go haywire if somehow the localTeam is sent more information in this interim period. (Though that would have even been a
				//   bug previously if such a thing happened between the time the destroy request was made here and when it was actually processed on the
				//   Team Server)
				// Note that the local team will continue to exist until such time as Cleanup is called (only if AutoTeaming gets turned off), or
				//   the partition is destroyed
//				RemoteCommand_aslTeam_DestroyWithReason(GLOBALTYPE_TEAMSERVER, 0, pLocalTeam->iTeamID, "Local team stopped updating");
				RemoteCommand_aslTeam_ReleaseOwned(GLOBALTYPE_TEAMSERVER, 0, pLocalTeam->iTeamID);
				pLocalTeam->iTeamID = 0;
				pLocalTeam->uNextTeamUpdateRequestTime = 0;
			}
		}
		else
		{
			//If it did get updated, then we need to tell the team server

			// We used to try to keep an old team intact at this point. That would fail to make the team 'owned'.
			{
				U32 uCurrentTime = timeSecondsSince2000();
				//Request a new team container ID if I haven't created one yet
				if(!pLocalTeam->iTeamID && uCurrentTime >= pLocalTeam->uNextTeamUpdateRequestTime)
				{
					U32 uPartitionID = partition_IDFromIdx(pInfo->iPartitionIdx);
					pLocalTeam->uNextTeamUpdateRequestTime = uCurrentTime + LOCAL_TEAM_REQUEST_TIMEOUT;

			
					
					RemoteCommand_aslTeam_OwnedTeamCreate(	GLOBALTYPE_TEAMSERVER, 0, 
															objServerID(), 
															pLocalTeam->iLocalID, uPartitionID,
															!pInfo->bAutoTeamMembersCanBeOnOtherMap
															);
				}
				else if(pLocalTeam->iTeamID)
				{
					Team *pTeam = GET_REF(pLocalTeam->hTeam);

					//If this local team had its members list updated, inform the team server of the changes
					//  Ignore disconnecteds

					if (pTeam!=NULL)
					{
						// Otherwise There is no team to send changes to. We might be waiting for it to be created or resolved? We have an ID but the GETREF failed
						// In any case, we can't update anything.

						if (uCurrentTime >= pLocalTeam->uNextTeamUpdateRequestTime || pTeam->uiLocalTeamSyncRequestID >= pLocalTeam->uLastUpdateRequestID)
						{
							// List of members that SHOULD be on the team to send to the TeamServer
							IntEarrayWrapper List = {0};
							int iIdx;
	
							// Okay. We want to construct a list of members to send top the TeamServer, but we only want to send it if we need to.
							//  First distinguish between online and member states.
	
							if (pInfo->bAutoTeamMembersCanBeOnOtherMap)
							{
								// Use Members rather than OnlineMembers. This may also choose disconnected members. That's okay.
	
								for(iIdx=eaiSize(&pLocalTeam->piMembers)-1; iIdx>=0; iIdx--)
								{
									int iMemberID = pLocalTeam->piMembers[iIdx];
									
									eaiPush(&List.eaInts, iMemberID);
								}
							}
							else
							{
								// We only care about online members on this partition. Others (including those not on this map) will not be sent to the TeamServer.
								//   (Not sure about disconnecteds management here. If we disconnect from the map we probably still want them on the team
	
								for(iIdx=eaiSize(&pLocalTeam->piOnlineMembers)-1; iIdx>=0; iIdx--)
								{
									Entity *pEnt = entFromContainerID(pInfo->iPartitionIdx, GLOBALTYPE_ENTITYPLAYER, pLocalTeam->piOnlineMembers[iIdx]);
									if(pEnt)
									{
										eaiPush(&List.eaInts, pLocalTeam->piOnlineMembers[iIdx]);
									}
								}
							}
	
							// Check the Send list against the team membership
							if (gslTeam_TeamServerNeedsUpdate(pTeam, &List))
							{
								//use the next request ID
								if (pTeam->uiLocalTeamSyncRequestID >= pLocalTeam->uLastUpdateRequestID)
									pLocalTeam->uLastUpdateRequestID = pTeam->uiLocalTeamSyncRequestID+1;
	
								RemoteCommand_aslTeam_OwnedTeamUpdate(	GLOBALTYPE_TEAMSERVER, 0, 
																		pLocalTeam->iTeamID, &List, pLocalTeam->uLastUpdateRequestID);
								
								pLocalTeam->uNextTeamUpdateRequestTime = uCurrentTime + 15;
								// Wait at least 15 seconds before requesting changes again, unless our last request was successful (as reflected in the persisted request ID)
							}
							
							StructDeInit(parse_IntEarrayWrapper, &List);
						}
					}
				}
			}		
			//Reset the update flags
			pLocalTeam->bUpdated = false;
		}
	}
}

void gslTeam_CleanupLocalTeams(QueuePartitionInfo *pInfo)
{
	S32 i;
	for(i=eaSize(&pInfo->ppLocalTeams)-1; i>=0; i--)
	{
		LocalTeam *pLocalTeam = pInfo->ppLocalTeams[i];
		//If I made this team...
		if(pLocalTeam->iTeamID)
		{
			// See big comment above. 
//			RemoteCommand_aslTeam_DestroyWithReason(GLOBALTYPE_TEAMSERVER, 0, pLocalTeam->iTeamID, "Cleaning up local team");
			RemoteCommand_aslTeam_ReleaseOwned(GLOBALTYPE_TEAMSERVER, 0, pLocalTeam->iTeamID);
		}
		StructDestroy(parse_LocalTeam, eaRemove(&pInfo->ppLocalTeams, i));
	}
}



#include "AutoGen/gslTeam_c_ast.c"
