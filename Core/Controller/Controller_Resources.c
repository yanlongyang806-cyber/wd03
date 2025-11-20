#include "controller.h"
#include "ResourceInfo.h"
#include "StringUtil.h"
#include "controller_h_ast.h"
#include "controllerpub_h_ast.h"

void *GetServerCB(const char *pDictName, const char *itemName, void *pUserData)
{
	char nameCopy[512];
	char *pUnderscore;
	
	GlobalType eType;
	ContainerID iID = 0;
	
	strcpy(nameCopy, itemName);
	pUnderscore = strchr(nameCopy, '_');
	if (!pUnderscore)
	{
		return NULL;
	}

	*pUnderscore = 0;
	eType = NameToGlobalType(nameCopy);
	StringToUint(pUnderscore+1, &iID);

	return FindServerFromID(eType, iID);
}

bool GetTypeAndIDServerCB(const char *pDictName, const char *itemName, GlobalType *pOutType, ContainerID *pOutID, void *pUserData)
{
	TrackedServerState *pServer = GetServerCB(pDictName, itemName, pUserData);

	if (pServer)
	{
		*pOutType = pServer->eContainerType;
		*pOutID = pServer->iContainerID;
		return true;
	}

	return false;
}

int GetNumServersCB(const char *pDictName, void *pUserData, enumResDictFlags eFlags)
{
	return giTotalNumServers;
}


bool InitIteratorServerCB(const char *pDictName, ResourceIterator *pIterator, void *pUserData)
{
	pIterator->index = 0;
	pIterator->pUserData = gpServersByType[0];

	return true;
}

//the iterator generally points to the NEXT thing it will return
bool GetNextServerCB(ResourceIterator *pIterator, const char **ppOutName, void **ppOutObj, void *pUserData)
{
	TrackedServerState *pServer;

	if (pIterator->index >= GLOBALTYPE_MAXTYPES)
	{
		return false;
	}

	pServer = pIterator->pUserData;

	while (!pServer)
	{
		pIterator->index++;
		if (pIterator->index >= GLOBALTYPE_MAXTYPES)
		{
			return false;
		}
		pServer = gpServersByType[pIterator->index];
	}

	*ppOutObj = pServer;
	*ppOutName = pServer->uniqueName;

	pIterator->pUserData = pServer->pNext;

	return true;
}
	


void *GetUniqueServerCB(const char *pDictName, const char *itemName, void *pUserData)
{
	char nameCopy[512];
	char *pUnderscore;
	
	GlobalType eType;
	ContainerID iID = 0;
	
	strcpy(nameCopy, itemName);
	pUnderscore = strchr(nameCopy, '_');
	if (!pUnderscore)
	{
		return NULL;
	}
	*pUnderscore = 0;
	eType = NameToGlobalType(nameCopy);
	StringToUint(pUnderscore+1, &iID);

	return FindServerFromID(eType, iID);
}

bool GetTypeAndIDUniqueServerCB(const char *pDictName, const char *itemName, GlobalType *pOutType, ContainerID *pOutID, void *pUserData)
{
	TrackedServerState *pServer = GetServerCB(pDictName, itemName, pUserData);

	if (pServer)
	{
		*pOutType = pServer->eContainerType;
		*pOutID = pServer->iContainerID;
		return true;
	}

	return false;
}

int GetNumUniqueServersCB(const char *pDictName, void *pUserData, enumResDictFlags eFlags)
{
	int iCount = 0;
	int i;

	for (i = 0; i < GLOBALTYPE_MAXTYPES; i++)
	{
		if (gServerTypeInfo[i].bIsUnique)
		{
			iCount += giTotalNumServersByType[i];
		}
	}

	return iCount;
}


bool InitIteratorUniqueServerCB(const char *pDictName, ResourceIterator *pIterator, void *pUserData)
{
	pIterator->index = 0;
	pIterator->pUserData = gpServersByType[0];



	return true;
}

//the iterator generally points to the NEXT thing it will return
bool GetNextUniqueServerCB(ResourceIterator *pIterator, const char **ppOutName, void **ppOutObj, void *pUserData)
{
	TrackedServerState *pServer;

	if (pIterator->index >= GLOBALTYPE_MAXTYPES)
	{
		return false;
	}

	pServer = pIterator->pUserData;

	while (!pServer)
	{
		do
		{
			pIterator->index++;
			if (pIterator->index >= GLOBALTYPE_MAXTYPES)
			{
				return false;
			}
		} 
		while (!gServerTypeInfo[pIterator->index].bIsUnique);

		pServer = gpServersByType[pIterator->index];
	}

	*ppOutObj = pServer;
	*ppOutName = pServer->uniqueName;

	pIterator->pUserData = pServer->pNext;

	return true;
}
	


