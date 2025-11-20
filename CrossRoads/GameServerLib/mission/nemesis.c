/***************************************************************************
*     Copyright (c) 2008-2009, Cryptic Studios
*     All Rights Reserved
*     Confidential Property of Cryptic Studios
***************************************************************************/

#include "nemesis.h"
#include "nemesis_common.h"

#include "AutoTransDefs.h"
#include "CostumeCommonEntity.h"
#include "CostumeCommonTailor.h"
#include "entCritter.h"
#include "entity.h"
#include "EntityLib.h"
#include "EntitySavedData.h"
#include "GameAccountDataCommon.h"
#include "gslContact.h"
#include "gslCostume.h"
#include "gslLogSettings.h"
#include "LoggedTransactions.h"
#include "gslSavedPet.h"
#include "objtransactions.h"
#include "gslMission.h"
#include "mission_common.h"
#include "gslMission_transact.h"
#include "Player.h"
#include "CostumeCommon.h"
#include "ReferenceSystem.h"
#include "ResourceInfo.h"
#include "StringCache.h"
#include "StringUtil.h"
#include "transactionsystem.h"
#include "inventoryCommon.h"

#include "logging.h"
#include "GameStringFormat.h"
#include "NotifyCommon.h"
#include "mapstate_common.h"
#include "gslCritter.h"

#include "AutoGen/entity_h_ast.h"
#include "AutoGen/entitysaveddata_h_ast.h"
#include "AutoGen/Mission_common_h_ast.h"
#include "AutoGen/nemesis_common_h_ast.h"
#include "Player_h_ast.h"
#include "AutoGen/CostumeCommon_h_ast.h"
#include "AutoGen/mapstate_common_h_ast.h"

#include "autogen/gameserverlib_autotransactions_autogen_wrappers.h"
#include "AutoGen/GameServerLib_autogen_RemoteFuncs.h"
#include "AutoGen/GameClientLib_autogen_ClientCmdWrappers.h"

#include "character.h"

AUTO_RUN_ANON(memBudgetAddMapping(__FILE__, BUDGET_GameSystems););


// --------------------------------------------------------------------------
// Nemesis logic and utilities
// --------------------------------------------------------------------------

static Mission *nemesis_GetPrimaryNemesisArc(MissionInfo *pInfo)
{
	int i;
	for (i = eaSize(&pInfo->missions)-1; i>=0; --i) {
		MissionDef *pDef = mission_GetDef(pInfo->missions[i]);
		if (pDef && pDef->missionType == MissionType_NemesisArc) {
			return pInfo->missions[i];
		}
	}
	return NULL;
}


void nemesis_RefreshNemesisArc(Entity *pEnt)
{
	MissionInfo *pInfo = SAFE_MEMBER2(pEnt, pPlayer, missionInfo);

	if (pEnt && pInfo && player_GetPrimaryNemesis(pEnt) && !nemesis_GetPrimaryNemesisArc(pInfo)) {
		DictionaryEArrayStruct *pStruct = resDictGetEArrayStruct(g_MissionDictionary);
		int i, n = eaSize(&pStruct->ppReferents);
		MissionDef **eaAvailableArcs = NULL;

		for (i = 0; i < n; i++) {
			MissionDef *pDef = (MissionDef*)pStruct->ppReferents[i];
			if (pDef->missionType == MissionType_NemesisArc && missiondef_CanBeOfferedAsPrimary(pEnt, pDef, NULL, NULL)) {
				eaPush(&eaAvailableArcs, pDef);
			}
		}

		if (eaSize(&eaAvailableArcs) > 0) {
			MissionDef *pDef = eaRandChoice(&eaAvailableArcs);
			missioninfo_AddMission(entGetPartitionIdx(pEnt), pInfo, pDef, NULL, NULL, NULL);
		}
		eaDestroy(&eaAvailableArcs);
	}
}


NemesisPowerSet* nemesis_NemesisPowerSetFromName(const char *pchName)
{
	int i;
	if (pchName){
		for (i = eaSize(&g_NemesisPowerSetList.sets)-1; i>=0; --i){
			if (g_NemesisPowerSetList.sets[i]->pcName && !stricmp(pchName, g_NemesisPowerSetList.sets[i]->pcName)){
				return g_NemesisPowerSetList.sets[i];
			}
		}
	}
	return NULL;
}

NemesisMinionPowerSet* nemesis_NemesisMinionPowerSetFromName(const char *pchName)
{
	int i;
	if (pchName){
		for (i = eaSize(&g_NemesisMinionPowerSetList.sets)-1; i>=0; --i){
			if (g_NemesisMinionPowerSetList.sets[i]->pcName && !stricmp(pchName, g_NemesisMinionPowerSetList.sets[i]->pcName)){
				return g_NemesisMinionPowerSetList.sets[i];
			}
		}
	}
	return NULL;
}

NemesisMinionCostumeSet* nemesis_NemesisMinionCostumeSetFromName(const char *pchName)
{
	int i;
	if (pchName){
		for (i = eaSize(&g_NemesisMinionCostumeSetList.sets)-1; i>=0; --i){
			if (g_NemesisMinionCostumeSetList.sets[i]->pcName && !stricmp(pchName, g_NemesisMinionCostumeSetList.sets[i]->pcName)){
				return g_NemesisMinionCostumeSetList.sets[i];
			}
		}
	}
	return NULL;
}

PlayerCostume* nemesis_MinionCostumeByClass(const NemesisMinionCostumeSet *pSet, const char *pchClassName)
{
	S32 i;
	if(pSet && pchClassName)
	{
		for(i = 0; i < eaSize(&pSet->eaCostumes); ++i)
		{
			if(stricmp(pchClassName, pSet->eaCostumes[i]->pcClassName) == 0)
			{
				return GET_REF(pSet->eaCostumes[i]->hCostume);
			}
		}
	}
	return NULL;
}

// Called when a Nemesis is added to the Container Subscription dictionary
void nemesis_DictionaryLoadCB(Entity *pNemesisEnt)
{
	// Regenerate the lists of substitute defs and groups
	if (pNemesisEnt && pNemesisEnt->pSaved){
		Entity *pPlayerEnt = gslSavedPetGetOwner(pNemesisEnt);

		// If this is the primary nemesis, regenerate the substitute defs
		if(pPlayerEnt && player_GetPrimaryNemesisID(pPlayerEnt) == pNemesisEnt->myContainerID)
		{			
			// In case this nemesis was just created, give the player a Nemesis arc
			nemesis_RefreshNemesisArc(pPlayerEnt);
		}
	}
}

const char* nemesis_ChooseDefaultVoiceSet(Entity *pNemesisEnt)
{
	// Set voice-set based on personality and gender
	if (pNemesisEnt && pNemesisEnt->pNemesis)
	{
		switch(pNemesisEnt->pNemesis->personality)
		{
			xcase NemesisPersonality_Mastermind: 
			{
				switch(pNemesisEnt->eGender)
				{
					xcase Gender_Female: 
					{
						return allocAddString("/Nemesis/Mastermind/Female");
					}
					xcase Gender_Male: 
					{
						return allocAddString("/Nemesis/Mastermind/Male");
					}
				}
			}
			xcase NemesisPersonality_Savage: 
			{
				switch(pNemesisEnt->eGender)
				{
					xcase Gender_Female: 
					{
						return allocAddString("/Nemesis/Savage/Female");
					}
					xcase Gender_Male: 
					{
						return allocAddString("/Nemesis/Savage/Male");
					}
				}
			}
			xcase NemesisPersonality_Maniac: 
			{
				switch(pNemesisEnt->eGender)
				{
					xcase Gender_Female: 
					{
						return allocAddString("/Nemesis/Maniac/Female");
					}
					xcase Gender_Male: 
					{
						return allocAddString("/Nemesis/Maniac/Male");
					}
				}
			}
		}
	}
	return NULL;
}

// --------------------------------------------------------------------------
// Nemesis transactions
// --------------------------------------------------------------------------

// --- Delete a Nemesis ---

AUTO_TRANSACTION
	ATR_LOCKS(pPlayerEnt, ".pInventoryV2.Peaowneduniqueitems, .pInventoryV2.Pplitebags, .Pplayer.Nemesisinfo.Eanemesisstates, .Pplayer.Missioninfo.Missions, .Psaved.Conowner.Containerid, .Psaved.Conowner.Containertype, .Psaved.Ppuppetmaster.Pppuppets, .Psaved.Ppownedcontainers, .Psaved.Ppalwayspropslots, .Psaved.Pipetidsremovedfixup, .Psaved.Pppreferredpetids, .Pplayer.Missioninfo.Eamissionrequests, .pInventoryV2.Ppinventorybags, .Pplayer.Missioninfo.Completedmissions, .Hallegiance, .Hsuballegiance, .Pplayer.Pplayeraccountdata.Eagamepermissionmaxvaluenumerics, .Psaved.Uiindexbuild, .Psaved.Ppbuilds, .Pchar.Ilevelexp")
	ATR_LOCKS(pNemesis, ".Psaved.Conowner.Containerid, .Psaved.Conowner.Containertype, .Pchar.Hclass");
