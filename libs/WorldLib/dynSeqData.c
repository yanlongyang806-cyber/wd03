
#include "dynSeqData.h"

#include "error.h"
#include "fileutil.h"
#include "foldercache.h"
#include "StringCache.h"
#include "ThreadSafePriorityCache.h"
#include "ThreadSafeMemoryPool.h"


#include "wlState.h"

#include "dynSeqdata_h_ast.h"

#include "dynAction.h"
#include "dynBitField.h"
#include "ResourceInfo.h"

AUTO_RUN_ANON(memBudgetAddMapping(__FILE__, BUDGET_Animation););

DictionaryHandle hSeqDataDict;



DynSeqDataCollection** eaDynSeqDataCollections;
StashTable stSeqDataCollection;
DynSeqDataCollection* pDefaultCollection;

ThreadSafePriorityCache* pSeqDataCache;

typedef struct DynSeqDataBitSetKey
{
	DynSeqDataCollection* pCollection;
	DynBitField bits;
} DynSeqDataBitSetKey;

TSMP_DEFINE(DynSeqDataBitSetKey);

static DynSeqDataBitSetKey* createDynSeqDataBitSetKey(DynSeqDataCollection* pCollection, const DynBitField* pBF)
{
	DynSeqDataBitSetKey* pKey = TSMP_ALLOC(DynSeqDataBitSetKey);
	pKey->pCollection = pCollection;
	dynBitFieldCopy(pBF, &pKey->bits);
	return pKey;
}

static const char *s_pcDefault;
const char* pcHipsName;
const char *pcRootUpperbodyName;
const char* pcWaistName;
const char* pcHandL;
const char* pcHandR;
AUTO_RUN;
void dynSeqData_InitStrings(void)
{
	s_pcDefault = allocAddStaticString("Default");
	pcHipsName = allocAddStaticString("Hips");
	pcHandL = allocAddStaticString("HandL");
	pcHandR = allocAddStaticString("HandR");
	pcWaistName = allocAddStaticString("Waist");
	pcRootUpperbodyName = allocAddStaticString("Root_Upperbody");
}


const char* pcPrefixName = "dyn/sequence";

static const char* dynSeqDataCollectionNameFromFileName(const char* pcFileName)
{
	char cCollectionName[MAX_PATH];
	char* pcPrefix;
	char* pcResult;
	strcpy(cCollectionName, pcFileName);
	pcPrefix = strstri(cCollectionName, pcPrefixName);
	if (!pcPrefix)
	{
		AnimFileError(pcFileName, "Can't find base sequence directory for sequence data!");
	}
	pcResult = pcPrefix + strlen(pcPrefixName) + 1;
	if (strchr(pcResult, '/'))
		return allocAddString(getDirectoryName(pcResult));
	return s_pcDefault;
}

DynSeqDataCollection* dynSeqDataCollectionFromName(const char* pcSequencer)
{
	DynSeqDataCollection* pCollection;
	if (!pcSequencer)
		return pDefaultCollection;
	if (stashFindPointer(stSeqDataCollection, pcSequencer, &pCollection))
		return pCollection;
	return NULL;
}

static void dynSeqDataPushIntoCollection(DynSeqData* pData)
{
	// Generate seq name from directory
	const char* pcCollectionName = dynSeqDataCollectionNameFromFileName(pData->pcFileName);
	DynSeqDataCollection* pCollection = dynSeqDataCollectionFromName(pcCollectionName);
	if (!pCollection)
	{
		pCollection = calloc(sizeof(DynSeqDataCollection), 1);
		pCollection->pcSequencerName = pcCollectionName;
		if (pCollection->pcSequencerName == s_pcDefault)
		{
			pCollection->bDefault = true;
			pDefaultCollection = pCollection;
		}
		eaPush(&eaDynSeqDataCollections, pCollection);
		if (!stSeqDataCollection)
			stSeqDataCollection = stashTableCreateWithStringKeys(32, StashDefault);
		stashAddPointer(stSeqDataCollection, pCollection->pcSequencerName, pCollection, false);
	}

	// Make sure the handle isn't already there
	{
		U32 uiNumSeqDatas = eaSize(&pCollection->eaDynSeqDatas);
		U32 uiSeqRefIndex;
		DynSeqDataReference *pSeqRef;
		for (uiSeqRefIndex=0; uiSeqRefIndex<uiNumSeqDatas; ++uiSeqRefIndex)
		{
			const char* pcHandleString = REF_STRING_FROM_HANDLE(pCollection->eaDynSeqDatas[uiSeqRefIndex]->hSeqData);			
			if (inline_stricmp(pcHandleString, pData->pcName)==0)
			{
				// ALready here, forget about adding it
				return;
			}
		}

		// If we made it here, it is unique
		pSeqRef = calloc(sizeof(DynSeqDataReference),1);
		SET_HANDLE_FROM_STRING(hSeqDataDict, pData->pcName, pSeqRef->hSeqData);
		eaPush(&pCollection->eaDynSeqDatas, pSeqRef);

		if (pData->bDefaultSequence)
		{
			// Make sure there isn't another already
			if (IS_HANDLE_ACTIVE(pCollection->hDefaultSequence))
			{
				AnimFileError(pData->pcFileName, "Already have a default anim %s, in sequencer %s!", REF_STRING_FROM_HANDLE(pCollection->hDefaultSequence), pCollection->pcSequencerName);
			}
			else
			{
				SET_HANDLE_FROM_STRING(hSeqDataDict, pData->pcName, pCollection->hDefaultSequence);
			}
		}
	}
}

