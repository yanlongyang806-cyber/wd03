#include "Error.h"
#include "Message.h"
#include "estring.h"
#include "objPath.h"
#include "StringFormat.h"
#include "Expression.h"
#include "StringCache.h"

AUTO_RUN_ANON(memBudgetAddMapping(__FILE__, BUDGET_GameSystems););

typedef struct ContextAndLang
{
	ExprContext *pContext;
	Language langID;
	char **ppchErr;
	ExprFuncReturnVal eReturn;
	const char *pchKey;
	const char *pchFilename;
} ContextAndLang;

static bool s_bMessageExpressionDebug = true;
// Turn on Errorf dialogs for various things that can go wrong in message expressions.

AUTO_CMD_INT(s_bMessageExpressionDebug, MessageExpressionDebug) ACMD_CATEGORY(Debug);

static void ExprContextFormat(unsigned char **ppchResult, const unsigned char *pchToken, ContextAndLang *pData)
{
	const unsigned char *pchPath = strchr(pchToken, '.');
	ExprContext *pContext = pData->pContext;
	Language langID = pData->langID;
	if (pchToken[0] == 'k' && pchToken[1] == ':')
	{
		if (!strfmt_AppendMessageKey(ppchResult, pchToken + 2, langID))
			ErrorFilenamef(pData->pchFilename, "Invalid message key replacement %s", pchToken);
	}
	else if (pchPath)
	{
		// Object path.
		char *pchVar = NULL;
		char *pchPathResult = NULL;
		void *pStruct;
		ParseTable *pTable;
		ParseTable *pTableOut;
		void *pStructOut;
		S32 iColumn;
		S32 iIndex;
		estrStackCreate(&pchVar);
		estrStackCreate(&pchPathResult);
		estrConcat(&pchVar, pchToken, pchPath - pchToken);
		if ((pStruct = exprContextGetVarPointerAndType(pContext, pchVar, &pTable)) && pTable)
		{
			if (objPathResolveFieldWithResult(pchPath, pTable, pStruct, &pTableOut, &iColumn, &pStructOut, &iIndex, OBJPATHFLAG_TRAVERSEUNOWNED | OBJPATHFLAG_SEARCHNONINDEXED, &pchPathResult))
			{
				if (!langFieldToSimpleEString(langID, pTableOut, iColumn, pStructOut, iIndex, ppchResult, WRITETEXTFLAG_PRETTYPRINT) && s_bMessageExpressionDebug)
					ErrorFilenamef(pData->pchFilename, "Non-string path %s in message %s", pchToken, pData->pchKey);
			}
			else
			{
				estrAppend2(ppchResult, "[Invalid Object Path]");
				if (s_bMessageExpressionDebug)
					ErrorFilenamef(pData->pchFilename, "Invalid object path %s in message %s: %s", pchToken, pData->pchKey, pchPath);
			}
		}
		else if (objPathResolveFieldWithResult(pchToken, pTable, pStruct, &pTableOut, &iColumn, &pStructOut, &iIndex, OBJPATHFLAG_TRAVERSEUNOWNED | OBJPATHFLAG_SEARCHNONINDEXED, &pchPathResult))
		{
			if (!langFieldToSimpleEString(langID, pTableOut, iColumn, pStructOut, iIndex, ppchResult, WRITETEXTFLAG_PRETTYPRINT) && s_bMessageExpressionDebug)
				ErrorFilenamef(pData->pchFilename, "Non-string path %s in message %s", pchToken, pData->pchKey);
		}
		else
		{
			if (s_bMessageExpressionDebug)
				ErrorFilenamef(pData->pchFilename, "Unknown/null variable %s in message key %s", pchToken, pData->pchKey);
			estrAppend2(ppchResult, "(null)");
		}
		estrDestroy(&pchPathResult);
		estrDestroy(&pchVar);
	}
	else
	{
		// Primitive value
		MultiVal *mv = exprContextGetSimpleVar(pContext, pchToken);
		if (mv)
			MultiValToEString(mv, ppchResult);
		else
		{
			if (s_bMessageExpressionDebug)
				ErrorFilenamef(pData->pchFilename, "Unknown/null variable %s in message key %s", pchToken, pData->pchKey);
			estrAppend2(ppchResult, "(null)");
		}
	}
}

