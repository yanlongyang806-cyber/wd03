#pragma once
/***************************************************************************
*     Copyright (c) 2012, Cryptic Studios
*     All Rights Reserved
*     Confidential Property of Cryptic Studios
***************************************************************************/

typedef U32 ContainerID;

typedef void (*RequestEntityFixupCB)(ContainerID entityID, bool succeeded, void *userData);

void aslLogin2_RequestPlayerEntityFixup(ContainerID playerEntityID, ContainerID accountID, bool fixupSharedBank, RequestEntityFixupCB cbFunc, void *userData);
void aslLogin2_EntityFixupTick(void);