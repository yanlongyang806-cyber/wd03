#pragma once
GCC_SYSTEM

#include "ExpressionMinimal.h"
#include "stdtypes.h"
#include "MultiVal.h" // for MultiValType (at least until forward declared enums are less confusing to VS2005)
#include "StringCache.h" // For alloc add string

typedef struct CommandQueue			CommandQueue;
typedef struct Entity				Entity;
typedef struct ExprContext			ExprContext;
typedef struct Expression			Expression;
typedef struct ExprFuncDesc			ExprFuncDesc;
typedef struct ExprFuncTable		ExprFuncTable;
typedef struct FSMContext			FSMContext;
typedef struct ExprLocalData		ExprLocalData;
typedef struct FSMStateTrackerEntry	FSMStateTrackerEntry;
typedef struct StashTableImp*		StashTable;
typedef struct WorldScope		    WorldScope;
typedef struct GameAccountData		GameAccountData;

#define EXPR_MAX_ALLOWED_ARGS 12 // should probably equal CMDMAXARGS
#define EXPR_MAX_ALLOWED_TAGS 12 // this should line up with MAX_COMMAND_SETS in structparser
#define EXPR_MAX_TAGS 5
#define EXPR_VARNAME_LEN 64

extern ParseTable parse_Expression[];
#define TYPE_parse_Expression Expression
extern ParseTable parse_Expression_StructParam[];
#define TYPE_parse_Expression_StructParam Expression_StructParam
extern ParseTable parse_ExpressionList[];
#define TYPE_parse_ExpressionList ExpressionList

extern ParseTable parse_ExprContext[];
#define TYPE_parse_ExprContext ExprContext

extern ParseTable parse_FSMStateTrackerEntry[];
#define TYPE_parse_FSMStateTrackerEntry FSMStateTrackerEntry

// If you need expression static typechecking to be less strict, pass
// some of these flags in.
typedef enum ExprStaticCheckFeatures
{
	ExprStaticCheck_Default = 0,
	ExprStaticCheck_AllowTypeChanges = 1 << 1,
} ExprStaticCheckFeatures;

typedef ExprFuncReturnVal (*ExprFunctionPtr)(MultiVal** args, MultiVal* retval,
											 ExprContext* context, char** errEString);

typedef int (*ExprResolveLocationCallback)(ExprContext* context, const char* location,
										   Mat4 matOut, const char* blamefile);
typedef int (*ExprArgumentTypeStaticCheckFunction)(ExprContext* context, MultiVal* arg, char** blameStr);

AUTO_STRUCT;
typedef struct ExpressionList
{
	Expression **expressions; AST(NAME(ExpressionBlock) REDUNDANT_STRUCT(Expression, parse_Expression_StructParam))
	const char *filename; AST(CURRENTFILE)
}ExpressionList;

#define exprGenerate(expr, context) exprGenerate_dbg(expr, context MEM_DBG_PARMS_INIT)
int exprGenerate_dbg(Expression* expr, ExprContext* context MEM_DBG_PARMS);
#define exprGenerateFromString(expr, context, exprStr, filename) exprGenerateFromString_dbg(expr, context, exprStr, filename MEM_DBG_PARMS_INIT)
int exprGenerateFromString_dbg(Expression* expr, ExprContext* context, const char* exprStr, const char* filename MEM_DBG_PARMS);

#define exprEvaluate(expr, context, answer) exprEvaluate_dbg(expr, context, answer MEM_DBG_PARMS_INIT)
void exprEvaluate_dbg(Expression* expr, ExprContext* context, MultiVal* answer MEM_DBG_PARMS);
// when using this, please actually free your multival afterwards

#define exprEvaluateDeepCopyAnswer(expr, context, answer) exprEvaluateDeepCopyAnswer_dbg(expr, context, answer MEM_DBG_PARMS_INIT)
void exprEvaluateDeepCopyAnswer_dbg(Expression* expr, ExprContext* context, MultiVal* answer MEM_DBG_PARMS);

#define exprEvaluateWithFuncTable(expr, context, funcTable, answer) exprEvaluateWithFuncTable_dbg(expr, context, funcTable, answer MEM_DBG_PARMS_INIT)
void exprEvaluateWithFuncTable_dbg(Expression* expr, ExprContext* context, ExprFuncTable* funcTable, MultiVal* answer MEM_DBG_PARMS);