void ExprContextFormatCheck(unsigned char **ppchResult, const unsigned char *pchToken, ContextAndLang *pData)
{
	unsigned char *pchVar = NULL;
	const unsigned char *pchPath = strchr(pchToken, '.');
	ExprContext *pContext = pData->pContext;
	Language langID = pData->langID;
	if (pchToken[0] == 'k' && pchToken[1] == ':')
	{
		if (!msgExists(pchToken + 2))
			ErrorFilenamef(pData->pchFilename, "Invalid message key replacement %s", pchToken);
	}
	else
	{
		if (pchPath)
		{
			estrStackCreate(&pchVar);
			estrConcat(&pchVar, pchToken, pchPath - pchToken);
			pchToken = pchVar;
		}
		if (!exprContextHasVar(pData->pContext, pchVar))
		{
			estrPrintf(pData->ppchErr, "Invalid variable name: %s. in key: %s", pchVar, pData->pchKey ? pData->pchKey : "(null)");
			pData->eReturn = ExprFuncReturnError;
		}
	}

	// TODO: This can be made smarter, e.g. checking to make sure object paths are valid for the known types.

	estrDestroy(&pchVar);
}

AUTO_EXPR_FUNC_STATIC_CHECK;
ExprFuncReturnVal exprLangTranslateDefaultCheck(ExprContext *pContext, ACMD_EXPR_STRING_OUT ppchOut, U32 langID, const char *pchKey, const char *pchDefault, ACMD_EXPR_ERRSTRING errEstr)
{
	ContextAndLang context = {pContext, 0, errEstr, ExprFuncReturnFinished, pchKey, exprContextGetBlameFile(pContext)};
	const char *pchMessage = langTranslateMessageKey(langID, pchKey);
	static unsigned char *s_pchOut;
	estrClear(&s_pchOut);
	*ppchOut = "";
	if (pchMessage)
	{
		strfmt_Format(&s_pchOut, pchMessage, ExprContextFormatCheck, &context, NULL, NULL);
		if (s_pchOut)
			*ppchOut = s_pchOut;
		return context.eReturn;
	}
	else if (!(stricmp(pchKey, MULTI_DUMMY_STRING)))
	{
		return ExprFuncReturnFinished;
	}
	else if (quickLoadMessages)
	{
		estrPrintf(&s_pchOut, "[UNTRANSLATED: %s]", pchKey);
		*ppchOut = s_pchOut;
		return ExprFuncReturnFinished;
	}
	else
	{
		estrPrintf(errEstr, "Message key must be either valid or '%s', got '%s'", MULTI_DUMMY_STRING, pchKey);
		return ExprFuncReturnError;
	}
}

AUTO_EXPR_FUNC_STATIC_CHECK;
ExprFuncReturnVal exprLangTranslateCheck(ExprContext *pContext, ACMD_EXPR_STRING_OUT ppchOut, U32 uiLang, const char *pchKey, ACMD_EXPR_ERRSTRING errEstr)
{
	return exprLangTranslateDefaultCheck(pContext, ppchOut, uiLang, pchKey, NULL, errEstr);
}

AUTO_EXPR_FUNC_STATIC_CHECK;
ExprFuncReturnVal exprTranslateCheck(ExprContext *pContext, ACMD_EXPR_STRING_OUT ppchOut, const char *pchKey, ACMD_EXPR_ERRSTRING errEstr)
{
	return exprLangTranslateDefaultCheck(pContext, ppchOut, locGetLanguage(getCurrentLocale()), pchKey, NULL, errEstr);
}

AUTO_EXPR_FUNC_STATIC_CHECK;
ExprFuncReturnVal exprTranslateDefaultCheck(ExprContext *pContext, ACMD_EXPR_STRING_OUT ppchOut, const char *pchKey, const char *pchDefault, ACMD_EXPR_ERRSTRING errEstr)
{
	return exprLangTranslateDefaultCheck(pContext, ppchOut, locGetLanguage(getCurrentLocale()), pchKey, pchDefault, errEstr);
}

void exprLangFormat(unsigned char **ppchResult, const char *pchFormat, ExprContext *pContext, Language langID, const char *pchFilename)
{
	ContextAndLang context = {pContext, langID, NULL, 0, pchFormat, exprContextGetBlameFile(pContext) ? exprContextGetBlameFile(pContext) : pchFilename };
	strfmt_Format(ppchResult, pchFormat, ExprContextFormat, &context, NULL, NULL);
}

void exprFormat(unsigned char **ppchResult, const char *pchFormat, ExprContext *pContext, const char *pchFilename)
{
	exprLangFormat(ppchResult, pchFormat, pContext, locGetLanguage(getCurrentLocale()), pchFilename);
}

void exprLangTranslate(unsigned char **ppchResult, ExprContext *pContext, Language langID, const char *pchKey, const char *pchFilename)
{
	const char *pchFormat = langTranslateMessageKey(langID, pchKey);
	if (pchFormat)
		exprLangFormat(ppchResult, pchFormat, pContext, langID, pchFilename);
	else
		estrConcatf(ppchResult, "[UNTRANSLATED: %s]", pchKey);
}

