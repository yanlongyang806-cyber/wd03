#include "chatCommonStructs.h"
#include "chatCommonStructs_h_ast.h"
#include "chatdb.h"
#include "chatGlobal.h"
#include "cmdparse.h"
#include "earray.h"
#include "estring.h"
#include "msgsend.h"
#include "qsortG.h"
#include "StringCache.h"
#include "users.h"

#include "AutoGen/AppServerLib_autogen_RemoteFuncs.h"
#include "AutoGen/ChatServer_autogen_SlowFuncs.h"
#include "Autogen/GameServerLib_autogen_RemoteFuncs.h"

extern ChatGuild **g_eaChatGuilds;

AUTO_COMMAND ACMD_CATEGORY(ChatAdmin);
void ChatServerForceGuildResync(void)
{
	if (ChatServerIsConnectedToGlobal())
		RemoteCommand_aslGuild_ChatUpdate(GLOBALTYPE_GUILDSERVER, 0);
	else
		Errorf("Global Chat Server is not connected.");
}

AUTO_COMMAND_REMOTE ACMD_IFDEF(APPSERVER);
void ChatServerGuildCreateFailed(CmdContext *context, const char *guildName)
{
	sendCommandToGlobalChatServer(context->commandString);
}

AUTO_COMMAND_REMOTE ACMD_IFDEF(CHATSERVER);
void ChatServerReserveGuildName_Response(bool bSuccess, int iCmdID)
{
	SlowRemoteCommandReturn_ChatServerReserveGuildName(iCmdID, bSuccess);
}

AUTO_COMMAND_REMOTE_SLOW(int) ACMD_IFDEF(APPSERVER);
void ChatServerReserveGuildName(const char *guildName, SlowRemoteCommandID iCmdID)
{
	if (ChatServerIsConnectedToGlobal())
	{
		char *escapedGuildName = NULL;
		estrAppendEscaped(&escapedGuildName, guildName);
		sendCmdAndParamsToGlobalChat("ChatServerReserveGuildName", "\"%s\" %d",	escapedGuildName, iCmdID);
		estrDestroy(&escapedGuildName);
	}
	else
	{
		SlowRemoteCommandReturn_ChatServerReserveGuildName(iCmdID, -1);
	}
}

AUTO_COMMAND_REMOTE ACMD_IFDEF(APPSERVER);
void ChatServerRemoveGuild(CmdContext *context, ContainerID iGuildID)
{
	int idx = eaIndexedFindUsingInt(&g_eaChatGuilds, iGuildID);
	if(idx != -1)
	{
		StructDestroy(parse_ChatGuild, g_eaChatGuilds[idx]);
		eaRemove(&g_eaChatGuilds, idx);
	}
	sendCommandToGlobalChatServer(context->commandString);
}

AUTO_COMMAND_REMOTE ACMD_IFDEF(APPSERVER);
int ChatServerUpdateGuild(CmdContext *context, ContainerID iGuildID, char *pchName)
{
	ChatGuild *pGuild = eaIndexedGetUsingInt(&g_eaChatGuilds, iGuildID);
	if(!pGuild)
	{
		ChatGuild *pNewGuild = StructCreate(parse_ChatGuild);
		pNewGuild->iGuildID = iGuildID;
		pNewGuild->pchName = allocAddString(pchName);
		eaPush(&g_eaChatGuilds, pNewGuild);
	}
	else if(stricmp(pGuild->pchName, pchName))
	{
		pGuild->pchName = allocAddString(pchName);
	}
	sendCommandToGlobalChatServer(context->commandString);
	return CHATRETURN_FWD_NONE;
}

AUTO_COMMAND_REMOTE ACMD_IFDEF(APPSERVER);
int ChatServerUpdateGuildBatch(CmdContext *context, ChatGuildList *pGuildList)
{
	int i, size;
	size = eaSize(&pGuildList->ppGuildList);
	for (i=0; i<size; i++)
	{
		ChatGuild *pGuild = eaIndexedGetUsingInt(&g_eaChatGuilds, pGuildList->ppGuildList[i]->iGuildID);
		if(!pGuild)
		{
			ChatGuild *pNewGuild = StructCreate(parse_ChatGuild);
			pNewGuild->iGuildID = pGuildList->ppGuildList[i]->iGuildID;
			pNewGuild->pchName = allocAddString(pGuildList->ppGuildList[i]->pchName);
			eaPush(&g_eaChatGuilds, pNewGuild);
		}
		else if(stricmp(pGuild->pchName, pGuildList->ppGuildList[i]->pchName))
		{
			pGuild->pchName = allocAddString(pGuildList->ppGuildList[i]->pchName);
		}
	}
	sendCommandToGlobalChatServer(context->commandString);
	return CHATRETURN_FWD_NONE;
}

AUTO_COMMAND_REMOTE ACMD_IFDEF(APPSERVER);
int ChatServerAllGuildMembers(CmdContext *context, ChatGuildMemberArray *pMemberArray)
{

	PERFINFO_AUTO_START_FUNC();
	loadstart_printf("Relaying guild member list...");
	sendCommandToGlobalChatServer(context->commandString);
	loadend_printf(" done (%d items)", eaiSize(&pMemberArray->pMemberData));
	PERFINFO_AUTO_STOP_FUNC();

	return CHATRETURN_FWD_NONE;
}

AUTO_COMMAND_REMOTE ACMD_IFDEF(APPSERVER);
int ChatServerAddGuildMemberById(CmdContext *context, ChatGuild *pGuild, U32 uAccountId, U32 uFlags)
{
	ChatGuild *pStoredGuild = eaIndexedGetUsingInt(&g_eaChatGuilds, pGuild->iGuildID);
	if (pStoredGuild)
	{
		int i = (int)ea32BFind(pStoredGuild->pGuildMembers, cmpU32, uAccountId);
		if (i == ea32Size(&pStoredGuild->pGuildMembers)
			|| pStoredGuild->pGuildMembers && pStoredGuild->pGuildMembers[i] != uAccountId)
			ea32Insert(&pStoredGuild->pGuildMembers, uAccountId, i);
	}
	sendCommandToGlobalChatServer(context->commandString);
	return CHATRETURN_FWD_NONE;
}

AUTO_COMMAND_REMOTE ACMD_IFDEF(APPSERVER);
int ChatServerRemoveGuildMemberById(CmdContext *context, ChatGuild *pGuild, U32 uAccountId)
{
	ChatGuild *pStoredGuild = eaIndexedGetUsingInt(&g_eaChatGuilds, pGuild->iGuildID);
	if (pStoredGuild)
	{
		int i = (int)ea32BFind(pStoredGuild->pGuildMembers, cmpU32, uAccountId);
		if (pStoredGuild->pGuildMembers && i < ea32Size(&pStoredGuild->pGuildMembers) && pStoredGuild->pGuildMembers[i] == uAccountId)
			ea32Remove(&pStoredGuild->pGuildMembers, i);
	}
	sendCommandToGlobalChatServer(context->commandString);
	return CHATRETURN_FWD_NONE;
}
