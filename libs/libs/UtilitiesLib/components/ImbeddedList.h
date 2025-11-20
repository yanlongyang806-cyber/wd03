// inlinelist.h - a list intended as a client owned node paradigm, for fast list add/removal
//				intended use is to put the ImbeddedListNode as part of the structure that will be used in the list
// i.e.
/*
struct Foo
{
	S32 data;
	ImbeddedListNode	*pNode;
};
*/

#pragma once
GCC_SYSTEM

#include "stdtypes.h"

typedef struct ImbeddedList ImbeddedList;
typedef struct ImbeddedListNode ImbeddedListNode;


typedef struct ImbeddedListIterator
{
	ImbeddedListNode	*pCurNode;
	
} ImbeddedListIterator;

ImbeddedList* ImbeddedList_Create();
void ImbeddedList_Destroy(ImbeddedList **ppList);

// returns true if the node is in the list
bool ImbeddedList_IsInList(ImbeddedList *pList, ImbeddedListNode *pNode);

// pushes the given node to the front of this list. 
// Will remove the node from any list it is in regardless of the list (even if it is the list passed in)
void ImbeddedList_PushFront(ImbeddedList *pList, ImbeddedListNode *pNode);

// assumes that the InlineListNode given is a part of this list
void ImbeddedList_Remove(ImbeddedList *pList, ImbeddedListNode *pNode);

typedef void (*fpILCallback)(void *pData);
void ImbeddedList_Clear(ImbeddedList *pList, fpILCallback fpOptionalCb);


ImbeddedListNode* ImbeddedList_NodeAlloc(void *pClientPtr);
// destroys the node and removes it from the list if it is attached
void ImbeddedList_NodeFree(ImbeddedListNode **ppNode);
// returns true if the node is not a part of any list
bool ImbeddedList_NodeIsOrphaned(ImbeddedListNode *pNode);

// initializes the iterator and returns the first clientPtr in the list
void* ImbeddedList_IteratorInitialize(ImbeddedList *pList, ImbeddedListIterator *it);
// iterates to the next node in the list, returns the clientPtr, NULL if it is done iterating 
	// note: don't pass in NULL for the client pointer or iterating will get confusing...
void* ImbeddedList_IteratorGetNext(ImbeddedListIterator *it);
// returns the clientPtr of the current node
void* ImbeddedList_IteratorGetData(ImbeddedListIterator *it);