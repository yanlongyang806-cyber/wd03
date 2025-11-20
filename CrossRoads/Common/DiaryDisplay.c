/***************************************************************************
*     Copyright (c) 2009, Cryptic Studios
*     All Rights Reserved
*     Confidential Property of Cryptic Studios
***************************************************************************/

#include "DiaryDisplay.h"
#include "DiaryCommon.h"
#include "nemesis_common.h"
#include "mission_common.h"
#include "EString.h"
#include "StashTable.h"
#include "Entity.h"
#include "EntitySavedData.h"
#include "ActivityLogCommon.h"
#include "GameStringFormat.h"
#include "StringUtil.h"
#include "UGCCommon.h"
#include "ugcProjectUtils.h"

#include "AutoGen/DiaryDisplay_h_ast.h"
#include "AutoGen/DiaryEnums_h_ast.h"
#include "AutoGen/DiaryCommon_h_ast.h"

AUTO_RUN_ANON(memBudgetAddMapping(__FILE__, BUDGET_UISystem););

// This function maps reference strings or message id strings to locally generated entry IDs
// It is used to keep track of local entry IDs that have been assigned to a
//  particular mission.

static StashTable s_missionRefToLocalEntryIDMap = NULL;
static StashTable s_localEntryIDToMissionRefMap = NULL;

static U32
GetLocalMissionEntryID(const char *refName)
{
	static U32 s_nextLocalMissionEntryID = 0x40000001;
	U32 result;

	if ( s_missionRefToLocalEntryIDMap == NULL )
	{
		s_missionRefToLocalEntryIDMap = stashTableCreateWithStringKeys(100, StashDefault);
		s_localEntryIDToMissionRefMap = stashTableCreateInt(100);
	}

	if ( stashFindInt(s_missionRefToLocalEntryIDMap, refName, (int *)&result) == false )
	{
		// didn't find an ID for this string, so make a new one

		// get the next available ID
		result = s_nextLocalMissionEntryID;
		s_nextLocalMissionEntryID++;

		// add it to the mission->id stash table
		stashAddInt(s_missionRefToLocalEntryIDMap, refName, (int)result, false);

		// add it to the id->mission stash table
		stashIntAddPointer(s_localEntryIDToMissionRefMap, result, (char *)refName, false);
	}

	return result;
}

const char *
DiaryDisplay_GetMissionNameFromLocalID(U32 entryID)
{
	char *missionName = NULL;

	stashIntFindPointer(s_localEntryIDToMissionRefMap, entryID, &missionName);

	return missionName;
}

DiaryEntryType
DiaryDisplay_GetEntryTypeFromLocalID(U32 entryID)
{
	DiaryEntryType entryType = DiaryEntryType_None;
	if ( ENTRY_ID_IS_LOCAL(entryID) )
	{
		if ( LOCAL_ENTRY_TYPE(entryID) == LOCAL_ENTRY_TYPE_MISSION )
		{
			// Warning: returning mission type for both perks and missions.  The code that depends
			//  on this value currently does the same thing for mission and perk entries.
			entryType = DiaryEntryType_Mission;
		}
		else if ( LOCAL_ENTRY_TYPE(entryID) == LOCAL_ENTRY_TYPE_ACTIVITY )
		{
			entryType = DiaryEntryType_Activity;
		}
	}

	return entryType;
}

static DiaryEntryType
GetEntryTypeFromEntryID(DiaryHeaders *headers, U32 entryID)
{
	DiaryEntryType entryType = DiaryEntryType_None;

	if ( ENTRY_ID_IS_LOCAL(entryID) )
	{
		entryType = DiaryDisplay_GetEntryTypeFromLocalID(entryID); 
	}
	else
	{
		DiaryHeader *header = eaIndexedGetUsingInt(&headers->headers, entryID);
		if ( header != NULL )
		{
			entryType = header->type;
		}
	}

	return entryType;
}

static const char *
GetMissionNameFromEntryID(DiaryHeaders *headers, U32 entryID)
{
	const char *missionName = NULL;

	if ( ENTRY_ID_IS_LOCAL(entryID) )
	{
		missionName = DiaryDisplay_GetMissionNameFromLocalID(entryID);
	}
	else
	{
		DiaryHeader *header = eaIndexedGetUsingInt(&headers->headers, entryID);
		if ( header != NULL )
		{
			missionName = header->refName;
		}
	}

	return missionName;
}

DiaryEntryReferences *
DiaryDisplay_CreateDiaryEntryReferences(void)
{
	DiaryEntryReferences *refs;

	refs = StructCreate(parse_DiaryEntryReferences);

	refs->missionsReferenced = stashTableCreateWithStringKeys(100, StashDefault);

	refs->activityLogEntriesReferenced = stashTableCreateInt(100);

	return refs;
}

