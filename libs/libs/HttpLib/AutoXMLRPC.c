#include "HttpLib.h"
#include "net.h"
#include "netprivate.h"
#include "netreceive.h"

#include "HttpXpathSupport.h"

// ----------------------------------------------------------------------------------------
// LateLink support for LINK_ALLOW_XMLRPC / LINK_REPURPOSED_XMLRPC NetLinks

// Executables must link against HttpLib in order for LINK_ALLOW_XMLRPC to actually work,
// otherwise they assert on startup if the flag is used.

void OVERRIDE_LATELINK_netcomm_verifyXMLRPCIsLinkedIn(void)
{
	// The default asserts, but since we're linked in here, everything is fine.
}

void OVERRIDE_LATELINK_netreceive_repurposeLinkForXMLRPC(NetLink *link, Packet *pak)
{
	linkSetType(link, LINKTYPE_USES_FULL_SENDBUFFER);
	link->flags |= LINK_RAW|LINK_HTTP|LINK_REPURPOSED_XMLRPC;
	link->raw_data_left = linkGetRawContentLength((char*)pak->data);
	link->connected = 1;
}

void OVERRIDE_LATELINK_netreceive_processXMLRPC(NetLink *link, Packet *pak)
{
	httpProcessPostDataAuthenticated(link, link->user_data, pak, httpProcessXMLRPC);
}

void * OVERRIDE_LATELINK_netreceive_createXMLRPCUserData(void)
{
	return calloc(1, sizeof(HttpClientStateDefault));
}

void OVERRIDE_LATELINK_netlink_cleanupXMLRPCUserData(void *data)
{
	httpCleanupClientState(data);
}
