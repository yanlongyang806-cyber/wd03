/***************************************************************************
*     Copyright (c) 2009, Cryptic Studios
*     All Rights Reserved
*     Confidential Property of Cryptic Studios
***************************************************************************/

#include "gclActivityLog.h"
#include "ActivityLogCommon.h"
#include "Entity.h"
#include "EntitySavedData.h"
#include "Guild.h"
#include "Player.h"
#include "GameStringFormat.h"
#include "EString.h"
#include "UIGen.h"
#include "StringCache.h"
#include "DiaryCommon.h"

#include "AutoGen/DiaryCommon_h_ast.h"

#include "AutoGen/gclActivityLog_h_ast.h"

AUTO_RUN_ANON(memBudgetAddMapping(__FILE__, BUDGET_UISystem););

static NOCONST(DiaryFilter) *s_currentActivityLogFilter = NULL;

// keeps track of various set of scratch tag bits that the UI may be using
//static U64 s_workingTagBits = 0;
static StashTable s_activityLogScratchTagBits = NULL;

bool CheckActivityLogFilter(const char *text, DiaryFilter *filter)
{
	//if ( ( filter->startTime != 0 ) && ( displayHeader->displayTime < filter->startTime ) )
	//{
	//	return false;
	//}

	//if ( ( filter->endTime != 0 ) && ( displayHeader->displayTime > filter->endTime ) )
	//{
	//	return false;
	//}

	//if ( ( filter->type != DiaryEntryType_None ) && ( filter->type != displayHeader->type ) )
	//{
	//	return false;
	//}

	if ( ( filter->titleSubstring != NULL ) && ( ( text == NULL ) || ( strstri(text, filter->titleSubstring) == NULL ) ) )
	{
		return false;
	}

	//if ( ( filter->tagIncludeMask != 0 ) && ( ( filter->tagIncludeMask & displayHeader->tagBits ) == 0 ) )
	//{
	//	return false;
	//}

	//if ( ( filter->tagExcludeMask != 0 ) && ( ( filter->tagExcludeMask & displayHeader->tagBits ) != 0 ) )
	//{
	//	return false;
	//}

	return true;
}

//
// Implement named sets of scratch tag bits that can be used by different
//  parts of the UI at the same time, and not interfere with each other.
//
U64 *GetActivityLogScratchTagBits(const char *setName)
{
	U64 *ret;
	const char *pooledSetName = allocAddString(setName);

	if ( s_activityLogScratchTagBits == NULL )
	{
		s_activityLogScratchTagBits = stashTableCreateWithStringKeys(100, StashDefault);
	}

	if ( !stashFindPointer(s_activityLogScratchTagBits, pooledSetName, &ret) )
	{
		// if we didn't find an entry in the table, then create one
		ret = (U64 *)malloc(sizeof(U64));
		*ret = 0;
		stashAddPointer(s_activityLogScratchTagBits, pooledSetName, ret, false);
	}

	return ret;
}

NOCONST(DiaryFilter) *GetCurrentActivityLogFilter()
{
	if ( s_currentActivityLogFilter == NULL )
	{
		s_currentActivityLogFilter = StructCreateNoConst(parse_DiaryFilter);
	}

	return s_currentActivityLogFilter;
}

AUTO_EXPR_FUNC(UIGen) ACMD_NAME(ActivityLogSetCurrentFilter);
void exprActivityLogSetCurrentFilter(int entryType, const char *titleSubString, const char *tagSetInclude, const char *tagSetExclude)
{
	NOCONST(DiaryFilter) *filter = GetCurrentActivityLogFilter();
	U64 *pTagBits;

	// no time support right now
	filter->endTime = 0;
	filter->startTime = 0;

	filter->type = (DiaryEntryType)entryType;

	if ( filter->titleSubstring != NULL )
	{
		StructFreeString(filter->titleSubstring);
		filter->titleSubstring = NULL;
	}

	if ( ( titleSubString != NULL ) && ( titleSubString[0] != '\0' ) )
	{
		// only copy the string if it has something interesting in it
		filter->titleSubstring = StructAllocString(titleSubString);
	}

	pTagBits = GetActivityLogScratchTagBits(tagSetInclude);
	filter->tagIncludeMask = *pTagBits;

	pTagBits = GetActivityLogScratchTagBits(tagSetExclude);
	filter->tagExcludeMask = *pTagBits;

	return;
}