void
DiaryDisplay_ResetDiaryEntryReferences(DiaryEntryReferences *refs)
{
	stashTableClear(refs->missionsReferenced);
	stashTableClear(refs->activityLogEntriesReferenced);
}

void
DiaryDisplay_FreeDiaryEntryReferences(DiaryEntryReferences *refs)
{

	StructDestroy(parse_DiaryEntryReferences, refs);
}

void
DiaryDisplay_AddMissionReference(DiaryEntryReferences *refs, const char *missionName)
{
	stashAddPointer(refs->missionsReferenced, missionName, NULL, false);
}

void
DiaryDisplay_AddActivityReference(DiaryEntryReferences *refs, U32 entryID)
{
	stashIntAddPointer(refs->activityLogEntriesReferenced, (int)entryID, NULL, false);
}

bool
DiaryDisplay_CheckMissionReference(DiaryEntryReferences *refs, const char *missionName)
{
	return stashFindPointer(refs->missionsReferenced, missionName, NULL);
}

bool
DiaryDisplay_CheckActivityReference(DiaryEntryReferences *refs, U32 entryID)
{
	return stashIntFindPointer(refs->activityLogEntriesReferenced, (int)entryID, NULL);
}

void
DiaryDisplay_PopulateDiaryEntryReferences(DiaryEntryReferences *refs, DiaryHeaders *headers)
{
	if ( headers != NULL )
	{
		// Record all the missions, perks and activity logs entries that the passed in headers refer to.
		// We can later query to find out if a given mission or activity log entry has a real diary entry.
		FOR_EACH_IN_EARRAY(headers->headers, DiaryHeader, header)
		{
			if ( ( ( header->type == DiaryEntryType_Mission) || ( header->type == DiaryEntryType_Perk ) ) && ( header->refName != NULL ) )
			{
				DiaryDisplay_AddMissionReference(refs, header->refName);
			}
			else if ( header->type == DiaryEntryType_Activity )
			{
				DiaryDisplay_AddActivityReference(refs, header->activityEntryID);
			}
		}
		FOR_EACH_END;
	}
}

static char *
GetMissionSummaryString(Language lang, Entity *pEnt, MissionDef *missionDef)
{
	ContainerID idNemesis = player_GetPrimaryNemesisID(pEnt);
	char *tmpstr = NULL;
	char *ret;

	// The translated text comes back as an estring, so we need to copy
	// it to a regular malloced string before stashing it in the comment,
	// since the comment's parse table needs a regular string.
	if(GET_REF(missionDef->summaryMsg.hMessage))
	{
		missionsystem_FormatMessagePtr(lang, "DiaryUI", pEnt, missionDef, idNemesis,
			&tmpstr, GET_REF(missionDef->summaryMsg.hMessage));
	}
	else
	{
		missionsystem_FormatMessagePtr(lang, "DiaryUI", pEnt, missionDef, idNemesis,
			&tmpstr, GET_REF(missionDef->uiStringMsg.hMessage));
	}

	// should we use detail string instead of summary?
	//missionsystem_ClientFormatMessagePtr("MissionUI", pEnt, missionDef, idNemesis,
	//	&comment->text, GET_REF(missionDef->detailStringMsg.hMessage));

	if ( ( tmpstr == NULL ) || ( tmpstr[0] == '\0' ) )
	{
		ret = StructAllocString("[Mission summary text missing]");
	}
	else
	{
		ret = StructAllocString(tmpstr);
	}

	estrDestroy(&tmpstr);
	return ret;
}

