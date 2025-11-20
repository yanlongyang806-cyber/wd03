#include "stdtypes.h"
#include "mission_common.h"
#include "Expression.h"
#include "GameEvent.h"
#include "gslEventTracker.h"
#include "ugcprojectcommon.h"
#include "Entity.h"
#include "UGCAchievements.h"

AUTO_EXPR_FUNC_STATIC_CHECK;
int exprFunc_UGCAuthor_AccountAvailable_StaticCheck(ExprContext* context)
{
#ifdef GAMESERVER
	MissionDef* pMissionDef = exprContextGetVarPointerUnsafePooled(context, g_MissionDefVarName);
	if(pMissionDef)
	{
		GameEvent *pEvent = StructCreate(parse_GameEvent);
		pEvent->type = EventType_UGCAccountChanged;
		if (missiondef_GetType(pMissionDef) != MissionType_OpenMission) {
			pEvent->pchEventName = allocAddString("PlayerUGCAccountChanged");
			pEvent->tMatchSource = TriState_Yes;
		} else {
			pEvent->pchEventName = allocAddString("AnyPlayerUGCAccountChanged");
		}
		eventtracker_AddNamedEventToList(&pMissionDef->eaTrackedEventsNoSave, pEvent, pMissionDef->filename);
	}
#endif
	return 0;
}

AUTO_EXPR_FUNC(player, mission) ACMD_NAME(ugcAuthor_AccountAvailable) ACMD_EXPR_STATIC_CHECK(exprFunc_UGCAuthor_AccountAvailable_StaticCheck);
int exprFunc_UGCAuthor_AccountAvailable(ExprContext* context)
{
	Entity* pEntity = exprContextGetVarPointerUnsafePooled(context, g_PlayerVarName);
	if(pEntity)
	{
		UGCAccount *pUGCAccount = entGetUGCAccount(pEntity);
		if(pUGCAccount)
			return true;
	}
	return false;
}

/*
 * Commented out the following 2 expressions because we may bring them back some day. Once UGC Achievements goes live at launch,
 * we can reliably remove them.
 */

/*AUTO_EXPR_FUNC_STATIC_CHECK;
int exprFunc_UGCAuthor_AccountAchievementGranted_StaticCheck(ExprContext* context, const char *ugcAchievementName)
{
#ifdef GAMESERVER
	MissionDef* pMissionDef = exprContextGetVarPointerUnsafePooled(context, g_MissionDefVarName);
	if(pMissionDef)
	{
		GameEvent *pEvent = StructCreate(parse_GameEvent);
		pEvent->type = EventType_UGCAccountChanged;
		if (missiondef_GetType(pMissionDef) != MissionType_OpenMission) {
			pEvent->pchEventName = allocAddString("PlayerUGCAccountChanged");
			pEvent->tMatchSource = TriState_Yes;
		} else {
			pEvent->pchEventName = allocAddString("AnyPlayerUGCAccountChanged");
		}
		eventtracker_AddNamedEventToList(&pMissionDef->eaTrackedEventsNoSave, pEvent, pMissionDef->filename);
	}
#endif
	return 0;
}

AUTO_EXPR_FUNC(player, mission) ACMD_NAME(ugcAuthor_AccountAchievementGranted) ACMD_EXPR_STATIC_CHECK(exprFunc_UGCAuthor_AccountAchievementGranted_StaticCheck);
int exprFunc_UGCAuthor_AccountAchievementGranted(ExprContext* context, ACMD_EXPR_RES_DICT(AllUGCAchievementsIndex) const char *ugcAchievementName)
{
	Entity* pEntity = exprContextGetVarPointerUnsafePooled(context, g_PlayerVarName);
	if(pEntity)
	{
		UGCAccount *pUGCAccount = entGetUGCAccount(pEntity);
		if(pUGCAccount)
		{
			if(ugcAchievementName)
			{
				UGCAchievementDef *pUGCAchievementDef = ugcAchievement_DefFromRefString(ugcAchievementName);
				if(pUGCAchievementDef)
				{
					if(0 == stricmp(ugcAchievement_Scope(pUGCAchievementDef), "Account"))
					{
						int index = eaIndexedFindUsingString(&pUGCAccount->author.ugcAccountAchievements.eaAchievements, ugcAchievementName);
						if(index >= 0)
						{
							UGCAchievement *pUGCAchievement = pUGCAccount->author.ugcAccountAchievements.eaAchievements[index];
							if(pUGCAchievement)
								return !!pUGCAchievement->uGrantTime;
						}
					}
				}
			}
		}
	}
	return 0;
}

AUTO_EXPR_FUNC(player, mission) ACMD_NAME(ugcAuthor_AccountAchievementTarget);
U32 exprFunc_UGCAuthor_AccountAchievementTarget(ExprContext* context, ACMD_EXPR_RES_DICT(AllUGCAchievementsIndex) const char *ugcAchievementName)
{
	UGCAchievementDef *pUGCAchievementDef = ugcAchievement_DefFromRefString(ugcAchievementName);

	if(pUGCAchievementDef)
	{
		int count = eaSize(&pUGCAchievementDef->subAchievements);
		return count ? count : pUGCAchievementDef->uTarget;
	}

	return 1;
}*/

