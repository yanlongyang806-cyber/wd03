#ifndef RESOURCESYSTEMINTERNAL_H_
#define RESOURCESYSTEMINTERNAL_H_
GCC_SYSTEM

#include "ResourceInfo.h"
#include "ResourceManager.h"



// Used to send commands to any reference request back end (server, disk, decompress)
// The Z's are to force to end of dictionary list

#define GLOBALCOMMANDS_DICTIONARY "ZZZFakeDictionaryForGlobalCommands"


// Order for these matters somewhat. They will be sorted and sent with lower commands first
AUTO_ENUM;
typedef enum ResourceRequestType
{
	RESREQUEST_NONE = 0,
	RESREQUEST_SET_EDITMODE, // Client wishes to enter edit mode
	RESREQUEST_SUBSCRIBE_TO_INDEX, // Client wants to subscribe to updates to an index
	RESREQUEST_REQUEST_EDITING_LOGIN, // Client is logging in for editing
	RESREQUEST_REQUEST_EDITING_LOGOFF, // Client no longer wishes to edit
	RESREQUEST_CANCEL_REQUEST, // Client no longer cares about referent
	RESREQUEST_GET_RESOURCE, // Client wants a copy of a referent
	RESREQUEST_OPEN_RESOURCE, // Client wants a copy of a referent, in all cases
	RESREQUEST_HAS_RESOURCE, // Client already has a copy of referent, but wants to subscribe to it
	RESREQUEST_GET_ALL_RESOURCES, // Client wants all referents in dictionary, for editing
	RESREQUEST_REQUEST_RESOURCE_STATUS, // Requests the resource status window
	RESREQUEST_APPLY_RESOURCE_ACTIONS, // Request to do resource actions (checkin, checkout, etc)
} ResourceRequestType;

// Responses back from a request back end

AUTO_ENUM;
typedef enum ResourceUpdateType
{
	RESUPDATE_NONE = 0,
	RESUPDATE_UNLOCKED, // Client has lost lock on a resource
	RESUPDATE_LOCKED, // Client has gained lock on a resource
	RESUPDATE_NEW_RESOURCE, // Send down update only if client not subscribed
	RESUPDATE_MODIFIED_RESOURCE, // Only send if client has already subscribed to object
	RESUPDATE_FORCE_UPDATE, // Always send down update
	RESUPDATE_INDEX_UPDATE, // Updates to an index
	RESUPDATE_ERROR, // An error string sent from server
	RESUPDATE_NAMESPACE_UPDATE, // Namespace was sent
	RESUPDATE_DISPLAY_RESOURCE_STATUS, // Display the resource status window
	RESUPDATE_HANDLE_COMPLETED_ACTIONS, // Handle a completed list of resource actions
	RESUPDATE_INDEX_SUBSCRIBED, // Sent after all index updates have been sent as a result of a subscription
	RESUPDATE_DESTROYED_RESOURCE, // Sent when client is subscribed and object is destroyed.
	RESUPDATE_FENCE_INSTRUCTION, // An editor is waiting on a set of resources
} ResourceUpdateType;

//Dictionaries which request missing data need a way to track all the requests generated during a frame
//We keep an earray of such requests

//AUTO_STRUCTed solely for logging purposes, DO NOT structCreate/structDestroy these
AUTO_STRUCT;
typedef struct ResourceRequest
{
	ResourceUpdateType operation;
	const char *pDictionaryName;
	char *pResourceName;
	S32 resourceNameIsPooled;
	void *pRequestData; NO_AST // May be index struct if operation is INDEX_UPDATE
	void *pRequestData2; NO_AST
	void *pRequestParseTable; NO_AST // Parse Table of requestData
	U32 uFenceID; // Used for RESUPDATE_FENCE_INSTRUCTION
} ResourceRequest;

typedef struct ClientResourceSentState {
	StructTypeField excludeFlags;
} ClientResourceSentState;

typedef struct PerDictionaryResourceCache
{
	const char *pDictName;
	StashTable sTableResourceToVersion;
	StashTable sTableResourceToSentState;
	int iSubscribedToIndex; // Reference counted, you're subscribed if > 0
} PerDictionaryResourceCache;

typedef struct ResourceCache
{
	const char *userLogin;
	char* debugName;
	U32 userID;
	StashTable perDictionaryCaches;

	//eArray
	ResourceRequest **ppPendingResourceUpdates;
	ResourceRequest **ppSentResourceUpdates;
} ResourceCache;

