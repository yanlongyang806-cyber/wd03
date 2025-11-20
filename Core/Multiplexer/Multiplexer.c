/***************************************************************************
*     Copyright (c) 2005-2006, Cryptic Studios
*     All Rights Reserved
*     Confidential Property of Cryptic Studios
***************************************************************************/

#include "multiplexer.h"
#include "serverlib.h"
#include "error.h"
#include "GlobalTypes.h"
#include "utils.h"
#include "LocalTransactionManager.h"

MultiplexerConnection *FindConnectionFromIndex(Multiplexer *pMultiplexer, int iIndex)
{
	MultiplexerConnection *pConnection = &pMultiplexer->connections[MULTIPLEXER_GET_REAL_CONNECTION_INDEX(iIndex) ];
	if (pConnection->iIndex == iIndex)
	{
		return pConnection;
	}

	return NULL;
}

void IncrementConnectionUID(MultiplexerConnection *pConnection)
{
	int iUID = pConnection->iIndex >> MAX_MULTIPLEXER_CONNECTIONS_BITS;
	iUID++;
	if (iUID == (1 << (32 - MAX_MULTIPLEXER_CONNECTIONS_BITS - 2)))
	{
		iUID = 0;
	}

	pConnection->iIndex = MULTIPLEXER_GET_REAL_CONNECTION_INDEX(pConnection->iIndex) + (iUID << MAX_MULTIPLEXER_CONNECTIONS_BITS);
}

Packet *CreatePacketWithCopiedContents(Packet *pSourcePacket, NetLink *pLink, int iCmd)
{
	Packet *pNewPacket;
	pktCreateWithCachedTracker(pNewPacket, pLink, iCmd);
	pktAppend(pNewPacket, pSourcePacket, -1);
	return pNewPacket;
}



static Multiplexer *spMultiplexer = NULL;

MultiplexerConnection dummyConnection = {0};

