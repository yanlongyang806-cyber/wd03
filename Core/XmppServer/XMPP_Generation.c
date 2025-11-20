// Create XML for XMPP messages.
// This knows very little about XMPP other than how to format it.

#include "crypt.h"
#include "error.h"
#include "objTransactions.h"
#include "trivia.h"
#include "XMLWriter.h"
#include "XMPP_Generation.h"
#include "XMPP_Generation_c_ast.h"
#include "XMPP_Net.h"
#include "XMPP_Types.h"

#include "ChatServer/xmppShared.h"

// XMPP Status Codes
typedef enum XmppStatusCode
{
	XmppStatus_Self = 110,    // Chat room presence update is for self
	XmppStatus_Rewrite = 210, // Force nickname change when joining chat room
	XmppStatus_Kicked = 307,  // User was kicked from the chat room
} XmppStatusCode;

// XMPP Generator
AUTO_STRUCT;
typedef struct XmppGenerator
{
	XmppClientLink *link;		AST(UNOWNED LATEBIND)	// Output link
	XmlWriter xml;				NO_AST					// XML generator
} XmppGenerator;

// Total number of XMPP stanzas generated.
static siStanzasGenerated = 0;

// Get the next message ID, in static storage.
static const char *NextMessageId()
{
	static U32 nextMessageID = 1;
	static char buffer[7 + 8 + 1] = "cryptic";
	snprintf_s(buffer + 7, sizeof(buffer)/sizeof(*buffer) - 7, "%08x", ++nextMessageID);
	return buffer;
}

// Finish and send a stanza.
static void StanzaDoneEx(XmppGenerator *gen, const char *pchFunction)
{
	++siStanzasGenerated;
	devassertmsgf(xmlWriterGetDepthLevel(&gen->xml) == 2, "XMPP XML Error - StanzaDone for %s.", pchFunction);
	xmlWriterEndElement(&gen->xml);
	xmpp_SendStringToClient(gen->link, xmlWriterGet(&gen->xml));
	xmlWriterClear(&gen->xml);
}
#define StanzaDone(gen) StanzaDoneEx(gen, __FUNCTION__)

// Add to and from attributes if present, and an id.  Generate an id if one is not given.
static void ToFromId(XmppGenerator *gen, const char *to, const char *from, const char *id)
{
	// Create a message identifier is none was given.
	if (!id || !*id)
		id = NextMessageId();

	// Send any attributes that were given.
	if (to)
		xmlWriterAddAttribute(&gen->xml, "to", to);
	if (from)
		xmlWriterAddAttribute(&gen->xml, "from", from);
	xmlWriterAddAttribute(&gen->xml, "id", id);
}

// Create XMPP generator.
XmppGenerator *XMPP_CreateGenerator(XmppClientLink *link)
{
	XmppGenerator *gen = StructCreate(parse_XmppGenerator);
	gen->link = link;
	xmlWriterCreate(&gen->xml);
	return gen;
}

// Destroy XMPP generator.
void XMPP_DestroyGenerator(XmppGenerator *gen)
{
	xmlWriterDestroy(&gen->xml);
	StructDestroy(parse_XmppGenerator, gen);
}

// Restart the XMPP generator.
void XMPP_RestartGenerator(XmppGenerator *gen)
{
	xmlWriterDestroy(&gen->xml);
	xmlWriterCreate(&gen->xml);
}

// Total number of XMPP stanzas generated.
U64 XMPP_StanzasGenerated(void)
{
	return siStanzasGenerated;
}

// <stream>
void xmpp_s_openstream(XmppGenerator *gen, const char *domain, U64 clientId)
{
	char buf[1023];

	// Generate cryptigraphically-secure random identifier.
	sprintf(buf, "cc%u_%"FORM_LL"u_%u", objServerID(), clientId, (unsigned)cryptSecureRand());

	// Open stream.
	xmlWriterStartElement(&gen->xml, "stream:stream");
	xmlWriterAddAttributes(&gen->xml, "xmlns", "jabber:client", "xmlns:stream", "http://etherx.jabber.org/streams",
		"from", domain, "id", buf, "version", "1.0", NULL);
	xmpp_SendStringToClient(gen->link, xmlWriterGet(&gen->xml));
	xmlWriterClear(&gen->xml);
	devassertmsgf(xmlWriterGetDepthLevel(&gen->xml) == 1, "XMPP-XML Error - stream open");
}

