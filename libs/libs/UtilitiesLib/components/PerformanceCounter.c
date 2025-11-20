#if !PLATFORM_CONSOLE

#include "wininclude.h"
#include <conio.h>
#include <stdio.h>
#include <pdh.h>

#include "earray.h"
#include "StringCache.h"
#include "Alerts.h"
#include "utf8.h"

// This line CANNOT be linked into gameserver or anything else that needs shared memory
// On certain servers pdh.lib ends up loading dlls that conflict with our shared memory address
#pragma comment(lib,"pdh.lib")


typedef struct CounterRef {
	HCOUNTER hCounter;
	LONG *storage;
	double *d_storage;
	const char *pCounterName; //pooled
} CounterRef;

typedef struct PerformanceCounter {
	HQUERY hQuery;
	char **eaInstances;
	CounterRef **eaCounters;
	S16 *instanceList;
	char *interfaceName;
	S16 *wideInterfaceName;
} PerformanceCounter;

// Walks a string of strings
static void printList(char *s) {
	char *walk;
	for (walk=s; *walk;) {
		printf("\t%s\n", walk);
		walk += strlen(walk)+1;
	}
}

void performanceCounterDestroy(PerformanceCounter *counter)
{
	int i;
	if (!counter)
		return;
	for (i=0; i<eaSize(&counter->eaCounters); i++) {
		PdhRemoveCounter(counter->eaCounters[i]->hCounter);
	}
	eaDestroyEx(&counter->eaCounters, NULL);
	eaDestroyEString(&counter->eaInstances);
	if (counter->hQuery) {
		PdhCloseQuery(counter->hQuery);
		counter->hQuery = NULL;
	}
	SAFE_FREE(counter->interfaceName);
	SAFE_FREE(counter->instanceList);
	SAFE_FREE(counter->wideInterfaceName);

}

PerformanceCounter *performanceCounterCreate(const char *interfaceName)
{
	PDH_STATUS           pdhStatus = ERROR_SUCCESS;
	LPTSTR               szCounterListBuffer = NULL;
	DWORD                dwCounterListSize  = 0;
	LPTSTR               szInstanceListBuffer = NULL;
	DWORD                dwInstanceListSize  = 0;
	PerformanceCounter   *counter = calloc(sizeof(PerformanceCounter),1);


	counter->interfaceName = strdup(interfaceName);
	counter->wideInterfaceName = UTF8_To_UTF16_malloc(interfaceName);

	pdhStatus = PdhEnumObjectItems (NULL,NULL,
		counter->wideInterfaceName,
		szCounterListBuffer,
		&dwCounterListSize,
		szInstanceListBuffer,
		&dwInstanceListSize,
		PERF_DETAIL_WIZARD,
		0);

	szCounterListBuffer = (LPTSTR)malloc ((dwCounterListSize *
		sizeof (TCHAR)));
	szInstanceListBuffer = (LPTSTR)malloc ((dwInstanceListSize *
		sizeof (TCHAR)));

	if(!szCounterListBuffer || !szInstanceListBuffer)
	{
		printf ("unable to allocate performance buffer\n");
		performanceCounterDestroy(counter);
		return NULL;
	}

	pdhStatus = PdhEnumObjectItems (NULL,NULL,
		counter->wideInterfaceName,
		szCounterListBuffer,
		&dwCounterListSize,
		szInstanceListBuffer,
		&dwInstanceListSize,
		PERF_DETAIL_WIZARD,
		0);

	if(pdhStatus == ERROR_SUCCESS) 
	{
		S16 *walk;
		counter->instanceList = szInstanceListBuffer;
		for (walk=szInstanceListBuffer; *walk;) 
		{
			char *pShortWalk = NULL;
			UTF16ToEstring(walk, 0, &pShortWalk);


			if (stricmp(pShortWalk, "MS TCP Loopback interface")==0) 
			{
				// Don't add the loopback adapter for networking counters
			} 
			else if (stricmp(pShortWalk, "_Total")==0) 
			{
				// There's a total, just use it!
				eaClearEString(&counter->eaInstances);

				eaPush(&counter->eaInstances, pShortWalk);
				break;
			} else 
			{
				eaPush(&counter->eaInstances, pShortWalk);
			}
			walk += wcslen(walk) + 1;
		}
	}
	else
	{
		free(szCounterListBuffer);
		free(szInstanceListBuffer);
		performanceCounterDestroy(counter);
		printf ("unable to allocate performance buffer\n");
		return NULL;
	}

	free(szCounterListBuffer);

	if( PdhOpenQuery(NULL ,0 ,&counter->hQuery))
	{
		printf("PdhOpenQuery failed\n");
		performanceCounterDestroy(counter);
		return NULL;
	}

	return counter;
}

