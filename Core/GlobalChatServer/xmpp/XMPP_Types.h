// General XMPP types and values shared among the XMPP components.

#pragma once

#include "AutoGen/XMPP_Types_h_ast.h"

// Stream errors
// Warning: Do not confuse these with stanza errors, defined below.
AUTO_ENUM;
enum XMPP_StreamErrorCondition
{
	XMPP_StreamError_BadFormat,
	XMPP_StreamError_BadNamespacePrefix,
	XMPP_StreamError_Conflict,
	XMPP_StreamError_ConnectionTimeout,
	XMPP_StreamError_HostGone,
	XMPP_StreamError_ImproperAddressing,
	XMPP_StreamError_InternalServerError,
	XMPP_StreamError_InvalidFrom,
	XMPP_StreamError_InvalidId,
	XMPP_StreamError_InvalidNamespace,
	XMPP_StreamError_InvalidXml,
	XMPP_StreamError_NotAuthorized,
	XMPP_StreamError_PolicyViolation,
	XMPP_StreamError_RemoteConnectionFailed,
	XMPP_StreamError_ResourceConstraint,
	XMPP_StreamError_RestrictedXml,
	XMPP_StreamError_SeeOtherHost,
	XMPP_StreamError_SystemShutdown,
	XMPP_StreamError_UndefinedCondition,
	XMPP_StreamError_UnsupportedEncoding,
	XMPP_StreamError_UnsupportedFeature,
	XMPP_StreamError_UnsupportedStanzaType,
	XMPP_StreamError_UnsupportedVersion,
	XMPP_StreamError_XmlNotWellFormed
};

// Stanza error types.
AUTO_ENUM;
typedef enum XMPP_StanzaErrorType {
	Stanza_UndefinedError = 0,
	Stanza_Cancel,
	Stanza_Continue,
	Stanza_Modify,
	Stanza_Auth,
	Stanza_Wait,
} XMPP_StanzaErrorType;

// Stanza error conditions.
AUTO_ENUM;
typedef enum XMPP_StanzaErrorCondition
{
	StanzaError_BadRequest,				//the sender has sent XML that is malformed or that cannot be processed (e.g., an IQ stanza that includes an unrecognized value of the 'type' attribute); the associated error type SHOULD be "modify".
	StanzaError_Conflict,				//access cannot be granted because an existing resource or session exists with the same name or address; the associated error type SHOULD be "cancel".
	StanzaError_FeatureNotImplemented,	//the feature requested is not implemented by the recipient or server and therefore cannot be processed; the associated error type SHOULD be "cancel".
	StanzaError_Forbidden,				//the requesting entity does not possess the required permissions to perform the action; the associated error type SHOULD be "auth".
	StanzaError_Gone,					//the recipient or server can no longer be contacted at this address (the error stanza MAY contain a new address in the XML character data of the <gone/> element); the associated error type SHOULD be "modify".
	StanzaError_InternalServerError,	//the server could not process the stanza because of a misconfiguration or an otherwise-undefined internal server error; the associated error type SHOULD be "wait".
	StanzaError_ItemNotFound,			//the addressed JID or item requested cannot be found; the associated error type SHOULD be "cancel".
	StanzaError_JIDMalformed,			//the sending entity has provided or communicated an XMPP address (e.g., a value of the 'to' attribute) or aspect thereof (e.g., a resource identifier) that does not adhere to the syntax defined in Addressing Scheme; the associated error type SHOULD be "modify".
	StanzaError_NotAcceptable,			//the recipient or server understands the request but is refusing to process it because it does not meet criteria defined by the recipient or server (e.g., a local policy regarding acceptable words in messages); the associated error type SHOULD be "modify".
	StanzaError_NotAllowed,				//the recipient or server does not allow any entity to perform the action; the associated error type SHOULD be "cancel".
	StanzaError_NotAuthorized,			//the sender must provide proper credentials before being allowed to perform the action, or has provided improper credentials; the associated error type SHOULD be "auth".
	StanzaError_PaymentRequired,		//the requesting entity is not authorized to access the requested service because payment is required; the associated error type SHOULD be "auth".
	StanzaError_PolicyViolation,		//local policy violation (3920bis-04)
	StanzaError_RecipientUnavailable,	//the intended recipient is temporarily unavailable; the associated error type SHOULD be "wait" (note: an application MUST NOT return this error if doing so would provide information about the intended recipient's network availability to an entity that is not authorized to know such information).
	StanzaError_Redirect,				//the recipient or server is redirecting requests for this information to another entity, usually temporarily (the error stanza SHOULD contain the alternate address, which MUST be a valid JID, in the XML character data of the <redirect/> element); the associated error type SHOULD be "modify".
	StanzaError_RegistrationRequired,	//the requesting entity is not authorized to access the requested service because registration is required; the associated error type SHOULD be "auth".
	StanzaError_RemoteServerNotFound,	//a remote server or service specified as part or all of the JID of the intended recipient does not exist; the associated error type SHOULD be "cancel".
	StanzaError_RemoteServerTimeout,	//a remote server or service specified as part or all of the JID of the intended recipient (or required to fulfill a request) could not be contacted within a reasonable amount of time; the associated error type SHOULD be "wait".
	StanzaError_ResourceConstraint,		//the server or recipient lacks the system resources necessary to service the request; the associated error type SHOULD be "wait".
	StanzaError_ServiceUnavailable,		//the server or recipient does not currently provide the requested service; the associated error type SHOULD be "cancel".
	StanzaError_SubscriptionRequired,	//the requesting entity is not authorized to access the requested service because a subscription is required; the associated error type SHOULD be "auth".
	StanzaError_UndefinedCondition,		//the error condition is not one of those defined by the other conditions in this list; any error type may be associated with this condition, and it SHOULD be used only in conjunction with an application-specific condition.
	StanzaError_UnexpectedRequest,		//the recipient or server understood the request but was not expecting it at this time (e.g., the request was out of order); the associated error type SHOULD be "wait".
} XMPP_StanzaErrorCondition;