// </stream>
void xmpp_s_closestream(XmppGenerator *gen)
{
	if (!gen)
		return;
	xmlWriterEndElement(&gen->xml);
	xmpp_SendStringToClient(gen->link, xmlWriterGet(&gen->xml));
	xmlWriterClear(&gen->xml);
	devassertmsgf(xmlWriterGetDepthLevel(&gen->xml) == 0, "XMPP-XML Error - stream close");
}

// <stream:features> before authentication
void xmpp_s_sendauthstreamfeatures(XmppGenerator *gen, bool allowTls, bool requireTls)
{
	devassert(allowTls || !requireTls);

	xmlWriterStartElement(&gen->xml, "stream:features");

	// Advertise STARTTLS if we have a certificate to use.
	if (allowTls)
	{
		const char *tlsStatus = requireTls ? "required" : "optional";

		xmlWriterStartElement(&gen->xml, "starttls");;
		xmlWriterAddAttribute(&gen->xml, "xmlns", "urn:ietf:params:xml:ns:xmpp-tls");
		xmlWriterStartElement(&gen->xml, tlsStatus);
		xmlWriterEndElement(&gen->xml);
		xmlWriterEndElement(&gen->xml);
	}

	// Advertise mechanisms.
	if (!requireTls)
	{
		xmlWriterStartElement(&gen->xml, "mechanisms");
		xmlWriterAddAttribute(&gen->xml, "xmlns", "urn:ietf:params:xml:ns:xmpp-sasl");
		xmlWriterStartElement(&gen->xml, "mechanism");
		xmlWriterCharacters(&gen->xml, "PLAIN");
		xmlWriterEndElement(&gen->xml);
		xmlWriterEndElement(&gen->xml);
	}

	StanzaDone(gen);
}

// <stream:features> after authentication
void xmpp_s_sendstreamfeatures(XmppGenerator *gen)
{
	xmlWriterStartElement(&gen->xml, "stream:features");
	xmlWriterStartElement(&gen->xml, "bind");
	xmlWriterAddAttribute(&gen->xml, "xmlns", "urn:ietf:params:xml:ns:xmpp-bind");
	xmlWriterEndElement(&gen->xml);
	xmlWriterStartElement(&gen->xml, "session");
	xmlWriterAddAttribute(&gen->xml, "xmlns", "urn:ietf:params:xml:ns:xmpp-session");
	xmlWriterEndElement(&gen->xml);

	StanzaDone(gen);
}

// <success>
void xmpp_s_sendauthresponse(XmppGenerator *gen)
{
	xmlWriterStartElement(&gen->xml, "success");
	xmlWriterAddAttribute(&gen->xml, "xmlns", "urn:ietf:params:xml:ns:xmpp-sasl");
	StanzaDone(gen);
}

// <bind>
bool xmpp_s_sendbindresponse(XmppGenerator *gen, const char *id, const char *resource,
							 const char *username, const char *domain)
{
	xmlWriterStartElement(&gen->xml, "iq");
	xmlWriterAddAttributes(&gen->xml, "from", domain, "type", "result", "id", id, NULL);

	xmlWriterStartElement(&gen->xml, "bind");
	xmlWriterAddAttribute(&gen->xml, "xmlns", "urn:ietf:params:xml:ns:xmpp-bind");
	xmlWriterStartElement(&gen->xml, "jid");
	xmlWriterCharactersf(&gen->xml, "%s@%s/%s", username, domain, resource);
	xmlWriterEndElement(&gen->xml);
	xmlWriterEndElement(&gen->xml);

	StanzaDone(gen);
	return true;
}

// <session>
void xmpp_s_sendsessionresponse(XmppGenerator *gen, char *id, const char *domain)
{

	xmlWriterStartElement(&gen->xml, "iq");
	xmlWriterAddAttributes(&gen->xml, "type", "result", "id", id, "from", domain, NULL);
	xmlWriterStartElement(&gen->xml, "session");
	xmlWriterAddAttribute(&gen->xml, "xmlns", "urn:ietf:params:xml:ns:xmpp-session");
	xmlWriterEndElement(&gen->xml);
	StanzaDone(gen);
}

