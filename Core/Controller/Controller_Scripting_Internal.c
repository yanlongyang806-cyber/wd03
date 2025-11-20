#include "controller.h"
#include "controller_h_ast.h"
#include "autogen/serverlib_autogen_remotefuncs.h"
#include "TestClient_comm.h"
#include "fileutil2.h"
#include "GlobalComm.h"
#include "Sock.h"
#include "autogen/Controller_scripting_h_ast.h"
#include "instringcommands.h"
#include "Expression.h"
#include "stringcache.h"
#include "logging.h"
#include "HttpLib.h"
#include "HttpClient.h"
#include "ContinuousBuilderSupport.h"
#include "StringUtil.h"
#include "accountNet.h"
#include "xboxHostIO.h"
#include "ticketnet.h"
#include "ControllerPub_h_ast.h"
#include "serverLib.h"
#include "Controller_MachineGroups.h"
#include "sysutil.h"
#include "fileutil.h"
#include "Alerts.h"
#include "Controller_Utils.h"
#include "ShardCluster.h"
#include "SentryServerComm.h"
#include "Controller_ClusterController.h"
#include "UtilitiesLib.h"

//used by KILLALL and KILLSERVER commands
typedef struct ServerToKill
{
	TrackedServerState *pServer;
	GlobalType eContainerType;
	U32 iContainerID;
} ServerToKill;


static ServerToKill **ppServersToKill = NULL;

bool gbShutdownWatcher = false;
AUTO_CMD_INT(gbShutdownWatcher, ShutdownWatcher) ACMD_COMMANDLINE;

void NextCommandStep(void);
void ControllerScripting_SetupMachines(void);

static bool sbUserConfirmed = false;

bool ControllerScripting_FixupFileName_ForInString(char *pInName, char outName[MAX_PATH], void *pUserData);
void ControllerScripting_Fail_ForInString(char *pFailureString, void *pUserData);


static bool sbErrorsDisabled = false;

static void StartErrorTracking(void)
{
	sbErrorsDisabled = false;
	CBSupport_StartErrorTracking();
}

static void StopErrorTracking(void)
{
	sbErrorsDisabled = true;
	CBSupport_StopErrorTracking();
}



AUTO_COMMAND;
void ConfirmControllerScriptingStep(void)
{
	sbUserConfirmed = true;
}

QueryableProcessHandle *pScriptingProcessHandle = 0;

CmdList gControllerScriptingSpecialList = {0};

//if true, there is no xbox plugged in, so skip any xbox client scripting
bool gbNoXBOX = false;
AUTO_CMD_INT(gbNoXBOX, NoXBOX) ACMD_CMDLINE;

//tracked state of the xbox client
char gXBOXClientState[256] = "";

int iXBOXClientControllerScriptingCommandStepResult = 0;
char XBOXClientControllerScriptingCommandStepResultString[256];

//if true, then we need to send status strings to the continuous builder
int gbSendStringsToContinuousBuilder = 0;
AUTO_CMD_INT(gbSendStringsToContinuousBuilder, SendStringsToContinuousBuilder) ACMD_CMDLINE;


//if true, quit when the script you are running is done
bool gbQuitOnScriptCompletion = 0;
AUTO_CMD_INT(gbQuitOnScriptCompletion, QuitOnScriptCompletion) ACMD_CMDLINE;

//sets the controller script to run
char controllerScriptFileName[MAX_PATH] = "";
AUTO_CMD_STRING(controllerScriptFileName, ScriptFile)  ACMD_CMDLINE;

//sets the script error log file name
char gErrorLogFileName[MAX_PATH] = "";
AUTO_CMD_STRING(gErrorLogFileName, ScriptErrorLogFile)  ACMD_CMDLINE;

int giControllerScriptingErrorCount = 0;
int gbFailOnError = 0;


//min delay for LAUNCH_NORMALLY steps
U32 gLaunchNormallyTime = 3600;
AUTO_CMD_INT(gLaunchNormallyTime, LaunchNormallyTime) ACMD_CMDLINE;

static enumControllerScriptingState sScriptingState = CONTROLLERSCRIPTING_NOTRUNNING;

bool ControllerScripting_FixupFileName(char *pInName, char outName[MAX_PATH]);

static InStringCommandsAllCBs sControllerScriptingInStringCommandsCBs = 
{
	ControllerScripting_FixupFileName_ForInString,
	ControllerScripting_Fail_ForInString
};


void ControllerScripting_SendStringToContinuousBuilder(char *pString, bool bIsSubSub)
{
	SendStringToCB(bIsSubSub ? CBSTRING_SUBSUBSTATE : CBSTRING_SUBSTATE, "%s", pString);
	/*Packet *pPak;

	if (!pLinkToContinuousBuilder)
	{
		pLinkToContinuousBuilder = commConnect(comm_controller,LINKTYPE_UNSPEC, LINK_FORCE_FLUSH,
			"localhost", CONTINUOUS_BUILDER_PORT,
			0,0,0,sizeof(TrackedServerState *));

		if (!linkConnectWait(&pLinkToContinuousBuilder,2.f))
		{
			return;
		}
	}
	
	pPak = pktCreate(pLinkToContinuousBuilder, bIsSubSub ? TO_CONTINUOUSBUILDER_SUBSUBSTATE : TO_CONTINUOUSBUILDER_SUBSTATE);
	pktSendString(pPak, pString);
	pktSend(&pPak);*/
}

		


typedef struct
{
	char *pVarName;
	char *pVarValue;
} ControllerScriptingVariable;

ControllerScriptingVariable **ppScriptingVariables = {0};

//starting variables are those set by -SETVAR on the command line. They can not be overridden
int giNumStartingVariables = 0;


void ControllerScripting_AddVariable(char *pVarName, char *pVarValue)
{
	int i;
	ControllerScriptingVariable *pNewVar;

	ControllerScripting_LogString(STACK_SPRINTF("Setting variable %s to \"%s\"", pVarName, pVarValue), 0, 0);

	for (i=0; i < eaSize(&ppScriptingVariables); i++)
	{
		if (stricmp(ppScriptingVariables[i]->pVarName, pVarName) == 0)
		{
			if (i < giNumStartingVariables)
			{
				return;
			}

			free(ppScriptingVariables[i]->pVarValue);
			ppScriptingVariables[i]->pVarValue = strdup(pVarValue);
			return;
		}
	}

	pNewVar = malloc(sizeof(ControllerScriptingVariable));

	pNewVar->pVarName = strdup(pVarName);
	pNewVar->pVarValue = strdup(pVarValue);

	eaPush(&ppScriptingVariables, pNewVar);
}

//returns true on success. Calls ControllerScripting_Fail on failure and returns false
bool ControllerScripting_ImportVariables(char *pVarNames_In)
{
	char **ppVarNames = NULL;
	int i;

	static char *pQueryString = NULL;
	static char *pResultString = NULL;
	static char *pVarName = NULL;

	if (!g_isContinuousBuilder)
	{
		ControllerScripting_Fail("Trying to call IMPORTVARIABLES when not running from a CB");
		return false;
	}

	DivideString(pVarNames_In, ",", &ppVarNames, DIVIDESTRING_POSTPROCESS_STRIP_WHITESPACE);

	for (i=0; i < eaSize(&ppVarNames); i++)
	{
		int iResult;
		
		estrClear(&pResultString);
		estrPrintf(&pQueryString, "http://localhost/bsvars?varname=%s", ppVarNames[i]);
		iResult = httpBasicGetText(pQueryString, &pResultString);

		if (iResult == 200 && estrLength(&pResultString))
		{
			estrPrintf(&pVarName, "$%s$", ppVarNames[i]);
			ControllerScripting_AddVariable(pVarName, pResultString);
			printf("Imported variable %s with value %s\n", pVarName, pResultString);
		}
		else
		{
			ControllerScripting_Fail(STACK_SPRINTF("Couldn't import variable %s", ppVarNames[i]));
			return false;
		}
	}

	return true;
}





//Sets a scripting variable to an un-overrideable value
AUTO_COMMAND ACMD_NAME(SetVar);
void ControllerScripting_SetVar(char *pVarName, char *pVarValue)
{
	char *pVarNameToUse = NULL;

	estrStackCreate(&pVarNameToUse);
	estrPrintf(&pVarNameToUse, "$%s$", pVarName);

	ControllerScripting_AddVariable(pVarNameToUse, pVarValue);

	giNumStartingVariables = eaSize(&ppScriptingVariables);

	estrDestroy(&pVarNameToUse);
}

#define MAX_VAR_NAME_LENGTH 256

#define ISOKFORVARNAME(c) ( isalnum(c) || (c) == '_')

//returns false on parsing failure
bool ControllerScripting_SetVarFromString(char *pString, int iIndex)
{
	char varName[MAX_VAR_NAME_LENGTH + 2];
	int iVarNameLength = 1;
	char *pFirstComma;

	memset(varName, 0, sizeof(varName));

	varName[0] = '$';

	while (ISOKFORVARNAME(*pString))
	{
		if (iVarNameLength >= MAX_VAR_NAME_LENGTH)
		{
			return false;
		}

		varName[iVarNameLength++] = *pString;
		pString++;
	}

	if (iVarNameLength == 1)
	{
		return false;
	}

	varName[iVarNameLength++] = '$';

	while (*pString == ' ')
	{
		pString++;
	}

	if (*pString != '=')
	{
		return false;
	}

	pString++;

	while (*pString == ' ')
	{
		pString++;
	}

	if (*pString == 0)
	{
		return false;
	}

	if (iIndex == -1)
	{
		ControllerScripting_AddVariable(varName, pString);
		return true;
	}

	while (iIndex)
	{
		pFirstComma = strchr(pString, ',');
		if (!pFirstComma)
		{
			return false;
		}
		pString = pFirstComma + 1;

		while (*pString == ' ')
		{
			pString++;
		}

		if (*pString == 0)
		{
			return false;
		}
		iIndex--;
	}

	pFirstComma = strchr(pString, ',');
	if (pFirstComma)
	{
		*pFirstComma = 0;
	}
	
	ControllerScripting_AddVariable(varName, pString);

	if (pFirstComma)
	{
		*pFirstComma = ',';
	}

	return true;
}

	
bool ControllerScripting_SetVarFromSpecialString(char *pString)
{
	char varName[MAX_VAR_NAME_LENGTH + 2];
	int iVarNameLength = 1;
	CmdContext cmd_context = {0};
	char *pRetString = NULL;

	memset(varName, 0, sizeof(varName));

	varName[0] = '$';

	while (ISOKFORVARNAME(*pString))
	{
		if (iVarNameLength >= MAX_VAR_NAME_LENGTH)
		{
			return false;
		}

		varName[iVarNameLength++] = *pString;
		pString++;
	}

	if (iVarNameLength == 1)
	{
		return false;
	}

	varName[iVarNameLength++] = '$';

	while (*pString == ' ')
	{
		pString++;
	}

	if (*pString != '=')
	{
		return false;
	}

	pString++;

	while (*pString == ' ')
	{
		pString++;
	}

	if (*pString == 0)
	{
		return false;
	}

	cmd_context.access_level = 10;
	cmd_context.output_msg = &pRetString;

	if (!cmdParseAndExecute(&gControllerScriptingSpecialList, pString, &cmd_context))
	{
		estrDestroy(&pRetString);
		return false;
	}

	ControllerScripting_AddVariable(varName, pRetString);

	estrDestroy(&pRetString);
	return true;
}

static bool isalnumOrUnder(char c)
{
	return isalnum(c) || c == '_';
}


bool ControllerScripting_DoVariableReplacingInStrings(char *pSourceString, char **ppDestEString);


//looks for all occurrences of CHECK(varname) or CHECK($varname$). Then looks up that variable. If it isn't defined,
//or is "0" or is all empty, replaces the entire thing with "0". Otherwise replaces it with "1".
//
//returns true if syntax was all correct
bool ControllerScripting_FindAndReplaceCheckMacro(char **ppOutString)
{
	char *pFoundCheck;
	int iReadHeadIndex = 0;
	while ((pFoundCheck = strstr(*ppOutString + iReadHeadIndex, "CHECK")))
	{
		//check if this is a valid occurrence
		int iFoundIndex = pFoundCheck - *ppOutString;
		int iOpenParensIndex;
		int iCloseParensIndex;

		if (
			//CHECK is at beginning of string, or character before is not part of an identifier
			(iFoundIndex == 0 || !isalnumOrUnder((*ppOutString)[iFoundIndex - 1]))
			
			//left parens comes immediately after CHECK
			&& GetFirstNonWhitespaceChar((*ppOutString) + iFoundIndex + 5, &iOpenParensIndex) == '(')
		{
			int i;
			char *pInside = NULL;
			char *pVarName = NULL;
			char replaceString[2] = "0";
			char *pCloseParens;

			iOpenParensIndex += iFoundIndex + 5;


			pCloseParens = strchr((*ppOutString) + iOpenParensIndex + 1, ')');
			if (!pCloseParens)
			{
				return false;
			}

			estrStackCreate(&pInside);
			estrStackCreate(&pVarName);

			iCloseParensIndex = pCloseParens - (*ppOutString);

			estrConcat(&pInside, (*ppOutString) + iOpenParensIndex + 1, iCloseParensIndex - iOpenParensIndex - 1);

			estrTrimLeadingAndTrailingWhitespaceEx(&pInside, "$");

			estrPrintf(&pVarName, "$%s$", pInside);

			for (i=0; i < eaSize(&ppScriptingVariables); i++)
			{
				if (stricmp(ppScriptingVariables[i]->pVarName, pVarName) == 0)
				{
					if (strcmp(ppScriptingVariables[i]->pVarValue, "0") == 0 || StringIsAllWhiteSpace(ppScriptingVariables[i]->pVarValue))
					{
					}
					else
					{
						//here we may have to do more variable replacing on the value we got out, as it might contain something that resolves to empty or 0
						char *pTempValueString;
						estrStackCreate(&pTempValueString);
						ControllerScripting_DoVariableReplacingInStrings(ppScriptingVariables[i]->pVarValue, &pTempValueString);

						if (strcmp(pTempValueString, "0") == 0 || StringIsAllWhiteSpace(pTempValueString))
						{
						}
						else
						{
							replaceString[0] = '1';
						}

						estrDestroy(&pTempValueString);
					}
					break;
				}
			}

			estrDestroy(&pInside);
			estrDestroy(&pVarName);

			estrRemove(ppOutString, iFoundIndex, iCloseParensIndex - iFoundIndex + 1);
			estrInsert(ppOutString, iFoundIndex, replaceString, 1);

			//don't modify readHeadIndex since we've removed this occurrence entirely
		}
		else
		{
			iReadHeadIndex += 5;
		}
	}

	return true;
}



