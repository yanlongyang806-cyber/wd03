/***************************************************************************



***************************************************************************/

#ifndef OBJCONTAINER_H_
#define OBJCONTAINER_H_
GCC_SYSTEM

// This defines how to access and use containers, which are the top level structure
// for character data
#include "objEnums.h"
#include "objSchema.h"
#include "Array.h"
#include "synchronization.h"

typedef struct ContainerLock ContainerLock;
typedef struct ObjectPathOperation ObjectPathOperation;
typedef struct ObjectIndex ObjectIndex;
typedef struct ObjectIndexHeader ObjectIndexHeader;
typedef struct ObjectIndexKey ObjectIndexKey;
typedef struct ObjectIndexIterator ObjectIndexIterator;
typedef struct ObjectHeaderOrData ObjectHeaderOrData;
typedef struct ContainerUpdate ContainerUpdate;
typedef struct ReadWriteLock ReadWriteLock;
typedef struct ObjectLock ObjectLock;

AUTO_ENUM;
typedef enum ContainerState {
	CONTAINERSTATE_UNKNOWN, //we don't know where it is
	CONTAINERSTATE_OWNED,	//We own this object
	CONTAINERSTATE_DB_COPY,	//This is the copy of a container on the database
	CONTAINERSTATE_SUBSCRIBED_COPY, //This is a partial copy that was sent as part of the subscription
} ContainerState;

AUTO_STRUCT;
typedef struct ContainerMeta {
	ContainerState containerState; // Offline, on, or in transition
	GlobalType containerOwnerType; // Which server id owns this.
	ContainerID containerOwnerID; // Which server id owns this.
} ContainerMeta;

// Top level object
typedef struct Container {
	GlobalType containerType; // Global type of container, here for cache speed
	ContainerID containerID; // The cached container ID for this container
	void *containerData; // The actual data of this object
	ObjectIndexHeader *header; // The header data used for indexing
	ObjectLock *lock; // The ObjectLock associated with this container; don't index this directly, it might not be populated!

	// Data for lazy loading
	char *fileData;
	U32 fileSize;
	U32 bytesCompressed;
	U8 *rawHeader;
	U32 headerSize;
	U32 deletedTime;
	U32 checksum;

	U32 fileSizeHint;
	// Data for hacky loading (possibly union with other stuff?)

		
	ContainerSchema *containerSchema; // The actual schema of this object

	ContainerMeta	meta;				//Who owns the container

	U32 lastAccessed; // timeSecondsSince2000 of last access

	//ContainerState containerState; // Offline, on, or in transition
	//GlobalType containerOwnerType; // Which server id owns this.
	//ContainerID containerOwnerID; // Which server id owns this.
	U32 oldFileNoDevassert : 1; // The hogg file timestamp is less than 333961199 (8/2010) so don't complain about possibly missing a crc
	U32 isTemporary : 1; // Is this a temporary container, such as a backup?
	U32 isModified : 1; // Has this container been modified, and should be saved?
	U32 isBeingForceLoggedOut : 1; //ForceLogOut has been called on this container
	U32 isSubscribed : 1;
	// Used for synchronizing saving
	ContainerUpdate *savedUpdate;
	int updateLocked;
} Container;

U32 objGetCurrentlyLockedContainerStoreCount(void);

// Used as part of the database multithreading code
ObjectLock *objGetObjectLock(GlobalType containerType, ContainerID containerID);

void enableObjectLocks(bool enable);

AUTO_STRUCT;
typedef struct TrackedContainer
{
	ContainerRef conRef; AST(KEY)
	Container *container; NO_AST
} TrackedContainer;

typedef struct TrackedContainerArray
{
	StashTable lookupContainerByRef;
	TrackedContainer **trackedContainers;
} TrackedContainerArray;

void AddContainerToTrackedContainerArray(TrackedContainerArray *containerArray, TrackedContainer *trackedCon);
void RemoveContainerFromTrackedContainerArray(TrackedContainerArray *containerArray, TrackedContainer *trackedCon);
TrackedContainer *FindContainerInTrackedContainerArray(TrackedContainerArray *containerArray, TrackedContainer *trackedCon);
void ClearTrackedContainerArray(TrackedContainerArray *containerArray);

typedef struct DeletedContainerQueueEntry DeletedContainerQueueEntry;

typedef U32 (*getDCCDestroyTimeFunc)(const DeletedContainerQueueEntry *entry);
typedef void *(*getDCCDestroyTimeUserDataFunc)(Container *con);

