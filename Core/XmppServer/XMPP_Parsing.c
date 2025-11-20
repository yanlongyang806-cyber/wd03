// Parse XML data coming from clients into meaningful XMPP.
// It passes the data off to XMPP_Gateway for processing.

#include <errno.h>
#include <string.h>

#include "AutoGen/XMPP_Parsing_c_ast.h"
#include "crypt.h"
#include "error.h"
#include "EString.h"
#include "StashTable.h"
#include "StringUtil.h"
#include "XMPP_Gateway.h"
#include "XMPP_Parsing.h"
#include "ChatServer/xmppShared.h"
#include "AutoGen/xmppShared_h_ast.h"
#include "AutoGen/xmppTypes_h_ast.h"

// Namespace delimiter.
static const char xmppNamespaceDelimiter = '\b';

// Namespaces
#define XMPP_SASL_NS "urn:ietf:params:xml:ns:xmpp-sasl"
#define XMPP_TLS_NS "urn:ietf:params:xml:ns:xmpp-tls"
#define XMPP_BIND_NS "urn:ietf:params:xml:ns:xmpp-bind"
#define XMPP_SESSION_NS "urn:ietf:params:xml:ns:xmpp-session"
#define XMPP_CLIENT_NS "jabber:client"
#define XMPP_ROSTER_NS "jabber:iq:roster"
#define XMPP_ROSTER_PING "urn:xmpp:ping"
#define XMPP_DISCO_INFO_NS "http://jabber.org/protocol/disco#info"
#define XMPP_DISCO_ITEMS_NS "http://jabber.org/protocol/disco#items"

// Message types
// These correspond to the defined values of the 'type' attribute of <presence>.
AUTO_ENUM;
typedef enum XMPP_PresenceType
{
	XMPP_PresenceType_Update = 0,				// In stanzas, when no "type" attribute exists.
	XMPP_PresenceType_Unavailable,
	XMPP_PresenceType_Subscribe,
	XMPP_PresenceType_Subscribed,
	XMPP_PresenceType_Unsubscribe,
	XMPP_PresenceType_Unsubscribed,
	XMPP_PresenceType_Probe,
	XMPP_PresenceType_Error
} XMPP_PresenceType;

// Element handler callbacks
struct XMPP_Element;
typedef bool (*xmpp_CreateElement)(XMPP_Parser *xp, SA_PARAM_NN_STR const char *ns, const char *name, const XML_Char **attr, void *userData);
typedef bool (*xmpp_DestroyElement)(XMPP_Parser *xp, SA_PARAM_NN_STR const char *ns, const char *name, char *text, void *userData);
typedef bool (*xmpp_StartChild)(XMPP_Parser *xp, SA_PARAM_NN_STR const char *ns, const char *name, const XML_Char **attr, struct XMPP_Element *element,
								void *userData);
typedef bool (*xmpp_EndChild)(XMPP_Parser *xp, SA_PARAM_NN_STR const char *ns, const char *name, bool ignore, void *userData);

// Element context
struct XMPP_Element
{
	xmpp_CreateElement create;			// Called when the element is started
	xmpp_DestroyElement destroy;		// Called when the element is ended
	xmpp_StartChild startChild;			// Called when a child element is created
	xmpp_EndChild endChild;				// Called when a child element is destroyed
	char *text;							// Element text
	void *userData;						// Element handler data
	bool ignore;						// If true, do not continue to call callbacks for this element.
	bool recording;						// If true, this element is recording.
};

// XMPP parser state
AUTO_STRUCT;
typedef struct XMPP_Parser
{

	// Parameters
	bool plaintextPermitted;						// True if this stream is allowed to authenticate in plaintext
	bool secure;									// True if the parser is on a secure link
	void *userData;					NO_AST			// Opaque pointer to a callback parameter provided by the parser creator.

	// Parser state
	XML_Parser p;					NO_AST			// XML parser
	const char *encoding;							// The XML encoding to use
	bool processing;								// true if this parser is currently being run
	bool parseError;								// true if an error has occurred, and no further parsing will be done

	// Stream information
	XMPP_LoginState loginState;						// Current state of authentication and login

	// Element stack
	struct XMPP_Element **elements;	NO_AST

	// Recording
	XML_Index recordPos;			AST(INT)		// Stream position immediately after last character recorded
	char *recordString;				AST(ESTRING)	// Characters recorded since recording was turned on
	const char *recordSource;		AST(UNOWNED)	// Source buffer for recording
	int recordLen;									// Size of recordSource
	XML_Index recordStart;			AST(INT)		// XML source position of the beginning of recordSource

	// Deferred parser actions
	bool restart;									// true if the parser needs to be restarted
	bool destroy;									// true if the parser should be destroyed

	// Resource usage
	size_t memory;
	StashTable allocations;			NO_AST
} XMPP_Parser;

/************************************************************************/
/* Variables                                                            */
/************************************************************************/

// expat parse memory allocator
static XML_Memory_Handling_Suite memorySuite;

// Maximum amount of memory a client is allowed to consume through XML parsing.
static const size_t maxMemoryUse = 1024*1024;  // One megabyte.

// Pointer to the memory tracker for the currently-processing client.
// Warning: This code assumes that the parser is single-threaded and non-reentrant with respect to allocator calls.
static size_t *xmppClientMemoryTracker;

// Allocation stash for the currently-processing client.
// Warning: This code assumes that the parser is single-threaded and non-reentrant with respect to allocator calls.
static StashTable xmppClientMemoryAllocations;

// This is set to true if the client exceeds the amount of allowable memory usage.
static bool xmppClientMemoryExhausted = false;

/************************************************************************/
/* XMPP parsing support functions                                       */
/************************************************************************/

// Return true XML considers this character to be whitespace.
static bool IsXmlWhitespace(const char *c)
{
	return *c == '\r' || *c == '\n' || *c == '\t' || *c == ' ';
}

// Allocate a new string that is the same as the string passed in except without leading or trailing XML whitespace.
static void estrTrimXmlWhitespace(char **estr, const char *str)
{
	const char *begin = str;
	const char *i;
	const char *lastNonwhitespaceNext;

	// Clear output string.
	estrClear(estr);

	// Validate.
	devassert(str);
	if (!str)
		return;

	// Skip leading whitespace.
	while (*begin && IsXmlWhitespace(begin))
		begin = UTF8GetNextCodepoint(begin);
	if (!*begin)
		return;

	// Reserve estimate of output string length.
	estrReserveCapacity(estr, (int)strlen(str));

	// Copy the string, keeping track of trailing whitespace.
	lastNonwhitespaceNext = begin;
	for (i = begin; *i; i = UTF8GetNextCodepoint(i))
	{
		char *next = UTF8GetNextCodepoint(i);
		estrConcat(estr, i, next - i);
		if (!IsXmlWhitespace(i))
			lastNonwhitespaceNext = next;
	}

	// Trim trailing whitespace.
	estrSetSize(estr, lastNonwhitespaceNext - begin);
}

//returns a copy of the tag string, sets ns to point to the namespace within the buffer returned or NULL if no namespace found.
static char * xmpp_copytag(const XML_Char *el, char **nsout)
{
	char *tag = NULL;
	char *del;

	// Validate.
	devassert(el && el[0]);
	if (!el || !el[0])
	{
		*nsout = "";
		return strdup("");
	}
	if(nsout)
		*nsout = "";

	// Split tag and namespace.
	tag = strdup(el);
	del = strchr(tag, xmppNamespaceDelimiter);
	if (del && del > tag)
	{
		size_t ns_size = (del++ - tag);		//doesn't include terminating \0
		size_t tag_size = strlen(del)+1;	//includes terminating \0
		memcpy_fast(tag, del, tag_size);
		if (nsout)
		{
			*nsout = tag+tag_size;
			memcpy_fast(*nsout, el, ns_size);
			(*nsout)[ns_size] = '\0';
		}
	}
	return tag;
}

