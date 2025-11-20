#include "HybridObj.h"
#include "error.h"
#include "objpath.h"
#include "estring.h"
#include "tokenstore.h"
#include "StringCache.h"
#include "structInternals.h"

#include "autogen/HybridObj_c_ast.h"

AUTO_RUN_ANON(memBudgetAddMapping(__FILE__, BUDGET_EngineMisc););


typedef enum enumNameType
{
	NAME_NORMAL,
	NAME_UNDESCRIPTIVE,
	NAME_DONTUSE,
} enumNameType;


//object whose auto-generated TPI is used as a template for
//TPIs we make for hybrid objects (just to get the TOK_STARTs and such right
AUTO_STRUCT;
typedef struct HybridObjTestTemplate
{
	int foo;
} HybridObjTestTemplate;

typedef struct HybridObjSourceObject
{
	char *pObjName;
	ParseTable *pTPI;
} HybridObjSourceObject;

typedef struct HybridObjField
{
	char *pXPath;
	int iSourceObj;

	//calculated when the field is added
	ParseTable *pTPI;
	int iIndex;
	int iColumn;

	//true if this is in the top level TPI passed in, as opposed to somewhere in its tree
	bool bIsInTopLevelTPI;

	//this is a field which is being plucked out of an array into a non-array field
	bool bFlattenedArray;

	int iColumnInHybridTPI;
} HybridObjField;

typedef struct HybridObjHandle
{
	HybridObjSourceObject **ppSourceObjects;
	HybridObjField **ppFields;

	//stuff that is calculated internally for object creation
	size_t iObjSize;
	ParseTable *pTPI; //constructed when needed internally
	
	char *name;	//an eString that defines what the struct will be named in the TPI

	HybridObjHandle_TPICopyingCB *pTPICopyingCB;
	void *pTPICopyingUserData;

	bool bUnownedStructsCopiedWithKeys;
} HybridObjHandle;

bool HybridObjHandle_SetupInternalTPI(HybridObjHandle *pHandle);


HybridObjHandle *HybridObjHandle_Create(void)
{
	return HybridObjHandle_CreateNamed("HybridObject");
}

void HybridObjHandle_SetUnownedStructsCopiedWithKeys(HybridObjHandle *pHandle, bool bSet)
{
	pHandle->bUnownedStructsCopiedWithKeys = true;
}

HybridObjHandle *HybridObjHandle_CreateNamed(const char *name)
{
	HybridObjHandle *handle = (HybridObjHandle*)calloc(sizeof(HybridObjHandle), 1);
	handle->name = 0;
	estrCreate(&handle->name);
	estrPrintf(&handle->name, "%s", name);
	return handle;
}

void HybridObjHandle_SetTPICopyingCB(HybridObjHandle *pHandle, HybridObjHandle_TPICopyingCB *pCB, void *pUserData)
{
	pHandle->pTPICopyingCB = pCB;
	pHandle->pTPICopyingUserData = pUserData;
}


int HybridObjHandle_FindSourceObject(HybridObjHandle *pHandle, char *pObjName)
{
	int i;

	for (i=0; i < eaSize(&pHandle->ppSourceObjects); i++)
	{
		if (strcmp(pHandle->ppSourceObjects[i]->pObjName, pObjName) == 0)
		{
			return i;
		}
	}

	return -1;
}

void HybridObjHandle_DeleteTPI(ParseTable **ppParseTable)
{
	if (!ppParseTable || !(*ppParseTable))
	{
		return;
	}

	ParserClearTableInfo(*ppParseTable);

	free(*ppParseTable);
	*ppParseTable = NULL;
}

void HybridObjHandle_InternalReset(HybridObjHandle *pHandle)
{
	pHandle->iObjSize = 0;
	HybridObjHandle_DeleteTPI(&pHandle->pTPI);
}


bool HybridObjHandle_AddObject(HybridObjHandle *pHandle, ParseTable *pNewObjTPI, char *pObjName)
{
	HybridObjSourceObject *pSourceObject;

	if (HybridObjHandle_FindSourceObject(pHandle, pObjName) >= 0)
	{
		Errorf("Duplicate source objects named %s in HybridObj", pObjName);
		return false;
	}

	HybridObjHandle_InternalReset(pHandle);

	pSourceObject = calloc(sizeof(HybridObjSourceObject), 1);
	pSourceObject->pTPI = pNewObjTPI;
	pSourceObject->pObjName = strdup(pObjName);

	eaPush(&pHandle->ppSourceObjects, pSourceObject);

	return true;
}

