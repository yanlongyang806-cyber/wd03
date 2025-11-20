/***************************************************************************



***************************************************************************/

#include "GlobalTypes.h"


#include "globalTypes_h_ast.h"
#include "sysutil.h"
#include "estring.h"
#include "Message.h"
#include "stringcache.h"
#include "stringUtil.h"
#include "ThreadSafeMemoryPool.h"
#include "cmdParse.h"
#include "appLocale.h"

AUTO_RUN_ANON(memBudgetAddMapping(__FILE__, BUDGET_GameSystems););

//a single globaltype-of-cur-app variable, not needed for random utility apps, but needed
//for all servers, gameclient
GlobalType gAppGlobalType = GLOBALTYPE_NONE;


GlobalTypeMapping genericTypeMappings[] = 
{
	{GLOBALTYPE_NONE, "InvalidType", "", SCHEMATYPE_NOSCHEMA, GLOBALTYPE_NONE}, // the invalid type, from GlobalTypes.h
	{GLOBALTYPE_OBJECTDB, "ObjectDB", "", SCHEMATYPE_NOSCHEMA, GLOBALTYPE_NONE},						
	{GLOBALTYPE_CLONEOBJECTDB, "CloneObjectDB", "", SCHEMATYPE_NOSCHEMA, GLOBALTYPE_NONE},
	{GLOBALTYPE_TRANSACTIONSERVER, "TransactionServer", "", SCHEMATYPE_NOSCHEMA, GLOBALTYPE_NONE},		
	{GLOBALTYPE_LAUNCHER, "Launcher", "", SCHEMATYPE_NOSCHEMA, GLOBALTYPE_NONE},						
	{GLOBALTYPE_LOGSERVER, "LogServer", "", SCHEMATYPE_NOSCHEMA, GLOBALTYPE_NONE},						
	{GLOBALTYPE_CLIENT, "Client", "", SCHEMATYPE_NOSCHEMA, GLOBALTYPE_NONE},							
	{GLOBALTYPE_CONTROLLER, "Controller", "", SCHEMATYPE_NOSCHEMA, GLOBALTYPE_NONE},					
	{GLOBALTYPE_APPSERVER, "AppServer", "", SCHEMATYPE_NOSCHEMA, GLOBALTYPE_NONE},						
	{GLOBALTYPE_CONTROLLERTRACKER, "ControllerTracker", "", SCHEMATYPE_NOSCHEMA, GLOBALTYPE_NONE},		
	{GLOBALTYPE_MAPMANAGER, "MapManager", "", SCHEMATYPE_NOSCHEMA, GLOBALTYPE_APPSERVER},					
	{GLOBALTYPE_MULTIPLEXER, "Multiplexer", "", SCHEMATYPE_NOSCHEMA, GLOBALTYPE_NONE},					
	{GLOBALTYPE_MASTERCONTROLPROGRAM, "MasterControlProgram", "", SCHEMATYPE_NOSCHEMA, GLOBALTYPE_NONE},
	{GLOBALTYPE_GAMESERVER, "GameServer", "G", SCHEMATYPE_NOSCHEMA, GLOBALTYPE_NONE},					
	{GLOBALTYPE_TEAMSERVER, "TeamServer", "", SCHEMATYPE_NOSCHEMA, GLOBALTYPE_APPSERVER},					
	{GLOBALTYPE_GUILDSERVER, "GuildServer", "", SCHEMATYPE_NOSCHEMA, GLOBALTYPE_APPSERVER},					
	{GLOBALTYPE_QUEUESERVER, "QueueServer", "", SCHEMATYPE_NOSCHEMA, GLOBALTYPE_APPSERVER},					
	{GLOBALTYPE_CHATSERVER, "ChatServer", "", SCHEMATYPE_NOSCHEMA, GLOBALTYPE_NONE},
	{GLOBALTYPE_LEADERBOARDSERVER, "LeaderboardServer", "", SCHEMATYPE_NOSCHEMA, GLOBALTYPE_APPSERVER},
	{GLOBALTYPE_AUCTIONSERVER, "AuctionServer", "", SCHEMATYPE_NOSCHEMA, GLOBALTYPE_APPSERVER},
	{GLOBALTYPE_TESTCLIENTSERVER, "TestClientServer", "", SCHEMATYPE_NOSCHEMA, GLOBALTYPE_APPSERVER},
	{GLOBALTYPE_ACCOUNTSERVER, "AccountServer", "", SCHEMATYPE_NOSCHEMA, GLOBALTYPE_NONE},					
	{GLOBALTYPE_ACCOUNT, "Account", "", SCHEMATYPE_PERSISTED, GLOBALTYPE_NONE},					
	{GLOBALTYPE_LOGINSERVER, "LoginServer", "", SCHEMATYPE_NOSCHEMA, GLOBALTYPE_NONE},					
	{GLOBALTYPE_ENTITY, "Entity", "", SCHEMATYPE_LOCAL, GLOBALTYPE_NONE},								
	{GLOBALTYPE_ENTITYPLAYER, "EntityPlayer", "P", SCHEMATYPE_PERSISTED, GLOBALTYPE_ENTITY},				
	{GLOBALTYPE_ENTITYCRITTER, "EntityCritter", "C", SCHEMATYPE_LOCAL, GLOBALTYPE_ENTITY},
	{GLOBALTYPE_ENTITYSAVEDPET, "EntitySavedPet", "S", SCHEMATYPE_PERSISTED, GLOBALTYPE_ENTITY},
	{GLOBALTYPE_ENTITYPUPPET, "EntityPuppet", "U", SCHEMATYPE_PERSISTED, GLOBALTYPE_ENTITY},
	{GLOBALTYPE_TEAM, "Team", "", SCHEMATYPE_PERSISTED, GLOBALTYPE_NONE},
	{GLOBALTYPE_AUCTIONLOT, "AuctionLot", "", SCHEMATYPE_PERSISTED, GLOBALTYPE_NONE},
	{GLOBALTYPE_UGCSEARCHMANAGER, "UGCSearchManager", "", SCHEMATYPE_NOSCHEMA, GLOBALTYPE_NONE},
	{GLOBALTYPE_PREFSTORE, "PrefStore", "", SCHEMATYPE_PERSISTED, GLOBALTYPE_NONE},
	{GLOBALTYPE_GUILD, "Guild", "", SCHEMATYPE_PERSISTED, GLOBALTYPE_NONE},
	{GLOBALTYPE_TESTGAMESERVER, "TestGameServer", "", SCHEMATYPE_NOSCHEMA, GLOBALTYPE_NONE},	
	{GLOBALTYPE_ERRORTRACKER, "ErrorTracker", "", SCHEMATYPE_NOSCHEMA, GLOBALTYPE_NONE},	
	{GLOBALTYPE_CLIENTCONTROLLER, "ClientController", "", SCHEMATYPE_NOSCHEMA, GLOBALTYPE_NONE},	
	{GLOBALTYPE_TESTCLIENT, "TestClient", "", SCHEMATYPE_NOSCHEMA, GLOBALTYPE_NONE},	
	{GLOBALTYPE_LOGPARSER, "LogParser", "", SCHEMATYPE_NOSCHEMA, GLOBALTYPE_NONE},						
	{GLOBALTYPE_ARBITRARYPROCESS, "ArbitraryProcess", "", SCHEMATYPE_NOSCHEMA, GLOBALTYPE_NONE},
	{GLOBALTYPE_TICKETTRACKER, "TicketTracker", "", SCHEMATYPE_NOSCHEMA, GLOBALTYPE_NONE},
	{GLOBALTYPE_ERRORTRACKERENTRY, "ErrorTrackerEntry", "", SCHEMATYPE_PERSISTED, GLOBALTYPE_NONE},
	{GLOBALTYPE_ERRORTRACKERENTRY_LAST, "ErrorTrackerEntry_Last", "", SCHEMATYPE_PERSISTED, GLOBALTYPE_NONE},
	{GLOBALTYPE_TICKETENTRY, "TicketEntry", "", SCHEMATYPE_PERSISTED, GLOBALTYPE_NONE},
	{GLOBALTYPE_TICKETUSER, "TicketTrackerUser", "", SCHEMATYPE_PERSISTED, GLOBALTYPE_NONE},
	{GLOBALTYPE_TICKETGROUP, "TicketUserGroup", "", SCHEMATYPE_PERSISTED, GLOBALTYPE_NONE},
	{GLOBALTYPE_PRODUCTKEY_USED, "ProductKey_Used", "", SCHEMATYPE_PERSISTED, GLOBALTYPE_NONE},
	{GLOBALTYPE_PRODUCTKEY_NEW, "ProductKey_New", "", SCHEMATYPE_PERSISTED, GLOBALTYPE_NONE},
	{GLOBALTYPE_CHATCHANNEL, "ChatChannel", "", SCHEMATYPE_PERSISTED, GLOBALTYPE_CHATSERVER},
	{GLOBALTYPE_CHATUSER, "ChatUser", "", SCHEMATYPE_PERSISTED, GLOBALTYPE_CHATSERVER},
	{GLOBALTYPE_HEADSHOTSERVER, "HeadShotServer", "", SCHEMATYPE_NOSCHEMA, GLOBALTYPE_NONE},
	{GLOBALTYPE_MACHINE, "Machine", "", SCHEMATYPE_NOSCHEMA, GLOBALTYPE_NONE},
	{GLOBALTYPE_WEBREQUESTSERVER, "WebRequestServer", "", SCHEMATYPE_NOSCHEMA, GLOBALTYPE_NONE},
	{GLOBALTYPE_CONTINUOUSBUILDER, "ContinuousBuilder", "", SCHEMATYPE_NOSCHEMA, GLOBALTYPE_NONE},
	{GLOBALTYPE_PATCHSERVER, "PatchServer", "", SCHEMATYPE_NOSCHEMA, GLOBALTYPE_NONE},
	{GLOBALTYPE_ACCOUNTPROXYSERVER, "AccountProxyServer", "", SCHEMATYPE_NOSCHEMA, GLOBALTYPE_APPSERVER},
	{GLOBALTYPE_ACCOUNTPROXYSERVER_LOCKS, "AccountProxyServerLocks", "", SCHEMATYPE_PERSISTED, GLOBALTYPE_NONE},
	{GLOBALTYPE_ACCOUNTSERVER_LOCKS, "AccountServerLocks", "", SCHEMATYPE_PERSISTED, GLOBALTYPE_NONE},
	{GLOBALTYPE_ACCOUNTSERVER_SUBSCRIPTION, "AccountServerSubscription", "", SCHEMATYPE_PERSISTED, GLOBALTYPE_NONE},
	{GLOBALTYPE_ACCOUNTSERVER_INTERNAL_SUBSCRIPTION, "AccountServerInternalSubscription", "", SCHEMATYPE_PERSISTED, GLOBALTYPE_NONE},
	{GLOBALTYPE_ACCOUNTSERVER_PRODUCT, "AccountServerProduct", "", SCHEMATYPE_PERSISTED, GLOBALTYPE_NONE},
	{GLOBALTYPE_QUEUEINFO, "QueueInfo", "", SCHEMATYPE_PERSISTED, GLOBALTYPE_NONE},
	{GLOBALTYPE_ACCOUNTSERVER_GLOBALDATA, "AccountServerGlobalData", "", SCHEMATYPE_PERSISTED, GLOBALTYPE_NONE},
	{GLOBALTYPE_ENTITYDESCRIPTOR, "EntityDescriptor", "", SCHEMATYPE_PERSISTED, GLOBALTYPE_NONE},
	{GLOBALTYPE_GATEWAYLOGINLAUNCHER, "GatewayLoginLauncher", "", SCHEMATYPE_NOSCHEMA, GLOBALTYPE_NONE},
	{GLOBALTYPE_CLUSTERCONTROLLER, "ClusterController", "", SCHEMATYPE_NOSCHEMA, GLOBALTYPE_NONE},
	{GLOBALTYPE_UGCDATAMANAGER, "UGCDataManager", "", SCHEMATYPE_NOSCHEMA, GLOBALTYPE_APPSERVER},
	{GLOBALTYPE_DEPRECATED_66, "Deprecated66", "", SCHEMATYPE_PERSISTED, GLOBALTYPE_NONE},
	{GLOBALTYPE_GAMEACCOUNTDATA, "GameAccountData", "", SCHEMATYPE_PERSISTED, GLOBALTYPE_NONE},
	{GLOBALTYPE_ACCOUNTSERVER_PURCHASE_LOG, "AccountServerPurchaseLog", "", SCHEMATYPE_PERSISTED, GLOBALTYPE_NONE},
	{GLOBALTYPE_DIARYSERVER, "DiaryServer", "", SCHEMATYPE_NOSCHEMA, GLOBALTYPE_APPSERVER},
	{GLOBALTYPE_PLAYERDIARY, "PlayerDiary", "", SCHEMATYPE_PERSISTED, GLOBALTYPE_NONE},
	{GLOBALTYPE_DIARYENTRYBUCKET, "DiaryEntryBucket", "", SCHEMATYPE_PERSISTED, GLOBALTYPE_NONE},
	{GLOBALTYPE_OBJECTDB_MERGER, "ObjectDB_Merger", "", SCHEMATYPE_NOSCHEMA, GLOBALTYPE_NONE},				
	{GLOBALTYPE_GLOBALCHATSERVER, "GlobalChatServer", "", SCHEMATYPE_NOSCHEMA, GLOBALTYPE_NONE},
	{GLOBALTYPE_SHARDVARIABLE, "ShardVariableContainer", "", SCHEMATYPE_PERSISTED, GLOBALTYPE_NONE},					
	{GLOBALTYPE_RESOURCEDB, "ResourceDB", "", SCHEMATYPE_NOSCHEMA, GLOBALTYPE_NONE},	
	{GLOBALTYPE_TESTSERVER, "TestServer", "", SCHEMATYPE_NOSCHEMA, GLOBALTYPE_NONE},
	{GLOBALTYPE_ACCOUNTSERVER_KEYGROUP, "ProductKeyGroup", "", SCHEMATYPE_PERSISTED, GLOBALTYPE_NONE},
	{GLOBALTYPE_ACCOUNTSERVER_KEYBATCH, "ProductKeyBatch", "", SCHEMATYPE_PERSISTED, GLOBALTYPE_NONE},
	{GLOBALTYPE_ACCOUNTSERVER_DISCOUNT, "AccountServerDiscount", "", SCHEMATYPE_PERSISTED, GLOBALTYPE_NONE},
	{GLOBALTYPE_TESTING, "TestingContainer", "", SCHEMATYPE_PERSISTED, GLOBALTYPE_NONE},
	{GLOBALTYPE_JOBMANAGER, "JobManager", "", SCHEMATYPE_NOSCHEMA, GLOBALTYPE_NONE},
	{GLOBALTYPE_CLIENTBINNER, "ClientBinner", "", SCHEMATYPE_NOSCHEMA, GLOBALTYPE_NONE},
	{GLOBALTYPE_SERVERBINNER, "ServerBinner", "", SCHEMATYPE_NOSCHEMA, GLOBALTYPE_NONE},
	{GLOBALTYPE_BCNMASTERSERVER, "BcnMasterServer", "MS", SCHEMATYPE_NOSCHEMA, GLOBALTYPE_GAMESERVER},
	{GLOBALTYPE_BCNSUBSERVER, "BcnSubServer", "RS", SCHEMATYPE_NOSCHEMA, GLOBALTYPE_GAMESERVER},
	{GLOBALTYPE_BCNCLIENTSENTRY, "BcnClientSentry", "BS", SCHEMATYPE_NOSCHEMA, GLOBALTYPE_GAMESERVER},
	{GLOBALTYPE_UGCPROJECT, "UgcProject", "", SCHEMATYPE_PERSISTED, GLOBALTYPE_NONE},	
	{GLOBALTYPE_LEADERBOARD, "Leaderboard", "", SCHEMATYPE_PERSISTED, GLOBALTYPE_LEADERBOARDSERVER},
	{GLOBALTYPE_LEADERBOARDDATA, "LeaderboardData", "", SCHEMATYPE_PERSISTED, GLOBALTYPE_LEADERBOARDSERVER},
	{GLOBALTYPE_VIRTUALSHARD, "VirtualShard", "", SCHEMATYPE_PERSISTED, GLOBALTYPE_NONE},	
	{GLOBALTYPE_CRYPTICMON, "CrypticMon", "", SCHEMATYPE_NOSCHEMA, GLOBALTYPE_NONE},
	{GLOBALTYPE_PERSISTEDSTORE, "PersistedStore", "", SCHEMATYPE_PERSISTED, GLOBALTYPE_APPSERVER},
	{GLOBALTYPE_PATCHSERVER_PATCHDB, "PatchServerPatchDb", "", SCHEMATYPE_PERSISTED, GLOBALTYPE_NONE},
	{GLOBALTYPE_PATCHSERVER_DIRENTRY, "PatchServerDirEntry", "", SCHEMATYPE_PERSISTED, GLOBALTYPE_NONE},
	{GLOBALTYPE_PATCHSERVER_CHECKIN, "PatchServerCheckin", "", SCHEMATYPE_PERSISTED, GLOBALTYPE_NONE},
	{GLOBALTYPE_PATCHSERVER_NAMEDVIEW, "PatchServerNamedView", "", SCHEMATYPE_PERSISTED, GLOBALTYPE_NONE},
	{GLOBALTYPE_ACCOUNTSERVER_KEYVALUECHAIN, "AccountServerKeyValueChain", "", SCHEMATYPE_PERSISTED, GLOBALTYPE_NONE},
	{GLOBALTYPE_ACCOUNTSERVER_LOG_BATCH, "AccountServerLogBatch", "", SCHEMATYPE_PERSISTED, GLOBALTYPE_NONE},
	{GLOBALTYPE_ACCOUNTSTUB, "AccountStub", "", SCHEMATYPE_PERSISTED, GLOBALTYPE_NONE},
	{GLOBALTYPE_CHATRELAY, "ChatRelay", "", SCHEMATYPE_NOSCHEMA, GLOBALTYPE_NONE},
	{GLOBALTYPE_UGCPROJECTSERIES, "UgcProjectSeries", "", SCHEMATYPE_PERSISTED, GLOBALTYPE_NONE},
	{GLOBALTYPE_ENTITYSHAREDBANK, "EntitySharedBank", "", SCHEMATYPE_PERSISTED, GLOBALTYPE_ENTITY},
	{GLOBALTYPE_ALL, "All", "", SCHEMATYPE_NOSCHEMA, GLOBALTYPE_NONE},
	{GLOBALTYPE_CURRENCYEXCHANGE, "CurrencyExchange", "", SCHEMATYPE_PERSISTED, GLOBALTYPE_NONE},
	{GLOBALTYPE_ENTITYPROJECTILE, "EntityProjectile", "Pr", SCHEMATYPE_LOCAL, GLOBALTYPE_ENTITY},
	{GLOBALTYPE_PROCEDURAL, "Procedural", "", SCHEMATYPE_NOSCHEMA, GLOBALTYPE_NONE},
	{GLOBALTYPE_ENTITYGUILDBANK, "EntityGuildBank", "", SCHEMATYPE_PERSISTED, GLOBALTYPE_ENTITY}, 
	{GLOBALTYPE_CBMONITOR, "CBMonitor", "", SCHEMATYPE_NOSCHEMA, GLOBALTYPE_NONE},		
	{GLOBALTYPE_ACCOUNTSERVER_PWCOMMONACCOUNT, "PerfectWorldAccountBatch", "", SCHEMATYPE_PERSISTED, GLOBALTYPE_NONE},
	{GLOBALTYPE_LOGINHAMMER, "LoginHammer", "", SCHEMATYPE_NOSCHEMA, GLOBALTYPE_NONE},		
	{GLOBALTYPE_NOTESSERVER, "NotesServer", "", SCHEMATYPE_NOSCHEMA, GLOBALTYPE_NONE},		
	{GLOBALTYPE_CLONEOFCLONE, "CloneOfClone", "", SCHEMATYPE_NOSCHEMA, GLOBALTYPE_NONE},
	{GLOBALTYPE_EVENTCONTAINER, "EventContainer", "", SCHEMATYPE_PERSISTED, GLOBALTYPE_NONE},
	{GLOBALTYPE_TESTSUITE, "TestSuite", "", SCHEMATYPE_NOSCHEMA, GLOBALTYPE_NONE},						
	{GLOBALTYPE_GETVRML, "GetVrml", "", SCHEMATYPE_NOSCHEMA, GLOBALTYPE_NONE},						
	{GLOBALTYPE_GATEWAYSERVER, "GatewayServer", "", SCHEMATYPE_NOSCHEMA, GLOBALTYPE_NONE},
    {GLOBALTYPE_GROUPPROJECTCONTAINER, "GroupProjectContainer", "", SCHEMATYPE_LOCAL, GLOBALTYPE_NONE},								
    {GLOBALTYPE_GROUPPROJECTCONTAINERGUILD, "GroupProjectContainerGuild", "", SCHEMATYPE_PERSISTED, GLOBALTYPE_GROUPPROJECTCONTAINER},
    {GLOBALTYPE_GROUPPROJECTSERVER, "GroupProjectServer", "", SCHEMATYPE_NOSCHEMA, GLOBALTYPE_NONE},
	{GLOBALTYPE_ACCOUNTSERVER_TRANSACTIONLOG, "AccountServer_TransactionLog", "", SCHEMATYPE_PERSISTED, GLOBALTYPE_NONE},
	{GLOBALTYPE_TEXTURESERVER, "TextureServer", "", SCHEMATYPE_NOSCHEMA, GLOBALTYPE_NONE},
	{GLOBALTYPE_UGCACCOUNT, "UGCAccount", "", SCHEMATYPE_PERSISTED, GLOBALTYPE_NONE},
	{GLOBALTYPE_ACCOUNTSERVER_VIRTUALCURRENCY, "AccountServere_VirtualCurrency", "", SCHEMATYPE_PERSISTED, GLOBALTYPE_NONE},
    {GLOBALTYPE_GROUPPROJECTCONTAINERPLAYER, "GroupProjectContainerPlayer", "", SCHEMATYPE_PERSISTED, GLOBALTYPE_GROUPPROJECTCONTAINER},
	{GLOBALTYPE_CHATSHARD, "ChatShard", "", SCHEMATYPE_PERSISTED, GLOBALTYPE_CHATSERVER},
	{GLOBALTYPE_CHATCLUSTER, "ChatCluster", "", SCHEMATYPE_PERSISTED, GLOBALTYPE_CHATSERVER},
	{GLOBALTYPE_DEEPSPACESERVER, "DeepSpaceServer", "", SCHEMATYPE_NOSCHEMA, GLOBALTYPE_NONE},
	{GLOBALTYPE_AUCTIONPRICEHISTORYCONTAINER, "AuctionPriceHistories", "", SCHEMATYPE_PERSISTED, GLOBALTYPE_NONE},
    {GLOBALTYPE_CURRENCYEXCHANGESERVER, "CurrencyExchangeServer", "", SCHEMATYPE_NOSCHEMA, GLOBALTYPE_APPSERVER},
    {GLOBALTYPE_SHARDMERGE, "ShardMerge", "", SCHEMATYPE_NOSCHEMA, GLOBALTYPE_NONE},
    {GLOBALTYPE_MACHINESTATUS, "MachineStatus", "", SCHEMATYPE_NOSCHEMA, GLOBALTYPE_NONE},
	{GLOBALTYPE_OVERLORD, "Overlord", "", SCHEMATYPE_NOSCHEMA, GLOBALTYPE_NONE},
	{GLOBALTYPE_GAMELOGREPORTER, "GameLogReporter", "", SCHEMATYPE_NOSCHEMA, GLOBALTYPE_NONE},
	{GLOBALTYPE_FARADAY, "Faraday", "", SCHEMATYPE_NOSCHEMA, GLOBALTYPE_NONE},					
	{GLOBALTYPE_REVISIONFINDER,"RevisionFinder","",SCHEMATYPE_NOSCHEMA,GLOBALTYPE_NONE},
	{GLOBALTYPE_GATEWAYGAMEDATA, "GatewayGameData","",SCHEMATYPE_PERSISTED,GLOBALTYPE_NONE},
	{0}
};


