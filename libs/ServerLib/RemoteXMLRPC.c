
#include "HttpLib.h"
#include "HttpXpathSupport.h"

#include "GlobalTypes.h"
#include "GlobalComm.h"
#include "cmdparse.h"
#include "HttpXpathSupport.h"
#include "GlobalEnums.h"


//For slow commands on entities
#include "../ServerLib/TransactionRequestManager.h"
#include "objTransactions.h"
#include "objContainer.h"


#include "../HTTPLib/XMLRPC.h"
#include "../Common/AutoGen/ServerLib_autogen_RemoteFuncs.h"
#include "../../Core/Common/AutoGen/ObjectDB_autogen_remotefuncs.h"

typedef U32 ContainerID;



void OVERRIDE_LATELINK_HandleMonitoringCommandRequestFromPacket(Packet *pPacket, NetLink *pNetLink)
{
	int iClientID = pktGetBits(pPacket, 32);
	int iCommandRequestID = pktGetBits(pPacket, 32);
	ContainerID iMCPID = GetContainerIDFromPacket(pPacket);
	CommandServingFlags eFlags = pktGetBits(pPacket, 32);
	char *pCommandString = pktGetStringTemp(pPacket);
	const char *pAuthNameAndIP = pktGetStringTemp(pPacket);
	int iAccessLevel = pktGetBits(pPacket, 32);
	Packet *pOutPak;

	if (eFlags & CMDSRV_NORETURN)
	{
		cmdParseForServerMonitor(eFlags, pCommandString, iAccessLevel, NULL, 0, 0, 0, 0, 0, pAuthNameAndIP, NULL);
	}
	else
	{
		char *pRetString = NULL;
		bool bSlowReturn = false;

		//I'm not sure this ifndef is necessary since we've already linked expat.
#ifndef _XBOX
		if (strStartsWith(pCommandString, "<?xml"))
		{
			//This code is duplicated in ServerLib.c and Controller_net.c
			//any updates must update all 3 places.
			char clientname[16];
			XMLParseInfo *info = XMLRPC_Parse(pCommandString,linkGetIpStr(pNetLink, clientname, 16));
			XMLMethodResponse *response = NULL;
			CmdSlowReturnForServerMonitorInfo slowReturnInfo = {0};
			estrCreate(&pRetString);
			if (info)
			{
				XMLMethodCall *method = NULL;
				if (method = XMLRPC_GetMethodCall(info))
				{
					slowReturnInfo.iClientID = iClientID;
					slowReturnInfo.iCommandRequestID = iCommandRequestID;
					slowReturnInfo.iMCPID = iMCPID;
					slowReturnInfo.pSlowReturnCB = DoSlowReturn_NetLink;
					response = XMLRPC_ConvertAndExecuteCommand(method, iAccessLevel, 
						//This is probably the wrong link! This'll give bad categories.
						httpGetCommandCategories(pNetLink), 
						pAuthNameAndIP,
						&slowReturnInfo);
/* ABW removing this 4/19/2013, pretty sure that it's just leaking memory at this point, not sure why it was
ever there.*/

					if (slowReturnInfo.bDontDestroyXMLMethodCall)
					{	//We're going to pass the method call to the destination server so don't destroy it.
						info->state->methodCall = NULL;
					}
				}
				else
				{
					estrPrintf(&pRetString, "XMLRPC Request contained an error: %s", info->error);
				}
				StructDestroy(parse_XMLParseInfo, info);
			}
			else
			{
				estrPrintf(&pRetString, "Error generating XMLRPC request.");
			}
			if (!response)
			{
				response = XMLRPC_BuildMethodResponse(NULL, XMLRPC_FAULT_BADPARAMS, pRetString);
				estrClear(&pRetString);
			}
			if (!(bSlowReturn = slowReturnInfo.bDoingSlowReturn))
			{
				XMLRPC_WriteOutMethodResponse(response, &pRetString);
			}
			StructDestroy(parse_XMLMethodResponse, response);
		}
		else
#endif
		{
			estrStackCreate(&pRetString);
			cmdParseForServerMonitor(eFlags, pCommandString, iAccessLevel, &pRetString, iClientID, iCommandRequestID, iMCPID, DoSlowReturn_NetLink, NULL, pAuthNameAndIP, &bSlowReturn);
		}

		if (!bSlowReturn)
		{
			pOutPak = pktCreate(pNetLink, TO_CONTROLLER_HERE_IS_REQUESTED_MONITORING_COMMAND_RESULT);

			pktSendBits(pOutPak, 32, iClientID);
			pktSendBits(pOutPak, 32, iCommandRequestID);
			PutContainerIDIntoPacket(pOutPak, iMCPID);
			pktSendBits(pOutPak, 32, eFlags);
			pktSendString(pOutPak, pRetString);

			pktSend(&pOutPak);		
		}

		estrDestroy(&pRetString);
	}
}


