/***************************************************************************
*     Copyright (c) 2012, Cryptic Studios
*     All Rights Reserved
*     Confidential Property of Cryptic Studios
***************************************************************************/
#include "UICore.h"
#include "UISkin.h"
#include "UIWindow.h"
#include "UIScrollbar.h"
#include "UILabel.h"
#include "UIButton.h"
#include "UICheckButton.h"
#include "UITree.h"
#include "earray.h"
#include "Prefs.h"
#include "cmdparse.h"
#include "GfxFont.h"
#include "GfxSpriteText.h"
#include "GfxClipper.h"
#include "CBox.h"
#include "StringFormat.h"

#include "Entity.h"
#include "Player.h"
#include "ReferenceSystem.h"

#include "UGCCommon.h"
#include "UGCProjectCommon.h"
#include "UGCAchievements.h"

#include "gclEntity.h"

#include "AutoGen/GameServerLib_autogen_ServerCmdWrappers.h"

GCC_SYSTEM

#ifndef NO_EDITORS

AUTO_RUN_ANON(memBudgetAddMapping(__FILE__, BUDGET_UISystem););

//
// Root UGC Achievements Debug UI Structure
//
typedef struct UGCAchievementsDebugUI {
	UIWindow *mainWindow;

	UITree *achievementTree;
} UGCAchievementsDebugUI;

static UGCAchievementsDebugUI achDebugUI = { 0 };

AUTO_STRUCT;
typedef struct UGCAchievementsNodeData {
	ContainerID projectID;
	ContainerID seriesID;

	const char *ugcAchievementName;	AST( POOL_STRING )
} UGCAchievementsNodeData;
extern ParseTable parse_UGCAchievementsNodeData[];
#define TYPE_parse_UGCAchievementsNodeData UGCAchievementsNodeData

static void FreeUGCAchievementsNodeDataNode(UITreeNode *node)
{
	if(node->contents)
	{
		UGCAchievementsNodeData *pData = node->contents;
		StructDestroy(parse_UGCAchievementsNodeData, pData);
		node->contents = NULL;
	}
}

AUTO_COMMAND ACMD_PRIVATE ACMD_CLIENTCMD ACMD_ACCESSLEVEL(7);
void achDebugAccountChangeCB()
{
	if(achDebugUI.achievementTree)
		ui_TreeRefresh(achDebugUI.achievementTree);
}

static bool achDebugUICloseCallback(UIAnyWidget *widget, UserData userdata_unused)
{
	GamePrefStoreFloat("UGCAchievements.x", ui_WidgetGetX((UIWidget*)widget));
	GamePrefStoreFloat("UGCAchievements.y", ui_WidgetGetY((UIWidget*)widget));
	GamePrefStoreFloat("UGCAchievements.w", ui_WidgetGetWidth((UIWidget*)widget));
	GamePrefStoreFloat("UGCAchievements.h", ui_WidgetGetHeight((UIWidget*)widget));

	ZeroStruct(&achDebugUI);

	return 1;
}

