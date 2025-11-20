#include "Error.h"
#include "EString.h"
#include "file.h"
#include "objPath.h"
#include "Message.h"
#include "mathutil.h"

#include "StringFormat.h"
#include "AutoGen/StringFormat_h_ast.h"

AUTO_RUN_ANON(memBudgetAddMapping(__FILE__, BUDGET_GameSystems););

#define IsWhitespaceFast(c) (((c) == ' ' || (c) == '\t' || (c) == '\n' || (c) == '\r'))

// Trim whitespace from pchToken and return it.
__forceinline static const unsigned char *TrimWhitespace(unsigned char *pchToken, ptrdiff_t szLength)
{
	while (IsWhitespaceFast(*pchToken))
	{
		pchToken++;
		szLength--;
	}
	while (szLength > 0 && IsWhitespaceFast(pchToken[szLength - 1]))
		szLength--;
	pchToken[szLength] = '\0';
	return pchToken;
}

bool strfmt_NumericCondition(F64 dValue, const unsigned char *pchField, const char *pchFilename)
{
	F64 dOtherValue;
	const unsigned char *pchSep;
	const unsigned char *pchDigit = pchField;
	// don't read the "12" from "Hours12" as the digits.
	while (*pchDigit && (isalnum(*pchDigit) || *pchDigit == '.'))
		pchDigit++;
	while (*pchDigit && !isdigit(*pchDigit) && *pchDigit != '-')
		pchDigit++;
	if (!*pchDigit)
	{
		ErrorFilenamef(pchFilename, "Invalid condition for an integer/float: %s", pchField);
		return true;
	}
	dOtherValue = atof(pchDigit);
	if (pchSep = strstr(pchField, "<="))
		return dValue <= dOtherValue;
	else if (pchSep = strstr(pchField, ">="))
		return dValue >= dOtherValue;
	else if (pchSep = strchr(pchField, '<'))
		return dValue < dOtherValue;
	else if (pchSep = strchr(pchField, '>'))
		return dValue > dOtherValue;
	else if (pchSep = strstr(pchField, "!="))
		return !nearf(dValue, dOtherValue);
	else
		return nearf(dValue, dOtherValue);
}

static bool DefaultCondition(unsigned char **ppchResult, const unsigned char *pchCondition, StrFmtContext *pContext)
{
	char *pchDot = strchr(pchCondition, '.');
	if (!pchDot)
		pchDot = strchr(pchCondition, ' ');
	if (pchDot && pContext)
	{
		StrFmtContainer *pContainer = NULL;
		*pchDot++ = '\0';
		stashFindPointer(pContext->stArgs, pchCondition, &pContainer);
		if (pContainer && pContainer->chType == STRFMT_CODE_INT)
			return strfmt_NumericCondition(pContainer->iValue, pchDot, NULL);
		else if (pContainer && pContainer->chType == STRFMT_CODE_FLOAT)
			return strfmt_NumericCondition(pContainer->fValue, pchDot, NULL);
		else if (pContainer && pContainer->chType == STRFMT_CODE_STRUCT)
			return !!pContainer->pValue;
	}
	return pchCondition && *pchCondition;
}

