#pragma once
GCC_SYSTEM

typedef U32 ContainerID;

AUTO_STRUCT;
typedef struct PersistedStorePlayerRequest
{
	ContainerID uPlayerID; AST(KEY)
	U32 uNextCheckTime;
} PersistedStorePlayerRequest;

AUTO_STRUCT;
typedef struct PersistedStoreRequest
{
	ContainerID uContainerID;
	PersistedStorePlayerRequest** eaPlayers;
	U32 uNextPlayerCheckTime;
} PersistedStoreRequest;

void aslPersistedStores_Load(void);
void aslPersistedStores_Tick(void);