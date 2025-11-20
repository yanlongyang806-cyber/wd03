// Manage XMPP client network connections.

#include "CrypticPorts.h"
#include "estring.h"
#include "error.h"
#include "file.h"
#include "logging.h"
#include "net.h"
#include "netipfilter.h"
#include "rand.h"
#include "sock.h"
#include "StringUtil.h"
#include "timing.h"
#include "tls.h"
#include "XMPP_Gateway.h"
#include "XMPP_Net.h"
#include "XMPP_Net_c_ast.h"
#include "XMPP_Parsing.h"

// Self-discovered domain name to use for XMPP.
static char *gJIDDomain = NULL;

// Domain name forced by a command-line parameter override.
char gJIDDomainForced[512] = "";
AUTO_CMD_STRING(gJIDDomainForced, setXmppDomain) ACMD_CMDLINE;

// Link status
AUTO_STRUCT;
typedef struct XmppClientLink {
	XmppClientState *state;				NO_AST			// Client state
	NetLink *link;						NO_AST			// Client network link
	TlsSession *tlsSession;				NO_AST			// TLS state, if the link is encrypted
	XMPP_Parser *xp;					AST(LATEBIND)	// XMPP parser for this link
	char *pOutputBuffer;				NO_AST			// Buffer for unsent data
	U64 id;												// Unique client ID
	bool bRedact;										// If true, redact traffic on this link from the logs.
	bool bRedactPending;								// If true, redaction has not yet been performed.
	char *pRedactString;				NO_AST			// If found, just redact this string instead of everything.
	bool bClosed;						NO_AST			// xmpp_Disconnect was called
	char *pClosedReason; AST(ESTRING)
} XmppClientLink;

// Outgoing traffic log line that wasn't sent because parsing was in progress
AUTO_STRUCT;
typedef struct XmppDeferredLog {
	U64 uLinkId;										// Link ID that this data was sent to
	char *string;						AST(ESTRING)	// Data that was sent
} XmppDeferredLog;

// If an XMPP certificate name has been provided, XMPP support has been requested.
static char xmppCertificateName[256] = "";
AUTO_CMD_STRING(xmppCertificateName, setXmppCertificate) ACMD_CMDLINE;

// If a certificate has been loaded, XMPP support is available.
static TlsCertificate *xmppCertificate = NULL;

// XMPP comm
static NetComm *spXmppComm = NULL;

// True if TLS is optional on all connections.
static int siXmppTlsOptional = 0;
AUTO_CMD_INT(siXmppTlsOptional, XmppTlsOptional);

// If true, print all data sent to and received from XMPP clients.
static int siXmppTraceClients = 0;
AUTO_CMD_INT(siXmppTraceClients, XmppTraceClients);

// If true, disable XMPP_TRAFFIC logs.
static bool sbDisableXmppTrafficLogging = false;
AUTO_CMD_INT(sbDisableXmppTrafficLogging, DisableXmppTrafficLogging);

// Total number of connected clients.
static unsigned long siConnections = 0;

// Total bytes sent.
static U64 siBytesSent = 0;

// Total bytes received.
static U64 siBytesReceived = 0;

// Set to true if XMPP is currently being parsed.
// This is used to decide if logging should be deferred until the end of the parse.
static bool sbInParse = false;

// Outgoing traffic log lines that weren't sent because parsing was in progress
XmppDeferredLog **ppDeferredLogs = NULL;

static int xmpp_StartTlsNegotiation(NetLink *link, XmppClientLink *client);
static int xmpp_ServingDisconnect(NetLink *link, XmppClientLink *client);

// Print some XMPP-related debugging information in a particular color.
static void xmpp_DebugPrintTraffic(U32 uId, bool bReceived, const char *pData, unsigned uLength)
{
	char *data = NULL;
	char *i;
	int color = (bReceived ? COLOR_RED : COLOR_BLUE) | COLOR_BRIGHT;

	// Copy string to working buffer.
	estrStackCreate(&data);
	estrConcat(&data, pData, uLength);

	// Replace non-displayable characters.
	for (i = data; i != data + uLength; ++i)
		if (!__isascii(*i) || !isprint((unsigned char)*i) || *i == '\n' || *i == '\r')
			*i = '.';

	// Print data.
	printf("\t%s %lu (%u): ", bReceived ? "Received from" : "Sent to", uId, uLength);
	printfColor(color, "%s\n", data);
	estrDestroy(&data);
}