// SASL authentication errors
AUTO_ENUM;
typedef enum XMPP_SaslError
{
	XMPP_SaslError_Aborted,
	XMPP_SaslError_AccountDisabled,
	XMPP_SaslError_CredentialsExpired,
	XMPP_SaslError_EncryptionRequired,
	XMPP_SaslError_IncorrectEncoding,
	XMPP_SaslError_InvalidAuthzid,
	XMPP_SaslError_InvalidMechanism,
	XMPP_SaslError_MalformedRequest,
	XMPP_SaslError_MechanismTooWeak,
	XMPP_SaslError_NotAuthorized,
	XMPP_SaslError_TemporaryAuthFailure,
	XMPP_SaslError_TransitionNeeded
} XMPP_SaslError;

// An XMPP priority integer, from -128 to 127.
typedef signed char XMPP_Priority;

// Message types
AUTO_ENUM;
typedef enum XMPP_MessageType
{
	XMPP_MessageType_Normal = 0,
	XMPP_MessageType_Chat,
	XMPP_MessageType_Error,
	XMPP_MessageType_Groupchat,
	XMPP_MessageType_Headline
} XMPP_MessageType;

// Availability types
// These correspond to the defined values of the <show> member of <presence>, with the
// exception of XMPP_PresenceAvailability_Unavailable.
AUTO_ENUM;
typedef enum XMPP_PresenceAvailability
{
	XMPP_PresenceAvailability_Normal = 0,							// In stanzas, the absence of a <show>, meaning just 'available'
	XMPP_PresenceAvailability_Away,
	XMPP_PresenceAvailability_Chat,
	XMPP_PresenceAvailability_Dnd,
	XMPP_PresenceAvailability_Xa,
	XMPP_PresenceAvailability_Unavailable							// In stanzas, when the type attribute is "unavailable"
} XMPP_PresenceAvailability;

// XMPP client login state
AUTO_ENUM;
typedef enum XMPP_LoginState
{
	XMPP_LoginState_Connected = 1,									// Just connected; if tlsSession is set, after starttls.  Next step is starttls or auth.
	XMPP_LoginState_AuthWait,										// Waiting on authentication success or failure
	XMPP_LoginState_Authenticated,									// Successfully authenticated, need to bind to a resource
	XMPP_LoginState_LoggedIn										// Successfully bound to a resource, fully logged-in
} XMPP_LoginState;

// Presence relationship with a roster contact
// Note that this is only for current subscriptions, not outstanding requests.
AUTO_ENUM;
typedef enum XMPP_RosterSubscriptionState
{
	XMPP_RosterSubscriptionState_None = 0,							// No presence relationship
	XMPP_RosterSubscriptionState_To,								// Subscribed to contact's presence; will receive updates (not implemented)
	XMPP_RosterSubscriptionState_From,								// Contact subscribed to this user's presence; updates will be sent (not implemented)
	XMPP_RosterSubscriptionState_Both								// Bidrectional presence subscription (friends)
} XMPP_RosterSubscriptionState;

// Outstanding roster request-related presence for a contact.
// Note that this is only for requests, not subscriptions themselves.
AUTO_ENUM;
typedef enum XMPP_RosterSubRequestState
{
	XMPP_RosterSubscribeState_None = 0,								// In stanzas, the absence of a subscribe attribute
	XMPP_RosterSubscribeState_Subscribe,							// Sent presence subscribe request to this roster item
	XMPP_RosterSubscribeState_Subscribed							// Preauthorized presence subscription authorization (not implemented)
} XMPP_RosterSubRequestState;

// An item in the roster
AUTO_STRUCT;
typedef struct XMPP_RosterItem {
	char *jid;									AST(ESTRING)		// JID of contact
	char *name;														// Listed name of contact
	char **group;								AST(ESTRING)		// Group that contact is in
	XMPP_RosterSubscriptionState subscription;						// Presence relationship to this contact
	XMPP_RosterSubRequestState ask;									// Subscription request state
} XMPP_RosterItem;

// Room affilation
AUTO_ENUM;
typedef enum XMPP_Affiliation
{
	XMPP_Affiliation_None = 0,
	XMPP_Affiliation_Owner,
	XMPP_Affiliation_Admin,
	XMPP_Affiliation_Member,
	XMPP_Affiliation_Outcast
} XMPP_Affiliation;

// Room role
AUTO_ENUM;
typedef enum XMPP_Role
{
	XMPP_Role_None = 0,
	XMPP_Role_Moderator,
	XMPP_Role_Participant,
	XMPP_Role_Visitor
} XMPP_Role;

// An item in the roster
AUTO_STRUCT;
typedef struct XMPP_ChatOccupant {
	char *nick;									AST(ESTRING)		// Room nick of occupant
	XMPP_Affiliation affiliation;									// Occupant room affilation
	XMPP_Role role;													// Occupant room role
	bool own;														// True if this occupant should be marked as the user's own occupant.
} XMPP_ChatOccupant;
