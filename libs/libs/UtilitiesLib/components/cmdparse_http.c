#include "GlobalTypes.h"
#include "cmdparse.h"
#include "stringCache.h"
#include "httpXPathSupport.h"
#include "cmdparse_http_c_ast.h"
#include "url.h"
#include "NotesServerComm.h"
#include "StashTable.h"

AUTO_RUN_ANON(memBudgetAddMapping(__FILE__, BUDGET_Networking););


#define MAX_CATEGORY_NAME_LENGTH 128

//earray of all category names, each of which is allocAddStringed. Calculated once when first queried
static const char **sppCategoryNames = NULL;

CmdList *spAllCmdLists[] = 
{
	&gGlobalCmdList,
	&gPrivateCmdList,
};

void FindAllCategoryNames(void)
{
	int iCmdListNum;
	CmdList *pCmdList;

	eaPush(&sppCategoryNames, allocAddString("All"));
	eaPush(&sppCategoryNames, allocAddString("HasNotesSet"));
	eaPush(&sppCategoryNames, allocAddString("Uncategorized"));

	for (iCmdListNum = 0; iCmdListNum < ARRAY_SIZE(spAllCmdLists); iCmdListNum++)
	{
		pCmdList = spAllCmdLists[iCmdListNum];

		FOR_EACH_IN_STASHTABLE(pCmdList->sCmdsByName, Cmd, pCmd)
		{
			if (pCmd->categories)
			{
				const char *pReadHead = pCmd->categories;

				while (*pReadHead)
				{
					while (*pReadHead == ' ')
					{
						pReadHead++;
					}

					if (*pReadHead)
					{
						char tempString[MAX_CATEGORY_NAME_LENGTH];
						char *pWriteHead = tempString;

						tempString[0] = 0;

						while (*pReadHead && *pReadHead != ' ')
						{
							*pWriteHead = *pReadHead;
							pWriteHead++;
							pReadHead++;
						}

						*pWriteHead = 0;

						eaPushUnique(&sppCategoryNames, allocAddString(tempString));
					}
				}
			}
		}
		FOR_EACH_END;
	}
}

int GetNumCommandCategories(void)
{
	if (!sppCategoryNames)
	{
		FindAllCategoryNames();
	}

	return eaSize(&sppCategoryNames);
}

const char *GetNthCommandCategoryName(int n)
{
	if (!sppCategoryNames)
	{
		FindAllCategoryNames();
	}

	assert(sppCategoryNames);

	return sppCategoryNames[n];
}

AUTO_ENUM;
typedef enum
{
	CMDPRIVACY_PUBLIC,
	CMDPRIVACY_PRIVATE,

	CMDPRIVACY_LAST,
} enumCommandPrivacy;

AUTO_STRUCT;
typedef struct CommandArgOverview
{
	const char *pArgName; AST(POOL_STRING)
	const char *pTypeName; AST(POOL_STRING)
	const char *pStructName; AST(POOL_STRING)
} CommandArgOverview;

AUTO_STRUCT;
typedef struct CommandOverview
{
	char *pCommandName; AST(ESTRING)
	char *pNotesLink; AST(FORMATSTRING(HTML=1))
	char *pCommandComment; AST(ESTRING)
	char *pExecute; AST(ESTRING, FORMATSTRING(command=1))
	int iAccessLevel;
	enumCommandPrivacy ePrivacy;
	const char *pReturnTypeName; AST(POOL_STRING)
	const char *pReturnTypeStructName; AST(POOL_STRING)
	const char *pCategories; AST(POOL_STRING)
	CommandArgOverview **ppArgs;
} CommandOverview;

AUTO_STRUCT;
typedef struct CommandCategoryOverview
{
	char *pTitle; AST(ESTRING)
	CommandOverview **ppCommands;
} CommandCategoryOverview;

bool CanCommandBeExecutedViaMonitoring(Cmd *pCommand)
{
	int i;

	for (i=0; i < CMDMAXARGS; i++)
	{
		switch (pCommand->data[i].type)
		{
		case MULTI_FLOAT:
		case MULTI_INT:
		case MULTI_STRING:
		case MULTI_VEC3:
		case MULTI_VEC4:
		case MULTI_QUAT:
		case MULTI_NONE:
			break;

		default:
			return false;
		}
	}

	return true;
}

#define MAX_ITEMS_IN_A_SELECT_PULLDOWN_FOR_COMMANDS 20

