#ifndef _CMDPARSE_H
#define _CMDPARSE_H
#pragma once
GCC_SYSTEM


#include "memcheck.h"
#include "timing.h"
#include "MultiVal.h"
#include "EString.h"
#include "namelist.h"
#include "GlobalTypeEnum.h"
#include "language/AppLocale.h"

#include "UtilitiesLibEnums.h"

//#include "net/net.h"


#define CMDMAXARGS 12 // Raise this if we need more arguments per command
#define CMDBUFLENGTH 1000 // The size of the command buffer to initially create, will expand if needed

typedef enum CommandServingFlags CommandServingFlags;

typedef enum CmdFlag
{
	CMDF_HIDEPRINT = 1,		// Don't show command in 'cmdlist', don't auto-complete
	CMDF_PRINTVARS = 1 << 1,      // If we have pointers to global vars, print those out if given 0 arguments. Don't call the callback
	CMDF_COMMANDLINE = 1 << 2,	//this command can be executed via the command line 
	CMDF_EARLYCOMMANDLINE = 1 << 3, //this command is in the earlyCommandLine cmdList
	CMDF_COMMANDLINEONLY = 1 << 4, //this command can ONLY be run from the command line
	CMDF_PASSENTITY = 1 << 5, //this command is has an Entity passed in as the first parameter (not the wrapper)
	CMDF_IGNOREPARSEERRORS = 1 << 6, //don't trigger an errorf if this command is called with a bad syntax (used for
		//weird cases like the auto-completion commands which might have weird things like "=" passed as arguments

	CMDF_CONTROLLERAUTOSETTING = 1 << 7, //this command is part of the AutoSettings system, on the controller 
		//which does special things with them

	CMDF_AUTOSETTING_NONCONTROLLER = 1 << 8, //this command is part of the AutoSettings system, not on the controller

	CMDF_ALL_PRODUCTS = 1 << 9, //ACMD_PRODUCTS(all) was set for this command. Currently only used for reporting,
		//as ACMD_PRODUCTS(all) means the same as not having ACMD_PRODUCTS at all

	CMDF_CACHE_AUTOCOMPLETE = 1 << 10, //only useful for commands on gameservers... this command has AUTO_COMPLETE arguments
		//which take a long time to generate and never change. So do so only once, using pool strings instead of strdups,
		//then save the struct for resending to other clients

	CMDF_ALLOW_JSONRPC = 1 << 11, //always allow this command to be called via JSONRPC. Redundant if -allowJSONRPC
		//is set
} CmdFlag;

typedef enum CmdOperation
{
	CMD_OPERATION_NONE = 0,
	CMD_OPERATION_AND,
	CMD_OPERATION_OR,
} CmdOperation;

typedef enum CmdArgFlag
{
	CMDAF_SENTENCE = 1,
	CMDAF_TEXTPARSER = 2,
	CMDAF_ALLOCATED = 4, // Did the cmdparse system allocate this?
	CMDAF_ESCAPEDSTRING = 8,
	CMDAF_HAS_DEFAULT = 16,
} CmdArgFlag;

typedef enum CmdContextOutputFlag
{
	CTXTOUTFLAG_NO_CLIENT_TO_SERVER_PROPOGATION = 1,
	CTXTOUTFLAG_NO_OUTPUT_ON_UNHANDLED = 1 << 1,
} CmdContextOutputFlag;

AUTO_ENUM;
typedef enum enumCmdContextHowCalled
{
	CMD_CONTEXT_HOWCALLED_UNSPECIFIED,
	CMD_CONTEXT_HOWCALLED_COMMANDLINE,
	CMD_CONTEXT_HOWCALLED_EARLY_COMMANDLINE,
	CMD_CONTEXT_HOWCALLED_TILDE_WINDOW,
	CMD_CONTEXT_HOWCALLED_CLIENTWRAPPER,
	CMD_CONTEXT_HOWCALLED_SERVERWRAPPER,
	CMD_CONTEXT_HOWCALLED_SERVER_MONITORING,
	CMD_CONTEXT_HOWCALLED_DIRECT_SERVER_MONITORING,
	CMD_CONTEXT_HOWCALLED_HTML_HEADER,
	CMD_CONTEXT_HOWCALLED_CONTROLLER_SCRIPTING,
	CMD_CONTEXT_HOWCALLED_INSTRING_COMMAND,
	CMD_CONTEXT_HOWCALLED_KEYBIND,
	CMD_CONTEXT_HOWCALLED_REPLAY,
	CMD_CONTEXT_HOWCALLED_CONFIGFILE,
	CMD_CONTEXT_HOWCALLED_CSR_COMMAND,
	CMD_CONTEXT_HOWCALLED_PLAYTEST_FILE,
	CMD_CONTEXT_HOWCALLED_UIGEN,
	CMD_CONTEXT_HOWCALLED_XMLRPC,
	CMD_CONTEXT_HOWCALLED_BUILDSCRIPTING,
	CMD_CONTEXT_HOWCALLED_LOGPARSER,
	CMD_CONTEXT_HOWCALLED_DDCONSOLE,
	CMD_CONTEXT_HOWCALLED_TRANSACTION,
	CMD_CONTEXT_HOWCALLED_CHATWINDOW,
	CMD_CONTEXT_HOWCALLED_CLUSTERCONTROLLER,
	CMD_CONTEXT_HOWCALLED_STATUSREPORTING,
	CMD_CONTEXT_HOWCALLED_JSONRPC,
	CMD_CONTEXT_HOWCALLED_MULTICOMMAND,
	CMD_CONTEXT_HOWCALLED_ONCEPERFRAME,

	//all "official" ways that auto settings can be set go in this block
	CMD_CONTEXT_HOWCALLED_FIRST_AUTOSETTING,
	CMD_CONTEXT_HOWCALLED_CONTROLLER_AUTO_SETTING_FILE,
	CMD_CONTEXT_HOWCALLED_CONTROLLER_AUTO_SETTING_SERVERMON,
	CMD_CONTEXT_HOWCALLED_CONTROLLER_AUTO_SETTING_NEWCMDINIT,
	CMD_CONTEXT_HOWCALLED_CONTROLLER_SETS_AUTO_SETTING,

	//someone called the command EmergencyAutoSettingOverride, used for unusual cases where
	//the normal auto_setting tech just plain won't work
	CMD_CONTEXT_HOWCALLED_EMERGENCY_AUTO_SETTING_OVERRIDE,

	CMD_CONTEXT_HOWCALLED_LAST_AUTOSETTING,
} enumCmdContextHowCalled;

