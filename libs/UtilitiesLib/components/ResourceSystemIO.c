#include "net/net.h"
#include "structnet.h"
#include "file.h"
#include "estring.h"
#include "structPack.h"
#include "timing.h"
#include "StringCache.h"
#include "ResourceSystem_Internal.h"
#include "SharedMemory.h"
#include "MemoryPool.h"
#include "Message.h"
#include "logging.h"
#include "AutoGen/ResourceSystem_Internal_h_ast.h"
#include "ContinuousBuilderSupport.h"
#include "windefinclude.h"
#include "stringutil.h"
#include "utilitiesLib.h"

// AMA - For Debugging
// Remove eventually
char *g_pchCommentString = NULL;

#define DEFAULT_CONTAINER_EXCLUDE_FLAGS ((StructTypeField)(TOK_SERVER_ONLY | TOK_CLIENT_ONLY | TOK_EDIT_ONLY))

ResourceCache **ppAllResourceCaches = NULL;

bool g_DisableNonExistantResError = false;


//stuff for VERBOSE_PERDICT_LOGGING, turn it on or off at the bottom of ResourceSystem_Internal.h, and then
//add -VerboseResourceLoggingPerDict dictname to your command line

char *spDictNameForVerboseResourceLogging = NULL;
AUTO_COMMAND ACMD_CMDLINEORPUBLIC;
void VerboseResourceLoggingPerDict(char *pDictionaryName)
{
#ifndef VERBOSE_PERDICT_LOGGING
	AssertOrAlert("Bad_Perdict_Logging", "VerboseResourceLoggingPerDict accomplishes nothing if you don't recompile with VERBOSE_PERDICT_LOGGING on");
#endif
	estrCopy2(&spDictNameForVerboseResourceLogging, pDictionaryName);
}

#ifdef VERBOSE_PERDICT_LOGGING

void VerboseLoggingPerDict(const char *pFmt, ...)
{
	static char *pFileName = NULL;
	FILE *pFile;
	char *pFullString = NULL;
	static S64 siLastTick = 0;

	estrGetVarArgs(&pFullString, pFmt);

	if (!pFileName)
	{
		estrPrintf(&pFileName, "c:\\temp\\resources_%s_%s_%u.txt", GlobalTypeToName(GetAppGlobalType()), spDictNameForVerboseResourceLogging, timeSecondsSince2000());
	}

	pFile = fopen(pFileName, "at");
	if (pFile)
	{
		if (siLastTick == gUtilitiesLibTicks)
		{
			fprintf(pFile, "%s\n------------------------------------------------------------------\n", pFullString);
		}
		else
		{
			fprintf(pFile, "Tick: %I64d Time: %s\n%s\n------------------------------------------------------------------\n", gUtilitiesLibTicks, timeGetLocalTimeStringFromSecondsSince2000(timeSecondsSince2000()),
				pFullString);
		}
		fclose(pFile);
	}

	siLastTick = gUtilitiesLibTicks;

	estrDestroy(&pFullString);
}


void VerboseLoggingPerDictWithStruct(ParseTable *pTPI, void *pStruct, FORMAT_STR const char* format, ...)
{
	char *pFullString = NULL;
	char *pStructString = NULL;

	estrGetVarArgs(&pFullString, format);
	
	if (pStruct)
	{
		ParserWriteText(&pStructString, pTPI, pStruct, 0, 0, 0);
	}
	else
	{
		estrPrintf(&pStructString, "(none)");
	}

	VerboseLoggingPerDict("%s--\n%s", pFullString, pStructString);

	estrDestroy(&pFullString);
	estrDestroy(&pStructString);
}

#endif


int gLogResourceRequests;
AUTO_CMD_INT(gLogResourceRequests, LogResourceRequests) ACMD_CMDLINE;

static S32 printSubscriptionChanges;
AUTO_CMD_INT(printSubscriptionChanges, printSubscriptionChanges) ACMD_CMDLINE;

static S32 printContainerReceives;
AUTO_CMD_INT(printContainerReceives, printContainerReceives) ACMD_CMDLINE;
#define SHOULD_PRINT_CONTAINER_RECEIVE(pDictionary)										\
	(	printContainerReceives &&														\
		pDictionary &&																	\
		((printContainerReceives & 4) || pDictionary->bIsCopyDictionary))
		
static S32 printContainerSends;
AUTO_CMD_INT(printContainerSends, printContainerSends) ACMD_CMDLINE;

static S32 errorOnSendMissingMessageTranslations;
AUTO_CMD_INT(errorOnSendMissingMessageTranslations, errorOnSendMissingMessageTranslations) ACMD_CMDLINE;

AUTO_RUN_ANON(memBudgetAddMapping(__FILE__, BUDGET_GameSystems););

typedef struct ResourceTraffic
{
	bool bChanged;
	int iNumRequestSent;
	int iNumRequestReceived;

	int iNumResourceSent;
	int iNumResourceReceived;
	int iNumResourceBytesSent;
	int iNumResourceBytesReceived;
} ResourceTraffic;

static ResourceTraffic s_ResourceTraffic;

// General reference request system, will end up calling various callbacks

bool resSetLocationID(DictionaryHandleOrName dictHandle, const char *pResourceName, U32 locationID)
{
	ResourceDictionary *pDictionary = resGetDictionary(dictHandle);
	ResourceStatus *pResourceStatus;

	if (!pDictionary)
	{
		return false;
	}

	pResourceStatus = resGetOrCreateStatus(pDictionary, pResourceName);
	pResourceStatus->iResourceLocationID = locationID;

	return true;
}

bool resIsEditingVersionAvailable(DictionaryHandleOrName dictHandle, const char * pResourceName)
{
	ResourceDictionary *pDictionary = resGetDictionary(dictHandle);
	ResourceStatus *pResourceStatus;	

	if (!pDictionary)
	{
		return false;
	}

	if (!pDictionary->bShouldRequestMissingData)
	{
		return !!resGetObjectFromDict(pDictionary, pResourceName);
	}

	pResourceStatus = resGetStatus(pDictionary, pResourceName);

	if (pResourceStatus)
	{
		return pResourceStatus->bIsEditCopy && pResourceStatus->bResourcePresent;
	}

	return false;
}

U32 resGetLockOwner(DictionaryHandleOrName dictHandle, const char *pResourceName)
{
	ResourceDictionary *pDictionary = resGetDictionary(dictHandle);
	ResourceStatus *pResourceStatus;	

	if (!pDictionary)
	{
		return 0;
	}

	pResourceStatus = resGetStatus(pDictionary, pResourceName);

	if (pResourceStatus)
	{
		return pResourceStatus->iLockOwner;
	}

	return 0;
}

bool resGetLockOwnerIsZero(DictionaryHandleOrName dictHandle, const char *pResourceName)
{
	ResourceDictionary *pDictionary = resGetDictionary(dictHandle);
	ResourceStatus *pResourceStatus;	

	if (!pDictionary)
	{
		return false;
	}

	pResourceStatus = resGetStatus(pDictionary, pResourceName);

	if (pResourceStatus)
	{
		return (pResourceStatus->iLockOwner == 0);
	}

	return false;
}

bool resIsWritable(DictionaryHandleOrName dictHandle, const char * pResourceName)
{	
	ResourceDictionary *pDictionary = resGetDictionary(dictHandle);
	ResourceStatus *pResourceStatus;	

	if (!pDictionary)
	{
		return false;
	}

	pResourceStatus = resGetStatus(pDictionary, pResourceName);

	if (pResourceStatus)
	{
		if (pResourceStatus->iLockOwner)
			return true;
		// TODO: Fix in production
		if (pResourceStatus->pResourceOwner)
			return true;
	}

	return false;
}

U32 resGetLocationID(DictionaryHandleOrName dictHandle, const char *pResourceName)
{
	ResourceDictionary *pDictionary = resGetDictionary(dictHandle);
	ResourceStatus *pResourceStatus;	

	if (!pDictionary)
	{
		return 0;
	}

	pResourceStatus = resGetStatus(pDictionary, pResourceName);

	if (pResourceStatus)
	{
		return pResourceStatus->iResourceLocationID;
	}

	return 0;
}

//client side
void resReRequestMissingResourcesWithReason(DictionaryHandleOrName dictHandle,
											const char* reason)
{
	ResourceDictionary *pDictionary = resGetDictionary(dictHandle);
	StashTableIterator iterator;
	StashElement element;

	if (!pDictionary || !resShouldRequestMissingResources(pDictionary))
	{
		return;
	}

	VERBOSE_LOG_DICT(dictHandle, "Requesting missing resources with reason: %s", reason);

	stashGetIterator(pDictionary->resourceStatusTable, &iterator);

	while (stashGetNextElement(&iterator, &element))
	{
		ResourceStatus *pResourceStatus = stashElementGetPointer(element);
		if (pResourceStatus->bResourceRequested && !pResourceStatus->bResourcePresent)
		{
			pDictionary->pSendRequestCB(pDictionary->pDictName,
										RESREQUEST_GET_RESOURCE,
										pResourceStatus->pResourceName,
										NULL,
										reason);
		}
	}
}


void resRequestOpenResource(DictionaryHandleOrName dictHandle, const char *pResourceName)
{
	ResourceActionList tempList = {0};

	VERBOSE_LOG_DICT(dictHandle, "Requesting open resource: %s", pResourceName);

	resAddRequestOpenResource(&tempList, dictHandle, pResourceName);
	resRequestResourceActions(&tempList);
	StructDeInit(parse_ResourceActionList, &tempList);
}


void resRequestLockResource(DictionaryHandleOrName dictHandle, const char *pResourceName, void * pOverrideReferent)
{
	ResourceActionList tempList = {0};

	VERBOSE_LOG_DICT(dictHandle, "Requesting lock resource: %s", pResourceName);

	resAddRequestLockResource(&tempList, dictHandle, pResourceName, pOverrideReferent);
	resRequestResourceActions(&tempList);
	StructDeInit(parse_ResourceActionList, &tempList);
}


void resRequestUnlockResource(DictionaryHandleOrName dictHandle, const char *pResourceName, void * pOverrideReferent)
{
	ResourceActionList tempList = {0};

	VERBOSE_LOG_DICT(dictHandle, "Requesting unlock resource: %s", pResourceName);

	resAddRequestUnlockResource(&tempList, dictHandle, pResourceName, pOverrideReferent);
	resRequestResourceActions(&tempList);
	StructDeInit(parse_ResourceActionList, &tempList);
}

void resRequestSaveResource(DictionaryHandleOrName dictHandle, const char *pResourceName, void * pOverrideReferent)
{
	ResourceActionList tempList = {0};

	VERBOSE_LOG_DICT(dictHandle, "Requesting save resource: %s", pResourceName);

	resAddRequestSaveResource(&tempList, dictHandle, pResourceName, pOverrideReferent);
	resRequestResourceActions(&tempList);
	StructDeInit(parse_ResourceActionList, &tempList);
}


void resAddRequestOpenResource(ResourceActionList *pHolder, DictionaryHandleOrName dictHandle, const char * pResourceName)
{
	ResourceAction *pAction;
	ResourceDictionary *pDictionary = resGetDictionary(dictHandle);	

	if (!pDictionary)
	{
		return;
	}

	pAction = StructCreate(parse_ResourceAction);
	pAction->pDictName = pDictionary->pDictName;
	pAction->pResourceName = allocAddString(pResourceName);
	pAction->eActionType = kResAction_Open;

	eaPush(&pHolder->ppActions, pAction);
}


void resAddRequestLockResource(ResourceActionList *pHolder, DictionaryHandleOrName dictHandle, const char * pResourceName, void * pResource)
{
	ResourceAction *pAction;
	ResourceDictionary *pDictionary = resGetDictionary(dictHandle);	

	if (!pDictionary)
	{
		return;
	}

	pAction = StructCreate(parse_ResourceAction);
	pAction->pDictName = pDictionary->pDictName;
	pAction->pResourceName = allocAddString(pResourceName);
	pAction->eActionType = kResAction_Check_Out;
	if (pResource)
	{
		ParserWriteText(&pAction->estrActionDetails, pDictionary->pDictTable, pResource, WRITETEXTFLAG_FORCEWRITECURRENTFILE, 0, 0);
	}

	eaPush(&pHolder->ppActions, pAction);
}

void resAddRequestUnlockResource(ResourceActionList *pHolder, DictionaryHandleOrName dictHandle, const char * pResourceName, void * pResource)
{
	ResourceAction *pAction;
	ResourceDictionary *pDictionary = resGetDictionary(dictHandle);

	if (!pDictionary)
	{
		return;
	}

	pAction = StructCreate(parse_ResourceAction);
	pAction->pDictName = pDictionary->pDictName;
	pAction->pResourceName = allocAddString(pResourceName);
	pAction->eActionType = kResAction_Revert;
	if (pResource)
	{
		ParserWriteText(&pAction->estrActionDetails, pDictionary->pDictTable, pResource, WRITETEXTFLAG_FORCEWRITECURRENTFILE, 0, 0);
	}

	eaPush(&pHolder->ppActions, pAction);
}

void resAddRequestSaveResource(ResourceActionList *pHolder, DictionaryHandleOrName dictHandle, const char * pResourceName, void * pResource)
{
	ResourceAction *pAction;
	ResourceDictionary *pDictionary = resGetDictionary(dictHandle);
	ResourceStatus *pResourceStatus;
	const char *pRealName = allocAddString(pResourceName);

	if (!pDictionary)
	{
		return;
	}

	pResourceStatus = resGetStatus(pDictionary, pResourceName);

	if (!pResourceStatus || !pResourceStatus->iLockOwner)
	{
		int i, bFoundLock = false;
		for (i = 0; i < eaSize(&pHolder->ppActions); i++)
		{
			pAction = pHolder->ppActions[i];
			if (pAction->pDictName == pDictionary->pDictName && pAction->pResourceName == pRealName && pAction->eActionType == kResAction_Check_Out)
			{
				bFoundLock = true;
				break;
			}
		}
		if (!bFoundLock)
		{		
			Errorf("Attempted to send modification of object %s[%s] without lock!",pDictionary->pDictName,pResourceName);
			return;
		}
	}

	pAction = StructCreate(parse_ResourceAction);
	pAction->pDictName = pDictionary->pDictName;
	pAction->pResourceName = pRealName;
	pAction->eActionType = kResAction_Modify;
	if (pResource)
	{
		ParserWriteText(&pAction->estrActionDetails, pDictionary->pDictTable, pResource, WRITETEXTFLAG_FORCEWRITECURRENTFILE | pDictionary->iParserWriteFlags, 0, 0);
	}

	eaPush(&pHolder->ppActions, pAction);
}


void resRequestAllResourcesInDictionaryWithReason(	DictionaryHandleOrName dictHandle,
													const char* reason)
{
	ResourceDictionary *pDictionary = resGetDictionary(dictHandle);

	assertmsg(isDevelopmentMode() || isProductionEditMode(), "Can't run dev commands in production mode");

	if (!pDictionary || !pDictionary->pSendRequestCB || !resShouldRequestMissingResources(pDictionary))
		return;

	VERBOSE_LOG_DICT(dictHandle, "Requesting all resource with reason: %s", reason);

	pDictionary->bRequestedAllReferents = true;

	pDictionary->pSendRequestCB(pDictionary->pDictName,
								RESREQUEST_GET_ALL_RESOURCES,
								"INVALID",
								NULL,
								reason);
}


