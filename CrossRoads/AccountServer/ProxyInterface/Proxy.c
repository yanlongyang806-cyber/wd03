#include "Proxy.h"
#include "earray.h"
#include "sock.h"
#include "net/net.h"
#include "logging.h"
#include "MemTrack.h"
#include "StringUtil.h"
#include "timing.h"

#define ACCOUNT_PROXY_INVALID_PROTOCOL_VERSION U32_MAX
#define PROXY_INFO_FORMAT "[p:%p n:%p v:%u]"

/************************************************************************/
/* Types                                                                */
/************************************************************************/

typedef struct Proxy
{
	NetLink *pLink;
	U32 uProtocolVersion;
	U32 uLastSeen; // SS2000
	char *pProxyName;
	char *pClusterName;
	char *pEnvironmentName;
} Proxy;


/************************************************************************/
/* Globals                                                              */
/************************************************************************/

static EARRAY_OF(Proxy) geaProxies = NULL;


/************************************************************************/
/* Private functions                                                    */
/************************************************************************/

static void destroyProxy(SA_PARAM_NN_VALID Proxy *pProxy)
{
	if (!verify(pProxy)) return;

#define destroyProxyRemoveMessage "\tAccount Proxy " PROXY_INFO_FORMAT " destroyed.\n"
	log_printf(LOG_ACCOUNT_PROXY, destroyProxyRemoveMessage, pProxy, pProxy->pLink, pProxy->uProtocolVersion);
	printf(destroyProxyRemoveMessage, pProxy, pProxy->pLink, pProxy->uProtocolVersion);
#undef destroyProxyRemoveMessage

	devassert(eaFindAndRemove(&geaProxies, pProxy) != -1);

	pProxy->pLink = NULL;
	pProxy->uProtocolVersion = ACCOUNT_PROXY_INVALID_PROTOCOL_VERSION;
	if (pProxy->pProxyName)
		free(pProxy->pProxyName);
	if (pProxy->pClusterName)
		free(pProxy->pClusterName);
	free(pProxy);
}

/************************************************************************/
/* Public functions                                                     */
/************************************************************************/

// Add a new proxy
void addProxy(SA_PARAM_NN_VALID NetLink *pLink, U32 uProtocolVersion)
{
	Proxy *pProxy = NULL;

	if (!verify(pLink)) return;
	if (!verify(uProtocolVersion != ACCOUNT_PROXY_INVALID_PROTOCOL_VERSION)) return;
	
	if (!devassertmsg(!findProxyByLink(pLink), "Attempt to add a proxy twice!")) return;

	pProxy = callocStruct(Proxy);

	if (!devassert(pProxy)) return;

	pProxy->pLink = pLink;
	pProxy->uProtocolVersion = uProtocolVersion;
	eaPush(&geaProxies, pProxy);
	linkSetUserData(pLink, pProxy);

#define addProxyAddMessage "\tAccount Proxy " PROXY_INFO_FORMAT " created.\n"
	log_printf(LOG_ACCOUNT_PROXY, addProxyAddMessage, pProxy, pLink, uProtocolVersion);
	printf(addProxyAddMessage, pProxy, pLink, uProtocolVersion);
#undef addProxyAddMessage
}

// Find a proxy
Proxy *findProxyByLink(NetLink *pLink)
{
	Proxy *pProxy = NULL;
	
	if (!verify(pLink)) return NULL;

	pProxy = linkGetUserData(pLink);

	if (pProxy)
	{
		if (!devassert(pProxy->pLink == pLink) ||
			!devassert(pProxy->uProtocolVersion != ACCOUNT_PROXY_INVALID_PROTOCOL_VERSION))
		{
			return NULL;
		}
	}

	return pProxy;
}

// Find a proxy by name
Proxy *findProxyByName(const char *pName)
{
	Proxy *pProxy = NULL;

	if (!verify(pName)) return NULL;

	EARRAY_CONST_FOREACH_BEGIN(geaProxies, iCurProxy, iNumProxies);
	{
		Proxy *pCurProxy = geaProxies[iCurProxy];

		if (!devassert(pCurProxy)) continue;

		if (!stricmp_safe(pCurProxy->pProxyName, pName))
		{
			pProxy = pCurProxy;
			break;
		}
	}
	EARRAY_FOREACH_END;

	if (pProxy)
	{
		devassert(pProxy->uProtocolVersion != ACCOUNT_PROXY_INVALID_PROTOCOL_VERSION);
	}

	return pProxy;
}