static bool dynSeqDataVerify(DynSeqData* pSeq)
{
	const char* pcBadBit = NULL;
	pSeq->bVerified = false;
	// Process and verify the requiredBits
	{
		if (!dynBitFieldStaticSetFromStrings(&pSeq->requiresBits, &pcBadBit))
		{
			AnimFileError(pSeq->pcFileName, "Invalid RequiresBit %s in DynSequence %s", pcBadBit, pSeq->pcName);
			return false;
		}

		pSeq->uiNumCriticalBits = dynBitFieldStaticCountCriticalBits(&pSeq->requiresBits, NULL);
		// Free the bit string array?
	}
	// Process and verify the optionalBits
	{
		// Merge requiredBits into optionalBits
		eaPushEArray(&pSeq->optionalBits.ppcBits, &pSeq->requiresBits.ppcBits);
		if (!dynBitFieldStaticSetFromStrings(&pSeq->optionalBits, &pcBadBit))
		{
			AnimFileError(pSeq->pcFileName, "Invalid OptionalBit %s in DynSequence %s", pcBadBit, pSeq->pcName);
			return false;
		}
	}

	// Verify the actions
	{
		const U32 uiNumActions = eaSize(&pSeq->eaActions);
		U32 uiActionIndex;
		for (uiActionIndex=0; uiActionIndex<uiNumActions; ++uiActionIndex)
		{
			DynAction* pAction = pSeq->eaActions[uiActionIndex];
			if (!dynActionVerify(pAction, pSeq))
				return false;
			pAction->pParentSeq = pSeq;
		}

		if ( uiNumActions == 0 )
		{
			AnimFileError(pSeq->pcFileName, "DynSequence %s has no DynActions!", pSeq->pcName);
			return false;
		}
	}


	// Lookup all nextactions
	{
		const U32 uiNumActions = eaSize(&pSeq->eaActions);
		U32 uiActionIndex;
		pSeq->pDefaultFirstAction = NULL;
		for (uiActionIndex=0; uiActionIndex<uiNumActions; ++uiActionIndex)
		{
			DynAction* pAction = pSeq->eaActions[uiActionIndex];
			if ( !dynActionLookupNextActions(pAction, &pSeq->eaActions))
				return false;

			// Also, check for default first
			if ( pAction->bDefaultFirst )
			{
				if ( pSeq->pDefaultFirstAction )
				{
					AnimFileError(pSeq->pcFileName, "DynSequence %s has two DefaultFirst DynActions!", pSeq->pcName);
					return false;
				}
				pSeq->pDefaultFirstAction = pAction;
			}
		}
	}

	if (!pSeq->pDefaultFirstAction)
	{
		pSeq->pDefaultFirstAction = pSeq->eaActions[0];
	}

	dynSeqDataPushIntoCollection(pSeq);


	pSeq->bVerified = true;
	return true;
}