bool ControllerScripting_DoVariableReplacingInStrings(char *pSourceString, char **ppDestEString)
{

	int iStartingLength;
	bool bSomethingChanged = true;
	int iResult;
	int i;

	if (!pSourceString)
	{
		return true;
	}

	iStartingLength = (int) strlen(pSourceString);

	estrCopy2(ppDestEString, pSourceString);

	ApplyDirectoryMacrosToEString(ppDestEString, true);


	while (bSomethingChanged)
	{
		if (!ControllerScripting_FindAndReplaceCheckMacro(ppDestEString))
		{
			return false;
		}

		do
		{
			bSomethingChanged = false;
			for (i=0; i < eaSize(&ppScriptingVariables); i++)
			{
				if (estrReplaceOccurrences_CaseInsensitive(ppDestEString, ppScriptingVariables[i]->pVarName, 
					ppScriptingVariables[i]->pVarValue))
				{
					bSomethingChanged = true;
				}
			}
		}
		while (bSomethingChanged);

		if (!ControllerScripting_FindAndReplaceCheckMacro(ppDestEString))
		{
			return false;
		}

		iResult = InStringCommands_Apply(ppDestEString, &sControllerScriptingInStringCommandsCBs, NULL);

		if (iResult < 0)
		{
			return false;
		}

		if (iResult > 0)
		{
			bSomethingChanged |= true;
		}
	}

	return true;
}



void ControllerScripting_DoVariableReplacing(ControllerScriptingCommand *pCommand)
{
	ControllerScripting_DoVariableReplacingInStrings(pCommand->pScriptString_Raw, &pCommand->pScriptString_Use);
	ControllerScripting_DoVariableReplacingInStrings(pCommand->pDisplayString_Raw, &pCommand->pDisplayString_Use);
	ControllerScripting_DoVariableReplacingInStrings(pCommand->pExtraCommandLine_Raw, &pCommand->pExtraCommandLine_Use);
	ControllerScripting_DoVariableReplacingInStrings(pCommand->pScriptResultString_Raw, &pCommand->pScriptResultString_Use);
	ControllerScripting_DoVariableReplacingInStrings(pCommand->pWorkingDir_Raw, &pCommand->pWorkingDir_Use);
	ControllerScripting_DoVariableReplacingInStrings(pCommand->pIfExpression_Raw, &pCommand->pIfExpression_Use);
}



enumControllerScriptingState ControllerScripting_GetState(void)
{
	return sScriptingState;
}



void ControllerScripting_SetState(enumControllerScriptingState eState)
{
	TrackedServerState *pServer;

	sScriptingState = eState;


	//send this update to all MCPs
	pServer = gpServersByType[GLOBALTYPE_MASTERCONTROLPROGRAM];

	while (pServer)
	{
		if (pServer->pLink)
		{
			Packet *pPack;

			pPack = pktCreate(pServer->pLink, FROM_CONTROLLER_SCRIPT_STATE_FOR_MCP);
			pktSendBits(pPack, 32, eState);
			pktSend(&pPack);
		}

		pServer = pServer->pNext;
	}

	switch (eState)
	{
	case CONTROLLERSCRIPTING_RUNNING:
		Controller_SetStartupStatusString("main", "Controller scripting beginning");
		ControllerScripting_LogString(STACK_SPRINTF("----Execution of controller script %s began at %s", controllerScriptFileName, timeGetLogDateStringFromSecondsSince2000(timeSecondsSince2000())), 0, 0);
		break;

	case CONTROLLERSCRIPTING_SUCCEEDED:
		Controller_SetStartupStatusString("main", NULL);
		consoleSetColor(COLOR_BRIGHT|COLOR_GREEN, 0);
		ControllerScripting_LogString("Script completed successfully", 0, 0);
		consoleSetDefaultColor();
		break;

	case CONTROLLERSCRIPTING_FAILED:
		Controller_SetStartupStatusString("main", NULL);
		consoleSetColor(COLOR_BRIGHT|COLOR_RED, 0);
		ControllerScripting_LogString("Script failed", 0, 0);
		consoleSetDefaultColor();

		break;

	case CONTROLLERSCRIPTING_COMPLETE_W_ERRORS:
		Controller_SetStartupStatusString("main", NULL);
		consoleSetColor(COLOR_BRIGHT|COLOR_GREEN, 0);
		ControllerScripting_LogString("Script completed with errors", 0, 0);
		consoleSetDefaultColor();
		break;
	}


	if (gbQuitOnScriptCompletion)
	{
		if (eState == CONTROLLERSCRIPTING_SUCCEEDED)
		{
			printf("Exiting with code 0\n");
			exit(0);
		}
		else if (eState == CONTROLLERSCRIPTING_FAILED)
		{
			printf("Exiting with code -1\n");
			exit(-1);
		}
		else if (eState == CONTROLLERSCRIPTING_COMPLETE_W_ERRORS)
		{
			printf("Exiting with code -2\n");
			exit(-2);
		}
	}
}


void ControllerScripting_Cancel(void)
{
	ControllerScripting_SetState(CONTROLLERSCRIPTING_CANCELLED);
}

int gCommandStep;
int gCommandStepBeforeFailure = -1;
U32 gTimeCurStepStarted = 0;
__int64 gMsecTimeCurStepStarted = 0;



bool ControllerScripting_FixupFileName(char *pInName, char outName[MAX_PATH])
{
	if (fileIsAbsolutePath(pInName))
	{
		quick_sprintf(outName, MAX_PATH, "%s", pInName);
		return true;
	}

	if (strstri(pInName, "server/controllerScripts") || strstri(pInName, "server\\controllerScripts"))
	{
		quick_sprintf(outName, MAX_PATH, "%s", pInName);
		return true;
	}

	quick_sprintf(outName, MAX_PATH, "server/controllerScripts/%s", pInName);
	return true;
}

bool ControllerScripting_FixupFileName_ForInString(char *pInName, char outName[MAX_PATH], void *pUserData)
{
	return  ControllerScripting_FixupFileName(pInName, outName);
}

AUTO_EXPR_FUNC(controllerScripting) ACMD_NAME(MAP_RUNNING);
bool ControllerScripting_MAP_RUNNING(const char *pMapName)
{
	TrackedServerState *pServer = gpServersByType[GLOBALTYPE_GAMESERVER];

	while (pServer)
	{
		if (pServer->pGameServerSpecificInfo)
		{
			if (strstri_safe(pServer->pGameServerSpecificInfo->mapName, pMapName))
			{
				if (strstri_safe(pServer->stateString, "gslRunning"))
				{
					return true;
				}
			}
		}

		pServer = pServer->pNext;
	}

	return false;
}


bool ControllerScripting_IsExpressionStringTrue(char *pString)
{
	Expression *pExpr = exprCreateFromString(pString, NULL);
	static ExprContext *pContext = NULL;
	static ExprFuncTable* pFuncTable;
	MultiVal answer = {0};

	if (!pContext)
	{
		pContext = exprContextCreate();
		pFuncTable = exprContextCreateFunctionTable();
		exprContextAddFuncsToTableByTag(pFuncTable, "util");
		exprContextAddFuncsToTableByTag(pFuncTable, "controllerScripting");
		exprContextSetFuncTable(pContext, pFuncTable);
	}

	exprGenerate(pExpr, pContext);

	exprEvaluate(pExpr, pContext, &answer);

	exprDestroy(pExpr);

	return QuickGetInt(&answer);	
}



bool ControllerScripting_LoadScriptIntoList(char *pScriptName, ControllerScriptingCommandList *pList)
{

	int i,j;
	bool bNeedToDoMoreIncluding = true;
	int iIncludeCount = 0;
	char fixedUpFileName[MAX_PATH];
	
	ControllerScripting_FixupFileName(pScriptName, fixedUpFileName);

	if (!ParserReadTextFile(fixedUpFileName, parse_ControllerScriptingCommandList, pList, 0))
	{
		char *pTempString = NULL;

		StructReset(parse_ControllerScriptingCommandList, pList);
		ErrorfPushCallback(EstringErrorCallback, (void*)(&pTempString));
		ParserReadTextFile(fixedUpFileName, parse_ControllerScriptingCommandList, pList, 0);
		estrInsertf(&pTempString, 0, "Couldn't load controller script from file %s: ", fixedUpFileName);
		ControllerScripting_Fail(pTempString);
		estrDestroy(&pTempString);
		ErrorfPopCallback();

		StructDeInit(parse_ControllerScriptingCommandList, pList);
		return false;
	}



	for (i=0; i < eaSize(&pList->ppCommands); i++)
	{
		if (pList->ppCommands[i]->eCommand == CONTROLLERCOMMAND_INCLUDE)
		{
			ControllerScriptingCommandList gIncludedList = {0};
	
			if (pList->ppCommands[i]->pIfExpression_Raw && pList->ppCommands[i]->pIfExpression_Raw[0])
			{
				ControllerScripting_DoVariableReplacingInStrings(pList->ppCommands[i]->pIfExpression_Raw, &pList->ppCommands[i]->pIfExpression_Use);
				if (!ControllerScripting_IsExpressionStringTrue(pList->ppCommands[i]->pIfExpression_Use))
				{
					printf("Replacing an INCLUDE of %s with a WAIT because of ifExpression %s",
						pList->ppCommands[i]->pScriptString_Use, pList->ppCommands[i]->pIfExpression_Raw);
					pList->ppCommands[i]->eCommand = CONTROLLERCOMMAND_WAIT;
					pList->ppCommands[i]->iScriptInt = 1;
					continue;
				}
			}




			iIncludeCount++;

			ControllerScripting_DoVariableReplacing(pList->ppCommands[i]);


			assertmsg(iIncludeCount < 256, "More than 256 includes... probable include recursion");

			ControllerScripting_FixupFileName(pList->ppCommands[i]->pScriptString_Use, fixedUpFileName);

			if (!ParserReadTextFile(fixedUpFileName, parse_ControllerScriptingCommandList, &gIncludedList, 0))
			{
				char *pTempString = NULL;

				StructReset(parse_ControllerScriptingCommandList, &gIncludedList);
				ErrorfPushCallback(EstringErrorCallback, (void*)(&pTempString));
				ParserReadTextFile(fixedUpFileName, parse_ControllerScriptingCommandList, &gIncludedList, 0);
				estrInsertf(&pTempString, 0, "Couldn't load controller script from file %s: ", fixedUpFileName);
				ControllerScripting_Fail(pTempString);
				estrDestroy(&pTempString);
				ErrorfPopCallback();

				StructDeInit(parse_ControllerScriptingCommandList, &gIncludedList);
			}

			for (j=eaSize(&gIncludedList.ppCommands) - 1; j >= 0; j--)
			{
				eaInsert(&pList->ppCommands, gIncludedList.ppCommands[j], i+1);
			}

			eaDestroy(&gIncludedList.ppCommands);
			StructDeInit(parse_ControllerScriptingCommandList, &gIncludedList);

		}
	}

	return true;

}



void ControllerScripting_LoadWhileRunning(char *pScriptName)
{
	int i;


	switch(sScriptingState)
	{
	case CONTROLLERSCRIPTING_NOTRUNNING:
		strcpy(controllerScriptFileName, pScriptName);
		ControllerScripting_Load();
		break;
	case CONTROLLERSCRIPTING_SUCCEEDED:
	case CONTROLLERSCRIPTING_FAILED:
	case CONTROLLERSCRIPTING_COMPLETE_W_ERRORS:
		{
			ControllerScriptingCommandList tempList = {0};
			if (!ControllerScripting_LoadScriptIntoList(pScriptName, &tempList))
			{
				ControllerScripting_Fail(STACK_SPRINTF("Couldn't hot-load controller script from file %s\n", pScriptName));
				StructDeInit(parse_ControllerScriptingCommandList, &tempList);
				return;
			}


			for (i=0;i < eaSize(&tempList.ppCommands); i++)
			{
				eaPush(&gCommandList.ppCommands, tempList.ppCommands[i]);
			}

			eaDestroy(&tempList.ppCommands);

			ControllerScripting_SetupMachines();

			sScriptingState = CONTROLLERSCRIPTING_RUNNING;
			giControllerScriptingErrorCount = 0;


			gCommandStep--;
			NextCommandStep();



		}
		break;



		break;
	default:
		Errorf("Someone tried to run controller script %s while the controller is already running", pScriptName);
		break;
	}
}

