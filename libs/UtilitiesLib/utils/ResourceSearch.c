/***************************************************************************



***************************************************************************/

#include "Expression.h"
#include "estring.h"
#include "timing.h"
#include "ResourceSearch.h"
#include "ResourceInfo.h"
#include "StringCache.h"

#include "AutoGen/ResourceSearch_h_ast.c"

AUTO_RUN_ANON(memBudgetAddMapping(__FILE__, BUDGET_GameSystems););

static StashTable stTagCache = NULL;
static SearchItemDefRef **s_ppItemDefRefs = NULL;

static void tagParseAllocedString(const char *pcTagString, const char ***peaTags);
static bool tagMatchDetails(const ResourceSearchRequestTags* details, const char **eaCompleteTags);

ResourceSearchResultRow *createResourceSearchResultRow(const char *pcName, const char *pcType, const char *pcExtraData)
{
	ResourceSearchResultRow *pRow = StructCreate(parse_ResourceSearchResultRow);

	pRow->pcName = StructAllocString(pcName);
	pRow->pcType = StructAllocString(pcType);
	pRow->pcExtraData = StructAllocString(pcExtraData);

	return pRow;
}


void SearchUsageGeneric(ResourceSearchRequest *pRequest, ResourceSearchResult *pResult, const char **ppIgnoreDictionaries)
{
	const char *pcType = pRequest->pcType;
	const char *pcName = pRequest->pcName;
	int i;
	ResourceInfoHolder infoHolder = {0};

	if (!pRequest->pcType || !pRequest->pcName)
		return;

	eaClearStruct(&s_ppItemDefRefs, parse_SearchItemDefRef);

	resFindReferencesToResource(pcType, pcName, &infoHolder);

	for (i = 0; i < eaSize(&infoHolder.ppInfos); i++)
	{
		int j;
		ResourceInfo *pInfo = infoHolder.ppInfos[i];
		bool bSkip = false;

		for (j = 0; j < eaSize(&ppIgnoreDictionaries); j++)
		{
			if (pInfo->resourceDict == ppIgnoreDictionaries[j])
			{
				bSkip = true;
				break;
			}
		}
		if (bSkip)
			continue;
		for (j = 0; j < eaSize(&pInfo->ppReferences); j++)
		{
			ResourceReference *pRef = pInfo->ppReferences[j];
			SearchItemDefRef *pDefRef = StructCreate(parse_SearchItemDefRef);
			ResourceSearchResultRow *pRow = NULL;
			if (stricmp(pRef->resourceDict, pcType) == 0 && stricmp(pRef->resourceName, pcName) == 0)
			{
				char *relationType;
				if (pRef->referenceType == REFTYPE_CHILD_OF)
					relationType = SEARCH_RELATION_CHILD;
				else if (pRef->referenceType == REFTYPE_CONTAINS)
					relationType = SEARCH_RELATION_CONTAINS;
				else
					relationType = SEARCH_RELATION_USES;
				if(pInfo->resourceDict == allocFindString("ItemDef"))
				{
					SET_HANDLE_FROM_STRING(pInfo->resourceDict, pInfo->resourceName, pDefRef->hDef);
					eaPush(&s_ppItemDefRefs, pDefRef);
				}
				pRow = createResourceSearchResultRow((char*)pInfo->resourceName, pInfo->resourceDict, relationType);
				pRow->pResRef = StructClone(parse_SearchItemDefRef, pDefRef);
				eaPush(&pResult->eaRows, pRow);
			}
		}
	}

	// these are shallow copies
	eaDestroy(&infoHolder.ppInfos);
}

