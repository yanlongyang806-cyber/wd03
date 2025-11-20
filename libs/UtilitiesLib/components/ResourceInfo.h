#ifndef RESOURCEINFO_H_
#define RESOURCEINFO_H_
GCC_SYSTEM

#include "GlobalTypeEnum.h"
#include "objContainer.h"
#include "ReferenceSystem.h"

typedef struct ResourceDictionary ResourceDictionary;
typedef struct Message Message;

#define RESOURCE_NAME_MAX_SIZE 1024
#define RESOURCE_DICT_NAME_MAX_SIZE 64

// Resources are objects that are accessible using a name and a key
// These wrap other things, like reference dictionaries

AUTO_ENUM;
typedef enum ResourceReferenceType
{
	REFTYPE_REFERENCE_TO, // Resource refers to named resource
	REFTYPE_CHILD_OF, // Resource inherits from named resource
	REFTYPE_CONTAINS, // Resource is composed from named resource (ie, is inseperable)
} ResourceReferenceType;

AUTO_STRUCT AST_SINGLETHREADED_MEMPOOL;
typedef struct ResourceReference
{
	const char *resourceDict; AST(POOL_STRING) // Corresponds to dictionary name
	const char *resourceName; AST(POOL_STRING)// Corresponds to referent name	

	char *referencePath; // Path to this reference
	char *errorString; // If set, this reference is invalid and should be displayed as error
	ResourceReferenceType referenceType; // Type of relationship
	
} ResourceReference;

AUTO_STRUCT AST_SINGLETHREADED_MEMPOOL;
typedef struct ResourceInfo
{
	//It appears that "resourceName" is a pool string of a int UID made by hashing the display name and setting some key bits,
	// so these are compared by an address of a string of a int hash of a string.  The display name is then pooled into "resourceNotes".
	// this is super duper inefficient, and also some functions combinations result in multiple atoi/itoa operations.  
	// (the above is what I have observed in data, but there might be important exceptions!) TODO: make this less stupid.
	const char *resourceDict;		AST(POOL_STRING)		// Corresponds to dictionary name
	const char *resourceName;		AST(KEY POOL_STRING)	// Corresponds to referent name  THIS IS A INT UID IN A STRING!

	const char *resourceLocation;	AST(POOL_STRING)		// File location
	char *resourceDisplayName;		AST(RESOURCEDICT(Message)) // Display name
	const char *resourceScope;		AST(POOL_STRING)		// Location within a hierarchy
	const char *resourceTags;		AST(POOL_STRING)		// Space deliminated tags
	char *resourceNotes;									// Notes about resource THIS IS EDITOR DISPLAY NAME!
	const char *resourceIcon;		AST(POOL_STRING)		// Name of icon representing this
	U32 resourceID;											// unique locator id for a resource	
	ResourceReference **ppReferences; AST(NAME(Ref) NO_NETSEND)
} ResourceInfo;

AUTO_STRUCT;
typedef struct ResourceInfoHolder
{
	ResourceInfo ** ppInfos; AST(NAME(Resource) NO_INDEX)
} ResourceInfoHolder;

// Used to perform operations on resources
AUTO_ENUM;
typedef enum ResourceActionType
{
	kResAction_None,
	kResAction_Open,
	kResAction_Check_Out,
	kResAction_Modify,
	kResAction_Commit,
	kResAction_Revert,
} ResourceActionType;

AUTO_ENUM;
typedef enum ResourceActionResult
{
	kResResult_None,
	kResResult_Failure,
	kResResult_Success
} ResourceActionResult;

AUTO_STRUCT;
typedef struct ResourceAction
{	
	const char *pDictName; AST(POOL_STRING)
	const char *pResourceName; AST(POOL_STRING)
	ResourceActionType eActionType;
	char *estrActionDetails; AST(ESTRING)

	char *estrResultString; AST(ESTRING)
	ResourceActionResult eResult;
} ResourceAction;

AUTO_STRUCT;
typedef struct ResourceActionList
{
	ResourceAction ** ppActions; AST(NAME(Action))
	bool bDisableValidation;
	
	ResourceActionResult eResult;
} ResourceActionList;

