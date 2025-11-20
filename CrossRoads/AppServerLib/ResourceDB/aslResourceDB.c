/***************************************************************************
*     Copyright (c) 2005-2006, Cryptic Studios
*     All Rights Reserved
*     Confidential Property of Cryptic Studios
***************************************************************************/

#include "AppServerLib.h"
#include "AslPatching.h"
#include "CostumeCommon.h"
#include "FileUtil.h"
#include "ResourceDBUtils.h"
#include "ScratchStack.h"
#include "TimedCallback.h"
#include "aslResourceDB.h"
#include "aslResourceDBPub.h"
#include "autostartupsupport.h"
#include "contact_common.h"
#include "error.h"
#include "globalTypes.h"
#include "mapDescription.h"
#include "mission_Common.h"
#include "objSchema.h"
#include "objTransactions.h"
#include "pcl_client.h"
#include "resourceManager.h"
#include "serverlib.h"
#include "structInternals.h"
#include "winInclude.h"
#include "worldGridPrivate.h"
#include "utilitiesLib.h"
#include "ugcProjectCommon.h"
#include "ugcProjectUtils.h"
#include "Alerts.h"
#include "ResourceInfo.h"
#include "httpXPathSupport.h"

#include "autogen/Appserverlib_autogen_remotefuncs.h"
#include "autogen/AppServerLib_autogen_slowfuncs.h"
#include "autogen/controller_autogen_remotefuncs.h"
#include "autogen/GameServerLib_autogen_remotefuncs.h"
#include "AutoGen/ResourceDBUtils_h_ast.h"
#include "autogen/ServerLib_autogen_remotefuncs.h"

#include "aslResourceDB_h_ast.h"
#include "aslResourceDB_c_ast.h"
#include "mapDescription_h_ast.h"
#include "FloatAverager.h"
#include "eventCountingHeatMap.h"
#include "logging.h"

IntAverager *pPatchTimeAverager = NULL; //average time patching took, not counting time waiting

IntAverager *pTotalPatchWaitAverager = NULL; //average time to wait before patching began

IntAverager *pQueuedPatchWaitAverager = NULL; //of things stuck in the queue, average time spent waiting

FloatMaxTracker *pMaxPatchTimeTracker = NULL; //max time a patching took

EventCounter *pRequestCounter = NULL; //total number of requests

EventCounter *pCacheMissCounter = NULL; //total number of cache misses (ie, requests for namespaces that are totally unknown)
EventCounter *pCacheSemiMissCounter = NULL; //number of requests for resources that were currently being patched
EventCounter *pQueueCount = NULL; //num NSs that were queued
EventCounter *pFreedNSCounter = NULL;

static int iNumPatchFailures = 0;


void LoadNameSpaceData(ResourceDBNameSpace *pNameSpace);
void ProcessNewlyLoadedResourcesForNameSpace(PCL_Client *client, ResourceDBNameSpace *pNameSpace);
void ResourceDB_NameSpaceIsLoaded(ResourceDBNameSpace *pNameSpace);


static StashTable sResourceDBNameSpacesByName = NULL;

static bool sbFreeAllNamespaces = false;
//the resource DB will immediately free all its cached namespaces
AUTO_CMD_INT(sbFreeAllNamespaces, FreeAllNamespaces) ACMD_CATEGORY(debug);



static int siNamespacePatchAlertTime = 60;
//how long a single namespace can be patching without either failing or succeeding before we alert
AUTO_CMD_INT(siNamespacePatchAlertTime, NamespacePatchAlertTime) ACMD_COMMANDLINE;


static int sMegsRAMBeforeReleasingNameSpaces=1024;

//__CATEGORY Settings for the Resource DB
//When this much RAM of namespaces are loaded, start freeing old ones
AUTO_CMD_INT(sMegsRAMBeforeReleasingNameSpaces, MegsRAMBeforeReleasingNameSpaces) ACMD_AUTO_SETTING(ResourceDBSettings, RESOURCEDB);

static int siBadNameSpaceInvalidationTime = 30 * 60;

//When a NS patching attempt fails, or a patched resource is absent, wait this long and then flush that NS out of the cache,
//so it can reload if re-requested
AUTO_CMD_INT(siBadNameSpaceInvalidationTime, BadNameSpaceInvalidationTime) ACMD_AUTO_SETTING(ResourceDBSettings, RESOURCEDB);


//keep a sorted doubly-linked-list by most-recently-accessed so it's easy to purge old ones.
static ResourceDBNameSpace *spNewest = NULL;
static ResourceDBNameSpace *spOldest = NULL;

static int siNumNameSpaces = 0;

static U64 siNameSpaceMemUsage = 0;

bool gbLoadFromPatchServer = true;

bool gbForceLoadFromPatchServer = false;
AUTO_CMD_INT(gbForceLoadFromPatchServer, ForceLoadFromPatchServer) ACMD_COMMANDLINE;

bool gbForceLoadFromDisk = false;
AUTO_CMD_INT(gbForceLoadFromDisk, ForceLoadFromDisk) ACMD_COMMANDLINE;

bool sbVerboseResourceLogging = false;
AUTO_CMD_INT(sbVerboseResourceLogging, VerboseResourceLogging);

void NSLog(ResourceDBNameSpace *pNameSpace, FORMAT_STR const char *pFmt, ...)
{
	char *pLogString = NULL;

	estrStackCreate(&pLogString);

	if (pNameSpace)
	{
		estrPrintf(&pLogString, "NS %s: ", pNameSpace->pNameSpaceName);
	}

	estrGetVarArgs(&pLogString, pFmt);

	log_printf(LOG_RESOURCEDB, "%s", pLogString);

	estrDestroy(&pLogString);


}

#define NSLogVerbose(pNS, fmt, ...) if (sbVerboseResourceLogging) NSLog(pNS, fmt, __VA_ARGS__)

//call this when you want to flush this namespace out of the cache siBadNameSpaceInvalidationTime seconds from now
void RemoveNSInTheFuture(ResourceDBNameSpace *pNameSpace, FORMAT_STR const char *pWhy, ...);


void MakeNewest(ResourceDBNameSpace *pNameSpace)
{
	if (spNewest == pNameSpace)
	{
		return;
	}

	//first, unlink if linked
	if (pNameSpace->pNewer)
	{
		pNameSpace->pNewer->pOlder = pNameSpace->pOlder;
	}
	if (pNameSpace->pOlder)
	{
		pNameSpace->pOlder->pNewer = pNameSpace->pNewer;
	}
	if (pNameSpace == spOldest)
	{
		spOldest = pNameSpace->pNewer;
	}

	if (!spNewest)
	{
		assert (!spOldest);
		spNewest = spOldest = pNameSpace;
		return;
	}

	spNewest->pNewer = pNameSpace;
	pNameSpace->pOlder = spNewest;
	pNameSpace->pNewer = NULL;
	spNewest = pNameSpace;
}

AUTO_FIXUPFUNC;
TextParserResult fixupResourceDBNameSpace(ResourceDBNameSpace *pNameSpace, enumTextParserFixupType eType, void *pExtraData)
{
	int i;
	NameSpaceAllResourceList emptyList = {0};

	switch (eType)
	{
	case FIXUPTYPE_DESTRUCTOR:
		if (pNameSpace->pInvalidationCallback)
		{
			TimedCallback_Remove(pNameSpace->pInvalidationCallback);
			pNameSpace->pInvalidationCallback = NULL;
		}

		for (i=0; i < eaSize(&pNameSpace->ppNameSpaceReqs); i++)
		{
			SlowRemoteCommandReturn_RequestResourceNames(pNameSpace->ppNameSpaceReqs[i]->iCmdID, &emptyList);
		}
		eaDestroyStruct(&pNameSpace->ppNameSpaceReqs, parse_NameSpaceNamesRequest);

	//NOTE NOTE NOTE these loops should be in sync with the CheckNamespaceAndMaybeAddReferent
	//ones in ProcessNewlyLoadedResourcesForNameSpace
		
	for(i=0; i<eaSize(&pNameSpace->ppZoneMapInfos); ++i) 
	{
		if (!RefSystem_RemoveReferent(pNameSpace->ppZoneMapInfos[i], false))
		{
			AssertOrAlert("COULDNT_REMOVE_REFERENT", 
				"Something went wrong while removing reference to zoneMapInfo %s",
				pNameSpace->ppZoneMapInfos[i]->map_name);
		}
	}
	for(i=0; i<eaSize(&pNameSpace->ppMessages); ++i) 
	{
		if (!RefSystem_RemoveReferent(pNameSpace->ppMessages[i], false))
		{
			AssertOrAlert("COULDNT_REMOVE_REFERENT", 
				"Something went wrong while removing reference to message %s",
				pNameSpace->ppMessages[i]->pcMessageKey);
		}		
	}
	for(i=0; i<eaSize(&pNameSpace->ppMissionDefs); ++i) 
	{
		if (!RefSystem_RemoveReferent(pNameSpace->ppMissionDefs[i], false))
		{
			AssertOrAlert("COULDNT_REMOVE_REFERENT", 
				"Something went wrong while removing reference to mission %s",
				pNameSpace->ppMissionDefs[i]->name);
		}	
	}
	for(i=0; i<eaSize(&pNameSpace->ppContactDefs); ++i) 
	{
		if (!RefSystem_RemoveReferent(pNameSpace->ppContactDefs[i], false))
		{
			AssertOrAlert("COULDNT_REMOVE_REFERENT", 
				"Something went wrong while removing reference to contact %s",
				pNameSpace->ppContactDefs[i]->name);
		}	
	}
	for(i=0; i<eaSize(&pNameSpace->ppPlayerCostumes); ++i) 
	{
		if (!RefSystem_RemoveReferent(pNameSpace->ppPlayerCostumes[i], false))
		{
			AssertOrAlert("COULDNT_REMOVE_REFERENT", 
				"Something went wrong while removing reference to costume %s",
				pNameSpace->ppPlayerCostumes[i]->pcName);
		}		
	}
	for(i=0; i<eaSize(&pNameSpace->ppItemDefs); ++i) 
	{
		if (!RefSystem_RemoveReferent(pNameSpace->ppItemDefs[i], false))
		{
			AssertOrAlert("COULDNT_REMOVE_REFERENT", 
				"Something went wrong while removing reference to item %s",
				pNameSpace->ppItemDefs[i]->pchName);
		}		
	}
	for(i=0; i<eaSize(&pNameSpace->ppRewardTables); ++i)
	{
		if (!RefSystem_RemoveReferent(pNameSpace->ppRewardTables[i], false))
		{
			AssertOrAlert("COULDNT_REMOVE_REFERENT",
				"Something went wrong while removing reference to reward table %s",
				pNameSpace->ppRewardTables[i]->pchName);
		}
	}



		break;
	}

	return 1;

}
AUTO_COMMAND;
void FreeOldest(void)
{
	ResourceDBNameSpace *pPrevOldest;

	if (!spOldest)
	{
		return;
	}

	NSLogVerbose(spOldest, "Freeing oldest");

	pPrevOldest = spOldest;

	if (spOldest->pNewer)
	{
		spOldest->pNewer->pOlder = NULL;
	}

	spOldest = spOldest->pNewer;
	if (!spOldest)
	{
		spNewest = NULL;
	}

	siNameSpaceMemUsage -= pPrevOldest->iMemUsage;
	siNumNameSpaces--;

	stashRemovePointer(sResourceDBNameSpacesByName, pPrevOldest->pNameSpaceName, NULL);

	StructDestroy(parse_ResourceDBNameSpace, pPrevOldest);

	EventCounter_ItHappened(pFreedNSCounter, timeSecondsSince2000());
}

