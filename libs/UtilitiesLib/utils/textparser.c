// textparser.c - provides text and token processing functions
// NOTE - the textparser can NOT be used from a second thread.
// Please let Mark know if this needs to be changed


#include "memorypool.h"
#include "textparser.h"
#include "textparser_h_ast.h"
#include "url.h"
#include <stdio.h>
#include <string.h>
#include "fileutil.h"
#include <time.h>
#include "serialize.h"
#include "StringCache.h"
#include "tokenstore.h"
#include "structPack.h"
#include "structNet.h"
#include <limits.h>
#include "network/crypt.h"
#include "strings_opt.h"
#include "SharedMemory.h"
#include "objPath.h"
#include "ScratchStack.h"
#include "textparserinheritance.h"
#include "mutex.h"
#include "MemoryBudget.h"
#include "structinternals.h"
#include "ControllerScriptingSupport.h"
#include "stringutil.h"
#include "threadmanager.h"
#include "fileutil2.h"
#include "HttpXpathSupport.h"
#include "CmdParse.h"
#include "GenericPreProcess.h"
#include "resourceManager.h"
#include "ExpressionFunc.h"
#include "utilitiesLib.h"
#include "threadSafeMemoryPool.h"
#include "fileCache.h"
#include "FolderCache.h"
#include "StructINit.h"
#include "structinternals_h_ast.h"
#include "ResourceSystem_Internal.h"
#include "../../3rdparty/zlib/zlib.h"
#include "ScratchStack.h"
#include "logging.h"
#include "memalloc.h"
#include "errornet.h"
#include "errornet_h_ast.h"
#include "resourceManager_h_ast.h"
#include "textparser_c_ast.h"
#include "wininclude.h"

#include "netprivate.h" //so we can check error_occurred without function call overhead




//global flag used during produciton build to force creation of bins even if there are data
//errors
bool gbForceBinCreate = false;
// Forces bin files to be written, even if they have errors
AUTO_CMD_INT(gbForceBinCreate, forceBinCreate) ACMD_CMDLINE;

//Normally ForceBinCreate means "always read from text files no matter what, then always write the bin file no matter what". 
//This option changes that to "do whatever reading you would normally do, then always write the bin file no matter what". This 
//vastly speeds up makebins, at the price of a drop in dependability
bool gbForceBinCreateDoesntForceTextFileReading = false;
AUTO_CMD_INT(gbForceBinCreateDoesntForceTextFileReading, ForceBinCreateDoesntForceTextFileReading) ACMD_CMDLINE;

//The "cached error text file" system is in use. So if you're going to potentially read a bin file, only do so
//if there's an up to date cached error text file for it
bool gbOnlyReadBinFilesIfCachedErrorFileExists = false;
AUTO_CMD_INT(gbOnlyReadBinFilesIfCachedErrorFileExists, OnlyReadBinFilesIfCachedErrorFileExists) ACMD_CMDLINE;


bool gbProductionModeBins = false;
// Assumes bin files are up to date, like in production mode
AUTO_CMD_INT(gbProductionModeBins, productionModeBins) ACMD_CMDLINE ACMD_EARLYCOMMANDLINE ACMD_ACCESSLEVEL(0);

//global flag set during production build testing after makeBinsAndExit has run, to ensure that all
//bins exist
bool gbBinsMustExist = false;
AUTO_CMD_INT(gbBinsMustExist, BinsMustExist) ACMD_CMDLINE;

//special override set by multiplexed makebins... force read the bin file even if FORCE_REBUILD is set
bool gbForceReadBinFilesForMultiplexedMakebins = false;
AUTO_CMD_INT(gbForceReadBinFilesForMultiplexedMakebins, ForceReadBinFilesForMultiplexedMakebins) ACMD_CMDLINE;



AUTO_RUN_ANON(memBudgetAddMapping(__FILE__, BUDGET_Unsorted);); // Should be 0 bytes, all tracked to callers

AUTO_RUN_ANON(memBudgetAddMapping("ThreadStack:TestTextParser_Thread", BUDGET_EngineMisc););

bool gbWriteLayoutFiles = false;
AUTO_CMD_INT(gbWriteLayoutFiles, WriteLayoutFiles) ACMD_CMDLINE;

bool gbWriteTextMetadata = false;
AUTO_CMD_INT(gbWriteTextMetadata, WriteTextMetadata) ACMD_CMDLINE;

bool gbDontCheckIfTablesAreValidForWriting = false;
AUTO_CMD_INT(gbDontCheckIfTablesAreValidForWriting, DontCheckIfTablesAreValidForWriting) ACMD_CMDLINE;

//if this is set, and if you end up reading text files and writing a binary file, then write a cached error file to 
//this folder
static char *spFolderForCachedErrorTextFiles = NULL;
AUTO_CMD_ESTRING(spFolderForCachedErrorTextFiles, FolderForCachedErrorTextFiles) ACMD_CMDLINE;

// List of dictionaries to validate in the near future.
//
// A bundle of files can get saved together and need to not get
// validated until they ALL get reloaded.
static const char** gQueuedDictsToValidate = NULL;

// Do not validate the dicts in gQueuedDictsToValidate until this reaches 0.
//
// Fixes a problem where file reloads of a bundle gets split across
// mulitple server frames.
static S64 gQueuedDictsTicksToValidate;

//Prototypes for TextParser Use
const char*		MultiValTypeToString(MultiValType t);			// Returns 4 byte (3 char + NULL) representation of type
MultiValType	MultiValTypeFromString(const char* str);		// Returns type from 4 byte (3 char + NULL) representation
void ClearParameters(TokenizerHandle tok);
void VerifyParametersClear(TokenizerHandle tok, TextParserResult *parseResult);

#define HAS_ERRORS_DEP_VALUE "HasErrorsDepValue"

#undef ParserAllocStruct


CRITICAL_SECTION gTextParserGlobalLock;
CRITICAL_SECTION gTextWriteValidityCheckLock;
CRITICAL_SECTION gTextParserCRCLock;
CRITICAL_SECTION gTextParserFormatStringCritSec;

void TextParserPreAutoRunInit(void)
{
	static bool bInitted = false;

	if (!bInitted)
	{
		bInitted  = true;
		InitializeCriticalSection(&gTextParserGlobalLock);
		InitializeCriticalSection(&gTextWriteValidityCheckLock);
		InitializeCriticalSection(&gTextParserCRCLock);
		InitializeCriticalSection(&gTextParserFormatStringCritSec);
		StructInitPreAutoRunInit();
	}
}

// Forward declares:
void nonarray_applydynop(ParseTable tpi[], int column, void* dstStruct, void* srcStruct, int index, DynOpType optype, const F32* values, U8 uiValuesSpecd, U32* seed);
void preparesharedmemoryforfixupStruct(ParseTable pti[], void *structptr, char **ppFixupData);
void fixupSharedMemoryStruct(ParseTable pti[], void *structptr, char **ppFixupData);

//pPreExistingBinFileDependencies is for situations where we know ahead of time that there will
//be a dependency for the bin file. For instance, the message translation bin files depend on the
//translation text files. Note that the VALUE of the dependency passed in is irrelevant, just the type
//and name matter, because the value is calculated in code and checked against the value in the bin file
static bool ParserLoadFilesInternal(const char* dirs, const char* filemask, const char* persistfile, 
	const char* metadatafile, int flags, ParseTable pti[], void* structptr, DictionaryHandleOrName dictHandle, 
	bool doNotBin,
	DependencyList preExistingBinFileDependencies);


static void Free(void* p) { free(p); }


void TextParserState_Init(TextParserState *pTps)
{
	ANALYSIS_ASSUME(pTps);
	ZeroStruct(pTps);
	pTps->lf_loadedok = 1;
	pTps->lf_forcebincreate = gbForceBinCreate;
	pTps->lf_forceBinCreateDoesntForceTextFileReading = gbForceBinCreateDoesntForceTextFileReading;
}

void TextParserState_Destroy(TextParserState *pTps)
{
	PERFINFO_AUTO_START_FUNC();
	FileListDestroy(&pTps->parselist);
	FileListDestroy(&pTps->FilesWithErrors);
	DependencyListDestroy(&pTps->deplist);
	eaDestroy(&pTps->lf_filemasks);
	eaDestroy(&pTps->lf_dirs);
	SAFE_FREE(pTps->old_dirs);
	SAFE_FREE(pTps->old_filemask);
	PERFINFO_AUTO_STOP();
}


//useful when tracking down redundant names... this will return true if the two
//entries are a column and its redundant name, or two redundant names for the same column, etc
static __forceinline bool TwoColumnsAreRedundantCopies(ParseTable *pTPI, int iColumn1, int iColumn2)
{
	if (TOK_GET_TYPE(pTPI[iColumn1].type) != TOK_GET_TYPE(pTPI[iColumn2].type))
	{
		return false;
	}

	if (pTPI[iColumn1].storeoffset != pTPI[iColumn2].storeoffset)
	{
		return false;
	}

	//for bitfields, also need to compare param, because they can have the same storeoffset
	if (TOK_GET_TYPE(pTPI[iColumn1].type) == TOK_BIT)
	{
		if (pTPI[iColumn1].param != pTPI[iColumn2].param)
		{
			return false;
		}
	}

	return true;
}



int GetNonRedundantColumnNumFromRedundantColumn(ParseTable *pTPI, int iRedundantColumn)
{
	int i;

	FORALL_PARSETABLE(pTPI, i)
	{
		if (i != iRedundantColumn)
		{
			if (TwoColumnsAreRedundantCopies(pTPI, i, iRedundantColumn))
			{
				return i;
			}
		}
	}

	assertmsgf(0, "Couldn't find non redundant column num from redundant column named %s", pTPI[iRedundantColumn].name);

	return 0;
}



//////////////////////////////////////////////////////////////////////////
// Memory tracking
//////////////////////////////////////////////////////////////////////////

/*
static StashTable alloc_struct_tracker = 0;

typedef struct ParserTracker
{
	int alloc_count, free_count;
	size_t total_size;
} ParserTracker;

MP_DEFINE(ParserTracker);

typedef struct StructAlloc
{
	const char *str;
	ParserTracker *track;
} StructAlloc;

static StructAlloc **alloc_array = 0;
static int printStructAlloc(StashElement elem)
{
	ParserTracker *track;
	StructAlloc *sa;
	if (!(track = stashElementGetPointer(elem)))
		return 1;
	sa = malloc(sizeof(StructAlloc));
	sa->str = stashElementGetStringKey(elem);
	sa->track = track;
	eaPush(&alloc_array, sa);
	return 1;
}

static int sacmp(const StructAlloc **sa1, const StructAlloc **sa2)
{
	if ((*sa1)->track->total_size < (*sa2)->track->total_size)
		return -1;
	return (*sa1)->track->total_size > (*sa2)->track->total_size;
}
*/

/*
static char* trackParserMemAlloc(const char *file, int line, const char *structname, size_t size)
{
	static char buf[1024];
	ParserTracker *track;
	char* ret = 0;
	buf[0] = 0;

	STR_COMBINE_BEGIN(buf);
	if (!file)
	{
		STR_COMBINE_CAT("Unknown");
	}
	else
	{
		STR_COMBINE_CAT(file);
		if (line > 0)
		{
			STR_COMBINE_CAT(" (");
			STR_COMBINE_CAT_D(line);
			STR_COMBINE_CAT(")");
		}
	}
	if (structname)
	{
		STR_COMBINE_CAT(" <");
		STR_COMBINE_CAT(structname);
		STR_COMBINE_CAT(">");
	}
	STR_COMBINE_END(buf);

	stashFindPointer( alloc_struct_tracker, buf, &track );
	if (!track)
	{
		MP_CREATE(ParserTracker, 4096);
		track = MP_ALLOC(ParserTracker);
		stashAddPointer(alloc_struct_tracker, buf, track, false);
	}
	track->alloc_count++;
	track->total_size += size;

	stashGetKey(alloc_struct_tracker, buf, &ret);
	return ret;
}

static void trackParserMemFree(char* key, size_t size)
{
	ParserTracker *track = stashFindPointerReturnPointer(alloc_struct_tracker, key);
	if (!track)
	{
		MP_CREATE(ParserTracker, 4096);
		track = MP_ALLOC(ParserTracker);
		stashAddPointer(alloc_struct_tracker, key, track, false);
	}
	track->free_count++;
	track->total_size -= size;
}
*/

////////////////////////////////////////////////////////////////////////////////////// Parser strings and structs

const char *GetCreationCommentFromFileAndLine_dbg(ParseTable pti[], const char *pFunction, const char *pCallerName, int line)
{
	char *pTemp = NULL;
	const char *pRetVal;
	estrStackCreate(&pTemp);
	//start with __ so that we can easily tell the difference between "real" comments and autogen ones
	estrPrintf(&pTemp, "__%s called from %s(%d)", pFunction, pCallerName, line);
	pRetVal = allocAddString(pTemp);
	estrDestroy(&pTemp);
	return pRetVal;
}


void *StructCreateFromStringEscapedWithFileAndLine_dbg(ParseTable pti[], char *str, const char *dataFileName, int iDataLineNum, const char* callerName, int line)
{
	void *created = StructCreate_dbg(pti, MaybeGetCreationCommentFromFileAndLine(pti, callerName, line), callerName, line);
	char *pTempComment = NULL;
	int iRetVal;

	if (!created)
	{
		return NULL;
	}


	if (!dataFileName)
	{
		estrStackCreate(&pTempComment);
		estrPrintf(&pTempComment, "StructCreateFromStringEscaped, type %s, file %s line %d",
			ParserGetTableName(pti), callerName, line);

		iRetVal = ParserReadTextEscapedWithComment(&str,pti,created, 0, pTempComment);

		estrDestroy(&pTempComment);
	}
	else
	{
		iRetVal = ParserReadTextEscapedWithFileAndLine(&str,pti,created, 0, dataFileName, iDataLineNum);

	}

	if (iRetVal)
	{
		return created;
	}
	else
	{
		StructDestroyVoid(pti,created);
		return NULL;
	}
}

void* StructCreateFromString_dbg(ParseTable pti[], char *str, const char* callerName, int line)
{
	void *created = StructCreate_dbg(pti, MaybeGetCreationCommentFromFileAndLine(pti, callerName, line), callerName, line);
	char *pTempComment = NULL;
	int iRetVal;


	if (!created)
	{
		return NULL;
	}

	estrStackCreate(&pTempComment);
	estrPrintf(&pTempComment, "StructCreateFromString, type %s, file %s line %d",
		ParserGetTableName(pti), callerName, line);

	iRetVal = ParserReadTextWithComment(str,pti,created, 0, pTempComment);

	estrDestroy(&pTempComment);


	if (iRetVal)
	{
		return created;
	}
	else
	{
		StructDestroyVoid(pti,created);
		return NULL;
	}
}

void*	StructConvert_dbg(ParseTable sourcepti[], void *source, ParseTable destpti[], const char* callerName, int line)
{
	char *tempStr = NULL;
	void *created;

	if (!source)
		return NULL;

	estrStackCreate(&tempStr);

	created = StructCreate_dbg(destpti,MaybeGetCreationCommentFromFileAndLine(destpti, callerName, line),callerName,line);
	if (created)
	{
		char *pTempComment;
		int iRetVal;
		ParserWriteText(&tempStr, sourcepti, source, 0, 0, 0);

		estrStackCreate(&pTempComment);
		estrPrintf(&pTempComment, "Converting %s to %s. File %s line %d",
			ParserGetTableName(sourcepti), ParserGetTableName(destpti), callerName, line);

		iRetVal = ParserReadTextWithComment(tempStr, destpti, created, 0, pTempComment);
		estrDestroy(&pTempComment);

		if (iRetVal)
		{
			estrDestroy(&tempStr);
			return created;			
		}
		else
		{
			StructDestroyVoid(destpti,created);
			estrDestroy(&tempStr);
			return NULL;
		}
	}
	estrDestroy(&tempStr);
	return created;
}

void*	StructClone_dbg(ParseTable pti[], const void *source, const char *pComment, const char* callerName, int line)
{
	ParseTableInfo *info;
	void *created;

	if (!source)
		return NULL;
	
	info = ParserGetTableInfo(pti);
	if (!pComment)
	{
		pComment = MaybeGetCreationCommentFromFileAndLine(pti, callerName, line);
	}

	created = StructCreate_dbg(pti,pComment,callerName,line);
	if (created)
	{
		if (StructCopyAllVoid(pti,source,created))
		{
			return created;
		}
		else
		{
			StructDestroyVoid(pti,created);
			return NULL;
		}
	}
	return created;
}

void*	StructCloneFields_dbg(ParseTable pti[], const void *source, const char *pComment, const char* callerName, int line)
{
	void *created;

	if (!source)
		return NULL;

	if (!pComment)
	{
		pComment = MaybeGetCreationCommentFromFileAndLine(pti, callerName, line);
	}

	created = StructCreate_dbg(pti,pComment, callerName,line);
	if (created)
	{
		if (StructCopyFieldsVoid(pti,source,created, 0, 0))
		{
			return created;
		}
		else
		{
			StructDestroyVoid(pti,created);
			return NULL;
		}
	}
	return created;
}


// allocate memory for a structure
void*	StructAlloc_dbg(ParseTable pti[], ParseTableInfo *optPtr, const char* callerName, int line)
{
	int iSize;
	const char *pName;

	if(!optPtr)
	{
		optPtr = ParserGetTableInfo(pti);
	}

	if (optPtr)
	{
		if (optPtr->pSingleThreadedMemPool)
		{
			return mpAlloc(*optPtr->pSingleThreadedMemPool);
		}
		else if (optPtr->pThreadSafeMemPool)
		{
			return threadSafeMemoryPoolCalloc(optPtr->pThreadSafeMemPool);
		}

		if (optPtr->bNoMemoryTracking)
		{
			pName = NULL;
		}
		else
		{
			pName = optPtr->name;
		}

		iSize = optPtr->size;
	}
	else
	{
		pName = ParserGetTableName(pti);
		iSize = ParserGetTableSize(pti);
	}

	if (!iSize)
	{
		devassertmsgf(0, "StructAlloc called on a struct that wasn't initialized with ParserSetTableInfo: %s!", pName);
		return 0;
	}

	return _calloc_dbg(iSize, 1,_NORMAL_BLOCK, pName ? pName : callerName, line);
}	

void* StructAllocRaw_dbg(size_t size, const char* callerName, int line)
{
	return _calloc_dbg(size, 1,_NORMAL_BLOCK, callerName, line);

}

void *StructAllocRawCharged_dbg(size_t size, ParseTable *pti, const char* callerName, int line)

{
	ParseTableInfo *pTableInfo = ParserGetTableInfo(pti);

	if (pTableInfo && pTableInfo->pSingleThreadedMemPool)
	{
		assert(size == (size_t)(pTableInfo->size));
		return mpAlloc_dbg(*pTableInfo->pSingleThreadedMemPool, 0, callerName, line);
	}
	else if (pTableInfo && pTableInfo->pThreadSafeMemPool)
	{
		assert(size == (size_t)(pTableInfo->size));
		return threadSafeMemoryPoolCalloc(pTableInfo->pThreadSafeMemPool);
	}
	else
	{
		if (pTableInfo)
		{
			if (pTableInfo->name)
			{
				return _calloc_dbg(size, 1, _NORMAL_BLOCK, pTableInfo->name, LINENUM_FOR_STRUCTS);
			}
		}

		return _calloc_dbg(size, 1, _NORMAL_BLOCK, callerName, line);
	}
}

void _StructFree_internal(ParseTable pti[], void* structptr)
{
#ifdef TOKENSTORE_DETAILED_TIMERS
	{
		ParseTableInfo *info;

		PERFINFO_AUTO_START_FUNC();

		if (pti) {
			info = ParserGetTableInfo(pti);
			PERFINFO_AUTO_START_STATIC(info->name, &info->piStructDestroy, 1);
		}
	}
#endif

	if (structptr && !isSharedMemory(structptr))
	{	

		MemoryPool singleThreadedMemPool;
		ThreadSafeMemoryPool *pThreadSafeMemPool;
		if (pti && (singleThreadedMemPool = ParserGetTPISingleThreadedMemPool(pti)))
		{
			mpFree(singleThreadedMemPool, structptr);

		}
		else if (pti && (pThreadSafeMemPool = ParserGetTPIThreadSafeMemPool(pti)))
		{
			threadSafeMemoryPoolFree(pThreadSafeMemPool, structptr);
		}
		else
		{

			free(structptr);
		}
	}

#ifdef TOKENSTORE_DETAILED_TIMERS
	if (pti) {
		PERFINFO_AUTO_STOP();
	}
	PERFINFO_AUTO_STOP();
#endif
}

static int siHeapForStructStrings = CRYPTIC_FIRST_HEAP;

static bool UseSpecialHeapForStrings_default(void)
{
	if (IsThisObjectDB() && sizeof(void*) == 8)
	{
		return true;
	}

	return false;

}


AUTO_RUN_FIRST;
void InitStructStringHeap(void)
{
	char temp[16];

	if (ParseCommandOutOfCommandLine("SpecialHeapForStructStrings", temp))
	{
		if (atoi(temp))
		{
			siHeapForStructStrings = CRYPTIC_STRUCT_STRING_HEAP;
		}
	}
	else
	{
		if (UseSpecialHeapForStrings_default())
		{
			siHeapForStructStrings = CRYPTIC_STRUCT_STRING_HEAP;
		}
	}
		
}

AUTO_COMMAND;
void SpecialHeapForStructStrings(int iSet)
{
}

// allocate memory for a string and copy string in
char* StructAllocStringLen_dbg(const char* string, int len MEM_DBG_PARMS)
{
	char* ret;

	if (!string)
		return NULL;

	ret = malloc_timed_canfail(len+1, siHeapForStructStrings, false MEM_DBG_PARMS_CALL);
	strncpy_s(ret, len+1, string, len);
	return ret;
}

// release memory for a string
void StructFreeString(char* string)
{
	if (!string)
		return;

#ifdef TOKENSTORE_DETAILED_TIMERS
	PERFINFO_AUTO_START_FUNC();
#endif

	if (isSharedMemory(string))
		return;
	free(string);

#ifdef TOKENSTORE_DETAILED_TIMERS
	PERFINFO_AUTO_STOP();
#endif
}

void StructCopyString_dbg(char** dest, const char* src MEM_DBG_PARMS)
{
	StructFreeStringSafe(dest);
	if (src) {
		*dest = StructAllocString_dbg( src, caller_fname, line ); //< can't use MEM_DBG_PARMS_CALL because of macro expansion
	} else {
		*dest = NULL;
	}
}

void StructFreeFunctionCall(StructFunctionCall* callstruct)
{
	if (callstruct->function)
		StructFreeString(callstruct->function);
	if (callstruct->params)
	{
		eaClearEx(&callstruct->params, StructFreeFunctionCall);
		eaDestroy(&callstruct->params);
	}
	_StructFree_internal(NULL, callstruct);
}




void StructInitFields(ParseTable pti[], void* structptr)
{
	TextParserAutoFixupCB *pFixupCB;
	int i;
	int iCreationCommentOffset;

	PERFINFO_AUTO_START_FUNC();

	FORALL_PARSETABLE(pti, i)
	{
		if (pti[i].type & TOK_UNOWNED)
		{
			continue;
		}

		initstruct_autogen(pti, i, structptr, 0);
	} // each token
	
	if ((iCreationCommentOffset = ParserGetCreationCommentOffset(pti)))
	{
		const char **ppStrPtr;
		
		ppStrPtr = (char**)(((char*)structptr) + iCreationCommentOffset);
		*ppStrPtr = "initted with StructInitFields, which is nonstandard";	

		ErrorOrAlert("NO_CREATION_COMMENT", "An struct of type %s with a creation comment field was initialized with StructInitFields, which is mostly obsolete and doesn't respect the creation comment system",
			ParserGetTableName(pti));		
		
	}

	pFixupCB = ParserGetTableFixupFunc(pti);

	if (pFixupCB)
	{
		pFixupCB(structptr, FIXUPTYPE_CONSTRUCTOR, NULL);
	}

	PERFINFO_AUTO_STOP();
}

void ParserUpdateCRCFunctionCall(StructFunctionCall* callstruct)
{
	int len, i;

	if (callstruct->function)
	{
		len = (int)strlen(callstruct->function);
		cryptAdler32Update(callstruct->function, len);
	}
	else
		cryptAdler32Update((U8*)&callstruct->function, sizeof(char*)); // zero

	for (i = 0; i < eaSize(&callstruct->params); i++)
	{
		ParserUpdateCRCFunctionCall(callstruct->params[i]);
	}
}

// used by StructCRC
void StructUpdateCRC(ParseTable pti[], void* structptr)
{
	int i;
	if (!structptr)
		return;

	IF_DEBUG_CRCS(printf("%sAbout to CRC the fields for %s\n", pDebugCRCPrefix, ParserGetTableName(pti)); estrConcatf(&pDebugCRCPrefix, "  ");)


	// look through token to crc each data member
	FORALL_PARSETABLE(pti, i)
	{
		if (pti[i].type & TOK_REDUNDANTNAME) continue;


		updatecrc_autogen(pti, i, structptr, 0);

		IF_DEBUG_CRCS( printf("%sAfter field %d(%s), CRC: %u\n", pDebugCRCPrefix, i, pti[i].name, cryptAdlerGetCurValue());)
	}

	IF_DEBUG_CRCS (estrSetSize(&pDebugCRCPrefix, estrLength(&pDebugCRCPrefix) - 2);)
}



// get a CRC of all data in structure tree
U32 StructCRC(ParseTable pti[], void* structptr)
{
	U32 iRetVal;
	IF_DEBUG_CRCS ( printf("About to calculate CRC for %s at %p\n", ParserGetTableName(pti), structptr); estrCopy2(&pDebugCRCPrefix, ""); )


	cryptAdler32Init();
	StructUpdateCRC(pti, structptr);
	iRetVal = cryptAdler32Final();

	IF_DEBUG_CRCS (printf("CRC for %s(%p): %u\n",	ParserGetTableName(pti), structptr, iRetVal); )

	return iRetVal;

}

int TokenCompare(ParseTable tpi[], int column, const void *structptr1, const void *structptr2, StructTypeField iOptionFlagsToMatch, StructTypeField iOptionFlagsToExclude)
{
	return compare_autogen(tpi, column, structptr1, structptr2, 0, false, iOptionFlagsToMatch, iOptionFlagsToExclude);
}

/////////////////////////////////////////////////// Parser structure copying (for shared memory)
size_t ParserGetFunctionCallMemoryUsage(StructFunctionCall* callstruct, bool bAbsoluteUsage)
{
	int i;
	size_t memory_usage = sizeof(StructFunctionCall);
	if (callstruct->function)
		memory_usage += (int)strlen(callstruct->function) + 1;
	if (callstruct->params)
	{
		for (i = 0; i < eaSize(&callstruct->params); i++)
			memory_usage += ParserGetFunctionCallMemoryUsage(callstruct->params[i], bAbsoluteUsage);
		memory_usage += eaMemUsage(&callstruct->params, bAbsoluteUsage);
	}
	return memory_usage;
}

static __forceinline size_t StructGetMemoryUsage_inline(ParseTable pti[], const void* structptr, bool bAbsoluteUsage) // Counts how much memory is used to store an entire structure
{
	int i;
	size_t memory_usage = 0;
	PERFINFO_AUTO_START_FUNC();
	FORALL_PARSETABLE(pti, i)
	{
		if (pti[i].type & TOK_REDUNDANTNAME)
			continue;
		memory_usage += memusage_autogen(pti, i, (void*)structptr, 0, bAbsoluteUsage);
	}
	PERFINFO_AUTO_STOP();
	return memory_usage;
}

size_t StructGetMemoryUsage(ParseTable pti[], const void* structptr, bool bAbsoluteUsage) // Counts how much memory is used to store an entire structure
{
	return StructGetMemoryUsage_inline(pti,structptr,bAbsoluteUsage);
}

#if !USE_NEW_STRUCTCOPY
int StructCopyAll(ParseTable pti[], const void* source, void* dest)
{
	int retVal;
	int size;

	if (size = ParserCanTpiBeCopiedFast(pti))
	{
		memcpy(dest, source, size);
		return 1;
	}

	PERFINFO_RUN(
		ParseTableInfo* info = ParserGetTableInfo(pti);
		PERFINFO_AUTO_START("StructCopyAll", 1);
		PERFINFO_AUTO_START_STATIC(info->name, &info->piCopy, 1);
		PERFINFO_AUTO_START("StructDeInit", 1);
	);
	
	StructDeInit(pti, dest);

	PERFINFO_AUTO_STOP_START("StructCompress", 1);

	retVal = !!StructCompress(pti, source, dest, NULL, NULL);

	PERFINFO_RUN(
		PERFINFO_AUTO_STOP();
		PERFINFO_AUTO_STOP();
		PERFINFO_AUTO_STOP();
	);
		
	return retVal;
}

int StructCopyFields(ParseTable pti[], const void* source, void* dest, StructTypeField iOptionFlagsToMatch, StructTypeField iOptionFlagsToExclude)
{
	int i;
	assert(source && dest);

	PERFINFO_AUTO_START_FUNC();

	FORALL_PARSETABLE(pti, i)
	{
		if (pti[i].type & TOK_REDUNDANTNAME)
		{
			continue;
		}

		if (pti[i].type & TOK_UNOWNED)
		{
			continue;
		}

		if (!FlagsMatchAll(pti[i].type,iOptionFlagsToMatch))
		{
			continue;
		}

		if (!FlagsMatchNone(pti[i].type,iOptionFlagsToExclude))
		{
			continue;
		}
		copyfield_autogen(pti, i, dest, (void*)source, 0, NULL, NULL, iOptionFlagsToMatch,iOptionFlagsToExclude);
	}

	PERFINFO_AUTO_STOP();

	return 1;
}
#endif


//given a TPI and a non-redundant-named column, finds all possible names for that column, and allocadds them and pushes
//them into an earray
void FindAllNamesForColumn(ParseTable tpi[], int iColumn, const char ***pppOutNames)
{
	int i;
	U32 iType = TOK_GET_TYPE(tpi[iColumn].type);

	eaPush(pppOutNames, tpi[iColumn].name);

//we have to assume that zero-size types don't have redundant names, because our code depends on comparing offsets
//if we don't do this check, if first field after a "{" has a redundant name, that name gets returned by this algorithm as
//a redundant name for the TOK_START
	if (iType == TOK_IGNORE || iType == TOK_START || iType == TOK_END || iType == TOK_COMMAND)
	{
		return;
	}

	FORALL_PARSETABLE(tpi, i)
	{
		if (tpi[i].type & TOK_REDUNDANTNAME)
		{
			if (TwoColumnsAreRedundantCopies(tpi, i, iColumn))
			{
				eaPush(pppOutNames, tpi[i].name);
			}
		}
	}
}



//given two TPIs and a (non-redundant-named) column number in one, try to find a non-redundant-named column in the other with the
//same name, but respecting redundant names as appropriate. Returns -1 on failure
int FindMatchingColumnInOtherTpi(ParseTable inTpi[], int iInColumn, ParseTable outTpi[])
{
	const char **ppInNames = NULL;
	int iOutColumn;
	const char **ppOutNames = NULL;
	int iRetVal = -1;
	int i, j;

	FindAllNamesForColumn(inTpi, iInColumn, &ppInNames);

	FORALL_PARSETABLE(outTpi, iOutColumn)
	{
		if (outTpi[iOutColumn].type & TOK_REDUNDANTNAME)
		{
			continue;
		}

		FindAllNamesForColumn(outTpi, iOutColumn, &ppOutNames);

		for (i=0; i < eaSize(&ppInNames); i++)
		{
			for (j=0; j < eaSize(&ppOutNames); j++)
			{
				if (ppInNames[i] == ppOutNames[j])
				{
					if (iRetVal == -1)
					{
						iRetVal = iOutColumn;
					}
					else if (iRetVal != iOutColumn)
					{
						Errorf("Found multiple columns with matching redundant names between two TPIs");
						eaDestroy(&ppOutNames);
						eaDestroy(&ppInNames);
						return -1;
					}
				}
			}
		}

		eaDestroy(&ppOutNames);
	}

	eaDestroy(&ppInNames);

	return iRetVal;
}



//any option bits which are so extraodinarily special that if they don't match between 2 fields, no copying can occur
//
//This should really be zero until someone adds something to it, 
//but the compiler complains then, and 1 is a value which is outside the option bits anyhow
#define COPY2TPI_TPI_OPTIONS_WHICH_MUST_MATCH 1



bool CanDoStringifyCopying(StructTokenType eType)
{
	switch (eType)
	{
	case TOK_U8_X:			
	case TOK_INT16_X:	
	case TOK_INT_X:			
	case TOK_INT64_X:		
	case TOK_F32_X:			
	case TOK_STRING_X:		
	case TOK_CURRENTFILE_X:	
	case TOK_TIMESTAMP_X:	
	case TOK_LINENUM_X:	
	case TOK_BOOL_X:		
	case TOK_BOOLFLAG_X:		
	case TOK_QUATPYR_X:		
	case TOK_MATPYR_X:		
	case TOK_FILENAME_X:		
	case TOK_REFERENCE_X:	
	case TOK_BIT:			
	case TOK_MULTIVAL_X:
		return true;
	}
	return false;
}

enumCopy2TpiResult	StructCopyFields2tpis(ParseTable src_tpi[], const void* source, ParseTable dest_tpi[], void* dest, StructTypeField iOptionFlagsToMatch, StructTypeField iOptionFlagsToExclude, char **ppResultString)
{
	int iSrcColumn;
	int iDestColumn;
	enumCopy2TpiResult eRetVal = COPY2TPIRESULT_SUCCESS;

	U32 iSrcCRC;
	U32 iDestCRC;

	assert(source&&dest);

	FORALL_PARSETABLE(src_tpi, iSrcColumn)
	{

		U32 iSrcType = TOK_GET_TYPE(src_tpi[iSrcColumn].type);

		if (iSrcType == TOK_IGNORE || iSrcType == TOK_START || iSrcType == TOK_END)
		{
			continue;
		}


		if (src_tpi[iSrcColumn].type & TOK_REDUNDANTNAME)
		{
			continue;
		}

		if (src_tpi[iSrcColumn].type & TOK_PARSETABLE_INFO)
		{
			continue;
		}

		if (!FlagsMatchAll(src_tpi[iSrcColumn].type,iOptionFlagsToMatch))
		{
			continue;
		}

		if (!FlagsMatchNone(src_tpi[iSrcColumn].type,iOptionFlagsToExclude))
		{
			continue;
		}

		iDestColumn = FindMatchingColumnInOtherTpi(src_tpi, iSrcColumn, dest_tpi);

		if (iDestColumn < 0)
		{
			eRetVal = MAX(eRetVal, COPY2TPIRESULT_UNKNOWN_FIELDS);
			if (ppResultString)
			{
				estrConcatf(ppResultString, "Unknown field %s\n", src_tpi[iSrcColumn].name);
			}
			continue;
		}

		//first check if the column CRCs match. If they do, then we can do the quickest kind of copying
		iSrcCRC = GetCRCFromParseInfoColumn(src_tpi, iSrcColumn, TPICRCFLAG_IGNORE_NAME | TPICRCFLAG_ALWAYS_DESCEND_ONE_LEVEL);
		iDestCRC = GetCRCFromParseInfoColumn(dest_tpi, iDestColumn, TPICRCFLAG_IGNORE_NAME | TPICRCFLAG_ALWAYS_DESCEND_ONE_LEVEL);

		if (iSrcCRC == iDestCRC)
		{
			enumCopy2TpiResult eRecurseResultVal = copyfield2tpis_autogen(src_tpi, iSrcColumn, 0, (void*)source, dest_tpi, iDestColumn, 0, dest, NULL, NULL, iOptionFlagsToMatch,iOptionFlagsToExclude, ppResultString);
			eRetVal = MAX(eRetVal, eRecurseResultVal);
		}
		else
		{
			U32 iSrcStorage = TokenStoreGetStorageType(src_tpi[iSrcColumn].type);
			U32 iDestStorage = TokenStoreGetStorageType(dest_tpi[iDestColumn].type);
			StructTypeField iSrcOptions = src_tpi[iSrcColumn].type & TOK_OPTION_MASK;
			StructTypeField iDestOptions = dest_tpi[iSrcColumn].type & TOK_OPTION_MASK;
			U32 iDestType = TOK_GET_TYPE(dest_tpi[iDestColumn].type);

			//if the two fields are not the same kind of array, it's totally hopeless, so we give up
			if (TokenStoreStorageTypeIsFixedArray(iSrcStorage) != TokenStoreStorageTypeIsFixedArray(iDestStorage)
				|| TokenStoreStorageTypeIsEArray(iSrcStorage) != TokenStoreStorageTypeIsEArray(iDestStorage))
			{
				if (ppResultString)
				{
					estrConcatf(ppResultString, "Non-matching array types for fields %s and %s\n", src_tpi[iSrcColumn].name, dest_tpi[iDestColumn].name);
				}
				eRetVal = MAX(eRetVal, COPY2TPIRESULT_FAILED_FIELDS);
				continue;
			}

			//next check if we have any bits that don't match which, when non-matching, cause failures.
			if ((iSrcOptions ^ iDestOptions) & COPY2TPI_TPI_OPTIONS_WHICH_MUST_MATCH)
			{
				if (ppResultString)
				{
					estrConcatf(ppResultString, "Invalid option bits for fields %s and %s\n", src_tpi[iSrcColumn].name, dest_tpi[iDestColumn].name);
				}
				eRetVal = MAX(eRetVal, COPY2TPIRESULT_FAILED_FIELDS);
				continue;
			}

			//next, check if we have the same basic field types. If so, we try to copy as normal and hope for the best 
			if (iSrcType == iDestType)
			{
				enumCopy2TpiResult eRecurseResultVal = copyfield2tpis_autogen(src_tpi, iSrcColumn, 0, (void*)source, dest_tpi, iDestColumn, 0, dest, NULL, NULL, iOptionFlagsToMatch,iOptionFlagsToExclude, ppResultString);
				eRetVal = MAX(eRetVal, eRecurseResultVal);
			}
			else
			{
				//non-matching types... we'd like to try to copy by stringifying and then de-stringifying. But we only allow that for certain types
				if (CanDoStringifyCopying(iSrcType) && CanDoStringifyCopying(iDestType))
				{
					enumCopy2TpiResult eRecurseResultVal = stringifycopy_autogen(src_tpi, iSrcColumn, 0, (void*)source, dest_tpi, iDestColumn, 0, dest, ppResultString);
					eRetVal = MAX(eRetVal, eRecurseResultVal);
				}
				else
				{
					if (ppResultString)
					{
						estrConcatf(ppResultString, "Non-matching types which do not allow stringify copying for fields %s and %s\n", src_tpi[iSrcColumn].name, dest_tpi[iDestColumn].name);
					}
					eRetVal = MAX(eRetVal, COPY2TPIRESULT_FAILED_FIELDS);
				}
			}
		}
	}

	return eRetVal;
}


