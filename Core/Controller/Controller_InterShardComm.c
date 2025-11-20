#include "controller.h"
#include "net.h"
#include "Controller_InterShardComm_c_ast.h"
#include "sock.h"
#include "earray.h"
#include "error.h"
#include "alerts.h"
#include "serverlib.h"
#include "globaltypes.h"
#include "cmdParse.h"
#include "objTransactions.h"
#include "autogen/serverlib_autogen_remotefuncs.h"
#include "alerts.h"

static NetListen *spInterShardListen = NULL;

AUTO_STRUCT;
typedef struct InterShardCommPermission
{
	U32 iIP; 
	char **ppCommands; 
} InterShardCommPermission;

static InterShardCommPermission **ppInterShardPermissions = NULL;

//the controller optionally opens up a port through which other shards can send command requests. If so,
//it must have a list of acceptable IPs along with a comma
AUTO_COMMAND ACMD_COMMANDLINE;
void ListenForInterShardCommands(char *pIP, char *pCommaSepCommands)
{
	U32 iIP = ipFromString(pIP);
	if (!iIP)
	{
		ErrorOrAlert("BAD_INTERSHARD_IP", "Can't convert (%s) to a numeric IP", pIP);
	}
	else
	{
		InterShardCommPermission *pPermission = StructCreate(parse_InterShardCommPermission);
		pPermission->iIP = iIP;
		DivideString(pCommaSepCommands, ",", &pPermission->ppCommands, DIVIDESTRING_POSTPROCESS_STRIP_WHITESPACE);

		eaPush(&ppInterShardPermissions, pPermission);
	}
}

//NULL cmd name means "Are any commands allowed from this IP"
bool CmdIsOKForInterShardCom(U32 iIP, char *pCmdName)
{
	int i;

	for (i=0; i < eaSize(&ppInterShardPermissions); i++)
	{
		if (ppInterShardPermissions[i]->iIP == iIP)
		{
			if (!pCmdName)
			{
				return true;
			}


			if (eaFindString(&ppInterShardPermissions[i]->ppCommands, pCmdName) != -1)
			{
				return true;
			}
		}
	}

	return false;
}

void InterShardCommHandleMsg(Packet *pak,int cmd, NetLink *link, void *pUserData)
{
	switch (cmd)
	{
	case INTERSHARDCOMM_REQUEST_CMD:
		{
			char *pCmdName = pktGetStringTemp(pak);
			char *pFullCmdString = NULL;
			GlobalType eServerTypeForCmd = GetContainerTypeFromPacket(pak);
			ContainerID iServerIDForCmd = GetContainerIDFromPacket(pak);
			char *pArgString = pktGetStringTemp(pak);

			if (CmdIsOKForInterShardCom(linkGetIp(link), pCmdName))
			{
				estrPrintf(&pFullCmdString, "%s %s", pCmdName, pArgString);

				if (eServerTypeForCmd == GLOBALTYPE_CONTROLLER)
				{
					cmdParseForServerMonitor(pFullCmdString, ACCESS_DEBUG, NULL, NULL, 0, 0, 0, 0, 0, NULL);
				}
				else
				{
					if (objLocalManager())
					{
						RemoteCommand_CallLocalCommandRemotely(eServerTypeForCmd, iServerIDForCmd, pFullCmdString);
					}
				}
			
				estrDestroy(&pFullCmdString);
			}
			else
			{
				TriggerAlertf("INVALID_INTERSHARD_COMMAND", ALERTLEVEL_CRITICAL, ALERTCATEGORY_NETOPS, 0, 0, 0, 0, 0, NULL, 0, 
					"Someone tried to send intershard command %s from %s, this is not allowed",
					pCmdName, makeIpStr(linkGetIp(link)));
			}

		}
		break;
	}
}

int InterShardCommConnect(NetLink* link,void *pUserData)
{
	if (!CmdIsOKForInterShardCom(linkGetIp(link), NULL))
	{
		TriggerAlertf("INVALID_INTERSHARD_IP", ALERTLEVEL_CRITICAL, ALERTCATEGORY_NETOPS, 0, 0, 0, 0, 0, NULL, 0, 
			"Someone tried to connect for intershard commands from %s, this is not allowed",
			makeIpStr(linkGetIp(link)));
	}
	return 1;
}

void InitInterShardComm(void)
{
	if (eaSize(&ppInterShardPermissions))
	{
		spInterShardListen = commListen(commDefault(),LINKTYPE_SHARD_NONCRITICAL_20MEG, LINK_FORCE_FLUSH,INTERSHARD_COMM_PORT,InterShardCommHandleMsg,InterShardCommConnect,NULL,0);
	}
}



AUTO_STRUCT;
typedef struct CommandForOtherShard
{
	GlobalType eTypeOnOtherShard;
	ContainerID iIDOnOtherShard;
	char *pCommandName;
	char *pArgs; AST(ESTRING)
} CommandForOtherShard;



AUTO_STRUCT;
typedef struct OtherShardConnection
{
	U32 iIP;
	CommConnectFSM *pFSM; NO_AST
	NetLink *pLink; NO_AST
	CommandForOtherShard **ppCommands;
} OtherShardConnection;

static OtherShardConnection **sppOtherShardConnections = NULL;

void SendCommandToOtherShardThroughLink(NetLink *pLink, GlobalType eTypeOnOtherShard, ContainerID iIDOnOtherShard, char *pCommandName, char *pArgs)
{
	Packet *pPak = pktCreate(pLink, INTERSHARDCOMM_REQUEST_CMD);
	pktSendString(pPak, pCommandName);
	PutContainerTypeIntoPacket(pPak, eTypeOnOtherShard);
	PutContainerIDIntoPacket(pPak, iIDOnOtherShard);
	pktSendString(pPak, pArgs);
	pktSend(&pPak);
}


