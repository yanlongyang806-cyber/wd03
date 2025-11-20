/***************************************************************************
*     Copyright (c) 2009, Cryptic Studios
*     All Rights Reserved
*     Confidential Property of Cryptic Studios
***************************************************************************/

#include "gclDiary.h"
#include "DiaryCommon.h"
#include "Entity.h"
#include "UIGen.h"
#include "EString.h"
#include "GameStringFormat.h"
#include "Message.h"
#include "timing.h"
#include "missionui_eval.h"
#include "mission_common.h"
#include "nemesis_common.h"
#include "gclEntity.h"
#include "StringCache.h"
#include "ActivityLogCommon.h"
#include "EntitySavedData.h"
#include "DiaryDisplay.h"

#include "AutoGen/GameServerLib_autogen_ServerCmdWrappers.h"
#include "AutoGen/DiaryCommon_h_ast.h"
#include "AutoGen/DiaryEnums_h_ast.h"
#include "AutoGen/gclDiary_h_ast.h"
#include "AutoGen/DiaryDisplay_h_ast.h"

AUTO_RUN_ANON(memBudgetAddMapping(__FILE__, BUDGET_UISystem););
AUTO_RUN_ANON(memBudgetAddMapping("DiaryHeaders", BUDGET_UISystem););
AUTO_RUN_ANON(memBudgetAddMapping("DiaryHeader", BUDGET_UISystem););
AUTO_RUN_ANON(memBudgetAddMapping("DiaryEntry", BUDGET_UISystem););
AUTO_RUN_ANON(memBudgetAddMapping("DiaryComment", BUDGET_UISystem););

static NOCONST(DiaryHeaders) *s_diaryHeaders = NULL;
static NOCONST(DiaryEntry) *s_diaryEntry = NULL;
static U32 s_lastRequestedID = 0;
static DiaryDisplayHeader **s_diaryDisplayHeaders = NULL;
static bool s_diaryEntryDirty = true;

// this stash table maps entry IDs to display headers
static StashTable s_displayHeaderMap = NULL;

// tagging related statics
static STRING_EARRAY s_playerTags = NULL;
static EARRAY_OF(TagListRow) s_sortedTags = NULL;
static EARRAY_OF(TagListRow) s_sortedTagsCanDelete = NULL;

// keeps track of various set of scratch tag bits that the UI may be using
//static U64 s_workingTagBits = 0;
static StashTable s_scratchTagBits = NULL;

static DiaryEntryReferences *s_DiaryEntryRefs = NULL;

static NOCONST(DiaryFilter) *s_currentFilter = NULL;

static NOCONST(DiaryFilter) *s_defaultFilter = NULL;


AUTO_EXPR_FUNC(UIGen) ACMD_NAME(FormatStardate);
const char * 
exprDiaryFormatStardate(U32 seconds)
{
	static char *s_pch = NULL;

	DiaryDisplay_FormatStardate(&s_pch, seconds);

	return s_pch ? s_pch : "";
}

DiaryDisplayHeader *
DisplayHeaderFromEntryID(U32 entryID)
{
	DiaryDisplayHeader *header;
	stashIntFindPointer(s_displayHeaderMap, entryID, &header);

	return header;
}

//
// Implement named sets of scratch tag bits that can be used by different
//  parts of the UI at the same time, and not interfere with each other.
//
U64 *
GetScratchTagBits(const char *setName)
{
	U64 *ret;
	const char *pooledSetName = allocAddString(setName);

	if ( s_scratchTagBits == NULL )
	{
		s_scratchTagBits = stashTableCreateWithStringKeys(100, StashDefault);
	}

	if ( !stashFindPointer(s_scratchTagBits, pooledSetName, &ret) )
	{
		// if we didn't find an entry in the table, then create one
		ret = (U64 *)malloc(sizeof(U64));
		*ret = 0;
		stashAddPointer(s_scratchTagBits, pooledSetName, ret, false);
	}

	return ret;
}