void SearchParentUsageGeneric(ResourceSearchRequest *pRequest, ResourceSearchResult *pResult, const char **ppIgnoreDictionaries)
{
	const char *pcType = pRequest->pcType;
	const char *pcName = pRequest->pcName;
	int i;
	ResourceInfoHolder infoHolder = {0};

	if (!pRequest->pcType || !pRequest->pcName)
		return;

	eaClearStruct(&s_ppItemDefRefs, parse_SearchItemDefRef);

	resFindReferencesToResource(pcType, pcName, &infoHolder);

	for (i = 0; i < eaSize(&infoHolder.ppInfos); i++)
	{
		int j;
		ResourceInfo *pInfo = infoHolder.ppInfos[i];
		bool bSkip = false;

		for (j = 0; j < eaSize(&ppIgnoreDictionaries); j++)
		{
			if (pInfo->resourceDict == ppIgnoreDictionaries[j])
			{
				bSkip = true;
				break;
			}
		}
		if (bSkip)
			continue;
		for (j = 0; j < eaSize(&pInfo->ppReferences); j++)
		{
			ResourceReference *pRef = pInfo->ppReferences[j];
			SearchItemDefRef *pDefRef = StructCreate(parse_SearchItemDefRef);
			ResourceSearchResultRow *pRow = NULL;
			if (stricmp(pRef->resourceDict, pcType) == 0 && stricmp(pRef->resourceName, pcName) == 0)
			{
				char *relationType;
				if (pRef->referenceType == REFTYPE_CHILD_OF)
					relationType = SEARCH_RELATION_CHILD;
				else if (pRef->referenceType == REFTYPE_CONTAINS)
					relationType = SEARCH_RELATION_CONTAINS;
				else
					relationType = SEARCH_RELATION_USES;
				{
					ResourceSearchRequest TempRequest;
					TempRequest.pcName = (char *)pInfo->resourceName;
					TempRequest.pcType = (char *)pInfo->resourceDict;
					SearchUsageGeneric(&TempRequest, pResult, ppIgnoreDictionaries);
				}
			}
		}
	}

	eaDestroy(&infoHolder.ppInfos);
}

void SearchReferencesGeneric(ResourceSearchRequest *pRequest, ResourceSearchResult *pResult)
{
	const char *pcType = pRequest->pcType;
	const char *pcName = pRequest->pcName;
	ResourceInfo *pInfo = resGetInfo(pcType, pcName);
	if (pInfo)
	{	
		int j;

		eaClearStruct(&s_ppItemDefRefs, parse_SearchItemDefRef);

		for (j = 0; j < eaSize(&pInfo->ppReferences); j++)
		{
			ResourceReference *pRef = pInfo->ppReferences[j];
			char *relationType;
			ResourceSearchResultRow *pRow = NULL;
			SearchItemDefRef *pDefRef = StructCreate(parse_SearchItemDefRef);

			if (pRef->referenceType == REFTYPE_CHILD_OF)
				relationType = SEARCH_RELATION_CHILD;
			else if (pRef->referenceType == REFTYPE_CONTAINS)
				relationType = SEARCH_RELATION_CONTAINS;
			else
				relationType = SEARCH_RELATION_USES;
			if(pRef->resourceDict == allocFindString("ItemDef"))
			{
				SET_HANDLE_FROM_STRING(pRef->resourceDict, pRef->resourceName, pDefRef->hDef);
				eaPush(&s_ppItemDefRefs, pDefRef);
			}
			pRow = createResourceSearchResultRow((char*)pRef->resourceName, pRef->resourceDict, relationType);
			pRow->pResRef = StructClone(parse_SearchItemDefRef, pDefRef);
			eaPush(&pResult->eaRows, pRow);
		}
	}
}

