/***************************************************************************
*     Copyright (c) 2006-2007, Cryptic Studios
*     All Rights Reserved
*     Confidential Property of Cryptic Studios
***************************************************************************/

#include "GameEvent.h"

#include "encounter_common.h"
#include "EString.h"
#include "Entity.h"
#include "Expression.h"
#include "GlobalTypes.h"
#include "StringCache.h"
#include "WorldGrid.h"
#include "entEnums_h_ast.h"
#include "MinigameCommon.h"
#include "mission_common.h"
#include "oldencounter_common.h"
#include "ResourceInfo.h"
#include "wlEncounter.h"
#include "../StaticWorld/ZoneMap.h"

#ifdef GAMESERVER
#include "gslInteractable.h"
#include "contact_common.h"
#endif

#include "itemEnums_h_ast.h"
#include "GameEvent_h_ast.h"
#include "MinigameCommon_h_ast.h"
#include "mission_enums_h_ast.h"
#include "encounter_enums_h_ast.h"


AUTO_RUN_ANON(memBudgetAddMapping(__FILE__, BUDGET_GameSystems););

static GameEventTypeInfo s_GameEventTypeInfo[EventType_Count];

// -----------------------------------------------------------------------
// Static checking
// -----------------------------------------------------------------------

// returns TRUE if a Listening GameEvent has Source entity info filled out
// Unused
static bool gameevent_ListenerHasSourceEnt(GameEvent *ev)
{
	if (ev->pchSourceActorName || ev->pchSourceCritterGroupName || ev->pchSourceCritterName
		|| ev->pchSourceEncounterName || ev->pchSourceObjectName || ev->pchSourceStaticEncName
		|| ev->pchSourceRank || ev->eSourceRegionType != -1 
		|| ev->tMatchSource || ev->tMatchSourceTeam || ev->tSourceIsPlayer 
		|| ev->pchSourceClassName || ev->pchSourcePowerMode || eaiSize(&ev->piSourceCritterTags) ){
			return true;
	}
	return false;
}

// returns TRUE if a Listening GameEvent has Target entity info filled out
static bool gameevent_ListenerHasTargetEnt(GameEvent *ev)
{
	if (ev->pchTargetActorName || ev->pchTargetCritterGroupName || ev->pchTargetCritterName
		|| ev->pchTargetEncounterName || ev->pchTargetObjectName || ev->pchTargetStaticEncName
		|| ev->pcTargetRank || ev->eTargetRegionType != -1 
		|| ev->tMatchTarget || ev->tMatchTargetTeam || ev->tTargetIsPlayer
		|| ev->pchTargetClassName || ev->pchTargetPowerMode || eaiSize(&ev->piTargetCritterTags)){
			return true;
	}
	return false;
}

// Runs some validation on a GameEvent
bool gameevent_Validate(GameEvent *ev, char **estrError, const char *pchErrorDetailString, bool bAllowPlayerScoped)
{
	bool success = true;
	if (ev && (ev->tMatchSource || ev->tMatchSourceTeam || ev->tMatchTarget || ev->tMatchTargetTeam) && !bAllowPlayerScoped)
	{
		// tMatchSource, etc. only work on Missions and EncounterDefs, or in an event that is explicitly player-scoped
		if (estrError){
			if (pchErrorDetailString && pchErrorDetailString[0])
				estrPrintf(estrError, "'CurrentPlayer/Encounter' option used in a non-player scoped event (not a Mission or Encounter)! (%s)", pchErrorDetailString);
			else
				estrPrintf(estrError, "'CurrentPlayer/Encounter' option used in a non-player scoped event (not a Mission or Encounter)!");
		}
		success = false;
	}
	else if (ev && (ev->type == EventType_InteractBegin
				|| ev->type == EventType_InteractFailure
				|| ev->type == EventType_InteractInterrupted
				|| ev->type == EventType_InteractSuccess
				|| ev->type == EventType_InteractEndActive))
	{
		// Interaction Events may have a Clickable name or a Critter, but not both
		if (gameevent_ListenerHasTargetEnt(ev) && (ev->pchClickableName || ev->pchClickableGroupName))
		{
			if (estrError){
				if (pchErrorDetailString && pchErrorDetailString[0])
					estrPrintf(estrError, "Interaction Events may have a Clickable or a Target critter, but not both! (%s)", pchErrorDetailString);
				else
					estrPrintf(estrError, "Interaction Events may have a Clickable or a Target critter, but not both!");
			}
			success = false;
		}
	}
	else if (ev && ev->pChainEventDef)
	{
		if (ev->fChainTime <= 0.f)
		{
			estrPrintf(estrError, "Chain Time must be greater than zero with a valid chained game event.");
			success = false;
		}

	}
#ifdef GAMESERVER
	else if (ev && (ev->type == EventType_ContactDialogStart || ev->type == EventType_ContactDialogComplete) &&
		ev->pchContactName && ev->pchDialogName)
	{

		ContactDef *pContactDef = (ContactDef *)RefSystem_ReferentFromString(g_ContactDictionary, ev->pchContactName);
		// Make sure the contact is valid
		if (pContactDef == NULL)
		{
			char cNamespace[1024];
			cNamespace[0] = '\0';

			if (ev->pchContactName) {
				resExtractNameSpace_s(ev->pchContactName, cNamespace, 1025, NULL, 0);
			}
			if (strStartsWith(cNamespace, "Dyn_Sc_")) {
				// Don't complain about references to contacts in a star cluster namespace.
				// This is here because missions outside the namespace may reference contacts
				// inside a namespace and such contacts won't ever be loaded at the time we validate missions.
			} else {
				estrPrintf(estrError, "You have defined a contact dialog start/complete event listener but the contact does not exist. Contact name: %s", ev->pchContactName);
				success = false;
			}
		}
		else
		{
			// Make sure the contact has the special dialog entered
			if (contact_SpecialDialogFromName(pContactDef, ev->pchDialogName) == NULL)
			{
				estrPrintf(estrError, "You have defined a contact dialog start/complete event listener but the dialog does not exist in the contact. Contact name: %s, Dialog name: %s. If you're using an override dialog, please make sure you use the internal name (The one prefixed with the mission name).", ev->pchContactName, ev->pchDialogName);
				success = false;
			}
		}
	}
#endif
	return success;
}

static int StaticCheckEvent_Helper(ExprContext *context, MultiVal *pMV, char **estrError, bool playerScoped)
{
	bool success = true;
	if(!pMV->type==MULTI_STRING)
	{
		estrPrintf(estrError, "Must be a string");
		return false;
	}
	else if (pMV->str && pMV->str[0] && stricmp(pMV->str, MULTI_DUMMY_STRING)) // An empty string or MULTI_DUMMY_STRING means it's probably a GetExternStringVar, which we shouldn't check
	{
		MissionDef *missionDef = NULL;
		EncounterTemplate *pTemplate = NULL;
		EncounterDef *encDef = NULL;
#ifdef GAMESERVER
		GameInteractable *interactable = NULL;
#endif
		WorldLayerFSM *pLayerFSM = NULL;
		const char *eventName = pMV->str;
		GameEvent *ev = gameevent_EventFromString(eventName);
		const char *blamefile = exprContextGetBlameFile(context);
		char *objString = NULL;
		estrStackCreate(&objString);

		if ((missionDef = exprContextGetVarPointerUnsafePooled(context, g_MissionDefVarName)))
			estrPrintf(&objString, "'%s'", missionDef->pchRefString);
		else if ((pTemplate = exprContextGetVarPointerUnsafe(context, "EncounterTemplate")))
			estrPrintf(&objString, "EncounterTemplate '%s'", pTemplate->pcName);
		else if (gConf.bAllowOldEncounterData && (encDef = exprContextGetVarPointerUnsafe(context, "EncounterDef")))
			estrPrintf(&objString, "Encounter '%s'", encDef->name);
#ifdef GAMESERVER
		else if ((interactable = exprContextGetVarPointerUnsafePooled(context, g_InteractableExprVarName)))
			estrPrintf(&objString, "Clickable '%s'", interactable->pcName);
#endif
		else if ((pLayerFSM = exprContextGetVarPointerUnsafe(context, "INTERNAL_LayerFSM")))
			estrPrintf(&objString, "LayerFSM '%s'", REF_STRING_FROM_HANDLE(pLayerFSM->properties->hFSM));

		if (!ev)
		{
			// Error: could not determine the type of event.
			if (objString && objString[0])
				estrPrintf(estrError, "Invalid event in %s: %s", objString, eventName);
			else
				estrPrintf(estrError, "Invalid event: %s", eventName);
			success = false;
		}
		else
		{
			bool bHasEncounter = (pTemplate != NULL) || (encDef != NULL);
			bool bAllowPlayerScoped = (playerScoped || (missionDef && missionDef->missionType == MissionType_OpenMission) || bHasEncounter);
			gameevent_Validate(ev, estrError, objString, bAllowPlayerScoped);
		}

		estrDestroy(&objString);
		StructDestroy(parse_GameEvent, ev);
	}

	return success;
}

static int StaticCheckEvent(ExprContext *context, MultiVal *pMV, char **estrError)
{
	return StaticCheckEvent_Helper(context, pMV, estrError, false);
}
static int StaticCheckPlayerScopedEvent(ExprContext *context, MultiVal *pMV, char **estrError)
{
	return StaticCheckEvent_Helper(context, pMV, estrError, true);
}


