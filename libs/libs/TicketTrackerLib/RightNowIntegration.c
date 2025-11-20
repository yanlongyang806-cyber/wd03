#include "RightNowIntegration.h"
#include "AutoGen/RightNowIntegration_h_ast.h"
#include "AutoGen/RightNowIntegration_c_ast.h"

#include "ManageTickets.h"
#include "ProductNames.h"
#include "TicketAPI.h"
#include "TicketEntry.h"
#include "TicketTracker.h"
#include "TicketTrackerConfig.h"
#include "ticketenums.h"

#include "Alerts.h"
#include "csv.h"
#include "earray.h"
#include "error.h"
#include "EString.h"
#include "GlobalComm.h"
#include "JSONRPC.h"
#include "net.h"
#include "objContainer.h"
#include "objTransactions.h"
#include "StringUtil.h"
#include "SuperAssert.h"
#include "ticketnet.h"
#include "TimedCallback.h"
#include "timing.h"
#include "trivia.h"
#include "utilitiesLib.h"
#include "websrv.h"
#include "winutil.h"

#include "AutoGen/AppLocale_h_ast.h"
#include "AutoGen/TicketTrackerLib_autotransactions_autogen_wrappers.h"
#include "AutoGen/TicketEntry_h_ast.h"
#include "AutoGen/ticketenums_h_ast.h"
#include "AutoGen/ticketnet_h_ast.h"
#include "AutoGen/websrv_h_ast.h"

bool gbEnableRightNowIntegration = false;
AUTO_CMD_INT(gbEnableRightNowIntegration, EnableRN);

bool RightNow_IntegrationIsEnabled(void)
{
	const TicketTrackerConfig *config = GetTicketTrackerConfig();
	return gbEnableRightNowIntegration && WEBSRV_IS_CONFIGURED(config);
}

static void RightNow_JSONConnectFailAlert(void)
{
	static U32 suLastAlertTime = 0;
	U32 uCurTime;

	uCurTime = timeSecondsSince2000();
	// Alerts at most once every 5 minutes
	if (uCurTime - suLastAlertTime < 300)
	{
		TriggerAlert("TICKETTRACKER_WEBSRV_CONNECT_FAIL", "Failed to connect to WebSrv for sending JSON request.", 
			ALERTLEVEL_CRITICAL, ALERTCATEGORY_NETOPS, 0, 0, 0, 0, 0, NULL, 0);
		suLastAlertTime = uCurTime;
	}
}

// --------------------------------------
// Queries (Objects and CSV)

typedef struct RNQueryRequest
{
	DWORD timeRequested;
	U32 uID;
	U32 uLinkID;
} RNQueryRequest;

static void RightNow_UserQueryIncidentsCB(JSONRPCState *state, RNQueryRequest *requestData)
{
	if (state->error)
	{
		Alertf("WebSrv Query JSON RPC Error: %s.", state->error);
	}
	else if (requestData)
	{
		NetLink *link = linkFindByID(requestData->uLinkID);
		DWORD elapsed;
		elapsed = GetTickCount() - requestData->timeRequested;

		// TODO(Theo) change this to logging; make TT talk to GlobalLogServer
		verbose_printf("Query JSON RPC Success - %d ms\n", elapsed);
		if (link && linkConnected(link))
		{
			TicketRequestResponseList *pResponseList = (TicketRequestResponseList*) state->result;
			char *pOutputParse = NULL;
			Packet *pak = pktCreate(link, FROM_TICKETTRACKER_STATUS);
			TicketRequestResponseWrapper *pWrapper = StructCreate(parse_TicketRequestResponseWrapper);

			ParserWriteTextSafe(&pWrapper->pListString, &pWrapper->pTPIString, &pWrapper->uCRC, 
				parse_TicketRequestResponseList, pResponseList, 0, 0, 0);
			ParserWriteText(&pOutputParse, parse_TicketRequestResponseWrapper, pWrapper, 0, 0, 0);
			StructDestroy(parse_TicketRequestResponseWrapper, pWrapper);
		
			pktSendU32(pak, requestData->uID);
			pktSendString(pak, pOutputParse);
			pktSend(&pak);
			estrDestroy(&pOutputParse);
		}
	}
	if (requestData)
		free(requestData);
}

