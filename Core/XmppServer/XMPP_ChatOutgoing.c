#include "XMPP_ChatOutgoing.h"
#include "Autogen/XMPP_ChatOutgoing_c_ast.h"

#include "earray.h"
#include "net.h"
#include "GlobalComm.h"
#include "timing.h"
#include "ChatServer/chatShard_Shared.h"

// Command Queue
#define MAX_COMMANDS_PER_TICK 200
#define MAX_COMMAND_QUEUE_SIZE 100000

// Commands that need to be sent to the Global Chat Server
AUTO_STRUCT;
typedef struct XMPPChatCommand
{
	U32 uTime; // time this command was queued
	U32 uLinkID;
	char *commandString; AST(ESTRING)

	U32 uCommandFlag; // for special commands
} XMPPChatCommand;

static XMPPChatCommand **sppLocalChatCommandQueue = NULL;

static void addCommandToQueue(SA_PARAM_NN_VALID NetLink *link, const char *commandString, U32 uCommandFlag)
{
	if (eaSize(&sppLocalChatCommandQueue) < MAX_COMMAND_QUEUE_SIZE)
	{
		XMPPChatCommand *cmd = StructCreate(parse_XMPPChatCommand);

		if (commandString)
			estrCopy2(&cmd->commandString, commandString);
		cmd->uCommandFlag = uCommandFlag;
		cmd->uLinkID = linkID(link);
		cmd->uTime = timeSecondsSince2000();
		eaPush(&sppLocalChatCommandQueue, cmd);
	} // else this is a silent failure
}

void XMPP_SendGlobalChatQuery(NetLink *link, const char *pCommand, const char *pParamFormat, ... )
{
	char *fullCommand = NULL;
	char *pParameters = NULL;

	if (!linkConnected(link))
		return;
	estrStackCreate(&fullCommand);
	estrStackCreate(&pParameters);
	if (pParamFormat)
		estrGetVarArgs(&pParameters, pParamFormat);
	if (pParameters)
		estrPrintf(&fullCommand, "%s %s", pCommand, pParameters);
	else
		estrPrintf(&fullCommand, "%s", pCommand);

	if (!sendCommandToGlobalChat(link, fullCommand))
		addCommandToQueue(link, fullCommand, 0);
	estrDestroy(&fullCommand);
	estrDestroy(&pParameters);
}

#include "Autogen/XMPP_ChatOutgoing_c_ast.c"