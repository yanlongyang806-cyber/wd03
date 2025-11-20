
#include "AssociationList.h"
#include "MemoryPool.h"
#include "WinInclude.h"
#include "mutex.h"

typedef struct AssociationList {
	void*						listOwner;
	const AssociationListType*	listType;
	AssociationNode*			head;
} AssociationList;

typedef struct AssociationNode {
	struct {
		AssociationNode*		next;
		AssociationNode*		prev;
	} node1, node2;

	AssociationList*			list1;
	AssociationList*			list2;
	void*						userPointer;
} AssociationNode;

typedef struct AssociationListIterator {
	AssociationList*			al;
	AssociationNode*			node;
	
	struct {
		U32						isPrimary		: 1;
		U32						ignoreNextGoto	: 1;
	} flags;
} AssociationListIterator;

static CrypticalSection csAL;

static void alEnterCS(void){
	csEnter(&csAL);
}

static void alLeaveCS(void){
	csLeave(&csAL);
}

MP_DEFINE(AssociationList);

void alCreate(	AssociationList** alOut,
				void* listOwner,
				const AssociationListType* listType)
{
	AssociationList* al;

	if(!alOut){
		return;
	}
	
	alEnterCS();
		MP_CREATE_COMPACT(AssociationList, 100, 200, 0.80);
		
		al = *alOut = MP_ALLOC(AssociationList);
	alLeaveCS();

	al->listOwner = listOwner;
	al->listType = listType;
}

void alDestroy(AssociationList** alInOut){
	AssociationList* al = SAFE_DEREF(alInOut);
	
	if(al){
		*alInOut = NULL;

		alRemoveAll(al);
		
		alEnterCS();
			MP_FREE(AssociationList, al);
		alLeaveCS();
	}
}

MP_DEFINE(AssociationNode);

void alAssociate(	AssociationNode** nodeOut,
					AssociationList* al1,
					AssociationList* al2,
					void* userPointer)
{
	AssociationNode* node;
	
	assert(	al1 &&
			al1->listType &&
			al1->listType->flags.isPrimary &&
			al2 &&
			al2->listType &&
			!al2->listType->flags.isPrimary);
	
	alEnterCS();
		MP_CREATE_COMPACT(AssociationNode, 500, 1000, 0.80);
	
		node = MP_ALLOC(AssociationNode);
	alLeaveCS();
	
	node->list1 = al1;
	node->list2 = al2;
	node->userPointer = userPointer;

	if(al1->head){
		al1->head->node1.prev = node;
		node->node1.next = al1->head;
	}
	
	al1->head = node;

	if(al2->head){
		al2->head->node2.prev = node;
		node->node2.next = al2->head;
	}
	
	al2->head = node;
	
	if(nodeOut){
		*nodeOut = node;
	}
}

S32 alIsEmpty(AssociationList* al){
	return !SAFE_MEMBER(al, head);
}

S32 alGetOwner(	void** listOwnerOut,
				AssociationList* al)
{
	if(	!listOwnerOut ||
		!al)
	{
		return 0;
	}
	
	*listOwnerOut = al->listOwner;
	
	return 1;
}

S32 alNodeGetValues(AssociationNode* node,
					AssociationList** al1Out,
					AssociationList** al2Out,
					void** userPointerOut)
{
	if(!node){
		return 0;
	}
	
	if(al1Out){
		*al1Out = node->list1;
	}

	if(al2Out){
		*al2Out = node->list2;
	}

	if(userPointerOut){
		*userPointerOut = node->userPointer;
	}
	
	return 1;
}

S32 alNodeGetOwners(AssociationNode* node,
					void** owner1Out,
					void** owner2Out)
{
	if(!node){
		return 0;
	}
	
	if(owner1Out){
		*owner1Out = node->list1->listOwner;
	}

	if(owner2Out){
		*owner2Out = node->list2->listOwner;
	}
	
	return 1;
}