#define exprEvaluateDebugStep(expr, context, answer) exprEvaluateDebugStep_dbg(expr, context, answer MEM_DBG_PARMS_INIT)
void exprEvaluateDebugStep_dbg(Expression* expr, ExprContext* context, MultiVal* answer MEM_DBG_PARMS);
#define exprEvaluateStaticCheck(expr, context, answer) exprEvaluateStaticCheck_dbg(expr, context, answer MEM_DBG_PARMS_INIT)
int exprEvaluateStaticCheck_dbg(Expression* expr, ExprContext* context, MultiVal* answer MEM_DBG_PARMS);
#define exprEvaluateSubExpr(subExpr, origContext, subExprContext, answer, deepCopyAnswer) exprEvaluateSubExpr_dbg(subExpr, origContext, subExprContext, answer, deepCopyAnswer MEM_DBG_PARMS_INIT)
void exprEvaluateSubExpr_dbg(ACMD_EXPR_SUBEXPR_IN subExpr, ExprContext* origContext, ExprContext* subExprContext, MultiVal* answer, int deepCopyAnswer MEM_DBG_PARMS);
#define exprEvaluateSubExprStaticCheck(subExpr, origContext, subExprContext, answer, deepCopyAnswer) exprEvaluateSubExprStaticCheck_dbg(subExpr, origContext, subExprContext, answer, deepCopyAnswer MEM_DBG_PARMS_INIT)
void exprEvaluateSubExprStaticCheck_dbg(ACMD_EXPR_SUBEXPR_IN subExpr, ExprContext* origContext, ExprContext* subExprContext, MultiVal* answer, int deepCopyAnswer MEM_DBG_PARMS);

//takes a string and evaluates it and returns the int to evaluate. 
#define exprEvaluateRawString(str) exprEvaluateRawString_dbg(str MEM_DBG_PARMS_INIT)
int exprEvaluateRawString_dbg(const char* str MEM_DBG_PARMS);

// evaluates a string, returns a float, no asserts, should be safe for cmdParse usage
#define exprEvaluateRawStringSafe(str) exprEvaluateRawStringSafe_dbg(str MEM_DBG_PARMS_INIT)
F32 exprEvaluateRawStringSafe_dbg(const char* str MEM_DBG_PARMS);

//evaluates a raw string which contains object paths off of a "me" (ie, "me.hp"), and provides the
//object and tpi of me. If bCacheContext is true, then the expression context will be internally cached,
//eating up some RAM but meaning that subsequent calls with the same string but different objects
//will be faster (in that case, the expression string passed in should be pooled)
#define exprEvaluateRawStringWithObject(str, name, obj, tpi, cacheContext) exprEvaluateRawStringWithObject_dbg(str, name, obj, tpi, cacheContext MEM_DBG_PARMS_INIT)
int exprEvaluateRawStringWithObject_dbg(const char* str, const char* name, void* obj, ParseTable* tpi, bool cacheContext MEM_DBG_PARMS);

// Only allowed to call this function with specific signoff from your lead. It exists purely
// to let us add error checking for expression evaluation inside transactions without getting
// a lot of errors that we don't have time to fix right away
#define exprEvaluateTolerateInvalidUsage(expr, context, answer) exprEvaluateTolerateInvalidUsage_dbg(expr, context, answer MEM_DBG_PARMS_INIT)
void exprEvaluateTolerateInvalidUsage_dbg(Expression* expr, ExprContext* context, MultiVal* answer MEM_DBG_PARMS);

F32 exprGetCost(Expression* expr);

Expression* exprCreate(void);
Expression* exprCreateFromString(const char* str, const char* filename);
void exprPrintDecompiledVersion(Expression *pExpr, char** estr, U32 printWarning);
void exprCopy(Expression *dest, const Expression *source);
Expression* exprClone(Expression *source);
void exprDestroy(Expression* expr);
int exprCompare(Expression* expr1, Expression* expr2);
void exprSetOrigStrNoFilename(Expression* expr, const char* origStr);
void exprSetOrigStr(Expression* expr, const char* origStr, const char* filename);
int exprMatchesString(Expression *expr, const char* matchStr);
int exprIsEmpty(Expression* expr);
int exprIsZero(Expression* expr);
int exprIsSimpleNumber(Expression* expr, F64* numOut);
int exprUsesNOT(Expression* expr);
void exprClean(Expression *expr);

int exprIsLocationString(const char* location);
int exprMat4FromLocationString(ExprContext* context, const char* location, Mat4 pos,
							   int getConstantOnly, const char* blamefile);