// Send a stanza error.
void XMPP_GenerateStanzaError(XmppGenerator *gen, enum XMPP_StanzaErrorCondition error, XMPP_StanzaErrorType type, const char *domain,
							  const char *to, const char *element, const char *id, const char *text)
{
	char *cond;
	char *errtype;

	// Look up the error name.
	switch (error)
	{
		case StanzaError_BadRequest:			cond = "bad-request";				break;
		case StanzaError_Conflict:				cond = "conflict";					break;
		case StanzaError_FeatureNotImplemented: cond = "feature-not-implemented";	break;
		case StanzaError_Forbidden:				cond = "forbidden";					break;
		case StanzaError_Gone:					cond = "gone";						break;
		case StanzaError_InternalServerError:	cond = "internal-server-error";		break;
		case StanzaError_ItemNotFound:			cond = "item-not-found";			break;
		case StanzaError_JIDMalformed:			cond = "jid-malformed";				break;
		case StanzaError_NotAcceptable:			cond = "not-acceptable";			break;
		case StanzaError_NotAllowed:			cond = "not-allowed";				break;
		case StanzaError_NotAuthorized:			cond = "not-authorized";			break;
		case StanzaError_PaymentRequired:		cond = "payment-required";			break;
		case StanzaError_PolicyViolation:		cond = "policy-violation";			break;
		case StanzaError_RecipientUnavailable:	cond = "recipient-unavailable";		break;
		case StanzaError_Redirect:				cond = "redirect";					break;
		case StanzaError_RegistrationRequired:	cond = "registration-required";		break;
		case StanzaError_RemoteServerNotFound:	cond = "remote-server-not-found";	break;
		case StanzaError_RemoteServerTimeout:	cond = "remote-server-timeout";		break;
		case StanzaError_ResourceConstraint:	cond = "resource-constraint";		break;
		case StanzaError_ServiceUnavailable:	cond = "service-unavailable";		break;
		case StanzaError_SubscriptionRequired:	cond = "subscription-required";		break;
		case StanzaError_UndefinedCondition:	cond = "undefined-condition";		break;
		case StanzaError_UnexpectedRequest:		cond = "unexpected-request";		break;
		default:
			devassert(0);
	}

	// Look up the error type name.
	switch (type)
	{
		case Stanza_Cancel: errtype = "cancel"; break;
		case Stanza_Continue: errtype = "continue"; break;
		case Stanza_Modify:	errtype = "modify"; break;
		case Stanza_Auth: errtype = "auth"; break;
		case Stanza_Wait: errtype = "wait"; break;
		default:
			devassert(0);
	}

	xmlWriterStartElement(&gen->xml, element);
	xmlWriterAddAttribute(&gen->xml, "type", "error");
	ToFromId(gen, to, domain, id);
	xmlWriterStartElement(&gen->xml, "error");
	xmlWriterAddAttribute(&gen->xml, "type", errtype);
	xmlWriterStartElement(&gen->xml, cond);
	xmlWriterAddAttribute(&gen->xml, "xmlns", "urn:ietf:params:xml:ns:xmpp-stanzas");
	xmlWriterEndElement(&gen->xml);
	if (text)
	{
		xmlWriterStartElement(&gen->xml, "text");
		xmlWriterAddAttribute(&gen->xml, "xmlns", "urn:ietf:params:xml:ns:xmpp-stanzas");
		xmlWriterCharacters(&gen->xml, text);
		xmlWriterEndElement(&gen->xml);
	}
	xmlWriterEndElement(&gen->xml);
	StanzaDone(gen);
}

// Pong
void xmpp_s_sendpong(XmppGenerator *gen, const char *domain, char *id, char *to)
{
	xmlWriterStartElement(&gen->xml, "iq");
	xmlWriterAddAttribute(&gen->xml, "type", "result");
	ToFromId(gen, to, domain, id);
	StanzaDone(gen);

	verbose_printf("Pong.\n");
}

// <item>
static void xmpp_s_sendrosteritem(XmppGenerator *gen, XMPP_RosterItem *roster)
{
	if (roster->jid == NULL)
		estrPrintf(&roster->jid, "%s@%s", roster->name, XMPP_Domain());
	xmlWriterStartElement(&gen->xml, "item");
	xmlWriterAddAttribute(&gen->xml, "jid", roster->jid);
	if (roster->name)
		xmlWriterAddAttribute(&gen->xml, "name", roster->name);

	switch (roster->subscription)
	{
		case XMPP_RosterSubscriptionState_None:
			// Don't add anything.
			break;
		case XMPP_RosterSubscriptionState_To:
			xmlWriterAddAttribute(&gen->xml, "subscription", "to");
			break;
		case XMPP_RosterSubscriptionState_From:
			xmlWriterAddAttribute(&gen->xml, "subscription", "from");
			break;
		case XMPP_RosterSubscriptionState_Both:
			xmlWriterAddAttribute(&gen->xml, "subscription", "both");
			break;
		default:
			devassert(0);
	}
	switch(roster->ask)
	{
		case XMPP_RosterSubscribeState_None:
			// Don't add anything.
			break;
		case XMPP_RosterSubscribeState_Subscribe:
			xmlWriterAddAttribute(&gen->xml, "ask", "subscribe");
			break;
		case XMPP_RosterSubscribeState_Subscribed:
			xmlWriterAddAttribute(&gen->xml, "ask", "subscribed");
			break;
		default:
			devassert(0);
	}

	EARRAY_CONST_FOREACH_BEGIN(roster->group, i, n);
	{
		xmlWriterStartElement(&gen->xml, "group");
		xmlWriterCharacters(&gen->xml, roster->group[i]);
		xmlWriterEndElement(&gen->xml);
	}
	EARRAY_FOREACH_END;

	xmlWriterEndElement(&gen->xml);
	++siStanzasGenerated;
}

