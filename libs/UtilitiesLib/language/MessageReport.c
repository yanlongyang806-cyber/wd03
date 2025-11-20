#ifndef NO_EDITORS

#include "EString.h"
#include "Message.h"
#include "ResourceInfo.h"
#include "ResourceSearch.h"
#include "StringCache.h"

#include "Autogen/MessageReport_c_ast.h"

AUTO_RUN_ANON(memBudgetAddMapping(__FILE__, BUDGET_Editors);); // Used editors because none of the memory allocated here will be done so by normal players

AUTO_STRUCT;
typedef struct MessageReference {
	const char *pchType; AST(UNOWNED)
		const char *pchLocation; AST(UNOWNED)
		U32 iCount; // Count of this reference in this location
} MessageReference;

AUTO_STRUCT;
typedef struct MessageCountSize {
	const char *pchLabel; AST(POOL_STRING)
		U32 iCount;
	U32 iSize;
} MessageCountSize;

static S32 s_iRequestId = 0;
static ResourceSearchResult **s_eaResults = NULL;

static void msgPrintMemoryUsage(MessageCountSize *data) {
	F32 fAvg = data->iCount ? (F32) data->iSize / (F32) data->iCount : 0;
	printf("%20s: %10d %10d %7.2f\n", data->pchLabel, data->iCount, data->iSize, fAvg);
}

static int msgCompareCountSize(const MessageCountSize **ppMcs1, const MessageCountSize **ppMcs2) {
	const MessageCountSize *pMcs1 = *ppMcs1;
	const MessageCountSize *pMcs2 = *ppMcs2;
	if (pMcs1->iSize != pMcs2->iSize) {
		return pMcs2->iSize - pMcs1->iSize;
	}

	return strcmp(pMcs1->pchLabel, pMcs2->pchLabel);
}

static void msgPrintAggregateMemoryUsage(char *pchTitle, StashTable stTable, bool bIgnoreSingleCountEntries) {
	MessageCountSize **eaMessageCountSize = NULL;
	S32 i;

	printf("%s\n", pchTitle);
	printf("-----------------------------\n");
	FOR_EACH_IN_STASHTABLE(stTable, MessageCountSize, pCountSize) 
	{
		if (!bIgnoreSingleCountEntries || pCountSize->iCount > 1) {
			eaPush(&eaMessageCountSize, pCountSize);
		}
	}
	FOR_EACH_END

		eaQSort(eaMessageCountSize, msgCompareCountSize);

	for (i=0; i < eaSize(&eaMessageCountSize); i++) {
		MessageCountSize *pCountSize = eaMessageCountSize[i];
		msgPrintMemoryUsage(pCountSize);		
	}

	printf("\n");
}

static void msgUpdateData(const char *str, MessageCountSize *data) {
	if (str) {
		data->iCount++;
		data->iSize += (U32) strlen(str) + 1;
	}
}

static void msgUpdateAggregateData(StashTable stTable, SA_PARAM_NN_STR const char *pchLookupKey, S32 iSize) {
	MessageCountSize *pLookup = NULL;
	const char *pchUniqueKey = allocAddString(pchLookupKey);

	if (!stashFindPointer(stTable, pchUniqueKey, &pLookup)) {
		pLookup = StructAlloc(parse_MessageCountSize);
		pLookup->pchLabel = pchUniqueKey;
		stashAddPointer(stTable, pchUniqueKey, pLookup, true);
	}
	pLookup->iCount++;
	pLookup->iSize += iSize;
}

static void msgUpdateAggregateKeyTranslateData(StashTable stKeyTable, StashTable stTrTable, const char *pchLookupKey, Message *pMsg) {
	msgUpdateAggregateData(stKeyTable, pchLookupKey, (S32) strlen(pMsg->pcMessageKey));
	msgUpdateAggregateData(stTrTable, pchLookupKey, (S32) strlen(pMsg->pcDefaultString));
}

SA_RET_NN_STR static const char *msgFindUniqueLookupKey(StashTable stTable, SA_PARAM_NN_STR const char *pchLookupKey) {
	// Ugly...but I really need to hash on the string value, not the pointer.
	FOR_EACH_IN_STASHTABLE(stTable, MessageCountSize, pCounts)
	{
		if (strcmp(pCounts->pchLabel, pchLookupKey)==0) {
			return pCounts->pchLabel;
		}
	}
	FOR_EACH_END

		return pchLookupKey;
}

#define COSTUMES_KEY "Defs/Costumes"
#define GENS_KEY "UI/Gens"

SA_RET_NN_STR static const char *msgGetFilenameForLookup(StashTable stTable, const char *pchFilename) {
	static char *pchLookup = NULL;

	if (pchFilename && strStartsWith(pchFilename, COSTUMES_KEY)) {
		return COSTUMES_KEY;
	} else if (pchFilename && strStartsWith(pchFilename, GENS_KEY)) {
		return GENS_KEY;
	}

	if (!pchFilename || !*pchFilename) {
		return "<<EMPTY>>";
	}

	if (strchr(pchFilename, '/')) {
		estrClear(&pchLookup);
		estrCopy2(&pchLookup, pchFilename);
		estrTruncateAtLastOccurrence(&pchLookup, '/');
		return msgFindUniqueLookupKey(stTable, pchLookup);
	}

	return pchFilename;
}

