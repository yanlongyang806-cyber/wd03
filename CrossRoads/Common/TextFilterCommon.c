#include "TextFilter.h"
#include "StashTable.h"
#include "EString.h"
#include "error.h"
#include "GlobalTypes.h"

#include "AutoGen/TextFilterCommon_c_ast.h"

AUTO_RUN_ANON(memBudgetAddMapping(__FILE__, BUDGET_GameSystems););

AUTO_STRUCT;
typedef struct FilterPairStruct
{
	char *pStringName; AST(STRUCTPARAM)
	char *pDetailName; AST(STRUCTPARAM)
} FilterPairStruct;

AUTO_STRUCT;
typedef struct FilterStruct
{
	FilterPairStruct **ppPairs;
} FilterStruct;

void FilterStructReformattingCB(char *pInString, char **ppOutString, const char *pFileName)
{
	char *pReadHead = pInString;

	estrConcatf(ppOutString, "{\n");

	while (pReadHead)
	{
		char *pEOL = strchr(pReadHead, '\n');

		if (pEOL)
		{
			*pEOL = 0;
		}

		// This is the parsing code, the data I'm looking at doesn't know about : at all
		if (pReadHead && pReadHead[0])	
			estrConcatf(ppOutString, "Pairs %s\n", pReadHead);

		if (pEOL)
		{
			pReadHead = pEOL + 1;
		}
		else
		{
			pReadHead = NULL;
		}
	}
	estrConcatf(ppOutString, "}\n");
}	


AUTO_RUN;
void FilterStructInit(void)
{
	ParserSetReformattingCallback(parse_FilterStruct, FilterStructReformattingCB);
}

AUTO_FIXUPFUNC;
TextParserResult fixupFilterStruct(FilterStruct* filterStruct, enumTextParserFixupType eType, void *pExtraData)
{
	int i;
	switch (eType)
	{
	case FIXUPTYPE_POST_ALL_TEXT_READING_AND_INHERITANCE_DURING_LOADFILES:
		for (i = 0; i < eaSize(&filterStruct->ppPairs); i++)
		{
			char *oldString = filterStruct->ppPairs[i]->pStringName;
			char *oldDetail = filterStruct->ppPairs[i]->pDetailName;

			// Do the scramble, before it gets binned
			if (oldString)
			{
				int len = (int)strlen(oldString);								
				tf_CopyNonDelim(oldString, oldString, len, TF_STD_DELIMS);								
			}

			if (oldDetail)
			{
				int len = (int)strlen(oldDetail);								
				tf_CopyNonDelim(oldDetail, oldDetail, len, TF_STD_DELIMS);	
			}
		}		
	}
	return 1;
}

FilterTrieNode* TrieCreate( void )
{
	return calloc( 1, sizeof( FilterTrieNode ));
}

FilterTrieNode* TrieAddString( FilterTrieNode* pTrie, const char* str )
{
	unsigned char cur;
	
	if( !str ) {
		return NULL;
	}
	
	cur = *str;
	if( cur == '\0' ) {
		pTrie->isEnd = true;
		return pTrie;
	}

	cur = tf_UnscrambleChar( cur );
	cur = tolower( tf_De1337Single( cur ));

	if( !pTrie->children[ cur ]) {
		pTrie->children[ cur ] = TrieCreate();
	}

	return TrieAddString( pTrie->children[ cur ], str + 1 );
}

void TextFilterAddToTrie(FilterStruct* filterStruct, FilterTrieNode* pTrie)
{
	unsigned char pchMarked[2] = { '0', '\0' };
	int it;

	pchMarked[ 0 ] = tf_ScrambleChar( pchMarked[ 0 ]);
	for( it = 0; it != eaSize( &filterStruct->ppPairs ); ++it ) {
		char* string = filterStruct->ppPairs[ it ]->pStringName;
		char* detail = filterStruct->ppPairs[ it ]->pDetailName;
		FilterTrieNode* node = TrieAddString( pTrie, string );
		
		if( node && detail && stricmp( detail, pchMarked ) == 0 ) {
			node->ignoreSplitTokens = true;
			node->ignore1337Coalesce = true;
		}
	}
}

AUTO_STARTUP(AS_TextFilter);
void TextFilterLoad(void)
{
	FilterStruct filterStruct = {0};
	
	loadstart_printf("Loading TextFilters...");

	if ( !s_ProfanityTrie )
	{
		ParserLoadFiles(NULL,"defs/filters/profanity.txt", "cebsnar.bin", PARSER_OPTIONALFLAG, parse_FilterStruct, &filterStruct);
		s_ProfanityTrie = TrieCreate();
		TextFilterAddToTrie(&filterStruct, s_ProfanityTrie);
		StructReset(parse_FilterStruct,&filterStruct);
	}

	if ( !s_RestrictedTrie )
	{
		ParserLoadFiles(NULL,"defs/filters/ReservedNamesPartial.txt", "reservednames.bin", PARSER_OPTIONALFLAG, parse_FilterStruct, &filterStruct);
		s_RestrictedTrie = TrieCreate();
		TextFilterAddToTrie(&filterStruct, s_RestrictedTrie);
		StructReset(parse_FilterStruct,&filterStruct);
	}

	if ( !s_DisallowedNameTrie )
	{
		ParserLoadFiles(NULL,"defs/filters/ReservedNamesFull.txt", "disallowednames.bin", PARSER_OPTIONALFLAG, parse_FilterStruct, &filterStruct);
		s_DisallowedNameTrie = TrieCreate();
		TextFilterAddToTrie(&filterStruct, s_DisallowedNameTrie);
		StructReset(parse_FilterStruct,&filterStruct);
	}

	loadend_printf(" done.");
}

#include "AutoGen/TextFilterCommon_c_ast.c"
