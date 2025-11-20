// XMPP types and values specific for XMPP server - Stream and Sasl Error types

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