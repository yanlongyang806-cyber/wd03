#include "ticketnet.h"
#include "ticketnet_h_ast.h"
#include "ticketnet_c_ast.h"
#include "AutoGen/ticketenums_h_ast.h"

#include "estring.h"
#include "net/net.h"
#include "timing.h"
#include "trivia.h"
#include "sysutil.h"
#include "../serverlib/pub/serverlib.h"
#include "Organization.h"
#include "ContinuousBuilderSupport.h"
#include "wininclude.h"

#include "AutoGen/AppLocale_h_ast.h"

AUTO_RUN_ANON(memBudgetAddMapping(__FILE__, BUDGET_GameSystems););

static char szTicketTracker[128] = ORGANIZATION_DOMAIN;
static NetComm *comm = NULL;
static bool sbTicketTrackerWasSet = false;
AUTO_COMMAND ACMD_EARLYCOMMANDLINE ACMD_ACCESSLEVEL(0) ACMD_HIDE;
void setTicketTracker(const char *pTicketTracker)
{
	sbTicketTrackerWasSet = true;
	strcpy(szTicketTracker, pTicketTracker);
}

AUTO_COMMAND ACMD_ACCESSLEVEL(0) ACMD_HIDE;
const char * getTicketTracker(void)
{
	if (g_isContinuousBuilder)
	{
		return "localhost";
	}
	return szTicketTracker;
}

bool ticketTrackerWasSet(void)
{
	return sbTicketTrackerWasSet;
}

TicketData * getTicketDataFromPacket(Packet *pkt)
{
	TicketData *pTicketData = StructCreate(parse_TicketData);
	char *pParseString = pktGetStringTemp(pkt);
	if(!ParserReadText(pParseString, parse_TicketData, pTicketData, PARSER_IGNORE_ALL_UNKNOWN | PARSER_NOERROR_ONIGNORE))
	{
		StructDestroy(parse_TicketData, pTicketData);
		return NULL;
	}

	return pTicketData;
}

bool putUserDataIntoTicket (TicketData *ticket, void *pStruct, ParseTable *pti)
{
	char *structString = NULL;
	char *ptiString = NULL;

	ParserWriteText(&structString, pti, pStruct, 0, 0, 0);
	ParseTableWriteText(&ptiString, pti, ParserGetTableName(pti), 0);

	if (!structString || !ptiString)// || strlen(structString) > TICKET_MAX_ENTITY_LEN || strlen(ptiString) > TICKET_MAX_ENTITY_LEN)
	{
		estrDestroy(&structString);
		estrDestroy(&ptiString);
		return false;
	}
	eaPush(&ticket->ppUserDataStr, structString);
	eaPush(&ticket->ppUserDataPTIStr, ptiString);
	return true;
}

void putTicketDataIntoPacket(Packet *pkt, TicketData *pTicketData)
{
	char *pOutputParseText = NULL;

	estrAllocaCreate(&pOutputParseText, 2048);
	ParserWriteText(&pOutputParseText, parse_TicketData, pTicketData, 0, 0, 0);
	pktSendString(pkt, pOutputParseText);
	estrDestroy(&pOutputParseText);
}

static bool bTrackerResponseReceived = false;
static int iTicketTrackerErrorFlags = 0;
static char pTicketTrackerErrorMsg[256] = "";

int GetTicketTrackerErrorFlags(void)
{
	return iTicketTrackerErrorFlags;
	//return 0;
}

const char * GetTicketTrackerErrorMsg(void)
{
	return pTicketTrackerErrorMsg;
	//return 0;
}

///////////////////////////////////////
// Ticket Sending and Receiving

AUTO_ENUM;
typedef enum TicketSendStatus
{
	Ticket_Unknown = 0,
	Ticket_ConnectWait = 1, 
	Ticket_ResponseWait,
	Ticket_Done,
} TicketSendStatus;