// Print some XMPP-related debugging information in a particular color.
static void xmpp_LogTraffic(XmppClientLink *link, bool bReceived, const char *pData, unsigned uLength)
{
	char *pEscapedString = NULL;

	PERFINFO_AUTO_START_FUNC();

	// Return immediately if diabled.
	if (sbDisableXmppTrafficLogging)
	{
		PERFINFO_AUTO_STOP_FUNC();
		return;
	}

	// Return if there is nothing to log.
	if (!uLength)
	{
		PERFINFO_AUTO_STOP_FUNC();
		return;
	}

	// Redact and escape data.
	estrStackCreate(&pEscapedString);
	if (bReceived && link->bRedactPending)
	{
		const char *match = NULL;
		if (link->pRedactString)
			match = strnstr(pData, uLength, link->pRedactString);
		if (match)
		{
			char *redacted = NULL;
			estrStackCreate(&redacted);
			estrConcat(&redacted, pData, uLength);
			memset(redacted + (match - pData), 'X', strlen(link->pRedactString));
			estrAppendEscapedCount(&pEscapedString, redacted, uLength, true);
			estrDestroy(&redacted);
			free(link->pRedactString);
			link->pRedactString = NULL;
		}
		else
			estrConcatCharCount(&pEscapedString, 'X', uLength);
		if (!link->bRedact)
			link->bRedact = false;
		link->bRedactPending = false;
	}
	else
		estrAppendEscapedCount(&pEscapedString, pData, uLength, true);

	// If we're sending data in a parse, defer logging.
	// The purpose of deferring logging of sent data is to make sure that it is logged in proper sequence,
	// after the received data is logged.  The received data is not actually logged until after parsing, to allow
	// passwords to be redacted.  This is because there is no way to redact passwords without actually parsing the
	// XML first.  This does mean that XMPP_TRAFFIC logs will be out-of-order with respect to all other logs, but
	// this is necessary to protect the secrecy of passwords.
	if (!bReceived && sbInParse)
	{
		int current;
		if (!ppDeferredLogs)
			eaPush(&ppDeferredLogs, StructCreate(parse_XmppDeferredLog));
		current = eaSize(&ppDeferredLogs) - 1;
		eaPush(&ppDeferredLogs, StructCreate(parse_XmppDeferredLog));
		ppDeferredLogs[current]->uLinkId = link->id;
		estrCopy(&ppDeferredLogs[current]->string, &pEscapedString);
		estrDestroy(&pEscapedString);
		PERFINFO_AUTO_STOP_FUNC();
		return;
	}

	// Log data.
	servLog(LOG_XMPP_TRAFFIC, bReceived ? "StanzaReceived" : "StanzaSent", "link %"FORM_LL"u string \"%s\"", link->id, pEscapedString);
	estrDestroy(&pEscapedString);

	PERFINFO_AUTO_STOP_FUNC();
}

// Print data received by a client.
static void xmpp_DebugPrintDataReceived(XmppClientLink *link, const char *pData, unsigned uLength)
{
	if (siXmppTraceClients)
		xmpp_DebugPrintTraffic(link->id, true, pData, uLength);
	xmpp_LogTraffic(link, true, pData, uLength);
}

// Print data sent to a client.
static void xmpp_DebugPrintDataSent(XmppClientLink *link, const char *pData, unsigned uLength)
{
	if (siXmppTraceClients)
		xmpp_DebugPrintTraffic(link->id, false, pData, uLength);
	xmpp_LogTraffic(link, false, pData, uLength);
}