void GetCommandStringForMonitoringFromCommand(char **ppEString, Cmd *pCommand)
{
	int i, x;
	estrConcatf(ppEString, "%s ", pCommand->name);
	
	estrConcatf(ppEString, "$CONFIRM(Really execute command %s)", pCommand->name);

	for (i=0; i < CMDMAXARGS; i++)
	{
		switch (pCommand->data[i].type)
		{
		case MULTI_FLOAT:
			estrConcatf(ppEString, " $FLOAT(%s)", pCommand->data[i].pArgName);
			break;

		case MULTI_INT:
			estrConcatf(ppEString, " $INT(%s)", pCommand->data[i].pArgName);
			break;
	
		case MULTI_STRING:
			if (pCommand->data[i].eNameListType != NAMELISTTYPE_NONE)
			{
				NameList *pNameList;
				const char *pNameFromList;
				bool bIsFirst = true;
				char *pTempEString = NULL;
				int iCount = 0;
				bool bFailed = false;

				//if there are 20 or fewer options, create a selection drop-down. Otherwise, use a string

		
				estrConcatf(&pTempEString, " $SELECT(%s|", pCommand->data[i].pArgName);

				pNameList = CreateTempNameListFromTypeAndData(pCommand->data[i].eNameListType, pCommand->data[i].ppNameListData);

				if (pNameList)
				{
					while(pNameFromList = pNameList->pGetNextCB(pNameList, true))
					{
						iCount++;
						if (iCount > MAX_ITEMS_IN_A_SELECT_PULLDOWN_FOR_COMMANDS)
						{
							break;
						}

						estrConcatf(&pTempEString, "%s%s", bIsFirst ? "" : ",", pNameFromList);

						bIsFirst = false;
					}
					estrConcatf(&pTempEString, ")");
				}

				if (iCount <= MAX_ITEMS_IN_A_SELECT_PULLDOWN_FOR_COMMANDS && pNameList)
				{
					estrConcatf(ppEString, "%s", pTempEString);
				}
				else
				{
					estrConcatf(ppEString, " $STRING(%s)", pCommand->data[i].pArgName);
				}

				estrDestroy(&pTempEString);

			}
			else
			{
				estrConcatf(ppEString, " $STRING(%s)", pCommand->data[i].pArgName);
			}
			break;

		case MULTI_VEC3:
			for (x=0; x < 3; x++)
			{
				estrConcatf(ppEString, " $FLOAT(%s[%d])", pCommand->data[i].pArgName, x);
			}
			break;

		case MULTI_VEC4:
			for (x=0; x < 4; x++)
			{
				estrConcatf(ppEString, " $FLOAT(%s[%d])", pCommand->data[i].pArgName, x);
			}
			break;


		case MULTI_QUAT:
			for (x=0; x < 4; x++)
			{
				estrConcatf(ppEString, " $FLOAT(%s[%d])", pCommand->data[i].pArgName, x);
			}
			break;
		}
	}
}

static void GetCommandNotesLink(CommandOverview *pOverview)
{
	char *pTitle = NULL;
	char *pNoteName = NULL;

	estrPrintf(&pNoteName, "Command.%s", pOverview->pCommandName);
			
	estrPrintf(&pTitle, "Command %s",
		pOverview->pCommandName);
			
	pOverview->pNotesLink = strdup(NotesServer_GetLinkToNoteServerMonPage(pNoteName, pTitle, true, false));

	estrDestroy(&pTitle);
	estrDestroy(&pNoteName);
}