static void ugcAchievementsDebug_DisplayAccountAchievementCB(UITreeNode *achievement, UserData data, UI_MY_ARGS, F32 z)
{
	UGCAchievementsNodeData *pData = (UGCAchievementsNodeData *)achievement->contents;
	if(pData && pData->ugcAchievementName)
	{
		UGCAchievementDef *pUGCAchievementDef = ugcAchievement_DefFromRefString(pData->ugcAchievementName);
		if(pUGCAchievementDef)
		{
			UGCAccount *pUGCAccount = entGetUGCAccount(entActivePlayerPtr());
			if(pUGCAccount)
			{
				int achIndex = eaIndexedFindUsingString(&pUGCAccount->author.ugcAccountAchievements.eaAchievements, pData->ugcAchievementName);
				if(achIndex >= 0)
				{
					UGCAchievement *pUGCAchievement = pUGCAccount->author.ugcAccountAchievements.eaAchievements[achIndex];
					char *display = NULL;

					if(ugcAchievement_IsHidden(pUGCAchievementDef))
						estrPrintf(&display, "HIDDEN %s", pUGCAchievementDef->name);
					else
					{
						char *description = NULL;

						FormatDisplayMessage(&description, pUGCAchievementDef->descriptionMsg, STRFMT_INT("Target", pUGCAchievementDef->uTarget), STRFMT_END);

						if(description)
							estrPrintf(&display, "%s - %s", TranslateDisplayMessage(pUGCAchievementDef->nameMsg), description);
						else
							estrPrintf(&display, "%s", TranslateDisplayMessage(pUGCAchievementDef->nameMsg));

						estrDestroy(&description);
					}

					if(0 == eaSize(&pUGCAchievementDef->subAchievements))
						// NOTE - to support our ability to increase/decrease the target required to gain an achievement, we support the ability for someone to have reached an
						// achievement at the original target. Therefore, we display the count as the target if already achieved.
						estrConcatf(&display, " %d/%d", pUGCAchievement->uCount, pUGCAchievement->uGrantTime ? pUGCAchievement->uCount : pUGCAchievementDef->uTarget);

					if(pUGCAchievement->uGrantTime)
					{
						U32 now = timeSecondsSince2000();
						if(now > pUGCAchievement->uGrantTime)
						{
							char *estrGrantedStr = NULL;
							U32 uGrantedAgo = now - pUGCAchievement->uGrantTime;
							timeSecondsDurationToShortEString(uGrantedAgo, &estrGrantedStr);
							estrConcatf(&display, " GRANTED %s ago", estrGrantedStr);
							estrDestroy(&estrGrantedStr);
						}
						else
							estrConcatf(&display, " GRANTED now");
					}

					if(pUGCAchievementDef->bRepeatable)
					{
						U32 uCooldownDuration = pUGCAchievementDef->uRepeatCooldownHours * 60 * 60;
						U32 uGrantTime = ugcAchievement_GetCooldownBlockGrantTime(pUGCAchievement->uGrantTime, pUGCAchievementDef);
						U32 now = timeSecondsSince2000();
						if(now < uGrantTime + uCooldownDuration)
						{
							U32 uRepeatIn = uGrantTime + uCooldownDuration - now;
							char *estrRepeatInStr = NULL;
							timeSecondsDurationToShortEString(uRepeatIn, &estrRepeatInStr);
							estrConcatf(&display, " REPEATABLE in %s", estrRepeatInStr);
							estrDestroy(&estrRepeatInStr);
						}
						else
							estrConcatf(&display, " REPEATABLE now");
					}

					ui_TreeDisplayText(achievement, display, UI_MY_VALUES, z);
					estrDestroy(&display);
				}
			}
		}
	}
}

static void ugcAchievementsDebug_FillAccountAchievementCB(UITreeNode *achievement, UserData data)
{
	UGCAchievementsNodeData *pData = (UGCAchievementsNodeData *)achievement->contents;
	if(pData && pData->ugcAchievementName)
	{
		UGCAchievementDef *pUGCAchievementDef = ugcAchievement_DefFromRefString(pData->ugcAchievementName);
		if(pUGCAchievementDef)
		{
			UGCAccount *pUGCAccount = entGetUGCAccount(entActivePlayerPtr());
			if(pUGCAccount)
			{
				int achIndex = eaIndexedFindUsingString(&pUGCAccount->author.ugcAccountAchievements.eaAchievements, pData->ugcAchievementName);
				if(achIndex >= 0)
				{
					UGCAchievement *pUGCAchievement = pUGCAccount->author.ugcAccountAchievements.eaAchievements[achIndex];
					FOR_EACH_IN_EARRAY_FORWARDS(pUGCAchievementDef->subAchievements, UGCAchievementDef, pSubUGCAchievementDef)
					{
						UITreeNode* node = NULL;
						pData = StructCreate(parse_UGCAchievementsNodeData);
						pData->ugcAchievementName = pSubUGCAchievementDef->pchRefString;
						node = ui_TreeNodeCreate(achievement->tree, (U32)pSubUGCAchievementDef->name, parse_UGCAchievementsNodeData, (UserData)pData,
							eaSize(&pSubUGCAchievementDef->subAchievements) ? ugcAchievementsDebug_FillAccountAchievementCB : NULL, NULL,
							ugcAchievementsDebug_DisplayAccountAchievementCB, NULL,
							20);
						ui_TreeNodeSetFreeCallback(node, FreeUGCAchievementsNodeDataNode);
						ui_TreeNodeAddChild(achievement, node);
					}
					FOR_EACH_END;
				}
			}
		}
	}
}