extern StaticDefineInt enumCmdContextHowCalledEnum[];



// These macros are used for the old argument passing that needs explicit paramaters
#define CMDINT(x) {NULL, MULTI_INT, &x, sizeof(x), 0}
#define CMDFLOAT(x) {NULL, MULTI_FLOAT, &x, sizeof(x), 0}
#define CMDSTR(x) {NULL, MULTI_STRING, x, sizeof(x), 0}
#define CMDSENTENCE(x) {NULL, MULTI_STRING, x, sizeof(x), CMDAF_SENTENCE}

// These are for the new argument passing that creates the paramaters for you.
#define ARGMAT4 {NULL, MULTI_MAT4,0,0,CMDAF_ALLOCATED}
#define ARGVEC3 {NULL, MULTI_VEC3,0,0,CMDAF_ALLOCATED}
#define ARGVEC4 {NULL, MULTI_VEC4,0,0,CMDAF_ALLOCATED}
#define ARGQUAT {NULL, MULTI_QUAT,0,0,CMDAF_ALLOCATED}
#define ARGF32 {NULL, MULTI_FLOAT, 0, sizeof(F32), CMDAF_ALLOCATED}
#define ARGU32 {NULL, MULTI_INT,0, sizeof(U32), CMDAF_ALLOCATED}
#define ARGS32 {NULL, MULTI_INT,0, sizeof(S32), CMDAF_ALLOCATED}
#define ARGSTR {NULL, MULTI_STRING, 0, 0, CMDAF_ALLOCATED}
#define ARGSENTENCE {NULL, MULTI_STRING, 0, 0, CMDAF_SENTENCE|CMDAF_ALLOCATED}
#define ARGESCAPEDSTRING {NULL, MULTI_STRING, 0, 0, CMDAF_ESCAPEDSTRING|CMDAF_ALLOCATED}
#define ARGSTRUCT(str,def) {NULL, MULTI_NP_POINTER, def, sizeof(str), CMDAF_ALLOCATED|CMDAF_TEXTPARSER}
#define ARGSTRUCTNOSIZE(def) {NULL, MULTI_NP_POINTER, def, 0, CMDAF_ALLOCATED|CMDAF_TEXTPARSER}
#define ARGNONE {NULL, MULTI_NONE,0,0,0}

#define NOARGS {{0}}

typedef struct CmdServerContext CmdServerContext;
typedef struct TransactionCommand TransactionCommand;
typedef struct NameList NameList;

typedef struct PerfInfoStaticData PerfInfoStaticData;
typedef struct StashTableImp *StashTable;
typedef struct Cmd Cmd;
typedef struct CmdGroup CmdGroup;
typedef struct Entity Entity;
typedef struct XMLParam XMLParam;

//this is the information that is neede for a "slow" return to a command. This is used when the server monitor requests
//an action which can't complete instantly, but which should look from the server monitor's perspective like a command
typedef void SlowCmdReturnCallbackFunc(ContainerID iMCPID, int iRequestID, int iClientID, CommandServingFlags eFlags, char *pMessageString, void *pUserData);

typedef void (*CSRPetCallback)(GlobalType ePetType, ContainerID uiPetID);

typedef struct JsonRPCSlowReturnInfo
{
	char IDString[128];
	int iID;
	int eJsonVersion;
} JsonRPCSlowReturnInfo;

typedef struct CmdSlowReturnForServerMonitorInfo
{
	int iClientID;
	int iCommandRequestID;
	U32 iMCPID;
	bool bDoingSlowReturn;
	bool bForceLogReturn; //when the slow command returns, log its return value
	
	bool bDontDestroyXMLMethodCall; //the XML code uses this when it goes into the crazy 
		//decode-character-names code which sends requests around and stuff before it actually
		//does anything


	SlowCmdReturnCallbackFunc *pSlowReturnCB; 
	enumCmdContextHowCalled eHowCalled;
	void *pUserData; 
	CommandServingFlags eFlags;

	JsonRPCSlowReturnInfo jsonInfo;
} CmdSlowReturnForServerMonitorInfo;


