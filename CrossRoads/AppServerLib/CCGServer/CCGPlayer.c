/***************************************************************************
*     Copyright (c) 2009, Cryptic Studios
*     All Rights Reserved
*     Confidential Property of Cryptic Studios
***************************************************************************/

#include "CCGPlayer.h"

CCGPlayer *
CCG_CreatePlayer(U32 authToken, CCGPlayerData *playerData)
{
	CCGPlayer *player = (CCGPlayer *)malloc(sizeof(CCGPlayer));

	player->authToken = authToken;
	player->data = playerData;
	player->characterInfos = NULL;

	return player;
}