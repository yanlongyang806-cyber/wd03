/***************************************************************************
*     Copyright (c) 2009, Cryptic Studios
*     All Rights Reserved
*     Confidential Property of Cryptic Studios
***************************************************************************/

#include "DiaryServer.h"
#include "DiaryCommon.h"

#include "timing.h"
#include "objTransactions.h"

#include "AutoGen/DiaryCommon_h_ast.h"

AUTO_TRANSACTION;
enumTransactionOutcome
DiaryServer_tr_AddBlogEntry(ATR_ARGS, NOCONST(PlayerDiary) *diary, const char *title, const char *text)
{
	NOCONST(DiaryEntry) *entry;
	NOCONST(DiaryComment) *comment;

	// make sure title is less than the limit
	if ( strlen(title) > DIARY_MAX_TITLE_LEN )
	{
		return TRANSACTION_OUTCOME_FAILURE;
	}

	// make sure body is less than the limit
	if ( strlen(text) > DIARY_MAX_BODY_LEN )
	{
		return TRANSACTION_OUTCOME_FAILURE;
	}

	entry = StructCreate(parse_DiaryEntry);
	// entry id is just index into entries array
	entry->id = eaSize(&diary->entries);
	entry->type = DiaryEntryType_Blog;
	entry->time = timeSecondsSince2000();

	// entry title and text go into an initial comment
	comment = StructCreate(parse_DiaryComment);
	comment->entityID = diary->entityID;
	comment->id = diary->nextCommentID;
	diary->nextCommentID++;
	comment->time = entry->time;
	comment->title = strdup(title);
	comment->text = strdup(text);

	// add initial comment to entry
	eaPush(&entry->comments, comment);

	// add entry to diary
	eaPush(&diary->entries, entry);

	printf("AddBlogEntry transaction end\n");

	return TRANSACTION_OUTCOME_SUCCESS;
}

AUTO_TRANSACTION;
enumTransactionOutcome
DiaryServer_tr_AddComment(ATR_ARGS, NOCONST(PlayerDiary) *diary, U32 entryID, const char *title, const char *text)
{
	NOCONST(DiaryEntry) *entry;
	NOCONST(DiaryComment) *comment;

	// make sure title is less than the limit
	if ( strlen(title) > DIARY_MAX_TITLE_LEN )
	{
		return TRANSACTION_OUTCOME_FAILURE;
	}

	// make sure body is less than the limit
	if ( strlen(text) > DIARY_MAX_BODY_LEN )
	{
		return TRANSACTION_OUTCOME_FAILURE;
	}

	// make sure entry ID is valid
	if ( entryID >= (unsigned)eaSize(&diary->entries) )
	{
		return TRANSACTION_OUTCOME_FAILURE;
	}

	// look up the entry
	entry = eaIndexedGetUsingInt(&diary->entries, entryID);

	devassertmsg(entry != NULL, "Diary entry indexed lookup failed");

	// create and populate comment struct
	comment = StructCreate(parse_DiaryComment);
	comment->entityID = diary->entityID;
	comment->id = diary->nextCommentID;
	diary->nextCommentID++;
	comment->time = timeSecondsSince2000();
	comment->title = strdup(title);
	comment->text = strdup(text);

	// add to entry
	eaPush(&entry->comments, comment);

	return TRANSACTION_OUTCOME_SUCCESS;
}

AUTO_TRANSACTION;
enumTransactionOutcome
DiaryServer_tr_AddMissionEntry(ATR_ARGS, NOCONST(PlayerDiary) *diary, const char *missionRef)
{
	NOCONST(DiaryEntry) *entry;

	entry = StructCreate(parse_DiaryEntry);
	// entry id is just index into entries array
	entry->id = eaSize(&diary->entries);
	entry->type = DiaryEntryType_MissionStarted;
	entry->time = timeSecondsSince2000();
	entry->refName = missionRef;

	// add entry to diary
	eaPush(&diary->entries, entry);

	printf("AddMissionEntry transaction end\n");

	return TRANSACTION_OUTCOME_SUCCESS;
}