bool HybridObjHandle_AddField(HybridObjHandle *pHandle, char *pObjName, char *pXPath)
{
	int iSourceIndex = HybridObjHandle_FindSourceObject(pHandle, pObjName);
	HybridObjField *pField;

	if (iSourceIndex < 0)
	{
		Errorf("Unknown source object named %s in HybridObj", pObjName);
		return false;
	}

	HybridObjHandle_InternalReset(pHandle);

	pField = calloc(sizeof(HybridObjField), 1);


	if (!objPathResolveField(pXPath, pHandle->ppSourceObjects[iSourceIndex]->pTPI, NULL, 
		&pField->pTPI, &pField->iColumn, NULL, &pField->iIndex, OBJPATHFLAG_TRAVERSEUNOWNED | OBJPATHFLAG_TOLERATEMISSPELLEDFIELDS))
	{
		free(pField);
		return false;
	}

	pField->bIsInTopLevelTPI = (pField->pTPI == pHandle->ppSourceObjects[iSourceIndex]->pTPI);

	pField->iSourceObj = iSourceIndex;
	pField->pXPath = strdup(pXPath);

	pField->iColumnInHybridTPI = eaSize(&pHandle->ppFields) + NUM_HEADER_TPI_COLUMNS;

	eaPush(&pHandle->ppFields, pField);

	return true;
}

int HybridObjHandle_IndexOfLastFieldAdded(HybridObjHandle *pHandle)
{
	return eaSize(&pHandle->ppFields) - 1;
}

void HybridObjHandle_Destroy(HybridObjHandle *pHandle)
{
	int i;

	for (i=0; i < eaSize(&pHandle->ppFields); i++)
	{
		free(pHandle->ppFields[i]->pXPath);
		free(pHandle->ppFields[i]);
	}

	for (i=0; i < eaSize(&pHandle->ppSourceObjects); i++)
	{
		free(pHandle->ppSourceObjects[i]->pObjName);
		free(pHandle->ppSourceObjects[i]);
	}

	HybridObjHandle_DeleteTPI(&pHandle->pTPI);

	estrDestroy(&pHandle->name);

	free(pHandle);
}


void HybridObjHandle_DestroyObject(HybridObjHandle *pHandle, void *structptr)
{
	StructDestroyVoid(pHandle->pTPI, structptr);
}

void *HybridObjHandle_ConstructObject(HybridObjHandle *pHandle, NameObjPair *pInObjects, int iNumInObjects)
{
	void *pObject;
	char *pDiffString = NULL;
	void **ppInObjects = NULL;
	int i;
	char *pFieldName = NULL;

	HybridObjHandle_SetupInternalTPI(pHandle);


	eaSetSize(&ppInObjects, eaSize(&pHandle->ppSourceObjects));

	for (i=0; i < eaSize(&ppInObjects); i++)
	{
		ppInObjects[i] = NULL;
	}

	for (i=0; i < iNumInObjects; i++)
	{
		int iIndex = HybridObjHandle_FindSourceObject(pHandle, pInObjects[i].pObjName);

		if (iIndex < 0)
		{
			return NULL;
		}

		ppInObjects[iIndex] = pInObjects[i].pObj;
	}

	estrStackCreate(&pDiffString);
	estrStackCreate(&pFieldName);

	for (i=0; i < eaSize(&pHandle->ppFields); i++)
	{
		if (ppInObjects[pHandle->ppFields[i]->iSourceObj])
		{
			ParseTable *pFoundTable;
			int iFoundColumn;
			void *pFoundObject;
			int iFoundIndex;

			if (objPathResolveField(pHandle->ppFields[i]->pXPath, pHandle->ppSourceObjects[pHandle->ppFields[i]->iSourceObj]->pTPI, 
				ppInObjects[pHandle->ppFields[i]->iSourceObj], &pFoundTable, &iFoundColumn, &pFoundObject, &iFoundIndex, OBJPATHFLAG_TOLERATEMISSPELLEDFIELDS))
			{
				U32 storage = TokenStoreGetStorageType(pHandle->pTPI[pHandle->ppFields[i]->iColumnInHybridTPI].type);
				estrPrintf(&pFieldName, ".%s", pHandle->pTPI[pHandle->ppFields[i]->iColumnInHybridTPI].name);
				if (TokenStoreStorageTypeIsAnArray(storage)) {
					//XXXXXX:When hybridobject tpi reinterprets an element of an array it barfs here because array element != single element.
				}
				if (iFoundIndex >= 0)
				{				
					FieldWriteTextDiff(&pDiffString, pFoundTable, iFoundColumn, NULL, pFoundObject, iFoundIndex, 
						pFieldName, 0, 0, pHandle->bUnownedStructsCopiedWithKeys ? TEXTDIFFFLAG_ONLYKEYSFROMUNOWNED : 0);
				}
				else
				{
					TokenWriteTextDiff(&pDiffString, pFoundTable, iFoundColumn, NULL, pFoundObject, 
						pFieldName, 0, 0, pHandle->bUnownedStructsCopiedWithKeys ? TEXTDIFFFLAG_ONLYKEYSFROMUNOWNED : 0);
				}
			}

		}
	}


	ParserSetTableInfo(pHandle->pTPI, (int)(pHandle->iObjSize), "HybridObj", NULL, NULL, SETTABLEINFO_TABLE_IS_TEMPORARY);

	pObject = StructCreateVoid(pHandle->pTPI);

	
	objPathParseAndApplyOperations(pHandle->pTPI, pObject, pDiffString);
	
	eaDestroy(&ppInObjects);
	estrDestroy(&pDiffString);
	estrDestroy(&pFieldName);
	return pObject;
}

