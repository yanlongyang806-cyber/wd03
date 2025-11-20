#pragma once
GCC_SYSTEM

#include "GenericGraph.h"


/////////////////////////////////////////////////////////////////////////////////////////
// Example Graph Object
/////////////////////////////////////////////////////////////////////////////////////////


// Per-node properties
AUTO_STRUCT;
typedef struct SampleGraphNode
{
	char *pchName; AST(KEY)

	char *pchDescription;
} SampleGraphNode;

AUTO_STRUCT;
typedef struct SampleGraph
{
	const char *pchName; AST(POOL_STRING KEY)

	const char *pchScope; AST(POOL_STRING)

	char *pchTags;

	const char *pchFilename; AST(CURRENTFILE)

	GenericGraph graph; AST(STRUCT(parse_GenericGraph))

	SampleGraphNode *ppNodes;

} SampleGraph;

