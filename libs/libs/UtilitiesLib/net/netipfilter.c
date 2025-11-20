#include "error.h"
#include "sock.h"
#include "netipfilter.h"
#include "foldercache.h"
#include "earray.h"
#include "estring.h"
#include "StashTable.h"
#include "netipfilter_c_ast.h"

#ifdef _WIN32
#include "wininclude.h"
#endif

#define RESTRICT_LOCAL_IPS_BY_DEFAULT false
#define NO_DEFAULT_LOCAL_IPS_BY_DEFAULT false

bool gbRestrictLocalIps = RESTRICT_LOCAL_IPS_BY_DEFAULT;

bool gbNoDefaultLocalIps = NO_DEFAULT_LOCAL_IPS_BY_DEFAULT; // a negative bool due to textparser defaults

char gpcIPFiltersFile[MAX_PATH] = {"server/IPFilters.txt"};
AUTO_CMD_STRING(gpcIPFiltersFile, ipfFile);


AUTO_ENUM;
typedef enum IPFilterType
{
	IPFT_SINGLE = 0,     // uIP1 matches IP
	IPFT_LOCALHOST,      // uIP1 matches any of our localhost IP addresses
	IPFT_RANGE,          // IP between uIP1 and uIP2
	IPFT_CIDR,           // (IP masked with uIP2) matches uIP1 (network, pre-masked)
	IPFT_GROUP,			 // Named group to do a recursive check on

	IPFT_COUNT
} IPFilterType;

AUTO_STRUCT;
typedef struct IPFilter
{
	IPFilterType type;
	U32 uIP1;            // IP (single), Start IP (range), Network (CIDR)
	U32 uIP2;            // Ignored (single), End IP (range), Netmask (CIDR)
	char *group;		 // Group name (group), Ignored (all others)
	bool reloadable;
} IPFilter;

AUTO_STRUCT;
typedef struct IPFilterGroup
{
	char *name; AST(STRUCTPARAM)
	IPFilter **pFilters;
	char **pLines; AST(NAME(IP))
} IPFilterGroup;

AUTO_STRUCT;
typedef struct IPFilterFile
{
	char **localIPs;        AST(ESTRING NAME("LocalIPs"))
	char **trustedIPs;      AST(ESTRING NAME("TrustedIPs"))
	bool  restrictLocalIps; AST(NAME("RestrictLocalIPs"))
	bool noDefaultLocalIps; AST(NAME("NoDefaultLocalIPs"))
	IPFilterGroup **groups; AST(NAME("Group"))
} IPFilterFile;



//static IPFilter **sppLocalFilters   = NULL;
//static IPFilter **sppTrustedFilters = NULL;
static StashTable spFilters = NULL;

// -------------------------------------------------------------------

static IPFilterGroup *ipfGetGroup(const char *name, bool createIfNeeded)
{
	IPFilterGroup *pFilter = NULL;
	if(!spFilters)
		spFilters = stashTableCreateWithStringKeys(8, StashDeepCopyKeys_NeverRelease);
	if(!stashFindPointer(spFilters, name, &pFilter))
	{
		if(createIfNeeded)
		{
			pFilter = StructCreate(parse_IPFilterGroup);
			pFilter->name = StructAllocString(name);
			stashAddPointer(spFilters, name, pFilter, true);
		}
		else
			return NULL;
	}
	return pFilter;
}

static bool ipfMatchesFilters(IPFilter **ppFilters, U32 uIP)
{
	U32 uNetOrderedIP = htonl(uIP);
	EARRAY_FOREACH_BEGIN(ppFilters, i);
	{
		IPFilter *filter = ppFilters[i];
		switch(filter->type)
		{
		xcase IPFT_SINGLE:    if  (uIP == filter->uIP1)                               return true;
		xcase IPFT_LOCALHOST: if ((uIP == LOCALHOST_ADDR) || uIP == getHostLocalIp()) return true;
		xcase IPFT_RANGE:     if ((uNetOrderedIP >= htonl(filter->uIP1)) && (uNetOrderedIP <= htonl(filter->uIP2)))     return true;
		xcase IPFT_CIDR:      if ((uNetOrderedIP & filter->uIP2) == filter->uIP1)        return true;
		xcase IPFT_GROUP:	  { IPFilterGroup *pGroup = ipfGetGroup(filter->group, false);
								if(pGroup && ipfMatchesFilters(pGroup->pFilters, uIP)) return true;}
		}
	}
	EARRAY_FOREACH_END;

	return false;
}

bool ipfGroupExists(const char *groupName)
{
	return ipfGetGroup(groupName, false) != NULL;
}

bool ipfIsLocalIp(U32 uIP)
{
	return ipfIsIpInGroup("Local", uIP);
}