void *HybridObjHandle_ConstructObject_1(HybridObjHandle *pHandle, char *pObjName1, void *pObj1)
{
	NameObjPair pair;
	void *pRetVal;

	pair.pObj = pObj1;
	pair.pObjName = pObjName1;

	pRetVal = HybridObjHandle_ConstructObject(pHandle, &pair, 1);

	return pRetVal;
}
void *HybridObjHandle_ConstructObject_2(HybridObjHandle *pHandle, char *pObjName1, void *pObj1, char *pObjName2, void *pObj2)
{
	NameObjPair pairs[2];
	void *pRetVal;

	pairs[0].pObj = pObj1;
	pairs[0].pObjName = pObjName1;

	pairs[1].pObj = pObj2;
	pairs[1].pObjName = pObjName2;

	pRetVal = HybridObjHandle_ConstructObject(pHandle, pairs, 2);

	return pRetVal;
}

void *HybridObjHandle_ConstructObject_3(HybridObjHandle *pHandle, char *pObjName1, void *pObj1, char *pObjName2, void *pObj2,  char *pObjName3, void *pObj3)
{
	NameObjPair pairs[3];
	void *pRetVal;

	pairs[0].pObj = pObj1;
	pairs[0].pObjName = pObjName1;

	pairs[1].pObj = pObj2;
	pairs[1].pObjName = pObjName2;

	pairs[2].pObj = pObj3;
	pairs[2].pObjName = pObjName3;

	pRetVal = HybridObjHandle_ConstructObject(pHandle, pairs, 3);

	return pRetVal;
}


/*takes a list of xpaths, with "segments" separated by .[]. Takes as many segments from each as necessary to make all
unique and strdups them, with punctuation replaced by underscores, into outuniquenames. Adds _1, _2, _3 etc if necessary
*/

//somewhat unncessary struct so that we don't have an earray of earrays;

AUTO_STRUCT;
typedef struct DividedPath
{
	char **ppParts; //strdup'd
} DividedPath;

DividedPath *GetDividedPath(char *pInPath)
{
	DividedPath *pOutPath = StructCreate(parse_DividedPath);
	char *pBegin = pInPath;
	bool inBrack = false;
	bool inQuote = false;
	while (1)
	{
		if (inBrack)
		{
			while (*pInPath) 
			{	
				if (!inQuote && *pInPath == ']') break;
				if (*pInPath == '"') inQuote = !inQuote;
				pInPath++;
			}
			inBrack = false;
		}
		else
		{
			while (*pInPath)
			{
				if (!inQuote && (*pInPath == '[' || *pInPath == ']' || *pInPath == '.')) break;
				if (*pInPath == '"') inQuote = !inQuote;
				pInPath++;
			}
		}

		if (pInPath - pBegin > 1)
		{
			char *pTemp = malloc(pInPath - pBegin + 1);
			int i;
			pTemp[pInPath - pBegin] = 0;
			memcpy(pTemp, pBegin, pInPath - pBegin);
			for (i = 0; i < pInPath - pBegin; i++) if (pTemp[i] == '.' || pTemp[i] == '"') pTemp[i] = '_';

			eaPush(&pOutPath->ppParts, pTemp);
		}
		pBegin = pInPath + 1;

		if (!(*pInPath))
		{
			return pOutPath;
		}
		
		if (*pInPath == '[') inBrack = true;

		pInPath++;
	}
}

