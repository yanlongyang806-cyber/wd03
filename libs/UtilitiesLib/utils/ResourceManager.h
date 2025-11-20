#pragma once
GCC_SYSTEM
/***************************************************************************



***************************************************************************/

#ifndef RESOURCEMANAGER_H_
#define RESOURCEMANAGER_H_

#include "referencesystem.h"
#include "textparserUtils.h"
#include "ResourceInfo.h"

typedef struct TextParserState TextParserState;
typedef struct TimedCallback TimedCallback;
typedef struct ResourceActionList ResourceActionList;
typedef struct ResourceDictionary ResourceDictionary;

// This file is for dealing with checking out and modifying resources 
// It ties with the code in ResourceSystemIO.c


/********************************* Managing Resource Dictionaries *********************************/

// The validation function will get called with one of these validation types
AUTO_ENUM;
typedef enum enumResourceValidateType
{
	// First 3 validations get called in order. When loading from bin files, first step is skipped
	RESVALIDATE_POST_TEXT_READING,	// Called only when loading from text data (ie, everything but from bins)
							// This gets called after inheritance has been applied
	RESVALIDATE_POST_BINNING, // Called for all loading/saving, after either FROM_TEXT is called or we load from bins
	RESVALIDATE_FINAL_LOCATION, // Called for all loading/saving, when data is in final location (post shared memory move)
							// No memory allocations or frees can happen here, and invalidDataErrorF will not work.
	// Other special case validation cases
	RESVALIDATE_FIX_FILENAME, // Called when a filename needs to be fixed up
	RESVALIDATE_CHECK_REFERENCES,	// This is called after ALL data has been loaded, and is in shared memory
									// You can't modify any data during this callback, but is useful for checking circular references

	RESVALIDATE_POST_RESDB_RECEIVE, //called after the resource DB has sent this thing, before it gets added to the dictionary

	RESVALIDATE_COUNT,
} enumResourceValidateType;

#define VALIDATE_HANDLED (-1)
#define VALIDATE_NOT_HANDLED (-2)

// Handles all validation and fixup duties. Return VALIDATE_HANDLED or VALIDATE_NOT_HANDLED 
typedef int resCallback_Validate(enumResourceValidateType eType, const char *pDictName, const char *pResourceName, void *pResource, U32 userID);

// This callback is used by the dictionary
typedef void resCallback_SendRequest(DictionaryHandleOrName dictHandle, int command, const char * pRefData, void * pResource, const char* reason);

// Sets up a resource dictionary to handle saving/loading.
// If combined with resDictRequestMissingResources, does partial validation on client and then forward to server
// If combined with resDictProvideMissingResources, accept save requests from clients
void resDictManageValidation(DictionaryHandleOrName dictHandle, resCallback_Validate pValidateCB);

// Sets up a resource dictionary to request resources (and do saving/loading) using the passed in callback
// iNumUnreferencedReferentsToKeep specifies how many unreferenced resources are maintained at any time, with 0 meaning unlimited.
// If Volatile is set, this dictionary should not assume that the client is EVER correct when connecting to a server, and should re-request
void resDictRequestMissingResources(DictionaryHandleOrName dictHandle, int iNumUnreferencedReferentsToKeep, bool bIsVolatile, resCallback_SendRequest *callback);

// Sets up a resource dictionary to handle requests for resources from a client that has called resDictRequestMissingResources
void resDictProvideMissingResources(DictionaryHandleOrName dictHandle);

// Sets up a resource dictionary to only provide/request resources when in edit mode
void resDictProvideMissingRequiresEditMode(DictionaryHandleOrName dictHandle);

// Enables/disables the resource earray struct
void resDictEnableEArrayStruct(DictionaryHandleOrName dictHandle, bool bEnable);

//even though this resource dictionary is providing missing resources, it still needs to do its editing locally
void resDictSetLocalEditingOverride(DictionaryHandleOrName dictHandle);