__forceinline static const char *ParseCondition(
	unsigned char **ppchResult, const char *pchCondition, const char *pchFormat,
	StringFormatTokenFunc cbTokenFmt, UserData pFormatData,
	StringFormatConditionFunc cbCondition, UserData pConditionData)
{
	bool bChoice = cbCondition ? cbCondition(ppchResult, pchCondition, pConditionData) : true;
	const char *pchStart = pchFormat;
	const char *pchPivot = NULL;
	const char *pchEnd = NULL;
	bool bEscape = false;
	S32 iDepth = 0;

	// Parse out pivot point ('|') and end ('}'), handling nested {|}s and escaping.
	do 
	{
		if (!bEscape)
		{
			switch (*pchFormat)
			{
			case STRFMT_TOKEN_ESCAPE:
				bEscape = true;
				break;
			case STRFMT_TOKEN_ALTERNATE:
				if (iDepth == 0)
					pchPivot = pchFormat;
				break;
			case STRFMT_TOKEN_START:
				iDepth++;
				break;
			case STRFMT_TOKEN_END:
				if (iDepth == 0)
					pchEnd = pchFormat;
				else
					iDepth--;
				break;
			}
		}
		else
			bEscape = false;
		pchFormat++;
	} while (*pchFormat && !(pchPivot && pchEnd));

	if (pchEnd && !pchPivot)
		pchPivot = pchEnd;

	if (!(pchPivot && pchEnd && pchEnd >= pchPivot && pchStart <= pchPivot))
	{
		estrConcatf(ppchResult, "{Unterminated Condition: %s}", pchStart);
		return pchEnd ? pchEnd : "}";
	}
	else
	{
		unsigned char achToken[STRFMT_TOKEN_LENGTH];
		const char *pchToken = achToken;
		ptrdiff_t sz = 0;
		achToken[0] = '\0';
		if (bChoice)
		{
			sz = pchPivot - pchStart;
			strncpy(achToken, pchStart, sz);
		}
		else if(pchPivot < pchEnd)
		{
			sz = pchEnd - (pchPivot + 1);
			strncpy(achToken, pchPivot + 1, sz);
		}
		if (*achToken)
			strfmt_Format(ppchResult, TrimWhitespace(achToken, sz), cbTokenFmt, pFormatData, cbCondition, pConditionData);
	}
	return pchEnd;
}

void strfmt_Format(unsigned char **ppchResult, const unsigned char *pchFormat,
				   StringFormatTokenFunc cbTokenFmt, UserData pFormatData,
				   StringFormatConditionFunc cbCondition, UserData pConditionData)
{
	unsigned char ch;
	unsigned char achToken[STRFMT_TOKEN_LENGTH];
	const unsigned char *pchStart = pchFormat; // Used to track the start of the current token/string literal.
	bool bReadingToken = false;
	bool bEscape = false;

	if (!cbCondition)
		cbCondition = DefaultCondition;

	achToken[0] = '\0';
	if (!pchFormat)
	{
		Errorf("Got a null format string, is there a bad message ref somewhere?");
		estrCopy2(ppchResult, "{Null Format String!}");
		return;
	}

	for (ch = *pchFormat; ch = *pchFormat; ++pchFormat)
	{
		if (!pchStart)
			pchStart = pchFormat;
		if (bEscape)
		{
			if (pchStart && pchStart != pchFormat)
			{
				estrConcat(ppchResult, pchStart, pchFormat - pchStart);
			}
			pchStart = NULL;
			bEscape = false;
			estrConcatChar(ppchResult, *pchFormat);
		}
		else if (*pchFormat == STRFMT_TOKEN_ESCAPE)
		{
			if (pchStart && pchStart != pchFormat)
			{
				estrConcat(ppchResult, pchStart, pchFormat - pchStart);
			}
			pchStart = NULL;
			bEscape = true;
		}
		else if (*pchFormat == STRFMT_TOKEN_END)
		{
			if (bReadingToken)
			{
				strncpy(achToken, pchStart, pchFormat - pchStart);
				cbTokenFmt(ppchResult, TrimWhitespace(achToken, pchFormat - pchStart), pFormatData);
				pchStart = NULL;
			}

			bReadingToken = false;
		}
		else if (*pchFormat == STRFMT_TOKEN_CHOICE && bReadingToken && cbCondition)
		{
			strncpy(achToken, pchStart, pchFormat - pchStart);
			pchFormat = ParseCondition(ppchResult, TrimWhitespace(achToken, pchFormat - pchStart), ++pchFormat, cbTokenFmt, pFormatData, cbCondition, pConditionData);
			bReadingToken = false;
			pchStart = NULL;
		}
		else if (*pchFormat == STRFMT_TOKEN_START)
		{
			if (pchStart && pchStart != pchFormat)
			{
				estrConcat(ppchResult, pchStart, pchFormat - pchStart);
			}
			pchStart = NULL;
			if (bReadingToken)
				estrConcatf(ppchResult, "{Unescaped '{' Within Token: %s}", achToken);
			bReadingToken = true;
			achToken[0] = '\0';
		}
	}
	if (bReadingToken && pchStart)
		estrConcatf(ppchResult, "{Unterminated Token: %s}", pchStart);
	else if (pchStart && pchStart != pchFormat)
		estrConcat(ppchResult, pchStart, pchFormat - pchStart);
	if (bEscape)
		estrConcatf(ppchResult, "{EoS Escaped}");
}

