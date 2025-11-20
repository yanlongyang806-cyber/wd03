/***************************************************************************
*     Copyright (c) 2005-2012, Cryptic Studios
*     All Rights Reserved
*     Confidential Property of Cryptic Studios
***************************************************************************/

#include "ActivityCommon.h"
#include "AutoGen/ActivityCommon_h_ast.h"
#include "TransactionOutcomes.h"

AUTO_TRANS_HELPER
ATR_LOCKS(pContainer, ".Ppentries[AO]");
NOCONST(EventEntry) *aslMapManagerActivity_NewEventEntry(ATH_ARG NOCONST(EventContainer) *pContainer, const char *pchEventName)
{
	NOCONST(EventEntry) *newEntry = (NOCONST(EventEntry)*) StructCreate(parse_EventEntry);

	newEntry->pchEventName = StructAllocString(pchEventName);
	newEntry->uLastTimeStarted = 0;
	newEntry->iTimingIndex = -1;
	newEntry->iEventRunMode = kEventRunMode_Auto;

	eaIndexedAdd(&pContainer->ppEntries,newEntry);

	return newEntry;
}

AUTO_TRANS_HELPER
ATR_LOCKS(pcontainer, ".Ppentries");
bool aslMapManagerActivity_trh_UpdateEventEntry(ATH_ARG NOCONST(EventContainer) *pcontainer, const char *pchEventName, U32 uTimeStart, int iTimingIndex, int iEventRunMode)
{
	int iEntryIndex = eaIndexedFindUsingString(&pcontainer->ppEntries,pchEventName);
	NOCONST(EventEntry) *entry = NULL;

	if(iEntryIndex >= 0)
	{
		entry = pcontainer->ppEntries[iEntryIndex];
	}
	else
	{
		entry = aslMapManagerActivity_NewEventEntry(pcontainer,pchEventName);
	}

	if(entry)
	{
		entry->uLastTimeStarted = uTimeStart;
		entry->iTimingIndex = iTimingIndex;
		entry->iEventRunMode = iEventRunMode;
		
		return true;
	}

	return false;
}

AUTO_TRANSACTION
ATR_LOCKS(pcontainer, ".Ppentries");
enumTransactionOutcome aslMapManagerActivity_trans_UpdateEventEntry(ATR_ARGS, NOCONST(EventContainer) *pcontainer, const char *pchEventName,
																			U32 uTimeStart, int iTimingIndex, int iEventRunMode)
{
	if(!aslMapManagerActivity_trh_UpdateEventEntry(pcontainer,pchEventName,uTimeStart,iTimingIndex,iEventRunMode))
	{
		return TRANSACTION_OUTCOME_FAILURE;
	}

	return TRANSACTION_OUTCOME_SUCCESS;
}