AUTO_STRUCT;
typedef struct ResourceNameSpace
{
	// Name of this namespace, must match directory
	char *pName;

	// List of other namespaces to depend on
	char **ppDependencies;

	// Access Control List. This is just a first pass

	// Accounts with write access
	char **ppWritableAccounts;

} ResourceNameSpace;

// Information about a given dictionary
AUTO_STRUCT;
typedef struct ResourceDictionaryInfo
{
	const char *pDictName; AST(POOL_STRING) // Do not modify
	const char *pDictCategoryName; AST(POOL_STRING) // Do Not Modify
	ParseTable * pDictTable; NO_AST // Do not modify
	ResourceInfo ** ppInfos; AST(NAME(Resource)) // indexed list of info about all objects in dictionary. Must be managed manually
	bool bBrowsable;
	bool bNoLocation;
} ResourceDictionaryInfo;

extern ParseTable parse_ResourceDictionaryInfo[];
#define TYPE_parse_ResourceDictionaryInfo ResourceDictionaryInfo
extern ParseTable parse_ResourceReference[];
#define TYPE_parse_ResourceReference ResourceReference
extern ParseTable parse_ResourceInfo[];
#define TYPE_parse_ResourceInfo ResourceInfo
extern ParseTable parse_ResourceInfoHolder[];
#define TYPE_parse_ResourceInfoHolder ResourceInfoHolder
extern ParseTable parse_ResourceAction[];
#define TYPE_parse_ResourceAction ResourceAction
extern ParseTable parse_ResourceActionList[];
#define TYPE_parse_ResourceActionList ResourceActionList
extern ParseTable parse_ResourceNameSpace[];
#define TYPE_parse_ResourceNameSpace ResourceNameSpace

/********************************* Individual Resource Operations *********************************/

// Gets the specified object, for dictionary and item name
void *resGetObject(DictionaryHandleOrName dictHandle, const char *itemName);

// Updates the specified resource info based on the passed in object and paths
// if pOBject is NULL, it will 
void resUpdateInfo(DictionaryHandleOrName dictHandle, const char *resourceName, ParseTable *pParseTable, void *pObject,
				   const char *displayNamePath, const char *scopePath, const char *tagPath, const char *notesPath,
				   const char *iconPath, bool bFindDependencies, bool bMaintainIDs);

// Searches ppObjects in a dictionary info for a certain ResourceInfo. This only works if Update is manually called
ResourceInfo *resGetInfo(DictionaryHandleOrName dictHandle, const char *resourceName);

// Returns number of resource infos in a dictionary
//
//not to be confused with resDictGetNumberOfObjects
int resGetNumberOfInfos(DictionaryHandleOrName dictHandle);

// Returns a useful number to display for loading messages
int resGetNumberOfInfosEvenPacked(DictionaryHandleOrName dictHandle);

// Searches ppObjects in a dictionary info for a certain ResourceInfo. This only works if Update is manually called
ResourceInfo *resGetInfoFromDictInfo(ResourceDictionaryInfo *pInfo, const char *resourceName);

// Gets info, or creates new one if it doesn't exist
SA_RET_NN_VALID ResourceInfo *resGetOrCreateInfo(ResourceDictionaryInfo *pInfo, const char *resourceName);

// Remove Resource Info from the list on the dictionary info
void resRemoveInfo(ResourceDictionaryInfo *pDictInfo, const char *resourceName);

// Call this after manually modifying a resource info
void resHandleChangedInfo(DictionaryHandleOrName dictHandle, const char *itemName, bool bNoLocation);

// Returns file name for object
const char *resGetLocation(DictionaryHandleOrName dictHandle, const char *itemName);

// Searches ppObjects in a dictionary info for a certain ResourceInfo. This only works if Update is manually called
ResourceInfo *resGetInfoFromHolder(ResourceInfoHolder *pHolder, const char *pDictName, const char *resourceName);

// Gets info, or creates new one if it doesn't exist
SA_RET_NN_VALID ResourceInfo *resGetOrCreateInfoFromHolder(ResourceInfoHolder *pHolder, const char *pDictName, const char *resourceName);

// Get reference, or add it to a ResourceInfo
ResourceReference *resInfoGetOrCreateReference(ResourceInfo *resInfo, const char *pDictName, const char *pResourceName, const char *path, ResourceReferenceType type, const char *errorString);