void RightNow_UserQueryIncidents(U32 uLinkID, TicketRequestData *data)
{
	const TicketTrackerConfig *config = GetTicketTrackerConfig();
	RNQueryRequest *request;
	WebSrvKeyValueList list = {0};
	JSONRPCState *state;
	
	websrvKVList_Add(&list, "account", data->pPWAccountName);

	assertmsgf(WEBSRV_IS_CONFIGURED(config), "No WebSrv configured.");
	request = malloc(sizeof(RNQueryRequest));
	request->uID = data->uID;
	request->uLinkID = uLinkID;
	request->timeRequested = GetTickCount();
	state = jsonrpcCreate(ticketTrackerCommDefault(), config->pWebSrvAddress, config->iWebSrvPort, "/rpc/", 
		RightNow_UserQueryIncidentsCB, request, parse_TicketRequestResponseList, "RightNow.query_incidents", 3, 
		JT_INTEGER, GetRightNowUserQueryLimit(), 
		JT_STRING, nullStr(config->pRightNowProductNameOverride) ? "" : config->pRightNowProductNameOverride,
		JT_OBJECT, parse_WebSrvKeyValueList, &list);
	if (!state)
		RightNow_JSONConnectFailAlert();
	StructDeInit(parse_WebSrvKeyValueList, &list);
}

void RightNow_ServerQueryIncidents(jsonrpcFinishedCB cb, void *data, const char *columns, const char *account, 
	U32 updateTime, U32 updateDuration, bool bInGameTicket, int iOffset)
{
	const TicketTrackerConfig *config = GetTicketTrackerConfig();
	WebSrvKeyValueList list = {0};
	JSONRPCState *state;
	
	if (account && *account)
		websrvKVList_Add(&list, "account", account);
	if (updateTime)
	{
		websrvKVList_Addf(&list, "updateTimeStart", "%d", updateTime);
		if (updateDuration)
			websrvKVList_Addf(&list, "updateTimeEnd", "%d", updateTime + updateDuration);
	}
	if (bInGameTicket)
		websrvKVList_Add(&list, "cryptic_ingame", NULL);

	websrvKVList_Add(&list, "columns", columns);

	assertmsgf(WEBSRV_IS_CONFIGURED(config), "No WebSrv configured.");
	state = jsonrpcCreate(ticketTrackerCommDefault(), config->pWebSrvAddress, config->iWebSrvPort, "/rpc/", 
		cb, data, parse_RightNowCSVResponse, "RightNow.query_incidents_csv", 4, 
		JT_INTEGER, GetRightNowQueryLimit(), 
		JT_INTEGER, iOffset,
		JT_STRING, nullStr(config->pRightNowProductNameOverride) ? "" : config->pRightNowProductNameOverride,
		JT_OBJECT, parse_WebSrvKeyValueList, &list);
	if (!state)
		RightNow_JSONConnectFailAlert();
	StructDeInit(parse_WebSrvKeyValueList, &list);
}

// --------------------------------------
// Creating Incidents

#define RIGHTNOW_PUSH_RETRY_RATE (3)
static CONTAINERID_EARRAY seaiQueuedTickets = NULL;
static CONTAINERID_EARRAY seaiFailedTickets = NULL;

AUTO_TRANSACTION ATR_LOCKS(pEntry, ".uFlags");
enumTransactionOutcome trSetRightNowPushFlag(ATR_ARGS, NOCONST(TicketEntryConst) *pEntry, U32 uFlags)
{
	pEntry->uFlags &= ~(TICKET_FLAG_RIGHTNOW_QUEUED | TICKET_FLAG_RIGHTNOW_FAILED);
	if (uFlags)
		pEntry->uFlags |= uFlags;
	return TRANSACTION_OUTCOME_SUCCESS;
}