void xmpp_s_sendroster(XmppGenerator *gen, const char *id, const char *clientjid, XMPP_RosterItem **roster)
{
	xmlWriterStartElement(&gen->xml, "iq");
	xmlWriterAddAttributes(&gen->xml, "to", clientjid, "type", "result", "id", id && *id ? id : NextMessageId(), NULL);
	xmlWriterStartElement(&gen->xml, "query");
	xmlWriterAddAttribute(&gen->xml, "xmlns", "jabber:iq:roster"); 

	// Generate each friend.
	EARRAY_CONST_FOREACH_BEGIN(roster, i, n);
		xmpp_s_sendrosteritem(gen, roster[i]);
	EARRAY_FOREACH_END;

	// Send roster
	xmlWriterEndElement(&gen->xml);
	StanzaDone(gen);
}

// jabber:iq:roster push
void xmpp_s_sendrosterpush(XmppGenerator *gen, const char *id, const char *to, XMPP_RosterItem **roster)
{
	xmlWriterStartElement(&gen->xml, "iq");
	xmlWriterAddAttributes(&gen->xml, "to", to, "type", "set", "id", id && *id ? id : NextMessageId(), NULL);
	xmlWriterStartElement(&gen->xml, "query");
	xmlWriterAddAttribute(&gen->xml, "xmlns", "jabber:iq:roster"); 

	// Generate each friend.
	EARRAY_CONST_FOREACH_BEGIN(roster, i, n);
		xmpp_s_sendrosteritem(gen, roster[i]);
	EARRAY_FOREACH_END;

	xmlWriterEndElement(&gen->xml);
	StanzaDone(gen);
}

// jabber:iq:roster remove
void xmpp_s_sendrosterpushremove(XmppGenerator *gen, const char *id, const char *to, const char *jid)
{
	xmlWriterStartElement(&gen->xml, "iq");
	xmlWriterAddAttribute(&gen->xml, "type", "set");
	ToFromId(gen, to, NULL, NULL);
	xmlWriterStartElement(&gen->xml, "query");
	xmlWriterAddAttribute(&gen->xml, "xmlns", "jabber:iq:roster");
	xmlWriterStartElement(&gen->xml, "item");
	xmlWriterAddAttributes(&gen->xml, "jid", jid, "subscription", "remove", NULL);
	xmlWriterEndElement(&gen->xml);
	xmlWriterEndElement(&gen->xml);
	StanzaDone(gen);
}

// Roster update result
void xmpp_s_sendrosterresult(XmppGenerator *gen, const char *id, const char *to)
{
	xmlWriterStartElement(&gen->xml, "iq");
	xmlWriterAddAttributes(&gen->xml, "id", id, "to", to, "type", "result", NULL);
	StanzaDone(gen);
}