static void checkForDuplicateSeqDatas(void)
{
	DictionaryEArrayStruct *pSeqDataArray = resDictGetEArrayStruct(hSeqDataDict);

	FOR_EACH_IN_EARRAY(pSeqDataArray->ppReferents, DynSeqData, pSeq)
		pSeq->bOverRidden = false;
	FOR_EACH_END

	FOR_EACH_IN_EARRAY(eaDynSeqDataCollections, DynSeqDataCollection, pCollection)
		U32 uiNumSeqDatas = eaSize(&pCollection->eaDynSeqDatas);
		U32 uiSeqRefIndex;
		for (uiSeqRefIndex=0; uiSeqRefIndex<uiNumSeqDatas; ++uiSeqRefIndex)
		{
			DynSeqData* pSeq = GET_REF(pCollection->eaDynSeqDatas[uiSeqRefIndex]->hSeqData);
			U32 uiOtherSeqRefIndex;
			if (!pSeq || !pSeq->bVerified)
				continue;
			for (uiOtherSeqRefIndex=0; uiOtherSeqRefIndex<uiNumSeqDatas; ++uiOtherSeqRefIndex)
			{
				DynSeqData* pOtherSeq = GET_REF(pCollection->eaDynSeqDatas[uiOtherSeqRefIndex]->hSeqData);
				if (pSeq == pOtherSeq)
					continue;
				if (!pOtherSeq || !pOtherSeq->bVerified)
					continue;
				if (dynBitFieldStaticsAreEqual(&pSeq->requiresBits, &pOtherSeq->requiresBits))
				{
					if (!pSeq->bCore && !pOtherSeq->bCore)
						AnimFileError(pSeq->pcFileName, "Error. Two seqs in Sequencer %s have same bits and neither are set to be Core: %s and %s in file %s.", pCollection->pcSequencerName, pSeq->pcName, pOtherSeq->pcName, pOtherSeq->pcFileName);
					else if (pSeq->bCore && pOtherSeq->bCore)
						AnimFileError(pSeq->pcFileName, "Error. Two seqs in Sequencer %s have same bits and both are set to be Core: %s and %s in file %s.", pCollection->pcSequencerName, pSeq->pcName, pOtherSeq->pcName, pOtherSeq->pcFileName);
					else
					{
						// Flag the "Core" one as overridden!
						if (pSeq->bCore)
							pSeq->bOverRidden = true;
						else if (pOtherSeq->bCore)
							pOtherSeq->bOverRidden = true;
					}
				}
			}
		}
	FOR_EACH_END
}

static void verifyDefaultSequences(void)
{
	FOR_EACH_IN_EARRAY(eaDynSeqDataCollections, DynSeqDataCollection, pCollection)
		if (!GET_REF(pCollection->hDefaultSequence))
		{
			FatalErrorf("Unable to find default animation for sequencer %s. You must add the flag \"DefaultSequence\" to one (and only one) Sequence!", pCollection->pcSequencerName);
		}
	FOR_EACH_END
}


static void dynSeqDataReloadCallback(const char *relpath, int when)
{
	if (strstr(relpath, "/_")) {
		return;
	}

	if (!fileExists(relpath))
		; // File was deleted, do we care here?

	fileWaitForExclusiveAccess(relpath);
	errorLogFileIsBeingReloaded(relpath);


	if(!ParserReloadFileToDictionary(relpath,hSeqDataDict))
	{
		AnimFileError(relpath, "Error reloading DynSequence file: %s", relpath);
	}
	else
	{
		// nothing to do here
	}
	checkForDuplicateSeqDatas();
	verifyDefaultSequences();
	dynSequencerResetAll();
	tspCacheClear(pSeqDataCache);
}


AUTO_FIXUPFUNC;
TextParserResult fixupSeqData(DynSeqData* pSeqData, enumTextParserFixupType eType, void *pExtraData)
{
	switch (eType)
	{
		xcase FIXUPTYPE_POST_BIN_READ:
		case FIXUPTYPE_POST_TEXT_READ:
			if (!dynSeqDataVerify(pSeqData))
			{
				return PARSERESULT_INVALID; // remove this from the costume list
			}
	}

	return PARSERESULT_SUCCESS;
}

AUTO_RUN;
void registerSeqDataDictionary(void)
{
	hSeqDataDict = RefSystem_RegisterSelfDefiningDictionary("SeqData", false, parse_DynSeqData, true, true, "DynSequence");
}

static void dynSeqDataBitSetKeyFree(DynSeqDataBitSetKey* pKey)
{
	TSMP_FREE(DynSeqDataBitSetKey, pKey);
}

// Should return > 0 if A is higher priority than B
static int dynSeqDataCompare(const DynSeqData* pA, const DynSeqData* pB)
{
	return (int)pA->uiLastUsedFrameStamp - (int)pB->uiLastUsedFrameStamp;
}