// Compare an expat-generated combined attribute with a tag and a namespace for equality.
static bool xmpp_attribEqual(const char *combinedAttrib, const char *tag, const char *ns)
{
	char *tagLhs;
	char *nsLhs;
	bool equal = false;

	devassert(ns);

	// Separate attribute.
	tagLhs = xmpp_copytag(combinedAttrib, &nsLhs);

	// Check for equality.
	if (tagLhs)
	{
		ANALYSIS_ASSUME(tagLhs);
		if (!strcmp(tagLhs, tag))
		{
			if (!nsLhs)
			{
				equal = true;
			}
			else
			{
				ANALYSIS_ASSUME(nsLhs);
				equal = strcmp(nsLhs, ns) != 0;
			}
		}
		free(tagLhs);
	}

	// Return result.
	return equal;
}

// Begin recording raw XML.
static void xmpp_StartRecord(XMPP_Parser *xp)
{
	struct XMPP_Element *top;
	devassert(!xp->recordPos);
	devassert(eaSize(&xp->elements));
	estrClear(&xp->recordString);
	xp->recordPos = XML_GetCurrentByteIndex(xp->p);

	top = eaTail(&xp->elements);
	devassert(top);
	devassert(!top->recording);
	top->recording = true;
}

// Stop recording raw XML.
static char *xmpp_StopRecord(XMPP_Parser *xp)
{
	XML_Index position = XML_GetCurrentByteIndex(xp->p);
	struct XMPP_Element *top;
	devassert(xp->recordPos >= xp->recordStart);
	devassert(position >= xp->recordPos);
	devassert(position - (xp->recordPos - xp->recordStart) < xp->recordLen);
	devassert(top->recording);
	estrConcat(&xp->recordString, xp->recordSource + (xp->recordPos - xp->recordStart), position - xp->recordPos);

	top = eaTail(&xp->elements);
	top->recording = false;
	xp->recordPos = 0;
}

// Stanza error information.
AUTO_STRUCT;
typedef struct XmppStanzaError
{
	XMPP_StanzaErrorCondition error;
	XMPP_StanzaErrorType type;
	char *text;
} XmppStanzaError;

// StructCreate a XmppStanzaError.
static XmppStanzaError *CreateXmppStanzaError(XMPP_StanzaErrorCondition error, XMPP_StanzaErrorType type, const char *text)
{
	XmppStanzaError *result = StructCreate(parse_XmppStanzaError);
	result->error = error;
	result->type = type;
	result->text = strdup(text);
	return result;
}

// Common data associated with most stanzas.
AUTO_STRUCT;
typedef struct ElementCommonData
{
	char *to;									// Destination of the stanza
	char *from;									// Source of the stanza
	char *id;									// Stanza unique identifier
	// Note that 'type' is stanza-specific.
	char *lang;									// Language of the stanza, if any
} ElementCommonData;

// Get common stanza attributes.
static ElementCommonData *GetCommonAttributes(const XML_Char **attr)
{
	ElementCommonData *result = StructCreate(parse_ElementCommonData);
	const char **i;

	// Search for recognized common attributes.
	for (i = attr; i[0]; i += 2)
	{
		if (xmpp_attribEqual(i[0], "to", XMPP_CLIENT_NS))
			result->to = strdup(i[1]);
		else if (xmpp_attribEqual(i[0], "from", XMPP_CLIENT_NS))
			result->from = strdup(i[1]);
		else if (xmpp_attribEqual(i[0], "id", XMPP_CLIENT_NS))
			result->id = strdup(i[1]);
		else if (xmpp_attribEqual(i[0], "xml:lang", ""))
			result->lang = strdup(i[1]);
		// 'type' is recognized by the stanza constructor
	}

	return result;
}

/************************************************************************/
/* <iq>                                                                 */
/************************************************************************/

static bool xmpp_StartBindChild(XMPP_Parser *xp, const char *ns, const char *name, const XML_Char **attr, struct XMPP_Element *element,
								void *userData);
static bool xmpp_CreateItem(XMPP_Parser *xp, const char *ns, const char *name, const XML_Char **attr, void *userData);
static bool xmpp_StartItemChild(XMPP_Parser *xp, const char *ns, const char *name, const XML_Char **attr, struct XMPP_Element *element,
								void *userData);
static bool xmpp_StartIqSetQueryChild(XMPP_Parser *xp, const char *ns, const char *name, const XML_Char **attr, struct XMPP_Element *element,
									  void *userData);

// IQ types
AUTO_ENUM;
typedef enum IqType
{
	IqType_Error,
	IqType_Get,
	IqType_Result,
	IqType_Set
} IqType;

// IQ types
AUTO_ENUM;
typedef enum IqKind
{
	IqKind_Unknown = 0,
	IqKind_Bind,
	IqKind_Session,
	IqKind_RosterGet,
	IqKind_RosterSet,
	IqKind_Ping,
	IqKind_Disco_Items,
	IqKind_Disco_Info
} IqKind;

// Data associated with the <iq> element.
AUTO_STRUCT;
typedef struct ElementIqData
{

	// Common
	ElementCommonData *common;

	// Classification
	IqType type;				// General type of iq that this is
	int kind;					// Kind of iq that this is, XMPP_NONE if not yet differentiated.
	XmppStanzaError *error;		// Error information if one has occurred, otherwise null.

	// <bind>
	char *resource;

	// <query><item>
	char *itemJid;
	char *itemName;
	bool itemRemove;
	STRING_EARRAY itemGroups;
} ElementIqData;

// Start <iq>.
static bool xmpp_CreateIq(XMPP_Parser *xp, const char *ns, const char *name, const XML_Char **attr, void *userData)
{
	struct ElementIqData *data = userData;
	const char **i;
	bool hasType = false;

	// Process attributes.
	devassert(data);
	data->common = GetCommonAttributes(attr);
	for (i = attr; i[0]; i += 2)
	{
		if (xmpp_attribEqual(i[0], "type", ns))
		{
			hasType = true;
			if (!strcmp(i[1], "error"))
				data->type = IqType_Error;
			else if (!strcmp(i[1], "get"))
				data->type = IqType_Get;
			else if (!strcmp(i[1], "result"))
				data->type = IqType_Result;
			else if (!strcmp(i[1], "set"))
				data->type = IqType_Set;
			else
				hasType = false;
		}
	}

	// Validate.
	if (!data->common->id || !hasType)
		data->error = CreateXmppStanzaError(StanzaError_BadRequest, Stanza_Modify, NULL);

	return true;
}

#define XMPP_IQCHECK_TO(state, to, element, id) if (!to || !(*to))\
{\
	XMPP_HandleStanzaError(state, StanzaError_BadRequest, Stanza_Cancel, element, id, NULL);\
	break;\
}

