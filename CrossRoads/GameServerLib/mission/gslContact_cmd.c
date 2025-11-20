/***************************************************************************
*     Copyright (c) 2006-2007, Cryptic Studios
*     All Rights Reserved
*     Confidential Property of Cryptic Studios
***************************************************************************/

#include "contact_common.h"
#include "Entity.h"
#include "EntityInteraction.h"
#include "EntityLib.h"
#include "GameStringFormat.h"
#include "gslContact.h"
#include "gslInteraction.h"
#include "gslMission.h"
#include "gslWaypoint.h"
#include "interaction_common.h"
#include "mission_common.h"
#include "NameList.h"
#include "Player.h"
#include "PowerTree.h"

#include "AutoGen/GameClientLib_autogen_ClientCmdWrappers.h"
#include "AutoGen/Entity_h_ast.h"

NameList* g_ContactNameList = NULL;

// ----------------------------------------------------------------------------------
// Contact Commands used by contact UI
// ----------------------------------------------------------------------------------

AUTO_COMMAND ACMD_ACCESSLEVEL(0) ACMD_SERVERCMD ACMD_PRIVATE;
void ContactResponse(SA_PARAM_OP_VALID Entity* playerEnt, SA_PARAM_OP_STR const char* responseKey, SA_PARAM_OP_VALID ContactRewardChoices* rewardChoices, U32 iOptionAssistEntID)
{
	if (playerEnt && responseKey)
		contact_InteractResponse(playerEnt, responseKey, rewardChoices, iOptionAssistEntID, false);
}

// Stop talking to the current contact.  Safe to use if there is no current contact.
// Call "ContactDialogEnd" on the client instead of this so that the client can
// validate that the player is actually in a contact dialog.  Otherwise, there are
// some weird edge cases when multiple Contact Dialogs happen close together.
AUTO_COMMAND ACMD_ACCESSLEVEL(0) ACMD_SERVERCMD ACMD_PRIVATE;
void ContactDialogEndServer(Entity* playerEnt)
{
	if(playerEnt)
		interaction_EndInteractionAndDialog(entGetPartitionIdx(playerEnt), playerEnt, false, false, true);
}

// ----------------------------------------------------------------------------------
// Contact Debugging Commands
// ----------------------------------------------------------------------------------

// Create a name list to use for the contact auto commands
AUTO_STARTUP(Contacts);
void CreateContactNameList(void)
{
	g_ContactNameList = CreateNameList_RefDictionary("Contact");
}

// Talk to a contact even if there is no contact critter
AUTO_COMMAND ACMD_ACCESSLEVEL(9) ACMD_NAME("ContactDialog");
void contact_StartDialogCmd(Entity* playerEnt, ACMD_NAMELIST(g_ContactNameList) const char* contactName)
{
	if(playerEnt && contactName)
	{
		ContactDef* contactDef = contact_DefFromName(contactName);

		if(contactDef)
			contact_InteractBegin(playerEnt, NULL, contactDef, NULL, NULL);
	}
}

AUTO_COMMAND ACMD_ACCESSLEVEL(9) ACMD_NAME("ContactDialog2");
void contact_StartDialogCmd2(Entity* playerEnt, ACMD_NAMELIST(g_ContactNameList) const char* contactName, const char* dialogName)
{
	if(playerEnt && contactName)
	{
		ContactDef* contactDef = contact_DefFromName(contactName);

		if(contactDef)
			contact_InteractBegin(playerEnt, NULL, contactDef, dialogName, NULL);
	}
}

AUTO_COMMAND ACMD_ACCESSLEVEL(0) ACMD_NAME("ContactInteractWithLastRecentlyCompletedDialog") ACMD_SERVERCMD ACMD_PRIVATE;
void contact_InteractWithLastRecentlyCompletedDialogCmd(Entity* pEnt)
{
	ContactInfo* pContactInfo = NULL;
	ContactDialogInfo* pDialogInfo = contact_GetLastCompletedDialog(pEnt, &pContactInfo, true);
	if (pDialogInfo)
	{
		ContactDef* pContactDef = GET_REF(pDialogInfo->hContact);
		Entity* pContactEnt = pContactInfo ? entFromEntityRefAnyPartition(pContactInfo->entRef) : NULL;
		if (pContactDef)
		{
			contact_InteractBegin(pEnt, pContactEnt, GET_REF(pDialogInfo->hContact), pDialogInfo->pcDialogName, NULL);
		}
	}
}

