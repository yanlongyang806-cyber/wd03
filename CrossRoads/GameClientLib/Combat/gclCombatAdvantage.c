#include "gclCombatAdvantage.h"
#include "Character.h"
#include "CombatAdvantage.h"
#include "CombatConfig.h"
#include "DamageTracker.h"
#include "dynFxInterface.h"
#include "dynFxInfo.h"
#include "dynNodeInline.h"
#include "dynFx.h"

#include "Entity.h"
#include "gclEntity.h"
#include "GlobalTypes.h"
#include "MemoryPool.h"
#include "PowerAnimFX.h"
#include "PowerApplication.h"
#include "PowersMovement.h"
#include "StringCache.h"
#include "gclCombatAdvantage_c_ast.h"

#include "EntityIterator.h"

AUTO_RUN_ANON(memBudgetAddMapping(__FILE__, BUDGET_GameSystems););

typedef struct CombatAdvantageFlankFX
{
	F32		fArcMin;
	F32		fArcMax;
	bool	bContainsPlayer;
} CombatAdvantageFlankFX;

AUTO_STRUCT;
typedef struct CombatAdvantageEntity
{
	EntityRef	erEntity;							AST(KEY)

	AST_STOP
	F32						*eafUpcomingHitframes;				
	U32						bHasUnconditionalAdvantage;			
	CombatAdvantageFlankFX	**eaCombatAdvantageFX;	
	dtFx					hCombatAdvantageFX;
	F32						fBaseYaw;
} CombatAdvantageEntity;

extern ParseTable parse_DynDefineParam[];
#define TYPE_parse_DynDefineParam DynDefineParam
extern ParseTable parse_DynParamBlock[];
#define TYPE_parse_DynParamBlock DynParamBlock

#define MAX_INTERVALS	3

MP_DEFINE(CombatAdvantageFlankFX);
MP_DEFINE(CombatAdvantageEntity);

#define COMBAT_ADVANTAGE_ENTITY_IGNORE_FLAGS (ENTITYFLAG_DEAD | ENTITYFLAG_DESTROY | ENTITYFLAG_DONOTDRAW | ENTITYFLAG_DONOTSEND | ENTITYFLAG_IGNORE | ENTITYFLAG_UNSELECTABLE | ENTITYFLAG_UNTARGETABLE)


static struct 
{
	int bEnableFlankFX;
	int bInit;
	CombatAdvantageEntity **eaCombatFlankEntities;
	F32	*eafUpcomingHostileHitframes;
	S32* eaUpcomingHostileHitframeOwners;

	CombatAdvantageEntity	caPlayer;

} s_combatAdvantageData = {true, false, NULL, NULL, NULL, 0};


static void gclCombatAdvantage_UpdateFlankFX(Entity *pEnt, CombatAdvantageEntity* pFlankEnt, bool bForceHitFrame);
static void gclCombatAdvantage_UpdateLocalPlayer();

// -----------------------------------------------------------------------------------------------------------------------
static __forceinline bool gclCombatAdvantage_IsEnabled()
{
	return s_combatAdvantageData.bEnableFlankFX && g_CombatConfig.pCombatAdvantage != NULL;
}

AUTO_COMMAND;
void gclCombatAdvantageEnable(int bEnable)
{
	s_combatAdvantageData.bEnableFlankFX = bEnable;
}

// -----------------------------------------------------------------------------------------------------------------------
static void freeCombatAdvantageFlankFX(CombatAdvantageFlankFX *pFX)
{
	if (pFX)
	{
		MP_FREE(CombatAdvantageFlankFX, pFX);
	}
}

// -----------------------------------------------------------------------------------------------------------------------
static void freeCombatAdvantageEntity(CombatAdvantageEntity *pFlankEnt)
{
	if (pFlankEnt)
	{
		if (pFlankEnt->eafUpcomingHitframes)
			eafDestroy(&pFlankEnt->eafUpcomingHitframes);

		dtFxKill(pFlankEnt->hCombatAdvantageFX);
		pFlankEnt->hCombatAdvantageFX = 0;

		eaDestroyEx(&pFlankEnt->eaCombatAdvantageFX, freeCombatAdvantageFlankFX);
		MP_FREE(CombatAdvantageEntity, pFlankEnt);
	}
}

