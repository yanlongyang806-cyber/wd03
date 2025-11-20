#include "gclCamera.h"
#include "GlobalTypes.h"
#include "gclEntity.h"
#include "Character_target.h"
#include "EntityIterator.h"
#include "entCritter.h"
#include "inputKeyBind.h"
#include "WorldGrid.h"

AUTO_RUN_ANON(memBudgetAddMapping(__FILE__, BUDGET_GameSystems););


typedef struct SpectatorSettings
{
	F32			fTimeAfterDeathTillSpectate;
} SpectatorSettings;

static struct
{
	S64							timeWhenDetectedDeath;
	EntityRef					erCurrentFollowEntity;

	REF_TO(CritterFaction)		hFaction;

	// flags
	U32							localPlayerIsDead;
	U32							enabled;

	U32							bIsSpectating;

} s_spectatorData;

void gclSpectator_Enable(int bEnble);
static void gclSpectator_StopSpectating();
int gclSpectator_IsSpectating();

static SpectatorSettings s_spectatorSettings = {0};
static int gclSpectator_FollowNextEnt(int bNext);
#define SPECTATOR_KEYBIND_DEF	"Spectator"

AUTO_RUN;
void gclSpectator_AutoRun(void)
{
	s_spectatorSettings.fTimeAfterDeathTillSpectate = 3.f;
	if (gConf.bEnableSpectator)
	{
		gclSpectator_Enable(true);
	}
}


// -------------------------------------------------------------------------------------------------------------------------
AUTO_COMMAND ACMD_NAME(SpectatorEnable);
void gclSpectator_Enable(int bEnble)
{
	if (s_spectatorData.enabled != (U32)bEnble)
	{
		s_spectatorData.enabled = bEnble;
		
		s_spectatorData.localPlayerIsDead = false;
		s_spectatorData.timeWhenDetectedDeath = 0;
		s_spectatorData.erCurrentFollowEntity = 0;
	}

}

// -------------------------------------------------------------------------------------------------------------------------
void gclSpectator_MapUnload()
{
	gclSpectator_StopSpectating();
	
}

// -------------------------------------------------------------------------------------------------------------------------
AUTO_COMMAND ACMD_NAME(SpectatorNext) ACMD_ACCESSLEVEL(0) ACMD_PRODUCTS(Bronze);
void gclSpectator_Follow_Next()
{
	if (gclSpectator_IsSpectating())
		gclSpectator_FollowNextEnt(true);
}

// -------------------------------------------------------------------------------------------------------------------------
AUTO_COMMAND ACMD_NAME(SpectatorPrevious) ACMD_ACCESSLEVEL(0) ACMD_PRODUCTS(Bronze);
void gclSpectator_Follow_Previous()
{
	if (gclSpectator_IsSpectating())
		gclSpectator_FollowNextEnt(false);
}

// -------------------------------------------------------------------------------------------------------------------------
static int gclSpectator_IsValidEntity(Entity *e)
{
	if (entIsAlive(e))
	{
		Entity *localPlayerEnt = entActivePlayerPtr();
		CritterFaction *spectateFaction = GET_REF(s_spectatorData.hFaction);

		if ((!spectateFaction && !localPlayerEnt))
			return false;

		if (localPlayerEnt)
		{
			ANALYSIS_ASSUME(localPlayerEnt);
			return kEntityRelation_Friend == critter_IsKOSEx(PARTITION_CLIENT, e, localPlayerEnt, true);
		}
		else
		{
			CritterFaction *entFaction;
			ANALYSIS_ASSUME(spectateFaction);
			entFaction = entGetFaction(e);
			if (!entFaction)
				return false;
			ANALYSIS_ASSUME(entFaction);
			return kEntityRelation_Friend == faction_GetRelation(spectateFaction, entFaction);
		}
	}

	return false;
}

