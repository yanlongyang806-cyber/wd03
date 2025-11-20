#pragma once
GCC_SYSTEM
/***************************************************************************
*     Copyright (c) 2008, Cryptic Studios
*     All Rights Reserved
*     Confidential Property of Cryptic Studios
***************************************************************************/

typedef struct Packet Packet;
typedef struct MapState MapState;
typedef struct WorldInteractionNode WorldInteractionNode;

// Client mapstate receive function
void mapState_ClientReceiveMapStateFromPacket(Packet* pak);

// Demo versions
void mapState_InitialRecordForDemo(void);
void mapState_ClientReceiveMapStateFullFromDemo(MapState* mapState, U32 id);
void mapState_ClientReceiveMapStateDiffFromDemo(char* diffString, U32 id);
void mapState_ClientReceiveMapStateDestroyFromDemo(U32 id);

bool mapState_IsNodeHiddenOrDisabled(WorldInteractionNode *pNode);
void mapState_SetNodeVisibleOverride(WorldInteractionNode *pNode, bool bVisible);

void mapState_ClientResetVisibleChildAllNextFrame(void);

MapState *mapStateClient_Get();

void mapState_RegisterDictListeners(void);
void mapState_OncePerFrame(void);

void mapState_MapLoad(void);
void mapState_MapUnload(void);