static void ugcAchievementsDebug_DisplayProjectAchievementCB(UITreeNode *achievement, UserData data, UI_MY_ARGS, F32 z)
{
	UGCAchievementsNodeData *pData = (UGCAchievementsNodeData *)achievement->contents;
	if(pData && pData->ugcAchievementName && pData->projectID)
	{
		UGCAchievementDef *pUGCAchievementDef = ugcAchievement_DefFromRefString(pData->ugcAchievementName);
		if(pUGCAchievementDef)
		{
			UGCAccount *pUGCAccount = entGetUGCAccount(entActivePlayerPtr());
			if(pUGCAccount)
			{
				UGCProjectAchievementInfo *pUGCProjectAchievementInfo = eaIndexedGetUsingInt(&pUGCAccount->author.eaProjectAchievements, pData->projectID);
				if(pUGCProjectAchievementInfo)
				{
					int achIndex = eaIndexedFindUsingString(&pUGCProjectAchievementInfo->ugcAchievementInfo.eaAchievements, pData->ugcAchievementName);
					if(achIndex >= 0)
					{
						UGCAchievement *pUGCAchievement = pUGCProjectAchievementInfo->ugcAchievementInfo.eaAchievements[achIndex];
						char *display = NULL;

						if(ugcAchievement_IsHidden(pUGCAchievementDef))
							estrPrintf(&display, "HIDDEN %s", pUGCAchievementDef->name);
						else
						{
							char *description = NULL;

							FormatDisplayMessage(&description, pUGCAchievementDef->descriptionMsg, STRFMT_INT("Target", pUGCAchievementDef->uTarget), STRFMT_END);

							if(description)
								estrPrintf(&display, "%s - %s", TranslateDisplayMessage(pUGCAchievementDef->nameMsg), description);
							else
								estrPrintf(&display, "%s", TranslateDisplayMessage(pUGCAchievementDef->nameMsg));

							estrDestroy(&description);
						}

						if(0 == eaSize(&pUGCAchievementDef->subAchievements))
							// NOTE - to support our ability to increase/decrease the target required to gain an achievement, we support the ability for someone to have reached an
							// achievement at the original target. Therefore, we display the count as the target if already achieved.
							estrConcatf(&display, " %d/%d", pUGCAchievement->uCount, pUGCAchievement->uGrantTime ? pUGCAchievement->uCount : pUGCAchievementDef->uTarget);

						if(pUGCAchievement->uGrantTime)
						{
							U32 now = timeSecondsSince2000();
							if(now > pUGCAchievement->uGrantTime)
							{
								char *estrGrantedStr = NULL;
								U32 uGrantedAgo = now - pUGCAchievement->uGrantTime;
								timeSecondsDurationToShortEString(uGrantedAgo, &estrGrantedStr);
								estrConcatf(&display, " GRANTED %s ago", estrGrantedStr);
								estrDestroy(&estrGrantedStr);
							}
							else
								estrConcatf(&display, " GRANTED now");
						}

						if(pUGCAchievementDef->bRepeatable)
						{
							U32 uCooldownDuration = pUGCAchievementDef->uRepeatCooldownHours * 60 * 60;
							U32 uGrantTime = ugcAchievement_GetCooldownBlockGrantTime(pUGCAchievement->uGrantTime, pUGCAchievementDef);
							U32 now = timeSecondsSince2000();
							if(now <= uGrantTime + uCooldownDuration)
							{
								U32 uRepeatIn = uGrantTime + uCooldownDuration - now;
								char *estrRepeatInStr = NULL;
								timeSecondsDurationToShortEString(uRepeatIn, &estrRepeatInStr);
								estrConcatf(&display, " REPEATABLE in %s", estrRepeatInStr);
								estrDestroy(&estrRepeatInStr);
							}
							else
								estrConcatf(&display, " REPEATABLE now");
						}

						ui_TreeDisplayText(achievement, display, UI_MY_VALUES, z);
						estrDestroy(&display);
					}
				}
			}
		}
	}
}

