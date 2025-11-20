#pragma once
GCC_SYSTEM
/***************************************************************************
*     Copyright (c) 2009, Cryptic Studios
*     All Rights Reserved
*     Confidential Property of Cryptic Studios
***************************************************************************/

#include "referencesystem.h"
#include "message.h"
#include "wlGroupPropertyStructs.h"

typedef struct AIAnimList AIAnimList;

AUTO_STRUCT AST_IGNORE(Tags) AST_IGNORE_STRUCT(UGCProperties);
typedef struct AIAnimList
{
	const char **inheritAnims;  AST(ADDNAMES("InheritAnim:"), SIMPLE_INHERITANCE)

	const char* name;		AST(KEY, POOL_STRING, STRUCTPARAM)
	
	const char* animKeyword; AST(POOL_STRING NAME("AnimKeyword"))
	const char** bits;		AST(POOL_STRING, NAME("Bit"))
	U32* bitHandles;		AST(NO_TEXT_SAVE)
	const char** FX;		AST(POOL_STRING, NAME("FX"))
	const char** FlashFX;	AST(POOL_STRING, NAME("FlashFX"))
	U32 manuallyDestroyedFX : 1; AST(NAME("ManuallyDestroyedFX"))
	U32 enableStance		: 1;
	const char* filename;	AST(CURRENTFILE)

	U32 usedFields[1];		AST(USEDFIELD)
}AIAnimList;
extern ParseTable parse_AIAnimList[];
#define TYPE_parse_AIAnimList AIAnimList

extern DictionaryHandle g_AnimListDict;