// Forward requests, i.e. from GameClient to ResourceDB
void resDictAllowForwardIncomingRequests(DictionaryHandleOrName dictHandle);

/********************************* Validation Functions *********************************/

// Returns true if pcName is a valid resource name
bool resIsValidNameEx(const char *pcName, char const ** ppcErrorInfo);
#define resIsValidName(name) resIsValidNameEx(name, NULL)

// Returns true if pcName is a valid extended name (used for internal things that are not resources)
bool resIsValidExtendedName(const char *pcName);

// formats the filename into an estring
void resFormatFilename(char **estr, const char *pcBase, const char *pcScope, const char *pcName, const char *pcExtension);

// Wraps up the filename fixing.  Returns true if the name is changed.
// The first parameter is the address of the filename
// The filename MUST be a pooled string to use this function
bool resFixPooledFilename(const char **ppcFilename, const char *pcBase, const char *pcScope, const char *pcName, const char *pcExtension);

// Returns true if pcScope is a valid resource scope
bool resIsValidScope(const char *pcScope);

// Returns true if pcScope is a valid extended scope (used for internal things that are not resources)
bool resIsValidExtendedScope(const char *pcScope);

// Makes sure the name string is legal
// Returns true if the estring is populated with a corrected name
bool resFixName(const char *pcName, char **estrNewName);

// Makes sure the scope string is legal
// Returns true if the estring is populated with a corrected scope
bool resFixScope(const char *pcScope, char **estrNewScope);

// Clears the earray, then adds unique scope values and sorts them
// Use "eaDestroyEx(&eaScopes, NULL)" to free the memory
void resGetUniqueScopes(DictionaryHandleOrName dictHandle, char ***peaScopes);

// Gets the list of valid tags for a dictionary
// The strings are allocated inside the dictionary, so do NOT free them
const char **resGetValidTags(DictionaryHandleOrName dictHandle);

// Manually add a tag to valid list, useful for tags loaded somewhere else
void resAddValidTag(DictionaryHandleOrName dictHandle, const char *pchTag);

/********************************* Dictionary Callbacks *********************************/

// Resource dictionaries can have one or more of the following user callbacks which inform the outside world
// when things happen inside the dictionary
typedef enum enumResourceEventType
{
	RESEVENT_RESOURCE_ADDED, // a new referent was added to this dictionary... only works for self-defining dictionaries
	RESEVENT_RESOURCE_PRE_MODIFIED, // A referent is about to be modified/replaced. Sometimes happens right before ReferentModified
	RESEVENT_RESOURCE_MODIFIED, // A referent was modified...only works if the someone calls RefSystem_ReferentModified
	RESEVENT_RESOURCE_REMOVED, // A referent is being removed (called right before the removal begins)
	RESEVENT_RESOURCE_LOCKED, // The client acquired a lock
	RESEVENT_RESOURCE_UNLOCKED, // The client lost a lock
	RESEVENT_INDEX_MODIFIED,// The ResourceInfo index info for an object has changed
	RESEVENT_NO_REFERENCES, // The resource has no references to it

	RESEVENT_COUNT,
} enumResourceEventType;

typedef void resCallback_HandleEvent(enumResourceEventType eType, const char *pDictName, const char *pResourceName, void *pResource, void *pUserData);

//  Add a new user callback, to the end of the list. Only instance of each function is allowed
void resDictRegisterEventCallback(DictionaryHandleOrName dictHandle, resCallback_HandleEvent *pCB, void *pUserData);

//  Remove an existing user callback function
void resDictRemoveEventCallback(DictionaryHandleOrName dictHandle, resCallback_HandleEvent *pCB);

/********************************* Loading Resources *********************************/


