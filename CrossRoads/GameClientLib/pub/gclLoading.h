#pragma once
GCC_SYSTEM
/***************************************************************************
*     Copyright (c) 2005-2006, Cryptic Studios
*     All Rights Reserved
*     Confidential Property of Cryptic Studios
***************************************************************************/

#ifndef GCLLOADING_H_
#define GCLLOADING_H_

typedef struct Packet Packet;

// Deals with a server sending down what address to connect to
void HandleReturnedServerAddress(Packet *pak);

// Connection to the GameServer succeeded
void HandleServerConnectSuccess(Packet *pak);

// Connection to the GameServer failed
void HandleServerConnectFailure(Packet *pak);

// Start a map transfer, which means to shift to the loading state
void HandleStartTransfer(Packet *pak);

S32 gclHasReturnedServerAddress(void);

void gclLoadingHandleDisconnect(S32 isUnexpectedDisconnect);

F32 gclLoadingScreenGetProgress(void);

bool gclLoadingIsStillLoading(void);

#endif
