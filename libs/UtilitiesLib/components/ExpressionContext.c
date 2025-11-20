#include "ExpressionPrivate.h"

#include "CommandQueue.h"
#include "crypt.h"
#include "error.h"
#include "EString.h"
#include "MemoryPool.h"
#include "ScratchStack.h"
#include "StateMachine.h"
#include "StringCache.h"
#include "textparser.h"

AUTO_RUN_ANON(memBudgetAddMapping(__FILE__, BUDGET_GameSystems););

//////////////////////////////////////////////////////////////////////////
// ExprExternVarCategory
//////////////////////////////////////////////////////////////////////////

MP_DEFINE(ExprExternVarCategory);
ExprExternVarCategory* exprExternVarCategoryCreate(void)
{
	MP_CREATE(ExprExternVarCategory, 32);
	return MP_ALLOC(ExprExternVarCategory);
}

void exprExternVarDestroy(ExprExternVarCategory* category)
{
	stashTableDestroyEx(category->externVarOverrides, NULL, MultiValDestroy);
	MP_FREE(ExprExternVarCategory, category);
}

//////////////////////////////////////////////////////////////////////////
// ExprVarEntry
//////////////////////////////////////////////////////////////////////////

ExprVarEntry* exprVarEntryCreate(const char* name MEM_DBG_PARMS)
{
	ExprVarEntry* var = scalloc(1,sizeof(ExprVarEntry));
	var->name = name;
	return var;
}

void exprVarEntryDestroy(ExprVarEntry* entry)
{
	free(entry);
}

void exprVarEntryChangeSimple(ExprContext* context, ExprVarEntry* oldVar, MultiVal* newSimpleVal)
{
	if(oldVar->table || oldVar->simpleVar.type && oldVar->simpleVar.type != newSimpleVal->type)
	{
		ErrorFilenamef(context->curExpr ? context->curExpr->filename : NULL,
			"Redefining expression variable %s with new type", oldVar->name);
	}

	MultiValCopy(&oldVar->simpleVar, newSimpleVal);
}

void exprVarEntryChangePointer(ExprContext* context, ExprVarEntry* oldVar, void* ptr, ParseTable* table, int allowVarAccess, int allowObjPath, bool allowTypeChange)
{
	if(!allowTypeChange && (oldVar->simpleVar.type || oldVar->table && oldVar->table != table))
	{
		ErrorFilenamef(context->curExpr ? context->curExpr->filename : NULL,
			"Redefining expression variable %s with new type", oldVar->name);
	}

	oldVar->inptr = ptr;
	oldVar->table = table;
	oldVar->allowVarAccess = allowVarAccess;
	oldVar->allowObjPath = allowObjPath;
}

//////////////////////////////////////////////////////////////////////////
// ExprContext
//////////////////////////////////////////////////////////////////////////

static int afterLoad = false;

ExprContext* exprContextCreateEx(size_t scratch_size MEM_DBG_PARMS)
{
	ExprContext* context;

	devassertmsg(afterLoad, "Not allowed to create expression contexts until AUTO_RUN or later");
	context = scalloc(1, sizeof(*context));
	context->input = "No Input Given";

#ifdef EXPR_DEBUG_INFO
	context->exprContextFile = caller_fname;
	context->exprContextLine = line;
#endif

	if (scratch_size)
		ScratchStackInit(&context->scratch, scratch_size, SCRATCHSTACK_DEFAULT_FLAGS, STACK_SPRINTF("ExprContext: %s %d", caller_fname, line));

	return context;
}

ExprFuncTable* emptyFuncTable;

ExprContext* exprContextCreateWithEmptyFunctionTableEx(size_t scratch_size MEM_DBG_PARMS)
{
	ExprContext* context = exprContextCreateEx(scratch_size MEM_DBG_PARMS_CALL);
	if(!emptyFuncTable)
		emptyFuncTable = exprContextCreateFunctionTable("Empty");
	exprContextSetFuncTable(context, emptyFuncTable);
	return context;
}

