/***************************************************************************
*     Copyright (c) 2006, Cryptic Studios
*     All Rights Reserved
*     Confidential Property of Cryptic Studios
***************************************************************************/

#pragma once

#include "CombatEnums.h"
#include "entEnums.h"

typedef struct Entity Entity;
typedef struct ZoneMap ZoneMap;
typedef struct WorldInteractionNode WorldInteractionNode;
typedef struct WorldInteractionEntry WorldInteractionEntry;
typedef struct NodeSummary NodeSummary;
typedef struct MapSummary MapSummary;

// This exists so the beaconserver can load only the encounter data
extern bool g_EncounterNoErrorCheck;

extern int g_EnableDynamicRespawn;
extern F32 g_fDynamicRespawnScale;

typedef struct Expression Expression;

// Gets the map's level
U32 mechanics_GetMapLevel(int iPartitionIdx);

// Get map summary information from a node
NodeSummary* mechanics_GetNodeSummaryFromNode(WorldInteractionNode* pNode);
MapSummary* mechanics_FindMapSummaryFromMapInfo(const char* pchMapName, const char* pchMapVars);

// Sound functions: play sounds for any nearby players
void mechanics_playMusicAtLocation(int iPartitionIdx, SA_PRE_NN_RELEMS(3) const Vec3 loc, SA_PARAM_NN_VALID const char *pcSoundName, SA_PARAM_OP_VALID const char *pcBlameFile);
void mechanics_replaceMusicAtLocation(int iPartitionIdx, SA_PRE_NN_RELEMS(3) const Vec3 loc, SA_PARAM_NN_VALID const char *pcSoundName, SA_PARAM_OP_VALID const char *pcBlameFile);
void mechanics_clearMusicAtLocation(int iPartitionIdx, SA_PRE_NN_RELEMS(3) const Vec3 loc);
void mechanics_endMusicAtLocation(int iPartitionIdx, SA_PRE_NN_RELEMS(3) const Vec3 loc);
void mechanics_playOneShotSoundAtLocation(int iPartitionIdx, SA_PRE_OP_RELEMS(3) const Vec3 vLoc, SA_PARAM_OP_VALID Entity **entsIn, SA_PARAM_NN_VALID const char *pcSoundName, SA_PARAM_OP_VALID const char *pcBlameFile);
void mechanics_playOneShotSoundFromEntity(int iPartitionIdx, SA_PARAM_NN_VALID Entity* e, SA_PARAM_NN_VALID const char *pcSoundName, SA_PARAM_OP_VALID const char *pcBlameFile);
void mechanics_stopOneShotSoundAtLocation(int iPartitionIdx, SA_PRE_OP_RELEMS(3) const Vec3 vLoc, SA_PARAM_OP_VALID Entity **eaEntsIn, SA_PARAM_NN_VALID const char* pcSoundName);
void mechanics_stopOneShotSoundFromEntity(int iPartitionIdx, SA_PARAM_NN_VALID Entity* e, SA_PARAM_NN_VALID const char* pcSoundName);


AUTO_ENUM;
typedef enum LogoffType
{
	kLogoffType_Normal,
		//Normal, clicked logout logout
	kLogoffType_Disconnect,
		// Exited the game without waiting
	kLogoffType_GoToCharacterSelect,
		// Allows the player go back to the character select without actually logging them off
	kLogoffType_MeetPartyInLobby,
		// Allows the player go back to the lobby to meet with their party without actually logging them off
} LogoffType;

AUTO_STRUCT;
typedef struct EntityTimedLogoff
{
	ContainerID cid;		AST(KEY)
		//The container ID of the entity logging out

	LogoffType eType;
		//How was this timed logoff started?

	F32 fTimeToLogoff;
		// The time this player takes to logoff

	F32 fTimeSpent;
		// The time this player has spend logging out.  Once fTimeSpent >= fTimeToLogoff, logoff occurs
	U32 iAuthTicket;
		// The authentication ticket used to reauthenticate after log off
	U32 iLastGetAuthTicketTimestamp;
		// Last time we requested the authentication ticket

	U32 bRanPostLogoff : 1;
		// Ran the client side post-logoff code
	U32 bDisconnected : 1;
		// If the client has disconnected already
	U32 bGettingAuthTicket : 1;
		// We're trying to get an authentication ticket for the client from the account server
} EntityTimedLogoff;

AUTO_STRUCT;
typedef struct LogoffConfig
{
	S32 *eaiCancelTypes;		AST(NAME(LogoffCanceledBy) SUBTABLE(LogoffCancelTypeEnum))
		// The cancel types allowed to cancel logoff

	F32 fNormalLogoffTime;		AST(NAME(NormalLogoff))
		// How long a user has to wait when logging off normally

	F32 fDisconnectLogoffTime;	AST(NAME(DisconnectLogoff))
		// How long a user who disconnects/exits the game will wait
		
	Expression *pExprDoQuickLogout;	AST(NAME(ExprBlockQuickLogout), REDUNDANT_STRUCT(DoQuickLogout, parse_Expression_StructParam), LATEBIND)
		// if returning true, the entity will be logged out immediately

} LogoffConfig;

extern LogoffConfig g_pLogoffConfig;
extern EntityTimedLogoff **g_eaLogoffEnts;

// Logout timers

//Starting and stopping log off timers
void gslLogoff_StartTimer(SA_PARAM_NN_VALID Entity *pEnt, F32 fTime, LogoffType eType);
void gslLogoff_RemoveTimer(SA_PARAM_NN_VALID Entity *pEnt);

//Like remove timer, but cancels the log off if there is one.  Returns true/false if it had to cancel a log off
// eType of enum LogoffCancelType
bool gslLogoff_Cancel(SA_PARAM_NN_VALID Entity *pEnt, S32 eType);

//Tick function for logging off
void gslLogoff_Tick(F32 fTimeElapsed);

void mechanics_LogoutTimerStartEx(SA_PARAM_NN_VALID Entity *pEnt, LogoutTimerType eLogoutType, U32 uLogoutTimer);
#define mechanics_LogoutTimerStart(pEnt, eLogoutType) mechanics_LogoutTimerStartEx(pEnt, eLogoutType, 0)
void mechanics_LogoutTimersProcess(SA_PARAM_NN_VALID Entity *pEnt);
void mechanics_LogoutTimerRemove(SA_PARAM_NN_VALID Entity *pEnt);

void mechanics_UpdateCombatLevels(int iPartitionIdx, int iMinLevel, int iMaxLevel);

// Cleanup any map-specific information on the player
void mechanics_LeaveMapEntityCleanup(Entity *pPlayerEnt);

// Cleanup player state during InitEncounters
void mechanics_CleanupAllPlayers(void);

// Called during InitEncounters to restore some player state
void mechanics_LoadAllPlayers(void);

void mechanics_OncePerFrame(F32 fTimeStep);

// Called on map load and unload
void mechanics_MapLoad(ZoneMap *pZoneMap, bool bFullInit);
void mechanics_MapUnload(void);
void mechanics_MapReset(void);
void mechanics_MapValidate(ZoneMap *pZoneMap);

// For function in "mapchange_common.c"
void game_MapReInit(void);