void ControllerScripting_Load(void)
{


	if (!controllerScriptFileName[0])
	{
		return;
	}

	eaDestroyEx(&ppServersToKill, NULL);


	ControllerScripting_AddVariable("$EXECDIR$", gExecutableDirectory);
	ControllerScripting_AddVariable("$COREEXECDIR$", gCoreExecutableDirectory);

	ControllerScripting_AddVariable("$PRODUCTNAME$", GetProductName());
	ControllerScripting_AddVariable("$SHORTPRODUCTNAME$", GetShortProductName());


	sScriptingState = CONTROLLERSCRIPTING_LOADING;
	gCommandStep = -1;

	if (!ControllerScripting_LoadScriptIntoList(controllerScriptFileName, &gCommandList))
	{
		return;
	}

	ControllerScripting_LogString(STACK_SPRINTF("Beginning controller scripting with file %s", controllerScriptFileName), true, false);
}

	
void ControllerScripting_SetupMachines(void)
{
	int i;


	
	for (i=0; i < giNumMachines; i++)
	{
		ApplyCommandConfigStuffToMachine(&gTrackedMachines[i]);
	}
}



void LaunchServerFromCommand(ControllerScriptingCommand *pCommand)
{
	int i;
	TrackedServerState *pServer;

	if (pCommand->eServerType == GLOBALTYPE_NONE)
	{
		CRITICAL_NETOPS_ALERT("TRYING_TO_LAUNCH_BAD_TYPE", "A controller scripting command from %s(%d) is trying to launch a server with an invalid type... possible shardlauncher incompatibility?", 
			pCommand->pSourceFileName, pCommand->iLineNum);
		return;
	}
	

	//if a launch_normally is trying to launch a unique server that already exists, that's fine, just do nothing
	if (gServerTypeInfo[pCommand->eServerType].bIsUnique && gpServersByType[pCommand->eServerType])
	{
		return;
	}


	//note that bScriptingLaunchesAsManyOfTheseAsAllowed is now overridden by iCount, so if iCount is set, use it
	if (gServerTypeInfo[pCommand->eServerType].bScriptingLaunchesAsManyOfTheseAsAllowed &&pCommand->iCount == 1)
	{
	

		for (i=0; i < eaSize(&pCommand->ppMachines); i++)
		{
			pServer = RegisterNewServerAndMaybeLaunch(LAUNCHFLAG_FAILURE_IS_ALERTABLE, 
				pCommand->eVisibility,
				pCommand->ppMachines[i], pCommand->eServerType, GetFreeContainerID(pCommand->eServerType), false, 
					pCommand->pExtraCommandLine_Use, 
					pCommand->pWorkingDir_Use,
					false,  0, NULL, "Launched by Controller Scripting command");
			

			if (pCommand->bWillDie)
			{
				pServer->bKilledIntentionally = true;
			}
		}
	}
	else
	{
		TrackedMachineState **ppMachines = NULL;

		//if count > 1 and we have more than one assigned machine, distribute them evenly
		if (!pCommand->iFirstIP && pCommand->pMachineName[0] == '*' && pCommand->pMachineName[1] == 0)
		{
			FindAllMachinesForServerType(pCommand->eServerType, &ppMachines);
			if (eaSize(&ppMachines) == 1)
			{
				eaDestroy(&ppMachines);
			}
		}


		for (i=0; i < pCommand->iCount; i++)
		{
			TrackedMachineState *pMachine;

			if (ppMachines)
			{
				pMachine = ppMachines[ i % eaSize(&ppMachines)];
			}
			else if (pCommand->iFirstIP)
			{
				pMachine = FindDefaultMachineForTypeInIPRange(pCommand->eServerType, NULL, pCommand->iFirstIP, pCommand->iLastIP, NULL);
			}
			else if (pCommand->pMachineName[0] == '*' && pCommand->pMachineName[1] == 0)
			{
				pMachine = FindDefaultMachineForType(pCommand->eServerType, NULL, NULL);
			}
			else
			{
				pMachine = FindMachineByName(pCommand->pMachineName);
			}


			if (!pMachine)
			{
				CRITICAL_NETOPS_ALERT("NO_MACHINE_FOR_SERVER", "Unable to find a machine to launch a %s during controller scripting", GlobalTypeToName(pCommand->eServerType));
				return;
			}

			if (gServerTypeInfo[pCommand->eServerType].bMachineSpecifiedInShardSetupFile && gServerTypeInfo[pCommand->eServerType].bNonRelocatable && pMachine->canLaunchServerTypes[pCommand->eServerType].eCanLaunch == CAN_LAUNCH_DEFAULT)
			{
				CRITICAL_NETOPS_ALERT("NO_MACHINE_FOR_SERVER", "Want to launch a %s, but it is nonrelocatable, and none of the machines specified for it successfully connected during shard startup",
					GlobalTypeToName(pCommand->eServerType));
				return;
			}

			if (pCommand->eServerType == GLOBALTYPE_CLIENT)
			{
				static int siNextTestClientLinkID = 1;
				//special case so that clients launched by command scripts can accept commands via the testclient interface
				
				pServer = RegisterNewServerAndMaybeLaunch(LAUNCHFLAG_FAILURE_IS_ALERTABLE, pCommand->eVisibility, pMachine, pCommand->eServerType, GetFreeContainerID(pCommand->eServerType), false,
					STACK_SPRINTF(" %s -SetTestingMode -TestClientIsController -EnableQuickplay", pCommand->pExtraCommandLine_Use),
					pCommand->pWorkingDir_Use,
					false,  0, NULL, "Launched by Controller Scripting command");
				siNextTestClientLinkID++;
			}
			else
			{
				pServer = RegisterNewServerAndMaybeLaunch(LAUNCHFLAG_FAILURE_IS_ALERTABLE, pCommand->eVisibility, pMachine, pCommand->eServerType, GetFreeContainerID(pCommand->eServerType), false, 
					pCommand->pExtraCommandLine_Use, 
					pCommand->pWorkingDir_Use,
					false,  0, NULL, "Launched by Controller Scripting command");
			}

			if (pCommand->bWillDie)
			{
				pServer->bKilledIntentionally = true;
			}

		}

		eaDestroy(&ppMachines);
	}
}

void GetServerTypeReadyToLaunchOnMachine(GlobalType eType, bool bLaunchIfReady, TrackedMachineState *pMachine, GlobalType eParentType)
{
	if (!pMachine && gpServersByType[eType])
	{
		return;
	}

	if (pMachine && pMachine->pServersByType[eType])
	{
		return;
	}

	if (bLaunchIfReady && ServerTypeIsReadyToLaunchOnMachine(eType, pMachine, NULL))
	{
		char reason[2048];


		if (eParentType != GLOBALTYPE_NONE)
		{
			sprintf(reason, "During controller scripting, launching %s because %s depends on it",
				GlobalTypeToName(eType), GlobalTypeToName(eParentType));
		}
		else
		{
			sprintf(reason, "Launched by controller scripting");
		}

		RegisterNewServerAndMaybeLaunch(LAUNCHFLAG_FAILURE_IS_ALERTABLE, VIS_UNSPEC, pMachine ? pMachine : FindDefaultMachineForType(eType, NULL, NULL), eType, GetFreeContainerID(eType), false, NULL, NULL, false, 0, NULL, "%s", reason);
	}
	else
	{
		int iNumDependencies = eaSize(&gServerTypeInfo[eType].ppServerDependencies);
		int i;

		for (i=0; i < iNumDependencies; i++)
		{
			ServerDependency *pDependency = gServerTypeInfo[eType].ppServerDependencies[i];

			if (DependencyIsActive(pDependency))
			{
				if (!ServerDependencyIsFulfilledForMachine(eType, pMachine, pDependency))
				{
					if (pDependency->bPerMachineDependency)
					{
						GetServerTypeReadyToLaunchOnMachine(pDependency->eTypeToWaitOn, true, pMachine, eType);
					}
					else
					{
						GetServerTypeReadyToLaunchOnMachine(pDependency->eTypeToWaitOn, true, NULL, eType);
					}
				}
			}
		}
	}	
}

void GetServerTypeReadyToLaunchOnMachines(GlobalType eType, bool bLaunchIfReady, TrackedMachineState **ppMachines)
{
	int iNumMachines = eaSize(&ppMachines);
	int i;

	for (i=0; i < iNumMachines; i++)
	{
		GetServerTypeReadyToLaunchOnMachine(eType, bLaunchIfReady, ppMachines[i], GLOBALTYPE_NONE);
	}
}

	
void ApplyCommandConfigStuffToMachine(TrackedMachineState *pMachine)
{
	int i;

	if (!gCommandList.ppCommands)
	{
		return;
	}

	for (i=0; i < eaSize(&gCommandList.ppCommands); i++)
	{
		ControllerScriptingCommand *pCommand = gCommandList.ppCommands[i];

		switch (pCommand->eCommand)
		{
		case CONTROLLERCOMMAND_SPECIFY_MACHINE:
			ControllerScripting_DoVariableReplacing(pCommand);

			if (pCommand->iFirstIP && (pCommand->iFirstIP <= GetIPToUse(pMachine) && pCommand->iLastIP >= GetIPToUse(pMachine))
				|| pMachine == FindMachineByName(pCommand->pMachineName)
				)
			{

				pMachine->canLaunchServerTypes[pCommand->eServerType].eCanLaunch = CAN_LAUNCH_SPECIFIED;

				if (!pMachine->bIsLocalHost)
				{
					if (spLocalMachine->canLaunchServerTypes[pCommand->eServerType].eCanLaunch == CAN_LAUNCH_DEFAULT)
					{
						spLocalMachine->canLaunchServerTypes[pCommand->eServerType].eCanLaunch = CAN_NOT_LAUNCH;
					}
				}

				if (pCommand->pScriptString_Use && pCommand->pScriptString_Use[0])
				{
					int j;
					char **ppTempList = NULL;
					ExtractAlphaNumTokensFromString(pCommand->pScriptString_Use, &ppTempList);

					for (j=0; j < eaSize(&ppTempList); j++)
					{
						eaPushUnique(&pMachine->canLaunchServerTypes[pCommand->eServerType].ppCategories, allocAddString(ppTempList[j]));
					}

					eaDestroyEx(&ppTempList, NULL);
				}				
			}
			break;

		//if at some point we are going to want to launch servers of a given type on this machine,
		//set that it can launch servers of that type (but do NOT forbid them on localhost)
		case CONTROLLERCOMMAND_LAUNCH_NORMALLY:
		case CONTROLLERCOMMAND_LAUNCH_DIRECTLY:
		case CONTROLLERCOMMAND_PREPARE_FOR_LAUNCH:
			if (pCommand->iFirstIP && (pCommand->iFirstIP <= GetIPToUse(pMachine) && pCommand->iLastIP >= GetIPToUse(pMachine))
				|| pMachine == FindMachineByName(pCommand->pMachineName))
			{
				pMachine->canLaunchServerTypes[pCommand->eServerType].eCanLaunch = CAN_LAUNCH_SPECIFIED;
			}
			break;


		}
	}
}

void ControllerScripting_LogString(char *pString, bool bImportant, bool bIsError)
{
	TrackedServerState *pServer = gpServersByType[GLOBALTYPE_MASTERCONTROLPROGRAM];

	char shortFileName[MAX_PATH];

	if (bIsError && sbErrorsDisabled)
	{
		return;
	}

	getFileNameNoExt(shortFileName, controllerScriptFileName);

	if (gbSendStringsToContinuousBuilder)
	{
		ControllerScripting_SendStringToContinuousBuilder(pString, !bImportant);
	}


	printf("Script update: %s\n", pString);

	filelog_printf(STACK_SPRINTF("scripting/%s", shortFileName), "%s", pString);

	if (bIsError)
	{
		giControllerScriptingErrorCount++;
	}

	if (bIsError && gErrorLogFileName[0])
	{
		static FILE *gpErrorLogFile = NULL;

		if (!gpErrorLogFile)
		{
			gpErrorLogFile = fopen(gErrorLogFileName, "wt");
		}

		fprintf(gpErrorLogFile, "%s\n", pString);
	}



	while (pServer)
	{
		if (pServer->pLink)
		{
			Packet *pPak = pktCreate(pServer->pLink, FROM_CONTROLLER_SCRIPT_UPDATE_FOR_MCP);
			pktSendString(pPak, pString);
			pktSendBits(pPak, 1, bImportant);
			pktSend(&pPak);
		}
		pServer = pServer->pNext;
	}

	if (bIsError && gbFailOnError)
	{
		ControllerScripting_Fail(STACK_SPRINTF("Failure to due error %s", pString));
	}

}

void ResetGettingNextServerForCommand(ControllerScriptingCommand *pCommand)
{
	pCommand->iNextServerIndex = 0;

}

void ResetAllCommandStepResults(void)
{
	int i;

	for (i=0; i < GLOBALTYPE_MAXTYPES; i++)
	{
		TrackedServerState *pServer = gpServersByType[i];

		while (pServer)
		{
			pServer->controllerScriptingCommandStepResultString[0] = 0;
			pServer->iControllerScriptingCommandStepResult = 0;

			pServer = pServer->pNext;
		}
	}

	iXBOXClientControllerScriptingCommandStepResult = 0;
	XBOXClientControllerScriptingCommandStepResultString[0] = 0;
}