void exprContextDestroy(ExprContext* context)
{
	int i;

	stashTableDestroyEx(context->varTable, NULL, exprVarEntryDestroy);
	stashTableDestroyEx(context->externVarCallbacks, NULL, exprExternVarDestroy);

	for(i = 0; i < EXPR_MAX_ENTARRAY; i++)
		eaDestroy(&context->entArrays[i]);

	for(i = 0; i < ARRAY_SIZE(context->staticVars); i++)
	{
		if(context->staticVars[i])
		{
			exprVarEntryDestroy(context->staticVars[i]);
			context->staticVars[i] = NULL;
		}
	}

	eaDestroyEx(&context->cleanupStack, NULL);
	estrDestroy(&context->stringResult);

	free(context);
}

void exprContextClear(ExprContext* context)
{
	int i;

	//TODO: make "clear" disable all programmer specified entries and destroy all user ones
	stashTableClearEx(context->varTable, NULL, exprVarEntryDestroy);
	stashTableClearEx(context->externVarCallbacks, NULL, exprExternVarDestroy);

	exprContextClearAllEntArrays(context);

	for(i = 0; i < ARRAY_SIZE(context->staticVars); i++)
	{
		if(context->staticVars[i])
		{
			exprVarEntryDestroy(context->staticVars[i]);
			context->staticVars[i] = NULL;
		}
	}
}

ExprContext* exprContextGetGlobalContext()
{
	static ExprContext *pContext = NULL;
	if(!pContext)
		pContext = exprContextCreate();
	return pContext;
}

Entity*** exprContextGetNewEntArray(ExprContext* context)
{
	int i;

	for(i = 0; i < EXPR_MAX_ENTARRAY; i++)
	{
		if(!context->entArrayInUse[i])
		{
			context->entArrayInUse[i] = true;
			eaSetSize(&context->entArrays[i], 0);
			return &context->entArrays[i];
		}
	}

	Errorf("Expression uses more ent arrays than allowed, please ask Raoul");
	return NULL;
}

void exprContextClearEntArray(ExprContext* context, Entity*** entarray)
{
	int i;

	for(i = 0; i < EXPR_MAX_ENTARRAY; i++)
	{
		if(entarray == &context->entArrays[i])
		{
			devassertmsg(context->entArrayInUse[i], "Trying to clear an ent array not in use?");
			context->entArrayInUse[i] = false;
			return;
		}
	}

	devassertmsg(0, "Ent array not found on context");
}

void exprContextClearAllEntArrays(ExprContext* context)
{
	int i;

	for(i = 0; i < EXPR_MAX_ENTARRAY; i++)
	{
		if(context->entArrayInUse[i])
		{
			eaSetSize(&context->entArrays[i], 0);
			context->entArrayInUse[i] = false;
		}
	}
}

void exprContextAddExternVarCategory(ExprContext* context, const char* category,
									ExprExternVarLoadtimeCallback loadtimeCallback,
									ExprExternVarRuntimeCallback runtimeCallback,
									ExprExternVarSCCallback staticCheckCallback)
{
	ExprExternVarCategory* newcat = exprExternVarCategoryCreate();

	newcat->category = allocAddString(category);
	newcat->loadtimeCallback = loadtimeCallback;
	newcat->runtimeCallback = runtimeCallback;
	newcat->staticCheckCallback = staticCheckCallback;

	if(!context->externVarCallbacks)
		context->externVarCallbacks = stashTableCreateWithStringKeys(1, StashDefault);	

	if(!newcat->externVarOverrides)
		newcat->externVarOverrides = stashTableCreateWithStringKeys(1, StashDefault);

	devassert(!stashFindPointer(context->externVarCallbacks, category, NULL));
	stashAddPointer(context->externVarCallbacks, newcat->category, newcat, true);
}

