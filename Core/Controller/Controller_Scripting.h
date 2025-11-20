#pragma once

#include "ControllerPub.h"

typedef struct TrackedMachineState TrackedMachineState;

//This header file contains the enums and structs that define the controller scripting language.
//
//If you are writing commands outside the controller that will be interacting with the controller scripting,
//you will need to call the functions in ControllerScriptingSupport.c/.h



//information loaded in from config files concerning the configuration of machines that the controller is currently
//running on, which can be used to set up production configurations or test settings
AUTO_ENUM;
typedef enum
{
	CONTROLLERCOMMAND_DONOTHING, //does nothing, but still displays display string

//commands which set stuff up, and are all "Executed" at startup time
	CONTROLLERCOMMAND_SPECIFY_MACHINE, //don't launch anything, but specify that this machine or range of machines
										//can run servers of this type
	

//commands which happen sequentially
	CONTROLLERCOMMAND_LAUNCH_NORMALLY, //Launch all servers that this server depends on, then launch this server
									   //launch this server once everything it depends on has reached its normal state
									   //note that this command always has a very long time out (half hour) because 
									   //one server might have to wait for a previous server before it can launch. 
									   //If you want timeout checks on server
									   //launching, do them with KeepAliveDelays in controller_serverSetup.txt
	
	CONTROLLERCOMMAND_LAUNCH_DIRECTLY, //Specifically launches a particular server. Does NOT do any dependencies or
									   //anything else. RARELY USEFUL BE CAREFUL.




	CONTROLLERCOMMAND_PREPARE_FOR_LAUNCH, //like LAUNCH_NORMALLY, except that once a server is ready to launch it is not 
										  //actually launched

//"scripting" commands. In general, if these refer to a particular server, then eServerType is used to indicate
//the type of server, and iServerIndex indicates the index of server (ie, 0 = the first server of that type, 1 = the
//second one, etc.). iServerIndex of -1 means do it to all servers of that type. If no servertype is specified, then
//the command either doesn't refer to a specific server, or refers to the controller itself

	CONTROLLERCOMMAND_EXECUTECOMMAND, //uses cmdParse to execute the string given in pScriptString.
									  //
									  //see the comment by KILLSERVER concerning "quit" and similar commands

	CONTROLLERCOMMAND_WAITFORSTATE, //waits until the specified server's GSM state string contains 
									//pScriptString as a substring

	CONTROLLERCOMMAND_WAITFORSTEPDONE,	// continually queries the value of gbCommandStepDone,
										  //which can be set (to 1 or -1 for success or failure) by 
										  //any server calling ControllerScript_Succeeded() or Failed()
										  //
										  //note that a specified server is required... ie, WAITFORSTEPDONE JobManager
	
	CONTROLLERCOMMAND_WAITFORSERVERDEATH, //waits until the specified server no longer exists

	CONTROLLERCOMMAND_EXECUTECOMMAND_WAIT, //the same as EXECUTECOMMAND followed by WAITFORSTEPDONE. Generally, you
											//should use this. The exception is when executing a command on one server,
											//but a different server will be informing you that the step is done.

	CONTROLLERCOMMAND_EXITWITHVALUE, //tells the controller to exit with the return value given in ScriptInt

	CONTROLLERCOMMAND_KILLSERVER, //kills the specified server
					//if ScriptString is specified, then send that string to the server as a command, and assume
					//that it will cause the server to kill itself (this is the only way to test commands like 
					//"quit", as otherwise a server closing itself will cause the controller to believe that the
					//script failed)

	CONTROLLERCOMMAND_SETSHAREDCOMMANDLINE, //sets the extra command line that is shared by all launched servers
		//this command is DANGEROUS as it resets all "normal" default options, such as the ones that tell
		//the launched servers how to report errors to the script, etc. If you want to use this, let me know
		//and we'll figure out the right way for it to work (ie, maybe it should maintain the default options
		//and only reset the other ones, etc.)
		//
		//note that commands in the shared command line should often be of the form "-?Command" rather than "-command". This means that
		//it's not an error if that command doesn't exist, which is often the case for commands being sent to
		//everything, which are in (for instance) serverlib, and thus exist on everything but the client


	CONTROLLERCOMMAND_APPENDSHAREDCOMMANDLINE, //appends to the extra command line that is shared by all launched servers

	CONTROLLERCOMMAND_SETSERVERTYPECOMMANDLINE,
	CONTROLLERCOMMAND_APPENDSERVERTYPECOMMANDLINE, //like the above, but only applies to one specific server type

	CONTROLLERCOMMAND_KILLALL, //kills all servers

	CONTROLLERCOMMAND_SYSTEM, //executes pScriptString as a system string, waits until it is finished
							 //normal directory macros are used (see ApplyDirectoryMacros() in fileutil2.h)
	
	CONTROLLERCOMMAND_SETFAILONERROR, //sets or unsets "fail-on-error" mode, which causes any errorf on any server to 
	CONTROLLERCOMMAND_UNSETFAILONERROR, //make the script fail

	CONTROLLERCOMMAND_WAIT, //waits for ScriptInt or ScriptFloat seconds, whichever is non-zero. Useful to make sure that an error doesn't occur for a while

	CONTROLLERCOMMAND_SETMODE_TESTING, //sets various default options to the usually-appropriate modes for doing 
		//tests of various sorts

	CONTROLLERCOMMAND_SETMODE_SHARDSETUP, //sets various default options to the usually-appropriate modes for doing
		//shard setup things

	CONTROLLERCOMMAND_INCLUDE, //loads the controller script named by ScriptString, inserts all of its commands
							   //right here in the list

	CONTROLLERCOMMAND_SETVAR, //ScriptString should be of format "VARNAME = valuable", sets $VARNAME$ to that value
	CONTROLLERCOMMAND_SETVARIABLE = CONTROLLERCOMMAND_SETVAR, //ScriptString should be of format "VARNAME = valuable", sets $VARNAME$ to that value

	CONTROLLERCOMMAND_FOR, //ScriptString should be of format "VARNAME = val1, val2, val3, ..., valn"
	CONTROLLERCOMMAND_ENDFOR, //end of a for loop (for loops can be nested)

	CONTROLLERCOMMAND_LAUNCHXBOXCLIENT, //launches xbox client on local machine with extraCommandLine 
	CONTROLLERCOMMAND_WAITFORXBOXCLIENTSTATE, //like WAITFORSTATE

	//like EXECUTECOMMAND_WAIT and EXECUTECOMMAND, obviously
	CONTROLLERCOMMAND_EXECUTECOMMAND_WAIT_XBOXCLIENT,
	CONTROLLERCOMMAND_EXECUTECOMMAND_XBOXCLIENT,
	CONTROLLERCOMMAND_KILLXBOXCLIENT,


#define STR_CONTAINS "STR_CONTAINS "
#define STR_EQUALS "STR_EQUALS "
#define STR_CONTAINED_BY "STR_CONTAINED_BY "
	//repeated queries a server by sending it the command in ScriptString every second,
	//waits until that command succeeds AND the return string for the command matches
	//ScriptResultString. Legal syntax for ScriptResultString is one of "= %d", "< %d", "> %d", "STR_CONTAINS %s",
	//"STR_EQUALS %s", "STR_CONTAINED_BY %s". All string comparisons are case insensitive.
	CONTROLLERCOMMAND_REPEATEDLY_QUERY_SERVER,

	//like the above, but it querys all servers of all types until they all return the desired result.
	CONTROLLERCOMMAND_REPEATEDLY_QUERY_ALL_SERVERS,

	//expects ScriptString in the form "materials/*.material;defs/encounters/*.encounter"
	//
	//Then for each directory, finds the scriptInt largest files matching the wildcard, 
	//and touches all of them (presumably triggering a reload)
	//
	//For the moment, the wildcard is fake, and must be an asterisk immediately after the last
	//slash.
	CONTROLLERCOMMAND_TOUCH_FILES,

	//goes to another command. The "name" of the command to go to is its DisplayString,
	//which should be put in this command's ScriptString. Alternatively, can jump to any REM 
	//using its REM string. Note that this uses RAW strings, so variable replacing and stuff won't work
	CONTROLLERCOMMAND_GOTO,

	//only works if the controller is being run from a continuous builder
	//scriptString is a comma-separated list of variables. Imports those variables
	//from the continuous build script currently running.
	CONTROLLERCOMMAND_IMPORT_VARIABLES,

	//wait until the expression in ScriptString is true. Fail if it isn't true by FailureTime seconds
	CONTROLLERCOMMAND_WAITFOREXPRESSION,
	
	//a remark
	CONTROLLERCOMMAND_REM,

	//displays a message on the front page of the servermonitor, waits until the user confirms before proceeding
	CONTROLLERCOMMAND_WAITFORCONFIRM,
} enumControllerCommand;