static void ugcAchievementsDebug_DisplaySeriesAchievementCB(UITreeNode *achievement, UserData data, UI_MY_ARGS, F32 z)
{
	UGCAchievementsNodeData *pData = (UGCAchievementsNodeData *)achievement->contents;
	if(pData && pData->ugcAchievementName && pData->seriesID)
	{
		UGCAchievementDef *pUGCAchievementDef = ugcAchievement_DefFromRefString(pData->ugcAchievementName);
		if(pUGCAchievementDef)
		{
			UGCAccount *pUGCAccount = entGetUGCAccount(entActivePlayerPtr());
			if(pUGCAccount)
			{
				UGCSeriesAchievementInfo *pUGCSeriesAchievementInfo = eaIndexedGetUsingInt(&pUGCAccount->author.eaSeriesAchievements, pData->seriesID);
				if(pUGCSeriesAchievementInfo)
				{
					int achIndex = eaIndexedFindUsingString(&pUGCSeriesAchievementInfo->ugcAchievementInfo.eaAchievements, pData->ugcAchievementName);
					if(achIndex >= 0)
					{
						UGCAchievement *pUGCAchievement = pUGCSeriesAchievementInfo->ugcAchievementInfo.eaAchievements[achIndex];
						char *display = NULL;

						if(ugcAchievement_IsHidden(pUGCAchievementDef))
							estrPrintf(&display, "HIDDEN %s", pUGCAchievementDef->name);
						else
						{
							char *description = NULL;

							FormatDisplayMessage(&description, pUGCAchievementDef->descriptionMsg, STRFMT_INT("Target", pUGCAchievementDef->uTarget), STRFMT_END);

							if(description)
								estrPrintf(&display, "%s - %s", TranslateDisplayMessage(pUGCAchievementDef->nameMsg), description);
							else
								estrPrintf(&display, "%s", TranslateDisplayMessage(pUGCAchievementDef->nameMsg));

							estrDestroy(&description);
						}

						if(0 == eaSize(&pUGCAchievementDef->subAchievements))
							// NOTE - to support our ability to increase/decrease the target required to gain an achievement, we support the ability for someone to have reached an
							// achievement at the original target. Therefore, we display the count as the target if already achieved.
							estrConcatf(&display, " %d/%d", pUGCAchievement->uCount, pUGCAchievement->uGrantTime ? pUGCAchievement->uCount : pUGCAchievementDef->uTarget);

						if(pUGCAchievement->uGrantTime)
						{
							U32 now = timeSecondsSince2000();
							if(now > pUGCAchievement->uGrantTime)
							{
								char *estrGrantedStr = NULL;
								U32 uGrantedAgo = now - pUGCAchievement->uGrantTime;
								timeSecondsDurationToShortEString(uGrantedAgo, &estrGrantedStr);
								estrConcatf(&display, " GRANTED %s ago", estrGrantedStr);
								estrDestroy(&estrGrantedStr);
							}
							else
								estrConcatf(&display, " GRANTED now");
						}

						if(pUGCAchievementDef->bRepeatable)
						{
							U32 uCooldownDuration = pUGCAchievementDef->uRepeatCooldownHours * 60 * 60;
							U32 uGrantTime = ugcAchievement_GetCooldownBlockGrantTime(pUGCAchievement->uGrantTime, pUGCAchievementDef);
							U32 now = timeSecondsSince2000();
							if(now <= uGrantTime + uCooldownDuration)
							{
								U32 uRepeatIn = uGrantTime + uCooldownDuration - now;
								char *estrRepeatInStr = NULL;
								timeSecondsDurationToShortEString(uRepeatIn, &estrRepeatInStr);
								estrConcatf(&display, " REPEATABLE in %s", estrRepeatInStr);
								estrDestroy(&estrRepeatInStr);
							}
							else
								estrConcatf(&display, " REPEATABLE now");
						}

						ui_TreeDisplayText(achievement, display, UI_MY_VALUES, z);
						estrDestroy(&display);
					}
				}
			}
		}
	}
}

