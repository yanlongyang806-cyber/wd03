#include "ticketnet.h"
#include "estring.h"
#include "Message.h"
#include "StringFormat.h"
#include "GameClientLib.h"
#include "Autogen/GameServerLib_autogen_ServerCmdWrappers.h"

#include "UIGen.h"
#include "Expression.h"
#include "GameStringFormat.h"
#include "NotifyCommon.h"

AUTO_RUN_ANON(memBudgetAddMapping(__FILE__, BUDGET_GameSystems););

extern ParseTable parse_TicketRequestResponse[];
#define TYPE_parse_TicketRequestResponse TicketRequestResponse
extern ParseTable parse_TicketRequestResponseWrapper[];
#define TYPE_parse_TicketRequestResponseWrapper TicketRequestResponseWrapper
extern ParseTable parse_TicketRequestResponseList[];
#define TYPE_parse_TicketRequestResponseList TicketRequestResponseList
// -----------------------------------------------------------------------------------

static TicketRequestResponse **sppTickets = NULL;
static TicketRequestResponse *sSelectedTicket = NULL;
static bool sbWaitingForResponse = false;

/*static void displayHasResponse(struct UIList *uiList, struct UIListColumn *col, UI_MY_ARGS, F32 z, CBox *pBox, int index, void *drawData)
{
	TicketRequestResponse *pResponse = ((TicketRequestResponse**)(*uiList->peaModel))[index];
	AtlasTex *checkTex = (g_ui_Tex.checkBoxChecked);
	CBox box;
	F32 width = checkTex->width * scale;
	F32 height = checkTex->height * scale;
	F32 xPos = x + (col->fWidth*0.5f) - (width*0.5f);
	F32 yPos = y + (uiList->fRowHeight*0.5f) - (height*0.5f);
	BuildIntCBox(&box, xPos, yPos, width, height);
	if (pResponse->pResponse && strlen(pResponse->pResponse))
		display_sprite_box((g_ui_Tex.checkBoxChecked), &box, z, 0xFFFFFFFF);
	else
		display_sprite_box((g_ui_Tex.checkBoxUnchecked), &box, z, 0xFFFFFFFF);
}*/

// ---------------------------------------------
AUTO_COMMAND ACMD_CLIENTCMD ACMD_ACCESSLEVEL(0) ACMD_PRIVATE;
void ClientUpdateTicketStatus(char *ticketResponse)
{
	TicketRequestResponseWrapper *pWrapper;
	TicketRequestResponseList *pList;
	int i;

	if (!ticketResponse)
	{
		return;
	}

	if (strStartsWith(ticketResponse, "CTicket"))
	{
		char *failString = NULL;
		FormatGameMessageKey(&failString, ticketResponse, STRFMT_END);
		notify_NotifySend(NULL, kNotifyType_TicketError, failString, NULL, NULL);
		estrDestroy(&failString);
		return;
	}

	pWrapper = StructCreate(parse_TicketRequestResponseWrapper);
	if (!ParserReadText(ticketResponse, parse_TicketRequestResponseWrapper, pWrapper, 0))
	{
		StructDestroy(parse_TicketRequestResponseWrapper, pWrapper);
		return;
	}

	pList = StructCreate(parse_TicketRequestResponseList);
	if (!pWrapper->pListString || !pWrapper->pTPIString ||
		!ParserReadTextSafe(pWrapper->pListString, pWrapper->pTPIString, pWrapper->uCRC, parse_TicketRequestResponseList, pList, 0))
	{
		StructDestroy(parse_TicketRequestResponseWrapper, pWrapper);
		StructDestroy(parse_TicketRequestResponseList, pList);
		return;
	}

	sSelectedTicket = NULL;
	eaDestroyStruct(&sppTickets, parse_TicketRequestResponse);
	eaCopyStructs(&pList->ppTickets, &sppTickets, parse_TicketRequestResponse);

	for (i=eaSize(&sppTickets)-1; i>=0; i--)
	{
		if (sppTickets[i]->pDescription)
		{
			estrCopy2(&sppTickets[i]->pDescriptionBreak, sppTickets[i]->pDescription);
			estrReplaceOccurrences(&sppTickets[i]->pDescriptionBreak, "\n", "<br>");
		}
		if (sppTickets[i]->pResponse)
		{
			estrCopy2(&sppTickets[i]->pResponseBreak, sppTickets[i]->pResponse);
			estrReplaceOccurrences(&sppTickets[i]->pResponseBreak, "\n", "<br>");
		}
	}

	sbWaitingForResponse = false;
	StructDestroy(parse_TicketRequestResponseWrapper, pWrapper);
	StructDestroy(parse_TicketRequestResponseList, pList);

	{
		UIGen *pGen = ui_GenFind("MyTickets_MainList", kUIGenTypeList);
		if (pGen)
		{
			ui_GenSetList(pGen, &sppTickets, parse_TicketRequestResponse);
			ui_GenMarkDirty(pGen);
		}
	}
}

AUTO_EXPR_FUNC(UIGen) ACMD_NAME("MyTickets_Update");
void MyTickets_UpdateRequest(void)
{
	sSelectedTicket = NULL;
	sbWaitingForResponse = true;
	ServerCmd_sendTicketStatusRequest(true);
}

AUTO_EXPR_FUNC(UIGen) ACMD_NAME("MyTickets_GetList");
void MyTickets_GetList(SA_PARAM_NN_VALID UIGen *pGen)
{
	ui_GenSetList(pGen, &sppTickets, parse_TicketRequestResponse);
}


AUTO_EXPR_FUNC(UIGen) ACMD_NAME("MyTickets_SelectTicket");
void MyTickets_Select(SA_PARAM_OP_VALID TicketRequestResponse * ticket)
{
	sSelectedTicket = ticket;
}

AUTO_EXPR_FUNC(UIGen) ACMD_NAME("MyTicketsSelected_GetData");
const char *MyTickets_GetAndFormatData(ExprContext *pContext, const char *pchDescription)
{
	static char *s_pchDescription = NULL;
	estrClear(&s_pchDescription);
	if (!sSelectedTicket)
	{
		return TranslateMessageKeyDefault("Ticket.NoTicketSelected", "[No Ticket Selected]");
	}
	FormatMessageKey(&s_pchDescription, pchDescription, STRFMT_STRUCT("TicketData", sSelectedTicket, parse_TicketRequestResponse), STRFMT_END);
	return s_pchDescription;
}

AUTO_EXPR_FUNC(UIGen) ACMD_NAME("MyTickets_ClearSelection");
void MyTickets_SelectClear(void)
{
	sSelectedTicket = NULL;
}

AUTO_EXPR_FUNC(UIGen) ACMD_NAME("MyTickets_GetStatus");
const char *MyTickets_GetStatusString(ExprContext *pContext)
{
	return sbWaitingForResponse ? TranslateMessageKeyDefault("Ticket.Refresh.Text", "Refreshing...") : "";
}

AUTO_EXPR_FUNC(UIGen) ACMD_NAME("MyTickets_HasResponse");
SA_RET_NN_STR const char * MyTickets_HasResponse(SA_PARAM_NN_VALID TicketRequestResponse *ticket)
{
	if (ticket->pResponse && *(ticket->pResponse))
		return TranslateMessageKeySafe("Yes");
	return "--";
}