void exprRegisterLocationPrefix(const char* prefix, ExprResolveLocationCallback callback, int constant);

// It's perfectly valid to pass in a NULL for the checking function in cases where you don't want
// to specify a function (server check functions on the client, using the tag for expr editor
// choosers only, etc)
void exprRegisterStaticCheckArgumentType(const char* argtype, const char* argtypemsg, ExprArgumentTypeStaticCheckFunction callback);
const char* exprGetStaticCheckArgumentMsgKey(MultiValType type, const char* argtype, const char* argname);
ExprArgumentTypeStaticCheckFunction exprGetArgumentTypeStaticCheckFunction(const char* argtype);

void exprContextSetStaticCheck(ExprContext* context, ExprStaticCheckFeatures eStaticChecks);

// Returns whether the last expression generated will always return the same value
bool exprContextLastGenerateWasConstant(ExprContext* context, Expression* expr);

// Used for external systems checking function args they get through ExternVars
int exprStaticCheckWithType(ExprContext* context, MultiVal* val, const char* scType, ExprStaticCheckCategory scTypeCategory, const char* blamefile);

void exprRegisterFunctionTable(ExprFuncDesc* exprTable, size_t numEntries, bool bFunctionWontBeExecuted);
void exprUnregisterFunctionTable(ExprFuncDesc* exprTable, size_t numEntries);

char* exprGetCompleteString(Expression* expr);
void exprGetCompleteStringEstr(Expression* expr, char** estr);
void exprAppendStringLines(Expression* expr, const char *str);

#define QuickGetFloatSafe(val) ((val)->type == MULTI_FLOAT ? (val)->floatval : \
	(val)->type == MULTI_INT ? (val)->intval : 0.0f)

#define QuickGetIntSafe(val) ((val)->type == MULTI_INT ? (val)->intval : 0)

// Expression context stuff
#define exprContextCreate() exprContextCreateEx(0 MEM_DBG_PARMS_INIT)
#define exprContextCreateWithEmptyFunctionTable() exprContextCreateWithEmptyFunctionTableEx(0 MEM_DBG_PARMS_INIT)

ExprContext* exprContextCreateEx(size_t scratch_size MEM_DBG_PARMS);
ExprContext* exprContextCreateWithEmptyFunctionTableEx(size_t scratch_size MEM_DBG_PARMS);
void exprContextDestroy(ExprContext* context);
void exprContextClear(ExprContext* context);

ExprContext* exprContextGetGlobalContext();

Entity*** exprContextGetNewEntArray(ExprContext* context);
void exprContextClearEntArray(ExprContext* context, Entity*** entarray);
void exprContextClearAllEntArrays(ExprContext* context);

// Fills the context with int vars named from the define table, with an optional prefix
void exprContextAddStaticDefineIntAsVars(ExprContext* context, StaticDefineInt* table, const char* pchPrefix);

// macros to track memory for Ex versions below
#define exprContextSetIntVar(context, name, val) \
	exprContextSetIntVarEx(context, allocAddString(name), val, NULL MEM_DBG_PARMS_INIT)
#define exprContextSetFloatVar(context, name, val) \
	exprContextSetFloatVarEx(context, allocAddString(name), val, NULL MEM_DBG_PARMS_INIT)
#define exprContextSetStringVar(context, name, val) \
	exprContextSetStringVarEx(context, allocAddString(name), val, NULL MEM_DBG_PARMS_INIT)
#define exprContextSetSimpleVar(context, name, val) \
	exprContextSetSimpleVarEx(context, allocAddString(name), val, NULL MEM_DBG_PARMS_INIT)
#define exprContextSetPointerVar(context, name, ptr, table, allowVarAccess, allowObjPath) \
	exprContextSetPointerVarEx(context, allocAddString(name), ptr, table, allowVarAccess, \
		allowObjPath, NULL MEM_DBG_PARMS_INIT)

#define exprContextSetPointerVarPooled(context, allocname, ptr, table, allowVarAccess, allowObjPath) \
	exprContextSetPointerVarEx(context, allocname, ptr, table, allowVarAccess, \
		allowObjPath, NULL MEM_DBG_PARMS_INIT)

#define exprContextSetIntVarPooledCached(context, allocname, val, handleptr) \
	exprContextSetIntVarEx(context, allocname, val, handleptr MEM_DBG_PARMS_INIT)