// End <iq>.
static bool xmpp_DestroyIq(XMPP_Parser *xp, const char *ns, const char *name, char *text, void *userData)
{
	struct ElementIqData *data = userData;
	bool result = true;

	// If an error occurred, process the error condition and return.
	if (data->error)
	{
		XMPP_HandleStanzaError(xp->userData, data->error->error, data->error->type, name, data->common->id, data->error->text);
		StructDestroy(parse_ElementIqData, data);
		return false;
	}

	// Process the completed iq stanza.
	switch (data->kind)
	{

		// Unknown: no children, or unrecognized children
		// If its some sort of request, send an error.  Otherwise, just ignore it.
		case IqKind_Unknown:
			if (data->type == IqType_Get || data->type == IqType_Set)
				XMPP_HandleStanzaError(xp->userData, StanzaError_BadRequest, Stanza_Modify, "iq", data->common->id, NULL);
			break;

		// <bind>
		case IqKind_Bind:
			result = XMPP_ProcessBind(xp->userData, data->common->id, data->resource);
			//if (result)
			//	xp->loginState = XMPP_LoginState_LoggedIn;
			break;

		// <session>
		case IqKind_Session:
			XMPP_ProcessSession(xp->userData, data->common->id);
			break;

		// get:<query>
		case IqKind_RosterGet:
			XMPP_ProcessRosterGet(xp->userData, data->common->id, data->common->to, data->common->from);
			break;

		// set:<query>
		case IqKind_RosterSet:
			if (!data->itemJid)
			{
				XMPP_HandleStanzaError(xp->userData, StanzaError_BadRequest, Stanza_Modify, name, data->common->id, NULL);
				result = false;
			}
			else if (data->itemRemove)
				XMPP_ProcessRosterRemove(xp->userData, data->common->id, data->common->to, data->common->from, data->itemJid);
			else
				XMPP_ProcessRosterUpdate(xp->userData, data->common->id, data->common->to, data->common->from, data->itemJid,
					data->itemName, data->itemGroups);
			break;

		// <ping>
		case IqKind_Ping:
			XMPP_ProcessPing(xp->userData, data->common->id);
			break;

		// disco#info
		case IqKind_Disco_Info:
			XMPP_IQCHECK_TO(xp->userData, data->common->to, name, data->common->id);
			XMPP_ProcessDiscoInfo(xp->userData, data->common->to, data->common->from, data->common->id);
			break;

		// disco#items
		case IqKind_Disco_Items:
			XMPP_IQCHECK_TO(xp->userData, data->common->to, name, data->common->id);
			XMPP_ProcessDiscoItems(xp->userData, data->common->to, data->common->from, data->common->id);
			break;

		default:
			devassert(0);
	}

	StructDestroy(parse_ElementIqData, data);

	return true;
}

// <iq> subelement started
static bool xmpp_StartIqChild(XMPP_Parser *xp, const char *ns, const char *name, const XML_Char **attr, struct XMPP_Element *element,
	void *userData)
{
	struct ElementIqData *data = userData;

	// If the iq is already differentiated, this is invalid.
	if (data->kind)
	{
		data->error = CreateXmppStanzaError(StanzaError_BadRequest, Stanza_Modify, NULL);
		return true;
	}

	// Not logged in: Allow only resource binding.
	if (xp->loginState != XMPP_LoginState_LoggedIn)
	{
		if (data->type == IqType_Set && !strcmp(name, "bind") && !strcmp(ns, XMPP_BIND_NS))
		{
			data->kind = IqKind_Bind;
			element->startChild = xmpp_StartBindChild;
			element->userData = userData;
			return true;
		}
		else
		{
			data->error = CreateXmppStanzaError(StanzaError_NotAuthorized, Stanza_Auth, NULL);
			return true;
		}
	}

	// Process errors
	if (data->type == IqType_Error)
	{
		// Just ignore errors.
		return false;
	}

	// Process get requests.
	else if (data->type == IqType_Get)
	{
		if (!strcmp(name, "query") && !strcmp(ns, XMPP_ROSTER_NS))
			data->kind = IqKind_RosterGet;
		else if (!strcmp(name, "ping") && !strcmp(ns, XMPP_ROSTER_PING))
			data->kind = IqKind_Ping;
		else if (!strcmp(name, "query") && !strcmp(ns, XMPP_DISCO_INFO_NS))
			data->kind = IqKind_Disco_Info;
		else if (!strcmp(name, "query") && !strcmp(ns, XMPP_DISCO_ITEMS_NS))
			data->kind = IqKind_Disco_Items;
		else
		{
			data->error = CreateXmppStanzaError(StanzaError_FeatureNotImplemented, Stanza_Cancel, NULL);
			return true;
		}
	}

	// Process request results.
	else if (data->type == IqType_Result)
	{
		// No response is necessary.
	}

	// Process set requests.
	else if (data->type == IqType_Set)
	{
		if (!strcmp(name, "session") && !strcmp(ns, XMPP_SESSION_NS))
		{
			data->kind = IqKind_Session;
			return true;
		}
		if (!strcmp(name, "query") && !strcmp(ns, XMPP_ROSTER_NS))
		{
			data->kind = IqKind_RosterSet;
			element->startChild = xmpp_StartIqSetQueryChild;
			element->userData = userData;
			return true;
		}
		else
		{
			data->error = CreateXmppStanzaError(StanzaError_FeatureNotImplemented, Stanza_Cancel, NULL);
			return true;
		}
	}
	else
		devassert(0);

	return true;
}

/************************************************************************/
/* <resource>                                                           */
/************************************************************************/

// End <resource>.
static bool xmpp_DestroyResource(XMPP_Parser *xp, const char *ns, const char *name, char *text, void *userData)
{
	struct ElementIqData *data = userData;
	if (data->resource || !text || !text[0])
	{
		data->error = CreateXmppStanzaError(StanzaError_BadRequest, Stanza_Modify, NULL);
		return false;
	}
	data->resource = strdup(text);
	return true;
}

/************************************************************************/
/* <bind>                                                               */
/************************************************************************/

// <bind> subelement started
static bool xmpp_StartBindChild(XMPP_Parser *xp, const char *ns, const char *name, const XML_Char **attr, struct XMPP_Element *element,
	void *userData)
{
	if (!strcmp(name, "resource") && !strcmp(ns, XMPP_BIND_NS))
	{
		element->destroy = xmpp_DestroyResource;
		element->userData = userData;
	}
	return true;
}

/************************************************************************/
/* <group>                                                           */
/************************************************************************/

// End <group>.
static bool xmpp_DestroyGroup(XMPP_Parser *xp, const char *ns, const char *name, char *text, void *userData)
{
	struct ElementIqData *data = userData;
	if (!text || !text[0])
	{
		data->error = CreateXmppStanzaError(StanzaError_BadRequest, Stanza_Modify, NULL);
		return false;
	}
	eaPush(&data->itemGroups, strdup(text));
	return true;
}

/************************************************************************/
/* <item>                                                               */
/************************************************************************/

// Start <item>.
static bool xmpp_CreateItem(XMPP_Parser *xp, const char *ns, const char *name, const XML_Char **attr, void *userData)
{
	struct ElementIqData *data = userData;
	const char **i;

	// Only one <item> allowed.
	if (data->itemJid)
	{
		data->error = CreateXmppStanzaError(StanzaError_BadRequest, Stanza_Modify, NULL);
		return false;
	}

	// Collect attributes.
	for (i = attr; i[0]; i += 2)
	{
		if (xmpp_attribEqual(i[0], "jid", ns))
			data->itemJid = strdup(i[1]);
		else if (xmpp_attribEqual(i[0], "name", ns))
			data->itemName = strdup(i[1]);
		else if (xmpp_attribEqual(i[0], "subscription", ns))
		{
			if (!strcmp(i[1], "remove"))
				data->itemRemove = true;
		}
	}

	// Validate.
	if (!data->itemJid)
	{
		data->error = CreateXmppStanzaError(StanzaError_BadRequest, Stanza_Modify, NULL);
		return false;
	}

	return true;
}