void dynSeqDataLoadAll(void)
{
	loadstart_printf("Loading DynSequences...");

	// optional for outsource build
	ParserLoadFilesToDictionary("dyn/sequence", ".dseq", "DynSequences.bin", PARSER_BINS_ARE_SHARED|PARSER_OPTIONALFLAG, hSeqDataDict);

	if(isDevelopmentMode())
	{
		FolderCacheSetCallback(FOLDER_CACHE_CALLBACK_UPDATE, "dyn/sequence/*.dseq", dynSeqDataReloadCallback);
	}

	checkForDuplicateSeqDatas();
	verifyDefaultSequences();
	pSeqDataCache = tspCacheCreate(256, 8, StashDefault, StashKeyTypeFixedSize, sizeof(DynSeqDataBitSetKey), dynSeqDataCompare, dynSeqDataBitSetKeyFree, NULL, parse_DynSeqData);
	TSMP_CREATE(DynSeqDataBitSetKey, 272);
	loadend_printf("done (%d DynSequences)", RefSystem_GetDictionaryNumberOfReferents(hSeqDataDict) );
}


static bool bReloadAll = false;

void dynSeqDataCheckReloads(void)
{
	DictionaryEArrayStruct *pSeqDataArray;

	//this should only be run without shared memory since it reprocesses the
	//existing data.. to work with shared memory it would have to reload the
	//files from disk (which is significantly slower)
	assert(sharedMemoryGetMode() != SMM_ENABLED);

	if (!bReloadAll) {
		return;
	}

	loadstart_printf("Reloading DynSequences...");
	pSeqDataArray = resDictGetEArrayStruct(hSeqDataDict);
	FOR_EACH_IN_EARRAY(pSeqDataArray->ppReferents, DynSeqData, pSeq)
		dynSeqDataVerify(pSeq);
	FOR_EACH_END
	checkForDuplicateSeqDatas();
	verifyDefaultSequences();
	dynSequencerResetAll();
	tspCacheClear(pSeqDataCache);
	bReloadAll = false;
	loadend_printf("done (%d DynSequences)", RefSystem_GetDictionaryNumberOfReferents(hSeqDataDict) );
}

void dynSeqDataReloadAll(void)
{
	bReloadAll = true;
}

const DynSeqData* dynSeqDataFromName(const char* pcSeqName)
{
	return RefSystem_ReferentFromString(hSeqDataDict, pcSeqName);
}

//used to report errors when the fixed version of dynSeqDataFromCollectionAndIndex gives a different result than the original version
static U32 uiReportDynSeqDataLookupBugs = 0;
AUTO_CMD_INT( uiReportDynSeqDataLookupBugs, danimReportDynSeqDataLookupBugs ) ACMD_COMMANDLINE ACMD_CATEGORY(dynAnimation);

//used to run the original version of dynSeqDataFromCollectionAndIndex which contains a search results truncation bug
static U32 uiUseOriginalDynSeqDataLookup = 0;
AUTO_CMD_INT( uiUseOriginalDynSeqDataLookup, danimUseOriginalDynSeqDataLookup ) ACMD_COMMANDLINE ACMD_CATEGORY(dynAnimation);

#define DYN_SEQUENCE_MAX_MATCHES 32
#define DYN_SEQUENCE_MAX_DEBUG_MATCHES 64

const DynSeqData* dynSeqDataFromCollectionAndIndex(DynSeqDataCollection* pCollection, U32 uiIndex)
{
	return GET_REF(pCollection->eaDynSeqDatas[uiIndex]->hSeqData);	
}

