
#include "estring.h"
#include "timing.h"
#include "gimmeDLLWrapper.h"
#include "stringcache.h"
#include "ResourceSystem_Internal.h"
#include "FolderCache.h"
#include "fileutil.h"
#include "ResourceDataBackend.h"
#include "..\..\libs\patchClientLib\patchtrivia.h"
#include "mutex.h"
#include "memlog.h"
#include "UtilitiesLib.h"

AUTO_RUN_ANON(memBudgetAddMapping(__FILE__, BUDGET_GameSystems););

#ifndef NO_EDITORS
// Attempt to check out a resource
bool resGimmeCheckoutResource(ResourceDictionary *pDictionary, void * pResource, const char *pResourceName, char **estrOut)
{
	void *pOldResource;

	const char *fileName = NULL;
	const char *oldFileName = NULL;

	fileName = ParserGetFilename(pDictionary->pDictTable, pResource);

	pOldResource = resGetObjectFromDict(pDictionary, pResourceName);

	if (pOldResource)
	{
		oldFileName = ParserGetFilename(pDictionary->pDictTable, pOldResource);
		if (oldFileName == fileName)
		{
			oldFileName = NULL;
			// Same name, don't bother checking it out twice
		}
	}

	if (!fileName)
	{		
		return false;
	}

	return true;
}

// Attempt to undo a checkout of a resource
bool resGimmeUndoCheckoutResource(ResourceDictionary *pDictionary, void * pResource, const char *pResourceName, char **estrOut)
{
	const char *fileName;

	fileName = ParserGetFilename(pDictionary->pDictTable, pResource);

	if (!fileName)
	{
		return false;
	}
	
	return true;
}

static StashTable sResourceFilenames;

MemLog res_memlog;

static void resGimmeFileChanged(const char *relpath, int when)
{
	const char *pSharedName = allocFindString(relpath);
	ResourceDictionary *resDict;

	if (!relpath || !relpath[0])
		return;
	if (!pSharedName)
	{
		memlog_printf(&res_memlog, "Callback on non-shared: %s", relpath);
		return;
	}

	if (stashFindPointer(sResourceFilenames, relpath, &resDict))
	{
		int i;
		memlog_printf(&res_memlog, "%s Callback on filename: %s", timeGetLocalDateString(), pSharedName);
		for (i = 0; i < eaSize(&resDict->pDictInfo->ppInfos); i++)
		{
			ResourceInfo *resInfo = resDict->pDictInfo->ppInfos[i];
			if (resInfo->resourceLocation == pSharedName)
			{
				resGimmeCheckWritable(resDict, resInfo, 0, true);
				resUpdateResourceLockedStatus(resDict, resInfo->resourceName);
			}
		}
	}
}
#endif

// Checks the read-only status against the filesystem for all ResourceInfos
AUTO_COMMAND;
void resVerifyWritable(void)
{
}

#ifndef NO_EDITORS
void resGimmeCheckWritable(ResourceDictionary *resDict, ResourceInfo *resInfo, bool bNoLocationFile, bool bForReload)
{
	void *oldPointer;
	ResourceStatus *pStatus;

	if (!resDict || !resInfo || !resInfo->resourceLocation)
	{
		return;
	}

	if (bNoLocationFile)
	{
		pStatus = resGetOrCreateStatus(resDict, resInfo->resourceName);
		pStatus->pResourceOwner = allocAddString(gimmeDLLQueryUserName());
	}
	else 
	{
		if (bForReload)
		{
			// Callback, do a little extra work
			if (!fileIsReadOnlyDoNotTrustFolderCache(resInfo->resourceLocation))
			{
				memlog_printf(&res_memlog, "    file is writeable");
				pStatus = resGetOrCreateStatus(resDict, resInfo->resourceName);
				pStatus->pResourceOwner = allocAddString(gimmeDLLQueryUserName());
			}
			else
			{
				memlog_printf(&res_memlog, "    file is read-only");
				pStatus = resGetStatus(resDict, resInfo->resourceName);
				if (pStatus)
				{
					pStatus->pResourceOwner = NULL;
				}
			}
		} else {
			// Startup, do it fast!
			if (!fileIsReadOnly(resInfo->resourceLocation))
			{
				pStatus = resGetOrCreateStatus(resDict, resInfo->resourceName);
				pStatus->pResourceOwner = allocAddString(gimmeDLLQueryUserName());
			}
			else
			{
				pStatus = resGetStatus(resDict, resInfo->resourceName);
				if (pStatus)
				{
					pStatus->pResourceOwner = NULL;
				}
			}
		}
	}

	if (!stashFindPointer(sResourceFilenames, resInfo->resourceLocation, &oldPointer))
	{
		stashAddPointer(sResourceFilenames, resInfo->resourceLocation, resDict, false);
	}
}

