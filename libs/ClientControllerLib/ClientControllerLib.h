#pragma once

// Includes to support the GCL command func wrappers
#include "EString.h"
#include "GlobalTypes.h"

typedef enum ClientControllerState
{
	// 1-indexed to cooperate better with Lua
	CC_NOT_RUNNING = 1,
	CC_RUNNING,
	CC_CONNECTED,
	CC_CRASHED,
	CC_CRASH_COMPLETE,
} ClientControllerState;

void ClientController_InitLib(void);
bool ClientController_Tick(void);

const char *ClientController_GetExecDirectoryForClient(void);
void ClientController_SetExecDirectoryForClient(const char *pcDir);

void ClientController_AppendClientCommandLine(const char *pcCmdLineOption, const char *pcCmdLineSetting);
bool ClientController_StartClient(bool bWorld, bool bGraphics, const char *pchExtraCmdLine);
U32 ClientController_SendCommandToClient(const char *pchCommand);
void ClientController_KillClient(void);

ClientControllerState ClientController_MonitorState(void);
const char *ClientController_GetClientFSMState(void);

typedef struct NetLink NetLink;
typedef void LinkCallback(NetLink* link, void *user_data);
typedef void PacketCallback(Packet *pkt, int cmd, NetLink *link, void *user_data);

void ClientController_SetConnectCallback(LinkCallback *pCallback);
void ClientController_SetDisconnectCallback(LinkCallback *pCallback);
void ClientController_SetMessageCallback(PacketCallback *pCallback);

typedef void ClientController_CommandCB(const char *pcCommand, int id, const char *pcResult, const char *pcCommandString);
void ClientController_SetCommandCallback(ClientController_CommandCB *pCallback);

bool ClientController_MonitorCrashBeganEvents(const char *pcEventName);
bool ClientController_MonitorCrashCompletedEvents(const char *pcEventName);