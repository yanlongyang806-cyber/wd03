#include "netsmtp.h"
#include "AutoGen/netsmtp_h_ast.h"
#include "file.h"
#include "net.h"
#include "StashTable.h"
#include "estring.h"
#include "rand.h"
#include "timing.h"
#include "logging.h"
#include "wininclude.h"
#include "intFIFO.h"
#include "autogen/netsmtp_c_ast.h"
#include "threadManager.h"
#include "alerts.h"

static char *spMailServer = NULL;
AUTO_CMD_ESTRING(spMailServer, MailServer) ACMD_COMMANDLINE;

const char* smtpGetMailServer(void)
{
	return spMailServer;
}

AUTO_FIXUPFUNC;
TextParserResult SMTPMessageRequestFixup(SMTPMessageRequest *pRequest, enumTextParserFixupType eType, void *pExtraData)
{
	switch (eType)
	{
	case FIXUPTYPE_CONSTRUCTOR:
		estrCopy2(&pRequest->server, spMailServer ? spMailServer : SMTP_DEFAULT_SERVER);
		break;
	}



	return PARSERESULT_SUCCESS;
}

#if !PLATFORM_CONSOLE

// ---------------------------------------------------------------------------------
// Generic Internal Helper Functions

// This belongs somewhere in UtilitiesLib in a formal way, pilfered from TicketTracker
static const char * getMachineAddress(void)
{
#if _XBOX
	return "xbox";
#else
	static char *pMachineAddress = NULL;
	if(pMachineAddress == NULL)
	{
		estrForceSize(&pMachineAddress, 128);
		gethostname(pMachineAddress, 127);
	}
	return pMachineAddress;
#endif
}

static void linkSendRawText(NetLink *link, const char *text)
{
	Packet *p = pktCreateRaw(link);
#ifdef SMTP_DEBUG
	printf("%s\n", text);
	log_printf(LOG_SMTP, "%s\n", text);
#endif
	pktSendStringRaw(p, text);
	pktEnd(p);
	pktSendRaw(&p);
}

static void keyDestructor(char *str) { estrDestroy(&str); }

AUTO_FIXUPFUNC;
TextParserResult fixupSMTPMessage(SMTPMessage *msg, enumTextParserFixupType type, void *pExtraData)
{
	switch(type)
	{
	xcase FIXUPTYPE_CONSTRUCTOR: 
		msg->headers = stashTableCreateWithStringKeys(16, StashDefault);
	xcase FIXUPTYPE_DESTRUCTOR:
		stashTableDestroyEx(msg->headers, NULL, keyDestructor);
	}

	return PARSERESULT_SUCCESS;
}

bool smtpMsgAddRecipient(SMTPMessage *msg, const char *header, const char *name, const char *address)
{
	if(!address || !address[0])
		return true;

	if(strchr(address, '@') == NULL)
		address = STACK_SPRINTF("%s@" SMTP_DEFAULT_DOMAIN, address);

	eaPush(&msg->recipients, estrCreateFromStr(address));
	if(header)
	{
		char *str = NULL;

		if(stashFindPointer(msg->headers, header, &str))
			estrConcatf(&str, ", ");
		else
			str = NULL;
		estrConcatf(&str, "%s <%s>", name ? name : "", address);
		stashAddPointer(msg->headers, header, str, true);
	}

	return true;
}

bool smtpMsgSetHeader(SMTPMessage *msg, const char *header, const char *val)
{
	char *str = NULL;

	if(!stashFindPointer(msg->headers, header, &str))
		str = NULL;
	estrCopy2(&str, val);
	stashAddPointer(msg->headers, header, str, true);

	return true;
}

bool smtpMsgSetFrom(SMTPMessage *msg, const char *name, const char *address)
{
	if(strchr(address, '@') == NULL)
		address = STACK_SPRINTF("%s@" SMTP_DEFAULT_DOMAIN, address);

	estrPrintf(&msg->from, "%s", address);
	if(!smtpMsgSetHeader(msg, "From", STACK_SPRINTF("%s <%s>", name ? name : "", address)))
		return false;
	return true;
}

