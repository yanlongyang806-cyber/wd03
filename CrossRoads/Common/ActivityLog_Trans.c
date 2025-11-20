/***************************************************************************
*     Copyright (c) 2009, Cryptic Studios
*     All Rights Reserved
*     Confidential Property of Cryptic Studios
***************************************************************************/

#include "ActivityLogCommon.h"

#include "timing.h"
#include "objTransactions.h"
#include "Entity.h"
#include "EntitySavedData.h"
#include "AutoTransDefs.h"
#include "Guild.h"
#include "Player.h"

#include "AutoGen/ActivityLogCommon_h_ast.h"
#include "AutoGen/Entity_h_ast.h"
#include "AutoGen/EntitySavedData_h_ast.h"
#include "AutoGen/Guild_h_ast.h"
#include "AutoGen/Player_h_ast.h"

AUTO_TRANSACTION
ATR_LOCKS(pEnt, ".Psaved.Nextactivitylogid, .Psaved.Activitylogentries[AO]");
enumTransactionOutcome
ActivityLog_tr_AddEntityLogEntry(ATR_ARGS, NOCONST(Entity) *pEnt, int entryType, const char *argString, U32 time, float playedTime)
{
	NOCONST(ActivityLogEntry) *entry;

	if ( ISNULL(pEnt) || ISNULL(pEnt->pSaved) )
	{
		return TRANSACTION_OUTCOME_FAILURE;
	}

	// don't ever use zero for entry ID
	if ( pEnt->pSaved->nextActivityLogID == 0 )
	{
		pEnt->pSaved->nextActivityLogID = 1;
	}

	// create the entry
	entry = StructCreateNoConst(parse_ActivityLogEntry);
	entry->entryID = pEnt->pSaved->nextActivityLogID;
	pEnt->pSaved->nextActivityLogID++;
	entry->type = (ActivityLogEntryType)entryType;
	entry->argString = StructAllocString(argString);
	entry->time = time;
	entry->playedTimeAtEvent = playedTime;

	// add the entry to the player
	eaIndexedAdd(&pEnt->pSaved->activityLogEntries, entry);

	return TRANSACTION_OUTCOME_SUCCESS;
}

AUTO_TRANSACTION
ATR_LOCKS(pEnt, ".Psaved.Nextactivitylogid, .Psaved.Activitylogentries[AO], .Pplayer.Langid")
ATR_LOCKS(pGuild, ".Earanks");
enumTransactionOutcome
ActivityLog_tr_AddEntityGuildLogEntry(ATR_ARGS, NOCONST(Entity) *pEnt, NOCONST(Guild) *pGuild, int entryType, const char *argString, U32 time, float playedTime)
{
	NOCONST(ActivityLogEntry) *entry;

	if ( ISNULL(pEnt) || ISNULL(pEnt->pSaved) )
	{
		return TRANSACTION_OUTCOME_FAILURE;
	}

	if (entryType == ActivityLogEntryType_GuildRankChange)
	{
		// resolve the guild rank name
		if (NONNULL(pGuild) && NONNULL(argString) && argString[0] == '\33')
		{
			S32 rankNum = atoi(argString+1);
			NOCONST(GuildCustomRank) *customRank = eaGet(&pGuild->eaRanks, rankNum);
			if (NONNULL(customRank))
			{
				if (NONNULL(customRank->pcDisplayName))
					argString = customRank->pcDisplayName;
				else
					argString = langTranslateMessageKey(NONNULL(pEnt) && NONNULL(pEnt->pPlayer) ? pEnt->pPlayer->langID : locGetLanguage(getCurrentLocale()), customRank->pcDefaultNameMsg);
			}
		}
	}

	// don't ever use zero for entry ID
	if ( pEnt->pSaved->nextActivityLogID == 0 )
	{
		pEnt->pSaved->nextActivityLogID = 1;
	}

	// create the entry
	entry = StructCreateNoConst(parse_ActivityLogEntry);
	entry->entryID = pEnt->pSaved->nextActivityLogID;
	pEnt->pSaved->nextActivityLogID++;
	entry->type = (ActivityLogEntryType)entryType;
	entry->argString = StructAllocString(argString);
	entry->time = time;
	entry->playedTimeAtEvent = playedTime;

	// add the entry to the player
	eaIndexedAdd(&pEnt->pSaved->activityLogEntries, entry);

	return TRANSACTION_OUTCOME_SUCCESS;
}

AUTO_TRANSACTION
ATR_LOCKS(pGuildContainer, ".Inextactivitylogentryid, .Eaactivityentries, eaMembers[]");
enumTransactionOutcome
ActivityLog_tr_AddGuildLogEntry(ATR_ARGS, NOCONST(Guild) *pGuildContainer, int entryType, const char *argString, U32 time, U32 subjectID)
{
	NOCONST(ActivityLogEntry) *entry;
	int maxGuildLogSize;
	int n;

	if ( ISNULL(pGuildContainer) )
	{
		return TRANSACTION_OUTCOME_FAILURE;
	}

	// If a subjectID is specified, it must be a guild member.
	if ( subjectID != 0 )
	{
		if ( eaIndexedGetUsingInt(&pGuildContainer->eaMembers, subjectID) == NULL )
		{
			return TRANSACTION_OUTCOME_FAILURE;
		}
	}

	// don't ever use zero for entry ID
	if ( pGuildContainer->iNextActivityLogEntryID == 0 )
	{
		pGuildContainer->iNextActivityLogEntryID = 1;
	}

	// create the entry
	entry = StructCreateNoConst(parse_ActivityLogEntry);
	entry->entryID = pGuildContainer->iNextActivityLogEntryID;
	pGuildContainer->iNextActivityLogEntryID++;
	entry->type = (ActivityLogEntryType)entryType;
	entry->argString = StructAllocString(argString);
	entry->time = time;
	entry->subjectID = subjectID;

	// make sure the log does not exceed the maximum size
	maxGuildLogSize = ActivityLog_GetMaxGuildLogSize();
	n = eaSize(&pGuildContainer->eaActivityEntries);
	if ( n >= maxGuildLogSize )
	{
		// remove any excess log entries from the beginning of the list (oldest entries)
		eaRemoveRange(&pGuildContainer->eaActivityEntries, 0, n - maxGuildLogSize + 1);
	}

	// add the entry to the guild
	eaPush(&pGuildContainer->eaActivityEntries, entry);

	return TRANSACTION_OUTCOME_SUCCESS;
}
