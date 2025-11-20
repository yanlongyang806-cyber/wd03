#include "textparser.h"
#include "textparserUtils.h"
#include "HttpXpathSupport.h"
#include "TextParserServerMon_c_ast.h"
#include "StashTable.h"
#include "StructDefines.h"
#include "structInternals.h"

AUTO_STRUCT;
typedef struct ParseTableNameAndLink
{
	char *pName; AST(KEY, POOL_STRING)
	char *pLink; AST(ESTRING, FORMATSTRING(HTML=1))
} ParseTableNameAndLink;

AUTO_STRUCT;
typedef struct ParseTableList
{
	ParseTableNameAndLink **ppParseTables;
} ParseTableList;

AUTO_STRUCT;
typedef struct ParseTableFieldForServerMon
{
	const char *pFieldName; AST(KEY, POOL_STRING)
	const char *pType; AST(POOL_STRING)
	const char **ppRedundantNames; AST(POOL_STRING)
	const char *pStructTypeName; AST(POOL_STRING)
	const char *pStaticDefineName; AST(POOL_STRING)
} ParseTableFieldForServerMon;

AUTO_STRUCT;
typedef struct ParseTableForServerMon
{
	const char *pTableName; AST(POOL_STRING)
	ParseTableFieldForServerMon **ppFields;
} ParseTableForServerMon;

ParseTableForServerMon *GetParseTableForServerMon(ParseTable *pParseTable)
{
	int i;
	ParseTableForServerMon *pRetStruct = StructCreate(parse_ParseTableForServerMon);
	pRetStruct->pTableName = ParserGetTableName(pParseTable);

	FORALL_PARSETABLE(pParseTable, i)
	{
		StructTypeField type = TOK_GET_TYPE(pParseTable[i].type);
		ParseTableFieldForServerMon *pField;

		if (type == TOK_IGNORE) continue;
		if (pParseTable[i].type & TOK_REDUNDANTNAME) continue;
		if (type == TOK_START) continue;
		if (type == TOK_END) continue;

		pField = StructCreate(parse_ParseTableFieldForServerMon);
		pField->pFieldName = pParseTable[i].name;
		pField->pType = ParseInfoGetType(pParseTable, i, NULL);

		if (TOK_HAS_SUBTABLE(pParseTable[i].type))
		{
			if (pParseTable[i].subtable)
			{
				pField->pStructTypeName = ParserGetTableName(pParseTable[i].subtable);
			}
			else
			{
				pField->pStructTypeName = "UNKNOWN";
			}
		}

		if (TYPE_INFO(pParseTable[i].type).interpretfield(pParseTable, i, SubtableField) == StaticDefineList && pParseTable[i].subtable)
		{
			const char *pName = FindStaticDefineName(pParseTable[i].subtable);
			if (pName)
			{
				pField->pStaticDefineName = pName;
			}
			else
			{
				pField->pStaticDefineName = "UNKNOWN";
			}
		}

		eaPush(&pRetStruct->ppFields, pField);
	}

	FORALL_PARSETABLE(pParseTable, i)
	{
		if (pParseTable[i].type & TOK_REDUNDANTNAME)
		{
			int iIndexOfMainField = ParseInfoFindAliasedField(pParseTable, i);
			if (iIndexOfMainField >= 0)
			{
				ParseTableFieldForServerMon *pMainField = eaIndexedGetUsingString(&pRetStruct->ppFields, pParseTable[iIndexOfMainField].name);
				if (pMainField)
				{
					eaPush(&pMainField->ppRedundantNames, pParseTable[i].name);
				}
			}
		}
	}


	return pRetStruct;
}



bool ProcessParseTableIntoStructInfoForHttp(char *pLocalXPath, UrlArgumentList *pArgList, int iAccessLevel, StructInfoForHttpXpath *pStructInfo, GetHttpFlags eFlags)
{
	char *pFirstBracket;
	ParseTable *pParseTable;

	if (pLocalXPath[0] != '[')
	{
		static ParseTableList *pList = NULL;

		if (pList)
		{
			StructReset(parse_ParseTableList, pList);
		}
		else
		{
			pList = StructCreate(parse_ParseTableList);
		}

		FOR_EACH_IN_STASHTABLE2(gParseTablesByName, pElem)
		{
			ParseTableNameAndLink *pLink = StructCreate(parse_ParseTableNameAndLink);
			pLink->pName = stashElementGetStringKey(pElem);
			estrPrintf(&pLink->pLink, "<a href=\"%s.ParseTables[%s]\">Link</a>", 
						LinkToThisServer(), pLink->pName);
			eaPush(&pList->ppParseTables, pLink);
		}
		FOR_EACH_END;

	
		return ProcessStructIntoStructInfoForHttp("", pArgList,
			pList, parse_ParseTableList, iAccessLevel, 0, pStructInfo, eFlags);
	}

	pFirstBracket = strchr(pLocalXPath, ']');
	if (!pFirstBracket)
	{
		GetMessageForHttpXpath("Error - expected ] (format should be .ParseTables[tablename])", pStructInfo, true);
		return true;
	}

	*pFirstBracket = 0;

	pParseTable = ParserGetTableFromStructName(pLocalXPath + 1);

	if (pParseTable)
	{
		bool bRetVal;
		ParseTableForServerMon *pTableForServerMon = GetParseTableForServerMon(pParseTable);
		bRetVal = ProcessStructIntoStructInfoForHttp("", pArgList, pTableForServerMon, parse_ParseTableForServerMon,
			iAccessLevel, 0, pStructInfo, eFlags);
		StructDestroy(parse_ParseTableForServerMon, pTableForServerMon);
		return bRetVal;
	}
	else
	{
		GetMessageForHttpXpath(STACK_SPRINTF("Unknown parse table %s", pLocalXPath + 1), pStructInfo, true);
		return true;
	}
}




