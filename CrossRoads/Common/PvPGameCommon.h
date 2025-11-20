/***************************************************************************
*     Copyright (c) 2006-2011, Cryptic Studios
*     All Rights Reserved
*     Confidential Property of Cryptic Studios
***************************************************************************/
#ifndef PVPGAMECOMMON_H
#define PVPGAMECOMMON_H

#include "referencesystem.h"

typedef struct QueueDef QueueDef;
typedef struct QueuePartitionInfo QueuePartitionInfo;
typedef struct Entity Entity;
typedef struct Expression Expression;
typedef struct CritterDef CritterDef;
typedef struct MapState MapState;
typedef struct CharacterClass CharacterClass;
typedef struct RewardTable RewardTable;
typedef struct QueueMatch QueueMatch;
typedef struct PVPCurrentGameDetails PVPCurrentGameDetails;
typedef struct MatchMapState MatchMapState;

AUTO_RUN_ANON(memBudgetAddMapping(__FILE__, BUDGET_GameSystems););

AUTO_ENUM;
typedef enum PVPGameType
{
	kPVPGameType_None,
	kPVPGameType_Deathmatch,
	kPVPGameType_Domination,
	kPVPGameType_CaptureTheFlag,
	kPVPGameType_TowerDefense,
	kPVPGameType_LastManStanding,
	kPVPGameType_Custom,
	kPVPGameType_Maximum,	EIGNORE
}PVPGameType;

AUTO_ENUM;
typedef enum EQueueRewardTableCondition
{
	kQueueRewardTableCondition_Draw = 0,
	kQueueRewardTableCondition_Loss,
	kQueueRewardTableCondition_Win,
	kQueueRewardTableCondition_UseExpression,

}EQueueRewardTableCondition;

typedef void (*PVPGameTickFunction)(PVPCurrentGameDetails*, F32);
typedef void (*PVPPlayerNearDeathEnterFunction)(PVPCurrentGameDetails*, Entity*, Entity*);
typedef void (*PVPPlayerKilledFunction)(PVPCurrentGameDetails*, Entity*, Entity*);
typedef void (*PVPGameStartFunction)(PVPCurrentGameDetails*, void*);
typedef void (*PVPGameTimeCompleteFunction)(PVPCurrentGameDetails*);
typedef int (*PVPGroupWinningFunction)(PVPCurrentGameDetails*, int *);

// for queue editor
AUTO_STRUCT;
typedef struct qeGameRuleGroup
{
	char *groupName;
}qeGameRuleGroup;

AUTO_STRUCT;
typedef struct PVPGroupGameParams
{
	PVPGameType eType;			AST(SUBTABLE(PVPGameTypeEnum), POLYPARENTTYPE)
		// This field exists only to enable the polytype feature, I think

	int iScore;					AST(NAME(CTFScore))
		// Current Score

	int iNumMembers;
}PVPGroupGameParams;

AUTO_ENUM;
typedef enum CTFFlagStatus
{
	kCTFFlagStatus_InPlace = 0,
	kCTFFlagStatus_PickedUp,
	kCTFFlagStatus_Dropped,
}CTFFlagStatus;

AUTO_STRUCT;
typedef struct CTFGroupParams
{
	PVPGroupGameParams params;			AST(POLYCHILDTYPE(kPVPGameType_CaptureTheFlag))
		// Most be first. Parent parameter struct.

	CTFFlagStatus eFlagStatus;
	
	F32 fTimeDropped;					AST(SERVER_ONLY)
		// If the flag is current dropped, this is for how long it has been dropped
	Vec3 vecFlagLocation;				AST(SERVER_ONLY)
		// If the flag is dropped, then this is the location of the flag
	Vec3 vecCapLocation;
		// Where to capture the flag
	EntityRef eFlagHolder;
		// If the flag is being carried, this is the flag barer
}CTFGroupParams;

AUTO_ENUM;
typedef enum DOMPointStatus
{
	kDOMPointStatus_Unowned,
	kDOMPointStatus_Controled,
	kDOMPointStatus_Contested,
}DOMPointStatus;

