/***************************************************************************
*     Copyright (c) 2008, Cryptic Studios
*     All Rights Reserved
*     Confidential Property of Cryptic Studios
***************************************************************************/

#include "ActivityLogCommon.h"
#include "AutoTransDefs.h"
#include "Color.h"
#include "contact_common.h"
#include "earray.h"
#include "entity.h"
#include "entityiterator.h"
#include "entitylib.h"
#include "EntityMailCommon.h"
#include "expression.h"
#include "GameAccountDataCommon.h"
#include "gameaction_common.h"
#include "gamestringformat.h"
#include "gslActivityLog.h"
#include "gslContact.h"
#include "gslEncounter.h"
#include "gslEntity.h"
#include "gslGameAction.h"
#include "gslGuild.h"
#include "gslItemAssignments.h"
#include "LoggedTransactions.h"
#include "gslMailNPC.h"
#include "gslMapVariable.h"
#include "gslMission.h"
#include "gslMission_transact.h"
#include "gslPartition.h"
#include "gslSendToClient.h"
#include "ShardVariable_Transact.h"
#include "gslSpawnPoint.h"
#include "gslWorldVariable.h"
#include "Guild.h"
#include "ItemAssignments.h"
#include "itemCommon.h"
#include "mission_common.h"
#include "nemesis.h"
#include "NotifyCommon.h"
#include "objpath.h"
#include "objtransactions.h"
#include "Player.h"
#include "rand.h"
#include "ReferenceSystem.h"
#include "ShardVariableCommon.h"
#include "StringCache.h"
#include "transactionsystem.h"
#include "WorldVariable.h"
#include "WorldGrid.h"

#include "Entity_h_ast.h"
#include "Player_h_ast.h"
#include "Autogen/NotifyEnum_h_ast.h"
#include "AutoGen/GameAccountData_h_ast.h"
#include "AutoGen/gameaction_common_h_ast.h"
#include "AutoGen/Guild_h_ast.h"
#include "AutoGen/itemCommon_h_ast.h"
#include "AutoGen/mission_common_h_ast.h"
#include "AutoGen/GameServerLib_autogen_RemoteFuncs.h"
#include "autogen/gameserverlib_autotransactions_autogen_wrappers.h"
#include "AutoGen/GameClientLib_autogen_ClientCmdWrappers.h"
#include "AutoGen/contact_common_h_ast.h"

#include "AutoGen/inventoryCommon_h_ast.h"

#include "AutoGen/ShardVariableCommon_h_ast.h"

AUTO_RUN_ANON(memBudgetAddMapping(__FILE__, BUDGET_GameSystems););


//----------------------------------------------------------------
// Forward Declarations
//----------------------------------------------------------------


enumTransactionOutcome gameaction_trh_GrantMissions(ATR_ARGS, ATH_ARG NOCONST(Entity)* ent, ATH_ARG CONST_EARRAY_OF(NOCONST(ShardVariableContainer)) eaVarContainer, ATH_ARG NOCONST(Guild)* pGuild, ATH_ARG NOCONST(Mission)* pMission, MissionDef *pDef, CONST_EARRAY_OF(WorldGameActionProperties)* actions, WorldVariableArray* pMapVariables);
enumTransactionOutcome gameaction_trh_GrantSubMissions(ATR_ARGS, ATH_ARG NOCONST(Entity)* ent, ATH_ARG CONST_EARRAY_OF(NOCONST(ShardVariableContainer)) eaVarContainer, ATH_ARG NOCONST(Guild)* pGuild, ATH_ARG NOCONST(Mission)* pMission, MissionDef *pDef, CONST_EARRAY_OF(WorldGameActionProperties)* actions, WorldVariableArray* pMapVariables, GameActionDoorDestinationVarArray* pDoorDestVarArray, const char* pchInitMapVars, const char* pchMapName, const ItemChangeReason *pReason, GameAccountDataExtract *pExtract);
enumTransactionOutcome gameaction_trh_GrantSubMissionsNoLocking(ATR_ARGS, ATH_ARG NOCONST(Entity)* ent, ATH_ARG NOCONST(Mission)* pMission, MissionDef *pDef, MissionDef *pRootDef, CONST_EARRAY_OF(WorldGameActionProperties)* actions);
enumTransactionOutcome gameaction_trh_RunMissionOffers(ATR_ARGS, CONST_EARRAY_OF(WorldGameActionProperties)* actions, ATH_ARG NOCONST(Mission) *pMission, WorldVariableArray* pMapVariables);
enumTransactionOutcome gameaction_trh_DropMissions(ATR_ARGS, ATH_ARG NOCONST(Entity)* ent, CONST_EARRAY_OF(WorldGameActionProperties)* actions, const ItemChangeReason *pReason, GameAccountDataExtract* pExtract);
enumTransactionOutcome gameaction_trh_SendFloaters(ATR_ARGS, CONST_EARRAY_OF(WorldGameActionProperties)* actions);
enumTransactionOutcome gameaction_trh_SendNotifications(ATR_ARGS, CONST_EARRAY_OF(WorldGameActionProperties)* actions);
enumTransactionOutcome gameaction_trh_UpdateItemAssignments(ATR_ARGS, CONST_EARRAY_OF(WorldGameActionProperties)* actions);
enumTransactionOutcome gameaction_trh_RunContacts(ATR_ARGS, CONST_EARRAY_OF(WorldGameActionProperties)* actions);
enumTransactionOutcome gameaction_trh_RunExpressions(ATR_ARGS, CONST_EARRAY_OF(WorldGameActionProperties)* actions);
enumTransactionOutcome gameaction_trh_TakeItems(ATR_ARGS, ATH_ARG NOCONST(Entity)* ent, CONST_EARRAY_OF(WorldGameActionProperties)* actions, const ItemChangeReason *pReason, GameAccountDataExtract *pExtract);
enumTransactionOutcome gameaction_trh_GiveItems(ATR_ARGS, ATH_ARG NOCONST(Entity)* ent, CONST_EARRAY_OF(WorldGameActionProperties)* actions, const char* pcMission, const ItemChangeReason *pReason, GameAccountDataExtract *pExtract);
enumTransactionOutcome gameaction_trh_GiveDoorKeyItems(ATR_ARGS, ATH_ARG NOCONST(Entity)* ent, CONST_EARRAY_OF(WorldGameActionProperties)* actions, const char* pcMission, WorldVariableArray* pMapVariableArray, GameActionDoorDestinationVarArray* pDoorDestVarArray, const char* pchInitMapVars, const char* pchMapName, const ItemChangeReason *pReason, GameAccountDataExtract *pExtract);
enumTransactionOutcome gameaction_trh_ChangeNemesisState(ATR_ARGS, ATH_ARG NOCONST(Entity)* ent, CONST_EARRAY_OF(WorldGameActionProperties)* actions);
enumTransactionOutcome gameaction_trh_ShardVariables(ATR_ARGS, ATH_ARG CONST_EARRAY_OF(NOCONST(ShardVariableContainer)) eaVarContainer, CONST_EARRAY_OF(WorldGameActionProperties)* actions);
enumTransactionOutcome gameaction_trh_NPCSendEmail(ATR_ARGS, ATH_ARG NOCONST(Entity)* ent, CONST_EARRAY_OF(WorldGameActionProperties)* actions, GameAccountDataExtract *pExtract);
enumTransactionOutcome gameaction_trh_GADAttribValue(ATR_ARGS, ATH_ARG NOCONST(Entity)* ent, CONST_EARRAY_OF(WorldGameActionProperties)* actions);
enumTransactionOutcome gameaction_trh_ActivityLog(ATR_ARGS, ATH_ARG NOCONST(Entity)* ent, CONST_EARRAY_OF(WorldGameActionProperties)* actions);
enumTransactionOutcome gameaction_trh_GuildActivityLog(ATR_ARGS, ATH_ARG NOCONST(Guild)* pGuild, ContainerID subjectID, CONST_EARRAY_OF(WorldGameActionProperties)* actions);
enumTransactionOutcome gameaction_trh_RunGuildStatUpdates(ATR_ARGS, CONST_EARRAY_OF(WorldGameActionProperties)* actions, ATH_ARG NOCONST(Guild)* pGuild);
enumTransactionOutcome gameaction_trh_RunGuildThemeSetOperations(ATR_ARGS, CONST_EARRAY_OF(WorldGameActionProperties)* actions, ATH_ARG NOCONST(Guild)* pGuild);

static void gameaction_SendFloatersNoTrans(Entity* ent, const WorldGameActionBlock* pActionBlock);
static void gameaction_SendNotificationsNoTrans(Entity* ent, const WorldGameActionBlock* pActionBlock);
static void gameaction_RunItemAssignmentUpdatesNoTrans(Entity* ent, const WorldGameActionBlock* pActionBlock);
static void gameaction_RunContactsNoTrans(Entity* ent, const WorldGameActionBlock* pActionBlock);
static void gameaction_RunMissionOffersNoTrans(Entity* ent, const WorldGameActionBlock* pActionBlock, WorldVariableArray* pMapVariables);
static void gameaction_RunWarpsNoTrans(Entity* ent, const WorldGameActionBlock* pActionBlock, int seed);
static void gameaction_RunExpressionsNoTrans(Entity* ent, const WorldGameActionBlock* pActionBlock);
// ----------------------------------------------------------------------------------
// Static Data Initialization
// ----------------------------------------------------------------------------------

static ExprContext *s_pGameActionContext = NULL;


//----------------------------------------------------------------
// Utility
//----------------------------------------------------------------

bool gameaction_MustLockInventory(CONST_EARRAY_OF(WorldGameActionProperties)* pppActions, MissionDef *pRootDef)
{
	int i;
	for (i = 0; i < eaSize(pppActions); i++) {
		WorldGameActionProperties *action = (*pppActions)[i];
		if (action->eActionType == WorldGameActionType_TakeItem) {
			return true;
		}
		if (action->eActionType == WorldGameActionType_GiveItem) {
			return true;
		}
		if (action->eActionType == WorldGameActionType_GiveDoorKeyItem) {
			return true;
		}

		if (action->eActionType == WorldGameActionType_GrantMission) {
			// If anything grants a new root mission, for now, we assume that it locks everything
			return true;
		}
		if (action->eActionType == WorldGameActionType_DropMission) {
			return true;
		}
		
		// Recurse to sub-missions
		if (action->eActionType == WorldGameActionType_GrantSubMission && pRootDef && action->pGrantSubMissionProperties) {
			MissionDef *pChildDef = missiondef_ChildDefFromNamePooled(pRootDef, action->pGrantSubMissionProperties->pcSubMissionName);
			if (pChildDef && missiondef_MustLockInventoryOnStart(pChildDef, pRootDef)) {
				return true;
			}
		}
	}
	return false;
}


bool gameaction_MustLockNPCEMail(CONST_EARRAY_OF(WorldGameActionProperties)* pppActions, MissionDef *pRootDef)
{
	int i;
	for (i = 0; i < eaSize(pppActions); i++) {
		WorldGameActionProperties *action = (*pppActions)[i];
		if (action->eActionType == WorldGameActionType_NPCSendMail) {
			return true;
		}

		if (action->eActionType == WorldGameActionType_GrantMission) {
			// If anything grants a new root mission, for now, we assume that it locks everything
			return true;
		}

		// Recurse to sub-missions
		if (action->eActionType == WorldGameActionType_GrantSubMission && pRootDef && action->pGrantSubMissionProperties) {
			MissionDef *pChildDef = missiondef_ChildDefFromNamePooled(pRootDef, action->pGrantSubMissionProperties->pcSubMissionName);
			if (pChildDef && missiondef_MustLockNPCEMailOnStart(pChildDef, pRootDef)) {
				return true;
			}
		}
	}
	return false;
}


bool gameaction_MustLockMissions(CONST_EARRAY_OF(WorldGameActionProperties)* pppActions, MissionDef *pRootDef)
{
	int i;
	for (i = 0; i < eaSize(pppActions); i++) {
		WorldGameActionProperties *action = (*pppActions)[i];
		if (action->eActionType == WorldGameActionType_GrantMission ||
			action->eActionType == WorldGameActionType_MissionOffer ||
			action->eActionType == WorldGameActionType_DropMission) {
			return true;
		}

		// Recurse to sub-missions
		if (action->eActionType == WorldGameActionType_GrantSubMission && pRootDef && action->pGrantSubMissionProperties) {
			MissionDef *pChildDef = missiondef_ChildDefFromNamePooled(pRootDef, action->pGrantSubMissionProperties->pcSubMissionName);
			if (pChildDef && missiondef_MustLockMissionsOnStart(pChildDef, pRootDef)) {
				return true;
			}
		}
	}
	return false;
}


bool gameaction_MustLockNemesis(CONST_EARRAY_OF(WorldGameActionProperties)* pppActions, MissionDef *pRootDef)
{
	int i;
	for (i = 0; i < eaSize(pppActions); i++) {
		WorldGameActionProperties *action = (*pppActions)[i];
		if (action->eActionType == WorldGameActionType_ChangeNemesisState) {
			return true;
		}

		if (action->eActionType == WorldGameActionType_GrantMission) {
			// If anything grants a new root mission, for now, we assume that it locks everything
			return true;
		}

		// Recurse to sub-missions
		if (action->eActionType == WorldGameActionType_GrantSubMission && pRootDef && action->pGrantSubMissionProperties){
			MissionDef *pChildDef = missiondef_ChildDefFromNamePooled(pRootDef, action->pGrantSubMissionProperties->pcSubMissionName);
			if (pChildDef && missiondef_MustLockNemesisOnStart(pChildDef, pRootDef)) {
				return true;
			}
		}
	}
	return false;
}


bool gameaction_MustLockGameAccount(CONST_EARRAY_OF(WorldGameActionProperties)* pppActions, MissionDef *pRootDef)
{
	int i;
	for (i = 0; i < eaSize(pppActions); i++) {
		WorldGameActionProperties *action = (*pppActions)[i];
		if (action->eActionType == WorldGameActionType_GADAttribValue) {
			return true;
		}

		if (action->eActionType == WorldGameActionType_GrantMission) {
			// If anything grants a new root mission, for now, we assume that it locks everything
			return true;
		}

		// Recurse to sub-missions
		if (action->eActionType == WorldGameActionType_GrantSubMission && pRootDef && action->pGrantSubMissionProperties) {
			MissionDef *pChildDef = missiondef_ChildDefFromNamePooled(pRootDef, action->pGrantSubMissionProperties->pcSubMissionName);
			if (pChildDef && missiondef_MustLockGameAccountOnStart(pChildDef, pRootDef)) {
				return true;
			}
		}
	}
	return false;
}


bool gameaction_MustLockShardVariables(CONST_EARRAY_OF(WorldGameActionProperties)* pppActions, MissionDef *pRootDef)
{
	int i;
	for (i = 0; i < eaSize(pppActions); i++) {
		WorldGameActionProperties *action = (*pppActions)[i];
		if (action->eActionType == WorldGameActionType_ShardVariable) {
			return true;
		}

		// Recurse to sub-missions
		if (action->eActionType == WorldGameActionType_GrantSubMission && pRootDef && action->pGrantSubMissionProperties) {
			MissionDef *pChildDef = missiondef_ChildDefFromNamePooled(pRootDef, action->pGrantSubMissionProperties->pcSubMissionName);
			if (pChildDef && missiondef_MustLockShardVariablesOnStart(pChildDef, pRootDef)) {
				return true;
			}
		}
	}
	return false;
}


bool gameaction_MustLockActivityLog(CONST_EARRAY_OF(WorldGameActionProperties)* pppActions, MissionDef *pRootDef)
{
	int i;
	for (i = 0; i < eaSize(pppActions); i++) {
		WorldGameActionProperties *action = (*pppActions)[i];
		if (action->eActionType == WorldGameActionType_ActivityLog) {
			return true;
		}

		// Recurse to sub-missions
		if (action->eActionType == WorldGameActionType_GrantSubMission && pRootDef && action->pGrantSubMissionProperties) {
			MissionDef *pChildDef = missiondef_ChildDefFromNamePooled(pRootDef, action->pGrantSubMissionProperties->pcSubMissionName);
			if (pChildDef && missiondef_MustLockActivityLogOnStart(pChildDef, pRootDef)) {
				return true;
			}
		}
	}
	return false;
}


bool gameaction_MustLockGuildActivityLog(bool inGuild, CONST_EARRAY_OF(WorldGameActionProperties)* pppActions, MissionDef *pRootDef)
{
	int i;

	if ( !inGuild ) {
		return false;
	}

	for (i = 0; i < eaSize(pppActions); i++) {
		WorldGameActionProperties *action = (*pppActions)[i];
		if (action->eActionType == WorldGameActionType_ActivityLog) {
			ActivityLogEntryTypeConfig *typeConfig;

			typeConfig = ActivityLog_GetTypeConfig(action->pActivityLogProperties->eEntryType);
			if ( typeConfig && typeConfig->addToGuildLog ) {
				return true;
			}
		}

		// Recurse to sub-missions
		if (action->eActionType == WorldGameActionType_GrantSubMission && pRootDef && action->pGrantSubMissionProperties) {
			MissionDef *pChildDef = missiondef_ChildDefFromNamePooled(pRootDef, action->pGrantSubMissionProperties->pcSubMissionName);
			if (pChildDef && missiondef_MustLockGuildActivityLogOnStart(inGuild, pChildDef, pRootDef)) {
				return true;
			}
		}
	}
	return false;
}


AUTO_TRANS_HELPER_SIMPLE;
bool gameaction_MustLockGuild(CONST_EARRAY_OF(WorldGameActionProperties)* pppActions, MissionDef *pRootDef)
{
	int i;
	for (i = 0; i < eaSize(pppActions); i++) {
		WorldGameActionProperties *action = (*pppActions)[i];
		if (action->eActionType == WorldGameActionType_GuildStatUpdate || action->eActionType == WorldGameActionType_GuildThemeSet) {
			return true;
		}

		// Recurse to sub-missions
		if (action->eActionType == WorldGameActionType_GrantSubMission && pRootDef && action->pGrantSubMissionProperties) {
			MissionDef *pChildDef = missiondef_ChildDefFromNamePooled(pRootDef, action->pGrantSubMissionProperties->pcSubMissionName);
			if (pChildDef && missiondef_MustLockGuildOnStart(pChildDef, pRootDef)) {
				return true;
			}
		}
	}
	return false;
}


bool gameaction_PlayerIsEligible(Entity *pEnt, const WorldGameActionBlock *pActionBlock)
{
	int i;
	PERFINFO_AUTO_START_FUNC();

	for (i = 0; i < eaSize(&pActionBlock->eaActions); i++) {
		WorldGameActionProperties *pAction = pActionBlock->eaActions[i];

		// For now this only handles Mission Offers and Contacts
		switch(pAction->eActionType)
		{
		xcase WorldGameActionType_MissionOffer:
			if (pAction->pMissionOfferProperties) {
				MissionDef *pDef = GET_REF(pAction->pMissionOfferProperties->hMissionDef);
				if (!pDef || !missiondef_CanBeOfferedAsPrimary(pEnt, pDef, NULL, NULL)) {
					PERFINFO_AUTO_STOP();
					return false;
				}
			}
		xcase WorldGameActionType_Contact:
			if (pAction->pContactProperties) {
				ContactDef *pDef = GET_REF(pAction->pContactProperties->hContactDef);
				if (!pDef || !contact_CanInteract(pDef, pEnt)) {
					PERFINFO_AUTO_STOP();
					return false;
				}
			}
		}
	}

	PERFINFO_AUTO_STOP();
	return true;
}


// TRUE if the gameaction block can provide a Display String that describes itself
bool gameaction_CanProvideDisplayString(const WorldGameActionBlock *pActionBlock)
{
	// Must have exactly one Action
	if (eaSize(&pActionBlock->eaActions) != 1) {
		return false;
	}

	// For now, only Mission Offers can determine their own Display String
	if ((pActionBlock->eaActions[0]->eActionType == WorldGameActionType_MissionOffer)
		&& pActionBlock->eaActions[0]->pMissionOfferProperties ) {
		MissionDef *pDef = GET_REF(pActionBlock->eaActions[0]->pMissionOfferProperties->hMissionDef);
		if (pDef && GET_REF(pDef->displayNameMsg.hMessage)) {
			return true;
		}
	}

	return false;
}


// Gets an autogenerated Display String for this GameActionBlock
// May return false if this GameActionBlock can't determine a display string for itself
// (in which case validation should have failed)
bool gameaction_GetDisplayString(Entity *pEnt, const WorldGameActionBlock *pActionBlock, char** estrBuffer)
{
	// Must have exactly one Action
	if (eaSize(&pActionBlock->eaActions) != 1) {
		return false;
	}

	// For now, only Mission Offers can determine their own Display String
	if ((pActionBlock->eaActions[0]->eActionType == WorldGameActionType_MissionOffer)
		&& pActionBlock->eaActions[0]->pMissionOfferProperties ) {
		MissionDef *pDef = GET_REF(pActionBlock->eaActions[0]->pMissionOfferProperties->hMissionDef);
		if (pDef && pEnt) {
			estrClear(estrBuffer);
			langFormatGameMessage(entGetLanguage(pEnt), estrBuffer, GET_REF(pDef->displayNameMsg.hMessage), STRFMT_ENTITY_KEY("Entity", pEnt), STRFMT_END);
			return true;
		}
	}

	return false;
}


void gameaction_GenerateExpression(WorldGameActionProperties *pAction)
{
	exprGenerate(pAction->pExpressionProperties->pExpression, s_pGameActionContext);
}


//----------------------------------------------------------------
// Functions that run a Transaction to execute all Actions
//----------------------------------------------------------------

// Struct used to pass data to the transaction return callback
typedef struct GameActionTransactStruct
{
	EntityRef entRef;
	GameActionExecuteCB callback;
	void *pData;
} GameActionTransactStruct;


// Return callback
void gameaction_RunActionsReturn(TransactionReturnVal* returnVal, GameActionTransactStruct* transactStruct)
{
	Entity *pEnt = entFromEntityRefAnyPartition(transactStruct->entRef);
	if (transactStruct->callback) {
		transactStruct->callback(returnVal->eOutcome, pEnt, transactStruct->pData);
	}
	free(transactStruct);
}


// Actual transactions -- several versions based on how much locking is needed
AUTO_TRANSACTION
	ATR_LOCKS(ent, ".Pplayer.Skilltype, .Pplayer.Eskillspecialization, .Pchar.Hclass, .Pchar.Ppsecondarypaths, .Pchar.Hpath, .Psaved.pSCPdata.Isummonedscp, .Psaved.pSCPdata.Fcachedpetbonusxppct, .Psaved.pSCPdata.erSCP, .Psaved.pSCPdata.Isummonedscp, .Psaved.pSCPdata.Fcachedpetbonusxppct, .Pplayer.Pugckillcreditlimit, .Pplayer.Pplayeraccountdata.Eapendingkeys, .Psaved.Nextactivitylogid, .Hallegiance, .Hsuballegiance, .Pchar.Ilevelexp, .Pplayer.Pplayeraccountdata.Eagamepermissionmaxvaluenumerics, .Psaved.Activitylogentries[AO], .Psaved.Uiindexbuild, .Psaved.Ppbuilds, .Psaved.Conowner.Containerid, .Psaved.Conowner.Containertype, .Pinventoryv2.Peaowneduniqueitems, .Pinventoryv2.Ppinventorybags, .Pinventoryv2.Pplitebags, .Pplayer.Playertype, .Psaved.Ppownedcontainers, .Itemidmax, .Pchar.Uipoweridmax, .Psaved.Ipetid, .Psaved.Ppuppetmaster.Curid, .Psaved.Ppuppetmaster.Curtype, .Psaved.Ppuppetmaster.Curtempid, .Psaved.Ppallowedcritterpets, .Pplayer.Missioninfo.Missions, .Pplayer.Missioninfo.Completedmissions, .Pplayer.Missioninfo.Eamissionrequests, .Pplayer.Missioninfo.Eamissioncooldowns, .Pplayer.Nemesisinfo.Eanemesisstates, .Pplayer.pEmailV2.Ilastusedid, .Pplayer.Langid, .Pplayer.pEmailV2.Mail")
	ATR_LOCKS(eaVarContainer, ".id ,Uclock, .Eaworldvars")
	ATR_LOCKS(pGuild, ".Inextactivitylogentryid, .Eaactivityentries, .Eamembers, .Pguildstatsinfo, .Htheme");
enumTransactionOutcome gameaction_tr_RunActionsLockAll(ATR_ARGS, NOCONST(Entity)* ent, CONST_EARRAY_OF(NOCONST(ShardVariableContainer)) eaVarContainer, NOCONST(Guild) *pGuild, const WorldGameActionBlock* pActionBlock, WorldVariableArray* pMapVariableArray, GameActionDoorDestinationVarArray* pDoorDestVarArray, const char* pchInitMapVars, const char* pchMapName, const ItemChangeReason *pReason, GameAccountDataExtract *pExtract)
{
	return gameaction_trh_RunActionsLockAll(ATR_PASS_ARGS, ent, eaVarContainer, pGuild, &pActionBlock->eaActions, pMapVariableArray, pDoorDestVarArray, pchInitMapVars, pchMapName, pReason, pExtract);
}


AUTO_TRANSACTION
ATR_LOCKS(eaVarContainer, ".id, .Uclock, .Eaworldvars");
enumTransactionOutcome gameaction_tr_RunActionsLockVarsOnly(ATR_ARGS, CONST_EARRAY_OF(NOCONST(ShardVariableContainer)) eaVarContainer, const WorldGameActionBlock* pActionBlock)
{
	return gameaction_trh_RunActionsLockVarsOnly(ATR_PASS_ARGS, eaVarContainer, &pActionBlock->eaActions);
}


AUTO_TRANSACTION
ATR_LOCKS(ent, ".Pplayer.Skilltype, .Pplayer.Eskillspecialization, .Pchar.Hclass, .Pchar.Ppsecondarypaths, .Pchar.Hpath, .Psaved.pSCPdata.Isummonedscp, .Psaved.pSCPdata.Fcachedpetbonusxppct, .Psaved.pSCPdata.erSCP, .Hallegiance, .Hsuballegiance, .Pplayer.Pplayeraccountdata.Eapendingkeys, .Psaved.Nextactivitylogid, .Psaved.Activitylogentries[AO], .Itemidmax, .Pplayer.Pugckillcreditlimit, .Pplayer.Pplayeraccountdata.Eagamepermissionmaxvaluenumerics, .Pchar.Ilevelexp, .Pinventoryv2.Peaowneduniqueitems, .Pinventoryv2.Ppinventorybags, .Pinventoryv2.Pplitebags, .Psaved.Conowner.Containerid, .Psaved.Conowner.Containertype, .Psaved.Uiindexbuild, .Psaved.Ppbuilds, .Psaved.Ppownedcontainers, .Pchar.Uipoweridmax, .Psaved.Ipetid, .Psaved.Ppuppetmaster.Curid, .Psaved.Ppuppetmaster.Curtype, .Psaved.Ppuppetmaster.Curtempid, .Psaved.Ppallowedcritterpets, .Pplayer.Nemesisinfo.Eanemesisstates, .Pplayer.pEmailV2.Ilastusedid, .Pplayer.Langid, .Pplayer.pEmailV2.Mail, .Pplayer.Playertype, .Pplayer.Missioninfo.Missions, .Pplayer.Missioninfo.Completedmissions, .Pplayer.Missioninfo.Eamissionrequests, .Pplayer.Missioninfo.Eamissioncooldowns");
enumTransactionOutcome gameaction_tr_RunActionsLockAllEntity(ATR_ARGS, NOCONST(Entity)* ent, const WorldGameActionBlock* pActionBlock, WorldVariableArray* pVariables, GameActionDoorDestinationVarArray* pDoorDestVarArray, const char* pchInitMapVars, const char* pchMapName, const ItemChangeReason *pReason, GameAccountDataExtract *pExtract)
{
	return gameaction_trh_RunActionsLockAllEntity(ATR_PASS_ARGS, ent, &pActionBlock->eaActions, pVariables, pDoorDestVarArray, pchInitMapVars, pchMapName, pReason, pExtract);
}


AUTO_TRANSACTION
ATR_LOCKS(ent, ".Pplayer.Skilltype, .Pplayer.Eskillspecialization, .Pchar.Hclass, .Pchar.Ppsecondarypaths, .Pchar.Hpath, .Psaved.pSCPdata.Isummonedscp, .Psaved.pSCPdata.Fcachedpetbonusxppct, .Psaved.pSCPdata.erSCP, .Hallegiance, .Hsuballegiance, .Pplayer.Pplayeraccountdata.Eapendingkeys, .Psaved.Nextactivitylogid, .Psaved.Activitylogentries[AO], .Itemidmax, .Pplayer.Pugckillcreditlimit, .Pplayer.Pplayeraccountdata.Eagamepermissionmaxvaluenumerics, .Pchar.Ilevelexp, .Pinventoryv2.Peaowneduniqueitems, .Pinventoryv2.Ppinventorybags, .Pinventoryv2.Pplitebags, .Psaved.Conowner.Containerid, .Psaved.Conowner.Containertype, .Psaved.Uiindexbuild, .Psaved.Ppbuilds, .Psaved.Ppownedcontainers, .Pchar.Uipoweridmax, .Psaved.Ipetid, .Psaved.Ppuppetmaster.Curid, .Psaved.Ppuppetmaster.Curtype, .Psaved.Ppuppetmaster.Curtempid, .Psaved.Ppallowedcritterpets, .Pplayer.Nemesisinfo.Eanemesisstates, .Pplayer.pEmailV2.Ilastusedid, .Pplayer.Langid, .Pplayer.pEmailV2.Mail, .Pplayer.Playertype, .Pplayer.Missioninfo.Missions, .Pplayer.Missioninfo.Completedmissions, .Pplayer.Missioninfo.Eamissionrequests, .Pplayer.Missioninfo.Eamissioncooldowns")
ATR_LOCKS(pGuild, ".Inextactivitylogentryid, .Eaactivityentries, .Eamembers, .Pguildstatsinfo, .Htheme");
enumTransactionOutcome gameaction_tr_RunActionsLockAllEntityWithGuild(ATR_ARGS, NOCONST(Entity)* ent, NOCONST(Guild)* pGuild, const WorldGameActionBlock* pActionBlock, WorldVariableArray* pMapVariableArray, GameActionDoorDestinationVarArray* pDoorDestVarArray, const char* pchInitMapVars, const char* pchMapName, const ItemChangeReason *pReason, GameAccountDataExtract *pExtract)
{
	return gameaction_trh_RunActionsLockAllEntityWithGuild(ATR_PASS_ARGS, ent, pGuild, &pActionBlock->eaActions, pMapVariableArray, pDoorDestVarArray, pchInitMapVars, pchMapName, pReason, pExtract);
}