typedef struct DeletedContainerQueueEntry
{
	U32 iDeletedTime;
	ContainerID containerID;
	bool queuedForDelete;
	int heapIndex;
	getDCCDestroyTimeFunc destroyTimeFunc;
	void *userData; // Custom data for the destroyTimeFunc so that it does not need to get the container.
} DeletedContainerQueueEntry;

U32 GetCachedDeleteTimeout();

U32 dccGetDestroyTime(const DeletedContainerQueueEntry *entry);

typedef void (*ContainerExistenceCallback)(Container *con, void *data);

#define DEBUG_NEW_CONTAINERSTORE_LOCKING 0

typedef struct ContainerStoreReadWriteLockInfo
{
	const char *lastFilename;
	int lastLineNumber;
	bool readOnly;
} ContainerStoreReadWriteLockInfo;

// A collection of containers
typedef struct ContainerStore
{
	// Starting with basic critical sections, but I will be creating an 
	// interface on this side to support read/write locking if we decide it 
	// is necessary
	ReadWriteLock *containerReadWriteLock;
	ContainerStoreReadWriteLockInfo lockInfo;
	Container **containers;
#if DEBUG_NEW_CONTAINERSTORE_LOCKING
	U32 readersInContainerCriticalSection;
	U32 writersInContainerCriticalSection;
#endif
	ReadWriteLock *deletedContainerReadWriteLock;
	ContainerStoreReadWriteLockInfo deletedLockInfo;
	Container **deletedContainerCache;
#if DEBUG_NEW_CONTAINERSTORE_LOCKING
	U32 readersInDeletedContainerCriticalSection;
	U32 writersInDeletedContainerCriticalSection;
#endif

	//The schema that container type maps to
	ContainerSchema *containerSchema;

	ContainerID maxID;

	StashTable lookupTable; //Lookup table for id->container
	StashTable deletedLookupTable; //Lookup table for id->container
	Array *deletedContainerQueue;
	
	getDCCDestroyTimeFunc deletedContainerQueueDestroyTimeFunc;
	getDCCDestroyTimeUserDataFunc deletedContainerQueueDestroyTimeDataFunc;
	StashTable deletedCleanUpLookupTable;

	int deletedContainers; // Containers of the type deleted in this store
	int ownedContainers; // Containers of type owned in this store
	int totalContainers; // Containers of any type in this store

	ContainerExistenceCallback addCallback;
	ContainerExistenceCallback removeCallback;

	ContainerExistenceCallback deleteCallback;
	ContainerExistenceCallback undeleteCallback;

	ObjectIndex **indices;
	ObjectIndex **deletedIndices;

	ObjectIndexHeaderType objectIndexHeaderType;
	StashTable headerFieldTable;
	U32 requiresHeaders : 1; // Any indexing for this containerstore requires headers
	U32 saveAllHeaders : 1; // Save all headers that are generated to the hog
	U32 lazyLoad : 1; // Load zipped versions of containers and only unzip when necessary
	U32 disableIndexing : 1; // Ignore indexing when adding containers
	U32 ignoreBaseContainerID : 1; // Ignore any container id limits imposed by objReserveNewContainerID

	StashTable objectLocks;
	U32 lockedContainerCount;
} ContainerStore;

extern ContainerID gMaxContainerID;
extern ContainerID gBaseContainerID;
extern bool gAllowChangeOfBaseContainerID;

// hasReadLock should only be true if you are requesting a write lock and have already locked for read
void enableContainerStoreLocking(bool enable);
#define objLockContainerStore(store, readOnly) objLockContainerStore_dbg(store, readOnly, __FILE__, __LINE__);
void objLockContainerStore_dbg(ContainerStore *store, bool readOnly MEM_DBG_PARMS);
void objUnlockContainerStore(ContainerStore *store, bool readOnly);
#define objLockContainerStoreDeleted(store, readOnly) objLockContainerStoreDeleted_dbg(store, readOnly, __FILE__, __LINE__);
void objLockContainerStoreDeleted_dbg(ContainerStore *store, bool readOnly MEM_DBG_PARMS);
void objUnlockContainerStoreDeleted(ContainerStore *store, bool readOnly);
#define objLockContainerStore_ReadOnly(store) objLockContainerStore(store, true)
#define objLockContainerStoreDeleted_ReadOnly(store) objLockContainerStoreDeleted(store, true)
#define objUnlockContainerStore_ReadOnly(store) objUnlockContainerStore(store, true)
#define objUnlockContainerStoreDeleted_ReadOnly(store) objUnlockContainerStoreDeleted(store, true)
#define objLockContainerStore_Write(store) objLockContainerStore(store, false)
#define objLockContainerStoreDeleted_Write(store) objLockContainerStoreDeleted(store, false)
#define objUnlockContainerStore_Write(store) objUnlockContainerStore(store, false)
#define objUnlockContainerStoreDeleted_Write(store) objUnlockContainerStoreDeleted(store, false)