AUTO_EXPR_FUNC_STATIC_CHECK;
U32 exprFunc_UGCAuthor_AccountAchievementCount_StaticCheck(ExprContext* context, const char *ugcAchievementName)
{
#ifdef GAMESERVER
	MissionDef* pMissionDef = exprContextGetVarPointerUnsafePooled(context, g_MissionDefVarName);
	if(pMissionDef)
	{
		GameEvent *pEvent = StructCreate(parse_GameEvent);
		pEvent->type = EventType_UGCAccountChanged;
		if (missiondef_GetType(pMissionDef) != MissionType_OpenMission) {
			pEvent->pchEventName = allocAddString("PlayerUGCAccountChanged");
			pEvent->tMatchSource = TriState_Yes;
		} else {
			pEvent->pchEventName = allocAddString("AnyPlayerUGCAccountChanged");
		}
		eventtracker_AddNamedEventToList(&pMissionDef->eaTrackedEventsNoSave, pEvent, pMissionDef->filename);
	}
#endif
	return 0;
}

AUTO_EXPR_FUNC(player, mission) ACMD_NAME(ugcAuthor_AccountAchievementCount) ACMD_EXPR_STATIC_CHECK(exprFunc_UGCAuthor_AccountAchievementCount_StaticCheck);
U32 exprFunc_UGCAuthor_AccountAchievementCount(ExprContext* context, ACMD_EXPR_RES_DICT(AllUGCAchievementsIndex) const char *ugcAchievementName)
{
	Entity* pEntity = exprContextGetVarPointerUnsafePooled(context, g_PlayerVarName);
	if(pEntity)
	{
		UGCAccount *pUGCAccount = entGetUGCAccount(pEntity);
		if(pUGCAccount)
		{
			if(ugcAchievementName)
			{
				UGCAchievementDef *pUGCAchievementDef = ugcAchievement_DefFromRefString(ugcAchievementName);
				if(pUGCAchievementDef)
				{
					if(0 == stricmp(ugcAchievement_Scope(pUGCAchievementDef), "Account"))
					{
						int count = eaSize(&pUGCAchievementDef->subAchievements);
						if(count)
						{
							int totalGranted = 0;
							FOR_EACH_IN_EARRAY_FORWARDS(pUGCAchievementDef->subAchievements, UGCAchievementDef, pSubAchievementDef)
							{
								int index = eaIndexedFindUsingString(&pUGCAccount->author.ugcAccountAchievements.eaAchievements, pSubAchievementDef->pchRefString);
								if(index >= 0)
								{
									UGCAchievement *pSubAchievement = pUGCAccount->author.ugcAccountAchievements.eaAchievements[index];
									if(pSubAchievement->uGrantTime)
										totalGranted++;
								}
							}
							FOR_EACH_END;
							return totalGranted;
						}
						else
						{
							int index = eaIndexedFindUsingString(&pUGCAccount->author.ugcAccountAchievements.eaAchievements, ugcAchievementName);
							if(index >= 0)
							{
								UGCAchievement *pUGCAchievement = pUGCAccount->author.ugcAccountAchievements.eaAchievements[index];
								if(pUGCAchievement)
									return pUGCAchievement->uCount;
							}
						}
					}
				}
			}
		}
	}
	return 0;
}