//when executing a command with structs as arguments, we sometimes don't want to have to stringify
//the structs. In that case, we encode the structs in the command string as STRUCT(n), with 
//n being an index into a CmdParseStructList. Note that if the cmdContext->pStructList is non-NULL
//then all struct args MUST come from it
typedef struct CmdParseStructListEntry
{
	ParseTable *pTPI;
	void *pStruct;
} CmdParseStructListEntry;

typedef struct CmdParseStructList
{
	CmdParseStructListEntry **ppEntries;
} CmdParseStructList;


typedef struct CmdContext
{
	CmdOperation	op; // If we've determined we should do some logical operations, this is the one to do
	char			**output_msg; // An error or general output message, in estring form. return_val may end up pointing here
	Cmd 			*found_cmd; // Set to true if a command was found (but not necessarily executed, i.e. if it had the wrong number of parameters)
	bool			good_args; // Set to true if the correct number of arguments was provided
	bool			multi_line; // A single, possibly multiline command, ignore newlines when parsing sentences
	bool			banned_cmd; //an attempt was made to execute a banned command
	bool			cmd_unknown; // if the command was deemed unknown for any reason
	CmdContextFlag  flags; // flags from the enum list CmdContextFlag
	CmdContextOutputFlag outFlags; //things that are set by the command as it executes which tell the context what to do

	
	int				access_level; // The access level of the person executing this
	MultiVal		args[CMDMAXARGS]; // Where the arg and arg pointers are stored for new-style calls
	MultiVal		return_val; // The return-val for this command

	SlowRemoteCommandID iSlowCommandID;

	GlobalType		clientType;
	ContainerID		clientID;

	// union for ease of use
	union{
		void		*data; // Arbitrary data that may be needed by some handler functions
		CmdServerContext *svr_context;
		TransactionCommand *tr_command;
	};

	const char *commandString;
	void *commandData; // Additional arbitrary data
	CmdSlowReturnForServerMonitorInfo slowReturnInfo;

	Language language;

	enumCmdContextHowCalled eHowCalled;

	char **categories; // Pointer to earray of strings, following the format: " category " (leading and trailing spaces, all lowercase)
	                   // Easily created via cmdFilterAppendCategories()
	                   // Access Level 9 doesn't honor this filter, on purpose

	const char *pAuthNameAndIP; //set for commands called through server monitoring, possibly for other similar methods in the future.

	CmdParseStructList *pStructList;
} CmdContext;



typedef struct DataDesc
{
	char *pArgName;
	MultiValType		type;
	void	*ptr;
	int		data_size;
	int		flags;
	int		max_value; //used for ++

	//when there's a default value, eNameListType is NAMELISTTYPE_NONE, 
	//flags has CMDAF_HASDEFAULT and ppNameListData is either set to
	//the integer value, or points to the string value. (Int or string is determined by the arg type).
	//It can also be NULL to indicate a NULL struct pointer
	enumNameListType eNameListType;
	void **ppNameListData;
} DataDesc;



typedef void (*CmdHandler)(Cmd *c, CmdContext *o);

typedef struct Cmd
{
	int					access_level;
	const char			*name;
	const char			*pSourceFileName;
	int					iSourceLineNum;



	char *pProductNames; //comma-separated list of products for which this command "exists" (NULL means all). This is filled
		//in at structparser time, and used at one of two times, depending on when the product name is set.
		//When the product name is set, cmdParseHandleProductNameSet is called and goes throu all cmd lists, removing
		//commands that are in other products. Also, when commands are added to command list via cmdAddSingleCommandToList,
		//if the product is set and doesn't match, the command just isn't added.


	const char			*origin; //what project this command is defined in, ie "ServerLib"
	const char			*categories; //"categories" this command is in (if multiple, separated by spaces)
							//(always has a leading and trailing space, so you can strstr for " categoryname ")
		
	DataDesc			data[CMDMAXARGS];
	int					flags;
	const char			*comment;
	CmdHandler			handler;
	DataDesc			return_type;
	CmdHandler			error_handler;

	//in iNumRadArgs, a matrix counts as 12 args, etc. So this is NOT in any way
	//related to the number of actual meaningful fields in data[]
	U16					iNumReadArgs;

	//this is the number of meaningful fields in data[]
	U16					iNumLogicalArgs;

	U32			*pAutoSettingGlobalTypes;

	
	PERFINFO_TYPE*		perfInfo; // Make sure this is last.

		//if the access level is changed via controller_auto_setting, we store the
		//real access level here, so we can restore it if necessary
	int					original_access_level;
} Cmd;

typedef struct CmdList
{
	bool bInternalList; // If true, don't strip underscores

	StashTable	sCmdsByName;

	StashTable sCmdsNotInThisProduct; //we keep a stash table of these so we can list them
		//for debugging purposes

	// On the server we need all translated command names, and there
	// might be collisions for the shorter ones.
	StashTable	sCmdsByName_Translated[LANGUAGE_MAX];
} CmdList;

// These macros are for easy access to the arglist in CmdContext
// All of these macros assume the args are named what they are in CMDARGS


// Writes an int of the appropriate size in bytes to a destination. This is used by cmdparse
void MultiValWriteInt(MultiVal *val, void *dest, int size);
void MultiValWriteFloat(MultiVal *val, void *dest, int size);
void MultiValReadInt(MultiVal *val, void *dest, int size);
void MultiValReadFloat(MultiVal *val, void *dest, int size);