__forceinline static void ReturnAllResourceInfos(ResourceSearchResult *pResult, ResourceDictionaryInfo *pDictInfo)
{
	int i;

	if(pDictInfo->pDictName == allocFindString("ItemDef"))
	{
		if(!s_ppItemDefRefs)
		{
			eaCreate(&s_ppItemDefRefs);
		}
		else
		{
			eaClearStruct(&s_ppItemDefRefs, parse_SearchItemDefRef);
		}
	}

	for (i = 0; i < eaSize(&pDictInfo->ppInfos); i++)
	{
		ResourceInfo *pInfo = pDictInfo->ppInfos[i];
		ResourceSearchResultRow *pRow = NULL;
		SearchItemDefRef *pDefRef = StructCreate(parse_SearchItemDefRef);

		if(pDictInfo->pDictName == allocFindString("ItemDef"))
		{
			SET_HANDLE_FROM_STRING(pDictInfo->pDictName, pInfo->resourceName, pDefRef->hDef);
			eaPush(&s_ppItemDefRefs, pDefRef);
		}

		pRow = createResourceSearchResultRow((char*)pInfo->resourceName, pInfo->resourceDict, NULL);
		pRow->pResRef = StructClone(parse_SearchItemDefRef, pDefRef);
		eaPush(&pResult->eaRows, pRow);
	}
}

void SearchListGeneric(ResourceSearchRequest *pRequest, ResourceSearchResult *pResult)
{
	ResourceDictionaryInfo *pDictInfo;

	if (!pRequest->pcType)
	{
		// check all dictionaries
		ResourceDictionaryIterator res_iterator;
		resDictInitIterator(&res_iterator);

		while ((pDictInfo = resDictIteratorGetNextInfo(&res_iterator)) != NULL)
		{
			ReturnAllResourceInfos(pResult, pDictInfo);
		}

		return;
	}

	pDictInfo = resDictGetInfo(pRequest->pcType);
	if ( !pDictInfo )
		return;

	ReturnAllResourceInfos(pResult, pDictInfo);
}

void SearchTagComplexGeneric(ResourceSearchRequest *pRequest, ResourceSearchResult *pResult)
{
	const char *pcType = pRequest->pcType;
	ResourceDictionaryInfo *pDictInfo = resDictGetInfo(pcType);
	const char **eaInfoTags = NULL;
	int i;
	
	if( !pRequest->pTagsDetails || !pcType || !pDictInfo ) {
		return;
	}

	PERFINFO_AUTO_START_FUNC();

	eaClearStruct(&s_ppItemDefRefs, parse_SearchItemDefRef);
	for( i = 0; i < eaSize(&pDictInfo->ppInfos); i++ ) {
		ResourceInfo* pInfo = pDictInfo->ppInfos[i];

		if( pRequest->filterFn && !pRequest->filterFn( pInfo, pRequest->filterData )) {
			continue;
		}
		if( !pInfo->resourceTags ) {
			continue;
		}

		tagParseAllocedString( pInfo->resourceTags, &eaInfoTags );
		if( tagMatchDetails( pRequest->pTagsDetails, eaInfoTags )) {
			ResourceSearchResultRow *pRow = NULL;
			SearchItemDefRef *pDefRef = StructCreate(parse_SearchItemDefRef);
			if(pInfo->resourceDict == allocFindString("ItemDef"))
			{
				SET_HANDLE_FROM_STRING(pInfo->resourceDict, pInfo->resourceName, pDefRef->hDef);
				eaPush(&s_ppItemDefRefs, pDefRef);
			}
			pRow = createResourceSearchResultRow(pInfo->resourceName, pInfo->resourceDict, NULL);
			pRow->pResRef = StructClone(parse_SearchItemDefRef, pDefRef);
			eaPush(&pResult->eaRows, pRow);
			if(pRequest->one_result)
				break;
		}
	}

	PERFINFO_AUTO_STOP();
}