AUTO_STRUCT;
typedef struct StaticDefineNameAndLink
{
	char *pName; AST(KEY, POOL_STRING)
	char *pLink; AST(ESTRING, FORMATSTRING(HTML=1))
} StaticDefineNameAndLink;

AUTO_STRUCT;
typedef struct StaticDefineListForServerMon
{
	StaticDefineNameAndLink **ppStaticDefines;
} StaticDefineListForServerMon;

AUTO_STRUCT;
typedef struct StaticDefineEntryForServerMon
{
	const char *pFieldName; AST(POOL_STRING)
	int iValue;
} StaticDefineEntryForServerMon;

AUTO_STRUCT;
typedef struct StaticDefineForServerMon
{
	const char *pDefineName; AST(POOL_STRING)
	StaticDefineEntryForServerMon **ppFields;
} StaticDefineForServerMon;


bool ProcessStaticDefineIntoStructInfoForHttp(char *pLocalXPath, UrlArgumentList *pArgList, int iAccessLevel, StructInfoForHttpXpath *pStructInfo, GetHttpFlags eFlags)
{
	char *pFirstBracket;
	StaticDefineInt *pDefine;

	if (pLocalXPath[0] != '[')
	{
		static StaticDefineListForServerMon *pList = NULL;

		if (pList)
		{
			StructReset(parse_StaticDefineListForServerMon, pList);
		}
		else
		{
			pList = StructCreate(parse_StaticDefineListForServerMon);
		}

		FOR_EACH_IN_STASHTABLE2(sStaticDefinesByName, pElem)
		{
			StaticDefineNameAndLink *pLink = StructCreate(parse_StaticDefineNameAndLink);
			pLink->pName = stashElementGetStringKey(pElem);
			estrPrintf(&pLink->pLink, "<a href=\"%s.StaticDefines[%s]\">Link</a>", 
						LinkToThisServer(), pLink->pName);
			eaPush(&pList->ppStaticDefines, pLink);
		}
		FOR_EACH_END;

	
		return ProcessStructIntoStructInfoForHttp("", pArgList,
			pList, parse_ParseTableList, iAccessLevel, 0, pStructInfo, eFlags);
	}

	pFirstBracket = strchr(pLocalXPath, ']');
	if (!pFirstBracket)
	{
		GetMessageForHttpXpath("Error - expected ] (format should be .StaticDefines[tablename])", pStructInfo, true);
		return true;
	}

	*pFirstBracket = 0;

	pDefine = FindNamedStaticDefine(pLocalXPath + 1);

	if (pDefine)
	{
		char **ppKeys = NULL;
		U32 *piValues = NULL;
		int i;
		bool bRetVal;

		StaticDefineForServerMon *pStaticDefineForServerMon = StructCreate(parse_StaticDefineForServerMon);

		pStaticDefineForServerMon->pDefineName = FindStaticDefineName(pDefine);
		DefineFillAllKeysAndValues(pDefine, &ppKeys, &piValues);

		for (i = 0; i < eaSize(&ppKeys); i++)
		{
			StaticDefineEntryForServerMon *pEntry = StructCreate(parse_StaticDefineEntryForServerMon);
			pEntry->pFieldName = ppKeys[i];
			pEntry->iValue = piValues[i];
			eaPush(&pStaticDefineForServerMon->ppFields, pEntry);
		}
		eaDestroy(&ppKeys);
		ea32Destroy(&piValues);

		bRetVal = ProcessStructIntoStructInfoForHttp("", pArgList, pStaticDefineForServerMon, parse_StaticDefineForServerMon,
			iAccessLevel, 0, pStructInfo, eFlags);
		StructDestroy(parse_StaticDefineForServerMon, pStaticDefineForServerMon);
		return bRetVal;
	}
	else
	{
		GetMessageForHttpXpath(STACK_SPRINTF("Unknown staticDefine %s", pLocalXPath + 1), pStructInfo, true);
		return true;
	}
}


AUTO_RUN;
void initTextParserServerMon(void)
{
	RegisterCustomXPathDomain(".ParseTables", ProcessParseTableIntoStructInfoForHttp, NULL);
	RegisterCustomXPathDomain(".StaticDefines", ProcessStaticDefineIntoStructInfoForHttp, NULL);
}





#include "TextParserServerMon_c_ast.c"
