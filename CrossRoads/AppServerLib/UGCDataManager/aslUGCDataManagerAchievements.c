#include "UGCAchievements.h"
#include "UGCAchievements_h_ast.h"

#include "UGCProjectCommon.h"
#include "UGCProjectCommon_h_ast.h"

#include "UGCCommon.h"

#include "rewardCommon.h"

#include "LoggedTransactions.h"
#include "StringUtil.h"

#include "aslUGCDataManagerProject.h"

#include "autogen/AppServerLib_autotransactions_autogen_wrappers.h"
#include "AutoGen/AppServerLib_autogen_RemoteFuncs.h"
#include "AutoGen/GameServerLib_autogen_RemoteFuncs.h"

#include "aslUGCDataManagerAchievements_c_ast.h"

#include "aslUGCDataManager.h"

static bool s_bUGCAchievementEventsEnable = true;

AUTO_CMD_INT(s_bUGCAchievementEventsEnable, UGCAchievementEventsEnable) ACMD_ACCESSLEVEL(9);

//AUTO_TRANS_HELPER;
void ugc_trh_AchievementPropagate(ATR_ARGS, ATH_ARG NOCONST(UGCAchievementInfo) *pUGCAchievementInfo, ATH_ARG NOCONST(UGCAchievement) *pUGCAchievement, ContainerID uUGCAccountID);

AUTO_TRANS_HELPER;
void ugc_trh_AchievementGrantAndPropogate(ATR_ARGS, ATH_ARG NOCONST(UGCAchievementInfo) *pUGCAchievementInfo, ATH_ARG NOCONST(UGCAchievement) *pUGCAchievement,
	UGCAchievementDef *pUGCAchievementDef, ContainerID uUGCAccountID)
{
	if(gConf.bUGCAchievementsEnable)
	{
		if(pUGCAchievementDef->uTarget > 0)
			pUGCAchievement->uCount = pUGCAchievementDef->uTarget;

		if(!pUGCAchievement->uGrantTime)
		{
			UGCAchievementEvent *event = NULL;

			pUGCAchievement->uGrantTime = timeSecondsSince2000();

			event = StructCreate(parse_UGCAchievementEvent);
			event->uUGCAuthorID = uUGCAccountID;
			event->ugcAchievementServerEvent = StructCreate(parse_UGCAchievementServerEvent);
			event->ugcAchievementServerEvent->ugcAchievementGrantedEvent = StructCreate(parse_UGCAchievementGrantedEvent);
			event->ugcAchievementServerEvent->ugcAchievementGrantedEvent->pUGCAchievementName = pUGCAchievement->ugcAchievementName;
			QueueRemoteCommand_ugcAchievementEvent_Send(ATR_RESULT_SUCCESS, GLOBALTYPE_UGCDATAMANAGER, 0, event, __FUNCTION__);
			StructDestroy(parse_UGCAchievementEvent, event);
		}

		ugc_trh_AchievementPropagate(ATR_RECURSE, pUGCAchievementInfo, pUGCAchievement, uUGCAccountID);
	}
}

AUTO_TRANS_HELPER;
void ugc_trh_AchievementPropagate(ATR_ARGS, ATH_ARG NOCONST(UGCAchievementInfo) *pUGCAchievementInfo, ATH_ARG NOCONST(UGCAchievement) *pUGCAchievement, ContainerID uUGCAccountID)
{
	if(gConf.bUGCAchievementsEnable)
	{
		if(pUGCAchievement->uGrantTime)
		{
			UGCAchievementDef *pUGCAchievementDef = ugcAchievement_DefFromRefString(pUGCAchievement->ugcAchievementName);

			// Traverse up Achievement tree to root, marking achieved if all children have been achieved
			if(pUGCAchievementDef && pUGCAchievementDef->pParentDef)
			{
				bool all_children_achieved = true;
				FOR_EACH_IN_EARRAY_FORWARDS(pUGCAchievementDef->pParentDef->subAchievements, UGCAchievementDef, pUGCAchievementDefChild)
				{
					int index = eaIndexedFindUsingString(&pUGCAchievementInfo->eaAchievements, pUGCAchievementDefChild->pchRefString);
					if(index >= 0)
					{
						NOCONST(UGCAchievement) *pUGCAchievementChild = pUGCAchievementInfo->eaAchievements[index];
						if(!pUGCAchievementChild->uGrantTime)
						{
							all_children_achieved = false;
							break;
						}
					}
				}
				FOR_EACH_END;

				if(all_children_achieved)
				{
					int index = eaIndexedFindUsingString(&pUGCAchievementInfo->eaAchievements, pUGCAchievementDef->pParentDef->pchRefString);
					if(index >= 0)
						ugc_trh_AchievementGrantAndPropogate(ATR_RECURSE, pUGCAchievementInfo, pUGCAchievementInfo->eaAchievements[index], pUGCAchievementDef->pParentDef, uUGCAccountID);
				}
			}
		}
	}
}

AUTO_TRANS_HELPER;
void ugc_trh_SynchronizeAchievementsRecurse(ATH_ARG NOCONST(UGCAchievementInfo) *pUGCAchievementInfo, UGCAchievementDef *pUGCAchievementDef )
{
	if(gConf.bUGCAchievementsEnable)
	{
		NOCONST(UGCAchievement) *pUGCAchievement = eaIndexedGetUsingString(&pUGCAchievementInfo->eaAchievements, pUGCAchievementDef->pchRefString);
		if(!pUGCAchievement)
		{
			pUGCAchievement = StructCreateNoConst(parse_UGCAchievement);
			pUGCAchievement->ugcAchievementName = pUGCAchievementDef->pchRefString;
			eaIndexedPushUsingStringIfPossible(&pUGCAchievementInfo->eaAchievements, pUGCAchievement->ugcAchievementName, pUGCAchievement);
		}

		FOR_EACH_IN_EARRAY_FORWARDS(pUGCAchievementDef->subAchievements, UGCAchievementDef, pSubAchievementDef)
		{
			ugc_trh_SynchronizeAchievementsRecurse(pUGCAchievementInfo, pSubAchievementDef);
		}
		FOR_EACH_END;
	}
}

AUTO_TRANS_HELPER;
void ugc_trh_SynchronizeAchievementsByScope(ATR_ARGS, ATH_ARG NOCONST(UGCAchievementInfo) *pUGCAchievementInfo, const char *scope, ContainerID uUGCAccountID)
{
	if(gConf.bUGCAchievementsEnable)
	{
		const char *pUGCAchievementDefName = NULL;
		UGCAchievementDef *pUGCAchievementDef = NULL;
		ResourceIterator iter;
		NOCONST(UGCAchievement) **removals = NULL;

		resInitIterator("UGCAchievement", &iter);
		while(resIteratorGetNext(&iter, &pUGCAchievementDefName, &pUGCAchievementDef))
			if(stricmp(pUGCAchievementDef->scope, scope) == 0)
				ugc_trh_SynchronizeAchievementsRecurse(pUGCAchievementInfo, pUGCAchievementDef);
		resFreeIterator(&iter);

		// Generate removals or flag as achieved if the target has changed. Also handle cooldowns
		FOR_EACH_IN_EARRAY_FORWARDS(pUGCAchievementInfo->eaAchievements, NOCONST(UGCAchievement), pUGCAchievement)
		{
			pUGCAchievementDef = ugcAchievement_DefFromRefString(pUGCAchievement->ugcAchievementName);
			if(!pUGCAchievementDef || 0 != stricmp(scope, pUGCAchievementDef->scope))
				eaPush(&removals, pUGCAchievement); // remove achievements that are missing or in the wrong scope (maybe scope "None")
			else
			{
				if(pUGCAchievementDef->uTarget > 0 && pUGCAchievement->uCount >= pUGCAchievementDef->uTarget)
					ugc_trh_AchievementGrantAndPropogate(ATR_RECURSE, pUGCAchievementInfo, pUGCAchievement, pUGCAchievementDef, uUGCAccountID);

				if(pUGCAchievement->uGrantTime && pUGCAchievementDef->bRepeatable)
				{
					S32 iCooldownDuration = pUGCAchievementDef->uRepeatCooldownHours * 60 * 60;
					U32 grantTime = ugcAchievement_GetCooldownBlockGrantTime(pUGCAchievement->uGrantTime, pUGCAchievementDef);
					if(timeSecondsSince2000() >= grantTime + iCooldownDuration)
					{
						pUGCAchievement->uCount = 0;
						pUGCAchievement->uGrantTime = 0;
						while(pUGCAchievementDef->pParentDef)
						{
							NOCONST(UGCAchievement) *pUGCAchievementParent = eaIndexedGetUsingString(&pUGCAchievementInfo->eaAchievements, pUGCAchievementDef->pParentDef->pchRefString);
							if(pUGCAchievementParent)
							{
								pUGCAchievement->uCount = 0;
								pUGCAchievementParent->uGrantTime = 0;
							}
							pUGCAchievementDef = pUGCAchievementDef->pParentDef;
						}
					}
				}
			}
		}
		FOR_EACH_END;

		FOR_EACH_IN_EARRAY_FORWARDS(removals, NOCONST(UGCAchievement), pUGCAchievement)
		{
			eaIndexedRemoveUsingString(&pUGCAchievementInfo->eaAchievements, pUGCAchievement->ugcAchievementName);
		}
		FOR_EACH_END;

		eaDestroy(&removals);
	}
}

