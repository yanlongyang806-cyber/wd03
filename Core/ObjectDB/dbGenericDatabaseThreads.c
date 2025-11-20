/***************************************************************************
*     Copyright (c) 2005-2006, Cryptic Studios
*     All Rights Reserved
*     Confidential Property of Cryptic Studios
***************************************************************************/

#include "dbCharacterChoice.h"
#include "dbGenericDatabaseThreads.h"
#include "dbLocalTransactionManager.h"
#include "dbLogin2.h"
#include "dbOfflining.h"
#include "dbSubscribe.h"
#include "file.h"
#include "LoginCommon.h"
#include "mathutil.h"
#include "ObjectDB.h"
#include "ThreadSafeMemoryPool.h"
#include "UtilitiesLib.h"

#include "AutoGen/AppServerLib_autogen_RemoteFuncs.h"
#include "AutoGen/ChatServer_autogen_RemoteFuncs.h"

GenericWorkerThreadManager *GenericDatabaseThreadManager;

U32 gGenericDatabaseThreadCount = 1;
AUTO_CMD_INT(gGenericDatabaseThreadCount, GenericDatabaseThreadCount) ACMD_CMDLINE ACMD_CALLBACK(ChangeGenericDatabaseThreadCount);

U32 gGenericDatabaseThreadCountUsingDefault = true;

void ChangeGenericDatabaseThreadCount(CMDARGS)
{
	gGenericDatabaseThreadCountUsingDefault = false;
}

U32 gGenericDatabaseThreadQueueSize = 50000;
AUTO_CMD_INT(gGenericDatabaseThreadQueueSize, GenericDatabaseThreadQueueSize) ACMD_CMDLINE;

// Struct representing a request to be queued to the generic DB threads

// If you need to send a request bigger than this, consider passing a pointer instead
#define GENERIC_DB_REQUEST_MAX_SIZE 120

typedef struct GenericDatabaseRequest GenericDatabaseRequest;
typedef struct GenericDatabaseRequest
{
	// Singly-linked list pointer
	GenericDatabaseRequest *next;

	// Information about the command to be queued
	int cmd; // i.e. CMD_DB_*
	int numLocks; // How many ObjectLocks are associated

	union
	{
		// NULL if numLocks == 0
		ObjectLock *single; // numLocks == 1
		ObjectLock **array; // numLocks > 1
	} lock;

	int dataSize;
	U8 data[GENERIC_DB_REQUEST_MAX_SIZE]; // Data is copied flat here to avoid doing more allocs than necessary
} GenericDatabaseRequest;

TSMP_DEFINE(GenericDatabaseRequest);

// Linked list of generic DB thread requests
GenericDatabaseRequest *genericDatabaseRequestHead = NULL;
GenericDatabaseRequest *genericDatabaseRequestTail = NULL;
int genericDatabaseOutstandingRequestCount = 0;
TimingHistory *genericDatabaseThreadFullnessHistory = NULL;

int GetOutstandingGenericDatabaseRequestCount(void)
{
	return genericDatabaseOutstandingRequestCount;
}

static int sendOneGenericDatabaseRequest(void)
{
	GenericDatabaseRequest *request = genericDatabaseRequestHead;
	int result = 0;

	if (!request)
	{
		return 0;
	}

	if (request->numLocks > 1)
	{
		result = gwtQueueCmd_ObjectLockArray(GenericDatabaseThreadManager, request->cmd, request->data, request->dataSize, request->lock.array);
	}
	else if (request->numLocks == 1)
	{
		result = gwtQueueCmd_ObjectLock(GenericDatabaseThreadManager, request->cmd, request->data, request->dataSize, 1, request->lock.single);
	}
	else
	{
		result = gwtQueueCmd_ObjectLock(GenericDatabaseThreadManager, request->cmd, request->data, request->dataSize, 0);
	}

	if (result)
	{
		if (request->numLocks > 1)
		{
			eaDestroy(&request->lock.array);
		}

		genericDatabaseRequestHead = genericDatabaseRequestHead->next;

		if (!genericDatabaseRequestHead)
		{
			genericDatabaseRequestTail = NULL;
		}

		TSMP_FREE(GenericDatabaseRequest, request);
		--genericDatabaseOutstandingRequestCount;
	}

	return result;
}

static void sendGenericDatabaseRequests(void)
{
	if (!genericDatabaseRequestHead)
	{
		return;
	}

	PERFINFO_AUTO_START_FUNC();
	while (genericDatabaseRequestHead)
	{
		if (!sendOneGenericDatabaseRequest())
		{
			ADD_MISC_COUNT(1, "Blocked");
			break;
		}

		ADD_MISC_COUNT(1, "Requests queued");
	}

	if (genericDatabaseOutstandingRequestCount)
	{
		ADD_MISC_COUNT(genericDatabaseOutstandingRequestCount, "Outstanding requests");
	}
	PERFINFO_AUTO_STOP();
}

