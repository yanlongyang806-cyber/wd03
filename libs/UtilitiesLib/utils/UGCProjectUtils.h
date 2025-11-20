#pragma once

#include "GlobalTypes.h"
#include "utils.h"

typedef enum UGCProjectVersionState UGCProjectVersionState;
typedef struct NOCONST(UGCProjectVersion) NOCONST(UGCProjectVersion);
typedef struct UGCProjectSeries UGCProjectSeries;
typedef struct UGCProjectSeriesNode UGCProjectSeriesNode;
typedef struct UGCProjectVersion UGCProjectVersion;
typedef U32 ContainerID;

extern bool ugc_DevMode;
extern int ugc_NumReviewsBeforeNonReviewerCanPlay;

//returns "SHARDNAME_ugc_"
const char *UGC_GetShardSpecificNSPrefix(const char* shardName);
int UGC_GetShardSpecificNSPrefixLen(const char* shardName);

// Returns true if the resource is in a UGC namespace
bool resNamespaceIsUGC(const char *resourceName);

// Returns true if the namespace is a UGC namespace
bool namespaceIsUGC(const char *namespaceName);

// Returns true if the namespace is a UGC namespace, from a specific shard
bool namespaceIsUGCOtherShard( const char* namespace, const char* otherShardName );

// Returns true if the namespace is a UGC namespace from any other shard
bool namespaceIsUGCAnyShard( const char* namespace );

ContainerID UGCProject_GetContainerIDFromUGCNamespace(const char *pNameSpace);
ContainerID UGCProject_GetProjectContainerIDFromUGCResource(const char *pchResourceName);

void ugcProjectSeriesGetProjectIDs( ContainerID** out_eaProjectIDs, CONST_EARRAY_OF(UGCProjectSeriesNode) seriesNodes );


//ugc id string is ID string + 2 or 3 letters of short product name + 1 letter of beginning of shard name + (optionally) 1 letter for type + 1 letter of dash, ie,
//STO_H12345678 
#define UGC_IDSTRING_LENGTH (ID_STRING_LENGTH + 6)
#define UGC_IDSTRING_LENGTH_BUFFER_LENGTH (UGC_IDSTRING_LENGTH + 1)

void UGCIDString_IntToString(U32 iInt, bool isSeries, char outString[UGC_IDSTRING_LENGTH_BUFFER_LENGTH]);

//returns true if string is valid
bool UGCIDString_StringToInt(const char *pString, U32 *pOutInt, bool* pOutIsSeries);

/// Validation functions needed by AppServers
void ugcValidateSeries( const UGCProjectSeries* ugcSeries );

char *ugc_ShardName();

void ugc_SetShardName(const char *name);

UGCProjectVersionState ugcProjectGetVersionStateConst(const UGCProjectVersion *pVersion);