static void
FormatActivityLogString(Language lang, Entity *pEnt, ActivityLogEntry *activityLogEntry, char **stringHandle, bool title)
{
	ActivityLogEntryTypeConfig *activityEntryTypeConfig;
	DisplayMessage *displayMessage;

	activityEntryTypeConfig = ActivityLog_GetTypeConfig(activityLogEntry->type);
	if ( activityEntryTypeConfig != NULL )
	{
		// format the title string
		estrClear(stringHandle);

		if ( title )
		{
			displayMessage = &activityEntryTypeConfig->diaryTitleDisplayStringMsg;
		}
		else
		{
			displayMessage = &activityEntryTypeConfig->diaryBodyDisplayStringMsg;
		}

		if ( activityEntryTypeConfig->splitArgString ) 
		{
			static char *tmpString1 = NULL;
			static char *tmpString2 = NULL;
			const char *argString1;
			const char *argString2;

			estrClear(&tmpString1);
			estrClear(&tmpString2);

			estrAppend2(&tmpString1, activityLogEntry->argString);
			estrAppend2(&tmpString2, activityLogEntry->argString);

			estrTruncateAtFirstOccurrence(&tmpString1, ':');
			estrRemoveUpToFirstOccurrence(&tmpString2, ':');

			if ( activityEntryTypeConfig->translateArg )
			{
				argString1 = langTranslateMessageKey(lang, tmpString1);
			}
			else
			{
				argString1 = tmpString1;
			}

			if ( activityEntryTypeConfig->translateArg2 )
			{
				argString2 = langTranslateMessageKey(lang, tmpString2);
			}
			else
			{
				argString2 = tmpString2;
			}

			langFormatGameDisplayMessage(lang, stringHandle, displayMessage, STRFMT_PLAYER(pEnt), 
				STRFMT_STRING("ArgString1", argString1), STRFMT_STRING("ArgString2", argString2), STRFMT_END);

		}
		else
		{
			const char *argString;
			if ( activityEntryTypeConfig->translateArg )
			{
				argString = langTranslateMessageKey(lang, activityLogEntry->argString);
			}
			else
			{
				argString = activityLogEntry->argString;
			}
			langFormatGameDisplayMessage(lang, stringHandle, displayMessage, STRFMT_PLAYER(pEnt), 
				STRFMT_STRING("ArgString", argString), STRFMT_END);
		}
	}
}

static void
GetMissionEntryTitle(Language lang, Entity *pEnt, MissionDef *missionDef, char **titleStringHandle)
{
	ContainerID idNemesis = player_GetPrimaryNemesisID(pEnt);

	estrClear(titleStringHandle);
	missionsystem_FormatMessagePtr(lang, "DiaryUI", pEnt, missionDef, idNemesis,
		titleStringHandle, GET_REF(missionDef->displayNameMsg.hMessage));

	return;
}

static void
PopulateDisplayHeaderFromHeader(Language lang, DiaryDisplayHeader *displayHeader, DiaryHeader *header, Entity *pEnt)
{
	displayHeader->displayTime = header->time;
	displayHeader->type = header->type;
	displayHeader->entryID = header->entryID;
	displayHeader->tagBits = header->tagBits;

	estrClear(&displayHeader->title);
	if ( ( header->type == DiaryEntryType_Mission ) || ( header->type == DiaryEntryType_Perk) )
	{
		MissionDef *missionDef = missiondef_DefFromRefString(header->refName);

		if ( missionDef != NULL )
		{
			GetMissionEntryTitle(lang, pEnt, missionDef, &displayHeader->title);
		}
		else
		{
			estrClear(&displayHeader->title);
			estrAppend2(&displayHeader->title, "[Invalid Mission]");
		}
		displayHeader->missionName = header->refName;
	}
	else if ( header->type == DiaryEntryType_Activity )
	{
		ActivityLogEntry *activityLogEntry;

		displayHeader->missionName = NULL;

		// get the activity entry
		activityLogEntry = eaIndexedGetUsingInt(&pEnt->pSaved->activityLogEntries, header->activityEntryID);
		if ( activityLogEntry != NULL )
		{
			FormatActivityLogString(lang, pEnt, activityLogEntry, &displayHeader->title, true);
		}
	}
	else
	{
		if ( header->title != NULL )
		{
			// Unescape title.  The title in DiaryHeader is always escaped, while the title in
			//  DiaryDisplayHeader is not escaped.
			estrAppendUnescaped(&displayHeader->title, header->title);
		}
		displayHeader->missionName = NULL;
	}
}

void PopulateSimpleMissionCompleteDisplayHeader(Language lang, DiaryDisplayHeader *displayHeader, const char *missionName, Entity *pEnt, U32 time, U32 entryID)
{
	ContainerID idNemesis = player_GetPrimaryNemesisID(pEnt);
	MissionDef* missionDef = missiondef_DefFromRefString(missionName);

	estrClear(&displayHeader->title);
	if(missionDef)
	{
		missionsystem_FormatMessagePtr(lang, "DiaryUI", pEnt, missionDef, idNemesis,
			&displayHeader->title, GET_REF(missionDef->displayNameMsg.hMessage));
	}

	if ( missionDef && missionDef->missionType == MissionType_Perk )
	{
		displayHeader->type = DiaryEntryType_Perk;
	}
	else
	{
		displayHeader->type = DiaryEntryType_Mission;
	}


	displayHeader->displayTime = time;
	displayHeader->entryID = LOCAL_ID_FROM_ACTIVITY_ID(entryID);
	displayHeader->tagBits = 0;
	displayHeader->missionName = missionName;
}