// -----------------------------------------------------------------------------------------------------------------------
static CombatAdvantageEntity* gclCombatAdvantage_FindOrAddCombatAdvantageEntity(EntityRef erRef, bool bCreate)
{
	CombatAdvantageEntity *pFlankEnt = eaIndexedGetUsingInt(&s_combatAdvantageData.eaCombatFlankEntities, erRef);

	if (!pFlankEnt && bCreate)
	{
		MP_CREATE(CombatAdvantageEntity, 16);
		pFlankEnt = MP_ALLOC(CombatAdvantageEntity);
		
		pFlankEnt->erEntity = erRef;

		eaIndexedAdd(&s_combatAdvantageData.eaCombatFlankEntities, pFlankEnt);
	}
	
	return pFlankEnt;
	
}

// -----------------------------------------------------------------------------------------------------------------------
void gclCombatAdvantage_OncePerFrame(F32 fElapsed)
{
	S32 i;
	if (!gclCombatAdvantage_IsEnabled())
	{
		return;
	}

	if (!s_combatAdvantageData.bInit)
	{
		s_combatAdvantageData.bInit = true;
		eaIndexedEnable(&s_combatAdvantageData.eaCombatFlankEntities, parse_CombatAdvantageEntity);
	}
	
	for (i = 0; i < eafSize(&s_combatAdvantageData.eafUpcomingHostileHitframes); i++)
	{
		s_combatAdvantageData.eafUpcomingHostileHitframes[i] -= fElapsed;
		if (s_combatAdvantageData.eafUpcomingHostileHitframes[i] < -g_CombatConfig.pCombatAdvantage->fFXHitFrameTimer)
		{
			eaiRemove(&s_combatAdvantageData.eaUpcomingHostileHitframeOwners, i);
			eaiRemove(&s_combatAdvantageData.eafUpcomingHostileHitframes, i);
		}
	}
	
	FOR_EACH_IN_EARRAY(s_combatAdvantageData.eaCombatFlankEntities, CombatAdvantageEntity, pFlankEnt)
	{
		Entity *pEnt = entFromEntityRefAnyPartition(pFlankEnt->erEntity);
		if (!pEnt)
		{
			freeCombatAdvantageEntity(pFlankEnt);
			eaRemove(&s_combatAdvantageData.eaCombatFlankEntities, FOR_EACH_IDX(-, pFlankEnt));
		}
		else
		{
			S32 iNumHitFrames;

			iNumHitFrames = eafSize(&pFlankEnt->eafUpcomingHitframes);
			for (i = iNumHitFrames - 1; i >= 0; i--)
			{
				if (pFlankEnt->eafUpcomingHitframes[i] < -g_CombatConfig.pCombatAdvantage->fFXHitFrameTimer)
				{
					eafRemove(&pFlankEnt->eafUpcomingHitframes, i);
				}
				else
				{
					pFlankEnt->eafUpcomingHitframes[i] -= fElapsed;
				}
			}
		}
	}
	FOR_EACH_END

	gclCombatAdvantage_UpdateLocalPlayer();
}


// -----------------------------------------------------------------------------------------------------------------------
void gclCombatAdvantage_ReportHitForTarget(Character *pcharTarget, PowerApplication *papp, PowerAnimFX *pafx)
{
	CombatAdvantageEntity* pFlankEnt;
	Entity* pPlayer = entActivePlayerPtr();
	int i, iNumFrames;

	iNumFrames = eaiSize(&pafx->piFramesBeforeHit);

	// todo: if pafx->piFramesBeforeHit is 0, we might want to assume the first frame is the hit

	if (!gclCombatAdvantage_IsEnabled() ||
		!iNumFrames ||
		!pPlayer || !pPlayer->pChar || 
		(pPlayer->pChar != papp->pcharSource) || 
		!pcharTarget || 
		!eaiSize(&pafx->piFramesBeforeHit) )
	{
		return;
	}

	pFlankEnt = gclCombatAdvantage_FindOrAddCombatAdvantageEntity(entGetRef(pcharTarget->pEntParent), false);
	if (pFlankEnt)
	{	//if we're the ones flanking, store hitfx frame delays to flash the ring later
		
		for (i = 0; i < iNumFrames; i++)
		{
			F32 fHitTime = (pafx->piFramesBeforeHit[i]-papp->iFramesBeforeHitAdjust)/PAFX_FPS + pmTimeUntil(papp->uiTimestampAnim);
			eafPush(&pFlankEnt->eafUpcomingHitframes, fHitTime);
		}
	}
}


