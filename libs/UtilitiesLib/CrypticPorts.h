#pragma once

/***************************************************************************



***************************************************************************/


//all ports used for cryptic products are listed in this file, just to keep them all in one place
// public ports are 7000+  7000- are private ports.
// web ports are 8080+

/************************************************************************/
/* Standard: Low-valued port numbers for standard Internet services     */
/************************************************************************/

#define XMPP_DEFAULT_PORT								5222	// XMPP (RFC 3920)
#define XMPP_LEGACY_TLS_PORT							5223	// Legacy TLS/SSL tunneled XMPP port (nonstandard)

/************************************************************************/
/* Private ports: Only accessible from our local network                */
/************************************************************************/

// This is the lowest port that will be routed on the internal datacenter network.
// WARNING: If you change this, you must coordinate with Network Operations, since this value
// is baked into the firewall rules.
#define PRIVATE_PORT_START								6700

// Define new private ports below this line, counting downward.

// ******** ADD PRIVATE PORTS HERE ********

#define GATEWAYSERVER_PORT_START						6830	// Start of range for GatewayServer to listen for Proxy connections 
#define GATEWAYSERVER_PORT_END							6839	// End of range for GatewayServer to listen for Proxy connections

// unused 6840 - 2848. Feel free to use these

#define DEEPSPACE_SERVER_PORT							6849	// DeepSpace server-to-server port
#define GATEWAYLOGIN_CLIENT_PORT_START					6850	// Start of range that the GL listens for client connections on.
#define GATEWAYLOGIN_CLIENT_PORT_END					6859	// End (inclusive) of range that the GL listens for client connections on.
#define GATEWAYPROXY_CLIENT_PORT_START					6860	// Start of range that the GP listens for client connections on.
#define GATEWAYPROXY_CLIENT_PORT_END					6891	// End (inclusive) of range that the GP listens for client connections on.

// unused 6892 - 6896. Feel free to use these

#define OVERLORD_SIMPLE_STATUS_PORT						6896    // overlord does normal simple status listening on this port

#define OVERLORD_LISTEN_FOR_SIMPLE_STATUS_FORWARDING	6897	// overlord gets simple statuses forwarded to it, presumably
	//by machineStatus.exe

