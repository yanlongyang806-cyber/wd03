
#include "dynBitField.h"

#include "fileutil.h"
#include "FolderCache.h"
#include "StringCache.h"
#include "timing.h"
#include "qsortG.h"
#include "referencesystem.h"

#include "dynSeqData.h"
#include "dynBitField_h_ast.h"
#include "dynBitField_c_ast.h"

AUTO_RUN_ANON(memBudgetAddMapping(__FILE__, BUDGET_Animation););

DictionaryHandle hIRQGroupDict;


AUTO_STRUCT AST_STARTTOK("") AST_ENDTOK("\n");
typedef struct DynBitString 
{
	const char*		pcBitName; AST(STRUCTPARAM POOL_STRING)
} DynBitString;

AUTO_STRUCT AST_STARTTOK("") AST_ENDTOK("\n");
typedef struct DynBitGroup
{
	const char*		pcBitGroupName; AST(STRUCTPARAM POOL_STRING)
	const char**	eaBits; AST(STRUCTPARAM POOL_STRING)
} DynBitGroup;



AUTO_STRUCT;
typedef struct DynBitList
{
	StashTable		stBitNumbers; NO_AST
	StashTable		stBitGroups; NO_AST
	DynBitString**	bits[iNumDynBitTypes];AST(INDEX(eDynBitType_Mode, ModeBit) INDEX(eDynBitType_Detail, DetailBit))
	DynBitGroup**	bitGroup;
	DynBitFieldStatic		criticalBits; AST(NAME(CriticalBit))
} DynBitList;

DynBitList bitList;

AUTO_RUN;
void initDynBitListTPI(void)
{
	ParserSetTableInfoRecurse(parse_DynBitList, sizeof(DynBitList), "DynBitList", NULL, __FILE__, NULL, SETTABLEINFO_NAME_STATIC | SETTABLEINFO_ALLOW_CRC_CACHING);
}

AUTO_RUN;
void registerIRQGroupDictionary(void)
{
	hIRQGroupDict = RefSystem_RegisterSelfDefiningDictionary("IRQGroup", false, parse_IRQGroup, true, true, NULL);
}

// Returns true if found
__forceinline static DynBit dynBitFromNameInline(const char* pcBitName)
{
	U32 uiResult;
	if (stashFindInt(bitList.stBitNumbers, pcBitName, &uiResult))
	{
		return (DynBit)uiResult;
	}
	return (DynBit)0;
}

DynBit dynBitFromName(const char* pcBitName)
{
	if (pcBitName)
		return dynBitFromNameInline(pcBitName);
	return (DynBit)0;
}

__forceinline static U32 dynNumBitsPerType(eDynBitType eBitType)
{
	return eaUSize(&bitList.bits[eBitType]);
}

__forceinline static const char* dynBitNameFromBit(DynBit bit)
{
	if (dynBitIsModeBit(bit))
	{
		if (bit < dynNumBitsPerType(eDynBitType_Mode))
			return bitList.bits[eDynBitType_Mode][bit]->pcBitName;
	}
	else
	{
		bit &= DYNBIT_DETAIL_BIT_MASK;
		if (bit < dynNumBitsPerType(eDynBitType_Detail))
			return bitList.bits[eDynBitType_Detail][bit]->pcBitName;
	}
	return NULL;
}


static void dynBitListUnload(void)
{
	stashTableDestroy(bitList.stBitNumbers);
	stashTableDestroy(bitList.stBitGroups);
	StructDeInit(parse_DynBitList, &bitList);
}

AUTO_COMMAND ACMD_CATEGORY(dynAnimation);
void danimForceBitListReload(void)
{
	dynBitListUnload();
	dynBitListLoad();
}