// -----------------------------------------------------------------------------------------------------------------------
void gclCombatAdvantage_ReportDamageFloat(CombatTrackerNet *pNet, Entity *pTarget, F32 fDelay)
{
	Entity* pPlayer = entActivePlayerPtr();

	if (!gclCombatAdvantage_IsEnabled() || !pNet || !pPlayer || !(pPlayer == pTarget))
		return;

	if (pNet->eFlags & kCombatTrackerFlag_Flank)
	{
		S32 i;

		for (i = 0; i < eafSize(&s_combatAdvantageData.eafUpcomingHostileHitframes); i++)
		{
			if (s_combatAdvantageData.eafUpcomingHostileHitframes[i] < fDelay)
			{
				eafInsert(&s_combatAdvantageData.eafUpcomingHostileHitframes, fDelay, i);
				eaiInsert(&s_combatAdvantageData.eaUpcomingHostileHitframeOwners, pNet->erSource, i);
				return;
			}
		}
		eafPush(&s_combatAdvantageData.eafUpcomingHostileHitframes, fDelay);
		eaiPush(&s_combatAdvantageData.eaUpcomingHostileHitframeOwners, pNet->erSource);
	}
}


// -----------------------------------------------------------------------------------------------------------------------
// returns true if the entity should display the given entity
static bool gclCombatAdvantage_ShouldEntityDisplayUI(Entity *pPlayer, Entity *pEnt, const Vec3 vPlayerPos, const Vec3 vEntPos)
{
	if (!pEnt->pChar || pEnt->pChar->pNearDeath)
		return false;
	if (entCheckFlag(pEnt, COMBAT_ADVANTAGE_ENTITY_IGNORE_FLAGS))
		return false;
	if (!gclEntGetIsFoe(pPlayer, pEnt))
		return false;
	if (pPlayer == pEnt)
		return false;
	
	if (distance3SquaredXZ(vPlayerPos, vEntPos) > SQR(g_CombatConfig.pCombatAdvantage->fClientAdvantageRingsShowDistance))
	{	// they are outside of the range, only show it if there is unconditional advantage going on
		return CombatAdvantage_HasUnconditionalAdvantageOnCharacter(pPlayer->pChar, pEnt->pChar);
	}

	return true;	
}

// -----------------------------------------------------------------------------------------------------------------------
static __forceinline bool gclCombatAdvantage_CanEntityFlankForPlayer(Entity *pPlayer, Entity *pEnt)
{
	if (!pEnt->pChar)
		return false;
	if (pEnt->pChar->pNearDeath)
		return false;
	if (!gclEntGetIsFriend(pPlayer, pEnt))
		return false;

	return true;
}


// -----------------------------------------------------------------------------------------------------------------------
static __forceinline void gclCombatAdvantage_FixFlankAngle(F32 *pfAngleInOut)
{
	if ((*pfAngleInOut) < 0)
	{
		do {
			(*pfAngleInOut) += 360;
		} while((*pfAngleInOut) < 0);
	}
	else if ((*pfAngleInOut) >= 360)
	{
		do {
			(*pfAngleInOut) -= 360;
		} while((*pfAngleInOut) >= 360);
	}
}

// -----------------------------------------------------------------------------------------------------------------------
static bool gclCombatAdvantage_AreArcsOverlapping(F32 fMin1, F32 fMax1, F32 fMin2, F32 fMax2)
{
	return	(fMin1 >= fMin2 && fMin1 <= fMax2) || (fMax1 >= fMin2 && fMax1 <= fMax2);
}

// -----------------------------------------------------------------------------------------------------------------------
static void gclCombatAdvantage_CombineArcs(CombatAdvantageFlankFX *pFlankFX, F32 fMin, F32 fMax, bool bContainsPlayer)
{
	pFlankFX->fArcMin = MIN(fMin, pFlankFX->fArcMin);
	pFlankFX->fArcMax = MAX(fMax, pFlankFX->fArcMax);
	pFlankFX->bContainsPlayer = pFlankFX->bContainsPlayer || bContainsPlayer;
}