#define MACHINESTATUS_LOCAL_STATUS_PORT					6898    // MachineStatus.exe does generic status reporting on this port
#define DEFAULT_GATEWAYLOGINLAUNCHER_PORT				6899	// GatewayLoginLauncher talks to GatewayLogin on this port
//#define DEFAULT_GATEWAYSERVER_PORT					6900	// Deprecated, now a range. See GATEWAYSERVER_PORT_START/END
#define DEFAULT_ACCOUNTSERVER_TRUSTED_PORT				6901	// A trusted port to the accountserver
#define SXFER_PRIVATE_PORT								6902	// sxfer port for private networks
#define MAGIC_CAPTURE_CONNECT_PORT						6903	// Request that the peer capture this session.
#define DEFAULT_MULTIPLEXER_PORT_EXTRA_FIRST			6904	// SM: adding overflow ranges for launcher and multiplexer ports based on SharedMachineIndex,
#define DEFAULT_MULTIPLEXER_PORT_EXTRA_LAST				6934	// SM: but setting it up so that SharedMachineIndex==0 uses the old ports, so forward/backwards compatibility works
#define LAUNCHER_LISTEN_PORT_EXTRA_FIRST				6935	// SM: as well as possible
#define LAUNCHER_LISTEN_PORT_EXTRA_LAST					6965	// SM:
#define DEFAULT_ACCOUNTSERVER_GLOBALCHATSERVER_PORT		6966
#define SENTRYMONITOR_PORT								6967
#define CRYPTIC_PROFILER_PORT							6968
#define XBOX_FILESERVER_PORT							6969
#define CONTROLLERTRACKER_CRITICAL_SYSTEM_INFO_PORT		6970	// new controller tracker monitors critical systems 
#define CONTROLLERTRACKER_SHARD_INFO_PORT				6971	// new controller tracker gets info from controllers about themselves
#define LOGSERVER_LOGPARSER_PORT						6972	// port the logserver listens on for logparsers
#define DEFAULT_MCP_PRINTF_PORT							6973	// MCP receives printfs from GetTex on this port
#define DEFAULT_OBJECTDB_REPLICATE_PORT					6974	// Port that the clone objectDb listens on
#define DEFAULT_CONTROLLER_PORT							6975	// Controller listens for servers
#define CONTROLLERTRACKER_SLAVED_CONTROLLERTRACKER_PORT 6976	// master CT listens for slave CTs
#define DEFAULT_MULTIPLEXER_PORT_MAIN					6977	// multiplexers listen for servers
#define DEFAULT_TRANSACTIONSERVER_PORT					6978	// transaction server listens for LTMs
#define SHARDLAUNCHER_PATCHSTATUS_PORT					6979    // ShardLAuncher.exe listens for pcl status updates
#define DEFAULT_ERRORTRACKER_PORT						6980	// Listens for crash/error/assert output
#define DEFAULT_SYMSRV_PORT								6981
#define INTERSHARD_COMM_PORT							6982	// two controllers can talk to each other on this port and send commands back and forth (used for UGC project transfer)
#define CLUSTERCONTROLLER_PATCHSTATUS_PORT				6983    //cluster controller listens for PCL status updates
#define CLUSTERCONTROLLER_PORT							6984	// ClusterController listens for information from controllers
#define DEFAULT_OBJECTDB_TRANSFER_PORT					6985	// Intershard communication between ObjectDBs
#define DEFAULT_SHARDCLUSTER_PORT_TRANSSERVER			6986    //transaction servers in a shard talk to each other on this port
#define DEFAULT_SHARDCLUSTER_PORT_CONTROLLERS			6987    //controllers talk to each other in a shardCluster on this port
#define DEFAULT_LOGINSERVER_GATEWAY_LOGIN_PORT			6988	// loginServer listens for GatewayProxies
#define DEFAULT_LOGSERVER_PORT							6989	// Port the logserver listens on
#define DEFAULT_MULTIPLEXED_MAKEBINS_PORT				6990	//GS and GC launch slaves to do makebins in parallel, communicate on this port
#define DEFAULT_NOTESSERVER_PORT						6991
#define DEFAULT_CBMONITOR_PORT							6992	// CB Monitor listens for CBs
#define DEFAULT_CONTROLLER_PATCHSTATUS_PORT				6993    // controller listens on this port to get status from patchclient.exe
#define DEFAULT_MACHINESTATUS_PATCHSTATUS_PORT			6994    // MachineStatus.exe listens on this port for local patchclients
#define DEFAULT_ACCOUNTPROXYSERVER_PORT					6995	// For the in-shard AccountProxy app servers, internal
#define DEFAULT_MACHINESTATUS_PORT						6996	// MachineStatus.exe listens on this port for local launchers
#define DEFAULT_ERRORTRACKER_SECURE_PORT				6997	// For XLSP
#define CONTINUOUS_BUILDER_PORT							6998
#define LAUNCHER_LISTEN_PORT_MAIN						6999	// launcher listens for crash messages from crypticerror, and for some debug stuff from 
																// xbox clients and so forth


/************************************************************************/
/* Public ports: Customers and the outside world can access these       */
/************************************************************************/

#define PUBLIC_PORT_START								7000
#define DEFAULT_LOGINSERVER_PORT						7001
//#define DEFAULT_ACCOUNTSERVER_PORT					7002	// Defunct and unused
#define STARTING_GAMESERVER_PORT						7003	// (7003 - 7395) Game servers
#define DEFAULT_ACCOUNTSERVER_UNENCRYPTED_PORT			7004	// Account Server (Overlaps with game server range!)
#define DEFAULT_GLOBAL_CHATSERVER_PORT					7203	// Global Chat Server (Overlaps with game server range!)
#define DEFAULT_ERRORTRACKER_PUBLIC_PORT				7224	// Error Tracker (Overlaps with game server range!)
#define DEFAULT_TICKET_TRACKER_PORT						7225	// Ticket Tracker (Overlaps with game server range!)
#define DEFAULT_PATCHSERVER_PORT						7255	// Patch Server (Overlaps with game server range!)
//#define CCG_SERVER_PORT								7277	// Deprecated
//#define DIARY_SERVER_XMLRPC_PORT						7278	// Deprecated
#define MAX_GAMESERVER_PORT								7395
#define NEWCONTROLLERTRACKER_GENERAL_MCP_PORT			7399	// port the new controller tracker does general MCP communication on
#define DEFAULT_TESTSERVER_PORT							7401	// TestServer port for listening to test apps, incl. Test Clients
#define STARTING_CHATRELAY_PORT                         7402
#define MAX_CHATRELAY_PORT                              7421
#define FIRST_EXTRA_LOGINSERVER_PORT					7422	//7 extra ports, plus DEFAULT_LOGINSERVER_PORT... max 8 loginservers per machine
#define MAX_EXTRA_LOGINSERVER_PORT						7428
#define CLIENT_SIMPLE_COMMAND_PORT						7451
#define SXFER_PUBLIC_PORT								7453	// sxfer port for public networks
#define DEEPSPACE_PORT									7454	// Deep Space Network communication
#define DEEPSPACE_PUBLIC_SERVER_PORT					7455	// Public DeepSpace server-to-server port

