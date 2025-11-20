#pragma once

typedef struct ChatRelayUser ChatRelayUser;
typedef enum CmdContextFlag CmdContextFlag;
typedef enum enumCmdContextHowCalled enumCmdContextHowCalled;
typedef struct CmdParseStructList CmdParseStructList;

void chatRelaySendCommandToClient(U32 uAccountID, const char *pCmd, bool bPrivate, CmdContextFlag iFlags, enumCmdContextHowCalled eHow, CmdParseStructList *pStructs);
int ChatRelayParsePublicCommand(const char *cmd_str_orig, CmdContextFlag iFlags, ChatRelayUser *client, char **ppRetString, int iOverrideAccessLevel, enumCmdContextHowCalled eHow, CmdParseStructList *pStructs);
int ChatRelayParsePrivateCommand(const char *cmd_str, CmdContextFlag iFlags, ChatRelayUser *client, int iOverrideAccessLevel, bool *pbUnknownCommand, enumCmdContextHowCalled eHow, CmdParseStructList *pStructs);