NOCONST(DiaryFilter) *
GetCurrentFilter()
{
	if ( s_currentFilter == NULL )
	{
		s_currentFilter = StructCreateNoConst(parse_DiaryFilter);
	}

	return s_currentFilter;
}

AUTO_EXPR_FUNC(UIGen) ACMD_NAME(DiarySetCurrentFilter);
void 
exprDiarySetCurrentFilter(int entryType, const char *titleSubString, const char *tagSetInclude, const char *tagSetExclude)
{
	NOCONST(DiaryFilter) *filter = GetCurrentFilter();
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

	pTagBits = GetScratchTagBits(tagSetInclude);
	filter->tagIncludeMask = *pTagBits;

	pTagBits = GetScratchTagBits(tagSetExclude);
	filter->tagExcludeMask = *pTagBits;

	return;
}

AUTO_COMMAND ACMD_ACCESSLEVEL(0) ACMD_CLIENTCMD ACMD_PRIVATE ACMD_CATEGORY(Diary);
void
gclDiary_SetDefaultFilter(DiaryFilter *filter)
{
	NOCONST(DiaryFilter) *currentFilter = GetCurrentFilter();

	if ( s_defaultFilter != NULL )
	{
		StructDestroyNoConst(parse_DiaryFilter, s_defaultFilter);
		s_defaultFilter = NULL;
	}

	if ( filter != NULL )
	{
		s_defaultFilter = StructCloneDeConst(parse_DiaryFilter, filter);

		// Copy to the current filter as well.
		StructCopyAllNoConst(parse_DiaryFilter, s_defaultFilter, currentFilter);
	}

}

AUTO_EXPR_FUNC(UIGen) ACMD_NAME(DiarySaveCurrentFilterAsDefault);
void 
exprDiarySaveCurrentFilterAsDefault(void)
{
	ServerCmd_gslDiary_SetDefaultFilter((DiaryFilter *)GetCurrentFilter());
}

AUTO_EXPR_FUNC(UIGen) ACMD_NAME(DiaryCurrentFilterFromDefault);
void 
exprDiaryCurrentFilterFromDefault(void)
{
	NOCONST(DiaryFilter) *currentFilter = GetCurrentFilter();

	if ( s_defaultFilter != NULL )
	{
		StructCopyAllNoConst(parse_DiaryFilter, s_defaultFilter, currentFilter);
	}
}

static int 
CompareTagStrings(const TagListRow** left, const TagListRow** right)
{
	return stricmp((*left)->String,(*right)->String);
}

AUTO_COMMAND ACMD_ACCESSLEVEL(0) ACMD_CLIENTCMD ACMD_PRIVATE ACMD_CATEGORY(Diary);
void
gclDiary_SetTagStrings(DiaryTagStrings *tagStrings)
{
	int i;
	int numTagStrings = 0;

	eaClear(&s_playerTags);
	eaClear(&s_sortedTags);
	eaClear(&s_sortedTagsCanDelete);
	
	for ( i = 0; i < eaSize(&tagStrings->tagStrings); i++ )
	{
		DiaryTagString *tagString = tagStrings->tagStrings[i];
		if ( tagString == NULL )
		{
			eaPush(&s_playerTags, NULL);
		}
		else
		{
			// tag strings are pooled, so we can just push pointers
			eaPush(&s_playerTags, (char *)tagString->tagName);

			// expand s_sortedTags as needed
			while (eaSize(&s_sortedTags) <= numTagStrings)
			{
				eaPush(&s_sortedTags, StructCreate(parse_TagListRow));
			}
			s_sortedTags[numTagStrings]->String = tagString->tagName;
			s_sortedTags[numTagStrings]->bitNum = i;
			s_sortedTags[numTagStrings]->permanent = tagString->permanent;
			numTagStrings++;
		}
	}

	while (eaSize(&s_sortedTags) > numTagStrings)
	{
		StructDestroy(parse_TagListRow, eaPop(&s_sortedTags));
	}

	// sort the tags
	if ( eaSize(&s_sortedTags) > 1 )
	{
		eaQSort(s_sortedTags, CompareTagStrings);
	}

	for ( i = 0; i < eaSize(&s_sortedTags); i++ )
	{
		if ( s_sortedTags[i]->permanent == false )
		{
			eaPush(&s_sortedTagsCanDelete, s_sortedTags[i]);
		}
	}
	
}

