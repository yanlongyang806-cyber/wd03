/***************************************************************************



***************************************************************************/

#ifndef OBJLOCKS_H_
#define OBJLOCKS_H_
// Functions for handling locks and backups

#include "objContainer.h"

// Stores the information about a particular locked field

//AUTO_STRUCTed purely for servermonitoring, don't assume you can structCreate/StructDestroy
AUTO_STRUCT;
typedef struct FieldLock
{
	char* fieldPath; // path string (buffer EString using the following variable)
	char fieldPathBuffer[300];
	int pathLength; // length in bytes

	U32 lockOwnerId; // not actually locked if this is 0
	int lockExtra; // additional locks from same transaction
} FieldLock;


// Stores the information about a particular locked container
//AUTO_STRUCTed purely for servermonitoring, don't assume you can StructCreate/StructDestroy
AUTO_STRUCT;
typedef struct ContainerLock
{
	GlobalType containerType; // Identifies container
	U32 containerID; AST(KEY)
	bool containerRemoved; // Does this container need to be removed on commit?

	FieldLock **fieldLocks; // The locked fields

	U32 lockOwnerId; //if this is 0, not actually locked
	int lockExtra; // additional locks from same transaction
} ContainerLock;

typedef struct ContainerBackup
{
	GlobalType containerType; // Identifies container
	U32 containerID; 

	Container *containerBackup;
	bool isSparse;
	bool containerCreated; // Was this container nonexistant prior to transaction?

	U32 lockOwnerId; //if this is 0, not actually locked
	int lockExtra; // additional locks from same transaction
} ContainerBackup;


// Internal Utility Functions
ContainerLock *FindContainerLock(GlobalType containerType, U32 containerID);
void RemoveContainerLock(GlobalType containerType, U32 containerID);

ContainerBackup *FindContainerBackup(U32 trID, GlobalType containerType, U32 containerID);
ContainerBackup *AddContainerBackupEx(U32 trID, GlobalType containerType, U32 containerID, bool deleted, bool sparse);
#define AddDeletedContainerBackup(trID, containerType, containerID) AddContainerBackupEx(trID, containerType, containerID, true, false)
#define AddContainerBackup(trID, containerType, containerID) AddContainerBackupEx(trID, containerType, containerID, false, false)
#define AddSparseContainerBackup(trID, containerType, containerID) AddContainerBackupEx(trID, containerType, containerID, false, true)
int RemoveContainerBackup(U32 trID, GlobalType containerType, U32 containerID);

// note that after creating a lock, all path matching is based on 
// POINTER COMPARISON, so the string pointer must not change
FieldLock *FindFieldLock(ContainerLock *container, char *path);
void RemoveFieldLock(ContainerLock *container, char *path);

// Specific lock operations for fields

bool CanFieldBeLocked(U32 trID, GlobalType containerType, U32 containerID, char *path, U32 *blockingID);
void LockField(U32 trID, GlobalType containerType, U32 containerID, char *path);
int UnlockField(U32 trID, GlobalType containerType, U32 containerID, char *path);
void BackupField(U32 trID, GlobalType containerType, U32 containerID, char *path);

// Specific lock operations for containers

bool CanContainerBeLocked(U32 trID, GlobalType containerType, U32 containerID, U32 *blockingID);
void LockContainer(U32 trID, GlobalType containerType, U32 containerID);
int UnlockContainer(U32 trID, GlobalType containerType, U32 containerID);



#endif