//mostly, command sub types only apply to system commands
AUTO_ENUM;
typedef enum enumControllerScriptCommandSubType
{
	CSCSUBTYPE_MUSTSUCCEED, //default... if this command returns non-zero, the entire script fails

	CSCSUBTYPE_IGNORERESULT, //the return value of this command is ignored

	CSCSUBTYPE_FAILURE_IS_NON_FATAL, //if this command fails, that counts as an "error", but not a failure				
} enumControllerScriptCommandSubType;

AUTO_STRUCT;
typedef struct ControllerScriptingCommand
{
	enumControllerCommand eCommand; AST(STRUCTPARAM)
	GlobalType eServerType;  AST(SUBTABLE(GlobalTypeEnum))
	enumControllerScriptCommandSubType eSubType; 
	char *pMachineName; AST(DEF("*"))

	char *pExtraCommandLine_Raw; AST(NAME(ExtraCommandLine)) //extra command line used for LAUNCH_NORMALLY
	char *pExtraCommandLine_Use; AST(ESTRING)  //the above, with variable replacing done


	U32 iFirstIP; AST(FORMAT_IP) //if firstIP and lastIP are zero, then this command refers only to a single server, as
								//defined in pServerName. Otherwise it refers to an entire range of machines
	U32 iLastIP; AST(FORMAT_IP)
	int iCount; AST(DEF(1)) //how many servers of this type to launch

	U32 bStartHidden_Internal : 1; AST(NAME(StartHidden) INDEX_DEFINE) //deprecated... use Visibility instead, but this is still supported

	U32 bWillDie : 1;

	enumVisibilitySetting eVisibility;

//stuff used by "scripting" commands
	char *pScriptString_Raw; AST(NAME(ScriptString))//generic string used by various scripting commands for various things
	char *pScriptString_Use; AST(ESTRING) //the above, with variable replacing done

	char *pScriptResultString_Raw; AST(NAME(ScriptResultString))//generic string used by various scripting commands for various things involving return values
	char *pScriptResultString_Use; AST(ESTRING) //the above, with variable replacing done

	
	char *pDisplayString_Raw; AST(NAME(DisplayString)) //string to display while this command is ongoing (only useful for commands that wait)
	char *pDisplayString_Use;  AST(ESTRING)//the above, with variable replacing done

	char *pWorkingDir_Raw; AST(NAME(WorkingDir)) //Working Dir for command launching
	char *pWorkingDir_Use;  AST(ESTRING)//the above, with variable replacing done

	char *pIfExpression_Raw; AST(NAME(If)) //an expression. If it exists, only do this step if the if expression is true
	char *pIfExpression_Use; AST(ESTRING) //the above, with variable replacing done

	int iScriptInt; AST(INDEX_DEFINE)
	float fScriptFloat; AST(INDEX_DEFINE)
	int iServerIndex; //which server of the specified type this command refers to
	U32 iFailureTime; AST(DEF(120)) //if we stay in this step for this number of seconds, fail. If 0, wait forever

	int iNextServerIndex; NO_AST //used locally to iterate through all servers for this command

	int iForCount; NO_AST
	bool bLooping; NO_AST //set to true when looping back to this FOR command so that it knows whether to start from 0
	TrackedMachineState **ppMachines; NO_AST //what machines this command will affect. Calculated as the command begins,
											//so recalculated every time through a FOR loop.
	bool bFirstTime; NO_AST //for commands that take multiple frames to complete, is set to true whenever a command
							//is begun. (You have to manually set it back to false if you are using it.)

	U32 iUsedBits[2]; AST(USEDFIELD)


	char **ppInternalStrs; AST(STRUCTPARAM)
	//during post-text-read fixup, these are turned into various "Real" fields.

	char *pSourceFileName; AST(CURRENTFILE)
	int iLineNum; AST(LINENUM)

	//if set, for a system command, then do printf redirection as if this launch were of a server of this type
	GlobalType eLoggingServerType;  AST(SUBTABLE(GlobalTypeEnum))
} ControllerScriptingCommand;


//given a server type, looks through the currently active script and attempts to guess what command line it will likely be launched
//with. This isn't perfect because the script actually executing might change things, but the purpose of this is to preemptively generate 
//warnings and stuff, not to be 100% perfect. Note that this only cares about command line stuff that is set through controller scripting,
//not internal things, things set by the MCP, etc.
void ControllerScripting_GetLikelyCommandLine(GlobalType eServerType, char **ppCommandLine);


//The way this works is that it effectively skips its timeout backwards n seconds, but not additively. This is used for
//shader compilation, and each time it begins compiling a shader, it will extend by some amount, but presumably
//most shader compilations take less than that time. So, for example, we might be getting "extend by 15 seconds" requests
//coming in every 5 seconds for a long time. When the very last one of those comes in, we want to then extend by 15
//seconds, not by 15 seconds + 10 seconds times the number of shaders
void ControllerScripting_TemporaryPause(int iForHowManySeconds, FORMAT_STR const char *pReasonFmt, ...);