AUTO_EXPR_FUNC_STATIC_CHECK;
U32 exprFunc_UGCAuthor_AccountAchievementMaximumConsecutiveCount_StaticCheck(ExprContext* context, const char *ugcAchievementName)
{
#ifdef GAMESERVER
	MissionDef* pMissionDef = exprContextGetVarPointerUnsafePooled(context, g_MissionDefVarName);
	if(pMissionDef)
	{
		GameEvent *pEvent = StructCreate(parse_GameEvent);
		pEvent->type = EventType_UGCAccountChanged;
		if (missiondef_GetType(pMissionDef) != MissionType_OpenMission) {
			pEvent->pchEventName = allocAddString("PlayerUGCAccountChanged");
			pEvent->tMatchSource = TriState_Yes;
		} else {
			pEvent->pchEventName = allocAddString("AnyPlayerUGCAccountChanged");
		}
		eventtracker_AddNamedEventToList(&pMissionDef->eaTrackedEventsNoSave, pEvent, pMissionDef->filename);
	}
#endif
	return 0;
}

AUTO_EXPR_FUNC(player, mission) ACMD_NAME(ugcAuthor_AccountAchievementMaximumConsecutiveCount) ACMD_EXPR_STATIC_CHECK(exprFunc_UGCAuthor_AccountAchievementMaximumConsecutiveCount_StaticCheck);
U32 exprFunc_UGCAuthor_AccountAchievementMaximumConsecutiveCount(ExprContext* context, ACMD_EXPR_RES_DICT(AllUGCAchievementsIndex) const char *ugcAchievementName)
{
	Entity* pEntity = exprContextGetVarPointerUnsafePooled(context, g_PlayerVarName);
	if(pEntity)
	{
		UGCAccount *pUGCAccount = entGetUGCAccount(pEntity);
		if(pUGCAccount)
		{
			if(ugcAchievementName)
			{
				UGCAchievementDef *pUGCAchievementDef = ugcAchievement_DefFromRefString(ugcAchievementName);
				if(pUGCAchievementDef)
				{
					if(0 == stricmp(ugcAchievement_Scope(pUGCAchievementDef), "Account"))
					{
						
						int index = eaIndexedFindUsingString(&pUGCAccount->author.ugcAccountAchievements.eaAchievements, ugcAchievementName);
						if(index >= 0)
						{
							UGCAchievement *pUGCAchievement = pUGCAccount->author.ugcAccountAchievements.eaAchievements[index];
							if(pUGCAchievement)
								return pUGCAchievement->uMaximumConsecutiveCount;
						}
					}
				}
			}
		}
	}
	return 0;
}

AUTO_EXPR_FUNC_STATIC_CHECK;
U32 exprFunc_UGCAuthor_AccountAchievementConsecutiveMultipleSetCount_StaticCheck(ExprContext* context, const char *ugcAchievementName)
{
#ifdef GAMESERVER
	MissionDef* pMissionDef = exprContextGetVarPointerUnsafePooled(context, g_MissionDefVarName);
	if(pMissionDef)
	{
		GameEvent *pEvent = StructCreate(parse_GameEvent);
		pEvent->type = EventType_UGCAccountChanged;
		if (missiondef_GetType(pMissionDef) != MissionType_OpenMission) {
			pEvent->pchEventName = allocAddString("PlayerUGCAccountChanged");
			pEvent->tMatchSource = TriState_Yes;
		} else {
			pEvent->pchEventName = allocAddString("AnyPlayerUGCAccountChanged");
		}
		eventtracker_AddNamedEventToList(&pMissionDef->eaTrackedEventsNoSave, pEvent, pMissionDef->filename);
	}
#endif
	return 0;
}

