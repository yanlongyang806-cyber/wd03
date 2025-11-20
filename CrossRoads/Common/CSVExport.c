#include "CSVExport.h"

#include "EString.h"
#include "Expression.h"
#include "File.h"
#include "objPath.h"
#include "osdependent.h"
#include "Message.h"
#include "MicroTransactions.h"
#include "StringUtil.h"
#include "textparser.h"
#include "TextParserInheritance.h"
#include "tokenstore.h"
#include "stdtypes.h"

#include "AutoGen/Message_h_ast.h"
#include "AutoGen/MicroTransactions_h_ast.h"

AUTO_RUN_ANON(memBudgetAddMapping(__FILE__, BUDGET_Editors););
AUTO_RUN_ANON(memBudgetAddMapping("CSVConfig", BUDGET_Editors););
AUTO_RUN_ANON(memBudgetAddMapping("CSVColumn", BUDGET_Editors););

void CSV_GetDocumentsDirEx(char *chDocumentsDir, size_t iLength)
{
	if (IsUsingVista())
	{
		quick_sprintf(chDocumentsDir, iLength, "C:/Users/%s/Documents", getUserName());
	}
	else if (IsUsingWin2kOrXp())
	{
		quick_sprintf(chDocumentsDir, iLength, "C:/Documents and Settings/%s/My Documents", getUserName());
	}
	else
	{
		quick_sprintf(chDocumentsDir, iLength, "C:/");
	}
}

// You know what the worst thing in the world is? We have to do this asinine microtrans-specific fixup here,
// because this CSV export is too generic for us to be able to do anything useful with it for microtransactions
static bool mtexport_ShouldUseFix(MicroTransactionDef *pDef, const char *pchFix)
{
	bool bFound = false;
	int i;
	char *pTemp = NULL;

	if (!eaiSize(&pDef->eaShards)) return true;

	estrStackCreate(&pTemp);

	for (i = 0; i < eaiSize(&pDef->eaShards); ++i)
	{
		MicroTrans_ShardConfig *pConfig = eaIndexedGetUsingInt(&g_MicroTransConfig.ppShardConfigs, pDef->eaShards[i]);

		if (!pConfig) continue;

		estrPrintf(&pTemp, "%s.", pConfig->pchCategoryPrefix);
		if (!stricmp_safe(pTemp, pchFix))
		{
			bFound = true;
			break;
		}

		estrPrintf(&pTemp, " _%s", pConfig->pchCurrency);
		if (!stricmp_safe(pTemp, pchFix))
		{
			bFound = true;
			break;
		}
	}

	estrDestroy(&pTemp);

	return bFound;
}

static void CSVFixEachToken(char *pcInput, char **estrOutput, const char *pchFix, bool bPrefix)
{
	char *estrCopy = estrStackCreateFromStr(pcInput);
	char *context = NULL;
	char *pTok = NULL;
	int i = 0;

	pTok = strtok_s(estrCopy, ",", &context);

	while (pTok != NULL)
	{
		if (i++ != 0)
		{
			estrConcatf(estrOutput, ",");
		}

		if (bPrefix)
		{
			estrConcatf(estrOutput, "%s%s", pchFix, pTok);
		}
		else
		{
			estrConcatf(estrOutput, "%s%s", pTok, pchFix);
		}

		pTok = strtok_s(NULL, ",", &context);
	}

	estrDestroy(&estrCopy);
}

void referentCSV_ResolveObjectPath(const char *objPath,
								   ParseTable *pParseTable,
								   void *pData,
								   ParseTable **ppResultParseTable,
								   int *piResultCol,
								   void **ppResultData)
{
	if (objPath[0] == '.')
	{
		int index;
		if (!objPathResolveField(objPath, pParseTable, pData, ppResultParseTable, piResultCol, ppResultData, &index, 0))
		{
			*ppResultParseTable = NULL;
			*piResultCol = -1;
			*ppResultData = NULL;
		}
	}
	else if (objPath[0] == '@')
	{
		// This is used for polymorphic fields, expecting format "@base.path@inside.poly.path"
		char buf[260];
		char *pos;
		int index;

		strcpy(buf, objPath);
		buf[0] = '.';
		pos = strchr(buf,'@');

		if (pos)
		{
			*pos = '\0';
		}

		if (!objPathResolveField(buf, pParseTable, pData, ppResultParseTable, piResultCol, ppResultData, &index, 0))
		{
			*ppResultData = NULL;
		}

		if (*ppResultData)
		{
			// Get here if poly field exists and is non-null, now test if whole field is valid
			if (pos)
			{
				*pos = '.';
			}
			if (!objPathResolveField(buf, pParseTable, pData, ppResultParseTable, piResultCol, ppResultData, &index, 0))
			{
				*ppResultParseTable = NULL;
				*piResultCol = -1;
				*ppResultData = NULL;
			}
		}
	}
	else
	{
		if (!ParserFindColumn(pParseTable, objPath, piResultCol))
		{
			*ppResultParseTable = NULL;
			*piResultCol = -1;
			*ppResultData = NULL;
		}
		else
		{
			*ppResultParseTable = pParseTable;
			*ppResultData = pData;
		}
	}
}