//NOTE: correctness of this enum is checked by a call to VerifyFlagEnums in resourcemanager.c
AUTO_ENUM;
typedef enum enumResourceLoadFlags
{
	RESOURCELOAD_SHAREDMEMORY	= ((PARSER_LASTPLUSONE-1) << 1), // Load resources into shared memory
	RESOURCELOAD_USERDATA		= ((PARSER_LASTPLUSONE-1) << 2), // Load data from user name spaces
	RESOURCELOAD_USEOVERLAYS	= ((PARSER_LASTPLUSONE-1) << 3), // Check for and use an overlay for this dictionary
	RESOURCELOAD_ALLNS			= ((PARSER_LASTPLUSONE-1) << 4), // Load files from all namespaces into a single bin 
	RESOURCELOAD_NO_BINS		= ((PARSER_LASTPLUSONE-1) << 5) // Do not generate bins
} enumResourceLoadFlags;

// Loads resources from disk, including calling ParserLoadFiles of the appropriate type and registering resload callbacks
// If binFile is NULL, generate a bin name dynamically
void resLoadResourcesFromDisk(DictionaryHandleOrName dictHandle, const char *directory, const char *filespec, const char *binFile, int flags);

// Run a validation on a specific resource, and return the success state
TextParserResult resRunValidate(enumResourceValidateType eType, DictionaryHandleOrName dictHandle, const char *pResourceName, void *pResource, U32 userID, TextParserState *tps);

// Run the 3 normal validates on an individual resource that we're thinking of saving. Returns true if all succeed
bool resValidateClientSave(DictionaryHandleOrName dictHandle, const char *pResourceName, void *pResource);

// for an entire resource dictionary, go through and make sure that everything that inherits from anything is updated
// (does nothing for dictionaries that have no tpi, or whose TPI has no InheritanceData)
void resApplyInheritanceToDictionary(DictionaryHandleOrName dictHandle, void ***pppResourceArray);

// Call this to indicate that a Dictionary should have RESVALIDATE_FINAL_LOCATION called on it after loading is complete.
// If handle is passed in, it will unlock that shared memory handle when loading is complete
void resWaitForFinishLoading(DictionaryHandleOrName dictHandle, SharedMemoryHandle *handle, bool bOnlyUnlock);

// Call this function after all loading has ended to call RESVALIDATE_FINAL_LOCATION and unlock shared memory
void resFinishLoading(void);

// Call this function after all loading has occurred to run RESVALIDATE_CHECK_REFERENCES on everything
void resValidateCheckAllReferences(void);

// Call this function to check all references within dictionary, and to dictionary
void resValidateCheckAllReferencesForDictionary(DictionaryHandleOrName dictHandle);

// Check a set of dictionaries, but only once each
void resValidateCheckAllReferencesForDictionaries(const char **ppDictNames);

// Enable/Disable loading status cache table for namespace loading
void resLoadingEnableStatusCache(DictionaryHandleOrName dictHandle, bool bEnabled);

// These functions can only be called during RESVALIDATE_POST_TEXT_READING
// They add dependencies to bin files

// This pushes a textparser state, so you can call following functions outside normal validation
// You MUST pop the state afterwards
void resPushTextParserState(TextParserState *tps);
void resPopTextParserState(void);

// This can be called during RESVALIDATE_POST_TEXT_READING to request that a resource not
// be loaded.
void resDoNotLoadCurrentResource(void);

// Add dependency on a given resource, will add a file dep implicitly
bool resAddResourceDep(DictionaryHandleOrName dictHandle, SA_PARAM_OP_STR const char *resourceName, ResourceReferenceType refType, const char *errorString);

// Add dependency on given file
bool resAddFileDep(SA_PARAM_OP_STR const char *filename);

// Add a dependency on CRC of a parse table
bool resAddParseTableDep(ParseTable *pTable);

// Add a dependency on CRC of a parse table
bool resAddParseTableNameDep(const char *pchTable);

// Add dependency on expression function definition
bool resAddExprFuncDep(const char *pchFuncName);

// Add dependency on arbitrary key/value pair, that must be registered using ParserBinRegisterDepValue
bool resAddValueDep(const char *pchValueName);