// This has to be called because textparser doesn't support sorting when reading
void StructSortIndexedArrays(ParseTable pti[], void* structptr)
{
	int i;
	int keyColumn;

	FORALL_PARSETABLE(pti, i)
	{
		if (ParserColumnIsIndexedEArray(pti, i, &keyColumn))		
		{
			void ***pArray = TokenStoreGetEArray_inline(pti,&pti[i],i,structptr,NULL);
			eaSortUsingKeyVoid(pArray,pti[i].subtable);
		}
	}
}


StructFunctionCall* ParserCompressFunctionCall(StructFunctionCall* src, CustomMemoryAllocator memAllocator, void* customData, ParseTable tpi[])
{
	StructFunctionCall* dst;
	int len, i;

	// allocate struct
	if (memAllocator)
	{
		dst = memAllocator(customData, sizeof(StructFunctionCall));
	}
	else
		dst = StructAllocRawCharged(sizeof(StructFunctionCall), tpi);
	if (!dst)
		return NULL;

	// copy string
	if (src->function)
	{
		if (memAllocator)
		{
			len = (int)strlen(src->function)+1;
			dst->function = memAllocator(customData, len);
			if (!dst->function)
				return dst;
			strcpy_s(dst->function, len, src->function);
		}
		else
			dst->function = StructAllocString(src->function);
	}

	// copy substructures
	if (src->params)
	{
		eaCompress(&dst->params, &src->params, memAllocator, customData);
		for (i = 0; i < eaSize(&src->params); i++)
		{
			dst->params[i] = ParserCompressFunctionCall(src->params[i], memAllocator, customData, tpi);
		}
	}
	return dst;
}

// MAK - made this work with normal ParserXxxStruct semantics when memAllocator is NULL
void *StructCompress(ParseTable pti[], const void* pSrcStruct, void *pDestStruct, CustomMemoryAllocator memAllocator, void *customData)  // Returns the new structure, or NULL if copying failed (ran out of memory?)
{
	int i;
	int iSize = ParserGetTableSize(pti);
	
	PERFINFO_AUTO_START_FUNC();

	assert(pDestStruct || iSize);
	assert(pSrcStruct != pDestStruct);
	if (!pDestStruct) {
		PERFINFO_AUTO_START("create", 1);
		if (memAllocator)
		{
			pDestStruct = memAllocator(customData, iSize);
		}
		else
			pDestStruct = StructAllocVoid(pti);
		PERFINFO_AUTO_STOP();
		if (!pDestStruct)
		{
			PERFINFO_AUTO_STOP();// FUNC.
			return NULL;
		}
	}

	PERFINFO_AUTO_START("Prep", 1);

	if (ParserCanTpiBeCopiedFast(pti))
	{
		memcpy(pDestStruct, pSrcStruct, iSize);
		PERFINFO_AUTO_STOP();
		PERFINFO_AUTO_STOP();// FUNC.
		return pDestStruct;
	}
	
	// Copy the "static" members of the structure
	memcpy(pDestStruct, pSrcStruct, iSize);

	PERFINFO_AUTO_STOP_START("copystruct", 1);
	{
		FORALL_PARSETABLE(pti, i)
		{
			if (pti[i].type & TOK_REDUNDANTNAME)
				continue;
			if (pti[i].type & TOK_UNOWNED)
				continue;
			copystruct_autogen(pti, &pti[i], i, pDestStruct, (void*)pSrcStruct, 0, memAllocator, customData);
		}
	}
	PERFINFO_AUTO_STOP();

	PERFINFO_AUTO_STOP();// FUNC.

	return pDestStruct;
}

// Functions for auto-reloading files

// Assumes the first "string" parameter defined in pti is a "name"
static const char *structGetFirstString(ParseTable *pti, void *structptr)
{
	int i;
	if (!structptr)
		return NULL;
	FORALL_PARSETABLE(pti, i)
	{
		if (TOK_GET_TYPE(pti[i].type) == TOK_STRING_X ||
			TOK_GET_TYPE(pti[i].type) == TOK_CURRENTFILE_X)
			return TokenStoreGetString_inline(pti, &pti[i], i, structptr, 0, NULL);
	}
	printf("Warning: reload called on structure missing any string fields to use as a unique ID!\n");
	return NULL;
}

static int hasTokenType(ParseTable *pti, StructTokenType type)
{
	int i;
	FORALL_PARSETABLE(pti, i)
	{
		if (TOK_GET_TYPE(pti[i].type) == (U32)type)
			return 1;
	}
	return 0;
}

static int StringNeedsEscaping(const char* str)
{
	if (strchr(str, '\n')) return 1;
	if (strchr(str, '\r')) return 1;
	if (strchr(str, '\"')) return 1;
	return 0;
}

static int StringNeedsQuotes(const char* str)
{
	if (*str==0) return 1;   // need quotes around empty string
	if (strchr(str, ' ')) return 1;	// standard delimeters between tokens
	if (strchr(str, ',')) return 1;
	if (strchr(str, '\n')) return 1;
	if (strchr(str, '\r')) return 1;
	if (strchr(str, '\t')) return 1;
	if (strchr(str, '|')) return 1; //causes confusion with earrays of strings
	if (str[0] == '#') return 1; // could cause confusion with comments
	if (str[0] == '/') return 1;
	if (str[0] == '<') return 1; // could cause confusion with escaping
	return 0;
}

__inline void WriteString(FILE* out, const char* str, int tabs, int eol)
{
	if (tabs > 0)
	{
		if (tabs < 32)
			fwrite("\t\t\t\t\t\t\t\t\t\t\t\t\t\t\t\t\t\t\t\t\t\t\t\t\t\t\t\t\t\t\t", tabs, sizeof(char), out);
		else
			while (tabs-- > 0) 
				fwrite("\t", 1, sizeof(char), out);
	}

	fwrite(str, strlen(str), sizeof(char), out);
	if (eol) fwrite("\r\n", 2, sizeof(char), out);
}


void WriteInt64(FILE* out, S64 i, int tabs, int eol, void* subtable)
{
	const char* define = 0;
	char str[21];
	int len = sprintf(str, "%"FORM_LL"d", i);
	assert(len != -1);

	//leaving this with the old unoptimized one on the off chance that someone is doing something insane
	//with 64-bit enums, but I don't think any actually exist
	if (subtable) 
		define = StaticDefineRevLookup((StaticDefine*)subtable, str);
	if (define)
		WriteString(out, define, tabs, eol);
	else
		WriteNString(out, str, len, tabs, eol);
}

void WriteInt(FILE* out, int i, int tabs, int eol, void* subtable)
{
	const char* define = 0;
	char str[20];
	int len;
	
	if (subtable)
	{
		define = StaticDefine_FastIntToString((StaticDefine*)subtable, i);
		if (define)
		{
			WriteString(out, define, tabs, eol);
			return;
		}
	}
	
	len = sprintf(str, "%i", i);
	assert(len != -1);
	WriteNString(out, str, len, tabs, eol);
}

void WriteUInt(FILE* out, unsigned int i, int tabs, int eol, void* subtable)
{
	const char* define = 0;
	char str[20];
	int len;
	
	if (subtable)
	{
		define = StaticDefine_FastIntToString((StaticDefine*)subtable, i);
		if (define)
		{
			WriteString(out, define, tabs, eol);
			return;
		}
	}
	
	len = sprintf(str, "%u", i);
	assert(len != -1);
	
	WriteNString(out, str, len, tabs, eol);
}

void WriteFloat(FILE* out, float f, int tabs, int eol, void* subtable)
{
	const char* define = 0;
	char str[20];
	float af = ABS(f);
	int len;

	if (af < 1000000.f && af >= 1.f)
		len = sprintf(str, "%.6f", f);
	else 
		len = sprintf(str, "%.6g", f);
	assert(len != -1);
	if (subtable) 
		define = StaticDefineRevLookup((StaticDefine*)subtable, str);
	if (define)
		WriteString(out, define, tabs, eol);
	else
		WriteNString(out, str, len, tabs, eol);
}

void WriteFloatHighPrec(FILE* out, float f, int tabs, int eol)
{
	const char* define = 0;
	char str[100];
	int len;

	len = sprintf(str, "%.49g", f);

	WriteNString(out, str, len, tabs, eol);
}


void WriteTextFunctionCalls(FILE* out, StructFunctionCall** structarray)
{
	int escaped, quoted;
	int i, n;

	n = eaSize(&structarray);
	for (i = 0; i < n; i++)
	{
		char* str = structarray[i]->function;
		if (!str) continue; // not really a valid input
		if (i) WriteNString(out, ", ", 2, 0, 0);

		// make sure to quote our string if it has a paren
		escaped = StringNeedsEscaping(str);
		if (escaped)
		{
			int iLen = (int)strlen(str);
			int newlen;
			char *pBuffer = ScratchAlloc(iLen * 2 + 5);
			newlen = TokenizerEscape(pBuffer, str);
			WriteNString(out, pBuffer, newlen, 0, 0);
			ScratchFree(pBuffer);
		}
		else
		{
			quoted = StringNeedsQuotes(str) || strchr(str, '(') || strchr(str, ')');
			if (quoted) WriteNString(out, "\"", 1, 0, 0);
			WriteString(out, str, 0, 0);
			if (quoted) WriteNString(out, "\"", 1, 0, 0);
		}

		// any of my children
		if (structarray[i]->params)
		{
			WriteNString(out, "( ", 2, 0, 0); // space in case we start with escaped string
			WriteTextFunctionCalls(out, structarray[i]->params);
			WriteNString(out, " )", 2, 0, 0);
		}
	}
}


//used when ignoring all unknown fields, and an unknown field name has been read. Skip the rest of the line, then
//a struct following if there is one
void parserDoFieldOrStructIgnoring(TokenizerHandle tok, TextParserResult *parseResult)
{
	ignore_parse(tok, NULL, NULL, 0, NULL, 0, parseResult);
}



// Updates all fields in a structure that exist in pNewStruct, and prunes any that exist
//  in pOldStruct and have the same filename as "relpath"
// relpath is the file that was modified, for checking for orphaned structures
static int ParserReloadStructHelper(const char *relpath, ParseTable pti[], void *pOldStruct, void *pNewStruct, ParserReloadCallback subStructCallback, bool bTopLevel)
{
	int i, sNew, sOld;
	bool didUnshare=false;
	bool bFoundOneCurrentFile=false;
	
	FORALL_PARSETABLE(pti, i)
	{
		if (TOK_GET_TYPE(pti[i].type) == TOK_STRUCT_X) {
			// handle all the elements in this structure
			if (hasTokenType(pti[i].subtable, TOK_CURRENTFILE_X)) 
			{
				bFoundOneCurrentFile = true;
				if (TokenStoreGetStorageType(pti[i].type) == TOK_STORAGE_DIRECT_SINGLE) 
				{
					ParserReloadStructHelper(relpath, pti[i].subtable, 
						TokenStoreGetPointer_inline(pti, &pti[i], i, pOldStruct, 0, NULL), 
						TokenStoreGetPointer_inline(pti, &pti[i], i, pNewStruct, 0, NULL), NULL, false); // just recurse through any top-level embedded structs
				} 
				else 
				{
					const char *oldName, *newName;
					const char *path;
					void **eaRelevantStructs=NULL;
					int iNumElems;

					// list containing all structures in the old data that match our filename
					iNumElems = TokenStoreGetNumElems_inline(pti, &pti[i], i, pOldStruct, NULL);
					for (sOld = 0; sOld < iNumElems; sOld++) 
					{
						void *substructOld = TokenStoreGetPointer_inline(pti, &pti[i], i, pOldStruct, sOld, NULL);
						assert(substructOld);
						path = ParserGetFilename(pti[i].subtable, substructOld);
						if (!path)
						{
							printf("Warning: reload called on structure missing filename field!\n");
						}
						if (stricmp(path, relpath)!=0) // Not in our file
							continue;
						eaPush(&eaRelevantStructs, substructOld);
					}

					// Copy new elements over old elements
					iNumElems = TokenStoreGetNumElems_inline(pti, &pti[i], i, pNewStruct, NULL);
					for (sNew = 0; sNew < iNumElems; sNew++)
					{
						void *substructNew = TokenStoreGetPointer_inline(pti, &pti[i], i, pNewStruct, sNew, NULL);
						bool bFoundOldOne=false;
						assert(substructNew);

						// Find element with this name in the destination list and overwrite it
						newName = structGetFirstString(pti[i].subtable, substructNew);
						path = ParserGetFilename(pti[i].subtable, substructNew);
						if (!path)
						{
							printf("Warning: reload called on structure missing filename field!\n");
						}
						if (stricmp(path, relpath)!=0) {
							printf("Error: reloading for %s, but struct had path of %s\n", relpath, path);
							continue;
						}

						// Look through the old list for this guy
						for (sOld = eaSize(&eaRelevantStructs)-1; sOld >= 0; sOld--) 
						{
							void *substructOld = eaRelevantStructs[sOld];
							oldName = structGetFirstString(pti[i].subtable, substructOld);
							if (stricmp(oldName, newName)==0) {
								void *oldStructCopy = StructAllocVoid(pti[i].subtable);
								// Found him!
								bFoundOldOne = true;
								// Make a copy of the old structure to pass to the reload callback before we clear it 
								StructCopyAllVoid(pti[i].subtable, substructOld, oldStructCopy);
								// Compare check doesn't work because of (at least) CURRENTFILE_TIMESTAMP has changed
								//if (StructCompare(pti[i].subtable, substructOld, substructNew)==0) {
								//	printf("Reload: no changes to existing structure named: %s\n", newName);
								//	break;
								//}
								StructDeInitVoid(pti[i].subtable, substructOld);
								//assert(ParserValidateHeap());

								if (isSharedMemory(substructOld) && !didUnshare) {
									// If in dev mode and something reloads, we want to mark the shared memory as dead
									// so that future dev mode servers start and don't trust shared memory.
									// In production mode we can't support reloads into shared memory!
									if (isDevelopmentMode()) {
										sharedMemoryEnableEditorMode();
										didUnshare = true;
									} else {
										assertmsgf(0, "ParserReloadStructHelper: Attempt to reload shared memory stored object from file %s", relpath); 
									}
								}

								SetRefSystemAllowsNulledActiveHandles(true);
								StructCopyAllVoid(pti[i].subtable, substructNew, substructOld);\
								SetRefSystemAllowsNulledActiveHandles(false);
								eaRemove(&eaRelevantStructs, sOld);
								verbose_printf("Reload: updating existing structure named: %s\n", newName);
								if (subStructCallback && bTopLevel)
									subStructCallback(substructOld, oldStructCopy, pti[i].subtable, eParseReloadCallbackType_Update);
								if (bTopLevel)
									FixupStructLeafFirst(pti[i].subtable, substructOld, FIXUPTYPE_POST_RELOAD, NULL);
								StructDestroyVoid(pti[i].subtable, oldStructCopy);
								break;
							}
						}
						if (!bFoundOldOne) {
							// New structure, add it in!
							int numStructs = TokenStoreGetNumElems_inline(pti, &pti[i], i, pOldStruct, NULL);
							void *substructOld;
							TokenStoreMakeLocalEArray(pti, i, pOldStruct, NULL);
							substructOld = TokenStoreAlloc(pti, i, pOldStruct, numStructs, pti[i].param, NULL, NULL, NULL);
							StructCopyAllVoid(pti[i].subtable, substructNew, substructOld);
							verbose_printf("Reload: adding new structure named: %s\n", newName);
							if (subStructCallback && bTopLevel)
								subStructCallback(substructOld, NULL, pti[i].subtable, eParseReloadCallbackType_Add);
							if (bTopLevel)
								FixupStructLeafFirst(pti[i].subtable, substructOld, FIXUPTYPE_POST_RELOAD, NULL);
						}
					}
					// Search for any old structures that have this filename, but were not modified, remove them from list (leak them)
					if (eaSize(&eaRelevantStructs)) 
					{
						verbose_printf("Reload: pruning %d structures deleted from %s: ", eaSize(&eaRelevantStructs), relpath);
						for (sOld=0; sOld < eaSize(&eaRelevantStructs); sOld++) 
						{
							void *removedStruct = TokenStoreRemovePointer(pti, i, pOldStruct, eaRelevantStructs[sOld], NULL);
							// Could free this with a flag passed in, but for reloading we want to leak it, as
							//  external things might still be pointing at this structure!
							verbose_printf("%s ", structGetFirstString(pti[i].subtable, removedStruct));
							if (subStructCallback && bTopLevel)
								subStructCallback(removedStruct, NULL, pti[i].subtable, eParseReloadCallbackType_Delete);
						}
						verbose_printf("\n");
					}
					eaDestroy(&eaRelevantStructs);
				}
			} else {
				// // Could just recurse, this must be a structure of substructures?
				//printf("Warning: reload called on structure with its substructures missing a TOK_CURRENTFILE field\n");
			}
		} else {
			bool bIgnore=false;
			StructTokenType type = TOK_GET_TYPE(pti[i].type);
			if (type == TOK_STASHTABLE_X && subStructCallback)
				bIgnore = true; // The caller will update his hashtables, presumably
			if (type == TOK_START || type == TOK_END || type == TOK_IGNORE)
				bIgnore = true;
			if (!bIgnore)
				printf("Warning: reload called on structure with unsupported high-level elements\n");
		}
	}
	if (!bFoundOneCurrentFile)
	{
		printf("Warning: reload called on structure with its substructures missing a TOK_CURRENTFILE field\n");
	}
	return 1;
}

// Assumes the following:
// the base structure just contains one or more earrays of substructures
// the sub structures all are set up as such:
//   the first string parameter in the TokenizerParserInfo is a "Name" string, or some kind of
//      unique identifier (this is used when finding old elements that need to get replaced
//		with new ones)
//   somewhere there is defined a TOK_CURRENTFILE parameter (ideally, the second entry)
//   the filename and the "Name" parameter can, in fact, be the same string (assuming only one entry per file, like FX)
// any structure that has the same Filename field as the file reloaded will either
//   be overwritten or removed from the list (but not freed, although a flag for this could
//   be trivially added).
// The callback is called on each reloaded substruct
bool ParserReloadFile(const char *relpath_in, ParseTable pti[], void *pOldStruct, ParserReloadCallback subStructCallback, int flags )
{
	TextParserResult eRet=PARSERESULT_SUCCESS;
	char relpath[CRYPTIC_MAX_PATH];
	void *tempBase;

	tempBase = StructAllocVoid(pti);

	while (relpath_in[0]=='/')
		relpath_in++;
	strcpy(relpath, relpath_in);
	forwardSlashes(relpath);

	if (!ParserLoadFilesInternal(NULL, relpath, NULL, NULL, flags | PARSER_INTERNALLOAD, pti, tempBase, NULL, false, NULL))
	{
		eRet = PARSERESULT_ERROR;
	} else {
		ParserReloadStructHelper(relpath, pti, pOldStruct, tempBase, subStructCallback, true);
	}
	SET_MIN_RESULT(eRet, FixupStructLeafFirst(pti, pOldStruct, FIXUPTYPE_POST_RELOAD, NULL));
	StructDestroyVoid(pti, tempBase);
	return (eRet == PARSERESULT_SUCCESS);
}

int TokenizerParseToken(TokenizerHandle tok, ParseTable tpi[], ParseTable *ptcc, int column, void* structptr, TextParserResult *parseResult)
{
	int done = 0;

	if (TYPE_INFO(ptcc->type).interpretfield(tpi, column, SubtableField) == StaticDefineList)
		TokenizerSetStaticDefines(tok, ptcc->subtable);

	done = parse_autogen(tok, tpi, ptcc, column, structptr, 0, parseResult);
	TokenizerSetStaticDefines(tok, NULL);

	return done; 
}


// public ParserReadTokenizer function
void ParserReadTokenizer(TokenizerHandle tok, ParseTable pti[], void* structptr, 
		bool bMultiplePasses, TextParserResult *parseResult)
{
	char* nexttoken;
	int i;
	int matched;
	int done = 0;
	U32* usedBitField = NULL;
	int usedBitFieldSize = 0;
	const char *pUsedBitFieldFieldName = "none";
	TextParserAutoFixupCB *pFixupCB;
	int iSize = 0;
	bool bAtLeastOneRequiredField = false;
	bool bAtLeastOneStructParam = false;
	TextParserResult tempResult = PARSERESULT_SUCCESS;
	const char *pStartTok = NULL;
	int iDefaultFieldColumn = -1;

	PERFINFO_RUN(
		ParseTableInfo* info = ParserGetTableInfo(pti);
		PERFINFO_AUTO_START_FUNC();
		PERFINFO_AUTO_START_STATIC(info->name, &info->piReadTokenizer, 1);
	)

	if (!parseResult)
	{
		parseResult = &tempResult;
	}

	PERFINFO_AUTO_START("init", 1);
	{
		iDefaultFieldColumn = ParserGetDefaultFieldColumn(pti);

		// init filenames, etc. before parsing through tokens
		FORALL_PARSETABLE(pti, i)
		{
			ParseTable *ptcc = &pti[i];
			StructTypeField field_type = ptcc->type;
			TokenTypeInfo *pInfo = &TYPE_INFO(field_type);

			if (pInfo->preparse)
			{
				pInfo->preparse(pti, i, structptr, tok);
			}
			if (field_type & TOK_USEDFIELD)
			{
				assertmsgf(TOK_GET_TYPE(field_type) == TOK_INT_X, "TPI %s has a used field (column %s) which is not 32-bit ints",
					ParserGetTableName(pti), ptcc->name);
				usedBitField = TokenStoreGetPointer_inline(pti, ptcc, i, structptr, 0, parseResult);
				usedBitFieldSize = TokenStoreGetFixedArraySize_inline(pti, ptcc, i);
				pUsedBitFieldFieldName = ptcc->name;
			}

			if (TOK_GET_TYPE(field_type) == TOK_START)
			{
				pStartTok = ptcc->name;
			}
			
			if (field_type & TOK_REQUIRED)
			{
				bAtLeastOneRequiredField = true;
			}

			iSize = i;
		}
	}
	PERFINFO_AUTO_STOP();

	if (!usedBitField && bAtLeastOneRequiredField)
	{
		usedBitFieldSize = (iSize / 32) + 1;
		usedBitField = _alloca(usedBitFieldSize * sizeof(U32) );
		memset(usedBitField, 0, usedBitFieldSize * sizeof(U32) );
	}

	// grab struct param fields immediately following the structure name
	FORALL_PARSETABLE(pti, i)
	{
		ParseTable *ptcc = &pti[i];
		StructTypeField field_type = ptcc->type;

		if ((field_type & TOK_STRUCTPARAM) && (!(field_type & TOK_UNOWNED))) 
		{
			if (field_type & TOK_REDUNDANTNAME)
			{
				if (usedBitField)
				{
					assertmsgf(i < usedBitFieldSize * 32, "pti %s has an insufficiently sized used bit field array %s",
						ParserGetTableName(pti), pUsedBitFieldFieldName);
					SETB(usedBitField, i);
				}
			}
			else
			{
				bAtLeastOneStructParam = true;
				nexttoken = TokenizerPeek(tok, PEEKFLAG_IGNORECOMMA, parseResult);
				if (nexttoken && !IsEol(nexttoken))
				{
					if (usedBitField)
					{
						assertmsgf(i < usedBitFieldSize * 32, "pti %s has an insufficiently sized used bit field array %s",
							ParserGetTableName(pti), pUsedBitFieldFieldName);

						SETB(usedBitField, i);
					}
					if (TokenizerParseToken(tok, pti, ptcc, i, structptr, parseResult))
					{
						done = true;
					}
				}
			}
		}
	}

	// allow a structure to close with eol
	FORALL_PARSETABLE(pti, i)
	{
		ParseTable *ptcc = &pti[i];
		if ((TOK_GET_TYPE(ptcc->type) == TOK_END) && ptcc->name && ptcc->name[0] == '\n' && !ptcc->name[1])
			done = 1;
		// we don't eat the eol here - this allows for structures to be params to other structures
	}

	//special case code to make auto_structed structs work without {} when they are nothing but
	//structparams
	//
	//if we got at least one structparam and have a start token of { and the next token doesn't match the start token, then
	//we are done
	//
	//need to do this even without structparams now to keep compatilbity between reading/writing.
	if (bAtLeastOneStructParam && pStartTok && stricmp(pStartTok, "{") == 0)
	{
		nexttoken = TokenizerPeek(tok, PEEKFLAG_IGNORELINEBREAK, parseResult);
		if (nexttoken && stricmp(nexttoken, pStartTok) != 0)
		{
			done = true;
		}
	}
	while (!done) // main loop
	{
		// try to match in our list
		bool bMatchedColumnZeroIgnore = false;
		bool bDontEatName = false;
		matched = 0;
		nexttoken = TokenizerPeek(tok, PEEKFLAG_IGNORELINEBREAK | PEEKFLAG_IGNORECOMMA, parseResult);
		if (!nexttoken) break; // ran out of tokens

		FORALL_PARSETABLE(pti, i)
		{
			ParseTable *ptcc = &pti[i];
			StructTypeField field_type = ptcc->type;

			if (!(field_type & (TOK_PARSETABLE_INFO | TOK_UNOWNED)))
			{
				if (stricmp(nexttoken, ptcc->name) == 0)
				{
					//special case... if column 0 is an ignore field and matches this token, we need to check if 
					//have another field that also matches it. This is because parsetables that get sent in packets
					//have their PARSE_TABLE_INFO field converted into a normal IGNORE, and there's no rule against
					//a TPI having the same name as one of its columns. Thus you can have column zero named "foo" and
					//column 7 named "foo", with column zero being an ignore, and we want to read with column 7
					if (i == 0 && TOK_GET_TYPE(field_type) == TOK_IGNORE)
					{
						bMatchedColumnZeroIgnore = true;
					}
					else
					{
						matched = 1;
						break;
					}
				}
			}
		}

		if (!matched && bMatchedColumnZeroIgnore)
		{
			matched = 1;
			i = 0;
		}

		//if we got a token, but it's one we don't recognize, and we have a DEFAULT_FIELD, then we want to 
		//parse what we see as the default field
		if (!matched && iDefaultFieldColumn != -1)
		{
			char *pTempTok;

			matched = 1;
			i = iDefaultFieldColumn;
			bDontEatName = true;

			//have to do something a bit kludgy here becuse the next token that is read will be read without PEEKFLAG_IGNORELINEBREAK,
			//so if we are currently pointing at a newline and then a token, we need to eat all the newlines so we end up just
			//as we would have had we just eaten a name
			while (1)
			{
				pTempTok = TokenizerPeek(tok, PEEKFLAG_IGNORECOMMA, parseResult);

				//should be impossible to run off EOF here, since we already know there's at least one token coming

				if (StringIsAllWhiteSpace(pTempTok))
				{
					TokenizerGet(tok, PEEKFLAG_IGNORECOMMA, parseResult);
				}
				else
				{
					break;
				}
			}
		}

		if (!matched)	// unknown token
		{
			if (TokenizerGetFlags(tok) & PARSER_IGNORE_ALL_UNKNOWN)
			{
				char *pTemp = NULL;
				estrStackCreate(&pTemp);
				estrPrintf(&pTemp, "%s/%s", ParserGetTableName(pti), nexttoken);
				TokenizerReportUnknown(tok, pTemp);
				estrDestroy(&pTemp);
				parserDoFieldOrStructIgnoring(tok, parseResult);
			}
			else
			{
				TokenizerErrorf(tok,"Unrecognized token %s", TokenizerGet(tok, PEEKFLAG_IGNORECOMMA | PEEKFLAG_IGNORELINEBREAK, parseResult));
				SET_INVALID_RESULT(*parseResult);
			}
		}
		else
		{
			// Note that "i" falls through from above blocks
			ParseTable *ptcc = &pti[i];

			if (usedBitField)
			{
				if (ptcc->type & TOK_REDUNDANTNAME)
				{
					int iBitNum = GetNonRedundantColumnNumFromRedundantColumn(pti, i);
					assertmsgf(iBitNum < usedBitFieldSize * 32, "pti %s has an insufficiently sized used bit field array %s",
						ParserGetTableName(pti), pUsedBitFieldFieldName);

					SETB(usedBitField, iBitNum);
				}
				else
				{
					assertmsgf(i < usedBitFieldSize * 32, "pti %s has an insufficiently sized used bit field array %s",
						ParserGetTableName(pti), pUsedBitFieldFieldName);

					SETB(usedBitField, i);
				}
			}

			if (!bDontEatName)
			{
				TokenizerGet(tok, PEEKFLAG_IGNORECOMMA | PEEKFLAG_IGNORELINEBREAK, parseResult); // get rid of the name
			}

			if (TokenizerParseToken(tok, pti, ptcc, i, structptr, parseResult))
			{
				done = 1;
			}
			if (!done && (!TOK_HAS_SUBTABLE(ptcc->type)) ) 
			{
				VerifyParametersClear(tok, parseResult); // clear the eol
			}
		}
	} // while !done

	// swapped post text read and required field check so that you at least get the
	// errors about a field being missing before you get the errors about that field
	if (bAtLeastOneRequiredField)
	{
		FORALL_PARSETABLE(pti, i)
		{
			ParseTable *ptcc = &pti[i];

			if (ptcc->type & TOK_REQUIRED)
			{
				assertmsgf(i < usedBitFieldSize * 32, "pti %s has an insufficiently sized used bit field array %s",
					ParserGetTableName(pti), pUsedBitFieldFieldName);


				if (!TSTB(usedBitField, i))
				{
					TokenizerErrorf(tok,"Couldn't find required field %s",ptcc->name);
					SET_INVALID_RESULT(*parseResult);
				}
			}
		}
	}

	if (!bMultiplePasses)
	{
		StructSortIndexedArrays(pti, structptr);

		pFixupCB = ParserGetTableFixupFunc(pti);
		if (pFixupCB)
		{
			TextParserResult res = pFixupCB(structptr, FIXUPTYPE_POST_TEXT_READ, TokenizerGetTextParserState(tok));
			if (res != PARSERESULT_SUCCESS)
			{
				//TokenizerErrorf(tok, 
				verbose_printf("Got a failure from a fixup POST_TEXT_READ callback for type %s\n",
					ParserGetTableName(pti));
			}

			MIN1(*parseResult,res);
		}
	}

	TokenizerDoSpecialIgnoreCallbacks(tok, structptr);

	PERFINFO_AUTO_STOP();// struct name.
	PERFINFO_AUTO_STOP();// FUNC.
}

bool FieldFailsHTMLAccessLevelCheck(ParseTable tpi[], int column, void *pStruct)
{
	int iFieldAccessLevel;
	int bCommand = false;

	if (GetIntFromTPIFormatString(&tpi[column], "Command", &bCommand) && bCommand)
	{
		if (cmdGetAccessLevelFromFullString(&gGlobalCmdList, TokenStoreGetString_inline(tpi, &tpi[column], column, pStruct, 0, NULL)) > GetHTMLAccessLevel())
		{
			return true;
		}
	}

	if (TOK_GET_TYPE(tpi[column].type) == TOK_COMMAND)
	{
		if (cmdGetAccessLevelFromFullString(&gGlobalCmdList, (const char*)tpi[column].param) > GetHTMLAccessLevel())
		{
			return true;
		}

	}

	if (!GetIntFromTPIFormatString(&tpi[column], "AccessLevel", &iFieldAccessLevel))
	{
		return false;
	}

	if (iFieldAccessLevel > GetHTMLAccessLevel())
	{
		return true;
	}

	return false;
}

int InnerWriteTextToken(FILE* out, ParseTable tpi[], int column, void* structptr, int level, bool showname, WriteTextFlags iWriteTextFlags, StructTypeField iOptionFlagsToMatch, StructTypeField iOptionFlagsToExclude)
{
	writetext_autogen(out, tpi, column, structptr, 0, showname, 0, level,iWriteTextFlags,iOptionFlagsToMatch,iOptionFlagsToExclude);
	return 1;
}

void CheckIfTableIsValidForItsParentsWriting(ParseTable tpi[], ParseTableInfo *pInfo)
{
	int i;
	
	bool bEndIsSlashN = false;
	bool bHasAtLeastOneNonStructParam = false;

	pInfo->bMyParentsAreInvalidForWriting_HasBeenSet = true;

	FORALL_PARSETABLE(tpi, i)
	{
		if (TOK_GET_TYPE(tpi[i].type) == TOK_END)
		{
			if (stricmp_safe(tpi[i].name, "\n") == 0)
			{
				bEndIsSlashN = true;
			}
			continue;
		}

		if (tpi[i].type & TOK_REDUNDANTNAME) continue;
		if (tpi[i].type & TOK_STRUCTPARAM) continue;
		if (tpi[i].type & TOK_USEDFIELD) continue;
		if (TOK_GET_TYPE(tpi[i].type) == TOK_IGNORE) continue;
		if (TOK_GET_TYPE(tpi[i].type) == TOK_START) continue;
		if (!tpi[i].name || !tpi[i].name[0]) continue;

		if (!(tpi[i].type & TOK_STRUCTPARAM))
		{
			bHasAtLeastOneNonStructParam = true;
		}
	}

	if (bHasAtLeastOneNonStructParam && bEndIsSlashN)
	{
		pInfo->bMyParentsAreInvalidForWriting = true;
	}
}



void CheckIfTableIsValidForWriting(ParseTable tpi[])
{
	ParseTableInfo *pInfo = ParserGetTableInfo(tpi);
	int i;

	if (!pInfo->bDidWritingValidityCheck)
	{
		EnterCriticalSection(&gTextWriteValidityCheckLock);
		if (!pInfo->bDidWritingValidityCheck)
		{
			FORALL_PARSETABLE(tpi, i)
			{	
				if (TOK_HAS_SUBTABLE(tpi[i].type))
				{
					ParseTable *pSubTable = (ParseTable*)(tpi[i].subtable);
					ParseTableInfo *pSubTableInfo = ParserGetTableInfo(pSubTable);
	
					if (tpi[i].type & TOK_REDUNDANTNAME) continue;
					if (!tpi[i].name || !tpi[i].name[0]) continue;	


					if (!pSubTableInfo->bMyParentsAreInvalidForWriting_HasBeenSet)
					{
						CheckIfTableIsValidForItsParentsWriting(pSubTable, pSubTableInfo);
					}

					if (pSubTableInfo->bMyParentsAreInvalidForWriting)
					{
						AssertOrAlert("BAD_TPI_WRITING", "Someone is trying to write a string using %s. This is illegal because it has a child, %s, which has one or more non-struct-param fields and \"\\n\" as its end string",
							ParserGetTableName(tpi), ParserGetTableName(pSubTable));
					}
				}
			}

			pInfo->bDidWritingValidityCheck = true;
		}

		LeaveCriticalSection(&gTextWriteValidityCheckLock);
	}
}