static void ugcAchievementsDebug_FillProjectAchievementCB(UITreeNode *achievement, UserData data)
{
	UGCAchievementsNodeData *pData = (UGCAchievementsNodeData *)achievement->contents;
	if(pData && pData->ugcAchievementName && pData->projectID)
	{
		UGCAchievementDef *pUGCAchievementDef = ugcAchievement_DefFromRefString(pData->ugcAchievementName);
		if(pUGCAchievementDef)
		{
			UGCAccount *pUGCAccount = entGetUGCAccount(entActivePlayerPtr());
			if(pUGCAccount)
			{
				UGCProjectAchievementInfo *pUGCProjectAchievementInfo = eaIndexedGetUsingInt(&pUGCAccount->author.eaProjectAchievements, pData->projectID);
				if(pUGCProjectAchievementInfo)
				{
					int achIndex = eaIndexedFindUsingString(&pUGCProjectAchievementInfo->ugcAchievementInfo.eaAchievements, pData->ugcAchievementName);
					if(achIndex >= 0)
					{
						UGCAchievement *pUGCAchievement = pUGCProjectAchievementInfo->ugcAchievementInfo.eaAchievements[achIndex];
						FOR_EACH_IN_EARRAY_FORWARDS(pUGCAchievementDef->subAchievements, UGCAchievementDef, pSubUGCAchievementDef)
						{
							UITreeNode* node = NULL;
							pData = StructCreate(parse_UGCAchievementsNodeData);
							pData->ugcAchievementName = pSubUGCAchievementDef->pchRefString;
							pData->projectID = pUGCProjectAchievementInfo->projectID;
							node = ui_TreeNodeCreate(achievement->tree, (U32)pSubUGCAchievementDef->name, parse_UGCAchievementsNodeData, (UserData)pData,
								eaSize(&pSubUGCAchievementDef->subAchievements) ? ugcAchievementsDebug_FillProjectAchievementCB : NULL, (UserData)pUGCProjectAchievementInfo->projectID,
								ugcAchievementsDebug_DisplayProjectAchievementCB, (UserData)pUGCProjectAchievementInfo->projectID,
								20);
							ui_TreeNodeSetFreeCallback(node, FreeUGCAchievementsNodeDataNode);
							ui_TreeNodeAddChild(achievement, node);
						}
						FOR_EACH_END;
					}
				}
			}
		}
	}
}

static void ugcAchievementsDebug_FillSeriesAchievementCB(UITreeNode *achievement, UserData data)
{
	UGCAchievementsNodeData *pData = (UGCAchievementsNodeData *)achievement->contents;
	if(pData && pData->ugcAchievementName && pData->seriesID)
	{
		UGCAchievementDef *pUGCAchievementDef = ugcAchievement_DefFromRefString(pData->ugcAchievementName);
		if(pUGCAchievementDef)
		{
			UGCAccount *pUGCAccount = entGetUGCAccount(entActivePlayerPtr());
			if(pUGCAccount)
			{
				UGCSeriesAchievementInfo *pUGCSeriesAchievementInfo = eaIndexedGetUsingInt(&pUGCAccount->author.eaSeriesAchievements, pData->seriesID);
				if(pUGCSeriesAchievementInfo)
				{
					int achIndex = eaIndexedFindUsingString(&pUGCSeriesAchievementInfo->ugcAchievementInfo.eaAchievements, pData->ugcAchievementName);
					if(achIndex >= 0)
					{
						UGCAchievement *pUGCAchievement = pUGCSeriesAchievementInfo->ugcAchievementInfo.eaAchievements[achIndex];
						FOR_EACH_IN_EARRAY_FORWARDS(pUGCAchievementDef->subAchievements, UGCAchievementDef, pSubUGCAchievementDef)
						{
							UITreeNode* node = NULL;
							pData = StructCreate(parse_UGCAchievementsNodeData);
							pData->ugcAchievementName = pSubUGCAchievementDef->pchRefString;
							pData->seriesID = pUGCSeriesAchievementInfo->seriesID;
							node = ui_TreeNodeCreate(achievement->tree, (U32)pSubUGCAchievementDef->name, parse_UGCAchievementsNodeData, (UserData)pData,
								eaSize(&pSubUGCAchievementDef->subAchievements) ? ugcAchievementsDebug_FillSeriesAchievementCB : NULL, (UserData)pUGCSeriesAchievementInfo->seriesID,
								ugcAchievementsDebug_DisplaySeriesAchievementCB, (UserData)pUGCSeriesAchievementInfo->seriesID,
								20);
							ui_TreeNodeSetFreeCallback(node, FreeUGCAchievementsNodeDataNode);
							ui_TreeNodeAddChild(achievement, node);
						}
						FOR_EACH_END;
					}
				}
			}
		}
	}
}