AUTO_TRANSACTION
ATR_LOCKS(ent, ".Pplayer.Nemesisinfo.Eanemesisstates, .Pplayer.pEmailV2.Mail, .Pplayer.pEmailV2.Ilastusedid, .Pplayer.Langid, .Hallegiance, .Hsuballegiance, .Pplayer.Pplayeraccountdata.Eagamepermissionmaxvaluenumerics, .Pchar.Ilevelexp");
enumTransactionOutcome gameaction_tr_RunActionsNoInventory(ATR_ARGS, NOCONST(Entity)* ent, const WorldGameActionBlock* pActionBlock, WorldVariableArray *pMapVariableArray, GameAccountDataExtract *pExtract)
{
	return gameaction_trh_RunActionsNoInventory(ATR_PASS_ARGS, ent, &pActionBlock->eaActions, pMapVariableArray, pExtract);
}


AUTO_TRANSACTION
ATR_LOCKS(ent, ".Pplayer.Skilltype, .Pplayer.Eskillspecialization, .Pchar.Hclass, .Pchar.Ppsecondarypaths, .Pchar.Hpath, .Psaved.pSCPdata.Isummonedscp, .Psaved.pSCPdata.Fcachedpetbonusxppct, .Psaved.pSCPdata.erSCP, .pInventoryV2.Peaowneduniqueitems, .pInventoryV2.Pplitebags, .Hallegiance, .Hsuballegiance, .Pplayer.pEmailV2.Ilastusedid, .Pplayer.Langid, .Pplayer.pEmailV2.Mail, .Itemidmax, .Pplayer.Pugckillcreditlimit, .Pplayer.Pplayeraccountdata.Eagamepermissionmaxvaluenumerics, .Pchar.Ilevelexp, .pInventoryV2.Ppinventorybags, .Psaved.Conowner.Containerid, .Psaved.Conowner.Containertype, .Psaved.Uiindexbuild, .Psaved.Ppbuilds, .Psaved.Ppownedcontainers, .Pchar.Uipoweridmax, .Psaved.Ipetid, .Psaved.Ppuppetmaster.Curid, .Psaved.Ppuppetmaster.Curtype, .Psaved.Ppuppetmaster.Curtempid, .Psaved.Ppallowedcritterpets, .Pplayer.Nemesisinfo.Eanemesisstates, .Pplayer.Playertype");
enumTransactionOutcome gameaction_tr_RunActionsNoMissions(ATR_ARGS, NOCONST(Entity)* ent, const WorldGameActionBlock* pActionBlock, WorldVariableArray* pMapVariableArray, GameActionDoorDestinationVarArray* pDoorDestVarArray, const char* pchInitMapVars, const char* pchMapName, const ItemChangeReason *pReason, GameAccountDataExtract *pExtract)
{
	return gameaction_trh_RunActionsNoMissions(ATR_PASS_ARGS, ent, &pActionBlock->eaActions, pMapVariableArray, pDoorDestVarArray, pchInitMapVars, pchMapName, pReason, pExtract);
} 


AUTO_TRANSACTION
ATR_LOCKS(ent, ".Pplayer.Skilltype, .Pplayer.Eskillspecialization, .Pchar.Hclass, .Pchar.Ppsecondarypaths, .Pchar.Hpath, .Psaved.pSCPdata.Isummonedscp, .Psaved.pSCPdata.Fcachedpetbonusxppct, .Psaved.pSCPdata.erSCP, .pInventoryV2.Peaowneduniqueitems, .pInventoryV2.Pplitebags, .Hallegiance, .Hsuballegiance, .Pplayer.pEmailV2.Ilastusedid, .Pplayer.Langid, .Pplayer.pEmailV2.Mail, .Itemidmax, .Pplayer.Pugckillcreditlimit, .Pplayer.Pplayeraccountdata.Eagamepermissionmaxvaluenumerics, .Pchar.Ilevelexp, .pInventoryV2.Ppinventorybags, .Psaved.Conowner.Containerid, .Psaved.Conowner.Containertype, .Psaved.Uiindexbuild, .Psaved.Ppbuilds, .Psaved.Ppownedcontainers, .Pchar.Uipoweridmax, .Psaved.Ipetid, .Psaved.Ppuppetmaster.Curid, .Psaved.Ppuppetmaster.Curtype, .Psaved.Ppuppetmaster.Curtempid, .Psaved.Ppallowedcritterpets, .Pplayer.Playertype");
enumTransactionOutcome gameaction_tr_RunActionsNoNemesis(ATR_ARGS, NOCONST(Entity)* ent, const WorldGameActionBlock* pActionBlock, WorldVariableArray* pMapVariableArray, GameActionDoorDestinationVarArray* pDoorDestVarArray, const char* pchInitMapVars, const char* pchMapName, const ItemChangeReason *pReason, GameAccountDataExtract *pExtract)
{
	return gameaction_trh_RunActionsNoNemesis(ATR_PASS_ARGS, ent, &pActionBlock->eaActions, pMapVariableArray, pDoorDestVarArray, pchInitMapVars, pchMapName, pReason, pExtract);
}


AUTO_TRANSACTION
ATR_LOCKS(ent, ".Pplayer.Nemesisinfo.Eanemesisstates, .Pplayer.pEmailV2.Mail, .Pplayer.pEmailV2.Ilastusedid, .Pplayer.Langid, .Hallegiance, .Hsuballegiance, .Pplayer.Pplayeraccountdata.Eagamepermissionmaxvaluenumerics, .Pchar.Ilevelexp");
enumTransactionOutcome gameaction_tr_RunActionsNoInventoryOrMissions(ATR_ARGS, NOCONST(Entity)* ent, const WorldGameActionBlock* pActionBlock, GameAccountDataExtract *pExtract)
{
	return gameaction_trh_RunActionsNoInventoryOrMissions(ATR_PASS_ARGS, ent, &pActionBlock->eaActions, pExtract);
}


AUTO_TRANSACTION
ATR_LOCKS(ent, ".Pplayer.pEmailV2.Mail, .Pplayer.pEmailV2.Ilastusedid, .Pplayer.Langid, .Hallegiance, .Hsuballegiance, .Pplayer.Pplayeraccountdata.Eagamepermissionmaxvaluenumerics, .Pchar.Ilevelexp");
enumTransactionOutcome gameaction_tr_RunActionsNoInventoryOrNemesis(ATR_ARGS, NOCONST(Entity)* ent, const WorldGameActionBlock* pActionBlock, WorldVariableArray *pMapVariableArray, GameAccountDataExtract *pExtract)
{
	return gameaction_trh_RunActionsNoInventoryOrNemesis(ATR_PASS_ARGS, ent, &pActionBlock->eaActions, pMapVariableArray, pExtract);
}


AUTO_TRANSACTION
ATR_LOCKS(ent, ".Pplayer.Skilltype, .Pplayer.Eskillspecialization, .Pchar.Hclass, .Pchar.Ppsecondarypaths, .Pchar.Hpath, .Psaved.pSCPdata.Isummonedscp, .Psaved.pSCPdata.Fcachedpetbonusxppct, .Psaved.pSCPdata.erSCP, .pInventoryV2.Peaowneduniqueitems, .pInventoryV2.Pplitebags, .Hallegiance, .Hsuballegiance, .Pplayer.pEmailV2.Ilastusedid, .Pplayer.Langid, .Pplayer.pEmailV2.Mail, .Itemidmax, .Pplayer.Pugckillcreditlimit, .Pplayer.Pplayeraccountdata.Eagamepermissionmaxvaluenumerics, .Pchar.Ilevelexp, .pInventoryV2.Ppinventorybags, .Psaved.Conowner.Containerid, .Psaved.Conowner.Containertype, .Psaved.Uiindexbuild, .Psaved.Ppbuilds, .Psaved.Ppownedcontainers, .Pchar.Uipoweridmax, .Psaved.Ipetid, .Psaved.Ppuppetmaster.Curid, .Psaved.Ppuppetmaster.Curtype, .Psaved.Ppuppetmaster.Curtempid, .Psaved.Ppallowedcritterpets, .Pplayer.Playertype");
enumTransactionOutcome gameaction_tr_RunActionsNoMissionsOrNemesis(ATR_ARGS, NOCONST(Entity)* ent, const WorldGameActionBlock* pActionBlock, WorldVariableArray* pMapVariableArray, GameActionDoorDestinationVarArray* pDoorDestVarArray, const char* pchInitMapVars, const char* pchMapName, const ItemChangeReason *pReason, GameAccountDataExtract *pExtract)
{
	return gameaction_trh_RunActionsNoMissionsOrNemesis(ATR_PASS_ARGS, ent, &pActionBlock->eaActions, pMapVariableArray, pDoorDestVarArray, pchInitMapVars, pchMapName, pReason, pExtract);
} 


AUTO_TRANSACTION
ATR_LOCKS(ent, ".Pplayer.pEmailV2.Mail, .Pplayer.pEmailV2.Ilastusedid, .Pplayer.Langid, .Hallegiance, .Hsuballegiance, .Pplayer.Pplayeraccountdata.Eagamepermissionmaxvaluenumerics, .Pchar.Ilevelexp");
enumTransactionOutcome gameaction_tr_RunActionsLockNPCEMailOnly(ATR_ARGS, NOCONST(Entity)* ent, const WorldGameActionBlock* pActionBlock, GameAccountDataExtract *pExtract)
{
	return gameaction_trh_RunActionsLockNPCEMailOnly(ATR_PASS_ARGS, ent, &pActionBlock->eaActions, pExtract);
}


AUTO_TRANSACTION
ATR_LOCKS(ent, ".Psaved.Nextactivitylogid, .Psaved.Activitylogentries[AO]");
enumTransactionOutcome gameaction_tr_RunActionsLockActivityLogOnly(ATR_ARGS, NOCONST(Entity)* ent, const WorldGameActionBlock* pActionBlock)
{
	return gameaction_trh_RunActionsLockActivityLogOnly(ATR_PASS_ARGS, ent, &pActionBlock->eaActions);
}


AUTO_TRANSACTION
ATR_LOCKS(ent, ".Psaved.Nextactivitylogid, .Psaved.Activitylogentries[AO]")
ATR_LOCKS(pGuild, ".Inextactivitylogentryid, .Eaactivityentries, .Eamembers");
enumTransactionOutcome gameaction_tr_RunActionsLockActivityLogWithGuildOnly(ATR_ARGS, NOCONST(Entity)* ent, NOCONST(Guild) *pGuild, const WorldGameActionBlock* pActionBlock)
{
	return gameaction_trh_RunActionsLockActivityLogWithGuildOnly(ATR_PASS_ARGS, ent, pGuild, &pActionBlock->eaActions);
}


AUTO_TRANSACTION
ATR_LOCKS(pGuild, ".Pguildstatsinfo, .Htheme");
enumTransactionOutcome gameaction_tr_RunActionsLockGuildOnly(ATR_ARGS, NOCONST(Guild) *pGuild, const WorldGameActionBlock* pActionBlock)
{
	return gameaction_trh_RunActionsLockGuildOnly(ATR_PASS_ARGS, pGuild, &pActionBlock->eaActions);
}


void gameaction_RunActionsNoLocking(GameActionTransactStruct* returnStruct, Entity* ent, const WorldGameActionBlock* pActionBlock, WorldVariableArray *pMapVariableArray)
{
	int seed = randomInt();
	
	// The outcome is all of the return val struct that's used by the RunActionsReturn callback
	TransactionReturnVal dummyReturnValStruct;
	dummyReturnValStruct.eOutcome = TRANSACTION_OUTCOME_SUCCESS;

	// Do non-transactional actions
	gameaction_SendFloatersNoTrans(ent, pActionBlock);
	gameaction_SendNotificationsNoTrans(ent, pActionBlock);
	gameaction_RunItemAssignmentUpdatesNoTrans(ent, pActionBlock);
	gameaction_RunContactsNoTrans(ent, pActionBlock);
	gameaction_RunMissionOffersNoTrans(ent, pActionBlock, pMapVariableArray);
	gameaction_RunExpressionsNoTrans(ent, pActionBlock);
	gameaction_RunWarpsNoTrans(ent, pActionBlock, seed);

	gameaction_RunActionsReturn(&dummyReturnValStruct, returnStruct);
}

GameActionDoorDestinationVarArray* gameaction_GenerateDoorDestinationVariables(int iPartitionIdx, const WorldGameActionProperties** eaActions)
{
	GameActionDoorDestinationVarArray* pVarArray = StructCreate(parse_GameActionDoorDestinationVarArray);
	int i;
	for (i = 0; i < eaSize(&eaActions); i++) {
		const WorldGameActionProperties* pAction = eaActions[i];
		if (pAction->pGiveDoorKeyItemProperties && pAction->pGiveDoorKeyItemProperties->pDestinationMap) {
			WorldVariable* pVar = worldVariableCalcVariableAndAlloc(iPartitionIdx, pAction->pGiveDoorKeyItemProperties->pDestinationMap, NULL, randomInt(), 0);
			if (pVar) {
				GameActionDoorDestinationVariable* pGameActionVar = StructCreate(parse_GameActionDoorDestinationVariable);
				pGameActionVar->pDoorDestination = pVar;
				pGameActionVar->iGameActionIndex = i;

				if (pVar->pcZoneMap && pAction->pGiveDoorKeyItemProperties->eaVariableDefs) {
					WorldVariable** eaMapVars = worldVariableCalcVariablesAndAlloc(iPartitionIdx, pAction->pGiveDoorKeyItemProperties->eaVariableDefs, NULL, randomInt(), 0);
					pGameActionVar->pchMapVars = worldVariableArrayToString(eaMapVars);
					eaDestroyStruct(&eaMapVars, parse_WorldVariable);
				}
				eaPush(&pVarArray->eaVariables, pGameActionVar);
			}
		}
	}
	return pVarArray;
}

// Call this to initiate the transaction
void gameaction_RunActions(Entity *pEnt, const WorldGameActionBlock *pActionBlock, const ItemChangeReason *pReason, GameActionExecuteCB callback, void *pData)
{
	if (pEnt && pActionBlock && eaSize(&pActionBlock->eaActions)) {
		GameAccountDataExtract *pExtract = entity_GetCachedGameAccountDataExtract(pEnt);
		GameActionTransactStruct *returnStruct = calloc(1, sizeof(GameActionTransactStruct));
		TransactionReturnVal* returnVal = NULL;
		bool bMustLockInventory, bMustLockMissions, bMustLockNemesis, bMustLockNPCEmail, bMustLockShardVariables, bMustLockActivityLog, bMustLockGuildActivityLog, bMustLockGuild;
		ContainerID iGuildID = 0;
		int iPartitionIdx = entGetPartitionIdx(pEnt);
		WorldVariableArray *pMapVariableArray = mapVariable_GetAllAsWorldVarArray(iPartitionIdx);
		const char* pchInitMapVars = partition_MapVariablesFromIdx(iPartitionIdx);
		const char* pchMapName = zmapInfoGetPublicName(NULL);

		returnStruct->entRef = entGetRef(pEnt);
		returnStruct->callback = callback;
		returnStruct->pData = pData;
		
		returnVal = LoggedTransactions_CreateManagedReturnValEnt("GameAction", pEnt, gameaction_RunActionsReturn, returnStruct);

		// Choose the most efficient version of the transaction
		bMustLockInventory = gameaction_MustLockInventory(&pActionBlock->eaActions, NULL);
		bMustLockMissions = gameaction_MustLockMissions(&pActionBlock->eaActions, NULL);
		bMustLockNemesis = gameaction_MustLockNemesis(&pActionBlock->eaActions, NULL);
		bMustLockNPCEmail = gameaction_MustLockNPCEMail(&pActionBlock->eaActions, NULL);
		bMustLockShardVariables = gameaction_MustLockShardVariables(&pActionBlock->eaActions, NULL);
		bMustLockActivityLog = gameaction_MustLockActivityLog(&pActionBlock->eaActions, NULL);
		bMustLockGuildActivityLog = gameaction_MustLockGuildActivityLog(((NONNULL(pEnt->pPlayer)) && NONNULL(pEnt->pPlayer->pGuild)), &pActionBlock->eaActions, NULL);
		bMustLockGuild = gameaction_MustLockGuild(&pActionBlock->eaActions, NULL);

		// only specify a guild container if we need to lock it
		if (bMustLockGuildActivityLog || bMustLockGuild) {
			PlayerGuild *pPlayerGuild = SAFE_MEMBER2(pEnt, pPlayer, pGuild);
			if (pPlayerGuild) {
				iGuildID = pPlayerGuild->iGuildID;
			}
		}

		if (bMustLockShardVariables && !bMustLockInventory && !bMustLockMissions && !bMustLockNemesis && !bMustLockNPCEmail && !bMustLockActivityLog && !bMustLockGuildActivityLog && !bMustLockGuild) {
			AutoTrans_gameaction_tr_RunActionsLockVarsOnly(returnVal, GetAppGlobalType(), 
					GLOBALTYPE_SHARDVARIABLE, shardVariable_GetContainerIDList(), 
					pActionBlock);

		} else if (bMustLockGuild && !bMustLockInventory && !bMustLockMissions && !bMustLockNemesis && !bMustLockNPCEmail && !bMustLockActivityLog && !bMustLockGuildActivityLog && !bMustLockShardVariables) {
			if (iGuildID) {
				AutoTrans_gameaction_tr_RunActionsLockGuildOnly(returnVal, GetAppGlobalType(), 
						GLOBALTYPE_GUILD, iGuildID, 
						pActionBlock);
			} else if (callback) {
				// Run callback immediately
				callback(TRANSACTION_OUTCOME_SUCCESS, pEnt, pData);
			}

		} else if ( ( bMustLockActivityLog || bMustLockGuildActivityLog ) && !bMustLockInventory && !bMustLockMissions && !bMustLockNemesis && !bMustLockNPCEmail && !bMustLockShardVariables && !bMustLockGuild) {
			if ( bMustLockGuildActivityLog ) {
				AutoTrans_gameaction_tr_RunActionsLockActivityLogWithGuildOnly(returnVal, GetAppGlobalType(), 
					entGetType(pEnt), entGetContainerID(pEnt), 
					GLOBALTYPE_GUILD, iGuildID, 
					pActionBlock);
			} else {
				AutoTrans_gameaction_tr_RunActionsLockActivityLogOnly(returnVal, GetAppGlobalType(), 
					entGetType(pEnt), entGetContainerID(pEnt), 
					pActionBlock);
			}

		} else if (bMustLockShardVariables)	{
			GameActionDoorDestinationVarArray* pDoorDestVarArray = gameaction_GenerateDoorDestinationVariables(iPartitionIdx, pActionBlock->eaActions);
			AutoTrans_gameaction_tr_RunActionsLockAll(returnVal, GetAppGlobalType(), 
				entGetType(pEnt), entGetContainerID(pEnt), 
					GLOBALTYPE_SHARDVARIABLE, shardVariable_GetContainerIDList(), 
													  
				GLOBALTYPE_GUILD, iGuildID, 
				pActionBlock, pMapVariableArray, pDoorDestVarArray, pchInitMapVars, pchMapName, pReason, pExtract);
			StructDestroySafe(parse_GameActionDoorDestinationVarArray, &pDoorDestVarArray);

		} else if (bMustLockGuildActivityLog || bMustLockGuild) {
			GameActionDoorDestinationVarArray* pDoorDestVarArray = gameaction_GenerateDoorDestinationVariables(iPartitionIdx, pActionBlock->eaActions);
			AutoTrans_gameaction_tr_RunActionsLockAllEntityWithGuild(returnVal, GetAppGlobalType(), 
				entGetType(pEnt), entGetContainerID(pEnt), 
				GLOBALTYPE_GUILD, iGuildID, 
				pActionBlock, pMapVariableArray, pDoorDestVarArray, pchInitMapVars, pchMapName, pReason, pExtract);
			StructDestroySafe(parse_GameActionDoorDestinationVarArray, &pDoorDestVarArray);

		} else if ((bMustLockInventory && bMustLockMissions && bMustLockNemesis && bMustLockNPCEmail) || bMustLockActivityLog ) {
			GameActionDoorDestinationVarArray* pDoorDestVarArray = gameaction_GenerateDoorDestinationVariables(iPartitionIdx, pActionBlock->eaActions);
			AutoTrans_gameaction_tr_RunActionsLockAllEntity(returnVal, GetAppGlobalType(), 
				entGetType(pEnt), entGetContainerID(pEnt), 
				pActionBlock, pMapVariableArray, pDoorDestVarArray, pchInitMapVars, pchMapName, pReason, pExtract);
			StructDestroySafe(parse_GameActionDoorDestinationVarArray, &pDoorDestVarArray);

		} else if (bMustLockInventory && bMustLockMissions && !bMustLockNemesis) {
			GameActionDoorDestinationVarArray* pDoorDestVarArray = gameaction_GenerateDoorDestinationVariables(iPartitionIdx, pActionBlock->eaActions);
			AutoTrans_gameaction_tr_RunActionsNoNemesis(returnVal, GetAppGlobalType(), 
				entGetType(pEnt), entGetContainerID(pEnt), 
				pActionBlock, pMapVariableArray, pDoorDestVarArray, pchInitMapVars, pchMapName, pReason, pExtract);
			StructDestroySafe(parse_GameActionDoorDestinationVarArray, &pDoorDestVarArray);

		} else if (bMustLockInventory && !bMustLockMissions && bMustLockNemesis) {
			GameActionDoorDestinationVarArray* pDoorDestVarArray = gameaction_GenerateDoorDestinationVariables(iPartitionIdx, pActionBlock->eaActions);
			AutoTrans_gameaction_tr_RunActionsNoMissions(returnVal, GetAppGlobalType(), 
				entGetType(pEnt), entGetContainerID(pEnt), 
				pActionBlock, pMapVariableArray, pDoorDestVarArray, pchInitMapVars, pchMapName, pReason, pExtract);
			StructDestroySafe(parse_GameActionDoorDestinationVarArray, &pDoorDestVarArray);

		} else if (bMustLockInventory && !bMustLockMissions && !bMustLockNemesis) {
			GameActionDoorDestinationVarArray* pDoorDestVarArray = gameaction_GenerateDoorDestinationVariables(iPartitionIdx, pActionBlock->eaActions);
			AutoTrans_gameaction_tr_RunActionsNoMissionsOrNemesis(returnVal, GetAppGlobalType(), 
				entGetType(pEnt), entGetContainerID(pEnt), 
				pActionBlock, pMapVariableArray, pDoorDestVarArray, pchInitMapVars, pchMapName, pReason, pExtract);
			StructDestroySafe(parse_GameActionDoorDestinationVarArray, &pDoorDestVarArray);

		} else if (!bMustLockInventory && bMustLockMissions && bMustLockNemesis) {
			AutoTrans_gameaction_tr_RunActionsNoInventory(returnVal, GetAppGlobalType(), 
				entGetType(pEnt), entGetContainerID(pEnt), 
				pActionBlock, pMapVariableArray, pExtract);

		} else if (!bMustLockInventory && bMustLockMissions && !bMustLockNemesis) {
			AutoTrans_gameaction_tr_RunActionsNoInventoryOrNemesis(returnVal, GetAppGlobalType(), 
				entGetType(pEnt), entGetContainerID(pEnt), 
				pActionBlock, pMapVariableArray, pExtract);

		} else if (!bMustLockInventory && !bMustLockMissions && bMustLockNemesis) {
			AutoTrans_gameaction_tr_RunActionsNoInventoryOrMissions(returnVal, GetAppGlobalType(), 
				entGetType(pEnt), entGetContainerID(pEnt), 
				pActionBlock, pExtract);

		} else if (bMustLockNPCEmail) {
			AutoTrans_gameaction_tr_RunActionsLockNPCEMailOnly(returnVal, GetAppGlobalType(), 
				entGetType(pEnt), entGetContainerID(pEnt), 
				pActionBlock, pExtract);

		} else {
			gameaction_RunActionsNoLocking(returnStruct, pEnt, pActionBlock, pMapVariableArray);
		}

		StructDestroy(parse_WorldVariableArray, pMapVariableArray);

	} else if (callback) {
		// Run callback immediately
		callback(TRANSACTION_OUTCOME_SUCCESS, pEnt, pData);
	}
}


//----------------------------------------------------------------
// Transaction Helpers to execute a list of Actions
//----------------------------------------------------------------

// Runs everything, which means lots of things have to be locked
AUTO_TRANS_HELPER	
ATR_LOCKS(ent, ".Pplayer.Skilltype, .Pplayer.Eskillspecialization, .Pchar.Hclass, .Pchar.Ppsecondarypaths, .Pchar.Hpath, .Psaved.pSCPdata.Isummonedscp, .Psaved.pSCPdata.Fcachedpetbonusxppct, .Psaved.pSCPdata.erSCP, .Pplayer.Pugckillcreditlimit, .Pplayer.Pplayeraccountdata.Eapendingkeys, .Psaved.Nextactivitylogid, .Hallegiance, .Hsuballegiance, .Pchar.Ilevelexp, .Pplayer.Pplayeraccountdata.Eagamepermissionmaxvaluenumerics, .Psaved.Activitylogentries[AO], .Psaved.Uiindexbuild, .Psaved.Ppbuilds, .Psaved.Conowner.Containerid, .Psaved.Conowner.Containertype, .Pinventoryv2.Peaowneduniqueitems, .Pinventoryv2.Ppinventorybags, .Pinventoryv2.Pplitebags, .Pplayer.Playertype, .Psaved.Ppownedcontainers, .Itemidmax, .Pchar.Uipoweridmax, .Psaved.Ipetid, .Psaved.Ppuppetmaster.Curid, .Psaved.Ppuppetmaster.Curtype, .Psaved.Ppuppetmaster.Curtempid, .Psaved.Ppallowedcritterpets, .Pplayer.Missioninfo.Missions, .Pplayer.Missioninfo.Completedmissions, .Pplayer.Missioninfo.Eamissionrequests, .Pplayer.Missioninfo.Eamissioncooldowns, .Pplayer.Nemesisinfo.Eanemesisstates, .Pplayer.pEmailV2.Ilastusedid, .Pplayer.Langid, .Pplayer.pEmailV2.Mail")
	ATR_LOCKS(eaVarContainer, ".id, .Uclock, .Eaworldvars")
	ATR_LOCKS(pGuild, ".Inextactivitylogentryid, .Eaactivityentries, .Eamembers, .Pguildstatsinfo, .Htheme");