AUTO_EXPR_FUNC(player, mission) ACMD_NAME(ugcAuthor_AccountAchievementConsecutiveMultipleSetCount) ACMD_EXPR_STATIC_CHECK(exprFunc_UGCAuthor_AccountAchievementConsecutiveMultipleSetCount_StaticCheck);
U32 exprFunc_UGCAuthor_AccountAchievementConsecutiveMultipleSetCount(ExprContext* context, ACMD_EXPR_RES_DICT(AllUGCAchievementsIndex) const char *ugcAchievementName)
{
	Entity* pEntity = exprContextGetVarPointerUnsafePooled(context, g_PlayerVarName);
	if(pEntity)
	{
		UGCAccount *pUGCAccount = entGetUGCAccount(pEntity);
		if(pUGCAccount)
		{
			if(ugcAchievementName)
			{
				UGCAchievementDef *pUGCAchievementDef = ugcAchievement_DefFromRefString(ugcAchievementName);
				if(pUGCAchievementDef)
				{
					if(0 == stricmp(ugcAchievement_Scope(pUGCAchievementDef), "Account"))
					{

						int index = eaIndexedFindUsingString(&pUGCAccount->author.ugcAccountAchievements.eaAchievements, ugcAchievementName);
						if(index >= 0)
						{
							UGCAchievement *pUGCAchievement = pUGCAccount->author.ugcAccountAchievements.eaAchievements[index];
							if(pUGCAchievement)
								return pUGCAchievement->uCount / pUGCAchievementDef->uConsecutiveMissCountResetMultiple;
						}
					}
				}
			}
		}
	}
	return 0;
}

AUTO_EXPR_FUNC_STATIC_CHECK;
U32 exprFunc_UGCAuthor_AccountAchievementConsecutiveCount_StaticCheck(ExprContext* context, const char *ugcAchievementName)
{
#ifdef GAMESERVER
	MissionDef* pMissionDef = exprContextGetVarPointerUnsafePooled(context, g_MissionDefVarName);
	if(pMissionDef)
	{
		GameEvent *pEvent = StructCreate(parse_GameEvent);
		pEvent->type = EventType_UGCAccountChanged;
		if (missiondef_GetType(pMissionDef) != MissionType_OpenMission) {
			pEvent->pchEventName = allocAddString("PlayerUGCAccountChanged");
			pEvent->tMatchSource = TriState_Yes;
		} else {
			pEvent->pchEventName = allocAddString("AnyPlayerUGCAccountChanged");
		}
		eventtracker_AddNamedEventToList(&pMissionDef->eaTrackedEventsNoSave, pEvent, pMissionDef->filename);
	}
#endif
	return 0;
}

AUTO_EXPR_FUNC(player, mission) ACMD_NAME(ugcAuthor_AccountAchievementConsecutiveCount) ACMD_EXPR_STATIC_CHECK(exprFunc_UGCAuthor_AccountAchievementConsecutiveCount_StaticCheck);
U32 exprFunc_UGCAuthor_AccountAchievementConsecutiveCount(ExprContext* context, ACMD_EXPR_RES_DICT(AllUGCAchievementsIndex) const char *ugcAchievementName)
{
	Entity* pEntity = exprContextGetVarPointerUnsafePooled(context, g_PlayerVarName);
	if(pEntity)
	{
		UGCAccount *pUGCAccount = entGetUGCAccount(pEntity);
		if(pUGCAccount)
		{
			if(ugcAchievementName)
			{
				UGCAchievementDef *pUGCAchievementDef = ugcAchievement_DefFromRefString(ugcAchievementName);
				if(pUGCAchievementDef)
				{
					if(0 == stricmp(ugcAchievement_Scope(pUGCAchievementDef), "Account"))
					{

						int index = eaIndexedFindUsingString(&pUGCAccount->author.ugcAccountAchievements.eaAchievements, ugcAchievementName);
						if(index >= 0)
						{
							UGCAchievement *pUGCAchievement = pUGCAccount->author.ugcAccountAchievements.eaAchievements[index];
							if(pUGCAchievement)
								return pUGCAchievement->uCount % pUGCAchievementDef->uConsecutiveMissCountResetMultiple;
						}
					}
				}
			}
		}
	}
	return 0;
}

