#pragma once

#include "file.h"
#include "InStringCommands.h"

typedef struct NameValuePairList NameValuePairList;
typedef struct BuildScriptingContext BuildScriptingContext;

//some commands (particularly compile commands), when they fail, spit out a bunch of output, which a
//callback then processes and decides how to proceed
typedef enum enumExtendedErrorType
{
	EXTENDEDERROR_NOTANERROR, //pretend you succeeded
	EXTENDEDERROR_FATAL,

	EXTENDEDERROR_RETRY, //try this step again
} enumExtendedErrorType;

LATELINK;
int BuildScripting_GetExtendedErrors(char *pExtendedErrorType, char *pBuildOutputFile, char **ppExtendedErrorString);



AUTO_ENUM;
typedef enum enumBuildScriptCommand
{
	BUILDSCRIPTCOMMAND_WAIT,
	BUILDSCRIPTCOMMAND_SETVARIABLE,
	BUILDSCRIPTCOMMAND_SETVAR = BUILDSCRIPTCOMMAND_SETVARIABLE,
	BUILDSCRIPTCOMMAND_SET = BUILDSCRIPTCOMMAND_SETVARIABLE,

	BUILDSCRIPTCOMMAND_SET_NO_EXPAND,


//if subtype is IGNORERESULT, then don't fail if the file can't be opened
	BUILDSCRIPTCOMMAND_SETVARIABLESFROMFILE,
	
	
	
	BUILDSCRIPTCOMMAND_SYSTEM,
	BUILDSCRIPTCOMMAND_FOR,
	BUILDSCRIPTCOMMAND_ENDFOR,
	BUILDSCRIPTCOMMAND_INCLUDE,
	BUILDSCRIPTCOMMAND_REQUIREVARIABLES, 
	BUILDSCRIPTCOMMAND_EXECUTE_COMMAND, //executes ScriptString as a cmd parse command

	//scriptstring should look like "awerner;brogers|Build has completed|Here is the contents of a long email"
	BUILDSCRIPTCOMMAND_SENDEMAIL,

	//if the expression in ScriptString is true, abort (note that it does NOT use ifExpression)
	BUILDSCRIPTCOMMAND_ABORT_IF_EXPRESSION_TRUE,

	//waits until the expression in ScriptString is true
	BUILDSCRIPTCOMMAND_WAIT_UNTIL_EXPRESSION_TRUE,

	//if a particular expression is true, fail the script
	//
	//preferred usage is to have the expression in FailExpression, and an error string in ScriptString. For backwards
	//compatibility, if only ScriptString exists, it is the expression
	BUILDSCRIPTCOMMAND_FAIL_IF_EXPRESSION_TRUE, 

	//Looks at scriptstring. If it's all whitespace, then continue. Otherwise, fail
	//with scriptString as the error message
	BUILDSCRIPTCOMMAND_FAIL_IF_STRING_NONEMPTY,

	BUILDSCRIPTCOMMAND_IF,
	BUILDSCRIPTCOMMAND_ELSE,
	BUILDSCRIPTCOMMAND_ENDIF,

	//both of these accept an optional string which is a comment as to why failing or aborting is happening
	BUILDSCRIPTCOMMAND_ABORT,
	BUILDSCRIPTCOMMAND_FAIL,

	BUILDSCRIPTCOMMAND_COMMENT,
	BUILDSCRIPTCOMMAND_REM = BUILDSCRIPTCOMMAND_COMMENT,

	//scriptString shoudl look like "VARNAME1,VARNAME2,VARNAME3=val1,val2,val3", all space-or-comma-separated for now, might
	//get more sophisticated later
	BUILDSCRIPTCOMMAND_SETMULTIPLEVARS,

	//I'm in a child context, and have changed one or more script variables... I want to reflect their new values
	//back up into my parent's variable table. Accepts a comma-separated list of variables, fails if any one of them 
	//doesn't exist (you can do an IF(CHECK) to get around that if you want)
	BUILDSCRIPTCOMMAND_EXPORT_VARS_TO_PARENT,

	//same as EXPORT_VARS_TO_PARENT, but recurses all the way up to the root
	BUILDSCRIPTCOMMAND_EXPORT_VARS_TO_ROOT,

/*
	The first way to launch child contexts is with the BEGIN_CHILDREN command, used like this:

BEGIN_CHILDREN
	BEGIN_CHILD SVN_Stuff
		cmd
		cmd
		cmd
		cmd
		cmd
	END_CHILD

	BEGIN_CHILD Gimme_Stuff
		cmd
		cmd
		cmd
		cmd
	END_CHILD
END_CHILDREN

Execution of the parent context will hit the BEGIN_CHILDREN, then spawn one child context for each BEGIN_CHILD. These
will all execute in parallel. When they have all gotten to END_CHILD, if they all succeeded, then the parent will
resume execution after END_CHILDREN, otherwise it will fail
*/
	BUILDSCRIPTCOMMAND_BEGIN_CHILDREN,
	BUILDSCRIPTCOMMAND_END_CHILDREN,
	BUILDSCRIPTCOMMAND_BEGIN_CHILD,
	BUILDSCRIPTCOMMAND_END_CHILD,

	//wait until some specified detached children are complete. ScriptString should contain a comma-separated list of
	//child context names. If it's empty, that implies "all". Note that an instance of this command is 
	//implicitly added to the end of every loaded script so that every script always ends by waiting for all detached children
	BUILDSCRIPTCOMMAND_WAIT_FOR_DETACHED_CHILDREN,

	//the commands between this begin and end will be started in a "separate thread", which runs in parallel with the 
	//continued execution of the main script
	BUILDSCRIPTCOMMAND_BEGIN_DETACHED_CHILD,
	BUILDSCRIPTCOMMAND_END_DETACHED_CHILD,

	//used for synchronization of multiple parallel children... does nothing but check for a variable in the root context
	//every frame until it is set and non-zero/non-empty. Presumably used when there are two child contexts running and 
	//one needs to wait for the other one to have reached some point before it can proceed... so context A calls 
	//BUILDSCRIPTCOMMAND_EXPORT_VARS_TO_ROOT "ReachedX" and context B calls WAIT_FOR_VARIABLE_IN_ROOT "ReachedX"
	//
	//Must have a DisplayString set, which is used for the timing, and should be of the form "Waiting for compile to complete
	//so we can furgle the smoogie" or something like that.
	BUILDSCRIPTCOMMAND_WAIT_FOR_VARIABLE_IN_ROOT,


	//given a string and a separator string, chops the string up wherever the separator appears, and sticks the
	//result into some number of variables. If there are too many parts, it fails, if there are too few it 
	//sets the later variables to "". For instance,
	// SUBDIVIDE_STRING_INTO_VARS "DEMO_NAME, MAP_NAME, __IN__, HappyDemo__IN__FunPlace" will set
	//$DEMO_NAME$ to HappyDemo and $MAP_NAME$ to FunPlace. 
	BUILDSCRIPTCOMMAND_SUBDIVIDE_STRING_INTO_VARS,

	//takes in the precise string of a comment, as comments double as labels.
	//
	//Note that scriptString_Use is used for the command you're going FROM, but scriptString_Raw is used
	//for the command you're going TO. This can be used to set up a switch statement of sorts.
	BUILDSCRIPTCOMMAND_GOTO,

	//takes a comma-separated list of variables, finds the value in the parent or root (if any), sets
	//that value in the current context. If the variable isn't set at all in the parent/root, sets
	//it to "0"
	//
	//Note that IMPORT_VARS_FROM_ROOT recurses all the way down, so all sets the variable in all intermediate contexts
	BUILDSCRIPTCOMMAND_IMPORT_VARS_FROM_PARENT,
	BUILDSCRIPTCOMMAND_IMPORT_VARS_FROM_ROOT,

} enumBuildScriptCommand;

