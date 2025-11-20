// Regular expressions

#include "Regex.h"

// Right now, only support regular expressions on Win32/64.
#ifdef CRYPTIC_REGEX_SUPPORTED

#define PCRE_STATIC

#include "pcre.h"
#include "EString.h"
#include "earray.h"
#include "textparser.h"
#include "Regex_c_ast.h"
#include "timing_profiler.h"

AUTO_RUN_ANON(memBudgetAddMapping(__FILE__, BUDGET_GameSystems););

#ifdef _WIN64
#pragma comment(lib, "pcre64.lib")
#else
#pragma comment(lib, "pcre.lib")
#endif

AUTO_STRUCT;
typedef struct CompiledRegex
{
	char *pPattern; AST(KEY)
	pcre *pRe;		NO_AST
} CompiledRegex;

SA_RET_OP_VALID static pcre * getCompiledRegex(SA_PARAM_NN_STR const char * pPattern, SA_PRE_NN_NN_STR const char **pErrorMessage)
{
	static EARRAY_OF(CompiledRegex) eaRegex = NULL;
	int index = eaIndexedFindUsingString(&eaRegex, pPattern);
	CompiledRegex *pRegex;
	char *re_str;
	int re_erroroff;
	pcre *re;

	PERFINFO_AUTO_START_FUNC();

	if (pErrorMessage)
		*pErrorMessage = NULL;

	if (!eaRegex)
	{
		eaIndexedEnable(&eaRegex, parse_CompiledRegex);
	}

	if (index >= 0)
	{
		PERFINFO_AUTO_STOP_FUNC();

		return eaRegex[index]->pRe;
	}

	re = pcre_compile(pPattern, PCRE_CASELESS, &re_str, &re_erroroff, NULL);

	if (!re && pErrorMessage)
		*pErrorMessage = re_str;

	if (re)
	{
		pRegex = StructCreate(parse_CompiledRegex);
		pRegex->pRe = re;
		pRegex->pPattern = strdup(pPattern);
		eaIndexedAdd(&eaRegex, pRegex);
	}

	PERFINFO_AUTO_STOP_FUNC();

	return re;
}

int regexMatch_s(SA_PARAM_NN_STR const char * pPattern, SA_PARAM_NN_STR const char *pSubject, SA_PARAM_OP_VALID int * pMatches, int iMatchesSize,
				 char **pErrorMessage)
{
	pcre *re;
	int result = -100;
	
	PERFINFO_AUTO_START_FUNC();

	// Compile regular expression if necessary.
	re = getCompiledRegex(pPattern, pErrorMessage);

	// Check for matches.
	if (re)
		result = pcre_exec(re, NULL, pSubject, (int)strlen(pSubject), 0, 0, pMatches, iMatchesSize);

	PERFINFO_AUTO_STOP_FUNC();

	return result;
}

void regexFancyReplace(SA_PARAM_NN_STR char **pEstrSubject, SA_PARAM_NN_STR const char * pPattern, SA_PARAM_NN_STR const char *pReplace)
{
	char *pReplaceCopy = NULL;
	int pMatches[100];
	int iNumMatches;
	int i;

	PERFINFO_AUTO_START_FUNC();

	iNumMatches = regexMatch(pPattern, *pEstrSubject, pMatches);

	for (i = 0; i < iNumMatches; i += 2)
	{
		char pSubString[256];
		char pInnerSubString[256];
		char *pSubStart = *pEstrSubject + pMatches[i * 2];
		int iSubLen = pMatches[i * 2 + 1] - pMatches[i * 2];
		char *pInnerSubStart = *pEstrSubject + pMatches[i * 2 + 2];
		int iInnerSubLen = pMatches[i * 2 + 3] - pMatches[i * 2 + 2];
		sprintf(pSubString, "%.*s", iSubLen, pSubStart);
		sprintf(pInnerSubString, "%.*s", iInnerSubLen, pInnerSubStart);

		estrCopy2(&pReplaceCopy, pReplace);
		estrReplaceOccurrences(&pReplaceCopy, "[replace]", pInnerSubString);
		estrReplaceOccurrences(pEstrSubject, pSubString, pReplaceCopy);
	}

	if (pReplaceCopy) estrDestroy(&pReplaceCopy);

	PERFINFO_AUTO_STOP_FUNC();
}

#else

typedef struct CompiledRegex
{
	char *pPattern; AST(KEY)
	void *pRe;		NO_AST
} CompiledRegex;

int regexMatch_s(SA_PARAM_NN_STR const char * pPattern, SA_PARAM_NN_STR const char *pSubject, SA_PARAM_NN_VALID int * pMatches, int iMatchesSize,
				 char **pErrorMessage)
{
	assertmsg(0, "Unsupported");
	return 0;
}

void regexFancyReplace(SA_PARAM_NN_STR char **pEstrSubject, SA_PARAM_NN_STR const char * pPattern, SA_PARAM_NN_STR const char *pReplace)
{
	assertmsg(0, "Unsupported");
}

#endif

// Find an entry that matches a regular expression in an EArray.
int eaFindRegex(CONST_STRING_EARRAY * eaArray, CONST_STRING_EARRAY * eaPatterns)
{
	// If the regex set is empty, return no match.
	if (!eaSize(eaPatterns))
		return -1;

	// Search for a match.
	EARRAY_CONST_FOREACH_BEGIN(*eaArray, i, n);
	{
		EARRAY_CONST_FOREACH_BEGIN(*eaPatterns, j, m);
		{
			if (!regexMatch_s((*eaPatterns)[j], (*eaArray)[i], NULL, 0, NULL))
				return i;
		}
		EARRAY_FOREACH_END;
	}
	EARRAY_FOREACH_END;

	// No match.
	return -1;
}

//making this both an auto_command (for build scripting) and an expr func (for everything else)


AUTO_EXPR_FUNC(util) ACMD_NAME(RegexExtract);
char *RegexExtract_Internal(const char *regex, const char *input, char *defaultOutput)
{
#ifdef CRYPTIC_REGEX_SUPPORTED
	static char output[1024] = {0}; // XXX: Not exactly threadsafe. <NPK 2010-06-07>
	int ovector[6], rv;

	rv = regexMatch(regex, input, ovector);
	if(rv == 0 || rv == 2)
	{
		int rv2 = pcre_copy_substring(input, ovector, rv?rv:2, 1, SAFESTR(output));
		if(rv2 < 0)
			output[0] = '\0';
	}
	else
		output[0] = '\0';
	
	if (!output[0] && defaultOutput)
	{
		strcpy(output, defaultOutput);
	}
		
	return output;

#else
	return "";
#endif
}


AUTO_COMMAND;
char *RegexExtract(const char *regex, const char *input, char *defaultOutput)
{
	return RegexExtract_Internal(regex, input, defaultOutput);
}



#include "Regex_c_ast.c"