char *ControllerScripting_GetCurStepName()
{
	static char logString[1024];
	char tempString[1024];

	tempString[0] = 0;

	if (gCommandStep < 0 || gCommandStep >= eaSize(&gCommandList.ppCommands))
	{
		sprintf(logString, "Invalid/unknown step %d", gCommandStep);
		return logString;
	}

	switch (gCommandList.ppCommands[gCommandStep]->eCommand)
	{
	case CONTROLLERCOMMAND_SPECIFY_MACHINE:
	case CONTROLLERCOMMAND_LAUNCH_NORMALLY:
	case CONTROLLERCOMMAND_LAUNCH_DIRECTLY:
	case CONTROLLERCOMMAND_KILLSERVER:
	case CONTROLLERCOMMAND_PREPARE_FOR_LAUNCH:
	case CONTROLLERCOMMAND_WAITFORSERVERDEATH:
		sprintf(tempString, "%s", GlobalTypeToName(gCommandList.ppCommands[gCommandStep]->eServerType));
		break;

	case CONTROLLERCOMMAND_EXECUTECOMMAND:
	case CONTROLLERCOMMAND_EXECUTECOMMAND_WAIT:
	case CONTROLLERCOMMAND_WAITFORSTATE:
	case CONTROLLERCOMMAND_SETSERVERTYPECOMMANDLINE:
	case CONTROLLERCOMMAND_APPENDSERVERTYPECOMMANDLINE:
		sprintf(tempString, "%s \"%s\"", 
			GlobalTypeToName(gCommandList.ppCommands[gCommandStep]->eServerType),
			gCommandList.ppCommands[gCommandStep]->pScriptString_Use);
		break;

	case CONTROLLERCOMMAND_REPEATEDLY_QUERY_SERVER:
		sprintf(tempString, "%s \"%s\"(\"%s\")",
			GlobalTypeToName(gCommandList.ppCommands[gCommandStep]->eServerType),
			gCommandList.ppCommands[gCommandStep]->pScriptString_Use,
			gCommandList.ppCommands[gCommandStep]->pScriptResultString_Use);
		break;

	case CONTROLLERCOMMAND_REPEATEDLY_QUERY_ALL_SERVERS:
		sprintf(tempString, "\"%s\"(\"%s\")",
			gCommandList.ppCommands[gCommandStep]->pScriptString_Use,
			gCommandList.ppCommands[gCommandStep]->pScriptResultString_Use);
		break;



	case CONTROLLERCOMMAND_SETSHAREDCOMMANDLINE:
	case CONTROLLERCOMMAND_APPENDSHAREDCOMMANDLINE:
	case CONTROLLERCOMMAND_SYSTEM:
	case CONTROLLERCOMMAND_INCLUDE:
	case CONTROLLERCOMMAND_SETVAR:
	case CONTROLLERCOMMAND_FOR:
	case CONTROLLERCOMMAND_EXECUTECOMMAND_WAIT_XBOXCLIENT:
	case CONTROLLERCOMMAND_EXECUTECOMMAND_XBOXCLIENT:
	case CONTROLLERCOMMAND_WAITFORXBOXCLIENTSTATE:
	case CONTROLLERCOMMAND_TOUCH_FILES:
	case CONTROLLERCOMMAND_GOTO:
	case CONTROLLERCOMMAND_IMPORT_VARIABLES:
	case CONTROLLERCOMMAND_WAITFOREXPRESSION:
	case CONTROLLERCOMMAND_REM:
	case CONTROLLERCOMMAND_WAITFORCONFIRM:
		sprintf(tempString, "\"%s\"", 
			gCommandList.ppCommands[gCommandStep]->pScriptString_Use);
		break;

	case CONTROLLERCOMMAND_EXITWITHVALUE:
		sprintf(tempString, "%d", 
			gCommandList.ppCommands[gCommandStep]->iScriptInt);
		break;

	case CONTROLLERCOMMAND_WAIT:
		if (gCommandList.ppCommands[gCommandStep]->iScriptInt)
		{
			sprintf(tempString, "%d", 
				gCommandList.ppCommands[gCommandStep]->iScriptInt);
		}
		else
		{
			sprintf(tempString, "%f", 
				gCommandList.ppCommands[gCommandStep]->fScriptFloat);
		}
	default:
		break;
	}

	sprintf(logString, "Step %d (%s %s)", gCommandStep, 
		StaticDefineIntRevLookup(enumControllerCommandEnum, gCommandList.ppCommands[gCommandStep]->eCommand), tempString);

	return logString;
}


void NextCommandStep(void)
{
	static char *pTempString = NULL;
	assert(gCommandList.ppCommands);
	ResetAllCommandStepResults();
	StartErrorTracking();

	//keep going forwards until we find a step with out an ifExpression that is false
	while (1)
	{
		gCommandStep++;

		if (gCommandStep >= eaSize(&gCommandList.ppCommands))
		{
			break;
		}

		if (!gCommandList.ppCommands[gCommandStep]->pIfExpression_Raw)
		{
			break;
		}

		ControllerScripting_DoVariableReplacingInStrings(gCommandList.ppCommands[gCommandStep]->pIfExpression_Raw, 
			&gCommandList.ppCommands[gCommandStep]->pIfExpression_Use);

		estrPrintf(&pTempString, "About to evaluate Expr \"%s\" (orig \"%s\") to see if we should execute step %s",
			gCommandList.ppCommands[gCommandStep]->pIfExpression_Use,
			gCommandList.ppCommands[gCommandStep]->pIfExpression_Raw, 
			ControllerScripting_GetCurStepName());

		ControllerScripting_LogString(pTempString, false, false);

		if (ControllerScripting_IsExpressionStringTrue(gCommandList.ppCommands[gCommandStep]->pIfExpression_Use))
		{
			ControllerScripting_LogString("Expression was TRUE", false, false);
			break;
		}
		else
		{
			ControllerScripting_LogString("Expression was FALSE... skipping step", false, false);
		}

	}
	
	gTimeCurStepStarted = timeSecondsSince2000_ForceRecalc();
	gMsecTimeCurStepStarted = timeMsecsSince2000();

	if (gCommandStep < eaSize(&gCommandList.ppCommands))
	{
		ControllerScripting_DoVariableReplacing(gCommandList.ppCommands[gCommandStep]);

		if (gCommandList.ppCommands[gCommandStep]->pDisplayString_Use)
		{
			ControllerScripting_LogString(gCommandList.ppCommands[gCommandStep]->pDisplayString_Use, 1, 0);
		}

		ControllerScripting_LogString(STACK_SPRINTF("NEW STEP: %s\n", ControllerScripting_GetCurStepName()), 0, 0);

		Controller_SetStartupStatusString("main", "New scripting step: %s", ControllerScripting_GetCurStepName());

		ResetGettingNextServerForCommand(gCommandList.ppCommands[gCommandStep]);
		gCommandList.ppCommands[gCommandStep]->bFirstTime = true;

	}
}

void ControllerScrpting_AbortAndCleanupCurrentStep()
{
	//this may need to be more sophisticated in the future. Right now, the only step that needs cleaning up is
	//SYSTEM, and it's always safe to call this function

	KillQueryableProcess(&pScriptingProcessHandle);
	
}

void ControllerScripting_Fail(char *pFailureString)
{
	ControllerScrpting_AbortAndCleanupCurrentStep();

	log_printf(LOG_CONTROLLER, "%s", pFailureString);

//don't change this string without changing the corresponding place in the ContinuousBuilder where it looks for
//Command execution failed
	ControllerScripting_LogString(STACK_SPRINTF("Error in step %s\nCommand execution failed: %s", ControllerScripting_GetCurStepName(), pFailureString), 1, 1);
	ControllerScripting_SetState(CONTROLLERSCRIPTING_FAILED);
	commFlushAllLinks(comm_controller);
	Sleep(50);

	gCommandStepBeforeFailure = gCommandStep;
	gCommandStep = eaSize(&gCommandList.ppCommands);

	
}

void ControllerScripting_Fail_ForInString(char *pFailureString, void *pUserData)
{
	ControllerScripting_Fail(pFailureString);
}

bool ControllerScripting_MaybeFail(ControllerScriptingCommand *pCommand, char *pFailureString)
{
	switch (pCommand->eSubType)
	{
	case CSCSUBTYPE_MUSTSUCCEED:
		ControllerScripting_Fail(pFailureString);
		return true;

	case CSCSUBTYPE_IGNORERESULT:
		return false;

	case CSCSUBTYPE_FAILURE_IS_NON_FATAL:
		log_printf(LOG_CONTROLLER, "%s", pFailureString);
		ControllerScripting_LogString(STACK_SPRINTF("NON-FATAL Error in step %s\nCommand execution failed: %s", ControllerScripting_GetCurStepName(), pFailureString), 0, 1);
		return false;
	}

	return false;

}
		
void ControllerScripting_GetExtraTimeoutErrorString(char **ppErrorString, ControllerScriptingCommand* pCommand)
{
	int i;
	switch (pCommand->eCommand)
	{
	case CONTROLLERCOMMAND_KILLALL:
		for (i=0; i < eaSize(&ppServersToKill); i++)
		{
			if (FindServerFromID(ppServersToKill[i]->eContainerType, ppServersToKill[i]->iContainerID))
			{
				estrConcatf(ppErrorString, " COULD NOT KILL: %s", GlobalTypeAndIDToString( ppServersToKill[i]->eContainerType, ppServersToKill[i]->iContainerID ));
			}
		}
		break;
	}
}



bool CheckForTimeFailure(bool bDontActuallyFail)
{
	assert(gCommandList.ppCommands);

	if (gCommandStep < eaSize(&gCommandList.ppCommands))
	{
		U32 iCurTime;

		if (gTimeCurStepStarted == 0)
		{
			gTimeCurStepStarted = timeSecondsSince2000_ForceRecalc();
			return false;
		}

		if (gCommandList.ppCommands[gCommandStep]->iFailureTime == 0)
		{
			return false;
		}

		//LAUNCH_NORMALLY always has a very long delay because the server that is being launched 
		//will be waiting on previous servers, which might take a long time. If you want finer control,
		//use keepalives
		if (gCommandList.ppCommands[gCommandStep]->eCommand == CONTROLLERCOMMAND_LAUNCH_NORMALLY)
		{
			if (gCommandList.ppCommands[gCommandStep]->iFailureTime < gLaunchNormallyTime)
			{
				gCommandList.ppCommands[gCommandStep]->iFailureTime = gLaunchNormallyTime;
			}
		}

		iCurTime = timeSecondsSince2000();

		if (iCurTime - gTimeCurStepStarted > gCommandList.ppCommands[gCommandStep]->iFailureTime)
		{
			ControllerScripting_LogString(STACK_SPRINTF("Time exceeded while executing step %s", ControllerScripting_GetCurStepName()), 0, 1);
			if (!bDontActuallyFail)
			{
				char *pErrorString = NULL;
				estrPrintf(&pErrorString, "Failure time overflow");
				ControllerScripting_GetExtraTimeoutErrorString(&pErrorString, gCommandList.ppCommands[gCommandStep]);
				ControllerScripting_Fail(pErrorString);
				estrDestroy(&pErrorString);
			}
			return true;
		}
		else
		{
			setConsoleTitle(STACK_SPRINTF("Time out in %d seconds", gCommandList.ppCommands[gCommandStep]->iFailureTime - (iCurTime - gTimeCurStepStarted)));
		}
	}

	return false;
}


bool CommandAppliesToAllServers(ControllerScriptingCommand *pCommand)
{
	if (pCommand->eCommand == CONTROLLERCOMMAND_KILLALL || pCommand->eCommand == CONTROLLERCOMMAND_REPEATEDLY_QUERY_ALL_SERVERS)
	{
		return true;
	}

	return false;
}

TrackedServerState *FindNextServerForCommandFromAllServers(ControllerScriptingCommand *pCommand)
{

	int iCounter = 0;

	int eType;

	for (eType = 0; eType < GLOBALTYPE_MAXTYPES; eType++)
	{
		if (!gServerTypeInfo[eType].bIgnoredByControllerScripting)
		{
			TrackedServerState *pServer = gpServersByType[eType];

			while (pServer)
			{
				if (iCounter == pCommand->iNextServerIndex)
				{
					pCommand->iNextServerIndex++;
					return pServer;
				}

				pServer = pServer->pNext;
				iCounter++;
			}
		}
	}

	pCommand->iNextServerIndex = -1;
	return NULL;
}

TrackedServerState *FindNextServerForCommand(ControllerScriptingCommand *pCommand, bool bOKIfDoesntExist)
{
	//if we're done returning things return NULL
	if (pCommand->iNextServerIndex == -1)
	{
		return NULL;
	}

	if (CommandAppliesToAllServers(pCommand))
	{
		return FindNextServerForCommandFromAllServers(pCommand);
	}

	//if a single server is requested, return it, then set ourselves to done
	if (pCommand->iServerIndex != -1)
	{
		TrackedServerState *pServer = gpServersByType[pCommand->eServerType];

		pCommand->iNextServerIndex = -1;

		while (pServer)
		{
			if (pServer->iScriptingIndex == pCommand->iServerIndex)
			{
				return pServer;
			}

			pServer = pServer->pNext;
		}
		
		if (bOKIfDoesntExist)
		{
			return NULL;
		}
		else
		{
			ControllerScripting_Fail(STACK_SPRINTF("Invalid server index %d of type %s requested",
				pCommand->iServerIndex, GlobalTypeToName(pCommand->eServerType)));
		}
	}

	assert(pCommand->eServerType >= 0 && pCommand->eServerType < GLOBALTYPE_MAXTYPES);

	//all servers requested
	{
		int iCounter = 0;
		TrackedServerState *pServer = gpServersByType[pCommand->eServerType];

		while (pServer)
		{
			if (iCounter == pCommand->iNextServerIndex)
			{
				pCommand->iNextServerIndex++;
				return pServer;
			}

			pServer = pServer->pNext;
			iCounter++;
		}

		pCommand->iNextServerIndex = -1;
		return NULL;
	}
}


