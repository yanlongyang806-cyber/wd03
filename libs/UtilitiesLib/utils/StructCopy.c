#include "StructCopy.h"
#include "StructInternals.h"
#include "ByteBlock_h_ast.h"
//#include "StructCopy_c_ast.h"
#include "ReferenceSystem.h"
#include "tokenstore.h"
#include "estring.h"
#include "WinInclude.h"
#include "objPath.h"
#include "timing.h"
#include "structinternals_h_Ast.h"

AUTO_RUN_ANON(memBudgetAddMapping(__FILE__, BUDGET_EngineMisc););

CRITICAL_SECTION sGlobalStructCopyStashTableSection = {0};

AUTO_RUN_FIRST;
void StructCopy_InitSystem(void)
{
	InitializeCriticalSection(&sGlobalStructCopyStashTableSection);
}

void StructCopyQueryResult_MarkBytes(bool bCopyBytes, int iByteOffset, int iNumBytes, void *pUserData)
{
	SCICreationInfo *pCreationInfo = (SCICreationInfo*)pUserData;

	ByteBlock *pBlock = calloc(sizeof(ByteBlock), 1);
	pBlock->iSize = iNumBytes;
	pBlock->iStartByteOffset = iByteOffset;

	if (bCopyBytes)
	{
		eaPush(&pCreationInfo->pRawBytesToCopy->onBlocks.ppBlocks, pBlock);
	}
	else
	{
		eaPush(&pCreationInfo->pRawBytesToCopy->offBlocks.ppBlocks, pBlock);
	}
}

void StructCopyQueryResult_MarkBit(bool bMarkToCopy, int iByteOffset, int iBitOffset, void *pUserData)
{
	SCICreationInfo *pCreationInfo = (SCICreationInfo*)pUserData;

	SingleBit *pBit = calloc(sizeof(SingleBit), 1);
	pBit->iByteOffset = iByteOffset;
	pBit->iBitOffset = iBitOffset;

	if (bMarkToCopy)
	{
		eaPush(&pCreationInfo->pRawBytesToCopy->ppOnBits, pBit);
	}
	else
	{
		eaPush(&pCreationInfo->pRawBytesToCopy->ppOffBits, pBit);
	}
}

static void StructCopy_AddCallback(SCICreationInfo *pCreationInfo, bool bIsEarrayCallback)
{
	CopyCallbackNeeded *pCBNeeded = calloc(sizeof(CopyCallbackNeeded), 1);

	//check to see if we already have a list of callbacks for the current tpi (which may be
	//an embedded struct inside the "main" tpi, or even an embed inside an embed or something)
	if (eaSize(&pCreationInfo->pSCI->ppCallbackLists) == 0 
		|| pCreationInfo->pSCI->ppCallbackLists[0]->iOffsetInParentStruct != pCreationInfo->iCurStartingOffsetIntoParentStruct 
		|| pCreationInfo->pSCI->ppCallbackLists[0]->pTPI != pCreationInfo->pCurTPI)
	{
		CopyCallbackNeededList *pList = calloc(sizeof(CopyCallbackNeededList), 1);
		pList->pTPI = pCreationInfo->pCurTPI;
		pList->iOffsetInParentStruct = pCreationInfo->iCurStartingOffsetIntoParentStruct;

		eaInsert(&pCreationInfo->pSCI->ppCallbackLists, pList, 0);
	}

	pCBNeeded->iColumn = pCreationInfo->iCurColumn;
	pCBNeeded->iIndex = pCreationInfo->iCurIndex;
	pCBNeeded->bIsEarrayCallback = bIsEarrayCallback;

	eaPush(&pCreationInfo->pSCI->ppCallbackLists[0]->ppCallbacks, pCBNeeded);
}
void StructCopyQueryResult_NeedCallback(void *pUserData)
{
	SCICreationInfo *pCreationInfo = (SCICreationInfo*)pUserData;
	StructCopy_AddCallback(pCreationInfo, false);
}



typedef struct
{
	ParseTable *pTPI;
	int iOffsetIntoParent;
} EmbeddedStructToProcess;