AUTO_STARTUP(ExpressionSCRegister);
void gameevent_InitStaticCheck(void)
{
	exprRegisterStaticCheckArgumentType("Event", NULL, StaticCheckEvent);
	exprRegisterStaticCheckArgumentType("PlayerScopedEvent", NULL, StaticCheckPlayerScopedEvent);
}

// -----------------------------------------------------------------------
// Static GameEvent metadata initialization and accessors
// -----------------------------------------------------------------------

// This data determines which fields are displayed in the Event Editor
AUTO_RUN;
void gameevent_InitTypeInfo(void)
{
	GameEventTypeInfo *info = NULL;
	int i, n = EventType_Count;
	for (i = 0; i < n; i++)
	{
		info = &s_GameEventTypeInfo[i];
		memset(info, 0, sizeof(GameEventTypeInfo));
		switch (i)
		{
			xcase EventType_ClickableActive:
			case EventType_ZoneEventRunning:
			case EventType_ZoneEventState:
				info->pchDescription = "Deprecated Event";
				info->bDeprecated = true;
			xcase EventType_ContactDialogStart:
			case EventType_ContactDialogComplete:
				info->pchDescription = "A \"Special Dialog\" has been started/completed on a Contact";
				info->hasSourcePlayer = true;
				info->hasSourcePlayerTeam = true;
				info->hasContactName = true;
				info->hasDialogName = true;
				info->hasMap = true;
			xcase EventType_CutsceneEnd:
				info->pchDescription = "A cutscene has ended";
				info->hasCutsceneName = true;
				info->hasMap = true;
				info->hasSourcePlayer = true;
				info->hasSourcePlayerTeam = true;
			xcase EventType_CutsceneStart:
				info->pchDescription = "A cutscene has started";
				info->hasCutsceneName = true;
				info->hasMap = true;
				info->hasSourcePlayer = true;
				info->hasSourcePlayerTeam = true;
			xcase EventType_VideoEnded:
				info->pchDescription = "A video has ended";
				info->hasVideoName = true;
				info->hasMap = true;
			xcase EventType_VideoStarted:
				info->pchDescription = "A video has started";
				info->hasVideoName = true;
				info->hasMap = true;
			xcase EventType_Damage:
				info->pchDescription = "An entity did damage to another entity.  \n\tSource: attacker \n\tTarget: defender";
				info->hasSourceCritter = true;
				info->hasSourceEncounter = true;
				info->hasSourcePlayer = true;
				info->hasSourcePlayerTeam = true;
				info->hasTargetCritter = true;
				info->hasTargetEncounter = true;
				info->hasTargetPlayer = true;
				info->hasTargetPlayerTeam = true;
				info->hasDamageType = true;
				info->hasMap = true;
				info->hasVolumeName = true;
			xcase EventType_EncounterState:
				info->pchDescription = "An Encounter has changed state.  \n\tSource: Players with credit for the Encounter ('Success' state only)";
				//info->hasSourceCritter = true;  // This actually works, but it's too confusing for users I think
				info->hasSourcePlayer = true;
				info->hasSourcePlayerTeam = true;
				info->hasTargetEncounter = true;
				info->hasEncState = true;
				info->hasMap = true;
			xcase EventType_FSMState:
				info->pchDescription = "A critter's FSM changed state";
				info->hasSourceCritter = true;
				info->hasSourceEncounter = true;
				info->hasFSMName = true;
				info->hasFsmStateName = true;
				info->hasMap = true;
				info->hasVolumeName = true;
			xcase EventType_Healing:
				info->pchDescription = "An entity healed another entity.  \n\tSource: healer \n\tTarget: heal target";
				info->hasSourceCritter = true;
				info->hasSourceEncounter = true;
				info->hasSourcePlayer = true;
				info->hasSourcePlayerTeam = true;
				info->hasTargetCritter = true;
				info->hasTargetEncounter = true;
				info->hasTargetPlayer = true;
				info->hasTargetPlayerTeam = true;
				info->hasAttribType = true;
				info->hasMap = true;
				info->hasVolumeName = true;
			xcase EventType_HealthState:
				info->pchDescription = "A character's health has dropped into a certain health range";
				info->hasSourceCritter = true;
				info->hasSourcePlayer = true;
				info->hasHealthState = true;
				info->hasMap = true;
			xcase EventType_InteractBegin:
				info->pchDescription = "Sends when a player first clicks on a Clickable or Critter and the Interact Bar starts filling.  Source: player, Target: critter (if any)";
				info->hasClickableName = true;
				info->hasSourcePlayer = true;
				info->hasSourcePlayerTeam = true;
				info->hasTargetCritter = true;
				info->hasTargetEncounter = true;
				info->hasContactName = true;
				info->hasMap = true;
				info->hasVolumeName = true;
			xcase EventType_InteractFailure:
				info->pchDescription = "Sends when a player tries to interact with something, but doesn't meet the Success condition.  Source: player, Target: critter (if any)";
				info->hasClickableName = true;
				info->hasSourcePlayer = true;
				info->hasSourcePlayerTeam = true;
				info->hasTargetCritter = true;
				info->hasTargetEncounter = true;
				info->hasContactName = true;
				info->hasMap = true;
				info->hasVolumeName = true;
			xcase EventType_InteractInterrupted:
				info->pchDescription = "A player was interrupted while interacting.  Source: player, Target: critter (if any)";
				info->hasClickableName = true;
				info->hasSourcePlayer = true;
				info->hasSourcePlayerTeam = true;
				info->hasTargetCritter = true;
				info->hasTargetEncounter = true;
				info->hasContactName = true;
				info->hasMap = true;
				info->hasVolumeName = true;
			xcase EventType_InteractSuccess:
				info->pchDescription = "A player has successfully interacted with a Clickable or Critter.  Source: player, Target: critter (if any)";
				info->hasClickableName = true;
				info->hasSourcePlayer = true;
				info->hasSourcePlayerTeam = true;
				info->hasTargetCritter = true;
				info->hasTargetEncounter = true;
				info->hasContactName = true;
				info->hasMap = true;
				info->hasVolumeName = true;
			xcase EventType_InteractEndActive:
				info->pchDescription = "A clickable's \"Active\" state has completed, and Cooldown will begin.";
				info->hasClickableName = true;
				info->hasSourcePlayer = true;
				info->hasSourcePlayerTeam = true;
				info->hasMap = true;
				info->hasVolumeName = true;
			xcase EventType_ItemGained:
				info->pchDescription = "Sends when a player receives an Item.  (Don't use this for Missions!  You probably want PlayerItemCount())";
				info->hasItemName = true;
				info->hasItemCategories = true;
				info->hasSourcePlayer = true;
				info->hasSourcePlayerTeam = true;
				info->hasMap = true;
				info->hasVolumeName = true;
			xcase EventType_ItemLost:
				info->pchDescription = "Sends when a player loses an Item.  (Don't use this for Missions!  You probably want PlayerItemCount())";
				info->hasItemName = true;
				info->hasItemCategories = true;
				info->hasSourcePlayer = true;
				info->hasSourcePlayerTeam = true;
				info->hasMap = true;
				info->hasVolumeName = true;
			xcase EventType_ItemPurchased:
				info->pchDescription = "Sends when a player purchases an Item from a Store.";
				info->hasItemName = true;
				info->hasSourcePlayer = true;
				info->hasSourcePlayerTeam = true;
				info->hasContactName = true;
				info->hasStoreName = true;
				info->hasMap = true;
			xcase EventType_ItemPurchaseEP:
				info->pchDescription = "Sends when a player purchases an Item from a Store, but counts the Economy Point value of the item.";
				info->hasItemName = true;
				info->hasSourcePlayer = true;
				info->hasSourcePlayerTeam = true;
				info->hasContactName = true;
				info->hasStoreName = true;
				info->hasMap = true;
			xcase EventType_ItemUsed:
				info->pchDescription = "Sends when a player uses a power on an item.";
				info->hasItemName = true;
				info->hasSourcePlayer = true;
				info->hasSourcePlayerTeam = true;
				info->hasPower = true;
				info->hasMap = true;
				info->hasVolumeName = true;
			xcase EventType_Kills:
				info->pchDescription = "Something was killed.  \n\tSource: Ents with credit for the kill \n\tTarget: Dead thing";
				info->hasSourceCritter = true;
				info->hasSourcePlayer = true;
				info->hasSourcePlayerTeam = true;
				info->hasTargetCritter = true;
				info->hasTargetPlayer = true;
				info->hasTargetPlayerTeam = true;
				info->hasVolumeName = true;
				info->hasMap = true;
			xcase EventType_NearDeath:
				info->pchDescription = "A player has entered the near death state\n\tTarget: Nearly dead thing";
				info->hasTargetPlayer = true;
				info->hasTargetPlayerTeam = true;
			xcase EventType_Assists:
				info->pchDescription = "Something was killed, and assists are enabled. \n\tSource: Ents with credit on the assist \n\tTarget: Dead thing";
				info->hasSourceCritter = true;
				info->hasSourcePlayer = true;
				info->hasSourcePlayerTeam = true;
				info->hasTargetCritter = true;
				info->hasTargetPlayer = true;
				info->hasTargetPlayerTeam = true;
				info->hasVolumeName = true;
				info->hasMap = true;
			xcase EventType_LevelUp:
				info->pchDescription = "A player leveled up.";
				info->hasSourcePlayer = true;
				info->hasSourcePlayerTeam = true;
				info->hasMap = true;
				xcase EventType_LevelUpPet:
				info->pchDescription = "A player's pet leveled up.";
				info->hasSourcePlayer = true;
				info->hasSourcePlayerTeam = true;
				info->hasMap = true;
			xcase EventType_MissionLockoutState:
				info->pchDescription = "Sends every time a Mission Lockout List changes state.";
				info->hasMissionRefString = true;
				info->hasMissionLockoutState = true;
				info->hasMap = true;
			xcase EventType_MissionState:
				info->pchDescription = "Sends every time a Mission changes state.  You probably want 'MissionState___()'";
				info->hasSourcePlayer = true;
				info->hasSourcePlayerTeam = true;
				info->hasMissionRefString = true;
				info->hasMissionState = true;
				info->hasMap = true;
				info->hasUGCProject = true;
			xcase EventType_NemesisState:
				info->pchDescription = "Sends every time a Nemesis changes state.  You probably want 'PlayerHasPrimaryNemesis()' or something else.";
				info->hasSourcePlayer = true;
				info->hasSourcePlayerTeam = true;
				info->hasNemesisState = true;
				info->hasNemesisName = true;
				info->hasMap = true;
			xcase EventType_PowerAttrModApplied:
				info->pchDescription = "An entity has used a power on another entity";
				info->hasSourceCritter = true;
				info->hasSourcePlayer = true;
				info->hasSourcePlayerTeam = true;
				info->hasTargetCritter = true;
				info->hasTargetPlayer = true;
				info->hasTargetPlayerTeam = true;
				info->hasPower = true;
				info->hasVolumeName = true;
				info->hasMap = true;
			xcase EventType_PickedUpObject:
				info->pchDescription = "A player has picked up an object";
				info->hasSourceCritter = true;
				info->hasSourcePlayer = true;
				info->hasSourcePlayerTeam = true;
				info->hasTargetCritter = true;
				info->hasTargetPlayer = true;
				info->hasTargetPlayerTeam = true;
				info->hasVolumeName = true;
				info->hasMap = true;
			xcase EventType_PlayerSpawnIn:
				info->pchDescription = "Player spawned in";
				info->hasMap = true;
			xcase EventType_Poke:
				info->pchDescription = "Any expression function can use this to send an arbitrarily tagged event";
				info->hasSourceCritter = true;
				info->hasTargetCritter = true;
				info->hasTargetPlayer = true;
				info->hasTargetPlayerTeam = true;
				info->hasMessage = true;
				info->hasMap = true;
			xcase EventType_VolumeEntered:
				info->pchDescription = "An entity has entered a volume";
				info->hasSourceCritter = true;
				info->hasSourcePlayer = true;
				info->hasSourcePlayerTeam = true;
				info->hasVolumeName = true;
				info->hasMap = true;
			xcase EventType_VolumeExited:
				info->pchDescription = "An entity has left a volume";
				info->hasSourceCritter = true;
				info->hasSourcePlayer = true;
				info->hasSourcePlayerTeam = true;
				info->hasVolumeName = true;
				info->hasMap = true;
			xcase EventType_Emote:
				info->pchDescription = "A player emoted";
				info->hasSourcePlayer = true;
				info->hasSourcePlayerTeam = true;
				info->hasEmoteName = true;
				info->hasVolumeName = true;
				info->hasMap = true;
			xcase EventType_ItemAssignmentStarted:
				info->pchDescription = "A player started an item assignment";
				info->hasSourcePlayer = true;
				info->hasSourcePlayerTeam = true;
				info->hasItemAssignmentName = true;
				info->hasVolumeName = true;
				info->hasMap = true;
			xcase EventType_ItemAssignmentCompleted:
				info->pchDescription = "A player completed an item assignment";
				info->hasSourcePlayer = true;
				info->hasSourcePlayerTeam = true;
				info->hasItemAssignmentName = true;
				info->hasItemAssignmentOutcome = true;
				info->hasItemAssignmentSpeedBonus = true;
				info->hasVolumeName = true;
				info->hasMap = true;
			xcase EventType_BagGetsItem:
				info->pchDescription = "Sends when a player moves an item into a bag.";
				info->hasBagName = true;
				info->hasSourcePlayer = true;
			xcase EventType_DuelVictory:
				info->pchDescription = "One player has defeated another";
				info->hasSourcePlayer = true;
				info->hasTargetPlayer = true;
				info->hasVictoryType = true;
			xcase EventType_PvPQueueMatchResult:
				info->pchDescription = "Sends when a player wins or loses a PvP queue match.";
				info->hasSourcePlayer = true;
				info->hasPvPQueueMatchResult = true;
			xcase EventType_MinigameBet:
				info->pchDescription = "Sends when a player bets in a minigame.";
				info->hasSourcePlayer = true;
				info->hasItemName = true;
				info->hasMinigameType = true;
			xcase EventType_MinigamePayout:
				info->pchDescription = "Sends when a player wins something in a minigame.";
				info->hasSourcePlayer = true;
				info->hasItemName = true;
				info->hasMinigameType = true;
			xcase EventType_MinigameJackpot:
				info->pchDescription = "Sends when a player wins a jackpot in a minigame.";
				info->hasSourcePlayer = true;
				info->hasMinigameType = true;
				info->hasClickableName = true;
			xcase EventType_PvPEvent:
				info->pchDescription = "Sends when specific pvp events occur.";
				info->hasSourcePlayer = true;
				info->hasTargetPlayer = true;
				info->hasPvPEventType = true;
			xcase EventType_GemSlotted:
				info->pchDescription = "Sends when a player slots a gem into an item";
				info->hasSourcePlayer = true;
				info->hasItemName = true;
				info->hasGemName = true;
				info->hasMap = true;
			xcase EventType_PowerTreeStepAdded:
				info->pchDescription = "Sends when a player adds a step to a power tree";
				info->hasSourcePlayer = true;
				info->hasMap = true;
			xcase EventType_ContestWin:
				info->pchDescription = "Sends when a player places in a contest";
				info->hasSourcePlayer = true;
				info->hasMap = true;
				info->hasMissionRefString = true;
			xcase EventType_ScoreboardMetricResult:
				info->pchDescription = "Sends when a player places for a certain metric in a queued scoreboard";
				info->hasSourcePlayer = true;
				info->hasMap = true;
				info->hasScoreboardMetricName = true;
				info->hasScoreboardRank = true;
			xcase EventType_UGCProjectCompleted:
				info->pchDescription = "Sends when a UGC mission that is featured is turned in";
				info->hasUGCProjectData = true;
				info->hasSourcePlayer = true;
			xcase EventType_GroupProjectTaskCompleted:
				info->pchDescription = "Sends when a GroupProject task is completed";
				info->hasSourcePlayer = true;
				info->hasMap = true;
			xcase EventType_AllegianceSet:
				info->pchDescription = "Sends when a player's allegiance changes";
				info->hasSourcePlayer = true;
				info->hasMap = true;
			xcase EventType_UGCAccountChanged:
				info->pchDescription = "Sends when a UGC account has changed for an active player";
				info->hasSourcePlayer = true;
			xdefault:
				devassertmsg(0, "Programmer Error: New GameEvent type added without adding a GameEventTypeInfo!");
				break;
		}
	}
}

