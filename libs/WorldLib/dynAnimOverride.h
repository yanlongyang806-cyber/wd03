#pragma once
GCC_SYSTEM

#include "referencesystem.h"
#include "dynBitField.h"

typedef struct DynMove DynMove;

AUTO_STRUCT AST_STARTTOK("") AST_ENDTOK("End");
typedef struct DynAnimOverride
{
	const char*					pcName;				AST(STRUCTPARAM POOL_STRING)
	DynBitFieldStatic			bits;
	CONST_REF_TO(DynMove)		hMove;				AST(NAME(DynMove) REQUIRED NON_NULL_REF)
    U32                         nBones;             NO_AST
	const char**				eaBones;			AST(POOL_STRING)
	const char*					pcSuppressionTag;	AST(POOL_STRING)
	bool						bDisableHandIK;
	F32							fInterpolation;
} DynAnimOverride;

AUTO_STRUCT AST_STARTTOK("") AST_ENDTOK("End");
typedef struct DynAnimOverrideList
{
	const char*					pcName;				AST(KEY POOL_STRING)
	const char*					pcFileName;			AST(CURRENTFILE)
	DynAnimOverride**			eaAnimOverride;		AST(NAME(DynAnimOverride))
} DynAnimOverrideList;

void dynAnimOverrideListLoadAll(void);