typedef void (*CommitContainerCallback)(Container *con, ObjectPathOperation **operations);
typedef bool (*CommitContainerCallbackFilter)(Container *con, ObjectPathOperation *operation);
typedef void (*CommitContainerStateCallback)(Container *pContainer, ContainerState newState, GlobalType newOwnerType, ContainerID newOwnerID);

typedef struct CommitCallbackStruct
{
	CommitContainerCallback commitCallback;
	CommitContainerCallbackFilter filterCallback;
	char *matchString;
	bool bRunOnce;
	bool bRunOnceWithAllPathOps;
	bool bPreCommit;
} CommitCallbackStruct;

typedef struct QueuedContainerCommitStruct
{
	CommitContainerCallback commitCallback;
	ObjectPathOperation **compiledPaths;
} QueuedContainerCommitStruct;

// A collection of collections
typedef struct ContainerRepository
{
	ContainerStore containerStores[GLOBALTYPE_MAXTYPES];

	// Global Commit callbacks
	CommitCallbackStruct **commitCallbacks;

	// Deleted containers we need to deal with. (copies used for processing deletion)
	TrackedContainerArray removedContainersLocking;

	// Containers we need to save
	TrackedContainerArray modifiedEntityPlayers;
	TrackedContainerArray modifiedEntitySavedPets;
	TrackedContainerArray modifiedContainers;

	// Containers with errors that we didn't succesfully load. (copies used for processing deletion)
	Container **invalidContainers;

	CriticalSectionWrapper *repositoryCriticalSection;
} ContainerRepository;

void objLockGlobalContainerRepository(void);
void objUnlockGlobalContainerRepository(void);

// Register commit callbacks

void objRegisterContainerTypeAddCallback(GlobalType containerType, ContainerExistenceCallback cb);
void objRegisterContainerTypeRemoveCallback(GlobalType containerType, ContainerExistenceCallback cb);
void objRegisterContainerTypeDeleteCallback(GlobalType containerType, ContainerExistenceCallback cb);
void objRegisterContainerTypeUndeleteCallback(GlobalType containerType, ContainerExistenceCallback cb);

// Registers a per-container type commit callback
// Match string is a string that is matched against a path. It may contain *
// If bRunOnce is set, the function will only be called one time per commit
// If bRunOnceWithAllPathOps, the function will be called one time and pass the entire list of path operations to the commit callback
void objRegisterContainerTypeCommitCallback(GlobalType containerType, CommitContainerCallback cb, char *matchString, bool bRunOnce, bool bRunOnceWithAllPathOps, bool bPreCommit, CommitContainerCallbackFilter filter);

// Registers a global container type commit callback
void objRegisterGlobalCommitCallback(CommitContainerCallback cb, char *matchString, bool bRunOnce, bool bPreCommit);

// Run the commit callbacks for a given set of paths, if it is appropriate
int objContainerCommitNotify(Container *object, ObjectPathOperation **paths, bool bPreCommit);

// Register a state change callback. There can only be one of these, and it's global
void objRegisterGlobalStateCommitCallback(CommitContainerStateCallback cb);


// Container creation/destruction

// Initialize an object. This should set any non-textparser starting values
void *objCreateContainerObject(ContainerSchema *classDef, char *pComment);

// Destroys a container object. Should clean up any non-textparser data
#define objDestroyContainerObject(classDef, object) objDestroyContainerObjectEx(classDef, object, __FILE__, __LINE__)
void objDestroyContainerObjectEx(ContainerSchema *classDef, void *object, const char* file, int line);

// Initializes a container object to default values
void objInitContainerObject(ContainerSchema *classDef, void *object);

// Clears all data in container object
void objDeInitContainerObject(ContainerSchema *classDef, void *object);

// Creates a temp version of a container object. Won't call create callbacks
void *objCreateTempContainerObject(ContainerSchema *classDef, char *pComment);

// Destroys a temp version of a container object, without calling callbacks
void objDestroyTempContainerObject(ContainerSchema *classDef, void *object);

// Create the container itself. Does not create the object
Container *objCreateContainer(ContainerSchema *schema);

// Destroys a container and any objects contained therein
#define objDestroyContainer(object) objDestroyContainerEx(object, __FILE__, __LINE__)
void objDestroyContainerEx(Container *object, const char* file, int line);

