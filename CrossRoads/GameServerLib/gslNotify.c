/***************************************************************************
*     Copyright (c) 2006, Cryptic Studios
*     All Rights Reserved
*     Confidential Property of Cryptic Studios
***************************************************************************/

#include "Entity.h"
#include "fileutil.h"
#include "FolderCache.h"
#include "GameStringFormat.h"
#include "gslMission.h"
#include "gslNotify.h"
#include "gslSocial.h"
#include "Message.h"
#include "mission_common.h"
#include "NotifyCommon.h"
#include "NotifyCommon_h_ast.h"
#include "qsortG.h"
#include "Player.h"
#include "Player_h_ast.h"
#include "SocialCommon.h"
#include "StringCache.h"

#include "autogen/GameClientLib_AutoGen_ClientCmdWrappers.h"

static const char** s_ppchNotifySettingsGroups = NULL;

static void gslNotifySettingsLoad(const char *pchPath, S32 iWhen)
{
	int i, j;
	NotifySettingsDef NotifySettingsData = {0};

	if (pchPath)
	{
		fileWaitForExclusiveAccess(pchPath);
		errorLogFileIsBeingReloaded(pchPath);
	}

	ParserLoadFiles(NULL, "defs/config/NotifySettings.def", "NotifySettings.bin", PARSER_OPTIONALFLAG, parse_NotifySettingsDef, &NotifySettingsData);

	eaClear(&s_ppchNotifySettingsGroups);

	for (i = 0; i < eaSize(&NotifySettingsData.eaCategoryDefs); i++)
	{
		NotifySettingsCategoryDef* pCategory = NotifySettingsData.eaCategoryDefs[i];
		for (j = 0; j < eaSize(&pCategory->eaGroupDefs); j++)
		{
			NotifySettingsGroupDef* pGroup = pCategory->eaGroupDefs[j];
			int iIndex = eaBFind(s_ppchNotifySettingsGroups, strCmp, pGroup->pchName);
			if (iIndex == eaSize(&s_ppchNotifySettingsGroups) || s_ppchNotifySettingsGroups[iIndex] != pGroup->pchName)
			{
				eaInsert(&s_ppchNotifySettingsGroups, pGroup->pchName, iIndex);
			}
		}
	}

	StructDeInit(parse_NotifySettingsDef, &NotifySettingsData);
}

AUTO_STARTUP(NotifySettings) ASTRT_DEPS(AS_Messages); 
void gclNotifySettingsStartup(void)
{
	gslNotifySettingsLoad(NULL, 0);
	FolderCacheSetCallback(FOLDER_CACHE_CALLBACK_UPDATE, "defs/config/NotifySettings.def", gslNotifySettingsLoad);
}


// ----------------------------------------------------------------------------------
// Notify Logic
// ----------------------------------------------------------------------------------

void notify_SendMissionNotification(Entity *pEnt, Entity *pFormatEnt, MissionDef *pDef, const char *pchMessageKey, NotifyType eNotifyType)
{
	Message *pMessage = RefSystem_ReferentFromString(gMessageDict, pchMessageKey);
	MissionDef *pRootDef = pDef;
	
	while (pRootDef && GET_REF(pRootDef->parentDef))
		pRootDef = GET_REF(pRootDef->parentDef);

	// Only send messages for missions with Display Names
	if (pEnt && (pMessage || eNotifyType == kNotifyType_MissionInvisibleSubObjectiveComplete) && pDef && pRootDef && (missiondef_HasDisplayName(pRootDef) || eNotifyType == kNotifyType_MissionInvisibleSubObjectiveComplete))
	{
		char *estrBuffer = NULL;
		
		estrStackCreate(&estrBuffer);

		entFormatGameMessage(pEnt, &estrBuffer, pMessage,
			STRFMT_ENTITY_KEY("Entity", pFormatEnt), STRFMT_MISSIONDEF(pDef), STRFMT_END);
		
		// Send the notification
		if (estrBuffer && estrBuffer[0])
		{
			ClientCmd_NotifySend(pEnt, eNotifyType, estrBuffer, pDef->pchRefString, pRootDef->pchIconName);
		}
		
		if(pDef->missionType == MissionType_Perk)
		{
			gslSocialActivity(pEnt, kActivityType_Perk, strdup(pDef->pchRefString));
			ClientCmd_gclSteamCmdSetAchievement(pEnt, pDef->name);
		}

		if(estrBuffer)
			estrDestroy(&estrBuffer);
	}
}


AUTO_COMMAND ACMD_NAME("notify_ChangeSetting") ACMD_ACCESSLEVEL(0) ACMD_SERVERCMD ACMD_HIDE;
void gslNotify_ChangeSetting(Entity* pEnt, const char* pchNotifyGroupName, NotifySettingFlags eFlags)
{
	PlayerUI* pUI = SAFE_MEMBER2(pEnt, pPlayer, pUI);
	if (pUI)
	{
		int i = eaIndexedFindUsingString(&pUI->eaNotifySettings, pchNotifyGroupName);
		bool bValid = false;
		
		if (i >= 0)
		{
			NotifySetting* pSetting = pUI->eaNotifySettings[i];
			if (eFlags == kNotifySettingFlags_None)
			{
				eaRemove(&pUI->eaNotifySettings, i);
				StructDestroy(parse_NotifySetting, pSetting);
				bValid = true;
			}
			else if (pSetting->eFlags != eFlags)
			{
				pSetting->eFlags = eFlags;
				bValid = true;
			}
		}
		else if (eFlags != kNotifySettingFlags_None)
		{
			const char* pchNotifyGroupNamePooled = allocFindString(pchNotifyGroupName);
			int iIndex = eaBFind(s_ppchNotifySettingsGroups, strCmp, pchNotifyGroupNamePooled);
			if (iIndex != eaSize(&s_ppchNotifySettingsGroups) && s_ppchNotifySettingsGroups[iIndex] == pchNotifyGroupNamePooled)
			{
				NotifySetting* pSetting = StructCreate(parse_NotifySetting);
				pSetting->pchNotifyGroupName = pchNotifyGroupNamePooled;
				pSetting->eFlags = eFlags;
				eaIndexedEnable(&pUI->eaNotifySettings, parse_NotifySetting);
				eaPush(&pUI->eaNotifySettings, pSetting);
				bValid = true;
			}
		}
		if (bValid)
		{
			pUI->iNotifySettingVersion++;
			entity_SetDirtyBit(pEnt, parse_PlayerUI, pEnt->pPlayer->pUI, true);
			entity_SetDirtyBit(pEnt, parse_Player, pEnt->pPlayer, false);
		}
	}
}

AUTO_COMMAND ACMD_NAME("notify_ResetSettings") ACMD_ACCESSLEVEL(0) ACMD_SERVERCMD ACMD_HIDE;
void gslNotify_ResetSettings(Entity* pEnt, const char* pchNotifyGroupName, NotifySettingFlags eFlags)
{
	PlayerUI* pUI = SAFE_MEMBER2(pEnt, pPlayer, pUI);
	if (pUI)
	{
		eaDestroyStruct(&pUI->eaNotifySettings, parse_NotifySetting);

		pUI->iNotifySettingVersion++;
		entity_SetDirtyBit(pEnt, parse_PlayerUI, pEnt->pPlayer->pUI, true);
		entity_SetDirtyBit(pEnt, parse_Player, pEnt->pPlayer, false);
	}
}