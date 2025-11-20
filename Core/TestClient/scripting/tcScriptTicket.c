#if 0

#include "tcScriptInternal.h"
#include "tcScriptShared.h"

#include "ClientControllerLib.h"
#include "EString.h"
#include "GlobalTypeEnum.h"
#include "TestClientCommon.h"

#include "Autogen/GameClientLib_commands_autogen_CommandFuncs.h"

// -------------------------------------------------------------------------
// Ticket tag declaration
// Represents a ticket within the Lua context
// -------------------------------------------------------------------------
GLUA_VAR_FUNC_DECLARE( ticket_category );
GLUA_VAR_FUNC_DECLARE( ticket_summary );
GLUA_VAR_FUNC_DECLARE( ticket_description );

GLUA_TAG_DEFINE( tag_ticket )
{
	GLUA_TAG_VARIABLE( "category", ticket_category, GLUA_VAR_READONLY ),
	GLUA_TAG_VARIABLE( "summary", ticket_summary, GLUA_VAR_READONLY ),
	GLUA_TAG_VARIABLE( "description", ticket_description, GLUA_VAR_READONLY )
}
GLUA_TAG_END( tag_ticket )

GLUA_VAR_FUNC( ticket_category )
{
	TestClientTicket *pTicket = (TestClientTicket *)obj;

	if(!pTicket)
	{
		glua_error("ticket.category: invalid ticket reference\n");
		glua_pushNil();
		break;
	}

	glua_pushInteger(cmd_TestClient_TicketCategory(pTicket->iHandle, pTicket->iID));
}
GLUA_END

GLUA_VAR_FUNC( ticket_summary )
{
	TestClientTicket *pTicket = (TestClientTicket *)obj;

	if(!pTicket)
	{
		glua_error("ticket.summary: invalid ticket reference\n");
		glua_pushNil();
		break;
	}

	glua_pushInteger(cmd_TestClient_TicketSummary(pTicket->iHandle, pTicket->iID));
}
GLUA_END

GLUA_VAR_FUNC( ticket_description )
{
	TestClientTicket *pTicket = (TestClientTicket *)obj;

	if(!pTicket)
	{
		glua_error("ticket.description: invalid ticket reference\n");
		glua_pushNil();
		break;
	}

	glua_pushInteger(cmd_TestClient_TicketDescription(pTicket->iHandle, pTicket->iID));
}
GLUA_END
// -------------------------------------------------------------------------

GLUA_FUNC( ticket_tracker_set )
{
	GLUA_ARG_COUNT_CHECK( ticket_tracker_set, 1, 1 );
	GLUA_ARG_CHECK( ticket_tracker_set, 1, GLUA_TYPE_STRING, false, "" );

	cmd_TestClient_SetTicketTracker(glua_getString(1));
}
GLUA_END

GLUA_FUNC( ticket_search )
{
	char *pCategory = NULL;
	int iHandle;

	// Available categories:
	// Mission, Inventory, Npc, Technical, Ui, Environment, Art, Powers, Audio,
	// Language, Harassment, Cbug, Ipviolation, Other, Gmrequest, Feedback

	GLUA_ARG_COUNT_CHECK( ticket_search, 1, 2 );
	GLUA_ARG_CHECK( ticket_search, 1, GLUA_TYPE_STRING, false, "" );
	GLUA_ARG_CHECK( ticket_search, 2, GLUA_TYPE_STRING, true, "" );

	estrPrintf(&pCategory, "Cbug.Category.%s", glua_getString(1));
	iHandle = cmd_TestClient_SearchTicketCategory(pCategory, glua_argn() > 1 ? glua_getString(2) : NULL, false);

	glua_pushInteger(iHandle);
}
GLUA_END