const GameEventTypeInfo* gameevent_GetTypeInfo(EventType type)
{
	if (type >= 0 && type < EventType_Count)
		return &(s_GameEventTypeInfo[type]);
	return NULL;
}


//-------------------------------------------------------------------------
// Functions to read and print GameEvents to strings
//-------------------------------------------------------------------------

static void prettyprintPrintSourceCritters(GameEvent *ev, char **estrBuffer)
{
	if(ev->pchSourceEncounterName)
	{
		estrConcatf(estrBuffer, "%s.%s", ev->pchSourceEncounterName, ev->pchSourceActorName ? ev->pchSourceActorName : "");
	}
	else if(ev->pchSourceCritterName)  // Having both makes little sense to me
	{
		estrConcatf(estrBuffer, "%s", ev->pchSourceCritterName);
	}
	else if(ev->tMatchSource == TriState_Yes)
	{
		estrConcatf(estrBuffer, "Player");
	}
	else if(ev->tMatchSourceTeam == TriState_Yes)
	{
		estrConcatf(estrBuffer, "Team");
	}
}

static void prettyprintPrintTargetCritters(GameEvent *ev, char **estrBuffer)
{
	if(ev->pchTargetEncounterName)
	{
		estrConcatf(estrBuffer, "%s.%s", ev->pchTargetEncounterName, ev->pchTargetActorName ? ev->pchTargetActorName : "");
	}
	else if(ev->pchTargetCritterName)  // Having both makes little sense to me
	{
		estrConcatf(estrBuffer, "%s", ev->pchTargetCritterName);
	}
	else if(ev->tMatchTarget == TriState_Yes)
	{
		estrConcatf(estrBuffer, "Player");
	}
	else if(ev->tMatchTargetTeam == TriState_Yes)
	{
		estrConcatf(estrBuffer, "Team");
	}
}

void prettyprintHealthState(GameEvent *ev, char **estrBuffer)
{
	int min = -1, max = -1;

	prettyprintPrintSourceCritters(ev, estrBuffer);
	
#define XCASE_HEALTHSTATE(n, x) xcase HealthState_##n##_to_##x: { min = n; max = x; }
	switch(ev->healthState)
	{
		XCASE_HEALTHSTATE(0, 25);
		XCASE_HEALTHSTATE(0, 33);
		XCASE_HEALTHSTATE(0, 50);
		XCASE_HEALTHSTATE(0, 100);
		XCASE_HEALTHSTATE(25, 50);
		XCASE_HEALTHSTATE(33, 67);
		XCASE_HEALTHSTATE(50, 75);
		XCASE_HEALTHSTATE(50, 100);
		XCASE_HEALTHSTATE(75, 100);
		XCASE_HEALTHSTATE(67, 100);
	}
	estrConcatf(estrBuffer, ".%d-%d", min, max);
#undef XCASE_HEALTHSTATE
}

