#include "WikiToHTML.h"
#include "Regex.h"
#include "EString.h"
#include "file.h"
#include "textparser.h"
#include "WikiToHTML_c_ast.h"

AUTO_STRUCT;
typedef struct WikiRegex 
{
	char * pPattern; AST(ESTRING NAME(Pattern))
	char * pReplace; AST(ESTRING NAME(Replace))
} WikiRegex;

AUTO_STRUCT;
typedef struct WikiRegexList
{
	EARRAY_OF(WikiRegex) ppWikiRegex;
} WikiRegexList;

extern char gAccountDataPath[MAX_PATH];

// Converts wiki syntax to an HTML estring
char * wikiToHTML(SA_PARAM_NN_STR const char *pWiki)
{
	static WikiRegexList *pList = NULL;
	char *pCopy = estrDup(pWiki);

	if (!pList)
	{
		char filepath[MAX_PATH];
		sprintf(filepath, "%swikiSyntax.txt", gAccountDataPath);
		if (!fileExists(filepath)) return pCopy;
		pList = StructCreate(parse_WikiRegexList);
		ParserReadTextFile(filepath, parse_WikiRegexList, pList, 0);
	}

	if (!pList) return pCopy;

	EARRAY_CONST_FOREACH_BEGIN(pList->ppWikiRegex, i, s);
		regexFancyReplace(&pCopy, pList->ppWikiRegex[i]->pPattern, pList->ppWikiRegex[i]->pReplace);
	EARRAY_FOREACH_END;

	return pCopy;
}

#include "WikiToHTML_c_ast.c"