static void
PopulateDisplayHeaderFromMissionDef(Language lang, DiaryDisplayHeader *displayHeader, MissionDef *missionDef, Entity *pEnt, U32 time)
{
	ContainerID idNemesis = player_GetPrimaryNemesisID(pEnt);
	char* estrTitle = NULL;

	estrStackCreate(&estrTitle);
	estrClear(&displayHeader->title);
	missionsystem_FormatMessagePtr(lang, "DiaryUI", pEnt, missionDef, idNemesis,
		&estrTitle, GET_REF(missionDef->displayNameMsg.hMessage));
	StringStripTagsPrettyPrint(estrTitle, &displayHeader->title);
	estrDestroy(&estrTitle);

	displayHeader->displayTime = time;
	displayHeader->entryID = GetLocalMissionEntryID(missionDef->name);
	displayHeader->tagBits = 0;
	if ( missionDef->missionType == MissionType_Perk )
	{
		displayHeader->type = DiaryEntryType_Perk;
	}
	else
	{
		displayHeader->type = DiaryEntryType_Mission;
	}
	displayHeader->missionName = missionDef->name;
}

static void
PopulateDisplayHeaderFromMission(Language lang, DiaryDisplayHeader *displayHeader, Mission *mission, Entity *pEnt)
{
	MissionDef *missionDef = mission_GetDef(mission);

	PopulateDisplayHeaderFromMissionDef(lang, displayHeader, missionDef, pEnt, mission->startTime);
}

static void
PopulateDisplayHeaderFromCompletedMission(Language lang, DiaryDisplayHeader *displayHeader, CompletedMission *completedMission, Entity *pEnt)
{
	MissionDef *missionDef = GET_REF(completedMission->def);

	PopulateDisplayHeaderFromMissionDef(lang, displayHeader, missionDef, pEnt, completedmission_GetLastCompletedTime(completedMission));
}

static void
PopulateDisplayHeaderFromActivityLogEntry(Language lang, DiaryDisplayHeader *displayHeader, ActivityLogEntry *activityEntry, Entity *pEnt)
{
	ActivityLogEntryTypeConfig *activityEntryTypeConfig;

	// Special handling for SimpleMissionComplete logs
	if(activityEntry->type == ActivityLogEntryType_SimpleMissionComplete)
	{
		PopulateSimpleMissionCompleteDisplayHeader(lang, displayHeader, activityEntry->argString, pEnt, activityEntry->time, activityEntry->entryID);
		return;
	}

	activityEntryTypeConfig = ActivityLog_GetTypeConfig(activityEntry->type);

	if ( activityEntryTypeConfig != NULL )
	{
		displayHeader->displayTime = activityEntry->time;
		displayHeader->type = DiaryEntryType_Activity;
		displayHeader->tagBits = 0;
		displayHeader->entryID = LOCAL_ID_FROM_ACTIVITY_ID(activityEntry->entryID);
		displayHeader->missionName = NULL;

		// format the title string
		FormatActivityLogString(lang, pEnt, activityEntry, &displayHeader->title, true);
	}
}

static int 
SortDisplayHeadersByTime(const DiaryDisplayHeader **displayHeader1, const DiaryDisplayHeader **displayHeader2)
{
	if ((*displayHeader1)->displayTime > (*displayHeader2)->displayTime)
	{
		return -1;
	}
	else if ((*displayHeader1)->displayTime < (*displayHeader2)->displayTime) 
	{
		return 1;
	}
	return 0;
}

static bool
CheckFilter(DiaryDisplayHeader *displayHeader, DiaryFilter *filter)
{
	if ( ( filter->startTime != 0 ) && ( displayHeader->displayTime < filter->startTime ) )
	{
		return false;
	}

	if ( ( filter->endTime != 0 ) && ( displayHeader->displayTime > filter->endTime ) )
	{
		return false;
	}

	if ( ( filter->type != DiaryEntryType_None ) && ( filter->type != displayHeader->type ) )
	{
		return false;
	}

	if ( ( filter->titleSubstring != NULL ) && ( ( displayHeader->title == NULL ) || ( strstri(displayHeader->title, filter->titleSubstring) == NULL ) ) )
	{
		return false;
	}

	if ( ( filter->tagIncludeMask != 0 ) && ( ( filter->tagIncludeMask & displayHeader->tagBits ) == 0 ) )
	{
		return false;
	}

	if ( ( filter->tagExcludeMask != 0 ) && ( ( filter->tagExcludeMask & displayHeader->tagBits ) != 0 ) )
	{
		return false;
	}

	return true;
}