StructCopyInformation *MakeStructCopyInfoFromTpiAndFlags(ParseTable *pTPI, StructCopyFlags eFlags, 
	StructTypeField iOptionFlagsToMatch, StructTypeField iOptionFlagsToExclude)
{
	int i;
	EmbeddedStructToProcess **ppStructsToProcess = 0;
	EmbeddedStructToProcess *pFirstStruct = calloc(sizeof(EmbeddedStructToProcess), 1);
	StructCopyInformation *pCopyInformation = calloc(sizeof(StructCopyInformation), 1);
	SCICreationInfo *pCreationInfo = calloc(sizeof(SCICreationInfo), 1);

	TextParserAutoFixupCB *pFixupCB = ParserGetTableFixupFunc(pTPI);

	pFirstStruct->pTPI = pTPI;
	pFirstStruct->iOffsetIntoParent = 0;
	eaPush(&ppStructsToProcess, pFirstStruct);

	pCreationInfo->pSCI = pCopyInformation;


	pCreationInfo->pRawBytesToCopy = StructCreate(parse_RawByteCopyGroup);
	pCreationInfo->pRawBytesToCopy->iTotalSize = ParserGetTableSize(pTPI);
	pCreationInfo->pRawBytesToCopy->bDefaultToOn = !(eFlags & STRUCTCOPYFLAG_DONT_COPY_NO_ASTS);
	
	pCopyInformation->pParentTPI = pTPI;
	pCopyInformation->eFlags = eFlags;
	pCopyInformation->iOptionFlagsToMatch = iOptionFlagsToMatch;
	pCopyInformation->iOptionFlagsToExclude = iOptionFlagsToExclude;

	while (eaSize(&ppStructsToProcess))
	{
		int iCreationCommentOffset;

		pCreationInfo->pCurTPI = ppStructsToProcess[0]->pTPI;
		pCreationInfo->iCurStartingOffsetIntoParentStruct = ppStructsToProcess[0]->iOffsetIntoParent;
		free(ppStructsToProcess[0]);
		eaRemove(&ppStructsToProcess, 0);

		if ((iCreationCommentOffset = ParserGetCreationCommentOffset(pCreationInfo->pCurTPI)))
		{
			ByteBlock *pBlock = calloc(sizeof(ByteBlock), 1);
			pBlock->iSize = sizeof(void*); //the size of an earray in the struct itself is always a pointer
			pBlock->iStartByteOffset = (int)(pCreationInfo->iCurStartingOffsetIntoParentStruct + iCreationCommentOffset);
			eaPush(&pCreationInfo->pRawBytesToCopy->offBlocks.ppBlocks, pBlock);
		}
		
		
		FORALL_PARSETABLE(pCreationInfo->pCurTPI, i)
		{
			bool bFlaggedOut;

			if (pCreationInfo->pCurTPI[i].type & (TOK_REDUNDANTNAME | TOK_UNOWNED))
			{
				continue;
			}
			
			if (TOK_HAS_SUBTABLE(pCreationInfo->pCurTPI[i].type) && (eFlags & STRUCTCOPYFLAG_ALWAYS_RECURSE))
			{
				bFlaggedOut = false;
			}
			else
			{
				bFlaggedOut = !FlagsMatchAll(pCreationInfo->pCurTPI[i].type,iOptionFlagsToMatch) || !FlagsMatchNone(pCreationInfo->pCurTPI[i].type,iOptionFlagsToExclude);
			}


			//if this is an earray, we mask out its bytes and add a callback
			if (TokenStoreStorageTypeIsEArray(TokenStoreGetStorageType(pCreationInfo->pCurTPI[i].type)))
			{
				ByteBlock *pBlock = calloc(sizeof(ByteBlock), 1);
				pBlock->iSize = sizeof(void*); //the size of an earray in the struct itself is always a pointer
				pBlock->iStartByteOffset = (int)(pCreationInfo->iCurStartingOffsetIntoParentStruct + pCreationInfo->pCurTPI[i].storeoffset);
				eaPush(&pCreationInfo->pRawBytesToCopy->offBlocks.ppBlocks, pBlock);

				if (!bFlaggedOut)
				{
					pCreationInfo->iCurColumn = i;
					pCreationInfo->iCurIndex = 0;
					StructCopy_AddCallback(pCreationInfo, true);
				}
			}
			//special case for embedded structs, add them to our list of substructs to process
			else if (TOK_GET_TYPE(pCreationInfo->pCurTPI[i].type) == TOK_STRUCT_X && TokenStoreGetStorageType(pCreationInfo->pCurTPI[i].type) == TOK_STORAGE_DIRECT_SINGLE)
			{
				if (bFlaggedOut)
				{
					ByteBlock *pBlock = calloc(sizeof(ByteBlock), 1);
					pBlock->iSize = ParserGetTableSize(pCreationInfo->pCurTPI[i].subtable);
					pBlock->iStartByteOffset = (int)(pCreationInfo->iCurStartingOffsetIntoParentStruct + pCreationInfo->pCurTPI[i].storeoffset);
					eaPush(&pCreationInfo->pRawBytesToCopy->offBlocks.ppBlocks, pBlock);
				}
				else
				{
					EmbeddedStructToProcess *pEmbeddedStruct = calloc(sizeof(EmbeddedStructToProcess), 1);
					pEmbeddedStruct->pTPI = pCreationInfo->pCurTPI[i].subtable;
					pEmbeddedStruct->iOffsetIntoParent = (int)(pCreationInfo->iCurStartingOffsetIntoParentStruct + pCreationInfo->pCurTPI[i].storeoffset);

					eaPush(&ppStructsToProcess, pEmbeddedStruct);
				}
			}
			else
			{
				
				structCopyQuery_f queryFunc = TYPE_INFO(pCreationInfo->pCurTPI[i].type).queryCopyStruct;
				if (queryFunc)
				{
							//special case for fixed arrays... iterate over all their fields
					if ( TokenStoreStorageTypeIsFixedArray(TokenStoreGetStorageType(pCreationInfo->pCurTPI[i].type)))
					{
						int j;
						int iNumElems = pCreationInfo->pCurTPI[i].param;

						for (j=0; j < iNumElems; j++)
						{
							pCreationInfo->iCurColumn = i;
							pCreationInfo->iCurIndex = j;

							queryFunc(pCreationInfo->pCurTPI, i, j, (int)(pCreationInfo->iCurStartingOffsetIntoParentStruct + pCreationInfo->pCurTPI[i].storeoffset),
								COPYQUERYFLAG_IN_FIXED_ARRAY | (bFlaggedOut ? COPYQUERYFLAG_YOU_ARE_FLAGGED_OUT : 0), eFlags, iOptionFlagsToMatch, iOptionFlagsToExclude, pCreationInfo);


						}
					}
					else
					{
						pCreationInfo->iCurColumn = i;
						pCreationInfo->iCurIndex = 0;

						queryFunc(pCreationInfo->pCurTPI, i, 0, (int)(pCreationInfo->iCurStartingOffsetIntoParentStruct + pCreationInfo->pCurTPI[i].storeoffset),
							bFlaggedOut ? COPYQUERYFLAG_YOU_ARE_FLAGGED_OUT : 0, eFlags, iOptionFlagsToMatch, iOptionFlagsToExclude, pCreationInfo);
					}
				}
			}
		}
	}

	CompileByteCopyGroup(&pCopyInformation->bytesToCopy, pCreationInfo->pRawBytesToCopy);

	StructDestroy(parse_RawByteCopyGroup, pCreationInfo->pRawBytesToCopy);
	free(pCreationInfo);

	if (pFixupCB)
	{
		if (pFixupCB(NULL, FIXUPTYPE_POSTSC_INTERNAL_CHECKFOREXISTENCE, NULL) == PARSERESULT_SPECIAL_CALLBACK_RESULT)
		{
			pCopyInformation->bNeedPostCopyCB = true;
		}
		if (pFixupCB(NULL, FIXUPTYPE_PRESC_INTERNAL_CHECKFOREXISTENCE, NULL) == PARSERESULT_SPECIAL_CALLBACK_RESULT)
		{
			pCopyInformation->bNeedPreCopyCB = true;
		}
		if (pFixupCB(NULL, FIXUPTYPE_POSTSC_SRC_INTERNAL_CHECKFOREXISTENCE, NULL) == PARSERESULT_SPECIAL_CALLBACK_RESULT)
		{
			pCopyInformation->bNeedPostCopySrcCB = true;
		}
		if (pFixupCB(NULL, FIXUPTYPE_PRESC_DEST_INTERNAL_CHECKFOREXISTENCE, NULL) == PARSERESULT_SPECIAL_CALLBACK_RESULT)
		{
			pCopyInformation->bNeedPreCopyDestCB = true;
		}
	}

	return pCopyInformation;
}