//AUTO_STRUCTed only for logging purposes, do not StructCreate or StructDestroy
AUTO_STRUCT;
typedef struct ResourceStatus
{
	const char *pResourceName;
	U32 iResourceLocationID; // Referent locator id, used for reference requesting
	U32 iLockOwner; // Opaque ID of who owns the lock on this struct, this corresponds to a client
	const char *pResourceOwner; // User name of who owns the lock

	Referent pBackupCopy; NO_AST
	Referent pWorkingCopy; NO_AST

	U32 bResourceRequested : 1;
	U32 bResourcePresent : 1;
	U32 bResourceManaged : 1;
	U32 bIsEditCopy : 1; // Was this sent down while in edit mode
	U32 bSaveWorkingCopy : 1;
	U32 bLoadedFromDisk : 1; //If this was loaded off disk, never free it
	U32 bIsBeingLoaded : 1;
	U32 bRefsExist : 1;
	U32 bInUnreferencedList : 1;
} ResourceStatus;

typedef struct ResourceEventCallbackStruct
{
	resCallback_HandleEvent *pUserCallBack;
	void *pUserCallBackUserData;
} ResourceEventCallbackStruct;

typedef struct ResourceDictionary
{
	const char *pDictName;
	const char *pDictCategoryName; //poolstring
	const char *pParseName; //poolstring
	enumResDictFlags eFlags;
	const void *pRefDictHandle;
	const char *pDeprecatedName;
	char *pItemDisplayName; // Display name of a single item "Power"
	char *pItemDisplayNamePlural; // Plural display name "Powers"
	ParseTable *pDictTable;
	ResourceDictionaryInfo *pDictInfo;
	void *pUserData;
	ResourceDictionaryInfo *pPendingInfo;
	bool bHasErrorToValidate;

	resCallback_GetObject *pGetObjectCB;
	resCallback_GetObjectPrevious *pGetObjectPreviousCB;
	resCallback_QueueCopyObjectToPrevious *pQueueCopyObjectToPreviousCB;
	resCallback_GetNumberOfObjects *pGetNumObjectsCB;
	resCallback_GetString *pGetLocationCB;
	resCallback_FindDependencies *pFindDependenciesCB;
	resCallback_InitIterator *pInitIteratorCB;
	resCallback_GetNext *pGetNextCB;
	resCallback_FreeIterator *pFreeIteratorCB;
	resCallback_AddObject *pAddObjectCB;
	resCallback_RemoveObject *pRemoveObjectCB;

	resCallback_SendRequest *pSendRequestCB;
	resCallback_Validate *pValidateCB;
	resCallback_GetGlobalTypeAndID *pGetGlobalTypeAndIDCB;

	resCallback_PreContainerSend *pPreContainerSendCB;

	resCallback_HandleSimpleEdit *pSimpleCheckOutCB;
	resCallback_HandleSimpleEdit *pSimpleRevertCB;
	resCallback_HandleSimpleEdit *pSimpleTagEditCB;

	resCallback_GetVerboseName *pGetVerboseNameCB;

	resCallback_GetHTMLCommentString *pGetHTMLCommentCB;

	//when looking at filtered lists of this item type, here's an extra command that should show up
	char *pExtraServerMonCommandName;
	char *pExtraServerMonCommandString;

	ResourceEventCallbackStruct **ppEventCallbacks;
	DictionaryEArrayStruct *pEArrayStruct;

	// allocAddStringed names of dictionaries this dictionary depends on
	const char **ppDictsIDependOn;
	// dicts that depend on this dict
	const char **ppDictsDependingOnMe;
	
	StashTable resourceStatusTable;
	StashTable resourceStatusLoadingTable; // Temporary table for the things currently being added/removed. Designed to speed up namespace loading

	int iMaxUnreferencedResources;			// The number of unreferenced resources to keep around. RES_DICT_KEEP_ALL and RES_DICT_KEEP_NONE are special values.
	ResourceStatus **ppUnreferencedResources;	

	int iParserLoadFlags;
	int iIndexSubscribed;

	int iParserWriteFlags;

	SharedMemoryHandle **ppDictSharedMemory;
	bool bShouldFinishLoading;

	bool bDataEditingMode;
	bool bShouldRequestMissingData;
	bool bMustHaveEditDataInEditMode;

	bool bLocalEditingOverride; //if true, the dictionary does its resource editing locally even if bShouldRequestMissingData is true
	bool bForwardIncomingRequests; // Used, i.e. to forward resource requests from client to Resource DB

	bool bShouldProvideMissingData;
	bool bRequiresEditingModeToProvideMissing;
	bool bShouldMaintainIndex;
	bool bUseExtendedName;
	bool bUseAnyName;				// Allow the name to begin with underscore, etc.
	bool bIsCopyDictionary;
	bool bShouldSaveResources;
	bool bDictionaryBeingModified;
	bool bTempDisableResourceInfo;
	bool bRequestedAllReferents;
	bool bReceivedAllInfos;
	bool bVolatileDictionary; // Don't assume the copies of things you have on the client are at all valid
	bool bUsingLoadingTable; // If set, use the Loading table to accelerate namespace loading
	bool bMaintainIDs;
	bool bSendNoReferencesEvent; // If set then send RESEVENT_NO_REFERENCES when last reference is lost
	char *pDisplayNamePath;
	char *pScopePath;
	char *pTagsPath;
	char *pNotesPath;
	char *pIconPath;

	char **ppValidTags;

	// This is set during load to resources that are cross-referenced
	// It is cleaned up during the commit of load changes
	char **ppCrossReferencedResourcesDuringLoad;

	char *pHTMLExtraLink; //extra link that shows up at the top of the page when looking at this dictionary
		//in servermonitor (ESTRING)

	PERFINFO_TYPE *validateTimer; 
	// Filename and line number of who created this dictionary
	MEM_DBG_STRUCT_PARMS
} ResourceDictionary;