AUTO_COMMAND ACMD_NAME(PlayerSetTrackedContact);
char *contact_CmdPlayerSetTrackedContact(Entity *pEnt, const char *pcContactDef)
{
	MissionInfo *pInfo = mission_GetInfoFromPlayer(pEnt);
	if (pInfo) {
		SET_HANDLE_FROM_STRING("ContactDef", pcContactDef, pInfo->hTrackedContact);
	}
	waypoint_UpdateTrackedContactWaypoints(pEnt);
	return NULL;
}


AUTO_COMMAND ACMD_SERVERCMD ACMD_ACCESSLEVEL(0) ACMD_PRIVATE;
void contact_StartRemoteContact(Entity* playerEnt, ACMD_NAMELIST(g_ContactNameList) const char* contactName)
{
	if(playerEnt && contactName)
	{
		ContactDef* contactDef = contact_DefFromName(contactName);

		if (!contact_CanInteract(contactDef, playerEnt)) return;

		if(contactDef) {
			InteractInfo* pInfo = SAFE_MEMBER2(playerEnt, pPlayer, pInteractInfo);
			if(pInfo) {
				// This looks backward, but it's doing the right thing
				// If you pass the canAccessRemotely, then we run the contact as if it's local
				// If you fail that, then the restricted remote interact path runs
				if((!g_ContactConfig.bIncludeMissionSearchResultContactsInRemoteContacts || !contact_ShowInSearchResults(contactDef))
						&& (!contactDef->canAccessRemotely || !contact_Evaluate(playerEnt, contactDef->canAccessRemotely, NULL))) {
					pInfo->bPartialPermissions = true;
				} else {
					pInfo->bPartialPermissions = false;
				}
				pInfo->bRemotelyAccessing = true;
			}
			contact_InteractBegin(playerEnt, NULL, contactDef, NULL, NULL);
		}
	}
}

AUTO_COMMAND ACMD_SERVERCMD ACMD_ACCESSLEVEL(0) ACMD_PRIVATE;
void contact_StartRemoteContactWithOption(Entity* playerEnt, ACMD_NAMELIST(g_ContactNameList) const char* contactName, const char* pchOptionKey)
{
	if(playerEnt && contactName && pchOptionKey)
	{
		ContactDef* contactDef = contact_DefFromName(contactName);
		ContactDialogOption* pInitialOption = NULL;

		if (!contact_CanInteract(contactDef, playerEnt)) return;

		if(contactDef) {
			InteractInfo* pInfo = SAFE_MEMBER2(playerEnt, pPlayer, pInteractInfo);
			if(pInfo) {
				RemoteContact* pRemoteContact = pInfo->eaRemoteContacts ? (RemoteContact*)eaIndexedGetUsingString(&pInfo->eaRemoteContacts, contactName) : NULL;
				RemoteContactOption* pOption = pRemoteContact && pRemoteContact->eaOptions ? (RemoteContactOption*)eaIndexedGetUsingString(&pRemoteContact->eaOptions, pchOptionKey) : NULL;

				if(pOption)
					pInitialOption = pOption->pOption;

				// This looks backward, but it's doing the right thing
				// If you pass the canAccessRemotely, then we run the contact as if it's local
				// If you fail that, then the restricted remote interact path runs
				if((!g_ContactConfig.bIncludeMissionSearchResultContactsInRemoteContacts || !contact_ShowInSearchResults(contactDef))
						&& (!contactDef->canAccessRemotely || !contact_Evaluate(playerEnt, contactDef->canAccessRemotely, NULL))) {
					pInfo->bPartialPermissions = true;
				} else {
					pInfo->bPartialPermissions = false;
				}
				pInfo->bRemotelyAccessing = true;

			}
			if(pInitialOption)
				contact_InteractBeginWithInitialOption(playerEnt, NULL, contactDef, pInitialOption->pData, NULL);
			else
				contact_InteractBegin(playerEnt, NULL, contactDef, NULL, NULL);
		}
	}
}

