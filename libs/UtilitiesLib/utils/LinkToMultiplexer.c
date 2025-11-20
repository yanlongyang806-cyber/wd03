/***************************************************************************



***************************************************************************/

#include "LinkToMultiplexer.h"

#include "GlobalComm.h"
#include "timing.h"
#include "GlobalTypes.h"
#include "estring.h"
#include "net/netprivate.h"

static NetComm *spLTMComms[MULTIPLEX_CONST_ID_MAX] = {0};


static void LTM_HandleMessage(Packet *pak,int cmd, NetLink *link, void *pUserData)
{
	enumMultiplexCommand eCommand;

	PERFINFO_AUTO_START_FUNC();

	switch(cmd)
	{
	case SHAREDCMD_MULTIPLEX:
		{
			bool bIsCommand = (pktGetBits(pak, 1) == 1);
			LinkToMultiplexer *pManager = linkGetLinkToMultiplexer(link);

			assert(pManager);

			if (bIsCommand)
			{
				eCommand = (enumMultiplexCommand)pktGetBits(pak, BITS_FOR_MULTIPLEX_COMMAND);

				switch (eCommand)
				{

				case MULTIPLEX_COMMAND_REGISTER_SUCCEEDED:
					pManager->iMyIndexOnMultiplexingServer = pktGetBits(pak, 32);
					pManager->bErrorFlag = false;
					break;

				case MULTIPLEX_COMMAND_REGISTER_FAILED:
					{
						char *pErrorMessage = pktGetStringTemp(pak);

						if (pManager->ppConnectTimeErrorMessage)
						{
							estrPrintf(pManager->ppConnectTimeErrorMessage, "Multiplex register failed: %s", pErrorMessage);
						}
					}
					break;
			
				case MULTIPLEX_COMMAND_PING_RETURN:
					{
						U64 iPingTime = pktGetBits64(pak, 64);
						printf("Ping from multiplexer received in %"FORM_LL"d milliseconds\n", timeMsecsSince2000() - iPingTime);
					}
					break;


				case MULTIPLEX_COMMAND_SINGLE_SERVER_DIED:
					{
						int iDeadServerID = pktGetBits(pak, 32);
						assertmsg(pManager->iMyIndexOnMultiplexingServer != -1, "Multiplex messages received out of order");


						pManager->pAnotherServerDiedCB(iDeadServerID, pManager);
					}
					break;
				}
			}
			else
			{
				int iSourceIndex;
				int iDestIndex;
				int iCmd;
			
				assertmsg(pManager->iMyIndexOnMultiplexingServer != -1, "Multiplex messages received out of order");

				iSourceIndex = pktGetBits(pak, 32);
				iDestIndex = pktGetBits(pak, 32);

				assert(iDestIndex == pManager->iMyIndexOnMultiplexingServer);

				iCmd = pktGetBitsPack(pak, 4);

				if (iCmd == pManager->eCommandToBlockFor)
				{
					pManager->pBlockCallback(pak, iCmd, link, pManager->pUserData);
					pManager->bBlockingDone = true;
				}
				else
				{
					if (gbTrackLinkReceiveStats)
					{
						S64 iTicks = timerCpuTicks64();
						U32 iSize = pktGetSize(pak);

						pManager->pGotMessageCB(pak, iCmd, iSourceIndex, pManager);
						linkUpdateReceiveStats(link, iCmd, iSize, timerCpuTicks64() - iTicks);
					}
					else
					{
						pManager->pGotMessageCB(pak, iCmd, iSourceIndex, pManager);
					}
					
				}
			}
		}
	}

	PERFINFO_AUTO_STOP();// FUNC
}



