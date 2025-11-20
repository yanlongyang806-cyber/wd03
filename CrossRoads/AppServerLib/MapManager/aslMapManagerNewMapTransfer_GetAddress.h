#pragma once
//stuff relating to the final step of map transfer, when a server says "here's where I want to go, what
//address is that"

typedef struct TrackedGameServerExe TrackedGameServerExe;
typedef struct PendingRequestCache PendingRequestCache;
typedef struct NewOrExistingGameServerAddressRequesterInfo NewOrExistingGameServerAddressRequesterInfo;
typedef struct PossibleUGCProject PossibleUGCProject;


void NewMapTransfer_SendPlayerToServerNow(TrackedGameServerExe *pServer, PendingRequestCache *pCache);
void NewMapTransfer_SendReturnAddressNow(TrackedGameServerExe *pServer, PendingRequestCache *pCache);
void NewMapTransfer_RequestNewOrExistGameServerAddress_Fail(SlowRemoteCommandID iCmdID, FORMAT_STR const char *pFmt, ...);

void NewMapTransfer_MaybeFulfillPendingDebugTransferNotifications(TrackedGameServerExe *pServer);

void NewMapTransfer_RequestUGCEditingServer(PossibleUGCProject *pUGCProjectRequest, SlowRemoteCommandID iCmdID, NewOrExistingGameServerAddressRequesterInfo *pRequesterInfo);

bool NewMapTransfer_ServerHasRoomForNewPartition(TrackedGameServerExe *pServer);

U32 NewMapTransfer_GetLastTimeTeamWasSentToPartition(MapPartitionSummary *pPartition, ContainerID iTeamID, int iCutoffPeriod);
void NewMapTransfer_TeamBeingSentToPartitionNow(MapPartitionSummary *pPartition, ContainerID iTeamID);