AUTO_TRANSACTION
ATR_LOCKS(pUGCAccount, ".author.ugcAccountAchievements.eaAchievements");
enumTransactionOutcome trUgcSynchronizeAccountAchievements(ATR_ARGS, NOCONST(UGCAccount) *pUGCAccount, ContainerID uUGCAccountID)
{
	if(gConf.bUGCAchievementsEnable)
		ugc_trh_SynchronizeAchievementsByScope(ATR_RECURSE, &pUGCAccount->author.ugcAccountAchievements, "Account", uUGCAccountID);

	return TRANSACTION_OUTCOME_SUCCESS;
}

AUTO_TRANS_HELPER;
void ugc_trh_SynchronizeProjectAchievements(ATR_ARGS, ATH_ARG NOCONST(UGCAccount) *pUGCAccount, ContainerID uUGCAccountID, ContainerID uUGCProjectID)
{
	if(gConf.bUGCAchievementsEnable)
	{
		UGCProject *pUGCProject = NULL;

		NOCONST(UGCProjectAchievementInfo) *pUGCProjectAchievementInfo = eaIndexedGetUsingInt(&pUGCAccount->author.eaProjectAchievements, uUGCProjectID);
		if(!pUGCProjectAchievementInfo)
		{
			pUGCProjectAchievementInfo = StructCreateNoConst(parse_UGCProjectAchievementInfo);
			pUGCProjectAchievementInfo->projectID = uUGCProjectID;
			eaIndexedPushUsingIntIfPossible(&pUGCAccount->author.eaProjectAchievements, uUGCProjectID, pUGCProjectAchievementInfo);
		}

		ASSERT_CONTAINER_DATA_UGC("A non-UGCDataManager server tried to get a UGC container!");

		pUGCProject = objGetContainerData(GLOBALTYPE_UGCPROJECT, uUGCProjectID);
		if(pUGCProject)
		{
			const char *name = UGCProject_GetVersionName(pUGCProject, UGCProject_GetMostRecentPublishedVersion(pUGCProject));
			if(!nullStr(name))
				pUGCProjectAchievementInfo->pcName = strdup(name);
		}

		ugc_trh_SynchronizeAchievementsByScope(ATR_RECURSE, &pUGCProjectAchievementInfo->ugcAchievementInfo, "Project", uUGCAccountID);
	}
}

AUTO_TRANSACTION
ATR_LOCKS(pUGCAccount, ".author.eaProjectAchievements[]");
enumTransactionOutcome trUgcSynchronizeProjectAchievements(ATR_ARGS, NOCONST(UGCAccount) *pUGCAccount, ContainerID uUGCAccountID, ContainerID uUGCProjectID)
{
	if(gConf.bUGCAchievementsEnable)
		ugc_trh_SynchronizeProjectAchievements(ATR_RECURSE, pUGCAccount, uUGCAccountID, uUGCProjectID);

	return TRANSACTION_OUTCOME_SUCCESS;
}

AUTO_TRANS_HELPER;
void ugc_trh_SynchronizeSeriesAchievements(ATR_ARGS, ATH_ARG NOCONST(UGCAccount) *pUGCAccount, ContainerID uUGCAccountID, ContainerID uUGCSeriesID)
{
	if(gConf.bUGCAchievementsEnable)
	{
		UGCProjectSeries *pUGCProjectSeries = NULL;

		NOCONST(UGCSeriesAchievementInfo) *pUGCSeriesAchievementInfo = eaIndexedGetUsingInt(&pUGCAccount->author.eaSeriesAchievements, uUGCSeriesID);
		if(!pUGCSeriesAchievementInfo)
		{
			pUGCSeriesAchievementInfo = StructCreateNoConst(parse_UGCSeriesAchievementInfo);
			pUGCSeriesAchievementInfo->seriesID = uUGCSeriesID;
			eaIndexedPushUsingIntIfPossible(&pUGCAccount->author.eaSeriesAchievements, uUGCSeriesID, pUGCSeriesAchievementInfo);
		}

		ASSERT_CONTAINER_DATA_UGC("A non-UGCDataManager server tried to get a UGC container!");

		pUGCProjectSeries = objGetContainerData(GLOBALTYPE_UGCPROJECTSERIES, uUGCSeriesID);
		if(pUGCProjectSeries)
		{
			const UGCProjectSeriesVersion *pUGCProjectSeriesVersion = UGCProjectSeries_GetMostRecentPublishedVersion(pUGCProjectSeries);
			if(pUGCProjectSeriesVersion && !nullStr(pUGCProjectSeriesVersion->strName))
				pUGCSeriesAchievementInfo->pcName = strdup(pUGCProjectSeriesVersion->strName);
		}

		ugc_trh_SynchronizeAchievementsByScope(ATR_RECURSE, &pUGCSeriesAchievementInfo->ugcAchievementInfo, "Series", uUGCAccountID);
	}
}

AUTO_TRANSACTION
ATR_LOCKS(pUGCAccount, ".author.eaSeriesAchievements[]");
enumTransactionOutcome trUgcSynchronizeSeriesAchievements(ATR_ARGS, NOCONST(UGCAccount) *pUGCAccount, ContainerID uUGCAccountID, ContainerID uUGCSeriesID)
{
	if(gConf.bUGCAchievementsEnable)
		ugc_trh_SynchronizeSeriesAchievements(ATR_RECURSE, pUGCAccount, uUGCAccountID, uUGCSeriesID);

	return TRANSACTION_OUTCOME_SUCCESS;
}

AUTO_TRANSACTION
ATR_LOCKS(pUGCAccount, ".author.ugcAccountAchievements.eaAchievements, .author.eaProjectAchievements, .author.eaSeriesAchievements");
enumTransactionOutcome trUgcSynchronizeAchievements(ATR_ARGS, NOCONST(UGCAccount) *pUGCAccount, ContainerID uUGCAccountID)
{
	if(gConf.bUGCAchievementsEnable)
	{
		ContainerID *eauUGCProjectIDs = NULL;
		ContainerID *eauUGCSeriesIDs = NULL;
		int i;

		ugc_trh_SynchronizeAchievementsByScope(ATR_RECURSE, &pUGCAccount->author.ugcAccountAchievements, "Account", uUGCAccountID);

		GetUGCProjectsByUGCAccount(uUGCAccountID, &eauUGCProjectIDs, &eauUGCSeriesIDs);

		for(i = 0; i < eaiSize(&eauUGCProjectIDs); i++)
			ugc_trh_SynchronizeProjectAchievements(ATR_RECURSE, pUGCAccount, uUGCAccountID, eauUGCProjectIDs[i]);

		for(i = 0; i < eaiSize(&eauUGCSeriesIDs); i++)
			ugc_trh_SynchronizeSeriesAchievements(ATR_RECURSE, pUGCAccount, uUGCAccountID, eauUGCSeriesIDs[i]);

		eaiDestroy(&eauUGCProjectIDs);
		eaiDestroy(&eauUGCSeriesIDs);
	}

	return TRANSACTION_OUTCOME_SUCCESS;
}

AUTO_TRANSACTION
ATR_LOCKS(pUGCAccount, ".author.ugcAccountAchievements.eaAchievements");
enumTransactionOutcome trUgcRemoveAccountAchievements(ATR_ARGS, NOCONST(UGCAccount) *pUGCAccount)
{
	eaDestroyStructNoConst(&pUGCAccount->author.ugcAccountAchievements.eaAchievements, parse_UGCAchievement);

	return TRANSACTION_OUTCOME_SUCCESS;
}

AUTO_TRANSACTION
ATR_LOCKS(pUGCAccount, ".author.eaProjectAchievements[]");
enumTransactionOutcome trUgcRemoveProjectAchievements(ATR_ARGS, NOCONST(UGCAccount) *pUGCAccount, ContainerID uUGCProjectID)
{
	eaIndexedRemoveUsingInt(&pUGCAccount->author.eaProjectAchievements, uUGCProjectID);

	return TRANSACTION_OUTCOME_SUCCESS;
}

AUTO_TRANSACTION
ATR_LOCKS(pUGCAccount, ".author.eaSeriesAchievements[]");
enumTransactionOutcome trUgcRemoveSeriesAchievements(ATR_ARGS, NOCONST(UGCAccount) *pUGCAccount, ContainerID uUGCSeriesID)
{
	eaIndexedRemoveUsingInt(&pUGCAccount->author.eaSeriesAchievements, uUGCSeriesID);

	return TRANSACTION_OUTCOME_SUCCESS;
}

