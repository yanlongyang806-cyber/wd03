#include "TestServerEntity.h"
#include "earray.h"
#include "objContainer.h"
#include "objTransactions.h"
#include "TimedCallback.h"
#include "tokenstore.h"
#include "windefinclude.h"

#include "AutoGen/AppServerLib_autogen_RemoteFuncs.h"
#include "AutoGen/ObjectDB_autogen_RemoteFuncs.h"

#define TESTSERVER_CONTAINER_MINI_BATCH_SIZE 256
#define TESTSERVER_MAX_CONTAINERS 1024

static int giActiveMoves = 0;

typedef struct TestServerPercBaton
{
	bool bFree;
	GlobalType eType;
	int batch;
	int offset;
	int requests;
	int successes;
	int failures;
	ContainerList *pList;
	
	PercedContainerCallback pContainerCallback;
	void *pContainerUserData;

	ContainerPercCallback pCallback;
	void *pUserData;
} TestServerPercBaton;

typedef struct ContainerBaton
{
	ContainerID id;
	TestServerPercBaton *pBaton;
} ContainerBaton;

// This code blatantly ripped off from DBScript
typedef struct XPathLookup
{
	ParseTable *tpi;
	void *ptr;
	int column;
	int index;
} XPathLookup;

bool xlookup(const char *xpath, Container *con, XPathLookup *p, bool requireStruct)
{
	bool found = true;

	if(!strcmp(xpath, ".") || xpath[0] == 0)
	{
		p->tpi    = con->containerSchema->classParse;
		p->ptr    = con->containerData;
		p->column = 0;
		p->index  = 0;
	}
	else
	{
		found = objPathResolveField(xpath, con->containerSchema->classParse, con->containerData, &p->tpi, &p->column, &p->ptr, &p->index, OBJPATHFLAG_TRAVERSEUNOWNED);
		if(found && requireStruct)
		{
			if(TOK_HAS_SUBTABLE(p->tpi[p->column].type) && p->tpi[p->column].subtable)
			{
				p->tpi = p->tpi[p->column].subtable;
				p->column = 0;
			}
			else
			{
				found = false;
				p->tpi = NULL;
			}
		}
	}

	return found;
}
// End ripped-off code

static struct
{
	TestServerObjectRequest	reqType;
	GlobalType eType;
	ContainerID id;
	char *xPath;

	// Results
	bool bDone;
	bool bFound;

	// xinfo -- type, value, count
	XPathLookup lookup;
	MultiVal val;

	// xmembers / xindices -- string array of members/indices, indexed flag, count
	char **keys;
	bool indexed;
	int count;
} gXPathRequest;
static CRITICAL_SECTION cs_gXPathRequest;

void TestServer_InitEntity(void)
{
	InitializeCriticalSection(&cs_gXPathRequest);
}

void TestServer_XPathRequest(TestServerObjectRequest reqType, GlobalType eType, ContainerID id, const char *xPath)
{
	EnterCriticalSection(&cs_gXPathRequest);
	// Clear existing results
	free(gXPathRequest.xPath);
	gXPathRequest.bDone = false;
	gXPathRequest.bFound = false;
	MultiValClear(&gXPathRequest.val);
	ZeroStruct(&gXPathRequest.lookup);
	eaDestroyEx(&gXPathRequest.keys, NULL);
	gXPathRequest.indexed = false;
	gXPathRequest.count = 0;

	// Now initialize new request
	gXPathRequest.reqType = reqType;
	gXPathRequest.eType = eType;
	gXPathRequest.id = id;
	gXPathRequest.xPath = strdup(xPath);
	LeaveCriticalSection(&cs_gXPathRequest);
}

