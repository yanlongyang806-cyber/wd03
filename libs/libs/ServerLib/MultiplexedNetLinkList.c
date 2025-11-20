/***************************************************************************



***************************************************************************/

#include "MultiplexedNetLinkList.h"
#include "serverlib.h"
#include "earray.h"
#include "net/netprivate.h"
#include "timing.h"

#define MNLL_STATE_INACTIVE -2
#define MNLL_STATE_WAITING_FOR_FIRST_CONNECT -1

//this state variable starts out as MNLL_STATE_INACTIVE or MNLL_STATE_WAITING_FOR_FIRST_CONNECT. Once at least one server has connected,
//it becomes the local index on the multiplexing servers.
static sMNLLState = MNLL_STATE_INACTIVE;
static MNLLCallBack_GotMessage *spMNLLGotMessageCB;
static MNLLCallBack_ServerDied *spMNLLServerDiedCB;
static PacketCallback *spMNLLExternalGotMessageCB;
static LinkCallback *spMNLLExternalDestroyCB;

Packet *CreateMultiplexedNetLinkListPacket(NetLink *pNetLink, int iDestIndex, int iCmd, PacketTracker *pTracker)
{
	Packet *pak;

	//if this assert hits, someone is trying to create a packet to send from a server to a multiplexed server, when
	//it has not connected to such a server, or has not had PrepareForMultiplexedNetLinkListMode called.
	assert(sMNLLState >= 0);

	pak = pktCreateWithTracker(pNetLink, SHAREDCMD_MULTIPLEX, pTracker);

	//send 0 because this is a normal multiplexed message, not a Multiplex server command
	pktSendBits(pak, 1, 0);

	//send the source index
	pktSendBits(pak, 32, sMNLLState);

	//send the dest index
	pktSendBits(pak, 32, iDestIndex);

	//send the command
	pktSendBitsPack(pak, 4, iCmd);

	return pak;
}

Packet *CreateMultiplexedNetLinkListPacket_MultipleRecipients(NetLink *pNetLink, int *piDestIndices, int iCmd, PacketTracker *pTracker)
{
	Packet *pak;
	int i;

	//if this assert hits, someone is trying to create a packet to send from a server to a multiplexed server, when
	//it has not connected to such a server, or has not had PrepareForMultiplexedNetLinkListMode called.
	assert(sMNLLState >= 0);

	pak = pktCreateWithTracker(pNetLink, SHAREDCMD_MULTIPLEX, pTracker);
	
	//send 1 because this is a command
	pktSendBits(pak, 1, 1);
	
	//send the special command
	pktSendBits(pak, BITS_FOR_MULTIPLEX_COMMAND, MULTIPLEX_COMMAND_SEND_PACKET_TO_MULTIPLE_RECIPIENTS);

	//send the source index
	pktSendBits(pak, 32, sMNLLState);

	//send the number of destination indices
	pktSendBits(pak, 32, ea32Size(&piDestIndices));

	//send each dest index
	for (i = 0; i < ea32Size(&piDestIndices); i++)
	{
		pktSendBits(pak, 32, piDestIndices[i]);
	}

	//send the command
	pktSendBitsPack(pak, 4, iCmd);

	return pak;
}






void PrepareForMultiplexedNetLinkListMode(MNLLCallBack_GotMessage *pGotMessageCB,
	MNLLCallBack_ServerDied *pServerDiedCB,

	//also pass in the "normal" get-message callback, which is called-through when NON-multiplex
	//messages are received
	PacketCallback *pExternalGotMessageCB,
	LinkCallback *pExternalDestroyCB)
{
	//this function can only be called once per process
	assert(sMNLLState == MNLL_STATE_INACTIVE);

	sMNLLState = MNLL_STATE_WAITING_FOR_FIRST_CONNECT;

	spMNLLGotMessageCB = pGotMessageCB;
	spMNLLServerDiedCB = pServerDiedCB;
	spMNLLExternalGotMessageCB = pExternalGotMessageCB;
	spMNLLExternalDestroyCB = pExternalDestroyCB;
}

void MultiplexedNetLinkList_Wrapper_HandleMsg(Packet *pak,int cmd, NetLink *link, void *pUserData)
{
	bool bIsCommand;

	//if this assert hits, PrepareForMultiplexedNetLinkListMode was not called
	assert(sMNLLState != MNLL_STATE_INACTIVE);

	if (cmd != SHAREDCMD_MULTIPLEX)
	{
		spMNLLExternalGotMessageCB(pak, cmd, link, pUserData);
		return;
	}

	bIsCommand = (pktGetBits(pak, 1) == 1);

	if (!bIsCommand)
	{
		int iSourceIndex;
		int iDestIndex;
		int iCmd;

		//can't receive normal multiplexed messages until we got a MULTIPLEX_COMMAND_SERVER_WANTS_TO_REGISTER_WITH_YOU
		assert(sMNLLState >= 0);

		iSourceIndex = pktGetBits(pak, 32);
		iDestIndex = pktGetBits(pak, 32);
		iCmd = pktGetBitsPack(pak, 4);

//		printf("Received message from %d to %d, type %d\n", iSourceIndex, iDestIndex, iCmd);

		//make sure that we are getting messages sent to the index that we live at
		assert(iDestIndex == sMNLLState);

		if (gbTrackLinkReceiveStats)
		{
			S64 iTicks = timerCpuTicks64();
			U32 iSize = pktGetSize(pak);

			spMNLLGotMessageCB(pak, iCmd, iSourceIndex, link, pUserData);
			linkUpdateReceiveStats(link, iCmd, iSize, timerCpuTicks64() - iTicks);
		}
		else
		{
			spMNLLGotMessageCB(pak, iCmd, iSourceIndex, link, pUserData);
		}


		return;
	}
	else
	{
		enumMultiplexCommand eCommand;

		eCommand = (enumMultiplexCommand)pktGetBits(pak, BITS_FOR_MULTIPLEX_COMMAND);

		switch (eCommand)
		{
		case MULTIPLEX_COMMAND_SERVER_WANTS_TO_REGISTER_WITH_YOU:
			{
				int iRequestedIndex;
				Packet *pResponsePacket;

				iRequestedIndex = pktGetBits(pak, 32);
				assert(sMNLLState == MNLL_STATE_WAITING_FOR_FIRST_CONNECT || sMNLLState == iRequestedIndex);
				sMNLLState = iRequestedIndex;

				pResponsePacket = pktCreate(link, SHAREDCMD_MULTIPLEX);
				pktSendBits(pResponsePacket, 1, 1);
				pktSendBits(pResponsePacket, BITS_FOR_MULTIPLEX_COMMAND, MULTIPLEX_COMMAND_REGISTRATION_REQUEST_RECEIVED);
				pktSendBitsPack(pResponsePacket, 1, gServerLibState.antiZombificationCookie);
				pktSend(&pResponsePacket);
			}
			return;



		case MULTIPLEX_COMMAND_SINGLE_SERVER_DIED:
			{
				int iDeadServerIndex = pktGetBits(pak, 32);
				if (spMNLLServerDiedCB)
				{
					spMNLLServerDiedCB(link, iDeadServerIndex, pUserData);
				}
			}
			return;
		}
	}
}

void MultiplexedNetLinkList_Wrapper_ClientDisconnect(NetLink* link, void *pUserData)
{
	if (spMNLLExternalDestroyCB)
	{
		spMNLLExternalDestroyCB(link, pUserData);
	}
}