// Get/release a mutex around resource actions, to prevent reading in a half-written file
HANDLE resGetResourceMutex(void);
void resReleaseResourceMutex(HANDLE loadMutex);

/********************************* Resource Request *********************************/

// These are magic values for iNumUnreferencedReferentsToKeep:
#define RES_DICT_KEEP_ALL -1			// bypasses the array and doesn't free anything
#define RES_DICT_KEEP_NONE 0		// bypasses the array and frees everything

// General functions for dealing with clients that request referents

// Re-request all missing referents for the given dictionary. Useful if some resource is newly available
void resReRequestMissingResourcesWithReason(DictionaryHandleOrName dictHandle, const char* reason);
#define resReRequestMissingResources(dictHandle) resReRequestMissingResourcesWithReason(dictHandle, __FUNCTION__)

// Set the location ID of named referent, for use later by callbacks.
// If a location ID is set, ref info will never be destroyed (thus losing the location ID)
bool resSetLocationID(DictionaryHandleOrName dictHandle, const char * pResourceName, U32 locationID);

// Get back the location ID. This is useful for seeing if a name has been registered
U32 resGetLocationID(DictionaryHandleOrName dictHandle, const char * pResourceName);

// For changing the iNumUnreferencedReferentsToKeep on a dictionary that requests missing data on the fly
void resDictSetMaxUnreferencedResources(DictionaryHandleOrName dictHandle, int iNumUnreferencedReferentsToKeep);

//  Request all members of a dictionary, to populate for editing. 
//  Invalid in production mode!
void resRequestAllResourcesInDictionaryWithReason(	DictionaryHandleOrName dictHandle,
													const char* reason);
#define resRequestAllResourcesInDictionary(dictHandle) resRequestAllResourcesInDictionaryWithReason(dictHandle, __FUNCTION__)

//  Sends a request to the server asking to open a referent read-only
//  Invalid in production mode!
void resRequestOpenResource(DictionaryHandleOrName dictHandle, const char * pResourceName);

// Returns true if the referent named is available for editing
bool resIsEditingVersionAvailable(DictionaryHandleOrName dictHandle, const char * pResourceName);

// Returns the lock owner, which is 1 on the client and the id on the server
U32 resGetLockOwner(DictionaryHandleOrName dictHandle, const char * pResourceName);

// Returns true if the lock owner is 0, which allows differentiation between the id being 0 and other ways resGetLockOwner() returns 0
bool resGetLockOwnerIsZero(DictionaryHandleOrName dictHandle, const char * pResourceName);

// Returns true if we can write the named resource
bool resIsWritable(DictionaryHandleOrName dictHandle, const char * pResourceName);

//  Sends a request to the server asking to lock a referent
//  Invalid in production mode!
void resRequestLockResource(DictionaryHandleOrName dictHandle, const char * pResourceName, void * pOverrideReferent);

//  Ask to unlock it
//  Invalid in production mode!
void resRequestUnlockResource(DictionaryHandleOrName dictHandle, const char * pResourceName, void * pOverrideReferent);

//  Send a modification to the server, which only works if you have it locked
//  Invalid in production mode!
void resRequestSaveResource(DictionaryHandleOrName dictHandle, const char * pResourceName, void * pOverrideReferent);

// Tells a client-side dictionary that you're going to be editing data inside this dictionary.
//  lockCallback gets called when the server decides you're allowed to lock a member of this dictionary
//  Invalid in production mode!
void resSetDictionaryEditModeWithReason(DictionaryHandleOrName dictHandle, bool bEnabled, const char* reason);
#define resSetDictionaryEditMode(dictHandle, bEnabled) resSetDictionaryEditModeWithReason(dictHandle, bEnabled, __FUNCTION__)

bool resIsDictionaryEditMode(DictionaryHandleOrName dictHandle);
void resSetDictionaryMustHaveEditCopyInEditMode(DictionaryHandleOrName dictHandle);