GlobalTypeMapping globalTypeMapping[GLOBALTYPE_MAXTYPES + 1];

// Handle the mapping

static StashTable globalNameToType = 0;
static StashTable globalShortNameToType = 0;

void AddGlobalTypeMapping(GlobalType type, const char *name, const char *shortname, SchemaType schemaType, GlobalType parent)
{
	int found;
	assert(parent <= GLOBALTYPE_MAXTYPES);
	assertmsg(type >= 0,"Can't add a negative type!");
	assertmsg(type < GLOBALTYPE_MAXTYPES,"You tried to add too many global type mappings. Increase GLOBALTYPE_MAXTYPES");
	assertmsg(!globalTypeMapping[type].name[0], "There is already a type registered with this number!");

	assertmsgf(strlen(name) >= MIN_GLOBAL_TYPE_NAME_LENGTH, "Global type name %s is too short", 
		name);

	if (stashFindInt(globalNameToType,name,&found))
	{
		if (type >= GLOBALTYPE_LAST && found < GLOBALTYPE_LAST)
		{
			// trying to override a global with a product type, due to out of data files
			return;
		}
		else
		{				
			assertmsg(0,"Duplicate type name entry found in GlobalTypeMapping! Each type name must be unique.");
			return;
		}
	}

	if (stashFindInt(globalShortNameToType,shortname,&found))
	{
		if (type >= GLOBALTYPE_LAST && found < GLOBALTYPE_LAST)
		{
			// trying to override a global with a product type, due to out of data files
			return;
		}
		else
		{				
			assertmsg(0,"Duplicate type name entry found in GlobalTypeMapping! Each type name must be unique.");
			return;
		}
	}

	globalTypeMapping[type].type = type;
	globalTypeMapping[type].schemaType = schemaType;
	globalTypeMapping[type].parent = parent;
	strncpyt(globalTypeMapping[type].name,name,GLOBALTYPE_MAXSCHEMALEN);	
	strncpyt(globalTypeMapping[type].shortname,shortname,GLOBALTYPE_MAXSCHEMALEN);	

	if (parent)
	{
		assertmsg(globalTypeMapping[parent].type,"An entry in GlobalTypeMapping has an invalid parent type!");
	}

	if (name[0])
	{		
		stashAddInt(globalNameToType,globalTypeMapping[type].name,type,0);
	}

	if (shortname[0])
	{
		stashAddInt(globalShortNameToType,globalTypeMapping[type].shortname,type,0);
	}
}