// Presence update
void xmpp_s_sendpresence(XmppGenerator *gen, XMPP_PresenceAvailability availability, const char *from, const char *to,
						 const char *id, XMPP_Priority priority, const char *status)
{
	xmlWriterStartElement(&gen->xml, "presence");

	// Add a type attribute if this is an unavailability update.
	if (availability == XMPP_PresenceAvailability_Unavailable)
		xmlWriterAddAttribute(&gen->xml, "type", "unavailable");

	ToFromId(gen, to, from, id);

	// Set <show> appropriately.
	switch (availability)
	{
		case XMPP_PresenceAvailability_Away:
			xmlWriterStartElement(&gen->xml, "show");
			xmlWriterCharacters(&gen->xml, "away");
			xmlWriterEndElement(&gen->xml);
			break;
		case XMPP_PresenceAvailability_Chat:
			xmlWriterStartElement(&gen->xml, "show");
			xmlWriterCharacters(&gen->xml, "chat");
			xmlWriterEndElement(&gen->xml);
			break;
		case XMPP_PresenceAvailability_Dnd:
			xmlWriterStartElement(&gen->xml, "show");
			xmlWriterCharacters(&gen->xml, "dnd");
			xmlWriterEndElement(&gen->xml);
			break;
		case XMPP_PresenceAvailability_Xa:
			xmlWriterStartElement(&gen->xml, "show");
			xmlWriterCharacters(&gen->xml, "xa");
			xmlWriterEndElement(&gen->xml);
			break;
		case XMPP_PresenceAvailability_Normal:
		case XMPP_PresenceAvailability_Unavailable:
			break;  // No <show> element.
		default:
			devassert(0);
	}

	// Add status if available.
	if (status)
	{
		xmlWriterStartElement(&gen->xml, "status");
		xmlWriterCharacters(&gen->xml, status);
		xmlWriterEndElement(&gen->xml);
	}

	// Add priority if it is not the default.
	if (priority)
	{
		xmlWriterStartElement(&gen->xml, "priority");
		xmlWriterCharactersf(&gen->xml, "%d", (int)priority);
		xmlWriterEndElement(&gen->xml);
	}

	// Send the presence update.
	StanzaDone(gen);
}

// Presence subscription request
void xmpp_s_sendpresencesubscribe(XmppGenerator *gen, const char *from, const char *to, const char *id)
{
	xmlWriterStartElement(&gen->xml, "presence");
	xmlWriterAddAttribute(&gen->xml, "type", "subscribe");
	ToFromId(gen, to, from, id);
	StanzaDone(gen);
}

// Presence approved
void xmpp_s_sendpresencesubscribed(XmppGenerator *gen, const char *from, const char *to, const char *id)
{
	xmlWriterStartElement(&gen->xml, "presence");
	xmlWriterAddAttribute(&gen->xml, "type", "subscribed");
	ToFromId(gen, to, from, id);
	StanzaDone(gen);
}

// Presence unsubscription request
void xmpp_s_sendpresenceunsubscribe(XmppGenerator *gen, const char *from, const char *to, const char *id)
{
	xmlWriterStartElement(&gen->xml, "presence");
	xmlWriterAddAttribute(&gen->xml, "type", "unsubscribe");
	ToFromId(gen, to, from, id);
	StanzaDone(gen);
}

// Presence unsubscription response
void xmpp_s_sendpresenceunsubscribed(XmppGenerator *gen, const char *from, const char *to, const char *id)
{
	xmlWriterStartElement(&gen->xml, "presence");
	xmlWriterAddAttribute(&gen->xml, "type", "unsubscribed");
	ToFromId(gen, to, from, id);
	StanzaDone(gen);
}

// <message>
void xmpp_s_sendmessage(XmppGenerator *gen, const char *to, const char *from, const char *id,
						const char *body, const char *subject, const char *thread, XMPP_MessageType type)
{
	const char *typeString;

	// Get message type string.
	switch (type)
	{
		case XMPP_MessageType_Chat:
			typeString = "chat";
			break;
		case XMPP_MessageType_Error:
			typeString = "error";
			break;
		case XMPP_MessageType_Groupchat:
			typeString = "groupchat";
			break;
		case XMPP_MessageType_Headline:
			typeString = "headline";
			break;
		case XMPP_MessageType_Normal:
			typeString = "normal";
			break;
		default:
			devassert(0);
	}

	// Create message header.
	xmlWriterStartElement(&gen->xml, "message");
	xmlWriterAddAttribute(&gen->xml, "type", typeString);
	ToFromId(gen, to, from, id);

	// Copy message data.
	if (subject)
	{
		xmlWriterStartElement(&gen->xml, "subject");
		xmlWriterCharacters(&gen->xml, subject);
		xmlWriterEndElement(&gen->xml);
	}
	if (body)
	{
		xmlWriterStartElement(&gen->xml, "body");
		xmlWriterCharacters(&gen->xml, body);
		xmlWriterEndElement(&gen->xml);

	}
	if (thread)
	{
		xmlWriterStartElement(&gen->xml, "thread");
		xmlWriterCharacters(&gen->xml, thread);
		xmlWriterEndElement(&gen->xml);
	}

	// Send message.
	StanzaDone(gen);
}

// <starttls>
// Reset the connection as if it were freshly connected.
void xmpp_s_starttls(XmppGenerator *gen)
{
	// Tell client to go ahead with negotiation.
	xmlWriterStartElement(&gen->xml, "proceed");
	xmlWriterAddAttribute(&gen->xml, "xmlns", "urn:ietf:params:xml:ns:xmpp-tls");
	StanzaDone(gen);
}

