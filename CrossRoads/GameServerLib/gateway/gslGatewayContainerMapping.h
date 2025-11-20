/***************************************************************************
 *     Copyright (c) 2012-, Cryptic Studios
 *     All Rights Reserved
 *     Confidential Property of Cryptic Studios
 *
 ***************************************************************************/
#ifndef GSLGATEWAYCONTAINERMAPPING_H__
#define GSLGATEWAYCONTAINERMAPPING_H__
#pragma once
GCC_SYSTEM

#include "GlobalTypeEnum.h"

typedef enum GatewayGlobalType
{
	GW_GLOBALTYPE_FIRST = GLOBALTYPE_MAX,

	GW_GLOBALTYPE_LOGIN_ENTITY,
	GW_GLOBALTYPE_AUCTION_SEARCH,
	GW_GLOBALTYPE_CRAFTING_LIST,
	GW_GLOBALTYPE_CRAFTING_DETAIL,
	GW_GLOBALTYPE_MAILLIST,
	GW_GLOBALTYPE_MAILDETAIL,
	// Add new types here

	GW_GLOBALTYPE_MAX
} GatewayGlobalType;

typedef struct PacketTracker PacketTracker;
typedef struct ParseTable ParseTable;
typedef struct GatewaySession GatewaySession;
typedef struct ContainerTracker ContainerTracker;

typedef void (*ContainerMapperSubscribeFunc)(GatewaySession *psess, ContainerTracker *ptracker, void *pvParams);
	// The function is called when the container is subscribed. This
	//   might actually subscribe to something, or collect data, or do
	//   nothing (IsReady and/or Create would be used to do the work).
	//
	//   psess - Session which is asking for the container.
	//   ptracker - the tracker for this container.
	//   pvParams - the parameter struct for this container. You MUST copy this
	//              if you want to keep it. The pointer will be destroyed after
	//              the subscribe call.

typedef bool (*ContainerMapperIsModifiedFunc)(GatewaySession *psess, ContainerTracker *ptracker);
	// Called periodically on ready containers to allow the container to
	//   detect that a change to source data has occurred. If so, this
	//   function should return return true.
	// If true is returned, IsReady will be called until the data is ready
	//   as usual.
	//
	//   psess - Session which is asking for the container.
	//   ptracker - the tracker for this container.
	//
	// Returns true if the tracker has been modified

typedef bool (*ContainerMapperIsReadyFunc)(GatewaySession *psess, ContainerTracker *ptracker);
	// Called to determine if the container is ready to be mapped and sent.
	// Return true if it is. Once true is returned, pfnCreate will be
	//   called at some point.
	// Once a container is ready, this function isn't called again until
	//   it has been marked as modified.
	//
	//   psess - Session which is asking for the container.
	//   ptracker - the tracker for this container.
	//
	// Returns true if the tracker is ready to be sent.

typedef void *(*ContainerMapperCreateFunc)(GatewaySession *psess, ContainerTracker *ptracker, void *pvObj);
	// The function to call to create from the source to a tpiDest.
	// It's called only after IsReady has returned true.
	// This is responsible for allocating and returning a mapped structure
	//   for the container.
	//
	//   psess - Session which is asking for the container.
	//   ptracker - the tracker for this container.
	//   pvObj - The existing mapped object for this container (if there is one).
	// 
	//   Returns a pointer to the mapped container. This pointer is managed
	//     by the ContainerTracker code, and will eventually be destroyed
	//     via a call to the pfnDestroy.

typedef void (*ContainerMapperDestroyFunc)(GatewaySession *psess, ContainerTracker *ptracker, void *pvObj);
	// The function to properly free a mapped container created by pfnCreate.
	//
	//   psess - Session which is asking for the container.
	//   ptracker - the tracker for this container.
	//   pvObj - The mapped object to destroy.


typedef struct ContainerMapping
{
	const char *pchName;
		// Name of the container, used for messaging

	GatewayGlobalType gatewaytype;
		// GatewayGlobalType of container (Usually just the GlobalType, if it has one)

	GlobalType globaltype;
		// The actual GlobalType, if it has one.

	ParseTable *tpiParams;
		// The ParseTable of the parameters for this container.

	ParseTable *tpiDest;
		// The ParseTable of the new struct to build.

	bool bAlwaysFullUpdate;
		// If true, never send a diff. Always send a full update.

	ContainerMapperSubscribeFunc pfnSubscribe;
		// The function is called when the container is subscribed. This
		//   might actually subscribe to something, or collect data, or do
		//   nothing (IsReady and/or Create would be used to do the work).
	
	ContainerMapperIsModifiedFunc pfnIsModified;
		// Called periodically on ready containers to allow the container to
		//   detect that a change to source data has occurred. If so, this
		//   function should return return true.
		// If true is returned, IsReady will be called until the data is ready
		//   as usual.
		// This function is called often, so you want it to be fast. If it
		//   can't be fast or cheap, then you can do nothing and provide a
		//   CheckModified function which can be called by the client when
		//   appropriate.

	ContainerMapperIsModifiedFunc pfnCheckModified;
		// Called by the client to determine if any changes have occurred.
		//   If so, this function should return return true.
		// If true is returned, IsReady will be called until the data is ready
		//   as usual.
		// This function should be called rarely so may be moderately expensive.
		// If one can make a fast and cheap function, implement it as IsModified
		//   instead.

	ContainerMapperIsReadyFunc pfnIsReady;
		// Called to determine if the container is ready to be mapped and sent.
		// Return true if it is. Once true is returned, pfnCreate will be
		//   called at some point.
		// Once a container is ready, this function isn't called again until
		//   it has been marked as modified.

	ContainerMapperCreateFunc pfnCreate;
		// The function to call to create from the source to a tpiDest.
		// It's called only after IsReady has returned true.
		// This is responsible for allocating and returning a mapped structure
		//   for the container.

	ContainerMapperDestroyFunc pfnDestroy;
		// The function to properly free a mapped container created by pfnCreate.


	// Data that isn't part of the CONTAINER_MAPPING stuff goes after here.

	size_t offReference;
		// The offset in the ContainerTracker to the reference holding this
		//   object, or zero if there is none.

	PacketTracker *pPacketTracker;
		// For tracking packets sent for this container type.

} ContainerMapping;