TSMP_DEFINE(ContainerRef);

AUTO_RUN;
void InitializeGlobalPools(void)
{
	TSMP_SMART_CREATE(ContainerRef, 256, TSMP_X64_RECOMMENDED_CHUNK_SIZE);
	ParserSetTPIUsesThreadSafeMemPool(parse_ContainerRef, &TSMP_NAME(ContainerRef));
}

AUTO_RUN;
void RegisterGenericGlobalTypes(void)
{
	static int ran = 0;
	if (!ran)
	{	
		AddGlobalTypeMappingList(genericTypeMappings);
		ran = 1;
	}
}

void AddGlobalTypeMappingList(GlobalTypeMapping *mappingList)
{
	static int init= false;
	int i;
	if (!init)
	{
		init = true;

		globalNameToType = stashTableCreateWithStringKeys(128,StashDefault);
		globalShortNameToType = stashTableCreateWithStringKeys(128,StashDefault);

		for (i = 0; genericTypeMappings[i].name[0];i++)	
		{
			AddGlobalTypeMapping(genericTypeMappings[i].type,genericTypeMappings[i].name,genericTypeMappings[i].shortname,
				genericTypeMappings[i].schemaType,genericTypeMappings[i].parent);
		}
		if (mappingList == genericTypeMappings)
		{		
			return;
		}
	}
	else if (mappingList == genericTypeMappings)
	{
		return; // Already added
	}
	for (i = 0; mappingList[i].name[0];i++)	
	{
		AddGlobalTypeMapping(mappingList[i].type,mappingList[i].name,mappingList[i].shortname,mappingList[i].schemaType,mappingList[i].parent);
	}
}