// <item> subelement started
static bool xmpp_StartItemChild(XMPP_Parser *xp, const char *ns, const char *name, const XML_Char **attr, struct XMPP_Element *element,
								void *userData)
{
	if (!strcmp(name, "group") && !strcmp(ns, XMPP_ROSTER_NS))
	{
		element->destroy = xmpp_DestroyGroup;
		element->userData = userData;
	}
	return true;
}

/************************************************************************/
/* set:<query>                                                          */
/************************************************************************/

// <query> subelement started
static bool xmpp_StartIqSetQueryChild(XMPP_Parser *xp, const char *ns, const char *name, const XML_Char **attr, struct XMPP_Element *element,
								void *userData)
{
	if (!strcmp(name, "item") && !strcmp(ns, XMPP_ROSTER_NS))
	{
		element->create = xmpp_CreateItem;
		element->startChild = xmpp_StartItemChild;
		element->userData = userData;
	}
	return true;
}

/************************************************************************/
/* <message>                                                            */
/************************************************************************/

static bool xmpp_DestroyBody(XMPP_Parser *xp, const char *ns, const char *name, char *text, void *userData);
static bool xmpp_DestroySubject(XMPP_Parser *xp, const char *ns, const char *name, char *text, void *userData);
static bool xmpp_DestroyThread(XMPP_Parser *xp, const char *ns, const char *name, char *text, void *userData);

// Data associated with the <iq> element.
AUTO_STRUCT;
typedef struct ElementMessageData
{

	// Common
	ElementCommonData *common;

	// Classification
	XMPP_MessageType type;			// General type of message that this is
	XmppStanzaError *error;			// Error information if one has occurred, otherwise null.

	// Message information
	char *body;						// Body of message
	char *subject;					// Message subject, if any
	char *thread;					// Message thread identifier, if any
} ElementMessageData;

// Start <message>.
static bool xmpp_CreateMessage(XMPP_Parser *xp, const char *ns, const char *name, const XML_Char **attr, void *userData)
{
	struct ElementMessageData *data = userData;
	const char **i;
	bool hasType = false;

	devassert(xp->loginState == XMPP_LoginState_LoggedIn);
	devassert(data);

	// Process attributes.
	data->common = GetCommonAttributes(attr);
	for (i = attr; i[0]; i += 2)
	{
		if (xmpp_attribEqual(i[0], "type", ns))
		{
			devassert(!hasType);
			hasType = true;
			if (!strcmp(i[1], "chat"))
				data->type = XMPP_MessageType_Chat;
			else if (!strcmp(i[1], "error"))
				data->type = XMPP_MessageType_Error;
			else if (!strcmp(i[1], "groupchat"))
				data->type = XMPP_MessageType_Groupchat;
			else if (!strcmp(i[1], "headline"))
				data->type = XMPP_MessageType_Headline;
			else if (!strcmp(i[1], "normal"))
				data->type = XMPP_MessageType_Normal;
			else
				hasType = false;
		}
	}

	// Validate.
	if (!data->common->id || !data->common->to)
		data->error = CreateXmppStanzaError(StanzaError_BadRequest, Stanza_Modify, NULL);

	return true;
}

// End <message>.
static bool xmpp_DestroyMessage(XMPP_Parser *xp, const char *ns, const char *name, char *text, void *userData)
{
	struct ElementMessageData *data = userData;

	// If an error occurred, process the error condition and return.
	if (data->error)
	{
		XMPP_HandleStanzaError(xp->userData, data->error->error, data->error->type, name, data->common->id, data->error->text);
		return false;
	}

	// Process the message.
	XMPP_ProcessMessage(xp->userData, data->common->to, data->common->from, data->common->id, data->body, data->subject,
		data->thread, data->type);

	StructDestroy(parse_ElementMessageData, data);

	return true;
}

// <message> subelement started
static bool xmpp_StartMessageChild(XMPP_Parser *xp, const char *ns, const char *name, const XML_Char **attr, struct XMPP_Element *element,
	void *userData)
{
	struct ElementMessageData *data = userData;

	if (!strcmp(name, "body") && !strcmp(ns, XMPP_CLIENT_NS))
	{
		element->destroy = xmpp_DestroyBody;
		element->userData = userData;
	}
	else if (!strcmp(name, "subject") && !strcmp(ns, XMPP_CLIENT_NS))
	{
		element->destroy = xmpp_DestroySubject;
		element->userData = userData;
	}
	else if (!strcmp(name, "thread") && !strcmp(ns, XMPP_CLIENT_NS))
	{
		element->destroy = xmpp_DestroyThread;
		element->userData = userData;
	}

	return true;
}

/************************************************************************/
/* <body>                                                               */
/************************************************************************/

// End <body>.
static bool xmpp_DestroyBody(XMPP_Parser *xp, const char *ns, const char *name, char *text, void *userData)
{
	struct ElementMessageData *data = userData;
	if (data->body)
	{
		data->error = CreateXmppStanzaError(StanzaError_BadRequest, Stanza_Modify, NULL);
		return false;
	}
	data->body = text ? strdup(text) : strdup("");
	return true;
}

/************************************************************************/
/* <subject>                                                            */
/************************************************************************/

// End <subject>.
static bool xmpp_DestroySubject(XMPP_Parser *xp, const char *ns, const char *name, char *text, void *userData)
{
	struct ElementMessageData *data = userData;
	if (data->subject)
	{
		data->error = CreateXmppStanzaError(StanzaError_BadRequest, Stanza_Modify, NULL);
		return false;
	}
	data->subject = text ? strdup(text) : NULL;
	return true;
}

/************************************************************************/
/* <thread>                                                             */
/************************************************************************/

// End <thread>.
static bool xmpp_DestroyThread(XMPP_Parser *xp, const char *ns, const char *name, char *text, void *userData)
{
	struct ElementMessageData *data = userData;
	if (data->thread)
	{
		data->error = CreateXmppStanzaError(StanzaError_BadRequest, Stanza_Modify, NULL);
		return false;
	}
	data->thread = text ? strdup(text) : NULL;
	return true;
}

/************************************************************************/
/* <presence>                                                           */
/************************************************************************/

static bool xmpp_DestroyShow(XMPP_Parser *xp, const char *ns, const char *name, char *text, void *userData);
static bool xmpp_DestroyStatus(XMPP_Parser *xp, const char *ns, const char *name, char *text, void *userData);
static bool xmpp_DestroyPriority(XMPP_Parser *xp, const char *ns, const char *name, char *text, void *userData);

// Data associated with the <presence> element.
AUTO_STRUCT;
typedef struct ElementPresenceData
{

	// Common
	ElementCommonData *common;

	// Classification
	XMPP_PresenceType type;						// Type of presence stanza
	XmppStanzaError *error;						// Error information if one has occurred, otherwise null.

	// Presence information
	XMPP_PresenceAvailability availability;		// Availability category
	char *status;								// Human-readable status message
	XMPP_Priority priority;		AST(INT)		// Resource priority
} ElementPresenceData;