void dynBitListLoad(void)
{
	int count=0;
	loadstart_printf("Loading DynSeqBits...");

	ParserLoadFiles("dyn/seqbits", ".dbit", "DynSeqBits.bin", PARSER_BINS_ARE_SHARED | PARSER_OPTIONALFLAG, parse_DynBitList, &bitList);

	// First, make the 0th bit "UNASSIGNED" for error checking
	if (eaSize(&bitList.bits[eDynBitType_Mode]) > 0)
	{
		DynBitString* pNewBit = StructCreate(parse_DynBitString);
		pNewBit->pcBitName = allocAddString("UNASSIGNED_BIT");
		eaPush(&bitList.bits[eDynBitType_Mode], bitList.bits[eDynBitType_Mode][0]);
		bitList.bits[eDynBitType_Mode][0] = pNewBit;
	}

	FOR_EACH_IN_EARRAY(bitList.criticalBits.ppcBits, const char, pcCriticalBit)
		DynBitString* pNewBit = StructCreate(parse_DynBitString);
		eaPush(&bitList.bits[eDynBitType_Mode], pNewBit);
		pNewBit->pcBitName = pcCriticalBit;
	FOR_EACH_END;

	// Process bits and add to stashtable
	{
		int iBitType;
		StashTable stTempNameChecking;
		stTempNameChecking = stashTableCreateWithStringKeys(256, StashDefault);
		bitList.stBitNumbers = stashTableCreateWithStringKeys(dynNumBitsPerType(eDynBitType_Mode) + dynNumBitsPerType(eDynBitType_Detail), StashDefault);

		for (iBitType=eDynBitType_Mode; iBitType<iNumDynBitTypes; ++iBitType)
		{
			U32 uiBitIndex;
			for (uiBitIndex=0; uiBitIndex < dynNumBitsPerType(iBitType); ++uiBitIndex )
			{
				bool bNew = stashAddInt(stTempNameChecking, bitList.bits[iBitType][uiBitIndex]->pcBitName, 1, false);
				if ( !bNew )
				{
					Errorf("Bit name %s already used, you can not have more than one bit with the same name (even in different bit types)", bitList.bits[iBitType][uiBitIndex]->pcBitName );
				}
				count++;
				{
					U32 uiBitIndexToAdd = uiBitIndex;
					if (iBitType == eDynBitType_Detail)
						uiBitIndexToAdd |= DYNBIT_DETAIL_BIT_FLAG;
					stashAddInt(bitList.stBitNumbers, bitList.bits[iBitType][uiBitIndex]->pcBitName, uiBitIndexToAdd, false);
				}
			}
		}
		stashTableDestroy(stTempNameChecking);

		bitList.stBitGroups = stashTableCreateWithStringKeys(32, StashDefault);
		FOR_EACH_IN_EARRAY(bitList.bitGroup, DynBitGroup, pBitGroup)
			if (!stashAddPointer(bitList.stBitGroups, pBitGroup->pcBitGroupName, pBitGroup, false))
			{
				FatalErrorf("Already found bit group %s!", pBitGroup->pcBitGroupName);
			}
		FOR_EACH_END;
	}
	dynBitFieldStaticSetFromStrings(&bitList.criticalBits, NULL);
	loadend_printf(" done (%d bits).", count);
}

bool verifyIRQGroup(IRQGroup* pIRQGroup)
{
	if (!pIRQGroup->pcIRQGroupName)
	{
		AnimFileError(pIRQGroup->pcFileName, "IRQGroups must be named");
		return false;
	}

	if (strlen(pIRQGroup->pcIRQGroupName) < 1 || pIRQGroup->pcIRQGroupName[0] != '[')
	{
		AnimFileError(pIRQGroup->pcFileName, "IRQGroup names must start with '['. IRQGroup %s is invalid.", pIRQGroup->pcIRQGroupName);
		return false;
	}

	if (eaSize(&pIRQGroup->eaIRQNames) == 0)
	{
		AnimFileError(pIRQGroup->pcFileName, "IRQGroup %s must have at least one member!", pIRQGroup->pcIRQGroupName);
		return false;
	}

	return true;
}

AUTO_FIXUPFUNC;
TextParserResult fixupIRQGroup(IRQGroup* pIRQGroup, enumTextParserFixupType eType, void *pExtraData)
{
	switch (eType)
	{
		xcase FIXUPTYPE_POST_TEXT_READ:
		if (!verifyIRQGroup(pIRQGroup))
		{
			return PARSERESULT_INVALID; // remove this from the costume list
		}
	}

	return PARSERESULT_SUCCESS;
}

static void irqGroupReloadCallback(const char *relpath, int when)
{

	loadstart_printf("Reloading IRQGroups...");
	fileWaitForExclusiveAccess(relpath);
	errorLogFileIsBeingReloaded(relpath);

	if (!fileExists(relpath))
		; // File was deleted, do we care here?

	if(!ParserReloadFileToDictionary(relpath,hIRQGroupDict))
	{
		ErrorFilenamef(relpath, "Error reloading irqgroup file: %s", relpath);
	}

	loadend_printf("done");
}

