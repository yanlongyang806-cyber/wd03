#pragma once
/***************************************************************************
*     Copyright (c) 2012, Cryptic Studios
*     All Rights Reserved
*     Confidential Property of Cryptic Studios
***************************************************************************/

typedef U32 ContainerID;
typedef struct NetLink NetLink;
typedef struct Packet Packet;
typedef struct Login2State Login2State;
typedef struct Login2CharacterDetail Login2CharacterDetail;
typedef struct PossibleMapChoices PossibleMapChoices;
typedef struct ReturnedGameServerAddress ReturnedGameServerAddress;
typedef struct Login2CharacterCreationData Login2CharacterCreationData;
typedef struct UGCProjectReviews UGCProjectReviews;
typedef struct PossibleUGCProjects PossibleUGCProjects;
typedef struct UGCSearchResult UGCSearchResult;
typedef struct UGCProjectList UGCProjectList;
typedef struct Login2ClusterStatus Login2ClusterStatus;

// This is the data sent from the client in the BeginLogin packet.
AUTO_STRUCT;
typedef struct BeginLoginPacketData
{
    bool noTimeout;
    U32 clientLanguageID;
    ContainerID accountID;
    U32 ticketID;
    const char *machineID;
    const char *accountName;
    U32 clientCRC;
    const char *affiliate;      AST(POOL_STRING)
} BeginLoginPacketData;

AUTO_STRUCT;
typedef struct RedirectLoginPacketData
{
    ContainerID accountID;
    U64 transferCookie;
    bool noTimeout;
} RedirectLoginPacketData;

AUTO_STRUCT;
typedef struct SaveNextMachinePacketData
{
    const char *machineName;
} SaveNextMachinePacketData;

AUTO_STRUCT;
typedef struct OneTimeCodePacketData
{
    const char *oneTimeCode;
    const char *machineName;
} OneTimeCodePacketData;

AUTO_STRUCT;
typedef struct RequestCharacterDetailPacketData
{
    ContainerID characterID;
} RequestCharacterDetailPacketData;

AUTO_STRUCT;
typedef struct ChooseCharacterPacketData
{
    ContainerID characterID;
    bool UGCEdit;
} ChooseCharacterPacketData;

AUTO_STRUCT;
typedef struct DeleteCharacterPacketData
{
    ContainerID characterID;
} DeleteCharacterPacketData;

AUTO_STRUCT;
typedef struct RenameCharacterPacketData
{
    ContainerID characterID;
    const char *newName;
    bool badName;
} RenameCharacterPacketData;

AUTO_STRUCT;
typedef struct CreateCharacterPacketData
{
    Login2CharacterCreationData *characterCreationData;
    bool UGCEdit;
} CreateCharacterPacketData;

void aslLogin2_ConfigureClientLoginNetLink(NetLink *netLink);
void aslLogin2_FailLogin(Login2State *loginState, const char *errorMessageKey);
void aslLogin2_SendLoginInfo(Login2State *loginState);
void aslLogin2_SendCharacterSelectionData(Login2State *loginState);
void aslLogin2_SendCharacterDetail(Login2State *loginState, Login2CharacterDetail *characterDetail);
void aslLogin2_SendShardLockedNotification(Login2State *loginState);
void aslLogin2_SendQueueUpdate(Login2State *loginState, int queuePosition, int queueSize);
void aslLogin2_SendMapChoices(Login2State *loginState, PossibleMapChoices *possibleMapChoices);
void aslLogin2_SendGameserverAddress(Login2State *loginState, ReturnedGameServerAddress *gameserverAddress);
void aslLogin2_SendNewCharacterID(Login2State *loginState, ContainerID newCharacterID);
void aslLogin2_SendPossibleUGCProjects(Login2State *loginState, PossibleUGCProjects *possibleUGCProjects);
void aslLogin2_SendPossibleUGCImports(Login2State *loginState, PossibleUGCProjects *possibleUGCImports);
void aslLogin2_SendReviews(Login2State *loginState, ContainerID projectID, ContainerID seriesID, S32 pageNumber, const UGCProjectReviews *ugcReviews);
void aslLogin2_SendUGCProjectSeriesCreateResult(Login2State *loginState, ContainerID seriesID);
void aslLogin2_SendUGCProjectSeriesUpdateResult(Login2State *loginState, bool success, const char *errorMsg);
void aslLogin2_SendUGCSearchResults(Login2State *loginState, UGCSearchResult *searchResult);
void aslLogin2_SendUGCProjectRequestByIDResults(Login2State *loginState, UGCProjectList *projectList);
void aslLogin2_SendUGCNumAheadInQueue(Login2State *loginState, U32 numAhead);
void aslLogin2_SendUGCEditPermissionCookie(Login2State *loginState, U32 queueCookie);
void aslLogin2_SendClientRedirect(Login2State *loginState, U32 redirectIP, U32 redirectPort, U64 transferCookie);
void aslLogin2_SendClientNoRedirect(Login2State *loginState);
void aslLogin2_SendRedirectDone(Login2State *loginState);
void aslLogin2_SendClusterStatus(Login2State *loginState, Login2ClusterStatus *clusterStatus);
void aslLogin2_SendGamePermissions(Login2State*loginState);
bool aslLogin2_FailIfNotInState(Login2State *loginState, char *stateString);
void aslLogin2_HandleInput(Packet* pak, int cmd, NetLink* netLink, Login2State *loginState);