ResourceDBNameSpace *FindOrCreateNameSpace(char *pNameSpaceName)
{
	ResourceDBNameSpace *pNameSpace;
	if (!sResourceDBNameSpacesByName)
	{
		sResourceDBNameSpacesByName = stashTableCreateWithStringKeys(100, StashDefault);
		resRegisterDictionaryForStashTable("ResourceNameSpaces", RESCATEGORY_SYSTEM, 0, sResourceDBNameSpacesByName, parse_ResourceDBNameSpace);

	}

	if (stashFindPointer(sResourceDBNameSpacesByName, pNameSpaceName, &pNameSpace))
	{
		NSLogVerbose(pNameSpace, "Found, making newest");
		pNameSpace->iMostRecentTimeAccessed = timeSecondsSince2000();
	
		MakeNewest(pNameSpace);

		return pNameSpace;
	}

	EventCounter_ItHappened(pCacheMissCounter, timeSecondsSince2000());
	siNumNameSpaces++;
	pNameSpace = StructCreate(parse_ResourceDBNameSpace);
	pNameSpace->pNameSpaceName = strdup(pNameSpaceName);
	pNameSpace->iMostRecentTimeAccessed = pNameSpace->iTimeCreated = timeSecondsSince2000(); 

	MakeNewest(pNameSpace);
	NSLogVerbose(pNameSpace, "Newly created");

	stashAddPointer(sResourceDBNameSpacesByName, pNameSpace->pNameSpaceName, pNameSpace, false);

	LoadNameSpaceData(pNameSpace);

	return pNameSpace;
}


ResourceDBNameSpace *FindNameSpace(char *pNameSpaceName)
{
	ResourceDBNameSpace *pNameSpace;
	if (!sResourceDBNameSpacesByName)
	{
		return NULL;
	}

	if (stashFindPointer(sResourceDBNameSpacesByName, pNameSpaceName, &pNameSpace))
	{
		pNameSpace->iMostRecentTimeAccessed = timeSecondsSince2000();
	
		MakeNewest(pNameSpace);
		NSLogVerbose(pNameSpace, "Found, making newest");

		return pNameSpace;
	}

	return NULL;
}

ResourceDBNameSpace *FindOrCreateNameSpaceFromResourceName(char *pResourceName)
{
	char nameSpaceName[RESOURCE_NAME_MAX_SIZE], baseName[RESOURCE_NAME_MAX_SIZE];


	if (!resExtractNameSpace(pResourceName, nameSpaceName, baseName))
	{
		return NULL;
	}

	return FindOrCreateNameSpace(nameSpaceName);
}
	

void ***GetResourceEarrayFromType(ResourceDBNameSpace *pNameSpace, eResourceDBResourceType eResourceType)
{
	switch (eResourceType)
	{
	case RESOURCETYPE_ZONEMAPINFO:
		return &pNameSpace->ppZoneMapInfos;
	case RESOURCETYPE_MESSAGE:
		return &pNameSpace->ppMessages;
	case RESOURCETYPE_MISSIONDEF:
		return &pNameSpace->ppMissionDefs;
	case RESOURCETYPE_CONTACTDEF:
		return &pNameSpace->ppContactDefs;
	case RESOURCETYPE_PLAYERCOSTUME:
		return &pNameSpace->ppPlayerCostumes;
	case RESOURCETYPE_ITEMDEF:
		return &pNameSpace->ppItemDefs;
	case RESOURCETYPE_REWARDTABLE:
		return &pNameSpace->ppRewardTables;
	default:
		assert(0);
	}
}

const char *GetResourcePrivateNameFromType(eResourceDBResourceType eType, void *pResource)
{
	switch (eType)
	{
	case RESOURCETYPE_ZONEMAPINFO:
		return ((ZoneMapInfo*)pResource)->map_name;
	case RESOURCETYPE_MESSAGE:
		return ((Message*)pResource)->pcMessageKey;
	case RESOURCETYPE_MISSIONDEF:
		return ((MissionDef*)pResource)->name;
	case RESOURCETYPE_CONTACTDEF:
		return ((ContactDef*)pResource)->name;
	case RESOURCETYPE_PLAYERCOSTUME:
		return ((PlayerCostume*)pResource)->pcName;
	case RESOURCETYPE_ITEMDEF:
		return ((ItemDef*)pResource)->pchName;
	case RESOURCETYPE_REWARDTABLE:
		return ((RewardTable*)pResource)->pchName;
	default:
		assert(0);
	}
}

const char *GetResourcePublicNameFromType(eResourceDBResourceType eType, void *pResource)
{
	switch (eType)
	{
	case RESOURCETYPE_ZONEMAPINFO:
		return ((ZoneMapInfo*)pResource)->map_name;
	case RESOURCETYPE_MESSAGE:
		return ((Message*)pResource)->pcMessageKey;
	case RESOURCETYPE_MISSIONDEF:
		return ((MissionDef*)pResource)->name;
	case RESOURCETYPE_CONTACTDEF:
		return ((ContactDef*)pResource)->name;
	case RESOURCETYPE_PLAYERCOSTUME:
		return ((PlayerCostume*)pResource)->pcName;
	case RESOURCETYPE_ITEMDEF:
		return ((ItemDef*)pResource)->pchName;
	case RESOURCETYPE_REWARDTABLE:
		return ((RewardTable*)pResource)->pchName;
	default:
		assert(0);
	}
}

SimpleResourceRef *GetSimpleResourceRefFromType(eResourceDBResourceType eType, void *pResource)
{
	SimpleResourceRef *pOutRef = StructCreate(parse_SimpleResourceRef);
	pOutRef->pPrivateName = strdup(GetResourcePrivateNameFromType(eType, pResource));
	pOutRef->pPublicName = strdup(GetResourcePublicNameFromType(eType, pResource));

	return pOutRef;
}

void SendDependentResources(ResourceDBNameSpace *pNameSpace, GlobalType eServerType, ContainerID iServerID, const char *pcDictName, const char *pcResourceName, char *pComment)
{
	ResourceInfo *pInfo = NULL;

	pInfo = resGetInfo(pcDictName, pcResourceName);
	if (pInfo)
	{
		int i;
		
		for (i = 0; i < eaSize(&pInfo->ppReferences); i++)
		{
			ResourceReference *pRef = pInfo->ppReferences[i];
			if (pRef->referenceType == REFTYPE_CONTAINS) 
			{
				if (pRef->resourceDict == allocAddString("Message"))
				{
					Message *pMessage = RefSystem_ReferentFromString("Message", pRef->resourceName);
					if (pMessage)
					{
						NSLogVerbose(pNameSpace, "Sending dependent Message %s to %s\n", pMessage->pcMessageKey, GlobalTypeAndIDToString(eServerType, iServerID));
						RemoteCommand_ResourceDB_ReceiveMessage(eServerType, iServerID, pRef->resourceName, pMessage, pComment);
					}
				}
				// Else it's an unsupported CONTAINS reference
			}
		}
	}
}