// now a macro in header file
/*
char *GlobalTypeToName(GlobalType type)
{
	if (type < 0 || type >= GLOBALTYPE_MAXTYPES)
	{
		return globalTypeMapping[GLOBALTYPE_NONE].name;
	}
	return globalTypeMapping[type].name;
}
*/


SchemaType GlobalTypeSchemaType(GlobalType type)
{
	if (type < 0 || type >= GLOBALTYPE_MAXTYPES)
	{
		return false;
	}
	return globalTypeMapping[type].schemaType;
}


GlobalType GlobalTypeParent(GlobalType type)
{
	if (type < 0 || type >= GLOBALTYPE_MAXTYPES)
	{
		return false;
	}
	return globalTypeMapping[type].parent;
}


GlobalType ShortNameToGlobalType(const char *shortname)
{
	int result;

	if (stashFindInt(globalShortNameToType,shortname,&result))
	{
		return result;
	}
	return NameToGlobalType(shortname);
}

GlobalType NameToGlobalType(const char *name)
{
	int result;

	if (stashFindInt(globalNameToType,name,&result))
	{
		return result;
	}
	return 0;
}



void ParseGlobalTypeAndID(const char *string, GlobalType *pOutType, ContainerID *pOutID)
{
	char tempString[1024];
	char *brace = NULL;

	size_t iLen = strlen(string);

	brace = strchr(string, '[');

	if (brace && brace + 3 <= iLen + string)
	{
		memcpy(tempString, string, brace - string);
		tempString[brace - string] = 0;
		*pOutType = NameToGlobalType(tempString);
		*pOutID = atoi(brace + 1);

		if (*pOutType && (*pOutID || (*(brace + 1) == '0' && *(brace + 2) == ']')))
		{
			return;
		}
	}

	*pOutType = 0;
	*pOutID = 0;
}