AUTO_EXPR_FUNC(UIGen) ACMD_NAME(GetDiaryTagStrings);
void 
exprGetDiaryTagStrings(SA_PARAM_NN_VALID UIGen *pGen)
{
	ui_GenSetList(pGen, &s_sortedTags, parse_TagListRow);
}

AUTO_EXPR_FUNC(UIGen) ACMD_NAME(GetDiaryTagStringsCanDelete);
void 
exprGetDiaryTagStringsCanDelete(SA_PARAM_NN_VALID UIGen *pGen)
{
	ui_GenSetList(pGen, &s_sortedTagsCanDelete, parse_TagListRow);
}

AUTO_EXPR_FUNC(UIGen) ACMD_NAME(DiaryDeleteTagString);
void 
exprDiaryDeleteTagString(const char *tagString)
{
	ServerCmd_gslDiary_RemoveTagString(tagString);
}

AUTO_EXPR_FUNC(UIGen) ACMD_NAME(DiaryAddTagString);
void 
exprDiaryAddTagString(const char *tagString)
{
	ServerCmd_gslDiary_AddTagString(tagString);
}

AUTO_EXPR_FUNC(UIGen) ACMD_NAME(DiaryCanAddTagString);
bool
exprCanDiaryAddTagString()
{
	return ( eaSize(&s_sortedTags) < DIARY_MAX_TAGS_PER_PLAYER );
}

AUTO_EXPR_FUNC(UIGen) ACMD_NAME(GetDiaryTagOn);
bool
exprGetDiaryTagOn(const char *setName, int tagBit)
{
	U64 tagMask;
	U64 *pTagBits;

	if ( tagBit >= DIARY_MAX_TAGS_PER_PLAYER )
	{
		return false;
	}

	tagMask = ((U64)1)<<tagBit;

	pTagBits = GetScratchTagBits(setName);

	return ( (*pTagBits) & tagMask ) == tagMask;
}

AUTO_EXPR_FUNC(UIGen) ACMD_NAME(DiaryTagSet);
void
exprDiaryTagSet(const char *setName, int tagBit, bool on)
{
	U64 tagMask;
	U64 *pTagBits;

	if ( tagBit >= DIARY_MAX_TAGS_PER_PLAYER )
	{
		return;
	}

	tagMask = ((U64)1)<<tagBit;

	pTagBits = GetScratchTagBits(setName);

	if ( on )
	{
		(*pTagBits) = (*pTagBits) | tagMask;
	}
	else
	{
		(*pTagBits) = (*pTagBits) & (~tagMask);
	}
}

AUTO_EXPR_FUNC(UIGen) ACMD_NAME(DiaryTagsClear);
void
exprDiaryTagsClear(const char *setName)
{
	U64 *pTagBits;

	pTagBits = GetScratchTagBits(setName);

	(*pTagBits) = 0;
}

AUTO_EXPR_FUNC(UIGen) ACMD_NAME(DiaryTagToggle);
void
exprDiaryTagToggle(const char *setName, int tagBit)
{
	U64 tagMask;
	U64 *pTagBits;

	if ( tagBit >= DIARY_MAX_TAGS_PER_PLAYER )
	{
		return;
	}

	tagMask = ((U64)1)<<tagBit;

	pTagBits = GetScratchTagBits(setName);

	(*pTagBits) = (*pTagBits) ^ tagMask;
}

