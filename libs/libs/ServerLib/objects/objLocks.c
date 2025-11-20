/***************************************************************************



***************************************************************************/


#include "ThreadSafeMemoryPool.h"
#include "objLocks.h"
#include "objTransactions.h"
#include "memlog.h"
#include "resourceInfo.h"
#include "stringCache.h"
#include "ObjLocks_h_ast.h"

bool gStoreObjLockMemlog = false;
AUTO_CMD_INT(gStoreObjLockMemlog, StoreObjLockMemlog) ACMD_CMDLINE;

MemLog objlockmemlog={0};

// CRITICAL_SECTION for accessing gObjectTransactionManager.backupTables and .lockTables

static CRITICAL_SECTION sBackupAndLockTablesMutex = {0};

static void EnterBackupAndLockTablesCriticalSection(void)
{
	ATOMIC_INIT_BEGIN;

	InitializeCriticalSection(&sBackupAndLockTablesMutex); 

	ATOMIC_INIT_END;

	EnterCriticalSection(&sBackupAndLockTablesMutex);
}

static void LeaveBackupAndLockTablesCriticalSection(void)
{
	LeaveCriticalSection(&sBackupAndLockTablesMutex);
}

// The locking scheme in place now:
// Fields can only be locked if not locked or locked by same transaction
// Containers can only be locked if not locked or locked by same transaction
// Fields can't be locked if container is locked, and vice versa

TSMP_DEFINE(FieldLock);

AUTO_RUN;
void initializeLockMemlog(void)
{
	memlog_enableThreadId(&objlockmemlog);
}

static FieldLock *CreateFieldLock(void)
{
	ATOMIC_INIT_BEGIN;
	TSMP_SMART_CREATE(FieldLock, 1024, TSMP_X64_RECOMMENDED_CHUNK_SIZE);
	ATOMIC_INIT_END;
	return TSMP_CALLOC(FieldLock);
}


static void DestroyFieldLock(FieldLock *lock)
{
	estrDestroy(&lock->fieldPath);
	TSMP_FREE(FieldLock,lock);
}

TSMP_DEFINE(ContainerLock);


static ContainerLock *CreateContainerLock(void)
{
	ContainerLock *lock;
	ATOMIC_INIT_BEGIN;
	TSMP_SMART_CREATE(ContainerLock, 1024, TSMP_X64_RECOMMENDED_CHUNK_SIZE);
	ATOMIC_INIT_END;
	lock = TSMP_CALLOC(ContainerLock);
	return lock;
}


static void DestroyContainerLock(ContainerLock *lock)
{
	eaDestroyEx(&lock->fieldLocks,DestroyFieldLock);

	TSMP_FREE(ContainerLock,lock);
}


ContainerLock *FindContainerLock(GlobalType containerType, U32 containerID)
{
	ContainerLock *lock = NULL;
	EnterBackupAndLockTablesCriticalSection();
	if (gObjectTransactionManager.lockTables[containerType])
	{
		stashIntFindPointer(gObjectTransactionManager.lockTables[containerType],containerID,&lock);
	}
	LeaveBackupAndLockTablesCriticalSection();
	return lock;
}


static ContainerLock *AddContainerLock(GlobalType containerType, U32 containerID)
{
	ContainerLock *lock = FindContainerLock(containerType,containerID);
	if (lock)
	{
		return lock;
	}

	lock = CreateContainerLock();
	lock->containerType = containerType;
	lock->containerID = containerID;

	EnterBackupAndLockTablesCriticalSection();
	if (!gObjectTransactionManager.lockTables[containerType])
	{
		char temp[64];
		gObjectTransactionManager.lockTables[containerType] = stashTableCreateInt(1024);
		sprintf(temp, "Locks_%s", GlobalTypeToName(containerType));
		resRegisterDictionaryForStashTable(allocAddString(temp), 
			"System", 0, gObjectTransactionManager.lockTables[containerType], parse_ContainerLock);
	}
	if(!stashIntAddPointer(gObjectTransactionManager.lockTables[containerType],containerID,lock,false))
	{
		assertmsgf(0, "Container %d, id %d was already locked.", containerType, containerID);
	}
	LeaveBackupAndLockTablesCriticalSection();
	return lock;
}


void RemoveContainerLock(GlobalType containerType, U32 containerID)
{
	ContainerLock *lock = NULL;
	EnterBackupAndLockTablesCriticalSection();
	if (gObjectTransactionManager.lockTables[containerType])
	{
		stashIntRemovePointer(gObjectTransactionManager.lockTables[containerType],containerID,&lock);
	}
	LeaveBackupAndLockTablesCriticalSection();
	if (lock)
	{
		DestroyContainerLock(lock);
	}
}