//For updating indices.
bool objUpdateIndexInsert(Container *object, ObjectPathOperation **paths, ObjectPath ***cachedpaths);
bool objUpdateIndexRemove(Container *object, ObjectPathOperation **paths, ObjectPath ***cachedpaths);

// Simple ways to get information from index results without unpacking the containers
U32 objContainerIDFromIndexData(ContainerStore *store, ObjectHeaderOrData *idxdata);
ObjectIndexHeader *objHeaderFromIndexData(ContainerStore *store, ObjectHeaderOrData *indexData);
U32 objVirtualShardIDFromIndexData(ContainerStore *store, ObjectHeaderOrData *idxdata);


// Gets container data from index results (NOTE: this unpacks every container visited,
// please try to use the header data if you can)
void *objContainerDataFromIndexData(ContainerStore *store, ObjectHeaderOrData *idxdata);
void *objIndexGetNextContainerData(ObjectIndexIterator *iter, ContainerStore* store);
void *objIndexGetNextMatchContainerData(ObjectIndexIterator *iter, ObjectIndexKey *key, int match, ContainerStore *store);
bool objIndexGetContainerData(ObjectIndex *oi, ObjectIndexKey *key, S64 n, ContainerStore *store, void **strfound);

// Modify a container by applying the given string
int objModifyContainer(Container *object, char *diffstring);

// Make a copy of the container (including header etc)
// Any caller of this should already have the lock on the container
Container *objContainerCopy(Container *container, int temporary, char *pComment);


#define objGetContainerType(container) ((container)->containerType)
#define objGetContainerID(container) ((container)->containerID)

#define objGetContainer(type, id) objGetContainer_dbg(type, id, true, false, false MEM_DBG_PARMS_INIT)
#define objGetContainerEx(type, id, force_decompress, getFileData, getLock) objGetContainer_dbg(type, id, force_decompress, getFileData, getLock MEM_DBG_PARMS_INIT)

bool objDoesContainerExist(GlobalType containerType, ContainerID containerID);
bool objDoesDeletedContainerExist(GlobalType containerType, ContainerID containerID);

bool objIsContainerOwnedByMe(GlobalType containerType, ContainerID containerID);
bool objIsDeletedContainerOwnedByMe(GlobalType containerType, ContainerID containerID);

#define objGetContainerData(type, id) objGetContainerData_dbg(type, id MEM_DBG_PARMS_INIT)

#define objGetDeletedContainer(type, id) objGetDeletedContainer_dbg(type, id, true, false, false MEM_DBG_PARMS_INIT)
#define objGetDeletedContainerEx(type, id, force_decompress, getFileData, getLock) objGetDeletedContainer_dbg(type, id, force_decompress, getFileData, getLock MEM_DBG_PARMS_INIT)

#define objGetContainerGeneral(type, id, deleted) (deleted) ? objGetDeletedContainer((type), (id)) : objGetContainer((type), (id))
#define objGetContainerGeneralEx(type, id, force_decompress, getFileData, getLock, deleted) (deleted) ? objGetDeletedContainerEx((type), (id), (force_decompress), (getFileData), (getLock)) : objGetContainerEx((type), (id), (force_decompress), (getFileData), (getLock))

// Gets a container from the container repository
Container *objGetContainer_dbg(GlobalType containerType, ContainerID containerID, int forceDecompress, int getFileData, int getLock MEM_DBG_PARMS);
Container *objGetDeletedContainer_dbg(GlobalType containerType, ContainerID containerID, int forceDecompress, int getFileData, int getLock MEM_DBG_PARMS);


// Get and lock an ObjectLock for a container by type and ID
// Doesn't actually get you the container - once you have the lock, you could use objGetContainerEx(..., false) to do that
SA_RET_OP_VALID ObjectLock *objLockContainerByTypeAndID(GlobalType containerType, ContainerID containerID);

// Unlock an ObjectLock that was locked with objLockContainerByTypeAndID()
// Not intended for any other purpose!
void objUnlockContainerLock(SA_PRE_NN_NN_VALID SA_POST_NN_NULL ObjectLock **lock);

// Unlock a container that was locked with objGetContainerEx(..., true)
// NULLs your container pointer, because you're no longer allowed to use the container after unlocking
void objUnlockContainer(SA_PRE_NN_NN_VALID SA_POST_NN_NULL Container **container);

//Returns the number of containers of a type unpacked since DB startup
int objCountUnpackedContainersOfType(GlobalType eType);