void SendCommandToClientThroughTestClientPort(GlobalType eType, U32 iContainerID, char *pCommandString)
{
	TrackedServerState *pServer = FindServerFromID(eType, iContainerID);
	Packet *pPack;

	assert(pServer && pServer->eContainerType == GLOBALTYPE_CLIENT);

	if (!pServer->pTestClientLink)
	{
		pServer->pTestClientLink  = commConnect(comm_controller,LINKTYPE_UNSPEC, LINK_FORCE_FLUSH,
			pServer->pMachine->machineName,CLIENT_SIMPLE_COMMAND_PORT,0,0,0,0);
	
		//super long connect so that the client has time to finish sending its error dump, if any
		if (pServer->pTestClientLink && linkConnectWait(&pServer->pTestClientLink, 240.f))
		{
			//yay, we connected
		}
		else
		{
			ControllerScripting_Fail("Could not connect to client to sent it commands... perhaps it is still busy doing something else?");
			return;
		}
	}

	pPack = pktCreate(pServer->pTestClientLink, FROM_TESTCLIENT_CMD_SENDCOMMAND);
	pktSendString(pPack, GetProductName());
	pktSendString(pPack, pCommandString);
	pktSend(&pPack);
}


ControllerScriptingCommand *ControllerScripting_FindMatchingFOR(int iEndForIndex, int *piFoundIndex)
{
	int i = iEndForIndex - 1;
	int iDepth = 0;
	assert(gCommandList.ppCommands[iEndForIndex]->eCommand == CONTROLLERCOMMAND_ENDFOR);

	while (i>=0)
	{
		switch (gCommandList.ppCommands[i]->eCommand)
		{
		case CONTROLLERCOMMAND_FOR:
			if (iDepth == 0)
			{
				*piFoundIndex = i;
				return gCommandList.ppCommands[i];
			}
			iDepth--;
			break;
		case CONTROLLERCOMMAND_ENDFOR:
			iDepth++;
			break;
		}

		i--;
	}

	return NULL;
}


bool ControllerScripting_IsRunning(void)
{
	return ControllerScripting_GetState() == CONTROLLERSCRIPTING_RUNNING || ControllerScripting_GetState() == CONTROLLERSCRIPTING_LOADING;
}

bool ControllerScripting_WereThereErrors(void)
{
	return giControllerScriptingErrorCount > 0;
}

AUTO_COMMAND;
void ControllerScripting_RestartAfterTimeout(void)
{
	if (ControllerScripting_GetState() == CONTROLLERSCRIPTING_FAILED && gCommandStepBeforeFailure >= 0)
	{
		gCommandStep = gCommandStepBeforeFailure;

		ControllerScripting_SetState(CONTROLLERSCRIPTING_RUNNING);
	}
}


//returns how many steps are in the FOR command (must be at least one)
int ControllerScripting_GetMaxFORCount(ControllerScriptingCommand *pCommand)
{
	int iCommaCount = 0;
	char *pNextComma = pCommand->pScriptString_Use;
	assert(pCommand->eCommand == CONTROLLERCOMMAND_FOR);

	while ((pNextComma = strchr(pNextComma, ',')))
	{
		iCommaCount++;
		pNextComma++;		
	}

	return iCommaCount+1;
}


void ControllerScripting_FindAllMachinesForLaunchNormallyCommand(ControllerScriptingCommand *pCommand)
{
	TrackedMachineState *pMachine;
		int i;

	eaDestroy(&pCommand->ppMachines);

	if (pCommand->pMachineName[0] && pCommand->pMachineName[0] != '*')
	{
		pMachine = FindMachineByName(pCommand->pMachineName);

		if (!pMachine)
		{
			ControllerScripting_Fail(STACK_SPRINTF("Couldn't find required machine %s\n", 
				pCommand->pMachineName));
			return;
		}

		eaPush(&pCommand->ppMachines, pMachine);
		return;
	}

	//if this command is only going to launch one server, we can just pick a single machine and put it in
	//the list. Otherwise, this command could affect all machines capable of launching this server type
	if (pCommand->iCount == 1 && !gServerTypeInfo[pCommand->eServerType].bScriptingLaunchesAsManyOfTheseAsAllowed)
	{
		if (pCommand->iFirstIP)
		{
			pMachine = FindDefaultMachineForTypeInIPRange(pCommand->eServerType, NULL, pCommand->iFirstIP, pCommand->iLastIP, NULL);
			
			if (!pMachine)
			{
				ControllerScripting_Fail(STACK_SPRINTF("Couldn't find required machine in IP range for type %s",
					GlobalTypeToName(pCommand->eServerType)));
				return;
			}
		}
		else
		{
			pMachine = FindDefaultMachineForType(pCommand->eServerType, NULL, NULL);

			if (!pMachine)
			{
				ControllerScripting_Fail(STACK_SPRINTF("Couldn't find default machine for type %s", GlobalTypeToName(pCommand->eServerType)));
				return;
			}
		}

		eaPush(&pCommand->ppMachines, pMachine);
		return;
	}

	if (pCommand->iFirstIP)
	{

		for (i=0; i < giNumMachines; i++)
		{
			if (GetIPToUse(&gTrackedMachines[i]) >= pCommand->iFirstIP && GetIPToUse(&gTrackedMachines[i]) <= pCommand->iLastIP)
			{
				eaPush(&pCommand->ppMachines, &gTrackedMachines[i]);
			}
		}

		if (!eaSize(&pCommand->ppMachines))
		{
			ControllerScripting_Fail(STACK_SPRINTF("Couldn't find any machines for type %s", GlobalTypeToName(pCommand->eServerType)));
		}

		return;
	}

	assert(pCommand->eServerType >= 0 && pCommand->eServerType < GLOBALTYPE_MAXTYPES);

	for (i=0; i < giNumMachines; i++)
	{
		if (gTrackedMachines[i].canLaunchServerTypes[pCommand->eServerType].eCanLaunch)
		{
			eaPush(&pCommand->ppMachines, &gTrackedMachines[i]);
		}
	}

	if (!eaSize(&pCommand->ppMachines))
	{
		ControllerScripting_Fail(STACK_SPRINTF("Couldn't find any machines for type %s", GlobalTypeToName(pCommand->eServerType)));
	}
}

/*given a script string and a result string (the script string being the one in the script, and the result
string being the one currently generated by some command on some server, returns true if they "match". Whether
they match depends on the syntax of the script string. It can be:
"> %d"
"= %d"
"< %d"
In this case, a match is true iff the result string can legally be parsed as an int,
and if it meets the given comparison (if the string is "> -7", the function returns true if resultString is "-5" 
   but not if resultString is "-8" or "asdfasdf" or ""

It can also be
"STR_CONTAINS %s" (matches if %s is a substring of result string)
"STR_EQUALS %s" (matches if %s stricmps exactly with result string)
"STR_CONTAINED_BY %s" (matches if result string is a substring of %s)*/
bool DoScriptStringCompare(char *pScriptString_Use, char *pResultString)
{
	while (*pScriptString_Use == ' ')
	{
		pScriptString_Use++;
	}

	if (pScriptString_Use[0] == '>' || pScriptString_Use[0] == '=' || pScriptString_Use[0] == '<')
	{
		int iScriptInt;
		int iResultInt;

		if (sscanf(pScriptString_Use + 1, "%d", &iScriptInt) != 1)
		{
			assertmsgf(0, "Couldn't find int in scriptResultString %s", pScriptString_Use);
		}

		if (sscanf(pResultString, "%d", &iResultInt) != 1)
		{
			return false;
		}

		if (pScriptString_Use[0] == '>')
		{
			return iResultInt > iScriptInt;
		}
		if (pScriptString_Use[0] == '<')
		{
			return iResultInt < iScriptInt;
		}

		return iResultInt == iScriptInt;
	}

	if (strStartsWith(pScriptString_Use, STR_CONTAINS))
	{
		char *pScriptStringToUse = pScriptString_Use + strlen(STR_CONTAINS);
		while (*pScriptStringToUse == ' ')
		{
			pScriptStringToUse++;
		}

		return strstri(pResultString, pScriptStringToUse) != NULL;
	}

	if (strStartsWith(pScriptString_Use, STR_EQUALS))
	{
		char *pScriptStringToUse = pScriptString_Use + strlen(STR_EQUALS);
		while (*pScriptStringToUse == ' ')
		{
			pScriptStringToUse++;
		}

		return stricmp(pResultString, pScriptStringToUse) == 0;
	}

	if (strStartsWith(pScriptString_Use, STR_CONTAINED_BY))
	{
		char *pScriptStringToUse = pScriptString_Use + strlen(STR_CONTAINED_BY);
		while (*pScriptStringToUse == ' ')
		{
			pScriptStringToUse++;
		}

		return strstri(pScriptStringToUse, pResultString) != NULL;
	}

	assertmsgf(0, "Can't parse ScriptResultString %s", pScriptString_Use);

	return false;
}

bool XboxIsAtShellOrDumping(void)
{
	bool bReady;
	static char *pExeName = NULL;

	xboxQueryStatusFromThread(&bReady, NULL, &pExeName);

	if (bReady && (strstri(pExeName, "xshell.xex") || strstri(pExeName, "processdump.xex")))
	{
		return true;
	}

	return false;
}

void ShowOrHideAllLaunchers(void)
{
	TrackedServerState *pServer = gpServersByType[GLOBALTYPE_LAUNCHER];

	while (pServer)
	{
		Packet *pOutPack = pktCreate(pServer->pLink, LAUNCHERQUERY_HIDEPROCESS);
		PutContainerIDIntoPacket(pOutPack, pServer->iContainerID);
		PutContainerTypeIntoPacket(pOutPack, pServer->eContainerType);
		pktSendBits(pOutPack, 1, ShouldWindowActuallyBeHidden(VIS_UNSPEC, GLOBALTYPE_LAUNCHER));

		pktSend(&pOutPack);

		pServer = pServer->pNext;
	}




}