// Send data to a client.
void xmpp_SendDataToClient(XmppClientLink *link, const char *pData, unsigned uLength)
{
	// Do not send any additional data to closed streams
	if (link->state && link->state->stream_closed)
		return;

	// If there is buffered data, send it.
	if (estrLength(&link->pOutputBuffer))
	{
		estrConcat(&link->pOutputBuffer, pData, uLength);
		pData = link->pOutputBuffer;
		uLength = estrLength(&link->pOutputBuffer);
	}

	// Return if there is nothing to send.
	if (!pData || !uLength)
		return;

	// Send data to the client
	xmpp_DebugPrintDataSent(link, pData, uLength);
	if (link->tlsSession)
		// If this is a TLS link, send with TLS.
		tlsSessionSendPlaintext(link->tlsSession, pData, uLength);
	else
	{
		// Otherwise just write the data to the link.
		if (link->link)
		{
			Packet *packet = pktCreateRaw(link->link);
			pktSendBytesRaw(packet, pData, (int)uLength);
			pktSendRaw(&packet);
			siBytesSent += uLength;
		}
	}

	// Clear the output buffer, if any.
	estrClear(&link->pOutputBuffer);
}

// Send a string to a client.
void xmpp_SendStringToClient(XmppClientLink *link, const char *pString)
{
	size_t len = strlen(pString);
	devassert(len < INT_MAX);
	xmpp_SendDataToClient(link, pString, (int)len);
}

// Send a string to a client, with formatting.
void xmpp_SendStringToClientf(XmppClientLink *link, const char *pFormat, ...)
{
	estrGetVarArgs(&link->pOutputBuffer, pFormat);
	xmpp_SendDataToClient(link, NULL, 0);
}

// Queue data to be sent to a client.
void xmpp_SendDataToClientBuffer(XmppClientLink *link, const char *pData, unsigned uLength)
{
	estrConcat(&link->pOutputBuffer, pData, uLength);
}

// Queue a string to be sent to a client.
void xmpp_SendStringToClientBuffer(XmppClientLink *link, const char *pString)
{
	size_t len = strlen(pString);
	devassert(len < INT_MAX);
	xmpp_SendDataToClientBuffer(link, pString, (int)len);
}

// Queue a string to be sent to a client, with formatting.
void xmpp_SendStringToClientBufferf(XmppClientLink *link, const char *pFormat, ...)
{
	estrGetVarArgs(&link->pOutputBuffer, pFormat);
}

bool xmpp_ClientIsConnected(XmppClientLink *client)
{
	return linkConnected(client->link);
}

bool xmpp_ClientIsClosing(XmppClientLink *client)
{
	return !linkConnected(client->link) || client->bClosed;
}

void xmpp_DestroyClientLink(XmppClientLink *client)
{
	if (client)
	{
		// Free resources.
		estrDestroy(&client->pOutputBuffer);
		if (client->xp)
		{
			XMPP_ParserDestroy(client->xp);
			client->xp = NULL;
		}
		if (client->tlsSession)
		{
			tlsSessionDestroy(client->tlsSession);
			client->tlsSession = NULL;
		}
		free(client->pRedactString);
		client->pRedactString = NULL;
		if (client->state)
		{
			XMPP_DestroyClient(client->state);
			client->state = NULL;
		}
	}
}

// Disconnect a client
void xmpp_Disconnect(XmppClientLink *link, const char *reason)
{
	verbose_printf("Client %"FORM_LL"u: Being disconnected\n", link->id);

	// Close link.
	if (!link->link)
		return;
	link->bClosed = true;
	// Data is freed in xmpp_ServingDisconnect (NetLink disconnect handler)
	linkFlushAndClose(&link->link, reason);
	estrCopy2(&link->pClosedReason, reason);
}

// Note that parsing has begun.
static void StartParse()
{
	devassert(!sbInParse);
	sbInParse = true;
}

// Note that parsing has completed.
static void EndParse()
{
	devassert(sbInParse);
	sbInParse = false;

	// Flush logs, if necessary.
	if (!sbDisableXmppTrafficLogging && ppDeferredLogs)
	{
		int i;
		for (i = 0; i < eaSize(&ppDeferredLogs) - 1; ++i)
			servLog(LOG_XMPP_TRAFFIC, "StanzaSent", "link %"FORM_LL"u string \"%s\"", ppDeferredLogs[i]->uLinkId, ppDeferredLogs[i]->string);
		eaSetSizeStruct(&ppDeferredLogs, parse_XmppDeferredLog, 1);
	}
}

