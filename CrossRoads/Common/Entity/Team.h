/***************************************************************************
*     Copyright (c) 2005-2006, Cryptic Studios
*     All Rights Reserved
*     Confidential Property of Cryptic Studios
***************************************************************************/

#pragma once
GCC_SYSTEM

#include "GlobalTypeEnum.h"
#include "itemCommon.h"
#include "AutoTransDefs.h"
#include "XBoxStructs.h"
#include "chatCommon.h"

typedef struct GameInteractable GameInteractable;

typedef struct PossibleMapChoice PossibleMapChoice;
typedef struct Entity Entity;
typedef struct WorldVariable WorldVariable;
typedef struct AwayTeamMembers AwayTeamMembers;
typedef struct RegionRules RegionRules;
typedef struct DoorTransitionSequenceDef DoorTransitionSequenceDef;
typedef struct CachedDoorDestination CachedDoorDestination;
typedef struct ZoneMapInfo ZoneMapInfo;
typedef struct MapDescription MapDescription;
typedef struct Team_AutoGen_NoConst Team_AutoGen_NoConst; 


typedef int PlayerDifficultyIdx;

#define TEAM_MAX_SIZE 5
#define TEAM_CLEANUP_INTERVAL 5
#define GAME_SESSION_UPDATE_INTERVAL 1
#define TEAM_TIME_OUT_INTERVAL 30.f
#define TEAM_REJOIN_TIMEOUT 900.f // 15 minutes
#define MAX_TEAM_DESTROY_PER_TICK 1000 // do not destroy more than this many team containers per tick
#define TEAM_MAX_INIT_TIME 300 // if a team container has been around for 5 minutes and is not yet initialized, then kill it
#define TEAM_LOBBY_MAX_TIME	3600
#define TEAM_STATUS_MESSAGE_MAX_LENGTH 300
#define TEAM_BAD_LOGOUT_SECONDS 120		// the maximum time a team of one will exist if a member gets a bad-logout

//@see also ui/TeamUI.c/(1128):static char* msgkey_from_lootmode
AUTO_ENUM;
typedef enum LootMode
{
    LootMode_RoundRobin,
    LootMode_FreeForAll,
    LootMode_NeedOrGreed,
    LootMode_MasterLooter,
    LootMode_Count
} LootMode;

AUTO_ENUM;
typedef enum TeamMapTransferResult
{
	TeamMapTransferResult_None,
	TeamMapTransferResult_SameMap,
	TeamMapTransferResult_DifferentMap,
} TeamMapTransferResult;
 
AUTO_STRUCT;
typedef struct TeamMemberMapInfoRequest
{
	ContainerID uMemberID;
	ContainerID uRequesterID;
	const char* pchMapName; AST(POOL_STRING)
	char* pchMapDisplayName;
} TeamMemberMapInfoRequest;

AUTO_STRUCT AST_CONTAINER
AST_IGNORE(ppchCompletedNodes);
typedef struct TeamMember
{
	const ContainerID iEntID;				AST(PERSIST SUBSCRIBE)
	CONST_REF_TO(Entity) hEnt;				AST(PERSIST SUBSCRIBE COPYDICT(ENTITYPLAYER))
	const U32 iJoinTime;					AST(PERSIST SUBSCRIBE)
	const bool bSidekicked;					AST(PERSIST SUBSCRIBE)
	CONST_STRING_MODIFIABLE pcName;			AST(PERSIST SUBSCRIBE)
	CONST_STRING_MODIFIABLE pcAccountHandle;AST(PERSIST SUBSCRIBE)
	CONST_STRING_MODIFIABLE pcLogName;		AST(PERSIST SUBSCRIBE)
	CONST_STRING_POOLED pcMapName;			AST(PERSIST SUBSCRIBE POOL_STRING)
	CONST_STRING_MODIFIABLE pcMapMsgKey;	AST(PERSIST SUBSCRIBE)
	CONST_STRING_POOLED pcMapVars;			AST(PERSIST SUBSCRIBE POOL_STRING)
	const U32 iMapInstanceNumber;			AST(PERSIST SUBSCRIBE)
	const U32 iMapContainerID;				AST(PERSIST SUBSCRIBE)
	const U32 uPartitionID;					AST(PERSIST SUBSCRIBE)
	CONST_STRING_MODIFIABLE pcStatus;		AST(PERSIST SUBSCRIBE)
	CONST_STRING_POOLED pchClassName;		AST(PERSIST SUBSCRIBE POOL_STRING NAME(ClassName))

	const S32 iExpLevel;					AST(PERSIST SUBSCRIBE)
	const S32 iOfficerRank;					AST(PERSIST SUBSCRIBE)
	
	// Indicates if the team member participates in the voice chat
	const bool bJoinedVoiceChat;			AST(PERSIST SUBSCRIBE)

	// The receive time of last voice packet from this player
	U32 iLastVoicePacketRcvTime;			AST(CLIENT_ONLY)

	// These fields are used to track which map a team member has gone to.
	// When a player who is part of a team transfers to a new map, we
	//  scan these fields to find if any other team members have left this
	//  map previously for the same destination.  If we find one, we request
	//  the same instance.
	// This allows teamed players who are going to static or shared maps to
	//  end up in the same instance if they start in the same instance.
	OPTIONAL_STRUCT(MapDescription) pExitMapDescription;	NO_AST
	U32 iExitMapTime;						NO_AST

	U32 uiLobbyJoinTime;					NO_AST

	// Indicates whether the player is ready to start the game
	U32 bLobbyReadyFlag : 1;				NO_AST

	// We need queuing info from each member when TeamJoining. This is set when we get information back.
	U32 bQueueValidationRequested : 1;		NO_AST
	U32 bQueueValidationReceived : 1;		NO_AST
	U32 bQueueValidationWasOkay : 1;		NO_AST

} TeamMember;

