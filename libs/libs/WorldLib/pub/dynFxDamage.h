#pragma once
GCC_SYSTEM

#include "ReferenceSystem.h"

typedef struct DynFxInfo DynFxInfo;
typedef struct DynFx DynFx;
typedef struct DynFxManager DynFxManager;
typedef struct DynDefineParam DynDefineParam;
typedef struct Entity Entity;

AUTO_STRUCT AST_STARTTOK("") AST_ENDTOK("End");
typedef struct DynFxDamageRangeInfoFxRef
{
	REF_TO(DynFxInfo) hFx;	AST(STRUCTPARAM REQUIRED REFDICT(DynFxInfo))
	F32	fHue;				AST(NAME(Hue))
	DynDefineParam** eaDefineParams; AST(NAME("PassParam"))
} DynFxDamageRangeInfoFxRef;

AUTO_STRUCT AST_STARTTOK("") AST_ENDTOK("End");
typedef struct DynFxDamageRangeInfo
{
	F32 minHitPoints;		AST( NAME(MinHitPoints) )
	F32 maxHitPoints;		AST( NAME(MaxHitPoints) )
	//by default max hit points = 1 and your values are scaled unless you set this bit
	bool useAbsoluteValues; AST( NAME(UseAbsoluteValues) )
	DynFxDamageRangeInfoFxRef** eaFxList; AST( NAME(DamageFx) )
	DynFxDamageRangeInfoFxRef** eaAlphaFxList; AST( NAME(DamageFxAlpha) )
} DynFxDamageRangeInfo;

AUTO_STRUCT AST_STARTTOK("") AST_ENDTOK("End");
typedef struct DynFxDamageInfo
{
	const char* pcName;		AST( STRUCTPARAM KEY POOL_STRING NAME(DynFxDamageInfo) )
	const char* pcFileName;	AST( CURRENTFILE )
	DynFxDamageRangeInfo** eaDamageRanges; AST( NAME(DamageRange) )
} DynFxDamageInfo;

extern DictionaryHandle g_hDamageInfoDict;

void dynFxDamageInfoLoadAll();
bool dynFxDamageInfoReloadedThisFrame();
void dynFxDamageResetReloadedFlag();
