/***************************************************************************
*     Copyright (c) 2010, Cryptic Studios
*     All Rights Reserved
*     Confidential Property of Cryptic Studios
***************************************************************************/

#pragma once
GCC_SYSTEM

#include "GlobalTypes.h"

typedef struct ContainerRef ContainerRef;

typedef void (*AccountLocationCallback) (ContainerRef *pRef, void *userData);

// Finds where the accountID is logged in.  Currently supports the login server and game server.
void aslAPFindAccountLocation(U32 uAccountID, AccountLocationCallback pCallback, void *userData);