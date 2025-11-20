// TLS debugging commands

#include <stdio.h>

#include "cmdparse.h"
#include "error.h"
#include "net.h"
#include "tls.h"
#include "tlsCommands.h"
#include "ThreadManager.h"
#include "wininclude.h"

// TLS echo server information
struct EchoServer
{
	unsigned short uPort;
	TlsCertificate *pCertificate;
};

// TLS echo link information
struct EchoClient
{
	unsigned long uId;
	TlsSession *pSession;
	NetLink *pLink;
};

// One-by-one increasing client IDs.
static unsigned long uClientId = 0;

// Get the next client ID.
static unsigned long GetNextClientId()
{
	return (unsigned long)InterlockedIncrement((long *)&uClientId);
}

// Set or get the server information for this thread.
// To get without getting, pass a null pointer.
static struct EchoServer *ThreadEchoServer(struct EchoServer *pServer)
{
	struct EchoServer **server = NULL;
	STATIC_THREAD_ALLOC_TYPE(server, struct EchoServer **);
	if (pServer)
		*server = pServer;
	devassert(*server);
	return *server;
};

// Some plaintext was decrypted; send echo.
void TlsEchoServer_TlsPlainDataReceived(TlsSession *pSession, void *pUserData, const char *pData, size_t uLength)
{
	struct EchoClient *client = pUserData;

	// Send echo to client.
	tlsSessionSendPlaintext(pSession, pData, uLength);
}

// Encrypted data needs to be sent.
void TlsEchoServer_TlsCipherDataSend(TlsSession *pSession, void *pUserData, const char *pData, size_t uLength)
{
	struct EchoClient *client = pUserData;
	Packet *packet = pktCreateRaw(client->pLink);
	pktSendBytesRaw(packet, pData, (int)uLength);
	pktSendRaw(&packet);
}

// A TLS session has been created.
void TlsEchoServer_TlsOpened(TlsSession *pSession, void *pUserData)
{
	struct EchoClient *client = pUserData;
	verbose_printf("TLS Echo Server: Client %lu has begin a TLS session\n", client->uId);
}

// The TLS session has ended.
void TlsEchoServer_TlsClosed(TlsSession *pSession, void *pUserData)
{
	struct EchoClient *client = pUserData;
	verbose_printf("TLS Echo Server: Client %lu has concluded its TLS session\n", client->uId);
}

// A client has sent TLS encrypted data to the echo server.
static void TlsEchoServer_ServingMsg(Packet *pkt, int cmd, NetLink *link, void *state)
{
	struct EchoClient *client = state;
	char *data = pktGetStringRaw(pkt);
	U32 len = pktGetSize(pkt);
	tlsSessionReceivedCiphertext(client->pSession, data, len);
}

// A client has connected to the echo server.
static int TlsEchoServer_ServingConnect(NetLink *link, void *state)
{
	struct EchoServer *server = ThreadEchoServer(NULL);
	struct EchoClient *client = state;
	
	// Announce client.
	client->uId = GetNextClientId();
	verbose_printf("TLS Echo Server: Client %lu connected on %u\n", client->uId, server->uPort);
	client->pLink = link;

	// Start negotiation.
	client->pSession = tlsSessionStartServer(state, server->pCertificate, TlsEchoServer_TlsPlainDataReceived, TlsEchoServer_TlsCipherDataSend,
		TlsEchoServer_TlsOpened, TlsEchoServer_TlsClosed);
	if (!client->pSession)
	{
		Errorf("Unable to start negotiation with client");
		linkFlushAndClose(&link, "Unable to start negotiation with client");
		return 0;
	}
	return 1;
}

// A client has disconnected from the echo server.
static int TlsEchoServer_ServingDisconnect(NetLink *link, void *state)
{
	struct EchoClient *client = state;
	verbose_printf("TLS Echo Server: Client %lu disconnected\n", client->uId);
	return 1;
}

// Run the echo server.
static DWORD WINAPI TlsEchoServer(LPVOID lpParam)
{
	struct EchoServer *server = lpParam;
	NetComm *comm = NULL;
	NetListen *netListen;

	// Set up the per-thread server information.
	devassert(server);
	ThreadEchoServer(server);

	// Create comm.
	comm = commCreate(0, 0);
	if (!comm)
	{
		Errorf("commCreate() failed");
		return 0;
	}

	// Listen for incoming connections.
	netListen = commListen(comm, LINKTYPE_UNSPEC, LINK_RAW, server->uPort, TlsEchoServer_ServingMsg, TlsEchoServer_ServingConnect,
		TlsEchoServer_ServingDisconnect, sizeof(struct EchoClient));
	if (!netListen)
	{
		Errorf("commListen() failed");
		commDestroy(&comm);
		return 0;
	}

	// Process network activity.
	verbose_printf("TLS Echo Server: Waiting for connections on %u\n", server->uPort);
	for(;;)
		commMonitor(comm);

	// Never get here
	devassert(0);
	return 0;
}

// Auto command for setting the debug level
int tlsDebugLevelValue = 0;
AUTO_CMD_INT(tlsDebugLevelValue, setTlsDebugLevel) ACMD_CALLBACK(MyCallback);
void MyCallback(CMDARGS)
{
	printf("TLS debug level set to: %d\n", tlsDebugLevelValue);
}

// Get the debug level.
int tlsDebugLevel()
{
	return MAX(errorGetVerboseLevel(), tlsDebugLevelValue);
}

// Start a test TLS echo server.
AUTO_COMMAND;
void TlsStartEchoServer(char *pCertificate, int iPort)
{
	ManagedThread *mtServer = NULL;
	struct EchoServer *server;
	if (iPort <= 0 || iPort > 65535)
	{
		Errorf("This is not a valid port number.");
		return;
	}
	server = server = malloc(sizeof(struct EchoServer));
	server->uPort = iPort;
	server->pCertificate = tlsLoadCertificate(pCertificate);
	if (!server->pCertificate)
	{
		Errorf("Unable to load certificate");
		free(server);
		return;
	}
	mtServer = tmCreateThread(TlsEchoServer, server);
}
