#include "structInternals.h"
#include "url.h"
#include "tokenstore.h"
#include "autogen/textparser_h_ast.h"
#include "autogen/textparserhtml_c_ast.h"
#include "objpath.h"
#include "expression.h"
#include "sock.h"
#include "cmdparse.h"

#include "crypt.h"
#include "structinternals_h_ast.h"
#include "textParserHtml.h"
#include "NotesServerComm.h"
#include "StringUtil.h"

// ------------------------------------------------------------------------------------------------------
// Forwards

bool FindOverrideLink(ParseTable *pTPI, void *pStructData, char **ppOutEString);

// in textparser.c
void WriteString(FILE* out, const char* str, int tabs, int eol);

// ------------------------------------------------------------------------------------------------------
// Helpful macros

#define writeHTMLContextPushGeneratingTable(pContext, value) { eaiPush(&pContext->internalState.iGeneratingTableStack, value); }
#define writeHTMLContextPopGeneratingTable(pContext) { eaiPop(&pContext->internalState.iGeneratingTableStack); }
#define writeHTMLContextIsGeneratingTable(pContext) ((eaiSize(&pContext->internalState.iGeneratingTableStack) > 0) \
	? pContext->internalState.iGeneratingTableStack[eaiSize(&pContext->internalState.iGeneratingTableStack)-1] : false)


#define writeHTMLContinueIfWeDontCareAboutThisEntry(tpi, iColumn, pStruct, bMakingTable) \
		if (tpi[iColumn].type & TOK_REDUNDANTNAME) continue; \
	/*	if (tpi[column].type & TOK_STRUCTPARAM) continue;   ABW not sure why this was here, I think it was just a mistake JDRAGO made*/ \
		if (TOK_GET_TYPE(tpi[iColumn].type) == TOK_START) continue; \
		if (TOK_GET_TYPE(tpi[iColumn].type) == TOK_END) continue; \
		if (TOK_GET_TYPE(tpi[iColumn].type) == TOK_IGNORE) continue; \
		if (TOK_GET_TYPE(tpi[iColumn].type) == TOK_COMMAND) continue; \
		if (GetBoolFromTPIFormatString(&tpi[iColumn], "command")) continue; \
		if (GetBoolFromTPIFormatString(&tpi[iColumn], "HTML_SKIP")) continue; \
		if (bMakingTable && GetBoolFromTPIFormatString(&tpi[iColumn], "HTML_SKIP_IN_TABLE")) continue; \
		if (!tpi[iColumn].name || !tpi[iColumn].name[0]) continue; \
		if (pContext->bNeedToDoAccessLevelChecks && FieldFailsHTMLAccessLevelCheck(tpi, iColumn, pStruct)) continue
		


// ------------------------------------------------------------------------------------------------------

static void pushXPath(WriteHTMLContext *pContext, const char *pName, char *sIndex, ParseTable *pTPI, void *pStruct)
{
	char *pTempEString = NULL;

	if(strcmp(sIndex,"-1") == 0)
	{
		eaPush(&pContext->internalState.ppXPathSegments, strdup(pName));
	}
	else
	{
		char *pTemp = NULL;
		estrStackCreate(&pTemp);
		estrPrintf(&pTemp, "[%s]", sIndex);
		eaPush(&pContext->internalState.ppXPathSegments, strdup(pTemp));
		estrDestroy(&pTemp);
	}

	if (pStruct)
	{
		if (FindOverrideLink(pTPI, pStruct, &pTempEString))
		{
			estrCopy(&pContext->internalState.pOverrideLink, &pTempEString);
			estrDestroy(&pTempEString);
			pContext->internalState.iXpathDepthOfOverrideLink = eaSize(&pContext->internalState.ppXPathSegments);
		}
	}
}

static void popXPath(WriteHTMLContext *pContext)
{
	char *p = eaPop(&pContext->internalState.ppXPathSegments);
	if(p)
		free(p);

	if (pContext->internalState.iXpathDepthOfOverrideLink == eaSize(&pContext->internalState.ppXPathSegments) + 1)
	{
		estrDestroy(&pContext->internalState.pOverrideLink);
		pContext->internalState.iXpathDepthOfOverrideLink = -1;
	}
}

//Just print the path after "...Struct"
static void concatXPath(WriteHTMLContext *pContext, char **eStr)
{
	int i;
	if (!eStr) return;
	if (eaSize(&pContext->internalState.ppXPathSegments) < 2) return;
	if (strcmpi(pContext->internalState.ppXPathSegments[1],"Struct") != 0) return;
	for (i = 2; i < eaSize(&pContext->internalState.ppXPathSegments); i++)
	{
		if (pContext->internalState.ppXPathSegments[i][0] == '[')
		{
			if (pContext->internalState.ppXPathSegments[i][1] != '"')
			{
				char *closebracket;
				estrConcatf(eStr, "[\"%s", &pContext->internalState.ppXPathSegments[i][1]);
				closebracket = strrchr(*eStr, ']');
				*closebracket = '"';
				estrConcatf(eStr, "]");
			}
			else
			{
				estrConcatf(eStr, "%s", pContext->internalState.ppXPathSegments[i]);
			}
		}
		else
		{
			estrConcatf(eStr, ".%s", pContext->internalState.ppXPathSegments[i]);
		}
	}
}


static void concatXPathWithoutStruct(WriteHTMLContext *pContext, char **eStr)
{
	int i;
	if (!eStr) return;
	for (i = 1; i < eaSize(&pContext->internalState.ppXPathSegments); i++)
	{
		if (pContext->internalState.ppXPathSegments[i][0] == '[')
		{
			if (pContext->internalState.ppXPathSegments[i][1] != '"')
			{
				char *closebracket;
				estrConcatf(eStr, "[\"%s", &pContext->internalState.ppXPathSegments[i][1]);
				closebracket = strrchr(*eStr, ']');
				*closebracket = '"';
				estrConcatf(eStr, "]");
			}
			else
			{
				estrConcatf(eStr, "%s", pContext->internalState.ppXPathSegments[i]);
			}
		}
		else
		{
			estrConcatf(eStr, ".%s", pContext->internalState.ppXPathSegments[i]);
		}
	}
}


// ------------------------------------------------------------------------------------------------------
// WriteHTMLContext init / shutdown

void initWriteHTMLContext(WriteHTMLContext *pContext, 
						  bool bArrayContext,
						  UrlArgumentList *pUrlArgs, 
						  int iMaxDepth, 
						  const char *pCurrentXPath,
						  const char *pViewURL,
						  const char *pFormURL,
						  const char *pProcessURL,
						  bool bNeedsToDoAccessLevelChecks)
{
	memset(pContext, 0, sizeof(WriteHTMLContext));

	pContext->bArrayContext = bArrayContext;
	pContext->pUrlArgs      = pUrlArgs;
	pContext->iMaxDepth     = iMaxDepth;

	pContext->pViewURL           = pViewURL;
	pContext->pCommandFormURL    = pFormURL;
	pContext->pCommandProcessURL = pProcessURL;
	pContext->bNeedToDoAccessLevelChecks = bNeedsToDoAccessLevelChecks;

	eaPush(&pContext->internalState.ppXPathSegments, strdup(pCurrentXPath));
}

void shutdownWriteHTMLContext(WriteHTMLContext *pContext)
{
	pContext->pUrlArgs = NULL; // We don't own this
	StructDeInit(parse_WriteHTMLContext, pContext);
}

//given a WriteHTMLContext, returns the string name of the server type that is being browsed
//(ie, "GameServer")
char *GetServerTypeNameFromHTMLContext(WriteHTMLContext *pContext)
{
	static char retString[64];

	const char *pXPath = urlFindSafeValue(pContext->pUrlArgs, "xpath");

	if (pXPath)
	{
		char *pFirstLeftBracket;
		strcpy_trunc(retString, pXPath);

		pFirstLeftBracket = strchr(retString, '[');

		if (pFirstLeftBracket)
		{
			*pFirstLeftBracket = 0;
		}

	}
	else
	{
		sprintf(retString, "Unknown Server Type");
	}

	return retString;
}


//-------------------------------------------------------------------------------------------------------
U32 DEFAULT_LATELINK_TextParserHTML_GetTime(void)
{
	return timeSecondsSince2000();
}

// ------------------------------------------------------------------------------------------------------
// Command form generation / handling

typedef void (*MacroDataCallback)(char **estr, 
								  ParseTable *pParseTable, 
								  int iCurrentColumn, 
								  void *pCurrentObject, 
								  int iCurrentIndex, 
								  bool bIsRootStruct,
								  const char *pMacro, 
								  const char *pMacroContents, 
								  void *pUserData);

static bool findInnermostMacro(char *cmd, int *ret_index, int *ret_count)
{
	int parens_count = 0;
	int max_parens = -1;
	int index = 0;

	bool bCounting = false;
	int found_parens = -1;
	int found_index  = -1;
	int found_end    = -1;

	while(cmd[index])
	{
		if(cmd[index] == '(')
		{
			parens_count++;
		}
		else if(cmd[index] == ')')
		{
			parens_count--;

			if(bCounting && (parens_count == found_parens))
			{
				bCounting = false;
				found_end = index;
			}
		}

		if(cmd[index] == '$')
		{
			if(max_parens < parens_count)
			{
				max_parens   = parens_count;

				found_parens = parens_count; 
				found_index  = index;
				found_end    = -1;
				bCounting    = true;
			}
		}

		index++;
	};

	if((found_index != -1)
	&& (found_end   != -1))
	{
		*ret_index = found_index;
		*ret_count = (found_end - found_index) + 1;
		return true;
	}

	return false;
}

static void parseCommandData(char **estr, 
					         const char *cmd, 
							 ParseTable *pParseTable, 
							 int iCurrentColumn, 
							 void *pCurrentObject, 
							 int iCurrentIndex, 
							 bool bIsRootStruct,
							 bool bArrayContext,
							 void *pUserData,
							 MacroDataCallback pMacroDataCallback)
{
	int index = 0;
	int count = 0;
	char *pCmd    = NULL;
	char *pMacro  = NULL;
	char *pOutput = NULL;

	estrStackCreate(&pCmd);
	estrStackCreate(&pMacro);
	estrStackCreate(&pOutput);

	estrCopy2(&pCmd, cmd);
	estrCopy2(&pMacro, "");

	while(findInnermostMacro(pCmd, &index, &count))
	{
		estrClear(&pMacro);
		estrConcat(&pMacro, pCmd+index, count);

		estrCopy2(&pOutput, "");

		// Verify that pMacro is of the form: $MACRO(...)
		// and then rip it up into pieces
		if(count && (pMacro[0] == '$') && (pMacro[count-1] == ')'))
		{
			char *pMacroName = pMacro+1;
			char *pMacroContents = pMacroName;
			while(*pMacroContents)
			{
				if(*pMacroContents == '(')
				{
					*pMacroContents = 0;
					pMacroContents++;
					break;
				}
				pMacroContents++;
			}
			pMacro[count-1] = 0;

			pMacroDataCallback(
				&pOutput, 
				pParseTable, 
				iCurrentColumn, 
				pCurrentObject, 
				iCurrentIndex, 
				bIsRootStruct,
				pMacroName, 
				pMacroContents, 
				pUserData);
		}

		estrRemove(&pCmd, index, count);
		estrInsert(&pCmd, index, pOutput, (int)strlen(pOutput));
	}

	estrCopy(estr, &pCmd);

	estrDestroy(&pOutput);
	estrDestroy(&pMacro);
	estrDestroy(&pCmd);
}

