#include "accountCommon.h"
#include "accountnet.h"
#include "chatdb.h"
#include "chatCommonStructs.h"
#include "ChatServer/chatShared.h"
#include "StashTable.h"
#include "estring.h"

#include "XMPP_Chat.h"
#include "XMPP_ChatUtils.h"
#include "XMPP_Gateway.h"
#include "XMPP_Structs.h"
#include "ChatServer/xmppShared.h"
#include "rand.h"
#include "chatCommon.h"
#include "users.h"

#include "AutoGen/XMPP_Structs_h_ast.h"

XmppServerState gXmppState = {0};
XmppServerState * xmpp_getserverstate(void)
{
	return &gXmppState;
}

// Clears all client and other stored information
void XMPP_ClearServerState(void)
{
	int i;
	StashTableIterator iter = {0};
	StashElement elem = NULL;

	stashGetIterator(gXmppState.stXmppNodes, &iter);
	while (stashGetNextElement(&iter, &elem))
	{
		XmppClientNode *node = stashElementGetPointer(elem);
		ChatUser *user = xmpp_chatuser_from_node(node);
		if (user)
			userLogout(user, XMPP_CHAT_ID);
		// No need to remove nodes/states or leave rooms here
	}
	stashTableClearStruct(gXmppState.stXmppNodes, NULL, parse_XmppClientNode);
	stashTableClearStruct(gXmppState.stXmppStates, NULL, parse_XmppClientState);

	for (i=0; i<XMPP_RoomDomain_Max; i++)
	{
		if (gXmppState.stChatRooms[i])
		{
			stashTableClearStruct(gXmppState.stChatRooms[i], NULL, parse_XmppChatRoom);
		}
	}
	gXmppState.xmppInitialized = false;
}

// Return true if this resource is not used by this node.
static bool IsResourceAvailable(XmppClientNode *node, int iThisStateID, const char *name)
{
	if (!name)
		return false;
	if (!node)
		return true;
	EARRAY_CONST_FOREACH_BEGIN(node->resources, i, n);
	{
		XmppClientState *state = node->resources[i];
		if (state->uStateID == iThisStateID || !state->resource)
			continue;
		if (XmppStringEqual(state->resource, strlen(state->resource), name, strlen(name)))
			return false;
	}
	EARRAY_FOREACH_END;
	return true;
}

// Autogenerate a resource suitable for this client.
static char *MakeResource(XmppClientState *state)
{
	const char defaultResource[] = "IM";
	XmppClientNode *node = XMPPChat_ClientNode(state);

	// Try just "IM" first.
	if (IsResourceAvailable(node, state->uStateID, defaultResource))
		return strdup(defaultResource);

	// Search for a random resource.
	for(;;)
	{
		char *resource = strdupf("IM-%x", randomU32());
		if (IsResourceAvailable(node, state->uStateID, resource))
			return resource;
		free(resource);
	}

	// Never get here.
	devassert(0);
	return NULL;
}

XmppClientState *XmppServer_AddClientState(SA_PARAM_NN_VALID ChatUser *user, U32 uStateID, const char *resource)
{
	XmppClientNode *node;
	XmppClientState *state;
	bool bSuccess;
	if (!stashIntFindPointer(gXmppState.stXmppNodes, user->id, &node))
	{
		// Create XMPP node.
		node = StructCreate(parse_XmppClientNode);
		node->uAccountID = user->id;
		bSuccess = stashIntAddPointer(gXmppState.stXmppNodes, user->id, node, false);
		eaIndexedEnable(&node->resources, parse_XmppClientState);
		devassert(bSuccess);
	}
	if (!stashIntFindPointer(gXmppState.stXmppStates, uStateID, &state))
	{
		// Create XMPP state
		state = StructCreate(parse_XmppClientState);
		state->uAccountID = user->id;
		state->uStateID = uStateID;
		bSuccess = stashIntAddPointer(gXmppState.stXmppStates, uStateID, state, false);
		devassert(bSuccess);
	}
	state->uAccountID = user->id;
	state->uStateID = uStateID;
	eaIndexedAdd(&node->resources, state);
	if (IsResourceAvailable(node, state->uStateID, resource))
		state->resource = strdup(resource);
	else
		state->resource = MakeResource(state);
	return state;
}

void XmppServer_RemoveClientState(SA_PARAM_NN_VALID ChatUser *user, XmppClientState *state)
{
	XmppClientNode *node;
	XmppClientState *state_removed;
	// Remove state from node, and remove node if necessary
	if (stashIntFindPointer(gXmppState.stXmppNodes, user->id, &node))
	{
		int idx = eaIndexedFindUsingInt(&node->resources, state->uStateID);
		if (idx >= 0)
			eaRemove(&node->resources, idx);
		if (eaSize(&node->resources) == 0)
		{
			stashIntRemovePointer(gXmppState.stXmppNodes, user->id, NULL);
			StructDestroy(parse_XmppClientNode, node);
		}
	}

	// Remove state from stash
	if (devassert(stashIntRemovePointer(gXmppState.stXmppStates, state->uStateID, &state_removed)))
	{
		devassert(state == state_removed);
		// State/member from ChatRooms should be removed in XMPP_Chat.c:XMPPChat_Logout
	}
	StructDestroy(parse_XmppClientState, state);
}