//equivalent to calling resSetDictionaryEditMode(dict, false) for all dictionaries for
//which resIsDictionaryEditMode() is true
void resClearAllDictionaryEditModes(void);

// Tells a server-side dictionary that you're going to be editing data inside this dictionary.
void resSetDictionaryEditModeServer(DictionaryHandleOrName dictHandle, bool bEnabled);


// Requests an atomic group of related resource actions
void resRequestResourceActions(ResourceActionList *pHolder);

void resAddRequestOpenResource(ResourceActionList *pHolder, DictionaryHandleOrName dictHandle, const char * pResourceName);
void resAddRequestLockResource(ResourceActionList *pHolder, DictionaryHandleOrName dictHandle, const char * pResourceName, void * pOverrideReferent);
void resAddRequestUnlockResource(ResourceActionList *pHolder, DictionaryHandleOrName dictHandle, const char * pResourceName, void * pOverrideReferent);
void resAddRequestSaveResource(ResourceActionList *pHolder, DictionaryHandleOrName dictHandle, const char * pResourceName, void * pOverrideReferent);


// Tells the server to send down all index information about a given dictionary
void resSubscribeToInfoIndexWithReason(DictionaryHandleOrName dictHandle, bool enabled, const char* reason);
#define resSubscribeToInfoIndex(dictHandle, enabled) resSubscribeToInfoIndexWithReason(dictHandle, enabled, __FUNCTION__)

// Tells the server to send down all index information about all dictionaries
void resSubscribeToAllInfoIndicesOnceWithReason(const char* reason);
#define resSubscribeToAllInfoIndicesOnce() resSubscribeToAllInfoIndicesOnceWithReason(__FUNCTION__)

// Tells the server to update the data for all referents currently in the dictionary
void resSyncDictionaryToServerWithReason(DictionaryHandleOrName dictHandle, const char* reason);
#define resSyncDictionaryToServer(dictHandle) resSyncDictionaryToServerWithReason(dictHandle, __FUNCTION__)

// Returns true if this dictionary is synced with server
bool resIsDictionaryFromServer(DictionaryHandleOrName dictHandle);

// Tells the server to update the data for all referents in all dictionaries
void resSyncAllDictionariesToServer(void);

// Runs the filename fixup function (if any) on the provided referent
// Returns true if the filename is changed.
// This referent does not need to currently be in the dictionary, but it must be of the same type.
bool resFixFilename(DictionaryHandleOrName dictHandle, const char * pResourceName, void * pOverrideReferent);

// Names are normally strict.  If this is set to true, the name can be more relaxed.
void resDictSetUseExtendedName(DictionaryHandleOrName dictHandle, bool bUseExtendedName);

// Names are normally strict.  If this is set to true, the name can be anything.
void resDictSetUseAnyName(DictionaryHandleOrName dictHandle, bool bUseAnyName);

// Reset ALL resource request data, both client and server. This needs to be done on both client AND server
void resDictResetRequestData(DictionaryHandleOrName dictHandle);

void resourcePeriodicUpdate(TimedCallback *callback, F32 timeSinceLastCallback, UserData userData);

/********************************* Dynamic Unpacking *********************************/

// Unpacks the requested referent, from the PackedStructStream passed in
// Each type needs to make their own resCallback_SendRequest that then calls this function with the right stream
void resUnpackHandleRequest(DictionaryHandleOrName dictHandle, int command, const char * pRefData, void * pResource, PackedStructStream *structStream);

// Adds passed in void * to the passed in stream, storing the location on the void * Info
#define resUnpackPackPotentialReferent(dictHandle, pRefData, pResource, structStream) resUnpackPackPotentialReferent_dbg(dictHandle, pRefData, pResource, structStream MEM_DBG_PARMS_INIT) 
bool resUnpackPackPotentialReferent_dbg(DictionaryHandleOrName dictHandle, const char * pRefData, void * pResource, PackedStructStream *structStream MEM_DBG_PARMS);