// -------------------------------------------------------------------------------------------------------------------------
static int gclSpectator_BuildValidEntityList(Entity ***peaValidEntities)
{
	EntityIterator* iter; 
	Entity *curEnt = NULL;

	Entity *localPlayerEnt = entActivePlayerPtr();
	eaClear(peaValidEntities);
	
	iter = entGetIteratorSingleTypeAllPartitions(0, ENTITYFLAG_IGNORE, GLOBALTYPE_ENTITYPLAYER);
	while (curEnt = EntityIteratorGetNext(iter))
	{
		if (curEnt == localPlayerEnt)
			continue;
		if (gclSpectator_IsValidEntity(curEnt))
		{
			eaPush(peaValidEntities, curEnt);
		}
	}

	EntityIteratorRelease(iter);
	return true;
}


// -------------------------------------------------------------------------------------------------------------------------
static Entity* gclSpectator_GetFollowEntityOrdered(EntityRef erCurEnt, int bNext)
{
	static Entity **s_eaValidEntities = NULL;
	S32 i;
	S32 count = 0;

	gclSpectator_BuildValidEntityList(&s_eaValidEntities);

	count =  eaSize(&s_eaValidEntities);

	if (!count)
		return NULL;

	for(i = 0; i < eaSize(&s_eaValidEntities); ++i)
	{
		Entity *e = s_eaValidEntities[i];
		if (entGetRef(e) == erCurEnt)
		{
			if (bNext)
			{
				S32 next = i + 1;
				if (next >= count)
					return s_eaValidEntities[0];
				return s_eaValidEntities[next];
			}
			else
			{
				S32 prev = i - 1;
				if (prev < 0)
					return s_eaValidEntities[count -1];
				return s_eaValidEntities[prev];
			}
		}
	}

	return s_eaValidEntities[0];
}

// -------------------------------------------------------------------------------------------------------------------------
static int gclSpectator_FollowNextEnt(int bNext)
{
	Entity *pFollow = gclSpectator_GetFollowEntityOrdered(s_spectatorData.erCurrentFollowEntity, bNext);
	if (pFollow)
	{
		s_spectatorData.erCurrentFollowEntity = entGetRef(pFollow);
		s_spectatorData.timeWhenDetectedDeath = 0;
		// tell the camera who to follow
		gclCamera_SetFocusEntityOverride(pFollow);

		// something probably specific to game, hard-coding the camera switch behavior
		gclCamera_SetMode(kCameraMode_ChaseCamera, false);
		return true;
	}
	

	return false;
}


// -------------------------------------------------------------------------------------------------------------------------
static void gclSpectator_StartSpectating()
{
	if (!s_spectatorData.bIsSpectating)
	{
		KeyBindProfile* pSpectatorKeybinds = keybind_FindProfile(SPECTATOR_KEYBIND_DEF);

		s_spectatorData.bIsSpectating = true;

		gclSpectator_FollowNextEnt(true);

		if (pSpectatorKeybinds)
		{
			keybind_PushProfile(pSpectatorKeybinds);
		}
	}
	
}

// -------------------------------------------------------------------------------------------------------------------------
static void gclSpectator_StopSpectating()
{
	if (s_spectatorData.bIsSpectating)
	{
		KeyBindProfile* pSpectatorKeybinds = keybind_FindProfile(SPECTATOR_KEYBIND_DEF);

		if (pSpectatorKeybinds)
		{
			keybind_PopProfile(pSpectatorKeybinds);
		}

		s_spectatorData.erCurrentFollowEntity = 0;
		s_spectatorData.bIsSpectating = false;
		// tell the camera to stop following
		gclCamera_SetFocusEntityOverride(NULL);

		// something probably specific to game, hard-coding the camera switch behavior
		gclCamera_SetMode(kCameraMode_Default, false);
	}
}

// -------------------------------------------------------------------------------------------------------------------------
int gclSpectator_IsSpectating()
{
	return s_spectatorData.bIsSpectating && s_spectatorData.erCurrentFollowEntity;
}