static void referent_FixCSV(void *pRef, ParseTable *pTPI, CSVColumn *pColumn, char *estrValue, char **estrFixed, bool bMicrotransactionExport)
{
	int i;
	bool bFirst = true;

	for (i = 0; i < eaSize(&pColumn->ppchFixes); ++i)
	{
		if (bMicrotransactionExport && !mtexport_ShouldUseFix(pRef, pColumn->ppchFixes[i])) continue;

		if (!bFirst)
		{
			estrConcatf(estrFixed, ",");
		}

		bFirst = false;

		if (!pColumn->bTokens)
		{
			if (pColumn->bPrefix)
			{
				estrConcatf(estrFixed, "%s%s", pColumn->ppchFixes[i], estrValue);
			}
			else
			{
				estrConcatf(estrFixed, "%s%s", estrValue, pColumn->ppchFixes[i]);
			}
		}
		//Else fix each token
		else
		{
			CSVFixEachToken(estrValue, estrFixed, pColumn->ppchFixes[i], pColumn->bPrefix);
		}
	}
}

void referentColumn_GetString(void *pRef, ParseTable *pTPI, CSVColumn *pColumn, char **estr, bool bMicrotransactionExport)
{
	char *estrValue = NULL;

	estrStackCreate(&estrValue);

	if (pColumn->eType == kCSVColumn_Parent)
	{
		// Find Parent reference for the object
		char *pcParentName = StructInherit_GetParentName(pTPI, pRef);

		if (pcParentName)
		{
			estrPrintf(&estrValue, "%s", pcParentName);
		}
	}
	else if (pColumn->eType == kCSVColumn_StaticText)
	{
		estrPrintf(&estrValue, "%s", pColumn->pchObjPath);
	}
	else
	{
		static char token[4096];
		int iFieldCol = -1;
		ParseTable *pFieldTable = NULL;
		void *pFieldData = NULL;

		referentCSV_ResolveObjectPath(pColumn->pchObjPath,
			pTPI,
			pRef,
			&pFieldTable,
			&iFieldCol,
			&pFieldData);

		if (iFieldCol != -1 && pFieldTable && pFieldData)
		{
			StructTypeField eStructColType = pFieldTable[iFieldCol].type;

			if (pColumn->eType == kCSVColumn_Message)
			{
				const Message *pMsg = NULL;

				if (pFieldTable[iFieldCol].subtable == parse_DisplayMessage)
				{
					DisplayMessage *pDisplayMessage = TokenStoreGetPointer(pFieldTable, iFieldCol, pFieldData, 0, 0);
					if (pDisplayMessage)
					{
						pMsg = GET_REF(pDisplayMessage->hMessage);
					}
				}
				else
				{
					pMsg = TokenStoreGetPointer(pFieldTable, iFieldCol, pFieldData, 0, NULL);
				}

				if (pMsg && pMsg->pcDefaultString)
				{
					estrPrintf(&estrValue, "%s", pMsg->pcDefaultString);
				}
			}
			else if (pColumn->eType == kCSVColumn_Flag && TOK_GET_TYPE(eStructColType) == TOK_STRUCT_X)
			{
				void *pStruct = TokenStoreGetPointer(pFieldTable, iFieldCol, pFieldData, 0, NULL);

				if (pStruct)
				{
					ParseTable *pStructTable;
					StructGetSubtable(pFieldTable, iFieldCol, pFieldData, 0, &pStructTable, NULL);
					if (TokenToSimpleString(pStructTable, 2, pStruct, token, 256, 0))
					{
						estrPrintf(&estrValue, "%s", token);
					}
				}
			}
			else if (pColumn->eType == kCSVColumn_Expression && TOK_GET_TYPE(eStructColType) == TOK_STRUCT_X)
			{
				Expression *pExprTarget = (Expression*)TokenStoreGetPointer(pFieldTable, iFieldCol, pFieldData, 0, NULL);
				const char *pText = pRef ? (pExprTarget ? exprGetCompleteString(pExprTarget) : "") : "";
				estrPrintf(&estrValue, "%s", pText);
			}
			else if (pColumn->eType == kCSVColumn_Boolean)
			{
				bool value = false;

				if (TOK_GET_TYPE(eStructColType) == TOK_BIT)
				{
					value = TokenStoreGetBit(pFieldTable, iFieldCol, pFieldData, 0, 0);
				}
				else
				{
					value = TokenStoreGetU8(pFieldTable, iFieldCol, pFieldData, 0, 0);
				}

				estrPrintf(&estrValue, "%s", value ? "TRUE" : "FALSE");
			}
			else
			{
				if (TokenToSimpleString(pFieldTable, iFieldCol, pFieldData, token, 1024, 0))
				{
					estrPrintf(&estrValue, "%s", token);
				}
			}
		}
	}

	if (estrValue)
	{
		estrReplaceOccurrences(&estrValue, "\"", "`");
		estrReplaceOccurrences(&estrValue, "\r\n", ". ");

		if (pColumn->bRemoveWhitespace)
		{
			while(estrReplaceOccurrences(&estrValue, ", ", ","));
			while(estrReplaceOccurrences(&estrValue, " ,", ","));
		}

		if (eaSize(&pColumn->ppchFixes))
		{
			char *estrFixed = NULL;
			estrStackCreate(&estrFixed);
			referent_FixCSV(pRef, pTPI, pColumn, estrValue, &estrFixed, bMicrotransactionExport);
			estrCopy(&estrValue, &estrFixed);
			estrDestroy(&estrFixed);
		}

		if (strchr(estrValue, ',') != NULL)
			estrConcatf(estr, "\"%s\"", estrValue);
		else
			estrConcatf(estr, "%s", estrValue);
	}

	estrDestroy(&estrValue);
}


