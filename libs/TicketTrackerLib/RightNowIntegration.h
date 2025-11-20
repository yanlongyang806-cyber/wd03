#pragma once

#include "JSONRPC.h"
#define RIGHTNOW_SOAP_ACTOR "RightNowAPI"

typedef struct TicketEntryConst TicketEntryConst;
typedef struct TicketRequestData TicketRequestData;

bool RightNow_IntegrationIsEnabled(void);

void RightNow_UserQueryIncidents(U32 uLinkID, TicketRequestData *data);
void RightNow_ServerQueryIncidents(jsonrpcFinishedCB cb, void *data, const char *columns, const char *account, 
	U32 updateTime, U32 updateDuration, bool bInGameTicket, int iOffset);

AUTO_STRUCT;
typedef struct RNCreateIncidentResponse
{
	int incidentID;
} RNCreateIncidentResponse;
void RightNow_CreateIncident(TicketEntryConst *data);
void RightNowPush_Init(void);
void RightNowPush_Tick(void);

void UpdateTicketsFromRightNow(void);