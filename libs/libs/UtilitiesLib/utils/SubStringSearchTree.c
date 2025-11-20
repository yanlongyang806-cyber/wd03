#include "SubStringSearchTree.h"
#include "MemoryPool.h"
#include "estring.h"
#include "Earray.h"
#include "timing_profiler.h"

#define SSSTREE_ALPHABET_SIZE 36 //alphanum characters only

MP_DEFINE(SSSTreeElement);
MP_DEFINE(SSSTreeNode);

#define SSSALPHABET_SIZE 36
static char *sSSSAlphabet = "0123456789ABCDEFGHIJKLMNOPQRSTUVWXYZ";

//looking up 'B' returns 11, for instance. All others are -1.
static S8 sSSSTreeRevLookupTable[256];


bool SSSTree_FindPreciseElement_InternalizedString(SubStringSearchTree *pTree, const char *pInternalName, void *pData);


AUTO_RUN_EARLY;
void SSSTree_InitTables(void)
{
	int i;
	assert(SSSALPHABET_SIZE == strlen(sSSSAlphabet));
	for (i=0 ; i < 256; i++)
	{
		sSSSTreeRevLookupTable[i] = -1;
	}

	for (i=0; i < SSSALPHABET_SIZE; i++)
	{
		assert(toupper(sSSSAlphabet[i]) == sSSSAlphabet[i]);
		sSSSTreeRevLookupTable[sSSSAlphabet[i]] = i;
	}
}



//for each element we store, we keep a search number with it so that we never double-return it from the same search.
//(Otherwise if we add the string "dragondragon" and search for the substring "dragon" we'll return the same element
//twice)
typedef struct SSSTreeElement
{
	void *pValue;

	U32 iLastSearchID;
} SSSTreeElement;


typedef struct SSSTreeNode
{
	int iNumImmediateChildNodes; //doesn't count grandchildren, etc.
	struct SSSTreeNode *pChildren[SSSTREE_ALPHABET_SIZE];
	SSSTreeElement **ppSubStringElements;
	SSSTreeElement **ppFullStringElements;
} SSSTreeNode;

typedef struct SubStringSearchTree
{
	int iMinStringLength;
	SSSTreeNode *pRootNode;
	bool bStoreInts;
} SubStringSearchTree;

SubStringSearchTree *SSSTree_Create(int iMinStringLength)
{
	SubStringSearchTree *pRetVal = calloc(sizeof(SubStringSearchTree), 1);
	MP_CREATE(SSSTreeElement, 1024);
	MP_CREATE(SSSTreeNode, 1024);

	mpSetMode(MP_NAME(SSSTreeElement), ZERO_MEMORY_BIT);
	mpSetMode(MP_NAME(SSSTreeNode), ZERO_MEMORY_BIT);

	pRetVal->iMinStringLength = iMinStringLength;

	return pRetVal;
}

SubStringSearchTreeInt *SSSTreeInt_Create(int iMinStringLength)
{
	SubStringSearchTree *pRetVal = calloc(sizeof(SubStringSearchTree), 1);
	MP_CREATE(SSSTreeElement, 1024);
	MP_CREATE(SSSTreeNode, 1024);

	mpSetMode(MP_NAME(SSSTreeElement), ZERO_MEMORY_BIT);
	mpSetMode(MP_NAME(SSSTreeNode), ZERO_MEMORY_BIT);

	pRetVal->iMinStringLength = iMinStringLength;
	pRetVal->bStoreInts = true;

	return (SubStringSearchTreeInt*)pRetVal;
}

void SSSTree_InternalizeString_dbg(char **ppOutString, const char *pInString, const char *caller_fname, int line)
{
	char c;

	if( !pInString ) {
		*ppOutString = NULL;
		return;
	}
	
	while ((c = *pInString))
	{
		c = toupper(c);
		pInString++;

		if (c > 0 && sSSSTreeRevLookupTable[c] != -1)
		{
			estrConcatChar_dbg(ppOutString, c, 1, caller_fname, line);
		}
	}
}