AUTO_TRANS_HELPER;
void ugc_trh_IncrementAchievement(ATR_ARGS, ATH_ARG NOCONST(UGCAchievementInfo) *pUGCAchievementInfo, ATH_ARG NOCONST(UGCAchievement) *pUGCAchievement, U32 increment, ContainerID uUGCAccountID)
{
	if(gConf.bUGCAchievementsEnable)
	{
		UGCAchievementDef *pUGCAchievementDef = ugcAchievement_DefFromRefString(pUGCAchievement->ugcAchievementName);
		if(pUGCAchievementDef)
		{
			U32 now = timeSecondsSince2000();

			// Skip if we are in an ordered counting parent and the previous sibling has not yet been granted
			if(pUGCAchievementDef->pParentDef && pUGCAchievementDef->pParentDef->bOrderedCounting)
			{
				UGCAchievementDef *pPreviousSiblingDef = ugcAchievement_GetPreviousSibling(pUGCAchievementDef);
				if(pPreviousSiblingDef)
				{
					int index = eaIndexedFindUsingString(&pUGCAchievementInfo->eaAchievements, pPreviousSiblingDef->pchRefString);
					if(index >= 0)
					{
						NOCONST(UGCAchievement) *pPreviousSibling = pUGCAchievementInfo->eaAchievements[index];
						if(!pPreviousSibling->uGrantTime)
							return;
					}
				}
			}

			if(pUGCAchievementDef->uConsecutiveHours)
			{
				if(pUGCAchievement->uLastCountTime)
				{
					U32 uLastCountTime = ugcAchievement_GetConsecutiveBlockLastCountTime(pUGCAchievement->uLastCountTime, pUGCAchievementDef);
					if(now >= uLastCountTime + 60 * 60 * 2 * pUGCAchievementDef->uConsecutiveHours)
					{
						// player skipped at least one whole consecutive period
						if(pUGCAchievementDef->uConsecutiveMissCountResetMultiple)
							pUGCAchievement->uCount = pUGCAchievementDef->uConsecutiveMissCountResetMultiple * (pUGCAchievement->uCount / pUGCAchievementDef->uConsecutiveMissCountResetMultiple);
						else
							pUGCAchievement->uCount = 0;
					}
					else if(now < uLastCountTime + 60 * 60 * pUGCAchievementDef->uConsecutiveHours)
						return; // player already completed the activity during this period
					// otherwise, count the activity
				}
			}

			pUGCAchievement->uCount += increment;
			pUGCAchievement->uLastCountTime = now;

			if(pUGCAchievementDef->uConsecutiveHours)
				if(pUGCAchievement->uCount > pUGCAchievement->uMaximumConsecutiveCount)
					pUGCAchievement->uMaximumConsecutiveCount = pUGCAchievement->uCount;

			if(pUGCAchievementDef->uTarget > 0 && pUGCAchievement->uCount >= pUGCAchievementDef->uTarget)
			{
				ugc_trh_AchievementGrantAndPropogate(ATR_RECURSE, pUGCAchievementInfo, pUGCAchievement, pUGCAchievementDef, uUGCAccountID);
			}
		}
	}
}

AUTO_TRANSACTION
ATR_LOCKS(pUGCAccount, ".author.ugcAccountAchievements.eaAchievements");
enumTransactionOutcome trUgcAccountAchievementIncrement(ATR_ARGS, NOCONST(UGCAccount) *pUGCAccount, ContainerID uUGCAccountID, const char *ugcAchievementName, U32 increment)
{
	if(gConf.bUGCAchievementsEnable)
	{
		NOCONST(UGCAchievement) *pUGCAchievement = NULL;
		int index = eaIndexedFindUsingString(&pUGCAccount->author.ugcAccountAchievements.eaAchievements, ugcAchievementName);
		if(index < 0)
		{
			AssertOrAlert("UGC_ACCOUNT_ACHIEVEMENT_NOT_FOUND", "UGCAccount id: %d, Achievement Name: %s, in function: %s", uUGCAccountID, ugcAchievementName, __FUNCTION__);
			return TRANSACTION_OUTCOME_FAILURE;
		}

		if(!increment)
		{
			AssertOrAlert("UGC_INCREMENT_AMOUNT_MUST_BE_POSITIVE", "UGCAccount id: %d, Achievement Name: %s, in function: %s", uUGCAccountID, ugcAchievementName, __FUNCTION__);
			return TRANSACTION_OUTCOME_FAILURE;
		}

		pUGCAchievement = pUGCAccount->author.ugcAccountAchievements.eaAchievements[index];

		ugc_trh_IncrementAchievement(ATR_RECURSE, &pUGCAccount->author.ugcAccountAchievements, pUGCAchievement, increment, uUGCAccountID);
	}

	return TRANSACTION_OUTCOME_SUCCESS;
}

AUTO_TRANSACTION
ATR_LOCKS(pUGCAccount, ".author.eaProjectAchievements[]");
enumTransactionOutcome trUgcProjectAchievementIncrement(ATR_ARGS, NOCONST(UGCAccount) *pUGCAccount, ContainerID uUGCAccountID, ContainerID uUGCProjectID, const char *ugcAchievementName,
	U32 increment)
{
	if(gConf.bUGCAchievementsEnable)
	{
		NOCONST(UGCAchievement) *pUGCAchievement = NULL;
		int index;
		NOCONST(UGCProjectAchievementInfo) *pUGCProjectAchievementInfo = eaIndexedGetUsingInt(&pUGCAccount->author.eaProjectAchievements, uUGCProjectID);
		if(!pUGCProjectAchievementInfo)
		{
			AssertOrAlert("UGC_PROJECT_ACHIEVEMENT_INFO_NOT_FOUND", "UGCAccount id: %d, UGCProject id: %d, in function: %s", uUGCAccountID, uUGCProjectID, __FUNCTION__);
			return TRANSACTION_OUTCOME_FAILURE;
		}

		index = eaIndexedFindUsingString(&pUGCProjectAchievementInfo->ugcAchievementInfo.eaAchievements, ugcAchievementName);
		if(index < 0)
		{
			AssertOrAlert("UGC_PROJECT_ACHIEVEMENT_NOT_FOUND", "UGCAccount id: %d, UGCProject id: %d, Achievement Name: %s, in function: %s", uUGCAccountID, uUGCProjectID, ugcAchievementName, __FUNCTION__);
			return TRANSACTION_OUTCOME_FAILURE;
		}

		if(!increment)
		{
			AssertOrAlert("UGC_INCREMENT_AMOUNT_MUST_BE_POSITIVE", "UGCAccount id: %d, UGCProject id: %d, Achievement Name: %s, in function: %s", uUGCAccountID, uUGCProjectID, ugcAchievementName, __FUNCTION__);
			return TRANSACTION_OUTCOME_FAILURE;
		}

		pUGCAchievement = pUGCProjectAchievementInfo->ugcAchievementInfo.eaAchievements[index];

		ugc_trh_IncrementAchievement(ATR_RECURSE, &pUGCProjectAchievementInfo->ugcAchievementInfo, pUGCAchievement, increment, uUGCAccountID);
	}

	return TRANSACTION_OUTCOME_SUCCESS;
}

AUTO_TRANSACTION
ATR_LOCKS(pUGCAccount, ".author.eaSeriesAchievements[]");
enumTransactionOutcome trUgcSeriesAchievementIncrement(ATR_ARGS, NOCONST(UGCAccount) *pUGCAccount, ContainerID uUGCAccountID, ContainerID uUGCSeriesID, const char *ugcAchievementName,
	U32 increment)
{
	if(gConf.bUGCAchievementsEnable)
	{
		NOCONST(UGCAchievement) *pUGCAchievement = NULL;
		int index;
		NOCONST(UGCSeriesAchievementInfo) *pUGCSeriesAchievementInfo = eaIndexedGetUsingInt(&pUGCAccount->author.eaSeriesAchievements, uUGCSeriesID);
		if(!pUGCSeriesAchievementInfo)
		{
			AssertOrAlert("UGC_PROJECT_ACHIEVEMENT_INFO_NOT_FOUND", "UGCAccount id: %d, UGCSeries id: %d, in function: %s", uUGCAccountID, uUGCSeriesID, __FUNCTION__);
			return TRANSACTION_OUTCOME_FAILURE;
		}

		index = eaIndexedFindUsingString(&pUGCSeriesAchievementInfo->ugcAchievementInfo.eaAchievements, ugcAchievementName);
		if(index < 0)
		{
			AssertOrAlert("UGC_SERIES_ACHIEVEMENT_NOT_FOUND", "UGCAccount id: %d, UGCSeries id: %d, Achievement Name: %s, in function: %s", uUGCAccountID, uUGCSeriesID, ugcAchievementName, __FUNCTION__);
			return TRANSACTION_OUTCOME_FAILURE;
		}

		if(!increment)
		{
			AssertOrAlert("UGC_INCREMENT_AMOUNT_MUST_BE_POSITIVE", "UGCAccount id: %d, UGCSeries id: %d, Achievement Name: %s, in function: %s", uUGCAccountID, uUGCSeriesID, ugcAchievementName, __FUNCTION__);
			return TRANSACTION_OUTCOME_FAILURE;
		}

		pUGCAchievement = pUGCSeriesAchievementInfo->ugcAchievementInfo.eaAchievements[index];

		ugc_trh_IncrementAchievement(ATR_RECURSE, &pUGCSeriesAchievementInfo->ugcAchievementInfo, pUGCAchievement, increment, uUGCAccountID);
	}

	return TRANSACTION_OUTCOME_SUCCESS;
}

