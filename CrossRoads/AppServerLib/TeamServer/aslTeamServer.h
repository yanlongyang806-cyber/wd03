/***************************************************************************
*     Copyright (c) 2005-2006, Cryptic Studios
*     All Rights Reserved
*     Confidential Property of Cryptic Studios
***************************************************************************/

#pragma once

typedef struct Team Team;
typedef struct GameSession GameSession;
typedef struct GameSessionQueryResult GameSessionQueryResult;
typedef struct GameSessionQuery GameSessionQuery;
typedef struct GameContentNodePartyCountResult GameContentNodePartyCountResult;
typedef U32 ContainerID;

typedef struct Container Container;
typedef struct ObjectPathOperation ObjectPathOperation;


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////
// Entity -> Team lookup management.
//   We keep around these stash tables of entity to team info for members and disconnecteds to allow
// us to correctly predict what transactions we will need in order to get a ForceJoin to work when all
// we have is an Entity ID. Since the actual Entity data may be on a GameServer or the LoginServer or the ObjectDB,
// it would involve much overhead to get the info through a read-only transaction or through a remote command with return.

void aslTeam_InitStashedTeamLookups();
int aslTeam_GetStashedMemberTeamID(int iMemberID);
int aslTeam_GetStashedDisconnectedTeamID(int iDisconnectedID);


/// ObjRegisterContainer callbacks. Set up in aslTeamServerInit.
void aslTeam_StatePreChangeMembers_CB(Container *con, ObjectPathOperation **operations);
void aslTeam_StatePostChangeMembers_CB(Container *con, ObjectPathOperation **operations);
void aslTeam_StatePreChangeDisconnecteds_CB(Container *con, ObjectPathOperation **operations);
void aslTeam_StatePostChangeDisconnecteds_CB(Container *con, ObjectPathOperation **operations);
void aslTeam_TeamAdd_CB(Container *con, Team *pTeam);
void aslTeam_TeamRemove_CB(Container *con, Team *pTeam);


int TeamServerLibOncePerFrame(F32 fElapsed);
Team *aslTeam_GetTeam(ContainerID iTeam);

// Initializes the global game session information
void aslTeam_InitGameSessionInfo(void);

// Handles team updates to keep the game sessions up to date
void aslTeam_HandleTeamUpdatesForGameSessions(ContainerID iTeamID);

// Removes a game session
bool aslTeam_RemoveGameSession(ContainerID iTeamID);

// Returns a game session based on the given team ID
GameSession * aslTeam_GetGameSessionByID(U32 uiTeamID, U32 uiCurrentVersion);

// Sets the ready flag for a team member in a game session
void aslTeam_SetReadyFlagInGameSession(ContainerID uiTeamID, ContainerID uiEntID, bool bReady);

bool aslTeam_IsEveryoneInSessionReady(SA_PARAM_NN_VALID GameSession *pGameSession);

// Returns the game sessions in the given group
void aslTeam_GetGameSessions(SA_PARAM_NN_VALID GameSessionQuery *pQuery, SA_PARAM_NN_VALID GameSessionQueryResult *pResult);

// Returns the number of sessions for the given game session group
GameContentNodePartyCountResult * aslTeam_GetGameSessionCountByGroup(const char *pchGroupName);
