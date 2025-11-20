/***************************************************************************
 
 
 
 *
 ***************************************************************************/
#ifndef GATEWAYWATCHER_H__
#define GATEWAYWATCHER_H__
#pragma once
GCC_SYSTEM

extern int g_bGatewayUseCmdToLaunch;
extern int g_bGatewayRestartSlaveIfDead;

AUTO_ENUM;
typedef enum GatewayWatcherFlags
{
	kGatewayWatcherFlags_WatchConnection      = 1 << 0,
		// If the connection goes down, kill the process.
	kGatewayWatcherFlags_AddLoginServers      = 1 << 1,
		// Add login server IPs to the command line.
	kGatewayWatcherFlags_AddShardName         = 1 << 2,
		// Add the name of the shard to the command line.
	kGatewayWatcherFlags_RestartProcessIfLost = 1 << 3,
		// If we lose contact with the watched process, this process will exit(-1) unless
		//   this flag is set. When this flag is set, we attempt to restart it.
		// This flag is automatically set when the server is started with -GatewayRestartSlaveIfDead
	kGatewayWatcherFlags_UseShellToLaunch     = 1 << 4,
		// Normally, nodejs is directly executed. This makes it difficult to
		//   debug, so this options runs nodejs inside a persistent shell.
		// This flag is automatically set when the server is started with -GatewayUseCmdToLaunch
} GatewayWatcherFlags;

typedef void LinkCallback(NetLink* link,void *user_data);
typedef void PacketCallback(Packet *pkt,int cmd,NetLink* link,void *user_data);

typedef struct GatewayWatcher GatewayWatcher;

GatewayWatcher *gateway_CreateAndStartWatcher(char *pchName,
	char *pchScript, char *pchAddToCommandLine, int eGatewayWatcherFlags,
	const char * pchOptionNameForIPAndPort, int portStart, int portEnd,
	PacketCallback packet_cb, LinkCallback connect_cb, LinkCallback disconnect_cb,
	int user_data_size);
	// Creates a Gateway process watcher. The caller is responsible for all the
	//   actual connection handling, especially notifying the GatewayWatcher when the
	//   connection is connected or disconnected.
	// Actually starts the process, watches it, etc.

extern void gateway_DestroyWatcher(GatewayWatcher *pWatcher);
	// There is probably no need to ever destroy a GatewayWatcher.
	// Doing so kills the process being watched.

extern void gateway_WatcherConnected(GatewayWatcher *pWatcher);
	// Should be called by the caller's connection handler to notify the
	//   GatewayWatcher we've connected to the process.

extern void gateway_WatcherDisconnected(GatewayWatcher *pWatcher);
	// Should be called by the caller's disconnect handler to notify the
	//   GatewayWatcher that we've been disconnected. This will likely cause
	//   the process to be killed and restarted.

extern void gateway_SetShutdownMessage(GatewayWatcher *pWatcher, const char *pch);
	// Allows the caller to poke a message into the next shutdown message
	//   that is sent to the controller. (Basically, used as a way to get the
	//   call stack to the monitor.)

#endif /* #ifndef GATEWATCHER_H__ */

/* End of File */
