#include "ImbeddedList.h"
#include "MemoryPool.h"


AUTO_RUN_ANON(memBudgetAddMapping(__FILE__, BUDGET_EngineMisc);); 


#define INLINELIST_DBG_CHECK

typedef struct ImbeddedListNode 
{
	ImbeddedListNode	*pPrev;
	ImbeddedListNode	*pNext;
	ImbeddedList		*pParentList;
	void				*pClientPtr;
} ImbeddedListNode;

typedef struct ImbeddedList
{
	ImbeddedListNode	*pHead;
	// if a PushBack() is ever needed, keep track of the tail of the list
	
} ImbeddedList;


MP_DEFINE(ImbeddedListNode);


// --------------------------------------------------------------------------------------
ImbeddedList* ImbeddedList_Create()
{
	return calloc(1, sizeof(ImbeddedList));
}

// --------------------------------------------------------------------------------------
void ImbeddedList_Destroy(ImbeddedList **ppList)
{
	if (*ppList)
	{
		ImbeddedList_Clear(*ppList, NULL);
		free(*ppList);
	}
}


bool isInList(ImbeddedList *pList, ImbeddedListNode *pNode)
{
	ImbeddedListNode *p;

	p = pList->pHead;

	while(p)
	{
		if (p == pNode)
			return true;
		p = p->pNext;
	}

	return false;
}

// --------------------------------------------------------------------------------------
bool ImbeddedList_IsInList(ImbeddedList *pList, ImbeddedListNode *pNode)
{
	if (! pList || !pNode)
		return false;

#ifndef INLINELIST_DBG_CHECK
	devassert( (pNode->pParentList == pList) == isInList(pList, pNode));
#endif

	return (pNode->pParentList == pList);
}


// --------------------------------------------------------------------------------------
void ImbeddedList_PushFront(ImbeddedList *pList, ImbeddedListNode *pNode)
{
	if (! pList || !pNode)
		return;

	if (pNode->pParentList)
	{
		if (pNode->pParentList != pList)
		{// remove it from the old list it was in
			ImbeddedList_Remove(pNode->pParentList, pNode); 
		}
		else
		{	// already in this list
			return;
		}
	}

	pNode->pPrev = NULL;
	pNode->pNext = pList->pHead;

	if (pList->pHead)
		pList->pHead->pPrev = pNode;

	pList->pHead = pNode;
	pNode->pParentList = pList;
}


// --------------------------------------------------------------------------------------
void ImbeddedList_Remove(ImbeddedList *pList, ImbeddedListNode *pNode)
{
	if (! pList || !pNode)
		return;
	
	devassert(pNode->pParentList == pList);

	if (pNode->pPrev)
	{
		pNode->pPrev->pNext = pNode->pNext;
	}
	else
	{	// no previous, so this must be the head of the list
		devassert(pList->pHead == pNode);
		pList->pHead = pNode->pNext;
	}

	if (pNode->pNext)
	{
		pNode->pNext->pPrev = pNode->pPrev;
	}

	pNode->pPrev = NULL;
	pNode->pNext = NULL;
	pNode->pParentList = NULL;
}


// --------------------------------------------------------------------------------------
void ImbeddedList_Clear(ImbeddedList *pList, fpILCallback fpOptionalCb)
{
	ImbeddedListNode *pNode;
	
	if (! pList)
		return;
		
	pNode = pList->pHead;
	while(pNode)
	{
		ImbeddedListNode *pRem = pNode;
		
		if (fpOptionalCb)
		{
			fpOptionalCb(pNode->pClientPtr);
		}

		pNode = pNode->pNext;
		
		// orphan the nodes, but we cannot free them since the client owns them
		pRem->pPrev = NULL;
		pRem->pNext = NULL;
		pRem->pParentList = NULL;
	}

	pList->pHead = NULL;
}



// --------------------------------------------------------------------------------------
// ImbeddedListNode functions
// --------------------------------------------------------------------------------------
ImbeddedListNode* ImbeddedList_NodeAlloc(void *pClientPtr)
{
	ImbeddedListNode* pNode;

	MP_CREATE(ImbeddedListNode, 20);
	
	pNode = MP_ALLOC(ImbeddedListNode);
	pNode->pClientPtr = pClientPtr;

	return pNode;
}

// --------------------------------------------------------------------------------------
void ImbeddedList_NodeFree(ImbeddedListNode **ppNode)
{
	if (*ppNode)
	{
		// if it still in a list, remove it
		if ((*ppNode)->pParentList)
		{
			ImbeddedList_Remove((*ppNode)->pParentList, *ppNode);
		}

		MP_FREE(ImbeddedListNode, *ppNode);
	}

}

// --------------------------------------------------------------------------------------
bool ImbeddedList_NodeIsOrphaned(ImbeddedListNode *pNode)
{
	devassert(pNode);

	return pNode->pParentList == NULL;
}


// --------------------------------------------------------------------------------------
// ImbeddedListIterator
// --------------------------------------------------------------------------------------
void* ImbeddedList_IteratorInitialize(ImbeddedList *pList, ImbeddedListIterator *it)
{
	devassert(it);
	if (!pList)
	{
		it->pCurNode = NULL;
		return NULL;
	}

	it->pCurNode = pList->pHead;
	if (it->pCurNode)
	{
		return it->pCurNode->pClientPtr;
	}
	return NULL;
}


// --------------------------------------------------------------------------------------
void* ImbeddedList_IteratorGetNext(ImbeddedListIterator *it)
{
	devassert (it);
	if (it->pCurNode)
	{
		it->pCurNode = it->pCurNode->pNext;
		return (it->pCurNode) ? it->pCurNode->pClientPtr : NULL;
	}

	return NULL;
}

// --------------------------------------------------------------------------------------
void* ImbeddedList_IteratorGetData(ImbeddedListIterator *it)
{
	devassert(it);
	return (it->pCurNode) ? it->pCurNode->pClientPtr : NULL;
}