XmppClientState * XmppServer_FindClientStateById(U32 uStateID)
{
	XmppClientState *state;
	stashIntFindPointer(gXmppState.stXmppStates, uStateID, &state);
	return state;
}

// Get a resource name from a JID, if any.
char *xmpp_make_resource_from_jid(const char *jid)
{
	struct JidComponents components;
	char *resource;

	// Decompose JID.
	if (!XMPP_ValidateJid(jid))
		return NULL;
	components = XMPP_JidDecompose(jid);

	// If there is no resource, return null.
	if (!components.resourcelen)
		return NULL;

	// Copy resource name.
	resource = malloc(components.resourcelen + 1);
	strcpy_s(resource, components.resourcelen + 1, components.resource);
	return resource;
}

// Get a ChatUser that matches a JID, if any.
PlayerInfoStruct *xmpp_playerinfo_from_jid(const char *jid, ChatUser *user)
{
	struct JidComponents components;
	char *node = NULL;

	// Decompose JID.
	if (!XMPP_ValidateJid(jid) || !user)
		return NULL;
	components = XMPP_JidDecompose(jid);

	// Search for this player.
	EARRAY_CONST_FOREACH_BEGIN(user->ppPlayerInfo, i, n);
	const char *name = user->ppPlayerInfo[i]->onlinePlayerName;
	if (strlen(name) == components.nodelen && strncmp(name, components.node, components.nodelen))
		return user->ppPlayerInfo[i];
	EARRAY_FOREACH_END;

	// Not found
	return NULL;
}

// Get a ChatUser that matches a JID, if any.
ChatUser *xmpp_chatuser_from_jid(const char *jid)
{
	struct JidComponents components;
	char *handle = NULL;
	ChatUser *user;

	// Decompose JID.
	if (!XMPP_ValidateJid(jid))
		return NULL;
	components = XMPP_JidDecompose(jid);

	// Get username.
	estrStackCreate(&handle);
	estrConcat(&handle, components.node, (unsigned)components.nodelen);
	estrReplaceOccurrences(&handle, "+", " ");
	user = userFindByHandle(handle);
	estrDestroy(&handle);
	return user;
}

// Get the ChatUser object associated with a XmppClientState object.
ChatUser *xmpp_chatuser_from_clientstate(const XmppClientState *state)
{
	ChatUser *user = userFindByContainerId(state->uAccountID);
	devassert(user);
	return user;
}

// If this user is logged in with XMPP, return their client node.  Otherwise, return NULL.
XmppClientNode *xmpp_node_from_id(U32 uAccountID)
{
	XmppClientNode *found = NULL;

	// Find client node.
	stashIntFindPointer(gXmppState.stXmppNodes, uAccountID, &found);
	if (found && !eaSize(&found->resources))
	{
		devassert(0);
		return NULL;
	}
	return found;
}

XmppClientNode *xmpp_node_from_chatuser(const ChatUser *user)
{
	return xmpp_node_from_id(user->id);
}

// Return the chat user from the client node.
ChatUser *xmpp_chatuser_from_node(const XmppClientNode *node)
{
	ChatUser *user = userFindByContainerId(node->uAccountID);
	devassert(user);
	return user;
}

// If this user is logged in with XMPP, return their client state object.  Otherwise, return NULL.
XmppClientState *xmpp_clientstate_from_chatuser(const ChatUser *user, const char *resource)
{
	XmppClientNode *found;
	int i;

	// Find client node.
	found = xmpp_node_from_chatuser(user);
	if (!found)
		return NULL;

	// If no specific resource was requested, choose the highest-priority resource.
	if (!resource)
		return found->resources[0];

	// Find this resource.
	for (i = 0; i != eaSize(&found->resources); ++i)
	{
		if (!strcmp(found->resources[i]->resource, resource))
			return found->resources[i];
	}

	// This resource was not found.
	return NULL;
}

//typedef struct XmppClientLink XmppClientLink;
//void xmpp_Disconnect(XmppClientLink *link, const char *reason);
//AUTO_COMMAND ACMD_CATEGORY(ChatDebug);
//void xmpp_DisconnectTest(char *handle)
//{
//	ChatUser *user = userFindByHandle(handle);
//	XmppClientNode *found;
//	
//	if (!user)
//		return;
//	found = xmpp_node_from_chatuser(user);
//	if (!found)
//		return;
//	EARRAY_CONST_FOREACH_BEGIN(found->resources, i, n);
//	{
//		xmpp_Disconnect(found->resources[i]->link, "test disco");
//	}
//	EARRAY_FOREACH_END;
//}