AUTO_EXPR_FUNC(UIGen) ACMD_NAME(DiaryWorkingTagsFromEntry);
void
exprDiaryWorkingTagsFromEntry(const char *setName, U32 entryID)
{
	DiaryDisplayHeader *displayHeader = DisplayHeaderFromEntryID(entryID);
	U64 *pTagBits;
	
	pTagBits = GetScratchTagBits(setName);

	if ( displayHeader != NULL )
	{
		(*pTagBits) = displayHeader->tagBits;
	}
	else
	{
		(*pTagBits) = 0;
	}
}

AUTO_EXPR_FUNC(UIGen) ACMD_NAME(DiaryWorkingTagsToEntry);
void
exprDiaryWorkingTagsToEntry(const char *setName, U32 entryID)
{
	DiaryDisplayHeader *displayHeader = DisplayHeaderFromEntryID(entryID);
	U64 *pTagBits;

	pTagBits = GetScratchTagBits(setName);

	if ( displayHeader != NULL )
	{
		if ( ENTRY_ID_IS_LOCAL(entryID) )
		{
			// if it is a client generated mission or perk entry, then turn it into a real entry with a comment
			if ( ( displayHeader->type == DiaryEntryType_Mission ) || ( displayHeader->type == DiaryEntryType_Perk ) )
			{
				ServerCmd_gslDiary_AddMissionEntry(displayHeader->missionName, NULL, NULL, (*pTagBits));
			}
			else if ( displayHeader->type == DiaryEntryType_Activity )
			{
				ServerCmd_gslDiary_AddActivityEntry(ACTIVITY_ID_FROM_LOCAL_ID(displayHeader->entryID), NULL, NULL, (*pTagBits));
			}
		}
		else
		{
			ServerCmd_gslDiary_SetEntryTags(entryID, (*pTagBits));
		}
	}
}

const char *
GetTagDisplayString(U64 tagBits)
{
	int currentBit = 0;
	static char *s_tagsStr = NULL;
	bool first = true;
	int tagsAdded = 0;

	estrClear(&s_tagsStr);

	while ( ( tagBits != 0 ) && ( tagsAdded < 10 ) )
	{
		if ( tagBits & 1 )
		{
			if ( !first )
			{
				estrAppend2(&s_tagsStr, ", ");
			}
			estrAppend2(&s_tagsStr, s_playerTags[currentBit]);
			first = false;
			tagsAdded++;
		}
		currentBit++;
		tagBits = tagBits >> 1;
	}

	// we added 10 tags already
	if ( tagBits != 0 )
	{
		estrAppend2(&s_tagsStr, "...");
	}

	return s_tagsStr;
}

//
// Return a string containing all the tags for the given entry
//
AUTO_EXPR_FUNC(UIGen) ACMD_NAME(DiaryGetTagDisplayString);
const char *
exprDiaryGetTagDisplayString(U32 entryID)
{
	DiaryDisplayHeader *displayHeader = DisplayHeaderFromEntryID(entryID);

	if ( displayHeader != NULL )
	{
		return GetTagDisplayString(displayHeader->tagBits);
	}

	return NULL;
}

//
// Return a string containing all the tags for the given tag set
//
AUTO_EXPR_FUNC(UIGen) ACMD_NAME(DiaryGetDisplayStringForTagSet);
const char *
exprDiaryGetDisplayStringForTagSet(const char *setName)
{
	U64 *pTagBits;

	pTagBits = GetScratchTagBits(setName);

	return GetTagDisplayString(*pTagBits);
}