static GenericDatabaseRequest *queueGenericDatabaseRequest_internal(int cmd, void *data, int size)
{
	GenericDatabaseRequest *request = NULL;

	ATOMIC_INIT_BEGIN;
	TSMP_SMART_CREATE(GenericDatabaseRequest, 512, TSMP_X64_RECOMMENDED_CHUNK_SIZE);
	ATOMIC_INIT_END;

	request = TSMP_CALLOC(GenericDatabaseRequest);
	request->cmd = cmd;

	if (size)
	{
		assert(size <= GENERIC_DB_REQUEST_MAX_SIZE);
		request->dataSize = size;
		memcpy(request->data, data, size);
	}

	if (genericDatabaseRequestTail)
	{
		genericDatabaseRequestTail->next = request;
		genericDatabaseRequestTail = request;
	}
	else
	{
		genericDatabaseRequestHead = genericDatabaseRequestTail = request;
	}

	++genericDatabaseOutstandingRequestCount;

	return request;
}

static void queueGenericDatabaseRequestSingleLock(int cmd, void *data, int size, ObjectLock *lock)
{
	GenericDatabaseRequest *request = queueGenericDatabaseRequest_internal(cmd, data, size);

	request->numLocks = lock ? 1 : 0;
	request->lock.single = lock;
}

static void queueGenericDatabaseRequestMultipleLocks(int cmd, void *data, int size, ObjectLock ***locks)
{
	GenericDatabaseRequest *request = queueGenericDatabaseRequest_internal(cmd, data, size);

	assert(locks);
	request->numLocks = eaSize(locks);

	if (request->numLocks > 1)
	{
		request->lock.array = (*locks);
		(*locks) = NULL;
	}
	else
	{
		if (request->numLocks == 1)
		{
			request->lock.single = (*locks)[0];
		}

		eaDestroy(locks);
	}
}

#define GDB_VERIFY_STRUCT(data, type) (1/(sizeof(data)==sizeof(type)))
#define GDB_VERIFY_POINTER(data, type) (1/((sizeof(data)==sizeof(type*)) && (sizeof(*data)==sizeof(type))))

#define queueGenericDatabaseRequest_Struct(cmd, data, type, lock) (GDB_VERIFY_STRUCT(data, type), queueGenericDatabaseRequestSingleLock(cmd, &(data), sizeof(type), lock))
#define queueGenericDatabaseRequest_Pointer(cmd, data, type, lock) (GDB_VERIFY_POINTER(data, type), queueGenericDatabaseRequestSingleLock(cmd, &(data), sizeof(type*), lock))
#define queueGenericDatabaseRequest_Struct_MultipleLocks(cmd, data, type, array) (GDB_VERIFY_STRUCT(data, type), queueGenericDatabaseRequestMultipleLocks(cmd, &(data), sizeof(type), array))
#define queueGenericDatabaseRequest_Pointer_MultipleLocks(cmd, data, type, array) (GDB_VERIFY_POINTER(data, type), queueGenericDatabaseRequestMultipleLocks(cmd, &(data), sizeof(type*), array))

void dbUpdateContainerThreaded(void *user_data, void *data, GWTCmdPacket *packet)
{
	ContainerUpdateInfo *info = (ContainerUpdateInfo*)data;
	dbUpdateContainer_CB(packet, info, false);
}

void dbUpdateContainerOwnerThreaded(void *user_data, void *data, GWTCmdPacket *packet)
{
	ContainerOwnerUpdateInfo *info = (ContainerOwnerUpdateInfo*)data;
	dbUpdateContainerOwner_CB(packet, info, false);
}

void dbWriteContainerThreaded(void *user_data, void *data, GWTCmdPacket *packet)
{
	ContainerUpdateInfo *info = (ContainerUpdateInfo*)data;
	dbWriteContainer_CB(info);
}

void dbDestroyContainerThreaded(void *user_data, void *data, GWTCmdPacket *packet)
{
	ContainerUpdateInfo *info = (ContainerUpdateInfo*)data;
	dbDestroyContainer_CB(packet, info, false);
}

void dbDestroyDeletedContainerThreaded(void *user_data, void *data, GWTCmdPacket *packet)
{
	ContainerUpdateInfo *info = (ContainerUpdateInfo*)data;
	dbDestroyDeletedContainer_CB(packet, info, false);
}

void dbDeleteContainerThreaded(void *user_data, void *data, GWTCmdPacket *packet)
{
	ContainerDeleteInfo *info = (ContainerDeleteInfo*)data;
	dbDeleteContainer_CB(packet, info, false);
}

void dbUndeleteContainerThreaded(void *user_data, void *data, GWTCmdPacket *packet)
{
	ContainerUndeleteInfo *info = (ContainerUndeleteInfo*)data;
	dbUndeleteContainer_CB(info, false);
}

void dbUndeleteContainerWithRenameThreaded(void *user_data, void *data, GWTCmdPacket *packet)
{
	ContainerUndeleteInfo *info = (ContainerUndeleteInfo*)data;
	dbUndeleteContainerWithRename_CB(info, false);
}

void dbWriteContainerToOfflineHoggThreaded(void *user_data, void *data, GWTCmdPacket *packet)
{
	ContainerUpdateInfo *info = (ContainerUpdateInfo*)data;
	dbWriteContainerToOfflineHogg_CB(info, false);
	gwtQueueMsg(packet, MSG_DB_DECREMENT_OFFLINE_COUNT, NULL, 0);
}