void ControllerScripting_Update(void)
{
	TrackedServerState *pServer;
	static bool bLoaded = false;
	//if we were started by an MCP, wait for that MCP to be linked to us before beginning scripting
	if (sUIDOfMCPThatStartedMe)
	{
		if (!gbMCPThatStartedMeIsReady)
		{
			return;
		}
	}

	if (ControllerScripting_GetState() == CONTROLLERSCRIPTING_CANCELLED)
	{
		Contoller_StartupStuffToDoWhenScriptingNotRunning();
		return;
	}


	if (!bLoaded)
	{
		char *pErrorString = NULL;
		bLoaded = true;
		ControllerScripting_Load();
	
		if (!ControllerStartup_ConnectionStuffWithSentryServer(&pErrorString))
		{
			Controller_FailWithAlert(pErrorString);
			exit(-1);
		}
	
		gTimeCurStepStarted = timeSecondsSince2000_ForceRecalc();
		gMsecTimeCurStepStarted = timeMsecsSince2000();

		ControllerScripting_SetupMachines();

		if (gbShutdownWatcher)
		{
			if (Controller_GetClusterControllerName() && Controller_GetClusterControllerName()[0])
			{
				char *pCmdLine = NULL;
				char **ppExes = GetListOfMustShutItselfDownExes();
				int i;
				int iPointerSize = sizeof(void*);

				estrPrintf(&pCmdLine, "c:\\cryptic\\tools\\bin\\ClusterAwareShutdownWatcher%s.exe -ShardName %s -MainExecutable %s controller%s %d", 
					(iPointerSize == 8 ? "X64" : ""), GetShardNameFromShardInfoString(), getHostName(), (iPointerSize == 8 ? "X64" : ""), _getpid());

				for (i=0; i < giNumMachines; i++)
				{
					estrConcatf(&pCmdLine, " -machine %s ", gTrackedMachines[i].bIsLocalHost ? getHostName() : gTrackedMachines[i].machineName);
				}

				if (gbControllerMachineGroups_SystemIsActive)
				{
					ControllerMachineGroups_AddMachinesToShutDownWatcherCommandLine(&pCmdLine);
					estrConcatf(&pCmdLine, " -FolderForExes %s ", getExecutableTopDirCropped());
				}

				for (i=0; i < eaSize(&ppExes); i++)
				{
					estrConcatf(&pCmdLine, " -exeToTrack %s ", ppExes[i]);
				}

				estrConcatf(&pCmdLine, " -exeToTrack launcher.exe -exeToTrack launcherX64.exe");

				SentryServerComm_RunCommand_1Machine(Controller_GetClusterControllerName(), pCmdLine);

				estrDestroy(&pCmdLine);




			}
			else
			{
				char *pCmdLine = NULL;
				char **ppExes = GetListOfMustShutItselfDownExes();
				int i;
				int iPointerSize = sizeof(void*);

				estrPrintf(&pCmdLine, "c:\\cryptic\\tools\\bin\\ShutdownWatcher%s.exe -MainExecutable controller%s.exe %d", (iPointerSize == 8 ? "X64" : ""), (iPointerSize == 8 ? "X64" : ""), _getpid());

				for (i=0; i < giNumMachines; i++)
				{
					estrConcatf(&pCmdLine, " -machine %s ", gTrackedMachines[i].bIsLocalHost ? getHostName() : gTrackedMachines[i].machineName);
				}

				if (gbControllerMachineGroups_SystemIsActive)
				{
					ControllerMachineGroups_AddMachinesToShutDownWatcherCommandLine(&pCmdLine);
					estrConcatf(&pCmdLine, " -FolderForExes %s ", getExecutableTopDirCropped());
				}

				for (i=0; i < eaSize(&ppExes); i++)
				{
					estrConcatf(&pCmdLine, " -exeToTrack %s ", ppExes[i]);
				}

				estrConcatf(&pCmdLine, " -exeToTrack launcher.exe -exeToTrack launcherX64.exe");

				system_detach(pCmdLine, false, false);

				estrDestroy(&pCmdLine);
			}
		}


		
	}



	if (!gCommandList.ppCommands)
	{
		Contoller_StartupStuffToDoWhenScriptingNotRunning();
		return;
	}

	if (gCommandStep >= eaSize(&gCommandList.ppCommands))
	{
		if (ControllerScripting_GetState() == CONTROLLERSCRIPTING_RUNNING)
		{
			ControllerScripting_SetState(ControllerScripting_WereThereErrors() ? CONTROLLERSCRIPTING_COMPLETE_W_ERRORS : CONTROLLERSCRIPTING_SUCCEEDED);
		}

		Contoller_StartupStuffToDoWhenScriptingNotRunning();
		return;
	}




	if (gCommandStep == -1)
	{
		ControllerScripting_SetState(CONTROLLERSCRIPTING_RUNNING);
		NextCommandStep();
		return;
	}

	do
	{

		ControllerScriptingCommand *pCurCommand = gCommandList.ppCommands[gCommandStep];

		if (pCurCommand->eCommand != CONTROLLERCOMMAND_SYSTEM)
		{
			//system commands handle time failure specially, since they need to able to fail nonfatally and so forth
			if (CheckForTimeFailure(false))
			{
				return;
			}
		}

		switch (pCurCommand->eCommand)
		{
		case CONTROLLERCOMMAND_LAUNCH_NORMALLY:
		case CONTROLLERCOMMAND_PREPARE_FOR_LAUNCH:
			if (pCurCommand->bFirstTime)
			{
				pCurCommand->bFirstTime = false;
				ControllerScripting_FindAllMachinesForLaunchNormallyCommand(pCurCommand);
			}
	
			GetServerTypeReadyToLaunchOnMachines(pCurCommand->eServerType, false, pCurCommand->ppMachines);

			if (ServerTypeIsReadyToLaunchOnMachines(pCurCommand->eServerType, pCurCommand->ppMachines))
			{
				if (pCurCommand->eCommand == CONTROLLERCOMMAND_LAUNCH_NORMALLY)
				{
					LaunchServerFromCommand(pCurCommand);
				}
				NextCommandStep();
			}
			return;

		case CONTROLLERCOMMAND_LAUNCH_DIRECTLY:
			LaunchServerFromCommand(pCurCommand);
			NextCommandStep();
			return;


		case CONTROLLERCOMMAND_EXECUTECOMMAND:
			//execute on the controller itself
			if (pCurCommand->eServerType == GLOBALTYPE_NONE || pCurCommand->eServerType == GLOBALTYPE_CONTROLLER)
			{
				globCmdParse(pCurCommand->pScriptString_Use);
			}
			else if (pCurCommand->eServerType == GLOBALTYPE_CLIENT)
			{
				while ((pServer = FindNextServerForCommand(pCurCommand, false)))
				{
					SendCommandToClientThroughTestClientPort(pCurCommand->eServerType, pServer->iContainerID,
						pCurCommand->pScriptString_Use);
				}
			}
			else
			{
				while ((pServer = FindNextServerForCommand(pCurCommand, false)))
				{
					RemoteCommand_CallLocalCommandRemotely(pCurCommand->eServerType, pServer->iContainerID,
						pCurCommand->pScriptString_Use);
				}
			}
			NextCommandStep();
			return;

		case CONTROLLERCOMMAND_EXECUTECOMMAND_XBOXCLIENT:
		case CONTROLLERCOMMAND_EXECUTECOMMAND_WAIT_XBOXCLIENT:
			if (gbNoXBOX)
			{
				NextCommandStep();
				return;
			}

			if (pCurCommand->bFirstTime)
			{
				Packet *pPak = pktCreate(spLocalMachine->pServersByType[GLOBALTYPE_LAUNCHER]->pLink, FROM_CONTROLLER_RELAY_COMMAND_TO_XBOX_CLIENT);
				pktSendString(pPak, pCurCommand->pScriptString_Use);
				pktSend(&pPak);
				pCurCommand->bFirstTime = false;
			}

			if (pCurCommand->eCommand == CONTROLLERCOMMAND_EXECUTECOMMAND_XBOXCLIENT)
			{
				NextCommandStep();
				return;
			}
	
			if (iXBOXClientControllerScriptingCommandStepResult == 1)
			{
				NextCommandStep();
				return;
			}

			if (iXBOXClientControllerScriptingCommandStepResult == -1)
			{
				if (!ControllerScripting_MaybeFail(pCurCommand, XBOXClientControllerScriptingCommandStepResultString))
				{
					NextCommandStep();
				}
				return;
			}

			if (timeSecondsSince2000_ForceRecalc() > gTimeCurStepStarted + 5)
			{
				if (XboxIsAtShellOrDumping())
				{
					if (!ControllerScripting_MaybeFail(pCurCommand, "XBox reset while we were waiting for a command to complete"))
					{
						NextCommandStep();
					}
				}
			}
			
			return;




		case CONTROLLERCOMMAND_EXECUTECOMMAND_WAIT:
			{
				bool bAllDone = true;

				if (pCurCommand->bFirstTime)
				{
		
					pCurCommand->bFirstTime = false;

					//execute on the controller itself
					if (pCurCommand->eServerType == GLOBALTYPE_NONE || pCurCommand->eServerType == GLOBALTYPE_CONTROLLER)
					{
						assertmsgf(0, "Error while executing command %s... commands can't be executed on the controller itself",
							pCurCommand->pScriptString_Use);
					}
					else if (pCurCommand->eServerType == GLOBALTYPE_CLIENT)
					{
						while ((pServer = FindNextServerForCommand(pCurCommand, false)))
						{
							SendCommandToClientThroughTestClientPort(pCurCommand->eServerType, pServer->iContainerID,
								pCurCommand->pScriptString_Use);
						}
					}
					else
					{
						while ((pServer = FindNextServerForCommand(pCurCommand, false)))
						{
							RemoteCommand_CallLocalCommandRemotely(pCurCommand->eServerType, pServer->iContainerID,
								pCurCommand->pScriptString_Use);
						}
					}
				}

				ResetGettingNextServerForCommand(pCurCommand);
				
				while ((pServer = FindNextServerForCommand(pCurCommand, false)))
				{
					

					if (pServer->iControllerScriptingCommandStepResult == 0)
					{
						bAllDone = false;
					}
					else if (pServer->iControllerScriptingCommandStepResult == -1)
					{
						ControllerScripting_MaybeFail(pCurCommand, pServer->controllerScriptingCommandStepResultString);
					}
				}
				

				if (bAllDone)
				{
					NextCommandStep();
					return;
				}
			}
			return;

		case CONTROLLERCOMMAND_WAITFORCONFIRM:
			if (pCurCommand->bFirstTime)
			{
				pCurCommand->bFirstTime = false;
				Controller_SetServerMonitorBannerString(pCurCommand->pScriptString_Use);
				sbUserConfirmed = false;
			}

			if (sbUserConfirmed)
			{
				Controller_SetServerMonitorBannerString(NULL);
				NextCommandStep();
			}
			return;


		case CONTROLLERCOMMAND_WAITFORSERVERDEATH:
			{
				ResetGettingNextServerForCommand(pCurCommand);

				while ((pServer = FindNextServerForCommand(pCurCommand, true)))
				{
					return;
				}

				NextCommandStep();
				return;
			}

		case CONTROLLERCOMMAND_WAITFORSTATE:
			{
				bool bFoundAtLeastOne = false;

				assert(pCurCommand->pScriptString_Use);


				ResetGettingNextServerForCommand(pCurCommand);

				while ((pServer = FindNextServerForCommand(pCurCommand, true)))
				{
					bFoundAtLeastOne = true;

					if (!(strstri(pServer->stateString, pCurCommand->pScriptString_Use)))
					{
						return;
					}
				}

				if (!bFoundAtLeastOne)
				{
					return;
				}

				NextCommandStep();
				return;
			}

		case CONTROLLERCOMMAND_LAUNCHXBOXCLIENT:
			{
				char *pSystemString = NULL;
				char *pWorkingDir;
	
				char programFilesFolder[CRYPTIC_MAX_PATH];

				GetEnvironmentVariable("ProgramFiles", SAFESTR(programFilesFolder));


				if (gbNoXBOX)
				{
					NextCommandStep();
					return;
				}

				estrStackCreate(&pSystemString);

				estrPrintf(&pSystemString, "echo -IsContinuousBuilder %d -SetAccountServer %s -SetTicketTracker %s -SetTestingMode -TestClientIsController -SetErrorTracker %s -localHost %s -server %s -RCOLDL 1 - %s - %s - %s - %s > c:\\tempcmdline.txt",
					g_isContinuousBuilder,				
					makeIpStr(ipFromString(getAccountServer())),
					makeIpStr(ipFromString(getTicketTracker())),
					makeIpStr(ipFromString(getErrorTracker())),
					makeIpStr(spLocalMachine->IP), 
					makeIpStr(spLocalMachine->IP),
					pCurCommand->pExtraCommandLine_Use ? pCurCommand->pExtraCommandLine_Use : "", GetGlobalSharedCommandLine(),
					gServerTypeInfo[GLOBALTYPE_CLIENT].pSharedCommandLine_FromScript ? gServerTypeInfo[GLOBALTYPE_CLIENT].pSharedCommandLine_FromScript : "",
					gServerTypeInfo[GLOBALTYPE_CLIENT].pSharedCommandLine_FromMCP ? gServerTypeInfo[GLOBALTYPE_CLIENT].pSharedCommandLine_FromMCP : "");
				
				system(pSystemString);

				if (pCurCommand->pWorkingDir_Use)
				{
					pWorkingDir = pCurCommand->pWorkingDir_Use;
				}
				else
				{
					pWorkingDir = "xe:\\fcgameclient";
				}

				estrPrintf(&pSystemString, "\"%s\\microsoft xbox 360 sdk\\bin\\win32\\xbcp\" -Y -T c:\\tempcmdline.txt %s\\cmdline.txt", programFilesFolder, pWorkingDir);
				system(pSystemString);

				estrPrintf(&pSystemString, "\"%s\\microsoft xbox 360 sdk\\bin\\win32\\xbreboot\" ",programFilesFolder ); 

				if (pCurCommand->pScriptString_Use)
				{
					estrConcatf(&pSystemString, "%s", pCurCommand->pScriptString_Use);
				}
				else
				{
					estrConcatf(&pSystemString, "%s\\gameclientxbox.exe", pWorkingDir);
				}

				system(pSystemString);
				estrDestroy(&pSystemString);
				NextCommandStep();
				gXBOXClientState[0] = 0;

			}
			return;

		case CONTROLLERCOMMAND_KILLXBOXCLIENT:
			{
				char systemString[2048];
				char programFilesFolder[CRYPTIC_MAX_PATH];

				GetEnvironmentVariable("ProgramFiles", SAFESTR(programFilesFolder));

				if (gbNoXBOX)
				{
					NextCommandStep();
					return;
				}

				sprintf(systemString, "\"%s\\microsoft xbox 360 sdk\\bin\\win32\\xbreboot\"", programFilesFolder);
				system(systemString);
				NextCommandStep();
				gXBOXClientState[0] = 0;

			}
			return;



		case CONTROLLERCOMMAND_WAITFORXBOXCLIENTSTATE:
			if (gbNoXBOX)
			{
				NextCommandStep();
				return;
			}
	
			
			assert(pCurCommand->pScriptString_Use);

			if (!(strstri(gXBOXClientState, pCurCommand->pScriptString_Use)))
			{
				if (timeSecondsSince2000_ForceRecalc() > gTimeCurStepStarted + 5)
				{
					if (XboxIsAtShellOrDumping())
					{
						if (!ControllerScripting_MaybeFail(pCurCommand, "XBox reset while we were waiting for xbox client state"))
						{
							NextCommandStep();
						}
					}
				}
				return;
			}

			

			NextCommandStep();
			return;



		case CONTROLLERCOMMAND_WAITFORSTEPDONE:
			{
				bool bAllDone = true;

				ResetGettingNextServerForCommand(pCurCommand);
				
				while ((pServer = FindNextServerForCommand(pCurCommand, false)))
				{
					

					if (pServer->iControllerScriptingCommandStepResult == 0)
					{
						bAllDone = false;
					}
					else if (pServer->iControllerScriptingCommandStepResult == -1)
					{
						ControllerScripting_MaybeFail(pCurCommand, pServer->controllerScriptingCommandStepResultString);
					}
				}
				

				if (bAllDone)
				{
					NextCommandStep();
					return;
				}
			}
			return;
			
		case CONTROLLERCOMMAND_EXITWITHVALUE:
			exit(pCurCommand->iScriptInt);
			return;

		case CONTROLLERCOMMAND_KILLSERVER:
		case CONTROLLERCOMMAND_KILLALL:
			{
				//need to make list of servers to kill before iterating to avoid list
				//corruption
				int i;

				if (ppServersToKill == NULL)
				{

					while ((pServer = FindNextServerForCommand(pCurCommand, true)))
					{
						ServerToKill *pServerToKill = calloc(sizeof(ServerToKill), 1);
						pServerToKill->pServer = pServer;
						pServerToKill->eContainerType = pServer->eContainerType;
						pServerToKill->iContainerID = pServer->iContainerID;

						eaPush(&ppServersToKill, pServerToKill);
					}

					if (eaSize(&ppServersToKill) == 0)
					{

						NextCommandStep();
						return;
					}

					//during a KILLALL, disable CB error reporting, as lots of fake errors tend to show up then
					if (pCurCommand->eCommand == CONTROLLERCOMMAND_KILLALL)
					{
						StopErrorTracking();
					}

					for (i=0; i <eaSize(&ppServersToKill); i++)
					{
						pServer = ppServersToKill[i]->pServer;

						pServer->bKilledIntentionally = true;

						if (pServer->eContainerType == GLOBALTYPE_TRANSACTIONSERVER)
						{
							objShutdownTransactions();
						}

						if (pCurCommand->pScriptString_Use)
						{
							if (pServer->eContainerType == GLOBALTYPE_CLIENT)
							{
								SendCommandToClientThroughTestClientPort(pServer->eContainerType, pServer->iContainerID,
									pCurCommand->pScriptString_Use);
							}
							else
							{
								RemoteCommand_CallLocalCommandRemotely(pServer->eContainerType, pServer->iContainerID,
									pCurCommand->pScriptString_Use);						
							}
						}
						else if (pServer->pLink)
						{
						Packet *pak = pktCreate(pServer->pLink, FROM_CONTROLLER_KILLYOURSELF);
							pktSend(&pak);
						}
						else
						{
							if (pServer->pMachine->pServersByType[GLOBALTYPE_LAUNCHER])
							{
								KillServer(pServer, "Controller Scripting Command");
							}
						}
					}
					return;
				}
				else
				{

					//wait until all killed servers are well and truly dead
					for (i=0; i < eaSize(&ppServersToKill); i++)
					{
						if (FindServerFromID(ppServersToKill[i]->eContainerType, ppServersToKill[i]->iContainerID))
						{
							return;
						}
					}

					eaDestroyEx(&ppServersToKill, NULL);

					NextCommandStep();
					return;
				}
			}

		case CONTROLLERCOMMAND_SETSHAREDCOMMANDLINE:
			SetGlobalSharedCommandLine(pCurCommand->pScriptString_Use);
			NextCommandStep();
			return;

		case CONTROLLERCOMMAND_APPENDSHAREDCOMMANDLINE:
			AppendGlobalSharedCommandLine(pCurCommand->pScriptString_Use);
			NextCommandStep();
			return;

		case CONTROLLERCOMMAND_SETSERVERTYPECOMMANDLINE:
			estrCopy2(&gServerTypeInfo[pCurCommand->eServerType].pSharedCommandLine_FromScript, pCurCommand->pScriptString_Use ? pCurCommand->pScriptString_Use : "");
			if (pCurCommand->eVisibility)
			{
				gServerTypeInfo[pCurCommand->eServerType].eVisibility_FromScript = pCurCommand->eVisibility;
				if (pCurCommand->eServerType == GLOBALTYPE_LAUNCHER)
				{
					ShowOrHideAllLaunchers();
				}			
			}			
			NextCommandStep();
			return;

		case CONTROLLERCOMMAND_APPENDSERVERTYPECOMMANDLINE:
			estrConcatf(&gServerTypeInfo[pCurCommand->eServerType].pSharedCommandLine_FromScript, " %s", pCurCommand->pScriptString_Use ? pCurCommand->pScriptString_Use : "");
			if (pCurCommand->eVisibility)
			{
				gServerTypeInfo[pCurCommand->eServerType].eVisibility_FromScript = pCurCommand->eVisibility;
				if (pCurCommand->eServerType == GLOBALTYPE_LAUNCHER)
				{
					ShowOrHideAllLaunchers();
				}
			}
			NextCommandStep();
			return;

		case CONTROLLERCOMMAND_SETMODE_TESTING:
			AppendGlobalSharedCommandLine(" -?SetTestingMode 1 -SendAllErrorsToController 1 -?noWorldBinsOnError 1 -NoStackDumpsOnErrors 1 -?SendStateToController 1 ");
			NextCommandStep();
			return;

		case CONTROLLERCOMMAND_SYSTEM:
			{
				int iRetVal;

				if (pCurCommand->bFirstTime)
				{
		
					pCurCommand->bFirstTime = false;

					if (pCurCommand->eLoggingServerType)
					{
						estrConcatf(&pCurCommand->pScriptString_Use, " %s", GetPrintfForkingCommand(pCurCommand->eLoggingServerType, 0));
					}
					
					if (pCurCommand->pWorkingDir_Use && pCurCommand->pWorkingDir_Use[0])
					{
						char originalWorkingDir[CRYPTIC_MAX_PATH];
						fileGetcwd(SAFESTR(originalWorkingDir));
						if (chdir(pCurCommand->pWorkingDir_Use) != 0)
						{
							ControllerScripting_Fail(STACK_SPRINTF("Couldn't change to dir %s for system command %s", 
								pCurCommand->pWorkingDir_Use, pCurCommand->pScriptString_Use));
							return;
						}

						pScriptingProcessHandle = StartQueryableProcess(pCurCommand->pScriptString_Use, NULL, false, false, false, NULL);
						assert(chdir(originalWorkingDir) == 0);
					}
					else
					{
						pScriptingProcessHandle = StartQueryableProcess(pCurCommand->pScriptString_Use, NULL, false, false, false, NULL);
					}



					if (!pScriptingProcessHandle)
					{
						ControllerScripting_Fail(STACK_SPRINTF("Couldn't execute command line %s", pCurCommand->pScriptString_Use));
						return;
					}
					return;
				}

				if (QueryableProcessComplete(&pScriptingProcessHandle, &iRetVal))
				{
					if (iRetVal == 0)
					{
						NextCommandStep();
						return;
					}
					else
					{
						if (!ControllerScripting_MaybeFail(pCurCommand, STACK_SPRINTF("Got error result %d from command line %s", iRetVal, pCurCommand->pScriptString_Use)))
						{
							NextCommandStep();
							return;
						}

						return;
					}
				}
				else
				{ 
					if (CheckForTimeFailure(true))
					{
						KillQueryableProcess(&pScriptingProcessHandle);
						if (!ControllerScripting_MaybeFail(pCurCommand, "Failure time overflow"))
						{
							NextCommandStep();
							return;
						}
					}
				}
			}
			return;

		case CONTROLLERCOMMAND_SETFAILONERROR:
			gbFailOnError = true;
			NextCommandStep();
			return;

		case CONTROLLERCOMMAND_UNSETFAILONERROR:
			gbFailOnError = false;
			NextCommandStep();
			return;

		case CONTROLLERCOMMAND_REM:
			ControllerScripting_LogString(pCurCommand->pScriptString_Use ? pCurCommand->pScriptString_Use : "(no comment)", true, false);
			NextCommandStep();
			return;



		case CONTROLLERCOMMAND_WAIT:
			if (!((pCurCommand->iScriptInt > 0 && pCurCommand->fScriptFloat == 0) || (pCurCommand->iScriptInt == 0 && pCurCommand->fScriptFloat > 0)))
			{
				ControllerScripting_Fail(STACK_SPRINTF("WAIT command must have either an int or a float > 0 (%d %f)", pCurCommand->iScriptInt, pCurCommand->fScriptFloat));
				return;
			}

			pCurCommand->iFailureTime = 0;

			if (pCurCommand->fScriptFloat)
			{
				if (timeMsecsSince2000() - gMsecTimeCurStepStarted > (int)(pCurCommand->fScriptFloat * 1000.0f))
				{
					NextCommandStep();
				}
				else
				{
					return;
				}
			}
			else
			{
				if (timeSecondsSince2000() - gTimeCurStepStarted > (U32)(pCurCommand->iScriptInt))
				{
					NextCommandStep();
				}
				else
				{
					return;
				}
			}
			return;

		case CONTROLLERCOMMAND_SETVAR:
			if (!ControllerScripting_SetVarFromString(pCurCommand->pScriptString_Use, -1))
			{
				ControllerScripting_Fail(STACK_SPRINTF("Couldn't parse SETVAR string \"%s\"", pCurCommand->pScriptString_Use));
			}
			NextCommandStep();
			return;

		case CONTROLLERCOMMAND_IMPORT_VARIABLES:
			if (ControllerScripting_ImportVariables(pCurCommand->pScriptString_Use))
			{
				NextCommandStep();
			}
			return;

		case CONTROLLERCOMMAND_FOR:
			if (!pCurCommand->bLooping)
			{
				pCurCommand->iForCount = 0;
			}
			pCurCommand->bLooping = false;

			if (!ControllerScripting_SetVarFromString(pCurCommand->pScriptString_Use, pCurCommand->iForCount))
			{
				ControllerScripting_Fail(STACK_SPRINTF("Couldn't parse FOR string \"%s\" (forCount %d)", pCurCommand->pScriptString_Use, pCurCommand->iForCount));
			}
			NextCommandStep();
			return;

		case CONTROLLERCOMMAND_ENDFOR:
			{
				int iForCommandIndex;
				ControllerScriptingCommand *pForCommand = ControllerScripting_FindMatchingFOR(gCommandStep, &iForCommandIndex);

				if (!pForCommand)
				{
					ControllerScripting_Fail("NonMatching ENDFOR");
					return;
				}

				pForCommand->iForCount++;
				if (pForCommand->iForCount == ControllerScripting_GetMaxFORCount(pForCommand))
				{
					NextCommandStep();
				}
				else
				{
					pForCommand->bLooping = true;
					gCommandStep = iForCommandIndex - 1;
					NextCommandStep();
				}
			}
			return;
		case CONTROLLERCOMMAND_REPEATEDLY_QUERY_SERVER:
		case CONTROLLERCOMMAND_REPEATEDLY_QUERY_ALL_SERVERS:
			{
				static U32 iLastSentTime = 0;
				bool bFoundAtLeastOneServer = false;
				bool bAllDone = true;
				bool bSendThisFrame = false;

				if (pCurCommand->bFirstTime)
				{
					pCurCommand->bFirstTime = false;
					iLastSentTime = 0;
				}

				if (iLastSentTime < timeSecondsSince2000() - 1)
				{
					bSendThisFrame = true;
					iLastSentTime = timeSecondsSince2000();
				}



				ResetGettingNextServerForCommand(pCurCommand);
				
				while ((pServer = FindNextServerForCommand(pCurCommand, true)))
				{
					if (!gServerTypeInfo[pServer->eContainerType].bCanNotBeQueriedByControllerScripting)
					{
						bFoundAtLeastOneServer = true;

						if (DoScriptStringCompare(pCurCommand->pScriptResultString_Use, pServer->controllerScriptingCommandStepResultString) 
							&& pServer->iControllerScriptingCommandStepResult == 1)
						{
							//this server is done... do nothing
						}
						else
						{
							bAllDone = false;

							if (bSendThisFrame)
							{
								if (pServer->eContainerType == GLOBALTYPE_CLIENT)
								{
									char *pCmdString = NULL;

									estrStackCreate(&pCmdString);
									estrPrintf(&pCmdString, "RunCommandAndReturnStringToControllerScripting %s", pCurCommand->pScriptString_Use);

									SendCommandToClientThroughTestClientPort(GLOBALTYPE_CLIENT, pServer->iContainerID,
										pCmdString);

									estrDestroy(&pCmdString);							
								}
								else
								{
									RemoteCommand_RunLocalCommandAndReturnStringToControllerScripting(pServer->eContainerType, pServer->iContainerID,
										pCurCommand->pScriptString_Use);
								}
							}
						}
					}
				}

				if (bFoundAtLeastOneServer && bAllDone)
				{
					NextCommandStep();
				}
			}
			return;

		case CONTROLLERCOMMAND_TOUCH_FILES:
			{
				char **ppPathsToTouch = NULL;
				int i;
				int iNumPaths;

				assertmsg(pCurCommand->iScriptInt > 0, "ScriptInt can't be 0 for TOUCH_FILES");

				DivideString(pCurCommand->pScriptString_Use, ";", &ppPathsToTouch, DIVIDESTRING_POSTPROCESS_NONE);
				iNumPaths = eaSize(&ppPathsToTouch);

				for (i=0; i < iNumPaths; i++)
				{
					char *pAsterisk = strchr(ppPathsToTouch[i], '*');
					char **ppFilesToTouch = NULL;
					int j;


					assertmsgf(pAsterisk, "Badly formed string %s for TOUCH_FILES", ppPathsToTouch[i]);
					*pAsterisk = 0;
					pAsterisk++;

					FindNLargestFilesInDirectory(ppPathsToTouch[i], pAsterisk, pCurCommand->iScriptInt, &ppFilesToTouch);

					for (j=0; j < eaSize(&ppFilesToTouch); j++)
					{
						TouchFile(ppFilesToTouch[j]);
					}

					eaDestroyEx(&ppFilesToTouch, NULL);
				}

				eaDestroyEx(&ppPathsToTouch, NULL);


				NextCommandStep();
				return;


			}

		case CONTROLLERCOMMAND_GOTO:
			{
				int i;

				if (!pCurCommand->pScriptString_Use || !pCurCommand->pScriptString_Use[0])
				{
					ControllerScripting_Fail("Invalid ScriptString in GOTO");
					return;
				}

				for (i=0; i < eaSize(&gCommandList.ppCommands); i++)
				{
					if (gCommandList.ppCommands[i]->pDisplayString_Raw && stricmp(gCommandList.ppCommands[i]->pDisplayString_Raw, pCurCommand->pScriptString_Use) == 0)
					{
						gCommandStep = i-1;
						NextCommandStep();
						return;
					}
				
					if (gCommandList.ppCommands[i]->eCommand == CONTROLLERCOMMAND_REM && gCommandList.ppCommands[i]->pScriptString_Raw && stricmp(gCommandList.ppCommands[i]->pScriptString_Raw, pCurCommand->pScriptString_Use) == 0)
					{
						gCommandStep = i-1;
						NextCommandStep();
						return;
					}
				}

				ControllerScripting_Fail(STACK_SPRINTF("Unknown step %s to go to", pCurCommand->pScriptString_Use));
				return;
			}

		case CONTROLLERCOMMAND_WAITFOREXPRESSION:
			{
				static U32 iLastQueryTime = 0;
				U32 iCurTime = timeSecondsSince2000();

				if (iCurTime > iLastQueryTime + 1)
				{
					iLastQueryTime = iCurTime;

					if (ControllerScripting_IsExpressionStringTrue(pCurCommand->pScriptString_Use))
					{
						NextCommandStep();
					}
				}
			}
			return;



				










			







				



		default:
			NextCommandStep();
			return;
		}
	}
	while (gCommandStep < eaSize(&gCommandList.ppCommands));
}