// Send a stream error on a stream, DOES NOT terminate the stream.
void XMPP_GenerateStreamError(XmppGenerator *gen, enum XMPP_StreamErrorCondition error, const char *text)
{
	const char *name;

	// Convert error code to element name.
	switch (error)
	{
		case XMPP_StreamError_BadFormat:
			name = "bad-format";
			break;
		case XMPP_StreamError_BadNamespacePrefix:
			name = "bad-namespace-prefix";
			break;
		case XMPP_StreamError_Conflict:
			name = "conflict";
			break;
		case XMPP_StreamError_ConnectionTimeout:
			name = "connection-timeout";
			break;
		case XMPP_StreamError_HostGone:
			name = "host-gone";
			break;
		case XMPP_StreamError_ImproperAddressing:
			name = "host-unknown";
			break;
		case XMPP_StreamError_InternalServerError:
			name = "improper-addressing";
			break;
		case XMPP_StreamError_InvalidFrom:
			name = "invalid-from";
			break;
		case XMPP_StreamError_InvalidId:
			name = "invalid-id";
			break;
		case XMPP_StreamError_InvalidNamespace:
			name = "invalid-namespace";
			break;
		case XMPP_StreamError_InvalidXml:
			name = "invalid-xml";
			break;
		case XMPP_StreamError_NotAuthorized:
			name = "not-authorized";
			break;
		case XMPP_StreamError_PolicyViolation:
			name = "policy-violation";
			break;
		case XMPP_StreamError_RemoteConnectionFailed:
			name = "remote-connection-failed";
			break;
		case XMPP_StreamError_ResourceConstraint:
			name = "resource-constraint";
			break;
		case XMPP_StreamError_RestrictedXml:
			name = "restricted-xml";
			break;
		case XMPP_StreamError_SeeOtherHost:
			name = "see-other-host";
			break;
		case XMPP_StreamError_SystemShutdown:
			name = "system-shutdown";
			break;
		case XMPP_StreamError_UndefinedCondition:
			name = "undefined-condition";
			break;
		case XMPP_StreamError_UnsupportedEncoding:
			name = "unsupported-encoding";
			break;
		case XMPP_StreamError_UnsupportedFeature:
			name = "unsupported-feature";
			break;
		case XMPP_StreamError_UnsupportedStanzaType:
			name = "unsupported-stanza-type";
			break;
		case XMPP_StreamError_UnsupportedVersion:
			name = "unsupported-version";
			break;
		case XMPP_StreamError_XmlNotWellFormed:
			name = "xml-not-well-formed";
			break;
		default:
			devassert(0);
	}

	// Send an error element and the end of the stream element.
	xmlWriterStartElement(&gen->xml, "stream:error");
	xmlWriterStartElement(&gen->xml, name);
	xmlWriterAddAttribute(&gen->xml, "xmlns", "urn:ietf:params:xml:ns:xmpp-streams");
	xmlWriterEndElement(&gen->xml);
	if (text)
	{
		xmlWriterStartElement(&gen->xml, "text");
		xmlWriterAddAttributes(&gen->xml, "xml:lang", "en", "xmlns", "urn:ietf:params:xml:ns:xmpp-streams", NULL);
		xmlWriterCharacters(&gen->xml, text);
		xmlWriterEndElement(&gen->xml);
	}

	{
		char *fnMsg = NULL;
		estrStackCreate(&fnMsg);
		estrPrintf(&fnMsg, "%s_%d_%s", __FUNCTION__, error, text);
		StanzaDoneEx(gen, fnMsg);
		estrDestroy(&fnMsg);
	}
}