// Add object dependencies to list, optionally recursing
bool resFindDependencies(DictionaryHandleOrName dictHandle, const char *itemName, ResourceInfoHolder *pHolder);

//given an object, returns the global type and container ID of that object, if it has one.
bool resFindGlobalTypeAndID(DictionaryHandleOrName dictHandle, const char *itemName, GlobalType *pOutType, ContainerID *pOutID);

// Find all references to a given resource. modifies the holder to put to SHALLOW COPIES of the resources, so don't free them
bool resFindReferencesToResource(DictionaryHandleOrName dictHandle, const char *itemName, ResourceInfoHolder *pHolder);

//if a callback has added for getting verbose object names, get one. Otherwise, use the normal name
void resGetVerboseObjectName(DictionaryHandleOrName dictHandle, const char *itemName, char **ppOutName);

/********************************* Resource Iteration *********************************/

// This structure is complicated so it can be allocated on the stack to avoid leaking
typedef struct ResourceIterator
{
	ResourceDictionary *pDict;
	ContainerIterator containerIter;
	RefDictIterator refIter;
	StashTableIterator stashIter;
	int index;

	union
	{
		void *pUserData;
		int index2;
	};
} ResourceIterator;

// Sets up an iterator, which can be on the stack
bool resInitIterator(DictionaryHandleOrName dictHandle, ResourceIterator *pIterator);

// Return the next object pointer and/or name, depending on which of 
//ppOutName and ppOutObj are non-NULL. Returns true if it returned something,
//false otherwise. Note that ppOutName is not an estring, and ppOutObj is not
//an earray
bool resIteratorGetNext(ResourceIterator *pIterator, const char **ppOutName, void **ppOutObj);

// Like resIteratorGetNext, but filters out objects not it NAMESPACE
// for you.  Equivalent to resIteratorGetNext when NAMESPACE is null.
bool resIteratorGetNextForNamespace(ResourceIterator *pIterator, const char* strNamespace, const char **ppOutName, void **ppOutObj);

// Cleans up an iterator. Only used right now to release locking on Container dictionaries
void resFreeIterator(ResourceIterator *pIterator);

/********************************* Dictionary Operations *********************************/

// Get the modifiable dictionary info structure for a dictionary
ResourceDictionaryInfo *resDictGetInfo(DictionaryHandleOrName dictHandle);

// Returns the proper name of a dictionary, or NULL if the dictionary doesn't exist
const char *resDictGetName(DictionaryHandleOrName dictHandle);

// Get the depcrecated name, used for loading
const char *resDictGetDeprecatedName(DictionaryHandleOrName dictHandle);

// Returns the dictionary parse object header, or NULL if the dictionary doesn't exist
const char *resDictGetParseName(DictionaryHandleOrName dictHandle);

// Gets ParseTable for a global object dictionary
ParseTable *resDictGetParseTable(DictionaryHandleOrName dictHandle);

// Returns total number of objects in dictionary
int resDictGetNumberOfObjects(DictionaryHandleOrName dictHandle);

typedef StashTableIterator ResourceDictionaryIterator;

// Iterate over all dictionaries
void resDictInitIterator(ResourceDictionaryIterator *pIterator);

// Return next modifiable dictionary Info
ResourceDictionaryInfo *resDictIteratorGetNextInfo(ResourceDictionaryIterator *pIterator);

typedef struct DictionaryEArrayStruct
{
	void **ppReferents;
	ParseTable *pEArrayParseTable;
} DictionaryEArrayStruct;

// If registered for the given dictionary, return an earray struct for iterating
DictionaryEArrayStruct *resDictGetEArrayStruct(DictionaryHandleOrName dictHandle);

// Set the deprecated name, used for loading
void resDictSetDeprecatedName(DictionaryHandleOrName dictHandle, const char *pDeprecatedName);

// Set the parse name override, used for saving
void resDictSetParseName(DictionaryHandleOrName dictHandle, const char *pParseName);

// Sets the display names for a dictionary
void resDictSetDisplayName(DictionaryHandleOrName dictHandle, const char *pDisplayName, const char *pPluralDisplayName, const char *pCategoryName);

