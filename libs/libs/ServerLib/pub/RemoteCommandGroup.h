#pragma once
#include "GlobalTypes.h"
/*
A remote command group is a collection of things to potentially maybe do in the future. For instance, you can tell the
controller "hey, if the task manager crashes, here's a list of things to do". The things to do must be RETURNING REMOTE
commands, and they will be retried repeatedly until they succeed. (They're assumed to be always-succeed, so that they
presumably can only fail in cases that should repair themselves, such as a temporarily crashed server.
*/


AUTO_STRUCT;
typedef struct RemoteCommandGroup_SingleCommand
{
	GlobalType eServerTypeToExecuteOn; 
	ContainerID iServerIDToExecuteOn;
	char *pCommandString; AST(ESTRING)
	bool bCommandIsSlow;
} RemoteCommandGroup_SingleCommand;

AUTO_STRUCT;
typedef struct RemoteCommandGroup
{
	//kinda kludkgily, keystring is just %d %d %d the three ID fields below, to make indexed earrays
	//of RemoteCommandGroups work easily
	char keyString[32]; AST(KEY)

	//each command group must be uniquely identifiable so it can be cancelled
	GlobalType eSubmitterServerType;
	ContainerID iSubmitterContainerID;
	U32 iUidFromSubmitter;

	RemoteCommandGroup_SingleCommand **ppCommands;
} RemoteCommandGroup;

//fills in eSubmitterServerType, iSubmitterContainerID and iUidFromSubmitter (using interlockedIncrement). Always
//use this unless you have some good reason not to
RemoteCommandGroup *CreateEmptyRemoteCommandGroup(void);

void GetRemoteCommandGroupKeyString(char outString[32], GlobalType eSubmitterServerType, ContainerID iSubmitterContainerID, U32 iUidFromSubmitter);

void AddCommandToRemoteCommandGroup(RemoteCommandGroup *pGroup, GlobalType eTypeToExecuteOn, ContainerID iIDToExecuteOn,
	bool bCommandIsSlow, FORMAT_STR const char* pCommandFmt, ...);
#define AddCommandToRemoteCommandGroup(pGroup, eTypeToExecuteOn, iIDToExecuteOn, bCommandIsSlow, pCommandFmt, ...) \
	AddCommandToRemoteCommandGroup(pGroup, eTypeToExecuteOn, iIDToExecuteOn, bCommandIsSlow, FORMAT_STRING_CHECKED(pCommandFmt), __VA_ARGS__)

void AddCommandToRemoteCommandGroup_SentNPCEmail(RemoteCommandGroup *pGroup, ContainerID iAccountID, ContainerID vshardID, const char *pAccountName, const char *pFrom, const char *pTitle, const char *pBody);

typedef void (*RemoteCommandGroupDoneExecutingFunc)(void *pUserData);

//Automatically retries each one separately on failure, alerts after a certain number of failures
void ExecuteAndFreeRemoteCommandGroup(RemoteCommandGroup *pGroup, RemoteCommandGroupDoneExecutingFunc pCB, void *pUserData);

//execute nothing, same as StructDestroy
void DestroyRemoteCommandGroup(RemoteCommandGroup *pGroup);

//for easy testing... sets up an RCG to return to the current server, container ID 0, with
//a printf command
RemoteCommandGroup *CreateRCGWithPrintf(FORMAT_STR const char *pFmt, ...);

extern ParseTable parse_RemoteCommandGroup[];
#define TYPE_parse_RemoteCommandGroup RemoteCommandGroup