// -----------------------------------------------------------------------------------------------------------------------
static void gclCombatAdvantage_StoreFlankFXAngle(CombatAdvantageFlankFX ***peaFlankFX, 
												F32 fMin, F32 fMax, bool bContainsPlayer, 
												SA_PARAM_OP_VALID CombatAdvantageFlankFX *pCombineFlank)
{
	// if we detect that the arc is overlapping with any existing arc, combine them
	// if any are combined, we need to check the new arc vs the other existing ones and combine any that overlap
	
	if (pCombineFlank)
	{
		bContainsPlayer = pCombineFlank->bContainsPlayer;
		fMin = pCombineFlank->fArcMin;
		fMax = pCombineFlank->fArcMax;
	}

	FOR_EACH_IN_EARRAY((*peaFlankFX), CombatAdvantageFlankFX, pFlankFX)
	{
		bool bFound = false;
		if (pCombineFlank == pFlankFX)
			continue;

		if (gclCombatAdvantage_AreArcsOverlapping(fMin, fMax, pFlankFX->fArcMin, pFlankFX->fArcMax))
		{
			gclCombatAdvantage_CombineArcs(pFlankFX, fMin, fMax, bContainsPlayer);
			bFound = true;
		}
		else if (fMin+360.f >= pFlankFX->fArcMin && fMin+360.f <= pFlankFX->fArcMax)
		{	// intersecting, but the FlankFX arc is extending past the 360, but the new arc isn't 
			fMin += 360.f;
			fMax += 360.f;
			gclCombatAdvantage_CombineArcs(pFlankFX, fMin, fMax, bContainsPlayer);
			bFound = true;
		}
		else if (fMax-360.f >= pFlankFX->fArcMin && fMax-360.f <= pFlankFX->fArcMax)
		{	// intersecting, but the new arc is extending past the 360, and the existing one isn't
			pFlankFX->fArcMin += 360.f;
			pFlankFX->fArcMax += 360.f;
			gclCombatAdvantage_CombineArcs(pFlankFX, fMin, fMax, bContainsPlayer);
			bFound = true;
		}

		if (bFound)
		{
			if (pCombineFlank)
			{
				eaFindAndRemoveFast(peaFlankFX, pCombineFlank);
				freeCombatAdvantageFlankFX(pCombineFlank);
			}
			
			// now we have to see if this new arc should be combined with other existing arcs.
			gclCombatAdvantage_StoreFlankFXAngle(peaFlankFX, 0, 0, 0, pFlankFX);
			return;
		}
	}
	FOR_EACH_END

	if (!pCombineFlank)
	{
		CombatAdvantageFlankFX *pFlankFX;

		MP_CREATE(CombatAdvantageFlankFX, 64);
		pFlankFX = MP_ALLOC(CombatAdvantageFlankFX);
		pFlankFX->fArcMax = fMax;
		pFlankFX->fArcMin = fMin;
		pFlankFX->bContainsPlayer = bContainsPlayer;

		eaPush(peaFlankFX, pFlankFX);
	}

}

// -----------------------------------------------------------------------------------------------------------------------
static void gclCombatAdvantage_GetEnts(Entity ***peaProxEnts)
{
	// entGridProximityLookupExEArray isn't working because the entityGrid does not exist on the client
	EntityIterator *iter = entGetIteratorAllTypesAllPartitions(0,COMBAT_ADVANTAGE_ENTITY_IGNORE_FLAGS);
	Entity *pent;
	
	eaClearFast(peaProxEnts);
	while(pent = EntityIteratorGetNext(iter))
	{
		if(pent->pChar)
		{
			eaPush(peaProxEnts,pent);
		}
	}
	EntityIteratorRelease(iter);
}