void dynIRQGroupListLoad(void)
{
	loadstart_printf("Loading IRQGroups...");

	/*
	sharedMemoryPushDefaultMemoryAccess(PAGE_WRITECOPY); // For reload
	ParserLoadFilesSharedToDictionary("SM_SkelInfo", "defs/skel_infos", ".skif", "SkelInfos.bin", PARSER_BINS_ARE_SHARED, hSkelInfoDict);
	sharedMemoryPopDefaultMemoryAccess(); 
	*/
	// Do not share (until some shared memory bugs are resolved)
	ParserLoadFilesToDictionary("dyn/seqbits", ".irq", "IRQGroups.bin", PARSER_OPTIONALFLAG | PARSER_BINS_ARE_SHARED, hIRQGroupDict);

	// Reload callbacks not allowed
	if(isDevelopmentMode())
	{
		FolderCacheSetCallback(FOLDER_CACHE_CALLBACK_UPDATE, "dyn/seqbits/*.irq", irqGroupReloadCallback);
	}

	loadend_printf("done (%d IRQGroups)", RefSystem_GetDictionaryNumberOfReferents(hIRQGroupDict));
}

const IRQGroup* dynIRQGroupFromName(const char* pcName)
{
	return RefSystem_ReferentFromString(hIRQGroupDict, pcName);
}

bool dynIRQGroupContainsBit( const char* pcIRQGroupName, const char* pcBitName )
{
	const IRQGroup* pIRQGroup = dynIRQGroupFromName(pcIRQGroupName);

	if (!pIRQGroup)
	{
		Errorf("Unable to find IRQGroup %s", pcIRQGroupName);
		return false;
	}
	FOR_EACH_IN_EARRAY(pIRQGroup->eaIRQNames, const char, pcIRQBitName)
		if (pcBitName == pcIRQBitName)
			return true;
	FOR_EACH_END
	return false;
}

__forceinline static int findBitIndex(DynBitField* pBF, DynBit bitToFind)
{
	int result = 0;
	while (result < (int)pBF->uiNumBits)
	{
		if (pBF->aBits[result] == bitToFind)
			return result;
		result++;
	}
	return -1;
}

// this will walk through the list no more than once
// possible a binary search is faster, but since the vast majority of the time the number of bits will be < 4, probably not worth it,
// especially since the list needs to be kept sorted so the bits need to be walked in an insertion
bool dynBitFieldBitSet(DynBitField* pBF, DynBit bit)
{
	U32 uiIndex = 0;
	while (uiIndex < pBF->uiNumBits)
	{
		if (pBF->aBits[uiIndex] == bit)
			return false;
		if (pBF->aBits[uiIndex] > bit)
			break;
		++uiIndex;
	}

	if (pBF->uiNumBits < DYNBITFIELD_ARRAY_SIZE)
	{
		++pBF->uiNumBits; // we're adding one, if there's room, otherwise the last bit will fall off the list
	}
	else
	{
		char cBitArrayString[1024];
		dynBitArrayWriteBitString(SAFESTR(cBitArrayString), pBF->aBits, pBF->uiNumBits);
		ErrorDetailsf("Bit array: %s", cBitArrayString);
		Errorf("Exceeded bit array size of %d", DYNBITFIELD_ARRAY_SIZE);
	}

	{
		int iToInsert = bit;
		while (uiIndex < pBF->uiNumBits)
		{
			int iTemp = pBF->aBits[uiIndex];
			pBF->aBits[uiIndex] = iToInsert;
			iToInsert = iTemp;
			++uiIndex;
		}
	}
	return true;
}

bool dynBitFieldBitClear(DynBitField* pBF, DynBit bit)
{
	U32 uiIndex = 0;
	while (uiIndex < pBF->uiNumBits)
	{
		if (pBF->aBits[uiIndex] == bit)
			break;
		++uiIndex;
	}
	// didn't find it
	if (uiIndex == pBF->uiNumBits)
		return false; 
	--pBF->uiNumBits; // subtract one bit
	while (uiIndex < pBF->uiNumBits)
	{
		// this works because we've subtracted one off pBF->uiNumBits, so one past that number is still a valid bit
		pBF->aBits[uiIndex] = pBF->aBits[uiIndex+1];
		++uiIndex;
	}
	pBF->aBits[pBF->uiNumBits] = 0;
	return true;
}

