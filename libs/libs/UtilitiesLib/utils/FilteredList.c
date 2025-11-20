#include "filteredList.h"
#include "Expression.h"
#include "error.h"
#include "estring.h"
#include "ResourceInfo.h"
#include "StringCache.h"
#include "objPath.h"

AUTO_RUN_ANON(memBudgetAddMapping(__FILE__, BUDGET_EngineMisc););


#define FILTEREDLIST_FLAG_HYBRID (1 << 0)

AUTO_STRUCT;
typedef struct fakeObject
{
	int x;
} fakeObject;

//FIXME this LINKOVERRIDE stuff is specific to the way global objects are server monitored. If anyone wants to do
//lots of filtered list stuff for any other reason, this should not be here.
AUTO_STRUCT;
typedef struct ObjectWithLink
{
	char *pLink; AST(ESTRING, FORMATSTRING(HTML=1, HTML_LINKOVERRIDE=1, HTML_LINKOVERRIDE_SUFFIX=".Struct"))
} ObjectWithLink;


AUTO_STRUCT;
typedef struct FilteredListReturn
{
	char *pTitle; AST(ESTRING)
	int offset;					AST(FORMATSTRING(HTML_SVR_PARAM=1))
	int limit;					AST(FORMATSTRING(HTML_SVR_PARAM=1))
	int more;					AST(FORMATSTRING(HTML_SVR_PARAM=1))
	int count;					AST(FORMATSTRING(HTML_SVR_PARAM=1))
	//note that this is type will be overwritten with a real type. I'm just sticking another
	//struct that is AUTO_STRUCTed in the same file here so that structparser won't complain
	fakeObject **ppObjects; AST(FORMATSTRING(TEST_FILTER = "svr"))
} FilteredListReturn;

// Checks to see if an object path is NULL.
AUTO_EXPR_FUNC(util) ACMD_NAME(IsNull);
bool exprFuncIsNull(ExprContext* context, const char *path)
{
	ParseTable *tpi = exprContextGetParseTable(context);
	void *structptr;
	int column, index;

	bool didGetField = false;
	int key;

	if (!tpi) return false;

	structptr = exprContextGetUserPtr(context, tpi);
	if (!structptr) return false;

	objGetKeyInt(tpi, structptr, &key);

	didGetField = objPathResolveField(path, tpi, structptr,&tpi, &column, &structptr, &index, 0);
	if (didGetField)
	{
		void *nullstruct = StructCreateVoid(tpi);
		if (!TokenCompare(tpi, column, structptr, nullstruct, 0, 0)) didGetField = false;
		StructDestroyVoid(tpi, nullstruct);
	}
	
	return !didGetField;
}

#include "autogen/filteredlist_c_ast.c"