void resSetDictionaryEditModeWithReason(DictionaryHandleOrName dictHandle,
										bool enabled,
										const char* reason)
{
	ResourceDictionary *pDictionary = resGetDictionary(dictHandle);

	assertmsg(!enabled || areEditorsAllowed(), "Can't run dev commands in production mode");

	if (!pDictionary)
		return;	

	VERBOSE_LOG_DICT(dictHandle, "Setting edit mode with reaason: %d %s", enabled, reason);

	if (IsClient()) {
		if (!pDictionary->bDataEditingMode && enabled && !isDevelopmentMode() && !isProductionEditMode())
		{
			// It is not okay to try and set a dictionary on the client into edit mode
			// while in production (except in ProductionEdit UGC).  This is only okay in development.
			Errorf("Dictionary %s is attempting to enter edit mode while in production.  Ignoring the request.", pDictionary->pDictName);
			return;
		}
		if (pDictionary->bDataEditingMode && !enabled && isDevelopmentMode())
		{
			// It is not okay to exit edit mode on the client while in development.  
			// It damages all data in all editors and leaves the game in a terrible state.
			Errorf("Dictionary %s is exiting edit mode.  All editors open on this dictionary are now hosed.  Tell the software team if you see this message.", pDictionary->pDictName);
		}
	}

	if(pDictionary->bDataEditingMode == enabled)
	{
		loadstart_printf("Aborting set dictionary %s to edit mode as it's already set ...", pDictionary->pDictName);
		loadend_printf("done");
		return;
	}

	pDictionary->bDataEditingMode = enabled;

	if (pDictionary->bShouldRequestMissingData)
	{	
		loadstart_printf("Setting dictionary %s to edit mode...", pDictionary->pDictName);

		pDictionary->pSendRequestCB(pDictionary->pDictName,
									RESREQUEST_SET_EDITMODE,
									enabled?"ON":"OFF",
									NULL,
									reason);

		if (pDictionary->bRequiresEditingModeToProvideMissing)
			resReRequestMissingResources(dictHandle);

		if (pDictionary->bMustHaveEditDataInEditMode)
			resSyncDictionaryToServer(dictHandle);

		loadend_printf("done");
	}
	else
	{
		devassertmsgf(pDictionary->bShouldSaveResources, "Can't set edit mode on dictionary %s", pDictionary->pDictName);
	}

	if (!enabled)
	{
		PruneUnreferencedList(pDictionary);
	}
}

bool resIsDictionaryEditMode(DictionaryHandleOrName dictHandle)
{
	ResourceDictionary *pDictionary;

	pDictionary = resGetDictionary(dictHandle);

	if (!pDictionary || !pDictionary->pSendRequestCB)
		return false;

	return pDictionary->bDataEditingMode;
}

void resSetDictionaryMustHaveEditCopyInEditMode(DictionaryHandleOrName dictHandle)
{
	ResourceDictionary *pDictionary;

	pDictionary = resGetDictionary(dictHandle);

	if (pDictionary) 
	{
		pDictionary->bMustHaveEditDataInEditMode = true;
	}
}

void resSetDictionaryEditModeServer(DictionaryHandleOrName dictHandle, bool bEnabled)
{
	ResourceDictionary *pDictionary = resGetDictionary(dictHandle);
	
	if (pDictionary->bDataEditingMode != bEnabled)
	{
		if (bEnabled && (pDictionary->iParserLoadFlags & RESOURCELOAD_SHAREDMEMORY)) {
			sharedMemoryEnableEditorMode(); // Set all shared memory to write copy
			g_DisableNonExistantResError = true;
		}

		pDictionary->bDataEditingMode = bEnabled;
	}
}

bool resIsDictionaryFromServer(DictionaryHandleOrName dictHandle)
{
	ResourceDictionary *pDictionary = resGetDictionary(dictHandle);

	if (!pDictionary || !pDictionary->bShouldRequestMissingData || pDictionary->pSendRequestCB != resClientRequestSendReferentCommand)
	{
		return false;
	}
	return true;
}

void resSyncDictionaryToServerWithReason(	DictionaryHandleOrName dictHandle,
											const char* reason)
{
	int i;
	ResourceDictionary *pDictionary = resGetDictionary(dictHandle);
	StashTableIterator iterator;
	StashElement element;
	
	VERBOSE_LOG_DICT(dictHandle, "Sync dict to server with reason: %s", reason);


	if (!pDictionary ||
		!pDictionary->bShouldRequestMissingData ||
		pDictionary->pSendRequestCB != resClientRequestSendReferentCommand)
	{
		return;
	}
	
	if (pDictionary->bVolatileDictionary)
	{
		stashGetIterator(pDictionary->resourceStatusTable, &iterator);
		while (stashGetNextElement(&iterator, &element))
		{
			ResourceStatus *pResourceStatus = stashElementGetPointer(element);
			if (pResourceStatus->bResourcePresent)
			{
				resUpdateObject(pDictionary, pResourceStatus, NULL);
				// This could invalidate pResourceStatus so we have to iterate again
			}
		}

		stashGetIterator(pDictionary->resourceStatusTable, &iterator);

		while (stashGetNextElement(&iterator, &element))
		{
			ResourceStatus *pResourceStatus = stashElementGetPointer(element);
			if (pResourceStatus->bResourceRequested)
			{
				// It's volatile but wasn't cancelled because refs exist, so re-request it
				pDictionary->pSendRequestCB(pDictionary->pDictName,
											RESREQUEST_GET_RESOURCE,
											pResourceStatus->pResourceName,
											NULL,
											reason);
			}
		}

		return;
	}
	
	if (!areEditorsAllowed()) {
		return;
	}
	
	for (i = 0; i < pDictionary->iIndexSubscribed; i++)
	{
		assertmsg(areEditorsAllowed(), "Can't run dev commands in production mode");

		pDictionary->pSendRequestCB(pDictionary->pDictName,
									RESREQUEST_SUBSCRIBE_TO_INDEX,
									"ON",
									NULL,
									reason);
	}

	if (pDictionary->bDataEditingMode)
	{
		assertmsg(areEditorsAllowed(), "Can't run dev commands in production mode");

		pDictionary->pSendRequestCB(pDictionary->pDictName,
									RESREQUEST_SET_EDITMODE,
									"ON",
									NULL,
									reason);
	}

	if (resShouldRequestMissingResources(pDictionary))
	{
		if (pDictionary->bRequestedAllReferents)
		{
			assertmsg(areEditorsAllowed(), "Can't run dev commands in production mode");

			pDictionary->pSendRequestCB(pDictionary->pDictName,
										RESREQUEST_GET_ALL_RESOURCES,
										"INVALID",
										NULL,
										reason);
		}

		stashGetIterator(pDictionary->resourceStatusTable, &iterator);

		while (stashGetNextElement(&iterator, &element))
		{
			ResourceStatus *pResourceStatus = stashElementGetPointer(element);
			if (pResourceStatus->bResourceRequested)
			{					
				if (pResourceStatus->bIsEditCopy)
				{
					assertmsg(areEditorsAllowed(), "Can't run dev commands in production mode");

					pDictionary->pSendRequestCB(pDictionary->pDictName,
												RESREQUEST_OPEN_RESOURCE,
												pResourceStatus->pResourceName,
												NULL,
												reason);
				}
				else if (pResourceStatus->bResourcePresent)
				{	
					// Tell server we already have the resource if we're in dev mode so we get
					// reloads, or if the resource has a namespace for similar reasons.
					// Assumes that non-namespace resources don't change in production mode.
					if (isDevelopmentMode() || resHasNamespace(pResourceStatus->pResourceName)) {
						pDictionary->pSendRequestCB(pDictionary->pDictName,
													RESREQUEST_HAS_RESOURCE,
													pResourceStatus->pResourceName,
													NULL,
													reason);
					}
				}
				else
				{
					pDictionary->pSendRequestCB(pDictionary->pDictName,
												RESREQUEST_GET_RESOURCE,
												pResourceStatus->pResourceName,
												NULL,
												reason);
				}
			}
			else if (pDictionary->bDataEditingMode && !pResourceStatus->bIsEditCopy && pDictionary->bMustHaveEditDataInEditMode)
			{
				// Request edit copy if dictionary requires it
				pDictionary->pSendRequestCB(pDictionary->pDictName,
											RESREQUEST_OPEN_RESOURCE,
											pResourceStatus->pResourceName,
											NULL,
											reason);
			}
		}
	}
}

AUTO_COMMAND;
void resSyncAllDictionariesToServer(void)
{
	ResourceDictionaryIterator iter;
	ResourceDictionary *pDict;

	loadstart_printf("Syncing all dictionaries to server...");

	resDictInitIterator(&iter);

	while (pDict = resDictIteratorGetNextDictionary(&iter))
	{
		if (pDict && pDict->bShouldRequestMissingData)
		{			
			resReRequestMissingResources(pDict->pDictName);
			resSyncDictionaryToServer(pDict->pDictName);
		}
	}

	loadend_printf("done.");
}

void resSubscribeToInfoIndexWithReason(	DictionaryHandleOrName dictHandle,
										bool enabled,
										const char* reason)
{
	ResourceDictionary *pDictionary = resGetDictionary(dictHandle);

	assertmsg(areEditorsAllowed(), "Can't run dev commands in production mode");

	if (!pDictionary || !pDictionary->pSendRequestCB)
		return;

	if (enabled)
	{
		pDictionary->iIndexSubscribed++;
	}
	else
	{
		pDictionary->iIndexSubscribed--;
	}

	pDictionary->pSendRequestCB(pDictionary->pDictName,
								RESREQUEST_SUBSCRIBE_TO_INDEX,
								enabled?"ON":"OFF",
								NULL,
								reason);
}

static int bSubscribedToAllIndices;

void resSubscribeToAllInfoIndicesOnceWithReason(const char* reason)
{
	ResourceDictionaryIterator iter;
	ResourceDictionary *pDict;

	if (bSubscribedToAllIndices)
		return;

	resDictInitIterator(&iter);

	while (pDict = resDictIteratorGetNextDictionary(&iter))
	{
		if (pDict && pDict->bShouldRequestMissingData && pDict->pSendRequestCB == resClientRequestSendReferentCommand && CopyDictionaryNameToGlobalType(pDict->pDictName) == GLOBALTYPE_NONE)
		{			
			resSubscribeToInfoIndexWithReason(pDict->pDictName, true, reason);
		}
	}
	bSubscribedToAllIndices = 1;
}

void resValidateCheckAllRequestedReferences(void)
{
	ResourceDictionary *pDictionary;
	ResourceDictionaryIterator iter;
	bool bAllDownloaded = true;

	if (!bSubscribedToAllIndices)
	{
		return;
	}

	resDictInitIterator(&iter);

	while (pDictionary = resDictIteratorGetNextDictionary(&iter))
	{
		if (pDictionary->bShouldRequestMissingData && pDictionary->iIndexSubscribed > 0)
		{
			if (!pDictionary->bReceivedAllInfos)
			{
				bAllDownloaded = false;
			}
		}
	}

	if (!bAllDownloaded)
	{
		return;
	}

	resDictInitIterator(&iter);

	while (pDictionary = resDictIteratorGetNextDictionary(&iter))
	{
		if (pDictionary->bShouldSaveResources && !pDictionary->bShouldRequestMissingData)
		{
			resValidateResourceReferenceErrors(pDictionary);
		}
	}
}


void resClientRequestEditingLoginWithReason(const char *loginName,
											bool bEnabled,
											const char* reason)
{
	assertmsg(areEditorsAllowed(), "Can't run dev commands in production mode");

	if (bEnabled)
	{	
		resClientRequestSendReferentCommand(GLOBALCOMMANDS_DICTIONARY, RESREQUEST_REQUEST_EDITING_LOGIN, loginName, NULL, reason);
	}
	else
	{
		resClientRequestSendReferentCommand(GLOBALCOMMANDS_DICTIONARY, RESREQUEST_REQUEST_EDITING_LOGOFF, loginName, NULL, reason);
	}
}

void resDictRequestMissingResources(DictionaryHandleOrName dictHandle,
									int iNumUnreferencedReferentsToKeep,
									bool bVolatile,
									resCallback_SendRequest *requestCallback)
{
	ResourceDictionary *pDictionary;

	pDictionary = resGetDictionary(dictHandle);

	assertmsg(pDictionary && !pDictionary->bShouldRequestMissingData, 
		"Invalid dictionary specified to request missing data... must be self-defining and not already requesting");

	assertmsg(pDictionary->pDictTable, "Invalid dictionary specified to request missing data... must have parse table");

	pDictionary->bShouldRequestMissingData = true;
	pDictionary->pSendRequestCB = requestCallback;
	pDictionary->bVolatileDictionary = bVolatile;

	pDictionary->iMaxUnreferencedResources = iNumUnreferencedReferentsToKeep;
}

// Only to be used for changing the iNumUnreferencedReferentsToKeep on the fly
void resDictSetMaxUnreferencedResources(DictionaryHandleOrName dictHandle, int iNumUnreferencedReferentsToKeep)
{
	ResourceDictionary *pDictionary;

	pDictionary = resGetDictionary(dictHandle);
	if (!pDictionary)
		return;
	
	pDictionary->iMaxUnreferencedResources = iNumUnreferencedReferentsToKeep;
	PruneUnreferencedList(pDictionary);
}

bool resFixFilename(DictionaryHandleOrName dictHandle, const char *pResourceName, void * pResource)
{
	ResourceDictionary *pDictionary = resGetDictionary(dictHandle);

	if (!pDictionary || !pResource || !pResourceName)
	{
		return false;
	}

	if (resRunValidate(RESVALIDATE_FIX_FILENAME, pDictionary->pDictName, pResourceName, pResource, -1, NULL) 
		== PARSERESULT_SUCCESS)
	{
		return true;
	}
	return false;
}

void resDictSetUseExtendedName(DictionaryHandleOrName dictHandle, bool bUseExtendedName)
{
	ResourceDictionary *pDictionary;

	pDictionary = resGetDictionary(dictHandle);

	pDictionary->bUseExtendedName = bUseExtendedName;
}

void resDictSetUseAnyName(DictionaryHandleOrName dictHandle, bool bUseAnyName)
{
	ResourceDictionary *pDictionary;

	pDictionary = resGetDictionary(dictHandle);

	pDictionary->bUseAnyName = bUseAnyName;
}

// Reference System net request/update handling

// Client-side functions

// Used by the client to request things from the server
// Sorted
ResourceRequest **ppPendingResourceRequests;

static int CompareResourceRequestNoOperation(const ResourceRequest** a, const ResourceRequest** b)
{
	int result = stricmp((*a)->pDictionaryName, (*b)->pDictionaryName);
	if (result != 0) return result;

	return stricmp((*a)->pResourceName, (*b)->pResourceName);
}

static int CompareResourceRequest(const ResourceRequest** a, const ResourceRequest** b)
{
	int result = stricmp((*a)->pDictionaryName, (*b)->pDictionaryName);
	if (result != 0) return result;

	result = (*a)->operation - (*b)->operation;
	if (result != 0) return result;

	return stricmp((*a)->pResourceName, (*b)->pResourceName);
}

MP_DEFINE(ResourceRequest);

static ResourceRequest *resCreateResourceRequest(int command, const char *pDictionaryName, const char *pResourceName, S32 resourceNameIsPooled, ParseTable *pParseTable)
{
	ResourceRequest *pRequest;

	PERFINFO_AUTO_START_FUNC();
	
	MP_CREATE(ResourceRequest, 64);
	pRequest = MP_ALLOC(ResourceRequest);

	pRequest->operation = command;
	pRequest->pDictionaryName = pDictionaryName;
	pRequest->pRequestParseTable = pParseTable;
	pRequest->resourceNameIsPooled = resourceNameIsPooled;
	pRequest->pResourceName = resourceNameIsPooled ? (char*)pResourceName : strdup(pResourceName);
	
	PERFINFO_AUTO_STOP();
	
	return pRequest;
}

void resDestroyResourceRequest(ResourceRequest *pRequest)
{
	if(!pRequest->resourceNameIsPooled){
		SAFE_FREE(pRequest->pResourceName);
	}

	if (pRequest->pRequestParseTable && pRequest->pRequestData)
	{
		StructDestroyVoid(pRequest->pRequestParseTable,pRequest->pRequestData);
	}

	MP_FREE(ResourceRequest, pRequest);
}

static const char* resGetOperationName(bool isClient, int value){
	const char* name = StaticDefineIntRevLookup(isClient ?
													ResourceRequestTypeEnum :
													ResourceUpdateTypeEnum,
												value);

	return FIRST_IF_SET(name, "Unknown");
}

static S32 printResRequestCorrections;
AUTO_CMD_INT(printResRequestCorrections, printResRequestCorrections) ACMD_CMDLINE;