// FIXME(jm): Move to header file.
extern ContactDialogOption *contact_GetMissionDialogOption(Entity *pEnt, ContactDef *pContact, MissionDef *pMission, bool bCanAccessFullContactRemotely, bool bRemotelyAccessing);

AUTO_COMMAND ACMD_SERVERCMD ACMD_ACCESSLEVEL(0) ACMD_PRIVATE;
void contact_StartRemoteContactWithMission(Entity* playerEnt, ACMD_NAMELIST(g_ContactNameList) const char* contactName, const char* missionName)
{
	if(playerEnt && contactName && missionName)
	{
		ContactDef* contactDef = contact_DefFromName(contactName);
		MissionDef* missionDef = missiondef_FindMissionByName(NULL, missionName);

		if (!contact_CanInteract(contactDef, playerEnt)) return;

		if(contactDef && missionDef) {
			InteractInfo* pInfo = SAFE_MEMBER2(playerEnt, pPlayer, pInteractInfo);
			ContactDialogOption* pInitialOption = NULL;
			if(pInfo) {
				// This looks backward, but it's doing the right thing
				// If you pass the canAccessRemotely, then we run the contact as if it's local
				// If you fail that, then the restricted remote interact path runs
				if((!g_ContactConfig.bIncludeMissionSearchResultContactsInRemoteContacts || !contact_ShowInSearchResults(contactDef))
						&& (!contactDef->canAccessRemotely || !contact_Evaluate(playerEnt, contactDef->canAccessRemotely, NULL))) {
					pInfo->bPartialPermissions = true;
					pInitialOption = contact_GetMissionDialogOption(playerEnt, contactDef, missionDef, true, true);
				} else {
					pInfo->bPartialPermissions = false;
					pInitialOption = contact_GetMissionDialogOption(playerEnt, contactDef, missionDef, false, true);
				}
				pInfo->bRemotelyAccessing = true;
			}
			if(pInitialOption)
				contact_InteractBeginWithInitialOption(playerEnt, NULL, contactDef, pInitialOption->pData, NULL);
			else
				contact_InteractBegin(playerEnt, NULL, contactDef, NULL, NULL);

			StructDestroySafe(parse_ContactDialogOption, &pInitialOption);
		}
	}
}


AUTO_COMMAND ACMD_SERVERCMD ACMD_ACCESSLEVEL(0) ACMD_PRIVATE;
void entRefreshRemoteContactList(Entity* pEnt)
{
	contact_entRefreshRemoteContactList(pEnt);
}

AUTO_COMMAND ACMD_ACCESSLEVEL(0) ACMD_SERVERCMD ACMD_HIDE;
void ContactCreateTrainerFromPlayer(Entity* pPlayerEnt)
{
	if ( pPlayerEnt && powertree_CharacterHasTrainerUnlockNode( pPlayerEnt->pChar, NULL ) )
	{
		contact_CreateTrainerFromEntity( pPlayerEnt, pPlayerEnt );
	}
}

// Hacky, hardcoded command to start the "library computer" contact.  This breaks the dependence
// of the client on an access level 9 command.
AUTO_COMMAND ACMD_ACCESSLEVEL(0) ACMD_HIDE ACMD_NAME("AccessLibraryComputer") ACMD_PRODUCTS(StarTrek);
void Contact_StartLibraryContact(Entity* playerEnt)
{
	contact_StartDialogCmd(playerEnt, "Fed_Library_Computer");
}