#define CMDARGS Cmd *cmd, CmdContext *cmd_context

#define STARTARG int argIndex = 0;assertmsg(cmd_context->good_args,"ReadArgs called on invalid args! Check for good_args, or don't use ALLOWBADARGS flag!");
#define GETARG(var,vartype)\
	switch(cmd->data[argIndex].type) {	\
	case MULTI_INT:MultiValWriteInt(&cmd_context->args[argIndex],&var,sizeof(var));break;\
	case MULTI_FLOAT:MultiValWriteFloat(&cmd_context->args[argIndex],&var,sizeof(var));break;\
	case MULTI_STRING: case MULTI_NP_POINTER: case MULTI_VEC3: case MULTI_VEC4: case MULTI_QUAT: case MULTI_MAT4: var = *((vartype *)&cmd_context->args[argIndex].ptr);break;\
	default: assertmsg(0,"Invalid argument type!"); var = 0; break;}argIndex++;

#define READARGS1(vartype1, var1) \
	vartype1 var1;STARTARG;\
	GETARG(var1,vartype1);

#define READARGS2( vartype1, var1, vartype2, var2) \
	vartype1 var1;vartype2 var2;STARTARG;\
	GETARG(var1,vartype1);GETARG(var2,vartype2);

#define READARGS3( vartype1, var1, vartype2, var2, vartype3, var3) \
	vartype1 var1;vartype2 var2;vartype3 var3;STARTARG;\
	GETARG(var1,vartype1);GETARG(var2,vartype2);\
	GETARG(var3,vartype3);

#define READARGS4( vartype1, var1, vartype2, var2, vartype3, var3, vartype4, var4) \
	vartype1 var1;vartype2 var2;vartype3 var3;vartype4 var4;STARTARG;\
	GETARG(var1,vartype1);GETARG(var2,vartype2);\
	GETARG(var3,vartype3);GETARG(var4,vartype4);

#define READARGS5( vartype1, var1, vartype2, var2, vartype3, var3, vartype4, var4, vartype5, var5) \
	vartype1 var1;vartype2 var2;vartype3 var3;vartype4 var4;vartype5 var5;STARTARG;\
	GETARG(var1,vartype1);GETARG(var2,vartype2);\
	GETARG(var3,vartype3);GETARG(var4,vartype4);\
	GETARG(var5,vartype5);

#define READARGS6( vartype1, var1, vartype2, var2, vartype3, var3, vartype4, var4, vartype5, var5,vartype6,var6) \
	vartype1 var1;vartype2 var2;vartype3 var3;vartype4 var4;vartype5 var5;vartype6 var6;STARTARG;\
	GETARG(var1,vartype1);GETARG(var2,vartype2);\
	GETARG(var3,vartype3);GETARG(var4,vartype4);\
	GETARG(var5,vartype5);GETARG(var6,vartype6);

#define READARGS7( vartype1, var1, vartype2, var2, vartype3, var3, vartype4, var4, vartype5, var5,vartype6,var6,vartype7,var7) \
	vartype1 var1;vartype2 var2;vartype3 var3;vartype4 var4;vartype5 var5;vartype6 var6;vartype7 var7;STARTARG;\
	GETARG(var1,vartype1);GETARG(var2,vartype2);\
	GETARG(var3,vartype3);GETARG(var4,vartype4);\
	GETARG(var5,vartype5);GETARG(var6,vartype6);\
	GETARG(var7,vartype7);

#define READARGS8( vartype1, var1, vartype2, var2, vartype3, var3, vartype4, var4, vartype5, var5,vartype6,var6,vartype7,var7,vartype8,var8) \
	vartype1 var1;vartype2 var2;vartype3 var3;vartype4 var4;vartype5 var5;vartype6 var6;vartype7 var7;vartype8 var8;STARTARG;\
	GETARG(var1,vartype1);GETARG(var2,vartype2);\
	GETARG(var3,vartype3);GETARG(var4,vartype4);\
	GETARG(var5,vartype5);GETARG(var6,vartype6);\
	GETARG(var7,vartype7);GETARG(var8,vartype8);

#define READARGS9( vartype1, var1, vartype2, var2, vartype3, var3, vartype4, var4, vartype5, var5,vartype6,var6,vartype7,var7,vartype8,var8,vartype9,var9) \
	vartype1 var1;vartype2 var2;vartype3 var3;vartype4 var4;vartype5 var5;vartype6 var6;vartype7 var7;vartype8 var8;vartype9 var9;STARTARG;\
	GETARG(var1,vartype1);GETARG(var2,vartype2);\
	GETARG(var3,vartype3);GETARG(var4,vartype4);\
	GETARG(var5,vartype5);GETARG(var6,vartype6);\
	GETARG(var7,vartype7);GETARG(var8,vartype8);GETARG(var9,vartype9);

#define READARGS10( vartype1, var1, vartype2, var2, vartype3, var3, vartype4, var4, vartype5, var5,vartype6,var6,vartype7,var7,vartype8,var8,vartype9,var9,vartype10,var10) \
	vartype1 var1;vartype2 var2;vartype3 var3;vartype4 var4;vartype5 var5;vartype6 var6;vartype7 var7;vartype8 var8;vartype9 var9;vartype10 var10;STARTARG;\
	GETARG(var1,vartype1);GETARG(var2,vartype2);\
	GETARG(var3,vartype3);GETARG(var4,vartype4);\
	GETARG(var5,vartype5);GETARG(var6,vartype6);\
	GETARG(var7,vartype7);GETARG(var8,vartype8);GETARG(var9,vartype9);GETARG(var10,vartype10);