void SSSTree_AddStringInternal(SSSTreeNode *pNode, char *pString, int iStrLen, SSSTreeElement *pElement, bool bIsFullString)
{
	SSSTreeNode *pChildNode;
	int iIndex;

	assert(iStrLen > 0);
	iIndex = sSSSTreeRevLookupTable[pString[0]];

	assert(iIndex != -1);
	
	pChildNode = pNode->pChildren[iIndex];

	if (!pChildNode)
	{
		pChildNode = pNode->pChildren[iIndex] = MP_ALLOC(SSSTreeNode);
		pNode->iNumImmediateChildNodes++;
	}

	if (iStrLen == 1)
	{
		if (bIsFullString)
		{
			eaPush(&pChildNode->ppFullStringElements, pElement);
		}
		else
		{
			eaPush(&pChildNode->ppSubStringElements, pElement);
		}
	}
	else
	{
		SSSTree_AddStringInternal(pChildNode, pString + 1, iStrLen - 1, pElement, bIsFullString);
	}
}


bool SSSTree_AddElement(SubStringSearchTree *pTree, const char *pName, void *pData)
{
	char *pInternalName = NULL;
	SSSTreeElement *pElement;
	int i;


	estrStackCreate(&pInternalName);
	SSSTree_InternalizeString(&pInternalName, pName);
	if ((int)estrLength(&pInternalName) < pTree->iMinStringLength)
	{
		estrDestroy(&pInternalName);
		return false;
	}

	if (!pTree->pRootNode)
	{
		pTree->pRootNode = MP_ALLOC(SSSTreeNode);
	}
	
	pElement = MP_ALLOC(SSSTreeElement);
	pElement->pValue = pData;

	for (i=0; i < (int)estrLength(&pInternalName) - pTree->iMinStringLength + 1; i++)
	{
		SSSTree_AddStringInternal(pTree->pRootNode, pInternalName + i, (int)estrLength(&pInternalName) - i, pElement, i == 0);
	}


	estrDestroy(&pInternalName);
	return true;
}




void SSSTree_TestDumpInternal(char **ppCurStr, SSSTreeNode *pNode)
{
	int i;

	for (i=0; i < eaSize(&pNode->ppFullStringElements); i++)
	{
		printf("%s: (FULL) %p\n", *ppCurStr, pNode->ppFullStringElements[i]->pValue);
	}

	for (i=0; i < eaSize(&pNode->ppSubStringElements); i++)
	{
		printf("%s: (SUB) %p\n", *ppCurStr, pNode->ppSubStringElements[i]->pValue);
	}

	for (i=0; i < SSSALPHABET_SIZE; i++)
	{
		if (pNode->pChildren[i])
		{
			estrConcatChar(ppCurStr, sSSSAlphabet[i]);
			SSSTree_TestDumpInternal(ppCurStr, pNode->pChildren[i]);
			estrRemove(ppCurStr, estrLength(ppCurStr) - 1, 1);
		}
	}
}

void SSSTree_TestDump(SubStringSearchTree *pTree)
{
	char *pTempStr = NULL;

	printf("\n\nTree state:\n");

	estrStackCreate(&pTempStr);

	if (pTree->pRootNode)
	{
		SSSTree_TestDumpInternal(&pTempStr, pTree->pRootNode);
	}
	else
	{
		printf("Tree is empty\n");
	}

	estrDestroy(&pTempStr);
}




void SSSTree_RemoveElementInternal(SSSTreeNode *pNode, char *pString, int iStrLen, void *pData, SSSTreeElement **ppOutFoundElement)
{
	*ppOutFoundElement = NULL;

	if (iStrLen == 0)
	{
		int i;

		for (i=0; i < eaSize(&pNode->ppFullStringElements); i++)
		{
			if (pNode->ppFullStringElements[i]->pValue == pData)
			{
				*ppOutFoundElement = pNode->ppFullStringElements[i];

				eaRemoveFast(&pNode->ppFullStringElements, i);
				break;
			}
		}

		for (i=0; i < eaSize(&pNode->ppSubStringElements); i++)
		{
			if (pNode->ppSubStringElements[i]->pValue == pData)
			{
				*ppOutFoundElement = pNode->ppSubStringElements[i];

				eaRemoveFast(&pNode->ppSubStringElements, i);
				break;
			}
		}
	}
	else
	{
		int iIndex = sSSSTreeRevLookupTable[pString[0]];
		SSSTreeNode *pChildNode;
		assert(iIndex != -1);
		pChildNode = pNode->pChildren[iIndex];
		if (!pChildNode)
		{
			return;
		}

		SSSTree_RemoveElementInternal(pChildNode, pString + 1, iStrLen - 1, pData, ppOutFoundElement);

		if (pChildNode->iNumImmediateChildNodes == 0 && eaSize(&pChildNode->ppSubStringElements) == 0 && eaSize(&pChildNode->ppFullStringElements) == 0)
		{
			MP_FREE(SSSTreeNode, pNode->pChildren[iIndex]);
			pNode->iNumImmediateChildNodes--;
		}
	}
}

