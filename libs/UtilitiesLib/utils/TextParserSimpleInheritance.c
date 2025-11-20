#include "TextParserSimpleInheritance.h"
#include "structInternals.h"

#include "estring.h"
#include "error.h"
#include "referencesystem.h"
#include "stringcache.h"
#include "tokenstore.h"

AUTO_RUN_ANON(memBudgetAddMapping(__FILE__, BUDGET_GameSystems););

#define TOK_HAS_SIMPLE_INHERITANCE(type) (((type) & TOK_INHERITANCE_STRUCT) && ((type) & TOK_EARRAY))
#define TOK_HAS_USEDFIELD(type)			 ((type) & TOK_USEDFIELD)

void SimpleInheritanceApply(ParseTable *pti, void *dst, void *src, SimpleInheritanceCopyFunc func, void *userdata)
{
	int i;
	int usedFieldIndex = ParserGetUsedBitFieldIndex(pti);

	devassertmsgf(usedFieldIndex>=0, 
		"Unable to apply simple inheritance to struct %s - used bit field does not exist", 
		ParserGetTableName(pti));

	FORALL_PARSETABLE(pti, i)
	{
		if(pti[i].type & TOK_REDUNDANTNAME)
			continue;
		
		if(TOK_HAS_SIMPLE_INHERITANCE(pti[i].type))
		{
			const char *dictName = ParserGetTableName(pti);
			int parentIndex;
			const char ***eaParents;

			assertmsgf(RefSystem_DoesDictionaryExist(dictName), "Dictionary name for simple inheritance must be same as struct name: %s", ParserGetTableName(pti));
			assertmsgf(TOK_GET_TYPE(pti[i].type)==TOK_STRING_X, "Simple inheritance data on struct %s was not of type string", ParserGetTableName(pti));

			eaParents = (const char***)TokenStoreGetEArray(pti, i, src, NULL);

			for(parentIndex = 0; parentIndex < eaSize(eaParents); parentIndex++)
			{
				void *parent = RefSystem_ReferentFromString(dictName, (*eaParents)[parentIndex]);

				SimpleInheritanceApply(pti, dst, parent, func, userdata);
			}
			continue;
		}

		if(dst==src)
		{
			continue;
		}
		
		if(func && func(pti, i, dst, src, userdata))
		{
			continue;
		}
				

		if(TokenIsSpecified(pti, i, src, usedFieldIndex) && 
			!TokenIsSpecified(pti, i, dst, usedFieldIndex))
		{
			TokenCopy(pti, i, dst, src, 1);
		}
		else if(TOK_HAS_SUBTABLE(pti[i].type))
		{
			int j;
			int subUsedField;
			int foundSubSimpleInheritance = 0;
			ParseTable *ptisubdst = NULL, *ptisubsrc = NULL;
			void *subdst = StructGetSubtable(pti, i, dst, 0, &ptisubdst, NULL);
			void *subsrc = StructGetSubtable(pti, i, src, 0, &ptisubsrc, NULL);

			assert(ptisubdst==ptisubsrc);

			subUsedField = ParserGetUsedBitFieldIndex(ptisubdst);
			if(subUsedField!=-1)
			{
				FORALL_PARSETABLE(ptisubdst, j)
				{
					if(TOK_HAS_USEDFIELD(ptisubdst[j].type) || TOK_HAS_SIMPLE_INHERITANCE(ptisubdst[j].type))
					{
						foundSubSimpleInheritance = 1;
						break;
					}
				}
			}

			if(foundSubSimpleInheritance)
				SimpleInheritanceApply(ptisubdst, subdst, subsrc, func, userdata);
		}
		
		
	}
}