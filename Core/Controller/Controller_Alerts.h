#pragma once

typedef struct TrackedServerState TrackedServerState;
void CheckGameServerInfoForAlerts(TrackedServerState *pServer);
extern int gDelayBeforeKillingStalledGameserver;

const char *GetCrashedOrAssertedKey(GlobalType eServerType);