bool ipfIsTrustedIp(U32 uIP)
{
	return ipfIsIpInGroup("Trusted", uIP);
}

bool ipfIsIpInGroup(const char *groupName, U32 uIP)
{
	IPFilterGroup *group;

	if(uIP == 0)
		return false;

	group = ipfGetGroup(groupName, false);
	if(!group)
		return false;

	if(ipfMatchesFilters(group->pFilters, uIP))
		return true;

	return false;
}

bool ipfIsLocalIpString(const char *ipstr)
{
	U32 uIP = ipFromString(ipstr);
	return ipfIsLocalIp(uIP);
}

bool ipfIsTrustedIpString(const char *ipstr)
{
	U32 uIP = ipFromString(ipstr);
	return ipfIsTrustedIp(uIP);
}

bool ipfIsIpStringInGroup(const char *groupName, const char *ipstr)
{
	U32 uIP = ipFromString(ipstr);
	return ipfIsIpInGroup(groupName, uIP);
}

static IPFilter * ipfCreate(const char *filterString)
{
	IPFilter   *filter   = NULL;
	const char *dashLoc  = strchr(filterString, '-');
	const char *slashLoc = strchr(filterString, '/');
	const char *atLoc    = strchr(filterString, '@');

	if(atLoc)
	{
		// Group Notation!

		char *group = estrStackCreateFromStr(atLoc+1);
		estrTrimLeadingAndTrailingWhitespace(&group);

		if(*group)
		{
			filter = StructCreate(parse_IPFilter);
			filter->type = IPFT_GROUP;
			filter->group = strdup(group);
		}
		estrDestroy(&group);
	}
	else if(dashLoc)
	{
		// Range Notation!

		char *startIPStr = estrStackCreateFromStr(filterString);
		estrSetSize(&startIPStr, dashLoc-filterString);
		estrTrimLeadingAndTrailingWhitespace(&startIPStr);

		dashLoc++;
		while(*dashLoc && (*dashLoc == ' '))
			dashLoc++;

		if(*dashLoc)
		{
			U32 uStartIP = ipFromString(startIPStr);
			U32 uEndIP   = ipFromString(dashLoc);

			filter = StructCreate(parse_IPFilter);
			filter->type = IPFT_RANGE;
			filter->uIP1 = uStartIP;
			filter->uIP2 = uEndIP;
		}

		estrDestroy(&startIPStr);
	}
	else if(slashLoc)
	{
		// CIDR Notation!

		char *subnetStr = estrStackCreateFromStr(filterString);
		estrSetSize(&subnetStr, slashLoc-filterString);
		estrTrimLeadingAndTrailingWhitespace(&subnetStr);

		slashLoc++;
		while(*slashLoc && (*slashLoc == ' '))
			slashLoc++;

		if(*slashLoc)
		{
			U32 uMask   = 0;
			U32 uSubnet = ipFromString(subnetStr);
			int uSuffix = atoi(slashLoc);
			int i;

			uSuffix = CLAMP(uSuffix, 0, 32);
			for(i=0;i<uSuffix;i++)
			{
				uMask |= (1 << (31-i));
			}

			filter = StructCreate(parse_IPFilter);
			filter->type = IPFT_CIDR;
			filter->uIP1 = (htonl(uSubnet) & uMask); // e.g. 192.168.0.0
			filter->uIP2 = uMask;                    // e.g. 255.255.0.0
		}

		estrDestroy(&subnetStr);
	}
	else if(!stricmp(filterString, "localhost") || !stricmp(filterString, "127.0.0.1"))
	{
		filter = StructCreate(parse_IPFilter);
		filter->type = IPFT_LOCALHOST;
	}
	else
	{
		// The normal case ... just a regular host.

		U32 uFilterIP = ipFromString(filterString);

		filter = StructCreate(parse_IPFilter);
		filter->type = IPFT_SINGLE;
		filter->uIP1 = uFilterIP;
	}

	return filter;
}

static bool ipfAddFilterInternal(const char *groupName, const char *filterString, bool reloadable)
{
	IPFilterGroup *group = ipfGetGroup(groupName, true);
	IPFilter *filter = ipfCreate(filterString);
	if(filter)
	{
		filter->reloadable = reloadable;
		eaPush(&group->pFilters, filter);
	}

	return (filter != NULL);
}

bool ipfAddFilter(const char *groupName, const char *filterString)
{
	return ipfAddFilterInternal(groupName, filterString, false);
}