// Creates a CSV description of the power and concatenates it to the estring
void referent_CSV(void *pRef, ParseTable *pTPI, CSVColumn **eaColumns, char **estr, bool bMicrotransactionExport)
{
	int i;
	bool bFirst = true;

	for (i = 0; i < eaSize(&eaColumns); ++i)
	{
		// Skip the "prices" column in microtransactions specifically
		if (bMicrotransactionExport && !stricmp(eaColumns[i]->pchTitle, "prices")) continue;

		if (!bFirst)
		{
			estrConcatf(estr, ",");
		}

		bFirst = false;
		referentColumn_GetString(pRef, pTPI, eaColumns[i], estr, bMicrotransactionExport);
	}

	estrConcatf(estr, "\n");
}

static void mtexport_CSV(MicroTransactionDef *pDef, CSVColumn **eaColumns, char **estr)
{
	char *estrRow = NULL;
	char *estrPrice = NULL;
	char *estrPriceFix = NULL;
	int i;

	estrStackCreate(&estrRow);
	estrStackCreate(&estrPrice);
	estrStackCreate(&estrPriceFix);
	referent_CSV(pDef, parse_MicroTransactionDef, eaColumns, &estrRow, true);
	estrTrimLeadingAndTrailingWhitespace(&estrRow);

	for (i = eaSize(&eaColumns) - 1; i >= 0; --i)
	{
		if (!stricmp(eaColumns[i]->pchTitle, "prices"))
			break;
	}

	devassert(i >= 0);

	if (pDef->bGenerateReclaimProduct && pDef->pReclaimProductConfig)
	{
		estrPrintf(&estrPrice, "%d", pDef->pReclaimProductConfig->uiOverridePrice);
		referent_FixCSV(pDef, parse_MicroTransactionDef, eaColumns[i], estrPrice, &estrPriceFix, true);

		if (strchr(estrPriceFix, ','))
			estrPrintf(&estrPrice, "\"%s\"", estrPriceFix);
		else
			estrCopy(&estrPrice, &estrPriceFix);

		estrConcatf(estr, "%s,%s,%s,%s,%s\n",
			pDef->pReclaimProductConfig->pchName,
			estrPrice,
			estrRow,
			pDef->pReclaimProductConfig->pchKeyValueChanges ? pDef->pReclaimProductConfig->pchKeyValueChanges : "",
			pDef->pReclaimProductConfig->pchPrerequisites ? pDef->pReclaimProductConfig->pchPrerequisites : "");
	}

	estrPrintf(&estrPrice, "%d", pDef->uiPrice);
	estrClear(&estrPriceFix);
	referent_FixCSV(pDef, parse_MicroTransactionDef, eaColumns[i], estrPrice, &estrPriceFix, true);

	if (strchr(estrPriceFix, ','))
		estrPrintf(&estrPrice, "\"%s\"", estrPriceFix);
	else
		estrCopy(&estrPrice, &estrPriceFix);

	if (pDef->pProductConfig)
	{
		estrConcatf(estr, "%s,%s,%s,%s,%s\n",
			pDef->pProductConfig->pchName,
			estrPrice,
			estrRow,
			pDef->pProductConfig->pchKeyValueChanges ? pDef->pProductConfig->pchKeyValueChanges : "",
			pDef->pProductConfig->pchPrerequisites ? pDef->pProductConfig->pchPrerequisites : "");
	}
	else
	{
		estrConcatf(estr, "%s,%s,%s,,\n", pDef->pchProductName, estrPrice, estrRow);
	}

	estrDestroy(&estrRow);
	estrDestroy(&estrPrice);
	estrDestroy(&estrPriceFix);
}