//
// This command is used by the server to set the current diary headers.
//
AUTO_COMMAND ACMD_ACCESSLEVEL(0) ACMD_CLIENTCMD ACMD_PRIVATE ACMD_CATEGORY(Diary);
void 
gclDiary_SetHeaders(DiaryHeaders *headers)
{
	if (!s_diaryHeaders)
	{
		s_diaryHeaders = StructCreateNoConst(parse_DiaryHeaders);
	}
	StructCopyDeConst(parse_DiaryHeaders, headers, s_diaryHeaders, 0, 0, 0);

	// Record all missions, perks and activity log entries that have real diary entries.
	if ( s_DiaryEntryRefs == NULL )
	{
		s_DiaryEntryRefs = DiaryDisplay_CreateDiaryEntryReferences();
	}
	else
	{
		DiaryDisplay_ResetDiaryEntryReferences(s_DiaryEntryRefs);
	}

	DiaryDisplay_PopulateDiaryEntryReferences(s_DiaryEntryRefs, headers);
}

AUTO_EXPR_FUNC(UIGen) ACMD_NAME(DiaryDisplayHeaders);
void exprDiaryDisplayHeaders(SA_PARAM_OP_VALID Entity *pEnt, SA_PARAM_NN_VALID UIGen *pGen)
{
	int i;
	int numDisplayHeaders = 0;
	NOCONST(DiaryFilter) *filter = GetCurrentFilter();

	if ( s_DiaryEntryRefs == NULL )
	{
		s_DiaryEntryRefs = DiaryDisplay_CreateDiaryEntryReferences();
	}

	DiaryDisplay_GenerateDisplayHeaders(langGetCurrent(), pEnt, (DiaryHeaders *)s_diaryHeaders, (DiaryFilter *)GetCurrentFilter(), &s_diaryDisplayHeaders, true, s_DiaryEntryRefs);

	// create the stash table for looking up display headers by ID
	if ( s_displayHeaderMap == NULL )
	{
		s_displayHeaderMap = stashTableCreateInt(100);
	}
	else
	{
		stashTableClear(s_displayHeaderMap);
	}

	for ( i = 0; i < eaSize(&s_diaryDisplayHeaders); i++ )
	{
		DiaryDisplayHeader *displayHeader = s_diaryDisplayHeaders[i];
		stashIntAddPointer(s_displayHeaderMap, displayHeader->entryID, displayHeader, false);
	}

	ui_GenSetList(pGen, &s_diaryDisplayHeaders, parse_DiaryDisplayHeader);
}

AUTO_EXPR_FUNC(UIGen) ACMD_NAME(DiaryHeaders);
void exprDiaryHeaders(SA_PARAM_OP_VALID Entity *pEnt, SA_PARAM_NN_VALID UIGen *pGen)
{
	ui_GenSetList(pGen, &s_diaryHeaders->headers, parse_DiaryHeader);
}

AUTO_EXPR_FUNC(UIGen) ACMD_NAME(DiaryRequestHeaders);
void exprDiaryRequestHeaders(SA_PARAM_OP_VALID Entity *pEnt, SA_PARAM_NN_VALID UIGen *pGen)
{
	// clear out current headers and entry, since they may be bad
	if ( s_diaryHeaders != NULL )
	{
		StructResetNoConst(parse_DiaryHeaders, s_diaryHeaders);
	}
	if ( s_diaryEntry != NULL )
	{
		StructResetNoConst(parse_DiaryEntry, s_diaryEntry);
		s_lastRequestedID = 0;
	}

	// ask the server for new headers
	ServerCmd_gslDiary_RequestHeaders();
}

//
// Server calls this command to return the response from gslDiary_GetEntry().
//
AUTO_COMMAND ACMD_ACCESSLEVEL(0) ACMD_CLIENTCMD ACMD_PRIVATE ACMD_CATEGORY(Diary);
void gclDiary_SetCurrentEntry(DiaryEntry *entry)
{
	s_diaryEntryDirty = true;

	if (!s_diaryEntry)
	{
		s_diaryEntry = StructCreateNoConst(parse_DiaryEntry);
	}
	else
	{
		StructResetNoConst(parse_DiaryEntry, s_diaryEntry);
	}

	StructCopyDeConst(parse_DiaryEntry, entry, s_diaryEntry, 0, 0, 0);

}