// A stub of a team member. Used to represent a team member who is off line or otherwise not currently 'available'
AUTO_STRUCT AST_CONTAINER;
typedef struct StubTeamMember
{
	const ContainerID iEntID;				AST(PERSIST SUBSCRIBE)
	const U32 iStubTime;					AST(PERSIST SUBSCRIBE)
	CONST_STRING_MODIFIABLE pcName;			AST(PERSIST SUBSCRIBE)
	CONST_STRING_MODIFIABLE pcAccountHandle;AST(PERSIST SUBSCRIBE)
	CONST_STRING_POOLED pchClassName;		AST(PERSIST SUBSCRIBE POOL_STRING NAME(ClassName))
} StubTeamMember;


AUTO_STRUCT AST_CONTAINER;
typedef struct AwayTeamMemberPref
{
	const GlobalType	eEntType;			AST(PERSIST SUBSCRIBE)
	const ContainerID	iEntID;				AST(PERSIST SUBSCRIBE)
	const U32			uiCritterPetID;		AST(PERSIST SUBSCRIBE)
} AwayTeamMemberPref;

AUTO_STRUCT AST_CONTAINER;
typedef struct AwayTeamPrefs
{				
	CONST_EARRAY_OF(AwayTeamMemberPref) eaTeamMembers;	AST(PERSIST SUBSCRIBE)

} AwayTeamPrefs;