static int performanceCounterAddEither(PerformanceCounter *counter, const char *counterName, LONG *storage, double *d_storage)
{
	int i;
	for (i=0; i<eaSize(&counter->eaInstances); i++)
	{
		char szCounter[256];
		S16 *pLongCounter = NULL;
		CounterRef *ref = calloc(sizeof(CounterRef),1);
		sprintf_s(SAFESTR(szCounter),"\\%s(%s)\\%s",
			counter->interfaceName, counter->eaInstances[i], counterName);

		pLongCounter = UTF8_To_UTF16_malloc(szCounter);

		if( PdhAddCounter(counter->hQuery,pLongCounter,(DWORD_PTR)NULL,&ref->hCounter))
		{
			free(pLongCounter);
			printf("PdhAddCounter failed : %s\n", szCounter);
			free(ref);
			return 0;
		}

		free(pLongCounter);

		ref->storage = storage;
		ref->d_storage = d_storage;
		ref->pCounterName = allocAddString(counterName);
		eaPush(&counter->eaCounters, ref);
	}
	return 1;
}

int performanceCounterAdd(PerformanceCounter *counter, const char *counterName, LONG *storage)
{
	return performanceCounterAddEither(counter,counterName,storage,0);
}

int performanceCounterAddF64(PerformanceCounter *counter, const char *counterName, double *storage)
{
	return performanceCounterAddEither(counter,counterName,0,storage);
}



int performanceCounterQuery(PerformanceCounter *counter, AlertOnSlowArgs *pSlowArgs)
{
	PDH_FMT_COUNTERVALUE value;
	int i;
	int ret=1;
	bool bRet;

	if (pSlowArgs)
	{
		bRet = PdhCollectQueryData(counter->hQuery);
			if (bRet)
		{
			printf("PdhCollectQueryData failed\n");
			return 0;
		}
	}
	else
	{
		if(PdhCollectQueryData(counter->hQuery))
		{
			printf("PdhCollectQueryData failed\n");
			return 0;
		}
	}

	for (i=0; i<eaSize(&counter->eaCounters); i++)
	{
		if (counter->eaCounters[i]->storage)
			*counter->eaCounters[i]->storage = 0;
		if (counter->eaCounters[i]->d_storage)
			*counter->eaCounters[i]->d_storage = 0;
	}

	for (i=0; i<eaSize(&counter->eaCounters); i++)
	{
		if (counter->eaCounters[i]->storage)
		{
			if (pSlowArgs)
			{
				bRet = PdhGetFormattedCounterValue(counter->eaCounters[i]->hCounter, PDH_FMT_LONG, NULL, &value);
			}
			else
			{
				bRet = PdhGetFormattedCounterValue(counter->eaCounters[i]->hCounter, PDH_FMT_LONG, NULL, &value);
			}

			if(bRet)
			{
				//printf("PdhGetFormattedCounterValue failed\n");
				ret = 0;
				continue;
			}
			*counter->eaCounters[i]->storage += value.longValue;
		}
		if (counter->eaCounters[i]->d_storage)
		{
			if (pSlowArgs)
			{
				bRet = PdhGetFormattedCounterValue(counter->eaCounters[i]->hCounter, PDH_FMT_DOUBLE, NULL, &value);
			}
			else
			{
				bRet = PdhGetFormattedCounterValue(counter->eaCounters[i]->hCounter, PDH_FMT_DOUBLE, NULL, &value);
			}

			if( bRet)
			{
				//printf("PdhGetFormattedCounterValue failed\n");
				ret = 0;
				continue;
			}
			*counter->eaCounters[i]->d_storage += value.doubleValue;
		}
	}
	return ret;
}

int testPerformanceCounter()
{
	LONG bytesSent;
	LONG bytesRead;
	LONG cpuUsage;
	PerformanceCounter *counter;
	PerformanceCounter *counterCPU;

	if ((counter=performanceCounterCreate("Network Interface"))==NULL)
		return 0;

	performanceCounterAdd(counter, "Bytes Sent/sec", &bytesSent);
	performanceCounterAdd(counter, "Bytes Received/sec", &bytesRead);

	if ((counterCPU=performanceCounterCreate("Processor"))==NULL)
		return 0;

	performanceCounterAdd(counterCPU, "% Processor Time", &cpuUsage);

	while(!_kbhit())
	{

		performanceCounterQuery(counter, NULL);
		performanceCounterQuery(counterCPU, NULL);

		printf("Bytes Sent/sec : %ld\n",bytesSent);
		printf("Bytes Read/sec : %ld\n",bytesRead);
		printf("CPU Usage : %ld\n",cpuUsage);
		Sleep(1000);
	}

	performanceCounterDestroy(counter);

	return 1;
}

#endif