//mostly, command sub types only apply to system commands
//
//however, SETVARIABLESFROMFILE can have IGNORERESULT, which means that if the file doesn't exist, do nothing
AUTO_ENUM;
typedef enum enumBuildScriptCommandSubType
{
	BSCSUBTYPE_MUSTSUCCEED, //default... if this command returns non-zero, the entire script fails

	BSCSUBTYPE_IGNORERESULT, //the return value of this command is ignored

	BSCSUBTYPE_FAILURE_IS_NON_FATAL, //if this command fails, that counts as an "error", but not a failure
									//(this also works for WAIT_UNTIL_EXPRESSION_TRUE

	BSCSUBTYPE_CONTROLLER_STYLE, //this command returns -1 if it fails, -2 if it succeeds with errors
} enumBuildScriptCommandSubType;

//when including a file, can do any number of simple literal replacements before any textparsing happens
AUTO_STRUCT;
typedef struct BuildScriptingSimpleIncludeMacro
{
	char *pFrom; AST(STRUCTPARAM)
	char *pTo; AST(STRUCTPARAM)
} BuildScriptingSimpleIncludeMacro;


AUTO_STRUCT AST_IGNORE(SetsCBStringsByItself);
typedef struct BuildScriptCommand
{
	enumBuildScriptCommand eCommand; AST(STRUCTPARAM)

	enumBuildScriptCommandSubType eSubType;

	char *pScriptString_Raw; AST(NAME(ScriptString))//generic string used by various scripting commands for various things
	char *pScriptString_Use; AST(ESTRING) //the above, with variable replacing done

	char *pDisplayString_Raw; AST(NAME(DisplayString)) //string to display while this command is ongoing (only useful for commands that wait)
	char *pDisplayString_Use;  AST(ESTRING)//the above, with variable replacing done

	char *pWorkingDirectory_Raw; AST(NAME(WorkingDir)) //the working directory in which this (system) command should execute
	char *pWorkingDirectory_Use; AST(ESTRING)

	//only used during WAIT_FOR_EXPRESSION. If this expression is defined and true, then the script fails
	char *pFailureExpression_Raw; AST(NAME(FailureExpression))
	char *pFailureExpression_Use; AST(ESTRING)


	char *pIfExpression_Raw; AST(NAME(IfExpression, If)) //if this string is nonempty, then the command will only be executed if
													 //this expression is true. Only valid for SYSTEM and the various SETVARs
													 //For FOR commands, put the IfExpression in the FOR command and it will
													 //check only once. If false, the whole loop will be skipped
	char *pIfExpression_Use; AST(ESTRING)

	char *pVariableForSystemOutput; //if set, specifies the name of a variable (without $s). If this is a system command
									//its console output will be placed into this variable

	char *pVariableForEscapedSystemOutput; //like above, but calls estrAppendEscaped

	char *pVariableForSystemResult; //if set, specifies the name of a variable (without $s). If this is a system command,
									//the result value from the command will be placed into this variable (presumably
									//eSubType will be IGNORERESULT or something like that



	int iNumTries; AST(DEFAULT(1)) // for system commands, they will be attempted this many times before giving up

	int iScriptInt;
	float fScriptFloat;
	U32 iFailureTime; AST(DEF(120)) //if we stay in this step for this number of seconds, fail. If 0, wait forever

	int iForCount; NO_AST
	bool bLooping; NO_AST //set to true when looping back to this FOR command so that it knows whether to start from 0

	bool bFirstTime; NO_AST //for commands that take multiple frames to complete, is set to true whenever a command
							//is begun. (You have to manually set it back to false if you are using it.)

	int iRetryCount; NO_AST

	U32 bCompileErrors : 1; //if this is true, then this step is dumping compile output into c:\continuousBuilder\CB_CompileLog.txt
		//which should be parsed in order to properly report compile errors
		//this is somewhat deprecated, and is identical to pExtendedErrorType = "compile"


	U32 bNoScriptStringInErrorReport : 1; //if this is true, then when reporting an error because a system step failed,
		//don't include the script string (this is useful because compile steps in the CB sometimes do a build and
		//sometimes do a rebuild, but we don't want the error generated by the rebuild to appear to be a different
		//error than the error generated by the build, lest fake blaming go on

	U32 bSetVariableRaw : 1; //if true for SETVARIABLE command, then no whitespace trimming is done, so if you say "MYVAR =   3 ", you get "   3 "

	U32 bIsCrypticAppWithOwnConsole : 1; //if true, then use -forkPrintfsToFile to redirect output rather than normal 
		//redirection stuff

	U32 bBreak : 1; //for debugging only... if CB is attached to a debugger, break at the beginning of this step

	U32 bNonInterruptible : 1; //for commands which launch child contexts only... sets them as NON_INTERRUPTIBLE,
		//meaning they will complete even if the parent context fails

	U32 bIsExpectingCBComments : 1; //we suspect that this system commadn will create a netlink to the 
		//CB and send comments and substates on that link

	U32 bResetTimeoutOnAnyUpdate : 1; //any time we get a state change message or comment, reset the timeout for this step.
		//This is useful for MakeBins, where it could take 20 hours to run, but we also want to fairly promptly catch the case where
		//it's just hung up doing nothing (but make sure that there are regular messages being sent)

	const char *pFileName; NO_AST

	char *pFileName_internal; AST(CURRENTFILE)
	int iLineNum; AST(LINENUM)

	//only used for #include commands
	BuildScriptingSimpleIncludeMacro **ppSimpleMacros; AST(NAME(SimpleMacro))

	//all system commands are automatically redirected. Thus, if you want your command to have a > or >>, you have
	//to use these variables instead
	char *pOutputFile_Raw; AST(NAME(OutputFile))
	char *pOutputFile_Use; AST(ESTRING)
	char *pAppendFile_Raw; AST(NAME(AppendFile))
	char *pAppendFile_Use; AST(ESTRING)

	char outputFileName[CRYPTIC_MAX_PATH];
	char outputFileName_short[CRYPTIC_MAX_PATH];

	//for internal use only
	int iIfIndex; AST(DEF(-1))
	int iElseIndex; AST(DEF(-1))
	int iEndIfIndex; AST(DEF(-1))

	//internally, everything on the same line as the command gets read into this earray of strings, then
	//ripped apart and parsed and stuffed into ScriptString plus potentially setting other options
	char **ppInternalStrs; AST(STRUCTPARAM)

	int iIncludeDepth; NO_AST

	//in a FOR loop, the string is tokenized into this earray to avoid repeated string parsing work.
	char **ppBufferedFORTokens; 

	//if set, and if this system command fails, then call the extended errors callback to get further information on
	//how to proceed/report
	char *pExtendedErrorType; AST(ESTRING)
} BuildScriptCommand;