S32	alNodeSetUserPointer(	AssociationNode* node,
							void* userPointer)
{
	if(!node){
		return 0;
	}
	
	node->userPointer = userPointer;

	return 1;
}

static void alNotifyListOwner(AssociationList* al){
	const AssociationListType* listType = SAFE_MEMBER(al, listType);
	
	if(	al &&
		!al->head &&
		SAFE_MEMBER(listType, notifyEmptyFunc))
	{
		listType->notifyEmptyFunc(al->listOwner, al);
	}
}

static void destroyUserPointer(AssociationNode* node){
	const AssociationListType* listType = node->list1->listType;

	if(SAFE_MEMBER(listType, userPointerDestructor)){
		listType->userPointerDestructor(node->list1->listOwner,
										node->list1,
										node,
										node->userPointer);
	}
}
						
S32 alNodeRemove(AssociationNode* node){
	if(!node){
		return 0;
	}
	
	if(node->node1.next){
		node->node1.next->node1.prev = node->node1.prev;
	}

	if(node->node1.prev){
		node->node1.prev->node1.next = node->node1.next;
	}else{
		assert(node->list1->head == node);
		node->list1->head = node->node1.next;
	}
	
	if(node->node2.next){
		node->node2.next->node2.prev = node->node2.prev;
	}

	if(node->node2.prev){
		node->node2.prev->node2.next = node->node2.next;
	}else{
		assert(node->list2->head == node);
		node->list2->head = node->node2.next;
	}
	
	destroyUserPointer(node);

	alNotifyListOwner(node->list1);
	alNotifyListOwner(node->list2);
	
	ZeroStructForce(node);
	
	alEnterCS();
		MP_FREE(AssociationNode, node);
	alLeaveCS();
	
	return 1;
}

S32 alHeadRemove(AssociationList* al){
	if(SAFE_MEMBER(al, head)){
		alNodeRemove(al->head);
		return 1;
	}
	
	return 0;
}

void alRemoveAll(AssociationList* al){
	while(alHeadRemove(al));
}

void alInvalidate(AssociationList* al){
	AssociationListIterator*	iter;
	AssociationList*			alOther;
	
	for(alItCreate(&iter, al);
		alItGetValues(iter, &alOther, NULL);
		alItGotoNextThenDestroy(&iter))
	{
		const AssociationListType* listType = alOther->listType;
		
		if(SAFE_MEMBER(listType, notifyInvalidatedFunc)){
			AssociationNode* node;
			
			alItGetNode(iter, &node);
			
			listType->notifyInvalidatedFunc(alOther->listOwner,
											alOther,
											node);
		}
	}
}

U32 alGetCount(AssociationList* al){
	AssociationListIterator*	iter;
	U32							count = 0;
	
	for(alItCreate(&iter, al);
		alItGetNode(iter, NULL);
		alItGotoNextThenDestroy(&iter))
	{
		count++;
	}
	
	return count;
}

S32 alHasOnlyOneNode(AssociationList* al){
	if(!SAFE_MEMBER(al, head)){
		return 0;
	}

	if(al->listType->flags.isPrimary){
		assert(!al->head->node1.prev);
		return !al->head->node1.next;
	}else{
		assert(!al->head->node2.prev);
		return !al->head->node2.next;
	}
}

MP_DEFINE(AssociationListIterator);

void alItCreate(AssociationListIterator** iterOut,
				AssociationList* al)
{
	AssociationListIterator* iter;

	if(!iterOut){
		return;
	}
	
	if(	!al ||
		!al->head)
	{
		*iterOut = NULL;
		return;
	}
	
	alEnterCS();
		MP_CREATE(AssociationListIterator, 10);

		iter = MP_ALLOC(AssociationListIterator);
	alLeaveCS();
	
	iter->al = al;
	iter->flags.isPrimary = al->listType->flags.isPrimary;
	iter->node = al->head;
	
	*iterOut = iter;
}

