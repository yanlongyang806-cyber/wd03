#include "BuildScripting.h"
#include "TextParser.h"
#include "earray.h"
#include "timing.h"
#include "estring.h"
#include "stringutil.h"
#include "file.h"
#include "simpleparser.h"
#include "stringcache.h"
#include "cmdparse.h"
#include "wininclude.h"
#include "svnutils.h"
#include "Expression.h"
#include "InStringCommands.h"
#include "gimmeutils.h"
#include "sock.h"
#include "NameValuePair.h"

#include "ETCommon/ETCommonStructs.h"

#include "stashTable.h"
#include "fileutil2.h"


#include "autogen/buildscripting_h_ast.h"
#include "autogen/buildscripting_c_ast.h"

#include "objPath.h"
#include "net/net.h"

#include "globalComm.h"
#include "qsortG.h"
#include "utf8.h"

#define MAX_VAR_NAME_LENGTH 256

#define ISOKFORVARNAME(c) ( isalnum(c) || (c) == '_' || (c) == '.')


typedef enum BuildScriptingContextChildBehaviorFlags BuildScriptingContextChildBehaviorFlags;

static InStringCommandsAllCBs sBuildScriptingInStringCommandCBs;

#define BuildScripting_Fail(pContext, fmt, ...) BuildScripting_FailEx(pContext, false, fmt, __VA_ARGS__)
void BuildScripting_FailEx(BuildScriptingContext *pContext, bool bNoStep, char *pInErrorMessage, ...);
void BuildScripting_FailWithStep(BuildScriptingContext *pContext, char *pInErrorMessage);
void BuildScripting_FailWithStepf(BuildScriptingContext *pContext, char *pInErrorMessage, ...);
void BuildScripting_Tick_Internal(BuildScriptingContext *pContext);

bool BuildScripting_DEFINED_AND_TRUE(BuildScriptingContext *pContext, const char *pVarName);
char *BuildScripting_GetCommentForCommand(BuildScriptCommand *pCommand);
BuildScriptingContext *BuildScripting_CreateChildContext(BuildScriptingContext *pParent, bool bIsDetachedChild, int iFirstCommandIndex, int iLastCommandIndex, 
	char *pContextName, BuildScriptingContextChildBehaviorFlags eChildBehaviorFlags);
void BuildScripting_AddDollarSignsIfNecessary(char **ppVarName);


AUTO_STRUCT;
typedef struct BuildScriptingVariable
{
	char *pVarName;
	char *pVarValue;
	char *pComment;
} BuildScriptingVariable;

AUTO_STRUCT;
typedef struct BuildScriptingVariableList
{
	BuildScriptingVariable **ppScriptingVariables;
	BuildScriptingVariable **ppStartingVariables;
	BuildScriptingVariable **ppResettableStartingVariables;
} BuildScriptingVariableList;


AUTO_STRUCT;
typedef struct BuildScriptCommandList
{
	BuildScriptCommand **ppCommands; AST(NAME(Command) FORMATSTRING(DEFAULT_FIELD=1))
} BuildScriptCommandList;


//flags that control the behavior of child scripting contexts
AUTO_ENUM;
typedef enum BuildScriptingContextChildBehaviorFlags
{
	FAILURE_IS_NONFATAL_FOR_PARENT = 1 << 0, //if this child context fails, do NOT instantly and automatically kill the parent

	NON_INTERRUPTIBLE = 1 << 1, //if the parent context fails, we would normally abort all child contexts instantly. However,
		//there are some tasks (ie, detaching symServ pushes) which should complete even if the entire context fails. If
		//the child context is set to NON_INTERRUPTIBLE, then it won't be aborted, but will be allowed to complete
} BuildScriptingContextChildBehaviorFlags;

typedef struct BuildScriptingContext BuildScriptingContext;

AUTO_STRUCT;
typedef struct BuildScriptingContext
{
	char *pContextName; AST(ESTRING)

	//a time (ss2000) at which scripting will fail
	U32 iBuildScriptingFailTime;

	bool bFailAfterDelayCalled;
	bool bSkipCurrentStep;

	char *pFailureLinkString; AST(ESTRING)


	int iInternalIncludeDepth;

	int iCurForDepth;

	bool bInSpecialFailingModeWaitingForUninterruptibleChildContexts;

	bool bForceAbort;

	bool bTestingOnly; //if true, we don't actually do anything, we just print out what we would do

	bool bAbortAndCloseInstantly;

	QueryableProcessHandle *pBuildScriptingProcHandle; NO_AST

	enumBuildScriptState eScriptState;


	BuildScriptCommandList commandList;
	int iCurCommandNum;
	bool bThereWereErrors;
	int iFramesInState;
	U64 iTimeEnteredState; //msecsSince2000

	U32 iScriptStartTime;

//Estrings
	char *pCurDisplayString; AST(ESTRING) //the DisplayString specified in each script command
	char *pCurStepString; AST(ESTRING) //auto-generated string describing the current step, its type, etc.
	char *pFailureMessage; AST(ESTRING)

//any time the build scripting tries to load a file it looks in these directories, in order. Earray of malloced strings
	char **ppDefaultDirectories;

	char compileLogFileName[MAX_PATH];

	char *pMostRecentSystemCommandOutputFileName; AST(ESTRING) 

	BuildScriptingVariableList variables;
	int iNumStartingVariables;

	bool bDisabled;

	BuildScriptingContext *pParent; NO_AST

	//"normal" child contexts... while these are running, the parent is waiting.
	BuildScriptingContext **ppChildren; 
	
	//"detached" child contexts... these continue to run in parallel with the parent. For each, a variable NAME_RESULT will
	//always exist in the parent, starting with RUNNING, and going through various possibilities in enumBuildScriptState.
	
	BuildScriptingContext **ppDetachedChildren; 


	bool bFailing;

	BuildScriptingContextChildBehaviorFlags eChildBehaviorFlags;
} BuildScriptingContext;

bool BuildScripting_IsRootContext(BuildScriptingContext *pContext)
{
	return !pContext->pParent;
}

void BuildScripting_ForceAbort(BuildScriptingContext *pContext)
{
	pContext->bForceAbort = true;
}

void BuildScripting_TotallyDisable(BuildScriptingContext *pContext, bool bDisable)
{
	pContext->bDisabled = bDisable;
}

bool BuildScripting_TotallyDisabled(BuildScriptingContext *pContext)
{
	return pContext->bDisabled;
}

static BuildScriptingContext **sppContextStack = NULL;
BuildScriptingContext *BuildScripting_GetCurExecutingContext(void)
{
	assertmsgf(eaSize(&sppContextStack), "Trying to get cur executing context when it doesn't exist");
	return eaTail(&sppContextStack);
}

void BuildScripting_PushCurExecutingContext(BuildScriptingContext *pContext)
{
	eaPush(&sppContextStack, pContext);
}

void BuildScripting_PopCurExecutingContext(void)
{
	assertmsgf(eaSize(&sppContextStack), "Trying to pop cur executing context when it doesn't exist");
	eaPop(&sppContextStack);
}



//forward declarations
bool BuildScripting_SetVariablesFromFile(BuildScriptingContext *pContext, char *pVariableFileName, bool bFailOnNoFile, char *pComment);
BuildScriptCommand *BuildScripting_FindMatchingFOR(BuildScriptingContext *pContext, int iEndForIndex, int *piFoundIndex);
bool BuildScripting_DoVariableReplacingInStrings(BuildScriptingContext *pContext, char *pSourceString, char **ppDestEString);
bool DEFINED(BuildScriptingContext *pContext, const char *pVarName);


static BuildScriptingContext *spRootContext = NULL;

BuildScriptingContext *GetRootContext(void)
{
	return spRootContext;
}

BuildScriptingContext *BuildScripting_CreateRootContext(void)
{
	assertmsg(!spRootContext, "Can't create root scripting context twice");
	spRootContext = StructCreate(parse_BuildScriptingContext);
	estrPrintf(&spRootContext->pContextName, "Root");

	return spRootContext;
}

bool BuildScripting_IsRunning(BuildScriptingContext *pContext)
{
	return pContext->eScriptState == BUILDSCRIPTSTATE_RUNNING;
}

char *BuildScripting_GetCurDisplayString(BuildScriptingContext *pContext)
{
	return pContext->pCurDisplayString;
}

bool BuildScripting_CantBeInterrupted(BuildScriptingContext *pContext)
{
	if ((pContext->eChildBehaviorFlags & NON_INTERRUPTIBLE)
		&& pContext->eScriptState == BUILDSCRIPTSTATE_RUNNING)
	{
	
		return true;
	}

	return false;
}

bool BuildScripting_CantBeInterrupted_Recurse(BuildScriptingContext *pContext, char **ppOutNameOfUninterruptibleContext)
{
	if ((pContext->eChildBehaviorFlags & NON_INTERRUPTIBLE)
		&& pContext->eScriptState == BUILDSCRIPTSTATE_RUNNING)
	{
		if (ppOutNameOfUninterruptibleContext)
		{
			*ppOutNameOfUninterruptibleContext = pContext->pContextName;
		}
		return true;
	}

	FOR_EACH_IN_EARRAY(pContext->ppChildren, BuildScriptingContext, pChild)
	{
		if (BuildScripting_CantBeInterrupted_Recurse(pChild, ppOutNameOfUninterruptibleContext))
		{
			return true;
		}
	}
	FOR_EACH_END;

	FOR_EACH_IN_EARRAY(pContext->ppDetachedChildren, BuildScriptingContext, pChild)
	{
		if (BuildScripting_CantBeInterrupted_Recurse(pChild, ppOutNameOfUninterruptibleContext))
		{
			return true;
		}
	}
	FOR_EACH_END;

	return false;
}

//generic build scripting logging function
void bsLogf(BuildScriptingContext *pContext, bool bImportant, char *pString, ...)
{
	char *pFullString = NULL;
	VA_START(args, pString);
	estrConcatfv(&pFullString, pString, args);
	VA_END();

	
	if (pContext->pParent)
	{
		consolePushColor();
		consoleSetColor(0, COLOR_GREEN | COLOR_BLUE | COLOR_RED);
		printf("[%s] ", pContext->pContextName);
		consolePopColor();
	}

	if (bImportant)
	{
		consoleSetFGColor(COLOR_GREEN | COLOR_BLUE | COLOR_BRIGHT);
	}
	else
	{
		consoleSetFGColor(COLOR_GREEN | COLOR_BLUE);
	}


	printf("%s\n", pFullString);

	consoleSetFGColor(COLOR_GREEN | COLOR_BLUE | COLOR_RED);


	BuildScriptingLogging(pContext, bImportant, pFullString);
	

	estrDestroy(&pFullString);


}


void BuildScripting_SetTestingOnlyMode(BuildScriptingContext *pContext, bool bTestingOnly)
{
	pContext->bTestingOnly = bTestingOnly;
}


bool BuildScripting_CurrentCommandSetsCBStringsByItself(BuildScriptingContext *pContext)
{
	BuildScriptCommand *pCurCommand;

	if (pContext->eScriptState != BUILDSCRIPTSTATE_RUNNING)
	{
		return false;
	}

	assert(pContext->commandList.ppCommands);
	pCurCommand = pContext->commandList.ppCommands[pContext->iCurCommandNum];

	return pCurCommand->eSubType == BSCSUBTYPE_CONTROLLER_STYLE;
}


enumBuildScriptState BuildScripting_GetState(BuildScriptingContext *pContext)
{
	return pContext->eScriptState;
}


bool BuildScripting_FindFile(BuildScriptingContext *pContext, char *pInFile, char outFile[MAX_PATH])
{
	while (IS_WHITESPACE(*pInFile))
	{
		pInFile++;
	}

	if (pInFile[0] == '\\' || pInFile[0] == '/' || pInFile[1] == ':')
	{
		strcpy_s(outFile, MAX_PATH, pInFile);
		backSlashes(outFile);
		return true;
	}
	else
	{
		int i;

		for (i=eaSize(&pContext->ppDefaultDirectories) - 1; i >= 0; i--)
		{
			char tempName[MAX_PATH];
			
			sprintf(tempName, "%s%s", pContext->ppDefaultDirectories[i], pInFile);
			if (fileExists(tempName))
			{
				strcpy_s(outFile, MAX_PATH, tempName);
				backSlashes(outFile);
				return true;
			}
		}
	}

	return false;
}

bool BuildScripting_FindFile_ForInString(char *pInFile, char outFile[MAX_PATH], BuildScriptingContext *pContext)
{
	return BuildScripting_FindFile(pContext, pInFile, outFile);
}




void BuildScripting_PutVariablesIntoList(BuildScriptingContext *pContext, NameValuePairList *pList)
{
	int i;

	for (i=0; i < eaSize(&pContext->variables.ppScriptingVariables); i++)
	{
		NameValuePair *pPair = StructCreate(parse_NameValuePair);
		pPair->pName = strdup(pContext->variables.ppScriptingVariables[i]->pVarName);
		pPair->pValue = strdup(pContext->variables.ppScriptingVariables[i]->pVarValue);

		eaPush(&pList->ppPairs, pPair);
	}
}


void BuildScripting_AddResettableStartingVariable(BuildScriptingContext *pContext, char *pVarName, char *pVarValue, char *pComment)
{

	char *pVarNameToUse = NULL;
	
	BuildScriptingVariable *pNewVariable = StructCreate(parse_BuildScriptingVariable);


	assertmsgf(pVarName && pVarValue, "Trying to set variable with missing name (%s) or value (%s)",
		pVarName, pVarValue);


	estrPrintf(&pVarNameToUse, "$%s$", pVarName);
	pNewVariable->pVarName = strdup(pVarNameToUse);
	pNewVariable->pVarValue = strdup(pVarValue);
	pNewVariable->pComment = strdup(pComment);

	estrDestroy(&pVarNameToUse);

	eaPush(&pContext->variables.ppResettableStartingVariables, pNewVariable);
}

void BuildScripting_ResetResettableStartingVariables(BuildScriptingContext *pContext)
{
	eaDestroyStruct(&pContext->variables.ppResettableStartingVariables, parse_BuildScriptingVariable);
}

void BuildScripting_WriteCurrentVariablesToText(BuildScriptingContext *pContext, char* pTextFileName )
{
	int i;
	FILE *pFile = fopen(pTextFileName, "wt");
	if (pFile)
	{
		for (i=0; i < eaSize(&pContext->variables.ppScriptingVariables); i++)
		{
			fprintf(pFile, "//%s\n%s: %s\n\n", pContext->variables.ppScriptingVariables[i]->pComment, pContext->variables.ppScriptingVariables[i]->pVarName, pContext->variables.ppScriptingVariables[i]->pVarValue);
		}
		fclose(pFile);
	}


}

/*
AUTO_COMMAND;
void SetVar(char *pVarName, char *pVarValue, CmdContext *pContext)
{
	char *pVarNameToUse = NULL;
	BuildScriptingVariable *pNewVariable = calloc(sizeof(BuildScriptingVariable), 1);

	estrPrintf(&pVarNameToUse, "$%s$", pVarName);
	pNewVariable->pVarName = strdup(pVarNameToUse);
	pNewVariable->pVarValue = strdup(pVarValue);
	pNewVariable->pComment = strdupf("SetVar command called by: %s", StaticDefineIntRevLookup(enumCmdContextHowCalledEnum, pContext->eHowCalled));

	estrDestroy(&pVarNameToUse);

	eaPush(&pContext->variables.ppStartingVariables, pNewVariable);
}
*/

void SetVariableFromFile(BuildScriptingContext *pContext, char *pVarName, char *pFileName, bool bEscaped, char *pComment)
{
	int iSize;
	char *pBuf = fileAlloc(pFileName, &iSize);
	static char *pVarNameToUse = NULL;
	estrPrintf(&pVarNameToUse, "$%s$", pVarName);

	if (!pBuf)
	{
		BuildScripting_AddVariable(pContext, pVarNameToUse, "", pComment);
	}
	else
	{
		if (bEscaped)
		{
			char *pTemp = NULL;
			estrAppendEscaped(&pTemp, pBuf);
			BuildScripting_AddVariable(pContext, pVarNameToUse, pTemp, pComment);
			estrDestroy(&pTemp);
		}
		else
		{
			BuildScripting_AddVariable(pContext, pVarNameToUse, pBuf, pComment);
		}
		free(pBuf);
	}
}




void BuildScripting_DumpAllVariables(BuildScriptingContext *pContext, char **ppEString, bool bComments)
{
	int i;
	char *pFixedVarValue = NULL;;

	BuildScripting_PushCurExecutingContext(pContext);

	for (i=0; i < eaSize(&pContext->variables.ppScriptingVariables); i++)
	{
		BuildScripting_DoVariableReplacingInStrings(pContext, pContext->variables.ppScriptingVariables[i]->pVarValue, &pFixedVarValue);
		if (bComments)
		{
			estrConcatf(ppEString, "//%s\n", pContext->variables.ppScriptingVariables[i]->pComment);
		}
		estrConcatf(ppEString, "%s = \"%s\"\n", pContext->variables.ppScriptingVariables[i]->pVarName, pFixedVarValue);
		if (bComments)
		{
			estrConcatf(ppEString, "\n");
		}

	}

	estrDestroy(&pFixedVarValue);
	BuildScripting_PopCurExecutingContext();

}



void BuildScripting_AddVariable_Internal(BuildScriptingContext *pContext, char *pVarName, const char *pVarValue_Orig, char *pComment, bool bDontTrimWhiteSpace)
{
	int i;
	BuildScriptingVariable *pNewVar;
	char *pVarValue_Dup = strdup(pVarValue_Orig);

	for (i = (int)strlen(pVarName) - 2; i >= 1; i--)
	{
		if (!ISOKFORVARNAME(pVarName[i]))
		{
			BuildScripting_Fail(pContext, "Trying to add variable named %s... this contains illegal character %c(%d)",
				pVarName, pVarName[i], (int)(pVarName[i]));
		}
	}


	//need to strdup twice... first, make a copy, then trim the whitespace on it, so we don't
	//destroy the original string passed in. Then, make a copy with the whitespace trimmed
	//then free the first dup

	if (!bDontTrimWhiteSpace)
	{
		char *pTemp = pVarValue_Dup;

		removeTrailingWhiteSpaces(pVarValue_Dup);
		while (IS_WHITESPACE(*pVarValue_Dup))
		{
			pVarValue_Dup++;
		}

		pVarValue_Dup = strdup(pVarValue_Dup);
		free(pTemp);
	}


	for (i=0; i < eaSize(&pContext->variables.ppScriptingVariables); i++)
	{
		if (stricmp(pContext->variables.ppScriptingVariables[i]->pVarName, pVarName) == 0)
		{
			if (i < pContext->iNumStartingVariables)
			{
				free(pVarValue_Dup);
				return;
			}
			free(pContext->variables.ppScriptingVariables[i]->pVarValue);
			pContext->variables.ppScriptingVariables[i]->pVarValue = pVarValue_Dup;
			SAFE_FREE(pContext->variables.ppScriptingVariables[i]->pComment);
			pContext->variables.ppScriptingVariables[i]->pComment = strdup(pComment);
			return;
		}
	}

	pNewVar = StructCreate(parse_BuildScriptingVariable);

	pNewVar->pVarName = strdup(pVarName);
	pNewVar->pVarValue = pVarValue_Dup;
	pNewVar->pComment = strdup(pComment);

	eaPush(&pContext->variables.ppScriptingVariables, pNewVar);

	BuildScriptingVariableWasSet(pContext, pVarName, pVarValue_Dup);


}

void BuildScripting_AddVariable(BuildScriptingContext *pContext, char *pVarName, const char *pVarValue_Orig, char *pComment)
{
	BuildScripting_AddVariable_Internal(pContext, pVarName, pVarValue_Orig, pComment, false);
}



bool BuildScripting_ClearAndResetVariables(BuildScriptingContext *pContext)
{
	int i;
	static char *pTempComment = NULL;

	pContext->iScriptStartTime = timeSecondsSince2000_ForceRecalc();

	for (i=0;i < eaSize(&pContext->variables.ppScriptingVariables); i++)
	{
		free(pContext->variables.ppScriptingVariables[i]->pVarName);
		free(pContext->variables.ppScriptingVariables[i]->pVarValue);
		free(pContext->variables.ppScriptingVariables[i]);
	}

	eaDestroy(&pContext->variables.ppScriptingVariables);
	pContext->iNumStartingVariables = 0;


	{
		char dateString[256];
		char labelDateString[256];
		int iLen;
		SYSTEMTIME t;

		timeMakeLocalDateStringFromSecondsSince2000(dateString, pContext->iScriptStartTime);
		iLen = (int)strlen(dateString);

		for (i=0; i < iLen; i++)
		{
			if (!isalnum(dateString[i]))
			{
				dateString[i] = '_';
			}
		}

		timerLocalSystemTimeFromSecondsSince2000(&t,pContext->iScriptStartTime);
		sprintf(labelDateString, "%04d%02d%02d_%02d%02d",
			t.wYear, t.wMonth, t.wDay, t.wHour, t.wMinute);


	
		BuildScripting_AddVariable(pContext, "$BUILDSTARTTIME$", dateString, "BUILTIN_STARTUP");
		BuildScripting_AddVariable(pContext, "$BUILDSTARTTIME_LABEL$", labelDateString, "BUILTIN_STARTUP");

		BuildScripting_AddVariable(pContext, "$LOCALHOST$", makeIpStr(getHostPublicIp()), "BUILTIN_STARTUP");
		BuildScripting_AddVariable(pContext, "$LOCALHOSTNAME$", getHostName(), "BUILTIN_STARTUP");

		BuildScripting_AddStartingVariables(pContext);
	}

	for (i=0; i < eaSize(&pContext->variables.ppStartingVariables); i++)
	{
		estrPrintf(&pTempComment, "Starting variable because: %s", pContext->variables.ppStartingVariables[i]->pComment);
		BuildScripting_AddVariable(pContext, pContext->variables.ppStartingVariables[i]->pVarName, pContext->variables.ppStartingVariables[i]->pVarValue, pTempComment);
	}

	pContext->iNumStartingVariables = eaSize(&pContext->variables.ppScriptingVariables);

	for (i=0; i < eaSize(&pContext->variables.ppResettableStartingVariables); i++)
	{
		estrPrintf(&pTempComment, "Resettable starting variable because: %s", pContext->variables.ppResettableStartingVariables[i]->pComment);
		BuildScripting_AddVariable(pContext, pContext->variables.ppResettableStartingVariables[i]->pVarName, pContext->variables.ppResettableStartingVariables[i]->pVarValue, pTempComment);
	}

	return true;
}


bool BuildScripting_VariableExists(BuildScriptingContext *pContext, char *pVarName)
{
	int i;

	for (i=0; i < eaSize(&pContext->variables.ppScriptingVariables); i++)
	{
		if (stricmp(pContext->variables.ppScriptingVariables[i]->pVarName, pVarName) == 0)
		{
			return true;
		}
	}

	return false;
}

bool BuildScripting_FindVarValue(BuildScriptingContext *pContext, char *pVarName, char **ppVarValue)
{
	int i;

	for (i=0; i < eaSize(&pContext->variables.ppScriptingVariables); i++)
	{
		if (stricmp(pContext->variables.ppScriptingVariables[i]->pVarName, pVarName) == 0)
		{
			BuildScripting_DoVariableReplacingInStrings(pContext, pContext->variables.ppScriptingVariables[i]->pVarValue, ppVarValue);
			return true;
		}
	}

	return false;
}

bool BuildScripting_FindRawVarValueAndComment(BuildScriptingContext *pContext, char *pVarName, char **ppVarValue, char **ppComment)
{
	int i;

	for (i=0; i < eaSize(&pContext->variables.ppScriptingVariables); i++)
	{
		if (stricmp(pContext->variables.ppScriptingVariables[i]->pVarName, pVarName) == 0)
		{
			*ppVarValue = pContext->variables.ppScriptingVariables[i]->pVarValue;

			if (ppComment)
			{
				*ppComment = pContext->variables.ppScriptingVariables[i]->pComment;
			}
			
			return true;
		}
	}

	return false;

}



void BuildScripting_SaveVariablesForNextRun(BuildScriptingContext *pContext)
{
	if (BuildScripting_IsRootContext(pContext))
	{
		ParserWriteTextFile(BuildScriptingGetStatusFileLocation("lastRunVars.txt"), parse_BuildScriptingVariableList, &pContext->variables, 0, 0);
	}
}



