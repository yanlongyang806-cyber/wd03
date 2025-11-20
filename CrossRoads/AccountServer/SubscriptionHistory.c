#include "SubscriptionHistory.h"
#include "timing.h"
#include "objTransactions.h"
#include "AutoGen/AccountServer_autotransactions_autogen_wrappers.h"
#include "AutoTransDefs.h"
#include "Product.h"
#include "InternalSubs.h"
#include "AccountManagement.h"
#include "StringUtil.h"

/************************************************************************/
/* Private functions                                                    */
/************************************************************************/

typedef enum OverlapResult
{
	ORESULT_NoChange,
	ORESULT_Adjusted,
	ORESULT_IgnoreOriginal,
} OverlapResult;

static OverlapResult adjustOverlap(U32 originalStart, U32 originalEnd, SA_PARAM_NN_VALID U32 *newStart, SA_PARAM_NN_VALID U32 *newEnd)
{
	OverlapResult eResult = ORESULT_NoChange;

	if (!verify(originalStart <= originalEnd)) return false;
	if (!verify(newStart && newEnd)) return false;
	if (!verify(*newStart <= *newEnd)) return false;

	if (IN_CLOSED_INTERVAL_SAFE(originalStart, *newStart, originalEnd)) // a <= x <= b
	{
		*newStart = MIN(*newEnd, originalEnd);
		devassert(*newStart <= *newEnd);
		eResult = ORESULT_Adjusted;
	}
	else if (IN_CLOSED_INTERVAL_SAFE(originalStart, *newEnd, originalEnd)) // a <= x <= b
	{
		*newEnd = MAX(*newStart, originalStart);
		devassert(*newStart <= *newEnd);
		eResult = ORESULT_Adjusted;
	}
	else if (*newStart <= originalStart && *newEnd >= originalEnd)
	{
		eResult = ORESULT_IgnoreOriginal;
	}

	return eResult;
}

AUTO_COMMAND;
void testOverlap(U32 a, U32 b, U32 c, U32 d)
{
	OverlapResult eResult = ORESULT_NoChange;

	printf("%u-%u & %u-%u = ", a, b, c, d);
	eResult = adjustOverlap(a, b, &c, &d);

	if (eResult == ORESULT_IgnoreOriginal)
	{
		a = b;
	}

	printf("%u-%u & %u-%u (%u + %u = %u)\n", a, b, c, d, b - a, d - c, (b - a) + (d - c));
}

// Calculate the total archived seconds of a subscription history
static U32 subHistoryTotalSeconds(SA_PARAM_NN_VALID const SubscriptionHistory *pHistory, SA_PARAM_NN_VALID U32 *pStart, SA_PARAM_NN_VALID U32 *pEnd)
{
	U32 uTotal = 0;

	if (!verify(pHistory)) return 0;

	PERFINFO_AUTO_START_FUNC();

	EARRAY_CONST_FOREACH_BEGIN(pHistory->eaArchivedEntries, i, iNumArchived);
	{
		const SubscriptionHistoryEntry *pEntry = pHistory->eaArchivedEntries[i];
		OverlapResult eResult = ORESULT_NoChange;

		if (!devassert(pEntry)) continue;

		eResult = adjustOverlap(pEntry->uAdjustedStartSS2000, pEntry->uAdjustedEndSS2000, pStart, pEnd);

		if (eResult != ORESULT_IgnoreOriginal)
		{
			uTotal += subHistoryEntrySeconds(pEntry);
		}
	}
	EARRAY_FOREACH_END;

	PERFINFO_AUTO_STOP_FUNC();

	return uTotal;
}


/************************************************************************/
/* Transactions                                                         */
/************************************************************************/