static bool resPushRequestClient(	ResourceRequest ***pppRequests,
									ResourceRequest *pRequestToSearch)
{
	int idx;
	
	PERFINFO_AUTO_START_FUNC();
	
	idx = (int)eaBFind(*pppRequests, CompareResourceRequest, pRequestToSearch);

	if (stricmp(pRequestToSearch->pDictionaryName, GLOBALCOMMANDS_DICTIONARY) != 0 &&
		*pppRequests && idx != eaSize(pppRequests) && CompareResourceRequest(&pRequestToSearch, &((*pppRequests)[idx])) == 0)
	{
		if(printResRequestCorrections)
		{
			printfColor(COLOR_BRIGHT|COLOR_RED|COLOR_GREEN,
						"Ignoring duplicate request for %s[%s] op %s.\n",
						pRequestToSearch->pDictionaryName,
						pRequestToSearch->pResourceName,
						resGetOperationName(1, pRequestToSearch->operation));
		}
		
		PERFINFO_AUTO_STOP();
		return false; // identical thing is already here
	}
	else
	{
		if (pRequestToSearch->operation == RESREQUEST_CANCEL_REQUEST)
		{
			ResourceRequest tempSearch = *pRequestToSearch;
			ResourceRequest *tempPointer = &tempSearch;
			int iRemoveIdx;

			tempSearch.operation = RESREQUEST_GET_RESOURCE;
			iRemoveIdx = (int)eaBFind(*pppRequests, CompareResourceRequest, tempPointer);

			if (stricmp(tempPointer->pDictionaryName, GLOBALCOMMANDS_DICTIONARY) != 0 &&
				*pppRequests && iRemoveIdx != eaSize(pppRequests) && CompareResourceRequest(&tempPointer, &((*pppRequests)[iRemoveIdx])) == 0)
			{
				ResourceRequest *pDeleted = (*pppRequests)[iRemoveIdx];
				
				if (gLogResourceRequests)
					filelog_printf("resourceSend.log","Cancelling GET due to later CANCEL request: %s %s %s", StaticDefineIntRevLookup(ResourceRequestTypeEnum,pDeleted->operation), pDeleted->pDictionaryName, pDeleted->pResourceName);

				if(printResRequestCorrections)
				{
					printfColor(COLOR_BRIGHT|COLOR_RED|COLOR_GREEN,
								"Canceling get request for %s[%s] op %s.\n",
								pRequestToSearch->pDictionaryName,
								pRequestToSearch->pResourceName,
								resGetOperationName(0, pRequestToSearch->operation));
				}

				resDestroyResourceRequest(pDeleted);
				eaRemove(pppRequests, iRemoveIdx);
				PERFINFO_AUTO_STOP();
				return false;
			}

			tempSearch.operation = RESREQUEST_HAS_RESOURCE;
			iRemoveIdx = (int)eaBFind(*pppRequests, CompareResourceRequest, tempPointer);

			if (stricmp(tempPointer->pDictionaryName, GLOBALCOMMANDS_DICTIONARY) != 0 &&
				*pppRequests && iRemoveIdx != eaSize(pppRequests) && CompareResourceRequest(&tempPointer, &((*pppRequests)[iRemoveIdx])) == 0)
			{
				ResourceRequest *pDeleted = (*pppRequests)[iRemoveIdx];

				if (gLogResourceRequests)
					filelog_printf("resourceSend.log","Cancelling HAS due to later CANCEL request: %s %s %s", StaticDefineIntRevLookup(ResourceRequestTypeEnum,pDeleted->operation), pDeleted->pDictionaryName, pDeleted->pResourceName);

				if(printResRequestCorrections)
				{
					printfColor(COLOR_BRIGHT|COLOR_RED|COLOR_GREEN,
						"Canceling has request for %s[%s] op %s.\n",
						pRequestToSearch->pDictionaryName,
						pRequestToSearch->pResourceName,
						resGetOperationName(0, pRequestToSearch->operation));
				}

				resDestroyResourceRequest(pDeleted);
				eaRemove(pppRequests, iRemoveIdx);
				PERFINFO_AUTO_STOP();
				return false;
			}
		}		
		eaInsert(pppRequests,pRequestToSearch, idx);
		PERFINFO_AUTO_STOP();
		return true;
	}
}

static bool resPushRequestServer(	ResourceRequest ***pppRequests,
									ResourceRequest *pRequestToSearch)
{
	#define REQUESTS pppRequests[0]

	int idx;

	PERFINFO_AUTO_START_FUNC();
	
	idx = (int)eaBFind(REQUESTS, CompareResourceRequestNoOperation, pRequestToSearch);
	
	if(	stricmp(pRequestToSearch->pDictionaryName, GLOBALCOMMANDS_DICTIONARY) &&
		idx != eaSize(&REQUESTS) &&
		!CompareResourceRequestNoOperation(&pRequestToSearch, &REQUESTS[idx]))
	{
		ResourceRequest* pRequestLast;

		// A request already exists for this resource, so find the last request.

		for(idx++; idx != eaSize(&REQUESTS); idx++){
			if(CompareResourceRequestNoOperation(&pRequestToSearch, &REQUESTS[idx])){
				break;
			}
		}
		idx--;

		pRequestLast = REQUESTS[idx];

		if(pRequestToSearch->operation == pRequestLast->operation){
			// Last request is the same, so reject the current one.

			if(printResRequestCorrections)
			{
				printfColor(COLOR_BRIGHT|COLOR_RED|COLOR_GREEN,
							"Ignoring duplicate request for %s[%s] op %s.\n",
							pRequestToSearch->pDictionaryName,
							pRequestToSearch->pResourceName,
							resGetOperationName(0, pRequestToSearch->operation));
			}

			PERFINFO_AUTO_STOP();
			return false;
		}
	
		if(printResRequestCorrections)
		{
			printfColor(COLOR_BRIGHT|COLOR_GREEN,
						"Adding request for %s[%s] op %s (last op %s).\n",
						pRequestToSearch->pDictionaryName,
						pRequestToSearch->pResourceName,
						resGetOperationName(0, pRequestToSearch->operation),
						resGetOperationName(0, pRequestLast->operation));
		}

		// Nothing canceled the request, so insert after the last one for this resource.
		
		idx++;
	}

	eaInsert(pppRequests,pRequestToSearch, idx);

	PERFINFO_AUTO_STOP();
	return true;
	
	#undef REQUESTS
}

bool resClientAreTherePendingRequests(void)
{
	return (eaSize(&ppPendingResourceRequests) != 0);
}



void resClientCancelAnyPendingRequests(void)
{
	int i; 
	for (i=0; i < eaSize(&ppPendingResourceRequests); i++)
	{
		ResourceRequest *pRequest = ppPendingResourceRequests[i];
		if (gLogResourceRequests)
			filelog_printf("resourceSend.log","Cancelling Request %s %s %s", StaticDefineIntRevLookup(ResourceRequestTypeEnum,pRequest->operation), pRequest->pDictionaryName, pRequest->pResourceName);
		resDestroyResourceRequest(pRequest);
	}

	eaClear(&ppPendingResourceRequests);
}

void resClientSendRequestsToServer(Packet *pPacket)
{
	int i;
	int iSize = eaSize(&ppPendingResourceRequests);

	for (i=0; i < iSize; i++)
	{
		ResourceStatus *pStatus;
		ResourceRequest *pRequest = ppPendingResourceRequests[i];
		ResourceDictionary *pDictionary;

		assert(pRequest);
		assert(pRequest->pDictionaryName);
		assert(pRequest->pResourceName);

		pDictionary = resGetDictionary(pRequest->pDictionaryName);

		pStatus = resGetStatus(pDictionary, pRequest->pResourceName);

		if (pStatus)
		{
			if (pStatus->bResourcePresent && pRequest->operation == RESREQUEST_GET_RESOURCE)
			{
				if (gLogResourceRequests)
					filelog_printf("resourceSend.log","Canceling Get because already exists: %s %s %s", StaticDefineIntRevLookup(ResourceRequestTypeEnum,pRequest->operation), pRequest->pDictionaryName, pRequest->pResourceName);
				resDestroyResourceRequest(pRequest);
				continue;
			}
		}

		if (gLogResourceRequests)
			filelog_printf("resourceSend.log","Requesting %s %s %s", StaticDefineIntRevLookup(ResourceRequestTypeEnum,pRequest->operation), pRequest->pDictionaryName, pRequest->pResourceName);
		if (isDevelopmentMode())
		{
			++s_ResourceTraffic.iNumRequestSent;
		}

		pktSendBitsPack(pPacket,1,pRequest->operation);
		pktSendString(pPacket, pRequest->pDictionaryName);
		pktSendString(pPacket, pRequest->pResourceName);

		if (pRequest->operation == RESREQUEST_APPLY_RESOURCE_ACTIONS)
		{
			void * pResource;

			pResource = pRequest->pRequestData;
			if (pResource)
			{
				pktSendBits(pPacket,1,1);
				ParserSend(pRequest->pRequestParseTable, pPacket, NULL, pResource, SENDDIFF_FLAG_FORCEPACKALL, 0, 0, NULL);
			}
			else
			{
				pktSendBits(pPacket,1,0);
			}
		}

		resDestroyResourceRequest(pRequest);
	}

	eaClear(&ppPendingResourceRequests);

	pktSendBitsPack(pPacket,1,RESREQUEST_NONE);
}

bool gDisableNetworkRequests=false;
void resClientDisableAllRequests(void)
{
	gDisableNetworkRequests = true;
}

static void printSubscriptions(	ResourceCache* c,
								PerDictionaryResourceCache* pdrc)
{
	StashTableIterator	sti;
	StashElement		se;
	S32					found = 0;
	ResourceDictionary* d = resGetDictionary(pdrc->pDictName);

	stashGetIterator(pdrc->sTableResourceToVersion, &sti);
	
	while(stashGetNextElement(&sti, &se)){
		const char*						name = stashElementGetKey(se);
		U32								sentVersion = stashElementGetInt(se);
		const ReferentPrevious*			rp = resGetObjectPreviousFromDict(d, name);
		char							flagsString[100];
		
		flagsString[0] = 0;

		if(d->pPreContainerSendCB){
			const ClientResourceSentState* sentState = NULL;

			stashFindPointerConst(	pdrc->sTableResourceToSentState,
									name,
									&sentState);
		
			sprintf(flagsString,
					"/0x%"FORM_LL"x",
					sentState ? sentState->excludeFlags : DEFAULT_CONTAINER_EXCLUDE_FLAGS);
		}
		
		if(FALSE_THEN_SET(found)){
			printf(	"Version/Latest%s: Name    %s subscriptions for %d:%s:\n",
					d->pPreContainerSendCB ? "/Exclude Flags     " : "",
					pdrc->pDictName,
					c->userID,
					c->debugName);
		}

		if(rp){
			printfColor(COLOR_BRIGHT|(sentVersion == rp->version ? COLOR_GREEN : COLOR_RED),
						"  %5d/%6d%s: %s\n",
						sentVersion,
						rp->version,
						flagsString,
						name);
		}else{
			printfColor(COLOR_BRIGHT|(sentVersion ? COLOR_RED : COLOR_GREEN),
						"  %5d/%6s%s: %s\n",
						sentVersion,
						"none",
						flagsString,
						name);
		}
	}
}

AUTO_COMMAND;
void printDictionarySubscriptions(const char* nameSubString){
	EARRAY_CONST_FOREACH_BEGIN(ppAllResourceCaches, i, isize);
		ResourceCache*		c = ppAllResourceCaches[i];
		StashTableIterator	sti;
		StashElement		se;

		stashGetIterator(c->perDictionaryCaches, &sti);
		
		while(stashGetNextElement(&sti, &se)){
			PerDictionaryResourceCache* pdrc = stashElementGetPointer(se);
			
			if(strstri(pdrc->pDictName, nameSubString)){
				printSubscriptions(c, pdrc);
			}
		}
	EARRAY_FOREACH_END;
}

void resClientRequestSendReferentCommand(DictionaryHandleOrName dictHandle, int command, const char *pResourceName, void * pResource, const char* reason)
{
	ResourceDictionary *pDictionary = resGetDictionary(dictHandle);
	if (gDisableNetworkRequests)
	{
		return;
	}

	VERBOSE_LOG_DICT(dictHandle, "Requesting resource %s because: %s", pResourceName, reason);

	if(	printSubscriptionChanges &&
		pDictionary && 
		pDictionary->bIsCopyDictionary)
	{
		printfColor(COLOR_BRIGHT|COLOR_GREEN,
					"Requesting op %d: %s[%s].\n",
					command,
					pDictionary->pDictName,
					pResourceName);
	}
	
	if (pDictionary || stricmp(dictHandle, GLOBALCOMMANDS_DICTIONARY) == 0)
	{	
		ResourceRequest *pRequest;
		if (pDictionary)
		{
			pRequest = resCreateResourceRequest(command, pDictionary->pDictName, pResourceName, 0, pDictionary->pDictTable);
		}
		else
		{
			pRequest = resCreateResourceRequest(command, GLOBALCOMMANDS_DICTIONARY, pResourceName, 0, NULL);
		}
		if (command == RESREQUEST_APPLY_RESOURCE_ACTIONS)
		{
			pRequest->pRequestParseTable = parse_ResourceActionList;
		}
//		filelog_printf("referenceReq.log", "Requested command %d for %s %s", command, pRequest->pDictionaryName, pResourceName);
		
		if (resPushRequestClient(&ppPendingResourceRequests, pRequest))
		{
			if (gLogResourceRequests)
				filelog_printf("resourceSend.log","Queuing Request: %s %s %s", StaticDefineIntRevLookup(ResourceRequestTypeEnum,pRequest->operation), pRequest->pDictionaryName, pRequest->pResourceName);
			if (pResource)
			{
				pRequest->pRequestData = StructCreateVoid(pRequest->pRequestParseTable);
				StructCopyFieldsVoid(pRequest->pRequestParseTable,pResource,pRequest->pRequestData,0,0);
			}
		}
		else
		{
			if (gLogResourceRequests)
				filelog_printf("resourceSend.log","Failed to Queue Request: %s %s %s", StaticDefineIntRevLookup(ResourceRequestTypeEnum,pRequest->operation), pRequest->pDictionaryName, pRequest->pResourceName);
			resDestroyResourceRequest(pRequest);
			return;
		}
	}
}