typedef struct ErrorMessage ErrorMessage;

typedef struct ResourceValidateState
{
	U32 userID; // Need a better system for this
	bool bIsValidating;
	bool bDuringSave;
	bool bForceNoLoad;
	ResourceDictionary *pDictionary;
	const char *pResourceName;
	void *pResource;
	ErrorMessage **ppErrorMessages;
	TextParserState *pTPS;
} ResourceValidateState;

extern ResourceValidateState gValidateState;

extern bool gIsReceivingData;

extern StashTable gResourceDictionaries;
extern StashTable gResourceNameSpaces;
extern int gLogResourceRequests;

extern resCallback_HandleActionList *gDisplayStatusCB;
extern resCallback_HandleActionList *gHandleActionListCB;

ResourceDictionary *resGetDictionary(DictionaryHandleOrName pDictName);
ResourceStatus *resGetStatus(ResourceDictionary *pDict, const char *resourceName);
ResourceStatus *resGetOrCreateStatus(ResourceDictionary *pDict, const char *resourceName);
void resDestroyStatus(ResourceDictionary *pDict, const char *resourceName);
void resDestroyAllStatus(ResourceDictionary *pDict);
ResourceDictionary *resDictIteratorGetNextDictionary(ResourceDictionaryIterator *pIterator);
void resDeleteUnusedStatuses(void);

void resServerRequestSendResourceUpdate(ResourceDictionary *pDictionary, const char *pResourceName, void * pResource, ResourceCache *pCache, PerDictionaryResourceCache *pDictCache, int command);

typedef void resServerFenceCallback(U32 uFenceID, UserData pData);
// Returns a fence ID which will be sent to the callback
U32 resServerRequestFenceInstruction(ResourceCache *pCache, resServerFenceCallback *pFenceCB, UserData pData);

void resServerUpdateModifiedReferentOnAllClients(ResourceDictionary *pDictionary, const char *pResourceName);
void resServerUpdateDestroyedReferentOnAllClients(ResourceDictionary *pDictionary, const char *pResourceName);

// These functions assume the dictionary name is a pooled string
PerDictionaryResourceCache *resGetOrCreatePerDictionaryCache(ResourceCache *pCache, const char *pDictName);
PerDictionaryResourceCache *resGetPerDictionaryCache(ResourceCache *pCache, const char *pDictName);

void resCacheCancelAllLocks(ResourceCache *pCache, bool bSendUpdates);
void resUpdateResourceLockedStatus(ResourceDictionary *pDict, const char *pResourceName);
void resUpdateResourceLockOwner(ResourceDictionary *pDictionary, const char *pResourceName, U32 newOwner);

void RemoveResourceFromUnreferencedList(ResourceDictionary *pDictionary, ResourceStatus *pResourceStatus);
void AddResourceToUnreferencedList(ResourceDictionary *pDictionary, ResourceStatus *pResourceStatus);
void PruneUnreferencedList(ResourceDictionary *pDictionary);