bool InnerWriteTextFile(FILE* out, ParseTable pti[], void* structptr, int level, bool ignoreInherited, WriteTextFlags iWriteTextFlags, StructTypeField iOptionFlagsToMatch, StructTypeField iOptionFlagsToExclude)
{
	int i;
	TextParserResult eResult = PARSERESULT_SUCCESS;
	int startedbody = 0;
	int inheritanceColumn = -1;
	ParseTableInfo *pTableInfo = ParserGetTableInfo(pti);

	TextParserAutoFixupCB *pFixupCB;

	//in order to figure out if certain things have been written, we track the offset of the file at various points
	S64 iOffsetBeforeStructParams;
	S64 iOffsetAfterWritingLastNonEmptyStructParam;
	S64 iOffsetBeforeStartTok;
	S64 iOffsetAfterStartTok;
	S64 iOffsetBeforeEndTok;
	bool bWroteAtLeastOneStructParam = false;
	bool bDontWriteEndTokenIfNothingElseWritten = (iWriteTextFlags & WRITETEXTFLAG_DONTWRITEENDTOKENIFNOTHINGELSEWRITTEN) ? 1 : 0;
	bool bDontWriteEOLIfNothingElseWritten = (iWriteTextFlags & WRITETEXTFLAG_DONTWRITEEOLIFNOTHINGELSEWRITTEN) ? 1 : 0;
	bool bYouAreADirectEmbed = (iWriteTextFlags & WRITETEXTFLAG_DIRECTEMBED) ? 1 : 0;

	//checks if a particular flag has been set saying "no writing with this table"
	CheckForIllegalTableWriting(NULL, NULL, pti);

	//there are a few ways that a TPI can be set up which make it not valid for text read/write... check
	//for them, but only once per tpi. Don't do this check if writing is for display only, as in that case we don't
	//care if it can be read back in
	if (!gbDontCheckIfTablesAreValidForWriting && !(iWriteTextFlags & WRITETEXTFLAG_WRITINGFORDISPLAY))
	{
		CheckIfTableIsValidForWriting(pti);
	}

	iWriteTextFlags |= WRITETEXTFLAG_STRUCT_BEING_WRITTEN;

	iWriteTextFlags &= ~(WRITETEXTFLAG_DIRECTEMBED | WRITETEXTFLAG_DONTWRITEENDTOKENIFNOTHINGELSEWRITTEN | WRITETEXTFLAG_DONTWRITEEOLIFNOTHINGELSEWRITTEN);

	pFixupCB = ParserGetTableFixupFunc(pti);


	if (pFixupCB)
	{
		SET_MIN_RESULT(eResult, pFixupCB(structptr, FIXUPTYPE_PRE_TEXT_WRITE, NULL));
	}

// don't write the initial eol until we need to (this is for single-line structs)
#define START_BODY	if (!startedbody && level >= 0) { WriteNString(out, NULL, 0, 0, 1); startedbody = 1; }

	iOffsetBeforeStructParams = ftell(out);
	iOffsetAfterWritingLastNonEmptyStructParam = ftell(out);

/*some trickery is required to get structparams to properly NOT display if they are default, because if there are
multiple in a row, you don't know whether you need to write them unless you know whether you need to write later
ones. That is, if you have "mystruct 1 2 3", and the last one becomes 0, you can write "mystruct 1 2", but if the
first one becomes zero, you still need to write it: "mystruct 0 2 3". So we use the return values from foo_writetext,
which hopefully return true if something "Real" was written and false otherwise, and then we fseek back over
any structparams that turned out to be unneeded*/

	// write any structure parameters
	FORALL_PARSETABLE(pti, i)
	{
		if (pti[i].type & TOK_USEDFIELD) continue;
		if (pti[i].type & TOK_REDUNDANTNAME) continue;
		if (pti[i].type & TOK_UNOWNED && !((iWriteTextFlags & WRITETEXTFLAG_WRITINGFORHTML) && GetBoolFromTPIFormatString(&pti[i], "HTML_WRITE_UNOWNED"))) continue;
		if (!FlagsMatchAll(pti[i].type,iOptionFlagsToMatch)) continue;
		if (!FlagsMatchNone(pti[i].type,iOptionFlagsToExclude)) continue;
		if ((iWriteTextFlags & WRITETEXTFLAG_USEHTMLACCESSLEVEL) && FieldFailsHTMLAccessLevelCheck(pti, i, structptr)) continue;


		//this is as good a place as any to put in our check for writing structs which are not allowed to be written (dev mode only)
		if (TOK_HAS_SUBTABLE(pti[i].type))
		{
			CheckForIllegalTableWriting(pti, pti[i].name, pti[i].subtable);
		}

		if (pti[i].type & TOK_STRUCTPARAM)
		{
			if (writetext_autogen(out, pti, i, structptr, 0, 0, ignoreInherited, level, iWriteTextFlags, iOptionFlagsToMatch,iOptionFlagsToExclude))
			{
				iOffsetAfterWritingLastNonEmptyStructParam = ftell(out);
			}
		}
	}
	

	if (iOffsetAfterWritingLastNonEmptyStructParam != ftell(out))
	{
		fseek_and_set(out, iOffsetAfterWritingLastNonEmptyStructParam, ' ');
	}

	if (iOffsetBeforeStructParams != ftell(out))
	{
		bWroteAtLeastOneStructParam = true;
	}

	iOffsetBeforeStartTok = ftell(out);
		

	// write the start token
	FORALL_PARSETABLE(pti, i)
	{
		if (pti[i].type & TOK_REDUNDANTNAME) continue;
		if (TOK_GET_TYPE(pti[i].type) == TOK_START)
		{
			START_BODY;
			WriteNString(out, pti[i].name, ParseTableColumnNameLen(pti,i), level, 1);
			break;
		}
	}

	iOffsetAfterStartTok = ftell(out);

	if (ignoreInherited)
	{
		inheritanceColumn = StructInherit_GetInheritanceDataColumn(pti);
		if (inheritanceColumn >= 0)
		{
			if (!StructInherit_GetParentName(pti, structptr))
			{
				// Not active, disable check
				inheritanceColumn = -1;
			}
		}
	}

	// write each field
	FORALL_PARSETABLE(pti, i)
	{
		if (pti[i].type & TOK_REDUNDANTNAME) continue;
		if (pti[i].type & TOK_STRUCTPARAM) continue;
		if (pti[i].type & TOK_UNOWNED && !((iWriteTextFlags & WRITETEXTFLAG_WRITINGFORHTML) && (GetBoolFromTPIFormatString(&pti[i], "HTML_WRITE_UNOWNED") || GetStringFromTPIFormatString(&pti[i], "HTML_CONVERT_OPT_STRUCT_TO_KEY_STRING", NULL)))) continue;
		if (pti[i].type & TOK_USEDFIELD) continue;
		if (TOK_GET_TYPE(pti[i].type) == TOK_END) continue;
		if (TOK_GET_TYPE(pti[i].type) == TOK_IGNORE) continue;
		if (TOK_GET_TYPE(pti[i].type) == TOK_CURRENTFILE_X && !(iWriteTextFlags & WRITETEXTFLAG_FORCEWRITECURRENTFILE)) continue;
		if (!pti[i].name || !pti[i].name[0]) continue; // unnamed fields shouldn't be parsed or written
		if (!FlagsMatchAll(pti[i].type,iOptionFlagsToMatch)) continue;
		if (!FlagsMatchNone(pti[i].type,iOptionFlagsToExclude)) continue;
		if (inheritanceColumn >= 0 && i != inheritanceColumn) continue;
		if ((iWriteTextFlags & WRITETEXTFLAG_USEHTMLACCESSLEVEL) && FieldFailsHTMLAccessLevelCheck(pti, i, structptr)) continue;
		if ((iWriteTextFlags & WRITETEXTLFAG_ONLY_WRITE_USEDFIELDS) && !TokenIsSpecifiedEx(pti, i, structptr, -1, true)) continue;

		START_BODY;
		writetext_autogen(out, pti, i, structptr, 0, 1, ignoreInherited, level,iWriteTextFlags, iOptionFlagsToMatch,iOptionFlagsToExclude);
	}


	iOffsetBeforeEndTok = ftell(out);

	//we wrote at least one struct param, and a startt ok, and no "actual" fields. So get rid of the start tok, and write no end tok
	// This change temporarily backed out because fseek doesn't work on write-only "string wrapper files" allocated in memory
	//
	//addendum: looks like the bWroteAtLeastOneStructParam check isn't necessary for direct embedded structs
	if ((bWroteAtLeastOneStructParam || bYouAreADirectEmbed) && iOffsetBeforeEndTok == iOffsetAfterStartTok && iOffsetBeforeStartTok != iOffsetAfterStartTok)
	{
		fseek_and_set(out, iOffsetBeforeStartTok, ' ');
		WriteNString(out, NULL, 0, 0, (bDontWriteEOLIfNothingElseWritten && !bWroteAtLeastOneStructParam) ? 0 : 1);	
	}
	else if (bDontWriteEndTokenIfNothingElseWritten && (iOffsetBeforeEndTok == iOffsetBeforeStructParams))
	{
	}
	else
	{
		// write the end token
		FORALL_PARSETABLE(pti, i)
		{
			if (pti[i].type & TOK_REDUNDANTNAME) continue;
			if (TOK_GET_TYPE(pti[i].type) == TOK_END)
			{
				if (pti[i].name && pti[i].name[0] == '\n' && !pti[i].name[1])
				{
					WriteNString(out, NULL, 0, 0, 1);
				}
				else
				{
					// multi-line struct
					START_BODY;
					WriteNString(out, pti[i].name,ParseTableColumnNameLen(pti,i), level, 1);
				}
				break;
			}
		}
	}

	return eResult == PARSERESULT_SUCCESS;
}

int ParserWriteTextFileAppend(const char* filename, ParseTable pti[], void* structptr, StructTypeField iOptionFlagsToMatch, StructTypeField iOptionFlagsToExclude)
{
	int ok = 1;
	FILE* out;
	int ret;

	PERFINFO_AUTO_START_FUNC();

	// init file
	makeDirectoriesForFile(filename);
	if (fileIsAbsolutePath(filename))
		out = fopen(filename, "ab");
	else
		out = fileOpen(filename, "ab");
	if (!out) 
	{
		Errorf("Couldn't open file %s for writing data of type %s", filename, ParserGetTableName(pti));
		PERFINFO_AUTO_STOP();
		return 0;
	}
	ok = InnerWriteTextFile(out, pti, structptr, -1, true, 0, iOptionFlagsToMatch, iOptionFlagsToExclude | TOK_NO_TEXT_SAVE);
	if (ferror(out))
	{
		Errorf("I/O error on file %s after writing data of type %s", filename, ParserGetTableName(pti));
		ok = 0;
	}
	ret = fclose(out);
	if (ret)
	{
		Errorf("Couldn't close file %s after writing data of type %s", filename, ParserGetTableName(pti));
		PERFINFO_AUTO_STOP();
		return 0;
	}
	PERFINFO_AUTO_STOP();
	return ok;
}

int ParserWriteTextFileEx(const char* filename, ParseTable pti[], void* structptr, WriteTextFlags iWriteTextFlages, 
	StructTypeField iOptionFlagsToMatch, StructTypeField iOptionFlagsToExclude, const char *pPrefixString, const char *pSuffixString)
{
	int ok = 1;
	FILE* out;

	PERFINFO_AUTO_START_FUNC();

	// init file
	makeDirectoriesForFile(filename);
	if (fileIsAbsolutePath(filename))
		out = fopen(filename, "!wbm");
	else
		out = fileOpen(filename, "!wbm");
	if (!out) 
	{
		Errorf("Couldn't open file %s for writing data of type %s", filename, ParserGetTableName(pti));
		PERFINFO_AUTO_STOP();
		return 0;
	}

	if (pPrefixString)
	{
		fprintf(out, "%s", pPrefixString);
	}

	ok = InnerWriteTextFile(out, pti, structptr, -1, true, iWriteTextFlages, iOptionFlagsToMatch, iOptionFlagsToExclude | TOK_NO_TEXT_SAVE);


	if (pSuffixString)
	{
		fprintf(out, "%s", pSuffixString);
	}

	fclose(out);
	
	PERFINFO_AUTO_STOP();
	return ok;
}

int ParserWriteZippedTextFile(const char* filename, ParseTable pti[], void* structptr, StructTypeField iOptionFlagsToMatch, StructTypeField iOptionFlagsToExclude)
{
	char *pStr = NULL;
	char absFile[CRYPTIC_MAX_PATH];
	int iRetVal;
	FILE *pOutFile;
	iRetVal = ParserWriteText(&pStr, pti, structptr, 0, iOptionFlagsToMatch, iOptionFlagsToExclude);


	fileLocatePhysical(filename, absFile);
	backSlashes(absFile);
	mkdirtree_const(absFile);

	pOutFile = fopen(absFile, "wbz");

	if (!pOutFile)
	{
		iRetVal = 0;
	}
	else
	{
		fwrite(pStr, estrLength(&pStr) + 1, 1, pOutFile);
		fclose(pOutFile);
	}
	
	estrDestroy(&pStr);

	return iRetVal;
}

int ParserReadZippedTextFile(const char* filename, ParseTable *pti, void* structptr, int flags)
{
	int iInLength, iRetVal;
	char *pBufCompressed = fileAlloc(filename, &iInLength);
	char *pBufUncompressed;
	char strTempFile[MAX_PATH], strTempFileAbsolute[MAX_PATH];
	FILE *fOut;

	if (!pBufCompressed)
	{
		return 0;
	}

	// Hack to allow us to read .gz files from hog files
	sprintf(strTempFile, "%s/gztemp-%d", fileTempDir(), GetAppGlobalID());
	fileLocateWrite(strTempFile, strTempFileAbsolute);
	makeDirectoriesForFile(strTempFileAbsolute);
	fOut = fileOpen(strTempFileAbsolute, "wb");
	if (!fOut)
	{
		SAFE_FREE(pBufCompressed);
		return 0;
	}
	fwrite(pBufCompressed, iInLength, 1, fOut);
	fclose(fOut);

	SAFE_FREE(pBufCompressed);

	pBufUncompressed = fileAllocWBZ(strTempFile, NULL);
	fileForceRemove(strTempFileAbsolute);

	if (!pBufUncompressed)
	{
		return 0;
	}

	iRetVal = ParserReadTextForFile(pBufUncompressed, filename, pti, structptr, flags);

	free(pBufUncompressed);

	return iRetVal;
}





int ParserReadTextFile(const char* filename, ParseTable *pti, void* structptr, int iFlags)
{
	fileLoadGameDataDirAndPiggs();
	return ParserLoadFilesInternal(NULL, filename, NULL, NULL, PARSER_INTERNALLOAD | iFlags, pti, structptr, NULL, false, NULL);
}

int ParserReadTextFile_CaptureErrors(const char* filename, ParseTable *pti, void* structptr, int flags, char **ppErrors)
{
	int iRetVal;
	ErrorfPushCallback(EstringErrorCallback, (void*)(ppErrors));
	iRetVal = ParserReadTextFile(filename, pti, structptr, flags);
	ErrorfPopCallback();
	return iRetVal;
}



//////////////////////////////////////////////////////////////////////////////////// Serialized Parsed Files
static void UpdateCrcFromStaticDefines(StaticDefine defines[])
{
	int curtype = DM_NOTYPE;
	StaticDefine* cur = defines;

	while (1)
	{
		// look for key markers first
		if (cur->key == U32_TO_PTR(DM_END))
			return;
		else if (cur->key == U32_TO_PTR(DM_INT))
			curtype = DM_INT;
		else if (cur->key == U32_TO_PTR(DM_STRING))
			curtype = DM_STRING;
		else if (cur->key == U32_TO_PTR(DM_DYNLIST))
		{
			if (*(DefineContext**)cur->value)
				UpdateCrcFromDefineList(*(DefineContext**)cur->value);
		}
		else if (cur->key == U32_TO_PTR(DM_TAILLIST))
		{
			UpdateCrcFromStaticDefines((StaticDefine *)cur->value);
			return;
		}
		else
		{
			// crc the key and value
			cryptAdler32Update_IgnoreCase(cur->key, (int)strlen(cur->key));
			if (curtype == DM_INT)
			{
				int val = PTR_TO_S32(cur->value);
				xbEndianSwapU32(val);
				cryptAdler32Update((U8*)&val, sizeof(int));
			}
			else if (curtype == DM_STRING)
				cryptAdler32Update_IgnoreCase(cur->value, (int)strlen(cur->value));
		}
		// keep looking for keys
		cur++;
	}
}

static void UpdateCRCFromParseInfo(ParseTable pti[], U32 iFlags);

static bool sbPrintfCRCUpdates = false;



void UpdateCRCFromParseInfoColumn(ParseTable pti[], int iColumn, U32 iFlags)
{
	int usage;
	StructTypeField typeAllBits;
	StructTokenType type = TOK_GET_TYPE(pti[iColumn].type);

	bool bRecurseOneLevel = iFlags & TPICRCFLAG_ALWAYS_DESCEND_ONE_LEVEL;

	iFlags &= ~TPICRCFLAG_ALWAYS_DESCEND_ONE_LEVEL;

	if (pti[iColumn].type & TOK_REDUNDANTNAME) return; // ignore redundant fields
	if (pti[iColumn].type & TOK_UNOWNED) return; // ignore unowned fields
	if (pti[iColumn].type & TOK_STRUCT_NORECURSE) return;

	if (sbPrintfCRCUpdates)
	{
		printf("Going to update CRC for %s[%d]... starting at %u\n", ParserGetTableName(pti), iColumn, cryptAdlerGetCurValue());
	}

	// name field
	if (pti[iColumn].name && !(iFlags & TPICRCFLAG_IGNORE_NAME) )
		cryptAdler32Update_IgnoreCase(pti[iColumn].name, (int)strlen(pti[iColumn].name));

	if (sbPrintfCRCUpdates)
	{
		printf("Name: %u", cryptAdlerGetCurValue());
	}


	typeAllBits = pti[iColumn].type;

	assert(sizeof(typeAllBits) == 8);
	xbEndianSwapU64(typeAllBits);

	cryptAdler32Update((void*)&typeAllBits, sizeof(StructTypeField));

	if (sbPrintfCRCUpdates)
	{
		printf("TypeAllBits: %u", cryptAdlerGetCurValue());
	}

	// Can NOT CRC the offset to member, because it's different on Xbox and x64

	// default value field 
	usage = TYPE_INFO(type).interpretfield(pti, iColumn, ParamField);
	switch (usage)
	{
	case OffsetOfSizeField: break; // don't care about offset reposition
	case SizeOfSubstruct: // JE: Do care about size of struct changing for SharedMemory
		// JE: But we don't care about it for loading from disk, and it breaks loading on different platforms
		//  so only check it for those who ask (just shared memory and some quickly deprecated load-old-bins code).
		if (!(iFlags & TPICRCFLAG_CRC_SUBSTRUCT_SIZE))
			break;
		// else fall through
	case NumberOfElements: // fall
	case DefaultValue: // fall
	case EmbeddedStringLength: // fall
	case BitOffset: // fall
	case SizeOfRawField: 
		{
			int param = (int)pti[iColumn].param; // make ourselves insensitive to 64-bit while crc'ing
			if (usage == BitOffset)
			{
				// Use the bit count for CRCing for bit fields
				param = TextParserBitField_GetBitCount(pti, iColumn);
			}
			xbEndianSwapU32(param);

			cryptAdler32Update((U8*)&param, sizeof(int));
		}
		break;
	case PointerToDefaultString:
	case PointerToCommandString:
		{
			char* str = (char*)pti[iColumn].param;
			if (str)
				cryptAdler32Update(str, (int)strlen(str));
		}
		break;
	}

	if (sbPrintfCRCUpdates)
	{
		printf("Param: %u", cryptAdlerGetCurValue());
	}


	// subtable
	if (TOK_SHOULD_RECURSE_PARSEINFO(pti,iColumn) || TOK_HAS_SUBTABLE(pti[iColumn].type) && bRecurseOneLevel) 
	{
		UpdateCRCFromParseInfo(pti[iColumn].subtable, iFlags);
		if (sbPrintfCRCUpdates)
		{
			printf("Subtable (recurse): %u", cryptAdlerGetCurValue());
		}
	}



	// static defines
	if (pti[iColumn].subtable && !(iFlags & TPICRCFLAG_IGNORE_SUBTABLE))
	{
		usage = TYPE_INFO(type).interpretfield(pti, iColumn, SubtableField);
		if (usage == StaticDefineList)
		{
			UpdateCrcFromStaticDefines(pti[iColumn].subtable);
			if (sbPrintfCRCUpdates)
			{
				printf("Subtable (static defines): %u", cryptAdlerGetCurValue());
			}
		}
	}

	// Format string stores the default for the bit fields
	if (type == TOK_BIT && FormatStringIsSet(&pti[iColumn]))
	{
		const char *pFormatString = GetRawFormatString(&pti[iColumn]);
		cryptAdler32Update(pFormatString, (int)strlen(pFormatString));
		if (sbPrintfCRCUpdates)
		{
			printf("format string %u", cryptAdlerGetCurValue());
		}
	}

	if (sbPrintfCRCUpdates)
	{
		printf("\n");
	}	
}


void (*pUpdateCRCFromParseInfoCB)(ParseTable pti[]);
static void UpdateCRCFromParseInfo(ParseTable pti[], U32 iFlags)	// get crc to use as build number based on parse tree
{
	int i;
#if _DEBUG
        if(pUpdateCRCFromParseInfoCB)
            pUpdateCRCFromParseInfoCB(NULL);
#endif
    FORALL_PARSETABLE(pti, i)
	{
		UpdateCRCFromParseInfoColumn(pti, i, iFlags & (TPICRCFLAG_CRC_SUBSTRUCT_SIZE));
#if _DEBUG
        if(pUpdateCRCFromParseInfoCB)
            pUpdateCRCFromParseInfoCB(&pti[i]);
#endif
	}
}
static int siCRCInvalidationCookie = 1;

void Parser_InvalidateParseTableCRCs(void)
{
	InterlockedIncrement(&siCRCInvalidationCookie);
}

int ParseTableCRCInternal(ParseTable pti[], DefineContext* defines, U32 iFlags)
{
	int crc;

	cryptAdler32Init();
	UpdateCRCFromParseInfo(pti, iFlags);
	if (defines)
		UpdateCrcFromDefineList(defines);
	crc = (int)cryptAdler32Final();

	return crc;
}

static char *spDbgParseTableName = NULL;

static bool sbDisableNewCRCCode = false;
AUTO_CMD_INT(sbDisableNewCRCCode, DisableNewCRCCode);

int ParseTableCRC(ParseTable pti[], DefineContext* defines, U32 iFlags)
{
	int crc;
	ParseTableInfo *pInfo;
	bool bPrintOutCRCs = false;
	int iCachedCrc;

	//never bother caching things with defines
	if (defines || sbDisableNewCRCCode)
	{
//		printf("CRCCache - have defines... not caching %s\n", ParserGetTableName(pti));
		return ParseTableCRCInternal(pti, defines, iFlags);
	}

	if (spDbgParseTableName && stricmp_safe(ParserGetTableName(pti), spDbgParseTableName) == 0)
	{
		bPrintOutCRCs = true;
	}

	pInfo = ParserGetTableInfo(pti);
	if (!pInfo || !pInfo->bAllowCRCCaching)
	{
		crc = ParseTableCRCInternal(pti, defines, iFlags);

//		printf("CRCCache - caching not allowed, calculated CRC %d for %s\n", crc, ParserGetTableName(pti));
		return crc;
	}

	EnterCriticalSection(&gTextParserCRCLock);

	if (pInfo->iCRCValidationCookie != siCRCInvalidationCookie)
	{
		if (pInfo->sCachedCRCsByFlags)
		{
			stashTableClear(pInfo->sCachedCRCsByFlags);
		}
	}
	
	if (!pInfo->sCachedCRCsByFlags)
	{
		pInfo->sCachedCRCsByFlags = stashTableCreateInt(4);
	}
	
	if (stashIntFindInt(pInfo->sCachedCRCsByFlags, iFlags + 1, &iCachedCrc))
	{
//		printf("CRCCache - %s was found\n", ParserGetTableName(pti));
	
/* this is the code that verifies that CRCs aren't changing behind our backs,
	it ran for several months and found some issues which are now all fixed, so hopefully we're good to go now

		crc = ParseTableCRCInternal(pti, defines, iFlags);
		if (iCachedCrc != crc)
		{
			if (bPrintOutCRCs)
			{
				sbPrintfCRCUpdates = true;
				printf("---------Calculating CRC for %s\n", ParserGetTableName(pti));
				ParseTableCRCInternal(pti, defines, iFlags);
				printf("---------Done\n");
				sbPrintfCRCUpdates = false;
			}

			assertmsgf(0, "Got two different CRCs for parse table %s, it needs AST_RUNTIME_MODIFIED",
				ParserGetTableName(pti));
		}*/

		LeaveCriticalSection(&gTextParserCRCLock);
		return iCachedCrc;
	}
	else
	{
//		printf("CRCCache - %s was NOT found\n", ParserGetTableName(pti));
		if (bPrintOutCRCs)
		{
			sbPrintfCRCUpdates = true;
			printf("---------Calculating CRC for %s\n", ParserGetTableName(pti));
		}
		crc = ParseTableCRCInternal(pti, defines, iFlags);
		stashIntAddInt(pInfo->sCachedCRCsByFlags, iFlags + 1, crc, true);
		pInfo->iCRCValidationCookie = siCRCInvalidationCookie;

		if (bPrintOutCRCs)
		{
			printf("---------Done\n");
			sbPrintfCRCUpdates = false;
		}
	}
	LeaveCriticalSection(&gTextParserCRCLock);

	return crc;
}

U32 GetCRCFromParseInfoColumn(ParseTable tpi[], int iColumn, U32 iFlags)
{
	cryptAdler32Init();
	UpdateCRCFromParseInfoColumn(tpi, iColumn, iFlags);
	return cryptAdler32Final();
}



// return success
int WriteBinaryFunctionCalls(SimpleBufHandle file, StructFunctionCall** structarray, int* sum)
{
	int succeeded = 1;
	int i, wr, number = eaSize(&structarray);

	if (!SimpleBufWriteU32(number, file)) succeeded = 0;
	*sum += sizeof(int);
	for (i = 0; i < number; i++)
	{
		if (!(wr = WritePascalString(file, structarray[i]->function))) succeeded = 0;
		*sum += wr;
		if (!WriteBinaryFunctionCalls(file, structarray[i]->params, sum)) succeeded = 0;
	}
	return succeeded;
}

void SendDiffFunctionCalls(Packet* pak, StructFunctionCall** structarray)
{
	int i, n = eaSize(&structarray);
	pktSendBits(pak, 32, n);
	for (i = 0; i < n; i++)
	{
		pktSendString(pak, structarray[i]->function);
		SendDiffFunctionCalls(pak, structarray[i]->params);
	}
}

bool ParserWriteBinaryTable(SimpleBufHandle file, FILE *pLayoutFile, FileList *pFileList, ParseTable pti[], void* structptr, int* sum, StructTypeField iOptionFlagsToMatch, StructTypeField iOptionFlagsToExclude) // returns success
{
	TextParserResult eResult = PARSERESULT_SUCCESS;
	long loc, dataloc;
	int i, datasum;
	TextParserAutoFixupCB *pFixupCB;
	int iCurOffset;
	int iStartingLayoutFileOffset = 0;

	if (!structptr) return false; // fail out
	*sum = 0;

	//this must always be set, as it's assumed in ParserReadBinaryTable
	iOptionFlagsToExclude |= TOK_NO_WRITE;

	pFixupCB = ParserGetTableFixupFunc(pti);

	TagLayoutFileBegin(pLayoutFile, file, "Beginning of struct %s (file %s) of type %s",
		ParserGetStructName(pti, structptr), ParserGetFilename(pti, structptr), ParserGetTableName(pti));

	if (pFixupCB)
	{
		SET_MIN_RESULT(eResult, pFixupCB(structptr, FIXUPTYPE_PRE_BIN_WRITE, NULL));
	}


	// skip data segment length
	datasum = 0;
	dataloc = SimpleBufTell(file);
	SimpleBufSeek(file, sizeof(int), SEEK_CUR);
	*sum += sizeof(int);

	// data segment
	FORALL_PARSETABLE(pti, i)
	{
		if (pti[i].type & TOK_REDUNDANTNAME) 
		{
			continue; // don't allow TOK_REDUNDANTNAME
		}
		if (pti[i].type & TOK_UNOWNED) 
		{
			continue; // don't allow TOK_UNOWNED
		}
		if (!FlagsMatchAll(pti[i].type,iOptionFlagsToMatch))
		{
			continue;
		}
		if (!FlagsMatchNone(pti[i].type,iOptionFlagsToExclude))
		{
			continue;
		}

		iCurOffset = SimpleBufTell(file);
		if (pLayoutFile)
		{
			iStartingLayoutFileOffset = ftell(pLayoutFile);
			TagLayoutFileOffset(pLayoutFile, file, iCurOffset, "Field %s", pti[i].name);
		}



		SET_MIN_RESULT(eResult, writebin_autogen(file, pLayoutFile, pFileList, pti, i, structptr, 0, &datasum, iOptionFlagsToMatch, iOptionFlagsToExclude));

		//if nothing was actually written into the data file, unroll the tag we put into the layout file
		if (pLayoutFile && iCurOffset == SimpleBufTell(file))
		{
			fseek(pLayoutFile, iStartingLayoutFileOffset, SEEK_SET);
		}



		if (eResult == PARSERESULT_INVALID)
			break;
	} // each data token

	// fixup length of data segment
	loc = SimpleBufTell(file);
	SimpleBufSeek(file, dataloc, SEEK_SET);
	if (!SimpleBufWriteU32(datasum, file))
	{
		eResult = PARSERESULT_ERROR;
	}
	*sum += datasum;
	SimpleBufSeek(file, loc, SEEK_SET);

	TagLayoutFileEnd(pLayoutFile, file, "End of struct %s (file %s) of type %s",
		ParserGetStructName(pti, structptr), ParserGetFilename(pti, structptr), ParserGetTableName(pti));


	return (eResult == PARSERESULT_SUCCESS);
}

int ParserWriteTextFileToHogg(
	const char *filename,
	ParseTable pti[], void* structptr,
	HogFile *pHogFile) {

	char *estrData = NULL;
	int ret = ParserWriteText(&estrData, pti, structptr, 0, 0, 0);

	if(ret == PARSERESULT_SUCCESS) {

		SimpleBufHandle file = SimpleBufOpenWrite(
			filename, 1,
			pHogFile, false,
			false);

		if(file) {
			SimpleBufWriteString(estrData, file);
			SerializeClose(file);
		} else {
			ret = PARSERESULT_ERROR;
		}

		estrDestroy(&estrData);
	}

	return ret;
}

int ParserReadTextFileFromHogg(
	const char *filename,
	ParseTable pti[], void* structptr,
	HogFile *pHogFile) {

	SimpleBufHandle file = SimpleBufOpenRead(
		filename, pHogFile);
	char *data = NULL;
	int ret;

	if(file) {
		SimpleBufReadString(&data, file);
		ret = ParserReadTextForFile(data, filename, pti, structptr, 0);
		SerializeClose(file);
	} else {
		ret = PARSERESULT_ERROR;
	}

	return ret;
}

int ParserWriteBinaryFile(const char* filename, 
	const char *layoutFilename, 
	ParseTable pti[], void* structptr, FileList* filelist, FileList *filesWithErrorsList, DependencyList *deplist, DefineContext* defines, StructTypeField iOptionFlagsToMatch, StructTypeField iOptionFlagsToExclude, 
		HogFile *pHogFile, enumParserWriteBinaryFlags eFlags, enumBinaryFileHeaderFlags eHeaderFlagsToWrite) // returns success
{
	int crc;
	SimpleBufHandle file;
	int wr = 0;
	int success;

//	U32 time;

	FILE *pLayoutFile = NULL; 
	
	PERFINFO_AUTO_START_FUNC();

	crc = (eFlags & PARSERWRITE_IGNORECRC) ? 0 : ParseTableCRC(pti, defines, 0);
	file = SerializeWriteOpen(filename, PARSE_SIG, crc, pHogFile,
		(eFlags & PARSERWRITE_IGNORECRC) || (eFlags & PARSERWRITE_ZEROTIMESTAMP), eFlags & PARSERWRITE_HUGEBUFFER,
		eHeaderFlagsToWrite);
	if (!file)
	{
		PERFINFO_AUTO_STOP();
		return 0; // can't create
	}

	if (layoutFilename && layoutFilename[0])
	{
		mkdirtree_const(layoutFilename);
		pLayoutFile = fopen(layoutFilename, "wt");
		if (!pLayoutFile)
		{
			Errorf("Couldn't open .bin layout file %s", layoutFilename);
		}

		TagLayoutFile(pLayoutFile, file, "End of Header and CRC information");
	}
	

	success = FileListWrite(filelist, file, pLayoutFile, "main");
	if (!success) Errorf("ParserWriteBinaryFile: could not write to %s\n", filename);
	success = FileListWrite(filesWithErrorsList, file, pLayoutFile, "filesWithErrors");
	if (!success) Errorf("ParserWriteBinaryFile: could not write to %s\n", filename);
	success = DependencyListWrite(deplist, file, pLayoutFile);
	if (!success) Errorf("ParserWriteBinaryFile: could not write to %s\n", filename);
	success = ParserWriteBinaryTable(file, pLayoutFile, filelist, pti, structptr, &wr, iOptionFlagsToMatch, iOptionFlagsToExclude);
	if (!success) Errorf("ParserWriteBinaryFile: could not write to %s\n", filename);
	SerializeClose(file);

	/*if (filelist)
	{
		time = FileListGetMostRecentTimeSS2000(filelist);
		if (time)
		{
			//bin file will always have mod time 1 second later than latest text file. This is consistent so that
			//bins built on multiple machines will have the same timestamp, but it is +1 instead of equal to avoid
			//<= >= errors.
			//Creation and access time set to current time so it's easy to see if the bin file was touched (Jimb's request)
			fileSetModificationTimesSS2000(filename, timeSecondsSince2000(), time + 1, timeSecondsSince2000());
		}
	}*/
	

	if (pLayoutFile)
	{
		fclose(pLayoutFile);
	}

	PERFINFO_AUTO_STOP();
	
	return success;
}

TextParserResult ReadBinaryFunctionCalls(SimpleBufHandle file, StructFunctionCall*** structarray, int* sum, ParseTable tpi[])
{
	char *pTempEString = NULL;
	TextParserResult succeeded = PARSERESULT_SUCCESS;
	int number, re, i;
	StructFunctionCall* newstruct;

	if (!SimpleBufReadU32((U32*)&number, file)) 
		SET_INVALID_RESULT(succeeded);
	*sum += sizeof(int);
	if (number)
	{
		eaDestroy(structarray);
		for (i = 0; i < number; i++)
		{
			if (!(re = ReadPascalStringIntoEString(file, &pTempEString))) 
				SET_INVALID_RESULT(succeeded);
			*sum += re;
			if (pTempEString && pTempEString[0])
			{
				newstruct = StructAllocRawCharged(sizeof(*newstruct), tpi);
				newstruct->function = StructAllocString(pTempEString);
				SET_MIN_RESULT(succeeded,ReadBinaryFunctionCalls(file, &newstruct->params, sum, tpi));
				eaPush(structarray, newstruct);
			}
		}
	}

	estrDestroy(&pTempEString);
	return succeeded;
}

bool RecvDiffFunctionCalls(Packet* pak, StructFunctionCall*** structarray, ParseTable tpi[], enumRecvDiffFlags eFlags)
{
	int i;
	U32 iSize;

	// destroy any existing data
	for (i = 0; i < eaSize(structarray); i++)
		StructFreeFunctionCall((*structarray)[i]);

	iSize = pktGetBits(pak,32);
	if (iSize == 0)
	{
		eaDestroy(structarray);
		return true;
	}

	if (eFlags & RECVDIFF_FLAG_UNTRUSTWORTHY_SOURCE && iSize > MAX_UNTRUSTWORTHY_ARRAY_SIZE)
	{
		RECV_FAIL("Received array size %u from an untrustworthy source... data corruption?", 
			iSize);
	}

	eaSetSize(structarray, iSize);
	for (i = 0; i < (int)iSize; i++)
	{
		char *pFunction = pktMallocString(pak);
		(*structarray)[i] = StructAllocRawCharged(sizeof(StructFunctionCall), tpi);
		if (pFunction && pFunction[0])
		{
			(*structarray)[i]->function = pFunction;
		}
		else
		{
			if (pFunction)
			{
				free(pFunction);
			}
		}

		if (!RecvDiffFunctionCalls(pak, &(*structarray)[i]->params, tpi, eFlags))
		{
			return 0;
		}

		RECV_CHECK_PAK(pak);
	}

	return 1;
}
#ifdef _XBOX
static __forceinline TextParserResult nonarray_readbin_inline(SimpleBufHandle file, ParseTable tpi[], int column, void* structptr, int index, int* datasum, StructTypeField iOptionFlagsToMatch, StructTypeField iOptionFlagsToExclude)
{
	readbin_f readbinFunc;
	if (TOK_GET_TYPE(tpi[column].type) == TOK_F32_X)
		return float_readbin(file, tpi, column, structptr, index, datasum, iOptionFlagsToMatch, iOptionFlagsToExclude);

	readbinFunc = TYPE_INFO(tpi[column].type).readbin;
	if (readbinFunc)
		return readbinFunc(file, tpi, column, structptr, index, datasum, iOptionFlagsToMatch, iOptionFlagsToExclude);
	return PARSERESULT_SUCCESS;
}
#endif

U32 prbtUsedCount;
U32 prbtSkippedCount;

AUTO_COMMAND;
void printPRBTCounts(void){
	printf(	"ParserReadBinaryTable: %d used, %d skipped.\n",
			prbtUsedCount,
			prbtSkippedCount);
}