enumTransactionOutcome nemesis_tr_RemoveNemesis(ATR_ARGS, NOCONST(Entity) *pPlayerEnt, NOCONST(Entity) *pNemesis, const ItemChangeReason *pReason, GameAccountDataExtract *pExtract)
{
	int i;

	if (!trhRemoveSavedPet(ATR_PASS_ARGS, pPlayerEnt, pNemesis)){
		return TRANSACTION_OUTCOME_FAILURE;
	}

	for (i = eaSize(&pPlayerEnt->pPlayer->nemesisInfo.eaNemesisStates)-1; i >= 0; i--) {
		if (pPlayerEnt->pPlayer->nemesisInfo.eaNemesisStates[i]->iNemesisID == pNemesis->myContainerID) {
			
			// TODO(BF) - 
			// Completed Missions may still reference this Nemesis, something has to be done with those
			// Also, the edge case for crashing immediately after deleting the container needs to be handled

			// If the primary nemesis is deleted, remove the main Nemesis Arc as well
			if (pPlayerEnt->pPlayer->nemesisInfo.eaNemesisStates[i]->eState == NemesisState_Primary){
				int j;
				for (j = eaSize(&pPlayerEnt->pPlayer->missionInfo->missions)-1; j>=0; --j){
					MissionDef *pDef = RefSystem_ReferentFromString(g_MissionDictionary, pPlayerEnt->pPlayer->missionInfo->missions[j]->missionNameOrig);
					if (pDef && pDef->missionType == MissionType_NemesisArc){
						mission_tr_DropMission(ATR_PASS_ARGS, pPlayerEnt, pDef->name, pReason, /*NULL, */pExtract);
						break;
					}
				}
			}

			QueueRemoteCommand_eventsend_RemoteRecordNemesisState(ATR_RESULT_SUCCESS, 0, 0, pNemesis->myContainerID, NemesisState_Deleted);
			StructDestroyNoConst(parse_PlayerNemesisState, pPlayerEnt->pPlayer->nemesisInfo.eaNemesisStates[i]);
			eaRemove(&pPlayerEnt->pPlayer->nemesisInfo.eaNemesisStates, i);
		}
	}
	
	return TRANSACTION_OUTCOME_SUCCESS;
}

// --- Change a Nemesis' State ---

AUTO_TRANSACTION
	ATR_LOCKS(playerEnt, ".Pplayer.Nemesisinfo.Eanemesisstates");
enumTransactionOutcome nemesis_tr_ChangeNemesisState(ATR_ARGS, NOCONST(Entity) *playerEnt, U32 iNemesisID, int newState, U32 bCountsAsDefeat)
{
	int i, n;
	if(!iNemesisID || ISNULL(playerEnt->pPlayer)){
		TRANSACTION_RETURN_LOG_FAILURE("No nemesis ID or not a player");
	}

	if (!(newState > NemesisState_None && newState < NemesisState_Max)){
		TRANSACTION_RETURN_LOG_FAILURE("Invalid Nemesis state %d", newState);
	}

	QueueRemoteCommand_eventsend_RemoteRecordNemesisState(ATR_RESULT_SUCCESS, 0, 0, iNemesisID, newState);
	QueueRemoteCommand_nemesis_RemoteRefreshNemesisArc(ATR_RESULT_SUCCESS, 0, 0);

	// If this is creating a new primary nemesis, make sure there isn't already another primary nemesis
	n = eaSize(&playerEnt->pPlayer->nemesisInfo.eaNemesisStates);
	if(NemesisState_Primary == newState)
	{
		for(i=0; i<n; i++)
		{
			NOCONST(PlayerNemesisState)* currNemesis = playerEnt->pPlayer->nemesisInfo.eaNemesisStates[i];
			if(currNemesis->eState == NemesisState_Primary && currNemesis->iNemesisID != iNemesisID)
			{
				TRANSACTION_RETURN_LOG_FAILURE("Player already has a primary nemesis");
			}
		}		
	}

	// Find a nemesis with this name and change its state
	for(i=0; i<n; i++)
	{
		NOCONST(PlayerNemesisState)* currNemesis = playerEnt->pPlayer->nemesisInfo.eaNemesisStates[i];
		if(currNemesis->iNemesisID == iNemesisID)
		{
			int oldState = currNemesis->eState;
			currNemesis->eState = newState;
			if (bCountsAsDefeat)
				currNemesis->uTimesDefeated++;
			TRANSACTION_RETURN_LOG_SUCCESS("Nemesis %u: State changed from %d to %d", currNemesis->iNemesisID, oldState, newState);
		}
	}

	TRANSACTION_RETURN_LOG_FAILURE("No such nemesis on player");
}


AUTO_TRANSACTION
	ATR_LOCKS(playerEnt, ".Pplayer.Nemesisinfo.Eanemesisstates");
enumTransactionOutcome nemesis_tr_ChangePrimaryNemesisState(ATR_ARGS, NOCONST(Entity) *playerEnt, int newState, U32 bCountsAsDefeat)
{
	int i, n;
	if(ISNULL(playerEnt->pPlayer))
		TRANSACTION_RETURN_LOG_FAILURE("Not a player");

	if (!(newState > NemesisState_None && newState < NemesisState_Max)){
		TRANSACTION_RETURN_LOG_FAILURE("Invalid Nemesis state %d", newState);
	}

	// Find the primary nemesis and change its state
	n = eaSize(&playerEnt->pPlayer->nemesisInfo.eaNemesisStates);
	for(i=0; i<n; i++)
	{
		NOCONST(PlayerNemesisState)* currNemesis = playerEnt->pPlayer->nemesisInfo.eaNemesisStates[i];
		if(currNemesis->eState == NemesisState_Primary)
		{
			int oldState = currNemesis->eState;
			currNemesis->eState = newState;
			if (bCountsAsDefeat)
				currNemesis->uTimesDefeated++;
			QueueRemoteCommand_eventsend_RemoteRecordNemesisState(ATR_RESULT_SUCCESS, 0, 0, currNemesis->iNemesisID, newState);
			TRANSACTION_RETURN_LOG_SUCCESS("Nemesis %u: State changed from %d to %d", currNemesis->iNemesisID, oldState, newState);
		}
	}

	TRANSACTION_RETURN_LOG_FAILURE("No primary nemesis");
}


AUTO_TRANSACTION
	ATR_LOCKS(playerEnt, ".Pplayer.Nemesisinfo.Eanemesisstates");
enumTransactionOutcome nemesis_tr_PlayerSetPrimaryNemesis(ATR_ARGS, NOCONST(Entity) *playerEnt, U32 iNemesisID)
{
	int i;
	
	for (i = eaSize(&playerEnt->pPlayer->nemesisInfo.eaNemesisStates)-1; i >= 0; --i) {
		NOCONST(PlayerNemesisState) *pState = playerEnt->pPlayer->nemesisInfo.eaNemesisStates[i];
		if (pState->iNemesisID == iNemesisID && pState->eState != NemesisState_Primary) {
			pState->eState = NemesisState_Primary;
			QueueRemoteCommand_eventsend_RemoteRecordNemesisState(ATR_RESULT_SUCCESS, 0, 0, pState->iNemesisID, pState->eState);
		} else if (pState->eState == NemesisState_Primary) {
			pState->eState = NemesisState_AtLarge;
			QueueRemoteCommand_eventsend_RemoteRecordNemesisState(ATR_RESULT_SUCCESS, 0, 0, pState->iNemesisID, pState->eState);
		}
	}	
	return TRANSACTION_OUTCOME_SUCCESS;
}

// --- Edit a Nemesis ---

AUTO_TRANSACTION
	ATR_LOCKS(pPlayerEnt, ".Psaved.pSCPdata.Isummonedscp, .Psaved.pSCPdata.Fcachedpetbonusxppct, .Psaved.pSCPdata.erSCP, .Pinventoryv2.Ppinventorybags[], .Hallegiance, .Hsuballegiance, .Pchar.Ilevelexp, .Pplayer.Pugckillcreditlimit, .Pplayer.Pplayeraccountdata.Eagamepermissionmaxvaluenumerics, pInventoryV2.ppLiteBags[]")
	ATR_LOCKS(pNemesisEnt, ".Psaved.pSCPdata.Isummonedscp, .Psaved.pSCPdata.Fcachedpetbonusxppct, .Psaved.pSCPdata.erSCP, .Pinventoryv2.Ppinventorybags[], .Psaved.Bbadname, .Psaved.Saveddescription, .Pnemesis.Motivation, .Pnemesis.Personality, .Pnemesis.Pchpowerset, .Pnemesis.Pchminionpowerset, .Pnemesis.Pchminioncostumeset, .Pnemesis.Fpowerhue, .Pnemesis.Fminionpowerhue, .Costumeref.Hmood, .Psaved.Savedname, .Psaved.Costumedata.Uivalidatetag, .Hallegiance, .Hsuballegiance, .Psaved.Costumedata.Iactivecostume, .Psaved.Costumedata.Eacostumeslots, .Pchar.Ilevelexp, .Pplayer.Pugckillcreditlimit, .Pplayer.Pplayeraccountdata.Eagamepermissionmaxvaluenumerics, pInventoryV2.ppLiteBags[]");
enumTransactionOutcome nemesis_tr_EditNemesis(ATR_ARGS, NOCONST(Entity) *pPlayerEnt, S32 iPlayerLevel, NOCONST(Entity) *pNemesisEnt,
											  const char *pchNemesisName, const char *pchNemesisDescription, 
											  /*NemesisMotivation*/ int motivation, /*NemesisPersonality*/ int personality,
											  const char *pchNewPowerSet, const char *pchNewMinionPowerSet, const char *pchNewMinionCostumeSet, 
											  StorePlayerCostumeParam *pCostumeParam, const char *pchNewMood, F32 fPowerHue, F32 fMinionPowerHue,
											  const ItemChangeReason *pReason)
{
	// Calculate cost
	S32 cost = nemesis_trh_GetCostToChange(pNemesisEnt->pNemesis, iPlayerLevel, personality, pchNewPowerSet, pchNewMinionPowerSet, pchNewMinionCostumeSet, fPowerHue, fMinionPowerHue);

	// Change costume
	if (costume_tr_StorePlayerCostumeSimpleCostNoCreate(ATR_PASS_ARGS, pNemesisEnt, pPlayerEnt, pReason, pCostumeParam) != TRANSACTION_OUTCOME_SUCCESS){
		TRANSACTION_RETURN_LOG_FAILURE("Failed to edit costume");
	}

	// Change mood
	SET_HANDLE_FROM_STRING("CostumeMood", pchNewMood, pNemesisEnt->costumeRef.hMood);

	// Change Nemesis Name
	if (pchNemesisName && strcmp(pchNemesisName, pNemesisEnt->pSaved->savedName) != 0){
		if (pNemesisEnt->pSaved->bBadName){
			strcpy(pNemesisEnt->pSaved->savedName, pchNemesisName);
			pNemesisEnt->pSaved->bBadName = false;
		} else {
			TRANSACTION_RETURN_LOG_FAILURE("Name change not allowed (changing from %s to %s)", pNemesisEnt->pSaved->savedName, pchNemesisName);
		}
	}

	// Change Nemesis Description
	if (strlen(pchNemesisDescription) > MAX_DESCRIPTION_LEN){
		TRANSACTION_RETURN_LOG_FAILURE("Nemesis Description was too long");
	}
	StructFreeString(pNemesisEnt->pSaved->savedDescription);
	pNemesisEnt->pSaved->savedDescription = StructAllocString(pchNemesisDescription);

	// Change other Nemesis data
	pNemesisEnt->pNemesis->motivation = motivation;
	pNemesisEnt->pNemesis->personality = personality;
	pNemesisEnt->pNemesis->pchPowerSet = pchNewPowerSet;
	pNemesisEnt->pNemesis->pchMinionPowerSet = pchNewMinionPowerSet;
	pNemesisEnt->pNemesis->pchMinionCostumeSet = pchNewMinionCostumeSet;
	pNemesisEnt->pNemesis->fPowerHue = fPowerHue;
	pNemesisEnt->pNemesis->fMinionPowerHue = fMinionPowerHue;

	// Apply cost
	if (!inv_ent_trh_AddNumeric(ATR_PASS_ARGS, pPlayerEnt, false, "Resources", -cost, pReason)){
		return TRANSACTION_OUTCOME_FAILURE;
	}

	return TRANSACTION_OUTCOME_SUCCESS;
}

