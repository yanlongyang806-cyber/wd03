#pragma once

#include "AccountServer.h"
#include "SubscriptionHistory_h_ast.h"

AUTO_STRUCT;
typedef struct SubHistoryStat
{
	char *pInternalProductName;
	U32 uTotalSeconds;
} SubHistoryStat;

AUTO_STRUCT;
typedef struct SubHistoryStats
{
	EARRAY_OF(SubHistoryStat) eaStats;
} SubHistoryStats;

// Archive a subscription
void accountArchiveSubscription(U32 uAccountID,
								SA_PARAM_NN_STR const char *pProductInternalName,
								SA_PARAM_OP_STR const char *pSubInternalName,
								SA_PARAM_OP_STR const char *pSubVID,
								U32 uStartTime,
								U32 uEndTime,
								SubscriptionTimeSource eSubTimeSource,
								SubscriptionHistoryEntryReason eReason,
								U32 uProblemFlags);

// Recalculate subscription history
void accountRecalculateArchivedSubHistory(U32 uAccountID,
										  SA_PARAM_NN_STR const char *pProductInternalName);

// Recalculate all subscription history
void accountRecalculateAllArchivedSubHistory(U32 uAccountID);

// Enable/disable an entry
void accountEnableArchivedSubHistory(U32 uAccountID,
									 SA_PARAM_NN_STR const char *pProductInternalName,
									 U32 uID,
									 bool bEnable);

// Calculate the total seconds of a subscription history entry
U32 subHistoryEntrySeconds(SA_PARAM_NN_VALID const SubscriptionHistoryEntry *pEntry);

// Determine the total number of seconds
U32 productTotalSeconds(SA_PARAM_NN_VALID const AccountInfo *pAccount,
						SA_PARAM_NN_STR const char *pProductInternalName);

// Get all sub stats for an account
SA_RET_OP_VALID SubHistoryStats *getAllSubStats(SA_PARAM_NN_VALID const AccountInfo *pAccount);