AUTO_STRUCT;
typedef struct FormState
{
	char *pForm;    AST(ESTRING)
	char *pConfirm; AST(ESTRING)
} FormState;


void processFieldMacro(char **estr, 
								 ParseTable *pParseTable, 
								 int iCurrentColumn, 
								 void *pCurrentObject, 
								 int iCurrentIndex, 
								 bool bIsRootStruct,
								 const char *pMacroName, 
								 const char *pMacroContents, 
								 void *pUserData  )
{
	if (bIsRootStruct)
	{
		ParseTable *pFoundTable = NULL;
		int iFoundColumn = 0;
		void *pFoundObject = NULL;
		int iFoundIndex = 0;

		if(objPathResolveField(STACK_SPRINTF(".%s",pMacroContents), pParseTable, pCurrentObject, 
			&pFoundTable, &iFoundColumn, &pFoundObject, &iFoundIndex, OBJPATHFLAG_TRAVERSEUNOWNED))
		{
			char tempBuffer[1024];
			FieldToSimpleString(pFoundTable, iFoundColumn, pFoundObject, iFoundIndex, tempBuffer, 1024, WRITETEXTFLAG_PRETTYPRINT);
			estrPrintf(estr, "%s", tempBuffer);
		}
		else
		{
			estrPrintf(estr, "FAILED");
		}
	}
	else
	{

		int type = TOK_GET_TYPE(pParseTable[iCurrentColumn].type);
		if(type == TOK_STRUCT_X)
		{
			ParseTable *pSubTable = pParseTable[iCurrentColumn].subtable;
			void *pSubObject = TokenStoreGetPointer(pParseTable, iCurrentColumn, pCurrentObject, iCurrentIndex, NULL);

			ParseTable *pFoundTable = NULL;
			int iFoundColumn = 0;
			void *pFoundObject = NULL;
			int iFoundIndex = 0;

			if(objPathResolveField(STACK_SPRINTF(".%s",pMacroContents), pSubTable, pSubObject, 
				&pFoundTable, &iFoundColumn, &pFoundObject, &iFoundIndex, OBJPATHFLAG_TRAVERSEUNOWNED))
			{
				char tempBuffer[1024];
				FieldToSimpleString(pFoundTable, iFoundColumn, pFoundObject, iFoundIndex, tempBuffer, 1024, WRITETEXTFLAG_PRETTYPRINT);
				estrPrintf(estr, "%s", tempBuffer);
			}
			else
			{
				estrPrintf(estr, "FAILED");
			}
		}
		
	}
}

void fieldOnlyMacroDataCallback(char **estr, 
								 ParseTable *pParseTable, 
								 int iCurrentColumn, 
								 void *pCurrentObject, 
								 int iCurrentIndex, 
								 bool bIsRootStruct,
								 const char *pMacroName, 
								 const char *pMacroContents, 
								 void *pUserData)
{
	FormState *pFormState = (FormState *)pUserData;

	if(!strcmp(pMacroName, "FIELD"))
	{
		processFieldMacro(estr, pParseTable, iCurrentColumn, pCurrentObject, iCurrentIndex,
			bIsRootStruct, pMacroName, pMacroContents, pUserData);
	}
}


void createFormMacroDataCallback(char **estr, 
								 ParseTable *pParseTable, 
								 int iCurrentColumn, 
								 void *pCurrentObject, 
								 int iCurrentIndex, 
								 bool bIsRootStruct,
								 const char *pMacroName, 
								 const char *pMacroContents, 
								 void *pUserData)
{
	FormState *pFormState = (FormState *)pUserData;

	if(!strcmp(pMacroName, "CONFIRM"))
	{
		estrCopy2(&pFormState->pConfirm, pMacroContents);
	}
	else if(!strcmp(pMacroName, "STRING") || !strcmp(pMacroName, "INT") || !strcmp(pMacroName, "FLOAT"))
	{
		estrConcatf(&pFormState->pForm, "<tr><td>%s (%s):</td><td><input type=\"text\" name=\"field%s\"></td></tr>\n", 
			pMacroContents, 
			pMacroName, 
			pMacroContents);
	}
	else if(!strcmp(pMacroName, "STRINGBOX"))
	{
		estrConcatf(&pFormState->pForm, "<tr><td>%s (%s):</td><td><textarea rows=20 cols=80 name=\"field%s\"></textarea></td></tr>\n", 
			pMacroContents, 
			pMacroName, 
			pMacroContents);
	}
	else if (!strcmp(pMacroName, "SELECT"))
	{
		char *pFoundBar = strchr(pMacroContents, '|');
		char *pNameToUse = NULL;
		char **ppSelectionChoices = NULL;
		StaticDefineInt *pEnum;
		int i;

		if (!pFoundBar)
		{
			Errorf("Badly formated $SELECT macro (missing | separating text from selection choices): %s", pMacroContents);
			return;
		}

		estrStackCreateSize(&pNameToUse, pFoundBar - pMacroContents);
		estrSetSize(&pNameToUse, pFoundBar - pMacroContents);
		memcpy(pNameToUse, pMacroContents, pFoundBar - pMacroContents);

		DivideString(pFoundBar + 1, ",", &ppSelectionChoices, DIVIDESTRING_POSTPROCESS_NONE);



		estrConcatf(&pFormState->pForm, "<tr><td>%s:</td><td><select name=\"field%s\">\n",
			pNameToUse, pNameToUse);

		//three possible forms of this... either $SELECT(Comment|choice1,choice2,choice3), $SELECT(Comment|NAMELIST_NameListName), or ($SELECT(Comment|ENUM_EnumName)

		if (eaSize(&ppSelectionChoices) == 1 && strStartsWith(ppSelectionChoices[0], "NAMELIST_"))
		{
			char *pNameListName = ppSelectionChoices[0] + 9;
			NameList *pNameList = NameList_FindByName(pNameListName);

			if (pNameList)
			{
				const char *pName;
				pNameList->pResetCB(pNameList);

				while (pName = pNameList->pGetNextCB(pNameList, true))
				{
					estrConcatf(&pFormState->pForm, "<option value=\"%s\">%s</option>\n", pName, pName);
				}
			}
			else
			{
				estrConcatf(&pFormState->pForm, "<option value=\"Unknown namelist %s\">Unknown namelist %s</option>\n", pNameListName, pNameListName);
			}

		}
		else if (eaSize(&ppSelectionChoices) == 1 && strStartsWith(ppSelectionChoices[0], "ENUM_") && (pEnum = FindNamedStaticDefine(ppSelectionChoices[0] + 5)))
		{
			NameList *pNameList = CreateNameList_StaticDefine((StaticDefine*)pEnum);

			const char *pName;
			pNameList->pResetCB(pNameList);

			while (pName = pNameList->pGetNextCB(pNameList, true))
			{
				estrConcatf(&pFormState->pForm, "<option value=\"%s\">%s</option>\n", pName, pName);
			}

			FreeNameList(pNameList);
		}
		else
		{
			for (i=0; i < eaSize(&ppSelectionChoices); i++)
			{
				estrConcatf(&pFormState->pForm, "<option value=\"%s\">%s</option>\n", ppSelectionChoices[i], ppSelectionChoices[i]);
			}
		}

		estrConcatf(&pFormState->pForm, "</select></td></tr>\n");




		eaDestroyEx(&ppSelectionChoices, NULL);
		estrDestroy(&pNameToUse);
	}	
	else if (!strcmp(pMacroName, "CHECKBOX"))
	{
		estrConcatf(&pFormState->pForm, "<tr><td>%s:</td><td><input type=\"checkbox\" name=\"field%s\">\n",
			pMacroContents, pMacroContents);

	}
	else if(!strcmp(pMacroName, "FIELD"))
	{
		processFieldMacro(estr, pParseTable, iCurrentColumn, pCurrentObject, iCurrentIndex,
			bIsRootStruct, pMacroName, pMacroContents, pUserData);
	}
}


void generateCommandMacroDataCallback(char **estr, 
									  ParseTable *pParseTable, 
									  int iCurrentColumn, 
									  void *pCurrentObject, 
									  int iCurrentIndex, 
									  bool bIsRootStruct,
									  const char *pMacroName, 
									  const char *pMacroContents, 
									  void *pUserData)
{
	WriteHTMLContext *pContext = (WriteHTMLContext *)pUserData;

	char *pTempArgName = NULL;
	estrStackCreate(&pTempArgName);

	if(!strcmp(pMacroName, "CONFIRM"))
	{
		// do nothing, this should have already happened
	}
	else if(!strcmp(pMacroName, "STRING") || !strcmp(pMacroName, "STRINGBOX") || !strcmp(pMacroName, "INT") || !strcmp(pMacroName, "FLOAT"))
	{

		const char *pValue;
		
		estrPrintf(&pTempArgName, "field%s", pMacroContents);
		pValue = urlFindSafeValue(pContext->pUrlArgs, pTempArgName);

	

		if (strcmp(pMacroName, "STRINGBOX") == 0)
		{
			estrPrintf(estr, "\"");
			estrAppendEscaped(estr, pValue);
			estrConcatf(estr, "\"");
		}
		else
		{
			if (strcmp(pMacroName, "INT") == 0 && strcmp(pValue, "") == 0)
			{
				pValue = "0";
			}
			else if (strcmp(pMacroName, "FLOAT") == 0 && strcmp(pValue, "") == 0)
			{
				pValue = "0.0";
			}
			else if (strcmp(pMacroName, "STRING") == 0 && strcmp(pValue, "") == 0)
			{
				pValue = "\"\"";
			}
	
			estrPrintf(estr, "%s", pValue);
		}


	}
	else if (!strcmp(pMacroName, "SELECT"))
	{
		char *pFoundBar = strchr(pMacroContents, '|');
		char *pNameToUse = NULL;
		const char *pValue;

		if (!pFoundBar)
		{
			Errorf("Badly formated $SELECT macro (missing | separating text from selection choices): %s", pMacroContents);
			return;
		}

		estrStackCreateSize(&pNameToUse, pFoundBar - pMacroContents);
		estrSetSize(&pNameToUse, pFoundBar - pMacroContents);
		memcpy(pNameToUse, pMacroContents, pFoundBar - pMacroContents);
		estrPrintf(&pTempArgName, "field%s", pNameToUse);
		pValue = urlFindSafeValue(pContext->pUrlArgs, pTempArgName);

		estrPrintf(estr, "%s", pValue);

		estrDestroy(&pNameToUse);
	}
	else if (!strcmp(pMacroName, "CHECKBOX"))
	{
		const char *pValue;
		estrPrintf(&pTempArgName, "field%s", pMacroContents);
		pValue = urlFindValue(pContext->pUrlArgs, pTempArgName);

		if (pValue && stricmp(pValue, "on") == 0)
		{
			estrPrintf(estr, "1");
		}
		else
		{
			estrPrintf(estr, "0");
		}
	}
	else if(!strcmp(pMacroName, "FIELD"))
	{
		processFieldMacro(estr, pParseTable, iCurrentColumn, pCurrentObject, iCurrentIndex,
			bIsRootStruct, pMacroName, pMacroContents, pUserData);

	}

	estrDestroy(&pTempArgName);
}

