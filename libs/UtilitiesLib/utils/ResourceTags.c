#include "AutoGen/ResourceTags_c_ast.h"
#include "error.h"
#include "ResourceSystem_Internal.h"
#include "qsortG.h"

AUTO_RUN_ANON(memBudgetAddMapping(__FILE__, BUDGET_Editors););

// This is the file responsible for loading dictionary tag descriptions

AUTO_STRUCT;
typedef struct DictionaryTagNames 
{
	const char *pDictName; AST(POOL_STRING STRUCTPARAM)

	char **ppTags; AST(NAME(Tag))

	const char *pFileName; AST(CURRENTFILE)
} DictionaryTagNames;

AUTO_STRUCT;
typedef struct DictionaryTagNamesList
{
	DictionaryTagNames **ppDicts; AST(NAME(TagsFor))
} DictionaryTagNamesList;

void resAddValidTag(DictionaryHandleOrName dictHandle, const char *pchTag)
{
	ResourceDictionary *pDict = resGetDictionary(dictHandle);
	if (pDict)
	{	
		int index = (int)eaBFind(pDict->ppValidTags, strCmp, pchTag);

		if (!pDict->ppValidTags || index == eaSize(&pDict->ppValidTags) || stricmp(pchTag, pDict->ppValidTags[index]) != 0)
		{				
			eaInsert(&pDict->ppValidTags, strdup(pchTag), index);
		}
	}
}


const char **resGetValidTags(DictionaryHandleOrName dictHandle)
{
	ResourceDictionary *pDictionary = resGetDictionary(dictHandle);

	if (pDictionary)
	{
		return pDictionary->ppValidTags;
	}
	return NULL;
}




AUTO_STARTUP(ResourceTags);
void ResourceTagsLoad(void)
{
	DictionaryTagNamesList sLoadedTags = {0};
	int i;
	loadstart_printf("Loading ResourceTags...");

	ParserLoadFiles("defs/config", ".tagnames", "ResourceTags.bin", PARSER_OPTIONALFLAG, parse_DictionaryTagNamesList, &sLoadedTags);

	for (i = 0; i < eaSize(&sLoadedTags.ppDicts); i++)
	{
		int j;
		DictionaryTagNames *pTags = sLoadedTags.ppDicts[i];
		ResourceDictionary *pDict = resGetDictionary(pTags->pDictName);

		if (!pDict)
		{
			ErrorFilenamef(pTags->pFileName, "Tags listed for nonexistent dictionary '%s'", pTags->pDictName);

			continue;			
		}

		for (j = 0; j < eaSize(&pTags->ppTags); j++)
		{
			if (strchr(pTags->ppTags[j], ','))
			{
				ErrorFilenamef(pTags->pFileName, "Tag %s listed for dictionary '%s' has invalid character ','", pTags->ppTags[j], pTags->pDictName);
				continue;
			}
			if (strchr(pTags->ppTags[j], ' '))
			{
				ErrorFilenamef(pTags->pFileName, "Tag %s listed for dictionary '%s' has invalid character ' '", pTags->ppTags[j], pTags->pDictName);
				continue;
			}
			resAddValidTag(pTags->pDictName, pTags->ppTags[j]);
		}
	}

	StructDeInit(parse_DictionaryTagNamesList, &sLoadedTags);

	loadend_printf(" done.");
}

#include "AutoGen/ResourceTags_c_ast.c"