/***************************************************************************
*     Copyright (c) 2009, Cryptic Studios
*     All Rights Reserved
*     Confidential Property of Cryptic Studios
***************************************************************************/

#pragma once

#include "DiaryEnums.h"
#include "referencesystem.h"
#include "mission_enums.h"
#include "Message.h"

#define DIARY_MAX_TAGS_PER_PLAYER 64

// If you change this, you need to change DiaryTagManager_TagEntry in DiaryTagManager.uigen as well
#define DIARY_MAX_TAG_LENGTH 15

AUTO_STRUCT AST_CONTAINER;
typedef struct DiaryComment
{
	// the unique id of this comment
	const U32 id;								AST(PERSIST SUBSCRIBE KEY)

	// the container ID of the character posting the comment
	const U32 entityID;							AST(PERSIST SUBSCRIBE FORMATSTRING(FIXUP_CONTAINER_TYPE = "EntityPlayer"))

	// time posted
	const U32 time;								AST(PERSIST SUBSCRIBE)

	// title for this comment 
	CONST_STRING_MODIFIABLE title;				AST(PERSIST SUBSCRIBE)

	// body text for this comment
	CONST_STRING_MODIFIABLE text;				AST(PERSIST SUBSCRIBE)
} DiaryComment;

AUTO_STRUCT AST_CONTAINER;
typedef struct DiaryEntry
{
	// The ID of this entry. Will be the same as the entryID in the DiaryHeader
	//  for this entry.
	const U32 entryID;							AST(PERSIST SUBSCRIBE KEY)

	// The comments for this diary entry.
	CONST_EARRAY_OF(DiaryComment) comments;		AST(PERSIST SUBSCRIBE)

} DiaryEntry;

AUTO_STRUCT AST_CONTAINER AST_IGNORE(dummy);
typedef struct DiaryEntryBucket
{
	// container ID
	const U32 bucketID;							AST(PERSIST SUBSCRIBE KEY)

	// the entries stored in this bucket
	CONST_EARRAY_OF(DiaryEntry) entries;		AST(PERSIST SUBSCRIBE)

} DiaryEntryBucket;

//
// The DiaryHeader contains the information needed to display the entry
//  in the list of entries, and for the most common filters.
//
AUTO_STRUCT AST_CONTAINER;
typedef struct DiaryHeader
{
	// The unique id of this entry
	const U32 entryID;							AST(PERSIST SUBSCRIBE KEY)

	// The type of the entry
	const DiaryEntryType type;					AST(PERSIST SUBSCRIBE)

	// The time the entry was created
	const U32 time;								AST(PERSIST SUBSCRIBE)

	// Name of a referenced mission, perk or message id.
	CONST_STRING_POOLED refName;				AST(PERSIST SUBSCRIBE POOL_STRING)

	// Title of the entry, only used for user generated(blog) entries.
	CONST_STRING_MODIFIABLE title;				AST(PERSIST SUBSCRIBE)

	// The ID of an activity entry.  Only used for activity type diary entries.
	const U32 activityEntryID;					AST(PERSIST SUBSCRIBE)

	// Is this entry hidden by the player
	const bool hidden;							AST(PERSIST SUBSCRIBE BOOLFLAG)

	// Reference to the bucket containing the rest of this entry 
	const U32 bucketID;							AST(PERSIST SUBSCRIBE FORMATSTRING(DEPENDENT_CONTAINER_TYPE = "DiaryEntryBucket"))

	// Bit flags representing user specified tags for this entry
	const U64 tagBits;							AST(PERSIST SUBSCRIBE)
} DiaryHeader;

AUTO_STRUCT AST_CONTAINER;
typedef struct DiaryHeaders
{
	CONST_EARRAY_OF(DiaryHeader) headers;		AST(PERSIST SUBSCRIBE)
} DiaryHeaders;

AUTO_STRUCT AST_CONTAINER;
typedef struct DiaryTagString
{
	CONST_STRING_POOLED tagName;				AST(PERSIST SUBSCRIBE POOL_STRING)

	// is true if the player can not delete the tag
	const bool permanent;						AST(PERSIST SUBSCRIBE BOOLFLAG)
} DiaryTagString;