// -------------------------------------------------------------------------------------------------------------------------
Entity* gclSpectator_GetSpectatingEntity()
{
	return (s_spectatorData.bIsSpectating && s_spectatorData.erCurrentFollowEntity) ? 
				entFromEntityRefAnyPartition(s_spectatorData.erCurrentFollowEntity) : NULL;
}


// -------------------------------------------------------------------------------------------------------------------------
static void gclSpectator_UpdateAsSpectator()
{
	Entity *e = entActivePlayerPtr();
	Entity *pFollowEnt = NULL;
	if (e && entIsAlive(e))	
	{	// we are alive
		gclSpectator_StopSpectating();
		s_spectatorData.localPlayerIsDead = true;
		s_spectatorData.timeWhenDetectedDeath = ABS_TIME;
		return;
	}

	pFollowEnt = entFromEntityRefAnyPartition(s_spectatorData.erCurrentFollowEntity);
	
	if (!pFollowEnt)
	{// auto switch immediately
		if (!gclSpectator_FollowNextEnt(true))
		{
			gclSpectator_StopSpectating();
			return;
		}
	}
	else
	{
		if (!entIsAlive(pFollowEnt))
		{	// wait some time after this guy is dead before we switch
			if (s_spectatorData.timeWhenDetectedDeath == 0)
			{
				s_spectatorData.timeWhenDetectedDeath = ABS_TIME;
			}
			else if (ABS_TIME_PASSED(s_spectatorData.timeWhenDetectedDeath, s_spectatorSettings.fTimeAfterDeathTillSpectate))
			{
				if (!gclSpectator_FollowNextEnt(true))
				{
					gclSpectator_StopSpectating();
					return;
				}
			}
		}
	}
}

// -------------------------------------------------------------------------------------------------------------------------
static void gclSpectator_UpdateNotSpectating()
{
	Entity *e = entActivePlayerPtr();


	if (!s_spectatorData.localPlayerIsDead)
	{
		if (!e || !entIsAlive(e))	
		{	// we are dead, start checking if we should to go spectator mode
			s_spectatorData.localPlayerIsDead = true;
			s_spectatorData.timeWhenDetectedDeath = ABS_TIME;
			return;
		}
	}
	else
	{
		if (e && entIsAlive(e))	
		{
			s_spectatorData.localPlayerIsDead = false;
			s_spectatorData.timeWhenDetectedDeath = 0;
			return;
		}

		if (ABS_TIME_PASSED(s_spectatorData.timeWhenDetectedDeath, s_spectatorSettings.fTimeAfterDeathTillSpectate))
		{
			gclSpectator_StartSpectating();
		}
	}
}

// -------------------------------------------------------------------------------------------------------------------------
void gclSpectator_UpdateLocalPlayer()
{
	Entity *e = entActivePlayerPtr();

	if (s_spectatorData.enabled == false)
		return;
	if (zmapInfoGetMapType(NULL) != ZMTYPE_PVP)
		return;

	
	if (s_spectatorData.bIsSpectating)
	{
		gclSpectator_UpdateAsSpectator();
	}
	else
	{
		gclSpectator_UpdateNotSpectating();
	}

}


// -------------------------------------------------------------------------------------------------------------------------
AUTO_EXPR_FUNC(UIGen) ACMD_NAME(SpectatorGetFollowingEntName);
const char* gclSpectator_GetFollowingEntity()
{
	Entity *pEnt = gclSpectator_GetSpectatingEntity();
	const char *pchName = pEnt ? entGetLocalName(pEnt) : NULL;
	return pchName ? pchName : "";
}

// -------------------------------------------------------------------------------------------------------------------------
AUTO_EXPR_FUNC(UIGen) ACMD_NAME(SpectatorIsFollowingEnt);
int gclSpectator_IsFollowingEnt()
{
	return gclSpectator_GetSpectatingEntity() != NULL;
}