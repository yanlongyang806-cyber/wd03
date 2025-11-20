#include "Textparser.h"
#include "earray.h"
#include "StructInternals.h"
#include "textParserutils.h"
#include "TokenStore.h"
#include "WinInclude.h"
#include "UtilitiesLib.h"
#include "timing.h"
#include "StructInit.h"
#include "structInternals_h_ast.h"

//special "comment" passed from structReset into StructInit which tells StructInit to leave the creation comment alone
#define SPECIAL_STRUCTINIT_COMMENT_FROM_STRUCTRESET ((char*)0x1)

AUTO_RUN_ANON(memBudgetAddMapping(__FILE__, BUDGET_EngineMisc););

#define USE_OLD_STRUCTINIT 0
#define USE_OLD_STRUCTDEINIT 0


#define BREAK_ON_STRUCT_CREATE 0
#define BREAK_ON_STRUCT_DESTROY 0



typedef struct
{
	ParseTable *pSubTable;
	int iCurStructOffset;
	int iColumnNum;
} StructInitInfoEmbeddedInfo;

typedef struct
{
	TextParserAutoFixupCB *pFixupFunc;
	int iOffset;
} StructInitExtraFixupInfo;

typedef struct StructInitInfo
{
	void *pTemplate;
	int iSize;
	U32 *piColumns;
	U32 *piColumnsToDeInitNonNULLOnly;
	StructInitInfoEmbeddedInfo **ppEmbeds;
	StructInitExtraFixupInfo **ppExtraFixups;
} StructInitInfo;

static CRITICAL_SECTION sStructInitGlobalLock = {0};
static CRITICAL_SECTION sStructDeInitGlobalLock = {0};


// fill in default values for a newly created struct
void StructInit_Old(ParseTable pti[], void* structptr)
{
	TextParserAutoFixupCB *pFixupCB;
	int i;
	size_t size = ParserGetTableSize(pti);

	if (size)
		memset(structptr, 0, size);

	FORALL_PARSETABLE(pti, i)
	{
		initstruct_autogen(pti, i, structptr, 0);
	} // each token

	pFixupCB = ParserGetTableFixupFunc(pti);

	if (pFixupCB)
	{
		pFixupCB(structptr, FIXUPTYPE_TEMPLATE_CONSTRUCTOR, NULL);
	}

	if (pFixupCB)
	{
		pFixupCB(structptr, FIXUPTYPE_CONSTRUCTOR, NULL);
	}

}

void StructDeInit_Old(ParseTable pti[], void* structptr)
{
	int i;
	TextParserAutoFixupCB *pFixupCB;
	
	if(!structptr){
		return;
	}

	pFixupCB = ParserGetTableFixupFunc(pti);

	if (pFixupCB)
	{
		pFixupCB(structptr, FIXUPTYPE_DESTRUCTOR, NULL);
	}


	FORALL_PARSETABLE(pti, i)
	{
		if (pti[i].type & TOK_REDUNDANTNAME) continue;
		if (pti[i].type & TOK_UNOWNED) continue;
		destroystruct_autogen(pti, &pti[i], i, structptr, 0);
	}
}


typedef struct StructInitMaybeRecurseInfo
{
	ParseTable *pTPI;
	int iOffset;
} StructInitMaybeRecurseInfo;