void dbRemoveContainerFromOfflineHoggThreaded(void *user_data, void *data, GWTCmdPacket *packet)
{
	ContainerUpdateInfo *info = (ContainerUpdateInfo*)data;
	dbRemoveContainerFromOfflineHogg_CB(info, false);
	gwtQueueMsg(packet, MSG_DB_DECREMENT_OFFLINE_CLEANUP_COUNT, NULL, 0);
}

void dbAddOfflineEntityPlayerToAccountStubThreaded(void *user_data, void *data, GWTCmdPacket *packet)
{
	ContainerAccountStubInfo *info = (ContainerAccountStubInfo*)data;
	dbAddOfflineEntityPlayerToAccountStub_CB(info);
	gwtQueueMsg(packet, MSG_DB_DECREMENT_ACCOUNT_STUB_OPERATIONS, NULL, 0);
}

void dbMarkEntityPlayerRestoredInAccountStubThreaded(void *user_data, void *data, GWTCmdPacket *packet)
{
	ContainerMiniAccountStubInfo *info = (ContainerMiniAccountStubInfo*)data;
	dbMarkEntityPlayerRestoredInAccountStub_CB(info);
}

void dbRemoveOfflineEntityPlayerFromAccountStubThreaded(void *user_data, void *data, GWTCmdPacket *packet)
{
	ContainerMiniAccountStubInfo *info = (ContainerMiniAccountStubInfo*)data;
	dbRemoveOfflineEntityPlayerFromAccountStub_CB(info);
	gwtQueueMsg(packet, MSG_DB_DECREMENT_ACCOUNT_STUB_OPERATIONS, NULL, 0);
}

void dbAddOfflineAccountWideContainerToAccountStubThreaded(void *user_data, void *data, GWTCmdPacket *packet)
{
	AccountWideContainerAccountStubInfo *info = (AccountWideContainerAccountStubInfo*)data;
	dbAddOfflineAccountWideContainerToAccountStub_CB(info);
}

void dbMarkAccountWideContainerRestoredInAccountStubThreaded(void *user_data, void *data, GWTCmdPacket *packet)
{
	AccountWideContainerAccountStubInfo *info = (AccountWideContainerAccountStubInfo*)data;
	dbMarkAccountWideContainerRestoredInAccountStub_CB(info);
}

void dbRemoveOfflineAccountWideContainerFromAccountStubThreaded(void *user_data, void *data, GWTCmdPacket *packet)
{
	AccountWideContainerAccountStubInfo *info = (AccountWideContainerAccountStubInfo*)data;
	dbRemoveOfflineAccountWideContainerFromAccountStub_CB(info);
}

void dbSubscribeToContainerThreaded(void *user_data, void *data, GWTCmdPacket *packet)
{
	SubscribeToContainerInfo *info = (SubscribeToContainerInfo*)data;
	dbSubscribeToContainer_CB(packet, info);
}

void dbUnsubscribeFromContainerThreaded(void *user_data, void *data, GWTCmdPacket *packet)
{
	SubscribeToContainerInfo *info = (SubscribeToContainerInfo*)data;
	dbUnsubscribeFromContainer_CB(info);
}

void dbFreeSubscribeCacheCopy(void *user_data, void *data, GWTCmdPacket *packet)
{
	CachedSubscriptionCopy *copy = *(CachedSubscriptionCopy**)data;
	DestroyCachedSubscription(copy);
}

void dbHandleNewTransactionThreaded(void *user_data, void *data, GWTCmdPacket *packet)
{
	dbHandleNewTransaction_Data *transactionData = (dbHandleNewTransaction_Data*)data;
	dbHandleNewTransactionCB(packet, transactionData);
}

void dbHandleCancelTransactionThreaded(void *user_data, void *data, GWTCmdPacket *packet)
{
	dbHandleCancelTransaction_Data *transactionData = (dbHandleCancelTransaction_Data*)data;
	dbHandleCancelTransactionCB(transactionData);
}

void dbHandleConfirmTransactionThreaded(void *user_data, void *data, GWTCmdPacket *packet)
{
	dbHandleConfirmTransaction_Data *transactionData = (dbHandleConfirmTransaction_Data*)data;
	dbHandleConfirmTransactionCB(transactionData);
}

void PerformGuildCleanup(ContainerID guildID, ContainerID playerID)
{
	RemoteCommand_aslGuild_RemoveDeletedPlayer(GLOBALTYPE_GUILDSERVER, 0, guildID, playerID);
}

void dbDoGuildCleanup(void *user_data, void *data, GWTCmdPacket *packet)
{
	GuildCleanupInfo *info = (GuildCleanupInfo*)data;
	PerformGuildCleanup(info->guildID, info->playerID);
}

void PerformChatServerCleanup(ContainerID accountID, ContainerID playerID)
{
	EARRAY_OF(PossibleCharacterChoice) eaCharacters = NULL;
	ContainerID *eaIDs = GetContainerIDsFromAccountID(accountID);
	if (ea32Size(&eaIDs) == 0 || (ea32Size(&eaIDs) == 1 && eaIDs[0] == playerID))
	{
		RemoteCommand_userShardCharactersDeleted(GLOBALTYPE_CHATSERVER, 0, accountID, GetShardNameFromShardInfoString());
	}
	ea32Destroy(&eaIDs);
}