void ResolveSimpleFieldMacrosIntoEstring(char **estr,
	const char *pInString,
	 ParseTable *pParseTable,
	 int iCurrentColumn,
	 void *pCurrentObject,
	 int iCurrentIndex,
	 bool bIsRootStruct,
	 WriteHTMLContext *pContext)
{
	FormState formState = {0};

	parseCommandData(
		estr, 
		pInString, 
		pParseTable, 
		iCurrentColumn, 
		pCurrentObject, 
		iCurrentIndex, 
		bIsRootStruct,
		pContext->bArrayContext, 
		&formState,
		fieldOnlyMacroDataCallback);

	StructDeInit(parse_FormState, &formState);
}

void ParserWriteConfirmationForm(char **estr,
								 const char *cmd,
								 ParseTable *pParseTable,
								 int iCurrentColumn,
								 void *pCurrentObject,
								 int iCurrentIndex,
								 bool bIsRootStruct,
								 WriteHTMLContext *pContext)
{
	FormState formState = {0};

	parseCommandData(
		estr, 
		cmd, 
		pParseTable, 
		iCurrentColumn, 
		pCurrentObject, 
		iCurrentIndex, 
		bIsRootStruct,
		pContext->bArrayContext, 
		&formState,
		createFormMacroDataCallback);

	// Yes, use Printf, we don't actually want the results in estr
	estrPrintf(estr, "<form method=POST action=\"%s\">\n", 
		pContext->pCommandProcessURL ? pContext->pCommandProcessURL : "/process");

	if(formState.pForm)
	{
		estrConcatf(estr, "<table>\n");

		estrConcatf(estr, "%s", formState.pForm);
		estrConcatf(estr, "</table>\n");
	}

	if(formState.pConfirm)
	{
		estrConcatf(estr, "<b>%s</b><br>\n", formState.pConfirm);
		estrConcatf(estr, "<input type=button onclick=\"history.back()\" name=\"confirm\" value=\"No\"> --- ");
		estrConcatf(estr, "<input type=submit name=\"confirm\" value=\"Yes\"><br>\n");
	}
	else
	{
		estrConcatf(estr, "<input type=hidden name=\"confirm\" value=\"Yes\"><br>\n");
		estrConcatf(estr, "<input type=submit name=\"ignored\" value=\"Go!\"><br>\n");
	}

	estrConcatf(estr, "<input type=hidden name=\"xpath\" value=\"%s\">", urlFindSafeValue(pContext->pUrlArgs, "xpath"));
	estrConcatf(estr, "<input type=hidden name=\"cmd\" value=\"%s\">", urlFindSafeValue(pContext->pUrlArgs, "cmd"));
	estrConcatf(estr, "<input type=hidden name=\"oldxpath\" value=\"%s\">", urlFindSafeValue(pContext->pUrlArgs, "oldxpath"));

	estrConcatf(estr, "</form>\n");

	StructDeInit(parse_FormState, &formState);
}


void ParserGenerateCommand(char **estr,
						   const char *cmd,
						   ParseTable *pParseTable,
						   int iCurrentColumn,
						   void *pCurrentObject,
						   int iCurrentIndex,
						   bool bIsRootStruct,
						   WriteHTMLContext *pContext)
{
	parseCommandData(
		estr, 
		cmd, 
		pParseTable, 
		iCurrentColumn, 
		pCurrentObject, 
		iCurrentIndex, 
		bIsRootStruct,
		pContext->bArrayContext, 
		pContext,
		generateCommandMacroDataCallback);
}

// ------------------------------------------------------------------------------------------------------

static bool CommandFieldIsHidden(ParseTable tpi[], int iColumn, void *pStructPtr)
{
	const char *pCmdString = NULL;

	if (TOK_GET_TYPE(tpi[iColumn].type) == TOK_COMMAND)
	{
		pCmdString = (const char *)(tpi[iColumn].param);
	}
	else
	{
		if (!pStructPtr)
		{
			return false;
		}

		pCmdString = TokenStoreGetString(tpi, iColumn, pStructPtr, 0, NULL);
	}

	if (pCmdString)
	{
		ANALYSIS_ASSUME(pCmdString);
		if (strstri(pCmdString, "$HIDE"))
		{
			return true;
		}
	}

	return false;
}

static bool tpiHasCommands(ParseTable *tpi, void *pStructPtr)
{
	int i;

	FORALL_PARSETABLE(tpi, i)
	{	
		if (TOK_GET_TYPE(tpi[i].type) == TOK_COMMAND
			|| GetBoolFromTPIFormatString(&tpi[i], "command"))
		{
			if (CommandFieldIsHidden(tpi, i, pStructPtr))
			{
				continue;
			}

			return true;
		}
	}

	return false;
}

static void estrPrintCurrentXPath(char **estr, WriteHTMLContext *pContext, bool bUseOverrideLinkIfPossible)
{
	bool bFront = true;
	int iStartingSegmentNum = -1;

	estrCopy2(estr, "");

	if (bUseOverrideLinkIfPossible && pContext->internalState.pOverrideLink && pContext->internalState.iXpathDepthOfOverrideLink > 0)
	{
		estrCopy2(estr, pContext->internalState.pOverrideLink);
		iStartingSegmentNum = pContext->internalState.iXpathDepthOfOverrideLink - 1;
		bFront = false;
	}
	
	FOR_EACH_IN_EARRAY_FORWARDS(pContext->internalState.ppXPathSegments, char, pStr)
	{
		if (ipStrIndex >= iStartingSegmentNum)
		{
			if(!bFront && strcmp(*estr, ".") && pStr && pStr[0] != '[')
			{
				estrConcatf(estr, ".");
			}

			bFront = false;
			estrConcatf(estr, "%s", pStr);
		}
	}
	FOR_EACH_END;
	
}


static void estrConcatCurrentXPath(char **estr, WriteHTMLContext *pContext)
{
	bool bFront = true;

	FOR_EACH_IN_EARRAY_FORWARDS(pContext->internalState.ppXPathSegments, char, pStr)
	{
		if(!bFront && strcmp(*estr, ".") && pStr && pStr[0] != '[')
		{
			estrConcatf(estr, ".");
		}

		bFront = false;
		estrConcatf(estr, "%s", pStr);
	}
	FOR_EACH_END;
}


// Generate a link to this tpi/column/index
void outputTPILink(ParseTable tpi[], int column, void* structptr, int index, FILE* out, WriteHTMLContext *pContext)
{
	bool bParentGeneratingTable = writeHTMLContextIsGeneratingTable(pContext);
	char *pTempString = NULL;
	char *pTempURL = NULL;
	char *url_esc = NULL;
	TextParserResult ignoredResult = 0;

	estrStackCreate(&pTempString);
	estrStackCreate(&pTempURL);
	estrStackCreate(&url_esc);

	estrPrintCurrentXPath(&pTempURL, pContext, false);

	if(bParentGeneratingTable)
		WriteString(out, "<td>", 0, 0);

	estrPrintf(&pTempString, "[<a href=\"%s?xpath=", 
				pContext->pViewURL ? pContext->pViewURL : "/view");
	urlEscape(pTempURL, &url_esc, true, false);
	estrConcatf(&pTempString, "%s", url_esc);
	estrConcatf(&pTempString, "\">%s", tpi[column].name);

	if (TokenStoreStorageTypeIsAnArray(TokenStoreGetStorageType(tpi[column].type)))
	{
		estrConcatf(&pTempString, "(%d elements)", TokenStoreGetNumElems(tpi, column, structptr, &ignoredResult));
	}

	estrConcatf(&pTempString, "</a>]");
	WriteString(out, pTempString, 0, 0);

	if(bParentGeneratingTable)
		WriteString(out, "</td>", 0, 0);

	estrDestroy(&pTempString);
	estrDestroy(&pTempURL);
	estrDestroy(&url_esc);
}

// ------------------------------------------------------------------------------------------------------
// Sorting Routines

typedef struct InternalTokenComparatorData
{
	ParseTable *tpi;
	int iColumn;

	ParseTable *header_tpi;
	int iSortColumn;

	void *structptr;

	bool bAscending;

} InternalTokenComparatorData;

//given a string of HTML, finds the part that is visible to the user. Typically gets an input like
//"<a href="/viewxpath?xpath=Controller[1].machine[0]">blade01rack4</a>
//and returns blade01rack4
//(just returns a pointer inside the source string and a length
void FindVisiblePartOfHTMLString(const char *pInString, const char **ppOutString, int *piOutStrLength)
{
	char *pFirstGT;
	char *pNextLT;

	if (!pInString)
	{
		pInString = "";
	}

	*ppOutString = pInString;
	*piOutStrLength = (int)strlen(pInString);

	pFirstGT = strchr(pInString, '>');

	if (!pFirstGT)
	{
		return;
	}

	pNextLT = strchr(pFirstGT + 1, '<');

	if (!pNextLT)
	{
		return;
	}

	*ppOutString = pFirstGT + 1;
	*piOutStrLength = pNextLT - pFirstGT - 1;
}
int internalTokenComparator(const int *pi1, const int *pi2, const InternalTokenComparatorData *pData)
{
	void *pOuter1 = TokenStoreGetPointer(pData->tpi, pData->iColumn, pData->structptr, *pi1, NULL);
	void *pOuter2 = TokenStoreGetPointer(pData->tpi, pData->iColumn, pData->structptr, *pi2, NULL);
	int iCompareMult = -1;

	if(pData->bAscending)
	{
		iCompareMult = 1;
	}

	//strings go backwards from everything else, so that we start by sorting with high numbers (usually more interesting), 
	//but sort strings from A to Z
	if (TOK_GET_TYPE(pData->header_tpi[pData->iSortColumn].type) == TOK_STRING_X)
	{
		iCompareMult *= -1;
	}
	

	//special case for HTML strings (ie, links), where we want to sort by the visible part of the link, not the
	//whole link string
	if (TOK_GET_TYPE(pData->header_tpi[pData->iSortColumn].type) == TOK_STRING_X && GetBoolFromTPIFormatString(pData->header_tpi + pData->iSortColumn, "html"))
	{
		char *pStr1Start;
		int iStr1Size;

		char *pStr2Start;
		int iStr2Size;

		int iResult;

		FindVisiblePartOfHTMLString(TokenStoreGetString(pData->header_tpi, pData->iSortColumn, pOuter1, 0, NULL), &pStr1Start, &iStr1Size);
		FindVisiblePartOfHTMLString(TokenStoreGetString(pData->header_tpi, pData->iSortColumn, pOuter2, 0, NULL), &pStr2Start, &iStr2Size);

		iResult = strnicmp(pStr1Start, pStr2Start, MIN(iStr1Size, iStr2Size));

		if (iResult == 0)
		{
			if (iStr1Size > iStr2Size)
			{
				iResult = 1;
			}
			else if (iStr2Size > iStr1Size)
			{
				iResult = -1;
			}
		}

		return iResult * iCompareMult;
	}
	else
	{
		pOuter1 = TokenStoreGetPointer(pData->tpi, pData->iColumn, pData->structptr, *pi1, NULL);
		pOuter2 = TokenStoreGetPointer(pData->tpi, pData->iColumn, pData->structptr, *pi2, NULL);
	}
	


	return (iCompareMult * TokenCompare(pData->header_tpi, pData->iSortColumn, pOuter1, pOuter2, 0, 0));
}

// ------------------------------------------------------------------------------------------------------

// Command HTML Link generation
bool command_writehtmlfile(ParseTable tpi[], int column, void* structptr, int index, FILE* out, WriteHTMLContext *pContext)
{
	WriteString(out, "Command", 0, 1);
	return true;
}