void MultiplexerHandleMessageCB(Packet *pak, int cmd, NetLink *link, void *pUserData)
{
	MultiplexerConnection *pConnection = pUserData ? pUserData : &dummyConnection;
	MultiplexerConnection *pDestConnection;

	int iIndex = pConnection->iIndex;

	int iStartingPktIndex = pktGetIndex(pak);

	assert(spMultiplexer);


	if (cmd == SHAREDCMD_MULTIPLEX)
	{
		bool bIsCommand = (pktGetBits(pak, 1) == 1);

		if (!bIsCommand)
		{
			int iSenderIndex;
			int iDestIndex;
			int iCmd;

			iSenderIndex = pktGetBits(pak, 32);
			iDestIndex = pktGetBits(pak, 32);
			iCmd = pktGetBitsPack(pak, 4);

//			printf("Being asked to relay command %d from %d to %d\n", iCmd, iSenderIndex, iDestIndex);

			assert(iSenderIndex == iIndex);
			//check if the destination link is active
			pDestConnection = FindConnectionFromIndex(spMultiplexer, iDestIndex);

			if (pDestConnection && pDestConnection->pLink)
			{
				Packet *pNewPacket;

				pktSetIndex(pak, iStartingPktIndex);

				pNewPacket = CreatePacketWithCopiedContents(pak, pDestConnection->pLink, SHAREDCMD_MULTIPLEX);

//				printf("about to send it\n");

				pktSend(&pNewPacket);

				spMultiplexer->iTotalMessagesRelayed++;

			}		
		}
		else
		{
			enumMultiplexCommand eCommand;

			eCommand = (enumMultiplexCommand)pktGetBits(pak, BITS_FOR_MULTIPLEX_COMMAND);

			switch (eCommand)
			{
			case MULTIPLEX_COMMAND_PING:
				{
					U64 iInTime = pktGetBits64(pak, 64);
					Packet *pReturnPack = pktCreate(link, SHAREDCMD_MULTIPLEX);
					pktSendBits(pReturnPack, 1, 1);
					pktSendBits(pReturnPack, BITS_FOR_MULTIPLEX_COMMAND, MULTIPLEX_COMMAND_PING_RETURN);
					pktSendBits64(pReturnPack, 64, iInTime);
					pktSend(&pReturnPack);
					return;
				}

			case MULTIPLEX_COMMAND_I_WANT_TO_REGISTER_WITH_SERVER:
				{
					bool bSucceeded = true;
					int iRequiredIndex;

					GlobalType eType = GetContainerTypeFromPacket(pak);
					ContainerID iID = GetContainerIDFromPacket(pak);
					char *pComment = pktGetStringTemp(pak);
					char errorString[1024];

					errorString[0] = 0;

					if (pConnection == &dummyConnection)
					{
						Packet *pReturnPack = pktCreate(link, SHAREDCMD_MULTIPLEX);
						pktSendBits(pReturnPack, 1, 1);
						pktSendBits(pReturnPack, BITS_FOR_MULTIPLEX_COMMAND, MULTIPLEX_COMMAND_REGISTER_FAILED);
						pktSendString(pReturnPack, "Internal multiplexer corruption... connection not found");
						pktSend(&pReturnPack);
						return;
					}

					assert(MULTIPLEXER_GET_REAL_CONNECTION_INDEX(iIndex) >= MULTIPLEX_CONST_ID_MAX);
					assert(pConnection->bRegistrationReceived == false);


					while ((iRequiredIndex = (pktGetBitsPack(pak, 4) - 1)) != -1)
					{
						if (iRequiredIndex >= MULTIPLEX_CONST_ID_MAX)
						{
							sprintf(errorString, "invalid required index");
							bSucceeded = false;
							break;
						}

						if (!spMultiplexer->connections[iRequiredIndex].pLink || spMultiplexer->connections[iRequiredIndex].bRegistrationReceived == false)
						{
							sprintf(errorString, "One or more required Multiplexer servers not registered");
							bSucceeded = false;
							break;
						}
					}

					if (bSucceeded)
					{
						Packet *pReturnPack = pktCreate(link, SHAREDCMD_MULTIPLEX);
						pktSendBits(pReturnPack, 1, 1);
						pktSendBits(pReturnPack, BITS_FOR_MULTIPLEX_COMMAND, MULTIPLEX_COMMAND_REGISTER_SUCCEEDED);
						pktSendBits(pReturnPack, 32, iIndex);
						pktSend(&pReturnPack);

						spMultiplexer->connections[MULTIPLEXER_GET_REAL_CONNECTION_INDEX(iIndex)].bRegistrationReceived = true;

						linkSetDebugName(link, STACK_SPRINTF("Link from %s for \"%s\"", GlobalTypeAndIDToString(eType, iID), pComment));
					}
					else
					{
						Packet *pReturnPack = pktCreate(link, SHAREDCMD_MULTIPLEX);
						pktSendBits(pReturnPack, 1, 1);
						pktSendBits(pReturnPack, BITS_FOR_MULTIPLEX_COMMAND, MULTIPLEX_COMMAND_REGISTER_FAILED);
						pktSendString(pReturnPack, errorString);
						pktSend(&pReturnPack);
					}
				}				
			}
		}
	}
}



int MultiplexerConnectCB(NetLink* link, void *pUserData)
{
	MultiplexerConnection *pConnection;

	assert(spMultiplexer);

	pConnection = spMultiplexer->pFirstFree;

	if (!pConnection)
	{
		AssertOrAlert("MULTIPLEXER_FULL", "Multiplexer %u on %s already has %d netlinks, got a request for another one, is full.",
			GetAppGlobalID(), getHostName(), spMultiplexer->iCurNumConnections);
		linkSetUserData(link, NULL);
		return 1;
	}

	IncrementConnectionUID(pConnection);
	assert(pConnection);

	spMultiplexer->pFirstFree = pConnection->pNext;

	pConnection->bRegistrationReceived = false;
	pConnection->pLink = link;

	spMultiplexer->iCurNumConnections++;

	linkSetUserData(link, pConnection);


	return 1;
}