ContainerRef ParseGlobalTypeAndIDIntoContainerRef(const char *string)
{
	ContainerRef returnRef = {0};
	ParseGlobalTypeAndID(string, &returnRef.containerType, &returnRef.containerID);
	return returnRef;
}





const char *GlobalTypeToCopyDictionaryName(GlobalType type)
{
	// Cache names so they are produced once per dictionary and are otherwise looked up
	static char *apcCopyDictNames[GLOBALTYPE_MAXTYPES];
	const char *pcName;

	assert(type >= 0); // This is necessary to make the compiler happy.

	pcName = apcCopyDictNames[type];
	if (!pcName) {
		char copyDictName[GLOBALTYPE_MAXSCHEMALEN+ARRAY_SIZE(CopyDictionaryPrefix)];
		strcpy(copyDictName, CopyDictionaryPrefix);
		strcat(copyDictName, GlobalTypeToName(type));
		pcName = allocAddString(copyDictName);
		apcCopyDictNames[type] = (char*)pcName;
	}
	return pcName;
}


GlobalType CopyDictionaryNameToGlobalType(const char *name)
{
	if (strlen(name) <= strlen(CopyDictionaryPrefix))
	{
		return GLOBALTYPE_NONE;
	}
	name += strlen(CopyDictionaryPrefix);
	return NameToGlobalType(name);
}