int
DiaryDisplay_GenerateDisplayHeaders(Language lang, SA_PARAM_OP_VALID Entity *pEnt, DiaryHeaders *diaryHeaders, DiaryFilter *filter, DiaryDisplayHeader ***diaryDisplayHeadersArray, bool freeExtraDisplayHeaders, DiaryEntryReferences *refs)
{
	int i, n;
	int numDisplayHeaders = 0;
	MissionInfo *pInfo;
	MissionDef *missionDef;
	MissionDef **eaSimpleCompletedMissions = NULL;

	// add any entries from the server
	if ( diaryHeaders != NULL )
	{
		n = eaSize(&diaryHeaders->headers);
		for ( i = 0; i < n; i++ )
		{
			// expand s_diaryDisplayHeaders as needed
			while (eaSize(diaryDisplayHeadersArray) <= numDisplayHeaders)
			{
				eaPush(diaryDisplayHeadersArray, StructCreate(parse_DiaryDisplayHeader));
			}

			PopulateDisplayHeaderFromHeader(lang, (* diaryDisplayHeadersArray)[numDisplayHeaders], diaryHeaders->headers[i], pEnt);

			if ( ( filter == NULL ) || CheckFilter((* diaryDisplayHeadersArray)[numDisplayHeaders], filter) )
			{
				// only keep the entry if the filter passes
				numDisplayHeaders++;
			}
		}
	}

	pInfo = mission_GetInfoFromPlayer(pEnt);
	if ( pInfo != NULL )
	{
		// add current missions
		n = eaSize(&pInfo->missions);
		for (i = 0; i < n; i++)
		{
			Mission *mission = pInfo->missions[i];

			if ( mission->bHiddenFullChild == false )
			{
				missionDef = mission_GetDef(mission);

				if ( missionDef != NULL )
				{
					const char *missionName = missionDef->name;
					CompletedMission *completedMission = eaIndexedGetUsingString(&pInfo->completedMissions, missionName);

					// Only add the mission as a local entry if we don't already have it in the diary.
					// Also don't add it here if we are going to add it as a completed mission below.  This prevents
					//  us from displaying the mission twice if the player is repeating a mission that has already been
					//  completed.
					if ( ( completedMission == NULL ) && ( DiaryDisplay_CheckMissionReference(refs, missionName) == false ) )
					{
						if ( mission_HasDisplayName(mission) && DiaryConfig_AutoAddCurrentMissionType(mission_GetType(mission)))
						{
							// expand s_diaryDisplayHeaders as needed
							while (eaSize(diaryDisplayHeadersArray) <= numDisplayHeaders)
							{
								eaPush(diaryDisplayHeadersArray, StructCreate(parse_DiaryDisplayHeader));
							}

							PopulateDisplayHeaderFromMission(lang, (*diaryDisplayHeadersArray)[numDisplayHeaders], mission, pEnt);

							if(missionDef->bDisableCompletionTracking)
							{
								eaPush(&eaSimpleCompletedMissions, missionDef);
							}

							if ( ( filter == NULL ) || CheckFilter((*diaryDisplayHeadersArray)[numDisplayHeaders], filter) )
							{
								// only keep the entry if the filter passes
								numDisplayHeaders++;
							}
						}
					}
				}
			}
		}

		// add completed missions
		n = eaSize(&pInfo->completedMissions);
		for (i = 0; i < n; i++)
		{
			CompletedMission *completedMission = pInfo->completedMissions[i];
			if ( completedMission->bHidden == false )
			{
				missionDef = GET_REF(completedMission->def);

				// only add the mission as a local entry if we don't already have it in the diary
				if ( ( missionDef != NULL ) && ( DiaryDisplay_CheckMissionReference(refs, missionDef->name) == false ) )
				{
					if (missiondef_HasDisplayName(missionDef) && DiaryConfig_AutoAddCompletedMissionType(missiondef_GetType(missionDef)))
					{
						// expand s_diaryDisplayHeaders as needed
						while (eaSize(diaryDisplayHeadersArray) <= numDisplayHeaders)
						{
							eaPush(diaryDisplayHeadersArray, StructCreate(parse_DiaryDisplayHeader));
						}

						PopulateDisplayHeaderFromCompletedMission(lang, (*diaryDisplayHeadersArray)[numDisplayHeaders], completedMission, pEnt);

						if ( ( filter == NULL ) || CheckFilter((*diaryDisplayHeadersArray)[numDisplayHeaders], filter) )
						{
							// only keep the entry if the filter passes
							numDisplayHeaders++;
						}
					}
				}
			}
		}

		for (i = eaSize(&pEnt->pSaved->activityLogEntries)-1; i >= 0; i--)
		{
			ActivityLogEntry *activityEntry = pEnt->pSaved->activityLogEntries[i];

			// only add local entries if we don't have a real diary entry for this activity log
			if ( DiaryDisplay_CheckActivityReference(refs, activityEntry->entryID) == false)
			{
				MissionDef* pSimpleMissionDef = NULL;
				if(activityEntry->type == ActivityLogEntryType_SimpleMissionComplete)
				{
					pSimpleMissionDef = missiondef_DefFromRefString(activityEntry->argString);
					if(DiaryDisplay_CheckMissionReference(refs, activityEntry->argString))
						continue;
					if(pSimpleMissionDef && eaFind(&eaSimpleCompletedMissions, pSimpleMissionDef) > -1)
						continue;
				}
				// expand s_diaryDisplayHeaders as needed
				while (eaSize(diaryDisplayHeadersArray) <= numDisplayHeaders)
				{
					eaPush(diaryDisplayHeadersArray, StructCreate(parse_DiaryDisplayHeader));
				}

				PopulateDisplayHeaderFromActivityLogEntry(lang, (*diaryDisplayHeadersArray)[numDisplayHeaders], activityEntry, pEnt);

				if(activityEntry->type == ActivityLogEntryType_SimpleMissionComplete && pSimpleMissionDef)
				{
					eaPush(&eaSimpleCompletedMissions, pSimpleMissionDef);
				}

				if ( ( (*diaryDisplayHeadersArray)[numDisplayHeaders]->entryID != 0 ) && ( ( filter == NULL ) || CheckFilter((*diaryDisplayHeadersArray)[numDisplayHeaders], filter) ) )
				{
					// only keep the entry if the entryID is valid and the filter passes
					numDisplayHeaders++;
				}
			}
		}
	}
	if ( freeExtraDisplayHeaders )
	{
		while (eaSize(diaryDisplayHeadersArray) > numDisplayHeaders)
		{
			StructDestroy(parse_DiaryDisplayHeader, eaPop(diaryDisplayHeadersArray));
		}
	}

	if ( eaSize(diaryDisplayHeadersArray) > 0 )
	{
		// Sort the headers by time, so the default view shows them in a reasonable
		//  order.  Need to find a way to detect if the view is already sorted, so we
		//  don't need to do 2 sorts per frame. TODO(jsw)
		eaQSort(*diaryDisplayHeadersArray, SortDisplayHeadersByTime);
	}

	if(eaSimpleCompletedMissions)
		eaDestroy(&eaSimpleCompletedMissions);

	return numDisplayHeaders;
}