static void TryLongerName(char **ppOutString /*NOT an ESTRING*/, DividedPath *pPath, int *piCurDepth, int iIndex)
{
	int i;
	char *pTempString = NULL;
	int iDepthFromPath;
	int iStackSize = eaSize(&pPath->ppParts);

	estrStackCreate(&pTempString);
	free(*ppOutString);
	(*piCurDepth)++;
	
	iDepthFromPath = MIN(*piCurDepth, iStackSize);

	for (i=iStackSize- iDepthFromPath; i < iStackSize; i++)
	{
		estrConcatf(&pTempString, "%s%s", i==iStackSize - iDepthFromPath ? "" : "_",
			pPath->ppParts[i]);
	}

	if (*piCurDepth == iStackSize + 1)
	{
		estrConcatf(&pTempString, "_%d", iIndex);
	}
	else if (*piCurDepth > iStackSize + 1)
	{
		estrConcatf(&pTempString, "_%d_%d", *piCurDepth - iStackSize - 1, iIndex);
	}

	*ppOutString = strdup(pTempString);
	estrDestroy(&pTempString);
}



static void MakeUniqueNames(char **ppInPaths, int **ppiNameTypes, char ***pppOutUniqueNames)
{
	int iSize = eaSize(&ppInPaths);
	int i, j;
	DividedPath **ppDividedPaths = NULL;
	int *piDepths = 0;
	bool bDone = false;

	eaSetSize(pppOutUniqueNames, iSize);
	ea32SetSize(&piDepths, iSize);

	for (i=0; i < iSize; i++)
	{
		eaPush(&ppDividedPaths, GetDividedPath(ppInPaths[i]));

		if (ppDividedPaths[i]->ppParts)
		{
			if ((*ppiNameTypes)[i] == NAME_DONTUSE && eaSize(&ppDividedPaths[i]->ppParts) > 1)
			{
				piDepths[i] = 2;
				(*pppOutUniqueNames)[i] = strdup(ppDividedPaths[i]->ppParts[eaSize(&ppDividedPaths[i]->ppParts) - 2]);
			}
			else
			{
				piDepths[i] = 1;
				(*pppOutUniqueNames)[i] = strdup(ppDividedPaths[i]->ppParts[eaSize(&ppDividedPaths[i]->ppParts) - 1]);
			}
		}
	}

	for (i=0; i < iSize; i++)
	{
		if ((*ppiNameTypes)[i] == NAME_UNDESCRIPTIVE)
		{
			TryLongerName((*pppOutUniqueNames)+i, ppDividedPaths[i], &piDepths[i], i);
		}
	}


	while (!bDone)
	{
		bDone = true;

		for (i=0; i < iSize-1; i++)
		{
			for (j=i+1; j < iSize; j++)
			{
				if (stricmp((*pppOutUniqueNames)[i], (*pppOutUniqueNames)[j]) == 0)
				{
					bDone = false;
					
					TryLongerName((*pppOutUniqueNames)+j, ppDividedPaths[j], &piDepths[j], j);
				}
			}

			//if we've found at least one group that we had to fixup, then we start the whole scan over
			if (bDone == false)
			{
				TryLongerName((*pppOutUniqueNames)+i, ppDividedPaths[i], &piDepths[i], i);
				break;
			}
		}
	}


	



	eaDestroyStruct(&ppDividedPaths, parse_DividedPath);
	ea32Destroy(&piDepths);
}


				