// Returns one of the display names. If none are registered, display the dictionary name
const char *resDictGetItemDisplayName(DictionaryHandleOrName dictHandle);
const char *resDictGetPluralDisplayName(DictionaryHandleOrName dictHandle);

//gets the "HTML comment string" for this dictionary, which is a string that will show up on the bottom of the page
//whenever this dictionary is being servermonitored
char *resDictGetHTMLCommentString(DictionaryHandleOrName dictHandle);

void resDictSetSendNoReferencesCallback(DictionaryHandleOrName dictHandle, bool bFlag);


/********************************* Hierarchical Group *********************************/

typedef struct ResourceGroup ResourceGroup;

AUTO_STRUCT;
typedef struct ResourceGroup {
	char *pchName;
	ResourceGroup **ppGroups;
	const ResourceInfo **ppInfos;
} ResourceGroup;

extern ParseTable parse_ResourceGroup[];
#define TYPE_parse_ResourceGroup ResourceGroup

typedef bool resCallback_GroupFilter(ResourceInfo *pInfo, void *pUserData);
typedef int resComparatorFunc(const ResourceInfo **left, const ResourceInfo **right);

// Build a hierarchical ResourceGroup structure
void resBuildGroupTreeEx(DictionaryHandleOrName dictHandle, ResourceGroup *pTop, resComparatorFunc *fpComparator);
#define resBuildGroupTree(dictHandle, pTop) resBuildGroupTreeEx(dictHandle, pTop, NULL)

// Build a filtered ResourceGroup
void resBuildFilteredGroupTree(DictionaryHandleOrName dictHandle, ResourceGroup *pTop, resCallback_GroupFilter *pCB, void *pUserData);

/********************************* Name Spaces *********************************/

#define NAMESPACE_PATH "ns/"

ResourceNameSpace *resNameSpaceGetDefault(void);
bool resNameSpaceSetDefault(char const *spaceName);

// Get or add a new namespace
ResourceNameSpace *resNameSpaceGetOrCreate(const char *spaceName);

// Gets a namespace
ResourceNameSpace *resNameSpaceGetByName(const char *spaceName);

// Removes a namespace
void resNameSpaceRemove(const char *spaceName);

typedef StashTableIterator ResourceNameSpaceIterator;

// Iterate over all name spaces
void resNameSpaceInitIterator(ResourceNameSpaceIterator *pIterator);

// Return next name space
ResourceNameSpace *resNameSpaceIteratorGetNext(ResourceNameSpaceIterator *pIterator);

// Return true if the given resource cache has access to passed in space
bool resNameSpaceValidForCache(ResourceNameSpace *pSpace, ResourceCache *pCache);

// Extracts the name space and base name from a resource name
bool resExtractNameSpace_s(const char *resourceName, char *nameSpace, size_t nameSpaceSize, char *baseName, size_t baseNameSize);
#define resExtractNameSpace(resourceName, nameSpace, baseName) resExtractNameSpace_s(resourceName, SAFESTR(nameSpace), SAFESTR(baseName))

bool resNamespaceBaseNameEq(const char *resourceA, const char *resourceB);
bool resNamespaceNameEq(const char *resourceA, const char *resourceB);

// Return TRUE if the resource is in a given directory, irrespective of namespace
bool resIsInDirectory(const char *resourceName, const char *directory);

// Call this to write out manifests for all active namespaces
void resWriteNamespaceManifests(void);

// Return true if the resource name includes a namespace
bool resHasNamespace(const char *resourceName);



/********************************* Dictionary Registration *********************************/

//global dictionaries have categories for purposes of sorting and monitoring
#define RESCATEGORY_REFDICT "RefDict"
#define RESCATEGORY_ART "Art"
#define RESCATEGORY_DESIGN "Design"
#define RESCATEGORY_CONTAINER "Container"
#define RESCATEGORY_SYSTEM "System"
#define RESCATEGORY_INDEX "Index"
#define RESCATEGORY_OTHER "Other"