TextParserResult ParserReadBinaryTableEx(SimpleBufHandle file, FileList *pFileList, ParseTable pti[], void* structptr, int* sum, StructTypeField iOptionFlagsToMatch, StructTypeField iOptionFlagsToExclude) // returns success
{
	TextParserResult succeeded = PARSERESULT_SUCCESS;
	long dataloc;
	int i, datasum;
	int readdatasum;
	TextParserAutoFixupCB *pFixupCB;

	if (!structptr) return PARSERESULT_INVALID; // fail out
	
	pFixupCB = ParserGetTableFixupFunc(pti);

	// read data segment length
	if (!SimpleBufReadU32((U32*)&readdatasum, file)) 
	{
		return PARSERESULT_INVALID;
	}

	*sum += sizeof(int);
	dataloc = SimpleBufTell(file);
	datasum = 0;

	// read data segment
	FORALL_PARSETABLE(pti, i)
	{
		if(	pti[i].type & TOK_REDUNDANTNAME || // don't allow TOK_REDUNDANTNAME
			pti[i].type & TOK_UNOWNED || // don't allow TOK_UNOWNED
			pti[i].type & TOK_NO_WRITE || //if it has NOWRITE, it will never be in a .bin file so we skip it
			!FlagsMatchAll(pti[i].type,iOptionFlagsToMatch) ||
			!FlagsMatchNone(pti[i].type,iOptionFlagsToExclude))
		{
			prbtSkippedCount++;
			continue;
		}
		
		prbtUsedCount++;

#ifdef _XBOX
		//LDM: using the function pointer table is very slow on xbox
		if (pti[i].type & TOK_EARRAY)
			SET_MIN_RESULT(succeeded, earray_readbin(file, pti, i, structptr, 0, &datasum, iOptionFlagsToMatch, iOptionFlagsToExclude));		
		else if (pti[i].type & TOK_FIXED_ARRAY)
			SET_MIN_RESULT(succeeded, fixedarray_readbin(file, pti, i, structptr, 0, &datasum, iOptionFlagsToMatch, iOptionFlagsToExclude));		
		else
			SET_MIN_RESULT(succeeded, nonarray_readbin_inline(file, pti, i, structptr, 0, &datasum, iOptionFlagsToMatch, iOptionFlagsToExclude));		
#else
		//PERFINFO_AUTO_START_STATIC(pti[i].name, (PERFINFO_TYPE**)(&pti[i].param), 1);
		SET_MIN_RESULT(succeeded,readbin_autogen(file, pFileList, pti, i, structptr, 0, &datasum, iOptionFlagsToMatch, iOptionFlagsToExclude));
		//PERFINFO_AUTO_STOP();
#endif
		if (succeeded != PARSERESULT_SUCCESS)
		{
			printf("%s: unexpected end of file\n", __FUNCTION__);
			return succeeded;
		}
	} // each data token

	// check data segment length
	if (datasum != readdatasum)
	{
		printf("%s: datasum length not correct\n", __FUNCTION__);
		return PARSERESULT_INVALID;
	}
	*sum += readdatasum;

	if (pFixupCB)
	{
		PERFINFO_AUTO_START("fixup", 1);
		SET_MIN_RESULT(succeeded, pFixupCB(structptr, FIXUPTYPE_POST_BIN_READ, NULL));
		PERFINFO_AUTO_STOP();
	}

	return succeeded;
}

TextParserResult ParserReadBinaryTable(SimpleBufHandle file, FileList *pFileList, ParseTable pti[], void* structptr, int* sum, StructTypeField iOptionFlagsToMatch, StructTypeField iOptionFlagsToExclude)
{
	TextParserResult ret;
	PERFINFO_AUTO_START_FUNC();
	ret = ParserReadBinaryTableEx(file, pFileList, pti, structptr, sum, iOptionFlagsToMatch, iOptionFlagsToExclude);
	PERFINFO_AUTO_STOP();
	return ret;
}

// returns success
int ParserReadBinaryFile(SimpleBufHandle file, ParseTable pti[], void* structptr, FileList* filelist, FileList *filesWithErrorsList, DependencyList *deplist, DefineContext* defines, StructTypeField iOptionFlagsToMatch, StructTypeField iOptionFlagsToExclude, bool bCloseFile)
{
	int size = 0;
	int success;
	long loc, endloc;
	FileList localFileList = NULL;

	if (!file)
		return 0;  // no file, or failed crc number

	PERFINFO_AUTO_START_FUNC();

	if (!filelist)
	{
		FileListCreate(&localFileList);
		filelist = &localFileList;
	}

	errorIsDuringDataLoadingInc(NULL);
	if (!FileListRead(filelist, file)) {
		if (bCloseFile)
			SerializeClose(file);
		errorIsDuringDataLoadingDec();
		if (localFileList)
		{
			FileListDestroy(&localFileList);
		}
		PERFINFO_AUTO_STOP();
		return 0; // couldn't read file table
	}

	if (!FileListRead(filesWithErrorsList, file)) {
		if (bCloseFile)
			SerializeClose(file);
		errorIsDuringDataLoadingDec();
		if (localFileList)
		{
			FileListDestroy(&localFileList);
		}
		PERFINFO_AUTO_STOP();
		return 0; // couldn't read file table
	}

	if (!DependencyListRead(deplist, file)) {
		if (bCloseFile)
			SerializeClose(file);
		errorIsDuringDataLoadingDec();
		if (localFileList)
		{
			FileListDestroy(&localFileList);
		}
		PERFINFO_AUTO_STOP();
		return 0; // couldn't read file table
	}
	loc = SimpleBufTell(file);

	success = (PARSERESULT_SUCCESS == ParserReadBinaryTable(file, filelist, pti, structptr, &size, iOptionFlagsToMatch, iOptionFlagsToExclude));

	SimpleBufSeek(file, 0, SEEK_END);
	endloc = SimpleBufTell(file);
	if (endloc - loc != size)
	{
		printf("%s: unexpected end of file\n", __FUNCTION__);
		success = 0;
	}
	if (bCloseFile)
		SerializeClose(file);
	errorIsDuringDataLoadingDec();
	if (localFileList)
	{
		FileListDestroy(&localFileList);
	}
	PERFINFO_AUTO_STOP();
	return success;
}

SimpleBufHandle ParserOpenBinaryFile(HogFile *hog_file, const char *filename, ParseTable pti[], enumBinaryReadFlags eFlags, DefineContext* defines)
{
	int crc1 = (eFlags & BINARYREADFLAG_IGNORE_CRC) ? 0 : ParseTableCRC(pti, defines, 0);
	int crc2 = (eFlags & BINARYREADFLAG_IGNORE_CRC) ? 0 : ParseTableCRC(pti, defines, TPICRCFLAG_CRC_SUBSTRUCT_SIZE);

	if ( !fileIsUsingDevData() || gbProductionModeBins)
	{
		eFlags |= BINARYREADFLAG_IGNORE_CRC;
	}

	return SerializeReadOpen(filename, PARSE_SIG, crc1, crc2, eFlags, hog_file);
}

int ParserOpenReadBinaryFile(HogFile *hog_file, const char *filename, ParseTable pti[], void* structptr, FileList* filelist, FileList *filesWithErrorsList, DependencyList *deplist, DefineContext* defines, StructTypeField iOptionFlagsToMatch, StructTypeField iOptionFlagsToExclude, enumBinaryReadFlags eFlags)
{
	SimpleBufHandle file;
	int retval;
	PERFINFO_AUTO_START_FUNC();
	file = ParserOpenBinaryFile(hog_file, filename, pti, eFlags, defines);
	retval = ParserReadBinaryFile(file, pti, structptr, filelist, filesWithErrorsList, deplist, defines, iOptionFlagsToMatch, iOptionFlagsToExclude, true);
	PERFINFO_AUTO_STOP();
	return retval;
}


////////////////////////////////////////Stuff relating to formatString

///this struct defines all the values in a formatString, starts with a magic string beginning with an ! so that
//a pointer to this can be differentiated from a pointer to an actual string
typedef struct
{
	char magicString[4];
	char *pRawString;
	StashTable ints;
	StashTable strings;
} FormatStringValues;

//a single global stashtable of FormatStringValues structs, keyed by the original string
StashTable FormatStringTable = 0;

AUTO_RUN_ANON(memBudgetAddMapping("textparser_c_FormatStrings", BUDGET_EngineMisc););

__forceinline bool FormatStringIsSet(ParseTable *pTable)
{
	return pTable->formatString_UseAccessor && pTable->formatString_UseAccessor[0];
}




FormatStringValues *GetFormatStringValues(ParseTable *pTPI)
{
	if (!pTPI->formatString_UseAccessor || !pTPI->formatString_UseAccessor[0])
	{
		return NULL;
	}

	if (pTPI->formatString_UseAccessor[0] == '!')
	{
		return (FormatStringValues*)(pTPI->formatString_UseAccessor);
	}
	else
	{
		FormatStringValues *pRetVal;
		char *pDupString;
		char *pTokens[256] = {0};
		int iCount;
		int i;

		EnterCriticalSection(&gTextParserFormatStringCritSec);
		if (pTPI->formatString_UseAccessor[0] == '!')
		{
			LeaveCriticalSection(&gTextParserFormatStringCritSec);
			return (FormatStringValues*)(pTPI->formatString_UseAccessor);
		}

		ONCE(FormatStringTable = stashTableCreateWithStringKeysEx(16, StashDefault, "textparser_c_FormatStrings", __LINE__));

		if (stashFindPointer(FormatStringTable, pTPI->formatString_UseAccessor, &pRetVal))
		{
			pTPI->formatString_UseAccessor = (char*)pRetVal;
			LeaveCriticalSection(&gTextParserFormatStringCritSec);
			return pRetVal;
		}

		pDupString = strdup(pTPI->formatString_UseAccessor);
		pRetVal = calloc_timed(sizeof(FormatStringValues), 1, _NORMAL_BLOCK, "textparser_c_FormatStrings", __LINE__);
		pRetVal->magicString[0] = '!';
		pRetVal->pRawString = strdup(pTPI->formatString_UseAccessor);

		stashAddPointer(FormatStringTable, pRetVal->pRawString, pRetVal, true);

		iCount = TokenizeLineRespectingStrings(pTokens, pDupString);
		assertmsg(iCount < ARRAY_SIZE(pTokens), "Possible format string token overflow");

		i = 0;

		while (i < iCount)
		{
			char *pName = pTokens[i];

			assert(strcmp(pTokens[i+1], "=") == 0);

			if (pTokens[i+2][0] == '"')
			{
				//make a dup copy to save
				int iLen = (int)strlen(pTokens[i+2]);
				char *pValue = calloc_timed(iLen - 1, 1, _NORMAL_BLOCK, "textparser_c_FormatStrings", __LINE__);
				memcpy(pValue, pTokens[i+2]+1, iLen - 2);

				//make sure the name is not duplicated
				if (pRetVal->ints)
				{
					assert(!stashFindInt(pRetVal->ints, pName, NULL));
				}

				if (pRetVal->strings)
				{
					assert(!stashFindPointer(pRetVal->strings, pName, NULL));
				}
				else
				{
					pRetVal->strings = stashTableCreateWithStringKeysEx(2, StashDeepCopyKeys_NeverRelease, "textparser_c_FormatStrings", __LINE__);
				}

				stashAddPointer(pRetVal->strings, pName, pValue, false);
			}
			else
			{
				int iVal;
				int iSuccess;
			
				if (pRetVal->strings)
				{
					assert(!stashFindPointer(pRetVal->strings, pName, NULL));
				}

				if (pRetVal->ints)
				{
					assert(!stashFindInt(pRetVal->ints, pName, NULL));
				}
				else
				{
					pRetVal->ints = stashTableCreateWithStringKeysEx(2, StashDeepCopyKeys_NeverRelease, "textparser_c_FormatStrings", __LINE__);
				}
			
			
				iSuccess = sscanf(pTokens[i+2], "%d", &iVal);

				assertmsgf(iSuccess, "Expected integer, got %s", pTokens[i+2]);

				stashAddInt(pRetVal->ints, pName, iVal, true);
			}

			if (i+3 < iCount)
			{
				assert(strcmp(pTokens[i+3], ",") == 0);
			}
			i+=4;
		}

		free(pDupString);


		pTPI->formatString_UseAccessor = (char*)pRetVal;
		LeaveCriticalSection(&gTextParserFormatStringCritSec);
		return pRetVal;
	}
}

const char *GetRawFormatString(ParseTable *pTable)
{
	char *pStr;
	FormatStringValues *pValues;

	if (!FormatStringIsSet(pTable))
	{
		return NULL;
	}

	pStr = pTable->formatString_UseAccessor;
	if (pStr[0] != '!')
	{
		return pStr;
	}

	pValues = GetFormatStringValues(pTable);
	return pValues->pRawString;
}

//this seems too simple... but note that calling GetFormatStringValues will always set
//the magical values pointer while leaving whatever pointer was in there intact
void SetFormatString(ParseTable *pTable, const char *pInString)
{
	pTable->formatString_UseAccessor = (char*)pInString;
	GetFormatStringValues(pTable);
}

void FreeFormatString(ParseTable *pTable)
{
	pTable->formatString_UseAccessor = NULL;
}

bool GetIntFromTPIFormatString(ParseTable *pParseTable, char *pName, int *pOutVal)
{
	FormatStringValues *pValues = NULL;

	if (!FormatStringIsSet(pParseTable))
	{
		return false;
	}

	pValues = GetFormatStringValues(pParseTable);

	if (!pValues->ints)
	{
		return false;
	}

	return stashFindInt(pValues->ints, pName, pOutVal);
}

bool GetStringFromTPIFormatString(ParseTable *pParseTable, char *pName, const char **ppOutString)
{
	FormatStringValues *pValues = NULL;

	if (!FormatStringIsSet(pParseTable))
	{
		return false;
	}

	pValues = GetFormatStringValues(pParseTable);

	if (!pValues->strings)
	{
		return false;
	}

	return stashFindPointerConst(pValues->strings, pName, ppOutString);
}


	



///////////////////////////////////////////////////////////////////////////////// ParserLoadFiles

static int matchesFileMask(TextParserState *pTps, const char *str)
{
	int i;
	if (!pTps->lf_filemasks)
		return 1;

	for (i = 0; i<eaSize(&pTps->lf_filemasks); i++)
	{
		if (strEndsWith(str, pTps->lf_filemasks[i]))
			return 1;
	}

	return 0;
}
	
// turn on/off forced .bin creation by ParserLoadFiles
void ParserForceBinCreation(int set)
{
	gbForceBinCreate = set;
}

static int ParserLoadFile(TextParserState *pTPS, const char* relpath, int ignore_empty)
{
	int ret = 0;
	TokenizerHandle tok;
	TextParserResult parseResult = PARSERESULT_SUCCESS;
	TextParserReformattingCallback *pReformattingCB;
	
	PERFINFO_AUTO_START_FUNC();

	if (strstri(relpath, ".preproc."))
	{
		int iSize;
		char *pSourceFileBuf = fileAlloc(relpath, &iSize);

		if (!pSourceFileBuf || iSize == 0)
		{
			if (!ignore_empty)
				pTPS->lf_loadedok = 0;
			SAFE_FREE(pSourceFileBuf);
			PERFINFO_AUTO_STOP();
			return 0;
		}
		else
		{

			char *pStrTokContext = NULL;
			int include_guard = 0, macro_guard = 0;

			if (pTPS->flags & PARSER_USE_CRCS)
				FileListInsertChecksum(&pTPS->parselist, relpath, 0);
			else
				FileListInsert(&pTPS->parselist, relpath, 0);

			genericPreProcEnterCriticalSection();

			genericPreProcReset();
			while (genericPreProcIncludes(&pSourceFileBuf, relpath, NULL, &pTPS->parselist, PreProc_Default) && include_guard < MAX_INCLUDE_DEPTH)
				include_guard++; // loop until done including files
			while (genericPreProcMacros(&pSourceFileBuf) && macro_guard < MAX_INCLUDE_DEPTH)
				macro_guard++; // loop until done evaluating macros
			genericPreProcIfDefs(pSourceFileBuf, &pStrTokContext, 0, relpath );

			genericPreProcReset();
			genericPreProcLeaveCriticalSection();

			tok = TokenizerCreateLoadedFile(pSourceFileBuf, relpath, pTPS, ignore_empty);
			if (!tok)
			{
				if (!ignore_empty)
					pTPS->lf_loadedok = 0;
				SAFE_FREE(pSourceFileBuf);
				PERFINFO_AUTO_STOP();
				return 0;
			}
		}
	}
	else if ((pReformattingCB = ParserGetReformattingCallback(pTPS->lf_pti)))
	{
		int iSize;
		char *pSourceFileBuf = fileAlloc(relpath, &iSize);

		if (!pSourceFileBuf || iSize == 0)
		{
			if (!ignore_empty)
				pTPS->lf_loadedok = 0;
			SAFE_FREE(pSourceFileBuf);
			PERFINFO_AUTO_STOP();
			return 0;
		}
		else
		{

			char *pStrTokContext = NULL;
			char *pReformattedBuf = NULL;
			if (pTPS->flags & PARSER_USE_CRCS)
				FileListInsertChecksum(&pTPS->parselist, relpath, 0);
			else
				FileListInsert(&pTPS->parselist, relpath, 0);

			pReformattingCB(pSourceFileBuf, &pReformattedBuf, relpath);
			free(pSourceFileBuf);
			pSourceFileBuf = strDupFromEString(&pReformattedBuf);
			estrDestroy(&pReformattedBuf);

			tok = TokenizerCreateLoadedFile(pSourceFileBuf, relpath, pTPS, ignore_empty);
			if (!tok)
			{
				if (!ignore_empty)
					pTPS->lf_loadedok = 0;
				SAFE_FREE(pSourceFileBuf);
				PERFINFO_AUTO_STOP();
				return 0;
			}
		}

	}
	else
	{
		tok = TokenizerCreateEx(relpath, pTPS, ignore_empty);
		if (!tok)
		{
			if (!ignore_empty)
				pTPS->lf_loadedok = 0;
			PERFINFO_AUTO_STOP();
			return 0;
		}
	}

	TokenizerSetFlags(tok, pTPS->flags);
	pTPS->lf_loadedonefile = 1;
	ParserReadTokenizer(tok, pTPS->lf_pti, pTPS->lf_structptr, true, &parseResult);
	TokenizerDestroy(tok);

	if (parseResult != PARSERESULT_SUCCESS)
	{
		pTPS->lf_loadedok = 0;

		if (pTPS->FilesWithErrors)
		{
			printf("Had errors while loading %s... adding it to the filesWithErrors list\n", relpath);
			FileListInsert(&pTPS->FilesWithErrors, relpath, 0);
		}

		PERFINFO_AUTO_STOP();
		return 0;
	}
	PERFINFO_AUTO_STOP();
	return 1;
}

static FileScanAction ParserLoadCallback(const char* path, FolderNode *node, void *unused, void *pUserData)
{
	char buffer[512];

	TextParserState *pTps = pUserData;

	if (node->name[0] != '_' || pTps->lf_include_hidden) 
	{ 
		if (matchesFileMask(pTps, node->name)) 
		{
			fileRelativePath(path, buffer); 
			ParserLoadFile(pTps, buffer, pTps->lf_ignoreempty); 
		}
		if (stricmp(node->name, ".svn")==0)
			return FSA_NO_EXPLORE_DIRECTORY;
		else
			return FSA_EXPLORE_DIRECTORY; 
	} else { 
		return FSA_NO_EXPLORE_DIRECTORY; 
	}
}

static U32 s_NumFilesChecked = 0;
static U32 s_NumFilesMatched = 0;

static FileScanAction DateCheckCallback(const char* path, FolderNode *node, void *unused, void *pUserData)
{
	TextParserState *pTps = pUserData;

	++s_NumFilesChecked;

	if (node->is_dir) {
		// If it's a subdirectory, we don't care about checking times, etc
		if (node->name[0]=='_') {
			return FSA_NO_EXPLORE_DIRECTORY;
		} else {
			return FSA_EXPLORE_DIRECTORY;
		}
	}

	if(!(node->name[0] == '_') && matchesFileMask(pTps, node->name) )
	{
		// look for this file in the bin list
		FileEntry * pEntry;

		// I can call this here only because I know my slashes are all well-behaved
		// If the implementation of FileList were faster, this could be done much faster yet
		pEntry = FileListFindFast(&pTps->lf_binfilelist,path);

		if (pEntry)
		{
			// It's in there already, great
			pEntry->seen = 1;
			if (node->timestamp != pEntry->date)
			{
				verbose_printf("%s in bin file has different date than %s scanned for\n",
					pEntry->path, path);
				pTps->lf_filesmissingfrombin = 1;
				return FSA_STOP;
			}
		}
		else
		{
			// It's not in there.  We're done.
			verbose_printf("%s on disk isn't in bin file\n", path);
			pTps->lf_filesmissingfrombin = 1;
			return FSA_STOP;
		}
	}
	return FSA_EXPLORE_DIRECTORY;
}

static SimpleBufHandle ParserIsPersistNewer(TextParserState *pTps, const char* filemask, const char* persistfile, const char *relpersistfile,
											ParseTable pti[], DefineContext* defines, bool force_devmode)
{
	DependencyList bindeplist = NULL;
	SimpleBufHandle binfile;
	int crc1, crc2, i, n;
	int savedpos;
	enumBinaryReadFlags eBinaryReadFlags = 0;

	//decide if we want to load the bin file even if it has errors
	bool bIgnoreHasErrorsDepFlag = gbBinsMustExist || (pTps->flags &  PARSER_ALLOW_BINS_WITH_ERRORS_AND_RELOADING);

	PERFINFO_AUTO_START_FUNC();

	if (!fileIsUsingDevData() || gbProductionModeBins)
	{
		eBinaryReadFlags |= BINARYREADFLAG_IGNORE_CRC;
	}

	if (pTps->flags &  PARSER_ONLY_LOAD_BIN_FILE_IF_IT_HAD_NO_ERRORS )
	{
		eBinaryReadFlags |= BINARYREADFLAG_REQUIRE_NO_ERRORS_FLAG;
	}

	// open the bin file
	crc1 = ParseTableCRC(pti, defines, 0);
	crc2 = ParseTableCRC(pti, defines, TPICRCFLAG_CRC_SUBSTRUCT_SIZE);
	binfile = SerializeReadOpen(persistfile, PARSE_SIG, crc1, crc2, 
		eBinaryReadFlags, NULL);
	if (!binfile)
	{
		verbose_printf("bin file %s is incorrect version\n", persistfile);
		PERFINFO_AUTO_STOP();
		return 0; // don't have bin file or crc wrong
	}

	savedpos = SimpleBufTell(binfile);

	// always assume .bin file is ok if not in development mode
	if(!force_devmode && (!fileIsUsingDevData() || gbProductionModeBins) &&  !fileIsProductionEditAllowed(relpersistfile))
	{
		PERFINFO_AUTO_STOP();
		return binfile;
	}

	// read the file table from the bin file first
	if (!FileListRead(&pTps->lf_binfilelist, binfile))
		goto IsPersist_Fail;

	//read the list of files with errors, not used yet
	if (!FileListRead(NULL, binfile))
		goto IsPersist_Fail;


	if (!DependencyListRead(&bindeplist, binfile))
		goto IsPersist_Fail;

	//if we have a set of preexisting dependencies, then check that the bin file already contains all of them. If it doesn't,
	//then code presumably changed and the dependency was added, so we need to force a rebuild. Note that we don't need
	//to bother checking the other direction, because having an extra dependency just means that at some point a rebuild
	//will happen that was not strictly needed, and then the extra dependency will remove itself automatically
	if (pTps->preExistingDependencies)
	{
		if (!DependencyListIsSuperSetOfOtherList(&bindeplist, &pTps->preExistingDependencies))
		{
			verbose_printf("DependencyListIsSuperSetOfOtherList failed. This presumably means that a new preExisting dependency was added in code\n");
			goto IsPersist_Fail;
		}
	}

	// assemble file list from directories
	if (pTps->lf_dirs) // prefixes are handled just by mask matching
	{
		if(errorGetVerboseLevel())
			loadstart_printf("Checking file dates...");
		s_NumFilesChecked = 0;
		s_NumFilesMatched = 0;
		for(i=0; i<eaSize(&pTps->lf_dirs); i++)
		{
			fileScanAllDataDirs2(pTps->lf_dirs[i], DateCheckCallback, pTps);
			if (pTps->lf_filesmissingfrombin)
			{
				break;
			}
		}
		if(errorGetVerboseLevel())
			loadend_printf("done (%u matches of %u files)", s_NumFilesMatched, s_NumFilesChecked);

		if (pTps->lf_filesmissingfrombin)
		{
			goto IsPersist_Fail;
		}
	}
	else if (filemask)
	{
		FileEntry * pEntry = FileListFind(&pTps->lf_binfilelist,filemask);

		if (pEntry)
		{
			if (!fileExists(filemask))
			{
				verbose_printf("%s is in bin file, but should not be\n", filemask);
				goto IsPersist_Fail;
			}
			else
			{
				// check the date
				if (fileLastChanged(filemask) != pEntry->date)
				{
					verbose_printf("%s in bin file has different date than %s scanned for\n",
						pEntry->path, filemask);
					goto IsPersist_Fail;
				}
				pEntry->seen = true;
			}
		}
		else
		{

			//note that zero-byte files don't get written into the file lists at all, so without the filesize check here
			//we'll always end up getting false rejections whenever there is zero-byte text file
			if (fileExists(filemask) && fileSize(filemask) > 0)
			{
					verbose_printf("%s exists, but isn't in the bin file\n", filemask);
					goto IsPersist_Fail;
			}
		}

	}

	// OK if dir and filemask are both NULL, we just compare to files on disk then

	// compare lists -
	// iterate through files in .bin
	n = eaSize(&pTps->lf_binfilelist);
	for (i = 0; i < n; i++)
	{
		FileEntry* binfile_entry = pTps->lf_binfilelist[i];
		if (!binfile_entry->seen)
		{
			__time32_t diskdate;
			if (pTps->flags & PARSER_USE_CRCS)
				diskdate = fileCachedChecksum(binfile_entry->path) | FILELIST_CHECKSUM_BIT;
			else
				diskdate = fileLastChanged(binfile_entry->path);
			if (!diskdate && binfile_entry->date)
			{
				verbose_printf("%s in bin file no longer exists on disk\n", binfile_entry->path);
				goto IsPersist_Fail;
			}
			else if (diskdate != binfile_entry->date)
			{
				verbose_printf("%s in bin file has different date than one on disk\n",
					binfile_entry->path);
				goto IsPersist_Fail;
			}
		}
	}

	// so, every file in bin has same date as one on disk


	// Check other dependencies
	n = eaSize(&bindeplist); 
	for (i = 0; i < n; i++)
	{
		U32 val;
		DependencyEntry* dep = bindeplist[i];

		val = ParserBinGetValueForDep(dep->type, dep->name);
		if (dep->value != val)
		{
			// As a special case, ignore error flag during binsMustExist		
			if (!(bIgnoreHasErrorsDepFlag && stricmp(dep->name,HAS_ERRORS_DEP_VALUE) == 0))
			{

				verbose_printf("Dependency %s(%d) in bin (%08x) differs from current value (%08x)", dep->name, dep->type, dep->value, val);
				goto IsPersist_Fail;
			}
		}
	}

	// ok then..
	FileListDestroy(&pTps->lf_binfilelist);
	DependencyListDestroy(&bindeplist);
	SimpleBufSeek(binfile, savedpos, SEEK_SET);
	PERFINFO_AUTO_STOP();
	return binfile;
IsPersist_Fail:
	FileListDestroy(&pTps->lf_binfilelist);
	DependencyListDestroy(&bindeplist);
	SerializeClose(binfile);
	PERFINFO_AUTO_STOP();
	return 0;
}

// This gets modified at run time
ParseTable parse_ResourceLoaderStruct[] =
{
	{ NULL, TOK_INDIRECT | TOK_EARRAY | TOK_STRUCT_X, offsetof(ResourceLoaderStruct,earrayOfStructs), 0, NULL },
	{ "Version",TOK_IGNORE, 0 }, // Ignore "Version" (for Object Library compatability)
	{ "{",		TOK_IGNORE, 0 },
	{ "}",		TOK_IGNORE, 0 }, // Strip out erroneous top level braces
	{ "", TOK_REDUNDANTNAME | TOK_INDIRECT | TOK_EARRAY | TOK_STRUCT_X, offsetof(ResourceLoaderStruct,earrayOfStructs), 0, NULL },
	{ "", 0, 0 }
};

AUTO_RUN;
void registerReferenceLoaderStruct(void)
{
	ParserSetTableInfo(parse_ResourceLoaderStruct,sizeof(ResourceLoaderStruct),"ResourceLoaderStruct",NULL,__FILE__, 0);
}

void SetUpResourceLoaderParse(const char *pDictName, int childSize, void *childTable, const char *deprecatedName)
{
	assertmsg(!parse_ResourceLoaderStruct[0].name,"Dictionary load/write functions can't be called recursively or from multiple threads!");

	parse_ResourceLoaderStruct[0].name = allocAddString((void *)pDictName);
	parse_ResourceLoaderStruct[0].param = childSize;
	parse_ResourceLoaderStruct[0].subtable = childTable;

	if (deprecatedName && deprecatedName[0])
	{
		parse_ResourceLoaderStruct[4].name = allocAddString((void *)deprecatedName);
		parse_ResourceLoaderStruct[4].param = childSize;
		parse_ResourceLoaderStruct[4].subtable = childTable;
		parse_ResourceLoaderStruct[4].type = TOK_REDUNDANTNAME | TOK_INDIRECT | TOK_EARRAY | TOK_STRUCT_X;
	}
	else
	{
		parse_ResourceLoaderStruct[4].name = allocAddString("");
		parse_ResourceLoaderStruct[4].param = 0;
		parse_ResourceLoaderStruct[4].subtable = NULL;
		parse_ResourceLoaderStruct[4].type = 0;
	}
}

int ParserWriteTextFileFromDictionary(const char* filename, DictionaryHandleOrName dictHandle, StructTypeField iOptionFlagsToMatch, StructTypeField iOptionFlagsToExclude)
{
	const char *pDictName = resDictGetParseName(dictHandle);
	const char *refName;
	ParseTable *childTable = resDictGetParseTable(dictHandle);
	int childSize = ParserGetTableSize(childTable);
	ResourceLoaderStruct loadStruct = {0};
	int keyColumn;
	ResourceIterator iterator;
	void *childStruct;
	int result;

	assertmsg(pDictName && childTable && childSize,"Invalid dictionary passed to ParserWriteTextFileFromDictionary!");

	assertmsg((keyColumn = ParserGetTableKeyColumn(childTable)) >= 0,"ParseTable must have Key to be written from dictionary!");

	if (!filename)
	{
		return 0;
	}

	eaIndexedEnableVoid(&loadStruct.earrayOfStructs,childTable);

	SetUpResourceLoaderParse(pDictName,childSize,childTable,resDictGetDeprecatedName(dictHandle));

	resInitIterator(dictHandle, &iterator);
	while (resIteratorGetNext(&iterator, &refName, NULL))
	{
		const char *childFile;

		childStruct = resEditGetSaveCopy(dictHandle, refName);

		childFile = ParserGetFilename(childTable,childStruct);
		if (childFile && stricmp(filename,childFile) == 0)
		{
			eaIndexedAdd(&loadStruct.earrayOfStructs,childStruct);
		}
	}
	resFreeIterator(&iterator);

	if (eaSize(&loadStruct.earrayOfStructs))
	{
		result = ParserWriteTextFile(filename,parse_ResourceLoaderStruct,&loadStruct,iOptionFlagsToMatch,iOptionFlagsToExclude);
	}
	else
	{
		char buf[260];
		fileLocatePhysical(filename, buf);
		
		// Remove will return error if it tries to remove a
		// non-existant file.  As long as the file doesn't exist,
		// we're happy. :)
		remove(buf);
		result = !fileExists(buf);
	}

	eaDestroy(&loadStruct.earrayOfStructs);
	parse_ResourceLoaderStruct[0].name = NULL;
	return result;
}

int ParserWriteReferentFromDictionary(DictionaryHandleOrName dictHandle, const char *writeName, StructTypeField iOptionFlagsToMatch, StructTypeField iOptionFlagsToExclude)
{
	ResourceDictionary *pDictionary = resGetDictionary(dictHandle);
	const char *pDictName = resDictGetParseName(dictHandle);
	const char *refName;
	ParseTable *childTable = resDictGetParseTable(dictHandle);
	int childSize = ParserGetTableSize(childTable);
	ResourceLoaderStruct loadStruct = {0};
	int keyColumn;
	ResourceIterator iterator;
	void *writeStruct = NULL;
	void *backupStruct = NULL;
	const char *oldFilename = NULL;
	const char *newFilename = NULL;
	void *childStruct;
	int result = 0;

	assertmsg(pDictName && childTable && childSize,"Invalid dictionary passed to ParserWriteTextFileFromDictionary!");

	assertmsg((keyColumn = ParserGetTableKeyColumn(childTable)) >= 0,"ParseTable must have Key to be written from dictionary!");

	eaIndexedEnableVoid(&loadStruct.earrayOfStructs,childTable);

	SetUpResourceLoaderParse(pDictName,childSize,childTable,resDictGetDeprecatedName(dictHandle));

	resEditSetSaveWorkingCopy(dictHandle, writeName, true);
	writeStruct = resEditGetWorkingCopy(dictHandle, writeName);
	backupStruct = resEditGetBackupCopy(dictHandle, writeName);

	if (writeStruct)
	{
		newFilename = ParserGetFilename(childTable, writeStruct);
	}
	if (backupStruct)
	{
		oldFilename = ParserGetFilename(childTable, backupStruct);
	}

	if (!writeStruct && !backupStruct)
	{
		parse_ResourceLoaderStruct[0].name = NULL;
		return 1; // no pending changes; return success
	}

	if (oldFilename && (!newFilename || stricmp(newFilename, oldFilename) != 0))
	{
		// Write old file if referent moved

		resInitIterator(dictHandle, &iterator);
		while (resIteratorGetNext(&iterator, &refName, NULL))
		{
			const char *childFile;
			
			childStruct = resEditGetSaveCopy(dictHandle, refName);
			
			childFile = ParserGetFilename(childTable, childStruct);
			if (childFile && stricmp(oldFilename, childFile) == 0 
				&& stricmp(refName, writeName) != 0)
			{
				eaIndexedAdd(&loadStruct.earrayOfStructs, childStruct);
			}
		}
		resFreeIterator(&iterator);

		if (eaSize(&loadStruct.earrayOfStructs))
		{
			result = ParserWriteTextFileEx(oldFilename, parse_ResourceLoaderStruct, &loadStruct, pDictionary->iParserWriteFlags, iOptionFlagsToMatch, iOptionFlagsToExclude,
				NULL, NULL);
		}
		else
		{
			char buf[260];
			ANALYSIS_ASSUME(oldFilename);
			fileLocatePhysical(oldFilename, buf);
			result = !remove(buf); // Remove returns 0 on success and 1 on failure
		}
	}

	eaClear(&loadStruct.earrayOfStructs);

	if (newFilename)
	{
		eaIndexedAdd(&loadStruct.earrayOfStructs, writeStruct);

		resInitIterator(dictHandle, &iterator);
		while (resIteratorGetNext(&iterator, &refName, NULL))
		{
			const char *childFile;		

			childStruct = resEditGetSaveCopy(dictHandle, refName);
					
			childFile = ParserGetFilename(childTable, childStruct);
			if (childFile && stricmp(newFilename, childFile) == 0 
				&& stricmp(refName, writeName) != 0)
			{
				eaIndexedAdd(&loadStruct.earrayOfStructs, childStruct);
			}
		}
		resFreeIterator(&iterator);

		if (eaSize(&loadStruct.earrayOfStructs))
		{
			result = ParserWriteTextFileEx(newFilename, parse_ResourceLoaderStruct, &loadStruct, pDictionary->iParserWriteFlags, iOptionFlagsToMatch, iOptionFlagsToExclude,
				NULL, NULL);
		}
		else
		{
			char buf[260];
			fileLocatePhysical(newFilename, buf);
			result = !remove(buf); // Remove returns 0 on success and 1 on failure
		}
	}

	eaDestroy(&loadStruct.earrayOfStructs);
	parse_ResourceLoaderStruct[0].name = NULL;
	return result;
}

int ParserWriteTextFileFromSingleDictionaryStruct(const char* filename, DictionaryHandleOrName dictHandle, void* structPtr, StructTypeField iOptionFlagsToMatch, StructTypeField iOptionFlagsToExclude)
{
	const char *pDictName = resDictGetParseName(dictHandle);
	ParseTable *childTable = resDictGetParseTable(dictHandle);
	int childSize = ParserGetTableSize(childTable);
	ResourceLoaderStruct loadStruct = {0};
	int keyColumn;
	int result;

	assertmsg(pDictName && childTable && childSize,"Invalid dictionary passed to ParserWriteTextFileFromDictionary!");

	assertmsg((keyColumn = ParserGetTableKeyColumn(childTable)) >= 0,"ParseTable must have Key to be written from dictionary!");

	eaIndexedEnableVoid(&loadStruct.earrayOfStructs,childTable);

	SetUpResourceLoaderParse(pDictName,childSize,childTable,resDictGetDeprecatedName(dictHandle));

	// Instead of looping through dictionary, just write this one struct as if it was in the dictionary.
	eaIndexedAdd(&loadStruct.earrayOfStructs,structPtr);

	result = ParserWriteTextFile(filename,parse_ResourceLoaderStruct,&loadStruct,iOptionFlagsToMatch,iOptionFlagsToExclude);

	eaDestroy(&loadStruct.earrayOfStructs);
	parse_ResourceLoaderStruct[0].name = NULL;
	return result;
}