// --------------------------------------------------------------------------
// Nemesis Commands
// --------------------------------------------------------------------------

AUTO_COMMAND ACMD_SERVERCMD ACMD_ACCESSLEVEL(0) ACMD_PRIVATE;
void playerMakeNemesis(Entity *pEnt, const char *pchNemesisName, const char *pchNemesisDescription, PlayerCostume *pCostume, const char *pchMood, NemesisMotivation motivation, NemesisPersonality personality, const char *pchNemesisPowerSet, const char *pchMinionPowerSet, const char *pchMinionCostumeSet, F32 fPowerHue, F32 fMinionPowerHue)
{
	NemesisPowerSet* pPowerSet = NULL;
	NemesisMinionPowerSet *pMinionPowerSet = NULL;
	NemesisMinionCostumeSet *pMinionCostumeSet = NULL;
	CritterDef* pNemesisCritter = NULL;
	CritterGroup* pMinionGroup = NULL;
	PCMood *pMood = NULL;
	char pcMinionGroupName[1024];
	SpeciesDef *pSpecies = NULL;
	Character *pCharacter;
	char *estrReason=NULL;

	// This can only be done while talking to a nemesis contact
	if (!contact_IsPlayerNearNemesisContact(pEnt)){
		const char *pchError = langTranslateMessageKey(entGetLanguage(pEnt), "Nemesis_Error_Create_NoContact");
		notify_NotifySend(pEnt, kNotifyType_NemesisError, pchError, NULL, NULL);
		if (gbEnablePetAndPuppetLogging) {
			entLog(LOG_NEMESIS, pEnt, "CreateNemesis", "Cannot create nemesis unless talking to a nemesis contact.");
		}
		return;
	}

	// The player can't currently have a primary nemesis
	if(player_GetPrimaryNemesisID(pEnt)){
		const char *pchError = langTranslateMessageKey(entGetLanguage(pEnt), "Nemesis_Error_Create_Ineligible");
		notify_NotifySend(pEnt, kNotifyType_NemesisError, pchError, NULL, NULL);
		if (gbEnablePetAndPuppetLogging) {
			entLog(LOG_NEMESIS, pEnt, "CreateNemesis", "Player tried to make a Nemesis, but is ineligible.");
		}
		return;
	}

	// Players may only have 10 Nemeses at a time (there is a check for this inside the transaction as well)
	if (pEnt && pEnt->pPlayer && eaSize(&pEnt->pPlayer->nemesisInfo.eaNemesisStates) >= MAX_NEMESIS_COUNT){
		const char *pchError = langTranslateMessageKey(entGetLanguage(pEnt), "Nemesis_Error_Create_TooMany");
		notify_NotifySend(pEnt, kNotifyType_NemesisError, pchError, NULL, NULL);
		if (gbEnablePetAndPuppetLogging) {
			entLog(LOG_NEMESIS, pEnt, "CreateNemesis", "Player tried to make a Nemesis, but already has too many.");
		}
		return;
	}

	// Check for a duplicate name
	// This is unreliable because the other Nemeses may not be loaded, so don't rely on having unique names!
	if (nemesis_FindIDFromName(pEnt, pchNemesisName) != 0){
		char *estrBuffer = NULL;
		estrStackCreate(&estrBuffer);
		if (gbEnablePetAndPuppetLogging) {
			entLog(LOG_NEMESIS, pEnt, "CreateNemesis", "Non-unique Nemesis name \"%s\".", pchNemesisName);
		}
		entFormatGameMessageKey(pEnt, &estrBuffer, "Nemesis_Error_Create_DuplicateName", STRFMT_STRING("Name", pchNemesisName), STRFMT_END);
		notify_NotifySend(pEnt, kNotifyType_NemesisError, estrBuffer, NULL, NULL);
		estrDestroy(&estrBuffer);
		return;
	}

	//Check for a profane or invalid name
	if ( StringIsValidCommonName( pchNemesisName, SAFE_MEMBER2(pEnt, pPlayer, accessLevel) ) == false ){
		char *estrBuffer = NULL;
		estrStackCreate(&estrBuffer);
		if (gbEnablePetAndPuppetLogging) {
			entLog(LOG_NEMESIS, pEnt, "CreateNemesis", "Nemesis name \"%s\" is invalid.", pchNemesisName);
		}
		entFormatGameMessageKey(pEnt, &estrBuffer, "Nemesis_Error_Create_InvalidName", STRFMT_STRING("Name", pchNemesisName), STRFMT_END);
		notify_NotifySend(pEnt, kNotifyType_NemesisError, estrBuffer, NULL, NULL);
		estrDestroy(&estrBuffer);
		return;
	}

	//Check for profanity in the description
	if ( StringIsInvalidDescription( pchNemesisDescription ) != STRINGERR_NONE ) {
		char *estrBuffer = NULL;
		estrStackCreate(&estrBuffer);
		if (gbEnablePetAndPuppetLogging) {
			entLog(LOG_NEMESIS, pEnt, "CreateNemesis", "Nemesis description \"%s\" is invalid.", pchNemesisDescription);
		}
		entFormatGameMessageKey(pEnt, &estrBuffer, "Nemesis_Error_Create_InvalidDescription", STRFMT_STRING("Description", pchNemesisDescription), STRFMT_END);
		notify_NotifySend(pEnt, kNotifyType_NemesisError, estrBuffer, NULL, NULL);
		estrDestroy(&estrBuffer);
		return;
	}

	// Check for valid Costume
	if (!pCostume)
	{
		const char *pchError = langTranslateMessageKey(entGetLanguage(pEnt), "Nemesis_Error_Create_InvalidCostume");
		notify_NotifySend(pEnt, kNotifyType_NemesisError, pchError, NULL, NULL);
		if (gbEnablePetAndPuppetLogging) {
			entLog(LOG_NEMESIS, pEnt, "CreateNemesis", "Attempt to create nemesis with no costume.");
		}
		return;
	}

	pCharacter = pEnt->pChar;
	if(pCharacter){
		pSpecies = GET_REF(pCharacter->hSpecies);
	}
	costumeTailor_StripUnnecessary(CONTAINER_NOCONST(PlayerCostume, pCostume));
	estrStackCreate(&estrReason);
	if(!costumeValidate_ValidatePlayerCreated(pCostume, pSpecies, NULL, pEnt, pEnt, &estrReason, NULL, NULL, false))
	{
		const char *pchError = langTranslateMessageKey(entGetLanguage(pEnt), "Nemesis_Error_Create_InvalidCostume");
		notify_NotifySend(pEnt, kNotifyType_NemesisError, pchError, NULL, NULL);
		if (gbEnablePetAndPuppetLogging) {
			entLog(LOG_NEMESIS, pEnt, "CreateNemesis", "Attempt to create nemesis with invalid costume.");
		}
		Errorf("Attempt to create nemesis with invalid costume. Result: %s", estrReason);
		estrDestroy(&estrReason);
		return;
	}
	estrDestroy(&estrReason);

	// Check for valid Mood
	if (!(pMood = RefSystem_ReferentFromString("CostumeMood", pchMood))){
		const char *pchError = langTranslateMessageKey(entGetLanguage(pEnt), "Nemesis_Error_Create_Generic");
		notify_NotifySend(pEnt, kNotifyType_NemesisError, pchError, NULL, NULL);
		if (gbEnablePetAndPuppetLogging) {
			entLog(LOG_NEMESIS, pEnt, "CreateNemesis", "Attempt to create nemesis with invalid mood: %s.", pchMood);
		}
		return;
	}

	// Check for valid Motivation
	if (!(motivation >= 0 && motivation < NemesisMotivation_Count)){
		const char *pchError = langTranslateMessageKey(entGetLanguage(pEnt), "Nemesis_Error_Create_Generic");
		notify_NotifySend(pEnt, kNotifyType_NemesisError, pchError, NULL, NULL);
		if (gbEnablePetAndPuppetLogging) {
			entLog(LOG_NEMESIS, pEnt, "CreateNemesis", "Attempt to create nemesis with invalid Motivation: %d.", motivation);
		}
		return;
	}

	// Check for valid Personality
	if (!(personality >= 0 && personality < NemesisPersonality_Count)){
		const char *pchError = langTranslateMessageKey(entGetLanguage(pEnt), "Nemesis_Error_Create_Generic");
		notify_NotifySend(pEnt, kNotifyType_NemesisError, pchError, NULL, NULL);
		if (gbEnablePetAndPuppetLogging) {
			entLog(LOG_NEMESIS, pEnt, "CreateNemesis", "Attempt to create nemesis with invalid Personality: %d.", personality);
		}
		return;
	}

	// Check for valid Nemesis Power Set
	if (!(pPowerSet = nemesis_NemesisPowerSetFromName(pchNemesisPowerSet))){
		const char *pchError = langTranslateMessageKey(entGetLanguage(pEnt), "Nemesis_Error_Create_InvalidPowerSet");
		notify_NotifySend(pEnt, kNotifyType_NemesisError, pchError, NULL, NULL);
		if (gbEnablePetAndPuppetLogging) {
			entLog(LOG_NEMESIS, pEnt, "CreateNemesis", "Attempt to create nemesis with invalid Power Set '%s'.", pchNemesisPowerSet);
		}
		Errorf("Attempt to create nemesis with invalid Power Set '%s'.", pchNemesisPowerSet);
		return;
	}

	if (pPowerSet && !(pNemesisCritter = critter_DefGetByName(pPowerSet->pcCritter))){
		const char *pchError = langTranslateMessageKey(entGetLanguage(pEnt), "Nemesis_Error_Create_InvalidPowerSet");
		notify_NotifySend(pEnt, kNotifyType_NemesisError, pchError, NULL, NULL);
		if (gbEnablePetAndPuppetLogging) {
			entLog(LOG_NEMESIS, pEnt, "CreateNemesis", "Failed to create nemesis: Power Set '%s' refers to invalid CritterDef '%s'.", pchNemesisPowerSet, pPowerSet->pcCritter);
		}
		Errorf("Failed to create nemesis: Power Set '%s' refers to invalid CritterDef '%s'.", pchNemesisPowerSet, pPowerSet->pcCritter);
		return;
	}

	// Check for valid Minion Power Set
	if (!(pMinionPowerSet = nemesis_NemesisMinionPowerSetFromName(pchMinionPowerSet))){
		const char *pchError = langTranslateMessageKey(entGetLanguage(pEnt), "Nemesis_Error_Create_InvalidMinions");
		notify_NotifySend(pEnt, kNotifyType_NemesisError, pchError, NULL, NULL);
		if (gbEnablePetAndPuppetLogging) {
			entLog(LOG_NEMESIS, pEnt, "CreateNemesis", "Attempt to create nemesis with invalid Minion Power Set '%s'.", pchMinionPowerSet);
		}
		Errorf("Attempt to create nemesis with invalid Minion Power Set '%s'.", pchMinionPowerSet);
		return;
	}

	// Check for valid Minion Costume Set
	if (!(pMinionCostumeSet = nemesis_NemesisMinionCostumeSetFromName(pchMinionCostumeSet))){
		const char *pchError = langTranslateMessageKey(entGetLanguage(pEnt), "Nemesis_Error_Create_InvalidMinions");
		notify_NotifySend(pEnt, kNotifyType_NemesisError, pchError, NULL, NULL);
		if (gbEnablePetAndPuppetLogging) {
			entLog(LOG_NEMESIS, pEnt, "CreateNemesis", "Attempt to create nemesis with invalid Minion Costume Set '%s'.", pchMinionCostumeSet);
		}
		Errorf("Attempt to create nemesis with invalid Minion Costume Set '%s'.", pchMinionCostumeSet);
		return;
	}

	// Check that a Critter Group exists for the minion costume/power combination
	sprintf(pcMinionGroupName, "NemesisMinion_%s_%s", pchMinionPowerSet, pchMinionCostumeSet);
	if (!(pMinionGroup = RefSystem_ReferentFromString(g_hCritterGroupDict, pcMinionGroupName))) {
		const char *pchError = langTranslateMessageKey(entGetLanguage(pEnt), "Nemesis_Error_Create_InvalidMinions");
		notify_NotifySend(pEnt, kNotifyType_NemesisError, pchError, NULL, NULL);
		if (gbEnablePetAndPuppetLogging) {
			entLog(LOG_NEMESIS, pEnt, "CreateNemesis", "Attempt to create nemesis with invalid Minion Critter Group '%s'.", pcMinionGroupName);
		}
		Errorf("Attempt to create nemesis with invalid Minion Critter Group '%s'.", pcMinionGroupName);
		return;
	}

	// Create Saved Pet
	if (pEnt && pPowerSet && pchNemesisName && pMinionPowerSet && pMinionCostumeSet && pCostume && pMood){
		gslCreateSavedPetNemesis(pEnt, pchNemesisName, pchNemesisDescription, motivation, personality, pPowerSet, pMinionPowerSet, pMinionCostumeSet, pCostume, pMood, fPowerHue, fMinionPowerHue, NemesisState_Primary);
	}
}