void dbDoChatServerCleanup(void *user_data, void *data, GWTCmdPacket *packet)
{
	ChatServerCleanupInfo *info = (ChatServerCleanupInfo*)data;
	PerformChatServerCleanup(info->accountID, info->playerID);
}

void dbDecrementContainersToOffline(void *user_data, void *data, GWTCmdPacket *packet)
{
	DecrementContainersToOffline();
}

void dbDecrementOfflineContainersToCleanup(void *user_data, void *data, GWTCmdPacket *packet)
{
	DecrementOfflineContainersToCleanup();
}

void dbDecrementOutstandingAccountStubOperations(void *user_data, void *data, GWTCmdPacket *packet)
{
	DecrementOutstandingAccountStubOperations();
}

void dbQueueFreeSubscribeCacheCopy(void *user_data, void *data, GWTCmdPacket *packet)
{
	CachedSubscriptionCopy *copy = *(CachedSubscriptionCopy**)data;
	QueueFreeSubscribeCacheCopyOnGenericDatabaseThreads(copy);
}

void dbUnpackCharacterThreaded(void *user_data, void *data, GWTCmdPacket *packet)
{
	DBGetCharacterDetailState *getDetailstate = *(DBGetCharacterDetailState**)data;
	dbLogin2_UnpackCharacter(packet, getDetailstate);
}

void dbUnpackPetsThreaded(void *user_data, void *data, GWTCmdPacket *packet)
{
	DBGetCharacterDetailState *getDetailstate = *(DBGetCharacterDetailState**)data;
	dbLogin2_UnpackPets(getDetailstate);
}

void dbQueueUnpackPets(void *user_data, void *data, GWTCmdPacket *packet)
{
	DBGetCharacterDetailState *getDetailstate = *(DBGetCharacterDetailState**)data;
	dbLogin2_QueueUnpackPets(getDetailstate);
}

