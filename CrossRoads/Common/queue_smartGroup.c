/***************************************************************************
*     Copyright (c) 2005-2006, Cryptic Studios
*     All Rights Reserved
*     Confidential Property of Cryptic Studios
***************************************************************************/

#include "queue_smartGroup.h"

#include "queue_common.h"
#include "queue_common_structs.h"

#include "character.h"
#include "CharacterClass.h"
#include "Entity.h"
#include "OfficerCommon.h"
#include "ResourceManager.h"
#include "StringCache.h"
#include "utilitiesLib.h"


// Used in queue_common. Possibly used on many server types
void NNO_queue_EntFillJoinCriteria(Entity *pEntity, QueueMemberJoinCriteria* pMemberJoinCriteria)
{
	if (pEntity!=NULL && pMemberJoinCriteria!=NULL)
	{
		if (pEntity && pEntity->pChar)
		{
			CharacterClass *pClass = GET_REF(pEntity->pChar->hClass);
			if (pClass && pClass->pchName)
			{
				if (stricmp(pClass->pchName, "Player_Guardian")==0)
				{
					pMemberJoinCriteria->iGroupRole = 1;
					pMemberJoinCriteria->iGroupClass = 1;
				}
				else if (stricmp(pClass->pchName, "Player_Devoted")==0)
				{
					pMemberJoinCriteria->iGroupRole = 2;
					pMemberJoinCriteria->iGroupClass = 2;
				}
				else if (stricmp(pClass->pchName, "Player_Greatweapon")==0)
				{
					pMemberJoinCriteria->iGroupRole = 3;	// dps
					pMemberJoinCriteria->iGroupClass = 3;
				}
				else if (stricmp(pClass->pchName, "Player_Controller")==0)
				{
					pMemberJoinCriteria->iGroupRole = 3;	// dps
					pMemberJoinCriteria->iGroupClass = 4;
				}
				else if (stricmp(pClass->pchName, "Player_Trickster")==0)
				{
					pMemberJoinCriteria->iGroupRole = 3;	// dps
					pMemberJoinCriteria->iGroupClass = 5;
				}
				else
				{
					pMemberJoinCriteria->iGroupRole = 3;   // dps
					pMemberJoinCriteria->iGroupClass = 6;  // essentially 'other'
				}
			}
		}
	}
}



#if defined(APPSERVER)

// App ServerOnly. Used in aslQueueServer.c

// Someone could change all of this to eArrays if they wished

static int NNO_aslQueue_CountArrayContainsItem(int *aIntArray, int iArraySize, int iValue)
{
	int i;
	int iCount=0;
	for (i=0;i<iArraySize;i++)
	{
		if (aIntArray[i]==iValue)
		{
			iCount++;
		}
	}
	return(iCount);
}

// Returns false if we're trying to add classes in a particular role where it will make too many of that class.
// We can't just add up the total members because it's okay if one side already has over the limit (we allow people to
//   queue outside the limits and must accept that as valid)
// Class zero is always ignored for testing (it cannot cause a check to fail)
static bool NNO_aslQueue_CheckGroupClassBalance(int iRoleToTest, int iMaxAllowable,
											int iIncomingMembers,
											int *aiIncomingRoles,
											int *aiIncomingClasses,
											int iExistingMembers,
											int *aiExistingRoles,
											int *aiExistingClasses)
											
{

	// We assume the existing team is already considered okay. So we don't need to check the existing classes against the incoming.
	// Just make sure for every incoming class, our adding in doesn't put us over.

	// This algorithm is inefficient because it will check a class multiple times. Someone can add a 'already been tested' thing later.
	
	int j;
	for (j=0;j<iIncomingMembers;j++)
	{
		if (aiIncomingRoles[j]==iRoleToTest && aiIncomingClasses[j]!=0)
		{
			// Find how many of this class we're trying to add. Cap at the MaxAllowable because an incoming team is allowed to have too many internally
			//   as long as the existing team doesn't have any.
			int iNumThisClassIncoming = NNO_aslQueue_CountArrayContainsItem(aiIncomingClasses,iIncomingMembers, aiIncomingClasses[j]);
			if (iNumThisClassIncoming > iMaxAllowable)
			{
				iNumThisClassIncoming = iMaxAllowable;
			}
			// See if adding this many puts us over.
			if (iNumThisClassIncoming + NNO_aslQueue_CountArrayContainsItem(aiExistingClasses,iExistingMembers, aiIncomingClasses[j]) > iMaxAllowable)
			{
				return(false);
			}
		}
	}
	return(true);
}