// Recalculate adjusted times
AUTO_TRANS_HELPER;
static void trRecalculateAdjustedTimes(ATH_ARG NOCONST(SubscriptionHistory) *pHistory)
{
	PERFINFO_AUTO_START_FUNC();

	EARRAY_CONST_FOREACH_BEGIN(pHistory->eaArchivedEntries, iCurEntry, iNumArchived);
	{
		NON_CONTAINER NOCONST(SubscriptionHistoryEntry) *pEntry = pHistory->eaArchivedEntries[iCurEntry];

		if (!devassert(pEntry)) continue;

		if (!devassert(pEntry->uStartTimeSS2000 && pEntry->uEndTimeSS2000)) continue;
		if (!devassert(pEntry->uStartTimeSS2000 <= pEntry->uEndTimeSS2000)) continue;

		pEntry->uAdjustedStartSS2000 = pEntry->uStartTimeSS2000;
		pEntry->uAdjustedEndSS2000 = pEntry->uEndTimeSS2000;

		// Determine if it overlaps with any other entries
		pEntry->uProblemFlags &= ~SHEP_OVERLAPS;
		EARRAY_CONST_FOREACH_BEGIN(pHistory->eaArchivedEntries, iOldEntry, iNumArchived2);
		{
			NOCONST(SubscriptionHistoryEntry) *pExistingEntry = pHistory->eaArchivedEntries[iOldEntry];
			OverlapResult eResult = ORESULT_NoChange;

			if (!devassert(pExistingEntry)) continue;

			// Only consider ones that came before
			if (iOldEntry >= iCurEntry) break;

			// Ignore disabled ones
			if (!pExistingEntry->bEnabled) continue;

			eResult = adjustOverlap(pExistingEntry->uAdjustedStartSS2000, pExistingEntry->uAdjustedEndSS2000,
				&pEntry->uAdjustedStartSS2000, &pEntry->uAdjustedEndSS2000);

			// Adjust for overlap
			if (eResult == ORESULT_Adjusted)
			{
				pEntry->uProblemFlags |= SHEP_OVERLAPS;

				if (pEntry->uAdjustedStartSS2000 == pEntry->uAdjustedEndSS2000)
					break; // Early out if we are down to 0 time
			}
			else if (eResult == ORESULT_IgnoreOriginal)
			{
				pExistingEntry->uAdjustedEndSS2000 = pExistingEntry->uAdjustedStartSS2000;
				pExistingEntry->uProblemFlags |= SHEP_OVERLAPS;
			}
		}
		EARRAY_FOREACH_END;

		pEntry->uLastCalculatedSS2000 = timeSecondsSince2000();
		devassert(pEntry->uLastCalculatedSS2000);
	}
	EARRAY_FOREACH_END;

	PERFINFO_AUTO_STOP_FUNC();
}

// Make sure a subscription archive exists for an internal product name
AUTO_TRANS_HELPER;
int trAccountEnsureArchiveExists(ATH_ARG NOCONST(AccountInfo) *pAccount,
								 SA_PARAM_NN_STR const char *pProductInternalName)
{
	int index;
	NOCONST(SubscriptionHistory) *pHistory;

	if (!verify(NONNULL(pAccount))) return -1;
	if (!verify(pProductInternalName && *pProductInternalName)) return -1;

	if (!pAccount->ppSubscriptionHistory)
	{
		// Enable indexing for the subscription history
		eaIndexedEnableNoConst(&pAccount->ppSubscriptionHistory, parse_SubscriptionHistory);
		if (!devassert(pAccount->ppSubscriptionHistory)) return -1;
	}

	index = eaIndexedFindUsingString(&pAccount->ppSubscriptionHistory, pProductInternalName);

	// If it already exists, return the index
	if (index >= 0) return index;

	// Create the history entry and add it to the account
	pHistory = StructCreateNoConst(parse_SubscriptionHistory);

	if (!devassert(pHistory)) return -1;

	pHistory->pProductInternalName = strdup(pProductInternalName);
	devassert(pHistory->pProductInternalName && *pHistory->pProductInternalName);

	eaIndexedEnableNoConst(&pHistory->eaArchivedEntries, parse_SubscriptionHistoryEntry);
	devassert(pHistory->eaArchivedEntries);

	eaIndexedAdd(&pAccount->ppSubscriptionHistory, pHistory);

	return 0;
}

