#include "gclEntity.h"
#include "dynFxInterface.h"
#include "dynFxManager.h"
#include "dynAnimInterface.h"
#include "dynSkeleton.h"
#include "StringCache.h"
#include "EntityMovementManager.h"
#include "EntityMovementDefault.h"
#include "Character.h"
#include "AutoGen/Entity_h_ast.h"
#include "CostumeCommonEntity.h"
#include "CostumeCommonGenerate.h"
#include "EntityMovementFx.h"
#include "EntityClient.h"
#include "wlCostume.h"
#include "GraphicsLib.h"
#include "timing.h"
#include "GlobalTypes.h"

AUTO_RUN_ANON(memBudgetAddMapping(__FILE__, BUDGET_GameSystems););

typedef struct DeadBodyEntity
{
	Vec3				vDeadBodyPos;
	ClientOnlyEntity	*pCODeadBodyEntity;
	EntityRef			erSourceBody;
	EntityRef			erDeadBody;

	S64					timeDied;
	S64					timeRespawned;
	
	U32					bQueued : 1;
	U32					bIgnoreDeadBody : 1;
	U32					bSettled : 1;
} DeadBodyTracker;


static struct 
{
	DeadBodyTracker **eaDeadBodyTrackers;
		
	S32 iMaxDeadBodies;
	F32 fBodyLifetime;
	S32 bDebugging;

	U32					bEnabled : 1;

} s_deadBodyData = {0};

// defs?
// - max body count (probably customized per graphic settings level)
// - dead body lifetime
// - send distance

#define FX_KILL_MESSAGE "DeadBodyKillFX"

// todo: filter bodies that die by falling off the map

AUTO_CMD_INT(s_deadBodyData.bDebugging, DeadBodiesDebug);
AUTO_CMD_INT(s_deadBodyData.iMaxDeadBodies, DeadBodiesMax);

AUTO_COMMAND ACMD_NAME(DeadBodiesEnable);
void gckDeadBodies_Enable(int bEnable)
{
	if (gConf.bEnableDeadPlayerBodies)
	{
		s_deadBodyData.bEnabled = !!bEnable;
	}
}

// ------------------------------------------------------------------------------------------------------------
static DeadBodyTracker* findDeadBodyForEntityRef(EntityRef erEntity)
{
	FOR_EACH_IN_EARRAY(s_deadBodyData.eaDeadBodyTrackers, DeadBodyTracker, pDeadBody)
	{
		if (pDeadBody->erSourceBody == erEntity)
			return pDeadBody;
	}
	FOR_EACH_END
	return NULL;
}

// ------------------------------------------------------------------------------------------------------------
static DeadBodyTracker* allocDeadBodyTracker()
{
	return calloc(1, sizeof(DeadBodyTracker));
}

// ------------------------------------------------------------------------------------------------------------
static void freeDeadBodyTracker(DeadBodyTracker *p)
{
	if (p)
	{	
		if (p->pCODeadBodyEntity)
		{
			gclClientOnlyEntityDestroy(&p->pCODeadBodyEntity);
		}
		free (p);
	}
}

// ------------------------------------------------------------------------------------------------------------
// should be called on mapload
void gclDeadBodies_Initialize()
{
	if (gConf.bEnableDeadPlayerBodies)
	{
		s_deadBodyData.bEnabled = true;
		s_deadBodyData.iMaxDeadBodies = 15;
		s_deadBodyData.bDebugging = false;
		s_deadBodyData.fBodyLifetime = 3.f;
	}
}

// ------------------------------------------------------------------------------------------------------------
// should be called on map unload
void gclDeadBodies_Shutdown()
{
	// clean up s_eaDeadBodyList, but are entities auto destroyed?
	eaDestroyEx(&s_deadBodyData.eaDeadBodyTrackers, freeDeadBodyTracker);
	s_deadBodyData.eaDeadBodyTrackers = NULL;
	s_deadBodyData.bEnabled = false;
}