AUTO_EXPR_FUNC_STATIC_CHECK;
U32 exprFunc_UGCAuthor_ProjectAchievementCount_StaticCheck(ExprContext* context, const char *ugcAchievementName, U32 count)
{
#ifdef GAMESERVER
	MissionDef* pMissionDef = exprContextGetVarPointerUnsafePooled(context, g_MissionDefVarName);
	if(pMissionDef)
	{
		GameEvent *pEvent = StructCreate(parse_GameEvent);
		pEvent->type = EventType_UGCAccountChanged;
		if (missiondef_GetType(pMissionDef) != MissionType_OpenMission) {
			pEvent->pchEventName = allocAddString("PlayerUGCAccountChanged");
			pEvent->tMatchSource = TriState_Yes;
		} else {
			pEvent->pchEventName = allocAddString("AnyPlayerUGCAccountChanged");
		}
		eventtracker_AddNamedEventToList(&pMissionDef->eaTrackedEventsNoSave, pEvent, pMissionDef->filename);
	}
#endif
	return 0;
}

AUTO_EXPR_FUNC(player, mission) ACMD_NAME(ugcAuthor_ProjectAchievementCount) ACMD_EXPR_STATIC_CHECK(exprFunc_UGCAuthor_ProjectAchievementCount_StaticCheck);
U32 exprFunc_UGCAuthor_ProjectAchievementCount(ExprContext* context, ACMD_EXPR_RES_DICT(AllUGCAchievementsIndex) const char *ugcAchievementName, U32 count)
{
	U32 result = 0;
	Entity* pEntity = exprContextGetVarPointerUnsafePooled(context, g_PlayerVarName);
	if(pEntity)
	{
		UGCAccount *pUGCAccount = entGetUGCAccount(pEntity);
		if(pUGCAccount)
		{
			if(ugcAchievementName)
			{
				UGCAchievementDef *pUGCAchievementDef = ugcAchievement_DefFromRefString(ugcAchievementName);
				if(pUGCAchievementDef)
				{
					if(0 == stricmp(ugcAchievement_Scope(pUGCAchievementDef), "Project"))
					{
						int iter;
						for(iter = 0; iter < eaSize(&pUGCAccount->author.eaProjectAchievements); iter++)
						{
							UGCProjectAchievementInfo *pUGCProjectAchievementInfo = pUGCAccount->author.eaProjectAchievements[iter];
							int index = eaIndexedFindUsingString(&pUGCProjectAchievementInfo->ugcAchievementInfo.eaAchievements, ugcAchievementName);
							if(index >= 0)
							{
								UGCAchievement *pUGCAchievement = pUGCProjectAchievementInfo->ugcAchievementInfo.eaAchievements[index];
								if(pUGCAchievement && pUGCAchievement->uCount >= count)
									result++;
							}
						}
					}
				}
			}
		}
	}
	return result;
}

AUTO_EXPR_FUNC_STATIC_CHECK;
U32 exprFunc_UGCAuthor_ProjectAchievementLargestCount_StaticCheck(ExprContext* context, const char *ugcAchievementName)
{
#ifdef GAMESERVER
	MissionDef* pMissionDef = exprContextGetVarPointerUnsafePooled(context, g_MissionDefVarName);
	if(pMissionDef)
	{
		GameEvent *pEvent = StructCreate(parse_GameEvent);
		pEvent->type = EventType_UGCAccountChanged;
		if (missiondef_GetType(pMissionDef) != MissionType_OpenMission) {
			pEvent->pchEventName = allocAddString("PlayerUGCAccountChanged");
			pEvent->tMatchSource = TriState_Yes;
		} else {
			pEvent->pchEventName = allocAddString("AnyPlayerUGCAccountChanged");
		}
		eventtracker_AddNamedEventToList(&pMissionDef->eaTrackedEventsNoSave, pEvent, pMissionDef->filename);
	}
#endif
	return 0;
}