// Archive a subscription
AUTO_TRANSACTION
ATR_LOCKS(pAccount, ".Ppsubscriptionhistory");
enumTransactionOutcome trAccountArchiveSubscription(ATR_ARGS, NOCONST(AccountInfo) *pAccount,
													SA_PARAM_NN_STR const char *pProductInternalName,
													SA_PARAM_OP_STR const char *pSubInternalName,
													SA_PARAM_NN_STR const char *pSubVID,
													U32 uStartTime,
													U32 uEndTime,
													int eSubTimeSourceArg,
													int eReasonArg,
													U32 uProblemFlags)
{
	SubscriptionTimeSource eSubTimeSource = eSubTimeSourceArg;
	SubscriptionHistoryEntryReason eReason = eReasonArg;
	int historyIndex;
	NOCONST(SubscriptionHistory) *pHistory;
	NOCONST(SubscriptionHistoryEntry) *pEntry;

	if (!verify(NONNULL(pAccount))) return TRANSACTION_OUTCOME_FAILURE;
	if (!verify(pProductInternalName && *pProductInternalName)) return TRANSACTION_OUTCOME_FAILURE;
	if (!verify(uStartTime <= uEndTime)) return TRANSACTION_OUTCOME_FAILURE;
	if (!verify(uStartTime && uEndTime)) return TRANSACTION_OUTCOME_FAILURE;
	if (!verify(eSubTimeSource != STS_Invalid)) return TRANSACTION_OUTCOME_FAILURE;
	if (!verify(eReason != SHER_Invalid)) return TRANSACTION_OUTCOME_FAILURE;

	if (pSubVID && !*pSubVID) pSubVID = NULL;
	if (pSubInternalName && !*pSubInternalName) pSubInternalName = NULL;

	historyIndex = trAccountEnsureArchiveExists(pAccount, pProductInternalName);

	if (!devassert(historyIndex >= 0)) return TRANSACTION_OUTCOME_FAILURE;

	pHistory = pAccount->ppSubscriptionHistory[historyIndex];

	if (!devassert(pHistory)) return TRANSACTION_OUTCOME_FAILURE;

	// Now that we know where to put it and that our input is clean, construct and put the entry on the list

	pEntry = StructCreateNoConst(parse_SubscriptionHistoryEntry);

	if (!devassert(pEntry)) return TRANSACTION_OUTCOME_FAILURE;

	// Populate the initial fields
	pEntry->uID = pHistory->uNextEntryID;
	pEntry->eSubTimeSource = eSubTimeSource;
	pEntry->eReason = eReason;
	pEntry->pSubInternalName = pSubInternalName ? strdup(pSubInternalName) : NULL;
	pEntry->pSubscriptionVID = pSubVID ? strdup(pSubVID) : NULL;
	pEntry->uStartTimeSS2000 = uStartTime;
	pEntry->uEndTimeSS2000 = uEndTime;
	pEntry->uProblemFlags = uProblemFlags;
	pEntry->uCreatedSS2000 = timeSecondsSince2000();

	eaIndexedAdd(&pHistory->eaArchivedEntries, pEntry);
	devassert(pHistory->eaArchivedEntries);

	pHistory->uNextEntryID++;
	devassert(pHistory->uNextEntryID);

	// Calculate and populate the remaining fields
	trRecalculateAdjustedTimes(pHistory);

	return TRANSACTION_OUTCOME_SUCCESS;
}