/********************************* Client/Server Networking *********************************/

// Deals with transferring reference data between servers and clients, which is a subset of general void * Request

// Disable all client side networking requests, which is meant for standalone mode
void resClientDisableAllRequests(void);

// Asks server what name spaces the client is allowed to edit
void resClientRequestEditingLoginWithReason(const char *loginName, bool bEnabled, const char* reason);
#define resClientRequestEditingLogin(loginName, bEnabled) resClientRequestEditingLoginWithReason(loginName, bEnabled, __FUNCTION__)

// Pass this to SetDictionaryShouldRequestMissingData to set up client-server net requests
void resClientRequestSendReferentCommand(DictionaryHandleOrName dictHandle, int command, const char * pRefData, void * pResource, const char* reason);

// Callback called for bad reference data
typedef void resCallback_BadPacket(U32 userID, const char *msg);

void resServerSetBadPacketCallback(resCallback_BadPacket *pCB);


// a referent cache is used by server-side ref systems to track what referents they believe they have already
// sent down to each client. These are used only by self-defining dictionaries with the request/provide referent
// options set
ResourceCache *resServerCreateResourceCache(U32 userID);
void resServerDestroyResourceCache(ResourceCache *pCache);

// Set debug name.
void resCacheSetDebugName(ResourceCache *pCache, const char* name);

// Sends update on an index object down to all clients who are subscribed to it
void resServerUpdateModifiedResourceInfoOnAllClients(ResourceDictionary *pDictionary, const char *pRefData);

// Sends specific object to a given client
// If bForce, then it always sends.  Otherwise only sends if client doesn't already have it.
void resServerSendReferentToClient(ResourceCache *pCache, DictionaryHandleOrName dictHandleOrName, const char *pResourceName, bool bForce);

// checks with the (client-side) ref system whether it has any requests for missing data that it needs
// to put into a packet
bool resClientAreTherePendingRequests(void);

// Reset any pending requests
void resClientCancelAnyPendingRequests(void);

// Tells the (client-side) ref system that a packet is going to be sent, presumably from client to server, into which the
// ref system should put any requests for missing data
void resClientSendRequestsToServer(Packet *pPacket);

// Sends namespace information to the client
void resServerSendNamespaceInfo(ResourceCache *pCache);

// Sends current ZoneMap to the client
void resServerSendZoneMap(ResourceCache *pCache, const char *map_name);

// Grant the user editing login, even if they haven't requested it yet
void resServerGrantEditingLogin(ResourceCache *pCache);

// Sends namespace resources for a given dictionary to the client
void resServerSendResourcesForNamespace(ResourceCache* pCache, DictionaryHandleOrName dict, const char* snamespace);

// Tells the (server-side) ref system that some missing data requests have arrived, and are in one packet, and they should
// be processed and the missing data put into another packet
bool resServerProcessClientRequests(Packet *pInPacket, ResourceCache *pCache);

// checks with the (client-side) ref system whether it has any requests for missing data that it needs
// to put into a packet
bool resServerAreTherePendingUpdates(ResourceCache *pCache);

// resServerProcessClientRequests might get called multiple times before the outpacket is actually sent. Thus,
// it leaves the packet unterminated. It is terminated here
void resServerSendUpdatesToClient(Packet *pOutPacket, ResourceCache *pCache, void *pCallbackData, int langID, bool bDestroyRequests);

// frees updates that were sent to the client (not done at send time because of threading).
void resServerDestroySentUpdates(ResourceCache *pCache);

// Tells the (client-side) ref system that missing data requests have been fulfilled. Returns true on success
bool resClientProcessServerUpdates(Packet *pPacket);

// Tells the resource io system to silently ignore specific dictionaries
void resServerIgnoreDictionaryRequests(const char *dictName);

/********************************* Notify Functions *********************************/

// These are called by various things that wrap objects