// ------------------------------------------------------------------------------------------------------------
static void gclDeadBodies_SetInvisible(Entity* e, int invisible)
{
	if (invisible)
	{
		e->fAlpha = 0.f;
		e->bDeadBodyFaded = 1;
	}
	else
	{
		e->fAlpha = 1.f;
		e->bDeadBodyFaded = 0;
	}
	dtDrawSkeletonSetAlpha(e->dyn.guidDrawSkeleton, e->fAlpha, e->fAlpha);
}

// ------------------------------------------------------------------------------------------------------------
static void gclDeadBodies_CreateBodyForEntity(DeadBodyTracker *pTracker, Entity *pSourceEnt)
{
	Entity *pNewEnt;
	ClientOnlyEntity *pDeadCOEnt;
	char cBuffer[256];

	pTracker->pCODeadBodyEntity = gclClientOnlyEntityCreate(true);
	pDeadCOEnt = pTracker->pCODeadBodyEntity;
	pDeadCOEnt->noAutoFree = true;
	pDeadCOEnt->oldEntityRef = entGetRef(pSourceEnt);
	
	pNewEnt = pTracker->pCODeadBodyEntity->entity;
	pTracker->erDeadBody = entGetRef(pNewEnt);
	
		
	pNewEnt->fEntitySendDistance = ENTITY_DEFAULT_SEND_DISTANCE; 
		
	// copy over the positional entity information
	copyVec3(pSourceEnt->posNextFrame, pNewEnt->posNextFrame);
	pNewEnt->locationNextFrameValid = pSourceEnt->locationNextFrameValid;
	copyVec3(pSourceEnt->pos_use_accessor, pNewEnt->pos_use_accessor);
	copyQuat(pSourceEnt->rot_use_accessor, pNewEnt->rot_use_accessor);
	copyVec2(pSourceEnt->pyFace_use_accessor, pNewEnt->pyFace_use_accessor);

	// steal 
	#define MOVE_ENTITY_VALUE(x) pNewEnt->x = pSourceEnt->x;pSourceEnt->x = 0
		MOVE_ENTITY_VALUE(posViewIsAtRest);
		MOVE_ENTITY_VALUE(rotViewIsAtRest);
		MOVE_ENTITY_VALUE(pyFaceViewIsAtRest);
		MOVE_ENTITY_VALUE(frameWhenViewChanged);
		MOVE_ENTITY_VALUE(frameWhenViewSet);
		MOVE_ENTITY_VALUE(dyn.guidRoot);
		MOVE_ENTITY_VALUE(dyn.guidLocation);
		MOVE_ENTITY_VALUE(dyn.guidSkeleton);
		MOVE_ENTITY_VALUE(dyn.guidDrawSkeleton);
		MOVE_ENTITY_VALUE(dyn.guidFxMan);
		MOVE_ENTITY_VALUE(fHue);
	#undef MOVE_ENTITY_VALUE

	{
		PlayerCostume *pCostume = costumeEntity_GetEffectiveCostume(pSourceEnt);
		if(pCostume)
			pNewEnt->costumeRef.pEffectiveCostume = StructClone(parse_PlayerCostume, pCostume);
	}
	
	// create new tags with this new entity name
	sprintf(cBuffer, "%s-Location", pNewEnt->debugName);
	dtNodeSetTag(pNewEnt->dyn.guidLocation, allocAddString(cBuffer));
	sprintf(cBuffer, "%s-Root", pNewEnt->debugName);
	dtNodeSetTag(pNewEnt->dyn.guidRoot, allocAddString(cBuffer));
	
	// create a character if the source ent had it and set up some basic data for it
	if (pSourceEnt->pChar)
	{
		NOCONST(Entity) *pNoConstEnt = CONTAINER_NOCONST(Entity, (pDeadCOEnt->entity));
		pNoConstEnt->pChar = StructCreateNoConst(parse_Character);
		character_Reset(entGetPartitionIdx(pNewEnt), pNewEnt->pChar, pNewEnt, NULL);
		if (pSourceEnt->pChar->pattrBasic && pNewEnt->pChar->pattrBasic)
		{	// need this so damage FX can work
			pNewEnt->pChar->pattrBasic->fHitPointsMax = pSourceEnt->pChar->pattrBasic->fHitPointsMax;
		}
	}
	// set this entity visible
	costumeGenerate_FixEntityCostume(pNewEnt);

	{
		DynSkeleton* s = dynSkeletonFromGuid(pNewEnt->dyn.guidSkeleton);
		dynSkeletonSetLogReceiver(s, NULL);
		dtSkeletonSetCallbacks(	pNewEnt->dyn.guidSkeleton, NULL, NULL, NULL, NULL);
	}

	gclDeadBodies_SetInvisible(pNewEnt, false);

	// send a message to the FX manager to kill any FX that care
	{
		DynFxManager* pFXMan = dynFxManFromGuid(pNewEnt->dyn.guidFxMan);
		if (pFXMan)
		{
			dynFxManBroadcastMessage(pFXMan, FX_KILL_MESSAGE);
		}
	}

	// since we stole the dyn info from the old entity, recreate it all here
	{
		sprintf(cBuffer, "%s-Location", pSourceEnt->debugName);
		pSourceEnt->dyn.guidLocation = dtNodeCreate();
		dtNodeSetTag(pSourceEnt->dyn.guidLocation, allocAddString(cBuffer));

		sprintf(cBuffer, "%s-Root", pSourceEnt->debugName);
		pSourceEnt->dyn.guidRoot = dtNodeCreate();
		dtNodeSetTag(pSourceEnt->dyn.guidRoot, allocAddString(cBuffer));
		
		costumeGenerate_FixEntityCostume(pSourceEnt);
		gclDeadBodies_SetInvisible(pSourceEnt, true);
	}
	
}