void resGimmeCheckRepositoryInfo(ResourceDictionary *resDict, ResourceInfo *resInfo, bool bNoLocation)
{
	char fullname[MAX_PATH];

	if (!resDict)
	{
		return;
	}

	fileLocateWrite(resInfo->resourceLocation, fullname);

/*	switch (gimmeDLLQueryFileStatus(fullname))
	{
	case GIMME_STATUS_CHECKED_OUT_BY_ME:
		resInfo->bCheckedOut = true;
		resInfo->bNew = false;
		break;
	case GIMME_STATUS_NOT_CHECKED_OUT_BY_ME:
	case GIMME_STATUS_UNKNOWN:
		resInfo->bCheckedOut = false;
		resInfo->bNew = false;
		break;
	case GIMME_STATUS_NEW_FILE:
		resInfo->bCheckedOut = false;
		resInfo->bNew = true;
		break;
	}*/
}

void resGimmeApplyResourceActions(ResourceActionList *pHolder)
{
/*	int ret;
	int i;

	// Do reverts first;
	const char **names_array = NULL;
	for (i = 0; i < eaSize(&pHolder->ppActions); i++)
	{
		ResourceAction *pAction = pHolder->ppActions[i];

		if (pAction->actionType == kResAction_Undo_Checkout && pAction->resInfo.resourceLocation)
		{
			eaPushUnique(&names_array, pAction->resInfo.resourceLocation);
		}
	}

	ret = gimmeDLLDoOperations(names_array, GIMME_UNDO_CHECKOUT, GIMME_QUIET);
	if (ret != GIMME_NO_ERROR)
	{
		 Alertf("Files unable to be reverted (%s)", gimmeDLLGetErrorString(ret));
	}

	eaClear(&names_array);

	for (i = 0; i < eaSize(&pHolder->ppActions); i++)
	{
		ResourceAction *pAction = pHolder->ppActions[i];

		if (pAction->actionType == kResAction_Check_In && pAction->resInfo.resourceLocation)
		{
			eaPushUnique(&names_array, pAction->resInfo.resourceLocation);
		}
	}

	ret = gimmeDLLDoOperations(names_array, GIMME_CHECKIN, GIMME_QUIET);
	if (ret != GIMME_NO_ERROR)
	{
		Alertf("Files unable to be checked in (%s)", gimmeDLLGetErrorString(ret));
	}
	eaDestroy(&names_array);*/
}

bool resGimmeWriteDictionaryInfo(ResourceDictionary *pDict)
{
	int result = false;
	char triviaPath[MAX_PATH], rootPath[MAX_PATH];
	if (getPatchTriviaList(SAFESTR(triviaPath), SAFESTR(rootPath), fileLocalDataDir()))
	{
		ThreadAgnosticMutex binMutex = 0;
		ResourceInfoHolder infoHolder = {0};
		char dictInfoPath[MAX_PATH], mutexName[MAX_PATH];
		sprintf(dictInfoPath, "%s/.patch/%s.dictinfo", rootPath, pDict->pDictName);

		makeLegalMutexName(mutexName, dictInfoPath);
		binMutex = acquireThreadAgnosticMutex(mutexName);

		eaCopy(&infoHolder.ppInfos, &pDict->pDictInfo->ppInfos);

		result = ParserWriteTextFile(dictInfoPath, parse_ResourceInfoHolder, &infoHolder, 0, 0);

		if (binMutex)
		{
			releaseThreadAgnosticMutex(binMutex);
		}
		eaDestroy(&infoHolder.ppInfos);
	}
	return result;
}

bool resGimmeReadDictionaryInfo(ResourceDictionary *pDict)
{
	int result = false;
	char triviaPath[MAX_PATH], rootPath[MAX_PATH];
	if (getPatchTriviaList(SAFESTR(triviaPath), SAFESTR(rootPath), fileLocalDataDir()))
	{
		ThreadAgnosticMutex binMutex = 0;
		ResourceInfoHolder infoHolder = {0};
		char dictInfoPath[MAX_PATH], mutexName[MAX_PATH], binName[MAX_PATH];
		sprintf(dictInfoPath, "%s/.patch/%s.dictinfo", rootPath, pDict->pDictName);
		sprintf(binName, "%s.bin", dictInfoPath);

		makeLegalMutexName(mutexName, dictInfoPath);
		binMutex = acquireThreadAgnosticMutex(mutexName);

		result = fileExists(dictInfoPath);

		if (result)
			result = ParserLoadFiles(0, dictInfoPath, binName, PARSER_OPTIONALFLAG, parse_ResourceInfoHolder, &infoHolder);

		if (result)
		{
			int i;
			eaCopy(&pDict->pDictInfo->ppInfos, &infoHolder.ppInfos);
			for (i = 0; i < eaSize(&pDict->pDictInfo->ppInfos); i++)
			{
				resGimmeCheckWritable(pDict, pDict->pDictInfo->ppInfos[i], 0, false);
			}
		}
		else
		{
			eaClearStruct(&infoHolder.ppInfos, parse_ResourceInfo);
		}

		if (binMutex)
		{
			releaseThreadAgnosticMutex(binMutex);
		}
		eaDestroy(&infoHolder.ppInfos);
	}
	return result;
}