#define READARGS11( vartype1, var1, vartype2, var2, vartype3, var3, vartype4, var4, vartype5, var5,vartype6,var6,vartype7,var7,vartype8,var8,vartype9,var9,vartype10,var10,vartype11,var11) \
	vartype1 var1;vartype2 var2;vartype3 var3;vartype4 var4;vartype5 var5;vartype6 var6;vartype7 var7;vartype8 var8;vartype9 var9;vartype10 var10;vartype11 var11;STARTARG;\
	GETARG(var1,vartype1);GETARG(var2,vartype2);\
	GETARG(var3,vartype3);GETARG(var4,vartype4);\
	GETARG(var5,vartype5);GETARG(var6,vartype6);\
	GETARG(var7,vartype7);GETARG(var8,vartype8);GETARG(var9,vartype9);GETARG(var10,vartype10);GETARG(var11,vartype11);

#define READARGS12( vartype1, var1, vartype2, var2, vartype3, var3, vartype4, var4, vartype5, var5,vartype6,var6,vartype7,var7,vartype8,var8,vartype9,var9,vartype10,var10,vartype11,var11,vartype12,var12) \
	vartype1 var1;vartype2 var2;vartype3 var3;vartype4 var4;vartype5 var5;vartype6 var6;vartype7 var7;vartype8 var8;vartype9 var9;vartype10 var10;vartype11 var11;vartype12 var12;STARTARG;\
	GETARG(var1,vartype1);GETARG(var2,vartype2);\
	GETARG(var3,vartype3);GETARG(var4,vartype4);\
	GETARG(var5,vartype5);GETARG(var6,vartype6);\
	GETARG(var7,vartype7);GETARG(var8,vartype8);GETARG(var9,vartype9);GETARG(var10,vartype10);GETARG(var11,vartype11);GETARG(var12,vartype12);

int cmdFilterAppendCategories(const char *filter, char ***eArrayOutput); // Breaks space-delimited string of categories into a CmdContext-friendly earray of estrings; returns number of categories appended

//given a context, returns a debugging string that describes how this command is being called, ie "from the command line" 
//or "from the ~ window"
const char *GetContextHowString(CmdContext *pContext);

// Initializes and cleans up output variable on a CmdContext
#define InitCmdOutput(context, str) context.output_msg = &str;estrStackCreate(context.output_msg);
#define CleanupCmdOutput(context) estrDestroy(context.output_msg);

// Prints command as a string (with parameters)
void cmdPrintCommmandString(Cmd *cmd,char *buf, int buflen);
// Prints usage information for a command
void cmdPrintUsage(Cmd *cmd, char *buf, int buf_size);

// Cleans up the result of a cmdRead call, this should be called after the case callback
void cmdCleanup(Cmd *cmd, CmdContext* output);

// Initializes and error checks a command
void cmdInit(Cmd *cmd);

// Dispatches a command to the appropriate handler
int cmdParseAndExecute(CmdList *cmdlist, const char* cmdstr, CmdContext *context );

// Cleans up a CmdContext, so it can be reused. The outputmsg estring is still valid
void cmdContextReset(CmdContext *context);

// Searches a CmdList and finds the corresponding command
Cmd *cmdListFind(CmdList *cmdlist, const char *fullname);

//same as above, but only finds commands appropriate for the given context
Cmd *cmdListFindWithContext(CmdList *cmdlist, const char *fullname, CmdContext *context);

//special version of parseAndExecute. The cmd string must have just a command name, no args. The args
//are already put into an earray of multivals.
int cmdExecuteWithMultiVals(CmdList *cmdlist, const char *cmdName, CmdContext *context, MultiVal ***pppArgs);

// Checks to see if this command could be executed, but don't actually do it.
int cmdCheckSyntax(CmdList *cmdlist, const char* cmdstr, CmdContext *context );

// Executes a command, by directly passing through MultiVals
// DEPRECATED do not use
int cmdDirectExecute(Cmd *cmd, CmdContext *cmd_context, MultiVal **args);

// Adds a single command to a global command list
void cmdAddSingleCmdToList(CmdList *cmdList, Cmd *cmd);

// Adds a set of commands to a global command list
void cmdAddCmdArrayToList(CmdList *cmdList, Cmd cmds[]);

//if iOverrideAccessLevel passed in is -1, then use whatever you would normally use
typedef int (*CmdParseFunc)(const char *cmd, char **ppEString, CmdContextFlag iCmdContextFlags, int iOverrideAccessLevel, enumCmdContextHowCalled eHow, CmdParseStructList *pStructs);

//special case to check whether a given command, that could not be executed, actually does exist but has had its access level 
//overridden by the AccessLevelOverrides AUTO_SETTING, and would normally be executable at the given AL
bool cmdExistsButIsAccessLevelOverridden(CmdList *cmd_list, char *pCommandName /*including args*/, int iAccessLevel, Language language);

// Set the command parse function to use for globCmdParse
void cmdSetGlobalCmdParseFunc(CmdParseFunc func);