void TestServer_XPathRequestHandle(void *user_data, void *data, WTCmdPacket *packet)
{
	Container *con = objGetContainer(gXPathRequest.eType, gXPathRequest.id);
	
	EnterCriticalSection(&cs_gXPathRequest);
	switch(gXPathRequest.reqType)
	{
	case TSObj_XInfo:
		if((gXPathRequest.bFound = xlookup(gXPathRequest.xPath, con, &gXPathRequest.lookup, false)))
		{
			objPathGetMultiVal(gXPathRequest.xPath, con->containerSchema->classParse, con->containerData, &gXPathRequest.val);
		}
	xcase TSObj_XIndices:
		if((gXPathRequest.bFound = xlookup(gXPathRequest.xPath, con, &gXPathRequest.lookup, false)))
		{
			XPathLookup *l = &(gXPathRequest.lookup);
			gXPathRequest.indexed = ParserColumnIsIndexedEArray(l->tpi, l->column, NULL);
			gXPathRequest.count = TokenStoreGetNumElems(l->tpi, l->column, l->ptr, NULL);

			if(gXPathRequest.indexed)
			{
				ParseTable* subtable = NULL;
				char buf[MAX_TOKEN_LENGTH];
				int keyfield;
				int i;

				for(i = 0; i < gXPathRequest.count; ++i)
				{
					void* substruct = StructGetSubtable(l->tpi, l->column, l->ptr, i, &subtable, NULL);
					if (!substruct)
						break;
					keyfield = ParserGetTableKeyColumn(subtable);
					assertmsg(keyfield >= 0, "Some polymorph types of have a key field, but some do not?? BAD");
					if(TokenToSimpleString(subtable, keyfield, substruct, SAFESTR(buf), false))
					{
						eaPush(&gXPathRequest.keys, strdup(buf));
					}
					else
					{
						eaPush(&gXPathRequest.keys, NULL);
					}
				}
			}
		}
	xcase TSObj_XMembers:
		if((gXPathRequest.bFound = xlookup(gXPathRequest.xPath, con, &gXPathRequest.lookup, true)))
		{
			XPathLookup *l = &(gXPathRequest.lookup);
			int i = 0;

			while(l->tpi[i].type || (l->tpi[i].name && l->tpi[i].name[0]))
			{
				int type = TOK_GET_TYPE(l->tpi[i].type);
				if (type == TOK_START)   { ++i; continue; }
				if (type == TOK_END)     { ++i; continue; }
				if (type == TOK_IGNORE)  { ++i; continue; }
				if (type == TOK_COMMAND) { ++i; continue; }

				eaPush(&gXPathRequest.keys, strdup(l->tpi[i].name));
				++gXPathRequest.count;
				++i;
			}
		}
	xdefault:
		break;
	}

	gXPathRequest.bDone = true;
	LeaveCriticalSection(&cs_gXPathRequest);
}

bool TestServer_XPathRequestReady(void)
{
	bool ready = false;

	EnterCriticalSection(&cs_gXPathRequest);
	ready = gXPathRequest.bDone;
	LeaveCriticalSection(&cs_gXPathRequest);

	return ready;
}

TestServerXPathType TestServer_XPathType(void)
{
	XPathLookup *l = &(gXPathRequest.lookup);

	if(!gXPathRequest.bFound)
		return TSXPT_NotFound;
	else if(TokenStoreStorageTypeIsAnArray(TokenStoreGetStorageType(l->tpi[l->column].type)) && (l->index == -1))
		return TSXPT_Array;
	else if(l->column == 0 || TOK_HAS_SUBTABLE(l->tpi[l->column].type))
		return TSXPT_Struct;
	else if(MultiValIsNumber(&gXPathRequest.val))
		return TSXPT_Number;
	else if(MultiValIsString(&gXPathRequest.val))
		return TSXPT_String;
	else
		return TSXPT_Invalid;
}

const char *TestServer_XPathStringValue(void)
{
	return MultiValGetString(&gXPathRequest.val, NULL);
}

float TestServer_XPathNumberValue(void)
{
	return MultiValGetFloat(&gXPathRequest.val, NULL);
}

int TestServer_XPathNumKeys(void)
{
	return gXPathRequest.count;
}

bool TestServer_XPathIndexed(void)
{
	return gXPathRequest.indexed;
}

char *TestServer_XPathKey(int index)
{
	return gXPathRequest.keys[index];
}

static bool TestServer_ContainerPerc_Complete(GlobalType eType, int numContainers, int offset, void *userData)
{
	printf("ContainerPerc success - %d %s containers percolated.\n", numContainers, GlobalTypeToName(eType));
	return true;
}

static bool TestServer_ContainerPerc_ContainerComplete(GlobalType eType, ContainerID id, void *userData)
{
	return true;
}

static void TestServer_ContainerPerc_Step(TimedCallback *pCallback, F32 timeSinceLastCallback, TestServerPercBaton *pBaton);

