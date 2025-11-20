#pragma once
GCC_SYSTEM
/***************************************************************************
 *     Copyright (c) Cryptic Studios
 *     All Rights Reserved
 *     Confidential Property of Cryptic Studios
 ***************************************************************************/
#ifndef GSLTIMECONTROL_H__
#define GSLTIMECONTROL_H__


AUTO_STRUCT;
typedef struct TimeControlConfig
{
	F32 fMaxTime;
		// How much slow-time you can store up

	F32 fSlowTimeRate;
		// How fast time runs when in slow-time

	F32 fRechargeRate;
		// The rate at which you gain slow-time.

	int *piMapTypesAllowed; AST(SUBTABLE(ZoneMapTypeEnum))
		// A list of map types where time control is allowed.

	int *piRegionTypesAllowed; AST(SUBTABLE(WorldRegionTypeEnum))
		// A list of region types where time control is allowed.
		// If empty, all region types are allowed.

	U32 uMaxPlayersOnMapAllowed; AST(NAME(MaxPlayersOnMapAllowed))
		// The maximum number of players on the map before time control
		//   is shut off. If there are more than this number, time control
		//   is disabled.
		// If 0, there is no limit.

	U32 bMustBeInCombat: 1;
		// If true, the player must be in combat to use slow-time.

	U32 bMustBeTeamLeader: 1;
		// If true, the player must be a team leader to use slow-time.

} TimeControlConfig;

extern TimeControlConfig g_TimeControlConfig;


void timecontrol_OncePerFrame(void);


#endif /* #ifndef TIMECONTROL_H__ */

/* End of File */

