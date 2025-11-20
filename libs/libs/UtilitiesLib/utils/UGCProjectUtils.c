#include "UGCProjectUtils.h"

#include "ResourceInfo.h"
#include "UtilitiesLib.h"
#include "estring.h"
#include "crypt.h"
#include "namelist.h"
#include "UGCProjectCommon.h"

AUTO_RUN_ANON(memBudgetAddMapping(__FILE__, BUDGET_Editors););

// This should be passed in with "-ugc_ShardName MyUgcShard" at the command line for GameServers, the LoginServer, and the MapManager
static char s_ugc_ShardName[128] = "";
AUTO_CMD_STRING(s_ugc_ShardName, ugc_ShardName) ACMD_EARLYCOMMANDLINE ACMD_ACCESSLEVEL(0);

bool ugc_DevMode;
AUTO_COMMAND ACMD_NAME( ugc_DevMode ) ACMD_EARLYCOMMANDLINE ACMD_ACCESSLEVEL( 0 );
void ugc_SetDevMode( bool value )
{
	ugc_DevMode = !!value;
}

int ugc_NumReviewsBeforeNonReviewerCanPlay = 5;
AUTO_CMD_INT( ugc_NumReviewsBeforeNonReviewerCanPlay, ugc_NumReviewsBeforeNonReviewerCanPlay ) ACMD_AUTO_SETTING(UGC, UGCDATAMANAGER, UGCSEARCHMANAGER);
	
const char *UGC_GetShardSpecificNSPrefix(const char* shardName)
{
	static char *spUGCPrefix = NULL;

	if (!shardName) {
		shardName = ugc_ShardName();
	}
	estrPrintf( &spUGCPrefix, "%s_ugc_", shardName );

	return spUGCPrefix;
}

int UGC_GetShardSpecificNSPrefixLen(const char* shardName)
{
	const char* estrName = UGC_GetShardSpecificNSPrefix(shardName);
	return estrLength(&estrName);
}

bool resNamespaceIsUGC(const char *resourceName)
{
	char ns[RESOURCE_NAME_MAX_SIZE];
	if (!resExtractNameSpace_s(resourceName, SAFESTR(ns), NULL, 0))
		return false;
	return namespaceIsUGC(ns);
}

bool namespaceIsUGC(const char *namespaceName)
{
	return strStartsWith(namespaceName, "ugc_") || strStartsWith(namespaceName, UGC_GetShardSpecificNSPrefix(NULL));
}

bool namespaceIsUGCOtherShard( const char* namespace, const char* otherShardName )
{
	assert( otherShardName );
	return strStartsWith(namespace, UGC_GetShardSpecificNSPrefix( otherShardName ));
	
}

bool namespaceIsUGCAnyShard( const char* namespace )
{
	return strStartsWith( namespace, "ugc_" ) || strstri( namespace, UGC_GetShardSpecificNSPrefix( "" ));
}

ContainerID UGCProject_GetContainerIDFromUGCNamespace(const char *pNameSpace)
{
	ContainerID iRetVal = 0;

	if (strStartsWith(pNameSpace, UGC_GetShardSpecificNSPrefix(NULL)))
	{
		pNameSpace += UGC_GetShardSpecificNSPrefixLen(NULL);
	}
	else if (strStartsWith(pNameSpace, "ugc_"))
	{
		pNameSpace += 4;
	} 
	else
	{
		return 0;
	}


	while (isdigit(pNameSpace[0]))
	{
		iRetVal *= 10;
		iRetVal += pNameSpace[0] - '0';
		pNameSpace++;
	}

	if (pNameSpace[0] == '_')
	{
		return iRetVal;
	}

	return 0;
}

ContainerID UGCProject_GetProjectContainerIDFromUGCResource(const char *pchResourceName)
{
	char pchNameSpace[RESOURCE_NAME_MAX_SIZE], pchBase[RESOURCE_NAME_MAX_SIZE];
	ContainerID iProjectID = 0;
	resExtractNameSpace(pchResourceName, pchNameSpace, pchBase);
	if(pchNameSpace && pchNameSpace[0])
	{
		iProjectID = UGCProject_GetContainerIDFromUGCNamespace(pchNameSpace);
	}
	return iProjectID;
}


void ugcProjectSeriesGetProjectIDs( ContainerID** out_eaProjectIDs, CONST_EARRAY_OF(UGCProjectSeriesNode) seriesNodes )
{
	int it;
	for( it = 0; it != eaSize( &seriesNodes ); ++it ) {
		ContainerID projID = seriesNodes[ it ]->iProjectID;

		if( projID ) {
			ea32PushUnique( out_eaProjectIDs, projID );
		}

		ugcProjectSeriesGetProjectIDs( out_eaProjectIDs, seriesNodes[ it ]->eaChildNodes );
	}
}