AUTO_STRUCT AST_CONTAINER 
AST_IGNORE(bEnableOpenInstancing) 
AST_IGNORE(iRewardIndex) 
AST_IGNORE(iCompletedMissionsVersion) 
AST_IGNORE(ppchCompletedMissions)
AST_IGNORE(hCurrentProgressionNode)
AST_IGNORE(bAllGameProgressionNodesUnlocked);
typedef struct Team
{
	const ContainerID iContainerID;						AST(PERSIST SUBSCRIBE KEY)

	const U32 iCreatedOn;								AST(PERSIST SUBSCRIBE)
	CONST_EARRAY_OF(TeamMember) eaMembers;				AST(PERSIST SUBSCRIBE)
	CONST_EARRAY_OF(TeamMember) eaInvites;				AST(PERSIST SUBSCRIBE)
	CONST_EARRAY_OF(TeamMember) eaRequests;				AST(PERSIST SUBSCRIBE)
	CONST_OPTIONAL_STRUCT(TeamMember) pLeader;			AST(PERSIST SUBSCRIBE)
	CONST_OPTIONAL_STRUCT(TeamMember) pChampion;		AST(PERSIST SUBSCRIBE)
	CONST_EARRAY_OF(AwayTeamPrefs) eaAwayTeamPrefs;		AST(PERSIST SUBSCRIBE)

	CONST_EARRAY_OF(StubTeamMember) eaDisconnecteds;	AST(PERSIST SUBSCRIBE)
	
	const TeamMode eMode;								AST(PERSIST SUBSCRIBE)
	const U32 iVersion;									AST(PERSIST SUBSCRIBE)
	const bool bInviteAccepted;							AST(PERSIST SUBSCRIBE)
	const LootMode loot_mode;							AST(PERSIST SUBSCRIBE)
    CONST_STRING_POOLED loot_mode_quality;				AST(PERSIST SUBSCRIBE POOL_STRING)

	const PlayerDifficultyIdx iDifficulty;				AST(PERSIST SUBSCRIBE INT)

	GameInteractable** eaRevealedInteractables;			NO_AST
	// Keeps track of interactionNodes that this team has discovered.

	// A cached interaction entry set when a player on the team passes through a keyed door.
	// Allows other teammates to pass through the door with the player before the player has reached
	// the next map.
	CachedDoorDestination* pCachedDestination;			NO_AST
    
	TeamMode eCachedMode;
	int iCachedMembers;
	char *pchCachedStatusMessage;						AST(ESTRING)
	// This is checked on the gameserver to see if the team mode has changed and we need to resend status

	CONST_STRING_POOLED pchPrimaryMission;				AST(PERSIST SUBSCRIBE POOL_STRING ADDNAMES(Esprimarymission))

	// Track allegiance and suballegiance of the team.  This is used to ensure that there is no cross-allegiance teaming.
	CONST_STRING_POOLED pcAllegiance;					AST(PERSIST SUBSCRIBE POOL_STRING)
	CONST_STRING_POOLED pcSubAllegiance;				AST(PERSIST SUBSCRIBE POOL_STRING)

	const bool bRequireSubAllegianceMatch;				AST(PERSIST SUBSCRIBE)

	CONST_OPTIONAL_STRUCT(CrypticXSessionInfo) pXSessionInfo;	AST(PERSIST SUBSCRIBE FORCE_CONTAINER)
	// session info for an XBOX online-game session.

	const ContainerID iTeamSpokesmanEntID;				AST(PERSIST SUBSCRIBE)
	// The container ID for the team spokesman

	// The current UGC project being played through
	const ContainerID iCurrentUGCProjectID;				AST(PERSIST SUBSCRIBE)

	// The status message set by the team leader
	CONST_STRING_MODIFIABLE pchStatusMessage;			AST(PERSIST SUBSCRIBE ESTRING)

	// Time last member was logged out badly. Used to prevent clearing of team where just leader is left
	const U32 uBadLogoutTime;									AST(PERSIST SUBSCRIBE)

	////////////
	// ServerOwner data. Our team is tied to a GameServer by a Local Team. The team stays until the local team on the game server goes away.
	const ContainerID iGameServerOwnerID;				AST(PERSIST SUBSCRIBE SERVER_ONLY)
		//The game server ID that has the local team that represents this team.
	
	const bool bTeamMembersMustBeOnOwnerGameServer;		AST(PERSIST SUBSCRIBE SERVER_ONLY)
		// If this is set, team members are removed from the team if they are not on the correct game server/map. See TeamCommands.c: gslTeam_EntityUpdate

	const U32 uiLocalTeamSyncRequestID;					AST(PERSIST SUBSCRIBE SERVER_ONLY)

		// Used for pinging the game server for team/game server status:
	U32 iOwnedLastUpdate;								NO_AST
	U32 iOwnedLastRequest;								NO_AST

	////////////
} Team;

AUTO_STRUCT;
typedef struct TeamList
{
	U32 *eaiTeams;
} TeamList;


AUTO_STRUCT;
typedef struct TeamInfoPowerElem
{
	const char *pchIcon;
	char *pchToolTip;
	int iePurpose;
	U32 uiID;
	REF_TO(PowerDef) hRef;	AST(REFDICT(PowerDef))
} TeamInfoPowerElem;

AUTO_STRUCT;
typedef struct TeamInfoPet
{
	int containerID;
	TeamInfoPowerElem **eaPowerElem;
} TeamInfoPet;

AUTO_STRUCT;
typedef struct TeamInfoPlayer
{
	int containerID;
	TeamInfoPet **eaPetList;
} TeamInfoPlayer;

AUTO_STRUCT;
typedef struct TeamInfoFromServer
{
	TeamInfoPlayer **eaPlayerList;
} TeamInfoFromServer;

AUTO_STRUCT;
typedef struct TeamInfoRequest
{
	char **eaPowersCategories;
} TeamInfoRequest;

AUTO_ENUM;
typedef enum TeamRequestCheckStatus
{
	TeamRequestCheckStatus_Success,
	TeamRequestCheckStatus_NotOnMap,
	TeamRequestCheckStatus_NotOnTeam,
	TeamRequestCheckStatus_TeamIsLocal,
	TeamRequestCheckStatus_PlayerHostile,
} TeamRequestCheckStatus;