AUTO_TRANSACTION
ATR_LOCKS(pUGCAccount, ".author.ugcAccountAchievements.eaAchievements");
enumTransactionOutcome trUgcAccountAchievementGrant(ATR_ARGS, NOCONST(UGCAccount) *pUGCAccount, ContainerID uUGCAccountID, const char *ugcAchievementName, const char *reason)
{
	if(gConf.bUGCAchievementsEnable)
	{
		NOCONST(UGCAchievement) *pUGCAchievement = NULL;
		UGCAchievementDef *pUGCAchievementDef = NULL;
		int index = eaIndexedFindUsingString(&pUGCAccount->author.ugcAccountAchievements.eaAchievements, ugcAchievementName);
		if(index < 0)
		{
			AssertOrAlert("UGC_ACCOUNT_ACHIEVEMENT_NOT_FOUND", "UGCAccount id: %d, Achievement Name: %s, in function: %s", uUGCAccountID, ugcAchievementName, __FUNCTION__);
			return TRANSACTION_OUTCOME_FAILURE;
		}

		pUGCAchievement = pUGCAccount->author.ugcAccountAchievements.eaAchievements[index];

		pUGCAchievementDef = ugcAchievement_DefFromRefString(pUGCAchievement->ugcAchievementName);
		if(pUGCAchievementDef)
			ugc_trh_AchievementGrantAndPropogate(ATR_RECURSE, &pUGCAccount->author.ugcAccountAchievements, pUGCAchievement, pUGCAchievementDef, uUGCAccountID);
	}

	return TRANSACTION_OUTCOME_SUCCESS;
}

AUTO_TRANSACTION
ATR_LOCKS(pUGCAccount, ".author.eaProjectAchievements[]");
enumTransactionOutcome trUgcProjectAchievementGrant(ATR_ARGS, NOCONST(UGCAccount) *pUGCAccount, ContainerID uUGCAccountID, ContainerID uUGCProjectID, const char *ugcAchievementName,
	const char *reason)
{
	if(gConf.bUGCAchievementsEnable)
	{
		NOCONST(UGCAchievement) *pUGCAchievement = NULL;
		UGCAchievementDef *pUGCAchievementDef = NULL;
		int index;
		NOCONST(UGCProjectAchievementInfo) *pUGCProjectAchievementInfo = eaIndexedGetUsingInt(&pUGCAccount->author.eaProjectAchievements, uUGCProjectID);
		if(!pUGCProjectAchievementInfo)
		{
			AssertOrAlert("UGC_PROJECT_ACHIEVEMENT_INFO_NOT_FOUND", "UGCAccount id: %d, UGCProject id: %d, in function: %s", uUGCAccountID, uUGCProjectID, __FUNCTION__);
			return TRANSACTION_OUTCOME_FAILURE;
		}

		index = eaIndexedFindUsingString(&pUGCProjectAchievementInfo->ugcAchievementInfo.eaAchievements, ugcAchievementName);
		if(index < 0)
		{
			AssertOrAlert("UGC_PROJECT_ACHIEVEMENT_NOT_FOUND", "UGCAccount id: %d, UGCProject id: %d, Achievement Name: %s, in function: %s", uUGCAccountID, uUGCProjectID, ugcAchievementName, __FUNCTION__);
			return TRANSACTION_OUTCOME_FAILURE;
		}

		pUGCAchievement = pUGCProjectAchievementInfo->ugcAchievementInfo.eaAchievements[index];

		pUGCAchievementDef = ugcAchievement_DefFromRefString(pUGCAchievement->ugcAchievementName);
		if(pUGCAchievementDef)
			ugc_trh_AchievementGrantAndPropogate(ATR_RECURSE, &pUGCProjectAchievementInfo->ugcAchievementInfo, pUGCAchievement, pUGCAchievementDef, uUGCAccountID);
	}

	return TRANSACTION_OUTCOME_SUCCESS;
}

AUTO_TRANSACTION
ATR_LOCKS(pUGCAccount, ".author.eaSeriesAchievements[]");
enumTransactionOutcome trUgcSeriesAchievementGrant(ATR_ARGS, NOCONST(UGCAccount) *pUGCAccount, ContainerID uUGCAccountID, ContainerID uUGCSeriesID, const char *ugcAchievementName,
	const char *reason)
{
	if(gConf.bUGCAchievementsEnable)
	{
		NOCONST(UGCAchievement) *pUGCAchievement = NULL;
		UGCAchievementDef *pUGCAchievementDef = NULL;
		int index;
		NOCONST(UGCSeriesAchievementInfo) *pUGCSeriesAchievementInfo = eaIndexedGetUsingInt(&pUGCAccount->author.eaSeriesAchievements, uUGCSeriesID);
		if(!pUGCSeriesAchievementInfo)
		{
			AssertOrAlert("UGC_PROJECT_ACHIEVEMENT_INFO_NOT_FOUND", "UGCAccount id: %d, UGCSeries id: %d, in function: %s", uUGCAccountID, uUGCSeriesID, __FUNCTION__);
			return TRANSACTION_OUTCOME_FAILURE;
		}

		index = eaIndexedFindUsingString(&pUGCSeriesAchievementInfo->ugcAchievementInfo.eaAchievements, ugcAchievementName);
		if(index < 0)
		{
			AssertOrAlert("UGC_SERIES_ACHIEVEMENT_NOT_FOUND", "UGCAccount id: %d, UGCSeries id: %d, Achievement Name: %s, in function: %s", uUGCAccountID, uUGCSeriesID, ugcAchievementName, __FUNCTION__);
			return TRANSACTION_OUTCOME_FAILURE;
		}

		pUGCAchievement = pUGCSeriesAchievementInfo->ugcAchievementInfo.eaAchievements[index];

		pUGCAchievementDef = ugcAchievement_DefFromRefString(pUGCAchievement->ugcAchievementName);
		if(pUGCAchievementDef)
			ugc_trh_AchievementGrantAndPropogate(ATR_RECURSE, &pUGCSeriesAchievementInfo->ugcAchievementInfo, pUGCAchievement, pUGCAchievementDef, uUGCAccountID);
	}

	return TRANSACTION_OUTCOME_SUCCESS;
}

AUTO_TRANSACTION
ATR_LOCKS(pUGCAccount, ".author.eaProjectAchievements, .author.eaSeriesAchievements, .author.ugcAccountAchievements.eaAchievements");
enumTransactionOutcome trUgcResetAchievements(ATR_ARGS, NOCONST(UGCAccount) *pUGCAccount)
{
	eaDestroyStructNoConst(&pUGCAccount->author.eaProjectAchievements, parse_UGCProjectAchievementInfo);
	eaDestroyStructNoConst(&pUGCAccount->author.eaSeriesAchievements, parse_UGCSeriesAchievementInfo);
	eaDestroyStructNoConst(&pUGCAccount->author.ugcAccountAchievements.eaAchievements, parse_UGCAchievement);

	return TRANSACTION_OUTCOME_SUCCESS;
}

AUTO_STRUCT;
typedef struct UGCAchievementEventData
{
	UGCAchievementEvent *pUGCAchievementEvent;
	char *reason;
} UGCAchievementEventData;
extern ParseTable parse_UGCAchievementEventData[];
#define TYPE_parse_UGCAchievementEventData UGCAchievementEventData

void ugcAchievementEvent_Send_SynchronizeAccountAchievementsCB(TransactionReturnVal *pReturn, UGCAchievementEventData *pUGCAchievementEventData)
{
	if(pReturn->eOutcome == TRANSACTION_OUTCOME_SUCCESS && gConf.bUGCAchievementsEnable)
	{
		UGCAchievementEvent *pUGCAchievementEvent = pUGCAchievementEventData->pUGCAchievementEvent;
		const char *reason = pUGCAchievementEventData->reason;
		const UGCAccount* pUGCAccount = NULL;
		UGCAchievementInfo *pAchievementInfo = NULL;

		if(!pUGCAchievementEvent->uUGCAuthorID)
		{
			AssertOrAlertWithStruct("UGC_MALFORMED_INPUT", parse_UGCAchievementEvent, pUGCAchievementEvent, "Reason: %s, in fuction: %s", reason, __FUNCTION__);
			StructDestroy(parse_UGCAchievementEventData, pUGCAchievementEventData);
			return;
		}

		ASSERT_CONTAINER_DATA_UGC("A non-UGCDataManager server tried to get a UGC container!");

		pUGCAccount = objGetContainerData(GLOBALTYPE_UGCACCOUNT, pUGCAchievementEvent->uUGCAuthorID);
		if(!pUGCAccount)
		{
			AssertOrAlertWithStruct("UGC_MISSING_UGCACCOUNT_CONTAINER", parse_UGCAchievementEvent, pUGCAchievementEvent, "Reason: %s, in function: %s", reason, __FUNCTION__);
			StructDestroy(parse_UGCAchievementEventData, pUGCAchievementEventData);
			return;
		}

		FOR_EACH_IN_EARRAY_FORWARDS(pUGCAccount->author.ugcAccountAchievements.eaAchievements, UGCAchievement, ugcAchievement)
		{
			UGCAchievementDef *pUGCAchievementDef = ugcAchievement_DefFromRefString(ugcAchievement->ugcAchievementName);
			if(pUGCAchievementDef)
				// NOTE - to support our ability to increase/decrease the target required to gain an achievement, we support the ability for someone to have reached an
				// achievement at the original target. Therefore, we do not check if count is less than target; we just check the achieved status.
				if(!ugcAchievement->uGrantTime || 0 == pUGCAchievementDef->uTarget) // if Target is zero, we want to count forever, even if previously granted
				{
					U32 increment = ugcAchievement_EventFilter(&pUGCAchievementDef->ugcAchievementFilter, pUGCAchievementEvent);
					if(increment)
						AutoTrans_trUgcAccountAchievementIncrement(LoggedTransactions_CreateManagedReturnVal(__FUNCTION__, NULL, NULL), GetAppGlobalType(),
							GLOBALTYPE_UGCACCOUNT, pUGCAchievementEvent->uUGCAuthorID,
							pUGCAchievementEvent->uUGCAuthorID,
							ugcAchievement->ugcAchievementName,
							increment);
				}
		}
		FOR_EACH_END;
	}

	StructDestroy(parse_UGCAchievementEventData, pUGCAchievementEventData);
}