ExprFuncReturnVal exprContextGetExternVar(ExprContext* context, const char* category, const char* name, MultiValType expectedType, MultiVal* outval, ACMD_EXPR_ERRSTRING_STATIC errString)
{
	ExprContext* externContext = context;
	ExprExternVarCategory* callbackInfo = NULL;
	MultiVal* answer = NULL;

	while(externContext)
	{
		// do lookup from extern var callback table on context
		stashFindPointer(externContext->externVarCallbacks, category, &callbackInfo);
		if(callbackInfo)
			break;
		externContext = context->parent;
	}

	devassert(callbackInfo); // this should've been caught at load time, what's going on?
	devassertmsg(callbackInfo->runtimeCallback, "Should have a runtime function, this expression is getting called at run time...");

	if(!callbackInfo->externVarOverrides || !stashFindPointer(callbackInfo->externVarOverrides, name, &answer) || !answer)
		answer = callbackInfo->runtimeCallback(externContext, name);

	if(!answer)
	{
		if(errString)
			*errString = "Extern var not found";
		return ExprFuncReturnError;
	}
	else if(expectedType == MULTIOP_LOC_STRING)
	{
		if(MULTI_FLAGLESS_TYPE(answer->type) == MULTI_STRING || MULTI_FLAGLESS_TYPE(answer->type) == MULTIOP_LOC_STRING)
		{
			outval->type = MULTIOP_LOC_STRING;
			outval->str = answer->str;
		}
		else
		{
			if(errString)
				*errString = "Invalid loc string specified";
			return ExprFuncReturnError;
		}
	}
	else if(MULTI_FLAGLESS_TYPE(answer->type) != MULTI_FLAGLESS_TYPE(expectedType))
	{
		if(errString)
			*errString = "Extern var returned non-expected type";
		return ExprFuncReturnError;
	}
	else
	{
		outval->type = answer->type;
		outval->intval = answer->intval;
	}

	return ExprFuncReturnFinished;
}

ExprFuncReturnVal exprContextOverrideExternVar(ExprContext* context, const char* category, const char* name, MultiVal* val)
{
	ExprContext* externContext = context;
	ExprExternVarCategory* catInfo = NULL;
	MultiVal *prevVal = NULL;

	while(externContext)
	{
		// do lookup from extern var callback table on context
		stashFindPointer(externContext->externVarCallbacks, category, &catInfo);
		if(catInfo)
			break;
		externContext = context->parent;
	}

	if(!catInfo || !catInfo->externVarOverrides)
		return ExprFuncReturnFinished;

	if(stashFindPointer(catInfo->externVarOverrides, name, &prevVal) && prevVal)
		MultiValDestroy(prevVal);

	stashAddPointer(catInfo->externVarOverrides, name, val, true);

	return ExprFuncReturnFinished;
}

ExprFuncReturnVal exprContextClearOverrideExternVar(ExprContext* context, const char* category, const char* name, MultiVal **valOut)
{
	ExprContext* externContext = context;
	ExprExternVarCategory* catInfo = NULL;
	MultiVal *prevVal = NULL;

	while(externContext)
	{
		// do lookup from extern var callback table on context
		stashFindPointer(externContext->externVarCallbacks, category, &catInfo);
		if(catInfo)
			break;
		externContext = context->parent;
	}

	if(!catInfo || !catInfo->externVarOverrides)
		return ExprFuncReturnFinished;

	stashRemovePointer(catInfo->externVarOverrides, name, valOut);

	return ExprFuncReturnFinished;
}

int exprContextExternVarSC(ExprContext* context, const char* category, const char* name, const char* enabler,
						   const char* limitName, MultiValType desiredType, const char* scType, 
						   ExprStaticCheckCategory scTypeCategory, ACMD_EXPR_ERRSTRING_STATIC errString)
{
	// call load time function to store the tag for editors etc
	ExprExternVarCategory* callbackInfo;
	int valid = false;

	stashFindPointer(context->externVarCallbacks, category, &callbackInfo);

	if(!callbackInfo)
	{
		*errString = "Trying to do lookup in unknown extern var category";
		return ExprFuncReturnError;
	}

	if(callbackInfo->loadtimeCallback)
	{
		void* userData = NULL;
		valid = callbackInfo->loadtimeCallback(context, category, name, enabler, limitName, desiredType, &userData);

		// add userdata to the return value's static checking info
		if(valid && callbackInfo->staticCheckCallback)
		{
			if(scType)
				callbackInfo->staticCheckCallback(context, userData, scType, scTypeCategory);
			else
				exprContextSetStaticCheckCallback(context, callbackInfo->staticCheckCallback, userData, true);
		}

		if(!valid)
		{
			*errString = "Couldn't register extern var";
			return ExprFuncReturnError;
		}
	}

	return ExprFuncReturnFinished;
}