void CopyStructWithSCI(StructCopyInformation *pSCI, void *pFrom, void *pTo)
{
	int i, j;

	assert(pFrom && pTo);

	if (pFrom == pTo)
	{
		return;
	}

	if (pSCI->bNeedPreCopyCB)
	{
		TextParserAutoFixupCB *pFixupCB = ParserGetTableFixupFunc(pSCI->pParentTPI);
		
		//if this ever crashes with NULL pfixupCB then someone did something insane like unset a fixup CB after one 
		//was set
		pFixupCB(pFrom, FIXUPTYPE_PRESC_INTERNAL_ACTUAL, NULL);
	}

	if (pSCI->bNeedPreCopyDestCB)
	{
		TextParserAutoFixupCB *pFixupCB = ParserGetTableFixupFunc(pSCI->pParentTPI);
		
		//if this ever crashes with NULL pfixupCB then someone did something insane like unset a fixup CB after one 
		//was set
		pFixupCB(pTo, FIXUPTYPE_PRESC_DEST_INTERNAL_ACTUAL, NULL);
	}


	CopyMemoryWithByteCopyGroup(pTo, pFrom, &pSCI->bytesToCopy);

	for (i=0; i < eaSize(&pSCI->ppCallbackLists); i++)
	{
		CopyCallbackNeededList *pList = pSCI->ppCallbackLists[i];

		for (j=0; j < eaSize(&pList->ppCallbacks); j++)
		{
			CopyCallbackNeeded *pCallback = pList->ppCallbacks[j];

			if (pCallback->bIsEarrayCallback)
			{
				newCopyEarray_f earrayCopyCB = TYPE_INFO(pList->pTPI[pCallback->iColumn].type).earrayCopy;
				assertmsgf(earrayCopyCB, "Trying to copy type %d which doesn't support earrays", TOK_GET_TYPE(pList->pTPI[pCallback->iColumn].type));

				earrayCopyCB(pList->pTPI, pCallback->iColumn, 
						((char*)pTo) + pList->iOffsetInParentStruct, ((char*)pFrom) + pList->iOffsetInParentStruct,
						pSCI->eFlags, pSCI->iOptionFlagsToMatch, pSCI->iOptionFlagsToExclude);
			}
			else
			{
				copyfield_f pOldCopyCB;
				newCopyField_f pNewCopyCB = TYPE_INFO(pList->pTPI[pCallback->iColumn].type).newFieldCopy;

				if (pNewCopyCB)
				{
					pNewCopyCB(pList->pTPI, pCallback->iColumn, 
						((char*)pTo) + pList->iOffsetInParentStruct, ((char*)pFrom) + pList->iOffsetInParentStruct,
						pCallback->iIndex, pSCI->eFlags, pSCI->iOptionFlagsToMatch, pSCI->iOptionFlagsToExclude);
				}
				else
				{
					pOldCopyCB = TYPE_INFO(pList->pTPI[pCallback->iColumn].type).copyfield_func;
					assertmsgf(pOldCopyCB, "TPI %s column %d can't be copied. Talk to Alex.", 
						ParserGetTableName(pList->pTPI), pCallback->iColumn); //a field that requests a callback must have either a new or old copyfield CB

			

					pOldCopyCB(pList->pTPI, &pList->pTPI[pCallback->iColumn], pCallback->iColumn, 
						((char*)pTo) + pList->iOffsetInParentStruct, ((char*)pFrom) + pList->iOffsetInParentStruct,
						pCallback->iIndex, NULL, NULL, pSCI->iOptionFlagsToMatch, pSCI->iOptionFlagsToExclude);
				}
			}
		}
	}

	if (pSCI->bNeedPostCopyCB)
	{
		TextParserAutoFixupCB *pFixupCB = ParserGetTableFixupFunc(pSCI->pParentTPI);
		
		//if this ever crashes with NULL pfixupCB then someone did something insane like unset a fixup CB after one 
		//was set
		pFixupCB(pTo, FIXUPTYPE_POSTSC_INTERNAL_ACTUAL, NULL);
	}

	if (pSCI->bNeedPostCopySrcCB)
	{
		TextParserAutoFixupCB *pFixupCB = ParserGetTableFixupFunc(pSCI->pParentTPI);
		
		//if this ever crashes with NULL pfixupCB then someone did something insane like unset a fixup CB after one 
		//was set
		pFixupCB(pFrom, FIXUPTYPE_POSTSC_SRC_INTERNAL_ACTUAL, NULL);
	}
}