void ugcAchievementEvent_Send_SynchronizeProjectAchievementsCB(TransactionReturnVal *pReturn, UGCAchievementEventData *pUGCAchievementEventData)
{
	if(pReturn->eOutcome == TRANSACTION_OUTCOME_SUCCESS && gConf.bUGCAchievementsEnable)
	{
		UGCAchievementEvent *pUGCAchievementEvent = pUGCAchievementEventData->pUGCAchievementEvent;
		const char *reason = pUGCAchievementEventData->reason;
		const UGCAccount* pUGCAccount = NULL;
		UGCProjectAchievementInfo *pUGCProjectAchievementInfo = NULL;
		int index;

		if(!pUGCAchievementEvent->uUGCAuthorID || !pUGCAchievementEvent->uUGCProjectID)
		{
			AssertOrAlertWithStruct("UGC_MALFORMED_INPUT", parse_UGCAchievementEvent, pUGCAchievementEvent, "Reason: %s, in function: %s", reason, __FUNCTION__);
			StructDestroy(parse_UGCAchievementEventData, pUGCAchievementEventData);
			return;
		}

		ASSERT_CONTAINER_DATA_UGC("A non-UGCDataManager server tried to get a UGC container!");

		pUGCAccount = objGetContainerData(GLOBALTYPE_UGCACCOUNT, pUGCAchievementEvent->uUGCAuthorID);
		if(!pUGCAccount)
		{
			AssertOrAlertWithStruct("UGC_MISSING_UGCACCOUNT_CONTAINER", parse_UGCAchievementEvent, pUGCAchievementEvent, "Reason: %s, in function: %s", reason, __FUNCTION__);
			StructDestroy(parse_UGCAchievementEventData, pUGCAchievementEventData);
			return;
		}

		index = eaIndexedFindUsingInt(&pUGCAccount->author.eaProjectAchievements, pUGCAchievementEvent->uUGCProjectID);
		if(index < 0)
		{
			AssertOrAlertWithStruct("UGC_MISSING_UGCACHIEVEMENTINFO_FOR_UGCPROJECT", parse_UGCAchievementEvent, pUGCAchievementEvent, "Reason: %s, in function: %s", reason, __FUNCTION__);
			StructDestroy(parse_UGCAchievementEventData, pUGCAchievementEventData);
			return;
		}

		pUGCProjectAchievementInfo = pUGCAccount->author.eaProjectAchievements[index];
		FOR_EACH_IN_EARRAY_FORWARDS(pUGCProjectAchievementInfo->ugcAchievementInfo.eaAchievements, UGCAchievement, ugcAchievement)
		{
			UGCAchievementDef *pUGCAchievementDef = ugcAchievement_DefFromRefString(ugcAchievement->ugcAchievementName);
			if(pUGCAchievementDef)
				// NOTE - to support our ability to increase/decrease the target required to gain an achievement, we support the ability for someone to have reached an
				// achievement at the original target. Therefore, we do not check if count is less than target; we just check the achieved status.
				if(!ugcAchievement->uGrantTime || 0 == pUGCAchievementDef->uTarget) // if Target is zero, we want to count forever, even if previously granted
				{
					U32 increment = ugcAchievement_EventFilter(&pUGCAchievementDef->ugcAchievementFilter, pUGCAchievementEvent);
					if(increment)
					{
						AutoTrans_trUgcProjectAchievementIncrement(LoggedTransactions_CreateManagedReturnVal(__FUNCTION__, NULL, NULL), GetAppGlobalType(),
							GLOBALTYPE_UGCACCOUNT, pUGCAchievementEvent->uUGCAuthorID,
							pUGCAchievementEvent->uUGCAuthorID,
							pUGCAchievementEvent->uUGCProjectID,
							ugcAchievement->ugcAchievementName,
							increment);
					}
				}
		}
		FOR_EACH_END;
	}

	StructDestroy(parse_UGCAchievementEventData, pUGCAchievementEventData);
}

void ugcAchievementEvent_Send_SynchronizeSeriesAchievementsCB(TransactionReturnVal *pReturn, UGCAchievementEventData *pUGCAchievementEventData)
{
	if(pReturn->eOutcome == TRANSACTION_OUTCOME_SUCCESS && gConf.bUGCAchievementsEnable)
	{
		UGCAchievementEvent *pUGCAchievementEvent = pUGCAchievementEventData->pUGCAchievementEvent;
		const char *reason = pUGCAchievementEventData->reason;
		const UGCAccount* pUGCAccount = NULL;
		UGCSeriesAchievementInfo *pUGCSeriesAchievementInfo = NULL;
		int index;

		if(!pUGCAchievementEvent->uUGCAuthorID || !pUGCAchievementEvent->uUGCSeriesID)
		{
			AssertOrAlertWithStruct("UGC_MALFORMED_INPUT", parse_UGCAchievementEvent, pUGCAchievementEvent, "Reason: %s, in function: %s", reason, __FUNCTION__);
			StructDestroy(parse_UGCAchievementEventData, pUGCAchievementEventData);
			return;
		}

		ASSERT_CONTAINER_DATA_UGC("A non-UGCDataManager server tried to get a UGC container!");

		pUGCAccount = objGetContainerData(GLOBALTYPE_UGCACCOUNT, pUGCAchievementEvent->uUGCAuthorID);
		if(!pUGCAccount)
		{
			AssertOrAlertWithStruct("UGC_MISSING_UGCACCOUNT_CONTAINER", parse_UGCAchievementEvent, pUGCAchievementEvent, "Reason: %s, in function: %s", reason, __FUNCTION__);
			StructDestroy(parse_UGCAchievementEventData, pUGCAchievementEventData);
			return;
		}

		index = eaIndexedFindUsingInt(&pUGCAccount->author.eaSeriesAchievements, pUGCAchievementEvent->uUGCSeriesID);
		if(index < 0)
		{
			AssertOrAlertWithStruct("UGC_MISSING_UGCACHIEVEMENTINFO_FOR_UGCPROJECT", parse_UGCAchievementEvent, pUGCAchievementEvent, "Reason: %s, in function: %s", reason, __FUNCTION__);
			StructDestroy(parse_UGCAchievementEventData, pUGCAchievementEventData);
			return;
		}

		pUGCSeriesAchievementInfo = pUGCAccount->author.eaSeriesAchievements[index];
		FOR_EACH_IN_EARRAY_FORWARDS(pUGCSeriesAchievementInfo->ugcAchievementInfo.eaAchievements, UGCAchievement, ugcAchievement)
		{
			UGCAchievementDef *pUGCAchievementDef = ugcAchievement_DefFromRefString(ugcAchievement->ugcAchievementName);
			if(pUGCAchievementDef)
				// NOTE - to support our ability to increase/decrease the target required to gain an achievement, we support the ability for someone to have reached an
				// achievement at the original target. Therefore, we do not check if count is less than target; we just check the achieved status.
				if(!ugcAchievement->uGrantTime || 0 == pUGCAchievementDef->uTarget) // if Target is zero, we want to count forever, even if previously granted
				{
					U32 increment = ugcAchievement_EventFilter(&pUGCAchievementDef->ugcAchievementFilter, pUGCAchievementEvent);
					if(increment)
					{
						AutoTrans_trUgcSeriesAchievementIncrement(LoggedTransactions_CreateManagedReturnVal(__FUNCTION__, NULL, NULL), GetAppGlobalType(),
							GLOBALTYPE_UGCACCOUNT, pUGCAchievementEvent->uUGCAuthorID,
							pUGCAchievementEvent->uUGCAuthorID,
							pUGCAchievementEvent->uUGCSeriesID,
							ugcAchievement->ugcAchievementName,
							increment);
					}
				}
		}
		FOR_EACH_END;
	}

	StructDestroy(parse_UGCAchievementEventData, pUGCAchievementEventData);
}