//Returns the number of deleted containers of a type unpacked since DB startup
int objCountDeletedUnpackedContainersOfType(GlobalType eType);

//Returns the number of times an unpack has occurred for a container type since DB startup
//Differs from objCountUnpackedContainersOfType in that it counts all temporary unpacks
int objCountTotalUnpacksOfType(GlobalType eType);

//Returns the average ticks for unpacks of a container type since DB startup
U64 objGetAverageUnpackTicksOfType(GlobalType eType);

// This should not longer be called on the ObjectDB, because it does not allow for locking of containers
//directly gets the container data (ie, the object that the container "is")
static __forceinline void* objGetContainerData_dbg(GlobalType containerType, ContainerID containerID MEM_DBG_PARMS)
{
	Container *con = objGetContainer_dbg(containerType, containerID, true, false, false MEM_DBG_PARMS_CALL);
	if (con)
	{
		return con->containerData;
	}

	return NULL;
}


// Wrapper to quickly get state of container
ContainerState objGetContainerState(GlobalType containerType, ContainerID containerID);

// Find the index, given an ObjectID
int objContainerStoreFindIndex(ContainerStore *base, ContainerID objectID);

// Find the index, given an ObjectID
int objContainerStoreFindDeletedIndex(ContainerStore *base, ContainerID objectID);

// Add given container to container store
// Passing a 0 objectID will store the container at the next available index; otherwise objectID is used to verify the incoming container data state.
bool objAddToContainerStore(ContainerStore *base, Container *newcontainer, ContainerID newID, bool modified, U32 deletedTime);

// Reserve a containerID
ContainerID objReserveNewContainerID(ContainerStore *base);

// Delete container in hog. Only use this for containers that cannot be loaded. 
// Loaded containers removed this way will still be in RAM.
bool objRemoveContainerFromHog(Container **object);

// Remove container with given ID from store, also remove it from the hog.
bool objRemoveFromContainerStore(ContainerStore *base, ContainerID objectID);

// Remove container with given ID from the store, optionally remove it from the hog.
bool objRemoveFromContainerStoreAndHog(ContainerStore *base, ContainerID objectID, bool removeFromHog);

// Like objRemoveFromContainerStoreAndHog, but the caller may assert that it will only be called once per container for a performance increase.
bool objRemoveFromContainerStoreAndHogEx(ContainerStore *base, ContainerID objectID, bool removeFromHog, bool bGuaranteeNoDuplicateRemoves);

// Remove container with given ID from store, also remove it from the hog.
bool objRemoveDeletedFromContainerStore(ContainerStore *base, ContainerID objectID);

// Remove container with given ID from the store, optionally remove it from the hog.
bool objRemoveDeletedFromContainerStoreAndHog(ContainerStore *base, ContainerID objectID, bool removeFromHog);

// Like objRemoveFromContainerStoreAndHog, but the caller may assert that it will only be called once per container for a performance increase.
bool objRemoveDeletedFromContainerStoreAndHogEx(ContainerStore *base, ContainerID objectID, bool removeFromHog, bool bGuaranteeNoDuplicateRemoves);

// Modifies the container ID of a newly created container. DO NOT use this on existing containers
int objSetNewContainerID(Container *object, ContainerID id, ContainerID curID);

// Changes the container state (owned, etc) of a container
int objChangeContainerState(Container *object, ContainerState state, GlobalType ownerType, ContainerID ownerID);

// Prepare to modify, which can do some stuff in the background thread
void objContainerPrepareForModify(Container *container);

// Mark a container as modified
void objContainerMarkModified(Container *container);

// Returns the max ID used for containers of the specified type
ContainerID objContainerGetMaxID(GlobalType containerType);

// Create a container store based on the schema, and add it to the repository
#define objCreateContainerStore(schema) objCreateContainerStoreEx(schema, NULL, 1024, false, 0, false)
#define objCreateContainerStoreLazyLoad(schema, lazyLoad) objCreateContainerStoreEx(schema, NULL, 1024, lazyLoad, 0, false)
#define objCreateContainerStoreIgnoreBaseContainerID(schema) objCreateContainerStoreEx(schema, NULL, 1024, false, 0, true)
#define objCreateContainerStoreSize(schema, size) objCreateContainerStoreEx(schema, NULL, size, false, 0, true)
ContainerStore *objCreateContainerStoreEx(ContainerSchema *schema, StashTable headerFieldTable, U32 lut_size, bool lazyLoad, ObjectIndexHeaderType headerType, bool ignoreBaseContainerID);

// Find a container store, given a container type
ContainerStore *objFindContainerStoreFromType(GlobalType id);

