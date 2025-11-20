#pragma once
/***************************************************************************
*     Copyright (c) 2012, Cryptic Studios
*     All Rights Reserved
*     Confidential Property of Cryptic Studios
***************************************************************************/

typedef struct Login2InterShardDestination Login2InterShardDestination;
typedef struct Login2CharacterDetailDBReturn Login2CharacterDetailDBReturn;
typedef struct ContainerRef ContainerRef;
typedef struct GWTCmdPacket GWTCmdPacket;

AUTO_STRUCT;
typedef struct DBGetCharacterDetailState
{
	ContainerID accountID;
	ContainerID playerID;
	bool returnActivePuppets;
	Login2InterShardDestination *returnDestination;

	Login2CharacterDetailDBReturn *detailReturn;

	bool failed;

	// String used to record errors for logging.
	STRING_MODIFIABLE errorString;          AST(ESTRING)

	ContainerRef **puppetRefs;
} DBGetCharacterDetailState;

AUTO_STRUCT;
typedef struct DBOnlineCharacterState
{
	SlowRemoteCommandID commandID;
} DBOnlineCharacterState;

void LoginCharacterRestoreTick(void);

void dbLogin2_UnpackCharacter(GWTCmdPacket *packet, DBGetCharacterDetailState *getDetailState);
void dbLogin2_UnpackPets(DBGetCharacterDetailState *getDetailState);
void dbLogin2_QueueUnpackPets(DBGetCharacterDetailState *getDetailState);