static void ugcAchievementEvent_Send_Ensure_CB(UGCAccount* pUGCAccount, UGCAchievementEventData *pUGCAchievementEventData)
{
	if(!pUGCAccount)
	{
		AssertOrAlertWithStruct("UGC_MISSING_UGCACCOUNT_CONTAINER", parse_UGCAchievementEvent, pUGCAchievementEventData->pUGCAchievementEvent, "Reason: %s, in function: %s",
								pUGCAchievementEventData->reason, __FUNCTION__);
		StructDestroy(parse_UGCAchievementEventData, pUGCAchievementEventData);
		return;
	}

	AutoTrans_trUgcSynchronizeAccountAchievements(
		LoggedTransactions_CreateManagedReturnVal(__FUNCTION__, ugcAchievementEvent_Send_SynchronizeAccountAchievementsCB, StructClone(parse_UGCAchievementEventData, pUGCAchievementEventData)),
			GLOBALTYPE_UGCDATAMANAGER,
				GLOBALTYPE_UGCACCOUNT, pUGCAchievementEventData->pUGCAchievementEvent->uUGCAuthorID,
				pUGCAchievementEventData->pUGCAchievementEvent->uUGCAuthorID);

	if(pUGCAchievementEventData->pUGCAchievementEvent->uUGCProjectID)
	{
		AutoTrans_trUgcSynchronizeProjectAchievements(
			LoggedTransactions_CreateManagedReturnVal(__FUNCTION__, ugcAchievementEvent_Send_SynchronizeProjectAchievementsCB, StructClone(parse_UGCAchievementEventData, pUGCAchievementEventData)),
				GLOBALTYPE_UGCDATAMANAGER,
					GLOBALTYPE_UGCACCOUNT, pUGCAchievementEventData->pUGCAchievementEvent->uUGCAuthorID,
					pUGCAchievementEventData->pUGCAchievementEvent->uUGCAuthorID,
					pUGCAchievementEventData->pUGCAchievementEvent->uUGCProjectID);
	}

	if(pUGCAchievementEventData->pUGCAchievementEvent->uUGCSeriesID)
	{
		AutoTrans_trUgcSynchronizeSeriesAchievements(
			LoggedTransactions_CreateManagedReturnVal(__FUNCTION__, ugcAchievementEvent_Send_SynchronizeSeriesAchievementsCB, StructClone(parse_UGCAchievementEventData, pUGCAchievementEventData)),
				GLOBALTYPE_UGCDATAMANAGER,
					GLOBALTYPE_UGCACCOUNT, pUGCAchievementEventData->pUGCAchievementEvent->uUGCAuthorID,
					pUGCAchievementEventData->pUGCAchievementEvent->uUGCAuthorID,
					pUGCAchievementEventData->pUGCAchievementEvent->uUGCSeriesID);
	}

	StructDestroy(parse_UGCAchievementEventData, pUGCAchievementEventData);
}

AUTO_COMMAND_REMOTE ACMD_INTERSHARD ACMD_IFDEF(GAMESERVER) ACMD_IFDEF(APPSERVER);
void ugcAchievementEvent_Send(UGCAchievementEvent *pUGCAchievementEvent, const char *reason)
{
	UGCAchievementEventData *pUGCAchievementEventData = NULL;

	if(!s_bUGCAchievementEventsEnable)
		return;

	if(!pUGCAchievementEvent->uUGCAuthorID)
	{
		AssertOrAlertWithStruct("UGC_MALFORMED_INPUT", parse_UGCAchievementEvent, pUGCAchievementEvent, "Reason: %s, in function: %s", reason, __FUNCTION__);
		return;
	}

	pUGCAchievementEventData = StructCreate(parse_UGCAchievementEventData);
	pUGCAchievementEventData->pUGCAchievementEvent = StructClone(parse_UGCAchievementEvent, pUGCAchievementEvent);
	StructCopyString(&pUGCAchievementEventData->reason, reason);
	UGCAccountEnsureExists(pUGCAchievementEvent->uUGCAuthorID, ugcAchievementEvent_Send_Ensure_CB, pUGCAchievementEventData);
}

AUTO_STRUCT;
typedef struct UGCAccountEnsureExistsData
{
	ContainerID uUGCAuthorID;
	ContainerID uUGCProjectID;
	ContainerID uUGCSeriesID;
	char *pcShard;
	ContainerID uUGCEntContainerID;
	char *reason;
	UGCAchievementEvent *pUGCAchievementEvent;
} UGCAccountEnsureExistsData;
extern ParseTable parse_UGCAccountEnsureExistsData[];
#define TYPE_parse_UGCAccountEnsureExistsData UGCAccountEnsureExistsData

static void ugcAchievement_SynchronizeAccountAchievements_Ensure_CB(UGCAccount* pUGCAccount, UGCAccountEnsureExistsData *pUGCAccountEnsureExistsData)
{
	if(!pUGCAccount)
	{
		AssertOrAlert("UGC_MISSING_UGCACCOUNT_CONTAINER", "UGCAuthorID=%d, Reason: %s, in function: %s",
					  pUGCAccountEnsureExistsData->uUGCAuthorID, pUGCAccountEnsureExistsData->reason, __FUNCTION__);
		StructDestroy(parse_UGCAccountEnsureExistsData, pUGCAccountEnsureExistsData);
		return;
	}

	AutoTrans_trUgcSynchronizeAccountAchievements(LoggedTransactions_CreateManagedReturnVal(__FUNCTION__, NULL, NULL), GLOBALTYPE_UGCDATAMANAGER,
		GLOBALTYPE_UGCACCOUNT, pUGCAccountEnsureExistsData->uUGCAuthorID,
		pUGCAccountEnsureExistsData->uUGCAuthorID);

	StructDestroy(parse_UGCAccountEnsureExistsData, pUGCAccountEnsureExistsData);
}

AUTO_COMMAND_REMOTE ACMD_INTERSHARD ACMD_IFDEF(GAMESERVER) ACMD_IFDEF(APPSERVER);
void ugcAchievement_SynchronizeAccountAchievements(const char *shard, ContainerID uUGCAuthorID, const char *reason)
{
	UGCAccountEnsureExistsData *pUGCAccountEnsureExistsData = NULL;

	if(!uUGCAuthorID)
	{
		AssertOrAlert("UGC_MALFORMED_INPUT", "UGCAuthorID=%d, Reason: %s, in function: %s", uUGCAuthorID, reason, __FUNCTION__);
		return;
	}

	pUGCAccountEnsureExistsData = StructCreate(parse_UGCAccountEnsureExistsData);
	pUGCAccountEnsureExistsData->pcShard = StructAllocString(shard);
	pUGCAccountEnsureExistsData->uUGCAuthorID = uUGCAuthorID;
	StructCopyString(&pUGCAccountEnsureExistsData->reason, reason);
	UGCAccountEnsureExists(uUGCAuthorID, ugcAchievement_SynchronizeAccountAchievements_Ensure_CB, pUGCAccountEnsureExistsData);
}

static void ugcAchievement_SynchronizeProjectAchievements_Ensure_CB(UGCAccount* pUGCAccount, UGCAccountEnsureExistsData *pUGCAccountEnsureExistsData)
{
	if(!pUGCAccount)
	{
		AssertOrAlert("UGC_MISSING_UGCACCOUNT_CONTAINER", "UGCAuthorID=%d, UGCProjectID=%d, Reason: %s, for function: %s",
					  pUGCAccountEnsureExistsData->uUGCAuthorID, pUGCAccountEnsureExistsData->uUGCProjectID, pUGCAccountEnsureExistsData->reason, __FUNCTION__);
		StructDestroy(parse_UGCAccountEnsureExistsData, pUGCAccountEnsureExistsData);
		return;
	}

	AutoTrans_trUgcSynchronizeProjectAchievements(LoggedTransactions_CreateManagedReturnVal(__FUNCTION__, NULL, NULL), GLOBALTYPE_UGCDATAMANAGER,
		GLOBALTYPE_UGCACCOUNT, pUGCAccountEnsureExistsData->uUGCAuthorID,
		pUGCAccountEnsureExistsData->uUGCAuthorID,
		pUGCAccountEnsureExistsData->uUGCProjectID);

	StructDestroy(parse_UGCAccountEnsureExistsData, pUGCAccountEnsureExistsData);
}

AUTO_COMMAND_REMOTE ACMD_INTERSHARD ACMD_IFDEF(GAMESERVER) ACMD_IFDEF(APPSERVER);
void ugcAchievement_SynchronizeProjectAchievements(const char *shard, ContainerID uUGCAuthorID, ContainerID uUGCProjectID, const char *reason)
{
	UGCAccountEnsureExistsData *pUGCAccountEnsureExistsData = NULL;

	if(!uUGCAuthorID || !uUGCProjectID)
	{
		AssertOrAlert("UGC_MALFORMED_INPUT", "UGCAuthorID=%d, UGCProjectID=%d, Reason: %s, for function: %s", uUGCAuthorID, uUGCProjectID, reason, __FUNCTION__);
		return;
	}

	pUGCAccountEnsureExistsData = StructCreate(parse_UGCAccountEnsureExistsData);
	pUGCAccountEnsureExistsData->pcShard = StructAllocString(shard);
	pUGCAccountEnsureExistsData->uUGCAuthorID = uUGCAuthorID;
	pUGCAccountEnsureExistsData->uUGCProjectID = uUGCProjectID;
	StructCopyString(&pUGCAccountEnsureExistsData->reason, reason);
	UGCAccountEnsureExists(uUGCAuthorID, ugcAchievement_SynchronizeProjectAchievements_Ensure_CB, pUGCAccountEnsureExistsData);
}