bool smtpMsgAddPart(SMTPMessage *msg, SMTPMessagePart *part)
{
	eaPush(&msg->parts, part);

	return true;
}

bool smtpMsgAttachFile(SMTPMessage *msg, const char *filename, const char *suggestedName, const char *pMimeType)
{
	int len = 0;
	char *base64Data = NULL;
	char *disposition = NULL;
	SMTPMessagePart *part = NULL;
	char *rawData = fileAlloc(filename, &len);

	if(!rawData)
		return false;

	if(len == 0)
	{
		if(rawData)
			free(rawData);
		return false;
	}

	estrBase64Encode(&base64Data, rawData, len);

	if(!pMimeType)
		pMimeType = "application/octet-stream";

	estrPrintf(&disposition, "attachment;\r\n filename=\"%s\"", suggestedName);

	part = smtpMsgPartCreate(pMimeType, NULL, "base64", disposition, base64Data);
	smtpMsgAddPart(msg, part);

	estrDestroy(&disposition);
	estrDestroy(&base64Data);
	free(rawData);
	return true;
}

bool smtpMsgSerialize(SMTPMessage *msg, char **out)
{
	StashTableIterator iter;
	StashElement elm;
	char *boundary = NULL;

	// Sanity checks
	if(eaSize(&msg->recipients) == 0)
	{
		Errorf("Cannot send a message with no recipients");
		return false;
	}
	if(eaSize(&msg->parts) == 0)
	{
		Errorf("Cannot send a message with no contents");
		return false;
	}

	// Make sure we start empty
	estrClear(out);

	// Is this a multipart message?
	if(eaSize(&msg->parts) == 1)
	{
		smtpMsgSetHeader(msg, "Content-Type", STACK_SPRINTF("%s", msg->parts[0]->type));
		smtpMsgSetHeader(msg, "Content-Transfer-Encoding", msg->parts[0]->encoding);
	}
	else
	{
		int i;
		for(i=0; i<8; i++)
			estrConcatf(&boundary, "%x", randomU32());
		smtpMsgSetHeader(msg, "Content-Type", STACK_SPRINTF("multipart/alternative;\r\n boundary=\"%s\"", boundary));
	}

	// Set the date if needed
	if(!stashFindElement(msg->headers, "Date", NULL))
	{
		char tmpbuf[100], tmpbuf2[100];
		__time64_t now_secs;
		struct tm now;
		int tzoff, dstoff;

		_time64(&now_secs);
		_localtime64_s(&now, &now_secs);
		_get_timezone(&tzoff);
		_get_dstbias(&dstoff);
		if (now.tm_isdst)
			tzoff += dstoff;
		strftime(tmpbuf, ARRAY_SIZE_CHECKED(tmpbuf), "%a, %d %b %Y %H:%M:%S", &now);
		// ???: Not sure how _get_timezone works in +UTC timezones, the +/- check might need some extra logic. <NPK 2009-01-16>
		sprintf(tmpbuf2, "%s %c%02u%02u", tmpbuf, (tzoff > 0 ? '-' : '+'), (tzoff / 3600), (tzoff % 60));
		smtpMsgSetHeader(msg, "Date", tmpbuf2);
	}

	// Write in all the headers
	stashGetIterator(msg->headers, &iter);
	while(stashGetNextElement(&iter, &elm))
	{
		char *key, *value;

		key = stashElementGetStringKey(elm);
		value = stashElementGetPointer(elm);
		estrConcatf(out, "%s: %s\r\n", key, value);
	}

	// Blanks before the body
	estrConcatf(out, "\r\n");

	if(!boundary)
	{
		estrConcatf(out, "%s\r\n", msg->parts[0]->content);
	}
	else
	{
		FOR_EACH_IN_EARRAY(msg->parts, SMTPMessagePart, part)
			estrConcatf(out, "--%s\r\n", boundary);
			estrConcatf(out, "Content-Type: %s\r\n", part->type);
			estrConcatf(out, "Content-Transfer-Encoding: %s\r\n", part->encoding);
			if(part->disposition)
				estrConcatf(out, "Content-Disposition: %s\r\n", part->disposition);
			estrConcatf(out, "\r\n%s\r\n", part->content);
		FOR_EACH_END
		estrConcatf(out, "--%s--\r\n", boundary);
	}

	estrReplaceOccurrences(out, "\r\n.\r\n", "\r\n .\r\n");

	return true;
}