void InitGenericDatabaseThreads(void)
{
	U32 QueueSize = 1 << (highBitIndex(gGenericDatabaseThreadQueueSize - 1) + 1);
	if (isDevelopmentMode() && UserIsInGroup("Software"))
	{
		if(gGenericDatabaseThreadCountUsingDefault)
			gGenericDatabaseThreadCount = 2;
	}

	if (GenericDatabaseThreadIsEnabled())
	{
		EnableLocalTransactions(false);
	}

	GenericDatabaseThreadManager = gwtCreateEx(QueueSize, QueueSize, gGenericDatabaseThreadCount, NULL, "GenericDatabaseThreads", GWT_LOCKSTYLE_OBJECTLOCK);

	gwtRegisterCmdDispatch(GenericDatabaseThreadManager, CMD_DB_UPDATE_CONTAINER, dbUpdateContainerThreaded);
	gwtRegisterCmdDispatch(GenericDatabaseThreadManager, CMD_DB_UPDATE_CONTAINER_OWNER, dbUpdateContainerOwnerThreaded);
	gwtRegisterCmdDispatch(GenericDatabaseThreadManager, CMD_DB_WRITE_CONTAINER, dbWriteContainerThreaded);
	gwtRegisterCmdDispatch(GenericDatabaseThreadManager, CMD_DB_DESTROY_CONTAINER, dbDestroyContainerThreaded);
	gwtRegisterCmdDispatch(GenericDatabaseThreadManager, CMD_DB_DESTROY_DELETED_CONTAINER, dbDestroyDeletedContainerThreaded);
	gwtRegisterCmdDispatch(GenericDatabaseThreadManager, CMD_DB_DELETE_CONTAINER, dbDeleteContainerThreaded);
	gwtRegisterCmdDispatch(GenericDatabaseThreadManager, CMD_DB_UNDELETE_CONTAINER, dbUndeleteContainerThreaded);
	gwtRegisterCmdDispatch(GenericDatabaseThreadManager, CMD_DB_UNDELETE_CONTAINER_WITH_RENAME, dbUndeleteContainerWithRenameThreaded);
	gwtRegisterCmdDispatch(GenericDatabaseThreadManager, CMD_DB_WRITE_CONTAINER_TO_OFFLINE, dbWriteContainerToOfflineHoggThreaded);
	gwtRegisterCmdDispatch(GenericDatabaseThreadManager, CMD_DB_REMOVE_CONTAINER_FROM_OFFLINE, dbRemoveContainerFromOfflineHoggThreaded);
	gwtRegisterCmdDispatch(GenericDatabaseThreadManager, CMD_DB_ADD_OFFLINE_PLAYER_TO_ACCOUNT_STUB, dbAddOfflineEntityPlayerToAccountStubThreaded);
	gwtRegisterCmdDispatch(GenericDatabaseThreadManager, CMD_DB_MARK_ENTITY_PLAYER_RESTORED, dbMarkEntityPlayerRestoredInAccountStubThreaded);
	gwtRegisterCmdDispatch(GenericDatabaseThreadManager, CMD_DB_REMOVE_OFFLINE_ENTITY_PLAYER, dbRemoveOfflineEntityPlayerFromAccountStubThreaded);
	gwtRegisterCmdDispatch(GenericDatabaseThreadManager, CMD_DB_ADD_ACCOUNT_WIDE_TO_ACCOUNT_STUB, dbAddOfflineAccountWideContainerToAccountStubThreaded);
	gwtRegisterCmdDispatch(GenericDatabaseThreadManager, CMD_DB_MARK_ACCOUNT_WIDE_RESTORED, dbAddOfflineAccountWideContainerToAccountStubThreaded);
	gwtRegisterCmdDispatch(GenericDatabaseThreadManager, CMD_DB_REMOVE_ACCOUNT_WIDE_FROM_ACCOUNT_STUB, dbAddOfflineAccountWideContainerToAccountStubThreaded);
	gwtRegisterCmdDispatch(GenericDatabaseThreadManager, CMD_DB_SUBSCRIBE, dbSubscribeToContainerThreaded);
	gwtRegisterCmdDispatch(GenericDatabaseThreadManager, CMD_DB_UNSUBSCRIBE, dbUnsubscribeFromContainerThreaded);
	gwtRegisterCmdDispatch(GenericDatabaseThreadManager, CMD_DB_FREE_SUBSCRIBE_CACHE_COPY, dbFreeSubscribeCacheCopy);
	gwtRegisterCmdDispatch(GenericDatabaseThreadManager, CMD_DB_HANDLE_NEW_TRANSACTION, dbHandleNewTransactionThreaded);
	gwtRegisterCmdDispatch(GenericDatabaseThreadManager, CMD_DB_HANDLE_CANCEL_TRANSACTION, dbHandleCancelTransactionThreaded);
	gwtRegisterCmdDispatch(GenericDatabaseThreadManager, CMD_DB_HANDLE_CONFIRM_TRANSACTION, dbHandleConfirmTransactionThreaded);
	gwtRegisterCmdDispatch(GenericDatabaseThreadManager, CMD_DB_LOGIN2_UNPACK_CHARACTER, dbUnpackCharacterThreaded);
	gwtRegisterCmdDispatch(GenericDatabaseThreadManager, CMD_DB_LOGIN2_UNPACK_PETS, dbUnpackPetsThreaded);

	gwtRegisterMsgDispatch(GenericDatabaseThreadManager, MSG_DB_DO_GUILD_CLEANUP, dbDoGuildCleanup);
	gwtRegisterMsgDispatch(GenericDatabaseThreadManager, MSG_DB_DO_CHAT_SERVER_CLEANUP, dbDoChatServerCleanup);
	gwtRegisterMsgDispatch(GenericDatabaseThreadManager, MSG_DB_DECREMENT_OFFLINE_COUNT, dbDecrementContainersToOffline);
	gwtRegisterMsgDispatch(GenericDatabaseThreadManager, MSG_DB_DECREMENT_OFFLINE_CLEANUP_COUNT, dbDecrementOfflineContainersToCleanup);
	gwtRegisterMsgDispatch(GenericDatabaseThreadManager, MSG_DB_DECREMENT_ACCOUNT_STUB_OPERATIONS, dbDecrementOutstandingAccountStubOperations);
	gwtRegisterMsgDispatch(GenericDatabaseThreadManager, MSG_DB_QUEUE_FREE_SUBSCRIBE_CACHE_COPY, dbQueueFreeSubscribeCacheCopy);
	gwtRegisterMsgDispatch(GenericDatabaseThreadManager, MSG_DB_LOGIN2_UNPACK_PETS, dbQueueUnpackPets);
	gwtRegisterMsgDispatch(GenericDatabaseThreadManager, MSG_DB_TRANSACTION_TIMING, dbAddHandleNewTransactionTimingThreaded);

	gwtSetThreaded(GenericDatabaseThreadManager, true, 0, false);
	gwtSetSkipIfFull(GenericDatabaseThreadManager, 1);

	gwtStart(GenericDatabaseThreadManager);
}

void QueueWriteContainerOnGenericDatabaseThreads(ContainerUpdateInfo *info)
{
	//objGetObjectLock will always return a valid struct
	ObjectLock *objectLock;
	PERFINFO_AUTO_START_FUNC();
	objectLock = objGetObjectLock(info->containerType, info->containerID);
	assert(objectLock);
	queueGenericDatabaseRequest_Struct(CMD_DB_WRITE_CONTAINER, *info, ContainerUpdateInfo, objectLock);
	PERFINFO_AUTO_STOP();
}

void QueueDBUpdateContainerOnGenericDatabaseThreads(ContainerUpdateInfo *info)
{
	//objGetObjectLock will always return a valid struct
	ObjectLock *objectLock;
	PERFINFO_AUTO_START_FUNC();
	objectLock = objGetObjectLock(info->containerType, info->containerID);
	assert(objectLock);
	queueGenericDatabaseRequest_Struct(CMD_DB_UPDATE_CONTAINER, *info, ContainerUpdateInfo, objectLock);
	PERFINFO_AUTO_STOP();
}

void QueueDestroyContainerOnGenericDatabaseThreads(ContainerUpdateInfo *info)
{
	//objGetObjectLock will always return a valid struct
	ObjectLock *objectLock;
	PERFINFO_AUTO_START_FUNC();
	objectLock = objGetObjectLock(info->containerType, info->containerID);
	assert(objectLock);
	queueGenericDatabaseRequest_Struct(CMD_DB_DESTROY_CONTAINER, *info, ContainerUpdateInfo, objectLock);
	PERFINFO_AUTO_STOP();
}