static const DynSeqData* dynSeqDataFromBitsLinearLookup( DynSeqDataCollection* pCollection, const DynBitField* pBF, bool * pIsDefault, char** pcDebugLog) 
{
	const DynSeqData *pResultFixed = NULL;
	const DynSeqData *pResultBugged = NULL;
	bool bTruncatedSearchResults = false;

	if (!uiUseOriginalDynSeqDataLookup ||
		uiReportDynSeqDataLookupBugs)
	{
		//NEW VERSION SANS TRUNCATION BUG

		U32 uiNumSequences = eaSize(&pCollection->eaDynSeqDatas);
		U32 uiSequenceIndex;

		U32 uiBestMatch_SeqIndex;
		U32 uiBestMatch_NumCriticalBits;
		U32 uiBestMatch_NumModeBitsInCommon;
		F32 fBestMatch_Priority;
		const char *pcBestMatch_Name;
		bool bInitBestMatch = true;	

		U32 uiDebug_SequenceIndices[DYN_SEQUENCE_MAX_DEBUG_MATCHES];
		U32 uiDebug_ModeBitsInCommon[DYN_SEQUENCE_MAX_DEBUG_MATCHES];
		U32 uiDebug_NumCriticalBits[DYN_SEQUENCE_MAX_DEBUG_MATCHES];
		U32 uiDebug_NumMatches = 0;

		for (uiSequenceIndex = 0; uiSequenceIndex < uiNumSequences; ++uiSequenceIndex)
		{
			const DynSeqData* pSequence = dynSeqDataFromCollectionAndIndex(pCollection, uiSequenceIndex);
			U32 uiNumModeBitsInCommon;
			bool bIsBestMatch;

			// We don't allow any actions that have extraneous bits of any kind
			if (!pSequence || !pSequence->bVerified || pSequence->bOverRidden || dynBitFieldCheckForExtraBits(&pSequence->requiresBits, pBF))
				continue;

			// Add this one to our list of possible matches for debug output
			if (pcDebugLog &&
				uiDebug_NumMatches < DYN_SEQUENCE_MAX_DEBUG_MATCHES)
			{
				uiDebug_ModeBitsInCommon[uiDebug_NumMatches] = dynBitArrayCountBitsInCommon(pSequence->optionalBits.pBits, pSequence->optionalBits.uiNumBits, pBF->aBits, pBF->uiNumBits, eDynBitType_Mode, NULL);
				uiDebug_NumCriticalBits[uiDebug_NumMatches] = pSequence->uiNumCriticalBits;
				uiDebug_SequenceIndices[uiDebug_NumMatches++] = uiSequenceIndex;
			}

			//check if it's the best match seen so far
			bIsBestMatch = false;

			if (TRUE_THEN_RESET(bInitBestMatch))
			{
				uiNumModeBitsInCommon = dynBitArrayCountBitsInCommon(pSequence->optionalBits.pBits, pSequence->optionalBits.uiNumBits, pBF->aBits, pBF->uiNumBits, eDynBitType_Mode, NULL);
				bIsBestMatch = true;
			}
			else if (uiBestMatch_NumCriticalBits < pSequence->uiNumCriticalBits)
			{
				uiNumModeBitsInCommon = dynBitArrayCountBitsInCommon(pSequence->optionalBits.pBits, pSequence->optionalBits.uiNumBits, pBF->aBits, pBF->uiNumBits, eDynBitType_Mode, NULL);
				bIsBestMatch = true;
			}
			else if (uiBestMatch_NumCriticalBits == pSequence->uiNumCriticalBits)
			{
				uiNumModeBitsInCommon = dynBitArrayCountBitsInCommon(pSequence->optionalBits.pBits, pSequence->optionalBits.uiNumBits, pBF->aBits, pBF->uiNumBits, eDynBitType_Mode, NULL);
				if (uiBestMatch_NumModeBitsInCommon < uiNumModeBitsInCommon)
				{
					bIsBestMatch = true;
				}
				else if (uiBestMatch_NumModeBitsInCommon == uiNumModeBitsInCommon)
				{
					if (fBestMatch_Priority < pSequence->fPriority)
					{
						bIsBestMatch = true;
					}
					else if (fBestMatch_Priority == pSequence->fPriority)
					{
						// An exact match, this is an undefined event. Rather than leave it up to chance, we should sort by name so it is consistent:
						if (stricmp(pSequence->pcName, pcBestMatch_Name) < 0)
						{
							bIsBestMatch = true;
						}
					}
				}
			}

			// Update the best match data
			if (bIsBestMatch)
			{
				uiBestMatch_SeqIndex			= uiSequenceIndex;
				uiBestMatch_NumCriticalBits		= pSequence->uiNumCriticalBits;
				uiBestMatch_NumModeBitsInCommon	= uiNumModeBitsInCommon;
				fBestMatch_Priority				= pSequence->fPriority;
				pcBestMatch_Name				= pSequence->pcName;
			}
		}

		if (pcDebugLog && !uiUseOriginalDynSeqDataLookup)
		{
			U32 uiMatchIndex;
			char cBuffer[256];

			estrConcatf(pcDebugLog, "** Running Fixed Version **\n");
			estrConcatf(pcDebugLog, "** Only showing the first %d matches **\n", DYN_SEQUENCE_MAX_DEBUG_MATCHES);

			dynBitArrayWriteBitString(SAFESTR(cBuffer), pBF->aBits, pBF->uiNumBits);
			strupr(cBuffer);
			estrConcatf(pcDebugLog, "Test bits: %s\n\n", cBuffer);

			for (uiMatchIndex = 0; uiMatchIndex < uiDebug_NumMatches; ++uiMatchIndex)
			{
				const DynSeqData* pNewMatchData = dynSeqDataFromCollectionAndIndex(pCollection, uiDebug_SequenceIndices[uiMatchIndex]);
				estrConcatf(pcDebugLog, "%2d: %s:   %d mode bits in common, %d critical bits, %.2f priority\n", uiMatchIndex+1, pNewMatchData->pcName, uiDebug_ModeBitsInCommon[uiMatchIndex], uiDebug_NumCriticalBits[uiMatchIndex], pNewMatchData->fPriority);
				estrConcatf(pcDebugLog, "\tMode bits: ");
				dynBitArrayCountBitsInCommon(pNewMatchData->optionalBits.pBits, pNewMatchData->optionalBits.uiNumBits, pBF->aBits, pBF->uiNumBits, eDynBitType_Mode, pcDebugLog);
				estrConcatf(pcDebugLog, "\n");
				if (uiDebug_NumCriticalBits[uiMatchIndex] > 0)
				{
					estrConcatf(pcDebugLog, "\tCritical bits: ");
					dynBitFieldStaticCountCriticalBits(&pNewMatchData->optionalBits, pcDebugLog);
					estrConcatf(pcDebugLog, "\n");
				}
				estrConcatf(pcDebugLog, "\n");
			}
		}

		if (!bInitBestMatch)
		{
			//found a best match
			pResultFixed = dynSeqDataFromCollectionAndIndex(pCollection, uiBestMatch_SeqIndex);

			if (pIsDefault && !uiUseOriginalDynSeqDataLookup)
				*pIsDefault = false;

			if (pcDebugLog && !uiUseOriginalDynSeqDataLookup)
				estrConcatf(pcDebugLog, "\n\nChose #%d: %s\n", uiBestMatch_SeqIndex+1, pcBestMatch_Name);
		}
		else
		{
			//didn't find a best match
			pResultFixed = GET_REF(pCollection->hDefaultSequence);
			if (!pResultFixed) FatalErrorf("Can't find DefaultAnim, are you missing Core/data?");

			if (pIsDefault && !uiUseOriginalDynSeqDataLookup)
				*pIsDefault = true;

			if (pcDebugLog && pResultFixed && !uiUseOriginalDynSeqDataLookup)
				estrConcatf(pcDebugLog, "Found no match, chose default: %s\n", pResultFixed->pcName);
		}
	}
	
	if (uiUseOriginalDynSeqDataLookup ||
		uiReportDynSeqDataLookupBugs)
	{
		//OLD VERSION WITH TRUNCATION BUG

		// Ok, now find the best matches looking only at mode bits
		U32 uiNumModeBitMatches = 0;
		U32 uiSequenceIndices[DYN_SEQUENCE_MAX_MATCHES];
		U32 uiModeBitsInCommon[DYN_SEQUENCE_MAX_MATCHES];
		U32 uiNumCriticalBits[DYN_SEQUENCE_MAX_MATCHES];
		U32 uiNumSequences = eaSize(&pCollection->eaDynSeqDatas);
		U32 uiSequenceIndex;

		for (uiSequenceIndex=0; uiSequenceIndex < uiNumSequences; ++uiSequenceIndex)
		{
			const DynSeqData* pSequence = dynSeqDataFromCollectionAndIndex(pCollection, uiSequenceIndex);
			// We don't allow any actions that have extraneous bits of any kind
			if (!pSequence || !pSequence->bVerified || pSequence->bOverRidden || dynBitFieldCheckForExtraBits(&pSequence->requiresBits, pBF))
				continue;
			// Add this one to our list of possible matches
			uiModeBitsInCommon[uiNumModeBitMatches] = dynBitArrayCountBitsInCommon(pSequence->optionalBits.pBits, pSequence->optionalBits.uiNumBits, pBF->aBits, pBF->uiNumBits, eDynBitType_Mode, NULL);
			uiNumCriticalBits[uiNumModeBitMatches] = pSequence->uiNumCriticalBits;
			uiSequenceIndices[uiNumModeBitMatches++] = uiSequenceIndex;
			if (uiNumModeBitMatches == DYN_SEQUENCE_MAX_MATCHES) {
				bTruncatedSearchResults = true;
				break;
			}
		}

		if (pcDebugLog && uiUseOriginalDynSeqDataLookup)
		{
			U32 uiMatchIndex;
			char cBuffer[256];

			dynBitArrayWriteBitString(SAFESTR(cBuffer), pBF->aBits, pBF->uiNumBits);
			strupr(cBuffer);
			estrConcatf(pcDebugLog, "Test bits: %s\n\n", cBuffer);

			for (uiMatchIndex=0; uiMatchIndex<uiNumModeBitMatches; ++uiMatchIndex)
			{
				const DynSeqData* pNewMatchData = dynSeqDataFromCollectionAndIndex(pCollection, uiSequenceIndices[uiMatchIndex]);
				estrConcatf(pcDebugLog, "%2d: %s:   %d mode bits in common, %d critical bits, %.2f priority\n", uiMatchIndex+1, pNewMatchData->pcName, uiModeBitsInCommon[uiMatchIndex], uiNumCriticalBits[uiMatchIndex], pNewMatchData->fPriority);
				estrConcatf(pcDebugLog, "\tMode bits: ");
				dynBitArrayCountBitsInCommon(pNewMatchData->optionalBits.pBits, pNewMatchData->optionalBits.uiNumBits, pBF->aBits, pBF->uiNumBits, eDynBitType_Mode, pcDebugLog);
				estrConcatf(pcDebugLog, "\n");
				if (uiNumCriticalBits[uiMatchIndex] > 0)
				{
					estrConcatf(pcDebugLog, "\tCritical bits: ");
					dynBitFieldStaticCountCriticalBits(&pNewMatchData->optionalBits, pcDebugLog);
					estrConcatf(pcDebugLog, "\n");
				}
				estrConcatf(pcDebugLog, "\n");
			}
		}

		// Pick the best match (most bits in common)
		if ( uiNumModeBitMatches > 0 )
		{
			U32 uiMatchIndex;
			U32 uiBestMatchIndex = 0;
			for (uiMatchIndex=1; uiMatchIndex<uiNumModeBitMatches; ++uiMatchIndex)
			{
				if ( uiNumCriticalBits[uiMatchIndex] > uiNumCriticalBits[uiBestMatchIndex] )
					uiBestMatchIndex = uiMatchIndex;
				else if (uiNumCriticalBits[uiMatchIndex] < uiNumCriticalBits[uiBestMatchIndex] )
					continue;
				else if ( uiModeBitsInCommon[uiMatchIndex] > uiModeBitsInCommon[uiBestMatchIndex] )
					uiBestMatchIndex = uiMatchIndex;
				else if (uiModeBitsInCommon[uiMatchIndex] == uiModeBitsInCommon[uiBestMatchIndex] )
				{
					// Same number, first look to priority
					const DynSeqData* pNewMatchData = dynSeqDataFromCollectionAndIndex(pCollection, uiSequenceIndices[uiMatchIndex]);
					const DynSeqData* pBestMatchData = dynSeqDataFromCollectionAndIndex(pCollection, uiSequenceIndices[uiBestMatchIndex]);

					if ( pNewMatchData->fPriority > pBestMatchData->fPriority )
					{
						uiBestMatchIndex = uiMatchIndex;
					}
					else if (pNewMatchData->fPriority == pBestMatchData->fPriority)
					{
						// An exact match, this is an undefined event. Rather than leave it up to chance, we should sort by name so it is consistent:
						if (stricmp(pNewMatchData->pcName, pBestMatchData->pcName) < 0)
						{
							uiBestMatchIndex = uiMatchIndex;
						}
					}
				}
			}
			if (pIsDefault && uiUseOriginalDynSeqDataLookup)
				*pIsDefault = false;

			if (pcDebugLog && uiUseOriginalDynSeqDataLookup)
			{
				const DynSeqData* pBestMatchData = dynSeqDataFromCollectionAndIndex(pCollection, uiSequenceIndices[uiBestMatchIndex]);
				estrConcatf(pcDebugLog, "\n\nChose #%d: %s\n", uiBestMatchIndex+1, pBestMatchData->pcName);
			}

			pResultBugged = dynSeqDataFromCollectionAndIndex(pCollection, uiSequenceIndices[uiBestMatchIndex]);
		}
		else
		{
			pResultBugged = GET_REF(pCollection->hDefaultSequence);
			if (!pResultBugged)
				FatalErrorf("Can't find DefaultAnim, are you missing Core/data?");

			if (pIsDefault && uiUseOriginalDynSeqDataLookup)
				*pIsDefault = true;

			if (pcDebugLog && pResultBugged && uiUseOriginalDynSeqDataLookup)
			{
				estrConcatf(pcDebugLog, "Found no match, chose default: %s\n", pResultBugged->pcName);
			}
		}
	}

	if (uiReportDynSeqDataLookupBugs)
	{
		if (pResultFixed != pResultBugged)
		{
			char cBuffer[256];
			dynBitArrayWriteBitString(SAFESTR(cBuffer), pBF->aBits, pBF->uiNumBits);
			strupr(cBuffer);
			Errorf("%s: Found a fixed result '%s' that doesn't match the bugged result '%s' on sequencer '%s' with bits '%s'\n",
				__FUNCTION__,
				pResultFixed->pcName,
				pResultBugged->pcName,
				pCollection->pcSequencerName,
				cBuffer);
		}
		else if (bTruncatedSearchResults)
		{
			char cBuffer[256];
			dynBitArrayWriteBitString(SAFESTR(cBuffer), pBF->aBits, pBF->uiNumBits);
			strupr(cBuffer);
			Errorf("%s: Search results were truncated but still found matching fixed & bugged result '%s' on sequencer '%s' with bits '%s'\n",
				__FUNCTION__,
				pResultFixed->pcName,
				pCollection->pcSequencerName,
				cBuffer);
		}
	}

	if (uiUseOriginalDynSeqDataLookup) {
		return pResultBugged;
	} else {
		return pResultFixed;
	}
}