// Start <presence>.
static bool xmpp_CreatePresence(XMPP_Parser *xp, const char *ns, const char *name, const XML_Char **attr, void *userData)
{
	struct ElementPresenceData *data = userData;
	const char **i;
	bool hasType = false;

	devassert(xp->loginState == XMPP_LoginState_LoggedIn);

	// Process attributes.
	data->common = GetCommonAttributes(attr);
	for (i = attr; i[0]; i += 2)
	{
		if (xmpp_attribEqual(i[0], "type", ns))
		{
			hasType = true;
			if (!strcmp(i[1], "unavailable"))
				data->type = XMPP_PresenceType_Unavailable;
			else if (!strcmp(i[1], "subscribe"))
				data->type = XMPP_PresenceType_Subscribe;
			else if (!strcmp(i[1], "subscribed"))
				data->type = XMPP_PresenceType_Subscribed;
			else if (!strcmp(i[1], "unsubscribe"))
				data->type = XMPP_PresenceType_Unsubscribe;
			else if (!strcmp(i[1], "unsubscribed"))
				data->type = XMPP_PresenceType_Unsubscribed;
			else if (!strcmp(i[1], "probe"))
				data->type = XMPP_PresenceType_Probe;
			else if (!strcmp(i[1], "error"))
				data->type = XMPP_PresenceType_Error;
			else
				hasType = false;
		}
	}

	return true;
}

// End <presence>.
static bool xmpp_DestroyPresence(XMPP_Parser *xp, const char *ns, const char *name, char *text, void *userData)
{
	struct ElementPresenceData *data = userData;

	// If an error occurred, process the error condition and return.
	if (data->error)
	{
		XMPP_HandleStanzaError(xp->userData, data->error->error, data->error->type, name, data->common->id, data->error->text);
		return false;
	}

	// Process this presence depending on what type of presence stanza it is.
	switch (data->type)
	{

		// Normal presence update
		case XMPP_PresenceType_Update:
			XMPP_ProcessPresenceUpdate(xp->userData, data->availability, data->common->from, data->common->to, data->common->id,
				data->priority, data->status);
			break;

		// Client logging out
		case XMPP_PresenceType_Unavailable:
			XMPP_ProcessPresenceUnavailable(xp->userData, data->common->from, data->common->to, data->common->id, data->status);
			break;

		// Client adding another user as a friend
		case XMPP_PresenceType_Subscribe:
			XMPP_ProcessPresenceSubscribe(xp->userData, data->common->from, data->common->to, data->common->id);
			break;

		// Client confirming friendship
		case XMPP_PresenceType_Subscribed:
			XMPP_ProcessPresenceSubscribed(xp->userData, data->common->from, data->common->to, data->common->id);
			break;

		// Client removing a friend
		case XMPP_PresenceType_Unsubscribe:
			XMPP_ProcessPresenceUnsubscribe(xp->userData, data->common->from, data->common->to, data->common->id);
			break;

		// Confirming removal
		case XMPP_PresenceType_Unsubscribed:
			XMPP_ProcessPresenceUnsubscribed(xp->userData, data->common->from, data->common->to, data->common->id);
			break;

		// Server requesting user status
		case XMPP_PresenceType_Probe:
			XMPP_ProcessPresenceProbe(xp->userData, data->common->from, data->common->to, data->common->id);
			break;

		// Client reporting presence error
		case XMPP_PresenceType_Error:
			XMPP_ProcessPresenceError(xp->userData, data->common->from, data->common->to, data->common->id);
			break;

		default:
			devassert(0);
	}

	// Process this presence stanza.
	StructDestroy(parse_ElementPresenceData, data);
	return true;
}

// <presence> subelement starting
static bool xmpp_StartPresenceChild(XMPP_Parser *xp, const char *ns, const char *name, const XML_Char **attr, struct XMPP_Element *element,
	void *userData)
{
	struct ElementPresenceData *data = userData;

	if (!strcmp(name, "show") && !strcmp(ns, XMPP_CLIENT_NS))
	{
		element->destroy = xmpp_DestroyShow;
		element->userData = userData;
	}
	else if (!strcmp(name, "status") && !strcmp(ns, XMPP_CLIENT_NS))
	{
		element->destroy = xmpp_DestroyStatus;
		element->userData = userData;
	}
	else if (!strcmp(name, "priority") && !strcmp(ns, XMPP_CLIENT_NS))
	{
		element->destroy = xmpp_DestroyPriority;
		element->userData = userData;
	}

	return true;
}

/************************************************************************/
/* <show>                                                               */
/************************************************************************/

// End <show>.
static bool xmpp_DestroyShow(XMPP_Parser *xp, const char *ns, const char *name, char *text, void *userData)
{
	struct ElementPresenceData *data = userData;
	char *trimmed;

	// Validate.
	if (data->availability || !text || !text[0])
	{
		data->error = CreateXmppStanzaError(StanzaError_BadRequest, Stanza_Modify, NULL);
		return false;
	}

	// Trim whitespace.
	estrStackCreate(&trimmed);
	estrTrimXmlWhitespace(&trimmed, text);

	// Parse availability.
	if (!strcmp(trimmed, "away"))
		data->availability = XMPP_PresenceAvailability_Away;
	else if (!strcmp(trimmed, "chat"))
		data->availability = XMPP_PresenceAvailability_Chat;
	else if (!strcmp(trimmed, "dnd"))
		data->availability = XMPP_PresenceAvailability_Dnd;
	else if (!strcmp(trimmed, "xa"))
		data->availability = XMPP_PresenceAvailability_Xa;
	else
	{
		data->error = CreateXmppStanzaError(StanzaError_BadRequest, Stanza_Modify, NULL);
		estrDestroy(&trimmed);
		return false;
	}

	estrDestroy(&trimmed);
	return true;
}

/************************************************************************/
/* <status>                                                             */
/************************************************************************/

// End <status>.
static bool xmpp_DestroyStatus(XMPP_Parser *xp, const char *ns, const char *name, char *text, void *userData)
{
	struct ElementPresenceData *data = userData;
	if (data->status)
	{
		data->error = CreateXmppStanzaError(StanzaError_BadRequest, Stanza_Modify, NULL);
		return false;
	}
	data->status = text ? strdup(text) : NULL;
	return true;
}

/************************************************************************/
/* <priority>                                                             */
/************************************************************************/

// End <priority>.
static bool xmpp_DestroyPriority(XMPP_Parser *xp, const char *ns, const char *name, char *text, void *userData)
{
	struct ElementPresenceData *data = userData;
	long value = 0;
	bool error = false;

	if (text)
	{
		const char *i = text;
		char *end;
		// Skip leading whitespace.
		while (IsXmlWhitespace(i) && *i)
			i = UTF8GetNextCodepoint(i);

		// Parse integer value.
		errno = 0;
		value = strtol(i, &end, 10);
		if (!value && errno)
			error = true;

		// Check for trailing garbage.
		for (i = end; *i && IsXmlWhitespace(i); i = UTF8GetNextCodepoint(i))
			{}
		if (*i)
			error = true;

		// Check range.
		if (value < -128 || value > 127)
			error = true;
	}

	// Report errors, if any.
	if (error || data->priority)
	{
		data->error = CreateXmppStanzaError(StanzaError_BadRequest, Stanza_Modify, NULL);
		return false;
	}
	data->priority = (XMPP_Priority)value;
	return true;
}

/************************************************************************/
/* <auth>                                                               */
/************************************************************************/

// Data associated with the <auth> element.
AUTO_STRUCT;
typedef struct ElementAuthData
{
	bool mechanismPlain;			// True if the mechanism is "PLAIN"
} ElementAuthData;

