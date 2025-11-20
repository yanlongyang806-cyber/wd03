#pragma once
GCC_SYSTEM

#include "Organization.h"

typedef struct NetComm NetComm;
typedef struct NetLink NetLink;
typedef struct StashTableImp* StashTable;

#define SMTP_RESPONSE_TIMEOUT_SEC 60
#define SMTP_DEFAULT_SERVER ORGANIZATION_DOMAIN
#define SMTP_DEFAULT_DOMAIN ORGANIZATION_DOMAIN
#define SMTP_DEFAULT_FROM "utilitieslib@" ORGANIZATION_DOMAIN

//#define SMTP_DEBUG 0

//used for return vals when calling smtpMsgRequestSend_BgThread
//will be called for every email send, errorString is NULL if the email sent correctly.
typedef void (*smtpBgThreadSendResultFunc)(void *pUserData, char *pErrorString);


AUTO_STRUCT;
typedef struct SMTPMessagePart
{
	char *type; AST(ESTRING)
	char *encoding; AST(ESTRING)
	char *disposition; AST(ESTRING)
	char *content; AST(ESTRING)
} SMTPMessagePart;

AUTO_STRUCT;
typedef struct SMTPMessage
{
	char *from; AST(ESTRING DEFAULT(SMTP_DEFAULT_FROM))
	char **recipients; AST(ESTRING)
	StashTable headers; NO_AST
	SMTPMessagePart **parts;
} SMTPMessage;

typedef enum SMTPClientState
{
	SMTP_CONNECTING = 0,
	SMTP_WAITING_FOR_SERVICE_READY,
	SMTP_SENT_HELO,
	SMTP_SENT_FROM,
	SMTP_SENT_TO,
	SMTP_REQUESTED_DATA_SEND,
	SMTP_SENT_DATA,
	SMTP_DISCONNECTED
} SMTPClientState;

typedef struct SMTPClient
{
	// Connection vars
	NetComm *comm;
	NetLink *link;
	bool made_comm;

	// State Handling
	SMTPClientState state;
	U32 lastStateTime;
	SMTPMessage **messages;

	// Internal buffer during SMTP conversation
	char *incomingData; AST(ESTRING)
	int currentRecipient;
	char *lastRecipient;

	// Return data
	char *result; AST(ESTRING)
	bool success;
} SMTPClient;

AUTO_STRUCT;
typedef struct SMTPMessageRequest
{
	char *from; AST(ESTRING, DEFAULT(SMTP_DEFAULT_FROM))
	char **to; AST(ESTRING)
	char *subject; AST(ESTRING)
	char *body; AST(ESTRING)
	int priority;

	int timeout; //seconds... 0 means none

	// For file attachments ...                Examples:
	char *attachfilename; AST(ESTRING)      // "c:/temp/tempgifimage_rawdata_thatwasjustgenerated"
	char *attachsuggestedname; AST(ESTRING) // "PrettyAttachedName.gif"
	char *attachmimetype; AST(ESTRING)      // "image/gif"  (should be NULL for regular attachments)

	bool html;
	char *server; AST(ESTRING)
	NetComm *comm; NO_AST

	smtpBgThreadSendResultFunc pResultCBFunc; NO_AST
	void *pUserData; NO_AST

} SMTPMessageRequest;

// Get mail server
const char* smtpGetMailServer(void);

// Add a new recipient to a message.
bool smtpMsgAddRecipient(SMTPMessage *msg, const char *header, const char *name, const char *address);
#define smtpMsgAddTo(msg, name, address) smtpMsgAddRecipient((msg), "To", (name), (address))
#define smtpMsgAddCC(msg, name, address) smtpMsgAddRecipient((msg), "CC", (name), (address))
#define smtpMsgAddBCC(msg, name, address) smtpMsgAddRecipient((msg), NULL, (name), (address))

// Set a header value of a message.
bool smtpMsgSetHeader(SMTPMessage *msg, const char *header, const char *val);
#define smtpMsgSetSubject(msg, subject) smtpMsgSetHeader((msg), "Subject", (subject))

// Set the From value of a message.
bool smtpMsgSetFrom(SMTPMessage *msg, const char *name, const char *address);

// Add a new body part to a message.
bool smtpMsgAddPart(SMTPMessage *msg, SMTPMessagePart *part);

// Warning, performs all work in memory ... don't attach enormous files!
// If pMimeType is NULL, it will choose application/octet-stream
bool smtpMsgAttachFile(SMTPMessage *msg, const char *filename, const char *suggestedName, const char *pMimeType);

// Serialize to a valid message.
bool smtpMsgSerialize(SMTPMessage *msg, char **out);

// Create a new message part.
SMTPMessagePart *smtpMsgPartCreate(const char* type, const char *charset, const char *encoding, const char *disposition, const char *content);

#define smtpMsgAddTextPart(msg, content) smtpMsgAddPart((msg), smtpMsgPartCreate("text/plain", "iso-8859-1", "8bit", NULL, (content)))
#define smtpMsgAddHTMLPart(msg, content) smtpMsgAddPart((msg), smtpMsgPartCreate("text/html", "iso-8859-1", "8bit", NULL, (content)))

SMTPClient *smtpClientCreate(NetComm *comm, const char *server, char **ppError);
void smtpClientDestroy(SMTPClient *client);
bool smtpClientOncePerFrame(SMTPClient *client); // Returns true when the client has finished processing
bool smtpClientSend(SMTPClient *client, SMTPMessage *msg);
bool smtpClientWait(SMTPClient *client, F32 timeout);
#define smtpClientSendWait(client, msg, timeout) smtpClientSend((client), (msg)) && smtpClientWait((client), (timeout))

bool smtpMsgRequestSend_Blocking(SMTPMessageRequest *req, char **status);

typedef struct ParseTable ParseTable;
extern ParseTable parse_SMTPMessageRequest[];
#define TYPE_parse_SMTPMessageRequest SMTPMessageRequest



//simple background-threaded version of the above

//the netcomm in pReq, if any, is ignored
void smtpMsgRequestSend_BgThread(SMTPMessageRequest *pReq);

//must be called by anyone using the above
void smtpBgThreadingOncePerFrame(void);



//generic failure CB for use with above code... pUserData must be a STRDUP of the subject line of the email
void GenericSendEmailResultCB(char *pUserData, char *pErrorString);