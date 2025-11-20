/***************************************************************************
*     Copyright (c) 2005-2006, Cryptic Studios
*     All Rights Reserved
*     Confidential Property of Cryptic Studios
***************************************************************************/

#pragma once

#include "ResourceDBUtils.h"


typedef struct ContactDef ContactDef;
typedef struct ItemDef ItemDef;
typedef struct Message Message;
typedef struct MissionDef MissionDef;
typedef struct PlayerCostume PlayerCostume;
typedef struct RewardTable RewardTable;
typedef struct ZoneMapInfo ZoneMapInfo;


AUTO_STRUCT;
typedef struct ResourceDBRequest
{
	eResourceDBResourceType eResourceType;
	char *pResourceName;
	GlobalType eRequestingServerType;
	ContainerID iRequestingServerId;
} ResourceDBRequest;
extern ParseTable parse_ResourceDBRequest[];
#define TYPE_parse_ResourceDBRequest ResourceDBRequest

typedef struct ResourceDBNameSpace ResourceDBNameSpace;

AUTO_STRUCT AST_FORMATSTRING(HTML_DEF_FIELDS_TO_SHOW = "RemoveFromCache, Name, iMostRecentTimeAccessed, iTimesAccessed, iMemUsage");
typedef struct ResourceDBNameSpace
{
	char *pNameSpaceName; AST(KEY)

	U32 iTimeCreated; AST(FORMATSTRING(HTML_SECS_AGO = 1))
	U32 iTimePatchingBegan; AST(FORMATSTRING(HTML_SECS_AGO = 1))
	U32 iTimePatchingCompleted; AST(FORMATSTRING(HTML_SECS_AGO = 1))

	U32 iMostRecentTimeAccessed; AST(FORMATSTRING(HTML_SECS_AGO = 1))

	int iTimesAccessed;

	ResourceDBRequest **ppRequests; AST(FORMATSTRING(HTML_SKIP_IN_TABLE = 1))
	ZoneMapInfo **ppZoneMapInfos; AST(FORMATSTRING(HTML_SKIP_IN_TABLE = 1)) 
	Message **ppMessages; AST(FORMATSTRING(HTML_SKIP_IN_TABLE = 1))
	MissionDef **ppMissionDefs; AST(FORMATSTRING(HTML_SKIP_IN_TABLE = 1))
	ContactDef **ppContactDefs; AST(FORMATSTRING(HTML_SKIP_IN_TABLE = 1))
	PlayerCostume **ppPlayerCostumes; AST(FORMATSTRING(HTML_SKIP_IN_TABLE = 1))
	ItemDef **ppItemDefs; AST(FORMATSTRING(HTML_SKIP_IN_TABLE = 1))
	RewardTable **ppRewardTables; AST(FORMATSTRING(HTML_SKIP_IN_TABLE = 1))
	bool bLoaded;

	NameSpaceNamesRequest **ppNameSpaceReqs;  AST(FORMATSTRING(HTML_SKIP_IN_TABLE = 1))

	ResourceDBNameSpace *pOlder; NO_AST
	ResourceDBNameSpace *pNewer; NO_AST

	U32 iMemUsage; AST(FORMATSTRING(HTML_BYTES=1))

	TimedCallback *pInvalidationCallback; NO_AST

	AST_COMMAND("RemoveFromCache", "RemoveNSFromCacheByName $FIELD(NameSpaceName)")
} ResourceDBNameSpace;

void SendResourceToRequestingServer(ResourceDBNameSpace *pNameSpace, GlobalType eServerType, ContainerID iServerID, eResourceDBResourceType eType, char *pResourceName, void *pResource, char *pComment);

ResourceDBNameSpace *FindOrCreateNameSpaceFromResourceName(char *pResourceName);
ResourceDBNameSpace *FindOrCreateNameSpace(char *pNameSpaceName);
ResourceDBNameSpace *FindNameSpace(char *pNameSpaceName);


void ***GetResourceEarrayFromType(ResourceDBNameSpace *pNameSpace, eResourceDBResourceType eResourceType);


SimpleResourceRef *GetSimpleResourceRefFromType(eResourceDBResourceType eType, void *pResource);

void FillResourceList(NameSpaceAllResourceList *pNameList, ResourceDBNameSpace *pNameSpace, NameSpaceNamesRequest *pReq);

//ugc namespaces don't "exist" until the map manager says they do. So when we get a request for one, if it's one we don't yet 
//recognize, we have to verify it with the mapmanager before we patch it, etc.
bool CheckIfNameSpaceNeedsVerification(char *pNameSpaceName);


void RequestResource_Internal(GlobalType eServerType, ContainerID iServerID, eResourceDBResourceType eResourceType, char *pResourceName, bool bNamespaceVerified);


typedef struct EventCounter EventCounter;
extern EventCounter *pRequestCounter;