// -----------------------------------------------------------------------------------------------------------------------
static void gclCombatAdvantage_Process(	CombatAdvantageEntity *pCAEnt, 
										const Vec3 vSourcePos, 
										const Vec3 vTargetPos,
										F32 fPlayerYaw, 
										bool bPlayerInCombatAdvantageRange)
{
	bool bPlayerInArc = false;
	F32 fFlankYawMin, fFlankYawMax;
	Vec3 vProxEntToEnt;

	subVec3(vSourcePos, vTargetPos, vProxEntToEnt);

	fFlankYawMin = fixAngle(getVec3Yaw(vProxEntToEnt));
	fFlankYawMin = DEG(fFlankYawMin);
	gclCombatAdvantage_FixFlankAngle(&fFlankYawMin);

	fFlankYawMin = fFlankYawMin - g_CombatConfig.pCombatAdvantage->fFlankAngleTolerance * 0.5f;
	fFlankYawMax = fFlankYawMin + g_CombatConfig.pCombatAdvantage->fFlankAngleTolerance;
	if (fFlankYawMin >= 360.f)
	{
		fFlankYawMin -= 360.f;
		fFlankYawMax -= 360.f;
	}
	else if (fFlankYawMin < 0.f)
	{
		fFlankYawMin += 360.f;
		fFlankYawMax += 360.f;
	}

	if (bPlayerInCombatAdvantageRange)
	{
		//flank arc might span 0/360 threshold.
		bPlayerInArc = (fPlayerYaw >= fFlankYawMin && fPlayerYaw <= fFlankYawMax) ||
						(fPlayerYaw+360 >= fFlankYawMin && fPlayerYaw+360 <= fFlankYawMax);
	}

	gclCombatAdvantage_StoreFlankFXAngle(&pCAEnt->eaCombatAdvantageFX, fFlankYawMin, fFlankYawMax, bPlayerInArc, NULL);
}

// -----------------------------------------------------------------------------------------------------------------------
void gclCombatAdvantage_EntityTick(F32 fElapsed, Entity *pEnt)
{
	Entity* pPlayer = entActivePlayerPtr();
	Vec3 vPlayerPos, vPlayerToEnt;
	Vec3 vEntPos;
	
	if ( !gclCombatAdvantage_IsEnabled() || !pPlayer || !pPlayer->pChar)
		return;

	entGetPos(pPlayer, vPlayerPos);
	entGetPos(pEnt, vEntPos);

	subVec3(vEntPos, vPlayerPos, vPlayerToEnt);

	if (gclCombatAdvantage_ShouldEntityDisplayUI(pPlayer, pEnt, vPlayerPos, vEntPos))
	{	// get all nearby entities and see who is flanking me
		int iPartitionIdx = entGetPartitionIdx(pEnt);
		static Entity **s_eaProxEnts = NULL;
		bool bPlayerInCombatAdvantageRange;
		F32 fPlayerYaw;
		CombatAdvantageEntity* pFlankEnt = gclCombatAdvantage_FindOrAddCombatAdvantageEntity(entGetRef(pEnt), true);
		
		eaClearEx(&pFlankEnt->eaCombatAdvantageFX, freeCombatAdvantageFlankFX);
		
		pFlankEnt->bHasUnconditionalAdvantage = CombatAdvantage_HasUnconditionalAdvantageOnCharacter(pPlayer->pChar, pEnt->pChar);

		if (!pFlankEnt->bHasUnconditionalAdvantage)
		{
			gclCombatAdvantage_GetEnts(&s_eaProxEnts);
				
			// get the player's relative flanking
			fPlayerYaw = getVec3Yaw(vPlayerToEnt) + PI;
			fPlayerYaw = DEG(fPlayerYaw);
			gclCombatAdvantage_FixFlankAngle(&fPlayerYaw);
			
			bPlayerInCombatAdvantageRange = entGetDistance(pPlayer, NULL, pEnt, NULL, NULL) <= g_CombatConfig.pCombatAdvantage->fFlankingDistance;

			FOR_EACH_IN_EARRAY(s_eaProxEnts, Entity, pProxEnt)
			{
				Vec3 vProxEntPos;
				bool bPlayerInArc = false;

				if (pProxEnt == pEnt || pProxEnt == pPlayer)
					continue;
				if (!gclCombatAdvantage_CanEntityFlankForPlayer(pPlayer, pProxEnt))
					continue;
				if (entGetDistance(pProxEnt, NULL, pEnt, NULL, NULL) > g_CombatConfig.pCombatAdvantage->fFlankingDistance)
					continue;

				entGetPos(pProxEnt, vProxEntPos);

				gclCombatAdvantage_Process(pFlankEnt, vEntPos, vProxEntPos, fPlayerYaw, bPlayerInCombatAdvantageRange);
			}
			FOR_EACH_END
		}

		gclCombatAdvantage_UpdateFlankFX(pEnt, pFlankEnt, false);
	}
	else
	{
		CombatAdvantageEntity* pFlankEnt = gclCombatAdvantage_FindOrAddCombatAdvantageEntity(entGetRef(pEnt), false);
		if (pFlankEnt)
		{
			eaFindAndRemove(&s_combatAdvantageData.eaCombatFlankEntities, pFlankEnt);
			freeCombatAdvantageEntity(pFlankEnt);
		}
	}

}


