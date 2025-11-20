#pragma once
GCC_SYSTEM


typedef struct PoolQueue
{
	int iMaxElements;
	int iHead;
	int iTail;
	int iNumElements;
	int iElementSize;
	int iAlignmentBytes;
	char* pStorage;
} PoolQueue;

typedef struct PoolQueueIterator
{
	PoolQueue*		pQueue;
	int				iIndex;
	bool			bBackwards;
} PoolQueueIterator;

void poolQueueInit(SA_PARAM_NN_VALID PoolQueue* pQueue, int iElementSize, int iMaxElements, int iAlignmentBytes);
PoolQueue* poolQueueCreate(void);

void poolQueueDeinit(SA_PARAM_NN_VALID PoolQueue* pQueue);
void poolQueueDestroy(SA_PRE_NN_VALID SA_POST_FREE PoolQueue* pQueue);

void poolQueueClear(SA_PARAM_NN_VALID PoolQueue* pQueue);
bool poolQueueIsFull(SA_PARAM_NN_VALID PoolQueue* pQueue);
bool poolQueueEnqueue(SA_PARAM_NN_VALID PoolQueue* pQueue, SA_PARAM_NN_VALID void* pElement);
void* poolQueuePreEnqueue(SA_PARAM_NN_VALID PoolQueue* pQueue);
bool poolQueueDequeue(SA_PARAM_NN_VALID PoolQueue* pQueue, SA_PARAM_OP_VALID void** ppElementTarget);
bool poolQueuePeek(SA_PARAM_NN_VALID PoolQueue* pQueue, SA_PARAM_OP_VALID const void** ppElementTarget);
bool poolQueuePeekTail(SA_PARAM_NN_VALID PoolQueue* pQueue, SA_PARAM_OP_VALID const void** ppElementTarget);
int poolQueueGetNumElements(SA_PARAM_NN_VALID PoolQueue* pQueue);
int poolQueueGetElementSize(SA_PARAM_NN_VALID PoolQueue* pQueue);
void poolQueueGrow(SA_PARAM_NN_VALID PoolQueue* pQueue, int iMaxElements);

void poolQueueGetIterator(PoolQueue* pQueue, PoolQueueIterator* pIter);
void poolQueueGetBackwardsIterator(PoolQueue* pQueue, PoolQueueIterator* pIter);
bool poolQueueGetNextElement(PoolQueueIterator* pIter, void** ppElem);
