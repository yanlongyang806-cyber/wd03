#pragma once
GCC_SYSTEM

#include "MultiVal.h"

typedef struct Entity		Entity;
typedef struct ExprContext	ExprContext;
typedef const void*			DictionaryHandle;

AUTO_ENUM;
typedef enum ExprStaticCheckCategory
{
	ExprStaticCheckCat_None, 
	ExprStaticCheckCat_Resource,	// Resource dictionary
	ExprStaticCheckCat_Reference,	// Reference dictionary
	ExprStaticCheckCat_Enum,		// AUTO_ENUM
	ExprStaticCheckCat_Custom		// custom registered type
} ExprStaticCheckCategory;

typedef enum ExprFuncReturnVal
{
	ExprFuncReturnFinished,
	ExprFuncReturnError,
	ExprFuncReturnContinuation,	// return value is ignored with continuations
}ExprFuncReturnVal;

typedef struct ExprVarEntry
{
	MultiVal simpleVar;
	const char* name;
	ParseTable* table;
	void* inptr;

	U8 allowObjPath		: 1;
	U8 allowVarAccess	: 1;
	U8 SCMightBeNull	: 1;
	//TODO: implement the following
	/*
	U8 userVar			: 1;// on for things created by "setvar" functions in expressions
	U8 disabled			: 1;// allow disabling to avoid memory allocation/deallocation when reusing contexts
	*/
}ExprVarEntry;

//typedefs for AUTO_COMMAND auto-generation of expression command function wrappers
typedef struct AcmdType_ExprSubExpr
{
	const MultiVal *exprPtr;
	int exprSize;
} AcmdType_ExprSubExpr;

//#defines used for AUTO_COMMAND auto-generation of expression command function wrappers

#define ACMD_EXPR_STATIC_CHECK(checkfunc)	// what function to use at static checking time for validation etc
#define ACMD_EXPR_SC_TYPE(type)
#define ACMD_EXPR_DICT(dictName)
#define ACMD_EXPR_RES_DICT(dictName)
#define ACMD_EXPR_ENUM(enumName)

#define ACMD_EXPR_FUNC_COST(cost)
#define ACMD_EXPR_FUNC_COST_MOVEMENT

#define ACMD_EXPR_SUBEXPR_IN AcmdType_ExprSubExpr*
#define ACMD_EXPR_INT_OUT int*
#define ACMD_EXPR_FLOAT_OUT float*
#define ACMD_EXPR_STRING_OUT const char**
#define ACMD_EXPR_LOC_MAT4_IN Mat4
#define ACMD_EXPR_LOC_MAT4_OUT Mat4
#define ACMD_EXPR_VEC4_OUT Vec4

#define ACMD_EXPR_ERRSTRING char**
#define ACMD_EXPR_ERRSTRING_STATIC char**

#define ACMD_EXPR_ENTARRAY_IN Entity***
#define ACMD_EXPR_ENTARRAY_OUT Entity***
#define ACMD_EXPR_ENTARRAY_IN_OUT Entity***

#define ACMD_EXPR_SELF
#define ACMD_EXPR_PARTITION int

#define QuickGetFloat(val) ((val)->type == MULTI_FLOAT ? (val)->floatval : \
	(val)->type == MULTI_INT ? (val)->intval : devassertmsg(0, "Non-float type accessed as float"))

#define QuickGetInt(val) ((val)->type == MULTI_INT ? (val)->intval : \
	devassertmsg(0, "Non-int type accessed as int"))

// these are duplicated in Expression.h so other people can find them more easily
Entity*** exprContextGetNewEntArray(ExprContext* context);
void exprContextClearEntArray(ExprContext* context, Entity*** entarray);

void* exprContextAllocScratchMemory(ExprContext* context, size_t sizeInBytes);
void exprContextFreeScratchMemory(ExprContext* context, void* ptr);

Entity* exprContextGetSelfPtr(ExprContext* context);
int exprContextGetPartition(ExprContext* context);