bool SSSTree_RemoveElement(SubStringSearchTree *pTree, const char *pName, void *pData)
{
	bool bFound = false;
	char *pInternalName = NULL;
	SSSTreeElement *pElement = NULL;
	int i;

	if (!pTree->pRootNode)
	{
		return false;
	}

	PERFINFO_AUTO_START_FUNC();


	estrStackCreate(&pInternalName);
	SSSTree_InternalizeString(&pInternalName, pName);
	if ((int)estrLength(&pInternalName) < pTree->iMinStringLength)
	{
		estrDestroy(&pInternalName);
		PERFINFO_AUTO_STOP();
		return false;
	}

	if (!SSSTree_FindPreciseElement_InternalizedString(pTree, pInternalName, pData))
	{
		estrDestroy(&pInternalName);
		PERFINFO_AUTO_STOP();
		return false;
	}



	for (i=0; i < (int)estrLength(&pInternalName) - pTree->iMinStringLength + 1; i++)
	{
		SSSTreeElement *pTempElement;

		SSSTree_RemoveElementInternal(pTree->pRootNode, pInternalName + i, (int)estrLength(&pInternalName) - i, pData, &pTempElement);

		if (i == 0)
		{
			assert(pTempElement); 
			//this should always exist because we already tried to find the precise element, thus verifying that it exists

			pElement = pTempElement;
		}
		else
		{
			assert(pElement == pTempElement);
		}
	}

	if (pTree->pRootNode->iNumImmediateChildNodes == 0)
	{
		MP_FREE(SSSTreeNode, pTree->pRootNode);
	}
		
	estrDestroy(&pInternalName);

	PERFINFO_AUTO_STOP();


	return pElement ? true : false;
}

SSSTreeNode *FindPreciseNode(SubStringSearchTree *pTree, const char *pString)
{
	SSSTreeNode *pNode;
	if (!(pNode = pTree->pRootNode))
	{
		return NULL;
	}



	while (*pString)
	{
		int iIndex = sSSSTreeRevLookupTable[pString[0]];
		assert(iIndex != -1);
		pNode = pNode->pChildren[iIndex];
		if (!pNode)
		{
			return NULL;
		}

		pString++;
	}

	return pNode;
}

void SSSTree_FindElementsByPreciseName(SubStringSearchTree *pTree, void ***pppOutFoundElements, const char *pName)
{
	char *pInternalName = NULL;
	SSSTreeNode *pNode;
	int i;

	PERFINFO_AUTO_START_FUNC();

	estrStackCreate(&pInternalName);
	SSSTree_InternalizeString(&pInternalName, pName);
	if ((int)estrLength(&pInternalName) < pTree->iMinStringLength)
	{
		estrDestroy(&pInternalName);
		PERFINFO_AUTO_STOP();
		return;
	}

	pNode = FindPreciseNode(pTree, pInternalName);

	if (!pNode)
	{
		estrDestroy(&pInternalName);
		PERFINFO_AUTO_STOP();
		return;
	}

	for (i=0; i < eaSize(&pNode->ppFullStringElements); i++)
	{
		eaPush(pppOutFoundElements, pNode->ppFullStringElements[i]->pValue);
	}
		
	estrDestroy(&pInternalName);
	PERFINFO_AUTO_STOP();
	return;
}