AUTO_ENUM;
typedef enum TicketSendType
{
	TICKETTYPE_DEFAULT = 0,
	TICKETTYPE_EDIT,
	TICKETTYPE_CLOSE,
} TicketSendType;

// Time is in ticks
#define WAIT_FOR_TRACKER_CONNECT_TIMEOUT 5000
// Wait 30s for response from Ticket Tracker
#define WAIT_FOR_TRACKER_RESPONSE_TIMEOUT 30000

AUTO_STRUCT;
typedef struct TicketSendInternalStruct
{
	U32 uID;
	TicketSendStatus eStatus;
	DWORD uStartTime;

	TicketSendType eType;
	NetLink *link; NO_AST
	TicketData *pData; NO_AST
	
	TicketReturnCallback cbResponse; NO_AST
	U32 uErrorFlags;
	char *trackerResponse;
} TicketSendInternalStruct;

static TicketSendInternalStruct **sppTicketQueue = NULL;

static bool RemoveTicketFromQueue(TicketSendInternalStruct *ticket, const char *msg, int result)
{
	int idx = eaFindAndRemove(&sppTicketQueue, ticket);
	U32 uTicketID = 0;

	if (result == TICKETFLAGS_SUCCESS)
	{
		if (ticket->trackerResponse)
			uTicketID = atoi(ticket->trackerResponse);
		if (uTicketID)
			msg = NULL;
		else if (ticket->pData->iMergeID)
			uTicketID = ticket->pData->iMergeID;
	}
	else if (result == TICKETFLAGS_ERROR && ticket->trackerResponse && stricmp(ticket->trackerResponse, "CTicket.TicketResubscribed") == 0)
	{
		// Handling data from older ticket trackers
		uTicketID = ticket->pData->iMergeID;
		result = ticket->uErrorFlags = TICKETFLAGS_SUCCESS; 
	}

	// Callback responsible for freeing data if present, else free it locally
	if (ticket->cbResponse)
		ticket->cbResponse(ticket->pData, msg, uTicketID, result);
	else
		StructDestroy(parse_TicketData, ticket->pData);
	ticket->pData = NULL;

	linkRemove(&ticket->link);
	StructDestroy(parse_TicketSendInternalStruct, ticket);
	return (idx >= 0);
}