FilteredList *GetFilteredListOfObjects(char *pTitle, const char *pFilteringExpression, char **ppFieldsToReturn,
  ParseTable *pObjTPI, GenericIterator_Begin *pIterBeginCB, GenericIterator_GetNext *pIterGetNextCB, void *pUserData,
  char *pLinkString, char ***pppListOfNames, int limit, int offset, bool bForServerMonitoring)
{
	void *pCurObject;
	void *pCounter;
	ExprContext *pContext = exprContextCreate();
	Expression *pExpression = NULL;
	FilteredListReturn *pInnerList;
	int i;
	HybridObjHandle *pHybridHandle = NULL;
	ParseTable *pHybridTPI = NULL;
	char *pObjNameEString = NULL;
	char *pSortField = NULL;
	int sortIndex = -1;
	ParseTable *outTable = NULL;
	ObjectWithLink objWithLink = {0};
	int keyCol;
	static ExprFuncTable* funcTable;

	FilteredList *pOutList = calloc(sizeof(FilteredList), 1);
	
	//Make the context use default objectpath root object.
	exprContextSetUserPtrIsDefault(pContext, true);
	
	//load expression functions
	if (!funcTable)
	{
		funcTable = exprContextCreateFunctionTable("FilteredList");
		exprContextAddFuncsToTableByTag(funcTable, "util");
	}

	exprContextSetFuncTable(pContext, funcTable);


	pHybridHandle = HybridObjHandle_Create();
	HybridObjHandle_SetUnownedStructsCopiedWithKeys(pHybridHandle, true);

	if (pLinkString && pLinkString[0])
	{
		HybridObjHandle_AddObject(pHybridHandle, parse_ObjectWithLink, "objWithLink");
		HybridObjHandle_AddField(pHybridHandle, "objWithLink", ".Link");
	}

	if (!HybridObjHandle_AddObject(pHybridHandle, pObjTPI, "main"))
	{
		Errorf("Couldn't add hybrid object while making filtered list");
		free(pOutList);
		return NULL;
	}

	if (eaSize(&ppFieldsToReturn) == 0)
	{
		keyCol = ParserGetTableKeyColumn(pObjTPI);
		if (keyCol != -1)
		{	
			eaPush(&ppFieldsToReturn, strdup(pObjTPI[keyCol].name));
		}
	}

	//add fields to the tpi
	estrStackCreate(&pSortField);

	for (i=0; i < eaSize(&ppFieldsToReturn); i++)
	{
		char *field = ppFieldsToReturn[i];

		if (field[0] == '(')
		{	//if the field is wrapped in parens, it is the sort column.
			field++;
			estrSetSize(&pSortField, 0);
			estrConcat(&pSortField, field, (int)(strlen(field)-1)); //chop the closing ')' off.
			field = pSortField;
		}
		else if (i == 0 && !pSortField[0])
		{
			estrPrintf(&pSortField, "%s", field);
		}
		if (HybridObjHandle_AddField(pHybridHandle, "main", field))
		{
			int index = HybridObjHandle_IndexOfLastFieldAdded(pHybridHandle);
			if (index >= 0)
				sortIndex = index;
		}
		else
		{
			//handle error somehow?
		}
	}

	pHybridTPI = HybridObjHandle_GetTPI(pHybridHandle);
	pOutList->iFlags = FILTEREDLIST_FLAG_HYBRID;

	pInnerList = pOutList->pObject = StructCreate(parse_FilteredListReturn);
	estrCopy2(&pInnerList->pTitle, pTitle);
	pInnerList->limit = limit;
	pInnerList->offset = offset;
	pOutList->pTPI = calloc(sizeof(parse_FilteredListReturn), 1);
	memcpy(pOutList->pTPI, parse_FilteredListReturn, sizeof(parse_FilteredListReturn));


	pIterBeginCB(pUserData, &pCounter);

	estrStackCreate(&pObjNameEString);


	while ((pCurObject = pIterGetNextCB(pUserData, &pCounter, &pObjNameEString)))
	{
		MultiVal answer = {0};
		int iAnswer;

		//Set the default objectpath root
		exprContextSetUserPtr(pContext, pCurObject, pObjTPI);
		
		//I'm putting this back just in case people got married to the old way of filtering. This is somewhat redundant.
		exprContextSetPointerVar(pContext, "me", pCurObject, pObjTPI, false, true);

		if (!pExpression)
		{
			pExpression = exprCreate();
			exprGenerateFromString(pExpression, pContext, pFilteringExpression, NULL);
		}
		exprEvaluate(pExpression, pContext, &answer);
		

		//Expression will fail for all?
		if (exprContextCheckStaticError(pContext))
			break;

		if (answer.type == MULTI_INT)
			iAnswer = QuickGetInt(&answer);
		else
			iAnswer = 0;
		if (iAnswer)
		{
			if (bForServerMonitoring)
			{
				FixupStructLeafFirst(pObjTPI, pCurObject, FIXUPTYPE_ABOUT_TO_BE_SERVERMONITORED, NULL);
			}
			if (pHybridHandle)
			{
				void *pHybridObject;

				if (pppListOfNames)
				{
					eaPush(pppListOfNames, strdup(pObjNameEString));
				}
				
				if (pLinkString && pLinkString[0])
				{
					estrPrintf(&objWithLink.pLink, FORMAT_OK(pLinkString), pObjNameEString, pObjNameEString);
					pHybridObject = HybridObjHandle_ConstructObject_2(pHybridHandle, "main", pCurObject, "objWithLink", &objWithLink);
			
				}
				else
				{
					pHybridObject = HybridObjHandle_ConstructObject_1(pHybridHandle, "main", pCurObject);
				}
				if (pHybridObject)
				{
					eaPush(&pInnerList->ppObjects, pHybridObject);
				}
			}
			else
			{
				if (bForServerMonitoring)
				{
					FixupStructLeafFirst(pObjTPI, pCurObject, FIXUPTYPE_ABOUT_TO_BE_SERVERMONITORED, NULL);
				}
				eaPush(&pInnerList->ppObjects, pCurObject);
			}
		}
	}

	exprContextDestroy(pContext);
	exprDestroy(pExpression);

	FORALL_PARSETABLE(pOutList->pTPI, i)
	{
		if (strcmp(pOutList->pTPI[i].name, "Objects") == 0)
		{
			if (pHybridHandle)
			{
				pOutList->pTPI[i].subtable = outTable = pHybridTPI;
			}
			else
			{
				pOutList->pTPI[i].subtable = outTable = pObjTPI;
			}
		}
	}

	if (outTable)
	{
		int iSortColumn = -1;
		if (sortIndex >= 0)
		{
			iSortColumn = NUM_HEADER_TPI_COLUMNS + sortIndex;
		}
		else if (pSortField[0])
		{
			//Do the sorting
			FORALL_PARSETABLE(outTable, i)
			{
				if(!strcmpi(outTable[i].name, &pSortField[1]))
				{
					iSortColumn = i;
					break;
				}
			}
		}
		else
		{
			//TODO: Default sorting would go here.
		}
		if (iSortColumn >= 0)
			eaStableSortUsingColumnVoid(&pInnerList->ppObjects, outTable, iSortColumn);
	}
	estrDestroy(&pSortField);

	if (offset)
	{
		int j;
		if (offset > eaSize(&pInnerList->ppObjects))
		{
			offset = eaSize(&pInnerList->ppObjects);
		}
		for (j = 0; j < offset; j++)
		{
			if (pHybridHandle)
				HybridObjHandle_DestroyObject(pHybridHandle,pInnerList->ppObjects[j]);
		}
		eaRemoveRange(&pInnerList->ppObjects, 0, offset);
	}
	if (limit)
	{
		U32 size = eaSize(&pInnerList->ppObjects);
		int more = size - limit;
		if (more > 0) 
		{
			U32 j;
			pInnerList->more = more;
			for (j = limit; j < size; j++)
			{
				if (pHybridHandle)
					HybridObjHandle_DestroyObject(pHybridHandle,pInnerList->ppObjects[j]);
			}
			eaRemoveRange(&pInnerList->ppObjects, limit, more);
		}
	}
	pInnerList->count = eaSize(&pInnerList->ppObjects);

	if (pHybridHandle)
	{
		HybridObjHandle_Destroy(pHybridHandle);
	}


	estrDestroy(&pObjNameEString);
	StructDeInit(parse_ObjectWithLink, &objWithLink);

	return pOutList;
}