int GetNumScarceServersCB(const char *pDictName, void *pUserData, enumResDictFlags eFlags)
{
	int iCount = 0;
	int i;

	for (i = 0; i < GLOBALTYPE_MAXTYPES; i++)
	{
		if (gServerTypeInfo[i].bIsScarce)
		{
			iCount += giTotalNumServersByType[i];
		}
	}

	return iCount;
}


bool InitIteratorScarceServerCB(const char *pDictName, ResourceIterator *pIterator, void *pUserData)
{
	pIterator->index = 0;
	pIterator->pUserData = gpServersByType[0];



	return true;
}

//the iterator generally points to the NEXT thing it will return
bool GetNextScarceServerCB(ResourceIterator *pIterator, const char **ppOutName, void **ppOutObj, void *pUserData)
{
	TrackedServerState *pServer;

	if (pIterator->index >= GLOBALTYPE_MAXTYPES)
	{
		return false;
	}

	pServer = pIterator->pUserData;

	while (!pServer)
	{
		do
		{
			pIterator->index++;
			if (pIterator->index >= GLOBALTYPE_MAXTYPES)
			{
				return false;
			}
		} 
		while (!gServerTypeInfo[pIterator->index].bIsScarce);

		pServer = gpServersByType[pIterator->index];
	}

	*ppOutObj = pServer;
	*ppOutName = pServer->uniqueName;

	pIterator->pUserData = pServer->pNext;

	return true;
}
	







void *GetMachineCB(const char *pDictName, const char *itemName, void *pUserData)
{
	return FindMachineByName(itemName);
}


bool GetTypeAndIDMachineCB(const char *pDictName, const char *itemName, GlobalType *pOutType, ContainerID *pOutID, void *pUserData)
{
	TrackedMachineState *pMachine = GetMachineCB(pDictName, itemName, pUserData);
	if (pMachine)
	{
		*pOutType = GLOBALTYPE_MACHINE;
		*pOutID = pMachine - gTrackedMachines;
	}
	return false;
}


int GetNumMachinesCB(const char *pDictName, void *pUserData, enumResDictFlags eFlags)
{
	return giNumMachines;
}


bool InitIteratorMachineCB(const char *pDictName, ResourceIterator *pIterator, void *pUserData)
{
	pIterator->index = 0;

	return true;
}

//the iterator generally points to the NEXT thing it will return
bool GetNextMachineCB(ResourceIterator *pIterator, const char **ppOutName, void **ppOutObj, void *pUserData)
{
	if (pIterator->index < giNumMachines)
	{
		*ppOutObj = &gTrackedMachines[pIterator->index];
		*ppOutName = gTrackedMachines[pIterator->index++].machineName;
		return true;
	}

	return false;
}
	



void *GetGameServerCB(const char *pDictName, const char *itemName, void *pUserData)
{
	char nameCopy[512];
	char *pUnderscore;
	
	ContainerID iID = 0;
	
	strcpy(nameCopy, itemName);
	pUnderscore = strchr(nameCopy, '_');
	if (!pUnderscore)
	{
		return NULL;
	}
	*pUnderscore = 0;
	StringToUint(pUnderscore+1, &iID);

	return FindServerFromID(GLOBALTYPE_GAMESERVER, iID);
}

bool GetTypeAndIDGameServerCB(const char *pDictName, const char *itemName, GlobalType *pOutType, ContainerID *pOutID, void *pUserData)
{
	TrackedServerState *pServer = GetGameServerCB(pDictName, itemName, pUserData);

	if (pServer)
	{
		*pOutType = pServer->eContainerType;
		*pOutID = pServer->iContainerID;
		return true;
	}

	return false;
}



int GetNumGameServersCB(const char *pDictName, void *pUserData, enumResDictFlags eFlags)
{
	return giTotalNumServersByType[GLOBALTYPE_GAMESERVER];
}


bool InitIteratorGameServerCB(const char *pDictName, ResourceIterator *pIterator, void *pUserData)
{
	pIterator->pUserData = gpServersByType[GLOBALTYPE_GAMESERVER];

	return true;
}

//the iterator generally points to the NEXT thing it will return
bool GetNextGameServerCB(ResourceIterator *pIterator, const char **ppOutName, void **ppOutObj, void *pUserData)
{
	TrackedServerState *pServer = pIterator->pUserData;

	if (!pServer)
	{
		return false;
	}

	*ppOutObj = pServer;
	*ppOutName = pServer->uniqueName;

	pIterator->pUserData = pServer->pNext;

	return true;
}
	






















