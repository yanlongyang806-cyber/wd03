/***************************************************************************
*     Copyright (c) 2006-2008, Cryptic Studios
*     All Rights Reserved
*     Confidential Property of Cryptic Studios
***************************************************************************/

#include "Character.h"
#include "Character_target.h"
#include "ClientTargeting.h"
#include "CombatConfig.h"
#include "EntityLib.h"
#include "GameClientLib.h"
#include "gclControlScheme.h"
#include "gclEntity.h"
#include "PowerModes.h"
#include "Team.h"

// used for a toggle whether to target friendlies or hostiles when using the targeting function
static bool s_bTargetModalFriendly = false;

AUTO_COMMAND ACMD_ACCESSLEVEL(0) ACMD_CATEGORY(Standard, Targeting) ACMD_NAME(Target_Ref) ACMD_HIDE ACMD_PRODUCTS(StarTrek, FightClub);
void clientTarget_SetTargetRef(U32 iRef)
{
	Entity *pEnt = entActivePlayerPtr();
	if (pEnt)
		entity_SetTarget(pEnt, iRef);
}


AUTO_COMMAND ACMD_ACCESSLEVEL(0) ACMD_CATEGORY(Standard, Targeting) ACMD_NAME(Target_Clear) ACMD_PRODUCTS(StarTrek, FightClub);
void clientTarget_Clear(void)
{
	Entity *e = entActivePlayerPtr();

	if(!e)
		return;
	

	entity_SetTarget(e, 0);
}

AUTO_COMMAND ACMD_ACCESSLEVEL(0) ACMD_CATEGORY(Standard, Powers) ACMD_HIDE ACMD_PRODUCTS(StarTrek, FightClub);
void clientTarget_Cycle(bool bBackwards, U32 targetRequirements, U32 targetExclusions)
{
	clientTarget_CycleEx(bBackwards, targetRequirements, targetExclusions, NULL);
}

void clientTarget_ManualCycle(bool bTargetFriends, bool bBackwards, Vec2 dir) // this basically does what clientTarget_Cycle does, but takes a direction for sorting things by.
{
	const ClientTargetDef *pTarget = NULL;
	Entity *pEntPlayer = entActivePlayerPtr();

	if (!pEntPlayer || gclClientTarget_TargetCyclingDisabled())
	{
		return;
	}

	if(bTargetFriends)
	{
		pTarget = clientTarget_IsTargetHard() ? clientTarget_GetCurrentTarget() : NULL;
		if(pTarget && (!clientTarget_MatchesType(entFromEntityRefAnyPartition(pTarget->entRef), kTargetType_Alive|kTargetType_Friend, kTargetType_Self) || IS_HANDLE_ACTIVE(pTarget->hInteractionNode)))
		{
			clientTarget_Clear();
			return;
		}
		else
		{
			pTarget = clientTarget_FindNextManual(pEntPlayer, bBackwards, kTargetType_Alive|kTargetType_Friend, kTargetType_Self, dir);
		}
	}
	else
	{
		pTarget = clientTarget_FindNextManual(pEntPlayer, bBackwards, kTargetType_Alive|kTargetType_Foe, 0, dir);
	}

	if(pTarget)
	{
		if(pTarget->entRef)
			entity_SetTarget(pEntPlayer,pTarget->entRef);
		else if(IS_HANDLE_ACTIVE(pTarget->hInteractionNode))
			entity_SetTargetObject(pEntPlayer,REF_STRING_FROM_HANDLE(pTarget->hInteractionNode));
		else
			clientTarget_Clear();
	}
	else
	{
		clientTarget_Clear();
	}
}

// Target the nearest enemy or friend
AUTO_COMMAND ACMD_ACCESSLEVEL(0)  ACMD_CATEGORY(Standard, Targeting) ACMD_HIDE ACMD_PRODUCTS(StarTrek, FightClub);
void clientTarget_Nearest(U32 targetRequirements, U32 targetExclusions, float arc, bool bForwardArc, bool bRotArc90, bool bBothArcs)
{
	const ClientTargetDef *pTarget = NULL;
	Entity *pEntPlayer = entActivePlayerPtr();

	if (!pEntPlayer || gclClientTarget_TargetCyclingDisabled())
	{
		return;
	}

	pTarget = clientTarget_FindNearest(pEntPlayer, targetRequirements, targetExclusions, arc, bForwardArc, bRotArc90, bBothArcs);

	if(pTarget)
	{
		if(pTarget->entRef)
			entity_SetTarget(pEntPlayer,pTarget->entRef);
		else if(IS_HANDLE_ACTIVE(pTarget->hInteractionNode))
			entity_SetTargetObject(pEntPlayer,REF_STRING_FROM_HANDLE(pTarget->hInteractionNode));
		else
			clientTarget_Clear();
	}
	else
	{
		clientTarget_Clear();
	}
}