enumTransactionOutcome gameaction_trh_RunActionsLockAll(ATR_ARGS, ATH_ARG NOCONST(Entity)* ent, ATH_ARG CONST_EARRAY_OF(NOCONST(ShardVariableContainer)) eaVarContainer, ATH_ARG NOCONST(Guild)* pGuild, CONST_EARRAY_OF(WorldGameActionProperties)* actions, WorldVariableArray* pMapVariableArray, GameActionDoorDestinationVarArray* pDoorDestVarArray, const char* pchInitMapVars, const char* pchMapName, const ItemChangeReason *pReason, GameAccountDataExtract *pExtract)
{
	// Does not run WARP actions within transaction

	if (TRANSACTION_OUTCOME_SUCCESS != gameaction_trh_SendFloaters(ATR_PASS_ARGS, actions))
		return TRANSACTION_OUTCOME_FAILURE;
	if (TRANSACTION_OUTCOME_SUCCESS != gameaction_trh_SendNotifications(ATR_PASS_ARGS, actions))
		return TRANSACTION_OUTCOME_FAILURE;
	if (TRANSACTION_OUTCOME_SUCCESS != gameaction_trh_UpdateItemAssignments(ATR_PASS_ARGS, actions))
		return TRANSACTION_OUTCOME_FAILURE;
	if (TRANSACTION_OUTCOME_SUCCESS != gameaction_trh_RunContacts(ATR_PASS_ARGS, actions))
		return TRANSACTION_OUTCOME_FAILURE;
	if (TRANSACTION_OUTCOME_SUCCESS != gameaction_trh_RunMissionOffers(ATR_PASS_ARGS, actions, NULL, pMapVariableArray))
		return TRANSACTION_OUTCOME_FAILURE;
	if (TRANSACTION_OUTCOME_SUCCESS != gameaction_trh_RunExpressions(ATR_PASS_ARGS, actions))
		return TRANSACTION_OUTCOME_FAILURE;
	if (TRANSACTION_OUTCOME_SUCCESS != 	gameaction_trh_TakeItems(ATR_PASS_ARGS, ent, actions, pReason, pExtract))
		return TRANSACTION_OUTCOME_FAILURE;
	if (TRANSACTION_OUTCOME_SUCCESS != 	gameaction_trh_GiveItems(ATR_PASS_ARGS, ent, actions, NULL, pReason, pExtract))
		return TRANSACTION_OUTCOME_FAILURE;
	if (TRANSACTION_OUTCOME_SUCCESS != 	gameaction_trh_GiveDoorKeyItems(ATR_PASS_ARGS, ent, actions, NULL, pMapVariableArray, pDoorDestVarArray, pchInitMapVars, pchMapName, pReason, pExtract))
		return TRANSACTION_OUTCOME_FAILURE;
	if (TRANSACTION_OUTCOME_SUCCESS != gameaction_trh_GrantMissions(ATR_PASS_ARGS, ent, eaVarContainer, pGuild, NULL, NULL, actions, pMapVariableArray))
		return TRANSACTION_OUTCOME_FAILURE;
	if (TRANSACTION_OUTCOME_SUCCESS != gameaction_trh_DropMissions(ATR_PASS_ARGS, ent, actions, pReason, pExtract))
		return TRANSACTION_OUTCOME_FAILURE;
	if (TRANSACTION_OUTCOME_SUCCESS != gameaction_trh_ChangeNemesisState(ATR_PASS_ARGS, ent, actions))
		return TRANSACTION_OUTCOME_FAILURE;
	if (TRANSACTION_OUTCOME_SUCCESS != gameaction_trh_NPCSendEmail(ATR_PASS_ARGS, ent, actions, pExtract))
		return TRANSACTION_OUTCOME_FAILURE;
	if (TRANSACTION_OUTCOME_SUCCESS !=  gameaction_trh_GADAttribValue(ATR_PASS_ARGS, ent, actions))
		return TRANSACTION_OUTCOME_FAILURE;
	if (TRANSACTION_OUTCOME_SUCCESS !=  gameaction_trh_ShardVariables(ATR_PASS_ARGS, eaVarContainer, actions))
		return TRANSACTION_OUTCOME_FAILURE;
	if (TRANSACTION_OUTCOME_SUCCESS !=  gameaction_trh_ActivityLog(ATR_PASS_ARGS, ent, actions))
		return TRANSACTION_OUTCOME_FAILURE;
	if (TRANSACTION_OUTCOME_SUCCESS !=  gameaction_trh_GuildActivityLog(ATR_PASS_ARGS, pGuild, ent->myContainerID, actions))
		return TRANSACTION_OUTCOME_FAILURE;
	if (TRANSACTION_OUTCOME_SUCCESS != gameaction_trh_RunGuildStatUpdates(ATR_PASS_ARGS, actions, pGuild))
		return TRANSACTION_OUTCOME_FAILURE;
	if (TRANSACTION_OUTCOME_SUCCESS != gameaction_trh_RunGuildThemeSetOperations(ATR_PASS_ARGS, actions, pGuild))
		return TRANSACTION_OUTCOME_FAILURE;

	return TRANSACTION_OUTCOME_SUCCESS;
}


// Runs only with shard variables locked
AUTO_TRANS_HELPER
ATR_LOCKS(eaVarContainer, ".id, .Uclock, .Eaworldvars");
enumTransactionOutcome gameaction_trh_RunActionsLockVarsOnly(ATR_ARGS, ATH_ARG CONST_EARRAY_OF(NOCONST(ShardVariableContainer)) eaVarContainer, CONST_EARRAY_OF(WorldGameActionProperties)* actions)
{
	if (gameaction_MustLockMissions(actions, NULL))
		TRANSACTION_RETURN_LOG_FAILURE("Tried to run the 'Only Shard Variables' version of a GameAction transaction, but Missions must be locked!");
	if (gameaction_MustLockInventory(actions, NULL))
		TRANSACTION_RETURN_LOG_FAILURE("Tried to run the 'Only Shard Variables' version of a GameAction transaction, but Inventory must be locked!");
	if (gameaction_MustLockNemesis(actions, NULL))
		TRANSACTION_RETURN_LOG_FAILURE("Tried to run the 'Only Shard Variables' version of a GameAction transaction, but Nemeses must be locked!");
	if (gameaction_MustLockNPCEMail(actions, NULL))
		TRANSACTION_RETURN_LOG_FAILURE("Tried to run the 'Only Shard Variables' version of a GameAction transaction, but NPCEMail must be locked!");
	if (gameaction_MustLockGameAccount(actions, NULL))
		TRANSACTION_RETURN_LOG_FAILURE("Tried to run the 'Only Shard Variables' version of a GameAction transaction, but Game Account must be locked!");
	if (gameaction_MustLockActivityLog(actions, NULL))
		TRANSACTION_RETURN_LOG_FAILURE("Tried to run the 'Only Shard Variables' version of a GameAction transaction, but ActivityLog must be locked!");
	if (gameaction_MustLockGuild(actions, NULL))
		TRANSACTION_RETURN_LOG_FAILURE("Tried to run the 'Only Shard Variables' version of a GameAction transaction, but Guild must be locked!");

	// Non-transacted game actions
	if (TRANSACTION_OUTCOME_SUCCESS != gameaction_trh_SendFloaters(ATR_PASS_ARGS, actions))
		return TRANSACTION_OUTCOME_FAILURE;
	if (TRANSACTION_OUTCOME_SUCCESS != gameaction_trh_SendNotifications(ATR_PASS_ARGS, actions))
		return TRANSACTION_OUTCOME_FAILURE;
	if (TRANSACTION_OUTCOME_SUCCESS != gameaction_trh_UpdateItemAssignments(ATR_PASS_ARGS, actions))
		return TRANSACTION_OUTCOME_FAILURE;
	if (TRANSACTION_OUTCOME_SUCCESS != gameaction_trh_RunContacts(ATR_PASS_ARGS, actions))
		return TRANSACTION_OUTCOME_FAILURE;
	if (TRANSACTION_OUTCOME_SUCCESS != gameaction_trh_RunExpressions(ATR_PASS_ARGS, actions))
		return TRANSACTION_OUTCOME_FAILURE;

	// Transacted game actions
	if (TRANSACTION_OUTCOME_SUCCESS !=  gameaction_trh_ShardVariables(ATR_PASS_ARGS, eaVarContainer, actions))
		return TRANSACTION_OUTCOME_FAILURE;

	return TRANSACTION_OUTCOME_SUCCESS;
}


// Runs everything, which means lots of things have to be locked
AUTO_TRANS_HELPER
	ATR_LOCKS(ent, ".Pplayer.Skilltype, .Pplayer.Eskillspecialization, .Pchar.Hclass, .Pchar.Ppsecondarypaths, .Pchar.Hpath, .Psaved.pSCPdata.Isummonedscp, .Psaved.pSCPdata.Fcachedpetbonusxppct, .Psaved.pSCPdata.erSCP, .Pplayer.Pugckillcreditlimit, .Pplayer.Pplayeraccountdata.Eapendingkeys, .Psaved.Nextactivitylogid, .Hallegiance, .Hsuballegiance, .Pchar.Ilevelexp, .Pplayer.Pplayeraccountdata.Eagamepermissionmaxvaluenumerics, .Psaved.Activitylogentries[AO], .Psaved.Uiindexbuild, .Psaved.Ppbuilds, .Psaved.Conowner.Containerid, .Psaved.Conowner.Containertype, .Pinventoryv2.Peaowneduniqueitems, .Pinventoryv2.Ppinventorybags, .Pinventoryv2.Pplitebags, .Pplayer.Playertype, .Psaved.Ppownedcontainers, .Itemidmax, .Pchar.Uipoweridmax, .Psaved.Ipetid, .Psaved.Ppuppetmaster.Curid, .Psaved.Ppuppetmaster.Curtype, .Psaved.Ppuppetmaster.Curtempid, .Psaved.Ppallowedcritterpets, .Pplayer.Missioninfo.Missions, .Pplayer.Missioninfo.Completedmissions, .Pplayer.Missioninfo.Eamissionrequests, .Pplayer.Missioninfo.Eamissioncooldowns, .Pplayer.Nemesisinfo.Eanemesisstates, .Pplayer.pEmailV2.Ilastusedid, .Pplayer.Langid, .Pplayer.pEmailV2.Mail");
enumTransactionOutcome gameaction_trh_RunActionsLockAllEntity(ATR_ARGS, ATH_ARG NOCONST(Entity)* ent, CONST_EARRAY_OF(WorldGameActionProperties)* actions, WorldVariableArray* pVariables, GameActionDoorDestinationVarArray* pDoorDestVarArray, const char* pchInitMapVars, const char* pchMapName, const ItemChangeReason *pReason, GameAccountDataExtract *pExtract)
{
	if (gameaction_MustLockShardVariables(actions, NULL))
		TRANSACTION_RETURN_LOG_FAILURE("Tried to run the 'All Entity' version of a GameAction transaction, but Shard Variables must be locked!");
	if (gameaction_MustLockGuild(actions, NULL))
		TRANSACTION_RETURN_LOG_FAILURE("Tried to run the 'All Entity' version of a GameAction transaction, but Guild must be locked!");

	// Does not run WARP actions within transaction

	// Non-transacted game actions
	if (TRANSACTION_OUTCOME_SUCCESS != gameaction_trh_SendFloaters(ATR_PASS_ARGS, actions))
		return TRANSACTION_OUTCOME_FAILURE;
	if (TRANSACTION_OUTCOME_SUCCESS != gameaction_trh_SendNotifications(ATR_PASS_ARGS, actions))
		return TRANSACTION_OUTCOME_FAILURE;
	if (TRANSACTION_OUTCOME_SUCCESS != gameaction_trh_UpdateItemAssignments(ATR_PASS_ARGS, actions))
		return TRANSACTION_OUTCOME_FAILURE;
	if (TRANSACTION_OUTCOME_SUCCESS != gameaction_trh_RunContacts(ATR_PASS_ARGS, actions))
		return TRANSACTION_OUTCOME_FAILURE;
	if (TRANSACTION_OUTCOME_SUCCESS != gameaction_trh_RunExpressions(ATR_PASS_ARGS, actions))
		return TRANSACTION_OUTCOME_FAILURE;

	// Transacted game actions
	if (TRANSACTION_OUTCOME_SUCCESS != 	gameaction_trh_TakeItems(ATR_PASS_ARGS, ent, actions, pReason, pExtract))
		return TRANSACTION_OUTCOME_FAILURE;
	if (TRANSACTION_OUTCOME_SUCCESS != 	gameaction_trh_GiveItems(ATR_PASS_ARGS, ent, actions, NULL, pReason, pExtract))
		return TRANSACTION_OUTCOME_FAILURE;
	if (TRANSACTION_OUTCOME_SUCCESS != 	gameaction_trh_GiveDoorKeyItems(ATR_PASS_ARGS, ent, actions, NULL, pVariables, pDoorDestVarArray, pchInitMapVars, pchMapName, pReason, pExtract))
		return TRANSACTION_OUTCOME_FAILURE;
	if (TRANSACTION_OUTCOME_SUCCESS != gameaction_trh_GrantMissions(ATR_PASS_ARGS, ent, NULL, NULL, NULL, NULL, actions, pVariables))
		return TRANSACTION_OUTCOME_FAILURE;
	if (TRANSACTION_OUTCOME_SUCCESS != gameaction_trh_DropMissions(ATR_PASS_ARGS, ent, actions, pReason, pExtract))
		return TRANSACTION_OUTCOME_FAILURE;
	if (TRANSACTION_OUTCOME_SUCCESS != gameaction_trh_RunMissionOffers(ATR_PASS_ARGS, actions, NULL, pVariables))
		return TRANSACTION_OUTCOME_FAILURE;
	if (TRANSACTION_OUTCOME_SUCCESS != gameaction_trh_ChangeNemesisState(ATR_PASS_ARGS, ent, actions))
		return TRANSACTION_OUTCOME_FAILURE;
	if (TRANSACTION_OUTCOME_SUCCESS != gameaction_trh_NPCSendEmail(ATR_PASS_ARGS, ent, actions, pExtract))
		return TRANSACTION_OUTCOME_FAILURE;
	if (TRANSACTION_OUTCOME_SUCCESS !=  gameaction_trh_GADAttribValue(ATR_PASS_ARGS, ent, actions))
		return TRANSACTION_OUTCOME_FAILURE;
	if (TRANSACTION_OUTCOME_SUCCESS !=  gameaction_trh_ActivityLog(ATR_PASS_ARGS, ent, actions))
		return TRANSACTION_OUTCOME_FAILURE;

	return TRANSACTION_OUTCOME_SUCCESS;
}


// Locks guild plus all the entity action related fields
AUTO_TRANS_HELPER
ATR_LOCKS(ent, ".Pplayer.Skilltype, .Pplayer.Eskillspecialization, .Pchar.Hclass, .Pchar.Ppsecondarypaths, .Pchar.Hpath, .Psaved.pSCPdata.Isummonedscp, .Psaved.pSCPdata.Fcachedpetbonusxppct, .Psaved.pSCPdata.erSCP, .Hallegiance, .Hsuballegiance, .Pplayer.Pplayeraccountdata.Eapendingkeys, .Psaved.Nextactivitylogid, .Psaved.Activitylogentries[AO], .Itemidmax, .Pplayer.Pugckillcreditlimit, .Pplayer.Pplayeraccountdata.Eagamepermissionmaxvaluenumerics, .Pchar.Ilevelexp, .Pinventoryv2.Peaowneduniqueitems, .Pinventoryv2.Ppinventorybags, .Pinventoryv2.Pplitebags, .Psaved.Conowner.Containerid, .Psaved.Conowner.Containertype, .Psaved.Uiindexbuild, .Psaved.Ppbuilds, .Psaved.Ppownedcontainers, .Pchar.Uipoweridmax, .Psaved.Ipetid, .Psaved.Ppuppetmaster.Curid, .Psaved.Ppuppetmaster.Curtype, .Psaved.Ppuppetmaster.Curtempid, .Psaved.Ppallowedcritterpets, .Pplayer.Nemesisinfo.Eanemesisstates, .Pplayer.pEmailV2.Ilastusedid, .Pplayer.Langid, .Pplayer.pEmailV2.Mail, .Pplayer.Playertype, .Pplayer.Missioninfo.Missions, .Pplayer.Missioninfo.Completedmissions, .Pplayer.Missioninfo.Eamissionrequests, .Pplayer.Missioninfo.Eamissioncooldowns")
ATR_LOCKS(pGuild, ".Inextactivitylogentryid, .Eaactivityentries, .Eamembers, .Pguildstatsinfo, .Htheme");
enumTransactionOutcome gameaction_trh_RunActionsLockAllEntityWithGuild(ATR_ARGS, ATH_ARG NOCONST(Entity)* ent, ATH_ARG NOCONST(Guild)* pGuild, CONST_EARRAY_OF(WorldGameActionProperties)* actions, WorldVariableArray* pMapVariableArray, GameActionDoorDestinationVarArray* pDoorDestVarArray, const char* pchInitMapVars, const char* pchMapName, const ItemChangeReason *pReason, GameAccountDataExtract *pExtract)
{
	if (gameaction_MustLockShardVariables(actions, NULL))
		TRANSACTION_RETURN_LOG_FAILURE("Tried to run the 'All Entity' version of a GameAction transaction, but Shard Variables must be locked!");

	// Does not run WARP actions within transaction

	// Non-transacted game actions
	if (TRANSACTION_OUTCOME_SUCCESS != gameaction_trh_SendFloaters(ATR_PASS_ARGS, actions))
		return TRANSACTION_OUTCOME_FAILURE;
	if (TRANSACTION_OUTCOME_SUCCESS != gameaction_trh_SendNotifications(ATR_PASS_ARGS, actions))
		return TRANSACTION_OUTCOME_FAILURE;
	if (TRANSACTION_OUTCOME_SUCCESS != gameaction_trh_UpdateItemAssignments(ATR_PASS_ARGS, actions))
		return TRANSACTION_OUTCOME_FAILURE;
	if (TRANSACTION_OUTCOME_SUCCESS != gameaction_trh_RunContacts(ATR_PASS_ARGS, actions))
		return TRANSACTION_OUTCOME_FAILURE;
	if (TRANSACTION_OUTCOME_SUCCESS != gameaction_trh_RunExpressions(ATR_PASS_ARGS, actions))
		return TRANSACTION_OUTCOME_FAILURE;

	// Transacted game actions
	if (TRANSACTION_OUTCOME_SUCCESS != 	gameaction_trh_TakeItems(ATR_PASS_ARGS, ent, actions, pReason, pExtract))
		return TRANSACTION_OUTCOME_FAILURE;
	if (TRANSACTION_OUTCOME_SUCCESS != 	gameaction_trh_GiveItems(ATR_PASS_ARGS, ent, actions, NULL, pReason, pExtract))
		return TRANSACTION_OUTCOME_FAILURE;
	if (TRANSACTION_OUTCOME_SUCCESS != 	gameaction_trh_GiveDoorKeyItems(ATR_PASS_ARGS, ent, actions, NULL, pMapVariableArray, pDoorDestVarArray, pchInitMapVars, pchMapName, pReason, pExtract))
		return TRANSACTION_OUTCOME_FAILURE;
	if (TRANSACTION_OUTCOME_SUCCESS != gameaction_trh_GrantMissions(ATR_PASS_ARGS, ent, NULL, pGuild, NULL, NULL, actions, pMapVariableArray))
		return TRANSACTION_OUTCOME_FAILURE;
	if (TRANSACTION_OUTCOME_SUCCESS != gameaction_trh_RunMissionOffers(ATR_PASS_ARGS, actions, NULL, pMapVariableArray))
		return TRANSACTION_OUTCOME_FAILURE;
	if (TRANSACTION_OUTCOME_SUCCESS != gameaction_trh_DropMissions(ATR_PASS_ARGS, ent, actions, pReason, pExtract))
		return TRANSACTION_OUTCOME_FAILURE;
	if (TRANSACTION_OUTCOME_SUCCESS != gameaction_trh_ChangeNemesisState(ATR_PASS_ARGS, ent, actions))
		return TRANSACTION_OUTCOME_FAILURE;
	if (TRANSACTION_OUTCOME_SUCCESS != gameaction_trh_NPCSendEmail(ATR_PASS_ARGS, ent, actions, pExtract))
		return TRANSACTION_OUTCOME_FAILURE;
	if (TRANSACTION_OUTCOME_SUCCESS !=  gameaction_trh_GADAttribValue(ATR_PASS_ARGS, ent, actions))
		return TRANSACTION_OUTCOME_FAILURE;
	if (TRANSACTION_OUTCOME_SUCCESS !=  gameaction_trh_ActivityLog(ATR_PASS_ARGS, ent, actions))
		return TRANSACTION_OUTCOME_FAILURE;
	if (TRANSACTION_OUTCOME_SUCCESS !=  gameaction_trh_GuildActivityLog(ATR_PASS_ARGS, pGuild, ent->myContainerID, actions))
		return TRANSACTION_OUTCOME_FAILURE;
	if (TRANSACTION_OUTCOME_SUCCESS !=  gameaction_trh_RunGuildStatUpdates(ATR_PASS_ARGS, actions, pGuild))
		return TRANSACTION_OUTCOME_FAILURE;
	if (TRANSACTION_OUTCOME_SUCCESS !=  gameaction_trh_RunGuildThemeSetOperations(ATR_PASS_ARGS, actions, pGuild))
		return TRANSACTION_OUTCOME_FAILURE;

	return TRANSACTION_OUTCOME_SUCCESS;
}


// Doesn't lock the inventory. Fails if there are inventory-related actions in the list.
AUTO_TRANS_HELPER
ATR_LOCKS(ent, ".Pplayer.Nemesisinfo.Eanemesisstates, .Pplayer.pEmailV2.Ilastusedid, .Pplayer.Langid, .Hallegiance, .Hsuballegiance, .Pplayer.Pplayeraccountdata.Eagamepermissionmaxvaluenumerics, .Pplayer.pEmailV2.Mail, .Pchar.Ilevelexp");
enumTransactionOutcome gameaction_trh_RunActionsNoInventory(ATR_ARGS, ATH_ARG NOCONST(Entity)* ent, CONST_EARRAY_OF(WorldGameActionProperties)* actions, WorldVariableArray *pMapVariableArray, GameAccountDataExtract *pExtract)
{
	if (gameaction_MustLockInventory(actions, NULL))
		TRANSACTION_RETURN_LOG_FAILURE("Tried to run the 'No Inventory' version of a GameAction transaction, but Inventory must be locked!");
	if (gameaction_MustLockGameAccount(actions, NULL))
		TRANSACTION_RETURN_LOG_FAILURE("Tried to run the 'No Inventory' version of a GameAction transaction, but Game Account must be locked!");
	if (gameaction_MustLockShardVariables(actions, NULL))
		TRANSACTION_RETURN_LOG_FAILURE("Tried to run the 'No Inventory' version of a GameAction transaction, but Shard Variables must be locked!");
	if (gameaction_MustLockActivityLog(actions, NULL))
		TRANSACTION_RETURN_LOG_FAILURE("Tried to run the 'No Inventory' version of a GameAction transaction, but Activity Log must be locked!");
	if (gameaction_MustLockGuildActivityLog(((NONNULL(ent->pPlayer)) && NONNULL(ent->pPlayer->pGuild)), actions, NULL))
		TRANSACTION_RETURN_LOG_FAILURE("Tried to run the 'No Inventory' version of a GameAction transaction, but Guild Activity Log must be locked!");
	if (gameaction_MustLockGuild(actions, NULL))
		TRANSACTION_RETURN_LOG_FAILURE("Tried to run the 'No Inventory' version of a GameAction transaction, but Guild must be locked!");

	// Does not run WARP actions within transaction

	// Non-transacted game actions
	if (TRANSACTION_OUTCOME_SUCCESS != gameaction_trh_SendFloaters(ATR_PASS_ARGS, actions))
		return TRANSACTION_OUTCOME_FAILURE;
	if (TRANSACTION_OUTCOME_SUCCESS != gameaction_trh_SendNotifications(ATR_PASS_ARGS, actions))
		return TRANSACTION_OUTCOME_FAILURE;
	if (TRANSACTION_OUTCOME_SUCCESS != gameaction_trh_UpdateItemAssignments(ATR_PASS_ARGS, actions))
		return TRANSACTION_OUTCOME_FAILURE;
	if (TRANSACTION_OUTCOME_SUCCESS != gameaction_trh_RunContacts(ATR_PASS_ARGS, actions))
		return TRANSACTION_OUTCOME_FAILURE;
	if (TRANSACTION_OUTCOME_SUCCESS != gameaction_trh_RunExpressions(ATR_PASS_ARGS, actions))
		return TRANSACTION_OUTCOME_FAILURE;

	// Transacted game actions
	if (TRANSACTION_OUTCOME_SUCCESS != gameaction_trh_GrantMissions(ATR_PASS_ARGS, ent, NULL, NULL, NULL, NULL, actions, pMapVariableArray))
		return TRANSACTION_OUTCOME_FAILURE;
	if (TRANSACTION_OUTCOME_SUCCESS != gameaction_trh_RunMissionOffers(ATR_PASS_ARGS, actions, NULL, pMapVariableArray))
		return TRANSACTION_OUTCOME_FAILURE;
	if (TRANSACTION_OUTCOME_SUCCESS != gameaction_trh_ChangeNemesisState(ATR_PASS_ARGS, ent, actions))
		return TRANSACTION_OUTCOME_FAILURE;
	if (TRANSACTION_OUTCOME_SUCCESS != gameaction_trh_NPCSendEmail(ATR_PASS_ARGS, ent, actions, pExtract))
		return TRANSACTION_OUTCOME_FAILURE;
		
	return TRANSACTION_OUTCOME_SUCCESS;
}


// Doesn't lock the player's missions. Fails if there are mission-related actions in the list.
AUTO_TRANS_HELPER
ATR_LOCKS(ent, ".Pplayer.Skilltype, .Pplayer.Eskillspecialization, .Pchar.Hclass, .Pchar.Ppsecondarypaths, .Pchar.Hpath, .Psaved.pSCPdata.Isummonedscp, .Psaved.pSCPdata.Fcachedpetbonusxppct, .Psaved.pSCPdata.erSCP, .pInventoryV2.Peaowneduniqueitems, .pInventoryV2.Pplitebags, .Hallegiance, .Hsuballegiance, .Pplayer.pEmailV2.Ilastusedid, .Pplayer.Langid, .Pplayer.pEmailV2.Mail, .Itemidmax, .Pplayer.Pugckillcreditlimit, .Pplayer.Pplayeraccountdata.Eagamepermissionmaxvaluenumerics, .Pchar.Ilevelexp, .pInventoryV2.Ppinventorybags, .Psaved.Conowner.Containerid, .Psaved.Conowner.Containertype, .Psaved.Uiindexbuild, .Psaved.Ppbuilds, .Psaved.Ppownedcontainers, .Pchar.Uipoweridmax, .Psaved.Ipetid, .Psaved.Ppuppetmaster.Curid, .Psaved.Ppuppetmaster.Curtype, .Psaved.Ppuppetmaster.Curtempid, .Psaved.Ppallowedcritterpets, .Pplayer.Nemesisinfo.Eanemesisstates, .Pplayer.Playertype");
enumTransactionOutcome gameaction_trh_RunActionsNoMissions(ATR_ARGS, ATH_ARG NOCONST(Entity)* ent, CONST_EARRAY_OF(WorldGameActionProperties)* actions, WorldVariableArray* pMapVariableArray, GameActionDoorDestinationVarArray* pDoorDestVarArray, const char* pchInitMapVars, const char* pchMapName, const ItemChangeReason *pReason, GameAccountDataExtract *pExtract)
{
	if (gameaction_MustLockMissions(actions, NULL))
		TRANSACTION_RETURN_LOG_FAILURE("Tried to run the 'No Missions' version of a GameAction transaction, but Missions must be locked!");
	if (gameaction_MustLockGameAccount(actions, NULL))
		TRANSACTION_RETURN_LOG_FAILURE("Tried to run the 'No Missions' version of a GameAction transaction, but Game Account must be locked!");
	if (gameaction_MustLockShardVariables(actions, NULL))
		TRANSACTION_RETURN_LOG_FAILURE("Tried to run the 'No Missions' version of a GameAction transaction, but Shard Variables must be locked!");
	if (gameaction_MustLockActivityLog(actions, NULL))
		TRANSACTION_RETURN_LOG_FAILURE("Tried to run the 'No Missions' version of a GameAction transaction, but Activity Log must be locked!");
	if (gameaction_MustLockGuildActivityLog(((NONNULL(ent->pPlayer)) && NONNULL(ent->pPlayer->pGuild)), actions, NULL))
		TRANSACTION_RETURN_LOG_FAILURE("Tried to run the 'No Missions' version of a GameAction transaction, but Guild Activity Log must be locked!");
	if (gameaction_MustLockGuild(actions, NULL))
		TRANSACTION_RETURN_LOG_FAILURE("Tried to run the 'No Missions' version of a GameAction transaction, but Guild must be locked!");

	// Does not run WARP actions within transaction

	// Non-transacted game actions
	if (TRANSACTION_OUTCOME_SUCCESS != gameaction_trh_SendFloaters(ATR_PASS_ARGS, actions))
		return TRANSACTION_OUTCOME_FAILURE;
	if (TRANSACTION_OUTCOME_SUCCESS != gameaction_trh_SendNotifications(ATR_PASS_ARGS, actions))
		return TRANSACTION_OUTCOME_FAILURE;
	if (TRANSACTION_OUTCOME_SUCCESS != gameaction_trh_UpdateItemAssignments(ATR_PASS_ARGS, actions))
		return TRANSACTION_OUTCOME_FAILURE;
	if (TRANSACTION_OUTCOME_SUCCESS != gameaction_trh_RunContacts(ATR_PASS_ARGS, actions))
		return TRANSACTION_OUTCOME_FAILURE;
	if (TRANSACTION_OUTCOME_SUCCESS != gameaction_trh_RunExpressions(ATR_PASS_ARGS, actions))
		return TRANSACTION_OUTCOME_FAILURE;

	// Transacted game actions
	if (TRANSACTION_OUTCOME_SUCCESS != 	gameaction_trh_TakeItems(ATR_PASS_ARGS, ent, actions, pReason, pExtract))
		return TRANSACTION_OUTCOME_FAILURE;
	if (TRANSACTION_OUTCOME_SUCCESS != 	gameaction_trh_GiveItems(ATR_PASS_ARGS, ent, actions, NULL, pReason, pExtract))
		return TRANSACTION_OUTCOME_FAILURE;
	if (TRANSACTION_OUTCOME_SUCCESS != 	gameaction_trh_GiveDoorKeyItems(ATR_PASS_ARGS, ent, actions, NULL, pMapVariableArray, pDoorDestVarArray, pchInitMapVars, pchMapName, pReason, pExtract))
		return TRANSACTION_OUTCOME_FAILURE;
	if (TRANSACTION_OUTCOME_SUCCESS != gameaction_trh_ChangeNemesisState(ATR_PASS_ARGS, ent, actions))
		return TRANSACTION_OUTCOME_FAILURE;
	if (TRANSACTION_OUTCOME_SUCCESS != gameaction_trh_NPCSendEmail(ATR_PASS_ARGS, ent, actions, pExtract))
		return TRANSACTION_OUTCOME_FAILURE;

	return TRANSACTION_OUTCOME_SUCCESS;
}