void exprContextSetStaticCheckCallback(ExprContext* context, ExprExternVarSCCallback staticCheckCallback, void* userData, int skipNormalCheck)
{
	context->curSCInfo->staticCheckCallback = staticCheckCallback;
	context->curSCInfo->externVarSCUserData = userData;
	context->curSCInfo->skipSCTypeCheck = !!skipNormalCheck;
}

void exprContextAddStaticDefineIntAsVars(ExprContext *context, StaticDefineInt *table, const char *pchPrefix)
{
	int i;
	char *pchName = NULL;
	char **ppchDefineKeys = NULL;
	S32 *piDefineValues = NULL;

	estrStackCreate(&pchName);
	DefineFillAllKeysAndValues(table,&ppchDefineKeys,&piDefineValues);
	
	for(i=eaSize(&ppchDefineKeys)-1; i>=0; i--)
	{
		if(pchPrefix)
			estrCopy2(&pchName,pchPrefix);
		else
			estrClear(&pchName);
		estrAppend2(&pchName,ppchDefineKeys[i]);
		// This throws a bunch of crap into the string pool because the stash tables don't deep copy the keys
		exprContextSetIntVar(context,pchName,piDefineValues[i]);
	}
	
	eaiDestroy(&piDefineValues);
	eaDestroy(&ppchDefineKeys);
	estrDestroy(&pchName);
}

void exprContextSetIntVarEx(ExprContext *context, const char *name, S64 val, int* handleptr MEM_DBG_PARMS)
{
	MultiVal multiVal;
	multiVal.type = MULTI_INT;
	multiVal.intval = val;
	exprContextSetSimpleVarEx(context, name, &multiVal, handleptr MEM_DBG_PARMS_CALL);
}

void exprContextSetFloatVarEx(ExprContext *context, const char *name, F64 val, int* handleptr MEM_DBG_PARMS)
{
	MultiVal multiVal;
	multiVal.type = MULTI_FLOAT;
	multiVal.floatval = val;
	exprContextSetSimpleVarEx(context, name, &multiVal, handleptr MEM_DBG_PARMS_CALL);
}

void exprContextSetStringVarEx(ExprContext *context, const char *name, const char *value, int* handleptr MEM_DBG_PARMS)
{
	MultiVal multiVal = {0};
	MultiValReferenceString(&multiVal, value);
	exprContextSetSimpleVarEx(context, name, &multiVal, handleptr MEM_DBG_PARMS_CALL);
}

static char* staticVars[] = {
	"Activation",
	"AdjustLevel",
	"Application",
	"ClickableTracker",
	"Contact",
	"Context",
	"CurNode",
	"CurrentState",
	"curStateTracker",
	"dependencyVal",
	"Encounter",
	"Encounter2",
	"EncounterDef",
	"EncounterTemplate",
	"Entity",
	"Forever",
	"GenData",
	"GenInstanceColumn",
	"GenInstanceColumnCount",
	"GenInstanceCount",
	"GenInstanceData",
	"GenInstanceNumber",
	"GenInstanceRow",
	"GenInstanceRowCount",
	"HP",
	"HPMax",
	"iLevel"
	"INTERNAL_LayerFSM",
	"IsDisabled",
	"IsSelectable",
	"IsVisible",
	"me",
	"Mission",
	"MissionClickable",
	"MissionDef",
	"Mod",
	"ModDef",
	"MouseX",
	"MouseY",
	"MouseZ",
	"NewTreeLevel",
	"NumTeamMembers",
	"ParentValue",
	"pathOldValue",
	"Player",
	"Power",
	"PowerDef",
	"PowerMax",
	"PowerVolumeData",
	"Prediction",
	"RowData",
	"Self",
	"Source",
	"SpawnLocation",
	"TableValue",
	"Target",
	"TargetEnt",
	"TeamHP",
	"TeamHPMax",
	"Volume",
};