void ProcessTicketSendQueue(void)
{
	int i, size;
	DWORD uTime = GetTickCount();

	size = eaSize(&sppTicketQueue);
	if (!comm || !size)
		return;
		
	PERFINFO_AUTO_START_FUNC();

	commMonitor(comm);
	for (i=0; i<size;)
	{
		TicketSendInternalStruct *ticket = sppTicketQueue[i];
		bool bRemovedTicket = false;
		switch(ticket->eStatus)
		{
		case Ticket_ConnectWait:
			{
				if (linkConnected(ticket->link))
				{
					switch (ticket->eType)
					{
					case TICKETTYPE_EDIT:
						{
							Packet *pPak = pktCreate(ticket->link, TO_TICKETTRACKER_TICKET_EDIT);
							putTicketDataIntoPacket(pPak, ticket->pData);
							pktSend(&pPak);
						}
					xcase TICKETTYPE_CLOSE:
						{
							Packet *pPak = pktCreate(ticket->link, TO_TICKETTRACKER_TICKET_CLOSE);
							putTicketDataIntoPacket(pPak, ticket->pData);
							pktSend(&pPak);
						}
					xdefault:
						{
							Packet *pPak = pktCreate(ticket->link, TO_TICKETTRACKER_ERROR);
							putTicketDataIntoPacket(pPak, ticket->pData);
							if (ticket->pData->uImageSize)
								pktSendBytes(pPak, ticket->pData->uImageSize, (void*) ticket->pData->imageBuffer);
							pktSend(&pPak);
						}
					}

					ticket->eStatus = Ticket_ResponseWait;
					ticket->uStartTime = GetTickCount();
				}
				else if (linkDisconnected(ticket->link))
				{
					ticket->uErrorFlags = TICKETFLAGS_ERROR;
					bRemovedTicket = RemoveTicketFromQueue(ticket, "CTicket.Failure.Disconnected", TICKETFLAGS_CONNECTION_ERROR);
				}
				else if (uTime - ticket->uStartTime > WAIT_FOR_TRACKER_CONNECT_TIMEOUT)
				{
					ticket->uErrorFlags = TICKETFLAGS_ERROR;
					bRemovedTicket = RemoveTicketFromQueue(ticket, "CTicket.Failure.ConnectTimeout", TICKETFLAGS_CONNECTION_ERROR);
				}
			}
		xcase Ticket_ResponseWait:
			if (uTime - ticket->uStartTime > WAIT_FOR_TRACKER_RESPONSE_TIMEOUT)
			{
				ticket->uErrorFlags = TICKETFLAGS_ERROR;
				bRemovedTicket = RemoveTicketFromQueue(ticket, "CTicket.Failure.Timeout", TICKETFLAGS_CONNECTION_ERROR);
			}
		xcase Ticket_Done:
			bRemovedTicket = RemoveTicketFromQueue(ticket, ticket->trackerResponse, ticket->uErrorFlags);
		xdefault:
			ticket->uErrorFlags = TICKETFLAGS_ERROR;
			bRemovedTicket = RemoveTicketFromQueue(ticket, "CTicket.FailureUnknown", TICKETFLAGS_CONNECTION_ERROR);
			break;
		}
		if (bRemovedTicket)
			size--;
		else
			i++;
	}
	
	PERFINFO_AUTO_STOP();
}

static void ReceiveMsgAsync(Packet *pkt, int cmd, NetLink* link, TicketSendInternalStruct *ticket)
{
	switch(cmd)
	{
	case FROM_TICKETTRACKER_ERRFLAGS:
		{
			ticket->eStatus = Ticket_Done;
			ticket->uErrorFlags = pktGetU32(pkt);
			ticket->trackerResponse = strdup(pktGetStringTemp(pkt));
		}
		break;
	};
}
static void ReceiveMsg(Packet *pkt, int cmd, NetLink* link, void *data)
{
	switch(cmd)
	{
	case FROM_TICKETTRACKER_ERRFLAGS:
		{
			bTrackerResponseReceived = true;
			iTicketTrackerErrorFlags = pktGetU32(pkt);
			pktGetString(pkt, pTicketTrackerErrorMsg, ARRAY_SIZE_CHECKED(pTicketTrackerErrorMsg));
		}
		break;
	};
}

int ticketTrackerGetLastID(void)
{
	if (iTicketTrackerErrorFlags == TICKETFLAGS_SUCCESS)
		return atoi(pTicketTrackerErrorMsg);
	return 0;
}

__forceinline static bool ticketTrackerAddTicketToQueue(TicketSendInternalStruct *pTicket)
{
	if (!comm)
	{
		comm = commCreate(0,0); // Must be non-threaded!
		commSetSendTimeout(comm, 10.0f);
	}

	pTicket->link = commConnect(comm, LINKTYPE_UNSPEC, LINK_NO_COMPRESS|LINK_FORCE_FLUSH, 
		getTicketTracker(), DEFAULT_TICKET_TRACKER_PORT, ReceiveMsgAsync, 0, 0, sizeof(TicketSendInternalStruct));
	if (!pTicket->link)
	{
		// Callback responsible for freeing data if present, else free it locally
		if (pTicket->cbResponse)
			pTicket->cbResponse(pTicket->pData, "CTicket.Failure.Connect", 0, TICKETFLAGS_CONNECTION_ERROR);
		else
			StructDestroy(parse_TicketData, pTicket->pData);
		StructDestroy(parse_TicketSendInternalStruct, pTicket);
		return false;
	}
	pTicket->uStartTime = GetTickCount();
	linkSetUserData(pTicket->link, pTicket);
	eaPush(&sppTicketQueue, pTicket);
	return true;
}