SMTPMessagePart *smtpMsgPartCreate(const char* type, const char *charset, const char *encoding, const char *disposition, const char *content)
{
	SMTPMessagePart *part = StructCreate(parse_SMTPMessagePart);
	estrPrintf(&part->type, "%s", type);
	if(charset)
		estrConcatf(&part->type, "; charset=\"%s\"", charset);
	estrPrintf(&part->encoding, "%s", encoding);
	if(disposition)
		estrPrintf(&part->disposition, "%s", disposition);
	estrPrintf(&part->content, "%s", content);
	return part;
}

// ---------------------------------------------------------------------------------
// SMTPClient-Specific Internal Helper Functions
static void smtpClientChangeState(SMTPClient *client, SMTPClientState newState)
{
	client->lastStateTime = timeSecondsSince2000();
	client->state = newState;
}

static U32 smtpClientGetStateTime(SMTPClient *client)
{
	return timeSecondsSince2000() - client->lastStateTime;
}

static void smtpClientCleanupConnection(SMTPClient *client)
{
	if(client->link)
		linkRemove(&client->link);

	smtpClientChangeState(client, SMTP_DISCONNECTED);
}

static void smtpClientFail(SMTPClient *client, const char *result)
{
	assertmsg(result && result[0], "No reason for failure given");
	log_printf(LOG_SMTP, "Failing with reason: %s", result);
	estrCopy2(&client->result, result);
	client->success = false;
	smtpClientCleanupConnection(client);
}

static void smtpClientSucceed(SMTPClient *client)
{
	client->success = true;
	smtpClientCleanupConnection(client);
	log_printf(LOG_SMTP, "SMTP Succeeding");

}

static bool smtpClientGetResponse(SMTPClient *client, char **estr, int *code)
{
	if(estrLength(&client->incomingData) > 0)
	{
		char *p = strchr(client->incomingData, '\r');
		if(*p)
		{
			int len;
			*p = 0;
			estrCopy(estr, &client->incomingData);
			*p = '\r';

			while((*p == '\r') || (*p == '\n'))
				p++;

			len = (int)(p - client->incomingData);
			estrRemove(estr, len, estrLength(&client->incomingData)-len);
			estrRemove(&client->incomingData, 0, len);
			*code = atoi(*estr);
			return true;
		}
	}
	return false;
}

static bool smtpClientReturnCodeIsSuccess(int code)
{
	if((code >= 200) && (code <= 299)) // Most all message successes
		return true;

	if(code == 354) // Success response when sending the "DATA" command
		return true;

	return false;
}

static bool smtpClientProcessLastResponse(SMTPClient *client)
{
	char *line = NULL;
	int code = 0;
	if(smtpClientGetResponse(client, &line, &code))
	{
#ifdef SMTP_DEBUG
		printf(">>> %s\n", line);
		log_printf(LOG_SMTP, ">>> %s\n", line);
#endif
		if(smtpClientReturnCodeIsSuccess(code))
		{
			return true;
		}
		else if(client->state == SMTP_SENT_TO)
		{
			Errorf("SMTP: Tried to send to invalid address: %s", client->lastRecipient);
			log_printf(LOG_SMTP, "Tried to send to invalid address: %s", client->lastRecipient);
			return true;
		}
		else
			smtpClientFail(client, line);

		estrDestroy(&line);
	}
	else
	{
		if(smtpClientGetStateTime(client) > SMTP_RESPONSE_TIMEOUT_SEC)
			smtpClientFail(client, "State change time out");
	}
	return false;
}