void ControllerScripting_ServerDied(TrackedServerState *pServer)
{
	if (!pServer->bKilledIntentionally && (gServerTypeInfo[pServer->eContainerType].bCanNotCrash || g_isContinuousBuilder))
	{
		ControllerScripting_Fail(STACK_SPRINTF("Server type %s ID %u died unexpectedly", 
			GlobalTypeToName(pServer->eContainerType), pServer->iContainerID));
	}
}

void ControllerScripting_ServerCrashed(TrackedServerState *pServer)
{
	if(gServerTypeInfo[pServer->eContainerType].bCanNotCrash || g_isContinuousBuilder)
	{
		ControllerScripting_Fail(STACK_SPRINTF("Server type %s ID %u crashed/asserted", 
			GlobalTypeToName(pServer->eContainerType), pServer->iContainerID));
	}
}





AUTO_COMMAND;
char *MakeIPNumeric(char *pInputString)
{
	U32 iIP = ipFromString(pInputString);
	return makeIpStr(iIP);
}


AUTO_COMMAND;
char *ContainerIDFromIndex(char *pGlobalTypeName, int iIndex)
{
	static char retVal[12];
	GlobalType eType = NameToGlobalType(pGlobalTypeName);
	TrackedServerState *pServer = gpServersByType[eType];


	while (pServer)
	{
		if (pServer->iScriptingIndex == iIndex)
		{
			sprintf(retVal, "%u", pServer->iContainerID);
			return retVal;
		}

		pServer = pServer->pNext;
	}

	ControllerScripting_Fail(STACK_SPRINTF("can't get container ID for nonexistent %s index %d", 
		pGlobalTypeName, iIndex));

	sprintf(retVal, "0");
	return retVal;
}

