#pragma once

typedef struct NetLink NetLink;
typedef struct Proxy Proxy;

typedef void (*ProxyProcessor)(SA_PARAM_NN_VALID Proxy *pProxy, SA_PARAM_OP_VALID void *pUserData);

// Add a new proxy
void addProxy(SA_PARAM_NN_VALID NetLink *pLink, U32 uProtocolVersion);

// Find a proxy
SA_RET_OP_VALID Proxy *findProxyByLink(SA_PARAM_NN_VALID NetLink *pLink);

// Find a proxy by name
SA_RET_OP_VALID Proxy *findProxyByName(SA_PARAM_NN_STR const char *pName);

// Remove a proxy
void disconnectProxy(SA_PARAM_NN_VALID Proxy *pProxy);

// Get the link of a proxy
SA_RET_NN_VALID NetLink *getProxyNetLink(SA_PARAM_NN_VALID Proxy *pProxy);

// Set the name of a proxy
void setProxyName(SA_PARAM_NN_VALID Proxy *pProxy, SA_PARAM_NN_STR const char *pName);

// Set the name of a proxy's cluster
void setProxyCluster(Proxy *pProxy, const char *pName);

// Set the name of a proxy's environment
void setProxyEnvironment(Proxy *pProxy, const char *pName);

// Get the name of a proxy
SA_RET_NN_STR const char * getProxyName(SA_PARAM_NN_VALID const Proxy * pProxy);

// Get the name of a proxy's cluster
SA_RET_NN_STR const char * getProxyCluster(SA_PARAM_NN_VALID const Proxy *pProxy);

// Get the name of a proxy's environment
SA_RET_NN_STR const char * getProxyEnvironment(SA_PARAM_NN_VALID const Proxy *pProxy);

// Get the last seen time of a proxy
U32 getProxyLastSeenTime(SA_PARAM_NN_VALID Proxy *pProxy);

// Call a callback for each proxy
void forEachProxy(SA_PARAM_NN_VALID ProxyProcessor pCallback, SA_PARAM_OP_VALID void *pUserData);

// Returns true if the proxy connection is fully initialized
bool proxyHandshakeCompleted(SA_PARAM_NN_VALID const Proxy * pProxy);
