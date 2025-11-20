#include "Entity.h"
#include "MapDescription.h"
#include "Team.h"
#include "earray.h"
#include "EString.h"
#include "Player.h"
#include "Guild.h"
#include "chatCommonStructs.h"
#include "MapTransferCommon.h"
#include "GlobalTypes.h"

#define GUILD_WEIGHT 1.0f
#define FRIEND_WEIGHT 1.0f

void TransferCommon_AddTeamMembersGuildMembersAndFriends(PossibleMapChoices *pChoices, Entity *pEntity)
{
	S32 i;

	if(!pEntity || !pEntity->pPlayer)
	{
		return;
	}

	//team members
	// First mark all softfull instances as NotALegalChoice
	FOR_EACH_IN_EARRAY(pChoices->ppChoices, PossibleMapChoice, pChoice)
		if(CHOICE_IS_SPECIFIC(pChoice) && pChoice->bIsSoftFull)
		{
			pChoice->bNotALegalChoice = true;
		}
	FOR_EACH_END;

	// Then count how many teammates are there, and make the softfull choices Legal again
	if (team_IsMember(pEntity))
	{
		Team *pTeam = GET_REF(pEntity->pTeam->hTeam);
		if(pTeam)
		{
			for(i = 0; i < eaSize(&pTeam->eaMembers); ++i)
			{
				if(pTeam->eaMembers[i]->iEntID == pEntity->myContainerID)
					continue;

				FOR_EACH_IN_EARRAY(pChoices->ppChoices, PossibleMapChoice, pChoice)
				{

					if (CHOICE_IS_SPECIFIC(pChoice))
					{
						if(stricmp(pChoice->baseMapDescription.mapDescription, pTeam->eaMembers[i]->pcMapName) == 0 &&
							(U32)pChoice->baseMapDescription.mapInstanceIndex == pTeam->eaMembers[i]->iMapInstanceNumber)
						{
							++pChoice->iNumTeamMembersThere;
							if(pChoice->bIsSoftFull)
							{
								// If it's only soft-full and you have a teammate there, it becomes a legal choice.
								pChoice->bNotALegalChoice = false;
							}

							if ( pTeam->eaMembers[i]->iEntID == pTeam->pLeader->iEntID )
							{
								pChoice->bYourTeamLeaderIsThere = true;
							}
						}
					}
				}
				FOR_EACH_END;
			}
		}
	}
	
	// guild members
	if(guild_IsMember(pEntity))
	{
		Guild *pGuild = GET_REF(pEntity->pPlayer->pGuild->hGuild);
		if(pGuild)
		{
			for(i = 0; i < eaSize(&pGuild->eaMembers); ++i)
			{
				if(pGuild->eaMembers[i]->iEntID == pEntity->myContainerID || !pGuild->eaMembers[i]->bOnline)
					continue;
				
				FOR_EACH_IN_EARRAY(pChoices->ppChoices, PossibleMapChoice, pChoice)
				{

					if (CHOICE_IS_SPECIFIC(pChoice))
					{
						if(stricmp(pChoice->baseMapDescription.mapDescription, pGuild->eaMembers[i]->pcMapName) == 0 &&
							(U32)pChoice->baseMapDescription.mapInstanceIndex == pGuild->eaMembers[i]->iMapInstanceNumber)
						{
							++pChoice->iNumGuildMembersThere;
						}
					}
				}
				FOR_EACH_END
			}
		}
	}
	
	// friends
	if(pEntity->pPlayer->pUI->pChatState)
	{
		ChatState *pChatState = pEntity->pPlayer->pUI->pChatState;
		for(i = 0; i < eaSize(&pChatState->eaFriends); ++i)
		{
			if(pChatState->eaFriends[i]->online_status != USERSTATUS_ONLINE)
				continue;
			
			FOR_EACH_IN_EARRAY(pChoices->ppChoices, PossibleMapChoice, pChoice)
			{

				if (CHOICE_IS_SPECIFIC(pChoice))
				{
					if(stricmp(pChoice->baseMapDescription.mapDescription, pChatState->eaFriends[i]->pPlayerInfo.playerMap.pchMapName) == 0 &&
						pChoice->baseMapDescription.mapInstanceIndex == pChatState->eaFriends[i]->pPlayerInfo.playerMap.iMapInstance)
					{
						++pChoice->iNumFriendsThere;
					}
				}
			}
			FOR_EACH_END;
		}
	}
}