bool ticketTrackerSendTicketAsync(TicketData *pTicketData, TicketReturnCallback cbResponse)
{
	TicketSendInternalStruct *pTicket = StructCreate(parse_TicketSendInternalStruct);
	pTicketData->uImageSize = 0;
	pTicket->pData = pTicketData;
	pTicket->cbResponse = cbResponse;
	pTicket->eStatus = Ticket_ConnectWait;

	return ticketTrackerAddTicketToQueue(pTicket);
}

bool ticketTrackerSendTicketPlusScreenshotAsync(TicketData *pTicketData, void *data, TicketReturnCallback cbResponse)
{
	TicketSendInternalStruct *pTicket = StructCreate(parse_TicketSendInternalStruct);
	pTicketData->imageBuffer = data;
	pTicket->pData = pTicketData;
	pTicket->cbResponse = cbResponse;
	pTicket->eStatus = Ticket_ConnectWait;

	return ticketTrackerAddTicketToQueue(pTicket);
}

bool ticketTrackerSendTicketEditAsync(TicketData *pTicketData, TicketReturnCallback cbResponse)
{
	TicketSendInternalStruct *pTicket = StructCreate(parse_TicketSendInternalStruct);
	pTicket->eType = TICKETTYPE_EDIT;
	pTicket->pData = pTicketData;
	pTicket->cbResponse = cbResponse;
	pTicket->eStatus = Ticket_ConnectWait;

	return ticketTrackerAddTicketToQueue(pTicket);
}

bool ticketTrackerSendTicketCloseAsync(TicketData *pTicketData, TicketReturnCallback cbResponse)
{
	TicketSendInternalStruct *pTicket = StructCreate(parse_TicketSendInternalStruct);
	pTicket->eType = TICKETTYPE_CLOSE;
	pTicket->pData = pTicketData;
	pTicket->cbResponse = cbResponse;
	pTicket->eStatus = Ticket_ConnectWait;

	return ticketTrackerAddTicketToQueue(pTicket);
}

bool ticketTrackerSendTicket(TicketData *pTicketData)
{
	NetLink *pTicketTrackerLink;
	Packet *pPak;
	DWORD start_tick;

	if (!comm)
	{
		comm = commCreate(0,0); // Must be non-threaded!
		commSetSendTimeout(comm, 10.0f);
	}

	pTicketData->uImageSize = 0;
	pTicketTrackerLink = commConnect(comm, LINKTYPE_UNSPEC, LINK_NO_COMPRESS|LINK_FORCE_FLUSH, 
		getTicketTracker(), DEFAULT_TICKET_TRACKER_PORT, ReceiveMsg,0,0,0);
	if (!linkConnectWait(&pTicketTrackerLink,2.f))
	{
		return false;
	}

	// ---------------------------------------------------------------------------------
	// Clear buffers and Send Ticket!
	bTrackerResponseReceived = false;
	iTicketTrackerErrorFlags = 0;
	pTicketTrackerErrorMsg[0] = 0;

	pPak = pktCreate(pTicketTrackerLink, TO_TICKETTRACKER_ERROR);
	putTicketDataIntoPacket(pPak, pTicketData);
	pktSend(&pPak);
	commMonitor(comm);

	// ---------------------------------------------------------------------------------
	// Wait for a few seconds to see if the server wants a dump from us, and to get error flags
	start_tick = GetTickCount();
	while(!bTrackerResponseReceived)
	{
		DWORD tick = GetTickCount();
		if(tick > (start_tick + WAIT_FOR_TRACKER_RESPONSE_TIMEOUT))
		{
			break;
		}

		Sleep(1);
		commMonitor(comm);
	}

	linkRemove(&pTicketTrackerLink);
	return bTrackerResponseReceived;
}