// Doesn't lock the player's nemeses. Fails if there are nemesis-related actions in the list.
AUTO_TRANS_HELPER
ATR_LOCKS(ent, ".Pplayer.Skilltype, .Pplayer.Eskillspecialization, .Pchar.Hclass, .Pchar.Ppsecondarypaths, .Pchar.Hpath, .Psaved.pSCPdata.Isummonedscp, .Psaved.pSCPdata.Fcachedpetbonusxppct, .Psaved.pSCPdata.erSCP, .pInventoryV2.Peaowneduniqueitems, .pInventoryV2.Pplitebags, .Hallegiance, .Hsuballegiance, .Pplayer.pEmailV2.Ilastusedid, .Pplayer.Langid, .Pplayer.pEmailV2.Mail, .Itemidmax, .Pplayer.Pugckillcreditlimit, .Pplayer.Pplayeraccountdata.Eagamepermissionmaxvaluenumerics, .Pchar.Ilevelexp, .pInventoryV2.Ppinventorybags, .Psaved.Conowner.Containerid, .Psaved.Conowner.Containertype, .Psaved.Uiindexbuild, .Psaved.Ppbuilds, .Psaved.Ppownedcontainers, .Pchar.Uipoweridmax, .Psaved.Ipetid, .Psaved.Ppuppetmaster.Curid, .Psaved.Ppuppetmaster.Curtype, .Psaved.Ppuppetmaster.Curtempid, .Psaved.Ppallowedcritterpets, .Pplayer.Playertype");
enumTransactionOutcome gameaction_trh_RunActionsNoNemesis(ATR_ARGS, ATH_ARG NOCONST(Entity)* ent, CONST_EARRAY_OF(WorldGameActionProperties)* actions, WorldVariableArray* pMapVariableArray, GameActionDoorDestinationVarArray* pDoorDestVarArray, const char* pchInitMapVars, const  char* pchMapName, const ItemChangeReason *pReason, GameAccountDataExtract *pExtract)
{
	if (gameaction_MustLockNemesis(actions, NULL))
		TRANSACTION_RETURN_LOG_FAILURE("Tried to run the 'No Nemesis' version of a GameAction transaction, but Nemeses must be locked!");
	if (gameaction_MustLockGameAccount(actions, NULL))
		TRANSACTION_RETURN_LOG_FAILURE("Tried to run the 'No Nemesis' version of a GameAction transaction, but Game Account must be locked!");
	if (gameaction_MustLockShardVariables(actions, NULL))
		TRANSACTION_RETURN_LOG_FAILURE("Tried to run the 'No Nemesis' version of a GameAction transaction, but Shard Variables must be locked!");
	if (gameaction_MustLockActivityLog(actions, NULL))
		TRANSACTION_RETURN_LOG_FAILURE("Tried to run the 'No Nemesis' version of a GameAction transaction, but Activity Log must be locked!");
	if (gameaction_MustLockGuildActivityLog(((NONNULL(ent->pPlayer)) && NONNULL(ent->pPlayer->pGuild)), actions, NULL))
		TRANSACTION_RETURN_LOG_FAILURE("Tried to run the 'No Nemesis' version of a GameAction transaction, but Guild Activity Log must be locked!");
	if (gameaction_MustLockGuild(actions, NULL))
		TRANSACTION_RETURN_LOG_FAILURE("Tried to run the 'No Nemesis' version of a GameAction transaction, but Guild must be locked!");

	// Does not run WARP actions within transaction

	// Non-transacted game actions
	if (TRANSACTION_OUTCOME_SUCCESS != gameaction_trh_SendFloaters(ATR_PASS_ARGS, actions))
		return TRANSACTION_OUTCOME_FAILURE;
	if (TRANSACTION_OUTCOME_SUCCESS != gameaction_trh_SendNotifications(ATR_PASS_ARGS, actions))
		return TRANSACTION_OUTCOME_FAILURE;
	if (TRANSACTION_OUTCOME_SUCCESS != gameaction_trh_UpdateItemAssignments(ATR_PASS_ARGS, actions))
		return TRANSACTION_OUTCOME_FAILURE;
	if (TRANSACTION_OUTCOME_SUCCESS != gameaction_trh_RunContacts(ATR_PASS_ARGS, actions))
		return TRANSACTION_OUTCOME_FAILURE;
	if (TRANSACTION_OUTCOME_SUCCESS != gameaction_trh_RunMissionOffers(ATR_PASS_ARGS, actions, NULL, pMapVariableArray))
		return TRANSACTION_OUTCOME_FAILURE;
	if (TRANSACTION_OUTCOME_SUCCESS != gameaction_trh_RunExpressions(ATR_PASS_ARGS, actions))
		return TRANSACTION_OUTCOME_FAILURE;

	// Transacted game actions
	if (TRANSACTION_OUTCOME_SUCCESS != 	gameaction_trh_TakeItems(ATR_PASS_ARGS, ent, actions, pReason, pExtract))
		return TRANSACTION_OUTCOME_FAILURE;
	if (TRANSACTION_OUTCOME_SUCCESS != 	gameaction_trh_GiveItems(ATR_PASS_ARGS, ent, actions, NULL, pReason, pExtract))
		return TRANSACTION_OUTCOME_FAILURE;
	if (TRANSACTION_OUTCOME_SUCCESS != 	gameaction_trh_GiveDoorKeyItems(ATR_PASS_ARGS, ent, actions, NULL, pMapVariableArray, pDoorDestVarArray, pchInitMapVars, pchMapName, pReason, pExtract))
		return TRANSACTION_OUTCOME_FAILURE;
	if (TRANSACTION_OUTCOME_SUCCESS != gameaction_trh_GrantMissions(ATR_PASS_ARGS, ent, NULL, NULL, NULL, NULL, actions, pMapVariableArray))
		return TRANSACTION_OUTCOME_FAILURE;
	if (TRANSACTION_OUTCOME_SUCCESS != gameaction_trh_NPCSendEmail(ATR_PASS_ARGS, ent, actions, pExtract))
		return TRANSACTION_OUTCOME_FAILURE;

	return TRANSACTION_OUTCOME_SUCCESS;
}


// Doesn't lock the player's missions or nemeses
AUTO_TRANS_HELPER
	ATR_LOCKS(ent, ".Pplayer.Skilltype, .Pplayer.Eskillspecialization, .Pchar.Hclass, .Pchar.Ppsecondarypaths, .Pchar.Hpath, .Psaved.pSCPdata.Isummonedscp, .Psaved.pSCPdata.Fcachedpetbonusxppct, .Psaved.pSCPdata.erSCP, .Pplayer.Pugckillcreditlimit, .Pplayer.pEmailV2.Ilastusedid, .Pplayer.Langid, .Hallegiance, .Hsuballegiance, .Pchar.Ilevelexp, .Pplayer.Pplayeraccountdata.Eagamepermissionmaxvaluenumerics, .Pplayer.pEmailV2.Mail, .Psaved.Uiindexbuild, .Psaved.Ppbuilds, .Psaved.Conowner.Containerid, .Psaved.Conowner.Containertype, .Pinventoryv2.Peaowneduniqueitems, .Pinventoryv2.Ppinventorybags, .Pinventoryv2.Pplitebags, .Pplayer.Playertype, .Psaved.Ppownedcontainers, .Itemidmax, .Pchar.Uipoweridmax, .Psaved.Ipetid, .Psaved.Ppuppetmaster.Curid, .Psaved.Ppuppetmaster.Curtype, .Psaved.Ppuppetmaster.Curtempid, .Psaved.Ppallowedcritterpets");
enumTransactionOutcome gameaction_trh_RunActionsNoMissionsOrNemesis(ATR_ARGS, ATH_ARG NOCONST(Entity)* ent, CONST_EARRAY_OF(WorldGameActionProperties)* actions, WorldVariableArray* pMapVariableArray, GameActionDoorDestinationVarArray* pDoorDestVarArray, const char* pchInitMapVars, const char* pchMapName, const ItemChangeReason *pReason, GameAccountDataExtract *pExtract)
{
	if (gameaction_MustLockMissions(actions, NULL))
		TRANSACTION_RETURN_LOG_FAILURE("Tried to run the 'No Missions or Nemesis' version of a GameAction transaction, but Missions must be locked!");
	if (gameaction_MustLockNemesis(actions, NULL))
		TRANSACTION_RETURN_LOG_FAILURE("Tried to run the 'No Missions or Nemesis' version of a GameAction transaction, but Nemeses must be locked!");
	if (gameaction_MustLockGameAccount(actions, NULL))
		TRANSACTION_RETURN_LOG_FAILURE("Tried to run the 'No Missions or Nemesis' version of a GameAction transaction, but Game Account must be locked!");
	if (gameaction_MustLockShardVariables(actions, NULL))
		TRANSACTION_RETURN_LOG_FAILURE("Tried to run the 'No Missions or Nemesis' version of a GameAction transaction, but Shard Variables must be locked!");
	if (gameaction_MustLockActivityLog(actions, NULL))
		TRANSACTION_RETURN_LOG_FAILURE("Tried to run the 'No Missions or Nemesis' version of a GameAction transaction, but Activity Log must be locked!");
	if (gameaction_MustLockGuildActivityLog(((NONNULL(ent->pPlayer)) && NONNULL(ent->pPlayer->pGuild)), actions, NULL))
		TRANSACTION_RETURN_LOG_FAILURE("Tried to run the 'No Missions or Nemesis' version of a GameAction transaction, but Guild Activity Log must be locked!");
	if (gameaction_MustLockGuild(actions, NULL))
		TRANSACTION_RETURN_LOG_FAILURE("Tried to run the 'No Missions or Nemesis' version of a GameAction transaction, but Guild must be locked!");

	// Does not run WARP actions within transaction

	// Non-transacted game actions
	if (TRANSACTION_OUTCOME_SUCCESS != gameaction_trh_SendFloaters(ATR_PASS_ARGS, actions))
		return TRANSACTION_OUTCOME_FAILURE;
	if (TRANSACTION_OUTCOME_SUCCESS != gameaction_trh_SendNotifications(ATR_PASS_ARGS, actions))
		return TRANSACTION_OUTCOME_FAILURE;
	if (TRANSACTION_OUTCOME_SUCCESS != gameaction_trh_UpdateItemAssignments(ATR_PASS_ARGS, actions))
		return TRANSACTION_OUTCOME_FAILURE;
	if (TRANSACTION_OUTCOME_SUCCESS != gameaction_trh_RunContacts(ATR_PASS_ARGS, actions))
		return TRANSACTION_OUTCOME_FAILURE;
	if (TRANSACTION_OUTCOME_SUCCESS != gameaction_trh_RunExpressions(ATR_PASS_ARGS, actions))
		return TRANSACTION_OUTCOME_FAILURE;

	// Transacted game actions
	if (TRANSACTION_OUTCOME_SUCCESS != 	gameaction_trh_TakeItems(ATR_PASS_ARGS, ent, actions, pReason, pExtract))
		return TRANSACTION_OUTCOME_FAILURE;
	if (TRANSACTION_OUTCOME_SUCCESS != 	gameaction_trh_GiveItems(ATR_PASS_ARGS, ent, actions, NULL, pReason, pExtract))
		return TRANSACTION_OUTCOME_FAILURE;
	if (TRANSACTION_OUTCOME_SUCCESS != 	gameaction_trh_GiveDoorKeyItems(ATR_PASS_ARGS, ent, actions, NULL, pMapVariableArray, pDoorDestVarArray, pchInitMapVars, pchMapName, pReason, pExtract))
		return TRANSACTION_OUTCOME_FAILURE;
	if (TRANSACTION_OUTCOME_SUCCESS != gameaction_trh_NPCSendEmail(ATR_PASS_ARGS, ent, actions, pExtract))
		return TRANSACTION_OUTCOME_FAILURE;

	return TRANSACTION_OUTCOME_SUCCESS;
}


// Only locks NPCEMail
AUTO_TRANS_HELPER
ATR_LOCKS(ent, ".Pplayer.pEmailV2.Ilastusedid, .Pplayer.Langid, .Hallegiance, .Hsuballegiance, .Pplayer.Pplayeraccountdata.Eagamepermissionmaxvaluenumerics, .Pplayer.pEmailV2.Mail, .Pchar.Ilevelexp");
enumTransactionOutcome gameaction_trh_RunActionsLockNPCEMailOnly(ATR_ARGS, ATH_ARG NOCONST(Entity)* ent, CONST_EARRAY_OF(WorldGameActionProperties)* actions, GameAccountDataExtract *pExtract)
{
	if (gameaction_MustLockMissions(actions, NULL))
		TRANSACTION_RETURN_LOG_FAILURE("Tried to run the 'NPCEmailOnly' version of a GameAction transaction, but Missions must be locked!");
	if (gameaction_MustLockNemesis(actions, NULL))
		TRANSACTION_RETURN_LOG_FAILURE("Tried to run the 'NPCEmailOnly' version of a GameAction transaction, but Nemeses must be locked!");
	if (gameaction_MustLockInventory(actions, NULL))
		TRANSACTION_RETURN_LOG_FAILURE("Tried to run the 'NPCEmailOnly' version of a GameAction transaction, but Inventory must be locked!");
	if (gameaction_MustLockGameAccount(actions, NULL))
		TRANSACTION_RETURN_LOG_FAILURE("Tried to run the 'NPCEmailOnly' version of a GameAction transaction, but Game Account must be locked!");
	if (gameaction_MustLockShardVariables(actions, NULL))
		TRANSACTION_RETURN_LOG_FAILURE("Tried to run the 'NPCEmailOnly' version of a GameAction transaction, but Shard Variables must be locked!");
	if (gameaction_MustLockActivityLog(actions, NULL))
		TRANSACTION_RETURN_LOG_FAILURE("Tried to run the 'NPCEmailOnly' version of a GameAction transaction, but Activity Log must be locked!");
	if (gameaction_MustLockGuildActivityLog(((NONNULL(ent->pPlayer)) && NONNULL(ent->pPlayer->pGuild)), actions, NULL))
		TRANSACTION_RETURN_LOG_FAILURE("Tried to run the 'NPCEmailOnly' version of a GameAction transaction, but Guild Activity Log must be locked!");
	if (gameaction_MustLockGuild(actions, NULL))
		TRANSACTION_RETURN_LOG_FAILURE("Tried to run the 'NPCEmailOnly' version of a GameAction transaction, but Guild must be locked!");

	// Does not run WARP actions within transaction

	// Non-transacted game actions
	if (TRANSACTION_OUTCOME_SUCCESS != gameaction_trh_SendFloaters(ATR_PASS_ARGS, actions))
		return TRANSACTION_OUTCOME_FAILURE;
	if (TRANSACTION_OUTCOME_SUCCESS != gameaction_trh_SendNotifications(ATR_PASS_ARGS, actions))
		return TRANSACTION_OUTCOME_FAILURE;
	if (TRANSACTION_OUTCOME_SUCCESS != gameaction_trh_UpdateItemAssignments(ATR_PASS_ARGS, actions))
		return TRANSACTION_OUTCOME_FAILURE;
	if (TRANSACTION_OUTCOME_SUCCESS != gameaction_trh_RunContacts(ATR_PASS_ARGS, actions))
		return TRANSACTION_OUTCOME_FAILURE;
	if (TRANSACTION_OUTCOME_SUCCESS != gameaction_trh_RunExpressions(ATR_PASS_ARGS, actions))
		return TRANSACTION_OUTCOME_FAILURE;

	// Transacted game actions
	if (TRANSACTION_OUTCOME_SUCCESS != gameaction_trh_NPCSendEmail(ATR_PASS_ARGS, ent, actions, pExtract))
		return TRANSACTION_OUTCOME_FAILURE;

	return TRANSACTION_OUTCOME_SUCCESS;
}


// Only locks Activity Log
AUTO_TRANS_HELPER
ATR_LOCKS(ent, ".Psaved.Nextactivitylogid, .Psaved.Activitylogentries[AO]");
enumTransactionOutcome gameaction_trh_RunActionsLockActivityLogOnly(ATR_ARGS, ATH_ARG NOCONST(Entity)* ent, CONST_EARRAY_OF(WorldGameActionProperties)* actions)
{
	if (gameaction_MustLockMissions(actions, NULL))
		TRANSACTION_RETURN_LOG_FAILURE("Tried to run the 'ActivityLog' version of a GameAction transaction, but Missions must be locked!");
	if (gameaction_MustLockNemesis(actions, NULL))
		TRANSACTION_RETURN_LOG_FAILURE("Tried to run the 'ActivityLog' version of a GameAction transaction, but Nemeses must be locked!");
	if (gameaction_MustLockInventory(actions, NULL))
		TRANSACTION_RETURN_LOG_FAILURE("Tried to run the 'ActivityLog' version of a GameAction transaction, but Inventory must be locked!");
	if (gameaction_MustLockGameAccount(actions, NULL))
		TRANSACTION_RETURN_LOG_FAILURE("Tried to run the 'ActivityLog' version of a GameAction transaction, but Game Account must be locked!");
	if (gameaction_MustLockShardVariables(actions, NULL))
		TRANSACTION_RETURN_LOG_FAILURE("Tried to run the 'ActivityLog' version of a GameAction transaction, but Shard Variables must be locked!");
	if (gameaction_MustLockNPCEMail(actions, NULL))
		TRANSACTION_RETURN_LOG_FAILURE("Tried to run the 'ActivityLog' version of a GameAction transaction, but NPC Email must be locked!");
	if (gameaction_MustLockGuildActivityLog(((NONNULL(ent->pPlayer)) && NONNULL(ent->pPlayer->pGuild)), actions, NULL))
		TRANSACTION_RETURN_LOG_FAILURE("Tried to run the 'ActivityLog' version of a GameAction transaction, but Guild Activity Log must be locked!");
	if (gameaction_MustLockGuild(actions, NULL))
		TRANSACTION_RETURN_LOG_FAILURE("Tried to run the 'ActivityLog' version of a GameAction transaction, but guild must be locked!");

	// Does not run WARP actions within transaction

	// Non-transacted game actions
	if (TRANSACTION_OUTCOME_SUCCESS != gameaction_trh_SendFloaters(ATR_PASS_ARGS, actions))
		return TRANSACTION_OUTCOME_FAILURE;
	if (TRANSACTION_OUTCOME_SUCCESS != gameaction_trh_SendNotifications(ATR_PASS_ARGS, actions))
		return TRANSACTION_OUTCOME_FAILURE;
	if (TRANSACTION_OUTCOME_SUCCESS != gameaction_trh_UpdateItemAssignments(ATR_PASS_ARGS, actions))
		return TRANSACTION_OUTCOME_FAILURE;
	if (TRANSACTION_OUTCOME_SUCCESS != gameaction_trh_RunContacts(ATR_PASS_ARGS, actions))
		return TRANSACTION_OUTCOME_FAILURE;
	if (TRANSACTION_OUTCOME_SUCCESS != gameaction_trh_RunExpressions(ATR_PASS_ARGS, actions))
		return TRANSACTION_OUTCOME_FAILURE;

	// Transacted game actions
	if (TRANSACTION_OUTCOME_SUCCESS != gameaction_trh_ActivityLog(ATR_PASS_ARGS, ent, actions))
		return TRANSACTION_OUTCOME_FAILURE;

	return TRANSACTION_OUTCOME_SUCCESS;
}


// Only locks Activity Log and guild log
AUTO_TRANS_HELPER
ATR_LOCKS(ent, ".Psaved.Nextactivitylogid, .Psaved.Activitylogentries[AO]")
ATR_LOCKS(pGuild, ".Inextactivitylogentryid, .Eaactivityentries, .Eamembers");
enumTransactionOutcome gameaction_trh_RunActionsLockActivityLogWithGuildOnly(ATR_ARGS, ATH_ARG NOCONST(Entity)* ent, ATH_ARG NOCONST(Guild) *pGuild, CONST_EARRAY_OF(WorldGameActionProperties)* actions)
{
	if (gameaction_MustLockMissions(actions, NULL))
		TRANSACTION_RETURN_LOG_FAILURE("Tried to run the 'ActivityLog' version of a GameAction transaction, but Missions must be locked!");
	if (gameaction_MustLockNemesis(actions, NULL))
		TRANSACTION_RETURN_LOG_FAILURE("Tried to run the 'ActivityLog' version of a GameAction transaction, but Nemeses must be locked!");
	if (gameaction_MustLockInventory(actions, NULL))
		TRANSACTION_RETURN_LOG_FAILURE("Tried to run the 'ActivityLog' version of a GameAction transaction, but Inventory must be locked!");
	if (gameaction_MustLockGameAccount(actions, NULL))
		TRANSACTION_RETURN_LOG_FAILURE("Tried to run the 'ActivityLog' version of a GameAction transaction, but Game Account must be locked!");
	if (gameaction_MustLockShardVariables(actions, NULL))
		TRANSACTION_RETURN_LOG_FAILURE("Tried to run the 'ActivityLog' version of a GameAction transaction, but Shard Variables must be locked!");
	if (gameaction_MustLockNPCEMail(actions, NULL))
		TRANSACTION_RETURN_LOG_FAILURE("Tried to run the 'ActivityLog' version of a GameAction transaction, but NPC Email must be locked!");
	if (gameaction_MustLockGuild(actions, NULL))
		TRANSACTION_RETURN_LOG_FAILURE("Tried to run the 'ActivityLog' version of a GameAction transaction, but guild must be locked!");

	// Does not run WARP actions within transaction

	// Non-transacted game actions
	if (TRANSACTION_OUTCOME_SUCCESS != gameaction_trh_SendFloaters(ATR_PASS_ARGS, actions))
		return TRANSACTION_OUTCOME_FAILURE;
	if (TRANSACTION_OUTCOME_SUCCESS != gameaction_trh_SendNotifications(ATR_PASS_ARGS, actions))
		return TRANSACTION_OUTCOME_FAILURE;
	if (TRANSACTION_OUTCOME_SUCCESS != gameaction_trh_UpdateItemAssignments(ATR_PASS_ARGS, actions))
		return TRANSACTION_OUTCOME_FAILURE;
	if (TRANSACTION_OUTCOME_SUCCESS != gameaction_trh_RunContacts(ATR_PASS_ARGS, actions))
		return TRANSACTION_OUTCOME_FAILURE;
	if (TRANSACTION_OUTCOME_SUCCESS != gameaction_trh_RunExpressions(ATR_PASS_ARGS, actions))
		return TRANSACTION_OUTCOME_FAILURE;

	// Transacted game actions
	if (TRANSACTION_OUTCOME_SUCCESS != gameaction_trh_ActivityLog(ATR_PASS_ARGS, ent, actions))
		return TRANSACTION_OUTCOME_FAILURE;
	if (TRANSACTION_OUTCOME_SUCCESS != gameaction_trh_GuildActivityLog(ATR_PASS_ARGS, pGuild, ent->myContainerID, actions))
		return TRANSACTION_OUTCOME_FAILURE;

	return TRANSACTION_OUTCOME_SUCCESS;
}


// Only locks guild
AUTO_TRANS_HELPER
ATR_LOCKS(pGuild, ".Pguildstatsinfo, .Htheme");
enumTransactionOutcome gameaction_trh_RunActionsLockGuildOnly(ATR_ARGS, ATH_ARG NOCONST(Guild) *pGuild, CONST_EARRAY_OF(WorldGameActionProperties)* actions)
{
	if (gameaction_MustLockMissions(actions, NULL))
		TRANSACTION_RETURN_LOG_FAILURE("Tried to run the 'Guild' version of a GameAction transaction, but Missions must be locked!");
	if (gameaction_MustLockNemesis(actions, NULL))
		TRANSACTION_RETURN_LOG_FAILURE("Tried to run the 'Guild' version of a GameAction transaction, but Nemeses must be locked!");
	if (gameaction_MustLockInventory(actions, NULL))
		TRANSACTION_RETURN_LOG_FAILURE("Tried to run the 'Guild' version of a GameAction transaction, but Inventory must be locked!");
	if (gameaction_MustLockGameAccount(actions, NULL))
		TRANSACTION_RETURN_LOG_FAILURE("Tried to run the 'Guild' version of a GameAction transaction, but Game Account must be locked!");
	if (gameaction_MustLockShardVariables(actions, NULL))
		TRANSACTION_RETURN_LOG_FAILURE("Tried to run the 'Guild' version of a GameAction transaction, but Shard Variables must be locked!");
	if (gameaction_MustLockNPCEMail(actions, NULL))
		TRANSACTION_RETURN_LOG_FAILURE("Tried to run the 'Guild' version of a GameAction transaction, but NPC Email must be locked!");
	if (gameaction_MustLockActivityLog(actions, NULL))
		TRANSACTION_RETURN_LOG_FAILURE("Tried to run the 'Guild' version of a GameAction transaction, but Activity Log must be locked!");
	if (gameaction_MustLockGuildActivityLog(NONNULL(pGuild), actions, NULL))
		TRANSACTION_RETURN_LOG_FAILURE("Tried to run the 'Guild' version of a GameAction transaction, but Guild Activity Log must be locked!");


	// Does not run WARP actions within transaction

	// Non-transacted game actions
	if (TRANSACTION_OUTCOME_SUCCESS != gameaction_trh_SendFloaters(ATR_PASS_ARGS, actions))
		return TRANSACTION_OUTCOME_FAILURE;
	if (TRANSACTION_OUTCOME_SUCCESS != gameaction_trh_SendNotifications(ATR_PASS_ARGS, actions))
		return TRANSACTION_OUTCOME_FAILURE;
	if (TRANSACTION_OUTCOME_SUCCESS != gameaction_trh_UpdateItemAssignments(ATR_PASS_ARGS, actions))
		return TRANSACTION_OUTCOME_FAILURE;
	if (TRANSACTION_OUTCOME_SUCCESS != gameaction_trh_RunContacts(ATR_PASS_ARGS, actions))
		return TRANSACTION_OUTCOME_FAILURE;
	if (TRANSACTION_OUTCOME_SUCCESS != gameaction_trh_RunExpressions(ATR_PASS_ARGS, actions))
		return TRANSACTION_OUTCOME_FAILURE;

	// Transacted game actions
	if (TRANSACTION_OUTCOME_SUCCESS != gameaction_trh_RunGuildStatUpdates(ATR_PASS_ARGS, actions, pGuild))
		return TRANSACTION_OUTCOME_FAILURE;
	if (TRANSACTION_OUTCOME_SUCCESS != gameaction_trh_RunGuildThemeSetOperations(ATR_PASS_ARGS, actions, pGuild))
		return TRANSACTION_OUTCOME_FAILURE;
	
	return TRANSACTION_OUTCOME_SUCCESS;
}


// Doesn't lock the player's inventory or nemeses
AUTO_TRANS_HELPER
ATR_LOCKS(ent, ".Pplayer.pEmailV2.Ilastusedid, .Pplayer.Langid, .Hallegiance, .Hsuballegiance, .Pplayer.Pplayeraccountdata.Eagamepermissionmaxvaluenumerics, .Pplayer.pEmailV2.Mail, .Pchar.Ilevelexp");
enumTransactionOutcome gameaction_trh_RunActionsNoInventoryOrNemesis(ATR_ARGS, ATH_ARG NOCONST(Entity)* ent, CONST_EARRAY_OF(WorldGameActionProperties)* actions, WorldVariableArray *pMapVariableArray, GameAccountDataExtract *pExtract)
{
	if (gameaction_MustLockInventory(actions, NULL))
		TRANSACTION_RETURN_LOG_FAILURE("Tried to run the 'No Inventory or Nemesis' version of a GameAction transaction, but Inventory must be locked!");
	if (gameaction_MustLockNemesis(actions, NULL))
		TRANSACTION_RETURN_LOG_FAILURE("Tried to run the 'No Inventory or Nemesis' version of a GameAction transaction, but Nemeses must be locked!");
	if (gameaction_MustLockGameAccount(actions, NULL))
		TRANSACTION_RETURN_LOG_FAILURE("Tried to run the 'No Inventory or Nemesis' version of a GameAction transaction, but Game Account must be locked!");
	if (gameaction_MustLockShardVariables(actions, NULL))
		TRANSACTION_RETURN_LOG_FAILURE("Tried to run the 'No Inventory or Nemesis' version of a GameAction transaction, but Shard Variables must be locked!");
	if (gameaction_MustLockActivityLog(actions, NULL))
		TRANSACTION_RETURN_LOG_FAILURE("Tried to run the 'No Inventory or Nemesis' version of a GameAction transaction, but Activity Log must be locked!");
	if (gameaction_MustLockGuildActivityLog(((NONNULL(ent->pPlayer)) && NONNULL(ent->pPlayer->pGuild)), actions, NULL))
		TRANSACTION_RETURN_LOG_FAILURE("Tried to run the 'No Inventory or Nemesis' version of a GameAction transaction, but Guild Activity Log must be locked!");
	if (gameaction_MustLockGuild(actions, NULL))
		TRANSACTION_RETURN_LOG_FAILURE("Tried to run the 'No Inventory or Nemesis' version of a GameAction transaction, but Guild must be locked!");

	// Does not run WARP actions within transaction

	// Non-transacted game actions
	if (TRANSACTION_OUTCOME_SUCCESS != gameaction_trh_SendFloaters(ATR_PASS_ARGS, actions))
		return TRANSACTION_OUTCOME_FAILURE;
	if (TRANSACTION_OUTCOME_SUCCESS != gameaction_trh_SendNotifications(ATR_PASS_ARGS, actions))
		return TRANSACTION_OUTCOME_FAILURE;
	if (TRANSACTION_OUTCOME_SUCCESS != gameaction_trh_UpdateItemAssignments(ATR_PASS_ARGS, actions))
		return TRANSACTION_OUTCOME_FAILURE;
	if (TRANSACTION_OUTCOME_SUCCESS != gameaction_trh_RunContacts(ATR_PASS_ARGS, actions))
		return TRANSACTION_OUTCOME_FAILURE;
	if (TRANSACTION_OUTCOME_SUCCESS != gameaction_trh_RunExpressions(ATR_PASS_ARGS, actions))
		return TRANSACTION_OUTCOME_FAILURE;

	// Transacted game actions
	if (TRANSACTION_OUTCOME_SUCCESS != gameaction_trh_GrantMissions(ATR_PASS_ARGS, ent, NULL, NULL, NULL, NULL, actions, pMapVariableArray))
		return TRANSACTION_OUTCOME_FAILURE;
	if (TRANSACTION_OUTCOME_SUCCESS != gameaction_trh_RunMissionOffers(ATR_PASS_ARGS, actions, NULL, pMapVariableArray))
		return TRANSACTION_OUTCOME_FAILURE;
	if (TRANSACTION_OUTCOME_SUCCESS != gameaction_trh_NPCSendEmail(ATR_PASS_ARGS, ent, actions, pExtract))
		return TRANSACTION_OUTCOME_FAILURE;

	return TRANSACTION_OUTCOME_SUCCESS;
}