void UpdateResourceInfo(ResourceDictionary *pDictionary, const char *pResourceName);
void UpdatePendingResourceInfo(ResourceDictionary *pDictionary, const char *pResourceName, void *pResource);
ResourceCache *resGetCacheFromUserID(U32 userID);
ResourceCache *resGetCacheFromEditingLogin(const char *pEditLogin);
ResourceCache *resGetCacheWithEditingLogin(void);


void resDictRunEventCallbacks(ResourceDictionary *pDictionary, enumResourceEventType eType,
							  const char *pResourceName, void * pResource);

void resHandleResourceActions(ResourceActionList *pHolder, U32 userID, const char *pLoginName);


// Handles a modification from the client on a locked reference
bool SaveResourceObject(DictionaryHandleOrName dictHandle, void * pResource, const char * pRefData, U32 userID, char **estrOut);

void *resGetObjectFromDict(ResourceDictionary *pDict, const char *itemName);
ReferentPrevious *resGetObjectPreviousFromDict(ResourceDictionary *pDict, const char *itemName);
void resQueueCopyObjectToPrevious(ResourceDictionary *pDict, const char *itemName);

// Copies data from passed in object to the version managed by the resource system.
// If pObject is null, delete resource
// pObject is consumed by this
// pResourceStatus MUST be an actual resource status from pDictionary, or memory will corrupt in confusing ways
void resUpdateObject(ResourceDictionary *pDictionary, ResourceStatus *pResourceStatus, void *pObject);

// Start/stop a validate. This captures ErrorFs
void resStartValidate(ResourceDictionary *pDictionary, const char *pResourceName, void *pResource, U32 userID, bool bDuringSave, TextParserState *tps);
TextParserResult resStopValidate(void);

void resValidateCheckAllRequestedReferences(void);
void resValidateResourceReferenceErrors(ResourceDictionary *pDictionary);

// Register that dictHandle depends upon targetHandle dictionary
void resDictDependOnOtherDict(ResourceDictionary *pDictionary, DictionaryHandleOrName targetHandle, char const * pchCause);

bool resShouldRequestMissingResources(ResourceDictionary *pDictionary);
bool resShouldProvideMissingResources(ResourceDictionary *pDictionary);

// Additional reference dictionary interface functions:
bool resAddObject_Ref(ResourceDictionary *pDictionary, const char *itemName, void *pObject, void *pOldObject, void *pUserData);
bool resRemoveObject_Ref(ResourceDictionary *pResDict, const char *itemName, void *pObject, void *pUserData);

// Register data back end functions, for dealing with 

typedef bool resDataBackend_FileAction(ResourceDictionary *pDictionary, void * pResource, const char *pResourceName, char **estrOut);
typedef void resDataBackend_GetInfo(ResourceDictionary *pDictionary, ResourceInfo *resInfo, bool bFlag);
typedef void resDataBackend_CheckWritable(ResourceDictionary *pDictionary, ResourceInfo *resInfo, bool bFlag, bool bForReload);
typedef void resDataBackend_ApplyActions(ResourceActionList *pHolder);
typedef bool resDataBackend_DictionaryOperation(ResourceDictionary *pDictionary);

typedef struct ResourceDataBackend
{
	resDataBackend_FileAction *CheckoutCB;
	resDataBackend_FileAction *UndoCheckoutCB;
	resDataBackend_CheckWritable *CheckWritableCB;
	resDataBackend_GetInfo *CheckStatusCB;
	resDataBackend_ApplyActions *ApplyActionsCB;
} ResourceDataBackend;

extern ResourceDataBackend gResourceBackend;


/********************************* Demo Hooks *********************************/

// Hooks for demo recording and playback.

// Some dictionaries may be important to the demo playback.  This
// allows the demo to get notified of any changes that could happen.
typedef void (*ResourceModifiedFn)(void *pData, const char *pDictName, const char *pResourceName,
								   void *pObject, ParseTable *pDictTable);

void resSetNotificationHookForDemo(ResourceModifiedFn pFn, void* pData);
void resUpdateObjectForDemo(DictionaryHandleOrName dictHandle, const char *pResourceName, void *pObject);

/********************************* Resource Overlays **************************/

