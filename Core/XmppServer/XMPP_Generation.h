// Create XML for XMPP messages.
// This knows very little about XMPP other than how to format it.

#pragma once

#include "ChatServer/xmppTypes.h"

typedef struct XmppClientLink XmppClientLink;
typedef struct XmppGenerator XmppGenerator;
typedef struct XMPP_DiscoItem XMPP_DiscoItem;
typedef struct XMPP_RosterItem XMPP_RosterItem;
typedef struct XMPP_ChatOccupant XMPP_ChatOccupant;

// Service discovery item type
enum XMPP_DiscoItemType
{
	XMPP_DiscoItemType_Server,
	XMPP_DiscoItemType_Conference
};

/************************************************************************/
/* Generation                                                           */
/************************************************************************/

// Create XMPP generator.
XmppGenerator *XMPP_CreateGenerator(XmppClientLink *link);

// Destroy XMPP generator.
void XMPP_DestroyGenerator(XmppGenerator *gen);

// Restart the XMPP generator.
void XMPP_RestartGenerator(XmppGenerator *gen);

// Total number of XMPP stanzas generated.
U64 XMPP_StanzasGenerated(void);

/************************************************************************/
/* XMPP Errors                                                          */
/************************************************************************/

// Send a stream error on a stream, and terminate the stream.
void XMPP_GenerateStreamError(XmppGenerator *gen, enum XMPP_StreamErrorCondition error, const char *text);

// Send an authentication error on a stream, and terminate the stream.
void XMPP_GenerateSaslError(XmppGenerator *gen, enum XMPP_SaslError error, const char *text);

// Send a stanza error.
void XMPP_GenerateStanzaError(XmppGenerator *gen, XMPP_StanzaErrorCondition error, XMPP_StanzaErrorType type, const char *domain,
							  const char *to, const char *element, const char *id, const char *text);

/************************************************************************/
/* XMPP Stanzas and Stanza Responses                                    */
/************************************************************************/

// <starttls>
// Reset the connection as if it were freshly connected.
void xmpp_s_starttls(XmppGenerator *gen);

// <stream>
void xmpp_s_openstream(XmppGenerator *gen, const char *domain, U64 clientId);

// </stream>
void xmpp_s_closestream(XmppGenerator *gen);

// <stream:features> before authentication
void xmpp_s_sendauthstreamfeatures(XmppGenerator *gen, bool allowTls, bool requireTls);

// <stream:features> after authentication
void xmpp_s_sendstreamfeatures(XmppGenerator *gen);

// <success>
void xmpp_s_sendauthresponse(XmppGenerator *gen);

// <bind>
bool xmpp_s_sendbindresponse(XmppGenerator *gen, const char *id, const char *resource, const char *username, const char *domain);

// <session>
void xmpp_s_sendsessionresponse(XmppGenerator *gen, char *id, const char *domain);

// Pong
void xmpp_s_sendpong(XmppGenerator *gen, const char *domain, char *id, char *to);

// <item>
void xmpp_s_sendroster(XmppGenerator *gen, const char *id, const char *to, XMPP_RosterItem **roster);

// jabber:iq:roster push
void xmpp_s_sendrosterpush(XmppGenerator *gen, const char *id, const char *to, XMPP_RosterItem **roster);

// jabber:iq:roster remove
void xmpp_s_sendrosterpushremove(XmppGenerator *gen, const char *id, const char *to, const char *jid);

// Roster update result
void xmpp_s_sendrosterresult(XmppGenerator *gen, const char *id, const char *to);

// Presence update
void xmpp_s_sendpresence(XmppGenerator *gen, XMPP_PresenceAvailability availability, const char *from, const char *to,
						 const char *id, XMPP_Priority priority, const char *status);

// Presence subscription request
void xmpp_s_sendpresencesubscribe(XmppGenerator *gen, const char *from, const char *to, const char *id);

// Presence approved
void xmpp_s_sendpresencesubscribed(XmppGenerator *gen, const char *from, const char *to, const char *id);

// Presence unsubscription request
void xmpp_s_sendpresenceunsubscribe(XmppGenerator *gen, const char *from, const char *to, const char *id);

// Presence ubsubscription response
void xmpp_s_sendpresenceunsubscribed(XmppGenerator *gen, const char *from, const char *to, const char *id);

// <message>
void xmpp_s_sendmessage(XmppGenerator *gen, const char *to, const char *from, const char *id,
						const char *body, const char *subject, const char *thread, XMPP_MessageType type);

// http://jabber.org/protocol/disco#items
void xmpp_s_senddiscoitem(XmppGenerator *gen, const char *id, const char *from, const char *to, XMPP_DiscoItem **ppItems);

// http://jabber.org/protocol/disco#info
void xmpp_s_senddiscoinfo(XmppGenerator *gen, const char *id, const char *from, const char *to, const char *const *features,
						  size_t featuresSize, const char *name, enum XMPP_DiscoItemType type);

// Send chat room occupant presences. Called on initial XMPP Chat Room join for the joining user.
void xmpp_s_sendroomoccupants(XmppGenerator *gen, const char *domain, const char *room,
							  const char *to, XMPP_ChatOccupant **eaOccupants, bool rewrite);

// Send chat room occupant presence update for a single occupant.
void xmpp_s_sendroompresence(XmppGenerator *gen, const char *domain, const char *room,
							 const char *to, XMPP_ChatOccupant *occupant, bool bLeaving, bool bKicked, bool bSelf);