bool SSSTree_FindPreciseElement(SubStringSearchTree *pTree, const char *pName, void *pData)
{
	SSSTreeNode *pNode;
	char *pInternalName = NULL;
	int i;
	PERFINFO_AUTO_START_FUNC();
	SSSTree_InternalizeString(&pInternalName, pName);
	if ((int)estrLength(&pInternalName) < pTree->iMinStringLength)
	{
		estrDestroy(&pInternalName);
		PERFINFO_AUTO_STOP();
		return false;
	}

	pNode = FindPreciseNode(pTree, pInternalName);
	estrDestroy(&pInternalName);

	if (!pNode)
	{
		PERFINFO_AUTO_STOP();
		return false;
	}

	for (i=0; i < eaSize(&pNode->ppFullStringElements); i++)
	{
		if (pNode->ppFullStringElements[i]->pValue == pData)
		{
			PERFINFO_AUTO_STOP();
			return true;
		}
	}

	PERFINFO_AUTO_STOP();
	return false;
}


bool SSSTree_FindPreciseElement_InternalizedString(SubStringSearchTree *pTree, const char *pInternalName, void *pData)
{
	SSSTreeNode *pNode;

	int i;
	PERFINFO_AUTO_START_FUNC();

	pNode = FindPreciseNode(pTree, pInternalName);

	if (!pNode)
	{
		PERFINFO_AUTO_STOP();
		return false;
	}

	for (i=0; i < eaSize(&pNode->ppFullStringElements); i++)
	{
		if (pNode->ppFullStringElements[i]->pValue == pData)
		{
			PERFINFO_AUTO_STOP();
			return true;
		}
	}

	PERFINFO_AUTO_STOP();
	return false;
}


void SSSTree_RecurseGetAllElements(SSSTreeNode *pNode, void ***pppOutFoundElements, U32 iSearchID)
{
	int i;
	for (i=0; i < eaSize(&pNode->ppFullStringElements); i++)
	{
		if (pNode->ppFullStringElements[i]->iLastSearchID != iSearchID)
		{
			eaPush(pppOutFoundElements, pNode->ppFullStringElements[i]->pValue);
			pNode->ppFullStringElements[i]->iLastSearchID = iSearchID;
		}

	}
	for (i=0; i < eaSize(&pNode->ppSubStringElements); i++)
	{
		if (pNode->ppSubStringElements[i]->iLastSearchID != iSearchID)
		{
			eaPush(pppOutFoundElements, pNode->ppSubStringElements[i]->pValue);
			pNode->ppSubStringElements[i]->iLastSearchID = iSearchID;
		}
	}
	for (i=0; i < SSSALPHABET_SIZE; i++)
	{
		if (pNode->pChildren[i])
		{
			SSSTree_RecurseGetAllElements(pNode->pChildren[i], pppOutFoundElements, iSearchID);
		}
	}

}

static U32 iNextSearchID = 1;


void SSSTree_FindElementsBySubString(SubStringSearchTree *pTree, void ***pppOutFoundElements, char *pSubString)
{
	char *pInternalName = NULL;
	SSSTreeNode *pNode;
	PERFINFO_AUTO_START_FUNC();
	iNextSearchID++;
	if (iNextSearchID == 0)
	{
		iNextSearchID++;
	}


	estrStackCreate(&pInternalName);
	SSSTree_InternalizeString(&pInternalName, pSubString);
	if ((int)estrLength(&pInternalName) < pTree->iMinStringLength)
	{
		estrDestroy(&pInternalName);
		PERFINFO_AUTO_STOP();
		return;
	}

	pNode = FindPreciseNode(pTree, pInternalName);
	estrDestroy(&pInternalName);

	if (!pNode)
	{
		PERFINFO_AUTO_STOP();
		return;
	}

	SSSTree_RecurseGetAllElements(pNode, pppOutFoundElements, iNextSearchID);
	PERFINFO_AUTO_STOP();
}

#define SSSTREEFLAGS_DONTRECURSE ( 1 << 0 )
#define SSSTREEFLAGS_FULLSTRINGSONLY ( 1 << 1 )