// Recalculate subscription history
AUTO_TRANS_HELPER;
bool trAccountRecalculateArchivedSubHistory(ATH_ARG NOCONST(AccountInfo) *pAccount,
											SA_PARAM_NN_STR const char *pProductInternalName)
{
	int index;
	NOCONST(SubscriptionHistory) *pHistory;

	if (!verify(NONNULL(pAccount))) return false;
	if (!verify(pProductInternalName && *pProductInternalName)) return false;

	index = eaIndexedFindUsingString(&pAccount->ppSubscriptionHistory, pProductInternalName);

	// We're done if it doesn't exist
	if (index < 0) return true;

	pHistory = pAccount->ppSubscriptionHistory[index];

	if (!devassert(pHistory)) return false;

	trRecalculateAdjustedTimes(pHistory);

	return true;
}

// Recalculate subscription history
AUTO_TRANSACTION
ATR_LOCKS(pAccount, ".Ppsubscriptionhistory");
enumTransactionOutcome
trAccountRecalculateArchivedSubHistoryTr(ATR_ARGS, NOCONST(AccountInfo) *pAccount,
									     SA_PARAM_NN_STR const char *pProductInternalName)
{
	if (!verify(NONNULL(pAccount))) return TRANSACTION_OUTCOME_FAILURE;
	if (!verify(pProductInternalName && *pProductInternalName)) return TRANSACTION_OUTCOME_FAILURE;

	if (!trAccountRecalculateArchivedSubHistory(pAccount, pProductInternalName))
		return TRANSACTION_OUTCOME_FAILURE;

	return TRANSACTION_OUTCOME_SUCCESS;
}

// Recalculate all subscription history
AUTO_TRANSACTION
ATR_LOCKS(pAccount, ".Ppsubscriptionhistory");
enumTransactionOutcome
trAccountRecalculateAllArchivedSubHistory(ATR_ARGS, NOCONST(AccountInfo) *pAccount)
{
	if (!verify(NONNULL(pAccount))) return TRANSACTION_OUTCOME_FAILURE;

	EARRAY_CONST_FOREACH_BEGIN(pAccount->ppSubscriptionHistory, iCurHistory, iNumHistory);
	{
		NOCONST(SubscriptionHistory) *pHistory = pAccount->ppSubscriptionHistory[iCurHistory];

		if (!devassert(pHistory)) continue;

		if (!trAccountRecalculateArchivedSubHistory(pAccount, pHistory->pProductInternalName))
			return TRANSACTION_OUTCOME_FAILURE;
	}
	EARRAY_FOREACH_END;

	return TRANSACTION_OUTCOME_SUCCESS;
}

// Enable/disable an entry
AUTO_TRANSACTION
ATR_LOCKS(pAccount, ".Ppsubscriptionhistory");
enumTransactionOutcome
trAccountEnableArchivedSubHistory(ATR_ARGS, NOCONST(AccountInfo) *pAccount,
								  SA_PARAM_NN_STR const char *pProductInternalName,
								  U32 uID,
								  int bEnable)
{
	int index;
	NOCONST(SubscriptionHistory) *pHistory;
	NOCONST(SubscriptionHistoryEntry) *pEntry;

	bEnable = !!bEnable;

	if (!verify(NONNULL(pAccount))) return TRANSACTION_OUTCOME_FAILURE;
	if (!verify(pProductInternalName && *pProductInternalName)) return TRANSACTION_OUTCOME_FAILURE;
	if (!verify(uID)) return TRANSACTION_OUTCOME_FAILURE;

	index = eaIndexedFindUsingString(&pAccount->ppSubscriptionHistory, pProductInternalName);

	// Nothing to do if it doesn't exist
	if (index < 0)
		return TRANSACTION_OUTCOME_SUCCESS;

	pHistory = pAccount->ppSubscriptionHistory[index];

	if (!devassert(pHistory))
		return TRANSACTION_OUTCOME_FAILURE;

	index = eaIndexedFindUsingInt(&pHistory->eaArchivedEntries, uID);

	// Nothing to do if it doesn't exist
	if (index < 0)
		return TRANSACTION_OUTCOME_SUCCESS;

	pEntry = pHistory->eaArchivedEntries[index];

	if (!devassert(pHistory))
		return TRANSACTION_OUTCOME_FAILURE;

	pEntry->bEnabled = bEnable;

	return TRANSACTION_OUTCOME_SUCCESS;
}