bool ParserLoadSingleParseTableStruct(const char* filename, const char *pParseName, const char *pDeprecatedName, ParseTable *childTable, void *structptr, int flags)
{
	int childSize = ParserGetTableSize(childTable);
	ResourceLoaderStruct loadStruct = {0};
	int keyColumn;
	TextParserResult eResult = PARSERESULT_SUCCESS;

	assertmsg(pParseName && childTable && childSize,"Invalid dictionary passed to ParserLoadSingleParseTableStruct!");

	assertmsg((keyColumn = ParserGetTableKeyColumn(childTable)) >= 0,"ParseTable must have Key to be loaded into dictionary!");
	
	eaIndexedEnableVoid(&loadStruct.earrayOfStructs,childTable);

	SetUpResourceLoaderParse(pParseName,childSize,childTable,pDeprecatedName);

	if (!ParserReadTextFile(filename, parse_ResourceLoaderStruct, &loadStruct, flags))
	{
		eResult = PARSERESULT_ERROR;
	}

	if (eaSize(&loadStruct.earrayOfStructs)){
		StructCopyVoid(childTable, loadStruct.earrayOfStructs[0], structptr, 0, 0, 0);
	} else {
		eResult = PARSERESULT_ERROR;
	}
	eaDestroyStructVoid(&loadStruct.earrayOfStructs, childTable);

	parse_ResourceLoaderStruct[0].name = NULL;

	return eResult == PARSERESULT_SUCCESS;
}

bool ParserLoadSingleDictionaryStruct(const char* filename, DictionaryHandleOrName dictHandle, void *structptr, int flags)
{
	const char *pDictName = resDictGetParseName(dictHandle);
	const char *pDeprecatedName = resDictGetDeprecatedName(dictHandle);
	ParseTable *childTable = resDictGetParseTable(dictHandle);
	return ParserLoadSingleParseTableStruct(filename, pDictName, pDeprecatedName, childTable, structptr, flags);
}

bool ParserLoadFilesToDictionaryEx(const char* dirs, const char* filemask, const char* persistfile, int flags, 
	DictionaryHandleOrName dictHandle, DependencyList extraBinFileDependencies)
{
	const char *pDictName = resDictGetName(dictHandle);
	ResourceDictionary *pDictionary = resGetDictionary(dictHandle);
	const char *pDictParseName = resDictGetParseName(dictHandle);
	ParseTable *childTable = resDictGetParseTable(dictHandle);
	int childSize = ParserGetTableSize(childTable);
	ResourceLoaderStruct loadStruct = {0};
	char *keyString = NULL;
	int keyColumn;
	int i;
	TextParserResult eResult = PARSERESULT_SUCCESS;
	char metadataFile[MAX_PATH] = {0};

	PERFINFO_AUTO_START_FUNC();

	assertmsg(pDictParseName && childTable && childSize,"Invalid dictionary passed to ParserLoadFilesToDictionary!");

	assertmsg((keyColumn = ParserGetTableKeyColumn(childTable)) >= 0,"ParseTable must have Key to be loaded into dictionary!");
	
	estrStackCreate(&keyString);
	eaIndexedEnableVoid(&loadStruct.earrayOfStructs,childTable);

	SetUpResourceLoaderParse(pDictParseName,childSize,childTable,resDictGetDeprecatedName(dictHandle));

	resEditStartDictionaryModification(dictHandle);

	if (persistfile && strchr(persistfile, ':') == NULL)
	{
		// Don't create metabins for namespace content
		changeFileExt(persistfile,".metabin", metadataFile);
	}

	if (!ParserLoadFilesInternal(dirs, filemask, persistfile, metadataFile[0]?metadataFile:NULL, flags, parse_ResourceLoaderStruct, &loadStruct, dictHandle, false, extraBinFileDependencies))
	{
		eResult = PARSERESULT_ERROR;
	}

	PERFINFO_AUTO_START("per Object", 1);
	for(i=eaSizeSlow(&loadStruct.earrayOfStructs)-1; i>=0; i--)
	{
		void *subStruct = loadStruct.earrayOfStructs[i];
		void *dupItem;
		estrClear(&keyString);
		if (!objGetKeyEString(childTable,loadStruct.earrayOfStructs[i],&keyString))
		{
			ErrorFilenamef(ParserGetFilename(childTable,subStruct), "Entry without name found in dictionary %s!",pDictName);
			StructDestroyVoid(childTable,subStruct);
			eaRemove(&loadStruct.earrayOfStructs,i);
			continue;
		}
		if ((dupItem = resGetObject(dictHandle,keyString))
			&& dupItem != subStruct)
		{
			ErrorFilenameDup(ParserGetFilename(childTable,subStruct),
				ParserGetFilename(childTable,dupItem), keyString, pDictName);
			StructDestroyVoid(childTable,subStruct);
			eaRemove(&loadStruct.earrayOfStructs,i);
			continue;
		}
		if (dupItem != subStruct)
		{
			resEditSetWorkingCopy(pDictionary, keyString, subStruct);
		}
	}
	PERFINFO_AUTO_STOP();

	SET_MIN_RESULT(eResult, resEditRunValidateOnDictionary(RESVALIDATE_POST_BINNING, dictHandle, 0, &loadStruct.earrayOfStructs, NULL));
	SET_MIN_RESULT(eResult, FixupStructLeafFirst(parse_ResourceLoaderStruct, &loadStruct, FIXUPTYPE_POST_BINNING_DURING_LOADFILES, NULL));

	FixupStructLeafFirst(parse_ResourceLoaderStruct, &loadStruct, FIXUPTYPE_POST_LOAD_DURING_LOADFILES_FINAL_LOCATION, NULL);

	// If it was put in shared memory, this would crash
	eaDestroy(&loadStruct.earrayOfStructs);

	resWaitForFinishLoading(dictHandle, NULL, false);
	resEditCommitAllModifications(dictHandle, true);

	parse_ResourceLoaderStruct[0].name = NULL;
	estrDestroy(&keyString);

	PERFINFO_AUTO_STOP();

	return eResult == PARSERESULT_SUCCESS;
}

bool ParserLoadFilesSharedToDictionary_dbg(const char *sharedMemoryName, const char* dirs, const char* filemask, const char* persistfile, int flags, DictionaryHandleOrName dictHandle MEM_DBG_PARMS)
{
	const char *pDictName = resDictGetName(dictHandle);
	ResourceDictionary *pDictionary = resGetDictionary(dictHandle);
	const char *pDictParseName = resDictGetParseName(dictHandle);
	ParseTable *childTable = resDictGetParseTable(dictHandle);
	int childSize = ParserGetTableSize(childTable);
	ResourceLoaderStruct loadStruct = {0};
	char *keyString = NULL;
	int keyColumn;
	int i;
	char *pFixupString;
	SharedMemoryHandle *shared_memory=NULL;
	TextParserResult eResult = PARSERESULT_SUCCESS;
	char metadataFile[MAX_PATH] = {0};

	PERFINFO_AUTO_START_FUNC();

	PERFINFO_AUTO_START("1", 1);

	{
		char buf[64];
		memBudgetAddStructMapping(allocAddString_dbg(getFileNameNoExt(buf, sharedMemoryName), false, false, false MEM_DBG_PARMS_CALL), caller_fname);
	}

	estrStackCreate(&keyString);

	assertmsg(pDictParseName && childTable && childSize,"Invalid dictionary passed to ParserLoadFilesToDictionary!");

	assertmsg((keyColumn = ParserGetTableKeyColumn(childTable)) >= 0,"ParseTable must have Key to be loaded into dictionary!");

	eaIndexedEnableVoid(&loadStruct.earrayOfStructs,childTable);

	SetUpResourceLoaderParse(pDictParseName,childSize,childTable,resDictGetDeprecatedName(dictHandle));

	resEditStartDictionaryModification(dictHandle);

	PERFINFO_AUTO_STOP_START("2", 1);
	if (ParserLoadFromShared(sharedMemoryName,flags,parse_ResourceLoaderStruct,&loadStruct,&shared_memory,&pFixupString,pDictName,resEditGetPendingInfo(dictHandle), false))
	{
		PERFINFO_AUTO_START("from shared", 1);
		if (isDevelopmentMode())
			ParserScanFiles(dirs, filemask);

		for(i=eaSizeSlow(&loadStruct.earrayOfStructs)-1; i>=0; i--)
		{
			estrClear(&keyString);
			if (!objGetKeyEString(childTable,loadStruct.earrayOfStructs[i],&keyString))
			{
				continue;
			}
			resEditSetWorkingCopy(pDictionary, keyString, loadStruct.earrayOfStructs[i]);
		}

		if (pFixupString)
		{		
			fixupSharedMemoryStruct(parse_ResourceLoaderStruct, &loadStruct, &pFixupString);
		}		

		resWaitForFinishLoading(dictHandle, shared_memory, true);
		PERFINFO_AUTO_STOP();
	}
	else if (shared_memory)
	{		
		PERFINFO_AUTO_START("new shared", 1);
		if (persistfile)
		{
			changeFileExt(persistfile,".metabin", metadataFile);
		}

		if (!ParserLoadFilesInternal(dirs, filemask, persistfile, metadataFile[0]?metadataFile:NULL, flags, parse_ResourceLoaderStruct, &loadStruct, dictHandle, false, NULL))
		{
			eResult = PARSERESULT_ERROR;
		}

		PERFINFO_AUTO_START("iterate earray1", 1);
		for(i=eaSizeSlow(&loadStruct.earrayOfStructs)-1; i>=0; i--)
		{
			void *subStruct = loadStruct.earrayOfStructs[i];
			void *dupItem;
			estrClear(&keyString);
			if (!objGetKeyEString(childTable,loadStruct.earrayOfStructs[i],&keyString))
			{
				ErrorFilenamef(ParserGetFilename(childTable,subStruct), "Entry without name found in dictionary %s!",pDictName);
				StructDestroyVoid(childTable,subStruct);
				eaRemove(&loadStruct.earrayOfStructs,i);
				continue;
			}
			if ((dupItem = resGetObject(dictHandle,keyString))
				&& dupItem != subStruct)
			{
				ErrorFilenameDup(ParserGetFilename(childTable,subStruct),
					ParserGetFilename(childTable,dupItem), keyString, pDictName);
				StructDestroyVoid(childTable,subStruct);
				eaRemove(&loadStruct.earrayOfStructs,i);
				continue;
			}
			if (dupItem != subStruct)
			{
				resEditSetWorkingCopy(pDictionary, keyString, subStruct);
			}
		}
		PERFINFO_AUTO_STOP();

		SET_MIN_RESULT(eResult, resEditRunValidateOnDictionary(RESVALIDATE_POST_BINNING, dictHandle, 0, &loadStruct.earrayOfStructs, NULL));
		SET_MIN_RESULT(eResult, FixupStructLeafFirst(parse_ResourceLoaderStruct, &loadStruct, FIXUPTYPE_POST_BINNING_DURING_LOADFILES, NULL));

		PERFINFO_AUTO_START("iterate earray2", 1);
		for(i=eaSizeSlow(&loadStruct.earrayOfStructs)-1; i>=0; i--)
		{
			estrClear(&keyString);
			if (!objGetKeyEString(childTable,loadStruct.earrayOfStructs[i],&keyString))
			{
				continue;
			}

			resEditSetWorkingCopy(pDictionary, keyString, NULL);
		}
		PERFINFO_AUTO_STOP();

		ParserMoveToShared(shared_memory,flags,parse_ResourceLoaderStruct,&loadStruct,resEditGetPendingInfo(dictHandle));

		PERFINFO_AUTO_START("iterate earray3", 1);
		for(i=eaSizeSlow(&loadStruct.earrayOfStructs)-1; i>=0; i--)
		{
			estrClear(&keyString);
			if (!objGetKeyEString(childTable,loadStruct.earrayOfStructs[i],&keyString))
			{
				continue;
			}
			resEditSetWorkingCopy(pDictionary, keyString, loadStruct.earrayOfStructs[i]);
		}
		PERFINFO_AUTO_STOP();
		
		FixupStructLeafFirst(parse_ResourceLoaderStruct, &loadStruct, FIXUPTYPE_POST_LOAD_DURING_LOADFILES_FINAL_LOCATION, NULL);

		resWaitForFinishLoading(dictHandle, shared_memory, false);

		sharedMemoryCommitButKeepLocked(shared_memory);

		PERFINFO_AUTO_STOP();
	}
	else
	{
		bool retval;

		resEditRevertAllModifications(dictHandle);
		// Error with shared memory, load like normal
		eaDestroy(&loadStruct.earrayOfStructs);		
		parse_ResourceLoaderStruct[0].name = NULL;
		estrDestroy(&keyString);
		
		retval = ParserLoadFilesToDictionary(dirs, filemask, persistfile, flags, dictHandle);
		PERFINFO_AUTO_STOP();
		PERFINFO_AUTO_STOP();
		return retval;
	}

	PERFINFO_AUTO_STOP_START("3", 1);

	if (!isSharedMemory(loadStruct.earrayOfStructs)) {
		eaDestroy(&loadStruct.earrayOfStructs);
	}

	resEditCommitAllModifications(dictHandle, true);

	parse_ResourceLoaderStruct[0].name = NULL;
	estrDestroy(&keyString);

	PERFINFO_AUTO_STOP();

	PERFINFO_AUTO_STOP();

	return eResult == PARSERESULT_SUCCESS;
}

bool ParserReloadFileToDictionaryWithFlags(const char *filename, DictionaryHandleOrName dictHandle, int flags)
{
	const char *pDictName = resDictGetName(dictHandle);
	ResourceDictionary *pDictionary = resGetDictionary(dictHandle);
	const char *pDictParseName = resDictGetParseName(dictHandle);
	ParseTable *childTable = resDictGetParseTable(dictHandle);
	int childSize = ParserGetTableSize(childTable);
	ResourceLoaderStruct loadStruct = {0};
	int keyColumn;
	char keyString[MAX_STRING_LENGTH];
	ResourceIterator iterator;
	void *childStruct;
	const char *childName;
	int i;
	bool didUnshare = false;
	TextParserResult eResult;
	const char **fileMembers = NULL;

	assertmsg(pDictParseName && childTable && childSize,"Invalid dictionary passed to ParserWriteTextFileFromDictionary!");

	assertmsg((keyColumn = ParserGetTableKeyColumn(childTable)) >= 0,"ParseTable must have Key to be written from dictionary!");
	
	if (!hasTokenType(childTable, TOK_CURRENTFILE_X))
	{
		Errorf("Dictionary %s must have a current file token in order to have ReloadToDictionary work", pDictName);
		return false;
	}

	if (isDevelopmentMode() && pDictionary->ppDictSharedMemory && !(flags & PARSER_DONTSETEDITMODE)) 
	{
		// Reloading any resource for dictionary that is in shared memory 
		// in development mode marks the shared memory segments as no longer useful
		sharedMemoryEnableEditorMode();
	}

	eaIndexedEnableVoid(&loadStruct.earrayOfStructs, childTable);

	SetUpResourceLoaderParse(pDictParseName,childSize,childTable,resDictGetDeprecatedName(dictHandle));

	resInitIterator(dictHandle, &iterator);
	
	while (resIteratorGetNext(&iterator, &childName, NULL))
	{
		const char *childFile = resGetLocation(dictHandle, childName);
		if (childFile)
		{
			if (stricmp(filename,childFile) == 0)
			{
				eaPush(&fileMembers, allocAddString(childName));
			}
			else if (flags & PARSER_IGNORE_EXTENSIONS)
			{
				char file1[MAX_PATH], file2[MAX_PATH];
				changeFileExt(filename, ".test", file1);
				changeFileExt(childFile, ".test", file2);
				if (stricmp(file1,file2) == 0)
				{
					eaPush(&fileMembers, allocAddString(childName));
				}
			}
		}
	}
	resFreeIterator(&iterator);

	resEditStartDictionaryModification(dictHandle);

	eResult = ParserLoadFilesInternal(NULL, filename, NULL, NULL, flags | PARSER_INTERNALLOAD, parse_ResourceLoaderStruct, &loadStruct, NULL, false, NULL) ? PARSERESULT_SUCCESS : PARSERESULT_ERROR;
	
	for (i = 0; i < eaSize(&fileMembers); i++)
	{
		int dupIndex;
		childName = fileMembers[i];
		childStruct = resGetObject(dictHandle, childName);

		if (isSharedMemory(childStruct) && !didUnshare && !(flags & PARSER_DONTSETEDITMODE)) 
		{
			// If in dev mode and something reloads, we want to mark the shared memory as dead
			// so that future dev mode servers start and don't trust shared memory.
			// In production mode we can't support reloads into shared memory!
			if (isDevelopmentMode())
			{
				sharedMemoryEnableEditorMode();
				didUnshare = true;	
			}
			else
			{
				assertmsgf(0, "ParserReloadFileToDictionary: Attempt to reload into shared memory from file %s", filename);
			}
		}

		dupIndex = eaIndexedFindUsingString(&loadStruct.earrayOfStructs, childName);
		if (dupIndex < 0)
		{
			// In old list but not new list, remove it
			resEditSetWorkingCopy(pDictionary, childName, NULL);

			eaRemove(&fileMembers, i);
			i--;

			continue;
		}
	}

	for(i=0; i < eaSize(&loadStruct.earrayOfStructs); i++)
	{
		void *dupItem;
		childStruct = loadStruct.earrayOfStructs[i];
		if (!objGetKeyString(childTable,childStruct,SAFESTR(keyString)))
		{
			ErrorFilenamef(filename, "Entry without name found in dictionary %s!",pDictName);
			StructDestroyVoid(childTable,childStruct);
			eaRemove(&loadStruct.earrayOfStructs,i);
			i--;
			continue;
		}
		childName = allocAddString(keyString);

		if ((dupItem = resGetObject(dictHandle, childName))
			&& eaFind(&fileMembers, childName) == -1)
		{
			// Duplicate in some other file
			ErrorFilenameDup(filename, ParserGetFilename(childTable,dupItem), childName, pDictName);
			StructDestroyVoid(childTable,childStruct);
			eaRemove(&loadStruct.earrayOfStructs,i);
			i--;
			continue;
		}

		resEditSetWorkingCopy(pDictionary, keyString, childStruct);
	}

	// Applies inheritance to earray and dictionary, using earray as override
	resApplyInheritanceToDictionary(dictHandle, &loadStruct.earrayOfStructs);

	SET_MIN_RESULT(eResult, resEditRunValidateOnDictionary(RESVALIDATE_POST_TEXT_READING, dictHandle, 0, &loadStruct.earrayOfStructs, NULL));
	SET_MIN_RESULT(eResult, resEditRunValidateOnDictionary(RESVALIDATE_POST_BINNING, dictHandle, 0, &loadStruct.earrayOfStructs, NULL));
	resEditRunValidateOnDictionary(RESVALIDATE_FINAL_LOCATION, dictHandle, 0, &loadStruct.earrayOfStructs, NULL);
	for(i=eaSizeSlow(&loadStruct.earrayOfStructs)-1; i>=0; i--)
	{
		SET_MIN_RESULT(eResult, FixupStructLeafFirst(childTable, loadStruct.earrayOfStructs[i], FIXUPTYPE_POST_RELOAD, NULL));
	}

	eaDestroy(&loadStruct.earrayOfStructs);
	eaDestroy(&fileMembers);

	if (resEditCommitAllModifications(dictHandle, false))
	{
		if (!(flags & PARSER_DONTSETEDITMODE)) {
			const char* dictName = resDictGetName(dictHandle);
			if( dictName ) {
				eaPushUnique(&gQueuedDictsToValidate, allocAddString(dictName));
				gQueuedDictsTicksToValidate = 4;
			}
		}
	}

	parse_ResourceLoaderStruct[0].name = NULL;
	return eResult == PARSERESULT_SUCCESS;
}

void ParserValidateQueuedDicts(void)
{
	if( !eaSize( &gQueuedDictsToValidate )) {
		return;
	}
	if( gQueuedDictsTicksToValidate > 0 ) {
		--gQueuedDictsTicksToValidate;
		return;
	}

	loadstart_printf( "Validating dictionaries for reload..." );
	{
		int it;
		for( it = 0; it != eaSize( &gQueuedDictsToValidate ); ++it ) {
			resValidateCheckAllReferencesForDictionary(gQueuedDictsToValidate[it]);
		}
		eaClear( &gQueuedDictsToValidate );
	}
	loadend_printf( "Done." );
}


static FileScanAction ParserScanCallback(const char* path, FolderNode *node, void *unused, void *pUserData)
{
	TextParserState *pTps = pUserData;

	return FSA_NO_EXPLORE_DIRECTORY;
}


// Function that scans a directory of files, which is used in dev mode to speed up loading
void ParserScanFiles(const char* dirs, const char* filemask)
{
	TextParserState tps;

	TextParserState_Init(&tps);

	// init info
	if (filemask)
	{
		char *mask, *last;
		tps.old_filemask = strdup(filemask);

		mask = strtok_quoted_r(tps.old_filemask, ";", ";", &last);
#if !_PS3
		if (mask && (mask[0] == '/' || mask[0] == '\\')) mask++;
#endif
		eaPush(&tps.lf_filemasks, mask);

		while (mask = strtok_quoted_r(NULL, ";", ";", &last))
		{
#if !_PS3
			if (mask[0] == '/' || mask[0] == '\\') mask++;
#endif
			eaPush(&tps.lf_filemasks, mask);
		}
	}

	if (dirs)
	{
		char *dir, *last;
		tps.old_dirs = strdup(dirs);

		dir = strtok_quoted_r(tps.old_dirs, ";", ";", &last);
#if !_PS3
		if (dir && (dir[0] == '/' || dir[0] == '\\')) dir++;
#endif
		eaPush(&tps.lf_dirs, dir);

		while (dir = strtok_quoted_r(NULL, ";", ";", &last))
		{
#if !_PS3
			if (dir[0] == '/' || dir[0] == '\\') dir++;
#endif
			eaPush(&tps.lf_dirs, dir);
		}
	}

	if (tps.lf_dirs)
	{	
		int i;
		if(errorGetVerboseLevel())
			loadstart_printf("Scanning files...");
		for (i = 0; i < eaSize(&tps.lf_dirs); i++)
		{
			fileScanAllDataDirs2(tps.lf_dirs[i], ParserScanCallback, &tps);
		}
		if(errorGetVerboseLevel())
			loadend_printf("done");
	}

	TextParserState_Destroy(&tps);
}


static void LazyInitErrorTextFileCaching(void)
{
	static U32 siThreadID = 0;

	ONCE( siThreadID = GetCurrentThreadId() );

	assertmsgf(siThreadID == GetCurrentThreadId(), "All ErrorTextFileCache stuff must occur in the same thread");

}

static FILE *spTextFileForCachedErrors = NULL;
static int siErrorCountForCachedErrors = 0;

static char *spDirNameForCachedErrors = NULL;
static char *spFileNameForCachedErrors = NULL;


static void MakeDirNameForCachedErrors(char *pFullBinPath)
{
	char *pDir = NULL;
	char *pName = NULL;

	estrStackCreate(&pDir);
	estrStackCreate(&pName);

	estrGetDirAndFileNameAndExtension(pFullBinPath, &pDir, &pName, NULL);
	estrPrintf(&spDirNameForCachedErrors, "%s\\%u_%s", spFolderForCachedErrorTextFiles, cryptAdler32String(pDir), pName);

	estrDestroy(&pDir);
	estrDestroy(&pName);
}

//assumes that MakeDirNameForCachedErrors was already called 
static void MakeFinalFilenameForCachedErrors(char **ppOutFileName, char *pFullBinPath)
{
	size_t iBinSize = fileSize(pFullBinPath);
	U32 iModTime = fileLastChangedSS2000(pFullBinPath);

	assert(iBinSize && iModTime);


	assert(fileExists(pFullBinPath));
	estrPrintf(ppOutFileName, "%s\\%u_%u.txt", spDirNameForCachedErrors, (U32)iBinSize, iModTime);
}

static void OpenErrorFileCachingFile(void)
{
	char systemString[1024];

	sprintf(systemString, "erase /f /s /q %s\\*.*", spDirNameForCachedErrors);
	system(systemString);

	estrPrintf(&spFileNameForCachedErrors, "%s\\errors.txt", spDirNameForCachedErrors);

	mkdirtree_const(spFileNameForCachedErrors);

	spTextFileForCachedErrors = fopen(spFileNameForCachedErrors, "wt");
	assert(spTextFileForCachedErrors);

	fprintf(spTextFileForCachedErrors, "{\n\n");	
}

static void ErrorTextFileCachingCB(ErrorData *pData)
{
	char *pOutString = NULL;

	LazyInitErrorTextFileCaching();

	if (!spTextFileForCachedErrors)
	{
		OpenErrorFileCachingFile();
	}

	estrStackCreate(&pOutString);
	ParserWriteText(&pOutString, parse_ErrorData, pData, 0, 0, 0);
	fprintf(spTextFileForCachedErrors, "Error\n%s\n\n", pOutString);
	estrDestroy(&pOutString);

	siErrorCountForCachedErrors++;
	
}



//always write into the same single file
static void BeginErrorTextFileCaching(char *pFullBinPath)
{
	if (!spFolderForCachedErrorTextFiles)
	{
		return;
	}

	siErrorCountForCachedErrors = 0;

	LazyInitErrorTextFileCaching();

	SetErrorSendingForkFunction(ErrorTextFileCachingCB);

	MakeDirNameForCachedErrors(pFullBinPath);
}

static void EndErrorTextFileCaching(void)
{
	SetErrorSendingForkFunction(NULL);

	if (spTextFileForCachedErrors)
	{
		fclose(spTextFileForCachedErrors);
		spTextFileForCachedErrors = NULL;
	}
}

static void FinalizeErrorTextFileCaching(char *pFullBinPath)
{
	if (siErrorCountForCachedErrors)
	{
		char *pFinalFileName = NULL;

		assert(fileExists(spFileNameForCachedErrors));

		MakeFinalFilenameForCachedErrors(&pFinalFileName, pFullBinPath);

		rename(spFileNameForCachedErrors, pFinalFileName);

		estrDestroy(&pFinalFileName);
	}
}

AUTO_STRUCT;
typedef struct ErrorDataCacheList
{
	ErrorData **ppErrors; AST(NAME(Error))
} ErrorDataCacheList;

static void ErrorTextFileCaching_EmitErrors(char *pFullBinPath)
{
	char *pFinalName = NULL;
	ErrorDataCacheList *pList = NULL;

	LazyInitErrorTextFileCaching();

	estrStackCreate(&pFinalName);
	MakeFinalFilenameForCachedErrors(&pFinalName, pFullBinPath);

	pList = StructCreate(parse_ErrorDataCacheList);
	ParserReadTextFile(pFinalName, parse_ErrorDataCacheList, pList, 0);
	estrDestroy(&pFinalName);

	if (eaSize(&pList->ppErrors))
	{
		printf("Found cached errors associated with %s:\n", pFullBinPath);
	}

	FOR_EACH_IN_EARRAY_FORWARDS(pList->ppErrors, ErrorData, pError)
	{
		printf("Reporting cached error for filename %s: %s\n",
			pError->pDataFile, pError->pErrorString);

		errorTrackerSendError(pError);
	}
	FOR_EACH_END;

	StructDestroy(parse_ErrorDataCacheList, pList);
}

static bool CachedErrorTextFileExistsAndIsUpToDate(char *pFullBinPath)
{
	char *pFinalName = NULL;
	bool bExists = false;


	LazyInitErrorTextFileCaching();

	if (!fileExists(pFullBinPath))
	{
		return false;
	}

	estrStackCreate(&pFinalName);
	MakeDirNameForCachedErrors(pFullBinPath);
	MakeFinalFilenameForCachedErrors(&pFinalName, pFullBinPath);

	bExists = fileExists(pFinalName);

	estrDestroy(&pFinalName);

	return bExists;
}


static bool sbLoadFilesPrintf = false;
AUTO_CMD_INT(sbLoadFilesPrintf, LoadFilesPrintf) ACMD_CMDLINE;

enum
{
	LFPRINT_NORMAL,
	LFPRINT_BINFILES,
	LFPRINT_TEXTFILES,
	LFPRINT_ERROR,
};

static int GetColorFromPrintType(int ePrintType)
{
	switch (ePrintType)
	{
	case LFPRINT_NORMAL:
		return COLOR_RED|COLOR_BLUE|COLOR_BRIGHT | COLOR_GREEN;
	case LFPRINT_BINFILES:
		return COLOR_BLUE|COLOR_BRIGHT | COLOR_GREEN;
	case LFPRINT_TEXTFILES:
		return COLOR_RED|COLOR_BLUE|COLOR_BRIGHT;
	}

	return COLOR_RED|COLOR_BRIGHT;
	
}

#define LOADFILES_PRINTF(ePrintType, fmt, ...) if (sbLoadFilesPrintf && persistfile) printfColor(GetColorFromPrintType(ePrintType), fmt, __VA_ARGS__)

//needs to use both enumParserLoadFlags and enumResourceLoadFlags
static char *GetStringForParserFlags(int iFlags)
{
	static char *spRetVal = NULL;
	U32 iFlag;

	estrClear(&spRetVal);

	for (iFlag = 1; iFlag; iFlag <<= 1)
	{
		if (iFlags & iFlag)
		{
			const char *pFlagName = StaticDefineInt_FastIntToString(enumParserLoadFlagsEnum, iFlag);
			if (!pFlagName)
			{
				pFlagName = StaticDefineInt_FastIntToString(enumResourceLoadFlagsEnum, iFlag);
			}

			if (pFlagName)
			{
				estrConcatf(&spRetVal, "%s%s", estrLength(&spRetVal) ? " | " : "", pFlagName);
			}
		}
	}

	return spRetVal;
}