bool SSSTree_RecurseGetAllElementsWithRestrictions(SSSTreeNode *pNode, void ***pppOutFoundElements, U32 iSearchID,
	int iMaxToReturn, SSSTreeCheckFunction *pCheckFunction, void *pUserData, U32 iFlags)
{
	int i;
	for (i=0; i < eaSize(&pNode->ppFullStringElements); i++)
	{
		if (pNode->ppFullStringElements[i]->iLastSearchID != iSearchID)
		{
			if (!pCheckFunction || pCheckFunction(pNode->ppFullStringElements[i]->pValue, pUserData))
			{
				if (iMaxToReturn && eaSize(pppOutFoundElements) == iMaxToReturn)
				{
					return true;
				}

				eaPush(pppOutFoundElements, pNode->ppFullStringElements[i]->pValue);
				pNode->ppFullStringElements[i]->iLastSearchID = iSearchID;
			}
		}

	}

	if (!(iFlags & SSSTREEFLAGS_FULLSTRINGSONLY))
	{
		for (i=0; i < eaSize(&pNode->ppSubStringElements); i++)
		{
			if (pNode->ppSubStringElements[i]->iLastSearchID != iSearchID)
			{
				if (!pCheckFunction || pCheckFunction(pNode->ppSubStringElements[i]->pValue, pUserData))
				{
					if (iMaxToReturn && eaSize(pppOutFoundElements) == iMaxToReturn)
					{
						return true;
					}

					eaPush(pppOutFoundElements, pNode->ppSubStringElements[i]->pValue);
					pNode->ppSubStringElements[i]->iLastSearchID = iSearchID;
				}		
			}
		}
	}

	if (!(iFlags & SSSTREEFLAGS_DONTRECURSE))
	{
		for (i=0; i < SSSALPHABET_SIZE; i++)
		{
			if (pNode->pChildren[i])
			{
				if (SSSTree_RecurseGetAllElementsWithRestrictions(pNode->pChildren[i], pppOutFoundElements, iSearchID,
					iMaxToReturn, pCheckFunction, pUserData, iFlags))
				{
					return true;
				}
			}
		}
	}

	return false;
}



bool SSSTree_FindElementsWithRestrictions(SubStringSearchTree *pTree, SSSTreeSearchType eSearchType,
	const char *pSubString, void ***pppOutFoundElements, int iMaxToReturn,
	SSSTreeCheckFunction *pCheckFunction, void *pUserData)
{	

	char *pInternalName = NULL;
	SSSTreeNode *pNode;
	U32 iFlags = 0;
	bool bRetVal;

	PERFINFO_AUTO_START_FUNC();

	iNextSearchID++;
	if (iNextSearchID == 0)
	{
		iNextSearchID++;
	}

	estrStackCreate(&pInternalName);
	SSSTree_InternalizeString(&pInternalName, pSubString);
	if ((int)estrLength(&pInternalName) < pTree->iMinStringLength)
	{
		estrDestroy(&pInternalName);
		PERFINFO_AUTO_STOP();
		return false;
	}

	pNode = FindPreciseNode(pTree, pInternalName);
	estrDestroy(&pInternalName);

	if (!pNode)
	{
		PERFINFO_AUTO_STOP();
		return false;
	}

	switch (eSearchType)
	{
	xcase SSSTREE_SEARCH_PRECISE_STRING:
		iFlags = SSSTREEFLAGS_FULLSTRINGSONLY | SSSTREEFLAGS_DONTRECURSE;
	xcase SSSTREE_SEARCH_PREFIXES:
		iFlags = SSSTREEFLAGS_FULLSTRINGSONLY;
	xcase SSSTREE_SEARCH_SUFFIXES:
		iFlags = SSSTREEFLAGS_DONTRECURSE;
	}

	bRetVal = SSSTree_RecurseGetAllElementsWithRestrictions(pNode, pppOutFoundElements, iNextSearchID, iMaxToReturn,
		pCheckFunction, pUserData, iFlags);
	PERFINFO_AUTO_STOP();
	return bRetVal;
}