// Doesn't lock the player's inventory or nemeses
AUTO_TRANS_HELPER
	ATR_LOCKS(ent, ".Pplayer.Nemesisinfo.Eanemesisstates, .Pplayer.Pplayeraccountdata.Eagamepermissionmaxvaluenumerics, .Pplayer.pEmailV2.Mail, .Pplayer.pEmailV2.Ilastusedid, .Pplayer.Langid, .Hallegiance, .Hsuballegiance, .Pchar.Ilevelexp");
enumTransactionOutcome gameaction_trh_RunActionsNoInventoryOrMissions(ATR_ARGS, ATH_ARG NOCONST(Entity)* ent, CONST_EARRAY_OF(WorldGameActionProperties)* actions, GameAccountDataExtract *pExtract)
{
	if (gameaction_MustLockInventory(actions, NULL))
		TRANSACTION_RETURN_LOG_FAILURE("Tried to run the 'No Inventory or Missions' version of a GameAction transaction, but Inventory must be locked!");
	if (gameaction_MustLockMissions(actions, NULL))
		TRANSACTION_RETURN_LOG_FAILURE("Tried to run the 'No Inventory or Missions' version of a GameAction transaction, but Missions must be locked!");
	if (gameaction_MustLockGameAccount(actions, NULL))
		TRANSACTION_RETURN_LOG_FAILURE("Tried to run the 'No Inventory or Missions' version of a GameAction transaction, but Game Account must be locked!");
	if (gameaction_MustLockShardVariables(actions, NULL))
		TRANSACTION_RETURN_LOG_FAILURE("Tried to run the 'No Inventory or Missions' version of a GameAction transaction, but Shard Variables must be locked!");
	if (gameaction_MustLockActivityLog(actions, NULL))
		TRANSACTION_RETURN_LOG_FAILURE("Tried to run the 'No Inventory or Missions' version of a GameAction transaction, but Activity Log must be locked!");
	if (gameaction_MustLockGuildActivityLog(((NONNULL(ent->pPlayer)) && NONNULL(ent->pPlayer->pGuild)), actions, NULL))
		TRANSACTION_RETURN_LOG_FAILURE("Tried to run the 'No Inventory or Missions' version of a GameAction transaction, but Guild Activity Log must be locked!");
	if (gameaction_MustLockGuild(actions, NULL))
		TRANSACTION_RETURN_LOG_FAILURE("Tried to run the 'No Inventory or Missions' version of a GameAction transaction, but guild must be locked!");

	// Does not run WARP actions within transaction

	// Non-transacted game actions
	if (TRANSACTION_OUTCOME_SUCCESS != gameaction_trh_SendFloaters(ATR_PASS_ARGS, actions))
		return TRANSACTION_OUTCOME_FAILURE;
	if (TRANSACTION_OUTCOME_SUCCESS != gameaction_trh_SendNotifications(ATR_PASS_ARGS, actions))
		return TRANSACTION_OUTCOME_FAILURE;
	if (TRANSACTION_OUTCOME_SUCCESS != gameaction_trh_UpdateItemAssignments(ATR_PASS_ARGS, actions))
		return TRANSACTION_OUTCOME_FAILURE;
	if (TRANSACTION_OUTCOME_SUCCESS != gameaction_trh_RunContacts(ATR_PASS_ARGS, actions))
		return TRANSACTION_OUTCOME_FAILURE;
	if (TRANSACTION_OUTCOME_SUCCESS != gameaction_trh_RunExpressions(ATR_PASS_ARGS, actions))
		return TRANSACTION_OUTCOME_FAILURE;

	// Transacted game actions
	if (TRANSACTION_OUTCOME_SUCCESS != gameaction_trh_ChangeNemesisState(ATR_PASS_ARGS, ent, actions))
		return TRANSACTION_OUTCOME_FAILURE;
	if (TRANSACTION_OUTCOME_SUCCESS != gameaction_trh_NPCSendEmail(ATR_PASS_ARGS, ent, actions, pExtract))
		return TRANSACTION_OUTCOME_FAILURE;

	return TRANSACTION_OUTCOME_SUCCESS;
}


// Doesn't lock anything; only performs non-transaction actions
enumTransactionOutcome gameaction_trh_RunActionsNoLocking(ATR_ARGS, CONST_EARRAY_OF(WorldGameActionProperties)* actions)
{
	if (gameaction_MustLockMissions(actions, NULL))
		TRANSACTION_RETURN_LOG_FAILURE("Tried to run the 'No Locking' version of a GameAction transaction, but Missions must be locked!");
	if (gameaction_MustLockInventory(actions, NULL))
		TRANSACTION_RETURN_LOG_FAILURE("Tried to run the 'No Locking' version of a GameAction transaction, but Inventory must be locked!");
	if (gameaction_MustLockNemesis(actions, NULL))
		TRANSACTION_RETURN_LOG_FAILURE("Tried to run the 'No Locking' version of a GameAction transaction, but Nemeses must be locked!");
	if (gameaction_MustLockNPCEMail(actions, NULL))
		TRANSACTION_RETURN_LOG_FAILURE("Tried to run the 'No Locking' version of a GameAction transaction, but NPCEMail must be locked!");
	if (gameaction_MustLockGameAccount(actions, NULL))
		TRANSACTION_RETURN_LOG_FAILURE("Tried to run the 'No Locking' version of a GameAction transaction, but Game Account must be locked!");
	if (gameaction_MustLockShardVariables(actions, NULL))
		TRANSACTION_RETURN_LOG_FAILURE("Tried to run the 'No Locking' version of a GameAction transaction, but Shard Variables must be locked!");
	if (gameaction_MustLockActivityLog(actions, NULL))
		TRANSACTION_RETURN_LOG_FAILURE("Tried to run the 'No Locking' version of a GameAction transaction, but Activity Log must be locked!");
	if (gameaction_MustLockGuild(actions, NULL))
		TRANSACTION_RETURN_LOG_FAILURE("Tried to run the 'No Locking' version of a GameAction transaction, but guild must be locked!");

	// Does not run WARP actions within transaction

	// Non-transacted game actions
	if (TRANSACTION_OUTCOME_SUCCESS != gameaction_trh_SendFloaters(ATR_PASS_ARGS, actions))
		return TRANSACTION_OUTCOME_FAILURE;
	if (TRANSACTION_OUTCOME_SUCCESS != gameaction_trh_SendNotifications(ATR_PASS_ARGS, actions))
		return TRANSACTION_OUTCOME_FAILURE;
	if (TRANSACTION_OUTCOME_SUCCESS != gameaction_trh_UpdateItemAssignments(ATR_PASS_ARGS, actions))
		return TRANSACTION_OUTCOME_FAILURE;
	if (TRANSACTION_OUTCOME_SUCCESS != gameaction_trh_RunContacts(ATR_PASS_ARGS, actions))
		return TRANSACTION_OUTCOME_FAILURE;
	if (TRANSACTION_OUTCOME_SUCCESS != gameaction_trh_RunExpressions(ATR_PASS_ARGS, actions))
		return TRANSACTION_OUTCOME_FAILURE;

	return TRANSACTION_OUTCOME_SUCCESS;
}


// Sub-mission action-running

// The other versions of this function (NoInventory, etc.) are never used, because this is called inside transactions
// and it would be a huge pain to create NoInventory versions of its parents, etc.
AUTO_TRANS_HELPER
	ATR_LOCKS(ent, ".Pplayer.Skilltype, .Pplayer.Eskillspecialization, .Psaved.pSCPdata.Isummonedscp, .Psaved.pSCPdata.Fcachedpetbonusxppct, .Psaved.pSCPdata.erSCP, .Pchar.Ppsecondarypaths, .Pplayer.Pugckillcreditlimit, .Pplayer.pEmailV2.Mail, .Psaved.Nextactivitylogid, .Hallegiance, .Hsuballegiance, .Pchar.Ilevelexp, .Pplayer.Pplayeraccountdata.Eagamepermissionmaxvaluenumerics, .Psaved.Activitylogentries[AO], .Psaved.Uiindexbuild, .Psaved.Ppbuilds, .Psaved.Conowner.Containerid, .Psaved.Conowner.Containertype, .Pinventoryv2.Peaowneduniqueitems, .Pinventoryv2.Ppinventorybags, .Pinventoryv2.Pplitebags, .Pplayer.Playertype, .Psaved.Ppownedcontainers, .Itemidmax, .Pchar.Uipoweridmax, .Psaved.Ipetid, .Psaved.Ppuppetmaster.Curid, .Psaved.Ppuppetmaster.Curtype, .Psaved.Ppuppetmaster.Curtempid, .Psaved.Ppallowedcritterpets, .Pplayer.Missioninfo.Unextrequestid, .Pplayer.Missioninfo.Completedmissions, .Pplayer.Earewardmods, .Pteam.Lastrecruittype, .Psaved.Costumedata.Eaunlockedcostumerefs, .Pplayer.Pplayeraccountdata.Eapendingkeys, .Pplayer.Missioninfo.Missions, .Pplayer.Missioninfo.Eamissionrequests, .Pplayer.Missioninfo.Eamissioncooldowns, .Pplayer.Nemesisinfo.Eanemesisstates, .Pplayer.pEmailV2.Ilastusedid, .Pplayer.Langid, .Egender, .Pchar.Hclass, .Pchar.Hpath, .Pchar.Hspecies, .Pplayer.eaRewardGatedData")
	ATR_LOCKS(eaVarContainer, ".id, .Uclock, .Eaworldvars")
	ATR_LOCKS(pGuild, ".Inextactivitylogentryid, .Eaactivityentries, .Eamembers, .Pguildstatsinfo, .Htheme")
	ATR_LOCKS(pMission, ".*");
enumTransactionOutcome gameaction_trh_RunActionsSubMissions(ATR_ARGS, ATH_ARG NOCONST(Entity)* ent, ATH_ARG CONST_EARRAY_OF(NOCONST(ShardVariableContainer)) eaVarContainer, ATH_ARG NOCONST(Guild) *pGuild, ATH_ARG NOCONST(Mission)* pMission, MissionDef *pDef, CONST_EARRAY_OF(WorldGameActionProperties)* actions, WorldVariableArray* pMapVariableArray, GameActionDoorDestinationVarArray* pDoorDestVarArray, const char* pchInitMapVars, const char* pchMapName, const ItemChangeReason *pReason, GameAccountDataExtract *pExtract)
{
	// Does not run WARP actions within transaction

	// Non-transacted game actions
	if (TRANSACTION_OUTCOME_SUCCESS != gameaction_trh_SendFloaters(ATR_PASS_ARGS, actions))
		return TRANSACTION_OUTCOME_FAILURE;
	if (TRANSACTION_OUTCOME_SUCCESS != gameaction_trh_SendNotifications(ATR_PASS_ARGS, actions))
		return TRANSACTION_OUTCOME_FAILURE;
	if (TRANSACTION_OUTCOME_SUCCESS != gameaction_trh_UpdateItemAssignments(ATR_PASS_ARGS, actions))
		return TRANSACTION_OUTCOME_FAILURE;
	if (TRANSACTION_OUTCOME_SUCCESS != gameaction_trh_RunContacts(ATR_PASS_ARGS, actions))
		return TRANSACTION_OUTCOME_FAILURE;
	if (TRANSACTION_OUTCOME_SUCCESS != gameaction_trh_RunExpressions(ATR_PASS_ARGS, actions))
		return TRANSACTION_OUTCOME_FAILURE;

	// Transacted game actions
	if (TRANSACTION_OUTCOME_SUCCESS != 	gameaction_trh_TakeItems(ATR_PASS_ARGS, ent, actions, pReason, pExtract))
		return TRANSACTION_OUTCOME_FAILURE;
	if (TRANSACTION_OUTCOME_SUCCESS != 	gameaction_trh_GiveItems(ATR_PASS_ARGS, ent, actions, NULL, pReason, pExtract))
		return TRANSACTION_OUTCOME_FAILURE;
	if (TRANSACTION_OUTCOME_SUCCESS != 	gameaction_trh_GiveDoorKeyItems(ATR_PASS_ARGS, ent, actions, pDef?pDef->name:NULL, pMapVariableArray, pDoorDestVarArray, pchInitMapVars, pchMapName, pReason, pExtract))
		return TRANSACTION_OUTCOME_FAILURE;
	if (TRANSACTION_OUTCOME_SUCCESS != gameaction_trh_GrantSubMissions(ATR_PASS_ARGS, ent, eaVarContainer, pGuild, pMission, pDef, actions, pMapVariableArray, pDoorDestVarArray, pchInitMapVars, pchMapName, pReason, pExtract))
		return TRANSACTION_OUTCOME_FAILURE;
	if (TRANSACTION_OUTCOME_SUCCESS != gameaction_trh_GrantMissions(ATR_PASS_ARGS, ent, eaVarContainer, pGuild, pMission, pDef, actions, pMapVariableArray))
		return TRANSACTION_OUTCOME_FAILURE;
	if (TRANSACTION_OUTCOME_SUCCESS != gameaction_trh_RunMissionOffers(ATR_PASS_ARGS, actions, pMission, pMapVariableArray))
		return TRANSACTION_OUTCOME_FAILURE;
	if (TRANSACTION_OUTCOME_SUCCESS != gameaction_trh_DropMissions(ATR_PASS_ARGS, ent, actions, pReason, pExtract))
		return TRANSACTION_OUTCOME_FAILURE;
	if (TRANSACTION_OUTCOME_SUCCESS != gameaction_trh_ChangeNemesisState(ATR_PASS_ARGS, ent, actions))
		return TRANSACTION_OUTCOME_FAILURE;
	if (TRANSACTION_OUTCOME_SUCCESS != gameaction_trh_NPCSendEmail(ATR_PASS_ARGS, ent, actions, pExtract))
		return TRANSACTION_OUTCOME_FAILURE;
	if (TRANSACTION_OUTCOME_SUCCESS !=  gameaction_trh_GADAttribValue(ATR_PASS_ARGS, ent, actions))
		return TRANSACTION_OUTCOME_FAILURE;
	if (TRANSACTION_OUTCOME_SUCCESS !=  gameaction_trh_ShardVariables(ATR_PASS_ARGS, eaVarContainer, actions))
		return TRANSACTION_OUTCOME_FAILURE;
	if (TRANSACTION_OUTCOME_SUCCESS !=  gameaction_trh_ActivityLog(ATR_PASS_ARGS, ent, actions))
		return TRANSACTION_OUTCOME_FAILURE;
	if (TRANSACTION_OUTCOME_SUCCESS !=  gameaction_trh_GuildActivityLog(ATR_PASS_ARGS, pGuild, ent->myContainerID, actions))
		return TRANSACTION_OUTCOME_FAILURE;
	if (TRANSACTION_OUTCOME_SUCCESS !=  gameaction_trh_RunGuildStatUpdates(ATR_PASS_ARGS, actions, pGuild))
		return TRANSACTION_OUTCOME_FAILURE;
	if (TRANSACTION_OUTCOME_SUCCESS !=  gameaction_trh_RunGuildThemeSetOperations(ATR_PASS_ARGS, actions, pGuild))
		return TRANSACTION_OUTCOME_FAILURE;

	return TRANSACTION_OUTCOME_SUCCESS;
}


// Doesn't lock the nemesis or inventory or missions
AUTO_TRANS_HELPER
ATR_LOCKS(ent, ".Pplayer.Missioninfo.Unextrequestid, .Pplayer.Missioninfo.Eamissionrequests[AO]")
ATR_LOCKS(pMission, ".Missionnameorig, .Starttime, .Eamissionvariables, .Children");
enumTransactionOutcome gameaction_trh_RunActionsSubMissionsNoLocking(ATR_ARGS, ATH_ARG NOCONST(Entity)* ent, ATH_ARG NOCONST(Mission)* pMission, MissionDef *pDef, MissionDef *pRootDef, CONST_EARRAY_OF(WorldGameActionProperties)* actions)
{
	if (gameaction_MustLockMissions(actions, pRootDef))
		TRANSACTION_RETURN_LOG_FAILURE("Tried to run the 'No Locking' version of a GameAction transaction, but missions must be locked!");
	if (gameaction_MustLockInventory(actions, pRootDef))
		TRANSACTION_RETURN_LOG_FAILURE("Tried to run the 'No Locking' version of a GameAction transaction, but Inventory must be locked!");
	if (gameaction_MustLockNemesis(actions, pRootDef))
		TRANSACTION_RETURN_LOG_FAILURE("Tried to run the 'No Locking' version of a GameAction transaction, but nemeses must be locked!");
	if (gameaction_MustLockNPCEMail(actions, NULL))
		TRANSACTION_RETURN_LOG_FAILURE("Tried to run the 'No Locking' version of a GameAction transaction, but NPCEMail must be locked!");
	if (gameaction_MustLockGameAccount(actions, NULL))
		TRANSACTION_RETURN_LOG_FAILURE("Tried to run the 'No Locking' version of a GameAction transaction, but Game Account must be locked!");
	if (gameaction_MustLockShardVariables(actions, NULL))
		TRANSACTION_RETURN_LOG_FAILURE("Tried to run the 'No Locking' version of a GameAction transaction, but Shard Variables must be locked!");
	if (gameaction_MustLockActivityLog(actions, NULL))
		TRANSACTION_RETURN_LOG_FAILURE("Tried to run the 'No Locking' version of a GameAction transaction, but Activity Log must be locked!");
	if (gameaction_MustLockGuild(actions, NULL))
		TRANSACTION_RETURN_LOG_FAILURE("Tried to run the 'No Locking' version of a GameAction transaction, but Guild must be locked!");

	// Does not run WARP actions within transaction

	// Non-transacted game actions
	if (TRANSACTION_OUTCOME_SUCCESS != gameaction_trh_SendFloaters(ATR_PASS_ARGS, actions))
		return TRANSACTION_OUTCOME_FAILURE;
	if (TRANSACTION_OUTCOME_SUCCESS != gameaction_trh_SendNotifications(ATR_PASS_ARGS, actions))
		return TRANSACTION_OUTCOME_FAILURE;
	if (TRANSACTION_OUTCOME_SUCCESS != gameaction_trh_UpdateItemAssignments(ATR_PASS_ARGS, actions))
		return TRANSACTION_OUTCOME_FAILURE;
	if (TRANSACTION_OUTCOME_SUCCESS != gameaction_trh_RunContacts(ATR_PASS_ARGS, actions))
		return TRANSACTION_OUTCOME_FAILURE;
	if (TRANSACTION_OUTCOME_SUCCESS != gameaction_trh_RunExpressions(ATR_PASS_ARGS, actions))
		return TRANSACTION_OUTCOME_FAILURE;

	if (TRANSACTION_OUTCOME_SUCCESS != gameaction_trh_GrantSubMissionsNoLocking(ATR_PASS_ARGS, ent, pMission, pDef, pRootDef, actions))
		return TRANSACTION_OUTCOME_FAILURE;

	return TRANSACTION_OUTCOME_SUCCESS;
}


//----------------------------------------------------------------
// GrantMission Actions
//----------------------------------------------------------------

// This executes all GrantMission actions, treating the new missions as submissions of the given Mission
// pDef - pMission's MissionDef.  (This has to be passed in because of how MadLibs missions' defs work)
AUTO_TRANS_HELPER
ATR_LOCKS(ent, ".Pplayer.Skilltype, .Pplayer.Eskillspecialization, .Psaved.pSCPdata.Isummonedscp, .Psaved.pSCPdata.Fcachedpetbonusxppct, .Psaved.pSCPdata.erSCP, .Pchar.Ppsecondarypaths, .Egender, .Pchar.Hclass, .Pchar.Hpath, .Pchar.Hspecies, .Pplayer.Missioninfo.Unextrequestid, .Pplayer.Missioninfo.Completedmissions, .Pplayer.Missioninfo.Missions, .Pplayer.Missioninfo.Eamissionrequests, .Pplayer.Missioninfo.Eamissioncooldowns, .Hallegiance, .Hsuballegiance, .Pplayer.pEmailV2.Mail, .Psaved.Nextactivitylogid, .Psaved.Activitylogentries[AO], .Itemidmax, .Pplayer.Pugckillcreditlimit, .Pplayer.Pplayeraccountdata.Eagamepermissionmaxvaluenumerics, .Pchar.Ilevelexp, .Pinventoryv2.Peaowneduniqueitems, .Pinventoryv2.Ppinventorybags, .Pinventoryv2.Pplitebags, .Psaved.Conowner.Containerid, .Psaved.Conowner.Containertype, .Psaved.Uiindexbuild, .Psaved.Ppbuilds, .Psaved.Ppownedcontainers, .Pchar.Uipoweridmax, .Psaved.Ipetid, .Psaved.Ppuppetmaster.Curid, .Psaved.Ppuppetmaster.Curtype, .Psaved.Ppuppetmaster.Curtempid, .Psaved.Ppallowedcritterpets, .Pplayer.Earewardmods, .Pteam.Lastrecruittype, .Psaved.Costumedata.Eaunlockedcostumerefs, .Pplayer.Pplayeraccountdata.Eapendingkeys, .Pplayer.Nemesisinfo.Eanemesisstates, .Pplayer.pEmailV2.Ilastusedid, .Pplayer.Langid, .Pplayer.Playertype, .Pplayer.eaRewardGatedData")
ATR_LOCKS(eaVarContainer, ".id, .Uclock, .Eaworldvars")
ATR_LOCKS(pGuild, ".Inextactivitylogentryid, .Eaactivityentries, .Eamembers, .Pguildstatsinfo, .Htheme")
ATR_LOCKS(pMission, ".Missionnameorig, .Starttime, .Eamissionvariables, .Children");
enumTransactionOutcome gameaction_trh_GrantSubMissions(ATR_ARGS, ATH_ARG NOCONST(Entity)* ent, ATH_ARG CONST_EARRAY_OF(NOCONST(ShardVariableContainer)) eaVarContainer, ATH_ARG NOCONST(Guild)*pGuild, ATH_ARG NOCONST(Mission)* pMission, MissionDef *pDef, CONST_EARRAY_OF(WorldGameActionProperties)* actions, WorldVariableArray* pMapVariables, GameActionDoorDestinationVarArray* pDoorDestVarArray, const char* pchInitMapVars, const char* pchMapName, const ItemChangeReason *pReason, GameAccountDataExtract *pExtract)
{
	MissionDef *pRootDef = pDef;
	int i;
	
	// Passing in a Mission without a MissionDef is a programmer error
	if (!(NONNULL(pMission) && NONNULL(pDef))) {
		TRANSACTION_RETURN_FAILURE("Programmer error calling gameaction_trh_GrantSubMissions: NULL Mission or MissionDef");
	}

	while (GET_REF(pRootDef->parentDef)) {
		pRootDef = GET_REF(pRootDef->parentDef);
	}

	for (i = 0; i < eaSize(actions); i++) {
		WorldGameActionProperties *action = (*actions)[i];
		if (action->eActionType == WorldGameActionType_GrantSubMission) {
			const char *pchNewSubMissionName = action->pGrantSubMissionProperties->pcSubMissionName;
			if (pchNewSubMissionName && pchNewSubMissionName[0]) {
				MissionDef *pChildDef = missiondef_ChildDefFromNamePooled(pRootDef, pchNewSubMissionName);
				
				if (!pChildDef) {
					TRANSACTION_RETURN_LOG_FAILURE("No mission could be found matching: %s::%s", pMission->missionNameOrig, pchNewSubMissionName);
				}
				
				// Add the child mission
				if (!(mission_trh_AddChildMission(ATR_PASS_ARGS, ent, NULL, eaVarContainer, pGuild, pMission, pChildDef, pMapVariables, pDoorDestVarArray, pchInitMapVars, pchMapName, pReason, pExtract) == TRANSACTION_OUTCOME_SUCCESS)) {
					return TRANSACTION_OUTCOME_FAILURE;
				}
			}
		}
	}
	return TRANSACTION_OUTCOME_SUCCESS;
}

AUTO_TRANS_HELPER;
enumTransactionOutcome gameaction_trh_DropMissions(ATR_ARGS, ATH_ARG NOCONST(Entity)* ent, CONST_EARRAY_OF(WorldGameActionProperties)* actions, const ItemChangeReason *pReason, GameAccountDataExtract* pExtract)
{
	int i;
	for (i = 0; i < eaSize(actions); i++) {
		WorldGameActionProperties *action = (*actions)[i];
		if (action->eActionType == WorldGameActionType_DropMission) {
			const char* pchMissionName = action->pDropMissionProperties->pcMissionName;
			MissionDef* pDef = RefSystem_ReferentFromString(g_MissionDictionary, pchMissionName);
			NOCONST(Mission)* pMission = pDef ? eaIndexedGetUsingString(&ent->pPlayer->missionInfo->missions, pchMissionName) : NULL;
			if (NONNULL(pMission)) {
				if (missiondef_DropMissionShouldUpdateCooldown(pDef)) {
					mission_tr_UpdateCooldownAndDropMission(ATR_PASS_ARGS, ent, pchMissionName, pMission->startTime, pReason, /*NULL, */pExtract);
				} else {
					mission_tr_DropMission(ATR_PASS_ARGS, ent, pchMissionName, pReason, /*NULL, */pExtract);
				}
			}
		}
	}
	return TRANSACTION_OUTCOME_SUCCESS;
}

// This executes all GrantMission actions, treating the new missions as submissions of the given Mission
// pDef - pMission's MissionDef.  (This has to be passed in because of how MadLibs missions' defs work)
// This version does less locking
AUTO_TRANS_HELPER
ATR_LOCKS(ent, ".Pplayer.Missioninfo.Unextrequestid, .Pplayer.Missioninfo.Eamissionrequests[AO]")
ATR_LOCKS(pMission, ".Missionnameorig, .Starttime, .Eamissionvariables, .Children");
enumTransactionOutcome gameaction_trh_GrantSubMissionsNoLocking(ATR_ARGS, ATH_ARG NOCONST(Entity)* ent, ATH_ARG NOCONST(Mission)* pMission, MissionDef *pDef, MissionDef *pRootDef, CONST_EARRAY_OF(WorldGameActionProperties)* actions)
{
	int i;
	
	// Passing in a Mission without a MissionDef is a programmer error
	if (!(NONNULL(pMission) && NONNULL(pDef) && NONNULL(pRootDef))) {
		TRANSACTION_RETURN_FAILURE("Programmer error calling gameaction_trh_GrantSubMissions: NULL Mission or MissionDef");
	}

	for (i = 0; i < eaSize(actions); i++) {
		WorldGameActionProperties *action = (*actions)[i];
		if (action->eActionType == WorldGameActionType_GrantSubMission) {
			const char *pchNewSubMissionName = action->pGrantSubMissionProperties->pcSubMissionName;
			if (pchNewSubMissionName && pchNewSubMissionName[0]) {
				MissionDef *pChildDef = missiondef_ChildDefFromNamePooled(pRootDef, pchNewSubMissionName);
				
				if (!pChildDef) {
					TRANSACTION_RETURN_LOG_FAILURE("No mission could be found matching: %s::%s", pMission->missionNameOrig, pchNewSubMissionName);
				}
				
				// Add the child mission
				if (!(mission_trh_AddChildMissionNoLocking(ATR_PASS_ARGS, ent, pMission, pChildDef, pRootDef) == TRANSACTION_OUTCOME_SUCCESS)) {
					return TRANSACTION_OUTCOME_FAILURE;
				}
			}
		}
	}
	return TRANSACTION_OUTCOME_SUCCESS;
}


// This executes all GrantMission actions, treating them as new missions
AUTO_TRANS_HELPER
ATR_LOCKS(pMission, ".Missionnameorig, .Eamissionvariables, .Childfullmissions");
enumTransactionOutcome gameaction_trh_GrantMissions(ATR_ARGS, ATH_ARG NOCONST(Entity)* ent, ATH_ARG CONST_EARRAY_OF(NOCONST(ShardVariableContainer)) eaVarContainer, ATH_ARG NOCONST(Guild)* pGuild, ATH_ARG NOCONST(Mission)* pMission, MissionDef *pDef, CONST_EARRAY_OF(WorldGameActionProperties)* actions, WorldVariableArray* pMapVariables)
{
	int i;	
	for (i = 0; i < eaSize(actions); i++) {
		WorldGameActionProperties *action = (*actions)[i];
		if (action->eActionType == WorldGameActionType_GrantMission) {
			const char *pchNewMissionName = NULL;

			if (action->pGrantMissionProperties->eType == WorldMissionActionType_Named) {
				pchNewMissionName = REF_STRING_FROM_HANDLE(action->pGrantMissionProperties->hMissionDef);

			} else if (NONNULL(pMission) && action->pGrantMissionProperties->eType == WorldMissionActionType_MissionVariable) {
				WorldVariable *pMissionVar = eaIndexedGetUsingString(&pMission->eaMissionVariables, action->pGrantMissionProperties->pcVariableName);
				pchNewMissionName = (pMissionVar && pMissionVar->eType == WVAR_MISSION_DEF) ? pMissionVar->pcStringVal : NULL;

			} else if (pMapVariables && action->pGrantMissionProperties->eType == WorldMissionActionType_MapVariable)	{
				WorldVariableContainer *pMissionVar = eaIndexedGetUsingString(&pMapVariables->eaVariables, action->pGrantMissionProperties->pcVariableName);
				pchNewMissionName = (pMissionVar && pMissionVar->eType == WVAR_MISSION_DEF) ? pMissionVar->pcStringVal : NULL;
			}

			if (!pchNewMissionName) {
				TRANSACTION_RETURN_LOG_FAILURE("No mission could be found.");
			} else {
				const char *pchRootMissionName = NULL;
				const char *pchSubMissionName = NULL;
				if (NONNULL(pMission)) {
					if (pDef && GET_REF(pDef->parentDef)) {
						pchRootMissionName = REF_STRING_FROM_HANDLE(pDef->parentDef);
						pchSubMissionName = pMission->missionNameOrig;
					} else {
						pchRootMissionName = pMission->missionNameOrig;
					}

					// store this connection between the mission and the soon-to-be granted mission immediately
					// TODO (JDJ): might need to have some sort of fixup step in case the mission grant transaction
					// fails, but for now, it shouldn't cause any crashes.
					eaPush(&pMission->childFullMissions, StructAllocString(pchNewMissionName));
				}
				QueueRemoteCommand_gameaction_RunGrantMission(ATR_RESULT_SUCCESS, 0, 0, pchNewMissionName, pchRootMissionName, pchSubMissionName);
			}
		}
	}
	return TRANSACTION_OUTCOME_SUCCESS;
}


