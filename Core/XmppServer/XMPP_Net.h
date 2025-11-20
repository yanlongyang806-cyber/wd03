// Manage XMPP client network connections.

#pragma once

#include "ChatServer/xmppTypes.h"

/************************************************************************/
/* XMPP Net                                                             */
/************************************************************************/

typedef struct XmppClientLink XmppClientLink;
typedef struct XmppClientState XmppClientState;

// Get the XMPP domain name.
const char *XMPP_Domain(void);

// Start accepting XMPP clients.
bool XMPP_NetBegin(void);

// Process pending XMPP network activity.
void XMPP_NetTick(F32 elapsed);

// Send data to a client.
void xmpp_SendDataToClient(XmppClientLink *link, const char *pData, unsigned uLength);

// Send a string to a client.
void xmpp_SendStringToClient(XmppClientLink *link, const char *pString);

// Send a string to a client, with formatting.
void xmpp_SendStringToClientf(XmppClientLink *link, const char *pFormat, ...);

// Queue data to be sent to a client.
void xmpp_SendDataToClientBuffer(XmppClientLink *link, const char *pData, unsigned uLength);

// Queue a string to be sent to a client.
void xmpp_SendStringToClientBuffer(XmppClientLink *link, const char *pString);

// Queue a string to be sent to a client, with formatting.
void xmpp_SendStringToClientBufferf(XmppClientLink *link, const char *pFormat, ...);

bool xmpp_ClientIsConnected(XmppClientLink *client);
bool xmpp_ClientIsClosing(XmppClientLink *client);

// Destroy all the data in the XmppClientLink (DO NOT CALL IN THE MIDDLE OF STREAM PROCESSING)
void xmpp_DestroyClientLink(XmppClientLink *client);

// Disconnect a client.
void xmpp_Disconnect(XmppClientLink *link, const char *reason);

// Returns true if TLS is enabled.
bool xmpp_TlsAllowed(void);

// Returns true if a connection is required to use TLS.
bool xmpp_TlsRequired(XmppClientLink *link);

// Return true if the link has successfully negotiated a TLS session.
bool xmpp_UsingTls(XmppClientLink *link);

// Allocate a string representing the JID domain for this host.
char *XMPP_MakeDomain(void);

// Authentication is complete.
void XMPP_NetAuthComplete(XmppClientLink *link, bool success);

// Start TLS negotiation.
void XMPP_NetStartTls(XmppClientLink *link);

// Get the link's client identifier.
U64 XMPP_NetUniqueClientId(XmppClientLink *link);

// Return true if this link is trusted.
bool XMPP_NetTrustedLink(XmppClientLink *link);

// Copy the IP address of this link.
char *XMPP_NetIpStr(XmppClientLink *link, char *buf, int buf_size);

// If true, redact traffic on this link.
void XMPP_NetRedact(XmppClientLink *link, bool bRedact);

// Redact only the specific string passed in, if found.  If not found, redact everything.
void XMPP_NetRedactString(XmppClientLink *link, const char *pString);

// Total number of connected XMPP clients.
unsigned long XMPP_NetTotalConnections(void);

// Total number of XMPP bytes sent.
U64 XMPP_NetBytesSent(void);

// Total number of XMPP bytes received.
U64 XMPP_NetBytesReceived(void);

// Set the XMPPParser's login state
void XMPPLink_SetLoginState(XmppClientState *state, XMPP_LoginState eState);