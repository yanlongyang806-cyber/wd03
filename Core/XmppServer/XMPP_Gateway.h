// Performs bulk of XMPP processing.
// It knows very little about the Global Chat Server, but a lot about XMPP.  It gets stanzas from XMPP_Parsing, and sends data back to the
// client with XMPP_Generation.  It manages the actual network connections with XMPP_Net.  To communicates with the rest of the
// Global Chat Server, it uses XMPP_Chat.

#pragma once

#include "ChatServer/xmppTypes.h"
#include "XMPP_Types.h"
#include "XMPP_Gateway_h_ast.h"

typedef struct AccountTicket AccountTicket;
typedef struct AccountValidator AccountValidator;
typedef struct XmppClientLink XmppClientLink;
typedef struct XmppGenerator XmppGenerator;
typedef struct XMPP_RosterItem XMPP_RosterItem;

// A particular client link
AUTO_STRUCT;
typedef struct XmppClientState
{
	U32 uStateID; AST(KEY)

	// Client link
	XmppClientLink *link;						AST(UNOWNED LATEBIND)	// Connection data for this client
	XmppGenerator *generator;					AST(UNOWNED LATEBIND)	// XMPP generator state

	// Account
	AccountValidator *validator;				NO_AST					// When logging in, account login validation data
	AccountTicket *ticket;						AST(UNOWNED LATEBIND)	// Once logged in, account login ticket data
	int iAccessLevel;                                                   // Max access level from AccountTicket permissions for products with XMPP access

	// Client state
	bool stream_open;													// True if the XML stream root tag has been sent
	bool stream_closed;													// True if the XML stream close tag has been sent
	char *resource;														// Identifier of this specific connection; analogous to character name

	// Cached values -- these stay on the XMPP Server
	char *cachedEscapedName;					AST(ESTRING)
	char *cachedFullJid;						AST(ESTRING)			// If set, full JID of this resource
	char *cachedBareJid;						AST(ESTRING)			// If set, bare JID of the resource's node
} XmppClientState;

// Decomposed parts of a JID, used by JidDecompose().
struct JidComponents
{
	const char *node;
	size_t nodelen;
	const char *domain;
	size_t domainlen;
	const char *resource;
	size_t resourcelen;
};

/************************************************************************/
/* General utility                                                      */
/************************************************************************/

void XMPP_ClearServerData(void);

// Return the full JID of an XMPP client.
const char *xmpp_client_jid(const XmppClientState *state);
// Return the bare JID of an XMPP client.
const char *xmpp_client_bare_jid(const XmppClientState *state);

// Return true if this is a well-formed JID.  Note that it may not actually exist.
bool XMPP_ValidateJid(const char *jid);

// Decompose a JID into its component parts.
struct JidComponents XMPP_JidDecompose(const char *jid);

// Return true if two JIDs are equal, and false otherwise.
bool JidEqual(const char *lhs, const char *rhs);

// Return true if two JIDs are equal, ignoring any resource components.
bool BareJidEqual(const char *lhs, const char *rhs);

// Return true if two domains are equal.
bool DomainEqual(const char *lhs, const char *rhs);

// Create a roster item.
XMPP_RosterItem *XMPP_MakeRosterItem(const char *jid, const char *name, bool friends, char **guilds, bool ask);

// Get the client's unique identifier.
U64 XMPP_UniqueClientId(const XmppClientState *state);

// Get the client's unique identifier, as a string.
char *XMPP_UniqueClientIdStr(const XmppClientState *state, char *buf, int buf_size);

// Total number of connected XMPP clients.
unsigned long XMPP_TotalConnections(void);

// Total number of XMPP bytes sent.
U64 XMPP_BytesSent(void);

// Total number of XMPP bytes received.
U64 XMPP_BytesReceived(void);

// Total number of XMPP stanzas sent.
U64 XMPP_StanzasSent(void);

// Total number of XMPP stanzas received.
U64 XMPP_StanzasReceived(void);

/************************************************************************/
/* Process incoming XMPP sessions and stanzas                           */
/************************************************************************/

// Increment the count of stanzas received.
void XMPP_StatsIncrementStanzaCount(void);

// Process request for STARTTLS negotiation.
void XMPP_ProcessStartTls(XmppClientState *state);

// Process resource bind request.
bool XMPP_ProcessBind(XmppClientState *state, const char *id, const char *resource);

// Process session initiation.
void XMPP_ProcessSession(XmppClientState *state, char *id);

// Process roster requests.
void XMPP_ProcessRosterGet(XmppClientState *state, const char *id, const char *to, const char *from);

// Process roster adds and updates.
void XMPP_ProcessRosterUpdate(XmppClientState *state, const char *id, const char *to, const char *from,
							  const char *jid, const char *name, const char *const *groups);

// Process roster removes.
void XMPP_ProcessRosterRemove(XmppClientState *state, const char *id, const char *to, const char *from,
							  const char *jid);