AUTO_COMMAND_REMOTE;
void gameaction_RunGrantMission(const char *pchMissionName, const char *pchParentRootMissionName, const char *pchParentSubMissionName, CmdContext *context)
{
	if (context && context->clientType == GLOBALTYPE_ENTITYPLAYER) {
		Entity *pEnt = entFromContainerIDAnyPartition(context->clientType, context->clientID);
		MissionDef *pDef = RefSystem_ReferentFromString(g_MissionDictionary, pchMissionName);
		if (pEnt && pDef) {
			MissionCreditType eCreditType = MissionCreditType_Primary;

			// If mission can be offered, set up params and add the mission
			// Otherwise, just don't grant it
			if (missiondef_CanBeOfferedAtAll(pEnt, pDef, NULL, NULL, &eCreditType)) {
				MissionOfferParams params = {0};
				params.eCreditType = eCreditType;
				params.pchParentMission = pchParentRootMissionName;
				params.pchParentSubMission = pchParentSubMissionName;
				missioninfo_AddMission(entGetPartitionIdx(pEnt), pEnt->pPlayer->missionInfo, pDef, &params, NULL, NULL);
			}
		}
	}
}


//----------------------------------------------------------------
// TakeItem Actions
//----------------------------------------------------------------

// This executes all TakeItem actions
AUTO_TRANS_HELPER
	ATR_LOCKS(ent, ".Psaved.pSCPdata.Isummonedscp, .Psaved.pSCPdata.Fcachedpetbonusxppct, .Psaved.pSCPdata.erSCP, .Pplayer.Pugckillcreditlimit, .Pinventoryv2.Peaowneduniqueitems, .Pinventoryv2.Ppinventorybags, .Hallegiance, .Hsuballegiance, .Pchar.Ilevelexp, .Pplayer.Pplayeraccountdata.Eagamepermissionmaxvaluenumerics, .Pinventoryv2.Pplitebags, .Psaved.Uiindexbuild, .Psaved.Ppbuilds, .Psaved.Conowner.Containerid, .Psaved.Conowner.Containertype");
enumTransactionOutcome gameaction_trh_TakeItems(ATR_ARGS, ATH_ARG NOCONST(Entity)* ent, CONST_EARRAY_OF(WorldGameActionProperties)* actions, const ItemChangeReason *pReason, GameAccountDataExtract *pExtract)
{
	int i;	
	for (i = 0; i < eaSize(actions); i++) {
		WorldGameActionProperties *action = (*actions)[i];
		if (action->eActionType == WorldGameActionType_TakeItem && action->pTakeItemProperties) {
			ItemDef *pDef = GET_REF(action->pTakeItemProperties->hItemDef);
			int iCount = action->pTakeItemProperties->iCount;
			if (pDef) {
				int iCountRemoved = 0;
				bool success;
				bool bTakeAllSilent = false;

				if (action->pTakeItemProperties->iCount > 0) {
					// Numerics are handled differently by the reward system.  Taking items means giving negative items
					if(pDef->eType == kItemType_Numeric) {
						success = inv_ent_trh_AddNumeric(ATR_PASS_ARGS, ent, false, pDef->pchName, -iCount, pReason);
					} else {
						iCountRemoved = inventory_RemoveItemByDefName(ATR_PASS_ARGS, ent, pDef->pchName, iCount, pReason, pExtract);
						if (iCountRemoved <= 0 && iCount > 0) {
							success = false;
						} else {
							success = true;
						}
					}
					// The above already sent an "Item Lost" message, so the Take All should be silent
					bTakeAllSilent = true;
				} else {
					success = true;
				}

				if (!success) {
					QueueRemoteCommand_notify_RemoteSendItemNotification(ATR_RESULT_FAIL, 0, 0, "MissionSystem.RequiresItemMessage", pDef->pchName, kNotifyType_ItemRequired);
					TRANSACTION_RETURN_LOG_FAILURE("Could not remove %d of Item %s", iCount, pDef->pchName);

				} else if (action->pTakeItemProperties->bTakeAll) {
					// If took the required number and take_all is chosen, then take all the rest
					// This works for numerics as well as for normal items
					inventory_RemoveAllItemByDefName(ATR_PASS_ARGS, ent, pDef->pchName, pReason, pExtract);
				}
			}
		}
	}
	
	return TRANSACTION_OUTCOME_SUCCESS;
}


//----------------------------------------------------------------
// GiveItem Actions
//----------------------------------------------------------------

// This executes all GiveItem actions
AUTO_TRANS_HELPER
ATR_LOCKS(ent, ".Psaved.pSCPdata.Isummonedscp, .Psaved.pSCPdata.Fcachedpetbonusxppct, .Psaved.pSCPdata.erSCP, .pInventoryV2.Pplitebags, .pInventoryV2.Peaowneduniqueitems, .Hallegiance, .Hsuballegiance, .Psaved.Ppallowedcritterpets, .Psaved.Uiindexbuild, .Psaved.Ppbuilds, .Itemidmax, .Pplayer.Pugckillcreditlimit, .Pplayer.Pplayeraccountdata.Eagamepermissionmaxvaluenumerics, .Pchar.Ilevelexp, .Psaved.Ppownedcontainers, .pInventoryV2.Ppinventorybags, .Psaved.Conowner.Containerid, .Psaved.Conowner.Containertype, .Pchar.Uipoweridmax, .Psaved.Ipetid, .Psaved.Ppuppetmaster.Curid, .Psaved.Ppuppetmaster.Curtype, .Psaved.Ppuppetmaster.Curtempid, .Pplayer.Playertype");
enumTransactionOutcome gameaction_trh_GiveItems(ATR_ARGS, ATH_ARG NOCONST(Entity)* ent, CONST_EARRAY_OF(WorldGameActionProperties)* actions, const char* pcMission, const ItemChangeReason *pReason, GameAccountDataExtract *pExtract)
{
	int i;	
	for (i = 0; i < eaSize(actions); i++) {
		WorldGameActionProperties *action = (*actions)[i];
		if (action->eActionType == WorldGameActionType_GiveItem && action->pGiveItemProperties) {
			const char *pchItemDefName = REF_STRING_FROM_HANDLE(action->pGiveItemProperties->hItemDef);
			if (pchItemDefName) {
				ItemDef* pItemDef = item_DefFromName(pchItemDefName);
				bool success;
				InvBagIDs destBag;

				// Numerics are stored as a single item with a variable value.
				if(pItemDef && pItemDef->eType == kItemType_Numeric) {
					destBag = InvBagIDs_Numeric;
					success = inv_ent_trh_AddNumeric(ATR_PASS_ARGS, ent, false, pchItemDefName, action->pGiveItemProperties->iCount, pReason);
				} else {
					Item *item;
					enumTransactionOutcome eResult;
					destBag = InvBagIDs_Inventory;

					if ( pItemDef != NULL ) {
						// check to see if there is a bag override for this item type
						destBag = itemAcquireOverride_FromGameAction(pItemDef);
						if ( destBag == InvBagIDs_None ) {
							destBag = InvBagIDs_Inventory;
						}
					}
					item = item_FromDefName(pchItemDefName);
					if (!item)
						TRANSACTION_RETURN_LOG_FAILURE("Failed to create an item from def %s!", pchItemDefName);

					CONTAINER_NOCONST(Item, item)->count = action->pGiveItemProperties->iCount;
					eResult = inv_AddItem(ATR_PASS_ARGS, ent, NULL, destBag, -1, item, pchItemDefName, ItemAdd_IgnoreUnique | ItemAdd_UseOverflow, pReason, pExtract);

					StructDestroySafe(parse_Item,&item);
					if(eResult == TRANSACTION_OUTCOME_SUCCESS)
					{
						success = true;
					}
					else
					{
						success = false;
					}
				}
				if (!success) {
					const char* pchBagFullMsgKey = inv_trh_GetBagFullMessageKey(ATR_PASS_ARGS, ent,destBag,pExtract);
					QueueRemoteCommand_notify_RemoteSendNotification(ATR_RESULT_FAIL, 0, 0, pchBagFullMsgKey, kNotifyType_InventoryFull);
					TRANSACTION_RETURN_LOG_FAILURE("Inventory full, could not add %d of Item %s", action->pGiveItemProperties->iCount, pchItemDefName);
				}
			}
		}
	}
	
	return TRANSACTION_OUTCOME_SUCCESS;
}


//----------------------------------------------------------------
// NPC send email
//----------------------------------------------------------------

// This executes all NPC send email 
AUTO_TRANS_HELPER
ATR_LOCKS(ent, " .Pplayer.pEmailV2.Ilastusedid, .Pplayer.Langid, .Hallegiance, .Hsuballegiance, .Pplayer.Pplayeraccountdata.Eagamepermissionmaxvaluenumerics, .Pplayer.pEmailV2.Mail, .Pchar.Ilevelexp");
enumTransactionOutcome gameaction_trh_NPCSendEmail(ATR_ARGS, ATH_ARG NOCONST(Entity)* ent, CONST_EARRAY_OF(WorldGameActionProperties)* actions, GameAccountDataExtract *pExtract)
{
	int i;	
	for (i = 0; i < eaSize(actions); i++) {
		WorldGameActionProperties *action = (*actions)[i];
		if (action->eActionType == WorldGameActionType_NPCSendMail && action->pNPCSendEmailProperties) {
			const char *pchItemDefName = REF_STRING_FROM_HANDLE(action->pNPCSendEmailProperties->hItemDef);
			Message* pFromName = GET_REF(action->pNPCSendEmailProperties->dFromName.hMessage);
			Message* pSubject = GET_REF(action->pNPCSendEmailProperties->dSubject.hMessage);
			Message* pBody = GET_REF(action->pNPCSendEmailProperties->dBody.hMessage);
			
			bool success = false;
			
			if (pFromName && pSubject && pBody) {
				int langID = entity_trh_GetLanguage(ent);
				
				const char * pcFromName = langTranslateMessage(langID, pFromName);	
				const char * pcSubject = langTranslateMessage(langID, pSubject);	
				const char * pcBody = langTranslateMessage(langID, pBody);	
				NOCONST(Item) *pItem = NULL;
				U32 uQuantity = 0;

				if (pchItemDefName) {
					pItem = CONTAINER_NOCONST(Item, item_FromEnt(ent, pchItemDefName,0,NULL,0));
					if(pItem) {
						uQuantity = 1;
					} else {
						TRANSACTION_RETURN_LOG_FAILURE("Failure to send NPC Email due to failed item creation %s.", pchItemDefName);
					}
				}

				if (pcFromName && pcSubject && pcBody) {
					MailCharacterItems *pCharacterItems = NULL;
					if (pItem) {
						pCharacterItems = CharacterMailAddItem(NULL, (Item *)pItem, uQuantity);
						if (!pCharacterItems) {
							StructDestroyNoConst(parse_Item, pItem);
							TRANSACTION_RETURN_LOG_FAILURE("Failure to send NPC Email due to CharacterMailAddItem() returning NULL for item %s.", pchItemDefName);
						}
					}
					
					success = EntityMail_trh_NPCAddMail(ATR_PASS_ARGS, ent,
						pcFromName,
						pcSubject,
						pcBody,
						pCharacterItems,
						action->pNPCSendEmailProperties->uFutureSendTime,
						kNPCEmailType_Default);
				}
			}
			
			if (!success) {
				QueueRemoteCommand_notify_RemoteSendNotification(ATR_RESULT_FAIL, 0, 0, "MissionSystem.NPCMailSendFailure", kNotifyType_NPCMailSendFailed);
				TRANSACTION_RETURN_LOG_FAILURE("Failure to send NPC Email.");
			}
		}
	}
	
	return TRANSACTION_OUTCOME_SUCCESS;
}


//----------------------------------------------------------------
// Game Account Data Attrib-Value Actions
//----------------------------------------------------------------

AUTO_TRANS_HELPER
ATR_LOCKS(ent, ".Pplayer.Pplayeraccountdata.Eapendingkeys");
enumTransactionOutcome gameaction_trh_GADAttribValue(ATR_ARGS, ATH_ARG NOCONST(Entity)* ent, CONST_EARRAY_OF(WorldGameActionProperties)* actions)
{
	int i;
	for (i = 0; i < eaSize(actions); i++) {
		WorldGameActionProperties *action = (*actions)[i];
		if (action->eActionType == WorldGameActionType_GADAttribValue && action->pGADAttribValueProperties) {
			NOCONST(AttribValuePair) *pPair = eaIndexedGetUsingString(&ent->pPlayer->pPlayerAccountData->eaPendingKeys, 
				action->pGADAttribValueProperties->pcAttribKey);
			if (!pPair) {
				pPair = StructCreateNoConst(parse_AttribValuePair);
				pPair->pchAttribute = StructAllocString(action->pGADAttribValueProperties->pcAttribKey);
				eaPush(&ent->pPlayer->pPlayerAccountData->eaPendingKeys, pPair);
			}
			if (action->pGADAttribValueProperties->eModifyType == WorldVariableActionType_IntIncrement) {
				int iValue = 0;
				char pchVal[16];
				pchVal[15] = '\0';

				if (pPair->pchValue) {
					iValue = atoi(pPair->pchValue);
					StructFreeString(pPair->pchValue);
				}

				//Make the change
				iValue += atoi(action->pGADAttribValueProperties->pcValue);
				if (iValue >= 0) {
					snprintf(pchVal, 15, "+%d", iValue);
				} else if(iValue < 0) {
					snprintf(pchVal, 15, "-%d", iValue);
				}

				pPair->pchValue = StructAllocString(pchVal);

				TRANSACTION_APPEND_LOG_SUCCESS("GADAttribValue: Incrementing key '%s' by '%s'",
					pPair->pchAttribute,
					pPair->pchValue);

			} else {
				if(pPair->pchValue) {
					StructFreeString(pPair->pchValue);
				}
				pPair->pchValue = StructAllocString(action->pGADAttribValueProperties->pcValue);

				TRANSACTION_APPEND_LOG_SUCCESS("GADAttribValue: Setting key '%s' to '%s'",
					pPair->pchAttribute,
					pPair->pchValue);
			}
		}
	}

	return TRANSACTION_OUTCOME_SUCCESS;
}


//----------------------------------------------------------------
// ChangeNemesisState Actions
//----------------------------------------------------------------

// This executes all ChangeNemesisState actions
AUTO_TRANS_HELPER
ATR_LOCKS(ent, ".Pplayer.Nemesisinfo.Eanemesisstates");
enumTransactionOutcome gameaction_trh_ChangeNemesisState(ATR_ARGS, ATH_ARG NOCONST(Entity)* ent, CONST_EARRAY_OF(WorldGameActionProperties)* actions)
{
	int i;
	enumTransactionOutcome retVal = TRANSACTION_OUTCOME_SUCCESS;

	for (i = 0; i < eaSize(actions); i++) {
		WorldGameActionProperties *action = (*actions)[i];
		if (action->eActionType == WorldGameActionType_ChangeNemesisState && action->pNemesisStateProperties) {
			bool bDefeated = (action->pNemesisStateProperties->eNewNemesisState != NemesisState_Primary && action->pNemesisStateProperties->eNewNemesisState != NemesisState_AtLarge);
			retVal = nemesis_tr_ChangePrimaryNemesisState(ATR_PASS_ARGS, ent, action->pNemesisStateProperties->eNewNemesisState, bDefeated);
		}
	}

	return retVal;
}


//----------------------------------------------------------------
// Shard Variables Actions
//----------------------------------------------------------------

// This executes all ShardVariables actions
AUTO_TRANS_HELPER
ATR_LOCKS(eaVarContainer, ".id, .Uclock, .Eaworldvars");

enumTransactionOutcome gameaction_trh_ShardVariables(ATR_ARGS, ATH_ARG CONST_EARRAY_OF(NOCONST(ShardVariableContainer)) eaVarContainer, CONST_EARRAY_OF(WorldGameActionProperties)* actions)
{
	int i;
	enumTransactionOutcome retVal = TRANSACTION_OUTCOME_SUCCESS;

	for (i = 0; i < eaSize(actions); i++) {
		WorldGameActionProperties *action = (*actions)[i];
		if (action->eActionType == WorldGameActionType_ShardVariable && action->pShardVariableProperties)
		{
			ShardVariable *pShardVariable = shardvariable_GetByName(action->pShardVariableProperties->pcVarName);
			if (pShardVariable!=NULL && NONNULL(eaVarContainer))
			{
				int j=0;
				int iContainerArrayIdx=-1;
				
				while (j<eaSize(&eaVarContainer) && iContainerArrayIdx<0)
				{
					if (eaVarContainer[j]->id == pShardVariable->iSubscribeTypeContainerID)
					{
						iContainerArrayIdx = j;
					}
				}
			
				if (iContainerArrayIdx < 0) {
					retVal = TRANSACTION_OUTCOME_FAILURE;
	
				} else if ((action->pShardVariableProperties->eModifyType == WorldVariableActionType_Set) && action->pShardVariableProperties->pVarValue) {
					WorldVariable *pVar = StructClone(parse_WorldVariable, action->pShardVariableProperties->pVarValue);
					pVar->pcName = action->pShardVariableProperties->pcVarName;
					retVal = shardvariable_tr_SetVariable(ATR_PASS_ARGS, eaVarContainer[iContainerArrayIdx], pVar);
					StructDestroy(parse_WorldVariable, pVar);
	
				} else if (action->pShardVariableProperties->eModifyType == WorldVariableActionType_IntIncrement) {
					const WorldVariable *pVar = shardvariable_GetDefaultValue(action->pShardVariableProperties->pcVarName);
					if (pVar) {
						retVal = shardvariable_tr_IncrementIntVariable(ATR_PASS_ARGS, eaVarContainer[iContainerArrayIdx], pVar, action->pShardVariableProperties->iIntIncrement);
					} else {
						retVal = TRANSACTION_OUTCOME_FAILURE;
					}
	
				} else if (action->pShardVariableProperties->eModifyType == WorldVariableActionType_FloatIncrement) {
					const WorldVariable *pVar = shardvariable_GetDefaultValue(action->pShardVariableProperties->pcVarName);
					if (pVar) {
						retVal = shardvariable_tr_IncrementFloatVariable(ATR_PASS_ARGS, eaVarContainer[iContainerArrayIdx], pVar, action->pShardVariableProperties->fFloatIncrement);
					} else {
						retVal = TRANSACTION_OUTCOME_FAILURE;
					}
	
				} else if (action->pShardVariableProperties->eModifyType == WorldVariableActionType_Reset) {
					retVal = shardvariable_tr_ClearVariable(ATR_PASS_ARGS, eaVarContainer[iContainerArrayIdx], action->pShardVariableProperties->pcVarName);
	
				} else {
					retVal = TRANSACTION_OUTCOME_FAILURE;
				}
			}
			else
			{
				retVal = TRANSACTION_OUTCOME_FAILURE;
			}
		}
	}

	return retVal;
}


//----------------------------------------------------------------
// Guild Stat Update Actions
//----------------------------------------------------------------

// This executes all GuildStatUpdate actions
AUTO_TRANS_HELPER
ATR_LOCKS(pGuild, ".Pguildstatsinfo");
enumTransactionOutcome gameaction_trh_RunGuildStatUpdates(ATR_ARGS, CONST_EARRAY_OF(WorldGameActionProperties)* actions, ATH_ARG NOCONST(Guild)* pGuild)
{
	S32 i;
	enumTransactionOutcome retVal = TRANSACTION_OUTCOME_SUCCESS;

	for (i = 0; i < eaSize(actions); i++) {
		WorldGameActionProperties *action = (*actions)[i];
		if (action->eActionType == WorldGameActionType_GuildStatUpdate && action->pGuildStatUpdateProperties) {
			retVal = gslGuild_tr_UpdateGuildStat(ATR_PASS_ARGS, pGuild, action->pGuildStatUpdateProperties->pchStatName, action->pGuildStatUpdateProperties->eOperation, action->pGuildStatUpdateProperties->iValue);
		}
	}

	return retVal;
}


//----------------------------------------------------------------
// Guild Theme Set Action
//----------------------------------------------------------------

// This executes all Guild Theme Set actions
AUTO_TRANS_HELPER
ATR_LOCKS(pGuild, ".Pguildstatsinfo, .Htheme");
enumTransactionOutcome gameaction_trh_RunGuildThemeSetOperations(ATR_ARGS, CONST_EARRAY_OF(WorldGameActionProperties)* actions, ATH_ARG NOCONST(Guild)* pGuild)
{
	S32 i;
	enumTransactionOutcome retVal = TRANSACTION_OUTCOME_SUCCESS;

	for (i = 0; i < eaSize(actions); i++) {
		WorldGameActionProperties *action = (*actions)[i];
		if (action->eActionType == WorldGameActionType_GuildThemeSet && action->pGuildThemeSetProperties) {
			retVal = gslGuild_tr_SetGuildTheme(ATR_PASS_ARGS, pGuild, action->pGuildThemeSetProperties->pchThemeName);
		}
	}

	return retVal;
}


//----------------------------------------------------------------
// Activity Log
//----------------------------------------------------------------

// This executes all ActivityLog actions
AUTO_TRANS_HELPER
ATR_LOCKS(ent, ".Psaved.Nextactivitylogid, .Psaved.Activitylogentries[AO]");
enumTransactionOutcome gameaction_trh_ActivityLog(ATR_ARGS, ATH_ARG NOCONST(Entity)* ent, CONST_EARRAY_OF(WorldGameActionProperties)* actions)
{
	int i;
	enumTransactionOutcome retVal = TRANSACTION_OUTCOME_SUCCESS;
	U32 time;

	time = timeSecondsSince2000();

	for (i = 0; i < eaSize(actions); i++) {
		WorldGameActionProperties *action = (*actions)[i];
		if (action->eActionType == WorldGameActionType_ActivityLog && action->pActivityLogProperties) {
			retVal = ActivityLog_tr_AddEntityLogEntry(ATR_PASS_ARGS, ent, action->pActivityLogProperties->eEntryType, REF_STRING_FROM_HANDLE(action->pActivityLogProperties->dArgString.hMessage), time, 0.0f);
		}
	}

	return retVal;
}


AUTO_TRANS_HELPER
ATR_LOCKS(pGuild, ".Inextactivitylogentryid, .Eaactivityentries, eaMembers[]");
enumTransactionOutcome gameaction_trh_GuildActivityLog(ATR_ARGS, ATH_ARG NOCONST(Guild)* pGuild, ContainerID subjectID, CONST_EARRAY_OF(WorldGameActionProperties)* actions)
{
	int i;
	enumTransactionOutcome retVal = TRANSACTION_OUTCOME_SUCCESS;
	U32 time;

	//
	// Don't fail the transaction if guild is NULL, since this can happen in cases where the player doesn't have a guild.
	// 
	if ( NONNULL(pGuild) && eaIndexedGetUsingInt(&pGuild->eaMembers, subjectID) ) {
		time = timeSecondsSince2000();

		for (i = 0; i < eaSize(actions); i++) {
			WorldGameActionProperties *action = (*actions)[i];
			if (action->eActionType == WorldGameActionType_ActivityLog && action->pActivityLogProperties) {
				ActivityLogEntryTypeConfig *typeConfig = ActivityLog_GetTypeConfig(action->pActivityLogProperties->eEntryType);
				if ( typeConfig->addToGuildLog ) {
					retVal = ActivityLog_tr_AddGuildLogEntry(ATR_PASS_ARGS, pGuild, action->pActivityLogProperties->eEntryType, REF_STRING_FROM_HANDLE(action->pActivityLogProperties->dArgString.hMessage), time, subjectID);
				}
			}
		}
	}

	return retVal;
}


//----------------------------------------------------------------
// Send Floater Actions
//----------------------------------------------------------------

static void gameaction_SendFloaterMsgToEnt(Entity *pEnt, Message *pMessage, int r, int g, int b)
{
	if (pEnt && pMessage) {
		int langID = entGetLanguage(pEnt);
		
		ClientCmd_NotifySend(pEnt, kNotifyType_LegacyFloaterMsg, langTranslateMessage(langID, pMessage), NULL, NULL);
	}
}


// Execute all SendFloaterMsg actions immediately
static void gameaction_SendFloatersNoTrans(Entity* ent, const WorldGameActionBlock* pActionBlock)
{
	int i;	
	for (i = 0; i < eaSize(&pActionBlock->eaActions); i++) {
		WorldGameActionProperties *action = (pActionBlock->eaActions)[i];
		if (action->eActionType == WorldGameActionType_SendFloaterMsg && action->pSendFloaterProperties) {
			Message* pMessage = GET_REF(action->pSendFloaterProperties->floaterMsg.hMessage);
			if (pMessage) {
				gameaction_SendFloaterMsgToEnt(ent, pMessage, action->pSendFloaterProperties->vColor[0]*255, action->pSendFloaterProperties->vColor[1]*255, action->pSendFloaterProperties->vColor[2]*255);
			}
		}
	}
}


// This executes all SendFloaterMsg actions
enumTransactionOutcome gameaction_trh_SendFloaters(ATR_ARGS, CONST_EARRAY_OF(WorldGameActionProperties)* actions)
{
	int i;	
	for (i = 0; i < eaSize(actions); i++) {
		WorldGameActionProperties *action = (*actions)[i];
		if (action->eActionType == WorldGameActionType_SendFloaterMsg && action->pSendFloaterProperties) {
			const char *pchMessageKey = REF_STRING_FROM_HANDLE(action->pSendFloaterProperties->floaterMsg.hMessage);
			if (pchMessageKey) {
				QueueRemoteCommand_gameaction_SendFloaterMsg(ATR_RESULT_SUCCESS, 0, 0, pchMessageKey, action->pSendFloaterProperties->vColor[0]*255, action->pSendFloaterProperties->vColor[1]*255, action->pSendFloaterProperties->vColor[2]*255);
			}
		}
	}
	return TRANSACTION_OUTCOME_SUCCESS;
}


static void gameaction_SendFloaterMsgToEntArray(Entity ***pEntArray, Message *pMessage, int r, int g, int b)
{
	if (pEntArray && pMessage) {
		const char *pchMessage = NULL;
		int i, n = eaSize(pEntArray);
		int langID = -1;
		for(i=0; i<n; i++) {
			Entity* pEnt = (*pEntArray)[i];
			if (pEnt && pEnt->pPlayer) {
				if (langID != entGetLanguage(pEnt)) {
					langID = entGetLanguage(pEnt);
					pchMessage = langTranslateMessage(langID, pMessage);
				}

				ClientCmd_NotifySend(pEnt, kNotifyType_LegacyFloaterMsg, pchMessage, NULL, NULL);
			}
		}
	}
}


AUTO_COMMAND_REMOTE;
void gameaction_SendFloaterMsg(const char *msgKey, int r, int g, int b, CmdContext *context)
{
	if (context && context->clientType == GLOBALTYPE_ENTITYPLAYER) {
		Entity *pEnt = entFromContainerIDAnyPartition(context->clientType, context->clientID);
		if (pEnt && msgKey) {
			gameaction_SendFloaterMsgToEnt(pEnt, RefSystem_ReferentFromString(gMessageDict, msgKey), r, g, b);
		}
	}
}