// Function pointer used to call a command
extern CmdParseFunc globCmdParseAndReturn;

#define globCmdParseAndReturnWithFlagsAndOverrideAccessLevelAndStructs(str, ppRetString, iCmdContextFlags, iOverrideAccessLevel, eHow, pStructs) globCmdParseAndReturn(str, ppRetString, iCmdContextFlags, iOverrideAccessLevel, eHow, pStructs)
#define globCmdParseAndReturnWithFlagsAndOverrideAccessLevel(str, ppRetString, iCmdContextFlags, iOverrideAccessLevel, eHow) globCmdParseAndReturn(str, ppRetString, iCmdContextFlags, iOverrideAccessLevel, eHow, NULL)
#define globCmdParseAndReturnWithFlags(str, ppRetString, iCmdContextFlags, eHow) globCmdParseAndReturn(str, ppRetString, iCmdContextFlags, -1, eHow, NULL)
#define cmdParseAndReturn(cmd, ppRetString, eHow) globCmdParseAndReturn(cmd, ppRetString, 0, -1, eHow, NULL)
#define globCmdParseSpecifyHow(str, eHow) globCmdParseAndReturn(str, NULL, 0, -1, eHow, NULL)
#define globCmdParse(str) globCmdParseAndReturn(str, NULL, 0, -1, CMD_CONTEXT_HOWCALLED_UNSPECIFIED, NULL)
int globCmdParseAndStripWhitespace(enumCmdContextHowCalled eHow, const char *str);
int globCmdParsefAndStripWhitespace(enumCmdContextHowCalled eHow, const char *fmt, ...);



// Add Cmds to the global command list
extern CmdList gGlobalCmdList;
extern CmdList gEarlyCmdList;
int globCmdParsef(FORMAT_STR const char *fmt, ...);
#define globCmdParsef(fmt, ...) globCmdParsef(FORMAT_STRING_CHECKED(fmt), __VA_ARGS__)

// No global handling of private commands, the app/library must have their own
extern CmdList gPrivateCmdList;
extern CmdList gGatewayCmdList;

// Prints out a command list
typedef void (*CmdPrintCB)(char *string, void *userData);

void cmdPrintList(CmdList *cmdList,int access_level,char *match_str_src,
				  int print_header,
				  CmdPrintCB printCB,
				  Language language, void *userData);

// Parse the command line, and run the appropriate commands
#define cmdParseCommandLine(argc, argv) cmdParseCommandLine_internal(argc, argv, false)
void cmdParseCommandLine_internal(int argc,char **argv, bool bUseEarlyList);

void cmdParsePrintCommandLine(bool should_print);

//used only during auto-generated initialization
void cmdAddAutoSettingGlobalType(Cmd *pCmd, GlobalType eType);


//sends a command from the server to the client, or does nothing if this is not a game server currently running
//(this is in UtilitiesLib so that things like EntityLib can set up server and client command wrappers and call
//them and have everything link)
void cmdSendCmdServerToClient(Entity *pEntity, const char *pCmd, bool bPrivate, CmdContextFlag iFlags, enumCmdContextHowCalled eHow, bool bFast, CmdParseStructList *pStructs);

//same as above in other direction
void cmdSendCmdClientToServer(const char *pCmd, bool bPrivate, CmdContextFlag iFlags, enumCmdContextHowCalled eHow, CmdParseStructList *pStructs);

//callbacks so the above two two functions can be set up to work only on client/server
typedef void cmdSendCmdServerToClientCB(Entity *pEntity, const char *pCmd, bool bPrivate, CmdContextFlag iFlags, enumCmdContextHowCalled eHow, bool bFast, CmdParseStructList *pStructs);
typedef void cmdSendCmdClientToServerCB(const char *pCmd, bool bPrivate, CmdContextFlag iFlags, enumCmdContextHowCalled eHow, CmdParseStructList *pStructs);
void cmdSetServerToClientCB(cmdSendCmdServerToClientCB *pCB);
void cmdSetClientToServerCB(cmdSendCmdClientToServerCB *pCB);

typedef void cmdSendCmdGenericServerToClientCB(U32 uID, const char *pCmd, bool bPrivate, CmdContextFlag iFlags, enumCmdContextHowCalled eHow, CmdParseStructList *pStructs);
typedef void cmdSendCmdGenericToServerCB(GlobalType eServerType, const char *pCmd, bool bPrivate, CmdContextFlag iFlags, enumCmdContextHowCalled eHow, CmdParseStructList *pStructs);
void cmdSetGenericServerToClientCB(cmdSendCmdGenericServerToClientCB *pCB);
void cmdSetGenericToServerCB(cmdSendCmdGenericToServerCB *pCB);

// Generic command-sending functions for direct server-client communication
// Server-to-client takes a generic ID that should be interpreted by the server to retrieve the link
void cmdSendCmdGenericServerToClient(U32 uID, const char *pCmd, bool bPrivate, CmdContextFlag iFlags, enumCmdContextHowCalled eHow, CmdParseStructList *pStructs);
// Client-to-server takes a server type that should be referenced by the client to retrieve the link
void cmdSendCmdGenericToServer(GlobalType eServerType, const char *pCmd, bool bPrivate, CmdContextFlag iFlags, enumCmdContextHowCalled eHow, CmdParseStructList *pStructs);