void exprLangTranslateDefault(unsigned char **ppchResult, ExprContext *pContext, Language langID, const char *pchKey, const char *pchDefaultKey, const char *pchFilename)
{
	const char *pchFormat = langTranslateMessageKey(langID, pchKey);
	if (pchFormat)
	{
		exprLangFormat(ppchResult, pchFormat, pContext, langID, pchFilename);
	}
	else
	{
		exprLangTranslate(ppchResult, pContext, langID, pchDefaultKey, pchFilename);
	}
}

void exprTranslate(unsigned char **ppchResult, ExprContext *pContext, const char *pchKey, const char *pchFilename)
{
	exprLangTranslate(ppchResult, pContext, locGetLanguage(getCurrentLocale()), pchKey, pchFilename);
}

// Translate a message key using the given language, in this expression context,
// with a default back-up message in case the first message isn't available.
AUTO_EXPR_FUNC(UIGen) ACMD_NAME("LangTranslateDefault") ACMD_EXPR_STATIC_CHECK(exprLangTranslateDefaultCheck);
ExprFuncReturnVal exprLangTranslateDefaultFunc(ExprContext *pContext, ACMD_EXPR_STRING_OUT ppchOut, U32 langID, const char *pchKey, const char *pchDefault, ACMD_EXPR_ERRSTRING errEstr)
{
	static unsigned char *s_pchOut;
	estrClear(&s_pchOut);
	exprLangTranslateDefault(&s_pchOut, pContext, langID, pchKey, pchDefault, exprContextGetBlameFile(pContext));
	*ppchOut = s_pchOut;
	return ExprFuncReturnFinished;
}

// Translate a message key in the current locale, in this expression context,
// with a default back-up message in case the first message isn't available.
AUTO_EXPR_FUNC(UIGen) ACMD_NAME("TranslateDefault") ACMD_EXPR_STATIC_CHECK(exprTranslateDefaultCheck);
ExprFuncReturnVal exprTranslateDefaultFunc(ExprContext *pContext, ACMD_EXPR_STRING_OUT ppchOut, const char *pchKey, const char *pchDefault, ACMD_EXPR_ERRSTRING errEstr)
{
	exprLangTranslateDefaultFunc(pContext, ppchOut, locGetLanguage(getCurrentLocale()), pchKey, pchDefault, errEstr);
	return ExprFuncReturnFinished;
}

// Translate a message key using the given language, in this expression context.
AUTO_EXPR_FUNC(UIGen) ACMD_NAME("LangTranslate") ACMD_EXPR_STATIC_CHECK(exprLangTranslateCheck);
ExprFuncReturnVal exprLangTranslateFunc(ExprContext *pContext, ACMD_EXPR_STRING_OUT ppchOut, U32 uiLang, const char *pchKey, ACMD_EXPR_ERRSTRING errEstr)
{
	static unsigned char *s_pchOut;
	estrClear(&s_pchOut);
	exprLangTranslate(&s_pchOut, pContext, uiLang, pchKey, exprContextGetBlameFile(pContext));
	*ppchOut = exprContextAllocString(pContext, s_pchOut);
	return ExprFuncReturnFinished;
}

// Translate a message key in the current locale, in this expression context.
AUTO_EXPR_FUNC(UIGen, util) ACMD_NAME("Translate") ACMD_EXPR_STATIC_CHECK(exprTranslateCheck);
ExprFuncReturnVal exprTranslateFunc(ExprContext *pContext, ACMD_EXPR_STRING_OUT ppchOut, const char *pchKey, ACMD_EXPR_ERRSTRING errEstr)
{
	exprLangTranslateFunc(pContext, ppchOut, locGetLanguage(getCurrentLocale()), pchKey, errEstr);
	return ExprFuncReturnFinished;
}

// Translate a message key in the current locale, in this expression context.
AUTO_EXPR_FUNC(UIGen) ACMD_NAME("TranslateMessage");
ExprFuncReturnVal exprTranslateMessageFunc(ExprContext *pContext, ACMD_EXPR_STRING_OUT ppchOut, SA_PARAM_OP_VALID Message* pMessage, ACMD_EXPR_ERRSTRING errEstr)
{
	static unsigned char *s_pchOut;
	const char *pchFormat = langTranslateMessage(locGetLanguage(getCurrentLocale()), pMessage);
	estrClear(&s_pchOut);
	if (pchFormat)
	{
		exprLangFormat(&s_pchOut, pchFormat, pContext, locGetLanguage(getCurrentLocale()), "");
	}
	else
	{
		estrConcatf(&s_pchOut, "[NULL MESSAGE]");
	}
	*ppchOut = exprContextAllocString(pContext, s_pchOut);
	return ExprFuncReturnFinished;
}