//returns true on success
bool resClientProcessServerUpdates(Packet *pPacket)
{
	char pDictName[RESOURCE_DICT_NAME_MAX_SIZE];
	char *pResourceName = 0;
	ResourceStatus *pResourceStatus;
	ResourceDictionary *pDictionary;

	int command;

	char pchDebugStr[1024];

	// Debug timing packet processing
	//long iStart = timerCpuTicks();
	//int iCount = 0;

	estrStackCreate(&pResourceName);

	while (command = pktGetBitsPack(pPacket,1))
	{
		int pktStart = pktGetIndex(pPacket);

		pktGetString(pPacket,SAFESTR(pDictName));
		estrCopyFromPacket(&pResourceName,pPacket);

		pDictionary = resGetDictionary(pDictName);		


		if(SHOULD_PRINT_CONTAINER_RECEIVE(pDictionary)){
			printfColor(COLOR_BRIGHT|COLOR_GREEN,
						"Receiving op %d: %s[%s].\n",
						command,
						pDictName,
						pResourceName);
		}

		pResourceStatus = resGetStatus(pDictionary, pResourceName);
		
		VERBOSE_LOG_DICT_WITH_STRUCT(pDictName, parse_ResourceStatus, pResourceStatus, "Resource Status");

		switch (command)
		{		
		xcase RESUPDATE_UNLOCKED:
		
			if (!pDictionary)
			{
				Errorf("Invalid dictionary name %s sent from server", pDictName);
				break;
			}
			if (pResourceStatus)
			{
			
		
				pResourceStatus->iLockOwner = 0; // Cancel lock so client has to re-request it
			}

			
			VERBOSE_LOG_DICT(pDictName, "%s", pResourceStatus ? "STATUS EXISTED" : "STATUS DID NOT EXIST");
			


			resDictRunEventCallbacks(pDictionary, RESEVENT_RESOURCE_UNLOCKED, pResourceName, resGetObjectFromDict(pDictionary, pResourceName));

		xcase RESUPDATE_LOCKED:
			if (!pDictionary)
			{
				Errorf("Invalid dictionary name %s sent from server", pDictName);
				break;
			}

			
			VERBOSE_LOG_DICT(pDictName, "%s", pResourceStatus ? "STATUS EXISTED" : "STATUS DID NOT EXIST");
			
		
			if (!pResourceStatus)
			{
				pResourceStatus = resGetOrCreateStatus(pDictionary, pResourceName);
			}
			pResourceStatus->iLockOwner = 1; // Cancel lock so client has to re-request it
			resDictRunEventCallbacks(pDictionary, RESEVENT_RESOURCE_LOCKED, pResourceName, resGetObjectFromDict(pDictionary, pResourceName));

		xcase RESUPDATE_FORCE_UPDATE:
		case RESUPDATE_NEW_RESOURCE:
		case RESUPDATE_MODIFIED_RESOURCE:
		case RESUPDATE_DESTROYED_RESOURCE:


			if (!pDictionary || 
				!pDictionary->bShouldRequestMissingData ||
				!pDictionary->pDictTable)
			{
				if (gLogResourceRequests)
					filelog_printf("resourceSend.log","Rejecting Update: %s %s %s", StaticDefineIntRevLookup(ResourceUpdateTypeEnum,command), pDictName, pResourceName);

				
				VERBOSE_LOG_DICT(pDictName, "Rejecting due to no dictionary, or something");
				
				

				estrDestroy(&pResourceName);
				devassertmsg(0, "Unable to unpack Resource Update message from server.  Client is in bad state now.");
				break;
			}
			
			if (pktGetBits(pPacket, 1))
			{
				void*	pResource = NULL;
				void*	pResourceCurrent = NULL;
				U32		pktSizeStart = 0;
				bool	bGotDiff = false;
				bool	bIgnoreDiff = false;
				bool	bIsEditCopy;
				
				// Get here if update has data
				
				bIsEditCopy = pktGetBits(pPacket, 1); // Find out if server thinks it sent an edit copy

				
				VERBOSE_LOG_DICT(pDictName, "Is edit copy: %d. Has resource status: %d",
					bIsEditCopy, !!pResourceStatus);
				

				if (!pResourceStatus)
				{
					pResourceStatus = resGetOrCreateStatus(pDictionary, pResourceName);
				}

				if(pktGetBits(pPacket, 1)){
					// Get here if a DIFF is received instead of a full object
					
					VERBOSE_LOG_DICT(pDictName,  "Got a DIFF instead of a full object");
								

					bGotDiff = true;
					pResourceCurrent = resGetObjectFromDict(pDictionary, pResourceName);
					
					bIgnoreDiff = !pResourceCurrent;
					pResource = NULL;

					VERBOSE_LOG_DICT_WITH_STRUCT(pDictName, pDictionary->pDictTable, pResourceCurrent, "Got ResourceCurrent");

					//assertmsgf(	pResourceCurrent,
					//			"Receiving diff of container %s[%s] but there's no previous copy.",
					//			pDictionary->pDictName,
					//			pResourceName);
				}

				if(!bIgnoreDiff)
				{
					
					VERBOSE_LOG_DICT(pDictName, "Creating pResource");
					

					pResource = StructCreateWithComment(pDictionary->pDictTable, "Receiving into resource dictionary");
				
					if(pResourceCurrent){
						StructCopyFieldsVoid(pDictionary->pDictTable, pResourceCurrent, pResource, 0, 0);
					}
				}

				if(SHOULD_PRINT_CONTAINER_RECEIVE(pDictionary)){
					pktSizeStart = pktGetIndex(pPacket);
				}

				//////////////////////////////////////////////////////////////////////////
				// This is a hack inserted for debugging only. 
				// This should be removed eventually
				sprintf(pchDebugStr, 
					"command : %d, "
					"pDictName : %s, "
					"pResourceName : %s, "
					"bIsEditCopy : %d, "
					"bGotDiff : %d, "
					"bIgnoreDiff : %d, "
					"pResource : %p, "
					"pResourceCurrent : %p, "
					"pktSizeStart : %d",
					command,
					pDictName,
					pResourceName,
					bIsEditCopy,
					bGotDiff,
					bIgnoreDiff,
					pResource,
					pResourceCurrent,
					pktSizeStart
					);
				g_pchCommentString = pchDebugStr;


				
				VERBOSE_LOG_DICT(pDictName, "About to do parserRecv");
				

				ParserRecv(	pDictionary->pDictTable,
							pPacket,
							bIgnoreDiff ? NULL : pResource,
							0);

				VERBOSE_LOG_DICT_WITH_STRUCT(pDictName, pDictionary->pDictTable, pResource, "Received Resource");

				g_pchCommentString = NULL;

				if(SHOULD_PRINT_CONTAINER_RECEIVE(pDictionary)){
					printfColor(COLOR_BRIGHT|COLOR_GREEN,
								"%d bytes.\n",
								pktGetIndex(pPacket) - pktSizeStart);
				}

				if(pResourceCurrent){
					if(SHOULD_PRINT_CONTAINER_RECEIVE(pDictionary)){
						static char* s;
						
						estrClear(&s);
						StructWriteTextDiff(&s,
											pDictionary->pDictTable,
											pResourceCurrent,
											pResource,
											NULL,
											0,
											0,
											0);

						if(SAFE_DEREF(s)){
							printfColor(COLOR_BRIGHT|COLOR_GREEN,
										"Diff:\n%s\n",
										s);
						}else{
							printfColor(COLOR_BRIGHT|COLOR_GREEN,
										"No differences.\n");
						}
					}
				}else{
					if(	SHOULD_PRINT_CONTAINER_RECEIVE(pDictionary) &&
						printContainerReceives & 2)
					{
						static char* s;
						
						estrClear(&s);
						ParserWriteText(&s,
										pDictionary->pDictTable,
										pResource,
										0,
										0,
										0);

						if(SAFE_DEREF(s)){
							printfColor(COLOR_BRIGHT|COLOR_GREEN,
										"Full:\n%s\n",
										s);
						}else{
							printfColor(COLOR_BRIGHT|COLOR_GREEN,
										"No differences.\n");
						}
					}
				}

				if (bIgnoreDiff){
					// If we get a modify when we don't have the object in the first place
					// ignore the modify
					if (gLogResourceRequests)
						filelog_printf("resourceSend.log","Ignoring diff modify on object that is not in dictionary: %s %s", pDictName, pResourceName );
					
					
					VERBOSE_LOG_DICT(pDictName, "IgnoreDiff");
					
					
					StructDestroySafeVoid(pDictionary->pDictTable, &pResource);
					break;
				}
				if(	!pResourceStatus->bResourceRequested &&
					(	command == RESUPDATE_MODIFIED_RESOURCE ||
						command == RESUPDATE_DESTROYED_RESOURCE ) &&
					!pDictionary->bDataEditingMode)
				{
					
					VERBOSE_LOG_DICT(pDictName, "Ignoring modify on object that was not requested");
					

					// If we get an update we didn't ask for, ignore it or else the client and server get out of sync
					if (gLogResourceRequests)
						filelog_printf("resourceSend.log","Ignoring modify on object that was not requested: %s %s", pDictName, pResourceName );
					StructDestroySafeVoid(pDictionary->pDictTable, &pResource);
					break;
				}
				if (!resShouldRequestMissingResources(pDictionary))
				{
					
					VERBOSE_LOG_DICT(pDictName,  "Ignoring modify on object, dict not in edit mode");
					
					// If we get an update and we're not allowed to get the resource, ignore it
					if (gLogResourceRequests)
						filelog_printf("resourceSend.log","Ignoring modify on object when dictionary not in edit mode: %s %s", pDictName, pResourceName );
					StructDestroySafeVoid(pDictionary->pDictTable, &pResource);
					break;
				}
				if (pDictionary->bDataEditingMode && !bIsEditCopy)
				{
					
					VERBOSE_LOG_DICT(pDictName,  "Ignoring modify on object when client dictionary is in edit mode but server thinks it it not in edit mode");
					
					
					// If we get a resource copy that is not in editing mode and the dictionary
					// thinks it should be in editing mode, ignore the receive
					Errorf("Received %s '%s' from server in wrong mode.  Server sent it as non-editing and client thinks it should be editing.  If you see this, get a programmer to find out what went wrong!", pDictName, pResourceName);
					if (gLogResourceRequests)
						filelog_printf("resourceSend.log","Ignoring modify on object when client dictionary is in edit mode but server thinks it it not in edit mode: %s %s", pDictName, pResourceName );
					StructDestroySafeVoid(pDictionary->pDictTable, &pResource);
					break;
				}

				
				VERBOSE_LOG_DICT(pDictName,  "Acually updating object");
				


				resUpdateObject(pDictionary, pResourceStatus, pResource);

				pResourceStatus->bIsEditCopy = pDictionary->bDataEditingMode;
				pResourceStatus->bResourceRequested = true;
			}
			else
			{
				
				VERBOSE_LOG_DICT(pDictName,  "update had no data, it's a delete");
				

				// Get here if update has no data, which means it's a delete
				if (pResourceStatus)
				{						
					resUpdateObject(pDictionary, pResourceStatus, NULL);
				}
			}				
		xcase RESUPDATE_INDEX_UPDATE:
			{
				ResourceInfo *objectInfo;
				if (!(pDictionary && pDictionary->bShouldRequestMissingData
					&& pDictionary->pDictTable))
				{
					Errorf("Invalid dictionary for fulfilling missing-data-request... possible server-to-packet packet corruption");
					estrDestroy(&pResourceName);
					devassertmsg(0, "Unable to unpack Index message from server.  Client is in bad state now.");

					
					VERBOSE_LOG_DICT(pDictName,  "invalid dictionary");
					

					break;
				}

				if (pktGetBits(pPacket, 1))
				{
					
					VERBOSE_LOG_DICT(pDictName,  "Creating object info");
					
						
					objectInfo = resGetOrCreateInfo(pDictionary->pDictInfo, pResourceName);

					ParserRecv(parse_ResourceInfo, pPacket, objectInfo, 0);

					if (areEditorsAllowed())
					{					
						int i;
						for (i = 0; i < eaSize(&objectInfo->ppReferences); i++)
						{
							resDictDependOnOtherDict(pDictionary, objectInfo->ppReferences[i]->resourceDict, objectInfo->ppReferences[i]->resourceName);
						}
					}
				}
				else
				{
					
					VERBOSE_LOG_DICT(pDictName,  "Removing info");
					

					resRemoveInfo(pDictionary->pDictInfo, pResourceName);
				}
				resDictRunEventCallbacks(pDictionary, RESEVENT_INDEX_MODIFIED, pResourceName, NULL);
			}
		xcase RESUPDATE_INDEX_SUBSCRIBED:
			{
				if (!(pDictionary && pDictionary->bShouldRequestMissingData
					&& pDictionary->pDictTable))
				{
					
					VERBOSE_LOG_DICT(pDictName,  "invalid dict");
					
					Errorf("Invalid dictionary for fulfilling missing-data-request... possible server-to-packet packet corruption");
					estrDestroy(&pResourceName);
					devassertmsg(0, "Unable to unpack Index Subscribe message from server.  Client is in bad state now.");
					break;
				}
				if (stricmp(pResourceName,"On") == 0)
				{
					
					VERBOSE_LOG_DICT(pDictName,  "Setting to ON");
					

					pDictionary->bReceivedAllInfos = 1;
					if (areEditorsAllowed())
					{
						resValidateCheckAllRequestedReferences();
					}
				}
				else
				{
					
					VERBOSE_LOG_DICT(pDictName,  "Setting to OFF");
					
					pDictionary->bReceivedAllInfos = 0;
				}
			}
		xcase RESUPDATE_ERROR:
			{
				ErrorMessage *errMsg;

				if (pktGetBits(pPacket, 1))
				{
					errMsg = StructCreate(parse_ErrorMessage);
					ParserRecv(parse_ErrorMessage, pPacket, errMsg, 0);
					ErrorfCallCallback(errMsg);
					StructDestroy(parse_ErrorMessage, errMsg);	
				}
			}
		xcase RESUPDATE_NAMESPACE_UPDATE:
			{
				ResourceNameSpace *pNameSpace;
				if (pktGetBits(pPacket, 1))
				{
					
					VERBOSE_LOG_DICT(pDictName,  "creating namespace");
					

					pNameSpace = resNameSpaceGetOrCreate(pResourceName);

					ParserRecv(parse_ResourceNameSpace, pPacket, pNameSpace, 0);
				}
				else
				{
					
					VERBOSE_LOG_DICT(pDictName,  "Removing namespace");
					
					resNameSpaceRemove(pResourceName);
				}
			}
		xcase RESUPDATE_DISPLAY_RESOURCE_STATUS:
			{
				ResourceActionList *pHolder;
				if (pktGetBits(pPacket, 1))
				{
					pHolder = StructCreate(parse_ResourceActionList);

					ParserRecv(parse_ResourceActionList, pPacket, pHolder, 0);

					if (gDisplayStatusCB)
					{
						gDisplayStatusCB(pHolder);
					}

					StructDestroy(parse_ResourceActionList, pHolder);
				}
				else
				{
					// Display failure somehow
				}
			}

		xcase RESUPDATE_HANDLE_COMPLETED_ACTIONS:
			{
				if (pktGetBits(pPacket, 1))
				{
					ResourceActionList *pHolder;
					pHolder = StructCreate(parse_ResourceActionList);

					ParserRecv(parse_ResourceActionList, pPacket, pHolder, 0);
							
					VERBOSE_LOG_DICT_WITH_STRUCT(pDictName, parse_ResourceActionList, pHolder, "Action List");

					if (gHandleActionListCB)
					{
						gHandleActionListCB(pHolder);
					}

					StructDestroy(parse_ResourceActionList, pHolder);
				}
				else
				{
					// Display failure somehow
				}
			}

		xdefault:
			Errorf("Invalid Reference command sent from server!");
			devassertmsg(0, "Unable to unpack Unknown message from server.  Client is in bad state now.");
		}
		if (gLogResourceRequests)
			filelog_printf("resourceSend.log","Handled Update: %s %s %s, %d bytes", StaticDefineIntRevLookup(ResourceUpdateTypeEnum,command), pDictName, pResourceName, pktGetIndex(pPacket) - pktStart );
		if (isDevelopmentMode()) {
			++s_ResourceTraffic.iNumResourceReceived;
			s_ResourceTraffic.iNumResourceBytesReceived += (pktGetIndex(pPacket) - pktStart);
		}
	}

	estrDestroy(&pResourceName);

	// Debug print for timing and count
	//printf("Reference data from server (%d in %g secs)\n", iCount, timerSeconds(timerCpuTicks() - iStart));

	return true;
}

// Server-side functions

static void destroyClientResourceSentState(ClientResourceSentState* sentState){
	free(sentState);
}

void resDictResetRequestData(DictionaryHandleOrName dictHandle)
{
	ResourceDictionary *pDictionary = resGetDictionary(dictHandle);
	int i;

	resDestroyAllStatus(pDictionary);

	for (i=0; i < eaSize(&ppAllResourceCaches); i++)
	{			
		PerDictionaryResourceCache *pCache;

		pCache = resGetPerDictionaryCache(ppAllResourceCaches[i], pDictionary->pDictName);
		if(pCache){
			stashTableClear(pCache->sTableResourceToVersion);
			stashTableClearEx(	pCache->sTableResourceToSentState,
								NULL,
								destroyClientResourceSentState);
		}
	}

}

bool resServerAreTherePendingUpdates(ResourceCache *pCache)
{
	if (pCache && eaSize(&pCache->ppPendingResourceUpdates))
	{
		return true;
	}
	return false;
}

static unsigned int sMaxResourcePacket; // Maximum number of resource sends per frame

AUTO_CMD_INT(sMaxResourcePacket, MaxResourcePacket) ACMD_COMMANDLINE;

AUTO_RUN;
void setMaxResourceSends(void)
{
	if (isDevelopmentMode())
	{
		sMaxResourcePacket = 4000000;
	}
	else
	{
		sMaxResourcePacket = 1000000;
	}
}