/************************************************************************/
/* Public functions                                                     */
/************************************************************************/

// Archive a subscription
void accountArchiveSubscription(U32 uAccountID,
							    SA_PARAM_NN_STR const char *pProductInternalName,
								SA_PARAM_OP_STR const char *pSubInternalName,
								SA_PARAM_OP_STR const char *pSubVID,
								U32 uStartTime,
								U32 uEndTime,
								SubscriptionTimeSource eSubTimeSource,
								SubscriptionHistoryEntryReason eReason,
								U32 uProblemFlags)
{
	// !!! Cannot call accountClearPermissionsCache because this function is called (directly or indirectly) from accountConstructPermissions

	if (!verify(uAccountID)) return;
	if (!verify(pProductInternalName && *pProductInternalName)) return;
	if (!verify(uStartTime <= uEndTime)) return;
	if (!verify(uStartTime && uEndTime)) return;
	if (!verify(eSubTimeSource != STS_Invalid)) return;
	if (!verify(eReason != SHER_Invalid)) return;

	PERFINFO_AUTO_START_FUNC();

	if (pSubVID && !*pSubVID) pSubVID = NULL;
	if (pSubInternalName && !*pSubInternalName) pSubInternalName = NULL;

	AutoTrans_trAccountArchiveSubscription(NULL, objServerType(), GLOBALTYPE_ACCOUNT, uAccountID,
		pProductInternalName, pSubInternalName, pSubVID, uStartTime, uEndTime, eSubTimeSource, eReason, uProblemFlags);

	PERFINFO_AUTO_STOP_FUNC();
}

// Recalculate subscription history
void accountRecalculateArchivedSubHistory(U32 uAccountID,
										  SA_PARAM_NN_STR const char *pProductInternalName)
{
	if (!verify(uAccountID)) return;
	if (!verify(pProductInternalName && *pProductInternalName)) return;

	PERFINFO_AUTO_START_FUNC();

	AutoTrans_trAccountRecalculateArchivedSubHistoryTr(NULL, objServerType(), GLOBALTYPE_ACCOUNT, uAccountID,
		pProductInternalName);

	PERFINFO_AUTO_STOP_FUNC();
}

// Recalculate all subscription history
void accountRecalculateAllArchivedSubHistory(U32 uAccountID)
{
	if (!verify(uAccountID)) return;

	AutoTrans_trAccountRecalculateAllArchivedSubHistory(NULL, objServerType(), GLOBALTYPE_ACCOUNT, uAccountID);
}

// Enable/disable an entry
void accountEnableArchivedSubHistory(U32 uAccountID,
									 SA_PARAM_NN_STR const char *pProductInternalName,
									 U32 uID,
									 bool bEnable)
{
	if (!verify(uAccountID)) return;
	if (!verify(pProductInternalName && *pProductInternalName)) return;
	if (!verify(uID)) return;

	AutoTrans_trAccountEnableArchivedSubHistory(NULL, objServerType(), GLOBALTYPE_ACCOUNT, uAccountID,
		pProductInternalName, uID, bEnable);

	accountRecalculateArchivedSubHistory(uAccountID, pProductInternalName);
}

// Calculate the total seconds of a subscription history entry
U32 subHistoryEntrySeconds(SA_PARAM_NN_VALID const SubscriptionHistoryEntry *pEntry)
{
	U32 uNow = timeSecondsSince2000();

	if (!verify(pEntry)) return 0;
	if (!verify(pEntry->uAdjustedEndSS2000 >= pEntry->uAdjustedStartSS2000)) return 0;

	if (!pEntry->bEnabled) return 0;
	if (pEntry->uAdjustedStartSS2000 > uNow) return 0;

	return MIN(uNow, pEntry->uAdjustedEndSS2000) - pEntry->uAdjustedStartSS2000;
}

