#pragma once
GCC_SYSTEM

#include "Expression.h"
#include "MultiVal.h"
#include "StashTable.h"

typedef struct FSMContext	FSMContext;
typedef struct ScratchStack	ScratchStack;

#define EXPR_STACK_SIZE 32
#define EXPR_MAX_ENTARRAY 5

#define EXPR_NUM_STATIC_VARS 64


#define EXPR_DEBUG_INFO

// enables extra error checking
#define EXPRESSION_STRICT

AUTO_STRUCT AST_THREADSAFE_MEMPOOL;
typedef struct ExprLine
{
	char* descStr;						AST(NAME("DescStr", "DescStr:", "Description:"))
	char* origStr;						AST(STRUCTPARAM, NAME("OrigStr", "OrigStr:"))
}ExprLine;

AUTO_STRUCT AST_THREADSAFE_MEMPOOL;
typedef struct Expression
{
	MultiVal* postfixEArray;			AST(NAME("") )
	ExprLine** lines;					AST(NAME("Statement", "lines") EDIT_ONLY)
	const char* filename;				AST(CURRENTFILE EDIT_ONLY)
	F32 cost;							AST(NAME("cost") EDIT_ONLY)
}Expression; 

typedef struct ExprSCInfo
{
	ExprVarEntry* lookupEntry;
	ExprExternVarSCCallback staticCheckCallback;
	void* externVarSCUserData;

	U8 isConstant : 1;
	U8 skipSCTypeCheck : 1;
}ExprSCInfo;

typedef struct CleanupStackItem {
	CommandQueue **cleanupQueue;
	ExprLocalData ***localData;
}CleanupStackItem;

typedef struct ExprStaticCheckResults {
	Expression* scExpr; // mostly here to make sure people don't get mismatched sc info
	U32 isConstant : 1;
}ExprStaticCheckResults;

AUTO_STRUCT;
typedef struct ExprContext
{
	MultiVal stack[EXPR_STACK_SIZE];				NO_AST
	int stackidx;									NO_AST

	int instrPtr;									NO_AST

	ExprSCInfo* scInfoStack;						NO_AST
	ExprSCInfo* curSCInfo;							NO_AST

	char* input;
	char* stringResult;								AST(ESTRING) // Used to store string return values

	Entity* selfPtr;								AST(LATEBIND UNOWNED)
	U32 partition;									NO_AST
	FSMContext* fsmContext;							AST(UNOWNED)
	CleanupStackItem **cleanupStack;				NO_AST
	WorldScope* scope;								NO_AST
	void* userPtr;									NO_AST
	ParseTable* userTable;							NO_AST

	Entity** entArrays[EXPR_MAX_ENTARRAY];			NO_AST
	int entArrayInUse[EXPR_MAX_ENTARRAY];			NO_AST

	ScratchStack* scratch;							NO_AST

	StashTable varTable;							NO_AST
	ExprVarEntry* staticVars[EXPR_NUM_STATIC_VARS];	NO_AST

	StashTable externVarTable;						NO_AST

	ExprFuncTable* funcTable;						NO_AST
	StashTable externVarCallbacks;					NO_AST

	StashTable constantTable;						NO_AST

	Expression* curExpr;							NO_AST
		
	U32 numErrors;									NO_AST
	
	U32 staticCheck						: 1;		NO_AST // Whether the current evaluation is a static check
	U32 staticCheckError				: 1;		NO_AST
	U32 staticCheckWillHaveSelfPtr		: 1;		NO_AST
	U32 staticCheckWillHavePartition	: 1;		NO_AST
	U32 silentErrors					: 1;		NO_AST
	U32 allowInvalidPaths				: 1;		NO_AST
	U32 userPtrIsDefaultIdentifier		: 1;		NO_AST
	U32 partitionIsSet					: 1;		NO_AST
	ExprStaticCheckFeatures staticCheckFeatures;	NO_AST // What to static check during exprGenerate.

	// variable/function lookups that fail in this context will be tried in the parent.
	ExprContext* parent;							AST(UNOWNED STRUCT_NORECURSE)

	ExprStaticCheckResults scResults;				NO_AST

	const GameAccountData	*pGameAccountData;		NO_AST


#ifdef EXPR_DEBUG_INFO
	// debug info
	const char* exprFile; // case mandated by stringcache due to struct with same name
	int exprLine;

	const char* exprContextFile;
	int exprContextLine;
#endif
}ExprContext;

#ifdef EXPR_DEBUG_INFO
	#define EXPR_DEBUG_INFO_PRINTF_STR		" called from %s:%d (exprcontext from %s:%d)"
	#define EXPR_DEBUG_INFO_PRINTF_PARAMS	context->exprFile, context->exprLine, context->exprContextFile, context->exprContextLine
#else
	#define EXPR_DEBUG_INFO_PRINTF_STR
	#define EXPR_DEBUG_INFO_PRINTF_PARAMS
#endif

// used for expression editor... at some point these should be removed and replaced
// with proper interface functions that hide the ExprLine stuff, but no time right now
ExprLine *exprLineCreate(const char *desc, const char *orig);
void exprLineSetDescStr(ExprLine *line, const char *desc);
void exprLineSetOrigStr(ExprLine *line, const char *orig);
void exprLineDestroy(ExprLine *line);

// Gets the string that this index represents in the static var table
const char* exprGetStaticVarName(int index);

// Prints out the postfix earray of multivals to an estring
void exprPrintMultiVals(Expression* expr, char** estr);

// Called to generate an error message during expression evaluation
char* exprGenerateErrorMsg(ExprContext* context, bool noReport, bool skipExprString, bool isFileError, char* msg, ...);

#define STATIC_VAR_INVALID 0xff

// Gets the index for this identifier if it's in the static var table
int exprGetStaticVarIndex(const char* str);

// Returns the CRC of the static var table for expression dependencies
U32 exprGetStaticVarCRC(void);

// Makes sure this is a valid expression function static check type
void exprCheckStaticCheckType(const char* scType, ExprStaticCheckCategory scTypeCategory);

// this is required to be in a header file because it's a latelink function but it's not actually
// needed by anything except ExpressionEvaluate.c
LATELINK;
int exprEvaluateRuntimeEntArrayFromLookupEntry(ParseTable* table, void* ptr, Entity** outEnt);

LATELINK;
int exprEvaluateRuntimeEntFromEntArray(Entity** ents, ParseTable* table, Entity** entOut);

// Checks whether the entity and partition match
LATELINK;
int exprContextValidatePartitionInfo(Entity* ent, int partition, Entity* oldEnt, int partitionWasSet, int oldPartition);