void dynSeqDataCacheUpdate(void)
{
	tspCacheUpdate(pSeqDataCache);
}

const DynSeqData* dynSeqDataFromBits(const char* pcSequencerName, const DynBitField* pBF, bool *pIsDefault)
{
	DynSeqDataCollection* pCollection = dynSeqDataCollectionFromName(pcSequencerName);
	if (!pCollection)
	{
		if (eaSize(&eaDynSeqDataCollections) <= 0)
		{
			FatalErrorf("No SeqData Collections found, has any animation data been loaded? Check DynSequences.bin!\n");
		}
		else
		{
			FatalErrorf("Can't find sequencer %s", pcSequencerName);
		}
		return NULL;
	}

	{
		DynSeqDataBitSetKey* pKey = createDynSeqDataBitSetKey(pCollection, pBF);
		const DynSeqData* pSeqData = tspCacheFind(pSeqDataCache, pKey);
		// First, try to look it up in the cache
		if (pSeqData)
		{
			TSMP_FREE(DynSeqDataBitSetKey, pKey);
		}
		else // if we don't find it, look it up with a slow linear lookup, and store it in the cache 
		{
			++dynDebugState.uiNumSeqDataCacheMisses[wl_state.frame_count & 15];
			pSeqData = dynSeqDataFromBitsLinearLookup(pCollection, pBF, pIsDefault, NULL);
			if (!tspCacheAdd(pSeqDataCache, pKey, pSeqData))
				TSMP_FREE(DynSeqDataBitSetKey, pKey); // Free it if we couldn't add it to the cache
		}
		((DynSeqData*)pSeqData)->uiLastUsedFrameStamp = wl_state.frame_count;
		return pSeqData;
	}
}

bool debugDynSeqDataFromBits(const char* pcSequencerName, const DynBitField* pBF, char** pcDebugLog)
{
	DynSeqDataCollection* pCollection = dynSeqDataCollectionFromName(pcSequencerName);
	if (!pCollection)
	{
		if (eaSize(&eaDynSeqDataCollections) <= 0)
		{
			Errorf("No SeqData Collections found, has any animation data been loaded? Check DynSequences.bin!\n");
		}
		else
		{
			Errorf("Can't find sequencer %s", pcSequencerName);
		}
		return false;
	}

	{
		dynSeqDataFromBitsLinearLookup(pCollection, pBF, NULL, pcDebugLog);
	}
	return true;
}



#include "dynSeqdata_h_ast.c"