//given a full command string, returns the access level of the command that string begins with, or -1
int cmdGetAccessLevelFromFullString(CmdList *pList, const char *pCmdString);

// Destroys sourceStr. Returns the next line of command followed by added NULL. sourceStr set to after line
char *cmdReadNextLine(char **sourceStr);

// Leaves sourceStr intact, returns flag to use original string if there are not separators.
S32 cmdReadNextLineConst(const char **sourceStr, char** estrCmdOut, const char** cmdOut);

// Debug: Check if we've been running non-accesslevel 0 commands
const char *cmdParseHasRanAccessLevelCmd(bool resetFlag, SA_PRE_OP_FREE SA_POST_OP_VALID AccessLevel *accessLevel);
void cmdParseNotifyRanAccessLevelCmd(const char *cmdstr, AccessLevel accesslevel);
typedef void (*AccessLevelCmdCallback)(const char *cmdstr, AccessLevel accesslevel);
void cmdParserSetAccessLevelCmdCallback(AccessLevelCmdCallback callback);

void cmdParseOncePerFrame(void);

//NameLists for command autocompletion
extern NameList *pGlobCmdListNames;
extern NameList *pAddedCmdNames;
extern NameList *pMRUCmdNameList;
extern NameList *pAllCmdNamesForAutoComplete;

//given a partially completed command string, returns what NameList, if any, to use to autocomplete the currently-being-typed
//argument
NameList *cmdGetNameListFromPartialCommandString(char *pCommandString);
enumNameListType cmdGetNameListTypeFromPartialCommandString(char *pCommandString);

//the structure that the server uses to send argument auto-completion namelists to the client
AUTO_STRUCT;
typedef struct NamesForAutoCompletion_SingleArg
{
	int iArgIndex;
	int eNameListType;
	char **ppNames;
} NamesForAutoCompletion_SingleArg;

AUTO_STRUCT;
typedef struct NamesForAutoCompletion
{
	char *pCmdName;
	NamesForAutoCompletion_SingleArg **ppNamesForArgs;
	bool bUpdateClientDataPerRequest;
} NamesForAutoCompletion;

//a callback which CmdParse uses to look for an "extra" namelist to use to get command arg auto-completion.
//this is always a particular function in gclCommandParse, but this level of indirection is necessary because
//UtilitiesLib can't link to functions in gclCommandParse.
typedef NameList *cmdGetExtraNameListForArgAutoCompletionCB(char *pCommandName, int iArgNum);
extern cmdGetExtraNameListForArgAutoCompletionCB *gpGetExtraNameListForArgAutoCompletionCB;
typedef enumNameListType cmdGetExtraNameListTypeForArgAutoCompletionCB(char *pCommandName, int iArgNum);
extern cmdGetExtraNameListTypeForArgAutoCompletionCB *gpGetExtraNameListTypeForArgAutoCompletionCB;

// Returns true if pchCommandString will execute pchCommandName.
bool cmdIsCommandIn(SA_PARAM_NN_STR const char *pchCommandString, SA_PARAM_NN_STR const char *pchCommandName);

//if true, then only commands with ACMD_CMDLINE (also spelled ACMD_COMMANDLINE) can be executed in the command
//line
extern bool gbLimitCommandLineCommands;


//special cmd parse that the server monitor calls which works with "slow" returning commands
int cmdParseForServerMonitor(CommandServingFlags eFlags, char *pCommand, int iAccessLevel, char **ppRetString, int iClientID, 
	int iCommandRequestID, U32 iMCPID, SlowCmdReturnCallbackFunc *pSlowReturnCB, void *pSlowReturnUserData, const char *pAuthNameAndIP,
	bool *pbOutReturnIsSlow);

//modified version of cmdParseForServerMonitor, doesn't used container IDs in return strings, some other minor differences
int cmdParseForClusterController(char *pCommand, int iAccessLevel, char **ppRetString, bool *pbReturnIsSlow, int iClientID, 
	int iCommandRequestID, U32 iMCPID, SlowCmdReturnCallbackFunc *pSlowReturnCB, void *pSlowReturnUserData, const char *pAuthNameAndIP);


//how a "slow" command returns its values when it completes. It needs to copy pSlowInfo out of the CmdContext passed in to it
void DoSlowCmdReturn(int iRetVal, const char *pRetString, CmdSlowReturnForServerMonitorInfo *pSlowInfo);

//You can use these next two in general cases, but they're specifically needed for commands that will be called via JsonRPC (in which
//case use DoSlowCmdReturn if it's a string to return)
void DoSlowCmdReturn_Int(S64 iRetVal, CmdSlowReturnForServerMonitorInfo *pSlowInfo);
void DoSlowCmdReturn_Struct(ParseTable *pTPI, void *pStruct, CmdSlowReturnForServerMonitorInfo *pSlowInfo);

//DO NOT USE THIS except in dire circumstances where you need to parse a string out of the command line VERY VERY VERY EARLY
//in startup. This only applies to the actual command line, not the cmdline.txt file, as it runs before the file system exists.
//It only works on a very simple format: "-commandname value". No stringifying or anything fancy. Note that it's OK to call
//this and then also do a normal auto_command for the same command.
int ParseCommandOutOfCommandLineEx(char *pCommandName, char *pValString, int iValStringSize);
#define ParseCommandOutOfCommandLine(pCommandName, valString) ParseCommandOutOfCommandLineEx(pCommandName, SAFESTR(valString))