void FillInStructInitInfo(StructInitInfo *pInfo, ParseTable pti[], bool bDeInit)
{
	StructInitMaybeRecurseInfo **ppRecurseInfos = NULL;
	TextParserAutoFixupCB *pFixupFunc;

	int i;
	FORALL_PARSETABLE(pti, i)
	{
		if (pti[i].type & (TOK_REDUNDANTNAME | TOK_UNOWNED))
		{
			continue;
		}
		
		if (TOK_GET_TYPE(pti[i].type) == TOK_STRUCT_X && TokenStoreGetStorageType(pti[i].type) == TOK_STORAGE_DIRECT_SINGLE)
		{
			StructInitMaybeRecurseInfo *pRecurseInfo = calloc(sizeof(StructInitMaybeRecurseInfo), 1);
			
			pRecurseInfo->pTPI = pti[i].subtable;
			pRecurseInfo->iOffset = (int)pti[i].storeoffset;
			eaPush(&ppRecurseInfos, pRecurseInfo);


			if ((pFixupFunc = ParserGetTableFixupFunc(pti[i].subtable)))
			{
				StructInitExtraFixupInfo *pExtraFixupInfo = calloc(sizeof(StructInitExtraFixupInfo), 1);

				pExtraFixupInfo->pFixupFunc = pFixupFunc;
				pExtraFixupInfo->iOffset = (int)pti[i].storeoffset;
				eaPush(&pInfo->ppExtraFixups, pExtraFixupInfo);
			}

			continue;
		}
	
		if (bDeInit)
		{
			switch (destroystruct_autogen(pti, &pti[i], i, NULL, 0))
			{
			case DESTROY_ALWAYS:
				ea32Push(&pInfo->piColumns, i);
				break;

			case DESTROY_IF_NON_NULL:
				ea32Push(&pInfo->piColumnsToDeInitNonNULLOnly, i);
				break;
			}
		}
		else
		{
			if (initstruct_autogen(pti, i, pInfo->pTemplate, 0))
			{
				ea32Push(&pInfo->piColumns, i);

				
				destroystruct_autogen(pti, &pti[i], i, pInfo->pTemplate, 0);
			}
		}
	}

	while (eaSize(&ppRecurseInfos))
	{
		StructInitMaybeRecurseInfo *pRecurseInfo = ppRecurseInfos[0];
		eaRemove(&ppRecurseInfos, 0);

		FORALL_PARSETABLE(pRecurseInfo->pTPI, i)
		{
			if (pRecurseInfo->pTPI[i].type & (TOK_REDUNDANTNAME | TOK_UNOWNED))
			{
				continue;
			}
			
			if (TOK_GET_TYPE(pRecurseInfo->pTPI[i].type) == TOK_STRUCT_X && TokenStoreGetStorageType(pRecurseInfo->pTPI[i].type) == TOK_STORAGE_DIRECT_SINGLE)
			{
				StructInitMaybeRecurseInfo *pNewRecurseInfo = calloc(sizeof(StructInitMaybeRecurseInfo), 1);
		
				pNewRecurseInfo->pTPI = pRecurseInfo->pTPI[i].subtable;
				pNewRecurseInfo->iOffset = (int)(pRecurseInfo->pTPI[i].storeoffset + pRecurseInfo->iOffset);
				eaPush(&ppRecurseInfos, pNewRecurseInfo);

				if ((pFixupFunc = ParserGetTableFixupFunc(pRecurseInfo->pTPI[i].subtable)))
				{
					StructInitExtraFixupInfo *pExtraFixupInfo = calloc(sizeof(StructInitExtraFixupInfo), 1);

					pExtraFixupInfo->pFixupFunc = pFixupFunc;
					pExtraFixupInfo->iOffset = (int)(pRecurseInfo->pTPI[i].storeoffset + pRecurseInfo->iOffset);
					eaPush(&pInfo->ppExtraFixups, pExtraFixupInfo);
				}

				continue;
			}
		
			if (bDeInit)
			{
				if (destroystruct_autogen(pRecurseInfo->pTPI, &pRecurseInfo->pTPI[i], i, NULL, 0))
				{
					StructInitInfoEmbeddedInfo *pEmbedded = calloc(sizeof(StructInitInfoEmbeddedInfo), 1);
					pEmbedded->iColumnNum = i;
					pEmbedded->iCurStructOffset = pRecurseInfo->iOffset;
					pEmbedded->pSubTable = pRecurseInfo->pTPI;

					eaPush(&pInfo->ppEmbeds, pEmbedded);
				}
			}
			else
			{
				if (initstruct_autogen(pRecurseInfo->pTPI, i, ((char*)(pInfo->pTemplate)) + pRecurseInfo->iOffset, 0))
				{
					StructInitInfoEmbeddedInfo *pEmbedded = calloc(sizeof(StructInitInfoEmbeddedInfo), 1);
					pEmbedded->iColumnNum = i;
					pEmbedded->iCurStructOffset = pRecurseInfo->iOffset;
					pEmbedded->pSubTable = pRecurseInfo->pTPI;

					eaPush(&pInfo->ppEmbeds, pEmbedded);

					destroystruct_autogen(pRecurseInfo->pTPI, &pRecurseInfo->pTPI[i], i, ((char*)(pInfo->pTemplate)) + pRecurseInfo->iOffset, 0);


				}
			}
		}

		free(pRecurseInfo);
	}

	eaDestroy(&ppRecurseInfos);

	FixupStructLeafFirst(pti, pInfo->pTemplate, FIXUPTYPE_TEMPLATE_CONSTRUCTOR, NULL);
}