// Find a container store, given a container type, and create if none exists
ContainerStore *objFindOrCreateContainerStoreFromType(GlobalType id);

// Clears out all current container data
void objDestroyAllContainers(void);

// Find out how many containers are loaded
int objCountOwnedContainers(void);

// Find out how many containers are loaded
int objCountTotalContainers(void);

// Find out how many deleted containers are loaded
int objCountDeletedContainers(void);

// Returns number of deleted containers of type
int objCountDeletedContainersWithType(GlobalType type);

// Returns number of owned containers of type
int objCountOwnedContainersWithType(GlobalType type);

// Returns number of unowned containers of type (This equates to active containers)
int objCountUnownedContainersWithType(GlobalType type);
#define objCountActiveContainersWithType(type) objCountUnownedContainersWithType(type)

// Returns number of containers of type
int objCountTotalContainersWithType(GlobalType type);

// Returns number of containers in the store
int objCountTotalContainersInStore(ContainerStore *store);

// Used by object paths to look up containers
int objContainerRootPathLookup(const char* name, const char *key, ParseTable** table, void** structptr, int* column);

// Adds a new container given a textparser string
Container *objAddToRepositoryFromText(GlobalType containerType, ContainerID containerID, char *structstring);

// Adds a new container, based on a given string
Container *objAddToRepositoryFromString(GlobalType containerType, ContainerID containerID, char *diffString);

// Adds a new container, that wraps an existing object
Container *objAddExistingContainerToRepository(GlobalType containerType, ContainerID containerID, void *wrappedObject);

void objUndeleteContainer(GlobalType containerType, ContainerID containerID, GlobalType sourceType, ContainerID sourceID, char *namestr);
void objDeleteContainer(GlobalType containerType, ContainerID containerID, GlobalType sourceType, ContainerID sourceID, bool destroyNow);

// Removes a container from the repository, removes it from the hog.
int objRemoveContainerFromRepository(GlobalType containerType, ContainerID containerID);

//Removes a container from the repository, optionally removes it from the hog.
int objRemoveContainerFromRepositoryAndHog(GlobalType containerType, ContainerID containerID, bool removeFromHog);

// Removes a deleted container from the repository, removes it from the hog.
int objRemoveDeletedContainerFromRepository(GlobalType containerType, ContainerID containerID);

//Removes a deleted container from the repository, optionally removes it from the hog.
int objRemoveDeletedContainerFromRepositoryAndHog(GlobalType containerType, ContainerID containerID, bool removeFromHog);

// Remove all containers of a certain type
void objRemoveAllContainersWithType(GlobalType containerType);

// Says a container is invalid
void objRegisterInvalidContainer(Container *temp);

// The number of containers registered as invalid.
U32 objCountInvalidContainers(void);

//Save a copy of a container to disk. Returns success. estrPrintf's the path to resultString, or error message on failure.
// outputName and resultString are optional.
bool objExportContainer(GlobalType containerType, ContainerID containerID, const char *outputDir, char *outputName, char **resultString);


// Container Iterators

typedef struct ContainerIterator
{
	ContainerRepository *repository;
	int repositoryIndex;
	ContainerStore *store;
	ContainerStore *initStore; //what the store was when we started, so we can reset it and do a second pass.
	int storeIndex;
	
	//Due to the lazy object loading on the object DB, some containers have a container header (ie, the container struct)
	//but no actual object (container->data). We want the container iterator to 
	//a container iterator only returns containers that are non-NULL on its first pass. If as we iterate through the
	//list we see any of those we skip them, and then turn the bNeedSecondPass flag on. Then when we are done with the first
	//pass we know if we will need a second pass
	bool bSecondPass;
	bool bNeedSecondPass;
} ContainerIterator;

// Initialize a container iterator, that iterates over one container type
void objInitContainerIteratorFromTypeEx(GlobalType containerType, ContainerIterator *iter, bool keepLock, bool readOnly);
#define objInitContainerIteratorFromType(containerType, iter) objInitContainerIteratorFromTypeEx(containerType, iter, true, true)

// Initialize a container iterator, that iterates over one container type, starting at a specific container
void objInitContainerIteratorFromContainerEx(Container *con, ContainerIterator *iter, bool keepLock, bool readOnly);
#define objInitContainerIteratorFromContainer(con, iter) objInitContainerIteratorFromContainerEx(con, iter, true, true)

// Initialize a container iterator from a ContainerStore
void objInitContainerIteratorFromStoreEx(ContainerStore *store, ContainerIterator *iter, bool keepLock, bool readOnly);
#define objInitContainerIteratorFromStore(store, iter) objInitContainerIteratorFromStoreEx(store, iter, true, true)