void DestroyStructCopyInformation(StructCopyInformation *pSCI)
{
	int i;

	if (!pSCI)
	{
		return;
	}

	for (i=0; i < eaSize(&pSCI->ppCallbackLists); i++)
	{
		eaDestroyEx(&pSCI->ppCallbackLists[i]->ppCallbacks, NULL);
	}

	eaDestroyEx(&pSCI->ppCallbackLists, NULL);

	SAFE_FREE(pSCI->bytesToCopy.pByteBlocks);
	SAFE_FREE(pSCI->bytesToCopy.pMaskedBytes);
	
	free(pSCI);
}

typedef struct CopyFlagHashStruct
{
	StructTypeField iOptionFlagsToMatch;
	StructTypeField iOptionFlagsToExclude;
	StructCopyFlags eFlags;
} CopyFlagHashStruct;

StructCopyInformation *FindStructCopyInformationForCopy(ParseTable *pTPI, StructCopyFlags eFlags, StructTypeField iOptionFlagsToMatch, StructTypeField iOptionFlagsToExclude)
{
	CopyFlagHashStruct searchStruct;
	ParseTableInfo *pTPIInfo = ParserGetTableInfo(pTPI);
	StructCopyInformation *pRetVal;

	if (iOptionFlagsToMatch == 0 && iOptionFlagsToExclude == 0)
	{

		if (eFlags == 0)
		{
			if (!pTPIInfo->pCopyWithNOASTs)
			{
				EnterCriticalSection(&sGlobalStructCopyStashTableSection);

				if (!pTPIInfo->pCopyWithNOASTs)
				{
					pTPIInfo->pCopyWithNOASTs = MakeStructCopyInfoFromTpiAndFlags(pTPI, eFlags, iOptionFlagsToMatch, iOptionFlagsToExclude);
				}

				LeaveCriticalSection(&sGlobalStructCopyStashTableSection);

			}

			return pTPIInfo->pCopyWithNOASTs;
		}
		//note that this is an equality check, not an OR, because if there are flags OTHER than STRUCTCOPYFLAG_DONT_COPY_NO_ASTS
		//set then we need to get a different SCI
		else if (eFlags == STRUCTCOPYFLAG_DONT_COPY_NO_ASTS)
		{
			if (!pTPIInfo->pCopyWithoutNOASTs)
			{
				EnterCriticalSection(&sGlobalStructCopyStashTableSection);
				if (!pTPIInfo->pCopyWithoutNOASTs)
				{
					pTPIInfo->pCopyWithoutNOASTs = MakeStructCopyInfoFromTpiAndFlags(pTPI, eFlags, iOptionFlagsToMatch, iOptionFlagsToExclude);
				}
				LeaveCriticalSection(&sGlobalStructCopyStashTableSection);
			}

			return pTPIInfo->pCopyWithoutNOASTs;
		}
	}

	memset(&searchStruct, 0, sizeof(CopyFlagHashStruct));

	searchStruct.eFlags = eFlags;
	searchStruct.iOptionFlagsToMatch = iOptionFlagsToMatch;
	searchStruct.iOptionFlagsToExclude = iOptionFlagsToExclude;

	
	EnterCriticalSection(&sGlobalStructCopyStashTableSection);

	if (!pTPIInfo->copyWithFlagsTable)
	{
		pTPIInfo->copyWithFlagsTable = stashTableCreateFixedSize(4, sizeof(CopyFlagHashStruct));
	}

	if (!stashFindPointer(pTPIInfo->copyWithFlagsTable, &searchStruct, &pRetVal))
	{
		CopyFlagHashStruct *newSearchStruct = calloc(sizeof(CopyFlagHashStruct),1);
		memcpy(newSearchStruct, &searchStruct, sizeof(CopyFlagHashStruct));
		pRetVal = MakeStructCopyInfoFromTpiAndFlags(pTPI, eFlags, iOptionFlagsToMatch, iOptionFlagsToExclude);
		stashAddPointer(pTPIInfo->copyWithFlagsTable, newSearchStruct, pRetVal, false);
	}
	else
	{
		//in case of hash overlap
		assert(pRetVal->pParentTPI == pTPI);
	}

	LeaveCriticalSection(&sGlobalStructCopyStashTableSection);

	return pRetVal;
	
}