// Determine the total number of seconds
U32 productTotalSeconds(SA_PARAM_NN_VALID const AccountInfo *pAccount,
						SA_PARAM_NN_STR const char *pProductInternalName)
{
	U32 uTotal = 0;
	int iHistoryIndex = 0;
	char ** eaSubInternalNames = NULL;
	const ProductContainer ** eaProducts = NULL;
	U32 uActiveSubStart = 0;
	U32 uActiveSubEnd = 0;
	U32 uNow = timeSecondsSince2000();
	ProductDefaultPermission * const * ppDefaults = NULL;

	if (!verify(pAccount)) return 0;
	if (!verify(pProductInternalName && *pProductInternalName)) return 0;

	PERFINFO_AUTO_START_FUNC();

	// Get a list of products the account owns that match the internal name given
	EARRAY_CONST_FOREACH_BEGIN(pAccount->ppProducts, iCurProduct, iNumProducts);
	{
		const AccountProductSub *pAccountProduct = pAccount->ppProducts[iCurProduct];
		const ProductContainer *pProduct = NULL;

		if (!devassert(pAccountProduct)) continue;

		pProduct = findProductByName(pAccountProduct->name);

		if (!devassert(pProduct)) continue;

		if (!stricmp_safe(pProduct->pInternalName, pProductInternalName))
			eaPush(&eaProducts, pProduct);
	}
	EARRAY_FOREACH_END;

	// Figure out which internal sub names would satisfy the product on the account
	EARRAY_CONST_FOREACH_BEGIN(eaProducts, iCurProduct, iNumProducts);
	{
		const ProductContainer *pProduct = eaProducts[iCurProduct];

		if (!devassert(pProduct)) continue;

		EARRAY_CONST_FOREACH_BEGIN(pProduct->ppRequiredSubscriptions, iCurRequiredSub, iNumRequiredSubs);
		{
			const char *pRequiredSub = pProduct->ppRequiredSubscriptions[iCurRequiredSub];

			if (!devassert(pRequiredSub && *pRequiredSub)) continue;

			eaPush(&eaSubInternalNames, estrDup(pRequiredSub));
		}
		EARRAY_FOREACH_END;
	}
	EARRAY_FOREACH_END;

	ppDefaults = productGetDefaultPermission();
	EARRAY_CONST_FOREACH_BEGIN(ppDefaults, iCurDefault, iNumDefaults);
	{
		ProductDefaultPermission *pDefaultPermission = ppDefaults[iCurDefault];

		if (!stricmp_safe(pDefaultPermission->pProductName, pProductInternalName))
		{
			EARRAY_CONST_FOREACH_BEGIN(pDefaultPermission->eaRequiredSubscriptions, iCurReq, iNumReq);
			{
				eaPush(&eaSubInternalNames, estrDup(pDefaultPermission->eaRequiredSubscriptions[iCurReq]));
			}
			EARRAY_FOREACH_END;
		}
	}
	EARRAY_FOREACH_END;

	eaRemoveDuplicateEStrings(&eaSubInternalNames);

	// Figure out the start and end of any active sub the person might have
	EARRAY_CONST_FOREACH_BEGIN(eaSubInternalNames, iCurSubInternalName, iNumSubInternalNames);
	{
		const char *pSubInternalName = eaSubInternalNames[iCurSubInternalName];
		const InternalSubscription *pInternalSub = NULL;
		const CachedAccountSubscription *pCachedSub = NULL;

		if (!devassert(pSubInternalName && *pSubInternalName)) continue;

		pInternalSub = findInternalSub(pAccount->uID, pSubInternalName);

		if (pInternalSub)
		{
			uActiveSubStart = uActiveSubStart ?
				MIN(pInternalSub->uCreated, uActiveSubStart) : pInternalSub->uCreated;
			uActiveSubEnd = uNow;
		}

		pCachedSub = findAccountSubscriptionByInternalName(pAccount, pSubInternalName);

		if (pCachedSub && getCachedSubscriptionStatus(pCachedSub) == SUBSCRIPTIONSTATUS_ACTIVE)
		{
			U32 uCachedSubStart = pCachedSub->estimatedCreationTimeSS2000 ? pCachedSub->estimatedCreationTimeSS2000 : pCachedSub->startTimeSS2000;

			if (uCachedSubStart)
			{
				uActiveSubStart = uActiveSubStart ?
					MIN(uCachedSubStart, uActiveSubStart) : uCachedSubStart;
				uActiveSubEnd = uNow;
			}
		}
	}
	EARRAY_FOREACH_END;

	// Make sure we have our active sub times in the right order
	if (uActiveSubStart && uActiveSubEnd)
	{
		U32 uFirstTime = uActiveSubStart;
		U32 uSecondTime = uActiveSubEnd;

		uActiveSubStart = MIN(uFirstTime, uSecondTime);
		uActiveSubEnd = MAX(uFirstTime, uSecondTime);
	}

	// Add the total from the history archive
	iHistoryIndex = eaIndexedFindUsingString(&pAccount->ppSubscriptionHistory, pProductInternalName);
	if (iHistoryIndex >= 0)
	{
		const SubscriptionHistory *pHistory = pAccount->ppSubscriptionHistory[iHistoryIndex];

		if (devassert(pHistory))
		{
			uTotal += subHistoryTotalSeconds(pHistory, &uActiveSubStart, &uActiveSubEnd);
		}
	}

	// Add the active sub time to our total
	if (uActiveSubStart && uActiveSubEnd)
	{
		uTotal += uActiveSubEnd - uActiveSubStart;
	}

	// Destroy the internal sub name array
	eaDestroyEString(&eaSubInternalNames);
	eaSubInternalNames = NULL;

	// Destroy the product array
	eaDestroy(&eaProducts);
	eaProducts = NULL;

	PERFINFO_AUTO_STOP_FUNC();

	return uTotal;
}