AUTO_COMMAND;
char *MachineNameForServerType(char *pGlobalTypeName)
{
	static char retString[256];
	
	GlobalType eType = NameToGlobalType(pGlobalTypeName);
	TrackedServerState *pServer = gpServersByType[eType];

	if (pServer)
	{
		strcpy(retString, pServer->pMachine->machineName);
		return retString;
	}

	return "UNKNOWN_MACHINE";
}

TextParserResult FixupCommand_ScriptStringOnly(ControllerScriptingCommand* pCommand)
{
	char *pTempString = NULL;
	int i;

	if (pCommand->pScriptString_Raw && eaSize(&pCommand->ppInternalStrs))
	{
		eaDestroyEx(&pCommand->ppInternalStrs, NULL);

		ErrorFilenamef(pCommand->pSourceFileName, "Around line %d: script command %s has both scriptString and internal strings",
			pCommand->iLineNum, StaticDefineIntRevLookup(enumControllerCommandEnum, pCommand->eCommand));
		return PARSERESULT_INVALID;
	}

	if (!eaSize(&pCommand->ppInternalStrs))
	{
		return PARSERESULT_SUCCESS;
	}

	for (i = 0; i < eaSize(&pCommand->ppInternalStrs); i++)
	{
		estrConcatf(&pTempString, "%s%s", i == 0 ? "" : " ", pCommand->ppInternalStrs[i]);
	}

	SAFE_FREE(pCommand->pScriptString_Raw);
	pCommand->pScriptString_Raw = strdup(pTempString);
	estrDestroy(&pTempString);

	return PARSERESULT_SUCCESS;
}

TextParserResult FixupCommand_ServerTypeAndScriptString(ControllerScriptingCommand* pCommand)
{

	if (eaSize(&pCommand->ppInternalStrs) < 1)
	{
		ErrorFilenamef(pCommand->pSourceFileName, "Around line %d: script command %s has no server type",
			pCommand->iLineNum, StaticDefineIntRevLookup(enumControllerCommandEnum, pCommand->eCommand));
		return PARSERESULT_INVALID;
	}

	pCommand->eServerType = NameToGlobalType(pCommand->ppInternalStrs[0]);
	if (!pCommand->eServerType)
	{
		ErrorFilenamef(pCommand->pSourceFileName, "Around line %d: script command %s has unknown server type %s",
		pCommand->iLineNum, StaticDefineIntRevLookup(enumControllerCommandEnum, pCommand->eCommand), pCommand->ppInternalStrs[0]);
		return PARSERESULT_INVALID;
	}

	free(pCommand->ppInternalStrs[0]);
	eaRemove(&pCommand->ppInternalStrs, 0);


	return FixupCommand_ScriptStringOnly(pCommand);
}
	
TextParserResult FixupCommand_IntOrFloat(ControllerScriptingCommand* pCommand)
{
	bool bIntOrFlagSpecified = TokenIsSpecified(parse_ControllerScriptingCommand, PARSE_CONTROLLERSCRIPTINGCOMMAND_SCRIPTINT_INDEX, pCommand, -1) 
		|| TokenIsSpecified(parse_ControllerScriptingCommand, PARSE_CONTROLLERSCRIPTINGCOMMAND_SCRIPTFLOAT_INDEX, pCommand, -1);

	if (eaSize(&pCommand->ppInternalStrs) && bIntOrFlagSpecified)
	{
		ErrorFilenamef(pCommand->pSourceFileName, "Around line %d: script command %s has internalStrs AND a specified int or float",
			pCommand->iLineNum, StaticDefineIntRevLookup(enumControllerCommandEnum, pCommand->eCommand));
		return PARSERESULT_INVALID;
	}
	
	if (bIntOrFlagSpecified)
	{
		return PARSERESULT_SUCCESS;
	}

	if (eaSize(&pCommand->ppInternalStrs) != 1)
	{
		ErrorFilenamef(pCommand->pSourceFileName, "Around line %d: script command %s has no int/float arg",
			pCommand->iLineNum, StaticDefineIntRevLookup(enumControllerCommandEnum, pCommand->eCommand));
		return PARSERESULT_INVALID;
	}

	if (StringToInt_Paranoid(pCommand->ppInternalStrs[0], &pCommand->iScriptInt))
	{
		return PARSERESULT_SUCCESS;
	}

	if (StringToFloat(pCommand->ppInternalStrs[0], &pCommand->fScriptFloat))
	{
		return PARSERESULT_SUCCESS;
	}

	ErrorFilenamef(pCommand->pSourceFileName, "Around line %d: script command %s has invalid int/float %s",
		pCommand->iLineNum, StaticDefineIntRevLookup(enumControllerCommandEnum, pCommand->eCommand), pCommand->ppInternalStrs[0]);
	return PARSERESULT_INVALID;


}

AUTO_FIXUPFUNC;
TextParserResult fixupControllerScriptingCommand(ControllerScriptingCommand* pCommand, enumTextParserFixupType eType, void *pExtraData)
{
	switch (eType)
	{
	case FIXUPTYPE_POST_TEXT_READ:
		if (TokenIsSpecified(parse_ControllerScriptingCommand, PARSE_CONTROLLERSCRIPTINGCOMMAND_STARTHIDDEN_INDEX, pCommand, -1))
		{
			pCommand->eVisibility = pCommand->bStartHidden_Internal ? VIS_HIDDEN : VIS_VISIBLE;
		}

		switch (pCommand->eCommand)
		{

		case CONTROLLERCOMMAND_SPECIFY_MACHINE:
		case CONTROLLERCOMMAND_LAUNCH_NORMALLY:
		case CONTROLLERCOMMAND_LAUNCH_DIRECTLY:
		case CONTROLLERCOMMAND_KILLSERVER:
		case CONTROLLERCOMMAND_PREPARE_FOR_LAUNCH:
		case CONTROLLERCOMMAND_WAITFORSERVERDEATH:
		case CONTROLLERCOMMAND_EXECUTECOMMAND:
		case CONTROLLERCOMMAND_EXECUTECOMMAND_WAIT:
		case CONTROLLERCOMMAND_WAITFORSTATE:
		case CONTROLLERCOMMAND_SETSERVERTYPECOMMANDLINE:
		case CONTROLLERCOMMAND_APPENDSERVERTYPECOMMANDLINE:
		case CONTROLLERCOMMAND_REPEATEDLY_QUERY_SERVER:
		case CONTROLLERCOMMAND_WAITFORSTEPDONE:
			FixupCommand_ServerTypeAndScriptString(pCommand);
			break;

		case CONTROLLERCOMMAND_WAIT:
		case CONTROLLERCOMMAND_EXITWITHVALUE:
			FixupCommand_IntOrFloat(pCommand);
			break;

		default:
			FixupCommand_ScriptStringOnly(pCommand);
			break;




		}



		break;
		

	}

	return PARSERESULT_SUCCESS;
}

void ControllerScripting_GetLikelyCommandLine(GlobalType eServerType, char **ppCommandLine)
{
	int i;

	if (!gCommandList.ppCommands)
	{
		return;
	}

	for (i=0; i < eaSize(&gCommandList.ppCommands); i++)
	{
		ControllerScriptingCommand *pCommand = gCommandList.ppCommands[i];

		switch (pCommand->eCommand)
		{
		case CONTROLLERCOMMAND_LAUNCH_NORMALLY:
		case CONTROLLERCOMMAND_LAUNCH_DIRECTLY: 
		case CONTROLLERCOMMAND_PREPARE_FOR_LAUNCH:
			if (pCommand->eServerType == eServerType)
			{
				estrConcatf(ppCommandLine, " %s", pCommand->pExtraCommandLine_Raw);
			}
			break;

		case CONTROLLERCOMMAND_SETSHAREDCOMMANDLINE:
		case CONTROLLERCOMMAND_APPENDSHAREDCOMMANDLINE:
			estrConcatf(ppCommandLine, " %s", pCommand->pScriptString_Raw);
			break;

		case CONTROLLERCOMMAND_SETSERVERTYPECOMMANDLINE:
		case CONTROLLERCOMMAND_APPENDSERVERTYPECOMMANDLINE:
			if (pCommand->eServerType == eServerType)
			{
				estrConcatf(ppCommandLine, " %s", pCommand->pScriptString_Raw);
			}
			break;
		}
	}
}
	
void ControllerScripting_TemporaryPause(int iForHowManySeconds, FORMAT_STR const char *pReasonFmt, ...)
{
	static U32 siTimeCurPauseWillExpire = 0;
	U32 iCurTime = timeSecondsSince2000_ForceRecalc();

	if (!gTimeCurStepStarted)
	{
		gTimeCurStepStarted = timeSecondsSince2000_ForceRecalc();
	}

	if (siTimeCurPauseWillExpire == 0 || siTimeCurPauseWillExpire < timeSecondsSince2000())
	{
		char *pComment = NULL;
		gTimeCurStepStarted += iForHowManySeconds;
		CBSupport_PauseTimeout(iForHowManySeconds);
		siTimeCurPauseWillExpire = iCurTime + iForHowManySeconds;
		
		estrPrintf(&pComment, "Beginning pause for %d seconds because: ",
			iForHowManySeconds);
		estrGetVarArgs(&pComment, pReasonFmt);

		ControllerScripting_LogString(pComment, true, false);
		estrDestroy(&pComment);



		return;
	}

	if (siTimeCurPauseWillExpire >= iCurTime + iForHowManySeconds)
	{
		return;
	}



	gTimeCurStepStarted += iCurTime + iForHowManySeconds - siTimeCurPauseWillExpire;
	CBSupport_PauseTimeout(iCurTime + iForHowManySeconds - siTimeCurPauseWillExpire);

	siTimeCurPauseWillExpire = iCurTime + iForHowManySeconds;
}



#include "autogen/Controller_scripting_h_ast.c"