int MultiplexerDestroyCB(NetLink* link,void *pUserData)
{
	int i;
	int iDeadIndex;
	MultiplexerConnection *pConnection = pUserData;


	assert(spMultiplexer);

	

	iDeadIndex = pConnection->iIndex;


	for (i=0; i < MAX_MULTIPLEXER_CONNECTIONS; i++)
	{
		if (spMultiplexer->connections[i].bRegistrationReceived && spMultiplexer->connections[i].pLink && &spMultiplexer->connections[i] != pConnection)
		{
			Packet *pak;

			pak = pktCreate(spMultiplexer->connections[i].pLink, SHAREDCMD_MULTIPLEX);
			pktSendBits(pak, 1, 1);
			pktSendBits(pak, BITS_FOR_MULTIPLEX_COMMAND, MULTIPLEX_COMMAND_SINGLE_SERVER_DIED);
			pktSendBits(pak, 32, iDeadIndex);
			pktSend(&pak);
		}
	}

	pConnection->pLink = NULL;

	if (iDeadIndex >= MULTIPLEX_CONST_ID_MAX)
	{
		pConnection->pNext = spMultiplexer->pFirstFree;
		spMultiplexer->pFirstFree = pConnection;
	}

	spMultiplexer->iCurNumConnections--;


	return 1;
}



void CreateMultiplexer(int iPortNum)
{
	int i;

	//can't create two multiplexers
	assert(spMultiplexer == NULL);

	spMultiplexer = (Multiplexer*)malloc(sizeof(Multiplexer));
	memset(spMultiplexer, 0, sizeof(Multiplexer));

	loadstart_printf("Creating multiplexer ports...");
	//downstream ports from the multiplexer are noncritical, in that we don't want the
	//multiplexer to back up when a gameserver is stalled
	commListen(commDefault(),LINKTYPE_SHARD_NONCRITICAL_2MEG, LINK_FORCE_FLUSH,iPortNum,MultiplexerHandleMessageCB,MultiplexerConnectCB,MultiplexerDestroyCB,0);

	loadend_printf("");


	for (i=0; i < MAX_MULTIPLEXER_CONNECTIONS; i++)
	{
		spMultiplexer->connections[i].iIndex = i;
	}

	for (i = MULTIPLEX_CONST_ID_MAX; i < MAX_MULTIPLEXER_CONNECTIONS; i++)
	{
		spMultiplexer->connections[i].pNext = ((i == MAX_MULTIPLEXER_CONNECTIONS - 1) ? NULL : &spMultiplexer->connections[i + 1]);
	}

	spMultiplexer->pFirstFree = &spMultiplexer->connections[MULTIPLEX_CONST_ID_MAX];
}

void Multiplexer_Update()
{
	int i;

	assert(spMultiplexer);

	commMonitor(commDefault());
	for (i=0; i < MULTIPLEX_CONST_ID_MAX; i++)
	{
		if (spMultiplexer->connections[i].pLink)
		{
			assert(linkGetUserData(spMultiplexer->connections[i].pLink) == &spMultiplexer->connections[i]);

			if (linkDisconnected(spMultiplexer->connections[i].pLink))
			{
				linkRemove(&spMultiplexer->connections[i].pLink);
			}
		}
	}
}