bool ticketTrackerSendTicketPlusScreenshot(TicketData *pTicketData, void *data)
{
	NetLink *pTicketTrackerLink;
	Packet *pPak;
	DWORD start_tick;
	
	assert(pTicketData->uImageSize > 0);
	if (!comm)
	{
		comm = commCreate(0,0); // Must be non-threaded!
		commSetSendTimeout(comm, 10.0f);
	}

	pTicketTrackerLink = commConnect(comm, LINKTYPE_UNSPEC, LINK_NO_COMPRESS|LINK_FORCE_FLUSH, 
		getTicketTracker(), DEFAULT_TICKET_TRACKER_PORT, ReceiveMsg, 0, 0, 0);
	if (!linkConnectWait(&pTicketTrackerLink,2.f))
	{
		return false;
	}

	bTrackerResponseReceived = false;
	iTicketTrackerErrorFlags = 0;
	pTicketTrackerErrorMsg[0] = 0;

	pPak = pktCreate(pTicketTrackerLink, TO_TICKETTRACKER_ERROR);
	putTicketDataIntoPacket(pPak, pTicketData);
	pktSendBytes(pPak, pTicketData->uImageSize,  data);
	pktSend(&pPak);
	commMonitor(comm);

	start_tick = GetTickCount();
	while(!bTrackerResponseReceived)
	{
		DWORD tick = GetTickCount();
		if(tick > (start_tick + WAIT_FOR_TRACKER_RESPONSE_TIMEOUT))
			break;
		Sleep(1);
		commMonitor(comm);
	}
	linkRemove(&pTicketTrackerLink);
	return bTrackerResponseReceived;
}

//////////////////////////////////////////
// Ticket Requests

AUTO_STRUCT;
typedef struct TicketRequestInternalStruct
{
	U32 uID;
	DWORD start_tick;
	TicketSendStatus eStatus;
	NetLink *link; NO_AST

	char *pRequestString; AST(ESTRING)
	bool bMyTicketRequest;

	char *categoryKey;
	char *label;

	char *pTicketStatusString; AST(ESTRING)
	TicketStatusRequestCallback pCallback; NO_AST
	UserData userData; NO_AST
} TicketRequestInternalStruct;

//static bool bRequestReponseReceived = false;
//static TicketRequestResponseList *spTicketStatusList = NULL;
//static char *spTicketStatusString = NULL;
#define WAIT_FOR_REQUEST_CONNECT_TIMEOUT 2000
#define WAIT_FOR_REQUEST_RESPONSE_TIMEOUT 10000

static TicketRequestInternalStruct **sppTicketRequests = NULL;
static int siPendingRequestCount = 0;

CRITICAL_SECTION gTicketRequestAccess;
CRITICAL_SECTION gTicketLabelAccess;
static NetComm * sTicketRequestComm = NULL;
static bool sbTicketRequestsInitialized = false;
static char *spTicketResponseString = NULL;

static void InitializeTicketRequests(void)
{	
	sTicketRequestComm = commCreate(0,0); // Must be non-threaded!
	commSetSendTimeout(sTicketRequestComm, 10.0f);
	
	InitializeCriticalSection(&gTicketRequestAccess);
	InitializeCriticalSection(&gTicketLabelAccess);
	sbTicketRequestsInitialized = true;
}

static bool RemoveTicketRequestFromQueue(TicketRequestInternalStruct *request, const char *response)
{
	int idx = eaFindAndRemove(&sppTicketRequests, request);
	if (request->pCallback)
		request->pCallback(request->userData, response);
	linkRemove(&request->link);
	StructDestroy(parse_TicketRequestInternalStruct, request);
	return (idx >= 0);
}


