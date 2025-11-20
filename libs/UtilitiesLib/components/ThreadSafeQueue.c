#include "ThreadSafeQueue.h"

#if defined(_WIN64) || defined(_WIN32)

#include "timing.h"

typedef struct XLF_QUEUE {
	volatile U32 iget;			//remove read index
	volatile U32 iput, rput;	//remove write index and add write index
	DWORD maximumLength;
	XLockFreeMemoryFree free;
} XLF_QUEUE;

// optimizations will cause some kind of horrible crash here
#pragma optimize( "", off)
HRESULT XLFQueueCreate(XLOCKFREE_CREATE *pinfo, XLOCKFREE_HANDLE *pqueue) {
	assert(pinfo->maximumLength !=0 && pinfo->maximumLength != (DWORD)-1);
	{
		XLF_QUEUE *pq = 0;
		DWORD size = sizeof(XLF_QUEUE) + pinfo->maximumLength * sizeof(void*);

		if (pinfo->allocate)
		{
			pq = (XLF_QUEUE *)(pinfo->allocate)(0, size);
		}
		else
		{
			pq = (XLF_QUEUE *)malloc(size);
		}

		if(pq) {
			pq->maximumLength = pinfo->maximumLength;
			pq->free = pinfo->free;

			pq->iget = 0;
			pq->iput = 0;
			pq->rput = 0;
		}

		*pqueue = pq;
	}
	return 0;
}

void XLFQueueDestroy(XLOCKFREE_HANDLE queue) {
	XLF_QUEUE *pq = (XLF_QUEUE *)queue;
	if(pq) {
		if (pq->free)
		{
			(pq->free)(0, pq);
		}
		else
		{
			free(pq);
		}
	}
}
#pragma optimize( "", on)

HRESULT XLFQueueAdd(XLOCKFREE_HANDLE queue, void* data) {
	XLF_QUEUE *pq = (XLF_QUEUE *)queue;
	void * volatile *p = (void **)(pq + 1);
	U32 iput, oput;

	PERFINFO_AUTO_START_FUNC();

	{
		do {
			iput = oput = pq->rput;

			if(++iput == pq->maximumLength)
				iput = 0;

			if(iput == pq->iget)
			{
				PERFINFO_AUTO_STOP();
				return XLOCKFREE_STRUCTURE_FULL;
			}

			//if we did increment, break.
			if ((U32)InterlockedCompareExchange(&pq->rput, iput, oput) == oput) break;
			
		} while (true);
	}
	p[oput] = data;
	
	MemoryBarrier();

	{
		//if pq->iput == oput, then this inequality will fail
		while((U32)InterlockedCompareExchange(&pq->iput, iput, oput) != oput)
		{
			Sleep(0);
		}
	}

	PERFINFO_AUTO_STOP();
	return 0;
}

HRESULT XLFQueueRemove(XLOCKFREE_HANDLE queue, void** data) {
	XLF_QUEUE *pq = (XLF_QUEUE *)queue;
	void * volatile *p = (void **)(pq + 1);
	
	U32 iget, oget;

	do {
		iget = oget = pq->iget;

		if(iget == pq->iput)
		{
			if (pq->iput != pq->rput)
				continue;
			else
				return XLOCKFREE_STRUCTURE_EMPTY;
		}

		MemoryBarrier();
		{
			void *d = p[oget];

			if(++iget == pq->maximumLength) iget = 0;

			//if we did not increment, try again.
			if ((U32)InterlockedCompareExchange(&pq->iget, iget, oget) == oget)
			{
				*data = d;
				break;
			}
		}
	} while (true);

	return 0;
}

BOOL WINAPI XLFQueueIsEmpty(IN XLOCKFREE_HANDLE queue) {
	XLF_QUEUE *pq = (XLF_QUEUE *)queue;
	U32 iget = pq->iget;
	return (pq->iput == iget && pq->rput == iget);
}

HRESULT WINAPI XLFQueueGetEntryCount(IN XLOCKFREE_HANDLE queue, OUT LONG* entries)
 {
	XLF_QUEUE *pq = (XLF_QUEUE *)queue;
	*entries = pq->iput-pq->iget;
	if (*entries < 0)
		*entries += pq->maximumLength;

	return S_OK;
}

#endif //_WIN64