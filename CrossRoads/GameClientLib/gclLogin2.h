#pragma once
/***************************************************************************
*     Copyright (c) 2012, Cryptic Studios
*     All Rights Reserved
*     Confidential Property of Cryptic Studios
***************************************************************************/

typedef U32 ContainerID;
typedef struct Login2CharacterDetail Login2CharacterDetail;
typedef struct Entity Entity;
typedef enum CharClassTypes CharClassTypes;

AUTO_ENUM;
typedef enum GCLLogin2FetchResult
{
    FetchResult_Succeeded,
    FetchResult_Failed,
    FetchResult_Timeout,
    FetchResult_Pending,
} GCLLogin2FetchResult;

typedef void (*FetchEntityDetailCB)(ContainerID playerID, GCLLogin2FetchResult result, void *userData);

void gclLogin2_CharacterDetailCache_Clear(void);
Entity *gclLogin2_CharacterDetailCache_GetEntity(ContainerID characterID);
Login2CharacterDetail *gclLogin2_CharacterDetailCache_Get(ContainerID characterID);
Entity *gclLogin2_CharacterDetailCache_GetPuppet(ContainerID playerID, CharClassTypes puppetClassType);
void gclLogin2_CharacterDetailCache_Add(Login2CharacterDetail *characterDetail);
void gclLogin2_CharacterDetailCache_Fetch(ContainerID characterID, FetchEntityDetailCB cbFunc, void *cbData);