AUTO_EXPR_FUNC(player, mission) ACMD_NAME(ugcAuthor_ProjectAchievementLargestCount) ACMD_EXPR_STATIC_CHECK(exprFunc_UGCAuthor_ProjectAchievementLargestCount_StaticCheck);
U32 exprFunc_UGCAuthor_ProjectAchievementLargestCount(ExprContext* context, ACMD_EXPR_RES_DICT(AllUGCAchievementsIndex) const char *ugcAchievementName)
{
	U32 result = 0;
	Entity* pEntity = exprContextGetVarPointerUnsafePooled(context, g_PlayerVarName);
	if(pEntity)
	{
		UGCAccount *pUGCAccount = entGetUGCAccount(pEntity);
		if(pUGCAccount)
		{
			if(ugcAchievementName)
			{
				UGCAchievementDef *pUGCAchievementDef = ugcAchievement_DefFromRefString(ugcAchievementName);
				if(pUGCAchievementDef)
				{
					if(0 == stricmp(ugcAchievement_Scope(pUGCAchievementDef), "Project"))
					{
						int iter;
						for(iter = 0; iter < eaSize(&pUGCAccount->author.eaProjectAchievements); iter++)
						{
							UGCProjectAchievementInfo *pUGCProjectAchievementInfo = pUGCAccount->author.eaProjectAchievements[iter];
							int index = eaIndexedFindUsingString(&pUGCProjectAchievementInfo->ugcAchievementInfo.eaAchievements, ugcAchievementName);
							if(index >= 0)
							{
								UGCAchievement *pUGCAchievement = pUGCProjectAchievementInfo->ugcAchievementInfo.eaAchievements[index];
								if(pUGCAchievement && pUGCAchievement->uCount >= result)
									result = pUGCAchievement->uCount;
							}
						}
					}
				}
			}
		}
	}
	return result;
}

AUTO_EXPR_FUNC_STATIC_CHECK;
U32 exprFunc_UGCAuthor_SeriesAchievementCount_StaticCheck(ExprContext* context, const char *ugcAchievementName, U32 count)
{
#ifdef GAMESERVER
	MissionDef* pMissionDef = exprContextGetVarPointerUnsafePooled(context, g_MissionDefVarName);
	if(pMissionDef)
	{
		GameEvent *pEvent = StructCreate(parse_GameEvent);
		pEvent->type = EventType_UGCAccountChanged;
		if (missiondef_GetType(pMissionDef) != MissionType_OpenMission) {
			pEvent->pchEventName = allocAddString("PlayerUGCAccountChanged");
			pEvent->tMatchSource = TriState_Yes;
		} else {
			pEvent->pchEventName = allocAddString("AnyPlayerUGCAccountChanged");
		}
		eventtracker_AddNamedEventToList(&pMissionDef->eaTrackedEventsNoSave, pEvent, pMissionDef->filename);
	}
#endif
	return 0;
}