void referent_CSVHeader(CSVColumn **eaColumns, char **estr, bool bMicrotransactionExport)
{
	S32 i;
	bool bFirst = true;

	for (i = 0; i < eaSize(&eaColumns); ++i)
	{
		// Skip the prices column, for microtransaction exports
		if (bMicrotransactionExport && !stricmp(eaColumns[i]->pchTitle, "prices")) continue;

		if (!bFirst)
		{
			estrConcatf(estr, ",");
		}

		bFirst = false;
		estrConcatf(estr, "%s", eaColumns[i]->pchTitle);
	}

	estrConcatf(estr,"\n");
}

static void mtexport_PrependSpecialHeaders(char **estr)
{
	estrConcatf(estr, "name,prices,");
}

static void mtexport_AppendSpecialHeaders(char **estr)
{
	estrConcatf(estr, ",keyValueChanges,prerequisites");
}

static void mtexport_CSVHeader(CSVColumn **eaColumns, char **estr)
{
	mtexport_PrependSpecialHeaders(estr);
	referent_CSVHeader(eaColumns, estr, true);
	estrTrimLeadingAndTrailingWhitespace(estr);
	mtexport_AppendSpecialHeaders(estr);
	estrConcatChar(estr, '\n');
}

AUTO_COMMAND ACMD_SERVERCMD ACMD_ACCESSLEVEL(9) ACMD_PRIVATE ACMD_IFDEF(GAMECLIENT);
void referent_CSVExport(CSVConfig *pConfig)
{
	int i;
	ParseTable *pTPI = NULL;
	char *outString = NULL;
	bool bAllGroups;
	FileWrapper *fw;
	void **ppRefs = NULL;

	if (!isDevelopmentMode()) return;

	pTPI = ParserGetTableFromStructName(pConfig->pchStructName);

	if (!pTPI) return;

	bAllGroups = (pConfig->pchScope && *pConfig->pchScope) ? strcmp(pConfig->pchScope, "*") == 0 : false;
	fw = fileOpen(pConfig->pchFileName, "w");

	if (!fw) return;

	estrStackCreate(&outString);

	if (pConfig->bMicrotransactionExport)
		mtexport_CSVHeader(pConfig->eaColumns, &outString);
	else
		referent_CSVHeader(pConfig->eaColumns, &outString, false);

	//TODO(BH): If we end up needing attribs dumped, place them into the loop(s) below, and add the config
	//			to the config in the PowersEditor.

	if (eaSize(&pConfig->eaRefList))
	{
		for (i = 0; i < eaSize(&pConfig->eaRefList); ++i)
		{
			void *pRef = RefSystem_ReferentFromString(pConfig->pchDictionary, pConfig->eaRefList[i]);

			if (pRef)
			{
				eaPush(&ppRefs, pRef);
			}
		}
	}
	else
	{
		RefDictIterator iter;
		void *pRef;
		int iGroupCol = -1;

		if (pConfig->pchScopeColumnName)
		{
			ParserFindColumn(pTPI, pConfig->pchScopeColumnName, &iGroupCol);
		}

		RefSystem_InitRefDictIterator(pConfig->pchDictionary, &iter);

		while (pRef = RefSystem_GetNextReferentFromIterator(&iter))
		{
			if (bAllGroups || iGroupCol < 0)
			{
				eaPush(&ppRefs, pRef);
			}
			else
			{
				char pchGroup[1024];

				if (!TokenToSimpleString(pTPI, iGroupCol, pRef, pchGroup, 1024, 0))
				{
					pchGroup[0] = '\0';
				}

				if (pchGroup && pConfig->pchScope &&
					strnicmp(pConfig->pchScope, pchGroup, strlen(pConfig->pchScope)) == 0)
				{
					eaPush(&ppRefs, pRef);
				}
			}
		}
	}

	for (i = 0; i < eaSize(&ppRefs); ++i)
	{
		if (pConfig->bMicrotransactionExport)
			mtexport_CSV(ppRefs[i], pConfig->eaColumns, &outString);
		else
			referent_CSV(ppRefs[i], pTPI, pConfig->eaColumns, &outString, false);
	}

	//write the csv to the file, flush it and close it
	fwrite(outString, estrLength(&outString), 1, fw);
	fflush(fw);
	fclose(fw);
	eaDestroy(&ppRefs);
	estrDestroy(&outString);
}

#include "CSVExport_h_ast.c"