static void Multiplex_HandleHandshakeCB(Packet *pak,int cmd, NetLink *link, void *pUserData)
{
	enumMultiplexCommand eCommand;
	int iServerCookie;

	switch(cmd)
	{
	case SHAREDCMD_MULTIPLEX:
		if (!pktGetBits(pak, 1))
		{
			assert(0);
			return;
		}

		eCommand = (enumMultiplexCommand)pktGetBits(pak, BITS_FOR_MULTIPLEX_COMMAND);

		switch (eCommand)
		{
		case MULTIPLEX_COMMAND_REGISTRATION_REQUEST_RECEIVED:
			iServerCookie = pktGetBitsPack(pak, 1);
			if (iServerCookie != gServerLibState.antiZombificationCookie)
			{
				Errorf("Multiplexer has different anti-zombification cookie from a server it is connecting to");
				assert(0);
				return;

			}

			return;

		default:
			//got an invalid message
			assert(0);
		}
	}
}

bool Multiplexer_ConnectToServer(char *pServerHostName, int iServerPort, int iServerMultiplexIndex, char *pDebugName, bool bCritical)
{
	NetLink *pLink;
	Packet *pak;
	int ret;
	MultiplexerConnection *pConnection;
	LinkFlags extraFlags = 0;

	assert(spMultiplexer);

	pConnection = FindConnectionFromIndex(spMultiplexer, iServerMultiplexIndex);

	if (!pConnection || pConnection->pLink)
	{
		return true;
	}

	pConnection->bRegistrationReceived = false;

	// Don't require compression, unless the other side wants it, or it's enabled by auto command.
	// This differs from regular Transaction Server links, which are compressed by default.
	// We don't use compression here to save CPU on the Transaction Server.
	if (giCompressTransactionLink != 1)
		extraFlags |= LINK_NO_COMPRESS;

	// Some upstream ports from the multiplexer are critical.  If the Transaction Server is going slow, we'd rather like to wait, because
	// the alternative is crashing, which will crash every Game Server on this machine immediately.  On the other hand, if the Log Server
	// goes down, we don't want to stall the Multiplexer, because logging is non-essential.
	pLink = commConnect(commDefault(),
		bCritical ? LINKTYPE_SHARD_CRITICAL_20MEG : LINKTYPE_SHARD_NONCRITICAL_20MEG,
		LINK_FORCE_FLUSH | extraFlags,
		pServerHostName,
		iServerPort,
		Multiplex_HandleHandshakeCB,
		NULL,
		MultiplexerDestroyCB,
		0);
	if (!linkConnectWait(&pLink,1.0))
	{
		return false;
	}
	pConnection->pLink = pLink;

	linkSetUserData(pLink, pConnection);

//	printf("About to send register requestion\n");

	pak = pktCreate(pLink, SHAREDCMD_MULTIPLEX);

	pktSendBits(pak, 1, 1);
	pktSendBits(pak, BITS_FOR_MULTIPLEX_COMMAND, MULTIPLEX_COMMAND_SERVER_WANTS_TO_REGISTER_WITH_YOU);
	pktSendBits(pak, 32, iServerMultiplexIndex);
	pktSend(&pak);

	ret = linkWaitForPacket(pLink,0,1000);

	if (!ret)
	{
		linkRemove(&pLink);
		pConnection->pLink = NULL;

		return false;
	}

	linkChangeCallback(pLink, MultiplexerHandleMessageCB);

	pConnection->bRegistrationReceived = true;

	spMultiplexer->iCurNumConnections++;

	linkSetDebugName(pLink, pDebugName);

	return true;
}


int GetNumMultiplexerConnections()
{
	if (!spMultiplexer)
	{
		return 0;
	}
	return spMultiplexer->iCurNumConnections;
}
int GetNumMultiplexerMessagesRelayed()
{
	if (!spMultiplexer)
	{
		return 0;
	}
	return spMultiplexer->iTotalMessagesRelayed;
}

bool Multiplexer_IsServerConnected(int iServerIndex)
{
	MultiplexerConnection *pConnection;

	if (!spMultiplexer)
	{
		return false;
	}

	pConnection = FindConnectionFromIndex(spMultiplexer, iServerIndex);

	if (!pConnection)
	{	
		return false;
	}

	return pConnection->pLink ? true : false;
}