static void ugcAchievementsDebug_FillProjectCB(UITreeNode *project, UserData data)
{
	ContainerID projectID = (ContainerID)data;
	UGCAccount *pUGCAccount = entGetUGCAccount(entActivePlayerPtr());
	if(pUGCAccount)
	{
		UGCProjectAchievementInfo *pUGCProjectAchievementInfo = eaIndexedGetUsingInt(&pUGCAccount->author.eaProjectAchievements, projectID);
		if(pUGCProjectAchievementInfo)
		{
			UGCAchievementDef *pUGCAchievementDef = NULL;
			const char *pUGCAchievementDefName = NULL;
			ResourceIterator iter;

			resInitIterator("UGCAchievement", &iter);
			while(resIteratorGetNext(&iter, &pUGCAchievementDefName, &pUGCAchievementDef))
			{
				if(stricmp(pUGCAchievementDef->scope, "Project") == 0)
				{
					int achIndex = eaIndexedFindUsingString(&pUGCProjectAchievementInfo->ugcAchievementInfo.eaAchievements, pUGCAchievementDef->pchRefString);
					if(achIndex >= 0)
					{
						UITreeNode* node = NULL;
						UGCAchievement *pUGCAchievement = pUGCProjectAchievementInfo->ugcAchievementInfo.eaAchievements[achIndex];
						UGCAchievementsNodeData *pData = StructCreate(parse_UGCAchievementsNodeData);
						pData->ugcAchievementName = pUGCAchievement->ugcAchievementName;
						pData->projectID = pUGCProjectAchievementInfo->projectID;
						node = ui_TreeNodeCreate(project->tree, (U32)pUGCAchievementDef->name, parse_UGCAchievementsNodeData, (UserData)pData,
							eaSize(&pUGCAchievementDef->subAchievements) ? ugcAchievementsDebug_FillProjectAchievementCB : NULL, (UserData)pUGCProjectAchievementInfo->projectID,
							ugcAchievementsDebug_DisplayProjectAchievementCB, (UserData)pUGCProjectAchievementInfo->projectID,
							20);
						ui_TreeNodeSetFreeCallback(node, FreeUGCAchievementsNodeDataNode);
						ui_TreeNodeAddChild(project, node);
					}
				}
			}
			resFreeIterator(&iter);
		}
	}
}

static void ugcAchievementsDebug_FillSeriesCB(UITreeNode *series, UserData data)
{
	ContainerID seriesID = (ContainerID)data;
	UGCAccount *pUGCAccount = entGetUGCAccount(entActivePlayerPtr());
	if(pUGCAccount)
	{
		UGCSeriesAchievementInfo *pUGCSeriesAchievementInfo = eaIndexedGetUsingInt(&pUGCAccount->author.eaSeriesAchievements, seriesID);
		if(pUGCSeriesAchievementInfo)
		{
			UGCAchievementDef *pUGCAchievementDef = NULL;
			const char *pUGCAchievementDefName = NULL;
			ResourceIterator iter;

			resInitIterator("UGCAchievement", &iter);
			while(resIteratorGetNext(&iter, &pUGCAchievementDefName, &pUGCAchievementDef))
			{
				if(stricmp(pUGCAchievementDef->scope, "Series") == 0)
				{
					int achIndex = eaIndexedFindUsingString(&pUGCSeriesAchievementInfo->ugcAchievementInfo.eaAchievements, pUGCAchievementDef->pchRefString);
					if(achIndex >= 0)
					{
						UITreeNode* node = NULL;
						UGCAchievement *pUGCAchievement = pUGCSeriesAchievementInfo->ugcAchievementInfo.eaAchievements[achIndex];
						UGCAchievementsNodeData *pData = StructCreate(parse_UGCAchievementsNodeData);
						pData->ugcAchievementName = pUGCAchievement->ugcAchievementName;
						pData->seriesID = pUGCSeriesAchievementInfo->seriesID;
						node = ui_TreeNodeCreate(series->tree, (U32)pUGCAchievementDef->name, parse_UGCAchievementsNodeData, (UserData)pData,
							eaSize(&pUGCAchievementDef->subAchievements) ? ugcAchievementsDebug_FillSeriesAchievementCB : NULL, (UserData)pUGCSeriesAchievementInfo->seriesID,
							ugcAchievementsDebug_DisplaySeriesAchievementCB, (UserData)pUGCSeriesAchievementInfo->seriesID,
							20);
						ui_TreeNodeSetFreeCallback(node, FreeUGCAchievementsNodeDataNode);
						ui_TreeNodeAddChild(series, node);
					}
				}
			}
			resFreeIterator(&iter);
		}
	}
}