void 
DiaryDisplay_FormatStardate(char **destString, U32 seconds)
{
	float stardate;

	estrClear(destString);

	stardate = timerStardateFromSecondsSince2000(seconds);

	estrConcatf(destString, "%.2f", stardate);

	return;
}

static void
FormatComment(Language lang, Entity *pEnt, DiaryDisplayComment *displayComment, bool allowMarkup, bool breakOnNewline)
{
	static char *s_tmpStardate = NULL;
	static char *s_tmpBody = NULL;
	static char *s_tmpTitle = NULL;

	estrClear(&displayComment->formattedText);

	DiaryDisplay_FormatStardate(&s_tmpStardate, displayComment->comment->time);

	estrClear(&s_tmpBody);
	estrClear(&s_tmpTitle);

	// unescape the body string
	if ( displayComment->comment->text != NULL )
	{
		estrAppendUnescaped(&s_tmpBody, displayComment->comment->text);
	}
	if ( displayComment->comment->title != NULL )
	{
		estrAppendUnescaped(&s_tmpTitle, displayComment->comment->title);
	}

	// prevent any embedded SMF tags from being interpreted by the SMF gen
	if ( !allowMarkup )
	{
		estrReplaceOccurrences(&s_tmpBody, "<", "&lt;");
		estrReplaceOccurrences(&s_tmpTitle, "<", "&lt;");
	}

	// replace newlines with HTML line breaks
	if ( breakOnNewline )
	{
		estrReplaceOccurrences(&s_tmpBody, "\n", "<br>");
	}

	langFormatGameDisplayMessage(lang, &displayComment->formattedText, DiaryConfig_CommentFormatMessage(), 
		STRFMT_PLAYER(pEnt), 
		STRFMT_STRING("Stardate", s_tmpStardate),
		STRFMT_DATETIME("Date", displayComment->comment->time),
		STRFMT_STRING("Header", displayComment->commentHeader),
		STRFMT_STRING("Title", s_tmpTitle),
		STRFMT_STRING("Body", s_tmpBody),
		STRFMT_END);

	return;
}