AUTO_EXPR_FUNC(player, mission) ACMD_NAME(ugcAuthor_SeriesAchievementCount) ACMD_EXPR_STATIC_CHECK(exprFunc_UGCAuthor_SeriesAchievementCount_StaticCheck);
U32 exprFunc_UGCAuthor_SeriesAchievementCount(ExprContext* context, ACMD_EXPR_RES_DICT(AllUGCAchievementsIndex) const char *ugcAchievementName, U32 count)
{
	U32 result = 0;
	Entity* pEntity = exprContextGetVarPointerUnsafePooled(context, g_PlayerVarName);
	if(pEntity)
	{
		UGCAccount *pUGCAccount = entGetUGCAccount(pEntity);
		if(pUGCAccount)
		{
			if(ugcAchievementName)
			{
				UGCAchievementDef *pUGCAchievementDef = ugcAchievement_DefFromRefString(ugcAchievementName);
				if(pUGCAchievementDef)
				{
					if(0 == stricmp(ugcAchievement_Scope(pUGCAchievementDef), "Series"))
					{
						int iter;
						for(iter = 0; iter < eaSize(&pUGCAccount->author.eaSeriesAchievements); iter++)
						{
							UGCSeriesAchievementInfo *pUGCSeriesAchievementInfo = pUGCAccount->author.eaSeriesAchievements[iter];
							int index = eaIndexedFindUsingString(&pUGCSeriesAchievementInfo->ugcAchievementInfo.eaAchievements, ugcAchievementName);
							if(index >= 0)
							{
								UGCAchievement *pUGCAchievement = pUGCSeriesAchievementInfo->ugcAchievementInfo.eaAchievements[index];
								if(pUGCAchievement && pUGCAchievement->uCount >= count)
									result++;
							}
						}
					}
				}
			}
		}
	}
	return result;
}

AUTO_EXPR_FUNC_STATIC_CHECK;
U32 exprFunc_UGCAuthor_SeriesAchievementLargestCount_StaticCheck(ExprContext* context, const char *ugcAchievementName)
{
#ifdef GAMESERVER
	MissionDef* pMissionDef = exprContextGetVarPointerUnsafePooled(context, g_MissionDefVarName);
	if(pMissionDef)
	{
		GameEvent *pEvent = StructCreate(parse_GameEvent);
		pEvent->type = EventType_UGCAccountChanged;
		if (missiondef_GetType(pMissionDef) != MissionType_OpenMission) {
			pEvent->pchEventName = allocAddString("PlayerUGCAccountChanged");
			pEvent->tMatchSource = TriState_Yes;
		} else {
			pEvent->pchEventName = allocAddString("AnyPlayerUGCAccountChanged");
		}
		eventtracker_AddNamedEventToList(&pMissionDef->eaTrackedEventsNoSave, pEvent, pMissionDef->filename);
	}
#endif
	return 0;
}

AUTO_EXPR_FUNC(player, mission) ACMD_NAME(ugcAuthor_SeriesAchievementLargestCount) ACMD_EXPR_STATIC_CHECK(exprFunc_UGCAuthor_SeriesAchievementLargestCount_StaticCheck);
U32 exprFunc_UGCAuthor_SeriesAchievementLargestCount(ExprContext* context, ACMD_EXPR_RES_DICT(AllUGCAchievementsIndex) const char *ugcAchievementName)
{
	U32 result = 0;
	Entity* pEntity = exprContextGetVarPointerUnsafePooled(context, g_PlayerVarName);
	if(pEntity)
	{
		UGCAccount *pUGCAccount = entGetUGCAccount(pEntity);
		if(pUGCAccount)
		{
			if(ugcAchievementName)
			{
				UGCAchievementDef *pUGCAchievementDef = ugcAchievement_DefFromRefString(ugcAchievementName);
				if(pUGCAchievementDef)
				{
					if(0 == stricmp(ugcAchievement_Scope(pUGCAchievementDef), "Series"))
					{
						int iter;
						for(iter = 0; iter < eaSize(&pUGCAccount->author.eaSeriesAchievements); iter++)
						{
							UGCSeriesAchievementInfo *pUGCSeriesAchievementInfo = pUGCAccount->author.eaSeriesAchievements[iter];
							int index = eaIndexedFindUsingString(&pUGCSeriesAchievementInfo->ugcAchievementInfo.eaAchievements, ugcAchievementName);
							if(index >= 0)
							{
								UGCAchievement *pUGCAchievement = pUGCSeriesAchievementInfo->ugcAchievementInfo.eaAchievements[index];
								if(pUGCAchievement && pUGCAchievement->uCount >= result)
									result = pUGCAchievement->uCount;
							}
						}
					}
				}
			}
		}
	}
	return result;
}