bool SSSTreeInt_RecurseGetAllElementsWithRestrictions(SSSTreeNode *pNode, U32 **ppOutFoundElements, U32 iSearchID,
	int iMaxToReturn, SSSTreeIntCheckFunction *pCheckFunction, void *pUserData, U32 iFlags)
{
	int i;
	for (i=0; i < eaSize(&pNode->ppFullStringElements); i++)
	{
		if (pNode->ppFullStringElements[i]->iLastSearchID != iSearchID)
		{
			if (!pCheckFunction || pCheckFunction((intptr_t)(pNode->ppFullStringElements[i]->pValue), pUserData))
			{
				if (iMaxToReturn && ea32Size(ppOutFoundElements) == iMaxToReturn)
				{
					return true;
				}

				ea32Push(ppOutFoundElements, (intptr_t)(pNode->ppFullStringElements[i]->pValue));
				pNode->ppFullStringElements[i]->iLastSearchID = iSearchID;
			}
		}

	}

	if (!(iFlags & SSSTREEFLAGS_FULLSTRINGSONLY))
	{
		for (i=0; i < eaSize(&pNode->ppSubStringElements); i++)
		{
			if (pNode->ppSubStringElements[i]->iLastSearchID != iSearchID)
			{
				if (!pCheckFunction || pCheckFunction((intptr_t)(pNode->ppSubStringElements[i]->pValue), pUserData))
				{
					if (iMaxToReturn && ea32Size(ppOutFoundElements) == iMaxToReturn)
					{
						return true;
					}

					ea32Push(ppOutFoundElements, (intptr_t)(pNode->ppSubStringElements[i]->pValue));
					pNode->ppSubStringElements[i]->iLastSearchID = iSearchID;
				}		
			}
		}
	}

	if (!(iFlags & SSSTREEFLAGS_DONTRECURSE))
	{
		for (i=0; i < SSSALPHABET_SIZE; i++)
		{
			if (pNode->pChildren[i])
			{
				if (SSSTreeInt_RecurseGetAllElementsWithRestrictions(pNode->pChildren[i], ppOutFoundElements, iSearchID,
					iMaxToReturn, pCheckFunction, pUserData, iFlags))
				{
					return true;
				}
			}
		}
	}

	return false;
}





bool SSSTreeInt_FindElementsWithRestrictions(SubStringSearchTreeInt *pTree, SSSTreeSearchType eSearchType, const char *pSubString, U32 **pppOutFoundElements, int iMaxToReturn,
	SSSTreeIntCheckFunction *pCheckFunction, void *pUserData)
{	

	char *pInternalName = NULL;
	SSSTreeNode *pNode;
	U32 iFlags = 0;
	bool bRetVal;

	PERFINFO_AUTO_START_FUNC();

	iNextSearchID++;
	if (iNextSearchID == 0)
	{
		iNextSearchID++;
	}

	estrStackCreate(&pInternalName);
	SSSTree_InternalizeString(&pInternalName, pSubString);
	if ((int)estrLength(&pInternalName) < ((SubStringSearchTree*)pTree)->iMinStringLength)
	{
		estrDestroy(&pInternalName);
		PERFINFO_AUTO_STOP();
		return false;
	}

	pNode = FindPreciseNode((SubStringSearchTree*)pTree, pInternalName);
	estrDestroy(&pInternalName);

	if (!pNode)
	{
		PERFINFO_AUTO_STOP();
		return false;
	}

	switch (eSearchType)
	{
	xcase SSSTREE_SEARCH_PRECISE_STRING:
		iFlags = SSSTREEFLAGS_FULLSTRINGSONLY | SSSTREEFLAGS_DONTRECURSE;
	xcase SSSTREE_SEARCH_PREFIXES:
		iFlags = SSSTREEFLAGS_FULLSTRINGSONLY;
	xcase SSSTREE_SEARCH_SUFFIXES:
		iFlags = SSSTREEFLAGS_DONTRECURSE;
	}

	bRetVal = SSSTreeInt_RecurseGetAllElementsWithRestrictions(pNode, pppOutFoundElements, iNextSearchID, iMaxToReturn,
		pCheckFunction, pUserData, iFlags);
	PERFINFO_AUTO_STOP();
	return bRetVal;
}