// Send an authentication error on a stream, and terminate the stream.
void XMPP_GenerateSaslError(XmppGenerator *gen, enum XMPP_SaslError error, const char *text)
{
	const char *name;

	// Convert error code to element name.
	switch (error)
	{
		case XMPP_SaslError_Aborted:
			name = "aborted";
			break;
		case XMPP_SaslError_AccountDisabled:
			name = "account-disabled";
			break;
		case XMPP_SaslError_CredentialsExpired:
			name = "credentials-expired";
			break;
		case XMPP_SaslError_EncryptionRequired:
			name = "encryption-required";
			break;
		case XMPP_SaslError_IncorrectEncoding:
			name = "incorrect-encoding";
			break;
		case XMPP_SaslError_InvalidAuthzid:
			name = "invalid-authzid";
			break;
		case XMPP_SaslError_InvalidMechanism:
			name = "invalid-mechanism";
			break;
		case XMPP_SaslError_MalformedRequest:
			name = "malformed-request";
			break;
		case XMPP_SaslError_MechanismTooWeak:
			name = "mechanism-too-weak";
			break;
		case XMPP_SaslError_NotAuthorized:
			name = "not-authorized";
			break;
		case XMPP_SaslError_TemporaryAuthFailure:
			name = "temporary-auth-failure";
			break;
		case XMPP_SaslError_TransitionNeeded:
			name = "transition-needed";
			break;
		default:
			devassert(0);
	}

	// Send an error element.
	xmlWriterStartElement(&gen->xml, "failure");
	xmlWriterAddAttribute(&gen->xml, "xmlns", "urn:ietf:params:xml:ns:xmpp-sasl");
	xmlWriterStartElement(&gen->xml, name);
	xmlWriterEndElement(&gen->xml);
	if (text)
	{
		xmlWriterStartElement(&gen->xml, "text");
		xmlWriterAddAttribute(&gen->xml, "xml:lang", "en");
		xmlWriterCharacters(&gen->xml, text);
		xmlWriterEndElement(&gen->xml);
	}
	StanzaDone(gen);
};

// http://jabber.org/protocol/disco#items
void xmpp_s_senddiscoitem(XmppGenerator *gen, const char *id, const char *from, const char *to, XMPP_DiscoItem **ppItems)
{
	int i, size = eaSize(&ppItems);

	xmlWriterStartElement(&gen->xml, "iq");
	xmlWriterAddAttribute(&gen->xml, "type", "result");
	ToFromId(gen, to, from, id);
	xmlWriterStartElement(&gen->xml, "query");
	xmlWriterAddAttribute(&gen->xml, "xmlns", "http://jabber.org/protocol/disco#items");

	for (i = 0; i < size; ++i)
	{
		xmlWriterStartElement(&gen->xml, "item");
		xmlWriterAddAttribute(&gen->xml, "jid", ppItems[i]->jid);
		if (ppItems[i]->name)
			xmlWriterAddAttribute(&gen->xml, "name", ppItems[i]->name);
		xmlWriterEndElement(&gen->xml);
	}

	xmlWriterEndElement(&gen->xml);
	StanzaDone(gen);
}

// http://jabber.org/protocol/disco#info
void xmpp_s_senddiscoinfo(XmppGenerator *gen, const char *id, const char *from, const char *to, const char *const *features,
						  size_t featuresSize, const char *name, enum XMPP_DiscoItemType type)
{
	size_t i;
	char *category;
	char *typeName;

	// Look up type.
	switch (type)
	{
		case XMPP_DiscoItemType_Server:
			category = "server";
			typeName = "im";
			break;
		case XMPP_DiscoItemType_Conference:
			category = "conference";
			typeName = "text";
			break;
		default:
			devassert(0);
	}

	// Send identity and features list.
	xmlWriterStartElement(&gen->xml, "iq");
	xmlWriterAddAttribute(&gen->xml, "type", "result");
	ToFromId(gen, to, from, id);
	xmlWriterStartElement(&gen->xml, "query");
	xmlWriterAddAttribute(&gen->xml, "xmlns", "http://jabber.org/protocol/disco#info");
	xmlWriterStartElement(&gen->xml, "identity");
	xmlWriterAddAttributes(&gen->xml, "category", category, "type", typeName, "name", name, NULL);
	xmlWriterEndElement(&gen->xml);
	for (i = 0; i != featuresSize; ++i)
	{
		xmlWriterStartElement(&gen->xml, "feature");
		xmlWriterAddAttribute(&gen->xml, "var", features[i]);
		xmlWriterEndElement(&gen->xml);
	}
	xmlWriterEndElement(&gen->xml);
	StanzaDone(gen);
}