static void ugcAchievement_SynchronizeSeriesAchievements_Ensure_CB(UGCAccount* pUGCAccount, UGCAccountEnsureExistsData *pUGCAccountEnsureExistsData)
{
	if(!pUGCAccount)
	{
		AssertOrAlert("UGC_MISSING_UGCACCOUNT_CONTAINER", "UGCAuthorID=%d, UGCSeriesID=%d, Reason: %s, for function: %s",
					  pUGCAccountEnsureExistsData->uUGCAuthorID, pUGCAccountEnsureExistsData->uUGCSeriesID, pUGCAccountEnsureExistsData->reason, __FUNCTION__);
		StructDestroy(parse_UGCAccountEnsureExistsData, pUGCAccountEnsureExistsData);
		return;
	}

	AutoTrans_trUgcSynchronizeSeriesAchievements(LoggedTransactions_CreateManagedReturnVal(__FUNCTION__, NULL, NULL), GLOBALTYPE_UGCDATAMANAGER,
		GLOBALTYPE_UGCACCOUNT, pUGCAccountEnsureExistsData->uUGCAuthorID,
		pUGCAccountEnsureExistsData->uUGCAuthorID,
		pUGCAccountEnsureExistsData->uUGCSeriesID);

	StructDestroy(parse_UGCAccountEnsureExistsData, pUGCAccountEnsureExistsData);
}

AUTO_COMMAND_REMOTE ACMD_INTERSHARD ACMD_IFDEF(GAMESERVER) ACMD_IFDEF(APPSERVER);
void ugcAchievement_SynchronizeSeriesAchievements(const char *shard, ContainerID uUGCAuthorID, ContainerID uUGCSeriesID, const char *reason)
{
	UGCAccountEnsureExistsData *pUGCAccountEnsureExistsData = NULL;

	if(!uUGCAuthorID || !uUGCSeriesID)
	{
		AssertOrAlert("UGC_MALFORMED_INPUT", "UGCAuthorID=%d, UGCSeriesID=%d, Reason: %s, for function: %s", uUGCAuthorID, uUGCSeriesID, reason, __FUNCTION__);
		return;
	}

	pUGCAccountEnsureExistsData = StructCreate(parse_UGCAccountEnsureExistsData);
	pUGCAccountEnsureExistsData->pcShard = StructAllocString(shard);
	pUGCAccountEnsureExistsData->uUGCAuthorID = uUGCAuthorID;
	pUGCAccountEnsureExistsData->uUGCSeriesID = uUGCSeriesID;
	StructCopyString(&pUGCAccountEnsureExistsData->reason, reason);
	UGCAccountEnsureExists(uUGCAuthorID, ugcAchievement_SynchronizeSeriesAchievements_Ensure_CB, pUGCAccountEnsureExistsData);
}

static void trUgcSynchronizeAchievements_CB(TransactionReturnVal *returnVal, UGCAccountEnsureExistsData *pUGCAccountEnsureExistsData)
{
	ASSERT_CONTAINER_DATA_UGC("A non-UGCDataManager server tried to get a UGC container!");

	if(TRANSACTION_OUTCOME_SUCCESS == returnVal->eOutcome)
	{
		if(pUGCAccountEnsureExistsData->uUGCEntContainerID)
		{
			UGCAccount *pUGCAccount = objGetContainerData(GLOBALTYPE_UGCACCOUNT, pUGCAccountEnsureExistsData->uUGCAuthorID);
			if(!pUGCAccount)
			{
				AssertOrAlert("UGC_MISSING_UGCACCOUNT_CONTAINER", "UGCAuthorID=%d, Reason: %s, for function: %s", pUGCAccountEnsureExistsData->uUGCAuthorID, pUGCAccountEnsureExistsData->reason, __FUNCTION__);
				return;
			}

			if(pUGCAccountEnsureExistsData->pcShard && pUGCAccountEnsureExistsData->uUGCEntContainerID) // was initiated by the author, not an event caused by another player
			{
				NOCONST(UGCAccount)* clone = ugcAccountClonePersistedAndSubscribedDataOnly(CONTAINER_NOCONST(UGCAccount, pUGCAccount));

				RemoteCommand_Intershard_gslUGC_ProvideAccount(pUGCAccountEnsureExistsData->pcShard, GLOBALTYPE_ENTITYPLAYER, pUGCAccountEnsureExistsData->uUGCEntContainerID,
					pUGCAccountEnsureExistsData->uUGCEntContainerID,
					CONTAINER_RECONST(UGCAccount, clone));

				StructDestroyNoConst(parse_UGCAccount, clone);
			}
		}
	}

	StructDestroy(parse_UGCAccountEnsureExistsData, pUGCAccountEnsureExistsData);
}

static void ugcAchievement_SynchronizeAchievements_Ensure_CB(UGCAccount* pUGCAccount, UGCAccountEnsureExistsData *pUGCAccountEnsureExistsData)
{
	if(!pUGCAccount)
	{
		AssertOrAlert("UGC_MISSING_UGCACCOUNT_CONTAINER", "UGCAuthorID=%d, UGCSeriesID=%d, Reason: %s, for function: %s",
					  pUGCAccountEnsureExistsData->uUGCAuthorID, pUGCAccountEnsureExistsData->uUGCSeriesID, pUGCAccountEnsureExistsData->reason, __FUNCTION__);
		StructDestroy(parse_UGCAccountEnsureExistsData, pUGCAccountEnsureExistsData);
		return;
	}

	AutoTrans_trUgcSynchronizeAchievements(LoggedTransactions_CreateManagedReturnVal(__FUNCTION__, trUgcSynchronizeAchievements_CB, pUGCAccountEnsureExistsData), GLOBALTYPE_UGCDATAMANAGER,
		GLOBALTYPE_UGCACCOUNT, pUGCAccountEnsureExistsData->uUGCAuthorID,
		pUGCAccountEnsureExistsData->uUGCAuthorID);
}

AUTO_COMMAND_REMOTE ACMD_INTERSHARD ACMD_IFDEF(GAMESERVER) ACMD_IFDEF(APPSERVER);
void ugcAchievement_SynchronizeAchievements(UGCSynchronizeAchievementsData *pUGCSynchronizeAchievementsData, const char *reason)
{
	UGCAccountEnsureExistsData *pUGCAccountEnsureExistsData = NULL;

	if(!pUGCSynchronizeAchievementsData->ugcAccountID)
	{
		AssertOrAlert("UGC_MALFORMED_INPUT", "UGCAuthorID=%d, Reason: %s, for function: %s", pUGCSynchronizeAchievementsData->ugcAccountID, reason, __FUNCTION__);
		return;
	}

	pUGCAccountEnsureExistsData = StructCreate(parse_UGCAccountEnsureExistsData);
	pUGCAccountEnsureExistsData->pcShard = StructAllocString(pUGCSynchronizeAchievementsData->pcShard);
	pUGCAccountEnsureExistsData->uUGCAuthorID = pUGCSynchronizeAchievementsData->ugcAccountID;
	pUGCAccountEnsureExistsData->uUGCEntContainerID = pUGCSynchronizeAchievementsData->entContainerID;
	StructCopyString(&pUGCAccountEnsureExistsData->reason, reason);
	UGCAccountEnsureExists(pUGCAccountEnsureExistsData->uUGCAuthorID, ugcAchievement_SynchronizeAchievements_Ensure_CB, pUGCAccountEnsureExistsData);
}

AUTO_COMMAND_REMOTE ACMD_IFDEF(GAMESERVER) ACMD_IFDEF(APPSERVER);
void ugcAchievement_RemoveAccountAchievements(ContainerID uUGCAuthorID, const char *reason)
{
	const UGCAccount* pUGCAccount = NULL;
	UGCAchievementEventData *pUGCAchievementEventData = NULL;

	if(!uUGCAuthorID)
	{
		AssertOrAlert("UGC_MALFORMED_INPUT", "UGCAuthorID=%d, Reason: %s, for function: %s", uUGCAuthorID, reason, __FUNCTION__);
		return;
	}

	ASSERT_CONTAINER_DATA_UGC("A non-UGCDataManager server tried to get a UGC container!");

	pUGCAccount = objGetContainerData(GLOBALTYPE_UGCACCOUNT, uUGCAuthorID);
	if(!pUGCAccount)
	{
		AssertOrAlert("UGC_MISSING_UGCACCOUNT_CONTAINER", "UGCAuthorID=%d, Reason: %s, for function: %s", uUGCAuthorID, reason, __FUNCTION__);
		return;
	}

	AutoTrans_trUgcRemoveAccountAchievements(
		LoggedTransactions_CreateManagedReturnVal(__FUNCTION__, NULL, NULL), GLOBALTYPE_UGCDATAMANAGER,
		GLOBALTYPE_UGCACCOUNT, uUGCAuthorID);
}