static void RightNow_CreateIncidentCB(JSONRPCState *state, void * userData)
{
	U32 uTicketID = (U32)(intptr_t) userData;
	if (state->error)
	{
		Alertf("Create JSON RPC Error: %s.", state->error);
		AutoTrans_trSetRightNowPushFlag(NULL, objServerType(), GLOBALTYPE_TICKETENTRY, uTicketID, TICKET_FLAG_RIGHTNOW_FAILED);
		eaiPush(&seaiFailedTickets, uTicketID);
	}
	else
	{
		RNCreateIncidentResponse *result = (RNCreateIncidentResponse*) state->result;
		if (result)
			verbose_printf("Create JSON RPC Success: %d\n", result->incidentID);
		else
			verbose_printf("Create JSON RPC Error: Nothing?\n");
		AutoTrans_trSetRightNowPushFlag(NULL, objServerType(), GLOBALTYPE_TICKETENTRY, uTicketID, 0);
	}
	eaiFindAndRemove(&seaiQueuedTickets, uTicketID);
}

void RightNow_CreateIncident(TicketEntryConst *data)
{
	WebSrvKeyValueList list = {0};
	const TicketTrackerConfig *config = GetTicketTrackerConfig();
	char *systemSpecs = NULL;
	char *gfxSettings = NULL;
	char *pandoraLink = NULL;
	JSONRPCState *state;

	if (!RightNow_IntegrationIsEnabled())
		return;
	if (!eaSize(&data->ppUserInfo))
		return;

	eaiPush(&seaiQueuedTickets, data->uID);
	AutoTrans_trSetRightNowPushFlag(NULL, objServerType(), GLOBALTYPE_TICKETENTRY, data->uID, TICKET_FLAG_RIGHTNOW_QUEUED);
		
	// Should be PWE Account name
	websrvKVList_Add(&list, "account", data->ppUserInfo[0]->pPWAccountName);
	if (data->ppUserInfo[0]->pCharacterName)
		websrvKVList_Add(&list, "character", data->ppUserInfo[0]->pCharacterName);
	if (data->pSummary)
		websrvKVList_Add(&list, "summary", data->pSummary);
	if (data->pUserDescription)
		websrvKVList_Add(&list, "description", data->pUserDescription);
	websrvKVList_Add(&list, "language", StaticDefineIntRevLookup(LanguageEnum, data->eLanguage));
	if (data->pProductName)
	{
		if (nullStr(config->pRightNowProductNameOverride))
			websrvKVList_Add(&list, "product", data->pProductName);
		else
		{
			char *summary = NULL;
			// Product Override adds the actual product name to the summary
			estrStackCreate(&summary);
			estrPrintf(&summary, "TTracker%s [%s] %s", config->pRightNowProductNameOverride, 
				productNameGetShortDisplayName(data->pProductName),
				data->pSummary ? data->pSummary : "");
			websrvKVList_Add(&list, "summary", summary);
			websrvKVList_Add(&list, "product", config->pRightNowProductNameOverride);
			estrDestroy(&summary);
		}
	}
	if (data->ppUserInfo[0]->pShardInfoString)
	{
		const char *shardName = GetShardValueFromInfoStringByKey(data->ppUserInfo[0]->pShardInfoString, "name");
		if (shardName)
			websrvKVList_Add(&list, "shard", shardName);
	}
	if (data->pMainCategory && *data->pMainCategory)
	{
		websrvKVList_Add(&list, "maincategory", data->pMainCategory);
		if (data->pCategory && *data->pCategory)
			websrvKVList_Add(&list, "category", data->pCategory);
	}

	EARRAY_FOREACH_BEGIN(data->ppTriviaData, i);
	{
		TriviaData *trivia = data->ppTriviaData[i];
		const char *name;
		if (name = strstri(trivia->pKey, "GfxSettings:"))
		{
			if (trivia->pVal && *trivia->pVal)
			{
				if (gfxSettings)
					estrConcatf(&gfxSettings, "\n%s: %s", name, trivia->pVal);
				else
					estrPrintf(&gfxSettings, "%s: %s", name, trivia->pVal);
			}
		}
		else if (name = strstri(trivia->pKey, "SystemSpecs:"))
		{
			if (trivia->pVal && *trivia->pVal)
			{
				if (systemSpecs)
					estrConcatf(&systemSpecs, "\n%s: %s", name, trivia->pVal);
				else
					estrPrintf(&systemSpecs, "%s: %s", name, trivia->pVal);
			}
		}
	}
	EARRAY_FOREACH_END;
	if (gfxSettings)
		websrvKVList_Add(&list, "gfxsettings", gfxSettings);
	if (systemSpecs)
		websrvKVList_Add(&list, "systemspecs", systemSpecs);
	estrDestroy(&gfxSettings);
	estrDestroy(&systemSpecs);

	// For Pandora References
	GetPandoraTicketLink(&pandoraLink, data->uID);
	websrvKVList_Add(&list, "pandora_ticket", pandoraLink);
	GetPandoraAccountLink(&pandoraLink, data->ppUserInfo[0]->pAccountName); // Cryptic account
	websrvKVList_Add(&list, "pandora_account", pandoraLink);
	if (data->ppUserInfo[0]->uCharacterID)
	{
		GetPandoraCharacterLink(&pandoraLink, GetProductNameFromShardInfoString(data->ppUserInfo[0]->pShardInfoString), data->ppUserInfo[0]->uCharacterID);
		websrvKVList_Add(&list, "pandora_character", pandoraLink);
	}

	state = jsonrpcCreate(ticketTrackerCommDefault(), config->pWebSrvAddress, config->iWebSrvPort, "/rpc/", 
		RightNow_CreateIncidentCB, (void*)(intptr_t) data->uID, parse_RNCreateIncidentResponse, "RightNow.create_incident", 1, 
		JT_OBJECT, parse_WebSrvKeyValueList, &list);
	if (!state)
	{
		RightNow_JSONConnectFailAlert();
		AutoTrans_trSetRightNowPushFlag(NULL, objServerType(), GLOBALTYPE_TICKETENTRY, data->uID, TICKET_FLAG_RIGHTNOW_FAILED);
	}
	StructDeInit(parse_WebSrvKeyValueList, &list);
}