// filemasks are separated by semicolons
static bool ParserLoadFilesInternal(const char* dirs, const char* filemask, const char* persistfile, const char* metadatafile, int flags, ParseTable pti[], 
	void* structptr, DictionaryHandleOrName dictHandle, bool doNotBin, DependencyList preExistingBinFileDependencies)
{
	char persistfilepath[CRYPTIC_MAX_PATH] = {0}, buf[CRYPTIC_MAX_PATH] = {0}, metadatafilepath[CRYPTIC_MAX_PATH] = {0};
	bool nameSpaceMatches = false;
	int forcerebuild = flags & PARSER_FORCEREBUILD;
	StructTypeField excludeFlags = 0;
	ThreadAgnosticMutex binMutex = 0;
	int iColumnForDoingInheritance = -1;
	TextParserState tps;

	PERFINFO_AUTO_START_FUNC();

	PERFINFO_AUTO_START("prep", 1);

	LOADFILES_PRINTF(LFPRINT_NORMAL, "\nParserLoadFilesInternal called. Dirs: %s. Filemask: %s. Persistfile: %s. flags: %s. Tpi: %s\n",
		dirs, filemask, persistfile, GetStringForParserFlags(flags), ParserGetTableName(pti));


	TextParserState_Init(&tps);

	if (flags & PARSER_NOERRORFSONPARSE)
	{
		tps.lf_noErrorfsOnRead = 1;
	}
	

	if (dictHandle)
	{
		int i;
		FORALL_PARSETABLE(pti, i)
		{
			if (ParserColumnIsIndexedEArray(pti, i, NULL))		
			{
				iColumnForDoingInheritance = i;
				break;
			}
		}

		assertmsg(iColumnForDoingInheritance != -1, "Dictionary loading can only be done on a struct with an indexed earray of structs");
	}
	
	if (!(flags & PARSER_BINS_ARE_SHARED) && !IsServer())
	{
		flags |= PARSER_CLIENTSIDE;
	}
	else if (!(flags & PARSER_BINS_ARE_SHARED)) 
	{
		flags |= PARSER_SERVERSIDE;
	}

	if (flags & PARSER_SERVERSIDE)
	{
		excludeFlags = TOK_CLIENT_ONLY;
	}
	else if (flags & PARSER_CLIENTSIDE)
	{
		excludeFlags = TOK_SERVER_ONLY;
	}

	if (metadatafile && (!resDictHasInfoIndex(dictHandle)))
	{
		// Only save out metadata if it's actually a dictionary with metadata, and we're in development mode
		metadatafile = NULL;
	}

	tps.preExistingDependencies = preExistingBinFileDependencies;

	TEST_PARSE_TABLE(pti);

	if (isProductionMode() || gbBinsMustExist)
	{
		forcerebuild = false;
	}

#if !_PS3
	// we know the client doesn't actually mean a non-relative path
	if (persistfile && (persistfile[0] == '/' || persistfile[0] == '\\')) persistfile++;
#endif

	// init info
	if (filemask)
	{
		char *mask, *last;
		tps.old_filemask = strdup(filemask);

		mask = strtok_quoted_r(tps.old_filemask, ";", ";", &last);
#if !_PS3
		if (mask && (mask[0] == '/' || mask[0] == '\\' && mask[1] != '\\')) mask++;
#endif
		eaPush(&tps.lf_filemasks, mask);

		while (mask = strtok_quoted_r(NULL, ";", ";", &last))
		{
#if !_PS3
			if (mask[0] == '/' || mask[0] == '\\' && mask[1] != '\\') mask++;
#endif
			eaPush(&tps.lf_filemasks, mask);
		}
	}

	if (dirs)
	{
		char *dir, *last;
		tps.old_dirs = strdup(dirs);
		
		dir = strtok_quoted_r(tps.old_dirs, ";", ";", &last);
#if !_PS3
		if (dir && (dir[0] == '/' || dir[0] == '\\')) dir++;
#endif
		eaPush(&tps.lf_dirs, dir);

		while (dir = strtok_quoted_r(NULL, ";", ";", &last))
		{
#if !_PS3
			if (dir[0] == '/' || dir[0] == '\\') dir++;
#endif
			eaPush(&tps.lf_dirs, dir);
		}
	}

	tps.lf_pti = pti;
	tps.lf_structptr = structptr;
	tps.flags = flags;

	// build the path to the bin file
	if (persistfile)
	{
		//the name of the mutext must not contain colons
		char mutexName[CRYPTIC_MAX_PATH];
		char nameSpace[RESOURCE_NAME_MAX_SIZE];
		char baseObjectName[RESOURCE_NAME_MAX_SIZE];
		
		PERFINFO_AUTO_START("build persist file path", 1);

		if (fileIsAbsolutePath(persistfile))
		{
			strcpy(persistfilepath, persistfile);
		}	
		else
		{			
			if (fileGetNameSpacePath(persistfile, nameSpace, baseObjectName))
			{
				sprintf(persistfilepath, "%s:/", nameSpace);
				if ((flags & PARSER_SERVERSIDE) && !(flags & PARSER_BINS_ARE_SHARED))
					strcat(persistfilepath, "server/bin");
				else
					strcat(persistfilepath, "bin");
				strcat(persistfilepath, baseObjectName);
				if(gpcMakeBinsAndExitNamespace && stricmp(gpcMakeBinsAndExitNamespace, nameSpace)==0)
					nameSpaceMatches = true;
			}
			else
			{
				if ((flags & PARSER_SERVERSIDE) && !(flags & PARSER_BINS_ARE_SHARED))
					strcat(persistfilepath, "server/bin/");
				else
					strcat(persistfilepath, "bin/");
				strcat(persistfilepath, persistfile);
			}
		}

		makeLegalMutexName(mutexName, persistfilepath);
		binMutex = acquireThreadAgnosticMutex(mutexName);

		binNotifyTouchedOutputFile(persistfilepath);

		if (metadatafile)
		{
			if (fileIsAbsolutePath(metadatafile))
			{
				strcpy(metadatafilepath, metadatafile);
			}
			else
			{	
				if (fileGetNameSpacePath(metadatafile, nameSpace, baseObjectName))
				{
					sprintf(metadatafilepath, "%s:/", nameSpace);
					if ((flags & PARSER_SERVERSIDE) && !(flags & PARSER_BINS_ARE_SHARED))
						strcat(metadatafilepath, "server/bin/metadata");
					else
						strcat(metadatafilepath, "bin/metadata");
					strcat(metadatafilepath, baseObjectName);
				}
				else
				{				
					if ((flags & PARSER_SERVERSIDE) && !(flags & PARSER_BINS_ARE_SHARED))
						strcat(metadatafilepath, "server/bin/metadata/");
					else
						strcat(metadatafilepath, "bin/metadata/");
					strcat(metadatafilepath, metadatafile);
				}
			}

			binNotifyTouchedOutputFile(metadatafilepath);
		}
		else
		{
			*metadatafilepath = 0;
		}
		
		PERFINFO_AUTO_STOP();
	}
	else
	{
		*persistfilepath = 0;
		*metadatafilepath = 0;
	}

	PERFINFO_AUTO_STOP();// prep.
	PERFINFO_AUTO_START("check bin", 1);

	if (persistfile && ((!tps.lf_forcebincreate || tps.lf_forceBinCreateDoesntForceTextFileReading) && !forcerebuild) 
		|| (tps.lf_forcebincreate && gpcMakeBinsAndExitNamespace && !nameSpaceMatches) 
		|| persistfile && gbForceReadBinFilesForMultiplexedMakebins)
	{
		char *path=NULL;

		PERFINFO_AUTO_START("read persist file", 1);

		if (gbOnlyReadBinFilesIfCachedErrorFileExists)
		{
			if (!CachedErrorTextFileExistsAndIsUpToDate(persistfilepath))
			{
				tps.flags |= PARSER_ONLY_LOAD_BIN_FILE_IF_IT_HAD_NO_ERRORS;
			}
			else
			{
				tps.flags |= PARSER_ALLOW_BINS_WITH_ERRORS_AND_RELOADING;
			}
		}

		path = fileLocateReadBin(persistfilepath, buf);
		if (path)
		{
			SimpleBufHandle binfile;

			binfile = ParserIsPersistNewer(&tps, filemask, path, persistfilepath, pti, NULL, flags & PARSER_DEVMODE);
			if (binfile)
			{
				bool bBinLoaded = false;
				FileList filesWithErrors = NULL;

				if (isDevelopmentMode() && (flags & PARSER_ALLOW_BINS_WITH_ERRORS_AND_RELOADING))
				{
					FileListCreate(&filesWithErrors);
				}


				PERFINFO_AUTO_START("found bin", 1);

				// try loading persist file
				bBinLoaded = ParserReadBinaryFile(binfile, pti, structptr, NULL, filesWithErrors ? &filesWithErrors : NULL, NULL, NULL, 0, excludeFlags, true);

				LOADFILES_PRINTF(LFPRINT_BINFILES, "Read binary file %s... result %d\n", persistfilepath, bBinLoaded);



				if (bBinLoaded && metadatafile)
				{
					//FileList binDep = NULL;
					ResourceDictionaryInfo *pInfoHolder = resEditGetPendingInfo(dictHandle);
					char *metadatapath = fileLocateReadBin(metadatafilepath, buf);

					if (metadatapath)
					{
						bBinLoaded = ParserOpenReadBinaryFile(NULL, metadatapath, parse_ResourceDictionaryInfo, pInfoHolder, NULL, NULL, NULL, NULL, 0, 0, 0);
						/*if (bBinLoaded)
						{
							bBinLoaded = FileListAllFilesUpToDate(&binDep);
						}*/
					}
					else
					{
						bBinLoaded = false;
					}
					if (!bBinLoaded)
					{
						verbose_printf("ParserLoadFiles: error loading metadata bin %s, loading text files instead\n", metadatafile);
					}
					//FileListDestroy(&binDep);
				}
				if (bBinLoaded)
				{
					if (binMutex) releaseThreadAgnosticMutex(binMutex);

					if (gbOnlyReadBinFilesIfCachedErrorFileExists)
					{
						ErrorTextFileCaching_EmitErrors(persistfilepath);
					}

					if (filesWithErrors)
					{
						FileListForceReloadAll(&filesWithErrors);
						FileListDestroy(&filesWithErrors);
					}
					TextParserState_Destroy(&tps);
					PERFINFO_AUTO_STOP();// found bin.
					PERFINFO_AUTO_STOP();// read persist file.
					PERFINFO_AUTO_STOP();// check bin.
					PERFINFO_AUTO_STOP();// FUNC.
					return true;
				} 
				else
				{
					verbose_printf("ParserLoadFiles: error loading %s, loading text files instead\n", persistfile);
					if (filesWithErrors)
					{
						FileListDestroy(&filesWithErrors);
					}
				}
				StructDeInitVoid(pti, structptr);
				PERFINFO_AUTO_STOP();// found bin.
			}
			else
			{
				LOADFILES_PRINTF(LFPRINT_TEXTFILES, "%s outdated... going to load text files instead\n", persistfilepath);
				verbose_printf("ParserLoadFiles: %s outdated, loading text files instead\n", persistfile);
			}
		}
		else
		{
			LOADFILES_PRINTF(LFPRINT_TEXTFILES, "%s didn't exist, or cached error file didn't exist, going to load text files instead\n", persistfilepath);
			verbose_printf("ParserLoadFiles: couldn't find %s, loading text files instead\n", persistfile);
		}
		
		PERFINFO_AUTO_STOP();// read persist file.
	}
	else
	{
		LOADFILES_PRINTF(LFPRINT_TEXTFILES, "Not loading bin files for %s due to flags/options\n", ParserGetTableName(pti));
	}

	// Do not attempt to parse data files for any reason if the program is running in production mode.
	//  unless we weren't given a persistfile (bin file) name, then they must want to parse the source
	//  files (used by Gimme to load config files)
	if( ((!fileIsUsingDevData() || gbProductionModeBins) && persistfile && !fileIsProductionEditAllowed(persistfile) && !(flags & PARSER_DEVMODE))
		|| doNotBin) {
		TextParserState_Destroy(&tps);
		if (binMutex) releaseThreadAgnosticMutex(binMutex);

		LOADFILES_PRINTF(LFPRINT_ERROR, "Doing prod mode early-out\n");

		PERFINFO_AUTO_STOP();// check bin.
		PERFINFO_AUTO_STOP();// FUNC.
		return false;
	}

	// otherwise, we have to load from text files
	PERFINFO_AUTO_STOP();// check bin.
	PERFINFO_AUTO_START("load from text", 1);

	if (!tps.parselist)
		FileListCreate(&tps.parselist);

	if (tps.flags & PARSER_ALLOW_BINS_WITH_ERRORS_AND_RELOADING)
	{
		if (!tps.FilesWithErrors)
		{
			FileListCreate(&tps.FilesWithErrors);
		}
	}

	if (spFolderForCachedErrorTextFiles && persistfilepath[0])
	{
		BeginErrorTextFileCaching(persistfilepath);
	}
		
	tps.lf_ignoreempty = !!(flags & PARSER_OPTIONALFLAG);
	if (!tps.lf_dirs)
	{
		int i;
		for (i = 0; i < eaSize(&tps.lf_filemasks); i++)
		{
			// load basic files
			tps.parser_relpath = persistfile?persistfile:tps.lf_filemasks[i];
			ParserLoadFile(&tps, tps.lf_filemasks[i], tps.lf_ignoreempty);

			tps.parser_relpath = 0;
		}
	}
	else // recurse subdirs, optional prefixes are handled just by mask matching
	{
		int i;
		tps.parser_relpath = persistfile;
		tps.lf_include_hidden = !!(flags & PARSER_INCLUDEHIDDEN);
		if(errorGetVerboseLevel())
			loadstart_printf("Loading files...");
		for (i = 0; i < eaSize(&tps.lf_dirs); i++)
		{
			fileScanAllDataDirs2(tps.lf_dirs[i], ParserLoadCallback, &tps);
		}
		tps.parser_relpath = 0;
		if(errorGetVerboseLevel())
			loadend_printf("done");
	}
	tps.lf_ignoreempty = 0;
	
	EndErrorTextFileCaching();

	LOADFILES_PRINTF(LFPRINT_TEXTFILES, "Text files loaded for %s\n", ParserGetTableName(pti));

	if (tps.lf_loadedonefile && persistfile && persistfile[0] && gbBinsMustExist && !(flags & PARSER_OPTIONALFLAG))
	{
		assertmsgf(0, "BinsMustExist is set, but bin file %s does not exist", persistfile);
	}
		

	//done loading from text files, call the TEXT_READ callback on the root
	PERFINFO_AUTO_START("fixup", 1);
	{
		int loadedok;
		ResourceDictionary *pDictionary = resGetDictionary(dictHandle);
		TextParserAutoFixupCB *pFixupCB;

		StructSortIndexedArrays(pti,structptr);

		pFixupCB = ParserGetTableFixupFunc(pti);

		if (pFixupCB)
		{
			loadedok = (pFixupCB(structptr, FIXUPTYPE_POST_TEXT_READ, &tps) == PARSERESULT_SUCCESS);
			tps.lf_loadedok &= loadedok;
		}

		if (dictHandle)
		{
			int i;
			ParseTable *childTable = pti[iColumnForDoingInheritance].subtable;
			void ***pppEArray = TokenStoreGetEArray_inline(pti, &pti[iColumnForDoingInheritance], iColumnForDoingInheritance, structptr, NULL);

			assert(pppEArray); // list of loaded structs, e.g. critters

			if (metadatafile)
				ParserBinAddParseTableDep(&tps, parse_ResourceInfo); // Depend on resource info so metabins don't get out of sync

			for (i = eaSize(pppEArray) - 1; i >= 0; i--) // add structs to dictionary
			{			
				void *dupItem;
				char keyString[MAX_STRING_LENGTH];
				void *subStruct = eaGet(pppEArray, i);
				if (!subStruct)
				{
					eaRemove(pppEArray,i);
					continue;
				}

				if (!objGetKeyString(childTable,subStruct,SAFESTR(keyString)))
				{
					ErrorFilenamef(ParserGetFilename(childTable,subStruct), "Entry without name found in dictionary %s!",
						resDictGetName(dictHandle));
					StructDestroyVoid(childTable,subStruct);
					eaRemove(pppEArray,i);
					tps.lf_loadedok = 0;
					continue;
				}
				if (dupItem = resGetObject(dictHandle,keyString))
				{
					ErrorFilenameDup(ParserGetFilename(childTable,subStruct),
						ParserGetFilename(childTable,dupItem), keyString, 
						resDictGetName(dictHandle));
					StructDestroyVoid(childTable,subStruct);
					eaRemove(pppEArray,i);
					tps.lf_loadedok = 0;
					continue;
				}
				resEditSetWorkingCopy(pDictionary,keyString, subStruct);
			}

			resApplyInheritanceToDictionary(dictHandle, pppEArray);
			loadedok = (resEditRunValidateOnDictionary(RESVALIDATE_POST_TEXT_READING, dictHandle, 0, pppEArray, &tps) == PARSERESULT_SUCCESS);
			tps.lf_loadedok &= loadedok;
		}

		if (!(flags & PARSER_INTERNALLOAD))
		{
			loadedok = (FixupStructLeafFirst(pti,structptr,FIXUPTYPE_POST_ALL_TEXT_READING_AND_INHERITANCE_DURING_LOADFILES, NULL) == PARSERESULT_SUCCESS);
			tps.lf_loadedok &= loadedok;
		}
	}
	PERFINFO_AUTO_STOP();// fixup.

	// don't write out persist file if we had an error during load
	if (!tps.lf_loadedonefile) {
		if (persistfile && !(flags & PARSER_OPTIONALFLAG)) 
		{
			if (isProductionMode())
			{
				FatalErrorf("ParserLoadFiles: couldn't find any files while creating %s", persistfile);
			}
			else
			{
				Errorf("ParserLoadFiles: couldn't find any files while creating %s", persistfile);
			}
		} 
		else 
		{
			verbose_printf("ParserLoadFiles: couldn't find any files\n");
		}
	}

	PERFINFO_AUTO_STOP();// load from text.

	if (persistfile && !(flags & PARSER_DONTREBUILD))
	{
		PERFINFO_AUTO_START("persistfile", 1);
		if (!tps.lf_loadedok)
			verbose_printf("ParserLoadFiles: error during parsing, not creating binary\n");
		if ((tps.lf_loadedonefile || flags & PARSER_OPTIONALFLAG) && (tps.lf_loadedok || flags & PARSER_ALLOW_BINS_WITH_ERRORS_AND_RELOADING) || tps.lf_forcebincreate) 
		{
			char layoutFileName[CRYPTIC_MAX_PATH];

			layoutFileName[0] = 0;
	
			mkdirtree(fileLocateWriteBin(persistfilepath, buf));
			
			if (!tps.lf_loadedok)
			{
				LOADFILES_PRINTF(LFPRINT_BINFILES, "Going to create bin file %s despite errors, due to ForceCreate or some similar flag\n",
					persistfilepath);

				ParserBinHadErrors(&tps);
				Errorf("Note: Due to ForceBinCreateFile or PARSER_ALLOW_BINS_WITH_ERRORS_AND_RELOADING, creating bin file %s despite one or more parse errors",
					persistfilepath);
			}
			else
			{
				LOADFILES_PRINTF(LFPRINT_BINFILES, "Creating bin file %s\n",
					persistfilepath);
			}

			if (gbWriteLayoutFiles)
			{
				char nameSpace[MAX_PATH], baseName[MAX_PATH];
				if(resExtractNameSpace(persistfile, nameSpace, baseName))
				{
					char nsLayoutFileName[MAX_PATH];
					sprintf(nsLayoutFileName, "%s:/BinLayoutFiles/%s_layout.txt", nameSpace, baseName);
					fileLocateWrite(nsLayoutFileName, layoutFileName);
				}
				else
					sprintf(layoutFileName, "%s/BinLayoutFiles/%s_layout.txt", fileLocalDataDir(), persistfile);
			}

			if (tps.preExistingDependencies)
			{
				DependencyEntry **ppList = tps.preExistingDependencies;

				FOR_EACH_IN_EARRAY(ppList, DependencyEntry, pDependency)
				{
					DependencyListInsert(&tps.deplist,
						pDependency->type, pDependency->name, ParserBinGetValueForDep(pDependency->type, pDependency->name));
				}
				FOR_EACH_END;
			}

			ParserWriteBinaryFile(persistfilepath, layoutFileName, pti, structptr, &tps.parselist, (flags & PARSER_ALLOW_BINS_WITH_ERRORS_AND_RELOADING) ? &tps.FilesWithErrors : NULL, &tps.deplist, NULL, 0, excludeFlags, NULL, (flags & PARSER_HUGEBUFFERFORBINARYFILE) ? PARSERWRITE_HUGEBUFFER : 0,
				(spFolderForCachedErrorTextFiles && siErrorCountForCachedErrors == 0) ? BINARYHEADERFLAG_NO_DATA_ERRORS : 0 
				);
			
			if (metadatafile)
			{
				ResourceDictionaryInfo *pInfoHolder = resEditGetPendingInfo(dictHandle);
				
				//FileList binDep = NULL;				
				//FileListInsert(&binDep, persistfilepath, 0);

				if (gbWriteLayoutFiles)
				{
					char nameSpace[MAX_PATH], baseName[MAX_PATH];
					if(resExtractNameSpace(persistfile, nameSpace, baseName))
					{
						char nsLayoutFileName[MAX_PATH];
						sprintf(nsLayoutFileName, "%s:/BinLayoutFiles/%s_metadata_layout.txt", nameSpace, baseName);
						fileLocateWrite(nsLayoutFileName, layoutFileName);
					}
					else
						sprintf(layoutFileName, "%s/BinLayoutFiles/%s_metadata_layout.txt", fileLocalDataDir(), persistfile);
				}
				else
				{
					layoutFileName[0] = 0;
				}

				ParserWriteBinaryFile(metadatafilepath, layoutFileName, parse_ResourceDictionaryInfo, pInfoHolder, NULL, NULL, NULL, NULL, 0, 0, NULL, 0, 0);

				if (gbWriteTextMetadata)
				{
					char textMetaData[CRYPTIC_MAX_PATH];
					sprintf(textMetaData, "%s.txt", metadatafilepath);
					ParserWriteTextFile(textMetaData, parse_ResourceDictionaryInfo, pInfoHolder, 0, 0);
				}
				//FileListDestroy(&binDep);
			}

			if (spFolderForCachedErrorTextFiles && persistfilepath[0])
			{
				FinalizeErrorTextFileCaching(persistfilepath);
			}
		}
		PERFINFO_AUTO_STOP();// persistfile.
	}

	PERFINFO_AUTO_START("cleanup", 1);
	{
		FileListClear(&tps.parselist);
		DependencyListClear(&tps.deplist);
	}

	LOADFILES_PRINTF(LFPRINT_NORMAL, "Done with ParserLoadFiles\n\n\n");

	{
		// Need to access these variables *before* TextParserState_Destroy()!
		bool ret = (tps.lf_loadedonefile || flags & PARSER_OPTIONALFLAG) && tps.lf_loadedok;
		TextParserState_Destroy(&tps);
		if (binMutex) releaseThreadAgnosticMutex(binMutex);

		PERFINFO_AUTO_STOP();// cleanup.
		PERFINFO_AUTO_STOP();// FUNC.

		return ret;
	}
}

bool ParserCheckLoadFiles(const char* dirs, const char* filemask, const char* persistfile, int flags, ParseTable pti[])
{
	bool ret;
	void *structptr = StructCreateVoid(pti);
	ret = ParserLoadFilesInternal(dirs, filemask, persistfile, NULL, flags, pti, structptr, NULL, true, NULL);
	StructDestroyVoid(pti, structptr);
	return ret;
}

bool ParserLoadFiles(const char* dirs, const char* filemask, const char* persistfile, int flags, 
	ParseTable pti[], void* structptr)
{
	TextParserResult eResult = ParserLoadFilesInternal(dirs, filemask, persistfile, NULL, flags, pti, structptr, NULL, false, NULL) ? PARSERESULT_SUCCESS : PARSERESULT_ERROR;
	SET_MIN_RESULT(eResult, FixupStructLeafFirst(pti, structptr, FIXUPTYPE_POST_BINNING_DURING_LOADFILES, NULL));
	FixupStructLeafFirst(pti, structptr, FIXUPTYPE_POST_LOAD_DURING_LOADFILES_FINAL_LOCATION, NULL);
	return eResult == PARSERESULT_SUCCESS;
}


#ifndef EXTERNAL_TEST

typedef struct PLFSHeader {
	U32 crc;
	char *pFixupString;
	void *pData;
	ResourceDictionaryInfo *pMetaData;
} PLFSHeader;

void MakeFileSpecFromDirFilename(const char *oldDirs, const char *oldFilemask, char ***pppFileSpecs)
{
	char* dirs = NULL, *filemask = NULL, *last, *temp;
	char** lf_filemasks = NULL;
	char** lf_dirs = NULL;
	int i, j;

	estrStackCreate(&dirs);
	estrCopy2(&dirs, oldDirs);
	estrStackCreate(&filemask);
	estrCopy2(&filemask, oldFilemask);

	if (dirs && dirs[0])
	{
		temp = strtok_quoted_r(dirs, ";", ";", &last);
#if !_PS3
		if (temp && (temp[0] == '/' || temp[0] == '\\')) temp++;
#endif
		eaPush(&lf_dirs, temp);

		while (temp = strtok_quoted_r(NULL, ";", ";", &last))
		{
#if !_PS3
			if (temp[0] == '/' || temp[0] == '\\') temp++;
#endif
			eaPush(&lf_dirs, temp);
		}
	}

	if (filemask && filemask[0])
	{
		i = 0;

		temp = strtok_quoted_r(filemask, ";", ";", &last);
#if !_PS3
		if (temp && (temp[0] == '/' || temp[0] == '\\')) temp++;
#endif
		eaPush(&lf_filemasks, temp);

		while (temp = strtok_quoted_r(NULL, ";", ";", &last))
		{
#if !_PS3
			if (temp[0] == '/' || temp[0] == '\\') temp++;
#endif
			eaPush(&lf_filemasks, temp);
		}
	}

	for (i = 0; i < eaSize(&lf_dirs); i++)
	{
		for (j = 0; j < eaSize(&lf_filemasks); j++)
		{
			char *newString;
			if (lf_filemasks[j][0] == '.')
			{
				size_t size = strlen(lf_dirs[i]) + strlen(lf_filemasks[j]) + 1 + 1;
				newString = calloc(size, 1);
				snprintf_s(newString, size, "%s*%s", lf_dirs[i], lf_filemasks[j]);
			}
			else
			{
				size_t size = strlen(lf_dirs[i]) + strlen(lf_filemasks[j]) + 1;
				newString = calloc(size, 1);
				snprintf_s(newString, size, "%s%s", lf_dirs[i], lf_filemasks[j]);
			}
			eaPush(pppFileSpecs, newString);
		}
	}
	eaDestroy(&lf_dirs);
	eaDestroy(&lf_filemasks);
	estrDestroy(&dirs);
	estrDestroy(&filemask);

}

void MakeSharedMemoryName(const char *pchBinFilename, char **ppOutEString)
{
	char *ext;
	estrDestroy(ppOutEString);

	estrPrintf(ppOutEString, "SM_%s", pchBinFilename); // If any memory is allocated from this line, it was leaked!

	ext = strchr(*ppOutEString, '.');
	if (ext)
	{
		estrSetSize(ppOutEString, ext - *ppOutEString);
	}
}

// Attempts to acquire the data from shared memory, and if not, then it loads it
// This does not work if you want to do any processing on the data after it is loaded (i.e. set backpointers, etc)
bool ParserLoadFilesShared_dbg(const char* sharedMemoryName, const char* dirs, const char* filemask, const char* persistfile, int flags, ParseTable pti[], void* structptr MEM_DBG_PARMS)
{
	SharedMemoryHandle *shared_memory=NULL;
	int forcerebuild = flags & PARSER_FORCEREBUILD;
	TextParserResult eResult;

	PERFINFO_AUTO_START("ParserLoadFilesSharedSize", 1);

	{
		char buf[64];
		memBudgetAddStructMapping(allocAddString_dbg(getFileNameNoExt(buf, sharedMemoryName), false, false, false MEM_DBG_PARMS_CALL), caller_fname);
	}

	if (!persistfile || forcerebuild) {
		eResult = ParserLoadFilesInternal(dirs, filemask, persistfile, NULL, flags, pti, structptr, NULL, false, NULL) ? PARSERESULT_SUCCESS : PARSERESULT_ERROR;
		SET_MIN_RESULT(eResult, FixupStructLeafFirst(pti, structptr, FIXUPTYPE_POST_BINNING_DURING_LOADFILES, NULL));

		FixupStructLeafFirst(pti, structptr, FIXUPTYPE_POST_LOAD_DURING_LOADFILES_FINAL_LOCATION, NULL);
		
		PERFINFO_AUTO_STOP();

		return eResult == PARSERESULT_SUCCESS;
	}

	if (ParserLoadFromShared(sharedMemoryName,flags,pti,structptr,&shared_memory, NULL, NULL,NULL, false))
	{
		eResult = PARSERESULT_SUCCESS;
	}
	else if (shared_memory)
	{
		// Load data and copy to shared memory
		eResult = ParserLoadFilesInternal(dirs, filemask, persistfile, NULL, flags, pti, structptr, NULL, false, NULL) ? PARSERESULT_SUCCESS : PARSERESULT_ERROR;
		
		SET_MIN_RESULT(eResult, FixupStructLeafFirst(pti, structptr, FIXUPTYPE_POST_BINNING_DURING_LOADFILES, NULL));

		ParserMoveToShared(shared_memory,flags,pti,structptr,NULL);

		FixupStructLeafFirst(pti, structptr, FIXUPTYPE_POST_LOAD_DURING_LOADFILES_FINAL_LOCATION, NULL);

		if (isDevelopmentMode() && sharedMemoryGetMode() == SMM_ENABLED)
		{
			CheckSharedMemory(pti, structptr);
		}

		sharedMemoryUnlock(shared_memory);
	}
	else
	{
		// An error occurred with the shared memory, just do what we would normally do
		eResult = ParserLoadFilesInternal(dirs, filemask, persistfile, NULL, flags, pti, structptr, NULL, false, NULL) ? PARSERESULT_SUCCESS : PARSERESULT_ERROR;

		SET_MIN_RESULT(eResult, FixupStructLeafFirst(pti, structptr, FIXUPTYPE_POST_BINNING_DURING_LOADFILES, NULL));

		FixupStructLeafFirst(pti, structptr, FIXUPTYPE_POST_LOAD_DURING_LOADFILES_FINAL_LOCATION, NULL);
	}

	PERFINFO_AUTO_STOP();

	return eResult == PARSERESULT_SUCCESS;
}


// Attempts to load the data from shared memory. Returns 1 on success, 0 on failure
// shared_memory is modified to point to the handle returned
// If you pass in fixupString, it will be modified to point to the fixup string, which can then be applied later
int ParserLoadFromShared(const char* sharedMemoryName, int flags, ParseTable pti[], void* structptr, SharedMemoryHandle **shared_memory, char **fixupString, const char *pDictName, ResourceDictionaryInfo *pMetadata, bool bLock)
{
	SM_AcquireResult ret;
	PLFSHeader header;
	int iSize = ParserGetTableSize(pti);

	assert(shared_memory);

	if(IsClient())
	{
		return 0;
	}

	if (!stringCacheSharingEnabled())
	{
		// If string cache isn't shared, not safe to enable shared memory
		// This will turn it off on the client
		*shared_memory = NULL;
		return 0;
	}

	ret = stringCacheSharedMemoryAcquire(shared_memory, sharedMemoryName, pDictName);

	if (ret==SMAR_DataAcquired) 
	{
		U32 crc;

		PERFINFO_AUTO_START_FUNC();

		header = *(PLFSHeader*)sharedMemoryGetDataPtr(*shared_memory);
		crc = ParseTableCRC(pti, NULL, TPICRCFLAG_CRC_SUBSTRUCT_SIZE);
		if (crc!=header.crc) {
			printf("WARNING: Detected data in shared memory (%s) from a different version.  Restarting the game/mapserver/tools should fix this.", sharedMemoryName);
			sharedMemorySetMode(SMM_DISABLED);
			*shared_memory = NULL;
			PERFINFO_AUTO_STOP();
			return 0;
		} else {
			void *pData = header.pData;
			char *pFixupString = header.pFixupString;
			// Data there already, just need to point to it!
			memcpy(structptr, (char*)pData, iSize);

			if (bLock)
				sharedMemoryLock(*shared_memory);

			if (pFixupString)
			{
				if (fixupString)
				{
					*fixupString = pFixupString;
				}
				else
				{				
					fixupSharedMemoryStruct(pti, structptr, &pFixupString);
				}
			}
			if (pMetadata && header.pMetaData)
			{
				// Shallow copy, so they point to the ones in shared memory instead of reallocing
				eaCopy(&pMetadata->ppInfos, &header.pMetaData->ppInfos);
			}
			PERFINFO_AUTO_STOP();
			return 1;
		}
	}
	else if (ret == SMAR_Error) 
	{
		*shared_memory = NULL;
	}
	return 0;
}

// Attempts to move the data to the shared memory handle that is passed in. We assume we have write access
int ParserMoveToShared(SharedMemoryHandle *shared_memory, int flags, ParseTable pti[], void* structptr, ResourceDictionaryInfo *pMetadata)
{
	void *pTemp = NULL, *pTempMetadata = NULL;
	PLFSHeader *pNewHeader;	
	size_t size;
	char *pFixupString = NULL;
	int iSize = ParserGetTableSize(pti);

	PERFINFO_AUTO_START_FUNC();

	PERFINFO_AUTO_START("1", 1);

	estrStackCreate(&pFixupString);

	// Copy to shared memory
	preparesharedmemoryforfixupStruct(pti, structptr, &pFixupString);
	
	size = StructGetMemoryUsage_inline(pti, structptr, false) + iSize + sizeof(PLFSHeader) + (pFixupString ? (estrLength(&pFixupString) + 1) : 0);
	if (pMetadata)
	{
		size+= ParserGetTableSize(parse_ResourceDictionaryInfo);
		size+= StructGetMemoryUsage_inline(parse_ResourceDictionaryInfo, pMetadata, false);

	}
	sharedMemorySetSize(shared_memory, size);
	pNewHeader = sharedMemoryAlloc(shared_memory, sizeof(PLFSHeader));
	pNewHeader->crc = ParseTableCRC(pti, NULL, TPICRCFLAG_CRC_SUBSTRUCT_SIZE);
	pTemp = StructCompress(pti, structptr, NULL, sharedMemoryAlloc, shared_memory);
	pNewHeader->pData = pTemp;
	
	if (!(flags & PARSER_DONTFREE))
	{			
		StructDeInitVoid(pti, structptr);
	}
	memcpy(structptr, pTemp, iSize);

	PERFINFO_AUTO_STOP_START("2", 1);

	if (pFixupString)
	{
		pNewHeader->pFixupString = sharedMemoryAlloc(shared_memory, estrLength(&pFixupString) + 1);
		memcpy(pNewHeader->pFixupString, pFixupString, estrLength(&pFixupString) + 1);
	}
	else
	{
		pNewHeader->pFixupString = NULL;
	}

	estrDestroy(&pFixupString);

	if (pMetadata)
	{
		pTempMetadata = StructCompress(parse_ResourceDictionaryInfo, pMetadata, NULL, sharedMemoryAlloc, shared_memory);
		pNewHeader->pMetaData = pTempMetadata;
	}
	else
	{
		pNewHeader->pMetaData = NULL;
	}

	PERFINFO_AUTO_STOP();

	PERFINFO_AUTO_STOP();

	return 1;
}

int ParserReadTextWithCommentEx(const char *str,ParseTable *tpi,void *struct_mem, int iFlags, const char *pComment, const char *pFunction, int iLine)
{
	TokenizerHandle tok;
	TextParserResult parseResult = PARSERESULT_SUCCESS;
	char *pInternalComment = NULL;

	if (!str || !str[0])
		return 0;

	if (!pComment)
	{	
		estrStackCreate(&pInternalComment);
		estrPrintf(&pInternalComment, "(Struct type %s, func %s, line %d)",
			ParserGetTableName(tpi), pFunction, iLine);
		pComment = pInternalComment;
	}

	tok = TokenizerCreateString(str, pComment, iFlags);
	if (!tok)
	{
		estrDestroy(&pInternalComment);
		return 0;
	}
	TokenizerSetFlags(tok, iFlags);
	ParserReadTokenizer(tok, tpi, struct_mem, false, &parseResult);
	TokenizerDestroy(tok);
	estrDestroy(&pInternalComment);
	return (parseResult == PARSERESULT_SUCCESS);
}

int ParserReadTextForFile(const char *str,const char *filename, ParseTable *tpi,void *struct_mem, int iFlags)
{
	TokenizerHandle tok;
	TextParserResult parseResult = PARSERESULT_SUCCESS;

	if (!str)
		return 0;
	tok = TokenizerCreateString(str, filename, iFlags);
	if (!tok)
		return 0;
	ParserReadTokenizer(tok, tpi, struct_mem, false, &parseResult);
	TokenizerDestroy(tok);
	return (parseResult == PARSERESULT_SUCCESS);
}

int ParserWriteText_dbg(char **estr,ParseTable *tpi, void *struct_mem, WriteTextFlags iWriteTextFlags, StructTypeField iOptionFlagsToMatch, StructTypeField iOptionFlagsToExclude, const char *caller_fname, int line)
{
	FILE		*fpBuff;
	int			ok;

	if (!estr)
		return 0;

	PERFINFO_AUTO_START_FUNC();

	fpBuff = fileOpenEString_dbg(estr, caller_fname, line);
	ok = InnerWriteTextFile(fpBuff, tpi, struct_mem, 0, 0, iWriteTextFlags, iOptionFlagsToMatch, iOptionFlagsToExclude);
	fileClose(fpBuff);

	PERFINFO_AUTO_STOP();

	return ok;
}

int ParserReadTextEscapedWithCommentOrFileAndLineEx(const char **str, ParseTable *tpi, void *struct_mem, int iFlags, const char *pComment, const char *dataFileName, int iDataFileLineNum, const char *pFunction, int iLine)
{
	const char *start;
	char *end;
	char *tempbuffer;
	int len;
	TokenizerHandle tok;
	TextParserResult parseResult = PARSERESULT_SUCCESS;
	char *pInternalComment = NULL;

	if (!str || !*str)
		return 0;

	PERFINFO_AUTO_START_FUNC();

	start = *str;
	start = strstr(start,"<&");
	if (!start)
	{
		// No start found
		PERFINFO_AUTO_STOP();
		return 0;
	}
	start++; start++;
	end = (char*) strstr(start,"&>");
	if (!end)
	{
		// Incomplete
		PERFINFO_AUTO_STOP();
		return 0;
	}
	*end = '\0';
	len = end - start;
	tempbuffer = malloc(len + 1);
	TokenizerUnescape(tempbuffer,start);
	*end = '&';
	end++;
	end++;

	// tempbuffer contains the unescaped string, and end is pointing after the struct
	if (dataFileName)
	{
		tok = TokenizerCreateString(tempbuffer, dataFileName, iFlags);
		if (!tok)
		{
			estrDestroy(&pInternalComment);
			PERFINFO_AUTO_STOP();
			return 0;
		}

		TokenizerSetLineNumForErrorReporting(tok, iDataFileLineNum);

	}
	else
	{
		if (!pComment)
		{	
			estrStackCreate(&pInternalComment);
			estrPrintf(&pInternalComment, "(Struct type %s, func %s, line %d)",
				ParserGetTableName(tpi), pFunction, iLine);
			pComment = pInternalComment;
		}

		tok = TokenizerCreateString(tempbuffer, pComment, iFlags);
		if (!tok)
		{
			estrDestroy(&pInternalComment);
			PERFINFO_AUTO_STOP();
			return 0;
		}
	}

	ParserReadTokenizer(tok, tpi, struct_mem, false, &parseResult);
	TokenizerDestroy(tok);
	SAFE_FREE(tempbuffer);
	estrDestroy(&pInternalComment);

	*str = end;

	PERFINFO_AUTO_STOP();

	return (parseResult == PARSERESULT_SUCCESS);
}



int ParserWriteTextEscaped(char **estr, ParseTable *tpi, const void *struct_mem, WriteTextFlags iWriteFlags, StructTypeField iOptionFlagsToMatch, StructTypeField iOptionFlagsToExclude)
{
	char *pTempEString = NULL;
	FILE *pEStringFile;
	int			ok;
	char *start;
	int	newlen;
	unsigned int estrlen;

	if (!estr)
	{
		return 0;
	}

	PERFINFO_AUTO_START_FUNC();

	estrStackCreate(&pTempEString);
	pEStringFile = fileOpenEString(&pTempEString);
	ok = InnerWriteTextFile(pEStringFile, tpi, (void*)struct_mem, 0, 0, iWriteFlags, iOptionFlagsToMatch, iOptionFlagsToExclude);

	if (!ok)
	{
		PERFINFO_AUTO_STOP();
		return ok;
	}

	estrlen = estrLength(estr);

	estrReserveCapacity(estr,estrlen + estrLength(&pTempEString) * 2 + 4); //This is the theoretical max space usage
	start = (*estr)+estrlen;

	newlen = TokenizerEscape(start,pTempEString);

	estrForceSize(estr,estrlen + newlen);

	fileClose(pEStringFile);
	estrDestroy(&pTempEString);

	PERFINFO_AUTO_STOP();

	return ok;
}

StructDiffOp *StructMakeDiffOp(ObjectPath* path, void *structptr, StructDiffOperator sdop)
{
	StructDiffOp *op = StructCreate(parse_StructDiffOp); //(StructDiffOp*)calloc(sizeof(StructDiffOp), 1);
	op->pField = path;
	op->op = sdop;
	return op;
}

StructDiffOp *StructMakeAndAppendDiffOp(StructDiff *diff, ObjectPath* path, void *structptr, StructDiffOperator sdop)
{
	StructDiffOp *op = StructMakeDiffOp(path, structptr, sdop);
	eaPush(&diff->ppOps, op);
	return op;
}

bool StructDiffIsValid(StructDiff *diff)
{
	StructDiffOp *op = eaTail(&diff->ppOps);
	if (op && op->op != STRUCTDIFF_INVALID) return true;
	else return false;
}

void StructDestroyDiffOp(StructDiffOp **op)
{
	if ((*op)->pOperand) MultiValDestroy((MultiVal*)(*op)->pOperand);
	(*op)->pField = NULL;
	StructDestroy(parse_StructDiffOp, *op); //free (*op);
	*op = NULL;
}

void StructWriteTextDiffFromBDiff(char **estr, StructDiff *diff)
{
	int i;
	if (!*estr) estrCreate(estr);
	
	for (i = 0; i < eaSize(&diff->ppOps); i++)
	{
		char *operation;
		StructDiffOp *op = diff->ppOps[i];

		switch (op->op) {
			case STRUCTDIFF_DESTROY: operation = "destroy"; break;
			case STRUCTDIFF_CREATE: operation = "create"; break;
			case STRUCTDIFF_SET: operation = "set"; break;
			default: operation = "invalid operation"; break;
		}
		if (op->op == STRUCTDIFF_SET)
		{
			estrConcatf(estr, "%s %s = \"", operation, op->pField->key->pathString);
			if (MULTI_GET_TYPE(((MultiVal*)op->pOperand)->type))
			{
				MultiValToEString((MultiVal*)op->pOperand, estr);
			}
			estrConcatf(estr, "\"\n");
		}
		else if (op->op == STRUCTDIFF_INVALID)
		{
			char *errstr = NULL;
			estrPrintf(&errstr, "StructDiff is invalid. reason: %s\n", MultiValGetString((MultiVal*)op->pOperand, false));
			//estrConcatf(estr, "StructDiff is invalid. reason: %s\n", MultiValGetString((MultiVal*)op->pOperand, false));
			assertmsg(false, errstr);
			estrDestroy(&errstr);
		}
		else
		{
			estrConcatf(estr, "%s %s\n", operation, op->pField->key->pathString);
		}
	}
}

StructDiff *StructMakeDiff_dbg(ParseTable tpi[], void *oldp, void *newp, StructTypeField iOptionFlagsToMatch, StructTypeField iOptionFlagsToExclude, bool invertExcludeFlags, bool forceAbsoluteDiff, const char *caller_fname, int line)
{
	StructDiff *diff = StructCreate(parse_StructDiff); //(StructDiff*)calloc(sizeof(StructDiff), 1);
	eaCreateInternal(&diff->ppOps, caller_fname, line);
	StructMakeDiffInternal(diff, tpi, oldp, newp, NULL, iOptionFlagsToMatch, iOptionFlagsToExclude, invertExcludeFlags, forceAbsoluteDiff, caller_fname, line);
	
	return diff;
}