// Initializes a container iterator that iterates over all containers
void objInitAllContainerIteratorEx(ContainerIterator *iter, bool keepLock, bool readOnly);
#define objInitAllContainerIterator(iter) objInitAllContainerIteratorEx(iter, true, true);

// When objectpath is done, this will create an iterator that does an objectpath search
//void InitContainerIteratorWithExpression(ContainerRepository *base, S32 containerType, Expression *exp, ContainerIterator *iter);

// Returns the next Container (wrapper object) from an iterator, or NULL if done
// This is not guaranteed to be valid or safe if modifications have been made to the container store
// If that happens, you should re-init the iterator
Container *objGetNextContainerFromIteratorEx(ContainerIterator *iter, bool alreadyHasLock, bool readOnly);
#define objGetNextContainerFromIterator(iter) objGetNextContainerFromIteratorEx(iter, true, true)

// Returns the object wrapped by the next Container, or NULL if done
void *objGetNextObjectFromIteratorEx(ContainerIterator *iter, bool alreadyHasLock, bool readOnly);
#define objGetNextObjectFromIterator(iter) objGetNextObjectFromIteratorEx(iter, true, true)

void objClearContainerIteratorEx(ContainerIterator *iter, bool alreadyHasLock, bool readOnly);
#define objClearContainerIterator(iter) objClearContainerIteratorEx(iter, true, true);

extern ContainerRepository gContainerRepository;

#define objAddContainerStoreIndexWithPath(store, path) objAddContainerStoreIndexWithPaths(store, path, 0)
ObjectIndex* objAddContainerStoreIndexWithPaths(ContainerStore *store, const char *paths, ...);
ObjectIndex* objAddContainerStoreDeletedIndexWithPaths(ContainerStore *store, const char *paths, ...);

void objFixDeletedCleanupTables(ContainerStore *base, ContainerID containerID, bool alreadyHasLock, bool removeFromStashTable);
void objResetDeletedCleanupTableEntry(ContainerStore *base, ContainerID containerID);

// Gets the delete time of a cached deleted container
U32 objGetDeletedTime(GlobalType containerType, ContainerID containerID);

//Returns the first existing index whose primary sort column matches the given path.
ObjectIndex* objFindContainerStoreIndexWithPath(ContainerStore *store, const char *path);

//Turn on header saving
void objContainerStoreSetSaveAllHeaders(ContainerStore *store, bool on);

//Make this store ignore all index inserts
void objContainerStoreDisableIndexing(ContainerStore* store);

// Parses the header for this container if applicable and stores it on the container
int objGenerateHeaderEx(void *containerData, ObjectIndexHeader **outputheader, ContainerSchema *schema);
#define objGenerateHeader(container, schema) objGenerateHeaderEx(container->containerData, &container->header, schema)

void objGenerateHeaderExtraData(void *containerData, ObjectIndexHeader **outputheader, ContainerSchema *schema);

// Recreates the header and compares all the fields
void objVerifyHeader(Container *container, void *containerData, ContainerSchema *schema);

// CRC used to see if the header is up to date
U32 headerCRC;

// CRC used to see if the configuration file for extra data has changed
U32 extraDataCRC;