int StructCopyFieldVoid(ParseTable *pTPI, const void *source, void *dest, const char *field, StructCopyFlags eFlags, StructTypeField iOptionFlagsToMatch, StructTypeField iOptionFlagsToExclude)
{
	ParseTable *subtable = NULL;
	int column = -1;
	int index = -1;
	void *srcptr;
	void *fieldptr;
	char *resultString = NULL;
	char *createString = NULL;

	estrStackCreate(&resultString);
	estrStackCreate(&createString);

	if (!ParserResolvePath((const char *)field, pTPI, (void*)source, &subtable, &column, &srcptr, &index, &resultString, NULL, 0))
	{
		estrDestroy(&resultString);
		estrDestroy(&createString);
		return 0;
	}
	
	devassertmsg(
		ParserResolvePath(field, pTPI, dest, &subtable, &column, &fieldptr, &index, NULL, NULL, OBJPATHFLAG_CREATESTRUCTS),
		"could not resolve field path for copying"
		);

	//copy the field
	copyfield_autogen(subtable, &subtable[column], column, fieldptr, srcptr, index, NULL, NULL, 0, 0);

	estrDestroy(&resultString);
	estrDestroy(&createString);

	return 0;
}


//theoretically should return 0 on failure, but can not fail.
int StructCopyVoid(ParseTable *pTPI, const void *source, void *dest, StructCopyFlags eFlags, StructTypeField iOptionFlagsToMatch, StructTypeField iOptionFlagsToExclude)
{
	StructCopyInformation *pSCI;

	PERFINFO_RUN(
		ParseTableInfo* info = ParserGetTableInfo(pTPI);
		PERFINFO_AUTO_START_FUNC();
		PERFINFO_AUTO_START_STATIC(info->name, &info->piCopy, 1);
	)

	pSCI = FindStructCopyInformationForCopy(pTPI, eFlags, iOptionFlagsToMatch, iOptionFlagsToExclude);
	CopyStructWithSCI(pSCI, (void*)source, dest);

	PERFINFO_RUN(
		PERFINFO_AUTO_STOP();
		PERFINFO_AUTO_STOP();
	)

//turn this on to verify the result of each structcopy after it occurs
/*
	{
		int iResult = StructCompare(pTPI, source, dest, 0, iOptionFlagsToMatch, iOptionFlagsToExclude);

		if (iResult != 0)
		{
			Errorf("Caught bad structcopy!");
			while (1)
			{
				Sleep(1);
			}
		}
	}
*/

	return 1;
}