// ------------------------------------------------------------------------------------------------------------
static int gclDeadBodies_ShouldCreateDeadBody(DeadBodyTracker *pTracker, Entity *e)
{
	if (ABS_TIME_PASSED(pTracker->timeDied, 2.f))
	{	// check to see if the body didn't move for some reason
		Vec3 vCurPos;
		entGetPos(e, vCurPos);
		return (distance3Squared(vCurPos, pTracker->vDeadBodyPos) < 3.f);
	}
	
	return false;
}

// ------------------------------------------------------------------------------------------------------------
static int gclDeadBodies_ShouldIgnoreDeadBody(Entity *pEnt)
{
	DynDrawSkeleton *pSkeleton = dynDrawSkeletonFromGuid(pEnt->dyn.guidDrawSkeleton);
	if (pEnt->fAlpha == 0.f || !pSkeleton || pSkeleton->fTotalAlpha == 0.f)
		return true;
	return false;
}

// ------------------------------------------------------------------------------------------------------------
void gclDeadBodies_OncePerFrame()
{
	S32 i;
	
	if (!s_deadBodyData.bEnabled)
		return;

	// go through all the dead trackers and for the ones that haven't created a dead body see if it's time to create one
	for (i = eaSize(&s_deadBodyData.eaDeadBodyTrackers) -1; i >= 0; i--)
	{
		DeadBodyTracker * pTracker = s_deadBodyData.eaDeadBodyTrackers[i];
		if (!pTracker->erDeadBody)
		{	// haven't created a dead body for this guy yet
			Entity *ent = pTracker->erSourceBody ? entFromEntityRefAnyPartition(pTracker->erSourceBody) : NULL;
					
			if (!ent)
			{
				// entity is no longer valid to create a dead body, lets delete the tracker
				freeDeadBodyTracker(pTracker);
				eaRemove(&s_deadBodyData.eaDeadBodyTrackers, i);
				continue;
			}
			if (entIsAlive(ent))
			{
				// entity is no longer valid to create a dead body, lets delete the tracker
				freeDeadBodyTracker(pTracker);
				eaRemove(&s_deadBodyData.eaDeadBodyTrackers, i);
				continue;
			}

			if (!pTracker->bIgnoreDeadBody)
			{
				pTracker->bIgnoreDeadBody = gclDeadBodies_ShouldIgnoreDeadBody(ent);
			}

			if (!pTracker->bSettled)
			{
				Vec3 vVelocity = {0};
				entCopyVelocityFG(ent, vVelocity);
				pTracker->bSettled = (lengthVec3Squared(vVelocity) <= 0.0001);
				if (pTracker->bSettled)
				{
					entGetPos(ent, pTracker->vDeadBodyPos);
				}
			}
			else if (gclDeadBodies_ShouldCreateDeadBody(pTracker, ent))
			{
				if (pTracker->bIgnoreDeadBody)
				{
					if (!ent->bDeadBodyFaded)
						gclDeadBodies_SetInvisible(ent, true);
				}
				else
				{
					pTracker->bQueued = true;
					gclDeadBodies_CreateBodyForEntity(pTracker, ent);
				}
			}
		}
		else if (pTracker->erSourceBody)
		{
			Entity *ent = entFromEntityRefAnyPartition(pTracker->erSourceBody);
			if (!ent || entIsAlive(ent))
			{
				pTracker->erSourceBody = 0;
				pTracker->timeRespawned = ABS_TIME;
			}
		}
		else if (s_deadBodyData.fBodyLifetime != 0.f && ABS_TIME_PASSED(pTracker->timeRespawned, s_deadBodyData.fBodyLifetime))
		{
			// we are fading out the dead bodies after a certain amount of time
			Entity *eBody = entFromEntityRefAnyPartition(pTracker->erDeadBody);
			if (eBody)
			{
				eBody->bFadeOutAndThenRemove = true;
			}
			pTracker->pCODeadBodyEntity = NULL;
			freeDeadBodyTracker(pTracker);
			eaRemove(&s_deadBodyData.eaDeadBodyTrackers, i);
		}
	}

	if (eaSize(&s_deadBodyData.eaDeadBodyTrackers) > s_deadBodyData.iMaxDeadBodies)
	{
		DeadBodyTracker * pTracker = s_deadBodyData.eaDeadBodyTrackers[0];
		eaRemove(&s_deadBodyData.eaDeadBodyTrackers, 0);
		if (pTracker->erDeadBody)
		{
			Entity *eBody = entFromEntityRefAnyPartition(pTracker->erDeadBody);

			if (eBody)
			{
				eBody->bFadeOutAndThenRemove = true;
			}
			pTracker->pCODeadBodyEntity = NULL;
			freeDeadBodyTracker(pTracker);
		}
	}
	
}

// ------------------------------------------------------------------------------------------------------------
void gclDeadBodies_EntityTick(Entity *e)
{
	if (!s_deadBodyData.bEnabled || !entIsPlayer(e))
		return;

	if (!entIsAlive(e))
	{
		DeadBodyTracker *pTracker;
		
		pTracker = findDeadBodyForEntityRef(entGetRef(e));
		if (!pTracker)
		{
			// create a new tracker
			pTracker = allocDeadBodyTracker();
			pTracker->erSourceBody = entGetRef(e);
			pTracker->timeDied = ABS_TIME;
			

			eaPush(&s_deadBodyData.eaDeadBodyTrackers, pTracker);
			return;
		}
	}
	else if (e->bDeadBodyFaded)
	{
		if (entIsAlive(e))
		{
			/*
			DeadBodyTracker *pTracker = findDeadBodyForEntityRef(entGetRef(e));
			if (pTracker)
			{
				pTracker->erSourceBody = 0;
				pTracker->timeRespawned = ABS_TIME;
			}
			*/
			gclDeadBodies_SetInvisible(e, false);
		}
	}
}