static char UGCIDStringProjPrefix[7] = "";
static int UGCIDStringProjPrefixLength = 0;
static U32 UGCIDStringProjExtraHash = 0;

static char UGCIDStringSeriesPrefix[7] = "";
static int UGCIDStringSeriesPrefixLength = 0;
static U32 UGCIDStringSeriesExtraHash = 0;

static char UGCIDStringProjPrefix_AllAlphaNum[7] = "";
static int UGCIDStringProjPrefixLength_AllAlphaNum = 0;

static char UGCIDStringSeriesPrefix_AllAlphaNum[7] = "";
static int UGCIDStringSeriesPrefixLength_AllAlphaNum = 0;

static __forceinline void UGCIDString_LazyInit(void)
{
	if (!UGCIDStringProjPrefix[0])
	{
		sprintf(UGCIDStringProjPrefix, "%s-%c", GetShortProductName(), GetShardNameFromShardInfoString()[0]);
		UGCIDStringProjPrefixLength = (int)strlen(UGCIDStringProjPrefix);
		UGCIDStringProjExtraHash = cryptAdler32(UGCIDStringProjPrefix, UGCIDStringProjPrefixLength);
		
		sprintf(UGCIDStringSeriesPrefix, "%sS-%c", GetShortProductName(), GetShardNameFromShardInfoString()[0]);
		UGCIDStringSeriesPrefixLength = (int)strlen(UGCIDStringSeriesPrefix);
		UGCIDStringSeriesExtraHash = cryptAdler32(UGCIDStringSeriesPrefix, UGCIDStringSeriesPrefixLength);
	
		sprintf(UGCIDStringProjPrefix_AllAlphaNum, "%s%c", GetShortProductName(), GetShardNameFromShardInfoString()[0]);
		UGCIDStringProjPrefixLength_AllAlphaNum = (int)strlen(UGCIDStringProjPrefix_AllAlphaNum);

		sprintf(UGCIDStringSeriesPrefix_AllAlphaNum, "%sS%c", GetShortProductName(), GetShardNameFromShardInfoString()[0]);
		UGCIDStringSeriesPrefixLength_AllAlphaNum = (int)strlen(UGCIDStringSeriesPrefix_AllAlphaNum);
	}
}

void UGCIDString_IntToString(U32 iInt, bool isSeries, char outString[UGC_IDSTRING_LENGTH_BUFFER_LENGTH])
{
	UGCIDString_LazyInit();

	if( !isSeries ) {
		memcpy(outString, UGCIDStringProjPrefix, UGCIDStringProjPrefixLength);
		IDString_IntToString(iInt, outString + UGCIDStringProjPrefixLength, UGCIDStringProjExtraHash);
	} else {
		memcpy(outString, UGCIDStringSeriesPrefix, UGCIDStringSeriesPrefixLength);
		IDString_IntToString(iInt, outString + UGCIDStringSeriesPrefixLength, UGCIDStringSeriesExtraHash);
	}
}



//returns true if string is valid
bool UGCIDString_StringToInt(const char *pString, U32 *pOutInt, bool* pOutIsSeries)
{
	UGCIDString_LazyInit();

	if (strStartsWith(pString, UGCIDStringProjPrefix_AllAlphaNum))
	{
		*pOutIsSeries = false;
		return IDString_StringToInt(pString + UGCIDStringProjPrefixLength_AllAlphaNum, pOutInt, UGCIDStringProjExtraHash);
	}
	else if (strStartsWith(pString, UGCIDStringProjPrefix))
	{
		*pOutIsSeries = false;
		return IDString_StringToInt(pString + UGCIDStringProjPrefixLength, pOutInt, UGCIDStringProjExtraHash);
	}
	else if (strStartsWith(pString, UGCIDStringSeriesPrefix_AllAlphaNum))
	{
		*pOutIsSeries = true;
		return IDString_StringToInt(pString + UGCIDStringSeriesPrefixLength_AllAlphaNum, pOutInt, UGCIDStringSeriesExtraHash);
	}
	else if (strStartsWith(pString, UGCIDStringSeriesPrefix))
	{
		*pOutIsSeries = true;
		return IDString_StringToInt(pString + UGCIDStringSeriesPrefixLength, pOutInt, UGCIDStringSeriesExtraHash);
	}
	else
	{
		*pOutIsSeries = false;
		*pOutInt = 0;
		return false;
	}
}

char *ugc_ShardName()
{
	return (s_ugc_ShardName && s_ugc_ShardName[0]) ? s_ugc_ShardName : GetShardNameFromShardInfoString();
}

void ugc_SetShardName(const char *name)
{
	strcpy(s_ugc_ShardName, name);
}

UGCProjectVersionState ugcProjectGetVersionStateConst(const UGCProjectVersion *pVersion)
{
	return pVersion->eState_USEACCESSOR;
}