#if 0



typedef struct FooStruct FooStruct;

/*
AUTO_ENUM;
typedef enum enumPetType
{
	PETTYPE_DOG,
	PETTYPE_CAT,
	PETTYPE_IGUANA,
} enumPetType;
	

AUTO_STRUCT;
typedef struct Pet
{
	char petName[256]; AST(KEY)
	int iColor;
	float fTest; AST(NO_WRITE)
	enumPetType eType; AST( POLYPARENTTYPE )
} Pet;

AUTO_STRUCT;
typedef struct Dog
{
	Pet pet; AST( POLYCHILDTYPE( PETTYPE_DOG) )

	int iBreed;

	
} Dog;

AUTO_STRUCT;
typedef struct Cat
{
	Pet pet; AST( POLYCHILDTYPE( PETTYPE_CAT) )

	float fWhiskerLength;
	
} Cat;

AUTO_STRUCT;
typedef struct Iguana
{
	Pet pet; AST( POLYCHILDTYPE( PETTYPE_IGUANA ) )

	int iNumLittleIguanaBabies;
} Iguana;


AUTO_STRUCT;
typedef struct EmbeddedTestStruct
{
	U8 foo[3];
	U8 bar; NO_AST
	U8 wakka;
	REF_TO(FooStruct) hFoo; AST(NO_WRITE)
	U32 a : 1;
	U32 b : 1; NO_AST
	U32 c : 1; AST(NO_WRITE)
	U32 d : 1;
	U8 hoopa;
} EmbeddedTestStruct;

AUTO_STRUCT; 
typedef struct KeyedStruct
{
	char *pName; AST(KEY)
	int x; AST(NO_WRITE)
} KeyedStruct;

AUTO_STRUCT;
typedef struct CopyTestStruct
{
	int x;
	MultiVal mValSingle;
	int z;
	MultiVal mValFixed[5];
	int y;
	EmbeddedTestStruct **ppList;
	int *pIntArray;
	float *pFloatArray;
	char **ppStringArray; 
	KeyedStruct **ppKeyedArray;

	Pet **ppPets; 

	MultiVal **ppMultiVal_StarStar;

//	int x;
//	EmbeddedTestStruct embStruct1;
//	EmbeddedTestStruct embStruct2; AST(NO_WRITE)
//	float foo; NO_AST
//	U8 y; AST(NO_WRITE)
//	REF_TO(FooStruct) hFoo; AST(NO_WRITE)
//	U64 z;

} CopyTestStruct;


AUTO_STRUCT;
typedef struct EmbeddedStringCopyTest
{
	char embedded[256];
	char *pOpt;
	const char *pOptPool; AST(POOL_STRING)
	char *pOptEstr; AST(ESTRING)
} EmbeddedStringCopyTest;



AUTO_STRUCT;
typedef struct StringCopyTest
{
	char embedded[256];
	char *pOpt;
	EmbeddedStringCopyTest *pOptStruct1;
	EmbeddedStringCopyTest embeddedStruct1;
	const char *pOptPool; AST(POOL_STRING)
	char *pOptEstr; AST(ESTRING)
	EmbeddedStringCopyTest embeddedStruct2;
	EmbeddedStringCopyTest *pOptStruct2;

} StringCopyTest;
*/
#define PUSH_TEST(s, v) { pEmbedded = StructCreate(parse_EmbeddedTestStruct); pEmbedded->wakka = v; eaPush(&s.ppList, pEmbedded); }

#define PUSH_KEY(s, n) { pKeyed = StructCreate(parse_KeyedStruct); pKeyed->pName = strdup(n); eaPush(&s.ppKeyedArray, pKeyed); }