void playerEditNemesis_CB(TransactionReturnVal *pReturnVal, EntityRef *pRef)
{
	Entity *pEnt = entFromEntityRefAnyPartition(*pRef);
	if (pEnt){
		if (pReturnVal->eOutcome == TRANSACTION_OUTCOME_SUCCESS){
			const char *pchError = langTranslateMessageKey(entGetLanguage(pEnt), "Nemesis_Edit_Succeeded");
			// TODO - Add the correct NotifyType here
			notify_NotifySend(pEnt, kNotifyType_NemesisAdded, pchError, NULL, NULL);
		} else {
			const char *pchError = langTranslateMessageKey(entGetLanguage(pEnt), "Nemesis_Error_Edit_TransactionFailure");
			notify_NotifySend(pEnt, kNotifyType_NemesisError, pchError, NULL, NULL);
		}
	}
	free(pRef);
}

AUTO_COMMAND ACMD_SERVERCMD ACMD_ACCESSLEVEL(0) ACMD_PRIVATE;
void playerEditNemesis(Entity *pEnt, ContainerID uNemesisID, const char *pchNemesisName, const char *pchNemesisDescription, PlayerCostume *pCostume, const char *pchMood, NemesisPersonality personality, const char *pchNemesisPowerSet, const char *pchMinionPowerSet, const char *pchMinionCostumeSet, F32 fPowerHue, F32 fMinionPowerHue)
{
	Entity *pNemesisEnt = player_GetNemesisByID(pEnt, uNemesisID);
	NemesisMotivation motivation = 0;
	NemesisPowerSet* pPowerSet = NULL;
	NemesisMinionPowerSet *pMinionPowerSet = NULL;
	NemesisMinionCostumeSet *pMinionCostumeSet = NULL;
	CritterDef* pNemesisCritter = NULL;
	CritterGroup* pMinionGroup = NULL;
	PCMood *pMood = NULL;
	char pcMinionGroupName[1024];
	SpeciesDef *pSpecies = NULL;
	Character *pCharacter;
	char *estrReason=NULL;

	// Make sure Nemesis exists
	if (!pNemesisEnt){
		const char *pchError = langTranslateMessageKey(entGetLanguage(pEnt), "Nemesis_Error_Edit_Generic");
		notify_NotifySend(pEnt, kNotifyType_NemesisError, pchError, NULL, NULL);
		if (gbEnablePetAndPuppetLogging) {
			entLog(LOG_NEMESIS, pEnt, "EditNemesis", "Nemesis %d doesn't appear to exist for player.", uNemesisID);
		}
		return;
	}

	// This can only be done while talking to a nemesis contact
	if (!contact_IsPlayerNearNemesisContact(pEnt)){
		const char *pchError = langTranslateMessageKey(entGetLanguage(pEnt), "Nemesis_Error_Edit_NoContact");
		notify_NotifySend(pEnt, kNotifyType_NemesisError, pchError, NULL, NULL);
		if (gbEnablePetAndPuppetLogging) {
			entLog(LOG_NEMESIS, pEnt, "EditNemesis", "Cannot edit nemesis unless talking to a nemesis contact.");
		}
		return;
	}

	// Check for a duplicate name
	// This is unreliable because the other Nemeses may not be loaded, so don't rely on having unique names!
	if (stricmp(pchNemesisName, entGetPersistedName(pNemesisEnt)) != 0
		&& nemesis_FindIDFromName(pEnt, pchNemesisName) != 0){
		char *estrBuffer = NULL;
		estrStackCreate(&estrBuffer);
		if (gbEnablePetAndPuppetLogging) {
			entLog(LOG_NEMESIS, pEnt, "EditNemesis", "Non-unique Nemesis name \"%s\".", pchNemesisName);
		}
		entFormatGameMessageKey(pEnt, &estrBuffer, "Nemesis_Error_Edit_DuplicateName", STRFMT_STRING("Name", pchNemesisName), STRFMT_END);
		notify_NotifySend(pEnt, kNotifyType_NemesisError, estrBuffer, NULL, NULL);
		estrDestroy(&estrBuffer);
		return;
	}

	//Check for a profane or invalid name
	if ( StringIsValidCommonName( pchNemesisName, SAFE_MEMBER2(pEnt, pPlayer, accessLevel) ) == false ){
		char *estrBuffer = NULL;
		estrStackCreate(&estrBuffer);
		if (gbEnablePetAndPuppetLogging) {
			entLog(LOG_NEMESIS, pEnt, "EditNemesis", "Nemesis name \"%s\" is invalid.", pchNemesisName);
		}
		entFormatGameMessageKey(pEnt, &estrBuffer, "Nemesis_Error_Edit_InvalidName", STRFMT_STRING("Name", pchNemesisName), STRFMT_END);
		notify_NotifySend(pEnt, kNotifyType_NemesisError, estrBuffer, NULL, NULL);
		estrDestroy(&estrBuffer);
		return;
	}

	//Check for profanity in the description
	if ( StringIsInvalidDescription( pchNemesisDescription ) != STRINGERR_NONE ) {
		char *estrBuffer = NULL;
		estrStackCreate(&estrBuffer);
		if (gbEnablePetAndPuppetLogging) {
			entLog(LOG_NEMESIS, pEnt, "EditNemesis", "Nemesis description \"%s\" is invalid.", pchNemesisDescription);
		}
		entFormatGameMessageKey(pEnt, &estrBuffer, "Nemesis_Error_Edit_InvalidDescription", STRFMT_STRING("Description", pchNemesisDescription), STRFMT_END);
		notify_NotifySend(pEnt, kNotifyType_NemesisError, estrBuffer, NULL, NULL);
		estrDestroy(&estrBuffer);
		return;
	}

	// Check for valid Costume
	if (!pCostume)
	{
		const char *pchError = langTranslateMessageKey(entGetLanguage(pEnt), "Nemesis_Error_Edit_InvalidCostume");
		notify_NotifySend(pEnt, kNotifyType_NemesisError, pchError, NULL, NULL);
		if (gbEnablePetAndPuppetLogging) {
			entLog(LOG_NEMESIS, pEnt, "EditNemesis", "Attempt to edit nemesis to have no costume.");
		}
		return;
	}

	// Check that the player isn't changing their gender/skeleton (not currently allowed)
	if (pCostume){
		PlayerCostume *pOldCostume = costumeEntity_GetSavedCostume(pNemesisEnt, 0);
		if (!pOldCostume || GET_REF(pOldCostume->hSkeleton) != GET_REF(pCostume->hSkeleton)){
			const char *pchError = langTranslateMessageKey(entGetLanguage(pEnt), "Nemesis_Error_Edit_InvalidCostume");
			notify_NotifySend(pEnt, kNotifyType_NemesisError, pchError, NULL, NULL);
			if (gbEnablePetAndPuppetLogging) {
				entLog(LOG_NEMESIS, pEnt, "EditNemesis", "Attempt to change the skeleton of a Nemesis.");
			}
			return;
		}
	}

	pCharacter = pEnt->pChar;
	if(pCharacter){
		pSpecies = GET_REF(pCharacter->hSpecies);
	}
	costumeTailor_StripUnnecessary(CONTAINER_NOCONST(PlayerCostume, pCostume));
	estrStackCreate(&estrReason);
	if(!costumeValidate_ValidatePlayerCreated(pCostume, pSpecies, NULL, pEnt, pEnt, &estrReason, NULL, NULL, false))
	{
		const char *pchError = langTranslateMessageKey(entGetLanguage(pEnt), "Nemesis_Error_Edit_InvalidCostume");
		notify_NotifySend(pEnt, kNotifyType_NemesisError, pchError, NULL, NULL);
		if (gbEnablePetAndPuppetLogging) {
			entLog(LOG_NEMESIS, pEnt, "EditNemesis", "Attempt to edit nemesis with invalid costume.");
		}
		Errorf("Attempt to edit nemesis with invalid costume. Result: %s", estrReason);
		estrDestroy(&estrReason);
		return;
	}
	estrDestroy(&estrReason);

	// Check for valid Mood
	if (!(pMood = RefSystem_ReferentFromString("CostumeMood", pchMood))){
		const char *pchError = langTranslateMessageKey(entGetLanguage(pEnt), "Nemesis_Error_Edit_Generic");
		notify_NotifySend(pEnt, kNotifyType_NemesisError, pchError, NULL, NULL);
		if (gbEnablePetAndPuppetLogging) {
			entLog(LOG_NEMESIS, pEnt, "EditNemesis", "Attempt to edit nemesis with invalid mood: %s.", pchMood);
		}
		return;
	}

	// Check for valid Motivation
	if (!(motivation >= 0 && motivation < NemesisMotivation_Count)){
		const char *pchError = langTranslateMessageKey(entGetLanguage(pEnt), "Nemesis_Error_Edit_Generic");
		notify_NotifySend(pEnt, kNotifyType_NemesisError, pchError, NULL, NULL);
		if (gbEnablePetAndPuppetLogging) {
			entLog(LOG_NEMESIS, pEnt, "EditNemesis", "Attempt to edit nemesis with invalid Motivation: %d.", motivation);
		}
		return;
	}

	// Check for valid Personality
	if (!(personality >= 0 && personality < NemesisPersonality_Count)){
		const char *pchError = langTranslateMessageKey(entGetLanguage(pEnt), "Nemesis_Error_Edit_Generic");
		notify_NotifySend(pEnt, kNotifyType_NemesisError, pchError, NULL, NULL);
		if (gbEnablePetAndPuppetLogging) {
			entLog(LOG_NEMESIS, pEnt, "EditNemesis", "Attempt to edit nemesis with invalid Personality: %d.", personality);
		}
		return;
	}

	// Check for valid Nemesis Power Set
	if (!(pPowerSet = nemesis_NemesisPowerSetFromName(pchNemesisPowerSet))){
		const char *pchError = langTranslateMessageKey(entGetLanguage(pEnt), "Nemesis_Error_Edit_InvalidPowerSet");
		notify_NotifySend(pEnt, kNotifyType_NemesisError, pchError, NULL, NULL);
		if (gbEnablePetAndPuppetLogging) {
			entLog(LOG_NEMESIS, pEnt, "EditNemesis", "Attempt to edit nemesis with invalid Power Set '%s'.", pchNemesisPowerSet);
		}
		Errorf("Attempt to edit nemesis with invalid Power Set '%s'.", pchNemesisPowerSet);
		return;
	}

	if (pPowerSet && !(pNemesisCritter = critter_DefGetByName(pPowerSet->pcCritter))){
		const char *pchError = langTranslateMessageKey(entGetLanguage(pEnt), "Nemesis_Error_Edit_InvalidPowerSet");
		notify_NotifySend(pEnt, kNotifyType_NemesisError, pchError, NULL, NULL);
		if (gbEnablePetAndPuppetLogging) {
			entLog(LOG_NEMESIS, pEnt, "EditNemesis", "Failed to modify nemesis: Power Set '%s' refers to invalid CritterDef '%s'.", pchNemesisPowerSet, pPowerSet->pcCritter);
		}
		Errorf("Failed to modify nemesis: Power Set '%s' refers to invalid CritterDef '%s'.", pchNemesisPowerSet, pPowerSet->pcCritter);
		return;
	}

	// Check for valid Minion Power Set
	if (!(pMinionPowerSet = nemesis_NemesisMinionPowerSetFromName(pchMinionPowerSet))){
		const char *pchError = langTranslateMessageKey(entGetLanguage(pEnt), "Nemesis_Error_Edit_InvalidMinions");
		notify_NotifySend(pEnt, kNotifyType_NemesisError, pchError, NULL, NULL);
		if (gbEnablePetAndPuppetLogging) {
			entLog(LOG_NEMESIS, pEnt, "EditNemesis", "Attempt to edit nemesis with invalid Minion Power Set '%s'.", pchMinionPowerSet);
		}
		Errorf("Attempt to edit nemesis with invalid Minion Power Set '%s'.", pchMinionPowerSet);
		return;
	}

	// Check for valid Minion Costume Set
	if (!(pMinionCostumeSet = nemesis_NemesisMinionCostumeSetFromName(pchMinionCostumeSet))){
		const char *pchError = langTranslateMessageKey(entGetLanguage(pEnt), "Nemesis_Error_Edit_InvalidMinions");
		notify_NotifySend(pEnt, kNotifyType_NemesisError, pchError, NULL, NULL);
		if (gbEnablePetAndPuppetLogging) {
			entLog(LOG_NEMESIS, pEnt, "EditNemesis", "Attempt to edit nemesis with invalid Minion Costume Set '%s'.", pchMinionCostumeSet);
		}
		Errorf("Attempt to edit nemesis with invalid Minion Costume Set '%s'.", pchMinionCostumeSet);
		return;
	}

	// Check that a Critter Group exists for the minion costume/power combination
	sprintf(pcMinionGroupName, "NemesisMinion_%s_%s", pchMinionPowerSet, pchMinionCostumeSet);
	if (!(pMinionGroup = RefSystem_ReferentFromString(g_hCritterGroupDict, pcMinionGroupName))) {
		const char *pchError = langTranslateMessageKey(entGetLanguage(pEnt), "Nemesis_Error_Edit_InvalidMinions");
		notify_NotifySend(pEnt, kNotifyType_NemesisError, pchError, NULL, NULL);
		if (gbEnablePetAndPuppetLogging) {
			entLog(LOG_NEMESIS, pEnt, "EditNemesis", "Attempt to edit nemesis with invalid Minion Critter Group '%s'.", pcMinionGroupName);
		}
		Errorf("Attempt to edit nemesis with invalid Minion Critter Group '%s'.", pcMinionGroupName);
		return;
	}

	// Create Saved Pet
	if (pEnt && pEnt->pPlayer && pPowerSet && pchNemesisName && pMinionPowerSet && pMinionCostumeSet && pCostume && pMood){
		S32 iCostumeCost = costumeEntity_GetCostToChange(pEnt, kPCCostumeStorageType_Pet, CONTAINER_NOCONST(PlayerCostume, costumeEntity_GetSavedCostume(pNemesisEnt, 0)), CONTAINER_NOCONST(PlayerCostume, pCostume), NULL);
		S32 iPlayerLevel = entity_GetSavedExpLevel(pEnt);
		EntityRef *pRef = calloc(1, sizeof(EntityRef));
		StorePlayerCostumeParam costumeParam = {0};
		ItemChangeReason reason = {0};

		*pRef = entGetRef(pEnt);

		if (!costumetransaction_InitStorePlayerCostumeParam(&costumeParam, pEnt, pNemesisEnt, entity_GetGameAccount(pEnt), kPCCostumeStorageType_Pet, 0, pCostume, NULL, iCostumeCost, kPCPay_Resources)) {
			return;
		}

		inv_FillItemChangeReason(&reason, pEnt, "Nemesis:Edit", pchNemesisName);

		AutoTrans_nemesis_tr_EditNemesis(LoggedTransactions_CreateManagedReturnValEnt("EditNemesis", pEnt, playerEditNemesis_CB, pRef), 
				GLOBALTYPE_GAMESERVER, entGetType(pEnt), entGetContainerID(pEnt), iPlayerLevel, 
				GLOBALTYPE_ENTITYSAVEDPET, uNemesisID, 
				pchNemesisName, pchNemesisDescription, motivation, personality, pPowerSet->pcName, pMinionPowerSet->pcName, pMinionCostumeSet->pcName, 
				&costumeParam, pMood->pcName, fPowerHue, fMinionPowerHue, &reason);
	}
}