// Initialization on TT restart to change "queued" to "failed" and seed failed earrays
void RightNowPush_Init(void)
{
	ContainerIterator iter = {0};
	Container *currCon = NULL;

	objInitContainerIteratorFromType(GLOBALTYPE_TICKETENTRY, &iter);
	while (currCon = objGetNextContainerFromIterator(&iter))
	{
		TicketEntry *pEntry = CONTAINER_ENTRY(currCon);
		if (pEntry->uFlags & TICKET_FLAG_RIGHTNOW_QUEUED)
		{
			AutoTrans_trSetRightNowPushFlag(NULL, objServerType(), GLOBALTYPE_TICKETENTRY, pEntry->uID, TICKET_FLAG_RIGHTNOW_FAILED);
			eaiPush(&seaiFailedTickets, pEntry->uID);
		}
		else if (pEntry->uFlags & TICKET_FLAG_RIGHTNOW_FAILED)
		{
			eaiPush(&seaiFailedTickets, pEntry->uID);
		}
	}
	objClearContainerIterator(&iter);
}

void RightNowPush_Tick(void)
{
	int count = 0;

	if (!RightNow_IntegrationIsEnabled())
		return;

	EARRAY_INT_CONST_FOREACH_BEGIN(seaiFailedTickets, i, n);
	{
		TicketEntryConst *pEntry = CONTAINER_RECONST(TicketEntryConst, findTicketEntryByID(seaiFailedTickets[i]));
		RightNow_CreateIncident(pEntry);
		count++;
		if (count >= RIGHTNOW_PUSH_RETRY_RATE)
			break;
	}
	EARRAY_FOREACH_END;

	eaiRemoveRange(&seaiFailedTickets, 0, count);
}