const char *ContainerIDToString_s(ContainerID id, size_t len, char *buf)
{
	snprintf_s(buf, len, "%u", id);
	return buf;
}

ContainerID StringToContainerID(const char *string)
{
	if (!string)
	{
		return 0;
	}
	return atoi(string);
}

#define CONTAINERTYPE_CMD "-ContainerType"

AUTO_COMMAND ACMD_NAME(ContainerType) ACMD_ACCESSLEVEL(0) ACMD_HIDE ACMD_CMDLINE;
void IgnoreContainerType(char *type)
{
	// only here so it won't complain
}

void parseGlobalTypeArgc(int argc, char **argv, GlobalType defaultType)
{
	int		i;
	//Sorry for the super-crude tokenizing, but I don't want to mess up the command line for when it's
	//used later
	{
		for ( i = 1; i < argc; i++)
		{
			if (stricmp(argv[i],CONTAINERTYPE_CMD) == 0 && i + 1 < argc)
			{
				char containerTypeName[256];
				GlobalType eType;

				strcpy(containerTypeName, argv[i+1]);

				eType = NameToGlobalType(containerTypeName);

				assertmsgf(eType != GLOBALTYPE_NONE, "Unknown container type name %s passed to AppServer", containerTypeName);
				SetAppGlobalType(eType);
			}
		}
	}

	if (GetAppGlobalType() == GLOBALTYPE_NONE)
	{
		// default to default type
		SetAppGlobalType(defaultType);
	}

}

