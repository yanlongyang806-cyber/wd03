/***************************************************************************
*     Copyright (c) 2006-2007, Cryptic Studios
*     All Rights Reserved
*     Confidential Property of Cryptic Studios
***************************************************************************/

#ifndef TIMEDEVENTQUEUE_H
#define TIMEDEVENTQUEUE_H

typedef void(*TimedEventAction)(void*);

// *************************************************************************
// Note: the queue takes ownership of the item passed and will free or 
//       StructDestroy it when the event happens
// *************************************************************************

// Creates a new timed event queue with the callback function for when the event happens
// This queue is added to a processing list until deleted
// The queue is referenced by the queueName used during creation
void timedeventqueue_Create(char* queueName, TimedEventAction onEventFunc, ParseTable opt_pti[]); // if opt_pti is passed, use StructDestroy instead of free

// Frees the timed event queue and removes it from the processing list
void timedeventqueue_Destroy(char* queueName);

// Add a new event to the queue
void timedeventqueue_Set(char* queueName, void* structPtr, U32 eventTime);

// Remove an event from the queue
void timedeventqueue_Remove(char* queueName, void* structPtr);

// Remove all events from the queue
void timedeventqueue_Reset(char* queueName);

// Checks all queues against the current time to see if any events were triggered
void timedeventqueue_UpdateTimers(void);

// check if this queue exists (expensive)
bool timedeventqueue_Exists(char *queueName);

#endif