AUTO_STRUCT AST_CONTAINER;
typedef struct DiaryFilter
{
	// Matching entries must have a time at or after this value.  Set to zero to match all.
	const U32 startTime;						AST(PERSIST SUBSCRIBE)

		// Matching entries must have a time at or before this value.  Set to zero to match all.
		const U32 endTime;							AST(PERSIST SUBSCRIBE)

		// Entry type to match.  Use DiaryEntryType_None to match all.
		const DiaryEntryType type;					AST(PERSIST SUBSCRIBE)

		// Match only entries whose titles contain this string.  No wildcard or fuzzy matching for now.
		CONST_STRING_MODIFIABLE titleSubstring;		AST(PERSIST SUBSCRIBE)

		// Match entries that contain any of these tags.
		const U64 tagIncludeMask;						AST(PERSIST SUBSCRIBE)

		// Match entries that contain none of these tags.
		const U64 tagExcludeMask;						AST(PERSIST SUBSCRIBE)
} DiaryFilter;

AUTO_STRUCT AST_CONTAINER AST_IGNORE(tagNames);
typedef struct PlayerDiary
{
	// the id of this container
	const U32 containerID;						AST(PERSIST SUBSCRIBE KEY)

	// the character that this diary belongs to
	const U32 entityID;							AST(PERSIST SUBSCRIBE FORMATSTRING(FIXUP_CONTAINER_TYPE = "EntityPlayer"))

	// the ID of the next comment to be created
	// comment IDs are unique per diary
	const U32 nextCommentID;					AST(PERSIST SUBSCRIBE)

	// the ID of the next entry to be created
	// entry IDs are unique per diary
	const U32 nextEntryID;						AST(PERSIST SUBSCRIBE)

	// The container ID of the current entry bucket
	const U32 currentBucketID;					AST(PERSIST SUBSCRIBE FORMATSTRING(DEPENDENT_CONTAINER_TYPE = "DiaryEntryBucket"))

	// The number of entries that have been allocated from the current bucket
	const U32 currentBucketCount;				AST(PERSIST SUBSCRIBE)

	// Diary entry headers for this player
	DiaryHeaders headers;						AST(PERSIST SUBSCRIBE)

	// The array of tags that this user can use to tag their entries.
	// The offset in this array is also the bit offset in the tagBits field
	//  of the DiaryHeader.
	CONST_EARRAY_OF(DiaryTagString) tags;		AST(PERSIST SUBSCRIBE)

	// The default filter to use for viewing diaries 
	DiaryFilter defaultFilter;					AST(PERSIST SUBSCRIBE)

	// keep track of which bucket containers for this diary are subscribed to by this game server
	INT_EARRAY subscribedBucketIDs;
} PlayerDiary;

AUTO_STRUCT;
typedef struct DiaryTagStrings
{
	EARRAY_OF(DiaryTagString) tagStrings;
} DiaryTagStrings;

AUTO_STRUCT;
typedef struct DiaryMissionTypeConfig
{
	MissionType missionType;								AST(KEY NAME("MissionType"))
	bool autoAddCurrent;									AST(BOOLFLAG NAME("AutoAddCurrent"))
	bool autoAddComplete;									AST(BOOLFLAG NAME("AutoAddComplete"))
} DiaryMissionTypeConfig;

AUTO_STRUCT;
typedef struct DiaryConfig
{
	// Default set of tags to add to player's tag list when it is first created
	// Do these need to be message keys so we can translate them?
	EARRAY_OF(DiaryTagString) defaultTags;					AST(NAME("DefaultTag"))

	// configure how various mission types are treated
	EARRAY_OF(DiaryMissionTypeConfig) missionTypeConfigs;	AST(NAME("MissionTypeConfig"))

	// the string to use for formatting the SMF display of a diary comment
	DisplayMessage commentFormatMessage;					AST(STRUCT(parse_DisplayMessage))

	// the string to use for formatting the SMF display of a diary mission comment header
	DisplayMessage missionHeaderFormatMessage;				AST(STRUCT(parse_DisplayMessage))

	// the string to use for formatting the SMF display of a diary mission comment header
	DisplayMessage completedMissionHeaderFormatMessage;		AST(STRUCT(parse_DisplayMessage))

	// the string to use for formatting the SMF display of a diary mission comment header
	DisplayMessage repeatedMissionHeaderFormatMessage;		AST(STRUCT(parse_DisplayMessage))
} DiaryConfig;

bool DiaryConfig_AutoAddCurrentMissionType(MissionType type);
bool DiaryConfig_AutoAddCompletedMissionType(MissionType type);
DiaryTagStrings *DiaryConfig_CopyDefaultTagStrings();
DisplayMessage *DiaryConfig_CommentFormatMessage();
DisplayMessage *DiaryConfig_MissionHeaderFormatMessage();
DisplayMessage *DiaryConfig_CompletedMissionHeaderFormatMessage();
DisplayMessage *DiaryConfig_RepeatedMissionHeaderFormatMessage();