void strfmt_AppendContainer(unsigned char **ppchResult, StrFmtContainer *pContainer, const char *pchPath,  StrFmtContext *pContext)
{
	switch (pContainer->chType)
	{
	case STRFMT_CODE_INT:
		estrConcatf(ppchResult, "%d", pContainer->iValue);
	xcase STRFMT_CODE_FLOAT:
		estrConcatf(ppchResult, "%g", pContainer->fValue);
	xcase STRFMT_CODE_STRING:
		estrAppend2(ppchResult, NULL_TO_EMPTY(pContainer->pValue));
	xcase STRFMT_CODE_MESSAGEKEY:
		if (devassertmsg(pContext->bTranslate, "Message keys cannot be passed to non-translated formatters"))
		{
			const char *pchValue = (pContainer->pchValue && *pContainer->pchValue) ? langTranslateMessageKey(pContext->langID, pContainer->pchValue) : "";
			if (pchValue)
				estrAppend2(ppchResult, NULL_TO_EMPTY(pchValue));
			else
			{
				Errorf("Invalid message key %s, string so far is %s", pContainer->pchValue, *ppchResult);
				estrAppend2(ppchResult, NULL_TO_EMPTY(pContainer->pchValue));
			}
		}
	xcase STRFMT_CODE_MESSAGE:
		if (pContainer->pValue)
		{
			Message *pMessage = pContainer->pValue;
			const char *pchValue = langTranslateMessage(pContext->langID, pMessage);
			estrAppend2(ppchResult, NULL_TO_EMPTY(pchValue));
		}
		else
		{
			Errorf("Invalid message passed to string formatter, string so far is %s", *ppchResult);
		}

	xcase STRFMT_CODE_STRUCT:
		{
			char* estrResult = NULL;
			void *pInnerStruct;
			ParseTable *pInnerTable;
			S32 iIndex;
			S32 iColumn;

			estrStackCreate(&estrResult);
			if (pchPath && objPathResolveFieldWithResult(pchPath, pContainer->pTable, pContainer->pValue, &pInnerTable, &iColumn, &pInnerStruct, &iIndex, OBJPATHFLAG_TRAVERSEUNOWNED, &estrResult) && pInnerStruct)
			{
				WriteTextFlags eFlags =  TOK_GET_TYPE(pInnerTable[iColumn].type)!=TOK_STRUCT_X ? WRITETEXTFLAG_PRETTYPRINT : 0;
				if (pContext->bTranslate)
					langFieldToSimpleEString(pContext->langID, pInnerTable, iColumn, pInnerStruct, iIndex, ppchResult, eFlags);
				else
					FieldWriteText(pInnerTable, iColumn, pInnerStruct, iIndex, ppchResult, eFlags);
			}
			else
			{
				// Do not error if it tried to traverse a null sub-struct
				// Also, in production mode, don't error if it cannot find an earray index by key
				if (!strStartsWith(estrResult, PARSERRESOLVE_TRAVERSE_NULL_SHORT) &&
					(isDevelopmentMode() || !strStartsWith(estrResult, PARSERRESOLVE_KEYED_INDEX_NOT_FOUND_SHORT)))
				{
					char *estrContainerKey = NULL;
					estrStackCreate(&estrContainerKey);
					objGetKeyEString(pContainer->pTable,pContainer->pValue,&estrContainerKey);
					ErrorDetailsf("Key: %s; Result: %s", estrContainerKey, estrResult);
					Errorf("Unknown path %s", pchPath);
					estrConcatf(ppchResult, "{Invalid Structure Field: %s}", pchPath);
					estrDestroy(&estrContainerKey);
				}
				
			}
			estrDestroy(&estrResult);
			pchPath = NULL;
		}
	xdefault:
		devassertmsgf(0, "Invalid type code passed to %s", __FUNCTION__);
	}

	if (pchPath)
	{
		Errorf("Invalid path %s passed to string formatter for token (probably not a pointer type?)  String so far is %s ", pchPath, *ppchResult);
	}
}