void WriteHTMLKbytes(FILE* out, S64 iValue)
{
	char *pOutString = 0;

	estrStackCreate(&pOutString);

	if (iValue < 1024)
	{
		estrPrintf(&pOutString, "%d KB", (int)iValue);
	}
	else if (iValue < 1024 * 1024)
	{
		estrPrintf(&pOutString, "%.2f MB", ((float)iValue) / 1024.0f );
	}
	else
	{
		estrPrintf(&pOutString, "%.2f GB", ((float)iValue) / (float)(1024 * 1024));
	}

	WriteString(out, pOutString, 0, 0);

	estrDestroy(&pOutString);
}

void WriteHTMLBytes(FILE* out, S64 iValue)
{
	char *pOutString = 0;

	estrStackCreate(&pOutString);

	estrMakePrettyBytesString(&pOutString, iValue);

	WriteString(out, pOutString, 0, 0);

	estrDestroy(&pOutString);
}

//macro to make %d second%s print out "1 second" but "2 seconds"
#define PLURALIZE(x) x, (x) == 1 ? "" : "s"

bool int_writehtmlfile(ParseTable tpi[], int column, void* structptr, int index, FILE* out, WriteHTMLContext *pContext)
{
	if (  GetBoolFromTPIFormatString(&tpi[column], "HTML_KBYTES") )
	{
		int value = TokenStoreGetInt(tpi, column, structptr, index, NULL);	

		WriteHTMLKbytes(out, (S64)value);

		return true;
	}
	else if (  GetBoolFromTPIFormatString(&tpi[column], "HTML_BYTES") )
	{
		int value = TokenStoreGetInt(tpi, column, structptr, index, NULL);	

		WriteHTMLBytes(out, (S64)value);

		return true;
	}
	else if ( GetBoolFromTPIFormatString(&tpi[column], "HTML_SECS") )
	{
		char timeString[1024];
		U32 iValue = TokenStoreGetInt(tpi, column, structptr, index, NULL);	
		if (iValue)
		{
			timeMakeLocalDateStringFromSecondsSince2000(timeString, iValue);
		}
		else
		{
			sprintf(timeString, "never");
		}

		WriteString(out, timeString, 0, 0);
		return true;
	}
	else if ( GetBoolFromTPIFormatString(&tpi[column], "HTML_SECS_AGO") )
	{
		U32 iValue = TokenStoreGetInt(tpi, column, structptr, index, NULL);	
		int iSecsAgo = TextParserHTML_GetTime() - iValue;
		char *pSecsAgoString = NULL;

		estrStackCreate(&pSecsAgoString);

		if (iValue == 0)
		{
			estrPrintf(&pSecsAgoString, "never");
		}
		else
		{
			char *pInnerString = NULL;
			estrStackCreate(&pInnerString);
			timeSecondsDurationToPrettyEString(iSecsAgo, &pInnerString);
			estrConcatf(&pInnerString, " ago");
			estrPrintf(&pSecsAgoString, "<span title=\"%s\">%s</span>", timeGetLocalDateStringFromSecondsSince2000(iValue), pInnerString);
			estrDestroy(&pInnerString);
		}
			

		WriteString(out, pSecsAgoString, 0, 0);

			

		estrDestroy(&pSecsAgoString);
		return true;
	}
	else if ( GetBoolFromTPIFormatString(&tpi[column], "HTML_SECS_AGO_SHORT") )
	{
		U32 iValue = TokenStoreGetInt(tpi, column, structptr, index, NULL);	
		int iSecsAgo = TextParserHTML_GetTime() - iValue;
		char *pSecsAgoString = NULL;

		estrStackCreate(&pSecsAgoString);

		if (iValue == 0)
		{
			estrPrintf(&pSecsAgoString, "never");
		}
		else
		{	
			char *pInnerString = NULL;
			estrStackCreate(&pInnerString);
			timeSecondsDurationToShortEString(iSecsAgo, &pInnerString);
			estrPrintf(&pSecsAgoString, "<span title=\"%s\">%s</span>", timeGetLocalDateStringFromSecondsSince2000(iValue), pInnerString);
			estrDestroy(&pInnerString);
		}
			

		WriteString(out, pSecsAgoString, 0, 0);

		estrDestroy(&pSecsAgoString);
		return true;
	}
	else if ( GetBoolFromTPIFormatString(&tpi[column], "HTML_SECS_DURATION") )
	{
		U32 iValue = TokenStoreGetInt(tpi, column, structptr, index, NULL);	
		char *pDurationString = NULL;

		estrStackCreate(&pDurationString);

		timeSecondsDurationToPrettyEString(iValue, &pDurationString);

		

		WriteString(out, pDurationString, 0, 0);

		estrDestroy(&pDurationString);
		return true;
	}
	else if ( GetBoolFromTPIFormatString(&tpi[column], "HTML_SECS_DURATION_SHORT") )
	{
		U32 iValue = TokenStoreGetInt(tpi, column, structptr, index, NULL);	
		char *pDurationString = NULL;

		estrStackCreate(&pDurationString);

		timeSecondsDurationToShortEString(iValue, &pDurationString);

		

		WriteString(out, pDurationString, 0, 0);

		estrDestroy(&pDurationString);
		return true;
	}
	else if ( GetBoolFromTPIFormatString(&tpi[column], "HTML_IP") )
	{
		U32 iValue = TokenStoreGetInt(tpi, column, structptr, index, NULL);	
		WriteString(out, makeIpStr(iValue), 0, 0);
		return true;
	}
	else
	{
		int iOffset = ftell(out);
		int_writetext(out, tpi, column, structptr, index, 0, 0, 0, 0, 0, 0);
		return (iOffset != ftell(out));
	}
}


bool int64_writehtmlfile(ParseTable tpi[], int column, void* structptr, int index, FILE* out, WriteHTMLContext *pContext)
{
	if (  GetBoolFromTPIFormatString(&tpi[column], "HTML_KBYTES") )
	{
		S64 value = TokenStoreGetInt64(tpi, column, structptr, index, NULL);	

		WriteHTMLKbytes(out, value);

		return true;
	}
	else if (  GetBoolFromTPIFormatString(&tpi[column], "HTML_BYTES") )
	{
		S64 value = TokenStoreGetInt64(tpi, column, structptr, index, NULL);	

		WriteHTMLBytes(out, value);

		return true;
	}
	else if ( GetBoolFromTPIFormatString(&tpi[column], "HTML_TICKS_DURATION"))
	{

		char *pDurationString = NULL;
		S64 iTicks = TokenStoreGetInt64(tpi, column, structptr, index, NULL);	

		estrStackCreate(&pDurationString);

		timeTicksToPrettyEString(iTicks, &pDurationString);

		WriteString(out, pDurationString, 0, 0);

		estrDestroy(&pDurationString);
		return true;


	}
	else
	{
		int iOffset = ftell(out);
		int64_writetext(out, tpi, column, structptr, index, 0, 0, 0, 0, 0, 0);
		return (iOffset != ftell(out));
	}
}


bool float_writehtmlfile(ParseTable tpi[], int column, void* structptr, int index, FILE* out, WriteHTMLContext *pContext)
{
	int iPrecision;

	if (  GetBoolFromTPIFormatString(&tpi[column], "HTML_PERCENT") )
	{
		float value = TokenStoreGetF32(tpi, column, structptr, index, NULL) * 100.0f;	
		char *pOutString = 0;

		estrStackCreate(&pOutString);


		if (value > 100.0f)
		{
			value = 100.0f;
		}
		else if (value < 0.0f)
		{
			value = 0.0f;
		}

		if (value > 2.0f)
		{
			int iValue = (int)value;

			estrPrintf(&pOutString, "%d%%", iValue );
		}
		else
		{
			estrPrintf(&pOutString, "%.2f%%", value);
		}

		WriteString(out, pOutString, 0, 0);

		estrDestroy(&pOutString);


		return true;
	}
	else if (GetIntFromTPIFormatString(&tpi[column], "HTML_FLOAT_PREC", &iPrecision))
	{
		int iOffset = ftell(out);
		float value = TokenStoreGetF32(tpi, column, structptr, index, NULL);	

		//none of the printf format specifiers do precisely what I want here, which is to always have two digits
		//to the right of the decimal point, but suppress trailing zeros.
		int iMainPart = (int)value;
		int iDecimal = (value - (float)iMainPart) * pow(10, iPrecision);

		if (!iDecimal)
		{
			fprintf(out, "%d", iMainPart);
		}
		else
		{
			while (iDecimal % 10 == 0)
			{
				iDecimal /= 10;
			}

			fprintf(out, "%d.%d", iMainPart, iDecimal);
		}

		return (iOffset != ftell(out));



	}
	else
	{
		int iOffset = ftell(out);
		float_writetext(out, tpi, column, structptr, index, 0, 0, 0, 0, 0, 0);
		return (iOffset != ftell(out));
	}
}


bool FindOverrideLink(ParseTable *pTPI, void *pStructData, char **ppOutEString)
{
	int i;
	FORALL_PARSETABLE(pTPI, i)
	{
		if (GetBoolFromTPIFormatString(&pTPI[i], "HTML_LINKOVERRIDE"))
		{
			char *pSuffix;

			FieldWriteText(pTPI, i, pStructData, 0, ppOutEString, 0);

			//need to strip away all the href<> business
			estrRemoveUpToFirstOccurrence(ppOutEString, '"');
			estrRemoveUpToFirstOccurrence(ppOutEString, '=');
			estrTruncateAtFirstOccurrence(ppOutEString, '"');

			if (GetStringFromTPIFormatString(&pTPI[i], "HTML_LINKOVERRIDE_SUFFIX", &pSuffix))
			{
				estrConcatf(ppOutEString, "%s", pSuffix);
			}

			return true;
		}
	}
			
	return false;
}