//
// Set the gen list model with display entries for the player's guild's activity log
//
AUTO_EXPR_FUNC(UIGen) ACMD_NAME(GuildActivityLogEntries);
void 
exprGuildActivityLogEntries(SA_PARAM_OP_VALID Entity *pEnt, SA_PARAM_NN_VALID UIGen *pGen)
{
	static ActivityLogDisplayEntry **s_displayEntries = NULL;
	int numDisplayEntries = 0;
	Guild *guild;

	if ( ( pEnt != NULL ) && ( pEnt->pPlayer->pGuild != NULL ) )
	{
		guild = GET_REF(pEnt->pPlayer->pGuild->hGuild);
		if ( guild != NULL )
		{
			int n = eaSize(&guild->eaActivityEntries);
			int i;
			for ( i = 0; i < n; i++ )
			{
				ActivityLogEntry *logEntry = guild->eaActivityEntries[i];
				ActivityLogEntryTypeConfig *activityEntryTypeConfig = ActivityLog_GetTypeConfig(logEntry->type);
				if ( activityEntryTypeConfig )
				{
					ActivityLogDisplayEntry *displayEntry = NULL;
					// skip log entries for ex-members
					if (logEntry->subjectID != 0
						&& eaIndexedGetUsingInt(&guild->eaMembers, logEntry->subjectID) == NULL )
					{
						continue;
					}

					// expand s_displayEntries as needed
					displayEntry = eaGetStruct(&s_displayEntries, parse_ActivityLogDisplayEntry, numDisplayEntries);
					
					// fill in the display entry
					displayEntry->entryID = logEntry->entryID;
					displayEntry->time = logEntry->time;
					estrClear(&displayEntry->text);
					ActivityLog_FormatEntry(entGetLanguage(pEnt), &displayEntry->text, logEntry, guild);

					if ( GetCurrentActivityLogFilter() == NULL || CheckActivityLogFilter(s_displayEntries[numDisplayEntries]->text, (DiaryFilter*)GetCurrentActivityLogFilter()) )
					{
						numDisplayEntries++;
					}
				}
			}
		}
	}
	while (eaSize(&s_displayEntries) > numDisplayEntries)
	{
		StructDestroy(parse_ActivityLogDisplayEntry, eaPop(&s_displayEntries));
	}

	ui_GenSetList(pGen, &s_displayEntries, parse_ActivityLogDisplayEntry);
}

static char *gActivityLogSearchStr = NULL;

AUTO_EXPR_FUNC(UIGen) ACMD_NAME(SetActivityLogSearchStr);
void 
exprSetActivityLogSearchStr(const char *pchSearch)
{
	if (gActivityLogSearchStr) StructFreeString(gActivityLogSearchStr);
	if (pchSearch) gActivityLogSearchStr = StructAllocString(pchSearch);
}

//
// Set the gen list model with display entries for the pet's activity log
//
AUTO_EXPR_FUNC(UIGen) ACMD_NAME(EntityActivityLogEntries);
void 
exprEntityActivityLogEntries(SA_PARAM_OP_VALID Entity *pEnt, SA_PARAM_NN_VALID UIGen *pGen)
{
	static ActivityLogDisplayEntry **s_displayEntries = NULL;
	int n;
	int i;
	int numDisplayEntries = 0;

	if ( ( pEnt != NULL ) && ( pEnt->pSaved != NULL ) )
	{
		n = eaSize(&pEnt->pSaved->activityLogEntries);
		for ( i = 0; i < n; i++ )
		{
			ActivityLogEntry *logEntry = pEnt->pSaved->activityLogEntries[i];
			ActivityLogEntryTypeConfig *activityEntryTypeConfig;

			activityEntryTypeConfig = ActivityLog_GetTypeConfig(logEntry->type);
			if ( activityEntryTypeConfig != NULL )
			{
				// expand s_displayEntries as needed
				while (eaSize(&s_displayEntries) <= numDisplayEntries)
				{
					eaPush(&s_displayEntries, StructCreate(parse_ActivityLogDisplayEntry));
				}

				// fill in the display entry
				s_displayEntries[numDisplayEntries]->entryID = logEntry->entryID;
				s_displayEntries[numDisplayEntries]->time = logEntry->time;
				estrClear(&s_displayEntries[numDisplayEntries]->text);
				if ( activityEntryTypeConfig->splitArgString ) 
				{
					char *tmpString1 = NULL;
					char *tmpString2 = NULL;
					const char *argString1;
					const char *argString2;

					estrAppend2(&tmpString1, logEntry->argString);
					estrAppend2(&tmpString2, logEntry->argString);

					estrTruncateAtFirstOccurrence(&tmpString1, ':');
					estrRemoveUpToFirstOccurrence(&tmpString2, ':');

					if ( activityEntryTypeConfig->translateArg )
					{
						argString1 = TranslateMessageKey(tmpString1);
					}
					else
					{
						argString1 = tmpString1;
					}

					if ( activityEntryTypeConfig->translateArg2 )
					{
						argString2 = TranslateMessageKey(tmpString2);
					}
					else
					{
						argString2 = tmpString2;
					}

					FormatGameDisplayMessage(&s_displayEntries[numDisplayEntries]->text, &activityEntryTypeConfig->diaryTitleDisplayStringMsg, 
						STRFMT_ENTITY(pEnt), STRFMT_STRING("ArgString1", argString1), STRFMT_STRING("ArgString2", argString2), STRFMT_END);

					estrDestroy(&tmpString1);
					estrDestroy(&tmpString2);
				}
				else
				{
					const char *argString;
					if ( activityEntryTypeConfig->translateArg )
					{
						argString = TranslateMessageKey(logEntry->argString);
					}
					else
					{
						argString = logEntry->argString;
					}
					FormatGameDisplayMessage(&s_displayEntries[numDisplayEntries]->text, &activityEntryTypeConfig->diaryTitleDisplayStringMsg, 
						STRFMT_ENTITY(pEnt), STRFMT_STRING("ArgString", argString), STRFMT_END);
				}

				if ((!gActivityLogSearchStr) || strstri(s_displayEntries[numDisplayEntries]->text, gActivityLogSearchStr))
				{
					numDisplayEntries++;
				}
			}
		}
	}
	while (eaSize(&s_displayEntries) > numDisplayEntries)
	{
		StructDestroy(parse_ActivityLogDisplayEntry, eaPop(&s_displayEntries));
	}

	ui_GenSetList(pGen, &s_displayEntries, parse_ActivityLogDisplayEntry);
}

#include "gclActivityLog_h_ast.c"