void SearchTagGeneric(ResourceSearchRequest *pRequest, ResourceSearchResult *pResult)
{
	const char *pcType = pRequest->pcType;
	int i;
	ResourceDictionaryInfo *pDictInfo = resDictGetInfo(pcType);
	const char **eaSearchTags = NULL;
	const char **eaInfoTags = NULL;
	
	PERFINFO_AUTO_START_FUNC();

	eaClearStruct(&s_ppItemDefRefs, parse_SearchItemDefRef);

	if (!pRequest->pcSearchDetails || !pRequest->pcType || !pDictInfo) {
		PERFINFO_AUTO_STOP();
		return;
	}

	// Parse the tags for the search
	tagParse(pRequest->pcSearchDetails, &eaSearchTags);

	for (i = 0; i < eaSize(&pDictInfo->ppInfos); i++)
	{
		ResourceInfo *pInfo = pDictInfo->ppInfos[i];
		if (!pRequest->filterFn || pRequest->filterFn(pInfo, pRequest->filterData))
		{
			if (pInfo->resourceTags)
			{
				tagParseAllocedString(pInfo->resourceTags, &eaInfoTags);
				if (tagMatchSubset(eaSearchTags, eaInfoTags))
				{
					ResourceSearchResultRow *pRow = NULL;
					SearchItemDefRef *pDefRef = StructCreate(parse_SearchItemDefRef);
					if(pInfo->resourceDict == allocFindString("ItemDef"))
					{
						SET_HANDLE_FROM_STRING(pInfo->resourceDict, pInfo->resourceName, pDefRef->hDef);
						eaPush(&s_ppItemDefRefs, pDefRef);
					}
					pRow = createResourceSearchResultRow(pInfo->resourceName, pInfo->resourceDict, NULL);
					pRow->pResRef = StructClone(parse_SearchItemDefRef, pDefRef);
					eaPush(&pResult->eaRows, pRow);
					if(pRequest->one_result)
						break;
				}
			}
		}
	}

	eaDestroy(&eaSearchTags);
	PERFINFO_AUTO_STOP();
}

static bool ExprSearchCheck(Expression *pExpression, void *pObject, const char *pchPathString, ResourceSearchRequest *pRequest)
{
	if (exprMatchesString(pExpression, pRequest->pcSearchDetails))
		return true;
	return false;
}

void SearchExprGeneric(ResourceSearchRequest *pRequest, ResourceSearchResult *pResult)
{
	const char *pcType = pRequest->pcType;
	int i;
	ResourceDictionaryInfo *pDictInfo = resDictGetInfo(pcType);

	if (!pRequest->pcSearchDetails || !pRequest->pcType || !pDictInfo)
		return;

	eaClearStruct(&s_ppItemDefRefs, parse_SearchItemDefRef);

	for (i = 0; i < eaSize(&pDictInfo->ppInfos); i++)
	{
		ResourceInfo *pInfo = pDictInfo->ppInfos[i];
		void *pResource = resGetObject(pInfo->resourceDict, pInfo->resourceName);

		if (pResource)
		{		
			if (ParserScanForSubstruct(pDictInfo->pDictTable, pResource, parse_Expression, 0, 0, ExprSearchCheck, pRequest))
			{
				ResourceSearchResultRow *pRow = NULL;
				SearchItemDefRef *pDefRef = StructCreate(parse_SearchItemDefRef);
				if(pInfo->resourceDict == allocFindString("ItemDef"))
				{
					SET_HANDLE_FROM_STRING(pInfo->resourceDict, pInfo->resourceName, pDefRef->hDef);
					eaPush(&s_ppItemDefRefs, pDefRef);
				}
				pRow = createResourceSearchResultRow(pInfo->resourceName, pInfo->resourceDict, NULL);
				pRow->pResRef = StructClone(parse_SearchItemDefRef, pDefRef);
				eaPush(&pResult->eaRows, pRow);
			}
		}
	}
}