// Pvp Specific smart groups
bool NNO_aslQueue_PvPIsSmartGroupToAddTo(QueueDef* pQueueDef, QueueGroup* pGroup, int iGroupMaxSize, QueueMember* pMember, int iTeamSize,
														QueueMember ***peaReadyMembers)
{
	int iIncomingMembers=0;
	int aiIncomingRoles[5];
	int iExistingMembers=0;
	int aiIncomingClasses[5];
	int aiExistingRoles[5];
	int aiExistingClasses[5];
	int i;

	// For PvP we only care about not overpopulating

	// Kind of lame that we're duplicating the PvE set-up code. But this allows the most flexibility.
	// If we get excited about making this really nice, or just complicated, we can pull out common sections later.

	if (iGroupMaxSize != 5)
	{
		// Group is not equal to 5. Don't balance. 
		return(true);
	}

	if (iTeamSize==5)
	{
		// Incoming Queued as a team. It's okay no matter what
		return(true);
	}

	// Store roles and classes on existing.
	for (i=0;i<eaSize(&pGroup->eaMembers);i++)
	{
		aiExistingRoles[iExistingMembers] = pGroup->eaMembers[i]->iGroupRole;
		aiExistingClasses[iExistingMembers] = pGroup->eaMembers[i]->iGroupClass;
		iExistingMembers++;
	}

	if (iExistingMembers<=0)
	{
		// Nothing in the group yet. We're fine no matter what.
		return(true);
	}

	// Store roles and classes on incoming
	if (iTeamSize == 1)
	{
		aiIncomingRoles[iIncomingMembers] = pMember->iGroupRole;
		aiIncomingClasses[iIncomingMembers] = pMember->iGroupClass;
		iIncomingMembers++;
	}
	else
	{
		// We need to get the roles and classes of the rest of the team. 
		S32 iMemberIdx, iNumMembers = eaSize(peaReadyMembers);
		for (iMemberIdx = 0; iMemberIdx < iNumMembers; iMemberIdx++)
		{
			QueueMember *pTeamMember = eaGet(peaReadyMembers, iMemberIdx);
			if (pTeamMember->iTeamID == pMember->iTeamID)
			{
				// This will include pMember
				if (iIncomingMembers < 5)
				{
					aiIncomingRoles[iIncomingMembers] = pTeamMember->iGroupRole;
					aiIncomingClasses[iIncomingMembers] = pTeamMember->iGroupClass;
				}
				iIncomingMembers++;
			}
		}
	}

	if (iIncomingMembers > 5 || iIncomingMembers != iTeamSize)
	{
		// Something bad happened. We had a team of 6??? Or the purported team size doesn't match how many are in the queue
		return(false);
	}

	// At this point we know both Incoming and Existing have members.

	// Make sure we're not overpopulating Role1, Role2, or Role 3 classes.
	// For each member of existing, incoming, check that there aren't already 2 of the same class in the other.

	// Yes, this is not at all as efficient as it could be

	// Role 1
	if (!NNO_aslQueue_CheckGroupClassBalance(1, 2,
											iIncomingMembers, aiIncomingRoles, aiIncomingClasses,
										   iExistingMembers, aiExistingRoles, aiExistingClasses))
	{
		return(false);
	}
	
	// Role 2
	if (!NNO_aslQueue_CheckGroupClassBalance(2, 2,
											iIncomingMembers, aiIncomingRoles, aiIncomingClasses,
										   iExistingMembers, aiExistingRoles, aiExistingClasses))
	{
		return(false);
	}
	
	// Role 3
	if (!NNO_aslQueue_CheckGroupClassBalance(3, 2,
											iIncomingMembers, aiIncomingRoles, aiIncomingClasses,
										   iExistingMembers, aiExistingRoles, aiExistingClasses))
	{
		return(false);
	}

	return(true);
}




/// This function is solely to determine if it makes sense to add pMember (and its team) to the existing pGroup from a class/role balance perspective.
//  It returns either true or false if it thinks it's a good idea or not.  Used in aslQueueServer.c