//
//	Write out the link tag (and XML) in the ServerMon UI.
//
static void writeCommandURL(FILE* out, ParseTable *tpi, void *pStructData, int column, char *pCommandString, WriteHTMLContext *pContext)
{
	char *pTempString = NULL;
	char *pBuildStr = NULL;
	char *pTempPath = NULL;
	char *escapedURL = NULL;

	U32 hash[4];
	char *pElementID = NULL;
	char *pFoundCommandName;
	char commandNameToUse[1024];


	estrStackCreate(&pElementID);
	estrStackCreate(&pBuildStr);
	estrStackCreate(&pTempString);
	estrStackCreate(&pTempPath);
	estrStackCreate(&escapedURL);

	//for constructed TPIs, such as the return values from filterlists and hybrid objects, the path
	//that a pure traversal will find will be incorrect. For isntance, it might be controller[0].globObj.alerts.list[0],
	//when we really want controller[0].globObj.alerts[5] (if, for instance, the alert with key 5 is currently in the 0th
	//position in the fake subobject "list". FindOverridePath finds if the struct has an override path built into it
	if (!FindOverrideLink(tpi, pStructData, &pTempPath))
	{
		estrPrintCurrentXPath(&pTempPath, pContext, true);
	}

	// TODO: urlEscape this stuff, write /command's handler

	//if we have a non-NULL command string, and if it has the magic token $COMMANDNAME in it, then we get the command name
	//from it. Otherwise, get it from the tpi's name field
	if (pCommandString && ((pFoundCommandName = strstr(pCommandString, "$COMMANDNAME("))))
	{
		char *pRightParens;
		pFoundCommandName += 13;
		pRightParens = strchr(pFoundCommandName, ')');
		if (pRightParens)
		{
			*pRightParens = 0;
		}

		strcpy(commandNameToUse, pFoundCommandName);


		if (pRightParens)
		{
			*pRightParens = ')';
		}

	}
	else
	{
		strcpy(commandNameToUse, tpi[column].name); 
	}
		
	urlEscape(commandNameToUse, &escapedURL, true, false);


	//create an html element ID
	estrPrintf(&pBuildStr, "%s%s", escapedURL, pTempPath);
	cryptMD5((U8*)pBuildStr, (int) strlen(pBuildStr), hash);
	estrPrintf(&pElementID, "%X%X%X%X",hash[0],hash[1],hash[2],hash[3]);

	//print the html link (this will be superceded by the xml handled version)
	estrPrintf(&pTempString, "<div class=\"commanddiv\" id=\"div-%s\">[<a href=\"%s?cmd=%s&xpath=%s&oldxpath=%s\"%s>%s</a>]", 
		pElementID,
		pContext->pCommandFormURL ? pContext->pCommandFormURL : "/command",
		escapedURL, 
		pTempPath, 
		urlFindSafeValue(pContext->pUrlArgs, "xpath"),
		GetBoolFromTPIFormatString(&tpi[column], "HTML_COMMAND_IN_NEW_WINDOW" ) ? "target=\"_blank\"" : "",
		commandNameToUse);
	
	//Create the xml for this command.
	//Spacing is trivially important here; if you put space around the command tag you'll get a text element child as firstChild.
	estrPrintf(&pBuildStr, "\
<command class=\"command\"\
		action=\"%s\"\
		cmd=\"%s\"\
		xpath=\">%s\"\
		oldxpath=\"%s\"\
		name=\"%s\"\
		href=\"%s?cmd=%s&xpath=%s&oldxpath=%s\"\
		id=\"cmd-%s\"\
/>\
</div>\
",
		pContext->pCommandFormURL ? pContext->pCommandFormURL : "/command",
		escapedURL,
		pTempPath,
		urlFindSafeValue(pContext->pUrlArgs, "xpath"),
		commandNameToUse,
		pContext->pCommandFormURL ? pContext->pCommandFormURL : "/command", escapedURL, pTempPath, urlFindSafeValue(pContext->pUrlArgs, "xpath"),
		pElementID);

	//print the xml
	estrConcat(&pTempString, pBuildStr, (unsigned int)strlen(pBuildStr));

	WriteString(out, pTempString, 0, 1);

	estrDestroy(&pElementID);
	estrDestroy(&pBuildStr);
	estrDestroy(&pTempString);
	estrDestroy(&pTempPath);
	estrDestroy(&escapedURL);
}


void WriteFilteringAndSearchingForm(char *pKey, ParseTable *pTPI, FILE *out, WriteHTMLContext *pContext)
{
	char *pInputHTMLString = NULL;
	const char *pFieldsToShow_PrevText, *pFilter_PrevText;
	char *pCurFieldsToShowName = NULL;
	char *pCurFilterName = NULL;

	estrStackCreate(&pInputHTMLString);
	estrStackCreate(&pCurFieldsToShowName);
	estrStackCreate(&pCurFilterName);

	estrPrintf(&pCurFieldsToShowName, "%sFieldsToShow", pKey);
	estrPrintf(&pCurFilterName, "%sFilter", pKey);

	pFieldsToShow_PrevText = urlFindValue(pContext->pUrlArgs, pCurFieldsToShowName);
	pFilter_PrevText = urlFindValue(pContext->pUrlArgs, pCurFilterName);


	estrPrintf(&pInputHTMLString, "<form name=\"input_%s\" method=\"get\">\n", pKey);
	estrConcatf(&pInputHTMLString, "Fields To Show <input type=\"text\" name=\"%s\" size=100 ", pCurFieldsToShowName);
	if (pFieldsToShow_PrevText && pFieldsToShow_PrevText[0])
	{
		char *pFieldsToShow_Prev_Escaped = NULL;

		estrStackCreate(&pFieldsToShow_Prev_Escaped);
		estrCopyWithHTMLEscaping(&pFieldsToShow_Prev_Escaped, pFieldsToShow_PrevText, false);
		estrConcatf(&pInputHTMLString, "value=\"%s\" ", pFieldsToShow_Prev_Escaped);

		estrDestroy(&pFieldsToShow_Prev_Escaped);
	}
	estrConcatf(&pInputHTMLString, "><br>\n");

	estrConcatf(&pInputHTMLString, "<span>Filter <input type=\"text\" name=\"%s\" size=100 ", pCurFilterName);
	if (pFilter_PrevText && pFilter_PrevText[0])
	{
		char *pFilter_Prev_Escaped = NULL;

		estrStackCreate(&pFilter_Prev_Escaped);

		estrCopyWithHTMLEscaping(&pFilter_Prev_Escaped, pFilter_PrevText, false);
		estrConcatf(&pInputHTMLString, "value=\"%s\" ", pFilter_Prev_Escaped);

		estrDestroy(&pFilter_Prev_Escaped);
	}
	estrConcatf(&pInputHTMLString, "><br>\n");

	estrConcatf(&pInputHTMLString, "<INPUT type=\"submit\">\n");

	estrConcatf(&pInputHTMLString, "<input type=hidden name=\"xpath\" value=\"%s\">", urlFindSafeValue(pContext->pUrlArgs, "xpath"));
	estrConcatf(&pInputHTMLString, "</span></form>\n");

	WriteString(out, pInputHTMLString, 0, 0);

	estrDestroy(&pInputHTMLString);
	estrDestroy(&pCurFieldsToShowName);
	estrDestroy(&pCurFilterName);
}

//tries to get a matching HTML_CLASS_IFEXPR string
void AttemptToGetIfExprClassNameString(ParseTable *tpi, int iColumn, void *pStruct, int iIndex, char **ppIfExprClassNameString)
{
	char *pRawIFEXPRString;
	
	char **ppStrings = NULL;
	int iNumStrings;
	int i;
	char *pValueString = NULL;
	char *pExpressionString = NULL;


	if (!GetStringFromTPIFormatString(tpi + iColumn, "HTML_CLASS_IFEXPR", &pRawIFEXPRString))
	{
		return;
	}

	estrStackCreate(&pExpressionString);
	estrStackCreate(&pValueString);

	DivideString(pRawIFEXPRString, ";", &ppStrings, 0);

	iNumStrings = eaSize(&ppStrings);

	assertmsgf(iNumStrings % 2 == 0, "HTML_CLASS_IFEXPR must have an even number of strings (expression-value pairs)");


	//SECS_AGO fields contain an absolute time, not seconds ago
	if ( GetBoolFromTPIFormatString(&tpi[iColumn], "HTML_SECS_AGO") || GetBoolFromTPIFormatString(&tpi[iColumn], "HTML_SECS_AGO_SHORT") )
	{
		U32 iValue = TokenStoreGetInt(tpi, iColumn, pStruct, iIndex, NULL);	
		int iSecsAgo = TextParserHTML_GetTime() - iValue;

		estrPrintf(&pValueString, "%d", iSecsAgo);
	}
	else
	{
		FieldWriteText(tpi, iColumn, pStruct, iIndex, &pValueString, 0);
	}

	for (i=0; i < iNumStrings / 2; i++)
	{
		char *pStyleString = ppStrings[i * 2 + 1];

		estrClear(&pExpressionString);
		estrAppendUnescaped(&pExpressionString, ppStrings[i * 2]);

		estrTrimLeadingAndTrailingWhitespace(&pExpressionString);

		//special case if the expression is just "$", treat empty or "0" as false, everything else as true... useful
		//for strings, for instance, where you want the style to show up if there's anything there
		if (stricmp(pExpressionString, "$") == 0)
		{
			if (strlen(pValueString) == 0 || stricmp(pValueString, "0") == 0)
			{
				//do nothing
			}
			else
			{
				estrCopy2(ppIfExprClassNameString, pStyleString);
				break;
			}
		}
		else
		{

			estrReplaceOccurrences(&pExpressionString, "$", pValueString);

			if (exprEvaluateRawString(pExpressionString))
			{
				estrCopy2(ppIfExprClassNameString, pStyleString);
				break;
			}
		}
	}

	eaDestroyEx(&ppStrings, NULL);
	estrDestroy(&pExpressionString);
	estrDestroy(&pValueString);


}

		
static void GetNoteNameFromHTMLContext(WriteHTMLContext *pContext, char **ppOutString)
{
	char *pCurSeg = NULL;
	int i;

	estrStackCreate(&pCurSeg);

	for (i = 0; i < eaSize(&pContext->internalState.ppXPathSegments); i++)
	{
		char *pLeftBrack, *pRightBrack;
	
		estrCopy2(&pCurSeg, pContext->internalState.ppXPathSegments[i]);
		pLeftBrack = strchr(pCurSeg, '[');
		if (pLeftBrack)
		{
			pRightBrack = strchr(pLeftBrack + 1, ']');

			if (pRightBrack)
			{
				estrRemove(&pCurSeg, pLeftBrack - pCurSeg, pRightBrack - pLeftBrack + 1);
			}
		}

		estrConcatf(ppOutString, "%s%s", i == 0 ? "" : ".", pCurSeg);
	}

	estrDestroy(&pCurSeg);
}

