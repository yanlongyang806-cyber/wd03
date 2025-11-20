/***************************************************************************
 *     Copyright (c) Cryptic Studios
 *     All Rights Reserved
 *     Confidential Property of Cryptic Studios
 ***************************************************************************/
#ifndef GSLGATEWAYSESSION_H__
#define GSLGATEWAYSESSION_H__

#pragma once

#include "referencesystem.h" // for REF_TO
#include "GlobalTypeEnum.h" // for GlobalType
#include "AppLocale.h" // For language
#include "gslGatewayContainerMapping.h" // for GatewayGlobalType

typedef U32 ContainerID;
typedef struct NetLink NetLink;
typedef struct Packet Packet;
typedef struct PacketTracker PacketTracker;
typedef struct Entity Entity;
typedef struct Guild Guild;
typedef struct GroupProjectContainer GroupProjectContainer;
typedef struct ContainerMapping ContainerMapping;
typedef struct MappedEntity MappedEntity;
typedef struct GameAccountData GameAccountData;
typedef struct ContainerTracker ContainerTracker;
typedef struct GameAccountDataExtract GameAccountDataExtract;
typedef struct ItemAssignmentCachedStruct ItemAssignmentCachedStruct;
typedef struct ItemAssignmentList ItemAssignmentList;
typedef struct ItemAssignmentGatewayData ItemAssignmentGatewayData;

typedef struct AuctionLotList AuctionLotList;
typedef struct TradeBagLite TradeBagLite;

typedef REF_TO(void) RefTo;


AUTO_STRUCT;
typedef struct ContainerTracker
{
	char *estrID; AST(ESTRING)
		// Unique identifier for this container.

	GatewayGlobalType gatewaytype; NO_AST
		// GW_GLOBALTYPE_... (or GLOBALTYPE_...)

	ContainerMapping *pMapping; NO_AST
		// How to map this container.

	char *estrDiff; AST(ESTRING)
		// The most recent diff.

	void *pMapped; NO_AST
		// A remapping and translation of the container that is appropriate
		//   for sending to the client. Each project will have its own
		//   definition of this structure for each GlobalType

	bool bReady;
		// If true, the entity is ready to be sent. If false, then it hasn't
		//   been subscribed to yet, or its dependencies (like pets) aren't
		//   ready yet.

	bool bModified;
		// If true, then we have received a subscription update for the
		//   container.

	bool bSend;
		// If true, then the entity has been requested by the client. So,
		//   when it finally arrives, send it.

	bool bFullUpdate;
		// If true, send a full update the next time a send is done.

	//
	// Special stuff for DB Containers
	//

	ContainerID idOwnerAccount;
		// The Container ID for the account which owns this container. Filled in
		//   once it has been subscribed. (If applicable.)

	//
	// Container references
	//
	// These are here individually mainly so the ServerMonitor can look at
	//   them. They are also convenient for debugging and for when this
	//   structure is destroyed.
	// Only one of these will be active at a time. It'll be pointed to with phRef.
	// Only used for DB Containers.
	REF_TO(Entity) hEntity;
		// (If it's an entity) Subscription ref to entity

	REF_TO(Guild) hGuild;
		// (If it's a guild) Subscription ref to guild

	REF_TO(GroupProjectContainer) hGroupProjectContainer;
		// (If it's a group project) Subscription ref to group project

	RefTo *phRef; NO_AST
		// a pointer to whichever REF_TO above is being used.

	AuctionLotList *pAuctionLotList;

	Entity *pOfflineCopy;
		// The offline entity copy cached

} ContainerTracker;