// -----------------------------------------------------------------------------------------------------------------------
// returns true if any hitframes were consumed 
static bool gclCombatAdvantage_InHitFrame(CombatAdvantageEntity* pFlankEnt)
{
	S32 numHitFrames = eafSize(&pFlankEnt->eafUpcomingHitframes);
	if (numHitFrames > 0)
	{
		if (pFlankEnt->eafUpcomingHitframes[0] <= 0)
		{
			return true;
		}
	}

	return false;
}

// -----------------------------------------------------------------------------------------------------------------------
static void gclCombatAdvantage_FixupFlankFX(CombatAdvantageEntity* pFlankEnt)
{
	F32 fMin = FLT_MAX;

	pFlankEnt->fBaseYaw = 0.f;

	if (!eaSize(&pFlankEnt->eaCombatAdvantageFX))
		return;
	
	// go through the FX and find the min, then make all the FX relative to that one
	FOR_EACH_IN_EARRAY(pFlankEnt->eaCombatAdvantageFX, CombatAdvantageFlankFX, pFlankFX)
	{
		// we need to do a transformation to get the gradient texture to be oriented correctly
		pFlankFX->fArcMin = -pFlankFX->fArcMin + 270.f;
		pFlankFX->fArcMax = -pFlankFX->fArcMax + 270.f;
		// swap the min/max
		{
			F32 fSwap = pFlankFX->fArcMin;
			pFlankFX->fArcMin = pFlankFX->fArcMax;
			pFlankFX->fArcMax = fSwap;
		}
				

		if (pFlankFX->fArcMin > 360.f)
		{
			pFlankFX->fArcMin -= 360.f;
			pFlankFX->fArcMax -= 360.f;
		}
		else if (pFlankFX->fArcMin < 0.f)
		{
			pFlankFX->fArcMin += 360.f;
			pFlankFX->fArcMax += 360.f;
		}


		if (pFlankFX->fArcMin < fMin)
		{
			fMin = pFlankFX->fArcMin;
		}
	}
	FOR_EACH_END

	pFlankEnt->fBaseYaw = fMin;
	
	FOR_EACH_IN_EARRAY(pFlankEnt->eaCombatAdvantageFX, CombatAdvantageFlankFX, pFlankFX)
	{
		pFlankFX->fArcMin -= pFlankEnt->fBaseYaw + 2;
		pFlankFX->fArcMax -= pFlankEnt->fBaseYaw;
	}
	FOR_EACH_END
	
}