// Struct HTML writing
bool ParseWriteHTMLFile(FILE* out, ParseTable *tpi, void* structptr, WriteHTMLContext *pContext)
{
	int i;
	int startedbody = 0;
	ParseTableInfo *pTableInfo = ParserGetTableInfo(tpi);
	char *pTempString = NULL;
	bool bParentGeneratingTable = writeHTMLContextIsGeneratingTable(pContext);
	bool bShowAllColumns = true;
	bool bWroteSomething = bParentGeneratingTable;

	TextParserAutoFixupCB *pFixupCB;

	//initialize cryptMD5 for html element command IDs
	cryptMD5Init();

	pFixupCB = ParserGetTableFixupFunc(tpi);

	if (pFixupCB)
	{
		pFixupCB(structptr, FIXUPTYPE_PRE_TEXT_WRITE, NULL);
	}

	writeHTMLHandleMaxDepthAndCollapsed(0, tpi, 0, structptr, 0, out, pContext);

	writeHTMLContextPushGeneratingTable(pContext, false);


	estrStackCreate(&pTempString);


	//make type-specific header
	//make all auto-generated links
	{
		char *pHeaderCommand;


		if (GetStringFromTPIFormatString(&tpi[0], "HTTP_HEADER_COMMAND", &pHeaderCommand))
		{
			char *pKeyString = NULL;
			char *pFullCommandString = NULL;
			char *pHeaderString = NULL;

			estrStackCreate(&pKeyString);
			estrStackCreate(&pHeaderString);
			estrStackCreate(&pFullCommandString);

			objGetKeyEString(tpi, structptr, &pKeyString);

			if (pKeyString && pKeyString[0])
			{
				estrConcatf(&pFullCommandString, "%s %s", pHeaderCommand, pKeyString);
			}
			else
			{
				estrCopy2(&pFullCommandString, pHeaderCommand);
			}
			
			cmdParseAndReturn(pFullCommandString, &pHeaderString, CMD_CONTEXT_HOWCALLED_HTML_HEADER);

			if (pHeaderString && pHeaderString[0])
			{
				WriteString(out, pHeaderString, 0, 0);
			}

			estrDestroy(&pKeyString);
			estrDestroy(&pHeaderString);
			estrDestroy(&pFullCommandString);
		}
		
	}






		
	// List all possible commands
	if(tpiHasCommands(tpi, structptr))
	{
		bWroteSomething = true;
		if(bParentGeneratingTable)
		{
			estrPrintf(&pTempString, "<td class=\"tdCommands\">");
			WriteString(out, pTempString, 0, 0);
		}
		else
		{
	//		estrPrintf(&pTempString, "<div class=\"structheader\"><span class=\"structheader%s\">Commands</span></div>", tpi[0].name);
	//		WriteString(out, pTempString, 0, 1);

			estrPrintf(&pTempString, "<div class=\"divCommands\">");
			WriteString(out, pTempString, 0, 1);
		}

		FORALL_PARSETABLE(tpi, i)
		{
			char *pExprString;	
			char *pCmdString = NULL;

			if (!(TOK_GET_TYPE(tpi[i].type) == TOK_COMMAND
			|| GetBoolFromTPIFormatString(&tpi[i], "command")))
			{
				continue;
			}

			if (pContext->bNeedToDoAccessLevelChecks && FieldFailsHTMLAccessLevelCheck(tpi, i, structptr))
			{
				continue;
			}

			if (CommandFieldIsHidden(tpi, i, structptr))
			{
				continue;
			}

			//if we have a commandExpr, check if it's true, if not don't
			//put a command link
			if (GetStringFromTPIFormatString(&tpi[i], "commandExpr", &pExprString))
			{
				char *pExpandedString = NULL;
				char *pExpandedUnescapedString = NULL;
				char *pServerReplacedString = NULL;
				int iRetVal;

				estrStackCreate(&pExpandedString);
				estrStackCreate(&pExpandedUnescapedString);
				estrStackCreate(&pServerReplacedString);

				estrCopy2(&pServerReplacedString, pExprString);

				estrReplaceOccurrences(&pServerReplacedString, "$SERVERTYPE", GetServerTypeNameFromHTMLContext(pContext));

				ResolveSimpleFieldMacrosIntoEstring(&pExpandedString, pServerReplacedString, tpi, 0, structptr, 
					0, true, pContext);

				estrAppendUnescaped(&pExpandedUnescapedString, pExpandedString);


				iRetVal = exprEvaluateRawString(pExpandedUnescapedString);

				estrDestroy(&pServerReplacedString);
				estrDestroy(&pExpandedString);
				estrDestroy(&pExpandedUnescapedString);
				
				if (!iRetVal)
				{
					continue;
				}
			}
			
			//if the command string comes from the struct, check if it's nonempty
			if (TOK_GET_TYPE(tpi[i].type) != TOK_COMMAND)
			{
				int iLen;

				estrStackCreate(&pCmdString);

				FieldWriteText(tpi, i, structptr, 0, &pCmdString, 0);

				iLen = estrLength(&pCmdString);

				if (!iLen)
				{
					estrDestroy(&pCmdString);
					continue;
				}
			}

			writeCommandURL(out, tpi, structptr, i, pCmdString, pContext);
			estrDestroy(&pCmdString);
			
		}

		if(bParentGeneratingTable)
		{
			WriteString(out, "</td>", 0, 1);
		}
		else
		{
			WriteString(out, "</div>", 0, 1);
		}
	}



	FORALL_PARSETABLE(tpi, i)
	{
		int ignored;
		bool bShowColumn = GetIntFromTPIFormatString(&tpi[i], "showcolumn", &ignored);

		if(bShowColumn)
		{
			bShowAllColumns = false;
			break;
		}
	}

	// write each field
	FORALL_PARSETABLE(tpi, i)
	{
		char *pRecurseEString = NULL;
		FILE *pRecurseFile;
		bool bRecurseWroteAnything;
		bool bForceTable;
		bool bIsEditable;
		bool bIsSvrParam;

		writeHTMLContinueIfWeDontCareAboutThisEntry(tpi, i, structptr, bParentGeneratingTable);

		if(!bShowAllColumns)
		{
			int ignored;
			bool bShowColumn = GetIntFromTPIFormatString(&tpi[i], "showcolumn", &ignored);

			if(!bShowColumn)
				continue;
		}

		bForceTable = GetBoolFromTPIFormatString( &tpi[i], "HTML_TABLE"  ) && TOK_HAS_SUBTABLE(tpi[i].type)
			&& !TokenStoreStorageTypeIsAnArray(TokenStoreGetStorageType(tpi[i].type));

		bIsSvrParam = GetBoolFromTPIFormatString(&tpi[i], "HTML_SVR_PARAM");


		bIsEditable = (!TOK_HAS_SUBTABLE(tpi[i].type) && tpi[i].type & TOK_PERSIST) || GetBoolFromTPIFormatString(&tpi[i], "EDITABLE");

		pRecurseFile = fileOpenEString(&pRecurseEString);
//		if(pContext->internalState.iDepth) 
		pushXPath(pContext, tpi[i].name, "-1", tpi, structptr);

		pContext->internalState.iDepth++;

		if (bForceTable)
		{
			int j;
			ParseTable *tpi_table_headings = tpi[i].subtable;
			char *pFilterNameString = NULL;
			

			writeHTMLContextPushGeneratingTable(pContext, true);

			if (GetStringFromTPIFormatString(&tpi[i], "TEST_FILTER", &pFilterNameString))
			{
				estrStackCreate(&pFilterNameString);
				WriteFilteringAndSearchingForm(pFilterNameString, &tpi[i], out, pContext);
				estrDestroy(&pFilterNameString);
			
			}


			WriteString(pRecurseFile, "\n<table>\n<tr class=\"heading\">\n", 0, 0);

			if(tpiHasCommands(tpi_table_headings, structptr))
			{
				WriteString(pRecurseFile, "<td class=\"headingCommands\">Commands</td>\n", 0, 1);
			}

			FORALL_PARSETABLE(tpi_table_headings, j)
			{
				writeHTMLContinueIfWeDontCareAboutThisEntry(tpi_table_headings, j, structptr, true);

				if(!bShowAllColumns)
				{
					int ignored;
					bool bShowColumn = GetIntFromTPIFormatString(&tpi_table_headings[j], "showcolumn", &ignored);
					
					if(!bShowColumn)
						continue;
				}

				estrPrintf(&pTempString, "<td class=\"heading%s\">%s</td>", tpi_table_headings[j].name, tpi_table_headings[j].name);

				WriteString(pRecurseFile, pTempString, 0, 0);
			}
			WriteString(pRecurseFile, "</tr>\n", 0, 0);
		}

 		bRecurseWroteAnything = writehtmlfile_autogen(tpi, i, structptr, 0, pRecurseFile, pContext);

		if ( bForceTable  )
		{
			writeHTMLContextPopGeneratingTable(pContext);
			WriteString(pRecurseFile, "</table>\n", 0, 0);
		}


		if (bWroteSomething)
		{
			char *pXPathString = NULL;
			estrPrintf(&pTempString, "<a name=\"");
			estrConcatCurrentXPath(&pTempString, pContext);
			estrConcatf(&pTempString, "\"></a>\n");

			//XXXXXX: these named anchors really can't go here. it borks table layout in certain compliant browsers.
			//WriteString(out, pTempString, 0, 0);
		}

		fileClose(pRecurseFile);


		bWroteSomething |= bRecurseWroteAnything;


		if (bRecurseWroteAnything || bParentGeneratingTable)
		{
			char *pIfExprClassNameString = NULL;
			char *currentXPath = 0;
			char *altPath = 0;
			bool bNoDiv = false;
			bool bWroteNotes = false;
			char *pNotesTitle;

			estrStackCreate(&currentXPath);
			estrStackCreate(&altPath);
			concatXPath(pContext, &currentXPath);
			estrReplaceOccurrences(&currentXPath, "\"", "");

			estrPrintf(&altPath, "");
			if (urlFindSafeValue(pContext->pUrlArgs, "svrAlt")[0] == '1')
			{
				if (estrLength(&currentXPath) > 0)
				{
					estrPrintf(&altPath, " alt='%s'", currentXPath);
				}
			}
			
			AttemptToGetIfExprClassNameString(tpi, i, structptr, 0, &pIfExprClassNameString);


			if(bParentGeneratingTable || bForceTable)
			{
				estrPrintf(&pTempString, "<td class=\"td%s %s\"%s>&nbsp;", tpi[i].name, pIfExprClassNameString ? pIfExprClassNameString : "", altPath);
				WriteString(out, pTempString, 0, 0);
			}
			else if (bIsSvrParam)
			{
				estrPrintf(&pTempString, "<div class=\"svrParam\" id=\"%s\">", tpi[i].name);
				WriteString(out, pTempString, 0, 1);
			}
			else
			{
				char *pClassNameString = NULL;

				if (GetBoolFromTPIFormatString(&tpi[i], "HTML_NO_DIV"))
				{
					bNoDiv = true;
				}
				else if (pIfExprClassNameString)
				{
					estrPrintf(&pTempString, "<div class=\"%s\" id=\"offsetID-%d\"%s>", pIfExprClassNameString, tpi[i].storeoffset, altPath);
				}
				else if (GetStringFromTPIFormatString(&tpi[i], "HTML_CLASS", &pClassNameString))
				{
					estrPrintf(&pTempString, "<div class=\"%s\" id=\"offsetID-%d\"%s>", pClassNameString, tpi[i].storeoffset, altPath);
				}
				else
				{
					estrPrintf(&pTempString, "<div class=\"div%s\" id=\"offsetID-%d\"%s>", tpi[i].name, tpi[i].storeoffset, altPath);
				}
				WriteString(out, pTempString, 0, 1);

				if (!GetBoolFromTPIFormatString(&tpi[i], "HTML_NO_HEADER"))
				{
					estrPrintf(&pTempString, "<div class=\"structheader\" ><span class=\"structheader%s\">%s</span>", tpi[0].name, tpi[i].name);
					if (GetStringFromTPIFormatString(&tpi[i], "HTML_NOTES", &pNotesTitle))
					{
						char *pNoteName = NULL;

						estrStackCreate(&pNoteName);
						GetNoteNameFromHTMLContext(pContext, &pNoteName);
						estrConcatf(&pTempString, "%s", NotesServer_GetLinkToNoteServerMonPage(pNoteName, pNotesTitle, false, false));

						estrDestroy(&pNoteName);

						bWroteNotes = true;
					}
					else if (GetBoolFromTPIFormatString(&tpi[0], "HTML_NOTES_AUTO"))
					{
						char *pNoteName = NULL;
						char *pAutoTitle = NULL;

						estrStackCreate(&pNoteName);
						estrStackCreate(&pAutoTitle);
						
						GetNoteNameFromHTMLContext(pContext, &pNoteName);
						estrPrintf(&pAutoTitle, "Autogenerated note %s", pNoteName);

						estrConcatf(&pTempString, "%s", NotesServer_GetLinkToNoteServerMonPage(pNoteName, pAutoTitle, false, false));

						estrDestroy(&pNoteName);
						estrDestroy(&pAutoTitle);

						bWroteNotes = true;
					}

					estrConcatf(&pTempString, "</div>");
					WriteString(out, pTempString, 0, 1);
				}

			}


			if (bIsEditable)
			{
				char *pEditString = 0;
				estrStackCreate(&pEditString);

				//the way Kelvin wrote this code, it assumes that ".struct" will be in the xpath, which is true when doing filtered
				//searches but not necessarily otherwise, and it won't generate an xpath without it. To make editable
				//fields work when that's not true, I have to stick in this kludge
				if (estrLength(&currentXPath))
				{
					estrPrintf(&pEditString, "<span class=\"editable\" path=\"%s\">&nbsp;", currentXPath);
				}
				else
				{
					char *pTempXPath = NULL;
					estrStackCreate(&pTempXPath);
					concatXPathWithoutStruct(pContext, &pTempXPath);
					estrPrintf(&pEditString, "<span class=\"editable\" path=\"%s\">&nbsp;", pTempXPath);
					estrDestroy(&pTempXPath);
				}



				WriteString(out, pEditString, 0, 0);
				estrDestroy(&pEditString);
			}

			if (pRecurseEString)
			{
				WriteString(out, pRecurseEString, 0, 0);
			}
			
			if (bIsEditable)
			{
				WriteString(out, "&nbsp;</span>",0,0);
			}
			

			if (!bWroteNotes && GetStringFromTPIFormatString(&tpi[i], "HTML_NOTES", &pNotesTitle) && !bParentGeneratingTable)
			{
				char *pNoteName = NULL;

				estrStackCreate(&pNoteName);
				GetNoteNameFromHTMLContext(pContext, &pNoteName);
				WriteString(out, NotesServer_GetLinkToNoteServerMonPage(pNoteName, pNotesTitle, false, false), 0, 0);
				estrDestroy(&pNoteName);

				bWroteNotes = true;
			}
			else if (!bWroteNotes && GetBoolFromTPIFormatString(&tpi[0], "HTML_NOTES_AUTO") && !bParentGeneratingTable)
			{

				char *pNoteName = NULL;
				char *pAutoTitle = NULL;

				estrStackCreate(&pNoteName);
				estrStackCreate(&pAutoTitle);
						
				GetNoteNameFromHTMLContext(pContext, &pNoteName);
				estrPrintf(&pAutoTitle, "Autogenerated note %s", pNoteName);

				WriteString(out, NotesServer_GetLinkToNoteServerMonPage(pNoteName, pAutoTitle, false, false), 0, 0);

				estrDestroy(&pNoteName);
				estrDestroy(&pAutoTitle);

				bWroteNotes = true;
			}

			if (bNoDiv)
			{

			}
			else if(bParentGeneratingTable || bForceTable)
			{
				WriteString(out, "</td>", 0, 1);
			}
			else
			{
				char *pPreDivCloseString;
				if (GetStringFromTPIFormatString(tpi + i, "HTML_PRE_DIV_CLOSE", &pPreDivCloseString))
				{
					char *pUnescaped = NULL;
					estrStackCreate(&pUnescaped);
					estrAppendUnescaped(&pUnescaped, pPreDivCloseString);
					WriteString(out, pUnescaped, 0, 1);
					estrDestroy(&pUnescaped);
				}

				estrPrintf(&pTempString, "</div>");
				WriteString(out, pTempString, 0, 1);
			}

			estrDestroy(&pIfExprClassNameString);
			estrDestroy(&altPath);
			estrDestroy(&currentXPath);
		}
		estrDestroy(&pRecurseEString);
		
		pContext->internalState.iDepth--;
		popXPath(pContext);

	}
	

	writeHTMLContextPopGeneratingTable(pContext);

	estrDestroy(&pTempString);

	return bWroteSomething;
}