__forceinline void StructInit_dbg(ParseTable pti[], void* structptr, const char *pComment, const char *callerName, int line)
{
	ParseTableInfo *pInfo = ParserGetTableInfo(pti);
	int i;
	StructInitInfo *pStructInitInfo;
	int iCreationCommentOffset;

	//100% illegal to do StructInit during AUTO_RUN_FIRST, SECOND or INTERNAL, as the auto_fixup functions
	//for the TPIs will not yet have been called
	assertmsgf(giCurAutoRunStep > 2, "Trying to do StructInit using %s during AUTO_RUN_FIRST, _SECOND or _INTERNAL. This is illegal as the auto-fixup for the tpi will not yet have been called",
		ParserGetTableName(pti));

	//early in autoruns, all the various inits are not yet in place for doing high perf struct init, fall back on old method
	if (
#if USE_OLD_STRUCTINIT
		1
#else		
		!pInfo
#endif		
		)
	{
		StructInit_Old(pti, structptr);
		return;
	}

	if (!(pStructInitInfo = pInfo->pStructInitInfo))
	{
		EnterCriticalSection(&sStructInitGlobalLock);

		if (!(pStructInitInfo = pInfo->pStructInitInfo))
		{

			//need to calculate new StructInitInfo
			pStructInitInfo = callocStruct(StructInitInfo);
			pStructInitInfo->pTemplate = StructAllocVoid(pti);

			FillInStructInitInfo(pStructInitInfo, pti, false);

			MemoryBarrier();
			pInfo->pStructInitInfo = pStructInitInfo;
		}

		LeaveCriticalSection(&sStructInitGlobalLock);
	}

	assertmsgf(pInfo->size, "Can't StructInit %s... size seems to be zero", ParserGetTableName(pti));

	memcpy(structptr, pStructInitInfo->pTemplate, pInfo->size);

	for (i=ea32Size(&pStructInitInfo->piColumns) - 1; i >= 0; i--)
	{
		int iColumn = pStructInitInfo->piColumns[i];
		initstruct_autogen(pti, iColumn, structptr, 0);
	}

	for (i=0; i < eaSize(&pStructInitInfo->ppEmbeds); i++)
	{
		StructInitInfoEmbeddedInfo *pEmbedInfo = pStructInitInfo->ppEmbeds[i];
		initstruct_autogen(pEmbedInfo->pSubTable, pEmbedInfo->iColumnNum, ((char*)structptr) + pEmbedInfo->iCurStructOffset, 0);
	}

	if ((iCreationCommentOffset = ParserGetCreationCommentOffset(pti)) && pComment != SPECIAL_STRUCTINIT_COMMENT_FROM_STRUCTRESET)
	{
		const char **ppStrPtr;
		if (!pComment)
		{
			pComment = MaybeGetCreationCommentFromFileAndLine(pti, callerName, line);
		}
		
		if (strStartsWith(pComment, "__"))
		{
			ErrorOrAlert("NO_CREATION_COMMENT", "An struct of type %s with a creation comment field was created without a proper comment. %s(%d)", 
				ParserGetTableName(pti), callerName, line);
		}

		ppStrPtr = (char**)(((char*)structptr) + iCreationCommentOffset);
		*ppStrPtr = pComment;
	}

	//do fixups in reverse order so that inners will always come before outers
	for (i=eaSize(&pStructInitInfo->ppExtraFixups) - 1; i >= 0; i--)
	{
		StructInitExtraFixupInfo *pExtraFixupInfo = pStructInitInfo->ppExtraFixups[i];

		pExtraFixupInfo->pFixupFunc(((char*)structptr) + pExtraFixupInfo->iOffset, FIXUPTYPE_CONSTRUCTOR, NULL);
	}

	if (pInfo->pFixupCB)
	{
		pInfo->pFixupCB(structptr, FIXUPTYPE_CONSTRUCTOR, NULL);
	}
}

void PreDestroyStructInitInfo(StructInitInfo *pInfo, ParseTable *pTPI)
{
	if (pInfo->pTemplate)
	{
		StructDestroyVoid(pTPI, pInfo->pTemplate);
		pInfo->pTemplate = NULL;
	}
}

void DestroyStructInitInfo(StructInitInfo *pInfo, ParseTable *pTPI)
{
	if (pInfo->pTemplate)
	{
		StructDestroyVoid(pTPI, pInfo->pTemplate);
	}

	eaDestroyEx(&pInfo->ppExtraFixups, NULL);
	eaDestroyEx(&pInfo->ppEmbeds, NULL);
	ea32Destroy(&pInfo->piColumns);

	free(pInfo);
}



