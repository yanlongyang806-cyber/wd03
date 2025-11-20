#pragma once

typedef struct NewOrExistingGameServerAddressRequesterInfo NewOrExistingGameServerAddressRequesterInfo;
typedef struct UGCProjectSeries UGCProjectSeries;
typedef struct UGCProject UGCProject;
typedef struct UGCProjectVersion UGCProjectVersion;
typedef struct PossibleMapChoice PossibleMapChoice;
typedef struct MapSearchInfo MapSearchInfo;
typedef struct UGCAccount UGCAccount;
typedef struct PossibleUGCProject PossibleUGCProject;
typedef struct Container Container;
typedef struct UGCPlayableNameSpaceData UGCPlayableNameSpaceData;

void aslMapManager_RequestNewUGCProjectAndGameServer(PossibleUGCProject *pUGCProjectRequest, SlowRemoteCommandID iCmdID, NewOrExistingGameServerAddressRequesterInfo *pRequesterInfo);
void RequestNewOrExistingUGCProjectGameServerForExistingProject(PossibleUGCProject *pUGCProjectRequest, SlowRemoteCommandID iCmdID, NewOrExistingGameServerAddressRequesterInfo *pRequesterInfo);

void aslUGCDataManagerProject_StartNormalOperation(void);

void UgcProjectPreCommitCB(Container *con, ObjectPathOperation **operations);
void UgcProjectPostCommitCB(Container *con, ObjectPathOperation **operations);
void UgcProjectSeriesPreCommitCB(Container *con, ObjectPathOperation **operations);
void UgcProjectSeriesPostCommitCB(Container *con, ObjectPathOperation **operations);

void UgcProjectAddCB(Container *con, UGCProject *pProj);
void UgcProjectRemoveCB(Container *con, UGCProject *pProj);
void UgcProjectSeriesAddCB(Container *con, UGCProjectSeries *pSeries);
void UgcProjectSeriesRemoveCB(Container *con, UGCProjectSeries *pSeries);

bool CheckEditQueueCookieValidity(U32 iCookie);
U32 aslMapManager_UGCEditQueue_Size(void);
void EditQueue_OncePerSecond(TimedCallback *callback, F32 timeSinceLastCallback, UserData userData);

void aslUGCDataManagerLibProjectNormalOperation(void);

//returns a POOL_STRING
const char *GetEditMapNameFromUGCProject(const UGCProject *pProj);
const char *GetEditMapNameFromPossibleUGCProject(const PossibleUGCProject *pPossibleUGCProject, bool bCopy);

typedef void (*UGCAccountContainerCB)(UGCAccount *author, UserData userData);

void UGCAccountEnsureExists(ContainerID containerID, UGCAccountContainerCB cb, UserData userData);

void GetUGCProjectsByUGCAccount(ContainerID uUGCAccountID, ContainerID **peauUGCProjectIDs, ContainerID **peauUGCProjectSeriesIDs);

PossibleUGCProject *CreatePossibleUGCProject(UGCProject *pProject, U32 iCopyID);

UGCPlayableNameSpaceData **GetUGCPlayableNameSpacesRecentlyChanged(void);