AUTO_STRUCT;
typedef struct DOMControlPoint
{
	int iPointNumber;
	DOMPointStatus eStatus;
	U32 *iAttackingEnts;
	F32 fLife;
	Vec3 vLocation;
	F32 fTick;							AST(SERVER_ONLY)
	int iAttackingGroup;
	int iOwningGroup;
	F32 fX;								AST(CLIENT_ONLY)
	F32 fY;								AST(CLIENT_ONLY)
}DOMControlPoint;

AUTO_STRUCT;
typedef struct DOMGroupParams
{
	PVPGroupGameParams params;			AST(POLYCHILDTYPE(kPVPGameType_Domination))
		// Must be first. Parent parameter struct.

	DOMControlPoint **ppOwnedPoints;	AST(UNOWNED)
}DOMGroupParams;

AUTO_STRUCT;
typedef struct CUSGroupParams
{
	PVPGroupGameParams params;			AST(POLYCHILDTYPE(kPVPGameType_Custom))
		// Must be first. parent parameter struct.
}CUSGroupParams;

AUTO_STRUCT;
typedef struct DMGroupParams
{
	PVPGroupGameParams params;			AST(POLYCHILDTYPE(kPVPGameType_Deathmatch))
		// Must be first. parent parameter struct.
}DMGroupParams;

AUTO_STRUCT;
typedef struct LMSGroupParams
{
	PVPGroupGameParams params;			AST(POLYCHILDTYPE(kPVPGameType_LastManStanding))
		// Must be first. parent parameter struct.
	int iTeammatesRemaining;
}LMSGroupParams;

AUTO_STRUCT;
typedef struct TDEGroupParams
{
	PVPGroupGameParams params;			AST(POLYCHILDTYPE(kPVPGameType_TowerDefense))
		// Must be first. parent parameter struct.
}TDEGroupParams;

AUTO_STRUCT;
typedef struct PVPGameDef
{
	int igamedef; //Delete me
}PVPGameDef;

AUTO_STRUCT;
typedef struct QueueRewardTable
{
	PVPGameType eType;				AST(SUBTABLE(PVPGameTypeEnum))
	// the game type that gets the reward

	EQueueRewardTableCondition eRewardCondition;	AST(ADDNAMES(Victory))
	// what condition on getting the reward, 

	Expression *pExprRewardCondition;	AST(NAME(ExprRewardConditionBlock), REDUNDANT_STRUCT(RewardCondition, parse_Expression_StructParam), LATEBIND)
	// only used if the RewardCondition is kQueueRewardTableCondition_UseExpression
	// if the expression returns true, grant the reward to the player

	REF_TO(RewardTable) hRewardTable;	AST(NAME(RewardTable) REFDICT(RewardTable) STRUCT_NORECURSE )

	char *pchEvent;

	U32 bScaleReward : 1;

}QueueRewardTable;

AUTO_STRUCT;
typedef struct CTF_FlagPowerStack
{
	char *pchPower;						AST(STRUCTPARAM)
	F32 fIntervalTime;					AST(STRUCTPARAM)
	F32 fStartTime;						AST(STRUCTPARAM)
}CTF_FlagPowerStack;

AUTO_ENUM;
typedef enum PVPSpecialActions
{
	kPVPSpecialAction_ThrowFlag=0,
	kPVPSpecialAction_DropFlag,
}PVPSpecialActions;

// these go to the client as well
AUTO_STRUCT;
typedef struct PVPPublicGameRules
{
	PVPGameType eGameType;				AST(SUBTABLE(PVPGameTypeEnum))
	// The pvp game type, which maps directly to the param structure used.

	int iPointMax;
} PVPPublicGameRules;