static void ugcAchievementsDebug_DisplayProjectCB(UITreeNode *project, UserData data, UI_MY_ARGS, F32 z)
{
	ContainerID projectID = (ContainerID)data;
	UGCAccount *pUGCAccount = entGetUGCAccount(entActivePlayerPtr());
	if(pUGCAccount)
	{
		UGCProjectAchievementInfo *pUGCProjectAchievementInfo = eaIndexedGetUsingInt(&pUGCAccount->author.eaProjectAchievements, projectID);
		if(pUGCProjectAchievementInfo && pUGCProjectAchievementInfo->pcName)
			ui_TreeDisplayText(project, pUGCProjectAchievementInfo->pcName, UI_MY_VALUES, z);
	}
}

static void ugcAchievementsDebug_DisplaySeriesCB(UITreeNode *series, UserData data, UI_MY_ARGS, F32 z)
{
	ContainerID seriesID = (ContainerID)data;
	UGCAccount *pUGCAccount = entGetUGCAccount(entActivePlayerPtr());
	if(pUGCAccount)
	{
		UGCSeriesAchievementInfo *pUGCSeriesAchievementInfo = eaIndexedGetUsingInt(&pUGCAccount->author.eaSeriesAchievements, seriesID);
		if(pUGCSeriesAchievementInfo && pUGCSeriesAchievementInfo->pcName)
			ui_TreeDisplayText(series, pUGCSeriesAchievementInfo->pcName, UI_MY_VALUES, z);
	}
}

static void ugcAchievementsDebug_FillAccountAchievementsCB(UITreeNode *root, UserData unused)
{
	UGCAccount *pUGCAccount = entGetUGCAccount(entActivePlayerPtr());
	if(pUGCAccount)
	{
		UGCAchievementDef *pUGCAchievementDef = NULL;
		const char *pUGCAchievementDefName = NULL;
		ResourceIterator iter;

		resInitIterator("UGCAchievement", &iter);
		while(resIteratorGetNext(&iter, &pUGCAchievementDefName, &pUGCAchievementDef))
		{
			if(stricmp(pUGCAchievementDef->scope, "Account") == 0)
			{
				int achIndex = eaIndexedFindUsingString(&pUGCAccount->author.ugcAccountAchievements.eaAchievements, pUGCAchievementDef->pchRefString);
				if(achIndex >= 0)
				{
					UITreeNode* node = NULL;
					UGCAchievement *pUGCAchievement = pUGCAccount->author.ugcAccountAchievements.eaAchievements[achIndex];
					UGCAchievementsNodeData *pData = StructCreate(parse_UGCAchievementsNodeData);
					pData->ugcAchievementName = pUGCAchievement->ugcAchievementName;
					node = ui_TreeNodeCreate(root->tree, (U32)pUGCAchievementDef->name, NULL, (UserData)pData,
						eaSize(&pUGCAchievementDef->subAchievements) ? ugcAchievementsDebug_FillAccountAchievementCB : NULL, NULL,
						ugcAchievementsDebug_DisplayAccountAchievementCB, NULL,
						20);
					ui_TreeNodeSetFreeCallback(node, FreeUGCAchievementsNodeDataNode);
					ui_TreeNodeAddChild(root, node);
				}
			}
		}
		resFreeIterator(&iter);
	}
}

static void ugcAchievementsDebug_FillProjectAchievementsCB(UITreeNode *root, UserData unused)
{
	UGCAccount *pUGCAccount = entGetUGCAccount(entActivePlayerPtr());
	if(pUGCAccount)
	{
		UGCAchievementDef *pUGCAchievementDef = NULL;
		const char *pUGCAchievementDefName = NULL;

		FOR_EACH_IN_EARRAY_FORWARDS(pUGCAccount->author.eaProjectAchievements, UGCProjectAchievementInfo, pUGCProjectAchievementInfo)
		{
			UITreeNode* node = ui_TreeNodeCreate(root->tree, (U32)pUGCProjectAchievementInfo->projectID, NULL, NULL,
				ugcAchievementsDebug_FillProjectCB, (UserData)pUGCProjectAchievementInfo->projectID,
				ugcAchievementsDebug_DisplayProjectCB, (UserData)pUGCProjectAchievementInfo->projectID,
				20);
			ui_TreeNodeAddChild(root, node);
		}
		FOR_EACH_END;
	}
}