// Get SASL authentication type.
static char *xmpp_AuthType(const char **attr, const char *attrNs)
{
	const XML_Char **i;
	char *mechanism = NULL;

	// Get mechanism.
	for (i = attr; *i; i += 2)
	{
		if (xmpp_attribEqual(*i, "mechanism", attrNs))
			mechanism = strdup(i[1]);
	}
	return mechanism;
}

// Start <auth>.
static bool xmpp_CreateAuth(XMPP_Parser *xp, const char *ns, const char *name, const XML_Char **attr, void *userData)
{
	ElementAuthData *data = userData;
	char *mechanism = xmpp_AuthType(attr, ns);
	data->mechanismPlain = !strcmp(mechanism, "PLAIN");
	XMPP_HandleAuthBegin(xp->userData);
	return true;
}

// End <auth>.
static bool xmpp_DestroyAuth(XMPP_Parser *xp, const char *ns, const char *name, char *text, void *userData)
{
	ElementAuthData *data = userData;
	char buf[771];		//255 NULL 255 NULL 255
	int decodelen;
	char *authcid;
	char *authzid;
	char *passwd;

	devassert(xp->loginState == XMPP_LoginState_Connected);
	XMPP_HandleAuthEnd(xp->userData);

	// Abort if this is not AUTH PLAIN.
	if (!data->mechanismPlain)
	{
		XMPP_HandleSaslError(xp->userData, XMPP_SaslError_InvalidMechanism, NULL);
		return false;
	}

	// Some element text must be provided.
	if (!text || !text[0])
	{
		XMPP_HandleSaslError(xp->userData, XMPP_SaslError_MalformedRequest, NULL);
		return false;
	}

	// Attempt to decode
	XMPP_RedactAuthString(xp->userData, text);
	decodelen = decodeBase64String(text, strlen(text), buf, sizeof(buf));
	if (!decodelen || decodelen > sizeof(buf) - 1)
	{
		XMPP_HandleSaslError(xp->userData, XMPP_SaslError_IncorrectEncoding, NULL);
		return false;
	}

	// Find the authzid and password.
	authcid = buf;
	authcid[decodelen] = '\0';
	authzid = authcid + strlen(authcid) + 1;
	passwd = authzid + strlen(authzid) + 1;

	// Start validating these credentials.
	XMPP_ValidateLogin(xp->userData, authzid, passwd);
	xp->loginState = XMPP_LoginState_AuthWait;
	verbose_printf("%s (%s) attempting to log on...\n", authzid, authcid);
	StructDestroy(parse_ElementAuthData, data);
	return true;
}

/************************************************************************/
/* <stream>                                                             */
/************************************************************************/

// Start <stream>.
static bool xmpp_CreateStream(XMPP_Parser *xp, const char *ns, const char *name, const XML_Char **attr, void *userData)
{
	// Open stream.
	devassert(xp->loginState != XMPP_LoginState_LoggedIn);
	XMPP_StartStream(xp->userData, xp->loginState);
	return true;
}

// End <stream>.
static bool xmpp_DestroyStream(XMPP_Parser *xp, const char *ns, const char *name, char *text, void *userData)
{
	const char *reason = "Stream ended";
	if (xp->loginState != XMPP_LoginState_LoggedIn)
	{
		reason = "Login aborted";
		XMPP_LoginAborted(xp->userData, xp->loginState);
	}
	XMPP_EndStream(xp->userData, reason);
	return true;
}

// Start of stanza
static bool xmpp_StartStreamChild(XMPP_Parser *xp, const char *ns, const char *name, const XML_Char **attr, struct XMPP_Element *element,
	void *userData)
{
	switch (xp->loginState)
	{
		// Handle messages from newly-connected clients.
		case XMPP_LoginState_Connected:

			// <starttls>
			if (!strcmp(name, "starttls") && !strcmp(ns, XMPP_TLS_NS))
			{
				// Handled when the element is closed
			}

			// <auth>
			else if ((xp->secure || xp->plaintextPermitted) && !strcmp(name, "auth") && !strcmp(ns, XMPP_SASL_NS))
			{
				element->userData = StructCreate(parse_ElementAuthData);
				element->create = xmpp_CreateAuth;
				element->destroy = xmpp_DestroyAuth;
			}

			// Unknown
			else
			{
				XMPP_HandleStreamError(xp->userData, XMPP_StreamError_NotAuthorized, NULL);
				return true;
			}
			break;

		// Error on data while waiting for authentication
		case XMPP_LoginState_AuthWait:
			XMPP_HandleSaslError(xp->userData, XMPP_SaslError_MalformedRequest, "Spurious elements after <auth/>");
			break;

		// Handle stanzas while waiting for resource binding.
		case XMPP_LoginState_Authenticated:

			// <iq>
			if (!strcmp(name, "iq") && !strcmp(ns, XMPP_CLIENT_NS))
			{
				element->userData = StructCreate(parse_ElementIqData);
				element->create = xmpp_CreateIq;
				element->destroy = xmpp_DestroyIq;
				element->startChild = xmpp_StartIqChild;
			}

			// Unknown
			else
			{
				XMPP_HandleStreamError(xp->userData, XMPP_StreamError_NotAuthorized, NULL);
				return true;
			}
			break;

		// Handle stanzas once logged in.
		case XMPP_LoginState_LoggedIn:

			// <iq>
			if (!strcmp(name, "iq") && !strcmp(ns, XMPP_CLIENT_NS))
			{
				element->userData = StructCreate(parse_ElementIqData);
				element->create = xmpp_CreateIq;
				element->destroy = xmpp_DestroyIq;
				element->startChild = xmpp_StartIqChild;
			}

			// <message>
			else if (!strcmp(name, "message") && !strcmp(ns, XMPP_CLIENT_NS))
			{
				element->userData = StructCreate(parse_ElementMessageData);
				element->create = xmpp_CreateMessage;
				element->destroy = xmpp_DestroyMessage;
				element->startChild = xmpp_StartMessageChild;
			}

			// <presence>
			else if (!strcmp(name, "presence") && !strcmp(ns, XMPP_CLIENT_NS))
			{
				element->userData = StructCreate(parse_ElementPresenceData);
				element->create = xmpp_CreatePresence;
				element->destroy = xmpp_DestroyPresence;
				element->startChild = xmpp_StartPresenceChild;
			}

			// Unknown
			else
				XMPP_HandleStreamError(xp->userData, XMPP_StreamError_UnsupportedStanzaType, NULL);
			break;

		default:
			devassert(0);
	}

	return true;
}

// End of stanza
static bool xmpp_EndStreamChild(XMPP_Parser *xp, const char *ns, const char *name, bool ignore, void *userData)
{
	// Close stanzas, processing if necessary.
	switch (xp->loginState)
	{
		case XMPP_LoginState_Connected:
			if (!strcmp(name, "starttls") && !strcmp(ns, XMPP_TLS_NS))
			{
				XMPP_ProcessStartTls(xp->userData);
				XML_StopParser(xp->p, false);
			}
			break;

		case XMPP_LoginState_AuthWait:
			break;

		case XMPP_LoginState_Authenticated:
			break;

		case XMPP_LoginState_LoggedIn:
			XMPP_StatsIncrementStanzaCount();
			break;

		default:
			devassert(0);
	}

	return true;
}

/************************************************************************/
/* Top level pseudo-element                                             */
/************************************************************************/