int TransferCommon_PossibleMapChoiceSort(const PossibleMapChoice **ppMapA, const PossibleMapChoice **ppMapB)
{
	const PossibleMapChoice *pMapA = *ppMapA;
	const PossibleMapChoice *pMapB = *ppMapB;

	// Invalid choices go to the bottom
	if (pMapA->bNotALegalChoice && !pMapB->bNotALegalChoice)
		return 1;
	else if (!pMapA->bNotALegalChoice && pMapB->bNotALegalChoice)
		return -1;

	// Team leader goes first
	if (pMapA->bYourTeamLeaderIsThere)
		return -1;
	else if (pMapB->bYourTeamLeaderIsThere)
		return 1;

	// Teammates go second
	if (pMapA->iNumTeamMembersThere > pMapB->iNumTeamMembersThere)
		return -1;
	else if (pMapB->iNumTeamMembersThere > pMapA->iNumTeamMembersThere)
		return 1;

	if ( gConf.bWeightMapTransferLists )
	{
		// Weight guildmates and friends to determine best map
		F32 fWSA = pMapA->iNumGuildMembersThere * GUILD_WEIGHT + pMapA->iNumFriendsThere * FRIEND_WEIGHT;
		F32 fWSB = pMapB->iNumGuildMembersThere * GUILD_WEIGHT + pMapB->iNumFriendsThere * FRIEND_WEIGHT;

		if ( fWSA > fWSB )
			return -1;
		if ( fWSB > fWSA )
			return 1;
	}
	else
	{
		// Guildmates third
		if (pMapA->iNumGuildMembersThere > pMapB->iNumGuildMembersThere)
			return -1;
		else if (pMapB->iNumGuildMembersThere > pMapA->iNumGuildMembersThere)
			return 1;

		// friends 4th
		if (pMapA->iNumFriendsThere > pMapB->iNumFriendsThere)
			return -1;
		else if (pMapB->iNumFriendsThere > pMapA->iNumFriendsThere)
			return 1;
	}

	// The last instance you were on is better than a different one
	if (pMapA->bLastMap)
		return -1;
	else if (pMapB->bLastMap)
		return 1;
	
	// Never default to creating a new map
	if (pMapA->bNewMap)
		return 1;
	else if (pMapB->bNewMap)
		return -1;

	// Otherwise, choose the map with the most players, in an attempt to keep players clustered in fewer more populated instances so that the less populated instances will go away
	return pMapB->iNumPlayers - pMapA->iNumPlayers;
}


void TransferCommon_RemoveNonSpecificChoices(SA_PARAM_NN_VALID PossibleMapChoice *** peaPossibleMapChoices)
{
	int i;

	for (i = eaSize(peaPossibleMapChoices) - 1; i >= 0; i--)
	{
		switch ((*peaPossibleMapChoices)[i]->eChoiceType)
		{
		case MAPCHOICETYPE_SPECIFIED_ONLY:
		case MAPCHOICETYPE_SPECIFIED_OR_BEST_FIT:
			break;
		default:
			StructDestroy(parse_PossibleMapChoice, (*peaPossibleMapChoices)[i]);
			eaRemove(peaPossibleMapChoices, i);
		}
	}
}


SA_RET_OP_VALID PossibleMapChoice * TransferCommon_GetBestChoiceBasedOnTeamMembersEtc(PossibleMapChoices *pChoices, Entity *pEntity)
{
	PERFINFO_AUTO_START_FUNC();

	if (!eaSize(&pChoices->ppChoices))
	{
		PERFINFO_AUTO_STOP();
		return NULL;
	}


	TransferCommon_RemoveNonSpecificChoices(&pChoices->ppChoices);

	if (!eaSize(&pChoices->ppChoices))
	{
		PERFINFO_AUTO_STOP();
		return NULL;
	}

	TransferCommon_AddTeamMembersGuildMembersAndFriends(pChoices, pEntity);

	eaQSort(pChoices->ppChoices, TransferCommon_PossibleMapChoiceSort);
	
	PERFINFO_AUTO_STOP();

	return pChoices->ppChoices[0];
}

PossibleMapChoice *TransferCommon_ChooseOnlySpecificChoiceIfTeamIsThere(PossibleMapChoices *pChoices, Entity *pEntity)
{
	TransferCommon_AddTeamMembersGuildMembersAndFriends(pChoices, pEntity);

	FOR_EACH_IN_EARRAY(pChoices->ppChoices, PossibleMapChoice, pChoice)
	{
		if (CHOICE_IS_SPECIFIC(pChoice))
		{
			if (pChoice->iNumTeamMembersThere)
			{
				return pChoice;
			}
		}
	}
	FOR_EACH_END;

	return NULL;
}