// Returns true if TLS is enabled.
bool xmpp_TlsAllowed()
{
	return !!xmppCertificate;
}

// Returns true if a connection is required to use TLS.
bool xmpp_TlsRequired(XmppClientLink *link)
{
	// Support mode to allow all clients to connect without TLS.
	if (siXmppTlsOptional)
		return false;

	// Trusted links are not required to use TLS.
	return !XMPP_NetTrustedLink(link);
}

// Return true if the link has successfully negotiated a TLS session.
bool xmpp_UsingTls(XmppClientLink *link)
{
	return !!link->tlsSession;
}

// Data has been decrypted by TLS, and needs to be parsed.
static void xmpp_TlsPlainDataReceived(TlsSession *pSession, void *pUserData, const char *pData, size_t uLength)
{
	XmppClientLink *link = pUserData;

	// Parse XML stream.
	StartParse();
	XMPP_ParserParse(link->xp, pData, (int)uLength);
	xmpp_DebugPrintDataReceived(link, pData, (unsigned)uLength);
	EndParse();
}

// Data has been encrypted by TLS, and needs to be sent to the client.
static void xmpp_TlsCipherDataSend(TlsSession *pSession, void *pUserData, const char *pData, size_t uLength)
{
	XmppClientLink *link = pUserData;
	Packet *packet;
	if (link->link)
	{
		packet = pktCreateRaw(link->link);
		pktSendBytesRaw(packet, pData, (int)uLength);
		pktSendRaw(&packet);
		siBytesSent += uLength;
	}
}

// Handle data from a client.
static void xmpp_ServingMsg(Packet *pkt, int cmd, NetLink *link, XmppClientLink *client)
{
	char *data = pktGetStringRaw(pkt);
	U32 len = pktGetSize(pkt);

	// Record bytes received.
	siBytesReceived += len;

	if (client->bClosed)
		return;

	// If the link is encrypted, decrypt.
	if (client->tlsSession)
		tlsSessionReceivedCiphertext(client->tlsSession, data, len);

	// Otherwise, parse XML stream.
	else if (client->xp)
	{
		StartParse();
		XMPP_ParserParse(client->xp, data, len);
		xmpp_DebugPrintDataReceived(client, data, len);
		EndParse();
	}
}

// Handle a client disconnecting.
static int xmpp_ServingDisconnect(NetLink *link, XmppClientLink *client)
{
	char ip[17];
	char remotePort[6];
	char localPort[6];
	char id[21];

	if (client && estrLength(&client->pOutputBuffer))
		verbose_printf("Warning: Discarding unsent data in output buffer of closed connection.\n");

	// Log disconnect.
	--siConnections;
	sprintf(remotePort, "%lu", linkGetPort(link));
	sprintf(localPort, "%lu", linkGetListenPort(link));
	if (client)
		sprintf(id, "%"FORM_LL"u", client->id);
	servLogWithPairs(LOG_XMPP_GENERAL, "XmppDisconnect",
		"id", id,
		"ip", linkGetIpStr(link, ip, sizeof(ip)),
		"remotePort", remotePort,
		"localPort", localPort, NULL);

	xmpp_DestroyClientLink(client);
	estrDestroy(&client->pOutputBuffer);
	return 0;
}

// Handle a new client connecting.
static int xmpp_ServingConnect(NetLink *link, XmppClientLink *client)
{
	static U64 nextID = 1;
	char ip[17];
	char remotePort[6];
	char localPort[6];
	char id[21];

	// In production mode, don't trust client links.
	if (isProductionMode())
		linkSetIsNotTrustworthy(link, true);

	// Initialize client data.
	client->link = link;
	client->id = nextID++;
	client->state = XMPP_CreateClient(client);
	client->xp = XMPP_ParserCreate(client->state, NULL, !xmpp_TlsRequired(client));

	// Log connection.
	++siConnections;
	sprintf(remotePort, "%lu", linkGetPort(link));
	sprintf(localPort, "%lu", linkGetListenPort(link));
	sprintf(id, "%"FORM_LL"u", client->id);
	servLogWithPairs(LOG_XMPP_GENERAL, "XmppConnect",
		"id", id,
		"ip", linkGetIpStr(link, ip, sizeof(ip)),
		"remotePort", remotePort,
		"localPort", localPort, NULL);

	return 1;
}

