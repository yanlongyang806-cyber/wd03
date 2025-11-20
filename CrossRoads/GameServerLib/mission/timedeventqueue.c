/***************************************************************************
*     Copyright (c) 2006-2007, Cryptic Studios
*     All Rights Reserved
*     Confidential Property of Cryptic Studios
***************************************************************************/

#include "earray.h"
#include "textparser.h"
#include "error.h"
#include "timing.h"
#include "timedeventqueue.h"

typedef struct TEQElement
{
	void*	structPtr;
	U32		eventTime;
    int     removed;
} TEQElement;

typedef struct TimedEventQueue
{
	char*				name;
	TimedEventAction	eventFunc;
	TEQElement**		events;
    ParseTable          *pti; // if present, use structdestroy
} TimedEventQueue;

// Even though this is referenced by name, I used an array instead of a hash because most of the time
// will be spent checking all the queues, not adding and removing elements
static TimedEventQueue** s_TimedQueueList = NULL;

static TimedEventQueue* FindQueueByName(char* queueName, bool removeQueue)
{
	int i, n = eaSize(&s_TimedQueueList);
	for (i = 0; i < n; i++)
	{
		if (!stricmp(queueName, s_TimedQueueList[i]->name))
		{
			TimedEventQueue* retQueue = s_TimedQueueList[i];
			if (removeQueue)
				eaRemove(&s_TimedQueueList, i);
			return retQueue;
		}
	}
	return NULL;
}

void timedeventqueue_Create(char* queueName, TimedEventAction onEventFunc, ParseTable opt_pti[])
{
	// We shouldn't be creating two queues with the same name, error and fail
	if (FindQueueByName(queueName, false))
		Errorf("timedeventqueue_Create: Queue by name \"%s\" already exists, failed to create queue", queueName);
	else
	{
		TimedEventQueue* newQueue = calloc(1, sizeof(TimedEventQueue));
		newQueue->name = strdup(queueName);
		newQueue->eventFunc = onEventFunc;
        newQueue->pti = opt_pti;
		eaPush(&s_TimedQueueList, newQueue);
	}
}

void timedeventqueue_Destroy(char* queueName)
{
	TimedEventQueue* queue = FindQueueByName(queueName, true);
	if (queue)
	{
		free(queue->name);
        eaDestroyStructVoid(&queue->events,queue->pti);
		free(queue);
	}
}

bool timedeventqueue_Exists(char *queueName)
{
    return FindQueueByName(queueName, false) != NULL;
}


// Assumption here: Linear search should be fastest as most inserts will have the latest time
void timedeventqueue_Set(char* queueName, void* structPtr, U32 eventTime)
{
	TimedEventQueue* queue;            

    queue = FindQueueByName(queueName, false);
	if (queue)
	{
		int insertIndex;
		TEQElement* newElement;
        int i, n = eaSize(&queue->events);

        for (i = 0; i < n; i++)
        {
            if (structPtr == queue->events[i]->structPtr)
            {
                queue->events[i]->structPtr = NULL;
                queue->events[i]->removed = true;
            }
        }

        newElement = calloc(1, sizeof(TEQElement));
		newElement->eventTime = eventTime;
		newElement->structPtr = structPtr;
		for (insertIndex = eaSize(&queue->events) - 1; insertIndex >= 0; insertIndex--)
			if (eventTime > queue->events[insertIndex]->eventTime)
				break;
		eaInsert(&queue->events, newElement, insertIndex + 1);
	}
}

void timedeventqueue_Remove(char* queueName, void* structPtr)
{
	TimedEventQueue* queue = FindQueueByName(queueName, false);
	if (queue)
	{
		int i, n = eaSize(&queue->events);
		for (i = 0; i < n; i++)
		{
			if (structPtr == queue->events[i]->structPtr)
			{
                queue->events[i]->removed = true;
				break;
			}
		}
	}
}

void timedeventqueue_Reset(char* queueName)
{
	TimedEventQueue* queue = FindQueueByName(queueName, false);
	if (queue)
        eaDestroyStructVoid(&queue->events,queue->pti);
}

void timedeventqueue_UpdateTimers(void)
{
	static U32 s_LastUpdateSecond = 0;
	U32 currTime = timeSecondsSince2000();
    int i, n;
    
	if (currTime != s_LastUpdateSecond)
	{
		n = eaSize(&s_TimedQueueList);
		for (i = 0; i < n; ++i)
		{
			TimedEventQueue* queue = s_TimedQueueList[i];
            int j;
            
            if(!queue)
                continue;
            
            for(j = eaSize(&queue->events) - 1; j >= 0; --j)
            {
                TEQElement *elt = queue->events[j];
                if(!elt || elt->eventTime > currTime || elt->removed)
                    continue;
                queue->eventFunc(elt->structPtr);
                elt->removed = true;
            }
		}
		s_LastUpdateSecond = currTime;
	}
    
    
    n = eaSize(&s_TimedQueueList);
    for (i = 0; i < n; ++i)
    {
        TimedEventQueue* queue = s_TimedQueueList[i];
        int j;
        
        if(!queue)
            continue;
        
        for(j = eaSize(&queue->events) - 1; j >= 0; --j)
        {
            TEQElement *elt = queue->events[j];
            if(!SAFE_MEMBER(elt,removed))
                continue;
            if(queue->pti)
                StructDestroyVoid(queue->pti,elt->structPtr);
            free(elt);
            eaRemove(&queue->events, j);
        }
    }   
}
