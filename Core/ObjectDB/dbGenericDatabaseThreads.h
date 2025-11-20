#pragma once

#include "GenericWorkerThread.h"
#include "GlobalTypes.h"

bool GenericDatabaseThreadIsEnabled(void);

void InitGenericDatabaseThreads(void);
void MonitorGenericDatabaseThreads(void);

int GetOutstandingGenericDatabaseRequestCount(void);

enum
{
	CMD_DB_UPDATE_CONTAINER = GWT_CMD_USER_START,
	CMD_DB_UPDATE_CONTAINER_OWNER,
	CMD_DB_WRITE_CONTAINER,
	CMD_DB_DESTROY_CONTAINER,
	CMD_DB_DESTROY_DELETED_CONTAINER,
	CMD_DB_DELETE_CONTAINER,
	CMD_DB_UNDELETE_CONTAINER,
	CMD_DB_UNDELETE_CONTAINER_WITH_RENAME,
	CMD_DB_WRITE_CONTAINER_TO_OFFLINE,
	CMD_DB_REMOVE_CONTAINER_FROM_OFFLINE,
	CMD_DB_ADD_OFFLINE_PLAYER_TO_ACCOUNT_STUB,
	CMD_DB_MARK_ENTITY_PLAYER_RESTORED,
	CMD_DB_REMOVE_OFFLINE_ENTITY_PLAYER,
	CMD_DB_ADD_ACCOUNT_WIDE_TO_ACCOUNT_STUB,
	CMD_DB_MARK_ACCOUNT_WIDE_RESTORED,
	CMD_DB_REMOVE_ACCOUNT_WIDE_FROM_ACCOUNT_STUB,
	CMD_DB_SUBSCRIBE,
	CMD_DB_UNSUBSCRIBE,
	CMD_DB_FREE_SUBSCRIBE_CACHE_COPY,
	CMD_DB_HANDLE_NEW_TRANSACTION,
	CMD_DB_HANDLE_CANCEL_TRANSACTION,
	CMD_DB_HANDLE_CONFIRM_TRANSACTION,
	CMD_DB_LOGIN2_UNPACK_CHARACTER,
	CMD_DB_LOGIN2_UNPACK_PETS,

	MSG_DB_DO_GUILD_CLEANUP,
	MSG_DB_DO_CHAT_SERVER_CLEANUP,
	MSG_DB_DECREMENT_OFFLINE_COUNT,
	MSG_DB_DECREMENT_OFFLINE_CLEANUP_COUNT,
	MSG_DB_DECREMENT_ACCOUNT_STUB_OPERATIONS,
	MSG_DB_QUEUE_FREE_SUBSCRIBE_CACHE_COPY,
	MSG_DB_LOGIN2_UNPACK_PETS,
	MSG_DB_TRANSACTION_TIMING,
};

typedef struct ContainerUpdateInfo
{
	GlobalType containerType;
	ContainerID containerID;
	char *diffString;
} ContainerUpdateInfo;

typedef struct ContainerOwnerUpdateInfo
{
	GlobalType containerType;
	ContainerID containerID;
	GlobalType ownerType;
	ContainerID ownerID;
} ContainerOwnerUpdateInfo;

typedef struct ContainerDeleteInfo
{
	GlobalType containerType;
	ContainerID containerID;
	bool destroyNow;
	bool cleanup;
} ContainerDeleteInfo;

typedef struct ContainerUndeleteInfo
{
	GlobalType containerType;
	ContainerID containerID;
	char *namestr;
} ContainerUndeleteInfo;

typedef struct ContainerAccountStubInfo
{
	GlobalType containerType;
	ContainerID accountID;
	char *characterStubString;
} ContainerAccountStubInfo;

typedef struct ContainerMiniAccountStubInfo
{
	GlobalType containerType;
	ContainerID accountID;
	ContainerID containerID;
} ContainerMiniAccountStubInfo;

typedef struct AccountWideContainerAccountStubInfo
{
	GlobalType containerType;
	ContainerID accountID;
	GlobalType accountWideContainerType;
} AccountWideContainerAccountStubInfo;

typedef struct SubscribeToContainerInfo
{
	GlobalType subscriberType;
	ContainerID subscriberID;
	GlobalType conType;
	ContainerID conID;
	char *reason;
} SubscribeToContainerInfo;

typedef struct GuildCleanupInfo
{
	ContainerID guildID;
	ContainerID playerID;
} GuildCleanupInfo;

typedef struct ChatServerCleanupInfo
{
	ContainerID accountID;
	ContainerID playerID;
} ChatServerCleanupInfo;

typedef struct dbHandleNewTransaction_Data dbHandleNewTransaction_Data;
typedef struct dbHandleCancelTransaction_Data dbHandleCancelTransaction_Data;
typedef struct dbHandleConfirmTransaction_Data dbHandleConfirmTransaction_Data;
typedef struct CachedSubscriptionCopy CachedSubscriptionCopy;
typedef struct DBGetCharacterDetailState DBGetCharacterDetailState;
typedef struct dbTransactionTimingData dbTransactionTimingData;