//Targets the next enemy in view
AUTO_COMMAND ACMD_ACCESSLEVEL(0) ACMD_CATEGORY(Standard, Powers) ACMD_PRODUCTS(StarTrek, FightClub);
void Target_Enemy_Next(void)
{
	clientTarget_Cycle(0, kTargetType_Alive|kTargetType_Foe, 0);
}

//Targets the previous enemy in view
AUTO_COMMAND ACMD_ACCESSLEVEL(0) ACMD_CATEGORY(Standard, Powers) ACMD_PRODUCTS(StarTrek, FightClub);
void Target_Enemy_Prev(void)
{
	clientTarget_Cycle(1, kTargetType_Alive|kTargetType_Foe, 0);
}

//Targets the next friend in view
AUTO_COMMAND ACMD_ACCESSLEVEL(0) ACMD_CATEGORY(Standard, Powers) ACMD_PRODUCTS(StarTrek, FightClub);
void Target_Friend_Next(void)
{
	clientTarget_Cycle(0, kTargetType_Alive|kTargetType_Friend, kTargetType_Self);
}

//Targets the previous friend in view
AUTO_COMMAND ACMD_ACCESSLEVEL(0) ACMD_CATEGORY(Standard, Powers) ACMD_PRODUCTS(StarTrek, FightClub);
void Target_Friend_Prev(void)
{
	clientTarget_Cycle(1, kTargetType_Alive|kTargetType_Friend, kTargetType_Self);
}

//Targets the nearest enemy in view
AUTO_COMMAND ACMD_ACCESSLEVEL(0) ACMD_CATEGORY(Standard, Powers) ACMD_PRODUCTS(StarTrek, FightClub);
void Target_Enemy_Near(void)
{
	clientTarget_Nearest(kTargetType_Alive|kTargetType_Foe, 0, 360, 1, 0, 0);
}

//Targets the nearest enemy in view and within the given forward firing arc
AUTO_COMMAND ACMD_ACCESSLEVEL(0) ACMD_CATEGORY(Standard, Powers) ACMD_PRODUCTS(StarTrek, FightClub);
void Target_Enemy_Near_ForArc(float arc)
{
	clientTarget_Nearest(kTargetType_Alive|kTargetType_Foe, 0, arc, 1, 0, 0);
}

//Targets the nearest enemy in view and within the given aft firing arc
AUTO_COMMAND ACMD_ACCESSLEVEL(0) ACMD_CATEGORY(Standard, Powers) ACMD_PRODUCTS(StarTrek, FightClub);
void Target_Enemy_Near_AftArc(float arc)
{
	clientTarget_Nearest(kTargetType_Alive|kTargetType_Foe, 0, arc, 0, 0, 0);
}

//Targets the nearest enemy in view and within the given side firing arc (starboard and port)
AUTO_COMMAND ACMD_ACCESSLEVEL(0) ACMD_CATEGORY(Standard, Powers) ACMD_PRODUCTS(StarTrek, FightClub);
void Target_Enemy_Near_SideArc(float arc)
{
	clientTarget_Nearest(kTargetType_Alive|kTargetType_Foe, 0, arc, 0, 1, 1);
}

//Targets the nearest friend in view
AUTO_COMMAND ACMD_ACCESSLEVEL(0) ACMD_CATEGORY(Standard, Powers) ACMD_PRODUCTS(StarTrek, FightClub);
void Target_Friend_Near(void)
{
	clientTarget_Nearest(kTargetType_Alive|kTargetType_Friend, kTargetType_Self, 360, 1, 0, 0);
}

//Targets the next enemy player in view
AUTO_COMMAND ACMD_ACCESSLEVEL(0) ACMD_CATEGORY(Standard, Powers) ACMD_PRODUCTS(StarTrek, FightClub);
void Target_Enemy_Player_Next(void)
{
	clientTarget_Cycle(0, kTargetType_Alive|kTargetType_Foe|kTargetType_Player, 0);
}

//Targets the previous enemy player in view
AUTO_COMMAND ACMD_ACCESSLEVEL(0) ACMD_CATEGORY(Standard, Powers) ACMD_PRODUCTS(StarTrek, FightClub);
void Target_Enemy_Player_Prev(void)
{
	clientTarget_Cycle(1, kTargetType_Alive|kTargetType_Foe|kTargetType_Player, 0);
}