static void ugcAchievementsDebug_FillSeriesAchievementsCB(UITreeNode *root, UserData unused)
{
	UGCAccount *pUGCAccount = entGetUGCAccount(entActivePlayerPtr());
	if(pUGCAccount)
	{
		UGCAchievementDef *pUGCAchievementDef = NULL;
		const char *pUGCAchievementDefName = NULL;

		FOR_EACH_IN_EARRAY_FORWARDS(pUGCAccount->author.eaSeriesAchievements, UGCSeriesAchievementInfo, pUGCSeriesAchievementInfo)
		{
			UITreeNode* node = ui_TreeNodeCreate(root->tree, (U32)pUGCSeriesAchievementInfo->seriesID, NULL, NULL,
				ugcAchievementsDebug_FillSeriesCB, (UserData)pUGCSeriesAchievementInfo->seriesID,
				ugcAchievementsDebug_DisplaySeriesCB, (UserData)pUGCSeriesAchievementInfo->seriesID,
				20);
			ui_TreeNodeAddChild(root, node);
		}
		FOR_EACH_END;
	}
}

static void ugcAchievementsDebug_FillRootCB(UITreeNode *root, UserData unused)
{
	UGCAccount *pUGCAccount = entGetUGCAccount(entActivePlayerPtr());
	if(pUGCAccount)
	{
		UITreeNode* node = ui_TreeNodeCreate(root->tree, (U32)pUGCAccount->accountID, NULL, NULL,
			ugcAchievementsDebug_FillAccountAchievementsCB, NULL,
			ui_TreeDisplayText, "Account Achievements",
			20);
		ui_TreeNodeAddChild(root, node);

		if(eaSize(&pUGCAccount->author.eaProjectAchievements))
		{
			node = ui_TreeNodeCreate(root->tree, (U32)pUGCAccount->accountID, NULL, NULL,
				ugcAchievementsDebug_FillProjectAchievementsCB, NULL,
				ui_TreeDisplayText, "Project Achievements",
				20);
			ui_TreeNodeAddChild(root, node);
		}

		if(eaSize(&pUGCAccount->author.eaSeriesAchievements))
		{
			node = ui_TreeNodeCreate(root->tree, (U32)pUGCAccount->accountID, NULL, NULL,
				ugcAchievementsDebug_FillSeriesAchievementsCB, NULL,
				ui_TreeDisplayText, "Series Achievements",
				20);
			ui_TreeNodeAddChild(root, node);
		}
	}
}

// Toggle command to hide or show the UGC Achievements Debugger
static void achDebugUIToggle(void)
{
	if(achDebugUI.mainWindow)
	{
		// Destroy it all
		ui_WindowClose(achDebugUI.mainWindow);
	}
	else
	{
		F32 x, y, w, h;

		ServerCmd_gslUGC_SynchronizeAchievements();

		// Create the main window
		x = GamePrefGetFloat("UGCAchievements.x", 5);
		y = GamePrefGetFloat("UGCAchievements.y", 5);
		w = GamePrefGetFloat("UGCAchievements.w", 600);
		h = GamePrefGetFloat("UGCAchievements.h", 600);

		achDebugUI.mainWindow = ui_WindowCreate("UGC Achievements Debugger", x, y, w, h);
		ui_WindowSetDimensions(achDebugUI.mainWindow, w, h, 500, 200);

		ui_WindowSetCloseCallback(achDebugUI.mainWindow, achDebugUICloseCallback, NULL);

		achDebugUI.achievementTree = ui_TreeCreate(0, 0, 1, 1);
		ui_WidgetSetDimensionsEx(UI_WIDGET(achDebugUI.achievementTree), 1, 1, UIUnitPercentage, UIUnitPercentage);
		ui_WindowAddChild(achDebugUI.mainWindow, UI_WIDGET(achDebugUI.achievementTree));

		ui_TreeNodeSetFillCallback(&achDebugUI.achievementTree->root, ugcAchievementsDebug_FillRootCB, NULL);
		ui_TreeRefresh(achDebugUI.achievementTree);

		ui_WindowShow(achDebugUI.mainWindow);
	}
}

AUTO_COMMAND ACMD_ACCESSLEVEL(9) ACMD_CLIENTONLY;
void ugcAchievementsDebug(void)
{
	achDebugUIToggle();
}

#include "AutoGen/UGCAchievementsDebugger_c_ast.c"

#endif // #endif NO_EDITORS
