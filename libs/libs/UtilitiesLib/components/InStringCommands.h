#pragma once
GCC_SYSTEM

// "in string commands" are macro-y ways of setting up strings which automatically replace parts of themselves
//with other things. See the wiki page for more information. (Sorry for being so vague, it's a bit confusing).


#define INSTRINGCOMMAND_LOADFILE "LOADFILE "
#define INSTRINGCOMMAND_SYSCMDOUTPUT "SYSCMDOUTPUT "
#define INSTRINGCOMMAND_SYSCMDRETVAL "SYSCMDRETVAL "
#define INSTRINGCOMMAND_EXPAND "EXPAND "
#define INSTRINGCOMMAND_IF "IF"
#define INSTRINGCOMMAND_COMMAND "COMMAND"
#define INSTRINGCOMMAND_SUPERESCAPE "SUPERESCAPE "
#define INSTRINGCOMMAND_ALPHANUM "ALPHANUM "
#define INSTRINGCOMMAND_STRIPNEWLINES "STRIPNEWLINES "


typedef bool InStringCommands_FindFileCB(char *pInFile, char outFile[MAX_PATH], void *pUserData);
typedef void InStringCommands_FailCB(char *pFailureString, void *pUserData);
typedef bool InStringCommands_IsExpressionTrueCB(char *pExprString, void *pUserData);

//called on the estring after each instring replacement occurs. Returns true on success, false on failure
typedef bool InStringCommands_PostReplacmentCB(char **ppEString, void *pUserData);

typedef bool InStringCommand_AuxCommandCB(char *pInString, char **ppOutString, void *pUserData);


typedef struct InStringCommandsAuxCommand
{
	char *pCommand;
	InStringCommand_AuxCommandCB *pCB;
} InStringCommandsAuxCommand;

typedef struct InStringCommandsAllCBs
{
	InStringCommands_FindFileCB *pFindFileCB;
	InStringCommands_FailCB *pFailCB;
	InStringCommands_IsExpressionTrueCB *pIsExpressionTrueCB;
	InStringCommands_PostReplacmentCB *pPostReplacementCB;
	InStringCommandsAuxCommand **ppAuxCommands;
} InStringCommandsAllCBs;

//returns number of replacements made, or -1 on error
int InStringCommands_Apply(char **ppDestEString, InStringCommandsAllCBs *pCBs, void *pUserData);