void QueueWriteContainerOnGenericDatabaseThreads(ContainerUpdateInfo *info);
void QueueDBUpdateContainerOnGenericDatabaseThreads(ContainerUpdateInfo *info);
void QueueDestroyContainerOnGenericDatabaseThreads(ContainerUpdateInfo *info);
void QueueDestroyDeletedContainerOnGenericDatabaseThreads(ContainerUpdateInfo *info);
void QueueDeleteContainerOnGenericDatabaseThreads(ContainerDeleteInfo *info);
void QueueUndeleteContainerOnGenericDatabaseThreads(ContainerUndeleteInfo *info);
void QueueUndeleteContainerWithRenameOnGenericDatabaseThreads(ContainerUndeleteInfo *info);
void QueueDBUpdateContainerOwnerOnGenericDatabaseThreads(ContainerOwnerUpdateInfo *info);
void QueueWriteContainerToOfflineHoggOnGenericDatabaseThreads(ContainerUpdateInfo *info);
void QueueDBRemoveContainerFromOfflineHoggOnGenericDatabaseThreads(ContainerUpdateInfo *info);
void QueueDBAddOfflineEntityPlayerToAccountStubOnGenericDatabaseThreads(ContainerAccountStubInfo *info);
void QueueDBMarkEntityPlayerRestoredInAccountStubOnGenericDatabaseThreads(ContainerMiniAccountStubInfo *info);
void QueueDBRemoveOfflineEntityPlayerFromAccountStubOnGenericDatabaseThreads(ContainerMiniAccountStubInfo *info);
void QueueDBAddOfflineAccountWideContainerToAccountStubOnGenericDatabaseThreads(AccountWideContainerAccountStubInfo *info);
void QueueDBMarkAccountWideContainerRestoredInAccountStubOnGenericDatabaseThreads(AccountWideContainerAccountStubInfo *info);
void QueueDBRemoveOfflineAccountWideContainerFromAccountStubOnGenericDatabaseThreads(AccountWideContainerAccountStubInfo *info);
void QueueSubscribeToContainerOnGenericDatabaseThreads(SubscribeToContainerInfo *info);
void QueueUnsubscribeFromContainerOnGenericDatabaseThreads(SubscribeToContainerInfo *info);
void QueueFreeSubscribeCacheCopyOnGenericDatabaseThreads(CachedSubscriptionCopy *copy);
void QueueDBHandleNewTransactionOnGenericDatabaseThreads(dbHandleNewTransaction_Data *data, GlobalType containerType, ContainerID containerID);
void QueueDBHandleCancelTransactionOnGenericDatabaseThreads(dbHandleCancelTransaction_Data *data, GlobalType containerType, ContainerID containerID);
void QueueDBHandleConfirmTransactionOnGenericDatabaseThreads(dbHandleConfirmTransaction_Data *data, GlobalType containerType, ContainerID containerID);
void QueueDBLogin2UnpackCharacterOnGenericDatabaseThreads(DBGetCharacterDetailState *detailState, ContainerID playerID);
void QueueDBLogin2UnpackPetsOnGenericDatabaseThreads(DBGetCharacterDetailState *detailState, ContainerRef **puppetRefs);
void QueueDBLogin2UnpackPetsOnMainThread(GWTCmdPacket *packet, DBGetCharacterDetailState *detailState);

void PerformGuildCleanup(ContainerID guildID, ContainerID playerID);
void QueueGuildCleanupOnMainThread(GWTCmdPacket *packet, GuildCleanupInfo *info);
void PerformChatServerCleanup(ContainerID accountID, ContainerID playerID);
void QueueChatServerCleanupOnMainThread(GWTCmdPacket *packet, ChatServerCleanupInfo *info);
void QueueFreeSubscribeCacheCopyOnMainThread(GWTCmdPacket *packet, CachedSubscriptionCopy *copy);

void FlushGenericDatabaseThreads(bool flushGenericRequestQueue);

bool GenericDatabaseThreadIsActive(void);
bool OnBackgroundGenericDatabaseThread(void);

int dbWriteContainerToOfflineHogg_CB(ContainerUpdateInfo *info, bool getLock);
int dbUndeleteContainerWithRename_CB(ContainerUndeleteInfo *info, bool getLock);
int dbUpdateContainer_CB(GWTCmdPacket *packet, ContainerUpdateInfo *info, bool getLock);
int dbUpdateContainerOwner_CB(GWTCmdPacket *packet, ContainerOwnerUpdateInfo *info, bool getLock);
int dbWriteContainer_CB(ContainerUpdateInfo *info);
int dbDestroyContainer_CB(GWTCmdPacket *packet, ContainerUpdateInfo *info, bool getLock);
int dbDestroyDeletedContainer_CB(GWTCmdPacket *packet, ContainerUpdateInfo *info, bool getLock);
int dbDeleteContainer_CB(GWTCmdPacket *packet, ContainerDeleteInfo *info, bool getLock);
int dbUndeleteContainer_CB(ContainerUndeleteInfo *info, bool getLock);
int dbRemoveContainerFromOfflineHogg_CB(ContainerUpdateInfo *info, bool getLock);
int dbAddOfflineEntityPlayerToAccountStub_CB(ContainerAccountStubInfo *info);
int dbMarkEntityPlayerRestoredInAccountStub_CB(ContainerMiniAccountStubInfo *info);
int dbRemoveOfflineEntityPlayerFromAccountStub_CB(ContainerMiniAccountStubInfo *info);
int dbAddOfflineAccountWideContainerToAccountStub_CB(AccountWideContainerAccountStubInfo *info);
int dbMarkAccountWideContainerRestoredInAccountStub_CB(AccountWideContainerAccountStubInfo *info);
int dbRemoveOfflineAccountWideContainerFromAccountStub_CB(AccountWideContainerAccountStubInfo *info);