void ProcessTicketRequestQueue(void)
{
	int i, size;
	DWORD tick = GetTickCount();

	if (!sTicketRequestComm)
		return;
		
	PERFINFO_AUTO_START_FUNC();

	commMonitor(sTicketRequestComm);
	EnterCriticalSection(&gTicketRequestAccess);
	size = eaSize(&sppTicketRequests);
	for (i=0; i<size;)
	{
		TicketRequestInternalStruct *request = sppTicketRequests[i];
		bool bRemovedTicket = false;

		switch(request->eStatus)
		{
		case Ticket_ConnectWait:
			{
				if (linkConnected(request->link))
				{
					// TODO
					Packet *pPak;
					if (request->bMyTicketRequest)
						pPak = pktCreate(request->link, TO_TICKETTRACKER_REQUEST_STATUS);
					else
						pPak = pktCreate(request->link, TO_TICKETTRACKER_REQUEST_LABEL);
					pktSendString(pPak, request->pRequestString);
					pktSend(&pPak);
					request->start_tick = GetTickCount();
					request->eStatus = Ticket_ResponseWait;
				}
				else if (linkDisconnected(request->link))
				{
					bRemovedTicket = RemoveTicketRequestFromQueue(request, "CTicket.Failure");
				}
				else if (tick - request->start_tick > WAIT_FOR_REQUEST_CONNECT_TIMEOUT)
				{
					bRemovedTicket = RemoveTicketRequestFromQueue(request, "CTicket.Failure");
				}
			}
		xcase Ticket_ResponseWait:
			if (linkDisconnected(request->link))
			{
				bRemovedTicket = RemoveTicketRequestFromQueue(request, "CTicket.FailureLostConnection");
			}
			else if (tick - request->start_tick > WAIT_FOR_REQUEST_RESPONSE_TIMEOUT)
			{
				bRemovedTicket = RemoveTicketRequestFromQueue(request, "CTicket.FailureTimeout");
			}
		xcase Ticket_Done:
			bRemovedTicket = RemoveTicketRequestFromQueue(request, request->pTicketStatusString);
		xdefault:
			bRemovedTicket = RemoveTicketRequestFromQueue(request, "CTicket.TicketFailure");
			break;
		}
		if (bRemovedTicket)
			size--;
		else
			i++;
	}
	LeaveCriticalSection(&gTicketRequestAccess);
	
	PERFINFO_AUTO_STOP();
}

// caller should lock gTicketRequestAccess critical section
static TicketRequestInternalStruct * findTicketRequest(U32 uResponseID)
{
	int i, size;
	
	size = eaSize(&sppTicketRequests);
	for (i=0; i<size; i++)
	{
		if (sppTicketRequests[i]->uID == uResponseID)
			return sppTicketRequests[i];
	}
	return NULL;
}

static void ReceiveStatusMsgAsync(Packet *pkt,int cmd,NetLink* link,void *user_data)
{
	switch(cmd)
	{
	case FROM_TICKETTRACKER_STATUS:
		{
			U32 uResponseID = pktGetU32(pkt);
			TicketRequestInternalStruct * pTRS;
			
			EnterCriticalSection(&gTicketRequestAccess);
			pTRS = findTicketRequest(uResponseID);
			if (pTRS)
			{
				estrCopy2(&pTRS->pTicketStatusString, pktGetStringTemp(pkt));
				pTRS->eStatus = Ticket_Done;
			}
			LeaveCriticalSection(&gTicketRequestAccess);
		}
		break;
	default:
		{
			// nothing
		}
	};
}

static void ReceiveStatusMsg(Packet *pkt,int cmd,NetLink* link,void *user_data)
{
	switch(cmd)
	{
	case FROM_TICKETTRACKER_STATUS:
		{
			U32 uResponseID = pktGetU32(pkt);
		
			bTrackerResponseReceived = true;
			//pktGetString(pkt, pTicketTrackerErrorMsg, ARRAY_SIZE_CHECKED(pTicketTrackerErrorMsg));
			estrCopy2(&spTicketResponseString, pktGetStringTemp(pkt));
		}
		break;
	default:
		{
			// nothing
		}
	};
}