void DestroyFilteredList(FilteredList *pList)
{
	FilteredListReturn *pInnerList = pList->pObject;
	int i;

	//in the hybrid case, the entire object is "owned", so can all be destroyed at once
	if (pList->iFlags & FILTEREDLIST_FLAG_HYBRID)
	{
		ParseTable *pHybridTPI = NULL;

		FORALL_PARSETABLE(pList->pTPI, i)
		{
			if (strcmp(pList->pTPI[i].name, "Objects") == 0)
			{
				pHybridTPI = pList->pTPI[i].subtable;
				break;
			}
		}	
		assert(pHybridTPI);

		StructDestroyVoid(pList->pTPI, pList->pObject);

		HybridObjHandle_DestroyTPI(pHybridTPI);
	}
	else
	{
		//in the non-hybrid case, we have to first destroy the EArray to ensure that we don't try to
		//destroy internally unowned objects
		eaDestroy(&pInnerList->ppObjects);

		StructDestroyVoid(pList->pTPI, pList->pObject);

	}

	ParserClearTableInfo(pList->pTPI);
	free(pList->pTPI);
	free(pList);

}





void GenericIteratorBegin_RefDict(void *pUserData, void **ppCounter)
{
	RefDictIterator *pIterator = (RefDictIterator*)calloc(sizeof(RefDictIterator), 1);
	*ppCounter = pIterator;
	RefSystem_InitRefDictIterator(pUserData, pIterator);
}

void *GenericIteratorGetNext_RefDict(void *pUserData, void **ppCounter, char **ppObjName)
{
	RefDictIterator *pIterator = (RefDictIterator*)(*ppCounter);
	void *pReferent;
	char *pRefData;

	RefSystem_GetNextReferentAndRefDataFromIterator(pIterator, &pReferent, &pRefData);

	if (!pReferent)
	{
		free(pIterator);

		if (ppObjName)
		{
			estrDestroy(ppObjName);
		}
	}
	else
	{
		if (ppObjName)
		{
			estrCopy2(ppObjName, pRefData);
		}
	}

	return pReferent;
}


void GenericIteratorBegin_GlobObj(void *pUserData, void **ppCounter)
{
	ResourceIterator *pIterator = (ResourceIterator*)calloc(sizeof(ResourceIterator), 1);
	*ppCounter = pIterator;
	resInitIterator(pUserData, pIterator);
}

void *GenericIteratorGetNext_GlobObj(void *pUserData, void **ppCounter, char **ppObjName)
{
	ResourceIterator *pIterator = (ResourceIterator*)(*ppCounter);
	void *pObj = NULL;
	char *pObjName;

	if (resIteratorGetNext(pIterator, &pObjName, &pObj))
	{
		if (ppObjName)
		{
			estrCopy2(ppObjName, pObjName);
		}
	}
	else
	{
		resFreeIterator(pIterator);
		free(pIterator);

		if (ppObjName)
		{
			estrDestroy(ppObjName);
		}
	}

	return pObj;
}


