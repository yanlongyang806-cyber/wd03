/***************************************************************************
*     Copyright (c) 2006, Cryptic Studios
*     All Rights Reserved
*     Confidential Property of Cryptic Studios
***************************************************************************/

#include "EntDebugMenu.h"
#include "Entity.h"
#include "gslContact.h"
#include "gslEventTracker.h"
#include "gslInteractable.h"
#include "gslMission.h"
#include "gslMissionDebug.h"
#include "mission_common.h"
#include "NameList.h"
#include "Player.h"
#include "ResourceInfo.h"

#include "AutoGen/mission_common_h_ast.h"


// ----------------------------------------------------------------------------------
// Static Data
// ----------------------------------------------------------------------------------

FoundDoorStruct **s_eaDebugDoorList = NULL;
static NameList *s_pDoorNameList = NULL;


// ----------------------------------------------------------------------------------
// Mission Debug Menu
// ----------------------------------------------------------------------------------

// Sort missions by level first and by logical name second.
static int missiondebug_cmpMissionDefByLevel(const void *a, const void *b)
{
	const MissionDef* defA = *(MissionDef**)a;
	const MissionDef* defB = *(MissionDef**)b;
	int ret = defA->levelDef.missionLevel - defB->levelDef.missionLevel;

	if (0 == ret) {
		ret = stricmp(defA->name, defB->name);
	}
	return ret;
}


static void missiondebug_DebugMenuAddMissionComplete(DebugMenuItem *pMenuItem, Mission *pMission, int iDepth)
{
	MissionDef *pCurrDef = mission_GetDef(pMission);
	char cmdStr[512];
	char *estrTmpStr = NULL;
	int i, n;

	if (pCurrDef && (pMission->state == MissionState_InProgress || pMission->openChildren)) {
		estrCreate(&estrTmpStr);
		for (i = 0; i < iDepth; i++) {
			estrAppend2(&estrTmpStr, "  ");
		}
		estrAppend2(&estrTmpStr, GET_REF(pCurrDef->uiStringMsg.hMessage) ? TranslateMessageRef(pCurrDef->uiStringMsg.hMessage) : pCurrDef->name); // TODO - Use display names?
		sprintf(cmdStr, "missioncomplete %s", pCurrDef->pchRefString);
		debugmenu_AddNewCommand(pMenuItem, estrTmpStr, cmdStr);
		
		n = eaSize(&pMission->children);
		for (i = 0; i < n; i++) {
			missiondebug_DebugMenuAddMissionComplete(pMenuItem, pMission->children[i], iDepth+1);
		}
	}
}