void StructDestroyDiff(StructDiff **diff)
{
	StructDiff *dif = *diff;
	int i = eaSize(&dif->ppOps) - 1;
	while (eaSize(&dif->ppOps) > 0) {
		StructDiffOp *op = eaPop(&dif->ppOps);
		ObjectPathDestroy(op->pField);
		op->pField = NULL;
		StructDestroyDiffOp(&op);
		i--;
	}
	StructDestroy(parse_StructDiff, dif); //free(dif);
	*diff = NULL;
}

//After calling a callback to add a diffop, errors will propagate back via the last operation being STRUCTDIFF_INVALID.
void StructMakeDiffInternal(StructDiff *diff, ParseTable parenttpi[], void* oldp, void* newp, ObjectPath *parentPath, StructTypeField iOptionFlagsToMatch, StructTypeField iOptionFlagsToExclude, bool invertExcludeFlags, bool forceAbsoluteDiff, const char *caller_fname, int line)
{	
	int i;
	bool ok;

	assert(parenttpi);

	if (parentPath)
	{
		ParseTable *tpi;
		int col;
		ObjectPathSegment *seg = ObjectPathTail(parentPath);
		bool descend = seg->descend;
		seg->descend = false;
		ParserResolvePathComp(parentPath, NULL, &tpi, &col, NULL, NULL, NULL, iOptionFlagsToMatch);
		seg->descend = descend;
		if (!!(tpi[col].type & TOK_INDIRECT) && (oldp && !newp) || (oldp && forceAbsoluteDiff))
		{
			StructDiffOp *op = StructMakeAndAppendDiffOp(diff, parentPath, oldp, STRUCTDIFF_DESTROY);
			if (!StructDiffIsValid(diff)) return;
		}
		else if ( !!(tpi[col].type & TOK_INDIRECT) && (newp && !oldp) || (newp && forceAbsoluteDiff))
		{
			StructDiffOp *op = StructMakeAndAppendDiffOp(diff, parentPath, newp, STRUCTDIFF_CREATE);
				if (!StructDiffIsValid(diff)) return;
		}
	}
	if (forceAbsoluteDiff)
		oldp = NULL; // this forces us to write every field below

	if (newp)
	{	
		ParseTable *tpi;
		int col, ind;
		void *nsub = NULL;
		void *osub = NULL;
		if (parentPath)
		{
			ObjectPathSegment *seg = ObjectPathTail(parentPath);

			ok = ParserResolvePathSegment(seg, parenttpi, newp, &tpi, &nsub, &col, &ind, NULL, iOptionFlagsToMatch);
		}
		else
		{
			ok = !!(tpi = parenttpi);
			nsub = newp;
			osub = oldp;
		}
		if (!ok) return; //This should report errors.

		FORALL_PARSETABLE(tpi, i)
		{
			StructTypeField fieldFlagsToMatch = iOptionFlagsToMatch;
			StructTypeField fieldFlagsToExclude = iOptionFlagsToExclude;
			bool fieldInvertExcludeFlags = invertExcludeFlags;

			StructTypeField type = TOK_GET_TYPE(tpi[i].type);
			if (type == TOK_IGNORE ||
				type == TOK_START ||
				type == TOK_END)
				continue;
			if (tpi[i].type & TOK_REDUNDANTNAME || tpi[i].type & TOK_NO_WRITE) continue;

			if (!FlagsMatchAll(tpi[i].type,iOptionFlagsToMatch))
			{
				continue;
			}

			if (invertExcludeFlags)
			{
				if (FlagsMatchAll(tpi[i].type,iOptionFlagsToMatch | iOptionFlagsToExclude))
				{
					// This would have been excluded, so include EVERYTHING below
					fieldInvertExcludeFlags = 0;
					fieldFlagsToExclude = 0;
				}				
				else if (!TOK_HAS_SUBTABLE(tpi[i].type))
				{
					// Leaf nodes would have been explicitly excluded, but there could be data farther down for structures
					continue;					
				}
			}
			else if (!FlagsMatchNone(tpi[i].type,iOptionFlagsToExclude))
			{
				continue;
			}

			if ((compare_autogen(tpi, i, osub, nsub, 0,COMPAREFLAG_NULLISDEFAULT | (fieldInvertExcludeFlags ? COMPAREFLAG_EXCLUDE_FLAG_WORKS_AS_INCLUDE_FLAG_UNTIL_TRAVERSE_INTO_STRUCT : 0), fieldFlagsToMatch, fieldFlagsToExclude)))
			{
				ObjectPath *path = parentPath ? ObjectPathCopyAndAppend(parentPath, tpi[i].name, i, -1, NULL) : ObjectPathCreate(tpi, tpi[i].name, i, -1, NULL);
				StructDiffOp *op = StructMakeAndAppendDiffOp(diff, path, nsub, STRUCTDIFF_SET);
				if (!StructDiffIsValid(diff)) break;

				writebdiff_autogen(diff, tpi, i, osub, nsub, -1, path, fieldFlagsToMatch, fieldFlagsToExclude, fieldInvertExcludeFlags, caller_fname, line);
				
				if (!StructDiffIsValid(diff)) break;
			}
		}
	}
}

// This is NOT threadsafe
bool g_writeSizeReport;
char *g_sizeReportString;

void StructWriteTextDiffInternal(char **estr, ParseTable tpi[], void *oldp, void *newp, char *prefix, 
								 StructTypeField iOptionFlagsToMatch, StructTypeField iOptionFlagsToExclude, TextDiffFlags eFlags, 
								 bool printCreate, bool forceAbsoluteDiff, const char *caller_fname, int line)
{
	int i;
	PERFINFO_AUTO_START_FUNC();
	if (!prefix) 
	{
		prefix = "";
	}

	if ((oldp && !newp && prefix[0] && printCreate) || (oldp && forceAbsoluteDiff))
	{
		estrConcatf(estr,"destroy %s\n",prefix);
	}
	else if ((newp && !oldp && prefix[0] && printCreate) || (newp && forceAbsoluteDiff))
	{
		if (prefix[strlen(prefix)-3] == '"')
			assert (false);
		estrConcatf(estr,"create %s\n",prefix);
	}
	if (forceAbsoluteDiff)
		oldp = NULL; // this forces us to write every field below

	if (g_writeSizeReport && newp)
	{
		size_t size = ParserGetTableSize(tpi) + StructGetMemoryUsage(tpi, newp, true);
		estrConcatf(&g_sizeReportString, "STRUCT %s SIZE %d\n", prefix, size);
	}

	if (newp)
		FORALL_PARSETABLE(tpi, i)
	{
		StructTypeField fieldFlagsToMatch = iOptionFlagsToMatch;
		StructTypeField fieldFlagsToExclude = iOptionFlagsToExclude;
		bool fieldInvertExcludeFlags = !!(eFlags & TEXTIDFFFLAG_EXCLUDE_FLAG_WORKS_AS_INCLUDE_FLAG_UNTIL_TRAVERSE_INTO_STRUCT);

		StructTypeField type = TOK_GET_TYPE(tpi[i].type);
		if (type == TOK_IGNORE ||
			type == TOK_START ||
			type == TOK_END)
			continue;
		if (tpi[i].type & TOK_REDUNDANTNAME || tpi[i].type & TOK_NO_WRITE) continue;
		if (!tpi[i].name || !tpi[i].name[0]) continue; // unnamed fields shouldn't be parsed or written

		if (!FlagsMatchAll(tpi[i].type,iOptionFlagsToMatch))
		{
			continue;
		}

		if (eFlags & TEXTDIFFFLAG_SKIP_POLYMORPHIC_CHILDREN && (type == TOK_POLYMORPH_X))
		{
			continue;
		}

		if (eFlags & TEXTIDFFFLAG_EXCLUDE_FLAG_WORKS_AS_INCLUDE_FLAG_UNTIL_TRAVERSE_INTO_STRUCT)
		{
			if (FlagsMatchAll(tpi[i].type,iOptionFlagsToMatch | iOptionFlagsToExclude))
			{
				// This would have been excluded, so include EVERYTHING below
				fieldInvertExcludeFlags = 0;
				fieldFlagsToExclude = 0;
			}				
			else if (!TOK_HAS_SUBTABLE(tpi[i].type))
			{
				// Leaf nodes would have been explicitly excluded, but there could be data farther down for structures
				continue;					
			}
		}
		else if (!FlagsMatchNone(tpi[i].type,iOptionFlagsToExclude))
		{
			continue;
		}

		if ((compare_autogen(tpi, i, oldp, newp, 0,COMPAREFLAG_NULLISDEFAULT | (fieldInvertExcludeFlags ? COMPAREFLAG_EXCLUDE_FLAG_WORKS_AS_INCLUDE_FLAG_UNTIL_TRAVERSE_INTO_STRUCT : 0), fieldFlagsToMatch, fieldFlagsToExclude)))
		{
			char *newpath = NULL;
			TextDiffFlags iFlagsToRecurseWith = eFlags;

			estrStackCreateSize(&newpath, MIN_STACK_ESTR);
			estrPrintf(&newpath,"%s.%s",prefix,tpi[i].name);

			if (fieldInvertExcludeFlags)
			{
				iFlagsToRecurseWith |= TEXTIDFFFLAG_EXCLUDE_FLAG_WORKS_AS_INCLUDE_FLAG_UNTIL_TRAVERSE_INTO_STRUCT;
			}
			else
			{
				iFlagsToRecurseWith &= ~TEXTIDFFFLAG_EXCLUDE_FLAG_WORKS_AS_INCLUDE_FLAG_UNTIL_TRAVERSE_INTO_STRUCT;
			}

			writehdiff_autogen(estr, tpi, i, oldp, newp, 0, newpath, fieldFlagsToMatch, fieldFlagsToExclude, iFlagsToRecurseWith, caller_fname, line);
			estrDestroy(&newpath);
		}
	}
	PERFINFO_AUTO_STOP();

}

void ParserWriteTextDiff_dbg(char **estr, ParseTable tpi[], int column, int oldindex, int newindex,
							 void *oldp, void *newp, char *prefix,
							 StructTypeField iOptionFlagsToMatch, StructTypeField iOptionFlagsToExclude,
							 TextDiffFlags eFlags MEM_DBG_PARMS)
{
	PERFINFO_AUTO_START_FUNC();
	if(oldindex != newindex)
	{
		ParseTable* actualTable;
		size_t actualTableSize;
		void* actualSrcObject;
		void* actualDstObject;

		// this means that in an indexed earray, the array was different but not actually
		// locked, so we can just diff the actual struct
		devassert(oldindex >= 0);
		devassert(newindex >= 0);

		actualSrcObject = StructGetSubtable(tpi, column, oldp, oldindex, &actualTable, &actualTableSize);
		actualDstObject= StructGetSubtable(tpi, column, newp, newindex, &actualTable, &actualTableSize);

		StructWriteTextDiff_dbg(estr,actualTable,actualSrcObject,actualDstObject,prefix,iOptionFlagsToMatch,iOptionFlagsToExclude,eFlags & ~TEXTIDFFFLAG_EXCLUDE_FLAG_WORKS_AS_INCLUDE_FLAG_UNTIL_TRAVERSE_INTO_STRUCT MEM_DBG_PARMS_CALL);
	}
	else if (newindex >= 0)
	{	
		FieldWriteTextDiff_dbg(estr,tpi,column,oldp,newp,newindex,prefix,iOptionFlagsToMatch,iOptionFlagsToExclude,eFlags & ~TEXTIDFFFLAG_EXCLUDE_FLAG_WORKS_AS_INCLUDE_FLAG_UNTIL_TRAVERSE_INTO_STRUCT MEM_DBG_PARMS_CALL);
	}
	else
	{
		TokenWriteTextDiff_dbg(estr,tpi,column,oldp,newp,prefix,iOptionFlagsToMatch,iOptionFlagsToExclude,eFlags & ~TEXTIDFFFLAG_EXCLUDE_FLAG_WORKS_AS_INCLUDE_FLAG_UNTIL_TRAVERSE_INTO_STRUCT MEM_DBG_PARMS_CALL);
	}
	PERFINFO_AUTO_STOP();
}

void ParserTextDiffWithNull_dbg(char **estr, ParseTable tpi[], int column, int index, void *newp, char *prefix, int iPrefixLen, StructTypeField iOptionFlagsToMatch, StructTypeField iOptionFlagsToExclude, TextDiffFlags eFlags MEM_DBG_PARMS)
{
	if (!prefix) 
	{
		prefix = "";
		iPrefixLen = 0;
	}

	if (index < 0)
	{
		textdiffwithnull_autogen(estr,tpi, column, newp,-1, prefix, iPrefixLen, iOptionFlagsToMatch,  iOptionFlagsToExclude, eFlags, caller_fname, line);
	}
	else
	{
		nonarray_textdiffwithnull(estr,tpi, column, newp,index, prefix, iPrefixLen, iOptionFlagsToMatch,  iOptionFlagsToExclude, eFlags, caller_fname, line);
	}
}


__forceinline void StructWriteTextDiff_dbg(char **estr, ParseTable tpi[], void *oldp, void *newp, char *prefix, 
						 StructTypeField iOptionFlagsToMatch, StructTypeField iOptionFlagsToExclude, TextDiffFlags eFlags, const char *caller_fname, int line)
{
	StructWriteTextDiffInternal(estr, tpi, oldp, newp,  prefix, iOptionFlagsToMatch, iOptionFlagsToExclude, eFlags, 1, false, caller_fname, line);
}


__forceinline void FieldWriteTextDiff_dbg(char **estr, ParseTable tpi[], int column, void *old_struct, void *new_struct, int index, char * prefix, StructTypeField iOptionFlagsToMatch, StructTypeField iOptionFlagsToExclude, TextDiffFlags eFlags, 
	const char *caller_fname, int line)
{
	PERFINFO_AUTO_START_FUNC();

	if (!prefix) 
	{
		prefix = "";
	}

	if (TYPE_INFO(tpi[column].type).compare(tpi, column, old_struct, new_struct, index,COMPAREFLAG_NULLISDEFAULT | ((eFlags & TEXTIDFFFLAG_EXCLUDE_FLAG_WORKS_AS_INCLUDE_FLAG_UNTIL_TRAVERSE_INTO_STRUCT) ? COMPAREFLAG_EXCLUDE_FLAG_WORKS_AS_INCLUDE_FLAG_UNTIL_TRAVERSE_INTO_STRUCT : 0), iOptionFlagsToMatch, iOptionFlagsToExclude))
	{
		if (TYPE_INFO(tpi[column].type).writehdiff)
			TYPE_INFO(tpi[column].type).writehdiff(estr,tpi, column, old_struct,new_struct,index, prefix, iOptionFlagsToMatch,  iOptionFlagsToExclude, eFlags, caller_fname, line);
	}

	PERFINFO_AUTO_STOP();
}

__forceinline void TokenWriteTextDiff_dbg(char **estr, ParseTable tpi[], int column, void *old_struct, void *new_struct, char * prefix, StructTypeField iOptionFlagsToMatch, StructTypeField iOptionFlagsToExclude, WriteTextFlags eFlags, const char *caller_fname, int line)
{
	PERFINFO_AUTO_START_FUNC();

	if (!prefix) 
	{
		prefix = "";
	}

	if (compare_autogen(tpi, column, old_struct, new_struct, -1,COMPAREFLAG_NULLISDEFAULT | ((eFlags & TEXTIDFFFLAG_EXCLUDE_FLAG_WORKS_AS_INCLUDE_FLAG_UNTIL_TRAVERSE_INTO_STRUCT) ? COMPAREFLAG_EXCLUDE_FLAG_WORKS_AS_INCLUDE_FLAG_UNTIL_TRAVERSE_INTO_STRUCT : 0), iOptionFlagsToMatch, iOptionFlagsToExclude))
	{
		writehdiff_autogen(estr,tpi, column, old_struct,new_struct,-1, prefix, iOptionFlagsToMatch,  iOptionFlagsToExclude, eFlags, caller_fname, line);
	}

	PERFINFO_AUTO_STOP();
}

const char *StructWriteMemoryReport(ParseTable tpi[], void *newp)
{
	char *tempStr = NULL;
	estrStackCreate(&tempStr);

	g_writeSizeReport = 1;
	estrClear(&g_sizeReportString);

	StructWriteTextDiff(&tempStr, tpi, NULL, newp, NULL, 0, 0, 0);

	g_writeSizeReport = 0;
	
	estrDestroy(&tempStr);

	return g_sizeReportString;
}

#endif // EXTERNAL_TEST

#include "autogen/textparser_c_ast.h"

// *********************************************************************************
//  textparser internal tests
// *********************************************************************************

AUTO_STRUCT;
typedef struct StructWithBits
{
	int i1;
	int i2;
	int b1 : 1;
	int b2 : 1;
	int i3;
	int b3 : 1;
} StructWithBits;

typedef struct _SingleLineStruct {
	int		first_int;
	char*	second_char;
	F32		third_float;
	REF_TO(RTHObject) fourth_ref;
} SingleLineStruct;

typedef struct _StructIntArray {
	int*	array;
} StructIntArray;

typedef struct _RawSubstructure {
	char	a;
	int		foo;
	int		yada;
} RawSubstructure;

AUTO_ENUM;
typedef enum enumWidgetType
{
	POLYWIDGET_COLOR,
	POLYWIDGET_STRING,
	POLYWIDGET_VEC3,
} enumWidgetType;

AUTO_STRUCT;
typedef struct PolymorphicWidget {
	int typeofwidget; AST( POLYPARENTTYPE )
} PolymorphicWidget;

AUTO_STRUCT;
typedef struct ColorWidget {
	PolymorphicWidget widget; AST( POLYCHILDTYPE(POLYWIDGET_COLOR) )
	int red;
	int blue;
	int green;
} ColorWidget;

AUTO_STRUCT;
typedef struct StringWidget {
	PolymorphicWidget widget; AST( POLYCHILDTYPE(POLYWIDGET_STRING) )
	char* firststring;
	char* secondstring;
} StringWidget;

AUTO_STRUCT;
typedef struct Vec3Widget {
	PolymorphicWidget widget; AST( POLYCHILDTYPE(POLYWIDGET_VEC3) )
	Vec3	vec;
} Vec3Widget;

// just some testing of parser capabilities stuck in here
typedef struct TestBlock {
	int			maskfield;
	int**		intstruct;
	F32*		floatarray;
	F32			testinitfloat;
	int*		intarray;
//	MultiVal	multival;
//	MultiVal	multival4[4];
	MultiVal**	multivalpp;
	int			rawdata[4];
	char*		definefield;
	REF_TO(RTHObject) testref;

	PolymorphicWidget** widgets;

	RawSubstructure rawstructs;
	StructWithBits *singleStruct;
	StructWithBits **eastruct;

	SingleLineStruct** singlelinestruct;
	StructIntArray** structintarray;
	StructFunctionCall** functioncall;

	StructWithBits bitsStruct;

	Vec3 vec3_1;
	IVec3 ivec3_1;
	Vec3 vec3_2;
	IVec3 ivec3_2;

	U32 bfParamsSpecified[10]; 

} TestBlock;

typedef struct TestBlockList {
	TestBlock**	testblocks;
} TestBlockList;

ParseTable ParseSingleLineStruct[] = {
	{ "",	TOK_STRUCTPARAM | TOK_INT(SingleLineStruct,first_int,0) },
	{ "",	TOK_STRUCTPARAM | TOK_STRING(SingleLineStruct,second_char,0) },
	{ "",	TOK_STRUCTPARAM | TOK_F32(SingleLineStruct,third_float,0) },
//	{ "",	TOK_STRUCTPARAM | TOK_REFERENCE(SingleLineStruct,fourth_ref,0,"TestHarness") },
	{ "\n",			TOK_END,		0 },
	{ "", 0, 0 }
};

ParseTable ParseStructIntArray[] = {
	{ "",	TOK_STRUCTPARAM | TOK_INTARRAY(StructIntArray,array) },
	{ "\n",			TOK_END,		0 },
	{ "", 0, 0 }
};

DefineContext* pFlagDefines = NULL;
DefineContext* pDynDefines = NULL;

StaticDefineInt maskdefines[] = {
	DEFINE_INT
	{ "flagOne",	1 },
	{ "flagTwo",	2 },
	{ "flagFour",	4 },
//	DEFINE_EMBEDDYNAMIC_INT(pFlagDefines)
	DEFINE_END
};

STATIC_DEFINE_WRAPPER(ParseDynDefines, pDynDefines);

extern ParseTable polyTable_PolymorphicWidget[];

ParseTable ParseTestBlock[] = {
	{ "{",				TOK_START,	0 },
	{ "maskfield",		TOK_AUTOINT(TestBlock,maskfield, 0),		maskdefines, TOK_FORMAT_FLAGS },
//	{ "definefield",	TOK_ESTRING | TOK_STRING(TestBlock,definefield, 0), ParseDynDefines },
	{ "floatarray",		TOK_F32ARRAY(TestBlock,floatarray) },
	{ "testinitfloat",	TOK_F32(TestBlock,testinitfloat, -1) },
	{ "intarray",		TOK_INTARRAY(TestBlock,intarray) },

//	{ "multival",		TOK_MULTIVAL(TestBlock, multival) },
//	{ "multival4",		TOK_MULTIARRAY(TestBlock, multival4) },
	{ "multivalpp",		TOK_MULTIEARRAY(TestBlock, multivalpp) },
//	{ "reffield",		TOK_REFERENCE(TestBlock,testref,0,"TestHarness") },
	{ "singlestruct",	TOK_OPTIONALSTRUCT(TestBlock,singleStruct, parse_StructWithBits) },
	{ "eastruct",		TOK_STRUCT(TestBlock,eastruct, parse_StructWithBits) },
	{ "singlelinestruct",	TOK_STRUCT(TestBlock,singlelinestruct,ParseSingleLineStruct) },
	{ "structintarray",		TOK_STRUCT(TestBlock,structintarray,ParseStructIntArray) },
	{ "functioncall", TOK_FUNCTIONCALL(TestBlock,functioncall)	},
	{ "bitsStruct", TOK_EMBEDDEDSTRUCT(TestBlock, bitsStruct, parse_StructWithBits) },
	{ "Widget",		TOK_POLYMORPH(TestBlock, widgets, polyTable_PolymorphicWidget ) },
	{ "ParamBitfield",	TOK_USEDFIELD | TOK_FIXED_ARRAY | TOK_AUTOINTARRAY(TestBlock,bfParamsSpecified)		},
	{ "TestVec3_1",		TOK_VEC3(TestBlock,vec3_1) },
	{ "TestIVec3_1",	TOK_IVEC3(TestBlock,ivec3_1) },
	{ "TestVec3_2",		TOK_VEC3(TestBlock,vec3_2) },
	{ "TestIVec3_2",	TOK_IVEC3(TestBlock,ivec3_2) },
	{ "}",			TOK_END,		0 },
	{ "", 0, 0 }
};

ParseTable ParseTestBlockList[] = {
	{ "TestBlock",  TOK_STRUCT(TestBlockList,testblocks, ParseTestBlock) },
	{ "", 0, 0 }
};
#define TYPE_ParseTestBlockList TestBlockList

typedef struct TreeTest
{
	const char *filespec;
	struct TreeTest **folders;
} TreeTest;

static ParseTable parse_treetest[] = {
	{ "filespec",	TOK_STRUCTPARAM|TOK_POOL_STRING|TOK_STRING(TreeTest,filespec,0)},
	{ "Folder",	TOK_STRUCT(TreeTest, folders, parse_treetest)},
	{ "EndFolder",	TOK_END,			0},
	{ "", 0, 0 }
};
#define TYPE_parse_treetest TreeTest


AUTO_STRUCT;
typedef struct StashTableTest
{
	StashTable stData;
} StashTableTest;

AUTO_STRUCT;
typedef struct SharedMemoryTest
{
	TestBlock**	testblocks;	AST( NAME(TestBlock) STRUCT(ParseTestBlock) )
	StashTableTest stashStruct;
} SharedMemoryTest;

AUTO_STRUCT;
typedef struct BitFieldTest1
{
	int v;
	S32 s1:1;
	U32 u1:1;
	U32 u2:2;
} BitFieldTest1;

AUTO_STRUCT;
typedef struct BitFieldTest2
{
	int v;
	U32 pad1; NO_AST
// 	S32 pad2:3; NO_AST
	S32 s1:1;
	U32 u1:1;
	U32 pad3:29; NO_AST
	U32 u2:2;
} BitFieldTest2;

AUTO_STRUCT;
typedef struct BitFieldParentTest1
{
	BitFieldTest1 **Child;
} BitFieldParentTest1;

AUTO_STRUCT;
typedef struct BitFieldParentTest2
{
	BitFieldTest2 **Child;
} BitFieldParentTest2;


DefineList g_moredefines[] = {
	{ "One",			"1" },
	{ "Two",			"2" },
	{ 0, 0 }
};

DefineIntList g_flags[] = {
	{ "flagEight",		8 },
	{ "flagSixteen",	16 },
	{ 0, 0 }
};

DefineList g_otherdefines[] = {
	{ "Foo",			"Bar" },
	{ "Good",			"Food" },
	{ "Holy",			"Metal" },
	{ 0, 0 }
};

static char* parserdebug_treetest =
//"// some comment\n"
"Folder fname\n"
"	Folder subfolder\n"
"	EndFolder\n"
"	Folder subfolder2\n"
"	EndFolder\n"
"EndFolder\n";


static char* parserdebug_testinput = 
"TestBlock\n"
"{\n"
"	maskfield flagOne, flagTwo, flagFour\n"
//"	definefield Holy\n"
"	floatarray 1.0 1.5 2.0 2.5 3.0 -1 -2 -3 2 1 // 2.54 will be added\n"
"	// let the test float stay\n"
//"	reffield \"<Object 1>\"\n"
"	intarray 1 2 3 4\n"
//"	multival  FLT 3.45\n"
//"	multival4 FLT 1.23 INT 3 NON IDS YO_Wazz_up\n"
"	multivalpp NON INT 1 FLT 1.23 INS 4 1 2 3 4 FLS 3 1.0 2.0 3.0 QAT 1.1 2.2 3.3 4.4 IDS YO_Wazz_up VEC 1.0 2.0 3.0 NON\n"
//"	singlelinestruct 2, Hello, 2.5, \"<Object 2>\"\n"
//"	singlelinestruct 3, Jonathan, 3.5, \"<Object 3>\"\n"
"	singlelinestruct 2, Hello, 2.5\n"
"	singlelinestruct 3, Jonathan, 3.5\n"
"	structintarray 456 789 1000\n"


"	singlestruct\n" 
"	{\n"
"	}\n"
"	eastruct\n"
"	{\n"
"	}\n"
"	eastruct\n"
"	{\n"
"		i1 15\n"
"	}\n"
"	structintarray\n"
"	structintarray 2\n"
"	TestVec3_1 0 0.5 2.3\n"
"	TestIVec3_1 2 0 3\n"
"   bitsStruct\n"
"   {\n"
"      i1 15\n"
"      i2 17\n"
"      i3 19\n"
"      b1 0\n"
"      b2 1\n"
"      b3 0\n"
"   }\n"
"	Widget ColorWidget {\n"
"		red 0\n"
"		blue 1\n"
"		green 2\n"
"	}\n"
"	Widget StringWidget {\n"
"		firststring \"hello\"\n"
"		secondstring \"world\"\n"
"	}\n"
"	Widget Vec3Widget {\n"
"		vec 0.32 0.42 0.5\n"
"	}\n"
"\n"
"} \n"
"\n"
"TestBlock\n"
"{ \n"
"	maskfield 2147483648, 1\n"
//"	definefield Curmudgeon\n"
"	testinitfloat 2.5\n"
"	functioncall f1(\"a())))\", \"(b\", \")c\") f2(d, e, f) f3(g h i)\n"


//commenting this out because it screwed up the used field compare, since reading in an empty embedded struct sets the bit,
//then writing it out writes out nothing (new optimization), then reading it back in again does NOT set the bit

//"   bitsStruct\n"
//"   {\n"
//"   }\n"
"}\n";

static char* parserdebug_nodefines =
"TestBlock\n"
"{\n"
//"	maskfield 1, 2, 4, 8\n"
//"	definefield Metal\n"
"	floatarray 1.0 1.5 2.0 2.5 3.0 -1 -2 -3 2 1 // 2.54 will be added\n"
"	// let the test float stay\n"
//"	reffield \"<Object 1>\"\n"
"	intarray 1 2 3 4\n"
//"	singlelinestruct 2, Hello, 2.5, \"<Object 2>\"\n"
//"	singlelinestruct 3, Jonathan, 3.5, \"<Object 3>\"\n"
"	singlelinestruct 2, Hello, 2.5\n"
"	singlelinestruct 3, Jonathan, 3.5\n"
"	structintarray 456 789 1000\n"
"	structintarray\n"
"	structintarray 2\n"
"   bitsStruct\n"
"   {\n"
"      i1 15\n"
"      i2 17\n"
"      i3 19\n"
"      b1 0\n"
"      b2 1\n"
"      b3 0\n"
"   }\n"
"	Widget ColorWidget {\n"
"		red 0\n"
"		blue 1\n"
"		green 2\n"
"	}\n"
"	Widget StringWidget {\n"
"		firststring \"hello\"\n"
"		secondstring \"world\"\n"
"	}\n"
"	Widget Vec3Widget {\n"
"		vec 0.32 0.42 0.5\n"
"	}\n"
"} \n"
"\n"
"TestBlock\n"
"{ \n"
//"	maskfield 2147483648, 1\n"
//"	definefield Curmudgeon\n"
"	testinitfloat 2.5\n"
"	functioncall f1(\"a())))\", \"(b\", \")c\") f2(d, e, f) f3(g h i)\n"
"}\n";

#include "autogen/textparser_c_ast.c"

AUTO_FIXUPFUNC;
TextParserResult TestFixupFunc(StashTableTest* sttest, enumTextParserFixupType eType, void *pExtraData)
{
	if (eType == FIXUPTYPE_POST_BINNING_DURING_LOADFILES) {
		if (!sttest->stData)
			sttest->stData = stashTableCreateWithStringKeys(16, StashDeepCopyKeys_NeverRelease);
		stashAddInt(sttest->stData, "testkey", 1, true);
	}
	return PARSERESULT_SUCCESS;
}



void TestTokenTable(void)
{
	int i;
	for (i = TOK_IGNORE; i < NUM_TOK_TYPE_TOKENS; i++)
		assert(TYPE_INFO(i).type == (U32)i);
}



#define FAIL(str) { printf("Failed: %s", str); ControllerScript_Failed(str); bFailed = true; }

bool sbNoSharedForTestTextParser = false;
static DictionaryHandle hAutoUnpackDict;
static PackedStructStream packedStructStream;

void AutoUnpackTestRequest(DictionaryHandleOrName dictHandle, int command, ConstReferenceData pRefData, Referent pReferent, const char* reason)
{
	resUnpackHandleRequest(dictHandle, command, pRefData, pReferent, &packedStructStream);
}

AUTO_RUN;
void InitTestTextParser(void)
{
	hAutoUnpackDict = RefSystem_RegisterSelfDefiningDictionary_dbg("AutoUnpackTest", false, ParseTestBlockList, true, false, NULL, FAKEFILENAME_TESTTEXTPARSER, __LINE__);
	resDictRequestMissingResources(hAutoUnpackDict, 20, false, AutoUnpackTestRequest);
}

//xbox runs out of memory/stack space easily, so only 2 threads
#if PLATFORM_CONSOLE
#define NUM_TESTTEXTPARSER_THREADS 2
#else
#define NUM_TESTTEXTPARSER_THREADS 5
#endif
//will be set to 1 for success, -1 for failure
static int iTestTextParserThreadsReturnVals[NUM_TESTTEXTPARSER_THREADS] = {0};
static char *pTestTextParserThreadReturnStrings[NUM_TESTTEXTPARSER_THREADS] = {0};

//something that is unique in case multiple apps are running TestTextParser at once
static char sTestTextParserFilenamePrefix[MAX_PATH] = "";


#define THREADED_FAIL(str) { estrPrintf(&pTestTextParserThreadReturnStrings[iMyThreadIndex], "%s", str); iMyResult = -1; }



//printfs coming from too many threads can break the CB, since the CB normally runs servers with -forkPrintfsToFile set,
//causing extra stuff to happen in lots of threads leading to malloc unhappiness. So normally turn off printfing in the
//threading tests
#define NO_TTP_THREAD_PRINTFS 1

#if NO_TTP_THREAD_PRINTFS
#define TTP_THREAD_PRINTF(...) {}
#else
#define TTP_THREAD_PRINTF(...) printf(__VA_ARGS__)
#endif

static DWORD WINAPI TestTextParser_Thread(LPVOID lpParam)
{
	EXCEPTION_HANDLER_BEGIN

	int j;
	int iMyResult = 0;
	int iMyThreadIndex = (INT_PTR)lpParam;

	for (j=0; j < 1; j++)
	{


		TestBlockList parserdebug_fromtext = {0};
		TestBlockList parserdebug_fromfile = {0};
		TestBlockList parserdebug_frombin = {0};
		TestBlockList parserdebug_frompkt = {0};

		char textFileName[MAX_PATH]; //  c:\\parsertest.txt
		char fromFileTextFileName[MAX_PATH]; //  c:\\parsertest.fromfile.txt
		char binFileName[MAX_PATH]; // parsertest.bin
		char fromBinFileName[MAX_PATH]; // c:\\parsertest.frombin.txt
		char fromPktFileName[MAX_PATH]; //c:\\parsertest.frompkt.txt

		sprintf(textFileName, "%s_%d.txt", sTestTextParserFilenamePrefix, iMyThreadIndex);
		sprintf(fromFileTextFileName, "%s.fromfile_%d.txt", sTestTextParserFilenamePrefix, iMyThreadIndex);
		sprintf(binFileName, "%s_%d.bin", sTestTextParserFilenamePrefix, iMyThreadIndex);
		sprintf(fromBinFileName, "%s.frombin_%d.txt", sTestTextParserFilenamePrefix, iMyThreadIndex);
		sprintf(fromPktFileName, "%s.frompkt_%d.txt", sTestTextParserFilenamePrefix, iMyThreadIndex);

		//From input string to debug_fromtext -> parsertest.txt
		TTP_THREAD_PRINTF("loading from test string ..");
		if (ParserReadText(parserdebug_testinput, ParseTestBlockList, &parserdebug_fromtext, 0))
		{
			TTP_THREAD_PRINTF("success\n");
		}
		else
		{
			THREADED_FAIL("ParserReadText failure");
		}

		TTP_THREAD_PRINTF("writing test file ..");
		if (ParserWriteTextFile(textFileName, ParseTestBlockList, &parserdebug_fromtext, 0, 0))
		{
			TTP_THREAD_PRINTF("success\n");
		}
		else
		{
			THREADED_FAIL("ParserWriteTextFile failure");
		}

		//From parsertest.txt to debug_fromfile -> parsertest.fromfile.txt
		TTP_THREAD_PRINTF("reading test file ..");
		if (ParserLoadFiles(NULL, textFileName, binFileName, PARSER_FORCEREBUILD, 
			ParseTestBlockList, &parserdebug_fromfile))
		{
			TTP_THREAD_PRINTF("success\n");
		}
		else
		{
			THREADED_FAIL("ParserLoadFiles failure");
		}
		ParserWriteTextFile(fromFileTextFileName, ParseTestBlockList, &parserdebug_fromfile, 0, 0);

		TTP_THREAD_PRINTF("comparing file to test string ..");
		if (StructCompare(ParseTestBlockList, &parserdebug_fromtext, &parserdebug_fromfile, 0, 0, 0))
		{
			THREADED_FAIL("ParserWriteTextFile + StructCompare failure");
		}
		else
		{
			TTP_THREAD_PRINTF("success\n");
		}

		
		//From parsertest.bin to parserdebug_frombin -> parser.frombin.txt
		TTP_THREAD_PRINTF("getting precompiled result from .bin file ..");
		if (ParserLoadFiles(NULL, textFileName, binFileName, 0, 
			ParseTestBlockList, &parserdebug_frombin))
		{
			TTP_THREAD_PRINTF("success\n");
		}
		else
		{
			THREADED_FAIL("ParserLoadFiles failure");
		}
		ParserWriteTextFile(fromBinFileName, ParseTestBlockList, &parserdebug_frombin, 0, 0);

		TTP_THREAD_PRINTF("comparing bin file to text file ..");
		if (StructCompare(ParseTestBlockList, &parserdebug_fromfile, &parserdebug_frombin, 0, 0, 0))
		{
			THREADED_FAIL("ParserWriteTextFile and StructCompare failure");
		}
		else
		{
			TTP_THREAD_PRINTF("success\n");
		}

		{
			Packet* pkt;
			NetLink dummy_link = {0};
			dummy_link.flags = LINK_PACKET_VERIFY;
		
			TTP_THREAD_PRINTF("Writing to Packet ..");

			pkt = pktCreateRaw(&dummy_link);
			ParserSend(ParseTestBlockList, pkt, NULL, &parserdebug_fromtext, SENDDIFF_FLAG_FORCEPACKALL, 0, 0, NULL);
			TTP_THREAD_PRINTF("success\n");

			TTP_THREAD_PRINTF("comparing pkt to text file ..");
			pktSetIndex(pkt, 0);
			ParserRecv(ParseTestBlockList, pkt, &parserdebug_frompkt, 0);
			ParserWriteTextFile(fromPktFileName, ParseTestBlockList, &parserdebug_frompkt, 0, 0);
			if (StructCompare(ParseTestBlockList, &parserdebug_fromtext, &parserdebug_frompkt, 0, 0, 0))
			{	
				THREADED_FAIL("ParserWriteTextFile and StructCompare failure");
			}
			else
			{
				TTP_THREAD_PRINTF("success\n");
			}
		}
		{
			extern void testBitStream(void);
			testBitStream(); // Test this while we're at it.
		}


		StructDeInit(ParseTestBlockList, &parserdebug_fromtext);
		StructDeInit(ParseTestBlockList, &parserdebug_fromfile);
		StructDeInit(ParseTestBlockList, &parserdebug_frombin);
		StructDeInit(ParseTestBlockList, &parserdebug_frompkt);
	}

	if (iMyResult == 0)
	{
		iMyResult = 1;
	}

	iTestTextParserThreadsReturnVals[iMyThreadIndex] = iMyResult;

	EXCEPTION_HANDLER_END

	return 0;
}