// Reactivates a Nemesis that has been defeated
AUTO_COMMAND ACMD_SERVERCMD ACMD_ACCESSLEVEL(0) ACMD_HIDE ACMD_PRODUCTS(FightClub);
void playerReactivateNemesis(Entity *pEnt, ContainerID uNemesisID)
{
	PlayerNemesisState *pNemesisState = NULL;
	if (pEnt && pEnt->pPlayer){
		int i;
		for (i = 0; i < eaSize(&pEnt->pPlayer->nemesisInfo.eaNemesisStates); i++){
			if (pEnt->pPlayer->nemesisInfo.eaNemesisStates[i]->iNemesisID == uNemesisID){
				pNemesisState = pEnt->pPlayer->nemesisInfo.eaNemesisStates[i];
			}
		}
	}

	// Make sure Nemesis is valid
	if (!pNemesisState){
		if (gbEnablePetAndPuppetLogging) {
			entLog(LOG_NEMESIS, pEnt, "ReactivateNemesis", "Player tried to reactivate a Nemesis, but the Nemesis doesn't exist.");
		}
		return;
	}

	// Make sure the Nemesis is in a state where reactivation is allowed
	if (pNemesisState){
		if (pNemesisState->eState == NemesisState_Primary){
			return;
		} else if (pNemesisState->eState == NemesisState_Dead 
			|| pNemesisState->eState == NemesisState_Deleted
			|| pNemesisState->eState < 0 
			|| pNemesisState->eState >= NemesisState_Max){
			const char *pchError = langTranslateMessageKey(entGetLanguage(pEnt), "Nemesis_Error_Reactivate_NemesisIneligible");
			notify_NotifySend(pEnt, kNotifyType_NemesisError, pchError, NULL, NULL);
			if (gbEnablePetAndPuppetLogging) {
				entLog(LOG_NEMESIS, pEnt, "ReactivateNemesis", "Player tried to reactivate a Nemesis, but the nemesis is in the wrong state.");
			}
			return;
		}
	}

	// This can only be done while talking to a nemesis contact
	if (!contact_IsPlayerNearNemesisContact(pEnt) && entGetAccessLevel(pEnt) < ACCESS_DEBUG){
		const char *pchError = langTranslateMessageKey(entGetLanguage(pEnt), "Nemesis_Error_Reactivate_NoContact");
		notify_NotifySend(pEnt, kNotifyType_NemesisError, pchError, NULL, NULL);
		if (gbEnablePetAndPuppetLogging) {
			entLog(LOG_NEMESIS, pEnt, "ReactivateNemesis", "Cannot reactivate nemesis unless talking to a nemesis contact.");
		}
		return;
	}

	// The player can't currently have a primary nemesis
	if(player_GetPrimaryNemesisID(pEnt)){
		const char *pchError = langTranslateMessageKey(entGetLanguage(pEnt), "Nemesis_Error_Reactivate_AlreadyHasPrimaryNemesis");
		notify_NotifySend(pEnt, kNotifyType_NemesisError, pchError, NULL, NULL);
		if (gbEnablePetAndPuppetLogging) {
			entLog(LOG_NEMESIS, pEnt, "ReactivateNemesis", "Player tried to reactivate a Nemesis, but is ineligible (already has a primary nemesis).");
		}
		return;
	}

	if (uNemesisID){
		AutoTrans_nemesis_tr_ChangeNemesisState(LoggedTransactions_CreateManagedReturnValEnt("Nemesis", pEnt, NULL, NULL), GLOBALTYPE_GAMESERVER, entGetType(pEnt), entGetContainerID(pEnt), uNemesisID, NemesisState_Primary, false);
	}
}