static void msgUpdateKeyParts(StashTable stTable, char *pchKey, S32 iSize) {
	char *pchLastDot = strrchr(pchKey, '.');
	char *pchLastUnderbar = strrchr(pchKey, '_');

	msgUpdateAggregateData(stTable, pchKey, iSize);

	// Give precedence to usage of '.' as a separator over '_'.
	// i.e. only use '_' as separator if there are no '.'s.
	if (pchLastDot) {
		*pchLastDot = '\0';
		msgUpdateKeyParts(stTable, pchKey, iSize);
	} else if (pchLastUnderbar) {
		*pchLastUnderbar = '\0';
		msgUpdateKeyParts(stTable, pchKey, iSize);
	}
}

static void msgUpdateKeyPartCounts(StashTable stTable, const char *pchKey) {
	char *pchCopy = strdup(pchKey);
	msgUpdateKeyParts(stTable, pchCopy, (S32) strlen(pchCopy)+1);
	free(pchCopy);
}

void msgDumpMemoryUsage(void) {
	MessageCountSize messageKeys = { "Message Keys", 0, 0};
	MessageCountSize defaultStrings = { "Defaults", 0, 0};
	MessageCountSize filenames = { "Filenames", 0, 0 };
	MessageCountSize scopes = { "Scopes", 0, 0 };

	StashTable stKeyCountsByFilename = stashTableCreateWithStringKeys(20000, StashDefault);
	StashTable stTranslateCountsByFilename = stashTableCreateWithStringKeys(20000, StashDefault);
	StashTable stKeyCountsByKeyPart = stashTableCreateWithStringKeys(100000, StashDefault);

	// bytes, count for messageKey & translated messages (client only)
	FOR_EACH_IN_REFDICT(gMessageDict, Message, pMsg)
	{
		if (pMsg) {
			msgUpdateData(pMsg->pcMessageKey, &messageKeys);
			msgUpdateData(pMsg->pcDefaultString, &defaultStrings);
			msgUpdateData(pMsg->pcFilename, &filenames);
			msgUpdateData(pMsg->pcScope, &scopes);

			msgUpdateAggregateKeyTranslateData(stKeyCountsByFilename, stTranslateCountsByFilename, msgGetFilenameForLookup(stKeyCountsByFilename, pMsg->pcFilename), pMsg);
			msgUpdateKeyPartCounts(stKeyCountsByKeyPart, pMsg->pcMessageKey);
		}
	}
	FOR_EACH_END;

	printf("Message Memory usage:\n");
	printf("---------------------\n");
	msgPrintMemoryUsage(&messageKeys);
	msgPrintMemoryUsage(&defaultStrings);
	msgPrintMemoryUsage(&filenames);
	msgPrintMemoryUsage(&scopes);

	msgPrintAggregateMemoryUsage("Keys by Filename", stKeyCountsByFilename, false);
	//msgPrintAggregateMemoryUsage("Translations by Filename", stTranslateCountsByFilename);
	msgPrintAggregateMemoryUsage("Keys by Part", stKeyCountsByKeyPart, true);

	stashTableDestroyStruct(stKeyCountsByFilename, NULL, parse_MessageCountSize);
	stashTableDestroyStruct(stTranslateCountsByFilename, NULL, parse_MessageCountSize);
	stashTableDestroyStruct(stKeyCountsByKeyPart, NULL, parse_MessageCountSize);
	printf("\n");
}

AUTO_COMMAND ACMD_NAME(DumpMessageMemory);
void cmdMsgDumpMemoryUsage(void) {
	msgDumpMemoryUsage();
}

void msgReportDumpSearchResultTypes(ResourceSearchResult *pResult) {
	StashTable stTypeCounts = stashTableCreateWithStringKeys(eaSize(&pResult->eaRows) * 2 + 1, StashDefault);
	S32 i;

	for (i=0; i < eaSize(&pResult->eaRows); i++) {
		ResourceSearchResultRow *pRow = pResult->eaRows[i];
		S32 iCount=0;
		stashFindInt(stTypeCounts, pRow->pcType, &iCount);
		iCount++;
		stashAddInt(stTypeCounts, pRow->pcType, iCount, true);
	}

	FOR_EACH_IN_STASHTABLE2(stTypeCounts, elem)
	{
		int	iCount = stashElementGetInt(elem);
		char *pchKey = stashElementGetKey(elem);
		printf("Count: %5d Type: %s\n", iCount, pchKey);
	}
	FOR_EACH_END

		stashTableDestroy(stTypeCounts);
}

void msgReportDumpResultDetails(ResourceSearchResult *pResult) {
	S32 i;
	for (i=0; i < eaSize(&pResult->eaRows); i++) {
		ResourceSearchResultRow *pRow = pResult->eaRows[i];
		ResourceInfo *pInfo = resGetInfo(pRow->pcType, pRow->pcName);
		const char *pchLocation = pInfo ? pInfo->resourceLocation : "???";
		printf("Type: %-20s Name: %-20s Location: %s\n", pRow->pcType, pRow->pcName, pchLocation);
	}
}

void msgReportResourcesResult(ResourceSearchResult *pResult)
{
	msgReportDumpSearchResultTypes(pResult);
	//msgReportDumpResultDetails(pResult);
}

#include "AutoGen/MessageReport_c_ast.c"

#endif