static void prettyprintKills(GameEvent *ev, char **estrBuffer)
{
	prettyprintPrintSourceCritters(ev, estrBuffer);

	estrConcatf(estrBuffer, ".killed.");

	prettyprintPrintTargetCritters(ev, estrBuffer);
}

static void prettyprintAssists(GameEvent *ev, char **estrBuffer)
{
	prettyprintPrintSourceCritters(ev, estrBuffer);

	estrConcatf(estrBuffer, ".AssistedInKilling.");

	prettyprintPrintTargetCritters(ev, estrBuffer);
}

static void prettyprintPVPEvents(GameEvent *ev, char **estrBuffer)
{
	prettyprintPrintSourceCritters(ev, estrBuffer);

	switch(ev->ePvPEvent)
	{
	case PvPEvent_CTF_FlagCaptured:
		estrConcatf(estrBuffer, ".CapturedFlag.");
		break;
	case PvPEvent_CTF_FlagDrop:
		estrConcatf(estrBuffer, ".DroppedFlag.");
		break;
	case PvPEvent_CTF_FlagReturned:
		estrConcatf(estrBuffer, ".ReturnedFlag.");
		break;
	}

	prettyprintPrintTargetCritters(ev, estrBuffer);
}

static void prettyprintVolume(GameEvent *ev, char **estrBuffer)
{
	estrPrintf(estrBuffer, "%s", ev->pchVolumeName);
}

static void prettyprintVolumeEntered(GameEvent *ev, char **estrBuffer)
{
	prettyprintVolume(ev, estrBuffer);

	estrConcatf(estrBuffer, ".Entered");
}

static void prettyprintVolumeExited(GameEvent *ev, char **estrBuffer)
{
	prettyprintVolume(ev, estrBuffer);

	estrConcatf(estrBuffer, ".Exited");
}

static void prettyprintContactDialogStart(GameEvent *ev, char **estrBuffer)
{
	estrPrintf(estrBuffer, "%s.Start", ev->pchContactName);
}

static void prettyprintContactDialogComplete(GameEvent *ev, char **estrBuffer)
{
	estrPrintf(estrBuffer, "%s.Complete", ev->pchContactName);
}

static void prettyprintInteract(GameEvent *ev, char **estrBuffer)
{
	if (ev->pchClickableName)
		estrPrintf(estrBuffer, "Interact.%s", ev->pchClickableName);
	else if (ev->pchTargetCritterName)
		estrPrintf(estrBuffer, "Interact.%s", ev->pchTargetCritterName);
	else if (ev->pchTargetActorName)
		estrPrintf(estrBuffer, "Interact.%s", ev->pchTargetActorName);
	else
		estrPrintf(estrBuffer, "Interact");
}

static void prettyprintClickableActive(GameEvent *ev, char **estrBuffer)
{
	prettyprintInteract(ev, estrBuffer);
	estrConcatf(estrBuffer, ".Active");
}

static void prettyprintInteractFailure(GameEvent *ev, char **estrBuffer)
{
	prettyprintInteract(ev, estrBuffer);
	estrConcatf(estrBuffer, ".Failure");
}

static void prettyprintInteractBegin(GameEvent *ev, char **estrBuffer)
{
	prettyprintInteract(ev, estrBuffer);
	estrConcatf(estrBuffer, ".Begin");
}

static void prettyprintInteractSuccess(GameEvent *ev, char **estrBuffer)
{
	prettyprintInteract(ev, estrBuffer);
	estrConcatf(estrBuffer, ".Success");
}

static void prettyprintInteractEndActive(GameEvent *ev, char **estrBuffer)
{
	prettyprintInteract(ev, estrBuffer);
	estrConcatf(estrBuffer, ".EndActive");
}

static void prettyprintInteractInterrupted(GameEvent *ev, char **estrBuffer)
{
	prettyprintInteract(ev, estrBuffer);
	estrConcatf(estrBuffer, ".Interrupted");
}

static void prettyprintMissionState(GameEvent *ev, char **estrBuffer)
{
	estrConcatf(estrBuffer, "%s.", ev->pchMissionRefString);
	switch(ev->missionState)
	{
		xcase MissionState_Succeeded: {
			estrConcatf(estrBuffer, "Succeeded");
		}
		xcase MissionState_Failed: {
			estrConcatf(estrBuffer, "Failed");
		}
		xcase MissionState_InProgress: {
			estrConcatf(estrBuffer, "InProgress");
		}
		xcase MissionState_TurnedIn: {
			estrConcatf(estrBuffer, "TurnedIn");
		}
	}
}

void gameevent_PrettyPrint(GameEvent *ev, char **estrBuffer)
{
	if(ev)
	{
		switch(ev->type)
		{
			xcase EventType_HealthState: {
				prettyprintHealthState(ev, estrBuffer);
			}
			
			xcase EventType_Kills: {
				prettyprintKills(ev, estrBuffer);
			}

			xcase EventType_Assists: {
				prettyprintAssists(ev, estrBuffer);
			}

			xcase EventType_PvPEvent: {
				prettyprintPVPEvents(ev, estrBuffer);
			}

			xcase EventType_VolumeEntered: {
				prettyprintVolumeEntered(ev, estrBuffer);
			}

			xcase EventType_VolumeExited: {
				prettyprintVolumeExited(ev, estrBuffer);
			}

			xcase EventType_ContactDialogStart: {
				prettyprintContactDialogStart(ev, estrBuffer);
			}
				
			xcase EventType_ContactDialogComplete: {
				prettyprintContactDialogComplete(ev, estrBuffer);
			}

			xcase EventType_ClickableActive: {
				prettyprintClickableActive(ev, estrBuffer);
			}

			xcase EventType_InteractFailure: {
				prettyprintInteractFailure(ev, estrBuffer);
			}

			xcase EventType_InteractSuccess: {
				prettyprintInteractSuccess(ev, estrBuffer);
			}
		
			xcase EventType_InteractBegin: {
				prettyprintInteractBegin(ev, estrBuffer);
			}
			
			xcase EventType_InteractEndActive: {
				prettyprintInteractEndActive(ev, estrBuffer);
			}

			xcase EventType_InteractInterrupted: {
				prettyprintInteractInterrupted(ev, estrBuffer);
			}

			xcase EventType_MissionState: {
				prettyprintMissionState(ev, estrBuffer);
			}

			xcase EventType_CutsceneEnd: {
				estrPrintf(estrBuffer, "CutsceneEnd.%s", ev->pchCutsceneName);
			}
			xcase EventType_CutsceneStart: {
				estrPrintf(estrBuffer, "CutsceneStart.%s", ev->pchCutsceneName);
			}
			xcase EventType_VideoStarted: {
				estrPrintf(estrBuffer, "VideoStarted.%s", ev->pchVideoName);
			}
			xcase EventType_VideoEnded: {
				estrPrintf(estrBuffer, "VideoEnded.%s", ev->pchVideoName);
			}
			xcase EventType_PlayerSpawnIn: {
				estrPrintf(estrBuffer, "PlayerSpawnIn");
			}

			xdefault: {
				estrPrintf(estrBuffer, "Not supported");
			}
		}
	}
}

void gameevent_WriteEvent(GameEvent *ev, char **estrBuffer)
{
	// for some reason piTargetCritterTags and piSourceCritterTags get set to an array size 1 with a NULL tag.
	// this seems hacky, but I'm doing a quick and dirty fix-up to the fields here.
	// ideally it would be nice to find and fix why the field gets populated on its own
	if (eaiSize(&ev->piTargetCritterTags) == 1 && ev->piTargetCritterTags[0] == 0)
		eaiDestroy(&ev->piTargetCritterTags);
	if (eaiSize(&ev->piSourceCritterTags) == 1 && ev->piSourceCritterTags[0] == 0)
		eaiDestroy(&ev->piSourceCritterTags);

	ParserWriteText(estrBuffer, parse_GameEvent, ev, 0, 0, 0);
}

void gameevent_WriteEventEscaped(GameEvent *ev, char **estrBuffer)
{
	static char *temp = NULL;

	PERFINFO_AUTO_START_FUNC();

	gameevent_WriteEvent(ev, &temp);
	estrClear(estrBuffer);
	estrAppendEscaped(estrBuffer, temp);
	estrClear(&temp);

	PERFINFO_AUTO_STOP();
}

void gameevent_WriteEventSingleLine(GameEvent *ev, char **estrBuffer)
{
	PERFINFO_AUTO_START_FUNC();
	estrClear(estrBuffer);
	gameevent_WriteEvent(ev, estrBuffer);
	estrReplaceOccurrences(estrBuffer, "\r", "");
	estrReplaceOccurrences(estrBuffer, "\n", " ");
	estrReplaceOccurrences(estrBuffer, "\t", " ");

	PERFINFO_AUTO_STOP();
}

bool gameevent_ReadEventEscaped(GameEvent *ev, const char *eventName)
{
	bool success = false;
	char *temp = NULL;
	estrStackCreate(&temp);
	estrAppendUnescaped(&temp, eventName);
	
	success = ParserReadText(temp, parse_GameEvent, ev, PARSER_NOERRORFSONPARSE);

	estrDestroy(&temp);
	return success;
}

