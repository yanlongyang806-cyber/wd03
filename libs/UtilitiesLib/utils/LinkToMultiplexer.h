/***************************************************************************



***************************************************************************/
#pragma once

#include "multiplex.h"
#include "net/net.h"

typedef struct StaticDefineInt StaticDefineInt;

typedef struct LinkToMultiplexer LinkToMultiplexer;

//callback function when a message is received
typedef void LTMCallBack_GotMessage(Packet *pak, int iMsg, int iIndexOfSender, LinkToMultiplexer *pManager);

//callback function for when one of the other servers attached to the multiplexing server dies
typedef void LTMCallBack_AnotherAttachedServerDied(int iIndexOfDeadServer, LinkToMultiplexer *pManager);

//callback for when the entire multiplexing server dies
typedef void LTMCallBack_EntireServerDied(LinkToMultiplexer *pManager);


typedef struct LinkToMultiplexer
{
	NetComm *comm;
	NetLink *pLinkToMultiplexingServer;
	int iMyIndexOnMultiplexingServer;

	LTMCallBack_GotMessage *pGotMessageCB;
	LTMCallBack_AnotherAttachedServerDied *pAnotherServerDiedCB;
	LTMCallBack_EntireServerDied *pEntireServerDiedCB;


	bool bErrorFlag;
	void *pUserData;

	//stuff used during LinkToMultiplexerMonitorBlock
	int eCommandToBlockFor;
	PacketCallback *pBlockCallback;

	//for LinkToMultiplexerMonitorBlock
	bool bBlockingDone;
	
	bool bEntireServerDiedCBCalled;

	char **ppConnectTimeErrorMessage;
} LinkToMultiplexer;

LinkToMultiplexer *GetAndAttachLinkToMultiplexer(char *pServerHostName, int iServerPort, LinkType eType,
	LTMCallBack_GotMessage *pGotMessageCB,
	StaticDefineInt *pCmdNamesForMessageCB,

	LTMCallBack_AnotherAttachedServerDied *pAnotherServerDiedCB,
	LTMCallBack_EntireServerDied *pEntireServerDiedCB,
	//optional pointer to array of server indices that must be present, terminated by -1
	int *pRequiredServerIndices, int iCommIndex,
	void *pUserData, char *pLinkDebugName, char **ppErrorMessage);

Packet *CreateLinkToMultiplexerPacket(LinkToMultiplexer *pManager, int iDestIndex, int iCmd, PacketTracker *pTracker);

void DestroyLinkToMultiplexer(LinkToMultiplexer *pManager);

void UpdateLinkToMultiplexer(LinkToMultiplexer *pManager);

int LinkToMultiplexerMonitorBlock(LinkToMultiplexer *pManager, int lookForCommand, PacketCallback* netCallBack, int timeout);

void LinkToMultiplexer_Ping(LinkToMultiplexer *pLTM);