#include "nameTable.h"
#include "net/net.h"
#include "StashTable.h"
#include "estring.h"
#include "WinInclude.h"
#include "earray.h"

typedef struct StashTableImp StashTableImp;

static CRITICAL_SECTION gcsNameTableMutex = {0};

AUTO_RUN;
void NameTableInit(void)
{
	InitializeCriticalSection(&gcsNameTableMutex);
}

StashTableImp **ppNameTables = NULL;

NameTable CreateNameTable(Packet *pPack)
{
	NameTable table;

	EnterCriticalSection(&gcsNameTableMutex);
	if (eaSize(&ppNameTables))
	{
		table = eaPop(&ppNameTables);
	}
	else
	{
		table = stashTableCreateWithStringKeys(16, StashDeepCopyKeys_NeverRelease);
	}
	LeaveCriticalSection(&gcsNameTableMutex);

	if (pPack)
	{
		do
		{
			int iSize;
			char *pKey = pktGetStringTemp(pPack);
			if (!pKey[0])
			{
				break;
			}
			iSize = pktGetBits(pPack, 32);
			assert(iSize);
			NameTableAddBytes(table, pKey, pktGetBytesTemp(pPack, iSize), iSize);
		}
		while (1);
	}

	return table;
}
		
void simpleFreeString(void *pValue)
{
	free(pValue);
}

void DestroyNameTable(NameTable table)
{
	stashTableClearEx(table, NULL, simpleFreeString);

	EnterCriticalSection(&gcsNameTableMutex);
	eaPush(&ppNameTables, table);
	LeaveCriticalSection(&gcsNameTableMutex);
}

void *NameTableLookup(NameTable table, const char *pKey, int *iSize)
{
	int *pRetVal;

	if (!stashFindPointer(table, pKey, &pRetVal))
	{
		return NULL;
	}

	*iSize = *pRetVal;

	return pRetVal + 1;
}

void NameTableAddBytes(NameTable table, const char *pKey, void *pData, int iSize)
{
	int *pBuf;

	if (stashRemovePointer(table, pKey, &pBuf))
	{
		free(pBuf);
	}

	pBuf = malloc(iSize + sizeof(int));
	*pBuf = iSize;
	memcpy(pBuf + 1, pData, iSize);
	

	stashAddPointer(table, pKey, pBuf, false);
}

void NameTablePutIntoPacket(NameTable table, Packet *pPack, char *pKeyList)
{
	if (pKeyList)
	{
		//this is a little non-destructive bit of strtokery.
		while (*pKeyList)
		{
			char *pNextSpace;
			void *pCurData;
			int iSize;

			//skip over any leading spaces
			while (*pKeyList && *pKeyList == ' ')
			{
				pKeyList++;
			}

			if (!*pKeyList)
			{
				break;
			}

			pNextSpace = strchr(pKeyList, ' ');

			if (pNextSpace)
			{
				*pNextSpace = 0;
			}

			pCurData = NameTableLookup(table, pKeyList, &iSize);

			if (pCurData)
			{
				pktSendString(pPack, pKeyList);
				pktSendBits(pPack, 32, iSize);
				pktSendBytes(pPack, iSize, pCurData);
			}

			pKeyList += strlen(pKeyList);

			if (pNextSpace)
			{
				*pNextSpace = ' ';
			}
		}
			
		pktSendString(pPack, "");
	}
	else
	{
		StashTableIterator iterator;
		StashElement element;

		stashGetIterator(table, &iterator);

		while (stashGetNextElement(&iterator, &element))
		{
			char *pKey = stashElementGetStringKey(element);
			int *pData = stashElementGetPointer(element);

			pktSendString(pPack, pKey);
			pktSendBits(pPack, 32, *pData);
			pktSendBytes(pPack, *pData, pData + 1);
		}

		pktSendString(pPack, "");
	}
}


void DumpNameTableIntoEString(NameTable table, char **ppEString)
{
	StashTableIterator iterator;
	StashElement element;

	stashGetIterator(table, &iterator);

	while (stashGetNextElement(&iterator, &element))
	{
		char *pKey = stashElementGetStringKey(element);
		int *pData = stashElementGetPointer(element);

		estrConcatf(ppEString, "%s (%d bytes)\n", pKey, *pData);
	
	}
}