static StashTable exprStaticVarTable = NULL;

void exprRegisterStaticVar(const char* str, int idx)
{
	const char* allocStr = allocAddString(str);

	if(!exprStaticVarTable)
		exprStaticVarTable = stashTableCreateWithStringKeys(EXPR_NUM_STATIC_VARS, StashDefault);

	devassertmsg(!stashFindInt(exprStaticVarTable, allocStr, NULL), "already registered %s as static var");

	if(afterLoad)
	{
		Errorf("Cannot add %s because the CRC has already been computed, please add it to the AUTO_RUN instead", str);
		return;
	}

	devassertmsg(idx < EXPR_NUM_STATIC_VARS,
		"More static vars than the context has space, please update EXPR_NUM_STATIC_VARS");

	if(!stashAddInt(exprStaticVarTable, allocStr, idx, false))
	{
		assert(0);
	}
}

int exprGetStaticVarIndex(const char* str)
{
	int idx;

	if(str && stashFindInt(exprStaticVarTable, str, &idx))
		return idx;

	return STATIC_VAR_INVALID;
}

const char* exprGetStaticVarName(int index)
{
	return staticVars[index];
}

void exprSetStaticVarCRC(void)
{
	ParserBinRegisterDepValue("ExprStaticVarCRC", exprGetStaticVarCRC());
}

AUTO_RUN_EARLY;
void exprRegisterAllStaticVars(void)
{
	int i;

	for(i = 0; i < ARRAY_SIZE(staticVars); i++)
		exprRegisterStaticVar(staticVars[i], i);

	afterLoad = true;
	exprSetStaticVarCRC();
}

/*
int exprGetStaticVarCRCHelper(StashElement elem)
{
	const char* str = stashElementGetStringKey(elem);
	int val = stashElementGetInt(elem);
	cryptAdler32Update_IgnoreCase(str, (int)strlen(str));
	cryptAdler32Update((U8*)&val, sizeof(int));
	return 1;
}
*/

U32 exprGetStaticVarCRC(void)
{
	static U32 crc = 0;

	if(!crc)
	{
		int i;

		cryptAdler32Init();
		for (i = 0; i < ARRAY_SIZE(staticVars); i++)
		{
			if (i > 0)
			{
				ANALYSIS_ASSUME(i > 1);
#pragma warning(suppress:6200) // /analyze not accepting the ANALYSIS_ASSUME above, for whatever reason
				devassertmsgf(stricmp(staticVars[i-1], staticVars[i]) < 0, "Static Var list must be in alphabetical order (\"%s\" is after \"%s\")", staticVars[i], staticVars[i-1]);
			}

			cryptAdler32Update_IgnoreCase(staticVars[i], (int)strlen(staticVars[i]));
		}
		//stashForEachElement(exprStaticVarTable, exprGetStaticVarCRCHelper);
		crc = cryptAdler32Final();
	}

	return crc;
}

#define STATICVAR_WATERMARK_MASK 0xffffff00
#define STATICVAR_WATERMARK ('r' << 24) + ('v' << 16) + ('p' << 8)

ExprVarEntry* exprContextGetLocalVar(SA_PARAM_NN_VALID ExprContext* context,
									 SA_PARAM_NN_STR const char* name,
									 SA_PARAM_OP_VALID int* handleptr MEM_DBG_PARMS)
{
	int index;

	if(handleptr && (*handleptr & STATICVAR_WATERMARK_MASK) == STATICVAR_WATERMARK)
		index = *handleptr & ~STATICVAR_WATERMARK_MASK;
	else
	{
		index = exprGetStaticVarIndex(name);
		if(handleptr)
			*handleptr = index | STATICVAR_WATERMARK;
	}

	if(index != STATIC_VAR_INVALID)
	{
		devassert(index < ARRAY_SIZE(context->staticVars));
		if(!context->staticVars[index])
			context->staticVars[index] = exprVarEntryCreate(name MEM_DBG_PARMS_CALL);
		return context->staticVars[index];
	}
	else
	{
		ExprVarEntry* found;

		if(!context->varTable)
			context->varTable = stashTableCreateAddress(16);

		if(stashAddressFindPointer(context->varTable, name, &found))
			return found;
		else
		{
			found = exprVarEntryCreate(name MEM_DBG_PARMS_CALL);
			stashAddressAddPointer(context->varTable, name, found, false);
			return found;
		}
	}
}