TSMP_DEFINE(ContainerBackup);


static ContainerBackup *CreateContainerBackup(void)
{
	ContainerBackup *backup;
	ATOMIC_INIT_BEGIN;
	TSMP_SMART_CREATE(ContainerBackup, 1024, TSMP_X64_RECOMMENDED_CHUNK_SIZE);
	ATOMIC_INIT_END;
	backup = TSMP_CALLOC(ContainerBackup);
	return backup;
}


static void DestroyContainerBackup(ContainerBackup *backup)
{
	if (backup->containerBackup)
	{
		//if (backup->isSparse)
		//{
		//	eaDestroyEString(&backup->backedFields);
		//}

		objDestroyContainer(backup->containerBackup);
	}

	TSMP_FREE(ContainerBackup,backup);
}


ContainerBackup *FindContainerBackup(U32 trID, GlobalType containerType, U32 containerID)
{
	ContainerBackup *backup = NULL;
	EnterBackupAndLockTablesCriticalSection();
	if (gObjectTransactionManager.backupTables[containerType])
	{
		stashIntFindPointer(gObjectTransactionManager.backupTables[containerType],containerID,&backup);
	}
	LeaveBackupAndLockTablesCriticalSection();
	return backup;
}


ContainerBackup *AddContainerBackupEx(U32 trID, GlobalType containerType, U32 containerID, bool deleted, bool sparse)
{
	ContainerBackup *backup = FindContainerBackup(trID,containerType,containerID);
	Container *object = deleted ? objGetDeletedContainer(containerType,containerID) : objGetContainer(containerType, containerID);
	ContainerSchema *newClass = objFindContainerSchema(containerType);
	Container *newContainer;

	PERFINFO_AUTO_START("AddContainerBackup",1);

	if (backup)
	{
		if (backup->isSparse && !sparse && object)
		{	//change to a non-sparse backup.
			newContainer = backup->containerBackup;
			StructCopyFieldsVoid(newContainer->containerSchema->classParse,object->containerData,newContainer->containerData,0,0);
			backup->isSparse = false;
		}
		//If a container exists but is not used for a lock, take ownership.
		if (backup->lockOwnerId == 0) backup->lockOwnerId = trID;

		backup->lockExtra++;
		PERFINFO_AUTO_STOP();
		return backup;
	}
	
	backup = CreateContainerBackup();
	backup->containerType = containerType;
	backup->containerID = containerID;
	backup->lockOwnerId = trID;
	backup->isSparse = sparse;

	newContainer = backup->containerBackup = objCreateContainer(newClass);
	newContainer->isTemporary = true;
	newContainer->containerData = objCreateTempContainerObject(newClass, "Creating newContainer in AddContainerBackupEx");
	newContainer->containerID = containerID;
	
	if (object)
	{	//This copies the entire container...
		objChangeContainerState(newContainer, object->meta.containerState, 
			object->meta.containerOwnerType, object->meta.containerOwnerID);

		if (!sparse)
		{
			StructCopyFieldsVoid(newContainer->containerSchema->classParse,object->containerData,newContainer->containerData,0,0);
		}
		else
		{
			//eaCreate(&backup->backedFields);
		}
	}
	else
	{
		objChangeContainerState(newContainer, CONTAINERSTATE_UNKNOWN, 0, 0);
		backup->containerCreated = true;

		objSetNewContainerID(newContainer,containerID,-1);
	}

	EnterBackupAndLockTablesCriticalSection();
	if (!gObjectTransactionManager.backupTables[containerType])
	{
		gObjectTransactionManager.backupTables[containerType] = stashTableCreateInt(1024);
	}
	stashIntAddPointer(gObjectTransactionManager.backupTables[containerType],containerID,backup,false);
	LeaveBackupAndLockTablesCriticalSection();

	PERFINFO_AUTO_STOP();
	return backup;
}

void BackupField(U32 trID, GlobalType containerType, U32 containerID, char *path)
{
	char *estrPath = NULL;

	ContainerBackup *backup = FindContainerBackup(trID,containerType,containerID);
	Container *object = objGetContainer(containerType,containerID);
	ContainerSchema *newClass = objFindContainerSchema(containerType);
	
	assert(backup);

	//if not sparse, then the entire container is already backedup.
	if (!backup->isSparse)
		return;

	StructCopyFieldVoid(newClass->classParse, object->containerData, backup->containerBackup->containerData, path, 0, 0, 0);
}