// Command to send the list of nemesis power sets down to the client
AUTO_COMMAND ACMD_ACCESSLEVEL(0) ACMD_SERVERCMD ACMD_PRIVATE;
void PopulateNemesisPowerSetList(Entity *pEnt)
{
	ClientCmd_PopulateNemesisPowerSetList(pEnt, &g_NemesisPowerSetList);
}

// Command to send the list of nemesis minion power sets down to the client
AUTO_COMMAND ACMD_ACCESSLEVEL(0) ACMD_SERVERCMD ACMD_PRIVATE;
void PopulateNemesisMinionPowerSetList(Entity *pEnt)
{
	ClientCmd_PopulateNemesisMinionPowerSetList(pEnt, &g_NemesisMinionPowerSetList);
}

// Command to send the list of nemesis minion costume sets down to the client
AUTO_COMMAND ACMD_ACCESSLEVEL(0) ACMD_SERVERCMD ACMD_PRIVATE;
void PopulateNemesisMinionCostumeSetList(Entity *pEnt)
{
	ClientCmd_PopulateNemesisMinionCostumeSetList(pEnt, &g_NemesisMinionCostumeSetList);
}


AUTO_COMMAND_REMOTE;
void nemesis_RemoteRefreshNemesisArc(CmdContext *pContext)
{
	if (pContext && pContext->clientType == GLOBALTYPE_ENTITYPLAYER) {
		Entity *pEnt = entFromContainerIDAnyPartition(pContext->clientType, pContext->clientID);
		nemesis_RefreshNemesisArc(pEnt);
	}
}


// ----------------------------------------------------------------------------
//  Nemesis Debug Commands
// ----------------------------------------------------------------------------

// Command for testing; changes the primary nemesis
AUTO_COMMAND;
const char* PlayerSetPrimaryNemesis(Entity *pEnt, const char *pchNemesisName)
{
	ContainerID iNemesisID = nemesis_FindIDFromName(pEnt, pchNemesisName);

	if (iNemesisID){
		AutoTrans_nemesis_tr_PlayerSetPrimaryNemesis(LoggedTransactions_CreateManagedReturnValEnt("Nemesis", pEnt, NULL, NULL), GLOBALTYPE_GAMESERVER, entGetType(pEnt), entGetContainerID(pEnt), iNemesisID);
	} else {
		return "Nemesis not found";
	}
	return NULL;
}

// Changes a nemesis state
AUTO_COMMAND;
const char* playerChangeNemesisState(Entity *pEnt, const char *pchNemesisName, const char* pchNewState)
{
	ContainerID iNemesisID = nemesis_FindIDFromName(pEnt, pchNemesisName);
	int eState = StaticDefineIntGetInt(NemesisStateEnum, pchNewState);

	if (eState == -1){
		return "Invalid state";
	}

	if (iNemesisID){
		AutoTrans_nemesis_tr_ChangeNemesisState(LoggedTransactions_CreateManagedReturnValEnt("Nemesis", pEnt, NULL, NULL), GLOBALTYPE_GAMESERVER, entGetType(pEnt), entGetContainerID(pEnt), iNemesisID, eState, false);
	} else {
		return "Nemesis not found";
	}
	return NULL;
}

AUTO_COMMAND ACMD_ACCESSLEVEL(9);
const char* playerDeleteNemesis(Entity *pEnt, ACMD_SENTENCE pcName)
{
	Entity *pNemesisEnt = player_GetNemesisByID(pEnt, nemesis_FindIDFromName(pEnt, pcName));
	if (pNemesisEnt){
		gslDestroySavedPetNemesis(pNemesisEnt);
		return "OK";
	} else {
		return "Couldn't find Nemesis";
	}
}

static void TestNemesisModifyColor(U8 color[4])
{
	F32 tmp = color[2];
	color[2] = color[1]/2;
	color[1] = color[0]/2;
	color[0] = tmp/2;
}

// Quick way to create a nemesis for testing.  Picks random options.
AUTO_COMMAND;
const char* PlayerCreateTestNemesis(Entity *pEnt, const char *pchNemesisName)
{
	CritterDef *pCritterDef = NULL;
	CritterGroup *pMinionGroup = NULL;
	PCMood *pMood = NULL;
	DictionaryEArrayStruct *pMoodDict = resDictGetEArrayStruct("CostumeMood");
	NemesisPowerSet *pPowerSet = NULL;
	NemesisMinionPowerSet *pMinionPowerSet = NULL;
	NemesisMinionCostumeSet *pMinionCostumeSet = NULL;
	static char buffer[256];
	
	// Select random options
	pPowerSet = eaRandChoice(&g_NemesisPowerSetList.sets);
	pMinionPowerSet = eaRandChoice(&g_NemesisMinionPowerSetList.sets);
	pMinionCostumeSet = eaRandChoice(&g_NemesisMinionCostumeSetList.sets);

	// Validate Power Set
	if (pPowerSet){
		pCritterDef = RefSystem_ReferentFromString("CritterDef", pPowerSet->pcCritter);
		if (!pCritterDef){
			sprintf(buffer, "Error: Couldn't find a Nemesis CritterDef named '%s'.  Bad data in NemesisPowerSets.def?", pPowerSet->pcCritter);
			return buffer;
		}
	} else {
		return "Error: No Nemesis Power Sets defined.";
	}

	// Validate Minion Set
	if (pMinionPowerSet && pMinionCostumeSet){
		char pchMinionGroupName[1024] = {0};
		sprintf(pchMinionGroupName, "NemesisMinion_%s_%s", pMinionPowerSet->pcName, pMinionCostumeSet->pcName);
		pMinionGroup = RefSystem_ReferentFromString("CritterGroup", pchMinionGroupName);
		if (!pMinionGroup){
			sprintf(buffer, "Error: Couldn't find Nemesis Minion Group named '%s'.  Bad data in NemesisMinionPowerSets.def or NemesisMinionCostumeSets.def?", pchMinionGroupName);
			return buffer;
		}
	} else if (!pMinionCostumeSet) {
		return "Error: No Nemesis Minion Costume Sets defined.";
	} else if (!pMinionPowerSet) {
		return "Error: No Nemesis Minion Power Sets defined.";
	}

	// Select random Mood
	if (pMoodDict && eaSize(&pMoodDict->ppReferents)){
		pMood = eaRandChoice(&pMoodDict->ppReferents);
	} else {
		return "Error: No Moods found";
	}

	if (pEnt && pchNemesisName && pPowerSet && pMinionPowerSet && pMinionCostumeSet && pMood){
		NOCONST(PlayerCostume) *pCostume = StructCloneDeConst(parse_PlayerCostume, costumeEntity_GetActiveSavedCostume(pEnt));
		int i, n = eaSize(&pCostume->eaParts);

		// Modify costume hack
		pCostume->fHeight += 2.f;  // Your nemesis is extra tall!
		for (i = 0; i < n; i++){
			// Modify colors
			TestNemesisModifyColor(pCostume->eaParts[i]->color0);
			TestNemesisModifyColor(pCostume->eaParts[i]->color1);
			TestNemesisModifyColor(pCostume->eaParts[i]->color2);
			TestNemesisModifyColor(pCostume->eaParts[i]->color3);
		}
	
		gslCreateSavedPetNemesis(pEnt, pchNemesisName, "", 0, 0, pPowerSet, pMinionPowerSet, pMinionCostumeSet, (PlayerCostume*)pCostume, pMood, 0, 0, player_GetPrimaryNemesisID(pEnt)?NemesisState_AtLarge:NemesisState_Primary);

		StructDestroyNoConst(parse_PlayerCostume, pCostume);
		return "Attempted to create nemesis";
	} else {
		return "Failed to create nemesis";
	}
}