AUTO_STRUCT;
typedef struct GatewaySession
{
	U32 uiIdxServer;
		// The index of the session on the server. Used to directly index
		//   to the session. Then the magic number is checked to make sure
		//   it's not a stale session or something.

	U32 uiMagic;
		// A magic number for this particular session. Both the server index
		//   and the magic number must match for a request to succeed.

	U32 idAccount;
		// The account id associated with this login.

	NetLink *link; NO_AST
		// Used to correlate the session to a particular link. If the link
		//   goes down, all the sessions assigned to that link will be
		//   deleted.

	//
	// Session data
	//

	Language lang;
		// The player's language.

	EARRAY_OF(ContainerTracker) ppContainers;
		// List of MappedEntity structs for regular entities.

	REF_TO(GameAccountData) hGameAccountData;		AST(COPYDICT(GameAccountData))
		// A reference (subscription) to gameaccountdata

	GameAccountDataExtract *pGameAccountDataExtract;
		// The Extract for game account data

	ItemAssignmentCachedStruct *pItemAssignmentsCache;

} GatewaySession;


#define SESSION_PKTCREATE(pkt, psess, pchMessage)								\
{																				\
	static PacketTracker *__pTracker;											\
	ONCE(__pTracker = PacketTrackerFind("GatewaySession", 0, pchMessage));		\
	pkt = session_pktCreateWithTracker(psess, pchMessage, __pTracker);			\
}

Packet *session_pktCreateWithTracker(GatewaySession *psess, const char *pchMessage, PacketTracker *packetTracker);


void wgsInitSessions(void);
int wgsGetSessionCount(void);
GatewaySession *wgsCreateSession(U32 uiMagic, U32 idAccount, U32 lang, NetLink *link);

void wgsDestroySession(GatewaySession *psess);
void wgsDestroySessionForIndex(U32 uiIdxServer, U32 uiMagic);
void wgsDestroyAllSessionsForLink(NetLink *link);
void wgsDestroyAllSessions(void);

GatewaySession *wgsFindSessionForIndex(U32 uiIdxServer, U32 uiMagic);

void wgsUpdateAllContainers(void);

GatewaySession *wgsFindSessionForAccountId(ContainerID iId);
GatewaySession *wgsFindOwningSessionForDBContainer(GlobalType type, ContainerID iId);

ContainerTracker *session_FindContainerTracker(GatewaySession *psess, GlobalType type, const char *pchID);
ContainerTracker *session_FindDBContainerTracker(GatewaySession *psess, GlobalType type,  ContainerID id);
ContainerTracker *session_FindFirstContainerTrackerForType(GatewaySession *psess, GlobalType type);

void session_SendDestroySession(GatewaySession *psess);
void session_ReleaseContainers(GatewaySession *psess);
void session_ContainerModified(GatewaySession *psess, GlobalType type, const char *pchID);

ContainerTracker *session_GetContainerEx(GatewaySession *psess, ContainerMapping *pmapping, const char *pchID, void *pvParams, bool bSend);
ContainerTracker *session_GetContainer(GatewaySession *psess, ContainerMapping *pmapping, const char *pchID, void *pvParams);
void session_ReleaseContainer(GatewaySession *psess, GlobalType type, const char *pchID);

GameAccountDataExtract *session_GetCachedGameAccountDataExtract(GatewaySession *psess);

// System
void session_sendClientCmd(GatewaySession *psess, const char *cmdLine);

// Internal helpers (used by Sessions internally)
ContainerTracker *session_CreateContainerTracker(GatewaySession *psess, ContainerMapping *pmapping, const char *pchID);
void session_GameAccountDataSubscribed(GatewaySession *psess);
void session_SendContainer(GatewaySession *psess, ContainerTracker *ptracker);

Entity *session_GetLoginEntity(GatewaySession *psess);
Entity *session_GetLoginEntityOfflineCopy(GatewaySession *psess);

// DB Container support
void wgsDBContainerModified(GlobalType type, ContainerID id);
void SubscribeDBContainer(GatewaySession *psess, ContainerTracker *ptracker, void *pvParams);
bool IsReadyDBContainer(GatewaySession *psess, ContainerTracker *ptracker);

LATELINK;
void GatewayEntityTick(Entity *pEnt);


#endif /* #ifndef GSLGATEWAYSESSSION_H__ */

/* End of File */