GLUA_FUNC( ticket_search_hammer )
{
	char *pCategory = NULL;
	int i;

	GLUA_ARG_COUNT_CHECK( ticket_search_hammer, 2, 3 );
	GLUA_ARG_CHECK( ticket_search_hammer, 1, GLUA_TYPE_INT, true, "" );
	GLUA_ARG_CHECK( ticket_search_hammer, 2, GLUA_TYPE_STRING, true, "" );
	GLUA_ARG_CHECK( ticket_search_hammer, 3, GLUA_TYPE_STRING, false, "" );

	estrPrintf(&pCategory, "Cbug.Category.%s", glua_getString(2));

	for(i = 0; i < glua_getInteger(1); ++i)
	{
		cmd_TestClient_SearchTicketCategory(pCategory, glua_argn() > 2 ? glua_getString(3) : NULL, true);
	}
}
GLUA_END

GLUA_FUNC( ticket_next )
{
	TestClientTicket ticket;

	GLUA_ARG_COUNT_CHECK( ticket_next, 1, 1 );
	GLUA_ARG_CHECK( ticket_next, 1, GLUA_TYPE_INT, false, "" );

	ticket.iHandle = glua_getInteger(1);
	ticket.iID = cmd_TestClient_TicketResponseNext(ticket.iHandle);

	if(ticket.iID == -1)
	{
		glua_pushNil();
		break;
	}

	glua_pushUserdata_copy(&ticket, tag_ticket);
}
GLUA_END

GLUA_FUNC( ticket_search_close )
{
	GLUA_ARG_COUNT_CHECK( ticket_search_close, 1, 1 );
	GLUA_ARG_CHECK( ticket_search_close, 1, GLUA_TYPE_INT, false, "" );

	cmd_TestClient_TicketResponseClose(glua_getInteger(1));
}
GLUA_END

GLUA_FUNC( ticket_send )
{
	char *pCategory = NULL;
	const char *pSummary;
	const char *pDescription;

	GLUA_ARG_COUNT_CHECK( ticket_send, 1, 3 );
	GLUA_ARG_CHECK( ticket_send, 1, GLUA_TYPE_STRING, false, "" );
	GLUA_ARG_CHECK( ticket_send, 2, GLUA_TYPE_STRING, true, "" );
	GLUA_ARG_CHECK( ticket_send, 3, GLUA_TYPE_STRING, true, "" );

	estrPrintf(&pCategory, "Cbug.Category.%s", glua_getString(1));
	pSummary = glua_argn() > 1 ? glua_getString(2) : NULL;
	pDescription = glua_argn() > 2 ? glua_getString(3) : NULL;

	cmd_TestClient_SendTicket(pCategory, pSummary, pDescription);
}
GLUA_END

GLUA_FUNC( ticket_send_hammer )
{
	char *pCategory = NULL;
	const char *pSummary;
	const char *pDescription;
	int i;

	GLUA_ARG_COUNT_CHECK( ticket_send, 2, 4 );
	GLUA_ARG_CHECK( ticket_send, 2, GLUA_TYPE_STRING, false, "" );
	GLUA_ARG_CHECK( ticket_send, 3, GLUA_TYPE_STRING, true, "" );
	GLUA_ARG_CHECK( ticket_send, 4, GLUA_TYPE_STRING, true, "" );

	estrPrintf(&pCategory, "Cbug.Category.%s", glua_getString(2));
	pSummary = glua_argn() > 2 ? glua_getString(3) : NULL;
	pDescription = glua_argn() > 3 ? glua_getString(4) : NULL;

	for(i = 0; i < glua_getInteger(1); ++i)
	{
		cmd_TestClient_SendTicket(pCategory, pSummary, pDescription);
	}
}
GLUA_END

void TestClient_InitTicket(LuaContext *pContext)
{
	lua_State *_lua_ = pContext->luaState;
	int namespaceTbl = _glua_create_tbl(_lua_, "tc", 0);

	GLUA_DECL( namespaceTbl )
	{
		glua_func( ticket_tracker_set ),
		glua_func( ticket_search ),
		glua_func( ticket_search_hammer ),
		glua_func( ticket_next ),
		glua_func( ticket_search_close ),
		glua_func( ticket_send ),
		glua_func( ticket_send_hammer ),
		glua_tag( "ticket", &tag_ticket ),
		glua_tagdispatch( tag_ticket )
	}
	GLUA_DECL_END
}

#endif