// Process ping.
void XMPP_ProcessPing(XmppClientState *state, char *id);

// Process message.
void XMPP_ProcessMessage(XmppClientState *state, const char *to, const char *from, const char *id,
						 const char *body, const char *subject, const char *thread, XMPP_MessageType type);

// Send a roster push with one or more items.
void XMPP_RosterPushItems(XmppClientState *state, XMPP_RosterItem **ppItems);

// Send a roster push, with explicit parameters.
void XMPP_RosterPush(XmppClientState *state, const char *jid, const char *name, bool friends, char **guilds, bool ask);

// Process presence update
void XMPP_ProcessPresenceUpdate(XmppClientState *state, XMPP_PresenceAvailability availability, const char *from, const char *to,
								const char *id, XMPP_Priority priority, const char *status);

// Process presence unavailability update.
void XMPP_ProcessPresenceUnavailable(XmppClientState *state, const char *from, const char *to, const char *id, const char *status);

// Process presence subscribe attempt.
void XMPP_ProcessPresenceSubscribe(XmppClientState *state, const char *from, const char *to, const char *id);

// Process presence subscription confirmation.
void XMPP_ProcessPresenceSubscribed(XmppClientState *state, const char *from, const char *to, const char *id);

// Process presence unsubscription attempt.
void XMPP_ProcessPresenceUnsubscribe(XmppClientState *state, const char *from, const char *to, const char *id);

// Process presence unsubscription.
void XMPP_ProcessPresenceUnsubscribed(XmppClientState *state, const char *from, const char *to, const char *id);

// Process presence probes.
void XMPP_ProcessPresenceProbe(XmppClientState *state, const char *from, const char *to, const char *id);

// Process presence errors.
void XMPP_ProcessPresenceError(XmppClientState *state, const char *from, const char *to, const char *id);

// Process information discovery.
void XMPP_ProcessDiscoInfo(XmppClientState *state, const char *to, const char *from, const char *id);

// Process item discovery.
void XMPP_ProcessDiscoItems(XmppClientState *state, const char *to, const char *from, const char *id);

/************************************************************************/
/* XMPP Parsing Events                                                  */
/************************************************************************/

// Handle a stream error generated by the parser.
void XMPP_HandleStreamError(XmppClientState *state, enum XMPP_StreamErrorCondition error, const char *text);

// Handle a SASL error generated by the parser.
void XMPP_HandleSaslError(XmppClientState *state, XMPP_SaslError error, const char *text);

// Handle a stanza error generated by the parser.
void XMPP_HandleStanzaError(XmppClientState *state, enum XMPP_StanzaErrorCondition error, XMPP_StanzaErrorType type,
							  const char *element, const char *id, const char *text);

// Authentication has begun.
void XMPP_HandleAuthBegin(XmppClientState *state);

// Authentication has ended.
void XMPP_HandleAuthEnd(XmppClientState *state);

// Redact a specific authentication string.
void XMPP_RedactAuthString(XmppClientState *state, const char *pString);

/************************************************************************/
/* XMPPChat interface to XMPP Gateway                                   */
/************************************************************************/

// Process pending XMPP client activity
void XMPP_Tick(F32 elapsed);

// Start accepting XMPP clients.
bool XMPP_Begin(void);

// Return true if this client is totally logged in.
bool XMPP_IsLoggedIn(const XmppClientState *state);

// Start a stream element in the appropriate state.
void XMPP_StartStream(XmppClientState *state, enum XMPP_LoginState loginState);

// The client has ended the stream prior to full login.
void XMPP_LoginAborted(XmppClientState *state, enum XMPP_LoginState loginState);

// The client or server has ended the stream.
void XMPP_EndStream(XmppClientState *state, const char *reason);

// Start checking the username and password for login.
void XMPP_ValidateLogin(XmppClientState *state, char *login, char *password);

// Find the client by state ID
XmppClientState *XMPP_FindClientById(U32 uID);

// Create a client. 
void *XMPP_CreateClient(XmppClientLink *link);

// Destroy a client.
void XMPP_DestroyClient(void *client);

// Destroy a client.
void XMPP_BootClient(void *client, const char *reason);

// Get the chat domain name.
const char *XMPP_RoomDomainName(int domain);

// Get the chat domain enum value from a JID.
int XMPP_GetRoomDomainEnum(const char *jid);

// Return true if this is a chat domain.
bool XMPP_IsAtChatDomain(const char *jid);

// Authentication is complete.
void XMPP_AuthComplete(XmppClientState *state, bool success, XMPP_SaslError error, const char *reason);

// Return true if this client is trusted by virtue on being on a trusted link.
bool XMPP_Trusted(XmppClientState *state);

// Copy the IP address of this link.
char *XMPP_GetIpStr(XmppClientState *state, char *buf, int buf_size);