AUTO_ENUM;
typedef enum
{
	BUILDSCRIPTSTATE_INACTIVE,
	BUILDSCRIPTSTATE_LOADING,
	BUILDSCRIPTSTATE_RUNNING,
	BUILDSCRIPTSTATE_SUCCEEDED,
	BUILDSCRIPTSTATE_FAILED,
	BUILDSCRIPTSTATE_SUCCEEDED_WITH_ERRORS,

//the script decided to stop running, and neither succeeded nor failed. For instance, the production detected
//that there were no gimme or svn checkins since the last time it ran
	BUILDSCRIPTSTATE_ABORTED,
} enumBuildScriptState;

BuildScriptingContext *BuildScripting_CreateRootContext(void);

BuildScriptingContext *BuildScripting_FindDescendantContextByName(BuildScriptingContext *pParentContext, char *pName);
BuildScriptingContext *BuildScripting_FindDescendantContextThatIsExpectingCBComments(BuildScriptingContext *pParentContext);
char *BuildScripting_GetContextName(BuildScriptingContext *pContext);

void BuildScripting_Begin(BuildScriptingContext *pContext, char *pScriptFileName, int iStartingIncludeDepth, char **ppDefaultDirectories);
void BuildScripting_Tick(BuildScriptingContext *pContext);
enumBuildScriptState BuildScripting_GetState(BuildScriptingContext *pContext);
char *BuildScripting_GetCurStateString(BuildScriptingContext *pContext);
bool BuildScripting_CurrentCommandSetsCBStringsByItself(BuildScriptingContext *pContext);
void BuildScripting_SetTestingOnlyMode(BuildScriptingContext *pContext, bool bTestingOnly);
char *BuildScripting_GetCurFailureString(BuildScriptingContext *pContext);
char *BuildScripting_GetCurDisplayString(BuildScriptingContext *pContext);
bool BuildScripting_IsRunning(BuildScriptingContext *pContext);
bool BuildScripting_FindVarValue(BuildScriptingContext *pContext, char *pVarName, char **ppVarValue);