//Targets the next friendly player in view
AUTO_COMMAND ACMD_ACCESSLEVEL(0) ACMD_CATEGORY(Standard, Powers) ACMD_PRODUCTS(StarTrek, FightClub);
void Target_Friend_Player_Next(void)
{
	clientTarget_Cycle(0, kTargetType_Alive|kTargetType_Friend|kTargetType_Player, kTargetType_Self);
}

//Targets the previous friendly player in view
AUTO_COMMAND ACMD_ACCESSLEVEL(0) ACMD_CATEGORY(Standard, Powers) ACMD_PRODUCTS(StarTrek, FightClub);
void Target_Friend_Player_Prev(void)
{
	clientTarget_Cycle(1, kTargetType_Alive|kTargetType_Friend|kTargetType_Player, kTargetType_Self);
}

//Targets the nearest enemy player in view
AUTO_COMMAND ACMD_ACCESSLEVEL(0) ACMD_CATEGORY(Standard, Powers) ACMD_PRODUCTS(StarTrek, FightClub);
void Target_Enemy_Player_Near(void)
{
	clientTarget_Nearest(kTargetType_Alive|kTargetType_Foe|kTargetType_Player, 0, 360, 1, 0, 0);
}

//Targets the nearest enemy player in view and within the given forward firing arc
AUTO_COMMAND ACMD_ACCESSLEVEL(0) ACMD_CATEGORY(Standard, Powers) ACMD_PRODUCTS(StarTrek, FightClub);
void Target_Enemy_Player_Near_ForArc(float arc)
{
	clientTarget_Nearest(kTargetType_Alive|kTargetType_Foe|kTargetType_Player, 0, arc, 1, 0, 0);
}

//Targets the nearest enemy player in view and within the given aft firing arc
AUTO_COMMAND ACMD_ACCESSLEVEL(0) ACMD_CATEGORY(Standard, Powers) ACMD_PRODUCTS(StarTrek, FightClub);
void Target_Enemy_Player_Near_AftArc(float arc)
{
	clientTarget_Nearest(kTargetType_Alive|kTargetType_Foe|kTargetType_Player, 0, arc, 0, 0, 0);
}

//Targets the nearest enemy player in view and within the given side firing arc (starboard and port)
AUTO_COMMAND ACMD_ACCESSLEVEL(0) ACMD_CATEGORY(Standard, Powers) ACMD_PRODUCTS(StarTrek, FightClub);
void Target_Enemy_Player_Near_SideArc(float arc)
{
	clientTarget_Nearest(kTargetType_Alive|kTargetType_Foe|kTargetType_Player, 0, arc, 0, 1, 1);
}

//Targets the nearest friendly player in view
AUTO_COMMAND ACMD_ACCESSLEVEL(0) ACMD_CATEGORY(Standard, Powers) ACMD_PRODUCTS(StarTrek, FightClub);
void Target_Friend_Player_Near(void)
{
	clientTarget_Nearest(kTargetType_Alive|kTargetType_Friend|kTargetType_Player, kTargetType_Self, 360, 1, 0, 0);
}

AUTO_COMMAND ACMD_ACCESSLEVEL(0)  ACMD_CATEGORY(Standard, Targeting) ACMD_PRODUCTS(StarTrek, FightClub);
void Target_Button_Clear(bool bDown)
{
	if (bDown)
		return; // only react to the release

	clientTarget_Clear();
}

AUTO_COMMAND ACMD_ACCESSLEVEL(0)  ACMD_CATEGORY(Standard, Targeting) ACMD_PRODUCTS(StarTrek, FightClub);
void Target_Button_ToggleModalCycle()
{
	s_bTargetModalFriendly = !s_bTargetModalFriendly;
}

AUTO_COMMAND ACMD_ACCESSLEVEL(0)  ACMD_CATEGORY(Standard, Targeting) ACMD_PRODUCTS(StarTrek, FightClub);
void Target_Button_SetModalCycle(bool bTargetFriendly)
{
	s_bTargetModalFriendly = bTargetFriendly;
}

