/***************************************************************************
*     Copyright (c) 2005-2006, Cryptic Studios
*     All Rights Reserved
*     Confidential Property of Cryptic Studios
***************************************************************************/

#ifndef GCLSENDTOSERVER_H_
#define GCLSENDTOSERVER_H_
#pragma once
GCC_SYSTEM

#include "GlobalComm.h"
#include "timing.h"
#include "EString.h"
#include "UtilitiesLibEnums.h"

typedef struct CmdParseStructList CmdParseStructList;

typedef struct GCLServerConnectionInfo {
	U32 				ip;
	U32 				port;
	U32 				disconnectErrorCode;
	
	struct {
		struct {
			U32			compressed;
			U32 		uncompressed;
		} sent, received;
	} bytes;
	
	struct {
		U32				disconnectedFromClient : 1;
	} flags;
} GCLServerConnectionInfo;

typedef enum enumCmdContextHowCalled enumCmdContextHowCalled;
extern Packet *gClientInputPak;

#define START_INPUT_PACKET(ent, pakx, cmdNumber) {									\
	Packet *pakx;																	\
	if (!gClientInputPak)															\
	{																				\
		gclServerNewInputPacket();													\
		entSendRef(gClientInputPak,entGetRef(ent));									\
	}																				\
	assert(gClientInputPak);														\
	pakx = gClientInputPak;															\
	START_BIT_COUNT(gClientInputPak, "send:" #cmdNumber);							\
	pktSendBits(gClientInputPak, 1, !!(cmdNumber & LIB_MSG_BIT));					\
	pktSendBitsPack(gClientInputPak, GAME_MSG_SENDBITS, cmdNumber & ~LIB_MSG_BIT);

#define END_INPUT_PACKET															\
	STOP_BIT_COUNT(gClientInputPak);												\
}

// Monitor the connection to the game server
void gclServerMonitorConnection(void);

// Re-request a world update
void gclServerRequestWorldUpdate(int full_update);

// Send updates for all the client-side locked layers
void gclServerSendLockedUpdates(void);

// Is the client currently connected?
S32 gclServerIsConnected(void);
S32 gclServerIsDisconnected(void);

// How long since a server packet was received.
F32 gclServerTimeSinceRecv(void);

// Create a real packet that is sent with pktSend.
Packet* gclServerCreateRealPacket(U32 cmd);

// Create a new packet shared input packet if there isn't one.
void gclServerNewInputPacket(void);

// Destroy and free the input packet... make sure to do this whenever the
//serverlink is destroyed
void gclServerDestroyInputPacket(void);

// Main loop for sending your information to the server
void gclServerSendInputPacket(F32 deltaSeconds);

// Send cmdparse commands to the server
// You should probably be using the AUTO_COMMAND wrappers instead
void gclSendPublicCommand(CmdContextFlag iFlags, const char *s, enumCmdContextHowCalled eHow, CmdParseStructList *pStructs);
void gclSendPrivateCommand(CmdContextFlag iFlags, const char *s, enumCmdContextHowCalled eHow, CmdParseStructList *pStructs);
void gclSendPublicCommandf(CmdContextFlag iFlags, FORMAT_STR const char *fmt, ...);
#define gclSendPublicCommandf(iFlags, fmt, ...) gclSendPublicCommandf(iFlags, FORMAT_STRING_CHECKED(fmt), __VA_ARGS__)
void gclSendPrivateCommandf(CmdContextFlag iFlags, FORMAT_STR const char *fmt, ...);
#define gclSendPrivateCommandf(iFlags, fmt, ...) gclSendPrivateCommandf(iFlags, FORMAT_STRING_CHECKED(fmt), __VA_ARGS__)

typedef struct ChatAuthData ChatAuthData;
void gclSendChatRelayCommand(CmdContextFlag iFlags, const char *s, bool bPrivate, enumCmdContextHowCalled eHow, CmdParseStructList *pStructs);
void gclChatConnect_ReceiveData(ChatAuthData *data);
void gclChatConnect_Tick(void);
void gclChatConnect_Logout(void);
// Immediately attempt to connect chat
// Called on login/game server link establishment so there is no lag in chat connection due to retry timeouts
void gclChatConnect_RetryImmediate(void);
bool gclChatIsConnected(void);
void gclChatConnect_LoginDone(void);

// Send Bug/Ticket + Screenshot to server
int gclServerSendTicketAndScreenshot(U32 uType, const char *pData, const char *pExtraData, const char *pImageBuffer, U32 uImageSize);

// Forcibly disconnect from the server.
void gclServerForceDisconnect(const char* reason);

// Start a server connection.
S32 gclServerConnect(const char* serverName, U32 serverPort);

void gclServerGetConnectionInfo(const GCLServerConnectionInfo** infoCurOut,
								const GCLServerConnectionInfo** infoPrevOut);

U32 gclServerMyIPOnTheServer(void);

F32 gclServerGetCurrentPing();

#endif