GameEvent *gameevent_EventFromString(const char *eventName)
{
	GameEvent *ev = NULL;
	bool failed = false;

	if (!eventName)
		return NULL;

	// Allocate a new event
	ev = StructCreate(parse_GameEvent);
	
	if (ParserReadText(eventName, parse_GameEvent, ev, PARSER_NOERRORFSONPARSE))
	{
		// This was a GameEvent struct written with TextParser
	}
	else if(gameevent_ReadEventEscaped(ev, eventName))
	{
		// This was a GameEvent struct written with TextParser, then estring escaped
	}
	else
	{
		// Support for old Event strings
		char *eventType = strdup(eventName);
		char **args = NULL;
		int argc = 0;
		int i, n;

		// Separate the arguments out
		n = (int)strlen(eventType);
		for (i = 0; i < n; i++)
		{
			if (eventType[i] == '.')
			{
				eventType[i] = 0;
				if (eventType[i+1])
				{
					eaPush(&args, &eventType[i+1]);
					argc++;
				}
			}
		}

		ev->eSourceRegionType = -1;
		ev->eTargetRegionType = -1;

		// Determine the type of event
		if (!stricmp(eventType, "clickableActive"))
		{
			ev->type = EventType_ClickableActive;
			if (argc == 1)
				ev->pchClickableName = StructAllocString(args[0]);
			else
				failed = true;
		}
		else if (!stricmp(eventType, "clickableInteract"))
		{
			ev->type = EventType_InteractSuccess;
			if (argc == 1)
				ev->pchClickableName = StructAllocString(args[0]);
			else
				failed = true;
		}
		else if (!stricmp(eventType, "myClickableInteract"))
		{
			ev->type = EventType_InteractSuccess;
			ev->tMatchSource = TriState_Yes;
			if (argc == 1)
				ev->pchClickableName = StructAllocString(args[0]);
			else
				failed = true;
		}
		else if (!stricmp(eventType, "teamClickableInteract"))
		{
			ev->type = EventType_InteractSuccess;
			ev->tMatchSourceTeam = TriState_Yes;
			if (argc == 1)
				ev->pchClickableName = StructAllocString(args[0]);
			else
				failed = true;
		}
		else if (!stricmp(eventType, "clickableFailure"))
		{
			ev->type = EventType_InteractFailure;
			if (argc == 1)
				ev->pchClickableName = StructAllocString(args[0]);
			else
				failed = true;
		}
		else if (!stricmp(eventType, "myClickableFailure"))
		{
			ev->type = EventType_InteractFailure;
			ev->tMatchSource = TriState_Yes;
			if (argc == 1)
				ev->pchClickableName = StructAllocString(args[0]);
			else
				failed = true;
		}
		else if (!stricmp(eventType, "teamClickableFailure"))
		{
			ev->type = EventType_InteractFailure;
			ev->tMatchSourceTeam = TriState_Yes;
			if (argc == 1)
				ev->pchClickableName = StructAllocString(args[0]);
			else
				failed = true;
		}
		else if (!stricmp(eventType, "globalContactDialogStart"))
		{
			ev->type = EventType_ContactDialogStart;
			if (argc == 1)
				ev->pchContactName = allocAddString(args[0]);
			else
				failed = true;
		}
		else if (!stricmp(eventType, "globalContactDialogComplete"))
		{
			ev->type = EventType_ContactDialogComplete;
			if (argc == 1)
				ev->pchContactName = allocAddString(args[0]);
			else
				failed = true;
		}
		else if (!stricmp(eventType, "actorDeaths"))
		{
			ev->type = EventType_Kills;
			if (argc == 1)
				ev->pchTargetActorName = allocAddString(args[0]);
			else
				failed = true;
		}
		else if (!stricmp(eventType, "critterDeaths"))
		{
			ev->type = EventType_Kills;
			if (argc == 1)
				ev->pchTargetCritterName = allocAddString(args[0]);
			else if (argc != 0)
				failed = true;
		}
		else if (!stricmp(eventType, "encounterActorDeaths"))
		{
			ev->type = EventType_Kills;
			if (argc == 2)
			{
				ev->pchTargetEncounterName = allocAddString(args[0]);
				ev->pchTargetActorName = allocAddString(args[1]);
			}
			else
				failed = true;
		}
		else if (!stricmp(eventType, "encounterCritterDeaths"))
		{
			ev->type = EventType_Kills;
			if (argc == 2)
			{
				ev->pchTargetEncounterName = allocAddString(args[0]);
				ev->pchTargetCritterName = allocAddString(args[1]);
			}
			else
				failed = true;
		}
		else if (!stricmp(eventType, "staticEncounterActorDeaths"))
		{
			ev->type = EventType_Kills;
			if (argc == 2)
			{
				ev->pchTargetStaticEncName = allocAddString(args[0]);
				ev->pchTargetActorName = allocAddString(args[1]);
			}
			else
				failed = true;
		}
		else if (!stricmp(eventType, "staticEncounterCritterDeaths"))
		{
			ev->type = EventType_Kills;
			if (argc == 2)
			{
				ev->pchTargetStaticEncName = allocAddString(args[0]);
				ev->pchTargetCritterName = allocAddString(args[1]);
			}
			else
				failed = true;
		}
		else if (!strnicmp(eventType, "actorState", strlen("actorState")))
		{
			char *fsm = eventType + strlen("actorState");
			ev->type = EventType_FSMState;
			if (fsm[0] && argc == 1)
			{
				ev->pchSourceActorName = allocAddString(args[0]);
				ev->pchFsmStateName = StructAllocString(fsm);
			}
			else
				failed = true;
		}
		else if (!strnicmp(eventType, "critterState", strlen("critterState")))
		{
			char *fsm = eventType + strlen("critterState");
			ev->type = EventType_FSMState;
			if (fsm[0] && argc == 1)
			{
				ev->pchSourceCritterName = allocAddString(args[0]);
				ev->pchFsmStateName = StructAllocString(fsm);
			}
			else
				failed = true;
		}
		else if (!strnicmp(eventType, "encounterActorState", strlen("encounterActorState")))
		{
			char *fsm = eventType + strlen("encounterActorState");
			ev->type = EventType_FSMState;
			if (fsm[0] && argc == 2)
			{
				ev->pchSourceEncounterName = allocAddString(args[0]);
				ev->pchSourceActorName = allocAddString(args[1]);
				ev->pchFsmStateName = StructAllocString(fsm);
			}
			else
				failed = true;
		}
		else if (!strnicmp(eventType, "encounterCritterState", strlen("encounterCritterState")))
		{
			char *fsm = eventType + strlen("encounterCritterState");
			ev->type = EventType_FSMState;
			if (fsm[0] && argc == 2)
			{
				ev->pchSourceEncounterName = allocAddString(args[0]);
				ev->pchSourceCritterName = allocAddString(args[1]);
				ev->pchFsmStateName = StructAllocString(fsm);
			}
			else
				failed = true;
		}
		else if (!strnicmp(eventType, "staticEncounterActorState", strlen("staticEncounterActorState")))
		{
			char *fsm = eventType + strlen("staticEncounterActorState");
			ev->type = EventType_FSMState;
			if (fsm[0] && argc == 2)
			{
				ev->pchSourceStaticEncName = allocAddString(args[0]);
				ev->pchSourceActorName = allocAddString(args[1]);
				ev->pchFsmStateName = StructAllocString(fsm);
			}
			else
				failed = true;
		}
		else if (!strnicmp(eventType, "staticEncounterCritterState", strlen("staticEncounterCritterState")))
		{
			char *fsm = eventType + strlen("staticEncounterCritterState");
			ev->type = EventType_FSMState;
			if (fsm[0] && argc == 2)
			{
				ev->pchSourceStaticEncName = allocAddString(args[0]);
				ev->pchSourceCritterName = allocAddString(args[1]);
				ev->pchFsmStateName = StructAllocString(fsm);
			}
			else
				failed = true;
		}
		else if (!stricmp(eventType, "cutsceneStart"))
		{
			ev->type = EventType_CutsceneStart;
			if (argc == 1)
				ev->pchCutsceneName = StructAllocString(args[0]);
			else
				failed = true;
		}
		else if (!stricmp(eventType, "cutsceneEnd"))
		{
			ev->type = EventType_CutsceneEnd;
			if (argc == 1)
				ev->pchCutsceneName = StructAllocString(args[0]);
			else
				failed = true;
		}
		else if (!stricmp(eventType, "myActorDeaths"))
		{
			ev->type = EventType_Kills;
			if (argc == 1)
			{
				ev->pchTargetActorName = allocAddString(args[0]);
				ev->tMatchTarget = TriState_Yes;
			}
			else
				failed = true;
		}
		else if (!stricmp(eventType, "myCritterDeaths"))
		{
			ev->type = EventType_Kills;
			if (argc == 1)
			{
				ev->pchTargetCritterName = allocAddString(args[0]);
				ev->tMatchTarget = TriState_Yes;
			}
			else
				failed = true;
		}
		else if (!stricmp(eventType, "myDeaths"))
		{
			ev->type = EventType_Kills;
			ev->tMatchTarget = TriState_Yes;
			if (argc != 0)
				failed = true;
		}
		else if (!strnicmp(eventType, "myActorState", strlen("myActorState")))
		{
			char *fsm = eventType + strlen("myActorState");
			ev->type = EventType_FSMState;
			if (fsm[0] && argc == 1)
			{
				ev->pchSourceActorName = allocAddString(args[0]);
				ev->pchFsmStateName = StructAllocString(fsm);
				ev->tMatchSource = TriState_Yes;
			}
			else
				failed = true;
		}
		else if (!strnicmp(eventType, "myCritterState", strlen("myCritterState")))
		{
			char *fsm = eventType + strlen("myCritterState");
			ev->type = EventType_FSMState;
			if (fsm[0] && argc == 1)
			{
				ev->pchSourceCritterName = allocAddString(args[0]);
				ev->pchFsmStateName = StructAllocString(fsm);
				ev->tMatchSource = TriState_Yes;
			}
			else
				failed = true;
		}
		else if (!stricmp(eventType, "myActorInteract"))
		{
			ev->type = EventType_InteractSuccess;
			if (argc == 1)
			{
				ev->pchTargetActorName = allocAddString(args[0]);
				ev->tMatchTarget = TriState_Yes;
			}
			else
				failed = true;
		}
		else if (!stricmp(eventType, "myCritterInteract"))
		{
			ev->type = EventType_InteractSuccess;
			if (argc == 1)
			{
				ev->pchTargetCritterName = allocAddString(args[0]);
				ev->tMatchTarget = TriState_Yes;
			}
			else
				failed = true;
		}
		else if (!stricmp(eventType, "teamCritterInteract"))
		{
			ev->type = EventType_InteractSuccess;
			if (argc == 1)
			{
				ev->pchTargetCritterName = allocAddString(args[0]);
				ev->tMatchSourceTeam = TriState_Yes;
			}
			else
				failed = true;
		}
		else if (!stricmp(eventType, "myActorDialogStart"))
		{
			ev->type = EventType_ContactDialogStart;
			if (argc == 1)
			{
				ev->pchTargetActorName = allocAddString(args[0]);
				ev->tMatchTarget = TriState_Yes;
			}
			else
				failed = true;
		}
		else if (!stricmp(eventType, "myActorDialogComplete"))
		{
			ev->type = EventType_ContactDialogComplete;
			if (argc == 1)
			{
				ev->pchTargetActorName = allocAddString(args[0]);
				ev->tMatchTarget = TriState_Yes;
			}
			else
				failed = true;
		}
		else if (!stricmp(eventType, "myCritterDialogStart"))
		{
			ev->type = EventType_ContactDialogStart;
			if (argc == 1)
			{
				ev->pchSourceCritterName = allocAddString(args[0]);
				ev->tMatchTarget = TriState_Yes;
			}
			else
				failed = true;
		}
		else if (!stricmp(eventType, "myCritterDialogComplete"))
		{
			ev->type = EventType_ContactDialogComplete;
			if (argc == 1)
			{
				ev->pchSourceCritterName = allocAddString(args[0]);
				ev->tMatchTarget = TriState_Yes;
			}
			else
				failed = true;
		}
		else if (!stricmp(eventType, "encounterAsleep"))
		{
			ev->type = EventType_EncounterState;
			ev->encState = EncounterState_Asleep;
			if (argc == 1)
			{
				ev->pchTargetEncounterName = allocAddString(args[0]);
			}
			else
				failed = true;
		}
		else if (!stricmp(eventType, "encounterWaiting"))
		{
			ev->type = EventType_EncounterState;
			ev->encState = EncounterState_Waiting;
			if (argc == 1)
				ev->pchTargetEncounterName = allocAddString(args[0]);
			else
				failed = true;
		}
		else if (!stricmp(eventType, "encounterSpawned"))
		{
			ev->type = EventType_EncounterState;
			ev->encState = EncounterState_Spawned;
			if (argc == 1)
				ev->pchTargetEncounterName = allocAddString(args[0]);
			else
				failed = true;
		}
		else if (!stricmp(eventType, "encounterActive"))
		{
			ev->type = EventType_EncounterState;
			ev->encState = EncounterState_Active;
			if (argc == 1)
				ev->pchTargetEncounterName = allocAddString(args[0]);
			else
				failed = true;
		}
		else if (!stricmp(eventType, "encounterAware"))
		{
			ev->type = EventType_EncounterState;
			ev->encState = EncounterState_Aware;
			if (argc == 1)
				ev->pchTargetEncounterName = allocAddString(args[0]);
			else
				failed = true;
		}
		else if (!stricmp(eventType, "encounterSuccess"))
		{
			ev->type = EventType_EncounterState;
			ev->encState = EncounterState_Success;
			if (argc == 1)
				ev->pchTargetEncounterName = allocAddString(args[0]);
			else
				failed = true;
		}
		else if (!stricmp(eventType, "encounterFailure"))
		{
			ev->type = EventType_EncounterState;
			ev->encState = EncounterState_Failure;
			if (argc == 1)
				ev->pchTargetEncounterName = allocAddString(args[0]);
			else
				failed = true;
		}
		else if (!stricmp(eventType, "encounterOff"))
		{
			ev->type = EventType_EncounterState;
			ev->encState = EncounterState_Off;
			if (argc == 1)
				ev->pchTargetEncounterName = allocAddString(args[0]);
			else
				failed = true;
		}
		else if (!stricmp(eventType, "encounterComplete"))
		{
			ev->type = EventType_EncounterState;
			ev->encState = EncounterState_Success; // TODO - success OR failure?
			if (argc == 1)
				ev->pchTargetEncounterName = allocAddString(args[0]);
			else
				failed = true;
			// TODO - Error message?
		}
		else if (!stricmp(eventType, "staticEncounterAsleep"))
		{
			ev->type = EventType_EncounterState;
			ev->encState = EncounterState_Asleep;
			if (argc == 1)
				ev->pchTargetStaticEncName = allocAddString(args[0]);
			else
				failed = true;
		}
		else if (!stricmp(eventType, "staticEncounterWaiting"))
		{
			ev->type = EventType_EncounterState;
			ev->encState = EncounterState_Waiting;
			if (argc == 1)
				ev->pchTargetStaticEncName = allocAddString(args[0]);
			else
				failed = true;
		}
		else if (!stricmp(eventType, "staticEncounterSpawned"))
		{
			ev->type = EventType_EncounterState;
			ev->encState = EncounterState_Spawned;
			if (argc == 1)
				ev->pchTargetStaticEncName = allocAddString(args[0]);
			else
				failed = true;
		}
		else if (!stricmp(eventType, "staticEncounterActive"))
		{
			ev->type = EventType_EncounterState;
			ev->encState = EncounterState_Active;
			if (argc == 1)
				ev->pchTargetStaticEncName = allocAddString(args[0]);
			else
				failed = true;
		}
		else if (!stricmp(eventType, "staticEncounterAware"))
		{
			ev->type = EventType_EncounterState;
			ev->encState = EncounterState_Aware;
			if (argc == 1)
				ev->pchTargetStaticEncName = allocAddString(args[0]);
			else
				failed = true;
		}
		else if (!stricmp(eventType, "staticEncounterSuccess"))
		{
			ev->type = EventType_EncounterState;
			ev->encState = EncounterState_Success;
			if (argc == 1)
				ev->pchTargetStaticEncName = allocAddString(args[0]);
			else
				failed = true;
		}
		else if (!stricmp(eventType, "staticEncounterFailure"))
		{
			ev->type = EventType_EncounterState;
			ev->encState = EncounterState_Failure;
			if (argc == 1)
				ev->pchTargetStaticEncName = allocAddString(args[0]);
			else
				failed = true;
		}
		else if (!stricmp(eventType, "staticEncounterOff"))
		{
			ev->type = EventType_EncounterState;
			ev->encState = EncounterState_Off;
			if (argc == 1)
				ev->pchTargetStaticEncName = allocAddString(args[0]);
			else
				failed = true;
		}
		else if (!stricmp(eventType, "staticEncounterComplete"))
		{
			ev->type = EventType_EncounterState;
			ev->encState = EncounterState_Success; // TODO - success OR failure
			if (argc == 1)
				ev->pchTargetStaticEncName = allocAddString(args[0]);
			else
				failed = true;
			// TODO - Error message?
		}
		else if (!stricmp(eventType, "playersInRadius"))
		{
			failed = true;
		}
		else if (!stricmp(eventType, "playerDeaths"))
		{
			ev->type = EventType_Kills;
			ev->tTargetIsPlayer = TriState_Yes;
			if (argc != 0)
				failed = true;
		}
		else if (!stricmp(eventType, "missionAssigned"))
		{
			ev->type = EventType_MissionState;
			ev->missionState = MissionState_InProgress;
			if (argc == 1)
				ev->pchMissionRefString = allocAddString(args[0]);
			else
				failed = true;
		}
		else if (!stricmp(eventType, "missionSuccess"))
		{
			ev->type = EventType_MissionState;
			ev->missionState = MissionState_Succeeded;
			if (argc == 1)
				ev->pchMissionRefString = allocAddString(args[0]);
			else
				failed = true;
		}
		else if (!stricmp(eventType, "missionFailure"))
		{
			ev->type = EventType_MissionState;
			ev->missionState = MissionState_Failed;
			if (argc == 1)
				ev->pchMissionRefString = allocAddString(args[0]);
			else
				failed = true;
		}
		else if (!stricmp(eventType, "myMissionSuccess"))
		{
			ev->type = EventType_MissionState;
			ev->missionState = MissionState_Succeeded;
			if (argc == 1)
			{
				ev->pchMissionRefString = allocAddString(args[0]);
				ev->tMatchSource = TriState_Yes;
			}
			else
				failed = true;
		}
		else if (!stricmp(eventType, "myMissionFailure"))
		{
			ev->type = EventType_MissionState;
			ev->missionState = MissionState_Failed;
			if (argc == 1)
			{
				ev->pchMissionRefString = allocAddString(args[0]);
				ev->tMatchSource = TriState_Yes;
			}
			else
				failed = true;
		}
		else if (!stricmp(eventType, "objectDestroyed"))
		{
			ev->type = EventType_Kills;
			if (argc == 1)
				ev->pchTargetObjectName = allocAddString(args[0]);
			else if (argc != 0)
				failed = true;
		}
		else if (!stricmp(eventType, "dmgDealt"))
		{
			ev->type = EventType_Damage;
			ev->tMatchSource = TriState_Yes;
			if (argc != 0)
				failed = true;
		}
		else if (!stricmp(eventType, "dmgTaken"))
		{
			ev->type = EventType_Damage;
			ev->tMatchTarget = TriState_Yes;
			if (argc != 0)
				failed = true;
		}
		else if (!stricmp(eventType, "actorInteract"))
		{
			ev->type = EventType_InteractSuccess;
			if (argc == 1)
			{
				ev->pchTargetActorName = allocAddString(args[0]);
				ev->tMatchSource = TriState_Yes;
			}
			else
				failed = true;
		}
		else if (!stricmp(eventType, "contactInteract"))
		{
			ev->type = EventType_InteractSuccess;
			if (argc == 1)
			{
				ev->pchContactName = allocAddString(args[0]);
				ev->tMatchSource = TriState_Yes;
			}
			else
				failed = true;
		}
		else if (!stricmp(eventType, "critterInteract"))
		{
			ev->type = EventType_InteractSuccess;
			if (argc == 1)
			{
				ev->pchTargetCritterName = allocAddString(args[0]);
				ev->tMatchSource = TriState_Yes;
			}
			else
				failed = true;
		}
		else if (!stricmp(eventType, "actorDialogStart"))
		{
			ev->type = EventType_ContactDialogStart;
			if (argc == 1)
			{
				ev->pchTargetActorName = allocAddString(args[0]);
				ev->tMatchSource = TriState_Yes;
			}
			else
				failed = true;
		}
		else if (!stricmp(eventType, "actorDialogComplete"))
		{
			ev->type = EventType_ContactDialogComplete;
			if (argc == 1)
			{
				ev->pchTargetActorName = allocAddString(args[0]);
				ev->tMatchSource = TriState_Yes;
			}
			else
				failed = true;
		}
		else if (!stricmp(eventType, "contactDialogStart"))
		{
			ev->type = EventType_ContactDialogStart;
			if (argc == 1)
			{
				ev->pchContactName = allocAddString(args[0]);
				ev->tMatchSource = TriState_Yes;
			}
			else
				failed = true;
		}
		else if (!stricmp(eventType, "contactDialogComplete"))
		{
			ev->type = EventType_ContactDialogComplete;
			if (argc == 1)
			{
				ev->pchContactName = allocAddString(args[0]);
				ev->tMatchSource = TriState_Yes;
			}
			else
				failed = true;
		}
		else if (!stricmp(eventType, "critterDialogStart"))
		{
			ev->type = EventType_ContactDialogStart;
			if (argc == 1)
			{
				ev->pchTargetCritterName = allocAddString(args[0]);
				ev->tMatchSource = TriState_Yes;
			}
			else
				failed = true;
		}
		else if (!stricmp(eventType, "critterDialogComplete"))
		{
			ev->type = EventType_ContactDialogComplete;
			if (argc == 1)
			{
				ev->pchTargetCritterName = allocAddString(args[0]);
				ev->tMatchSource = TriState_Yes;
			}
			else
				failed = true;
		}
		else if (!stricmp(eventType, "playerItemGained"))
		{
			ev->type = EventType_ItemGained;
			if (argc == 1)
			{
				ev->pchItemName = allocAddString(args[0]);
				ev->tMatchSource = TriState_Yes;
			}
			else
				failed = true;
		}
		else if (!stricmp(eventType, "playerItemLost"))
		{
			ev->type = EventType_ItemLost;
			if (argc == 1)
			{
				ev->pchItemName = allocAddString(args[0]);
				ev->tMatchSource = TriState_Yes;
			}
			else
				failed = true;
		}
		else if (!stricmp(eventType, "myActorKills"))
		{
			ev->type = EventType_Kills;
			if (argc == 1)
			{
				ev->pchTargetActorName = allocAddString(args[0]);
				ev->tMatchSource = TriState_Yes;
			}
			else
				failed = true;
		}
		else if (!stricmp(eventType, "myCritterKills"))
		{
			ev->type = EventType_Kills;
			ev->tMatchSource = TriState_Yes;
			if (argc == 1)
			{
				ev->pchTargetCritterName = allocAddString(args[0]);
			}
			else if (argc != 0)
				failed = true;
		}
		else if (!stricmp(eventType, "myEncounterActorKills"))
		{
			ev->type = EventType_Kills;
			if (argc == 2)
			{
				ev->pchTargetEncounterName = allocAddString(args[0]);
				ev->pchTargetActorName = allocAddString(args[1]);
				ev->tMatchSource = TriState_Yes;
			}
			else
				failed = true;
		}
		else if (!stricmp(eventType, "myEncounterCritterKills"))
		{
			ev->type = EventType_Kills;
			if (argc == 2)
			{
				ev->pchTargetEncounterName = allocAddString(args[0]);
				ev->pchTargetCritterName = allocAddString(args[1]);
				ev->tMatchSource = TriState_Yes;
			}
			else
				failed = true;
		}
		else if (!stricmp(eventType, "myStaticEncounterActorKills"))
		{
			ev->type = EventType_Kills;
			if (argc == 2)
			{
				ev->pchTargetStaticEncName = allocAddString(args[0]);
				ev->pchTargetActorName = allocAddString(args[1]);
				ev->tMatchSource = TriState_Yes;
			}
			else
				failed = true;
		}
		else if (!stricmp(eventType, "myStaticEncounterCritterKills"))
		{
			ev->type = EventType_Kills;
			if (argc == 2)
			{
				ev->pchTargetStaticEncName = allocAddString(args[0]);
				ev->pchTargetCritterName = allocAddString(args[1]);
				ev->tMatchSource = TriState_Yes;
			}
			else
				failed = true;
		}
		else if (!stricmp(eventType, "myObjectDestroyed"))
		{
			ev->type = EventType_Kills;
			ev->tMatchSource = TriState_Yes;
			if (argc == 1)
				ev->pchTargetObjectName = allocAddString(args[0]);
			else if (argc != 0)
				failed = true;
		}
		else if (!stricmp(eventType, "teamDeaths"))
		{
			ev->type = EventType_Kills;
			if (argc == 0)
			{
				ev->tMatchTargetTeam = TriState_Yes;
			}
			else
				failed = true;
		}
		else if (!stricmp(eventType, "teamActorKills"))
		{
			ev->type = EventType_Kills;
			if (argc == 1)
			{
				ev->pchTargetActorName = allocAddString(args[0]);
				ev->tMatchSourceTeam = TriState_Yes;
			}
			else
				failed = true;
		}
		else if (!stricmp(eventType, "teamCritterKills"))
		{
			ev->type = EventType_Kills;
			ev->tMatchSourceTeam = TriState_Yes;
			if (argc == 1)
			{
				ev->pchTargetCritterName = allocAddString(args[0]);
			}
			else if (argc != 0)
				failed = true;
		}
		else if (!stricmp(eventType, "teamEncounterActorKills"))
		{
			ev->type = EventType_Kills;
			if (argc == 2)
			{
				ev->pchTargetEncounterName = allocAddString(args[0]);
				ev->pchTargetActorName = allocAddString(args[1]);
				ev->tMatchSourceTeam = TriState_Yes;
			}
			else
				failed = true;
		}
		else if (!stricmp(eventType, "teamEncounterCritterKills"))
		{
			ev->type = EventType_Kills;
			if (argc == 2)
			{
				ev->pchTargetEncounterName = allocAddString(args[0]);
				ev->pchTargetCritterName = allocAddString(args[1]);
				ev->tMatchSourceTeam = TriState_Yes;
			}
			else
				failed = true;
		}
		else if (!stricmp(eventType, "teamStaticEncounterActorKills"))
		{
			ev->type = EventType_Kills;
			if (argc == 2)
			{
				ev->pchTargetStaticEncName = allocAddString(args[0]);
				ev->pchTargetActorName = allocAddString(args[1]);
				ev->tMatchSourceTeam = TriState_Yes;
			}
			else
				failed = true;
		}
		else if (!stricmp(eventType, "teamStaticEncounterCritterKills"))
		{
			ev->type = EventType_Kills;
			if (argc == 2)
			{
				ev->pchTargetStaticEncName = allocAddString(args[0]);
				ev->pchTargetCritterName = allocAddString(args[1]);
				ev->tMatchSourceTeam = TriState_Yes;
			}
			else
				failed = true;
		}
		else if (!stricmp(eventType, "teamObjectDestroyed"))
		{
			ev->type = EventType_Kills;
			ev->tMatchSourceTeam = TriState_Yes;
			if (argc == 1)
			{
				ev->pchTargetObjectName = allocAddString(args[0]);
			}
			else if (argc != 0)
				failed = true;
		}
		else if (!stricmp(eventType, "actorEnteredVolume"))
		{
			ev->type = EventType_VolumeEntered;
			if (argc == 2)
			{
				ev->pchVolumeName = StructAllocString(args[0]);
				ev->pchSourceActorName = allocAddString(args[1]);
			}
			else
				failed = true;
		}
		else if (!stricmp(eventType, "encounterActorEnteredVolume"))
		{
			ev->type = EventType_VolumeEntered;
			if (argc == 3)
			{
				ev->pchVolumeName = StructAllocString(args[0]);
				ev->pchSourceEncounterName = allocAddString(args[1]);
				ev->pchSourceActorName = allocAddString(args[2]);
			}
			else
				failed = true;
		}
		else if (!stricmp(eventType, "playerEnteredVolume"))
		{
			ev->type = EventType_VolumeEntered;
			if (argc == 1)
			{
				ev->pchVolumeName = StructAllocString(args[0]);
				ev->tSourceIsPlayer = TriState_Yes;
			}
			else
				failed = true;
		}
		else if (!stricmp(eventType, "myPlayerEnteredVolume"))
		{
			ev->type = EventType_VolumeEntered;
			if (argc == 1)
			{
				ev->pchVolumeName = StructAllocString(args[0]);
				ev->tSourceIsPlayer = TriState_Yes;
				ev->tMatchSource = TriState_Yes;
			}
			else
				failed = true;
		}
		else if (!stricmp(eventType, "staticEncounterActorEnteredVolume"))
		{
			ev->type = EventType_VolumeEntered;
			if (argc == 3)
			{
				ev->pchVolumeName = StructAllocString(args[0]);
				ev->pchSourceStaticEncName = allocAddString(args[1]);
				ev->pchSourceActorName = allocAddString(args[2]);
			}
			else
				failed = true;
		}
		else if (!stricmp(eventType, "actorExitedVolume"))
		{
			ev->type = EventType_VolumeExited;
			if (argc == 2)
			{
				ev->pchVolumeName = StructAllocString(args[0]);
				ev->pchSourceActorName = allocAddString(args[1]);
			}
			else
				failed = true;
		}
		else if (!stricmp(eventType, "encounterActorExitedVolume"))
		{
			ev->type = EventType_VolumeExited;
			if (argc == 3)
			{
				ev->pchVolumeName = StructAllocString(args[0]);
				ev->pchSourceEncounterName = allocAddString(args[1]);
				ev->pchSourceActorName = allocAddString(args[2]);
			}
			else
				failed = true;
		}
		else if (!stricmp(eventType, "playerExitedVolume"))
		{
			ev->type = EventType_VolumeExited;
			if (argc == 1)
			{
				ev->pchVolumeName = StructAllocString(args[0]);
				ev->tSourceIsPlayer = TriState_Yes;
			}
			else
				failed = true;
		}
		else if (!stricmp(eventType, "myPlayerExitedVolume"))
		{
			ev->type = EventType_VolumeExited;
			if (argc == 1)
			{
				ev->pchVolumeName = StructAllocString(args[0]);
				ev->tSourceIsPlayer = TriState_Yes;
				ev->tMatchSource = TriState_Yes;
			}
			else
				failed = true;
		}
		else if (!stricmp(eventType, "staticEncounterActorExitedVolume"))
		{
			ev->type = EventType_VolumeExited;
			if (argc == 3)
			{
				ev->pchVolumeName = StructAllocString(args[0]);
				ev->pchSourceStaticEncName = allocAddString(args[1]);
				ev->pchSourceActorName = allocAddString(args[2]);
			}
			else
				failed = true;
		}
		else if (!stricmp(eventType, "actorInVolume"))
		{
			failed = true;
		}
		else if (!stricmp(eventType, "encounterActorInVolume"))
		{
			failed = true;
		}
		else if (!stricmp(eventType, "playerInVolume"))
		{
			failed = true;
		}
		else if (!stricmp(eventType, "staticEncounterActorInVolume"))
		{
			failed = true;
		}
		else
		{
			failed = true;
		}
		free(eventType);
		eaDestroy(&args);
	}

	if (failed)
	{
		// Error: could not determine the type of event.
		StructDestroy(parse_GameEvent, ev);
		ev = NULL;
	}

	return ev;
}

