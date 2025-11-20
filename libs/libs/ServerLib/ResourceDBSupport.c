#include "ResourceDBSupport.h"
#include "ResourceDBUtils.h"
#include "TextParser.h"
#include "ResourceSystem_Internal.h"
#include "../../crossroads/appserverlib/pub/aslResourceDBPub.h"
#include "structinternals.h"
#include "error.h"
#include "logging.h"
#include "message_h_ast.h"
#include "message.h"
#include "ServerLib.h"
#include "autogen/resourceDBSupport_c_ast.h"
#include "AutoGen/ResourceDBUtils_h_ast.h"
#include "ugcProjectUtils.h"

#define PROJ_SPECIFIC_COMMANDS_ONLY 1
#define RESOURCE_DB_SUPPORT 1

#include "../../crossroads/common/autogen/AppServerLib_autogen_remotefuncs.h"
#include "ReferenceSystem.h"

//if true, assert instead of error/alerting when a bad namespace request would be sent to the
//res db.
static bool gbAssertOnBadNameSpace = false;
AUTO_CMD_INT(gbAssertOnBadNameSpace, AssertOnBadNameSpace) ACMD_CATEGORY(debug) ACMD_COMMANDLINE;

static bool sbUseResourceDB = false;

bool ResourceDBUseAllowed(void)
{
	if (GetAppGlobalType() == GLOBALTYPE_SERVERBINNER || GetAppGlobalType() == GLOBALTYPE_CLIENTBINNER)
	{
		printf("Not using resource DB even though requested\n");
		return false;
	}

	return true;
}


AUTO_COMMAND ACMD_EARLYCOMMANDLINE ACMD_ACCESSLEVEL(0) ACMD_NAME(UseResourceDB);
void AutoSetUseResourceDB(int iSet)
{
	if (ResourceDBUseAllowed())
	{
		sbUseResourceDB = iSet;
	}
}


bool UseResourceDB(void)
{
	return sbUseResourceDB || ugc_DevMode;
}

void SetUseResourceDB(bool bSet)
{
	sbUseResourceDB = bSet;
}

bool ResourceDBHandleGetObject(DictionaryHandleOrName hDict, char *pName, void *pObject, char *pComment)
{
	ResourceDictionary *pResDict = NULL;
	ResourceStatus *pResStatus = NULL;
	bool bAdded = true;

	pResDict = resGetDictionary(hDict);
	pResStatus = resGetStatus(pResDict, pName);

	if (pResDict->bShouldRequestMissingData && (!pResStatus || !pResStatus->bResourceRequested))
	{
		PERFINFO_AUTO_STOP();
		return false;
	}

	if (!pObject)
	{
		log_printf(LOG_RESOURCEDB, "Request for %s %s failed because: %s", 
			RefSystem_GetDictionaryNameFromNameOrHandle(hDict), pName, pComment);
		PERFINFO_AUTO_STOP();
		return false;
	}

	RefSystem_AddReferent(	hDict, 
							pName,
							pObject);

	resRunValidate(RESVALIDATE_POST_RESDB_RECEIVE, hDict, pName, pObject, -1, NULL);
	
	if (!pResStatus)
	{
		pResStatus = resGetOrCreateStatus(pResDict, pName);
	}
	pResStatus->bResourceManaged = true;


	PERFINFO_AUTO_STOP();
	return true;
}


eResourceDBResourceType TypeFromTPI(ParseTable *pTPI)
{
	ParseTableInfo *pInfo = ParserGetTableInfo(pTPI);
	if (pInfo->iResourceDBResType == RESOURCETYPE_UNCALCULATED)
	{
		const char *pName = ParserGetTableName(pTPI);
		pInfo->iResourceDBResType = StaticDefineIntGetIntDefault(eResourceDBResourceTypeEnum, pName, RESOURCETYPE_UNSUPPORTED);
	}
	return pInfo->iResourceDBResType;
}


AUTO_STRUCT;
typedef struct DeferredResourceDBRequest
{
	DictionaryHandleOrName dictHandle; NO_AST
	int command;
	char *pResourceName;
} DeferredResourceDBRequest;

static DeferredResourceDBRequest **sppDeferredRequests = NULL;


void deferResourceDBRequest(DictionaryHandleOrName dictHandle, int command, const char *pResourceName)
{
	int i;

	DeferredResourceDBRequest *pRequest;

	for (i=0; i < eaSize(&sppDeferredRequests); i++)
	{
		if (sppDeferredRequests[i]->command == command && sppDeferredRequests[i]->dictHandle == dictHandle && stricmp(sppDeferredRequests[i]->pResourceName, pResourceName) == 0)
		{
			return;
		}
	}

	pRequest = StructCreate(parse_DeferredResourceDBRequest);
	pRequest->dictHandle = dictHandle;
	pRequest->command = command;
	pRequest->pResourceName = strdup(pResourceName);
	eaPush(&sppDeferredRequests, pRequest);
}