static void
RequestEntryFromServer(U32 entryID)
{
	if ( ( ! ENTRY_ID_IS_LOCAL(entryID) ) && ( entryID != s_lastRequestedID ) )
	{
		ServerCmd_gslDiary_RequestEntry(entryID);

		// Remember the last entry we requested, so we don't spam the server with the same
		// request during the time it takes for the response to come back.
		s_lastRequestedID = entryID;
	}
}

//
// Fetch a diary entry from the server.
//
AUTO_EXPR_FUNC(UIGen) ACMD_NAME(DiaryRequestEntry);
void exprDiaryRequestEntry(DiaryDisplayHeader *displayHeader)
{
	RequestEntryFromServer(displayHeader->entryID);
}

AUTO_EXPR_FUNC(UIGen) ACMD_NAME(DiaryGetCurrentEntryComments);
void exprDiaryGetCurrentEntryComments(SA_PARAM_OP_VALID Entity *pEnt, SA_PARAM_NN_VALID UIGen *pGen, U32 entryID)
{
	static NOCONST(DiaryComment) *s_generatedComment = NULL;
	static DiaryDisplayComment **s_displayComments = NULL;
	int numDisplayComments = 0;

	// do one time initialization
	if ( s_generatedComment == NULL )
	{
		// allocate the static comment struct used for generated comments
		s_generatedComment = StructCreateNoConst(parse_DiaryComment);
	}

	if ( entryID != 0 )
	{
		numDisplayComments = DiaryDisplay_GenerateDisplayComments(langGetCurrent(), pEnt, (DiaryHeaders *)s_diaryHeaders, s_generatedComment, (DiaryEntry *)s_diaryEntry, entryID, &s_displayComments, true);

		if ( ! ENTRY_ID_IS_LOCAL(entryID) && ( ( s_diaryEntry == NULL ) || ( entryID != s_diaryEntry->entryID ) ) )
		{
			// since we have the wrong entry, request the right one
			RequestEntryFromServer(entryID);
		}
	}

	if ( numDisplayComments > 0 )
	{
		ui_GenSetList(pGen, &s_displayComments, parse_DiaryDisplayComment);
	}
	else
	{
		ui_GenSetList(pGen, NULL, parse_DiaryDisplayComment);
	}
}

AUTO_EXPR_FUNC(UIGen) ACMD_NAME(DiaryEntryTypeAsString);
const char * exprDiaryEntryTypeAsString(DiaryDisplayHeader *displayHeader)
{
	const char *retString = NULL;

	switch (displayHeader->type)
	{
	case DiaryEntryType_Blog:
		retString = TranslateMessageKey("Diary_EntryTypeBlog");
		break;
	case DiaryEntryType_Mission:
		retString = TranslateMessageKey("Diary_EntryTypeMission");
		break;
	case DiaryEntryType_Perk:
		retString = TranslateMessageKey("Diary_EntryTypePerk");
		break;
	case DiaryEntryType_Activity:
		retString = TranslateMessageKey("Diary_EntryTypeActivity");
		break;
	default:
		break;
	}

	return retString;
}

AUTO_EXPR_FUNC(UIGen) ACMD_NAME(DiaryNewEntry);
void exprDiaryNewEntry(const char *title, const char *body)
{
	// the server will escape the strings
	ServerCmd_gslDiary_AddBlogEntry(title, body);
	return;
}

AUTO_EXPR_FUNC(UIGen) ACMD_NAME(DiaryRemoveEntry);
void exprDiaryRemoveEntry(U32 entryID)
{
	DiaryDisplayHeader *displayHeader = DisplayHeaderFromEntryID(entryID);
	// can only delete blog entries currently
	if ( displayHeader->type == DiaryEntryType_Blog )
	{
		ServerCmd_gslDiary_RemoveEntry(displayHeader->entryID);
	}
}