// this is embedded flat in the queue def, so that's cool.....kind of
AUTO_STRUCT;
typedef struct PVPGameRules
{
	// basic information

	PVPPublicGameRules publicRules;				AST(EMBEDDED_FLAT)

	char *pchScoreboard;
	// Name of the scoreboard to set for the UI

	char **ppchRankingLeaderboards;		AST(NAME(Leaderboard))

	bool bDisableNaturalRespawn;
	// Only the pvp game will allow players to respawn. All respawn requests from the user will be ignored

	// Deathmatch 

	bool bSuicidePenality;
	//  A player committing suicide, removes 1 kill from the teams score

	// Last man standiong

	F32 fRoundTime; //How long a single round can last
	F32 fIntervalTime;	//Time between rounds

	// domination

	F32 fCaptureTime; //How long it takes to capture each point with only 1 player near by
	F32 fRecycleTime; //How long it takes for a partially captured point to return to 0
	F32 fFriendlyBonus; //The bonus a team gets with multiple players near by a capture point

	F32 fPointTime;	//How long it takes for a captured point to produce a point
	F32 fCaptureDistance; //Distance a player needs to be from the capture point to begin capturing it

	char *pchCapturePointName;	// Name of the world object that represents the capture points NOTE: To be appended with _<pointnumber> to find acture object
	char **ppchPointStatusFX;			AST(NAME(PointStatusFX))
	char **ppchCapturePointFX;			AST(NAME(CapturePointFX))// Name of possible FX to apply to capture points 
	char **ppchContestedPointFX;		AST(NAME(ContestedPointFX))// Name of FX to apply to points being contested

	// capture the flag
	F32 fMaxGameTime;
	// The maximum amount of time the game can last
	F32 fMaxOvertime;
	// The maximum amount of overtime allowed if the game is a draw after regulation time
	F32 fMaxDropTime;
	// Maximum amount of time the flag can be dropped on the ground
	char *pchFlagName;
	// Name of the world object that represents the flags NOTE: To be appended with _<groupnumber> to find actual object for each team
	char *pchFlagCritter;
	// When the flag is dropped, this is the critter that represents the flag
	char **ppchFlagPowers;						AST(NAME(FlagPower))
	// Powers to be added to the player when they are carrying the flag
	REF_TO(CharacterClass) hFlagCarrierClass;	AST(NAME(FlagCarrierClass))
	// Temp class change for the player when they are carrying the flag
	char **ppchRechargePowerCategories;			AST(NAME(RechargePowerCategories))
	// Power categories to trigger recharge times when flag is picked up
	U8 bRequireOwnFlagToScore : 1;
	// Turn on to require that you have your own flag in order to score a point

	EARRAY_OF(CTF_FlagPowerStack) eaFlagStackPowers;		AST(NAME(FlagStackPowers))
	// Powers to be added to the player when they are carrying the flag, and are stacked depending on how long the game has been going on.
	// This is only present if Capture the flag game type is present
} PVPGameRules;

// This struct is our go-to for logic in gslPvPGame.  It contains pointers to data that we will need, in addition to its own data
AUTO_STRUCT;
typedef struct PVPCurrentGameDetails
{
	int iPartitionIdx;
		// the partition to which this belongs
	MatchMapState	* pMapState;					NO_AST
		// That part of our state that is sent to the client
	QueueMatch * pQueueMatch;						NO_AST
		// The match details, for cases where we need to know about participants
	PVPGameRules * pRules;							NO_AST
		// owned by the queue def (because that's where the data lives on disk)

	PVPGameTickFunction pTickFunc;					NO_AST
	PVPPlayerNearDeathEnterFunction pNearDeathFunc;	NO_AST
	PVPPlayerKilledFunction pKilledFunc;			NO_AST
	PVPGameStartFunction pGameStartFunc;			NO_AST
	PVPGameTimeCompleteFunction pGameTimeComplete;	NO_AST
	PVPGameTimeCompleteFunction pIntermissionComplete; NO_AST
	PVPGroupWinningFunction pGroupWinningFunc;		NO_AST

	F32 fTimer;

	U8 bTimerCountDown : 1;
		// If the current timer is a countdown
	U8 bInOvertime : 1;
		// if the current game is in overtime (optionally set by the game mode)
}PVPCurrentGameDetails;

ParseTable *pvpGame_GetGroupParseTable(PVPGameType eType);

#endif