void SendResourceToRequestingServer(ResourceDBNameSpace *pNameSpace, GlobalType eServerType, ContainerID iServerID, eResourceDBResourceType eType, char *pResourceName, void *pResource, char *pComment)
{
	NSLogVerbose(pNameSpace, "Sending %s %s to %s\n", StaticDefineIntRevLookup(eResourceDBResourceTypeEnum, eType), pResourceName, GlobalTypeAndIDToString(eServerType, iServerID));

	switch (eType)
	{
	case RESOURCETYPE_ZONEMAPINFO:
		SendDependentResources(pNameSpace, eServerType, iServerID, "ZoneMapInfo", pResourceName, pComment);
		RemoteCommand_ResourceDB_ReceiveZoneMapInfo(eServerType, iServerID, pResourceName, (ZoneMapInfo*)pResource, pComment);
		break;
	case RESOURCETYPE_MESSAGE:
		RemoteCommand_ResourceDB_ReceiveMessage(eServerType, iServerID, pResourceName, (Message*)pResource, pComment);
		break;
	case RESOURCETYPE_MISSIONDEF:
		SendDependentResources(pNameSpace, eServerType, iServerID, "Mission", pResourceName, pComment);
		RemoteCommand_ResourceDB_ReceiveMissionDef(eServerType, iServerID, pResourceName, (MissionDef*)pResource, pComment);
		break;
	case RESOURCETYPE_CONTACTDEF:
		SendDependentResources(pNameSpace, eServerType, iServerID, "Contact", pResourceName, pComment);
		RemoteCommand_ResourceDB_ReceiveContactDef(eServerType, iServerID, pResourceName, (ContactDef*)pResource, pComment);
		break;		
	case RESOURCETYPE_PLAYERCOSTUME:
		SendDependentResources(pNameSpace, eServerType, iServerID, "PlayerCostume", pResourceName, pComment);
		RemoteCommand_ResourceDB_ReceivePlayerCostume(eServerType, iServerID, pResourceName, (PlayerCostume*)pResource, pComment);
		break;
	case RESOURCETYPE_ITEMDEF:
		SendDependentResources(pNameSpace, eServerType, iServerID, "ItemDef", pResourceName, pComment);
		RemoteCommand_ResourceDB_ReceiveItemDef(eServerType, iServerID, pResourceName, (ItemDef*)pResource, pComment);
		break;
	case RESOURCETYPE_REWARDTABLE:
		SendDependentResources(pNameSpace, eServerType, iServerID, "RewardTable", pResourceName, pComment);
		RemoteCommand_ResourceDB_ReceiveRewardTable(eServerType, iServerID, pResourceName, (RewardTable*)pResource, pComment);
		break;
	}

}



#define PCL_DO_ERROR()																	\
{																						\
	if(error != PCL_SUCCESS)															\
	{																					\
		char *msg = ScratchAlloc(MAX_PATH);												\
		char *details = NULL;															\
		pclGetErrorString(error, msg, MAX_PATH);										\
		pclGetErrorDetails(client, &details);											\
		strcat_s(msg + strlen(msg), MAX_PATH - strlen(msg), ", State: ");				\
		pclGetStateString(client, msg + strlen(msg), MAX_PATH - strlen(msg));			\
		AssertOrAlert("RESOURCEDB_PATCHING_ERROR", "PCL error: %s%s%s. Client: %s",		\
			msg,																		\
			details ? ": " : "",														\
			details,																	\
			pclGetUsefulDebugString_Static(client));									\
		ScratchFree(msg);																\
		/*eaFindAndRemoveFast(&gpClients, client);	*/									\
		/*pclDisconnectAndDestroy(client);		*/										\
		return;																			\
	}																					\
}

#define PCL_DO(fn)	\
{					\
	error = fn;		\
	PCL_DO_ERROR();	\
}

typedef struct PatchingUserdata
{
	DynamicPatchInfo *pPatchInfo;
	char *pNameSpaceName;
} PatchingUserdata;

AUTO_STRUCT;
typedef struct WrappedPCLClient
{
	char *pNameSpaceName;
	U32 iBeganTime;
	PCL_Client *pClient; NO_AST
	bool bAlreadyAlerted;
} WrappedPCLClient;

WrappedPCLClient **gpWrappedClients = NULL;





static void patchGetCB(PCL_Client *client, PCL_ErrorCode error, const char *error_details, PatchingUserdata *userdata)
{
	ResourceDBNameSpace *pNameSpace = NULL;

	PCL_DO_ERROR();
	
	if (sResourceDBNameSpacesByName && stashFindPointer(sResourceDBNameSpacesByName, userdata->pNameSpaceName, &pNameSpace))
	{
		ProcessNewlyLoadedResourcesForNameSpace(client, pNameSpace);


	}	
//	eaFindAndRemoveFast(&gpClients, client);
//	pclDisconnectAndDestroy(client);
}

static void patchViewCB(PCL_Client *client, PCL_ErrorCode error, const char *error_details, PatchingUserdata *userdata)
{
	PCL_DO_ERROR();
	PCL_DO(pclGetAllFiles(client, patchGetCB, userdata, NULL));
}

static void patchConnectCB(PCL_Client *client, bool updated, PCL_ErrorCode error, const char *error_details, PatchingUserdata *userdata)
{
	PCL_DO_ERROR();

	pclAddFileFlags(client, PCL_METADATA_IN_MEMORY);

	if(userdata->pPatchInfo->pPrefix)
		pclSetPrefix(client, userdata->pPatchInfo->pPrefix);

	if(userdata->pPatchInfo->pViewName)
	{
		PCL_DO(pclSetNamedView(client, userdata->pPatchInfo->pResourceProject, userdata->pPatchInfo->pViewName, true, false, patchViewCB, userdata));
	}
	else
	{
		PCL_DO(pclSetViewLatest(client, userdata->pPatchInfo->pResourceProject, userdata->pPatchInfo->iBranch, NULL, true, false, patchViewCB, userdata));
	}
}


AUTO_STRUCT;
typedef struct ListOfZoneMapInfos
{
	ZoneMapInfo **ppZoneMapInfos; AST(NAME(ZoneMap))
} ListOfZoneMapInfos;

AUTO_STRUCT;
typedef struct ListOfMessages
{
	Message **ppMessages; AST(NAME(Message))
} ListOfMessages;

AUTO_STRUCT;
typedef struct ListOfMissionDefs
{
	MissionDef **ppMissionDefs; AST(NAME(Mission))
} ListOfMissionDefs;

AUTO_STRUCT;
typedef struct ListOfContactDefs
{
	ContactDef **ppContactDefs; AST(NAME(Contact))
} ListOfContactDefs;

AUTO_STRUCT;
typedef struct ListOfPlayerCostumes
{
	PlayerCostume **ppPlayerCostumes; AST(NAME(PlayerCostume))
} ListOfPlayerCostumes;

AUTO_STRUCT;
typedef struct ListOfItemDefs
{
	ItemDef **ppItemDefs; AST(NAME(ItemDef))
} ListOfItemDefs;

AUTO_STRUCT;
typedef struct ListOfRewardTables
{
	RewardTable **ppRewardTables; AST(NAME(RewardTable))
} ListOfRewardTables;


AUTO_STRUCT;
typedef struct MaskAndTPIAndStruct
{
	ParseTable *pTPI; NO_AST
	void *pStruct; NO_AST
	char *pFileMask; AST(UNOWNED)
} MaskAndTPIAndStruct;

AUTO_STRUCT;
typedef struct InMemoryLoadFilesHandle
{
	char *pNameSpaceName; AST(UNOWNED)
	bool bReadOneFile;
	bool bErrors;

	MaskAndTPIAndStruct **ppMasks;

} InMemoryLoadFilesHandle;


void InMemoryTextParsingIterator(void* client, const char *filename, const char *data, U32 size, U32 modtime, InMemoryLoadFilesHandle *pHandle)
{
	TextParserResult parseResult = PARSERESULT_SUCCESS;
	int iMaskNum;
	
	for (iMaskNum = 0; iMaskNum < eaSize(&pHandle->ppMasks); iMaskNum++)
	{
		MaskAndTPIAndStruct *pMask = pHandle->ppMasks[iMaskNum];
		if (strEndsWith(filename, pMask->pFileMask) && size)
		{
			char *pDupData = malloc(size + 1);
			TokenizerHandle tok;

			//need to dup data to add NULL terminator, also because tokenizing will trash source data
			memcpy(pDupData, data, size);
			pDupData[size] = 0;
			
			tok = TokenizerCreateLoadedFile(pDupData, filename, NULL, true);

			if (!tok)
			{
				AssertOrAlert("EMPTY_RESDB_FILE", "In namespace %s, Resource DB couldn't tokenizer file %s because it was empty. This is likely an error", pHandle->pNameSpaceName, filename);
			}
			else
			{
				pHandle->bReadOneFile = true;
				ParserReadTokenizer(tok, pMask->pTPI, pMask->pStruct, true, &parseResult);
				TokenizerDestroy(tok);

				if (parseResult != PARSERESULT_SUCCESS)
				{
					pHandle->bErrors = true;
				}
			}
		}
	}
}


void CheckNamespaceAndMaybeAddReferent(const char *pNameSpaceName, const char *pDictName, const char *pRefName, void *pRefData)
{
	char ns[RESOURCE_NAME_MAX_SIZE], base[RESOURCE_NAME_MAX_SIZE];
	if (!resExtractNameSpace(pRefName, ns, base) || stricmp(ns, pNameSpaceName) != 0)
	{
		AssertOrAlert("BAD_NAMESPACE_RD_REF", "Resource DB got stuff in namespace %s from patcher. It included a %s named %s. But that is not in our namespace!",
			pNameSpaceName, pDictName, pRefName);
	}
	else
	{
		RefSystem_AddReferent(pDictName, pRefName, pRefData);
	}
}


