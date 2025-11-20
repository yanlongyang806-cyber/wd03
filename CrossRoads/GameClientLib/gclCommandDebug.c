#include "earray.h"
#include "EString.h"
#include "StringCache.h"
#include "referencesystem.h"
#include "utils.h"
#include "ExpressionFunc.h"
#include "structDefines.h"
#include "fileutil.h"

#include "GfxConsole.h"
#include "GfxTexturesPublic.h"
#include "GfxTexAtlas.h"

#include "UITextureAssembly.h"

AUTO_RUN_ANON(memBudgetAddMapping(__FILE__, BUDGET_EngineMisc););

void exprApropos(ACMD_SENTENCE pchFilter);

// Prints out information on an expression 
AUTO_COMMAND ACMD_CATEGORY(Debug) ACMD_NAME(man);
void exprMan(/*ACMD_NAMELIST(globalFuncTable, STASHTABLE)*/ ACMD_SENTENCE pchExpr)
{
	ExprFuncDesc* pFuncDesc = NULL;

	stashAddressFindPointer(globalFuncTable, allocFindString(pchExpr), &pFuncDesc);
	if (pFuncDesc)
	{
		int i;
		char* estrSignature = NULL;
		const char* pchType = "void";

		if (pFuncDesc->returnType.type == MULTI_NP_POINTER)
			pchType = pFuncDesc->returnType.ptrTypeName;
		else if (pFuncDesc->returnType.type == MULTI_CMULTI)
			pchType = "MultiVal";
		else if (pFuncDesc->returnType.type != MULTI_NONE && pFuncDesc->returnType.type != MULTIOP_NP_STACKPTR)
			pchType = MultiValTypeToReadableString(pFuncDesc->returnType.type);

		estrAppend2(&estrSignature, pchType);
		estrConcatChar(&estrSignature, ' ');
		estrAppend2(&estrSignature, pFuncDesc->funcName);
		estrConcatChar(&estrSignature, '(');
		for (i = 0; i < ARRAY_SIZE_CHECKED(pFuncDesc->args) && i < pFuncDesc->argc; i++)
		{
			if (i > 0) 
				estrAppend2(&estrSignature, ", ");

			if (pFuncDesc->args[i].type == MULTI_NP_POINTER)
				pchType = pFuncDesc->args[i].ptrTypeName;
			else if (pFuncDesc->args[i].type == MULTI_CMULTI)
				pchType = "MultiVal";
			else if (pFuncDesc->args[i].type != MULTI_NONE)
				pchType = MultiValTypeToReadableString(pFuncDesc->args[i].type);

			estrAppend2(&estrSignature, pchType);
			estrConcatChar(&estrSignature, ' ');
			estrAppend2(&estrSignature, pFuncDesc->args[i].name);
		}
		estrConcatChar(&estrSignature, ')');
		conPrintf("%s\n", estrSignature);
		estrDestroy(&estrSignature);
	}
	else if (pchExpr && !strnicmp(pchExpr, "-k", 2))
	{
		pchExpr += 2;
		while (*pchExpr && isspace((unsigned char)*pchExpr))
		{
			pchExpr++;
		}
		exprApropos((char *)pchExpr);
	}
	else
	{
		conPrintf("Invalid expression: %s\n", pchExpr);
	}
}