// Initial top level context.
bool xmpp_StartChildTopLevel(XMPP_Parser *xp, const char *ns, const char *name, const XML_Char **attr, struct XMPP_Element *element,
	void *userData)
{
	// Check the namespace.
	if (strcmp(ns, "http://etherx.jabber.org/streams"))
	{
		XMPP_HandleStreamError(xp->userData, XMPP_StreamError_BadNamespacePrefix, NULL);
		XML_StopParser(xp->p, false);
		return false;
	}

	// Make sure this is a stream element.
	if (strcmp(name, "stream"))
	{
		XMPP_HandleStreamError(xp->userData, XMPP_StreamError_BadFormat, NULL);
		XML_StopParser(xp->p, false);
		return false;
	}

	// Set up stream.
	element->create = xmpp_CreateStream;
	element->destroy = xmpp_DestroyStream;
	element->startChild = xmpp_StartStreamChild;
	element->endChild = xmpp_EndStreamChild;
	return true;
}

/************************************************************************/
/* Generic element                                                      */
/************************************************************************/

// No-op element creation handler.
static bool xmpp_CreateElementNoOp(XMPP_Parser *xp, const char *ns, const char *name, const XML_Char **attr, void *userData)
{
	return true;
}

// No-op element destruction handler.
static bool xmpp_DestroyElementNoOp(XMPP_Parser *xp, const char *ns, const char *name, char *text, void *userData)
{
	return true;
}

// No-op element child start handler.
static bool xmpp_StartChildNoOp(XMPP_Parser *xp, const char *ns, const char *name, const XML_Char **attr, struct XMPP_Element *element,
	void *userData)
{
	return true;
}

// No-op element child end handler.
static bool xmpp_EndChildNoOp(XMPP_Parser *xp, const char *ns, const char *name, bool ignore, void *userData)
{
	return true;
}

/************************************************************************/
/* XML parsing                                                          */
/************************************************************************/

// Allocate an initialized struct XMPP_Element with no-op handlers.
static struct XMPP_Element *xmpp_NewElement()
{
	struct XMPP_Element *element = malloc(sizeof(*element));
	element->create = xmpp_CreateElementNoOp;
	element->destroy = xmpp_DestroyElementNoOp;
	element->startChild = xmpp_StartChildNoOp;
	element->endChild = xmpp_EndChildNoOp;
	element->text = NULL;
	element->userData = NULL;
	element->ignore = false;
	return element;
}

static void xmpp_FreeElement(struct XMPP_Element *element)
{
	// Does not touch the userData
	estrDestroy(&element->text);
	free(element);
}

// Set up handlers for a new child XML element.
static void xmpp_StartElementHandler(XMPP_Parser *xp, const XML_Char *el, const XML_Char **attr)
{
	char *ns,*tag;
	struct XMPP_Element *element = xmpp_NewElement();
	struct XMPP_Element *parent = eaTail(&xp->elements);
	bool result;

	// Get tag and namespace.
	tag = xmpp_copytag(el, &ns);

	// Notify the parent.
	devassert(parent);
	if (!parent->ignore)
	{
		result = parent->startChild(xp, ns, tag, attr, element, parent->userData);
		parent->ignore = !result;
	}

	// Create the child.
	eaPush(&xp->elements, element);
	if (!element->ignore)
	{
		result = element->create(xp, ns, tag, attr, element->userData);
		element->ignore = !result;
	}

	free(tag);
}

// Close a child element.
static void xmpp_EndElementHandler(XMPP_Parser *xp, const XML_Char *el)
{
	char *ns,*tag;
	int len;
	struct XMPP_Element *element;
	struct XMPP_Element *parent;
	bool result;

	// Get tag and namespace.
	tag = xmpp_copytag(el, &ns);

	// Get elements of interest.
	len = eaSize(&xp->elements);
	devassert(xp->elements);
	element = xp->elements[len - 1];
	parent = xp->elements[len - 2];
	devassert(len >= 2 && element && parent);

	// Notify child.
	if (!element->ignore)
	{
		result = element->destroy(xp, ns, tag, element->text, element->userData);
		element->ignore = !result;
	}

	// Notify parent.
	if (!parent->ignore)
	{
		result = parent->endChild(xp, ns, tag, element->ignore, element->userData);
		parent->ignore = !result;
	}

	// Destroy child.
	free(tag);
	xmpp_FreeElement(element);
	eaPop(&xp->elements);
}

//grab text content
static void xmpp_CharacterDataHandler(XMPP_Parser *xp, const XML_Char *str, int len)
{
	int size = eaSize(&xp->elements);
	devassert(size);
	estrConcat(&xp->elements[size - 1]->text, str, len);
}

// Comments are not permitted in XMPP.
static void xmpp_CommentHandler(XMPP_Parser *xp, const XML_Char *data)
{
	XMPP_HandleStreamError(xp->userData, XMPP_StreamError_RestrictedXml, "XML comments are prohibited.");
	XML_StopParser(xp->p, false);
}

// Processing instructions are not permitted in XMPP.
static void xmpp_ProcessingInstructionHandler(XMPP_Parser *xp, const XML_Char *target, const XML_Char *data)
{
	XMPP_HandleStreamError(xp->userData, XMPP_StreamError_NotAuthorized, "XML processing instructions are prohibited.");
	XML_StopParser(xp->p, false);
}

// DTDs are not permitted in XMPP.
static void xmpp_StartDoctypeDeclHandler(XMPP_Parser *xp, const XML_Char *doctypeName, const XML_Char *sysid, const XML_Char *pubid,
	int has_internal_subset)
{
	XMPP_HandleStreamError(xp->userData, XMPP_StreamError_NotAuthorized, "Document type declarations are prohibited.");
	XML_StopParser(xp->p, false);
}

// Since DTDs are not allowed, this should never be called.
static void xmpp_EndDoctypeDeclHandler(XMPP_Parser *xp)
{
	devassert(0);
}

// Client parser allocator allocate
static void *xmpp_malloc(size_t size)
{
	void *memory;
	bool success;
	size_t newSize = *xmppClientMemoryTracker + size;

	// Check that there is enough memory for this new allocation.
	if (newSize > maxMemoryUse || newSize < *xmppClientMemoryTracker)
	{
		xmppClientMemoryExhausted = true;
		return NULL;
	}

	// Allocate the memory.
	memory = malloc(size);
	*xmppClientMemoryTracker = newSize;
	success = stashAddInt(xmppClientMemoryAllocations, memory, (int)size, false);
	devassert(success);

	return memory;
}

// Client parser allocator resize
static void *xmpp_realloc(void *ptr, size_t size)
{
	void *memory;
	bool success;
	size_t newSize;
	int oldSize;

	// Handle null specially.
	if (!ptr)
		return xmpp_malloc(size);

	// Look up this allocation.
	success = stashFindInt(xmppClientMemoryAllocations, ptr, &oldSize);
	devassert(success);
	devassert(oldSize >= 0);

	// Check that there is enough memory for this new allocation.
	newSize = *xmppClientMemoryTracker - oldSize + size;
	if (newSize > maxMemoryUse || newSize < *xmppClientMemoryTracker)
	{
		xmppClientMemoryExhausted = true;
		return NULL;
	}

	// Reallocate the block.
	memory = realloc(ptr, size);
	*xmppClientMemoryTracker = newSize;
	success = stashRemoveInt(xmppClientMemoryAllocations, ptr, &oldSize);
	devassert(success);
	success = stashAddInt(xmppClientMemoryAllocations, memory, (int)size, false);
	devassert(success);

	return memory;
}

