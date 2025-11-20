#ifndef GENERICGRAPH_H
#define GENERICGRAPH_H
#pragma once
GCC_SYSTEM

// This file describes a generic graph structure, that can be embedded into other objects
// and then analyzed in various ways

// Special node names, that don't correspond to actual nodes

#define SPECIAL_NODE_START	"StartNode"
#define SPECIAL_NODE_END	"EndNode"


// Structure of the generic graph

// It is safe to create, destroy, and copy GenericGraphs via textparser, but do NOT modify
// them directly. Use the accessors below.

typedef struct GraphNode GraphNode;

AUTO_STRUCT;
typedef struct GraphNode {

	char *pchName; AST(KEY) // The internal name of this graph node

	char **ppchIncoming; // List of inbound node names, SORTED. DO NOT directly access!

	GraphNode **ppIncomingCache; NO_AST
	GraphNode **ppOutgoingCache; NO_AST

} GraphNode;

AUTO_STRUCT;
typedef struct GenericGraph {

	GraphNode **ppGraphNodes;

	bool bCached; NO_AST // If this is true, the caches are up to date

} GenericGraph;

extern ParseTable parse_GenericGraph[];
#define TYPE_parse_GenericGraph GenericGraph

// Finds a graph node
GraphNode *graphGetNode(GenericGraph *pGraph, const char *pchNode);

// Remove a node, as well as all connections to/from it
bool graphRemoveNode(GenericGraph *pGraph, const char *pchNode);

// Adds a new node
bool graphAddNode(GenericGraph *pGraph, const char *pchNode);

// Renames a node, and fixes up all of the links
bool graphRenameNode(GenericGraph *pGraph, const char *pchOldName, const char *pchNewName);

// Add a graph connection
bool graphAddLink(GenericGraph *pGraph, const char *pchStart, const char *pchDest);

// Remove a graph connection
bool graphRemoveLink(GenericGraph *pGraph, const char *pchStart, const char *pchDest);

// Build the caches on the graph
void graphBuildCaches(GenericGraph *pGraph);

// Return if the specified link exists
bool graphDoesLinkExist(GenericGraph *pGraph, const char *pchStart, const char *pchDest);


// Iterate over various graph properties
// These iterators are invalidated by various graph operations!

typedef struct GraphIterator
{
	int iterMode;
	GenericGraph *pGraph;
	GraphNode *pNode;
	U32 index;
} GraphIterator;


// Starts iterating over outgoing connections
void graphIterInitOutgoingLinks(GraphIterator *pIter, GenericGraph *pGraph, const char *pchStart);

// Iterate over incoming
void graphIterInitIncomingLinks(GraphIterator *pIter, GenericGraph *pGraph, const char *pchStart);

// Iterate over entire graph
void graphIterInitAllNodes(GraphIterator *pIter, GenericGraph *pGraph);

// Returns next link (ie, name of node) or NULL if iteration is done
const char *graphIterGetNext(GraphIterator *pIter);


#endif