AUTO_STRUCT;
typedef struct ResourceOverlayDef
{
	const char *pchDictionary;		AST(POOL_STRING KEY REQUIRED STRUCTPARAM)
	const char *pchBase;			AST(POOL_STRING REQUIRED)
	const char **eaOverlays;		AST(POOL_STRING NAME(Overlay) NAME(Overlays) REQUIRED)

	const char *pchFilename;		AST(CURRENTFILE)

	// Used to store dictionary initialization state, so we can reload.
	char *pchBinFile;				AST(NO_TEXT_SAVE)
	char *pchFolders;				AST(NO_TEXT_SAVE)
	char *pchFileSpec;				AST(NO_TEXT_SAVE)
	U32 eParserLoadFlags;			AST(NO_TEXT_SAVE)
} ResourceOverlayDef;

AUTO_STRUCT;
typedef struct ResourceOverlayDefs
{
	ResourceOverlayDef **eaOverlayDefs; AST(NAME(Overlay) NAME(OverlayDef) NAME(ResourceOverlayDef))
} ResourceOverlayDefs;

extern ResourceOverlayDefs g_ResourceOverlayDefs;


//turn this on to enable verbose logging per-resource-dictionary. You also then need
//-DictNameForVerboseResourceLogging dictname
#if 0

#define VERBOSE_PERDICT_LOGGING

//verbose logging stuff for resource system debugging... turn it on via -VerboseResourceLoggingPerDict dictname
extern char *spDictNameForVerboseResourceLogging;
void VerboseLoggingPerDict(FORMAT_STR const char* format, ...);
void VerboseLoggingPerDictWithStruct(ParseTable *pTPI, void *pStruct, FORMAT_STR const char* format, ...);

#define VERBOSE_LOG_DICT(nameOrHandle, ...) { if (spDictNameForVerboseResourceLogging && stricmp_safe(resDictGetName(nameOrHandle),  spDictNameForVerboseResourceLogging) == 0) VerboseLoggingPerDict(__VA_ARGS__); }
#define VERBOSE_LOG_DICT_WITH_STRUCT(nameOrHandle, tpi, pStruct, ...) { if (spDictNameForVerboseResourceLogging && stricmp_safe(resDictGetName(nameOrHandle),  spDictNameForVerboseResourceLogging) == 0) VerboseLoggingPerDictWithStruct(tpi, pStruct, __VA_ARGS__); }

#else

#define VERBOSE_LOG_DICT(nameOrHandle, ...)
#define VERBOSE_LOG_DICT_WITH_STRUCT(nameOrHandle, tpi, pStruct, ...)

#endif


/* ABW 6/2013

One of the trickiest things about the resource system for me is just remembering what funcitons are called in all the different places
to make things work. So here are some sample callstacks of the various behaviors "in action":


Gameserver getting request for a resource that it already has a copy of:

 	GameServer.exe!resPushRequestServer
 	GameServer.exe!resServerRequestSendResourceUpdate
>	GameServer.exe!resServerProcessClientRequests
 	GameServer.exe!gslHandleRefDictDataRequests
 	GameServer.exe!gslHandleMsg
 	GameServer.exe!HandleGameMsgs
 	GameServer.exe!gslHandleInput

Gameserer fulfulling a request:

>	GameServer.exe!resServerSendUpdatesToClient
 	GameServer.exe!gslSendGeneralUpdateInThread
 	GameServer.exe!gslGeneralUpdateThreadMain

Gameserver receiving an update from the object DB for a subscribed container that a client is getting copies of:

 	GameServer.exe!resPushRequestServer
 	GameServer.exe!resServerRequestSendResourceUpdate
 	GameServer.exe!resServerUpdateModifiedReferentOnAllClients
 	GameServer.exe!resNotifyObjectModifiedWithReason
>	GameServer.exe!HandleSubscribedContainerCopyChange
 	GameServer.exe!MAGICCOMMANDWRAPPER_FROMPACKET_HandleSubscribedContainerCopyChange

Gameserver receiving an new object from the object DB for a subscribed container that a client is getting copies of:
 	GameServer.exe!resPushRequestServer
 	GameServer.exe!resServerRequestSendResourceUpdate
	GameServer.exe!resServerUpdateModifiedReferentOnAllClients
	GameServer.exe!resNotifyObjectCreated
	GameServer.exe!RefSystem_AddReferentWithReason
	GameServer.exe!ReceiveContainerCopy
>	GameServer.exe!HandleGotSendPacketSimple

Gameclient receiving an object from the gameserver:
>	GameClient.exe!RefSystem_AddReferentWithReason
	GameClient.exe!resAddObject_Ref
	GameClient.exe!resUpdateObject
	GameClient.exe!resClientProcessServerUpdates
	GameClient.exe!gclHandleGeneralUpdate
	GameClient.exe!gclHandlePacketFromGameServer
*/


#endif
