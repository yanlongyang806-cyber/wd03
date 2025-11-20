#pragma once

#include "globalTypes.h"

AUTO_STRUCT AST_CONTAINER;
typedef struct VirtualShard
{
	const ContainerID id; AST(KEY PERSIST SUBSCRIBE)
	const bool bUGCShard; AST(PERSIST SUBSCRIBE)
	CONST_STRING_MODIFIABLE pName; AST(PERSIST SUBSCRIBE)
	const bool bNoPVPQueues; AST(PERSIST SUBSCRIBE)
	const bool bNoAuctions; AST(PERSIST SUBSCRIBE)
	const bool bDisabled; AST(PERSIST SUBSCRIBE)
} VirtualShard;