static void FromListFormat(unsigned char **ppchResult, const unsigned char *pchToken, StrFmtContext *pContext)
{
	StrFmtContainer *pContainer;
	const char *pchPath = strchr(pchToken, '.');
	char *pchPrePath = NULL;

	if (pchToken[0] == 'k' && pchToken[1] == ':')
	{
		strfmt_AppendMessageKey(ppchResult, pchToken + 2, pContext->langID);
		return;
	}
	if (pchPath)
	{
		estrStackCreate(&pchPrePath);
		estrConcat(&pchPrePath, pchToken, pchPath - (char*)pchToken);
		pchToken = pchPrePath;
	}
	if (stashFindPointer(pContext->stArgs, pchToken, &pContainer))
		strfmt_AppendContainer(ppchResult, pContainer, pchPath, pContext);
	else
	{
		Errorf("Unable to find a replacement for token %s (string is %s so far)", pchToken, *ppchResult);
		estrConcatf(ppchResult, "{Unknown Token %s}", pchToken);
	}
	estrDestroy(&pchPrePath);
}

void strfmt_FromListEx(unsigned char **ppchResult, const unsigned char *pchFormat, bool bTranslate, Language langID, va_list va)
{
	static StrFmtContext s_context;
	S32 iContainer;
	const unsigned char *pchKey;

	if (!s_context.stArgs)
		s_context.stArgs = stashTableCreateWithStringKeys(16, StashDefault);
	s_context.langID = langID;
	s_context.bTranslate = bTranslate;

	for (iContainer = 0; (pchKey = va_arg(va, const unsigned char *)) != 0; iContainer++)
	{
#if _PS3
        int chType = va_arg(va, int);
#else
		char chType = va_arg(va, char);
#endif
		StrFmtContainer *pContainer = alloca(sizeof(*pContainer));
		pContainer->chType = chType;
		switch (chType)
		{
		case STRFMT_CODE_INT:
			pContainer->iValue = va_arg(va, S32);
		xcase STRFMT_CODE_FLOAT:
			pContainer->fValue = va_arg(va, F64);
		xcase STRFMT_CODE_STRING:
		case STRFMT_CODE_MESSAGEKEY:
			pContainer->pchValue = va_arg(va, unsigned char *);
			if (!pContainer->pchValue)
				pContainer->pchValue = "";
		xcase STRFMT_CODE_STRUCT:
			pContainer->pValue = va_arg(va, void *);
			pContainer->pTable = va_arg(va, ParseTable *);
		xcase STRFMT_CODE_MESSAGE:
			pContainer->pValue = va_arg(va, Message *);
		xdefault:
			devassertmsgf(0, "Invalid type code passed to %s, did you forget STRFMT_END?", __FUNCTION__);
		}
		devassertmsg(stashAddPointer(s_context.stArgs, pchKey, pContainer, false), "Tried to add a key twice to a format arg list");
	}

	strfmt_Format(ppchResult, pchFormat, FromListFormat, &s_context, DefaultCondition, &s_context);

	stashTableClear(s_context.stArgs);
}

void strfmt_FromList(unsigned char **ppchResult, const unsigned char *pchFormat, va_list va)
{
	strfmt_FromListEx(ppchResult, pchFormat, false, langGetCurrent(), va);
}