// ******** ADD PUBLIC PORTS HERE ********

// Define new public ports above this line, counting upward.

#define PUBLIC_PORT_END									7500

// Top-level server web interface ports.
#define DEFAULT_WEBMONITOR_ALTERNATE_SERVER				8080	// Generic alternate web port
#define DEFAULT_WEBMONITOR_ACCOUNT_SERVER				8081	// Account Server server monitor (Conflicts with Global Log Server!)
#define DEFAULT_WEBMONITOR_GLOBAL_LOG_SERVER			8081	// Global Log Server server monitor (Conflicts with Account Server!)
#define DEFAULT_WEBMONITOR_GLOBAL_CHAT_SERVER			8083	// Global Chat Server server monitor
#define DEFAULT_WEBMONITOR_GLOBAL_LOG_PARSER			8084	// Global Log Parser server monitor
#define DEFAULT_WEBMONITOR_PATCHSERVER					8085	// PatchServer server monitor
#define CLUSTERCONTROLLER_HTTP_PORT						8086
#define DEFAULT_WEBMONITOR_FARADAY						8087
#define OVERLORD_HTTP_PORT								8088
#define DEFAULT_WEBMONITOR_XMPPSERVER					8089	// XmppServer server monitor
#define DEFAULT_WEBINTERFACE_ACCOUNT_SERVER				8090	// MCP default for Account Server web interface (Note: On Live, port 80 is used.)
#define DEFAULT_CBMONITOR_HTML_PORT						8091	// CBmonitor servermonitoring
#define DEFAULT_NOTESSERVER_HTML_PORT					8092	// NotesServer servermonitoring
#define DEFAULT_WEBMONITOR_DEEPSPACE					8093
#define DEFAULT_WEBMONITOR_REVISION_FINDER				8094
// Unused												8095-8098
#define LIVELOGPARSER_LISTEN_PORT						8099	// Communication between Log Parser and standalones; not actually a web port.
#define STARTING_LOGPARSER_PORT							8100	// (8100-8200) Stand-alone Log Parsers
#define MAX_LOGPARSER_PORT								8200
#define MACHINESTATUS_HTTP_PORT							8888	//specifically easy to remember/type

// Define new web ports above this line, counting upward.


/************************************************************************/
/* Legacy ports                                                         */
/************************************************************************/

// Legacy non-production internal ports
// Please do not add new ports in this range.  Use 7000- instead.
#define SENTRYSERVER_PORT								9001	// Sentry client-server
#define SENTRYSERVERMONITOR_PORT						9002	// SentryServer monitor and control
#define SHADERSERVER_PORT								9101	// shader/task server ports
#define MIN_TASKSERVER_PORT								9103	// use giTaskServerPort, which will be set within this range.
#define MAX_TASKSERVER_PORT								9120

/************************************************************************/
/* Testing ports                                                        */
/************************************************************************/

#define MIN_TESTCLIENT_PORT								10000
#define MAX_TESTCLIENT_PORT								65535



/************************************************************************/
/* macros for getting multiplexer/launcher ports */
/************************************************************************/

extern int UtilitiesLib_GetSharedMachineIndex(void);

static __forceinline int GetLauncherListenPort(void)
{
	int iIndex = UtilitiesLib_GetSharedMachineIndex();
	if (iIndex == 0)
	{
		return LAUNCHER_LISTEN_PORT_MAIN;
	}

	return LAUNCHER_LISTEN_PORT_EXTRA_FIRST + iIndex - 1;
}

static __forceinline int GetMultiplexerListenPort(void)
{
	int iIndex = UtilitiesLib_GetSharedMachineIndex();
	if (iIndex == 0)
	{
		return DEFAULT_MULTIPLEXER_PORT_MAIN;
	}

	return DEFAULT_MULTIPLEXER_PORT_EXTRA_FIRST + iIndex - 1;
}

#define MAX_SHAREDMACHINEINDEX (LAUNCHER_LISTEN_PORT_EXTRA_LAST - LAUNCHER_LISTEN_PORT_EXTRA_FIRST + 2)
STATIC_ASSERT(LAUNCHER_LISTEN_PORT_EXTRA_LAST - LAUNCHER_LISTEN_PORT_EXTRA_FIRST == DEFAULT_MULTIPLEXER_PORT_EXTRA_LAST - DEFAULT_MULTIPLEXER_PORT_EXTRA_FIRST);
//+2 because the range is inclusive, plus there's LAUNCHER_LISTEN_PORT_MAIN)
