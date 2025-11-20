#pragma once

#define WI_LEGACY_DIR "/legacy/"

typedef struct NetLink NetLink;
typedef struct HttpClientStateDefault HttpClientStateDefault;

bool initWebInterface(void);

void shutdownWebInterface(void);

void httpLegacyHandlePost(NetLink *link, HttpClientStateDefault *pClientState);

void httpLegacyHandleGet(char *data, NetLink *link, HttpClientStateDefault *pClientState);