void resServerSendUpdatesToClient(Packet *pOutPacket, ResourceCache *pCache, void *pCallbackData, int langID, bool bDestroyRequests)
{
	char *pResourceName;
	int i;
	int oldSize = pktGetSize(pOutPacket);

	//printf("\nSending resServerSendUpdatesToClient\n");
	PERFINFO_AUTO_START_FUNC();

	//then, send down all the referents that had send requests that came from the server
	for (i=0; i < eaSize(&pCache->ppPendingResourceUpdates); i++)
	{
		ResourceDictionary *pDictionary;
		ResourceRequest *pRequest = pCache->ppPendingResourceUpdates[i];
		void * pResource = NULL;
		const ReferentPrevious* rp = NULL;
		ClientResourceSentState* sentState = NULL;
		bool bSendData = 0;
		bool bCancelSend = 0;
		bool bSendDiffFlag = 0;
		bool bSendEditModeFlag = 0;
		StructTypeField excludeFlags = DEFAULT_CONTAINER_EXCLUDE_FLAGS;
		StructTypeField includeFlags = 0; // Normally zero, but app servers can set to LOGIN_SUBSCRIBE
		PerDictionaryResourceCache *pDictCache = NULL;

		pDictionary = resGetDictionary(pRequest->pDictionaryName);
		pResourceName = pRequest->pResourceName;

		VERBOSE_LOG_DICT_WITH_STRUCT(pRequest->pDictionaryName, parse_ResourceRequest, pRequest, "request %d in ResServerSendUpdatesToClient, cache debug name %s", i,
			pCache->debugName);

		if(	pRequest->operation == RESUPDATE_NEW_RESOURCE ||
			pRequest->operation == RESUPDATE_FORCE_UPDATE ||
			pRequest->operation == RESUPDATE_MODIFIED_RESOURCE)
		{
			pResource = resGetObjectFromDict(pDictionary, pResourceName);
			
			VERBOSE_LOG_DICT_WITH_STRUCT(pRequest->pDictionaryName, pDictionary->pDictTable, pResource, "resGetObjectFromDict");

			bSendData = 1;
			bSendDiffFlag = 1;
			bSendEditModeFlag = 1;
			
			if(pDictionary->bIsCopyDictionary)
			{
				if(!pResource)
				{
					VERBOSE_LOG_DICT(pRequest->pDictionaryName, "Sending absent resource");

					if(printContainerSends){
						printfColor(COLOR_BRIGHT|COLOR_GREEN,
									"%4d: User %d:%s: Sending absent resource op %d: %s[%s].\n",
									GetCurrentThreadId(),
									pCache->userID,
									pCache->debugName,
									pRequest->operation,
									pRequest->pDictionaryName,
									pResourceName);
					}
				}
				else
				{
					pDictCache = resGetPerDictionaryCache(pCache, pDictionary->pDictName);

					if(pDictCache){
						U32 versionSent;
						
						if(pDictionary->pPreContainerSendCB){
							stashFindPointer(	pDictCache->sTableResourceToSentState,
												pResourceName,
												&sentState);
						}

						if(stashFindInt(pDictCache->sTableResourceToVersion, pResourceName, &versionSent)){
							U32 versionCur = 0;
							S32 versionWillIncrement;

							rp = resGetObjectPreviousFromDict(pDictionary, pResourceName);
							

							if(rp){
								versionCur = rp->version;
							}
							
							versionWillIncrement =	!rp ||
													rp->referentPrevious;
							
							resQueueCopyObjectToPrevious(pDictionary, pResourceName);

							if(	versionSent == versionCur &&
								versionWillIncrement)
							{
								// Send the diff and update the cached version.
								
								VERBOSE_LOG_DICT(pRequest->pDictionaryName, "Send the diff and update the cached version");


								if(printContainerSends){
									printfColor(COLOR_BRIGHT|COLOR_GREEN,
												"%4d: User %d:%s: Sending %s resource op %d v%d to v%d: %s[%s].\n",
												GetCurrentThreadId(),
												pCache->userID,
												pCache->debugName,
												rp ? "updated" : "unsent",
												pRequest->operation,
												versionSent,
												versionCur + 1,
												pRequest->pDictionaryName,
												pResourceName);
								}
							}
							else if(versionSent == versionCur + versionWillIncrement){
								// Already sent the diff, skip the whole send.
								
								VERBOSE_LOG_DICT(pRequest->pDictionaryName, "Already sent the diff, skip the whole send");


								versionWillIncrement = 0;

								bCancelSend = 1;

								if(printContainerSends){
									printfColor(COLOR_BRIGHT|COLOR_GREEN|COLOR_RED,
												"%4d: User %d:%s: Skipping sending resource op %d both v%d: %s[%s].\n",
												GetCurrentThreadId(),
												pCache->userID,
												pCache->debugName,
												pRequest->operation,
												versionSent,
												pRequest->pDictionaryName,
												pResourceName);
								}
							}else{
								// Too old, do a full send.
								
								VERBOSE_LOG_DICT(pRequest->pDictionaryName, "Too old, doing a ful send");

								assert(versionSent < versionCur);

								if(printContainerSends){
									printfColor(COLOR_BRIGHT|COLOR_GREEN,
												"%4d: User %d:%s: Sending full resource op %d v%d to v%d (%s): %s[%s].\n",
												GetCurrentThreadId(),
												pCache->userID,
												pCache->debugName,
												pRequest->operation,
												versionSent,
												versionCur + versionWillIncrement,
												rp ? "updated" : "unsent",
												pRequest->pDictionaryName,
												pResourceName);
								}

								rp = NULL;
							}

							if(versionWillIncrement){
								stashAddInt(pDictCache->sTableResourceToVersion,
											pResourceName,
											versionCur + 1,
											true);
							}
							else if(versionSent != versionCur){
								stashAddInt(pDictCache->sTableResourceToVersion,
											pResourceName,
											versionCur,
											true);
							}
						}else{
							// Not in cache, so just do a full send I guess.

							VERBOSE_LOG_DICT(pRequest->pDictionaryName, "Not in cache, so just do a full send I guess");


							rp = NULL;

							if(printContainerSends){
								printfColor(COLOR_BRIGHT|COLOR_GREEN,
											"%4d: User %d:%s Sending full resource op %d: %s[%s].\n",
											GetCurrentThreadId(),
											pCache->userID,
											pCache->debugName,
											pRequest->operation,
											pRequest->pDictionaryName,
											pResourceName);
							}
						}
					}
				}
			}

			if(pResource)
			{
				// Data editing mode on the dictionary
				if (pDictionary->bDataEditingMode)
				{
					// If user is in edit mode, send all flags
					excludeFlags = 0;
				}
				else if (pDictionary->pPreContainerSendCB)
				{
					// If an pre-container-send callback is defined, call it to update exclude flags
					// and to discover if a full send is required

					pDictionary->pPreContainerSendCB(	pDictionary->pDictName,
														pResourceName,
														pResource,
														pCallbackData,
														&excludeFlags,
														&includeFlags);
				}

				if(	printContainerSends &&
					pDictionary->bIsCopyDictionary)
				{
					printfColor(COLOR_BRIGHT|COLOR_GREEN,
								"%4d: Using exclude flags 0x%"FORM_LL"x.\n",
								GetCurrentThreadId(),
								excludeFlags);
				}
				
				if(	sentState && 
					excludeFlags != sentState->excludeFlags
					||
					!sentState &&
					pDictionary->pPreContainerSendCB &&
					pDictionary->bIsCopyDictionary &&
					excludeFlags != DEFAULT_CONTAINER_EXCLUDE_FLAGS)
				{
					// Force a full send because the send flags changed.

					VERBOSE_LOG_DICT(pRequest->pDictionaryName, "Force a full send because the send flags changed.");

					rp = NULL;

					if(printContainerSends){
						printfColor(COLOR_BRIGHT|COLOR_GREEN,
									"%4d: Forcing full send, exclude flags 0x%"FORM_LL"x (was %s 0x%"FORM_LL"x).\n",
									GetCurrentThreadId(),
									excludeFlags,
									sentState ? "sent" : "default",
									sentState ? sentState->excludeFlags : DEFAULT_CONTAINER_EXCLUDE_FLAGS);
					}
				}
				
				if(	pDictionary->pPreContainerSendCB &&
					pDictionary->bIsCopyDictionary)
				{
					// Update the exclude flags.

					if(excludeFlags != DEFAULT_CONTAINER_EXCLUDE_FLAGS){
						if(!sentState){
							if(!pDictCache){
								pDictCache = resGetPerDictionaryCache(pCache, pDictionary->pDictName);
							}
							
							if(pDictCache){
								sentState = callocStruct(ClientResourceSentState);
									
								if(!pDictCache->sTableResourceToSentState){
									pDictCache->sTableResourceToSentState = stashTableCreateWithStringKeys(100, StashDefault);
								}
								
								if(!stashAddPointer(pDictCache->sTableResourceToSentState,
													pResourceName,
													sentState,
													false))
								{
									assert(0);
								}
							}
						}

						if(sentState){
							sentState->excludeFlags = excludeFlags;
						}
					}
					else if(sentState){
						// Remove the sent state because exclude flags are default.
						
						ClientResourceSentState* sentStateCheck;

						assert(pDictCache);
						
						if(	!stashRemovePointer(pDictCache->sTableResourceToSentState,
												pResourceName,
												&sentStateCheck) ||
							sentState != sentStateCheck)
						{
							assert(0);
						}
						
						SAFE_FREE(sentState);
					}
				}
			} else {
				// Get here if resource doesn't exist for some reason
				if (IsGameServerBasedType() && !pDictionary->bIsCopyDictionary && isDevelopmentMode() && !resHasNamespace(pResourceName) && !g_DisableNonExistantResError && (stricmp("InteractionDictionary",pDictionary->pDictName) != 0)) {
					Errorf("Client requested non-existent %s '%s'.  If you see this, there should be another error from validation somewhere, or else someone is missing the validation to detect the source of this problem.", pDictionary->pDictName, pResourceName);
				}
			}
		}
		else if(pRequest->operation == RESUPDATE_DESTROYED_RESOURCE)
		{
			bSendData = 1;

			if(pDictionary->bIsCopyDictionary)
			{
				pDictCache = resGetPerDictionaryCache(pCache, pDictionary->pDictName);

				if(pDictCache)
				{
					StashElement se;
					
					if(printContainerSends){
						printfColor(COLOR_BRIGHT|COLOR_GREEN,
									"%4d: User %d:%s Sending destroy resource op %d: %s[%s].\n",
									GetCurrentThreadId(),
									pCache->userID,
									pCache->debugName,
									pRequest->operation,
									pRequest->pDictionaryName,
									pResourceName);
					}

					if(stashFindElement(pDictCache->sTableResourceToVersion, pResourceName, &se)){
						// Set the version back to zero.

						stashElementSetInt(se, 0);
					}

					if(	pDictCache->sTableResourceToSentState &&
						stashRemovePointer(	pDictCache->sTableResourceToSentState,
											pResourceName,
											&sentState))
					{
						SAFE_FREE(sentState);
					}
				}
			}
		}
		else if(pRequest->operation == RESUPDATE_INDEX_UPDATE ||
				pRequest->operation == RESUPDATE_NAMESPACE_UPDATE ||
				pRequest->operation == RESUPDATE_DISPLAY_RESOURCE_STATUS ||
				pRequest->operation == RESUPDATE_ERROR || 
				pRequest->operation == RESUPDATE_HANDLE_COMPLETED_ACTIONS)
		{
			bSendData = 1;
		}
		else if(pRequest->operation == RESUPDATE_FENCE_INSTRUCTION)
		{
			((resServerFenceCallback*)pRequest->pRequestData)(pRequest->uFenceID, pRequest->pRequestData2);
			bCancelSend = true;
		}

		if (pRequest->pRequestData)
		{
			pResource = pRequest->pRequestData;
		}

		if(!bCancelSend){
			pktSendBitsPack(pOutPacket,1,pRequest->operation);
			pktSendString(pOutPacket, pRequest->pDictionaryName);
			pktSendString(pOutPacket, pResourceName);
			
			if (bSendData)
			{
				if (!pResource)
				{
					pktSendBits(pOutPacket, 1, 0);
				}
				else
				{
					pktSendBits(pOutPacket, 1, 1);

					if (bSendEditModeFlag)
					{
						pktSendBits(pOutPacket, 1, (!pDictionary || !pDictionary->bDataEditingMode ? 0 : 1)); // Write if server thinks resource send it in edit mode
					}
					
					if (pDictionary &&
						!pDictionary->bDataEditingMode &&
						pRequest->pRequestParseTable == parse_Message) 
					{
						// Messages sent to the client are translated first, and then sent
						TranslatedMessage *pTrans = langFindTranslatedMessage(langID, (Message*)pResource);
						Message msg = {0};
						msg.pcMessageKey = ((Message*)pResource)->pcMessageKey;
						if (pTrans) {
							// found translation so use it
							msg.pcDefaultString = pTrans->pcTranslatedString;
							msg.bFailedLocalTranslation = false;
						} else {
							// No translation found, so check alternate language
							int altLang = locGetAlternateLanguageFromLang(langID);
							if (altLang == LANGUAGE_NONE) {
								// Alternate is empty string
								msg.pcDefaultString = "";
								msg.bFailedLocalTranslation = true;
							} else if (altLang == LANGUAGE_DEFAULT) {
								// Alternate is default string
								msg.pcDefaultString = ((Message*)pResource)->pcDefaultString;
								msg.bFailedLocalTranslation = true;
							} else {
								// Try alternate language
								pTrans = langFindTranslatedMessage(altLang, (Message*)pResource);
								if (pTrans) {
									// Found alternate, so use it
									msg.pcDefaultString = pTrans->pcTranslatedString;
									msg.bFailedLocalTranslation = false;
								} else {
									// Alternate missing, so use default
									msg.pcDefaultString = ((Message*)pResource)->pcDefaultString;
									msg.bFailedLocalTranslation = true;
								}
							}
						}
						msgSetLocallyTranslated(&msg, true);

						if( msg.bFailedLocalTranslation && isDevelopmentMode() && errorOnSendMissingMessageTranslations )
						{
							char tempFileName[CRYPTIC_MAX_PATH];
							sprintf(tempFileName, "defs/ServerMessages.%s.translation", locGetName(locGetIDByLanguage(langID)));
							ErrorFilenamef(tempFileName, "Failed to find translation in lang=%s  for message key=%s   default=%s", locGetName(locGetIDByLanguage(langID)), msg.pcMessageKey, msg.pcDefaultString);
						}

						if(bSendDiffFlag){
							pktSendBits(pOutPacket, 1, 0);
						}

						ParserSend(pRequest->pRequestParseTable, pOutPacket, NULL, &msg, 0, 0, excludeFlags, NULL);
					}
					else
					{
						void* pResourcePrevious = SAFE_MEMBER(rp, referentPrevious);
						
						// Normal resources are simply sent

						if(bSendDiffFlag){
							pktSendBits(pOutPacket, 1, !!pResourcePrevious);
						}

						if(pResourcePrevious){
							PERFINFO_AUTO_START("send diff", 1);
						}else{
							PERFINFO_AUTO_START("send full", 1);
						}

						
						VERBOSE_LOG_DICT_WITH_STRUCT(pRequest->pDictionaryName, pRequest->pRequestParseTable,
							pResourcePrevious, "Doing the actual send, here is the previous version");
						
						ParserSend(	pRequest->pRequestParseTable,
									pOutPacket,
									pResourcePrevious,
									pResource,
									0,
									includeFlags,
									excludeFlags,
									NULL);
						
						PERFINFO_AUTO_STOP();
					}
				}
			}

			if (gLogResourceRequests)
				filelog_printf("resourceSend.log","Sent Update: %s %s %s", StaticDefineIntRevLookup(ResourceUpdateTypeEnum,pRequest->operation), pRequest->pDictionaryName, pRequest->pResourceName);
			if (isDevelopmentMode()) {
				++s_ResourceTraffic.iNumResourceSent;
				s_ResourceTraffic.iNumResourceBytesSent += (pktGetSize(pOutPacket) - oldSize);
				oldSize = pktGetSize(pOutPacket);
			}
			//printf("Size: %d, %s %s\n", pktGetSize(pOutPacket) - oldSize, pRequest->pDictionaryName, pResourceName);
		}

		if(bDestroyRequests)
		{
			resDestroyResourceRequest(pRequest);
		}
		else
		{
			eaPush(&pCache->ppSentResourceUpdates, pRequest);
		}	

		if (pktGetSize(pOutPacket) > sMaxResourcePacket)
		{
			i++;
			break;
		}
	}

	eaRemoveRange(&pCache->ppPendingResourceUpdates, 0, i);
	pktSendBitsPack(pOutPacket,1,RESREQUEST_NONE);

	PERFINFO_AUTO_STOP_FUNC();
}