static U32 suTicketRequestID = 1;
void ticketTrackerSendStatusRequestAsync(TicketRequestData *pTicketData, TicketStatusRequestCallback pCallback, UserData userData)
{
	NetLink *pTicketTrackerLink;
	TicketRequestInternalStruct *pData;

	// ---------------------------------------------------------------------------
	if (!sbTicketRequestsInitialized)
	{
		InitializeTicketRequests();
	}

	pTicketTrackerLink = commConnect(sTicketRequestComm, LINKTYPE_UNSPEC, LINK_NO_COMPRESS|LINK_FORCE_FLUSH, 
		getTicketTracker(), DEFAULT_TICKET_TRACKER_PORT, ReceiveStatusMsgAsync,0,0,0);

	// ---------------------------------------------------------------------------------
	// Send Ticket!

	pData = calloc(1, sizeof(TicketRequestInternalStruct));
	pData->eStatus = Ticket_ConnectWait;
	pData->link = pTicketTrackerLink;
	pData->start_tick = GetTickCount();
	pData->pCallback = pCallback;
	pData->userData = userData;
	pData->uID = pTicketData->uID = suTicketRequestID++;
	pData->bMyTicketRequest = true;

	ParserWriteText(&pData->pRequestString, parse_TicketRequestData, pTicketData, 0, 0, 0);

	EnterCriticalSection(&gTicketRequestAccess);
	eaPush(&sppTicketRequests, pData);
	LeaveCriticalSection(&gTicketRequestAccess);
}

//#define TICKET_LABEL_CACHE_SIZE 10
//#define TICKET_LABEL_CACHE_AGE 5000 // in milliseconds/ticks
//static TicketRequestInternalStruct **sppTicketLabelCache = NULL;
//
//static TicketRequestInternalStruct *findTicketLabelInCache(const char *categoryKey, const char *label)
//{
//	int i, size;
//
//	if (!categoryKey || !label)
//		return NULL;
//	size = eaSize(&sppTicketLabelCache);
//
//	for (i=0; i<size; i++)
//	{
//		if (sppTicketLabelCache[i]->categoryKey && sppTicketLabelCache[i]->label &&
//			stricmp(sppTicketLabelCache[i]->categoryKey, categoryKey) == 0 &&
//			stricmp(sppTicketLabelCache[i]->label, label) == 0)
//			return sppTicketLabelCache[i];
//	}
//	return NULL;
//}

void ticketTrackerSendLabelRequestAsync(TicketRequestData *pTicketData, TicketStatusRequestCallback pCallback, UserData userData)
{
	//TicketRequestInternalStruct *cache = findTicketLabelInCache(categoryKey, label);
	//DWORD curTick = GetTickCount();

	//if (cache && curTick - cache->start_tick < TICKET_LABEL_CACHE_AGE)
	//{
	//	// TODO return the cache
	//	return;
	//}
	NetLink *pTicketTrackerLink;
	TicketRequestInternalStruct *pData;

	// ---------------------------------------------------------------------------
	if (!sbTicketRequestsInitialized)
	{
		InitializeTicketRequests();
	}

	pTicketTrackerLink = commConnect(sTicketRequestComm, LINKTYPE_UNSPEC, LINK_NO_COMPRESS|LINK_FORCE_FLUSH, 
		getTicketTracker(), DEFAULT_TICKET_TRACKER_PORT, ReceiveStatusMsgAsync,0,0,0);

	// ---------------------------------------------------------------------------------
	// Send Ticket!

	{
		pData = calloc(1, sizeof(TicketRequestInternalStruct));
		pData->eStatus = Ticket_ConnectWait;
		pData->link = pTicketTrackerLink;
		pData->start_tick = GetTickCount();
		pData->pCallback = pCallback;
		pData->userData = userData;
		pData->uID = pTicketData->uID = suTicketRequestID++;

		ParserWriteText(&pData->pRequestString, parse_TicketRequestData, pTicketData, 0, 0, 0);

		EnterCriticalSection(&gTicketRequestAccess);
		eaPush(&sppTicketRequests, pData);
		LeaveCriticalSection(&gTicketRequestAccess);
	}
}