static bool dynBitFieldBitToggle(SA_PARAM_NN_VALID DynBitField* pBF, DynBit bit)
{
	U32 uiIndex = 0;
	bool bClear = false;
	while (uiIndex < pBF->uiNumBits)
	{
		if (pBF->aBits[uiIndex] == bit)
		{
			bClear = true;
			break;
		}
		if (pBF->aBits[uiIndex] > bit)
			break;
		++uiIndex;
	}

	// we found either the insertion index or the index to clear
	if (bClear)
	{
		--pBF->uiNumBits; // subtract one bit
		while (uiIndex < pBF->uiNumBits)
		{
			// this works because we've subtracted one off pBF->uiNumBits, so one past that number is still a valid bit
			pBF->aBits[uiIndex] = pBF->aBits[uiIndex+1];
			++uiIndex;
		}
	}
	else // we're inserting the bit
	{
		if (pBF->uiNumBits < DYNBITFIELD_ARRAY_SIZE)
		{
			++pBF->uiNumBits; // we're adding one, if there's room, otherwise the last bit will fall off the list
		}
		else
		{
			char cBitArrayString[1024];
			dynBitArrayWriteBitString(SAFESTR(cBitArrayString), pBF->aBits, pBF->uiNumBits);
			ErrorDetailsf("Bit array: %s", cBitArrayString);
			Errorf("Exceeded bit array size of %d", DYNBITFIELD_ARRAY_SIZE);
		}

		{
			int iToInsert = bit;
			while (uiIndex < pBF->uiNumBits)
			{
				int iTemp = pBF->aBits[uiIndex];
				pBF->aBits[uiIndex] = iToInsert;
				iToInsert = iTemp;
				++uiIndex;
			}
		}
	}
	return true;
}

bool dynBitFieldBitTest(const DynBitField* pBF, DynBit bit)
{
	U32 uiIndex = 0;
	while (uiIndex < pBF->uiNumBits)
	{
		if (pBF->aBits[uiIndex] == bit)
		{
			return true;
			break;
		}
		if (pBF->aBits[uiIndex] > bit)
			break;
		++uiIndex;
	}
	return false;
}



bool dynBitIsValidName(const char* pcBitName)
{
	return (!!dynBitFromNameInline(pcBitName));
}

const DynBitGroup* dynBitGroupFromName(const char* pcBitName)
{
	const DynBitGroup* pResult;
	if (stashFindPointerConst(bitList.stBitGroups, pcBitName, &pResult))
		return pResult;
	return NULL;
}


bool dynBitActOnByName(DynBitField* pBF, const char* pcBit, eDynBitAction edba )
{
	DynBit bit = dynBitFromNameInline(pcBit);
	if ( bit )
	{
		switch( edba )
		{
			xcase edba_Set:
				dynBitFieldBitSet(pBF, bit);
				return true;
			xcase edba_Clear:
				dynBitFieldBitClear(pBF, bit);
				return true;
			xcase edba_Toggle:
				return dynBitFieldBitToggle(pBF, bit);
			xcase edba_Test:
				return dynBitFieldBitTest(pBF, bit);
		}
	}
	return false;
}


// Returns the number of chars written to the buffer
void dynBitArrayWriteBitString(char* pcBuffer, U32 uiMaxBufferSize, const DynBit* pBits, U32 uiNumBits)
{
	U32 uiIndex;
	bool bFirstBit = true;
	pcBuffer[0] = 0;

	for (uiIndex=0; uiIndex < uiNumBits; ++uiIndex)
	{
		const char* pcBitName = dynBitNameFromBit(pBits[uiIndex]);
		if (pcBitName)
		{
			if ( !bFirstBit )
			{
				strcat_s(pcBuffer, uiMaxBufferSize, " ");
			}
			else
				bFirstBit = false;
			strcat_s(pcBuffer, uiMaxBufferSize, pcBitName);
		}
		else
		{
			Errorf("Failed to find bit name for bit %d", pBits[uiIndex]);
		}
	}
}

bool dynBitFieldCheckForExtraBits(const DynBitFieldStatic* pBFToCheck, const DynBitField* pBFAgainst)
{
	// Look to see if pBFToCheck has any bits that pBFAgainst doesn't
	U32 uiA, uiB;
	uiA = uiB = 0;
	while (uiA < pBFToCheck->uiNumBits && uiB < pBFAgainst->uiNumBits)
	{
		if (pBFToCheck->pBits[uiA] < pBFAgainst->aBits[uiB])
			return true;
		else if (pBFToCheck->pBits[uiA] > pBFAgainst->aBits[uiB])
			++uiB;
		else // they are equal
		{
			++uiA;
			++uiB;
		}
	}
	// If we've exhausted one of them, make sure that pBFToCheck is exhausted, otherwise it has bits that pBFAgainst doesn't
	if (uiA < pBFToCheck->uiNumBits)
		return true;

	// If we got this far, there are no extra bits
	return false;
}