AUTO_COMMAND_REMOTE ACMD_IFDEF(GAMESERVER) ACMD_IFDEF(APPSERVER);
void ugcAchievement_RemoveProjectAchievements(ContainerID uUGCAuthorID, ContainerID uUGCProjectID, const char *reason)
{
	const UGCAccount* pUGCAccount = NULL;
	UGCAchievementEventData *pUGCAchievementEventData = NULL;

	if(!uUGCAuthorID || !uUGCProjectID)
	{
		AssertOrAlert("UGC_MALFORMED_INPUT", "UGCAuthorID=%d, UGCProjectID=%d, Reason: %s, for function: %s", uUGCAuthorID, uUGCProjectID, reason, __FUNCTION__);
		return;
	}

	ASSERT_CONTAINER_DATA_UGC("A non-UGCDataManager server tried to get a UGC container!");

	pUGCAccount = objGetContainerData(GLOBALTYPE_UGCACCOUNT, uUGCAuthorID);
	if(!pUGCAccount)
	{
		AssertOrAlert("UGC_MISSING_UGCACCOUNT_CONTAINER", "UGCAuthorID=%d, UGCProjectID=%d, Reason: %s, for function: %s", uUGCAuthorID, uUGCProjectID, reason, __FUNCTION__);
		return;
	}

	AutoTrans_trUgcRemoveProjectAchievements(
		LoggedTransactions_CreateManagedReturnVal(__FUNCTION__, NULL, NULL), GLOBALTYPE_UGCDATAMANAGER,
			GLOBALTYPE_UGCACCOUNT, uUGCAuthorID,
			uUGCProjectID);
}

AUTO_COMMAND_REMOTE ACMD_IFDEF(GAMESERVER) ACMD_IFDEF(APPSERVER);
void ugcAchievement_RemoveSeriesAchievements(ContainerID uUGCAuthorID, ContainerID uUGCSeriesID, const char *reason)
{
	const UGCAccount* pUGCAccount = NULL;
	UGCAchievementEventData *pUGCAchievementEventData = NULL;

	if(!uUGCAuthorID || !uUGCSeriesID)
	{
		AssertOrAlert("UGC_MALFORMED_INPUT", "UGCAuthorID=%d, UGCProjectID=%d, Reason: %s, for function: %s", uUGCAuthorID, uUGCSeriesID, reason, __FUNCTION__);
		return;
	}

	ASSERT_CONTAINER_DATA_UGC("A non-UGCDataManager server tried to get a UGC container!");

	pUGCAccount = objGetContainerData(GLOBALTYPE_UGCACCOUNT, uUGCAuthorID);
	if(!pUGCAccount)
	{
		AssertOrAlert("UGC_MISSING_UGCACCOUNT_CONTAINER", "UGCAuthorID=%d, UGCProjectID=%d, Reason: %s, for function: %s", uUGCAuthorID, uUGCSeriesID, reason, __FUNCTION__);
		return;
	}

	AutoTrans_trUgcRemoveSeriesAchievements(NULL, GLOBALTYPE_UGCDATAMANAGER,
		GLOBALTYPE_UGCACCOUNT, uUGCAuthorID,
		uUGCSeriesID);
}

AUTO_COMMAND_REMOTE ACMD_IFDEF(GAMESERVER) ACMD_IFDEF(APPSERVER) ACMD_ACCESSLEVEL(7);
void ugcAchievement_Reset(ContainerID uUGCAuthorID, const char *reason)
{
	const UGCAccount* pUGCAccount = objGetContainerData(GLOBALTYPE_UGCACCOUNT, uUGCAuthorID);
	ASSERT_CONTAINER_DATA_UGC("A non-UGCDataManager server tried to get a UGC container!");
	if(!pUGCAccount)
	{
		AssertOrAlert("UGC_MISSING_UGCACCOUNT_CONTAINER", "UGCAccount id: %d, Reason: %s, for function: %s", uUGCAuthorID, reason, __FUNCTION__);
		return;
	}

	AutoTrans_trUgcResetAchievements(NULL, GLOBALTYPE_UGCDATAMANAGER,
		GLOBALTYPE_UGCACCOUNT, uUGCAuthorID);
}

AUTO_COMMAND_REMOTE ACMD_INTERSHARD ACMD_IFDEF(GAMESERVER) ACMD_IFDEF(APPSERVER) ACMD_ACCESSLEVEL(7);
void ugcAchievement_Grant(ContainerID uUGCAuthorID, ContainerID uUGCProjectID, ContainerID uUGCSeriesID, const char *ugcAchievementName, const char *reason)
{
	if(gConf.bUGCAchievementsEnable)
	{
		const UGCAccount* pUGCAccount = NULL;

		if(!uUGCAuthorID)
		{
			AssertOrAlert("UGC_MALFORMED_INPUT", "UGCAuthorID=%d, Reason: %s, for function: %s", uUGCAuthorID, reason, __FUNCTION__);
			return;
		}

		ASSERT_CONTAINER_DATA_UGC("A non-UGCDataManager server tried to get a UGC container!");

		pUGCAccount = objGetContainerData(GLOBALTYPE_UGCACCOUNT, uUGCAuthorID);
		if(!pUGCAccount)
		{
			AssertOrAlert("UGC_MISSING_UGCACCOUNT_CONTAINER", "UGCAccount id: %d, Reason: %s, for function: %s", uUGCAuthorID, reason, __FUNCTION__);
			return;
		}

		if(uUGCProjectID)
		{
			int index;
			NOCONST(UGCAchievement) *pUGCAchievement = NULL;
			NOCONST(UGCProjectAchievementInfo) *pUGCProjectAchievementInfo = eaIndexedGetUsingInt(&pUGCAccount->author.eaProjectAchievements, uUGCProjectID);
			if(!pUGCProjectAchievementInfo)
			{
				AssertOrAlert("UGC_PROJECT_ACHIEVEMENT_INFO_NOT_FOUND", "UGCAccount id: %d, UGCProject id: %d, for function: %s", pUGCAccount->accountID, uUGCProjectID, __FUNCTION__);
				return;
			}

			index = eaIndexedFindUsingString(&pUGCProjectAchievementInfo->ugcAchievementInfo.eaAchievements, ugcAchievementName);
			if(index < 0)
			{
				AssertOrAlert("UGC_PROJECT_ACHIEVEMENT_NOT_FOUND", "UGCAccount id: %d, UGCProject id: %d, Achievement Name: %s, for function: %s", pUGCAccount->accountID, uUGCProjectID, ugcAchievementName, __FUNCTION__);
				return;
			}

			pUGCAchievement = pUGCProjectAchievementInfo->ugcAchievementInfo.eaAchievements[index];
			if(!pUGCAchievement->uGrantTime)
			{
				AutoTrans_trUgcProjectAchievementGrant(LoggedTransactions_CreateManagedReturnVal(__FUNCTION__, NULL, NULL), GLOBALTYPE_UGCDATAMANAGER,
					GLOBALTYPE_UGCACCOUNT, uUGCAuthorID,
					uUGCAuthorID,
					uUGCProjectID,
					ugcAchievementName,
					reason);
			}
		}
		else if(uUGCSeriesID)
		{
			int index;
			NOCONST(UGCAchievement) *pUGCAchievement = NULL;
			NOCONST(UGCSeriesAchievementInfo) *pUGCSeriesAchievementInfo = eaIndexedGetUsingInt(&pUGCAccount->author.eaSeriesAchievements, uUGCSeriesID);
			if(!pUGCSeriesAchievementInfo)
			{
				AssertOrAlert("UGC_SERIES_ACHIEVEMENT_INFO_NOT_FOUND", "UGCAccount id: %d, UGCProject id: %d, for function: %s", pUGCAccount->accountID, uUGCSeriesID, __FUNCTION__);
				return;
			}

			index = eaIndexedFindUsingString(&pUGCSeriesAchievementInfo->ugcAchievementInfo.eaAchievements, ugcAchievementName);
			if(index < 0)
			{
				AssertOrAlert("UGC_SERIES_ACHIEVEMENT_NOT_FOUND", "UGCAccount id: %d, UGCProject id: %d, Achievement Name: %s, for function: %s", pUGCAccount->accountID, uUGCSeriesID, ugcAchievementName, __FUNCTION__);
				return;
			}

			pUGCAchievement = pUGCSeriesAchievementInfo->ugcAchievementInfo.eaAchievements[index];
			if(!pUGCAchievement->uGrantTime)
			{
				AutoTrans_trUgcSeriesAchievementGrant(LoggedTransactions_CreateManagedReturnVal(__FUNCTION__, NULL, NULL), GLOBALTYPE_UGCDATAMANAGER,
					GLOBALTYPE_UGCACCOUNT, uUGCAuthorID,
					uUGCAuthorID,
					uUGCSeriesID,
					ugcAchievementName,
					reason);
			}
		}
		else
		{
			UGCAchievement *pUGCAchievement = NULL;
			int index = eaIndexedFindUsingString(&pUGCAccount->author.ugcAccountAchievements.eaAchievements, ugcAchievementName);
			if(index < 0)
			{
				AssertOrAlert("UGC_ACCOUNT_ACHIEVEMENT_NOT_FOUND", "UGCAccount id: %d, Achievement Name: %s, for function: %s", pUGCAccount->accountID, ugcAchievementName, __FUNCTION__);
				return;
			}

			pUGCAchievement = pUGCAccount->author.ugcAccountAchievements.eaAchievements[index];
			if(!pUGCAchievement->uGrantTime)
			{
				AutoTrans_trUgcAccountAchievementGrant(LoggedTransactions_CreateManagedReturnVal(__FUNCTION__, NULL, NULL), GLOBALTYPE_UGCDATAMANAGER,
					GLOBALTYPE_UGCACCOUNT, uUGCAuthorID,
					uUGCAuthorID,
					ugcAchievementName,
					reason);
			}
		}
	}
}

#include "aslUGCDataManagerAchievements_c_ast.c"