int RemoveContainerBackup(U32 trID, GlobalType containerType, U32 containerID)
{
	bool bRemoveSuccessful;
	ContainerBackup *backup = FindContainerBackup(trID, containerType,containerID);
	if (!backup)
	{
		return -1;
	}
	PERFINFO_AUTO_START("RemoveContainerBackup",1);
	if (backup->lockExtra > 0)
	{
		backup->lockExtra--;
		PERFINFO_AUTO_STOP();
		return backup->lockExtra + 1;
	}

	//XXXXXX:Destroying the backup is wasteful allocation and slow. We could keep it around...?
	//if ( true ) {
	//	backup->lockOwnerId = 0;
	//	PERFINFO_AUTO_STOP();
	//	return 0;
	//}

	EnterBackupAndLockTablesCriticalSection();
	bRemoveSuccessful = stashIntRemovePointer(gObjectTransactionManager.backupTables[containerType],containerID,&backup);
	LeaveBackupAndLockTablesCriticalSection();

	if (bRemoveSuccessful)
	{
		DestroyContainerBackup(backup);

		PERFINFO_AUTO_STOP();
		return 0;
	}
	else
	{
		PERFINFO_AUTO_STOP();
		return -1;
	}
}

FieldLock *FindFieldLock(ContainerLock *container, char *path)
{
	int i;
	size_t len = strlen(path);
	if (!container)
	{
		return NULL;
	}
	for (i = 0; i < eaSize(&container->fieldLocks); i++)
	{
		FieldLock *lock = container->fieldLocks[i];
		if (len != lock->pathLength)
		{
			continue;
		}
		if (stricmp(path,lock->fieldPath) == 0)
		{
			return lock;
		}
	}
	return NULL;
}

// Return if there are any conflicting paths
bool CheckForConflictingFieldLocks(ContainerLock *container, char *path, U32 trID, U32 *blockingID)
{
	int pathLength = (int)strlen(path);
	int i;
	if (!container)
	{
		return false;
	}
	for (i = 0; i < eaSize(&container->fieldLocks); i++)
	{
		FieldLock *lock = container->fieldLocks[i];
		if (pathLength <= lock->pathLength)
		{
			// Check if proposed path is parent
			if (strnicmp(path,lock->fieldPath,pathLength) == 0 &&
				lock->lockOwnerId > 0 && lock->lockOwnerId != trID)
			{
				*blockingID = lock->lockOwnerId;
				return true;
			}
		}
		else
		{
			// Check if proposed path is child
			if (strnicmp(path,lock->fieldPath,lock->pathLength) == 0 &&
				lock->lockOwnerId > 0 && lock->lockOwnerId != trID)
			{
				*blockingID = lock->lockOwnerId;
				return true;
			}
		}
		
	}
	return false;
}

// Sort longer paths at the end
/*static int SortFieldLockPathLength(const FieldLock ** a, const FieldLock** b)
{
	if (!a || !*a)
	{
		if (!b || !*b)
		{
			return 0;
		}
		return 1;
	}
	if (!b || !*b)
	{
		return -1;
	}
	return (*b)->pathLength - (*a)->pathLength;
}*/

static FieldLock *AddFieldLock(ContainerLock *container, char *path)
{
	FieldLock *lock = FindFieldLock(container,path);
	if (lock)
	{
		return lock;
	}

	lock = CreateFieldLock();
	estrBufferCreate(&lock->fieldPath, lock->fieldPathBuffer, sizeof(lock->fieldPathBuffer));
	estrCopy2(&lock->fieldPath, path);
	lock->pathLength = (int)strlen(path);

	//eaInsert(&container->fieldLocks,lock,(int)eaBFind(container->fieldLocks,SortFieldLockPathLength,lock));
	eaPush(&container->fieldLocks,lock);

	return lock;
}


void RemoveFieldLock(ContainerLock *container, char *path)
{
	int i;
	size_t len = strlen(path);
	if (!container)
	{
		return;
	}
	PERFINFO_AUTO_START_FUNC();
	for (i = 0; i < eaSize(&container->fieldLocks); i++)
	{
		FieldLock *lock = container->fieldLocks[i];
		if (len != lock->pathLength)
		{
			continue;
		}
		if (stricmp(path,lock->fieldPath) == 0)
		{
			eaRemoveFast(&container->fieldLocks,i);
			DestroyFieldLock(lock);
		}
	}
	PERFINFO_AUTO_STOP();
	return;
}