// --------------------------------------
// Updating Tickets

#define RIGHTNOW_TICKET_UPDATE_COLUMNS "ID,CustomFields.c.pandora_ticket,StatusWithType.Status,Category,CustomFields.c.character"
// Return fields should be:
// Incident ID, Ticket ID, Status, Main Category, Sub-category

static bool sbUpdatesRunning = false;

AUTO_STRUCT;
typedef struct RightNowIncidentUpdateFields
{
	U32 id; // RN Incident ID
	U32 ticketID; // TicketEntry ID
	TicketStatus eStatus;
	char *pMainCategory;
	char *pCategory;
} RightNowIncidentUpdateFields;

AUTO_STRUCT;
typedef struct RightNowCSVResponse
{
	char *pName;
	char *pColumns;
	EARRAY_OF(RightNowIncidentUpdateFields) eaRows;
} RightNowCSVResponse;

typedef struct RightNowUpdateData
{
	U32 uStartTime;
	U32 uSearchStart;
	U32 uSearchPeriod;
} RightNowUpdateData;

AUTO_TRANSACTION ATR_LOCKS(pEntry, ".eStatus, .pMainCategory, .pCategory, .ppLog");
enumTransactionOutcome trUpdateTicketEntry(ATR_ARGS, NOCONST(TicketEntryConst) *pEntry, NON_CONTAINER RightNowIncidentUpdateFields *update)
{
	char *logString = NULL;
	if (update->eStatus != TICKETSTATUS_UNKNOWN)
		pEntry->eStatus = update->eStatus;
	if (!nullStr(update->pMainCategory))
	{
		if (stricmp_safe(pEntry->pMainCategory, update->pMainCategory) || 
			stricmp_safe(pEntry->pCategory, update->pCategory))
		{
			SAFE_FREE(pEntry->pCategory);
			SAFE_FREE(pEntry->pMainCategory);
			pEntry->pMainCategory = StructAllocString(update->pMainCategory);
			if (!nullStr(update->pCategory))
				pEntry->pCategory = StructAllocString(update->pCategory);
		}
	}
	estrPrintf(&logString, "RN Update - %s,%s,%s", getStatusString(update->eStatus), update->pMainCategory, update->pCategory);
	trh_AddGenericLog(pEntry, RIGHTNOW_SOAP_ACTOR, logString);
	estrDestroy(&logString);
	return TRANSACTION_OUTCOME_SUCCESS;
}

static void RightNow_TicketUpdateCB(JSONRPCState *state, RightNowUpdateData *data);
static void RightNow_TimedUpdateCB(TimedCallback *callback, F32 timeSinceLastCallback, RightNowUpdateData *data)
{
	RightNow_ServerQueryIncidents(RightNow_TicketUpdateCB, data, RIGHTNOW_TICKET_UPDATE_COLUMNS, NULL, 
		data->uSearchStart, data->uSearchPeriod, true, 0);
}