Packet *CreateLinkToMultiplexerPacket(LinkToMultiplexer *pManager, int iDestIndex, int iCmd, PacketTracker *pTracker)
{

	Packet *pak;

	if (!pManager || !pManager->pLinkToMultiplexingServer)
	{
		//should only happen during server shutdown of various sorts... ie, multiplexer dies, so we call
		//logFlush, which tries to flush logs to multiplexer, etc.
		return NULL;
	}

	pak = pktCreateWithTracker(pManager->pLinkToMultiplexingServer, SHAREDCMD_MULTIPLEX, pTracker);

	//send 0 because this is a normal multiplexed message, not a Multiplex server command
	pktSendBits(pak, 1, 0);

	//send the source index
	pktSendBits(pak, 32, pManager->iMyIndexOnMultiplexingServer);

	//send the dest index
	pktSendBits(pak, 32, iDestIndex);

	//send the command
	pktSendBitsPack(pak, 4, iCmd);

//	printf("Creating multiplex packet with command %d\n", iCmd);

	return pak;

}

void LinkToMultiplexer_Ping(LinkToMultiplexer *pLTM)
{
	Packet *pak = pktCreate(pLTM->pLinkToMultiplexingServer, SHAREDCMD_MULTIPLEX);

	if (!pak)
	{
		return;
	}

	//this is a command
	pktSendBits(pak, 1, 1);
	pktSendBits(pak, BITS_FOR_MULTIPLEX_COMMAND, MULTIPLEX_COMMAND_PING);
	pktSendBits64(pak, 64, timeMsecsSince2000());
	pktSend(&pak);
}

void LTM_Disconnect(NetLink *pLink, void *pUserData)
{
	LinkToMultiplexer *pManager = linkGetLinkToMultiplexer(pLink);
	if (pManager)
	{
		pManager->pLinkToMultiplexingServer = NULL;
	}
}


LinkToMultiplexer *GetAndAttachLinkToMultiplexer(char *pServerHostName, int iServerPort, LinkType eType,
	LTMCallBack_GotMessage *pGotMessageCB,
	StaticDefineInt *pCmdNamesForMessageCB,
	LTMCallBack_AnotherAttachedServerDied *pAnotherServerDiedCB,
	LTMCallBack_EntireServerDied *pEntireServerDiedCB,
	//optional pointer to array of server indices that must be present, terminated by -1
	int *pRequiredServerIndices, int iCommIndex,
	void *pUserData, char *pLinkDebugName, char **ppErrorMessage)
{
	LinkToMultiplexer *pManager = (LinkToMultiplexer*)malloc(sizeof(LinkToMultiplexer));
	Packet *pak;
	//int ret;

	assert((1 << BITS_FOR_MULTIPLEX_COMMAND) >= MULTIPLEX_COMMAND_LAST);

	memset(pManager, 0, sizeof(*pManager));

	pManager->pGotMessageCB = pGotMessageCB;
	pManager->pAnotherServerDiedCB = pAnotherServerDiedCB;
	pManager->pEntireServerDiedCB = pEntireServerDiedCB;

	assert(iCommIndex >= 0 && iCommIndex < MULTIPLEX_CONST_ID_MAX);

	if (!spLTMComms[iCommIndex])
	{

		spLTMComms[iCommIndex] = commCreate(0,1);;
	}

	pManager->comm = spLTMComms[iCommIndex];


	pManager->pLinkToMultiplexingServer = commConnect(pManager->comm,eType, LINK_FORCE_FLUSH,pServerHostName,iServerPort,LTM_HandleMessage,0,LTM_Disconnect,0);
	if (!pManager->pLinkToMultiplexingServer)
	{
		if (ppErrorMessage)
		{
			estrPrintf(ppErrorMessage, "commConnect failed while attempting to connect to multiplexer");
		}
		DestroyLinkToMultiplexer(pManager);
		return NULL;
	}

	if (!linkConnectWait(&pManager->pLinkToMultiplexingServer, 10.0f))
	{
		if (ppErrorMessage)
		{
			estrPrintf(ppErrorMessage, "linkConnectWait failed while attempting to connect to multiplexer");
		}

		DestroyLinkToMultiplexer(pManager);
		return NULL;
	}

	if (pCmdNamesForMessageCB)
	{
		linkInitReceiveStats(pManager->pLinkToMultiplexingServer, pCmdNamesForMessageCB);
	}


	pManager->ppConnectTimeErrorMessage = ppErrorMessage;


	linkSetDebugName(pManager->pLinkToMultiplexingServer, pLinkDebugName);

	linkSetLinkToMultiplexer(pManager->pLinkToMultiplexingServer, pManager);


	//we've linked to multiplexing server... now send it a message and say we want to be connected to it, and hopefully
	//get back our index

	pak = pktCreate(pManager->pLinkToMultiplexingServer, SHAREDCMD_MULTIPLEX);

	//this is a command
	pktSendBits(pak, 1, 1);

	pktSendBits(pak, BITS_FOR_MULTIPLEX_COMMAND, MULTIPLEX_COMMAND_I_WANT_TO_REGISTER_WITH_SERVER);

	PutContainerTypeIntoPacket(pak, GetAppGlobalType());
	PutContainerIDIntoPacket(pak, GetAppGlobalID());
	pktSendString(pak, pLinkDebugName);

	if (pRequiredServerIndices)
	{
		while (*pRequiredServerIndices != -1)
		{
			pktSendBitsPack(pak, 4, (*pRequiredServerIndices) + 1);
			pRequiredServerIndices++;
		}
	}

	//sent all required indices... terminate with 0
	pktSendBitsPack(pak, 4, 0);

	pktSend(&pak);

	pManager->bErrorFlag = true;


	if (!linkWaitForPacket(pManager->pLinkToMultiplexingServer,0,5.0f) || pManager->bErrorFlag)
	{
		if (!pManager->bErrorFlag && ppErrorMessage)
		{
			estrPrintf(ppErrorMessage, "Timeout failed while waiting for after sending handshaking to multiplexer");
		}

		DestroyLinkToMultiplexer(pManager);
		return NULL;
	}

	pManager->pUserData = pUserData;
	linkSetUserData(pManager->pLinkToMultiplexingServer, pUserData);

	return pManager;

}



