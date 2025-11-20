/***************************************************************************



***************************************************************************/

#pragma once
GCC_SYSTEM

#include "AutoTransHelperDefs.h"
#include "GlobalTypeEnum.h"

#define MAX_ARGS_SINGLE_ATR_FUNC 16

typedef struct AutoTransContainer AutoTransContainer;
typedef struct StaticDefineInt StaticDefineInt;

typedef enum
{
	ATR_ARG_NONE,

	ATR_ARG_INT,
	ATR_ARG_INT_PTR,

	ATR_ARG_INT64,
	ATR_ARG_INT64_PTR,

	ATR_ARG_FLOAT,
	ATR_ARG_FLOAT_PTR,

	ATR_ARG_STRING,
	ATR_ARG_STRUCT,
	ATR_ARG_CONST_STRUCT,

	ATR_ARG_CONTAINER,
	ATR_ARG_CONTAINER_EARRAY,
} enumAutoTransArgType;

AUTO_ENUM;
typedef enum
{
	ATR_LOCK_NORMAL, //a non-indexed-lock
	ATR_LOCK_ARRAY_OPS, //Only lock the keys, because we're adding/subtracting something
	ATR_LOCK_INDEXED_NULLISOK,
	ATR_LOCK_INDEXED_FAILONNULL,

	ATR_LOCK_ARRAY_OPS_SPECIAL, //something like eaSize. If applied to the root of an earray of containers
		//passed into an ATR func, does nothing. Otherwise is ATR_LOCK_ARRAY_OPS. Fixed up at fixup time into
		//one or the other.
} enumLockType;

AUTO_ENUM;
typedef enum
{
	ATR_INDEX_LITERAL_INT,
	ATR_INDEX_LITERAL_STRING,
	ATR_INDEX_SIMPLE_ARG,
} enumEarrayIndexType;

typedef struct ATRArgDef
{
	enumAutoTransArgType eArgType;
	ParseTable *pParseTable;
	char *pArgName;
} ATRArgDef;


typedef enumTransactionOutcome ATRWrapperFunction(ATR_ARGS);

typedef struct ATRFuncCallSimpleArg 
{
	int iArgIndex;
	enumEarrayIndexType eArgType;
	int iVal;
	char *pSVal;
} ATRFuncCallSimpleArg;

typedef struct ATRFuncCallContainerArg
{
	int iParentArgIndex; //which argument in the calling function this is
	int iArgIndex; //which argument in the called function this is
	char *pDerefString;
} ATRFuncCallContainerArg;


typedef struct ATRFunctionCallDef 
{
	char *pFuncName;
	int iLineNum;
	ATRFuncCallSimpleArg *pSimpleArgs;
	ATRFuncCallContainerArg *pContainerArgs;
} ATRFunctionCallDef;





typedef struct ATREarrayUseDef
{
	int iContainerArgIndex; //the index of the container arg being indexed into
	char *pContainerDerefString; //the deref string off of that arg
	enumLockType eLockType;
	enumEarrayIndexType eIndexType; 
	int iVal; //for literal ints, the int value. For simple args, the arg index
	char *pSVal; //for literal strings, the string value
	int iLineNum;
} ATREarrayUseDef;

typedef struct ATRStaticDereference
{
	char *pDerefString;
	enumLockType eLockType;
	int iLineNum;
} ATRStaticDereference;


typedef struct ATRSimpleArgDef
{
	char *pArgName;
	int iArgIndex;
} ATRSimpleArgDef;


AUTO_STRUCT;
typedef struct ATRFixedUpLock
{
	char *pDerefString; AST(ESTRING)
	enumLockType eLockType;
	char *pComment;
} ATRFixedUpLock;

#define ATRFIXEDUPEARRAYUSE_NO_STATICDEFINE ((void*)((intptr_t)0x01))
AUTO_STRUCT;
typedef struct ATRFixedUpEarrayUse
{
	char *pContainerDerefString; AST(ESTRING)//the deref string off of that arg
	enumLockType eLockType;
	enumEarrayIndexType eIndexType; 
	int iVal; //for literal ints, the int value. For simple args, the arg index
	char *pSVal; AST(UNOWNED) //for literal strings, the string value
	char *pComment; AST(USERFLAG(TOK_USEROPTIONBIT_1))

	//we want to know whether this is an earray that is indexed by an int field that is an enum... but 
	//we want to cache that information, so if we've looked it up and it doesn't exist, set it to
	//ATRFIXEDUPEARRAYUSE_NO_STATICDEFINE
	StaticDefineInt *pStaticDefineForKey; NO_AST
} ATRFixedUpEarrayUse;