// Returns true if it actually sent another "RCPT TO" line
static bool smtpClientSendNextRcptLine(SA_PARAM_NN_VALID SMTPClient *client, SA_PARAM_NN_VALID SMTPMessage *msg)
{
	assert(msg->recipients);
	
	if(client->currentRecipient >= eaSize(&msg->recipients))
	{
		client->currentRecipient = 0;
		return false;
	}

	linkSendRawText(client->link, STACK_SPRINTF("RCPT TO: <%s>\r\n", msg->recipients[client->currentRecipient]));
	client->lastRecipient = msg->recipients[client->currentRecipient];
	client->currentRecipient++;
	return true;
}

// ---------------------------------------------------------------------------------
// Internal NetLink Handlers

static void smtpClientOnIncomingData(Packet *pkt, int cmd, NetLink *link, void *ignored)
{
	SMTPClient *client = linkGetUserData(link);

	char *str = pktGetStringRaw(pkt);
	estrConcatf(&client->incomingData, "%s", str);
}

static int smtpClientOnConnect(NetLink* link, void *ignored)
{
	SMTPClient *client = linkGetUserData(link);
	client->link = link;
	estrClear(&client->incomingData);
	client->state = SMTP_WAITING_FOR_SERVICE_READY;
	return 0;
}

static int smtpClientOnDisconnect(NetLink* link, void *ignored)
{
	SMTPClient *client = linkGetUserData(link);
	client->state = SMTP_DISCONNECTED;
	return 0;
}

// ---------------------------------------------------------------------------------
// Exposed Interface

SMTPClient * smtpClientCreate(NetComm *comm, const char *server, char **ppError)
{
	SMTPClient *client = callocStruct(SMTPClient);
	char error[1024];

	if(!comm)
	{
		comm = commCreate(0, 0);
		client->made_comm = true;
	}

	client->comm = comm;
	client->link = commConnectEx(comm, LINKTYPE_USES_FULL_SENDBUFFER, LINK_RAW, server ? server : SMTP_DEFAULT_SERVER, 25, 
		smtpClientOnIncomingData, smtpClientOnConnect, smtpClientOnDisconnect, sizeof(SMTPClient*), SAFESTR(error), __FILE__, __LINE__);

	if (!client->link)
	{
		if (ppError)
		{
			estrCopy2(ppError, error);
		}

		free(client);
		return NULL;
	}
	

	linkSetUserData(client->link, client);

	smtpClientChangeState(client, SMTP_CONNECTING);
	return client;
}

void smtpClientDestroy(SMTPClient *client)
{
	smtpClientCleanupConnection(client);
	estrDestroy(&client->incomingData);
	estrDestroy(&client->result);
	if(client->made_comm)
		commDestroy(&client->comm);
	free(client);
}