//
// Populate the passed in comment struct with info from a mission name
//
static void
PopulateDisplayCommentFromMission(Language lang, Entity *pEnt, NOCONST(DiaryComment) **ppComment, char **ppCommentHeader, const char* missionName)
{
	NOCONST(DiaryComment) *comment = (*ppComment);
	CompletedMission *completedMission;
	Mission *mission;
	static char *s_titleTmp = NULL;

	if ( missionName != NULL )
	{
		MissionDef *missionDef = missiondef_DefFromRefString(missionName);
		MissionInfo *missionInfo = mission_GetInfoFromPlayer(pEnt);

		if ( ( missionDef != NULL ) && ( missionInfo != NULL ) )
		{
			char pchUGCIDString[UGC_IDSTRING_LENGTH_BUFFER_LENGTH];
			comment->text = GetMissionSummaryString(lang, pEnt, missionDef);

			GetMissionEntryTitle(lang, pEnt, missionDef, &s_titleTmp);
			comment->title = StructAllocString(s_titleTmp);

			completedMission = eaIndexedGetUsingString(&missionInfo->completedMissions, missionName);

			if(missionDef->ugcProjectID)
			{
				UGCIDString_IntToString(missionDef->ugcProjectID, /*isSeries=*/false, pchUGCIDString); 
			}

			if ( completedMission )
			{
				comment->time = completedmission_GetLastCompletedTime(completedMission);
				if ( eaSize(&completedMission->eaStats) > 0 )
				{
					// mission has been repeated multiple times
					CompletedMissionStats *missionStats = completedMission->eaStats[0];
					langFormatGameDisplayMessage(lang, ppCommentHeader, DiaryConfig_RepeatedMissionHeaderFormatMessage(), 
						STRFMT_TIMER("BestTime", missionStats->bestTime),
						STRFMT_INT("CompletedCount", missionStats->timesRepeated + 1),
						STRFMT_INT("IsUGC", !!(missionDef->ugcProjectID)),
						STRFMT_STRING("UGCID", missionDef->ugcProjectID?pchUGCIDString:""),
						STRFMT_END);
				}
				else
				{
					// mission has been completed once
					langFormatGameDisplayMessage(lang, ppCommentHeader, DiaryConfig_CompletedMissionHeaderFormatMessage(), 
						STRFMT_TIMER("BestTime", completedMission->completedTime - completedMission->startTime),
						STRFMT_INT("CompletedCount",1),
						STRFMT_INT("IsUGC", !!(missionDef->ugcProjectID)),
						STRFMT_STRING("UGCID", missionDef->ugcProjectID?pchUGCIDString:""),
						STRFMT_END);
				}
			}
			else
			{
				mission = mission_GetMissionByName(missionInfo, missionName);
				if ( mission != NULL )
				{
					comment->time = mission->startTime;

					langFormatGameDisplayMessage(lang, ppCommentHeader, DiaryConfig_MissionHeaderFormatMessage(), 
						STRFMT_INT("IsUGC", !!(missionDef->ugcProjectID)),
						STRFMT_STRING("UGCID", missionDef->ugcProjectID?pchUGCIDString:""),
						STRFMT_END);
				}
			}
		}
	}
}

//
// Populate the passed in comment struct with info from a mission.
//
static void
PopulateMissionComment(Language lang, Entity *pEnt, DiaryHeaders *headers, DiaryDisplayComment *displayComment, U32 entryID)
{
	NOCONST(DiaryComment) *comment = CONTAINER_NOCONST(DiaryComment, displayComment->comment);
	const char *missionName = GetMissionNameFromEntryID(headers, entryID);

	StructResetNoConst(parse_DiaryComment, comment);

	comment->id = 0;
	displayComment->id = 0;
	estrClear(&displayComment->commentHeader);

	PopulateDisplayCommentFromMission(lang, pEnt, &comment, &displayComment->commentHeader, missionName);
}