// A TLS session has been established.
static void xmpp_TlsSessionOpen(TlsSession *pSession, void *pUserData)
{
	XmppClientLink *link = pUserData;
	char ip[17];
	char remotePort[6];
	char localPort[6];
	char id[21];

	// Log session creation.
	sprintf(remotePort, "%lu", linkGetPort(link->link));
	sprintf(localPort, "%lu", linkGetListenPort(link->link));
	sprintf(id, "%"FORM_LL"u", link->id);
	servLogWithPairs(LOG_XMPP_GENERAL, "XmppTlsOpen",
		"id", id,
		"ip", linkGetIpStr(link->link, ip, sizeof(ip)),
		"remotePort", remotePort,
		"localPort", localPort, NULL);
}

// The TLS session has been terminated.
static void xmpp_TlsSessionClosed(TlsSession *pSession, void *pUserData)
{
	XmppClientLink *link = pUserData;
	char ip[17];
	char remotePort[6];
	char localPort[6];
	char id[21];

	// Close underlying link.
	if (link->link)
		linkFlushAndClose(&link->link, "The TLS session has been completed.");

	// Log session end.
	sprintf(remotePort, "%lu", link->link ? linkGetPort(link->link) : 0);
	sprintf(localPort, "%lu", link->link ? linkGetListenPort(link->link) : 0);
	sprintf(id, "%"FORM_LL"u", link->id);
	servLogWithPairs(LOG_XMPP_GENERAL, "XmppTlsClose",
		"id", id,
		"ip", linkGetIpStr(link->link, ip, sizeof(ip)),
		"remotePort", remotePort,
		"localPort", localPort, NULL);
}

// Handle the successful creation of a TLS session.
static int xmpp_ServingConnectTls(NetLink *link, XmppClientLink *client)
{
	int success;

	// Initialize normally.
	xmpp_ServingConnect(link, client);
	XMPP_ParserSecure(client->xp);

	// Start TLS negotiation.
	success = xmpp_StartTlsNegotiation(link, client);
	if (!success)
		linkRemove(&link); // Immediately close it
	return success;
}

// Start TLS negotiation with a client.
static int xmpp_StartTlsNegotiation(NetLink *link, XmppClientLink *client)
{
	// Start TLS negotiation.
	devassert(xmppCertificate);
	client->tlsSession = tlsSessionStartServer(client, xmppCertificate, xmpp_TlsPlainDataReceived, xmpp_TlsCipherDataSend,
		xmpp_TlsSessionOpen, xmpp_TlsSessionClosed);
	if (!client->tlsSession)
	{
		linkFlushAndClose(&link, "Unable to start TLS negotiation");
		return 0;
	}
	return 1;
}

// Process pending XMPP network activity.
void XMPP_NetTick(F32 elapsed)
{
	PERFINFO_AUTO_START_FUNC();
	devassert(spXmppComm);
	commMonitor(spXmppComm);
	PERFINFO_AUTO_STOP_FUNC();
}

// Start accepting XMPP clients.
bool XMPP_NetBegin()
{
	if (!gJIDDomain)
		gJIDDomain = strdup(makeHostNameStr(getHostPublicIp()));
	//only once
	if (spXmppComm) return false;
	if (!(spXmppComm = commCreate(0,1)))
	{
		Errorf("Failed to comCreate XMPP comm.");
		return false;
	}

	// Listen on normal XMPP port.
	if (!commListen(spXmppComm, LINKTYPE_TOUNTRUSTED_500K, LINK_RAW, XMPP_DEFAULT_PORT, 
		xmpp_ServingMsg,
		xmpp_ServingConnect,
		xmpp_ServingDisconnect,
		sizeof(XmppClientLink)))
	{
		commDestroy(&spXmppComm);
		Errorf("Failed to commListen XMPP on port %d.", XMPP_DEFAULT_PORT);

		return false;
	}

	// Load certificate.
	if (*xmppCertificateName)
	{
		xmppCertificate = tlsLoadCertificate(xmppCertificateName);
		if (!xmppCertificate)
			Errorf("Failed to load certificate for XMPP TLS");
	}

	// Listen on legacy XMPP TLS port.
	if (xmppCertificate)
	{
		if (!commListen(spXmppComm, LINKTYPE_TOUNTRUSTED_500K, LINK_RAW, XMPP_LEGACY_TLS_PORT, 
			xmpp_ServingMsg,
			xmpp_ServingConnectTls,
			xmpp_ServingDisconnect,
			sizeof(XmppClientLink)))
		{
			commDestroy(&spXmppComm);
			Errorf("Failed to commListen XMPP on port %d.", XMPP_LEGACY_TLS_PORT);
			return false;
		}
	}

	return true;
}

