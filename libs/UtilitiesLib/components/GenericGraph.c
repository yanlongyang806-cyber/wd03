#include "GenericGraph.h"
#include "qsortG.h"

AUTO_RUN_ANON(memBudgetAddMapping(__FILE__, BUDGET_GameSystems););

#include "AutoGen/GenericGraph_h_ast.h"

AUTO_FIXUPFUNC;
TextParserResult GraphNodeFixup(GraphNode *pNode, enumTextParserFixupType eType, void *pExtraData)
{
	switch (eType)
	{
	case FIXUPTYPE_DESTRUCTOR:
		eaDestroy(&pNode->ppIncomingCache);
		eaDestroy(&pNode->ppOutgoingCache);
		break;
	}
	return PARSERESULT_SUCCESS;
}

static bool graphNodeAddLink(GraphNode *pNode, const char *pchNode)
{
	if (pNode)
	{	
		int index = (int)eaBFind(pNode->ppchIncoming, strCmp, pchNode);

		if (!pNode->ppchIncoming || index == eaSize(&pNode->ppchIncoming) || stricmp(pchNode, pNode->ppchIncoming[index]) != 0)
		{				
			eaInsert(&pNode->ppchIncoming, strdup(pchNode), index);
			return true;
		}
	}
	return false;
}

static bool graphNodeCheckLink(GraphNode *pNode, const char *pchNode)
{
	const char *pFound = eaBSearch(pNode->ppchIncoming, strCmp, pchNode);
	return !!pFound;
}

static bool graphNodeRemoveLink(GraphNode *pNode, const char *pchNode)
{
	if (pNode)
	{	
		int index = (int)eaBFind(pNode->ppchIncoming, strCmp, pchNode);

		if (pNode->ppchIncoming && index < eaSize(&pNode->ppchIncoming) && stricmp(pchNode, pNode->ppchIncoming[index]) == 0)
		{				
			free(eaRemove(&pNode->ppchIncoming, index));
			return true;
		}
	}
	return false;
}

GraphNode *graphGetNode(GenericGraph *pGraph, const char *pchNode)
{
	if (!pGraph || !pchNode)
		return NULL;
	return eaIndexedGetUsingString(&pGraph->ppGraphNodes, pchNode);
}

GraphNode *graphGetOrCreateNode(GenericGraph *pGraph, const char *pchNode)
{
	GraphNode *pNode;
	if (!pGraph || !pchNode)
		return NULL;
	pNode = eaIndexedGetUsingString(&pGraph->ppGraphNodes, pchNode);
	if (pNode)
	{
		return pNode;
	}
	pNode = StructCreate(parse_GraphNode);
	pNode->pchName = strdup(pchNode);
	eaPush(&pGraph->ppGraphNodes, pNode);
	return pNode;
}

bool graphAddNode(GenericGraph *pGraph, const char *pchNode)
{
	GraphNode *pNode = graphGetNode(pGraph, pchNode);
	if (pNode)
		return false;
	pNode = graphGetOrCreateNode(pGraph, pchNode);
	if (pNode)
	{
		if (pGraph->bCached)
			graphBuildCaches(pGraph);
		return true;
	}
	return false;
}

bool graphRenameNode(GenericGraph *pGraph, const char *pchOldName, const char *pchNewName)
{
	GraphNode *pOldNode;
	GraphNode *pNewNode;
	int i;
	if (!pGraph || !pchOldName || !pchNewName)
	{
		return false;
	}
	pOldNode = graphGetNode(pGraph, pchOldName);
	if (!pOldNode)
	{
		return false;
	}
	pNewNode = graphGetOrCreateNode(pGraph, pchNewName);
	if (!pNewNode || pNewNode == pOldNode)
	{
		return false;
	}
	for (i = 0; i < eaSize(&pGraph->ppGraphNodes); i++)
	{
		GraphNode *pNode = pGraph->ppGraphNodes[i];
		if (graphNodeRemoveLink(pNode, pchOldName))
		{
			graphNodeAddLink(pNode, pchNewName);
		}
	}
	// Copy over the already allocated and sorted strings, then delete the old one
	eaCopy(&pNewNode->ppchIncoming, &pOldNode->ppchIncoming);
	eaDestroy(&pOldNode->ppchIncoming);

	eaFindAndRemove(&pGraph->ppGraphNodes, pOldNode);
	StructDestroy(parse_GraphNode, pOldNode);
	if (pGraph->bCached)
		graphBuildCaches(pGraph);
	return true;
}

bool graphRemoveNode(GenericGraph *pGraph, const char *pchNode)
{
	GraphNode *pOldNode;
	int i;
	if (!pGraph || !pchNode)
	{
		return false;
	}
	pOldNode = graphGetNode(pGraph, pchNode);
	if (!pOldNode)
	{
		return false;
	}
	for (i = 0; i < eaSize(&pGraph->ppGraphNodes); i++)
	{
		GraphNode *pNode = pGraph->ppGraphNodes[i];
		graphNodeRemoveLink(pNode, pchNode);
	}
	eaFindAndRemove(&pGraph->ppGraphNodes, pOldNode);
	StructDestroy(parse_GraphNode, pOldNode);
	if (pGraph->bCached)
		graphBuildCaches(pGraph);
	return true;
}

