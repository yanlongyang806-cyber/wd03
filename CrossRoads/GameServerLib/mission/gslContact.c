/***************************************************************************
*     Copyright (c) 2006-2007, Cryptic Studios
*     All Rights Reserved
*     Confidential Property of Cryptic Studios
***************************************************************************/

#include "AlgoPet.h"
#include "allegiance.h"
#include "AutoTransDefs.h"
#include "Character.h"
#include "CharacterRespecServer.h"
#include "CharacterClass.h"
#include "Expression.h"
#include "contact_common.h"
#include "CostumeCommonRandom.h"
#include "CostumeCommonTailor.h"
#include "entcritter.h"
#include "entdebugmenu.h"
#include "entitygrid.h"
#include "EntityIterator.h"
#include "EntityLib.h"
#include "EntitySavedData.h"
#include "GameAccountDataCommon.h"
#include "gameaction_common.h"
#include "GameServerLib.h"
#include "GameStringFormat.h"
#include "gslChat.h"
#include "gslChatConfig.h"
#include "gslContact.h"
#include "gslEncounter.h"
#include "gslEventSend.h"
#include "gslGameAction.h"
#include "gslInteractable.h"
#include "gslInteraction.h"
#include "gslItemAssignments.h"
#include "gslMapState.h"
#include "gslMapTransfer.h"
#include "gslMapVariable.h"
#include "gslMission.h"
#include "gslMission_transact.h"
#include "gslOldEncounter.h"
#include "gslPartition.h"
#include "gslPowerStore.h"
#include "gslSavedPet.h"
#include "gslSendToClient.h"
#include "gslVolume.h"
#include "gslWaypoint.h"
#include "Guild.h"
#include "HashFunctions.h"
#include "interaction_common.h"
#include "inventoryTransactions.h"
#include "ItemAssignments.h"
#include "itemTransaction.h"
#include "mapstate_common.h"
#include "mission_common.h"
#include "nemesis_common.h"
#include "NotifyCommon.h"
#include "OfficerCommon.h"
#include "oldencounter_common.h"
#include "Player.h"
#include "powerStoreCommon.h"
#include "rand.h"
#include "Reward.h"
#include "rewardCommon.h"
#include "SavedPetCommon.h"
#include "species_common.h"
#include "Store.h"
#include "storeCommon.h"
#include "StringCache.h"
#include "wlInteraction.h"
#include "WorldGrid.h"
#include "AnimList_Common.h"
#include "CostumeCommonEntity.h"
#include "gslNamedPoint.h"
#include "wlEncounter.h"
#include "wininclude.h"
#include "GameAccountDataCommon.h"
#include "WorldColl.h"
#include "cutscene_common.h"
#include "StringUtil.h"
#include "aiTeam.h"

#include "AutoGen/allegiance_h_ast.h"
#include "AutoGen/Entity_h_ast.h"
#include "AutoGen/chatCommon_h_ast.h"
#include "AutoGen/contact_common_h_ast.h"
#include "AutoGen/CostumeCommon_h_ast.h"
#include "AutoGen/GameClientLib_autogen_ClientCmdWrappers.h"
#include "AutoGen/gslContact_h_ast.h"
#include "AutoGen/ItemAssignments_h_ast.h"
#include "AutoGen/mission_common_h_ast.h"
#include "AutoGen/SoundLib_autogen_ClientCmdWrappers.h"

// ----------------------------------------------------------------------------
//  Static data
// ----------------------------------------------------------------------------

// The queue for the dialog responses
static DialogResponseQueueItem **s_eaDialogResponseQueueItems = NULL;

// Special dialogs and mission offers injected into contacts from namespaced missions
static ContactOverrideData **s_eaNamespacedContactOverrides;

static bool contact_CanParticipateInTeamDialog(Entity *pEntTeamMember, Entity *pEntTeamTalker, ContactDialog *pDialog);
static bool contact_IsInTeamDialog(Entity *pEntTeamMember);
static void contact_EndDialogForOtherTeamMembers(SA_PARAM_NN_VALID Entity* pPlayerEnt);
static void contact_InteractResponseWithQueueing(Entity* playerEnt, const char* pchResponseKey, ContactRewardChoices* rewardChoices, U32 iOptionAssistEntID, bool bUseQueue, bool bAutomaticallyChosenBySystem);

ContactLocation** g_ContactLocations;

static ExprContext *s_pContactExprContext = NULL;
static RemoteContact** s_eaRemoteContactList = NULL;

static bool s_bContactSystemLoaded = false;

static U32 s_ContactTick = 0;

extern const char** g_eaMissionsWaitingForOverrideProcessing;
extern bool gValidateContactsOnNextTick;

// Keep this above 200, so that UGC missions (heavy on contact
// dialogs) do not run up against this limit.
#define MAX_TRACKED_DIALOGS 256

#define CONTACT_INDICATOR_SYSTEM_TICK 30
#define CONTACT_DIALOG_SYSTEM_TICK 10

#define TEAM_CONTACT_SHARE_DISTANCE 80.f

#define TEAM_DIALOG_RESPONSE_DELAY 1000 // 1 second delay
#define TEAM_DIALOG_RESPONSE_CLEAR_NOTIFICATION_DELAY 700 // 0.7 second delay

// Static message references for hardcoded ContactDialog messages
typedef struct ContactDialogMessages
{
	REF_TO(Message) hContinueMsg;
	REF_TO(Message) hBackMsg;
	REF_TO(Message) hExitMsg;

	REF_TO(Message) hContactInfoMsg;
	REF_TO(Message) hTailorMsg;
	REF_TO(Message) hStarshipTailorMsg;
	REF_TO(Message) hStarshipChooserMsg;
	REF_TO(Message) hWeaponTailorMsg;
	REF_TO(Message) hStoreMsg;
	REF_TO(Message) hRecipeStoreMsg;
	REF_TO(Message) hPowerStoreMsg;
	REF_TO(Message) hInjuryStoreMsg;
	REF_TO(Message) hNemesisMsg;
	REF_TO(Message) hNewNemesisMsg;
	REF_TO(Message) hGuildMsg;
	REF_TO(Message) hNewGuildMsg;
	REF_TO(Message) hRespecMsg;
	REF_TO(Message) hMailBoxMsg;
	REF_TO(Message) hBankMsg;
	REF_TO(Message) hSharedBankMsg;
	REF_TO(Message) hGuildBankMsg;
	REF_TO(Message) hSpecialDialogDefaultMsg;
	REF_TO(Message) hMinigameMsg;
	REF_TO(Message) hItemAssignmentMsg;
	REF_TO(Message) hImageMenuMsg;

	REF_TO(Message) hLearnScienceMsg;
	REF_TO(Message) hLearnScienceDifferentSpecMsg;
	REF_TO(Message) hLearnScienceConfirmHeader;
	REF_TO(Message) hLearnScienceConfirmText;
	REF_TO(Message) hLearnArmsMsg;
	REF_TO(Message) hLearnArmsDifferentSpecMsg;
	REF_TO(Message) hLearnArmsConfirmHeader;
	REF_TO(Message) hLearnArmsConfirmText;
	REF_TO(Message) hLearnMysticismMsg;
	REF_TO(Message) hLearnMysticismConfirmHeader;
	REF_TO(Message) hLearnMysticismConfirmText;
	REF_TO(Message) hLearnMysticismDifferentSpecMsg;
	REF_TO(Message) hTrainPowersMsg;
	REF_TO(Message) hEnterMarketMsg;
	REF_TO(Message) hAuctionBrokerMsg;
	REF_TO(Message) hUGCSearchMsg;	
	REF_TO(Message) hZStoreMsg;	

	REF_TO(Message) hPetJoinMsg;
	REF_TO(Message) hNotNowMsg;

	REF_TO(Message) hCompletedMissionMsg;
	REF_TO(Message) hFailedMissionMsg;
	REF_TO(Message) hOfferedMissionMsg;
	REF_TO(Message) hOfferedFlashbackMsg;
	REF_TO(Message) hInProgMissionMsg;
	REF_TO(Message) hViewSubMissionMsg;

	REF_TO(Message) hAcceptMsg;
	REF_TO(Message) hDeclineMsg;
	REF_TO(Message) hRestartMsg;
	REF_TO(Message) hCollectRewardMsg;
	REF_TO(Message) hSetWaypointMsg;

	REF_TO(Message) hDefaultListHeaderMsg;
	REF_TO(Message) hRewardsHeaderMsg;
	REF_TO(Message) hOptionalRewardsHeaderMsg;
	REF_TO(Message) hMissionObjectivesHeaderMsg;
} ContactDialogMessages;

static ContactDialogMessages s_ContactMsgs;
static SpecialDialogBlockRandomData** s_eaSpecialDialogRandomData = NULL;

#define CONTACT_DIALOG_OPTIONS_PERIODIC_UPDATE_TIME 10

// ----------------------------------------------------------------------------
//  AUTO_RUNs and startup code
// ----------------------------------------------------------------------------

AUTO_RUN_LATE;
void contactsystem_InitContactDialogMessages(void)
{
	// All hardcoded messages for Contact Dialogs are initialized here
	SET_HANDLE_FROM_STRING(gMessageDict, "ContactDialog.Continue", s_ContactMsgs.hContinueMsg);
	SET_HANDLE_FROM_STRING(gMessageDict, "ContactDialog.Back", s_ContactMsgs.hBackMsg);
	SET_HANDLE_FROM_STRING(gMessageDict, "ContactDialog.Exit", s_ContactMsgs.hExitMsg);

	SET_HANDLE_FROM_STRING(gMessageDict, "ContactDialog.ContactInfo", s_ContactMsgs.hContactInfoMsg);
	SET_HANDLE_FROM_STRING(gMessageDict, "ContactDialog.Tailor", s_ContactMsgs.hTailorMsg);
	SET_HANDLE_FROM_STRING(gMessageDict, "ContactDialog.StarshipTailor", s_ContactMsgs.hStarshipTailorMsg);
	SET_HANDLE_FROM_STRING(gMessageDict, "ContactDialog.StarshipChooser", s_ContactMsgs.hStarshipChooserMsg);
	SET_HANDLE_FROM_STRING(gMessageDict, "ContactDialog.WeaponTailor", s_ContactMsgs.hWeaponTailorMsg);
	SET_HANDLE_FROM_STRING(gMessageDict, "ContactDialog.Store", s_ContactMsgs.hStoreMsg);
	SET_HANDLE_FROM_STRING(gMessageDict, "ContactDialog.RecipeStore", s_ContactMsgs.hRecipeStoreMsg);
	SET_HANDLE_FROM_STRING(gMessageDict, "ContactDialog.PowerStore", s_ContactMsgs.hPowerStoreMsg);
	SET_HANDLE_FROM_STRING(gMessageDict, "ContactDialog.InjuryStore", s_ContactMsgs.hInjuryStoreMsg);
	SET_HANDLE_FROM_STRING(gMessageDict, "ContactDialog.Nemesis", s_ContactMsgs.hNemesisMsg);
	SET_HANDLE_FROM_STRING(gMessageDict, "ContactDialog.NewNemesis", s_ContactMsgs.hNewNemesisMsg);
	SET_HANDLE_FROM_STRING(gMessageDict, "ContactDialog.Guild", s_ContactMsgs.hGuildMsg);
	SET_HANDLE_FROM_STRING(gMessageDict, "ContactDialog.NewGuild", s_ContactMsgs.hNewGuildMsg);
	SET_HANDLE_FROM_STRING(gMessageDict, "ContactDialog.Respec", s_ContactMsgs.hRespecMsg);
	SET_HANDLE_FROM_STRING(gMessageDict, "ContactDialog.MailBox", s_ContactMsgs.hMailBoxMsg);
	SET_HANDLE_FROM_STRING(gMessageDict, "ContactDialog.Bank", s_ContactMsgs.hBankMsg);
	SET_HANDLE_FROM_STRING(gMessageDict, "ContactDialog.SharedBank", s_ContactMsgs.hSharedBankMsg);
	SET_HANDLE_FROM_STRING(gMessageDict, "ContactDialog.GuildBank", s_ContactMsgs.hGuildBankMsg);
	SET_HANDLE_FROM_STRING(gMessageDict, "ContactDialog.SpecialDialogDefault", s_ContactMsgs.hSpecialDialogDefaultMsg);
	SET_HANDLE_FROM_STRING(gMessageDict, "ContactDialog.Minigame", s_ContactMsgs.hMinigameMsg);
	SET_HANDLE_FROM_STRING(gMessageDict, "ContactDialog.ItemAssignments", s_ContactMsgs.hItemAssignmentMsg);
	SET_HANDLE_FROM_STRING(gMessageDict, "ContactDialog.ZStore", s_ContactMsgs.hZStoreMsg);

	SET_HANDLE_FROM_STRING(gMessageDict, "ContactDialog.LearnScienceSkill", s_ContactMsgs.hLearnScienceMsg);
	SET_HANDLE_FROM_STRING(gMessageDict, "ContactDialog.LearnScienceDifferentSpec", s_ContactMsgs.hLearnScienceDifferentSpecMsg);
	SET_HANDLE_FROM_STRING(gMessageDict, "ContactDialog.LearnScienceConfirmHeader", s_ContactMsgs.hLearnScienceConfirmHeader);
	SET_HANDLE_FROM_STRING(gMessageDict, "ContactDialog.LearnScienceConfirmText", s_ContactMsgs.hLearnScienceConfirmText);
	SET_HANDLE_FROM_STRING(gMessageDict, "ContactDialog.LearnArmsSkill", s_ContactMsgs.hLearnArmsMsg);
	SET_HANDLE_FROM_STRING(gMessageDict, "ContactDialog.LearnArmsDifferentSpec", s_ContactMsgs.hLearnArmsDifferentSpecMsg);
	SET_HANDLE_FROM_STRING(gMessageDict, "ContactDialog.LearnArmsConfirmHeader", s_ContactMsgs.hLearnArmsConfirmHeader);
	SET_HANDLE_FROM_STRING(gMessageDict, "ContactDialog.LearnArmsConfirmText", s_ContactMsgs.hLearnArmsConfirmText);
	SET_HANDLE_FROM_STRING(gMessageDict, "ContactDialog.LearnMysticismSkill", s_ContactMsgs.hLearnMysticismMsg);
	SET_HANDLE_FROM_STRING(gMessageDict, "ContactDialog.LearnMysticismDifferentSpec", s_ContactMsgs.hLearnMysticismDifferentSpecMsg);
	SET_HANDLE_FROM_STRING(gMessageDict, "ContactDialog.LearnMysticismConfirmHeader", s_ContactMsgs.hLearnMysticismConfirmHeader);
	SET_HANDLE_FROM_STRING(gMessageDict, "ContactDialog.LearnMysticismConfirmText", s_ContactMsgs.hLearnMysticismConfirmText);

	SET_HANDLE_FROM_STRING(gMessageDict, "ContactDialog.TrainPowers", s_ContactMsgs.hTrainPowersMsg);
	SET_HANDLE_FROM_STRING(gMessageDict, "ContactDialog.EnterMarket", s_ContactMsgs.hEnterMarketMsg);
	SET_HANDLE_FROM_STRING(gMessageDict, "ContactDialog.AuctionBroker", s_ContactMsgs.hAuctionBrokerMsg);

	SET_HANDLE_FROM_STRING(gMessageDict, "ContactDialog.PetJoin", s_ContactMsgs.hPetJoinMsg);
	SET_HANDLE_FROM_STRING(gMessageDict, "ContactDialog.NotNow", s_ContactMsgs.hNotNowMsg);

	SET_HANDLE_FROM_STRING(gMessageDict, "ContactDialog.ViewCompletedMission", s_ContactMsgs.hCompletedMissionMsg);
	SET_HANDLE_FROM_STRING(gMessageDict, "ContactDialog.ViewFailedMission", s_ContactMsgs.hFailedMissionMsg);
	SET_HANDLE_FROM_STRING(gMessageDict, "ContactDialog.ViewOfferedMission", s_ContactMsgs.hOfferedMissionMsg);
	SET_HANDLE_FROM_STRING(gMessageDict, "ContactDialog.ViewOfferedFlashbackMission", s_ContactMsgs.hOfferedFlashbackMsg);
	SET_HANDLE_FROM_STRING(gMessageDict, "ContactDialog.ViewInProgMission", s_ContactMsgs.hInProgMissionMsg);
	SET_HANDLE_FROM_STRING(gMessageDict, "ContactDialog.ViewSubMission", s_ContactMsgs.hViewSubMissionMsg);

	SET_HANDLE_FROM_STRING(gMessageDict, "ContactDialog.Accept", s_ContactMsgs.hAcceptMsg);
	SET_HANDLE_FROM_STRING(gMessageDict, "ContactDialog.Decline", s_ContactMsgs.hDeclineMsg);
	SET_HANDLE_FROM_STRING(gMessageDict, "ContactDialog.Restart", s_ContactMsgs.hRestartMsg);
	SET_HANDLE_FROM_STRING(gMessageDict, "ContactDialog.CollectReward", s_ContactMsgs.hCollectRewardMsg);
	SET_HANDLE_FROM_STRING(gMessageDict, "ContactDialog.SetWaypoint", s_ContactMsgs.hSetWaypointMsg);

	SET_HANDLE_FROM_STRING(gMessageDict, "ContactDialog.DefaultListHeader", s_ContactMsgs.hDefaultListHeaderMsg);
	SET_HANDLE_FROM_STRING(gMessageDict, "ContactDialog.RewardsHeader", s_ContactMsgs.hRewardsHeaderMsg);
	SET_HANDLE_FROM_STRING(gMessageDict, "ContactDialog.OptionalRewardsHeader", s_ContactMsgs.hOptionalRewardsHeaderMsg);
	SET_HANDLE_FROM_STRING(gMessageDict, "ContactDialog.MissionObjectivesHeader", s_ContactMsgs.hMissionObjectivesHeaderMsg);
}

AUTO_RUN;
void contactsystem_Init(void)
{
	s_pContactExprContext = exprContextCreate();
	exprContextSetFuncTable(s_pContactExprContext, contact_CreateExprFuncTable());
}

AUTO_STARTUP(Contacts) ASTRT_DEPS(Stores,Missions,WorldOptionalActionCategories,Minigames,ContactDialogFormatters);
void contactsystem_Load(void)
{
	contact_LoadDefs();
	s_bContactSystemLoaded = true;
}

bool contactsystem_IsLoaded()
{
	return s_bContactSystemLoaded;
}


// ----------------------------------------------------------------------------
//  Evaluating Expressions
// ----------------------------------------------------------------------------

int contact_Evaluate(Entity *pEnt, Expression *pExpr, WorldScope *pScope)
{
	int iResult;

	PERFINFO_AUTO_START_FUNC();

	contact_EvalSetupContext(pEnt, pScope);

	iResult = contact_EvaluateAfterManualContextSetup(pExpr);

	PERFINFO_AUTO_STOP();
	return iResult;
}

void contact_EvalSetupContext(Entity *pEnt, WorldScope *pScope)
{
	PERFINFO_AUTO_START_FUNC();
	exprContextSetSelfPtr(s_pContactExprContext, pEnt);
	exprContextSetPartition(s_pContactExprContext, entGetPartitionIdx(pEnt));
	exprContextSetScope(s_pContactExprContext, pScope);

	// If the entity is a player, add it to the context as "Player"
	if (entGetPlayer(pEnt)) {
		exprContextSetPointerVarPooled(s_pContactExprContext, g_PlayerVarName, pEnt, NULL, false, true);
	} else {
		exprContextRemoveVarPooled(s_pContactExprContext, g_PlayerVarName);
	}
	PERFINFO_AUTO_STOP();
}

int contact_EvaluateAfterManualContextSetup(Expression *pExpr)
{
	MultiVal mvResultVal;
	int iResult;

	PERFINFO_AUTO_START_FUNC();

	exprEvaluate(pExpr, s_pContactExprContext, &mvResultVal);

	iResult = MultiValGetInt(&mvResultVal, NULL);

	PERFINFO_AUTO_STOP();
	return iResult;
}

// ----------------------------------------------------------------------------
//  ContactDef post-processing
// ----------------------------------------------------------------------------
//generate all the expressions that could be in this contact def.
int contact_DefPostProcess(ContactDef* def)
{
	static ExprContext* exprContext = NULL;
	int i;

	if (!exprContext)
	{
		exprContext = exprContextCreate();
		exprContextSetFuncTable(exprContext, contact_CreateExprFuncTable());
		exprContextSetAllowRuntimePartition(exprContext);
		exprContextSetAllowRuntimeSelfPtr(exprContext);
	}

	if (def->interactReqs)
	{
		exprGenerate(def->interactReqs, exprContext);
	}

	if (def->canAccessRemotely)
	{
		exprGenerate(def->canAccessRemotely, exprContext);
	}
	//generate actions in special dialog responses
	if (eaSize(&def->specialDialog))
	{
		int numDialogs = eaSize(&def->specialDialog);
		for (i = 0; i < numDialogs; i++)
		{
			contact_SpecialDialogPostProcess(def->specialDialog[i], def->filename);
		}

	}
	//generate actions in special action blocks
	if (eaSize(&def->specialActions))
	{
		int numActions = eaSize(&def->specialActions);
		for (i = 0; i < numActions; i++)
		{
			contact_SpecialActionPostProcess(def->specialActions[i], def->filename);
		}

	}
	//generate actions in image menu items
	if(def->pImageMenuData){
		for( i = 0; i < eaSize( &def->pImageMenuData->items ); ++i ) {
			contact_ImageMenuItemPostProcess(def->pImageMenuData->items[i], def->filename);
		}
	}

	for (i = eaSize(&def->eaLoreDialogs)-1; i >= 0; --i){
		if (def->eaLoreDialogs[i]->pCondition){
			exprGenerate(def->eaLoreDialogs[i]->pCondition, exprContext);
		}
	}

	for (i = eaSize(&def->greetingDialog)-1; i >= 0; --i){
		if (def->greetingDialog[i]->condition){
			exprGenerate(def->greetingDialog[i]->condition, exprContext);
		}
	}

	return 1;
}

// ----------------------------------------------------------------------------
//  Contact System code
// ----------------------------------------------------------------------------
static void contact_GetPhrasePathFromDialogBlock(DialogBlock *pDialogBlock, const char **ppchPhrasePath);
static void contact_ChangeDialogState(Entity* pPlayerEnt, EntityRef contactEnt, ContactDef *pContactDef, const ContactDialogOptionData* pOption);
static void contactdialog_CreateSpecialDialogOptions(Entity *pPlayerEnt, int iPlayerLevel, ContactDef *pContactDef, ContactDialog *pDialog, SpecialDialogBlock *pSpecialDialog, bool bSetVisitedStatus, WorldVariable** eaMapVars);
static ContactDialogOption* contactdialog_CreateMissionDialogOption(Entity *pPlayerEnt, int iPlayerLevel, MissionDef *pMissionDef, ContactDialogOption*** peaOptionsList, const char *pchKey, Message *pDisplayTextMsg, ContactDialogState targetState, ContactActionType action, WorldVariable** eaMapVars);
static void contact_GetAvailableMissions(SA_PARAM_NN_VALID ContactDef* contact, SA_PARAM_NN_VALID Entity* playerEnt, SA_PARAM_OP_VALID MissionDef*** offerMissionDefs, SA_PARAM_OP_VALID MissionDef*** offerFlashbackMissionDefs, SA_PARAM_OP_VALID MissionDef*** inProgMissionDefs, SA_PARAM_OP_VALID MissionDef*** failedMissionDefs, SA_PARAM_OP_VALID MissionDef*** completeMissionDefs, SA_PARAM_OP_VALID MissionDef*** subMissionDefs, SA_PARAM_OP_VALID MissionDef*** onCooldownMissionDefs, SA_PARAM_OP_VALID MissionDef*** offerReplayMissionDefs, bool bRemoteInteract, SA_PARAM_OP_VALID ContactMissionOffer *pMissionOfferToProcess);
static ContactDialogOption * contact_CreateMissionDialogOptions(SA_PARAM_NN_VALID ContactDef* pContactDef, SA_PARAM_NN_VALID ContactDialogOption*** peaOptionsToFill, SA_PARAM_NN_VALID Entity* pPlayerEnt, int iPlayerLevel, SA_PARAM_NN_VALID MissionDef*** offerMissionDefs, SA_PARAM_NN_VALID MissionDef*** offerFlashbackMissionDefs, SA_PARAM_NN_VALID MissionDef*** inProgMissionDefs, SA_PARAM_NN_VALID MissionDef*** failedMissionDefs, SA_PARAM_NN_VALID MissionDef*** completeMissionDefs, SA_PARAM_NN_VALID MissionDef*** subMissionDefs, SA_PARAM_NN_VALID MissionDef*** onCooldownMissionDefs, SA_PARAM_NN_VALID MissionDef*** offerReplayMissionDefs, WorldVariable** eaMapVars, bool bRemoteInteract, bool bStopAfterFirstMission);

static ContactDialogOption * contact_CreateDialogOptionForMissionOfferTargetedFromSpecialDialog(SA_PARAM_NN_VALID ContactMissionOffer *pTargetedMissionOffer, 
																								SA_PARAM_NN_VALID ContactDef *pContactDef,
																								SA_PARAM_NN_VALID Entity *pPlayerEnt,
																								int iPlayerLevel,
																								SA_PARAM_NN_VALID ContactDialog *pDialog,
																								SA_PARAM_OP_VALID Message *pDisplayTextMsg,
																								ContactActionType eAction,
																								WorldVariable** eaMapVars,
																								ContactDialogFormatterDef *pFormatterDef)
{
	ContactDialogOption *pNewOption = NULL;

	if (pTargetedMissionOffer)
	{
		MissionDef** eaOfferMissionList = NULL;
		MissionDef** eaOfferFlashbackList = NULL;
		MissionDef** eaOfferReplayList = NULL;
		MissionDef** eaInProgMissionList = NULL;
		MissionDef** eaFailedMissionList = NULL;
		MissionDef** eaCompleteMissionList = NULL;
		MissionDef** eaSubMissionList = NULL;
		MissionDef** eaOnCooldownMissionList = NULL;

		// All missions
		contact_GetAvailableMissions(pContactDef, pPlayerEnt, &eaOfferMissionList, &eaOfferFlashbackList, &eaInProgMissionList, &eaFailedMissionList, &eaCompleteMissionList, &eaSubMissionList, &eaOnCooldownMissionList, &eaOfferReplayList, pDialog->bPartialPermissions, pTargetedMissionOffer);

		// Create mission dialog options
		pNewOption = contact_CreateMissionDialogOptions(pContactDef, &pDialog->eaOptions, pPlayerEnt, iPlayerLevel, &eaOfferMissionList, &eaOfferFlashbackList, &eaInProgMissionList, &eaFailedMissionList, &eaCompleteMissionList, &eaSubMissionList, &eaOnCooldownMissionList, &eaOfferReplayList, eaMapVars, pDialog->bPartialPermissions, true);

		if (pNewOption && pNewOption->pData)
		{
			pNewOption->pData->action = eAction;

			// Use the display text from the special dialog instead of the mission name
			if (pDisplayTextMsg) 
			{
				WorldVariable **ppMapVars = NULL;
				eaCreate(&ppMapVars);
				mapvariable_GetAllAsWorldVarsNoCopy(entGetPartitionIdx(pPlayerEnt), &ppMapVars);
				estrClear(&pNewOption->pchDisplayString);
				entFormatGameMessage(pPlayerEnt, &pNewOption->pchDisplayString, pDisplayTextMsg, STRFMT_ENTITY(pPlayerEnt), STRFMT_MAPVARS(ppMapVars), STRFMT_END);
				eaDestroy(&ppMapVars);
			}

			if (pFormatterDef)
			{
				SET_HANDLE_FROM_REFERENT(g_hContactDialogFormatterDefDictionary, pFormatterDef, pNewOption->hDialogFormatter);
			}
		}

		eaDestroy(&eaOfferMissionList);
		eaDestroy(&eaOfferFlashbackList);
		eaDestroy(&eaOfferReplayList);
		eaDestroy(&eaInProgMissionList);
		eaDestroy(&eaFailedMissionList);
		eaDestroy(&eaCompleteMissionList);
		eaDestroy(&eaSubMissionList);
		eaDestroy(&eaOnCooldownMissionList);
	}

	return pNewOption;
}

static void contact_SetVisitedDialogKey(const char * pchSpecialDialogName, const char * pchResponseKey, char **pestrKey, U32 iSpecialDialogSubIndex)
{
	if (gConf.bRememberVisitedDialogs)
	{
		if (pestrKey && pchResponseKey && pchResponseKey[0])
		{
			if (pchSpecialDialogName && pchSpecialDialogName[0])
			{
				estrAppend2(pestrKey, pchSpecialDialogName);
				estrAppend2(pestrKey, "_");
			}
			else
			{
				estrAppend2(pestrKey, "__Dialog_Root_");
			}
			estrAppend2(pestrKey, pchResponseKey);
			estrConcatf(pestrKey, "%u", iSpecialDialogSubIndex);
		}
	}
}

static void contact_SetDialogOptionVisitedStatus(Entity *pEnt, ContactDialogOption *pOption, ContactDef *pContactDef)
{
	if (gConf.bRememberVisitedDialogs)
	{
		InteractInfo *pInteractInfo = SAFE_MEMBER2(pEnt, pPlayer, pInteractInfo);
		if (pInteractInfo && pOption)
		{		
			ContactDialog *pOrigDialog = pInteractInfo->pContactDialog;
			SpecialDialogBlock *pSpecialDialog = pContactDef && pOption && pOption->pData ? contact_SpecialDialogFromName(pContactDef, pOption->pData->pchTargetDialogName) : NULL;		
			SpecialDialogBlock *pCurSpecialDialog = (pInteractInfo->pContactDialog ? contact_SpecialDialogFromName(pContactDef, pOrigDialog->pchSpecialDialogName) : NULL);
			if (pOption->pData && pOption->pData->targetState == ContactDialogState_OptionListFarewell)
			{
				char *estrDialogVisitedKey = NULL;

				// Set the key for this dialog option
				if(pOption->bWasAppended){
					contact_SetVisitedDialogKey(pInteractInfo->pContactDialog ? pCurSpecialDialog->pchAppendName : NULL, pOption->pchKey, &estrDialogVisitedKey, 0);
					//contact_SetVisitedDialogKey(pInteractInfo->pContactDialog ? pOrigDialog->pchSpecialDialogName : NULL, pOption->pchKey, &estrDialogVisitedKey, 0);
				} else {
					contact_SetVisitedDialogKey(pInteractInfo->pContactDialog ? pOrigDialog->pchSpecialDialogName : NULL, pOption->pchKey, &estrDialogVisitedKey, pOption->pData ? pOption->pData->iSpecialDialogSubIndex : 0);
				}

				if (estrDialogVisitedKey != NULL &&
					eaFindString(&pInteractInfo->ppchVisitedSpecialDialogs, estrDialogVisitedKey) >= 0)
				{
					pOption->bVisited = true;
				}

				estrDestroy(&estrDialogVisitedKey);
			}
			else if (pOption->pData && 
				pOption->pData->targetState == ContactDialogState_SpecialDialog &&
				pSpecialDialog)
			{
				bool bVisited = true;
				static ContactDialog *pDialog = NULL;
				WorldVariable **eaMapVars = NULL;
				char *estrDialogVisitedKey = NULL;
				int iPartitionIdx = entGetPartitionIdx(pEnt);
				int iExpLevel = entity_GetSavedExpLevel(pEnt);

				if (pDialog == NULL)
				{
					pDialog = StructCreate(parse_ContactDialog);
				}			

				// Reset the dialog fields
				pDialog->iSpecialDialogSubIndex = pOrigDialog ? pOrigDialog->iSpecialDialogSubIndex : 0;
				eaClearStruct(&pDialog->eaOptions, parse_ContactDialogOption);

				mapvariable_GetAllAsWorldVarsNoCopy(iPartitionIdx, &eaMapVars);

				// Generate the list of possible options
				contactdialog_CreateSpecialDialogOptions(pEnt, iExpLevel, pContactDef, pDialog, pSpecialDialog, false, eaMapVars);

				eaDestroy(&eaMapVars);

				// All child options must be visited for this node to be counted as visited
				FOR_EACH_IN_EARRAY_FORWARDS(pDialog->eaOptions, ContactDialogOption, pCurrentOption)
				{
					estrClear(&estrDialogVisitedKey);
					
					// Set the key for this dialog option
					if(pCurrentOption->bWasAppended){
						contact_SetVisitedDialogKey(pInteractInfo->pContactDialog ? pSpecialDialog->pchAppendName : NULL, pCurrentOption->pchKey, &estrDialogVisitedKey, 0);
						//contact_SetVisitedDialogKey(pSpecialDialog->name, pCurrentOption->pchKey, &estrDialogVisitedKey, 0);
					} else {
						contact_SetVisitedDialogKey(pSpecialDialog->name, pCurrentOption->pchKey, &estrDialogVisitedKey, pCurrentOption->pData ? pCurrentOption->pData->iSpecialDialogSubIndex : 0);
					}

					if (!pCurrentOption->bCannotChoose &&
						estrDialogVisitedKey &&
						estrDialogVisitedKey[0] &&
						eaFindString(&pInteractInfo->ppchVisitedSpecialDialogs, estrDialogVisitedKey) < 0)
					{
						bVisited = false;
						estrDestroy(&estrDialogVisitedKey);
						break;
					}				
				}
				FOR_EACH_END

				estrClear(&estrDialogVisitedKey);
				// Set the key for this dialog option
				if(pOption->bWasAppended){
					contact_SetVisitedDialogKey(pInteractInfo->pContactDialog ? pCurSpecialDialog->pchAppendName : NULL, pOption->pchKey, &estrDialogVisitedKey, 0);
					//contact_SetVisitedDialogKey(pInteractInfo->pContactDialog ? pOrigDialog->pchSpecialDialogName : NULL, pOption->pchKey, &estrDialogVisitedKey, 0);
				} else {
					contact_SetVisitedDialogKey(pInteractInfo->pContactDialog ? pOrigDialog->pchSpecialDialogName : NULL, pOption->pchKey, &estrDialogVisitedKey, pOption->pData ? pOption->pData->iSpecialDialogSubIndex : 0);
				}

				if(pOption->bWasAppended){
					pOption->bVisited = eaFindString(&pInteractInfo->ppchVisitedSpecialDialogs, estrDialogVisitedKey) >= 0;
				} else {
					pOption->bVisited = bVisited && eaFindString(&pInteractInfo->ppchVisitedSpecialDialogs, estrDialogVisitedKey) >= 0;
				}

				if (pOption->bVisited)
				{
					estrClear(&estrDialogVisitedKey);
					// Set the key for this dialog option
					if(pOption->bWasAppended){
						contact_SetVisitedDialogKey(pInteractInfo->pContactDialog ? pCurSpecialDialog->pchAppendName : NULL, pOption->pchKey, &estrDialogVisitedKey, 0);
						//contact_SetVisitedDialogKey(pInteractInfo->pContactDialog ? pOrigDialog->pchSpecialDialogName : NULL, pOption->pchKey, &estrDialogVisitedKey, 0);
					} else {
						contact_SetVisitedDialogKey(pInteractInfo->pContactDialog ? pOrigDialog->pchSpecialDialogName : NULL, pOption->pchKey, &estrDialogVisitedKey, pOption->pData ? pOption->pData->iSpecialDialogSubIndex : 0);
					}

					if (estrDialogVisitedKey &&
						estrDialogVisitedKey[0] && 
						eaFindString(&pInteractInfo->ppchVisitedSpecialDialogs, estrDialogVisitedKey) < 0)
					{
						// Add to the list of visited dialogs
						eaPush(&pInteractInfo->ppchVisitedSpecialDialogs, strdup(estrDialogVisitedKey));
					}
				}

				// Clean up
				estrDestroy(&estrDialogVisitedKey);
			}
		}
	}

}

static Message * contact_GetDialogExitText(ContactDef *pContactDef)
{
	Message *pMessage = pContactDef ? GET_REF(pContactDef->dialogExitTextOverrideMsg.hMessage) : NULL;
	if (pMessage == NULL)
	{
		pMessage = GET_REF(s_ContactMsgs.hExitMsg);
	}
	return pMessage;	
}

static bool contact_CanParticipateInTeamDialog(Entity *pEntTeamMember, Entity *pEntTeamTalker, ContactDialog *pDialog)
{
	if (pEntTeamMember == NULL || pEntTeamTalker == NULL || pDialog == NULL)
	{
		return false;
	}

	if (!vec3IsZero(pDialog->vecCameraSourcePos))
	{
		if (entGetDistance(pEntTeamMember, NULL, NULL, pDialog->vecCameraSourcePos, NULL) > 250.f)
		{
			return false;
		}
	}
	else if (!g_ContactConfig.bIgnoreDistanceCheckForTeamDialogs)
	{
		Entity *pContactEnt = entFromEntityRef(entGetPartitionIdx(pEntTeamMember), pDialog->headshotEnt);
		F32 fSendDistance = pContactEnt ? pContactEnt->fEntitySendDistance - 20.f : 250.f;
		if (pContactEnt == NULL || entGetDistance(pEntTeamMember, NULL, pContactEnt, NULL, NULL) > fSendDistance)
		{
			return false;
		}
	}

	return team_IsWithTeam(pEntTeamMember) &&
		pEntTeamMember->pTeam->eState == TeamState_Member &&
		(!interaction_IsPlayerInDialog(pEntTeamMember) || contact_IsInTeamDialog(pEntTeamMember)) &&
		!aiTeamInCombat(aiTeamGetCombatTeam(pEntTeamMember, pEntTeamMember->aibase));
		//!entIsInCombat(pEntTeamMember);
}

static bool contact_CanStartTeamDialog(Entity *pEnt)
{
	if (pEnt &&
		team_IsWithTeam(pEnt) &&
		pEnt->pTeam->eState == TeamState_Member)
	{
		Team *pTeam = team_GetTeam(pEnt);
		int iPartitionIdx = entGetPartitionIdx(pEnt);

		if (pTeam)
		{
			FOR_EACH_IN_EARRAY_FORWARDS(pTeam->eaMembers, TeamMember, pTeamMember)
			{
				Entity *pEntTeamMember = entFromContainerID(iPartitionIdx, GLOBALTYPE_ENTITYPLAYER, pTeamMember->iEntID);

				if (pEntTeamMember && 
					pEntTeamMember->myContainerID != pEnt->myContainerID &&
					pEntTeamMember->pTeam &&
					pEntTeamMember->pTeam->bIsTeamSpokesman)
				{
					return false;
				}
			}
			FOR_EACH_END

			return true;
		}
	}
	return false;
}

static void contactdialog_ForceOnTeamDialogViewed(Entity* playerEnt, ContactDef *pContactDef, const char* pchDialogName)
{
	if (pContactDef && pchDialogName && contact_SpecialDialogFromName(pContactDef, pchDialogName))
	{
		// Track the recently viewed force on team contact dialogs
		if (playerEnt && playerEnt->pPlayer && playerEnt->pPlayer->pInteractInfo) {
			ContactDialogInfo *pInfo;
			int i;

			// See if the dialog was already viewed
			for(i=eaSize(&playerEnt->pPlayer->pInteractInfo->recentlyViewedForceOnTeamDialogs)-1; i>=0; --i) {
				pInfo = playerEnt->pPlayer->pInteractInfo->recentlyViewedForceOnTeamDialogs[i];
				if ((pContactDef == GET_REF(pInfo->hContact)) && (pchDialogName == pInfo->pcDialogName)) {
					break;
				}
			}
			if (i < 0) {
				// First time viewing dialog, so add info
				pInfo = StructCreate(parse_ContactDialogInfo);
				SET_HANDLE_FROM_REFERENT(g_ContactDictionary, pContactDef, pInfo->hContact);
				pInfo->pcDialogName = pchDialogName;
				eaPush(&playerEnt->pPlayer->pInteractInfo->recentlyViewedForceOnTeamDialogs, pInfo);
			}
			// Restrict number of dialogs tracked
			while (eaSize(&playerEnt->pPlayer->pInteractInfo->recentlyViewedForceOnTeamDialogs) > MAX_TRACKED_DIALOGS) {
				StructDestroy(parse_ContactDialogInfo, playerEnt->pPlayer->pInteractInfo->recentlyViewedForceOnTeamDialogs[0]);
				eaRemove(&playerEnt->pPlayer->pInteractInfo->recentlyViewedForceOnTeamDialogs, 0);
			}
		}
	}
}

// Returns the number of conditional greetings which evaluate to true for the given player. Optionally the DialogBlock array can be populated with these greetings.
S32 contact_GetActiveConditionalGreetings(SA_PARAM_NN_VALID Entity* pPlayerEnt, SA_PARAM_NN_VALID ContactDef* pContactDef, SA_PARAM_OP_VALID DialogBlock ***peaGreetingDialogBlocks)
{
	if (pPlayerEnt == NULL || 
		pContactDef == NULL || 
		eaSize(&pContactDef->greetingDialog) <= 0)
	{
		return 0;
	}
	else
	{
		S32 iGreetingCount = 0;
		FOR_EACH_IN_EARRAY_FORWARDS(pContactDef->greetingDialog, DialogBlock, pGreetingDialogBlock)
		{
			const char *pchExpression = pGreetingDialogBlock->condition ? exprGetCompleteString(pGreetingDialogBlock->condition) : NULL;
			if (pchExpression && pchExpression[0])
			{
				if (contact_Evaluate(pPlayerEnt, pGreetingDialogBlock->condition, NULL))
				{
					iGreetingCount++;
					if (peaGreetingDialogBlocks)
					{
						eaPush(peaGreetingDialogBlocks, pGreetingDialogBlock);
					}
				}
			}
		}
		FOR_EACH_END

		return iGreetingCount;
	}
}

// Returns the number of unconditional greetings. Optionally the DialogBlock array can be populated with these greetings.
S32 contact_GetNonConditionalGreetings(SA_PARAM_NN_VALID ContactDef* pContactDef, SA_PARAM_OP_VALID DialogBlock ***peaGreetingDialogBlocks)
{
	if (pContactDef == NULL || 
		eaSize(&pContactDef->greetingDialog) <= 0)
	{
		return 0;
	}
	else
	{
		S32 iGreetingCount = 0;
		FOR_EACH_IN_EARRAY_FORWARDS(pContactDef->greetingDialog, DialogBlock, pGreetingDialogBlock)
		{
			const char *pchExpression = pGreetingDialogBlock->condition ? exprGetCompleteString(pGreetingDialogBlock->condition) : NULL;
			if (pchExpression == NULL || pchExpression[0] == '\0')
			{
				iGreetingCount++;
				if (peaGreetingDialogBlocks)
				{
					eaPush(peaGreetingDialogBlocks, pGreetingDialogBlock);
				}
			}
		}
		FOR_EACH_END

		return iGreetingCount;
	}
}

// Returns the number of active mission greetings if the contact has any mission greetings that would be displayed. Optionally the DialogBlock array can be populated with the mission greetings found.
S32 contact_GetActiveMissionGreetings(SA_PARAM_NN_VALID Entity* pPlayerEnt, SA_PARAM_NN_VALID ContactDef* pContactDef, SA_PARAM_OP_VALID DialogBlock ***peaGreetingDialogBlocks)
{
	MissionInfo* pMissionInfo;
	S32 i, iCount = 0;
	ContactMissionOffer** eaOfferList = NULL;
	devassert(pContactDef);

	if (pPlayerEnt == NULL || pContactDef == NULL)
		return 0;

	contact_GetMissionOfferList(pContactDef, pPlayerEnt, &eaOfferList);
	if(eaSize(&eaOfferList) <= 0) {
		eaDestroy(&eaOfferList);
		return 0;
	}

	// Get player mission info
	pMissionInfo = mission_GetInfoFromPlayer(pPlayerEnt);

	// Iterate all mission offers
	for (i = 0; i < eaSize(&eaOfferList); i++)
	{
		ContactMissionOffer* pOffer = eaOfferList[i];
		// See if this offer has any greeting dialogs
		if (eaSize(&pOffer->greetingDialog) > 0)
		{
			// Get the mission definition
			MissionDef *pMissionDef = GET_REF(pOffer->missionDef);
			// Get the player mission
			Mission* pPlayerMission = mission_GetMissionFromDef(pMissionInfo, pMissionDef);

			if(pPlayerMission && pPlayerMission->state == MissionState_InProgress) // Mission is in progress
			{
				if (peaGreetingDialogBlocks)
				{
					// Push all greeting dialogs
					eaPushEArray(peaGreetingDialogBlocks, &pOffer->greetingDialog);
				}
				++iCount;
			}
		}
	}

	if(eaOfferList)
		eaDestroy(&eaOfferList);

	return iCount;
}

// Checks whether a Contact has regular Stores
static bool contactdef_HasNormalStores(Entity* pPlayerEnt, ContactDef *pDef)
{
	int i;
	PERFINFO_AUTO_START_FUNC();

	for (i=eaSize(&pDef->stores)-1; i>=0; --i){
		StoreDef *pStoreDef = GET_REF(pDef->stores[i]->ref);

		if (pStoreDef && pStoreDef->pExprRequires)
		{
			MultiVal answer = {0};
			store_BuyContextSetup(pPlayerEnt, NULL);
			exprEvaluate(pStoreDef->pExprRequires, store_GetBuyContext(), &answer);
			if(answer.type == MULTI_INT)
			{
				if (!QuickGetInt(&answer))
				{
					continue;
				}
			}
		}

		// TODO: Change this if costume stores get their own UI; for now, let them be treated just like normal stores
		if (pStoreDef && (pStoreDef->eContents == Store_All || pStoreDef->eContents == Store_Costumes || pStoreDef->eContents == Store_Sellable_Items)){
			PERFINFO_AUTO_STOP();
			return true;
		}
	}

	PERFINFO_AUTO_STOP();
	return false;
}

// Checks whether a Contact has Crafting Recipe Stores
static bool contactdef_HasCraftingRecipeStores(Entity* pPlayerEnt, ContactDef *pDef)
{
	int i;
	PERFINFO_AUTO_START_FUNC();

	for (i=eaSize(&pDef->stores)-1; i>=0; --i){
		StoreDef *pStoreDef = GET_REF(pDef->stores[i]->ref);

		if (pStoreDef && pStoreDef->pExprRequires)
		{
			MultiVal answer = {0};
			store_BuyContextSetup(pPlayerEnt, NULL);
			exprEvaluate(pStoreDef->pExprRequires, store_GetBuyContext(), &answer);
			if(answer.type == MULTI_INT)
			{
				if (!QuickGetInt(&answer))
				{
					continue;
				}
			}
		}

		if (pStoreDef && pStoreDef->eContents == Store_Recipes){
			PERFINFO_AUTO_STOP();
			return true;
		}
	}

	PERFINFO_AUTO_STOP();
	return false;
}

// Checks whether a Contact has Injury Stores
static bool contactdef_HasInjuryStores(Entity* pPlayerEnt, ContactDef *pDef)
{
	int i;
	PERFINFO_AUTO_START_FUNC();

	for (i=eaSize(&pDef->stores)-1; i>=0; --i){
		StoreDef *pStoreDef = GET_REF(pDef->stores[i]->ref);

		if (pStoreDef && pStoreDef->pExprRequires)
		{
			MultiVal answer = {0};
			store_BuyContextSetup(pPlayerEnt, NULL);
			exprEvaluate(pStoreDef->pExprRequires, store_GetBuyContext(), &answer);
			if(answer.type == MULTI_INT)
			{
				if (!QuickGetInt(&answer))
				{
					continue;
				}
			}
		}

		if (pStoreDef && pStoreDef->eContents == Store_Injuries){
			PERFINFO_AUTO_STOP();
			return true;
		}
	}

	PERFINFO_AUTO_STOP();
	return false;
}

// Checks whether a Contact has Store Collections
static S32 contactdef_HasStoreCollections(Entity* pPlayerEnt, ContactDef *pDef)
{
	int i,j,n=eaSize(&pDef->storeCollections);
	PERFINFO_AUTO_START_FUNC();

	for (i=0; i<n; ++i)
	{
		StoreCollection* pCollection = pDef->storeCollections[i];
		
		if(!pCollection)
			continue;

		// Check store collection condition
		if (pCollection && pCollection->pCondition)
		{
			MultiVal answer = {0};
			store_BuyContextSetup(pPlayerEnt, NULL);
			exprEvaluate(pCollection->pCondition, store_GetBuyContext(), &answer);
			if(answer.type == MULTI_INT)
			{
				if (!QuickGetInt(&answer))
				{
					continue;
				}
			}
		}

		// Check for a valid store within this collection
		for (j=eaSize(&pCollection->eaStores)-1; j>=0; --j){
			StoreDef *pStoreDef = GET_REF(pCollection->eaStores[j]->ref);

			if (pStoreDef && pStoreDef->pExprRequires)
			{
				MultiVal answer = {0};
				store_BuyContextSetup(pPlayerEnt, NULL);
				exprEvaluate(pStoreDef->pExprRequires, store_GetBuyContext(), &answer);
				if(answer.type == MULTI_INT)
				{
					if (!QuickGetInt(&answer))
					{
						continue;
					}
				}
			}

			if (pStoreDef && (pStoreDef->eContents == Store_All || pStoreDef->eContents == Store_Costumes || pStoreDef->eContents == Store_Sellable_Items)){
				PERFINFO_AUTO_STOP();
				return i;
			}
		}
	}

	PERFINFO_AUTO_STOP();
	return -1;
}

// Determine if all stores of a particular content type have the same non-none region.
// If so, return it; otherwise return WRT_None
static WorldRegionType contactdef_GetStoreRegion(Entity* pPlayerEnt, ContactDef *pContactDef, StoreContents eContents)
{
	int i;
	WorldRegionType eRegion = WRT_None;
	for(i=0; i<eaSize(&pContactDef->stores); i++)
	{
		StoreDef* pStore = GET_REF(pContactDef->stores[i]->ref);
		if(pStore && pStore->eContents == eContents && pStore->eRegion != WRT_None)
		{
			// Set region for the first time
			if(eRegion == WRT_None)
			{
				eRegion = pStore->eRegion;
			} else if(eRegion != pStore->eRegion){
				// If region already set, and this store is of a different region, then bail
				eRegion = WRT_None;
				break;
			}
		}
	}
	return eRegion;
}

// Checks whether a Contact has Power Stores
static bool contactdef_HasPowerStores(ContactDef *pDef)
{
	int i;
	for (i=eaSize(&pDef->powerStores)-1; i>=0; --i){
		PowerStoreDef *pStoreDef = GET_REF(pDef->powerStores[i]->ref);

		if (pStoreDef){
			return true;
		}
	}
	return false;
}

bool contact_CanInteract(SA_PARAM_NN_VALID ContactDef* contact, SA_PARAM_NN_VALID Entity* playerEnt)
{
	PlayerDebug* pDebug;
	bool bResult = true;

	if (!playerEnt) {
		return false;
	}

	PERFINFO_AUTO_START_FUNC();

	pDebug = entGetPlayerDebug(playerEnt, false);

	// If player has a sync dialog, block all interaction except their current dialog
	if (team_IsMember(playerEnt) && mapState_GetSyncDialogForTeam(mapState_FromEnt(playerEnt), team_GetTeamID(playerEnt)) && !interaction_IsPlayerInDialog(playerEnt)) {
		bResult = false;
	} else if (contact && contact->interactReqs && (!pDebug || !pDebug->allowAllInteractions)) {
		bResult = contact_Evaluate(playerEnt, contact->interactReqs, NULL);
	}

	PERFINFO_AUTO_STOP();

	return bResult;
}

bool contact_CanInteractRemote(SA_PARAM_NN_VALID ContactDef* contact, SA_PARAM_NN_VALID Entity* playerEnt)
{
	PlayerDebug* pDebug;
	MapState *state;

	if (!playerEnt) {
		return false;
	}
	
	pDebug = entGetPlayerDebug(playerEnt, false);
	state = mapState_FromEnt(playerEnt);

	// If player has a sync dialog, block all interaction except their current dialog
	if(team_IsMember(playerEnt) && mapState_GetSyncDialogForTeam(state, team_GetTeamID(playerEnt)) && !interaction_IsPlayerInDialog(playerEnt)) {
		return false;
	}

	if(!contact->canAccessRemotely || !contact_Evaluate(playerEnt, contact->canAccessRemotely, NULL)) {
		if(!g_ContactConfig.bIncludeMissionSearchResultContactsInRemoteContacts || !contact_ShowInSearchResults(contact))
			return false;
	}

	return true;
}

static bool contact_MissionOfferAllowsTurnIn(const ContactMissionOffer* missionOffer)
{
	if(missionOffer->allowGrantOrReturn != ContactMissionAllow_GrantAndReturn 
		&& missionOffer->allowGrantOrReturn != ContactMissionAllow_ReturnOnly)
		return false;

	return true;
}

static bool contact_MissionOfferAllowsTurnInRemotely(const ContactMissionOffer* missionOffer)
{
	if( missionOffer && (missionOffer->eRemoteFlags & ContactMissionRemoteFlag_Return) ) {
		return contact_MissionOfferAllowsTurnIn(missionOffer);
	} else {
		return false;
	}
}

bool contact_MissionCanBeOffered(ContactDef* contact, MissionDef* missionDef, ContactMissionOffer* missionOffer, Entity* playerEnt, int* nextOfferLevel)
{
	bool bCanOffer;

	PERFINFO_AUTO_START_FUNC();

	// Is the mission really offered here?
	if (missionOffer->allowGrantOrReturn != ContactMissionAllow_GrantAndReturn && missionOffer->allowGrantOrReturn != ContactMissionAllow_GrantOnly) {
		bCanOffer = false;
	} else {
		bCanOffer = missiondef_CanBeOfferedAsPrimary(playerEnt, missionDef, nextOfferLevel, NULL);
	}

	PERFINFO_AUTO_STOP();

	return bCanOffer;
}

bool contact_MissionCanBeOfferedRemotely(ContactDef* contact, MissionDef* missionDef, ContactMissionOffer* missionOffer, Entity* playerEnt, int* nextOfferLevel)
{
	PERFINFO_AUTO_START_FUNC();

	if(missionOffer && (missionOffer->eRemoteFlags & ContactMissionRemoteFlag_Grant) ) {
		 if (contact_MissionCanBeOffered(contact, missionDef, missionOffer, playerEnt, nextOfferLevel)) {
			 PERFINFO_AUTO_STOP();
			 return true;
		 }
	}

	PERFINFO_AUTO_STOP();
	return false;
}

static bool contact_MissionCanBeOfferedAsFlashback(ContactDef* contact, MissionDef* missionDef, ContactMissionOffer* missionOffer, Entity* playerEnt)
{
	PERFINFO_AUTO_START_FUNC();

	if(missionOffer->allowGrantOrReturn == ContactMissionAllow_FlashbackGrant){
		MissionInfo *pInfo = SAFE_MEMBER2(playerEnt, pPlayer, missionInfo);
		if (missiondef_CanBeOfferedAtAll(playerEnt, missionDef, NULL, NULL, NULL) && pInfo && mission_GetCompletedMissionByDef(pInfo, missionDef)){
			PERFINFO_AUTO_STOP();
			return true;
		}
	}

	PERFINFO_AUTO_STOP();
	return false;
}

// This returns true if the offer is of type ContactMissionAllow_ReplayGrant and the mission can be offered as a secondary, already compelted mission.
// This is not the same as a flashback mission which utilizes additional logic.
static bool contact_MissionCanBeOfferedAsReplay(ContactDef* contact, MissionDef* missionDef, ContactMissionOffer* missionOffer, Entity* playerEnt, MissionOfferStatus* peOfferStatus)
{
	PERFINFO_AUTO_START_FUNC();

	if (missionOffer->allowGrantOrReturn == ContactMissionAllow_ReplayGrant){
		MissionInfo *pInfo = SAFE_MEMBER2(playerEnt, pPlayer, missionInfo);
		MissionCreditType eCredit = MissionCreditType_Primary;
		if (missiondef_CanBeOfferedAtAll(playerEnt, missionDef, NULL, peOfferStatus, &eCredit) && pInfo && mission_GetCompletedMissionByDef(pInfo, missionDef)){
			if(eCredit == MissionCreditType_AlreadyCompleted){
				PERFINFO_AUTO_STOP();
				return true;
			}
		}
	}
	PERFINFO_AUTO_STOP();
	return false;
}

static bool contact_ItemCanOfferMission(SA_PARAM_NN_VALID ItemDef* itemDef, SA_PARAM_NN_VALID MissionDef* missionDef, SA_PARAM_NN_VALID Entity* playerEnt, SA_PARAM_OP_VALID const char** ppErrMesgKey)
{
	MissionInfo* info;
	MissionOfferStatus status;

	PERFINFO_AUTO_START_FUNC();

	info = mission_GetInfoFromPlayer(playerEnt);

	// Mission can only be offered if this is a MissionGrant item for this mission and the player has it
	if(!item_IsMissionGrant(itemDef) || !item_CountOwned(playerEnt, itemDef))
	{
		if(ppErrMesgKey)
			*ppErrMesgKey = "Item.MissionGrant.InternalError";
		PERFINFO_AUTO_STOP();
		return false;
	}

	if(missionDef != GET_REF(itemDef->hMission))
	{
		if(ppErrMesgKey)
			*ppErrMesgKey = "Item.MissionGrant.InternalError";
		PERFINFO_AUTO_STOP();
		return false;
	}

	if (missiondef_CanBeOfferedAsPrimary(playerEnt, missionDef, NULL, &status))
	{
		PERFINFO_AUTO_STOP();
		return true;
	}
	else
	{
		switch (status){
		xcase MissionOfferStatus_HasMission:
			// No error
		xcase MissionOfferStatus_HasCompletedMission:
			if(ppErrMesgKey) *ppErrMesgKey = "Item.MissionGrant.AlreadyComplete";
		xcase MissionOfferStatus_TooLowLevel:
			if(ppErrMesgKey) *ppErrMesgKey = "Item.MissionGrant.LowLevel";
		xcase MissionOfferStatus_FailsRequirements:
			if(ppErrMesgKey) *ppErrMesgKey = "Item.MissionGrant.FailedRequirements";
		xcase MissionOfferStatus_InvalidAllegiance:
			if(ppErrMesgKey) *ppErrMesgKey = "Item.MissionGrant.InvalidAllegiance";
		}
		PERFINFO_AUTO_STOP();
		return false;
	}
}

static ContactIndicator contact_GetSpecialDialogIndicator(SpecialDialogBlock *pDialog)
{
	switch (pDialog->eIndicator){
		xcase SpecialDialogIndicator_Unimportant:
			return ContactIndicator_NoInfo;
		xcase SpecialDialogIndicator_Info:
			{
				if (gConf.bLowImportanceInfoDialogs)
					return ContactIndicator_LowImportanceInfo;
				else
					return ContactIndicator_HasInfoDialog;
			}
		xcase SpecialDialogIndicator_Important:
			return ContactIndicator_HasImportantDialog;
		xcase SpecialDialogIndicator_Goto:
			return ContactIndicator_HasGoto;
	}
	return ContactIndicator_NoInfo;
}

#define CONTACT_INDICATOR_IS_MULTIPLE_CANDIDATE(eOption)\
	(((eOption) >= ContactIndicator_HasInfoDialog && (eOption) < ContactIndicator_Multiple) || (eOption) >= ContactIndicator_FirstDataDefined)


static ContactIndicator contact_GetBestIndicatorState(ContactIndicator eCurState, ContactIndicator eNewState)
{
	// If we allow combined indicators, and both cur and new are of that category, then return multiple
	if (gConf.bUseCombinedContactIndicators &&
		CONTACT_INDICATOR_IS_MULTIPLE_CANDIDATE(eCurState) && 
		CONTACT_INDICATOR_IS_MULTIPLE_CANDIDATE(eNewState))
	{
		return ContactIndicator_Multiple;
	}
	
	// Mission types have highest priorty beyond that. We need to do these special checks since our data-defined enums will
	//   come in with higher values than the mission types

	// If new state is mission type
	if (eNewState>=ContactIndicator_MissionFlashbackAvailable && eNewState<=ContactIndicator_MissionCompletedRepeatable)
	{
		return MAX(eCurState, eNewState);
	}

	// If cur state is mission type, but new state is not
	if (eCurState>=ContactIndicator_MissionFlashbackAvailable && eCurState<=ContactIndicator_MissionCompletedRepeatable)
	{
		return(eCurState);
	}

	// Multiple is highest priority of what's left
	if (eCurState==ContactIndicator_Multiple || eNewState==ContactIndicator_Multiple)
	{
		return(ContactIndicator_Multiple);
	}

	// Otherwise, use the enum order as the priority. This places all data-defined enums as higher than
	//  the static ones
	
	return MAX(eCurState, eNewState);
}

static ContactIndicator contact_GetIndicatorState(SA_PARAM_NN_VALID ContactDef* contact, SA_PARAM_NN_VALID Entity* playerEnt, int* nextOfferLevelOut)
{
	MissionInfo* missionInfo;
	ContactIndicator state = ContactIndicator_NoInfo;
	ContactMissionOffer** eaOfferList = NULL;
	SpecialDialogBlock** eaSpecialDialogs = NULL;
	int playerLevel;
	int nextOfferLevel = -1;
	int i, j;

	if (!contact || !playerEnt) {
		return ContactIndicator_NoInfo;
	}

	PERFINFO_AUTO_START_FUNC();

	// If the ContactDef has an indicator override, then use that
	if (contact->eIndicatorOverride != ContactIndicator_NoInfo)
	{
		PERFINFO_AUTO_STOP();
		return contact->eIndicatorOverride;
	}

	missionInfo = mission_GetInfoFromPlayer(playerEnt);
	playerLevel = entity_GetSavedExpLevel(playerEnt);

	PERFINFO_AUTO_START("CheckOffers", 1);

	contact_GetMissionOfferList(contact, playerEnt, &eaOfferList);

	// Check each Mission Offer to determine the highest Indicator State
	for (i = 0; i < eaSize(&eaOfferList); i++)
	{
		ContactMissionOffer* missionOffer = eaOfferList[i];
		MissionDef* missionDef = GET_REF(missionOffer->missionDef);
		Mission* mission;

		if (missionOffer->pchSubMissionName){
			missionDef = missiondef_ChildDefFromName(missionDef, missionOffer->pchSubMissionName);
		}

		// Does the player have this mission?
		if (missionDef && missionInfo && (mission = mission_FindMissionFromRefString(missionInfo, missionDef->pchRefString)))
		{
			if (mission->state == MissionState_Succeeded && contact_MissionOfferAllowsTurnIn(missionOffer)){
				// If the Mission can be returned, show the Complete indicator
				if (g_ContactConfig.bUseDifferentIndicatorForRepeatableMissionTurnIns && missionDef->repeatable)
				{
					state = contact_GetBestIndicatorState(state, ContactIndicator_MissionCompletedRepeatable);
				}
				else
				{
					state = contact_GetBestIndicatorState(state, ContactIndicator_MissionCompleted);
				}											
			} else if (mission->state == MissionState_InProgress && missionOffer->allowGrantOrReturn == ContactMissionAllow_SubMissionComplete) {
				// For sub-mission turn-in, show the Complete indicator
				// If the Mission can be returned, show the Complete indicator
				if (g_ContactConfig.bUseDifferentIndicatorForRepeatableMissionTurnIns && missionDef->repeatable)
				{
					state = contact_GetBestIndicatorState(state, ContactIndicator_MissionCompletedRepeatable);
				}
				else
				{
					state = contact_GetBestIndicatorState(state, ContactIndicator_MissionCompleted);
				}
			
			} else if(mission->state == MissionState_InProgress && contact_MissionOfferAllowsTurnIn(missionOffer)){
				// Show In Progress indicator at the turn-in contact
				state = contact_GetBestIndicatorState(state, ContactIndicator_MissionInProgress);
			
			} else if (mission->state == MissionState_Failed && contact_MissionCanBeOffered(contact, missionDef, missionOffer, playerEnt, NULL)) {
				// Can restart the Mission; show "Available" indicator
				if(missionDef->repeatable){
					state = contact_GetBestIndicatorState(state, ContactIndicator_MissionRepeatableAvailable);
				}else{
					state = contact_GetBestIndicatorState(state, ContactIndicator_MissionAvailable);
				}
			}
		}
		else if (missionDef)
		{
			// Player does not have this mission; can it be offered?
			if (missionOffer->allowGrantOrReturn == ContactMissionAllow_FlashbackGrant){
				if (state < ContactIndicator_MissionFlashbackAvailable && contact_MissionCanBeOfferedAsFlashback(contact, missionDef, missionOffer, playerEnt)){
					state = contact_GetBestIndicatorState(state, ContactIndicator_MissionFlashbackAvailable);
				}
			} else {
				// Ignore mission offers that are too low-level for the player.  The player can still get them, but they
				// don't affect the contact indicator
				if(missionDef->levelDef.eLevelType != MissionLevelType_PlayerLevel && missiondef_GetMaxRewardLevel(entGetPartitionIdx(playerEnt), missionDef, playerLevel) < playerLevel)
					continue;

				if (!missionDef->repeatable && state < ContactIndicator_MissionAvailable && contact_MissionCanBeOffered(contact, missionDef, missionOffer, playerEnt, &nextOfferLevel)){
					state = contact_GetBestIndicatorState(state, ContactIndicator_MissionAvailable);
				} else if (missionDef->repeatable && state < ContactIndicator_MissionRepeatableAvailable && contact_MissionCanBeOffered(contact, missionDef, missionOffer, playerEnt, &nextOfferLevel)){
					state = contact_GetBestIndicatorState(state, ContactIndicator_MissionRepeatableAvailable);
				}
			}
		}

		// If the state is Mission Completed, we can't get higher, so early exit
		if (state == ContactIndicator_MissionCompleted || state == ContactIndicator_MissionCompletedRepeatable){
			break;
		}
	}

	if(nextOfferLevel != -1){
		state = contact_GetBestIndicatorState(state, ContactIndicator_PlayerTooLow);
	}

	if (eaOfferList) {
		eaDestroy(&eaOfferList);
	}

	PERFINFO_AUTO_STOP(); // CheckOffers

	PERFINFO_AUTO_START("CheckSpecialDialogs", 1);

	contact_GetSpecialDialogs(contact, &eaSpecialDialogs);

	// Check if the contact has any Special Dialog for the player
	for (i = 0; i < eaSize(&eaSpecialDialogs); i++){
		SpecialDialogBlock *pSpecialDialog = eaSpecialDialogs[i];
		ContactIndicator eDialogIndicator = contact_GetSpecialDialogIndicator(pSpecialDialog);
		if (state < eDialogIndicator){
			// check condition at root of special dialog
			if(pSpecialDialog->bUsesLocalCondExpression)
			{
				if(!pSpecialDialog->pCondition || contact_Evaluate(playerEnt, pSpecialDialog->pCondition, NULL)) {
					// make sure there is a dialog to display within the special dialog
					for(j = eaSize(&pSpecialDialog->dialogBlock)-1; j>=0; j--)
					{
						if (!pSpecialDialog->dialogBlock[j]->condition || contact_Evaluate(playerEnt, pSpecialDialog->dialogBlock[j]->condition, NULL)){
							state = contact_GetBestIndicatorState(state, eDialogIndicator);
							break;
						}
					}
				}
			}
			else
			{
				if(eaSize(&pSpecialDialog->dialogBlock) > 0) {
					if (!pSpecialDialog->dialogBlock[0]->condition || contact_Evaluate(playerEnt, pSpecialDialog->dialogBlock[0]->condition, NULL)){
						state = contact_GetBestIndicatorState(state, eDialogIndicator);
					}
				}
			}
		}
	}

	if (eaSpecialDialogs) {
		eaDestroy(&eaSpecialDialogs);
	}

	PERFINFO_AUTO_STOP();

	PERFINFO_AUTO_START("CheckOther", 1);

	if (contact_IsMarket(contact)){
		state = contact_GetBestIndicatorState(state, ContactIndicator_Market);
	}

	if (contact_IsAuctionBroker(contact))
	{
		state = contact_GetBestIndicatorState(state, ContactIndicator_AuctionBroker);
	}
	if (contact_IsUGCSearchAgent(contact))
	{
		state = contact_GetBestIndicatorState(state, ContactIndicator_UGCSearchAgent);
	}
	if (contact_IsImageMenu(contact))
	{
		state = contact_GetBestIndicatorState(state, ContactIndicator_ImageMenu);
	}
	if (eaSize(&contact->stores) || eaSize(&contact->storeCollections)){
		state = contact_GetBestIndicatorState(state, ContactIndicator_Vendor);
	}

	if (contactdef_HasInjuryStores(playerEnt, contact)){
		WorldRegionType eRegion = contactdef_GetStoreRegion(playerEnt, contact, Store_Injuries);
		if(eRegion == WRT_Ground) {
			state = contact_GetBestIndicatorState(state, ContactIndicator_InjuryHealer_Ground);
		} else if (eRegion == WRT_Space) {
			state = contact_GetBestIndicatorState(state, ContactIndicator_InjuryHealer_Space);
		} else {
			state = contact_GetBestIndicatorState(state, ContactIndicator_InjuryHealer);
		}
	}

	if(contact_IsTailor(contact) || contact_IsStarshipTailor(contact) || contact_IsWeaponTailor(contact)){
		state = contact_GetBestIndicatorState(state, ContactIndicator_Tailor);
	}

	if (contact_IsBank(contact) || contact_IsGuildBank(contact) || contact_IsSharedBank(contact)){
		state = contact_GetBestIndicatorState(state, ContactIndicator_Bank);
	}

	if (contact_IsStarshipChooser(contact)){
		state = contact_GetBestIndicatorState(state, ContactIndicator_StarshipChooser);
	}

	if (contact_IsMinigame(contact)){
		state = contact_GetBestIndicatorState(state, ContactIndicator_Minigame);
	}

	if (contact_IsItemAssignmentGiver(contact)){
		state = contact_GetBestIndicatorState(state, ContactIndicator_ItemAssignments);
	}

	if (contact_IsNemesis(contact)){
		state = contact_GetBestIndicatorState(state, ContactIndicator_Nemesis);
	}

	if (contact_IsPowersTrainer(contact) || eaSize(&contact->powerStores)){
		state = contact_GetBestIndicatorState(state, ContactIndicator_PowerTrainer);
	}

	if (contact_IsGuild(contact)){
		state = contact_GetBestIndicatorState(state, ContactIndicator_SuperGroup);
	}

	if (contact_IsReplayMissionGiver(contact)) {
		state = contact_GetBestIndicatorState(state, ContactIndicator_MissionRepeatableAvailable);
	}

	if (contact_IsZStore(contact)) {
		state = contact_GetBestIndicatorState(state, ContactIndicator_ZStore);
	}

	PERFINFO_AUTO_STOP();

	if (nextOfferLevelOut) {
		(*nextOfferLevelOut) = nextOfferLevel;
	}
	
	PERFINFO_AUTO_STOP_FUNC();

	return state;
}

static bool contact_CanOfferMissions(SA_PARAM_NN_VALID ContactDef* pContactDef, Entity *pPlayerEnt, bool bIgnoreLowLevel)
{
	ContactMissionOffer** eaOfferList = NULL;
	int i, n;
	int iLevel = entity_GetSavedExpLevel(pPlayerEnt);

	contact_GetMissionOfferList(pContactDef, pPlayerEnt, &eaOfferList);

	n = eaSize(&eaOfferList);

	for (i = 0; i < n; i++)
	{
		ContactMissionOffer* pMissionOffer = eaOfferList[i];
		if (pMissionOffer){
			MissionDef* pMissionDef = GET_REF(pMissionOffer->missionDef);
			if (pMissionDef && (!bIgnoreLowLevel || missiondef_GetMaxRewardLevel(entGetPartitionIdx(pPlayerEnt), pMissionDef, iLevel) >= iLevel) 
				&& contact_MissionCanBeOffered(pContactDef, pMissionDef, pMissionOffer, pPlayerEnt, NULL)){
				eaDestroy(&eaOfferList);
				return true;
			}
		}
	}

	if(eaOfferList)
		eaDestroy(&eaOfferList);

	return false;
}

static ContactDialogOption * contact_CreateMissionDialogOptions(SA_PARAM_NN_VALID ContactDef* pContactDef, 
																SA_PARAM_NN_VALID ContactDialogOption*** peaOptionsToFill, 
																SA_PARAM_NN_VALID Entity* pPlayerEnt, 
																int iPlayerLevel,
																SA_PARAM_NN_VALID MissionDef*** offerMissionDefs, 
																SA_PARAM_NN_VALID MissionDef*** offerFlashbackMissionDefs, 
																SA_PARAM_NN_VALID MissionDef*** inProgMissionDefs, 
																SA_PARAM_NN_VALID MissionDef*** failedMissionDefs, 
																SA_PARAM_NN_VALID MissionDef*** completeMissionDefs, 
																SA_PARAM_NN_VALID MissionDef*** subMissionDefs, 
																SA_PARAM_NN_VALID MissionDef*** onCooldownMissionDefs, 
																SA_PARAM_NN_VALID MissionDef*** offerReplayMissionDefs, 
																WorldVariable** eaMapVars, 
																bool bRemoteInteract, 
																bool bStopAfterFirstMission)
{
	S32 i;
	ContactDialogOption *pNewOption = NULL;
	char* estrBuf = NULL;
	S32 iCurrentOptionIndex;

	PERFINFO_AUTO_START_FUNC();

	estrStackCreate(&estrBuf);

	for(i=0; i<eaSize(completeMissionDefs); i++){
		MissionDef *pMissionDef = (*completeMissionDefs)[i];
		ContactMissionOffer *pContactMissionOffer = pContactDef && pMissionDef ? contact_GetMissionOffer(pContactDef, pPlayerEnt, pMissionDef) : NULL;
		Message *pTurnInMessage = pContactMissionOffer ? GET_REF(pContactMissionOffer->turnInStringMesg.hMessage) : NULL;
		if (pTurnInMessage == NULL)
			pTurnInMessage = GET_REF(s_ContactMsgs.hCompletedMissionMsg);

		iCurrentOptionIndex = eaSize(peaOptionsToFill);
		estrPrintf(&estrBuf, "OptionsList.CompleteMission.%s_%d", pMissionDef ? pMissionDef->pchRefString : "", iCurrentOptionIndex);

		pNewOption = contactdialog_CreateMissionDialogOption(pPlayerEnt, iPlayerLevel, pMissionDef, peaOptionsToFill, estrBuf, pTurnInMessage, ContactDialogState_ViewCompleteMission, 0, eaMapVars);

		if (bStopAfterFirstMission)
		{
			estrDestroy(&estrBuf);
			PERFINFO_AUTO_STOP();
			return pNewOption;
		}
	}

	for(i=0; i<eaSize(failedMissionDefs); i++){
		MissionDef* pMissionDef = (*failedMissionDefs)[i];
		iCurrentOptionIndex = eaSize(peaOptionsToFill);
		estrPrintf(&estrBuf, "OptionsList.FailedMission.%s_%d", pMissionDef->pchRefString, iCurrentOptionIndex);
		pNewOption = contactdialog_CreateMissionDialogOption(pPlayerEnt, iPlayerLevel, pMissionDef, peaOptionsToFill, estrBuf, GET_REF(s_ContactMsgs.hFailedMissionMsg), ContactDialogState_ViewFailedMission, 0, eaMapVars);
		
		if (bStopAfterFirstMission)
		{
			estrDestroy(&estrBuf);
			PERFINFO_AUTO_STOP();
			return pNewOption;
		}
	}

	if (bStopAfterFirstMission || !gConf.bDoNotShowMissionGrantDialogsForContact)
	{
		for(i=0; i<eaSize(offerMissionDefs); i++){
			// TODO - Different message/appearance if the mission is low level
			MissionDef* pMissionDef = (*offerMissionDefs)[i];
			iCurrentOptionIndex = eaSize(peaOptionsToFill);
			estrPrintf(&estrBuf, "OptionsList.MissionOffer.%s_%d", pMissionDef->pchRefString, iCurrentOptionIndex);
			pNewOption = contactdialog_CreateMissionDialogOption(pPlayerEnt, iPlayerLevel, pMissionDef, peaOptionsToFill, estrBuf, GET_REF(s_ContactMsgs.hOfferedMissionMsg), ContactDialogState_ViewOfferedMission, 0, eaMapVars);

			if (bStopAfterFirstMission)
			{
				estrDestroy(&estrBuf);
				PERFINFO_AUTO_STOP();
				return pNewOption;
			}
		}
	}
	for(i=0; i<eaSize(offerFlashbackMissionDefs); i++){
		MissionDef* pMissionDef = (*offerFlashbackMissionDefs)[i];
		iCurrentOptionIndex = eaSize(peaOptionsToFill);
		estrPrintf(&estrBuf, "OptionsList.FlashbackOffer.%s_%d", pMissionDef->pchRefString, iCurrentOptionIndex);
		pNewOption = contactdialog_CreateMissionDialogOption(pPlayerEnt, iPlayerLevel, pMissionDef, peaOptionsToFill, estrBuf, GET_REF(s_ContactMsgs.hOfferedFlashbackMsg), ContactDialogState_ViewOfferedMission, 0, eaMapVars);
		if (pNewOption && pNewOption->pData){
			if (!pNewOption->pData->pMissionOfferParams){
				pNewOption->pData->pMissionOfferParams = StructCreate(parse_MissionOfferParams);
			}
			pNewOption->pData->pMissionOfferParams->eCreditType = MissionCreditType_Flashback;
		}

		if (bStopAfterFirstMission)
		{
			estrDestroy(&estrBuf);
			PERFINFO_AUTO_STOP();
			return pNewOption;
		}
	}
	if (bStopAfterFirstMission || !gConf.bDoNotShowInProgressMissionsForContact)
	{
		for(i=0; i<eaSize(inProgMissionDefs); i++){
			MissionDef* pMissionDef = (*inProgMissionDefs)[i];
			iCurrentOptionIndex = eaSize(peaOptionsToFill);
			estrPrintf(&estrBuf, "OptionsList.InProgMission.%s_%d", pMissionDef->pchRefString, iCurrentOptionIndex);
			pNewOption = contactdialog_CreateMissionDialogOption(pPlayerEnt, iPlayerLevel, pMissionDef, peaOptionsToFill, estrBuf, GET_REF(s_ContactMsgs.hInProgMissionMsg), ContactDialogState_ViewInProgressMission, 0, eaMapVars);

			if (bStopAfterFirstMission)
			{
				estrDestroy(&estrBuf);
				PERFINFO_AUTO_STOP();
				return pNewOption;
			}
		}
	}
	for(i=0; i<eaSize(subMissionDefs); i++){
		MissionDef* pMissionDef = (*subMissionDefs)[i];
		iCurrentOptionIndex = eaSize(peaOptionsToFill);
		estrPrintf(&estrBuf, "OptionsList.SubMission.%s_%d", pMissionDef->pchRefString, iCurrentOptionIndex);
		pNewOption = contactdialog_CreateMissionDialogOption(pPlayerEnt, iPlayerLevel, pMissionDef, peaOptionsToFill, estrBuf, GET_REF(s_ContactMsgs.hViewSubMissionMsg), ContactDialogState_ViewSubMission, 0, eaMapVars);

		if (bStopAfterFirstMission)
		{
			estrDestroy(&estrBuf);
			PERFINFO_AUTO_STOP();
			return pNewOption;
		}
	}
	for(i=0; i<eaSize(offerReplayMissionDefs); i++){
		MissionDef* pMissionDef = (*offerReplayMissionDefs)[i];
		iCurrentOptionIndex = eaSize(peaOptionsToFill);
		estrPrintf(&estrBuf, "OptionsList.ReplayMissionOffer.%s_%d", pMissionDef->pchRefString, iCurrentOptionIndex);
		pNewOption = contactdialog_CreateMissionDialogOption(pPlayerEnt, iPlayerLevel, pMissionDef, peaOptionsToFill, estrBuf, GET_REF(s_ContactMsgs.hOfferedMissionMsg), ContactDialogState_ViewOfferedMission, 0, eaMapVars);
		if (pNewOption && pNewOption->pData){
			if (!pNewOption->pData->pMissionOfferParams){
				pNewOption->pData->pMissionOfferParams = StructCreate(parse_MissionOfferParams);
			}
			pNewOption->pData->pMissionOfferParams->eCreditType = MissionCreditType_AlreadyCompleted;
		}

		if (bStopAfterFirstMission)
		{
			estrDestroy(&estrBuf);
			PERFINFO_AUTO_STOP();
			return pNewOption;
		}
	}
	if(gConf.bShowContactOffersOnCooldown) {
		for(i=0; i<eaSize(onCooldownMissionDefs); i++){
			MissionDef* pMissionDef = (*onCooldownMissionDefs)[i];
			MissionInfo* pInfo = mission_GetInfoFromPlayer(pPlayerEnt);
			iCurrentOptionIndex = eaSize(peaOptionsToFill);
			estrPrintf(&estrBuf, "OptionsList.SubMission.%s_%d", pMissionDef->pchRefString, iCurrentOptionIndex);
			// Setup option
			pNewOption = contactdialog_CreateMissionDialogOption(pPlayerEnt, iPlayerLevel, pMissionDef, peaOptionsToFill, estrBuf, GET_REF(s_ContactMsgs.hOfferedMissionMsg), ContactDialogState_ViewOfferedMission, 0, eaMapVars);
			pNewOption->bCannotChoose = true;

			// Setup cooldown timer
			if(pNewOption && pInfo) {
				CompletedMission *pCompletedSecondary;
				CompletedMission *pCompletedMission;
				U32 uCompletedTime = 0;
				U32 uStartTime = 0;

				// Get the last start/completed times for this mission
				if (pCompletedSecondary = eaIndexedGetUsingString(&pInfo->eaRecentSecondaryMissions, pMissionDef->name)) {
					U32 uExpireTimer = missiondef_GetSecondaryCreditLockoutTime(pMissionDef);
					uCompletedTime = pCompletedSecondary->completedTime;
					uStartTime = pCompletedSecondary->startTime;
					pNewOption->uCooldownExpireTime = uCompletedTime + uExpireTimer;
				} else if (pCompletedMission = mission_GetCompletedMissionByDef(pInfo, pMissionDef)){
					uCompletedTime = completedmission_GetLastCompletedTime(pCompletedMission);
					uStartTime = completedmission_GetLastStartedTime(pCompletedMission);
				} else {
					MissionCooldown* pCooldown = eaIndexedGetUsingString(&pInfo->eaMissionCooldowns, pMissionDef->name);
					if(pCooldown) {
						uCompletedTime = pCooldown->completedTime;
						uStartTime = pCooldown->startTime;
					}
				}

				// Find the time when the last cooldown timer will expire
				if(pMissionDef->fRepeatCooldownHours > 0) {	
					// The time when the completed cooldown timer will expire (in seconds)
					U32 uCompletedCooldownExpire = uCompletedTime + round(pMissionDef->fRepeatCooldownHours*3600.f);
					if(uCompletedCooldownExpire > timeSecondsSince2000()) {
						pNewOption->uCooldownExpireTime = uCompletedCooldownExpire;
					}
				} 
				if(pMissionDef->fRepeatCooldownHoursFromStart > 0) {
					// The time when the start cooldown timer will expire (in seconds)
					U32 uStartCooldownExpire = uStartTime + round(pMissionDef->fRepeatCooldownHoursFromStart*3600.f);
					if(uStartCooldownExpire > timeSecondsSince2000() && uStartCooldownExpire > pNewOption->uCooldownExpireTime) {
						pNewOption->uCooldownExpireTime = uStartCooldownExpire;
					}
				}

				// Update the option data with the cooldown timer
				if(pNewOption->pData && pNewOption->uCooldownExpireTime) {
					pNewOption->pData->uCooldownExpireTime = pNewOption->uCooldownExpireTime;
				}

				if (bStopAfterFirstMission)
				{
					estrDestroy(&estrBuf);
					PERFINFO_AUTO_STOP();
					return pNewOption;
				}
			}
		}
	}

	estrDestroy(&estrBuf);
	PERFINFO_AUTO_STOP();
	return pNewOption;
}

static void contact_GetAvailableMissions(SA_PARAM_NN_VALID ContactDef* contact, SA_PARAM_NN_VALID Entity* playerEnt, SA_PARAM_OP_VALID MissionDef*** offerMissionDefs, SA_PARAM_OP_VALID MissionDef*** offerFlashbackMissionDefs, SA_PARAM_OP_VALID MissionDef*** inProgMissionDefs, SA_PARAM_OP_VALID MissionDef*** failedMissionDefs, SA_PARAM_OP_VALID MissionDef*** completeMissionDefs, SA_PARAM_OP_VALID MissionDef*** subMissionDefs, SA_PARAM_OP_VALID MissionDef*** onCooldownMissionDefs, SA_PARAM_OP_VALID MissionDef*** offerReplayMissionDefs, bool bRemoteInteract, SA_PARAM_OP_VALID ContactMissionOffer *pMissionOfferToProcess)
{
	ContactMissionOffer** eaOfferList = NULL;
	int i, n;
	MissionInfo* missionInfo = mission_GetInfoFromPlayer(playerEnt);

	PERFINFO_AUTO_START_FUNC();

	contact_GetMissionOfferList(contact, playerEnt, &eaOfferList);

	n = eaSize(&eaOfferList);

	for (i = 0; i < n; i++)
	{
		ContactMissionOffer* missionOffer = eaOfferList[i];
		MissionDef* missionDef = GET_REF(missionOffer->missionDef);
		if (missionDef && (pMissionOfferToProcess == NULL || pMissionOfferToProcess == missionOffer))
		{
			Mission* activeMission = mission_GetMissionFromDef(missionInfo, missionDef);
			bool bCanBeOffered;
			bool bCanBeReturned;
			bool bContactOffersMission;
			bool bCanBeOfferedAsFlashback = false;
			bool bCanBeOfferedAsReplay = false;
			MissionOfferStatus eOfferStatus = MissionOfferStatus_OK;

			if(bRemoteInteract) {
				bCanBeOffered = contact_MissionCanBeOfferedRemotely(contact, missionDef, missionOffer, playerEnt, NULL);
				bCanBeReturned = contact_MissionOfferAllowsTurnInRemotely(missionOffer);

			} else {
				bCanBeOffered = contact_MissionCanBeOffered(contact, missionDef, missionOffer, playerEnt, NULL);
				bCanBeReturned = contact_MissionOfferAllowsTurnIn(missionOffer);
				bCanBeOfferedAsFlashback = contact_MissionCanBeOfferedAsFlashback(contact, missionDef, missionOffer, playerEnt);
				bCanBeOfferedAsReplay = contact_MissionCanBeOfferedAsReplay(contact, missionDef, missionOffer, playerEnt, &eOfferStatus);
			}
			bContactOffersMission = (missionOffer->allowGrantOrReturn == ContactMissionAllow_GrantAndReturn || missionOffer->allowGrantOrReturn == ContactMissionAllow_GrantOnly);
				// Whether the contact offers this Mission, regardless of whether or not this player can accept it

			if (!bRemoteInteract || (missionOffer->eRemoteFlags & ContactMissionRemoteFlag_Grant))
			{
				// Check to see if the mission is on a cooldown
				if(gConf.bShowContactOffersOnCooldown && !activeMission && !bCanBeOffered && !bCanBeOfferedAsFlashback && missionDef->repeatable && (missionDef->fRepeatCooldownHours || missionDef->fRepeatCooldownHoursFromStart))
				{
					missiondef_CanBeOfferedAsPrimary(playerEnt, missionDef, NULL, &eOfferStatus);
					if(onCooldownMissionDefs && eOfferStatus == MissionOfferStatus_MissionOnCooldown)
					{
						eaPush(onCooldownMissionDefs, missionDef);
					}
				} 
				else if (gConf.bShowContactOffersOnCooldown && !activeMission && !bCanBeOffered && 
						 eOfferStatus == MissionOfferStatus_SecondaryCooldown)
				{
					if (onCooldownMissionDefs)
					{
						eaPush(onCooldownMissionDefs, missionDef);
					}
				}
			}

			// If the player doesn't have the mission and it can be offered
			if(!activeMission && bCanBeOffered){
				if (offerMissionDefs)
					eaPush(offerMissionDefs, missionDef);
			} else if (!activeMission && bCanBeOfferedAsFlashback){
				if (offerFlashbackMissionDefs)
					eaPush(offerFlashbackMissionDefs, missionDef);
			} else if (!activeMission && bCanBeOfferedAsReplay){
				if (offerReplayMissionDefs)
					eaPush(offerReplayMissionDefs, missionDef);
			}

			// If the mission is in progress, or complete but can't be returned here
			if(inProgMissionDefs && activeMission && (bContactOffersMission || bCanBeReturned) && eaSize(&missionOffer->inProgressDialog) &&
					((activeMission->state == MissionState_InProgress) || (activeMission->state == MissionState_Succeeded && !bCanBeReturned) ))
				eaPush(inProgMissionDefs, missionDef);

			// If the mission has been failed and can be re-offered
			if(failedMissionDefs && activeMission && activeMission->state == MissionState_Failed)
				eaPush(failedMissionDefs, missionDef);

			// If the mission is complete and can be returned
			if(completeMissionDefs && activeMission && activeMission->state == MissionState_Succeeded && bCanBeReturned)
				eaPush(completeMissionDefs, missionDef);
		}
	}

	if(!bRemoteInteract) {
		// Do SubMissionCompletes last, so that the InProgress option for the root Mission can be disabled
		for (i = 0; i < n; i++)
		{
			ContactMissionOffer* missionOffer = eaOfferList[i];
			MissionDef* missionDef = GET_REF(missionOffer->missionDef);
			if (missionDef && missionOffer->allowGrantOrReturn == ContactMissionAllow_SubMissionComplete)
			{
				Mission* activeMission = mission_GetMissionFromDef(missionInfo, missionDef);
				Mission *subMission = NULL;
				if (activeMission && (subMission = mission_FindChildByName(activeMission, missionOffer->pchSubMissionName))){
					if (subMissionDefs && subMission->state == MissionState_InProgress){
						MissionDef* pSubMissionDef = mission_GetDef(subMission);
						
						if (pSubMissionDef)
							eaPush(subMissionDefs, pSubMissionDef);
						else
							Errorf("contact_GetAvailableMissions() found a sub-mission \"%s\" on entity %s with no def!", subMission->missionNameOrig, playerEnt->debugName);
						
						if (inProgMissionDefs)
							eaFindAndRemove(inProgMissionDefs, missionDef);
					}
				}
			}
		}
	}

	if(eaOfferList)
		eaDestroy(&eaOfferList);

	PERFINFO_AUTO_STOP();
}

static void contact_GetAvailableSpecialDialogs(SA_PARAM_NN_VALID ContactDef* contact, 
											   SA_PARAM_NN_VALID Entity* playerEnt, 
											   SA_PARAM_NN_VALID int** dialogIndexes)
{
	int i, n;
	int j = 0;
	MissionInfo* missionInfo;
	SpecialDialogBlock** eaSpecialDialogs = NULL;

	PERFINFO_AUTO_START_FUNC();

	missionInfo = mission_GetInfoFromPlayer(playerEnt);

	contact_GetSpecialDialogs(contact, &eaSpecialDialogs);

	n = eaSize(&eaSpecialDialogs);

	eaiDestroy(dialogIndexes);

	for (i = 0; i < n; i++)
	{
		SpecialDialogBlock* dialog = eaSpecialDialogs[i];
		bool expressionTrue = true;
		if(dialog->bUsesLocalCondExpression)
		{
			// Check root condition
			if(dialog->pCondition)
			{
				expressionTrue = contact_Evaluate(playerEnt, dialog->pCondition, NULL);
			}
			// Check for valid dialog block
			if(expressionTrue)
			{
				bool bDialogFound = false;
				if(dialog->dialogBlock)
				{
					for(j = eaSize(&dialog->dialogBlock)-1; j >= 0 && !bDialogFound; j--)
					{
						bDialogFound = bDialogFound || (!dialog->dialogBlock[j]->condition || contact_Evaluate(playerEnt, dialog->dialogBlock[j]->condition, NULL));
					}
				}
				expressionTrue = expressionTrue && bDialogFound;
			}
		}
		else
		{
			if(eaSize(&dialog->dialogBlock) > 0) 
			{
				if(dialog->dialogBlock[0]->condition)
				{
					expressionTrue = contact_Evaluate(playerEnt, dialog->dialogBlock[0]->condition, NULL);
				}
			}
		}

		if (expressionTrue)
		{
			eaiPush(dialogIndexes, i);
		}
	}

	if(eaSpecialDialogs)
		eaDestroy(&eaSpecialDialogs);

	PERFINFO_AUTO_STOP();
}

// Get the first mission that this contact has that the player has completed
static ContactMissionOffer* contact_FindCompletedOffer(SA_PARAM_NN_VALID ContactDef* contact, SA_PARAM_NN_VALID Entity* playerEnt)
{
	ContactMissionOffer** eaOfferList = NULL;
	int i, n;
	MissionInfo* missionInfo = mission_GetInfoFromPlayer(playerEnt);
	int nextOfferLevel = -1;

	contact_GetMissionOfferList(contact, playerEnt, &eaOfferList);

	n = eaSize(&eaOfferList);

	for (i = 0; i < n; i++)
	{
		ContactMissionOffer* missionOffer = eaOfferList[i];
		if (contact_MissionOfferAllowsTurnIn(missionOffer)){
			MissionDef* missionDef = GET_REF(missionOffer->missionDef);
			Mission* mission;

			// Does the player have this mission?
			if (missionDef && missionInfo && (mission = mission_GetMissionFromDef(missionInfo, missionDef)))
			{
				if (mission_IsComplete(mission)) {
					eaDestroy(&eaOfferList);
					return missionOffer;
				}
			}
		}
	}

	if(eaOfferList)
		eaDestroy(&eaOfferList);

	return NULL;
}

static bool contactdialog_TrySkipOptionsList(Entity *pPlayerEnt, SA_PARAM_NN_VALID ContactDef* pContactDef, SA_PARAM_NN_VALID ContactDialog* pDialog)
{
	// If there's a Mission turn-in, skip to that
	// If this is a Single Dialog contact, skip to the first option
	ContactDialogOption* pTargetOption = NULL;
	int iNumValidOptions = 0;
	int i;

	for(i=0; i<eaSize(&pDialog->eaOptions); i++){
		if (pDialog->eaOptions[i] && pDialog->eaOptions[i]->pData){

			ContactDialogOptionData* pOptionData = pDialog->eaOptions[i]->pData;
			if(pOptionData->targetState != ContactDialogState_Exit){
				// Single Dialog contacts should always just skip to the first thing from the list
				if (contact_IsSingleScreen(pContactDef) && !pTargetOption)
					pTargetOption = pDialog->eaOptions[i];
				iNumValidOptions++;
			} 
			if (!pTargetOption && ((pOptionData->targetState == ContactDialogState_ViewCompleteMission && !gConf.bDoNotSkipContactOptionsForMissionTurnIn) || pOptionData->targetState == ContactDialogState_ViewSubMission)){
				// Otherwise, skip to the first mission turn-in
				pTargetOption = pDialog->eaOptions[i];
			}
		}
	}

	if(contact_IsSingleScreen(pContactDef) && !pTargetOption){
		Errorf("Contact %s is a single screen contact, but has no dialog choices", pContactDef->name);
	}

// Disabling this temporarily pending some discussion about SingleDialog contacts
//	if(contact_IsSingleScreen(pContactDef) && iNumValidOptions > 1){
//		Errorf("Contact %s is a single screen contact, but has more than 1 valid dialog choice", pContactDef->name);
//	}

	if (pTargetOption){
		contact_InteractResponse(pPlayerEnt, pTargetOption->pchKey, NULL, 0, true);
		return true;
	}

	return false;
}


static int contactdialogoption_SortByDisplayName(const ContactDialogOption **pOptionA, const ContactDialogOption **pOptionB)
{
	if (pOptionA && *pOptionA && pOptionB && *pOptionB)
		return strcmp((*pOptionA)->pchDisplayString, (*pOptionB)->pchDisplayString);
	return 0;
}

// StructAllocString's translated text from a Dialog Block
S32 contactdialog_SetTextFromDialogBlocks(Entity *pEnt, DialogBlock ***peaDialogBlocks, char** estrResult, const char** ppchPooledSound, const char **ppchPhrasePath, ReferenceHandle *pDialogFormatterRefHandle)
{
	S32 iDialogIndex = -1;

	Entity *pNemesisEnt = player_GetPrimaryNemesis(pEnt);
	if (estrResult)
		estrClear(estrResult);
	
	if (ppchPooledSound && *ppchPooledSound)
		ppchPooledSound = NULL;

	if(ppchPhrasePath && *ppchPhrasePath)
		ppchPhrasePath = NULL;

	if (pEnt && peaDialogBlocks)
	{
		int numDialogs = eaSize(peaDialogBlocks);
		int iDialog = randInt(numDialogs);
		if (iDialog >= 0 && iDialog < numDialogs)
		{			
			DialogBlock *pDialog = (*peaDialogBlocks)[iDialog];

			iDialogIndex = iDialog;

			if(estrResult){
				WorldVariable **ppMapVars = NULL;
				eaCreate(&ppMapVars);
				mapvariable_GetAllAsWorldVarsNoCopy(entGetPartitionIdx(pEnt), &ppMapVars);
				langFormatGameDisplayMessage(entGetLanguage(pEnt), estrResult, &pDialog->displayTextMesg, STRFMT_ENTITY(pEnt), STRFMT_ENTITY_KEY("Nemesis", pNemesisEnt), STRFMT_MAPVARS(ppMapVars), STRFMT_END);
				eaDestroy(&ppMapVars);
			}
			if (pDialogFormatterRefHandle)
			{
				ContactDialogFormatterDef *pFormatterDef = GET_REF(pDialog->hDialogFormatter);
				if (pFormatterDef)
				{
					RefSystem_SetHandleFromReferent(g_hContactDialogFormatterDefDictionary, pFormatterDef, pDialogFormatterRefHandle, __FUNCTION__);
				}
				else
				{
					RefSystem_RemoveHandleWithReason(pDialogFormatterRefHandle, __FUNCTION__);
				}				
			}

			if (ppchPooledSound)
			{
				(*ppchPooledSound) = pDialog->audioName;
			}
			contact_GetPhrasePathFromDialogBlock(pDialog, ppchPhrasePath);
		}
	}

	return iDialogIndex;
}

static ContactDialogOption* contactdialog_CreateDialogOptionEx(Entity *pPlayerEnt, ContactDialogOption*** peaOptionsList, const char *pchKey, Message *pDisplayTextMsg, ContactDialogState targetState, ContactActionType action, WorldVariable** eaMapVars, ContactDialogFormatterDef *pDialogFormatter)
{
	ContactDialogOption *pOption;

	PERFINFO_AUTO_START_FUNC();
	
	pOption = StructCreate(parse_ContactDialogOption);
	pOption->pchKey = StructAllocString(pchKey);
	if (pDisplayTextMsg) {
		entFormatGameMessage(pPlayerEnt, &pOption->pchDisplayString, pDisplayTextMsg, STRFMT_ENTITY(pPlayerEnt), STRFMT_MAPVARS(eaMapVars), STRFMT_END);
	}

	// Set the dialog formatter if available
	if (pDialogFormatter)
	{
		SET_HANDLE_FROM_REFERENT(g_hContactDialogFormatterDefDictionary, pDialogFormatter, pOption->hDialogFormatter);
	}
	
	switch (targetState) {
		case ContactDialogState_ContactInfo:
		case ContactDialogState_ViewLore:
		case ContactDialogState_SpecialDialog:
			pOption->eType = ContactIndicator_HasInfoDialog;
			break;
		case ContactDialogState_ViewOfferedMission:
			pOption->eType = ContactIndicator_MissionAvailable;
			break;
		case ContactDialogState_ViewInProgressMission:
			pOption->eType = ContactIndicator_MissionInProgress;
			break;
		case ContactDialogState_ViewFailedMission:
			pOption->eType = ContactIndicator_MissionRepeatableAvailable;
			break;
		case ContactDialogState_ViewCompleteMission:
			pOption->eType = ContactIndicator_MissionCompleted;
			break;
		case ContactDialogState_ViewSubMission:
			pOption->eType = ContactIndicator_MissionInProgress;
			break;
		case ContactDialogState_Store:
		case ContactDialogState_Market:
		case ContactDialogState_StoreCollection:
		case ContactDialogState_AuctionBroker:
			pOption->eType = ContactIndicator_Vendor;
			break;
		case ContactDialogState_UGCSearchAgent:
			pOption->eType = ContactIndicator_UGCSearchAgent;
			break;
		case ContactDialogState_ImageMenu:
			pOption->eType = ContactIndicator_ImageMenu;
			break;
		case ContactDialogState_RecipeStore:
		case ContactDialogState_PowerStore:
		case ContactDialogState_Respec:
		case ContactDialogState_PowersTrainer:
			pOption->eType = ContactIndicator_PowerTrainer;
			break;
		case ContactDialogState_InjuryStore:
			pOption->eType = ContactIndicator_InjuryHealer;
			break;
		case ContactDialogState_Tailor:
		case ContactDialogState_StarshipTailor:
		case ContactDialogState_WeaponTailor:
			pOption->eType = ContactIndicator_Tailor;
			break;
		case ContactDialogState_StarshipChooser:
			pOption->eType = ContactIndicator_StarshipChooser;
			break;
		case ContactDialogState_Minigame:
			pOption->eType = ContactIndicator_Minigame;
			break;
		case ContactDialogState_ItemAssignments:
			pOption->eType = ContactIndicator_ItemAssignments;
			break;
		case ContactDialogState_NewNemesis:
		case ContactDialogState_Nemesis:
			pOption->eType = ContactIndicator_Nemesis;
			break;
		case ContactDialogState_NewGuild:
		case ContactDialogState_Guild:
			pOption->eType = ContactIndicator_SuperGroup;
			break;
		case ContactDialogState_Bank:
			pOption->eType = ContactIndicator_Bank;
			break;
		case ContactDialogState_SharedBank:
			pOption->eType = ContactIndicator_SharedBank;
			break;
		case ContactDialogState_ZStore:
			pOption->eType = ContactIndicator_ZStore;
			break;
		case ContactDialogState_GuildBank:
			pOption->eType = ContactIndicator_SuperGroupBank;
			break;
		default:
			pOption->eType = ContactIndicator_NoInfo;
			break;
	}
	
	pOption->pData = StructCreate(parse_ContactDialogOptionData);
	pOption->pData->targetState = targetState;
	pOption->pData->action = action;
	
	if (peaOptionsList)
		eaPush(peaOptionsList, pOption);

	PERFINFO_AUTO_STOP();

	return pOption;
}

#define contactdialog_CreateDialogOption(pPlayerEnt, peaOptionsList, pchKey, pDisplayTextMsg, targetState, action, eaMapVars) contactdialog_CreateDialogOptionEx(pPlayerEnt, peaOptionsList, pchKey, pDisplayTextMsg, targetState, action, eaMapVars, NULL)

static ContactDialogOption* contactdialog_CreateMissionDialogOption(Entity *pPlayerEnt, int iPlayerLevel, MissionDef *pMissionDef, ContactDialogOption*** peaOptionsList, const char *pchKey, Message *pDisplayTextMsg, ContactDialogState targetState, ContactActionType action, WorldVariable** eaMapVars)
{
	ContactDialogOption *pOption = NULL;
	MissionDef *pRootDef = pMissionDef;

	PERFINFO_AUTO_START_FUNC();

	while (pRootDef && GET_REF(pRootDef->parentDef)){
		pRootDef = GET_REF(pRootDef->parentDef);
	}

	if (pPlayerEnt && pRootDef){		
		int iPartitionIdx = entGetPartitionIdx(pPlayerEnt);
		int iLevel = missiondef_CalculateLevel(iPartitionIdx, iPlayerLevel, pRootDef);

		pOption = contactdialog_CreateDialogOption(pPlayerEnt, peaOptionsList, pchKey, NULL, targetState, action, eaMapVars);
		if (g_ContactConfig.bUseReplayableMissionOfferIndicatorForDialogOptionsWhenAvailable && 
			pOption && 
			pRootDef->repeatable)
		{
			if (g_ContactConfig.bUseReplayableMissionOfferIndicatorForDialogOptionsWhenAvailable && pOption->eType == ContactIndicator_MissionAvailable)
			{
				pOption->eType = ContactIndicator_MissionRepeatableAvailable;
			}
			else if (g_ContactConfig.bUseDifferentIndicatorForRepeatableMissionTurnIns && pOption->eType == ContactIndicator_MissionCompleted)
			{
				pOption->eType = ContactIndicator_MissionCompletedRepeatable;
			}
		}

		if (pOption && pOption->pData){
			langFormatGameMessage(entGetLanguage(pPlayerEnt), &pOption->pchDisplayString, pDisplayTextMsg, STRFMT_DISPLAYMESSAGE("MissionName", pRootDef->displayNameMsg), STRFMT_INT("MissionLevel", iLevel), STRFMT_INT("LevelDifference", iLevel - iPlayerLevel), STRFMT_INT("SuggestedTeamSize", pRootDef->iSuggestedTeamSize), STRFMT_PLAYER(pPlayerEnt), STRFMT_MAPVARS(eaMapVars), STRFMT_END);

			SET_HANDLE_FROM_REFERENT(g_MissionDictionary, pRootDef, pOption->pData->hRootMission);
			if (pRootDef != pMissionDef){
				pOption->pData->pchSubMissionName = pMissionDef->name;
			}
		}
	}

	PERFINFO_AUTO_STOP();
	return pOption;
}

static Message* contact_GetMinigamePlayButtonMessage(ContactDef* pContactDef)
{
	MinigameDef* pDef = Minigame_FindByType(pContactDef->eMinigameType);
	if (pDef)
	{
		Message* pMessage = GET_REF(pDef->displayMsgPlay.hMessage);
		if (pMessage)
		{
			return pMessage;
		}
	}
	return GET_REF(s_ContactMsgs.hMinigameMsg);
}

static void contactdialog_CreateOptionsList(Entity *pPlayerEnt, ContactDef *pContactDef, ContactDialogOption*** peaOptionsToFill, bool bOnlyShowRemoteOptions)
{
	ContactDialogOption *pNewOption = NULL;

	MissionDef** eaOfferMissionList = NULL;
	MissionDef** eaOfferFlashbackList = NULL;
	MissionDef** eaOfferReplayList = NULL;
	MissionDef** eaInProgMissionList = NULL;
	MissionDef** eaFailedMissionList = NULL;
	MissionDef** eaCompleteMissionList = NULL;
	MissionDef** eaSubMissionList = NULL;
	MissionDef** eaOnCooldownMissionList = NULL;
	SpecialDialogBlock** eaSpecialDialogs = NULL;
	WorldVariable **eaMapVars = NULL;
	int* eaDialogIndexes = NULL;
	int i,j;
	int iPartitionIdx;
	int iPlayerLevel;
	int iValidCollection;
	char* estrBuf = NULL;

	PERFINFO_AUTO_START_FUNC();
		
	iPlayerLevel = entity_GetSavedExpLevel(pPlayerEnt);
	iPartitionIdx = entGetPartitionIdx(pPlayerEnt);
	estrStackCreate(&estrBuf);

	mapvariable_GetAllAsWorldVarsNoCopy(iPartitionIdx, &eaMapVars);
	
	// The list options will appear in the order they are added (unless the UI does something special)
	// Changing the order of these things should be fine

	if(!bOnlyShowRemoteOptions) 
	{
		// Contact info
		if(pContactDef->infoDialog){
			contactdialog_CreateDialogOption(pPlayerEnt, peaOptionsToFill, "OptionsList.ContactInfo", GET_REF(s_ContactMsgs.hContactInfoMsg), ContactDialogState_ContactInfo, ContactActionType_ContactInfo, eaMapVars);
		}
		// Tailor
		if(contact_IsTailor(pContactDef)){
			contactdialog_CreateDialogOption(pPlayerEnt, peaOptionsToFill, "OptionsList.Tailor", GET_REF(s_ContactMsgs.hTailorMsg), ContactDialogState_Tailor, 0, eaMapVars);
		}
		// Starship Tailor
		if(contact_IsStarshipTailor(pContactDef)){
			char buffer[256];
			CharClassCategorySet *pSet;
			RefDictIterator iter;
			RefSystem_InitRefDictIterator(g_hCharacterClassCategorySetDict, &iter);
			while (pSet = RefSystem_GetNextReferentFromIterator(&iter))
			{
				sprintf(buffer, "OptionsList.StarshipTailor_%s", pSet->pchName);
				pNewOption = contactdialog_CreateDialogOption(pPlayerEnt, peaOptionsToFill, buffer, NULL, ContactDialogState_StarshipTailor, 0, eaMapVars);
				SET_HANDLE_FROM_REFERENT(g_hCharacterClassCategorySetDict, pSet, pNewOption->pData->hClassCategorySet);
				if (GET_REF(s_ContactMsgs.hStarshipTailorMsg))
				{
					entFormatGameMessage(pPlayerEnt, &pNewOption->pchDisplayString, GET_REF(s_ContactMsgs.hStarshipTailorMsg),
						STRFMT_ENTITY(pPlayerEnt),
						STRFMT_MAPVARS(eaMapVars),
						STRFMT_MESSAGE("ShipType", pSet ? GET_REF(pSet->hDisplayName) : NULL),
						STRFMT_END);
				}
			}
		}
		// Starship Chooser
		if(contact_IsStarshipChooser(pContactDef)){
			contactdialog_CreateDialogOption(pPlayerEnt, peaOptionsToFill, "OptionsList.StarshipChooser", GET_REF(s_ContactMsgs.hStarshipChooserMsg), ContactDialogState_StarshipChooser, 0, eaMapVars);
		}
		// Weapon Tailor
		if(contact_IsWeaponTailor(pContactDef)){
			contactdialog_CreateDialogOption(pPlayerEnt, peaOptionsToFill, "OptionsList.WeaponTailor", GET_REF(s_ContactMsgs.hWeaponTailorMsg), ContactDialogState_WeaponTailor, 0, eaMapVars);
		}
		// Minigame
		if (contact_IsMinigame(pContactDef)){
			Message* pMinigameMessage = contact_GetMinigamePlayButtonMessage(pContactDef);
			pNewOption = contactdialog_CreateDialogOption(pPlayerEnt, peaOptionsToFill, "OptionsList.Minigame", pMinigameMessage, ContactDialogState_Minigame, 0, eaMapVars);
		}
		if (contact_IsItemAssignmentGiver(pContactDef)){
			pNewOption = contactdialog_CreateDialogOption(pPlayerEnt, peaOptionsToFill, "OptionsList.ItemAssignments", GET_REF(s_ContactMsgs.hItemAssignmentMsg), ContactDialogState_ItemAssignments, 0, eaMapVars);
		}
		// Store
		if(contactdef_HasNormalStores(pPlayerEnt, pContactDef)){
			contactdialog_CreateDialogOption(pPlayerEnt, peaOptionsToFill, "OptionsList.Store", GET_REF(s_ContactMsgs.hStoreMsg), ContactDialogState_Store, 0, eaMapVars);
		}
		// Store Collections
		if((iValidCollection=contactdef_HasStoreCollections(pPlayerEnt, pContactDef))>=0){
			pNewOption = contactdialog_CreateDialogOption(pPlayerEnt, peaOptionsToFill, "OptionsList.StoreCollection", GET_REF(s_ContactMsgs.hStoreMsg), ContactDialogState_StoreCollection, 0, eaMapVars);
			pNewOption->pData->iStoreCollection = iValidCollection;
		}
		// Crafting Recipe Store
		if(contactdef_HasCraftingRecipeStores(pPlayerEnt, pContactDef)){
			contactdialog_CreateDialogOption(pPlayerEnt, peaOptionsToFill, "OptionsList.RecipeStore", GET_REF(s_ContactMsgs.hRecipeStoreMsg), ContactDialogState_RecipeStore, 0, eaMapVars);
		}
		// Power Store
		if(contactdef_HasPowerStores(pContactDef)){
			contactdialog_CreateDialogOption(pPlayerEnt, peaOptionsToFill, "OptionsList.PowerStore", GET_REF(s_ContactMsgs.hPowerStoreMsg), ContactDialogState_PowerStore, 0, eaMapVars);
		}
		// Injury Store
		if(contactdef_HasInjuryStores(pPlayerEnt, pContactDef)){
			// if all injury stores have the same non-none region, use it to create the display string
			WorldRegionType eRegion = contactdef_GetStoreRegion(pPlayerEnt, pContactDef, Store_Injuries);
			if(eRegion!=WRT_None) {
				estrPrintf(&estrBuf, "ContactDialog.InjuryStore.%s", StaticDefineIntRevLookup(WorldRegionTypeEnum, eRegion));
				contactdialog_CreateDialogOption(pPlayerEnt, peaOptionsToFill, "OptionsList.InjuryStore", RefSystem_ReferentFromString(gMessageDict, estrBuf), ContactDialogState_InjuryStore, 0, eaMapVars);
			} else {
				contactdialog_CreateDialogOption(pPlayerEnt, peaOptionsToFill, "OptionsList.InjuryStore", GET_REF(s_ContactMsgs.hInjuryStoreMsg), ContactDialogState_InjuryStore, 0, eaMapVars);
			}
		}
		// Nemesis options
		if(contact_IsNemesis(pContactDef)){
			if(pPlayerEnt->pPlayer && eaSize(&pPlayerEnt->pPlayer->nemesisInfo.eaNemesisStates)){
				contactdialog_CreateDialogOption(pPlayerEnt, peaOptionsToFill, "OptionsList.Nemesis", GET_REF(s_ContactMsgs.hNemesisMsg), ContactDialogState_Nemesis, 0, eaMapVars);
			} else {
				contactdialog_CreateDialogOption(pPlayerEnt, peaOptionsToFill, "OptionsList.NewNemesis", GET_REF(s_ContactMsgs.hNewNemesisMsg), ContactDialogState_NewNemesis, 0, eaMapVars);
			}
		}
		//Guilds
		if ( contact_IsGuild(pContactDef) ) {
			if (guild_IsMember(pPlayerEnt)){
				contactdialog_CreateDialogOption(pPlayerEnt, peaOptionsToFill, "OptionsList.Guild", GET_REF(s_ContactMsgs.hGuildMsg), ContactDialogState_Guild, 0, eaMapVars);
			} else {
				contactdialog_CreateDialogOption(pPlayerEnt, peaOptionsToFill, "OptionsList.NewGuild", GET_REF(s_ContactMsgs.hNewGuildMsg), ContactDialogState_NewGuild, 0, eaMapVars);
			}
		}
		// Mission Search
		if ( contact_IsMissionSearch(pContactDef)){
			if(g_ContactConfig.bShowRemoteContactsFromMissionSearch)
				contactdialog_CreateDialogOption(pPlayerEnt, peaOptionsToFill, "OptionsList.MissionSearch", GET_REF(pContactDef->missionSearchStringMsg.hMessage), ContactDialogState_Exit, ContactActionType_RemoteContacts, eaMapVars);
			else
				contactdialog_CreateDialogOption(pPlayerEnt, peaOptionsToFill, "OptionsList.MissionSearch", GET_REF(pContactDef->missionSearchStringMsg.hMessage), ContactDialogState_MissionSearch, 0, eaMapVars);
		}
		// Bank
		if(contact_IsBank(pContactDef)){
			contactdialog_CreateDialogOption(pPlayerEnt, peaOptionsToFill, "OptionsList.Bank", GET_REF(s_ContactMsgs.hBankMsg), ContactDialogState_Bank, 0, eaMapVars);
		}
		// Shared Bank
		if(contact_IsSharedBank(pContactDef)){
			contactdialog_CreateDialogOption(pPlayerEnt, peaOptionsToFill, "OptionsList.SharedBank", GET_REF(s_ContactMsgs.hSharedBankMsg), ContactDialogState_SharedBank, 0, eaMapVars);
		}
		// ZStore
		if(contact_IsZStore(pContactDef)){
			contactdialog_CreateDialogOption(pPlayerEnt, peaOptionsToFill, "OptionsList.ZStore", GET_REF(s_ContactMsgs.hZStoreMsg), ContactDialogState_ZStore, 0, eaMapVars);
		}
		// Guild Bank
		if(contact_IsGuildBank(pContactDef)){
			contactdialog_CreateDialogOption(pPlayerEnt, peaOptionsToFill, "OptionsList.GuildBank", GET_REF(s_ContactMsgs.hGuildBankMsg), ContactDialogState_GuildBank, 0, eaMapVars);
		}

		// Powers Trainer
		if (contact_IsPowersTrainer(pContactDef)){
			pNewOption = contactdialog_CreateDialogOption(pPlayerEnt, peaOptionsToFill, "OptionsList.TrainPowers", GET_REF(s_ContactMsgs.hTrainPowersMsg), ContactDialogState_PowersTrainer, 0, eaMapVars);
		}

		// Respec
		if(contact_IsRespec(pContactDef)){
			contactdialog_CreateDialogOption(pPlayerEnt, peaOptionsToFill, "OptionsList.Respec", GET_REF(s_ContactMsgs.hRespecMsg), ContactDialogState_Respec, 0, eaMapVars);
			//TODO(BH): Add this in when contacts should allow respec'ing advantages
			//ADD_OPTION(ContactInteractType_RespecAdvantages, ContactScreenType_Respec, "RespecAdvtanges", NULL, NULL);
		}

		// Marketplace
		if (contact_IsMarket(pContactDef)){
			pNewOption = contactdialog_CreateDialogOption(pPlayerEnt, peaOptionsToFill, "OptionsList.EnterMarket", GET_REF(s_ContactMsgs.hEnterMarketMsg), ContactDialogState_Market, 0, eaMapVars);
		}

		// Auction broker options
		FOR_EACH_IN_CONST_EARRAY_FORWARDS(pContactDef->ppAuctionBrokerOptionList, AuctionBrokerContactData, pAuctionBrokerData)
		{
			if (GET_REF(pAuctionBrokerData->hAuctionBrokerDef))
			{
				Message *pAuctionBrokerMessage = GET_REF(pAuctionBrokerData->optionText.hMessage);
				if (!pAuctionBrokerMessage)
				{
					pAuctionBrokerMessage = GET_REF(s_ContactMsgs.hAuctionBrokerMsg);
				}

				pNewOption = contactdialog_CreateDialogOption(pPlayerEnt, peaOptionsToFill, "OptionsList.AuctionBroker", pAuctionBrokerMessage, ContactDialogState_AuctionBroker, 0, eaMapVars);
				COPY_HANDLE(pNewOption->pData->hAuctionBrokerDef, pAuctionBrokerData->hAuctionBrokerDef);
			}
		}
		FOR_EACH_END
		
		// UGC search agent options
		FOR_EACH_IN_CONST_EARRAY_FORWARDS(pContactDef->ppUGCSearchAgentOptionList, UGCSearchAgentData, pUGCSearchAgentData)
		{
			Message *pUGCSearchAgentMessage = GET_REF(pUGCSearchAgentData->optionText.hMessage);
			if (!pUGCSearchAgentMessage)
			{
				pUGCSearchAgentMessage = GET_REF(s_ContactMsgs.hUGCSearchMsg);
			}
			pNewOption = contactdialog_CreateDialogOption(pPlayerEnt, peaOptionsToFill, "OptionsList.UGCSearchAgent", pUGCSearchAgentMessage, ContactDialogState_UGCSearchAgent, 0, eaMapVars);
			pNewOption->pData->pUGCSearchAgentData = StructClone(parse_UGCSearchAgentData, pUGCSearchAgentData);
		}
		FOR_EACH_END
			
		// Image Menu
		if(contact_IsImageMenu(pContactDef)){
			ContactImageMenuItem** eaImageMenuItems = NULL;
			ContactScreenType curScreenType = SAFE_MEMBER( pPlayerEnt->pPlayer->pInteractInfo->pContactDialog, screenType );
			
			contact_GetImageMenuItems(pContactDef, &eaImageMenuItems);

			if( curScreenType == ContactScreenType_List ) {
				pNewOption = contactdialog_CreateDialogOption(pPlayerEnt, peaOptionsToFill, "OptionsList.ImageMenu", GET_REF(s_ContactMsgs.hImageMenuMsg), ContactDialogState_ImageMenu, 0, eaMapVars);
			} else if( curScreenType == ContactScreenType_ImageMenu ) {
				contact_EvalSetupContext(pPlayerEnt, NULL);
				for (i = 0; i < eaSize(&eaImageMenuItems); i++){
					if ( (!eaImageMenuItems[i]->visibleCondition || contact_EvaluateAfterManualContextSetup(eaImageMenuItems[i]->visibleCondition))){
						char buffer[256];
						sprintf(buffer, "ImageMenu.%d", i);
						pNewOption = contactdialog_CreateDialogOption(pPlayerEnt, peaOptionsToFill, buffer, GET_REF(s_ContactMsgs.hImageMenuMsg), ContactDialogState_OptionListFarewell, 0, eaMapVars);
						estrCopy2(&pNewOption->pchConfirmHeader, langTranslateMessage(entGetLanguage(pPlayerEnt), GET_REF(eaImageMenuItems[i]->name.hMessage)));
						pNewOption->xPos = eaImageMenuItems[i]->x;
						pNewOption->yPos = eaImageMenuItems[i]->y;
						estrCopy2(&pNewOption->pchIconName, eaImageMenuItems[i]->iconImage);
						pNewOption->pData->action = ContactActionType_PerformImageMenuAction;
						pNewOption->pData->iImageMenuItemIndex = i;
						if (eaImageMenuItems[i]->requiresCondition)
						{
							pNewOption->bCannotChoose = ! contact_EvaluateAfterManualContextSetup(eaImageMenuItems[i]->requiresCondition);
						}
						if (eaImageMenuItems[i]->recommendedCondition)
						{
							pNewOption->bRecommended = contact_EvaluateAfterManualContextSetup(eaImageMenuItems[i]->recommendedCondition);
						}
						else
						{
							pNewOption->bRecommended = 1;
						}
						//look into easImageMenuItems[i] for a map transfer, copy that map's name into the option
						if (eaImageMenuItems[i]->action)
						{
							WorldGameActionProperties** actions = eaImageMenuItems[i]->action->eaActions;
							for (j=0; j < eaSize(&actions); j++)
							{
								if (actions[j]->pWarpProperties)
								{
									pNewOption->pchMapName = allocAddString(actions[j]->pWarpProperties->warpDest.pSpecificValue->pcZoneMap);
									break;
								}
							}
						}
					}
				}
			}

			eaDestroy( &eaImageMenuItems );
		}

		// Mailbox
		if(contact_IsMailbox(pContactDef)){
			pNewOption = contactdialog_CreateDialogOption(pPlayerEnt, peaOptionsToFill, "OptionsList.MailBox", GET_REF(s_ContactMsgs.hMailBoxMsg), ContactDialogState_MailBox, 0, eaMapVars);
		}

		// Replay Missions Giver
		if (contact_IsReplayMissionGiver(pContactDef) && pPlayerEnt) {
			MissionInfo *pInfo = mission_GetInfoFromPlayer(pPlayerEnt);
			// Find completed missions
			if(pInfo && pInfo->completedMissions) {
				for(i=0; i<eaSize(&pInfo->completedMissions); i++){
					MissionDef* pMissionDef = GET_REF(pInfo->completedMissions[i]->def);
					if(pMissionDef && missiondef_CanBeOfferedAtAll(pPlayerEnt, pMissionDef, NULL, NULL, NULL)) {
						// Create the dialog option
						ContactDialogOption* pOption = NULL;
						estrPrintf(&estrBuf, "OptionsList.ReplayMissionOffer.%s", pMissionDef->pchRefString);
						pOption = contactdialog_CreateMissionDialogOption(pPlayerEnt, iPlayerLevel, pMissionDef, peaOptionsToFill, estrBuf, GET_REF(s_ContactMsgs.hOfferedMissionMsg), ContactDialogState_ViewOfferedMission, 0, eaMapVars);
						if(pOption && pOption->pData) {
							// Add credit type
							if(!pOption->pData->pMissionOfferParams) {
								pOption->pData->pMissionOfferParams = StructCreate(parse_MissionOfferParams);
							}
							pOption->pData->pMissionOfferParams->eCreditType = MissionCreditType_AlreadyCompleted;
						}
					}
				}
			}
		}

		// Lore Dialogs
		for(i=0; i<eaSize(&pContactDef->eaLoreDialogs); i++)
		{
			ContactLoreDialog* pLoreDialog = eaGet(&pContactDef->eaLoreDialogs, i);
			if (pLoreDialog && (!pLoreDialog->pCondition || contact_Evaluate(pPlayerEnt, pLoreDialog->pCondition, NULL))){
				ItemDef *pItemDef = GET_REF(pLoreDialog->hLoreItemDef);
				if (pItemDef && pItemDef->eType == kItemType_Lore){
					Message *pMessage = GET_REF(pLoreDialog->optionText.hMessage);
					if (!pMessage) {
						pMessage = GET_REF(pItemDef->displayNameMsg.hMessage);
					}

					estrPrintf(&estrBuf, "OptionsList.LoreDialog.%d", i);
					pNewOption = contactdialog_CreateDialogOption(pPlayerEnt, peaOptionsToFill, estrBuf, pMessage, ContactDialogState_ViewLore, ContactActionType_GiveLoreItem, eaMapVars);
					if (pNewOption && pNewOption->pData){
						COPY_HANDLE(pNewOption->pData->hItemDef, pLoreDialog->hLoreItemDef);
					}
				}
			}
		}
	}

	// All missions
	contact_GetAvailableMissions(pContactDef, pPlayerEnt, &eaOfferMissionList, &eaOfferFlashbackList, &eaInProgMissionList, &eaFailedMissionList, &eaCompleteMissionList, &eaSubMissionList, &eaOnCooldownMissionList, &eaOfferReplayList, bOnlyShowRemoteOptions, NULL);

	// Create mission dialog options
	contact_CreateMissionDialogOptions(pContactDef, peaOptionsToFill, pPlayerEnt, iPlayerLevel, &eaOfferMissionList, &eaOfferFlashbackList, &eaInProgMissionList, &eaFailedMissionList, &eaCompleteMissionList, &eaSubMissionList, &eaOnCooldownMissionList, &eaOfferReplayList, eaMapVars, bOnlyShowRemoteOptions, false);


	if(!bOnlyShowRemoteOptions) 
	{
		{
			// Error message to display in case more than one special dialog evaluates as visible for a single dialog contact
			char *estrErrorMessage = NULL;
			bool bListSorted = false;

			// Special Dialogs
			contact_GetAvailableSpecialDialogs(pContactDef, pPlayerEnt, &eaDialogIndexes);
			contact_GetSpecialDialogsEx(pContactDef, &eaSpecialDialogs, &bListSorted);

			if (bListSorted && contact_IsSingleScreen(pContactDef) && eaiSize(&eaDialogIndexes) > 1)
			{
				bool bProperSortOrderDefined = true;
				for (i = 0; i < eaiSize(&eaDialogIndexes); i++)
				{
					SpecialDialogBlock* pSpecialDialogBlock = eaSpecialDialogs[eaDialogIndexes[i]];
					SpecialDialogBlock* pPrevSpecialDialogBlock = i > 0 ? eaSpecialDialogs[eaDialogIndexes[i - 1]] : NULL;
					if (pSpecialDialogBlock->iSortOrder == 0 || 
						(pPrevSpecialDialogBlock && pPrevSpecialDialogBlock->iSortOrder == pSpecialDialogBlock->iSortOrder))
					{
						bProperSortOrderDefined = false;
						break;
					}
				}

				if (!bProperSortOrderDefined)
				{
					estrStackCreate(&estrErrorMessage);
					estrConcatf(&estrErrorMessage, "*** IMPORTANT *** More than one special dialog evaluate as visible and a proper sort order is not defined while the contact is a 'Single Dialog' contact. Contact name: %s.  Sort Order is REQUIRED if any overrides apply to this contact.\n", pContactDef->name);
				}
			}

			for(i=0; i<eaiSize(&eaDialogIndexes); i++)
			{
				SpecialDialogBlock* dialogBlock = eaGet(&eaSpecialDialogs, eaDialogIndexes[i]);
				if (dialogBlock)
				{				
					Message *pMessage = GET_REF(dialogBlock->displayNameMesg.hMessage);

					if (!pMessage) {
						pMessage = GET_REF(s_ContactMsgs.hSpecialDialogDefaultMsg);
					}

					estrPrintf(&estrBuf, "OptionsList.SpecialDialog.%s", dialogBlock->name);
					pNewOption = contactdialog_CreateDialogOptionEx(pPlayerEnt, peaOptionsToFill, estrBuf, pMessage, ContactDialogState_SpecialDialog, 0, eaMapVars, GET_REF(dialogBlock->hDisplayNameFormatter));
					pNewOption->eType = contact_GetSpecialDialogIndicator(dialogBlock);

					if (pNewOption && pNewOption->pData)
					{
						InteractInfo *pInteractInfo = SAFE_MEMBER2(pPlayerEnt, pPlayer, pInteractInfo);
						pNewOption->pData->pchTargetDialogName = dialogBlock->name;

						// Set the visited status of the dialog option
						contact_SetDialogOptionVisitedStatus(pPlayerEnt, pNewOption, pContactDef);

						// Find the correct dialog block to display
						for(j=0; j < eaSize(&dialogBlock->dialogBlock); j++)
						{
							if(!dialogBlock->dialogBlock[j]->condition || contact_Evaluate(pPlayerEnt, dialogBlock->dialogBlock[j]->condition, NULL))
								break;
						}
						if(j < eaSize(&dialogBlock->dialogBlock))
						{
							pNewOption->pData->iSpecialDialogSubIndex = j;
						}
					}

					if (estrErrorMessage)
					{
						estrConcatf(&estrErrorMessage, "Special dialog %d: %s\n", i + 1, dialogBlock->name);
					}
				}
			}

			if (estrErrorMessage)
			{
				// More than one special dialog evaluated as visible for a single dialog contact
				Errorf("%s", estrErrorMessage);
				estrDestroy(&estrErrorMessage);
			}

			if(eaSpecialDialogs)
				eaDestroy(&eaSpecialDialogs);
		}

		// Optional Actions
		if (pContactDef->pcOptionalActionCategory && pPlayerEnt->pPlayer){
			if (eaSize(&pPlayerEnt->pPlayer->InteractStatus.eaInVolumes)) {
				InteractOption **eaOptions = NULL;
				for(i=eaSize(&pPlayerEnt->pPlayer->InteractStatus.eaInVolumes)-1; i>=0; --i) {
					volume_AddInteractOptions(entGetPartitionIdx(pPlayerEnt), pPlayerEnt->pPlayer->InteractStatus.eaInVolumes[i], NULL, pPlayerEnt, &eaOptions);
				}
				for (i=0; i < eaSize(&eaOptions); i++){
					InteractOption *pOption = eaOptions[i];
					// Only allow volume interacts to be found for now
					if (pOption->pcVolumeName && (pOption->pcCategory == pContactDef->pcOptionalActionCategory)) { // Both are pooled so can do this
						estrPrintf(&estrBuf, "OptionsList.OptionalAction.%s.%d", pOption->pcVolumeName, pOption->iIndex);
						pNewOption = contactdialog_CreateDialogOption(pPlayerEnt, peaOptionsToFill, estrBuf, NULL, ContactDialogState_OptionList, ContactActionType_PerformOptionalAction, eaMapVars);
						if (pNewOption && pNewOption->pData){
							pNewOption->pData->pchVolumeName = StructAllocString(pOption->pcVolumeName);
							pNewOption->pData->iOptionalActionIndex = pOption->iIndex;
							estrPrintf(&pNewOption->pchDisplayString, "%s", pOption->pcInteractString);
						}
					}
				}
				eaDestroyStruct(&eaOptions, parse_InteractOption);
			}
		}
	}

	estrDestroy(&estrBuf);
	eaDestroy(&eaOfferMissionList);
	eaDestroy(&eaOfferFlashbackList);
	eaDestroy(&eaOfferReplayList);
	eaDestroy(&eaInProgMissionList);
	eaDestroy(&eaFailedMissionList);
	eaDestroy(&eaCompleteMissionList);
	eaDestroy(&eaSubMissionList);
	eaDestroy(&eaOnCooldownMissionList);
	eaiDestroy(&eaDialogIndexes);
	eaDestroy(&eaMapVars);

	PERFINFO_AUTO_STOP();
}

static void contactdialog_CreateSpecialDialogOptions(Entity *pPlayerEnt, int iPlayerLevel, ContactDef *pContactDef, ContactDialog *pDialog, SpecialDialogBlock *pSpecialDialog, bool bSetVisitedStatus, WorldVariable** eaMapVars)
{
	ContactDialogOption *pNewOption = NULL;
	bool bInCriticalTeamDialog = false;
	Team *pTeam = NULL;

	if (!pDialog) {
		return;
	}

	if (pSpecialDialog && 
		pSpecialDialog->eFlags & SpecialDialogFlags_CriticalDialog &&
		contact_CanStartTeamDialog(pPlayerEnt))
	{
		// The player is the team spokesman for a critical team dialog
		bInCriticalTeamDialog = true;

		// Get the team
		pTeam = team_GetTeam(pPlayerEnt);
	}

	// Handle case of multiple text blocks on a single special dialog
	// This is done by giving a "dialog index" within that special dialog
	if (pSpecialDialog && eaSize(&pSpecialDialog->dialogBlock)) {
		U32 numDialogs = eaSize(&pSpecialDialog->dialogBlock);
		U32 dialogIndex = pDialog->iSpecialDialogSubIndex;

		dialogIndex++;

		CLAMP(dialogIndex, 0, numDialogs - 1);

		while (dialogIndex < numDialogs) {
			if(!pSpecialDialog->dialogBlock[dialogIndex]->condition || contact_Evaluate(pPlayerEnt, pSpecialDialog->dialogBlock[dialogIndex]->condition, NULL))
			{
				// If not on last dialog, setup continue button to next dialog and nothing else
				ContactDialogOption *pDefaultBackOption = NULL;

				// Get the custom continue message if available
				Message *pCustomContinueMsg = GET_REF(pSpecialDialog->dialogBlock[pDialog->iSpecialDialogSubIndex]->continueTextMesg.hMessage);

				pNewOption = contactdialog_CreateDialogOptionEx(pPlayerEnt, 
					&pDialog->eaOptions, 
					"SpecialDialog.Continue", 
					pCustomContinueMsg ? pCustomContinueMsg : GET_REF(s_ContactMsgs.hContinueMsg), 
					ContactDialogState_SpecialDialog, 
					ContactActionType_PerformAction, 
					eaMapVars, 
					pCustomContinueMsg ? GET_REF(pSpecialDialog->dialogBlock[pDialog->iSpecialDialogSubIndex]->hContinueTextDialogFormatter) : NULL);

				if (pNewOption && pNewOption->pData && pSpecialDialog){
					SET_HANDLE_FROM_REFERENT("ContactDef", pContactDef, pNewOption->pData->hTargetContactDef);
					pNewOption->pData->iSpecialDialogSubIndex = dialogIndex;
				}
				pNewOption->pData->pchTargetDialogName = pSpecialDialog->name;

				// Set the visited status of the dialog option
				if (bSetVisitedStatus)
					contact_SetDialogOptionVisitedStatus(pPlayerEnt, pNewOption, pContactDef);

				return;
			}
			dialogIndex++;
		}
	}

	if (!pSpecialDialog || contact_getNumberOfSpecialDialogActions(pContactDef, pSpecialDialog) == 0 ) {
		// If there are no hand-coded special actions, put up the default ones.

		// Get the custom continue message if available
		Message *pCustomContinueMsg = NULL;
		if (pSpecialDialog && (S32)pDialog->iSpecialDialogSubIndex < eaSize(&pSpecialDialog->dialogBlock))
		{
			pCustomContinueMsg = GET_REF(pSpecialDialog->dialogBlock[pDialog->iSpecialDialogSubIndex]->continueTextMesg.hMessage);
		}

		if (contact_IsSingleScreen(pContactDef)) {
			// Single dialog mode contacts default to a "Continue" option
			pNewOption = contactdialog_CreateDialogOptionEx(pPlayerEnt, 
				&pDialog->eaOptions, 
				"SpecialDialog.Continue", 
				pCustomContinueMsg ? pCustomContinueMsg : GET_REF(s_ContactMsgs.hContinueMsg), 
				ContactDialogState_OptionListFarewell, 
				ContactActionType_DialogComplete, 
				eaMapVars,
				pCustomContinueMsg ? GET_REF(pSpecialDialog->dialogBlock[pDialog->iSpecialDialogSubIndex]->hContinueTextDialogFormatter) : NULL);
			if (pNewOption && pNewOption->pData && pSpecialDialog){
				pNewOption->pData->pchCompletedDialogName = pSpecialDialog->name;
			}
			if (pNewOption){
				pNewOption->bIsDefaultBackOption = true;  // either "Back" or "Continue"

				// Set the visited status of the dialog option
				if (bSetVisitedStatus)
					contact_SetDialogOptionVisitedStatus(pPlayerEnt, pNewOption, pContactDef);
			}

		} else {
			// List mode contacts end up here
			
			if (!gConf.bListModeContactsDoNotContinue) {
				pNewOption = contactdialog_CreateDialogOptionEx(pPlayerEnt, 
					&pDialog->eaOptions, 
					"SpecialDialog.Continue", 
					pCustomContinueMsg ? pCustomContinueMsg : GET_REF(s_ContactMsgs.hContinueMsg), 
					ContactDialogState_OptionListFarewell, 
					ContactActionType_DialogComplete, 
					eaMapVars,
					pCustomContinueMsg ? GET_REF(pSpecialDialog->dialogBlock[pDialog->iSpecialDialogSubIndex]->hContinueTextDialogFormatter) : NULL);

				// Set the visited status of the dialog option
				if (bSetVisitedStatus)
					contact_SetDialogOptionVisitedStatus(pPlayerEnt, pNewOption, pContactDef);

				if (pNewOption && pNewOption->pData && pSpecialDialog){
					pNewOption->pData->pchCompletedDialogName = pSpecialDialog->name;
				}
			}

			pNewOption = contactdialog_CreateDialogOption(pPlayerEnt, &pDialog->eaOptions, "SpecialDialog.Back", GET_REF(s_ContactMsgs.hBackMsg), ContactDialogState_OptionListFarewell, 0, eaMapVars);
			if (pNewOption){
				pNewOption->bIsDefaultBackOption = true;  // either "Back" or "Continue"

				// Set the visited status of the dialog option
				if (bSetVisitedStatus)
					contact_SetDialogOptionVisitedStatus(pPlayerEnt, pNewOption, pContactDef);
			}
		}

	} else {
		// We get here if there are hand-coded actions on the dialog
		ContactDialogOption *pDefaultBackOption = NULL;
		int *eaActionIndices = NULL;
		char buf[64];
		int i;
		bool bActionWasAppended = false;

		if (pSpecialDialog->eFlags & SpecialDialogFlags_RandomOptionOrder)
		{
			S32 iActionsSize = contact_getNumberOfSpecialDialogActions(pContactDef, pSpecialDialog);
			SpecialDialogBlockRandomData* pData;
			pData = eaIndexedGetUsingString(&s_eaSpecialDialogRandomData, pSpecialDialog->name);

			if (!pData)
			{
				S32 iPerms = 1;
				pData = StructCreate(parse_SpecialDialogBlockRandomData);
				pData->pchDialogBlockName = allocAddString(pSpecialDialog->name);
				for (i = 2; i <= iActionsSize; i++)
				{
					iPerms *= i;
				}
				pData->iSeed = randomIntRange(0, iPerms-1);
				eaIndexedEnable(&s_eaSpecialDialogRandomData, parse_SpecialDialogBlockRandomData);
				eaPush(&s_eaSpecialDialogRandomData, pData);
			}

			ea32SetSize(&eaActionIndices, iActionsSize);
			indexPermutation(pData->iSeed, eaActionIndices, iActionsSize);
		}
		else
		{
			S32 iActionsSize = contact_getNumberOfSpecialDialogActions(pContactDef, pSpecialDialog);
			for (i = 0; i < iActionsSize; i++)
			{
				ea32Push(&eaActionIndices, i);
			}
		}
		for(i=0; i<ea32Size(&eaActionIndices); ++i)
		{
			bool bCannotChoose = false;
			SpecialDialogAction *action = contact_getSpecialDialogActionByIndex(pContactDef, pSpecialDialog, eaActionIndices[i]);
			U32 *piMembersElibigleToSee = NULL;
			U32 *piMembersElibigleToInteract = NULL;

			if (!action) {
				continue;
			}

			if (bInCriticalTeamDialog)
			{
				S32 iPartitionIdx = entGetPartitionIdx(pPlayerEnt);

				// Evaluate for all team members
				FOR_EACH_IN_EARRAY_FORWARDS(pTeam->eaMembers, TeamMember, pTeamMember)
				{
					Entity *pEntTeamMember = entFromContainerID(iPartitionIdx, GLOBALTYPE_ENTITYPLAYER, pTeamMember->iEntID);
					if (pEntTeamMember &&
						(pEntTeamMember->myContainerID == pPlayerEnt->myContainerID || contact_CanParticipateInTeamDialog(pEntTeamMember, pPlayerEnt, pDialog)))
					{
						// Evaluate visibility
						if (action->condition == NULL || contact_Evaluate(pEntTeamMember, action->condition, NULL))
						{
							ea32Push(&piMembersElibigleToSee, pTeamMember->iEntID);
						}

						// Evaluate the interact condition
						if (action->canChooseCondition == NULL || contact_Evaluate(pEntTeamMember, action->canChooseCondition, NULL))
						{
							ea32Push(&piMembersElibigleToInteract, pTeamMember->iEntID);
						}
					}
				}
				FOR_EACH_END

				if (piMembersElibigleToSee == NULL)
				{
					// No team member is eligible to see this dialog action so skip it.
					continue;
				}
			}
			else
			{
				if(action->condition && !contact_Evaluate(pPlayerEnt, action->condition, NULL)) {
					continue;
				}
			}


			if (action->canChooseCondition)
			{
				// See if the player can choose this dialog
				bCannotChoose = !contact_Evaluate(pPlayerEnt, action->canChooseCondition, NULL);
			}

			if(i < eaSize(&pSpecialDialog->dialogActions)) {
				sprintf(buf, "SpecialDialog.action_%d",i);
				bActionWasAppended = false;
			} else {
				sprintf(buf, "SpecialActionBlock.action_%d", i - eaSize(&pSpecialDialog->dialogActions));
				bActionWasAppended = true;
			}
			
			if (action->dialogName) {
				ContactMissionOffer *pTargetedMissionOffer = contact_MissionOfferFromSpecialDialogName(pContactDef, action->dialogName);

				if (pTargetedMissionOffer)
				{
					pNewOption = contact_CreateDialogOptionForMissionOfferTargetedFromSpecialDialog(pTargetedMissionOffer, pContactDef, pPlayerEnt, iPlayerLevel, pDialog, GET_REF(action->displayNameMesg.hMessage), ContactActionType_PerformAction, eaMapVars, GET_REF(action->hDisplayNameFormatter));
				}
				else
				{
					pNewOption = contactdialog_CreateDialogOptionEx(pPlayerEnt, &pDialog->eaOptions, buf, GET_REF(action->displayNameMesg.hMessage), ContactDialogState_SpecialDialog, ContactActionType_PerformAction, eaMapVars, GET_REF(action->hDisplayNameFormatter));

					if (pNewOption && pNewOption->pData){
						COPY_HANDLE(pNewOption->pData->hTargetContactDef, action->contactDef);
						pNewOption->pData->pchTargetDialogName = action->dialogName;
						pNewOption->bWasAppended = bActionWasAppended;

						// Set the visited status of the dialog option
						if (bSetVisitedStatus)
							contact_SetDialogOptionVisitedStatus(pPlayerEnt, pNewOption, pContactDef);
					}
				}
			} else {
				pNewOption = contactdialog_CreateDialogOptionEx(pPlayerEnt, &pDialog->eaOptions, buf, GET_REF(action->displayNameMesg.hMessage), ContactDialogState_OptionListFarewell, ContactActionType_PerformAction, eaMapVars, GET_REF(action->hDisplayNameFormatter));

				// Set the visited status of the dialog option
				if (bSetVisitedStatus)
					contact_SetDialogOptionVisitedStatus(pPlayerEnt, pNewOption, pContactDef);

				if (!eaSize(&action->actionBlock.eaActions) && !action->bSendComplete){
					pDefaultBackOption = pNewOption;
				}
			}

			if (pNewOption)
			{
				pNewOption->bCannotChoose = bCannotChoose;
				pNewOption->piTeamMembersEligibleToSee = piMembersElibigleToSee;
				pNewOption->piTeamMembersEligibleToInteract = piMembersElibigleToInteract;
				pNewOption->bWasAppended = bActionWasAppended;
			}

			if (pNewOption && pNewOption->pData){
				pNewOption->pData->iDialogActionIndex = eaActionIndices[i];
				pNewOption->pData->pchCompletedDialogName = pSpecialDialog->name;
			}
		}
		if (pDefaultBackOption){
			pDefaultBackOption->bIsDefaultBackOption = true;
		}
		ea32Destroy(&eaActionIndices);
	}
}

static void contactdialog_CreateMissionSearchOptions(Entity *pPlayerEnt, ContactDef *pContactDef, ContactDialog *pDialog, WorldVariable** eaMapVars)
{
	ContactDialogOption *pNewOption = NULL;
	DictionaryEArrayStruct *pDictStruct = resDictGetEArrayStruct(g_ContactDictionary);
	char buf[256] = {0};
	int iPlayerLevel = entity_GetSavedExpLevel(pPlayerEnt);
	int i, j;

	for(i = 0; i < eaSize(&pDictStruct->ppReferents); i++){
		ContactDef *pDef = pDictStruct->ppReferents[i];
		if (pDef && contact_ShowInSearchResults(pDef) && contact_CanOfferMissions(pDef, pPlayerEnt, true)){
			
			ZoneMapInfo *zminfo = pDef->pchMapName?worldGetZoneMapByPublicName(pDef->pchMapName):NULL;
			const char *pchContactDisplayName = langTranslateMessageRef(entGetLanguage(pPlayerEnt), pDef->displayNameMsg.hMessage);
			if (!pchContactDisplayName){
				pchContactDisplayName = pDef->name;
			}

			sprintf(buf, "MissionSearch.Contact.%s", pDef->name);
			pNewOption = contactdialog_CreateDialogOption(pPlayerEnt, &pDialog->eaOptions, buf, NULL, ContactDialogState_MissionSearchViewContact, 0, eaMapVars);

			if (pNewOption){
				U8 iMaxMissionLevel = 0x0;
				U8 iMinMissionLevel = 0xFF;
				ContactMissionOffer** eaOfferList = NULL;
				
				contact_GetMissionOfferList(pDef, pPlayerEnt, &eaOfferList);

				if (pNewOption->pData){
					SET_HANDLE_FROM_REFERENT(g_ContactDictionary, pDef, pNewOption->pData->hMissionListContactDef);
				}

				for (j = 0; j < eaSize(&eaOfferList); j++)
				{
					ContactMissionOffer *pMissionOffer = eaOfferList[j];
					MissionDef *pMissionDef = pMissionOffer ? GET_REF(pMissionOffer->missionDef) : NULL;
					if (pMissionDef && contact_MissionCanBeOffered(pDef, pMissionDef, pMissionOffer, pPlayerEnt, NULL))
					{
						U8 iMissionLevel = missiondef_CalculateLevel(entGetPartitionIdx(pPlayerEnt), iPlayerLevel, pMissionDef);
						MIN1(iMinMissionLevel, iMissionLevel);
						MAX1(iMaxMissionLevel, iMissionLevel);
					}
				}

				if(eaOfferList)
					eaDestroy(&eaOfferList);

				// TODO - This is wrong, it should be formatted in a message or something... 
				// Ideally I would have like "header" rows but the Contact UI currently can't do that
				if (iMinMissionLevel == iMaxMissionLevel){
					if (zminfo){
						estrPrintf(&pNewOption->pchDisplayString, "[%s] %s, %d", langTranslateMessage(entGetLanguage(pPlayerEnt), zmapInfoGetDisplayNameMessagePtr(zminfo)), pchContactDisplayName, iMinMissionLevel);
					} else {
						estrPrintf(&pNewOption->pchDisplayString, "%s, %d", pchContactDisplayName, iMinMissionLevel);
					}
				}
				else{
					if (zminfo){
						estrPrintf(&pNewOption->pchDisplayString, "[%s] %s, %d-%d", langTranslateMessage(entGetLanguage(pPlayerEnt), zmapInfoGetDisplayNameMessagePtr(zminfo)), pchContactDisplayName, iMinMissionLevel, iMaxMissionLevel);
					} else {
						estrPrintf(&pNewOption->pchDisplayString, "%s, %d-%d", pchContactDisplayName, iMinMissionLevel, iMaxMissionLevel);
					}
				}
			}
		}
	}
	
	// Sort all those options alphabetically
	eaQSort(pDialog->eaOptions, contactdialogoption_SortByDisplayName);
}

static bool contactdialog_InventoryBagsHaveNonChoosableRewards(InventoryBag*** eaBags)
{
	int i;
	for(i=0; i<eaSize(eaBags); i++)
	{
		InventoryBag* pRewardBag = (*eaBags)[i];
		if(inv_bag_CountItems(pRewardBag, NULL) && pRewardBag->pRewardBagInfo->PickupType != kRewardPickupType_Choose)
			return true;
	}
	return false;
}

static bool contactdialog_InventoryBagsHaveChoosableRewards(InventoryBag*** eaBags)
{
	int i;
	for(i=0; i<eaSize(eaBags); i++)
	{
		InventoryBag* pRewardBag = (*eaBags)[i];
		if(pRewardBag->pRewardBagInfo->PickupType == kRewardPickupType_Choose
			&& (inv_bag_CountItems(pRewardBag, NULL) > 0 && pRewardBag->pRewardBagInfo->NumPicks > 0))
		{
			return true;
		}
	}
	return false;
}

// This ensures that MissionOfferParams are valid, and may change them to make them valid if necessary.
// Returns TRUE if successful, FALSE means the mission shouldn't be offered.
static bool contactdialog_ValidateMissionOfferParams(ContactDef *pContactDef, MissionCreditType eAllowedCreditType, MissionOfferParams *pParams)
{	
	if (pParams && pParams->eCreditType == MissionCreditType_Flashback){
		// Flashback offers should always be granted as Flashbacks, regardless of what CreditType is actually allowed
		return true;
	} else if (pParams && pParams->uSharerID) {
		// If this is a Shared Mission, update the offer params to whatever the current allowed CreditType is
		pParams->eCreditType = eAllowedCreditType;
		return true;
	} else if (pContactDef && contact_IsReplayMissionGiver(pContactDef) && pParams && pParams->eCreditType == MissionCreditType_AlreadyCompleted) {
		// If this is a Replay Mission Giver and the CreditType is "AlreadyCompleted", 
		// then this is a Replay Mission offer and no changes are needed
		return true;
	} else if (eAllowedCreditType == MissionCreditType_Primary) {
		// If none of the above things apply, and this mission can be offered as a Primary, continue
		if (pParams)
			pParams->eCreditType = MissionCreditType_Primary;
		return true;
	} else if(pParams && eAllowedCreditType == MissionCreditType_AlreadyCompleted && pParams->eCreditType == MissionCreditType_AlreadyCompleted) {
		return true;
	}

	// Couldn't find an appropriate CreditType for this mission
	return false;
}

//************************************************************************************
// Description: Generates headshot information based on a "ContactCostume" struct.
//				Does not support pulling costume information from an entity.
// 
// Returns:     < bool > True if successfully generated headshot information
//
// Parameter:   < Entity * pPlayerEnt > Player the headshot is being shown to
// Parameter:   < ContactCostume * pCostumePrefs >	Costume preferences to build
//													the headshot from
// Parameter:   < ContactHeadshotData * * ppReturn > Struct to store headshot info in.
//************************************************************************************
bool contact_CostumeToHeadshotData(Entity* pPlayerEnt, ContactCostume* pCostumePrefs, ContactHeadshotData** ppReturn)
{
	ContactHeadshotData *pHeadshot = NULL;
	if(ppReturn && *ppReturn)
	{
		pHeadshot = *ppReturn;
		StructReset(parse_ContactHeadshotData, pHeadshot);
	}

	if(pCostumePrefs) {
		if(pCostumePrefs->eCostumeType == ContactCostumeType_Specified && GET_REF(pCostumePrefs->costumeOverride)) {
			// Specified
			if(pHeadshot)
				COPY_HANDLE(pHeadshot->hCostume, pCostumePrefs->costumeOverride);
			return true;
		} else if((pCostumePrefs->eCostumeType == ContactCostumeType_PetContactList && GET_REF(pCostumePrefs->hPetOverride)) && pPlayerEnt) {
			// Pet Contact List
			PetContactList* pList = GET_REF(pCostumePrefs->hPetOverride);
			Entity* pPet = NULL;
			CritterCostume* pCritterCostume = NULL;
					
			PetContactList_GetPetOrCostume(pPlayerEnt, pList, NULL, &pPet, NULL, &pCritterCostume);
					
			if (pPet && pPet->myContainerID!=0)
			{
				// Pet found
				if(pHeadshot)
				{					
					pHeadshot->iPetID = pPet->myContainerID;
				}
				return true;
			}
			else if (pCritterCostume!=NULL)
			{
				// No pet found, use default critter costume if we found one
				if(pHeadshot)
				{
					COPY_HANDLE(pHeadshot->hCostume, pCritterCostume->hCostumeRef);
				}
				return true;
			}
		} else if(pCostumePrefs->eCostumeType == ContactCostumeType_CritterGroup) {
			// Critter Group
			char* pchHashString = NULL;
			U32 uSeed;
			CritterGroup* pCritterGroup = NULL;
			DictionaryEArrayStruct *pStruct = resDictGetEArrayStruct(g_hCritterDefDict);
			CritterDef **possible_list = (CritterDef**)pStruct->ppReferents;
			CritterDef **match_list = NULL;
			CritterDef *pDef = NULL;

			// Create seed from name and map name
			estrCreate(&pchHashString);
			estrPrintf(&pchHashString, "%s%s", pCostumePrefs->pchCostumeIdentifier, zmapInfoGetPublicName(NULL));
			uSeed = hashStringInsensitive(pchHashString);
			estrDestroy(&pchHashString);

			// Get the critter group
			switch(pCostumePrefs->eCostumeCritterGroupType) {
				xcase ContactMapVarOverrideType_Specified: 
				pCritterGroup = GET_REF(pCostumePrefs->hCostumeCritterGroup);

				xcase ContactMapVarOverrideType_MapVar:
				{
					MapVariable* pMapVar = pCostumePrefs->pchCostumeMapVar ? mapvariable_GetByName(entGetPartitionIdx(pPlayerEnt), pCostumePrefs->pchCostumeMapVar) : NULL;
					if(pMapVar && pMapVar->pVariable) {
						pCritterGroup = GET_REF(pMapVar->pVariable->hCritterGroup);
						if(!pCritterGroup) {
							pDef = GET_REF(pMapVar->pVariable->hCritterDef);
						}
					}
				}
				break;
			}

			// Find a critter def
			if(pCritterGroup) {
				if(GET_REF(pCritterGroup->hHeadshotCritterGroup)) {
					pCritterGroup = GET_REF(pCritterGroup->hHeadshotCritterGroup);
				}
				match_list = critter_GetMatchingGroupList(&possible_list, pCritterGroup);
				if(match_list && eaSize(&match_list)) {
					int iDef = randomIntRangeSeeded(&uSeed, RandType_LCG, 0, eaSize(&match_list)-1);
					int iCount = 1;
					// Determine if the def is a valid def
					do {
						pDef = match_list[iDef];
						iDef = (iDef+1)%eaSize(&match_list);
					} while(pDef->bDisabledForContacts && iCount <= eaSize(&match_list));

					// If no valid def is found, use the contact ent for a costume instead
					if(pDef->bDisabledForContacts)
					{
						pDef = NULL;
					}
				}
			}

			// Set the costume
			if(pDef) {
				if (pDef->bGenerateRandomCostume && GET_REF(pDef->hSpecies))
				{
					SpeciesDef *pSpecies = GET_REF(pDef->hSpecies);
					NOCONST(PlayerCostume) *pCostumeTemp = NULL;
					MersenneTable *pTable = mersenneTableCreate(uSeed);

					pCostumeTemp = StructCreateNoConst(parse_PlayerCostume);
					pCostumeTemp->eGender = pSpecies->eGender;
					COPY_HANDLE(pCostumeTemp->hSkeleton, pSpecies->hSkeleton);
					pCostumeTemp->eCostumeType = kPCCostumeType_NPC;

					costumeRandom_SetRandomTable(pTable);
					costumeRandom_FillRandom(pCostumeTemp, pSpecies, NULL, NULL, NULL, NULL, NULL, true, true, false, false, true, true, true);
					costumeTailor_StripUnnecessary(pCostumeTemp);
					costumeRandom_SetRandomTable(NULL);
					mersenneTableFree(pTable);

					if(pHeadshot)
						pHeadshot->pCostume = (PlayerCostume*)pCostumeTemp;
					return true;
				} else if ( pDef->ppCostume && eaSize(&pDef->ppCostume)) {
					CritterCostume *pCritterCostume = pDef->ppCostume[randomIntRangeSeeded(&uSeed, RandType_LCG, 0, eaSize(&pDef->ppCostume)-1)];
					if(pCritterCostume)
					{
						if(pHeadshot)
							COPY_HANDLE(pHeadshot->hCostume, pCritterCostume->hCostumeRef);
						return true;
					}
				}
			}
		} else if(pCostumePrefs->eCostumeType == ContactCostumeType_Player && pPlayerEnt) {
			PlayerCostume *pCostume = costumeEntity_GetEffectiveCostume(pPlayerEnt);
			if (pCostume && pHeadshot)
				pHeadshot->pCostume = StructClone(parse_PlayerCostume, pCostume);
			return true;
		}
	} 

	return false;
}

static void contactdialog_SetCostumeFromHeadshotData(SA_PARAM_NN_VALID ContactDialog* pDialog, 
													 SA_PARAM_NN_VALID ContactHeadshotData* pHeadshot)
{
	// Clear the fields
	pDialog->iHeadshotOverridePetID = 0;
	pDialog->headshotEnt = 0;
	if(IS_HANDLE_ACTIVE(pDialog->hHeadshotOverride))
		REMOVE_HANDLE(pDialog->hHeadshotOverride);
	if(pDialog->pHeadshotOverride)
		StructDestroySafe(parse_PlayerCostume, &pDialog->pHeadshotOverride);

	if(pHeadshot->iPetID)
	{
		// Set pet ID
		pDialog->iHeadshotOverridePetID = pHeadshot->iPetID;
	} 
	else if(pHeadshot->pCostume)
	{
		// Set costume
		pDialog->pHeadshotOverride = pHeadshot->pCostume;
		pHeadshot->pCostume = NULL;
	}
	else if(IS_HANDLE_ACTIVE(pHeadshot->hCostume))
	{
		// Set costume ref
		COPY_HANDLE(pDialog->hHeadshotOverride, pHeadshot->hCostume);
	}
}

static void contactdialog_SetCostumeFromPrefs(ContactDialog* pDialog, 
											  Entity* pPlayerEnt, 
											  EntityRef contactEntRef, 
											  ContactCostume* pCostumePrefs)
{
	if(!pDialog)
		return;

	if(pCostumePrefs) {
		ContactHeadshotData* pHeadshot = StructCreate(parse_ContactHeadshotData);

		if(contact_CostumeToHeadshotData(pPlayerEnt, pCostumePrefs, &pHeadshot))
		{
			contactdialog_SetCostumeFromHeadshotData(pDialog, pHeadshot);
		}

		StructDestroy(parse_ContactHeadshotData, pHeadshot);
	} 

	pDialog->headshotEnt = contactEntRef;
}

PlayerCostume *contact_PlayerCostumeFromContactDialog(ContactDialog *pContactDialog, Entity *pPlayer)
{
	PlayerCostume *pPlayerCostume = NULL;

	if(pContactDialog)
	{
		PlayerCostume *pCostume = pContactDialog->pHeadshotOverride ? pContactDialog->pHeadshotOverride : GET_REF(pContactDialog->hHeadshotOverride);
		if(pCostume) // Override
		{
			pPlayerCostume = pCostume;
		} 
		else if(pContactDialog->iHeadshotOverridePetID && pPlayer) // Pet
		{
			PetRelationship* pPetRelationship = SavedPet_GetPetFromContainerID(pPlayer, pContactDialog->iHeadshotOverridePetID, true);
			if(pPetRelationship) 
			{
				Entity* pPet = GET_REF(pPetRelationship->hPetRef);
				if(pPet) 
				{
					pPlayerCostume = costumeEntity_GetActiveSavedCostume(pPet);
				}
			}
		} 
		else if (pContactDialog->headshotEnt) // Headshot Entity
		{
			// No override so use the entity's costume
			Entity *pEnt = entFromEntityRef(entGetPartitionIdx(pPlayer), pContactDialog->headshotEnt);
			if(pEnt)
			{
				pPlayerCostume = costumeEntity_GetBaseCostume(pEnt);
			}
		}
	}

	return pPlayerCostume;
}

static void contact_GetVoicePathFromPlayerCostume(PlayerCostume *pPlayerCostume, const char **ppchVoicePath)
{
	PCVoice *pPCVoice;

	if(ppchVoicePath && *ppchVoicePath)
	{
		*ppchVoicePath = NULL;
	}

	if(pPlayerCostume)
	{
		if(pPCVoice = GET_REF(pPlayerCostume->hVoice))
		{
			if(ppchVoicePath)
			{
				*ppchVoicePath = pPCVoice->pcVoice;
			}
		}
	}
}

static void contact_GetPhrasePathFromDialogBlock(DialogBlock *pDialogBlock, const char **ppchPhrasePath)
{
	if(ppchPhrasePath && *ppchPhrasePath)
	{
		ppchPhrasePath = NULL;
	}

	if(pDialogBlock && pDialogBlock->ePhrase != ContactAudioPhrases_None)
	{
		if(ppchPhrasePath)
		{
			*ppchPhrasePath = StaticDefineIntRevLookup(ContactAudioPhrasesEnum, pDialogBlock->ePhrase);
		}
	}
}



static void contact_GetSoundVoiceAndPhrasePath(DialogBlock *pDialogBlock, PlayerCostume *pPlayerCostume, 
											   const char **ppchVoicePath, const char **ppchPhrasePath)
{
	contact_GetVoicePathFromPlayerCostume(pPlayerCostume, ppchVoicePath);
	contact_GetPhrasePathFromDialogBlock(pDialogBlock, ppchPhrasePath);
}

static void contact_GetDialogKeyForAllegiance(char** pestrDialog, const char* pchBaseKey, 
											  AllegianceDef* pAllegiance)
{
	estrClear(pestrDialog);
	if (pAllegiance)
	{
		estrPrintf(pestrDialog, "%s_%s", pchBaseKey, pAllegiance->pcName);
	}
	else
	{
		estrPrintf(pestrDialog, "%s", pchBaseKey);
	}
}

static void contact_ResetCameraSource(SA_PARAM_NN_VALID ContactDialog *pContactDialog)
{
	devassert(pContactDialog);

	if (pContactDialog)
	{
		zeroVec3(pContactDialog->vecCameraSourcePos);
		zeroVec4(pContactDialog->quatCameraSourceRot);
		pContactDialog->cameraSourceEnt = 0;
	}
}

static void contact_SetCameraSource(Entity *pPlayerEnt, ContactSourceType eSourceType, SA_PARAM_OP_STR const char *pchName, SA_PARAM_OP_STR const char *pchSecondaryName, SA_PARAM_NN_VALID ContactDialog *pContactDialog, SA_PARAM_NN_VALID ContactDef *pContactDef, SA_PARAM_OP_STR const char *pchFileName, SA_PARAM_OP_STR const char *pchSpecialDialogName)
{
	devassert(pContactDialog);

	if (pchSpecialDialogName == NULL)
	{
		pchSpecialDialogName = "[Not set in a special dialog]";
	}

	if (pContactDialog == NULL)
	{
		return;
	}

	if (eSourceType == ContactSourceType_None)
	{
		contact_ResetCameraSource(pContactDialog);
	}
	else
	{
		switch(eSourceType)
		{
			case ContactSourceType_NamedPoint:
				{
					GameNamedPoint* point = namedpoint_GetByName(pchName, NULL);
					if (point)
					{
						namedpoint_GetPosition(point, pContactDialog->vecCameraSourcePos, pContactDialog->quatCameraSourceRot);
					}
					else
					{
						ErrorFilenamef(pchFileName, "Named point '%s' defined as camera source for contact '%s' is not found! Special dialog name: %s", pchName, pContactDef->name, pchSpecialDialogName);
						contact_ResetCameraSource(pContactDialog);
					}
					break;
				}
			case ContactSourceType_Encounter:
				{
					GameEncounter *pEncounter = encounter_GetByName(pchName, NULL);
					if (pEncounter)
					{
						Entity *pActorEnt = encounter_GetActorEntity(entGetPartitionIdx(pPlayerEnt), pEncounter, pchSecondaryName);
						if (pActorEnt)
						{
							Vec3 vecPYR;
							zeroVec3(vecPYR);

							entGetPos(pActorEnt, pContactDialog->vecCameraSourcePos);
							entGetFacePY(pActorEnt, vecPYR);
							PYRToQuat(vecPYR, pContactDialog->quatCameraSourceRot);
							pContactDialog->cameraSourceEnt = pActorEnt->myRef;
						}
						else
						{
							ErrorFilenamef(pchFileName, "Actor '%s' in encounter '%s' defined as camera source for contact '%s' is not found! Special dialog name: %s", pchSecondaryName, pchName, pContactDialog->pchContactDispName, pchSpecialDialogName);
							contact_ResetCameraSource(pContactDialog);
						}
					}
					else // We don't care about old encounter system
					{
						ErrorFilenamef(pchFileName, "Encounter '%s' defined as camera source for contact '%s' is not found! Special dialog name: %s", pchName, pContactDef->name, pchSpecialDialogName);
						contact_ResetCameraSource(pContactDialog);
					}
					break;
				}
			case ContactSourceType_Clicky:
				{
					GameInteractable *pInteractable = interactable_GetByName(pchName, NULL);
					if (pInteractable &&
						pInteractable->pWorldInteractable && 
						pInteractable->pWorldInteractable->entry)
					{
						copyVec3(pInteractable->pWorldInteractable->entry->base_entry.bounds.world_matrix[3], pContactDialog->vecCameraSourcePos);
						mat3ToQuat(pInteractable->pWorldInteractable->entry->base_entry.bounds.world_matrix, pContactDialog->quatCameraSourceRot);
					}
					else // We don't care about old encounter system
					{
						ErrorFilenamef(pchSpecialDialogName, "Clicky '%s' defined as camera source for contact '%s' is not found! Special dialog name: %s", pchName, pContactDef->name, pchSpecialDialogName);
						contact_ResetCameraSource(pContactDialog);
					}
					break;
				}
		}
	}
}

static void contact_SendTextToChat(Entity *pPlayerEnt, ContactDialog *pDialog)
{	
	// Send contact text to chat
	if(pPlayerEnt && !characterIsTransferring(pPlayerEnt) && pDialog) 
	{
		ChatUserInfo* pFrom = EMPTY_TO_NULL(pDialog->pchContactDispName) ? ChatCommon_CreateUserInfoFromNameOrHandle(pDialog->pchContactDispName) : NULL;
		ChatMessage* pMsg = NULL;
		ContactLogEntry *pLog = StructCreate(parse_ContactLogEntry);
		char* estrChatMessage = NULL;

		estrStackCreate(&estrChatMessage);
		estrCopy2(&pLog->pchName, pDialog->pchContactDispName);
		pLog->uiTimestamp = timeSecondsSince2000();

		if(EMPTY_TO_NULL(pDialog->pchDialogText1)) {
			
			estrPrintf(&pLog->pchText, "%s<br>", pDialog->pchDialogText1);

			estrClear(&estrChatMessage);
			StringStripTagsPrettyPrint(pDialog->pchDialogText1, &estrChatMessage);

			pMsg = ChatCommon_CreateMsg(pFrom, NULL, kChatLogEntryType_NPC, NULL, estrChatMessage, NULL);
			ClientCmd_cmdChatLog_AddMessage(pPlayerEnt, pMsg);
			StructDestroy(parse_ChatMessage, pMsg);
		}

		if(EMPTY_TO_NULL(pDialog->pchDialogText2)) {

			estrAppend2(&pLog->pchText, pDialog->pchDialogText2);

			estrClear(&estrChatMessage);
			StringStripTagsPrettyPrint(pDialog->pchDialogText2, &estrChatMessage);

			pMsg = ChatCommon_CreateMsg(pFrom, NULL, kChatLogEntryType_NPC, NULL, estrChatMessage, NULL);
			ClientCmd_cmdChatLog_AddMessage(pPlayerEnt, pMsg);
			StructDestroy(parse_ChatMessage, pMsg);
		}

		pLog->pchHeadshotStyleDef;
		if (IS_HANDLE_ACTIVE(pDialog->hHeadshotOverride))
			COPY_HANDLE(pLog->hHeadshotCostumeRef, pDialog->hHeadshotOverride);
		if (pDialog->pHeadshotOverride)
			pLog->pHeadshotCostume = StructClone(parse_PlayerCostume, pDialog->pHeadshotOverride);
		if (pDialog->headshotEnt)
			pLog->erHeadshotEntity = pDialog->headshotEnt;
		if (pDialog->iHeadshotOverridePetID)
			pLog->iHeadshotPetID = pDialog->iHeadshotOverridePetID;

		estrDestroy(&estrChatMessage);

		if(pLog) {
			ClientCmd_addContactLogEntry(pPlayerEnt, pLog);
			StructDestroy(parse_ContactLogEntry, pLog);
		}
		StructDestroy(parse_ChatUserInfo, pFrom);
	}
}

//*****************************************************************************
// Description: If contact has no buy items listed (even ones the player cannot
//				currently buy) and the player cannot sell any items to the
//				contact, the "NoStoreItems" dialog will be shown instead of the
//				store.
//
//				This function assumes that the stores have already been processed
//				and all available buy items have already been stored in
//				pDialog->eaStoreItems
// 
// Returns:     < void >
// Parameter:   < Entity * pEnt > Player
// Parameter:   < ContactDef * pContactDef > The current contact def
// Parameter:   < ContactDialog * pDialog > Player's current dialog
//*****************************************************************************
static void contact_ShowNoStoreDialogIfNeeded(Entity* pEnt, 
											  ContactDef *pContactDef, 
											  ContactDialog *pDialog, 
											  WorldVariable** eaMapVars, 
											  GameAccountDataExtract *pExtract)
{
	bool bBuyAvailable = false;
	bool bSellAvailable = false;
	int i,j,k;
	int iStoreIdx;

	if(pDialog && pContactDef && pEnt && pEnt->pInventoryV2)
	{
		bBuyAvailable = eaSize(&pDialog->eaStoreItems) > 0;

		// Determine if there is anything listed to buy or sell at this store
		for (iStoreIdx=0; iStoreIdx < eaSize(&pContactDef->stores) && !bSellAvailable; iStoreIdx++) 
		{
			StoreDef* pStoreDef = GET_REF(pContactDef->stores[iStoreIdx]->ref);

			if (pStoreDef && pStoreDef->bSellEnabled) 
			{
				if(pStoreDef->eContents == Store_Sellable_Items)
				{
					// Iterate through each bag
					for (i = eaSize(&pEnt->pInventoryV2->ppInventoryBags)-1 && !bSellAvailable; i >= 0; i--) {
						InventoryBag *pInvBag;
						int NumSlots;

						// Check that we can sell out of this bag
						if (!(invbag_flags(pEnt->pInventoryV2->ppInventoryBags[i]) & InvBagFlag_SellEnabled)) {
							continue;
						}

						// Make sure the bag is valid, and not empty
						pInvBag = (InventoryBag*)inv_GetBag(CONTAINER_NOCONST(Entity, pEnt), invbag_bagid(pEnt->pInventoryV2->ppInventoryBags[i]), pExtract);
						if (!pInvBag || inv_ent_BagEmpty(pEnt, invbag_bagid(pEnt->pInventoryV2->ppInventoryBags[i]), pExtract)) {
							continue;
						}

						// Iterate through each of the bag's slots
						NumSlots = inv_ent_GetMaxSlots(pEnt, invbag_bagid(pEnt->pInventoryV2->ppInventoryBags[i]), pExtract);
						for (j = 0; j < NumSlots && !bSellAvailable; j++) {
							InventorySlot *pInvSlot = pInvSlot = inv_ent_GetSlotPtr(pEnt, invbag_bagid(pEnt->pInventoryV2->ppInventoryBags[i]), j, pExtract);
							Item *pItem = pInvSlot ? pInvSlot->pItem : NULL;
							ItemDef *pItemDef = pItem ? GET_REF(pItem->hItem) : NULL;
							bool bItemSellable = pItemDef ? (!(pItemDef->flags & kItemDefFlag_CantSell) && pItemDef->eType != kItemType_Mission && pItemDef->eType != kItemType_MissionGrant) : false;

							if(!bItemSellable)
								continue;

							// Iterate through the possible items to see if it's
							for(k=eaSize(&pStoreDef->inventory)-1; k >= 0; k--)
							{
								if(GET_REF(pStoreDef->inventory[k]->hItem) == pItemDef)
									break;
							}

							if(k >= 0)
								bSellAvailable = true;
						}
					}
				} else {
					bSellAvailable = true;
				}
			}
		}

		if(!bSellAvailable && !bBuyAvailable)
		{
			ContactDialogOption *pNewOption = NULL;
			contactdialog_SetTextFromDialogBlocks(pEnt, &pContactDef->noStoreItemsDialog, &pDialog->pchDialogText1, &pDialog->pchSoundToPlay, &pDialog->pchPhrasePath, REF_HANDLEPTR(pDialog->hDialogText1Formatter));
			pDialog->screenType = ContactScreenType_Buttons;
			eaDestroyStruct(&pDialog->eaOptions, parse_ContactDialogOption);
			pNewOption = contactdialog_CreateDialogOption(pEnt, &pDialog->eaOptions, "Store.Back", GET_REF(s_ContactMsgs.hBackMsg), ContactDialogState_OptionList, 0, eaMapVars);
			if(pNewOption)
				pNewOption->bIsDefaultBackOption = true;
		}
	}
}

static void contact_CleanupDialog(Entity* pEnt)
{
	ContactDialog* pContactDialog = SAFE_MEMBER3(pEnt, pPlayer, pInteractInfo, pContactDialog);
	if (pContactDialog && GET_REF(pContactDialog->hPersistStoreDef))
	{
		gslPersistedStore_PlayerRemoveRequests(pEnt);
	}
}

// Set the mission UI type information
static void contact_FillDialogMissionUITypeInfo(SA_PARAM_NN_VALID Entity* pPlayerEnt, 
	SA_PARAM_NN_VALID MissionDef * pMissionDef, SA_PARAM_NN_VALID ContactDialog * pDialog)
{
	// Set the mission type
	pDialog->eMissionUIType = pMissionDef->eUIType;
}

static void contact_UpdateDialogOptionsForTeammates(Entity* pPlayerEnt, ContactDialog* pDialog)
{
	Team *pTeam = team_GetTeam(pPlayerEnt);

	if (pTeam && pDialog)
	{
		int iPartitionIdx = entGetPartitionIdx(pPlayerEnt);
		FOR_EACH_IN_EARRAY_FORWARDS(pTeam->eaMembers, TeamMember, pTeamMember)
		{
			if (pTeamMember && 
				pTeamMember->iEntID != pPlayerEnt->myContainerID)
			{
				Entity *pEntTeamMember = entFromContainerID(iPartitionIdx, GLOBALTYPE_ENTITYPLAYER, pTeamMember->iEntID);
				if (contact_IsInTeamDialog(pEntTeamMember))
				{
					ContactDialog *pTeamMemberDialog = SAFE_MEMBER3(pEntTeamMember, pPlayer, pInteractInfo, pContactDialog);

					if (pTeamMemberDialog)
					{
						eaCopyStructs(&pDialog->eaOptions, &pTeamMemberDialog->eaOptions, parse_ContactDialogOption);
					}
				}
			}
		}
		FOR_EACH_END
	}
}

// Used for critical dialogs to copy the team spokesman's options for all other players on the team
static void contact_InitializeDialogOptionsForTeammates(Entity* pPlayerEnt, ContactDialog* pDialog)
{
	Team *pTeam = team_GetTeam(pPlayerEnt);
	devassert(pTeam);

	if (pTeam && pDialog)
	{
		int iPartitionIdx = entGetPartitionIdx(pPlayerEnt);

		FOR_EACH_IN_EARRAY_FORWARDS(pTeam->eaMembers, TeamMember, pTeamMember)
		{
			if (pTeamMember && 
				pTeamMember->iEntID != pPlayerEnt->myContainerID)
			{
				Entity *pEntTeamMember = entFromContainerID(iPartitionIdx, GLOBALTYPE_ENTITYPLAYER, pTeamMember->iEntID);
				if (contact_CanParticipateInTeamDialog(pEntTeamMember, pPlayerEnt, pDialog))
				{
					InteractInfo *pInteractInfoTeamMember = SAFE_MEMBER2(pEntTeamMember, pPlayer, pInteractInfo);

					if (pInteractInfoTeamMember)
					{
						// Increment the number of participants
						pDialog->iParticipatingTeamMemberCount++;
					}
				}
			}
		}
		FOR_EACH_END

		FOR_EACH_IN_EARRAY_FORWARDS(pTeam->eaMembers, TeamMember, pTeamMember)
		{
			if (pTeamMember && 
				pTeamMember->iEntID != pPlayerEnt->myContainerID)
			{
				Entity *pEntTeamMember = entFromContainerID(iPartitionIdx, GLOBALTYPE_ENTITYPLAYER, pTeamMember->iEntID);
				if (contact_CanParticipateInTeamDialog(pEntTeamMember, pPlayerEnt, pDialog))
				{
					InteractInfo *pInteractInfoTeamMember = SAFE_MEMBER2(pEntTeamMember, pPlayer, pInteractInfo);

					if (pInteractInfoTeamMember)
					{
						// Store the team spokesman container ID
						pDialog->iTeamSpokesmanID = pPlayerEnt->myContainerID;

						contact_SendTextToChat(pEntTeamMember, pDialog);

						if (!pInteractInfoTeamMember->pContactDialog)
						{
							pInteractInfoTeamMember->pContactDialog = StructCreate(parse_ContactDialog);
						} 

						contact_CleanupDialog(pEntTeamMember);
						StructCopy(parse_ContactDialog, pDialog, pInteractInfoTeamMember->pContactDialog, 0, 0, TOK_USEROPTIONBIT_1);
						if (GET_REF(pDialog->hPersistStoreDef))
						{
							gslPersistedStore_PlayerAddRequest(pEntTeamMember, GET_REF(pDialog->hPersistStoreDef));
						}

						// Not the team spokesman
						pInteractInfoTeamMember->pContactDialog->bIsTeamSpokesman = false;
						// Mark this dialog as view only
						pInteractInfoTeamMember->pContactDialog->bViewOnlyDialog = true;
						pInteractInfoTeamMember->pContactDialog->uTeamDialogStartTime = timeSecondsSince2000();
						entity_SetDirtyBit(pEntTeamMember, parse_InteractInfo, pInteractInfoTeamMember, true);
						entity_SetDirtyBit(pEntTeamMember, parse_Player, pEntTeamMember->pPlayer, false);

						// Other team members should be marked as in team dialog and not a spokesman
						if (!pEntTeamMember->pTeam->bInTeamDialog || pEntTeamMember->pTeam->bIsTeamSpokesman)
						{
							pEntTeamMember->pTeam->bIsTeamSpokesman = false;
							pEntTeamMember->pTeam->bInTeamDialog = true;
							entity_SetDirtyBit(pEntTeamMember, parse_PlayerTeam, pEntTeamMember->pTeam, false);
						}
					}
				}
			}
		}
		FOR_EACH_END
	}			

	if (pDialog->iParticipatingTeamMemberCount == 0)
	{
		pDialog->bIsTeamSpokesman = false;
		pPlayerEnt->pTeam->bIsTeamSpokesman = false;
		pPlayerEnt->pTeam->bInTeamDialog = false;
	}
	else
	{
		pDialog->bIsTeamSpokesman = true;
		pPlayerEnt->pTeam->bIsTeamSpokesman = true;
		pPlayerEnt->pTeam->bInTeamDialog = true;
					
		// If the server decides the dialog choice, then even the spokesman gets a ViewOnly dialog
		if (g_ContactConfig.bServerDecidesChoiceForTeamDialogs)
		{
			pDialog->bViewOnlyDialog = true;
			pDialog->uTeamDialogStartTime = timeSecondsSince2000();
		}
	}

	entity_SetDirtyBit(pPlayerEnt, parse_PlayerTeam, pPlayerEnt->pTeam, false);
}

// This builds the structure that is sent to the client.
// No other actions should be taken here.
// Games with different UI flow may need to change this function
static void contact_ChangeDialogState(Entity* pPlayerEnt, EntityRef contactEnt, ContactDef *pContactDef, const ContactDialogOptionData* pOption)
{
	MissionInfo* pMissionInfo = mission_GetInfoFromPlayer(pPlayerEnt);
	InteractInfo* pInteractInfo = SAFE_MEMBER2(pPlayerEnt, pPlayer, pInteractInfo);
	ContactDialogState newState = pOption->targetState;
	ContactDialog *pDialog = NULL;
	ContactDialogOption *pNewOption = NULL;
	SpecialDialogBlock *pSpecialDialog = NULL;
	ContactCostumeFallback *pCostumeFallback = NULL;
	Entity *pNemesisEnt = player_GetPrimaryNemesis(pPlayerEnt);
	int i;
	S32 dialogIndex = 0;
	WorldVariable **eaMapVars = NULL;
	bool skip = false;
	RecruitType eRecruitType= kRecruitType_None;
	bool bShowToTeamMembers = false;
	RewardContextData rewardContextData = {0};
	rewardContextData.pEnt = pPlayerEnt;

	PERFINFO_AUTO_START_FUNC();

	// If the selected option is on a cooldown and the cooldown has yet to expire, do nothing.
	if(pOption && pOption->uCooldownExpireTime && pOption->uCooldownExpireTime > timeSecondsSince2000())
	{
		return;
	}

	if (pInteractInfo){
		if(!pInteractInfo->pContactDialog)
		{
			pInteractInfo->pContactDialog = StructCreate(parse_ContactDialog);
		} 
		else 
		{
			char lastResponseDisplayString[1024] = { 0 };
			U32 uiDialogVersion = pInteractInfo->pContactDialog->uiVersion;
			bool bForceOnTeamDone = pInteractInfo->pContactDialog->bForceOnTeamDone;

			// Copy the last response string
			if (pInteractInfo->pContactDialog->pchLastResponseDisplayString &&
				pInteractInfo->pContactDialog->pchLastResponseDisplayString[0])
			{
				strcpy_s(lastResponseDisplayString, sizeof(lastResponseDisplayString), pInteractInfo->pContactDialog->pchLastResponseDisplayString);
			}			

			contact_CleanupDialog(pPlayerEnt);
			StructReset(parse_ContactDialog, pInteractInfo->pContactDialog);
			pInteractInfo->pContactDialog->uiVersion = uiDialogVersion;
			pInteractInfo->pContactDialog->bForceOnTeamDone = bForceOnTeamDone;
			if (lastResponseDisplayString[0])
			{
				estrCopy2(&pInteractInfo->pContactDialog->pchLastResponseDisplayString, lastResponseDisplayString);
			}
		}

		pInteractInfo->uNextContactDialogOptionsUpdateTime = timeSecondsSince2000();

		pCostumeFallback = pInteractInfo->pCostumeFallback;
		pDialog = pInteractInfo->pContactDialog;
		pDialog->bPartialPermissions = pInteractInfo->bPartialPermissions;
		pDialog->bRemotelyAccessing = pInteractInfo->bRemotelyAccessing;
		entity_SetDirtyBit(pPlayerEnt, parse_InteractInfo, pInteractInfo, true);
		entity_SetDirtyBit(pPlayerEnt, parse_Player, pPlayerEnt->pPlayer, false);
	}

	if (!pDialog){
		PERFINFO_AUTO_STOP_FUNC();
		return;
	}

	eRecruitType = GetRecruitTypes(pPlayerEnt);

	// Set up all data that's not specific to the state
	pDialog->state = newState;	

	mapvariable_GetAllAsWorldVarsNoCopy(entGetPartitionIdx(pPlayerEnt), &eaMapVars);

	if (pContactDef){
		SET_HANDLE_FROM_REFERENT(g_ContactDictionary, pContactDef, pDialog->hContactDef);
	}
	if (contactEnt){
		pDialog->contactEnt = contactEnt;
	}

	// Generate Costume
	if(pContactDef) {
		contactdialog_SetCostumeFromPrefs(pDialog, pPlayerEnt, contactEnt, &pContactDef->costumePrefs);
	} else if (pOption->pHeadshotData) {
		contactdialog_SetCostumeFromHeadshotData(pDialog, pOption->pHeadshotData);
		pDialog->pchHeadshotStyleDef = allocAddString(pOption->pHeadshotData->pchHeadshotStyleDef);
	}
	if(!contact_PlayerCostumeFromContactDialog(pDialog, pPlayerEnt) && pCostumeFallback) {
		COPY_HANDLE(pDialog->hHeadshotOverride, pCostumeFallback->hCostume);
	}

	// Display Name
	if (pContactDef && GET_REF(pContactDef->displayNameMsg.hMessage)){
		langFormatGameDisplayMessage(entGetLanguage(pPlayerEnt), &pDialog->pchContactDispName, &pContactDef->displayNameMsg, STRFMT_ENTITY(pPlayerEnt), STRFMT_MAPVARS(eaMapVars), STRFMT_END);
	} else if (contactEnt) {
		Entity *pContactEnt = entFromEntityRef(entGetPartitionIdx(pPlayerEnt), contactEnt);
		if (pContactEnt){
			estrPrintf(&pDialog->pchContactDispName, "%s", entGetLangName(pContactEnt, entGetLanguage(pPlayerEnt)));
		}
	} else if(pCostumeFallback && pCostumeFallback->pchDisplayName) {
		estrPrintf(&pDialog->pchContactDispName, "%s", pCostumeFallback->pchDisplayName);
	} else if (pOption->pchHeadshotDisplayName!=NULL) {
		estrCopy2(&pDialog->pchContactDispName, pOption->pchHeadshotDisplayName);
	}

	// Headshot style
	if (pContactDef && pContactDef->pchHeadshotStyleDef)
	{
		pDialog->pchHeadshotStyleDef = allocAddString(pContactDef->pchHeadshotStyleDef);
	}

	if (g_ContactConfig.bCheckLastCompletedDialogInteractable)
	{
		pDialog->bLastCompletedDialogIsInteractable = !!contact_GetLastCompletedDialog(pPlayerEnt, NULL, true);
	}

	// Set up the screen based on the dialog state
	if(pDialog && newState && pOption)
	{
		// Reset the anim list to play
		if (pContactDef && IS_HANDLE_ACTIVE(pContactDef->hAnimListToPlay))
		{
			COPY_HANDLE(pDialog->hAnimListToPlayForActiveEntity, pContactDef->hAnimListToPlay);
			COPY_HANDLE(pDialog->hAnimListToPlayForPassiveEntity, pContactDef->hAnimListToPlay);
		}
		else
		{
			if (IS_HANDLE_ACTIVE(pDialog->hAnimListToPlayForActiveEntity))
				REMOVE_HANDLE(pDialog->hAnimListToPlayForActiveEntity);
			if (IS_HANDLE_ACTIVE(pDialog->hAnimListToPlayForPassiveEntity))
				REMOVE_HANDLE(pDialog->hAnimListToPlayForPassiveEntity);

			if (gConf.pchClientSideContactDialogAnimList && 
				gConf.pchClientSideContactDialogAnimList[0] &&
				RefSystem_ReferentFromString(g_AnimListDict, gConf.pchClientSideContactDialogAnimList))
			{
				SET_HANDLE_FROM_STRING(g_AnimListDict, gConf.pchClientSideContactDialogAnimList, pDialog->hAnimListToPlayForActiveEntity);
				SET_HANDLE_FROM_STRING(g_AnimListDict, gConf.pchClientSideContactDialogAnimList, pDialog->hAnimListToPlayForPassiveEntity);
			}
		}

		// Set the initial cut-scene
		if (pContactDef &&
			IS_HANDLE_ACTIVE(pContactDef->hCutSceneDef) && 
			!IS_HANDLE_ACTIVE(pDialog->hCutSceneDef))
		{
			COPY_HANDLE(pDialog->hCutSceneDef, pContactDef->hCutSceneDef);
		}
		else if (gConf.pchDefaultCutsceneForContacts && 
			gConf.pchDefaultCutsceneForContacts[0] && 
			!IS_HANDLE_ACTIVE(pDialog->hCutSceneDef))
		{
			SET_HANDLE_FROM_STRING(g_hCutsceneDict, gConf.pchDefaultCutsceneForContacts, pDialog->hCutSceneDef);
		}

		switch(newState)
		{
		xcase ContactDialogState_Greeting:
			
			pDialog->screenType = ContactScreenType_Buttons;
			if (pContactDef){
				DialogBlock **peaMissionGreetings = NULL;
				DialogBlock **peaConditionalGreetings = NULL;
				DialogBlock **peaNonConditionalGreetings = NULL;

				contact_SetCameraSource(pPlayerEnt, pContactDef->eSourceType, pContactDef->pchSourceName, pContactDef->pchSourceSecondaryName, pDialog, pContactDef, pContactDef->filename, NULL);

				// Get all the possible greetings
				contact_GetActiveMissionGreetings(pPlayerEnt, pContactDef, &peaMissionGreetings);
				contact_GetActiveConditionalGreetings(pPlayerEnt, pContactDef, &peaConditionalGreetings);
				contact_GetNonConditionalGreetings(pContactDef, &peaNonConditionalGreetings);

				contactdialog_SetTextFromDialogBlocks(pPlayerEnt, 
					eaSize(&peaMissionGreetings) > 0 ? &peaMissionGreetings : eaSize(&peaConditionalGreetings) > 0 ? &peaConditionalGreetings : &peaNonConditionalGreetings, 
					&pDialog->pchDialogText1, 
					&pDialog->pchSoundToPlay, 
					&pDialog->pchPhrasePath,
					REF_HANDLEPTR(pDialog->hDialogText1Formatter));

				// Destroy the temporary earrays
				eaDestroy(&peaMissionGreetings);
				eaDestroy(&peaConditionalGreetings);
				eaDestroy(&peaNonConditionalGreetings);

				contact_GetVoicePathFromPlayerCostume(contact_PlayerCostumeFromContactDialog(pDialog, pPlayerEnt), &pDialog->pchVoicePath);
			}
			pNewOption = contactdialog_CreateDialogOption(pPlayerEnt, &pDialog->eaOptions, "Greeting.Continue", GET_REF(s_ContactMsgs.hContinueMsg), ContactDialogState_OptionList, 0, eaMapVars);
			if (pNewOption && pNewOption->pData){
				pNewOption->pData->bFirstOptionsList = true;
			}

		xcase ContactDialogState_OptionList:
			if (pContactDef)
			{
				contact_SetCameraSource(pPlayerEnt, pContactDef->eSourceType, pContactDef->pchSourceName, pContactDef->pchSourceSecondaryName, pDialog, pContactDef, pContactDef->filename, NULL);
			}			

			// If this is a Single Dialog contact, and we're not entering the dialog for the first time, or if the action explicitly closes the dialog just stop all interaction
			if (pContactDef && (contact_IsSingleScreen(pContactDef) || pOption->action == ContactActionType_DialogComplete) && !pOption->bFirstOptionsList){
				interaction_EndInteractionAndDialog(entGetPartitionIdx(pPlayerEnt), pPlayerEnt, true, false, true);
				skip = true;
				break;
			}
			
			pDialog->screenType = ContactScreenType_List;
			estrPrintf(&pDialog->pchListHeader, "%s", langTranslateMessage(entGetLanguage(pPlayerEnt), GET_REF(s_ContactMsgs.hDefaultListHeaderMsg)));
			if (pContactDef){

				contactdialog_CreateOptionsList(pPlayerEnt, pContactDef, &pDialog->eaOptions, pDialog->bPartialPermissions);

				// See if the player has any Mission Offers
				// For now, Optional Actions count as Mission Offers also
				for (i=0; i<eaSize(&pDialog->eaOptions); i++){
					if (pDialog->eaOptions[i]->pData && (pDialog->eaOptions[i]->pData->targetState == ContactDialogState_ViewOfferedMission || pDialog->eaOptions[i]->pData->action == ContactActionType_PerformOptionalAction)){
						break;
					}
				}
				
				// TODO - PlayerTooLow text
				
				if (eaSize(&pContactDef->noMissionsDialog) && (i == eaSize(&pDialog->eaOptions) || !eaSize(&pContactDef->missionListDialog))){
					contactdialog_SetTextFromDialogBlocks(pPlayerEnt, &pContactDef->noMissionsDialog, &pDialog->pchDialogText1, &pDialog->pchSoundToPlay, &pDialog->pchPhrasePath, REF_HANDLEPTR(pDialog->hDialogText1Formatter));
				} else {
					contactdialog_SetTextFromDialogBlocks(pPlayerEnt, &pContactDef->missionListDialog, &pDialog->pchDialogText1, &pDialog->pchSoundToPlay, &pDialog->pchPhrasePath, REF_HANDLEPTR(pDialog->hDialogText1Formatter));
				}
				contact_GetVoicePathFromPlayerCostume(contact_PlayerCostumeFromContactDialog(pDialog, pPlayerEnt), &pDialog->pchVoicePath);

				// If this is the first time entering the options list, attempt to skip it
				if (pOption->bFirstOptionsList){
					if (contactdialog_TrySkipOptionsList(pPlayerEnt, pContactDef, pDialog)){
						skip = true;
						break;
					}
				}
			}
			pNewOption = contactdialog_CreateDialogOption(pPlayerEnt, &pDialog->eaOptions, "OptionList.Exit", contact_GetDialogExitText(pContactDef), ContactDialogState_Exit, 0, eaMapVars);
			pNewOption->bIsDefaultBackOption = true;

		xcase ContactDialogState_OptionListFarewell:
			
			if (pContactDef)
			{
				contact_SetCameraSource(pPlayerEnt, pContactDef->eSourceType, pContactDef->pchSourceName, pContactDef->pchSourceSecondaryName, pDialog, pContactDef, pContactDef->filename, NULL);
			}

			// If this is a Single Dialog contact, and we're not entering the dialog for the first time, just stop all interaction
			if (pContactDef && contact_IsSingleScreen(pContactDef) && !pOption->bFirstOptionsList){
				interaction_EndInteractionAndDialog(entGetPartitionIdx(pPlayerEnt), pPlayerEnt, true, false, true);
				skip = true;
				break;
			}

			if (IS_HANDLE_ACTIVE(pDialog->hAuctionBrokerDef))
			{
				REMOVE_HANDLE(pDialog->hAuctionBrokerDef);
			}			

			pDialog->screenType = ContactScreenType_List;
			estrPrintf(&pDialog->pchListHeader, "%s", langTranslateMessage(entGetLanguage(pPlayerEnt), GET_REF(s_ContactMsgs.hDefaultListHeaderMsg)));
			if (pContactDef){

				contactdialog_CreateOptionsList(pPlayerEnt, pContactDef, &pDialog->eaOptions, pDialog->bPartialPermissions);
				//If we've been at this screen before and the only option is exit (added below), then just auto-exit:
				if (!pOption->bFirstOptionsList && !eaSize(&pDialog->eaOptions)){
					interaction_EndInteractionAndDialog(entGetPartitionIdx(pPlayerEnt), pPlayerEnt, true, false, true);
					skip = true;
					break;
				}


				if (eaSize(&pContactDef->exitDialog)){
					contactdialog_SetTextFromDialogBlocks(pPlayerEnt, &pContactDef->exitDialog, &pDialog->pchDialogText1, &pDialog->pchSoundToPlay, &pDialog->pchPhrasePath, REF_HANDLEPTR(pDialog->hDialogText1Formatter));
				} else {
					contactdialog_SetTextFromDialogBlocks(pPlayerEnt, &pContactDef->missionListDialog, &pDialog->pchDialogText1, &pDialog->pchSoundToPlay, &pDialog->pchPhrasePath, REF_HANDLEPTR(pDialog->hDialogText1Formatter));
				}
				contact_GetVoicePathFromPlayerCostume(contact_PlayerCostumeFromContactDialog(pDialog, pPlayerEnt), &pDialog->pchVoicePath);
			}
			pNewOption = contactdialog_CreateDialogOption(pPlayerEnt, &pDialog->eaOptions, "OptionList.Exit", contact_GetDialogExitText(pContactDef), ContactDialogState_Exit, 0, eaMapVars);
			pNewOption->bIsDefaultBackOption = true;

		xcase ContactDialogState_ContactInfo:

			pDialog->screenType = ContactScreenType_Buttons;
			if (pContactDef){
				contactdialog_SetTextFromDialogBlocks(pPlayerEnt, &pContactDef->infoDialog, &pDialog->pchDialogText1, &pDialog->pchSoundToPlay, &pDialog->pchPhrasePath, REF_HANDLEPTR(pDialog->hDialogText1Formatter));
				contact_GetVoicePathFromPlayerCostume(contact_PlayerCostumeFromContactDialog(pDialog, pPlayerEnt), &pDialog->pchVoicePath);
			}
			pNewOption = contactdialog_CreateDialogOption(pPlayerEnt, &pDialog->eaOptions, "ContactInfo.Back", GET_REF(s_ContactMsgs.hBackMsg), ContactDialogState_OptionList, 0, eaMapVars);
			pNewOption->bIsDefaultBackOption = true;

		xcase ContactDialogState_SpecialDialog:			
			pDialog->screenType = ContactScreenType_Buttons;
			pSpecialDialog = contact_SpecialDialogFromName(pContactDef, pOption->pchTargetDialogName);
			if(!pSpecialDialog){
				Errorf("Server selected nonexistent special dialog block '%s' for contact %s!", pOption->pchTargetDialogName, pContactDef->name);
			}

			if (pSpecialDialog){
				DialogBlock *pDialogBlock;

				// send the event
				eventsend_RecordDialogStart(pPlayerEnt, pContactDef, pOption->pchTargetDialogName);

				if (pSpecialDialog->eSourceType != ContactSourceType_None)
				{
					MissionDef *pOverridingMission;
					const char *pchFileName = pContactDef->filename;
					if (pOverridingMission = GET_REF(pSpecialDialog->overridingMissionDef))
					{
						pchFileName = pOverridingMission->filename;
					}
					contact_SetCameraSource(pPlayerEnt, pSpecialDialog->eSourceType, pSpecialDialog->pchSourceName, pSpecialDialog->pchSourceSecondaryName, pDialog, pContactDef, pchFileName, pSpecialDialog->name);
				}

				// Display Name
				if (pPlayerEnt && GET_REF(pSpecialDialog->displayNameMesg.hMessage)) {
					estrClear(&pDialog->pchContactDispName);
					langFormatGameDisplayMessage(entGetLanguage(pPlayerEnt), &pDialog->pchContactDispName, &pSpecialDialog->displayNameMesg, STRFMT_ENTITY(pPlayerEnt), STRFMT_MAPVARS(eaMapVars), STRFMT_END);
				}

				// Costume
				contactdialog_SetCostumeFromPrefs(pDialog, pPlayerEnt, contactEnt, &pSpecialDialog->costumePrefs);

				// Headshot style
				if ( pSpecialDialog->pchHeadshotStyleOverride )
				{
					pDialog->pchHeadshotStyleDef = allocAddString(pSpecialDialog->pchHeadshotStyleOverride);
				}

				// Determine the dialog to show
				dialogIndex = pOption->iSpecialDialogSubIndex;
				if(pSpecialDialog->bUsesLocalCondExpression)
				{
					while(dialogIndex < eaSize(&pSpecialDialog->dialogBlock) && pSpecialDialog->dialogBlock[dialogIndex]->condition && !contact_Evaluate(pPlayerEnt, pSpecialDialog->dialogBlock[dialogIndex]->condition, NULL))
					{
						dialogIndex++;
					}

					if(dialogIndex >= eaSize(&pSpecialDialog->dialogBlock))
					{
						dialogIndex = pOption->iSpecialDialogSubIndex;
					}
				}
				pDialogBlock = pSpecialDialog->dialogBlock[dialogIndex];

				// Set the animation list if there is one selected for this special dialog block
				if (pDialogBlock && IS_HANDLE_ACTIVE(pDialogBlock->hAnimList))
				{
					COPY_HANDLE(pDialog->hAnimListToPlayForActiveEntity, pDialogBlock->hAnimList);
				}

				// Set the cut-scene
				if (IS_HANDLE_ACTIVE(pSpecialDialog->hCutSceneDef))
				{
					COPY_HANDLE(pDialog->hCutSceneDef, pSpecialDialog->hCutSceneDef);
				}

				estrClear(&pDialog->pchDialogText1);
				REMOVE_HANDLE(pDialog->hDialogText1Formatter);
				langFormatGameDisplayMessage(entGetLanguage(pPlayerEnt), &pDialog->pchDialogText1, &pDialogBlock->displayTextMesg, STRFMT_ENTITY(pPlayerEnt), STRFMT_ENTITY_KEY("Nemesis", pNemesisEnt), STRFMT_MAPVARS(eaMapVars), STRFMT_END);
				pDialog->pchSoundToPlay = pDialogBlock->audioName;
				pDialog->pchSpecialDialogName = allocAddString(pSpecialDialog->name);
				pDialog->iSpecialDialogSubIndex = dialogIndex;
				if (IS_HANDLE_ACTIVE(pDialogBlock->hDialogFormatter))
				{
					COPY_HANDLE(pDialog->hDialogText1Formatter, pDialogBlock->hDialogFormatter);
				}				


				contact_GetSoundVoiceAndPhrasePath(pDialogBlock, contact_PlayerCostumeFromContactDialog(pDialog, pPlayerEnt), 
													&pDialog->pchVoicePath, &pDialog->pchPhrasePath);

				// Check flags
				if(pSpecialDialog->eFlags)
				{
					// Get team information
					Team* pTeam = NULL;
					if(pPlayerEnt && team_IsMember(pPlayerEnt)) 
					{
						pTeam = team_GetTeam(pPlayerEnt);
						if(pTeam)
						{
							// Force dialog on teammates
							if((pSpecialDialog->eFlags & SpecialDialogFlags_ForceOnTeam) && !pDialog->bForceOnTeamDone) {
								Entity **eaTeamMembers = NULL;
								contactdialog_ForceOnTeamDialogViewed(pPlayerEnt, pContactDef, pOption->pchTargetDialogName);
								team_GetOnMapEntsUnique(entGetPartitionIdx(pPlayerEnt), &eaTeamMembers, pTeam, false);
								if(eaTeamMembers) {
									int j;
									for(j = 0; j < eaSize(&eaTeamMembers); j++) {
										if(eaTeamMembers[j] != pPlayerEnt)
											contact_addQueuedContact(eaTeamMembers[j], pContactDef, pOption->pchTargetDialogName, pInteractInfo ? pInteractInfo->bPartialPermissions : false, pInteractInfo ? pInteractInfo->bRemotelyAccessing : false, contactEnt ? contact_PlayerCostumeFromContactDialog(pDialog, pPlayerEnt) : NULL, contactEnt ? pDialog->pchContactDispName : NULL, true);
									}
								}
								eaDestroy(&eaTeamMembers);

								pDialog->bForceOnTeamDone = true;
							}

							// Force dialog on teammates
							if (pSpecialDialog->eFlags & SpecialDialogFlags_CriticalDialog)
							{
								bShowToTeamMembers = true;
							}
						}
					}					
				}
			}

			// Create buttons for all actions
			contactdialog_CreateSpecialDialogOptions(pPlayerEnt, entity_GetSavedExpLevel(pPlayerEnt), pContactDef, pDialog, pSpecialDialog, true, eaMapVars);

		xcase ContactDialogState_BridgeOfficerOfferSelfOrTraining:
		{
			GameAccountDataExtract *pExtract = entity_GetCachedGameAccountDataExtract(pPlayerEnt);
			bool bMaxOfficers, bSameAllegiance;
			S32 iNumOfficers, iNumSameTypeOfficers;
			Entity* pEntSrc = entity_GetSubEntity(entGetPartitionIdx(pPlayerEnt), pPlayerEnt, pOption->iEntType, pOption->iEntID);
			Entity* pEntSpace = entity_GetPuppetEntityByType(pPlayerEnt, "Space", NULL, false, true);
			ItemDef* pItemDef = GET_REF(pOption->hItemDef);
			Item* pItem = (Item*)inv_trh_GetItemFromBag( ATR_EMPTY_ARGS, pEntSrc ? CONTAINER_NOCONST(Entity, pEntSrc) : CONTAINER_NOCONST(Entity, pPlayerEnt), pOption->iItemBagID, pOption->iItemSlot, pExtract );
			PetDef* pPetDef = pItemDef ? GET_REF(pItemDef->hPetDef) : NULL;
			PlayerCostume* pCostume = NULL;
			char* estrAllegiance = NULL; 
			char* estrName = NULL;
			char* estrDialogKey = NULL;
			const char* pchBaseDialogKey;
			const char* pchSpacePuppetName = pEntSpace && pEntSpace->pSaved ? pEntSpace->pSaved->savedName : NULL;
			AllegianceDef *pAllegiance = NULL, *pSubAllegiance = NULL, *pPetAllegiance = NULL;

			// WOLF[30Nov11] We have at least one report [STO-32452] of getting into this code and having the hPetDef on the pItemDef be not loaded.
			//  It's probably some sort of timing issue having to do with changing data, saving, and then trying to issue the command.
			//  We'll at least keep the crash from happening in that case. The item has already been awarded, so we are just skipping.
			//  the dialog that comes up about training, etc.
			if (pPetDef==NULL)
			{
				return;
			}

			//
			//	Get the pets name and costume
			//
			estrStackCreate(&estrName);
			estrStackCreate(&estrDialogKey);

			item_GetDisplayNameFromPetCostume(pPlayerEnt, pItem, &estrName, &pCostume);
			estrPrintf(&pDialog->pchContactDispName, "%s", estrName);

			//
			// Dialog setup
			//
			pDialog->screenType = ContactScreenType_Buttons;
			pDialog->iEntType = pOption->iEntType;
			pDialog->iEntID = pOption->iEntID;
			pDialog->iItemBagID = pOption->iItemBagID;
			pDialog->iItemSlot = pOption->iItemSlot;
			pDialog->bIsOfficerTrainer = true;
			
			if (pItem && GET_REF(pItem->hItem) == pItemDef)
				pDialog->iItemID = pItem->id;

			if (pCostume)
			{
				if ((!pItem->pSpecialProps) || (!pItem->pSpecialProps->pAlgoPet) || (!pItem->pSpecialProps->pAlgoPet->pCostume))
				{
					SET_HANDLE_FROM_STRING("PlayerCostume",(char*)pCostume->pcName,pDialog->hHeadshotOverride);
				}
				else
				{
					pDialog->pHeadshotOverride = StructClone(parse_PlayerCostume, pCostume);
				}
			}

			//load global headshot style setting for potential pets, if one exists
			if ( g_PetRestrictions.pchPotentialPetHeadshotStyle )
			{
				pDialog->pchHeadshotStyleDef = allocAddString(g_PetRestrictions.pchPotentialPetHeadshotStyle);
			}

			pAllegiance = GET_REF(pPlayerEnt->hAllegiance);
			pSubAllegiance = GET_REF(pPlayerEnt->hSubAllegiance);
			iNumOfficers = Entity_CountPets(pPlayerEnt, true, false, false);
			iNumSameTypeOfficers = Entity_CountPetsOfType(pPlayerEnt,petdef_GetCharacterClassType(pPetDef),false);
			bMaxOfficers = Officer_GetMaxAllowedPets( pPlayerEnt, pAllegiance, pExtract ) <= iNumOfficers;
			pPetAllegiance = GET_REF(pPetDef->hAllegiance);
			bSameAllegiance = (!pAllegiance) || (!pPetAllegiance) || pAllegiance == pPetAllegiance || (pSubAllegiance && pSubAllegiance == pPetAllegiance);
			if (!bSameAllegiance)
			{
				estrStackCreate(&estrAllegiance);
				langFormatMessage(entGetLanguage(pPlayerEnt),&estrAllegiance,GET_REF(pPetAllegiance->displayNameMsg.hMessage), STRFMT_END);
			}

			langFormatMessageKey( entGetLanguage(pPlayerEnt), &pDialog->pchDialogHeader, "OfficerTraining_DialogHeader", STRFMT_END );

			if (bSameAllegiance)
			{
				if (estrName && *estrName)
				{
					if (pOption->bFirstInteract)
						pchBaseDialogKey = "OfficerTraining_DialogText1_Introduction";
					else
						pchBaseDialogKey = "OfficerTraining_DialogText1";
					contact_GetDialogKeyForAllegiance(&estrDialogKey,pchBaseDialogKey,pPetAllegiance);
					langFormatMessageKey( entGetLanguage(pPlayerEnt), &pDialog->pchDialogText1, estrDialogKey, STRFMT_STRING("Name",estrName), STRFMT_END );
				}
				else
				{
					contact_GetDialogKeyForAllegiance(&estrDialogKey,"OfficerTraining_DialogText1_NoName",pPetAllegiance);
					langFormatMessageKey( entGetLanguage(pPlayerEnt), &pDialog->pchDialogText1, estrDialogKey, STRFMT_END );
				}
			}
			else
			{
				if (estrName && *estrName)
				{
					contact_GetDialogKeyForAllegiance(&estrDialogKey,"OfficerTraining_DialogText1_NotAllegiance",pPetAllegiance);
					langFormatMessageKey( entGetLanguage(pPlayerEnt), &pDialog->pchDialogText1, estrDialogKey, STRFMT_STRING("Name",estrName), STRFMT_STRING("Allegiance",estrAllegiance), STRFMT_END );
				}
				else
				{
					contact_GetDialogKeyForAllegiance(&estrDialogKey,"OfficerTraining_DialogText1_NotAllegiance_NoName",pPetAllegiance);
					langFormatMessageKey( entGetLanguage(pPlayerEnt), &pDialog->pchDialogText1, estrDialogKey, STRFMT_STRING("Allegiance",estrAllegiance), STRFMT_END );
				}
			}

			if (!bSameAllegiance)
			{
				contact_GetDialogKeyForAllegiance(&estrDialogKey,"OfficerTraining_DialogText2_NotAllegiance",pPetAllegiance);
				langFormatMessageKey( entGetLanguage(pPlayerEnt), &pDialog->pchDialogText2, estrDialogKey, STRFMT_END );
			}
			else if (bMaxOfficers)
			{
				contact_GetDialogKeyForAllegiance(&estrDialogKey,"OfficerTraining_DialogText2_MaxOfficers",pPetAllegiance);
				langFormatMessageKey( entGetLanguage(pPlayerEnt), &pDialog->pchDialogText2, estrDialogKey, STRFMT_STRING("SpacePuppetName",pchSpacePuppetName), STRFMT_INT("CanTrain", !pPetDef->bDisableTrainingFromPet), STRFMT_END );
			}
			else if (iNumSameTypeOfficers == 0)
			{
				if (pOption->bFirstInteract)
					pchBaseDialogKey = "OfficerTraining_DialogText2_FirstOfficer_Introduction";
				else
					pchBaseDialogKey = "OfficerTraining_DialogText2_FirstOfficer";
				contact_GetDialogKeyForAllegiance(&estrDialogKey,pchBaseDialogKey,pPetAllegiance);
				langFormatMessageKey( entGetLanguage(pPlayerEnt), &pDialog->pchDialogText2, estrDialogKey, STRFMT_STRING("SpacePuppetName",pchSpacePuppetName), STRFMT_INT("CanTrain", !pPetDef->bDisableTrainingFromPet), STRFMT_END );
			}
			else
			{
				if (pOption->bFirstInteract)
					pchBaseDialogKey = "OfficerTraining_DialogText2_Introduction";
				else
					pchBaseDialogKey = "OfficerTraining_DialogText2";
				contact_GetDialogKeyForAllegiance(&estrDialogKey,pchBaseDialogKey,pPetAllegiance);
				langFormatMessageKey( entGetLanguage(pPlayerEnt), &pDialog->pchDialogText2, estrDialogKey, STRFMT_STRING("SpacePuppetName",pchSpacePuppetName), STRFMT_INT("CanTrain", !pPetDef->bDisableTrainingFromPet), STRFMT_END );
			}

			if (bSameAllegiance && iNumSameTypeOfficers > 0 && !pPetDef->bDisableTrainingFromPet)
			{
				// Add "train" option
				pNewOption = contactdialog_CreateDialogOption(pPlayerEnt, &pDialog->eaOptions, "OptionsList.TrainPowers", GET_REF(s_ContactMsgs.hTrainPowersMsg), ContactDialogState_PowerStoreFromItem, ContactActionType_PowerStoreFromItem, eaMapVars);
			}

			if (bSameAllegiance && !bMaxOfficers)
			{
				// Add "join" option
				pNewOption = contactdialog_CreateDialogOption(pPlayerEnt, &pDialog->eaOptions, "PotentialBridgeOfficer.Join", GET_REF(s_ContactMsgs.hPetJoinMsg), ContactDialogState_Exit, ContactActionType_GivePetFromItem, eaMapVars);
			}

			pNewOption = contactdialog_CreateDialogOption(pPlayerEnt, &pDialog->eaOptions, "OptionList.Exit", GET_REF(s_ContactMsgs.hNotNowMsg), ContactDialogState_Exit, 0, eaMapVars);

			estrDestroy(&estrAllegiance);
			estrDestroy(&estrName);
			estrDestroy(&estrDialogKey);
		}
	
		xcase ContactDialogState_ViewOfferedNamespaceMission:
			{
				NamespacedMissionContactData *pData = pOption->pNamespacedMissionContactData;
				Message *pAcceptMsg = GET_REF(s_ContactMsgs.hAcceptMsg);
				Message *pDeclineMsg = GET_REF(s_ContactMsgs.hDeclineMsg);

				pDialog->screenType = ContactScreenType_Buttons;

				// Set the override entity
				if (pData)
				{
					// Pet Contact List
					PetContactList* pList = GET_REF(pData->hPetContactList);
					Entity* pPet = NULL;
					CritterCostume* pCritterCostume = NULL;
					
					PetContactList_GetPetOrCostume(pPlayerEnt, pList, NULL, &pPet, NULL, &pCritterCostume);
					
					if (pPet && pPet->myContainerID!=0)
					{
						// Pet found
						pDialog->iHeadshotOverridePetID = pPet->myContainerID;
					}
					else if (pCritterCostume!=NULL)
					{
						// No pet found, use default critter costume if we found one
						COPY_HANDLE(pDialog->hHeadshotOverride, pCritterCostume->hCostumeRef);
					}
					else
					{
						COPY_HANDLE(pDialog->hHeadshotOverride, pData->hContactCostume);
					}

					// Add the detail text
					langFormatGameString(entGetLanguage(pPlayerEnt), &pDialog->pchDialogText2, pData->pchMissionDetails, STRFMT_ENTITY(pPlayerEnt), STRFMT_END);
				}
				// Add an accept option
				pNewOption = contactdialog_CreateDialogOption(pPlayerEnt, &pDialog->eaOptions, "ViewOfferedMission.Accept", pAcceptMsg, ContactDialogState_Exit, ContactActionType_AcceptMissionOffer, eaMapVars);
				
				if (pNewOption && pNewOption->pData){
					COPY_HANDLE(pNewOption->pData->hRootMission, pOption->hRootMission);
					if (pOption->pNamespacedMissionContactData){
						pNewOption->pData->pNamespacedMissionContactData = StructClone(parse_NamespacedMissionContactData, pOption->pNamespacedMissionContactData);
					}
				}
				// Add a decline option
				pNewOption = contactdialog_CreateDialogOption(pPlayerEnt, &pDialog->eaOptions, "ViewOfferedMission.Back", pDeclineMsg, ContactDialogState_Exit, 0, eaMapVars);
				pNewOption->bIsDefaultBackOption = true;
			}

		xcase ContactDialogState_ViewOfferedMission:
			{
				MissionDef *pMissionDef = IS_HANDLE_ACTIVE(pOption->hRootMissionSecondary) ? GET_REF(pOption->hRootMissionSecondary) : GET_REF(pOption->hRootMission);
				Mission *pMission = pMissionInfo ? mission_GetMissionFromDef(pMissionInfo, pMissionDef) : NULL;
				//SIP TODO: add support for UGC missions to add fancy mission offer treatment to UGC Search Agents.
				//UGCProjectVersion *pUGCProjectVersion = UGCProject_GetMostRecentPublishedVersion(/*Store UGC Project ID and call */);	
				ContactMissionOffer *pMissionOffer = pContactDef?contact_GetMissionOffer(pContactDef, pPlayerEnt, pMissionDef):NULL;
				Message *pAcceptMsg = GET_REF(s_ContactMsgs.hAcceptMsg);
				Message *pDeclineMsg = GET_REF(s_ContactMsgs.hDeclineMsg);
				U32 seed;
				int iPlayerLevel;
				MissionCreditType eAllowedCreditType;
				U32 uDaysSubscribed = entity_GetDaysSubscribed(pPlayerEnt);
				SpecialDialogBlock *pCompletedSpecialDialog = pContactDef && pOption->pchCompletedDialogName ? contact_SpecialDialogFromName(pContactDef, pOption->pchCompletedDialogName) : NULL;
				RewardGatedDataInOut *pGatedData = NULL;

				// If the mission can't be offered at all, cancel
				if (!missiondef_CanBeOfferedAtAll(pPlayerEnt, pMissionDef, NULL, NULL, &eAllowedCreditType)
					 || !contactdialog_ValidateMissionOfferParams(pContactDef, eAllowedCreditType, pOption->pMissionOfferParams))
				{
					interaction_EndInteractionAndDialog(entGetPartitionIdx(pPlayerEnt), pPlayerEnt, false, true, true);
					skip = true;
					break;
				}

				if (pCompletedSpecialDialog && (pCompletedSpecialDialog->eFlags & SpecialDialogFlags_CriticalDialog))
				{
					bShowToTeamMembers = true;
				}

				// Set the MissionCreditType on the Dialog so that the offer will display correctly
				if (pOption->pMissionOfferParams){
					pDialog->eMissionCreditType = pOption->pMissionOfferParams->eCreditType;
				}

				iPlayerLevel = entity_GetSavedExpLevel(pPlayerEnt);

				if (g_ContactConfig.bMissionOffersUseDedicatedScreenType)
				{
					pDialog->screenType = ContactScreenType_MissionOffer;
				}
				else
				{
					pDialog->screenType = ContactScreenType_Buttons;
				}				

				if (pMissionDef)
				{
					// Set the mission UI type information
					contact_FillDialogMissionUITypeInfo(pPlayerEnt, pMissionDef, pDialog);

					// TODO - ContactMissionOffer offer text?
					if (GET_REF(pMissionDef->displayNameMsg.hMessage)){
						if (pDialog->eMissionCreditType == MissionCreditType_Flashback){
							langFormatGameMessage(entGetLanguage(pPlayerEnt), &pDialog->pchDialogHeader, GET_REF(g_MissionSystemMsgs.hFlashbackDisplayName), STRFMT_DISPLAYMESSAGE("MissionName", pMissionDef->displayNameMsg), STRFMT_END);
						} else {
							langFormatGameDisplayMessage(entGetLanguage(pPlayerEnt), &pDialog->pchDialogHeader, &pMissionDef->displayNameMsg, STRFMT_ENTITY(pPlayerEnt), STRFMT_MAPVARS(eaMapVars), STRFMT_END);
						}
					}
					
					if (GET_REF(pMissionDef->detailStringMsg.hMessage)) {
						if(gConf.bSwapMissionOfferDialogText) {
							langFormatGameDisplayMessage(entGetLanguage(pPlayerEnt), &pDialog->pchDialogText2, &pMissionDef->detailStringMsg, STRFMT_ENTITY(pPlayerEnt), STRFMT_ENTITY_KEY("Nemesis", pNemesisEnt), STRFMT_MAPVARS(eaMapVars), STRFMT_END);
						} else {
							langFormatGameDisplayMessage(entGetLanguage(pPlayerEnt), &pDialog->pchDialogText1, &pMissionDef->detailStringMsg, STRFMT_ENTITY(pPlayerEnt), STRFMT_ENTITY_KEY("Nemesis", pNemesisEnt), STRFMT_MAPVARS(eaMapVars), STRFMT_END);
						}
					}

					if (GET_REF(s_ContactMsgs.hMissionObjectivesHeaderMsg))
						estrPrintf(&pDialog->pchDialogHeader2, "%s", langTranslateMessage(entGetLanguage(pPlayerEnt), GET_REF(s_ContactMsgs.hMissionObjectivesHeaderMsg)));
					
					if (GET_REF(pMissionDef->summaryMsg.hMessage)) {
						if(gConf.bSwapMissionOfferDialogText) {
							langFormatGameDisplayMessage(entGetLanguage(pPlayerEnt), &pDialog->pchDialogText1, &pMissionDef->summaryMsg, STRFMT_ENTITY(pPlayerEnt), STRFMT_ENTITY_KEY("Nemesis", pNemesisEnt), STRFMT_MAPVARS(eaMapVars), STRFMT_END);
						} else {
							langFormatGameDisplayMessage(entGetLanguage(pPlayerEnt), &pDialog->pchDialogText2, &pMissionDef->summaryMsg, STRFMT_ENTITY(pPlayerEnt), STRFMT_ENTITY_KEY("Nemesis", pNemesisEnt), STRFMT_MAPVARS(eaMapVars), STRFMT_END);
						}
					}

					// If this is a timed mission or escort, add warning
					pDialog->uMissionTimeLimit = missiondef_OnStartTimerLengthRecursive(pMissionDef);
					pDialog->eMissionLockoutType = pMissionDef->lockoutType;
					if (pDialog->uMissionTimeLimit && pOption->pMissionOfferParams && pOption->pMissionOfferParams->uTimerStartTime){
						pDialog->uMissionExpiredTime = pOption->pMissionOfferParams->uTimerStartTime + pDialog->uMissionTimeLimit;
					}

					// Add team size info
					pDialog->iTeamSize = pMissionDef->iSuggestedTeamSize;
					pDialog->bScalesForTeam = pMissionDef->bScalesForTeamSize;
					
					SET_HANDLE_FROM_REFERENT(g_MissionDictionary, pMissionDef, pDialog->hRootMissionDef);

					// Add sound
					pDialog->pchSoundToPlay = allocAddString(pMissionDef->pchSoundOnContactOffer);
				}

				if (pMissionOffer && GET_REF(pMissionOffer->acceptStringMesg.hMessage)){
					pAcceptMsg = GET_REF(pMissionOffer->acceptStringMesg.hMessage);
				}
				if (pMissionOffer && GET_REF(pMissionOffer->declineStringMesg.hMessage)){
					pDeclineMsg = GET_REF(pMissionOffer->declineStringMesg.hMessage);
				}

				// Add Accept option
				if (pMissionOffer && pMissionOffer->pchAcceptTargetDialog)
				{
					ContactMissionOffer *pTargetedMissionOffer = contact_MissionOfferFromSpecialDialogName(pContactDef, pMissionOffer->pchAcceptTargetDialog);

					if (pTargetedMissionOffer)
					{						
						pNewOption = contact_CreateDialogOptionForMissionOfferTargetedFromSpecialDialog(pTargetedMissionOffer, pContactDef, pPlayerEnt, iPlayerLevel, pDialog, pAcceptMsg, ContactActionType_AcceptMissionOffer, eaMapVars, GET_REF(pMissionOffer->hAcceptDialogFormatter));
						pNewOption->pchKey = StructAllocString("ViewOfferedMission.Accept");

						if (pNewOption->pData)
						{
							// When we create the new option, mission fields point to the new mission. We want the store new mission info in the secondary fields.

							// Store new mission data in secondary fields
							COPY_HANDLE(pNewOption->pData->hRootMissionSecondary, pNewOption->pData->hRootMission);
							pNewOption->pData->pchSubMissionNameSecondary = pNewOption->pData->pchSubMissionName;

							// Set the original mission data in the primary fields
							pNewOption->pData->pchSubMissionName = pOption->pchSubMissionName;
							SET_HANDLE_FROM_REFERENT(g_MissionDictionary, pMissionDef, pNewOption->pData->hRootMission);
						}						
					}
					else
					{
						pNewOption = contactdialog_CreateDialogOptionEx(pPlayerEnt, &pDialog->eaOptions, "ViewOfferedMission.Accept", pAcceptMsg, ContactDialogState_SpecialDialog, ContactActionType_AcceptMissionOffer, eaMapVars, GET_REF(pMissionOffer->hAcceptDialogFormatter));

						if (pNewOption && pNewOption->pData)
						{
							pNewOption->pData->pchTargetDialogName =  pMissionOffer->pchAcceptTargetDialog;
						}
					}
				}
				else
				{
					if (pContactDef){
						pNewOption = contactdialog_CreateDialogOptionEx(pPlayerEnt, &pDialog->eaOptions, "ViewOfferedMission.Accept", pAcceptMsg, ContactDialogState_OptionListFarewell, ContactActionType_AcceptMissionOffer, eaMapVars, pMissionOffer ? GET_REF(pMissionOffer->hAcceptDialogFormatter) : NULL);
					} else {
						pNewOption = contactdialog_CreateDialogOptionEx(pPlayerEnt, &pDialog->eaOptions, "ViewOfferedMission.Accept", pAcceptMsg, ContactDialogState_Exit, ContactActionType_AcceptMissionOffer, eaMapVars, pMissionOffer ? GET_REF(pMissionOffer->hAcceptDialogFormatter) : NULL);
					}
				}

				if (pNewOption && pNewOption->pData){
					SET_HANDLE_FROM_REFERENT(g_MissionDictionary, pMissionDef, pNewOption->pData->hRootMission);
					if(pContactDef) {
						SET_HANDLE_FROM_REFERENT(g_ContactDictionary, pContactDef, pNewOption->pData->hMissionListContactDef);
					}
					if (pOption->pMissionOfferParams){
						pNewOption->pData->pMissionOfferParams = StructClone(parse_MissionOfferParams, pOption->pMissionOfferParams);
					}
				}

				// Add Decline option
				if (pMissionOffer && pMissionOffer->pchDeclineTargetDialog)
				{
					ContactMissionOffer *pTargetedMissionOffer = contact_MissionOfferFromSpecialDialogName(pContactDef, pMissionOffer->pchDeclineTargetDialog);

					if (pTargetedMissionOffer)
					{
						pNewOption = contact_CreateDialogOptionForMissionOfferTargetedFromSpecialDialog(pTargetedMissionOffer, pContactDef, pPlayerEnt, iPlayerLevel, pDialog, pDeclineMsg, ContactActionType_None, eaMapVars, GET_REF(pMissionOffer->hDeclineDialogFormatter));
						pNewOption->pchKey = StructAllocString("ViewOfferedMission.Back");

						if (pNewOption->pData)
						{
							// When we create the new option, mission fields point to the new mission. We want the store new mission info in the secondary fields.

							// Store new mission data in secondary fields
							COPY_HANDLE(pNewOption->pData->hRootMissionSecondary, pNewOption->pData->hRootMission);
							pNewOption->pData->pchSubMissionNameSecondary = pNewOption->pData->pchSubMissionName;

							// Set the original mission data in the primary fields
							pNewOption->pData->pchSubMissionName = pOption->pchSubMissionName;
							SET_HANDLE_FROM_REFERENT(g_MissionDictionary, pMissionDef, pNewOption->pData->hRootMission);
						}
					}
					else
					{
						pNewOption = contactdialog_CreateDialogOptionEx(pPlayerEnt, &pDialog->eaOptions, "ViewOfferedMission.Back", pDeclineMsg, ContactDialogState_SpecialDialog, ContactActionType_None, eaMapVars, GET_REF(pMissionOffer->hDeclineDialogFormatter));

						if (pNewOption && pNewOption->pData)
						{
							pNewOption->pData->pchTargetDialogName =  pMissionOffer->pchDeclineTargetDialog;
						}
					}
				}
				else
				{
					if (pContactDef){
						pNewOption = contactdialog_CreateDialogOptionEx(pPlayerEnt, &pDialog->eaOptions, "ViewOfferedMission.Back", pDeclineMsg, ContactDialogState_OptionList, 0, eaMapVars, pMissionOffer ? GET_REF(pMissionOffer->hDeclineDialogFormatter) : NULL);
					} else {
						pNewOption = contactdialog_CreateDialogOptionEx(pPlayerEnt, &pDialog->eaOptions, "ViewOfferedMission.Back", pDeclineMsg, ContactDialogState_Exit, 0, eaMapVars, pMissionOffer ? GET_REF(pMissionOffer->hDeclineDialogFormatter) : NULL);
					}
				}
				pNewOption->bIsDefaultBackOption = true;

				// Generate OnReturn rewards
				seed = mission_GetRewardSeed(pPlayerEnt, pMission, pMissionDef);

				// create reward gated data
				pGatedData = mission_trh_CreateRewardGatedData(CONTAINER_NOCONST(Entity, pPlayerEnt));

				reward_GenerateMissionActionRewards(entGetPartitionIdx(pPlayerEnt), pPlayerEnt, pMissionDef, MissionState_TurnedIn, &pDialog->eaRewardBags, &seed,
					pDialog->eMissionCreditType, 0, /*time_level=*/0, 0, 0, eRecruitType, /*bUGCProject=*/false, false, false, false, false, /*bGenerateChestRewards=*/true, &rewardContextData, 0, pGatedData);

				// destroy gated reward info (we are done with it)
				if(pGatedData)
				{
					StructDestroy(parse_RewardGatedDataInOut, pGatedData);
				}

				if (contactdialog_InventoryBagsHaveNonChoosableRewards(&pDialog->eaRewardBags))
					estrPrintf(&pDialog->pchRewardsHeader, "%s", langTranslateMessage(entGetLanguage(pPlayerEnt), GET_REF(s_ContactMsgs.hRewardsHeaderMsg)));
				if (contactdialog_InventoryBagsHaveChoosableRewards(&pDialog->eaRewardBags))
					estrPrintf(&pDialog->pchOptionalRewardsHeader, "%s", langTranslateMessage(entGetLanguage(pPlayerEnt), GET_REF(s_ContactMsgs.hOptionalRewardsHeaderMsg)));
			}

		xcase ContactDialogState_ViewInProgressMission:
			{
				MissionDef *pMissionDef = IS_HANDLE_ACTIVE(pOption->hRootMissionSecondary) ? GET_REF(pOption->hRootMissionSecondary) : GET_REF(pOption->hRootMission);
				ContactMissionOffer *pMissionOffer = pContactDef ? contact_GetMissionOffer(pContactDef, pPlayerEnt, pMissionDef) : NULL;
				SpecialDialogBlock *pCompletedSpecialDialog = pContactDef && pOption->pchCompletedDialogName ? contact_SpecialDialogFromName(pContactDef, pOption->pchCompletedDialogName) : NULL;

				if (pCompletedSpecialDialog && (pCompletedSpecialDialog->eFlags & SpecialDialogFlags_CriticalDialog))
				{
					bShowToTeamMembers = true;
				}

				pDialog->screenType = ContactScreenType_Buttons;
				if (pMissionDef)
				{
					// Set the mission UI type information
					contact_FillDialogMissionUITypeInfo(pPlayerEnt, pMissionDef, pDialog);

					if (GET_REF(pMissionDef->displayNameMsg.hMessage))
						langFormatGameDisplayMessage(entGetLanguage(pPlayerEnt), &pDialog->pchDialogHeader, &pMissionDef->displayNameMsg, STRFMT_ENTITY(pPlayerEnt), STRFMT_MAPVARS(eaMapVars), STRFMT_END);
					
					if(pMissionOffer){
						contactdialog_SetTextFromDialogBlocks(pPlayerEnt, &pMissionOffer->inProgressDialog, &pDialog->pchDialogText1, &pDialog->pchSoundToPlay, &pDialog->pchPhrasePath, REF_HANDLEPTR(pDialog->hDialogText1Formatter));
						contact_GetVoicePathFromPlayerCostume(contact_PlayerCostumeFromContactDialog(pDialog, pPlayerEnt), &pDialog->pchVoicePath);
					}else{
						langFormatGameDisplayMessage(entGetLanguage(pPlayerEnt), &pDialog->pchDialogText1, &pMissionDef->summaryMsg, STRFMT_ENTITY(pPlayerEnt), STRFMT_ENTITY_KEY("Nemesis", pNemesisEnt), STRFMT_MAPVARS(eaMapVars), STRFMT_END);
					}

					SET_HANDLE_FROM_REFERENT(g_MissionDictionary, pMissionDef, pDialog->hRootMissionDef);
				}

				pNewOption = contactdialog_CreateDialogOption(pPlayerEnt, &pDialog->eaOptions, "ViewInProgressMission.Back", GET_REF(s_ContactMsgs.hBackMsg), ContactDialogState_OptionList, 0, eaMapVars);
				pNewOption->bIsDefaultBackOption = true;
			}

		xcase ContactDialogState_ViewFailedMission:
			{
				MissionDef *pMissionDef = IS_HANDLE_ACTIVE(pOption->hRootMissionSecondary) ? GET_REF(pOption->hRootMissionSecondary) : GET_REF(pOption->hRootMission);
				Mission *pMission = pMissionInfo ? mission_GetMissionFromDef(pMissionInfo, pMissionDef) : NULL;
				ContactMissionOffer *pMissionOffer = pContactDef ? contact_GetMissionOffer(pContactDef, pPlayerEnt, pMissionDef) : NULL;
				SpecialDialogBlock *pCompletedSpecialDialog = pContactDef && pOption->pchCompletedDialogName ? contact_SpecialDialogFromName(pContactDef, pOption->pchCompletedDialogName) : NULL;

				if (pCompletedSpecialDialog && (pCompletedSpecialDialog->eFlags & SpecialDialogFlags_CriticalDialog))
				{
					bShowToTeamMembers = true;
				}

				pDialog->screenType = ContactScreenType_Buttons;
				if (pMissionDef)
				{
					// Set the mission UI type information
					contact_FillDialogMissionUITypeInfo(pPlayerEnt, pMissionDef, pDialog);

					if (GET_REF(pMissionDef->displayNameMsg.hMessage))
						langFormatGameDisplayMessage(entGetLanguage(pPlayerEnt), &pDialog->pchDialogHeader, &pMissionDef->displayNameMsg, STRFMT_ENTITY(pPlayerEnt), STRFMT_MAPVARS(eaMapVars), STRFMT_END);
					
					if(pMissionOffer){
						contactdialog_SetTextFromDialogBlocks(pPlayerEnt, &pMissionOffer->failureDialog, &pDialog->pchDialogText1, &pDialog->pchSoundToPlay, &pDialog->pchPhrasePath, REF_HANDLEPTR(pDialog->hDialogText1Formatter));
						contact_GetVoicePathFromPlayerCostume(contact_PlayerCostumeFromContactDialog(pDialog, pPlayerEnt), &pDialog->pchVoicePath);
					} else {
						U32 seed;
						int iPlayerLevel;
						bool bCanBeOffered;
						U32 uDaysSubscribed = entity_GetDaysSubscribed(pPlayerEnt);

						bCanBeOffered = missiondef_CanBeOfferedAtAll(pPlayerEnt, pMissionDef, NULL, NULL, &pDialog->eMissionCreditType);

						// Non-Primary mission credit is only allowed if this was a Shared Mission
						if (!bCanBeOffered || (pDialog->eMissionCreditType != MissionCreditType_Primary && (!pOption->pMissionOfferParams || !pOption->pMissionOfferParams->uSharerID))){
							interaction_EndInteractionAndDialog(entGetPartitionIdx(pPlayerEnt), pPlayerEnt, false, true, true);
							skip = true;
							break;
						}

						// If there is no Mission Offer here, this is probably a shared mission, so we should make it look like a new mission offer
						// Not sure this is the right thing to do but it should look OK
						
						if (GET_REF(pMissionDef->detailStringMsg.hMessage))
							langFormatGameDisplayMessage(entGetLanguage(pPlayerEnt), &pDialog->pchDialogText1, &pMissionDef->detailStringMsg, STRFMT_ENTITY(pPlayerEnt), STRFMT_ENTITY_KEY("Nemesis", pNemesisEnt), STRFMT_MAPVARS(eaMapVars), STRFMT_END);

						if (GET_REF(s_ContactMsgs.hMissionObjectivesHeaderMsg))
							estrPrintf(&pDialog->pchDialogHeader2, "%s", langTranslateMessage(entGetLanguage(pPlayerEnt), GET_REF(s_ContactMsgs.hMissionObjectivesHeaderMsg)));
						
						if (GET_REF(pMissionDef->summaryMsg.hMessage))
							langFormatGameDisplayMessage(entGetLanguage(pPlayerEnt), &pDialog->pchDialogText2, &pMissionDef->summaryMsg, STRFMT_ENTITY(pPlayerEnt), STRFMT_ENTITY_KEY("Nemesis", pNemesisEnt), STRFMT_MAPVARS(eaMapVars), STRFMT_END);

						// Generate OnReturn rewards
						seed = mission_GetRewardSeed(pPlayerEnt, pMission, pMissionDef);
						iPlayerLevel = entity_GetSavedExpLevel(pPlayerEnt);
						reward_GenerateMissionActionRewards(entGetPartitionIdx(pPlayerEnt), pPlayerEnt, pMissionDef, MissionState_TurnedIn, &pDialog->eaRewardBags, &seed,
							pDialog->eMissionCreditType, 0, /*time_level=*/0, 0, 0, eRecruitType, /*bUGCProject=*/false, false, false, false, false, /*bGenerateChestRewards=*/false, &rewardContextData, 0, /* RewardGatedDataInOut *pGatedData */ NULL);
						if (contactdialog_InventoryBagsHaveNonChoosableRewards(&pDialog->eaRewardBags))
							estrPrintf(&pDialog->pchRewardsHeader, "%s", langTranslateMessage(entGetLanguage(pPlayerEnt), GET_REF(s_ContactMsgs.hRewardsHeaderMsg)));
						if (contactdialog_InventoryBagsHaveChoosableRewards(&pDialog->eaRewardBags))
							estrPrintf(&pDialog->pchOptionalRewardsHeader, "%s", langTranslateMessage(entGetLanguage(pPlayerEnt), GET_REF(s_ContactMsgs.hOptionalRewardsHeaderMsg)));
					}

					// If this is a timed mission or escort, add warning
					pDialog->uMissionTimeLimit = missiondef_OnStartTimerLengthRecursive(pMissionDef);
					pDialog->eMissionLockoutType = pMissionDef->lockoutType;
					if (pDialog->uMissionTimeLimit && pOption->pMissionOfferParams && pOption->pMissionOfferParams->uTimerStartTime){
						pDialog->uMissionExpiredTime = pOption->pMissionOfferParams->uTimerStartTime + pDialog->uMissionTimeLimit;
					}

					// Add team size info
					pDialog->iTeamSize = pMissionDef->iSuggestedTeamSize;
					pDialog->bScalesForTeam = pMissionDef->bScalesForTeamSize;

					SET_HANDLE_FROM_REFERENT(g_MissionDictionary, pMissionDef, pDialog->hRootMissionDef);
				} else {
					interaction_EndInteractionAndDialog(entGetPartitionIdx(pPlayerEnt), pPlayerEnt, false, true, true);
					skip = true;
					break;
				}

				// Create Restart option
				if (pContactDef){
					pNewOption = contactdialog_CreateDialogOption(pPlayerEnt, &pDialog->eaOptions, "ViewFailedMission.Restart", GET_REF(s_ContactMsgs.hRestartMsg), ContactDialogState_OptionListFarewell, ContactActionType_RestartMission, eaMapVars);
				} else {
					pNewOption = contactdialog_CreateDialogOption(pPlayerEnt, &pDialog->eaOptions, "ViewFailedMission.Restart", GET_REF(s_ContactMsgs.hRestartMsg), ContactDialogState_Exit, ContactActionType_RestartMission, eaMapVars);
				}
				if (pNewOption && pNewOption->pData){
					SET_HANDLE_FROM_REFERENT(g_MissionDictionary, pMissionDef, pNewOption->pData->hRootMission);
					if (pOption->pMissionOfferParams){
						pNewOption->pData->pMissionOfferParams = StructClone(parse_MissionOfferParams, pOption->pMissionOfferParams);
					}
				}

				// Create Back option
				if (pContactDef){
					pNewOption = contactdialog_CreateDialogOption(pPlayerEnt, &pDialog->eaOptions, "ViewFailedMission.Back", GET_REF(s_ContactMsgs.hBackMsg), ContactDialogState_OptionList, 0, eaMapVars);
				} else {
					pNewOption = contactdialog_CreateDialogOption(pPlayerEnt, &pDialog->eaOptions, "ViewFailedMission.Back", GET_REF(s_ContactMsgs.hBackMsg), ContactDialogState_Exit, 0, eaMapVars);
				}
				pNewOption->bIsDefaultBackOption = true;
			}
		xcase ContactDialogState_ViewCompleteMission:
			{
				MissionDef *pMissionDef = IS_HANDLE_ACTIVE(pOption->hRootMissionSecondary) ? GET_REF(pOption->hRootMissionSecondary) : GET_REF(pOption->hRootMission);
				ContactMissionOffer *pMissionOffer = pContactDef ? contact_GetMissionOffer(pContactDef, pPlayerEnt, pMissionDef) : NULL;
				Mission *pMission = pMissionInfo ? mission_GetMissionFromDef(pMissionInfo, pMissionDef) : NULL;
				Message *pRewardAcceptMsg = pMissionOffer && GET_REF(pMissionOffer->rewardAcceptMesg.hMessage) ? 
					GET_REF(pMissionOffer->rewardAcceptMesg.hMessage) : GET_REF(s_ContactMsgs.hContinueMsg);
				Message *pRewardChooseMsg = pMissionOffer && GET_REF(pMissionOffer->rewardChooseMesg.hMessage) ? 
					GET_REF(pMissionOffer->rewardChooseMesg.hMessage) : GET_REF(s_ContactMsgs.hCollectRewardMsg);
				Message *pRewardAbortMsg = pMissionOffer && GET_REF(pMissionOffer->rewardAbortMesg.hMessage) ? 
					GET_REF(pMissionOffer->rewardAbortMesg.hMessage) : NULL;
				SpecialDialogBlock *pCompletedSpecialDialog = pContactDef && pOption->pchCompletedDialogName ? contact_SpecialDialogFromName(pContactDef, pOption->pchCompletedDialogName) : NULL;
				
				U32 seed;
				int iPlayerLevel;
				U32 uDaysSubscribed = entity_GetDaysSubscribed(pPlayerEnt);
				RewardGatedDataInOut *pGatedData = NULL;

				if(!pMissionDef || !pMissionOffer || !pMission){
					interaction_EndInteractionAndDialog(entGetPartitionIdx(pPlayerEnt), pPlayerEnt, false, true, true);
					skip = true;
					break;
				}

				if (pCompletedSpecialDialog && (pCompletedSpecialDialog->eFlags & SpecialDialogFlags_CriticalDialog))
				{
					bShowToTeamMembers = true;
				}

				if (g_ContactConfig.bMissionTurnInsUseDedicatedScreenType)
				{
					pDialog->screenType = ContactScreenType_MissionTurnIn;
				}
				else
				{
					pDialog->screenType = ContactScreenType_Buttons;
				}

				if (pMissionDef)
				{
					// Set the mission UI type information
					contact_FillDialogMissionUITypeInfo(pPlayerEnt, pMissionDef, pDialog);

					if (GET_REF(pMissionDef->displayNameMsg.hMessage))
						langFormatGameDisplayMessage(entGetLanguage(pPlayerEnt), &pDialog->pchDialogHeader, &pMissionDef->displayNameMsg, STRFMT_ENTITY(pPlayerEnt), STRFMT_MAPVARS(eaMapVars), STRFMT_END);
					if(pMissionOffer)
					{
						S32 iDialogIndex = contactdialog_SetTextFromDialogBlocks(pPlayerEnt, &pMissionOffer->completedDialog, &pDialog->pchDialogText1, &pDialog->pchSoundToPlay, &pDialog->pchPhrasePath, REF_HANDLEPTR(pDialog->hDialogText1Formatter));

						// Set the animation list if there is one
						if (pMissionOffer->completedDialog && 
							iDialogIndex >= 0 && 
							pMissionOffer->completedDialog[iDialogIndex] &&
							IS_HANDLE_ACTIVE(pMissionOffer->completedDialog[iDialogIndex]->hAnimList))
						{
							COPY_HANDLE(pDialog->hAnimListToPlayForActiveEntity, pMissionOffer->completedDialog[iDialogIndex]->hAnimList);
						}

						contact_GetVoicePathFromPlayerCostume(contact_PlayerCostumeFromContactDialog(pDialog, pPlayerEnt), &pDialog->pchVoicePath);
					}
					SET_HANDLE_FROM_REFERENT(g_MissionDictionary, pMissionDef, pDialog->hRootMissionDef);
				}

				// Generate OnReturn rewards
				seed = mission_GetRewardSeed(pPlayerEnt, pMission, pMissionDef);
				iPlayerLevel = entity_GetSavedExpLevel(pPlayerEnt);

				// Get gated data. Note this might not be exact as the gated type could be incremented before the mission has eneded if its a shared gated type
				pGatedData = mission_trh_CreateRewardGatedData(CONTAINER_NOCONST(Entity, pPlayerEnt));

				reward_GenerateMissionActionRewards(entGetPartitionIdx(pPlayerEnt), pPlayerEnt, pMissionDef, MissionState_TurnedIn, &pDialog->eaRewardBags, &seed,
					pMission->eCreditType, pMission->iLevel, /*time_level=*/0, pMission->startTime, 0, eRecruitType, /*bUGCProject=*/false, false, false, false, false, /*bGenerateChestRewards=*/false, &rewardContextData, 0, pGatedData);

				// destroy gated reward info
				if(pGatedData)
				{
					StructDestroy(parse_RewardGatedDataInOut, pGatedData);
				}

				if (contactdialog_InventoryBagsHaveNonChoosableRewards(&pDialog->eaRewardBags))
					estrPrintf(&pDialog->pchRewardsHeader, "%s", langTranslateMessage(entGetLanguage(pPlayerEnt), GET_REF(s_ContactMsgs.hRewardsHeaderMsg)));
				if (contactdialog_InventoryBagsHaveChoosableRewards(&pDialog->eaRewardBags))
					estrPrintf(&pDialog->pchOptionalRewardsHeader, "%s", langTranslateMessage(entGetLanguage(pPlayerEnt), GET_REF(s_ContactMsgs.hOptionalRewardsHeaderMsg)));

				if (contactdialog_InventoryBagsHaveChoosableRewards(&pDialog->eaRewardBags))
				{
					if (pMissionOffer && pMissionOffer->pchRewardChooseTargetDialog)
					{
						ContactMissionOffer *pTargetedMissionOffer = contact_MissionOfferFromSpecialDialogName(pContactDef, pMissionOffer->pchRewardChooseTargetDialog);

						if (pTargetedMissionOffer)
						{
							pNewOption = contact_CreateDialogOptionForMissionOfferTargetedFromSpecialDialog(pTargetedMissionOffer, pContactDef, pPlayerEnt, iPlayerLevel, pDialog, pRewardChooseMsg, ContactActionType_ReturnMission, eaMapVars, GET_REF(pMissionOffer->hRewardChooseDialogFormatter));
							pNewOption->pchKey = StructAllocString("ViewCompleteMission.Continue");

							if (pNewOption->pData)
							{
								// When we create the new option, mission fields point to the new mission. We want the store new mission info in the secondary fields.

								// Store new mission data in secondary fields
								COPY_HANDLE(pNewOption->pData->hRootMissionSecondary, pNewOption->pData->hRootMission);
								pNewOption->pData->pchSubMissionNameSecondary = pNewOption->pData->pchSubMissionName;

								// Set the original mission data in the primary fields
								pNewOption->pData->pchSubMissionName = pOption->pchSubMissionName;
								SET_HANDLE_FROM_REFERENT(g_MissionDictionary, pMissionDef, pNewOption->pData->hRootMission);
							}
						}
						else
						{
							pNewOption = contactdialog_CreateDialogOptionEx(pPlayerEnt, &pDialog->eaOptions, "ViewCompleteMission.Continue", pRewardChooseMsg, ContactDialogState_SpecialDialog, ContactActionType_ReturnMission, eaMapVars, GET_REF(pMissionOffer->hRewardChooseDialogFormatter));

							if (pNewOption && pNewOption->pData)
							{
								pNewOption->pData->pchTargetDialogName =  pMissionOffer->pchRewardChooseTargetDialog;
							}
						}
						if (pNewOption)
						{
							pNewOption->bShowRewardChooser = true;
						}
					}
					else
					{
						pNewOption = contactdialog_CreateDialogOptionEx(pPlayerEnt, &pDialog->eaOptions, "ViewCompleteMission.Continue", pRewardChooseMsg, ContactDialogState_OptionListFarewell, ContactActionType_ReturnMission, eaMapVars, pMissionOffer ? GET_REF(pMissionOffer->hRewardChooseDialogFormatter) : NULL);
						pNewOption->bShowRewardChooser = true;
					}
				} 
				else 
				{
					if (pMissionOffer && pMissionOffer->pchRewardAcceptTargetDialog)
					{
						ContactMissionOffer *pTargetedMissionOffer = contact_MissionOfferFromSpecialDialogName(pContactDef, pMissionOffer->pchRewardAcceptTargetDialog);

						if (pTargetedMissionOffer)
						{
							pNewOption = contact_CreateDialogOptionForMissionOfferTargetedFromSpecialDialog(pTargetedMissionOffer, pContactDef, pPlayerEnt, iPlayerLevel, pDialog, pRewardAcceptMsg, ContactActionType_ReturnMission, eaMapVars, GET_REF(pMissionOffer->hRewardAcceptDialogFormatter));
							pNewOption->pchKey = StructAllocString("ViewCompleteMission.Continue");

							if (pNewOption->pData)
							{
								// When we create the new option, mission fields point to the new mission. We want the store new mission info in the secondary fields.

								// Store new mission data in secondary fields
								COPY_HANDLE(pNewOption->pData->hRootMissionSecondary, pNewOption->pData->hRootMission);
								pNewOption->pData->pchSubMissionNameSecondary = pNewOption->pData->pchSubMissionName;

								// Set the original mission data in the primary fields
								pNewOption->pData->pchSubMissionName = pOption->pchSubMissionName;
								SET_HANDLE_FROM_REFERENT(g_MissionDictionary, pMissionDef, pNewOption->pData->hRootMission);
							}
						}
						else
						{
							pNewOption = contactdialog_CreateDialogOptionEx(pPlayerEnt, &pDialog->eaOptions, "ViewCompleteMission.Continue", pRewardAcceptMsg, ContactDialogState_SpecialDialog, ContactActionType_ReturnMission, eaMapVars, GET_REF(pMissionOffer->hRewardAcceptDialogFormatter));

							if (pNewOption && pNewOption->pData)
							{
								pNewOption->pData->pchTargetDialogName =  pMissionOffer->pchRewardAcceptTargetDialog;
							}
						}
					}
					else
					{
						pNewOption = contactdialog_CreateDialogOptionEx(pPlayerEnt, &pDialog->eaOptions, "ViewCompleteMission.Continue", pRewardAcceptMsg, ContactDialogState_OptionListFarewell, ContactActionType_ReturnMission, eaMapVars, pMissionOffer ? GET_REF(pMissionOffer->hRewardAcceptDialogFormatter) : NULL);
					}
				}
				if (pNewOption && pNewOption->pData){
					SET_HANDLE_FROM_REFERENT(g_MissionDictionary, pMissionDef, pNewOption->pData->hRootMission);
				}

				// Create the abort option if available
				if (pMissionOffer && pRewardAbortMsg)
				{
					ContactDialogState eNextState = ContactDialogState_Exit;
					if (pMissionOffer->pchRewardAbortTargetDialog)
					{
						eNextState = ContactDialogState_SpecialDialog;
					}
					
					pNewOption = contactdialog_CreateDialogOptionEx(pPlayerEnt, &pDialog->eaOptions, "ViewCompleteMission.Abort", pRewardAbortMsg, eNextState, ContactActionType_None, eaMapVars, GET_REF(pMissionOffer->hRewardAbortDialogFormatter));

					if (pNewOption && pNewOption->pData && pMissionOffer->pchRewardAbortTargetDialog)
					{
						pNewOption->pData->pchTargetDialogName =  pMissionOffer->pchRewardAbortTargetDialog;
					}
				}
				else
				{
					pNewOption = contactdialog_CreateDialogOption(pPlayerEnt, &pDialog->eaOptions, "ViewCompleteMission.Back", GET_REF(s_ContactMsgs.hBackMsg), ContactDialogState_OptionList, 0, eaMapVars);
					pNewOption->bIsDefaultBackOption = true;
				}
			}
		xcase ContactDialogState_ViewSubMission:
			{
				MissionDef *pRootDef = IS_HANDLE_ACTIVE(pOption->hRootMissionSecondary) ? GET_REF(pOption->hRootMissionSecondary) : GET_REF(pOption->hRootMission);
				MissionDef *pMissionDef = IS_HANDLE_ACTIVE(pOption->hRootMissionSecondary) ? missiondef_ChildDefFromName(pRootDef, pOption->pchSubMissionNameSecondary) : missiondef_ChildDefFromName(pRootDef, pOption->pchSubMissionName);
				Mission *pMission = pMissionInfo ? mission_GetMissionOrSubMission(pMissionInfo, pMissionDef) : NULL;
				Mission *pRootMission = pMissionInfo ? mission_GetMissionOrSubMission(pMissionInfo, pRootDef) : NULL;
				ContactMissionOffer *pMissionOffer = pContactDef ? contact_GetMissionOffer(pContactDef, pPlayerEnt, pMissionDef) : NULL;
				SpecialDialogBlock *pCompletedSpecialDialog = pContactDef && pOption->pchCompletedDialogName ? contact_SpecialDialogFromName(pContactDef, pOption->pchCompletedDialogName) : NULL;
				U32 seed;

				if(!pMissionOffer || !pMissionDef)
					break;

				if (pCompletedSpecialDialog && (pCompletedSpecialDialog->eFlags & SpecialDialogFlags_CriticalDialog))
				{
					bShowToTeamMembers = true;
				}
				
				if (pMissionOffer->eUIType == ContactMissionUIType_FauxTreasureChest)
				{
					pDialog->screenType = ContactScreenType_FauxTreasureChest;
				}
				else
				{
					pDialog->screenType = ContactScreenType_Buttons;
				}

				if (pMission)
				{
					bool bUGCProjectInPreviewMode = (isProductionEditMode() && pRootDef->ugcProjectID);
					bool bUGCProjectInLiveMode = !!pRootMission->pUGCMissionData;

					bool bMissionQualifiesForUGCReward = bUGCProjectInPreviewMode ? true : (pRootMission->pUGCMissionData ? pRootMission->pUGCMissionData->bStatsQualifyForUGCRewards : false);
					int iLevel = (bUGCProjectInPreviewMode || bUGCProjectInLiveMode) ? entity_GetSavedExpLevel(pPlayerEnt) : pMission->iLevel;
					int iTimeLevel = bUGCProjectInPreviewMode ? 30 : ((bMissionQualifiesForUGCReward && bUGCProjectInLiveMode) ? pRootMission->pUGCMissionData->fAverageDurationInMinutes : 0);

					seed = mission_GetRewardSeed(pPlayerEnt, pRootMission, pRootDef);
					reward_GenerateMissionActionRewards(entGetPartitionIdx(pPlayerEnt), pPlayerEnt, pMissionDef, MissionState_Succeeded, &pDialog->eaRewardBags, &seed,
						pMission->eCreditType, iLevel, /*time_level=*/iTimeLevel, pMission->startTime, 0, eRecruitType,
						/*bUGCProject=*/bUGCProjectInLiveMode || bUGCProjectInPreviewMode, 
						bMissionQualifiesForUGCReward,
						/*bMissionQualifiesForUGCFeaturedReward=*/false,
						/*bMissionQualifiesForUGCNonCombatReward=*/false,
						/*bSubMissionTurnin=*/true,
						/*bGenerateChestRewards=*/false,
						&rewardContextData, 0, /* RewardGatedDataInOut *pGatedData */ NULL);
				}
				if (pMissionDef)
				{
					// Set the mission UI type information
					contact_FillDialogMissionUITypeInfo(pPlayerEnt, pMissionDef, pDialog);

					if (GET_REF(pRootDef->displayNameMsg.hMessage))
						langFormatGameDisplayMessage(entGetLanguage(pPlayerEnt), &pDialog->pchDialogHeader, &pRootDef->displayNameMsg, STRFMT_ENTITY(pPlayerEnt), STRFMT_MAPVARS(eaMapVars), STRFMT_END);
					contactdialog_SetTextFromDialogBlocks(pPlayerEnt, &pMissionOffer->completedDialog, &pDialog->pchDialogText1, &pDialog->pchSoundToPlay, &pDialog->pchPhrasePath, REF_HANDLEPTR(pDialog->hDialogText1Formatter));
					contact_GetVoicePathFromPlayerCostume(contact_PlayerCostumeFromContactDialog(pDialog, pPlayerEnt), &pDialog->pchVoicePath);
					SET_HANDLE_FROM_REFERENT(g_MissionDictionary, pRootDef, pDialog->hRootMissionDef);
					pDialog->pchSubMissionName = pOption->pchSubMissionName;
				}
				
				pNewOption = contactdialog_CreateDialogOption(pPlayerEnt, &pDialog->eaOptions, "ViewSubMission.Continue", GET_REF(s_ContactMsgs.hContinueMsg), ContactDialogState_OptionListFarewell, ContactActionType_CompleteSubMission, eaMapVars);
				if (pNewOption && pNewOption->pData){
					SET_HANDLE_FROM_REFERENT(g_MissionDictionary, pRootDef, pNewOption->pData->hRootMission);
					pNewOption->pData->pchSubMissionName = pMissionDef->name;
					
					if (contactdialog_InventoryBagsHaveChoosableRewards(&pDialog->eaRewardBags))
						pNewOption->bShowRewardChooser = true;
				}

				pNewOption = contactdialog_CreateDialogOption(pPlayerEnt, &pDialog->eaOptions, "ViewSubMission.Back", GET_REF(s_ContactMsgs.hBackMsg), ContactDialogState_OptionList, 0, eaMapVars);
				pNewOption->bIsDefaultBackOption = true;
			}

		xcase ContactDialogState_MissionSearch:

			pDialog->screenType = ContactScreenType_List;
			if (pContactDef){
				contactdialog_SetTextFromDialogBlocks(pPlayerEnt, &pContactDef->eaMissionSearchDialog, &pDialog->pchDialogText1, &pDialog->pchSoundToPlay, &pDialog->pchPhrasePath, REF_HANDLEPTR(pDialog->hDialogText1Formatter));
				contact_GetVoicePathFromPlayerCostume(contact_PlayerCostumeFromContactDialog(pDialog, pPlayerEnt), &pDialog->pchVoicePath);
			}

			// Set up all the Mission Search options
			contactdialog_CreateMissionSearchOptions(pPlayerEnt, pContactDef, pDialog, eaMapVars);

			pNewOption = contactdialog_CreateDialogOption(pPlayerEnt, &pDialog->eaOptions, "ContactInfo.Back", GET_REF(s_ContactMsgs.hBackMsg), ContactDialogState_OptionListFarewell, 0, eaMapVars);
			pNewOption->bIsDefaultBackOption = true;

		xcase ContactDialogState_MissionSearchViewContact:
			
			pDialog->screenType = ContactScreenType_Buttons;
			{
				ContactDef *pSearchContact = NULL;
				if (pOption){
					pSearchContact = GET_REF(pOption->hMissionListContactDef);
					if (pSearchContact && GET_REF(pSearchContact->costumePrefs.costumeOverride)){
						COPY_HANDLE(pDialog->hHeadshotOverride, pSearchContact->costumePrefs.costumeOverride);
					}
				}
				if (pSearchContact){
					pNewOption = contactdialog_CreateDialogOption(pPlayerEnt, &pDialog->eaOptions, "MissionSearchView.SetWaypoint", GET_REF(s_ContactMsgs.hSetWaypointMsg), ContactDialogState_MissionSearch, ContactActionType_MissionSearchSetContactWaypoint, eaMapVars);
					if (pNewOption && pNewOption->pData){
						SET_HANDLE_FROM_REFERENT(g_ContactDictionary, pSearchContact, pNewOption->pData->hMissionListContactDef);
					}
					contactdialog_SetTextFromDialogBlocks(pPlayerEnt, &pSearchContact->infoDialog, &pDialog->pchDialogText1, &pDialog->pchSoundToPlay, &pDialog->pchPhrasePath, REF_HANDLEPTR(pDialog->hDialogText1Formatter));
					contact_GetVoicePathFromPlayerCostume(contact_PlayerCostumeFromContactDialog(pDialog, pPlayerEnt), &pDialog->pchVoicePath);
				}
				pNewOption = contactdialog_CreateDialogOption(pPlayerEnt, &pDialog->eaOptions, "MissionSearchView.Back", GET_REF(s_ContactMsgs.hBackMsg), ContactDialogState_MissionSearch, 0, eaMapVars);
				pNewOption->bIsDefaultBackOption = true;
			}

		xcase ContactDialogState_Store:
			pDialog->screenType = ContactScreenType_Store;
			pNewOption = contactdialog_CreateDialogOption(pPlayerEnt, &pDialog->eaOptions, "Store.Back", GET_REF(s_ContactMsgs.hBackMsg), ContactDialogState_OptionList, 0, eaMapVars);
			pNewOption->bIsDefaultBackOption = true;

			// Set Tab Text Values
			if (pContactDef && GET_REF(pContactDef->buyOptionMsg.hMessage)){
				estrPrintf(&pDialog->pchBuyOptionText, "%s", langTranslateDisplayMessage(entGetLanguage(pPlayerEnt), pContactDef->buyOptionMsg));
			} else {
				estrPrintf(&pDialog->pchBuyOptionText, "%s", langTranslateMessageKey(entGetLanguage(pPlayerEnt), "Store.Buy"));
			}
			if (pContactDef && GET_REF(pContactDef->sellOptionMsg.hMessage)){
				estrPrintf(&pDialog->pchSellOptionText, "%s", langTranslateDisplayMessage(entGetLanguage(pPlayerEnt), pContactDef->sellOptionMsg));
			} else {
				estrPrintf(&pDialog->pchSellOptionText, "%s", langTranslateMessageKey(entGetLanguage(pPlayerEnt), "Store.Sell"));
			}
			if (pContactDef && GET_REF(pContactDef->buyBackOptionMsg.hMessage)){
				estrPrintf(&pDialog->pchBuyBackOptionText, "%s", langTranslateDisplayMessage(entGetLanguage(pPlayerEnt), pContactDef->buyBackOptionMsg));
			} else {
				estrPrintf(&pDialog->pchBuyBackOptionText, "%s", langTranslateMessageKey(entGetLanguage(pPlayerEnt), "Store.BuyBack"));
			}


			// Look at all StoreDefs on the Contact
			if (pContactDef){
				GameAccountDataExtract *pExtract = entity_GetCachedGameAccountDataExtract(pPlayerEnt);
				bool bHasPersistedStore = false;

				// Check to see if this store should get any special screen types
				if (contact_IsPuppetVendor(pContactDef))
				{
					pDialog->screenType = ContactScreenType_PuppetStore;
				}

				for (i = 0; i < eaSize(&pContactDef->stores); i++){
					StoreDef *pStore = GET_REF(pContactDef->stores[i]->ref);

					// Only allow one persisted store per contact for now
					if (!pStore || (bHasPersistedStore && pStore->bIsPersisted))
					{
						continue;
					}
					if (pStore->pExprRequires)
					{
						MultiVal answer = {0};
						store_BuyContextSetup(pPlayerEnt, NULL);
						exprEvaluate(pStore->pExprRequires, store_GetBuyContext(), &answer);
						if(answer.type == MULTI_INT)
						{
							if (!QuickGetInt(&answer))
							{
								continue;
							}
						}
					}

					// TODO: Change this if costume stores get their own UI; for now, let them be treated just like normal stores
					if (pStore->eContents == Store_All || pStore->eContents == Store_Costumes){
						
						// If this is the first one that allows Sell, use it's Sell properties
						if (pStore->bSellEnabled && GET_REF(pStore->hCurrency)){
							COPY_HANDLE(pDialog->hSellStore, pContactDef->stores[i]->ref);
							COPY_HANDLE(pDialog->hStoreCurrency, pStore->hCurrency);
							pDialog->bSellEnabled = true;
						}
						// If this is a persisted store, request container data
						if (pStore->bIsPersisted) {
							bHasPersistedStore = pStore->bIsPersisted;
							gslPersistedStore_PlayerAddRequest(pPlayerEnt, pStore);
						}
						// Generate all items for the Buy menu
						store_GetStoreItemInfo(pPlayerEnt, pContactDef, pStore, &pDialog->eaStoreItems, &pDialog->eaUnavailableStoreItems,
												&pDialog->eaStoreDiscounts, pExtract);
					} else if (pStore->eContents == Store_Sellable_Items) {
						// Set this store to be the sell store
						if (pStore->bSellEnabled && GET_REF(pStore->hCurrency)){
							COPY_HANDLE(pDialog->hSellStore, pContactDef->stores[i]->ref);
							COPY_HANDLE(pDialog->hStoreCurrency, pStore->hCurrency);
							pDialog->bSellEnabled = true;
						}
					}
					pDialog->bDisplayStoreCPoints = pStore->bDisplayStoreCPoints;
				}
				contact_ShowNoStoreDialogIfNeeded(pPlayerEnt, pContactDef, pDialog, eaMapVars, pExtract);
			}

		xcase ContactDialogState_RecipeStore:
		{
			pDialog->screenType = ContactScreenType_RecipeStore;
			pNewOption = contactdialog_CreateDialogOption(pPlayerEnt, &pDialog->eaOptions, "RecipeStore.Back", GET_REF(s_ContactMsgs.hBackMsg), ContactDialogState_OptionList, 0, eaMapVars);
			pNewOption->bIsDefaultBackOption = true;

			// Look at all StoreDefs on the Contact
			if (pContactDef){
				bool bHasPersistedStore = false;
				for (i = 0; i < eaSize(&pContactDef->stores); i++){
					StoreDef *pStore = GET_REF(pContactDef->stores[i]->ref);

					// Only allow one persisted store per contact for now
					if (!pStore || (bHasPersistedStore && pStore->bIsPersisted))
					{
						continue;
					}
					if (pStore->pExprRequires)
					{
						MultiVal answer = {0};
						store_BuyContextSetup(pPlayerEnt, NULL);
						exprEvaluate(pStore->pExprRequires, store_GetBuyContext(), &answer);
						if(answer.type == MULTI_INT)
						{
							if (!QuickGetInt(&answer))
							{
								continue;
							}
						}
					}

					if (pStore->eContents == Store_Recipes){
						GameAccountDataExtract *pExtract = entity_GetCachedGameAccountDataExtract(pPlayerEnt);

						// If this is a persisted store, request container data
						if (pStore->bIsPersisted) {
							bHasPersistedStore = pStore->bIsPersisted;
							gslPersistedStore_PlayerAddRequest(pPlayerEnt, pStore);
						}
						// Generate all items for the Buy menu
						store_GetStoreItemInfo(pPlayerEnt, pContactDef, pStore, &pDialog->eaStoreItems, &pDialog->eaUnavailableStoreItems,
													&pDialog->eaStoreDiscounts, pExtract);
					}
				}
			}
		}
		xcase ContactDialogState_PowerStore:
		{
			pDialog->screenType = ContactScreenType_PowerStore;
			pNewOption = contactdialog_CreateDialogOption(pPlayerEnt, &pDialog->eaOptions, "Store.Back", GET_REF(s_ContactMsgs.hBackMsg), ContactDialogState_OptionList, 0, eaMapVars);
			pNewOption->bIsDefaultBackOption = true;

			// Look at all PowerStoreDefs on the Contact
			if (pContactDef){
				for (i = 0; i < eaSize(&pContactDef->powerStores); i++){
					PowerStoreDef *pStore = GET_REF(pContactDef->powerStores[i]->ref);
					if (pStore){
						if (pStore->bIsOfficerTrainer) {
							pDialog->bIsOfficerTrainer = true;
						}
						// Generate all powers for the Buy menu
						powerstore_GetStorePowerInfo(pPlayerEnt, pStore, &pDialog->eaStorePowers);
					}
				}
			}
		}
		xcase ContactDialogState_PowerStoreFromItem:
		{
			GameAccountDataExtract *pExtract = entity_GetCachedGameAccountDataExtract(pPlayerEnt);
			ItemDef* pItemDef = GET_REF(pOption->hItemDef);
			Entity* pEntSrc = entity_GetSubEntity(entGetPartitionIdx(pPlayerEnt), pPlayerEnt, pOption->iEntType, pOption->iEntID);
			NOCONST(Item)* pItem = inv_trh_GetItemFromBag(ATR_EMPTY_ARGS, pEntSrc ? CONTAINER_NOCONST(Entity, pEntSrc) : CONTAINER_NOCONST(Entity, pPlayerEnt), pOption->iItemBagID, pOption->iItemSlot, pExtract);

			PetDef* pPetDef = pItemDef ? GET_REF(pItemDef->hPetDef) : NULL;
			CritterDef* pCritterDef = pPetDef ? GET_REF(pPetDef->hCritterDef) : NULL;
			PlayerCostume* pCostume = pCritterDef ? GET_REF(pCritterDef->hOverrideCostumeRef) : NULL;

			if (!pItemDef)
			{
				break;
			}

			if (!pCritterDef)
			{
				// If this item doesn't have a PetDef, then try to use the first costume on the ItemDef
				ItemCostume* pItemCostume = eaGet(&pItemDef->ppCostumes, 0);
				if (pItemCostume)
				{
					pCostume = GET_REF(pItemCostume->hCostumeRef);
				}
			}
			else if (!pCostume)
			{
				//If there is no override costume on the CritterDef, just use the first costume on the CritterDef
				pCostume = eaSize(&pCritterDef->ppCostume) > 0 ? GET_REF(pCritterDef->ppCostume[0]->hCostumeRef) : NULL;
			}

			pDialog->iItemBagID = pOption->iItemBagID;
			pDialog->iItemSlot = pOption->iItemSlot;

			if (pOption->bIsOfficerTrainer)
				pDialog->bIsOfficerTrainer = true;
			
			if (pItem && GET_REF(pItem->hItem) == pItemDef)
				pDialog->iItemID = pItem->id;

			if (pCostume)
				SET_HANDLE_FROM_STRING("PlayerCostume",(char*)pCostume->pcName,pDialog->hHeadshotOverride);

			pDialog->screenType = ContactScreenType_PowerStore;
			pNewOption = contactdialog_CreateDialogOption(pPlayerEnt, &pDialog->eaOptions, "Store.Back", GET_REF(s_ContactMsgs.hBackMsg), ContactDialogState_Exit, 0, eaMapVars);
			pNewOption->bIsDefaultBackOption = true;

			if (pItem && GET_REF(pItem->hItem) == pItemDef)
			{
				powerstore_GetStorePowerInfoFromItem(pEntSrc, pOption->iItemBagID, pOption->iItemSlot, pItemDef, &pDialog->eaStorePowers, pExtract);
			}
		}
		xcase ContactDialogState_InjuryStore:
		{
			Entity *pTargetEnt = entity_GetSubEntity(entGetPartitionIdx(pPlayerEnt), pPlayerEnt, pOption->iEntType, pOption->iEntID);
			// Hardcoded check for UI purposes (yuck)
			if(pContactDef && stricmp(pContactDef->name, "Injury_Cure_Ground_Contact")==0 || stricmp(pContactDef->name, "Injury_Cure_Space_Contact")==0)
				pDialog->screenType = ContactScreenType_InjuryStoreFromPack;
			else
				pDialog->screenType = ContactScreenType_InjuryStore;
			pNewOption = contactdialog_CreateDialogOption(pPlayerEnt, &pDialog->eaOptions, "Store.Back", GET_REF(s_ContactMsgs.hBackMsg), ContactDialogState_OptionList, 0, eaMapVars);
			pNewOption->bIsDefaultBackOption = true;

			// Look at all StoreDefs on the Contact
			if (pContactDef){
				bool bHasPersistedStore = false;
				for (i = 0; i < eaSize(&pContactDef->stores); i++){
					StoreDef *pStore = GET_REF(pContactDef->stores[i]->ref);

					// Only allow one persisted store per contact for now
					if (!pStore || (bHasPersistedStore && pStore->bIsPersisted))
					{
						continue;
					}
					if (pStore->pExprRequires)
					{
						MultiVal answer = {0};
						store_BuyContextSetup(pPlayerEnt, NULL);
						exprEvaluate(pStore->pExprRequires, store_GetBuyContext(), &answer);
						if(answer.type == MULTI_INT)
						{
							if (!QuickGetInt(&answer))
							{
								continue;
							}
						}
					}

					if (pStore->eContents == Store_Injuries){
						// If this is a persisted store, request container data
						if (pStore->bIsPersisted) {
							bHasPersistedStore = pStore->bIsPersisted;
							gslPersistedStore_PlayerAddRequest(pPlayerEnt, pStore);
						}
						// Generate all items for the Buy menu
						if(pTargetEnt)
						{
							GameAccountDataExtract *pExtract = entity_GetCachedGameAccountDataExtract(pPlayerEnt);
							store_GetStoreOwnedItemInfo(pPlayerEnt, pTargetEnt, pContactDef, pStore, &pDialog->eaStoreItems, InvBagIDs_Injuries, pExtract);
						}
							
						pDialog->eStoreRegion = pStore->eRegion;
					}
				}
			}
		}
		xcase ContactDialogState_StoreCollection:
		{
			GameAccountDataExtract *pExtract = entity_GetCachedGameAccountDataExtract(pPlayerEnt);
			StoreCollection* pStoreCollection = NULL;

			// Hardcoded check for UI purposes (yuck)
			if(pContactDef && pContactDef->bIsResearchStoreCollection)
				pDialog->screenType = ContactScreenType_ResearchStoreCollection;
			else if (pContactDef && (pContactDef->eContactFlags & ContactFlag_Windowed))
				pDialog->screenType = ContactScreenType_WindowedStoreCollection;
			else
				pDialog->screenType = ContactScreenType_StoreCollection;
				pDialog->iCurrentStoreCollection = pOption->iStoreCollection;

			// Set Tab Text Values
			if (pContactDef && GET_REF(pContactDef->buyOptionMsg.hMessage)){
				estrPrintf(&pDialog->pchBuyOptionText, "%s", langTranslateDisplayMessage(entGetLanguage(pPlayerEnt), pContactDef->buyOptionMsg));
			} else {
				estrPrintf(&pDialog->pchBuyOptionText, "%s", langTranslateMessageKey(entGetLanguage(pPlayerEnt), "Store.Buy"));
			}
			if (pContactDef && GET_REF(pContactDef->sellOptionMsg.hMessage)){
				estrPrintf(&pDialog->pchSellOptionText, "%s", langTranslateDisplayMessage(entGetLanguage(pPlayerEnt), pContactDef->sellOptionMsg));
			} else {
				estrPrintf(&pDialog->pchSellOptionText, "%s", langTranslateMessageKey(entGetLanguage(pPlayerEnt), "Store.Sell"));
			}
			if (pContactDef && GET_REF(pContactDef->buyBackOptionMsg.hMessage)){
				estrPrintf(&pDialog->pchBuyBackOptionText, "%s", langTranslateDisplayMessage(entGetLanguage(pPlayerEnt), pContactDef->buyBackOptionMsg));
			} else {
				estrPrintf(&pDialog->pchBuyBackOptionText, "%s", langTranslateMessageKey(entGetLanguage(pPlayerEnt), "Store.BuyBack"));
			}

			// Build Options
			if(pContactDef) {
				char* estrBuf = NULL;
				estrCreate(&estrBuf);
				for(i = 0; i < eaSize(&pContactDef->storeCollections); i++)
				{
					MultiVal answer = {0};
					store_BuyContextSetup(pPlayerEnt, NULL);
					exprEvaluate(pContactDef->storeCollections[i]->pCondition, store_GetBuyContext(), &answer);
					if(answer.type == MULTI_INT)
					{
						if (!QuickGetInt(&answer))
						{
							continue;
						}
					}
					estrPrintf(&estrBuf, "StoreCollection.%d", i);
					pNewOption = contactdialog_CreateDialogOption(pPlayerEnt, &pDialog->eaOptions, estrBuf, GET_REF(pContactDef->storeCollections[i]->optionText.hMessage), ContactDialogState_StoreCollection, 0, eaMapVars);
					pNewOption->pData->iStoreCollection = i;
				}
				estrDestroy(&estrBuf);
			}

			// Build store collection
			if(pOption->iStoreCollection >= 0 && pOption->iStoreCollection < eaSize(&pContactDef->storeCollections))
			{
				pStoreCollection = pContactDef->storeCollections[pOption->iStoreCollection];
			}

			if (pStoreCollection){
				bool bHasPersistedStore = false;
				for (i = 0; i < eaSize(&pStoreCollection->eaStores); i++){
					StoreDef *pStore = GET_REF(pStoreCollection->eaStores[i]->ref);

					// Only allow one persisted store per contact for now
					if (!pStore || (bHasPersistedStore && pStore->bIsPersisted))
					{
						continue;
					}
					if (pStore->pExprRequires)
					{
						MultiVal answer = {0};
						store_BuyContextSetup(pPlayerEnt, NULL);
						exprEvaluate(pStore->pExprRequires, store_GetBuyContext(), &answer);
						if(answer.type == MULTI_INT)
						{
							if (!QuickGetInt(&answer))
							{
								continue;
							}
						}
					}

					if (pStore->eContents == Store_All || pStore->eContents == Store_Costumes){
						// If this is the first one that allows Sell, use it's Sell properties
						if (pStore->bSellEnabled && GET_REF(pStore->hCurrency)){
							COPY_HANDLE(pDialog->hSellStore, pStoreCollection->eaStores[i]->ref);
							COPY_HANDLE(pDialog->hStoreCurrency, pStore->hCurrency);
							pDialog->bSellEnabled = true;
						}
						pDialog->bDisplayStoreCPoints = pStore->bDisplayStoreCPoints;
						// If this is a persisted store, request container data
						if (pStore->bIsPersisted) {
							bHasPersistedStore = pStore->bIsPersisted;
							gslPersistedStore_PlayerAddRequest(pPlayerEnt, pStore);
						}
						// Generate all items for the Buy menu
						store_GetStoreItemInfo(pPlayerEnt, pContactDef, pStore, &pDialog->eaStoreItems, &pDialog->eaUnavailableStoreItems,
													&pDialog->eaStoreDiscounts, pExtract);
					}
				}
			}
			contact_ShowNoStoreDialogIfNeeded(pPlayerEnt, pContactDef, pDialog, eaMapVars, pExtract);
		}
		xcase ContactDialogState_TrainerFromEntity:
		{
			Entity* pTrainer = entity_GetSubEntity(entGetPartitionIdx(pPlayerEnt), pPlayerEnt, pOption->iEntType, pOption->iEntID);

			pDialog->screenType = ContactScreenType_PowerStore;
			pNewOption = contactdialog_CreateDialogOption(pPlayerEnt, &pDialog->eaOptions, "Store.Back", GET_REF(s_ContactMsgs.hBackMsg), ContactDialogState_Exit, 0, eaMapVars);
			pNewOption->bIsDefaultBackOption = true;

			if (Officer_GetRankCount(allegiance_GetOfficerPreference(GET_REF(pPlayerEnt->hAllegiance), GET_REF(pPlayerEnt->hSubAllegiance))))
			{
				pDialog->bIsOfficerTrainer = true;
			}
			if (pTrainer)
			{
				powerstore_GetTrainerPowerInfoFromEntity(pPlayerEnt, pTrainer, &pDialog->eaStorePowers);
			}
		}
		xcase ContactDialogState_ViewLore:
			pDialog->screenType = ContactScreenType_Buttons;
			if (pContactDef){
				pNewOption = contactdialog_CreateDialogOption(pPlayerEnt, &pDialog->eaOptions, "ViewLore.Back", GET_REF(s_ContactMsgs.hBackMsg), ContactDialogState_OptionListFarewell, 0, eaMapVars);
				pNewOption->bIsDefaultBackOption = true;
			} else {
				pNewOption = contactdialog_CreateDialogOption(pPlayerEnt, &pDialog->eaOptions, "ViewLore.Back", GET_REF(s_ContactMsgs.hExitMsg), ContactDialogState_Exit, 0, eaMapVars);
				pNewOption->bIsDefaultBackOption = true;
			}

			if (pOption)
			{
				ItemDef *pItemDef = GET_REF(pOption->hItemDef);
				if (pItemDef && pItemDef->eType == kItemType_Lore){
					if (GET_REF(pItemDef->displayNameMsg.hMessage))
						langFormatGameDisplayMessage(entGetLanguage(pPlayerEnt), &pDialog->pchDialogHeader, &pItemDef->displayNameMsg, STRFMT_ENTITY(pPlayerEnt), STRFMT_MAPVARS(eaMapVars), STRFMT_END);
					if (GET_REF(pItemDef->descriptionMsg.hMessage))
						langFormatGameDisplayMessage(entGetLanguage(pPlayerEnt), &pDialog->pchDialogText1, &pItemDef->descriptionMsg, STRFMT_ENTITY(pPlayerEnt), STRFMT_MAPVARS(eaMapVars), STRFMT_END);
				}
			}

		// These are not real contact screens.  They just trigger the Contact UI to open another type of dialog.
		// I'm not sure that this is the right way to link these things to the Contact Dialog.
		xcase ContactDialogState_Tailor:
			pDialog->screenType = ContactScreenType_Tailor;
			pNewOption = contactdialog_CreateDialogOption(pPlayerEnt, &pDialog->eaOptions, "Tailor.Back", GET_REF(s_ContactMsgs.hBackMsg), ContactDialogState_OptionListFarewell, 0, eaMapVars);
			pNewOption->bIsDefaultBackOption = true;
		xcase ContactDialogState_StarshipTailor:
			{
				CharClassCategorySet *pSet = GET_REF(pOption->hClassCategorySet);
				pDialog->uiShipTailorEntityID = 0;
				for (i=0; i < eaSize(&pPlayerEnt->pSaved->pPuppetMaster->ppPuppets); i++)
				{
					PuppetEntity *pPuppetEntity = pPlayerEnt->pSaved->pPuppetMaster->ppPuppets[i];
					Entity *pEntity = SAFE_GET_REF(pPuppetEntity, hEntityRef);
					CharacterClass *pClass = SAFE_GET_REF2(pEntity, pChar, hClass);
					if (pPuppetEntity->eState == PUPPETSTATE_ACTIVE 
						&& pSet 
						&& pClass 
						&& eaiFind(&pSet->eaCategories, pClass->eCategory) >= 0)
					{
						pDialog->uiShipTailorEntityID = pPuppetEntity->curID;
						break;
					}
				}
				pDialog->screenType = ContactScreenType_StarshipTailor;
				pNewOption = contactdialog_CreateDialogOption(pPlayerEnt, &pDialog->eaOptions, "Tailor.Back", GET_REF(s_ContactMsgs.hBackMsg), ContactDialogState_OptionListFarewell, 0, eaMapVars);
				pNewOption->bIsDefaultBackOption = true;
			}
		xcase ContactDialogState_WeaponTailor:
			pDialog->screenType = ContactScreenType_WeaponTailor;
			pNewOption = contactdialog_CreateDialogOption(pPlayerEnt, &pDialog->eaOptions, "Tailor.Back", GET_REF(s_ContactMsgs.hBackMsg), ContactDialogState_OptionListFarewell, 0, eaMapVars);
			pNewOption->bIsDefaultBackOption = true;
		xcase ContactDialogState_StarshipChooser:
			pDialog->screenType = ContactScreenType_StarshipChooser;
			pNewOption = contactdialog_CreateDialogOption(pPlayerEnt, &pDialog->eaOptions, "StarshipChooser.Back", GET_REF(s_ContactMsgs.hBackMsg), ContactDialogState_OptionListFarewell, 0, eaMapVars);
			pNewOption->bIsDefaultBackOption = true;
			//pDialog->ppchAllowedCharacterClasses;
		xcase ContactDialogState_NewNemesis:
			pDialog->screenType = ContactScreenType_NewNemesis;
			pNewOption = contactdialog_CreateDialogOption(pPlayerEnt, &pDialog->eaOptions, "NewNemesis.Back", GET_REF(s_ContactMsgs.hBackMsg), ContactDialogState_OptionListFarewell, 0, eaMapVars);
			pNewOption->bIsDefaultBackOption = true;
		xcase ContactDialogState_Nemesis:
			pDialog->screenType = ContactScreenType_Nemesis;
			pNewOption = contactdialog_CreateDialogOption(pPlayerEnt, &pDialog->eaOptions, "Nemesis.Back", GET_REF(s_ContactMsgs.hBackMsg), ContactDialogState_OptionListFarewell, 0, eaMapVars);
			pNewOption->bIsDefaultBackOption = true;
		xcase ContactDialogState_Guild:
			pDialog->screenType = ContactScreenType_Guild;
			pNewOption = contactdialog_CreateDialogOption(pPlayerEnt, &pDialog->eaOptions, "Guild.Back", GET_REF(s_ContactMsgs.hBackMsg), ContactDialogState_OptionListFarewell, 0, eaMapVars);
			pNewOption->bIsDefaultBackOption = true;
		xcase ContactDialogState_NewGuild:
			pDialog->screenType = ContactScreenType_NewGuild;
			pNewOption = contactdialog_CreateDialogOption(pPlayerEnt, &pDialog->eaOptions, "NewGuild.Back", GET_REF(s_ContactMsgs.hBackMsg), ContactDialogState_OptionListFarewell, 0, eaMapVars);
			pNewOption->bIsDefaultBackOption = true;
		xcase ContactDialogState_Respec:
			pDialog->screenType = ContactScreenType_Respec;
			pNewOption = contactdialog_CreateDialogOption(pPlayerEnt, &pDialog->eaOptions, "Respec.Back", GET_REF(s_ContactMsgs.hBackMsg), ContactDialogState_OptionListFarewell, 0, eaMapVars);
			pNewOption->bIsDefaultBackOption = true;
		xcase ContactDialogState_Bank:
			pDialog->screenType = ContactScreenType_Bank;
			pNewOption = contactdialog_CreateDialogOption(pPlayerEnt, &pDialog->eaOptions, "Bank.Back", GET_REF(s_ContactMsgs.hBackMsg), ContactDialogState_OptionListFarewell, 0, eaMapVars);
			pNewOption->bIsDefaultBackOption = true;
		xcase ContactDialogState_SharedBank:
			pDialog->screenType = ContactScreenType_SharedBank;
			pNewOption = contactdialog_CreateDialogOption(pPlayerEnt, &pDialog->eaOptions, "SharedBank.Back", GET_REF(s_ContactMsgs.hBackMsg), ContactDialogState_OptionListFarewell, 0, eaMapVars);
			pNewOption->bIsDefaultBackOption = true;
		xcase ContactDialogState_ZStore:
			pDialog->screenType = ContactScreenType_ZStore;
			pNewOption = contactdialog_CreateDialogOption(pPlayerEnt, &pDialog->eaOptions, "ZStore.Back", GET_REF(s_ContactMsgs.hBackMsg), ContactDialogState_OptionListFarewell, 0, eaMapVars);
			pNewOption->bIsDefaultBackOption = true;
		xcase ContactDialogState_GuildBank:
			pDialog->screenType = ContactScreenType_GuildBank;
			pNewOption = contactdialog_CreateDialogOption(pPlayerEnt, &pDialog->eaOptions, "GuildBank.Back", GET_REF(s_ContactMsgs.hBackMsg), ContactDialogState_OptionListFarewell, 0, eaMapVars);
			pNewOption->bIsDefaultBackOption = true;
		xcase ContactDialogState_PowersTrainer:
			pDialog->screenType = ContactScreenType_PowersTrainer;
			pNewOption = contactdialog_CreateDialogOption(pPlayerEnt, &pDialog->eaOptions, "PowersTrainer.Back", GET_REF(s_ContactMsgs.hBackMsg), ContactDialogState_OptionListFarewell, 0, eaMapVars);
			pNewOption->bIsDefaultBackOption = true;
		xcase ContactDialogState_Market:
			pDialog->screenType = ContactScreenType_Market;
			pNewOption = contactdialog_CreateDialogOption(pPlayerEnt, &pDialog->eaOptions, "Market.Back", GET_REF(s_ContactMsgs.hBackMsg), ContactDialogState_OptionListFarewell, 0, eaMapVars);
			pNewOption->bIsDefaultBackOption = true;
		xcase ContactDialogState_AuctionBroker:
			pDialog->screenType = ContactScreenType_AuctionBroker;
			COPY_HANDLE(pDialog->hAuctionBrokerDef, pOption->hAuctionBrokerDef);
			pNewOption = contactdialog_CreateDialogOption(pPlayerEnt, &pDialog->eaOptions, "AuctionBroker.Back", GET_REF(s_ContactMsgs.hBackMsg), ContactDialogState_OptionListFarewell, 0, eaMapVars);
			pNewOption->bIsDefaultBackOption = true;
		xcase ContactDialogState_UGCSearchAgent:
			pDialog->screenType = ContactScreenType_UGCSearchAgent;
			pNewOption = contactdialog_CreateDialogOption(pPlayerEnt, &pDialog->eaOptions, "UGCSearchAgent.Back", GET_REF(s_ContactMsgs.hBackMsg), ContactDialogState_OptionListFarewell, 0, eaMapVars);
			pNewOption->bIsDefaultBackOption = true;
			{
				if (pOption->pUGCSearchAgentData){
					pDialog->pUGCSearchAgentData = StructClone(parse_UGCSearchAgentData, pOption->pUGCSearchAgentData);
					estrCopy2(&pDialog->pchDialogHeader, langTranslateMessage(entGetLanguage(pPlayerEnt), GET_REF(pOption->pUGCSearchAgentData->dialogTitle.hMessage)));
					estrCopy2(&pDialog->pchDialogText1, langTranslateMessage(entGetLanguage(pPlayerEnt), GET_REF(pOption->pUGCSearchAgentData->dialogText.hMessage)));
				}
			}
		xcase ContactDialogState_ImageMenu:
			pDialog->screenType = ContactScreenType_ImageMenu;
			pNewOption = contactdialog_CreateDialogOption(pPlayerEnt, &pDialog->eaOptions, "ImageMenu.Back", GET_REF(s_ContactMsgs.hBackMsg), ContactDialogState_OptionListFarewell, 0, eaMapVars);
			pNewOption->bIsDefaultBackOption = true;
			contactdialog_CreateOptionsList(pPlayerEnt, pContactDef, &pDialog->eaOptions, pDialog->bPartialPermissions);
			if (pContactDef->pImageMenuData){
				estrCopy2(&pDialog->pchDialogText1, langTranslateMessage(entGetLanguage(pPlayerEnt), GET_REF(pContactDef->pImageMenuData->title.hMessage)));
				estrCopy2(&pDialog->pchBackgroundImage, pContactDef->pImageMenuData->backgroundImage);
			}
		xcase ContactDialogState_MailBox:
			pDialog->screenType = ContactScreenType_MailBox;
			pNewOption = contactdialog_CreateDialogOption(pPlayerEnt, &pDialog->eaOptions, "MailBox.Back", GET_REF(s_ContactMsgs.hBackMsg), ContactDialogState_OptionListFarewell, 0, eaMapVars);
			pNewOption->bIsDefaultBackOption = true;
		xcase ContactDialogState_Minigame:
			pDialog->screenType = ContactScreenType_Minigame;
			pNewOption = contactdialog_CreateDialogOption(pPlayerEnt, &pDialog->eaOptions, "Minigame.Back", GET_REF(s_ContactMsgs.hBackMsg), ContactDialogState_OptionListFarewell, 0, eaMapVars);
			pNewOption->bIsDefaultBackOption = true;
			pDialog->eMinigameType = pContactDef->eMinigameType;
		xcase ContactDialogState_ItemAssignments:
			pDialog->screenType = ContactScreenType_ItemAssignments;
			pDialog->uItemAssignmentRefreshTime = SAFE_MEMBER(pContactDef->pItemAssignmentData, uRefreshTime);
			pNewOption = contactdialog_CreateDialogOption(pPlayerEnt, &pDialog->eaOptions, "ItemAssignments.Back", GET_REF(s_ContactMsgs.hBackMsg), ContactDialogState_OptionListFarewell, 0, eaMapVars);
			pNewOption->bIsDefaultBackOption = true;
		xcase ContactDialogState_Exit:
			interaction_EndInteractionAndDialog(entGetPartitionIdx(pPlayerEnt), pPlayerEnt, true, false, true);
			skip = true;
			break;
		}

		pDialog = pInteractInfo->pContactDialog;

		if (pDialog)
		{
			pDialog->eContactType = SAFE_MEMBER(pContactDef, type);
			if(pContactDef)
			{
				pDialog->eIndicator = contact_GetIndicatorState(pContactDef, pPlayerEnt, NULL);
			}
			else
			{
				pDialog->eIndicator = ContactIndicator_NoInfo;
			}
			pDialog->uiVersion++;
		}

		if (!skip)
		{
			contact_SendTextToChat(pPlayerEnt, pDialog);

			if (pDialog)
			{
				pDialog->bIsTeamSpokesman = false;
				pDialog->iParticipatingTeamMemberCount = 0;
			}

			// If this is a critical dialog and the talker is with a team, copy the dialog to all other team members
			if (pContactDef && 
				bShowToTeamMembers &&
				contact_CanStartTeamDialog(pPlayerEnt))
			{
				contact_InitializeDialogOptionsForTeammates(pPlayerEnt, pDialog);
			}
			else if (team_IsWithTeam(pPlayerEnt))
			{			
				if (pPlayerEnt->pTeam->bInTeamDialog || pPlayerEnt->pTeam->bIsTeamSpokesman)
				{
					pPlayerEnt->pTeam->bIsTeamSpokesman = false;
					pPlayerEnt->pTeam->bInTeamDialog = false;
					entity_SetDirtyBit(pPlayerEnt, parse_PlayerTeam, pPlayerEnt->pTeam, false);
				}
			}
		}
	}

	// List or Button contact screens (the standard screen types) should have some text on them.
	// Send an error message if a player ever sees a contact screen with no text.	
	if (pDialog && (pDialog->screenType == ContactScreenType_List || pDialog->screenType == ContactScreenType_Buttons)){
		if (!(pDialog->pchDialogText1 && pDialog->pchDialogText1[0]) && !(pDialog->pchDialogText2 && pDialog->pchDialogText2[0])){
			if (pContactDef){
				ErrorFilenamef(pContactDef->filename, "Contact has no text to display for State %s", StaticDefineIntRevLookup(ContactDialogStateEnum, pDialog->state));
			} else {
				if(pOption)
				{
					ContactDef* pOrigContactDef = pOption ? GET_REF(pOption->hTargetContactDef) : NULL;
					if(pOrigContactDef)
					{
						ErrorFilenamef(pContactDef->filename, "Contact has no text to display for State %s", StaticDefineIntRevLookup(ContactDialogStateEnum, pDialog->state));
					}
					else
					{
						Errorf("Contact Dialog has no text for State %s. Parameters: ContactDef:(null), ItemDef:%s, Root Mission:%s, Mission Offered:%s%s%s, Target Dialog:%s",
							StaticDefineIntRevLookup(ContactDialogStateEnum, pDialog->state),
							IS_HANDLE_ACTIVE(pOption->hItemDef)?REF_STRING_FROM_HANDLE(pOption->hItemDef):"(null)",
							IS_HANDLE_ACTIVE(pOption->hRootMission)?REF_STRING_FROM_HANDLE(pOption->hRootMission):"(null)",
							pOption->pMissionOfferParams && pOption->pMissionOfferParams->pchParentMission ? pOption->pMissionOfferParams->pchParentMission:"(null)",
							pOption->pMissionOfferParams && pOption->pMissionOfferParams->pchParentSubMission ? ":":"",
							pOption->pMissionOfferParams && pOption->pMissionOfferParams->pchParentSubMission ? pOption->pMissionOfferParams->pchParentSubMission : "",
							pOption->pchTargetDialogName);
					}
				}
				else
				{
					Errorf("Contact has no text to display for State %s. ContactDef and DialogOption are both null.", StaticDefineIntRevLookup(ContactDialogStateEnum, pDialog->state));
				}
			}
		}
	}

	eaDestroy(&eaMapVars);

	PERFINFO_AUTO_STOP_FUNC();
}

static void contactdialog_UseDefaultBackOption(Entity *pPlayerEnt, ContactDialog *pDialog)
{
	int i;

	// Find a default Back option to use
	for (i=eaSize(&pDialog->eaOptions)-1; i>=0; --i){
		if (pDialog->eaOptions[i]->bIsDefaultBackOption){
			ContactDialogOption *pOption = eaRemove(&pDialog->eaOptions, i);
			contact_ChangeDialogState(pPlayerEnt, pDialog->contactEnt, GET_REF(pDialog->hContactDef), pOption->pData);
			StructDestroy(parse_ContactDialogOption, pOption);
			return;
		}
	}

	// No default Back option was found; end dialog
	interaction_EndInteractionAndDialog(entGetPartitionIdx(pPlayerEnt), pPlayerEnt, false, true, true);
}

// Get the ItemAssignment seed for a ContactDef
// The seed equals the hashed ContactDef name + a container ID
static U32 contact_GetItemAssignmentSeed(SA_PARAM_NN_VALID Entity* pEnt, SA_PARAM_NN_VALID ContactDialog* pDialog, SA_PARAM_NN_VALID ContactDef* pContactDef)
{
	U32 uSeed = hashStringInsensitive(pContactDef->name);

	if (pDialog->bRemotelyAccessing)
	{
		// If the player is remotely interacting with a contact, then use the player's container ID
		uSeed += pEnt->myContainerID;
	}
	else
	{
		ZoneMapType eMapType = zmapInfoGetMapType(NULL);
		switch (eMapType)
		{
			xcase ZMTYPE_STATIC:
			{
				// If this is a static map, ensure the seed is global to all instances yet different
				// if across different maps.
				uSeed += hashStringInsensitive(zmapInfoGetCurrentName(NULL));
			}
			xcase ZMTYPE_SHARED:
			acase ZMTYPE_MISSION:
			{
				// If this is a shared, or mission map just use the map ID.
				// This does mean that starting a new map will generate an entirely new list of assignments.
				uSeed += gGSLState.gameServerDescription.baseMapDescription.containerID;
				// Also add the partition ID
				uSeed += partition_IDFromIdx(entGetPartitionIdx(pEnt));
			}
			xcase ZMTYPE_OWNED:
			{
				// If this is an owned map, use the map's owner ID.
				uSeed += partition_OwnerIDFromIdx(entGetPartitionIdx(pEnt));
			}
		}
	}
	return uSeed;
}

// This refreshes the structure that is sent to the client.
// No other actions should be taken here.
// Games with different UI flow may need to change this function
static void contact_RefreshDialogState(Entity* pPlayerEnt)
{
	InteractInfo* interactInfo;
	ContactDialog *pDialog;
	ContactDef *pContactDef;

	if (!pPlayerEnt || !pPlayerEnt->pPlayer || !pPlayerEnt->pPlayer->pInteractInfo) {
		return;
	}

	interactInfo = pPlayerEnt->pPlayer->pInteractInfo;
	pDialog = interactInfo->pContactDialog;

	if (!pDialog) {
		return;
	}

	pContactDef = GET_REF(pDialog->hContactDef);

	PERFINFO_AUTO_START_FUNC();

	switch(pDialog->state)
	{
	xcase ContactDialogState_Greeting:
		// No refresh needed

	xcase ContactDialogState_OptionList:
	{
		// Refresh all options
		// Not sure if I can/should refresh the text here as well.
		ContactDialogOption *pNewOption;
		WorldVariable **eaMapVars = NULL;

		PERFINFO_AUTO_START("OptionList", 1);

		eaClearStruct(&pDialog->eaOptions, parse_ContactDialogOption);

		if (!pContactDef) {
			PERFINFO_AUTO_STOP();
			break;
		}

		mapvariable_GetAllAsWorldVarsNoCopy(entGetPartitionIdx(pPlayerEnt), &eaMapVars);
		contactdialog_CreateOptionsList(pPlayerEnt, pContactDef, &pDialog->eaOptions, pDialog->bPartialPermissions);
		pNewOption = contactdialog_CreateDialogOption(pPlayerEnt, &pDialog->eaOptions, "OptionList.Exit", contact_GetDialogExitText(pContactDef), ContactDialogState_Exit, 0, eaMapVars);
		pNewOption->bIsDefaultBackOption = true;
		eaDestroy(&eaMapVars);

		PERFINFO_AUTO_STOP();
	}

	xcase ContactDialogState_OptionListFarewell:
	{
		// Refresh all options
		// Not sure if I can/should refresh the text here as well.
		ContactDialogOption *pNewOption;
		WorldVariable **eaMapVars = NULL;

		PERFINFO_AUTO_START("OptionListFarewell", 1);

		eaClearStruct(&pDialog->eaOptions, parse_ContactDialogOption);

		if (!pContactDef) {
			PERFINFO_AUTO_STOP();
			break;
		}

		mapvariable_GetAllAsWorldVarsNoCopy(entGetPartitionIdx(pPlayerEnt), &eaMapVars);
		contactdialog_CreateOptionsList(pPlayerEnt, pContactDef, &pDialog->eaOptions, pDialog->bPartialPermissions);
		pNewOption = contactdialog_CreateDialogOption(pPlayerEnt, &pDialog->eaOptions, "OptionList.Exit", contact_GetDialogExitText(pContactDef), ContactDialogState_Exit, 0, eaMapVars);
		pNewOption->bIsDefaultBackOption = true;
		eaDestroy(&eaMapVars);

		PERFINFO_AUTO_STOP();
	}
		
	xcase ContactDialogState_ContactInfo:
		// No refresh needed

	xcase ContactDialogState_SpecialDialog:
	{
		// Refresh all options
		SpecialDialogBlock *pSpecialDialog;
		WorldVariable **eaMapVars = NULL;

		PERFINFO_AUTO_START("SpecialDialog", 1);

		eaClearStruct(&pDialog->eaOptions, parse_ContactDialogOption);

		if (!pContactDef) {
			PERFINFO_AUTO_STOP();
			break;
		}

		mapvariable_GetAllAsWorldVarsNoCopy(entGetPartitionIdx(pPlayerEnt), &eaMapVars);
		pSpecialDialog = contact_SpecialDialogFromName(pContactDef, pDialog->pchSpecialDialogName);
		contactdialog_CreateSpecialDialogOptions(pPlayerEnt, entity_GetSavedExpLevel(pPlayerEnt), pContactDef, pDialog, pSpecialDialog, true, eaMapVars);
		eaDestroy(&eaMapVars);

		if (pDialog->bIsTeamSpokesman &&
			pSpecialDialog && (pSpecialDialog->eFlags & SpecialDialogFlags_CriticalDialog))
		{
			contact_UpdateDialogOptionsForTeammates(pPlayerEnt, pDialog);
		}

		PERFINFO_AUTO_STOP();
	}

	xcase ContactDialogState_ViewOfferedNamespaceMission:
		// No refresh needed

	xcase ContactDialogState_ViewOfferedMission:
	{
		// If Mission can no longer be offered, return to options list
		MissionDef *pMissionDef;

		PERFINFO_AUTO_START("ViewOfferedMission", 1);
		
		pMissionDef = GET_REF(pDialog->hRootMissionDef);
		if (!pMissionDef || !missiondef_CanBeOfferedAtAll(pPlayerEnt, pMissionDef, NULL, NULL, NULL)){
			contactdialog_UseDefaultBackOption(pPlayerEnt, pDialog);
		}

		// If mission's timer has already expired, return to options list
		if (pDialog->uMissionExpiredTime && timeSecondsSince2000() > pDialog->uMissionExpiredTime){
			contactdialog_UseDefaultBackOption(pPlayerEnt, pDialog);
		}

		PERFINFO_AUTO_STOP();
	}

	xcase ContactDialogState_ViewInProgressMission:
	{
		// If Mission is no longer in progress, return to options list
		// We also view In Progress text for Completed missions that can't be returned here
		MissionDef *pMissionDef;

		PERFINFO_AUTO_START("ViewInProgressMission", 1);
		
		pMissionDef = GET_REF(pDialog->hRootMissionDef);
		if (pMissionDef && pContactDef){
			ContactMissionOffer *pOffer = contact_GetMissionOffer(pContactDef, pPlayerEnt, pMissionDef);
			MissionInfo *missionInfo = mission_GetInfoFromPlayer(pPlayerEnt);
			Mission *pMission = pMissionDef?mission_FindMissionFromDef(missionInfo, pMissionDef):NULL;
			if (!pMission || !(pMission->state == MissionState_InProgress || (pMission->state == MissionState_Succeeded && !contact_MissionOfferAllowsTurnIn(pOffer)))){
				contactdialog_UseDefaultBackOption(pPlayerEnt, pDialog);
			}
		}

		PERFINFO_AUTO_STOP();
	}

	xcase ContactDialogState_ViewFailedMission:
	{
		// If Mission is no longer failed, return to options list
		MissionDef *pMissionDef;
		MissionInfo *missionInfo;
		Mission *pMission = NULL;

		PERFINFO_AUTO_START("ViewFailedMission", 1);

		pMissionDef = GET_REF(pDialog->hRootMissionDef);
		if (pMissionDef) {
			missionInfo = mission_GetInfoFromPlayer(pPlayerEnt);
			pMission = mission_FindMissionFromDef(missionInfo, pMissionDef);
		}

		if (!pMission || pMission->state != MissionState_Failed){
			contactdialog_UseDefaultBackOption(pPlayerEnt, pDialog);
		}

		// If mission's timer has already expired, return to options list
		if (pDialog->uMissionExpiredTime && timeSecondsSince2000() > pDialog->uMissionExpiredTime){
			contactdialog_UseDefaultBackOption(pPlayerEnt, pDialog);
		}

		PERFINFO_AUTO_STOP();
	}

	xcase ContactDialogState_ViewCompleteMission:
	{
		// If Mission can no longer be turned in, return to options list
		MissionDef *pMissionDef;
		MissionInfo *missionInfo;
		Mission *pMission = NULL;

		PERFINFO_AUTO_START("ViewCompleteMission", 1);

		pMissionDef = GET_REF(pDialog->hRootMissionDef);
		if (pMissionDef) {
			missionInfo = mission_GetInfoFromPlayer(pPlayerEnt);
			pMission = mission_FindMissionFromDef(missionInfo, pMissionDef);
		}

		if (!pMission || pMission->state != MissionState_Succeeded){
			contactdialog_UseDefaultBackOption(pPlayerEnt, pDialog);
		}

		PERFINFO_AUTO_STOP();
	}

	xcase ContactDialogState_ViewSubMission:
	{
		// If sub-mission can no longer be turned in, return to options list
		MissionDef *pMissionDef;
		MissionInfo *missionInfo;
		Mission *pMission = NULL;

		PERFINFO_AUTO_START("ViewSubMission", 1);

		pMissionDef = GET_REF(pDialog->hRootMissionDef);
		if (pDialog->pchSubMissionName){
			pMissionDef = missiondef_ChildDefFromName(pMissionDef, pDialog->pchSubMissionName);
		}
		if (pMissionDef) {
			missionInfo = mission_GetInfoFromPlayer(pPlayerEnt);
			pMission = mission_FindMissionFromDef(missionInfo, pMissionDef);
		}
		if (!pMission || pMission->state != MissionState_InProgress){
			contactdialog_UseDefaultBackOption(pPlayerEnt, pDialog);
		}

		PERFINFO_AUTO_STOP();
	}

	xcase ContactDialogState_MissionSearch:
		// No refresh needed
	xcase ContactDialogState_MissionSearchViewContact:
		// No refresh needed
	xcase ContactDialogState_ViewLore:
		// No refresh needed

	xcase ContactDialogState_Store:
	case ContactDialogState_RecipeStore:
	case ContactDialogState_InjuryStore:
	case ContactDialogState_StoreCollection:
	{
		GameAccountDataExtract *pExtract;

		PERFINFO_AUTO_START("Store", 1);
		
		pExtract = entity_GetCachedGameAccountDataExtract(pPlayerEnt);
		store_RefreshStoreItemInfo(pPlayerEnt, &pDialog->eaStoreItems, &pDialog->eaUnavailableStoreItems, pExtract);
		store_UpdateStoreProvisioning(pPlayerEnt, pDialog, pExtract);

		PERFINFO_AUTO_STOP();
	}

	xcase ContactDialogState_PowerStore:
	{
		GameAccountDataExtract *pExtract;

		PERFINFO_AUTO_START("PowerStore", 1);
		
		pExtract = entity_GetCachedGameAccountDataExtract(pPlayerEnt);
		powerstore_RefreshStorePowerInfo(pPlayerEnt, &pDialog->eaStorePowers);

		PERFINFO_AUTO_STOP();
	}

	xcase ContactDialogState_ItemAssignments:
	{
		PERFINFO_AUTO_START("ContactItemAssignments", 1);

		if (pContactDef && pContactDef->pItemAssignmentData)
		{
			S32 i;
			U32 uRefreshIndex = 0;
			ItemAssignmentLocationData LocationData = {0};
			gslItemAssignments_GetPlayerLocationDataEx(pPlayerEnt, true, &LocationData);

			if (pContactDef->pItemAssignmentData->uRefreshTime)
			{
				U32 uCurrentTime = timeSecondsSince2000();
				U32 uRefreshTime = pContactDef->pItemAssignmentData->uRefreshTime;
				uRefreshIndex = uCurrentTime / uRefreshTime;
				uRefreshIndex += gslItemAssignment_GetRefreshIndexOffset();
			}

			if (!pDialog->bGeneratedItemAssignments || 
				uRefreshIndex != pDialog->uItemAssignmentRefreshIndex)
			{
				U32 uSeed = contact_GetItemAssignmentSeed(pPlayerEnt, pDialog, pContactDef) + uRefreshIndex;
				eaClearStruct(&pDialog->eaAllItemAssignments, parse_ItemAssignmentDefRef);
				gslItemAssignments_GenerateAssignmentList(pPlayerEnt, 
														  (ItemAssignmentRarityCountType*)pContactDef->pItemAssignmentData->peRarityCounts,
														  &LocationData,
														  &uSeed,
														  &pDialog->eaAllItemAssignments);
				pDialog->bGeneratedItemAssignments = true;
				pDialog->uItemAssignmentRefreshIndex = uRefreshIndex;
			}

			for (i = eaSize(&pDialog->eaItemAssignments)-1; i >= 0; i--)
			{
				pDialog->eaItemAssignments[i]->bDirty = false;
			}
			// Filter the assignment list
			for (i = 0; i < eaSize(&pDialog->eaAllItemAssignments); i++)
			{
				ItemAssignmentDefRef* pRef = pDialog->eaAllItemAssignments[i];
				ItemAssignmentDef* pDef = GET_REF(pRef->hDef);
				if (gslItemAssignments_IsValidAvailableAssignment(pPlayerEnt, pDef, &LocationData))
				{
					ItemAssignmentDefRef* pNewRef = eaIndexedGetUsingString(&pDialog->eaItemAssignments, REF_STRING_FROM_HANDLE(pRef->hDef));
					if (!pNewRef)
					{
						pNewRef = StructCreate(parse_ItemAssignmentDefRef);
						COPY_HANDLE(pNewRef->hDef, pRef->hDef);
						eaIndexedEnable(&pDialog->eaItemAssignments, parse_ItemAssignmentDefRef);
						eaPush(&pDialog->eaItemAssignments, pNewRef);
					}
					pNewRef->bFeatured = gslItemAssignment_IsFeatured(pDef);
					pNewRef->bDirty = true;
				}
				else
				{
					S32 iIndex = eaIndexedFindUsingString(&pDialog->eaItemAssignments, REF_STRING_FROM_HANDLE(pRef->hDef));
					if (iIndex >= 0)
					{
						StructDestroy(parse_ItemAssignmentDefRef, eaRemove(&pDialog->eaItemAssignments, iIndex));
					}
				}
			}
			for (i = eaSize(&pDialog->eaItemAssignments)-1; i >= 0; i--)
			{
				if (!pDialog->eaItemAssignments[i]->bDirty)
				{
					StructDestroy(parse_ItemAssignmentDefRef, eaRemove(&pDialog->eaItemAssignments, i));
				}
			}
		}

		PERFINFO_AUTO_STOP();
	}

	// - To be determined -
	xcase ContactDialogState_Tailor:
	xcase ContactDialogState_StarshipChooser:
	xcase ContactDialogState_Minigame:
	xcase ContactDialogState_NewNemesis:
	xcase ContactDialogState_Nemesis:
	xcase ContactDialogState_Guild:
	xcase ContactDialogState_NewGuild:
	xcase ContactDialogState_Respec:
	xcase ContactDialogState_Bank:
	xcase ContactDialogState_SharedBank:
	xcase ContactDialogState_ZStore:
	xcase ContactDialogState_GuildBank:
	xcase ContactDialogState_MailBox:
		break;
	}

	PERFINFO_AUTO_STOP_FUNC();
}

bool contactdialog_CreateNamespaceMissionGrantContact(Entity* pEnt, 
													  const char* pchMission,
													  PlayerCostume* pCostume,
													  PetContactList* pPetContactList,
													  const char* pchMissionDetails)
{
	if (pEnt && pchMissionDetails && (pCostume || pPetContactList))
	{
		NamespacedMissionContactData contactData = {0};
		ContactDialogOptionData initialOption = {0};
		initialOption.targetState = ContactDialogState_ViewOfferedNamespaceMission;
		SET_HANDLE_FROM_STRING("MissionDef", pchMission, initialOption.hRootMission);
		initialOption.pNamespacedMissionContactData = &contactData;
		contactData.pchMissionDetails = pchMissionDetails;
		SET_HANDLE_FROM_REFERENT("PlayerCostume", pCostume, contactData.hContactCostume);
		SET_HANDLE_FROM_REFERENT("PetContactList", pPetContactList, contactData.hPetContactList);
		
		contact_InteractEnd(pEnt, true); // Kill any previous dialog

		// Replace any previous interact state the player might have had with this new one
		contact_InteractBeginWithOptionData(pEnt, &initialOption);
		REMOVE_HANDLE(initialOption.hRootMission);
		REMOVE_HANDLE(contactData.hContactCostume);
		REMOVE_HANDLE(contactData.hPetContactList);
		return true;
	}
	return false;
}

// Create an interact state that is just a mission offer
static void contact_CreateSingleMissionOfferState(SA_PARAM_OP_VALID Entity* pPlayerEnt, 
												  SA_PARAM_NN_VALID MissionDef* pMissionDef, 
												  MissionOfferParams *pParams,
												  const char* estrHeadshotDisplayName,
												  ContactHeadshotData *pHeadshotData)
{
	InteractInfo* info = SAFE_MEMBER2(pPlayerEnt, pPlayer, pInteractInfo);

	if(info && pMissionDef)
	{
		ContactDef *pContactDef = info->pContactDialog ? GET_REF(info->pContactDialog->hContactDef) : NULL;
		ContactDialogOptionData initialOption = {0};
		initialOption.targetState = ContactDialogState_ViewOfferedMission;
		initialOption.pMissionOfferParams = pParams;
		initialOption.pchHeadshotDisplayName = estrHeadshotDisplayName; // Note we are not copying the string but passing along the pointer.
																		// Given the ephemeral nature of the OptionData, this is okay
		initialOption.pHeadshotData = pHeadshotData;

		SET_HANDLE_FROM_REFERENT(g_MissionDictionary, pMissionDef, initialOption.hRootMission);

		// Replace any previous interact state the player might have had with this new one
		contact_ChangeDialogState(pPlayerEnt, 0, pContactDef, &initialOption);

		REMOVE_HANDLE(initialOption.hRootMission);
	}
}

// Create an interact state that is just a mission offer
static void contact_CreateSingleMissionRestartState(SA_PARAM_OP_VALID Entity* pPlayerEnt, 
													SA_PARAM_NN_VALID MissionDef* pMissionDef, 
													MissionOfferParams *pParams,
													const char* estrHeadshotDisplayName,
													ContactHeadshotData *pHeadshotData)
{
	InteractInfo* info = SAFE_MEMBER2(pPlayerEnt, pPlayer, pInteractInfo);

	if(info && pMissionDef)
	{
		ContactDialogOptionData initialOption = {0};
		initialOption.targetState = ContactDialogState_ViewFailedMission;
		initialOption.pMissionOfferParams = pParams;
		initialOption.pHeadshotData = pHeadshotData;
		initialOption.pchHeadshotDisplayName = estrHeadshotDisplayName; // Note we are not copying the string but passing along the pointer.
																		// Given the ephemeral nature of the OptionData, this is okay.

		SET_HANDLE_FROM_REFERENT(g_MissionDictionary, pMissionDef, initialOption.hRootMission);

		// Replace any previous interact state the player might have had with this new one
		contact_ChangeDialogState(pPlayerEnt, 0, NULL, &initialOption);

		REMOVE_HANDLE(initialOption.hRootMission);
	}
}

bool contact_HasSpecialDialogForPlayer(Entity* playerEnt, ContactDef* contact)
{
	SpecialDialogBlock** eaSpecialDialogs = NULL;
	int i, j, size;
	bool expressionTrue = false;
	ContactDialog *pCurrentDialog;

	PERFINFO_AUTO_START_FUNC();
	
	// Get the current dialog of the player
	pCurrentDialog = SAFE_MEMBER3(playerEnt, pPlayer, pInteractInfo, pContactDialog);

	// If the player is talking to this contact we don't need to validate at all
	if (pCurrentDialog && 
		contact == GET_REF(pCurrentDialog->hContactDef))
	{
		PERFINFO_AUTO_STOP();
		return true;
	}

	contact_GetSpecialDialogs(contact, &eaSpecialDialogs);
	size = eaSize(&eaSpecialDialogs);

	for (i = 0; i < size; i++)
	{
		SpecialDialogBlock* pDialog = eaSpecialDialogs[i];
		if(pDialog->bUsesLocalCondExpression)
		{
			// Check root condition
			if(pDialog->pCondition)
			{
				expressionTrue = contact_Evaluate(playerEnt, pDialog->pCondition, NULL);
			}
			else
			{
				expressionTrue = true;
			}
			// Check for valid dialog block
			if(expressionTrue)
			{
				bool bDialogFound = false;
				if(pDialog->dialogBlock)
				{
					for(j = eaSize(&pDialog->dialogBlock)-1; j >= 0 && !bDialogFound; j--)
					{
						bDialogFound = bDialogFound || (!pDialog->dialogBlock[j]->condition || contact_Evaluate(playerEnt, pDialog->dialogBlock[j]->condition, NULL));
					}
				}
				expressionTrue = expressionTrue && bDialogFound;
			}
		}
		else if(eaSize(&pDialog->dialogBlock) > 0) {
			if(pDialog->dialogBlock[0]->condition)
			{
				expressionTrue = contact_Evaluate(playerEnt, pDialog->dialogBlock[0]->condition, NULL);
			}
			else
				expressionTrue = true;
		}

		if(expressionTrue) {
			if(eaSpecialDialogs)
				eaDestroy(&eaSpecialDialogs);
			PERFINFO_AUTO_STOP();
			return true;
		}
	}
	if(eaSpecialDialogs)
		eaDestroy(&eaSpecialDialogs);
	PERFINFO_AUTO_STOP();
	return false;
}

// Creates a potential pet contact from an item with a PetDef
bool contact_CreatePotentialPetFromItem(SA_PARAM_OP_VALID Entity* pPlayerEnt, SA_PARAM_OP_VALID Entity* pEntSrc, 
										S32 iBagID, S32 iSlot, SA_PARAM_NN_VALID ItemDef* pItemDef, 
										bool bFirstInteract, bool bQueueContact)
{
	bool bSuccess = false;
	if (pPlayerEnt && pPlayerEnt->pPlayer && pPlayerEnt->pPlayer->pInteractInfo)
	{
		ContactDialogOptionData initialOption = {0};
		initialOption.targetState = ContactDialogState_BridgeOfficerOfferSelfOrTraining;
		initialOption.iEntType = pEntSrc ? entGetType(pEntSrc): 0;
		initialOption.iEntID = pEntSrc ? entGetContainerID(pEntSrc) : 0;
		initialOption.iItemBagID = iBagID;
		initialOption.iItemSlot = iSlot; 
		initialOption.bFirstInteract = bFirstInteract;
		SET_HANDLE_FROM_REFERENT(g_hItemDict, pItemDef, initialOption.hItemDef);
	
		if (bQueueContact)
		{
			contact_addQueuedContactFromOptionData(pPlayerEnt, &initialOption);
		}
		else
		{
			contact_InteractBeginWithOptionData(pPlayerEnt, &initialOption);
		}
		REMOVE_HANDLE(initialOption.hItemDef);
		bSuccess = true;
	}
	return bSuccess;
}

// Creates a trainer contact using the trainable node list on an item
bool contact_CreateTrainerFromEntity(Entity* pPlayerEnt, SA_PARAM_OP_VALID Entity* pTrainer)
{
	if (pTrainer)
	{
		ContactDialogOptionData initialOption = {0};

		initialOption.targetState = ContactDialogState_TrainerFromEntity;
		initialOption.iEntType = entGetType(pTrainer);
		initialOption.iEntID = entGetContainerID(pTrainer);

		contact_InteractBeginWithOptionData(pPlayerEnt, &initialOption);
		return true;
	}
	return false;
}

bool contact_OfferMissionFromItem(Entity* playerEnt, ItemDef* itemDef, MissionDef* missionDef)
{
	InteractInfo* info = SAFE_MEMBER2(playerEnt, pPlayer, pInteractInfo);
	const char* errMesgKey = NULL;
	bool retVal = false;

	// Make sure the player meets the requirements for this mission
	if(info && playerEnt && contact_ItemCanOfferMission(itemDef, missionDef, playerEnt, &errMesgKey))
	{
		contact_InteractEnd(playerEnt, true);	// Kill any previous dialog

		SET_HANDLE_FROM_REFERENT(g_hItemDict, itemDef, info->interactItem);
		entity_SetDirtyBit(playerEnt, parse_InteractInfo, playerEnt->pPlayer->pInteractInfo, false);
		entity_SetDirtyBit(playerEnt, parse_Player, playerEnt->pPlayer, false);

		contact_CreateSingleMissionOfferState(playerEnt, missionDef, NULL, NULL, NULL);

		retVal = true;
	}
	else if(playerEnt && errMesgKey)
	{
		// Throw up an error message
		const char* errText = langTranslateMessageKey( entGetLanguage(playerEnt), errMesgKey);
		if(errText)
			notify_NotifySend(playerEnt, kNotifyType_MissionGrantItemFailed, errText, itemDef ? itemDef->pchName : NULL, NULL);
		else
			Errorf("Failed to get error string for item mission offer: message key was %s", errMesgKey);
	}

	return retVal;
}

// Offers the next Queued/Shared Mission in the player's Mission queue
void contact_OfferNextQueuedMission(SA_PARAM_OP_VALID Entity* playerEnt)
{
	InteractInfo *pInteractInfo = SAFE_MEMBER2(playerEnt, pPlayer, pInteractInfo);
	MissionInfo *pMissionInfo = mission_GetInfoFromPlayer(playerEnt);

	if (pMissionInfo && eaSize(&pMissionInfo->eaQueuedMissionOffers))
	{
		QueuedMissionOffer *pOfferInfo = eaRemove(&pMissionInfo->eaQueuedMissionOffers, 0);
		MissionDef *pDef = GET_REF(pOfferInfo->hMissionDef);
		Mission *pMission = mission_GetMissionFromDef(pMissionInfo, pDef);
		MissionOfferParams params = {0};
		params.eSharerType = pOfferInfo->eSharerType;
		params.uSharerID = pOfferInfo->uSharerID;
		params.uTimerStartTime = pOfferInfo->uTimerStartTime;
		params.eCreditType = pOfferInfo->eCreditType;

		interaction_EndInteractionAndDialog(entGetPartitionIdx(playerEnt), playerEnt, false, true, true);	// Kill any previous dialog
		
		if (pDef && pMission && pMission->state == MissionState_Failed){
			pInteractInfo->pSharedMission = pOfferInfo;
			entity_SetDirtyBit(playerEnt, parse_InteractInfo, pInteractInfo, true);
			entity_SetDirtyBit(playerEnt, parse_Player, playerEnt->pPlayer, false);
			contact_CreateSingleMissionRestartState(playerEnt, pDef, &params, NULL, NULL);
		}
		else if (pDef && !pMission){
			pInteractInfo->pSharedMission = pOfferInfo;
			entity_SetDirtyBit(playerEnt, parse_InteractInfo, pInteractInfo, true);
			entity_SetDirtyBit(playerEnt, parse_Player, playerEnt->pPlayer, false);
			contact_CreateSingleMissionOfferState(playerEnt, pDef, &params, NULL, NULL);
		}
		else{
			// Some kind of error
			mission_SharedMissionDeclined(playerEnt, pOfferInfo);
			StructDestroy(parse_QueuedMissionOffer, pOfferInfo);
		}
	}
}

static bool contact_OffersMission(SA_PARAM_NN_VALID ContactDef *pContactDef, Entity *pPlayerEnt, MissionDef *pMissionDef)
{
	S32 i;
	ContactMissionOffer** eaOfferList = NULL;

	devassert(pContactDef);
	devassert(pMissionDef);

	if (pContactDef == NULL || pMissionDef == NULL)
		return false;

	contact_GetMissionOfferList(pContactDef, pPlayerEnt, &eaOfferList);

	for (i = 0; i < eaSize(&eaOfferList); i++)
	{
		MissionDef *pMissionDefFromOffer = GET_REF(eaOfferList[i]->missionDef);
		if (pMissionDefFromOffer && stricmp(pMissionDefFromOffer->name, pMissionDef->name) == 0)
		{
			eaDestroy(&eaOfferList);
			return true;
		}
	}

	if(eaOfferList)
		eaDestroy(&eaOfferList);

	return false;
}

// Offer a mission with the given headshot and headshot name. These entries can be null.
bool contact_OfferMissionWithHeadshot(Entity* playerEnt, MissionDef* missionDef, const char* estrHeadshotDisplayName, ContactHeadshotData* pHeadshotData)
{
	InteractInfo* info = SAFE_MEMBER2(playerEnt, pPlayer, pInteractInfo);
	MissionInfo *pMissionInfo = mission_GetInfoFromPlayer(playerEnt);
	Mission *pMission = mission_GetMissionFromDef(pMissionInfo, missionDef);
	ContactDef *pContactDef = info && info->pContactDialog ? GET_REF(info->pContactDialog->hContactDef) : NULL;

	// Make sure the player meets the requirements for this mission
	if(info && playerEnt && missiondef_CanBeOfferedAsPrimary(playerEnt, missionDef, NULL, NULL))
	{
		bool bKillPreviousDialog = true;		

		// Do not kill the dialog if the contact offers this mission
		if (gConf.bDoNotEndDialogForMissionOfferActionIfContactOffers && pContactDef && contact_OffersMission(pContactDef, playerEnt, missionDef))
		{
			bKillPreviousDialog = false;
		}

		if (bKillPreviousDialog)
		{
			// Kill any previous dialog
			// TODO - If the previous contact was a Crime Computer, preserve the headshot somehow
			contact_InteractEnd(playerEnt, true);
		}

		if (missionDef && !pMission){
			contact_CreateSingleMissionOfferState(playerEnt, missionDef, NULL, estrHeadshotDisplayName, pHeadshotData);
			return true;
		} else if (pMission && pMission->state == MissionState_Failed){
			contact_CreateSingleMissionRestartState(playerEnt, missionDef, NULL, estrHeadshotDisplayName, pHeadshotData);
			return true;
		}
	}

	return false;
}

static void contact_GiveLoreItem(Entity* pPlayerEnt, ItemDef *pItemDef, const ItemChangeReason *pReason)
{
	if(pPlayerEnt && pItemDef && pItemDef->eType == kItemType_Lore)
	{
		// If the player doesn't already have this lore item, grant it to them
		GameAccountDataExtract *pExtract = entity_GetCachedGameAccountDataExtract(pPlayerEnt);
		if (inv_ent_CountItems(pPlayerEnt, InvBagIDs_Lore, pItemDef->pchName, pExtract) == 0){
			// TODO - Maybe grant XP here?
			Item *item = item_FromDefName(pItemDef->pchName);
			invtransaction_AddItem(pPlayerEnt, InvBagIDs_Lore, -1, item, 0, pReason, NULL, NULL);
			StructDestroy(parse_Item,item);
		}
	}
}

// Displays a Lore Item screen to the player, and unlocks the piece of Lore if the player doesn't already have it
void contact_DisplayLoreItem(Entity* pPlayerEnt, ItemDef *pItemDef)
{
	if(pItemDef && pItemDef->eType == kItemType_Lore)
	{
		ContactDialogOptionData initialOption = {0};
		ItemChangeReason reason = {0};

		initialOption.targetState = ContactDialogState_ViewLore;
		SET_HANDLE_FROM_REFERENT(g_hItemDict, pItemDef, initialOption.hItemDef);

		// Replace any previous interact state the player might have had with this new one
		contact_ChangeDialogState(pPlayerEnt, 0, NULL, &initialOption);

		REMOVE_HANDLE(initialOption.hItemDef);
		inv_FillItemChangeReason(&reason, pPlayerEnt, "Contact:DisplayLore", pItemDef->pchName);

		contact_GiveLoreItem(pPlayerEnt, pItemDef, &reason);
	}
}

bool contact_CreatePowerStoreFromItem(Entity* pPlayerEnt, S32 iType, U32 iID, S32 iBagID, S32 iSlot, U64 iItemID, bool bIsOfficerTrainer)
{
	Entity* pEnt = entity_GetSubEntity(entGetPartitionIdx(pPlayerEnt), pPlayerEnt, iType, iID);
	Item* pSrcItem = pEnt ? inv_GetItemFromBag(pEnt, iBagID, iSlot, NULL) : NULL;
	
	if (pPlayerEnt && pSrcItem && pSrcItem->id == iItemID)
	{
		ContactDialogOptionData initialOption = {0};

		contact_InteractEnd(pPlayerEnt, true);	// Kill any previous dialog

		initialOption.targetState = ContactDialogState_PowerStoreFromItem;
		initialOption.iEntType = iType;
		initialOption.iEntID = iID;
		initialOption.iItemBagID = iBagID;
		initialOption.iItemSlot = iSlot;
		initialOption.bIsOfficerTrainer = bIsOfficerTrainer;
		SET_HANDLE_FROM_REFERENT(g_hItemDict, GET_REF(pSrcItem->hItem), initialOption.hItemDef);
	
		// Replace any previous interact state the player might have had with this new one
		contact_ChangeDialogState(pPlayerEnt, 0, NULL, &initialOption);

		REMOVE_HANDLE(initialOption.hItemDef);

		return true;
	}

	return false;
}

bool contactdialog_GivePetFromItem(Entity* pPlayerEnt, S32 iType, U32 iID, S32 iBagID, S32 iSlot, U64 iKey)
{
	GameAccountDataExtract *pExtract = entity_GetCachedGameAccountDataExtract(pPlayerEnt);
	Entity* pEntSrc = entity_GetSubEntity(entGetPartitionIdx(pPlayerEnt),pPlayerEnt,iType,iID);
	Item* pSrcItem = inv_GetItemFromBag( pEntSrc ? pEntSrc : pPlayerEnt, iBagID, iSlot, pExtract);
	ItemDef* pSrcItemDef = pSrcItem ? GET_REF(pSrcItem->hItem) : NULL;

	if ( pPlayerEnt && pSrcItemDef && pSrcItem->id == iKey )
	{
		PetDef *pSavedPetDef = GET_REF(pSrcItemDef->hPetDef);

		if (pSavedPetDef)
		{
			if(pSrcItemDef->bMakeAsPuppet)
			{
				gslCreateNewPuppetFromDef(entGetPartitionIdx(pPlayerEnt),pPlayerEnt,pEntSrc,pSavedPetDef,pSrcItemDef->iLevel,pSrcItem->id,pExtract);
			}
			else if (pSrcItem->pSpecialProps)
			{
				PropEntIDs propEntIDs = { 0 };
				PropEntIDs_FillWithActiveEntIDs(&propEntIDs, pPlayerEnt);
				gslCreateSavedPetFromDef(entGetPartitionIdx(pPlayerEnt),pPlayerEnt,pEntSrc,pSrcItem->pSpecialProps->pAlgoPet,pSavedPetDef,pSrcItemDef->iLevel,NULL,NULL,pSrcItem->id,OWNEDSTATE_ACTIVE,&propEntIDs,true,pExtract);
				PropEntIDs_Destroy(&propEntIDs);
			}
			return true;
		}
	}

	return false;
}

static void contact_BeginQueuedContactDialog(Entity* pPlayerEnt, QueuedContactDialog** ppQueuedContactDialog)
{
	InteractInfo* info = SAFE_MEMBER2(pPlayerEnt, pPlayer, pInteractInfo);
	QueuedContactDialog* pQueuedContactDialog = *ppQueuedContactDialog;
	*ppQueuedContactDialog = NULL;

	if (!info || info->pContactDialog || info->pSharedMission)
		return;
	
	if(pQueuedContactDialog->pContactDialog) {
		// If this dialog has been viewed recently, `then ignore it
		if(info->recentlyCompletedDialogs) {
			int i;
			for(i = 0; i < eaSize(&info->recentlyCompletedDialogs); i++) {
				if(	stricmp(info->recentlyCompletedDialogs[i]->pcDialogName, pQueuedContactDialog->pContactDialog->pcDialogName) == 0 &&
					REF_COMPARE_HANDLES(info->recentlyCompletedDialogs[i]->hContact, pQueuedContactDialog->pContactDialog->hContact)) {
						return;
				}
			}
		}

		if(info->recentlyViewedForceOnTeamDialogs) {
			int i;
			for(i = 0; i < eaSize(&info->recentlyViewedForceOnTeamDialogs); i++) {
				if(	stricmp(info->recentlyViewedForceOnTeamDialogs[i]->pcDialogName, pQueuedContactDialog->pContactDialog->pcDialogName) == 0 &&
					REF_COMPARE_HANDLES(info->recentlyViewedForceOnTeamDialogs[i]->hContact, pQueuedContactDialog->pContactDialog->hContact)) {
						return;
				}
			}
		}


		info->bPartialPermissions = pQueuedContactDialog->bPartialPermissions;
		info->bRemotelyAccessing = pQueuedContactDialog->bRemotelyAccessing;
		if (pQueuedContactDialog->pOptionData) {
			contact_InteractBeginWithOptionData(pPlayerEnt, pQueuedContactDialog->pOptionData);
		} else {
			contact_InteractBegin(pPlayerEnt, NULL, GET_REF(pQueuedContactDialog->pContactDialog->hContact), pQueuedContactDialog->pContactDialog->pcDialogName, pQueuedContactDialog->pCostumeFallback);
		}
	}

	StructDestroy(parse_QueuedContactDialog, pQueuedContactDialog);
}

// Begins the queued contact for the player only if the player is not viewing a contact
void contact_MaybeBeginQueuedContact(Entity* pPlayerEnt)
{
	InteractInfo* info = SAFE_MEMBER2(pPlayerEnt, pPlayer, pInteractInfo);

	if(SAFE_MEMBER(info, pQueuedContactDialog)) {
		contact_BeginQueuedContactDialog(pPlayerEnt, &info->pQueuedContactDialog);
	}
}

// Begins the not in combat contact for the player if the player is not in combat
void contact_MaybeBeginNotInCombatContact(Entity* pPlayerEnt)
{
	InteractInfo* info = SAFE_MEMBER2(pPlayerEnt, pPlayer, pInteractInfo);

	if(SAFE_MEMBER(info, pQueuedNotInCombatDialog) && pPlayerEnt->pChar && pPlayerEnt->pChar->uiTimeCombatExit == 0) {
		contact_BeginQueuedContactDialog(pPlayerEnt, &info->pQueuedNotInCombatDialog);
	}
}

// If the player currently has a contact dialog open, this function adds the specified dialog to the player's queue.
// Otherwise, the specified dialog will launch immediately
void contact_addQueuedContact(Entity* pPlayerEnt, ContactDef* pContactDef, const char* pchDialogName, bool bPartialPermissions, bool bRemotelyAccessing, PlayerCostume* pCostumeFallback, char* pchDisplayNameFallback, bool bClearPreviousDialog) {
	InteractInfo* info = SAFE_MEMBER2(pPlayerEnt, pPlayer, pInteractInfo);

	if(info) {
		// If this dialog is currently being viewed, ignore it
		if(info->pContactDialog &&
			GET_REF(info->pContactDialog->hContactDef) == pContactDef &&
			stricmp(info->pContactDialog->pchSpecialDialogName, pchDialogName) == 0 &&
			info->bPartialPermissions == bPartialPermissions &&
			info->bRemotelyAccessing == bRemotelyAccessing)
		{
			return;
		}
		
		// If this dialog has been viewed recently, then ignore it
		if(info->recentlyCompletedDialogs) {
			int i;
			for(i = 0; i < eaSize(&info->recentlyCompletedDialogs); i++) {
				if(	stricmp(info->recentlyCompletedDialogs[i]->pcDialogName, pchDialogName) == 0 &&
					GET_REF(info->recentlyCompletedDialogs[i]->hContact) == pContactDef) {
						return;
				}
			}
		}

		if(info->recentlyViewedForceOnTeamDialogs) {
			int i;
			for(i = 0; i < eaSize(&info->recentlyViewedForceOnTeamDialogs); i++) {
				if(	stricmp(info->recentlyViewedForceOnTeamDialogs[i]->pcDialogName, pchDialogName) == 0 &&
					GET_REF(info->recentlyViewedForceOnTeamDialogs[i]->hContact) == pContactDef) {
						return;
				}
			}
		}

		if (bClearPreviousDialog) {
			// Kill any previous dialog
			contact_InteractEnd(pPlayerEnt, true);
		}

		// Check to make sure the player is interacting
		if( !info->pSharedMission && !info->pContactDialog ) {
			if(info->pQueuedContactDialog) {
				// If another dialog is on the queue and the player is not interacting, use that one
				contact_MaybeBeginQueuedContact(pPlayerEnt);
			} else {
				// Otherwise, no need to queue
				ContactCostumeFallback *pFallback = NULL;
				if(pchDisplayNameFallback || pCostumeFallback) {
					pFallback = StructCreate(parse_ContactCostumeFallback);
					if(pchDisplayNameFallback)
						pFallback->pchDisplayName = StructAllocString(pchDisplayNameFallback);
					if(pCostumeFallback)
						SET_HANDLE_FROM_REFERENT("PlayerCostume", pCostumeFallback, pFallback->hCostume);
				}
				info->bPartialPermissions = bPartialPermissions;
				info->bRemotelyAccessing = bRemotelyAccessing;
				contact_InteractBegin(pPlayerEnt, NULL, pContactDef, pchDialogName, pFallback);
				return;
			}
		} 

		if(!info->pQueuedContactDialog && (info->pSharedMission || info->pContactDialog)) {
			// Build queued dialog
			info->pQueuedContactDialog = StructCreate(parse_QueuedContactDialog);
			info->pQueuedContactDialog->bPartialPermissions = bPartialPermissions;
			info->pQueuedContactDialog->bRemotelyAccessing = bRemotelyAccessing;
			info->pQueuedContactDialog->pContactDialog = StructCreate(parse_ContactDialogInfo);
			SET_HANDLE_FROM_REFERENT("ContactDef", pContactDef, info->pQueuedContactDialog->pContactDialog->hContact);
			if(pchDisplayNameFallback || pCostumeFallback) {
				info->pQueuedContactDialog->pCostumeFallback = StructCreate(parse_ContactCostumeFallback);
				info->pQueuedContactDialog->pCostumeFallback->pchDisplayName = StructAllocString(pchDisplayNameFallback);
				SET_HANDLE_FROM_REFERENT("PlayerCostume", pCostumeFallback, info->pQueuedContactDialog->pCostumeFallback->hCostume);
			}
			info->pQueuedContactDialog->pContactDialog->pcDialogName = allocAddString(pchDialogName);
		}
	}
}

//Add a queued contact without a ContactDef
bool contact_addQueuedContactFromOptionData(Entity* pPlayerEnt, ContactDialogOptionData* pOptionData)
{
	InteractInfo* info = SAFE_MEMBER2(pPlayerEnt, pPlayer, pInteractInfo);
	if(info && pOptionData) 
	{
		// Check to make sure the player is interacting
		if( !info->pSharedMission && !info->pContactDialog ) {
			if(info->pQueuedContactDialog) {
				// If another dialog is on the queue and the player is not interacting, use that one
				contact_MaybeBeginQueuedContact(pPlayerEnt);
			} else {
				contact_InteractBeginWithOptionData(pPlayerEnt, pOptionData);
				return true;
			}
		} 

		if(!info->pQueuedContactDialog && (info->pSharedMission || info->pContactDialog)) {
			// Build queued dialog
			info->pQueuedContactDialog = StructCreate(parse_QueuedContactDialog);
			info->pQueuedContactDialog->pContactDialog = StructCreate(parse_ContactDialogInfo);
			info->pQueuedContactDialog->pOptionData = StructClone(parse_ContactDialogOptionData, pOptionData);
			return true;
		}
	}
	return false;
}

void contact_InteractBeginWithInitialOption(Entity* pPlayerEnt, Entity *pContactEnt, ContactDef* pContactDef, ContactDialogOptionData* pInitialOption, ContactCostumeFallback* pCostumeFallback) {
	InteractInfo* info = SAFE_MEMBER2(pPlayerEnt, pPlayer, pInteractInfo);	

	if(info && pPlayerEnt && pPlayerEnt->pPlayer && 
		(info->pContactDialog == NULL || !info->pContactDialog->bViewOnlyDialog || info->pContactDialog->bIsTeamSpokesman))
	{
		Player* pPlayer = pPlayerEnt->pPlayer;
		int i;
		bool recentContact = false;
		bool partialPermissions = info->bPartialPermissions;
		bool remotelyAccessing = info->bRemotelyAccessing;

		if (gConf.bRememberVisitedDialogs)
		{
			// Reset visited dialogs
			eaClearEx(&info->ppchVisitedSpecialDialogs, NULL);
		}

		/* MJF: Disabling queuing if not in combat, until we figure
		   out what is really needed.
		   
		if (pchDialogName && pPlayerEnt->pChar && pPlayerEnt->pChar->uiTimeCombatExit) {
			// check if this should be queued
			SpecialDialogBlock* specialDialog = contact_SpecialDialogFromName(pContactDef, pchDialogName);
			if (specialDialog && specialDialog->bDelayIfInCombat) {
				if (!info->pQueuedNotInCombatDialog) {
					info->pQueuedNotInCombatDialog = StructCreate(parse_QueuedContactDialog);
					info->pQueuedNotInCombatDialog->bRemoteContact = remoteContact;
					info->pQueuedNotInCombatDialog->pContactDialog = StructCreate(parse_ContactDialogInfo);
					SET_HANDLE_FROM_REFERENT("ContactDef", pContactDef, info->pQueuedNotInCombatDialog->pContactDialog->hContact);
					info->pQueuedNotInCombatDialog->pContactDialog->pcDialogName = allocAddString(pchDialogName);
					info->pQueuedNotInCombatDialog->pCostumeFallback = StructClone(parse_ContactCostumeFallback, pCostumeFallback);
				}
				return;
			}
		}
		*/

		contact_InteractEnd(pPlayerEnt, true);	// Kill any previous dialog
		if(pCostumeFallback) {
			info->pCostumeFallback = StructCreate(parse_ContactCostumeFallback);
			COPY_HANDLE(info->pCostumeFallback->hCostume, pCostumeFallback->hCostume);
			info->pCostumeFallback->pchDisplayName = StructAllocString(pCostumeFallback->pchDisplayName);
		}
		info->bPartialPermissions = partialPermissions; //Reset the remote contact setting
		info->bRemotelyAccessing = remotelyAccessing;

		// Remember that we talked to this contact
		if(!recentContact){
			if(eaSize(&pPlayer->eaRecentContacts) < NUM_RECENT_CONTACTS){
				eaPush(&pPlayer->eaRecentContacts, pContactDef->name);
			} else if (NUM_RECENT_CONTACTS){
				pPlayer->eaRecentContacts[pPlayer->uRecentContactsIndex] = pContactDef->name;
				if(++pPlayer->uRecentContactsIndex >= NUM_RECENT_CONTACTS)
					pPlayer->uRecentContactsIndex = 0;
			}
		}

		// If we had a waypoint leading us to this Contact, clear it
		if (pPlayerEnt->pPlayer->missionInfo && GET_REF(pPlayerEnt->pPlayer->missionInfo->hTrackedContact) == pContactDef){
			REMOVE_HANDLE(pPlayerEnt->pPlayer->missionInfo->hTrackedContact);
			waypoint_UpdateTrackedContactWaypoints(pPlayerEnt);
		}

		// Make sure contact dialogs don't exit due to external actions
		pPlayerEnt->pPlayer->InteractStatus.bInteractBreakOnDamage = false;
		pPlayerEnt->pPlayer->InteractStatus.bInteractBreakOnPower = false;
		pPlayerEnt->pPlayer->InteractStatus.bInteractBreakOnMove = false;

		// Stop any end dialog audio playing
		if (eaSize(&pContactDef->eaEndDialogAudios) > 0 && pContactEnt)
		{
			for (i = 0; i < eaSize(&pContactDef->eaEndDialogAudios); i++)
			{
				if (pContactDef->eaEndDialogAudios[i]->pchAudioName && pContactDef->eaEndDialogAudios[i]->pchAudioName[0])
				{
					ClientCmd_sndStopOneShot(pPlayerEnt, pContactDef->eaEndDialogAudios[i]->pchAudioName);					
				}
			}
		}

		// Transition to the first UI state
		contact_ChangeDialogState(pPlayerEnt, pContactEnt?entGetRef(pContactEnt):0, pContactDef, pInitialOption);
	}
}

void contact_InteractBegin(Entity* pPlayerEnt, Entity *pContactEnt, ContactDef* pContactDef, const char* pchDialogName, ContactCostumeFallback* pCostumeFallback)
{
	ContactDialogOptionData initialOption = {0};
	int i;
	bool recentContact = false;

	// Have we talked to this contact recently?
	if(pPlayerEnt && pPlayerEnt->pPlayer)
	{
		Player* pPlayer = pPlayerEnt->pPlayer;
		for(i=0; i<eaSize(&pPlayer->eaRecentContacts); i++){
			if((pPlayer->eaRecentContacts[i]) == pContactDef->name){
				recentContact = true;
				break;
			}
		}
	}

	// Set up an option for the desired state
	if (pchDialogName && pchDialogName[0]){
		SpecialDialogBlock *pSpecialDialog = contact_SpecialDialogFromName(pContactDef, pchDialogName);
		initialOption.targetState = ContactDialogState_SpecialDialog;
		initialOption.pchTargetDialogName = allocAddString(pchDialogName);
		// find the initial dialog block index
		if(pSpecialDialog && pSpecialDialog->dialogBlock) {
			for(i = 0; i < eaSize(&pSpecialDialog->dialogBlock); i++)
			{
				if(!pSpecialDialog->dialogBlock[i]->condition || contact_Evaluate(pPlayerEnt, pSpecialDialog->dialogBlock[i]->condition, NULL))
					break;
			}
			if(i < eaSize(&pSpecialDialog->dialogBlock))
			{
				initialOption.iSpecialDialogSubIndex = i;
			}
		}
	}
	else if(pPlayerEnt && (((contact_GetActiveConditionalGreetings(pPlayerEnt, pContactDef, NULL) > 0 || contact_GetNonConditionalGreetings(pContactDef, NULL) > 0) && !recentContact) || 
		contact_GetActiveMissionGreetings(pPlayerEnt, pContactDef, NULL)))
	{
		initialOption.targetState = ContactDialogState_Greeting;
	}
	else{
		initialOption.targetState = ContactDialogState_OptionList;
		initialOption.bFirstOptionsList = true;
	}

	contact_InteractBeginWithInitialOption(pPlayerEnt, pContactEnt, pContactDef, &initialOption, pCostumeFallback);

}

void contact_InteractBeginWithOptionData(Entity* pPlayerEnt, ContactDialogOptionData* pOptionData)
{
	InteractInfo* info = SAFE_MEMBER2(pPlayerEnt, pPlayer, pInteractInfo);
	if (info && pOptionData && (info->pContactDialog == NULL || !info->pContactDialog->bViewOnlyDialog || info->pContactDialog->bIsTeamSpokesman))
	{
		if (gConf.bRememberVisitedDialogs)
		{
			// Reset visited dialogs
			eaClearEx(&info->ppchVisitedSpecialDialogs, NULL);
		}

		contact_InteractEnd(pPlayerEnt, true);	// Kill any previous dialog
		
		info->bPartialPermissions = false; //Reset the remote contact setting
		info->bRemotelyAccessing = false;

		// Make sure contact dialogs don't exit due to external actions
		pPlayerEnt->pPlayer->InteractStatus.bInteractBreakOnDamage = false;
		pPlayerEnt->pPlayer->InteractStatus.bInteractBreakOnPower = false;
		pPlayerEnt->pPlayer->InteractStatus.bInteractBreakOnMove = false;

		contact_ChangeDialogState(pPlayerEnt, 0, NULL, pOptionData);

		if(info->pQueuedContactDialog)
		{
			StructDestroySafe(parse_QueuedContactDialog, &info->pQueuedContactDialog);
		}
	}
}

static void contact_PlayEndDialogAudio(Entity *pEnt, InteractInfo *pInteractInfo, ContactDef *pContactDef)
{
	if (pEnt && pInteractInfo && eaSize(&pContactDef->eaEndDialogAudios))
	{
		// Randomly choose an audio file
		EndDialogAudio *pEndDialogAudio = eaRandChoice(&pContactDef->eaEndDialogAudios);

		if (pEndDialogAudio && pEndDialogAudio->pchAudioName && pEndDialogAudio->pchAudioName[0])
		{
			// Play the end dialog audio
			EntityRef entRef = pInteractInfo->pContactDialog->headshotEnt;
			if (pInteractInfo->pContactDialog->cameraSourceEnt != 0)
				entRef = pInteractInfo->pContactDialog->cameraSourceEnt;
			ClientCmd_sndPlayRemote3dFromEntity(pEnt, pEndDialogAudio->pchAudioName, entRef, pContactDef->filename);
		}
	}
}

void contact_InteractEnd(Entity* playerEnt, bool bPersistCleanup)
{
	InteractInfo* info = SAFE_MEMBER2(playerEnt, pPlayer, pInteractInfo);
	if(info)
	{
		bool bDirty = false;
		if ( IS_HANDLE_ACTIVE(info->interactItem) )
		{
			REMOVE_HANDLE(info->interactItem);
			bDirty = true;
		}		

		if (info->pSharedMission)
		{
			mission_SharedMissionDeclined(playerEnt, info->pSharedMission);
			StructDestroySafe(parse_QueuedMissionOffer, &info->pSharedMission);
			bDirty = true;
		}

		if(info->pContactDialog)
		{
			ContactDef *pContactDef = GET_REF(info->pContactDialog->hContactDef);

			if (pContactDef)
			{
				if (playerEnt->pTeam && 
					(playerEnt->pTeam->bInTeamDialog || playerEnt->pTeam->bIsTeamSpokesman))
				{
					bDirty = true;
				}
				contact_PlayEndDialogAudio(playerEnt, info, pContactDef);
			}

			if(bPersistCleanup)
			{
				StoreDef *pStoreDef = GET_REF(info->pContactDialog->hSellStore);

				if(pStoreDef)
					store_Close(playerEnt);
			}


			contact_CleanupDialog(playerEnt);
			StructDestroy(parse_ContactDialog, info->pContactDialog);
			info->pContactDialog = NULL;
			bDirty = true;
		}

		if(info->pCostumeFallback) 
		{
			StructDestroySafe(parse_ContactCostumeFallback, &info->pCostumeFallback);
		}

		if(info->bAwaitingMapTransfer)
		{
			info->bAwaitingMapTransfer = false;
			gslTeam_AwayTeamMemberStateChangedUpdate(playerEnt);
		}

		if(info->bPartialPermissions)
		{
			info->bPartialPermissions = false;
			bDirty = true;
		}

		if(info->bRemotelyAccessing)
		{
			info->bRemotelyAccessing = false;
			bDirty = true;
		}

		if ( bDirty )
		{
			entity_SetDirtyBit(playerEnt, parse_InteractInfo, playerEnt->pPlayer->pInteractInfo, true);
			entity_SetDirtyBit(playerEnt, parse_Player, playerEnt->pPlayer, false);

			if (playerEnt->pTeam)
			{
				if (playerEnt->pTeam->bIsTeamSpokesman && playerEnt->pTeam->bInTeamDialog)
				{
					contact_EndDialogForOtherTeamMembers(playerEnt);
				}
				playerEnt->pTeam->bIsTeamSpokesman = false;
				playerEnt->pTeam->bInTeamDialog = false;
				entity_SetDirtyBit(playerEnt, parse_PlayerTeam, playerEnt->pTeam, false);
			}			
		}
	}
}

static void contact_AddMission_CB(TransactionReturnVal* pReturnVal, void* pData)
{
	Entity* pEnt = entFromContainerIDAnyPartition(GLOBALTYPE_ENTITYPLAYER, (ContainerID)(intptr_t)pData);
	
	if (pEnt && pReturnVal->eOutcome == TRANSACTION_OUTCOME_SUCCESS)
	{
		InteractInfo* pInteractInfo = SAFE_MEMBER(pEnt->pPlayer, pInteractInfo);
			
		// Refresh remote contacts
		if (pInteractInfo)
		{
			pInteractInfo->bUpdateRemoteContactsNextTick = true;
			pInteractInfo->bUpdateContactDialogOptionsNextTick = true;
		}
	}
}

static void contactdialog_AcceptMission(SA_PARAM_NN_VALID Entity* playerEnt, SA_PARAM_NN_VALID ContactDialogOptionData* pData)
{
	MissionDef* missionDef = GET_REF(pData->hRootMission);
	InteractInfo* pInteractInfo = SAFE_MEMBER2(playerEnt, pPlayer, pInteractInfo);
	MissionInfo *missionInfo = mission_GetInfoFromPlayer(playerEnt);
	MissionCreditType eAllowedCreditType;
	ContactDef* pContactDef = GET_REF(pData->hMissionListContactDef);

	if (pData->pNamespacedMissionContactData)
	{
		missioninfo_AddMissionByName(entGetPartitionIdx(playerEnt), missionInfo, REF_STRING_FROM_HANDLE(pData->hRootMission), contact_AddMission_CB, (void *)(intptr_t)entGetContainerID(playerEnt));
	}
	else if(missionInfo && missionDef && !pData->pchSubMissionName 
		&& missiondef_CanBeOfferedAtAll(playerEnt, missionDef, NULL, NULL, &eAllowedCreditType) 
		&& contactdialog_ValidateMissionOfferParams(pContactDef, eAllowedCreditType, pData->pMissionOfferParams))
	{
		int iPartitionIdx = entGetPartitionIdx(playerEnt);

		// Add mission
		missioninfo_AddMission(iPartitionIdx, missionInfo, missionDef, pData->pMissionOfferParams, contact_AddMission_CB, (void *)(intptr_t)entGetContainerID(playerEnt));

		// Clean up Shared Mission here, otherwise the Declined string will get sent back
		if (pInteractInfo && pInteractInfo->pSharedMission)
		{
			mission_SharedMissionAccepted(playerEnt, pInteractInfo->pSharedMission);
			StructDestroySafe(parse_QueuedMissionOffer, &pInteractInfo->pSharedMission);
			entity_SetDirtyBit(playerEnt, parse_InteractInfo, playerEnt->pPlayer->pInteractInfo, true);
			entity_SetDirtyBit(playerEnt, parse_Player, playerEnt->pPlayer, false);
		}
	}
}
static void contactdialog_RestartMission(SA_PARAM_NN_VALID Entity* playerEnt, SA_PARAM_NN_VALID ContactDialogOptionData* pData)
{
	MissionDef* missionDef = GET_REF(pData->hRootMission);
	MissionInfo* pMissionInfo = mission_GetInfoFromPlayer(playerEnt);
	InteractInfo* pInteractInfo = SAFE_MEMBER2(playerEnt, pPlayer, pInteractInfo);
	MissionCreditType eAllowedCreditType;
	ContactDef* pContactDef = GET_REF(pData->hMissionListContactDef);
	MissionOfferStatus eStatus;

	if(pMissionInfo && missionDef && !pData->pchSubMissionName 
		&& missiondef_CanBeOfferedAtAll(playerEnt, missionDef, NULL, &eStatus, &eAllowedCreditType))
	{
		Mission* mission = mission_FindMissionFromDef(pMissionInfo, missionDef);

		// Add or Restart mission
		if(contactdialog_ValidateMissionOfferParams(pContactDef, eAllowedCreditType, pData->pMissionOfferParams))
		{
			int iPartitionIdx = entGetPartitionIdx(playerEnt);
			if(mission && mission->state == MissionState_Failed)
				mission_RestartMissionEx(iPartitionIdx, playerEnt, pMissionInfo, mission, pData->pMissionOfferParams, contact_AddMission_CB, (void *)(intptr_t)entGetContainerID(playerEnt));
			else if (!mission) {
				missioninfo_AddMission(iPartitionIdx, pMissionInfo, missionDef, pData->pMissionOfferParams, contact_AddMission_CB, (void *)(intptr_t)entGetContainerID(playerEnt));
			}

			// Clean up Shared Mission here, otherwise the Declined string will get sent back
			if (pInteractInfo && pInteractInfo->pSharedMission)
			{
				mission_SharedMissionAccepted(playerEnt, pInteractInfo->pSharedMission);
				StructDestroySafe(parse_QueuedMissionOffer, &pInteractInfo->pSharedMission);
				entity_SetDirtyBit(playerEnt, parse_InteractInfo, playerEnt->pPlayer->pInteractInfo, true);
				entity_SetDirtyBit(playerEnt, parse_Player, playerEnt->pPlayer, false);
			}
		}
		else if(eAllowedCreditType == MissionCreditType_Ineligible && eStatus == MissionOfferStatus_MissionOnCooldown)
		{
			// If we cannot restart the mission since it is on a cooldown, just drop it.
			missioninfo_DropMission(playerEnt, pMissionInfo, mission);
		}
	}
}
static void contactdialog_TurnInMission(SA_PARAM_NN_VALID Entity* playerEnt, SA_PARAM_NN_VALID ContactDialogOptionData* pData, SA_PARAM_OP_VALID ContactRewardChoices* rewardChoices)
{
	MissionDef* missionDef = GET_REF(pData->hRootMission);
	MissionInfo* pMissionInfo = mission_GetInfoFromPlayer(playerEnt);

	if(missionDef && pMissionInfo && !pData->pchSubMissionName)
	{
		Mission* mission = mission_FindMissionFromDef(pMissionInfo, missionDef);

		if(mission){
			mission_TurnInMission(playerEnt, missionDef, rewardChoices);
		}
	}
}
static void contactdialog_CompleteSubMission(SA_PARAM_NN_VALID Entity* playerEnt, SA_PARAM_NN_VALID ContactDialogOptionData* pData)
{
	MissionDef* missionDef = GET_REF(pData->hRootMission);
	MissionInfo* pMissionInfo = mission_GetInfoFromPlayer(playerEnt);

	if (missionDef && pData->pchSubMissionName){
		missionDef = missiondef_ChildDefFromName(missionDef, pData->pchSubMissionName);
	}

	if(missionDef && pMissionInfo)
	{
		Mission* mission = mission_FindMissionFromDef(pMissionInfo, missionDef);

		if(mission && mission->state == MissionState_InProgress){
			mission_CompleteMission(playerEnt, mission, false);
		}
	}
}

static void contactdialog_SpecialDialogComplete(SA_PARAM_NN_VALID Entity* playerEnt, ContactDef *pContactDef, ContactDialogOptionData* pData)
{
	if (pContactDef && pData && pData->pchCompletedDialogName && contact_SpecialDialogFromName(pContactDef, pData->pchCompletedDialogName))
	{
		// Track the recently completed contact dialogs
		if (playerEnt && playerEnt->pPlayer && playerEnt->pPlayer->pInteractInfo) {
			ContactDialogInfo *pInfo;
			int i, iSize = eaSize(&playerEnt->pPlayer->pInteractInfo->recentlyCompletedDialogs);

			// See if the dialog was already completed
			for(i=iSize-1; i>=0; --i) {
				pInfo = playerEnt->pPlayer->pInteractInfo->recentlyCompletedDialogs[i];
				if ((pContactDef == GET_REF(pInfo->hContact)) && (pData->pchCompletedDialogName == pInfo->pcDialogName)) {
					break;
				}
			}
			if (i < 0) {
				// First time completing dialog, so add info
				pInfo = StructCreate(parse_ContactDialogInfo);
				SET_HANDLE_FROM_REFERENT(g_ContactDictionary, pContactDef, pInfo->hContact);
				pInfo->pcDialogName = pData->pchCompletedDialogName;
				eaPush(&playerEnt->pPlayer->pInteractInfo->recentlyCompletedDialogs, pInfo);
				iSize++;
			} else if (iSize > 1 && i != iSize-1) {
				// Move the recently completed dialog to the back of the list
				pInfo = eaRemove(&playerEnt->pPlayer->pInteractInfo->recentlyCompletedDialogs, i);
				eaPush(&playerEnt->pPlayer->pInteractInfo->recentlyCompletedDialogs, pInfo);
			}
			// Restrict number of dialogs tracked
			while (iSize > MAX_TRACKED_DIALOGS) {
				StructDestroy(parse_ContactDialogInfo, playerEnt->pPlayer->pInteractInfo->recentlyCompletedDialogs[0]);
				eaRemove(&playerEnt->pPlayer->pInteractInfo->recentlyCompletedDialogs, 0);
				iSize--;
			}
		}

		// Log the dialog complete event
		eventsend_RecordDialogComplete(playerEnt, pContactDef, pData->pchCompletedDialogName);
	}
}


void contactdialog_ChangeCraftingSkill(Entity* playerEnt, ContactDef* pContactDef)
{
	if (playerEnt &&
		playerEnt->pPlayer &&
		playerEnt->pPlayer->SkillType != pContactDef->skillTrainerType)
	{
		SetSkillType(playerEnt, pContactDef->skillTrainerType);
	}
}

static S32 CharacterCanContactRespec(Character *pchar)
{
	if (pchar && pchar->pEntParent->pPlayer &&
		(pchar->pEntParent->pPlayer->InteractStatus.eNearbyContactTypes & ContactFlag_Respec)) {
		return true;
	}
	return false;
}

static void contactdialog_Respec(Entity* playerEnt, ContactDef* pContactDef)
{
	if(playerEnt && playerEnt->pChar && CharacterCanContactRespec(playerEnt->pChar))
	{
		character_RespecFull(entGetPartitionIdx(playerEnt), playerEnt->pChar);
	}
}

static void contactdialog_RespecAdvantages(Entity* playerEnt, ContactDef* pContactDef)
{
	if(playerEnt && playerEnt->pChar && CharacterCanContactRespec(playerEnt->pChar))
	{
		character_RespecAdvantages(playerEnt->pChar);
	}
}

static void contactdialog_SetTrackedContact(Entity *pEnt, ContactDef *pContactDef)
{
	MissionInfo *pInfo = mission_GetInfoFromPlayer(pEnt);
	if (pInfo && pContactDef && contact_ShowInSearchResults(pContactDef)){
		SET_HANDLE_FROM_REFERENT(g_ContactDictionary, pContactDef, pInfo->hTrackedContact);
	}
	waypoint_UpdateTrackedContactWaypoints(pEnt);
}

static bool contactdialog_CanPerformSpecialDialogAction(WorldGameActionBlock *pActionBlock, bool bTeamSpokesmanOrNoTeam)
{
	if (!bTeamSpokesmanOrNoTeam)
	{
		// We must not allow warp actions to be executed by other team members
		FOR_EACH_IN_EARRAY_FORWARDS(pActionBlock->eaActions, WorldGameActionProperties, pGameActionProperties)
		{
			if (pGameActionProperties && pGameActionProperties->eActionType == WorldGameActionType_Warp)
			{
				return false;
			}
		}
		FOR_EACH_END
	}
	return true;
}

bool contactdialog_PerformSpecialDialogAction(SA_PARAM_NN_VALID Entity* playerEnt, ContactDef *pContactDef, ContactDialogOptionData *pData, bool bTeamSpokesmanOrNoTeam)
{
	SpecialDialogAction *pSpecialAction = NULL;
	SpecialDialogBlock** eaSpecialDialogs = NULL;
	bool bCriticalDialog = false;
	int i;

	if (!pData->pchCompletedDialogName){
		return true;
	}

	contact_GetSpecialDialogs(pContactDef, &eaSpecialDialogs);
	
	// Figure out which dialog action is to be performed
	for(i=eaSize(&eaSpecialDialogs)-1; i>=0; --i) {
		SpecialDialogBlock* pDialog = eaSpecialDialogs[i];
		if ((pDialog->name == pData->pchCompletedDialogName) &&
			(pData->iDialogActionIndex < contact_getNumberOfSpecialDialogActions(pContactDef, pDialog)) &&
			pData->iDialogActionIndex >= 0)
		{
			if (pDialog->eFlags & SpecialDialogFlags_CriticalDialog)
			{
				bCriticalDialog = true;
			}
			pSpecialAction = contact_getSpecialDialogActionByIndex(pContactDef, pDialog, pData->iDialogActionIndex);
			break;
		}
	}

	if(eaSpecialDialogs)
		eaDestroy(&eaSpecialDialogs);

	// Send completion event if requested
	if (pSpecialAction && pSpecialAction->bSendComplete) 
	{
		contactdialog_SpecialDialogComplete(playerEnt, pContactDef, pData);
	}

	if (pSpecialAction)
	{
		// Do the action
		if (contactdialog_CanPerformSpecialDialogAction(&pSpecialAction->actionBlock, bTeamSpokesmanOrNoTeam))
		{
			ItemChangeReason reason = {0};
			inv_FillItemChangeReason(&reason, playerEnt, "Contact:SpecialDialogAction", pContactDef->name);
			gameaction_RunActions(playerEnt, &pSpecialAction->actionBlock, &reason, NULL, NULL);
		}		

		// End the dialog if necessary
		if (pSpecialAction->bEndDialog)
		{
			interaction_EndInteractionAndDialog(entGetPartitionIdx(playerEnt), playerEnt, true, false, true);
			return false;
		}

		if (gameactionblock_CanOpenContactDialog(&pSpecialAction->actionBlock)) {
			// Don't go back into interact response because action block set up new contact!
			return false;
		}
	}

	return true;
}

bool contactdialog_PerformOptionalAction(SA_PARAM_NN_VALID Entity* playerEnt, ContactDef *pContactDef, ContactDialogOptionData *pData)
{
	GameNamedVolume *pVolume = volume_GetByName(pData->pchVolumeName, NULL);
	WorldInteractionPropertyEntry *pEntry = pVolume ? volume_GetInteractionPropEntry(pVolume, pData->iOptionalActionIndex) : NULL;

	// Do the action
	interaction_StartInteracting(playerEnt, NULL, NULL, pVolume, pData->iOptionalActionIndex, 0, 0, true);
	if (pEntry && pEntry->pActionProperties && gameactionblock_CanOpenContactDialog(&pEntry->pActionProperties->successActions)) {
		// Don't go back into interact response because action block set up new contact!
		return false;
	}

	return true;
}

static int SortTeamDialogTalliedVotes(const TeamVoteTally** ppA, const TeamVoteTally** ppB)
{
	const TeamVoteTally* pA = (*ppA);
	const TeamVoteTally* pB = (*ppB);

	if (pA->iVoteCount != pB->iVoteCount)
	{
		return pB->iVoteCount - pA->iVoteCount;
	}
	return pB->iPriority - pA->iPriority;
}

// Get the best dialog option to choose based on voting information
static const char* contact_GetBestResponseFromTeamDialogVotes(SA_PARAM_NN_VALID ContactDialog* pDialog)
{
	const char* pchBestDialogKey = NULL;
	TeamVoteTally** eaTalliedVotes = NULL;
	S32 i;

	FOR_EACH_IN_EARRAY(pDialog->eaTeamDialogVotes, TeamDialogVote, pVote)
	{
		TeamVoteTally* pTally = NULL;
		for (i = eaSize(&eaTalliedVotes)-1; i >= 0; i--)
		{
			if (stricmp(eaTalliedVotes[i]->pchDialogKey, pVote->pchDialogKey) == 0)
			{
				pTally = eaTalliedVotes[i];
				break;
			}
		}
		if (!pTally)
		{
			pTally = StructCreate(parse_TeamVoteTally);
			pTally->pchDialogKey = pVote->pchDialogKey;
			pTally->iPriority = 0;

			// Find the selected dialog option to get the sort priority
			for (i = eaSize(&pDialog->eaOptions)-1; i >= 0; i--)
			{
				if (stricmp(pDialog->eaOptions[i]->pchKey, pVote->pchDialogKey) == 0)
				{
					pTally->iPriority = eaSize(&pDialog->eaOptions) - i - 1;
					break;
				}
			}
			eaPush(&eaTalliedVotes, pTally);
		}
		pTally->iVoteCount++;
	}
	FOR_EACH_END

	if (!eaSize(&eaTalliedVotes))
	{
		ContactDialogOption* pOption = eaGet(&pDialog->eaOptions, 0);
		if (pOption)
		{
			pchBestDialogKey = pOption->pchKey;
		}
	}
	else
	{
		eaQSort(eaTalliedVotes, SortTeamDialogTalliedVotes);
		
		pchBestDialogKey = eaTalliedVotes[0]->pchDialogKey;

		eaDestroyStruct(&eaTalliedVotes, parse_TeamVoteTally);
	}
	return pchBestDialogKey;
}

static bool contact_IsInTeamDialog(Entity *pEntTeamMember)
{
	return pEntTeamMember &&
		team_IsWithTeam(pEntTeamMember) &&
		pEntTeamMember->pTeam->bInTeamDialog &&
		pEntTeamMember->pTeam->eState == TeamState_Member;
}

// Get the number of team members to notify for a team dialog
static S32 contact_GetNumTeamMembersToNotify(Entity *pEntity, ContactDef *pContactDef, bool bExcludeSelf)
{
	S32 iCount = 0;

	if (pEntity && pContactDef)
	{
		Team *pTeam = team_GetTeam(pEntity);

		if (pTeam)
		{
			int iPartitionIdx = entGetPartitionIdx(pEntity);
			FOR_EACH_IN_EARRAY_FORWARDS(pTeam->eaMembers, TeamMember, pTeamMember)
			{
				Entity *pEntTeamMember = pTeamMember ? entFromContainerID(iPartitionIdx, GLOBALTYPE_ENTITYPLAYER, pTeamMember->iEntID) : NULL;

				if (pEntTeamMember && 
					(!bExcludeSelf || pEntTeamMember->myContainerID != pEntity->myContainerID) &&
					contact_IsInTeamDialog(pEntTeamMember))
				{
					iCount++;
				}
			}
			FOR_EACH_END
		}		
	}

	return iCount;
}

// Tally a voting player's dialog option choice for a team dialog
bool contactdialog_AddTeamDialogVote(SA_PARAM_OP_VALID Entity* pEnt, ContainerID uVoterID, const char* pchDialogKey)
{
	ContactDialog* pDialog = SAFE_MEMBER3(pEnt, pPlayer, pInteractInfo, pContactDialog);
	if (pDialog)
	{
		ContactDef* pContactDef = GET_REF(pDialog->hContactDef);

		TeamDialogVote *pTeamDialogVote = NULL;
		S32 i;
		for (i = eaSize(&pDialog->eaTeamDialogVotes)-1; i >= 0; i--)
		{
			if (uVoterID == pDialog->eaTeamDialogVotes[i]->iEntID)
			{
				pTeamDialogVote = pDialog->eaTeamDialogVotes[i];
				break;
			}
		}

		if (!pTeamDialogVote)
		{
			// Add the vote
			pTeamDialogVote = StructCreate(parse_TeamDialogVote);
			pTeamDialogVote->iEntID = uVoterID;
			eaPush(&pDialog->eaTeamDialogVotes, pTeamDialogVote);
		}

		// Set voting information
		StructCopyString(&pTeamDialogVote->pchDialogKey, pchDialogKey);

		// Cache whether or not this player has voted
		if (entGetContainerID(pEnt) == uVoterID)
		{
			pDialog->bHasVoted = true;
		}

		// If all of the votes have been cast, and the server is managing the choice, make the decision now
		if (g_ContactConfig.bServerDecidesChoiceForTeamDialogs && pEnt->pTeam && pEnt->pTeam->bIsTeamSpokesman &&
			eaSize(&pDialog->eaTeamDialogVotes) == contact_GetNumTeamMembersToNotify(pEnt, pContactDef, false))
		{
			// Have the team spokesman issue the response
			const char* pchResponseKey = contact_GetBestResponseFromTeamDialogVotes(pDialog);
			contact_InteractResponseWithQueueing(pEnt, pchResponseKey, NULL, 0, true, false);
		}

		// Set dirty bits
		entity_SetDirtyBit(pEnt, parse_InteractInfo, pEnt->pPlayer->pInteractInfo, true);
		entity_SetDirtyBit(pEnt, parse_Player, pEnt->pPlayer, false);
		return true;
	}
	return false;
}

static void contact_InteractResponse_Cleanup(Entity* playerEnt, ContactDialogOption* pResponse, bool bDirty)
{
	InteractInfo* pInteractInfo = SAFE_MEMBER2(playerEnt, pPlayer, pInteractInfo);
	
	if ( pResponse )
	{
		StructDestroy(parse_ContactDialogOption, pResponse);
	}

	if ( playerEnt && bDirty )
	{
		ContactDialog *pDialog = SAFE_MEMBER3(playerEnt, pPlayer, pInteractInfo, pContactDialog);
		ContactDef *pContactDef = pDialog ? GET_REF(pDialog->hContactDef) : NULL;

		entity_SetDirtyBit(playerEnt, parse_InteractInfo, pInteractInfo, true);
		entity_SetDirtyBit(playerEnt, parse_Player, playerEnt->pPlayer, false);
	}
}

// Queues a dialog response based on the given parameters
static void contact_QueueDialogResponse(Entity* playerEnt, 
										ContactDialog* pDialog, 
										const char* pchResponseKey, 
										ContactRewardChoices* rewardChoices, 
										U32 iOptionAssistEntID, 
										bool bSelfCanChoose)
{
	if (playerEnt && pDialog)
	{
		Team *pTeam = team_GetTeam(playerEnt);

		// Create a new queue item
		DialogResponseQueueItem *pQueueItem = StructCreate(parse_DialogResponseQueueItem);
		pQueueItem->uiQueueEntryTime = timeGetTime();
		pQueueItem->iPartitionIdx = entGetPartitionIdx(playerEnt);
		pQueueItem->entRef = entGetRef(playerEnt);
		pQueueItem->pchResponseKey = pchResponseKey ? StructAllocString(pchResponseKey) : NULL;
		pQueueItem->pRewardChoices = rewardChoices ? StructClone(parse_ContactRewardChoices, rewardChoices) : NULL;
		pQueueItem->iOptionAssistEntID = iOptionAssistEntID;

		if (pDialog->bIsTeamSpokesman)
		{
			pDialog->bTeamDialogChoiceMade = true;
		}
		devassert(pTeam);
		if (pTeam)
		{
			int iPartitionIdx = entGetPartitionIdx(playerEnt);
			FOR_EACH_IN_EARRAY_FORWARDS(pTeam->eaMembers, TeamMember, pTeamMember)
			{
				Entity *pEntTeamMember = pTeamMember ? entFromContainerID(iPartitionIdx, GLOBALTYPE_ENTITYPLAYER, pTeamMember->iEntID) : NULL;
				if (contact_IsInTeamDialog(pEntTeamMember))
				{
					// Let the team members know about the dialog choice
					ClientCmd_contactUI_TeamDialogChoiceMade(pEntTeamMember, pchResponseKey, !bSelfCanChoose);
				}
			}
			FOR_EACH_END
		}

		// Push it to the queue
		eaPush(&s_eaDialogResponseQueueItems, pQueueItem);
	}
}

static bool contact_ProcessResponseAction(Entity *pEnt, ContactDialog *pDialog, ContactDef *pContactDef, ContactDialogOption *pResponse, ContactRewardChoices *pRewardChoices, bool bDirty, bool bTeamSpokesmanOrNoTeam)
{
	if (pEnt && pResponse)
	{
		switch(pResponse->pData->action)
		{
			xcase ContactActionType_AcceptMissionOffer:
				contactdialog_AcceptMission(pEnt, pResponse->pData);
			xcase ContactActionType_RestartMission:
				contactdialog_RestartMission(pEnt, pResponse->pData);
			xcase ContactActionType_ReturnMission:
				contactdialog_TurnInMission(pEnt, pResponse->pData, pRewardChoices);
			xcase ContactActionType_DialogComplete:
				contactdialog_SpecialDialogComplete(pEnt, pContactDef, pResponse->pData);
			xcase ContactActionType_CompleteSubMission:
				contactdialog_CompleteSubMission(pEnt, pResponse->pData);
			xcase ContactActionType_ContactInfo:
				eventsend_RecordContactInfoViewed(pEnt, pContactDef);
			xcase ContactActionType_ChangeCraftingSkill:
				contactdialog_ChangeCraftingSkill(pEnt, pContactDef);
			xcase ContactActionType_Respec:
				contactdialog_Respec(pEnt, pContactDef);
			xcase ContactActionType_RespecAdvantages:
				contactdialog_RespecAdvantages(pEnt, pContactDef);
			xcase ContactActionType_MissionSearchSetContactWaypoint:
				contactdialog_SetTrackedContact(pEnt, GET_REF(pResponse->pData->hMissionListContactDef));
			xcase ContactActionType_GiveLoreItem:
				{
					ItemChangeReason reason = {0};
					ItemDef *pItemDef = GET_REF(pResponse->pData->hItemDef);
					inv_FillItemChangeReason(&reason, pEnt, "Contact:GiveLoreItemAction", pItemDef ? pItemDef->pchName : NULL);
					contact_GiveLoreItem(pEnt, pItemDef, &reason);
				}
			xcase ContactActionType_PowerStoreFromItem:
				contact_CreatePowerStoreFromItem(pEnt, pDialog->iEntType, pDialog->iEntID, pDialog->iItemBagID, pDialog->iItemSlot, pDialog->iItemID, pDialog->bIsOfficerTrainer);
				if (bTeamSpokesmanOrNoTeam)
				{
					contact_InteractResponse_Cleanup(pEnt, pResponse, bDirty);
					return false;
				}
			xcase ContactActionType_GivePetFromItem:
				contactdialog_GivePetFromItem(pEnt, pDialog->iEntType, pDialog->iEntID, pDialog->iItemBagID, pDialog->iItemSlot, pDialog->iItemID);
			xcase ContactActionType_PerformAction:
				if (!contactdialog_PerformSpecialDialogAction(pEnt, pContactDef, pResponse->pData, bTeamSpokesmanOrNoTeam))
				{
					if (bTeamSpokesmanOrNoTeam)
					{
						// If action returns false, we do not change dialog state
						// The state will be changed by the game action
						contact_InteractResponse_Cleanup(pEnt, pResponse, bDirty);
						return false;
					}
				}
			xcase ContactActionType_PerformImageMenuAction:
				{
					ItemChangeReason reason = {0};
					ContactImageMenuItem* pItem = contact_ImageMenuItemFromName( pContactDef, pResponse->pData->iImageMenuItemIndex );
					if( pItem && (!pItem->requiresCondition || contact_Evaluate(pEnt, pItem->requiresCondition, NULL))) {
						inv_FillItemChangeReason(&reason, pEnt, "Contact:ImageMenuAction", pContactDef->name);
						gameaction_RunActions(pEnt, pItem->action, &reason, NULL, NULL);
					}
					else
					{
						return false;	//not a valid selection, don't do anything.
					}
				}
			xcase ContactActionType_PerformOptionalAction:
				if (!contactdialog_PerformOptionalAction(pEnt, pContactDef, pResponse->pData))
				{
					if (bTeamSpokesmanOrNoTeam)
					{
						// If action returns false, we do not change dialog state
						// The state will be changed by the game action
						contact_InteractResponse_Cleanup(pEnt, pResponse, bDirty);
						return false;
					}
				}
			xcase ContactActionType_RemoteContacts:
				ClientCmd_gclShowRemoteContacts(pEnt);
				return true;
		}
	}

	return true;
}

static bool contact_IsNextDialogCritical(SA_PARAM_NN_VALID ContactDef *pContactDef, 
										 SA_PARAM_NN_VALID ContactDialog *pDialog, 
										 SA_PARAM_NN_STR const char *pchResponseKey)
{
	if (pContactDef && pDialog && pchResponseKey && pchResponseKey[0])
	{
		ContactDialogOption *pSelectedDialogOption = NULL;		

		// Find the selected dialog option
		FOR_EACH_IN_EARRAY_FORWARDS(pDialog->eaOptions, ContactDialogOption, pDialogOption)
		{
			if (stricmp(pDialogOption->pchKey, pchResponseKey) == 0)
			{
				pSelectedDialogOption = pDialogOption;
				break;
			}
		}
		FOR_EACH_END

		if (pSelectedDialogOption && pSelectedDialogOption->pData)
		{
			if (pSelectedDialogOption->pData->pchTargetDialogName &&
				pSelectedDialogOption->pData->pchTargetDialogName[0])
			{
				ContactDef* pDialogContactDef = GET_REF(pSelectedDialogOption->pData->hTargetContactDef);
				SpecialDialogBlock *pNextSpecialDialogBlock = contact_SpecialDialogFromName(pDialogContactDef ? pDialogContactDef : pContactDef, pSelectedDialogOption->pData->pchTargetDialogName);

				if (pNextSpecialDialogBlock && (pNextSpecialDialogBlock->eFlags & SpecialDialogFlags_CriticalDialog))
				{
					return true;
				}
			}
			else if (pSelectedDialogOption->pData->pchCompletedDialogName &&
					 pSelectedDialogOption->pData->pchCompletedDialogName[0])
			{
				// Check to see if the selected special dialog will execute a game action 
				// that will start up a new critical dialog on a different contact
				SpecialDialogAction *pSpecialAction = NULL;
				SpecialDialogBlock** eaSpecialDialogs = NULL;
				int i;

				contact_GetSpecialDialogs(pContactDef, &eaSpecialDialogs);
	
				// Figure out which dialog action is to be performed
				for(i=eaSize(&eaSpecialDialogs)-1; i>=0; --i) {
					SpecialDialogBlock* pSpecialDialog = eaSpecialDialogs[i];
					if ((pSpecialDialog->name == pSelectedDialogOption->pData->pchCompletedDialogName) &&
						(pSelectedDialogOption->pData->iDialogActionIndex < eaSize(&pSpecialDialog->dialogActions)) &&
						pSelectedDialogOption->pData->iDialogActionIndex >= 0)
					{
						pSpecialAction = pSpecialDialog->dialogActions[pSelectedDialogOption->pData->iDialogActionIndex];
						break;
					}
				}

				if(eaSpecialDialogs)
					eaDestroy(&eaSpecialDialogs);

				if (pSpecialAction)
				{
					FOR_EACH_IN_EARRAY_FORWARDS(pSpecialAction->actionBlock.eaActions, WorldGameActionProperties, pGameActionProperties)
					{
						if (pGameActionProperties->eActionType == WorldGameActionType_Contact &&
							pGameActionProperties->pContactProperties)
						{
							ContactDef* pActionContactDef = GET_REF(pGameActionProperties->pContactProperties->hContactDef);
							const char* pchActionDialogName = allocFindString(pGameActionProperties->pContactProperties->pcDialogName);
							if (pActionContactDef)
							{
								bool bCriticalDialog = false;

								contact_GetSpecialDialogs(pActionContactDef, &eaSpecialDialogs);

								for(i=eaSize(&eaSpecialDialogs)-1; i>=0; --i) {
									SpecialDialogBlock* pSpecialDialog = eaSpecialDialogs[i];
									if (pSpecialDialog->name == pchActionDialogName)
									{
										if (pSpecialDialog->eFlags & SpecialDialogFlags_CriticalDialog)
										{
											bCriticalDialog = true;
										}
										break;
									}
								}

								if(eaSpecialDialogs)
									eaDestroy(&eaSpecialDialogs);
							
								if (bCriticalDialog)
								{
									return true;
								}
							}
						}
					}
					FOR_EACH_END
				}
			}
		}
	}
	return false;
}

static void contact_EndDialogForOtherTeamMembers(SA_PARAM_NN_VALID Entity* pPlayerEnt)
{
	if (pPlayerEnt)
	{
		Team *pTeam = team_GetTeam(pPlayerEnt);

		if (pTeam)
		{
			int iPartitionIdx = entGetPartitionIdx(pPlayerEnt);

			if (pPlayerEnt->pTeam->bInTeamDialog || pPlayerEnt->pTeam->bIsTeamSpokesman)
			{
				pPlayerEnt->pTeam->bIsTeamSpokesman = false;
				pPlayerEnt->pTeam->bInTeamDialog = false;
				entity_SetDirtyBit(pPlayerEnt, parse_PlayerTeam, pPlayerEnt->pTeam, false);
			}

			FOR_EACH_IN_EARRAY_FORWARDS(pTeam->eaMembers, TeamMember, pTeamMember)
			{
				if (pTeamMember && 
					pTeamMember->iEntID != pPlayerEnt->myContainerID)
				{
					Entity *pEntTeamMember = entFromContainerID(iPartitionIdx, GLOBALTYPE_ENTITYPLAYER, pTeamMember->iEntID);

					if (contact_IsInTeamDialog(pEntTeamMember))
					{
						contact_InteractEnd(pEntTeamMember, true);
					}
				}
			}
			FOR_EACH_END		
		}

	}	
}

static bool contact_CanChooseDialogOption(U32 iEntID, SA_PARAM_NN_VALID ContactDialogOption *pDialogOption)
{
	S32 iTeamMembersEligibleToSee = ea32Size(&pDialogOption->piTeamMembersEligibleToSee);
	S32 iTeamMembersEligibleToInteract = ea32Size(&pDialogOption->piTeamMembersEligibleToInteract);
	if (iEntID == 0)
	{
		return false;
	}

	if (iTeamMembersEligibleToSee > 0 || iTeamMembersEligibleToInteract > 0)
	{
		S32 i;
		bool bCanSee = false;

		// See if the assist entity can see this option
		for (i = 0; i < ea32Size(&pDialogOption->piTeamMembersEligibleToSee); i++)
		{
			if (pDialogOption->piTeamMembersEligibleToSee[i] == iEntID)
			{
				bCanSee = true;
				break;
			}
		}

		if (!bCanSee)
		{
			return false;
		}

		// See if the entity can choose this option
		for (i = 0; i < ea32Size(&pDialogOption->piTeamMembersEligibleToInteract); i++)
		{
			if (pDialogOption->piTeamMembersEligibleToInteract[i] == iEntID)
			{
				return true;
			}
		}

		return false;
	}
	else
	{
		return !pDialogOption->bCannotChoose;
	}
}

static void contact_InteractResponseWithQueueing(Entity* playerEnt, const char* pchResponseKey, ContactRewardChoices* rewardChoices, U32 iOptionAssistEntID, bool bUseQueue, bool bAutomaticallyChosenBySystem)
{
	InteractInfo* pInteractInfo = SAFE_MEMBER2(playerEnt, pPlayer, pInteractInfo);

	if(pInteractInfo && pInteractInfo->pContactDialog && (!pInteractInfo->pContactDialog->bViewOnlyDialog || pInteractInfo->pContactDialog->bIsTeamSpokesman || !team_IsWithTeam(playerEnt)))
	{
		bool bDestroyDialog = false;
		bool bDirty = false;
		ContactDialog* pDialog = pInteractInfo->pContactDialog;
		ContactDialogOption* pResponse = NULL;
		ContactDef* pContactDef = GET_REF(pDialog->hContactDef);
		SpecialDialogBlock *pSpecialDialog = pDialog && pContactDef ? contact_SpecialDialogFromName(pContactDef, pDialog->pchSpecialDialogName) : NULL;
		int i;
		bool bHasMembersToNotify = contact_GetNumTeamMembersToNotify(playerEnt, pContactDef, true) > 0;		
		S32 iPartitionIdx = entGetPartitionIdx(playerEnt);

		if (bUseQueue && pDialog->bIsTeamSpokesman && bHasMembersToNotify)
		{
			// Indicates whether this player can choose the dialog option selected
			bool bSelfCanChoose = false;
			// Find the response and make sure the player can choose it
			for (i = eaSize(&pDialog->eaOptions)-1; i >= 0; --i)
			{
				if((0 == stricmp(pDialog->eaOptions[i]->pchKey, pchResponseKey)))
				{
					bSelfCanChoose = contact_CanChooseDialogOption(playerEnt->myContainerID, pDialog->eaOptions[i]);

					// See if the player or the assisting player is allowed to choose this option
					if (!bSelfCanChoose && !contact_CanChooseDialogOption(iOptionAssistEntID, pDialog->eaOptions[i]))
					{
						return;
					}

					break;
				}
			}

			// Use queuing so other team members can see the player's response
			contact_QueueDialogResponse(playerEnt, pDialog, pchResponseKey, rewardChoices, iOptionAssistEntID, bSelfCanChoose);
		}
		else
		{			
			bool bNextDialogIsCritical = contact_IsNextDialogCritical(pContactDef, pDialog, pchResponseKey);
			const char *pchDefaultContinueText = langTranslateMessage(entGetLanguage(playerEnt), GET_REF(s_ContactMsgs.hContinueMsg));
			const char *pchSpecialDialogDefaultText = langTranslateMessage(entGetLanguage(playerEnt), GET_REF(s_ContactMsgs.hSpecialDialogDefaultMsg));

			// Find the response the player picked by the key
			for(i=eaSize(&pDialog->eaOptions)-1; i>=0; --i){
				if((0 == stricmp(pDialog->eaOptions[i]->pchKey, pchResponseKey))){

					// See if the player is allowed to choose this option
					if (!contact_CanChooseDialogOption(playerEnt->myContainerID, pDialog->eaOptions[i]) && !contact_CanChooseDialogOption(iOptionAssistEntID, pDialog->eaOptions[i]))
					{
						return;
					}

					// Remove this to be destroyed later - otherwise it may get destroyed while creating the new ContactDialog
					pResponse = eaRemove(&pDialog->eaOptions, i);
					bDirty = true;
					break;
				}
			}
			//SIP TODO: Assemble pResponse->pData for purdy mission-offer from UGCSearchAgents:
			/*if(!pResponse && strStartsWith(pchResponseKey, "UGCSearchAgent")){
				pResponse = StructCreate(parse_ContactDialogOption);
				pResponse->pData = StructCreate(parse_ContactDialogOptionData);
				pResponse->pData->targetState = ContactDialogState_ViewOfferedNamespaceMission;
			}//*/ 



			estrClear(&pDialog->pchLastResponseDisplayString);
			// Copy the last response
			if (!bAutomaticallyChosenBySystem && pResponse && 
				stricmp_safe(pchDefaultContinueText, pResponse->pchDisplayString) != 0 && stricmp_safe(pchSpecialDialogDefaultText, pResponse->pchDisplayString) != 0)
			{
				StringStripTagsPrettyPrint(pResponse->pchDisplayString, &pDialog->pchLastResponseDisplayString);				
			}

			// Wait for teammates to sync before proceeding
			if(team_IsMember(playerEnt) && pSpecialDialog && (pSpecialDialog->eFlags & SpecialDialogFlags_Synchronized) && pResponse) {
				MapState *state = mapState_FromEnt(playerEnt);

				if(!mapState_GetSyncDialogForTeam(state, team_GetTeamID(playerEnt))) {
					if(mapState_AddSyncDialog(playerEnt, pContactDef, pResponse->pData)) {
						contact_InteractResponse_Cleanup(playerEnt, pResponse, bDirty);
						contact_InteractEnd(playerEnt, true);
						return;
					}
				}
			}

			if (pResponse && iOptionAssistEntID && iOptionAssistEntID != playerEnt->myContainerID)
			{
				// Make sure the assist entity can actually make this choice
				S32 j;
				for (j = 0; j < ea32Size(&pResponse->piTeamMembersEligibleToInteract); j++)
				{
					if (pResponse->piTeamMembersEligibleToInteract[j] == iOptionAssistEntID)
					{
						Entity *pAssistEnt = entFromContainerID(iPartitionIdx, GLOBALTYPE_ENTITYPLAYER, iOptionAssistEntID);
						if (pAssistEnt)
						{
							Entity *pInteractEnt = NULL;
							WorldInteractionNode *pInteractNode = NULL;
							GameNamedVolume *pInteractVolume = NULL;

							if (IS_HANDLE_ACTIVE(playerEnt->pPlayer->InteractStatus.interactTarget.hInteractionNode)) 
							{
								pInteractNode = GET_REF(playerEnt->pPlayer->InteractStatus.interactTarget.hInteractionNode);
							} 
							else if (playerEnt->pPlayer->InteractStatus.interactTarget.entRef) 
							{
								pInteractEnt = entFromEntityRef(iPartitionIdx, playerEnt->pPlayer->InteractStatus.interactTarget.entRef);
							} 
							else if (playerEnt->pPlayer->InteractStatus.interactTarget.pcVolumeNamePooled) 
							{
								pInteractVolume = volume_GetByName(playerEnt->pPlayer->InteractStatus.interactTarget.pcVolumeNamePooled, NULL);
							}

							devassertmsg(pInteractEnt || pInteractNode || pInteractVolume, "Interact object could not be found for the teamspokesman while trying to update the team spokesman.");

							// Copy the dialog info because it will be destroyed
							bDestroyDialog = true;
							pDialog = StructClone(parse_ContactDialog, pDialog);

							interaction_EndInteraction(iPartitionIdx, playerEnt, true, false, true);
							playerEnt->pTeam->bIsTeamSpokesman = false;

							entity_SetDirtyBit(playerEnt, parse_PlayerTeam, playerEnt->pTeam, false);

							// Update the player ent
							playerEnt = pAssistEnt;

							interaction_StartInteracting(playerEnt, pInteractNode, pInteractEnt, pInteractVolume, 0, GLOBALTYPE_NONE, 0, false);

							playerEnt->pTeam->bIsTeamSpokesman = true;
							entity_SetDirtyBit(playerEnt, parse_PlayerTeam, playerEnt->pTeam, false);
						}
						break;
					}
				}
			}

			// If the response requires action to be taken, do that here.
			if(pResponse && pResponse->pData)
			{
				if (gConf.bRememberVisitedDialogs)
				{
					char *estrDialogVisitedKey = NULL;
					if(pResponse->bWasAppended){
						contact_SetVisitedDialogKey(pSpecialDialog->pchAppendName, pchResponseKey, &estrDialogVisitedKey, 0);
					} else {
						contact_SetVisitedDialogKey(pDialog->pchSpecialDialogName, pchResponseKey, &estrDialogVisitedKey, pResponse->pData->iSpecialDialogSubIndex);
					}

					if (estrDialogVisitedKey != NULL &&
						eaFindString(&pInteractInfo->ppchVisitedSpecialDialogs, estrDialogVisitedKey) < 0)
					{
						// Store the visited special dialog
						eaPush(&pInteractInfo->ppchVisitedSpecialDialogs, strdup(estrDialogVisitedKey));
					}
					estrDestroy(&estrDialogVisitedKey);
				}


				if (playerEnt && playerEnt->pTeam && playerEnt->pTeam->bIsTeamSpokesman && playerEnt->pTeam->bInTeamDialog)
				{
					// Process the response for all other team members
					Team *pTeam = team_GetTeam(playerEnt);
					devassert(pTeam);

					if (pTeam)
					{						
						FOR_EACH_IN_EARRAY_FORWARDS(pTeam->eaMembers, TeamMember, pTeamMember)
						{
							Entity *pEntTeamMember = pTeamMember ? entFromContainerID(iPartitionIdx, GLOBALTYPE_ENTITYPLAYER, pTeamMember->iEntID) : NULL;
							if (pEntTeamMember &&
								pEntTeamMember->pTeam->eState == TeamState_Member &&
								pEntTeamMember->myContainerID != playerEnt->myContainerID)
							{
								contact_ProcessResponseAction(pEntTeamMember, pDialog, pContactDef, pResponse, rewardChoices, false, false);
							}
						}
						FOR_EACH_END
					}
				}

				if (!contact_ProcessResponseAction(playerEnt, pDialog, pContactDef, pResponse, rewardChoices, bDirty, true))
				{
					if (bHasMembersToNotify && !bNextDialogIsCritical)
					{
						// End the dialog for all other team members, since the next dialog is not critical to them
						contact_EndDialogForOtherTeamMembers(playerEnt);
					}

					if (bDestroyDialog)
					{
						StructDestroy(parse_ContactDialog, pDialog);
					}
					return;
				}

				if (bHasMembersToNotify && !bNextDialogIsCritical)
				{
					// End the dialog for all other team members, since the next dialog is not critical to them
					contact_EndDialogForOtherTeamMembers(playerEnt);
				}

				// Change the contact interaction state
				if (pResponse->pData && GET_REF(pResponse->pData->hTargetContactDef) && GET_REF(pResponse->pData->hTargetContactDef) != pContactDef)
					contact_ChangeDialogState(playerEnt, 0, GET_REF(pResponse->pData->hTargetContactDef), pResponse->pData);
				else
					contact_ChangeDialogState(playerEnt, pDialog->contactEnt, pContactDef, pResponse->pData);
			}
			contact_InteractResponse_Cleanup(playerEnt, pResponse, bDirty);
		}

		if (bDestroyDialog)
		{
			StructDestroy(parse_ContactDialog, pDialog);
		}
	}
}

void contact_InteractResponse(Entity* playerEnt, const char* pchResponseKey, ContactRewardChoices* rewardChoices, U32 iOptionAssistEntID, bool bAutomaticallyChosenBySystem)
{
	contact_InteractResponseWithQueueing(playerEnt, pchResponseKey, rewardChoices, iOptionAssistEntID, true, bAutomaticallyChosenBySystem);
}

static bool contact_HasDoneCallout(ContactInfo** oldInfoList, EntityRef contactRef)
{
	int i, n = eaSize(&oldInfoList);
	for (i = 0; i < n; i++)
		if (oldInfoList[i]->entRef == contactRef)
			return oldInfoList[i]->calledOutToPlayer;
	return false;
}

#define CONTACT_INDICATOR_DIST 300
#define CONTACT_INDICATOR_DIST_SQ 90000 //300 * 300
#define CONTACT_CALLOUT_DIST_SQ 6400   //80 * 80
#define REMOTE_CONTACT_PERIODIC_UPDATE_TIME 60

void contact_ProcessPlayer(Entity* playerEnt)
{
	static Entity** nearbyEnts = NULL;
	int i, j, n;
	Vec3 playerPos, contactPos;
	InteractInfo* pInteractInfo = SAFE_MEMBER2(playerEnt, pPlayer, pInteractInfo);
	ContactInfo** oldInfoList = NULL;
	Team* pTeam;
	MapState *state;
	SyncDialog* pSyncDialog;
	bool bDirty = false;
	int iPartitionIdx;
	U32 uCurrentTime;

	if (!pInteractInfo || !playerEnt)
		return;

	// If there is no g_EncounterMasterLayer, the map is probably in the middle of reloading or something.
	// Suspend contact processing, because contacts may run expressions that depend on the map being loaded
	if (gConf.bAllowOldEncounterData && !g_EncounterMasterLayer)
		return;

	iPartitionIdx = entGetPartitionIdx(playerEnt);

	// Keep pointer to old list
	oldInfoList = pInteractInfo->nearbyContacts;

	// NULL out the old struct as we want to reset the last but save the information
	if ( pInteractInfo->nearbyContacts )
	{
		pInteractInfo->nearbyContacts = NULL;
		bDirty = true;
	}
	if (pInteractInfo->nearbyInteractCritterEnts && eaSize(&pInteractInfo->nearbyInteractCritterEnts))
	{
		eaClearStruct(&pInteractInfo->nearbyInteractCritterEnts, parse_CritterInteractInfo);
		// Any time we had any critters on the list, it will be considered dirty.  Not the most aggressive of optimizations.
		bDirty = true;
	}

	PERFINFO_AUTO_START("entGridProximityLookup", 1);
	
	// Look for contacts by finding all nearby entities 
	entGetPos(playerEnt, playerPos);
	entGridProximityLookupExEArray(iPartitionIdx, playerPos, &nearbyEnts, CONTACT_INDICATOR_DIST, 0, 0, playerEnt);
	
	PERFINFO_AUTO_STOP();


	PERFINFO_AUTO_START("ScanLocalContacts", 1);

	// TODO: this sends all nearby contacts, even those with no missions for the player.
	// Might want to revisit this at some point; may be possible to optimize it
	n = eaSize(&nearbyEnts);
	for (i = 0; i < n; i++)
	{
		Entity* ent = nearbyEnts[i];
		if (ent && ent->pCritter && ent->pCritter->bIsInteractable)
		{
			ContactDef **eaContactDefs = NULL;

			interaction_GetCritterContacts(playerEnt, ent, false, &eaContactDefs, &pInteractInfo->nearbyInteractCritterEnts);

			if (eaSize(&pInteractInfo->nearbyInteractCritterEnts))
			{
				CritterInteractInfo *pCritterInfo = eaIndexedGetUsingInt(&pInteractInfo->nearbyInteractCritterEnts, ent->myRef);
				if (pCritterInfo)
				{
					ContactDef *pActionContactDef = GET_REF(pCritterInfo->hActionContactDef); 

					if (pActionContactDef)
					{
						pCritterInfo->currIndicator = contact_GetIndicatorState(pActionContactDef, playerEnt, NULL);
					}
					else
					{
						pCritterInfo->currIndicator = ContactIndicator_NoInfo;
					}
					bDirty = true;
				}
			}

			for(j=eaSize(&eaContactDefs)-1; j>=0; --j)
			{
				ContactDef *contact = eaContactDefs[j];
				int nextOfferLevel = -1;
				bool hasCompletedMission = false;
				ContactIndicator whichIndicator;
				EntityRef contactRef = entGetRef(ent);
				ContactInfo* contactInfo = StructCreate(parse_ContactInfo);
				OldEncounter *pOldEncounter = SAFE_MEMBER2(ent, pCritter, encounterData.parentEncounter);
				OldStaticEncounter *pStaticEnc = pOldEncounter?GET_REF(pOldEncounter->staticEnc):NULL;
				GameEncounter *pEncounter = SAFE_MEMBER2(ent, pCritter, encounterData.pGameEncounter);

				whichIndicator = contact_GetIndicatorState(contact, playerEnt, &nextOfferLevel);
				contactInfo->entRef = contactRef;

				contactInfo->pchContactDef = contact->name;
				if (pEncounter) {
					contactInfo->pchStaticEncName = pEncounter->pcName;
				} else if (pStaticEnc && gConf.bAllowOldEncounterData) {
					contactInfo->pchStaticEncName = pStaticEnc->name;
				} else {
				    contactInfo->pchStaticEncName = NULL;
				}

				contactInfo->currIndicator = whichIndicator;
				contactInfo->eFlags = contact->eContactFlags;
				contactInfo->nextOfferLevel = nextOfferLevel;
				contactInfo->calledOutToPlayer = contact_HasDoneCallout(oldInfoList, contactRef);
				contactInfo->bAllowSwitchToLastActivePuppet = contact->bAllowSwitchToLastActivePuppet;
				eaiCopy(&contactInfo->peAllowedClassCategories, &contact->peAllowedClassCategories);
				contact_GetAvailableSpecialDialogs(contact, playerEnt, &contactInfo->availableSpecialDialogs);

				eaPush(&pInteractInfo->nearbyContacts, contactInfo);
				bDirty = true;

				// Now do the callout text if they are in range and have a mission
				if (whichIndicator != ContactIndicator_NoInfo && !contactInfo->calledOutToPlayer)
				{
					PERFINFO_AUTO_START("Callout", 1);

					entGetPos(ent, contactPos);
					if (distance3Squared(playerPos, contactPos) < CONTACT_CALLOUT_DIST_SQ)
					{
						DialogBlock* dialogToUse = NULL;
						
						// TODO - This still isn't quite right.  The contact should use their Mission Callout even if 
						// the Indicator is Completed, as long as the contact is also offering a new mission.
						if (contact_IndicatorStateIsAvailable(whichIndicator))
							dialogToUse = eaRandChoice(&contact->missionCallout);
						else if (whichIndicator != 0)
							dialogToUse = eaRandChoice(&contact->generalCallout);

						if (dialogToUse)
						{
							ChatUserInfo *pInfo = ServerChat_CreateLocalizedUserInfoFromEnt(ent, playerEnt);
							char* displayText = NULL;
							WorldVariable **eaMapVars = NULL;
							estrStackCreate(&displayText);
							mapvariable_GetAllAsWorldVarsNoCopy(iPartitionIdx, &eaMapVars);
							langFormatGameMessage(entGetLanguage(playerEnt), &displayText, GET_REF(dialogToUse->displayTextMesg.hMessage), STRFMT_ENTITY_KEY("Entity", playerEnt), STRFMT_MAPVARS(eaMapVars), STRFMT_END);
							if (displayText)
								ClientCmd_MissionCritterSpeak(playerEnt, pInfo, displayText);
							if (dialogToUse->audioName)
								ClientCmd_sndPlayRemote3dV2(playerEnt, dialogToUse->audioName, contactPos[0], contactPos[1], contactPos[2], "Contact code", -1);
							estrDestroy(&displayText);
							eaDestroy(&eaMapVars);
							StructDestroy(parse_ChatUserInfo, pInfo);
						}
						contactInfo->calledOutToPlayer = true;
					}
					PERFINFO_AUTO_STOP();
				}
			}
			eaDestroy(&eaContactDefs);
		}
	}
	eaDestroyStruct(&oldInfoList, parse_ContactInfo);

	PERFINFO_AUTO_STOP(); // ScanLocalContacts


	PERFINFO_AUTO_START("RemoteContactScan", 1);

	uCurrentTime = timeSecondsSince2000();
	if(pInteractInfo->bUpdateRemoteContactsNextTick || uCurrentTime >= pInteractInfo->uNextRemoteContactUpdateTime) {
		// Update the player's remote contact list
		contact_entRefreshRemoteContactList(playerEnt);
		pInteractInfo->uNextRemoteContactUpdateTime = uCurrentTime + REMOTE_CONTACT_PERIODIC_UPDATE_TIME;
		pInteractInfo->bUpdateRemoteContactsNextTick = false;
		bDirty = true;
	}

	PERFINFO_AUTO_STOP(); // RemoteContactScan


	PERFINFO_AUTO_START("SyncDialogScan", 1);

	// Check sync dialog associated with this player to see if it should be destroyed.
	pTeam = team_IsMember(playerEnt) ? team_GetTeam(playerEnt) : NULL;
	state = mapState_FromPartitionIdx(iPartitionIdx);
	pSyncDialog = pTeam ? mapState_GetSyncDialogForTeam(state, team_GetTeamID(playerEnt)) : NULL;
	if(pSyncDialog && pInteractInfo) {
		EntityRef entRef = entGetRef(playerEnt);
		bool bDestroy = true;
		if(pSyncDialog->uiExpireTime > uCurrentTime)
		{
			NOCONST(ChatConfig) *pConfig = ServerChatConfig_GetChatConfig(playerEnt);
			for(i = eaSize(&pSyncDialog->eaMembers)-1; i >=0; i--) {
				Entity* pTeammate = entFromEntityRef(iPartitionIdx, pSyncDialog->eaMembers[i]->entRef);
				if(pSyncDialog->eaMembers[i]->entRef == entRef && (!pInteractInfo->pContactDialog || (pConfig && (pConfig->status & USERSTATUS_AFK || pConfig->status & USERSTATUS_AUTOAFK))) ) {
					pSyncDialog->eaMembers[i]->bAwaitingResponse = false;
				}
				if(!pTeammate || !team_IsMember(pTeammate) || pTeam != team_GetTeam(pTeammate)) {
					pSyncDialog->eaMembers[i]->bAwaitingResponse = false;
				}
				bDestroy &= !pSyncDialog->eaMembers[i]->bAwaitingResponse;
			}
		}
		if(bDestroy)
			mapState_RemoveSyncDialog(iPartitionIdx, pSyncDialog);
	}

	PERFINFO_AUTO_STOP(); // SyncDialogScan

	if ( bDirty )
	{
		entity_SetDirtyBit(playerEnt, parse_InteractInfo, pInteractInfo, true);
		entity_SetDirtyBit(playerEnt, parse_Player, playerEnt->pPlayer, false);
	}
}

static bool contact_TeamSpokesmanNoLongerExistsCheck(SA_PARAM_NN_VALID Entity* playerEnt, SA_PARAM_NN_VALID ContactDialog *pContactDialog)
{
	if (pContactDialog->bViewOnlyDialog && pContactDialog->iTeamSpokesmanID)
	{
		Entity *pTeamSpokesmanEnt = entFromContainerIDAnyPartition(GLOBALTYPE_ENTITYPLAYER, pContactDialog->iTeamSpokesmanID);
		ContactDialog *pTeamSpokesmanContactDialog = SAFE_MEMBER3(pTeamSpokesmanEnt, pPlayer, pInteractInfo, pContactDialog);

		if (pTeamSpokesmanEnt == NULL || pTeamSpokesmanContactDialog == NULL || !pTeamSpokesmanContactDialog->bIsTeamSpokesman)
		{
			// Team spokesman is no longer in the server. We need to end the dialog for this player so he does not get stuck
			contact_InteractEnd(playerEnt, true);

			return true;
		}
	}

	return false;
}

void contact_ProcessDialogsForPlayer(Entity* playerEnt)
{
	InteractInfo *pInteractInfo;
	ContactDialog *pContactDialog;
	ContactIndicator whichIndicator;
	bool bInteractable = true;
	bool bDirty = false;
	int i;
	U32 uCurrentTime;

	if (!playerEnt || !playerEnt->pPlayer || !playerEnt->pPlayer->pInteractInfo) {
		return;
	}

	pInteractInfo = playerEnt->pPlayer->pInteractInfo;
	pContactDialog = pInteractInfo->pContactDialog;

	// Make sure players don't get stuck in team dialogs if the team spokesman leaves
	if (!pContactDialog || contact_TeamSpokesmanNoLongerExistsCheck(playerEnt, pContactDialog)) {
		return;
	}

	if (!pContactDialog->bViewOnlyDialog || pContactDialog->bIsTeamSpokesman)
	{
		ContactDef *pContactDef;
		int iPartitionIdx;
		Entity *pContactEnt;

		PERFINFO_AUTO_START("NormalDialog",1);

		pContactDef = GET_REF(pContactDialog->hContactDef);
		iPartitionIdx = entGetPartitionIdx(playerEnt);
		pContactEnt = entFromEntityRef(iPartitionIdx, pContactDialog->contactEnt);

		// Check whether the target is still interactable
		if (pContactEnt) {
			ContactDef **eaContactDefs = NULL;

			interaction_GetCritterContacts(playerEnt, pContactEnt, !pContactDialog->bIsTeamSpokesman, &eaContactDefs, NULL);

			for(i=eaSize(&eaContactDefs)-1; i>=0; --i) {
				if (eaContactDefs[i] == pContactDef) {
					break;
				}
			}
			eaDestroy(&eaContactDefs);

			if (i < 0) {
				// Contact in use was no longer available
				interaction_EndInteractionAndDialog(iPartitionIdx, playerEnt, false, true, true);

				PERFINFO_AUTO_STOP(); // NormalDialog
				return;
			}
		}

		// Update Contact Indicator for this Contact
		if (pContactDef){
			whichIndicator = contact_GetIndicatorState(pContactDef, playerEnt, NULL);
			for (i = 0; i < eaSize(&pInteractInfo->nearbyContacts); i++){
				if (pInteractInfo->nearbyContacts[i]->pchContactDef == pContactDef->name)
				{
					pInteractInfo->nearbyContacts[i]->currIndicator = whichIndicator;

					bDirty = true;
				}
			}

			if (pContactDef->bUpdateOptionsEveryTick){
				pInteractInfo->bUpdateContactDialogOptionsNextTick = true;
			}
		}

		// Refresh the contact interaction state
		uCurrentTime = timeSecondsSince2000();
		if(pInteractInfo->bUpdateContactDialogOptionsNextTick || uCurrentTime >= pInteractInfo->uNextContactDialogOptionsUpdateTime) {
			// Refresh the contact interaction state
			contact_RefreshDialogState(playerEnt);
			pInteractInfo->uNextContactDialogOptionsUpdateTime = uCurrentTime + CONTACT_DIALOG_OPTIONS_PERIODIC_UPDATE_TIME;
			pInteractInfo->bUpdateContactDialogOptionsNextTick = false;
			bDirty = true;
		}

		PERFINFO_AUTO_STOP(); // NormalDialog
	}

	if (pContactDialog->bViewOnlyDialog && 
		pContactDialog->bIsTeamSpokesman &&
		!pContactDialog->bTeamDialogChoiceMade &&
		pContactDialog->iParticipatingTeamMemberCount)
	{
		U32 uTimeout;

		PERFINFO_AUTO_START("ViewOnlyDialog", 1);
		
		uTimeout = g_ContactConfig.uTeamDialogResponseTimeout + 1; // Add a second to account for latency
		
		// If the voting period has expired, make a choice based on the current votes
		if (timeSecondsSince2000() - pContactDialog->uTeamDialogStartTime >= uTimeout)
		{
			const char* pchResponseKey = contact_GetBestResponseFromTeamDialogVotes(pContactDialog);
			contact_InteractResponseWithQueueing(playerEnt, pchResponseKey, NULL, 0, true, false);
		}

		PERFINFO_AUTO_STOP(); // ViewOnlyDialog
	}

	// Update persisted store items
	if (GET_REF(pContactDialog->hPersistedStore))
	{
		bDirty |= gslPersistedStore_UpdateItemInfo(playerEnt, &pContactDialog->eaStoreItems);
	}

	// Update sellable items
	if (GET_REF(pContactDialog->hSellStore) && !pContactDialog->bSellInfoUpdated)
	{
		GameAccountDataExtract *pExtract;
		StoreDef* pSellStore;

		PERFINFO_AUTO_START("SellStore", 1);

		pExtract = entity_GetCachedGameAccountDataExtract(playerEnt);
		pSellStore = GET_REF(pContactDialog->hSellStore);

		if (store_UpdateSellItemInfo(playerEnt, pSellStore, &pContactDialog->eaSellableItemInfo, pExtract))
		{
			pContactDialog->bSellInfoUpdated = true;
			bDirty = true;
		}

		PERFINFO_AUTO_STOP();
	}

	if (bDirty)
	{
		entity_SetDirtyBit(playerEnt, parse_InteractInfo, pInteractInfo, true);
		entity_SetDirtyBit(playerEnt, parse_Player, playerEnt->pPlayer, false);
	}
}


void contact_OncePerFrame(F32 fTimeStep)
{
	Entity *pCurrEnt;
	U32 uWhichPlayer = 0;
	U32 uCurrContactIndicatorTickModded = s_ContactTick % CONTACT_INDICATOR_SYSTEM_TICK;
	U32 uCurrContactDialogTickModded = s_ContactTick % CONTACT_DIALOG_SYSTEM_TICK;
	EntityIterator *pIter;

	// Process all players
	// Don't process ENTITYFLAG_IGNORE ents here, just to save time; they're probably still loading the map
	pIter = entGetIteratorSingleTypeAllPartitions(0, ENTITYFLAG_IGNORE, GLOBALTYPE_ENTITYPLAYER);
	while ((pCurrEnt = EntityIteratorGetNext(pIter))) {
		if (uWhichPlayer % CONTACT_INDICATOR_SYSTEM_TICK == uCurrContactIndicatorTickModded) {
			PERFINFO_AUTO_START("ContactIndicatorTick", 1);
			contact_ProcessPlayer(pCurrEnt);
			PERFINFO_AUTO_STOP();
		}
		if (uWhichPlayer % CONTACT_DIALOG_SYSTEM_TICK == uCurrContactDialogTickModded) {
			PERFINFO_AUTO_START("ContactDialogTick", 1);
			contact_ProcessDialogsForPlayer(pCurrEnt);
			PERFINFO_AUTO_STOP();
		}
		uWhichPlayer++;
	}
	EntityIteratorRelease(pIter);
	
	s_ContactTick++;

	PERFINFO_AUTO_START("ContactResponseQueue", 1);

	// Process all items in the queue
	FOR_EACH_IN_EARRAY(s_eaDialogResponseQueueItems, DialogResponseQueueItem, pQueueItem) {
		if (pQueueItem) {
			Entity *pEnt = entFromEntityRef(pQueueItem->iPartitionIdx, pQueueItem->entRef);
			U32 uQueueTime = (U32)(g_ContactConfig.fTeamDialogResponseQueueTime * 1000);

			if (pEnt && 
				!pQueueItem->bNotifiedTeamMembersToClearDialogChoice && 
				timeGetTime() - pQueueItem->uiQueueEntryTime >= uQueueTime)
			{
				ContactDialog *pDialog = SAFE_MEMBER3(pEnt, pPlayer, pInteractInfo, pContactDialog);
				ContactDef *pContactDef = pDialog ? GET_REF(pDialog->hContactDef) : NULL;

				if (pContactDef && contact_GetNumTeamMembersToNotify(pEnt, pContactDef, true) > 0) {
					Team *pTeam = team_GetTeam(pEnt);
					int iPartitionIdx = entGetPartitionIdx(pEnt);
					devassert(pTeam);

					// Tell all members to reset the current dialog choice
					if (pTeam) {
						FOR_EACH_IN_EARRAY_FORWARDS(pTeam->eaMembers, TeamMember, pTeamMember) {
							Entity *pEntTeamMember = pTeamMember ? entFromContainerID(iPartitionIdx, GLOBALTYPE_ENTITYPLAYER, pTeamMember->iEntID) : NULL;

							if (contact_IsInTeamDialog(pEntTeamMember)) {
								ClientCmd_contactUI_TeamDialogClearChoice(pEntTeamMember);
							}
						} FOR_EACH_END
					}
				}
	
				pQueueItem->bNotifiedTeamMembersToClearDialogChoice = true;
			}
			if (timeGetTime() - pQueueItem->uiQueueEntryTime >= TEAM_DIALOG_RESPONSE_DELAY)	{
				// Time to initiate the actual response
				if (pEnt) {
					contact_InteractResponseWithQueueing(pEnt, pQueueItem->pchResponseKey, pQueueItem->pRewardChoices, pQueueItem->iOptionAssistEntID, false, false);
				}				
				StructDestroy(parse_DialogResponseQueueItem, pQueueItem);
				eaRemove(&s_eaDialogResponseQueueItems, FOR_EACH_IDX(s_eaDialogResponseQueueItems, pQueueItem));
			}
		} else {
			eaRemove(&s_eaDialogResponseQueueItems, FOR_EACH_IDX(s_eaDialogResponseQueueItems, pQueueItem));
		}
	} FOR_EACH_END

	PERFINFO_AUTO_STOP();

	// Normally only runs during editing
	if (gValidateContactsOnNextTick) {
		PERFINFO_AUTO_START("ContactValidate", 1);

		contact_ValidateAll();
		interactable_ValidateOverrides();
		gValidateContactsOnNextTick = false;

		PERFINFO_AUTO_STOP();
	}

}


void contact_PlayerInventoryChanged(Entity* pEnt)
{
	ContactDialog* pDialog = SAFE_MEMBER3(pEnt, pPlayer, pInteractInfo, pContactDialog);

	// If the player has a sell store contact, flag as needing updating
	if (pDialog && GET_REF(pDialog->hSellStore))
	{
		pDialog->bSellInfoUpdated = false;	
	}
}


void contactsystem_MapValidate(void)
{
	// Setup the contact location data
	eaDestroyStruct(&g_ContactLocations, parse_ContactLocation);

	encounter_GetContactLocations(&g_ContactLocations);

	if (gConf.bAllowOldEncounterData)
		oldencounter_GetContactLocations(&g_ContactLocations);
}


static void contact_ClearSpecialDialogRandomData(void)
{
	eaDestroyStruct(&s_eaSpecialDialogRandomData, parse_SpecialDialogBlockRandomData);
}


void contactsystem_MapLoad(void)
{
	contact_ClearSpecialDialogRandomData();
	eaClearStruct(&s_eaNamespacedContactOverrides, parse_ContactOverrideData);

	if (contactsystem_IsLoaded() && interactable_AreInteractablesLoaded() && volume_AreVolumesLoaded() &&
		!eaSize(&g_eaMissionsWaitingForOverrideProcessing))
	{
		mission_ApplyAllNamespacedContactOverrides();
	}

}


void contactsystem_MapUnload(void)
{
	contact_ClearSpecialDialogRandomData();
	eaClearStruct(&s_eaNamespacedContactOverrides, parse_ContactOverrideData);
}


void contact_DebugMenu(Entity* playerEnt, DebugMenuItem* groupRoot)
{
	int i, n;

	// Add all contacts that you could interact with
	DictionaryEArrayStruct *pStruct = resDictGetEArrayStruct(g_ContactDictionary);
	n = eaSize(&pStruct->ppReferents);
	for (i = 0; i < n; i++)
	{
		char cmdStr[1024];
		ContactDef* currDef = pStruct->ppReferents[i];
		sprintf(cmdStr, "contactdialog %s", currDef->name);
		debugmenu_AddNewCommand(groupRoot, currDef->name, cmdStr);
	}
}


bool contact_IsPlayerNearNemesisContact(Entity *pEnt)
{
	if (pEnt->pPlayer && (pEnt->pPlayer->InteractStatus.eNearbyContactTypes & ContactFlag_Nemesis)) {
		return true;
	}
	return false;
}

/////////////////////////////////////
// Remote Contacts
/////////////////////////////////////


// Adds a new remote contact to the server's remote contact list.
void contact_AddRemoteContactToServerList(ContactDef* pContact, ContactFlags eRemoteFlags) 
{
	RemoteContact* pRContact;
	int i, iVersion = 0;

	if(!pContact || !eRemoteFlags)
		return;

	if(s_eaRemoteContactList) {
		i = eaIndexedFindUsingString(&s_eaRemoteContactList, pContact->name);
		if(i >= 0) {
			iVersion = s_eaRemoteContactList[i]->iVersion;
			StructDestroy(parse_RemoteContact, s_eaRemoteContactList[i]);
			eaRemove(&s_eaRemoteContactList, i);
		}
	} else {
		eaCreate(&s_eaRemoteContactList);
		eaIndexedEnable(&s_eaRemoteContactList, parse_RemoteContact);
	}

	pRContact = StructCreate(parse_RemoteContact);
	pRContact->pchContactDef = allocAddString(pContact->name);
	COPY_HANDLE(pRContact->hDisplayNameMsg, pContact->displayNameMsg.hMessage);
	pRContact->eFlags = eRemoteFlags;
	//Return > Grant > SpecDialog
	pRContact->iPriority = MAX(MAX(pRContact->eFlags & ContactFlag_RemoteOfferReturn, pRContact->eFlags & ContactFlag_RemoteOfferGrant), pRContact->eFlags & ContactFlag_RemoteSpecDialog);
	pRContact->iVersion = iVersion+1;
	eaIndexedAdd(&s_eaRemoteContactList, pRContact);
}

static void contact_AddRemoteContact(Entity *pEnt,
									 InteractInfo* pInteract, 
									 const RemoteContact* pContact, 
									 S32 eFlags, 
									 S32 iPriority, 
									 ContactDialogOption*** peaOptions)
{
	RemoteContact* pPlayerContact = NULL;
	const char *pchName;
	int iSize;
	int i;

	PERFINFO_AUTO_START_FUNC();
	
	i = eaIndexedFindUsingString(&pInteract->eaRemoteContacts, pContact->pchContactDef);
	
	if (i < 0)
	{
		pPlayerContact = StructClone(parse_RemoteContact, pContact);
		if (!pInteract->eaRemoteContacts)
			eaIndexedEnable(&pInteract->eaRemoteContacts, parse_RemoteContact);
		eaPush(&pInteract->eaRemoteContacts, pPlayerContact);
	}
	else
	{

		pPlayerContact = pInteract->eaRemoteContacts[i];

		if (pContact->iVersion != pPlayerContact->iVersion)
		{
			COPY_HANDLE(pPlayerContact->hDisplayNameMsg, pContact->hDisplayNameMsg);
		}

		pchName = entTranslateMessageRef(pEnt, pPlayerContact->hDisplayNameMsg);
		if (pEnt && pchName && strchr(pchName, '{'))
		{
			estrClear(&pPlayerContact->estrFormattedContactName);
			entFormatGameString(pEnt, &pPlayerContact->estrFormattedContactName, pchName, STRFMT_PLAYER(pEnt));
		}
		else if (pPlayerContact->estrFormattedContactName)
		{
			estrDestroy(&pPlayerContact->estrFormattedContactName);
		}
	}

	pPlayerContact->eFlags = eFlags;
	pPlayerContact->iPriority = iPriority;
	
	iSize = eaSize(&pPlayerContact->eaOptions);
	for (i = 0; i < iSize; i++)
	{
		pPlayerContact->eaOptions[i]->bDirty = false;
	}
	iSize = eaSize(peaOptions);
	for (i = 0; i < iSize; i++)
	{
		ContactDialogOption* pOption = (*peaOptions)[i];
		RemoteContactOption* pRemoteOption = eaIndexedGetUsingString(&pPlayerContact->eaOptions, pOption->pchKey);
		if (!pRemoteOption)
		{
			pRemoteOption = StructCreate(parse_RemoteContactOption);
			pRemoteOption->pchKey = StructAllocString(pOption->pchKey);

			if (!pPlayerContact->eaOptions)
				eaIndexedEnable(&pPlayerContact->eaOptions, parse_RemoteContactOption);
			
			eaPush(&pPlayerContact->eaOptions, pRemoteOption);
		}
		else if ((pRemoteOption->pOption->pData && !pOption->pData) ||
				 (!pRemoteOption->pOption->pData && pOption->pData) ||
				 (pRemoteOption->pOption->pData && pOption->pData &&
				  pRemoteOption->pOption->pData->targetState != pOption->pData->targetState))
		{
			StructFreeStringSafe(&pRemoteOption->pchDescription1);
			StructFreeStringSafe(&pRemoteOption->pchDescription2);
		}
		
		pRemoteOption->bDirty = true;
		if (pRemoteOption->pOption)
			StructDestroy(parse_ContactDialogOption, pRemoteOption->pOption);
		pRemoteOption->pOption = pOption;
		
		if (pOption->pData && GET_REF(pOption->pData->hRootMission))
		{
			MissionDef *pDef = GET_REF(pOption->pData->hRootMission);
			pRemoteOption->pcMissionName = pDef->name;
			COPY_HANDLE(pRemoteOption->hMissionDisplayName, pDef->displayNameMsg.hMessage);
			COPY_HANDLE(pRemoteOption->hMissionCategory, pDef->hCategory);
		}
		else
		{
			REMOVE_HANDLE(pRemoteOption->hMissionDisplayName);
			REMOVE_HANDLE(pRemoteOption->hMissionCategory);
			pRemoteOption->pcMissionName = NULL;
		}
	}
	for (i = eaSize(&pPlayerContact->eaOptions)-1; i >= 0; i--)
	{
		RemoteContactOption* pRemoteOption = pPlayerContact->eaOptions[i];
		if (!pRemoteOption->bDirty)
		{
			eaRemove(&pPlayerContact->eaOptions, i);
			StructDestroy(parse_RemoteContactOption, pRemoteOption);
		}
	}

	PERFINFO_AUTO_STOP();
}

static void contact_RemoveRemoteContact(InteractInfo* pInteract, const RemoteContact* pContact)
{
	int i = eaIndexedFindUsingString(&pInteract->eaRemoteContacts, pContact->pchContactDef);
	if (i >= 0)
	{
		StructDestroy(parse_RemoteContact, eaRemove(&pInteract->eaRemoteContacts, i));
	}
}

ContactDialogOption *contact_GetMissionDialogOption(Entity *pEnt, ContactDef *pContact, MissionDef *pMission, bool bCanAccessFullContactRemotely, bool bRemotelyAccessing)
{
	ContactDialogOption** eaOptions = NULL;
	ContactDialogOption* pMissionOption = NULL;
	int i;

	if (!pEnt || !pContact || !pMission)
		return NULL;

	contactdialog_CreateOptionsList(pEnt, pContact, &eaOptions, !bCanAccessFullContactRemotely);

	for (i = 0; i < eaSize(&eaOptions); i++)
	{
		if (GET_REF(eaOptions[i]->pData->hRootMission) == pMission)
		{
			pMissionOption = eaRemove(&eaOptions, i);
			break;
		}
	}

	eaDestroyStruct(&eaOptions, parse_ContactDialogOption);
	return pMissionOption;
}

// Copies the server's remote contact list to the entity's remote contact list
void contact_entRefreshRemoteContactList(SA_PARAM_NN_VALID Entity* pEnt) 
{
	int i, j;
	InteractInfo *pInteract;
	MissionInfo *pInfo;

	if(!pEnt || !pEnt->pPlayer || !pEnt->pPlayer->pInteractInfo)
		return;

	PERFINFO_AUTO_START_FUNC();

	pInteract = pEnt->pPlayer->pInteractInfo;
	pInfo = mission_GetInfoFromPlayer(pEnt);

	for(i = eaSize(&s_eaRemoteContactList)-1; i >= 0; i--) {
		const RemoteContact* pRContact = s_eaRemoteContactList[i];
		ContactDef* pContact = pRContact ? contact_DefFromName(pRContact->pchContactDef) : NULL;
		//Check for NULL ptrs
		if (!pContact || pContact->bHideFromRemoteContactList) 
		{
			//ContactDef is NULL, remove it from the list
			contact_RemoveRemoteContact(pInteract, pRContact);
			continue;
		} 
		else
		{
			bool missionFound = false;
			bool bInfoFound = false;
			bool bCanAccessFullContactRemotely = false;
			ContactDialogOption** eaOptions = NULL;
			S32 eFlags;
			S32 iPriority;

			if (pContact->canAccessRemotely) {
				PERFINFO_AUTO_START("Check CanAccessRemotely Expression", 1);
				bCanAccessFullContactRemotely= contact_Evaluate(pEnt, pContact->canAccessRemotely, NULL);
				PERFINFO_AUTO_STOP();
			}
			bCanAccessFullContactRemotely = bCanAccessFullContactRemotely
				|| (g_ContactConfig.bIncludeMissionSearchResultContactsInRemoteContacts && contact_ShowInSearchResults(pContact));

			if (!contact_CanInteract(pContact, pEnt)) {
				contact_RemoveRemoteContact(pInteract, pRContact);
				continue;
			}

			PERFINFO_AUTO_START("Check Options", 1);

			//Clear the flags to make sure that the remote offers are valid for this entity
			eFlags = pRContact->eFlags & ~ContactFlag_RemoteOfferGrant & ~ContactFlag_RemoteOfferReturn & ~ContactFlag_RemoteOfferInProgress;

			contactdialog_CreateOptionsList(pEnt, pContact, &eaOptions, !bCanAccessFullContactRemotely);

			for (j=0; j < eaSize(&eaOptions); j++) 
			{
				switch(eaOptions[j]->eType)
				{
					case ContactIndicator_MissionAvailable:
						eFlags |= ContactFlag_RemoteOfferGrant;
						break;
					case ContactIndicator_MissionInProgress:
					case ContactIndicator_MissionRepeatableAvailable:
						eFlags |= ContactFlag_RemoteOfferInProgress;
						break;
					case ContactIndicator_MissionCompleted:
					case ContactIndicator_MissionCompletedRepeatable:
						eFlags |= ContactFlag_RemoteOfferReturn;
						break;
					default:
						bInfoFound = true;
						break;
				}
			}

 			missionFound = !!(eFlags & (ContactFlag_RemoteOfferGrant | ContactFlag_RemoteOfferReturn | ContactFlag_RemoteOfferInProgress));

			// Does bCanAccessFullContactRemotely == true?
			if (!bCanAccessFullContactRemotely) {
				// If not, is there a remote mission?
				if(!missionFound) {
					// If no remote mission was found, then remove the contact
					contact_RemoveRemoteContact(pInteract, pRContact);
					eaDestroyStruct(&eaOptions, parse_ContactDialogOption);
					PERFINFO_AUTO_STOP(); // Check Options
					continue;
				} 
			} else {
				//bCanAccessFullContactRemotely == true, check to make sure the contact has something to say
				if(!pContact->eContactFlags && !missionFound) {
					if(!bInfoFound) {
						contact_RemoveRemoteContact(pInteract, pRContact);
						eaDestroyStruct(&eaOptions, parse_ContactDialogOption);
						PERFINFO_AUTO_STOP(); // Check Options
						continue;
					}
				}
			}

			PERFINFO_AUTO_STOP(); // Check Options

			iPriority = MAX(eFlags & ContactFlag_RemoteOfferReturn, eFlags & ContactFlag_RemoteOfferGrant);
			iPriority = MAX(iPriority, eFlags & ContactFlag_RemoteSpecDialog);
			iPriority = MAX(iPriority, eFlags & ContactFlag_RemoteOfferInProgress);

			contact_AddRemoteContact(pEnt, pInteract, pRContact, eFlags, iPriority, &eaOptions);
			eaDestroy(&eaOptions);
		}
	}

	entity_SetDirtyBit(pEnt, parse_InteractInfo, pInteract, true);

	PERFINFO_AUTO_STOP();
}

void contact_ClearPlayerContactTrackingData(Entity *pPlayerEnt)
{
	if (pPlayerEnt && pPlayerEnt->pPlayer && pPlayerEnt->pPlayer->pInteractInfo) {
		eaDestroyStruct(&pPlayerEnt->pPlayer->pInteractInfo->recentlyCompletedDialogs, parse_ContactDialogInfo);
	}
}

void contact_ClearAllPlayerContactTrackingData(void)
{
	EntityIterator* iter = entGetIteratorSingleTypeAllPartitions(0, ENTITYFLAG_IGNORE, GLOBALTYPE_ENTITYPLAYER);
	Entity *pPlayerEnt;

	while ((pPlayerEnt = EntityIteratorGetNext(iter)))
	{
		contact_ClearPlayerContactTrackingData(pPlayerEnt);
	}
	EntityIteratorRelease(iter);
}

AUTO_FIXUPFUNC;
TextParserResult ContactDialog_Fixup(ContactDialog* pDialog, enumTextParserFixupType eFixupType, void *pExtraData)
{
	switch (eFixupType)
	{
		xcase FIXUPTYPE_DESTRUCTOR:
		{
			if (GET_REF(pDialog->hPersistStoreDef))
			{
				//If this alert is triggered, a player has a contact dialog that has a persisted store on it that
				//didn't call contact_CleanupDialog before the structure was destroyed. 
				Alertf("A ContactDialog didn't remove a Persisted Store Request (%s) before DeInit was called.",
					REF_STRING_FROM_HANDLE(pDialog->hPersistStoreDef));
			}
		}
	}
	return true;
}

//--------------------------------
// CONTACT OVERRIDE FUNCTIONS
//--------------------------------

bool contact_ApplyNamespacedSpecialDialogOverride(const char *pchMissionName, ContactDef *pContactDef, SpecialDialogBlock *pDialog)
{
	ContactOverrideData *pContactOverrideData = NULL;
	SpecialDialogOverrideData *pDialogOverrideData = NULL;
	char pchSpecialDialogName[MAX_PATH];

	if (!pContactDef || !pDialog) {
		return false;
	}

	sprintf(pchSpecialDialogName, "%s/%s", pchMissionName, pDialog->name);

	// Add the properties
	pContactOverrideData = eaIndexedGetUsingString(&s_eaNamespacedContactOverrides, pContactDef->name);
	if (!pContactOverrideData) {
		pContactOverrideData = StructCreate(parse_ContactOverrideData);
		pContactOverrideData->pchContact = allocAddString(pContactDef->name);
		if(!s_eaNamespacedContactOverrides)
		{
			eaCreate(&s_eaNamespacedContactOverrides);
			eaIndexedEnable(&s_eaNamespacedContactOverrides, parse_ContactOverrideData);
		}
		eaIndexedAdd(&s_eaNamespacedContactOverrides, pContactOverrideData);
	}
	pDialogOverrideData = StructCreate(parse_SpecialDialogOverrideData);
	pDialogOverrideData->pchSourceName = allocAddString(pchMissionName);
	pDialogOverrideData->pSpecialDialogBlock = StructClone(parse_SpecialDialogBlock, pDialog);
	if(pDialogOverrideData->pSpecialDialogBlock)
		pDialogOverrideData->pSpecialDialogBlock->name = allocAddString(pchSpecialDialogName);
	eaPush(&pContactOverrideData->eaSpecialDialogOverrides, pDialogOverrideData);

	return true;
}

void contact_RemoveNamespacedMissionOfferOverridesFromMission(const char *pcMissionName)
{
	if(pcMissionName && pcMissionName[0])
	{
		int i,j;
		for (i = eaSize(&s_eaNamespacedContactOverrides)-1; i >= 0; i--) 
		{
			ContactOverrideData* pData = s_eaNamespacedContactOverrides[i];

			for (j=eaSize(&pData->eaMissionOfferOverrides)-1; j >= 0 ; j--) 
			{
				if(stricmp(pData->eaMissionOfferOverrides[j]->pchSourceName, pcMissionName) == 0)
				{
					StructDestroy(parse_MissionOfferOverrideData, pData->eaMissionOfferOverrides[j]);
					eaRemove(&pData->eaMissionOfferOverrides, j);
				}
			}
		}
	}
}

void contact_RemoveNamespacedSpecialDialogOverridesFromMission(const char *pcMissionName)
{
	if(pcMissionName && pcMissionName[0])
	{
		int i,j;
		for (i = eaSize(&s_eaNamespacedContactOverrides)-1; i >= 0; i--) 
		{
			ContactOverrideData* pData = s_eaNamespacedContactOverrides[i];

			for (j=eaSize(&pData->eaSpecialDialogOverrides)-1; j >= 0 ; j--) 
			{
				if(stricmp(pData->eaSpecialDialogOverrides[j]->pchSourceName, pcMissionName) == 0)
				{
					StructDestroy(parse_SpecialDialogOverrideData, pData->eaSpecialDialogOverrides[j]);
					eaRemove(&pData->eaSpecialDialogOverrides, j);
				}
			}
		}
	}
}

/// This is one of THREE known places where Missions apply contact
/// overrides.  The three places are:
/// 
/// * contact_FixupOverrides (at Contact load)
/// * contact_MissionAddedOrPostModifiedFixup (at Mission load)
/// * contact_ApplyNamespacedMissionOfferOverride (at Namespace load)
///
/// In the normal startup case, Contact load does nothing and Mission
/// load does all the work.  Namespace load is primarily used by UGC.
///
/// I didn't write this code, I just updated it.
/// Jared F. (July/10/2012)
bool contact_ApplyNamespacedMissionOfferOverride(ContactDef *pContactDef, ContactMissionOffer *pOffer)
{
	ContactOverrideData *pContactOverrideData = NULL;
	MissionOfferOverrideData *pOfferOverrideData = NULL;
	MissionDef *pMissionDef = pOffer ? GET_REF(pOffer->missionDef) : NULL;

	if (!pContactDef || !pMissionDef) {
		return false;
	}

	// Add the properties
	pContactOverrideData = eaIndexedGetUsingString(&s_eaNamespacedContactOverrides, pContactDef->name);
	if (!pContactOverrideData){
		pContactOverrideData = StructCreate(parse_ContactOverrideData);
		pContactOverrideData->pchContact = allocAddString(pContactDef->name);
		if(!s_eaNamespacedContactOverrides)
		{
			eaCreate(&s_eaNamespacedContactOverrides);
			eaIndexedEnable(&s_eaNamespacedContactOverrides, parse_ContactOverrideData);
		}
		eaIndexedAdd(&s_eaNamespacedContactOverrides, pContactOverrideData);
	}
	pOfferOverrideData = StructCreate(parse_MissionOfferOverrideData);
	pOfferOverrideData->pchSourceName = allocAddString(pMissionDef->name);
	pOfferOverrideData->pMissionOffer = StructClone(parse_ContactMissionOffer, pOffer);
	eaPush(&pContactOverrideData->eaMissionOfferOverrides, pOfferOverrideData);

	if (pOffer->eRemoteFlags && !pContactDef->bHideFromRemoteContactList) 
	{
		ContactFlags eRemoteFlags = contact_GenerateRemoteFlags(pContactDef);
		contact_AddRemoteContactToServerList(pContactDef, eRemoteFlags);
	}
	return true;
}

bool contact_ApplyNamespacedImageMenuItemOverride(const char *pchMissionName, ContactDef *pContactDef, ContactImageMenuItem *pItem)
{
	ContactOverrideData *pContactOverrideData = NULL;
	ImageMenuItemOverrideData* pItemData = NULL;

	if (!pContactDef) {
		return false;
	}

	// Add the properties
	pContactOverrideData = eaIndexedGetUsingString(&s_eaNamespacedContactOverrides, pContactDef->name);
	if (!pContactOverrideData){
		pContactOverrideData = StructCreate(parse_ContactOverrideData);
		pContactOverrideData->pchContact = allocAddString(pContactDef->name);
		if(!s_eaNamespacedContactOverrides)
		{
			eaCreate(&s_eaNamespacedContactOverrides);
			eaIndexedEnable(&s_eaNamespacedContactOverrides, parse_ContactOverrideData);
		}
		eaIndexedAdd(&s_eaNamespacedContactOverrides, pContactOverrideData);
	}
	pItemData = StructCreate(parse_ImageMenuItemOverrideData);
	pItemData->pchSourceName = allocAddString(pchMissionName);
	pItemData->pItem = StructClone(parse_ContactImageMenuItem, pItem);
	eaPush(&pContactOverrideData->eaImageMenuItemOverrides, pItemData);
	
	return true;
}

void contact_RemoveNamespacedImageMenuItemOverridesFromMission(const char *pcMissionName)
{
	if(pcMissionName && pcMissionName[0])
	{
		int i,j;
		for (i = eaSize(&s_eaNamespacedContactOverrides)-1; i >= 0; i--) 
		{
			ContactOverrideData* pData = s_eaNamespacedContactOverrides[i];

			for (j=eaSize(&pData->eaImageMenuItemOverrides)-1; j >= 0 ; j--) 
			{
				if(stricmp(pData->eaImageMenuItemOverrides[j]->pchSourceName, pcMissionName) == 0)
				{
					StructDestroy(parse_ImageMenuItemOverrideData, pData->eaImageMenuItemOverrides[j]);
					eaRemove(&pData->eaImageMenuItemOverrides, j);
				}
			}
		}
	}
}

MissionOfferOverrideData** contact_GetNamespacedMissionOfferOverrideList(ContactDef* pContact)
{
	if(pContact)
	{
		ContactOverrideData* pOverrideData = eaIndexedGetUsingString(&s_eaNamespacedContactOverrides, pContact->name);
		if(pOverrideData && pOverrideData->eaMissionOfferOverrides)
		{
			return pOverrideData->eaMissionOfferOverrides;
		}
	}
	return NULL;
}

SpecialDialogOverrideData** contact_GetNamespacedSpecialDialogOverrideList(ContactDef* pContact)
{
	if(pContact)
	{
		ContactOverrideData* pOverrideData = eaIndexedGetUsingString(&s_eaNamespacedContactOverrides, pContact->name);
		if(pOverrideData && pOverrideData->eaSpecialDialogOverrides)
		{
			return pOverrideData->eaSpecialDialogOverrides;
		}
	}
	return NULL;
}

ImageMenuItemOverrideData** contact_GetNamespacedImageMenuItemOverrideList(ContactDef* pContact)
{
	if(pContact)
	{
		ContactOverrideData* pOverrideData = eaIndexedGetUsingString(&s_eaNamespacedContactOverrides, pContact->name);
		if(pOverrideData && pOverrideData->eaImageMenuItemOverrides)
		{
			return pOverrideData->eaImageMenuItemOverrides;
		}
	}
	return NULL;
}

SpecialDialogBlock* contact_SpecialDialogOverrideFromName(ContactDef* pContact, const char* pchSpecialDialog)
{
	if(pContact && pchSpecialDialog && *pchSpecialDialog)
	{
		ContactOverrideData *pOverrideData = eaIndexedGetUsingString(&s_eaNamespacedContactOverrides, pContact->name);
		if(pOverrideData)
		{
			int i;
			for (i=0; i < eaSize(&pOverrideData->eaSpecialDialogOverrides); i++) 
			{
				SpecialDialogBlock* pDialogBlock = pOverrideData->eaSpecialDialogOverrides[i]->pSpecialDialogBlock;
				if(pDialogBlock && stricmp(pDialogBlock->name, pchSpecialDialog) == 0)
					return pDialogBlock;
			}
		}
	}
	return NULL;
}

SpecialActionBlock *contact_SpecialActionBlockOverrideFromName(ContactDef *pContact, const char *pchSpecialActionBlock) {
	if(pContact && pchSpecialActionBlock && *pchSpecialActionBlock)
	{
		ContactOverrideData *pOverrideData = eaIndexedGetUsingString(&s_eaNamespacedContactOverrides, pContact->name);
		if(pOverrideData)
		{
			int i;
			for (i=0; i < eaSize(&pOverrideData->eaSpecialDialogOverrides); i++) 
			{
				SpecialActionBlock* pActionBlock = pOverrideData->eaSpecialActionBlockOverrides[i]->pSpecialActionBlock;
				if(pActionBlock && stricmp(pActionBlock->name, pchSpecialActionBlock) == 0)
					return pActionBlock;
			}
		}
	}
	return NULL;
}

ContactDialogInfo* contact_GetLastCompletedDialog(Entity* pEnt, ContactInfo** ppContactInfo, bool bValidateInteract)
{
	InteractInfo* pInteractInfo = SAFE_MEMBER2(pEnt, pPlayer, pInteractInfo);
	if (pInteractInfo)
	{
		ContactDialogInfo* pDialogInfo = eaTail(&pInteractInfo->recentlyCompletedDialogs);
		if (!bValidateInteract)
		{
			return pDialogInfo;
		}
		if (pDialogInfo)
		{
			ContactDef* pContactDef = GET_REF(pDialogInfo->hContact);
			if (pContactDef)
			{
				if (contact_CanInteract(pContactDef, pEnt))
				{
					bool bCanInteract = false;
					if (pContactDef->canAccessRemotely)
					{
						bCanInteract = contact_Evaluate(pEnt, pContactDef->canAccessRemotely, NULL);
					}
					if (!bCanInteract)
					{
						int i;
						for (i = 0; i < eaSize(&pInteractInfo->nearbyContacts); i++)
						{
							ContactInfo* pContactInfo = pInteractInfo->nearbyContacts[i];
							if (pContactInfo->pchContactDef == pContactDef->name)
							{
								if (ppContactInfo)
								{
									(*ppContactInfo) = pContactInfo;
								}
								bCanInteract = interaction_CanInteractWithContact(pEnt, pContactInfo);
								break;
							}
						}
					}

					if (bCanInteract)
					{
						return pDialogInfo;
					}
				}
			}
		}
	}
	return NULL;
}

AUTO_EXPR_FUNC(Player, Mission) ACMD_NAME("SetSkill");
void exprSetSkill(SA_PARAM_OP_VALID Entity *pEnt, int iSkillType)
{
	SetSkillType(pEnt, iSkillType);
}

#include "gslContact_h_ast.c"
