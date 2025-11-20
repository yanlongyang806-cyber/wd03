#include "UGCAchievements.h"

#include "EntityLib.h"
#include "Player.h"
#include "LoggedTransactions.h"
#include "NotifyCommon.h"
#include "StringFormat.h"
#include "UGCCommon.h"
#include "UGCProjectCommon.h"
#include "UGCProjectUtils.h"
#include "utilitiesLib.h"

#include "AutoGen/AppServerLib_autogen_RemoteFuncs.h"
#include "AutoGen/GameServerLib_autotransactions_autogen_wrappers.h"
#include "AutoGen/GameServerLib_autogen_RemoteFuncs.h"

#include "AutoGen/UGCProjectCommon_h_ast.h"
#include "AutoGen/UGCAchievements_h_ast.h"

AUTO_RUN_ANON(memBudgetAddMapping(__FILE__, BUDGET_GameSystems););

static bool s_bUGCAchievementNotificationsEnable = false;

AUTO_CMD_INT(s_bUGCAchievementNotificationsEnable, UGCAchievementNotificationsEnable) ACMD_ACCESSLEVEL(9);

static void ugcAchievementNotifyGrants(Entity *pEntity, UGCAchievementInfo *pUGCAchievementInfo, const char *name, U32 uFromTime, U32 uToTime)
{
	FOR_EACH_IN_EARRAY_FORWARDS(pUGCAchievementInfo->eaAchievements, UGCAchievement, pUGCAchievement)
	{
		if(pUGCAchievement->uGrantTime > uFromTime && pUGCAchievement->uGrantTime <= uToTime)
		{
			UGCAchievementDef *pUGCAchievementDef = ugcAchievement_DefFromRefString(pUGCAchievement->ugcAchievementName);
			if(pUGCAchievementDef && !pUGCAchievementDef->bHidden)
			{
				Message *message = GET_REF(pUGCAchievementDef->grantedNotificationMsg.hMessage);
				if(message)
				{
					char *str = NULL;
					estrStackCreate(&str);
					FormatDisplayMessage(&str, pUGCAchievementDef->grantedNotificationMsg, STRFMT_STRING("Name", name), STRFMT_INT("Target", pUGCAchievementDef->uTarget), STRFMT_END);
					notify_NotifySend(pEntity, kNotifyType_UGCAchievementGranted, str, NULL, NULL);
					estrDestroy(&str);
				}
			}
		}
	}
	FOR_EACH_END;
}

AUTO_COMMAND_REMOTE ACMD_INTERSHARD;
void gslUGC_AchievementsNotifyGrants(UgcAchievementsNotifyData *pUgcAchievementsNotifyData)
{
	Entity *pEntity = entFromAccountID(pUgcAchievementsNotifyData->uUGCAuthorID);
	UGCAccount *pUGCAccount = entGetUGCAccount(pEntity);

	if(pUGCAccount)
	{
		ugcAchievementNotifyGrants(pEntity, &pUGCAccount->author.ugcAccountAchievements, "", pUgcAchievementsNotifyData->uFromTime, pUgcAchievementsNotifyData->uToTime);

		FOR_EACH_IN_EARRAY_FORWARDS(pUGCAccount->author.eaProjectAchievements, UGCProjectAchievementInfo, pUGCProjectAchievementInfo)
		{
			ugcAchievementNotifyGrants(pEntity, &pUGCProjectAchievementInfo->ugcAchievementInfo, pUGCProjectAchievementInfo->pcName, pUgcAchievementsNotifyData->uFromTime, pUgcAchievementsNotifyData->uToTime);
		}
		FOR_EACH_END;

		FOR_EACH_IN_EARRAY_FORWARDS(pUGCAccount->author.eaSeriesAchievements, UGCSeriesAchievementInfo, pUGCSeriesAchievementInfo)
		{
			ugcAchievementNotifyGrants(pEntity, &pUGCSeriesAchievementInfo->ugcAchievementInfo, pUGCSeriesAchievementInfo->pcName, pUgcAchievementsNotifyData->uFromTime, pUgcAchievementsNotifyData->uToTime);
		}
		FOR_EACH_END;
	}
}

AUTO_COMMAND_REMOTE;
void gslUGC_AchievementsNotify(ContainerID entContainerID)
{
	Entity *pEntity = NULL;

	if(!s_bUGCAchievementNotificationsEnable)
		return;

	pEntity = entFromContainerIDAnyPartition(GLOBALTYPE_ENTITYPLAYER, entContainerID);
	if(pEntity)
	{
		UGCAccount *pUGCAccount = entGetUGCAccount(pEntity);
		if(pUGCAccount)
		{
			U32 uTime = timeSecondsSince2000();
			if(uTime > pUGCAccount->author.uLastAchievementNotifyTime)
			{
				UgcAchievementsNotifyData ugcAchievementsNotifyData;
				ugcAchievementsNotifyData.pcShardName = GetShardNameFromShardInfoString();
				ugcAchievementsNotifyData.entContainerID = entContainerID;

				ugcAchievementsNotifyData.uUGCAuthorID = pUGCAccount->accountID;
				ugcAchievementsNotifyData.uFromTime = pUGCAccount->author.uLastAchievementNotifyTime;
				ugcAchievementsNotifyData.uToTime = uTime;

				RemoteCommand_Intershard_aslUGCDataManager_AchievementsNotify(ugc_ShardName(), GLOBALTYPE_UGCDATAMANAGER, 0, &ugcAchievementsNotifyData);
			}
		}
	}
}

void ugcAchievementsAccountChangeCB(enumResourceEventType eType, const char *pDictName, const char *pRefData, Referent pReferent, void *pUserData)
{
	if(!s_bUGCAchievementNotificationsEnable)
		return;

	if(eType == RESEVENT_RESOURCE_ADDED || eType == RESEVENT_RESOURCE_MODIFIED)
	{
		UGCAccount *pUGCAccount = (UGCAccount*)pReferent;
		if(pUGCAccount)
		{
			Entity *pEntity = entFromAccountID(pUGCAccount->accountID);
			if(pEntity)
			{
				U32 now = timeSecondsSince2000();
				if(now >= pEntity->pPlayer->iLastUGCAccountRequestTimestamp + 60) // only allow every 60 seconds
				{
					pEntity->pPlayer->iLastUGCAccountRequestTimestamp = now - 1; // subtract 1 to ensure it does not happen again within 1 second

					gslUGC_AchievementsNotify(entGetContainerID(pEntity));
				}
			}
		}
	}
}

AUTO_RUN_LATE;
void gslUGCRegisterDictionary( void )
{
	if(0 == stricmp(ugc_ShardName(), GetShardNameFromShardInfoString()))
	{
		DictionaryHandle ugcAccountDict = RefSystem_RegisterSelfDefiningDictionary( GlobalTypeToCopyDictionaryName( GLOBALTYPE_UGCACCOUNT ), false, parse_UGCAccount, false, false, NULL );
		resDictProvideMissingResources( ugcAccountDict );

		resDictRequestMissingResources( ugcAccountDict, RES_DICT_KEEP_NONE, false, objCopyDictHandleRequest );
		resDictRegisterEventCallback(GlobalTypeToCopyDictionaryName(GLOBALTYPE_UGCACCOUNT), ugcAchievementsAccountChangeCB, NULL);
	}
}