// Tell the resource system that there are now some or 0 references to a resource
void resNotifyRefsExistWithReason(DictionaryHandleOrName dictHandle, const char *pResourceName, const char* reason);
#define resNotifyRefsExist(dictHandle, pResourceName) resNotifyRefsExistWithReason(dictHandle, pResourceName, __FUNCTION__)

void resNotifyNoRefs(DictionaryHandleOrName dictHandle, const char *pResourceName, bool bHadRefs, const char* reason);

// A resource managed object has been created or destroyed
void resNotifyObjectCreated(ResourceDictionary *pResDict, const char *pResourceName, void *pObject, const char* reason);
void resNotifyObjectDestroyed(ResourceDictionary *pResDict, const char *pResourceName, void *pObject, const char* reason);

// Object is about to be modified
void resNotifyObjectPreModified(DictionaryHandleOrName dictHandle, const char *pResourceName);

// Object has been modified
void resNotifyObjectModifiedWithReason(DictionaryHandleOrName dictHandle, const char *pResourceName, const char* reason);
#define resNotifyObjectModified(dictHandle, pResourceName) resNotifyObjectModifiedWithReason(dictHandle, pResourceName, __FUNCTION__)


/********************************* Dictionary Editing *********************************/

// Functions to provide an interface to editing resource data

// You can create an "Edit Copy" of a resource, which is used for editing and previewing, but
// is not saved until the it is comitted to the dictionary. You can also change the active
// copy of the data to the edit copy, for previewing.

// These functions only work for self-defined dictionaries, so they use strings instead of reference data

// Set up dictionary to enable rest of resEdit commands
void resEditStartDictionaryModification(DictionaryHandleOrName dictHandle);

// Commit all non-reverted changes. 
bool resEditCommitAllModificationsWithReason(DictionaryHandleOrName dictHandle, bool bLoadsOnly, bool bExcludeNoTextSaveFieldsInComparison, const char* reason);
#define resEditCommitAllModifications(dictHandle, bLoadsOnly) resEditCommitAllModificationsWithReason(dictHandle, bLoadsOnly, true, __FUNCTION__)
#define resEditCommitAllModificationsIncludeNoTextSaveFieldsInComparison(dictHandle, bLoadsOnly) resEditCommitAllModificationsWithReason(dictHandle, bLoadsOnly, false, __FUNCTION__)

// Revert all pending modifications and stop dictionary modification
void resEditRevertAllModificationsWithReason(DictionaryHandleOrName dictHandle, const char* reason);
#define resEditRevertAllModifications(dictHandle) resEditRevertAllModificationsWithReason(dictHandle, __FUNCTION__)

// Returns if there are non-committed changes (including creation or destruction) to the given reference object
bool resEditDoChangesExist(DictionaryHandleOrName dictHandle, const char *pString);

// If the working copy is set to be saved, return that. Otherwise backup (or the base) copy.
void * resEditGetSaveCopy(DictionaryHandleOrName dictHandle, const char *pString);

// Set rather the working copy should be saved (used by textparser and other systems that save)
void resEditSetSaveWorkingCopy(DictionaryHandleOrName dictHandle, const char *pString, bool bSaveWorkingCopy);

// Returns the working copy of a reference object.
// This will return NULL if the object has been deleted but not committed, or if no changes exist
void * resEditGetWorkingCopy(DictionaryHandleOrName dictHandle, const char *pString);

// Returns the backup copy of a reference object. If no changes exist, return just the normal object
// This will return NULL if the object has been created but not committed.
void * resEditGetBackupCopy(DictionaryHandleOrName dictHandle, const char *pString);

// Sets the new working copy for a given reference object. Returns the new copy, or NULL on failure
// If the working copy is set as previewed, will update references.
// If NULL is passed in, this will cause the reference object to be deleted on commit.
// Now takes a ResourceDictionary* instead of handle for performance
void * resEditSetWorkingCopy(ResourceDictionary *pDictionary, const char *pString, void * pWorkingCopy);