//note that this function does NOT further evaluate variables in the value, so it can return "$SOMEOTHERVARNAME$"
bool BuildScripting_FindRawVarValueAndComment(BuildScriptingContext *pContext, char *pVarName, char **ppVarValue, char **ppComment);

void BuildScripting_AddVariable(BuildScriptingContext *pContext, char *pVarName, const char *pVarValue_Orig, char *pComment);

/*Given a string that must be in one of the following formats, in priority order 
  (whitespace always trimmed around stuff1 and stuff2):
  DEFINED(varname) (true if $varname$ is defined)
  stuff1 STR_CONTAINS stuff2 (true if stuff2 is a case-insensitive substring of stuff1)
  stuff1 == stuff2 (true if they are case-insensitive equal) (spaces around == are required)
  
  asserts on parsing error
*/
bool BuildScripting_IsExpressionStringTrue(char *pString);

void BuildScripting_DumpAllVariables(BuildScriptingContext *pContext, char **ppEString, bool bComments);


void BuildScripting_FailAfterDelay(BuildScriptingContext *pContext, int iNumSeconds);

void BuildScripting_AddResettableStartingVariable(BuildScriptingContext *pContext, char *pVarName, char *pVarValue, char *pComment);
void BuildScripting_ResetResettableStartingVariables(BuildScriptingContext *pContext);