// Client parser allocator free
static void xmpp_free(void *ptr)
{
	int size;
	bool success;

	// Handle null specially.
	if (!ptr)
		return;

	// Look up this allocation.
	success = stashFindInt(xmppClientMemoryAllocations, ptr, &size);
	devassert(success);
	devassert(size >= 0);

	// Free the allocation.
	free(ptr);
	*xmppClientMemoryTracker -= size;
	success = stashRemoveInt(xmppClientMemoryAllocations, ptr, &size);
	devassert(success);
}

// Create a parser in-place.
static void XMPP_ParserInit(XMPP_Parser *xp, void *userData, const XML_Char *encoding, bool plaintextPermitted)
{
	static volatile U32 sid = 0;
	XML_Parser p;
	struct XMPP_Element *element;

	// Set up allocator.
	memorySuite.malloc_fcn = xmpp_malloc;
	memorySuite.realloc_fcn = xmpp_realloc;
	memorySuite.free_fcn = xmpp_free;

	// Initialize parser object.
	memset(xp, 0, sizeof(*xp));
	xp->userData = userData;
	xp->encoding = strdup(encoding);
	xp->loginState = XMPP_LoginState_Connected;
	xp->plaintextPermitted = plaintextPermitted;

	// Initialize memory tracking for the parser object.
	xp->allocations = stashTableCreateAddress(32);
	xmppClientMemoryTracker = &xp->memory;
	xmppClientMemoryAllocations = xp->allocations;
	xmppClientMemoryExhausted = false;

	// Create top level context.
	element = xmpp_NewElement();
	element->startChild = xmpp_StartChildTopLevel;
	eaPush(&xp->elements, element);

	// Create the parser.
	p = XML_ParserCreate_MM(encoding, &memorySuite, &xmppNamespaceDelimiter);
	XML_SetElementHandler(p, xmpp_StartElementHandler, xmpp_EndElementHandler);
	XML_SetUserData(p, xp);
	XML_SetCharacterDataHandler(p, xmpp_CharacterDataHandler);
	XML_SetCommentHandler(p, xmpp_CommentHandler);
	XML_SetProcessingInstructionHandler(p, xmpp_ProcessingInstructionHandler);
	XML_SetDoctypeDeclHandler(p, xmpp_StartDoctypeDeclHandler, xmpp_EndDoctypeDeclHandler);
	xp->p = p;
}

// Deinitialize a parser.
static void XMPP_ParserDeinit(XMPP_Parser *xp)
{
	xmppClientMemoryTracker = &xp->memory;
	xmppClientMemoryAllocations = xp->allocations;
	xmppClientMemoryExhausted = false;
	XML_ParserFree(xp->p);
	devassert(!stashGetCount(xp->allocations));
	stashTableDestroy(xp->allocations);
	devassert(!xp->memory);
}

// Create an XMPP parser for a new client.
XMPP_Parser* XMPP_ParserCreate(void *userData, const XML_Char *encoding, bool plaintextPermitted)
{
	XMPP_Parser *xp = malloc(sizeof(XMPP_Parser));
	XMPP_ParserInit(xp, userData, encoding, plaintextPermitted);

	// Check for sufficient memory.
	if (xmppClientMemoryExhausted)
	{
		Errorf("Memory limit set too low.");
		XMPP_ParserDestroy(xp);
		return NULL;
	}

	return xp;
}

// Restart the XMPP parser.
void XMPP_ParserRestart(XMPP_Parser *stream)
{
	// If processing, defer restart.
	if (stream->processing)
		stream->restart = true;

	// Otherwise, restart the parser.
	else
	{
		// Save old state that should be preserved.
		void *userData = stream->userData;
		const XML_Char *encoding = stream->encoding;
		bool plaintextPermitted = stream->plaintextPermitted;
		enum XMPP_LoginState loginState = stream->loginState;
		bool secure = stream->secure;

		// Recreate the parser.
		XMPP_ParserDeinit(stream);
		XMPP_ParserInit(stream, userData, encoding, plaintextPermitted);

		// Restore state.
		stream->loginState = loginState;
		stream->secure = secure;

		// Check for sufficient memory.
		if (xmppClientMemoryExhausted)
		{
			Errorf("Memory limit set too low.");
			stream->parseError = true;
		}
	}
}

// Frees all parts of the XMPP_Parser.
void XMPP_ParserDestroy(XMPP_Parser *xp)
{
	if (xp->processing)
		xp->destroy = true;
	else
	{
		XMPP_ParserDeinit(xp);
		free(xp);
	}
}

// Parse an XMPP fragment.
void XMPP_ParserParse(XMPP_Parser *xp, const char *buf, int len)
{
	int result;
	const int STREAM_CONTINUE = 0;

	// Do nothing if the stream is in an error state.
	if (!xp || xp->parseError)
		return;

	// Set the memory tracker for this client.
	xmppClientMemoryTracker = &xp->memory;
	xmppClientMemoryAllocations = xp->allocations;
	xmppClientMemoryExhausted = false;

	// Parse the incoming data.
	devassert(!xp->processing);
	xp->processing = true;
	xp->recordSource = buf;
	xp->recordLen = len;
	xp->recordStart = XML_GetCurrentByteIndex(xp->p);
	result = XML_Parse(xp->p, buf, len, STREAM_CONTINUE);
	devassert(xp->processing);
	xp->processing = false;

	// Record extra data
	if (xp->recordPos)
	{
		XML_Index position = XML_GetCurrentByteIndex(xp->p);
		devassert(xp->recordPos >= xp->recordStart);
		devassert(position >= xp->recordPos);
		devassert(position - (xp->recordPos - xp->recordStart) < len);
		estrConcat(&xp->recordString, buf + (xp->recordPos - xp->recordStart), position - xp->recordPos);
		xp->recordPos = position;
	}

	// Check for memory exhaustion.
	if (xmppClientMemoryExhausted)
	{
		XMPP_HandleStreamError(xp->userData, XMPP_StreamError_ResourceConstraint, NULL);
		xp->parseError = true;
		return;
	}

	// Destroy the stream if requested.
	if (xp->destroy)
	{
		XMPP_ParserDestroy(xp);
		return;
	}

	// Restart if requested.
	if (xp->restart)
	{
		XMPP_ParserRestart(xp);
		return;
	}

	// Handle parse errors.
	if (!result)
	{
		XMPP_HandleStreamError(xp->userData, XMPP_StreamError_XmlNotWellFormed, NULL);
		xp->parseError = true;
		return;
	}
}

// Notify the XMPP parser that the link is secure.
void XMPP_ParserSecure(XMPP_Parser *xp)
{
	xp->secure = true;
}

// Notify the parser that authentication is complete.
void XMPP_ParserAuthComplete(XMPP_Parser *xp, bool success)
{
	// Ignore this unless the parser is waiting for authentication.
	if (xp->loginState != XMPP_LoginState_AuthWait)
		return;

	// On failure, roll back to the previous state.
	if (!success)
	{
		xp->loginState = XMPP_LoginState_Connected;
		return;
	}

	// Transition to the authenticated state.
	xp->loginState = XMPP_LoginState_Authenticated;
	XMPP_ParserRestart(xp);
}

void XMPP_ParserSetLoginState(XMPP_Parser *xp, XMPP_LoginState eState)
{
	if (devassert(xp))
		xp->loginState = eState;
}

#include "AutoGen/XMPP_Parsing_c_ast.c"