void exprContextSetSimpleVarEx(ExprContext* context, const char* name, MultiVal* val, int* handleptr MEM_DBG_PARMS)
{
	ExprVarEntry* var = exprContextGetLocalVar(context, name, handleptr MEM_DBG_PARMS_CALL);

	exprVarEntryChangeSimple(context, var, val);
}

void exprContextSetPointerVarEx(ExprContext* context, const char* name, void* ptr,
								ParseTable* table, int allowVarAccess, int allowObjPath,
								int* handleptr MEM_DBG_PARMS)
{
	ExprVarEntry* var = exprContextGetLocalVar(context, name, handleptr MEM_DBG_PARMS_CALL);

	exprVarEntryChangePointer(context, var, ptr, table, allowVarAccess, allowObjPath,
		!!(context->staticCheckFeatures & ExprStaticCheck_AllowTypeChanges));
}

int exprContextRemoveVarPooled(ExprContext* context, const char* name)
{
	ExprVarEntry* found;
	int index = exprGetStaticVarIndex(name);

	if(index != STATIC_VAR_INVALID)
	{
		devassert(index < ARRAY_SIZE(context->staticVars));
		if(context->staticVars[index])
		{
			exprVarEntryDestroy(context->staticVars[index]);
			context->staticVars[index] = NULL;
			return true;
		}
	}
	else if(context->varTable && stashAddressRemovePointer(context->varTable,name,&found))
	{
		exprVarEntryDestroy(found);
		return true;
	}
	return false;
}

void exprContextGetVarsAsEArray(ExprContext* context, ExprVarEntry ***entries)
{
	int i;
	StashTableIterator iter;
	StashElement elem;

	for(i = 0; i < ARRAY_SIZE(context->staticVars); i++)
		if(context->staticVars[i])
			eaPush(entries, context->staticVars[i]);

	stashGetIterator(context->varTable, &iter);
	while(stashGetNextElement(&iter, &elem))
		eaPush(entries, stashElementGetPointer(elem));
}

ExprVarEntry* exprContextGetVarEntryPooled(ExprContext* context, const char* name)
{
	ExprVarEntry* found;
	int index = exprGetStaticVarIndex(name);

	if(index != STATIC_VAR_INVALID)
	{
		devassert(index < ARRAY_SIZE(context->staticVars));
		do 
		{
			if(context->staticVars[index])
				return context->staticVars[index];
		} while(context = context->parent);
	}
	else
	{
		do 
		{
			if(stashAddressFindPointer(context->varTable,name,&found))
				return found;
		} while(context = context->parent);
	}

	return NULL;
}

MultiVal* exprContextGetSimpleVarPooled(ExprContext* context, const char* name)
{
	ExprVarEntry* found = exprContextGetVarEntryPooled(context, name);

	if(found && found->simpleVar.type)
		return &found->simpleVar;

	return NULL;
}

int exprContextHasVarPooled(ExprContext* context, const char* name)
{
	ExprVarEntry* found = exprContextGetVarEntryPooled(context, name);

	return !!found;
}

void* exprContextGetVarPointerUnsafePooled(ExprContext* context, const char* name)
{
	ExprVarEntry* found = exprContextGetVarEntryPooled(context, name);

	if(found && !found->simpleVar.type)
		return found->inptr;

	return NULL;
}