static void TestServer_ContainerPerc_MoveContainerCB(TransactionReturnVal *pReturnVal, ContainerBaton *pContainerBaton)
{
	TestServerPercBaton *pBaton = pContainerBaton->pBaton;
	bool bFree = true;

	--giActiveMoves;

	switch(pReturnVal->eOutcome)
	{
	case TRANSACTION_OUTCOME_FAILURE:
		++pBaton->failures;
		printf("ContainerPerc error: %s", pReturnVal->pBaseReturnVals[0].returnString);
	xcase TRANSACTION_OUTCOME_SUCCESS:
		++pBaton->successes;

		if(pBaton->pContainerCallback)
		{
			if(pBaton->pContainerCallback(pBaton->eType, pContainerBaton->id, pBaton->pContainerUserData))
			{
				objRequestContainerMove(NULL, pBaton->eType, pContainerBaton->id, GetAppGlobalType(), GetAppGlobalID(), GLOBALTYPE_OBJECTDB, 0);
			}
		}
		break;
	}

	free(pContainerBaton);

	if(pBaton->requests == pBaton->failures + pBaton->successes && !eaiSize(&pBaton->pList->eaiContainers))
	{
		if(pBaton->pCallback)
		{
			if(pBaton->pCallback(pBaton->eType, pBaton->requests, pBaton->offset, pBaton->pUserData) && pBaton->offset >= 0)
			{
				pBaton->requests = 0;
				pBaton->successes = 0;
				pBaton->failures = 0;
				TimedCallback_Run(TestServer_ContainerPerc_Step, pBaton, 0.0f);
				return;
			}
		}

		StructDestroy(parse_ContainerList, pBaton->pList);
		free(pBaton);
	}
}

static void TestServer_ContainerPerc_MoveContainers(TimedCallback *pCallback, F32 timeSinceLastCallback, TestServerPercBaton *pBaton)
{
	int i = 0;
	int numContainers = objCountOwnedContainersWithType(pBaton->eType) + giActiveMoves;

	while(i + numContainers < TESTSERVER_MAX_CONTAINERS && ++i <= TESTSERVER_CONTAINER_MINI_BATCH_SIZE)
	{
		ContainerBaton *pContainerBaton = calloc(1, sizeof(ContainerBaton));
		pContainerBaton->id = eaiPop(&pBaton->pList->eaiContainers);
		pContainerBaton->pBaton = pBaton;

		++pBaton->requests;
		++giActiveMoves;
		objRequestContainerMove(objCreateManagedReturnVal(TestServer_ContainerPerc_MoveContainerCB, pContainerBaton), pBaton->eType, pContainerBaton->id, GLOBALTYPE_OBJECTDB, 0, GetAppGlobalType(), GetAppGlobalID());

		if(!eaiSize(&pBaton->pList->eaiContainers))
		{
			break;
		}
	}

	if(eaiSize(&pBaton->pList->eaiContainers))
	{
		TimedCallback_Run(TestServer_ContainerPerc_MoveContainers, pBaton, 0.1f);
	}
}

static void TestServer_ContainerPerc_PercContainersCB(TransactionReturnVal *pReturnVal, TestServerPercBaton *pBaton)
{
	switch(RemoteCommandCheck_dbPercContainers(pReturnVal, &pBaton->pList))
	{
	case TRANSACTION_OUTCOME_FAILURE:
		printf("ContainerPerc failure - couldn't get container list at index %d.\n", pBaton->offset);
		free(pBaton);
	xcase TRANSACTION_OUTCOME_SUCCESS:
		pBaton->offset = pBaton->pList->storeindex;
		TimedCallback_Run(TestServer_ContainerPerc_MoveContainers, pBaton, 0.0f);
		break;
	}
}

void TestServer_ContainerPerc_Step(TimedCallback *pCallback, F32 timeSinceLastCallback, TestServerPercBaton *pBaton)
{
	RemoteCommand_dbPercContainers(objCreateManagedReturnVal(TestServer_ContainerPerc_PercContainersCB, pBaton), GLOBALTYPE_OBJECTDB, 0, pBaton->eType, pBaton->offset, pBaton->batch);
}

void TestServer_ContainerPerc_Generic(GlobalType eType, int batch, int offset, PercedContainerCallback pContainerCallback, void *pContainerUserData, ContainerPercCallback pCallback, void *pUserData)
{
	TestServerPercBaton *pBaton = calloc(1, sizeof(TestServerPercBaton));
	pBaton->eType = eType;
	pBaton->batch = batch;
	pBaton->offset = offset;

	pBaton->pContainerCallback = pContainerCallback;
	pBaton->pContainerUserData = pContainerUserData;

	pBaton->pCallback = pCallback;
	pBaton->pUserData = pUserData;

	TimedCallback_Run(TestServer_ContainerPerc_Step, pBaton, 0.0f);
}

AUTO_COMMAND ACMD_NAME(EntityPercolate);
void TestServer_EntityPercolate(int batch)
{
	TestServer_ContainerPerc_Generic(GLOBALTYPE_ENTITYPLAYER, batch, 0, TestServer_ContainerPerc_ContainerComplete, NULL, TestServer_ContainerPerc_Complete, NULL);
}