void strfmt_FromArgsEx(unsigned char **ppchResult, const unsigned char *pchFormat, bool bTranslate, Language langID, ...)
{
	va_list va;
	va_start(va, langID);
	strfmt_FromListEx(ppchResult, pchFormat, bTranslate, langID, va);
	va_end(va);
}

void strfmt_FromArgs(unsigned char **ppchResult, const unsigned char *pchFormat,...)
{
	va_list va;
	va_start(va, pchFormat);
	strfmt_FromList(ppchResult, pchFormat, va);
	va_end(va);
}

bool strfmt_AppendMessageKey(unsigned char **ppchResult, const unsigned char *pchMessageKey, Language eLang)
{
	const char *pch = langTranslateMessageKey(eLang, pchMessageKey);
	estrAppend2(ppchResult, pch);
	return !!pch;
}

void strfmt_FromStructEx(char **ppchResult, const char *pchFormat, EARRAY_OF(StrFmtParam) ppFmtParams, bool bTranslate, Language langID)
{
	static StrFmtContext s_context;

	if (!s_context.stArgs)
		s_context.stArgs = stashTableCreateWithStringKeys(16, StashDefault);
	s_context.langID = langID;
	s_context.bTranslate = bTranslate;
	
	EARRAY_FOREACH_BEGIN(ppFmtParams, i);
	{
#if _PS3
        int chType = (int) ppFmtParams[i]->code
#else
		char chType = (char) ppFmtParams[i]->code;
#endif
		StrFmtContainer *pContainer = alloca(sizeof(*pContainer));
		pContainer->chType = chType;
		switch (chType)
		{
		case STRFMT_CODE_INT:
			pContainer->iValue = (S32) ppFmtParams[i]->iIntValue;
		xcase STRFMT_CODE_FLOAT:
			pContainer->fValue = (F64) ppFmtParams[i]->fFloatValue;
		xcase STRFMT_CODE_STRING:
			pContainer->pchValue = ppFmtParams[i]->pchStrValue;
			if (!pContainer->pchValue)
				pContainer->pchValue = "";
		xdefault:
			devassertmsgf(0, "Invalid or unsupported type code passed to %s.", __FUNCTION__);
		}
		devassertmsg(stashAddPointer(s_context.stArgs, ppFmtParams[i]->key, pContainer, false), 
			"Tried to add a key twice to a format arg list");
	}
	EARRAY_FOREACH_END;

	strfmt_Format(ppchResult, pchFormat, FromListFormat, &s_context, DefaultCondition, &s_context);
	stashTableClear(s_context.stArgs);
}

//////////////////////////////////////////////////////////////////////////
// Test cases.

#define FAIL_UNLESS_EQUAL(pch1, pch2) devassertmsg(!strcmp(pch1, pch2), "Strings are not equal")

AUTO_COMMAND ACMD_CATEGORY(Debug);
void strfmt_Test(void)
{
	char *pchTest = NULL;
	char *apchValues[] = {
		"Simple Test", "Simple Test",
		"Escaped\\\\ \\{Test\\}", "Escaped\\ {Test}",
	};
	S32 i;
	estrStackCreate(&pchTest);
	for (i = 0; i < ARRAY_SIZE_CHECKED(apchValues) - 1; i += 2)
	{
		estrClear(&pchTest);
		strfmt_FromArgs(&pchTest, apchValues[i], STRFMT_END);
		FAIL_UNLESS_EQUAL(pchTest, apchValues[i+1]);
	}

	estrClear(&pchTest);
	strfmt_FromArgs(&pchTest, "{Greeting}, user #{Count}, your score is {Score}.",
		STRFMT_INT("Count", 3), STRFMT_FLOAT("Score", 3.5), STRFMT_STRING("Greeting", "Hello"), STRFMT_END);
	FAIL_UNLESS_EQUAL(pchTest, "Hello, user #3, your score is 3.5.");

	estrDestroy(&pchTest);
}

#include "AutoGen/StringFormat_h_ast.c"