void resServerDestroySentUpdates(ResourceCache *pCache)
{
	if(!eaSize(&pCache->ppSentResourceUpdates))
	{
		eaDestroy(&pCache->ppSentResourceUpdates);
	}
	else
	{
		PERFINFO_AUTO_START_FUNC();
		
		EARRAY_CONST_FOREACH_BEGIN(pCache->ppSentResourceUpdates, i, isize);
			resDestroyResourceRequest(pCache->ppSentResourceUpdates[i]);
		EARRAY_FOREACH_END;

		eaSetSize(&pCache->ppSentResourceUpdates, 0);
		
		PERFINFO_AUTO_STOP();
	}
}

ResourceCache *resServerCreateResourceCache(U32 userID)
{
	ResourceCache *newCache = calloc(1,sizeof(ResourceCache));
	newCache->userID = userID;
	newCache->perDictionaryCaches = stashTableCreateAddress(32);

	eaPush(&ppAllResourceCaches, newCache);

	return newCache;
}

void resCacheSetDebugName(ResourceCache *pCache, const char* name)
{
	SAFE_FREE(pCache->debugName);
	pCache->debugName = strdup(name);
}

void resCacheCancelAllLocks(ResourceCache *pCache, bool bSendUpdates)
{
	StashTableIterator stashIter;
	StashElement element;
	ResourceDictionaryIterator dictIter;
	ResourceDictionary *pCheckedDictionary;

	resDictInitIterator(&dictIter);
	while (pCheckedDictionary = resDictIteratorGetNextDictionary(&dictIter))
	{
		stashGetIterator(pCheckedDictionary->resourceStatusTable,&stashIter);
		while (stashGetNextElement(&stashIter, &element))
		{
			ResourceStatus *pInfo = stashElementGetPointer(element);
			if (pInfo->iLockOwner == pCache->userID)
			{				
				resUpdateResourceLockOwner(pCheckedDictionary, pInfo->pResourceName, 0);
			}
		}
	}
}

void resServerDestroyResourceCache(ResourceCache *pCache)
{
	StashTableIterator dictIter;
	StashElement dictElement;
	int i;
	
	SAFE_FREE(pCache->debugName);
	
	eaFindAndRemoveFast(&ppAllResourceCaches, pCache);

	if (pCache->userLogin)
	{	
		resCacheCancelAllLocks(pCache, false);
	}

	stashGetIterator(pCache->perDictionaryCaches, &dictIter);
	while (stashGetNextElement(&dictIter, &dictElement))
	{	
		PerDictionaryResourceCache *pDictCache = stashElementGetPointer(dictElement);

		stashTableDestroySafe(&pDictCache->sTableResourceToVersion);
		stashTableClearEx(	pDictCache->sTableResourceToSentState,
							NULL,
							destroyClientResourceSentState);
		stashTableDestroySafe(&pDictCache->sTableResourceToSentState);
		SAFE_FREE(pDictCache);
	}
	stashTableDestroySafe(&pCache->perDictionaryCaches);

	for (i=0; i < eaSize(&pCache->ppPendingResourceUpdates); i++)
	{
		ResourceRequest *pRequest = pCache->ppPendingResourceUpdates[i];
		if (gLogResourceRequests)
			filelog_printf("resourceSend.log","Deleting %s %s %s", StaticDefineIntRevLookup(ResourceUpdateTypeEnum,pRequest->operation), pRequest->pDictionaryName, pRequest->pResourceName);

		resDestroyResourceRequest(pRequest);
	}

	eaDestroy(&pCache->ppPendingResourceUpdates);
	resServerDestroySentUpdates(pCache);
	eaDestroy(&pCache->ppSentResourceUpdates);

	free(pCache);
}

PerDictionaryResourceCache *resGetPerDictionaryCache(ResourceCache *pCache, const char *pDictName)
{
	PerDictionaryResourceCache *pDictCache;
	if (!pCache)
	{
		return NULL;
	}
	if (stashFindPointer(pCache->perDictionaryCaches, pDictName, &pDictCache))
	{
		return pDictCache;
	}
	return NULL;
}

PerDictionaryResourceCache *resGetOrCreatePerDictionaryCache(ResourceCache *pCache, const char *pDictName)
{
	PerDictionaryResourceCache *pDictCache;
	if (!pCache)
	{
		return NULL;
	}
	if (stashFindPointer(pCache->perDictionaryCaches, pDictName, &pDictCache))
	{
		return pDictCache;
	}

	pDictCache = calloc(sizeof(PerDictionaryResourceCache), 1);
	pDictCache->pDictName = pDictName;
	pDictCache->sTableResourceToVersion = stashTableCreateAddress(16);
	stashAddPointer(pCache->perDictionaryCaches, pDictName, pDictCache, 0);
	return pDictCache;
}

//Request that the server-side ref system send the specified referent down to the client, if it's not already believed
//to be there.
void resServerRequestSendResourceUpdate(ResourceDictionary *pDictionary, const char *pResourceName, void * pResource, ResourceCache *pCache, PerDictionaryResourceCache *pDictCache, int command)
{
	ResourceRequest *pRequest;
	StructTypeField excludeFlags = TOK_SERVER_ONLY | TOK_EDIT_ONLY;

	assertmsg(	pDictionary ||
					command == RESUPDATE_HANDLE_COMPLETED_ACTIONS ||
					command == RESUPDATE_NAMESPACE_UPDATE ||
					command == RESUPDATE_DISPLAY_RESOURCE_STATUS,
				"Invalid ref dictionary for RequestSendResourceToClient");

	//No longer asserting on this because this dictionary may normally provide missing data
	//but no longer does due to having that disabled
	if (pDictionary && !pDictionary->bShouldProvideMissingData)
	{
		return;
	}
	
	PERFINFO_AUTO_START_FUNC();

	if (!areEditorsAllowed())
	{
		assert(	command == RESUPDATE_NEW_RESOURCE ||
				command == RESUPDATE_MODIFIED_RESOURCE ||
				command == RESUPDATE_FORCE_UPDATE ||
				command == RESUPDATE_DESTROYED_RESOURCE);
	}

	pResourceName = allocAddString(pResourceName);

	if (pDictionary)
	{	
		// Optimization in case caller already looked up the cache
		if (!pDictCache)
		{
			pDictCache = resGetPerDictionaryCache(pCache, pDictionary->pDictName);
		}

		// Figure out if we should actually send down the referent, based on what the client has already asked for
		// Test modify/destroy first since it's most frequent
		if (command == RESUPDATE_MODIFIED_RESOURCE ||
			command == RESUPDATE_DESTROYED_RESOURCE)
		{
			if (!pDictCache || !stashFindInt(pDictCache->sTableResourceToVersion, pResourceName, NULL))
			{
				if (gLogResourceRequests)
					filelog_printf("resourceSend.log","Failed to queue Update, client doesn't have: %s %s %s", StaticDefineIntRevLookup(ResourceUpdateTypeEnum,command), pDictionary->pDictName, pResourceName);

				PERFINFO_AUTO_STOP();// FUNC
				return;
			}
			if(	printSubscriptionChanges &&
				pDictionary->bIsCopyDictionary)
			{
				S32 found;
				U32 sentVersion = 0;
				ReferentPrevious* rp = resGetObjectPreviousFromDict(pDictionary, pResourceName);

				found = pDictCache && stashFindInt(pDictCache->sTableResourceToVersion, pResourceName, &sentVersion);
				
				printfColor(COLOR_BRIGHT|COLOR_RED|COLOR_GREEN,
							"User %d:%s: Queueing %s of %s[%s], %s v%d (cur %s v%d).\n",
							pCache->userID,
							pCache->debugName,
							command == RESUPDATE_MODIFIED_RESOURCE ?
								"MODIFIED_RESOURCE" :
								"DESTROYED_RESOURCE",
							pDictionary->pDictName,
							pResourceName,
							found ? "sent" : "unsent",
							sentVersion,
							rp ? "sent" : "unsent",
							SAFE_MEMBER(rp, version));
			}
			if (pDictionary->bDataEditingMode)
				excludeFlags = 0;
		}		
		else if (command == RESUPDATE_NEW_RESOURCE)
		{
			if(	printSubscriptionChanges &&
				pDictionary->bIsCopyDictionary)
			{
				S32 found;
				U32 sentVersion = 0;
				ReferentPrevious* rp = resGetObjectPreviousFromDict(pDictionary, pResourceName);

				found = pDictCache && stashFindInt(	pDictCache->sTableResourceToVersion, pResourceName, &sentVersion);
				
				printfColor(COLOR_BRIGHT|COLOR_GREEN,
							"User %d:%s: Queueing NEW_RESOURCE of %s[%s], %s v%d (cur %s v%d).\n",
							pCache->userID,
							pCache->debugName,
							pDictionary->pDictName,
							pResourceName,
							found ? "sent" : "unsent",
							sentVersion,
							rp ? "sent" : "unsent",
							SAFE_MEMBER(rp, version));
			}

			if (!pDictCache) {
				// Create if didn't exist
				pDictCache = resGetOrCreatePerDictionaryCache(pCache, pDictionary->pDictName);
				assert(pDictCache);
			}

			if (!stashAddInt(pDictCache->sTableResourceToVersion, pResourceName, 0, false))
			{
				// If this is not a copy dictionary, then don't re-send if think the client has it
				// Copy dictionaries always re-send when requested.
				if (!pDictionary->bIsCopyDictionary) {
					if (gLogResourceRequests)
						filelog_printf("resourceSend.log","Failed to queue New, client already has: %s %s %s", StaticDefineIntRevLookup(ResourceUpdateTypeEnum,command), pDictionary->pDictName, pResourceName);

					PERFINFO_AUTO_STOP();// FUNC
					return;
				}
			}
			if (pDictionary->bDataEditingMode)
				excludeFlags = 0;
		}
		else if (command == RESUPDATE_FORCE_UPDATE)
		{
			if(	printSubscriptionChanges &&
				pDictionary->bIsCopyDictionary)
			{
				S32 found;
				U32 sentVersion = 0;
				ReferentPrevious* rp = resGetObjectPreviousFromDict(pDictionary, pResourceName);

				found = pDictCache && stashFindInt(	pDictCache->sTableResourceToVersion, pResourceName, &sentVersion);
				
				printfColor(COLOR_BRIGHT|COLOR_GREEN,
							"User %d:%s: Queueing FORCE_UPDATE of %s[%s], %s v%d (cur %s v%d).\n",
							pCache->userID,
							pCache->debugName,
							pDictionary->pDictName,
							pResourceName,
							found ? "sent" : "unsent",
							sentVersion,
							rp ? "sent" : "unsent",
							SAFE_MEMBER(rp, version));
			}

			if (!pDictCache) {
				// Create if didn't exist
				pDictCache = resGetOrCreatePerDictionaryCache(pCache, pDictionary->pDictName);
				assert(pDictCache);
			}
			stashAddInt(pDictCache->sTableResourceToVersion, pResourceName, 0, false);
			if (pDictionary->bDataEditingMode)
				excludeFlags = 0;
		}
	}

	if (pDictionary)
	{
		pRequest = resCreateResourceRequest(command, pDictionary->pDictName, pResourceName, 1, NULL);
	}
	else	
	{
		pRequest = resCreateResourceRequest(command, GLOBALCOMMANDS_DICTIONARY, pResourceName, 1, NULL);
	}
	
	if (command == RESUPDATE_NEW_RESOURCE ||
		command == RESUPDATE_MODIFIED_RESOURCE ||
		command == RESUPDATE_FORCE_UPDATE)
	{
		pRequest->pRequestParseTable = pDictionary->pDictTable;
	}
	else if (command == RESUPDATE_INDEX_UPDATE)
	{
		pRequest->pRequestParseTable = parse_ResourceInfo;
	}
	else if (command == RESUPDATE_ERROR)
	{
		pRequest->pRequestParseTable = parse_ErrorMessage;
	}
	else if (command == RESUPDATE_NAMESPACE_UPDATE)
	{
		pRequest->pRequestParseTable = parse_ResourceNameSpace;
	}
	else if(command == RESUPDATE_DISPLAY_RESOURCE_STATUS ||
			command == RESUPDATE_HANDLE_COMPLETED_ACTIONS)
	{
		pRequest->pRequestParseTable = parse_ResourceActionList;
	}


	if (!resPushRequestServer(&pCache->ppPendingResourceUpdates, pRequest))
	{
		if (gLogResourceRequests)
		{
			filelog_printf(	"resourceSend.log",
							"Failed to queue Update, already in list: %s %s %s",
							StaticDefineIntRevLookup(ResourceUpdateTypeEnum,command),
							pDictionary?pDictionary->pDictName:"NONE",
							pResourceName);
		}

		resDestroyResourceRequest(pRequest);
		PERFINFO_AUTO_STOP();// FUNC
		return;
	}
	
	if (gLogResourceRequests)
	{
		filelog_printf("resourceSend.log","Queued Update: %s %s %s", StaticDefineIntRevLookup(ResourceUpdateTypeEnum,command),  pDictionary?pDictionary->pDictName:"NONE", pResourceName);
	}

	if (pResource)
	{		
		pRequest->pRequestData = StructCreateVoid(pRequest->pRequestParseTable);
		StructCopyFieldsVoid(pRequest->pRequestParseTable,pResource,pRequest->pRequestData,0,excludeFlags);
	}

	if (command == RESUPDATE_NEW_RESOURCE ||
		command == RESUPDATE_FORCE_UPDATE ||
		command == RESUPDATE_MODIFIED_RESOURCE)
	{
		// Also send referents that are needed by thing being sent down
		ResourceInfo *pInfo = NULL;

		if (pDictionary->bTempDisableResourceInfo && pDictionary->pPendingInfo)
		{
			pInfo = resGetInfoFromDictInfo(pDictionary->pPendingInfo, pResourceName);
		}
		if (!pInfo)
		{
			pInfo = resGetInfo(pDictionary->pDictName, pResourceName);
		}

		if (pInfo)
		{
			int i;
			
			PERFINFO_AUTO_START("references", 1);
			
			for (i = 0; i < eaSize(&pInfo->ppReferences); i++)
			{
				ResourceReference *pRef = pInfo->ppReferences[i];
				if (pRef->referenceType == REFTYPE_CONTAINS
					||
					pRef->referenceType == REFTYPE_CHILD_OF &&
					pDictionary->bDataEditingMode)
				{
					ResourceDictionary *pOther = resGetDictionary(pRef->resourceDict);
					if (pOther)
					{
						int newCommand = (command == RESUPDATE_FORCE_UPDATE) ? RESUPDATE_FORCE_UPDATE : RESUPDATE_NEW_RESOURCE;
						resServerRequestSendResourceUpdate(pOther, pRef->resourceName, NULL, pCache, NULL, newCommand);
					}
				}
			}
			
			PERFINFO_AUTO_STOP();
		}

	}

	PERFINFO_AUTO_STOP();// FUNC
}

U32 resServerRequestFenceInstruction(ResourceCache *pCache, resServerFenceCallback *pFenceCB, UserData pData)
{
	static U32 s_fence_id = 1;
	ResourceRequest *pRequest = resCreateResourceRequest(RESUPDATE_FENCE_INSTRUCTION, NULL, NULL, true, NULL);
	assert(pFenceCB);
	pRequest->pRequestData = pFenceCB;
	pRequest->pRequestData2 = pData;
	pRequest->uFenceID = s_fence_id++;
	eaPush(&pCache->ppPendingResourceUpdates, pRequest);
	return pRequest->uFenceID;
}

ResourceCache *resGetCacheFromUserID(U32 userID)
{
	int i;
	for (i=0; i < eaSize(&ppAllResourceCaches); i++)
	{	
		ResourceCache *pCache = ppAllResourceCaches[i];
		if (pCache->userID == userID)
		{
			return pCache;
		}
	}
	return NULL;
}

ResourceCache *resGetCacheFromEditingLogin(const char *pEditLogin)
{
	int i;
	pEditLogin = allocFindString(pEditLogin);
	if (!pEditLogin)
	{
		return NULL;
	}
	for (i=0; i < eaSize(&ppAllResourceCaches); i++)
	{	
		ResourceCache *pCache = ppAllResourceCaches[i];
		if (pCache->userLogin == pEditLogin)
		{
			return pCache;
		}
	}
	return NULL;
}