/*	


AUTO_STRUCT;
typedef struct hyb1
{
	int x;
	float f;
	char *pTestChar;
} hyb1;

AUTO_STRUCT;
typedef struct hyb2
{
	int x;
	int foo;
	int bar;
	hyb1 h1;
} hyb2;

AUTO_RUN;
void HybridObj_Test(void)
{
	hyb1 h1 = { 2, 3.5f, "wow" };
	hyb1 h1a = { 4, 5.5f, "crazywacky" };
	hyb2 h2 = { 6, 7, 8, { 9, 10.5f, "sexy" } };
	NameObjPair namedObjects[] = 
	{
		{ "hyb1", &h1},
		{ "hyb1a", &h1},
		{ "hyb2", &h2},
	};


	void *pHybObject;
	ParseTable *pHybridTPI;



	HybridObjHandle *pHandle = HybridObjHandle_Create();
	HybridObjHandle_AddObject(pHandle, parse_hyb1, "hyb1");
	HybridObjHandle_AddObject(pHandle, parse_hyb1, "hyb1a");
	HybridObjHandle_AddObject(pHandle, parse_hyb2, "hyb2");

	HybridObjHandle_AddField(pHandle, "hyb1", ".x");
	HybridObjHandle_AddField(pHandle, "hyb1", ".f");
	HybridObjHandle_AddField(pHandle, "hyb1a", ".x");
	HybridObjHandle_AddField(pHandle, "hyb1a", ".f");
	HybridObjHandle_AddField(pHandle, "hyb2", ".foo");
	HybridObjHandle_AddField(pHandle, "hyb2", ".h1.f");
	HybridObjHandle_AddField(pHandle, "hyb2", ".h1.x");
	HybridObjHandle_AddField(pHandle, "hyb2", ".x");


	pHybObject = HybridObjHandle_ConstructObject(pHandle, namedObjects, 3);
	pHybridTPI = HybridObjHandle_GetTPI(pHandle);

	HybridObjHandle_Destroy(pHandle);

	StructDestroyVoid(pHybridTPI, pHybObject);
	HybridObjHandle_DestroyTPI(pHybridTPI);

}

*/
#include "autogen/HybridObj_c_ast.c"