// Array HTML writing (all types)
bool all_arrays_writehtmlfile(ParseTable tpi[], int column, void* structptr, int index, FILE* out, WriteHTMLContext *pContext)
{
	int i = 0;
	int numelems = TokenStoreGetNumElems(tpi, column, structptr, NULL);
	int type = TOK_GET_TYPE(tpi[column].type);
	bool bGeneratingTable = false;
	ParseTable *tpi_table_headings = NULL;
	char *pTempString = NULL;
	char *pTempName = NULL;
	char *pTempAscendingName = NULL;
	const char *pCurrentSort = NULL;
	bool bAscending = false;
	int *paiSortedIndices = NULL;
	bool bWroteSomething = false;

	bool bDefaultNoSorting = GetBoolFromTPIFormatString(&tpi[column], "HTML_NO_DEFAULT_SORT");

	bool bReverseOrder = GetBoolFromTPIFormatString(&tpi[column], "HTML_REVERSE_ARRAY");

	bool bCollapsedArray = GetBoolFromTPIFormatString(&tpi[column], "HTML_COLLAPSED_ARRAY");

	writeHTMLHandleMaxDepthAndCollapsed(1, tpi, column, structptr, index, out, pContext);

	estrAllocaCreate(&pTempString,        512);
	estrAllocaCreate(&pTempName,          512);
	estrAllocaCreate(&pTempAscendingName, 512);

	// All arrays of structs become tables
	if(type == TOK_STRUCT_X)
	{
		tpi_table_headings = tpi[column].subtable;
		if(tpi_table_headings)
		{
			bGeneratingTable = true;
		}
	}

	writeHTMLContextPushGeneratingTable(pContext, bGeneratingTable);

	estrPrintf(&pTempAscendingName, "ascending%s", tpi[column].name);
	bAscending = (atoi(urlFindSafeValue(pContext->pUrlArgs, pTempAscendingName)) == 1);


	if(numelems == 0)
	{
		WriteString(out, "&nbsp;", 0, 0);
		bWroteSomething = false;
	}
	else
	{
		char toggleCategoryName[256] = "";
		int iNumNotToCollapse = 1;

		// Make our table header, all in the "heading" tr block
		if(bGeneratingTable && tpi_table_headings)
		{
			int numheadings  = TokenStoreGetNumElems(tpi_table_headings, 0, structptr, NULL);
			bool bShowAllColumns = true;
			char *pXPathString = NULL;
			char *pFilterNameString = NULL;
			estrPrintCurrentXPath(&pXPathString, pContext, false);

			if (bCollapsedArray)
			{
				sprintf(toggleCategoryName, "toggleCategory_%s", tpi[column].name);
			}

			bWroteSomething = true;

			FORALL_PARSETABLE(tpi_table_headings, i)
			{
				int ignored;
				bool bShowColumn = GetIntFromTPIFormatString(&tpi_table_headings[i], "showcolumn", &ignored);

				if(bShowColumn)
				{
					bShowAllColumns = false;
					break;
				}
			}
/*
<form name="input"
method="get">
Command 
<input type="text" name="substrings" value="asfasdf" size=100>
</form>
*/

			if (GetStringFromTPIFormatString(&tpi[column], "TEST_FILTER", &pFilterNameString))
			{
				WriteFilteringAndSearchingForm(pFilterNameString, &tpi[column], out, pContext);
			}
			
			if (bCollapsedArray)
			{
				GetIntFromTPIFormatString(&tpi[column], "HTML_COLLAPSED_ARRAY", &iNumNotToCollapse);
				fprintf(out, "<button class=\"toggleButton\" onclick=\"toggleTable('%s', this, %d);\">Expand</button>\n",
					toggleCategoryName, iNumNotToCollapse);
			}

			fprintf(out, "\n<table class=\"%s\">\n<tr class=\"heading\">\n",
				bCollapsedArray ? toggleCategoryName : "");

			if(tpiHasCommands(tpi_table_headings, NULL))
			{
				WriteString(out, "<td class=\"headingCommands\">Commands</td>\n", 0, 1);
			}
 
			estrPrintf(&pTempName, "sort%s", tpi[column].name);
			pCurrentSort = urlFindSafeValue(pContext->pUrlArgs, pTempName);

			FORALL_PARSETABLE(tpi_table_headings, i)
			{
				writeHTMLContinueIfWeDontCareAboutThisEntry(tpi_table_headings, i, structptr, true);

				if(!bShowAllColumns)
				{
					int ignored;
					bool bShowColumn = GetIntFromTPIFormatString(&tpi_table_headings[i], "showcolumn", &ignored);
					
					if(!bShowColumn)
						continue;
				}

				estrPrintf(&pTempString, "<td class=\"heading%s\">", tpi_table_headings[i].name);

				//build links for table sorting.
				estrConcatf(&pTempString, "<a href=\"%s?", pContext->pViewURL ? pContext->pViewURL : "/view");
				
				//strip ajax args
				urlRemoveValue(pContext->pUrlArgs, "update");
				urlRemoveValue(pContext->pUrlArgs, "fetch_time");

				if(!strcmp(tpi_table_headings[i].name, pCurrentSort))
				{
					urlAppendQueryStringWithOverrides(pContext->pUrlArgs, &pTempString, 
						1,
						pTempAscendingName, bAscending ? NULL : "1");
				}
				else
				{
					urlAppendQueryStringWithOverrides(pContext->pUrlArgs, &pTempString, 
						2,
						pTempName, tpi_table_headings[i].name,
						pTempAscendingName, NULL);
				}

				if (pTempString[estrLength(&pTempString) - 1] == '&')
				{
					estrSetSize(&pTempString, estrLength(&pTempString) - 1);
				}

				estrConcatf(&pTempString, "#%s", pXPathString);

				estrConcatf(&pTempString, "\">%s</a></td>\n", tpi_table_headings[i].name);

				WriteString(out, pTempString, 0, 0);
			}
			WriteString(out, "</tr>\n", 0, 0);

			estrDestroy(&pXPathString);
		}
		else
		{
			estrPrintf(&pTempString, "<ul class=\"ul%s\">\n", tpi[column].name);
			WriteString(out, pTempString, 0, 0);
		}

		if (bGeneratingTable && numelems > 1) {
			int iSortColumn = -1;

			// Find our sort column
			FORALL_PARSETABLE(tpi_table_headings, i)
			{
				writeHTMLContinueIfWeDontCareAboutThisEntry(tpi_table_headings, i, structptr, true);

				// See if we have a sort setting
				if((pCurrentSort) && (pCurrentSort[0] != 0))
				{
					if(!strcmp(tpi_table_headings[i].name, pCurrentSort))
					{
						iSortColumn = i;
						break;
					}
				}
				else
				{	//Autosort by first column.
					//if (bDefaultNoSorting)
					//{
						iSortColumn = -1;
					//}
					//else
					//{
					//	iSortColumn = i;
					//	bAscending = 1;
					//}
					break;
				}
			}

			// Found our sort column ... make our lookup table and sort it
			if(iSortColumn >= 0)
			{
				InternalTokenComparatorData data = {0};
				data.tpi         = tpi;
				data.iColumn     = column;
				data.header_tpi  = tpi_table_headings;
				data.iSortColumn = iSortColumn;
				data.structptr   = structptr;
				data.bAscending  = bAscending;

				paiSortedIndices = malloc(sizeof(int) * numelems);
				for (i = 0; i < numelems; i++)
					paiSortedIndices[i] = i;

				stableSort(paiSortedIndices, numelems, sizeof(int), &data, internalTokenComparator);
			}
		}



		for (i = 0; i < numelems; i++)
		{
			int current_index = i;
			char *cid = 0;
			char *xpathkey = 0;
			int key;

			estrStackCreate(&xpathkey);
			estrStackCreate(&cid);

			estrPrintf(&cid, "%d", current_index);

			if(paiSortedIndices)
				current_index = paiSortedIndices[i];

			if (bReverseOrder)
			{
				current_index = numelems - current_index - 1;
			}

			estrPrintf(&xpathkey, "%d", current_index);

			if(bGeneratingTable)
			{
				bool bCollapsed = false;
				if (tpi[column].subtable)
				{
					ParseTable *subtable = tpi[column].subtable;
					void* substruct = TokenStoreGetPointer(tpi, column, structptr, current_index, NULL);
					key = ParserGetTableKeyColumn(subtable);
					if (key >= 0)
					{
						char *pCidBasis = NULL;
						U32 hash[4];
						estrStackCreate(&pCidBasis);
						FieldWriteText(subtable,key,substruct,0,&pCidBasis,0);
						estrCopyWithHTMLEscaping(&xpathkey, pCidBasis, false);

						cryptMD5((U8*)pCidBasis, estrLength(&pCidBasis), hash);
						estrPrintf(&cid, "%X%X%X%X",hash[0],hash[1],hash[2],hash[3]);
						estrDestroy(&pCidBasis);
					}
				}
				else
				{
					key = ParserGetTableKeyColumn(tpi);
					if (key >= 0)
					{
						char *pCidBasis = NULL;
						U32 hash[4];
						estrStackCreate(&pCidBasis);
						FieldWriteText(tpi,key,structptr,0,&pCidBasis,0);
						estrCopyWithHTMLEscaping(&xpathkey, pCidBasis, false);

						cryptMD5((U8*)pCidBasis, estrLength(&pCidBasis), hash);
						estrPrintf(&cid, "%X%X%X%X",hash[0],hash[1],hash[2],hash[3]);
						estrDestroy(&pCidBasis);
					}
				}

				bCollapsed = (i >= iNumNotToCollapse) && bCollapsedArray;

				estrPrintf(&pTempString, "<tr class=\"tr%s\"",
					tpi[column].name);

				if (bCollapsed)
				{
					estrConcatf(&pTempString, " style=\"display: none\"");
				}
				
				estrConcatf(&pTempString, " id=\"offsetID-%d:%s\">\n", 
					tpi[column].storeoffset,cid);
				WriteString(out, pTempString, 0, 0);
			}
			else
			{
				key = ParserGetTableKeyColumn(tpi);
				if (key >= 0)
				{
					char cidbasis[256];
					U32 hash[4];
					FieldToSimpleString(tpi,key,structptr,0,cidbasis,255,0);
					estrCopyWithHTMLEscaping(&xpathkey, cidbasis, false);

					cryptMD5((U8*)cidbasis, (int) strlen(cidbasis), hash);
					estrPrintf(&cid, "%X%X%X%X",hash[0],hash[1],hash[2],hash[3]);
				}
				estrPrintf(&pTempString, "<li class=\"li%s\" id=\"offsetID-%d:%s\">\n", tpi[column].name, tpi[column].storeoffset,cid);
				WriteString(out, pTempString, 0, 0);
			}


//			if(pContext->internalState.iDepth) 
			
			//don't pass the tpi/struct in here, because they are only needed for adding the override link, which we
			//already added when we added the name of the array
			pushXPath(pContext, tpi[column].name, xpathkey, NULL, NULL);
			pContext->internalState.iDepth++;
			bWroteSomething |= nonarray_writehtmlfile(tpi, column, structptr, current_index, out, pContext);
			pContext->internalState.iDepth--;
//			if(pContext->internalState.iDepth) 
				popXPath(pContext);

			if(bGeneratingTable)
			{
				WriteString(out, "\n</tr>\n", 0, 0);
			}
			else
			{
				WriteString(out, "\n</li>\n", 0, 0);
			}

			estrDestroy(&xpathkey);
			estrDestroy(&cid);
		}

		if(bGeneratingTable)
		{
			WriteString(out, "</table>\n", 0, 0);
		}
		else
		{
			WriteString(out, "</ul>\n", 0, 0);
		}
	}

	writeHTMLContextPopGeneratingTable(pContext);

	estrDestroy(&pTempString);
	estrDestroy(&pTempName);
	estrDestroy(&pTempAscendingName);

	if(paiSortedIndices)
		free(paiSortedIndices);

	return bWroteSomething;
}