void* exprContextGetVarPointerAndTypePooled(ExprContext* context, const char* name, ParseTable** tableout)
{
	ExprVarEntry* found = exprContextGetVarEntryPooled(context, name);

	if(found && !found->simpleVar.type)
	{
		*tableout = found->table;
		return found->inptr;
	}

	*tableout = NULL;
	return NULL;
}

void* exprContextGetVarPointerPooled(ExprContext* context, const char* name, ParseTable* table)
{
	ExprVarEntry* found = exprContextGetVarEntryPooled(context, name);

	if(found)
	{
		devassertmsg(found->table == table, "Asked for a variable with a different parsetable");
		return found->inptr;
	}

	return NULL;
}

void* exprContextAllocScratchMemory(ExprContext* context, size_t sizeInBytes)
{
	return ScratchStackAlloc(&context->scratch, sizeInBytes);
}

const char *exprContextAllocString(ExprContext* context, const char *pchString)
{
	if (pchString)
	{
		size_t sz = strlen(pchString) + 1;
		char *pchCopy = ScratchStackAlloc(&context->scratch, sz);
		strcpy_s(pchCopy, sz, pchString);
		return pchCopy;
	}
	else
		return "";
}

void exprContextFreeScratchMemory(ExprContext* context, void* ptr)
{
	ScratchStackFree(&context->scratch, ptr);
}

int DEFAULT_LATELINK_exprContextValidatePartitionInfo(Entity* ent, int partition, Entity* oldEnt, int partitionWasSet, int oldPartition)
{
	return true;
}

void exprContextSetSelfPtr(ExprContext* context, Entity* selfPtr)
{
	if(context->partitionIsSet)
		exprContextValidatePartitionInfo(selfPtr, -1, context->selfPtr, context->partitionIsSet, context->partition);
	context->selfPtr = selfPtr;
}

Entity* exprContextGetSelfPtr(ExprContext* context)
{
	if(context->staticCheck)
		return (Entity*)0xbadbeef; // TODO: make SC functions not actually request self pointers eventually

	return context->selfPtr;
}

void exprContextSetPartition(ExprContext* context, int partition)
{
	if(context->selfPtr)
		exprContextValidatePartitionInfo(NULL, partition, context->selfPtr, context->partitionIsSet, context->partition);
	context->partition = partition;
	context->partitionIsSet = true;
}

void exprContextClearPartition(ExprContext* context)
{
	context->partitionIsSet = false;
}

int exprSCPartition = -1;

int exprContextGetPartition(ExprContext* context)
{
	if(context->staticCheck)
	{
		//devassertmsg(context->staticCheckWillHavePartition, "This context is not set up to guarantee a partition at run time, if you didn't get other errors you might not be using ACMD_EXPR_PARTITION");
		return exprSCPartition;
	}

	assertmsg(context->partitionIsSet, "Partition is not set but something's trying to use it");

	return context->partition;
}

void exprContextSetSelfPtrAndPartition(ExprContext* context, Entity* selfPtr, int partition)
{
	exprContextValidatePartitionInfo(selfPtr, partition, context->selfPtr, context->partitionIsSet, context->partition);
	context->selfPtr = selfPtr;
	context->partition = partition;
	context->partitionIsSet = true;
}

void exprContextClearSelfPtrAndPartition(ExprContext* context)
{
	context->selfPtr = NULL;
	context->partitionIsSet = false;
}

void exprContextSetAllowRuntimeSelfPtr(ExprContext* context)
{
	context->staticCheckWillHaveSelfPtr = true;
}

void exprContextSetAllowRuntimePartition(ExprContext* context)
{
	context->staticCheckWillHavePartition = true;
}

void exprContextSetStaticCheckPartition(int partition)
{
	exprSCPartition = partition;
}

WorldScope* exprContextGetScope(ExprContext* context)
{
	return context->scope;
}

void exprContextSetScope(ExprContext* context, WorldScope* scope)
{
	context->scope = scope;
}

void exprContextSetGAD(ExprContext* context, const GameAccountData *pData)
{
	context->pGameAccountData = pData;
}