void alItGotoNext(AssociationListIterator* iter){
	AssociationNode* node = SAFE_MEMBER(iter, node);

	if(node){
		if(iter->flags.ignoreNextGoto){
			iter->flags.ignoreNextGoto = 0;
		}
		else if(iter->flags.isPrimary){
			iter->node = node->node1.next;
		}
		else{
			iter->node = node->node2.next;
		}
	}
}

void alItDestroy(AssociationListIterator** iterInOut){
	if(SAFE_DEREF(iterInOut)){
		alEnterCS();
			MP_FREE(AssociationListIterator, *iterInOut);
		alLeaveCS();
	}
}

void alItGotoNextThenDestroy(AssociationListIterator** iterInOut){
	AssociationListIterator*	iter = SAFE_DEREF(iterInOut);
	AssociationNode*			node = SAFE_MEMBER(iter, node);

	if(node){
		if(iter->flags.ignoreNextGoto){
			iter->flags.ignoreNextGoto = 0;
		}
		else if(iter->flags.isPrimary){
			node = iter->node = node->node1.next;
		}
		else{
			node = iter->node = node->node2.next;
		}
	}
	
	if(!node){
		alItDestroy(iterInOut);
	}
}

S32 alItNodeRemove(AssociationListIterator* iter){
	AssociationNode* node = SAFE_MEMBER(iter, node);
	
	if(	node &&
		!SAFE_MEMBER(iter, flags.ignoreNextGoto))
	{
		iter->node = iter->flags.isPrimary ? iter->node->node1.next : iter->node->node2.next;
		
		iter->flags.ignoreNextGoto = 1;
		
		alNodeRemove(node);
		
		return 1;
	}
	
	return 0;
}

S32 alItGetNode(AssociationListIterator* iter,
				AssociationNode** nodeOut)
{
	AssociationNode* node = SAFE_MEMBER(iter, node);

	if(!node){
		return 0;
	}
	
	if(nodeOut){
		*nodeOut = node;
	}
	
	return 1;
}

static S32 alGetValues(	AssociationList* al,
						AssociationNode* node,
						AssociationList** alOut,
						void** userPointerOut)
{
	if(!node){
		return 0;
	}
	
	if(alOut){
		*alOut = al->listType->flags.isPrimary ? node->list2 : node->list1;
	}
	
	if(userPointerOut){
		*userPointerOut = node->userPointer;
	}
	
	return 1;
}

S32	alItGetValues(	AssociationListIterator* iter,
					AssociationList** alOut,
					void** userPointerOut)
{
	if(	!iter ||
		iter->flags.ignoreNextGoto)
	{
		return 0;
	}
	
	return alGetValues(iter->al, SAFE_MEMBER(iter, node), alOut, userPointerOut);
}

S32 alItGetOwner(	AssociationListIterator* iter,
					void** ownerOut)
{
	if(	!iter ||
		iter->flags.ignoreNextGoto ||
		!iter->node ||
		!iter->al)
	{
		return 0;
	}
	
	if(ownerOut){
		*ownerOut = iter->al->listType->flags.isPrimary ?
						iter->node->list2->listOwner :
						iter->node->list1->listOwner;
	}
	
	return 1;
}
						
S32 alItSetUserPointer(	AssociationListIterator* iter,
						void* userPointer)
{
	if(	iter &&
		!iter->flags.ignoreNextGoto &&
		iter->node)
	{
		iter->node->userPointer = userPointer;
		
		return 1;
	}
	
	return 0;
}

S32 alItHasOnlyOneNode(AssociationListIterator* iter){
	if(!iter){
		return 0;
	}

	if(iter->flags.isPrimary){
		return	!iter->node->node2.next &&
				!iter->node->node2.prev;
	}else{
		return	!iter->node->node1.next &&
				!iter->node->node1.prev;
	}
}

S32 alHeadGetValues(AssociationList* al,
					AssociationList** alOut,
					void** userPointerOut)
{
	return alGetValues(al, SAFE_MEMBER(al, head), alOut, userPointerOut);
}

