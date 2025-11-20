#pragma once
/***************************************************************************
*     Copyright (c) 2012, Cryptic Studios
*     All Rights Reserved
*     Confidential Property of Cryptic Studios
***************************************************************************/

typedef U32 ContainerID;
typedef struct Login2CharacterDetail Login2CharacterDetail;

typedef void (*GetCharacterDetailCB)(Login2CharacterDetail *characterDetail, void *userData);

void aslLogin2_GetCharacterDetail(ContainerID accountID, ContainerID playerID, const char *shardName, bool returnActivePuppets, GetCharacterDetailCB cbFunc, void *userData);