#define exprContextSetFloatVarPooledCached(context, allocname, val, handleptr) \
	exprContextSetFloatVarEx(context, allocname, val, handleptr MEM_DBG_PARMS_INIT)
#define exprContextSetStringVarPooledCached(context, allocname, val, handleptr) \
	exprContextSetStringVarEx(context, allocname, val, handleptr MEM_DBG_PARMS_INIT)
#define exprContextSetSimpleVarPooledCached(context, allocname, val, handleptr) \
	exprContextSetSimpleVarEx(context, allocname, val, handleptr MEM_DBG_PARMS_INIT)
#define exprContextSetPointerVarPooledCached(context, allocname, ptr, table, allowVarAccess, allowObjPath, handleptr) \
	exprContextSetPointerVarEx(context, allocname, ptr, table, allowVarAccess, \
		allowObjPath, handleptr MEM_DBG_PARMS_INIT)

// Creates a MultiVal for you, and adds it
void exprContextSetIntVarEx(ExprContext* context, const char* name, S64 val,
							int* handleptr MEM_DBG_PARMS);
void exprContextSetFloatVarEx(ExprContext* context, const char* name, F64 val,
							  int* handleptr MEM_DBG_PARMS);
void exprContextSetStringVarEx(ExprContext *context, const char *name, const char *value,
							   int* handleptr MEM_DBG_PARMS);

// Adds the specified MultiVal to the var table.
void exprContextSetSimpleVarEx(ExprContext* context, const char* name, MultiVal* val,
							   int* handleptr MEM_DBG_PARMS);
void exprContextSetPointerVarEx(ExprContext* context, const char* name, void* ptr,
								ParseTable* table, int allowVarAccess, int allowObjPath,
								int* handleptr MEM_DBG_PARMS);

#define exprContextGetSimpleVar(context, name) exprContextGetSimpleVarPooled(context, allocAddString(name))
#define exprContextGetVarPointerUnsafe(context, name) exprContextGetVarPointerUnsafePooled(context, allocAddString(name))
#define exprContextGetVarPointer(context, name, table) exprContextGetVarPointerPooled(context, allocAddString(name), table)
#define exprContextGetVarPointerAndType(context, name, tableout) exprContextGetVarPointerAndTypePooled(context, allocAddString(name), tableout)
#define exprContextRemoveVar(context, name) exprContextRemoveVarPooled(context, allocAddString(name))

#define exprContextHasVar(context, name) exprContextHasVarPooled(context, allocAddString(name))

// Returns a multival, or NULL if it doesn't exist
MultiVal* exprContextGetSimpleVarPooled(ExprContext* context, const char* name);

void* exprContextGetVarPointerUnsafePooled(ExprContext* context, const char* str);
void* exprContextGetVarPointerPooled(ExprContext* context, const char* str, ParseTable* table);
void* exprContextGetVarPointerAndTypePooled(ExprContext* context, const char* name, ParseTable** tableout);

// Remove the var with the given name from the context. Returns true if removed
int exprContextRemoveVarPooled(ExprContext* context, const char* name);

int exprContextHasVarPooled(ExprContext* context, const char* name);

typedef MultiVal* (*ExprExternVarRuntimeCallback)(ExprContext* context, const char* varName);
typedef int (*ExprExternVarLoadtimeCallback)(ExprContext* context, const char* category, 
											const char* varName, const char* enablerVar, 
											const char* limitName, MultiValType type, void** userDataOut);
typedef void (*ExprExternVarSCCallback)(ExprContext* context, void* userData, const char* scType, int scTypeCategory);

typedef struct ExprExternVarCategory
{
	const char* category;
	StashTable externVarOverrides;
	ExprExternVarRuntimeCallback runtimeCallback;
	ExprExternVarLoadtimeCallback loadtimeCallback;
	ExprExternVarSCCallback staticCheckCallback;
}ExprExternVarCategory;

// the ExternGet*Var functions call these callbacks with the var they want to get based on the category
void exprContextAddExternVarCategory(ExprContext* context, const char* category,
			ExprExternVarLoadtimeCallback loadtimeCallback,
			ExprExternVarRuntimeCallback runtimeCallback,
			ExprExternVarSCCallback staticCheckCallback);

ExprFuncReturnVal exprContextGetExternVar(ExprContext* context, const char* category,
						const char* name, MultiValType expectedType, MultiVal* outval,
						ACMD_EXPR_ERRSTRING_STATIC errString);

