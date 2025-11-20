/***************************************************************************



***************************************************************************/

#pragma once

#include "multiplex.h"
#include "net/net.h"



//stuff for Multiplex on NetLinkList mode(MNLL)

typedef void MNLLCallBack_GotMessage(Packet *pak, int iMsg, int iIndexOfSender, NetLink *pNetLink, void *pUserData);
typedef void MNLLCallBack_ServerDied(NetLink *pNetLink, int iIndexOfServer, void *pUserData);

Packet *CreateMultiplexedNetLinkListPacket(NetLink *pNetLink, int iDestIndex, int iCmd, PacketTracker *pTracker);
Packet *CreateMultiplexedNetLinkListPacket_MultipleRecipients(NetLink *pNetLink, int *piDestIndices, int iCmd, PacketTracker *pTracker);

void PrepareForMultiplexedNetLinkListMode(MNLLCallBack_GotMessage *pGotMessageCB,
	MNLLCallBack_ServerDied *pServerDiedCB,

	//also pass in the "external" get-message callback, which is called-through when NON-multiplex
	//messages are received
	PacketCallback *pExternalGotMessageCB,
	LinkCallback *pExternalDestroyCB);


//these are the callbacks that need to actually be attached to the NetLinkList
void MultiplexedNetLinkList_Wrapper_HandleMsg(Packet *pak,int cmd, NetLink *link,void *pUserData);
void MultiplexedNetLinkList_Wrapper_ClientDisconnect(NetLink* link, void *pUserData);