// Allocate a string representing the JID domain for this host.
char *XMPP_MakeDomain()
{
	return strdup(makeHostNameStr(getHostPublicIp()));
}

// Authentication is complete.
void XMPP_NetAuthComplete(XmppClientLink *link, bool success)
{
	XMPP_ParserAuthComplete(link->xp, success);
}

// Start TLS negotiation.
void XMPP_NetStartTls(XmppClientLink *client)
{
	int success;
	U32 id;
	NetLink *link;
	XMPP_Parser *parser;
	void *userData;

	// Don't allow STARTTLS twice.  The parser should not allow this.
	if (client->tlsSession)
	{
		devassert(0);
		return;
	}

	// Clear all state gathered to this point.
	id = client->id;
	link = client->link;
	parser = client->xp;
	userData = client->state;
	memset(client, 0, sizeof(*client));

	// Restart the parser.
	XMPP_ParserRestart(parser);
	XMPP_ParserSecure(parser);

	// Reinitialize the client state.
	client->id = id;
	client->link = link;
	client->xp = parser;
	client->state = userData;

	// Start TLS negotiation.
	success = xmpp_StartTlsNegotiation(client->link, client);
	if (!success)
		linkFlushAndClose(&link, "Failed TLS"); // This occurs inside the msg handler and must be a flush and close
}

// Get the link's client identifier.
U64 XMPP_NetUniqueClientId(XmppClientLink *link)
{
	return link->id;
}

// Return true if this link is trusted.
bool XMPP_NetTrustedLink(XmppClientLink *link)
{
	U32 ip;

	// If not connected, the link isn't trusted.
	if (!link->link)
		return false;

	// Trust links with IPs on the trusted IP list.
	ip = linkGetIp(link->link);
	return ipfIsTrustedIp(ip);
}

// Copy the IP address of this link.
char *XMPP_NetIpStr(XmppClientLink *link, char *buf, int buf_size)
{
	return linkGetIpStr(link->link, buf, sizeof(buf));
}

// If true, redact traffic on this link.
void XMPP_NetRedact(XmppClientLink *link, bool bRedact)
{
	devassert(bRedact == true || bRedact == false);
	link->bRedact = bRedact;
	if (bRedact)
		link->bRedactPending = true;
}

// Redact only the specific string passed in, if found.  If not found, redact everything.
void XMPP_NetRedactString(XmppClientLink *link, const char *pString)
{
	if (link->pRedactString)
	{
		free(link->pRedactString);
		link->pRedactString = NULL;
	}
	else
		link->pRedactString = strdup(pString);
}

// Total number of connected XMPP clients.
unsigned long XMPP_NetTotalConnections()
{
	return siConnections;
}

// Total number of bytes sent.
U64 XMPP_NetBytesSent()
{
	return siBytesSent;
}

// Total number of bytes received.
U64 XMPP_NetBytesReceived()
{
	return siBytesReceived;
}

void XMPPLink_SetLoginState(XmppClientState *state, XMPP_LoginState eState)
{
	if (devassert(state->link))
		XMPP_ParserSetLoginState(state->link->xp, eState);
}

// Get the XMPP domain name.
const char *XMPP_Domain()
{
	// If a domain to use was specified on the command line, use that one.
	if (*gJIDDomainForced)
		return gJIDDomainForced;

	// Otherwise, use the one that was discovered on startup.
	devassert(gJIDDomain && *gJIDDomain);
	return gJIDDomain;
}

#include "XMPP_Net_c_ast.c"