AUTO_EXPR_FUNC(UIGen) ACMD_NAME(DiaryAddComment);
void exprDiaryAddComment(U32 entryID, const char *title, const char *body)
{
	DiaryDisplayHeader *displayHeader = DisplayHeaderFromEntryID(entryID);

	if ( displayHeader != NULL )
	{
		if ( ENTRY_ID_IS_LOCAL(displayHeader->entryID) )
		{
			// if it is a client generated mission or perk entry, then turn it into a real entry with a comment
			if ( ( displayHeader->type == DiaryEntryType_Mission ) || ( displayHeader->type == DiaryEntryType_Perk ) )
			{
				ServerCmd_gslDiary_AddMissionEntry(displayHeader->missionName, title, body, 0);
			}
			else if ( displayHeader->type == DiaryEntryType_Activity )
			{
				ServerCmd_gslDiary_AddActivityEntry(ACTIVITY_ID_FROM_LOCAL_ID(displayHeader->entryID), title, body, 0);
			}
		}
		else
		{
			// adding a comment to an existing entry
			ServerCmd_gslDiary_AddComment(displayHeader->entryID, title, body);
		}
	}
	return;
}

AUTO_EXPR_FUNC(UIGen) ACMD_NAME(DiaryEditComment);
void exprDiaryEditComment(U32 entryID, U32 commentID, const char *title, const char *body)
{
	DiaryComment *comment = eaIndexedGetUsingInt(&s_diaryEntry->comments, commentID);

	// comment or entry ID were bogus
	if ( ( comment == NULL ) || ( s_diaryEntry->entryID != entryID ) )
	{
		return;
	}

	ServerCmd_gslDiary_EditComment(entryID, commentID, title, body);

	return;
}

AUTO_EXPR_FUNC(UIGen) ACMD_NAME(GetDiaryCommentBody);
const char * 
exprGetDiaryCommentBody(DiaryComment *comment, bool htmlify)
{
	static char *s_formattedStr = NULL;

	estrClear(&s_formattedStr);

	if ( comment->text != NULL )
	{
		// unescape the string
		estrAppendUnescaped(&s_formattedStr, comment->text);

		if ( htmlify )
		{
			// replace newlines with HTML line breaks
			estrReplaceOccurrences(&s_formattedStr, "\n", "<br>");
		}
	}
	return s_formattedStr;
}

AUTO_EXPR_FUNC(UIGen) ACMD_NAME(GetDiaryCommentTitle);
const char * 
exprGetDiaryCommentTitle(DiaryComment *comment)
{
	static char *s_formattedStr = NULL;

	estrClear(&s_formattedStr);

	if ( comment->title != NULL )
	{
		// unescape the string
		estrAppendUnescaped(&s_formattedStr, comment->title);
	}

	return s_formattedStr;
}

AUTO_EXPR_FUNC(UIGen) ACMD_NAME(DiaryEntryCanBeDeleted);
bool 
exprDiaryEntryCanBeDeleted(U32 entryID)
{
	DiaryDisplayHeader *displayHeader = DisplayHeaderFromEntryID(entryID);

	// only blog entries can be deleted
	return ( displayHeader->type == DiaryEntryType_Blog );
}


AUTO_EXPR_FUNC(UIGen) ACMD_NAME(GetDiaryCommentBodyByID);
const char * 
exprGetDiaryCommentBodyByID(U32 commentID)
{
	DiaryComment *comment;

	if ( s_diaryEntry == NULL )
	{
		return NULL;
	}

	comment = eaIndexedGetUsingInt(&s_diaryEntry->comments, commentID);

	if ( comment == NULL )
	{
		return NULL;
	}

	return exprGetDiaryCommentBody(comment, false);
}