bool graphAddLink(GenericGraph *pGraph, const char *pchStart, const char *pchDest)
{
	bool ret;
	GraphNode *pSource, *pDest;
	if (!pGraph || !pchStart || !pchDest)
	{
		return false;
	}
	pSource = graphGetOrCreateNode(pGraph, pchStart);
	pDest = graphGetOrCreateNode(pGraph, pchDest);

	ret = graphNodeAddLink(pDest, pchStart);
	if (pGraph->bCached)
		graphBuildCaches(pGraph);
	return ret;
}


bool graphRemoveLink(GenericGraph *pGraph, const char *pchStart, const char *pchDest)
{
	bool ret;
	GraphNode *pSource, *pDest;
	if (!pGraph || !pchStart || !pchDest)
	{
		return false;
	}
	pSource = graphGetNode(pGraph, pchStart);
	pDest = graphGetNode(pGraph, pchDest);

	if (!pSource || !pDest)
	{
		return false;
	}
	ret = graphNodeRemoveLink(pDest, pchStart);
	if (pGraph->bCached)
		graphBuildCaches(pGraph);
	return ret;
}

bool graphDoesLinkExist(GenericGraph *pGraph, const char *pchStart, const char *pchDest)
{
	GraphNode *pSource, *pDest;
	if (!pGraph || !pchStart || !pchDest)
	{
		return false;
	}
	pSource = graphGetNode(pGraph, pchStart);
	pDest = graphGetNode(pGraph, pchDest);

	if (!pSource || !pDest)
	{
		return false;
	}
	return graphNodeCheckLink(pDest, pchStart);
}

// Build the caches on the graph
void graphBuildCaches(GenericGraph *pGraph)
{
	int i;

	for (i = 0; i < eaSize(&pGraph->ppGraphNodes); i++)
	{
		GraphNode *pNode = pGraph->ppGraphNodes[i];
		
		if (pNode->ppIncomingCache)					
			eaClear(&pNode->ppIncomingCache);
		else
			eaIndexedEnable(&pNode->ppIncomingCache, parse_GraphNode);

		if (pNode->ppOutgoingCache)
			eaClear(&pNode->ppOutgoingCache);
		else
			eaIndexedEnable(&pNode->ppOutgoingCache, parse_GraphNode);
	}

	for (i = 0; i < eaSize(&pGraph->ppGraphNodes); i++)
	{
		int j;
		GraphNode *pNode = pGraph->ppGraphNodes[i];
		for (j = 0; j < eaSize(&pNode->ppchIncoming); j++)
		{
			GraphNode *pOtherNode = graphGetNode(pGraph, pNode->ppchIncoming[j]);
			if (!pOtherNode)
				continue;
			eaPush(&pNode->ppIncomingCache, pOtherNode);
			eaPush(&pOtherNode->ppOutgoingCache, pNode);
		}
	}

	pGraph->bCached = true;
}

typedef enum 
{
	kGraphIter_None, // Invalid iterator
	kGraphIter_Outgoing,
	kGraphIter_Incoming,
	kGraphIter_AllNodes,
} GraphIterType;

void graphIterInitOutgoingLinks(GraphIterator *pIter, GenericGraph *pGraph, const char *pchStart)
{
	assert(pGraph && pIter && pchStart);
	if (!pGraph->bCached)
		graphBuildCaches(pGraph);
	pIter->index = 0;
	pIter->iterMode = kGraphIter_Outgoing;
	pIter->pGraph = pGraph;
	pIter->pNode = graphGetNode(pGraph, pchStart);
}

void graphIterInitIncomingLinks(GraphIterator *pIter, GenericGraph *pGraph, const char *pchStart)
{
	assert(pGraph && pIter && pchStart);
	if (!pGraph->bCached)
		graphBuildCaches(pGraph);
	pIter->index = 0;
	pIter->iterMode = kGraphIter_Incoming;
	pIter->pGraph = pGraph;
	pIter->pNode = graphGetNode(pGraph, pchStart);
}

void graphIterInitAllNodes(GraphIterator *pIter, GenericGraph *pGraph)
{
	assert(pGraph && pIter);
	if (!pGraph->bCached)
		graphBuildCaches(pGraph);
	pIter->index = 0;
	pIter->iterMode = kGraphIter_AllNodes;
	pIter->pGraph = pGraph;
	pIter->pNode = NULL;
}

const char *graphIterGetNext(GraphIterator *pIter)
{
	GraphNode *pNode;
	const char *pReturn = NULL;
	switch(pIter->iterMode)
	{
	xcase kGraphIter_Outgoing:
		if (!pIter->pGraph || !pIter->pNode)
		{
			break;
		}
		pNode = eaGet(&pIter->pNode->ppOutgoingCache, pIter->index);
		if (pNode)
		{
			pReturn = pNode->pchName;
			pIter->index++;
		}
	xcase kGraphIter_Incoming:
		if (!pIter->pGraph || !pIter->pNode)
		{
			break;
		}
		pNode = eaGet(&pIter->pNode->ppIncomingCache, pIter->index);
		if (pNode)
		{
			pReturn = pNode->pchName;
			pIter->index++;
		}
	xcase kGraphIter_AllNodes:
		if (!pIter->pGraph)
		{
			break;
		}
		pNode = eaGet(&pIter->pGraph->ppGraphNodes, pIter->index);
		if (pNode)
		{
			pReturn = pNode->pchName;
			pIter->index++;
		}			
	}

	if (!pReturn)
	{
		pIter->iterMode = kGraphIter_None;
		pIter->index = 0;
		pIter->pGraph = NULL;
		pIter->pNode = NULL;
	}
	return pReturn;

}

#include "AutoGen/GenericGraph_h_ast.c"