const GameAccountData * exprContextGetGAD(ExprContext* context)
{
	return context->pGameAccountData;
}

AUTO_CMD_INT(gUseScopedExpr, useScopedExpr) ACMD_CMDLINE;
bool gUseScopedExpr = true;

ParseTable *exprContextGetParseTable(ExprContext *context)
{
	if (context->userTable)
		return context->userTable;
	else
		return NULL;
}

void* exprContextGetUserPtr(ExprContext* context, ParseTable *table)
{
	//!!!! hack just to avoid crash
	if (!context->userTable)
		return NULL;

	if (table == context->userTable)
		return context->userPtr;
	else if (devassertmsg(table && PolyTableContains(table, context->userTable), "Incorrect ParseTable passed to exprContextGetUserPtr"))
		return context->userPtr;
	else
		return NULL;
}

void exprContextSetUserPtr(ExprContext* context, void* userPtr, ParseTable *table)
{
	devassertmsg(table || !userPtr, "You must pass a ParseTable to SetUserPtr.");
	context->userPtr = userPtr;
	context->userTable = table;
}

void exprContextSetUserPtrIsDefault(ExprContext* context, bool on)
{
	context->userPtrIsDefaultIdentifier = on;
}

void exprContextSetSilentErrors(ExprContext* context, int on)
{
	context->silentErrors = !!on;
}

// Allow expressions to reference invalid XPaths without error, by converting them into empty strings.
void exprContextAllowInvalidPaths(ExprContext* context, int on)
{
	context->allowInvalidPaths = !!on;
}

int exprContextCheckStaticError(ExprContext* context)
{
	return context->staticCheckError;
}

void exprContextSetFSMContext(ExprContext* context, FSMContext* fsmContext)
{
	context->fsmContext = fsmContext;
}

FSMContext* exprContextGetFSMContext(ExprContext* context)
{
	return context->fsmContext;
}

void exprContextCleanupPush(ExprContext* context, CommandQueue** queue, ExprLocalData ***localData)
{
	CleanupStackItem *stack = calloc(1, sizeof(CleanupStackItem));

	stack->cleanupQueue = queue;
	stack->localData = localData;

	eaPush(&context->cleanupStack, stack);
}

void exprContextCleanupPop(ExprContext* context)
{
	CleanupStackItem *stack = eaPop(&context->cleanupStack);

	if(stack)
		free(stack);
}

void exprContextGetCleanupCommandQueue(ExprContext *context, CommandQueue **queueOut, ExprLocalData ****localDataOut)
{
	CleanupStackItem *stack = eaTail(&context->cleanupStack);

	if(!stack)
		return;

	if(stack->cleanupQueue && !*stack->cleanupQueue)
		*stack->cleanupQueue = CommandQueue_Create(16, false);

	if(stack->localData && !*stack->localData)
		eaCreate(stack->localData);

	if(queueOut)
		*queueOut = stack->cleanupQueue ? *stack->cleanupQueue : NULL;

	if(localDataOut)
		*localDataOut = stack->localData;
}

FSMStateTrackerEntry* exprContextGetCurStateTracker(ExprContext* context)
{
	return context->fsmContext->curTracker;
}

void exprContextSetFuncTable(ExprContext* context, ExprFuncTable* funcTable)
{
	context->funcTable = funcTable;
}

const char* exprContextGetBlameFile(ExprContext* context)
{
	return context->curExpr ? context->curExpr->filename : NULL;
}

void exprContextSetStaticCheck(ExprContext* context, ExprStaticCheckFeatures staticChecks)
{
	context->staticCheckFeatures = staticChecks;
}

Expression* exprContextGetCurExpression(ExprContext* context)
{
	return context->curExpr;
}

void exprContextSetParent(ExprContext* child, ExprContext* parent)
{
	child->parent = parent;
}

bool exprContextLastGenerateWasConstant(ExprContext* context, Expression* expr)
{
	if(expr != context->scResults.scExpr)
	{
		Errorf("You seem to be checking the result of a generate after already running or executing a different expression");
		return false;
	}

	return context->scResults.isConstant;
}