AUTO_STRUCT;
typedef struct TeamRequestCheckResult
{
	TeamRequestCheckStatus eStatus;
	ContainerID uiRequesterID;	// The ID doing the requesting
	ContainerID uiRequestedID;	// The ID being requested of
	ContainerID uiTeamID;
} TeamRequestCheckResult;

// team_IsWithTeam checks to see if the passed entity is at all associated with a team, i.e. on the request or invite list (doesn't necessarily mean they are a member)
#define team_IsWithTeam(pEnt) (pEnt && (pEnt)->pTeam && (pEnt)->pTeam->iTeamID) 
#define team_IsMember(pEnt) (team_IsWithTeam(pEnt) && (pEnt)->pTeam->eState == TeamState_Member)
#define team_IsInvitee(pEnt) (team_IsWithTeam(pEnt) && (pEnt)->pTeam->eState == TeamState_Invitee)
#define team_IsRequester(pEnt) (team_IsWithTeam(pEnt) && (pEnt)->pTeam->eState == TeamState_Requester)

// GetTeam returns the team only if the pEnt is a full member
#define team_GetTeam(pEnt) (team_IsMember(pEnt) ? GET_REF((pEnt)->pTeam->hTeam) : NULL)
// GetTeamID returns the teamID only if the pEnt is a full member
#define team_GetTeamID(pEnt) (team_IsMember(pEnt) ? (pEnt)->pTeam->iTeamID : 0)

#define team_OnSameTeam(pEnt1, pEnt2) (team_IsMember(pEnt1) && team_IsMember(pEnt2) && team_GetTeamID(pEnt1) == team_GetTeamID(pEnt2))
#define team_OnSameTeamID(pEnt1, iEnt2ID) (team_FindMemberID(team_GetTeam(pEnt1), (iEnt2ID)) != NULL)
#define team_IsOnThisTeam(pTeam, pEnt) (pTeam && team_GetTeamID(pEnt) == (pTeam)->iContainerID)
#define team_IsOnTeamWithID(pEnt, iContainerID) (team_IsMember(pEnt) && (team_GetTeamID(pEnt) == (iContainerID)))
#define team_GetTeamLeader(iPartitionIdx, pTeam) ((pTeam && pTeam->pLeader) ? entFromContainerID(iPartitionIdx, GLOBALTYPE_ENTITYPLAYER, (pTeam)->pLeader->iEntID) : NULL)
#define team_GetTeamLeaderAnyPartition(pTeam) ((pTeam && pTeam->pLeader) ? entFromContainerIDAnyPartition(GLOBALTYPE_ENTITYPLAYER, (pTeam)->pLeader->iEntID) : NULL)
#define team_FindMember(pTeam, pEnt) ((pEnt) ? team_FindMemberID((pTeam), (pEnt)->myContainerID) : NULL)
#define team_IsLocal(pEnt) (team_IsWithTeam(pEnt) && !!(pEnt)->pTeam->bMapLocal)

bool team_IsTeamLeader(const Entity *pEnt);
bool team_trh_IsTeamChampionID(U32 uiEntID, ATH_ARG NOCONST(Team) *pTeam);
#define team_IsTeamChampion(pEnt) team_trh_IsTeamChampionID((pEnt) ? (pEnt)->myContainerID : 0, CONTAINER_NOCONST(Team, team_GetTeam(pEnt)))
#define team_IsTeamChampionID(uiEntID, pTeam) team_trh_IsTeamChampionID(uiEntID, CONTAINER_NOCONST(Team, (pTeam)))
#define team_GetTeamSpokesman(pEnt) (team_GetTeamLeader(team_GetTeam(pEnt)))
TeamMember *team_FindMemberID(const Team *pTeam, U32 iEntID);
StubTeamMember *team_FindDisconnectedStubMemberID(const Team *pTeam, U32 iEntID);
StubTeamMember *team_FindDisconnectedStubMemberAccountAndName(const Team *pTeam, const char* pcAccount, const char* pcCharName);
TeamMember *team_FindChampion(const Team *pTeam);
// Checks to see if a pet is owned by the player or any member on the player's team
bool team_IsPetOwnedByPlayerOrTeam(SA_PARAM_OP_VALID Entity *pPlayer, SA_PARAM_OP_VALID Entity *pTeamMate);
// If pTeamMate is a pet, this function checks to see if the pet is owned pPlayer's team
bool team_IsPlayerOrPetOnTeam(SA_PARAM_OP_VALID Entity *pPlayer, SA_PARAM_OP_VALID Entity *pTeamMate);