//must come after _ast.c file as it needs to get sizeof(parse_HybridObjTestTemplate)
bool HybridObjHandle_SetupInternalTPI(HybridObjHandle *pHandle)
{
	int i;
	int iNumFields;
	ParseTable *pCurField;

	char tempString[1024];
	char **ppNonUniqueNames = NULL;
	char **ppUniqueNames = NULL;

	//effectively an array of enums telling whether the field has HTML_UNDESCRIPTIVENAME or HTML_DONTUSENAME set, meaning
	//that its uniquename should always grab its parent name (ie, if you have myFields[EXPERIENCE].sum, you probably
	//want the field to be named "EXPERIENCE_sum" instead of "sum", whereas if you have myFields[EXPERIENCE].value you
	//just want EXPERIENCE)
	int *piNameTypes = NULL;

	if (pHandle->pTPI)
	{
		return true;
	}


	iNumFields = eaSize(&pHandle->ppFields);

	for (i=0; i < iNumFields; i++)
	{
		sprintf(tempString, "%s%s", pHandle->ppSourceObjects[pHandle->ppFields[i]->iSourceObj]->pObjName,
			pHandle->ppFields[i]->pXPath);

		eaPush(&ppNonUniqueNames, strdup(tempString));
 
		if (GetBoolFromTPIFormatString(pHandle->ppFields[i]->pTPI + pHandle->ppFields[i]->iColumn, "HTML_UNDESCRIPTIVENAME"))
		{
			ea32Push(&piNameTypes, NAME_UNDESCRIPTIVE);
		}
		else if (GetBoolFromTPIFormatString(pHandle->ppFields[i]->pTPI + pHandle->ppFields[i]->iColumn, "HTML_DONTUSENAME"))
		{
			ea32Push(&piNameTypes, NAME_DONTUSE);
		}
		else
		{
			ea32Push(&piNameTypes, NAME_NORMAL);
		}
	}

	MakeUniqueNames(ppNonUniqueNames, &piNameTypes, &ppUniqueNames);

	eaDestroyEx(&ppNonUniqueNames, NULL);
	ea32Destroy(&piNameTypes);



	//need 4 extra rows... PARSETABLE_INFO row, start, end, and NULL terminator
	pHandle->pTPI = calloc(sizeof(ParseTable) * (iNumFields + NUM_HEADER_TPI_COLUMNS + NUM_FOOTER_TPI_COLUMNS), 1);

	memcpy(pHandle->pTPI, parse_HybridObjTestTemplate, NUM_HEADER_TPI_COLUMNS * sizeof(ParseTable));
	memcpy(pHandle->pTPI + NUM_HEADER_TPI_COLUMNS + iNumFields, parse_HybridObjTestTemplate + (sizeof(parse_HybridObjTestTemplate) / sizeof(ParseTable) - NUM_FOOTER_TPI_COLUMNS), NUM_FOOTER_TPI_COLUMNS * sizeof(ParseTable));

	for (i=0; i < iNumFields; i++)
	{
		pCurField = pHandle->pTPI + i + NUM_HEADER_TPI_COLUMNS;

		memcpy(pCurField, pHandle->ppFields[i]->pTPI + pHandle->ppFields[i]->iColumn, sizeof(ParseTable));

		pCurField->name = allocAddString(ppUniqueNames[i]);

		pCurField->type &= ~(TOK_REDUNDANTNAME | TOK_KEY);

		pCurField->storeoffset = 0;

		if (FormatStringIsSet(pCurField))
		{
			SetFormatString(pCurField, GetRawFormatString(pCurField));
		}

		if (TokenStoreStorageTypeIsAnArray(TokenStoreGetStorageType(pCurField->type)) && pHandle->ppFields[i]->iIndex != -1)
		{
			//the source TPI is an array, and the destination TPI needs to NOT be an array.
			pCurField->type &= ~TOK_EARRAY;
			pCurField->type &= ~TOK_FIXED_ARRAY;

			pHandle->ppFields[i]->bFlattenedArray = true;


		}

		if (pHandle->pTPICopyingCB)
		{
			pHandle->pTPICopyingCB(pHandle->ppFields[i]->pTPI, pHandle->ppFields[i]->iColumn,
				pHandle->pTPI, i + NUM_HEADER_TPI_COLUMNS, pHandle->ppFields[i]->bIsInTopLevelTPI,
				pHandle->pTPICopyingUserData);
		}

		if (pHandle->bUnownedStructsCopiedWithKeys)
		{
			if (pCurField->type & TOK_UNOWNED)
			{
				pCurField->type &= ~TOK_UNOWNED;
			}
		}
	}

	pHandle->iObjSize = ParseInfoCalcOffsets(pHandle->pTPI, true);

	pHandle->pTPI[0].name = allocAddString(pHandle->name);
	pHandle->pTPI[0].type = (TOK_IGNORE | TOK_PARSETABLE_INFO) + (((U64)((eaSize(&pHandle->ppFields) + NUM_HEADER_TPI_COLUMNS + NUM_FOOTER_TPI_COLUMNS - 1))) << 8);
	pHandle->pTPI[0].storeoffset = pHandle->iObjSize;
	pHandle->pTPI[0].subtable = NULL;

	pHandle->pTPI[1].name = allocAddString("{");
	pHandle->pTPI[1].type = TOK_START;
	pHandle->pTPI[1].storeoffset = 0;

	pCurField = pHandle->pTPI + i + NUM_HEADER_TPI_COLUMNS;
	pCurField->name = allocAddString("}");
	pCurField->type = TOK_END;
	pCurField->storeoffset = 0;

	pCurField = pHandle->pTPI + (i+1) + NUM_HEADER_TPI_COLUMNS;
	pCurField->name = allocAddString("");
	pCurField->type = 0;
	pCurField->storeoffset = 0;

	eaDestroyEx(&ppUniqueNames, NULL);


	return true;
}


ParseTable *HybridObjHandle_GetTPI(HybridObjHandle *pHandle)
{
	ParseTable *pTPI;
	int i;


	HybridObjHandle_SetupInternalTPI(pHandle);

	pTPI = calloc(sizeof(ParseTable) * (eaSize(&pHandle->ppFields) + (NUM_HEADER_TPI_COLUMNS + NUM_FOOTER_TPI_COLUMNS)), 1);
	memcpy(pTPI, pHandle->pTPI, sizeof(ParseTable) * (eaSize(&pHandle->ppFields) + (NUM_HEADER_TPI_COLUMNS + NUM_FOOTER_TPI_COLUMNS)));

	FORALL_PARSETABLE(pTPI, i)
	{
		StructTokenType type = TOK_GET_TYPE(pTPI[i].type);
		if (type != TOK_START && type != TOK_END)
		{
			pTPI[i].name = allocAddString(pTPI[i].name);
		}
	}
	
	return pTPI;
}

void HybridObjHandle_DestroyTPI(ParseTable *pTPI)
{
	int i;

	FORALL_PARSETABLE(pTPI, i)
	{
		StructTokenType type = TOK_GET_TYPE(pTPI[i].type);

		if (FormatStringIsSet(&pTPI[i]))
		{
			FreeFormatString(&pTPI[i]);
		}
	}

	ParserClearTableInfo(pTPI);
	free(pTPI);
}