//
// Some handy macros for defining ContainerMappings. If you use these, you
//   must follow strict naming rules. For example,
//
// CONTAINER_MAPPING("FooBar", GLOBALTYPE_FOOBAR, FooBar)
//      AUTO_STRUCT FooBar (which means there's a parse_FooBar)
//      AUTO_STRUCT ParamsFooBar (which means there's a parse_ParamsFooBar)
//      AUTO_STRUCT MappedFooBar (which means there's a parse_MappedFooBar)
//      function SubscribeFooBar
//      function IsModifiedFooBar
//      function CheckModifiedFooBar
//      function IsReadyFooBar
//      function CreateMappedFooBar
//      function DestroyMappedFooBar
//
// CONTAINER_MAPPING_SUBSCRIBE("DBFooBar", GLOBALTYPE_FOOBARINDATABASE, FooBarInDatabase)
//      Same as above (using FooBarInDatabase) plus
//      REF_TO(FooBarInDatabase) hFooBarInDatabase, in ContainerTracker.h
//
#define CONTAINER_MAPPING(name, gatewaytype, type) { name, gatewaytype, gatewaytype, parse_Params##type, parse_Mapped##type, false, Subscribe##type, IsModified##type, CheckModified##type, IsReady##type, CreateMapped##type, DestroyMapped##type, 0  }
#define CONTAINER_MAPPING_NOPARAMS(name, gatewaytype, type) {name, gatewaytype, gatewaytype, NULL, parse_Mapped##type, false, Subscribe##type, IsModified##type, CheckModified##type, IsReady##type, CreateMapped##type, DestroyMapped##type, 0  }
#define CONTAINER_MAPPING_SUBSCRIBE(name, globaltype, type) { name, globaltype, globaltype, NULL, parse_Mapped##type, false, SubscribeDBContainer, NULL, NULL, IsReadyDBContainer, CreateMapped##type, DestroyMapped##type, offsetof(ContainerTracker, h##type)  }

#define CONTAINER_MAPPING_LOGIN_ENTITY() { "LoginEntity", GW_GLOBALTYPE_LOGIN_ENTITY, GLOBALTYPE_ENTITYPLAYER, NULL, parse_MappedLoginEntity, true, SubscribeDBContainer, NULL, NULL, IsReadyDBContainer, CreateMappedLoginEntity, DestroyMappedLoginEntity, offsetof(ContainerTracker, hEntity)  }

#define CONTAINER_MAPPING_END { 0 }

//////////////////////////////////////////////////////////////////////////

ContainerMapping *FindContainerMappingForName(const char *pch);
	// Finds the container mapping for the given name.

ContainerMapping *FindContainerMapping(GatewayGlobalType type);
	// Finds the container mapping for the given type.

void WriteContainerJSON(char **pestr, GatewaySession *psess, ContainerTracker *ptracker);
	// Maps the given container into its web form, and writes it to the given EString in JSON.

void MakeMappedContainer(GatewaySession *psess, ContainerTracker *ptracker);
	// Fills in ptracker->pMapped with the mapped container.

void FreeMappedContainer(GatewaySession *psess, ContainerTracker *ptracker);
	// Frees the cached container in ptracker->pMapped.

typedef struct MappedLoginEntity MappedLoginEnity;
MappedLoginEnity *CreateMappedLoginEntity(GatewaySession *psess, ContainerTracker *ptracker, MappedLoginEnity *pent);
void DestroyMappedLoginEntity(GatewaySession *psess, ContainerTracker *ptracker, MappedLoginEnity *pent);


//////////////////////////////////////////////////////////////////////////
//
// Things that each project should provide.
//

LATELINK;
void ContainerMappingInit(void);
	// Optional.
	// Override in your project if you have any one-time set up to do.


LATELINK;
ContainerMapping *GetContainerMappings(void);
	// Required, If you expect any of this to work.
	// Each project should override this function to return an array of
	//   ContainerMapping structures. This will be used to find the right
	//   conversions between the real structure and the structure exposed
	//   to the web client.



#endif /* #ifndef GSLGATEWAYCONTAINERMAPPING_H__ */

/* End of File */