AUTO_EXPR_FUNC(UIGen) ACMD_NAME(GetDiaryCommentTitleByID);
const char * 
exprGetDiaryCommentTitleByID(U32 commentID)
{
	DiaryComment *comment;

	if ( s_diaryEntry == NULL )
	{
		return NULL;
	}

	comment = eaIndexedGetUsingInt(&s_diaryEntry->comments, commentID);

	if ( comment == NULL )
	{
		return NULL;
	}

	return exprGetDiaryCommentTitle(comment);
}

// look up the message key for a diary entry type using the enum value
AUTO_EXPR_FUNC(UIGen) ACMD_NAME(DiaryEntryTypeMessageFromValue);
const char *
exprDiaryEntryTypeMessageFromValue(int val)
{
	static char *s_entryTypeString = NULL;
	int i = 0;

	estrClear(&s_entryTypeString);

	while (DiaryEntryTypeEnum[i].key != U32_TO_PTR(DM_END)) 
	{
		if (DiaryEntryTypeEnum[i].key != U32_TO_PTR(DM_INT)) 
		{
			if ( DiaryEntryTypeEnum[i].value == val )
			{
				estrConcatf(&s_entryTypeString, "DiaryFilter_EntryType_%s", DiaryEntryTypeEnum[i].key);
			}
		}
		i++;
	}

	return s_entryTypeString;
}

// Get the list of diary entry types
AUTO_EXPR_FUNC(UIGen) ACMD_NAME(DiaryEntryTypeList);
void exprDiaryEntryTypeList(SA_PARAM_NN_VALID UIGen *pGen)
{
	static DiaryEntryTypeItem **s_TypeItemList = NULL;
	char messageKey[1024];

	if ( s_TypeItemList == NULL ) 
	{
		int i = 0;
		while (DiaryEntryTypeEnum[i].key != U32_TO_PTR(DM_END)) 
		{
			if (DiaryEntryTypeEnum[i].key != U32_TO_PTR(DM_INT)) 
			{
				DiaryEntryTypeItem *typeItem = StructCreate(parse_DiaryEntryTypeItem);
				sprintf(messageKey, "DiaryFilter_EntryType_%s", DiaryEntryTypeEnum[i].key);
				typeItem->message = allocAddString(messageKey);
				typeItem->value = DiaryEntryTypeEnum[i].value;
				eaPush(&s_TypeItemList, typeItem);
			}
			i++;
		}
	}
	ui_GenSetListSafe(pGen, &s_TypeItemList, DiaryEntryTypeItem);
}

// Get the entry type from the current filter
AUTO_EXPR_FUNC(UIGen) ACMD_NAME(DiaryGetCurrentFilterType);
int
exprDiaryGetCurrentFilterType()
{
	NOCONST(DiaryFilter) *filter = GetCurrentFilter();

	return (int)filter->type;
}

// Get the title string from the current filter
AUTO_EXPR_FUNC(UIGen) ACMD_NAME(DiaryGetCurrentFilterTitle);
const char *
exprDiaryGetCurrentFilterTitle()
{
	NOCONST(DiaryFilter) *filter = GetCurrentFilter();

	return filter->titleSubstring;
}

AUTO_EXPR_FUNC(UIGen) ACMD_NAME(DiaryWorkingTagsFromFilterInclude);
void
exprDiaryWorkingTagsFromFilterInclude(const char *setName)
{
	U64 *pTagBits;
	NOCONST(DiaryFilter) *filter = GetCurrentFilter();

	pTagBits = GetScratchTagBits(setName);

	(*pTagBits) = filter->tagIncludeMask;
}

AUTO_EXPR_FUNC(UIGen) ACMD_NAME(DiaryWorkingTagsFromFilterExclude);
void
exprDiaryWorkingTagsFromFilterExclude(const char *setName)
{
	U64 *pTagBits;
	NOCONST(DiaryFilter) *filter = GetCurrentFilter();

	pTagBits = GetScratchTagBits(setName);

	(*pTagBits) = filter->tagExcludeMask;
}
#include "gclDiary_h_ast.c"