void QueueDestroyDeletedContainerOnGenericDatabaseThreads(ContainerUpdateInfo *info)
{
	//objGetObjectLock will always return a valid struct
	ObjectLock *objectLock;
	PERFINFO_AUTO_START_FUNC();
	objectLock = objGetObjectLock(info->containerType, info->containerID);
	assert(objectLock);
	queueGenericDatabaseRequest_Struct(CMD_DB_DESTROY_DELETED_CONTAINER, *info, ContainerUpdateInfo, objectLock);
	PERFINFO_AUTO_STOP();
}

void QueueDeleteContainerOnGenericDatabaseThreads(ContainerDeleteInfo *info)
{
	//objGetObjectLock will always return a valid struct
	ObjectLock *objectLock;
	PERFINFO_AUTO_START_FUNC();
	objectLock = objGetObjectLock(info->containerType, info->containerID);
	assert(objectLock);
	queueGenericDatabaseRequest_Struct(CMD_DB_DELETE_CONTAINER, *info, ContainerDeleteInfo, objectLock);
	PERFINFO_AUTO_STOP();
}

void QueueUndeleteContainerOnGenericDatabaseThreads(ContainerUndeleteInfo *info)
{
	//objGetObjectLock will always return a valid struct
	ObjectLock *objectLock;
	PERFINFO_AUTO_START_FUNC();
	objectLock = objGetObjectLock(info->containerType, info->containerID);
	assert(objectLock);
	queueGenericDatabaseRequest_Struct(CMD_DB_UNDELETE_CONTAINER, *info, ContainerUndeleteInfo, objectLock);
	PERFINFO_AUTO_STOP();
}

void QueueUndeleteContainerWithRenameOnGenericDatabaseThreads(ContainerUndeleteInfo *info)
{
	//objGetObjectLock will always return a valid struct
	ObjectLock *objectLock;
	PERFINFO_AUTO_START_FUNC();
	objectLock = objGetObjectLock(info->containerType, info->containerID);
	assert(objectLock);
	queueGenericDatabaseRequest_Struct(CMD_DB_UNDELETE_CONTAINER_WITH_RENAME, *info, ContainerUndeleteInfo, objectLock);
	PERFINFO_AUTO_STOP();
}

void QueueDBUpdateContainerOwnerOnGenericDatabaseThreads(ContainerOwnerUpdateInfo *info)
{
	//objGetObjectLock will always return a valid struct
	ObjectLock *objectLock;
	PERFINFO_AUTO_START_FUNC();
	objectLock = objGetObjectLock(info->containerType, info->containerID);
	assert(objectLock);
	queueGenericDatabaseRequest_Struct(CMD_DB_UPDATE_CONTAINER_OWNER, *info, ContainerOwnerUpdateInfo, objectLock);
	PERFINFO_AUTO_STOP();
}

void QueueWriteContainerToOfflineHoggOnGenericDatabaseThreads(ContainerUpdateInfo *info)
{
	//objGetObjectLock will always return a valid struct
	ObjectLock *objectLock;
	PERFINFO_AUTO_START_FUNC();
	objectLock = objGetObjectLock(info->containerType, info->containerID);
	assert(objectLock);
	queueGenericDatabaseRequest_Struct(CMD_DB_WRITE_CONTAINER_TO_OFFLINE, *info, ContainerUpdateInfo, objectLock);
	PERFINFO_AUTO_STOP();
}

void QueueDBRemoveContainerFromOfflineHoggOnGenericDatabaseThreads(ContainerUpdateInfo *info)
{
	//objGetObjectLock will always return a valid struct
	ObjectLock *objectLock;
	PERFINFO_AUTO_START_FUNC();
	objectLock = objGetObjectLock(info->containerType, info->containerID);
	assert(objectLock);
	queueGenericDatabaseRequest_Struct(CMD_DB_REMOVE_CONTAINER_FROM_OFFLINE, *info, ContainerUpdateInfo, objectLock);
	PERFINFO_AUTO_STOP();
}

void QueueDBAddOfflineEntityPlayerToAccountStubOnGenericDatabaseThreads(ContainerAccountStubInfo *info)
{
	//objGetObjectLock will always return a valid struct
	ObjectLock *objectLock;
	PERFINFO_AUTO_START_FUNC();
	objectLock = objGetObjectLock(info->containerType, info->accountID);
	assert(objectLock);
	queueGenericDatabaseRequest_Struct(CMD_DB_ADD_OFFLINE_PLAYER_TO_ACCOUNT_STUB, *info, ContainerAccountStubInfo, objectLock);
	PERFINFO_AUTO_STOP();
}
void QueueDBMarkEntityPlayerRestoredInAccountStubOnGenericDatabaseThreads(ContainerMiniAccountStubInfo *info)
{
	//objGetObjectLock will always return a valid struct
	ObjectLock *objectLock;
	PERFINFO_AUTO_START_FUNC();
	objectLock = objGetObjectLock(info->containerType, info->containerID);
	assert(objectLock);
	queueGenericDatabaseRequest_Struct(CMD_DB_MARK_ENTITY_PLAYER_RESTORED, *info, ContainerMiniAccountStubInfo, objectLock);
	PERFINFO_AUTO_STOP();
}