/**
#include "memreport.h"
#include "rand.h"
AUTO_RUN;
void SSSMemTest(void)
{
	SubStringSearchTree *pTree = SSSTree_Create(3);

	char randomString[40];
	int i;
	char **ppFoundStrings = NULL;



	for (i=10000; i < 100000; i++)
	{
		char *pDupString;
		
		sprintf(randomString, "%d", i);

		pDupString = strdup(randomString);

		SSSTree_AddElement(pTree, pDupString, pDupString);
	}

//	SSSTree_FindElementsBySubString(pTree, &ppFoundStrings, "12345");

	SSSTree_FindElementsWithRestrictions(pTree, SSSTREE_SEARCH_SUBSTRINGS, "123", &ppFoundStrings, 0, NULL, NULL);

	printf("SUBSTRINGS found %d strings\n", eaSize(&ppFoundStrings));
	for (i=0; i < eaSize(&ppFoundStrings); i++)
	{
		printf("%s\n", ppFoundStrings[i]);
	}
	eaDestroy(&ppFoundStrings);

	SSSTree_FindElementsWithRestrictions(pTree, SSSTREE_SEARCH_PREFIXES, "123", &ppFoundStrings, 0, NULL, NULL);

	printf("PREFIXES found %d strings\n", eaSize(&ppFoundStrings));
	for (i=0; i < eaSize(&ppFoundStrings); i++)
	{
		printf("%s\n", ppFoundStrings[i]);
	}
	eaDestroy(&ppFoundStrings);

	SSSTree_FindElementsWithRestrictions(pTree, SSSTREE_SEARCH_SUFFIXES, "123", &ppFoundStrings, 0, NULL, NULL);

	printf("SUFFIXES found %d strings\n", eaSize(&ppFoundStrings));
	for (i=0; i < eaSize(&ppFoundStrings); i++)
	{
		printf("%s\n", ppFoundStrings[i]);
	}
	eaDestroy(&ppFoundStrings);

}


*/



/*
AUTO_RUN;
void SSSTreeTest(void)
{
	SubStringSearchTree *pTree = SSSTree_Create(5);
	bool bFound = false;
	void **ppFound = NULL;

	SSSTree_AddElement(pTree, "Happy", (void*)0x1);
	
	bFound = SSSTree_RemoveElement(pTree, "Happy", (void*)0x2);
	bFound = SSSTree_RemoveElement(pTree, "Shappy", (void*)0x1);
	bFound = SSSTree_RemoveElement(pTree, "appy", (void*)0x1);
	bFound = SSSTree_RemoveElement(pTree, "Happy", (void*)0x1);
	bFound = SSSTree_RemoveElement(pTree, "Happy", (void*)0x1);

	
	SSSTree_TestDump(pTree);

	eaDestroy(&ppFound);
	SSSTree_FindElementsBySubString(pTree, &ppFound, "Dumbdumb");
	
	eaDestroy(&ppFound);
	SSSTree_FindElementsBySubString(pTree, &ppFound, "Happy");



	SSSTree_AddElement(pTree, "HappyJoyJoy", (void*)0x2);
	SSSTree_TestDump(pTree);
	eaDestroy(&ppFound);
	SSSTree_FindElementsBySubString(pTree, &ppFound, "Happy");


	SSSTree_AddElement(pTree, "Very very very happy", (void*)0x3);
	SSSTree_TestDump(pTree);

	eaDestroy(&ppFound);
	SSSTree_FindElementsBySubString(pTree, &ppFound, "Happy");

	bFound = SSSTree_RemoveElement(pTree, "HappyJoyJoy", (void*)0x2);
	SSSTree_TestDump(pTree);

	bFound = SSSTree_RemoveElement(pTree, "Very very very happy", (void*)0x3);
	SSSTree_TestDump(pTree);

	bFound = SSSTree_RemoveElement(pTree, "Happy", (void*)0x1);
	SSSTree_TestDump(pTree);

	SSSTree_AddElement(pTree, "Happy", (void*)0x1);
	SSSTree_AddElement(pTree, "Hap py", (void*)0x2);
	SSSTree_AddElement(pTree, "Ha    ppy", (void*)0x3);
	SSSTree_AddElement(pTree, "happy", (void*)0x4);
	SSSTree_AddElement(pTree, "happyJoy", (void*)0x5);
	SSSTree_AddElement(pTree, "JoyHappy", (void*)0x6);

	eaDestroy(&ppFound);
	SSSTree_FindElementsByPreciseName(pTree, &ppFound, "dumbdumb");
	eaDestroy(&ppFound);
	SSSTree_FindElementsByPreciseName(pTree, &ppFound, "happy");
	eaDestroy(&ppFound);
	SSSTree_RemoveElement(pTree, "happy", (void*)0x2);
	eaDestroy(&ppFound);
	SSSTree_FindElementsByPreciseName(pTree, &ppFound, "happy");

}
*/