typedef enum enumResDictFlags
{
	RESDICTFLAG_NOLINKINSERVERMONITOR = 1 << 0,

	//if true, then whenever resource dictionary callbacks are used to get an object, call
	//this callback on anything right before returning it
	RESDICTFLAG_USE_FIXUPTYPE_GOTTEN_FROM_RES_DICT = 1 << 1,

	//if true, then this earray will have empty slots, so you can't just eaSize it to get the number of elements in it
	REDICTFLAG_SPARSE_EARRAY = 1 << 2,

	//don't show this res dict in the server monitor
	RESDICTFLAG_HIDE = 1 << 3,

	//always show all elements in this dictionary when servermonitoring instead of paginating (use carefully)
	RESDICTLFAG_NO_PAGINATION = 1 << 4, 
} enumResDictFlags;

// Specify as many of these callbacks as possible, most will provide default behavior if not specified
// GetObject is required
typedef void *resCallback_GetObject(const char *pDictName, const char *itemName, void *pUserData);
typedef ReferentPrevious *resCallback_GetObjectPrevious(const char *pDictName, const char *itemName, void *pUserData);
typedef void resCallback_QueueCopyObjectToPrevious(const char *pDictName, const char *itemName, void *pUserData);
typedef int resCallback_GetNumberOfObjects(const char *pDictName, void *pUserData, enumResDictFlags eFlags);
typedef const char *resCallback_GetString(const char *pDictName, const char *itemName, void *pUserData);
typedef bool resCallback_FindDependencies(const char *pDictName, const char *itemName, ResourceInfo *pInfo, void *pUserData);
typedef bool resCallback_InitIterator(const char *pDictName, ResourceIterator *pIterator, void *pUserData);
typedef bool resCallback_FreeIterator(ResourceIterator *pIterator);
typedef bool resCallback_GetNext(ResourceIterator *pIterator, const char **ppOutName, void **ppOutObj, void *pUserData);
// If an object already exists, AddObject is responsible for removing it
// "updated" these for temporary simplification
typedef bool resCallback_AddObject(ResourceDictionary *pResDict, const char *itemName, void *pObject, void *pOldObject, void *pUserData);
typedef bool resCallback_RemoveObject(ResourceDictionary *pResDict, const char *itemName, void *pObject, void *pUserData);
typedef bool resCallback_GetGlobalTypeAndID(const char *pDictName, const char *itemName, GlobalType *pOutType, ContainerID *pOutID, void *pUserData);
typedef void resCallback_PreContainerSend(const char *pDictName, const char *itemName, const void *pNewReferent, const void *pUserData, StructTypeField *excludeFlagsInOut, StructTypeField *includeFlagsInOut);
typedef void resCallback_GetVerboseName(const char *pDictName, void *pObject, void *pUserData, char **ppOutString);
// Register a global object dictionary, with the given name

typedef char *resCallback_GetHTMLCommentString(ResourceDictionary *pResDict);

void resRegisterDictionary_dbg(const char *pDictName, const char *pDictCategory, 
							enumResDictFlags eFlags, ParseTable *pDictTable,
							resCallback_GetObject *pGetObjectCB,
							resCallback_GetObjectPrevious *pGetObjectPreviousCB,
							resCallback_QueueCopyObjectToPrevious *pQueueCopyObjectToPreviousCB,
							resCallback_GetNumberOfObjects *pGetNumObjectsCB,
							resCallback_GetString *pGetLocationCB,
							resCallback_FindDependencies *pFindDependenciesCB,
							resCallback_InitIterator *pInitIteratorCB,
							resCallback_GetNext *pGetNextCB,
							resCallback_FreeIterator *pFreeIteratorCB,
							resCallback_AddObject *pAddObjectCB,
							resCallback_RemoveObject *pRemoveObjectCB,
							resCallback_GetGlobalTypeAndID *pGetGlobalTypeAndIDCB,
							resCallback_GetVerboseName *pGetVerboseNameCB,
							const char *pDeprecatedName, void *pUserData
							MEM_DBG_PARMS);