void QueueDBRemoveOfflineEntityPlayerFromAccountStubOnGenericDatabaseThreads(ContainerMiniAccountStubInfo *info)
{
	//objGetObjectLock will always return a valid struct
	ObjectLock *objectLock;
	PERFINFO_AUTO_START_FUNC();
	objectLock = objGetObjectLock(info->containerType, info->containerID);
	assert(objectLock);
	queueGenericDatabaseRequest_Struct(CMD_DB_REMOVE_OFFLINE_ENTITY_PLAYER, *info, ContainerMiniAccountStubInfo, objectLock);
	PERFINFO_AUTO_STOP();
}

void QueueDBAddOfflineAccountWideContainerToAccountStubOnGenericDatabaseThreads(AccountWideContainerAccountStubInfo *info)
{
	//objGetObjectLock will always return a valid struct
	ObjectLock *objectLock;
	PERFINFO_AUTO_START_FUNC();
	objectLock = objGetObjectLock(info->containerType, info->accountID);
	assert(objectLock);
	queueGenericDatabaseRequest_Struct(CMD_DB_ADD_ACCOUNT_WIDE_TO_ACCOUNT_STUB, *info, AccountWideContainerAccountStubInfo, objectLock);
	PERFINFO_AUTO_STOP();
}

void QueueDBMarkAccountWideContainerRestoredInAccountStubOnGenericDatabaseThreads(AccountWideContainerAccountStubInfo *info)
{
	//objGetObjectLock will always return a valid struct
	ObjectLock *objectLock;
	PERFINFO_AUTO_START_FUNC();
	objectLock = objGetObjectLock(info->containerType, info->accountID);
	assert(objectLock);
	queueGenericDatabaseRequest_Struct(CMD_DB_MARK_ACCOUNT_WIDE_RESTORED, *info, AccountWideContainerAccountStubInfo, objectLock);
	PERFINFO_AUTO_STOP();
}

void QueueDBRemoveOfflineAccountWideContainerFromAccountStubOnGenericDatabaseThreads(AccountWideContainerAccountStubInfo *info)
{
	//objGetObjectLock will always return a valid struct
	ObjectLock *objectLock;
	PERFINFO_AUTO_START_FUNC();
	objectLock = objGetObjectLock(info->containerType, info->accountID);
	assert(objectLock);
	queueGenericDatabaseRequest_Struct(CMD_DB_REMOVE_ACCOUNT_WIDE_FROM_ACCOUNT_STUB, *info, AccountWideContainerAccountStubInfo, objectLock);
	PERFINFO_AUTO_STOP();
}

void QueueSubscribeToContainerOnGenericDatabaseThreads(SubscribeToContainerInfo *info)
{
	//objGetObjectLock will always return a valid struct
	ObjectLock *objectLock;
	PERFINFO_AUTO_START_FUNC();
	objectLock = objGetObjectLock(info->conType, info->conID);
	assert(objectLock);
	queueGenericDatabaseRequest_Struct(CMD_DB_SUBSCRIBE, *info, SubscribeToContainerInfo, objectLock);
	PERFINFO_AUTO_STOP();
}

void QueueUnsubscribeFromContainerOnGenericDatabaseThreads(SubscribeToContainerInfo *info)
{
	//objGetObjectLock will always return a valid struct
	ObjectLock *objectLock;
	PERFINFO_AUTO_START_FUNC();
	objectLock = objGetObjectLock(info->conType, info->conID);
	assert(objectLock);
	queueGenericDatabaseRequest_Struct(CMD_DB_UNSUBSCRIBE, *info, SubscribeToContainerInfo, objectLock);
	PERFINFO_AUTO_STOP();
}

void QueueFreeSubscribeCacheCopyOnGenericDatabaseThreads(CachedSubscriptionCopy *copy)
{
	//objGetObjectLock will always return a valid struct
	ObjectLock *objectLock;
	PERFINFO_AUTO_START_FUNC();
	objectLock = objGetObjectLock(GetCachedSubscriptionCopyType(copy), GetCachedSubscriptionCopyID(copy));
	assert(objectLock);
	queueGenericDatabaseRequest_Pointer(CMD_DB_FREE_SUBSCRIBE_CACHE_COPY, copy, CachedSubscriptionCopy, objectLock);
	PERFINFO_AUTO_STOP();
}

void QueueDBHandleNewTransactionOnGenericDatabaseThreads(dbHandleNewTransaction_Data *data, GlobalType containerType, ContainerID containerID)
{
	ObjectLock *objectLock = NULL;
	PERFINFO_AUTO_START_FUNC();
	if (containerType && containerID)
	{
		objectLock = objGetObjectLock(containerType, containerID);
	}

	queueGenericDatabaseRequest_Struct(CMD_DB_HANDLE_NEW_TRANSACTION, *data, dbHandleNewTransaction_Data, objectLock);
	PERFINFO_AUTO_STOP();
}

void QueueDBHandleCancelTransactionOnGenericDatabaseThreads(dbHandleCancelTransaction_Data *data, GlobalType containerType, ContainerID containerID)
{
	ObjectLock *objectLock = NULL;
	PERFINFO_AUTO_START_FUNC();
	if (containerType && containerID)
	{
		objectLock = objGetObjectLock(containerType, containerID);
	}

	queueGenericDatabaseRequest_Struct(CMD_DB_HANDLE_CANCEL_TRANSACTION, *data, dbHandleCancelTransaction_Data, objectLock);
	PERFINFO_AUTO_STOP();
}