void parseGlobalTypeCmdLine(const char *lpCmdLine, GlobalType defaultType)
{
	//Sorry for the super-crude tokenizing, but I don't want to mess up the command line for when it's
	//used later	
	char *pArg;
	char *pSpace;

	if ((pArg = strstr(lpCmdLine, CONTAINERTYPE_CMD)))
	{
		char containerTypeName[256];
		GlobalType eType;

		strcpy(containerTypeName, pArg + strlen(CONTAINERTYPE_CMD) + 1);
		pSpace = strchr(containerTypeName, ' ');

		if (pSpace)
		{
			*pSpace = 0;
		}

		eType = NameToGlobalType(containerTypeName);

		assertmsgf(eType != GLOBALTYPE_NONE, "Unknown container type name %s passed on command line", containerTypeName);
		SetAppGlobalType(eType);
	}
	if (GetAppGlobalType() == GLOBALTYPE_NONE)
	{
		// default to default type
		SetAppGlobalType(defaultType);
	}
}

char sProductName[32] = PRODUCT_NAME_UNSPECIFIED;
char sShortProductName[32] = SHORT_PRODUCT_NAME_UNSPECIFIED;
char sProductDisplayNameKey[64] = PRODUCT_NAME_KEY_UNSPECIFIED;
static char sbProductNameSet = false;

AUTO_COMMAND ACMD_HIDE ACMD_CMDLINE;
void SetProductName(const char *pProductName, const char *pShortProductName)
{
	if (strcmp(sProductName, PRODUCT_NAME_UNSPECIFIED) == 0)
	{
		strcpy(sProductName, pProductName);
		strcpy(sShortProductName, pShortProductName);
		sprintf(sProductDisplayNameKey, "ProductName_%s", pProductName);
		cmdParseHandleProductNameSet(pProductName);
		locProductNameWasJustSet(pProductName);
		sbProductNameSet = true;

	}
	else
	{
		assertmsgf(strcmp(sProductName, pProductName) == 0, "Invalid product name setting, trying to change %s to %s\n",
			sProductName, pProductName);
	}
}

char *GetProductName_IfSet(void)
{
	if (sbProductNameSet)
	{
		return sProductName;
	}

	return NULL;
}


const char *GetProductDisplayName(Language langID)
{
	if (!sProductDisplayNameKey[0])
		sprintf(sProductDisplayNameKey, "ProductName_%s", sProductName);
	return langTranslateMessageKey(langID, sProductDisplayNameKey);
}

char *GetProductDisplayNameKey(void)
{
	return sProductDisplayNameKey;
}

// Get the product logical name, e.g. "FightClub", "StarTrek".
AUTO_EXPR_FUNC(util);
char *GetProductName(void)
{
	return sProductName;
}

// Get the short product logical name, e.g. "FC", "STO".
AUTO_EXPR_FUNC(util) ;
char *GetShortProductName(void)
{
	return sShortProductName;
}

char *GetProjectName(void)
{
	static char projectName[MAX_PATH];
	strcpy(projectName, sShortProductName);
	getFileNameNoExt_s(projectName + 2, ARRAY_SIZE(projectName) - 2, getExecutableName());
	return projectName;
}

char *GetCoreProjectName(void)
{
	static char projectName[MAX_PATH];
	getFileNameNoExt(projectName, getExecutableName());
	return projectName;
}

int sDatabaseVersion = 0;

int GetSchemaVersion(void)
{
	return sDatabaseVersion;
}
void SetSchemaVersion(int val)
{
	sDatabaseVersion = val;
}

bool IsTicketTracker(void)
{
	return GetAppGlobalType() == GLOBALTYPE_TICKETTRACKER;
}

bool IsChatServer(void)
{
	return GetAppGlobalType()==GLOBALTYPE_CHATSERVER || GetAppGlobalType() == GLOBALTYPE_CHATRELAY;
}

bool IsGameServerSpecificallly_NotRelatedTypes(void)
{
	return GetAppGlobalType()==GLOBALTYPE_GAMESERVER;
}

bool IsLoginServer(void)
{
	return GetAppGlobalType()==GLOBALTYPE_LOGINSERVER;
}

bool IsUGCSearchManager(void)
{
	return GetAppGlobalType()==GLOBALTYPE_UGCSEARCHMANAGER;
}

bool IsAuctionServer(void)
{
	return GetAppGlobalType() == GLOBALTYPE_AUCTIONSERVER;
}

bool IsCurrencyExchangeServer(void)
{
    return GetAppGlobalType() == GLOBALTYPE_CURRENCYEXCHANGESERVER;
}

bool IsAccountProxyServer(void)
{
	return GetAppGlobalType() == GLOBALTYPE_ACCOUNTPROXYSERVER;
}

bool IsMapManager(void)
{
	return GetAppGlobalType()==GLOBALTYPE_MAPMANAGER;
}

bool IsGatewayServer(void)
{
	return GetAppGlobalType()==GLOBALTYPE_GATEWAYSERVER;
}

bool IsGatewayLoginLauncher(void)
{
	return GetAppGlobalType()==GLOBALTYPE_GATEWAYLOGINLAUNCHER;
}

bool IsServer(void)
{
	if ((GetAppGlobalType() == GLOBALTYPE_NONE) || (GetAppGlobalType() == GLOBALTYPE_CLIENT))
	{
		return false;
	}
	return true;
}

bool IsClient(void)
{
	return (GetAppGlobalType() == GLOBALTYPE_CLIENT);
}

bool IsGuildServer(void)
{
	return (GetAppGlobalType() == GLOBALTYPE_GUILDSERVER);
}

bool IsTeamServer(void)
{
	return (GetAppGlobalType() == GLOBALTYPE_TEAMSERVER);
}