static void SearchDispGeneric(ResourceSearchRequest *pRequest, ResourceSearchResult *pResult)
{
	int i;
	ResourceDictionaryInfo *pDictInfo = resDictGetInfo(pRequest->pcType);
	char searchString[1024];
	
	PERFINFO_AUTO_START_FUNC();

	eaClearStruct(&s_ppItemDefRefs, parse_SearchItemDefRef);

	if (!pDictInfo || !pRequest->pcSearchDetails || !pRequest->pcSearchDetails[0]) {
		PERFINFO_AUTO_STOP();
		return;
	}

	sprintf(searchString, "*%s*", pRequest->pcSearchDetails);

	for (i = 0; i < eaSize(&pDictInfo->ppInfos); i++) {
		ResourceInfo *pInfo = pDictInfo->ppInfos[i];
		if(pInfo->resourceDisplayName && pInfo->resourceDisplayName[0] && matchExact(searchString, pInfo->resourceDisplayName))
		{
			ResourceSearchResultRow *pRow = NULL;
			SearchItemDefRef *pDefRef = StructCreate(parse_SearchItemDefRef);
			if(pInfo->resourceDict == allocFindString("ItemDef"))
			{
				SET_HANDLE_FROM_STRING(pInfo->resourceDict, pInfo->resourceName, pDefRef->hDef);
				eaPush(&s_ppItemDefRefs, pDefRef);
			}
			pRow = createResourceSearchResultRow(pInfo->resourceName, pInfo->resourceDict, NULL);
			pRow->pResRef = StructClone(parse_SearchItemDefRef, pDefRef);
			eaPush(&pResult->eaRows, pRow);
		}
	}

	PERFINFO_AUTO_STOP();
}

typedef struct FieldSearchStruct
{
	ResourceSearchRequest *pRequest;	
	ParseTable *pTable;
	int column;
} FieldSearchStruct;

static bool FieldSearchCheck(void *pObject, void *pRootObject, const char *pchPathString, FieldSearchStruct *pSearchStruct)
{
	static char *pSearchString;
	char realMatch[1024];
	estrClear(&pSearchString);
	TokenWriteText(pSearchStruct->pTable, pSearchStruct->column, pObject, &pSearchString, 0);

	sprintf(realMatch, "*%s*", pSearchStruct->pRequest->pcSearchDetails);

	if (estrLength(&pSearchString) && matchExact(realMatch, pSearchString))
	{
		return true;
	}

	return false;
}

void SearchFieldGeneric(ResourceSearchRequest *pRequest, ResourceSearchResult *pResult)
{
	char pStructName[1024], pFieldName[1024];
	const char *pcType = pRequest->pcType;
	int i;
	ResourceDictionaryInfo *pDictInfo = resDictGetInfo(pcType);
	char *pDot;	
	FieldSearchStruct searchStruct = {0};

	if (!pRequest->pcSearchDetails || !pRequest->pcType || !pRequest->pcName || !pDictInfo)
		return;

	eaClearStruct(&s_ppItemDefRefs, parse_SearchItemDefRef);

	pDot = strchr(pRequest->pcName,'.');
	
	if (!pDot)
		return;

	strncpy(pStructName, pRequest->pcName, pDot - pRequest->pcName);
	strcpy(pFieldName, pDot + 1);

	searchStruct.pRequest = pRequest;
	searchStruct.pTable = ParserGetTableFromStructName(pStructName);
	
	if (!searchStruct.pTable)
		return;

	if (!ParserFindColumn(searchStruct.pTable, pFieldName, &searchStruct.column))
		return;

	for (i = 0; i < eaSize(&pDictInfo->ppInfos); i++)
	{
		ResourceInfo *pInfo = pDictInfo->ppInfos[i];
		void *pResource = resGetObject(pInfo->resourceDict, pInfo->resourceName);

		if (pResource)
		{
			if (ParserScanForSubstruct(pDictInfo->pDictTable, pResource, searchStruct.pTable, 0, 0, FieldSearchCheck, &searchStruct))
			{
				ResourceSearchResultRow *pRow = NULL;
				SearchItemDefRef *pDefRef = StructCreate(parse_SearchItemDefRef);
				if(pInfo->resourceDict == allocFindString("ItemDef"))
				{
					SET_HANDLE_FROM_STRING(pInfo->resourceDict, pInfo->resourceName, pDefRef->hDef);
					eaPush(&s_ppItemDefRefs, pDefRef);
				}
				pRow = createResourceSearchResultRow(pInfo->resourceName, pInfo->resourceDict, NULL);
				pRow->pResRef = StructClone(parse_SearchItemDefRef, pDefRef);
				eaPush(&pResult->eaRows, pRow);
			}
		}
	}
}