int exprContextExternVarSC(ExprContext* context, const char* category, const char* name, const char* enablerVar,
						const char* limitName, MultiValType desiredType, const char* scType, 
						ExprStaticCheckCategory scTypeCategory, ACMD_EXPR_ERRSTRING_STATIC errString);

ExprFuncReturnVal exprContextOverrideExternVar(ExprContext* context, const char* category, 
											   const char* name, MultiVal* val);

ExprFuncReturnVal exprContextClearOverrideExternVar(ExprContext* context, const char* category, 
													const char* name, MultiVal **valOut);

void exprContextSetStaticCheckCallback(ExprContext* context,
			ExprExternVarSCCallback staticCheckCallback, void* userData, int skipNormalCheck);

// Get memory for temporary use. This memory will be automatically freed when the expression
// finishes executing (providing you're not using continuations)
void* exprContextAllocScratchMemory(ExprContext* context, size_t sizeInBytes);
void exprContextFreeScratchMemory(ExprContext* context, void* ptr);
const char *exprContextAllocString(ExprContext* context, const char *pchString);

// Gets the context's entity pointer
Entity* exprContextGetSelfPtr(ExprContext* context);
void exprContextSetSelfPtr(ExprContext* context, Entity* selfPtr);

int exprContextGetPartition(ExprContext* context);
void exprContextSetPartition(ExprContext* context, int partition);
void exprContextClearPartition(ExprContext* context);

void exprContextSetSelfPtrAndPartition(ExprContext* context, Entity* selfPtr, int partition);
void exprContextClearSelfPtrAndPartition(ExprContext* context);

// Set what partition to return during static checking
void exprContextSetStaticCheckPartition(int partition);

// Functions to tell the static checker you will specify stuff at runtime
void exprContextSetAllowRuntimeSelfPtr(ExprContext* context);
void exprContextSetAllowRuntimePartition(ExprContext* context);

// Gets the context's scope
WorldScope* exprContextGetScope(ExprContext* context);
void exprContextSetScope(ExprContext* context, WorldScope* scope);
extern bool gUseScopedExpr;

// set gad data for expression context
void exprContextSetGAD(ExprContext* context, const GameAccountData *pData);
// get gad data for context
const GameAccountData * exprContextGetGAD(ExprContext* context);


// Sets the FSM context needed for some state machine related expression functions
void exprContextSetFSMContext(ExprContext* context, FSMContext* fsmContext);
FSMContext* exprContextGetFSMContext(ExprContext* context);

// Sets the CommandQueue to be used for some expression functions to cleanup side effects
//  Uses ** so it can be created on demand in the expression, which is done in the Get func
void exprContextCleanupPush(ExprContext* context, CommandQueue** queue, ExprLocalData*** data);
void exprContextCleanupPop(ExprContext* context);
void exprContextGetCleanupCommandQueue(ExprContext *context, CommandQueue** queueOut, ExprLocalData ****dataOut);

//Get the parseTable of the UserPtr.
ParseTable *exprContextGetParseTable(ExprContext *context);

// Useful for "selfs" that aren't Entities.
void* exprContextGetUserPtr(SA_PARAM_NN_VALID ExprContext* context, ParseTable *table);
void exprContextSetUserPtr(ExprContext* context, void* userPtr, ParseTable *table);

// When set to on, expressions root anonymous objectpaths on the userPtr object.
void exprContextSetUserPtrIsDefault(ExprContext* context, bool on);

// Return the expression currently being processed
Expression* exprContextGetCurExpression(ExprContext* context);

// Gets the FSM's current state tracker entry
FSMStateTrackerEntry* exprContextGetCurStateTracker(ExprContext* context);

// Turn "silent errors" functionality on or off
void exprContextSetSilentErrors(ExprContext* context, int on);

// Allow expressions to reference invalid XPaths without error, by converting them into empty strings.
void exprContextAllowInvalidPaths(ExprContext* context, int on);

// Returns whether the last generated expression had any static checking errors
int exprContextCheckStaticError(ExprContext* context);

#define exprContextCreateFunctionTable(pHumanReadableName) exprContextCreateFunctionTableEx(pHumanReadableName, MEM_DBG_PARMS_INIT_VOID)
// Create a stash table for use by the expression context
ExprFuncTable* exprContextCreateFunctionTableEx(char *pHumanReadableName, MEM_DBG_PARMS_VOID);