Container *GetLockedContainerEx(GlobalType containerType, U32 containerID, U32 ownerID, bool deleted, bool assertIfSparse, bool forceUnpack)
{
	ContainerLock *conLock = FindContainerLock(containerType,containerID);
	ContainerBackup *conBackup = FindContainerBackup(ownerID,containerType,containerID);
	if (!conLock || !conBackup || !conLock->lockOwnerId)
	{
		return objGetContainerGeneralEx(containerType, containerID, forceUnpack, false, false, deleted);
	}
	if (conLock->containerRemoved)
	{
		return NULL;
	}
	assertmsg(!(assertIfSparse && conBackup->isSparse), "Full container backup is being requested from a sparse container.");
	return conBackup->containerBackup;
}


void DeleteLockedContainer(GlobalType containerType, U32 containerID)
{
	ContainerLock *conLock = FindContainerLock(containerType,containerID);
	if (!conLock || !conLock->lockOwnerId)
	{
		return;
	}
	conLock->containerRemoved = true;
}

int IsContainerBeingCreated(GlobalType containerType, U32 containerID, U32 ownerID)
{
	ContainerLock *conLock = FindContainerLock(containerType,containerID);
	ContainerBackup *conBackup = FindContainerBackup(ownerID,containerType,containerID);
	if (!conLock || !conBackup || !conLock->lockOwnerId)
	{
		return 0;
	}
	if (conLock->containerRemoved)
	{
		return 0;
	}	
	return conBackup->containerCreated;

}

Container *GetBaseContainer(GlobalType containerType, U32 containerID, U32 ownerID)
{
	ContainerBackup *conLock = FindContainerBackup(ownerID,containerType,containerID);
	Container *baseContainer = NULL;

	if (!conLock || !conLock->containerBackup)
	{
		baseContainer = objGetContainer(containerType,containerID);
	}
	else
	{
		baseContainer = conLock->containerBackup;
	}

	return baseContainer;
}

int GetLockedField(Container* baseContainer, const char *path, ParseTable** table, int* column, void** structptr,
				   int* index, char **resultString, char **diffString)
{
	bool result;
	int pathFlags = OBJPATHFLAG_DONTLOOKUPROOTPATH;

	result = ParserResolvePathEx(path,baseContainer->containerSchema->classParse,baseContainer->containerData,
		table, column, structptr, index, NULL, resultString, NULL, diffString, pathFlags);
	if (!result || (column && *column < -1))
	{
	
		return false;
	}
	if (!((*table)[*column].type & TOK_PERSIST) || ((*table)[*column].type & TOK_NO_TRANSACT))
	{
		if (resultString) estrPrintf(resultString, "Can't lock non-transacted field %s", path);
		Errorf("Can't lock non-transacted field %s, please tell a programmer what you were just doing", path);
		return false; // Can't lock a non-transacted field
	}	
	return true;
}


bool CanFieldBeLocked(U32 trID, GlobalType containerType, U32 containerID, char *path, U32 *blockingID)
{
	ContainerLock *conLock;
	FieldLock *fieldLock;
	Container *container;

	PERFINFO_AUTO_START("CanFieldBeLocked",1);

	conLock = FindContainerLock(containerType,containerID);

	if (!conLock)
	{
		// If no lock entry for this container, we know it's not locked
		PERFINFO_AUTO_STOP();
		return true;
	}
	if (conLock->lockOwnerId > 0)
	{
		// If the object as a whole is locked, even by same transaction, don't allow it
		// We may change this later
		*blockingID = conLock->lockOwnerId;
		PERFINFO_AUTO_STOP();
		return false;
	}
	fieldLock = FindFieldLock(conLock,path);
	if (!fieldLock)
	{
		// Make sure there are no conflicting field locks
		bool val = !CheckForConflictingFieldLocks(conLock,path,trID,blockingID);
		PERFINFO_AUTO_STOP();
		return val;
	}
	if (fieldLock->lockOwnerId > 0 && fieldLock->lockOwnerId != trID)
	{
		// Let us lock it if we're the same transaction
		PERFINFO_AUTO_STOP();
		*blockingID = fieldLock->lockOwnerId;
		return false;
	}
	if (CheckForConflictingFieldLocks(conLock,path,trID,blockingID))
	{
		// A parent or child of desired lock is locked by another transaction
		PERFINFO_AUTO_STOP();
		return false;
	}
	container = GetLockedContainerEx(containerType,containerID,trID, false, false, true);
	if (!container)
	{
		*blockingID = trID;
		PERFINFO_AUTO_STOP();
		return false;
	}
	PERFINFO_AUTO_STOP();
	return true;

}