AUTO_EXPR_FUNC(buildScripting) ACMD_NAME(LASTRUN_VARIABLE);
char *BuildScripting_GetLastRunVariable(const char *pVarName)
{
	char *pVarNameToUse = NULL;
	int i;
	static BuildScriptingVariableList list = {0};
	StructReset(parse_BuildScriptingVariableList, &list);
	ParserReadTextFile(BuildScriptingGetStatusFileLocation("lastRunVars.txt"), parse_BuildScriptingVariableList, &list, PARSER_NOINCLUDES);
	estrStackCreate(&pVarNameToUse);
	if (pVarName[0] == '$')
	{
		estrCopy2(&pVarNameToUse, pVarName);
	}
	else
	{
		estrPrintf(&pVarNameToUse, "$%s$", pVarName);
	}

	for (i=0; i < eaSize(&list.ppScriptingVariables); i++)
	{
		if (stricmp(list.ppScriptingVariables[i]->pVarName, pVarNameToUse) == 0)
		{
			return list.ppScriptingVariables[i]->pVarValue;
		}
	}

	return "__UNDEFINED__";
}


static void BuildScripting_TerminateCommandList(BuildScriptCommandList *pList)
{
	BuildScriptCommand *pCommand = StructCreate(parse_BuildScriptCommand);
	pCommand->eCommand = BUILDSCRIPTCOMMAND_WAIT_FOR_DETACHED_CHILDREN;
	eaPush(&pList->ppCommands, pCommand);
}

//when a child consists of an include of a file and absolutely nothing else, we end up with
//an extra unnamed timing step because the childing adds an include layer and the including adds
//an include layer, so there's one layer with nothing in it at all... fix that
static void BuildScripting_FixupIncludeOnlyChildIncludeDepth(BuildScriptCommandList *pList)
{
	int iSize =  eaSize(&pList->ppCommands);
	BuildScriptCommand *pFirst, *pSecond, *pLast;

	if (iSize < 4)
	{
		return;
	}

	pFirst = pList->ppCommands[0];
	pSecond = pList->ppCommands[1];
	pLast = pList->ppCommands[iSize - 1];

	if (pFirst->eCommand == BUILDSCRIPTCOMMAND_BEGIN_CHILD 
		&& pLast->eCommand == BUILDSCRIPTCOMMAND_END_CHILD
		&& pSecond->eCommand == BUILDSCRIPTCOMMAND_INCLUDE
		&& pSecond->iScriptInt == iSize - 3)
	{
		int i;
		for (i = 2; i < iSize - 1; i++)
		{
			pList->ppCommands[i]->iIncludeDepth--;
		}
	}
}


//prepends a log to a file if AUTOLOG_pTag_FILENAME and AUTOLOG_pTag_BODY all exist. Then truncates that file
//to 500 lines
void BuildScripting_DoAutoLogging(BuildScriptingContext *pContext, char *pTag)
{
	char *pFnameVarName = NULL;
	char *pFnameVarValue = NULL;
	char *pBodyVarName = NULL;
	char *pBodyVarValue = NULL;
	char *pFileContents = NULL;

	if (!BuildScripting_IsRootContext(pContext))
	{
		return;
	}

	estrPrintf(&pFnameVarName, "$AUTOLOG_%s_FILENAME$", pTag);
	estrPrintf(&pBodyVarName, "$AUTOLOG_%s_BODY$", pTag);

	if (BuildScripting_VariableExists(pContext, pFnameVarName) && BuildScripting_VariableExists(pContext, pBodyVarName))
	{
		char *pBuffer;
		FILE *pFile;

		BuildScripting_FindVarValue(pContext, pFnameVarName, &pFnameVarValue);
		BuildScripting_FindVarValue(pContext, pBodyVarName, &pBodyVarValue);

		pBuffer = fileAlloc(pFnameVarValue, NULL);

		if (pBuffer)
		{
			estrCopy2(&pFileContents, pBuffer);
			free(pBuffer);
		}

		estrAppend2(&pBodyVarValue, "\n");

		estrInsert(&pFileContents, 0, pBodyVarValue, estrLength(&pBodyVarValue));

		estrTruncateAtNthOccurrence(&pFileContents, '\n', 500);

		pFile = fopen(pFnameVarValue, "wb");
		if (!pFile)
		{
			printf("ERROR: Couldn't open %s for writing\n", pFnameVarValue);
		}
		else
		{
			fwrite(pFileContents, estrLength(&pFileContents)+1, 1, pFile);
			fclose(pFile);	
		}
	}
	else
	{
		printf("one of %s, %s undefined, not doing auto-logging", pFnameVarName, pBodyVarName);
	}

	estrDestroy(&pFnameVarName);
	estrDestroy(&pFnameVarValue);
	estrDestroy(&pBodyVarName );
	estrDestroy(&pBodyVarValue );
	estrDestroy(&pFileContents);
}


//sends an email if AUTOEMAIL_pTag_RECIPIENTS, _SUBJECTLINE and _BODY all exist.
void BuildScripting_SendAutoEmail(BuildScriptingContext *pContext, char *pTag)
{
	char *pRecipientsVarName = NULL;
	char *pRecipientsVarValue = NULL;
	char *pSubjectLineVarName = NULL;
	char *pSubjectLineVarValue = NULL;
	char *pBodyVarName = NULL;
	char *pBodyVarValue = NULL;
	char *pBuildID = NULL;
	char *pEmailFileName = NULL;

	bool bHTML = BuildScripting_DEFINED_AND_TRUE(pContext, "AUTOEMAIL_HTML");

	if (!BuildScripting_IsRootContext(pContext))
	{
		return;
	}

	estrPrintf(&pRecipientsVarName, "$AUTOEMAIL_%s_RECIPIENTS$", pTag);
	estrPrintf(&pSubjectLineVarName, "$AUTOEMAIL_%s_SUBJECTLINE$", pTag);
	estrPrintf(&pBodyVarName, "$AUTOEMAIL_%s_BODY$", pTag);

	if (BuildScripting_VariableExists(pContext, pRecipientsVarName) && BuildScripting_VariableExists(pContext, pSubjectLineVarName) && BuildScripting_VariableExists(pContext, pBodyVarName))
	{
		char **ppWhoToSendTo = NULL;
		FILE *pOutFile;
		static char *pFileNameOnDisk = NULL;


		BuildScripting_FindVarValue(pContext, pRecipientsVarName, &pRecipientsVarValue);
		BuildScripting_FindVarValue(pContext, pSubjectLineVarName, &pSubjectLineVarValue);
		BuildScripting_FindVarValue(pContext, pBodyVarName, &pBodyVarValue);

		DivideString(pRecipientsVarValue, ";", &ppWhoToSendTo, DIVIDESTRING_POSTPROCESS_ALLOCADD | DIVIDESTRING_POSTPROCESS_REMOVEUNIQUE);
			
		pFileNameOnDisk = BuildScriptingGetLogFileLocation(pContext, bHTML ? "autoemail.html" : "autoEmail.txt");
		
		pOutFile = fopen(pFileNameOnDisk, "wb");

		if (!pOutFile)
		{
			Errorf("Couldn't open %s for writing", pFileNameOnDisk);
		}
		else
		{
			char *pErrorsString = NULL;
			BuildScriptingAppendErrorsToEstring(&pErrorsString, bHTML);

			if (estrLength(&pErrorsString))
			{
				estrConcatf(&pBodyVarValue, "%s", pErrorsString);
			}
			estrDestroy(&pErrorsString);

			fprintf(pOutFile, "%s", pBodyVarValue);
			fclose(pOutFile);

			//if the script says to send emails, send them even if gbNoEmails is set
			BuildScripting_SendEmail(bHTML, &ppWhoToSendTo, pFileNameOnDisk, pSubjectLineVarValue);
		}

		eaDestroy(&ppWhoToSendTo);
	}
	else
	{
		printf("one of %s, %s, %s undefined, not sending email", pRecipientsVarName, pSubjectLineVarName, pBodyVarName);
	}

	estrDestroy(&pRecipientsVarName);
	estrDestroy(&pRecipientsVarValue);
	estrDestroy(&pSubjectLineVarName);
	estrDestroy(&pSubjectLineVarValue);
	estrDestroy(&pBodyVarName);
	estrDestroy(&pBodyVarValue);
	estrDestroy(&pBuildID);
	estrDestroy(&pEmailFileName);
}

void BuildScripting_DoFinalFailStuff(BuildScriptingContext *pContext)
{
	pContext->eScriptState = BUILDSCRIPTSTATE_FAILED;

	if (BuildScripting_IsRootContext(pContext))
	{
		BuildScriptingFailureExtraStuff(pContext, pContext->pFailureMessage, pContext->pFailureLinkString);

		BuildScripting_SendAutoEmail(pContext, "F");
		BuildScripting_DoAutoLogging(pContext, "F");

		BuildScripting_SaveVariablesForNextRun(pContext);
	}
		
	eaDestroyStruct(&pContext->ppChildren, parse_BuildScriptingContext);
	eaDestroyStruct(&pContext->ppDetachedChildren, parse_BuildScriptingContext);

	pContext->bFailing = false;
}


void BuildScripting_FailEx(BuildScriptingContext *pContext, bool bNoStep, char *pInErrorMessage, ...)
{
	char *pFullMessage = NULL;
	static char *pFullFailureString = NULL;
	static char *pStepString = NULL;
	char *pNameOfUninterruptibleChild;

	if (pContext->bFailing)
	{
		return;
	}

	if (bNoStep)
	{
		estrPrintf(&pStepString, "during script startup (or some other time with no current step)");
	}
	else
	{
		estrPrintf(&pStepString, "during step %s(%s)", pContext->pCurStepString,  pContext->pCurDisplayString);
	}

	pContext->bFailing = true;

	VA_START(args, pInErrorMessage);
	estrConcatfv(&pFullMessage, pInErrorMessage, args);
	VA_END();


	estrPrintf(&pContext->pFailureMessage, "Build Scripting failed with message (%s) %s",
		pFullMessage, pStepString);

	bsLogf(pContext, true, "%s", pContext->pFailureMessage);

	estrPrintf(&pFullFailureString, "Build Scripting failed with message (%s) %s",
		pFullMessage, pStepString);

	BuildScripting_AddVariable(pContext, "$FAILURESTRING$", pFullFailureString, "BUILTIN");

	estrDestroy(&pFullMessage);



	if (BuildScripting_CantBeInterrupted_Recurse(pContext, &pNameOfUninterruptibleChild))
	{
		bsLogf(pContext, true, "Context has failed... BUT we must wait for uninterruptible child %s (and possibly others) to complete", pNameOfUninterruptibleChild);
		pContext->bInSpecialFailingModeWaitingForUninterruptibleChildContexts = true;
	}
	else
	{
		BuildScripting_DoFinalFailStuff(pContext);

	}

	if (pContext->pParent)
	{
		BuildScripting_EndContext(pContext->pContextName, false);
	}
}



void BuildScripting_FailWithStep(BuildScriptingContext *pContext, char *pInErrorMessage)
{

	BuildScripting_FailEx(pContext, false, "%s", pInErrorMessage);

}

//needs to be a separate function because we pass a func ptr to the instringCommands stuff
void BuildScripting_FailWithStep_ForInString(char *pInErrorMessage, BuildScriptingContext *pContext)
{

	BuildScripting_FailEx(pContext, false, "%s", pInErrorMessage);

}

void BuildScripting_FailWithStepf(BuildScriptingContext *pContext, char *pInErrorMessage, ...)
{
	char *pFullStr = NULL;
	estrGetVarArgs(&pFullStr, pInErrorMessage);
	BuildScripting_FailWithStep(pContext, pFullStr);
	estrDestroy(&pFullStr);
}

void BuildScripting_Abort(BuildScriptingContext *pContext, char *pAbortMessage)
{

	estrPrintf(&pContext->pFailureMessage, "Build Scripting aborted with message (%s)",
		pAbortMessage);

	bsLogf(pContext, true, "%s", pContext->pFailureMessage);

	pContext->eScriptState = BUILDSCRIPTSTATE_ABORTED;

	eaDestroyStruct(&pContext->ppChildren, parse_BuildScriptingContext);
	eaDestroyStruct(&pContext->ppDetachedChildren, parse_BuildScriptingContext);
}



void BuildScripting_Error(BuildScriptingContext *pContext, char *pErrorMessage)
{
	pContext->bThereWereErrors = true;

	bsLogf(pContext, true, "Build Scripting encountered error (%s)", pErrorMessage);

	BuildScripting_ExtraErrorProcessing(pContext, pErrorMessage);
}

static bool isalnumOrUnder(char c)
{
	return isalnum(c) || c == '_';
}


