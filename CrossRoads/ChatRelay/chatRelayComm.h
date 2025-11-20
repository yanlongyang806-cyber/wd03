#pragma once

typedef enum GlobalType GlobalType;
typedef enum CmdContextFlag CmdContextFlag;
typedef enum enumCmdContextHowCalled enumCmdContextHowCalled;

void chatRelayStart(void);
void chatRelayCommsMonitor(void);
//void chatRelaySendCommandToServer(GlobalType eServerType, const char *pCmd, bool bPrivate, CmdContextFlag iFlags, enumCmdContextHowCalled eHow);
//void chatRelayRegisterUser(U32 uID);