static void xmlrpc_RequestContainerMove_CB(TransactionReturnVal *pReturn,  CmdContext *slowContext) {

	ContainerLocation *loc = slowContext->data;

	char *estr = 0;
	XMLMethodResponse *response = NULL;


	if (pReturn->eOutcome == TRANSACTION_OUTCOME_SUCCESS)
	{
		XMLMethodCall *method = (XMLMethodCall *)slowContext->commandData;

		xmlrpc_insert_entity_param(method, loc->containerID);
		response = XMLRPC_ConvertAndExecuteCommand(method, slowContext->access_level, NULL, slowContext->pAuthNameAndIP, NULL);
		StructDestroy(parse_XMLMethodCall, method);

		//return the container we checked out.
		objRequestContainerMove(NULL, loc->containerType, loc->containerID, objServerType(), objServerID(), loc->ownerType, loc->ownerID);
	}
	else
	{
		response = XMLRPC_BuildMethodResponse(NULL, XMLRPC_FAULT_ENTITYOWNED, "Could not get Entity for this command; Player may be logged in on another server.");
	}

	estrStackCreate(&estr);
	XMLRPC_WriteOutMethodResponse(response, &estr);
	StructDestroy(parse_XMLMethodResponse, response);

	slowContext->slowReturnInfo.pSlowReturnCB(
		slowContext->slowReturnInfo.iMCPID, 
		slowContext->slowReturnInfo.iCommandRequestID, 
		slowContext->slowReturnInfo.iClientID, 
		slowContext->slowReturnInfo.eFlags,
		estr, NULL);

	StructDestroy(parse_ContainerLocation, loc);
	free(slowContext);
	

	estrDestroy(&estr);
}

static void xmlrpc_RemoteCAE_CB(TransactionReturnVal *returnVal, CmdContext *slowContext)
{
	XMLMethodCall *method = (XMLMethodCall *)slowContext->commandData;
	char *result = NULL;
	if (RemoteCommandCheck_XMLRPC_RemoteCAEC(returnVal, &result) == TRANSACTION_OUTCOME_SUCCESS)
	{
		slowContext->slowReturnInfo.pSlowReturnCB(
			slowContext->slowReturnInfo.iMCPID, 
			slowContext->slowReturnInfo.iCommandRequestID, 
			slowContext->slowReturnInfo.iClientID, 
			slowContext->slowReturnInfo.eFlags,
			result, NULL);
		StructDestroy(parse_XMLMethodCall, method);
		free(slowContext);
		return;
	}
	//else
	{
		Cmd *cmd = slowContext->found_cmd;
		XMLMethodResponse *response = XMLRPC_BuildMethodResponse(NULL, XMLRPC_FAULT_REMOTEFAILURE, "Failed to execute command on remote server.");
		char *estr = 0;
		estrStackCreate(&estr);

		XMLRPC_WriteOutMethodResponse(response, &estr);
		StructDestroy(parse_XMLMethodResponse, response);

		slowContext->slowReturnInfo.pSlowReturnCB(
			slowContext->slowReturnInfo.iMCPID, 
			slowContext->slowReturnInfo.iCommandRequestID, 
			slowContext->slowReturnInfo.iClientID, 
			slowContext->slowReturnInfo.eFlags,
			estr, NULL);

		if (method)
			StructDestroy(parse_XMLMethodCall, method);
		estrDestroy(&estr);
		free(slowContext);
	}
}