//-------------------------------------------------------------------------
// Misc. GameEvent utilities and accessors
//-------------------------------------------------------------------------

// This copies a "Listening" gameevent quickly.  Speed is important since the Perks system has to
// copy these many times when a player enters a map.
static GameEvent* gameevent_FastCopyListener(const GameEvent *pSrc)
{
	GameEvent *ev = NULL;

	ev = StructAlloc(parse_GameEvent);

	devassertmsg(!eaSize(&pSrc->eaSources) && !eaSize(&pSrc->eaTargets), "Can't call gameevent_FastCopyListener on a non-listener GameEvent!");

	// Most of the fields should be pooled strings and ints, so memcpy should be fine for most of them
	memcpy(ev, pSrc, sizeof(GameEvent));

	// Have to NULL these first since we just memcpy'd
	ev->eaItemCategories = NULL;	
	ev->piSourceCritterTags = NULL;	
	ev->piTargetCritterTags = NULL;

	// don't copy the owned ChainEventDef, but put it in the unowned ChainEvent
	// so when this is later structDestroyed, it won't free the shared GameEvent
	if (ev->pChainEventDef && !ev->pChainEvent)
		ev->pChainEvent = ev->pChainEventDef;
	ev->pChainEventDef = NULL;		

	ea32Copy((U32**)&ev->eaItemCategories, (U32**)&pSrc->eaItemCategories);
	ea32Copy(&ev->piSourceCritterTags, &pSrc->piSourceCritterTags);
	ea32Copy(&ev->piTargetCritterTags, &pSrc->piTargetCritterTags);

	// Copy any strings that aren't Pooled Strings
	if (ev->pchClickableName) ev->pchClickableName = StructAllocString(ev->pchClickableName);
	if (ev->pchClickableGroupName) ev->pchClickableGroupName = StructAllocString(ev->pchClickableGroupName);
	if (ev->pchCutsceneName) ev->pchCutsceneName = StructAllocString(ev->pchCutsceneName);
	if (ev->pchVideoName) ev->pchVideoName = StructAllocString(ev->pchVideoName);
	if (ev->pchVolumeName) ev->pchVolumeName = StructAllocString(ev->pchVolumeName);
	if (ev->pchPowerName) ev->pchPowerName = StructAllocString(ev->pchPowerName);
	if (ev->pchPowerEventName) ev->pchPowerEventName = StructAllocString(ev->pchPowerEventName);
	if (ev->pchDamageType) ev->pchDamageType = StructAllocString(ev->pchDamageType);
	if (ev->pchDialogName) ev->pchDialogName = StructAllocString(ev->pchDialogName);
	if (ev->pchNemesisName) ev->pchNemesisName = StructAllocString(ev->pchNemesisName);
	if (ev->pchMessage) ev->pchMessage = StructAllocString(ev->pchMessage);
	if (ev->pchDoorKey) ev->pchDoorKey = StructAllocString(ev->pchDoorKey);
		
	
	return ev;
}