// Add all functions that have a certain tag on them to the function table specified
void exprContextAddFuncsToTableByTag(ExprFuncTable* funcTable, const char* tag);

// Set the function table for this expr context (which controls which functions are allowed for use)
void exprContextSetFuncTable(ExprContext* context, ExprFuncTable* funcTable);

// Set the parent context of this context.
void exprContextSetParent(ExprContext* child, ExprContext* parent);

// If mv is an ExprVarEntry, return the ParseTable and pointer associated with it,
// and true. Otherwise NULL them and return false.
int exprMultiValGetPointer(CMultiVal* mv, ParseTable** table, void** ptr);

// NOTE: Don't use this for normal error reporting, use ACMD_EXPR_ERRSTRING and
// ACMD_EXPR_ERRSTRING_STATIC instead.
// This gets the file to blame from the context (needed for things that send a filename down
// to the client for blaming)
const char* exprContextGetBlameFile(ExprContext* context);

// Return the identifier name associated with this MultiVal. Works for
// MULTIOP_IDENTIFIER, MULTIOP_STATICVAR, and MULTI_NP_POINTER.
const char *exprMultiValGetVarName(CMultiVal* mv, ExprContext* context);

// Returns function indices that match func (or all funcs if func==NULL)
//  Useful for following functions
void exprFindFunctions(Expression* expr, const char* func, int** eaFuncsOut);

const MultiVal* exprFindFuncParam(Expression* expr, int funcIndex, int paramIndex);

// This is not for general use, please at least ask Raoul before using this
int exprIsNonGenerated(Expression* expr);

#define EXPRFUNCHELPER_SHOULD_EXCLUDE_FROM_ENTARRAY_FLAGS (ENTITYFLAG_DEAD | ENTITYFLAG_DONOTDRAW | ENTITYFLAG_DONOTSEND | ENTITYFLAG_IGNORE | ENTITYFLAG_UNTARGETABLE | ENTITYFLAG_UNSELECTABLE)

LATELINK;
int exprFuncHelperShouldExcludeFromEntArray(Entity* e);

LATELINK;
int exprFuncHelperShouldIncludeInEntArray(Entity* e, int getDead, int getAll);

LATELINK;
U32 exprFuncHelperGetEntRef(Entity* be);

LATELINK;
Entity* exprFuncHelperBaseEntFromEntityRef(int iPartitionIdx, U32 ref);

LATELINK;
int exprFuncHelperEntIsAlive(Entity* be);

extern char* exprCurAutoTrans;



//use this macro in a very particular case... you have an expression string, and you're going to repeatedly
//want to evaluate that string with single user variable, which will always be of the same type, but a different
//struct every time. Ie, every time you launch a GS on a machine, you want to check whether the machine matches some condition
//
//NOT threadsafe
#define exprSimpleEvaluateWithVariable(exprString, tpi, pStruct, iOutVal)	\
{	static ExprContext *spContext = NULL;									\
	static char *spLastTimeString = NULL;									\
	static Expression *spExpression = NULL;									\
	static ExprFuncTable* spFuncTable;										\
	MultiVal answer = {0};													\
																			\
	if (!spContext)															\
	{																		\
		spContext = exprContextCreate();									\
		spFuncTable = exprContextCreateFunctionTable(NULL);					\
		exprContextAddFuncsToTableByTag(spFuncTable, "util");				\
		exprContextSetFuncTable(spContext, spFuncTable);					\
		exprContextSetUserPtrIsDefault(spContext, true);					\
	}																		\
																			\
	exprContextSetUserPtr(spContext, pStruct, tpi);							\
																			\
	if (!spExpression)														\
	{																		\
		spExpression = exprCreate();										\
		exprGenerateFromString(spExpression, spContext, exprString, NULL);	\
		spLastTimeString = strdup(exprString);								\
	}																		\
	else if (stricmp_safe(exprString, spLastTimeString) != 0)				\
	{																		\
		exprDestroy(spExpression);											\
		spExpression = exprCreate();										\
		exprGenerateFromString(spExpression, spContext, exprString, NULL);	\
		SAFE_FREE(spLastTimeString);										\
		spLastTimeString = strdup(spMachineExpressionForGSLaunchAlert);		\
	}																		\
																			\
	exprEvaluate(spExpression, spContext, &answer);							\
																			\
	if (answer.type == MULTI_INT)											\
		iOutVal = QuickGetInt(&answer);										\
	else																	\
		iOutVal = 0;														\
}