void ProcessNewlyLoadedResourcesForNameSpace(PCL_Client *client, ResourceDBNameSpace *pNameSpace)
{
	ListOfZoneMapInfos *pListOfZoneMapInfos = StructCreate(parse_ListOfZoneMapInfos);
	ListOfMessages *pListOfMessages = StructCreate(parse_ListOfMessages);
	ListOfMissionDefs *pListOfMissionDefs = StructCreate(parse_ListOfMissionDefs);
	ListOfContactDefs *pListOfContactDefs = StructCreate(parse_ListOfContactDefs);
	ListOfPlayerCostumes *pListOfPlayerCostumes = StructCreate(parse_ListOfPlayerCostumes);
	ListOfItemDefs *pListOfItemDefs = StructCreate(parse_ListOfItemDefs);
	ListOfRewardTables *pListOfRewardTables = StructCreate(parse_ListOfRewardTables);
	PCL_ErrorCode error;
	char *pParseError = NULL;
	int i;

	InMemoryLoadFilesHandle *pLoadHandle = StructCreate(parse_InMemoryLoadFilesHandle);

	MaskAndTPIAndStruct *pMask;


	pLoadHandle->pNameSpaceName = pNameSpace->pNameSpaceName;
	
	pMask = StructCreate(parse_MaskAndTPIAndStruct);
	pMask->pFileMask = ".zone";
	pMask->pStruct = pListOfZoneMapInfos;
	pMask->pTPI = parse_ListOfZoneMapInfos;
	eaPush(&pLoadHandle->ppMasks, pMask);

	pMask = StructCreate(parse_MaskAndTPIAndStruct);
	pMask->pFileMask = ".ms";
	pMask->pStruct = pListOfMessages;
	pMask->pTPI = parse_ListOfMessages;
	eaPush(&pLoadHandle->ppMasks, pMask);

	pMask = StructCreate(parse_MaskAndTPIAndStruct);
	pMask->pFileMask = ".mission";
	pMask->pStruct = pListOfMissionDefs;
	pMask->pTPI = parse_ListOfMissionDefs;
	eaPush(&pLoadHandle->ppMasks, pMask);

	pMask = StructCreate(parse_MaskAndTPIAndStruct);
	pMask->pFileMask = ".contact";
	pMask->pStruct = pListOfContactDefs;
	pMask->pTPI = parse_ListOfContactDefs;
	eaPush(&pLoadHandle->ppMasks, pMask);

	pMask = StructCreate(parse_MaskAndTPIAndStruct);
	pMask->pFileMask = ".costume";
	pMask->pStruct = pListOfPlayerCostumes;
	pMask->pTPI = parse_ListOfPlayerCostumes;
	eaPush(&pLoadHandle->ppMasks, pMask);

	pMask = StructCreate(parse_MaskAndTPIAndStruct);
	pMask->pFileMask = ".item";
	pMask->pStruct = pListOfItemDefs;
	pMask->pTPI = parse_ListOfItemDefs;
	eaPush(&pLoadHandle->ppMasks, pMask);

	pMask = StructCreate(parse_MaskAndTPIAndStruct);
	pMask->pFileMask = ".rewards";
	pMask->pStruct = pListOfRewardTables;
	pMask->pTPI = parse_ListOfRewardTables;
	eaPush(&pLoadHandle->ppMasks, pMask);

	ErrorfPushCallback(EstringErrorCallback, (void*)(&pParseError));
	PCL_DO(pclForEachInMemory(client, InMemoryTextParsingIterator, pLoadHandle));
	ErrorfPopCallback();

	//NOTE NOTE NOTE make sure that for each list of calls to CheckNamespaceAndMaybeAddReferent
	//you also have a corresponding set of RefSystem_RemoveReferent calls in
	//the AUTO_FIXUP for ResourceDBNameSpace
	//these post-read steps are normally done by ParserLoadFiles, which of course we never actually called.
	StructSortIndexedArrays(parse_ListOfZoneMapInfos, pListOfZoneMapInfos);
	FixupStructLeafFirst(parse_ListOfZoneMapInfos, pListOfZoneMapInfos, FIXUPTYPE_POST_TEXT_READ, NULL);
	for(i=0; i<eaSize(&pListOfZoneMapInfos->ppZoneMapInfos); ++i) {
		ZoneMapInfo *pInfo = pListOfZoneMapInfos->ppZoneMapInfos[i];
		CheckNamespaceAndMaybeAddReferent(pNameSpace->pNameSpaceName, "ZoneMapInfo", pInfo->map_name, pInfo);
	}

	StructSortIndexedArrays(parse_ListOfMessages, pListOfMessages);
	FixupStructLeafFirst(parse_ListOfMessages, pListOfMessages, FIXUPTYPE_POST_TEXT_READ, NULL);
	for(i=0; i<eaSize(&pListOfMessages->ppMessages); ++i) {
		Message *pMsg = pListOfMessages->ppMessages[i];
		CheckNamespaceAndMaybeAddReferent(pNameSpace->pNameSpaceName, "Message", pMsg->pcMessageKey, pMsg);
	}

	StructSortIndexedArrays(parse_ListOfMissionDefs, pListOfMissionDefs);
	FixupStructLeafFirst(parse_ListOfMissionDefs, pListOfMissionDefs, FIXUPTYPE_POST_TEXT_READ, NULL);
	for(i=0; i<eaSize(&pListOfMissionDefs->ppMissionDefs); ++i) {
		MissionDef *pDef = pListOfMissionDefs->ppMissionDefs[i];
		CheckNamespaceAndMaybeAddReferent(pNameSpace->pNameSpaceName, "Mission", pDef->name, pDef);
	}

	StructSortIndexedArrays(parse_ListOfContactDefs, pListOfContactDefs);
	FixupStructLeafFirst(parse_ListOfContactDefs, pListOfContactDefs, FIXUPTYPE_POST_TEXT_READ, NULL);
	for(i=0; i<eaSize(&pListOfContactDefs->ppContactDefs); ++i) {
		ContactDef *pDef = pListOfContactDefs->ppContactDefs[i];
		CheckNamespaceAndMaybeAddReferent(pNameSpace->pNameSpaceName, "Contact", pDef->name, pDef);
	}

	StructSortIndexedArrays(parse_ListOfPlayerCostumes, pListOfPlayerCostumes);
	FixupStructLeafFirst(parse_ListOfPlayerCostumes, pListOfPlayerCostumes, FIXUPTYPE_POST_TEXT_READ, NULL);
	for(i=0; i<eaSize(&pListOfPlayerCostumes->ppPlayerCostumes); ++i) {
		PlayerCostume *pCostume = pListOfPlayerCostumes->ppPlayerCostumes[i];
		CheckNamespaceAndMaybeAddReferent(pNameSpace->pNameSpaceName, "PlayerCostume", pCostume->pcName, pCostume);
	}

	StructSortIndexedArrays(parse_ListOfItemDefs, pListOfItemDefs);
	FixupStructLeafFirst(parse_ListOfItemDefs, pListOfItemDefs, FIXUPTYPE_POST_TEXT_READ, NULL);
	for(i=0; i<eaSize(&pListOfItemDefs->ppItemDefs); ++i) {
		ItemDef *pItemDef = pListOfItemDefs->ppItemDefs[i];
		CheckNamespaceAndMaybeAddReferent(pNameSpace->pNameSpaceName, "ItemDef", pItemDef->pchName, pItemDef);
	}

	StructSortIndexedArrays(parse_ListOfRewardTables, pListOfRewardTables);
	FixupStructLeafFirst(parse_ListOfRewardTables, pListOfRewardTables, FIXUPTYPE_POST_TEXT_READ, NULL);
	for(i=0; i<eaSize(&pListOfRewardTables->ppRewardTables); ++i) {
		RewardTable *pRewardTable = pListOfRewardTables->ppRewardTables[i];
		CheckNamespaceAndMaybeAddReferent(pNameSpace->pNameSpaceName, "RewardTable", pRewardTable->pchName, pRewardTable);
	}


	eaCopy(&pNameSpace->ppZoneMapInfos, &pListOfZoneMapInfos->ppZoneMapInfos);
	eaDestroy(&pListOfZoneMapInfos->ppZoneMapInfos);
	StructDestroy(parse_ListOfZoneMapInfos, pListOfZoneMapInfos);

	eaCopy(&pNameSpace->ppMessages, &pListOfMessages->ppMessages);
	eaDestroy(&pListOfMessages->ppMessages);
	StructDestroy(parse_ListOfMessages, pListOfMessages);

	eaCopy(&pNameSpace->ppMissionDefs, &pListOfMissionDefs->ppMissionDefs);
	eaDestroy(&pListOfMissionDefs->ppMissionDefs);
	StructDestroy(parse_ListOfMissionDefs, pListOfMissionDefs);

	eaCopy(&pNameSpace->ppContactDefs, &pListOfContactDefs->ppContactDefs);
	eaDestroy(&pListOfContactDefs->ppContactDefs);
	StructDestroy(parse_ListOfContactDefs, pListOfContactDefs);

	eaCopy(&pNameSpace->ppPlayerCostumes, &pListOfPlayerCostumes->ppPlayerCostumes);
	eaDestroy(&pListOfPlayerCostumes->ppPlayerCostumes);
	StructDestroy(parse_ListOfPlayerCostumes, pListOfPlayerCostumes);

	eaCopy(&pNameSpace->ppItemDefs, &pListOfItemDefs->ppItemDefs);
	eaDestroy(&pListOfItemDefs->ppItemDefs);
	StructDestroy(parse_ListOfItemDefs, pListOfItemDefs);

	eaCopy(&pNameSpace->ppRewardTables, &pListOfRewardTables->ppRewardTables);
	eaDestroy(&pListOfRewardTables->ppRewardTables);
	StructDestroy(parse_ListOfRewardTables, pListOfRewardTables);

	pNameSpace->iMemUsage = (U32)StructGetMemoryUsage(parse_ResourceDBNameSpace, pNameSpace, true);
	siNameSpaceMemUsage += pNameSpace->iMemUsage;

	if (pLoadHandle->bErrors)
	{
		ErrorOrAlert("RESDB_LOAD_TP_ERROR", "%s", pParseError);
	}

	StructDestroy(parse_InMemoryLoadFilesHandle, pLoadHandle);

	pNameSpace->bLoaded = true;
	ResourceDB_NameSpaceIsLoaded(pNameSpace);

	estrDestroy(&pParseError);
}

