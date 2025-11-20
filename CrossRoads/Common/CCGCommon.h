/***************************************************************************
*     Copyright (c) 2009, Cryptic Studios
*     All Rights Reserved
*     Confidential Property of Cryptic Studios
***************************************************************************/

#pragma once
AUTO_STRUCT;
typedef struct CCGCharacterInfo
{
	ContainerID id;
	char name[MAX_NAME_LEN];	AST( KEY )

	// When returning from the db server, this is a textparser string that contains all the character's powers
	// When it gets to the CCG server we parse it and extract just the primary power set name,
	//  which is what this string will contain.
	char *powerTree;			AST( ESTRING )
} CCGCharacterInfo;

AUTO_STRUCT;
typedef struct CCGCharacterInfos
{
	CCGCharacterInfo **charInfo;
} CCGCharacterInfos;