void resGimmeInitializeDataBackend(void)
{
	int init_size = IsServer() ? 65536 : 64;
	gResourceBackend.CheckoutCB = resGimmeCheckoutResource;
	gResourceBackend.UndoCheckoutCB = resGimmeUndoCheckoutResource;
	gResourceBackend.CheckWritableCB = resGimmeCheckWritable;
	gResourceBackend.CheckStatusCB = resGimmeCheckRepositoryInfo;
	gResourceBackend.ApplyActionsCB = resGimmeApplyResourceActions;

	sResourceFilenames = stashTableCreateWithStringKeys(init_size, StashDefault);

	//if this is turned on during makebins, it ends up buffering up zillions of callback changes which never get run, and
	//we run out of memory
	if (!gbMakeBinsAndExit)
	{
		FolderCacheSetCallback(FOLDER_CACHE_CALLBACK_ATTRIB_CHANGE|FOLDER_CACHE_CALLBACK_UPDATE, "*", resGimmeFileChanged);	
	}
}

// Production editing functions

// Attempt to check out a resource
bool resProductionCheckoutResource(ResourceDictionary *pDictionary, void * pResource, const char *pResourceName, char **estrOut)
{
	char nameSpace[RESOURCE_NAME_MAX_SIZE];
	char baseName[RESOURCE_NAME_MAX_SIZE];

	ResourceCache *pCache = resGetCacheWithEditingLogin();
	ResourceNameSpace *pSpace = NULL;

	if (pCache && resExtractNameSpace(pResourceName, nameSpace, baseName))
	{
		int i;

		pSpace = resNameSpaceGetByName(nameSpace);
		if (pSpace)
		{
			if (!eaSize(&pSpace->ppWritableAccounts))
				return true;

			for (i = 0; i < eaSize(&pSpace->ppWritableAccounts); i++)
			{
				if (!stricmp(pSpace->ppWritableAccounts[i], pCache->userLogin))
				{
					return true;
				}
			}
		}
	}

	if (!pCache) {
		estrPrintf(estrOut,"Do not have access to namespace -- No resource cache");
	} else if (!pSpace) {
		estrPrintf(estrOut,"Do not have access to namespace -- Namespace %s not found", nameSpace);
	} else {
		estrPrintf(estrOut,"Do not have access to namespace -- WritableAccounts is set, but account is not in the list");
	}
	return false;
}

// Attempt to undo a checkout of a resource
bool resProductionUndoCheckoutResource(ResourceDictionary *pDictionary, void * pResource, const char *pResourceName, char **estrOut)
{
	return true;
}

void resProductionCheckWritable(ResourceDictionary *resDict, ResourceInfo *resInfo, bool bNoLocationFile, bool bForReload)
{
	ResourceStatus *pStatus;
	char nameSpace[RESOURCE_NAME_MAX_SIZE];
	char baseName[RESOURCE_NAME_MAX_SIZE];


	if (!resDict || !resInfo || !resInfo->resourceLocation)
	{
		return;
	}


	if (resExtractNameSpace(resInfo->resourceName, nameSpace, baseName))
	{
		ResourceNameSpace *pSpace = resNameSpaceGetByName(nameSpace);
		if (!pSpace || !eaSize(&pSpace->ppWritableAccounts))
		{			
			pStatus = resGetStatus(resDict, resInfo->resourceName);
			if (pStatus)
			{
				pStatus->pResourceOwner = NULL;
			}
		}
		else
		{
			pStatus = resGetOrCreateStatus(resDict, resInfo->resourceName);
			pStatus->pResourceOwner = allocAddString(pSpace->ppWritableAccounts[0]);
		}
	}
	else
	{
		pStatus = resGetStatus(resDict, resInfo->resourceName);
		if (pStatus)
		{
			pStatus->pResourceOwner = NULL;
		}
	}
}

void resProductionCheckRepositoryInfo(ResourceDictionary *resDict, ResourceInfo *resInfo, bool bNoLocation)
{
	return;
}

void resProductionApplyResourceActions(ResourceActionList *pHolder)
{
	return;
}



void resProductionInitializeDataBackend(void)
{
	gResourceBackend.CheckoutCB = resProductionCheckoutResource;
	gResourceBackend.UndoCheckoutCB = resProductionUndoCheckoutResource;
	gResourceBackend.CheckWritableCB = resProductionCheckWritable;
	gResourceBackend.CheckStatusCB = resProductionCheckRepositoryInfo;
	gResourceBackend.ApplyActionsCB = resProductionApplyResourceActions;
}

#endif

AUTO_RUN;
void RegisterCorrectDataBackend(void)
{
#ifndef NO_EDITORS
	if (isDevelopmentMode())
	{
		resGimmeInitializeDataBackend();
	}
	else if (isProductionMode() && gConf.bUserContent)
	{
		resProductionInitializeDataBackend();
	}
#endif
}
