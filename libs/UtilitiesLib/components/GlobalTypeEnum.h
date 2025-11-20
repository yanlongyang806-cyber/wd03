#pragma once
GCC_SYSTEM

typedef U32 ContainerID;

// Data type used for (optional) first parameter for ACMD_GENERICSERVERCMDs
// that is populated by the server that is handling the command, populated by setting the 
// CmdContext.data field for the command parse
typedef void* GenericServerCmdData;  

#define GLOBALTYPE_MAXTYPES 150 //the max number of total supported types. Up this as necessary

// Add new enums to the end of this list, because some logs use these values as literal ints. :(
AUTO_ENUM;
typedef enum GlobalType
{
	GLOBALTYPE_NONE,
	GLOBALTYPE_OBJECTDB,
	GLOBALTYPE_CLONEOBJECTDB,
	GLOBALTYPE_TRANSACTIONSERVER,
	GLOBALTYPE_LAUNCHER,
	GLOBALTYPE_LOGSERVER,
	GLOBALTYPE_CLIENT,
	GLOBALTYPE_CONTROLLER,
	GLOBALTYPE_APPSERVER,
	GLOBALTYPE_CONTROLLERTRACKER,
	GLOBALTYPE_MAPMANAGER,
	GLOBALTYPE_MULTIPLEXER,
	GLOBALTYPE_MASTERCONTROLPROGRAM,
	GLOBALTYPE_GAMESERVER,
	GLOBALTYPE_TEAMSERVER,
	GLOBALTYPE_GUILDSERVER,
	GLOBALTYPE_CHATSERVER,
	GLOBALTYPE_LOGINSERVER,
	GLOBALTYPE_ENTITY,
	GLOBALTYPE_ENTITYPLAYER,
	GLOBALTYPE_ENTITYCRITTER,
	GLOBALTYPE_ENTITYSAVEDPET,
	GLOBALTYPE_ENTITYPUPPET,
	GLOBALTYPE_TEAM,
	GLOBALTYPE_GUILD,
	GLOBALTYPE_TESTGAMESERVER,
	GLOBALTYPE_ERRORTRACKER,
	GLOBALTYPE_CLIENTCONTROLLER, 
	GLOBALTYPE_LOGPARSER,
	GLOBALTYPE_ARBITRARYPROCESS,					// used by controller scripting to launch and monitor not-part-of-shard tasks
	GLOBALTYPE_TICKETTRACKER,
	GLOBALTYPE_ERRORTRACKERENTRY,
	GLOBALTYPE_ERRORTRACKERENTRY_LAST,
	GLOBALTYPE_TICKETENTRY,
	GLOBALTYPE_ACCOUNTSERVER,
	GLOBALTYPE_ACCOUNT,
	GLOBALTYPE_TICKETUSER,
	GLOBALTYPE_TICKETGROUP,
	GLOBALTYPE_PRODUCTKEY_USED,
	GLOBALTYPE_PRODUCTKEY_NEW,
	GLOBALTYPE_CHATCHANNEL,
	GLOBALTYPE_CHATUSER,
	GLOBALTYPE_HEADSHOTSERVER,
	GLOBALTYPE_MACHINE,								// used internally by controller for record keeping in the alert system
	GLOBALTYPE_WEBREQUESTSERVER,					// another name for a specific gameserver in production shards that responds to queries from 
													// the character webpage stuff, etc.
	GLOBALTYPE_CONTINUOUSBUILDER,
	GLOBALTYPE_PATCHSERVER,
	GLOBALTYPE_AUCTIONSERVER,
	GLOBALTYPE_AUCTIONLOT,
	GLOBALTYPE_QUEUESERVER,
	GLOBALTYPE_UGCSEARCHMANAGER, 
	GLOBALTYPE_PREFSTORE,
	GLOBALTYPE_TESTCLIENTSERVER,					// no longer in use
	GLOBALTYPE_ACCOUNTPROXYSERVER,
	GLOBALTYPE_ACCOUNTPROXYSERVER_LOCKS,			// Used in the account proxy and game servers to change values
	GLOBALTYPE_ACCOUNTSERVER_LOCKS,					// Used in the account server (not proxy)
	GLOBALTYPE_ACCOUNTSERVER_SUBSCRIPTION,			// Used in the account server to store subscriptions
	GLOBALTYPE_ACCOUNTSERVER_INTERNAL_SUBSCRIPTION,	// Used in the account server to store internal subscriptions
	GLOBALTYPE_ACCOUNTSERVER_PRODUCT,				// Used in the account server to store products
	GLOBALTYPE_QUEUEINFO,
	GLOBALTYPE_ENTITYDESCRIPTOR,					// Used in Ticket Tracker for storing ParseTables for old Entity structs
	GLOBALTYPE_GIMMEDLL,
	GLOBALTYPE_ACCOUNTSERVER_GLOBALDATA,			// Persisted global data for the account server (stats, etc.)
	GLOBALTYPE_GATEWAYLOGINLAUNCHER,							
	GLOBALTYPE_CLUSTERCONTROLLER,							
	GLOBALTYPE_UGCDATAMANAGER,							
	GLOBALTYPE_DEPRECATED_66,							
	GLOBALTYPE_GAMEACCOUNTDATA,						// Game specific, shard-wide data
	GLOBALTYPE_ACCOUNTSERVER_PURCHASE_LOG,			// Used in the account server to log purchases
	GLOBALTYPE_DIARYSERVER,							// ID for the diary app server - no longer used
	GLOBALTYPE_PLAYERDIARY,							// container type for a player's diary
	GLOBALTYPE_DIARYENTRYBUCKET,					// container type for holding multiple diary entries
	GLOBALTYPE_OBJECTDB_MERGER,
	GLOBALTYPE_GLOBALCHATSERVER,					// server type for Global Chat
	GLOBALTYPE_SHARDVARIABLE,
	GLOBALTYPE_RESOURCEDB, 
	GLOBALTYPE_TESTSERVER,							// for the Test Server (NOT the same as Test Game Server or Test Client Server)
	GLOBALTYPE_ACCOUNTSERVER_KEYGROUP,				// ProductKeyGroup
	GLOBALTYPE_ACCOUNTSERVER_KEYBATCH,				// ProductKeyBatch
	GLOBALTYPE_XBOXPATCHER,
	GLOBALTYPE_CRYPTICLAUNCHER,
	GLOBALTYPE_ACCOUNTSERVER_DISCOUNT,
	GLOBALTYPE_TESTING,								// For objTesting.c
	GLOBALTYPE_JOBMANAGER,
	GLOBALTYPE_TESTCLIENT,
	GLOBALTYPE_CLIENTBINNER,
	GLOBALTYPE_SERVERBINNER,
	GLOBALTYPE_BCNMASTERSERVER,						// Beaconizer server types
	GLOBALTYPE_BCNSUBSERVER,
	GLOBALTYPE_BCNCLIENTSENTRY,
	GLOBALTYPE_UGCPROJECT,
	GLOBALTYPE_LEADERBOARDSERVER,
	GLOBALTYPE_LEADERBOARD,
	GLOBALTYPE_LEADERBOARDDATA,
	GLOBALTYPE_VIRTUALSHARD,
	GLOBALTYPE_CRYPTICMON,
	GLOBALTYPE_PERSISTEDSTORE,
	GLOBALTYPE_PATCHSERVER_PATCHDB,
	GLOBALTYPE_PATCHSERVER_DIRENTRY,
	GLOBALTYPE_PATCHSERVER_CHECKIN,
	GLOBALTYPE_PATCHSERVER_NAMEDVIEW,
	GLOBALTYPE_ACCOUNTSERVER_KEYVALUECHAIN,
	GLOBALTYPE_ACCOUNTSERVER_LOG_BATCH,
	GLOBALTYPE_ACCOUNTSTUB,							// Holds a list of offline characters
	GLOBALTYPE_CHATRELAY,
	GLOBALTYPE_UGCPROJECTSERIES,
	GLOBALTYPE_ENTITYSHAREDBANK,					// This is an entity that used to hold shared storage for an account 

	GLOBALTYPE_ALL,									// used internally for AUTO_SETTING system, also used in mcp-to-controller
													// communication a few places

	GLOBALTYPE_CURRENCYEXCHANGE,					// Holds per account currency exchange data including open orders
	GLOBALTYPE_ENTITYPROJECTILE,
	GLOBALTYPE_PROCEDURAL,							// Log Parser procedural logs
	GLOBALTYPE_ENTITYGUILDBANK,
	GLOBALTYPE_CBMONITOR,
	GLOBALTYPE_ACCOUNTSERVER_PWCOMMONACCOUNT,
	GLOBALTYPE_LOGINHAMMER,
	GLOBALTYPE_NOTESSERVER,
	GLOBALTYPE_CLONEOFCLONE,
	GLOBALTYPE_EVENTCONTAINER,
	GLOBALTYPE_TESTSUITE,
	GLOBALTYPE_GETVRML,
	GLOBALTYPE_GATEWAYSERVER,						// A specific kind of gameserver that handles game requests from the web.
    GLOBALTYPE_GROUPPROJECTCONTAINER,
    GLOBALTYPE_GROUPPROJECTCONTAINERGUILD,
    GLOBALTYPE_GROUPPROJECTSERVER,
	GLOBALTYPE_ACCOUNTSERVER_TRANSACTIONLOG,
	GLOBALTYPE_TEXTURESERVER,
	GLOBALTYPE_UGCACCOUNT,							// A dictionary, similar to GameAccountData, but for UGC data like subscriptions 
	GLOBALTYPE_ACCOUNTSERVER_VIRTUALCURRENCY,
    GLOBALTYPE_GROUPPROJECTCONTAINERPLAYER,
	GLOBALTYPE_CHATSHARD,
	GLOBALTYPE_CHATCLUSTER,
	GLOBALTYPE_DEEPSPACESERVER,
	GLOBALTYPE_AUCTIONPRICEHISTORYCONTAINER,
    GLOBALTYPE_CURRENCYEXCHANGESERVER,
	GLOBALTYPE_SHARDMERGE,
	GLOBALTYPE_MACHINESTATUS,
	GLOBALTYPE_OVERLORD,
	GLOBALTYPE_GAMELOGREPORTER,
	GLOBALTYPE_FARADAY,
	GLOBALTYPE_REVISIONFINDER,
	GLOBALTYPE_GATEWAYGAMEDATA,
	// Add new global types immediately before this line.

	GLOBALTYPE_LAST, GLOBALTYPE_MAX, EIGNORE		// Leave this last, not a valid flag, just for the compile time assert below
} GlobalType;

//When adding a new global type, we need to push a new DBScript or reporting may break.
//When making changes to this file those changes should be merged to all current branches in order to keep the enums in sync.
STATIC_ASSERT_MESSAGE(GLOBALTYPE_LAST - 1 == GLOBALTYPE_GATEWAYGAMEDATA, "Whenever you add a new global type, you will hit this assert. That's OK, just change this assert and then increment the literal int in the assert in the next line. If a non-programmer ever sees this, some programmer did a VERY VERY VERY bad job of testing code before checking it in");
STATIC_ASSERT_MESSAGE(GLOBALTYPE_LAST - 1 == 140, "If you see this assert it's because you tried to add a global type to the middle of the list, or perhaps remove one. That is BAD BAD BAD BAD. Do not do it. Add all new types to the END of the list, and instead of removing unneeded ones, rename them into obsolescence.");


//R.I.P. InventorySlotType 3/27/09 - 12/16/11