AUTO_COMMAND ACMD_CATEGORY(Debug) ACMD_NAME(apropos);
void exprApropos(ACMD_SENTENCE pchFilter)
{
	static char *s_apchAllowedTags[] = { "UIGen", "gameutil", "util", "entityutil" };
	char *estrSignature = NULL;
	conPrintf("======================");
	FOR_EACH_IN_STASHTABLE(globalFuncTable, ExprFuncDesc, pFuncDesc);
	{
		const char *pchType = "void";
		bool bHasTag = false;
		S32 i, j;

		if (pchFilter && *pchFilter && !isWildcardMatch(pchFilter, pFuncDesc->funcName, false, false))
		{
			continue;
		}

		estrClear(&estrSignature);

		for (i = 0; i < ARRAY_SIZE_CHECKED(pFuncDesc->tags); i++)
		{
			if (pFuncDesc->tags[i].str)
			{
				if (estrLength(&estrSignature) > 0)
				{
					estrAppend2(&estrSignature, ", ");
				}
				estrAppend2(&estrSignature, pFuncDesc->tags[i].str);

				for (j = 0; !bHasTag && j < ARRAY_SIZE_CHECKED(s_apchAllowedTags); j++)
				{
					bHasTag = !stricmp(pFuncDesc->tags[i].str, s_apchAllowedTags[j]);
				}
			}
		}

		// Simple filter on the tags, just to keep the list size managable
		if (!bHasTag)
		{
			continue;
		}

		if (estrLength(&estrSignature) > 0)
		{
			estrAppend2(&estrSignature, " : ");
		}

		if (pFuncDesc->returnType.type == MULTI_NP_POINTER)
			pchType = pFuncDesc->returnType.ptrTypeName;
		else if (pFuncDesc->returnType.type == MULTI_CMULTI)
			pchType = "MultiVal";
		else if (pFuncDesc->returnType.type != MULTI_NONE)
			pchType = MultiValTypeToReadableString(pFuncDesc->args[i].type);

		estrAppend2(&estrSignature, pchType);
		estrConcatChar(&estrSignature, ' ');
		estrAppend2(&estrSignature, pFuncDesc->funcName);
		estrConcatChar(&estrSignature, '(');
		for (i = 0; i < ARRAY_SIZE_CHECKED(pFuncDesc->args) && i < pFuncDesc->argc; i++)
		{
			if (i > 0) 
				estrAppend2(&estrSignature, ", ");

			if (pFuncDesc->args[i].type == MULTI_NP_POINTER)
				pchType = pFuncDesc->args[i].ptrTypeName;
			else if (pFuncDesc->args[i].type == MULTI_CMULTI)
				pchType = "MultiVal";
			else if (pFuncDesc->args[i].type != MULTI_NONE && pFuncDesc->args[i].type != MULTIOP_NP_STACKPTR)
				pchType = MultiValTypeToReadableString(pFuncDesc->args[i].type);

			estrAppend2(&estrSignature, pchType);
			estrConcatChar(&estrSignature, ' ');
			estrAppend2(&estrSignature, pFuncDesc->args[i].name);
		}
		estrConcatChar(&estrSignature, ')');

		conPrintf("%s\n", estrSignature);
	}
	FOR_EACH_END;
	estrDestroy(&estrSignature);
}

AUTO_COMMAND ACMD_I_AM_THE_ERROR_FUNCTION_FOR(apropos);
void exprApropos0(void)
{
	exprApropos("");
}

AUTO_COMMAND ACMD_CATEGORY(Debug) ACMD_NAME("TextureSize");
void ui_GenCmdTextureSize(const char* pchTexture)
{
	AtlasTex* pAtlasTex = atlasLoadTexture(pchTexture);
	if (pAtlasTex)
	{
		conPrintf("%s: %dx%d\n", pchTexture, pAtlasTex->width, pAtlasTex->height);
	}
}

AUTO_COMMAND ACMD_CATEGORY(Debug) ACMD_NAME("TextureAssemblySize");
void ui_GenCmdAssemblyTextureSizes(const char* pchName)
{
	UITextureAssembly *pTexas = RefSystem_ReferentFromString("UITextureAssembly", pchName);
	if (pTexas)
	{
		int i;
		for (i = 0; i < eaSize(&pTexas->eaTextures); i++)
		{
			UITextureInstance *pInst = pTexas->eaTextures[i];
			if (pInst->bIsAtlas)
			{
				conPrintf("%s: %dx%d\n", pInst->pchTexture, pInst->pAtlasTexture->width, pInst->pAtlasTexture->height);
			}
			else
			{
				conPrintf("%s: %dx%d\n", pInst->pchTexture, pInst->pTexture->width, pInst->pTexture->height);
			}
		}
	}
}

AUTO_COMMAND ACMD_ACCESSLEVEL(9) ACMD_NAME(PrintEnumValues);
void PrintEnumValues(const char *pchName)
{
	StaticDefineInt *pDefine = FindNamedStaticDefine(pchName);
	if (pDefine)
	{
		const char **eaKeys = NULL;
		S32 *eaiValues = NULL;
		S32 i;
		DefineFillAllKeysAndValues(pDefine, &eaKeys, &eaiValues);
		for (i = 0; i < eaSize(&eaKeys); i++) 
		{
			conPrintf("%s\t\t\t\t\t%d\t(0x%.8X)\n", eaKeys[i], eaiValues[i], eaiValues[i]);
		}
	}
	else
	{
		conPrintf("Did not find StaticDefine \"%s\"\n", pchName);
	}
}