//
// Populate the passed in comment struct with info from an activity log entry.
//
static void
PopulateActivityComment(Language lang, Entity *pEnt, NOCONST(DiaryComment) *comment, U32 entryID, DiaryDisplayComment *displayComment, DiaryHeaders *diaryHeaders)
{
	ActivityLogEntry *activityEntry;
	U32 activityLogID = 0;
	if ( ENTRY_ID_IS_LOCAL(entryID) )
	{
		activityLogID = ACTIVITY_ID_FROM_LOCAL_ID(entryID);
	}
	else
	{
		DiaryHeader * diaryHeader = eaIndexedGetUsingInt(&diaryHeaders->headers, entryID);
		if ( diaryHeader != NULL )
		{
			activityLogID = diaryHeader->activityEntryID;
		}
	}

	if ( activityLogID != 0 )
	{
		activityEntry = eaIndexedGetUsingInt(&pEnt->pSaved->activityLogEntries, activityLogID);
		if ( activityEntry != NULL )
		{
			static char *s_tmpStr = NULL;

			if(activityEntry->type == ActivityLogEntryType_SimpleMissionComplete)
			{
				MissionDef* missionDef = missiondef_DefFromRefString(activityEntry->argString);
				if(missionDef)
				{
					PopulateDisplayCommentFromMission(lang, pEnt, &comment, &displayComment->commentHeader, activityEntry->argString);
				}
				comment->time = activityEntry->time;
			}
			else
			{
				comment->time = activityEntry->time;

				FormatActivityLogString(lang, pEnt, activityEntry, &s_tmpStr, true);
				comment->title = StructAllocString(s_tmpStr);

				FormatActivityLogString(lang, pEnt, activityEntry, &s_tmpStr, false);
				comment->text = StructAllocString(s_tmpStr);
			}
		}
	}
}

int
DiaryDisplay_GenerateDisplayComments(Language lang, SA_PARAM_OP_VALID Entity *pEnt, DiaryHeaders *diaryHeaders, NOCONST(DiaryComment) *generatedComment, DiaryEntry *diaryEntry, U32 entryID, DiaryDisplayComment ***diaryDisplayCommentsArray, bool freeExtraDisplayComments)
{
	int numDisplayComments = 0;
	DiaryEntryType entryType;
	DiaryDisplayComment *displayComment;

	if ( diaryHeaders == NULL || pEnt == NULL )
	{
		if ( freeExtraDisplayComments )
			eaDestroyStruct(diaryDisplayCommentsArray, parse_DiaryDisplayComment);
		return 0;
	}

	// NOTE - this call will return Mission for both Mission or Perk entries if it is locally generated
	entryType = GetEntryTypeFromEntryID(diaryHeaders, entryID);

	// generate first comment for missions and activities
	if ( ( entryType == DiaryEntryType_Mission ) || ( entryType == DiaryEntryType_Perk ) )
	{
		if ( eaSize(diaryDisplayCommentsArray) == 0 )
		{
			// allocate the first display comment
			eaPush(diaryDisplayCommentsArray, StructCreate(parse_DiaryDisplayComment));
		}

		displayComment = (*diaryDisplayCommentsArray)[0];
		displayComment->comment = (DiaryComment *)generatedComment;
		PopulateMissionComment(lang, pEnt, diaryHeaders, displayComment, entryID);
		FormatComment(lang, pEnt, displayComment, true, false);

		numDisplayComments = 1;
	}
	else if ( entryType == DiaryEntryType_Activity )
	{
		if ( eaSize(diaryDisplayCommentsArray) == 0 )
		{
			// allocate the first display comment
			eaPush(diaryDisplayCommentsArray, StructCreate(parse_DiaryDisplayComment));
		}

		displayComment = (*diaryDisplayCommentsArray)[0];
		estrClear(&displayComment->commentHeader);
		PopulateActivityComment(lang, pEnt, generatedComment, entryID, displayComment, diaryHeaders);
		displayComment->comment = (DiaryComment *)generatedComment;
		displayComment->id = generatedComment->id;
		FormatComment(lang, pEnt, displayComment, true, false);

		numDisplayComments = 1;
	}

	if ( ( diaryEntry != NULL ) && ( entryID == diaryEntry->entryID ) )
	{
		int i;
		int n;

		n = eaSize(&diaryEntry->comments);
		for ( i = 0; i < n; i++ )
		{
			// expand s_displayComments as needed
			while (eaSize(diaryDisplayCommentsArray) <= numDisplayComments)
			{
				eaPush(diaryDisplayCommentsArray, StructCreate(parse_DiaryDisplayComment));
			}

			displayComment = (*diaryDisplayCommentsArray)[numDisplayComments];
			displayComment->comment = (DiaryComment *)diaryEntry->comments[i];
			displayComment->id = diaryEntry->comments[i]->id;
			estrClear(&displayComment->commentHeader);

			FormatComment(lang, pEnt, displayComment, false, true);

			numDisplayComments++;

		}
	} 

	if ( freeExtraDisplayComments )
	{
		// clean up any excess row structures
		while (eaSize(diaryDisplayCommentsArray) > numDisplayComments)
		{
			StructDestroy(parse_DiaryDisplayComment, eaPop(diaryDisplayCommentsArray));
		}
	}

	if ( eaSize(diaryDisplayCommentsArray) > 0 )
	{
		(*diaryDisplayCommentsArray)[0]->isFirst = true;
	}

	return numDisplayComments;
}

#include "DiaryDisplay_h_ast.c"
