#pragma once
/***************************************************************************
*     Copyright (c) 2012, Cryptic Studios
*     All Rights Reserved
*     Confidential Property of Cryptic Studios
***************************************************************************/

typedef U32 ContainerID;
typedef enum GlobalType GlobalType;

typedef void (*RenameCharacterCB)(bool success, U64 userData);

// Rename a character from a remote shard.
void aslLogin2_RenameCharacter(ContainerID playerID, ContainerID accountID, GlobalType puppetType, ContainerID puppetID, const char *newName, bool badName, const char *shardName, RenameCharacterCB cbFunc, U64 userData);