AUTO_RUN;
void TestTextParserInitTPI(void)
{
	ParserSetTableInfoRecurse(ParseTestBlockList,sizeof(TestBlockList),"TestBlockList",NULL,__FILE__, NULL, SETTABLEINFO_NAME_STATIC | SETTABLEINFO_ALLOW_CRC_CACHING);
	ParserSetTableInfo(parse_treetest, sizeof(TreeTest), "parse_TreeTest", NULL, __FILE__, 0);
}

// Runs tests on the textparser system, and prints results to console
AUTO_COMMAND ACMD_CATEGORY(Debug) ACMD_CMDLINE;
void TestTextParser(void)
{
	TestBlockList parserdebug_fromtext = {0};
	TestBlockList parserdebug_fromfile = {0};
	TestBlockList parserdebug_frombin = {0};
	TestBlockList parserdebug_frompkt = {0};
	TestBlockList parserdebug_fromschema = {0};
	TestBlockList parserdebug_frommulti = {0};
	SharedMemoryTest parserdebug_fromshared = {0};
	MultiVal mv = {0};
	ParseTable** schema = 0;
	int size = 0;
	int col = 0;
	int i;
	bool bFailed = false;


	char textFileName[MAX_PATH]; 
	char fromFileTextFileName[MAX_PATH]; 
	char binFileName[MAX_PATH]; 
	char fromBinFileName[MAX_PATH]; 
	char fromPktFileName[MAX_PATH]; 
	char fromPackedFileName[MAX_PATH];
	char schemaFileName[MAX_PATH]; 
	char schema2FileName[MAX_PATH]; 
	char fromSchemaFileName[MAX_PATH];
	char testSharedBinFileName[MAX_PATH];



	//save this value off so we can restore it when done
	bool bOldBinsMustExist = gbBinsMustExist;
	gbBinsMustExist = false;

#if _PS3
	sprintf(sTestTextParserFilenamePrefix, "/app_home/TestTextParser/testTextParser_ps3");
    mkdirtree(sTestTextParserFilenamePrefix);
#elif _XBOX
	sprintf(sTestTextParserFilenamePrefix, "devkit:\\TestTextParser\\testTextParser_xbox");
	mkdirtree(sTestTextParserFilenamePrefix);
#else
	sprintf(sTestTextParserFilenamePrefix, "c:\\temp\\TestTextParser\\testTextParser_%u", _getpid());
	mkdirtree(sTestTextParserFilenamePrefix);
	PurgeDirectoryOfOldFiles_Secs("c:\\temp\\TestTextParser", 60, NULL, false);
#endif

	sprintf(textFileName, "%s.txt", sTestTextParserFilenamePrefix);
	sprintf(fromFileTextFileName, "%s.fromfile.txt", sTestTextParserFilenamePrefix);
	sprintf(binFileName, "%s.bin", sTestTextParserFilenamePrefix);
	sprintf(fromBinFileName, "%s.frombin.txt", sTestTextParserFilenamePrefix);
	sprintf(fromPktFileName, "%s.frompkt.txt", sTestTextParserFilenamePrefix);
	sprintf(fromPackedFileName, "%s.fromPacked.txt", sTestTextParserFilenamePrefix);
	sprintf(fromSchemaFileName, "%s.fromSchema.txt", sTestTextParserFilenamePrefix);
	sprintf(schemaFileName, "%s.schema.txt", sTestTextParserFilenamePrefix);
	sprintf(schema2FileName, "%s.schema2.txt", sTestTextParserFilenamePrefix);
	sprintf(testSharedBinFileName, "%s.fromshared.bin", sTestTextParserFilenamePrefix);

	RefSystem_Init();
	//RTH_Test();
	TestTokenTable();

	// defines only for testing
	printf("setting up textparser defines & clearing old structs ..");

	if (pFlagDefines)
		DefineDestroy(pFlagDefines);
	if (pDynDefines)
		DefineDestroy(pDynDefines);
	pFlagDefines = DefineCreateFromIntList(g_flags);
	pDynDefines = DefineCreateFromList(g_otherdefines);
	DefineSetHigherLevel(pDynDefines, pFlagDefines);
	printf("success\n");

	// Hack to name the parse tables the same thing so that it doesn't change the CRC because of the name

	//AWERNER these asserts will only be true the first time, causing TestTextParser to fail if run twice.
	//That seems bad.
	{
		static bool bFirst = true;
		if (bFirst)
		{
			bFirst = false;
			assert(stricmp(parse_BitFieldTest2[0].name, "BitFieldTest2")==0);
			assert(stricmp(parse_BitFieldParentTest2[0].name, "BitFieldParentTest2")==0);
		}
	}
	parse_BitFieldTest2[0].name = parse_BitFieldTest1[0].name;
	parse_BitFieldParentTest2[0].name = parse_BitFieldParentTest1[0].name;
// 	printf("BFT1 CRC: %08x\n", ParseTableCRC(parse_BitFieldTest1, NULL));
// 	printf("BFT2 CRC: %08x\n", ParseTableCRC(parse_BitFieldTest2, NULL));
// 	printf("BFPT1 CRC: %08x\n", ParseTableCRC(parse_BitFieldParentTest1, NULL));
// 	printf("BFPT2 CRC: %08x\n", ParseTableCRC(parse_BitFieldParentTest2, NULL));
	if (ParseTableCRC(parse_BitFieldTest1, NULL, 0) != 
		ParseTableCRC(parse_BitFieldTest2, NULL, 0))
	{
		FAIL("ParseTableCRC bitfields failure");
	}
	if (ParseTableCRC(parse_BitFieldParentTest1, NULL, 0) != 
		ParseTableCRC(parse_BitFieldParentTest2, NULL, 0))
	{
		FAIL("ParseTableCRC of parent struct with children having bitfields failure");
	}


	if (0) {
		TreeTest tt = {0};
		printf("loading from test string ..");
		if (ParserReadText(parserdebug_treetest, parse_treetest, &tt, 0))
			printf("success\n");
		else
			FAIL("ParserReadText failure");
		StructDeInit(parse_treetest, &tt);
	}


	//From input string to debug_fromtext -> parsertest.txt
	printf("loading from test string ..");
	if (ParserReadText(parserdebug_testinput, ParseTestBlockList, &parserdebug_fromtext, 0))
		printf("success\n");
	else
		FAIL("ParserReadText failure");

	printf("writing test file ..");
	if (ParserWriteTextFile(textFileName, ParseTestBlockList, &parserdebug_fromtext, 0, 0))
		printf("success\n");
	else
	{
		FAIL("ParserWriteTextFile failure");
	}

	//From parsertest.txt to debug_fromfile -> parsertest.fromfile.txt
	printf("reading test file ..");
	if (ParserLoadFiles(NULL, textFileName, binFileName, PARSER_FORCEREBUILD, 
		ParseTestBlockList, &parserdebug_fromfile))
		printf("success\n");
	else
	{
		FAIL("ParserLoadFiles failure");
	}
	ParserWriteTextFile(fromFileTextFileName, ParseTestBlockList, &parserdebug_fromfile, 0, 0);

	printf("comparing file to test string ..");
	if (StructCompare(ParseTestBlockList, &parserdebug_fromtext, &parserdebug_fromfile, 0, 0, 0))
	{
		FAIL("ParserWriteTextFile + StructCompare failure");
	}
	else
		printf("success\n");

	
	//From parsertest.bin to parserdebug_frombin -> parser.frombin.txt
	printf("getting precompiled result from .bin file ..");
	if (ParserLoadFiles(NULL, textFileName, binFileName, 0, 
		ParseTestBlockList, &parserdebug_frombin))
		printf("success\n");
	else
	{
		FAIL("ParserLoadFiles failure");
	}
	ParserWriteTextFile(fromBinFileName, ParseTestBlockList, &parserdebug_frombin, 0, 0);

	printf("comparing bin file to text file ..");
	if (StructCompare(ParseTestBlockList, &parserdebug_fromfile, &parserdebug_frombin, 0, 0, 0))
	{
		FAIL("ParserWriteTextFile and StructCompare failure");
	}
	else
		printf("success\n");

	{
		Packet* pkt;
		NetLink dummy_link = {0};
		dummy_link.flags = LINK_PACKET_VERIFY;
	
		printf("Writing to Packet ..");

		pkt = pktCreateRaw(&dummy_link);
		ParserSend(ParseTestBlockList, pkt, NULL, &parserdebug_fromtext, SENDDIFF_FLAG_FORCEPACKALL, 0, 0, NULL);
		printf("success\n");

		printf("comparing pkt to text file ..");
		pktSetIndex(pkt, 0);
		ParserRecv(ParseTestBlockList, pkt, &parserdebug_frompkt, 0);
		ParserWriteTextFile(fromPktFileName, ParseTestBlockList, &parserdebug_frompkt, 0, 0);
		if (StructCompare(ParseTestBlockList, &parserdebug_fromtext, &parserdebug_frompkt, 0, 0, 0))
		{	
			FAIL("ParserWriteTextFile and StructCompare failure");
		}
		else
			printf("success\n");
	}
	{
		extern void testBitStream(void);
		testBitStream(); // Test this while we're at it.
	}
	for (i=0; i<STRUCT_PACK_MAX_METHODS; i++)
	{

		TestBlockList *unpacked;
		U32 index;
		printf("Packing ..");
		PackedStructStreamInit(&packedStructStream, i);
		index = StructPack(ParseTestBlockList, &parserdebug_fromtext, &packedStructStream);
		PackedStructStreamFinalize(&packedStructStream);
		printf("success\n");
		printf("Unpacking ..");
		unpacked = StructUnpack(ParseTestBlockList, &packedStructStream, index);
		ParserWriteTextFile(fromPackedFileName, ParseTestBlockList, unpacked, 0, 0);
		if (StructCompare(ParseTestBlockList, &parserdebug_fromtext, unpacked, 0, 0, 0))
			FAIL("ParserWriteTextFile and StructCompare failure")
		else
			printf("success\n");

		StructDestroyVoid(ParseTestBlockList, unpacked);
		PackedStructStreamDeinit(&packedStructStream);
	}
	{
		TestBlockList *unpacked;
		REF_TO(TestBlockList) testBlockListRef;
		printf("Testing AutoUnpack ..");
		RefSystem_ClearDictionary(hAutoUnpackDict, true);
		PackedStructStreamInit(&packedStructStream, STRUCT_PACK_BITPACK);

		resUnpackPackPotentialReferent(hAutoUnpackDict, "TestStruct", &parserdebug_fromtext, &packedStructStream);
		PackedStructStreamFinalize(&packedStructStream);

		SET_HANDLE_FROM_STRING(hAutoUnpackDict, "TestStruct", testBlockListRef);

		unpacked = GET_REF(testBlockListRef);

		if (StructCompare(ParseTestBlockList, &parserdebug_fromtext, unpacked, 0, 0, 0))
			FAIL("StructCompare failure")
		else
			printf("success\n");
		
		PackedStructStreamDeinit(&packedStructStream);
		if (unpacked)
			RefSystem_RemoveReferent(unpacked, true);
	}

	printf("writing schema to disk ..");
	if (ParseTableWriteTextFile(schemaFileName, ParseTestBlockList, "ParseTestBlockList", 0))
		printf("success\n");
	else
		FAIL("ParseTableWriteTextFile failure");

	printf("reading schema from disk ..");
	if (ParseTableReadTextFile(schemaFileName, &schema, &size, 0, 0))
		printf("success\n");
	else 
		FAIL("ParseTableReadTextFile failure");


	if (schema)
	{
		printf("writing schema to disk again ..");
		if (ParseTableWriteTextFile(schema2FileName, schema[0], "ParseTestBlockList", 0))
			printf("success\n");
		else
			FAIL("ParseTableWriteTextFile failure");

		printf("using schema to load text ..");
		if (size > sizeof(TestBlockList))
			FAIL("schema test failed - I need a larger block than it was stored in??")
		else if (ParserReadText(parserdebug_nodefines, schema[0], &parserdebug_fromschema, 0))
			printf("success\n");
		else 
			FAIL("schema test failed!");

		printf("writing to disk using schema ..");
		if (ParserWriteTextFile(fromSchemaFileName, schema[0], &parserdebug_fromschema, 0, 0))
			printf("success\n");
		else 
			FAIL("ParserWriteTextFile schema test failed");
	}

	// test out to/from multi
	printf("going to and from multival's ..\n");
	StructCopyFields(ParseTestBlockList, &parserdebug_fromtext, &parserdebug_frommulti, 0, 0);

	ParserFindColumn(ParseTestBlock, "floatarray", &col);
	TokenToMultiVal(ParseTestBlock, col, parserdebug_fromtext.testblocks[0], &mv);
	printf("     floatarray: %s\n", MultiValGetString(&mv, 0));
	TokenFromMultiVal(ParseTestBlock, col, parserdebug_frommulti.testblocks[0], &mv);

	ParserFindColumn(ParseTestBlock, "intarray", &col);
	FieldToMultiVal(ParseTestBlock, col, parserdebug_fromtext.testblocks[0], 0, &mv, true, false);
	printf("     intarray[0]: %"FORM_LL"d (should be 1)\n", MultiValGetInt(&mv, 0));
	FieldFromMultiVal(ParseTestBlock, col, parserdebug_frommulti.testblocks[0], 0, &mv);

	ParserFindColumn(ParseTestBlock, "multivalpp", &col);
	FieldToMultiVal(ParseTestBlock, col, parserdebug_fromtext.testblocks[0], 2, &mv, true, false);
	printf("     multivalpp[2]: %f (should be 1.23)\n", MultiValGetFloat(&mv, 0));
	FieldFromMultiVal(ParseTestBlock, col, parserdebug_frommulti.testblocks[0], 2, &mv);

	if (StructCompare(ParseTestBlockList, &parserdebug_fromtext, &parserdebug_frommulti, 0, 0, 0) == 0)
		printf("success\n");
	else 
		FAIL("StructCompare failed after Multival stuff");

	printf("");


	if (!sbNoSharedForTestTextParser)
	{
		printf("Testing loading to shared memory ..");
		if (ParserLoadFilesShared("SM_TestTextParser", NULL, textFileName, testSharedBinFileName, 0, 
			parse_SharedMemoryTest, &parserdebug_fromshared))
		{
			int iResult;
			if (stashFindInt(parserdebug_fromshared.stashStruct.stData, "testkey", &iResult) && (iResult == 1)) {
				printf("success\n");
			} else {
				FAIL("ParserLoadFilesShared failure");
			}
		}
		else
		{
			FAIL("ParserLoadFilesShared failure");
		}
	}


	//ifed out threaded stuff for now... it has enough bugs that it is messing up the CB

	//do threaded tests
	for (i=0; i < NUM_TESTTEXTPARSER_THREADS; i++)
	{
		iTestTextParserThreadsReturnVals[i] = 0;
		estrDestroy(&pTestTextParserThreadReturnStrings[i]);
	}

	for (i=0; i < NUM_TESTTEXTPARSER_THREADS; i++)
	{
		tmCreateThreadEx(TestTextParser_Thread, (void*)((INT_PTR)i), 256 * 1024, 0);
	}

	//now wait for all threads to finish
	do
	{

		Sleep(1);

		for (i=0; i < NUM_TESTTEXTPARSER_THREADS; i++)
		{
			if (iTestTextParserThreadsReturnVals[i] == 0)
			{
				break;
			}
		}
	} while (i != NUM_TESTTEXTPARSER_THREADS);

	//now, check if any failed
	for (i=0; i < NUM_TESTTEXTPARSER_THREADS; i++)
	{
		if (iTestTextParserThreadsReturnVals[i] == -1)
		{
			FAIL(pTestTextParserThreadReturnStrings[i]);
			break;
		}
	}


	// close out defines
	if (schema)
	{
		StructDeInitVoid(schema[0], &parserdebug_fromschema);
		ParseTableFree(&schema);
	}
	DefineDestroy(pFlagDefines);
	DefineDestroy(pDynDefines);
	pFlagDefines = 0;
	pDynDefines = 0;



	//deinit all structs
	StructDeInit(ParseTestBlockList, &parserdebug_fromtext);
	StructDeInit(ParseTestBlockList, &parserdebug_fromfile);
	StructDeInit(ParseTestBlockList, &parserdebug_frombin);
	StructDeInit(ParseTestBlockList, &parserdebug_frompkt);
	StructDeInit(ParseTestBlockList, &parserdebug_frommulti);

	printf("Finished!\n");

	if (!bFailed)
	{
		ControllerScript_Succeeded();
	}

	gbBinsMustExist = bOldBinsMustExist;

}

AUTO_COMMAND ACMD_CATEGORY(Debug) ACMD_CMDLINE;
void TestTextParser_NoShared(void)
{
	sbNoSharedForTestTextParser = true;
	TestTextParser();
	sbNoSharedForTestTextParser = false;
}

int ParserWriteTextSafe(char **estrStruct, char **estrTPI, U32 *uCRC, ParseTable *tpi,void *struct_mem, WriteTextFlags iWriteTextFlags, StructTypeField iOptionFlagsToMatch, StructTypeField iOptionFlagsToExclude)
{
	const char *pTableName = ParserGetTableName(tpi);
	if (ParseTableWriteText(estrTPI, tpi, pTableName ? pTableName : "unknown", PARSETABLESENDFLAG_MAINTAIN_BITFIELDS))
	{
		*uCRC = ParseTableCRC(tpi, NULL, 0);
		return ParserWriteText(estrStruct, tpi, struct_mem, iWriteTextFlags, iOptionFlagsToMatch, iOptionFlagsToExclude);
	}
	return 0;
}
int ParserReadTextSafeWithCommentEx(const char *strStruct, const char *strTPI, U32 iOtherCRC, ParseTable *tpi,void *struct_mem, int iFlags, const char *pComment, const char *pFunction, int iLine)
{
	ParseTable **ppParseTables = NULL;
	U32 iLocalCRC = ParseTableCRC(tpi, NULL, 0);
	int iSize;
	char *pName;

	
	if (!ParseTableReadText(strTPI, &ppParseTables, &iSize, &pName, PARSETABLESENDFLAG_MAINTAIN_BITFIELDS))
	{
		Errorf("ParseTableReadText failure during ParserReadTextSafe");
		return 0;
	}

	if (iLocalCRC == iOtherCRC)
	{
		ParseTableFree(&ppParseTables);
		return ParserReadTextWithCommentEx(strStruct, tpi, struct_mem, iFlags, pComment, pFunction, iLine);
	}
	else
	{
		void *pOtherStruct = StructCreateVoid(ppParseTables[0]);
		char *pResultString = NULL;
		enumCopy2TpiResult eResult;

		ParserReadTextWithCommentEx(strStruct, ppParseTables[0], pOtherStruct, iFlags, pComment, pFunction, iLine);
		StructDeInitVoid(tpi, struct_mem);

		eResult = StructCopyFields2tpis(ppParseTables[0], pOtherStruct, tpi, struct_mem, 0, 0, &pResultString);

		if (eResult == COPY2TPIRESULT_FAILED_FIELDS)
		{
			Errorf("ParserReadTextSafe error during struct copying: %s", pResultString);
			return 0;
		}

		StructDestroyVoid(ppParseTables[0], pOtherStruct);
		ParseTableFree(&ppParseTables);
		estrDestroy(&pResultString);
		return 1;
	}
}

DependencyList gDepListCache;

void ParserBinAddFileDep(TextParserState *tps, const char *filename)
{
	if (tps)
	{
		if (tps->flags & PARSER_USE_CRCS)
			FileListInsertChecksum(&tps->parselist, filename, 0);
		else
			FileListInsert(&tps->parselist, filename, 0);
	}
}

void ParserBinAddParseTableDep(TextParserState *tps, ParseTable *pTable)
{
	const char *pchName = ParserGetTableName(pTable);
	if (pchName)
		ParserBinAddParseTableNameDep(tps, pchName);
}

void ParserBinAddParseTableNameDep(TextParserState *tps, const char *pchTable)
{
	U32 val = ParserBinGetValueForDep(DEPTYPE_PARSETABLE, pchTable);
	if (tps)
		DependencyListInsert(&tps->deplist, DEPTYPE_PARSETABLE, pchTable, val);
}

void ParserBinAddExprFuncDep(TextParserState *tps, const char *pchFuncName)
{
	U32 val = ParserBinGetValueForDep(DEPTYPE_EXPR_FUNC, pchFuncName);
	if (tps)
		DependencyListInsert(&tps->deplist, DEPTYPE_EXPR_FUNC, pchFuncName, val);
}

void ParserBinAddValueDep(TextParserState *tps, const char *pchValueName)
{
	U32 val = ParserBinGetValueForDep(DEPTYPE_VALUE, pchValueName);
	if (tps)
		DependencyListInsert(&tps->deplist, DEPTYPE_VALUE, pchValueName, val);
}

void ParserBinHadErrors(TextParserState *tps)
{
	if (tps)
	{
		DependencyListInsert(&tps->deplist, DEPTYPE_VALUE, HAS_ERRORS_DEP_VALUE, 1);
		tps->lf_loadedok = 0;
	}

}

void ParserBinRegisterDepValue(const char *pchValueName, U32 val)
{
	DependencyEntry *depEntry = DependencyListFind(&gDepListCache, DEPTYPE_VALUE, pchValueName);
	if (depEntry)
	{
		assertmsgf(depEntry->value == val, "Can't register two values for same dependency name %s", pchValueName);
		return;
	}
	assertmsgf(stricmp(pchValueName, HAS_ERRORS_DEP_VALUE) != 0, "Can't use special error name as name");

	DependencyListInsert(&gDepListCache, DEPTYPE_VALUE, pchValueName, val);	
}

U32 ParserBinGetValueForDep(DependencyType type, const char *pchName)
{
	DependencyEntry *depEntry = DependencyListFind(&gDepListCache, type, pchName);
	U32 val = 0;
	if (depEntry)
	{
		return depEntry->value;
	}
	switch (type)
	{
	xcase DEPTYPE_PARSETABLE:
		{
			ParseTable *pTable = ParserGetTableFromStructName(pchName);
			if (pTable)
			{
				val = ParseTableCRC(pTable, NULL, 0);
			}
			else 
			{
				val = 0;
			}			
		}
	xcase DEPTYPE_EXPR_FUNC:
		{		
			val = exprFuncGetCRC(pchName);
		}
	xcase DEPTYPE_VALUE:
		{
			val = 0;
		}

	xcase DEPTYPE_FILE:
		val = fileLastChangedSS2000(pchName);
	}

	DependencyListInsert(&gDepListCache, type, pchName, val);
	return val;
}


/*
typedef struct OptimizedParseTableContext
{
	StashTable OptimizedTableByParentTable;
} OptimizedParseTableContext;

//when evaluating a column for an optimized parse table, there are three possible results:
typedef enum
{
	PTOPT_DONTINCLUDE,
	PTOPT_INCLUDE_NO_REQUIRE, //this is for things like TOK_BEGIN and TOK_END
		//which you will include if you are going to actually make the parsetable, but
		//which don't, in and of themselves, result in the parsetable being made

	PTOPT_REQUIRE,
} enumParseTableOptimizationResult;


ParseTable *ParserGetOptimizedPolyTable(ParseTable pti[], StructTypeField iOptionFlagsToMatch, StructTypeField iOptionFlagsToExclude, 
	enumParseTableOptimizationFlags eOptionFlags, OptimizedParseTableContext *pContext)
{
	return NULL;
}


enumParseTableOptimizationResult ParserMakeOptimizedFieldEval(ParseTable pti[], int iColumn, StructTypeField iOptionFlagsToMatch, StructTypeField iOptionFlagsToExclude, 
	enumParseTableOptimizationFlags eOptionFlags, ParseTable **ppConstructedSubTable, OptimizedParseTableContext *pContext)
{
	if (pti[iColumn].type & TOK_REDUNDANTNAME || TOK_GET_TYPE(pti[iColumn].type) == TOK_IGNORE) 
	{
		return PTOPT_DONTINCLUDE;
	}

	if (TOK_GET_TYPE(pti[iColumn].type) == TOK_START || TOK_GET_TYPE(pti[iColumn].type) == TOK_END)
	{
		if (eOptionFlags & PTOPT_FLAG_NO_START_AND_END)
		{
			return PTOPT_DONTINCLUDE;
		}
		else
		{
			return PTOPT_INCLUDE_NO_REQUIRE;
		}
	}

	if (!FlagsMatchAll(pti[iColumn].type,iOptionFlagsToMatch))
	{
		return PTOPT_DONTINCLUDE;
	}

	if (!FlagsMatchNone(pti[iColumn].type,iOptionFlagsToExclude))
	{
		return PTOPT_DONTINCLUDE;
	}

	if (TOK_HAS_SUBTABLE(pti[iColumn].type))
	{
		if (TOK_GET_TYPE(pti[iColumn].type) == TOK_POLYMORPH_X)
		{
			*ppConstructedSubTable = ParserGetOptimizedPolyTable(pti[iColumn].subtable, iOptionFlagsToMatch, iOptionFlagsToExclude, eOptionFlags, pContext);

			return (*ppConstructedSubTable) ? PTOPT_REQUIRE : PTOPT_DONTINCLUDE;
		}
		else
		{
			*ppConstructedSubTable = ParserMakeOptimizedParseTable(pti[iColumn].subtable, iOptionFlagsToMatch, iOptionFlagsToExclude, eOptionFlags, pContext);

			return (*ppConstructedSubTable) ? PTOPT_REQUIRE : PTOPT_DONTINCLUDE;
		}
	}



	return PTOPT_REQUIRE;
}






ParseTable *ParserMakeOptimizedParseTable(ParseTable pti[], StructTypeField iOptionFlagsToMatch, StructTypeField iOptionFlagsToExclude, 
	enumParseTableOptimizationFlags eOptionFlags, OptimizedParseTableContext *pContext)
{
	int iRequiredFieldCount = 0;
	int iIncludedFieldCount = 0;
	ParseTable **ppSubTables = NULL;
	U32 *pSubTableIndices = NULL;
	U32 *pIncludedIndices = NULL;

	bool bTopLevelCall = false;

	ParseTable *pCurSubTable;

	ParseTable *pOutTable;

	int i;

	if (pContext)
	{
		if (stashFindPointer(pContext->OptimizedTableByParentTable, pti, &pOutTable))
		{
			return pOutTable;
		}
	}
	else
	{
		bTopLevelCall = true;
		pContext = calloc(sizeof(OptimizedParseTableContext), 1);
		pContext->OptimizedTableByParentTable = stashTableCreateAddress(16);
	}



	FORALL_PARSETABLE(pti, i)
	{
		pCurSubTable = NULL;

		switch(ParserMakeOptimizedFieldEval(pti, i, iOptionFlagsToMatch, iOptionFlagsToExclude, eOptionFlags, &pCurSubTable, pContext))
		{
		case PTOPT_DONTINCLUDE:
			break;
		case PTOPT_INCLUDE_NO_REQUIRE:
			iIncludedFieldCount++;
			ea32Push(&pIncludedIndices, i);
			break;
		case PTOPT_REQUIRE:
			iRequiredFieldCount++;
			iIncludedFieldCount++;
			ea32Push(&pIncludedIndices, i);
			if (pCurSubTable)
			{
				eaPush(&ppSubTables, pCurSubTable);
				ea32Push(&pSubTableIndices, i);
			}
			break;
		}
	}

	if (!iRequiredFieldCount)
	{
		pOutTable = NULL;
	}
	else
	{

		pOutTable = calloc(sizeof(ParseTable) * iIncludedFieldCount, 1);
		for (i=0; i < iIncludedFieldCount; i++)
		{
			U32 iCurIndex = pIncludedIndices[0];
			ea32Remove(&pIncludedIndices, 0);

			memcpy(pOutTable + i, pti + iCurIndex, sizeof(ParseTable));

			if (ea32Size(&pSubTableIndices) && pSubTableIndices[0] == iCurIndex)
			{
				pOutTable[i].subtable = ppSubTables[0];

				eaRemove(&ppSubTables, 0);
				ea32Remove(&pSubTableIndices, 0);
			}
		}
	}

	eaDestroy(&ppSubTables);
	ea32Destroy(&pIncludedIndices);
	ea32Destroy(&pSubTableIndices);

	if (bTopLevelCall)
	{
		stashTableDestroy(pContext->OptimizedTableByParentTable);
		free(pContext);
	}
	else
	{
		stashAddPointer(pContext->OptimizedTableByParentTable, pti, pOutTable, false);
	}
	
	return pOutTable;
}
*/

char *gpGlobalCreationComment = NULL;




int StructTextDiffWithNull_dbg(char **estr, ParseTable tpi[], void *new_struct, char *prefix, int iPrefixLen, StructTypeField iOptionFlagsToMatch, StructTypeField iOptionFlagsToExclude, TextDiffFlags eFlags MEM_DBG_PARMS)
{
	int i;
	bool bWroteSomething = false;
	char *pNewPrefix = NULL;
	char *pNewPrefixWriteHead = NULL;
	ParseTableInfo *pInfo = NULL;

	PERFINFO_AUTO_START_FUNC();
	if (!prefix) 
	{
		prefix = "";
		iPrefixLen = 0;
	}

	if (eFlags & TEXTDIFFFLAG_DONTWRITECREATE_NONRECURSING)
	{
		eFlags &= ~TEXTDIFFFLAG_DONTWRITECREATE_NONRECURSING;
	}
	else if (prefix[0])
	{
		estrConcatf(estr,"create %s\n",prefix);
	}
	

	FORALL_PARSETABLE(tpi, i)
	{
		StructTypeField fieldFlagsToMatch = iOptionFlagsToMatch;
		StructTypeField fieldFlagsToExclude = iOptionFlagsToExclude;
		ParseTable *ptcc = &tpi[i];


		StructTypeField type = TOK_GET_TYPE(ptcc->type);
		if (type == TOK_IGNORE ||
			type == TOK_START ||
			type == TOK_END)
			continue;
		if (ptcc->type & TOK_REDUNDANTNAME || ptcc->type & TOK_NO_WRITE) continue;
		if (!ptcc->name || !ptcc->name[0]) continue; // unnamed fields shouldn't be parsed or written


		if (!FlagsMatchAll(ptcc->type,iOptionFlagsToMatch))
		{
			continue;
		}

		if (!FlagsMatchNone(ptcc->type,iOptionFlagsToExclude))
		{
			continue;
		}

		if (eFlags & TEXTDIFFFLAG_SKIP_POLYMORPHIC_CHILDREN && (type == TOK_POLYMORPH_X))
		{
			continue;
		}

		if (!pNewPrefix)
		{
			pInfo = ParserGetTableInfo(tpi);

			pNewPrefix = ScratchAlloc(iPrefixLen + pInfo->iLongestColumnNameLength + 2);
			memcpy(pNewPrefix, prefix, iPrefixLen);
			pNewPrefix[iPrefixLen] = '.';
			pNewPrefixWriteHead = pNewPrefix + iPrefixLen + 1;
		}

		memcpy(pNewPrefixWriteHead, ptcc->name, pInfo->piColumnNameLengths[i] + 1);
		
		textdiffwithnull_autogen(estr, tpi, i, new_struct, 0, pNewPrefix, iPrefixLen + pInfo->piColumnNameLengths[i] + 1,
			iOptionFlagsToMatch, iOptionFlagsToExclude, eFlags, caller_fname, line);
	}
	
	if (pNewPrefix)
	{
		ScratchFree(pNewPrefix);
	}

	PERFINFO_AUTO_STOP();

	return 1;

}

//temporary variable while introducing new text diff mode...
//0, default, means do both old and new, asserting that
//they match. 1 = use new mode. 2 = use old mode
static int siNewTextDiffMode = 1;
AUTO_CMD_INT(siNewTextDiffMode, NewTextDiffMode);


int StructTextDiffWithNull_Verify_dbg(char **estr, ParseTable tpi[], void *new_struct, char *prefix, int iPrefixLen, StructTypeField iOptionFlagsToMatch, StructTypeField iOptionFlagsToExclude, TextDiffFlags eFlags MEM_DBG_PARMS)
{
	char *pStr1 = NULL;
	char *pStr2 = NULL;
	int iRetVal;

	switch (siNewTextDiffMode)
	{
	case 0:
		estrStackCreate(&pStr1);
		estrStackCreate(&pStr2);

		iRetVal = StructTextDiffWithNull_dbg(&pStr1, tpi, new_struct, prefix, iPrefixLen, iOptionFlagsToMatch, iOptionFlagsToExclude, eFlags, caller_fname, line);
		StructWriteTextDiff_dbg(&pStr2, tpi, NULL, new_struct, prefix, iOptionFlagsToMatch, iOptionFlagsToExclude, eFlags, caller_fname, line);

		if (stricmp_safe(pStr1, pStr2) != 0)
		{
			log_printf(LOG_ERRORS, "While diffing %s, with flags %d, %s(%d), got old string %s and new string %s",
				ParserGetTableName(tpi), eFlags, caller_fname, line, pStr2, pStr1);
			assertmsgf(0, "Old- and new- style text diffs  %s(%d) didn't match... talk to Alex. You can disable this by adding -NewTextDiffMode 2 to your command line",
				caller_fname, line);
		}

		if (estrLength(&pStr1))
		{
			estrConcatf(estr, "%s", pStr1);
		}

		estrDestroy(&pStr1);
		estrDestroy(&pStr2);

		return iRetVal;

	case 1:
		return StructTextDiffWithNull_dbg(estr, tpi, new_struct, prefix, iPrefixLen, iOptionFlagsToMatch, iOptionFlagsToExclude, eFlags, caller_fname, line);
	case 2:
		StructWriteTextDiff_dbg(estr, tpi, NULL, new_struct, prefix, iOptionFlagsToMatch, iOptionFlagsToExclude, eFlags, caller_fname, line);
	}

	return 1;
}

void ParserTextDiffWithNull_Verify_dbg(char **estr, ParseTable tpi[], int column, int index, void *newp, char *prefix, int iPrefixLen, StructTypeField iOptionFlagsToMatch, StructTypeField iOptionFlagsToExclude, TextDiffFlags eFlags MEM_DBG_PARMS)
{
	char *pStr1 = NULL;
	char *pStr2 = NULL;


	switch (siNewTextDiffMode)
	{
	case 0:
		estrStackCreate(&pStr1);
		estrStackCreate(&pStr2);

		ParserTextDiffWithNull_dbg(&pStr1, tpi, column, index, newp, prefix, iPrefixLen, iOptionFlagsToMatch, iOptionFlagsToExclude, eFlags, caller_fname, line);
		ParserWriteTextDiff_dbg(&pStr2, tpi, column, index, index, NULL, newp, prefix, iOptionFlagsToMatch, iOptionFlagsToExclude, eFlags, caller_fname, line);

		if (stricmp_safe(pStr1, pStr2) != 0)
		{
			log_printf(LOG_ERRORS, "While diffing %s, with flags %d, %s(%d), got old string %s and new string %s",
				ParserGetTableName(tpi), eFlags, caller_fname, line, pStr2, pStr1);
			assertmsgf(0, "Old- and new- style text diffs  %s(%d) didn't match... talk to Alex. You can disable this by adding -NewTextDiffMode 2 to your command line",
				caller_fname, line);
		}

		if (estrLength(&pStr1))
		{
			estrConcatf(estr, "%s", pStr1);
		}

		estrDestroy(&pStr1);
		estrDestroy(&pStr2);

		return;

	xcase 1:
		ParserTextDiffWithNull_dbg(estr, tpi, column, index, newp, prefix, iPrefixLen, iOptionFlagsToMatch, iOptionFlagsToExclude, eFlags, caller_fname, line);
	xcase 2:
		ParserWriteTextDiff_dbg(estr, tpi, column, index, index, NULL, newp, prefix, iOptionFlagsToMatch, iOptionFlagsToExclude, eFlags, caller_fname, line);
	}


}

#ifdef DEBUG_CRCS
bool giDebugCRCs = false;
char *pDebugCRCPrefix = NULL;
#endif


#include "textparser_h_ast.c"