ResourceCache *resGetCacheWithEditingLogin(void)
{
	int i;
	for (i=0; i < eaSize(&ppAllResourceCaches); i++)
	{	
		ResourceCache *pCache = ppAllResourceCaches[i];
		if (pCache->userLogin)
		{
			return pCache;
		}
	}
	return NULL;
}

void resServerUpdateModifiedReferentOnAllClients(	ResourceDictionary *pDictionary,
													const char *pResourceName)
{
	int i;

	if (!pDictionary)
	{
		return;
	}
	
	PERFINFO_AUTO_START_FUNC();
	
	for (i=0; i < eaSize(&ppAllResourceCaches); i++)
	{	
		ResourceCache *pCache = ppAllResourceCaches[i];
		PerDictionaryResourceCache *pDictCache = resGetPerDictionaryCache(pCache, pDictionary->pDictName);

		if (pDictCache)
		{
			resServerRequestSendResourceUpdate(pDictionary, pResourceName, NULL, pCache, pDictCache, RESUPDATE_MODIFIED_RESOURCE);
		}
	}
	
	PERFINFO_AUTO_STOP();
}

void resServerUpdateDestroyedReferentOnAllClients(ResourceDictionary *pDictionary, const char *pResourceName)
{
	int i;

	if (!pDictionary)
	{
		return;
	}
	
	PERFINFO_AUTO_START_FUNC();
	
	for (i=0; i < eaSize(&ppAllResourceCaches); i++)
	{	
		ResourceCache *pCache = ppAllResourceCaches[i];
		PerDictionaryResourceCache *pDictCache = resGetPerDictionaryCache(pCache, pDictionary->pDictName);

		if (pDictCache)
		{
			resServerRequestSendResourceUpdate(pDictionary, pResourceName, NULL, pCache, pDictCache, RESUPDATE_DESTROYED_RESOURCE);
		}
	}
	
	PERFINFO_AUTO_STOP();
}

void resUpdateResourceLockOwner(ResourceDictionary *pDictionary, const char *pResourceName, U32 newOwner)
{
	ResourceCache *pCache;
	ResourceStatus *pStatus = resGetOrCreateStatus(pDictionary,pResourceName);
	void *pResource = resGetObjectFromDict(pDictionary, pResourceName);
	U32 oldOwner = pStatus->iLockOwner;
	if (oldOwner && oldOwner != newOwner)
	{
		resDictRunEventCallbacks(pDictionary, RESEVENT_RESOURCE_UNLOCKED, pResourceName, pResource);
		pCache = resGetCacheFromUserID(pStatus->iLockOwner);
		if (pCache)
		{
			resServerRequestSendResourceUpdate(pDictionary, pResourceName, NULL, pCache, NULL, RESUPDATE_UNLOCKED);
		}
	}
	pStatus->iLockOwner = newOwner;
	if (pStatus->iLockOwner && oldOwner != newOwner)
	{	
		resDictRunEventCallbacks(pDictionary, RESEVENT_RESOURCE_LOCKED, pResourceName, pResource);
		pCache = resGetCacheFromUserID(pStatus->iLockOwner);
		if (pCache)
		{
			resServerRequestSendResourceUpdate(pDictionary, pResourceName, NULL, pCache, NULL, RESUPDATE_LOCKED);
		}
	}
}

void resUpdateResourceLockedStatus(ResourceDictionary *pDictionary, const char *pResourceName)
{
	ResourceCache *pCache;
	ResourceStatus *pStatus = resGetStatus(pDictionary,pResourceName);
	if (!pStatus)
	{
		return;
	}
	if (!pStatus->pResourceOwner)
	{
		resUpdateResourceLockOwner(pDictionary, pResourceName, 0);
	}	
	pCache = resGetCacheFromEditingLogin(pStatus->pResourceOwner);
	if (pCache)
	{
		resUpdateResourceLockOwner(pDictionary, pResourceName, pCache->userID);
	}
}

void resServerUpdateModifiedResourceInfoOnAllClients(ResourceDictionary *pDictionary, const char *pResourceName)
{
	int i;

	if (!pDictionary || !pDictionary->bShouldProvideMissingData)
	{
		return;
	}

	for (i=0; i < eaSize(&ppAllResourceCaches); i++)
	{	
		ResourceCache *pCache = ppAllResourceCaches[i];
		PerDictionaryResourceCache *pDictCache = resGetPerDictionaryCache(pCache, pDictionary->pDictName);

		if (pDictCache && pDictionary->bShouldMaintainIndex && pDictCache->iSubscribedToIndex)
		{
			ResourceInfo *objectInfo = resGetInfo(pDictionary->pDictName, pResourceName);
			resServerRequestSendResourceUpdate(pDictionary, pResourceName, objectInfo, pCache, pDictCache, RESUPDATE_INDEX_UPDATE);
		}		
	}
}

// this function is called from CostumeCommonLoad with dictionary handles
void resServerSendReferentToClient(ResourceCache *pCache, DictionaryHandleOrName dictHandleOrName, const char *pResourceName, bool bForce)
{
	ResourceDictionary *pDictionary = resGetDictionary(dictHandleOrName);

	if (!pDictionary)
	{
		return;
	}

	resServerRequestSendResourceUpdate(pDictionary, pResourceName, NULL, pCache, NULL, bForce ? RESUPDATE_FORCE_UPDATE : RESUPDATE_NEW_RESOURCE);	
}

void resDictProvideMissingResources(DictionaryHandleOrName dictHandle)
{
	ResourceDictionary *pDictionary;

	pDictionary = resGetDictionary(dictHandle);

	assertmsg(pDictionary && !pDictionary->bShouldProvideMissingData, "Invalid dictionary specified to provide missing data... must be self-defining, and not already providing");

	assertmsg(pDictionary->pDictTable, "Invalid dictionary specified to provide missing data... must have parse table");

	pDictionary->bShouldProvideMissingData = true;
}

void resDictSetLocalEditingOverride(DictionaryHandleOrName dictHandle)
{
	ResourceDictionary *pDictionary;

	pDictionary = resGetDictionary(dictHandle);

	assertmsg(pDictionary && pDictionary->bShouldProvideMissingData, "Invalid dictionary specified to for local editing override.... must be providing missing data");

	assertmsg(pDictionary->pDictTable, "Invalid dictionary specified to provide missing data... must have parse table");

	pDictionary->bLocalEditingOverride = true;
}

void resDictAllowForwardIncomingRequests(DictionaryHandleOrName dictHandle)
{
	ResourceDictionary *pDictionary;

	pDictionary = resGetDictionary(dictHandle);

	assertmsg(pDictionary && pDictionary->bShouldProvideMissingData, "Invalid dictionary specified for request forwarding.... must be providing missing data");

	assertmsg(pDictionary->pDictTable, "Invalid dictionary specified for request forwarding... must have parse table");

	pDictionary->bForwardIncomingRequests = true;
}



void resDictProvideMissingRequiresEditMode(DictionaryHandleOrName dictHandle)
{
	ResourceDictionary *pDictionary;

	pDictionary = resGetDictionary(dictHandle);

	assertmsg(pDictionary && !pDictionary->bRequiresEditingModeToProvideMissing, "Invalid dictionary specified to provide missing data... must be self-defining, and not already requiring");

	assertmsg(pDictionary->pDictTable, "Invalid dictionary specified to provide missing data... must have parse table");

	pDictionary->bRequiresEditingModeToProvideMissing = true;
}

resCallback_BadPacket *cbBadPacket;
void resServerSetBadPacketCallback(resCallback_BadPacket *pCB)
{
	cbBadPacket = pCB;
}


static void reportBadResourcePacket(ResourceCache *pCache, char *reason)
{
	if (g_isContinuousBuilder)
	{
		assertmsgf(0, "Invalid Resource Packet: %s", reason);
	}
	else
	{
		Errorf("Invalid Resource Packet: %s", reason);
		if (cbBadPacket)
		{
			cbBadPacket(pCache->userID, reason);
		}
	}
}

static void reportBadResourcePacketf(ResourceCache *pCache, char const *fmt, ...)
{
	static char *errMsg;
	va_list ap;

	va_start(ap, fmt);
	estrClear(&errMsg);
	estrConcatfv(&errMsg, fmt, ap);
	reportBadResourcePacket(pCache, errMsg);
	va_end(ap);
}

static char **ppIgnoredDictionaries; // Ignore any requests to these dictionaries.

void resServerIgnoreDictionaryRequests(const char *dictName)
{
	int i;
	for (i = 0; i < eaSize(&ppIgnoredDictionaries); i++)
	{
		if (stricmp(dictName, ppIgnoredDictionaries[i]) == 0)
		{
			// Silently ignore these dictionaries
			return;
		}
	}
	eaPush(&ppIgnoredDictionaries, strdup(dictName));
}

void resServerSendNamespaceInfo(ResourceCache *pCache)
{
	ResourceNameSpace *pNameSpace;
	ResourceNameSpaceIterator iterator;
	resNameSpaceInitIterator(&iterator);
	while (pNameSpace = resNameSpaceIteratorGetNext(&iterator))
	{
		if (!resNameSpaceValidForCache(pNameSpace, pCache))
		{
			continue;
		}
		resServerRequestSendResourceUpdate(NULL, pNameSpace->pName, pNameSpace, pCache, NULL, RESUPDATE_NAMESPACE_UPDATE);
	}
}

void resServerSendMessage(const char *name, ResourceCache *pCache)
{
	ResourceDictionary *pDictionary = resGetDictionary(gMessageDict);
	resServerRequestSendResourceUpdate(pDictionary, name, NULL, pCache, NULL, RESUPDATE_FORCE_UPDATE);
}

void resServerSendResourcesForNamespace(ResourceCache* pCache, DictionaryHandleOrName dict, const char* namespace)
{
	ResourceDictionary *pDictionary = resGetDictionary(dict);
	ParseTable* pti = resDictGetParseTable(dict);
	ResourceIterator iter;
	const char *pName;
	void *pObj;

	resInitIterator(dict, &iter);
	while (resIteratorGetNextForNamespace(&iter, namespace, &pName, &pObj))
	{
		langForEachMessageRef(pti, pObj, resServerSendMessage, pCache);
		resServerRequestSendResourceUpdate(pDictionary, pName, NULL, pCache, NULL, RESUPDATE_FORCE_UPDATE);
	}
	resFreeIterator(&iter);
}

void resServerSendZoneMap(ResourceCache *pCache, const char *map_name)
{
	ResourceDictionary *pDictionary_maps = resGetDictionary("ZoneMap");
	resServerRequestSendResourceUpdate(pDictionary_maps, map_name, NULL, pCache, NULL, RESUPDATE_FORCE_UPDATE);
}

void resServerGrantEditingLogin(ResourceCache *pCache)
{
	ResourceCache *pOtherCache;
	if (!areEditorsAllowed())
	{
		return;
	}

	if ((pOtherCache = resGetCacheWithEditingLogin()) != NULL)
	{
		Alertf("User %s is already logged in for editing. You should use your first client or close it", pOtherCache->userLogin);
		return;
	}

	pCache->userLogin = allocAddString("UGCUser"); // I don't think we can get this without the client telling us --TomY
}

static int eaSortCompare(const char **pstr1, const char **pstr2)
{
	return stricmp(*pstr1, *pstr2);
}