void UpdateLinkToMultiplexer(LinkToMultiplexer *pManager)
{
	PERFINFO_AUTO_START_FUNC();
	if(!pManager)
	{
		PERFINFO_AUTO_STOP();
		return;
	}

	if (!pManager->pLinkToMultiplexingServer)
	{
		if (!pManager->bEntireServerDiedCBCalled)
		{
			pManager->bEntireServerDiedCBCalled = true;
			pManager->pEntireServerDiedCB(pManager);
		}
	}
	else
	{
		if(linkDisconnected(pManager->pLinkToMultiplexingServer))
		{
			if (!pManager->bEntireServerDiedCBCalled)
			{
				pManager->bEntireServerDiedCBCalled = true;
				pManager->pEntireServerDiedCB(pManager);
			}
		}		
		else
		{
			commMonitor(pManager->comm);
		}
	}
	PERFINFO_AUTO_STOP();
}

void DestroyLinkToMultiplexer(LinkToMultiplexer *pManager)
{
	linkRemove(&pManager->pLinkToMultiplexingServer);
	free(pManager);
}

int LinkToMultiplexerMonitorBlock(LinkToMultiplexer *pManager, int lookForCommand, PacketCallback* netCallBack, int timeout)
{

	U32 iStartingTime = timeSecondsSince2000();
	pManager->eCommandToBlockFor = lookForCommand;
	pManager->pBlockCallback = netCallBack;

	
	pManager->bBlockingDone = false;

	do
	{
		commMonitor(pManager->comm);
	}
	while (!pManager->bBlockingDone && timeSecondsSince2000() < iStartingTime + timeout);


	pManager->eCommandToBlockFor = 0;

	return pManager->bBlockingDone;
}
