#pragma once

#include "net.h"
#include "ErrorTracker.h"

typedef struct ErrorEntry ErrorEntry;

// Bump this if you change any data structure or constant in this file or the header
#define BACKLOG_VERSION 1
#define MAX_BACKLOG_ENTRIES 100000

typedef struct RecentError
{
	U32 uTime;
	ErrorDataType eType;
	U32 uID;
	U32 uIndex;
} RecentError;

typedef struct Backlog
{
	RecentError aRecentErrors[MAX_BACKLOG_ENTRIES];
	U32 uFront;
	U32 uRear;
	U32 uCount;
} Backlog;

void backlogInit();
void backlogReceivedNewError(U32 uID, U32 uIndex, ErrorEntry *pEntry);
void backlogSend(NetLink *link, U32 uMaxCount, U32 uStartTime, U32 uEndTime);

Backlog *backlogClone();

void backlogSave(const char *filename);
void backlogLoad(const char *filename);

#define RING_INCREMENT(a) a = (a+1) % MAX_BACKLOG_ENTRIES
#define RING_PREVIOUS(a) ((a == 0) ? MAX_BACKLOG_ENTRIES-1 : (a-1))