void missiondebug_CreateDebugMenu(Entity *pPlayerEnt, DebugMenuItem *pGroupRoot)
{
	int i, n, iLevel;
	DebugMenuItem *pMenuItem, *pMissionMenu = NULL, *pMissionGroup;
	MissionInfo *pMissionInfo = mission_GetInfoFromPlayer(pPlayerEnt);
	MissionDef **eaSortedMissions = NULL;
	DictionaryEArrayStruct *pStruct = resDictGetEArrayStruct(g_MissionDictionary);
	char buf[1024];
	PlayerDebug *pDebug = entGetPlayerDebug(pPlayerEnt, false);

	PERFINFO_AUTO_START_FUNC();

	// All simple commands are first
	debugmenu_AddNewCommand(pGroupRoot, "Reset Mission Info", "missioninforeset");
	debugmenu_AddNewCommand(pGroupRoot, "Allow Repeated Missions", "repeatmissions");
	if (g_EventLogDebug) {
		debugmenu_AddNewCommand(pGroupRoot, "Disable Event Debugging", "eventlogdebug 0");
	} else {
		debugmenu_AddNewCommand(pGroupRoot, "Enable Event Debugging", "eventlogdebug 1");
	}
	if (pMissionInfo && pMissionInfo->showDebugInfo) {
		debugmenu_AddNewCommand(pGroupRoot, "Disable Mission Debug", "missiondebug 0");
	} else {
		debugmenu_AddNewCommand(pGroupRoot, "Enable Mission Debug", "missiondebug 1");
	}

	// Populating the mission lists can take a long time.  Don't do it by default
	debugmenu_AddNewCommand(pGroupRoot,
			(pDebug && pDebug->showMissionDebugMenu) ?
					"Hide mission debug lists" :
					"Show mission debug lists",
			(pDebug && pDebug->showMissionDebugMenu) ?
				"missionDebugMenuEnable 0$$debugmenu_close 10$$mmm" :
				"missionDebugMenuEnable 1$$debugmenu_close 10$$mmm");

	if (pDebug && pDebug->showMissionDebugMenu) {
		// Add new missions list
		// Create sorted list of all missions (maybe this should go elsewhere, or global mission list should always be sorted)
		n = eaSize(&pStruct->ppReferents);
		for(i=0; i<n; i++) {
			eaPush(&eaSortedMissions, (MissionDef*)pStruct->ppReferents[i]);
		}
		eaQSort(eaSortedMissions, missiondebug_cmpMissionDefByLevel);

		pMenuItem = debugmenu_AddNewCommandGroup(pGroupRoot, "Add Mission", "Attempts to grant a new mission to a player by name", false);


		// Add the missions to the menu groups by level
		iLevel = 0;
		for(i=0; i<n; i++) {
			MissionDef *pCurrDef = eaSortedMissions[i];
			char missionNameBuf[1024];
			char levelBuf[32];
			const char *pcDisplayName = TranslateMessageRef(pCurrDef->displayNameMsg.hMessage);

			if (pCurrDef->levelDef.missionLevel == 0) {
				ErrorFilenamef(pCurrDef->filename, "Mission %s is level zero", pCurrDef->name);
			}

			// If this mission is too high-level, add a new group to the menu
			if (iLevel == 0 || pCurrDef->levelDef.missionLevel > iLevel) {
				sprintf(buf, "Level %d - %d", iLevel + 1, iLevel + 5);
				pMissionMenu = debugmenu_AddNewCommandGroup(pMenuItem, buf, "See missions in this range", false);
				iLevel += 5;
			}

			if (pcDisplayName) {
				sprintf(missionNameBuf, "%s (%s)", pCurrDef->name, pcDisplayName);
			} else {
				sprintf(missionNameBuf, "%s", pCurrDef->name);
			}

			if (pCurrDef->levelDef.eLevelType == MissionLevelType_PlayerLevel) {
				sprintf(levelBuf, " (uses player level)");
			} else if(pCurrDef->levelDef.eLevelType == MissionLevelType_Specified) {
				sprintf(levelBuf, " %d", pCurrDef->levelDef.missionLevel);
			} else if(pCurrDef->levelDef.eLevelType == MissionLevelType_MapLevel) {
				sprintf(levelBuf, " (uses map level)");
			} else if(pCurrDef->levelDef.eLevelType == MissionLevelType_MapVariable) {
				sprintf(levelBuf, " (uses map var: %s)", pCurrDef->levelDef.pchLevelMapVar);
			} else {
				sprintf(levelBuf, " (uses unknown level type)");
			}

			strcat(missionNameBuf, levelBuf);

			sprintf(buf, "missionadd %s", pCurrDef->name);
			debugmenu_AddNewCommand(pMissionMenu, missionNameBuf, buf);
		}
		eaDestroy(&eaSortedMissions);
	}

	// Complete Missions list
	if (pMissionInfo) {
		DebugMenuItem *pPerkMenuItem = NULL;
		pMenuItem = debugmenu_AddNewCommandGroup(pGroupRoot, "Complete Mission/Task", "Complete a mission or sub-task", false);

		n = eaSize(&pMissionInfo->missions);
		for (i = 0; i < n; i++) {
			MissionDef *pDef = mission_GetDef(pMissionInfo->missions[i]);
			if (pDef && (pDef->missionType == MissionType_Normal
				|| pDef->missionType == MissionType_Nemesis 
				|| pDef->missionType == MissionType_NemesisArc 
				|| pDef->missionType == MissionType_NemesisSubArc 
				|| pDef->missionType == MissionType_Episode 
				|| pDef->missionType == MissionType_TourOfDuty
				|| pDef->missionType == MissionType_AutoAvailable)) // don't get Perks, Schemes
			{
				sprintf(buf, "%s", GET_REF(pDef->displayNameMsg.hMessage) ? TranslateMessageRef(pDef->displayNameMsg.hMessage) : pDef->name);
				pMissionGroup = debugmenu_AddNewCommandGroup(pMenuItem, buf, "Complete all or part of this mission", false);
				missiondebug_DebugMenuAddMissionComplete(pMissionGroup, pMissionInfo->missions[i], 0);
			}
		}

		n = eaSize(&pMissionInfo->eaNonPersistedMissions);
		for (i = 0; i < n; i++)	{
			MissionDef *pDef = mission_GetDef(pMissionInfo->eaNonPersistedMissions[i]);
			if (pDef && (pDef->missionType == MissionType_Normal
				|| pDef->missionType == MissionType_Nemesis 
				|| pDef->missionType == MissionType_NemesisArc
				|| pDef->missionType == MissionType_NemesisSubArc
				|| pDef->missionType == MissionType_Episode 
				|| pDef->missionType == MissionType_TourOfDuty
				|| pDef->missionType == MissionType_AutoAvailable)) // don't get Perks, Schemes
			{
				sprintf(buf, "%s", GET_REF(pDef->displayNameMsg.hMessage) ? TranslateMessageRef(pDef->displayNameMsg.hMessage) : pDef->name);
				pMissionGroup = debugmenu_AddNewCommandGroup(pMenuItem, buf, "Complete all or part of this mission", false);
				missiondebug_DebugMenuAddMissionComplete(pMissionGroup, pMissionInfo->eaNonPersistedMissions[i], 0);
			}
		}

		n = eaSize(&pMissionInfo->eaDiscoveredMissions);
		for (i = 0; i < n; i++) {
			MissionDef *pDef = mission_GetDef(pMissionInfo->eaDiscoveredMissions[i]);
			if (pDef && (pDef->missionType == MissionType_Normal
				|| pDef->missionType == MissionType_Nemesis 
				|| pDef->missionType == MissionType_NemesisArc
				|| pDef->missionType == MissionType_NemesisSubArc
				|| pDef->missionType == MissionType_Episode 
				|| pDef->missionType == MissionType_TourOfDuty
				|| pDef->missionType == MissionType_AutoAvailable)) // don't get Perks, Schemes
			{
				sprintf(buf, "%s", GET_REF(pDef->displayNameMsg.hMessage) ? TranslateMessageRef(pDef->displayNameMsg.hMessage) : pDef->name);
				pMissionGroup = debugmenu_AddNewCommandGroup(pMenuItem, buf, "Complete all or part of this mission", false);
				missiondebug_DebugMenuAddMissionComplete(pMissionGroup, pMissionInfo->eaDiscoveredMissions[i], 0);
			}
		}

		pPerkMenuItem = debugmenu_AddNewCommandGroup(pMenuItem, "Perks", "Complete a mission or sub-task for Perk missions", false);

		n = eaSize(&pMissionInfo->missions);
		for (i = 0; i < n; i++) {
			MissionDef *pDef = mission_GetDef(pMissionInfo->missions[i]);
			if (pDef && pDef->missionType == MissionType_Perk) {
				sprintf(buf, "%s", GET_REF(pDef->displayNameMsg.hMessage) ? TranslateMessageRef(pDef->displayNameMsg.hMessage) : pDef->name);
				pMissionGroup = debugmenu_AddNewCommandGroup(pPerkMenuItem, buf, "Complete all or part of this perk", false);
				missiondebug_DebugMenuAddMissionComplete(pMissionGroup, pMissionInfo->missions[i], 0);
			}
		}

		n = eaSize(&pMissionInfo->eaNonPersistedMissions);
		for (i = 0; i < n; i++) {
			MissionDef *pDef = mission_GetDef(pMissionInfo->eaNonPersistedMissions[i]);
			if (pDef && pDef->missionType == MissionType_Perk) {
				sprintf(buf, "%s", GET_REF(pDef->displayNameMsg.hMessage) ? TranslateMessageRef(pDef->displayNameMsg.hMessage) : pDef->name);
				pMissionGroup = debugmenu_AddNewCommandGroup(pPerkMenuItem, buf, "Complete all or part of this perk", false);
				missiondebug_DebugMenuAddMissionComplete(pMissionGroup, pMissionInfo->eaNonPersistedMissions[i], 0);
			}
		}

		n = eaSize(&pMissionInfo->eaDiscoveredMissions);
		for (i = 0; i < n; i++) {
			MissionDef *pDef = mission_GetDef(pMissionInfo->eaDiscoveredMissions[i]);
			if(pDef && pDef->missionType == MissionType_Perk)
			{
				sprintf(buf, "%s", GET_REF(pDef->displayNameMsg.hMessage) ? TranslateMessageRef(pDef->displayNameMsg.hMessage) : pDef->name);
				pMissionGroup = debugmenu_AddNewCommandGroup(pPerkMenuItem, buf, "Complete all or part of this perk", false);
				missiondebug_DebugMenuAddMissionComplete(pMissionGroup, pMissionInfo->eaDiscoveredMissions[i], 0);
			}
		}
	}

	if (pDebug && pDebug->showMissionDebugMenu) {
		// Contact Dialog
		pMenuItem = debugmenu_AddNewCommandGroup(pGroupRoot, "Contact Dialog", "Begin interaction with any contact", false);
		contact_DebugMenu(pPlayerEnt, pMenuItem);
	}
	PERFINFO_AUTO_STOP();
}