static int siMaxPCLsAtOnce = 16;
AUTO_CMD_INT(siMaxPCLsAtOnce, MaxPCLsAtOnce);

AUTO_STRUCT;
typedef struct WaitingNameSpace
{
	char *pNameSpaceName;
	U32 iWaitStartTime;
} WaitingNameSpace;

WaitingNameSpace **sppWaitingNameSpaces = NULL;

static int siNameSpaceWaitAlertTime = 120;
//alert when a namespace has been waiting more than this many seconds
AUTO_CMD_INT(siNameSpaceWaitAlertTime, NameSpaceWaitAlertTime) ACMD_COMMANDLINE;

static int siNameSpaceWaitAlertThrottle = 600;
//only generate waiting namespace alert every n seconds
AUTO_CMD_INT(siNameSpaceWaitAlertThrottle, NameSpaceWaitAlertThrottle) ACMD_COMMANDLINE;


void LoadNameSpaceData(ResourceDBNameSpace *pNameSpace)
{
	NSLogVerbose(pNameSpace, "LoadNameSpaceData");

	if (gbLoadFromPatchServer)
	{
		PatchingUserdata *pUserData;
		PCL_Client *client = NULL;
		PCL_ErrorCode error;
		WrappedPCLClient *pWrappedClient;

		if (eaSize(&gpWrappedClients) >= siMaxPCLsAtOnce)
		{
			WaitingNameSpace *pWaitingNS = StructCreate(parse_WaitingNameSpace);
		
			NSLogVerbose(pNameSpace, "Adding to Waiting NS list");
			
			pWaitingNS->pNameSpaceName = strdup(pNameSpace->pNameSpaceName);
			pWaitingNS->iWaitStartTime = timeSecondsSince2000();
			eaPush(&sppWaitingNameSpaces, pWaitingNS);
			EventCounter_ItHappened(pQueueCount, timeSecondsSince2000());

			return;
		}

		pNameSpace->iTimePatchingBegan = timeSecondsSince2000();

		//this namespaces did not have to wait at all
		IntAverager_AddDatapoint(pTotalPatchWaitAverager, 0);


		pUserData = calloc(1, sizeof(PatchingUserdata));
		pUserData->pNameSpaceName = strdup(pNameSpace->pNameSpaceName);
		pUserData->pPatchInfo = StructCreate(parse_DynamicPatchInfo);

		//PATCHINFO_FOR_UGC_PLAYING means that we want to get from the "getting" patchserver, not the "putting" patchserver
		//
		//ignored by non-UGC resources
		if (!aslFillInPatchInfo(pUserData->pPatchInfo, pNameSpace->pNameSpaceName, PATCHINFO_FOR_UGC_PLAYING))
		{
			AssertOrAlert("BAD_NAMESPACE", "Even though we checked immediately upon receiving this request, aslFillInPatchInfofailed. Memory corruption/");
		}

		// Could do some caching of active connections or something
		PCL_DO(pclConnectAndCreate(&client, pUserData->pPatchInfo->pServer, pUserData->pPatchInfo->iPort, 60, commDefault(), "", "ResourceDB", "", patchConnectCB, pUserData));
		pclAddFileFlags(client, PCL_IN_MEMORY);
		pclSetBadFilesDirectory(client, fileTempDir());

		pWrappedClient = StructCreate(parse_WrappedPCLClient);
		pWrappedClient->pClient = client;
		pWrappedClient->pNameSpaceName = strdup(pNameSpace->pNameSpaceName);
		pWrappedClient->iBeganTime = timeSecondsSince2000();

		eaPush(&gpWrappedClients, pWrappedClient);

		NSLogVerbose(pNameSpace, "Pathching begun");
	}
	else
	{
	

		char rootDirName[CRYPTIC_MAX_PATH];
		ListOfZoneMapInfos *pListOfZoneMapInfos = StructCreate(parse_ListOfZoneMapInfos);
		ListOfMessages *pListOfMessages = StructCreate(parse_ListOfMessages);
		ListOfMissionDefs *pListOfMissionDefs = StructCreate(parse_ListOfMissionDefs);
		ListOfContactDefs *pListOfContactDefs = StructCreate(parse_ListOfContactDefs);
		ListOfPlayerCostumes *pListOfPlayerCostumes = StructCreate(parse_ListOfPlayerCostumes);
		ListOfItemDefs *pListOfItemDefs = StructCreate(parse_ListOfItemDefs);
		ListOfRewardTables *pListOfRewardTables = StructCreate(parse_ListOfRewardTables);

	
		sprintf(rootDirName, "%s/ns/%s", fileDataDir(), pNameSpace->pNameSpaceName);
		

		ParserLoadFiles(rootDirName, ".zone", NULL, 0, parse_ListOfZoneMapInfos, pListOfZoneMapInfos);

		eaCopy(&pNameSpace->ppZoneMapInfos, &pListOfZoneMapInfos->ppZoneMapInfos);
		eaDestroy(&pListOfZoneMapInfos->ppZoneMapInfos);
		StructDestroy(parse_ListOfZoneMapInfos, pListOfZoneMapInfos);

		ParserLoadFiles(rootDirName, ".ms", NULL, 0, parse_ListOfMessages, pListOfMessages);

		eaCopy(&pNameSpace->ppMessages, &pListOfMessages->ppMessages);
		eaDestroy(&pListOfMessages->ppMessages);
		StructDestroy(parse_ListOfMessages, pListOfMessages);

		ParserLoadFiles(rootDirName, ".mission", NULL, 0, parse_ListOfMissionDefs, pListOfMissionDefs);

		eaCopy(&pNameSpace->ppMissionDefs, &pListOfMissionDefs->ppMissionDefs);
		eaDestroy(&pListOfMissionDefs->ppMissionDefs);
		StructDestroy(parse_ListOfMissionDefs, pListOfMissionDefs);

		ParserLoadFiles(rootDirName, ".contact", NULL, 0, parse_ListOfContactDefs, pListOfContactDefs);

		eaCopy(&pNameSpace->ppContactDefs, &pListOfContactDefs->ppContactDefs);
		eaDestroy(&pListOfContactDefs->ppContactDefs);
		StructDestroy(parse_ListOfContactDefs, pListOfContactDefs);

		ParserLoadFiles(rootDirName, ".costume", NULL, 0, parse_ListOfPlayerCostumes, pListOfPlayerCostumes);

		eaCopy(&pNameSpace->ppPlayerCostumes, &pListOfPlayerCostumes->ppPlayerCostumes);
		eaDestroy(&pListOfPlayerCostumes->ppPlayerCostumes);
		StructDestroy(parse_ListOfPlayerCostumes, pListOfPlayerCostumes);

		ParserLoadFiles(rootDirName, ".item", NULL, 0, parse_ListOfItemDefs, pListOfItemDefs);

		eaCopy(&pNameSpace->ppItemDefs, &pListOfItemDefs->ppItemDefs);
		eaDestroy(&pListOfItemDefs->ppItemDefs);
		StructDestroy(parse_ListOfItemDefs, pListOfItemDefs);

		ParserLoadFiles(rootDirName, ".rewards", NULL, 0, parse_ListOfRewardTables, pListOfRewardTables);

		eaCopy(&pNameSpace->ppRewardTables, &pListOfRewardTables->ppRewardTables);
		eaDestroy(&pListOfRewardTables->ppRewardTables);
		StructDestroy(parse_ListOfRewardTables, pListOfRewardTables);

		pNameSpace->iMemUsage = (U32)StructGetMemoryUsage(parse_ResourceDBNameSpace, pNameSpace, true);

		siNameSpaceMemUsage += pNameSpace->iMemUsage;	

		pNameSpace->bLoaded = true;
		ResourceDB_NameSpaceIsLoaded(pNameSpace);
	}
}

