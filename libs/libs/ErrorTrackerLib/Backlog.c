#include "Backlog.h"
#include "globalcomm.h"
#include "CrypticPorts.h"
#include "textparser.h"
#include "wininclude.h"
#include "earray.h"
#include "estring.h"
#include "timing.h"
#include "HttpLib.h"
#include "file.h"
#include "net.h"

CRITICAL_SECTION gCSBacklog;

AUTO_RUN;
void initBacklogCriticalSections(void)
{
	InitializeCriticalSection(&gCSBacklog);
}

static Backlog sBacklog = {0};
static bool sbBacklogDirty = false;

// ------------------------------------------------------------------------------

void backlogSave(const char *filename)
{
	EnterCriticalSection(&gCSBacklog);
	if (sbBacklogDirty)
	{
		{
			FILE *f = fopen(filename, "wb");
			if(f)
			{
				U32 uVersion = BACKLOG_VERSION;
				fwrite(&uVersion, 1, 4, f);
				fwrite(&sBacklog.uFront, 1, 4, f);
				fwrite(&sBacklog.uRear, 1, 4, f);
				fwrite(&sBacklog.uCount, 1, 4, f);
				fwrite(sBacklog.aRecentErrors, sizeof(RecentError), MAX_BACKLOG_ENTRIES, f);
				fclose(f);
			}
		}
		sbBacklogDirty = false;
	}
	LeaveCriticalSection(&gCSBacklog);
}

void backlogLoad(const char *filename)
{
	EnterCriticalSection(&gCSBacklog);
	{
		FILE *f = fopen(filename, "rb");
		if(f)
		{
			U32 version = 0;
			if(fread(&version, 1, sizeof(version), f))
			{
				if(version != BACKLOG_VERSION)
				{
					printf("Not loading backlog, incorrect version\n");
					fclose(f);
					return;
				}
			}

			fread(&sBacklog.uFront, 1, 4, f);
			fread(&sBacklog.uRear, 1, 4, f);
			fread(&sBacklog.uCount, 1, 4, f);
			fread(sBacklog.aRecentErrors, sizeof(RecentError), MAX_BACKLOG_ENTRIES, f);
			fclose(f);
		}
	}
	LeaveCriticalSection(&gCSBacklog);
}

Backlog *backlogClone()
{
	Backlog *pClone;
	EnterCriticalSection(&gCSBacklog);

	pClone = calloc(1, sizeof(Backlog));
	memcpy(pClone->aRecentErrors, sBacklog.aRecentErrors, sizeof(RecentError) * MAX_BACKLOG_ENTRIES);
	pClone->uFront = sBacklog.uFront;
	pClone->uRear  = sBacklog.uRear;
	pClone->uCount = sBacklog.uCount;

	LeaveCriticalSection(&gCSBacklog);
	return pClone;
}

// ------------------------------------------------------------------------------

void backlogSend(NetLink *link, U32 uMaxCount, U32 uStartTime, U32 uEndTime)
{
	static char *tempEString = NULL;
	U32 i;
	U32 uCurrIndex = sBacklog.uFront;
	U32 uCount     = sBacklog.uCount;
	U32 uPassed    = 0;

	U32 uOldestTime = 0;
	U32 uNewestTime = 0;

	EnterCriticalSection(&gCSBacklog);

	if(uMaxCount)
		uCount = MIN(uMaxCount, sBacklog.uCount);

	if(sBacklog.uCount)
	{
		uOldestTime = sBacklog.aRecentErrors[sBacklog.uFront].uTime;
		uNewestTime = sBacklog.aRecentErrors[RING_PREVIOUS(sBacklog.uRear)].uTime;
	}

	if(uStartTime != 0)
	{
		while(uPassed < sBacklog.uCount && sBacklog.aRecentErrors[uCurrIndex].uTime < uStartTime)
		{
			uPassed++;
			RING_INCREMENT(uCurrIndex);
		}
	}

	// JDRAGO - This is outputting in JSON because it is much, much leaner for this small set of data.
	//          We could obviously add a beefier XML output pretty easily, but I am trying to minimize
	//          the load on the ErrorTracker and still have reasonable reporting. 

	estrClear(&tempEString);
	estrConcatf(&tempEString, "{ \"list\":[\n");

	for(i=0; uPassed<sBacklog.uCount && i<uCount; i++)
	{
		if(uEndTime && sBacklog.aRecentErrors[uCurrIndex].uTime > uEndTime)
			break;

		estrConcatf(&tempEString, "%s[%d,%d,%d,%d]\n", 
			(i) ? "," : "",
			sBacklog.aRecentErrors[uCurrIndex].uTime,
			(int)sBacklog.aRecentErrors[uCurrIndex].eType,
			sBacklog.aRecentErrors[uCurrIndex].uID,
			sBacklog.aRecentErrors[uCurrIndex].uIndex);

		RING_INCREMENT(uCurrIndex);
		uPassed++;
	}

	estrConcatf(&tempEString, "],");
	estrConcatf(&tempEString, "\"oldest\":%d,\n", uOldestTime);
	estrConcatf(&tempEString, "\"newest\":%d\n", uNewestTime);
	estrConcatf(&tempEString, "}\n");

	httpSendStr(link, tempEString);

	LeaveCriticalSection(&gCSBacklog);
}

// ------------------------------------------------------------------------------

void backlogInit()
{
	EnterCriticalSection(&gCSBacklog);

	sBacklog.uFront = 0;
	sBacklog.uRear  = 0;
	sBacklog.uCount = 0;

	LeaveCriticalSection(&gCSBacklog);
}

void backlogReceivedNewError(U32 uID, U32 uIndex, ErrorEntry *pEntry)
{
	EnterCriticalSection(&gCSBacklog);
	{
		RecentError *p = &sBacklog.aRecentErrors[sBacklog.uRear];
		p->uTime  = timeSecondsSince2000();
		p->eType  = pEntry->eType;
		p->uID    = uID;
		p->uIndex = uIndex;

		RING_INCREMENT(sBacklog.uRear);
		if(sBacklog.uCount == MAX_BACKLOG_ENTRIES)
		{
			RING_INCREMENT(sBacklog.uFront);
			assertmsg(sBacklog.uFront == sBacklog.uRear, "ET monitor ringbuffer code is broken.");
		}
		else
		{
			sBacklog.uCount++;
		}
	}
	sbBacklogDirty = true;
	LeaveCriticalSection(&gCSBacklog);
}
