/***************************************************************************
*     Copyright (c) 2012, Cryptic Studios
*     All Rights Reserved
*     Confidential Property of Cryptic Studios
***************************************************************************/

typedef enum GlobalType GlobalType;
typedef U32 ContainerID;
typedef struct GWTCmdPacket GWTCmdPacket;
typedef struct CachedSubscriptionCopy CachedSubscriptionCopy;

typedef struct SubscriptionCache {
	StashTable lookupTable;
	CachedSubscriptionCopy *head;
	CachedSubscriptionCopy *tail;

	U32 count;
	U32 max;
} SubscriptionCache;

// This struct has a "shared ownership" lifecycle across the main thread and the subscription thread
// The main thread is responsible for creating new CachedSubscriptionCopy objects and managing their ordering in the cache
// The subscription thread is responsible for populating/retrieving the cached data
// Objects are freed by the main thread removing the object from its cache, then queueing it to worker thread for freeing;
//	this ensures that the object always exists as long as the subscription thread may be using it
typedef struct CachedSubscriptionCopy {
	// OWNED BY THE MAIN THREAD
	// Need the type/ID to find an arbitrary copy in the StashTable
	GlobalType type;
	ContainerID id;
	SubscriptionCache *cache;

	// Doubly-linked list
	CachedSubscriptionCopy *prev;
	CachedSubscriptionCopy *next;
	U32 created;

	// If we queue this copy to the subscription thread, will it have data? Basically, this flag means "have we sent it to
	// the subscription thread before?" The first time we send it, we know that it will get populated with data. After the
	// first time we send it, we know that if we send it again, it will have that data.
	//
	// It doesn't actually matter if it has actual data when the main thread checks it. The important thing is that the main
	// thread has asked the subscription thread to populate it. This assumption is another reason why we are currently hard-
	// limited to a single subscription thread. We do plan to revisit this later on.
	bool will_have_data;

	// OWNED BY THE SUBSCRIPTION THREAD
	// We always cache the results of ParserSend-ing the container, so we can subsequently just pktSendBytesRaw() (way cheaper)
	U8* pak_data;
	int len;
} CachedSubscriptionCopy;

GlobalType GetCachedSubscriptionCopyType(CachedSubscriptionCopy *copy);
ContainerID GetCachedSubscriptionCopyID(CachedSubscriptionCopy *copy);

void dbContainerCreateNotify(GWTCmdPacket *packet, GlobalType conType, ContainerID conID);
void dbContainerDeleteNotify(GWTCmdPacket *packet, GlobalType conType, U32 conID, bool doPermanentCleanup);

void dbContainerChangeNotify(GWTCmdPacket *packet, GlobalType conType, ContainerID conID, char *diffString);
void dbContainerOwnerChangeNotify(GWTCmdPacket *packet, GlobalType conType, ContainerID conID, GlobalType ownerType, ContainerID ownerID, bool getLock);

void dbUnsubscribeFromAllContainers(GlobalType eType, ContainerID iContainerID);

void CreateAndStartDBSubscriptionThread(void);

void LogSubscriptionSendTrackers(void);

void dbSubscribeToContainer_CB(GWTCmdPacket *packet, SubscribeToContainerInfo *info);
void dbUnsubscribeFromContainer_CB(SubscribeToContainerInfo *info);
void DestroyCachedSubscription(CachedSubscriptionCopy *copy);