__forceinline void StructDeInitVoid(ParseTable pti[], void* structptr)
{
	ParseTableInfo *pInfo;
	int i;
	StructInitInfo *pStructDeInitInfo;

	if (!structptr)
	{
		return;
	}

	pInfo = ParserGetTableInfo(pti);

	//early in autoruns, all the various inits are not yet in place for doing high perf struct init, fall back on old method
	if (
#if USE_OLD_STRUCTDEINIT
		1
#else		
		!pInfo || giCurAutoRunStep <= 2
#endif		
		)
	{
		StructDeInit_Old(pti, structptr);
		return;
	}

	if (!(pStructDeInitInfo = pInfo->pStructDeInitInfo))
	{
		EnterCriticalSection(&sStructDeInitGlobalLock);

		if (!(pStructDeInitInfo = pInfo->pStructDeInitInfo))
		{

			//need to calculate new StructDeInitInfo
			pStructDeInitInfo = calloc(sizeof(StructInitInfo), 1);

			FillInStructInitInfo(pStructDeInitInfo, pti, true);

			MemoryBarrier();
			pInfo->pStructDeInitInfo = pStructDeInitInfo;
		}

		LeaveCriticalSection(&sStructDeInitGlobalLock);
	}

	if (pInfo->pFixupCB)
	{
		pInfo->pFixupCB(structptr, FIXUPTYPE_DESTRUCTOR, NULL);
	}

	//do destructors go in reverse order from constructors
	for (i = 0 ;i < eaSize(&pStructDeInitInfo->ppExtraFixups); i++)
	{
		StructInitExtraFixupInfo *pExtraFixupInfo = pStructDeInitInfo->ppExtraFixups[i];

		pExtraFixupInfo->pFixupFunc(((char*)structptr) + pExtraFixupInfo->iOffset, FIXUPTYPE_DESTRUCTOR, NULL);
	}

	for (i=ea32Size(&pStructDeInitInfo->piColumns) - 1; i >= 0; i--)
	{
		int iColumn = pStructDeInitInfo->piColumns[i];
		destroystruct_autogen(pti, &pti[iColumn], iColumn, structptr, 0);
	}

	for (i=ea32Size(&pStructDeInitInfo->piColumnsToDeInitNonNULLOnly) - 1; i >= 0; i--)
	{
		int iColumn = pStructDeInitInfo->piColumnsToDeInitNonNULLOnly[i];
		void *pValue = *((void**)(((char*)structptr) + pti[iColumn].storeoffset));

		if (pValue)
		{
			destroystruct_autogen(pti, &pti[iColumn], iColumn, structptr, 0);
		}
	}

	for (i=0; i < eaSize(&pStructDeInitInfo->ppEmbeds); i++)
	{
		StructInitInfoEmbeddedInfo *pEmbedInfo = pStructDeInitInfo->ppEmbeds[i];
		destroystruct_autogen(pEmbedInfo->pSubTable, &pEmbedInfo->pSubTable[pEmbedInfo->iColumnNum], pEmbedInfo->iColumnNum, ((char*)structptr) + pEmbedInfo->iCurStructOffset, 0);
	}
}









void StructInitPreAutoRunInit(void)
{
	InitializeCriticalSection(&sStructInitGlobalLock);
	InitializeCriticalSection(&sStructDeInitGlobalLock);
}



void*   StructCreate_dbg(ParseTable pti[], const char *pComment, const char* callerName, int line)
{
	ParseTableInfo *info;
	void *created;

	void *pTemplate = NULL;

#if BREAK_ON_STRUCT_CREATE
	if (pti == GetParseTableForCreateBreak())
	{
		DebugBreak();
	}
#endif

	PERFINFO_AUTO_START_FUNC();

	info = ParserGetTableInfo(pti);

	PERFINFO_AUTO_START_STATIC(info->name, &info->piCreate, 1);

	created = StructAlloc_dbg(pti,info,callerName,line);

	if (created)
	{
		if (!pComment)
		{
			pComment = MaybeGetCreationCommentFromFileAndLine(pti, callerName, line);
		}

		StructInit_dbg(pti,created, pComment, callerName, line);
	}

	PERFINFO_AUTO_STOP();
	PERFINFO_AUTO_STOP();

	return created;
}




void StructDestroyVoid(ParseTable pti[], void* structptr)
{ 
#if BREAK_ON_STRUCT_DESTROY
	if (pti == GetParseTableForDestroyBreak())
	{
		DebugBreak();
	}
#endif

	PERFINFO_RUN(
		ParseTableInfo *info;
		PERFINFO_AUTO_START_FUNC();
		info = ParserGetTableInfo(pti);
		PERFINFO_AUTO_START_STATIC(info->name, &info->piDestroy, 1);
	);

	StructDeInitVoid(pti, structptr); 
	_StructFree_internal(pti, structptr); 

	PERFINFO_RUN(
		PERFINFO_AUTO_STOP();
		PERFINFO_AUTO_STOP();
	);
}

void StructResetVoid(ParseTable pti[], void *structptr)
{
	StructDeInitVoid(pti, structptr);
	StructInitWithCommentVoid(pti, structptr, SPECIAL_STRUCTINIT_COMMENT_FROM_STRUCTRESET);
}


ParseTable *DEFAULT_LATELINK_GetParseTableForCreateBreak(void)
{
	return NULL;
}

ParseTable *DEFAULT_LATELINK_GetParseTableForDestroyBreak(void)
{
	return NULL;
}