typedef struct CustomSearchHandlerStruct
{
	ResourceSearchMode eSearchMode;
	const char *pcType;
	
	CustomResourceSearchHandler *pHandler;

} CustomSearchHandlerStruct;

CustomSearchHandlerStruct **ppCustomSearchHandlers;

CustomSearchHandlerStruct *findCustomSearchHandler(ResourceSearchMode eSearchMode, const char *pcType)
{
	int i;
	pcType = allocFindString(pcType);

	if (!pcType)
	{
		return NULL;
	}
	for (i = eaSize(&ppCustomSearchHandlers) - 1; i >= 0; i--)
	{
		if (ppCustomSearchHandlers[i]->eSearchMode == eSearchMode
			 && ppCustomSearchHandlers[i]->pcType == pcType)
		{
			return ppCustomSearchHandlers[i];
		}
	}
	return NULL;
}

void registerCustomResourceSearchHandler(ResourceSearchMode eSearchMode, const char *pcType, CustomResourceSearchHandler *pHandler)
{
	CustomSearchHandlerStruct *pStruct = calloc(sizeof(CustomSearchHandlerStruct),1);
	pStruct->eSearchMode = eSearchMode;
	pStruct->pcType = allocAddString(pcType);
	pStruct->pHandler = pHandler;

	assertmsgf(!findCustomSearchHandler(eSearchMode, pcType), "Custom Search Handler for %s %s already registered!", StaticDefineIntRevLookup(ResourceSearchModeEnum, eSearchMode), pcType);

	eaPush(&ppCustomSearchHandlers, pStruct);
}


ResourceSearchResult *continueResourceSearchRequest(ResourceSearchRequest *pRequest, ResourceSearchResult *pExistingResult)
{
	CustomSearchHandlerStruct *pStruct;
	ResourceSearchResult *pResult = pExistingResult;
	PERFINFO_AUTO_START_FUNC();
	if (!pResult)
	{
		pResult = StructCreate(parse_ResourceSearchResult);
		pResult->iRequest = pRequest->iRequest;
	}

	if (pStruct = findCustomSearchHandler(pRequest->eSearchMode, pRequest->pcType))
	{
		pStruct->pHandler(pRequest, pResult);
	}
	else
	{
		if (pRequest->eSearchMode == SEARCH_MODE_USAGE) 
		{
			SearchUsageGeneric(pRequest, pResult, NULL);
		}
		else if (pRequest->eSearchMode == SEARCH_MODE_REFERENCES) 
		{
			SearchReferencesGeneric(pRequest, pResult);
		}
		else if (pRequest->eSearchMode == SEARCH_MODE_LIST) 
		{
			SearchListGeneric(pRequest, pResult);
		}
		else if (pRequest->eSearchMode == SEARCH_MODE_TAG_SEARCH) 
		{
			SearchTagGeneric(pRequest, pResult);
		}
		else if (pRequest->eSearchMode == SEARCH_MODE_TAG_COMPLEX_SEARCH)
		{
			SearchTagComplexGeneric(pRequest, pResult);
		}
		else if (pRequest->eSearchMode == SEARCH_MODE_EXPR_SEARCH) 
		{
			SearchExprGeneric(pRequest, pResult);
		}
		else if (pRequest->eSearchMode == SEARCH_MODE_DISP_SEARCH) 
		{
			SearchDispGeneric(pRequest, pResult);
		}
		else if (pRequest->eSearchMode == SEARCH_MODE_FIELD_SEARCH) 
		{
			SearchFieldGeneric(pRequest, pResult);
		}
		else if (pRequest->eSearchMode == SEARCH_MODE_PARENT_USAGE)
		{
			SearchParentUsageGeneric(pRequest, pResult, NULL);
		}
	}
	PERFINFO_AUTO_STOP();
	return pResult;
}