// Returns true when the client has finished processing
bool smtpClientOncePerFrame(SMTPClient *client)
{
	if((client->state != SMTP_DISCONNECTED) && (linkDisconnected(client->link)))
	{
		smtpClientFail(client, "Connection to SMTP dropped prematurely.");
		return true;
	}

	if(eaSize(&client->messages) == 0)
	{
		smtpClientSucceed(client);
		return true;
	}

	switch(client->state)
	{
	xcase SMTP_CONNECTING:
		if(linkConnected(client->link))
		{
			smtpClientChangeState(client, SMTP_WAITING_FOR_SERVICE_READY);
		}

	xcase SMTP_WAITING_FOR_SERVICE_READY:
		if(smtpClientProcessLastResponse(client))
		{
			char *heloMsg = NULL;
			estrStackCreate(&heloMsg);
			estrPrintf(&heloMsg, "HELO %s\r\n", getMachineAddress());
			linkSendRawText(client->link, heloMsg);
			estrDestroy(&heloMsg);

			smtpClientChangeState(client, SMTP_SENT_HELO);
		}

	xcase SMTP_SENT_HELO:
		if(smtpClientProcessLastResponse(client))
		{
			linkSendRawText(client->link, STACK_SPRINTF("MAIL FROM: <%s>\r\n", client->messages[0]->from));
			smtpClientChangeState(client, SMTP_SENT_FROM);
		}

	xcase SMTP_SENT_FROM:
	case SMTP_SENT_TO:
		if(smtpClientProcessLastResponse(client))
		{
			if(smtpClientSendNextRcptLine(client, client->messages[0]))
			{
				smtpClientChangeState(client, SMTP_SENT_TO);
			}
			else
			{
				linkSendRawText(client->link, "DATA\r\n");
				smtpClientChangeState(client, SMTP_REQUESTED_DATA_SEND);
			}
		}

	xcase SMTP_REQUESTED_DATA_SEND:
		if(smtpClientProcessLastResponse(client))
		{
			char *msg_str = NULL;
			smtpMsgSerialize(client->messages[0], &msg_str);
			estrConcatf(&msg_str, "\r\n.\r\n");
			linkSendRawText(client->link, msg_str);
			estrDestroy(&msg_str);
			smtpClientChangeState(client, SMTP_SENT_DATA);	
		}

	xcase SMTP_SENT_DATA:
		if(smtpClientProcessLastResponse(client))
		{
			eaRemove(&client->messages, 0);
			if(eaSize(&client->messages) == 0)
			{
				linkSendRawText(client->link, "QUIT\r\n");
				smtpClientSucceed(client);
			}
			else
				smtpClientChangeState(client, SMTP_SENT_HELO);
		}
	}

	return (client->state == SMTP_DISCONNECTED);
}

bool smtpClientSend(SMTPClient *client, SMTPMessage *msg)
{
	eaPush(&client->messages, msg);
	return true;
}

bool smtpClientWait(SMTPClient *client, F32 timeout)
{
	U32 timer = timerAlloc();

	while(1)
	{
		if(timeout && timerElapsed(timer) >= timeout)
		{
			timerFree(timer);
			return false;
		}
		commMonitor(client->comm);
		if(smtpClientOncePerFrame(client))
			break;
	}
	timerFree(timer);
	return true;
}

bool smtpMsgRequestSend_Blocking(SMTPMessageRequest *req, char **status)
{
	SMTPMessage *msg;
	char *pCreationError = NULL;
	SMTPClient *client = smtpClientCreate(req->comm, req->server, &pCreationError);
	bool ret;

	if (!client)
	{
		if (status)
		{
			estrPrintf(status, "Couldn't create smtp client... linkConnectError: %s", pCreationError);
			estrDestroy(&pCreationError);
		}

		return 0;
	}
	
	msg = StructCreate(parse_SMTPMessage);

	smtpMsgSetFrom(msg, NULL, req->from);
	FOR_EACH_IN_EARRAY(req->to, char, addr)
		smtpMsgAddTo(msg, NULL, addr);
	FOR_EACH_END

	smtpMsgSetSubject(msg, req->subject);

	if(req->html)
		smtpMsgAddHTMLPart(msg, req->body);
	else
		smtpMsgAddTextPart(msg, req->body);

	if(req->attachfilename && req->attachsuggestedname)
		smtpMsgAttachFile(msg, req->attachfilename, req->attachsuggestedname, req->attachmimetype);

	if(req->priority)
		smtpMsgSetHeader(msg, "X-Priority", STACK_SPRINTF("%d", req->priority));

	smtpClientSendWait(client, msg, req->timeout);
	ret = client->success;
	if(status)
		estrCopy2(status, client->result);
	
	smtpClientDestroy(client);
	StructDestroy(parse_SMTPMessage, msg);
	return ret;
}

#endif


//stuff for the very simple void smtpMsgRequestSend_BgThread(SMTPMessageRequest *pReq);

AUTO_STRUCT;
typedef struct SmtpBgEmailResult
{
	void *pUserData; NO_AST
	char *pErrorString;
	smtpBgThreadSendResultFunc pResultCBFunc; NO_AST
} SmtpBgEmailResult;

static PointerFIFO *spRequestsFIFO = NULL;
static PointerFIFO *spResultsFIFO = NULL;