void ResourceDB_NameSpaceIsLoaded(ResourceDBNameSpace *pNameSpace)
{
	int i;

	NSLogVerbose(pNameSpace, "Now loaded, will fill all requests");

	pNameSpace->iTimePatchingCompleted = timeSecondsSince2000();


	for (i=0; i < eaSize(&pNameSpace->ppRequests); i++)
	{
		ResourceDBRequest *pRequest = pNameSpace->ppRequests[i];

		void *pResource = eaIndexedGetUsingString(GetResourceEarrayFromType(pNameSpace, pRequest->eResourceType), pRequest->pResourceName);

		if (!pResource)
		{
			RemoveNSInTheFuture(pNameSpace, "Someone requested resource %s of type %s. This namespace was just loaded, but the resource doesn't seem to exist",
				pRequest->pResourceName, StaticDefineIntRevLookup(eResourceDBResourceTypeEnum, pRequest->eResourceType));
			ErrorOrAlert("RESOURCE_NOT_FOUND", "Someone requested resource %s of type %s. This namespace was just loaded, but the resource doesn't seem to exist",
				pRequest->pResourceName, StaticDefineIntRevLookup(eResourceDBResourceTypeEnum, pRequest->eResourceType));
			SendResourceToRequestingServer(pNameSpace, pRequest->eRequestingServerType, pRequest->iRequestingServerId, 
				pRequest->eResourceType, pRequest->pResourceName, NULL, "resource does not exist");
		}
		else
		{
			SendResourceToRequestingServer(pNameSpace, pRequest->eRequestingServerType, pRequest->iRequestingServerId, 
				pRequest->eResourceType, pRequest->pResourceName, pResource, NULL);
		}
	}
	eaDestroyStruct(&pNameSpace->ppRequests, parse_ResourceDBRequest);

	if (eaSize(&pNameSpace->ppNameSpaceReqs))
	{
		for (i=0; i < eaSize(&pNameSpace->ppNameSpaceReqs); i++)
		{
			NameSpaceAllResourceList *pRetVal = StructCreate(parse_NameSpaceAllResourceList);
			FillResourceList(pRetVal, pNameSpace, pNameSpace->ppNameSpaceReqs[i]);
			SlowRemoteCommandReturn_RequestResourceNames(pNameSpace->ppNameSpaceReqs[i]->iCmdID, pRetVal);
			StructDestroy(parse_NameSpaceAllResourceList, pRetVal);

		}

		eaDestroyStruct(&pNameSpace->ppNameSpaceReqs, parse_NameSpaceNamesRequest);
	}
}

int ResourceDBOncePerFrame(F32 fElapsed)
{
	PCL_ErrorCode error;

	while (siNameSpaceMemUsage > ((U64)sMegsRAMBeforeReleasingNameSpaces) * (1024LL * 1024LL) || (spOldest && sbFreeAllNamespaces))
	{
		assert(spOldest);
		FreeOldest();
	}

	sbFreeAllNamespaces = false;
	
	FOR_EACH_IN_EARRAY(gpWrappedClients, WrappedPCLClient, pWrappedClient)
	{
		PCL_Client *client = pWrappedClient->pClient;

		error = pclProcess(client);
		if(error != PCL_WAITING && error != PCL_SUCCESS)
		{
			char *msg = ScratchAlloc(MAX_PATH);	
			ResourceDBNameSpace *pNS;
			
			pclGetErrorString(error, msg, MAX_PATH);
			strcat_s(msg + strlen(msg), MAX_PATH - strlen(msg), ", State: ");
			pclGetStateString(client, msg + strlen(msg), MAX_PATH - strlen(msg));
			AssertOrAlert("RESOURCEDB_PATCHING_ERROR", "PCL error: %s. Client: %s", msg, pclGetUsefulDebugString_Static(client));
			
			if (stashFindPointer(sResourceDBNameSpacesByName, pWrappedClient->pNameSpaceName, &pNS))
			{
				RemoveNSInTheFuture(pNS, "PCL error: %s. Client: %s", msg, pclGetUsefulDebugString_Static(client));
			}
			
			ScratchFree(msg);

			iNumPatchFailures++;

		

			eaFindAndRemoveFast(&gpWrappedClients, pWrappedClient);
			pclDisconnectAndDestroy(client);
			StructDestroy(parse_WrappedPCLClient, pWrappedClient);
		}
		else if (error == PCL_SUCCESS)
		{
			IntAverager_AddDatapoint(pPatchTimeAverager, timeSecondsSince2000() - pWrappedClient->iBeganTime);
			FloatMaxTracker_AddDataPoint(pMaxPatchTimeTracker, timeSecondsSince2000() - pWrappedClient->iBeganTime, timeSecondsSince2000());
			eaFindAndRemoveFast(&gpWrappedClients, pWrappedClient);
			pclDisconnectAndDestroy(client);
			StructDestroy(parse_WrappedPCLClient, pWrappedClient);
		}
		else if (pWrappedClient->iBeganTime < timeSecondsSince2000() - siNamespacePatchAlertTime && !pWrappedClient->bAlreadyAlerted)
		{
			WARNING_NETOPS_ALERT("SLOW_NS_PATCH", "Namespace %s has been patching for more than %d seconds on the resource DB. PCLClient %s",
				pWrappedClient->pNameSpaceName, siNamespacePatchAlertTime, pclGetUsefulDebugString_Static(pWrappedClient->pClient));

			pWrappedClient->bAlreadyAlerted = true;
		}
	}
	FOR_EACH_END

	while (eaSize(&gpWrappedClients) < siMaxPCLsAtOnce && eaSize(&sppWaitingNameSpaces))
	{
		WaitingNameSpace *pWaitingNS = eaRemove(&sppWaitingNameSpaces, 0);
		ResourceDBNameSpace *pNameSpace;
		int iWaitTime = timeSecondsSince2000() - pWaitingNS->iWaitStartTime;

		IntAverager_AddDatapoint(pTotalPatchWaitAverager, iWaitTime);
		IntAverager_AddDatapoint(pQueuedPatchWaitAverager, iWaitTime);

		if (stashFindPointer(sResourceDBNameSpacesByName, pWaitingNS->pNameSpaceName, &pNameSpace))
		{
			NSLogVerbose(pNameSpace, "Removed from waiting NS list, patching should begin");
			LoadNameSpaceData(pNameSpace);
		}
		StructDestroy(parse_WaitingNameSpace, pWaitingNS);
	}

	return 1;
}

void ResourceDB_PeriodicUpdate(TimedCallback *callback, F32 timeSinceLastCallback, UserData userData)
{
	static U32 iLastAlertTime = 0;

	if (iLastAlertTime > timeSecondsSince2000() - siNameSpaceWaitAlertThrottle)
	{
		return;
	}

	//before we start generating alerts or anything, make sure these all still exist
	FOR_EACH_IN_EARRAY(sppWaitingNameSpaces, WaitingNameSpace, pWaitingNS)
	{
		if (!stashFindPointer(sResourceDBNameSpacesByName, pWaitingNS->pNameSpaceName, NULL))
		{
			eaFindAndRemoveFast(&sppWaitingNameSpaces, pWaitingNS);
			StructDestroy(parse_WaitingNameSpace, pWaitingNS);
		}
	}
	FOR_EACH_END;

	FOR_EACH_IN_EARRAY(sppWaitingNameSpaces, WaitingNameSpace, pWaitingNS)
	{
		if (pWaitingNS->iWaitStartTime < timeSecondsSince2000() - siNameSpaceWaitAlertTime)
		{
			char *pFullAlertString = 0;
			estrPrintf(&pFullAlertString, "One or more namespaces have been waiting to patch for longer than %d seconds\n(Note: this alert will not be generated again for %d seconds)\n",
				siNameSpaceWaitAlertTime, siNameSpaceWaitAlertThrottle);

			FOR_EACH_IN_EARRAY(sppWaitingNameSpaces, WaitingNameSpace, pInnerWaitingNS)
			{
				if (pInnerWaitingNS->iWaitStartTime < timeSecondsSince2000() - siNameSpaceWaitAlertTime)
				{		
					estrConcatf(&pFullAlertString, "%s: %d seconds\n", pInnerWaitingNS->pNameSpaceName, timeSecondsSince2000() - pInnerWaitingNS->iWaitStartTime);
				}
			}
			FOR_EACH_END

			estrConcatf(&pFullAlertString, "%d namespaces are currently patching (max should be %d):\n",
				eaSize(&gpWrappedClients), siMaxPCLsAtOnce);

			FOR_EACH_IN_EARRAY(gpWrappedClients, WrappedPCLClient, pWrappedClient)
			{
				estrConcatf(&pFullAlertString, "%s has been patching for %d seconds: %s\n",
					pWrappedClient->pNameSpaceName, timeSecondsSince2000() - pWrappedClient->iBeganTime, pclGetUsefulDebugString_Static(pWrappedClient->pClient));
			}
			FOR_EACH_END

			CRITICAL_NETOPS_ALERT("RES_DB_STALLED", "%s", pFullAlertString);

			iLastAlertTime = timeSecondsSince2000();
			estrDestroy(&pFullAlertString);
			return;
		}
	}
	FOR_EACH_END


}