// Translate a message key in the current locale, in this expression context.
AUTO_EXPR_FUNC(UIGen) ACMD_NAME("TranslateDisplayMessage");
ExprFuncReturnVal exprTranslateDisplayMessageFunc(ExprContext *pContext, ACMD_EXPR_STRING_OUT ppchOut, SA_PARAM_OP_VALID DisplayMessage* pDisplayMessage, ACMD_EXPR_ERRSTRING errEstr)
{
	Message *pMessage = pDisplayMessage ? GET_REF(pDisplayMessage->hMessage) : NULL;
	return exprTranslateMessageFunc(pContext, ppchOut, pMessage, errEstr);
}


AUTO_EXPR_FUNC_STATIC_CHECK;
ExprFuncReturnVal exprTranslateStaticDefineIntCheck(ExprContext *pContext, ACMD_EXPR_STRING_OUT ppchOut, const char *pchName, S32 iValue, ACMD_EXPR_ERRSTRING errEstr)
{
	StaticDefineInt *pList = FindNamedStaticDefine(pchName);
	Message *pMessage = pList ? StaticDefineGetMessage(pList, iValue) : NULL;
	if (!pList)
	{
		estrPrintf(errEstr, "Invalid StaticDefine Name: %s", pchName);
		return ExprFuncReturnError;
	}
	if (!pMessage)
	{
		estrPrintf(errEstr, "Invalid %s Value: %d / %s", pchName, iValue, StaticDefineIntRevLookup(pList, iValue));
		return ExprFuncReturnError;
	}
	return ExprFuncReturnFinished;
}

AUTO_EXPR_FUNC_STATIC_CHECK;
ExprFuncReturnVal exprTranslateStaticDefineIntKeyCheck(ExprContext *pContext, ACMD_EXPR_STRING_OUT ppchOut, const char *pchName, const char *pchKey, ACMD_EXPR_ERRSTRING errEstr)
{
	StaticDefineInt *pList = FindNamedStaticDefine(pchName);
	Message *pMessage;
	char achMessageKey[2048];

	sprintf(achMessageKey, "StaticDefine_%s_%s", pchName, pchKey);
	pMessage = RefSystem_ReferentFromString(gMessageDict, achMessageKey);
	if (!pList)
	{
		estrPrintf(errEstr, "Invalid StaticDefine Name: %s", pchName);
		return ExprFuncReturnError;
	}

	if (!stricmp("DummyMultiValString", pchKey))
	{
		return ExprFuncReturnFinished;
	}

	if (!pMessage)
	{
		estrPrintf(errEstr, "Invalid %s Message Key: %s", pchName, achMessageKey);
		return ExprFuncReturnError;
	}
	return ExprFuncReturnFinished;
}

void exprTranslateStaticDefineIntEx(ExprContext * pContext, ACMD_EXPR_STRING_OUT ppchOut, const char* pchName, const char* pchKey, ACMD_EXPR_ERRSTRING errEstr) 
{
	Message *pMessage;
	char achMessageKey[2048];
	sprintf(achMessageKey, "StaticDefine_%s_%s", pchName, pchKey);
	pMessage = RefSystem_ReferentFromString(gMessageDict, achMessageKey);
	if (pMessage)
	{
		exprLangTranslateFunc(pContext, ppchOut, locGetLanguage(getCurrentLocale()), achMessageKey, errEstr);
	}
	if (!pMessage || !*ppchOut)
	{
		*ppchOut = "Invalid StaticDefine/Key";
	}
}

// Translate a StaticDefineInt value.
AUTO_EXPR_FUNC(UIGen) ACMD_NAME("TranslateStaticDefineInt") ACMD_EXPR_STATIC_CHECK(exprTranslateStaticDefineIntCheck);
ExprFuncReturnVal exprTranslateStaticDefineInt(ExprContext *pContext, ACMD_EXPR_STRING_OUT ppchOut, const char *pchName, S32 iValue, ACMD_EXPR_ERRSTRING errEstr)
{
	StaticDefineInt *pList = FindNamedStaticDefine(pchName);
	*ppchOut = "Invalid StaticDefine/Key";
	if (pList)
	{
		const char *pchKey = StaticDefineIntRevLookup(pList, iValue);
		exprTranslateStaticDefineIntEx(pContext, ppchOut, pchName, pchKey, errEstr);
	}
	return ExprFuncReturnFinished;
}

// Translate a StaticDefineInt key.
AUTO_EXPR_FUNC(UIGen) ACMD_NAME("TranslateStaticDefineIntKey") ACMD_EXPR_STATIC_CHECK(exprTranslateStaticDefineIntKeyCheck);
ExprFuncReturnVal exprTranslateStaticDefineIntKey(ExprContext *pContext, ACMD_EXPR_STRING_OUT ppchOut, const char *pchName, const char *pchKey, ACMD_EXPR_ERRSTRING errEstr)
{
	exprTranslateStaticDefineIntEx(pContext, ppchOut, pchName, pchKey, errEstr);
	return ExprFuncReturnFinished;
}