void LockField(U32 trID, GlobalType containerType, U32 containerID, char *path)
{
	FieldLock *fieldLock;
	ContainerLock *conLock;
	
	PERFINFO_AUTO_START_FUNC();

	conLock = AddContainerLock(containerType,containerID);
	fieldLock = AddFieldLock(conLock,path);
	if (fieldLock->lockOwnerId > 0)
	{
		assert(fieldLock->lockOwnerId == trID);
		fieldLock->lockExtra++;
	}
	else
	{
		fieldLock->lockOwnerId = trID;
		fieldLock->lockExtra = 0;
	}
	PERFINFO_AUTO_STOP();
}


int UnlockField(U32 trID, GlobalType containerType, U32 containerID, char *path)
{
	ContainerLock *conLock;
	FieldLock *fieldLock;

	PERFINFO_AUTO_START("UnlockField",1);

	conLock = FindContainerLock(containerType,containerID);
	fieldLock = FindFieldLock(conLock,path);
	assert(fieldLock && fieldLock->lockOwnerId == trID);

	if (fieldLock->lockExtra > 0)
	{
		fieldLock->lockExtra--;
		PERFINFO_AUTO_STOP();
		return fieldLock->lockExtra + 1;
	}
	else
	{
		fieldLock->lockOwnerId = 0;
		RemoveFieldLock(conLock,path);
	}
	if (!eaSize(&conLock->fieldLocks) && conLock->lockOwnerId == 0)
	{
		RemoveContainerLock(containerType,containerID);
	}
	PERFINFO_AUTO_STOP();
	return 0;
}

bool CanContainerBeLocked(U32 trID, GlobalType containerType, U32 containerID, U32 *blockingID)
{
	ContainerLock *conLock;
	
	PERFINFO_AUTO_START("CanContainerBeLocked",1);

	conLock = FindContainerLock(containerType,containerID);
	if (!conLock)
	{
		// If no lock entry for this container, we know it's not locked
		PERFINFO_AUTO_STOP();
		return true;
	}
	if (conLock->lockOwnerId > 0 && conLock->lockOwnerId != trID)
	{
		// If the object as a whole is locked, even by same transaction, don't allow it
		// We may change this later
		PERFINFO_AUTO_STOP();
		*blockingID = conLock->lockOwnerId;
		return false;
	}
	if (eaSize(&conLock->fieldLocks) > 0)
	{
		// A particular field is locked, disallow full container lock
		PERFINFO_AUTO_STOP();
		*blockingID = conLock->fieldLocks[0]->lockOwnerId;

		return false;
	}

	PERFINFO_AUTO_STOP();
	return true;
}


void LockContainer(U32 trID, GlobalType containerType, U32 containerID)
{
	ContainerLock *conLock;

	PERFINFO_AUTO_START("LockContainer",1);

	conLock = AddContainerLock(containerType,containerID);
	if (conLock->lockOwnerId > 0)
	{
		assert(conLock->lockOwnerId == trID);
		conLock->lockExtra++;
	}
	else
	{
		conLock->lockOwnerId = trID;
		conLock->lockExtra = 0;		
	}
	if(gStoreObjLockMemlog)
		memlog_printf(&objlockmemlog, "Locking %s[%u]. (%d locks)", GlobalTypeToName(containerType), containerID, conLock->lockExtra + 1);
	PERFINFO_AUTO_STOP();
}


int UnlockContainer(U32 trID, GlobalType containerType, U32 containerID)
{
	ContainerLock *conLock;
	
	PERFINFO_AUTO_START("UnlockContainer",1);

	conLock = FindContainerLock(containerType,containerID);

	if (!conLock) // The lock must have failed
	{
		PERFINFO_AUTO_STOP();
		return 0;
	}

	assert(conLock->lockOwnerId == trID);

	if (conLock->lockExtra > 0)
	{
		conLock->lockExtra--;
		if(gStoreObjLockMemlog)
			memlog_printf(&objlockmemlog, "Unlocking %s[%u]. (%d locks)", GlobalTypeToName(containerType), containerID, conLock->lockExtra + 1);
		PERFINFO_AUTO_STOP();
		return conLock->lockExtra + 1;
	}
	else
	{
		conLock->lockOwnerId = 0;
	}
	if (!eaSize(&conLock->fieldLocks) && conLock->lockOwnerId == 0)
	{
		RemoveContainerLock(containerType,containerID);
	}
	if(gStoreObjLockMemlog)
		memlog_printf(&objlockmemlog, "Unlocking %s[%u]. (0 locks)", GlobalTypeToName(containerType), containerID);
	PERFINFO_AUTO_STOP();
	return 0;
}

#include "ObjLocks_h_ast.c"