bool resServerProcessClientRequests(Packet *pInPacket, ResourceCache *pCache)
{
	bool bResult = true;
	int command;
	PerfInfoGuard* piGuard;

	PERFINFO_AUTO_START_FUNC_GUARD(&piGuard);

	while (command = pktGetBitsPack(pInPacket,1))
	{
		char *pResourceName;
		const char *pRealName;
		ResourceDictionary *pDictionary;
		void * *pResource = NULL;
		void * *pPreexistingReferent = NULL;
		bool bIgnoredDictionary = false;
		char pDictName[RESOURCE_DICT_NAME_MAX_SIZE];
		pktGetString(pInPacket,pDictName,sizeof(pDictName));

		pResourceName = pktGetStringTemp(pInPacket);
		pDictionary = resGetDictionary(pDictName);
		pRealName = allocAddString(pResourceName); // TODO: Fix this possible DOS attack where things add to string table too much

		if (gLogResourceRequests)
			filelog_printf("resourceSend.log","Received Request: %s %s %s", StaticDefineIntRevLookup(ResourceRequestTypeEnum,command), pDictName, pResourceName);
		if (isDevelopmentMode())
		{
			++s_ResourceTraffic.iNumRequestReceived;
		}

		if (stricmp(pDictName, GLOBALCOMMANDS_DICTIONARY) != 0)
		{
			if (SAFE_DEREF(pDictName))
			{			
				int i;
				for (i = 0; i < eaSize(&ppIgnoredDictionaries); i++)
				{
					if (stricmp(pDictName, ppIgnoredDictionaries[i]) == 0)
					{
						// Silently ignore these dictionaries
						bIgnoredDictionary = true;
					}
				}
			}
			if (!pDictionary && !bIgnoredDictionary)
			{
				reportBadResourcePacketf(pCache,"Invalid dictionary %s sent to dictionary request handler", pDictName);
				return false;
			}
		}

		switch (command)
		{
		xcase RESREQUEST_GET_RESOURCE:
			{	
				PERFINFO_AUTO_START("RESREQUEST_GET_RESOURCE", 1);

				if (!(pDictionary && resShouldProvideMissingResources(pDictionary) && pDictionary->pDictTable))
				{				
					if (!bIgnoredDictionary)
					{
						reportBadResourcePacketf(	pCache,
													"Invalid dictionary %s[%s] for Get Resource",
													pDictName,
													pResourceName);
					}
					bResult = false;

					PERFINFO_AUTO_STOP();
					continue;
				}

				resServerRequestSendResourceUpdate(pDictionary, pResourceName, NULL, pCache, NULL, RESUPDATE_NEW_RESOURCE);

				if (pDictionary->bForwardIncomingRequests &&
					!resGetObjectFromDict(pDictionary, pResourceName))
				{
					resNotifyRefsExistWithReason(pDictName, pResourceName, "Forwarding from client");
				}

				PERFINFO_AUTO_STOP();
			}
		xcase RESREQUEST_OPEN_RESOURCE:
			{				
				PERFINFO_AUTO_START("RESREQUEST_OPEN_RESOURCE", 1);

				if (!(pDictionary && resShouldProvideMissingResources(pDictionary) && pDictionary->pDictTable))
				{				
					if (!bIgnoredDictionary)
					{
						reportBadResourcePacketf(	pCache,
													"Invalid dictionary %s[%s] for Open Resource",
													pDictName,
													pResourceName);
					}
					bResult = false;
				}
				else
				{
					resServerRequestSendResourceUpdate(pDictionary, pResourceName, NULL, pCache, NULL, RESUPDATE_FORCE_UPDATE);
				}

				PERFINFO_AUTO_STOP();
			}
		xcase RESREQUEST_HAS_RESOURCE:
			{			
				PerDictionaryResourceCache *pDictCache;

				PERFINFO_AUTO_START("RESREQUEST_HAS_RESOURCE", 1);

				if (!(pDictionary && resShouldProvideMissingResources(pDictionary) && pDictionary->pDictTable))
				{
					if (!bIgnoredDictionary)
						reportBadResourcePacketf(pCache,"Invalid dictionary %s for Has Resource", pDictName);
					bResult = false;
				}
				else
				{
					pDictCache = resGetOrCreatePerDictionaryCache(pCache, pDictionary->pDictName);

					stashAddInt(pDictCache->sTableResourceToVersion, pRealName, 0, false);
				}

				PERFINFO_AUTO_STOP();
			}
		xcase RESREQUEST_GET_ALL_RESOURCES:
			{
				ResourceIterator iterator;
				char *iterData;

				PERFINFO_AUTO_START("RESREQUEST_GET_ALL_RESOURCES", 1);

				if (!(pDictionary && resShouldProvideMissingResources(pDictionary) && pDictionary->pDictTable))
				{
					if (!bIgnoredDictionary)
						reportBadResourcePacketf(pCache,"Invalid dictionary %s for Get All", pDictName);
					bResult = false;
				}
				else if (!areEditorsAllowed())
				{
					reportBadResourcePacket(pCache,"Dev command in production mode");
					bResult = false;
				}
				else
				{
					char **dataList = NULL;
					int i;
					resInitIterator(pDictionary->pDictName, &iterator);

					while (resIteratorGetNext(&iterator, &iterData, NULL))							
					{
						eaPush(&dataList, iterData);
					}
					resFreeIterator(&iterator);
					eaQSort(dataList, eaSortCompare);
					for (i = 0; i < eaSize(&dataList); i++)
					{
						resServerRequestSendResourceUpdate(pDictionary, dataList[i], NULL, pCache, NULL, RESUPDATE_FORCE_UPDATE);
					}
				}

				PERFINFO_AUTO_STOP();
			}
		xcase RESREQUEST_SET_EDITMODE:
			{
				bool bChanged = false;

				PERFINFO_AUTO_START("RESREQUEST_SET_EDITMODE", 1);

				if (!(pDictionary && pDictionary->bShouldProvideMissingData && pDictionary->pDictTable))
				{
					if (!bIgnoredDictionary)
						reportBadResourcePacketf(pCache,"Invalid dictionary %s for Set Edit Mode", pDictName);
					bResult = false;
				}
				else if (!areEditorsAllowed())
				{
					reportBadResourcePacket(pCache,"Dev command in production mode");
					bResult = false;
				}
				else
				{
					if (stricmp(pResourceName,"ON") == 0)
					{
						resSetDictionaryEditModeServer(pDictName, true);
					}
					else if (stricmp(pResourceName,"OFF") == 0)
					{
						resSetDictionaryEditModeServer(pDictName, false);
					}
					else
					{
						reportBadResourcePacketf(pCache,"Invalid edit mode packet sent");
						bResult = false;
					}
				}

				PERFINFO_AUTO_STOP();
			}
		xcase RESREQUEST_SUBSCRIBE_TO_INDEX:
			{
				PERFINFO_AUTO_START("RESREQUEST_SUBSCRIBE_TO_INDEX", 1);

				if (!(pDictionary && pDictionary->bShouldProvideMissingData && pDictionary->pDictTable))
				{
					if (!bIgnoredDictionary)
						reportBadResourcePacketf(pCache,"Invalid dictionary %s for subscribe-to-index", pDictName);
					bResult = false;
				}
				else if (!areEditorsAllowed())
				{
					reportBadResourcePacket(pCache,"Dev command in production mode");
					bResult = false;
				}
				else if (stricmp(pResourceName,"ON") == 0)
				{
					ResourceDictionaryInfo *pDictInfo = resDictGetInfo(pDictName);
					PerDictionaryResourceCache *pDictCache = resGetOrCreatePerDictionaryCache(pCache, pDictionary->pDictName);
					pDictCache->iSubscribedToIndex++;
					if (pDictInfo && (pDictCache->iSubscribedToIndex == 1))
					{
						int i;
						for (i = 0; i < eaSize(&pDictInfo->ppInfos); i++)
						{
							ResourceInfo *objInfo = pDictInfo->ppInfos[i];
							resServerRequestSendResourceUpdate(pDictionary, objInfo->resourceName, objInfo, pCache, pDictCache, RESUPDATE_INDEX_UPDATE);
						}
						resServerRequestSendResourceUpdate(pDictionary, "ON", NULL, pCache, pDictCache, RESUPDATE_INDEX_SUBSCRIBED);
					}		
				}
				else if (stricmp(pResourceName,"OFF") == 0)
				{
					PerDictionaryResourceCache *pDictCache = resGetOrCreatePerDictionaryCache(pCache, pDictionary->pDictName);
					pDictCache->iSubscribedToIndex--;
					if (pDictCache->iSubscribedToIndex == 0)
					{
						resServerRequestSendResourceUpdate(pDictionary, "OFF", NULL, pCache, pDictCache, RESUPDATE_INDEX_SUBSCRIBED);
					}
					else if (pDictCache->iSubscribedToIndex < 0)
					{
						pDictCache->iSubscribedToIndex = 0;
					}
				}
				else
				{
					reportBadResourcePacketf(pCache,"Invalid subscription packet sent");
					bResult = false;
				}

				PERFINFO_AUTO_STOP();
			}
		xcase RESREQUEST_REQUEST_EDITING_LOGIN:
			{
				StashTableIterator stashIter;
				StashElement element;
				ResourceDictionaryIterator dictIter;
				ResourceDictionary *pCheckedDictionary;
				ResourceCache *pOtherCache;

				PERFINFO_AUTO_START("RESREQUEST_REQUEST_EDITING_LOGIN", 1);

				if (!areEditorsAllowed())
				{
					reportBadResourcePacket(pCache,"Dev command in production mode");
					bResult = false;
					PERFINFO_AUTO_STOP();
					continue;
				}

				if (pOtherCache = resGetCacheFromEditingLogin(pResourceName))
				{
					if (pCache == pOtherCache)
					{
						// Fine to double log in
						PERFINFO_AUTO_STOP();
						break;
					}
					// TODO: something better in production
					Alertf("User %s is already logged in for editing. You should use your first client or close it", pResourceName);
					PERFINFO_AUTO_STOP();
					break;
				}
				pCache->userLogin = allocAddString(pResourceName);

				resDictInitIterator(&dictIter);
				while (pCheckedDictionary = resDictIteratorGetNextDictionary(&dictIter))
				{
					stashGetIterator(pCheckedDictionary->resourceStatusTable,&stashIter);
					while (stashGetNextElement(&stashIter, &element))
					{
						ResourceStatus *pInfo = stashElementGetPointer(element);
						if (pInfo->pResourceOwner == pCache->userLogin)
						{
							resUpdateResourceLockOwner(pCheckedDictionary, pInfo->pResourceName, pCache->userID);
						}
					}
				}

				resServerSendNamespaceInfo(pCache);
					
				PERFINFO_AUTO_STOP();
			}
		xcase RESREQUEST_REQUEST_EDITING_LOGOFF:
			{
				ResourceNameSpace *pNameSpace;
				ResourceNameSpaceIterator iterator;
				
				PERFINFO_AUTO_START("RESREQUEST_REQUEST_EDITING_LOGOFF", 1);

				if (!areEditorsAllowed())
				{
					reportBadResourcePacket(pCache,"Dev command in production mode");
					bResult = false;
					PERFINFO_AUTO_STOP();
					continue;
				}				
				
				resCacheCancelAllLocks(pCache, true);

				pCache->userLogin = NULL;
						
				resNameSpaceInitIterator(&iterator);
				while (pNameSpace = resNameSpaceIteratorGetNext(&iterator))
				{
					if (!resNameSpaceValidForCache(pNameSpace, pCache))
					{
						continue;
					}
					resServerRequestSendResourceUpdate(NULL, pNameSpace->pName, NULL, pCache, NULL, RESUPDATE_NAMESPACE_UPDATE);
				}

				PERFINFO_AUTO_STOP();
			}
		xcase RESREQUEST_REQUEST_RESOURCE_STATUS:
			{
				ResourceActionList *pHolder;

				PERFINFO_AUTO_START("RESREQUEST_REQUEST_RESOURCE_STATUS", 1);

				if (!areEditorsAllowed())
				{
					reportBadResourcePacket(pCache,"Dev command in production mode");
					bResult = false;
				}
				else
				{
					pHolder = resGetResourceStatusList();

					resServerRequestSendResourceUpdate(NULL, "IGNORE", pHolder, pCache, NULL, RESUPDATE_DISPLAY_RESOURCE_STATUS);

					if (pHolder)
						StructDestroy(parse_ResourceActionList, pHolder);
				}

				PERFINFO_AUTO_STOP();
			}

		xcase RESREQUEST_APPLY_RESOURCE_ACTIONS:
			{
				ResourceActionList *pHolder;

				PERFINFO_AUTO_START("RESREQUEST_APPLY_RESOURCE_ACTIONS", 1);

				if (!areEditorsAllowed())
				{
					reportBadResourcePacket(pCache,"Dev command in production mode");
					bResult = false;
				}	
				else if (pktGetBits(pInPacket, 1))
				{
					pHolder = StructCreate(parse_ResourceActionList);

					ParserRecv(parse_ResourceActionList, pInPacket, pHolder, 0);

					resHandleResourceActions(pHolder, pCache->userID, pCache->userLogin);
									
					resServerRequestSendResourceUpdate(NULL, "IGNORE", pHolder, pCache, NULL, RESUPDATE_HANDLE_COMPLETED_ACTIONS);
					
					StructDestroy(parse_ResourceActionList, pHolder);
				}
				else
				{
					// Display failure somehow
				}

				PERFINFO_AUTO_STOP();
			}
		xcase RESREQUEST_CANCEL_REQUEST:
			{
				PerDictionaryResourceCache* pDictCache;
				ClientResourceSentState*	sentState = NULL;
				U32							sentVersion;

				PERFINFO_AUTO_START("RESREQUEST_CANCEL_REQUEST", 1);

				if (!(pDictionary && pDictionary->bShouldProvideMissingData && pDictionary->pDictTable))
				{
					if (!bIgnoredDictionary)
						reportBadResourcePacketf(pCache,"Invalid dictionary %s for Cancel Request", pDictName);
					bResult = false;
					PERFINFO_AUTO_STOP();
					continue;
				}

				pDictCache = resGetOrCreatePerDictionaryCache(pCache, pDictionary->pDictName);

				if(stashRemoveInt(pDictCache->sTableResourceToVersion, pRealName, &sentVersion)){
					if(	printSubscriptionChanges &&
						pDictionary->bIsCopyDictionary)
					{
						ReferentPrevious* rp = resGetObjectPreviousFromDict(pDictionary, pResourceName);

						printfColor(COLOR_BRIGHT|COLOR_RED,
									"User %d:%s: CANCEL_REQUEST of %s[%s], was v%d (cur %s v%d).\n",
									pCache->userID,
									pCache->debugName,
									pDictionary->pDictName,
									pResourceName,
									sentVersion,
									rp ? "sent" : "unsent",
									SAFE_MEMBER(rp, version));
					}
				}
				
				if(	pDictCache->sTableResourceToSentState &&
					stashRemovePointer(	pDictCache->sTableResourceToSentState,
										pRealName,
										&sentState))
				{
					SAFE_FREE(sentState);
				}

				PERFINFO_AUTO_STOP();
			}	
		xdefault:
			reportBadResourcePacket(pCache,"Invalid command");
			bResult = false;
		}
	}	

	PERFINFO_AUTO_STOP_GUARD(&piGuard);

	return bResult;
}

// Reference request Unpacking

void resUnpackHandleRequest(DictionaryHandleOrName dictHandle, int command, const char *pResourceName, void * pResource, PackedStructStream *structStream)
{
	ResourceDictionary *pDictionary = resGetDictionary(dictHandle);
	

	if (!pDictionary || !pDictionary->bShouldRequestMissingData)
	{
		return;
	}

	if (command == RESREQUEST_GET_RESOURCE || command == RESREQUEST_OPEN_RESOURCE)
	{	
		ResourceStatus *pResourceStatus = resGetStatus(pDictionary, pResourceName);
		if (pResourceStatus)
		{
			void *pOldReferent;
			void *pNewReferent;
			U32 dataOffset = pResourceStatus->iResourceLocationID - 1; // So 0 can be invalid
			pOldReferent = resGetObjectFromDict(pDictionary, pResourceName);
			if (pResourceStatus->iResourceLocationID == 0 || pOldReferent)
			{
				// No location, or already here
				return;
			}
			pNewReferent = StructUnpack(pDictionary->pDictTable, structStream, dataOffset);

			pOldReferent = resGetObjectFromDict(pDictionary, pResourceName);
			if (pNewReferent && !pOldReferent) // may have added itself
			{
				pResourceStatus->bResourceManaged = true;

				if (pDictionary->pAddObjectCB)
				{
					if (!pDictionary->pAddObjectCB(pDictionary, pResourceName, pNewReferent, pOldReferent, pDictionary->pUserData))
					{
						StructDestroyVoid(pDictionary->pDictTable, pNewReferent);
					}
				}
				else
				{
					StructDestroyVoid(pDictionary->pDictTable, pNewReferent);
				}
			}
		}
	}
	else if (command == RESREQUEST_GET_ALL_RESOURCES)
	{
		StashTableIterator iterator;
		StashElement element;

		stashGetIterator(pDictionary->resourceStatusTable, &iterator);

		while (stashGetNextElement(&iterator, &element))
		{
			void *pNewReferent;
			void *pOldReferent;
			ResourceStatus *pResourceStatus = stashElementGetPointer(element);		
			U32 dataOffset = pResourceStatus->iResourceLocationID - 1; // So 0 can be invalid
			pResourceName = pResourceStatus->pResourceName;				 
			pOldReferent = resGetObjectFromDict(pDictionary, pResourceName);
			if (!pResourceStatus || pResourceStatus->iResourceLocationID == 0 || pOldReferent)
			{
				// No location, or already here
				continue;
			}
			pNewReferent = StructUnpack(pDictionary->pDictTable, structStream, dataOffset);

			pOldReferent = resGetObjectFromDict(pDictionary, pResourceName);
			if (pNewReferent && !pOldReferent) // may have added itself
			{
				pResourceStatus->bResourceManaged = true;
				if (pDictionary->pAddObjectCB)
				{
					if (!pDictionary->pAddObjectCB(pDictionary, pResourceName, pNewReferent, pOldReferent, pDictionary->pUserData))
					{
						StructDestroyVoid(pDictionary->pDictTable, pNewReferent);
					}
				}
				else
				{
					StructDestroyVoid(pDictionary->pDictTable, pNewReferent);
				}
			}			
		}
	}
}

bool resUnpackPackPotentialReferent_dbg(DictionaryHandleOrName dictHandle, const char *pResourceName, void * pResource, PackedStructStream *structStream MEM_DBG_PARMS)
{
	ResourceDictionary *pDictionary = resGetDictionary(dictHandle);
	U32 dataOffset;

	if (!pDictionary || !pDictionary->bShouldRequestMissingData)
	{
		return false;
	}

	dataOffset = StructPack_dbg(pDictionary->pDictTable, pResource, structStream MEM_DBG_PARMS_INIT);

	return resSetLocationID(dictHandle, pResourceName, dataOffset + 1); // Add 1 so 0 can be invalid
}

// Use this to find if should request missing resource data.
// Don't use this to find if should request index data.  Simply use "pDictionary->bShouldRequestMissingData" for that.
bool resShouldRequestMissingResources(ResourceDictionary *pDictionary)
{
	return (pDictionary->bShouldRequestMissingData && (IsServer() || !pDictionary->bRequiresEditingModeToProvideMissing || pDictionary->bDataEditingMode));
}

// Use this to find if should provide missing resource data.
// Don't use this to find if should provide index data.  Simply use "pDictionary->bShouldRequestMissingData" for that.
bool resShouldProvideMissingResources(ResourceDictionary *pDictionary)
{
	return (pDictionary->bShouldProvideMissingData && (!IsGameServerSpecificallly_NotRelatedTypes() || !pDictionary->bRequiresEditingModeToProvideMissing || pDictionary->bDataEditingMode));
}

// This function is called periodically while in dev mode
void resourcePeriodicUpdate(TimedCallback *callback, F32 timeSinceLastCallback, UserData userData)
{
	if (s_ResourceTraffic.iNumRequestSent || s_ResourceTraffic.iNumRequestReceived || s_ResourceTraffic.iNumResourceSent || s_ResourceTraffic.iNumResourceReceived)
	{
		printf("Resource Traffic: Request(%d sent, %d recv), Resource(%d sent, %d recv, %d bytes)\n",
			s_ResourceTraffic.iNumRequestSent, s_ResourceTraffic.iNumRequestReceived, 
			s_ResourceTraffic.iNumResourceSent,	s_ResourceTraffic.iNumResourceReceived,
			s_ResourceTraffic.iNumResourceBytesSent + s_ResourceTraffic.iNumResourceBytesReceived);

		s_ResourceTraffic.bChanged = 0;
		s_ResourceTraffic.iNumRequestReceived = 0;
		s_ResourceTraffic.iNumRequestSent = 0;
		s_ResourceTraffic.iNumResourceReceived = 0;
		s_ResourceTraffic.iNumResourceSent = 0;
		s_ResourceTraffic.iNumResourceBytesReceived = 0;
		s_ResourceTraffic.iNumResourceBytesSent = 0;
	}
}

#include "AutoGen/ResourceSystem_Internal_h_ast.c"