static CRITICAL_SECTION gSMTPCritSection;
static CRITICAL_SECTION gSMTPResultsCritSection;




static DWORD WINAPI SmtpBgThread(LPVOID lpParam)
{

	static NetComm *pBGEmailComm = NULL;

	while (1)
	{
		SMTPMessageRequest *pNext = NULL;
		Sleep(1);



		EnterCriticalSection(&gSMTPCritSection);
		PointerFIFO_Get(spRequestsFIFO, &pNext);
		LeaveCriticalSection(&gSMTPCritSection);

		if (pNext)
		{
			char *pResultEString = NULL;
			SmtpBgEmailResult *pResult = StructCreate(parse_SmtpBgEmailResult);

			pResult->pUserData = pNext->pUserData;
			pResult->pResultCBFunc = pNext->pResultCBFunc;

			if (!pBGEmailComm)
			{
				pBGEmailComm = commCreate(0,0);
			}

			pNext->comm = pBGEmailComm;

			if (!smtpMsgRequestSend_Blocking(pNext, &pResultEString))
			{
				pResult->pErrorString = strdup(pResultEString);
			}
			
			estrDestroy(&pResultEString);

			EnterCriticalSection(&gSMTPResultsCritSection);
			PointerFIFO_Push(spResultsFIFO, pResult);
			LeaveCriticalSection(&gSMTPResultsCritSection);

			StructDestroy(parse_SMTPMessageRequest, pNext);
		}
	}
}


void SmtpBgThreadInit(void)
{
	InitializeCriticalSection(&gSMTPCritSection);
	InitializeCriticalSection(&gSMTPResultsCritSection);
	spRequestsFIFO = PointerFIFO_Create(16);
	spResultsFIFO = PointerFIFO_Create(16);
	tmCreateThread(SmtpBgThread, NULL);
}

void smtpMsgRequestSend_BgThread(SMTPMessageRequest *pReq)
{
	SMTPMessageRequest *pClone = StructClone(parse_SMTPMessageRequest, pReq);

	if (!spRequestsFIFO)
	{
		SmtpBgThreadInit();
	}

	EnterCriticalSection(&gSMTPCritSection);
	PointerFIFO_Push(spRequestsFIFO, pClone);
	LeaveCriticalSection(&gSMTPCritSection);
}

void smtpBgThreadingOncePerFrame(void)
{
	SmtpBgEmailResult **sppResults = NULL;
	SmtpBgEmailResult *pNext;

	if (!spRequestsFIFO)
	{
		return;
	}


	EnterCriticalSection(&gSMTPResultsCritSection);
	while(PointerFIFO_Get(spResultsFIFO, &pNext))
	{
		eaPush(&sppResults, pNext);
	}
	LeaveCriticalSection(&gSMTPResultsCritSection);

	FOR_EACH_IN_EARRAY_FORWARDS(sppResults, SmtpBgEmailResult, pResult)
	{
		if (pResult->pResultCBFunc)
		{
			pResult->pResultCBFunc(pResult->pUserData, pResult->pErrorString);
		}
	}
	FOR_EACH_END

	eaDestroyStruct(&sppResults, parse_SmtpBgEmailResult);
}


int siEmailFailAlertThrottle = 900;
AUTO_CMD_INT(siEmailFailAlertThrottle, EmailFailAlertThrottle);

void GenericSendEmailResultCB(char *pUserData, char *pErrorString)
{
	static U32 siLastAlertTime = 0;
	if (pErrorString)
	{
		if (siLastAlertTime < timeSecondsSince2000() - siEmailFailAlertThrottle)
		{
			siLastAlertTime = timeSecondsSince2000();
			CRITICAL_NETOPS_ALERT("EMAIL_SEND_FAIL",
				"Failed to send email entitled %s because: %s (alert will not be sent again for %d seconds)", 
				pUserData, pErrorString, siEmailFailAlertThrottle);
		}
	}

	free(pUserData);
}



#include "AutoGen/netsmtp_h_ast.c"
#include "autogen/netsmtp_c_ast.c"