#define resRegisterDictionary(	pDictName,\
								pDictCategory,\
								eFlags,\
								pDictTable,\
								pGetObjectCB,\
								pGetNumObjectsCB,\
								pGetLocationCB,\
								pFindDependenciesCB,\
								pInitIteratorCB,\
								pGetNextCB,\
								pFreeIteratorCB,\
								pAddObjectCB,\
								pRemoveObjectCB,\
								pGetGlobalTypeAndIDCB,\
								pGetVerboseNameCB,\
								pDeprecatedName,\
								pUserData) \
								resRegisterDictionary_dbg(	pDictName,\
															pDictCategory,\
															eFlags,\
															pDictTable,\
															pGetObjectCB,\
															NULL,\
															NULL,\
															pGetNumObjectsCB,\
															pGetLocationCB,\
															pFindDependenciesCB,\
															pInitIteratorCB,\
															pGetNextCB,\
															pFreeIteratorCB,\
															pAddObjectCB,\
															pRemoveObjectCB,\
															pGetGlobalTypeAndIDCB,\
															pGetVerboseNameCB,\
															pDeprecatedName,\
															pUserData MEM_DBG_PARMS_INIT)


// Tell Reference system to keep ResourceDictionaryInfo object index up to date
void resDictMaintainInfoIndex(DictionaryHandleOrName dictHandle, const char *displayNamePath, const char *scopePath, const char *tagPath, const char *notesPath, const char *iconPath);

// Used with resDictMaintainInfoIndex, also sets resourceID based on resourceName
void resDictMaintainResourceIDs(DictionaryHandleOrName dictHandle, bool maintainIDs);

// Return if it's supposed to maintain an index
bool resDictHasInfoIndex(DictionaryHandleOrName dictHandle);

//gets the flags
enumResDictFlags resDictGetFlags(DictionaryHandleOrName dictHandle);

// Helper functions for various callbacks

// Wrapper that registers correct callbacks for a ref dictionary
void resRegisterDictionaryForRefDict_dbg(const char *pDictName MEM_DBG_PARMS);
#define resRegisterDictionaryForRefDict(pDictName) resRegisterDictionaryForRefDict_dbg(pDictName MEM_DBG_PARMS_INIT)

// Wrapper that registers correct callbacks for a container repository
void resRegisterDictionaryForContainerType_dbg(GlobalType containerType MEM_DBG_PARMS);
#define resRegisterDictionaryForContainerType(containerType) resRegisterDictionaryForContainerType_dbg(containerType MEM_DBG_PARMS_INIT)

// Wrapper that registers callbacks for textparser objects in a stash table
void resRegisterDictionaryForStashTable_dbg(const char *pDictName, const char *pRefCategoryName, enumResDictFlags eFlags, StashTable table, ParseTable *pTPI MEM_DBG_PARMS);
#define resRegisterDictionaryForStashTable(pDictName, pRefCategoryName, eFlags, table, pTPI) resRegisterDictionaryForStashTable_dbg(pDictName, pRefCategoryName, eFlags, table, pTPI MEM_DBG_PARMS_INIT)

// Wrapper that registers callbacks for textparser objects in a stash table
void resRegisterDictionaryForEArray_dbg(const char *pDictName, const char *pRefCategoryName, enumResDictFlags eFlags, void ***pppEArray, ParseTable *pTPI MEM_DBG_PARMS);
#define resRegisterDictionaryForEArray(pDictName, pRefCategoryName, eFlags,pppEArray, pTPI) resRegisterDictionaryForEArray_dbg(pDictName, pRefCategoryName, eFlags, pppEArray, pTPI MEM_DBG_PARMS_INIT)

// Registers a dictionary that is only an index, for use in serializing arbitrary indexes to clients
void resRegisterIndexOnlyDictionary_dbg(const char *pDictName, const char *pRefCategoryName MEM_DBG_PARMS);
#define resRegisterIndexOnlyDictionary(pDictName, pCategoryName) resRegisterIndexOnlyDictionary_dbg(pDictName, pCategoryName MEM_DBG_PARMS_INIT)

void resDictSetHTMLCommentCallback(DictionaryHandleOrName dictHandle, resCallback_GetHTMLCommentString *pGetHTMLCommentCB);

//an extra command that shows up at the top of a filtered list page for a particular dictionary type
void resDictSetHTMLExtraCommand(DictionaryHandleOrName dictHandle, char *pCommandName, char *pCommandString);
void resDictGetHTMLExtraCommand(DictionaryHandleOrName dictHandle, char **ppOutCombinedCommandString);

char *resDictGetHTMLExtraLink(DictionaryHandleOrName dictHandle);
void resDictSetHTMLExtraLink(DictionaryHandleOrName dictHandle, FORMAT_STR const char *pFmt, ...);


#endif
