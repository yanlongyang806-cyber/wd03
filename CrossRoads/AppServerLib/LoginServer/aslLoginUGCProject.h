#pragma once

typedef struct GameAccountData GameAccountData;
typedef struct Login2State Login2State;
typedef struct Packet Packet;

bool aslLoginIsUGCAllowed(void);
U32 aslLoginUGCGetBaseEditSlots(GameAccountData *pGameAccount);
U32 aslLoginUGCGetProjectMaxSlots(GameAccountData *pGameAccount);
U32 aslLoginUGCGetSeriesMaxSlots(GameAccountData *pGameAccount);
bool aslLoginCheckAccountPermissionsForUgcPublishBanned(GameAccountData *gameAccountData);

void aslLoginHandleRequestReviewsForPage(Packet *pak, Login2State *loginState);
void aslLoginHandleUGCImportSimpleSearch(Packet *pak, Login2State *loginState);
void aslLoginHandleChooseUGCProject(Packet *pak, Login2State *loginState);
void aslLoginHandleAcceptedUGCCreateProjectEULA(Packet* pak, Login2State *loginState);
void aslLoginHandleDestroyUGCProject(Packet *pak, Login2State *loginState);
void aslLoginHandleReadyToChooseUGCProject(Packet *pak, Login2State *loginState);

void aslLoginUGCProjectSeriesCreate( Packet* pak, Login2State* loginState );
void aslLoginUGCProjectSeriesDestroy( Packet* pak, Login2State* loginState );
void aslLoginUGCProjectSeriesUpdate( Packet* pak, Login2State* loginState );
void aslLoginUGCSetPlayerIsReviewer( Packet* pak, Login2State* loginState );
void aslLoginUGCSetSearchEULAAccepted( Packet* pak, Login2State* loginState );
void aslLoginUGCProjectRequestByID( Packet* pak, Login2State* loginState );

// Call this whenever the list of editable projects (or if a new
// project is available) may have changed.
void aslLoginSendUGCProjects(Login2State *loginState);