AUTO_STRUCT;
typedef struct ATRContainerArgDef
{
	char *pArgName;
	int iArgIndex;
	bool bAllowFullLock;
	char *pExpectedLocks; //if non-NULL, a single string with comma separated locks, ie ".foo,.bar,.wakka.wappa"
	ATRStaticDereference *pStaticDerefs; NO_AST
	bool bFixedUp; NO_AST
	ATRFixedUpLock **ppLocks;
	ATRFixedUpEarrayUse **ppEarrayUses;
	ParseTable *pParseTable; NO_AST
} ATRContainerArgDef;


AUTO_STRUCT;
typedef struct ATR_FuncDef
{
	char *pFuncName; AST(KEY)
	char *pFileName;

	char *pSuspiciousFunctionCalls; NO_AST//comma-separated list of all functions called
		//inside this AUTO_TRANS that StructParser couldn't verify were OK

	ATRWrapperFunction *pWrapperFunc; NO_AST //if NULL, this must be a helper func

	ATRArgDef *pArgs; NO_AST //only used for non-helper funcs. Somewhat redundant, since
		//information about their args also appears in the simple and container arg lists,

	ATRSimpleArgDef *pSimpleArgs; NO_AST
	ATRContainerArgDef *pContainerArgs; NO_AST
	ATREarrayUseDef *pStaticEarrayUses; NO_AST
	ATRFunctionCallDef *pFunctionCalls; NO_AST

	bool bDoesReturnLogging; NO_AST
	bool bHasBeenFixedUp; NO_AST

	//this earray is created at the last moment for purposes of servermonitoring
	ATRContainerArgDef **ppContainerArgsEarray_ServerMonitoringOnly; AST(NAME(Args))

	//if you're puzzled why bDoesReturnLogging is true, it's because of at least one helper
	//function, we stick at least one such name here
	char *pHelperFuncWhichReturnsLogging; AST(POOL_STRING)

	AST_COMMAND("Get ATR_LOCKS string", "GetATRLocksString $FIELD(FuncName)")
} ATR_FuncDef;

typedef struct AutoTransContainer
{
	bool bIsArray;
	int iTransArgNum;
	GlobalType containerType;
	
	//for non-array
	ContainerID containerID;

	//for arrays (ea32)
	ContainerID *pContainerIDs;

	char **lockedFields;
	int *lockTypes;
	bool bLockAllFields;
} AutoTransContainer;

int GetATRArg_Int(int iIndex);
int *GetATRArg_IntPtr(int iIndex);
S64 GetATRArg_Int64(int iIndex);
S64 *GetATRArg_Int64Ptr(int iIndex);
float GetATRArg_Float(int iIndex);
float *GetATRArg_FloatPtr(int iIndex);
char *GetATRArg_String(int iIndex);
void *GetATRArg_Container(int iIndex);
void *GetATRArg_Struct(int iIndex);
void *GetATRArg_ContainerEArray(int iIndex);

void SetATRArg_Int(int iIndex, int iVal);
void SetATRArg_IntPtr(int iIndex, int *piVal);
void SetATRArg_Int64(int iIndex, S64 iVal);
void SetATRArg_Int64Ptr(int iIndex, S64 *piVal);
void SetATRArg_Float(int iIndex, float fVal);
void SetATRArg_FloatPtr(int iIndex, float *pfVal);
void SetATRArg_String(int iIndex, char *pString);
void SetATRArg_Container(int iIndex, void *pContainer);
void SetATRArg_Struct(int iIndex, void *pStruct);
void SetATRArg_ContainerEArray(int iIndex, void *ppContainerEArray);

ATR_FuncDef *FindATRFuncDef(const char *name);
void RegisterATRFuncDef(ATR_FuncDef *pFuncDef);
void RegisterSimpleATRHelper(char *pName, char *pFuncsCalled);

typedef void ATRIdentifierFixupFunc(void);
void RegisterATRIdentifierFixupFunc(ATRIdentifierFixupFunc *pFunc);
void ATR_CallIdentifierFixupFuncs(void);

ATRContainerArgDef *FindContainerArgDefByIndex(ATR_FuncDef *pFunc, int iContainerArgIndex);

void ATR_DoLateInitialization(void);


//if you wish to check whether one of the containers passed into an AUTO_TRANS function is NULL, you must
//use these special macros, which AUTO_TRANS parsing recognizes as safe-with-no-side-effects
#define ISNULL(x) ( (x) == NULL )
#define NONNULL(x) ( (x) != NULL )