// The team member calls this command from the client to let other team members know about the vote
AUTO_COMMAND ACMD_ACCESSLEVEL(0) ACMD_SERVERCMD ACMD_PRIVATE;
void ContactTeamDialogVote(SA_PARAM_OP_VALID Entity* pEnt, SA_PARAM_OP_STR const char* pchResponseKey)
{
	ContactDialog* pDialog = SAFE_MEMBER3(pEnt, pPlayer, pInteractInfo, pContactDialog);

	if (pEnt &&
		team_IsWithTeam(pEnt) &&
		pEnt->pTeam->bInTeamDialog &&
		pDialog &&
		(!pDialog->bHasVoted || g_ContactConfig.bTeamDialogAllowRevote) &&
		pchResponseKey && *pchResponseKey)
	{
		S32 iPartitionIdx = entGetPartitionIdx(pEnt);

		// Let all other team members know about this vote
		Team *pTeam = team_GetTeam(pEnt);

		if (!pTeam)
		{
			return;
		}

		FOR_EACH_IN_EARRAY_FORWARDS(pTeam->eaMembers, TeamMember, pTeamMember)
		{
			Entity *pEntTeamMember = pTeamMember ? entFromContainerID(iPartitionIdx, GLOBALTYPE_ENTITYPLAYER, pTeamMember->iEntID) : NULL;
			if (pEntTeamMember &&
				pEntTeamMember->pPlayer &&
				pEntTeamMember->pPlayer->pInteractInfo &&
				pEntTeamMember->pPlayer->pInteractInfo->pContactDialog &&
				pEntTeamMember->pTeam->bInTeamDialog)
			{
				contactdialog_AddTeamDialogVote(pEntTeamMember, pEnt->myContainerID, pchResponseKey);
			}
		}
		FOR_EACH_END
	}
}