void ProcessDeferredResDebRequests(void)
{
	int i;
	assertmsgf(objLocalManager(), "Can't process deferred res db requests before being connected to trans server... that would defeat the entire purpose");

	for (i=0 ; i < eaSize(&sppDeferredRequests); i++)
	{
		DeferredResourceDBRequest *pRequest = sppDeferredRequests[i];
		resourceDBHandleRequest(pRequest->dictHandle, pRequest->command, pRequest->pResourceName, NULL, "Deferred request");
	}

	eaDestroyStruct(&sppDeferredRequests, parse_DeferredResourceDBRequest);
}



void resourceDBHandleRequest(DictionaryHandleOrName dictHandle, int command, const char *pResourceName, void * pResource, const char* reason)
{
	ParseTable *pTPI;
	char nameSpaceName[RESOURCE_NAME_MAX_SIZE], baseName[RESOURCE_NAME_MAX_SIZE];
	eResourceDBResourceType eType;

	if (!objLocalManager())
	{
		deferResourceDBRequest(dictHandle, command, pResourceName);
		return;
	}

	if (!resExtractNameSpace(pResourceName, nameSpaceName, baseName))
	{
		return;
	}

	// If we're currently editing this namespace, don't service requests
	if (gServerLibState.pcEditingNamespace &&
		stricmp(nameSpaceName, gServerLibState.pcEditingNamespace) == 0)
	{
		return;
	}

	switch (command)
	{
	case RESREQUEST_GET_RESOURCE:
		pTPI = RefSystem_GetDictionaryParseTable(dictHandle);
		eType = TypeFromTPI(pTPI);

		if (eType == RESOURCETYPE_UNCALCULATED || eType == RESOURCETYPE_UNSUPPORTED)
		{
			ErrorOrAlert("INVALID_RESDB_REQ", "Someone requesting that the resource DB provide a %s, it doesn't know how to",
				ParserGetTableName(pTPI));
			return;
		}

		if (!ResourceNameIsSupportedByResourceDB(pResourceName))
		{
			if (gbAssertOnBadNameSpace)
			{
				assert(0);
			}
			else
			{
				ErrorOrAutoGroupingAlert("BAD_NS_FOR_RES_DB", 30, "Someone was about to request resource %s from the resource DB, this is a bad resource name",
					pResourceName);
				ErrorfForceCallstack("Someone was about to request resource %s from the resource DB, this is a bad resource name",
					pResourceName);
			}
			return;
		}

		verbose_printf("Requesting %s %s from the resource DB\n", ParserGetTableName(pTPI), pResourceName);

		VerifyServerTypeExistsInShard(GLOBALTYPE_RESOURCEDB);
		RemoteCommand_RequestResource(GLOBALTYPE_RESOURCEDB, 0, GetAppGlobalType(), GetAppGlobalID(), eType, pResourceName);
		break;

		
	}
}



void OVERRIDE_LATELINK_resDictGetMissingResourceFromResourceDBIfPossible(void *dictNameOrHandle)
{
	if (sbUseResourceDB || ugc_DevMode)
	{
		resDictRequestMissingResources(dictNameOrHandle, 1, false, resourceDBHandleRequest);
		resDictSetLocalEditingOverride(dictNameOrHandle);

		if (isProductionEditMode())
		{
			resDictAllowForwardIncomingRequests(dictNameOrHandle);
			resDictSetMaxUnreferencedResources(dictNameOrHandle, RES_DICT_KEEP_ALL);
		}
	}
}


AUTO_COMMAND_REMOTE;
void ResourceDB_ReceiveMessage(char *pName, ACMD_OWNABLE(Message) ppMessage, char *pComment)
{
	verbose_printf("Receiving Message %s. Comment: %s\n", pName, pComment);

	if (RefSystem_ReferentFromString(gMessageDict, pName))
	{
		//already got it, do nothing, it will get deleted automatically
		return;
	}

	if (ResourceDBHandleGetObject(gMessageDict, pName, *ppMessage, pComment))
	{
		verbose_printf("Added to resource system\n");
		*ppMessage = NULL;
	}
}



//if you create a handle to this resource, will a request be dispatched to the ResourceDB?
bool ResDbWouldTryToProvideResource(eResourceDBResourceType eType, const char *pResourceName)
{
	char nameSpaceName[RESOURCE_NAME_MAX_SIZE], baseName[RESOURCE_NAME_MAX_SIZE];

	if (!sbUseResourceDB && !ugc_DevMode)
	{
		return false;
	}

	if (!RESOURCE_DB_TYPE_VALID(eType))
	{
		return false;
	}

	if (!objLocalManager())
	{
		return false;
	}

	if (!resExtractNameSpace(pResourceName, nameSpaceName, baseName))
	{
		return false;
	}

	return true;
}

//should 100% mirror the logic in aslCheckNamespaceForPatchInfoFillIn
bool ResourceNameIsSupportedByResourceDB(const char *pResourceName)
{
	char nameSpaceName[RESOURCE_NAME_MAX_SIZE], baseName[RESOURCE_NAME_MAX_SIZE];


	if (!resExtractNameSpace(pResourceName, nameSpaceName, baseName))
	{
		return false;
	}
	

	if (namespaceIsUGCAnyShard(nameSpaceName) || strStartsWith(nameSpaceName, "dyn_"))
	{
		return true;
	}

	return false;
}


#include "../../CrossRoads/common/autogen/AppServerLib_autogen_RemoteFuncs.c"
#include "autogen/resourceDBSupport_c_ast.c"