void ipfResetFilters(bool full)
{
	gbRestrictLocalIps  = RESTRICT_LOCAL_IPS_BY_DEFAULT;
	gbNoDefaultLocalIps = NO_DEFAULT_LOCAL_IPS_BY_DEFAULT;

	if(full)
		stashTableClearStruct(spFilters, NULL, parse_IPFilterGroup);
	else
	{
		FOR_EACH_IN_STASHTABLE(spFilters, IPFilterGroup, group)
			FOR_EACH_IN_EARRAY(group->pFilters, IPFilter, filter)
				if(filter->reloadable)
					eaRemove(&group->pFilters, FOR_EACH_IDX(group->pFilters, filter));
			FOR_EACH_END
		FOR_EACH_END
	}
}

void ipfAddDefaultLocalFilters()
{
	// Add all IETF reserved private IPs
	ipfAddFilterInternal("Local", "10.0.0.0/8", true);
	ipfAddFilterInternal("Local", "172.16.0.0/12", true);
	ipfAddFilterInternal("Local", "192.168.0.0/16", true);
	ipfAddFilterInternal("Local", "127.0.0.1", true);
}

static void reloadIPFilters(const char *relpath, int when)
{
	if(!stricmp(gpcIPFiltersFile, relpath))
	{
		ipfLoadDefaultFilters();
	}
}

AUTO_COMMAND ACMD_NAME(ipfReload);
void ipfLoadDefaultFilters(void)
{
	IPFilterFile filterfile = {0};

	static bool sbInitialized = false;
	if(!sbInitialized)
	{
		FolderCacheSetCallback(FOLDER_CACHE_CALLBACK_UPDATE, "server/*.txt", reloadIPFilters);
	}

	loadstart_printf("Loading IP Filters from %s... ", gpcIPFiltersFile);

	ipfResetFilters(false);

	if(ParserReadTextFile(gpcIPFiltersFile, parse_IPFilterFile, &filterfile, 0))
	{
		int iFilterCount = 0;

		EARRAY_FOREACH_BEGIN(filterfile.localIPs, i);
		{
			if(ipfAddFilterInternal("Local", filterfile.localIPs[i], true))
				iFilterCount++;
		}
		EARRAY_FOREACH_END;


		EARRAY_FOREACH_BEGIN(filterfile.trustedIPs, i);
		{
			if(ipfAddFilterInternal("Trusted", filterfile.trustedIPs[i], true))
				iFilterCount++;
		}
		EARRAY_FOREACH_END;

		EARRAY_FOREACH_BEGIN(filterfile.groups, i);
		{
			EARRAY_FOREACH_BEGIN(filterfile.groups[i]->pLines, j);
			{
				if(ipfAddFilterInternal(filterfile.groups[i]->name, filterfile.groups[i]->pLines[j], true))
					iFilterCount++;
			}
			EARRAY_FOREACH_END;
		}
		EARRAY_FOREACH_END;

		gbRestrictLocalIps  = filterfile.restrictLocalIps;
		gbNoDefaultLocalIps = filterfile.noDefaultLocalIps;

		if(!gbRestrictLocalIps)
			ipfAddFilterInternal("Trusted", "@Local", true);
		if(!gbNoDefaultLocalIps)
			ipfAddDefaultLocalFilters();

		StructDeInit(parse_IPFilterFile, &filterfile);

		loadend_printf("%d Rule%s [%s] [%s].",
			iFilterCount,
			iFilterCount==1?"":"s",
			gbRestrictLocalIps ? "LocalRestricted" : "LocalTrusted",
			gbNoDefaultLocalIps ? "NoDefaultLocalIps" : "DefaultLocalIps"
			);
	}
	else
	{
		if(!gbRestrictLocalIps)
			ipfAddFilterInternal("Trusted", "@Local", true);
		if(!gbNoDefaultLocalIps)
			ipfAddDefaultLocalFilters();
		loadend_printf("Skipped.");
	}
}

AUTO_COMMAND;
void ipfTest(const char *ipstr)
{
	printf("IP String [%s]: [%s] [%s]\n",
		ipstr,
		ipfIsLocalIpString(ipstr)   ? "Local" : "Remote",
		ipfIsTrustedIpString(ipstr) ? "Trusted" : "Untrusted");
}

AUTO_COMMAND;
void ipfTestGroup(const char *ipstr)
{
	bool foundAny = false;
	U32 uIP = ipFromString(ipstr);
	printf("IP String [%s]:", ipstr);
	FOR_EACH_IN_STASHTABLE(spFilters, IPFilterGroup, group)
		if(ipfMatchesFilters(group->pFilters, uIP))
		{
			foundAny = true;
			printf(" [%s]", group->name);
		}
	FOR_EACH_END
	if(!foundAny)
		printf(" No matching groups");
	printf("\n");
}

#include "netipfilter_c_ast.c"