bool IsQueueServer(void)
{
	return (GetAppGlobalType() == GLOBALTYPE_QUEUESERVER);
}

bool IsResourceDB(void)
{
	return (GetAppGlobalType() == GLOBALTYPE_RESOURCEDB);
}

bool IsGroupProjectServer(void)
{
    return GetAppGlobalType() == GLOBALTYPE_GROUPPROJECTSERVER;
}

AUTO_STRUCT;
typedef struct GlobalTypeMappingList
{
	GlobalTypeMapping **ppMappings;
} GlobalTypeMappingList;

ContainerRef *CreateContainerRef(GlobalType type, ContainerID id)
{
	ContainerRef *ref = StructCreate(parse_ContainerRef);
	ref->containerType = type;
	ref->containerID = id;
	return ref;
}

void DestroyContainerRef(ContainerRef *ref)
{
	StructDestroy(parse_ContainerRef,ref);
}

ContainerRefArray *CreateContainerRefArray(void)
{
	ContainerRefArray *refArray = StructCreate(parse_ContainerRefArray);
	return refArray;
}

void AddToContainerRefArray(ContainerRefArray *array, GlobalType type, ContainerID id)
{
	ContainerRef *ref = CreateContainerRef(type,id);
	eaPush(&array->containerRefs,ref);
}

void DestroyContainerRefArray(ContainerRefArray *array)
{
	StructDestroy(parse_ContainerRefArray,array);
}

//the const-ness of this might need to be fixed up at some point, as it currently mucks with pString but then puts it back
bool DecodeContainerTypeAndIDFromString(const char *pString, GlobalType *peType, ContainerID *piID)
{
	char *pOpenBracket;
	char *pCloseBracket;
	bool bRetVal = false;

	pOpenBracket = strchr(pString, '[');
	pCloseBracket = strchr(pString, ']');

	if (!pOpenBracket || !pCloseBracket || pCloseBracket < pOpenBracket)
	{
		return false;
	}

	*pOpenBracket = 0;
	*pCloseBracket = 0;

	if (sscanf(pOpenBracket + 1, "%d", piID))
	{
		*peType = NameToGlobalType(pString);

		if (*peType != GLOBALTYPE_NONE)
		{
			bRetVal = true;
		}
	}

	*pOpenBracket = '[';
	*pCloseBracket = ']';

	return bRetVal;
}
	
GlobalConfig gConf;

U32 DEFAULT_LATELINK_GetAppGlobalID(void)
{
	return 1;
}

void* DEFAULT_LATELINK_GetAppIDStr(void)
{
	return "1";
}

AUTO_COMMAND ACMD_EARLYCOMMANDLINE ACMD_ACCESSLEVEL(0);
void ServerSaving(int serverSaving)
{
	gConf.bServerSaving = !!serverSaving;
}

AUTO_COMMAND ACMD_EARLYCOMMANDLINE ACMD_ACCESSLEVEL(0);
void NewAnimationSystem(int bNew)
{
	gConf.bNewAnimationSystem = !!bNew;
}

AUTO_COMMAND ACMD_EARLYCOMMANDLINE ACMD_ACCESSLEVEL(0);
void UseMovementGraphs(int bApply)
{
	gConf.bUseMovementGraphs = !!bApply;
}

AUTO_COMMAND ACMD_EARLYCOMMANDLINE ACMD_ACCESSLEVEL(0);
void UseNWOMovementAndAnimationOptions(int bApply)
{
	gConf.bUseNWOMovementAndAnimationOptions = !!bApply;
}

AUTO_COMMAND ACMD_EARLYCOMMANDLINE ACMD_ACCESSLEVEL(0);
void UseSTOMovementAndAnimationOptions(int bApply)
{
	gConf.bUseSTOMovementAndAnimationOptions = !!bApply;
}

void DEFAULT_LATELINK_ProdSpecificGlobalConfigSetup(void)
{
}

AUTO_RUN_POSTINTERNAL;
void SetGlobalConfDefaults(void)
{
    StructInit(parse_GlobalConfig,&gConf);
	gConf.bServerSaving = 1; // On by default now
	gConf.iDXTQuality = 2; // Normal DXT setting

	ProdSpecificGlobalConfigSetup();
}


char *GlobalTypeAndIDToString(GlobalType eType, ContainerID iID)
{
	static char *spRetString = NULL;

	estrPrintf(&spRetString, "%s[%u]", GlobalTypeToName(eType), iID);

	return spRetString;
}

bool GlobalTypeIsCriticallyImportant(GlobalType eType)
{
	switch(eType)
	{
	case GLOBALTYPE_CONTROLLER:
	case GLOBALTYPE_OBJECTDB:
	case GLOBALTYPE_TRANSACTIONSERVER:
		return true;
	}
	return false;
}


bool GlobalServerTypeIsLowImportance(GlobalType eType)
{
	switch (eType)
	{
	case GLOBALTYPE_GAMESERVER:
	case GLOBALTYPE_CHATRELAY:
		return true;
	}
	return false;
}

bool DEFAULT_LATELINK_IsGameServerBasedType(void)
{
	return false;
}

bool DEFAULT_LATELINK_IsAppServerBasedType(void)
{
	return false;
}

U32 GetAppGlobalID_ForCmdParse(void)
{
	if (GetAppGlobalType() == GLOBALTYPE_CONTROLLER)
	{
		return 0;
	}
	
	return GetAppGlobalID();
}

#include "globalTypes_h_ast.c"
#include "globalTypes_c_ast.c"

#include "GlobalTypeEnum_h_ast.c"
