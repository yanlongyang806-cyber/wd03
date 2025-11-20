/***************************************************************************
*     Copyright (c) 2009, Cryptic Studios
*     All Rights Reserved
*     Confidential Property of Cryptic Studios
***************************************************************************/
#pragma once

typedef struct CCGCharacterInfos CCGCharacterInfos;
typedef struct CCGPlayerData CCGPlayerData;

typedef struct CCGPlayer
{
	// The authentication token that the flash client sends with every
	//  connection to identify itself.  The CCG server creates it at
	//  the request of the web site after the player has logged on, and
	//  the web site then passes it to the flash client.
	U32 authToken;

	// The character info used to create a new hero card.
	// It is normally NULL, but we cache it so that we don't need to
	//  repeat the rather expensive database operation needed to generate it.
	CCGCharacterInfos *characterInfos;

	// The persistent player data, including cards, decks and stats.
	CCGPlayerData *data;
} CCGPlayer;

CCGPlayer *CCG_CreatePlayer(U32 authToken, CCGPlayerData *playerData);