// -------------------------------------------------------------------------
// These just call the above functions, mainly



void ParserWriteHTML(char **estr, ParseTable *tpi, void *struct_mem, WriteHTMLContext *pContext)
{
	FILE *fpBuff;

	if (!estr)
		return;

	fpBuff = fileOpenEString(estr);
	ParseWriteHTMLFile(fpBuff, tpi, struct_mem, pContext);
	fileClose(fpBuff);
}

void ParserWriteHTMLEx(char **estr, ParseTable *tpi, int column, void *structptr, int index, WriteHTMLContext *pContext)
{
	FILE *fpBuff;
	U32 storageType = TokenStoreGetStorageType(tpi[column].type);

	if (!estr)
		return;

	fpBuff = fileOpenEString(estr);

	if((pContext->internalState.iDepth == 0) 
	&& (pContext->bArrayContext || index == -1)
	&& TokenStoreStorageTypeIsAnArray(storageType))
	{
		all_arrays_writehtmlfile(tpi, column, structptr, index, fpBuff, pContext);
	}
	else
	{
		nonarray_writehtmlfile(tpi, column, structptr, index, fpBuff, pContext);
	}

	fileClose(fpBuff);
}

bool struct_writehtmlfile(ParseTable tpi[], int column, void* structptr, int index, FILE* out, WriteHTMLContext *pContext)
{
	void* substruct = TokenStoreGetPointer(tpi, column, structptr, index, NULL);
	if (substruct)
	{

		//for structs in arrays, they will be in a table,
		if (TokenStoreStorageTypeIsAnArray(TokenStoreGetStorageType(tpi[column].type)))
		{
			return ParseWriteHTMLFile(out, tpi[column].subtable, substruct, pContext);
		}
		else
		{
		//for other structs, they are either embedded or optional. In any case, mark them with a special div
			bool bRetVal;
			char *pRecurseString = NULL;
			FILE *pRecurseFile = fileOpenEString(&pRecurseString);
			bRetVal = ParseWriteHTMLFile(pRecurseFile, tpi[column].subtable, substruct, pContext);
			fileClose(pRecurseFile);

			if (bRetVal)
			{
				//XXXXXX: Not sure if it is always the case that the tpi is outputing a table row, but it seems to...
				if (GetBoolFromTPIFormatString(&tpi[column], "HTML_TABLE"))
				{
					WriteString(out, "<tr>", 0, 1);
				}
				else
				{
					WriteString(out, "<div class=\"StructInStruct\">", 0, 1);
				}

				if (pRecurseString)
				{
					WriteString(out, pRecurseString, 0, 1);
				}

				if (GetBoolFromTPIFormatString(&tpi[column], "HTML_TABLE"))
				{
					WriteString(out, "</tr>", 0, 1);
				}
				else
				{
					WriteString(out, "</div>", 0, 1);
				}
			}

			estrDestroy(&pRecurseString);
			return bRetVal;
		}
	}
	else
	{
		// This is probably a latebind struct ... 
		// just output a non-breaking space, in case we're generating tables

		WriteString(out, "&nbsp;", 0, 0);
		return false;
	}
}


bool  string_writehtmlfile(ParseTable tpi[], int column, void *structptr, int index, FILE* out, WriteHTMLContext *pContext)
{
	const char* str = TokenStoreGetString(tpi, column, structptr, index, NULL);

	if (str && str[0])
	{
		bool bMadeLink = false;

		int iTemp;
		
		if (GetIntFromTPIFormatString(&tpi[column], "FORMER_REFERENCE", &iTemp) && iTemp)
		{
			char xPath[256];

			const char *pCurXPath = urlFindSafeValue(pContext->pUrlArgs, "xpath");
			char *pFirstRightBracket;

			strcpy(xPath, pCurXPath);

			pFirstRightBracket = strchr(xPath, ']');

			if (pFirstRightBracket)
			{
				char *pDictionary = NULL;
				char *pLinkString = 0;
				estrStackCreate(&pLinkString);

				assert(GetStringFromTPIFormatString(&tpi[column], "FORMER_DICTIONARY", &pDictionary));

				*(pFirstRightBracket + 1) = 0;

				estrPrintf(&pLinkString, "<a href=\"/viewxpath?xpath=%s.globObj.%s[%s]\">%s</a>",
					xPath, pDictionary, str, str);

				WriteString(out, pLinkString, 0, 0);

				estrDestroy(&pLinkString);

				bMadeLink = true;
			}

		}
		
		if (!bMadeLink)
		{
			char *pHtmlSprintfString = NULL;

			if (GetIntFromTPIFormatString(&tpi[column], "HTML", &iTemp) && iTemp || tpi[column].format & TOK_FORMAT_UI_NOHEADER)
			{
				WriteString(out, str, 0, 0);
			}
			else if (GetBoolFromTPIFormatString(&tpi[column], "HTML_NOTENAME"))
			{
				char **ppStrs = NULL;
				int bPopLeft = false;
				int bDesigner = false;

				DivideString(str, ",", &ppStrs, DIVIDESTRING_STANDARD);

				assertmsgf(eaSize(&ppStrs) == 4, "Badly formatted HTML_NOTENAME in %s::%s",
					tpi[0].name, tpi[column].name);

				
				if (!StringToInt_Paranoid(ppStrs[2], &bPopLeft))
				{
					assertmsgf(0, "Badly formatted HTML_NOTENAME in %s::%s... popleft was not 1 or 0",
						tpi[0].name, tpi[column].name);
				}

				if (!StringToInt_Paranoid(ppStrs[3], &bDesigner))
				{
					assertmsgf(0, "Badly formatted HTML_NOTENAME in %s::%s... bDesigner was not 1 or 0",
						tpi[0].name, tpi[column].name);
				}
				

				WriteString(out, NotesServer_GetLinkToNoteServerMonPage(ppStrs[0], ppStrs[1], bPopLeft, bDesigner), 0, 0);

				eaDestroyEx(&ppStrs, NULL);
			}
			else if (GetStringFromTPIFormatString(&tpi[column], "HTML_SPRINTF", &pHtmlSprintfString))
			{
				char *pTempString = NULL;
				char *pUnescapedString = NULL;

				estrStackCreate(&pTempString);
				estrStackCreate(&pUnescapedString);


				estrAppendUnescaped(&pUnescapedString, pHtmlSprintfString);

				//pass in str twice, in case we have something like an HTML link with two %s's in it,
				//one for the link and one for the name of the link
				estrPrintf(&pTempString, FORMAT_OK(pUnescapedString), str, str);
				WriteString(out, pTempString, 0, 0);
				
				estrDestroy(&pTempString);
				estrDestroy(&pUnescapedString);
			}
			else
			{
				char *pEscapedEString = NULL;
				estrStackCreate(&pEscapedEString);
				estrCopyWithHTMLEscaping(&pEscapedEString, str, false);

				if (GetBoolFromTPIFormatString(&tpi[column], "HTML_PREFORMATTED"))
				{
					WriteString(out, "<pre>", 0, 0);
					WriteString(out, pEscapedEString, 0, 0);
					WriteString(out, "</pre>", 0, 0);
				}
				else
				{
					WriteString(out, pEscapedEString, 0, 0);
				}


				estrDestroy(&pEscapedEString);
			}

		}

		return true;
	}

	return false;
}

// -------------------------------------------------------------------------

#include "autogen/textparserhtml_c_ast.c"