void UpdateInterShardComm(void)
{
	int iConnectionNum;
	int i;

	for (iConnectionNum = eaSize(&sppOtherShardConnections) - 1; iConnectionNum >= 0; iConnectionNum--)
	{
		OtherShardConnection *pConnection = sppOtherShardConnections[iConnectionNum];

		if (pConnection->pFSM)
		{
			CommConnectFSMStatus eStatus = commConnectFSMUpdate(pConnection->pFSM, &pConnection->pLink);

			if (eStatus == COMMFSMSTATUS_SUCCEEDED)
			{
				commConnectFSMDestroy(&pConnection->pFSM);
				
				for (i=0; i < eaSize(&pConnection->ppCommands); i++)
				{
					SendCommandToOtherShardThroughLink(pConnection->pLink,
						pConnection->ppCommands[i]->eTypeOnOtherShard,
						pConnection->ppCommands[i]->iIDOnOtherShard,
						pConnection->ppCommands[i]->pCommandName,
						pConnection->ppCommands[i]->pArgs);
				}

				eaDestroyStruct(&pConnection->ppCommands, parse_CommandForOtherShard);
			}
			else if (eStatus == COMMFSMSTATUS_FAILED)
			{
				char *pErrorString = NULL;
				commConnectFSMDestroy(&pConnection->pFSM);

				estrPrintf(&pErrorString, "Couldn't connect to shard at %s for intershard commands. Lost commands are: ",
					makeIpStr(pConnection->iIP));
				
				for (i=0; i < eaSize(&pConnection->ppCommands); i++)
				{
					estrConcatf(&pErrorString, "%s%s", i == 0 ? "" : ", ", pConnection->ppCommands[i]->pCommandName);
				}

				TriggerAlert("FAILED_INTERSHARD_CMD_SEND", pErrorString, ALERTLEVEL_CRITICAL, ALERTCATEGORY_NETOPS,
					0, 0, 0, 0, 0, NULL, 0);

				estrDestroy(&pErrorString);
				StructDestroy(parse_OtherShardConnection, pConnection);
				eaRemoveFast(&sppOtherShardConnections, iConnectionNum);
			}
		}
	}
}

OtherShardConnection *FindOtherShardConnection(U32 iIP)
{
	int i;
	OtherShardConnection *pRetVal;

	for (i=0; i < eaSize(&sppOtherShardConnections); i++)
	{
		if (sppOtherShardConnections[i]->iIP == iIP)
		{
			pRetVal = sppOtherShardConnections[i];

			if (!pRetVal->pLink)
			{
				return pRetVal;
			}

			if (linkConnected(pRetVal->pLink) && !linkDisconnected(pRetVal->pLink))
			{
				return pRetVal;
			}

			linkRemove(&pRetVal->pLink);

			pRetVal->pFSM = commConnectFSM(COMMFSMTYPE_TRY_ONCE, 60, commDefault(), LINKTYPE_SHARD_NONCRITICAL_20MEG, LINK_FORCE_FLUSH, makeIpStr(iIP), INTERSHARD_COMM_PORT, NULL, NULL, NULL, 0, NULL, 0);
			return pRetVal;
		}
	}

	pRetVal = StructCreate(parse_OtherShardConnection);
	pRetVal->iIP = iIP;

	pRetVal->pFSM = commConnectFSM(COMMFSMTYPE_TRY_ONCE, 60, commDefault(), LINKTYPE_SHARD_NONCRITICAL_20MEG, LINK_FORCE_FLUSH, makeIpStr(iIP), INTERSHARD_COMM_PORT, NULL, NULL, NULL, 0, NULL, 0);
	eaPush(&sppOtherShardConnections, pRetVal);
	return pRetVal;

}


AUTO_COMMAND_REMOTE;
void SendCommandToOtherShard(U32 iIPOfOtherShard, GlobalType eTypeOnOtherShard, ContainerID iIDOnOtherShard, char *pCommandName, char *pArgs_In, bool bDoControllerIPReplacing)
{
	OtherShardConnection *pConnection = FindOtherShardConnection(iIPOfOtherShard);
	CommandForOtherShard *pDelayedCommand = NULL;

	char *pArgsToUse = NULL;

	estrCopy2(&pArgsToUse, pArgs_In);

	if (bDoControllerIPReplacing)
	{
		char *pMyIPString = NULL;
		estrPrintf(&pMyIPString, "\"%s\"", makeIpStr(getHostLocalIp()));
		estrReplaceOccurrences(&pArgsToUse, "\"__CONTROLLER_IP__\"", pMyIPString);
		estrDestroy(&pMyIPString);
	}

	//FindOtherShardConnection never returns shards with disconnected links
	if (pConnection->pLink)
	{
		SendCommandToOtherShardThroughLink(pConnection->pLink, eTypeOnOtherShard, iIDOnOtherShard, pCommandName, pArgsToUse);
		estrDestroy(&pArgsToUse);
		return;
	}


	pDelayedCommand = StructCreate(parse_CommandForOtherShard);
	pDelayedCommand->eTypeOnOtherShard = eTypeOnOtherShard;
	pDelayedCommand->iIDOnOtherShard = iIDOnOtherShard;
	pDelayedCommand->pArgs = pArgsToUse;
	pArgsToUse = NULL;
	pDelayedCommand->pCommandName = strdup(pCommandName);

	eaPush(&pConnection->ppCommands, pDelayedCommand);
}



#include "Controller_InterShardComm_c_ast.c"