static NameList *missiondebug_CreateDoorNameList(void)
{
	int i;

	if (s_pDoorNameList) {
		return s_pDoorNameList;
	}

	s_pDoorNameList = CreateNameList_Bucket();

	if (!s_eaDebugDoorList) {
		interactable_FindAllDoors(&s_eaDebugDoorList,NULL, false);
	}
	for (i=0; i < eaSize(&s_eaDebugDoorList); i++) {
		NameList_Bucket_AddName(s_pDoorNameList, s_eaDebugDoorList[i]->pDoorName);
	}

	return s_pDoorNameList;
}


void missiondebug_CreateCharacterDebugMenu(Entity *pPlayerEnt, DebugMenuItem *pGroupRoot)
{
	int i, n;
	DebugMenuItem *pMenuItem;
	char buf[1024];
	PlayerDebug *pDebug = entGetPlayerDebug(pPlayerEnt, false);

	PERFINFO_AUTO_START_FUNC();

	debugmenu_AddNewCommand(pGroupRoot, "God Mode (toggle)", "godmode");
	debugmenu_AddNewCommand(pGroupRoot, "Full Respec", "RespecFull");
	debugmenu_AddNewCommand(pGroupRoot, "Respec last PowerTree choice", "RespecPowerTrees 1");
	debugmenu_AddNewCommand(pGroupRoot, "Clear Inventory", "inventoryclear");
	debugmenu_AddNewCommand(pGroupRoot, "Grant Resources", "GiveNumeric Resources 10000");
	debugmenu_AddNewCommand(pGroupRoot, "Move to nearest spawn point", "respawn");

	debugmenu_AddNewCommand(pGroupRoot, "Enable Debugging Queues", "EnableDebuggingQueues 1");

	if (pDebug && pDebug->allowAllInteractions) {
		debugmenu_AddNewCommand(pGroupRoot, "Stop allowing all interactions", "Encountersystemdebug_AllowAllInteractions 0");
	} else {
		debugmenu_AddNewCommand(pGroupRoot, "Allow all interactions", "Encountersystemdebug_AllowAllInteractions 1");
	}

	// Warp to Contact
	pMenuItem = debugmenu_AddNewCommandGroup(pGroupRoot, "Warp to Contact", "Warp to any contact", false);
	n = eaSize(&g_ContactLocations);
	for(i=0; i<n; i++) {
		ContactLocation *pContactLocation = g_ContactLocations[i];
		sprintf(buf, "setpos %f %f %f", pContactLocation->loc[0], pContactLocation->loc[1], pContactLocation->loc[2]);
		debugmenu_AddNewCommand(pMenuItem, pContactLocation->pchContactDefName, buf);
	}

	// Populating the door list can take a long time.  Don't do it by default
	// This uses the same flag as the mission debug list (using either is rare enough that one flag is fine)
	debugmenu_AddNewCommand(pGroupRoot,
		pDebug && pDebug->showMissionDebugMenu ? "Hide door list" : "Show door list",
		pDebug && pDebug->showMissionDebugMenu ? "missionDebugMenuEnable 0$$debugmenu_close 10$$mmm" : "missionDebugMenuEnable 1$$debugmenu_close 10$$mmm");

	debugmenu_AddNewCommand(pGroupRoot, "Add Kill Power", "add_power \"Kill\"");
	debugmenu_AddNewCommand(pGroupRoot, "Bind K to Kill (need to add first)", "bind k +power_exec \"Kill\"");

	debugmenu_AddNewCommand(pGroupRoot, "Disable Cutscenes", "DisableCutscenes 0");

	PERFINFO_AUTO_STOP();
}


void missiondebug_UpdateDebugInfo(Mission *pMission)
{
	StructDestroySafe(parse_MissionDebug, &pMission->debugInfo);
	if (pMission->infoOwner && pMission->infoOwner->showDebugInfo) {
		pMission->debugInfo = StructCreate(parse_MissionDebug);
		eaCopyStructs(&pMission->eaEventCounts, &pMission->debugInfo->debugEventLog, parse_MissionEventContainer);
	}
}


void missiondebug_UpdateAllDebugInfo(CONST_EARRAY_OF(Mission) eaMissions)
{
	int i, n = eaSize(&eaMissions);
	for (i = 0; i < n; i++) {
		Mission *pMission = eaMissions[i];
		missiondebug_UpdateDebugInfo(pMission);
		missiondebug_UpdateAllDebugInfo(pMission->children);
	}
}


AUTO_RUN;
void missiondebug_InitDebugMenu(void)
{
	debugmenu_RegisterNewGroup("Missions", missiondebug_CreateDebugMenu);
	debugmenu_RegisterNewGroup("Character", missiondebug_CreateCharacterDebugMenu);

	NameList_RegisterGetListCallback("doors", missiondebug_CreateDoorNameList);
}