U32 dynBitArrayCountBitsInCommon(const DynBit* pBFToCheck, U32 uiNumToCheck, const DynBit* pBFAgainst, U32 uiNumAgainst, eDynBitType eBitType, char** pcDebugLog)
{
	U32 uiA, uiB, uiTotal;
	uiA = uiB = uiTotal = 0;
	switch (eBitType)
	{
		xcase eDynBitType_Detail:
		{
			while (uiA < uiNumToCheck && uiB < uiNumAgainst)
			{
				if (pBFToCheck[uiA] == pBFAgainst[uiB])
				{
					if (dynBitIsDetailBit(pBFToCheck[uiA]))
					{
						++uiTotal;
						if (pcDebugLog)
						{
							char cTempBuffer[128];
							strcpy(cTempBuffer, dynBitNameFromBit(pBFToCheck[uiA]));
							strupr(cTempBuffer);
							estrConcatf(pcDebugLog, "%s ", cTempBuffer);
						}
					}
					++uiA;
					++uiB;
				}
				else if (pBFToCheck[uiA] > pBFAgainst[uiB]) // A > B
				{
					++uiB;
				}
				else // B > A
				{
					++uiA;
				}
			}
		}
		xcase eDynBitType_Mode:
		{
			while (uiA < uiNumToCheck && uiB < uiNumAgainst)
			{
				if (dynBitIsDetailBit(pBFToCheck[uiA]) || dynBitIsDetailBit(pBFAgainst[uiB]))
					return uiTotal;
				if (pBFToCheck[uiA] == pBFAgainst[uiB])
				{
					if (pcDebugLog)
					{
						char cTempBuffer[128];
						strcpy(cTempBuffer, dynBitNameFromBit(pBFToCheck[uiA]));
						strupr(cTempBuffer);
						estrConcatf(pcDebugLog, "%s ", cTempBuffer);
					}
					++uiTotal;
					++uiA;
					++uiB;
				}
				else if (pBFToCheck[uiA] > pBFAgainst[uiB]) // A > B
				{
					++uiB;
				}
				else // B > A
				{
					++uiA;
				}
			}
		}
	}
	return uiTotal;
}

U32 dynBitFieldStaticCountCriticalBits(const DynBitFieldStatic* pBF, char** pcDebugLog)
{
	return dynBitArrayCountBitsInCommon(pBF->pBits, pBF->uiNumBits, bitList.criticalBits.pBits, bitList.criticalBits.uiNumBits, eDynBitType_Mode, pcDebugLog);
}

U32 dynBitArrayCountTotalBitsInCommon(const DynBit* pBFToCheck, U32 uiNumToCheck, const DynBit* pBFAgainst, U32 uiNumAgainst)
{
	U32 uiA, uiB, uiTotal;
	uiA = uiB = uiTotal = 0;
	while (uiA < uiNumToCheck && uiB < uiNumToCheck)
	{
		if (pBFToCheck[uiA] == pBFAgainst[uiB])
		{
			++uiTotal;
			++uiA;
			++uiB;
		}
		else if (pBFToCheck[uiA] > pBFAgainst[uiB]) // A > B
		{
			++uiB;
		}
		else // B > A
		{
			++uiA;
		}
	}
	return uiTotal;
}

static bool dynBitFieldSetFromStringsHelper(U32** peaIndices, const char** ppcBits, U32 uiNumStrings, SA_PARAM_OP_VALID const char** ppcBadBit)
{
	bool bRet = true;
	U32 uiStringIndex;
	for (uiStringIndex=0; uiStringIndex<uiNumStrings; ++uiStringIndex)
	{
		const char* pcBitName = ppcBits[uiStringIndex];
		const DynBitGroup* pBitGroup = dynBitGroupFromName(pcBitName);
		bool bFoundBit = false;
		if (pBitGroup)
		{
			if (dynBitFieldSetFromStringsHelper(peaIndices, pBitGroup->eaBits, eaSize(&pBitGroup->eaBits), ppcBadBit))
				bFoundBit = true;
		}
		else
		{
			DynBit bit = dynBitFromName(pcBitName);
			if (bit)
				bFoundBit = true;
			ea32Push(peaIndices, (U32)bit);
		}
		if (!bFoundBit)
		{
			if (ppcBadBit)
				*ppcBadBit = pcBitName;
			//return false;
			bRet = false;
		}
	}
	return bRet;
}