static void xmlrpc_GetPlayerIDFromName_CB(TransactionReturnVal *returnVal, CmdContext *slowContext)
{
	ContainerLocation *loc = NULL;

	if(RemoteCommandCheck_HTTP_dbContainerLocationFromPlayerRef(returnVal, &loc) == TRANSACTION_OUTCOME_SUCCESS)
	{
		if (loc->ownerType == objServerType() && loc->ownerID == objServerID())
		{	//The container should be resident, run the command here, immediately.
			XMLMethodCall *method = (XMLMethodCall *)slowContext->commandData;
			XMLMethodResponse *response = NULL;
			char *estr = 0;

			xmlrpc_insert_entity_param(method, loc->containerID);
			response = XMLRPC_ConvertAndExecuteCommand(method, slowContext->access_level, NULL, slowContext->pAuthNameAndIP, NULL);
			StructDestroy(parse_XMLMethodCall, method);

			estrStackCreate(&estr);
			XMLRPC_WriteOutMethodResponse(response, &estr);
			StructDestroy(parse_XMLMethodResponse, response);

			slowContext->slowReturnInfo.pSlowReturnCB(
				slowContext->slowReturnInfo.iMCPID, 
				slowContext->slowReturnInfo.iCommandRequestID, 
				slowContext->slowReturnInfo.iClientID, 
				slowContext->slowReturnInfo.eFlags,
				estr, NULL);

			estrDestroy(&estr);
			StructDestroy(parse_ContainerLocation, loc);
			free(slowContext);
			return;
		}
		else if (loc->ownerType == GLOBALTYPE_OBJECTDB)
		{	//transfer the container and run it here, then put it back.
			slowContext->data = loc;
			objRequestContainerMove(objCreateManagedReturnVal(xmlrpc_RequestContainerMove_CB, slowContext),
				GLOBALTYPE_ENTITYPLAYER, loc->containerID, loc->ownerType, loc->ownerID, objServerType(), objServerID());
			return;
		}
		else if (loc->ownerType == GLOBALTYPE_GAMESERVER)
		{   //send this method to the game server via a remote command and run it there.
			XMLMethodCall *method = (XMLMethodCall *)slowContext->commandData;
			xmlrpc_insert_entity_param(method, loc->containerID);
			RemoteCommand_XMLRPC_RemoteCAEC(objCreateManagedReturnVal(xmlrpc_RemoteCAE_CB, slowContext),
						 loc->ownerType, loc->ownerID, method);
			return;
		}
	}

	//fail
	{
		XMLMethodCall *method = (XMLMethodCall *)slowContext->commandData;
		Cmd *cmd = slowContext->found_cmd;
		XMLMethodResponse *response = XMLRPC_BuildMethodResponse(NULL, XMLRPC_FAULT_ENTITYNOTFOUND, "Could not find named character. Also, CodeRanger is a cupcake.");
		char *estr = 0;
		estrStackCreate(&estr);

		XMLRPC_WriteOutMethodResponse(response, &estr);
		StructDestroy(parse_XMLMethodResponse, response);

		slowContext->slowReturnInfo.pSlowReturnCB(
			slowContext->slowReturnInfo.iMCPID, 
			slowContext->slowReturnInfo.iCommandRequestID, 
			slowContext->slowReturnInfo.iClientID, 
			slowContext->slowReturnInfo.eFlags,
			estr, NULL);

		if (method)
			StructDestroy(parse_XMLMethodCall, method);
		estrDestroy(&estr);
		free(slowContext);
	}
}

XMLMethodResponse* OVERRIDE_LATELINK_XMLRPC_DispatchEntityCommand(CmdContext *slowContext, char *name, ContainerID iVirtualShardID)
{
	RemoteCommand_HTTP_dbContainerLocationFromPlayerRef(objCreateManagedReturnVal(xmlrpc_GetPlayerIDFromName_CB, slowContext),
		GLOBALTYPE_OBJECTDB, 0, GLOBALTYPE_ENTITYPLAYER, name, iVirtualShardID);
	
	return XMLRPC_BuildMethodResponse(NULL, XMLRPC_FAULT_SLOWCOMMAND, 
		"XMLRPC: this is not an error, we are dispatching slow return.");
}

void OVERRIDE_LATELINK_XMLRPC_LoadEntity(CmdContext *slowContext, char *ent)
{
	Container *con = NULL;
	GlobalType type = 0;
	ContainerID id = 0;
	DecodeContainerTypeAndIDFromString(ent, &type, &id);
	if (id) con = objGetContainer(type, id);
	if (con) slowContext->data = con->containerData;
	else slowContext->data = NULL;
}


AUTO_COMMAND_REMOTE ACMD_IFDEF(SERVERLIB);
char *XMLRPC_RemoteCAEC(CmdContext *pCmdContext, XMLMethodCall *method)
{
	XMLMethodResponse *response = XMLRPC_ConvertAndExecuteCommand(method, pCmdContext->access_level, NULL, pCmdContext->pAuthNameAndIP, NULL);
	char *estr = NULL;
	char *result;

	estrStackCreate(&estr);

	XMLRPC_WriteOutMethodResponse(response, &estr);
	StructDestroy(parse_XMLMethodResponse, response);

	result = strdup(estr);
	estrDestroy(&estr);

	return result;
}