static void tagParseAddEntry(const char ***peaTagCombinations, const char *pcTag)
{
	const char *pcCurrentTag;
	int cmp, i;

	for(i=0; i<eaSize(peaTagCombinations); ++i) {
		pcCurrentTag = (*peaTagCombinations)[i];
		cmp = stricmp(pcCurrentTag, pcTag);
		if (cmp == 0) {
			return;
		}
		if (cmp > 0) {
			eaInsert(peaTagCombinations, pcTag, i);
			return;
		}
	}

	eaPush(peaTagCombinations, pcTag);
}


// Parses the tag string into an earray of tags.  The eArray contains pooled strings.
// Duplicates are remove.  The array is sorted.
void tagParse(const char *pcTagString, const char ***peaTags)
{
	char buf[1024];
	char *ptr1, *ptr2, *ptr3;
	PERFINFO_AUTO_START_FUNC();

	if (!pcTagString) {
		PERFINFO_AUTO_STOP();
		return;
	}

	strcpy(buf, pcTagString);

	ptr1 = buf;
	while (ptr2 = strchr(ptr1, ',')) {
		*ptr2 = '\0';

		// trim leading spaces
		while (*ptr1 == ' ') {
			++ptr1;
		}

		// trim trailing spaces
		ptr3 = ptr2-1;
		while ((ptr3 > ptr1) && (*ptr3 == ' ')) {
			*ptr3 = '\0';
			--ptr3;
		}

		// Add the string
		if (*ptr1) {
			tagParseAddEntry(peaTags, allocAddString(ptr1));
		}
		++ptr2;
		ptr1 = ptr2;
	}

	// trim leading spaces
	while (*ptr1 == ' ') {
		++ptr1;
	}

	// trim trailing spaces
	ptr3 = ptr1 + strlen(ptr1) - 1;
	while ((ptr3 > ptr1) && (*ptr3 == ' ')) {
		*ptr3 = '\0';
		--ptr3;
	}

	// Add the string
	if (*ptr1) {
		tagParseAddEntry(peaTags, allocAddString(ptr1));
	}
	PERFINFO_AUTO_STOP();
}

// Optimized version of tagParse() that maintains the earray
// internally for speed.
void tagParseAllocedString(const char *pcTagString, const char ***peaTags)
{
	if (!stTagCache)
		stTagCache = stashTableCreateAddress( 32 );

	if (stashFindPointerConst(stTagCache, pcTagString, (const void**)peaTags)) {
		return;
	} else {
		char** tagList = NULL;
		tagParse(pcTagString, &tagList);
		stashAddPointer(stTagCache, pcTagString, tagList, false);
		*peaTags = tagList;
	}
}


// Return true if the match described by DETAILS are all present in EA-COMPLETE-TAGS.
bool tagMatchDetails(const ResourceSearchRequestTags* details, const char **eaCompleteTags)
{
	switch( details->type ) {
		xcase SEARCH_TAGS_NODE_TAG:
			return eaFind( &eaCompleteTags, allocAddString( details->strTag )) >= 0;

		xcase SEARCH_TAGS_NODE_AND: {
			int it;
			for( it = 0; it != eaSize( &details->eaChildren ); ++it ) {
				if( !tagMatchDetails( details->eaChildren[ it ], eaCompleteTags )) {
					return false;
				}
			}
			return true;
		}
		xcase SEARCH_TAGS_NODE_OR: {
			int it;
			for( it = 0; it != eaSize( &details->eaChildren ); ++it ) {
				if( tagMatchDetails( details->eaChildren[ it ], eaCompleteTags )) {
					return true;
				}
			}
			return false;
		}
	}

	return false;
}