// Get all sub stats for an account
SA_RET_OP_VALID SubHistoryStats *getAllSubStats(SA_PARAM_NN_VALID const AccountInfo *pAccount)
{
	SubHistoryStats *pStats = NULL;
	STRING_EARRAY eaProductInternalNames = NULL;

	if (!verify(pAccount)) return NULL;

	PERFINFO_AUTO_START_FUNC();

	eaProductInternalNames = accountGetInternalProductList(pAccount, SUBSTATE_ANY, NULL);

	// Create stats structure
	pStats = StructCreate(parse_SubHistoryStats);

	if (!devassert(pStats)) goto out;

	// Add each individual stat
	EARRAY_CONST_FOREACH_BEGIN(eaProductInternalNames, iCurInternalName, iNumInternalNames);
	{
		const char * pProductInternalName = eaProductInternalNames[iCurInternalName];
		SubHistoryStat *pStat = NULL;

		if (!devassert(pProductInternalName && *pProductInternalName)) continue;

		pStat = StructCreate(parse_SubHistoryStat);

		if (!devassert(pStat)) goto out;

		pStat->pInternalProductName = strdup(pProductInternalName);
		pStat->uTotalSeconds = productTotalSeconds(pAccount, pProductInternalName);

		eaPush(&pStats->eaStats, pStat);
	}
	EARRAY_FOREACH_END;

out:

	// Clean up the list of internal names
	eaDestroyEx(&eaProductInternalNames, NULL);

	PERFINFO_AUTO_STOP_FUNC();

	return pStats;
}

#include "SubscriptionHistory_h_ast.c"