//given a command line, divides it into presumed commands, stripping out the -?+ business at the beginning of each command, strdups
//the found commands and eapushes them
void cmdGetPresumedCommandsFromCommandLine(char *pInCommandLine, char ***pppOutEarray);

// "pretty print" the command output.  This will prefix commands that return
// numeric/bool results with "Cmd <commandname>: ".  Commands that return
// strings or structs are untouched.  ppchPrettyResult should be an estring.
typedef void (*CmdPrintfFunc)(FORMAT_STR char const *fmt, ...);
void cmdPrintPrettyOutput(CmdContext *pContext, CmdPrintfFunc printfFunc);

//banned commands can not be executed at all.
void cmdSetBannedCommands(char ***pppBannedCommands);
void cmdBanCommand(const char *pCommand);

//arguably should be in a better place. Used by CSR commands
AUTO_STRUCT;
typedef struct CSRCommandObject
{
	GlobalType callerType;
	ContainerID callerID;
	int callerAccessLevel;
	char *callerName;
	char *callerAccount;
	
	GlobalType playerType;
	ContainerID playerID;
	char *playerName;

	CSRPetCallback cbPetFunc; NO_AST

	char *commandString;
} CSRCommandObject;


//if true, then always use real access level even during UGC editing. Normally we force access level to ACCESS_UGC
//during UGC editing (whether it's higher or lower to begin with)
extern bool gbUseRealAccessLevelInUGC;


//stuff relating to CmdParseStructLists
void cmdParsePutStructListIntoPacket(Packet *pPkt, CmdParseStructList *pList, char *pComment);

void cmdClearStructList(CmdParseStructList *pList);

//the comment is passed down into ParserRecvStruct and used to generate creation comments for entities
void cmdParseGetStructListFromPacket(Packet *pPkt, CmdParseStructList *pList, char **ppErrorString, bool bSourceIsUntrustworthy);

void cmdDestroyUnownedStructList(CmdParseStructList *pList);
void cmdAddToUnownedStructList(CmdParseStructList *pList, ParseTable *pTPI, const void *pStruct);


//log all commands called which are AL >= this
LATELINK;
int cmdGetMinAccessLevelWhichForcesLogging(void);
#endif

//takes in a string like "doSomeThing 3, doSomeOtherThing 4" and sets
//the access level of doSomeThing to 3, doSomeOtherThing to 4, and all other
//commands back to their original access level. Won't set an access
//level to higher than MaxToSet
void cmdUpdateAccessLevelsFromCommaSeparatedString(CmdList *pList, char *pListName, char *pString, int iMaxToSet);

//the product name has now been set... go through all command lists and remove all commands that should not exist for this project
void cmdParseHandleProductNameSet(const char *pProductName);


//in order to export a really comprehensive and useful list of commands, we want to get all the information into a .csv
//file, so we put them temporarily into this struct for easy exporting
AUTO_STRUCT AST_FORMATSTRING(HTML_DEF_FIELDS_TO_SHOW = "Name, AccessLevel, SourceFile, LineNum, ExistsOnClient, ExistsOnServer, ChatAutoComplete, Hidden, Private, ExistsInCurrentProduct");
typedef struct CommandForCSV
{
	const char *pName; AST(POOL_STRING KEY)
	int iAccessLevel;

	const char *pSourceFile; AST(POOL_STRING)
	int iLineNum;

	bool bExistsOnClient;
	bool bExistsOnServer;
	bool bChatAutoComplete;
	bool bHidden;
	bool bPrivate;
	bool bExistsInCurrentProduct;
	bool bCommandLineOnly;
	bool bEarlyCommandLine;
	char *pProductString; AST(POOL_STRING)
	const char *pComment; AST(POOL_STRING)
} CommandForCSV;

AUTO_STRUCT;
typedef struct CommandForCSVList
{
	CommandForCSV **ppCommands;
} CommandForCSVList;

extern StashTable sCommandsForCSVExport;

//the client does special juju to get the server commands here
LATELINK;
bool ExportCSVCommandFile_Special(void);

//populates the internal CommandsForCSV stashtable, which can be servermonitored
void MakeCommandsForCSV();

//writes out the commands in the CommandsForCSV stashtable, use 
//ExportCommandsForCSV instead
void ExportCommandsForCSV_Internal();

//if true, then always log all AL>0 commands executed (in many contexts they are already logged,
//this is for something like Overlord where we want to be super-confident we know everything that is
//going on
//
//Also forces stringifying and logging of all multival args to commands (presumably from xmlrpc)
extern bool gbLogAllAccessLevelCommands;

//the command CmdPrintInfo prints out all known info about a given command. It's latelinked so the client/server stuff can work
//(pCommandName is really a char*, but latelinks in cmdparse.h don't work all that well)
LATELINK;
void CmdPrintInfoInternal(void *pCommandName_in, void *pContext_in);

char *cmdGetInfo_Internal(char *pCommandName);

//commands can have default values for args, which are used in contexts like jsonrpc where a function can be called
//with args-by-name or other methods different form the normal cmdparse fashion.
int cmdParseGetIntDefaultValueForArg(DataDesc *pArg);

//returns "" if there isn't one
char *cmdParseGetStringDefaultValueForArg(DataDesc *pArg);