//looks for all occurrences of CHECK(varname) or CHECK($varname$). Then looks up that variable. If it isn't defined,
//or is "0" or is all empty, replaces the entire thing with "0". Otherwise replaces it with "1".
//
//returns true if syntax was all correct
bool FindAndReplaceCheckMacro(BuildScriptingContext *pContext, char **ppOutString)
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

			for (i=0; i < eaSize(&pContext->variables.ppScriptingVariables); i++)
			{
				if (stricmp(pContext->variables.ppScriptingVariables[i]->pVarName, pVarName) == 0)
				{
					if (strcmp(pContext->variables.ppScriptingVariables[i]->pVarValue, "0") == 0 || StringIsAllWhiteSpace(pContext->variables.ppScriptingVariables[i]->pVarValue))
					{
					}
					else
					{
						//here we may have to do more variable replacing on the value we got out, as it might contain something that resolves to empty or 0
						char *pTempValueString;
						estrStackCreate(&pTempValueString);
						BuildScripting_DoVariableReplacingInStrings(pContext, pContext->variables.ppScriptingVariables[i]->pVarValue, &pTempValueString);

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

bool FindAndReplaceCheckMacro_ForInString(char **ppOutString, BuildScriptingContext *pContext)
{
	return FindAndReplaceCheckMacro(pContext, ppOutString);
}

//goes through an ESTRING and finds occurrences of $?. If it finds one, checks if it's of the form
//$?VARNAME$. If so, then checks if that variable exists. If it does, replace it. If it doesn't, 
//replace the whole thing with nothingness.
bool FindAndReplaceQuestionMarkVariables(BuildScriptingContext *pContext, char **ppDestString)
{
	int iCurOffset = 0;
	bool bDidSomething = false;
	int i;

	while (1)
	{

		char *pNextBeginning;
		char *pTemp;
		bool bReplaced;
	
BeginningOfLoop:

		pNextBeginning = strstri((*ppDestString) + iCurOffset, "$?");
		bReplaced = false;

		if (!pNextBeginning)
		{
			return bDidSomething;
		}

		pTemp = pNextBeginning + 2;
		
		if (*pTemp == '$')
		{
			iCurOffset = (pNextBeginning - *ppDestString) + 1;
			continue;
		}

		while (*pTemp && *pTemp != '$')
		{
			if (!ISOKFORVARNAME(*pTemp))
			{
				iCurOffset = (pNextBeginning - *ppDestString) + 1;

				//really want to "continue" in the outer loop but can't continue out of the inner loop
				goto BeginningOfLoop;
			}

			pTemp++;
		}

		if (!*pTemp)
		{
			return bDidSomething;
		}

		//at this point, pNextBeginning points to a $? and pTemp points to the final $
		for (i=0; i < eaSize(&pContext->variables.ppScriptingVariables); i++)
		{
			if (strStartsWith(pNextBeginning + 2, pContext->variables.ppScriptingVariables[i]->pVarName + 1))
			{
				int iStartingOffset = pNextBeginning - *ppDestString;
				estrRemove(ppDestString, iStartingOffset, pTemp - pNextBeginning + 1);
				estrInsertf(ppDestString, iStartingOffset, "%s", pContext->variables.ppScriptingVariables[i]->pVarValue);

				bReplaced = true;
				bDidSomething = true;
				break;
			}
		}

		if (!bReplaced)
		{
			estrRemove(ppDestString, pNextBeginning - *ppDestString, pTemp - pNextBeginning + 1);
			bDidSomething = true;
		}
	}

	return bDidSomething;
}








bool BuildScripting_DoVariableReplacingInStrings(BuildScriptingContext *pContext, char *pSourceString, char **ppDestEString)
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




	while (bSomethingChanged)
	{
		if (!FindAndReplaceCheckMacro(pContext, ppDestEString))
		{
			return false;
		}

		do
		{
			bSomethingChanged = false;
			for (i=0; i < eaSize(&pContext->variables.ppScriptingVariables); i++)
			{
				if (estrReplaceOccurrences_CaseInsensitive(ppDestEString, pContext->variables.ppScriptingVariables[i]->pVarName, 
					pContext->variables.ppScriptingVariables[i]->pVarValue))
				{
					bSomethingChanged = true;
				}
			}
		}
		while (bSomethingChanged);

		if (FindAndReplaceQuestionMarkVariables(pContext, ppDestEString))
		{
			bSomethingChanged = true;
		}

		if (!FindAndReplaceCheckMacro(pContext, ppDestEString))
		{
			return false;
		}

		iResult = InStringCommands_Apply(ppDestEString, &sBuildScriptingInStringCommandCBs, pContext);

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


bool BuildScripting_DoVariableReplacing_Internal(BuildScriptingContext *pContext, BuildScriptCommand *pCommand)
{
	if (!BuildScripting_DoVariableReplacingInStrings(pContext, pCommand->pIfExpression_Raw, &pCommand->pIfExpression_Use))
	{
		return false;
	}
	
	if (pCommand->pIfExpression_Use && strchr(pCommand->pIfExpression_Use, '$'))
	{
		BuildScripting_Fail(pContext, "Found an extra $ in string \"%s\", presumed unknown variable", pCommand->pIfExpression_Use);
		return false;
	}

	//if the expression is false, we are skipping this command, so just copy instead of replacing
	if (pCommand->pIfExpression_Use)
	{
		if (!BuildScripting_IsExpressionStringTrue(pCommand->pIfExpression_Use))
		{
			estrCopy2(&pCommand->pScriptString_Use, pCommand->pScriptString_Raw);
			estrCopy2(&pCommand->pDisplayString_Use, pCommand->pDisplayString_Raw);
			estrCopy2(&pCommand->pWorkingDirectory_Use, pCommand->pWorkingDirectory_Raw);
			return true;
		}
	}



	if (!BuildScripting_DoVariableReplacingInStrings(pContext, pCommand->pScriptString_Raw, &pCommand->pScriptString_Use))
	{
		return false;
	}

	if (!BuildScripting_DoVariableReplacingInStrings(pContext, pCommand->pDisplayString_Raw, &pCommand->pDisplayString_Use))
	{
		return false;
	}

	if (!BuildScripting_DoVariableReplacingInStrings(pContext, pCommand->pWorkingDirectory_Raw, &pCommand->pWorkingDirectory_Use))
	{
		return false;
	}

	if (!BuildScripting_DoVariableReplacingInStrings(pContext, pCommand->pFailureExpression_Raw, &pCommand->pFailureExpression_Use))
	{
		return false;
	}

	if (pCommand->pDisplayString_Use && strchr(pCommand->pDisplayString_Use, '$'))
	{
		BuildScripting_Fail(pContext, "Found an extra $ in string \"%s\", presumed unknown variable", pCommand->pDisplayString_Use);
		return false;
	}

	if (pCommand->pScriptString_Use && strchr(pCommand->pScriptString_Use, '$'))
	{
		BuildScripting_Fail(pContext, "Found an extra $ in string \"%s\", presumed unknown variable", pCommand->pScriptString_Use);
		return false;
	}

	if (pCommand->pWorkingDirectory_Use && strchr(pCommand->pWorkingDirectory_Use, '$'))
	{
		BuildScripting_Fail(pContext, "Found an extra $ in string \"%s\", presumed unknown variable", pCommand->pWorkingDirectory_Use);
		return false;
	}
	
	if (!BuildScripting_DoVariableReplacingInStrings(pContext, pCommand->pOutputFile_Raw, &pCommand->pOutputFile_Use))
	{
		return false;
	}
	if (pCommand->pOutputFile_Use && strchr(pCommand->pOutputFile_Use, '$'))
	{
		BuildScripting_Fail(pContext, "Found an extra $ in string \"%s\", presumed unknown variable", pCommand->pOutputFile_Use);
		return false;
	}

	if (!BuildScripting_DoVariableReplacingInStrings(pContext, pCommand->pAppendFile_Raw, &pCommand->pAppendFile_Use))
	{
		return false;
	}
	if (pCommand->pAppendFile_Use && strchr(pCommand->pAppendFile_Use, '$'))
	{
		BuildScripting_Fail(pContext, "Found an extra $ in string \"%s\", presumed unknown variable", pCommand->pAppendFile_Use);
		return false;
	}

	
	

	return true;
}


bool BuildScripting_DoVariableReplacing(BuildScriptingContext *pContext, BuildScriptCommand *pCommand)
{
	bool bRetVal;
	BuildScripting_PushCurExecutingContext(pContext);
	bRetVal = BuildScripting_DoVariableReplacing_Internal(pContext, pCommand);
	BuildScripting_PopCurExecutingContext();
	return bRetVal;
}


void BuildScripting_GetCommandDescription(char **ppOutString, BuildScriptCommand *pCurCommand)
{
	switch (pCurCommand->eCommand)
	{
	case BUILDSCRIPTCOMMAND_SETVARIABLE:
	case BUILDSCRIPTCOMMAND_SETVARIABLESFROMFILE:
	case BUILDSCRIPTCOMMAND_SET_NO_EXPAND:
	case BUILDSCRIPTCOMMAND_SYSTEM:
	case BUILDSCRIPTCOMMAND_INCLUDE:
	case BUILDSCRIPTCOMMAND_REQUIREVARIABLES:
	case BUILDSCRIPTCOMMAND_SENDEMAIL:
	case BUILDSCRIPTCOMMAND_ABORT_IF_EXPRESSION_TRUE:
	case BUILDSCRIPTCOMMAND_WAIT_UNTIL_EXPRESSION_TRUE:
	case BUILDSCRIPTCOMMAND_FAIL_IF_EXPRESSION_TRUE:
	case BUILDSCRIPTCOMMAND_FAIL_IF_STRING_NONEMPTY:
	case BUILDSCRIPTCOMMAND_EXECUTE_COMMAND:
	case BUILDSCRIPTCOMMAND_IF:
	case BUILDSCRIPTCOMMAND_COMMENT:
	case BUILDSCRIPTCOMMAND_ABORT:
	case BUILDSCRIPTCOMMAND_FAIL:
	case BUILDSCRIPTCOMMAND_SETMULTIPLEVARS: 
	case BUILDSCRIPTCOMMAND_EXPORT_VARS_TO_PARENT:
	case BUILDSCRIPTCOMMAND_EXPORT_VARS_TO_ROOT:
	case BUILDSCRIPTCOMMAND_BEGIN_CHILD:
	case BUILDSCRIPTCOMMAND_BEGIN_DETACHED_CHILD:
	case BUILDSCRIPTCOMMAND_SUBDIVIDE_STRING_INTO_VARS:
	case BUILDSCRIPTCOMMAND_GOTO:
	case BUILDSCRIPTCOMMAND_IMPORT_VARS_FROM_PARENT:
	case BUILDSCRIPTCOMMAND_IMPORT_VARS_FROM_ROOT:
		estrPrintf(ppOutString, "%s (%s)",
			StaticDefineIntRevLookup(enumBuildScriptCommandEnum, pCurCommand->eCommand),
			pCurCommand->pScriptString_Use);
		break;

	case BUILDSCRIPTCOMMAND_WAIT_FOR_DETACHED_CHILDREN:
		estrPrintf(ppOutString, "%s (%s)",
			StaticDefineIntRevLookup(enumBuildScriptCommandEnum, pCurCommand->eCommand),
			estrLength(&pCurCommand->pScriptString_Use) ? pCurCommand->pScriptString_Use : "ALL");
		break;

	case BUILDSCRIPTCOMMAND_FOR:
		{
			char *pTempString = NULL;
			estrCopy2(&pTempString, pCurCommand->pScriptString_Use);
			if (estrLength(&pTempString) > 256)
			{
				estrSetSize(&pTempString, 256);
				estrConcatf(&pTempString, "...");
		
				estrPrintf(ppOutString, "%s (%s)",
					StaticDefineIntRevLookup(enumBuildScriptCommandEnum, pCurCommand->eCommand),
					pTempString);
			}
			estrDestroy(&pTempString);
		}
		break;


	case BUILDSCRIPTCOMMAND_ENDFOR:
	case BUILDSCRIPTCOMMAND_WAIT:
	case BUILDSCRIPTCOMMAND_ENDIF:
	case BUILDSCRIPTCOMMAND_ELSE:
	case BUILDSCRIPTCOMMAND_END_CHILDREN:
	case BUILDSCRIPTCOMMAND_BEGIN_CHILDREN:
	case BUILDSCRIPTCOMMAND_END_CHILD:
	case BUILDSCRIPTCOMMAND_END_DETACHED_CHILD:
		estrPrintf(ppOutString, "%s",
			StaticDefineIntRevLookup(enumBuildScriptCommandEnum, pCurCommand->eCommand));
		break;
	}
}

void BuildScripting_UpdateStrings(BuildScriptingContext *pContext)
{
	BuildScriptCommand *pCurCommand;

	assert(pContext->commandList.ppCommands);
	pCurCommand = pContext->commandList.ppCommands[pContext->iCurCommandNum];

	if (pCurCommand->pDisplayString_Use)
	{
		estrCopy2(&pContext->pCurDisplayString, pCurCommand->pDisplayString_Use);
	}

	BuildScripting_GetCommandDescription(&pContext->pCurStepString, pCurCommand);

	
}

char *BuildScripting_GetCurStateString(BuildScriptingContext *pContext)
{
	static char *spRetVal = 0;

	estrPrintf(&spRetVal, "%s (%s)", pContext->pCurStepString, pContext->pCurDisplayString);

	return spRetVal;
}

void AssertCommandCanHaveExpressions(BuildScriptCommand *pCommand)
{
	if (pCommand->eCommand == BUILDSCRIPTCOMMAND_SETVARIABLE
		|| pCommand->eCommand == BUILDSCRIPTCOMMAND_SET_NO_EXPAND
		|| pCommand->eCommand == BUILDSCRIPTCOMMAND_SETVARIABLESFROMFILE
		|| pCommand->eCommand == BUILDSCRIPTCOMMAND_REQUIREVARIABLES
		|| pCommand->eCommand == BUILDSCRIPTCOMMAND_SYSTEM
		|| pCommand->eCommand == BUILDSCRIPTCOMMAND_EXECUTE_COMMAND
		|| pCommand->eCommand == BUILDSCRIPTCOMMAND_INCLUDE
		|| pCommand->eCommand == BUILDSCRIPTCOMMAND_WAIT
		|| pCommand->eCommand == BUILDSCRIPTCOMMAND_WAIT_UNTIL_EXPRESSION_TRUE
		|| pCommand->eCommand == BUILDSCRIPTCOMMAND_FOR
		|| pCommand->eCommand == BUILDSCRIPTCOMMAND_SETMULTIPLEVARS
		|| pCommand->eCommand == BUILDSCRIPTCOMMAND_SUBDIVIDE_STRING_INTO_VARS
		|| pCommand->eCommand == BUILDSCRIPTCOMMAND_GOTO)
	{
		return;
	}

	assertmsgf(0, "Command of type %s invalidly has an IfExpression", StaticDefineIntRevLookup(enumBuildScriptCommandEnum, pCommand->eCommand));
}


void BuildScripting_NewCommand(BuildScriptingContext *pContext, int iCommandNum)
{
	assert(pContext->commandList.ppCommands);

	BuildScripting_NewCommand_DoExtraStuff(pContext);

	pContext->bSkipCurrentStep = false;
	pContext->iBuildScriptingFailTime = 0;
	pContext->bFailAfterDelayCalled = false;

	if (iCommandNum < eaSize(&pContext->commandList.ppCommands) && pContext->commandList.ppCommands[iCommandNum]->bBreak)
	{
		int iBreak = 0;
	}
		
	BuildScriptingNewStepExtraStuff();

	if (iCommandNum >= eaSize(&pContext->commandList.ppCommands))
	{
		if (BuildScripting_IsRootContext(pContext))
		{
			BuildScripting_SaveVariablesForNextRun(pContext);
			BuildScripting_SendAutoEmail(pContext, "S");
			BuildScripting_DoAutoLogging(pContext, "S");			
		}
		else
		{
			BuildScripting_EndContext(pContext->pContextName, false);
		}


		if (pContext->bThereWereErrors)
		{
			if (pContext->pParent)
			{
				bsLogf(pContext, true, "Child context %s is complete but there were some errors...", pContext->pContextName);
			}
			else
			{
				bsLogf(pContext, true, "Build scripting is complete but there were some errors...");
			}

			pContext->eScriptState = BUILDSCRIPTSTATE_SUCCEEDED_WITH_ERRORS;
		}
		else
		{
			if (pContext->pParent)
			{
				bsLogf(pContext, true, "Child context %s is complete with no errors...", pContext->pContextName);
			}
			else
			{
				bsLogf(pContext, true, "Build scripting is complete with no errors...");
			}

			pContext->eScriptState = BUILDSCRIPTSTATE_SUCCEEDED;
		}

		eaDestroyStruct(&pContext->ppChildren, parse_BuildScriptingContext);
		eaDestroyStruct(&pContext->ppDetachedChildren, parse_BuildScriptingContext);


		return;
	}

	pContext->commandList.ppCommands[iCommandNum]->iRetryCount = 0;
	pContext->iCurCommandNum = iCommandNum;
	pContext->iFramesInState = -1;
	pContext->iTimeEnteredState = timeMsecsSince2000();

	if (!BuildScripting_DoVariableReplacing(pContext, pContext->commandList.ppCommands[iCommandNum]))
	{
		//something failed and has already called BuildScripting_Fail
		return;
	}

	BuildScripting_UpdateStrings(pContext);

	if (estrLength(&pContext->commandList.ppCommands[iCommandNum]->pIfExpression_Use))
	{
		AssertCommandCanHaveExpressions(pContext->commandList.ppCommands[iCommandNum]);

	


		if (!BuildScripting_IsExpressionStringTrue(pContext->commandList.ppCommands[iCommandNum]->pIfExpression_Use))
		{
			bsLogf(pContext, false, "Expression (%s) was false, so we're skipping a build scripting command", pContext->commandList.ppCommands[iCommandNum]->pIfExpression_Use);

			if (pContext->commandList.ppCommands[iCommandNum]->eCommand == BUILDSCRIPTCOMMAND_INCLUDE)
			{
				//need to use scriptint+1 to skip all the included steps, plus the INCLUDE step itself
				bsLogf(pContext, false, "Skipping an INCLUDE command... need to skip forward %d steps", pContext->commandList.ppCommands[iCommandNum]->iScriptInt + 1);
				BuildScripting_NewCommand(pContext, iCommandNum + pContext->commandList.ppCommands[iCommandNum]->iScriptInt + 1);
				return;
			}
			else if (pContext->commandList.ppCommands[iCommandNum]->eCommand == BUILDSCRIPTCOMMAND_FOR)
			{
				int i;

				for (i=iCommandNum+1; i < eaSize(&pContext->commandList.ppCommands); i++)
				{
					int iTemp;

					if (pContext->commandList.ppCommands[i]->eCommand == BUILDSCRIPTCOMMAND_ENDFOR 
						&& BuildScripting_FindMatchingFOR(pContext, i, &iTemp) == pContext->commandList.ppCommands[iCommandNum])
					{
						BuildScripting_NewCommand(pContext, i+1);
						return;
					}
				}

				BuildScripting_Fail(pContext, "Couldn't find matching ENDFOR when trying to skip a FOR loop due to an if expression");
			}
			else
			{

				BuildScripting_NewCommand(pContext, ++iCommandNum);
			}
		}
		else
		{
			bsLogf(pContext, false, "Expression (%s) was true, so we have new BuildScripting command %s (%s(%d))", 
				pContext->commandList.ppCommands[iCommandNum]->pIfExpression_Use,
				BuildScripting_GetCurStateString(pContext),
				pContext->commandList.ppCommands[iCommandNum]->pFileName,
				pContext->commandList.ppCommands[iCommandNum]->iLineNum);

		}
	}
	else
	{
		if (pContext->commandList.ppCommands[iCommandNum]->iLineNum == 22)
		{
			int iBrk = 0;
		}

		bsLogf(pContext, false, "New BuildScripting command %s (%s(%d))", BuildScripting_GetCurStateString(pContext),
			pContext->commandList.ppCommands[iCommandNum]->pFileName,
			pContext->commandList.ppCommands[iCommandNum]->iLineNum);
	}

	if (iCommandNum < eaSize(&pContext->commandList.ppCommands))
	{
		if (pContext->commandList.ppCommands[iCommandNum]->pDisplayString_Use)
		{
			BuildScriptingAddComment(STACK_SPRINTF("Script: %s", pContext->commandList.ppCommands[iCommandNum]->pDisplayString_Use));
		}
	}

}


//returns how many steps are in the FOR command (must be at least one)
int BuildScripting_GetMaxFORCount(BuildScriptCommand *pCommand)
{
	int iCommaCount = 0;
	char *pNextComma = pCommand->pScriptString_Use;

	char separator = strchr(pCommand->pScriptString_Use, '\n') ? '\n' : ',';

	

	assert(pCommand->eCommand == BUILDSCRIPTCOMMAND_FOR);

	while ((pNextComma = strchr(pNextComma, separator)))
	{
		//ignore a single trailing comma or newline		
		if (StringIsAllWhiteSpace(pNextComma+1))
		{
			return iCommaCount+1;
		}

		iCommaCount++;
		pNextComma++;		
	}

	return iCommaCount+1;
}

//returns true on proper finding, false if there was an error
bool BuildScripting_FindMatching_ELSE_ENDIF(BuildScriptingContext *pContext, int iIfIndex, int *piElseIndex, int *piEndifIndex, char **ppErrorString)
{
	int i = iIfIndex + 1;
	int iDepth = 0;

	assert(pContext->commandList.ppCommands);
	*piElseIndex = *piEndifIndex = -1;


	while (i < eaSize(&pContext->commandList.ppCommands))
	{
		if (pContext->commandList.ppCommands[i]->eCommand == BUILDSCRIPTCOMMAND_ELSE)
		{
			if (iDepth == 0)
			{
				if (*piElseIndex != -1)
				{
					estrPrintf(ppErrorString, "IF command at %s(%d) has two ELSE commands: %s(%d) and %s(%d)",
						pContext->commandList.ppCommands[iIfIndex]->pFileName, 
						pContext->commandList.ppCommands[iIfIndex]->iLineNum,
						pContext->commandList.ppCommands[*piElseIndex]->pFileName, 
						pContext->commandList.ppCommands[*piElseIndex]->iLineNum,
						pContext->commandList.ppCommands[i]->pFileName, 
						pContext->commandList.ppCommands[i]->iLineNum);
					return false;
				}

				*piElseIndex = i;
			}
		}


		if (pContext->commandList.ppCommands[i]->eCommand == BUILDSCRIPTCOMMAND_ENDIF)
		{
			if (iDepth == 0)
			{
				*piEndifIndex = i;
				return true;
			}

			iDepth--;
		}
		else if (pContext->commandList.ppCommands[i]->eCommand == BUILDSCRIPTCOMMAND_IF)
		{
			iDepth++;
		}

		i++;
	}

	estrPrintf(ppErrorString, "IF command at %s(%d) has no matching ENDIF",
		pContext->commandList.ppCommands[iIfIndex]->pFileName, 
		pContext->commandList.ppCommands[iIfIndex]->iLineNum);
	return false;
}


BuildScriptCommand *BuildScripting_FindMatchingFOR(BuildScriptingContext *pContext, int iEndForIndex, int *piFoundIndex)
{
	int i = iEndForIndex - 1;
	int iDepth = 0;
	assert(pContext->commandList.ppCommands[iEndForIndex]->eCommand == BUILDSCRIPTCOMMAND_ENDFOR);

	while (i>=0)
	{
		switch (pContext->commandList.ppCommands[i]->eCommand)
		{
		case BUILDSCRIPTCOMMAND_FOR:
			if (iDepth == 0)
			{
				*piFoundIndex = i;
				return pContext->commandList.ppCommands[i];
			}
			iDepth--;
			break;
		case BUILDSCRIPTCOMMAND_ENDFOR:
			iDepth++;
			break;
		}

		i--;
	}

	return NULL;
}

void SetFileNameForCommandList(BuildScriptCommandList *pList, char *pOrigName)
{
	const char *pName = allocAddString(pOrigName);

	int i;

	for (i=0; i < eaSize(&pList->ppCommands); i++)
	{
		pList->ppCommands[i]->pFileName = pName;
	}
}
AUTO_FIXUPFUNC;
TextParserResult BuildScriptingContextFixup(BuildScriptingContext *pContext, enumTextParserFixupType eType, void *pExtraData)
{
	switch (eType)
	{
	case FIXUPTYPE_DESTRUCTOR:
		if (pContext->pBuildScriptingProcHandle)
		{
			KillQueryableProcess(&pContext->pBuildScriptingProcHandle);
		}

	}

	return PARSERESULT_SUCCESS;
}

AUTO_FIXUPFUNC;
TextParserResult BuildScriptCommandFixup(BuildScriptCommand *pCommand, enumTextParserFixupType eType, void *pExtraData)
{
	int iSize;

	switch (eType)
	{
	case FIXUPTYPE_POST_TEXT_READ:

//		pCommand->iIncludeDepth = pContext->iInternalIncludeDepth;

		iSize = eaSize(&pCommand->ppInternalStrs);

		//if there's only one string, it must be our scriptString
		if (iSize== 1)
		{
			pCommand->pScriptString_Raw = pCommand->ppInternalStrs[0];
			pCommand->ppInternalStrs[0] = NULL;
		}
		//if multiple then the last one is script string, the first n-1 should, if glommed together, be (foo x bar y)
		//where foo and bar are fields in BuildScriptCommand
		else if (iSize)
		{
			char *pPairString = NULL;
			char **ppWords = NULL;
			int i;

			pCommand->pScriptString_Raw = eaPop(&pCommand->ppInternalStrs);
			estrConcatSeparatedStringEarray(&pPairString, &pCommand->ppInternalStrs, " ");

			if (!(strStartsWith(pPairString, "(") && strEndsWith(pPairString, ")")))
			{
				assertmsgf(0, "Invalid argument syntax %s(%d)", pCommand->pFileName_internal, pCommand->iLineNum);
			}

			estrRemove(&pPairString, estrLength(&pPairString) - 1, 1);
			estrRemove(&pPairString, 0, 1);

			DivideString(pPairString, " ", &ppWords, DIVIDESTRING_POSTPROCESS_DONT_PUSH_EMPTY_STRINGS | DIVIDESTRING_POSTPROCESS_STRIP_WHITESPACE);

			estrDestroy(&pPairString);

			if (!eaSize(&ppWords) || (eaSize(&ppWords) & 1) == 1)
			{
				assertmsgf(0, "Invalid argument syntax %s(%d)", pCommand->pFileName_internal, pCommand->iLineNum);
			}

			for (i=0; i < eaSize(&ppWords); i+= 2)
			{
				static char *pPath = NULL;
				estrPrintf(&pPath, ".%s", ppWords[i]);
				if (!objPathSetString(pPath, parse_BuildScriptCommand, pCommand, ppWords[i+1]))
				{
					assertmsgf(0, "Invalid arguments... can't set field \"%s\" to value \"%s\"",
						ppWords[i], ppWords[i+1]);
				}
			}
		}

		if (pCommand->bCompileErrors)
		{
			estrCopy2(&pCommand->pExtendedErrorType, "COMPILE");
		}

		
	}
	return PARSERESULT_SUCCESS;
}


void BuildScripting_BeginEx(BuildScriptingContext *pContext, char *pScriptFileName, int iStartingIncludeDepth, char **ppDefaultDirectories)
{
	int i,j;
	bool bNeedToDoMoreIncluding = true;
	int iIncludeCount = 0;
	char fixedUpFileName[MAX_PATH];
	char *pErrorString = NULL;

	pContext->iBuildScriptingFailTime = 0;
	pContext->bFailAfterDelayCalled = false;
	pContext->iCurForDepth = 0;
	pContext->bThereWereErrors = false;

	pContext->iInternalIncludeDepth = iStartingIncludeDepth;

	if (!pScriptFileName || !pScriptFileName[0])
	{
		pContext->eScriptState = BUILDSCRIPTSTATE_SUCCEEDED;
		return;
	}

	
	BuildScripting_ClearAndResetVariables(pContext);


	for (i=0; i <eaSize(&ppDefaultDirectories); i++)
	{
		char temp[CRYPTIC_MAX_PATH];
		strcpy(temp, ppDefaultDirectories[i]);
		backSlashes(temp);
		if (!strEndsWith(temp, "\\"))
		{
			strcat(temp, "\\");
		}

		eaPush(&pContext->ppDefaultDirectories, strdup(temp));
	}


	bsLogf(pContext, true, "Build Scripting beginning with file %s", pScriptFileName);


	pContext->eScriptState = BUILDSCRIPTSTATE_LOADING;

	estrPrintf(&pContext->pCurDisplayString, "Scripting starting up");

	if (!BuildScripting_DoAuxStuffBeforeLoadingScriptFiles(&pErrorString))
	{
		BuildScripting_FailEx(pContext, true, pErrorString);
		estrDestroy(&pErrorString);
	}


	if (!BuildScripting_FindFile(pContext, pScriptFileName, fixedUpFileName))
	{
		BuildScripting_FailEx(pContext, true, "Couldn't find file %s", pScriptFileName);
		return;
	}





	StructDeInit(parse_BuildScriptCommandList, &pContext->commandList);

	pContext->iInternalIncludeDepth = iStartingIncludeDepth;

	printfColor(COLOR_RED | COLOR_BLUE | COLOR_BRIGHT, "About to load main script from %s\n", fixedUpFileName);
	if (!ParserReadTextFile(fixedUpFileName, parse_BuildScriptCommandList, &pContext->commandList, PARSER_NOINCLUDES))
	{
		BuildScripting_FailEx(pContext, true, "Couldn't load build script from file %s", pScriptFileName);
		StructDeInit(parse_BuildScriptCommandList, &pContext->commandList);
		return;
	}

	SetFileNameForCommandList(&pContext->commandList, fixedUpFileName);

	for (i=0; i < eaSize(&pContext->commandList.ppCommands); i++)
	{
		pContext->commandList.ppCommands[i]->iIncludeDepth = pContext->iInternalIncludeDepth;
	}

	for (i=0; i < eaSize(&pContext->commandList.ppCommands); i++)
	{

		if (pContext->commandList.ppCommands[i]->eCommand == BUILDSCRIPTCOMMAND_INCLUDE)
		{
			BuildScriptCommandList gIncludedList = {0};
			BuildScripting_DoVariableReplacing(pContext, pContext->commandList.ppCommands[i]);
	
			pContext->iInternalIncludeDepth = pContext->commandList.ppCommands[i]->iIncludeDepth + 1;

			iIncludeCount++;

			assertmsg(iIncludeCount < 256, "More than 256 includes... probable include recursion");

			if (!BuildScripting_FindFile(pContext, pContext->commandList.ppCommands[i]->pScriptString_Use, fixedUpFileName))
			{
				BuildScripting_FailEx(pContext, true, "Couldn't find file %s", pContext->commandList.ppCommands[i]->pScriptString_Use);
				return;
			}

			if (eaSize(&pContext->commandList.ppCommands[i]->ppSimpleMacros))
			{
				char fileNameNoDir[CRYPTIC_MAX_PATH];
				char macroFileName[CRYPTIC_MAX_PATH];
				int iBufSize;
				char *pBuf = fileAlloc(fixedUpFileName, &iBufSize);
				char *pEstrBuf = NULL;
				FILE *pOutFile;

				if (!pBuf)
				{
					BuildScripting_FailEx(pContext, true, "Couldn't load %s", fixedUpFileName);
					return;
				}

				estrInsert(&pEstrBuf, 0, pBuf, iBufSize);
				for (j=0; j < eaSize(&pContext->commandList.ppCommands[i]->ppSimpleMacros); j++)
				{
					estrReplaceOccurrences(&pEstrBuf, pContext->commandList.ppCommands[i]->ppSimpleMacros[j]->pFrom, pContext->commandList.ppCommands[i]->ppSimpleMacros[j]->pTo);
				}

				getFileNameNoDir(fileNameNoDir, pContext->commandList.ppCommands[i]->pScriptString_Use);
				sprintf(macroFileName, "c:\\temp\\__macro__%s", fileNameNoDir);

				mkdirtree_const(macroFileName);

				pOutFile = fopen(macroFileName, "wt");

				if (!pOutFile)
				{
					estrDestroy(&pEstrBuf);
					BuildScripting_FailEx(pContext, true, "Couldn't create %s", macroFileName);
					return;
				}

				fprintf(pOutFile, "%s", pEstrBuf);
				fclose(pOutFile);
				estrDestroy(&pEstrBuf);

				printfColor(COLOR_RED | COLOR_BLUE | COLOR_BRIGHT, "About to load included script from %s\n", macroFileName);

				if (!ParserReadTextFile(macroFileName, parse_BuildScriptCommandList, &gIncludedList, PARSER_NOINCLUDES))
				{
					BuildScripting_FailEx(pContext, true, "parse error while including file %s", pContext->commandList.ppCommands[i]->pScriptString_Use);
					return;
				}
			}
			else
			{
				printfColor(COLOR_RED | COLOR_BLUE | COLOR_BRIGHT, "About to load included script from %s\n", fixedUpFileName);
				if (!ParserReadTextFile(fixedUpFileName, parse_BuildScriptCommandList, &gIncludedList, PARSER_NOINCLUDES))
				{
					BuildScripting_FailEx(pContext, true, "parse error while including file %s", pContext->commandList.ppCommands[i]->pScriptString_Use);
					return;
				}
			}

			for (j=0; j < eaSize(&gIncludedList.ppCommands); j++)
			{
				gIncludedList.ppCommands[j]->iIncludeDepth = pContext->iInternalIncludeDepth;
			}

			SetFileNameForCommandList(&gIncludedList, fixedUpFileName);

			//we're about to insert more commands into the list. Fix up all #include commands including our current spot.


			for (j=0; j < i; j++)
			{
				if (pContext->commandList.ppCommands[j]->eCommand == BUILDSCRIPTCOMMAND_INCLUDE 
					&& j + pContext->commandList.ppCommands[j]->iScriptInt >= i)
				{
					pContext->commandList.ppCommands[j]->iScriptInt += eaSize(&gIncludedList.ppCommands);
				}
			}

			pContext->commandList.ppCommands[i]->iScriptInt = eaSize(&gIncludedList.ppCommands);

			for (j=eaSize(&gIncludedList.ppCommands) - 1; j >= 0; j--)
			{
				eaInsert(&pContext->commandList.ppCommands, gIncludedList.ppCommands[j], i+1);
			}

			eaDestroy(&gIncludedList.ppCommands);
			StructDeInit(parse_BuildScriptCommandList, &gIncludedList);


		}
	}

	//always put a BUILDSCRIPTCOMMAND_WAIT_FOR_DETACHED_CHILDREN at the end of every command list
	BuildScripting_TerminateCommandList(&pContext->commandList);

	BuildScripting_NewCommand(pContext, 0);

	pContext->iFramesInState++;

	pContext->eScriptState = BUILDSCRIPTSTATE_RUNNING;
}

void BuildScripting_Begin(BuildScriptingContext *pContext, char *pScriptFileName, int iStartingIncludeDepth, char **ppDefaultDirectories)
{
	BuildScripting_PushCurExecutingContext(pContext);
	BuildScripting_BeginEx(pContext, pScriptFileName, iStartingIncludeDepth, ppDefaultDirectories);
	BuildScripting_PopCurExecutingContext();
}


bool BuildScripting_IsOkForVarName(char *pString)
{
	if (!*pString)
	{
		return false;
	}

	while (*pString)
	{
		if (!ISOKFORVARNAME(*pString))
		{
			return false;
		}

		pString++;
	}

	return true;
}

//returns false on parsing failure
//
//pppTokens is a cache into which the string can be divided to avoid having to do so every time through
//a for loop
bool BuildScripting_SetVarFromString(BuildScriptingContext *pContext, char *pString, int iIndex, bool bRaw, char ***pppTokens, char *pComment)
{
	char varName[MAX_VAR_NAME_LENGTH + 2];
	int iVarNameLength = 1;
	char *pFirstComma;
	char *pInString = pString;
	
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

	if (bRaw)
	{
		BuildScripting_AddVariable_Internal(pContext, varName, pString, pComment, true);
		return true;
	}
	
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
		if (strStartsWith(pString, "MATH("))
		{
			char *pCopy = estrDup(pString);
			int iOut;
			
			estrTrimLeadingAndTrailingWhitespace(&pCopy);
			
			if (!strEndsWith(pCopy, ")"))
			{
				estrDestroy(&pCopy);
				BuildScripting_Fail(pContext, "Unable to parse SET MATH command %s... format should be SET \"VARNAME = MATH(3+4)\"",
					pInString);
				return 0;
			}



			bsLogf(pContext, false, "About to try to evaluate math expression %s to set into variable %s",
				pCopy, varName);
			
			estrSetSize(&pCopy, estrLength(&pCopy) - 1);
			estrRemove(&pCopy, 0, 5);

			iOut = exprEvaluateRawString(pCopy);

			estrPrintf(&pCopy, "%d", iOut);

			BuildScripting_AddVariable(pContext, varName, pCopy, pComment);

			estrDestroy(&pCopy);

			

		}
		else
		{
			BuildScripting_AddVariable(pContext, varName, pString, pComment);
		}
		return true;
	}

	if (pppTokens)
	{
		if (eaSize(pppTokens) == 0)
		{
			char *pSeparatorString;

			if (strchr(pString, '\n'))
			{
				pSeparatorString = "\n";
			}
			else
			{
				pSeparatorString = ",";
			}

			DivideString(pString, pSeparatorString, pppTokens, DIVIDESTRING_POSTPROCESS_STRIP_WHITESPACE);
		}

		if (iIndex >= eaSize(pppTokens))
		{
			return false;
		}

		BuildScripting_AddVariable(pContext, varName, (*pppTokens)[iIndex], pComment);
		
		bsLogf(pContext, false, "In FOR loop, setting variable %s to value %s", varName, (*pppTokens)[iIndex]);
	}
	else
	{
		char separator;

		if (strchr(pString, '\n'))
		{
			separator = '\n';
		}
		else
		{
			separator = ',';
		}


		while (iIndex)
		{
			pFirstComma = strchr(pString, separator);
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

		pFirstComma = strchr(pString, separator);
		if (pFirstComma)
		{
			*pFirstComma = 0;
		}
		
		BuildScripting_AddVariable(pContext, varName, pString, pComment);

		bsLogf(pContext, false, "In FOR loop, setting variable %s to value %s", varName, pString);
						


		if (pFirstComma)
		{
			*pFirstComma = separator;
		}
	}

	return true;
}
bool BuildScripting_SetVariablesFromFile(BuildScriptingContext *pContext, char *pVariableFileName, bool bFailOnNoFile, char *pComment)
{
	char *pBuffer;
	int iFileSize;
	char *pReadHead;
	char fixedUpFileName[MAX_PATH];

	if (!BuildScripting_FindFile(pContext, pVariableFileName, fixedUpFileName))
	{
		if (!bFailOnNoFile)
		{
			BuildScripting_Fail(pContext, "Couldn't find file %s", pVariableFileName);
		}
		return false;
	}

	pBuffer = fileAlloc(fixedUpFileName, &iFileSize);

	if (!pBuffer)
	{
		if (!bFailOnNoFile)
		{
			BuildScripting_Fail(pContext, "Couldn't set variables from file %s", pVariableFileName);
		}
		return false;
	}

	pReadHead = pBuffer;

	while (1)
	{
		char *pFirstNewLine = strchr(pReadHead, '\n');

		if (pFirstNewLine)
		{
			*pFirstNewLine = 0;
		}

		while (IS_WHITESPACE(*pReadHead))
		{
			pReadHead++;
		}

		if (strStartsWith(pReadHead, "#include"))
		{
			char *pFileName = NULL;
			int iLength;
			char fullPath[MAX_PATH];
			static char *pIncludeComment = NULL;
			estrCopy2(&pFileName, pReadHead + 8);
			estrTrimLeadingAndTrailingWhitespace(&pFileName);

			iLength = estrLength(&pFileName);

			if (iLength < 3 || pFileName[0] != '"' || pFileName[iLength - 1] != '"')
			{
				BuildScripting_Fail(pContext, "Invalid #include while setting variables from file %s", pVariableFileName);
				estrDestroy(&pFileName);			
				return false;
			}

			estrSetSize(&pFileName, iLength - 1);
			estrRemove(&pFileName, 0, 1);


			if (!BuildScripting_FindFile(pContext, pFileName, fullPath))
			{
				BuildScripting_Fail(pContext, "Couldn't find #included file %s while setting variables from file", pFileName);
				estrDestroy(&pFileName);			
				return false;
			}
	
			estrDestroy(&pFileName);			

			estrPrintf(&pIncludeComment, "%s, then #including file %s", pComment, fullPath);

			if (!BuildScripting_SetVariablesFromFile(pContext, fullPath, bFailOnNoFile, pIncludeComment))
			{
				if (!bFailOnNoFile)
				{
					BuildScripting_Fail(pContext, "Couldn't set variables from file %s", fullPath);
				}
			}

		} 
		else if (*pReadHead != 0 && *pReadHead != '#' && *pReadHead != '/')
		{
			removeTrailingWhiteSpaces(pReadHead);

			//normal syntax is "varname = varvalue"
			if (strchr(pReadHead, '='))
			{
				if (!BuildScripting_SetVarFromString(pContext, pReadHead, -1, false, NULL, pComment))
				{
					free(pBuffer);
					return false;
				}
			}
			else
			{
/*multiline syntax is 
varname 
#BEGIN
blah
blah
blah
#END
*/

				char *pVarName = pReadHead;
				char *pVarValue = NULL;
				char *pFixedVarName = NULL;

				if (!BuildScripting_IsOkForVarName(pVarName))
				{
					BuildScripting_Fail(pContext, "Bad variable name %s", pVarName);
					free(pBuffer);
					return false;
				}

				if (!pFirstNewLine)
				{
					BuildScripting_Fail(pContext, "syntax error near presumed variable name %s", pVarName);
					free(pBuffer);
					return false;
				}

				pReadHead = pFirstNewLine + 1;
				pFirstNewLine = strchr(pReadHead, '\n');

				if (!strStartsWith(pReadHead, "#BEGIN") || !pFirstNewLine)
				{
					BuildScripting_Fail(pContext, "Didn't find \\n#BEGIN\\n after variable name %s", pVarName);
					free(pBuffer);
					return false;
				}

				pReadHead = pFirstNewLine + 1;

				while (!strStartsWith(pReadHead, "#END"))
				{
					pFirstNewLine = strchr(pReadHead, '\n');
					if (!pFirstNewLine)
					{
						estrDestroy(&pVarValue);
						BuildScripting_Fail(pContext, "never found #END in after variable name %s", pVarName);
						free(pBuffer);
						return false;
					}

					*pFirstNewLine = 0;
					estrConcatf(&pVarValue, "%s\n", pReadHead);

					pReadHead = pFirstNewLine + 1;
				}

				//remove last \n
				estrSetSize(&pVarValue, estrLength(&pVarValue) - 1);

				//actually set variable
				estrPrintf(&pFixedVarName, "$%s$", pVarName);
				BuildScripting_AddVariable(pContext, pFixedVarName, pVarValue, pComment);
				estrDestroy(&pFixedVarName);
				estrDestroy(&pVarValue);

				pFirstNewLine = strchr(pReadHead, '\n');
			}
		}

		if (pFirstNewLine)
		{
			pReadHead = pFirstNewLine + 1;
		}
		else
		{
			break;
		}
	}

	free(pBuffer);


	return true;
}


//special wrapper around BuildSystem_Fail which manages retries for SYSTEM commands
void SystemCommand_Fail(BuildScriptingContext *pContext, char *pFailureString)
{
	BuildScriptCommand *pCurCommand;
	assert(pContext->commandList.ppCommands);
	pCurCommand = pContext->commandList.ppCommands[pContext->iCurCommandNum];
	assert(pCurCommand->eCommand == BUILDSCRIPTCOMMAND_SYSTEM);

	pCurCommand->iRetryCount++;
	if (pCurCommand->iRetryCount >= pCurCommand->iNumTries)
	{
		estrCopy2(&pContext->pFailureLinkString, BuildScriptingGetLinkToLogFile(pContext, pCurCommand->outputFileName_short, "Console output"));
		BuildScripting_Fail(pContext, pFailureString);
		estrDestroy(&pContext->pFailureLinkString);
		return;
	}

	bsLogf(pContext, false, "Current step failed (%s)... retrying %d/%d\n", pFailureString,
		pCurCommand->iRetryCount + 1, pCurCommand->iNumTries);

	pContext->iTimeEnteredState = timeMsecsSince2000();
	pContext->iFramesInState = -1;
}
	

char *BuildScripting_GetCurFailureString(BuildScriptingContext *pContext)
{
	return pContext->pFailureMessage;
}

//make sure that "erase foo.exe" returns false
bool SystemStringIsAnExe(char *pCommandString)
{
	char *pFoundExe;
	char *pFoundSpace;
	while (IS_WHITESPACE(*pCommandString))
	{
		pCommandString++;
	}

	pFoundExe = strstri(pCommandString, ".exe");

	if (!pFoundExe)
	{
		return false;
	}

	pFoundSpace = strchr(pCommandString, ' ');

	if (pFoundSpace && pFoundSpace < pFoundExe)
	{
		return false;
	}

	return true;
}

bool GetSystemCommandOutputFileName(BuildScriptingContext *pContext, BuildScriptCommand *pCommand, char *pFinalString)
{
	static int iIndex = 0;
	static char *pLastFullName = NULL;

	char *pBaseName = NULL;
	int i = 0;

	//if the last file we wrote has been moved, reset the index back to zero
	if (pLastFullName && !fileExists(pLastFullName))
	{
		iIndex = 0;
	}

	estrPrintf(&pBaseName, "SYS%03d ", iIndex);
	iIndex++;

	if (pCommand->pDisplayString_Use && pCommand->pDisplayString_Use[0])
	{
		estrConcatf(&pBaseName, "%s", pCommand->pDisplayString_Use);
	}
	else
	{
		estrConcatf(&pBaseName, "%s", pFinalString);
	}

	if (estrLength(&pBaseName) > 64)
	{
		estrSetSize(&pBaseName, 64);
	}

	estrMakeAllAlphaNumAndUnderscores(&pBaseName);

	do
	{
		if (i > 0)
		{
			estrConcatf(&pBaseName, "_%d", i);
		}
		estrConcatf(&pBaseName, ".txt");
		i++;
	}
	while (fileExists(BuildScriptingGetLogFileLocation(pContext, pBaseName)));

	strcpy(pCommand->outputFileName_short, pBaseName);
	estrCopy2(&pLastFullName, BuildScriptingGetLogFileLocation(pContext, pBaseName));
	strcpy(pCommand->outputFileName, pLastFullName);
	estrCopy2(&pContext->pMostRecentSystemCommandOutputFileName, pCommand->outputFileName);

	return true;
}

void BuildScripting_AddTimeBeforeFailure(BuildScriptingContext *pContext, int iSeconds)
{
	if (pContext->commandList.ppCommands && pContext->iCurCommandNum < eaSize(&pContext->commandList.ppCommands))
	{
		pContext->commandList.ppCommands[pContext->iCurCommandNum]->iFailureTime += iSeconds;
	}
}

bool BuildScripting_SetMultipleVarsFromString(BuildScriptingContext *pContext, char *pScriptString, char *pCommentString)
{
	static char **ppVarNames = NULL;
	static char **ppVarValues = NULL;
	char *pEquals = strchr(pScriptString, '=');
	int i;

	eaDestroyEx(&ppVarNames, NULL);
	eaDestroyEx(&ppVarValues, NULL);

	if (!pEquals || strchr(pEquals + 1, '='))
	{
		return false;
	}

	*pEquals = 0;
	DivideString(pScriptString, ", ", &ppVarNames, DIVIDESTRING_POSTPROCESS_STRIP_WHITESPACE | DIVIDESTRING_POSTPROCESS_DONT_PUSH_EMPTY_STRINGS);
	DivideString(pEquals + 1, ", ", &ppVarValues, DIVIDESTRING_POSTPROCESS_STRIP_WHITESPACE | DIVIDESTRING_POSTPROCESS_DONT_PUSH_EMPTY_STRINGS);

	if (eaSize(&ppVarNames) != eaSize(&ppVarValues))
	{
		return false;
	}

	for (i = 0; i < eaSize(&ppVarNames); i++)
	{
		static char *pFullVarName = NULL;
		estrCopy2(&pFullVarName, ppVarNames[i]);
		if (pFullVarName[0] != '$')
		{
			estrInsertf(&pFullVarName, 0, "$");
		}

		if (pFullVarName[estrLength(&pFullVarName) - 1] != '$')
		{
			estrConcatf(&pFullVarName, "$");
		}

		bsLogf(pContext, false, "About to set %s to %s", pFullVarName, ppVarValues[i]);
		BuildScripting_AddVariable(pContext, pFullVarName, ppVarValues[i], pCommentString);
	}
	
	return true;
}

bool BuildScripting_SubDivideStringIntoVars(BuildScriptingContext *pContext, char *pScriptString, char *pCommentString)
{
	char **ppSubStrings = NULL;

	char *pSeparator;
	char *pWorkString;

	char **ppSubDivisions = NULL;

	int iNumVars;
	int i;


	DivideString(pScriptString, ",", &ppSubStrings, DIVIDESTRING_POSTPROCESS_DONT_PUSH_EMPTY_STRINGS 
		| DIVIDESTRING_POSTPROCESS_STRIP_WHITESPACE | DIVIDESTRING_POSTPROCESS_ESTRINGS);
	if (eaSize(&ppSubStrings) < 4)
	{
		eaDestroyEString(&ppSubStrings);
		return false;
	}

	iNumVars = eaSize(&ppSubStrings) - 2;

	for (i = 0; i < iNumVars; i++)
	{
		BuildScripting_AddDollarSignsIfNecessary(&ppSubStrings[i]);
		BuildScripting_AddVariable(pContext, ppSubStrings[i], "", pCommentString);
	}

	pSeparator = ppSubStrings[iNumVars];
	pWorkString = ppSubStrings[iNumVars + 1];


	while (1)
	{
		char *pNextSep = strstri(pWorkString, pSeparator);
		char *pCur;

		if (!pNextSep)
		{
			eaPush(&ppSubDivisions, strdup(pWorkString));
			break;
		}

		pCur = malloc(pNextSep - pWorkString + 1);
		memcpy(pCur, pWorkString, pNextSep - pWorkString);
		pCur[pNextSep - pWorkString] = 0;
		eaPush(&ppSubDivisions, pCur);
		pWorkString = pNextSep + strlen(pSeparator);
	}

	if (eaSize(&ppSubDivisions) > iNumVars)
	{
		eaDestroyEx(&ppSubDivisions, NULL);
		eaDestroyEString(&ppSubStrings);
		return false;
	}

	for (i = 0; i < eaSize(&ppSubDivisions); i++)
	{
		BuildScripting_AddVariable(pContext, ppSubStrings[i], ppSubDivisions[i], pCommentString);
	}

	eaDestroyEx(&ppSubDivisions, NULL);
	eaDestroyEString(&ppSubStrings);

	return true;
}

int BuildScripting_FindCommandWithSpecificComment(BuildScriptingContext *pContext, char *pStrToFind)
{
	int iRetVal = -1;
	int i;
	for (i = 0; i < eaSize(&pContext->commandList.ppCommands); i++)
	{
		BuildScriptCommand *pCommand = pContext->commandList.ppCommands[i];

		if (pCommand->eCommand == BUILDSCRIPTCOMMAND_COMMENT
			&& stricmp_safe(pCommand->pScriptString_Raw, pStrToFind) == 0)
		{
			if (iRetVal != -1)
			{
				BuildScripting_Fail(pContext, "Found two possible targets for GOTO to %s... %s(%d) and %s(%d)\n",
					pStrToFind,
					pCommand->pFileName_internal, pCommand->iLineNum,
					pContext->commandList.ppCommands[iRetVal]->pFileName_internal,
					pContext->commandList.ppCommands[iRetVal]->iLineNum);
			}
			else
			{
				iRetVal = i;
			}
		}
	}

	return iRetVal;
}
	


void BuildScripting_AddDollarSignsIfNecessary(char **ppVarName)
{
	if ((*ppVarName)[0] != '$')
	{
		estrInsert(ppVarName, 0, "$", 1);
	}

	if ((*ppVarName)[estrLength(ppVarName) - 1] != '$')
	{
		estrConcatChar(ppVarName, '$');
	}
}

bool ExportVarsToParent(BuildScriptingContext *pContext, char *pVars, bool bRecurse)
{
	static char **ppVars = NULL;
	int i;


	if (!pContext->pParent)
	{
		BuildScripting_Fail(pContext, "Can't export vars to parent... there is no parent!\n");
		return false;
	}

	eaDestroyEx(&ppVars, NULL);
	DivideString(pVars, ", ", &ppVars, DIVIDESTRING_POSTPROCESS_DONT_PUSH_EMPTY_STRINGS 
		| DIVIDESTRING_POSTPROCESS_STRIP_WHITESPACE);

	if (!eaSize(&ppVars))
	{
		BuildScripting_Fail(pContext, "Can't export vars to parent... no vars!\n");
		return false;
	}

	for (i = 0; i < eaSize(&ppVars); i++)
	{
		char *pVal;
		static char *pVar = NULL;
		char *pLocalComment;
		static char *pComment = NULL;

		estrCopy2(&pVar, ppVars[i]);

		BuildScripting_AddDollarSignsIfNecessary(&pVar);

		if (!BuildScripting_FindRawVarValueAndComment(pContext, pVar, &pVal, &pLocalComment))
		{
			BuildScripting_Fail(pContext, "Trying to export vars to parent... %s does not exist", ppVars[i]);
			return false;
		}

		estrPrintf(&pComment, "Exporting from scripting context %s to its parent... set in this context because: %s",
			pContext->pContextName, pLocalComment);

		BuildScripting_AddVariable(pContext->pParent, pVar, pVal, pComment);
	}

	if (bRecurse && pContext->pParent->pParent)
	{
		ExportVarsToParent(pContext->pParent, pVars, true);
	}

	return true;
}

bool ImportVarsFromParent(BuildScriptingContext *pContext, char *pVars, bool bRecurse)
{
	static char **ppVars = NULL;
	int i;

	if (!pContext->pParent)
	{
		BuildScripting_Fail(pContext, "Can't import vars from parent... there is no parent!\n");
		return false;
	}

	if (pContext->pParent->pParent && bRecurse)
	{
		if (!ImportVarsFromParent(pContext->pParent, pVars, true))
		{
			return false;
		}
	}

	eaDestroyEx(&ppVars, NULL);

	DivideString(pVars, ", ", &ppVars, DIVIDESTRING_POSTPROCESS_DONT_PUSH_EMPTY_STRINGS 
		| DIVIDESTRING_POSTPROCESS_STRIP_WHITESPACE);

	if (!eaSize(&ppVars))
	{
		BuildScripting_Fail(pContext, "Can't import vars from parent, no vars!\n");
		return false;
	}

	for (i = 0; i < eaSize(&ppVars); i++)
	{
		char *pVal;
		static char *pVar = NULL;
		static char *pComment = NULL;

		estrCopy2(&pVar, ppVars[i]);

		BuildScripting_AddDollarSignsIfNecessary(&pVar);

		if (!BuildScripting_FindRawVarValueAndComment(pContext->pParent, pVar, &pVal, NULL))
		{
			BuildScripting_AddVariable(pContext, pVar, "0", "Imported from parent, wasn't set in parent");
		}
		else
		{
			BuildScripting_AddVariable(pContext, pVar, pVal, "Imported from parent");
		}
	}

	return true;
}


enumBuildScriptCommand eMatchPairs[][2] = 
{
	{
		BUILDSCRIPTCOMMAND_FOR,
		BUILDSCRIPTCOMMAND_ENDFOR,
	},
	{
		BUILDSCRIPTCOMMAND_IF,
		BUILDSCRIPTCOMMAND_ENDIF,
	},
	{
		BUILDSCRIPTCOMMAND_BEGIN_CHILD,
		BUILDSCRIPTCOMMAND_END_CHILD,
	},
	{
		BUILDSCRIPTCOMMAND_BEGIN_CHILDREN,
		BUILDSCRIPTCOMMAND_END_CHILDREN,
	},
	{
		BUILDSCRIPTCOMMAND_BEGIN_DETACHED_CHILD,
		BUILDSCRIPTCOMMAND_END_DETACHED_CHILD,
	},
};

void BuildScripting_FindMatchingCloseAndOpenTypes(enumBuildScriptCommand eCommand, enumBuildScriptCommand *peOutMatchingOpen,
	enumBuildScriptCommand *peOutMatchingClose)
{
	int i;

	*peOutMatchingOpen = *peOutMatchingClose = 0;

	for (i = 0 ; i < ARRAY_SIZE(eMatchPairs) ; i++)
	{
		if (eMatchPairs[i][0] == eCommand)
		{
			*peOutMatchingClose = eMatchPairs[i][1];
			return;
		}
		if (eMatchPairs[i][1] == eCommand)
		{
			*peOutMatchingOpen = eMatchPairs[i][0];
			return;
		}
	}
}

AUTO_STRUCT;
typedef struct ChildBeginEndDef
{
	int iBeginChildIndex;
	int iEndChildIndex;
	BuildScriptingContextChildBehaviorFlags eFlags;
} ChildBeginEndDef;

bool CheckForBeginChildrenLegality(BuildScriptingContext *pContext, int iStartingIndex, int *piOutEndingIndex, ChildBeginEndDef ***pppBeginEndDefs)
{
	int *pOpenStack = NULL;
	bool bFoundAtLeastOneChild = false;

	int iCurIndex = iStartingIndex;

	int iCurChildBeginIndex = 0;
	enumBuildScriptCommandSubType eCurChildBeginSubType = 0;

	BuildScriptCommand *pStartingCommand = pContext->commandList.ppCommands[iCurIndex];
	BuildScriptCommand *pCurCommand;
	enumBuildScriptCommand eCommand;
	enumBuildScriptCommand eMatchingOpenType, eMatchingCloseType, ePoppedCommand;

	assert(pContext->commandList.ppCommands[iCurIndex]->eCommand == BUILDSCRIPTCOMMAND_BEGIN_CHILDREN);

	while (1)
	{
		if (iCurIndex == eaSize(&pContext->commandList.ppCommands))
		{
			BuildScripting_Fail(pContext, "Never found matching END_CHILDREN for BEGIN_CHILDREN, file %s line %d",
				pStartingCommand->pFileName_internal, pStartingCommand->iLineNum);
			return false;
		}
			
		pCurCommand = pContext->commandList.ppCommands[iCurIndex];
		eCommand = pCurCommand->eCommand;

		//at level 1, that is, between BEGIN_CHILDREN and END_CHILDREN, all that is legal is COMMENT and BEGIN_CHILD
		if (ea32Size(&pOpenStack) == 1)
		{
			if (eCommand != BUILDSCRIPTCOMMAND_COMMENT && eCommand != BUILDSCRIPTCOMMAND_BEGIN_CHILD 
				&& eCommand != BUILDSCRIPTCOMMAND_END_CHILDREN)
			{
				BuildScripting_Fail(pContext, "At the top level between a BEGIN_CHILDREN and an END_CHILDREN, found a %s, which is not legal: %s(%d)",
					StaticDefineInt_FastIntToString(enumBuildScriptCommandEnum, eCommand), pCurCommand->pFileName_internal, pCurCommand->iLineNum);
				return false;
			}
		}

		BuildScripting_FindMatchingCloseAndOpenTypes(eCommand, &eMatchingOpenType, &eMatchingCloseType);

		if (eMatchingCloseType)
		{
			if (eCommand == BUILDSCRIPTCOMMAND_BEGIN_CHILD)
			{
				if (!(pCurCommand->eSubType == BSCSUBTYPE_MUSTSUCCEED || pCurCommand->eSubType == BSCSUBTYPE_FAILURE_IS_NON_FATAL))
				{
					BuildScripting_Fail(pContext, "%s(%d): Got a BEGIN_CHILD with a SubType other than MUSTSUCCEED or FAILURE_IS_NON_FATAL, this is illegal",
						pCurCommand->pFileName_internal, pCurCommand->iLineNum);
				}

				eCurChildBeginSubType = pCurCommand->eSubType;

				BuildScripting_DoVariableReplacing(pContext, pCurCommand);
			
				if (!estrLength(&pCurCommand->pScriptString_Use))
				{
					BuildScripting_Fail(pContext, "%s(%d): Got a BEGIN_CHILD with no child name",
						pCurCommand->pFileName_internal, pCurCommand->iLineNum);
					return false;
				}

				if (ea32Size(&pOpenStack) == 1)
				{


					iCurChildBeginIndex = iCurIndex;
				}

				bFoundAtLeastOneChild = true;
			}

			ea32Push(&pOpenStack, eCommand);
		}
		else if (eMatchingOpenType)
		{
			if (!ea32Size(&pOpenStack))
			{
				BuildScripting_Fail(pContext, "Found an unmatched %s while checking for BEGIN_CHILDREN legality: %s(%d)",
					StaticDefineInt_FastIntToString(enumBuildScriptCommandEnum,eCommand), pCurCommand->pFileName_internal, pCurCommand->iLineNum);
				return false;
			}

			ePoppedCommand = ea32Pop(&pOpenStack);
			if (ePoppedCommand != eMatchingOpenType)
			{
				BuildScripting_Fail(pContext, "Found an unmatched %s while checking for BEGIN_CHILDREN legality: %s(%d)",
					StaticDefineInt_FastIntToString(enumBuildScriptCommandEnum,eCommand), pCurCommand->pFileName_internal, pCurCommand->iLineNum);
				return false;
			}

			if (ea32Size(&pOpenStack) == 1)
			{
				ChildBeginEndDef *pDef = StructCreate(parse_ChildBeginEndDef);
				assert(ePoppedCommand == BUILDSCRIPTCOMMAND_BEGIN_CHILD); //this should already be assured by the previous
					//safeguards
				pDef->iBeginChildIndex = iCurChildBeginIndex;
				pDef->iEndChildIndex = iCurIndex;

				pDef->eFlags = (eCurChildBeginSubType == BSCSUBTYPE_FAILURE_IS_NON_FATAL) ? FAILURE_IS_NONFATAL_FOR_PARENT : 0;

				eaPush(pppBeginEndDefs, pDef);


			}

		}

		if (ea32Size(&pOpenStack) == 0)
		{
			if (!bFoundAtLeastOneChild)
			{
				BuildScripting_Fail(pContext, "Got to END_CHILDREN without ever seeing BEGIN_CHILD: %s(%d)",
					pCurCommand->pFileName_internal, pCurCommand->iLineNum);
				return false;
			}

			*piOutEndingIndex = iCurIndex;
			return true;
		}

		iCurIndex++;
	}

	return true;
}

//given a script and the index of a command that is one of the BEGIN types from eMatchPairs,
//returns the index of the matching end, or -1 (in which case BuildScripting_Fail will already have been called)
int BuildScripting_FindBalancedBlockEndIndex(BuildScriptingContext *pContext, int iBeginCommandIndex)
{
	enumBuildScriptCommand eMatchingOpenCommand, eMatchingCloseCommand;
	int *pOpenCommandStack = NULL;
	int iCurIndex;
	BuildScriptCommand *pInitialCommand;

	pInitialCommand = pContext->commandList.ppCommands[iBeginCommandIndex];

	BuildScripting_FindMatchingCloseAndOpenTypes(pInitialCommand->eCommand, &eMatchingOpenCommand, &eMatchingCloseCommand);

	if (!eMatchingCloseCommand)
	{
		BuildScripting_Fail(pContext, "FindBalancedBlockEndIndex called for invalid command type: %s",
			StaticDefineInt_FastIntToString(enumBuildScriptCommandEnum, pInitialCommand->eCommand));
		return -1;
	}

	ea32Push(&pOpenCommandStack, pInitialCommand->eCommand);
	iCurIndex = iBeginCommandIndex + 1;

	while (1)
	{
		BuildScriptCommand *pCurCommand;

		if (iCurIndex >= eaSize(&pContext->commandList.ppCommands))
		{
			BuildScripting_Fail(pContext, "Never found match for %s (%s(%d))",
				StaticDefineInt_FastIntToString(enumBuildScriptCommandEnum, pInitialCommand->eCommand),
				pInitialCommand->pFileName_internal, pInitialCommand->iLineNum);
			return -1;
		}

		pCurCommand = pContext->commandList.ppCommands[iCurIndex];
		BuildScripting_FindMatchingCloseAndOpenTypes(pCurCommand->eCommand, &eMatchingOpenCommand, &eMatchingCloseCommand);

		if (eMatchingOpenCommand)
		{
			if (eMatchingOpenCommand != (enumBuildScriptCommand)ea32Pop(&pOpenCommandStack))
			{
				BuildScripting_Fail(pContext, "While trying to find match for %s (%s(%d)), found unbalanced %s (%s(%d))",
					StaticDefineInt_FastIntToString(enumBuildScriptCommandEnum, pInitialCommand->eCommand),
					pInitialCommand->pFileName_internal, pInitialCommand->iLineNum,
					StaticDefineInt_FastIntToString(enumBuildScriptCommandEnum, pCurCommand->eCommand),
					pCurCommand->pFileName_internal, pCurCommand->iLineNum);
				return -1;
			}

			if (ea32Size(&pOpenCommandStack) == 0)
			{
				return iCurIndex;
			}
		}

		if (eMatchingCloseCommand)
		{
			ea32Push(&pOpenCommandStack, pCurCommand->eCommand);
		}

		iCurIndex++;
	}
}



void BuildScripting_AddResultVariablesFromChildContext(BuildScriptingContext *pParent, BuildScriptingContext *pChild)
{
	char varName[256];

	sprintf(varName, "$%s_RESULT$", pChild->pContextName);
	BuildScripting_AddVariable(pParent, varName, StaticDefineInt_FastIntToString(enumBuildScriptStateEnum, pChild->eScriptState), "Set in parent context during END_CHILDREN command");

	if (pChild->eScriptState == BUILDSCRIPTSTATE_FAILED)
	{
		sprintf(varName, "$%s_FAILURE_REASON$", pChild->pContextName);
		BuildScripting_AddVariable(pParent, varName, pChild->pFailureMessage, 
			"Set in parent context during END_CHILDREN command");
	}
}

BuildScriptingContext *BuildScripting_FindDetachedContext(BuildScriptingContext *pParentContext, char *pName)
{

	FOR_EACH_IN_EARRAY(pParentContext->ppDetachedChildren, BuildScriptingContext, pChildContext)
	{
		if (stricmp(pChildContext->pContextName, pName) == 0)
		{
			return pChildContext;
		}
	}
	FOR_EACH_END;

	return NULL;
}



//returns true if waiting is complete
bool BuildScripting_WaitForDetachedChildren(BuildScriptingContext *pContext, BuildScriptCommand *pCommand)
{


	if (estrLength(&pCommand->pScriptString_Use))
	{
		char **ppNamesToWaitFor = NULL;
		int i;

		BuildScriptingContext **ppStillRunningContexts = NULL;
		
		DivideString(pCommand->pScriptString_Use, ",", &ppNamesToWaitFor, DIVIDESTRING_POSTPROCESS_STRIP_WHITESPACE | DIVIDESTRING_POSTPROCESS_DONT_PUSH_EMPTY_STRINGS);

		if (!eaSize(&ppNamesToWaitFor))
		{
			BuildScripting_Fail(pContext, "In WAIT_FOR_DETACHED_CHILDREN, can't parse string <<%s>> into meaningful child context names",
				pCommand->pScriptString_Use);
			return false;
		}

		if (pContext->iFramesInState == 0)
		{
			char *pFoundContexts = NULL;
			char *pCompleteContexts = NULL;

			char *pResultString;
			char varName[256];
			bool bDone = true;

			for (i = 0; i < eaSize(&ppNamesToWaitFor); i++)
			{
				if (BuildScripting_FindDetachedContext(pContext, ppNamesToWaitFor[i]))
				{
					bDone = false;
					estrConcatf(&pFoundContexts, "%s%s", estrLength(&pFoundContexts) ? ", ":"", ppNamesToWaitFor[i]);
				}
				else 
				{
					sprintf(varName, "$%s_RESULT$", ppNamesToWaitFor[i]);

					if (BuildScripting_FindVarValue(pContext, varName, &pResultString))
					{
						estrConcatf(&pCompleteContexts, "%s%s(%s)", estrLength(&pCompleteContexts) ? ", ":"",
							ppNamesToWaitFor[i], pResultString);
					}
					else
					{
						BuildScripting_Fail(pContext, "Trying to WAIT_FOR_DETACHED_CHILDREN, given name %s. It does not exist, and never seems to have existed",
							ppNamesToWaitFor[i]);

						estrDestroy(&pFoundContexts);
						estrDestroy(&pCompleteContexts);
						eaDestroyEx(&ppNamesToWaitFor, NULL);

						return false;
					}
				}
			}

			bsLogf(pContext, false, "Began WAIT_FOR_DETACHED_CHILDREN: Contexts that are still running: %s. Contexts that are completed: %s",
				estrLength(&pFoundContexts) ? pFoundContexts : "(NONE)",
				estrLength(&pCompleteContexts) ? pCompleteContexts : "(NONE)");
			
			estrDestroy(&pFoundContexts);
			estrDestroy(&pCompleteContexts);
			eaDestroyEx(&ppNamesToWaitFor, NULL);

			return bDone;
		}
		else
		{
			for (i = 0; i < eaSize(&ppNamesToWaitFor); i++)
			{
				if (BuildScripting_FindDetachedContext(pContext, ppNamesToWaitFor[i]))
				{
					eaDestroyEx(&ppNamesToWaitFor, NULL);
					return false;
				}
			}
			
			eaDestroyEx(&ppNamesToWaitFor, NULL);
			return true;
		}
	}
	else
	{

		if (!eaSize(&pContext->ppDetachedChildren))
		{
			return true;
		}

		if (pContext->iFramesInState == 0)
		{
			char *pAllNamesString = NULL;

			FOR_EACH_IN_EARRAY(pContext->ppDetachedChildren, BuildScriptingContext, pChildContext)
			{
				estrConcatf(&pAllNamesString, "%s%s", estrLength(&pAllNamesString) == 0 ? "" : ", ", pChildContext->pContextName);
			}
			FOR_EACH_END;
			bsLogf(pContext, false, "Just entered WAIT_FOR_DETACHED_CHILDREN, waiting for ALL, currently waiting on %d contexts: %s",
				eaSize(&pContext->ppDetachedChildren), pAllNamesString);
			estrDestroy(&pAllNamesString);
		}

		return false;
	}
}

//when a context has started to fail, but is waiting for all its uninterruptible children to complete,
//we don't do "normal" tick stuff, we just recurse this call, which turns into a "normal" tick solely for
//the noninterruptible children
void BuildScripting_DoSpecialWaitingForNoninterruptibleChildrenTick(BuildScriptingContext *pContext)
{
	FOR_EACH_IN_EARRAY(pContext->ppChildren, BuildScriptingContext, pChild)
	{
		if (BuildScripting_CantBeInterrupted(pChild))
		{
			BuildScripting_Tick(pChild);
		}
		else
		{
			BuildScripting_DoSpecialWaitingForNoninterruptibleChildrenTick(pChild);
		}
	}
	FOR_EACH_END;

	FOR_EACH_IN_EARRAY(pContext->ppDetachedChildren, BuildScriptingContext, pChild)
	{
		if (BuildScripting_CantBeInterrupted(pChild))
		{
			BuildScripting_Tick(pChild);
		}
		else
		{
			BuildScripting_DoSpecialWaitingForNoninterruptibleChildrenTick(pChild);
		}
	}
	FOR_EACH_END;


}

bool CheckVariableInRoot(BuildScriptingContext *pContext, const char *pVarName)
{
	BuildScriptingContext *pRoot = pContext;
	char tempName[256];
	char *pValue = NULL;
	bool bFound;

	while (pRoot->pParent)
	{
		pRoot = pRoot->pParent;
	}

	sprintf(tempName, "$%s$", pVarName);

	bFound = BuildScripting_FindVarValue(pRoot, tempName, &pValue);

	if (bFound)
	{
		estrTrimLeadingAndTrailingWhitespace(&pValue);

		if (!estrLength(&pValue) || stricmp(pValue, "0") == 0)
		{
			bFound = false;
		}
	}

	estrDestroy(&pValue);
	return bFound;
}


void BuildScripting_Tick_Internal(BuildScriptingContext *pContext)
{
	BuildScriptCommand *pCurCommand;
//	char *pBuildID = NULL;
	char *pEmailFileName = NULL;
	if (pContext->bDisabled)
	{
		return;
	}

	if (pContext->eScriptState != BUILDSCRIPTSTATE_RUNNING)
	{
		return;
	}

	//corner case... when child contexts are created, they start with frames in state -1 which sometimes
	//immediately gets bumped up to 0, but not always
	if (pContext->iFramesInState == -1)
	{
		pContext->iFramesInState = 0;
	}

	if (pContext->bInSpecialFailingModeWaitingForUninterruptibleChildContexts)
	{
		//first, check if all our children are now done, if they are we just do a normal fail
		if (BuildScripting_CantBeInterrupted_Recurse(pContext, NULL))
		{
			BuildScripting_DoSpecialWaitingForNoninterruptibleChildrenTick(pContext);
		}
		else
		{
			BuildScripting_DoFinalFailStuff(pContext);
		}
		return;
	}


	assertmsg(pContext->iCurCommandNum >= 0, "CommandNum negative... probably doing a return instead of a break out of BuildScripting_Tick");

	if (eaSize(&pContext->ppDetachedChildren))
	{
		int i;
		for (i = eaSize(&pContext->ppDetachedChildren) - 1; i >= 0; i--)
		{
			BuildScriptingContext *pDetachedChild = pContext->ppDetachedChildren[i];
			BuildScripting_Tick_Internal(pDetachedChild);
			switch (pDetachedChild->eScriptState)
			{
			case BUILDSCRIPTSTATE_RUNNING:
				//do nothing;
				break;

			case BUILDSCRIPTSTATE_SUCCEEDED:
			case BUILDSCRIPTSTATE_SUCCEEDED_WITH_ERRORS:
				
				BuildScripting_AddResultVariablesFromChildContext(pContext, pDetachedChild);
				bsLogf(pContext, false, "Detached child context %s is complete with state %s",
					pDetachedChild->pContextName, StaticDefineInt_FastIntToString(enumBuildScriptStateEnum, pDetachedChild->eScriptState));
				StructDestroy(parse_BuildScriptingContext, pDetachedChild);
				eaRemoveFast(&pContext->ppDetachedChildren, i);
				
				break;

			case BUILDSCRIPTSTATE_FAILED:
				BuildScripting_AddResultVariablesFromChildContext(pContext, pDetachedChild);
				if (pDetachedChild->eChildBehaviorFlags & FAILURE_IS_NONFATAL_FOR_PARENT)
				{
					bsLogf(pContext, false, "Detached child context %s failed, but this is nonfatal for us. Failure string: %s",
						pDetachedChild->pContextName, pDetachedChild->pFailureMessage);
					StructDestroy(parse_BuildScriptingContext, pDetachedChild);
					eaRemoveFast(&pContext->ppDetachedChildren, i);	
				}
				else
				{
					BuildScripting_Fail(pContext, "Detached child context %s failed. Failure string: %s",
						pDetachedChild->pContextName, pDetachedChild->pFailureMessage);
					return;
				}
				break;

			default:
				BuildScripting_Fail(pContext, "Child context %s got itself into illegal state %s",
					pDetachedChild->pContextName, StaticDefineInt_FastIntToString(enumBuildScriptStateEnum, pDetachedChild->eScriptState));
				return;

			}
		}
	}


	assert(pContext->commandList.ppCommands);
	pCurCommand = pContext->commandList.ppCommands[pContext->iCurCommandNum];
	assert(pCurCommand);

	if (pContext->bForceAbort)
	{
		if (pContext->pBuildScriptingProcHandle)
		{
			KillQueryableProcess(&pContext->pBuildScriptingProcHandle);
		}

		BuildScripting_Abort(pContext, "Force aborted by user");
		pContext->bForceAbort = false;
		return;
	}


	switch(pCurCommand->eCommand)
	{
	case BUILDSCRIPTCOMMAND_WAIT:
		{
			float fTimeToWait;

			if (pCurCommand->pScriptString_Use)
			{
				fTimeToWait = atof(pCurCommand->pScriptString_Use);
			}
			else if (pCurCommand->iScriptInt)
			{
				fTimeToWait = (float)pCurCommand->iScriptInt;
			}
			else
			{
				fTimeToWait = pCurCommand->fScriptFloat;
			}

			if ((float)(timeMsecsSince2000() - pContext->iTimeEnteredState) / 1000.0f > fTimeToWait || pContext->bSkipCurrentStep)
			{
				BuildScripting_NewCommand(pContext, ++pContext->iCurCommandNum);
			}
		}
		break;

	case BUILDSCRIPTCOMMAND_SETVARIABLE:
		bsLogf(pContext, false, "doing SETVARIABLE %s", pCurCommand->pScriptString_Use);
		if (!BuildScripting_SetVarFromString(pContext, pCurCommand->pScriptString_Use, -1, pCurCommand->bSetVariableRaw, NULL, BuildScripting_GetCommentForCommand(pCurCommand)))
		{
			BuildScripting_Fail(pContext, "Couldn't parse variable setting string %s", pCurCommand->pScriptString_Use);
			return;
		}
		BuildScripting_NewCommand(pContext, ++pContext->iCurCommandNum);
		break;	
	case BUILDSCRIPTCOMMAND_SET_NO_EXPAND:
		bsLogf(pContext, false, "doing SET_NO_EXPAND %s", pCurCommand->pScriptString_Use);
		if (!BuildScripting_SetVarFromString(pContext, pCurCommand->pScriptString_Raw, -1, pCurCommand->bSetVariableRaw, NULL, BuildScripting_GetCommentForCommand(pCurCommand)))
		{
			BuildScripting_Fail(pContext, "Couldn't parse variable setting string %s", pCurCommand->pScriptString_Raw);
			return;
		}
		BuildScripting_NewCommand(pContext, ++pContext->iCurCommandNum);
		break;
	case BUILDSCRIPTCOMMAND_SETVARIABLESFROMFILE:
		bsLogf(pContext, false, "doing BUILDSCRIPTCOMMAND_SETVARIABLESFROMFILE %s", pCurCommand->pScriptString_Use);

		if (!BuildScripting_SetVariablesFromFile(pContext, pCurCommand->pScriptString_Use, pCurCommand->eSubType == BSCSUBTYPE_IGNORERESULT, BuildScripting_GetCommentForCommand(pCurCommand)))
		{
			if (pCurCommand->eSubType == BSCSUBTYPE_IGNORERESULT)
			{
				bsLogf(pContext, false, "Couldn't set variables from file %s, but that's OK because IGNORERESULT is set", 
					pCurCommand->pScriptString_Use);
			}
			else
			{
				BuildScripting_Fail(pContext, "Couldn't parse variable setting file %s", pCurCommand->pScriptString_Use);
				return;
			}
		}

		BuildScripting_NewCommand(pContext, ++pContext->iCurCommandNum);
		break;

	case BUILDSCRIPTCOMMAND_ABORT_IF_EXPRESSION_TRUE:
		if (pCurCommand->pScriptString_Use && BuildScripting_IsExpressionStringTrue(pCurCommand->pScriptString_Use))
		{
			BuildScripting_Abort(pContext, STACK_SPRINTF("Aborting build scripting because expression \"%s\" (originally \"%s\") is true",
				pCurCommand->pScriptString_Use, pCurCommand->pScriptString_Raw));
			return;
		}
		else
		{
			bsLogf(pContext, false, "NOT aborting build scripting because expression \"%s\" (originally \"%s\") is false",
				pCurCommand->pScriptString_Use, pCurCommand->pScriptString_Raw);
		}

		BuildScripting_NewCommand(pContext, ++pContext->iCurCommandNum);
		break;

	case BUILDSCRIPTCOMMAND_ABORT:
		if (pCurCommand->pScriptString_Use)
		{
			BuildScripting_Abort(pContext, pCurCommand->pScriptString_Use);
		}
		else
		{
			BuildScripting_Abort(pContext, STACK_SPRINTF("Non-commented abort at %s(%d)", pCurCommand->pFileName, pCurCommand->iLineNum));
		}
		break;


	case BUILDSCRIPTCOMMAND_FAIL_IF_EXPRESSION_TRUE:
		//if pFailureExpression is set, use it for expression, scriptString for commentary
		if (pCurCommand->pFailureExpression_Use)
		{
			if (BuildScripting_IsExpressionStringTrue(pCurCommand->pFailureExpression_Use))
			{
				if (pCurCommand->pScriptString_Use)
				{
					BuildScripting_Fail(pContext, "Script failing: %s", pCurCommand->pScriptString_Use);
				}
				else
				{
					BuildScripting_Fail(pContext, "Failing build scripting because expression \"%s\" (originally \"%s\") is true",
						pCurCommand->pFailureExpression_Use, pCurCommand->pFailureExpression_Raw);
				}
			}
			else
			{
				bsLogf(pContext, false, STACK_SPRINTF("NOT failing build scripting because expression \"%s\" (originally \"%s\") is false",
					pCurCommand->pScriptString_Use, pCurCommand->pScriptString_Raw));
			}
		}
		else
		{
			if (pCurCommand->pScriptString_Use && BuildScripting_IsExpressionStringTrue(pCurCommand->pScriptString_Use))
			{
				BuildScripting_Fail(pContext, "Failing build scripting because expression \"%s\" (originally \"%s\") is true",
					pCurCommand->pScriptString_Use, pCurCommand->pScriptString_Raw);
				return;
			}
			else
			{
				bsLogf(pContext, false, STACK_SPRINTF("NOT failing build scripting because expression \"%s\" (originally \"%s\") is false",
					pCurCommand->pScriptString_Use, pCurCommand->pScriptString_Raw));
			}
		}

		BuildScripting_NewCommand(pContext, ++pContext->iCurCommandNum);
		break;

	case BUILDSCRIPTCOMMAND_FAIL:
		if (pCurCommand->pScriptString_Use)
		{
			BuildScripting_Fail(pContext, "%s", pCurCommand->pScriptString_Use);
		}
		else
		{
			BuildScripting_Fail(pContext, "non-commented FAIL at %s(%d)", pCurCommand->pFileName, pCurCommand->iLineNum);
		}
		break;


	case BUILDSCRIPTCOMMAND_FAIL_IF_STRING_NONEMPTY:
		if (pCurCommand->pScriptString_Use && !StringIsAllWhiteSpace(pCurCommand->pScriptString_Use))
		{
			if (pCurCommand->pDisplayString_Use)
			{
				BuildScripting_Fail(pContext, "%s - %s", pCurCommand->pDisplayString_Use, pCurCommand->pScriptString_Use);
			}
			else
			{
				BuildScripting_Fail(pContext, pCurCommand->pScriptString_Use);
			}
		}
		else
		{
			bsLogf(pContext, false, STACK_SPRINTF("NOT failing build scripting because expression \"%s\" was empty when evaluated",
				pCurCommand->pScriptString_Raw));
		}

		BuildScripting_NewCommand(pContext, ++pContext->iCurCommandNum);
		break;



	case BUILDSCRIPTCOMMAND_EXECUTE_COMMAND:
		if (!globCmdParseSpecifyHow(pCurCommand->pScriptString_Use, CMD_CONTEXT_HOWCALLED_BUILDSCRIPTING))
		{
			BuildScripting_Fail(pContext, "Couldn't EXECUTE_COMMAND %s (originally %s)", pCurCommand->pScriptString_Use, pCurCommand->pScriptString_Raw);
		}
		else
		{
			BuildScripting_NewCommand(pContext, ++pContext->iCurCommandNum);
		}
		break;



	case BUILDSCRIPTCOMMAND_SYSTEM:
		if (pContext->iFramesInState == 0)
		{
			char *pFinalString = NULL;
			char outFileName[CRYPTIC_MAX_PATH];

			if (pContext->bTestingOnly)
			{
				if (pCurCommand->pWorkingDirectory_Use)
				{
					printf("(Working directory: %s) ", pCurCommand->pWorkingDirectory_Use);
				}

				printf("%s\n", pCurCommand->pScriptString_Use);
				BuildScripting_NewCommand(pContext, ++pContext->iCurCommandNum);
				break;
			}



			if (pCurCommand->pWorkingDirectory_Use)
			{
				if (chdir(pCurCommand->pWorkingDirectory_Use))
				{
					
					SystemCommand_Fail(pContext, STACK_SPRINTF("Couldn't change to directory %s", pCurCommand->pWorkingDirectory_Use));
					pContext->iFramesInState++;
					return;
				}
			}

			estrPrintf(&pFinalString, "%s", pCurCommand->pScriptString_Use);
		
			if (!GetSystemCommandOutputFileName(pContext, pCurCommand, pFinalString))
			{
				return;
			}

			BuildScriptingCommandLineOverride(pCurCommand, &pFinalString);

			if (strchr(pFinalString, '>') || strstri(pFinalString, "forkprintfs"))
			{
				SystemCommand_Fail(pContext, STACK_SPRINTF("System command \"%s\" contains a > character or forkprintfs. This is illegal. If you want to redirect, use OutputFile and AppendFile, and IsCrypticAppWithOwnConsole if necessary", pFinalString));
				estrDestroy(&pFinalString);
				pContext->iFramesInState++;
				return;
			}



			if (pCurCommand->bIsCrypticAppWithOwnConsole)
			{
				estrConcatf(&pFinalString, " -forkPrintfsToFile %s", pCurCommand->outputFileName);

			}
/*			else
			{
				estrConcatf(&pFinalString, " 2>&1 | tee.exe -a %s", pCurCommand->outputFileName);
			}*/

			if (pCurCommand->bIsExpectingCBComments)
			{
				char *pSuperEscapedContextName = NULL;
				estrStackCreate(&pSuperEscapedContextName);
				estrSuperEscapeString(&pSuperEscapedContextName, pContext->pContextName);
				estrConcatf(&pFinalString, " -SuperEsc ContextNameOnCB %s", pSuperEscapedContextName);
				estrDestroy(&pSuperEscapedContextName);
			}
		
			consolePushColor();
			if (pCurCommand->pWorkingDirectory_Use)
			{
				bsLogf(pContext, true, "WD: %s", pCurCommand->pWorkingDirectory_Use);
			}
			if (pCurCommand->pOutputFile_Use)
			{
				bsLogf(pContext, true, "OUTPUTFILE: %s", pCurCommand->pOutputFile_Use);
			}
			if (pCurCommand->pAppendFile_Use)
			{
				bsLogf(pContext, true, "APPENDFILE: %s", pCurCommand->pAppendFile_Use);
			}

			
			getFileNameNoExtNoDirs(outFileName, BuildScripting_GetMostRecentSystemOutputFilename(pContext));
			outFileName[7] = 0;
			bsLogf(pContext, true, "(%s)SYSTEM: %s", outFileName, pFinalString);
			consolePopColor();

			if (!(pContext->pBuildScriptingProcHandle = StartQueryableProcess(pFinalString, NULL, true, false, false, pCurCommand->bIsCrypticAppWithOwnConsole ? NULL : pCurCommand->outputFileName)))
			{
				SystemCommand_Fail(pContext, STACK_SPRINTF("Couldn't start process %s", pCurCommand->pScriptString_Use));
				estrDestroy(&pFinalString);
				pContext->iFramesInState++;
				return;
			}
			estrDestroy(&pFinalString);
		}
		else
		{
			int iRetVal;
			bool bComplete;

			if (pContext->iBuildScriptingFailTime && pContext->iBuildScriptingFailTime < timeSecondsSince2000_ForceRecalc())
			{
				KillQueryableProcess(&pContext->pBuildScriptingProcHandle);
				iRetVal = -1;
				bComplete = true;
			}
			else
			{
				bComplete = QueryableProcessComplete(&pContext->pBuildScriptingProcHandle, &iRetVal);
			}

		

			if (bComplete)
			{
				if (pCurCommand->pVariableForSystemOutput)
				{
					static char *pTempComment = NULL;
					estrPrintf(&pTempComment, "VariableForSystemOutput for %s", BuildScripting_GetCommentForCommand(pCurCommand));
					SetVariableFromFile(pContext, pCurCommand->pVariableForSystemOutput, pCurCommand->outputFileName, false, pTempComment);
				}

				if (pCurCommand->pVariableForEscapedSystemOutput)
				{
					static char *pTempComment = NULL;
					estrPrintf(&pTempComment, "pVariableForEscapedSystemOutput for %s", BuildScripting_GetCommentForCommand(pCurCommand));
					SetVariableFromFile(pContext, pCurCommand->pVariableForEscapedSystemOutput, pCurCommand->outputFileName, true, pTempComment);
				}

				if (pCurCommand->pVariableForSystemResult)
				{
					char temp[32];
					static char *pTempName = NULL;
					static char *pTempComment = NULL;
					sprintf(temp, "%d", iRetVal);
					estrPrintf(&pTempName, "$%s$", pCurCommand->pVariableForSystemResult);
					estrPrintf(&pTempComment, "VariableForSystemResult for %s", BuildScripting_GetCommentForCommand(pCurCommand));
					BuildScripting_AddVariable(pContext, pTempName, temp, pTempComment);
				}

				if (pCurCommand->pOutputFile_Use)
				{
					char systemString[1024];
					sprintf(systemString, "type %s > %s", pCurCommand->outputFileName, pCurCommand->pOutputFile_Use);
					system(systemString);
				}

				if (pCurCommand->pAppendFile_Use)
				{
					char systemString[1024];
					sprintf(systemString, "type %s >> %s", pCurCommand->outputFileName, pCurCommand->pAppendFile_Use);
					system(systemString);
				}

				if (pContext->bFailAfterDelayCalled)
				{
					if (iRetVal == 0)
					{
						bsLogf(pContext, false, "Got return value 0 from step %s(%s), but we'd gotten a delayed fail request (presumably due to a detected crash/assert), so setting ret val to -1", pCurCommand->bNoScriptStringInErrorReport ? "" : pContext->pCurStepString, pContext->pCurDisplayString);
					
						iRetVal = -1;
					}
				}


				if (iRetVal != 0 && estrLength(&pCurCommand->pExtendedErrorType))
				{
					static char *pExtendedErrorString = NULL;
					
					switch (BuildScripting_GetExtendedErrors(pCurCommand->pExtendedErrorType, pCurCommand->outputFileName, &pExtendedErrorString))
					{
					case EXTENDEDERROR_NOTANERROR:
						iRetVal = 0;
						break;
					case EXTENDEDERROR_FATAL:
						break;
	/*				case EXTENDEDERROR_RESTART_SCRIPT:
						{
							ErrorData data = {0};
							data.eType = ERRORDATATYPE_COMPILE;
							data.pErrorString = STACK_SPRINTF("Found \"fake\" compile error %s", pExtendedErrorString);
							CB_ProcessErrorData(NULL, &data);
							break;
						}*/
					case EXTENDEDERROR_RETRY:
						bsLogf(pContext, false, "Got extended error message <<%s>>, that is a RETRY error, so retrying compile", pExtendedErrorString);
						//force a few retries when we get this kind of error message
						pCurCommand->iNumTries = MIN(3, pCurCommand->iRetryCount + 2);
						break;
					}
					
				}



				switch (pCurCommand->eSubType)
				{
				case BSCSUBTYPE_MUSTSUCCEED:
					if (iRetVal != 0)
					{
						SystemCommand_Fail(pContext, STACK_SPRINTF("Return value %d from step %s(%s)",
							iRetVal, pCurCommand->bNoScriptStringInErrorReport ? "" : pContext->pCurStepString, pContext->pCurDisplayString));
						pContext->iFramesInState++;

						return;
					}
					else
					{
						bsLogf(pContext, false, "System command completed successfully");
					}
				break;

				case BSCSUBTYPE_FAILURE_IS_NON_FATAL:
					if (iRetVal != 0)
					{
						BuildScripting_Error(pContext, STACK_SPRINTF("Return value %d from step %s(%s)",
							iRetVal, pCurCommand->bNoScriptStringInErrorReport ? "" : pContext->pCurStepString, pContext->pCurDisplayString));
					}
					else
					{
						bsLogf(pContext, false, "System command completed successfully");
					}

				break;

				case BSCSUBTYPE_CONTROLLER_STYLE:
					if (iRetVal == -2)
					{
						pContext->bThereWereErrors = true;
						bsLogf(pContext, false, "System command completed with errors");
					}
					else if (iRetVal != 0)
					{
						SystemCommand_Fail(pContext, STACK_SPRINTF("Return value %d from step %s(%s)",
							iRetVal, pCurCommand->bNoScriptStringInErrorReport ? "" : pContext->pCurStepString, pContext->pCurDisplayString));
						pContext->iFramesInState++;

						return;
					}
					else
					{
						bsLogf(pContext, false, "System command completed successfully");
					}

				break;
				}

				BuildScripting_NewCommand(pContext, ++pContext->iCurCommandNum);
			}
			else
			{
				if (BuildScripting_DEFINED_AND_TRUE(pContext, "NO_TIMEOUTS"))
				{
					if (timeSecondsSince2000() % 10 == 0)
					{
						SetConsoleTitle(L"NO_TIMEOUTS... this may run forever");
					}
				}
				else
				{
					if (pCurCommand->iFailureTime && (timeMsecsSince2000() - pContext->iTimeEnteredState) / 1000 > pCurCommand->iFailureTime)
					{
						KillQueryableProcess(&pContext->pBuildScriptingProcHandle);

						switch (pCurCommand->eSubType)
						{
						case BSCSUBTYPE_MUSTSUCCEED:
						case BSCSUBTYPE_CONTROLLER_STYLE:
							

							SystemCommand_Fail(pContext, STACK_SPRINTF("Failure time overflow. More than %s while doing %s(%s)",
								GetPrettyDurationString(pCurCommand->iFailureTime),
								pContext->pCurStepString, pContext->pCurDisplayString));
							pContext->iFramesInState++;
							return;
			
						

						case BSCSUBTYPE_FAILURE_IS_NON_FATAL:
							BuildScripting_Error(pContext, STACK_SPRINTF("Failure time overflow. More than %s while doing %s(%s) (a non-critical step)",
								GetPrettyDurationString(pCurCommand->iFailureTime),
								pContext->pCurStepString, pContext->pCurDisplayString));
							break;
						}

						BuildScripting_NewCommand(pContext, ++pContext->iCurCommandNum);

						pContext->iFramesInState++;
						return;
					}

					if (pCurCommand->iFailureTime)
					{
						int iTimeUntilFailure = pCurCommand->iFailureTime - (int)((timeMsecsSince2000() - pContext->iTimeEnteredState) / 1000);
						if (iTimeUntilFailure % 10 == 0)
						{
							SetConsoleTitle_UTF8(STACK_SPRINTF("Time out in %d seconds", iTimeUntilFailure));
						}
					}
				}

			}
		}
		break;


	case BUILDSCRIPTCOMMAND_FOR:
		if (!pCurCommand->bLooping)
		{
			eaDestroyEx(&pCurCommand->ppBufferedFORTokens, NULL);
			pCurCommand->iForCount = 0;
			pContext->iCurForDepth++;
		}
		pCurCommand->bLooping = false;

		if (!BuildScripting_SetVarFromString(pContext, pCurCommand->pScriptString_Use, pCurCommand->iForCount, false, &pCurCommand->ppBufferedFORTokens, BuildScripting_GetCommentForCommand(pCurCommand)))
		{
			BuildScripting_Fail(pContext, "Couldn't parse FOR string \"%s\" (forCount %d)", pCurCommand->pScriptString_Use, pCurCommand->iForCount);
			return;
		}
		BuildScripting_NewCommand(pContext, ++pContext->iCurCommandNum);
		break;


	case BUILDSCRIPTCOMMAND_ENDFOR:
			{
				int iForCommandIndex;
				BuildScriptCommand *pForCommand = BuildScripting_FindMatchingFOR(pContext, pContext->iCurCommandNum, &iForCommandIndex);

				if (!pForCommand)
				{
					BuildScripting_Fail(pContext, "NonMatching ENDFOR");
					return;
				}

				pForCommand->iForCount++;
				if (pForCommand->iForCount == BuildScripting_GetMaxFORCount(pForCommand))
				{
					BuildScripting_NewCommand(pContext, ++pContext->iCurCommandNum);
					pContext->iCurForDepth--;
				}
				else
				{
					pForCommand->bLooping = true;
					BuildScripting_NewCommand(pContext, iForCommandIndex);
				}
			}
			break;	

	case BUILDSCRIPTCOMMAND_IF:
		{
			static char *pFailString = NULL;

			if (pCurCommand->iIfIndex == -1)
			{
				if (!BuildScripting_FindMatching_ELSE_ENDIF(pContext, pContext->iCurCommandNum, &pCurCommand->iElseIndex, &pCurCommand->iEndIfIndex, &pFailString))
				{
					BuildScripting_Fail(pContext, "%s",
						pFailString);
					return;
				}
			}

			//once we know what else/endif are associated with this if, poke them to have that knowledge
			//also so that we always know when an else/endif is unmatched
		
			pCurCommand->iIfIndex = pContext->iCurCommandNum;
			if (pCurCommand->iElseIndex != -1)
			{
				pContext->commandList.ppCommands[pCurCommand->iElseIndex]->iIfIndex = pCurCommand->iIfIndex;
				pContext->commandList.ppCommands[pCurCommand->iElseIndex]->iElseIndex = pCurCommand->iElseIndex;
				pContext->commandList.ppCommands[pCurCommand->iElseIndex]->iEndIfIndex = pCurCommand->iEndIfIndex;
			}

			pContext->commandList.ppCommands[pCurCommand->iEndIfIndex]->iIfIndex = pCurCommand->iIfIndex;
			pContext->commandList.ppCommands[pCurCommand->iEndIfIndex]->iElseIndex = pCurCommand->iElseIndex;
			pContext->commandList.ppCommands[pCurCommand->iEndIfIndex]->iEndIfIndex = pCurCommand->iEndIfIndex;


			if (BuildScripting_IsExpressionStringTrue(pCurCommand->pScriptString_Use))
			{
				bsLogf(pContext, false, "Expression %s(raw: %s) is TRUE, entering block (%s(%d))",
					pCurCommand->pScriptString_Use, pCurCommand->pScriptString_Raw, pCurCommand->pFileName, pCurCommand->iLineNum);
				BuildScripting_NewCommand(pContext, ++pContext->iCurCommandNum);
			}
			else
			{
				if (pCurCommand->iElseIndex != -1)
				{
					pContext->iCurCommandNum = pCurCommand->iElseIndex+1;
					bsLogf(pContext, false, "Expression %s(raw: %s) is FALSE at %s(%d), skipping to ELSE at (%s(%d))",
						pCurCommand->pScriptString_Use, pCurCommand->pScriptString_Raw, 
						pCurCommand->pFileName, pCurCommand->iLineNum,
						pContext->commandList.ppCommands[pCurCommand->iElseIndex]->pFileName,
						pContext->commandList.ppCommands[pCurCommand->iElseIndex]->iLineNum);
					BuildScripting_NewCommand(pContext, pContext->iCurCommandNum);


				}
				else
				{
					pContext->iCurCommandNum = pCurCommand->iEndIfIndex + 1;
					bsLogf(pContext, false, "Expression %s(raw: %s) is FALSE at %s(%d), skipping to ENDIF at (%s(%d))",
						pCurCommand->pScriptString_Use, pCurCommand->pScriptString_Raw, 
						pCurCommand->pFileName, pCurCommand->iLineNum,
						pContext->commandList.ppCommands[pCurCommand->iEndIfIndex]->pFileName,
						pContext->commandList.ppCommands[pCurCommand->iEndIfIndex]->iLineNum);				
					BuildScripting_NewCommand(pContext, pContext->iCurCommandNum);
				}
			}
		}
		break;
		
	case BUILDSCRIPTCOMMAND_COMMENT:

		if (pCurCommand->pScriptString_Use)
		{
			estrCopy2(&pContext->pCurDisplayString, pCurCommand->pScriptString_Use);
		}
		else
		{
			estrPrintf(&pContext->pCurDisplayString, "empty COMMENT, %s(%d)", pCurCommand->pFileName, pCurCommand->iLineNum);
		}

		BuildScripting_AddStep(pContext->pCurDisplayString, pContext->pContextName, pCurCommand->iIncludeDepth + pContext->iCurForDepth, false, false);

		BuildScriptingAddComment(pCurCommand->pScriptString_Use);
		
		BuildScripting_NewCommand(pContext, ++pContext->iCurCommandNum);
		break;


	case BUILDSCRIPTCOMMAND_ENDIF:
		{
			if (pCurCommand->iIfIndex == -1)
			{
				BuildScripting_Fail(pContext, "nonmatched ENDIF at %s(%d)", 
					pCurCommand->pFileName, pCurCommand->iLineNum);
				return;
			}
			BuildScripting_NewCommand(pContext, ++pContext->iCurCommandNum);

		}
		break;


	case BUILDSCRIPTCOMMAND_ELSE:
		{
			if (pCurCommand->iIfIndex == -1)
			{
				BuildScripting_Fail(pContext, "nonmatched ELSE at %s(%d)", 
					pCurCommand->pFileName, pCurCommand->iLineNum);
				return;
			}
			pContext->iCurCommandNum = pCurCommand->iEndIfIndex + 1;
			BuildScripting_NewCommand(pContext, pContext->iCurCommandNum);

		}
		break;



	case BUILDSCRIPTCOMMAND_INCLUDE:
		BuildScripting_NewCommand(pContext, ++pContext->iCurCommandNum);
		break;

	case BUILDSCRIPTCOMMAND_REQUIREVARIABLES:
		{
			char **ppVariables = NULL;
			int i;

			DoVariableListSeparation(&ppVariables, pCurCommand->pScriptString_Use, false);

			for (i=0;i < eaSize(&ppVariables); i++)
			{
				if (!BuildScripting_VariableExists(pContext, ppVariables[i]))
				{
					BuildScripting_Fail(pContext, "Couldn't find required variable %s\n", ppVariables[i]);
				}
			}

			eaDestroyEx(&ppVariables, NULL);
			BuildScripting_NewCommand(pContext, ++pContext->iCurCommandNum);
		}
		break;

	case BUILDSCRIPTCOMMAND_SENDEMAIL:
		{
			char *pFirstBar, *pSecondBar;
			char **ppWhoToSendTo = NULL;
			FILE *pOutFile;
			static char *pFNameOnDisk = NULL;

			if (!pCurCommand->pScriptString_Use)
			{
				BuildScripting_Fail(pContext, "Missing script string for SENDEMAIL command");
				break;
			}

			pFirstBar = strchr(pCurCommand->pScriptString_Use, '|');

			if (!pFirstBar)
			{
				BuildScripting_Fail(pContext, "Badly formatted SENDEMAIL command");
				break;
			}

			pSecondBar = strchr(pFirstBar + 1, '|');

			if (!pSecondBar)
			{
				BuildScripting_Fail(pContext, "Badly formatted SENDEMAIL command");
				break;
			}

			*pFirstBar = *pSecondBar = 0;

			DivideString(pCurCommand->pScriptString_Use, ";", &ppWhoToSendTo, DIVIDESTRING_POSTPROCESS_ALLOCADD | DIVIDESTRING_POSTPROCESS_REMOVEUNIQUE);

			pFNameOnDisk = BuildScriptingGetLogFileLocation(pContext, "ScriptCommandEmail.txt");

			pOutFile = fopen(pFNameOnDisk, "wt");

			if (!pOutFile)
			{
				BuildScripting_Fail(pContext, "Couldn't open ScriptCommandEmail.txt for writing");
				eaDestroy(&ppWhoToSendTo);
				break;
			}

			fprintf(pOutFile, "%s", pSecondBar + 1);
			fclose(pOutFile);

			BuildScripting_SendEmail(false,&ppWhoToSendTo, pFNameOnDisk, pFirstBar + 1);

			*pFirstBar = *pSecondBar = '|';

			eaDestroy(&ppWhoToSendTo);

			BuildScripting_NewCommand(pContext, ++pContext->iCurCommandNum);
		}
		break;

	case BUILDSCRIPTCOMMAND_WAIT_UNTIL_EXPRESSION_TRUE:
		{
			static int iLastTimeChecked = 0;
			int iFrequency = pCurCommand->iScriptInt;
			int iCurTime = timeSecondsSince2000_ForceRecalc();
			if (!iFrequency)
			{
				iFrequency = 5;
			}

			BuildScripting_DoVariableReplacing_Internal(pContext, pCurCommand);


			if (iCurTime - iLastTimeChecked >= iFrequency)
			{

				iLastTimeChecked = iCurTime;

				if (BuildScripting_IsExpressionStringTrue(pCurCommand->pScriptString_Use))
				{
					bsLogf(pContext, false, "Expression \"%s\" is true... moving on to next command", pCurCommand->pScriptString_Use);
					BuildScripting_NewCommand(pContext, ++pContext->iCurCommandNum);
				}

				if (pCurCommand->pFailureExpression_Use && pCurCommand->pFailureExpression_Use[0] && BuildScripting_IsExpressionStringTrue(pCurCommand->pFailureExpression_Use))
				{
					bsLogf(pContext, false, "Expression \"%s\" is true... failing", pCurCommand->pFailureExpression_Raw);
					BuildScripting_Fail(pContext, "While waiting for \"%s\" to be true, \"%s\" was true... failing",
						pCurCommand->pScriptString_Raw, pCurCommand->pFailureExpression_Raw);
				}

			}
			else
			{
				if (BuildScripting_DEFINED_AND_TRUE(pContext, "NO_TIMEOUTS"))
				{
					if (timeSecondsSince2000() % 10 == 0)
					{
						SetConsoleTitle(L"NO_TIMEOUTS... this may run forever");
					}
				}
				else
				{
					if (pCurCommand->iFailureTime && (timeMsecsSince2000() - pContext->iTimeEnteredState) / 1000 > pCurCommand->iFailureTime)
					{
						if (pCurCommand->eSubType == BSCSUBTYPE_FAILURE_IS_NON_FATAL)
						{
							bsLogf(pContext, false, "Timed out while waiting for expression \"%s\". But this is NON_FATAL, so we move on",
								pCurCommand->pScriptString_Use);
							BuildScripting_NewCommand(pContext, ++pContext->iCurCommandNum);
						}
						else
						{
							BuildScripting_Fail(pContext, "Failure time overflow. More than %s while doing %s(%s)",
								GetPrettyDurationString(pCurCommand->iFailureTime),	
								pContext->pCurStepString, pContext->pCurDisplayString);
						}

						pContext->iFramesInState++;
						return;
					}

					if (pCurCommand->iFailureTime)
					{
						int iTimeUntilFailure = pCurCommand->iFailureTime - (int)((timeMsecsSince2000() - pContext->iTimeEnteredState) / 1000);
						if (iTimeUntilFailure % 10 == 0)
						{
							SetConsoleTitle_UTF8(STACK_SPRINTF("Time out in %d seconds", iTimeUntilFailure));
						}
					}
				}
			}
		}
		break;

	case BUILDSCRIPTCOMMAND_SETMULTIPLEVARS:
		bsLogf(pContext, false, "doing SETMULTIPLEVARS %s", pCurCommand->pScriptString_Use);

		if (!BuildScripting_SetMultipleVarsFromString(pContext, pCurCommand->pScriptString_Use, BuildScripting_GetCommentForCommand(pCurCommand)))
		{
			BuildScripting_Fail(pContext, "Couldn't parse SETMULTIPLEVARS string %s", pCurCommand->pScriptString_Use);
			return;
		}
		BuildScripting_NewCommand(pContext, ++pContext->iCurCommandNum);
		break;	

	case BUILDSCRIPTCOMMAND_SUBDIVIDE_STRING_INTO_VARS:
		bsLogf(pContext, false, "doing SUBDIVIDE_STRING_INTO_VARS %s", pCurCommand->pScriptString_Use);

		if (!BuildScripting_SubDivideStringIntoVars(pContext, pCurCommand->pScriptString_Use, BuildScripting_GetCommentForCommand(pCurCommand)))
		{
			BuildScripting_Fail(pContext, "Couldn't parse SUBDIVIDE_STRING_INTO_VARS string %s", pCurCommand->pScriptString_Use);
			return;
		}
		BuildScripting_NewCommand(pContext, ++pContext->iCurCommandNum);
		break;	

	case BUILDSCRIPTCOMMAND_EXPORT_VARS_TO_PARENT:
		bsLogf(pContext, false, "Going to attempt to export vars to parent: %s", pCurCommand->pScriptString_Use);
	

		if (!ExportVarsToParent(pContext, pCurCommand->pScriptString_Use, false))
		{
			//fail has already been called
		}
		else
		{
			BuildScripting_NewCommand(pContext, ++pContext->iCurCommandNum);
		};
		break;

	case BUILDSCRIPTCOMMAND_EXPORT_VARS_TO_ROOT:
		bsLogf(pContext, false, "Going to attempt to export vars to root: %s", pCurCommand->pScriptString_Use);
	

		if (!ExportVarsToParent(pContext, pCurCommand->pScriptString_Use, true))
		{
			//fail has already been called
		}
		else
		{
			BuildScripting_NewCommand(pContext, ++pContext->iCurCommandNum);
		};
		break;

	case BUILDSCRIPTCOMMAND_IMPORT_VARS_FROM_PARENT:
		bsLogf(pContext, false, "Going to attempt to import vars from parent: %s", pCurCommand->pScriptString_Use);
	

		if (!ImportVarsFromParent(pContext, pCurCommand->pScriptString_Use, false))
		{
			//fail has already been called
		}
		else
		{
			BuildScripting_NewCommand(pContext, ++pContext->iCurCommandNum);
		};
		break;

	case BUILDSCRIPTCOMMAND_IMPORT_VARS_FROM_ROOT:
		bsLogf(pContext, false, "Going to attempt to import vars from root: %s", pCurCommand->pScriptString_Use);
	

		if (!ImportVarsFromParent(pContext, pCurCommand->pScriptString_Use, true))
		{
			//fail has already been called
		}
		else
		{
			BuildScripting_NewCommand(pContext, ++pContext->iCurCommandNum);
		};
		break;
		

	case BUILDSCRIPTCOMMAND_BEGIN_CHILDREN:
		{
			ChildBeginEndDef **ppChildDefs = NULL;
			int iIndexOfMatchingEnd;
			char *pComment = NULL;

			if (!CheckForBeginChildrenLegality(pContext, pContext->iCurCommandNum, &iIndexOfMatchingEnd, &ppChildDefs))
			{
				eaDestroyStruct(&ppChildDefs, parse_ChildBeginEndDef);
				//if this fails, we've already failed the build.
				return;
			}

			FOR_EACH_IN_EARRAY(ppChildDefs, ChildBeginEndDef, pChildDef)
			{
				BuildScripting_DoVariableReplacing(pContext, pContext->commandList.ppCommands[pChildDef->iBeginChildIndex]);
				BuildScripting_CreateChildContext(pContext, false, pChildDef->iBeginChildIndex, pChildDef->iEndChildIndex, 
					pContext->commandList.ppCommands[pChildDef->iBeginChildIndex]->pScriptString_Use, pChildDef->eFlags);

				estrConcatf(&pComment, "%s%s", estrLength(&pComment) == 0 ? "" : ", ", pContext->commandList.ppCommands[pChildDef->iBeginChildIndex]->pScriptString_Use);
			}
			FOR_EACH_END;

			estrInsertf(&pComment, 0, "Child scripts: ");

			BuildScriptingAddComment(pComment);
			estrCopy2(&pContext->pCurDisplayString, pComment);

			estrDestroy(&pComment);


			BuildScripting_NewCommand(pContext, iIndexOfMatchingEnd);
			eaDestroyStruct(&ppChildDefs, parse_ChildBeginEndDef);

		}
		break;

		


	case BUILDSCRIPTCOMMAND_END_CHILDREN:
		{
			bool bAllChildrenComplete = true;

			if (!eaSize(&pContext->ppChildren))
			{
				BuildScripting_Fail(pContext, "Want to wait for child contexts, but there aren't any");
				return;
			}

			FOR_EACH_IN_EARRAY(pContext->ppChildren, BuildScriptingContext, pChildContext)
			{
				switch (pChildContext->eScriptState)
				{
				case BUILDSCRIPTSTATE_RUNNING:
					bAllChildrenComplete = false;
					BuildScripting_Tick(pChildContext);
					break;

				case BUILDSCRIPTSTATE_SUCCEEDED:
				case BUILDSCRIPTSTATE_SUCCEEDED_WITH_ERRORS:
					break;

				case BUILDSCRIPTSTATE_FAILED:
					if (!(pChildContext->eChildBehaviorFlags & FAILURE_IS_NONFATAL_FOR_PARENT))
					{
						BuildScripting_Fail(pContext, "Child %s has failed due to %s",
							pChildContext->pContextName, pChildContext->pFailureMessage);
						return;
					}
					break;
					

				default:
					BuildScripting_Fail(pContext, "Child %s of context %s is in state %s. This is not legal. Child contexts can only ever be in running, succeeded, succeededwerrs, or failed",
						pChildContext->pContextName, pContext->pContextName, StaticDefineInt_FastIntToString(enumBuildScriptStateEnum, pChildContext->eScriptState));
					return;
				}
			}
			FOR_EACH_END;

			if (bAllChildrenComplete)
			{
				FOR_EACH_IN_EARRAY(pContext->ppChildren, BuildScriptingContext, pChildContext)
				{
					BuildScripting_AddResultVariablesFromChildContext(pContext, pChildContext);

				}
				FOR_EACH_END;



				bsLogf(pContext, false, "All %d children have completed... cleaning them up and proceeding", eaSize(&pContext->ppChildren));

				eaDestroyStruct(&pContext->ppChildren, parse_BuildScriptingContext);
				BuildScripting_NewCommand(pContext, ++pContext->iCurCommandNum);
//				BuildScripting_AddStep("Children all complete", pContext->pContextName, pCurCommand->iIncludeDepth + pContext->iCurForDepth, false);
			}
		}
		break;

	case BUILDSCRIPTCOMMAND_BEGIN_CHILD:
		if (!pContext->pParent)
		{
			BuildScripting_Fail(pContext, "%s(%d): Encountered a BEGIN_CHILD, but we are a parent context",
				pCurCommand->pFileName_internal, pCurCommand->iLineNum);
		}
		BuildScripting_NewCommand(pContext, ++pContext->iCurCommandNum);
		break;
	case BUILDSCRIPTCOMMAND_END_CHILD:
		if (!pContext->pParent)
		{
			BuildScripting_Fail(pContext, "%s(%d): Encountered an END_CHILD, but we are a parent context",
				pCurCommand->pFileName_internal, pCurCommand->iLineNum);
		}
		BuildScripting_NewCommand(pContext, ++pContext->iCurCommandNum);
		break;

	case BUILDSCRIPTCOMMAND_WAIT_FOR_DETACHED_CHILDREN:
		if (BuildScripting_WaitForDetachedChildren(pContext, pCurCommand))
		{
			BuildScripting_NewCommand(pContext, ++pContext->iCurCommandNum);
		}
		break;

	case BUILDSCRIPTCOMMAND_BEGIN_DETACHED_CHILD:
		{
			int iEndIndex;

			if (!pCurCommand->pScriptString_Use)
			{
				BuildScripting_Fail(pContext, "Got BEGIN_DETACHED_CHILD with no child name");
				return;
			}

			if (BuildScripting_FindDetachedContext(pContext, pCurCommand->pScriptString_Use))
			{
				BuildScripting_Fail(pContext, "Can't BEGIN_DETACHED_CHILD, name %s already in use",
					pCurCommand->pScriptString_Use);
				return;
			}

			iEndIndex = BuildScripting_FindBalancedBlockEndIndex(pContext, pContext->iCurCommandNum);

			if (iEndIndex == -1)
			{
				//already called BuildSCripting_Fail
				return;
			}

			BuildScripting_CreateChildContext(pContext, true, pContext->iCurCommandNum + 1, iEndIndex - 1, 
				pCurCommand->pScriptString_Use, 
				(pCurCommand->eSubType == BSCSUBTYPE_FAILURE_IS_NON_FATAL ? FAILURE_IS_NONFATAL_FOR_PARENT : 0)
				| (pCurCommand->bNonInterruptible ? NON_INTERRUPTIBLE : 0));

			BuildScripting_NewCommand(pContext, iEndIndex + 1);
			break;

		}

	case BUILDSCRIPTCOMMAND_WAIT_FOR_VARIABLE_IN_ROOT:
		if (!estrLength(&pCurCommand->pScriptString_Use) || !estrLength(&pCurCommand->pDisplayString_Use))
		{
			BuildScripting_Fail(pContext, "WAIT_FOR_VARIABLE_IN_ROOT command (%s:%d) must have both ScriptString (containing variable name) and display string (waiting for foo so we can do bar) set",
				pCurCommand->pFileName_internal, pCurCommand->iLineNum);
			return;
		}

		if (pContext->iFramesInState == 0)
		{
			bool bDone = CheckVariableInRoot(pContext, pCurCommand->pScriptString_Use);
			if (bDone)
			{
				bsLogf(pContext, false, "Got to WAIT_FOR_VARIABLE_IN_ROOT %s, that was already set, immediately proceeding",
					pCurCommand->pScriptString_Use);
				BuildScripting_NewCommand(pContext, ++pContext->iCurCommandNum);
				break;
			}
			else
			{
				bsLogf(pContext, false, "Got to WAIT_FOR_VARIABLE_IN_ROOT %s, it is not set, beginning waiting",
					pCurCommand->pScriptString_Use);
				BuildScripting_AddStep(pContext->pCurDisplayString, pContext->pContextName, 
					pCurCommand->iIncludeDepth + pContext->iCurForDepth, false, false);
				break;
			}
		}
		else
		{
			bool bDone = CheckVariableInRoot(pContext, pCurCommand->pScriptString_Use);
			if (bDone)
			{
				bsLogf(pContext, false, "%s is now set, proceeding",
					pCurCommand->pScriptString_Use);
				BuildScripting_NewCommand(pContext, ++pContext->iCurCommandNum);
				break;
			}
		}
		break;

	case BUILDSCRIPTCOMMAND_GOTO:
		{
			int iNewCommandNum;
			BuildScriptCommand *pNewCommand;

			if (!pCurCommand->pScriptString_Use || StringIsAllWhiteSpace(pCurCommand->pScriptString_Use))
			{
				BuildScripting_Fail(pContext, "Can't try to find an empty comment for a GOTO\n");
				return;
			}

			bsLogf(pContext, false, "Want to goto %s... will try to find that command\n", pCurCommand->pScriptString_Use);
			iNewCommandNum = BuildScripting_FindCommandWithSpecificComment(pContext, pCurCommand->pScriptString_Use);
			if (iNewCommandNum == -1)
			{
				BuildScripting_Fail(pContext, "Wanted to goto %s... couldn't find it\n", pCurCommand->pScriptString_Use);
				return;
			}

			pNewCommand = pContext->commandList.ppCommands[iNewCommandNum];

			bsLogf(pContext, false, "Found our command... will go to %s(%d)\n", 
				pNewCommand->pFileName_internal, pNewCommand->iLineNum);

			BuildScripting_NewCommand(pContext, iNewCommandNum);
		}
		break;

	}

	pContext->iFramesInState++;
}

void AbortAndClose(BuildScriptingContext *pContext)
{
	//first find the root
	while (pContext->pParent)
	{
		pContext = pContext->pParent;
	}

	//StructDestroy the parent... that will destroy all children, and thus kill all system commands
	StructDestroy(parse_BuildScriptingContext, pContext);

	//done
	exit(0);

}

void BuildScripting_Tick(BuildScriptingContext *pContext)
{
	if (pContext->bAbortAndCloseInstantly)
	{
		AbortAndClose(pContext);
	}

	BuildScripting_PushCurExecutingContext(pContext);
	BuildScripting_Tick_Internal(pContext);
	BuildScripting_PopCurExecutingContext();
}

AUTO_EXPR_FUNC(buildScripting) ACMD_NAME(DEFINED);
bool Deprecated_DEFINED(const char *pVarName)
{
	assertmsg(0, "DEFINED is deprecated, use CHECK instead");
}


AUTO_EXPR_FUNC(buildScripting) ACMD_NAME(DEFINED_AND_TRUE);
bool Deprecated_DEFINED_AND_TRUE(const char *pVarName)
{
	assertmsg(0, "DEFINED_AND_TRUE is deprecated, use CHECK instead");
}



bool BuildScripting_DEFINED_AND_TRUE(BuildScriptingContext *pContext, const char *pVarName)
{
	int i;
	char *pNameToLookFor = NULL;
	char *pFound = NULL;
	estrPrintf(&pNameToLookFor, "$%s$", pVarName);


	for (i=0; i < eaSize(&pContext->variables.ppScriptingVariables); i++)
	{
		if (stricmp(pContext->variables.ppScriptingVariables[i]->pVarName, pNameToLookFor) == 0)
		{
			pFound = pContext->variables.ppScriptingVariables[i]->pVarValue;
			break;
		}
	}

	estrDestroy(&pNameToLookFor);
	if (!pFound)
	{
		return false;
	}

	if (stricmp(pFound, "0") == 0)
	{
		return false;
	}

	return true;
}


AUTO_EXPR_FUNC(buildScripting) ACMD_NAME(FILE_EXISTS);
bool BuildScripting_FILE_EXISTS(const char *pFileName)
{
	FILE *pFile = fopen(pFileName, "rt");


	if (pFile)
	{
		fclose(pFile);
		return true;
	}

	return false;
}

AUTO_EXPR_FUNC(buildScripting) ACMD_NAME(FILE_SIZE);
int BuildScripting_FILE_SIZE(const char *pFileName)
{
	return fileSize(pFileName);
}

AUTO_COMMAND;
char *GET_ENVIRONMENT_VARIABLE(const char *pVarName)
{
	static char outString[2048] = "";
	size_t outSize;

	getenv_s(&outSize, SAFESTR(outString), pVarName);

	return outString;
}

AUTO_EXPR_FUNC(buildScripting) ACMD_NAME(XBOX_FILE_SIZE);
int BuildScripting_XBOX_FILE_SIZE(const char *pFileName)
{

	return 0;
}


AUTO_EXPR_FUNC(buildScripting) ACMD_NAME(HOUR_OF_DAY);
int BuildScripting_HOUR_OF_DAY(void)
{
	SYSTEMTIME t;
	timerLocalSystemTimeFromSecondsSince2000(&t, timeSecondsSince2000_ForceRecalc());
	return t.wHour;
}

AUTO_COMMAND ACMD_NAME(REMOVE_SUBSTRINGS);
char *BuildScripting_RemoveSubstrings(char *pInString, char *pSubString)
{
	static char *pTemp = NULL;
	estrCopy2(&pTemp, pInString);
	estrReplaceOccurrences_CaseInsensitive(&pTemp, pSubString, "");
	return pTemp;
}

bool BuildScripting_IsExpressionStringTrue(char *pString)
{
	Expression *pExpr = exprCreateFromString(pString, NULL);
	static ExprContext *pContext = NULL;
	static ExprFuncTable* pFuncTable;
	MultiVal answer = {0};

	if (!pContext)
	{
		pContext = exprContextCreate();
		pFuncTable = exprContextCreateFunctionTable("BuildScripting");
		exprContextAddFuncsToTableByTag(pFuncTable, "util");
		exprContextAddFuncsToTableByTag(pFuncTable, "buildScripting");
		exprContextSetFuncTable(pContext, pFuncTable);
	}

	exprGenerate(pExpr, pContext);

	exprEvaluate(pExpr, pContext, &answer);

	exprDestroy(pExpr);

	return QuickGetInt(&answer);	
}

bool BuildScripting_IsExpressionStringTrue_ForInString(char *pString, BuildScriptingContext *pContext)
{
	return BuildScripting_IsExpressionStringTrue(pString);
}

AUTO_COMMAND;
char *GetCurGimmeDateString(void)
{
	return timeGetLocalGimmeStringFromSecondsSince2000(timeSecondsSince2000_ForceRecalc());
}

AUTO_COMMAND;
char *GetCurSVNRev(char *pFolderName)
{
	static char retString[16] = "";
	int iRevNum = SVN_GetRevNumOfFolders(pFolderName, NULL, NULL, 5);

	if (iRevNum)
	{
		sprintf(retString, "%d", iRevNum);
	}

	return retString;
}

AUTO_COMMAND;
char *GetSVNBranch(char *pFolderName)
{
	static char *pRetString = NULL;

	int iRevNum = SVN_GetRevNumOfFolders(pFolderName, NULL, &pRetString, 15);

	if (!iRevNum)
	{
		estrPrintf(&pRetString, "UNKNOWN");
	}

	return pRetString;
}

AUTO_COMMAND;
char *GetGimmeBranch(char *pFolderName)
{
	static char *pRetString = NULL;

	int iRevNum = Gimme_GetBranchNum(pFolderName);
	if (iRevNum == -1)
	{
		return "UNKNOWN";
	}
	else
	{
		estrPrintf(&pRetString, "%d", iRevNum);
		return pRetString;
	}
}

AUTO_COMMAND;
char *GetSVNNumberWhenBranchWasCreated(char *pBranchName)
{
	static char *pRetString = NULL;
	U32 iRetVal = SVN_GetSVNNumberWhenBranchWasCreated(pBranchName);

	if (!iRetVal)
	{
		estrPrintf(&pRetString, "UNKNOWN");
	}
	else
	{
		estrPrintf(&pRetString, "%d", iRetVal);
	}

	return pRetString;
}


AUTO_COMMAND;
char *GetScriptRunningTime(void)
{
	BuildScriptingContext *pContext = GetRootContext();

	if (pContext)
	{
		static char *pRetString = NULL;
		U32 iCurTime = timeSecondsSince2000_ForceRecalc();
		U32 iDuration = iCurTime - pContext->iScriptStartTime;

		int iSecs = iDuration % 60;
		int iMins = (iDuration / 60 ) % 60;
		int iHours = iDuration / (60 * 60);

		estrSetSize(&pRetString, 0);

		if (iHours)
		{
			estrConcatf(&pRetString, "%d Hour%s, ", iHours, iHours == 1 ? "" : "s");
		}

		if (iMins || iHours)
		{
			estrConcatf(&pRetString, "%d Minute%s, ", iMins, iMins == 1 ? "" : "s");
		}

		estrConcatf(&pRetString, "%d Second%s", iSecs, iSecs == 1 ? "" : "s");

		return pRetString;
	}
	else
	{
		return "(UNDEFINED)";
	}
}

AUTO_COMMAND;
char *GetTriviaFromFile(char *pFileName, char *pTriviaName)
{
	static char *pRetString = NULL;
	TriviaList* pList = triviaListCreateFromFile(pFileName);

	if (pList)
	{
		const char *pVal = triviaListGetValue(pList, pTriviaName);

		if (pVal && pVal[0])
		{
			estrCopy2(&pRetString, pVal);
		}
		else
		{
			estrCopy2(&pRetString, "ValueNotFound");
		}

		triviaListDestroy(&pList);
	}
	else
	{
		estrCopy2(&pRetString, "FileNotFound");
	}

	return pRetString;
}


AUTO_COMMAND;
char *GetStepDuration(void)
{
	BuildScriptingContext *pContext = GetRootContext();

	if (pContext)
	{
		static char *spOutBuffer = NULL;
		static U32 iLastStepTime = 0;
		U32 iCurTime = timeSecondsSince2000_ForceRecalc();
		int iCurDuration;

		if (pContext->iScriptStartTime > iLastStepTime)
		{
			iLastStepTime = pContext->iScriptStartTime;
		}

		iCurDuration = iCurTime - iLastStepTime;
		iLastStepTime = iCurTime;

		timeSecondsDurationToPrettyEString(iCurDuration, &spOutBuffer);

		return spOutBuffer;
	}

	return "(UNDEFINED)";
}





		
AUTO_COMMAND;
char *GetSVNCountFromFile(char *pFileName)
{
	char *pBuffer;
	int iSize;
	int iTotal = 0;
	char *pEOL;
	char *pReadHead;



	static char returnVal[16];

	pReadHead = pBuffer = fileAlloc(pFileName, &iSize);

	if (!pBuffer)
	{
		return "0";
	}

	//cheesy method for counting SVN modified files... every line of the file that contains a backslash
	//must be saying something happened to some file

	do
	{
		pEOL = strchr(pReadHead, '\n');

		if (pEOL)
		{
			*pEOL = 0;
		}

		if (strchr(pReadHead, '\\'))
		{
			iTotal++;
		}

		pReadHead = pEOL + 1;
	}
	while (pEOL);

	sprintf(returnVal, "%d", iTotal);

	free(pBuffer);

	return returnVal;
}





void BuildScripting_FailAfterDelay(BuildScriptingContext *pContext, int iNumSeconds)
{
	if (!BuildScripting_IsRunning(pContext))
	{
		return;
	}

	if (pContext->iBuildScriptingFailTime)
	{
		return;
	}

	if (!pContext->commandList.ppCommands)
	{
		return;
	}

	if (pContext->iCurCommandNum >= eaSize(&pContext->commandList.ppCommands))
	{
		return;
	}

	if (pContext->commandList.ppCommands[pContext->iCurCommandNum]->eCommand != BUILDSCRIPTCOMMAND_SYSTEM)
	{
		return;
	}

	pContext->iBuildScriptingFailTime = timeSecondsSince2000_ForceRecalc() + iNumSeconds;
	pContext->bFailAfterDelayCalled = true;


}



BuildScriptingContext *BuildScripting_CreateChildContext(BuildScriptingContext *pParent, bool bIsDetachedChild, int iFirstCommandIndex, int iLastCommandIndex, 
	char *pContextName, BuildScriptingContextChildBehaviorFlags eFlags)
{
	BuildScriptingContext *pChild = StructCreate(parse_BuildScriptingContext);
	int i;
	
	pChild->eChildBehaviorFlags = eFlags;
	pChild->pParent = pParent;

	estrCopy2(&pChild->pContextName, pContextName);
	estrMakeAllAlphaNumAndUnderscores(&pChild->pContextName);

	StructCopy(parse_BuildScriptingVariableList, &pParent->variables, &pChild->variables, 0, 0, 0);
	pChild->iNumStartingVariables = pParent->iNumStartingVariables;

	for (i = iFirstCommandIndex; i <= iLastCommandIndex; i++)
	{
		BuildScriptCommand *pClonedCommand = StructClone(parse_BuildScriptCommand, pParent->commandList.ppCommands[i]);
		pClonedCommand->iIncludeDepth++;
		eaPush(&pChild->commandList.ppCommands, pClonedCommand);
	}

	BuildScripting_FixupIncludeOnlyChildIncludeDepth(&pChild->commandList);
	BuildScripting_TerminateCommandList(&pChild->commandList);


	for (i = 0; i < eaSize(&pParent->ppDefaultDirectories); i++)
	{
		eaPush(&pChild->ppDefaultDirectories, strdup(pParent->ppDefaultDirectories[i]));
	}

	pChild->iInternalIncludeDepth = pParent->iInternalIncludeDepth + 1;
	pChild->bTestingOnly = pParent->bTestingOnly;
	pChild->eScriptState = BUILDSCRIPTSTATE_RUNNING;
	BuildScripting_NewCommand(pChild, 0);

	pChild->iScriptStartTime = timeSecondsSince2000();

	if (bIsDetachedChild)
	{
		eaPush(&pParent->ppDetachedChildren, pChild);

		bsLogf(pParent, true, "Created and started DETACHED child scripting context %s", pChild->pContextName);
		bsLogf(pChild, true, "Created as DETACHED child scripting context, parent is %s", pParent->pContextName);
	}
	else
	{
		eaPush(&pParent->ppChildren, pChild);

		bsLogf(pParent, true, "Created and started child scripting context %s", pChild->pContextName);
		bsLogf(pChild, true, "Created as child scripting context, parent is %s", pParent->pContextName);
	}

	BuildScripting_AddStep(pChild->pContextName, pParent->pContextName, pParent->iInternalIncludeDepth + pParent->iCurForDepth, true, bIsDetachedChild);

	return pChild;
}



//returns something that looks like this:
//59234 awerner mar 15 800 "this is the comment"
//61234 jesser mar 16 800 "this is another comment"
//
//pCheckinNums is comma-separated
AUTO_COMMAND;
char *GetDescriptiveSVNCheckinStrings(char *pRepository, ACMD_SENTENCE pCheckinNums)
{
	static char *pOutString = NULL;
	
	char **ppCheckinNumStrings = NULL;
	
	int i;

	CheckinInfo **ppCheckins = NULL;



	estrClear(&pOutString);

	DivideString(pCheckinNums, ",", &ppCheckinNumStrings, DIVIDESTRING_POSTPROCESS_STRIP_WHITESPACE);

	for (i=0; i < eaSize(&ppCheckinNumStrings); i++)
	{
		int iCheckinNum = atoi(ppCheckinNumStrings[i]);
		if (!iCheckinNum)
		{
			estrConcatf(&pOutString, "ERROR - unrecognized checkin ID \"%s\"\n", ppCheckinNumStrings[i]);
			continue;
		}

		eaDestroyStruct(&ppCheckins, parse_CheckinInfo);

		if (!SVN_GetCheckins(iCheckinNum-1, iCheckinNum, NULL, NULL, pRepository, &ppCheckins, 15, SVNGETCHECKINS_FLAG_REPLACE_DOLLARSIGNS))
		{
			estrConcatf(&pOutString, "ERROR - couldn't get description of checkin %d\n", iCheckinNum);
			continue;
		}

		if (eaSize(&ppCheckins) != 1)
		{
			estrConcatf(&pOutString, "ERROR - couldn't get description of checkin %d\n", iCheckinNum);
			continue;
		}

		estrTrimLeadingAndTrailingWhitespace(&ppCheckins[0]->checkinComment);

		estrConcatf(&pOutString, "%s\t%d\t%s\t\"%s\"\n",
			ppCheckins[0]->userName, ppCheckins[0]->iRevNum, timeGetLocalDateStringFromSecondsSince2000(ppCheckins[0]->iCheckinTimeSS2000),
			ppCheckins[0]->checkinComment);

	}

	eaDestroyStruct(&ppCheckins, parse_CheckinInfo);
	eaDestroyEx(&ppCheckinNumStrings, NULL);

	return pOutString;

}

	
AUTO_COMMAND;
char *GetLinkToLogFile(char *pLogFileName, ACMD_SENTENCE pLinkName)
{
	return BuildScriptingGetLinkToLogFile(BuildScripting_GetCurExecutingContext(), pLogFileName, pLinkName);
}

AUTO_COMMAND;
char *GetLogFileName(char *pShortName)
{
	return BuildScriptingGetLogFileLocation(BuildScripting_GetCurExecutingContext(), pShortName);
}

AUTO_COMMAND;
char *GetStatusFileName(char *pShortName)
{
	return BuildScriptingGetStatusFileLocation(pShortName);
}

AUTO_COMMAND;
char *GetLinkToLogDir(char *pLinkName)
{
	return BuildScriptingGetLinkToLogFile(BuildScripting_GetCurExecutingContext(), NULL, pLinkName);
}

AUTO_COMMAND;
char *GetLogDir(void)
{
	return BuildScriptingGetLogDir(BuildScripting_GetCurExecutingContext());
}


AUTO_COMMAND;
char *GetLongTermVariable(char *pVarName)
{
	NameValuePairList list = {0};
	char *pValue;
	static char *pRetVal = NULL;

	StructInit(parse_NameValuePairList, &list);
	ParserReadTextFile(BuildScriptingGetStatusFileLocation("LongTermVars.txt"), parse_NameValuePairList, &list, PARSER_NOINCLUDES);

	pValue = GetValueFromNameValuePairs(&list.ppPairs, pVarName);


	if (pValue)
	{
		estrCopy2(&pRetVal, pValue);
	}
	else
	{
		estrPrintf(&pRetVal, "0");
	}

	StructDeInit(parse_NameValuePairList, &list);



	return pRetVal;
}

AUTO_COMMAND;
void SetLongTermVariable(char *pVarName, char *pVarValue)
{
	NameValuePairList list = {0};
	static char *pRetVal = NULL;

	StructInit(parse_NameValuePairList, &list);
	ParserReadTextFile(BuildScriptingGetStatusFileLocation("LongTermVars.txt"), parse_NameValuePairList, &list, PARSER_NOINCLUDES);

	UpdateOrSetValueInNameValuePairList(&list.ppPairs, pVarName, pVarValue);

	ParserWriteTextFile(BuildScriptingGetStatusFileLocation("LongTermVars.txt"), parse_NameValuePairList, &list, 0, 0);

	StructDeInit(parse_NameValuePairList, &list);
}

void BuildScripting_SkipCurrentStep(BuildScriptingContext *pContext)
{
	if (BuildScripting_IsRunning(pContext))
	{
		pContext->bSkipCurrentStep = true;
	}
}

int BuildScripting_GetCurTimingDepth(BuildScriptingContext *pContext)
{
	if (!BuildScripting_IsRunning(pContext))
	{
		return 0;
	}

	if (pContext->iCurCommandNum >= eaSize(&pContext->commandList.ppCommands))
	{
		return 0;
	}

	assert(pContext->commandList.ppCommands);

	return pContext->commandList.ppCommands[pContext->iCurCommandNum]->iIncludeDepth + pContext->iCurForDepth;
}




AUTO_EXPR_FUNC(buildScripting) ACMD_NAME(XBOX_IS_RUNNING_EXE);
bool ControllerScripting_XBOXIsRunningEXE(char *pName)
{


	return false;
}


AUTO_EXPR_FUNC(buildScripting) ACMD_NAME(XBOX_IS_READY);
bool ControllerScripting_XBOXIsReady(void)
{
	return false;
}

AUTO_EXPR_FUNC(buildScripting) ACMD_NAME(GIMME_TIME_MINUTES_OLD);
int ControllerScripting_GimmeTimeMinutesOld(char *pGimmeTimeString)
{
	U32 iTime = timeGetSecondsSince2000FromLocalGimmeString(pGimmeTimeString);
	int iRetVal = (timeSecondsSince2000() - iTime) / 60;
	return  iRetVal;
}

AUTO_EXPR_FUNC(buildScripting) ACMD_NAME(STRING_IS_EMPTY);
bool ControllerScripting_StringIsEmpty(char *pStr)
{
	return StringIsAllWhiteSpace(pStr);
}


AUTO_COMMAND;
char *CheckSVNMergeOutputForConflicts(char *pFileName)
{
	static char *pRetString = NULL;

	char *pBuf;
	char **ppLines = NULL;
	int i;

	estrPrintf(&pRetString, " ");

	pBuf = fileAlloc(pFileName, NULL);

	if (!pBuf)
	{
		return pRetString;
	}

	DivideString(pBuf, "\n", &ppLines, DIVIDESTRING_POSTPROCESS_STRIP_WHITESPACE | DIVIDESTRING_POSTPROCESS_DONT_PUSH_EMPTY_STRINGS);

	free(pBuf);

	for (i=0; i < eaSize(&ppLines); i++)
	{
		if (strStartsWith(ppLines[i], "C "))
		{
			estrConcatf(&pRetString, "%s   ", ppLines[i]);
		}
	}

	eaDestroyEx(&ppLines, NULL);

	return pRetString;
}

AUTO_EXPR_FUNC(buildScripting) ACMD_NAME(SVN_MERGE_OUTPUT_HAS_ERRORS);
bool ControllerScripting_SvnMergeOutputHasErrors(char *pFileName)
{
	char *pErrorString = CheckSVNMergeOutputForConflicts(pFileName);
	if (StringIsAllWhiteSpace(pErrorString))
	{
		return false;
	}

	return true;
}

//returns true if the named file exists, and has a modification time more recent than the
//beginning of the current scripting run
//
//Note that this actually works on any file, not just .exes
AUTO_EXPR_FUNC(buildScripting) ACMD_NAME(EXE_WAS_BUILT_THIS_RUN);
bool ControllerScripting_ExeWasBuiltThisRun(const char *pExeName)
{
	BuildScriptingContext *pContext = GetRootContext();

	if (!pContext)
	{
		return false;
	}

	if (!fileExists(pExeName))
	{
		return false;
	}

	if (fileLastChangedSS2000(pExeName) >= RunStartTime())
	{
		return true;
	}

	return false;
}

//given a root dir and an extension and a comma-separated list of files,
//checks whether every one of those files exists somewhere in the directory hierarchy
//
//returns error string if files don't exist, "0" if they all do
//can't be an AUTO_EXPR because of ACMD_SENTENCE
AUTO_COMMAND;
char *FilesAllExistInDir(char *pRootDir, char *pExtension, ACMD_SENTENCE pFiles)
{
	char **ppInFiles = NULL;
	char **ppFilesOnDisk = NULL;
	int i, j;

	DivideString(pFiles, " ,", &ppInFiles, DIVIDESTRING_POSTPROCESS_DONT_PUSH_EMPTY_STRINGS | DIVIDESTRING_POSTPROCESS_REMOVEUNIQUE);

	ppFilesOnDisk = fileScanDirFolders(pRootDir, FSF_FILES | FSF_RETURNLOCALNAMES);

	for (i = 0; i < eaSize(&ppFilesOnDisk); i++)
	{
		char *pCurFileOnDisk = ppFilesOnDisk[i];
		if (strEndsWith(pCurFileOnDisk, pExtension))
		{
			char temp[CRYPTIC_MAX_PATH];
			getFileNameNoExt(temp, pCurFileOnDisk);

			j = eaFindString(&ppInFiles, temp);
			if (j != -1)
			{
				free(ppInFiles[j]);
				eaRemoveFast(&ppInFiles, j);
			}
		}
	}

	fileScanDirFreeNames(ppFilesOnDisk);

	if (eaSize(&ppInFiles))
	{
		static char *spRetString = NULL;
		estrPrintf(&spRetString, "Files that didn't exist: ");
		for (i = 0; i < eaSize(&ppInFiles); i++)
		{
			estrConcatf(&spRetString, "%s%s", i == 0 ? "" : ", ", ppInFiles[i]);
		}

		eaDestroyEx(&ppInFiles, NULL);
		return spRetString;
	}

	eaDestroyEx(&ppInFiles, NULL);
	return "0";
}

char *BuildScripting_GetMostRecentSystemOutputFilename(BuildScriptingContext *pContext)
{
	return pContext->pMostRecentSystemCommandOutputFileName;
}


char *BuildScripting_GetCommentForCommand(BuildScriptCommand *pCommand)
{
	static char *pRetVal = NULL;
	BuildScripting_GetCommandDescription(&pRetVal, pCommand);
	estrConcatf(&pRetVal, " (%s:%d)", 
		pCommand->pFileName_internal, pCommand->iLineNum);
	return pRetVal;
}

	
static InStringCommandsAllCBs sBuildScriptingInStringCommandCBs =
{
	BuildScripting_FindFile_ForInString,
	BuildScripting_FailWithStep_ForInString,
	BuildScripting_IsExpressionStringTrue_ForInString,
	FindAndReplaceCheckMacro_ForInString,
	NULL, 
};


void BuildScripting_AddAuxInStringCommand(InStringCommandsAuxCommand *pAuxCommand)
{
	eaPush(&sBuildScriptingInStringCommandCBs.ppAuxCommands, pAuxCommand);
}

void DEFAULT_LATELINK_BuildScripting_EndContext(char *pName, bool bFail)
{
}

void DEFAULT_LATELINK_BuildScriptingLogging(BuildScriptingContext *pContext, bool bIsImportant, char *pString)
{
}

void DEFAULT_LATELINK_BuildScriptingCommandLineOverride(BuildScriptCommand *pCommand, char **ppCmdLine)
{
}

void DEFAULT_LATELINK_BuildScriptingAppendErrorsToEstring(char **ppEstring, bool bHTML)
{
}

void DEFAULT_LATELINK_BuildScriptingFailureExtraStuff(BuildScriptingContext *pContext, char *pString, char *pTempString)
{
}

void DEFAULT_LATELINK_BuildScriptingNewStepExtraStuff(void)
{
}


char *DEFAULT_LATELINK_BuildScriptingGetLogFileLocation(BuildScriptingContext *pContext, const char *pInFileName)
{
	assertmsgf(0, "BuildScriptingGetLogFileLocation must be overridden");
}

char *DEFAULT_LATELINK_BuildScriptingGetStatusFileLocation(const char *pInFileName)
{
	assertmsgf(0, "BuildSCriptingGetStatusFileLocation must be overridden");
}

char *DEFAULT_LATELINK_BuildScriptingGetLinkToLogFile(BuildScriptingContext *pContext, const char *pFName, const char *pLinkName)
{
	assertmsgf(0, "BuildScriptingGetLinkToLogFile must be overridden");
}

void DEFAULT_LATELINK_BuildScriptingAddComment(const char *pComment)
{
}

char *DEFAULT_LATELINK_BuildScriptingGetLogDir(BuildScriptingContext *pContext)
{
	assertmsgf(0, "BuildScriptingGetLogDir must be overridden");
}


void DEFAULT_LATELINK_BuildScriptingVariableWasSet(BuildScriptingContext *pContext, const char *pVarName, const char *pValue)
{
}

void DEFAULT_LATELINK_BuildScripting_AddStartingVariables(BuildScriptingContext *pContext)
{

}

void DEFAULT_LATELINK_BuildScripting_SendEmail(bool bHTML, char ***pppRecipients, char *pEmailFileName, char *pSubject)
{

}

void DEFAULT_LATELINK_BuildScripting_ExtraErrorProcessing(BuildScriptingContext *pContext, char *pErrorMessage)
{
}

void DEFAULT_LATELINK_BuildScripting_NewCommand_DoExtraStuff(BuildScriptingContext *pContext)
{

}

bool DEFAULT_LATELINK_BuildScripting_DoAuxStuffBeforeLoadingScriptFiles(char **ppErrorString)
{
	return true;
}

int DEFAULT_LATELINK_BuildScripting_GetExtendedErrors(char *pExtendedErrorType, char *pBuildOutputFile, char **ppExtendedErrorString)
{
	return EXTENDEDERROR_FATAL;
}

void DEFAULT_LATELINK_BuildScripting_AddStep(char *pStepName, char *pParentContextName, int iDepth, bool bEstablishesNewContext, bool bDetachedContext)
{

}

char *BuildScripting_GetPathNameForChildContext(BuildScriptingContext *pContext)
{
	static char *spRetVal = NULL;

	if (!pContext->pParent)
	{
		return "";
	}

	estrCopy2(&spRetVal, pContext->pContextName);

	while (pContext->pParent->pParent)
	{
		pContext = pContext->pParent;
		estrInsertf(&spRetVal, 0, "%s/", pContext->pContextName);
	}

	return spRetVal;
}

BuildScriptingContext *BuildScripting_FindDescendantContextByName(BuildScriptingContext *pParentContext, char *pName)
{
	if (stricmp(pParentContext->pContextName, pName) == 0)
	{
		return pParentContext;
	}

	FOR_EACH_IN_EARRAY(pParentContext->ppChildren, BuildScriptingContext, pChild)
	{
		BuildScriptingContext *pRetVal = BuildScripting_FindDescendantContextByName(pChild, pName);
		if (pRetVal)
		{
			return pRetVal;
		}
	}
	FOR_EACH_END;


	FOR_EACH_IN_EARRAY(pParentContext->ppDetachedChildren, BuildScriptingContext, pChild)
	{
		BuildScriptingContext *pRetVal = BuildScripting_FindDescendantContextByName(pChild, pName);
		if (pRetVal)
		{
			return pRetVal;
		}
	}
	FOR_EACH_END;

	return NULL;
}

bool BuildScriptnig_ContextIsExpectingCBComments(BuildScriptingContext *pContext)
{
	BuildScriptCommand *pCurCommand;

	if (pContext->eScriptState != BUILDSCRIPTSTATE_RUNNING)
	{
		return false;
	}

	if (pContext->iCurCommandNum < 0 || pContext->iCurCommandNum >= eaSize(&pContext->commandList.ppCommands))
	{
		return false;
	}

	pCurCommand = pContext->commandList.ppCommands[pContext->iCurCommandNum];

	return pCurCommand->bIsExpectingCBComments;
}

BuildScriptingContext *BuildScripting_FindDescendantContextThatIsExpectingCBComments(BuildScriptingContext *pParentContext)
{
	if (BuildScriptnig_ContextIsExpectingCBComments(pParentContext))
	{
		return pParentContext;
	}

	FOR_EACH_IN_EARRAY(pParentContext->ppChildren, BuildScriptingContext, pChild)
	{
		BuildScriptingContext *pRetVal = BuildScripting_FindDescendantContextThatIsExpectingCBComments(pChild);
		if (pRetVal)
		{
			return pRetVal;
		}
	}
	FOR_EACH_END;


	FOR_EACH_IN_EARRAY(pParentContext->ppDetachedChildren, BuildScriptingContext, pChild)
	{
		BuildScriptingContext *pRetVal = BuildScripting_FindDescendantContextThatIsExpectingCBComments(pChild);
		if (pRetVal)
		{
			return pRetVal;
		}
	}
	FOR_EACH_END;

	return NULL;
}

char *BuildScripting_GetContextName(BuildScriptingContext *pContext)
{
	return pContext->pContextName;
}

void BuildScripting_AbortAndCloseInstantly(BuildScriptingContext *pContext)
{
	pContext->bAbortAndCloseInstantly = true;
}

void BuildScripting_CheckForTimeoutReset(BuildScriptingContext *pContext)
{
	BuildScriptCommand *pCurCommand;

	if (pContext->pParent)
	{
		BuildScripting_CheckForTimeoutReset(pContext->pParent);
	}

	if (pContext->eScriptState != BUILDSCRIPTSTATE_RUNNING)
	{
		return;
	}

	if (pContext->iCurCommandNum < 0 || pContext->iCurCommandNum >= eaSize(&pContext->commandList.ppCommands))
	{
		return;
	}

	pCurCommand = pContext->commandList.ppCommands[pContext->iCurCommandNum];

	if (pCurCommand->bResetTimeoutOnAnyUpdate)
	{
		pContext->iTimeEnteredState = timeMsecsSince2000();
	}
}

//given a string and a substring, finds the substring in the string. If it's there,
//returns a new string which is part of the main string AFTER the last occurrence of the substring
AUTO_COMMAND ACMD_NAME(TRUNCATE_STRING_AFTER_SUBSTR);
char *TruncateStringAfterSubstr(char *pInString, char *pSubStr)
{
	char *pFound;
	int iSubstrLen = (int)strlen(pSubStr);
	static char *spRetVal = NULL;

	pFound = strstri(pInString, pSubStr);

	if (!pFound)
	{
		estrCopy2(&spRetVal, pInString);
		return spRetVal;
	}

	while (1)
	{
		char *pFound2 = strstri(pFound + iSubstrLen, pSubStr);
		if (pFound2)
		{
			pFound = pFound2;
		}
		else
		{
			break;
		}
	}

	estrCopy2(&spRetVal, pFound + iSubstrLen);
	return spRetVal;
}

//given a string and a substring, finds the substring in the string. If it's there,
//returns a new string which is just the main string up to, but not including, the 
//first occurrence of the substr
AUTO_COMMAND ACMD_NAME(TRUNCATE_STRING_AT_SUBSTR);
char *TruncateStringAtSubstr(char *pInString, char *pSubStr)
{
	static char *spRetVal = NULL;
	char *pFound = strstri(pInString, pSubStr);

	if (pFound)
	{
		estrSetSize(&spRetVal, pFound - pInString);
		memcpy(spRetVal, pFound, pFound - pInString);
		return spRetVal;
	}
	else
	{
		estrCopy2(&spRetVal, pFound);
		return spRetVal;
	}
}

//given an input string that is a comma, or linebreak, separated list items, returns a string that
//is a comma-separated list of items, alphabetized, with all backslashes replaced with forward slashes
AUTO_COMMAND ACMD_NAME(NORMALIZE_LIST);
char *NormalizeList(char *pInString)
{
	char **ppItems = NULL;
	static char *spRetVal = NULL;
	int i;

	DivideString(pInString, ",\n\r", &ppItems, DIVIDESTRING_STANDARD);

	FOR_EACH_IN_EARRAY(ppItems, char, pItem)
	{
		forwardSlashes(pItem);
	}
	FOR_EACH_END;

	eaQSort(ppItems, strCmp);

	estrClear(&spRetVal);

	for (i = 0; i < eaSize(&ppItems); i++)
	{
		estrConcatf(&spRetVal, "%s%s", i == 0 ? "" : ", ", ppItems[i]);
	}

	eaDestroyEx(&ppItems, NULL);

	return spRetVal;
}

//given an input string that is a comma, or linebreak, separated list items, returns a string that
//is a comma-separated list of items, alphabetized, with all forward slashes replaced with backslashes
AUTO_COMMAND ACMD_NAME(NORMALIZE_LIST_BACKSLASHES);
char *NormalizeListBackslashes(char *pInString)
{
	char **ppItems = NULL;
	static char *spRetVal = NULL;
	int i;

	DivideString(pInString, ",\n\r", &ppItems, DIVIDESTRING_STANDARD);

	FOR_EACH_IN_EARRAY(ppItems, char, pItem)
	{
		backSlashes(pItem);
	}
	FOR_EACH_END;

	eaQSort(ppItems, strCmp);

	estrClear(&spRetVal);

	for (i = 0; i < eaSize(&ppItems); i++)
	{
		estrConcatf(&spRetVal, "%s%s", i == 0 ? "" : ", ", ppItems[i]);
	}

	eaDestroyEx(&ppItems, NULL);

	return spRetVal;
}


//given a comma-separated list of items, and a comma-separated list of substrings,
//removes all items from the main list any of which contains any of the substrings
AUTO_COMMAND ACMD_NAME(REMOVE_ITEMS_MATCHING_SUBSTRS);
char *RemoveItemsFromListMatchingSubstrings(char *pMainList, char *pSubstrings)
{
	char **ppItems = NULL;
	char **ppSubStrings = NULL;
	int i, j;
	static char *spRetVal = NULL;
	estrClear(&spRetVal);

	DivideString(pMainList, ",", &ppItems, DIVIDESTRING_STANDARD);
	DivideString(pSubstrings, ",", &ppSubStrings, DIVIDESTRING_STANDARD);

	for (i = 0; i < eaSize(&ppItems); i++)
	{
		bool bMatched = false;

		for (j = 0; j < eaSize(&ppSubStrings); j++)
		{
			if (strstri(ppItems[i], ppSubStrings[j]) )
			{
				bMatched = true;
				break;
			}
		}

		if (!bMatched)
		{
			estrConcatf(&spRetVal, "%s%s", estrLength(&spRetVal) == 0 ? "" : ", ", ppItems[i]);
		}
	}

	eaDestroyEx(&ppItems, NULL);
	eaDestroyEx(&ppSubStrings, NULL);

	return spRetVal;
}

//divides a string into words, looks for the word, if found, returns the next word, if not round, returns the fail word
AUTO_COMMAND ACMD_NAME(GET_WORD_AFTER_WORD);
char *GetNextWordAfterWord(char *pInString, char *pWord, char *pFailReturn)
{
	char **ppWords = NULL;
	static char *spRetVal = NULL;
	int i;

	estrCopy2(&spRetVal, pFailReturn);
	DivideString(pInString, " \n\r\t,", &ppWords, DIVIDESTRING_STANDARD);

	for (i = 0; i < eaSize(&ppWords) - 1; i++)
	{
		if (stricmp(ppWords[i], pWord) == 0)
		{
			estrCopy2(&spRetVal, ppWords[i+1]);
			break;
		}
	}

	eaDestroyEx(&ppWords, NULL);

	return spRetVal;
}

U32 DEFAULT_LATELINK_RunStartTime(void)
{
	if (spRootContext)
	{
		return spRootContext->iScriptStartTime;
	}

	return 0;

}
#include "autogen/buildscripting_h_ast.c"
#include "autogen/buildscripting_c_ast.c"