static void gameaction_np_SendFloater(int iPartitionIdx, Mission* pNonPersistedMission, WorldGameActionProperties *pAction)
{
	if (pAction && pAction->eActionType == WorldGameActionType_SendFloaterMsg && pAction->pSendFloaterProperties) {
		Entity **players = NULL;
		Message *pMessage = GET_REF(pAction->pSendFloaterProperties->floaterMsg.hMessage);
		int r = pAction->pSendFloaterProperties->vColor[0]*255;
		int g = pAction->pSendFloaterProperties->vColor[1]*255;
		int b = pAction->pSendFloaterProperties->vColor[2]*255;
		int langID = -1;

		if (pNonPersistedMission && pNonPersistedMission->infoOwner && pNonPersistedMission->infoOwner->parentEnt) {
			// Send floaters only to the Mission owner
			gameaction_SendFloaterMsgToEnt(pNonPersistedMission->infoOwner->parentEnt, pMessage, r, g, b);

		} else if (pNonPersistedMission && mission_GetType(pNonPersistedMission) == MissionType_OpenMission) {
			// If this is an Open Mission, send to all players who have that Mission active
			MissionDef *pRootDef = GET_REF(pNonPersistedMission->rootDefOrig);
			Entity* currEnt;
			EntityIterator* iter = entGetIteratorSingleType(iPartitionIdx, 0, ENTITYFLAG_IGNORE, GLOBALTYPE_ENTITYPLAYER);
			while ((currEnt = EntityIteratorGetNext(iter))) {
				MissionInfo *pMissionInfo = mission_GetInfoFromPlayer(currEnt);
				if (pMissionInfo && pMissionInfo->pchCurrentOpenMission && pMissionInfo->pchCurrentOpenMission == pRootDef->name) {
					eaPush(&players, currEnt);
				}
			}
			EntityIteratorRelease(iter);
			gameaction_SendFloaterMsgToEntArray(&players, pMessage, r, g, b);

		} else {
			// Unknown case... send to all players on the map
			Entity* currEnt;
			EntityIterator* iter = entGetIteratorSingleType(iPartitionIdx, 0, ENTITYFLAG_IGNORE, GLOBALTYPE_ENTITYPLAYER);
			while ((currEnt = EntityIteratorGetNext(iter))) {
				eaPush(&players, currEnt);
			}
			EntityIteratorRelease(iter);
			gameaction_SendFloaterMsgToEntArray(&players, pMessage, r, g, b);
		}

		eaDestroy(&players);
	}
}


//----------------------------------------------------------------
// Send Notification Actions
//----------------------------------------------------------------

static void gameaction_CopyHeadshotToContactCostume(const WorldGameActionHeadshotProperties *pHeadshotProps, ContactCostume *pCostume)
{
	if (pCostume && pHeadshotProps) {
		switch(pHeadshotProps->eType)
		{
			case WorldGameActionHeadshotType_Specified:
				pCostume->eCostumeType = ContactCostumeType_Specified;
			xcase WorldGameActionHeadshotType_PetContactList:
				pCostume->eCostumeType = ContactCostumeType_PetContactList;
			xcase WorldGameActionHeadshotType_CritterGroup:
				pCostume->eCostumeType = ContactCostumeType_CritterGroup;
			xdefault:
				pCostume->eCostumeType = ContactCostumeType_Default;
				break;
		}

		if (IS_HANDLE_ACTIVE(pHeadshotProps->hCostume)) {
			COPY_HANDLE(pCostume->costumeOverride, pHeadshotProps->hCostume);
		}
		if(IS_HANDLE_ACTIVE(pHeadshotProps->hPetContactList)) {
			COPY_HANDLE(pCostume->hPetOverride, pHeadshotProps->hPetContactList);
		}

		switch(pHeadshotProps->eCritterGroupType)
		{
			case WorldHeadshotMapVarOverrideType_Specified:
				pCostume->eCostumeCritterGroupType = ContactMapVarOverrideType_Specified;
			xcase WorldHeadshotMapVarOverrideType_MapVar:
				pCostume->eCostumeCritterGroupType = ContactMapVarOverrideType_MapVar;
			xdefault:
				pCostume->eCostumeCritterGroupType = ContactMapVarOverrideType_Specified;
				break;
		}

		if (IS_HANDLE_ACTIVE(pHeadshotProps->hCostumeCritterGroup)) {
			COPY_HANDLE(pCostume->hCostumeCritterGroup, pHeadshotProps->hCostumeCritterGroup);
		}
		if (pHeadshotProps->pchCritterGroupMapVar) {
			pCostume->pchCostumeMapVar = allocAddString(pHeadshotProps->pchCritterGroupMapVar);
		}
		if (pHeadshotProps->pchCritterGroupIdentifier) {
			pCostume->pchCostumeIdentifier = allocAddString(pHeadshotProps->pchCritterGroupIdentifier);
		}
	}
}


static void gameaction_SendNotificationToEnt(Entity *pEnt, Message *pMessage, const char *pchSound, NotifyType eNotifyType, const WorldGameActionHeadshotProperties *pHeadshotProps, const char *pchSplatFX, const char *pchLogicalString)
{
	if (pEnt) {
		int langID = entGetLanguage(pEnt);

		if (pHeadshotProps) {
			char* estrResult = NULL;
			ContactHeadshotData *pData = StructCreate(parse_ContactHeadshotData);
			ContactCostume *pCostume = StructCreate(parse_ContactCostume);

			gameaction_CopyHeadshotToContactCostume(pHeadshotProps, pCostume);

			contact_CostumeToHeadshotData(pEnt, pCostume, &pData);

			pData->pchHeadshotStyleDef = allocAddString(pHeadshotProps->pchHeadshotStyleDef);

			if (pMessage) {
				estrStackCreate(&estrResult);
				langFormatGameMessage(langID, &estrResult, pMessage, STRFMT_ENTITY(pEnt), STRFMT_END);
			}
			ClientCmd_NotifySendWithHeadshot(pEnt, eNotifyType, estrResult, pchLogicalString, pchSound, pData);

			StructDestroy(parse_ContactHeadshotData, pData);
			StructDestroy(parse_ContactCostume, pCostume);
			estrDestroy(&estrResult);

		} else if (pchSound && *pchSound) {
			const char* pchMessage = NULL;
			if (pMessage) {
				pchMessage = langTranslateMessage(langID, pMessage);
			}
			ClientCmd_NotifySendAudio(pEnt, eNotifyType, pchMessage, pchLogicalString, pchSound, NULL);

		} else if (pchSplatFX && *pchSplatFX) {
			const char* pchMessage = NULL;
			if (pMessage) {
				pchMessage = langTranslateMessage(langID, pMessage);
			}
			ClientCmd_NotifySendSplatFX(pEnt, eNotifyType, pchMessage, pchSplatFX);

		} else if (pMessage) {
			ClientCmd_NotifySend(pEnt, eNotifyType, langTranslateMessage(langID, pMessage), pchLogicalString, NULL);
		}
	}
}


// Execute all SendNotification actions immediately
static void gameaction_SendNotificationsNoTrans(Entity* ent, const WorldGameActionBlock* pActionBlock)
{
	int i;	
	for (i = 0; i < eaSize(&pActionBlock->eaActions); i++) {
		WorldGameActionProperties *action = (pActionBlock->eaActions)[i];
		if (action->eActionType == WorldGameActionType_SendNotification && action->pSendNotificationProperties) {
			Message* pMessage = GET_REF(action->pSendNotificationProperties->notifyMsg.hMessage);
			NotifyType eNotifyType = StaticDefineIntGetInt(NotifyTypeEnum, action->pSendNotificationProperties->pchNotifyType);
			gameaction_SendNotificationToEnt(ent, pMessage, action->pSendNotificationProperties->pchSound, eNotifyType, action->pSendNotificationProperties->pHeadshotProperties, action->pSendNotificationProperties->pchSplatFX, action->pSendNotificationProperties->pchLogicalString);
		}
	}
}


// This executes all SendNotification actions
enumTransactionOutcome gameaction_trh_SendNotifications(ATR_ARGS, CONST_EARRAY_OF(WorldGameActionProperties)* actions)
{
	int i;	
	for (i = 0; i < eaSize(actions); i++) {
		WorldGameActionProperties *action = (*actions)[i];
		if (action->eActionType == WorldGameActionType_SendNotification && action->pSendNotificationProperties) {
			const char *pchMessageKey = REF_STRING_FROM_HANDLE(action->pSendNotificationProperties->notifyMsg.hMessage);
			NotifyType eNotifyType = StaticDefineIntGetInt(NotifyTypeEnum, action->pSendNotificationProperties->pchNotifyType);
			WorldGameActionHeadshotProperties *pHeadshot = action->pSendNotificationProperties->pHeadshotProperties;
			QueueRemoteCommand_gameaction_SendNotification(ATR_RESULT_SUCCESS, 0, 0, pchMessageKey, action->pSendNotificationProperties->pchSound, eNotifyType, pHeadshot, action->pSendNotificationProperties->pchSplatFX, action->pSendNotificationProperties->pchLogicalString);
		}
	}
	return TRANSACTION_OUTCOME_SUCCESS;
}


static void gameaction_SendNotificationToEntArray(Entity ***pEntArray, Message *pMessage, const char *pchSound, NotifyType eNotifyType, const WorldGameActionHeadshotProperties *pHeadshot, const char *pchSplatFX, const char *pchLogicalString)
{
	if (pEntArray) {
		int i, n = eaSize(pEntArray);
		int langID = -1;
		for(i=0; i<n; i++) {
			Entity* pEnt = (*pEntArray)[i];
			if (pEnt && pEnt->pPlayer) {
				gameaction_SendNotificationToEnt(pEnt, pMessage, pchSound, eNotifyType, pHeadshot, pchSplatFX, pchLogicalString);
			}
		}
	}
}


AUTO_COMMAND_REMOTE ACMD_IFDEF(GAMESERVER);
void gameaction_SendNotification(const char *msgKey, const char *pchSound, NotifyType eNotifyType, CmdContext *context, WorldGameActionHeadshotProperties *pHeadshot, const char *pchFX, const char *pchLogicalString)
{
	if (context && context->clientType == GLOBALTYPE_ENTITYPLAYER) {
		Entity *pEnt = entFromContainerIDAnyPartition(context->clientType, context->clientID);
		if (pEnt) {
			Message *pMessage = msgKey && msgKey[0] ? RefSystem_ReferentFromString(gMessageDict, msgKey) : NULL;
			gameaction_SendNotificationToEnt(pEnt, pMessage, pchSound, eNotifyType, pHeadshot, pchFX, pchLogicalString);
		}
	}
}


static void gameaction_np_SendNotification(int iPartitionIdx, Mission* pNonPersistedMission, WorldGameActionProperties *pAction)
{
	if (pAction && pAction->eActionType == WorldGameActionType_SendNotification && pAction->pSendNotificationProperties) {
		Entity **players = NULL;
		Message *pMessage = GET_REF(pAction->pSendNotificationProperties->notifyMsg.hMessage);
		NotifyType eNotifyType = StaticDefineIntGetInt(NotifyTypeEnum, pAction->pSendNotificationProperties->pchNotifyType);
		WorldGameActionHeadshotProperties *pHeadshot = pAction->pSendNotificationProperties->pHeadshotProperties;
		const char *pchSound = pAction->pSendNotificationProperties->pchSound;
		const char *pchSplatFX = pAction->pSendNotificationProperties->pchSplatFX;
		const char *pchLogicalString = pAction->pSendNotificationProperties->pchLogicalString;
		int langID = -1;

		if (pNonPersistedMission && pNonPersistedMission->infoOwner && pNonPersistedMission->infoOwner->parentEnt) {
			// Send notifications only to the Mission owner
			gameaction_SendNotificationToEnt(pNonPersistedMission->infoOwner->parentEnt, pMessage, pchSound, eNotifyType, pHeadshot, pchSplatFX, pchLogicalString);

		} else if (pNonPersistedMission && mission_GetType(pNonPersistedMission) == MissionType_OpenMission) {
			// If this is an Open Mission, send to all players who have that Mission active
			MissionDef *pRootDef = GET_REF(pNonPersistedMission->rootDefOrig);
			Entity* currEnt;
			EntityIterator* iter = entGetIteratorSingleType(iPartitionIdx, 0, ENTITYFLAG_IGNORE, GLOBALTYPE_ENTITYPLAYER);
			while ((currEnt = EntityIteratorGetNext(iter))) {
				MissionInfo *pMissionInfo = mission_GetInfoFromPlayer(currEnt);
				if (pMissionInfo && pMissionInfo->pchCurrentOpenMission && pMissionInfo->pchCurrentOpenMission == pRootDef->name) {
					eaPush(&players, currEnt);
				}
			}
			EntityIteratorRelease(iter);
			gameaction_SendNotificationToEntArray(&players, pMessage, pchSound, eNotifyType, pHeadshot, pchSplatFX, pchLogicalString);

		} else {
			// Unknown case... send to all players on the map
			Entity* currEnt;
			EntityIterator* iter = entGetIteratorSingleType(iPartitionIdx, 0, ENTITYFLAG_IGNORE, GLOBALTYPE_ENTITYPLAYER);
			while ((currEnt = EntityIteratorGetNext(iter))) {
				eaPush(&players, currEnt);
			}
			EntityIteratorRelease(iter);
			gameaction_SendNotificationToEntArray(&players, pMessage, pchSound, eNotifyType, pHeadshot, pchSplatFX, pchLogicalString);
		}

		eaDestroy(&players);
	}
}

//----------------------------------------------------------------
// Update ItemAssignment Actions
//----------------------------------------------------------------

static void gameaction_UpdateItemAssignment(Entity* pEnt, const char* pchAssignmentName, S32 eOperation)
{
	ItemAssignmentDef* pDef = ItemAssignment_DefFromName(pchAssignmentName);
	if (pEnt && pDef)
	{
		gslItemAssignments_UpdateGrantedAssignments(pEnt, pDef, eOperation);
	}
}

static void gameaction_RunItemAssignmentUpdatesNoTrans(Entity* ent, const WorldGameActionBlock* pActionBlock)
{
	int i;
	for (i = 0; i < eaSize(&pActionBlock->eaActions); i++) {
		WorldGameActionProperties *action = (pActionBlock->eaActions)[i];
		if (action->eActionType == WorldGameActionType_UpdateItemAssignment && action->pItemAssignmentProperties) {
			S32 eOperation = action->pItemAssignmentProperties->eOperation;
			const char* pchAssignmentName = REF_STRING_FROM_HANDLE(action->pItemAssignmentProperties->hAssignmentDef);
			if (pchAssignmentName) {
				gameaction_UpdateItemAssignment(ent, pchAssignmentName, eOperation);
			}
		}
	}
}

AUTO_COMMAND_REMOTE ACMD_IFDEF(GAMESERVER);
void gameaction_rcmd_UpdateItemAssignment(const char *pchAssignmentName, S32 eOperation, CmdContext *context)
{
	if (context && context->clientType == GLOBALTYPE_ENTITYPLAYER) {
		Entity *pEnt = entFromContainerIDAnyPartition(context->clientType, context->clientID);
		if (pEnt) {
			gameaction_UpdateItemAssignment(pEnt, pchAssignmentName, eOperation);
		}
	}
}

enumTransactionOutcome gameaction_trh_UpdateItemAssignments(ATR_ARGS, CONST_EARRAY_OF(WorldGameActionProperties)* actions)
{
	int i;	
	for (i = 0; i < eaSize(actions); i++) {
		WorldGameActionProperties *action = (*actions)[i];
		if (action->eActionType == WorldGameActionType_UpdateItemAssignment && action->pItemAssignmentProperties) {
			const char* pchAssignmentName = REF_STRING_FROM_HANDLE(action->pItemAssignmentProperties->hAssignmentDef);
			if (pchAssignmentName) {
				S32 eOperation = action->pItemAssignmentProperties->eOperation;
				QueueRemoteCommand_gameaction_rcmd_UpdateItemAssignment(ATR_RESULT_SUCCESS, 0, 0, pchAssignmentName, eOperation);
			}
		}
	}
	return TRANSACTION_OUTCOME_SUCCESS;
}

static void gameaction_UpdateItemAssignmentEntArray(Entity ***pEntArray, const char *pchAssignmentName, S32 eOperation)
{
	if (pEntArray && pchAssignmentName) {
		int i, n = eaSize(pEntArray);
		for(i=0; i<n; i++) {
			Entity* pEnt = (*pEntArray)[i];
			if (pEnt && pEnt->pPlayer) {
				gameaction_UpdateItemAssignment(pEnt, pchAssignmentName, eOperation);
			}
		}
	}
}

static void gameaction_np_UpdateItemAssignment(int iPartitionIdx, Mission* pNonPersistedMission, WorldGameActionProperties *pAction)
{
	if (pAction && pAction->eActionType == WorldGameActionType_UpdateItemAssignment && pAction->pItemAssignmentProperties) {
		Entity **players = NULL;
		const char* pchAssignmentName = REF_STRING_FROM_HANDLE(pAction->pItemAssignmentProperties->hAssignmentDef);
		S32 eOperation = pAction->pItemAssignmentProperties->eOperation;

		if (pNonPersistedMission && pNonPersistedMission->infoOwner && pNonPersistedMission->infoOwner->parentEnt) {
			// Update ItemAssignment only for the Mission owner
			gameaction_UpdateItemAssignment(pNonPersistedMission->infoOwner->parentEnt, pchAssignmentName, eOperation);

		} else if (pNonPersistedMission && mission_GetType(pNonPersistedMission) == MissionType_OpenMission) {
			// If this is an Open Mission, send to all players who have that Mission active
			MissionDef *pRootDef = GET_REF(pNonPersistedMission->rootDefOrig);
			Entity* currEnt;
			EntityIterator* iter = entGetIteratorSingleType(iPartitionIdx, 0, ENTITYFLAG_IGNORE, GLOBALTYPE_ENTITYPLAYER);
			while ((currEnt = EntityIteratorGetNext(iter))) {
				MissionInfo *pMissionInfo = mission_GetInfoFromPlayer(currEnt);
				if (pMissionInfo && pMissionInfo->pchCurrentOpenMission && pMissionInfo->pchCurrentOpenMission == pRootDef->name) {
					eaPush(&players, currEnt);
				}
			}
			EntityIteratorRelease(iter);
			gameaction_UpdateItemAssignmentEntArray(&players, pchAssignmentName, eOperation);

		} else {
			// Unknown case... send to all players on the map
			Entity* currEnt;
			EntityIterator* iter = entGetIteratorSingleType(iPartitionIdx, 0, ENTITYFLAG_IGNORE, GLOBALTYPE_ENTITYPLAYER);
			while ((currEnt = EntityIteratorGetNext(iter))) {
				eaPush(&players, currEnt);
			}
			EntityIteratorRelease(iter);
			gameaction_UpdateItemAssignmentEntArray(&players, pchAssignmentName, eOperation);
		}

		eaDestroy(&players);
	}
}

//----------------------------------------------------------------
// Contact Actions
//----------------------------------------------------------------

static void gameaction_RunContactForEnt(Entity *pEnt, const char *pcContactName, const char *pcDialogName)
{
	if (pEnt && pcContactName) {
		ContactDef* pContactDef = contact_DefFromName(pcContactName);

		if (pContactDef) {
			contact_InteractBegin(pEnt, NULL, pContactDef, pcDialogName, NULL);
		}
	}
}


// Execute all Contact actions immediately
static void gameaction_RunContactsNoTrans(Entity* ent, const WorldGameActionBlock* pActionBlock)
{
	int i;	
	for (i = 0; i < eaSize(&pActionBlock->eaActions); i++) {
		WorldGameActionProperties *action = (pActionBlock->eaActions)[i];
		if (action->eActionType == WorldGameActionType_Contact && action->pContactProperties) {
			gameaction_RunContactForEnt(ent, REF_STRING_FROM_HANDLE(action->pContactProperties->hContactDef), action->pContactProperties->pcDialogName);
		}
	}
}


// This executes all Contact actions
enumTransactionOutcome gameaction_trh_RunContacts(ATR_ARGS, CONST_EARRAY_OF(WorldGameActionProperties)* actions)
{
	int i;	
	for (i = 0; i < eaSize(actions); i++) {
		WorldGameActionProperties *action = (*actions)[i];
		if (action->eActionType == WorldGameActionType_Contact && action->pContactProperties) {
			QueueRemoteCommand_gameaction_RunContact(ATR_RESULT_SUCCESS, 0, 0, REF_STRING_FROM_HANDLE(action->pContactProperties->hContactDef), action->pContactProperties->pcDialogName);
		}
	}
	return TRANSACTION_OUTCOME_SUCCESS;
}


static void gameaction_RunContactEntArray(Entity ***pEntArray, const char *pcContactName, const char *pcDialogName)
{
	if (pEntArray && pcContactName) {
		int i, n = eaSize(pEntArray);
		for(i=0; i<n; i++) {
			Entity* pEnt = (*pEntArray)[i];
			if (pEnt && pEnt->pPlayer) {
				gameaction_RunContactForEnt(pEnt, pcContactName, pcDialogName);
			}
		}
	}
}


AUTO_COMMAND_REMOTE;
void gameaction_RunContact(const char *pcContactName, const char *pcDialogName, CmdContext *context)
{
	if (context && context->clientType == GLOBALTYPE_ENTITYPLAYER) {
		Entity *pEnt = entFromContainerIDAnyPartition(context->clientType, context->clientID);
		if (pEnt && pcContactName) {
			gameaction_RunContactForEnt(pEnt, pcContactName, pcDialogName);
		}
	}
}


static void gameaction_np_RunContact(int iPartitionIdx, Mission* pNonPersistedMission, WorldGameActionProperties *pAction)
{
	if (pAction && pAction->eActionType == WorldGameActionType_Contact && pAction->pContactProperties) {
		Entity **players = NULL;

		if (pNonPersistedMission && pNonPersistedMission->infoOwner && pNonPersistedMission->infoOwner->parentEnt) {
			// Run contact only to the Mission owner
			gameaction_RunContactForEnt(pNonPersistedMission->infoOwner->parentEnt, REF_STRING_FROM_HANDLE(pAction->pContactProperties->hContactDef), pAction->pContactProperties->pcDialogName);

		} else if (pNonPersistedMission && mission_GetType(pNonPersistedMission) == MissionType_OpenMission) {
			// If this is an Open Mission, send to all players who have that Mission active
			MissionDef *pRootDef = GET_REF(pNonPersistedMission->rootDefOrig);
			Entity* currEnt;
			EntityIterator* iter = entGetIteratorSingleType(iPartitionIdx, 0, ENTITYFLAG_IGNORE, GLOBALTYPE_ENTITYPLAYER);
			while ((currEnt = EntityIteratorGetNext(iter))) {
				MissionInfo *pMissionInfo = mission_GetInfoFromPlayer(currEnt);
				if (pMissionInfo && pMissionInfo->pchCurrentOpenMission && pMissionInfo->pchCurrentOpenMission == pRootDef->name) {
					eaPush(&players, currEnt);
				}
			}
			EntityIteratorRelease(iter);
			gameaction_RunContactEntArray(&players, REF_STRING_FROM_HANDLE(pAction->pContactProperties->hContactDef), pAction->pContactProperties->pcDialogName);

		} else {
			// Unknown case... send to all players on the map
			Entity* currEnt;
			EntityIterator* iter = entGetIteratorSingleType(iPartitionIdx, 0, ENTITYFLAG_IGNORE, GLOBALTYPE_ENTITYPLAYER);
			while ((currEnt = EntityIteratorGetNext(iter))) {
				eaPush(&players, currEnt);
			}
			EntityIteratorRelease(iter);
			gameaction_RunContactEntArray(&players, REF_STRING_FROM_HANDLE(pAction->pContactProperties->hContactDef), pAction->pContactProperties->pcDialogName);
		}

		eaDestroy(&players);
	}
}


//----------------------------------------------------------------
// Mission Offer Actions
//----------------------------------------------------------------

static void gameaction_RunMissionOfferForEnt(Entity *pEnt, 
											 MissionDef* pDef,
											 DisplayMessage* pHeadshotNameMsg,
											 WorldGameActionHeadshotProperties *pHeadshotProps)
{
	char* estrHeadshotDisplayName = NULL;
	
	if (pEnt && pDef) {
		ContactHeadshotData *pHeadshotData = StructCreate(parse_ContactHeadshotData);
		ContactCostume *pCostume = StructCreate(parse_ContactCostume);

		gameaction_CopyHeadshotToContactCostume(pHeadshotProps, pCostume);

		if (pHeadshotNameMsg!=NULL && GET_REF(pHeadshotNameMsg->hMessage))
		{
			estrStackCreate(&estrHeadshotDisplayName);
			langFormatGameDisplayMessage(entGetLanguage(pEnt), &estrHeadshotDisplayName,pHeadshotNameMsg, STRFMT_ENTITY(pEnt), STRFMT_END);
		}

		contact_CostumeToHeadshotData(pEnt, pCostume, &pHeadshotData);
		if(pHeadshotProps)
			pHeadshotData->pchHeadshotStyleDef = allocAddString(pHeadshotProps->pchHeadshotStyleDef);

		contact_OfferMissionWithHeadshot(pEnt, pDef, estrHeadshotDisplayName, pHeadshotData);

		if (estrHeadshotDisplayName!=NULL)
		{
			estrDestroy(&estrHeadshotDisplayName);
		}

		StructDestroy(parse_ContactHeadshotData, pHeadshotData);
		StructDestroy(parse_ContactCostume, pCostume);
	}
}


// Execute all Mission Offer actions immediately
static void gameaction_RunMissionOffersNoTrans(Entity* ent, const WorldGameActionBlock* pActionBlock, WorldVariableArray *pMapVariables)
{
	int i;
	
	for (i = 0; i < eaSize(&pActionBlock->eaActions); i++) {
		WorldGameActionProperties *action = (pActionBlock->eaActions)[i];
		MissionDef *pMission = NULL;

		if (action->eActionType == WorldGameActionType_MissionOffer && action->pMissionOfferProperties) {
			if (action->pMissionOfferProperties->eType == WorldMissionActionType_Named) {
				pMission = GET_REF(action->pMissionOfferProperties->hMissionDef);
			} else if (pMapVariables && action->pMissionOfferProperties->eType == WorldMissionActionType_MissionVariable) {
				WorldVariable *pMissionVar = eaIndexedGetUsingString(&pMapVariables->eaVariables, action->pGrantMissionProperties->pcVariableName);
				const char *pchMissionName = (pMissionVar && pMissionVar->eType == WVAR_MISSION_DEF) ? pMissionVar->pcStringVal : NULL;
				pMission = RefSystem_ReferentFromString(g_MissionDictionary, pchMissionName);
			}
		}

		if (pMission)
		{
			gameaction_RunMissionOfferForEnt(ent, pMission, &(action->pMissionOfferProperties->headshotNameMsg),
											 action->pMissionOfferProperties->pHeadshotProps);
		}
	}
}


// This executes all Mission Offer actions
AUTO_TRANS_HELPER;
enumTransactionOutcome gameaction_trh_RunMissionOffers(ATR_ARGS, CONST_EARRAY_OF(WorldGameActionProperties)* actions, ATH_ARG NOCONST(Mission) *pMission, WorldVariableArray *pMapVariables)
{
	int i;	
	for (i = 0; i < eaSize(actions); i++) {
		WorldGameActionProperties *action = (*actions)[i];
		if (action->eActionType == WorldGameActionType_MissionOffer && action->pMissionOfferProperties) {
			const char *pchMissionName = NULL;

			if (action->pMissionOfferProperties->eType == WorldMissionActionType_Named) {
				pchMissionName = REF_STRING_FROM_HANDLE(action->pMissionOfferProperties->hMissionDef);
			} else if (action->pMissionOfferProperties->eType == WorldMissionActionType_MissionVariable) {
				if (pMapVariables) {
					WorldVariable *pMissionVar = eaIndexedGetUsingString(&pMapVariables->eaVariables, action->pMissionOfferProperties->pcVariableName);				
					pchMissionName = (pMissionVar && pMissionVar->eType == WVAR_MISSION_DEF) ? pMissionVar->pcStringVal : NULL;
				} else if (pMission) {
					WorldVariableContainer *pMissionVar = eaIndexedGetUsingString(&pMission->eaMissionVariables, action->pMissionOfferProperties->pcVariableName);
					pchMissionName = (pMissionVar && pMissionVar->eType == WVAR_MISSION_DEF) ? pMissionVar->pcStringVal : NULL;
				}
			}

			if (!pchMissionName) {
				TRANSACTION_RETURN_LOG_FAILURE("No mission could be found matching: %s", pchMissionName);
			} else {
				WorldGameActionHeadshotProperties *pHeadshotProps = action->pMissionOfferProperties->pHeadshotProps;
				DisplayMessage* pHeadshotNameMsg = &(action->pMissionOfferProperties->headshotNameMsg);
				QueueRemoteCommand_gameaction_RunMissionOffer(ATR_RESULT_SUCCESS, 0, 0, pchMissionName, pHeadshotNameMsg, pHeadshotProps);
			}
		}
	}
	return TRANSACTION_OUTCOME_SUCCESS;
}


AUTO_COMMAND_REMOTE ACMD_IFDEF(GAMESERVER);
void gameaction_RunMissionOffer(const char *pchMissionName,
								DisplayMessage* pHeadshotNameMsg,
								WorldGameActionHeadshotProperties *pHeadshotProps, 
								CmdContext *context)
{
	if (context && context->clientType == GLOBALTYPE_ENTITYPLAYER) {
		Entity *pEnt = entFromContainerIDAnyPartition(context->clientType, context->clientID);
		MissionDef *pDef = RefSystem_ReferentFromString(g_MissionDictionary, pchMissionName);
		if (pEnt && pDef) {
			gameaction_RunMissionOfferForEnt(pEnt, pDef, pHeadshotNameMsg, pHeadshotProps);
		}
	}
}


//----------------------------------------------------------------
// Warp Actions
//----------------------------------------------------------------

static void gameaction_RunWarpForEnt(Entity *pEnt, const char *pcMapName, const char *pcSpawnTarget, WorldVariable **eaVariables, DoorTransitionSequenceDef* pTransSequence, bool bIncludeTeammates)
{
	if (pEnt && (pcMapName || pcSpawnTarget)) {
		spawnpoint_MovePlayerToMapAndSpawn(pEnt, pcMapName, pcSpawnTarget,NULL, 0, 0, 0, 0, eaVariables, NULL, pTransSequence,0, bIncludeTeammates);
	}
}