// -----------------------------------------------------------------------------------------------------------------------
static void gclCombatAdvantage_UpdateFlankFX(Entity *pEnt, CombatAdvantageEntity* pFlankEnt, bool bForceHitFrame)
{
	Entity* pPlayer = entActivePlayerPtr();

	if (!pPlayer)
		return;

	if (!pFlankEnt->bHasUnconditionalAdvantage && eaSize(&pFlankEnt->eaCombatAdvantageFX) == 0)
	{
		// no flanking going on, kill the FX if it is alive
		if (pFlankEnt->hCombatAdvantageFX)
		{
			dtFxKill(pFlankEnt->hCombatAdvantageFX);
			pFlankEnt->hCombatAdvantageFX = 0;
		}
		return;
	}
	
	if (!pFlankEnt->hCombatAdvantageFX)
	{
		pFlankEnt->hCombatAdvantageFX = dtAddFx(pEnt->dyn.guidFxMan, "FX_Flanking_Ring", 
												NULL, 0, 0, 
												0.0f, 0, NULL, eDynFxSource_HardCoded, NULL, NULL);
	}

	// create the different intervals

	if (pFlankEnt->bHasUnconditionalAdvantage)
	{
		Vec4 vec = {0};
		
		if (bForceHitFrame || gclCombatAdvantage_InHitFrame(pFlankEnt))
		{
			vec[2] = g_CombatConfig.pCombatAdvantage->fFXHitFrameHue;
			vec[3] = g_CombatConfig.pCombatAdvantage->fFXHitFrameSaturation;
		}
		else
		{
			vec[2] = g_CombatConfig.pCombatAdvantage->fFXInArcHue;
			vec[3] = g_CombatConfig.pCombatAdvantage->fFXInArcSaturation;
		}

		vec[0] = 0.f;
		vec[1] = 255.f;
		
		dtFxSetColor(pFlankEnt->hCombatAdvantageFX, 1, vec);
		dtFxSetColor(pFlankEnt->hCombatAdvantageFX, 2, zerovec4);
		dtFxSetColor(pFlankEnt->hCombatAdvantageFX, 3, zerovec4);
	}
	else
	{
		S32 i, size;
		
		gclCombatAdvantage_FixupFlankFX(pFlankEnt);
		
		{
			DynFx *pFX;
			DynNode *pNode;

			pFX = dynFxFromGuid(pFlankEnt->hCombatAdvantageFX);
			pNode = pFX ? dynFxGetNode(pFX) : NULL;
			if (pNode)
			{
				Quat qRot;
				Vec3 pyr = {0};

				pyr[0] = -RAD(90.f);
				pyr[1] = -RAD(pFlankEnt->fBaseYaw);
				pyr[2] = 0.f;

				PYRToQuat(pyr, qRot);

				dynNodeSetRot(pNode, qRot);
			}
		}

		// multiple intervals, we can only process 3
		size = eaSize(&pFlankEnt->eaCombatAdvantageFX);
		MIN1(size, MAX_INTERVALS);
		for (i = 0; i < size; ++i)
		{
			Vec4 vec = {0};
			CombatAdvantageFlankFX *pFlankFX = pFlankEnt->eaCombatAdvantageFX[i];
						
			vec[0] = pFlankFX->fArcMin/360.f * 255.f;
			vec[1] = pFlankFX->fArcMax/360.f * 255.f;
			vec[2] = 20.f;

			if (bForceHitFrame || (pFlankFX->bContainsPlayer && gclCombatAdvantage_InHitFrame(pFlankEnt)))
			{
				vec[2] = g_CombatConfig.pCombatAdvantage->fFXHitFrameHue;
				vec[3] = g_CombatConfig.pCombatAdvantage->fFXHitFrameSaturation;
			}
			else if (pFlankFX->bContainsPlayer)
			{
				vec[2] = g_CombatConfig.pCombatAdvantage->fFXInArcHue;
				vec[3] = g_CombatConfig.pCombatAdvantage->fFXInArcSaturation;
			}

			
			dtFxSetColor(pFlankEnt->hCombatAdvantageFX, (i + 1), vec);

		}

	
		// clear out the other intervals on the FX
		while (i++ < MAX_INTERVALS)
			dtFxSetColor(pFlankEnt->hCombatAdvantageFX, i, zerovec4);
	}
}


// -----------------------------------------------------------------------------------------------------------------------
static void gclCombatAdvantage_UpdateLocalPlayer()
{
	Entity* pPlayer = entActivePlayerPtr();
	S32 numHitFrames = eafSize(&s_combatAdvantageData.eafUpcomingHostileHitframes);

	// 
	eaClearEx(&s_combatAdvantageData.caPlayer.eaCombatAdvantageFX, freeCombatAdvantageFlankFX);

	if (pPlayer && numHitFrames > 0)
	{
		Vec3 vPlayerPos;
		S32 i = 0;
				
		entGetPos(pPlayer, vPlayerPos);

		while (i < numHitFrames && s_combatAdvantageData.eafUpcomingHostileHitframes[i] <= 0)
		{
			Vec3 vSourcePos;
			Entity* pSource = entFromEntityRefAnyPartition(s_combatAdvantageData.eaUpcomingHostileHitframeOwners[i]);
			if (pSource)
			{
				entGetPos(pSource, vSourcePos);
				gclCombatAdvantage_Process(&s_combatAdvantageData.caPlayer, vSourcePos, vPlayerPos, 0, false);
			}
			
			i++;
		}
	}
	
	gclCombatAdvantage_UpdateFlankFX(pPlayer, &s_combatAdvantageData.caPlayer, true);
}

#include "gclCombatAdvantage_c_ast.c"