/***************************************************************************
*     Copyright (c) 2005-2006, Cryptic Studios
*     All Rights Reserved
*     Confidential Property of Cryptic Studios
***************************************************************************/
#ifndef CMDSERVER_CHARACTER_H_
#define CMDSERVER_CHARACTER_H_

//forward declarations
typedef struct Entity		Entity;
typedef struct PowerTable	PowerTable;
typedef struct PowerTreeDef PowerTreeDef;
typedef struct PTNodeDef	PTNodeDef;
typedef struct TimedCallback TimedCallback;
typedef enum AttribType		AttribType;
typedef enum GlobalType		GlobalType;

typedef void (*TimedCallbackFunc)(TimedCallback *pCallback, F32 timeSinceLastCallback, void *userData);

bool character_SetStatPointsByEnum(Entity *e, SA_PARAM_NN_STR const char *pchStatPointPoolName, AttribType eAttrib, int iPoints);
bool character_ModifyStatPointsByEnum(Entity *e, SA_PARAM_NN_STR const char *pchStatPointPoolName, AttribType eAttrib, int iPoints);

void gsl_GenericContainerRaider(GlobalType eType, int div, int mod, TimedCallbackFunc completeCB, void *userData);
void gsl_GenericPercoRaider(GlobalType eType, char *perc_id, int count, int index, int raidsPerFrame, F32 scaleback, TimedCallbackFunc completeCB, TimedCallbackFunc containerCB, void *userData);
void BuyCharacterPowersUsingPath(Entity* pEntity);

// Needed by UGC:
void SetExpLevelUsingCharacterPath(Entity *pClientEntity, int iLevelValue);

typedef U32 ContainerID;
typedef struct ContainerRaidBaton ContainerRaidBaton;
typedef struct BatonHolder
{
	GlobalType type;
	ContainerID id;
	ContainerRaidBaton *pBaton;
} BatonHolder;

AUTO_STRUCT;
typedef struct AutoLevelNodeData
{
	PTNodeDef* pNodeDef;	AST(UNOWNED)
	PowerTreeDef* pTreeDef;	AST(UNOWNED)
	int iRank;
	int iRandomIndex;
} AutoLevelNodeData;

AUTO_STRUCT;
typedef struct StatPointCartUpdateCBData
{
	EntityRef erEnt;
} StatPointCartUpdateCBData;

char **FindReplayLogs(const char *pDir);

#endif 