Pet *MakePet(enumPetType eType, int iOtherVal, char *pName)
{
	switch (eType)
	{
	case PETTYPE_DOG:
		{
			Dog *pDog = StructCreate(parse_Dog);
			sprintf(pDog->pet.petName, pName);
			pDog->pet.eType = PETTYPE_DOG;
			pDog->iBreed = iOtherVal;
			return (Pet*)pDog;
		}
	case PETTYPE_CAT:
		{
			Cat *pCat = StructCreate(parse_Cat);
			sprintf(pCat->pet.petName, pName);
			pCat->pet.eType = PETTYPE_CAT;
			pCat->fWhiskerLength = (float)iOtherVal;
			return (Pet*)pCat;
		}
	case PETTYPE_IGUANA:
		{
			Iguana *pIguana = StructCreate(parse_Iguana);
			sprintf(pIguana->pet.petName, pName);
			pIguana->pet.eType = PETTYPE_IGUANA;
			pIguana->iNumLittleIguanaBabies = iOtherVal;
			return (Pet*)pIguana;
		}
	}
	return NULL;
}
		

//AUTO_RUN_LATE;
void StructCopyTest(void)
{
/*	CopyTestStruct test1 = {1, {{4, 4, 4}, 5, 6, { NULL}, 1, 1, 1, 1, 7}, { {8, 8, 8}, 9, 10, {NULL}, 1, 1, 1, 1, 11}, 2, 3 };
	CopyTestStruct test2 = {0};*/

	U64 iStart, iEnd;
	int i;
	MultiVal *pTempMV;

	CopyTestStruct test1 = {1, NULL, 2};
	CopyTestStruct test2 = {0};

	EmbeddedTestStruct *pEmbedded;
	KeyedStruct *pKeyed;
	
	PUSH_TEST(test2, 1);
	PUSH_TEST(test2, 2);
	PUSH_TEST(test2, 3);
//	eaPush(&test2.ppList, NULL);
	PUSH_TEST(test2, 4);
	PUSH_TEST(test2, 5);

	PUSH_TEST(test1, 6);
//	eaPush(&test1.ppList, NULL);
	PUSH_TEST(test1, 7);

	ea32Push(&test1.pIntArray, 14);

	eaPush(&test1.ppStringArray, strdup("foo"));
//	eaPush(&test1.ppStringArray, NULL);
	eaPush(&test1.ppStringArray, strdup("bar"));
	eaPush(&test2.ppStringArray, strdup("haha"));

	eaIndexedEnable(&test1.ppKeyedArray, parse_KeyedStruct);
	eaIndexedEnable(&test2.ppKeyedArray, parse_KeyedStruct);

	PUSH_KEY(test1, "foo");
	PUSH_KEY(test1, "bar");
	PUSH_KEY(test1, "wakka");

	PUSH_KEY(test2, "a");
	PUSH_KEY(test2, "z");
	PUSH_KEY(test2, "bar");
	PUSH_KEY(test2, "wakka");


	eaIndexedEnable(&test1.ppPets, parse_Pet);
	eaIndexedEnable(&test2.ppPets, parse_Pet);

	eaPush(&test1.ppPets, MakePet(PETTYPE_CAT, 2, "frisky"));
	eaPush(&test1.ppPets, MakePet(PETTYPE_DOG, 3, "fido"));

	eaPush(&test2.ppPets, MakePet(PETTYPE_CAT, 4, "frisky"));
	eaPush(&test2.ppPets, MakePet(PETTYPE_IGUANA, 6, "bitey"));
	eaPush(&test2.ppPets, MakePet(PETTYPE_DOG, 5, "fido"));

	MultiValSetInt(&test1.mValSingle, 5);
	MultiValSetInt(&test2.mValSingle, 12);

	MultiValSetString(&test1.mValFixed[2], "test multival string");
	MultiValSetString(&test2.mValFixed[3], "other string");

	pTempMV = MultiValCreate();
	MultiValSetInt(pTempMV, 4);
	eaPush(&test1.ppMultiVal_StarStar, pTempMV);

//	eaPush(&test1.ppMultiVal_StarStar, NULL);

	pTempMV = MultiValCreate();
	MultiValSetString(pTempMV, "hello there");
	eaPush(&test1.ppMultiVal_StarStar, pTempMV);

	pTempMV = MultiValCreate();
	MultiValSetInt(pTempMV, 13);
	eaPush(&test2.ppMultiVal_StarStar, pTempMV);

	StructCopy(parse_CopyTestStruct, &test1, &test2, 0, 0, 0);
	assert(StructCompare(parse_CopyTestStruct, &test1, &test2, 0, 0, 0) == 0);

	iStart = timeMsecsSince2000();
	for (i=0; i < 10000; i++)
	{
		test1.x = i;
		StructCopy(parse_CopyTestStruct, &test1, &test2, 0, 0, TOK_NO_WRITE);
	}
	iEnd = timeMsecsSince2000();

	printf("New way took %d milliseconds\n", (int)(iEnd - iStart));

	iStart = timeMsecsSince2000();
	for (i=0; i < 10000; i++)
	{
		test1.x = i;
		StructCopyFields(parse_CopyTestStruct, &test1, &test2, 0, TOK_NO_WRITE);
	}
	iEnd = timeMsecsSince2000();

	printf("Old way took %d milliseconds\n", (int)(iEnd - iStart));


/*
	StringCopyTest stringTest1 = {0};
	StringCopyTest stringTest2 = {0};
	StructCopyInformation *pSCI;


	pSCI = FindStructCopyInformationForCopy(parse_CopyTestStruct, 0, 0, 0);
	pSCI = FindStructCopyInformationForCopy(parse_CopyTestStruct, 0, 0, 0);	CopyStructWithSCI(pSCI, &test1, &test2);
	StructDeInit(parse_CopyTestStruct, &test2);
	memset(&test2, 0, sizeof(CopyTestStruct));

	
	pSCI = FindStructCopyInformationForCopy(parse_CopyTestStruct, STRUCTCOPYFLAG_DONT_COPY_NO_ASTS, 0, 0);
	pSCI = FindStructCopyInformationForCopy(parse_CopyTestStruct, STRUCTCOPYFLAG_DONT_COPY_NO_ASTS, 0, 0);
	CopyStructWithSCI(pSCI, &test1, &test2);
	StructDeInit(parse_CopyTestStruct, &test2);
	memset(&test2, 0, sizeof(CopyTestStruct));

	pSCI = FindStructCopyInformationForCopy(parse_CopyTestStruct, 0, 0, TOK_NO_WRITE);
	pSCI = FindStructCopyInformationForCopy(parse_CopyTestStruct, 0, 0, TOK_NO_WRITE);
	CopyStructWithSCI(pSCI, &test1, &test2);
	StructDeInit(parse_CopyTestStruct, &test2);
	memset(&test2, 0, sizeof(CopyTestStruct));


	sprintf(stringTest1.embedded, "embedded");
	estrPrintf(&stringTest1.pOptEstr, "estring");
	stringTest1.pOpt = strdup("malloced");
	stringTest1.pOptPool = allocAddString("pooled");

	sprintf(stringTest1.embeddedStruct1.embedded, "emb1.embedded");
	estrPrintf(&stringTest1.embeddedStruct1.pOptEstr, "emb1.estring");
	stringTest1.embeddedStruct1.pOpt = strdup("emb1.malloced");
	stringTest1.embeddedStruct1.pOptPool = allocAddString("emb1.pooled");

	sprintf(stringTest1.embeddedStruct2.embedded, "emb2.embedded");
	estrPrintf(&stringTest1.embeddedStruct2.pOptEstr, "emb2.estring");
	stringTest1.embeddedStruct2.pOpt = strdup("emb2.malloced");
	stringTest1.embeddedStruct2.pOptPool = allocAddString("emb2.pooled");

	stringTest1.pOptStruct1 = StructCreate(parse_EmbeddedStringCopyTest);
	sprintf(stringTest1.pOptStruct1->embedded, "opt1.embedded");
	estrPrintf(&stringTest1.pOptStruct1->pOptEstr, "opt1.estring");
	stringTest1.pOptStruct1->pOpt = strdup("opt1.malloced");
	stringTest1.pOptStruct1->pOptPool = allocAddString("opt1.pooled");

	pSCI = FindStructCopyInformationForCopy(parse_StringCopyTest, 0, 0, 0);
	CopyStructWithSCI(pSCI, &stringTest1, &stringTest2);

	stringTest1.pOptStruct2 = stringTest1.pOptStruct1;
	stringTest1.pOptStruct1 = NULL;

	CopyStructWithSCI(pSCI, &stringTest1, &stringTest2);


	StructDeInit(parse_CopyTestStruct, &test2);
	memset(&test2, 0, sizeof(CopyTestStruct));*/


}
#endif
/*
AUTO_STRUCT;
typedef struct intArrayTestStruct
{
	int x;
	int y;
	int *pInts;
} intArrayTestStruct;

AUTO_RUN_LATE;
void intArrayTestCopy(void)
{
	intArrayTestStruct test1 = {0};
	intArrayTestStruct test2 = {0};

	StructCopy(parse_intArrayTestStruct, &test1, &test2, 0, 0, 0);
	assert(StructCompare(parse_intArrayTestStruct, &test1, &test2, 0, 0, 0) == 0);
	ea32Push(&test2.pInts, 5);
	StructCopy(parse_intArrayTestStruct, &test1, &test2, 0, 0, 0);
	assert(StructCompare(parse_intArrayTestStruct, &test1, &test2, 0, 0, 0) == 0);

}



#include "StructCopy_c_ast.c"
*/