void ticketTrackerSendLabelRequest(TicketRequestData *pTicketData, TicketStatusRequestCallback pCallback, UserData userData)
{
	char *pOutputParseString = NULL;
	Packet *pPak;
	NetLink *pTicketTrackerLink;
	DWORD start_tick;

	// ---------------------------------------------------------------------------
	if (!sbTicketRequestsInitialized)
	{
		InitializeTicketRequests();
	}

	pTicketTrackerLink = commConnect(sTicketRequestComm, LINKTYPE_UNSPEC, LINK_NO_COMPRESS|LINK_FORCE_FLUSH, 
		getTicketTracker(), DEFAULT_TICKET_TRACKER_PORT, ReceiveStatusMsg,0,0,0);	
	if (!linkConnectWait(&pTicketTrackerLink,2.f))
	{
		if (pCallback)
			pCallback(userData, ""); // TODO error msg
		return;
	}
	// ---------------------------------------------------------------------------------
	// Send Ticket!

	bTrackerResponseReceived = false;
	iTicketTrackerErrorFlags = 0;
	pTicketTrackerErrorMsg[0] = 0;
	pPak = pktCreate(pTicketTrackerLink, TO_TICKETTRACKER_REQUEST_LABEL);
	
	ParserWriteText(&pOutputParseString, parse_TicketRequestData, pTicketData, 0, 0, 0);
	pktSendString(pPak, pOutputParseString);
	pktSend(&pPak);
	estrDestroy(&pOutputParseString);

	commMonitor(sTicketRequestComm);
	start_tick = GetTickCount();

	start_tick = GetTickCount();
	while(!bTrackerResponseReceived)
	{
		DWORD tick = GetTickCount();
		if(tick > (start_tick + WAIT_FOR_REQUEST_RESPONSE_TIMEOUT))
			break;
		Sleep(1);
		commMonitor(sTicketRequestComm);
	}
	linkRemove(&pTicketTrackerLink);
	if (pCallback)
		pCallback(userData, spTicketResponseString);
	estrDestroy(&spTicketResponseString);
}

//////////////////////////////////////////
// Ticket Priority calculations

void TicketResponse_CalculatePriority(TicketRequestResponse *response, U32 uCurrentTime)
{
	if (stricmp(response->pMainCategory, "CBug.CategoryMain.Stickies") == 0)
	{
		response->fPriority = FLT_MAX;
	}
	else
	{
		U32 uTimeDifference = uCurrentTime - response->uLastTime;
		response->uDaysAgo = uTimeDifference / SECONDS_PER_DAY; // convert to days

		response->fPriority = ((F32) response->uCount) / (uTimeDifference + 1); 
	}
}

int TicketResponse_SortPriority(const TicketRequestResponse **p1, const TicketRequestResponse **p2, const void *ign)
{
	// Higher priority shows up first
	if      ((*p1)->fPriority < (*p2)->fPriority) return  1;
	else if ((*p1)->fPriority > (*p2)->fPriority) return -1;
	else return 0;
}

void TicketResponse_CalculateAndSortByPriority(TicketRequestResponse **ppResponses)
{
	U32 uCurrentTime = timeSecondsSince2000();
	int size = eaSize(&ppResponses);
	int i;

	for (i=0; i<size; i++)
		TicketResponse_CalculatePriority(ppResponses[i], uCurrentTime);

	eaStableSort(ppResponses, NULL, TicketResponse_SortPriority);
}

#include "ticketnet_h_ast.c"
#include "ticketnet_c_ast.c"