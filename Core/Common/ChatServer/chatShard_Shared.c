#include "chatShard_Shared.h"

#include "net.h"
#include "GlobalComm.h"

static char szGlobalChatServerAddress[128] = "localhost";

AUTO_COMMAND ACMD_EARLYCOMMANDLINE ACMD_ACCESSLEVEL(0);
void setGlobalChatServer(const char *pChatServer)
{
	strcpy(szGlobalChatServerAddress, pChatServer);
}
const char *getGlobalChatServer(void)
{
	return szGlobalChatServerAddress;
}

NetComm *getCommToGlobalChat(void)
{
	static NetComm *spGlobalComm = NULL;
	if (!spGlobalComm)
		spGlobalComm = commCreate(0,1);
	return spGlobalComm;
}

bool sendCommandToGlobalChat(NetLink *link, const char *pCommandString)
{
	if (linkConnected(link))
	{
		Packet *pkt = pktCreate(link, TO_GLOBALCHATSERVER_COMMAND);
		pktSendString(pkt, pCommandString);
		pktSend(&pkt);
		return true;
	}
	return false;
}