bool NNO_aslQueue_IsSmartGroupToAddTo(QueueDef* pQueueDef, QueueGroup* pGroup, int iGroupMaxSize, QueueMember* pMember, int iTeamSize,
														QueueMember ***peaReadyMembers)
{
	// iTeamSize may be 1 even if we are on a team, if splitTeams is on.

	// Roles can be 0, 1, 2, or 3. Zero means we don't care/don't know
	// Classes are arbitrary, zero means we don't care/don't know, though.
	int iIncomingMembers=0;
	int aiIncomingRoles[5];
	int iExistingMembers=0;
	int aiIncomingClasses[5];
	int aiExistingRoles[5];
	int aiExistingClasses[5];
	int i;
	bool bIncomingHasRole1=false;
	bool bIncomingHasRole2=false;
	bool bExistingHasRole1=false;
	bool bExistingHasRole2=false;


	// How to distinguish PvP/PvE currently.
	if (pQueueDef->MapRules.QGameRules.publicRules.eGameType!=kPVPGameType_None)
	{
		return(NNO_aslQueue_PvPIsSmartGroupToAddTo(pQueueDef, pGroup, iGroupMaxSize, pMember, iTeamSize, peaReadyMembers));
	}
	
	if (iGroupMaxSize != 5)
	{
		// Group is not equal to 5. Don't balance. 
		return(true);
	}

	if (iTeamSize==5)
	{
		// Incoming Queued as a team. It's okay no matter what
		return(true);
	}

	// Store roles and classes on existing.
	for (i=0;i<eaSize(&pGroup->eaMembers);i++)
	{
		aiExistingRoles[iExistingMembers] = pGroup->eaMembers[i]->iGroupRole;
		aiExistingClasses[iExistingMembers] = pGroup->eaMembers[i]->iGroupClass;
		iExistingMembers++;
	}

	if (iExistingMembers<=0)
	{
		// Nothing in the group yet. We're fine no matter what.
		return(true);
	}

	// Store roles and classes on incoming
	if (iTeamSize == 1)
	{
		aiIncomingRoles[iIncomingMembers] = pMember->iGroupRole;
		aiIncomingClasses[iIncomingMembers] = pMember->iGroupClass;
		iIncomingMembers++;
	}
	else
	{
		// We need to get the roles and classes of the rest of the team. 
		S32 iMemberIdx, iNumMembers = eaSize(peaReadyMembers);
		for (iMemberIdx = 0; iMemberIdx < iNumMembers; iMemberIdx++)
		{
			QueueMember *pTeamMember = eaGet(peaReadyMembers, iMemberIdx);
			if (pTeamMember->iTeamID == pMember->iTeamID)
			{
				// This will include pMember
				if (iIncomingMembers < 5)
				{
					aiIncomingRoles[iIncomingMembers] = pTeamMember->iGroupRole;
					aiIncomingClasses[iIncomingMembers] = pTeamMember->iGroupClass;
				}
				iIncomingMembers++;
			}
		}
	}

	if (iIncomingMembers > 5 || iIncomingMembers != iTeamSize)
	{
		// Something bad happened. We had a team of 6??? Or the purported team size doesn't match how many are in the queue
		return(false);
	}


	// See if we have Role1 Role2.
	bExistingHasRole1=(NNO_aslQueue_CountArrayContainsItem(aiExistingRoles,iExistingMembers,1)>0);
	bExistingHasRole2=(NNO_aslQueue_CountArrayContainsItem(aiExistingRoles,iExistingMembers,2)>0);
	bIncomingHasRole1=(NNO_aslQueue_CountArrayContainsItem(aiIncomingRoles,iIncomingMembers,1)>0);
	bIncomingHasRole2=(NNO_aslQueue_CountArrayContainsItem(aiIncomingRoles,iIncomingMembers,2)>0);

	// At this point we know both Incoming and Existing have members.

	// Make sure we are not adding a Role 1 to Role 1, or a Role 2 to a Role 2.
	if ((bExistingHasRole1 && bIncomingHasRole1) || (bExistingHasRole2 && bIncomingHasRole2))
	{
		// Don't duplicate tanks/healers unless someone teamed with more than one.
		return(false);
	}

	// Make sure we're not overpopulating Role3 classes. For each member of existing, incoming, check that there aren't already 2 of the same class in the other.
	if (!NNO_aslQueue_CheckGroupClassBalance(3, 2,
											iIncomingMembers, aiIncomingRoles, aiIncomingClasses,
										   iExistingMembers, aiExistingRoles, aiExistingClasses))
	{
		return(false);
	}

	// We now know we aren't overduplicating any roles/classes.
	// Check for Role1/Role2 completeness. We want there to be a Role1 and Role2 to exist. Or for there to be room to add one later.

	if (iIncomingMembers + iExistingMembers <= 3)
	{
		// We will have room no matter what. We're good.
		return(true);
	}

	if (iIncomingMembers + iExistingMembers == 4)
	{
		// We have to have at least one Role1 or one Role2.
		if (bIncomingHasRole1 || bExistingHasRole1 || bIncomingHasRole2 || bExistingHasRole2)
		{
			return(true);
		}
	}

	// Must be 5. Checking for full team.

	// Special cases for 4 existing or 4 incoming where the group of 4 has no Role1. In that case we only care that the other side has a Role1.
	//   We relax the restriction that we need a Role2. (This will result in 4 dps and 1 tank and no healer).
	if (iExistingMembers==4 && !bExistingHasRole1)
	{
		return(bIncomingHasRole1);
	}
	if (iIncomingMembers==4 && !bIncomingHasRole1)
	{
		return(bExistingHasRole1);
	}

	// Okay. Now make sure we have a Role1 AND a Role2

	if ((bIncomingHasRole1 || bExistingHasRole1) && (bIncomingHasRole2 || bExistingHasRole2))
	{
		return(true);
	}

	return(false);
}


#endif
