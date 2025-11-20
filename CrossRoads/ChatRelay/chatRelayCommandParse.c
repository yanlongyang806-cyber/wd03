#include "chatRelay.h"
#include "chatRelayCommandParse.h"
#include "cmdparse.h"
#include "GlobalComm.h"
#include "net.h"

void chatRelaySendCommandToClient(U32 uAccountID, const char *pCmd, bool bPrivate, CmdContextFlag iFlags, enumCmdContextHowCalled eHow, CmdParseStructList *pStructs)
{
	ChatRelayUser *user = chatRelayGetUser(uAccountID);
	int iCmd;

	if (!pCmd || !user)
		return;
	if (bPrivate)
		iCmd = CHATRELAY_CMD_PRIVATE;
	else
		iCmd = CHATRELAY_CMD_PUBLIC;

	if (linkConnected(user->link))
	{
		Packet* pak = pktCreate(user->link,TOCLIENT_GAME_MSG);
		pktSendBitsPack(pak,8,0); // Empty EntRef

		pktSendBits(pak, 1, !!(iCmd & LIB_MSG_BIT));
		pktSendBitsPack(pak, GAME_MSG_SENDBITS, iCmd & ~LIB_MSG_BIT);
		pktSendString(pak, pCmd);
		cmdParsePutStructListIntoPacket(pak, pStructs, NULL);
		pktSendBits(pak, 32, iFlags);
		pktSendBits(pak, 32, eHow);
		pktSend(&pak);
	}
}

int ChatRelayParsePublicCommand(const char *cmd_str_orig, CmdContextFlag iFlags, ChatRelayUser *client, char **ppRetString, int iOverrideAccessLevel, enumCmdContextHowCalled eHow, CmdParseStructList *pStructs)
{
	char *lineBuffer, *cmd_str, *cmd_str_start = NULL;
	char *pInternalRetString = NULL;
	char *pOrigCmdString = NULL;
	estrStackCreate(&pOrigCmdString);
	estrStackCreate(&cmd_str_start);
	estrStackCreate(&pInternalRetString);
	estrCopy2(&cmd_str_start,cmd_str_orig);
	cmd_str = cmd_str_start;

	while(cmd_str)
	{
		CmdContext cmd_context = {0};
		int result;
		cmd_context.eHowCalled = eHow;
		cmd_context.pStructList = pStructs;

		if (ppRetString)
			cmd_context.output_msg = ppRetString;
		else
			cmd_context.output_msg = &pInternalRetString;

		lineBuffer = cmdReadNextLine(&cmd_str);
		estrCopy2(&pOrigCmdString, lineBuffer);

		if (client)
		{
			cmd_context.access_level = iOverrideAccessLevel != -1 ? iOverrideAccessLevel : client->uAccessLevel;
		}
		else
		{
			continue; // Chat relay doesn't support running public commands with no user
		}
		cmd_context.language = client->eLanguage;
		cmd_context.flags = iFlags;

		cmd_context.data = client;
		result = cmdParseAndExecute(&gGlobalCmdList,pOrigCmdString,&cmd_context);

		// TODO(Theo) logging, CSR commands?
		if (result && cmd_context.found_cmd->access_level > 0 && (cmd_context.flags & CMD_CONTEXT_FLAG_LOG_IF_ACCESSLEVEL))
		{
			/*if (iOverrideAccessLevel != -1)
			{
				entLog(LOG_COMMANDS, clientEntity, "AccessLevelGT0Command", "Player %s executed access level %d command string (Using override access level %d): %s",
					entGetLocalName(clientEntity), cmd_context.found_cmd->access_level, iOverrideAccessLevel, pOrigCmdString);
			}
			else
			{			
				entLog(LOG_COMMANDS, clientEntity, "AccessLevelGT0Command", "Player %s executed access level %d command string: %s",
					entGetLocalName(clientEntity), cmd_context.found_cmd->access_level, pOrigCmdString);
			}*/
		}

		/*if (estrLength(cmd_context.output_msg))
		{
			if (clientEntity)
				gslSendPrintf(clientEntity,"%s\n",*cmd_context.output_msg);
			else 
				cmdPrintPrettyOutput(&cmd_context, printf);
		}

		// don't show csr commands on target client
		if (clientEntity && cmd_context.found_cmd && eHow != CMD_CONTEXT_HOWCALLED_CSR_COMMAND) {
			int cmd_accessLevel = cmd_context.found_cmd->access_level;
			if (cmd_accessLevel > 0) {
				ClientCmd_notifyRanAccessLevelCmd(clientEntity, cmd_context.found_cmd->name, cmd_accessLevel);
			}
		}*/
	}

	estrDestroy(&pOrigCmdString);
	estrDestroy(&cmd_str_start);
	estrDestroy(&pInternalRetString);

	return 1;
}

int ChatRelayParsePrivateCommand(const char *cmd_str, CmdContextFlag iFlags, ChatRelayUser *client, int iOverrideAccessLevel, bool *pbUnknownCommand, enumCmdContextHowCalled eHow, CmdParseStructList *pStructs)
{
	CmdContext		cmd_context = {0};
	char *msg = NULL;
	int result = 0;
	char *pOrigCmdString = NULL;
	estrStackCreate(&pOrigCmdString);
	estrCopy2(&pOrigCmdString, cmd_str);

	cmd_context.flags = iFlags;
	cmd_context.eHowCalled = eHow;
	cmd_context.pStructList = pStructs;

	if (client)
	{
		cmd_context.access_level = iOverrideAccessLevel != -1 ? iOverrideAccessLevel : client->uAccessLevel;
	}
	else
	{
		cmd_context.access_level = iOverrideAccessLevel != -1 ? iOverrideAccessLevel : ACCESS_INTERNAL;
	}

	InitCmdOutput(cmd_context,msg);

	cmd_context.data = client;
	result = cmdParseAndExecute(&gPrivateCmdList,cmd_str,&cmd_context);

	if (!result)
	{
		if (!cmd_context.found_cmd && !cmd_context.banned_cmd)
		{
			if (pbUnknownCommand)
			{
				*pbUnknownCommand = true;
			}
			Errorf("Internal server command (%s) not found", cmd_str);
		}
		else
		{
			if (!(cmd_context.found_cmd && cmd_context.found_cmd->flags & CMDF_IGNOREPARSEERRORS))
			{
				Errorf("Internal server command (%s) returned error %s", cmd_str, msg);
			}
		}
	}
	else
	{
		// Logs access level > 0 commands
		if (cmd_context.found_cmd->access_level > 0 && (cmd_context.flags & CMD_CONTEXT_FLAG_LOG_IF_ACCESSLEVEL))
		{
			// Logging
			//entLog(LOG_COMMANDS, clientEntity, "AccessLevelGT0Command", "Player %s executed access level %d command string: %s",
			//	entGetLocalName(clientEntity), cmd_context.found_cmd->access_level, pOrigCmdString);
		}
	}

	CleanupCmdOutput(cmd_context);
	estrDestroy(&pOrigCmdString);
	return result;
}