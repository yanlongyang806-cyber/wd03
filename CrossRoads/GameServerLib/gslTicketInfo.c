#include "gslTicketInfo.h"
#include "gslBugReport.h"
#include "GlobalTypes.h"
#include "gslEntity.h"
#include "ticketnet.h"
#include "GameServerLib.h"
#include "textparser.h"
#include "estring.h"
#include "utilitiesLib.h"
#include "AutoGen/GameClientLib_autogen_ClientCmdWrappers.h"
#include "Player.h"

#include "net/net.h"

extern ParseTable parse_TicketData[];
#define TYPE_parse_TicketData TicketData

void forwardTicketStatusToClient(Entity *ent, const char *ticketResponse)
{
	if (ent)
		ClientCmd_ClientUpdateTicketStatus(ent, ticketResponse);
}
void updateTicketStatusToClient(Entity *ent, const char *ticketResponse)
{
	if (ent)
		ClientCmd_ClientUpdateTicketStatus(ent, ticketResponse);
}

AUTO_COMMAND ACMD_SERVERCMD ACMD_ACCESSLEVEL(0) ACMD_PRIVATE;
void sendTicketStatusRequest(Entity *ent, bool bUpdate)
{
	TicketRequestData ticketData = {0};
	if (ent && ent->pPlayer)
	{
		if (ent->pPlayer->clientLink && ent->pPlayer->clientLink->netLink)
			ticketData.uIP = linkGetSAddr(ent->pPlayer->clientLink->netLink);

		ticketData.pAccountName = ent->pPlayer->privateAccountName;
		if (bUpdate)
			ticketTrackerSendStatusRequestAsync(&ticketData, updateTicketStatusToClient, ent);
		else
			ticketTrackerSendStatusRequestAsync(&ticketData, forwardTicketStatusToClient, ent);
	}
}

void updateTicketLabelsToClient(Entity *ent, const char *ticketResponse)
{
	if (ent)
		ClientCmd_Category_UpdateTicketList(ent, ticketResponse);
}

//AUTO_COMMAND ACMD_SERVERCMD ACMD_ACCESSLEVEL(0)  ACMD_PRIVATE;
//void sendTicketLabelRequest(Entity *ent, char *categoryKey, char *label)
//{
//	if (ent && ent->pPlayer && categoryKey && *categoryKey)
//	{
//		TicketRequestData ticketData = {0};
//		if (ent->pPlayer->clientLink && ent->pPlayer->clientLink->netLink)
//			ticketData.uIP = linkGetSAddr(ent->pPlayer->clientLink->netLink);
//
//		ticketData.pCategory = categoryKey;
//		ticketData.pLabel = label;
//		ticketData.pProduct = GetProductName();
//		ticketTrackerSendLabelRequest(&ticketData, updateTicketLabelsToClient, ent);
//		return;
//	}
//}

AUTO_COMMAND ACMD_SERVERCMD ACMD_ACCESSLEVEL(0) ACMD_PRIVATE;
void sendTicketLabelRequest(Entity *ent, TicketRequestData *ticketData)
{
	if (ent && ent->pPlayer && ticketData)
	{
		if (ent->pPlayer->clientLink && ent->pPlayer->clientLink->netLink)
			ticketData->uIP = linkGetSAddr(ent->pPlayer->clientLink->netLink);

		if (stricmp(ticketData->pCategory, "CBug.Category.InGame.Environment") == 0)
		{
			// Look up the specified target
			EntityRef targetRef = gslTicket_EntityTarget(entGetRef(ent));

			if (targetRef)
			{
				Entity *targetEnt = entFromEntityRefAnyPartition(targetRef);
				if (targetEnt)
				{
					if (ticketData->pLabel)
						free((void*) ticketData->pLabel);
					ticketData->pLabel = strdup(entGetLocalName(targetEnt));
				}
			}
			estrDestroy((char**) &ticketData->pDebugPosString);
		}

		ticketData->pProduct = strdup(GetProductName());
		ticketTrackerSendLabelRequestAsync(ticketData, updateTicketLabelsToClient, ent);
	}
}
