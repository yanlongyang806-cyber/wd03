/***************************************************************************
*     Copyright (c) 2005-2011, Cryptic Studios
*     All Rights Reserved
*     Confidential Property of Cryptic Studios
***************************************************************************/

#ifndef SMARTADD_H_
#define SMARTADD_H_

#include "structDefines.h"	// For StaticDefineInt

typedef struct Expression Expression;

extern DefineContext *g_DefineSmartAddAutoTags;
AUTO_ENUM AEN_EXTEND_WITH_DYNLIST(g_DefineSmartAddAutoTags);
typedef enum SmartAdAutoTag
{
	kSmartAutoTag_NONE = -1,
}SmartAdAutoTag;

extern DefineContext *g_pDefineSmartAddDisplayTags;
AUTO_ENUM AEN_EXTEND_WITH_DYNLIST(g_pDefineSmartAddDisplayTags);
typedef enum DisplayTags
{
	kDisplayTag_NONE = -1,
}DisplayTags;

AUTO_STRUCT;
typedef struct SmartAdDisplayTag
{
	const char *pchKey;			AST(STRUCTPARAM)
	F32 fPriority;				AST(DEFAULT(0.0))
}SmartAdDisplayTag;

AUTO_STRUCT;
typedef struct SmartAdDisplayTags
{
	SmartAdDisplayTag **ppTags;	AST(NAME(DisplayTag))
}SmartAdDisplayTags;

AUTO_STRUCT;
typedef struct SmartAdDef
{
	const char *pchKey;			AST(KEY STRUCTPARAM)
	SmartAdAutoTag eTag;
	DisplayTags	eDisplayTag;	AST(DEFAULT(kDisplayTag_NONE))

	Expression *pIncludeExpr;	AST(NAME(IncludeBlock) REDUNDANT_STRUCT(IncludeExpr, parse_Expression_StructParam) LATEBIND)
	Expression *pExcludeExpr;	AST(NAME(ExcludeBlock) REDUNDANT_STRUCT(ExcludeExpr, parse_Expression_StructParam) LATEBIND)

	bool bIsValidAd;			NO_AST
	bool bIncludeInPick;		NO_AST
	F32 fWeight;				AST(DEFAULT(1.0))
	F32 fPriority;				NO_AST
}SmartAdDef;

AUTO_STRUCT;
typedef struct SmartAds
{
	SmartAdDef **ppDefs;		AST(NAME(SmartAd))
}SmartAds;

void gclSmartAds_EvaulateAds(void);
SmartAdAutoTag gclSmartAds_GetQuickAd();
bool gclSmartAds_IsSetUp(void);

#endif