// Create a copy, but allow the above to stay static
GameEvent* gameevent_CopyListener(const GameEvent *pSrc)
{
	return gameevent_FastCopyListener(pSrc);
}

// If needed, this allocates a new copy of the GameEvent that is set up correctly for player-scoping
GameEvent* gameevent_SetupPlayerScopedEvent(GameEvent *defEvent, Entity *playerEnt)
{
	GameEvent* ev = NULL;
	PERFINFO_AUTO_START_FUNC();
	// If the event must be scoped to the player or team, copy the event
	// and fill in the entRef fields that it needs
	if (defEvent && (defEvent->tMatchSource || defEvent->tMatchSourceTeam))
	{
		if (playerEnt){
			ev = gameevent_FastCopyListener(defEvent);
			if(ev)	// Make static checker happy
				ev->sourceEntRef = playerEnt->myRef;
		} else {
			char *estrBuffer = NULL;
			estrStackCreate(&estrBuffer);
			gameevent_WriteEventEscaped(defEvent, &estrBuffer);
			Errorf("Error: No player present, cannot player-scope Event %s!", estrBuffer);
			estrDestroy(&estrBuffer);
		}
	}
	if (defEvent && (defEvent->tMatchTarget || defEvent->tMatchTargetTeam))
	{
		if (playerEnt){
			if (!ev)
				ev = gameevent_FastCopyListener(defEvent);
			if(ev)	// Make static checker happy
				ev->targetEntRef = playerEnt->myRef;
		} else {
			char *estrBuffer = NULL;
			estrStackCreate(&estrBuffer);
			gameevent_WriteEventEscaped(defEvent, &estrBuffer);
			Errorf("Error: No player present, cannot player-scope Event %s!", estrBuffer);
			estrDestroy(&estrBuffer);
		}
	}
	if (defEvent && defEvent->tMatchMapOwner){
		if (playerEnt){
			if (!ev)
				ev = gameevent_FastCopyListener(defEvent);
			if(ev)	// Make static checker happy
				ev->mapOwnerEntRef = playerEnt->myRef;
		} else {
			char *estrBuffer = NULL;
			estrStackCreate(&estrBuffer);
			gameevent_WriteEventEscaped(defEvent, &estrBuffer);
			Errorf("Error: No player present, cannot player-scope Event %s!", estrBuffer);
			estrDestroy(&estrBuffer);
		}
	}

	PERFINFO_AUTO_STOP();
	return ev;
}

bool gameevent_AreAssistsEnabled()
{
	return gConf.bEnableAssists && zmapInfoGetMapType(NULL) == ZMTYPE_PVP;
}


#include "GameEvent_h_ast.c"
#include "entEnums_h_ast.c"
#include "MinigameCommon_h_ast.c"
#include "mission_enums_h_ast.c"
#include "encounter_enums_h_ast.c"