// Remove a proxy
void disconnectProxy(Proxy *pProxy)
{
	if (!verify(pProxy)) return;

	if (!devassert(findProxyByLink(pProxy->pLink) == pProxy)) return;

	linkSetUserData(pProxy->pLink, NULL);

	if (!pProxy->pProxyName)
	{
		destroyProxy(pProxy);
	}
	else
	{
		pProxy->uLastSeen = timeSecondsSince2000();
		pProxy->pLink = NULL;
	}
}

// Get the link of a proxy
NetLink *getProxyNetLink(Proxy *pProxy)
{
	if (!verify(pProxy)) return NULL;

	return pProxy->pLink;
}

// Set the name of a proxy
void setProxyName(Proxy *pProxy, const char *pName)
{
	Proxy *pOldProxy = NULL;

	if (!verify(pProxy)) return;

	if (!stricmp_safe(pProxy->pProxyName, pName)) return;

	pOldProxy = findProxyByName(pName);

	if (pOldProxy)
	{
		if (pOldProxy->pLink)
		{
			AssertOrAlert("ACCOUNTSERVER_PROXY_DOUBLE_CONNECT", "An account proxy with the name %s has attempted to connect, even though one with that name was already connected.  The old one will be disconnected.", pName);
			linkShutdown(&pOldProxy->pLink);
			free(pOldProxy->pProxyName);
			pOldProxy->pProxyName = NULL;
		}
		else
		{
			destroyProxy(pOldProxy);
		}

		pOldProxy = NULL;
	}

	if (pProxy->pProxyName)
	{
		AssertOrAlert("ACCOUNTSERVER_PROXY_RENAME", "An account proxy with the name %s has been renamed to %s.", pProxy->pProxyName, pName);
		free(pProxy->pProxyName);
	}

	pProxy->pProxyName = strdup(pName);
}

// Set the name of a proxy's cluster
void setProxyCluster(Proxy *pProxy, const char *pName)
{
	if (pProxy->pClusterName)
		free(pProxy->pClusterName);

	pProxy->pClusterName = strdup(pName);
}

void setProxyEnvironment(Proxy *pProxy, const char *pName)
{
	if (pProxy->pEnvironmentName)
		free(pProxy->pEnvironmentName);

	pProxy->pEnvironmentName = strdup(pName);
}

// Get the name of a proxy
SA_RET_NN_STR const char * getProxyName(SA_PARAM_NN_VALID const Proxy * pProxy)
{
	return pProxy->pProxyName;
}

// Get the name of a proxy's cluster
SA_RET_NN_STR const char * getProxyCluster(SA_PARAM_NN_VALID const Proxy *pProxy)
{
	return pProxy->pClusterName;
}

SA_RET_NN_STR const char * getProxyEnvironment(SA_PARAM_NN_VALID const Proxy *pProxy)
{
	return pProxy->pEnvironmentName;
}

// Get the last seen time of a proxy
U32 getProxyLastSeenTime(Proxy *pProxy)
{
	if (!verify(pProxy)) return 0;

	if (pProxy->pLink) return timeSecondsSince2000();

	if (devassert(pProxy->uLastSeen)) return pProxy->uLastSeen;

	return 0;
}

// Call a callback for each proxy
void forEachProxy(ProxyProcessor pCallback, void *pUserData)
{
	if (!verify(pCallback)) return;

	EARRAY_CONST_FOREACH_BEGIN(geaProxies, iCurProxy, iNumProxies);
	{
		Proxy *pProxy = geaProxies[iCurProxy];

		if (!devassert(pProxy)) continue;

		pCallback(pProxy, pUserData);
	}
	EARRAY_FOREACH_END;
}

// Returns true if the proxy connection is fully initialized
bool proxyHandshakeCompleted(SA_PARAM_NN_VALID const Proxy * pProxy)
{
	if (!verify(pProxy)) return false;

	if (pProxy->uProtocolVersion != ACCOUNT_PROXY_INVALID_PROTOCOL_VERSION &&
		getProxyName(pProxy))
	{
		return true;
	}

	return false;
}