// Execute all Warp actions immediately
static void gameaction_RunWarpsNoTrans(Entity* ent, const WorldGameActionBlock* pActionBlock, int seed)
{
	int i;	
	for (i = 0; i < eaSize(&pActionBlock->eaActions); i++) {
		WorldGameActionProperties *action = (pActionBlock->eaActions)[i];
		if (action->eActionType == WorldGameActionType_Warp && action->pWarpProperties) {
			int iPartitionIdx = entGetPartitionIdx(ent);
			WorldVariable* dest = worldVariableCalcVariableAndAlloc(iPartitionIdx, &action->pWarpProperties->warpDest, ent, seed, 0);
			WorldVariable** vars = worldVariableCalcVariablesAndAlloc(iPartitionIdx, action->pWarpProperties->eaVariableDefs, ent, seed, 0);

			if (dest && dest->eType == WVAR_MAP_POINT) {
				gameaction_RunWarpForEnt(ent, dest->pcZoneMap, dest->pcStringVal, vars, GET_REF(action->pWarpProperties->hTransSequence), action->pWarpProperties->bIncludeTeammates);
			}

			StructDestroy(parse_WorldVariable, dest);
			eaDestroyStruct(&vars, parse_WorldVariable);
		}
	}
}

static void gameaction_RunWarpEntArray(Entity ***pEntArray, const char *pcMapName, const char *pcSpawnTarget, WorldVariable **eaVariables, DoorTransitionSequenceDef* pTransSequence, bool bIncludeTeammates)
{
	if (pEntArray && pcMapName) {
		int i, n = eaSize(pEntArray);
		for(i=0; i<n; i++) {
			Entity* pEnt = (*pEntArray)[i];
			if (pEnt && pEnt->pPlayer) {
				gameaction_RunWarpForEnt(pEnt, pcMapName, pcSpawnTarget, eaVariables, pTransSequence, bIncludeTeammates);
			}
		}
	}
}


//Uhh... it looks like this function and the struct it takes as an argument are 
//totally deprecated and unused in our codebase. So I'm just going to have it pass 0 for bIncludeTeammates.
// - CMiller 4/27/2011
AUTO_COMMAND_REMOTE;
void gameaction_RunWarp(GameActionWarpData *pData, CmdContext *context)
{
	if (context && context->clientType == GLOBALTYPE_ENTITYPLAYER) {
		Entity *pEnt = entFromContainerIDAnyPartition(context->clientType, context->clientID);
		if (pEnt && (pData->pcMapName || pData->pcSpawnTarget)) {
			gameaction_RunWarpForEnt(pEnt, pData->pcMapName, pData->pcSpawnTarget, NULL /*pData->eaVariables*/, NULL, 0);
		}
	}
}


static void gameaction_np_RunWarp(int iPartitionIdx, Mission* pNonPersistedMission, WorldGameActionProperties *pAction, int seed)
{
	if (pAction && pAction->eActionType == WorldGameActionType_Warp && pAction->pWarpProperties) {
		Entity **players = NULL;

		if (pNonPersistedMission && pNonPersistedMission->infoOwner && pNonPersistedMission->infoOwner->parentEnt) {
			WorldVariable* dest = worldVariableCalcVariableAndAlloc(iPartitionIdx, &pAction->pWarpProperties->warpDest, NULL, seed, 0);
			WorldVariable** variables = worldVariableCalcVariablesAndAlloc(iPartitionIdx, pAction->pWarpProperties->eaVariableDefs, NULL, seed, 0);

			if (dest && dest->eType == WVAR_MAP_POINT ) {
				// Run warp only to the Mission owner
				gameaction_RunWarpForEnt(pNonPersistedMission->infoOwner->parentEnt, dest->pcZoneMap, dest->pcStringVal, variables, GET_REF(pAction->pWarpProperties->hTransSequence), pAction->pWarpProperties->bIncludeTeammates);
			}

			StructDestroy(parse_WorldVariable, dest);
			eaDestroyStruct( &variables, parse_WorldVariable );

		} else if (pNonPersistedMission && mission_GetType(pNonPersistedMission) == MissionType_OpenMission) {
			// If this is an Open Mission, send to all players who have that Mission active
			MissionDef *pRootDef = GET_REF(pNonPersistedMission->rootDefOrig);
			Entity* currEnt;
			EntityIterator* iter = entGetIteratorSingleType(iPartitionIdx, 0, ENTITYFLAG_IGNORE, GLOBALTYPE_ENTITYPLAYER);
			// TODO (JDJ): should we calculate this per entity?  or is there someplace we can validate to ensure no warp action
			// ends up warping a group of players using variables based on persisted missions?
			WorldVariable* dest = worldVariableCalcVariableAndAlloc(iPartitionIdx, &pAction->pWarpProperties->warpDest, NULL, seed, 0);
			WorldVariable** variables = worldVariableCalcVariablesAndAlloc(iPartitionIdx, pAction->pWarpProperties->eaVariableDefs, NULL, seed, 0);

			while ((currEnt = EntityIteratorGetNext(iter))) {
				MissionInfo *pMissionInfo = mission_GetInfoFromPlayer(currEnt);
				if (pMissionInfo && pMissionInfo->pchCurrentOpenMission && pMissionInfo->pchCurrentOpenMission == pRootDef->name) {
					eaPush(&players, currEnt);
				}
			}
			EntityIteratorRelease(iter);
			
			if (dest && dest->eType == WVAR_MAP_POINT) {
				gameaction_RunWarpEntArray(&players, dest->pcZoneMap, dest->pcStringVal, variables, GET_REF(pAction->pWarpProperties->hTransSequence), pAction->pWarpProperties->bIncludeTeammates);
			}
			
			StructDestroy(parse_WorldVariable, dest);
			eaDestroyStruct(&variables, parse_WorldVariable);

		} else {
			// Unknown case... send to all players on the map
			Entity* currEnt;
			EntityIterator* iter = entGetIteratorSingleType(iPartitionIdx, 0, ENTITYFLAG_IGNORE, GLOBALTYPE_ENTITYPLAYER);
			// TODO (JDJ): should we calculate this per entity?  or is there someplace we can validate to ensure no warp action
			// ends up warping a group of players using variables based on persisted missions?
			WorldVariable* dest = worldVariableCalcVariableAndAlloc(iPartitionIdx, &pAction->pWarpProperties->warpDest, NULL, seed, 0);
			WorldVariable** variables = worldVariableCalcVariablesAndAlloc(iPartitionIdx, pAction->pWarpProperties->eaVariableDefs, NULL, seed, 0);
			
			while ((currEnt = EntityIteratorGetNext(iter))) {
				eaPush(&players, currEnt);
			}
			EntityIteratorRelease(iter);

			if (dest && dest->eType == WVAR_MAP_POINT) {
				gameaction_RunWarpEntArray(&players, dest->pcZoneMap, dest->pcStringVal, variables, GET_REF(pAction->pWarpProperties->hTransSequence), pAction->pWarpProperties->bIncludeTeammates);
			}

			StructDestroy(parse_WorldVariable, dest);
			eaDestroyStruct( &variables, parse_WorldVariable );
		}

		eaDestroy(&players);
	}
}

//----------------------------------------------------------------
// Expression Actions
//----------------------------------------------------------------

static void gameaction_RunExpressionForEnt(Entity *pEnt, Expression *pExpr)
{
	if (pEnt && pExpr) {
		MultiVal mvResult;

		// Set context data
		exprContextSetSelfPtr(s_pGameActionContext, pEnt);
		exprContextSetPartition(s_pGameActionContext, entGetPartitionIdx(pEnt));
		exprContextSetPointerVarPooled(s_pGameActionContext, g_PlayerVarName, pEnt, NULL, false, true);
		exprContextSetScope(s_pGameActionContext, NULL);

		// Evaluate the expression
		exprEvaluate(pExpr, s_pGameActionContext, &mvResult);
	}
}


static void gameaction_RunExpressionFromText(Entity *pEnt, const char *exprText)
{
	if (pEnt && exprText) {
		Expression *pExpr = exprCreateFromString(exprText, NULL);
		exprGenerate(pExpr, s_pGameActionContext);
		gameaction_RunExpressionForEnt(pEnt, pExpr);
		exprDestroy(pExpr);
	}
}


// Execute all Expression actions immediately
static void gameaction_RunExpressionsNoTrans(Entity* ent, const WorldGameActionBlock* pActionBlock)
{
	int i;	
	for (i = 0; i < eaSize(&pActionBlock->eaActions); i++) {
		WorldGameActionProperties *action = (pActionBlock->eaActions)[i];
		if (action->eActionType == WorldGameActionType_Expression && action->pExpressionProperties) {
			gameaction_RunExpressionForEnt(ent, action->pExpressionProperties->pExpression);
		}
	}
}


// This executes all Expression actions
enumTransactionOutcome gameaction_trh_RunExpressions(ATR_ARGS, CONST_EARRAY_OF(WorldGameActionProperties)* actions)
{
	int i;	
	for (i = 0; i < eaSize(actions); i++) {
		WorldGameActionProperties *action = (*actions)[i];
		if (action->eActionType == WorldGameActionType_Expression && action->pExpressionProperties) {
			const char *exprText = exprGetCompleteString(action->pExpressionProperties->pExpression);
			if (exprText) {
				QueueRemoteCommand_gameaction_RunExpression(ATR_RESULT_SUCCESS, 0, 0, exprText);
			}
		}
	}
	return TRANSACTION_OUTCOME_SUCCESS;
}


static void gameaction_RunExpressionEntArray(Entity ***pEntArray, Expression *pExpr)
{
	if (pEntArray && pExpr) {
		int i, n = eaSize(pEntArray);
		for(i=0; i<n; i++) {
			Entity* pEnt = (*pEntArray)[i];
			if (pEnt && pEnt->pPlayer) {
				gameaction_RunExpressionForEnt(pEnt, pExpr);
			}
		}
	}
}


AUTO_COMMAND_REMOTE;
void gameaction_RunExpression(const char *exprText, CmdContext *context)
{
	if (context && context->clientType == GLOBALTYPE_ENTITYPLAYER) {
		Entity *pEnt = entFromContainerIDAnyPartition(context->clientType, context->clientID);
		if (pEnt && exprText) {
			gameaction_RunExpressionFromText(pEnt, exprText);
		}
	}
}


static void gameaction_np_RunExpression(int iPartitionIdx, Mission* pNonPersistedMission, WorldGameActionProperties *pAction)
{
	if (pAction && pAction->eActionType == WorldGameActionType_Expression && pAction->pExpressionProperties) {
		Entity **players = NULL;

		if (pNonPersistedMission && pNonPersistedMission->infoOwner && pNonPersistedMission->infoOwner->parentEnt) {
			// Run expressions only to the Mission owner
			gameaction_RunExpressionForEnt(pNonPersistedMission->infoOwner->parentEnt, pAction->pExpressionProperties->pExpression);

		} else if (pNonPersistedMission && mission_GetType(pNonPersistedMission) == MissionType_OpenMission) {
			// If this is an Open Mission, send to all players who have that Mission active
			MissionDef *pRootDef = GET_REF(pNonPersistedMission->rootDefOrig);
			Entity* currEnt;
			EntityIterator* iter = entGetIteratorSingleType(iPartitionIdx, 0, ENTITYFLAG_IGNORE, GLOBALTYPE_ENTITYPLAYER);
			while ((currEnt = EntityIteratorGetNext(iter))) {
				MissionInfo *pMissionInfo = mission_GetInfoFromPlayer(currEnt);
				if (pMissionInfo && pMissionInfo->pchCurrentOpenMission && pMissionInfo->pchCurrentOpenMission == pRootDef->name) {
					eaPush(&players, currEnt);
				}
			}
			EntityIteratorRelease(iter);
			gameaction_RunExpressionEntArray(&players, pAction->pExpressionProperties->pExpression);

		} else {
			// Unknown case... send to all players on the map
			Entity* currEnt;
			EntityIterator* iter = entGetIteratorSingleType(iPartitionIdx, 0, ENTITYFLAG_IGNORE, GLOBALTYPE_ENTITYPLAYER);
			while ((currEnt = EntityIteratorGetNext(iter))) {
				eaPush(&players, currEnt);
			}
			EntityIteratorRelease(iter);
			gameaction_RunExpressionEntArray(&players, pAction->pExpressionProperties->pExpression);
		}

		eaDestroy(&players);
	}
}


//----------------------------------------------------------------
// Functions to execute a list of Actions for non-persisted Missions
//----------------------------------------------------------------

// Runs actions without an entity transaction; throws errors for actions that require entity transactions.
// May run a shard variable transaction if required
void gameaction_np_RunActions(int iPartitionIdx, Mission* pNonPersistedMission, CONST_EARRAY_OF(WorldGameActionProperties)* actions)
{
	int seed = randomInt();
	MissionDef *pDef = mission_GetDef(pNonPersistedMission);
	MissionDef *pRootDef = pDef;
	int i, n;

	while (pRootDef && GET_REF(pRootDef->parentDef)) {
		pRootDef = GET_REF(pRootDef->parentDef);
	}

	if (pNonPersistedMission && mission_IsPersisted(pNonPersistedMission)) {
		Errorf("Error: Can only call gameaction_np_* on non-persisted Missions!  %s", pDef?pDef->name:NULL);
		return;
	}

	if (gameaction_MustLockShardVariables(actions, pDef)) {
		WorldGameActionBlock block;
		block.eaActions = (WorldGameActionProperties**)*actions;
		AutoTrans_gameaction_tr_RunActionsLockVarsOnly(NULL, GetAppGlobalType(),
					GLOBALTYPE_SHARDVARIABLE, shardVariable_GetContainerIDList(), 
					&block);
	} else {
		// Run any Actions that can be run without an entity transaction (currently SendFloater, Contact, Warp, and Expression)
		n = eaSize(actions);
		for (i = 0; i < n; i++) {
			WorldGameActionProperties *pAction = (*actions)[i];
			if (pAction->eActionType == WorldGameActionType_SendFloaterMsg && pAction->pSendFloaterProperties){
				gameaction_np_SendFloater(iPartitionIdx, pNonPersistedMission, pAction);

			} else if (pAction->eActionType == WorldGameActionType_Contact && pAction->pContactProperties){
				gameaction_np_RunContact(iPartitionIdx, pNonPersistedMission, pAction);

			} else if (pAction->eActionType == WorldGameActionType_Warp && pAction->pWarpProperties){
				gameaction_np_RunWarp(iPartitionIdx, pNonPersistedMission, pAction, seed);

			} else if (pAction->eActionType == WorldGameActionType_Expression && pAction->pExpressionProperties){
				gameaction_np_RunExpression(iPartitionIdx, pNonPersistedMission, pAction);

			} else if (pAction->eActionType == WorldGameActionType_UpdateItemAssignment && pAction->pItemAssignmentProperties){
				gameaction_np_UpdateItemAssignment(iPartitionIdx, pNonPersistedMission, pAction);

			} else if (pAction->eActionType == WorldGameActionType_SendNotification && pAction->pSendNotificationProperties) {
				gameaction_np_SendNotification(iPartitionIdx, pNonPersistedMission,pAction);

			} else {
				Errorf("Error: Invalid action on non-persisted Mission %s", pDef?pDef->name:NULL);
			}
		}
	}
}


// This treats all GrantSubMission actions as submissions of the given Mission
// Runs actions without an entity transaction; throws errors for actions that require entity transactions.
// May run a shard variable transaction.
void gameaction_np_RunActionsSubMissions(int iPartitionIdx, Mission* pNonPersistedMission, CONST_EARRAY_OF(WorldGameActionProperties)* actions)
{
	int seed = randomInt();
	MissionDef *pDef = mission_GetDef(pNonPersistedMission);
	MissionDef *pRootDef = pDef;
	WorldGameActionProperties **eaShardVarActions = NULL;
	int i, n;

	while (pRootDef && GET_REF(pRootDef->parentDef)) {
		pRootDef = GET_REF(pRootDef->parentDef);
	}

	if (!pNonPersistedMission) {
		Errorf("Error: NULL Mission passed to gameaction_np_RunActionsSubMissions!");
		return;
	}

	if (pNonPersistedMission && mission_IsPersisted(pNonPersistedMission)) {
		Errorf("Error: Can only call gameaction_np_* on non-persisted Missions!  %s", pDef?pDef->name:NULL);
		return;
	}

	// Run Actions (only granting sub-mission, other Actions will be performed when Mission is made persisted)
	// Send all floaters first
	n = eaSize(actions);
	for (i = 0; i < n; i++) {
		WorldGameActionProperties *pAction = (*actions)[i];
		if (pAction->eActionType == WorldGameActionType_SendFloaterMsg && pAction->pSendFloaterProperties) {
			gameaction_np_SendFloater(iPartitionIdx, pNonPersistedMission, pAction);
		}
	}

	// Run expressions next
	for (i = 0; i < n; i++) {
		WorldGameActionProperties *pAction = (*actions)[i];
		if (pAction->eActionType == WorldGameActionType_Expression && pAction->pExpressionProperties) {
			gameaction_np_RunExpression(iPartitionIdx, pNonPersistedMission, pAction);
		}
	}

	// Run contacts next
	for (i = 0; i < n; i++) {
		WorldGameActionProperties *pAction = (*actions)[i];
		if (pAction->eActionType == WorldGameActionType_Contact && pAction->pContactProperties) {
			gameaction_np_RunContact(iPartitionIdx, pNonPersistedMission, pAction);
		}
	}

	// Run warp next
	for (i = 0; i < n; i++) {
		WorldGameActionProperties *pAction = (*actions)[i];
		if (pAction->eActionType == WorldGameActionType_Warp && pAction->pWarpProperties) {
			gameaction_np_RunWarp(iPartitionIdx, pNonPersistedMission, pAction, seed);
		}
	}

	// Run notifications next
	for (i = 0; i < n; i++) {
		WorldGameActionProperties *pAction = (*actions)[i];
		if (pAction->eActionType == WorldGameActionType_SendNotification && pAction->pSendNotificationProperties) {
			gameaction_np_SendNotification(iPartitionIdx, pNonPersistedMission,pAction);
		}
	}

	// Run item assignments next
	for (i = 0; i < n; i++) {
		WorldGameActionProperties *pAction = (*actions)[i];
		if (pAction->eActionType == WorldGameActionType_UpdateItemAssignment && pAction->pItemAssignmentProperties) {
			gameaction_np_UpdateItemAssignment(iPartitionIdx, pNonPersistedMission, pAction);
		}
	}

	// Grant submissions
	for (i = 0; i < n; i++) {
		WorldGameActionProperties *pAction = (*actions)[i];
		if (pAction->eActionType == WorldGameActionType_GrantSubMission && pAction->pGrantSubMissionProperties) {
			const char *pchSubMissionName = pAction->pGrantSubMissionProperties->pcSubMissionName;
			MissionDef *pChildDef = missiondef_ChildDefFromNamePooled(pRootDef, pchSubMissionName);
			if (pChildDef) {
				mission_AddNonPersistedSubMission(iPartitionIdx, pNonPersistedMission, pChildDef, false);
			}
		} else if (pAction->eActionType == WorldGameActionType_SendFloaterMsg && pAction->pSendFloaterProperties) {
			// already executed, see above
		} else if (pAction->eActionType == WorldGameActionType_Expression && pAction->pExpressionProperties) {
			// already executed, see above
		} else if (pAction->eActionType == WorldGameActionType_Contact && pAction->pContactProperties) {
			// already executed, see above
		} else if (pAction->eActionType == WorldGameActionType_Warp && pAction->pWarpProperties) {
			// already executed, see above
		} else if (pAction->eActionType == WorldGameActionType_SendNotification && pAction->pSendNotificationProperties) {
			// already executed, see above
		} else if (pAction->eActionType == WorldGameActionType_UpdateItemAssignment && pAction->pItemAssignmentProperties) {
			// already executed, see above
		} else if (pAction->eActionType == WorldGameActionType_ShardVariable && pAction->pShardVariableProperties) {
			// Executed below as a special case
			eaPush(&eaShardVarActions, pAction);
		} else {
			// Invalid action type
			Errorf("Error: Invalid action on non-persisted Mission %s", pDef?pDef->name:NULL);
		}
	}

	// Special case of shard variable actions
	// They get run in a transaction as required
	if (eaSize(&eaShardVarActions) > 0) {
		TransactionReturnVal* returnVal = NULL;
		WorldGameActionBlock block;
		block.eaActions = eaShardVarActions;
		returnVal = LoggedTransactions_CreateManagedReturnVal("GameAction", NULL, NULL);
		AutoTrans_gameaction_tr_RunActionsLockVarsOnly(returnVal, GetAppGlobalType(),
					GLOBALTYPE_SHARDVARIABLE, shardVariable_GetContainerIDList(), 
					&block);
		eaDestroy(&eaShardVarActions);
	}
}


// This treats all GrantSubMission actions as submissions of the given Mission
// Only executes GrantSubMission actions, does not throw errors for other actions
void gameaction_np_RunActionsSubMissionsOnly(int iPartitionIdx, Mission* pNonPersistedMission, CONST_EARRAY_OF(WorldGameActionProperties)* actions)
{
	MissionDef *pDef = mission_GetDef(pNonPersistedMission);
	MissionDef *pRootDef = pDef;
	int i, n;

	while (pRootDef && GET_REF(pRootDef->parentDef)) {
		pRootDef = GET_REF(pRootDef->parentDef);
	}

	if (!pNonPersistedMission) {
		Errorf("Error: NULL Mission passed to gameaction_np_RunActionsSubMissionsOnly!");
		return;
	}

	if (pNonPersistedMission && mission_IsPersisted(pNonPersistedMission)) {
		Errorf("Error: Can only call gameaction_np_RunActionsSubMissionsOnly on non-persisted Missions!  %s", pDef?pDef->name:NULL);
		return;
	}

	PERFINFO_AUTO_START_FUNC();

	// Run Actions (only granting sub-mission, other Actions will be performed when Mission is made persisted)
	n = eaSize(actions);
	for (i = 0; i < n; i++) {
		WorldGameActionProperties *pAction = (*actions)[i];
		if (pAction->eActionType == WorldGameActionType_GrantSubMission && pAction->pGrantSubMissionProperties) {
			const char *pchSubMissionName = pAction->pGrantSubMissionProperties->pcSubMissionName;
			MissionDef *pChildDef = missiondef_ChildDefFromNamePooled(pRootDef, pchSubMissionName);
			if (pChildDef) {
				mission_AddNonPersistedSubMission(iPartitionIdx, pNonPersistedMission, pChildDef, true);
			}
		}
	}

	PERFINFO_AUTO_STOP();
}


//----------------------------------------------------------------
// GiveDoorKeyItem Actions
//----------------------------------------------------------------

// This executes all GiveDoorKeyItem actions
AUTO_TRANS_HELPER
ATR_LOCKS(ent, ".Pplayer.Skilltype, .Pplayer.Eskillspecialization, .Pchar.Hclass, .Pchar.Ppsecondarypaths, .Pchar.Hpath, .Psaved.pSCPdata.Isummonedscp, .Psaved.pSCPdata.Fcachedpetbonusxppct, .Psaved.pSCPdata.erSCP, .pInventoryV2.Pplitebags, .pInventoryV2.Peaowneduniqueitems, .pInventoryV2.Ppinventorybags, .Psaved.Ppallowedcritterpets, .Hallegiance, .Hsuballegiance, .Pchar.Ilevelexp, .Pplayer.Pplayeraccountdata.Eagamepermissionmaxvaluenumerics, .Psaved.Uiindexbuild, .Psaved.Ppbuilds, .Itemidmax, .Pplayer.Pugckillcreditlimit, .Psaved.Ppownedcontainers, .Psaved.Conowner.Containerid, .Psaved.Conowner.Containertype, .Pchar.Uipoweridmax, .Psaved.Ipetid, .Psaved.Ppuppetmaster.Curid, .Psaved.Ppuppetmaster.Curtype, .Psaved.Ppuppetmaster.Curtempid, .Pplayer.Playertype");
enumTransactionOutcome gameaction_trh_GiveDoorKeyItems(ATR_ARGS, ATH_ARG NOCONST(Entity)* ent, CONST_EARRAY_OF(WorldGameActionProperties)* actions, const char* pcMission, WorldVariableArray* pMapVariableArray, GameActionDoorDestinationVarArray* pDoorDestVarArray, const char* pchInitMapVars, const char* pchMapName, const ItemChangeReason *pReason, GameAccountDataExtract *pExtract)
{
	int i, j;	
	for (i = 0; i < eaSize(actions); i++) {
		WorldGameActionProperties *action = (*actions)[i];
		if (action->eActionType == WorldGameActionType_GiveDoorKeyItem && action->pGiveDoorKeyItemProperties) {
			const char *pchItemDefName = REF_STRING_FROM_HANDLE(action->pGiveDoorKeyItemProperties->hItemDef);
			if (pchItemDefName) {
				ItemDef* pItemDef = item_DefFromName(pchItemDefName);

				// Numerics are stored as a single item with a variable value.
				if (pItemDef && pItemDef->flags && !(pItemDef->flags & kItemDefFlag_DoorKey)) {
					TRANSACTION_RETURN_LOG_FAILURE("Unable to execute GiveDoorKeyItem game action: Item [%s] is not flagged as a door key", pItemDef->pchName);
				} else {
					NOCONST(Item) *pItem = NULL;
						
					// Get Destination Bag
					InvBagIDs destBag = InvBagIDs_Inventory;

					if ( pItemDef != NULL ) {
						// check to see if there is a bag override for this item type
						destBag = itemAcquireOverride_FromGameAction(pItemDef);
						if ( destBag == InvBagIDs_None ) {
							destBag = inv_trh_GetBestBagForItemDef(ent, pItemDef, 1, false, pExtract);
							if (destBag == InvBagIDs_None) {
								destBag = InvBagIDs_Inventory;
							}
						}
					}					
					
					// Create Item
					pItem = CONTAINER_NOCONST(Item, item_FromEnt(ent, pchItemDefName,0,NULL,0));

					if (ISNULL(pItem)) {
						TRANSACTION_RETURN_LOG_FAILURE("Unable to execute GiveDoorKeyItem game action: Failed to create item [%s]", pItemDef?pItemDef->pchName:"(null)");
					} else {
						MissionDef* pMissionDef = missiondef_DefFromRefString(pcMission);
						WorldVariable* pDestination = NULL;
						const char* pchMap = NULL;
						const char* pchDoorKey = NULL;
						const char* pchVars = NULL;
						const char* pchMapVars = NULL;
						NOCONST(SpecialItemProps)* pProps = NULL;

						for (j = eaSize(&pDoorDestVarArray->eaVariables)-1; j >= 0; j--) {
							GameActionDoorDestinationVariable* pDoorVar = pDoorDestVarArray->eaVariables[j];
							if (pDoorVar->iGameActionIndex == i) {
								pDestination = pDoorVar->pDoorDestination;
								pchMapVars = pDoorVar->pchMapVars;
								break;
							}
						}

						pchMap = SAFE_MEMBER(pDestination, pcZoneMap);

						if (pDestination && !pDestination->pcZoneMap) {
							pchMap = pchMapName;
							pchVars = pchInitMapVars;
						} else if (pDestination) {
							pchVars = pchMapVars;
						}

						if (action->pGiveDoorKeyItemProperties->pchDoorKey) {				
							pchDoorKey = action->pGiveDoorKeyItemProperties->pchDoorKey;
						} else {
							int iVarIdx;
							for (iVarIdx = eaSize(&pMapVariableArray->eaVariables)-1; iVarIdx >= 0; iVarIdx--) {
								if (stricmp(pMapVariableArray->eaVariables[iVarIdx]->pcName, ITEM_DOOR_KEY_MAP_VAR)==0) {
									pchDoorKey = pMapVariableArray->eaVariables[iVarIdx]->pcStringVal;
									break;
								}
							}
						}

						pProps = (NOCONST(SpecialItemProps)*)item_trh_GetOrCreateSpecialProperties(pItem);

						// Check for valid destination
						if (!EMPTY_TO_NULL(pchMap) || !zmapInfoGetByPublicName(pchMap)) {
							TRANSACTION_RETURN_LOG_FAILURE("Unable to execute GiveDoorKeyItem game action: Invalid destination map");
						}

						if (ISNULL(pProps->pDoorKey)) {
							pProps->pDoorKey = StructCreateNoConst(parse_ItemDoorKey);
						}

						// Set map
						pProps->pDoorKey->pchMap = allocAddString(pchMap);

						// Set map vars
						StructCopyString(&pProps->pDoorKey->pchMapVars, pchVars);

						// Check for valid door key
						if (!EMPTY_TO_NULL(pchDoorKey)) {
							TRANSACTION_RETURN_LOG_FAILURE("Unable to execute GiveDoorKeyItem game action: Invalid door key");
						}

						// Set door key
						pProps->pDoorKey->pchDoorKey = allocAddString(pchDoorKey);

						// Setup Mission Info
						if (pMissionDef && pItemDef && pItemDef->flags && (pItemDef->flags & kItemDefFlag_SetMissionOnCreate)) {
							SET_HANDLE_FROM_REFERENT("Mission", pMissionDef, pProps->pDoorKey->hMission);
						}

						inv_AddItem(ATR_PASS_ARGS, ent, NULL, destBag, -1, (Item*)pItem, pchItemDefName, 0, pReason, pExtract);
						StructDestroyNoConst(parse_Item,pItem);
					}
				}
			}
		}
	}

	return TRANSACTION_OUTCOME_SUCCESS;
}


// ----------------------------------------------------------------------------------
// System Initialization
// ----------------------------------------------------------------------------------

AUTO_RUN;
void gameaction_InitSystem(void)
{
	// Set up the game action expression context
	s_pGameActionContext = exprContextCreate();
	exprContextSetFuncTable(s_pGameActionContext, encPlayer_CreateExprFuncTable());
	exprContextSetAllowRuntimePartition(s_pGameActionContext);
	exprContextSetAllowRuntimeSelfPtr(s_pGameActionContext);
}