int ResourceDBInit(void)
{
	pPatchTimeAverager = IntAverager_Create(AVERAGE_DAY);
	pTotalPatchWaitAverager = IntAverager_Create(AVERAGE_DAY);
	pQueuedPatchWaitAverager = IntAverager_Create(AVERAGE_DAY);

	pMaxPatchTimeTracker = FloatMaxTracker_Create();

	pRequestCounter = EventCounter_Create(timeSecondsSince2000());
	pCacheMissCounter = EventCounter_Create(timeSecondsSince2000());
	pCacheSemiMissCounter = EventCounter_Create(timeSecondsSince2000());
	pFreedNSCounter = EventCounter_Create(timeSecondsSince2000());
	pQueueCount = EventCounter_Create(timeSecondsSince2000());

	if (gbForceLoadFromPatchServer)
	{
		gbLoadFromPatchServer = true;
	}
	else if (gbForceLoadFromDisk)
	{
		gbLoadFromPatchServer = false;
	}


	objLoadAllGenericSchemas();
		
	loadstart_printf("Running Auto Startup...");
	AutoStartup_SetTaskIsOn("ResourceDB", 1);
	DoAutoStartup();
	loadend_printf(" done.");

	resFinishLoading();

	assertmsg(GetAppGlobalType() == GLOBALTYPE_RESOURCEDB, "Resource DB type not set");

	loadstart_printf("Connecting ResourceDB to TransactionServer (%s)... ", gServerLibState.transactionServerHost);

	while (!InitObjectTransactionManager(
		GetAppGlobalType(),
		gServerLibState.containerID,
		gServerLibState.transactionServerHost,
		gServerLibState.transactionServerPort,
		gServerLibState.bUseMultiplexerForTransactions, NULL))
	{
		Sleep(1000);
	}
	if (!objLocalManager())
	{
		loadend_printf("failed.");
		return 0;
	}

	loadend_printf("connected.");

	gAppServer->oncePerFrame = ResourceDBOncePerFrame;


	RemoteCommand_InformControllerOfServerState(GLOBALTYPE_CONTROLLER, 0, GetAppGlobalType(), gServerLibState.containerID, "ready");

	TimedCallback_Add(ResourceDB_PeriodicUpdate, NULL, 5.0f);

	SetRefSystemSuppressUnknownDicitonaryWarning_CertainDictionaries("SkyInfo");

	return 1;
}


void FillResourceList(NameSpaceAllResourceList *pNameList, ResourceDBNameSpace *pNameSpace, NameSpaceNamesRequest *pReq)
{
	eResourceDBResourceType eResourceType;
	pNameList->pNameSpaceName = strdup(pNameSpace->pNameSpaceName);
	for (eResourceType=0; eResourceType < RESOURCETYPE_LAST; eResourceType++)
	{
		if (RESOURCE_DB_TYPE_VALID(eResourceType))
		{
			if (!pReq || ea32Find(&pReq->piRequestedTypes, eResourceType) != -1)
			{
				void ***pppEarray = GetResourceEarrayFromType(pNameSpace, eResourceType);
				if (eaSize(pppEarray))
				{
					int i;
					NameSpaceResourceList *pList = StructCreate(parse_NameSpaceResourceList);
					eaPush(&pNameList->ppLists, pList);

					pList->eType = eResourceType;

					for (i=0; i < eaSize(pppEarray); i++)
					{
						eaPush(&pList->ppRefs, GetSimpleResourceRefFromType(eResourceType, (*pppEarray)[i]));
					}
				}
			}
		}
	}
}

bool CheckIfNameSpaceNeedsVerification(char *pNameSpaceName)
{

	return namespaceIsUGC(pNameSpaceName);
}

static void VerificationCB(TransactionReturnVal *returnVal, ResourceDBRequest *pRequest)
{
	bool bRetVal;

	if (RemoteCommandCheck_aslMapManager_IsNameSpacePlayable(returnVal, &bRetVal) != TRANSACTION_OUTCOME_SUCCESS || !bRetVal)
	{
		NSLog(NULL, "UGC playable check failed... %s can not be provided\n", pRequest->pResourceName); 

		SendResourceToRequestingServer(NULL, pRequest->eRequestingServerType, pRequest->iRequestingServerId, pRequest->eResourceType, pRequest->pResourceName, NULL, "namespace not yet published");
	}
	else
	{
		NSLogVerbose(NULL, "UGC playable check succeeded... %s will be provided\n", pRequest->pResourceName); 
	
		RequestResource_Internal(pRequest->eRequestingServerType, pRequest->iRequestingServerId, pRequest->eResourceType, pRequest->pResourceName, true);
	}

	StructDestroy(parse_ResourceDBRequest, pRequest);
}

void BeginResourceCheckVerification(char *pNameSpaceName, GlobalType eServerType, ContainerID iServerID, eResourceDBResourceType eResourceType, char *pResourceName)
{
	char buffer[1024];

	ResourceDBRequest *pRequest = StructCreate(parse_ResourceDBRequest);

	NSLogVerbose(NULL, "going to check if namespace %s is published and ready to provide resource\n", pNameSpaceName); 

	pRequest->eRequestingServerType = eServerType;
	pRequest->iRequestingServerId = iServerID;
	pRequest->pResourceName = strdup(pResourceName);
	pRequest->eResourceType = eResourceType;

	sprintf(buffer, "ResourceDB checking namespace %s for resource %s of type %s", pNameSpaceName, pResourceName, StaticDefineIntRevLookup(eResourceDBResourceTypeEnum, eResourceType));

	RemoteCommand_aslMapManager_IsNameSpacePlayable(objCreateManagedReturnVal(VerificationCB, pRequest), GLOBALTYPE_MAPMANAGER, 0, pNameSpaceName, buffer);
}

AUTO_RUN;
int ResourceDBRegister(void)
{
	aslRegisterApp(GLOBALTYPE_RESOURCEDB,ResourceDBInit, APPSERVERTYPEFLAG_NOPIGGSINDEVELOPMENT);
	return 1;
}



void RequestResource_Internal(GlobalType eServerType, ContainerID iServerID, eResourceDBResourceType eResourceType, char *pResourceName, bool bNamespaceVerified)
{
	char nameSpaceName[RESOURCE_NAME_MAX_SIZE], baseName[RESOURCE_NAME_MAX_SIZE];
	ResourceDBRequest *pRequest;
	ResourceDBNameSpace *pNameSpace;
	
	NSLogVerbose(NULL, "Got a request for resource %s of type %s, verified: %d", pResourceName, StaticDefineIntRevLookup(eResourceDBResourceTypeEnum, eResourceType), bNamespaceVerified);

	if (!resExtractNameSpace(pResourceName, nameSpaceName, baseName))
	{
		ErrorOrAlert("CORRUPT_RESOURCE_NAME", "Someone requested resource %s of type %s. This could not be decoded into a namespace",
			pResourceName, StaticDefineIntRevLookup(eResourceDBResourceTypeEnum, eResourceType));
		SendResourceToRequestingServer(NULL, eServerType, iServerID, eResourceType, pResourceName, NULL, "Invalid namespace");
		return;	
	}

	if (!aslCheckNamespaceForPatchInfoFillIn(nameSpaceName))
	{
		ErrorOrAlert("CORRUPT_NS_NAME", "Someone requested resource %s of type %s. This was decoded into a namespace, but not one that we could patch",
			pResourceName, StaticDefineIntRevLookup(eResourceDBResourceTypeEnum, eResourceType));
		SendResourceToRequestingServer(NULL, eServerType, iServerID, eResourceType, pResourceName, NULL, "Unpatchable namespace");
		return;	
	}

	if (bNamespaceVerified)
	{
		pNameSpace = FindOrCreateNameSpace(nameSpaceName);
	}
	else
	{

		pNameSpace = FindNameSpace(nameSpaceName);

		if (!pNameSpace)
		{
			if (CheckIfNameSpaceNeedsVerification(nameSpaceName))
			{
				BeginResourceCheckVerification(nameSpaceName, eServerType, iServerID, eResourceType, pResourceName);
			}
			else
			{
				pNameSpace = FindOrCreateNameSpace(nameSpaceName);
			}
		}
	}

	if (pNameSpace)
	{
		pNameSpace->iTimesAccessed++;

		if (pNameSpace->bLoaded)
		{
			void *pResource = eaIndexedGetUsingString(GetResourceEarrayFromType(pNameSpace, eResourceType), pResourceName);
			NSLogVerbose(pNameSpace, "NS already loaded");

			if (!pResource)
			{
				RemoveNSInTheFuture(pNameSpace, "Someone requested resource %s of type %s. This namespace has been loaded, but the resource doesn't seem to exist",
					pResourceName, StaticDefineIntRevLookup(eResourceDBResourceTypeEnum, eResourceType));
				ErrorOrAlert("RESOURCE_NOT_FOUND", "Someone requested resource %s of type %s. This namespace has been loaded, but the resource doesn't seem to exist",
					pResourceName, StaticDefineIntRevLookup(eResourceDBResourceTypeEnum, eResourceType));
				SendResourceToRequestingServer(pNameSpace, eServerType, iServerID, eResourceType, pResourceName, NULL, "resource does not exist");
				return;
			}



			SendResourceToRequestingServer(pNameSpace, eServerType, iServerID, eResourceType, pResourceName, pResource, NULL);
			return;
		}

		EventCounter_ItHappened(pCacheSemiMissCounter, timeSecondsSince2000());

		pRequest = StructCreate(parse_ResourceDBRequest);
		pRequest->eRequestingServerType = eServerType;
		pRequest->iRequestingServerId = iServerID;
		pRequest->pResourceName = strdup(pResourceName);
		pRequest->eResourceType = eResourceType;

		eaPush(&pNameSpace->ppRequests, pRequest);

		NSLogVerbose(pNameSpace, "NS was not loaded, adding a request (now %d)", eaSize(&pNameSpace->ppRequests));

	}
}

AUTO_COMMAND ACMD_CATEGORY(debug);
void TestLoadNamespace(char *pName)
{
	FindOrCreateNameSpace(pName);
}