bool ProcessCommandCategoryIntoStructInfoForHttp(char *pLocalXPath, UrlArgumentList *pArgList, int iAccessLevelOfViewer, StructInfoForHttpXpath *pStructInfo, GetHttpFlags eFlags)
{
	char *pCategoryName = NULL;
	char *pCategoryNameWithSpaces = NULL;
	CommandCategoryOverview overview = {0};
	char *pFirstDot;
	bool bIsAll;
	bool bIsUncategorized;
	bool bHasNotesSet;
	bool bRetVal;
	int iCmdListNum;

	pStructInfo->ePersistType = HTTPXPATHPERSIST_STATIC;

	estrCopy2(&pCategoryName, pLocalXPath);

	pFirstDot = strchr(pLocalXPath, '.');

	if (pFirstDot)
	{
		estrSetSize(&pCategoryName, pFirstDot - pLocalXPath);
		pLocalXPath = pFirstDot;
	}
	else
	{
		pLocalXPath = "";
	}

	estrPrintf(&overview.pTitle, "Commands in category %s", pCategoryName);

	estrPrintf(&pCategoryNameWithSpaces, " %s ", pCategoryName);

	bIsAll = (stricmp(pCategoryName, "all") == 0);
	bIsUncategorized = (stricmp(pCategoryName, "uncategorized") == 0);
	bHasNotesSet = (stricmp(pCategoryName, "HasNotesSet") == 0);

	for (iCmdListNum = 0; iCmdListNum < ARRAY_SIZE(spAllCmdLists); iCmdListNum++)
	{
		CmdList *pCmdList = spAllCmdLists[iCmdListNum];

		FOR_EACH_IN_STASHTABLE(pCmdList->sCmdsByName, Cmd, cmd)
		{
			bool bIsInCategory = false;

			if (bIsAll)
			{
				bIsInCategory = true;
			}
			else if (bIsUncategorized)
			{
				bIsInCategory = (cmd->categories == NULL);
			}
			else if (bHasNotesSet)
			{
				char *pTempNoteName = NULL;
				estrStackCreate(&pTempNoteName);
				estrPrintf(&pTempNoteName, "Command.%s", cmd->name);
				bIsInCategory = NotesServer_NoteIsSet(pTempNoteName);
				estrDestroy(&pTempNoteName);
			}
			else
			{
				bIsInCategory = (cmd->categories && strstri(cmd->categories, pCategoryNameWithSpaces) != NULL);
			}

			if (bIsInCategory && cmd->access_level <= iAccessLevelOfViewer)
			{
				CommandOverview *pCommand = StructCreate(parse_CommandOverview);
				int iArgNum;
				int iUrlVal;

				estrCopy2(&pCommand->pCommandName, cmd->name);
				if (cmd->comment)
				{
					estrCopy2(&pCommand->pCommandComment, cmd->comment);
				}
				if (CanCommandBeExecutedViaMonitoring(cmd))
				{
					GetCommandStringForMonitoringFromCommand(&pCommand->pExecute, cmd);
				}

				pCommand->iAccessLevel = cmd->access_level;

				pCommand->ePrivacy = (pCmdList == &gPrivateCmdList) ? CMDPRIVACY_PRIVATE : CMDPRIVACY_PUBLIC;

				pCommand->pCategories = cmd->categories;

				pCommand->pReturnTypeName = MultiValTypeToReadableString(cmd->return_type.type);
				if (cmd->return_type.type == MULTI_NP_POINTER)
				{
					pCommand->pReturnTypeStructName = ParserGetTableName(cmd->return_type.ptr);
					if (!pCommand->pReturnTypeStructName)
					{
						pCommand->pReturnTypeStructName = "UNKNOWN";
					}
				}

				if (urlFindInt(pArgList, "svrShowArgs", &iUrlVal) == 1 && iUrlVal == 1)
				{
					for (iArgNum = 0; iArgNum < cmd->iNumLogicalArgs; iArgNum++)
					{
						CommandArgOverview *pArgOverview = StructCreate(parse_CommandArgOverview);
						pArgOverview->pArgName = cmd->data[iArgNum].pArgName;
						pArgOverview->pTypeName = MultiValTypeToReadableString(cmd->data[iArgNum].type);
						if (cmd->data[iArgNum].type == MULTI_NP_POINTER)
						{
							pArgOverview->pStructName = ParserGetTableName(cmd->data[iArgNum].ptr);
							if (!pArgOverview->pStructName)
							{
								pArgOverview->pStructName = "UNKNOWN";
							}
						}

						eaPush(&pCommand->ppArgs, pArgOverview);
					}
				}

				GetCommandNotesLink(pCommand);


				eaPush(&overview.ppCommands, pCommand);
			}
		}
		FOR_EACH_END;
	}

	bRetVal = ProcessStructIntoStructInfoForHttp(pLocalXPath, pArgList, &overview, parse_CommandCategoryOverview, iAccessLevelOfViewer,  0, pStructInfo, eFlags);
	
	StructDeInit(parse_CommandCategoryOverview, &overview);

	return bRetVal;

}



void cmdParseIntoVerboseHtmlString(char *pCommandString, char **ppEString, enumCmdContextHowCalled eHow)
{
	char *pRetString = NULL;

	estrStackCreate(&pRetString);

	if (cmdParseAndReturn(pCommandString, &pRetString, eHow))
	{
		if (estrLength(&pRetString))
		{
			estrPrintf(ppEString, "Command \"%s\" on server %s[%u] completed successfully. Return string:<br>%s",
				pCommandString, GlobalTypeToName(GetAppGlobalType()), GetAppGlobalID_ForCmdParse(), pRetString);
		}
		else
		{
			estrPrintf(ppEString, "Command \"%s\" on server %s[%u] completed successfully. (No return string)",
				pCommandString, GlobalTypeToName(GetAppGlobalType()), GetAppGlobalID_ForCmdParse());
		}
	}
	else
	{
		estrPrintf(ppEString, "Command \"%s\" on server %s[%u] FAILED",
			pCommandString, GlobalTypeToName(GetAppGlobalType()), GetAppGlobalID_ForCmdParse());
	}

	estrDestroy(&pRetString);
}



#include "cmdparse_http_c_ast.c"







