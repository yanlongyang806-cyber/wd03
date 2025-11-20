/***************************************************************************
*     Copyright (c) 2009, Cryptic Studios
*     All Rights Reserved
*     Confidential Property of Cryptic Studios
***************************************************************************/

#pragma once
GCC_SYSTEM

#include "DiaryEnums.h"
#include "StashTable.h"
#include "AppLocale.h"

typedef struct DiaryComment DiaryComment;
typedef struct DiaryFilter DiaryFilter;
typedef struct DiaryHeaders DiaryHeaders;
typedef struct Entity Entity;
typedef struct NOCONST(DiaryComment) NOCONST(DiaryComment);
typedef struct DiaryEntry DiaryEntry;

#define LOCAL_ENTRY_ID_MASK 0x40000000

#define LOCAL_ENTRY_TYPE_MASK		0x30000000
#define LOCAL_ENTRY_TYPE_MISSION	0x00000000
#define LOCAL_ENTRY_TYPE_ACTIVITY	0x10000000
#define LOCAL_ENTRY_TYPE_UNUSED1	0x20000000
#define LOCAL_ENTRY_TYPE_UNUSED2	0x30000000
#define ENTRY_ID_IS_LOCAL(id) (((id) & LOCAL_ENTRY_ID_MASK) == LOCAL_ENTRY_ID_MASK)
#define LOCAL_ENTRY_TYPE(id) ((id) & LOCAL_ENTRY_TYPE_MASK)
#define LOCAL_ID_FROM_ACTIVITY_ID(id) ((id) | LOCAL_ENTRY_ID_MASK | LOCAL_ENTRY_TYPE_ACTIVITY)
#define ACTIVITY_ID_FROM_LOCAL_ID(id) ((id) & (~(LOCAL_ENTRY_ID_MASK | LOCAL_ENTRY_TYPE_ACTIVITY)))

AUTO_STRUCT;
typedef struct DiaryDisplayComment
{
	// copy ID from comment
	U32 id;								AST(KEY)

	// The base comment
	DiaryComment *comment;				AST(UNOWNED)

	// Any comment headers
	STRING_MODIFIABLE commentHeader;	AST(ESTRING)

	// Formatted display string for this comment
	STRING_MODIFIABLE formattedText;	AST(ESTRING)

	// first comment is flagged so that it can be displayed differently
	bool isFirst;
} DiaryDisplayComment;

AUTO_STRUCT;
typedef struct DiaryDisplayHeader
{
	U32 entryID;						AST(KEY)

	U32 displayTime;

	DiaryEntryType type;

	// Points to the localized human readable string to display, which could
	//  be a user entered title, a localized mission name, etc.
	char *title;						AST(ESTRING)

	// Mission name, used to look up associated mission
	const char *missionName;			AST(POOL_STRING)

	// tags for this entry
	U64 tagBits;
} DiaryDisplayHeader;

AUTO_STRUCT;
typedef struct DiaryDisplayHeaders
{
	EARRAY_OF(DiaryDisplayHeader) headers;
} DiaryDisplayHeaders;

AUTO_STRUCT;
typedef struct DiaryDisplayComments
{
	EARRAY_OF(DiaryDisplayComment) comments;
} DiaryDisplayComments;

// This structure is used to keep track of which external data items (like missions
//  and activity logs) have real diary entries.  It is used to avoid auto-generating
//  entries for items that already have a real entry.
// NOTE - this has nothing to do with the "reference system"
AUTO_STRUCT;
typedef struct DiaryEntryReferences
{
	// keeps track of which missions have been referenced in real diary entries
	StashTable missionsReferenced;

	// keeps track of which activity log entries have been referenced in real diary entries
	StashTable activityLogEntriesReferenced;
} DiaryEntryReferences;

int DiaryDisplay_GenerateDisplayHeaders(Language lang, SA_PARAM_OP_VALID Entity *pEnt, DiaryHeaders *diaryHeaders, DiaryFilter *filter, DiaryDisplayHeader ***diaryDisplayHeadersArray, bool freeExtraDisplayHeaders, DiaryEntryReferences *refs);
DiaryEntryReferences *DiaryDisplay_CreateDiaryEntryReferences(void);
void DiaryDisplay_ResetDiaryEntryReferences(DiaryEntryReferences *refs);
void DiaryDisplay_FreeDiaryEntryReferences(DiaryEntryReferences *refs);
void DiaryDisplay_PopulateDiaryEntryReferences(DiaryEntryReferences *refs, DiaryHeaders *headers);
int DiaryDisplay_GenerateDisplayComments(Language lang, SA_PARAM_OP_VALID Entity *pEnt, DiaryHeaders *diaryHeaders, NOCONST(DiaryComment) *generatedComment, DiaryEntry *diaryEntry, U32 entryID, DiaryDisplayComment ***diaryDisplayCommentsArray, bool freeExtraDisplayComments);
void DiaryDisplay_FormatStardate(char **destString, U32 seconds);
DiaryEntryType DiaryDisplay_GetEntryTypeFromLocalID(U32 entryID);
const char *DiaryDisplay_GetMissionNameFromLocalID(U32 entryID);