AUTO_STRUCT;
typedef struct ResourceDBOverview
{
	int iNumLoadedNameSpaces;
	U64 iSizeOfLoadedNameSpaces; AST(FORMATSTRING(HTML_BYTES = 1))
	U64 iAverageSize; AST(FORMATSTRING(HTML_BYTES = 1))

	int iTotalResourceRequests;
	
	int iResourceRequestsLastHour;
	int iCacheMissesLastHour;
	int iCacheSemiMissesLastHour;

	int iNameSpacesFreedLastHour; 

	int iAverageNSPatchSecondsLastHour; AST(FORMATSTRING(HTML_SECS_DURATION = 1))
	int iAverageNSPatchWaitTimeLastHour; AST(FORMATSTRING(HTML_SECS_DURATION = 1))
	int iAverageNSPatchWaitTimeOnceInQueueLastHour; AST(FORMATSTRING(HTML_SECS_DURATION = 1))
	int iNumQueuedNSLastHour; 

	int iMaxNSPatchTimeLastHour; AST(FORMATSTRING(HTML_SECS_DURATION = 1))


	int iResourceRequestsLastDay;
	int iCacheMissesLastDay;
	int iCacheSemiMissesLastDay;

	int iNameSpacesFreedLastDay; 

	int iAverageNSPatchSecondsLastDay; AST(FORMATSTRING(HTML_SECS_DURATION = 1))
	int iAverageNSPatchWaitTimeLastDay; AST(FORMATSTRING(HTML_SECS_DURATION = 1))
	int iAverageNSPatchWaitTimeOnceInQueueLastDay; AST(FORMATSTRING(HTML_SECS_DURATION = 1))
	int iNumQueuedNSLastDay; 

	int iMaxNSPatchTimeLastDay; AST(FORMATSTRING(HTML_SECS_DURATION = 1))


	int iCurCountPatchingNameSpaces;
	int iCurCountQueuedNameSpaces;

	char *pNameSpaces; AST(ESTRING FORMATSTRING(HTML = 1, HTML_NO_HEADER = 1))
	char *pGenericInfo; AST(ESTRING, FORMATSTRING(HTML=1, HTML_NO_HEADER=1))
} ResourceDBOverview;




void ResourceDB_GetCustomServerInfoStructForHttp(UrlArgumentList *pUrl, ParseTable **ppTPI, void **ppStruct)
{
	static ResourceDBOverview overview = {0};
	U32 iCurTime = timeSecondsSince2000();

	StructReset(parse_ResourceDBOverview, &overview);

	overview.iNumLoadedNameSpaces = siNumNameSpaces;
	overview.iSizeOfLoadedNameSpaces = siNameSpaceMemUsage;
	if (siNumNameSpaces)
	{
		overview.iAverageSize = siNameSpaceMemUsage / siNumNameSpaces;
	}

	overview.iNameSpacesFreedLastHour = EventCounter_GetCount(pFreedNSCounter, EVENTCOUNT_LASTFULLHOUR, iCurTime);
	overview.iTotalResourceRequests = EventCounter_GetTotalTotal(pRequestCounter);
	overview.iResourceRequestsLastHour = EventCounter_GetCount(pRequestCounter, EVENTCOUNT_LASTFULLHOUR, iCurTime);
	overview.iCacheMissesLastHour = EventCounter_GetCount(pCacheMissCounter, EVENTCOUNT_LASTFULLHOUR, iCurTime);
	overview.iCacheSemiMissesLastHour = EventCounter_GetCount(pCacheSemiMissCounter, EVENTCOUNT_LASTFULLHOUR, iCurTime);
	overview.iAverageNSPatchSecondsLastHour = IntAverager_Query(pPatchTimeAverager, AVERAGE_HOUR);
	overview.iAverageNSPatchWaitTimeLastHour = IntAverager_Query(pTotalPatchWaitAverager, AVERAGE_HOUR);
	overview.iAverageNSPatchWaitTimeOnceInQueueLastHour = IntAverager_Query(pQueuedPatchWaitAverager, AVERAGE_HOUR);
	overview.iNumQueuedNSLastHour = EventCounter_GetCount(pQueueCount, EVENTCOUNT_LASTFULLHOUR, iCurTime);
	overview.iMaxNSPatchTimeLastHour = FloatMaxTracker_GetMax(pMaxPatchTimeTracker, timeSecondsSince2000() - 60 * 60);

	overview.iNameSpacesFreedLastDay = EventCounter_GetCount(pFreedNSCounter, EVENTCOUNT_LASTFULLDAY, iCurTime);
	overview.iTotalResourceRequests = EventCounter_GetTotalTotal(pRequestCounter);
	overview.iResourceRequestsLastDay = EventCounter_GetCount(pRequestCounter, EVENTCOUNT_LASTFULLDAY, iCurTime);
	overview.iCacheMissesLastDay = EventCounter_GetCount(pCacheMissCounter, EVENTCOUNT_LASTFULLDAY, iCurTime);
	overview.iCacheSemiMissesLastDay = EventCounter_GetCount(pCacheSemiMissCounter, EVENTCOUNT_LASTFULLDAY, iCurTime);
	overview.iAverageNSPatchSecondsLastDay = IntAverager_Query(pPatchTimeAverager, AVERAGE_DAY);
	overview.iAverageNSPatchWaitTimeLastDay = IntAverager_Query(pTotalPatchWaitAverager, AVERAGE_DAY);
	overview.iAverageNSPatchWaitTimeOnceInQueueLastDay = IntAverager_Query(pQueuedPatchWaitAverager, AVERAGE_DAY);
	overview.iNumQueuedNSLastDay = EventCounter_GetCount(pQueueCount, EVENTCOUNT_LASTFULLDAY, iCurTime);
	overview.iMaxNSPatchTimeLastDay = FloatMaxTracker_GetMax(pMaxPatchTimeTracker, timeSecondsSince2000() - 24 * 60 * 60);

	overview.iCurCountPatchingNameSpaces = eaSize(&gpWrappedClients);
	overview.iCurCountQueuedNameSpaces = eaSize(&sppWaitingNameSpaces);


	
	estrPrintf(&overview.pNameSpaces, "<a href=\"%s%sResourceNameSpaces\">NameSpaces</a>",
		LinkToThisServer(), GLOBALOBJECTS_DOMAIN_NAME);

	estrPrintf(&overview.pGenericInfo, "<a href=\"/viewxpath?xpath=%s[%u].generic\">Generic ServerLib info for the %s</a>",
		GlobalTypeToName(GetAppGlobalType()), gServerLibState.containerID,GlobalTypeToName(GetAppGlobalType()));
		


	*ppTPI = parse_ResourceDBOverview;
	*ppStruct = &overview;
}

AUTO_STARTUP(ResourceDB) ASTRT_DEPS(ContactAudioPhrases, MissionPlayTypes);
void ResourceDB_Startup(void)
{
}


//because this should be rarely done, making it a bit inefficient by having
//it link the thing in as the oldest and then call removeOldest, just to avoid code duplication
void RemoveNS(ResourceDBNameSpace *pNameSpace)
{
	if (pNameSpace == spOldest)
	{
		FreeOldest();
		return;
	}

	if (pNameSpace->pNewer)
	{
		pNameSpace->pNewer->pOlder = pNameSpace->pOlder;
	}
	if (pNameSpace->pOlder)
	{
		pNameSpace->pOlder->pNewer = pNameSpace->pNewer;
	}

	spOldest->pOlder = pNameSpace;
	pNameSpace->pNewer = spOldest;
	pNameSpace->pOlder = NULL;
	spOldest = pNameSpace;

	FreeOldest();
}

AUTO_COMMAND;
int RemoveNSFromCacheByName(char *pNSName, CmdContext *pContext)
{
	ResourceDBNameSpace *pNameSpace;

	if (!stashFindPointer(sResourceDBNameSpacesByName, pNSName, &pNameSpace))
	{
		return 0;
	}

	NSLog(pNameSpace, "Removing via CMD called by: %s", GetContextHowString(pContext));

	RemoveNS(pNameSpace);

	return 1;
}


void RemoveNSCB(TimedCallback *callback, F32 timeSinceLastCallback, UserData userData)
{
	ResourceDBNameSpace *pNameSpace = (ResourceDBNameSpace*)userData;

	if (!pNameSpace->pInvalidationCallback)
	{
		AssertOrAlert("NS_CB_CORRUPTION", "Something invalidly cleared a namespace's pInvalidation CB");
	}

	pNameSpace->pInvalidationCallback = NULL;

	RemoveNS(pNameSpace);
}


//something is wrong with this namespace... remove it from the cache in a while so that it
//can be reloaded
void RemoveNSInTheFuture(ResourceDBNameSpace *pNameSpace, FORMAT_STR const char *pWhy, ...)
{
	char *pWhyToUse = NULL;
	//already being removed in the future
	if (pNameSpace->pInvalidationCallback)
	{
		return;
	}

	estrGetVarArgs(&pWhyToUse, pWhy);
	
	NSLog(pNameSpace, "Will be removed in %d seconds because: %s", siBadNameSpaceInvalidationTime, pWhyToUse);
	estrDestroy(&pWhyToUse);

	pNameSpace->pInvalidationCallback = TimedCallback_Run(RemoveNSCB, pNameSpace, siBadNameSpaceInvalidationTime);
}

AUTO_COMMAND_REMOTE ACMD_INTERSHARD;
void FlushResDBCache_ForJob(char *pNameSpaceName, char *pJobName)
{
	ResourceDBNameSpace *pNameSpace = NULL;

	if(!stashFindPointer(sResourceDBNameSpacesByName, pNameSpaceName, &pNameSpace))
	{
		NSLog(NULL, "Job %s wants to remove namespace %s, it's not currently loaded, that's fine", pJobName, pNameSpaceName);
		return;
	}

	NSLog(pNameSpace, "Removing due to job %s", pJobName);
	RemoveNS(pNameSpace);
}

#include "aslResourceDB_h_ast.c"
#include "aslResourceDB_c_ast.c"