// Returns true if all tags in the Test value are present in the Complete value.
bool tagMatchSubset(const char **eaTestTags, const char **eaCompleteTags)
{
	int i,j,non_negative_tags = 0;
	PERFINFO_AUTO_START_FUNC();

	for(i=eaSize(&eaTestTags)-1; i>=0; --i) {
		if (eaTestTags[i] && eaTestTags[i][0] != '!') {
			non_negative_tags++;
		}
	}

	if (non_negative_tags > eaSize(&eaCompleteTags)) {
		PERFINFO_AUTO_STOP();
		return false;
	}

	for(i=eaSize(&eaTestTags)-1; i>=0; --i) {
		if (!eaTestTags[i]) {
			continue;
		}
		if (eaTestTags[i][0] != '!') {
			// For positive flags need to find it
			for(j=eaSize(&eaCompleteTags)-1; j>=0; --j) {
				if (eaTestTags[i] == eaCompleteTags[j]) {
					break;
				}
			}
			if (j < 0) {
				PERFINFO_AUTO_STOP();
				return false;
			}
		} else {
			// For negative flags need to not find it
			for(j=eaSize(&eaCompleteTags)-1; j>=0; --j) {
				if (stricmp(&eaTestTags[i][1],eaCompleteTags[j]) == 0) {
					PERFINFO_AUTO_STOP();
					return false;
				}
			}
		}
	}

	PERFINFO_AUTO_STOP();
	return true;
}


static int tagNumTags(const char *pcTag)
{
	int count = 1;

	while (pcTag = strchr(pcTag,',')) {
		++count;
		++pcTag;
	}

	return count;
}


static bool tagCombinationsAddEntry(const char ***peaTagCombinations, const char *pcTag)
{
	const char *pcCurrentTag;
	int count, count2, cmp, i;

	count = tagNumTags(pcTag);

	for(i=0; i<eaSize(peaTagCombinations); ++i) {
		pcCurrentTag = (*peaTagCombinations)[i];
		count2 = tagNumTags(pcCurrentTag);
		if (count > count2) {
			continue;
		} else if (count < count2) {
			eaInsert(peaTagCombinations, pcTag, i);
			return true;
		}
		cmp = stricmp(pcCurrentTag, pcTag);
		if (cmp == 0) {
			return false;
		}
		if (cmp > 0) {
			eaInsert(peaTagCombinations, pcTag, i);
			return true;
		}
	}

	eaPush(peaTagCombinations, pcTag);
	return true;
}


// Adds valid combinations from the TagsToAdd into the Collected value
void tagAddCombinations(const char ***peaTags, const char **eaTagsToAdd)
{
	bool bHasNew = false;
	int i;

	// Scan all first level tags
	for(i=eaSize(&eaTagsToAdd)-1; i>=0; --i) {
		if (tagCombinationsAddEntry(peaTags, eaTagsToAdd[i])) {
			bHasNew = true;
		}
	}

	// If all the tags here were already present then no more work
	if (!bHasNew) {
		return;
	}
}


// Adds valid combinations from the TagString into the Collected value
void tagAddCombinationsFromString(const char ***peaTags, const char *pcTagString)
{
	char **eaTags = NULL;
	tagParse(pcTagString, &eaTags);
	tagAddCombinations(peaTags, eaTags);
	eaDestroy(&eaTags);
}

void tagGetCombinationsForDictionary(DictionaryHandleOrName dictNameOrHandle, const char ***peaTags)
{
	int i;
	ResourceDictionaryInfo *pDictInfo = resDictGetInfo(dictNameOrHandle);

	if (!pDictInfo)
		return;

	for (i = 0; i < eaSize(&pDictInfo->ppInfos); i++)
	{
		ResourceInfo *pInfo = pDictInfo->ppInfos[i];
		if (pInfo->resourceTags)
		{
			tagAddCombinationsFromString(peaTags, pInfo->resourceTags);
		}
	}

	// Add in complement tags to the peaTags list 
	{
		const char** complementTags = NULL;
		for( i = 0; i != eaSize(peaTags); ++i ) {
			char buffer[256];
			sprintf(buffer, "!%s", (*peaTags)[i]);
			eaPush(&complementTags, allocAddString(buffer));
		}

		eaPushEArray(peaTags, &complementTags);
		eaDestroy(&complementTags);
	}
}

static void eaDestroyPtr(void** ea)
{
	eaDestroy(&ea);
}

void tagCacheClear(void)
{
	if (stTagCache)
		stashTableClearEx(stTagCache, NULL, eaDestroyPtr);
}
