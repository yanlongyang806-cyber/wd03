/***************************************************************************
*     Copyright (c) 2008, Cryptic Studios
*     All Rights Reserved
*     Confidential Property of Cryptic Studios
***************************************************************************/

#include "Character.h"
#include "CharacterClass.h"
#include "CharacterClass_h_ast.h"
#include "cmdparse.h"
#include "Expression.h"
#include "UIGen.h"
#include "StringUtil.h"
#include "qsortG.h"
#include "ResourceManager.h"

#include "gclClass.h"
#include "gclEntity.h"

#include "AutoGen/gclClass_h_ast.h"
#include "AutoGen/GameClientLib_autogen_servercmdwrappers.h"

AUTO_RUN_ANON(memBudgetAddMapping(__FILE__, BUDGET_UISystem););

static CharacterClassList *g_pClasses = NULL;

DictionaryHandle g_hCharacterClassClientDataDict;

AUTO_RUN;
int RegisterClientClassDictionary(void)
{
	g_hCharacterClassClientDataDict = RefSystem_RegisterSelfDefiningDictionary("CharacterClassClientData",false, parse_CharacterClassClientData, false, false, NULL);
	return 1;
}

AUTO_STARTUP(CharacterClassesClient);
void characterclassclientdata_Load(void)
{
	//client-only subset of exported data
	if (IsClient()) 
	{
		resLoadResourcesFromDisk(g_hCharacterClassClientDataDict, NULL, "defs/config/CharacterClassClientData.def", "characterclassclientdata.bin", RESOURCELOAD_SHAREDMEMORY | PARSER_OPTIONALFLAG);
	}
}

// -------------------------------------------------------------
// Commands

AUTO_COMMAND ACMD_CLIENTCMD ACMD_NAME("UpdateClassList") ACMD_PRIVATE ACMD_ACCESSLEVEL(0) ACMD_IFDEF(GAMESERVER);
void classList_UpdateClassList(SA_PARAM_NN_VALID CharacterClassNameList *pClassNameList)
{
	S32 i;

	eaQSort(pClassNameList->ppCharacterClassNameList, strCmp);

	if(!g_pClasses)
		g_pClasses = StructCreate(parse_CharacterClassList);

	eaClearStruct(&g_pClasses->ppCharacterClassList, parse_CharacterClassRef);
	for(i = 0; i < eaSize(&pClassNameList->ppCharacterClassNameList); ++i)
	{
		CharacterClassRef *pNewRef = StructCreate(parse_CharacterClassRef);
		SET_HANDLE_FROM_STRING(g_hCharacterClassDict,
								pClassNameList->ppCharacterClassNameList[i],
								pNewRef->hClass);
		eaPush(&g_pClasses->ppCharacterClassList, pNewRef);
	}

	
}

// -------------------------------------------------------------
// Expressions

AUTO_EXPR_FUNC(UIGen) ACMD_NAME("GetClientClassValueAtLevel");
F32 exprGetClientClassValueAtLevel(const char* pchClassDataKey, S32 level)
{
	CharacterClassClientData* pData = RefSystem_ReferentFromString(g_hCharacterClassClientDataDict, pchClassDataKey);
	return (pData && level < eafSize(&pData->eafData)) ? pData->eafData[level] : -1;
}

AUTO_EXPR_FUNC(entityutil) ACMD_NAME("EntGetClasses");
void exprEntGetClasses(SA_PARAM_NN_VALID UIGen *pGen,
					   SA_PARAM_OP_VALID Entity *pEntity)
{
	static CharacterClass **s_ppClasses = NULL;

	if(g_pClasses && eaSize(&g_pClasses->ppCharacterClassList))
	{
		S32 i;
		eaClear(&s_ppClasses);
		for(i = 0; i < eaSize(&g_pClasses->ppCharacterClassList); i++)
		{
			eaPush(&s_ppClasses, GET_REF(g_pClasses->ppCharacterClassList[i]->hClass));
		}
	}
	ui_GenSetManagedListSafe(pGen, &s_ppClasses, CharacterClass, false);
}

AUTO_EXPR_FUNC(UIGen) ACMD_NAME("EntGetClassByName");
void exprEntGetClassByName(	SA_PARAM_NN_VALID UIGen *pGen,
							SA_PARAM_OP_VALID Entity *pEntity, 
							SA_PARAM_NN_VALID const char *pchClassName)
{
	static CharacterClass **s_ppClasses = NULL;

	if(g_pClasses && eaSize(&g_pClasses->ppCharacterClassList))
	{
		S32 i;
		eaClear(&s_ppClasses);
		for(i = 0; i < eaSize(&g_pClasses->ppCharacterClassList); i++)
		{
			CharacterClass *pClass = GET_REF(g_pClasses->ppCharacterClassList[i]->hClass);
			if (pClass && !stricmp(pClass->pchName, pchClassName))
			{
				eaPush(&s_ppClasses, pClass);
			}
		}
	}
	ui_GenSetManagedListSafe(pGen, &s_ppClasses, CharacterClass, false);
}

AUTO_EXPR_FUNC(UIGen) ACMD_NAME("EntGetNumClasses");
S32 exprEntGetNumClasses()
{
	if(g_pClasses)
	{
		return(eaSize(&g_pClasses->ppCharacterClassList));
	}
	else
		return(-1);
}


#include "AutoGen/gclClass_h_ast.c"