bool dynBitFieldStaticSetFromStrings(DynBitFieldStatic* pBF, const char** ppcBadBit)
{
	U32 uiIndex;
	U32* eaIndices = NULL;
	// Call the helper function, since it might need to recurse for bit groups
	bool bSuccess = dynBitFieldSetFromStringsHelper(&eaIndices, pBF->ppcBits, eaSize(&pBF->ppcBits), ppcBadBit);

	// Sort the results
	ea32QSort(eaIndices, intCmp);

	// Allocate and copy into the dynamic alloc array
	pBF->uiNumBits = ea32Size(&eaIndices);
	pBF->pBits = calloc(pBF->uiNumBits, sizeof(DynBit));
	for (uiIndex=0; uiIndex < pBF->uiNumBits; ++uiIndex)
		pBF->pBits[uiIndex] = (DynBit)eaIndices[uiIndex];

	// cleanup
	ea32Destroy(&eaIndices);
	return bSuccess;
}

void dynBitFieldSetAllFromBitFieldStatic(DynBitField* pBF, const DynBitFieldStatic* pSrc)
{
	U32 uiIndex = 0;
	while (uiIndex < pSrc->uiNumBits)
	{
		dynBitFieldBitSet(pBF, pSrc->pBits[uiIndex]);
		++uiIndex;
	}
}

void dynBitFieldSetAllFromBitField(DynBitField* pBF, const DynBitField* pSrc)
{
	U32 uiIndex = 0;
	while (uiIndex < pSrc->uiNumBits)
	{
		dynBitFieldBitSet(pBF, pSrc->aBits[uiIndex]);
		++uiIndex;
	}
}

void dynBitFieldGroupClearAll(DynBitFieldGroup* pBFGroup)
{
	dynBitFieldClear(&pBFGroup->flashBits);
	dynBitFieldClear(&pBFGroup->toggleBits);
}

void dynBitFieldGroupFlashBit(DynBitFieldGroup* pBFGroup, const char* pcBit)
{
	if (!dynBitActOnByName(&pBFGroup->flashBits, pcBit, edba_Set))
		Errorf("Unknown bit %s", pcBit);
}

void dynBitFieldGroupToggleBit(DynBitFieldGroup* pBFGroup, const char* pcBit)
{
	if (!dynBitActOnByName(&pBFGroup->toggleBits, pcBit, edba_Toggle))
		Errorf("Unknown bit %s", pcBit);
}

void dynBitFieldGroupSetBit(DynBitFieldGroup* pBFGroup, const char* pcBit)
{
	if (!dynBitActOnByName(&pBFGroup->toggleBits, pcBit, edba_Set))
		Errorf("Unknown bit %s", pcBit);
}

void dynBitFieldGroupClearBit(DynBitFieldGroup* pBFGroup, const char* pcBit )
{
	if (!dynBitActOnByName(&pBFGroup->toggleBits, pcBit, edba_Clear))
		Errorf("Unknown bit %s", pcBit);
	if (!dynBitActOnByName(&pBFGroup->flashBits, pcBit, edba_Clear))
		Errorf("Unknown bit %s", pcBit);
}

void dynBitFieldGroupSetToMatchSentence(DynBitFieldGroup* pBFGroup, char* pcBits )
{
	//meaningless call to reset strTok
	char* pcBit = strTokWithSpacesAndPunctuation(NULL, NULL);

	pcBit = strTokWithSpacesAndPunctuation(pcBits, " ");

	dynBitFieldGroupClearAll(pBFGroup);

	while (pcBit)
	{
		dynBitFieldGroupSetBit(pBFGroup, pcBit);
		pcBit = strTokWithSpacesAndPunctuation(pcBits, " ");
	}
}

void dynBitFieldGroupAddBits(DynBitFieldGroup * pBFG, const char *pcBits, bool bToggle)
{
	char *strtokcontext = 0;

	//meaningless call to reset strTok
	char* pcBit = strTokWithSpacesAndPunctuation(NULL, NULL);

	pcBit = strTokWithSpacesAndPunctuation(pcBits, " ");

	while (pcBit)
	{
		if ( bToggle )
			dynBitFieldGroupToggleBit(pBFG, pcBit);
		else
			dynBitFieldGroupFlashBit(pBFG, pcBit);

		pcBit = strTokWithSpacesAndPunctuation(pcBits, " ");
	}
}


#include "dynBitField_h_ast.c"
#include "dynBitField_c_ast.c"