// SUPER IMPORTANT NOTE ABOUT CONTAINER_FOREACH_BEGIN
// This had to be changed to not lock the ContainerStore, to avoid hilarity
// If you use it in a multi-threaded server (read: the ObjectDB) you can't use this safely
// Similar to ARRAY_FOREACH_BEGIN
#define CONTAINER_FOREACH_BEGIN(type, container) \
	{ Container * container; \
	  ContainerIterator iterator##container = {0}; \
	  objInitContainerIteratorFromTypeEx(type, &iterator##container, false, true); \
	  for (container = objGetNextContainerFromIteratorEx(&iterator##container, false, true); container; container = objGetNextContainerFromIteratorEx(&iterator##container, false, true)) { FORCED_FOREACH_BEGIN_SEMICOLON

// Similar to ARRAY_FOREACH_END
#define CONTAINER_FOREACH_END \
	  } \
	}FORCED_FOREACH_END_SEMICOLON

#define CON_PRINTF_STR "%s[%d]"
#define CON_PRINTF_ARG(containerType, containerID) GlobalTypeToName(containerType), containerID

int defaultDCQCompare(const DeletedContainerQueueEntry *lhs, const DeletedContainerQueueEntry *rhs);
void objContainerStoreSetDestroyTimeFunc(ContainerStore *store, getDCCDestroyTimeFunc customGetDestroyTimeFunc, getDCCDestroyTimeUserDataFunc customGetDestroyTimeDataFunc);
int ForceKeepLazyLoadFileData(GlobalType containerType);
int ForceKeepLazyLoadFileDataWithStore(ContainerStore *store);


typedef struct NonBlockingContainerIterationSummary
{
	S64 iStartTime; //in timeGetTime msecs... 
	U32 iTotalFound;
	int iTotalTicks;
	int iNumResets;
} NonBlockingContainerIterationSummary;

//returns true if iteration should contue, false if it should stop
typedef bool (*NonBlockingContainerIteration_ContainerCB)(Container *pContainer, NonBlockingContainerIterationSummary *pSummary, void *pUserData);
typedef void (*NonBlockingContainerIteration_ResetCB)(void *pUserData);


//starts iterating through all the containers of a given type, checking only a limited number each frame. Calls the containerCB whenever it
//finds one, calls the ResetCB if the iteration has to be restarted due to a stash table resize (should be very rare, but it's essential
//that it be respected)
//
//pContainerCB is called with container ID 0 when the iteration is complete, at which point a NonBlockingContainerIterationSummary is also passed in
//
//returns true if iteration has begun, false otherwise
bool IterateContainersNonBlocking(GlobalType eContainerType, NonBlockingContainerIteration_ContainerCB pContainerCB, 
	NonBlockingContainerIteration_ResetCB pResetCB, void *pUserData);
void NonBlockingContainerIterators_Tick(void);


//given a container type and an expression (ie, "me.hitpoints > 5"), nonblocking iterates through all containres of that type,
//assembles an ea32 of all containerIDs of containers that match the expression, eventually calls the given callback with the ea32
typedef void (*NonBlockingQueryCB)(U32 **ppOutContainerIDs, NonBlockingContainerIterationSummary *pSummary, void *pUserData);

typedef bool (*NonBlockingSearchFunc) (void *containerData, void *searchData);

//returns false if it can't search, most likely because the expression string is not valid, or if there are no objects to search.
//If there are no objects of the specified type, it will return false and call the callback function immediately.
bool NonBlockingContainerQuery(GlobalType eType, char *pExpressionString, const char *pCustomExpressionTag, int iMaxToReturn, NonBlockingQueryCB pCB, void *pUserData);

// non-expression version of the above
bool NonBlockingContainerSearch(GlobalType eType, NonBlockingSearchFunc searchFunc, ParseTable *searchPti, void *searchData, int iMaxToReturn, NonBlockingQueryCB pCB, void *pUserData);


//special XMLRPC version, has structs for the query and the return
AUTO_STRUCT;
typedef struct NonBlockingXMLRPCContainerQuery_QueryStruct
{
	GlobalType eType;
	char *pExpressionString;
	int iMaxToReturn;
} NonBlockingXMLRPCContainerQuery_QueryStruct;

AUTO_STRUCT;
typedef struct NonBlockingXMLRPCContainerQuery_ReturnStruct
{
	ContainerID *pIDs;
} NonBlockingXMLRPCContainerQuery_ReturnStruct;

// con will be locked before being passed to the callback function
typedef void (*ForEachContainerCBType)(Container *con, void *userData);

// ForEachContainerOfType will continue as long as this callback returns true. 
// If a NULL callback is passed in, the function will process all containers.
typedef bool (*ForEachContainerContinueCBType)(void *userData);

ContainerID *GetContainerIDListFromStore(ContainerStore *store, bool deleted);
ContainerID *GetContainerIDListFromType(GlobalType containerType, bool deleted);

//If you use the macro ForEachContainerOfType, it will walk the normal container array. 
// We must be cautious of this when walking lazyloaded container stores or we will unpack everything.
// We want unpack to be false if at all possible.
void ForEachContainerOfTypeEx(GlobalType containerType, ForEachContainerCBType forEachCallback, ForEachContainerContinueCBType continueCallback, void *userData, bool unpack, bool deleted);
#define ForEachContainerOfType(containerType, forEachCallback, userData, unpack) ForEachContainerOfTypeEx(containerType, forEachCallback, NULL, userData, unpack, false)

void ForEachContainerInRepositoryEx(ForEachContainerCBType forEachCallback, ForEachContainerContinueCBType continueCallback, void *userData, bool unpack, bool deleted);
#define ForEachContainerInRepository(forEachCallback, userData, unpack) ForEachContainerInRepositoryEx(forEachCallback, NULL, userData, unpack, false)

#endif