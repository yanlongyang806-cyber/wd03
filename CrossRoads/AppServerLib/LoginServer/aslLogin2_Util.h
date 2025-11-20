#pragma once
/***************************************************************************
*     Copyright (c) 2013, Cryptic Studios
*     All Rights Reserved
*     Confidential Property of Cryptic Studios
***************************************************************************/

typedef U32 ContainerID;
typedef enum GlobalType GlobalType;

ContainerID aslLogin2_GetRandomServerOfTypeInShard(const char *shardName, GlobalType serverType);