static bool sbTempDisableQueryLimit = false;
#define RIGHTNOW_MINIMUM_QUERY_PERIOD (300)
static void RightNow_TicketUpdateCB(JSONRPCState *state, RightNowUpdateData *data)
{
	if (state->error)
	{
		Alertf("Update JSON RPC - Error: %s.", state->error);
	}
	else
	{
		RightNowCSVResponse *pCSV = (RightNowCSVResponse*) state->result;
		if (pCSV)
		{
			U32 uSearchEndTime;

			if (eaSize(&pCSV->eaRows) == GetRightNowQueryLimit() && !sbTempDisableQueryLimit)
			{
				// Max limit on returns means we might miss changes
				U32 uOldQueryPeriod = TTSavedValues_GetQueryPeriod();
				data->uSearchPeriod = uOldQueryPeriod * 0.75;
				if (data->uSearchPeriod < RIGHTNOW_MINIMUM_QUERY_PERIOD)
				{
					data->uSearchPeriod = RIGHTNOW_MINIMUM_QUERY_PERIOD;
					sbTempDisableQueryLimit = true;
					TriggerAlertf("RN_UPDATE_FAIL", ALERTLEVEL_WARNING, ALERTCATEGORY_PROGRAMMER, 0, 0, 0, 0, 0, NULL, 0, 
						"RightNow updates returned more than the max number of tickets for minumum query period.");
				}
				verbose_printf("Update JSON RPC - Too many returns, changing query period to %d\n", data->uSearchPeriod);
				TTSavedValues_ModifyQueryPeriod(data->uSearchPeriod);
				TimedCallback_Run(RightNow_TimedUpdateCB, data, 1);
				return;
			}

			verbose_printf("Update JSON RPC - Success\n");
		
			EARRAY_FOREACH_BEGIN(pCSV->eaRows, i);
			{
				RightNowIncidentUpdateFields *update = pCSV->eaRows[i];
				TicketEntry *pEntry = findTicketEntryByID(update->ticketID);

				if (pEntry)
				{
					AutoTrans_trUpdateTicketEntry(NULL, objServerType(), GLOBALTYPE_TICKETENTRY, pEntry->uID, update);
				}
			}
			EARRAY_FOREACH_END;
			verbose_printf("Received %d entries.\n", eaSize(&pCSV->eaRows));
			
			uSearchEndTime = data->uSearchStart + data->uSearchPeriod;
			if (uSearchEndTime >= data->uStartTime)
			{
				uSearchEndTime = data->uStartTime;
				TTSavedValues_ModifyUpdateTime(uSearchEndTime);
			}
			else
			{
				TTSavedValues_ModifyUpdateTime(uSearchEndTime);
				data->uSearchStart = uSearchEndTime;
				TimedCallback_Run(RightNow_TimedUpdateCB, data, 2);
				return;
			}
		}
		else
			Alertf("Update JSON RPC - No Response.");
	}
	sbUpdatesRunning = false;
	free(data);
}

void UpdateTicketsFromRightNow(void)
{
	if (sbUpdatesRunning)
	{
		TriggerAlertf("RN_UPDATE_FAIL", ALERTLEVEL_WARNING, ALERTCATEGORY_NETOPS, 0, 0, 0, 0, 0, NULL, 0, 
			"RightNow updates triggered while previous update still running.");
		return;
	}
	if (RightNow_IntegrationIsEnabled())
	{
		RightNowUpdateData *data = malloc(sizeof(RightNowUpdateData));
		data->uStartTime = timeSecondsSince2000();
		data->uSearchStart = TTSavedValues_GetUpdateTime();
		data->uSearchPeriod = TTSavedValues_GetQueryPeriod();
		if (!data->uSearchPeriod)
		{
			data->uSearchPeriod = 3600;
			TTSavedValues_ModifyQueryPeriod(data->uSearchPeriod);
		}
		sbUpdatesRunning = true;
		RightNow_ServerQueryIncidents(RightNow_TicketUpdateCB, data, RIGHTNOW_TICKET_UPDATE_COLUMNS, NULL, 
			data->uSearchStart, data->uSearchPeriod, true, 0);
	}
}

AUTO_COMMAND ACMD_CATEGORY(TicketTracker_Debug);
void Cmd_ForceUpdateTickets(U32 uTicketsUpdatedSinceTime)
{
	if (!RightNow_IntegrationIsEnabled())
		return;
	if (uTicketsUpdatedSinceTime)
		TTSavedValues_ModifyUpdateTime(uTicketsUpdatedSinceTime);
	UpdateTicketsFromRightNow();
}

#include "AutoGen/RightNowIntegration_h_ast.c"
#include "AutoGen/RightNowIntegration_c_ast.c"