void BuildScripting_PutVariablesIntoList(BuildScriptingContext *pContext, NameValuePairList *pList);

void BuildScripting_TotallyDisable(BuildScriptingContext *pContext, bool bDisable);
bool BuildScripting_TotallyDisabled(BuildScriptingContext *pContext);

void BuildScripting_WriteCurrentVariablesToText( BuildScriptingContext *pContext, char* pFileName );

void BuildScripting_SkipCurrentStep();

char *BuildScripting_GetMostRecentSystemOutputFilename(BuildScriptingContext *pContext);

void BuildScripting_AddTimeBeforeFailure(BuildScriptingContext *pContext, int iSeconds);

int BuildScripting_GetCurTimingDepth(BuildScriptingContext *pContext);



void BuildScripting_ForceAbort(BuildScriptingContext *pContext);


void BuildScripting_AddAuxInStringCommand(InStringCommandsAuxCommand *pAuxCommand);

bool BuildScripting_ClearAndResetVariables(BuildScriptingContext *pContext);

char *GetTriviaFromFile(char *pFileName, char *pTriviaName);


//close the current script and all children instantly no matter what, then exit (so that a different thread
//can request the app close)
void BuildScripting_AbortAndCloseInstantly(BuildScriptingContext *pContext);

//in BuildSCriptingUI.c
bool BuildScripting_PickerCB(char *pInString, char **ppOutString, void *pUserData);

//for a child context, returns it's name. For a child of a child, returns parentname\childname, etc.
char *BuildScripting_GetPathNameForChildContext(BuildScriptingContext *pContext);

//a message of some sort arrived... check to see if any active context should have its timeout 
//reset 
void BuildScripting_CheckForTimeoutReset(BuildScriptingContext *pContext);

LATELINK;
void BuildScriptingLogging(BuildScriptingContext *pContext, bool bIsImportant, char *pString);

LATELINK;
void BuildScriptingCommandLineOverride(BuildScriptCommand *pCommand, char **ppCmdLine);

LATELINK;
void BuildScriptingAppendErrorsToEstring(char **ppEstring, bool bHTML);

//pString is the "real" failure. pTempString is additional info which may change from
//run to run, which should be presented to the user but should not be used
//when comparing an error from last run to an error from this run to last run
LATELINK;
void BuildScriptingFailureExtraStuff(BuildScriptingContext *pContext, char *pString, char *pTempString);

LATELINK;
void BuildScriptingNewStepExtraStuff(void);

LATELINK;
char *BuildScriptingGetLogFileLocation(BuildScriptingContext *pContext, const char *pInFileName);

LATELINK;
char *BuildScriptingGetLogDir(BuildScriptingContext *pContext);

LATELINK;
char *BuildScriptingGetStatusFileLocation(const char *pInFileName);

//if pFName is NULL, return a link to the log directory
LATELINK;
char *BuildScriptingGetLinkToLogFile(BuildScriptingContext *pContext, const char *pFName, const char *pLinkName);

LATELINK;
void BuildScriptingAddComment(const char *pComment);

LATELINK;
void BuildScripting_AddStep(char *pStepName, char *pParentContextName, int iDepth, bool bEstablishesNewContext, bool bDetachedContext);

LATELINK;
void BuildScripting_EndContext(char *pName, bool bFail);

LATELINK;
void BuildScriptingVariableWasSet(BuildScriptingContext *pContext, const char *pVarName, const char *pValue);

LATELINK;
void BuildScripting_AddStartingVariables(BuildScriptingContext *pContext);

LATELINK;
void BuildScripting_SendEmail(bool bHTML, char ***pppRecipients, char *pEmailFileName, char *pSubject);

LATELINK;
void BuildScripting_ExtraErrorProcessing(BuildScriptingContext *pContext, char *pErrorMessage);

LATELINK;
void BuildScripting_NewCommand_DoExtraStuff(BuildScriptingContext *pContext);

LATELINK;
bool BuildScripting_DoAuxStuffBeforeLoadingScriptFiles(char **ppErrorString);

LATELINK;
U32 RunStartTime(void);