// Get the leader index if it is set
S32 Nemesis_GetLeaderIndex(MapState *pState)
{
	if(pState && pState->nemesisInfo.bLeaderSet)
	{
		return pState->nemesisInfo.iLeaderIdx;
	}

	return -1;
}

void Nemesis_RecordTeamIndex(Entity *pEntity, S32 iIndex, S32 iPartitonIdx)
{
	if(iIndex >= 0 && iIndex < MAX_NEMESIS_TEAM_INDEX)
	{
		MapState *pState = mapState_FromPartitionIdx(iPartitonIdx);
		if(pState)
		{
			Entity *pNemesisEnt = player_GetPrimaryNemesis(pEntity);			// get the nemesis for this ent

			while(eaSize(&pState->nemesisInfo.eaNemesisTeam) <= iIndex)
			{
				NemesisTeamStruct *pTeam = StructCreate(parse_NemesisTeamStruct);
				eaPush(&pState->nemesisInfo.eaNemesisTeam, pTeam);
			}
			
			// if there is a nemesis ent then pEnt is also valid
			if(pNemesisEnt)
			{
				CritterDef *pCritterDef = critter_GetNemesisCritter(pNemesisEnt);
				CritterGroup *pCritterGroup = critter_GetNemesisMinionGroup(pNemesisEnt);
				PlayerCostume *pCostume = costumeEntity_GetSavedCostume(pNemesisEnt, 0);

				pState->nemesisInfo.eaNemesisTeam[iIndex]->iId = pEntity->myContainerID;
				pState->nemesisInfo.eaNemesisTeam[iIndex]->motivation = pNemesisEnt->pNemesis->motivation;
				pState->nemesisInfo.eaNemesisTeam[iIndex]->personality = pNemesisEnt->pNemesis->personality;

				if(pCritterDef)
				{
					SET_HANDLE_FROM_REFERENT(g_hCritterDefDict, pCritterDef, pState->nemesisInfo.eaNemesisTeam[iIndex]->hCritter);
					pState->nemesisInfo.eaNemesisTeam[iIndex]->pchNemesisName = StructAllocString(entGetPersistedName(pNemesisEnt));
				}
				if(pCritterGroup)
				{
					SET_HANDLE_FROM_REFERENT("CritterGroup", pCritterGroup, pState->nemesisInfo.eaNemesisTeam[iIndex]->hCritterGroup);
					pState->nemesisInfo.eaNemesisTeam[iIndex]->pchNemesisCostumeSet = pNemesisEnt->pNemesis->pchMinionCostumeSet;
				}
				if(pCostume)
				{
					pState->nemesisInfo.eaNemesisTeam[iIndex]->pNemesisCostume = StructClone(parse_PlayerCostume, pCostume);
				}
			}
			else
			{
				pState->nemesisInfo.eaNemesisTeam[iIndex]->bNoNemesis = true;

				// set default nemesis data

				// Nemesis critter
				if(eaSize(&g_NemesisConfig.eaNemesisCritters) > 0)
				{
					S32 idx = randomIntRange(0, eaSize(&g_NemesisConfig.eaNemesisCritters) - 1);
					CritterDef *pCritter = GET_REF(g_NemesisConfig.eaNemesisCritters[idx]->hCritter);
					if(pCritter)
					{
						SET_HANDLE_FROM_REFERENT(g_hCritterDefDict, pCritter, pState->nemesisInfo.eaNemesisTeam[iIndex]->hCritter);
					}
				}
				// Nemesis critter group (minions)
				if(eaSize(&g_NemesisConfig.eaNemesisCritterGroups) > 0)
				{
					S32 idx = randomIntRange(0, eaSize(&g_NemesisConfig.eaNemesisCritterGroups) - 1);
					CritterGroup *pCritterG = GET_REF(g_NemesisConfig.eaNemesisCritterGroups[idx]->hCritterGroup);
					if(pCritterG)
					{
						SET_HANDLE_FROM_REFERENT("CritterGroup", pCritterG, pState->nemesisInfo.eaNemesisTeam[iIndex]->hCritterGroup);
					}
				}
				// Nemesis critter name
				if(eaSize(&g_NemesisConfig.eaNemesisDefaultNames) > 0)
				{
					S32 idx = randomIntRange(0, eaSize(&g_NemesisConfig.eaNemesisDefaultNames) - 1);
					if(g_NemesisConfig.eaNemesisDefaultNames[idx]->pchNemesisName)
					{
						pState->nemesisInfo.eaNemesisTeam[iIndex]->pchNemesisName = StructAllocString(g_NemesisConfig.eaNemesisDefaultNames[idx]->pchNemesisName);
					}
				}
				// Nemesis critter costume
				if(eaSize(&g_NemesisConfig.eaNemesisCostumes) > 0)
				{
					S32 idx = randomIntRange(0, eaSize(&g_NemesisConfig.eaNemesisCostumes) - 1);
					PlayerCostume *pCostume = GET_REF(g_NemesisConfig.eaNemesisCostumes[idx]->hNemesisCostume);
					if(pCostume)
					{
						pState->nemesisInfo.eaNemesisTeam[iIndex]->pNemesisCostume = StructClone(parse_PlayerCostume, pCostume);
					}
				}
				// Nemesis minion costume sets
				if(eaSize(&g_NemesisConfig.eaNemesisDefaultMinionCostumes) > 0)
				{
					S32 idx = randomIntRange(0, eaSize(&g_NemesisConfig.eaNemesisDefaultMinionCostumes) - 1);
					if(g_NemesisConfig.eaNemesisDefaultMinionCostumes[idx]->pchNemesisCostumeSet)
					{
						pState->nemesisInfo.eaNemesisTeam[iIndex]->pchNemesisCostumeSet = g_NemesisConfig.eaNemesisDefaultMinionCostumes[idx]->pchNemesisCostumeSet;
					}
				}
			}
		}
	}
}

void Nemesis_RecordTeamLeader(Entity *pEntity, S32 iIndex, S32 iPartitonIdx)
{
		MapState *pState = mapState_FromPartitionIdx(iPartitonIdx);
		if(pState)
		{
			Nemesis_RecordTeamIndex(pEntity, iIndex, iPartitonIdx);
			pState->nemesisInfo.bLeaderSet = true;
			if(pEntity)
			{
				pState->nemesisInfo.iLeaderIdx = iIndex;
			}
			else
			{
				pState->nemesisInfo.bLeaderNoNemesis = true;
			}
		}
}

// Get the player ent who owns the nemesis at index
Entity *Nemesis_TeamGetPlayerEntAtIndex(S32 iPartitionIdx, S32 iIndex, bool bUseLeader)
{
	MapState *pState = mapState_FromPartitionIdx(iPartitionIdx);
	if(bUseLeader)
	{
		iIndex = Nemesis_GetLeaderIndex(pState);
	}
	if(pState && iIndex >= 0 && iIndex < eaSize(&pState->nemesisInfo.eaNemesisTeam))
	{
		return entFromContainerID(pState->iPartitionIdx, GLOBALTYPE_ENTITYPLAYER, pState->nemesisInfo.eaNemesisTeam[iIndex]->iId);
	}

	return NULL;
}