AUTO_COMMAND ACMD_ACCESSLEVEL(0) ACMD_SERVERCMD ACMD_PRIVATE;
void RemoteContactOption_RequestDescription(SA_PARAM_OP_VALID Entity* playerEnt, ACMD_NAMELIST(g_ContactNameList) const char* contactName, const char* pchOptionKey)
{
	if(playerEnt && contactName && pchOptionKey)
	{
		ContactDef* contactDef = contact_DefFromName(contactName);

		if(contactDef) {
			InteractInfo* pInfo = SAFE_MEMBER2(playerEnt, pPlayer, pInteractInfo);
			if(pInfo) {
				RemoteContact* pRemoteContact = pInfo->eaRemoteContacts ? (RemoteContact*)eaIndexedGetUsingString(&pInfo->eaRemoteContacts, contactName) : NULL;
				RemoteContactOption* pOption = pRemoteContact && pRemoteContact->eaOptions ? (RemoteContactOption*)eaIndexedGetUsingString(&pRemoteContact->eaOptions, pchOptionKey) : NULL;
				MissionDef* pMissionDef = pOption ? RefSystem_ReferentFromString(g_MissionDictionary, pOption->pcMissionName) : NULL;
				ContactMissionOffer *pMissionOffer = (contactDef && pMissionDef) ? contact_GetMissionOffer(contactDef, playerEnt, pMissionDef) : NULL;
				WorldVariable **ppMapVars = NULL;
				char* estrDescriptionText1;
				char* estrDescriptionText2;

				eaCreate(&ppMapVars);
				estrCreate(&estrDescriptionText1);
				estrCreate(&estrDescriptionText2);

				if(pMissionDef && pOption->pOption && pOption->pOption->pData)
				{
					switch (pOption->pOption->pData->targetState)
					{
						xcase ContactDialogState_ViewOfferedMission:
						{
							if (GET_REF(pMissionDef->detailStringMsg.hMessage)) {
								if(gConf.bSwapMissionOfferDialogText) {
									langFormatGameDisplayMessage(entGetLanguage(playerEnt), &estrDescriptionText2, &pMissionDef->detailStringMsg, STRFMT_ENTITY(playerEnt), STRFMT_MAPVARS(ppMapVars), STRFMT_END);
								} else {
									langFormatGameDisplayMessage(entGetLanguage(playerEnt), &estrDescriptionText1, &pMissionDef->detailStringMsg, STRFMT_ENTITY(playerEnt), STRFMT_MAPVARS(ppMapVars), STRFMT_END);
								}
							}
							if (GET_REF(pMissionDef->summaryMsg.hMessage)) {
								if(gConf.bSwapMissionOfferDialogText) {
									langFormatGameDisplayMessage(entGetLanguage(playerEnt), &estrDescriptionText1, &pMissionDef->summaryMsg, STRFMT_ENTITY(playerEnt), STRFMT_MAPVARS(ppMapVars), STRFMT_END);
								} else {
									langFormatGameDisplayMessage(entGetLanguage(playerEnt), &estrDescriptionText2, &pMissionDef->summaryMsg, STRFMT_ENTITY(playerEnt), STRFMT_MAPVARS(ppMapVars), STRFMT_END);
								}
							}
						}
						xcase ContactDialogState_ViewInProgressMission:
						{
							if(pMissionOffer){
								contactdialog_SetTextFromDialogBlocks(playerEnt, &pMissionOffer->inProgressDialog, &estrDescriptionText1, NULL, NULL, NULL);
							}else{
								langFormatGameDisplayMessage(entGetLanguage(playerEnt), &estrDescriptionText1, &pMissionDef->summaryMsg, STRFMT_ENTITY(playerEnt), STRFMT_MAPVARS(ppMapVars), STRFMT_END);
							}
						}
						xcase ContactDialogState_ViewFailedMission:
						{
							if(pMissionOffer){
								contactdialog_SetTextFromDialogBlocks(playerEnt, &pMissionOffer->failureDialog, &estrDescriptionText1, NULL, NULL, NULL);
							}
						}
						xcase ContactDialogState_ViewCompleteMission:
						{
							if(pMissionOffer){
								contactdialog_SetTextFromDialogBlocks(playerEnt, &pMissionOffer->completedDialog, &estrDescriptionText1, NULL, NULL, NULL);
							}
							break;
						}
					}
					
					if(estrDescriptionText1 && estrLength(&estrDescriptionText1) > 0)
					{
						pOption->pchDescription1 = StructAllocString(estrDescriptionText1);
					}
					if(estrDescriptionText2 && estrLength(&estrDescriptionText2) > 0)
					{
						pOption->pchDescription2 = StructAllocString(estrDescriptionText2);
					}

					entity_SetDirtyBit(playerEnt, parse_InteractInfo, pInfo, true);
					entity_SetDirtyBit(playerEnt, parse_Player, playerEnt->pPlayer, false);
					eaDestroyStruct(&ppMapVars, parse_WorldVariable);
					estrDestroy(&estrDescriptionText1);
					estrDestroy(&estrDescriptionText2);
				}
			}
		}
	}
}

AUTO_COMMAND ACMD_ACCESSLEVEL(0) ACMD_SERVERCMD ACMD_PRIVATE;
void RemoteContactOption_RequestHeadshot(SA_PARAM_OP_VALID Entity* playerEnt, ACMD_NAMELIST(g_ContactNameList) const char* contactName)
{
	if(playerEnt && contactName)
	{
		ContactDef* contactDef = contact_DefFromName(contactName);

		if(contactDef) {
			InteractInfo* pInfo = SAFE_MEMBER2(playerEnt, pPlayer, pInteractInfo);
			if(pInfo) {
				RemoteContact* pRemoteContact = pInfo->eaRemoteContacts ? (RemoteContact*)eaIndexedGetUsingString(&pInfo->eaRemoteContacts, contactName) : NULL;

				if(pRemoteContact && !pRemoteContact->pHeadshot)
				{
					pRemoteContact->pHeadshot = StructCreate(parse_ContactHeadshotData);
					contact_CostumeToHeadshotData(playerEnt, &contactDef->costumePrefs, &pRemoteContact->pHeadshot);
					entity_SetDirtyBit(playerEnt, parse_InteractInfo, pInfo, true);
					entity_SetDirtyBit(playerEnt, parse_Player, playerEnt->pPlayer, false);
				}
			}
		}
	}
}