void *GetClientControllerCB(const char *pDictName, const char *itemName, void *pUserData)
{
	char nameCopy[512];
	char *pUnderscore;
	
	ContainerID iID = 0;
	
	strcpy(nameCopy, itemName);
	pUnderscore = strchr(nameCopy, '_');
	if (!pUnderscore)
	{
		return NULL;
	}
	*pUnderscore = 0;
	StringToUint(pUnderscore+1, &iID);

	return FindServerFromID(GLOBALTYPE_TESTCLIENT, iID);
}

bool GetTypeAndIDClientControllerCB(const char *pDictName, const char *itemName, GlobalType *pOutType, ContainerID *pOutID, void *pUserData)
{
	TrackedServerState *pServer = GetClientControllerCB(pDictName, itemName, pUserData);

	if (pServer)
	{
		*pOutType = pServer->eContainerType;
		*pOutID = pServer->iContainerID;
		return true;
	}

	return false;
}



int GetNumClientControllersCB(const char *pDictName, void *pUserData, enumResDictFlags eFlags)
{
	return giTotalNumServersByType[GLOBALTYPE_TESTCLIENT];
}


bool InitIteratorClientControllerCB(const char *pDictName, ResourceIterator *pIterator, void *pUserData)
{
	pIterator->pUserData = gpServersByType[GLOBALTYPE_TESTCLIENT];

	return true;
}

//the iterator generally points to the NEXT thing it will return
bool GetNextClientControllerCB(ResourceIterator *pIterator, const char **ppOutName, void **ppOutObj, void *pUserData)
{
	TrackedServerState *pServer = pIterator->pUserData;

	if (!pServer)
	{
		return false;
	}

	*ppOutObj = pServer;
	*ppOutName = pServer->uniqueName;

	pIterator->pUserData = pServer->pNext;

	return true;
}
	


void GameServerGetVerboseNameCB(const char *pDictName, void *pObject, void *pUserData, char **ppOutString)
{
	TrackedServerState *pState = pObject;
	if (pState->pGameServerSpecificInfo)
	{
		estrPrintf(ppOutString, "GS[%u](%s)",
			pState->iContainerID, pState->pGameServerSpecificInfo->mapNameShort[0] ? pState->pGameServerSpecificInfo->mapNameShort : "starting up");
	}
	else
	{
		estrPrintf(ppOutString, "GS[%u](starting up)", pState->iContainerID);
	}
}


AUTO_RUN;
void ControllerInitResources(void)
{
	resRegisterDictionary("Servers", RESCATEGORY_SYSTEM, 0, parse_TrackedServerState,
		GetServerCB,
		GetNumServersCB,
		NULL,
		NULL,
		InitIteratorServerCB,
		GetNextServerCB,
		NULL,
		NULL,
		NULL,
		GetTypeAndIDServerCB,
		NULL, NULL, NULL);

	resRegisterDictionary("UniqueServers", RESCATEGORY_SYSTEM, 0, parse_TrackedServerState,
		GetUniqueServerCB,
		GetNumUniqueServersCB,
		NULL,
		NULL,
		InitIteratorUniqueServerCB,
		GetNextUniqueServerCB,
		NULL,
		NULL,
		NULL,
		GetTypeAndIDUniqueServerCB,
		NULL, NULL, NULL);

	resRegisterDictionary("ScarceServers", RESCATEGORY_SYSTEM, 0, parse_TrackedServerState,
		GetUniqueServerCB,
		GetNumScarceServersCB,
		NULL,
		NULL,
		InitIteratorScarceServerCB,
		GetNextScarceServerCB,
		NULL,
		NULL,
		NULL,
		GetTypeAndIDUniqueServerCB,
		NULL, NULL, NULL);

	resRegisterDictionary("Machines", RESCATEGORY_SYSTEM, 0, parse_TrackedMachineState,
		GetMachineCB,
		GetNumMachinesCB,
		NULL,
		NULL,
		InitIteratorMachineCB,
		GetNextMachineCB,
		NULL,
		NULL,
		NULL,
		GetTypeAndIDMachineCB,
		NULL, NULL, NULL);

	resRegisterDictionary("GameServers", RESCATEGORY_SYSTEM, 0, parse_TrackedServerState,
		GetGameServerCB,
		GetNumGameServersCB,
		NULL,
		NULL,
		InitIteratorGameServerCB,
		GetNextGameServerCB,
		NULL,
		NULL,
		NULL,
		GetTypeAndIDGameServerCB,
		GameServerGetVerboseNameCB, NULL, NULL);

	resRegisterDictionary("ClientControllers", RESCATEGORY_SYSTEM, 0, parse_TrackedServerState,
		GetClientControllerCB,
		GetNumClientControllersCB,
		NULL,
		NULL,
		InitIteratorClientControllerCB,
		GetNextClientControllerCB,
		NULL,
		NULL,
		NULL,
		GetTypeAndIDClientControllerCB,
		NULL, NULL, NULL);

}