// Get the team leader (ent player) if he has a primary nemesis, uses map state leader ent first
Entity *Nemesis_TeamGetTeamLeader(Entity *pEntity, bool bAnyTeamMember)
{
	if(pEntity)
	{
		MapState *pState = mapState_FromEnt(pEntity);
		S32 iPartitionIdx = entGetPartitionIdx(pEntity);
		if(pState)
		{
			if(pState->nemesisInfo.bLeaderSet)
			{
				if(pState->nemesisInfo.bLeaderNoNemesis)
				{
					return NULL;
				}
				return entFromContainerID(pState->iPartitionIdx, GLOBALTYPE_ENTITYPLAYER, pState->nemesisInfo.eaNemesisTeam[pState->nemesisInfo.iLeaderIdx]->iId);
			}
			else
			{
				Team *pTeam = NULL;
				if(pEntity->pTeam)
				{
					pTeam = GET_REF(pEntity->pTeam->hTeam);
				}
				if(team_IsMember(pEntity) && pTeam)
				{
					S32 iIndex = 0;
					Entity *pLeader = team_GetTeamLeader(iPartitionIdx, pTeam);
					if(pLeader)
					{
						S32 i;
						for(i = 0; i < eaSize(&pTeam->eaMembers); ++i)
						{
							Entity *pTeamEnt = entFromContainerID(iPartitionIdx, GLOBALTYPE_ENTITYPLAYER, pTeam->eaMembers[i]->iEntID);
							if(pTeamEnt && pTeamEnt->myContainerID == pLeader->myContainerID)
							{
								iIndex = i;
								break;
							}
						}
					}

					if(!bAnyTeamMember)
					{
						if(pLeader)
						{
							if(player_GetPrimaryNemesis(pLeader))
							{
								Nemesis_RecordTeamLeader(pLeader, iIndex, iPartitionIdx);
							}
							else
							{
								Nemesis_RecordTeamLeader(NULL, iIndex, iPartitionIdx);
							}
						}
					}
					else
					{
						// check all teammates
						S32 i;
						U32 uNumChecked = 0;
						bool bFound = false;
						bool bTimeOut = false;

						U32 uTm = timeSecondsSince2000();
						for(i = 0; i < eaSize(&pTeam->eaMembers); ++i)
						{
							Entity *pEnt = entFromContainerID(iPartitionIdx, GLOBALTYPE_ENTITYPLAYER, pTeam->eaMembers[i]->iEntID);
							if(pEnt)
							{
								if(player_GetPrimaryNemesis(pEnt))
								{
									Nemesis_RecordTeamLeader(pEnt, i, iPartitionIdx);
									bFound = true;
									break;
								}
								else
								{
									++uNumChecked;
								}
							}
						}

						// Added code so if someone does not arrive on the map within 15 seconds and no one has a nemesis use the default
						// At least one player must be on map for this check
						if(pState->nemesisInfo.uTimeFirstChecked == 0)
						{
							if(uNumChecked > 0)
							{
								pState->nemesisInfo.uTimeFirstChecked = uTm + 15;	// after 15 seconds use the no team leader nemesis
							}
						}
						else if(uTm > pState->nemesisInfo.uTimeFirstChecked)
						{
							bTimeOut = true;
						}

						if(!bFound && ((S32)uNumChecked >= eaSize(&pTeam->eaMembers) || bTimeOut))
						{
							// no one has a nemesis ... leader is now marked
							Nemesis_RecordTeamLeader(NULL, iIndex, iPartitionIdx);
						}
					}
				}
				else
				{
					// solo
					if(player_GetPrimaryNemesis(pEntity))
					{
						Nemesis_RecordTeamLeader(pEntity, 0, iPartitionIdx);
					}
					else
					{
						Nemesis_RecordTeamLeader(NULL, 0, iPartitionIdx);
					}
				}

				if(!pState->nemesisInfo.bLeaderNoNemesis && pState->nemesisInfo.bLeaderSet)
				{
					return entFromContainerID(pState->iPartitionIdx, GLOBALTYPE_ENTITYPLAYER, pState->nemesisInfo.eaNemesisTeam[pState->nemesisInfo.iLeaderIdx]->iId);
				}

			}	// has leader ID
		}	// pMapState
	}

	return NULL;
}

// Get the ent player at this team index. Is set once so if team changes this player is still recorded for 1st time index checked
Entity *Nemesis_TeamGetTeamIndex(Entity *pEntity, S32 iIndex)
{
	if(pEntity && iIndex >= 0 && iIndex < MAX_NEMESIS_TEAM_INDEX)
	{
		MapState *pState = mapState_FromEnt(pEntity);
		if(pState)
		{
			if(iIndex < eaSize(&pState->nemesisInfo.eaNemesisTeam) && pState->nemesisInfo.eaNemesisTeam[iIndex]->iId > 0)
			{
				// use index
				return entFromContainerID(pState->iPartitionIdx, GLOBALTYPE_ENTITYPLAYER, pState->nemesisInfo.eaNemesisTeam[iIndex]->iId);
			}
			else if(iIndex < eaSize(&pState->nemesisInfo.eaNemesisTeam) && pState->nemesisInfo.eaNemesisTeam[iIndex]->bNoNemesis)
			{
				// There isn't and will not be a nemesis at this team index
				return NULL;
			}
			else
			{
				// See if we can find team member at this index
				S32 iPartitionIdx = pState->iPartitionIdx;
				Team *pTeam = NULL;
				if(pEntity->pTeam)
				{
					pTeam = GET_REF(pEntity->pTeam->hTeam);
				}
				if(team_IsMember(pEntity) && pTeam)
				{
					if(iIndex >= 0 && iIndex < eaSize(&pTeam->eaMembers))
					{
						S32 i;
						bool bUse = true;

						Entity *pEnt = entFromContainerID(iPartitionIdx, GLOBALTYPE_ENTITYPLAYER, pTeam->eaMembers[iIndex]->iEntID);

						if(pEnt)
						{
							// make sure this ent isn't already being used at another index
							for(i = 0; i < eaSize(&pState->nemesisInfo.eaNemesisTeam); ++i)
							{
								if(pEnt->myContainerID == pState->nemesisInfo.eaNemesisTeam[i]->iId)
								{
									// already in use
									bUse = false;
									break;
								}
							}

							if(bUse && player_GetPrimaryNemesis(pEnt))
							{
								// record information about this entities nemesis
								Nemesis_RecordTeamIndex(pEnt, iIndex, iPartitionIdx);
							}
							else
							{
								// record that this index will not have a nemesis
								Nemesis_RecordTeamIndex(NULL, iIndex, iPartitionIdx);
							}
						}
					}
				}
				else if(iIndex == 0)	// index zero is used by solo players
				{
					// solo
					if(player_GetPrimaryNemesis(pEntity))
					{
						Nemesis_RecordTeamIndex(pEntity, iIndex, iPartitionIdx);
					}
					else
					{
						Nemesis_RecordTeamIndex(NULL, iIndex, iPartitionIdx);
					}
				}

				// Data could be set now, if so return entity
				if(iIndex < eaSize(&pState->nemesisInfo.eaNemesisTeam))
				{
					if(pState->nemesisInfo.eaNemesisTeam[iIndex]->iId)
					{
						return entFromContainerID(pState->iPartitionIdx, GLOBALTYPE_ENTITYPLAYER, pState->nemesisInfo.eaNemesisTeam[iIndex]->iId);
					}
				}
			}
		}
	}

	return NULL;
}

CritterDef *Nemesis_GetCritterDefAtIndex(S32 iPartitionIdx, S32 iIndex, bool bUseLeader)
{
	MapState *pState = mapState_FromPartitionIdx(iPartitionIdx);
	if(bUseLeader)
	{
		iIndex = Nemesis_GetLeaderIndex(pState);
	}
	if(pState && iIndex >=0 && iIndex < eaSize(&pState->nemesisInfo.eaNemesisTeam))
	{
		if(pState->nemesisInfo.eaNemesisTeam[iIndex]->iId > 0 || pState->nemesisInfo.eaNemesisTeam[iIndex]->bNoNemesis)
		{
			return GET_REF(pState->nemesisInfo.eaNemesisTeam[iIndex]->hCritter);
		}
	}

	return NULL;
}

CritterGroup *Nemesis_GetCritterGroupAtIndex(S32 iPartitionIdx, S32 iIndex, bool bUseLeader)
{
	MapState *pState = mapState_FromPartitionIdx(iPartitionIdx);
	if(bUseLeader)
	{
		iIndex = Nemesis_GetLeaderIndex(pState);
	}
	if(pState && iIndex >=0 && iIndex < eaSize(&pState->nemesisInfo.eaNemesisTeam))
	{
		if(pState->nemesisInfo.eaNemesisTeam[iIndex]->iId > 0 || pState->nemesisInfo.eaNemesisTeam[iIndex]->bNoNemesis)
		{
			return GET_REF(pState->nemesisInfo.eaNemesisTeam[iIndex]->hCritterGroup);
		}
	}

	return NULL;
}

const char *Nemesis_GetCostumeAtIndex(S32 iPartitionIdx, S32 iIndex, bool bUseLeader)
{
	MapState *pState = mapState_FromPartitionIdx(iPartitionIdx);
	if(bUseLeader)
	{
		iIndex = Nemesis_GetLeaderIndex(pState);
	}
	if(pState && iIndex >=0 && iIndex < eaSize(&pState->nemesisInfo.eaNemesisTeam))
	{
		if(pState->nemesisInfo.eaNemesisTeam[iIndex]->iId > 0 || pState->nemesisInfo.eaNemesisTeam[iIndex]->bNoNemesis)
		{
			return pState->nemesisInfo.eaNemesisTeam[iIndex]->pchNemesisCostumeSet;
		}
	}

	return NULL;
}

// Get the NemesisTeamStruct at index (or use leader). Returns NULL if bad index or has never been set
const NemesisTeamStruct *Nemesis_GetTeamStructAtIndex(S32 iPartitionIdx, S32 iIndex, bool bUseLeader)
{
	MapState *pState = mapState_FromPartitionIdx(iPartitionIdx);
	if(bUseLeader)
	{
		iIndex = Nemesis_GetLeaderIndex(pState);
	}
	if(pState && iIndex >=0 && iIndex < eaSize(&pState->nemesisInfo.eaNemesisTeam) &&
		(pState->nemesisInfo.eaNemesisTeam[iIndex]->iId > 0 || pState->nemesisInfo.eaNemesisTeam[iIndex]->bNoNemesis))
	{
		return pState->nemesisInfo.eaNemesisTeam[iIndex];
	}

	return NULL;
}

// Test the temal leader feature for nemesis
AUTO_COMMAND ACMD_ACCESSLEVEL(4);
void NemesisTestTeamLeader(Entity *pEntity, int bAnyMember)
{
	if(pEntity)
	{
		Entity *pNemesis = Nemesis_TeamGetTeamLeader(pEntity, bAnyMember);
		if(pNemesis)
		{
			printf("Got team leader nemesis %s.", pNemesis->debugName);
		}
		else
		{
			printf("No team leader nemesis yet.");
		}
	}
}

// 
AUTO_COMMAND ACMD_ACCESSLEVEL(9);
void NemesisInitPartition(Entity *pEntity)
{
	if(pEntity)
	{
		MapState *pState = mapState_FromEnt(pEntity);
		if(pState)
		{
			StructReset(parse_NemesisInfoStruct, &pState->nemesisInfo);
		}
	}
}


