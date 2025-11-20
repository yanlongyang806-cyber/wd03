#pragma once

#include "net/net.h"
#include "multiplex.h"


//a Multiplexer is something that has links to lots of servers, and sends messages from all of them to a single server, and back.
//Typically, a launcher has a multiplexer, and uses it to multiplex messages from all the servers it spawns up to the transaction
//server and log server
//
//only one multiplexer is allowed in a given process

#define MAX_MULTIPLEXER_CONNECTIONS 4096
#define MAX_MULTIPLEXER_CONNECTIONS_BITS 12

#define MULTIPLEXER_GET_REAL_CONNECTION_INDEX(i) (((i) & (MAX_MULTIPLEXER_CONNECTIONS-1)))


void CreateMultiplexer(int iPortNum);

bool Multiplexer_ConnectToServer(char *pServerHostName, int iServerPort, int iServerMultiplexIndex, char *pDebugName, bool bCritical);

void Multiplexer_Update();


int GetNumMultiplexerConnections();
int GetNumMultiplexerMessagesRelayed();

bool Multiplexer_IsServerConnected(int iServerIndex);

typedef struct MultiplexerConnection
{
	NetLink *pLink;
	int iIndex; //MAX_MULTIPLEXER_CONNECTIONS_BITS bits of index in list, remaining bits are UID
	bool bRegistrationReceived;

	struct MultiplexerConnection *pNext;
} MultiplexerConnection;



typedef struct Multiplexer
{
	//dynamic connections requested by other servers
	//NetLinkList netLinks;

	MultiplexerConnection connections[MAX_MULTIPLEXER_CONNECTIONS];
	MultiplexerConnection *pFirstFree;

	int iCurNumConnections;
	int iTotalMessagesRelayed;
} Multiplexer;