AUTO_COMMAND ACMD_ACCESSLEVEL(0)  ACMD_CATEGORY(Standard, Targeting) ACMD_PRODUCTS(StarTrek, FightClub);
void Target_Button_ModalCycle(bool down, bool bPrev)
{
	U32 iTargetRequirement = kTargetType_Foe;
	U32 iTargetExclusion = 0;

	if (down)	// targeting only cycles on release
		return;

	if(s_bTargetModalFriendly)
	{
		iTargetRequirement = kTargetType_Friend;
		iTargetExclusion = kTargetType_Self;
	}

	clientTarget_Cycle(bPrev, kTargetType_Alive|iTargetRequirement, iTargetExclusion);
}

// Target the next enemy or friend in order
AUTO_COMMAND ACMD_ACCESSLEVEL(0)  ACMD_CATEGORY(Standard, Targeting) ACMD_PRODUCTS(StarTrek, FightClub);
void Target_Button_Next(bool down, bool bTargetFriends)
{
	U32 iTargetRequirement = kTargetType_Foe;
	U32 iTargetExclusion = 0;

	if (down)	// targeting only cycles on release
		return;

	if(bTargetFriends)
	{
		iTargetRequirement = kTargetType_Friend;
		iTargetExclusion = kTargetType_Self;
	}

	clientTarget_Cycle(0, kTargetType_Alive|iTargetRequirement, iTargetExclusion);
}

// Find the previous targetable entity and target it.
AUTO_COMMAND ACMD_ACCESSLEVEL(0)  ACMD_CATEGORY(Standard, Targeting) ACMD_PRODUCTS(StarTrek, FightClub);
void Target_Button_Prev(bool down, bool bTargetFriends)
{
	U32 iTargetRequirement = kTargetType_Foe;
	U32 iTargetExclusion = 0;

	if (down)	// targeting only cycles on release
		return;

	if(bTargetFriends)
	{
		iTargetRequirement = kTargetType_Friend;
		iTargetExclusion = kTargetType_Self;
	}

	clientTarget_Cycle(1, kTargetType_Alive|iTargetRequirement, iTargetExclusion);
}

// Find the previous targetable entity and target it.
AUTO_COMMAND ACMD_ACCESSLEVEL(0)  ACMD_CATEGORY(Standard, Targeting) ACMD_PRODUCTS(StarTrek, FightClub);
void Target_Manual_Modal(int vertical, int horizontal)
{
	Vec2 dir;
	dir[0] = horizontal;
	dir[1] = vertical;
	clientTarget_ManualCycle(s_bTargetModalFriendly, 0, dir);
}


// Target the current player
AUTO_COMMAND ACMD_ACCESSLEVEL(0) ACMD_CATEGORY(Powers) ACMD_PRODUCTS(StarTrek, FightClub);
void Target_Self(void)
{
	Entity *pPlayer = entActivePlayerPtr();
	if (pPlayer)
	{
		entity_SetTarget(pPlayer, entGetRef(pPlayer));
	}
}

// Target the Nth person in your team.
AUTO_COMMAND ACMD_ACCESSLEVEL(0) ACMD_CATEGORY(Powers) ACMD_PRODUCTS(StarTrek, FightClub);
void Target_Teammate(S32 iTeammate)
{
	Entity *pPlayer = entActivePlayerPtr();
	Entity *pTargetEnt;
	Entity **eaTeamEnts = NULL;
	Team *pTeam = team_GetTeam(pPlayer);
	
	if (gclClientTarget_TargetCyclingDisabled())
	{
		return;
	}

	team_GetTeamListSelfFirst(pPlayer, &eaTeamEnts, NULL, false, true);

	pTargetEnt = eaGet(&eaTeamEnts, iTeammate-1);
	if (pTargetEnt)
	{
		entity_SetTarget(pPlayer, entGetRef(pTargetEnt));
	}
	else
	{
		clientTarget_Clear();
	}

	eaDestroy(&eaTeamEnts);
}

// Target the focus target
AUTO_COMMAND ACMD_ACCESSLEVEL(0) ACMD_CATEGORY(Powers) ACMD_PRODUCTS(StarTrek, FightClub);
void Target_Focus(void)
{
	Entity *pPlayer = entActivePlayerPtr();
	if (pPlayer && pPlayer->pChar && pPlayer->pChar->erTargetFocus)
	{
		entity_SetTarget(pPlayer, pPlayer->pChar->erTargetFocus);
	}
}



//--------ACCESS LEVEL 9 COMMANDS---------------//

AUTO_COMMAND ACMD_CATEGORY(Standard, Targeting) ACMD_NAME(SelectAnyEntity);
void clientTarget_SelectAny(bool bTargetAny)
{
	g_bSelectAnyEntity = bTargetAny;
}