void QueueDBHandleConfirmTransactionOnGenericDatabaseThreads(dbHandleConfirmTransaction_Data *data, GlobalType containerType, ContainerID containerID)
{
	ObjectLock *objectLock = NULL;
	PERFINFO_AUTO_START_FUNC();
	if (containerType && containerID)
	{
		objectLock = objGetObjectLock(containerType, containerID);
	}

	queueGenericDatabaseRequest_Struct(CMD_DB_HANDLE_CONFIRM_TRANSACTION, *data, dbHandleConfirmTransaction_Data, objectLock);
	PERFINFO_AUTO_STOP();
}

void QueueGuildCleanupOnMainThread(GWTCmdPacket *packet, GuildCleanupInfo *info)
{
	PERFINFO_AUTO_START_FUNC();
	gwtQueueMsgStruct(packet, MSG_DB_DO_GUILD_CLEANUP, *info, GuildCleanupInfo);
	PERFINFO_AUTO_STOP();
}

void QueueChatServerCleanupOnMainThread(GWTCmdPacket *packet, ChatServerCleanupInfo *info)
{
	PERFINFO_AUTO_START_FUNC();
	gwtQueueMsgStruct(packet, MSG_DB_DO_CHAT_SERVER_CLEANUP, *info, ChatServerCleanupInfo);
	PERFINFO_AUTO_STOP();
}

void QueueFreeSubscribeCacheCopyOnMainThread(GWTCmdPacket *packet, CachedSubscriptionCopy *copy)
{
	PERFINFO_AUTO_START_FUNC();
	gwtQueueMsgPointer(packet, MSG_DB_QUEUE_FREE_SUBSCRIBE_CACHE_COPY, copy);
	PERFINFO_AUTO_STOP();
}

void QueueDBLogin2UnpackCharacterOnGenericDatabaseThreads(DBGetCharacterDetailState *detailState, ContainerID playerID)
{
	ObjectLock *objectLock = NULL;
	PERFINFO_AUTO_START_FUNC();
	objectLock = objGetObjectLock(GLOBALTYPE_ENTITYPLAYER, playerID);
	assert(objectLock);
	queueGenericDatabaseRequest_Pointer(CMD_DB_LOGIN2_UNPACK_CHARACTER, detailState, DBGetCharacterDetailState, objectLock);
	PERFINFO_AUTO_STOP();
}

// Only queue this from the main thread because of the static
void QueueDBLogin2UnpackPetsOnGenericDatabaseThreads(DBGetCharacterDetailState *detailState, ContainerRef **puppetRefs)
{
	ObjectLock **objectLocks = NULL;
	int i;
	int numPuppets = eaSize(&puppetRefs);
	PERFINFO_AUTO_START_FUNC();
	for (i = 0; i < numPuppets; ++i)
	{
		eaPush(&objectLocks, objGetObjectLock(puppetRefs[i]->containerType, puppetRefs[i]->containerID));
	}

	queueGenericDatabaseRequest_Pointer_MultipleLocks(CMD_DB_LOGIN2_UNPACK_PETS, detailState, DBGetCharacterDetailState, &objectLocks);
	PERFINFO_AUTO_STOP();
}

void QueueDBLogin2UnpackPetsOnMainThread(GWTCmdPacket *packet, DBGetCharacterDetailState *detailState)
{
	PERFINFO_AUTO_START_FUNC();
	gwtQueueMsgPointer(packet, MSG_DB_LOGIN2_UNPACK_PETS, detailState);
	PERFINFO_AUTO_STOP();
}

void MonitorGenericDatabaseThreads(void)
{
	PERFINFO_AUTO_START_FUNC();
	if(GenericDatabaseThreadManager)
	{
		gwtMonitor(GenericDatabaseThreadManager);
		sendGenericDatabaseRequests();
	}
	PERFINFO_AUTO_STOP();
}

void FlushGenericDatabaseThreads(bool flushGenericRequestQueue)
{
	if (GenericDatabaseThreadManager)
	{
		PERFINFO_AUTO_START_FUNC();
		while (flushGenericRequestQueue && genericDatabaseRequestHead)
		{
			sendGenericDatabaseRequests();
			gwtFlush(GenericDatabaseThreadManager);
		}

		gwtFlush(GenericDatabaseThreadManager);
		gwtFlushMessages(GenericDatabaseThreadManager);
		PERFINFO_AUTO_STOP();
	}
}

bool gDisableGenericDatabaseThreads = false; //The goal is for this to be false once there has been more testing on dev machines
AUTO_CMD_INT(gDisableGenericDatabaseThreads, DisableGenericDatabaseThreads);

AUTO_CMD_INT(gDisableGenericDatabaseThreads, ForceDisableGenericDatabaseThreads);

bool GenericDatabaseThreadIsEnabled(void)
{
	return !gDisableGenericDatabaseThreads;
}

bool GenericDatabaseThreadIsActive(void)
{
	return GenericDatabaseThreadIsEnabled() && GenericDatabaseThreadManager;
}

bool OnBackgroundGenericDatabaseThread(void)
{
	return gwtInWorkerThread(GenericDatabaseThreadManager);
}