void team_GetOnMapEntRefs(int iPartitionIdx, EntityRef **res_refs, Team *team);
void team_GetOnMapEntIds(int iPartitionIdx, int **res_ids, Team *team);

// Adds entities to the array if they aren't already in it; doesn't clear
void team_GetOnMapEntsUnique(int iPartitionIdx, Entity ***peaEnts, Team *team, bool IncludePets);
// Gets all team ents, and optionally pets. Prioritizes self first. Returns the number of entities added to peaTeamEnts.
int team_GetTeamListSelfFirst(Entity* pEnt, Entity*** peaTeamEnts, TeamMember*** peaTeamMembers, bool bIncludeSelf, bool bIncludePets);

// Gets StubTeamMembers representing disconnected team members
int team_GetTeamStubMembers(Entity* pEnt, StubTeamMember*** peaStubTeamMembers);

bool team_IsAwayTeamLeader( Entity* pEntity, AwayTeamMembers* pAwayTeamMembers );
bool team_OnCompatibleFaction(Entity *playerEnt, Entity *targetEnt);
bool team_TeamOnCompatibleFaction(Entity *playerEnt, Entity *targetEnt);

// Indicates if the entity is the team spokesman
bool team_IsTeamSpokesman(const Entity *pEnt);
bool team_IsTeamSpokesmanBySelfTeam(const Entity *pEntSelf, const Entity *pEntToCheck);

#if GAMECLIENT
// Determines if the team member is currently talking in voice chat
bool team_IsTeamMemberTalking(SA_PARAM_NN_VALID TeamMember *pTeamMember);
//
SA_ORET_NN_VALID const TeamInfoFromServer *team_RequestTeamInfoFromServer(SA_PARAM_NN_STR const char *pPowersCategory);
#endif

#ifdef GAMESERVER
void gslTeam_cmd_SetSidekicking(Entity *pEnt, bool bSidekicking);
void gslTeam_LeaveSansFeedback(SA_PARAM_NN_VALID Entity* pEnt);
void gslTeam_EntityUpdate(Entity *pEnt);
void gslTeam_HandlePlayerTeamChange( Entity *pEntity );
void gslTeam_Tick( void );
TeamMapTransferResult gslTeam_IsMapTransferChoiceTakingPlace( Entity* pEntity, const char* pchMap, const char* pchSpawn );
bool gslTeam_CreateAwayTeamMapTransferData( Entity* pEntity, const char* pcMap, const char* pcSpawn, 
											GlobalType eOwnerType, ContainerID uOwner, const char* pchMapVars, 
											ZoneMapInfo* pCurrZoneMap, ZoneMapInfo* pNextoneMap, 
											RegionRules* pSrcRules, RegionRules* pDstRules, 
											DoorTransitionSequenceDef* pTransOverride );
bool gslTeam_AddAwayTeamMemberToMapTransfer( Entity* pEntity, bool bIncludeTeammates );
void gslTeam_SendMapTransferInfoToClients( Entity* pEntity, bool bIncludeTeammates );
void gslTeam_AwayTeamMemberStateChangedUpdate( Entity* pInstigator );
void gslTeam_AwayTeamMemberRemove( Entity* pEntity, bool bKeepIfMapOwner );
Team *gslTeam_GetTeam(ContainerID iTeamID);
bool gslTeam_IsValidAwayTeamMapTransfer(SA_PARAM_NN_VALID ZoneMapInfo* pCurrZoneMap, 
										SA_PARAM_NN_VALID ZoneMapInfo* pNextZoneMap, 
										SA_PARAM_NN_VALID RegionRules* pCurrRules, 
										SA_PARAM_NN_VALID RegionRules* pNextRules);
bool gslTeam_IsValidTeamMapTransferForNNO( ZoneMapInfo* pCurrZoneMap, ZoneMapInfo* pNextZoneMap, 
										RegionRules* pCurrRules, RegionRules* pNextRules );

#endif

int team_NumPresentMembers(const Team *pTeam);										// The number of members 'present'. Logged in, etc.
int team_NumTotalMembers(const Team *pTeam);										// Number of members present or disconnected.
int team_NumMembersThisServerAndPartition(const Team *pTeam, int iPartitionIdx);	// The number of members on the requested partition on this server. Excludes disconnecteds.