// Removes the working copy of the named referent.
// Returns what was the working copy, which needs to be deleted/reused as desired
void * resEditRevertWorkingCopy(DictionaryHandleOrName dictHandle, const char *pString);

// Runs a validation pass on an entire dictionary, while editing. 
// It will run the validate function on any resources with a working copy
// If pppResourceArray is passed in, it will remove the working copy from the passed in earray
TextParserResult resEditRunValidateOnDictionary(enumResourceValidateType eType, DictionaryHandleOrName dictHandle, U32 userID, void ***ppResourceArray, TextParserState *tps);

// Returns the pending resource info holder, which is safely editable during binning
ResourceDictionaryInfo *resEditGetPendingInfo(DictionaryHandleOrName dictHandle);

#ifdef GAMESERVER
// This function only updates the dictionary item in memory. It does not write the data back to the disk.
bool resSaveObjectInMemoryOnly(DictionaryHandle dictHandle, void * pResource, const char * pResourceName);
#endif


/********************************* General Management *********************************/

// Functions for managing the resource editing process in general

// Gets the status of all modified resources
ResourceActionList *resGetResourceStatusList(void);

typedef void resCallback_HandleActionList(ResourceActionList *pHolder);

// Register a display callback for displaying resource status
void resRegisterResourceStatusDisplayCB(resCallback_HandleActionList cb);

// Register a callback for action results
void resRegisterHandleActionListCB(resCallback_HandleActionList cb);

// Register a callback for exclude flags
void resRegisterPreContainerSendCB(DictionaryHandleOrName dictHandle, resCallback_PreContainerSend *cb);

// Request a resource status list
void resRequestResourceStatusList(const char* reason);


// If you want to make a simple edit, use these functions
typedef enum ResourceSimpleEditRequest
{
	kResEditType_None = 0,
	kResEditType_CheckOut = 1,
	kResEditType_Revert = 2,
	kResEditType_EditTags = 4,
} ResourceSimpleEditRequest;

typedef bool resCallback_HandleSimpleEdit(ResourceSimpleEditRequest eRequestType, ResourceInfo *pInfo, const char *newValue);

// Register the specified function to handle the request types specified in the bits
// If CB is null, it will use a default handler
void resRegisterSimpleEditCB(DictionaryHandleOrName dictHandle, U32 requestTypeBits, resCallback_HandleSimpleEdit *cb);

// Returns the callback for a specified request type and 
resCallback_HandleSimpleEdit *resGetSimpleEditCB(DictionaryHandleOrName dictHandle, ResourceSimpleEditRequest eRequestType);

/********************************* Resource Overlays **************************/
// Resource overlays are a way to "swap" the partial contents of a dictionary, e.g.
// a dictionary container A, B, C, and D might have three different overlays that
// change A and C. All overlays for a dictionary must contain the same set of objects.
//
// Swapping overlays involves loading a new bin file from disk, and so is a fairly
// costly operation.

typedef struct ResourceOverlayDef ResourceOverlayDef;

bool ResourceOverlayExists(const char *pchDictName, const char *pchOverlay);
void ResourceOverlayNames(const char *pchDictName, const char ***ppchOverlayNames);
const char *ResourceOverlayBaseName(const char *pchDictName);
void ResourceOverlayLoad(const char *pchDictName, const char *pchOverlay);

// Certain system (like the mission system) want to distinguish
// between the initial validation and all future ones.
extern bool gIsValidatingAllReferences;



/*****************************resource db stuff*******************/

//if this has serverlib, and if -UseResourceDB has been set, then
//attempt to get all missing resources via remote command from the resource DB (which will in turn
//try to get them from a patchserver). Used for UGC maps.
//
//note that because it's a LATELINK, we have to cast the dictNameOrHandle to a void and back
LATELINK;
void resDictGetMissingResourceFromResourceDBIfPossible(void *dictNameOrHandle);

#endif