static void xmpp_s_addoccupantinfo (XmppGenerator *gen, const char *domain, const char *room,
									const char *to, XMPP_ChatOccupant *occupant, bool bUnavailable, 
									INT_EARRAY eaiStatusCodes)
{
	const char *affiliation;
	const char *role;
	char buffer[16];

	switch (occupant->affiliation)
	{
	case XMPP_Affiliation_None:
		affiliation = "none";
		break;
	case XMPP_Affiliation_Member:
		affiliation = "member";
		break;
	case XMPP_Affiliation_Owner:
		affiliation = "owner";
		break;
	case XMPP_Affiliation_Admin:
		affiliation = "admin";
		break;
	case XMPP_Affiliation_Outcast:
		// Don't broadcast presence for an outcast.
		return;
	default:
		devassert(0);
	}
	switch (occupant->role)
	{
	case XMPP_Role_None:
		role = "none";
		break;
	case XMPP_Role_Moderator:
		role = "moderator";
		break;
	case XMPP_Role_Participant:
		role = "participant";
		break;
	case XMPP_Role_Visitor:
		role = "visitor";
		break;
	default:
		devassert(0);
	}

	xmlWriterStartElement(&gen->xml, "presence");
	xmlWriterAddAttributef(&gen->xml, "from", "%s@%s/%s", room, domain, occupant->nick);
	xmlWriterAddAttribute(&gen->xml, "to", to);
	if (bUnavailable)
		xmlWriterAddAttribute(&gen->xml, "type", "unavailable");
	xmlWriterStartElement(&gen->xml, "x");
	xmlWriterAddAttribute(&gen->xml, "xmlns", "http://jabber.org/protocol/muc#user");

	xmlWriterStartElement(&gen->xml, "item");
	xmlWriterAddAttributes(&gen->xml, "affiliation", affiliation, "role", role, NULL);
	xmlWriterEndElement(&gen->xml);

	EARRAY_INT_CONST_FOREACH_BEGIN(eaiStatusCodes, i, n);
	{
		sprintf(buffer, "%d", eaiStatusCodes[i]);
		xmlWriterStartElement(&gen->xml, "status");
		xmlWriterAddAttribute(&gen->xml, "code", buffer);
		xmlWriterEndElement(&gen->xml);
	}
	EARRAY_FOREACH_END;

	xmlWriterEndElement(&gen->xml);
	xmlWriterEndElement(&gen->xml);
}

// Send chat room occupant presences.
void xmpp_s_sendroomoccupants(XmppGenerator *gen, const char *domain, const char *room,
							  const char *to, XMPP_ChatOccupant **eaOccupants, bool rewrite)
{
	XMPP_ChatOccupant *selfOccupant = NULL;
	// Send presence for each occupant.
	EARRAY_CONST_FOREACH_BEGIN(eaOccupants, i, n);
	{
		XMPP_ChatOccupant *occupant = eaOccupants[i];

		if (occupant->own)
			selfOccupant = occupant;
		else
			xmpp_s_addoccupantinfo(gen, domain, room, to, occupant, false, NULL);
	}
	EARRAY_FOREACH_END;

	if (selfOccupant)
	{
		int *eaiStatusCodes = NULL;
		eaiPush(&eaiStatusCodes, XmppStatus_Self);
		if (rewrite)
			eaiPush(&eaiStatusCodes, XmppStatus_Rewrite);
		xmpp_s_addoccupantinfo(gen, domain, room, to, selfOccupant, false, eaiStatusCodes);
		eaiDestroy(&eaiStatusCodes);
	}

	// Flush out-buffer.
	devassertmsgf(xmlWriterGetDepthLevel(&gen->xml) == 1, "XMPP-XML Error - send chatroom");
	xmpp_SendStringToClient(gen->link, xmlWriterGet(&gen->xml));
	xmlWriterClear(&gen->xml);
	++siStanzasGenerated;
}


// Send chat room occupant presences.
void xmpp_s_sendroompresence(XmppGenerator *gen, const char *domain, const char *room,
							  const char *to, XMPP_ChatOccupant *occupant, 
							  bool bLeaving, bool bKicked, bool bSelf)
{
	int *eaiStatusCodes = NULL;
	devassert( !bKicked || bLeaving ); // If bKicked is set, then bLeaving must be as well

	if (bSelf)
	{   // Update refers to self
		eaiPush(&eaiStatusCodes, XmppStatus_Self);
	}
	if (bKicked)
	{   // Kicked status code
		eaiPush(&eaiStatusCodes, XmppStatus_Kicked);
	}

	xmpp_s_addoccupantinfo(gen, domain, room, to, occupant, bLeaving, eaiStatusCodes);
	eaiDestroy(&eaiStatusCodes);

	// Flush out-buffer.
	devassertmsgf(xmlWriterGetDepthLevel(&gen->xml) == 1, "XMPP-XML Error - send chatroom users");
	xmpp_SendStringToClient(gen->link, xmlWriterGet(&gen->xml));
	xmlWriterClear(&gen->xml);
	++siStanzasGenerated;
}

#include "XMPP_Generation_c_ast.c"
