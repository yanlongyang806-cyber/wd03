/***************************************************************************
*     Copyright (c) 2006-2008, Cryptic Studios
*     All Rights Reserved
*     Confidential Property of Cryptic Studios
***************************************************************************/
#include "Character_combat.h"

#include "Capsule.h"
#include "encounter_common.h"
#include "EntityIterator.h"
#include "EntityLib.h"
#include "Expression.h"
#include "logging.h"
#include "MemoryPool.h"
#include "mapstate_common.h"
#include "rand.h"
#include "Team.h"

#include "AutoTransDefs.h" // Hopefully temporary include for Team.h team macro

#include "WorldLib.h"
#include "WorldGrid.h"
#include "WorldColl.h"
#include "wlVolumes.h"
#include "cmdparse.h"
#include "PowerSlots.h"
#include "TriCube/vec.h"

#if GAMESERVER || GAMECLIENT
	#include "EntityMovementManager.h"
	#include "EntityMovementTactical.h"
	#include "PowersMovement.h"
	#include "EntityGrid.h"
	#include "CombatPowerStateSwitching.h"
#endif

#if GAMESERVER
	#include "CombatGlobal.h"
	#include "aiLib.h"
	#include "aiStruct.h"
	#include "gslInteraction.h"
	#include "gslInteractionManager.h"
    #include "gslLogSettings.h"
	#include "gslMechanics.h"
	#include "gslSendToClient.h"
	#include "gslPowerTransactions.h"
	#include "gslEncounterLog.h"
	#include "gslTeamUp.h"
	#include "mechanics_common.h"
	#include "TeamUpCommon.h"
#endif

#if GAMECLIENT
	#include "gclControlScheme.h"
	#include "gclPlayerControl.h"
	#include "Combat/ClientTargeting.h"
	#include "gclCommandParse.h"

	// All this crap to get one function
	#include "EditLib.h"
	#include "gclEntity.h"

	#include "gclCamera.h"
	#include "GfxCamera.h"
	#include "CostumeCommonGenerate.h"
#endif

#include "AttribCurve.h"
#include "AttribMod.h"
#include "AttribMod_h_ast.h"
#include "Character.h"
#include "CharacterAttribs.h"
#include "CharacterAttribsMinimal_h_ast.h"
#include "Character_target.h"
#include "Character_tick.h"
#include "Combat_DD.h"
#include "CombatConfig.h"
#include "CombatDebug.h"
#include "CombatEval.h"
#include "CombatReactivePower.h"
#include "DamageTracker.h"
#include "interaction_common.h"
#include "ItemArt.h"
#include "Player.h"
#include "PowerActivation.h"
#include "PowerAnimFX.h"
#include "PowerApplication.h"
#include "PowersAEDebug.h"
#include "AutoGen/PowerApplication_h_ast.h"
#include "PowerSubtarget.h"
#include "AutoGen/PowerSubtarget_h_ast.h"
#include "PowerHelpers.h"

#include "Autogen/GameServerLib_autogen_ServerCmdWrappers.h"
#include "AutoGen/GameClientLib_autogen_ClientCmdWrappers.h"

AUTO_RUN_ANON(memBudgetAddMapping(__FILE__, BUDGET_GameSystems););

int CharacterQueuedPowerWillBecomeCurrentSoon(Character *pchar);

MP_DEFINE(CombatTarget);
MP_DEFINE(PAPowerAnimFX);

// Bitmask for destructible and throwable objects for wlQueries
S32 g_iDestructibleThrowableMask = 0;
S32 g_bCombatPredictionDisabled = false;

AUTO_STARTUP(CharacterCombat) ASTRT_DEPS(WorldLibZone);
void CharacterCombatStartup(void)
{
	S32 iMaskDes, iMaskThrow;

	iMaskDes = wlInteractionClassNameToBitMask("Destructible");
	iMaskThrow = wlInteractionClassNameToBitMask("Throwable");
	g_iDestructibleThrowableMask = iMaskDes | iMaskThrow;

	MP_CREATE(CombatTarget,25);
	MP_CREATE(PAPowerAnimFX,4);
}

extern ControlScheme g_CurrentScheme;
/************************************************************
/***** Temporary copy-paste-cleanup of mmDrawCover code *****
 ************************************************************/

static int s_iCoverEnabled = 0;
AUTO_CMD_INT(s_iCoverEnabled,CombatCoverEnabled) ACMD_CATEGORY(Debug) ACMD_SERVERONLY;

static int s_iPowersAEClientViewEnabled = 1;
AUTO_CMD_INT(s_iPowersAEClientViewEnabled,PowersAEClientView) ACMD_SERVERONLY;


typedef struct CoverData
{
	Vec3 vecStart;
	Vec3 vecDir;
	F32 fRadius;
	F32 fLength;
	S32 iQuant;

	int *pRegionsHit;
	int iRegions;
	U32 bHit : 1;
} CoverData;

static void CoverHandleTriangles(void* coverData, Vec3 (*tris)[3], U32 triCount)
{
	int i;

	CoverData *data = (CoverData*)coverData;
	F32 fRadSqr = SQR(data->fRadius);
	F32 fLength = data->fLength;

	Mat3 mDir, mDirInv;

	int iQuant = 1 + 2 * data->iQuant;
	if(!data->iRegions)
	{
		data->iRegions = iQuant*iQuant;
		data->pRegionsHit = calloc(data->iRegions, sizeof(int));
	}

	// Create a mat in the direction of cover
	orientMat3(mDir,data->vecDir);
	invertMat3Copy(mDir,mDirInv);

	for (; triCount; --triCount, ++tris)
	{
		int v;
		for (v=0; v<3; v++)
		{
			Vec3 vecTo, vecProj, vecToTrans;
			F32 dotVertex;
			F32 fDistSqr;

			subVec3(tris[0][v],data->vecStart,vecTo);
			dotVertex = dotVec3(vecTo,data->vecDir);

			if(dotVertex<0)
			{
				// Vertex is behind the cover root
				continue;
			}

			scaleAddVec3(data->vecDir,-dotVertex,tris[0][v],vecProj);
			subVec3(vecProj,data->vecStart,vecTo);
			fDistSqr = lengthVec3Squared(vecTo);

			if(fDistSqr > fRadSqr)
			{
				// Vertex is outside the cover radius
				continue;
			}

			mulVecMat3(vecTo, mDirInv, vecToTrans);

			// Convert into [-1 .. 1] radius units
			vecToTrans[0] /= data->fRadius;
			vecToTrans[1] /= data->fRadius;

			// Convert into [0 .. 2] radius units
			vecToTrans[0] += 1.f;
			vecToTrans[1] += 1.f;

			// Convert into [0 .. iQuant] units
			vecToTrans[0] *= iQuant/2.f;
			vecToTrans[1] *= iQuant/2.f;

			// Floor
			vecToTrans[0] = floor(vecToTrans[0]);
			vecToTrans[1] = floor(vecToTrans[1]);

			i = vecToTrans[0] + iQuant * vecToTrans[1];
			data->pRegionsHit[i]++;

			data->bHit = true;
		}
	}
}

static F32 CoverCalc(Entity *eSource, Entity *eTarget)
{
#if !GAMESERVER && !GAMECLIENT
	assert(0);
#else
	F32 fCover = 0;

	if(eSource && eTarget && eSource->mm.movement && eTarget->mm.movement)
	{
		Vec3 vecSourcePos, vecTargetPos, vecSourceCoverPos, vecTargetCoverPos;
		Vec3 vecDir;
		Vec3 vecCapEnd;
		Quat rotSource, rotTarget;
		const Capsule*const* capsulesSource;
		const Capsule*const* capsulesTarget;
		int iHitSource = false, iHitTarget = false;
		CoverData coverTarget = {0};

		PERFINFO_AUTO_START_FUNC();

#define COVER_RADIUS_DEFAULT 2.7
#define COVER_LENGTH_DEFAULT 6


		// Get location/rotation of source
		entGetPos(eSource,vecSourcePos);
		entGetRot(eSource,rotSource);
		if(mmGetCapsules(eSource->mm.movement,&capsulesSource))
		{
			// Cover root and radius based on capsule
			CapsuleMidlinePoint(capsulesSource[0],vecSourcePos,rotSource,0.6f,vecSourceCoverPos);
		}
		else
		{
			copyVec3(vecSourcePos,vecSourceCoverPos);
		}

		// Get location/rotation of target
		entGetPos(eTarget,vecTargetPos);
		entGetRot(eTarget,rotTarget);
		if(mmGetCapsules(eTarget->mm.movement,&capsulesTarget))
		{
			// Cover root and radius based on capsule
			CapsuleMidlinePoint(capsulesTarget[0],vecTargetPos,rotTarget,0.6f,vecTargetCoverPos);
			coverTarget.fRadius = capsulesTarget[0]->fRadius + capsulesTarget[0]->fLength * .4;
		}
		else
		{
			copyVec3(vecTargetPos,vecTargetCoverPos);
			coverTarget.fRadius = COVER_RADIUS_DEFAULT;
		}

		// Determine direction between the two
		subVec3(vecSourceCoverPos,vecTargetCoverPos,vecDir);
		normalVec3(vecDir);


		// Fill target cover struct
		coverTarget.fLength = COVER_LENGTH_DEFAULT;
		coverTarget.iQuant = s_iCoverEnabled - 1;

		// Root cover position is pushed forward equal to half the radius
		scaleAddVec3(vecDir,coverTarget.fRadius/2.f,vecTargetCoverPos,vecTargetCoverPos);
		scaleAddVec3(vecDir,COVER_LENGTH_DEFAULT,vecTargetCoverPos,vecCapEnd);

		copyVec3(vecTargetCoverPos,coverTarget.vecStart);
		copyVec3(vecDir,coverTarget.vecDir);

		// Do the query
		wcQueryTrianglesInCapsule(worldGetActiveColl(entGetPartitionIdx(eSource)),
			WC_QUERY_BITS_TARGETING,
			vecTargetCoverPos,
			vecCapEnd,
			coverTarget.fRadius,
			CoverHandleTriangles,
			&coverTarget);

		if(coverTarget.iRegions)
		{
			int i,c=0;
			for(i=0; i<coverTarget.iRegions; i++)
			{
				if(coverTarget.pRegionsHit[i])
					c++;
			}
			free(coverTarget.pRegionsHit);
			fCover = (F32)c/(F32)coverTarget.iRegions;
		}

		PERFINFO_AUTO_STOP();
	}

	return fCover;
#endif
}
static F32 EvalChance(SA_PARAM_NN_VALID ChanceConfig *pChance, F32 fDiff, SA_PARAM_OP_VALID F32 *pfOverflow)
{
	F32 fChance = pChance->fChanceNeutral;

	if(fDiff > 0)
	{
		if(pChance->bSimple)
		{
			fChance += fDiff;
		}
		else
		{
			F32 fInverse = fChance / (1.f + fDiff);
			fChance = (fChance * 2.f) - fInverse;
		}

		if(pChance->fChanceMax && fChance > pChance->fChanceMax)
		{
			if(pChance->bOverflow && pfOverflow)
				*pfOverflow = fChance - pChance->fChanceMax;
			fChance = pChance->fChanceMax;
		}
	}
	else if(fDiff < 0)
	{
		if(pChance->bSimple)
		{
			fChance += fDiff;
		}
		else
		{
			fChance = fChance / (1.f - fDiff);
		}
		MAX1(fChance,pChance->fChanceMin);
	}

	return fChance;
}


static F32 EvalHitChance(int iPartitionIdx,
							SA_PARAM_OP_VALID Character *pcharSource,
							SA_PARAM_OP_VALID Character *pcharTarget,
							SA_PARAM_OP_VALID Power *ppow,
							SA_PARAM_OP_VALID PowerDef *ppowDef,
							SA_PARAM_OP_VALID Power **ppEnhancements,
							bool bEvalHitChanceWithoutPower,
							SA_PARAM_OP_VALID F32 *pfOverflow)
{
#if !GAMESERVER && !GAMECLIENT
	assert(0);
#else
	F32 fChance = 1.f;

	if (!ppowDef && ppow)
		ppowDef = GET_REF(ppow->hDef);

	if(pfOverflow)
		*pfOverflow = 0;

	if(ppowDef && ppowDef->bHitChanceIgnore)
		return fChance;

	if(s_iCoverEnabled)
	{
		F32 fCover = CoverCalc(pcharSource->pEntParent,pcharTarget->pEntParent);
		fChance = 1-fCover;
	}
	else if(g_CombatConfig.pHitChance && (ppow || (ppowDef && bEvalHitChanceWithoutPower)) && pcharSource && pcharTarget && character_TargetIsFoe(iPartitionIdx,pcharSource,pcharTarget))
	{
		EntityRef erTarget = entGetRef(pcharTarget->pEntParent);
		F32 fPos=0, fNeg=0, fDiff;
		S32 bDefenseless = (g_CombatConfig.pHitChance->bDefenseless
							&& (character_IsHeld(pcharTarget) > 0
								|| pcharTarget->pNearDeath
								|| pmKnockIsActive(pcharTarget->pEntParent)));

		if(g_CombatConfig.pHitChance->eAttribChancePos)
			fPos = character_PowerBasicAttribEx(iPartitionIdx,pcharSource,ppow,ppowDef,g_CombatConfig.pHitChance->eAttribChancePos,ppEnhancements,erTarget);

		if(g_CombatConfig.pHitChance->eAttribChanceNeg)
			fNeg = *F32PTR_OF_ATTRIB(pcharTarget->pattrBasic,g_CombatConfig.pHitChance->eAttribChanceNeg);

		// If the target is defenseless, it's not allowed any positive result from its attribute
		if(bDefenseless)
			MIN1(fNeg,0);

		fDiff = fPos - fNeg;

		fChance = EvalChance(g_CombatConfig.pHitChance, fDiff, pfOverflow);

		// If the target is defenseless and the chance was less than 1, set it to one and clear the overflow
		if(bDefenseless && fChance < 1)
		{
			fChance = 1;
			if(pfOverflow)
				*pfOverflow = 0;
		}
	}

	return fChance;
#endif
}

static void EvalAvoidChance(int iPartitionIdx, SA_PARAM_NN_VALID ChanceConfig *pChance, SA_PARAM_NN_VALID Character *pcharTarget, SA_PARAM_NN_VALID PowerApplication *papp)
{
#if !GAMESERVER && !GAMECLIENT
	assert(0);
#else
	F32 fChance = 0, fMag = 0;
	Character *pcharSource = papp->pcharSource;
	S32 bCategory;
	S32 bDefenseless = (pChance->bDefenseless
						&& (character_IsHeld(pcharTarget) > 0
							|| pcharTarget->pNearDeath
							|| pmKnockIsActive(pcharTarget->pEntParent)));

	// If the target is defenseless, there's 0 chance to use avoidance mechanics
	if(!bDefenseless)
	{
		bCategory = !pChance->iPowerCategory || -1!=eaiFind(&papp->pdef->piCategories,pChance->iPowerCategory);

		if(bCategory && pcharSource && character_TargetIsFoe(iPartitionIdx,pcharSource,pcharTarget))
		{
			EntityRef erTarget = entGetRef(pcharTarget->pEntParent);
			F32 fPos=0, fNeg=0, fDiff;

			if(pChance->eAttribChancePos)
				fPos = *F32PTR_OF_ATTRIB(pcharTarget->pattrBasic,pChance->eAttribChancePos);

			if(pChance->eAttribChanceNeg)
				fNeg = *F32PTR_OF_ATTRIB(pcharSource->pattrBasic,pChance->eAttribChanceNeg);

			fDiff = fPos - fNeg;

			fChance = EvalChance(pChance, fDiff, NULL);

			if(pChance->eAttribMag)
				fMag = *F32PTR_OF_ATTRIB(pcharTarget->pattrBasic,pChance->eAttribMag);
		}
	}

	papp->avoidance.fThreshold = fChance;
	papp->avoidance.fSeverity = fMag;

	if(fChance && fMag)
	{
		F32 fRoll = randomPositiveF32();
		if(fRoll < fChance)
		{
			papp->avoidance.bSuccess = true;
		}
		papp->avoidance.fRoll = fRoll;
	}
#endif
}

static void EvalCritChance(int iPartitionIdx, Character *pchar, Power *ppow, PowerApplication *papp, Character *pcharTarget, F32 fHitChanceOverflow)
{
	static int s_iNoDefaultCrit = 0;
	PowerDef *pdef = GET_REF(ppow->hDef);
	EntityRef erTarget = entGetRef(pcharTarget->pEntParent);
	AttribType eCritChanceAttrib = kAttribType_CritChance;
	AttribType eCritSeverityAttrib = kAttribType_CritSeverity;
	
	if(!s_iNoDefaultCrit)
	{
		s_iNoDefaultCrit = StaticDefineIntGetInt(PowerTagsEnum,"NoDefaultCrit");
	}

	// TODO(JW): Hack: Passives and any Power tagged with "NoDefaultCrit" has a fThreshold of -1, which means
	//  it can't crit.
	if(pdef
		&& (pdef->eType==kPowerType_Passive 
			|| (-1!=s_iNoDefaultCrit
				&& -1!=eaiFind(&pdef->tags.piTags,s_iNoDefaultCrit))))
	{
		papp->critical.fThreshold = -1;
	}
	else
	{
		if (pdef->eCriticalChanceAttrib != -1 && pdef->eCriticalSeverityAttrib != -1)
		{
			eCritChanceAttrib = pdef->eCriticalChanceAttrib;
			eCritSeverityAttrib = pdef->eCriticalSeverityAttrib;
		}

		papp->critical.fThreshold = character_PowerBasicAttribEx(iPartitionIdx,pchar,ppow,NULL,eCritChanceAttrib,papp->pppowEnhancements,erTarget);
		if(g_CombatConfig.pHitChance)
			papp->critical.fThreshold += g_CombatConfig.pHitChance->fCritChanceOverflowMulti*fHitChanceOverflow;
	}

	papp->critical.fRoll = randomPositiveF32();
	papp->critical.bSuccess = papp->critical.fRoll < papp->critical.fThreshold;
	papp->critical.fSeverity = character_PowerBasicAttribEx(iPartitionIdx,pchar,ppow,NULL,eCritSeverityAttrib,papp->pppowEnhancements,erTarget);
	if(g_CombatConfig.pHitChance)
		papp->critical.fSeverity += g_CombatConfig.pHitChance->fCritSeverityOverflowMulti*fHitChanceOverflow;
}

static F32 ApplyPowerDodgeFactor(PowerApplication *papp)
{
	F32 fDodgeFactor = 1.0f;

	if(papp->pact && papp->pdef && papp->pdef->eType!=kPowerType_Passive && papp->pdef->eType!=kPowerType_Toggle)
	{
		F32 fPowerExecTime = 0.0f;
		if(papp->pdef->eType==kPowerType_Maintained)
		{
			fPowerExecTime += (papp->pact->uiPeriod) ? (papp->pdef->fTimeActivatePeriod) : powerapp_GetTotalTime(papp);
		}
		else
		{
			fPowerExecTime += powerapp_GetTotalTime(papp);
		}
		
		//TODO(BH): Add in the clamping max factor if they like the dodge based on your anticipation of a power.
		//fDodgeFactor = 
		// CLAMP( (fPowerExecTime - g_CombatConfig.fNormalizedDodgeVal) / g_CombatConfig.fNormalizedDodgeVal,
		// -1 * g_CombatConfig.fNormalizedDodgeMaxFactor, 
		// g_CombatConfig.fNormalizedDodgeMaxFactor);
		fDodgeFactor += (fPowerExecTime - g_CombatConfig.fNormalizedDodgeVal) / g_CombatConfig.fNormalizedDodgeVal;
	}

	return(fDodgeFactor);
}

static void SplitCurvedDodgeAndAvoid(F32 *pfInOutDodge, F32 *pfInOutAvoid, F32 fMitCurved)
{
	F32 fAvoidInverse = (1.f - 1.f / (1.f + *pfInOutAvoid));
	F32 fMit = (*pfInOutDodge) * fAvoidInverse;

	if(fMit)
	{
		F32 fLoss = fMitCurved / fMit;
		fLoss = sqrt(fLoss);

		*pfInOutDodge *= fLoss;

		// If after curving we'd need to dodge more than 100% of the time, set to 100%
		//  and adjust the avoidance inverse appropriately
		if(*pfInOutDodge > 1.f)
		{
			*pfInOutDodge = 1.f;
			fAvoidInverse = fMitCurved;
		}
		else
		{
			fAvoidInverse *= fLoss;
		}

		if(fAvoidInverse >= 1.f)
		{
			// We'd have to avoid as much as 100% of the result, just set it really high
			*pfInOutAvoid = 1000.f;
		}
		else if(fAvoidInverse <= 0.f)
		{
			// We'd have to not change or increase the result, just set it to 0
			*pfInOutAvoid = 0;
		}
		else
		{
			*pfInOutAvoid = (1.f / (1.f - fAvoidInverse)) - 1.f;
		}
	}
}

static void CurveDodgeAndAvoid(Character *pchar, F32 *pfInOutDodge, F32 *pfInOutAvoid)
{
	F32 fAvoidInverse = (1.f - 1.f / (1.f + *pfInOutAvoid));
	F32 fMit = (*pfInOutDodge) * fAvoidInverse;
	F32 fMitCurved;
	S32 bCurved = false;

	// Apply special curve to combined mit
	fMitCurved = character_AttribCurve(pchar,kAttribType_CurveDodgeAndAvoidance,kAttribAspect_BasicAbs,fMit,&bCurved);

	if(bCurved)
		SplitCurvedDodgeAndAvoid(pfInOutDodge,pfInOutAvoid,fMitCurved);
}

static void EvalDodgeChance(SA_PARAM_NN_VALID Character *pchar, SA_PARAM_NN_VALID PowerApplication *papp)
{
	EntityRef erSource;
	int iPartitionIdx;
	F32 fDodge, fAvoid;

	papp->avoidance.bSuccess = false;
	papp->avoidance.fRoll = 0;

	// Can't dodge yourself
	if(pchar==papp->pcharSource)
		return;

	erSource = papp->pcharSource ? entGetRef(papp->pcharSource->pEntParent) : 0;
	iPartitionIdx = entGetPartitionIdx(pchar->pEntParent);

	fDodge = character_PowerBasicAttribEx(iPartitionIdx,pchar,NULL,papp->pdef,kAttribType_Dodge,NULL,erSource);
	fAvoid = character_PowerBasicAttribEx(iPartitionIdx,pchar,NULL,papp->pdef,kAttribType_Avoidance,NULL,erSource);

	if(g_CombatConfig.bUseNormalizedDodge)
		fDodge *= ApplyPowerDodgeFactor(papp);

	CurveDodgeAndAvoid(pchar,&fDodge,&fAvoid);

	papp->avoidance.fThreshold = fDodge;
	papp->avoidance.fSeverity = fAvoid;

	if(fDodge && fAvoid)
	{
		F32 fRoll = randomPositiveF32();
		if(fRoll < fDodge)
		{
			papp->avoidance.bSuccess = true;
		}
		papp->avoidance.fRoll = fRoll;
	}
}

void sortEntRefList(EntityRef **ppList)
{
	int i,c;

	for(i=0;i<ea32Size(ppList);i++)
	{
		for(c=i+1;c<ea32Size(ppList);c++)
		{
			if((*ppList)[i] > (*ppList)[c])
				ea32Swap(ppList,i,c);
		}
	}
}

EntityRef character_serverCalulateConfuse(Character *p,EntityRef **perConfuseTargetListOut, U32 *puiConfuseSeedOut)
{
#if !GAMESERVER && !GAMECLIENT
	assert(0);
#else
	// Check to see if the player or a player's pet is confused
	// AI entities handle confusion themselves
	if(p->pattrBasic->fConfuse > 0 && 
		(entIsPlayer(p->pEntParent) || entIsPlayer(entGetOwner(p->pEntParent))))
	{
		F32 fRand;
		U32 uiConfuseSeed = p->uiConfuseSeed;

		fRand = randomF32Seeded(puiConfuseSeedOut,RandType_BLORN);

		if(fRand < p->pattrBasic->fConfuse)
		{
			//Get the list of near by entities
			static Entity **ppEnts = NULL;
			Vec3 vPos;
			int i,iRand;
			U32 uiSeedOffset=0;

			entGetPos(p->pEntParent,vPos);

			entGridProximityLookupExEArray(entGetPartitionIdx(p->pEntParent),vPos,&ppEnts,50,0,ENTITYFLAG_UNTARGETABLE|ENTITYFLAG_IGNORE|ENTITYFLAG_DONOTDRAW,p->pEntParent);

			for(i=eaSize(&ppEnts)-1;i>=0;i--)
			{
				if(ppEnts[i] == p->pEntParent)
					continue;
				if(entGetRef(ppEnts[i]) == p->currentTargetRef)
					continue;
				//No destructible objects allowed
				if(IS_HANDLE_ACTIVE(ppEnts[i]->hCreatorNode))
					continue;

				ea32Push(perConfuseTargetListOut,entGetRef(ppEnts[i]));
				uiSeedOffset += entGetRef(ppEnts[i]);
			}
			sortEntRefList(perConfuseTargetListOut);

			if(ea32Size(perConfuseTargetListOut) == 0)
				return 0;

			*puiConfuseSeedOut += uiSeedOffset;
			iRand = randomIntRangeSeeded(puiConfuseSeedOut,RandType_BLORN,0,ea32Size(perConfuseTargetListOut)-1);
			return (*perConfuseTargetListOut)[iRand];			
		}
	}

	return 0;
#endif
}

// Places the character in combat for the given period of time
// bCombatEventActIn - if true will send a combat event kCombatEvent_CombatModeActIn, otherwise kCombatEvent_CombatModeActOut
void character_SetCombatExitTime(	Character *pChar, 
									F32 fTimeInCombat, 
									bool bSetActive, 
									bool bCombatEventActIn,
									Entity *pSourceEnt,
									PowerDef *pSourcePowerDef)
{
#if !GAMESERVER && !GAMECLIENT
	assert(0);
#else
	if (fTimeInCombat >= 0.f)
	{
		U32 uiTime = pmTimestamp(fTimeInCombat);

#ifdef GAMESERVER
		if (pChar->uiTimeCombatExit == 0 && g_CombatConfig.tactical.bTacticalDisableDuringCombat)
		{
			U32 uiDisableTacticalTimestamp = pmTimestamp(0.5);
			// Disable tactical movement in combat
			mrTacticalNotifyPowersStart(pChar->pEntParent->mm.mrTactical, 
				TACTICAL_COMBATDISABLE_UID, 
				combatconfig_GetTacticalDisableFlagsForCombat(), 
				uiDisableTacticalTimestamp);
			
			if (pChar->pEntParent->myEntityType == GLOBALTYPE_ENTITYPLAYER)
			{
				ClientCmd_PowersPredictDisableTacticalInCombat(pChar->pEntParent, uiDisableTacticalTimestamp);
			}			
		}
		
		if (bSetActive)
		{
			entSetActive(pChar->pEntParent);
		}		

		// Debug printing to see what is putting the character into combat
		if (g_erCombatDebugEntRef && g_erCombatDebugEntRef == entGetRef(pChar->pEntParent))
		{
			if (!pSourcePowerDef)
			{
				if (pSourceEnt == pChar->pEntParent)
					combatdebug_PowersDebugPrint(pChar->pEntParent, EPowerDebugFlags_UTILITY, "EnterCombat due to: Own Combat AITeam state COMBAT.\n" );
				else
					combatdebug_PowersDebugPrint(pChar->pEntParent, EPowerDebugFlags_UTILITY, "EnterCombat due to: AI Aggro from entity %s\n", (pSourceEnt ? pSourceEnt->debugName : "UNKNOWN"));
			}
			else
			{
				combatdebug_PowersDebugPrint(pChar->pEntParent, EPowerDebugFlags_UTILITY, "EnterCombat due to: Entity %s with Power %s\n", (pSourceEnt ? pSourceEnt->debugName : "UNKNOWN"), pSourcePowerDef->pchName);
			}
		}
#endif

		if (uiTime > pChar->uiTimeCombatExit)
		{
			pChar->uiTimeCombatExit = uiTime;
			entity_SetDirtyBit(pChar->pEntParent, parse_Character, pChar, false);
		}

		if (bCombatEventActIn)
		{
			character_CombatEventTrack(pChar, kCombatEvent_CombatModeActIn);
		}
		else
		{
			character_CombatEventTrack(pChar, kCombatEvent_CombatModeActOut);
		}

#ifdef GAMESERVER
		// Cancel log off
		gslLogoff_Cancel(pChar->pEntParent, kLogoffCancel_CombatState);
#endif //GAMESERVER
	}
#endif
}

// Places the character in combat visual ONLY mode for the given period of time
void character_SetCombatVisualsExitTime(SA_PARAM_NN_VALID Character *pChar, F32 fTimeInCombat)
{
#if !GAMESERVER && !GAMECLIENT
	assert(0);
#else
	if (fTimeInCombat >= 0.f)
	{
		U32 uiTime = pmTimestamp(fTimeInCombat);

		if (uiTime > pChar->uiTimeCombatVisualsExit)
		{
			bool bWasOn = (pChar->uiTimeCombatVisualsExit != 0);
			pChar->uiTimeCombatVisualsExit = uiTime;
			entity_SetDirtyBit(pChar->pEntParent, parse_Character, pChar, false);

			if (!bWasOn)
			{
				entity_UpdateItemArtAnimFX(pChar->pEntParent);
				pmUpdateCombatAnimBit(pChar->pEntParent, true);
			}
		}
	}
#endif
}

// Resets cooldown timers if necessary when critter exits combat
void character_ResetCooldownTimersOnExitCombat(SA_PARAM_NN_VALID Character *pChar)
{
	S32 i;
	for (i = eaSize(&pChar->ppCooldownTimers)-1; i>=0; i--)
	{
		CooldownTimer *pTimer = pChar->ppCooldownTimers[i];

		if (pTimer->fCooldown > 0)
		{
			PowerCategory *pPowerCat = g_PowerCategories.ppCategories[pTimer->iPowerCategory];
			if (pPowerCat->fTimeCooldownOutOfCombat >= 0.f)
			{
				pTimer->fCooldown = MIN(pTimer->fCooldown, pPowerCat->fTimeCooldownOutOfCombat);
			}
		}
	}
}

// Updates the combat timer for the source and target Characters,
//  based on the source targeting the target with the given powerdef
static void UpdateCombatTimer(int iPartitionIdx, Character *pcharSource, Character *pcharTarget, PowerDef *pdef)
{
#if !GAMESERVER && !GAMECLIENT
	assert(0);
#else
	F32 fSourceCombatTimer = 0.0f;
	F32 fTargetCombatTimer = 0.0f;

	if(!g_CombatConfig.pTimer)
		return;

	if(pdef->eSourceEnterCombat==kPowerEnterCombatType_Always)
		fSourceCombatTimer = g_CombatConfig.pTimer->fTimerAttack;

	// Passives don't put you in combat, nor does targeting yourself
	if(pdef->eType!=kPowerType_Passive && pcharSource!=pcharTarget)
	{
		if(pcharTarget==NULL)
		{
			PowerTarget *ppowtarget = GET_REF(pdef->hTargetAffected);
			// Used a power that didn't end up targeting anyone, check if it affects foes
			if(ppowtarget && ppowtarget->bAllowFoe && (pdef->eSourceEnterCombat!=kPowerEnterCombatType_Never))
			{
				fSourceCombatTimer = g_CombatConfig.pTimer->fTimerUse;
				if(pcharSource && g_CombatConfig.pTimer->fTimerUseVisual > 0.0f)
				{
					character_SetCombatVisualsExitTime(pcharSource, g_CombatConfig.pTimer->fTimerUseVisual);
				}
			}
		}
		else
		{
			// Targeted something
			if(character_TargetIsFoe(iPartitionIdx,pcharSource,pcharTarget))
			{
				// Targeted a foe, have the source enter combat if the power allows it
				if(pdef->eSourceEnterCombat!=kPowerEnterCombatType_Never)
					fSourceCombatTimer = g_CombatConfig.pTimer->fTimerAttack;
				
				// Check to see if the target should enter combat
				if(!pdef->bDisableTargetEnterCombat)
				{
					if(!pcharSource)
					{
						// There wasn't a source, only put the target into combat if this affects Foe
						PowerTarget *ppowtarget = GET_REF(pdef->hTargetAffected);
						if(ppowtarget && ppowtarget->bAllowFoe)
							fTargetCombatTimer = g_CombatConfig.pTimer->fTimerAttacked;
					}
					else
					{
						fTargetCombatTimer = g_CombatConfig.pTimer->fTimerAttacked;
					}
				}
#ifdef GAMESERVER
				if(pcharSource && pcharTarget)
				{
					bool bSourceOnly = !!pdef->bDisableTargetEnterCombat;
					Entity *eSource = pcharSource->pEntParent;
					if (entGetType(pcharSource->pEntParent) == GLOBALTYPE_ENTITYPROJECTILE)
					{
						eSource = entFromEntityRef(iPartitionIdx, pcharSource->pEntParent->erCreator);
					}
					
					if (eSource)
						aiNotifyUpdateCombatTimer(eSource, pcharTarget->pEntParent, bSourceOnly);
				}
#endif
			}
			else if(pcharTarget->uiTimeCombatExit)
			{
				// Targeted a non-foe that is in combat
				if(pdef->eSourceEnterCombat!=kPowerEnterCombatType_Never)
					fSourceCombatTimer = g_CombatConfig.pTimer->fTimerAssist;
			}
		}

		// Put the target into combat
		if(pcharTarget)
		{
			if (fTargetCombatTimer>0.0f)
			{
				character_SetCombatExitTime(pcharTarget, fTargetCombatTimer, true, true, 
											(pcharSource ? pcharSource->pEntParent : NULL), pdef);
			}
			else
			{
#ifdef GAMESERVER
				entSetActive(pcharTarget->pEntParent);
#endif //GAMESERVER
			}
		}

	}

	// Put the source into combat
	if(pcharSource && fSourceCombatTimer > 0.0f)
	{
		character_SetCombatExitTime(pcharSource, fSourceCombatTimer, true, false, pcharSource->pEntParent, pdef);
	}
#endif
}

// Tests to see if the source location can see the target, based on the visibility requirements
static S32 LocationVisibilityCheck(	int iPartitionIdx, 
									Entity* pentSource, 
									const Vec3 vecSrc, 
									Entity *pentTarget, 
									WorldInteractionNode *pnodeTarget, 
									const Vec3 vTargetPos,
									TargetVisibility eVis, 
									Vec3 vecTargetOut)
{
	if(eVis==kTargetVisibility_LineOfSight)
	{
		// Does this point have LoS to the target
		Vec3 vCombatPos;

		if(pentTarget && IS_HANDLE_ACTIVE(pentTarget->hCreatorNode))
		{
			pnodeTarget = GET_REF(pentTarget->hCreatorNode);
			if(pnodeTarget)
			{
				pentTarget = NULL;
				vTargetPos = NULL;
			}
		}

		if(pentTarget)
		{
			if (vTargetPos)
			{
				copyVec3(vTargetPos, vCombatPos);
				entOffsetPositionToCombatPos(pentTarget, vCombatPos);
			}
			else
			{
				entGetCombatPosDir(pentTarget, NULL, vCombatPos, NULL);
			}
						
			if(vecTargetOut)
				copyVec3(vCombatPos,vecTargetOut);

			return combat_CheckLoS(iPartitionIdx,vecSrc,vCombatPos,pentSource,pentTarget,NULL,false,false,vecTargetOut);
		}
		else if(pnodeTarget)
		{
			if (vTargetPos)
			{
				copyVec3(vTargetPos, vCombatPos);
			}
			else
			{
				character_FindNearestPointForObject(pentSource ? pentSource->pChar:NULL,vecSrc,pnodeTarget,vCombatPos,true);
			}

			if(vecTargetOut)
				copyVec3(vCombatPos,vecTargetOut);

			return combat_CheckLoS(iPartitionIdx,vecSrc,vCombatPos,pentSource,NULL,wlInteractionNodeGetEntry(pnodeTarget),false,false,NULL);
		}
		return false;
	}
	else
	{
		return true;
	}
}


// Tests to see if the source can see the target, based on the visibility requirements
//  Same as LocationVisibilityCheck, but breaks early if source character is the target character
static S32 CharacterVisibilityCheck(int iPartitionIdx, 
									Entity *pentSource, 
									const Vec3 vecSrc, 
									Entity *pentTarget, 
									const Vec3 vTargetPos,
									TargetVisibility eVis, 
									Vec3 vecTargetOut)
{
	if(pentTarget && pentSource==pentTarget)
	{
		return true;
	}
	else
	{
		return LocationVisibilityCheck(iPartitionIdx,pentSource,vecSrc,pentTarget,NULL,vTargetPos,eVis,vecTargetOut);
	}
}

// Assumes we already know the character target is within a reasonable distance
static S32 CylinderAndVisibilityCheck(	int iPartitionIdx, 
										Entity *pEntSource, 
										const Vec3 vCylinderStart, 
										const Vec3 vCylinderDir,
										F32 fCylinderLength,
										F32 fCylinderRadius, 
										Entity *pEntTarget, 
										WorldInteractionNode *pNodeTarget, 
										const Vec3 vTargetPos,
										const Quat qTargetRot,
										TargetVisibility eVis, 
										Vec3 vecTargetOut)
{
#if !GAMESERVER && !GAMECLIENT
	assert(0);
#else
	S32 bHit = false;
	Vec3 vHitPos = {0};
	Vec3 vCombatPos = {0};
	
	// see if we should be checking vs a world interaction node
	if(pEntTarget && IS_HANDLE_ACTIVE(pEntTarget->hCreatorNode))
	{
		pNodeTarget = GET_REF(pEntTarget->hCreatorNode);
		if(pNodeTarget)
		{
			character_FindNearestPointForObject((pEntSource ? pEntSource->pChar : NULL),
												vCylinderStart,
												pNodeTarget,
												vCombatPos,
												true);
			pEntTarget = NULL;
		}
	}
	else if (pNodeTarget)
	{
		copyVec3(vTargetPos, vCombatPos);
	}

	if (pEntTarget)
	{
		const Capsule*const* peaCapsulesSource = NULL;

		if (mmGetCapsules(pEntTarget->mm.movement, &peaCapsulesSource) && eaSize(&peaCapsulesSource))
		{
			bHit = CapsuleVsCylinder(	peaCapsulesSource[0], vTargetPos, qTargetRot, 
										vCylinderStart, vCylinderDir, fCylinderLength, fCylinderRadius, 
										vHitPos);
			
			copyVec3(vTargetPos, vCombatPos);
			entOffsetPositionToCombatPosByCapsules(pEntTarget, peaCapsulesSource, vCombatPos);
		}
	}
	else if (pNodeTarget)
	{
		// otherwise check if the point is within the cylinder
		if (CylinderVsPoint(vCylinderStart, vCylinderDir, fCylinderLength, fCylinderRadius, vCombatPos))
		{
			bHit = true;
			copyVec3(vTargetPos, vecTargetOut);
		}
	}

	if (bHit)
	{
		if (vecTargetOut) 
		{
			copyVec3(vHitPos, vecTargetOut);
		}

		if(eVis==kTargetVisibility_LineOfSight)
		{
			if(pEntTarget)
				return combat_CheckLoS(iPartitionIdx, vCylinderStart, vCombatPos, pEntSource, pEntTarget, NULL, false, false, NULL);
			else if(pNodeTarget)
				return combat_CheckLoS(iPartitionIdx, vCylinderStart, vCombatPos, pEntSource, NULL, wlInteractionNodeGetEntry(pNodeTarget), false, false, NULL);
		}
		else
		{
			return true;
		}
	}


	return false;
#endif
}

// Assumes we already know the character target is within a reasonable distance
static S32 ConeAndVisibilityCheck(	int iPartitionIdx, 
									Entity *pentSource, 
									const Vec3 vConePos, 
									const Vec3 vConeDir,
									F32 fArc, 
									F32 fConeLength,
									F32 fConeStartingRadius,
									Entity *pentTarget, 
									WorldInteractionNode *pnodeTarget, 
									const Vec3 vTargetPos,
									const Quat qTargetRot,
									TargetVisibility eVis, 
									Vec3 vecTargetOut)
{
	Vec3 vCombatPos, vConeToEnt;
	F32 fcosCone = cosf(RAD(fArc*0.5f));
	F32 fcosTarget;
	S32 bInCone = false;

	PowersDebugPrintEnt(EPowerDebugFlags_APPLY, pentSource, "Source Arc: %f, Cos: %f, Angle: %f\n",fArc, fcosCone, acosf(fcosCone));
	
	if(pentTarget && IS_HANDLE_ACTIVE(pentTarget->hCreatorNode))
	{
		pnodeTarget = GET_REF(pentTarget->hCreatorNode);
		if(pnodeTarget)
		{
			pentTarget = NULL;
			vTargetPos = NULL;
		}
	}

	if(pentTarget)
	{
		if (!vTargetPos)
		{
			entGetCombatPosDir(pentTarget,NULL,vCombatPos,NULL);
		}
		else
		{
			copyVec3(vTargetPos, vCombatPos);
			entOffsetPositionToCombatPos(pentTarget, vCombatPos);
		}
	}
	else if(pnodeTarget)
	{
		if (!vTargetPos)
		{
			character_FindNearestPointForObject(pentSource?pentSource->pChar:NULL,vConePos,pnodeTarget,vCombatPos,true);
		}
		else
		{
			copyVec3(vTargetPos, vCombatPos);
		}
	}

	if(vecTargetOut)
		copyVec3(vCombatPos,vecTargetOut);

	subVec3(vCombatPos,vConePos,vConeToEnt);
	{
		F32 len = lengthVec3(vConeToEnt);

		if(len > FLT_EPSILON){
			fcosTarget = CLAMPF32(dotVec3(vConeDir,vConeToEnt)/len, -1.f, 1.f);
		}else{
			fcosTarget = 1.f;
		}
	}
	
	PowersDebugPrintEnt(EPowerDebugFlags_APPLY, pentSource, "Target Cos: %f, Angle: %f\n", fcosTarget, acosf(fcosTarget));
	
	// If the target point is explicitly inside the exact cone
	if(fcosTarget >= fcosCone)
	{
		bInCone = true;
	}
	else if(pentTarget && fArc < 180.f)
	{
		const Capsule*const* eaCapsules = entGetCapsules(pentTarget);

		if (eaCapsules)
		{
			Vec3 vClosestPt, vConeEndPt;
			F32 fCapsuleLineDist;
			F32 distAlongCone;
			F32 radiusAtClosest;
			F32 tanfArc;
				

			scaleAddVec3(vConeDir, fConeLength, vConePos, vConeEndPt);
			fCapsuleLineDist = CapsuleLineDistance(eaCapsules, vTargetPos, qTargetRot, 
													vConePos, vConeDir, fConeLength, 0.0f, vClosestPt, 0);
				
			subVec3(vClosestPt, vConePos, vConeToEnt);
			distAlongCone = dotVec3(vConeToEnt, vConeDir);

			if (distAlongCone < 0.f)
			{
				if (-distAlongCone > eaCapsules[0]->fRadius)
				{	// the capsule is too far behind the cone
					return false;
				}
				distAlongCone = 0.f;
			}
						
			tanfArc = tanf(RAD(fArc*0.5f));
			radiusAtClosest = distAlongCone * tanfArc;
			if (fConeStartingRadius > 0.f)
			{
				F32 fNorm = distAlongCone / fConeLength;
				F32 width = fConeLength * tanfArc;
				F32 fInvWidth = fConeStartingRadius * (1.f - fNorm);

				radiusAtClosest += fInvWidth;
			}

			bInCone = (fCapsuleLineDist <= radiusAtClosest);
		}
	}

	if(bInCone)
	{
		if(eVis==kTargetVisibility_LineOfSight)
		{
			if(pentTarget)
				return combat_CheckLoS(iPartitionIdx,vConePos,vCombatPos,pentSource,pentTarget,NULL,false,false,vecTargetOut);
			else if(pnodeTarget)
				return combat_CheckLoS(iPartitionIdx,vConePos,vCombatPos,pentSource,NULL,wlInteractionNodeGetEntry(pnodeTarget),false,false,NULL);
		}
		else
		{
			return true;
		}
	}

	return false;
}

// utility function used to get the primitives' starting position
static void powerEffectArea_GetBasePosition(Entity *pEnt, const Vec3 vSourcePos, const Vec3 vDir, Vec3 vBasePosOut)
{
	// Get the cylinder's start position- 
	if(pEnt)
	{	// we are offsetting from an entity, 
		F32 fBasePosOffset = 0.f;
		Vec3 vPrimitiveOffsetDir;
		
		const Capsule* pCapsule = entGetPrimaryCapsule(pEnt);

		if (pCapsule)
		{
			if (vec3IsZeroXZ(pCapsule->vDir))
			{
				fBasePosOffset = pCapsule->fRadius;
			}
			else
			{	// the capsule it not directly upright, roughly project the length and radius to get the offset
				Vec3 vCapsuleEnd;
				scaleAddVec3(pCapsule->vDir, pCapsule->fLength, pCapsule->vStart, vCapsuleEnd);
				fBasePosOffset = (pCapsule->fRadius + vCapsuleEnd[2]) * pCapsule->vDir[2];
				fBasePosOffset += pCapsule->fRadius * pCapsule->vDir[1];
			}
		}
		

		copyVec3(vDir, vPrimitiveOffsetDir);
		vPrimitiveOffsetDir[1] = 0.f;
		normalVec3(vPrimitiveOffsetDir);
		scaleAddVec3(vPrimitiveOffsetDir, fBasePosOffset, vSourcePos, vBasePosOut);
		
	}
	else
	{	// if we don't have a source entity just use the given application's vecSourcePos
		copyVec3(vSourcePos, vBasePosOut);
	}
}

// utility function used to get the primitives' starting position with effect areas centered on the source ent
static void powerEffectArea_GetCenteredPosition(Entity *pEnt, Entity *pEntTarget, const Vec3 vSourcePos, const Vec3 vTargetPos, Vec3 vBasePosOut, Vec3 vBaseDirOut)
{
	// Get the effect's start position and direction 
	if(pEnt)
	{
		const Capsule* pCapsule = entGetPrimaryCapsule(pEnt);

		if (pEntTarget && pEnt != pEntTarget)
		{
			Vec3 vHitTargetPos;

			entGetDistanceSourcePos(pEnt, NULL, pEntTarget, NULL, vBasePosOut, vHitTargetPos);
			subVec3(vHitTargetPos, vBasePosOut, vBaseDirOut);
		}
		else
		{
			if (vTargetPos)
			{
				entGetDistanceSourcePos(pEnt, NULL, NULL, vTargetPos, vBasePosOut, NULL);
			}
			else
			{
				copyVec3(vSourcePos, vBasePosOut);
			}

			subVec3(vTargetPos, vBasePosOut, vBaseDirOut);
		}

		normalVec3(vBaseDirOut);

		if (pCapsule)
		{
			scaleAddVec3(vBaseDirOut, pCapsule->fRadius, vBasePosOut, vBasePosOut);
		}
	}
	else
	{	// if we don't have a source entity just use the given application's vecSourcePos
		copyVec3(vSourcePos, vBasePosOut);
		subVec3(vTargetPos, vBasePosOut, vBaseDirOut);
		normalVec3(vBaseDirOut);
	}
}

static Vec3 s_vecSortDistTarget;

// Comparator for CombatTarget sorting in ascending distance from s_vecSortDistTarget
static int CmpCombatTargetDist(const CombatTarget **a, const CombatTarget **b)
{
	F32 aDist = distance3Squared(s_vecSortDistTarget,(*a)->vecHitPos);
	F32 bDist = distance3Squared(s_vecSortDistTarget,(*b)->vecHitPos);
	return aDist>bDist ? 1 : (aDist<bDist ? -1 : 0);
}

// Comparator for CombatTarget sorting by non-object to object
static int CmpCombatTargetObject(const CombatTarget **a, const CombatTarget **b)
{
	int aObj = IS_HANDLE_ACTIVE((*a)->hObjectNode);
	int bObj = IS_HANDLE_ACTIVE((*b)->hObjectNode);
	return aObj-bObj;
}

// Comparator for CombatTarget sorting first by non-object to object, then in ascending distance from s_vecSortDistTarget
static int CmpCombatTargetObjectDist(const CombatTarget **a, const CombatTarget **b)
{
	int aObj = IS_HANDLE_ACTIVE((*a)->hObjectNode);
	int bObj = IS_HANDLE_ACTIVE((*b)->hObjectNode);
	if(aObj!=bObj)
		return aObj-bObj;
	return CmpCombatTargetDist(a,b);
}

// Comparator for CombatTarget sorting first by main target or not, then non-object to object
static int CmpCombatTargetMainObject(const CombatTarget **a, const CombatTarget **b)
{
	int aMain = (*a)->bIsMainTarget;
	int bMain = (*b)->bIsMainTarget;
	if(aMain!=bMain)
		return bMain-aMain;
	return CmpCombatTargetObject(a,b);
}

// Comparator for CombatTarget sorting first by hard target or not, then non-object to object
static int CmpCombatHardTargetMainObject(const CombatTarget **a, const CombatTarget **b)
{
	int aMain = (*a)->bIsHardTarget;
	int bMain = (*b)->bIsHardTarget;
	if(aMain!=bMain)
		return bMain-aMain;
	return CmpCombatTargetObject(a,b);
}

// Comparator for CombatTarget sorting first by main target or not, then non-object to object, then in ascending distance from s_vecSortDistTarget
static int CmpCombatTargetMainObjectDist(const CombatTarget **a, const CombatTarget **b)
{
	int aMain = (*a)->bIsMainTarget;
	int bMain = (*b)->bIsMainTarget;
	if(aMain!=bMain)
		return bMain-aMain;
	return CmpCombatTargetObjectDist(a,b);
}

// Comparator for CombatTarget sorting first by hard target or not, then non-object to object, then in ascending distance from s_vecSortDistTarget
static int CmpCombatHardTargetObjectDist(const CombatTarget **a, const CombatTarget **b)
{
	int aHard = (*a)->bIsHardTarget;
	int bHard = (*b)->bIsHardTarget;
	if(aHard!=bHard)
		return bHard-aHard;
	return CmpCombatTargetObjectDist(a,b);
}

// Randomly permutes the first n elements of the array of CombatTargets
static void PermuteCombatTargets(CombatTarget **ppTargets, int n)
{
	int i;
	for(i=n-1; i>0; i--)
	{
		int s = randomIntRange(0,i);
		CombatTarget *pTemp = ppTargets[s];
		ppTargets[s] = ppTargets[i];
		ppTargets[i] = pTemp;
	}
}

// Sorts the target list if needed.  Returns the number of valid targets if it is more than the number
//  of targets actually in the list, otherwise returns 0.  If a pentMainTarget is provided, the CombatTarget
//  that matches will always be sorted to the front.
static int SortApplyTargets(SA_PARAM_NN_VALID PowerDef *pdef, 
							SA_PARAM_NN_VALID CombatTarget ***pppTargets, 
							SA_PARAM_NN_VALID PowerApplication *papp,
							SA_PRE_NN_RELEMS(3) const Vec3 vecTarget,
							SA_PARAM_OP_VALID Entity *pentSource,
							SA_PARAM_OP_VALID Entity *pentMainTarget)
{
	int i,iTargets = eaSize(pppTargets);
	if(iTargets > 1)
	{
		EffectAreaSort eSort = pdef->eEffectAreaSort;
		PERFINFO_AUTO_START_FUNC();
		copyVec3(vecTarget,s_vecSortDistTarget);
	
		if(pentMainTarget && eSort==kEffectAreaSort_Primary_Dist)
		{
			EntityRef erCurTarget = 0;

			if (papp->erProximityAssistTarget)
				erCurTarget = papp->erProximityAssistTarget;
			else 
				erCurTarget = pentMainTarget ? entGetRef(pentMainTarget) : 0;

			if (erCurTarget)
			{
				// Sort main target to front, then non-objects to front, then distance sort, and then return the max potential targets
				for(i=eaSize(pppTargets)-1; i>=0; i--)
				{
					CombatTarget* pTarget = (*pppTargets)[i];
					EntityRef erTarget = SAFE_MEMBER2(pTarget->pChar, pEntParent, myRef);
					pTarget->bIsMainTarget = (erTarget == erCurTarget);
				}
			}
			
			qsort((void*)*pppTargets,iTargets,sizeof(CombatTarget *),CmpCombatTargetMainObjectDist);
		}
		else if (pentSource && eSort==kEffectAreaSort_HardTarget_Dist)
		{
			// Sort hard target to front, then non-objects to front, then distance sort, and then return the max potential targets
			for(i=eaSize(pppTargets)-1; i>=0; i--)
			{
				CombatTarget* pTarget = (*pppTargets)[i];
				EntityRef erTarget = SAFE_MEMBER2(pTarget->pChar, pEntParent, myRef);
				pTarget->bIsHardTarget = (erTarget == SAFE_MEMBER(pentSource->pChar, currentTargetRef));
			}
				
			qsort((void*)*pppTargets,iTargets,sizeof(CombatTarget *),CmpCombatHardTargetObjectDist);
		}
		else if(eSort==kEffectAreaSort_Primary_Dist || eSort==kEffectAreaSort_Dist || eSort==kEffectAreaSort_HardTarget_Dist)
		{
			// Sort non-objects to front, then distance sort, and then return the max potential targets
			qsort((void*)*pppTargets,iTargets,sizeof(CombatTarget *),CmpCombatTargetObjectDist);
		}
		else if(pentMainTarget && eSort==kEffectAreaSort_Primary_Random)
		{
			// Sort main target to front, then non-objects to front
			for(i=eaSize(pppTargets)-1; i>=0; i--)
			{
				CombatTarget* pTarget = (*pppTargets)[i];
				pTarget->bIsMainTarget = (pTarget->pChar && pTarget->pChar->pEntParent==pentMainTarget);
			}
			qsort((void*)*pppTargets,iTargets,sizeof(CombatTarget *),CmpCombatTargetMainObject);
		}
		else if(pentSource && eSort==kEffectAreaSort_HardTarget_Random)
		{
			// Sort hard target to front, then non-objects to front
			for(i=eaSize(pppTargets)-1; i>=0; i--)
			{
				CombatTarget* pTarget = (*pppTargets)[i];
				EntityRef erTarget = SAFE_MEMBER2(pTarget->pChar, pEntParent, myRef);
				pTarget->bIsHardTarget = (erTarget == SAFE_MEMBER(pentSource->pChar, currentTargetRef));
			}
			
			qsort((void*)*pppTargets,iTargets,sizeof(CombatTarget *),CmpCombatHardTargetMainObject);
		}
		else
		{
			// Sort non-objects to front
			qsort((void*)*pppTargets,iTargets,sizeof(CombatTarget *),CmpCombatTargetObject);
		}

		switch (eSort)
		{
			xcase kEffectAreaSort_Primary_Random:
			acase kEffectAreaSort_HardTarget_Random:
			acase kEffectAreaSort_Random:
			{
				// Randomize the groups (skip main, randomize non-objects, randomize objects)
				int j;
				i=0;
				i = ((*pppTargets)[0]->bIsMainTarget || (*pppTargets)[0]->bIsHardTarget) ? 1 : 0;
				for(j=i; j<iTargets; j++)
				{
					if(IS_HANDLE_ACTIVE((*pppTargets)[j]->hObjectNode))
						break;
				}
				PermuteCombatTargets((*pppTargets)+i, j-i);
				PermuteCombatTargets((*pppTargets)+j, iTargets-j);
			}
		}

		// Clamp the number of targets returned
		if(pdef->iMaxTargetsHit > 0)
		{
			MIN1(iTargets,pdef->iMaxTargetsHit);
		}
		PERFINFO_AUTO_STOP();
	}
	return iTargets;
}

static int FindCombatCharacter(CombatTarget **ppCombatList, Character *pChar)
{
	int i;

	for(i=0;i<eaSize(&ppCombatList);i++)
	{
		if(ppCombatList[i]->pChar == pChar)
			return i;
	}
	
	return -1;
}

AUTO_EXPR_FUNC(gameutil) ACMD_NAME(CanAffect);
S32 entity_CanAffect(ACMD_EXPR_PARTITION iPartitionIdx, SA_PARAM_OP_VALID Entity *pentSource, SA_PARAM_OP_VALID Entity *pentTarget)
{
	if(pentSource)
	{
		// OnlyAffectSelf test
		if(SAFE_MEMBER3(pentSource,pChar,pattrBasic,fOnlyAffectSelf) > 0 && pentSource != pentTarget)
			return false;

		// Safe test
		if(pentSource->pChar
			&& pentTarget
			&& pentTarget->pChar
			&& (pentSource->pChar->bSafe != pentTarget->pChar->bSafe
				|| (pentSource->pChar->bSafe
					&& character_TargetIsFoe(iPartitionIdx,pentSource->pChar,pentTarget->pChar))))
		{
			return false;
		}
	}

	return true;
}

// Wrapper function to get an earray of valid Entities: Server does an entgrid lookup, everything else iterates over all entities
static void GetApplyTargetsEntEArray(int iPartitionIdx, const Vec3 pos, Entity*** result, F32 distance, EntityFlags entFlagsToMatch, EntityFlags entFlagsToExclude)
{
#ifdef GAMESERVER
	entGridProximityLookupExEArray(iPartitionIdx,pos,result,distance,entFlagsToMatch,entFlagsToExclude,NULL);
#else
	EntityIterator *iter = entGetIteratorAllTypesAllPartitions(entFlagsToMatch,entFlagsToExclude);
	Entity *pent;
	eaClearFast(result);
	while(pent = EntityIteratorGetNext(iter))
	{
		if(pent->pChar)
		{
			eaPush(result,pent);
		}
	}
	EntityIteratorRelease(iter);
#endif
}


static bool CharacterIsLoaded(Character *pchar)
{
#ifdef GAMESERVER
	GlobalType eType = entGetType(pchar->pEntParent);
	switch (eType)
	{
		xcase GLOBALTYPE_ENTITYPLAYER:
		acase GLOBALTYPE_ENTITYSAVEDPET:
		{
			return pchar->bLoaded;
		}
	}
#endif
	return true;
}

#define COMBAT_PERF_START_EXCLUDE() GET_CPU_TICKS_64(lTimerExclude)
#define COMBAT_PERF_STOP_EXCLUDE() if(lTimerExclude) { S64 lTimerExcludeClose; GET_CPU_TICKS_64(lTimerExcludeClose); lTimerExcludeAccum += (lTimerExcludeClose - lTimerExclude); lTimerExclude = 0;	}

static F32 s_fAdditionalQueryDist = 30.f;
static S64 lTimerComplete = 0;
static S64 lTimerExclude = 0;
static S64 lTimerExcludeAccum = 0;

static int GetApplyTargets(	int iPartitionIdx, 
							PowerApplication *papp, 
							Entity *pentTarget, 
							const Vec3 vecTarget, 
							CombatTarget ***pppTargets)
{
#if !GAMESERVER && !GAMECLIENT
	assert(0);
#else
	static Entity **s_ppEnts = NULL;
	static EntityFlags s_uiEntFlagsExclude = ENTITYFLAG_IGNORE | ENTITYFLAG_UNTARGETABLE;
	
	PowerDef *pdef = SAFE_MEMBER(papp,pdef);
	PowerTarget *ppowtarget = SAFE_GET_REF(pdef,hTargetAffected);
	Entity *pentSource = SAFE_MEMBER2(papp,pcharSource,pEntParent);
	int i,iTargets = 0;
	Vec3 vecTargetHit;

	if(!ppowtarget)
	{
		return 0;
	}

	PERFINFO_AUTO_START_FUNC();

	switch(pdef->eEffectArea)
	{
		case kEffectArea_Character:
			PERFINFO_AUTO_START("Character",1);
			{
				Character *pTarget = pentTarget ? pentTarget->pChar : NULL;
			
				// pentTarget is the "real" target of the Power (aka the Main target).  That
				//  means we've already done the Main target type and visibility check.  It's
				//  almost never the case (and really should not be the case) that the Affected
				//  target is a different target type or different visibility.  However, just to 
				//  be safe, we still perform the target type check, and perform the visibility
				//  check if it's different.
				// This also means the passed in vecTarget is the "real" vecTargetHit, so we want
				//  to copy that in as the default under the assumption we're not going to
				//  re-perform the vis check.
					
				if(vecTarget)
					copyVec3(vecTarget,vecTargetHit);
				else
					zeroVec3(vecTargetHit);

				if(pTarget && CharacterIsLoaded(pTarget))
				{
					Vec3 vEntPos;
					
					entGetPosRotAtTime(pentTarget, papp->uiTimestampClientView, vEntPos, NULL);
										
					if (entity_CanAffect(iPartitionIdx, pentSource, pentTarget)
						&& character_TargetMatchesPowerType(iPartitionIdx,papp->pcharSourceTargetType,pTarget,ppowtarget)
						&& (!pentSource || character_CanPerceive(iPartitionIdx,pentSource->pChar,pentTarget->pChar))
						&& ((pdef->eTargetVisibilityMain==pdef->eTargetVisibilityAffected)
						|| CharacterVisibilityCheck(iPartitionIdx,pentSource,papp->vecSourcePos,pentTarget,vEntPos,pdef->eTargetVisibilityAffected,vecTargetHit)))
					{
						CombatTarget *pCombatTarget = MP_ALLOC(CombatTarget);

						copyVec3(vecTargetHit,pCombatTarget->vecHitPos);
						pCombatTarget->pChar = pTarget;
						COPY_HANDLE(pCombatTarget->hObjectNode, pentTarget->hCreatorNode);

						eaPush(pppTargets,pCombatTarget);

						if (g_powersDebugAEOn)
							PowersAEDebug_AddLocation(pentSource, papp->pdef, *pppTargets, vecTargetHit);
					}
				}

			}
			PERFINFO_AUTO_STOP();
			break;

		case kEffectArea_Location:
			break;

		case kEffectArea_Cylinder:
			PERFINFO_AUTO_START("Cylinder",1);
			{
				WorldInteractionNode **ppObjects=NULL;
				Vec3 vCylinderStartPos, vCylinderDir;
				F32 fRadius = 0;
				F32 fRange = power_GetRange(papp->ppow, pdef);
				F32 fDistPow = fRange; 
				Entity *pentSourceDistance = pentSource;
				F32 fQueryRadius = 0.f;
				Vec3 vQueryPosition;

				if(papp->pact && papp->pact->bIncludeLungeRange)
					fDistPow += papp->pact->fLungeDistance;
				
				fRadius = power_GetRadius(iPartitionIdx, papp->pcharSource, papp->ppow, pdef, (pentTarget?pentTarget->pChar:NULL), papp);
				if (papp->ppow)
					papp->ppow->fCachedAreaOfEffectExprValue = fRadius;

				if (papp->bOffsetEffectArea)
				{
					pentSourceDistance = NULL;
				}

				if (pdef->bEffectAreaCentered)
				{
					powerEffectArea_GetCenteredPosition(pentSourceDistance, pentTarget, papp->vecSourcePos, vecTarget, vCylinderStartPos, vCylinderDir);
				}
				else
				{
					// get the direction of the cylinder
					subVec3(vecTarget, papp->vecSourcePos, vCylinderDir);
					normalVec3(vCylinderDir);

					powerEffectArea_GetBasePosition(pentSourceDistance, papp->vecSourcePos, vCylinderDir, vCylinderStartPos);
				}
				
				PERFINFO_AUTO_START("Entities",1);

				// Entities
				//  Cylinder uses bonus distance to account for source capsule
				// get the bounding sphere for our query to get the ents we're going to test against
				// get the mid-point of the cylinder, then compute the radius as the distance to a corner of the cylinder
				fQueryRadius = fDistPow * 0.5f;
				scaleAddVec3(vCylinderDir, fQueryRadius, vCylinderStartPos, vQueryPosition);
					
				fQueryRadius = sqrtf(SQR(fRadius) + SQR(fQueryRadius)) + s_fAdditionalQueryDist;

				GetApplyTargetsEntEArray(	papp->iPartitionIdx,
											vQueryPosition,
											&s_ppEnts,
											fQueryRadius,
											0,
											s_uiEntFlagsExclude);
				
				FOR_EACH_IN_EARRAY(s_ppEnts, Entity, pEnt)
				{
					if(	pEnt->pChar && CharacterIsLoaded(pEnt->pChar) &&
						entity_CanAffect(iPartitionIdx, pentSource, pEnt) && 
						character_TargetMatchesPowerType(iPartitionIdx, papp->pcharSourceTargetType, pEnt->pChar, ppowtarget) && 
						(!pdef->bAffectedRequiresPerceivance || !pentSource || character_CanPerceive(iPartitionIdx,pentSource->pChar,pEnt->pChar)))
					{
						Vec3 vEntPos;
						Quat qEntRot;
						S32 bHit; 
						
						entGetPosRotAtTime(pEnt, papp->uiTimestampClientView, vEntPos, qEntRot);
						
						bHit = CylinderAndVisibilityCheck(	iPartitionIdx, 
															pentSourceDistance,
															vCylinderStartPos,
															vCylinderDir,
															fDistPow,
															fRadius,
															pEnt, 
															NULL,
															vEntPos, 
															qEntRot,
															pdef->eTargetVisibilityAffected,
															vecTargetHit);
						if (bHit)
						{
							CombatTarget *pCombatTarget = MP_ALLOC(CombatTarget);

							copyVec3(vecTargetHit, pCombatTarget->vecHitPos);
							pCombatTarget->pChar = pEnt->pChar;
							COPY_HANDLE(pCombatTarget->hObjectNode, pEnt->hCreatorNode);

							eaPush(pppTargets, pCombatTarget);
						}
					}
				}
				FOR_EACH_END

				PERFINFO_AUTO_STOP();

				if (g_powersDebugAEOn)
				{
					PowersAEDebug_AddCylinder(pentSource, papp->pdef, *pppTargets, vCylinderStartPos, vCylinderDir, fRange, fRadius);
				}

				if((!pdef->iMaxTargetsHit || eaSize(pppTargets)<pdef->iMaxTargetsHit)
					&& entity_CanAffect(iPartitionIdx, pentSource, NULL)
					&& character_TargetMatchesPowerTypeNode(papp->pcharSourceTargetType,ppowtarget))
				{
					// Early exit if the target type doesn't match
					PERFINFO_AUTO_START("Objects",1);
					COMBAT_PERF_START_EXCLUDE();
					// Non-entity objects
					wlInteractionQuerySphere(	iPartitionIdx,
												g_iDestructibleThrowableMask,
												NULL,
												vQueryPosition,
												fDistPow,
												false,
												false,
												true,
												&ppObjects);

					FOR_EACH_IN_EARRAY(ppObjects, WorldInteractionNode, pObject)
					{
						Vec3 vecObject;
						
						//wlInteractionNodeGetWorldMid(pObject,vecObject);
						if(pentSource)
							character_FindNearestPointForObject(pentSource?pentSource->pChar:NULL,papp->vecSourcePos,pObject,vecObject,true);
						else //Can this happen?
							wlInterationNode_FindNearestPoint(papp->vecSourcePos,pObject,vecObject);
#ifdef GAMESERVER	
						{
							Quat qRot;
							S32  bHit;
							unitQuat(qRot);
							bHit = CylinderAndVisibilityCheck(	iPartitionIdx,
																pentSourceDistance,
																vCylinderStartPos,
																vCylinderDir,
																fDistPow,
																fRadius,
																NULL,
																pObject,
																vecObject,
																qRot,
																pdef->eTargetVisibilityAffected,
																vecTargetHit);
							if(bHit)
							{
								Entity *pNewEnt = im_InteractionNodeToEntity(iPartitionIdx, pObject);
								if(pNewEnt
									&& pNewEnt->pChar
									&& character_TargetMatchesPowerType(iPartitionIdx,papp->pcharSourceTargetType,pNewEnt->pChar,ppowtarget))
								{
									CombatTarget *pCombatTarget = MP_ALLOC(CombatTarget);

									copyVec3(vecTargetHit,pCombatTarget->vecHitPos);
									pCombatTarget->pChar = pNewEnt->pChar;
									COPY_HANDLE(pCombatTarget->hObjectNode, pNewEnt->hCreatorNode);

									eaPush(pppTargets,pCombatTarget);
								}
							}
						}
#endif
					}
					FOR_EACH_END

					COMBAT_PERF_STOP_EXCLUDE();
					PERFINFO_AUTO_STOP();
				}

				iTargets = SortApplyTargets(pdef,pppTargets,papp,papp->vecSourcePos,pentSource,pentTarget);
				eaDestroy(&ppObjects);
			}
			PERFINFO_AUTO_STOP();
			break;

		case kEffectArea_Cone:
			PERFINFO_AUTO_START("Cone",1);
			{
				WorldInteractionNode **ppObjects=NULL;
				F32 fArc = 0, fSourceCollRadius = 0;
				F32 fRange = power_GetRange(papp->ppow, pdef);
				F32 fDistPow = fRange;
				Entity *pentSourceDistance = pentSource;
				Vec3 vConeDir, vConePos;
				
				if(papp->pact && papp->pact->bIncludeLungeRange)
					fDistPow += papp->pact->fLungeDistance;
				
				if(pdef->pExprArc)
				{
					combateval_ContextReset(kCombatEvalContext_Target);
					combateval_ContextSetupTarget(papp->pcharSource,pentTarget?pentTarget->pChar:NULL,papp);
					fArc = combateval_EvalNew(iPartitionIdx, pdef->pExprArc,kCombatEvalContext_Target,NULL);
					if (papp->ppow)
						papp->ppow->fCachedAreaOfEffectExprValue = fArc;
				}

				if (papp->bOffsetEffectArea)
				{
					pentSourceDistance = NULL;
				}

				if(pentSourceDistance)
				{
					mmGetCollisionRadius(pentSourceDistance->mm.movement,&fSourceCollRadius);
				}


				PERFINFO_AUTO_START("Entities",1);
				// Entities
				//  Cone uses bonus distance to account for source capsule
				// TODO(JW): if(fDistPow+fSourceCollRadius>500) Errorf("Area of %s is too large, with not hit all potential targets.  Please see Jered.",papp->pdef->pchName);

				subVec3(vecTarget, papp->vecSourcePos, vConeDir);
				normalVec3(vConeDir);

				if (pdef->bEffectAreaCentered)
				{
					copyVec3(papp->vecSourcePos, vConePos);
				}
				else
				{
					powerEffectArea_GetBasePosition(pentSourceDistance, papp->vecSourcePos, vConeDir, vConePos);
				}

				GetApplyTargetsEntEArray(	papp->iPartitionIdx,
											vConePos,
											&s_ppEnts,
											fDistPow + s_fAdditionalQueryDist,
											0,
											s_uiEntFlagsExclude);

				for(i=eaSize(&s_ppEnts)-1; i>=0; i--)
				{
					Entity *pEnt = s_ppEnts[i];
					if(pEnt->pChar && CharacterIsLoaded(pEnt->pChar) && entity_CanAffect(iPartitionIdx, pentSource, pEnt))
					{
						Vec3 vEntPos;
						Quat qEntRot;
						bool bInRange = false;
						
						entGetPosRotAtTime(pEnt, papp->uiTimestampClientView, vEntPos, qEntRot);

						if (pdef->bEffectAreaCentered)
							bInRange = (fDistPow >= entGetDistance(pentSourceDistance, NULL, pEnt, NULL, NULL));
						else
							bInRange = (fDistPow >= entGetDistanceAtPositions(pentSourceDistance, papp->vecSourcePos, pEnt, vEntPos, NULL));
												
						if(bInRange
							&& character_TargetMatchesPowerType(iPartitionIdx,papp->pcharSourceTargetType,pEnt->pChar,ppowtarget)
							&& (!pdef->bAffectedRequiresPerceivance || !pentSource || 
									character_CanPerceive(iPartitionIdx,pentSource->pChar,pEnt->pChar))
							&& ConeAndVisibilityCheck(iPartitionIdx,pentSourceDistance,vConePos,vConeDir,
														fArc,fDistPow,pdef->fStartingRadius,pEnt,NULL,vEntPos,qEntRot,
														pdef->eTargetVisibilityAffected,vecTargetHit))
						{
							CombatTarget *pCombatTarget = MP_ALLOC(CombatTarget);

							copyVec3(vecTargetHit,pCombatTarget->vecHitPos);
							pCombatTarget->pChar = pEnt->pChar;
							COPY_HANDLE(pCombatTarget->hObjectNode, pEnt->hCreatorNode);

							eaPush(pppTargets,pCombatTarget);
						}
					}
				}
				PERFINFO_AUTO_STOP();

				if (g_powersDebugAEOn)
				{
					PowersAEDebug_AddCone(pentSource, papp->pdef, *pppTargets, vConePos, vConeDir, fArc, fRange, pdef->fStartingRadius);
				}

				if((!pdef->iMaxTargetsHit || eaSize(pppTargets)<pdef->iMaxTargetsHit)
					&& entity_CanAffect(iPartitionIdx, pentSource, NULL)
					&& character_TargetMatchesPowerTypeNode(papp->pcharSourceTargetType,ppowtarget))
				{
					Quat qRot;
					PERFINFO_AUTO_START("Objects",1);
					unitQuat(qRot);

					COMBAT_PERF_START_EXCLUDE();
					// Non-entity objects
					wlInteractionQuerySphere(	iPartitionIdx,
												g_iDestructibleThrowableMask,
												NULL,
												vConePos,
												fDistPow,
												false,
												false,
												true,
												&ppObjects);
					for(i=eaSize(&ppObjects)-1;i>=0;i--)
					{
						Vec3 vecObject;
						bool bInRange = false;

						character_FindNearestPointForObject((pentSource?pentSource->pChar:NULL),vConePos,ppObjects[i],vecObject,true);

						if (pdef->bEffectAreaCentered && papp->ppow)
							bInRange = character_TargetInPowerRange(papp->pcharSource, papp->ppow, pdef, NULL, ppObjects[i]);
						else
							bInRange = (fDistPow >= entGetDistanceAtPositions(pentSourceDistance, vConePos, NULL, vecObject, NULL));

						if(bInRange)
						{
#ifdef GAMESERVER
							if(ConeAndVisibilityCheck(iPartitionIdx,pentSourceDistance,vConePos,vConeDir,
													  fDistPow,fArc,pdef->fStartingRadius,NULL,ppObjects[i],vecObject,
													  qRot,pdef->eTargetVisibilityAffected,vecTargetHit))
							{
								Entity *pNewEnt = im_InteractionNodeToEntity(iPartitionIdx, ppObjects[i]);
								if(pNewEnt
									&& pNewEnt->pChar 
									&& character_TargetMatchesPowerType(iPartitionIdx,papp->pcharSourceTargetType,pNewEnt->pChar,ppowtarget))
								{
									CombatTarget *pCombatTarget = MP_ALLOC(CombatTarget);

									copyVec3(vecTargetHit,pCombatTarget->vecHitPos);
									pCombatTarget->pChar = pNewEnt->pChar;
									COPY_HANDLE(pCombatTarget->hObjectNode, pNewEnt->hCreatorNode);

									eaPush(pppTargets,pCombatTarget);
								}
							}
#endif
						}
					}
					COMBAT_PERF_STOP_EXCLUDE();
					PERFINFO_AUTO_STOP();
				}

				iTargets = SortApplyTargets(pdef,pppTargets,papp,papp->vecSourcePos,pentSource,pentTarget);
				eaDestroy(&ppObjects);
			}
			PERFINFO_AUTO_STOP();
			break;

		case kEffectArea_Sphere:
			PERFINFO_AUTO_START("Sphere",1);
			{
				WorldInteractionNode **ppObjects=NULL;
				F32 fRadius = 0, fMinRadius = 0, fDistPow = 0, fDistPowMin = 0, fSourceCollRadius = 0, fTargetCollRadius = 0;
				Entity *pentSourceDistance = pentSource;
				F32 fQueryRadius = 0.f;
				
				fRadius = power_GetRadius(iPartitionIdx, papp->pcharSource, papp->ppow, 
											pdef, (pentTarget?pentTarget->pChar:NULL), papp);
				fDistPow = fRadius;
				if (papp->ppow)
					papp->ppow->fCachedAreaOfEffectExprValue = fRadius;

				if(pdef->pExprInnerRadius)
				{
					combateval_ContextReset(kCombatEvalContext_Target);
					combateval_ContextSetupTarget(papp->pcharSource,pentTarget?pentTarget->pChar:NULL,papp);
					fMinRadius = combateval_EvalNew(iPartitionIdx,pdef->pExprInnerRadius,kCombatEvalContext_Target,NULL);
					fDistPowMin = fMinRadius;
				}

				if (g_CombatConfig.bAreaEffectSpheresCalculateRadiusFromCombatPos || 
					papp->bOffsetEffectArea || 
					pdef->fRange > 0.0f || 
					pdef->fRangeSecondary > 0.0f)
				{
					pentSourceDistance = NULL;
				}

				if(pentSourceDistance)
				{
					mmGetCollisionRadius(pentSourceDistance->mm.movement,&fSourceCollRadius);
				}

				if(pentTarget && pdef->fRange>0)
				{
					mmGetCollisionRadius(pentTarget->mm.movement,&fTargetCollRadius);
				}

				PERFINFO_AUTO_START("Entities",1);
				// Entities
				//  Sphere uses target vec instead of source vec, with bonus distance to account for source capsule and potentially target capsule
				// TODO(JW): if(fDistPow+fSourceCollRadius+fTargetCollRadius>500) Errorf("Area of %s is too large, with not hit all potential targets.  Please see Jered.",papp->pdef->pchName);
				fQueryRadius = fDistPow + fSourceCollRadius + fTargetCollRadius + s_fAdditionalQueryDist;
				GetApplyTargetsEntEArray(	papp->iPartitionIdx,
											vecTarget,
											&s_ppEnts,
											fQueryRadius,
											0,
											s_uiEntFlagsExclude);

				for(i=eaSize(&s_ppEnts)-1; i>=0; i--)
				{
					Entity *pEnt = s_ppEnts[i];
					if(pEnt->pChar && CharacterIsLoaded(pEnt->pChar) && entity_CanAffect(papp->iPartitionIdx, pentSource, pEnt))
					{
						F32 fDistTarget;
						Vec3 vEntPos;

						entGetPosRotAtTime(pEnt, papp->uiTimestampClientView, vEntPos, NULL);

						fDistTarget = entGetDistanceAtPositions(pentSourceDistance, vecTarget, pEnt, vEntPos, NULL);
						
						// Default target location (since LocationVisibilityCheck() won't set it if no LOS required)
						copyVec3(vEntPos,vecTargetHit);

						if(fDistPow>=fDistTarget
							&& (fDistPowMin<=0.0f || fDistPowMin<=fDistTarget)
							&& character_TargetMatchesPowerType(iPartitionIdx,papp->pcharSourceTargetType,pEnt->pChar,ppowtarget)
							&& LocationVisibilityCheck(iPartitionIdx,pentSourceDistance,vecTarget,pEnt,NULL,vEntPos,pdef->eTargetVisibilityAffected,vecTargetHit)
							&& (!pdef->bAffectedRequiresPerceivance || !pentSource || character_CanPerceive(iPartitionIdx,pentSource->pChar,pEnt->pChar)))
						{
							CombatTarget *pCombatTarget = MP_ALLOC(CombatTarget);

							copyVec3(vecTargetHit,pCombatTarget->vecHitPos);
							pCombatTarget->pChar = pEnt->pChar;
							COPY_HANDLE(pCombatTarget->hObjectNode, pEnt->hCreatorNode);

							eaPush(pppTargets,pCombatTarget);
						}
					}
				}
				PERFINFO_AUTO_STOP();

				if (g_powersDebugAEOn)
					PowersAEDebug_AddSphere(pentSource, papp->pdef, *pppTargets, vecTarget, fRadius);

				if((!pdef->iMaxTargetsHit || eaSize(pppTargets)<pdef->iMaxTargetsHit)
					&& entity_CanAffect(iPartitionIdx, pentSource, NULL)
					&& character_TargetMatchesPowerTypeNode(papp->pcharSourceTargetType,ppowtarget))
				{
					PERFINFO_AUTO_START("Objects",1);
					COMBAT_PERF_START_EXCLUDE();
					// Non-entity objects
					wlInteractionQuerySphere(iPartitionIdx,g_iDestructibleThrowableMask,NULL,papp->vecSourcePos,fDistPow,false,false,true,&ppObjects);
					for(i=eaSize(&ppObjects)-1;i>=0;i--)
					{
						F32 fDistTarget;
						Vec3 vecObject;

						character_FindNearestPointForObject(pentSource?pentSource->pChar:NULL,papp->vecSourcePos,ppObjects[i],vecObject,true);

						fDistTarget = entGetDistance(pentSourceDistance, vecTarget, NULL, vecObject, NULL);

						// Default target location (since LocationVisibilityCheck() won't set it if no LOS required)
						copyVec3(vecObject,vecTargetHit);

						if(fDistPow>=fDistTarget)
						{
#ifdef GAMESERVER
							if(LocationVisibilityCheck(iPartitionIdx,pentSourceDistance,vecTarget,NULL,ppObjects[i],vecObject,pdef->eTargetVisibilityAffected,vecTargetHit))
							{
								Entity *pNewEnt = im_InteractionNodeToEntity(iPartitionIdx, ppObjects[i]);
								if(pNewEnt
									&& pNewEnt->pChar
									&& character_TargetMatchesPowerType(iPartitionIdx,papp->pcharSourceTargetType,pNewEnt->pChar,ppowtarget))
								{
									CombatTarget *pCombatTarget = MP_ALLOC(CombatTarget);

									copyVec3(vecTargetHit,pCombatTarget->vecHitPos);
									pCombatTarget->pChar = pNewEnt->pChar;
									COPY_HANDLE(pCombatTarget->hObjectNode, pNewEnt->hCreatorNode);

									eaPush(pppTargets,pCombatTarget);
								}
							}
#endif
						}
					}
					COMBAT_PERF_STOP_EXCLUDE();
					PERFINFO_AUTO_STOP();
				}

				iTargets = SortApplyTargets(pdef,pppTargets,papp,vecTarget,pentSource,NULL);
				eaDestroy(&ppObjects);
			}
			PERFINFO_AUTO_STOP();
			break;

		case kEffectArea_Team:
			PERFINFO_AUTO_START("Team",1);
			{
#ifdef GAMESERVER
				if(pentSource && pentSource->aibase && pentSource->aibase->team)
				{
					static Entity **s_ppEntitiesTeamed = NULL;
					F32 fRadius = 0;
					AITeam *pAITeam = pentSource->aibase->team;

					eaClearFast(&s_ppEntitiesTeamed);

					fRadius = power_GetRadius(iPartitionIdx, papp->pcharSource, papp->ppow, pdef, NULL, papp);

					// Push all the members of this ai-team into the potential list
					for(i=eaSize(&pAITeam->members)-1; i>=0; i--)
					{
						eaPushUnique(&s_ppEntitiesTeamed,pAITeam->members[i]->memberBE);
					}

					// This might require more work in regards to AITeams to get this perfect, but this should
					// provide basic functionality for team powers to affect team-ups at least
					if (pentSource->pTeamUpRequest)
					{
						TeamUpInstance *pTeamUpInstance = gslTeamUpInstance_FromKey(iPartitionIdx, pentSource->pTeamUpRequest->uTeamID);
						if (pTeamUpInstance)
						{
							TeamUpGroup **ppGroups = pTeamUpInstance->ppGroups;
							TeamUp_AddTeamToEntityList(pentSource, ppGroups, &s_ppEntitiesTeamed, NULL, false);
						}
					}

					// RMARR - this is no longer necessary as of my change to merge AITeams when a player joins a team [RMARR- 5/25/11]
#if 0
					// If there's a teamOwner, it means it's a team based around a Player,
					//  and that Player could be on a player-team, so we need to include
					//  those ai-teams as well.
					if(pentSource->aibase->team->teamOwner)
					{
						Entity *pentPlayer = pentSource->aibase->team->teamOwner;
						Team *pTeam = team_GetTeam(pentPlayer);
						if(pTeam)
						{
							for(i=eaSize(&pTeam->eaMembers)-1; i>=0; i--)
							{
								Entity *pentPlayerTeamed = entFromContainerID(iPartitionIdx,GLOBALTYPE_ENTITYPLAYER,pTeam->eaMembers[i]->iEntID);
								if(pentPlayerTeamed && pentPlayerTeamed!=pentPlayer && pentPlayerTeamed->aibase && pentPlayerTeamed->aibase->team)
								{
									int j;
									for(j=eaSize(&pentPlayerTeamed->aibase->team->members)-1; j>=0; j--)
									{
										eaPushUnique(&s_ppEntitiesTeamed,pentPlayerTeamed->aibase->team->members[j]->memberBE);
									}
								}
							}
						}
					}
#endif

					// Now we've got a complete list of potentially valid "team" ents, do the per-ent
					//  validity check
					for(i=eaSize(&s_ppEntitiesTeamed)-1; i>=0; i--)
					{
						Entity *pent = s_ppEntitiesTeamed[i];
						if(pent->pChar && CharacterIsLoaded(pent->pChar)
							&& entity_CanAffect(iPartitionIdx, pentSource, pent)
							&& character_TargetMatchesPowerType(iPartitionIdx,papp->pcharSourceTargetType,pent->pChar,ppowtarget)
							&& (fRadius==0 || fRadius >= entGetDistance(pentSource,vecTarget,pent,NULL,NULL))
							&& LocationVisibilityCheck(iPartitionIdx,pentSource,vecTarget,pent,NULL,NULL,pdef->eTargetVisibilityAffected,vecTargetHit)
							&& (!pdef->bAffectedRequiresPerceivance || !pentSource || character_CanPerceive(iPartitionIdx,pentSource->pChar,pent->pChar)))
						{
							CombatTarget *pCombatTarget = MP_ALLOC(CombatTarget);

							entGetCombatPosDir(pent,NULL,pCombatTarget->vecHitPos,NULL);
							pCombatTarget->pChar = pent->pChar;
							COPY_HANDLE(pCombatTarget->hObjectNode, pent->hCreatorNode);

							eaPush(pppTargets,pCombatTarget);
						}
					}
				}
#endif
				iTargets = SortApplyTargets(pdef,pppTargets,papp,vecTarget,pentSource,NULL);
			}
			PERFINFO_AUTO_STOP();
			break;

		case kEffectArea_Volume:
			PERFINFO_AUTO_START("Volume",1);
			if(papp->pppvolTarget)
			{
				devassert(s_EntityVolumeQueryType);

				// Given a list of all volumes affected, create a list of every entity in those volumes
				for(i=eaSize(papp->pppvolTarget)-1; i>=0; i--)
				{
					WorldVolume* volume = (*papp->pppvolTarget)[i];
					WorldVolumeQueryCache** caches = wlVolumeGetCachedQueries(volume);
					int j, k = eaSize(&caches);

					// Each "query cache" corresponds to a thing in the volume.
					for(j=0; j<k; j++)
					{
						if(wlVolumeQueryCacheIsType(caches[j], s_EntityVolumeQueryType))
						{
							Entity *pEnt = wlVolumeQueryCacheGetData(caches[j]);
							
							if(entCheckFlag(pEnt,s_uiEntFlagsExclude))
							{
								continue;
							}

							// Add the character to the list if it wasn't there before
							if(pEnt->pChar && CharacterIsLoaded(pEnt->pChar)
								&& entity_CanAffect(iPartitionIdx, pentSource, pEnt)
								&& character_TargetMatchesPowerType(iPartitionIdx,papp->pcharSourceTargetType,pEnt->pChar,ppowtarget)
								&& FindCombatCharacter(*pppTargets, pEnt->pChar) == -1
								&& (!pdef->bAffectedRequiresPerceivance || !pentSource || character_CanPerceive(iPartitionIdx,pentSource->pChar,pEnt->pChar)))
							{
								CombatTarget *pCombatTarget = MP_ALLOC(CombatTarget);

								entGetCombatPosDir(pEnt,NULL,pCombatTarget->vecHitPos,NULL);
								pCombatTarget->pChar = pEnt->pChar;
								COPY_HANDLE(pCombatTarget->hObjectNode, pEnt->hCreatorNode);

								eaPush(pppTargets,pCombatTarget);
							}
						}
					}
				}
			}
			PERFINFO_AUTO_STOP();
			break;

		case kEffectArea_Map:
			PERFINFO_AUTO_START("Map",1);
			{
				EntityIterator *iter = entGetIteratorAllTypes(iPartitionIdx,0,s_uiEntFlagsExclude);
				Entity *pent;
				while(pent = EntityIteratorGetNext(iter))
				{
					if(pent->pChar && CharacterIsLoaded(pent->pChar))
					{
						Vec3 vEntPos;

						entGetPosRotAtTime(pent, papp->uiTimestampClientView, vEntPos, NULL);

						if (entity_CanAffect(iPartitionIdx, pentSource, pent)
							&& character_TargetMatchesPowerType(iPartitionIdx,papp->pcharSourceTargetType,pent->pChar,ppowtarget)
							&& CharacterVisibilityCheck(iPartitionIdx,pentSource,papp->vecSourcePos,pent,vEntPos,pdef->eTargetVisibilityAffected,vecTargetHit)
							&& (!pdef->bAffectedRequiresPerceivance || !pentSource || character_CanPerceive(iPartitionIdx,pentSource->pChar,pent->pChar)))
						{
							CombatTarget *pCombatTarget = MP_ALLOC(CombatTarget);

							entGetCombatPosDir(pent,NULL,pCombatTarget->vecHitPos,NULL);
							pCombatTarget->pChar = pent->pChar;
							COPY_HANDLE(pCombatTarget->hObjectNode, pent->hCreatorNode);

							eaPush(pppTargets,pCombatTarget);
						}
					}
				}
				EntityIteratorRelease(iter);

				iTargets = SortApplyTargets(pdef,pppTargets,papp,vecTarget,pentSource,NULL);
			}
			PERFINFO_AUTO_STOP();
			break;

	}

	if(!iTargets) iTargets = eaSize(pppTargets);

	PERFINFO_AUTO_STOP();

	return iTargets;
#endif
}

static void GetLocationFXTarget(SA_PARAM_NN_VALID PowerApplication *papp, Vec3 vecRealTarget, Vec3 vecLocationFXTarget, Quat quatLocationFXTarget)
{
	// Try to fill with the loc/rot of first element of first volume being targeted
	if(papp->pppvolTarget && eaSize(papp->pppvolTarget))
	{
		WorldVolume *pvol = (*papp->pppvolTarget)[0];
		if(pvol)
		{
			wlVolumeGetWorldPosRotMinMax(pvol,vecLocationFXTarget,quatLocationFXTarget,NULL,NULL);
			return;
		}
	}

	// Wasn't targeting a useful volume, so just use the real target with no quat
	copyVec3(vecRealTarget,vecLocationFXTarget);
	zeroQuat(quatLocationFXTarget);
}

// Calculates the appropriate SBLORN seed for a PowerApplication given a PowerActivation
static U32 PowerActSBLORNSeedForApply(SA_PARAM_NN_VALID PowerActivation *pact)
{
	U32 uiSeed = pact->uiSeedSBLORN + pact->uiPeriod;
	return uiSeed;
}

// Returns the SBLORN seed to be used for the target for the PowerApplication.  This value will
//  be different per target and per PowerApplication, but is predictable by the client, as long
//  as the client uses it in the exact same manner as the server (e.g. same number of calls, etc).
static U32 GetApplySBLORNSeed(	SA_PARAM_OP_VALID PowerApplication *papp,
								SA_PARAM_OP_VALID PowerActivation *pact,
								SA_PARAM_OP_VALID Character *pcharSource,
								SA_PARAM_NN_VALID Character *pcharTarget)
{
	// Falling back to the Character's uiPowerActSeq isn't technically correct, but it should work 99.9% of the time
	U32 uiSeed = papp ? papp->uiSeedSBLORN : pact && pact->uiSeedSBLORN ? PowerActSBLORNSeedForApply(pact) : pcharSource ? pcharSource->uiPowerActSeq : 0;
	U32 erTarget = entGetRef(pcharTarget->pEntParent);
	uiSeed = uiSeed ^ erTarget;
	return uiSeed;
}

static int s_iPlayerHitMod = 0;
static int s_iEnemyHitMod = 0;

AUTO_CMD_INT(s_iPlayerHitMod,PlayerHitMod) ACMD_CATEGORY(Debug) ACMD_SERVERONLY;
AUTO_CMD_INT(s_iEnemyHitMod,EnemyHitMod) ACMD_CATEGORY(Debug) ACMD_SERVERONLY;

S32 combat_HitTest(	int iPartitionIdx,
					PowerApplication *papp,
					PowerActivation *pact,
					Character *pcharSource,
					Character *pcharTarget,
					Power *ppow,
					bool bEvalHitChanceWithoutPower,
					F32 *pfHitChanceOverflow)
{
	S32 bHit = true;
	
	U32 uiID = papp ? papp->uiApplyID : 0;

	if(pcharSource != pcharTarget)
	{
		PowerDef *pdef = papp ? papp->pdef : GET_REF(pact->hdef);
		F32 fHitChance=1, fHitRoll=0;
		Power **ppEnhancements = NULL;

		if(!papp && pcharSource)
			character_ActRefEnhancements(iPartitionIdx,pcharSource,pact,&ppEnhancements);
		else if(papp)
			ppEnhancements = papp->pppowEnhancements;

		if(!ppow && pcharSource && pact)
			ppow = character_ActGetPower(pcharSource,pact);

		fHitChance = EvalHitChance(iPartitionIdx,pcharSource,pcharTarget,ppow,pdef,ppEnhancements,bEvalHitChanceWithoutPower,pfHitChanceOverflow);
		PowersDebugPrintEnt(EPowerDebugFlags_APPLY, SAFE_MEMBER(pcharSource,pEntParent), "Apply %d: HitChance %f\n",uiID,fHitChance);

		// If we only calculate this once, and we're past the first stage of the Activation, get
		//  the old result.  If for some reason this is set on an Activation loaded from the db,
		//  you'll end up always missing, which is safer than always hitting.
		// We still evaluate the HitChance for this case so we can get the overflow value.
		if(pdef->bHitChanceOneTime
			&& pact
			&& ((pdef->eType==kPowerType_Maintained && pact->uiPeriod>0)
				|| (pdef->eType==kPowerType_Toggle && pact->uiPeriod>1)))
		{
			// Get the saved bHit result
			fHitChance = pact->bHitPrior ? 1 : 0;
		}

		if(fHitChance<1)
		{
			U32 uiSeedSBLORN = GetApplySBLORNSeed(papp,pact,pcharSource,pcharTarget);
			fHitRoll = randomPositiveF32Seeded(&uiSeedSBLORN,RandType_BLORN_Static); // Modifies uiSeedSBLORN
			PowersDebugPrintEnt(EPowerDebugFlags_APPLY, pcharSource->pEntParent, "Apply %d: HitRoll %f\n", uiID, fHitRoll);
		}

		bHit = (fHitRoll <= fHitChance);

		if(!papp)
			eaDestroy(&ppEnhancements);
	}

	return bHit;
}

static __forceinline bool ApplyPower_ShouldPlayHitFx(Character *pCharTarget)
{
	return	(!pCharTarget->pCombatReactivePowerInfo || CombatReactivePower_ShouldPlayHitFx(pCharTarget)) 
			&& (g_CombatConfig.iPowerAnimFxBlockModeRequire == -1 || 
				eaiFind(&pCharTarget->piPowerModes, g_CombatConfig.iPowerAnimFxBlockModeRequire) < 0);
}

// Performs the requested application
static bool ApplyPowerEx(int iPartitionIdx, SA_PARAM_NN_VALID PowerApplication *papp, SA_PARAM_OP_VALID ExprContext **ppOutContext, bool bEvalHitChanceWithoutPower)
{
	bool bApplied = true;

#if !GAMESERVER && !GAMECLIENT
	assert(0);
#else
	static CombatTarget **s_ppTargets = NULL;
	static EntityRef *s_perTargetsHitLast = NULL;

	int i;
	Entity *pentRealTarget=NULL, *pentRealSource;
	Vec3 vecRealTarget;
	F32 fCheckDelay = 0.0f;
	int bPeriodic = !!papp->uiPeriod;
	int iNumMods;
	
	PowerDef *pdef = papp->pdef;
	PowerAnimFX *pafx = papp->pafx;
	PowerActivation *pact = papp->pact;

	PERFINFO_AUTO_START_FUNC();

	for(i=eaSize(&g_ppchPowersDisabled)-1; i>=0; i--)
	{
		if(!strcmp(papp->pdef->pchName,g_ppchPowersDisabled[i]))
		{
			PowersDebugPrint(EPowerDebugFlags_APPLY, "%s Apply %d DISABLED\n",papp->pdef->pchName,papp->uiActID);
			PERFINFO_AUTO_STOP();
			return false;
		}
	}

	// Assign the random value
	papp->fRandom = randomPositiveF32();

	if(g_uiCombatDebugPerf)//(g_uiCombatDebugPerf)
	{
		GET_CPU_TICKS_64(lTimerComplete);
		lTimerExcludeAccum = 0;
	}
	else
	{
		lTimerComplete = 0;
		lTimerExcludeAccum = 0;
	}

	// Figure out where this is from
	pentRealSource = papp->pcharSource ? papp->pcharSource->pEntParent : NULL;
	PowersDebugPrintEnt(EPowerDebugFlags_APPLY, pentRealSource, "Apply %d: ApplyID %d (%s)\n", papp->uiActID,papp->uiApplyID,pdef->pchName);

	// See if we can find a proper target
	if(!combat_FindRealTargetEx(iPartitionIdx,
								papp->ppow,
								pdef,
								pentRealSource,
								papp->vecSourcePos,
								papp->vecSourceDir,
								papp->erTarget,
								papp->erProximityAssistTarget,
								papp->vecTarget,
								papp->vecTargetSecondary,
								papp->pppvolTarget,
								false,
								papp->uiTimestampClientView,
								&pentRealTarget,
								vecRealTarget,
								NULL,
								NULL))
	{
		PowersDebugPrintEnt(EPowerDebugFlags_APPLY, pentRealSource, "Apply %d FAIL: No valid targets\n", papp->uiActID);
		PERFINFO_AUTO_STOP();
		return false;
	}
	else
	{
		papp->pentTargetEff = pentRealTarget;
		copyVec3(vecRealTarget,papp->vecTargetEff);
		if(papp->pact)
		{
			papp->pact->erTarget = pentRealTarget ? entGetRef(pentRealTarget) : 0;
		}
	}

	iNumMods = eaSize(&pdef->ppOrderedMods);

	PowersDebugPrintEnt(EPowerDebugFlags_APPLY, pentRealSource, "Apply %d: Real target %s\n", papp->uiActID,pentRealTarget?pentRealTarget->debugName:"(NULL)");

	if(pafx && pafx->bCapsuleHit && pentRealTarget)
	{
		Vec3 vecCapsuleTarget;
		WorldCollCollideResults wcResults;

		zeroVec3(vecCapsuleTarget);

		if(worldCollideRay(iPartitionIdx,papp->vecSourcePos,papp->vecTargetEff,WC_QUERY_BITS_COMBAT,&wcResults))
		{
			// Hit a kinematic object, i.e. a moving platform/entity-with-collision-geometry
			// Check to see if target is the same
			Entity *pentHit = NULL;
			if(mmGetUserPointerFromWCO(wcResults.wco, &pentHit))
			{		
				if(pentHit) // Probably could assert, as it's kinematic, it must exist.
				{
					if(pentHit==papp->pentTargetEff)
					{
						copyVec3(wcResults.posWorldImpact,vecCapsuleTarget);
					}
				}
			}
		}

		if (!vec3IsZero(vecCapsuleTarget))
		{
			copyVec3(vecCapsuleTarget,vecRealTarget);
			copyVec3(vecCapsuleTarget,papp->vecTargetEff);
		}
	}
	
	// Play source animations
	if(pafx && (pdef->eType==kPowerType_Click || pdef->eType==kPowerType_Maintained || pdef->eType==kPowerType_Toggle))
	{
		// Figure delay before the actual hit/release, which is the minimum delay for AttribMods
		int *piFramesDelay = (bPeriodic?pafx->piFramesBeforePeriodicHit:pafx->piFramesBeforeHit);
		fCheckDelay = (eaiSize(&piFramesDelay)) ? ((piFramesDelay[0]-papp->iFramesBeforeHitAdjust)/PAFX_FPS) : 0.0f;
		MAX1(fCheckDelay, 0.0f);

		if (g_CombatConfig.bApplyModsImmediately && !pafx->bDelayedHit)
		{
			// For non delayed hits, apply it immediately if the option is set
			fCheckDelay = 0.0;
		}

		if(papp->bAnimActivate && !(pact && pact->bDelayActivateToHitCheck) && !powerddef_ShouldDelayTargeting(pdef))
		{
			Character *pcharTarget = pentRealTarget && pentRealTarget->pChar ? pentRealTarget->pChar : NULL;
			if(pentRealSource && pentRealSource->pChar)
			{
				PowerFXHitType eHitType = bEvalHitChanceWithoutPower ? kPowerFXHitType_UnsetEvalHitChanceWithoutPower : kPowerFXHitType_Unset;
				character_AnimFXActivateOn(iPartitionIdx,pentRealSource->pChar,papp,papp->pact,papp->ppow,pcharTarget,vecRealTarget,papp->uiTimestampAnim,papp->uiActID,papp->uiActSubID,eHitType);
			}
			else
			{
				Vec3 vecLocationFXTarget;
				Quat quatLocationFXTarget;
				GetLocationFXTarget(papp,vecRealTarget,vecLocationFXTarget,quatLocationFXTarget);
				location_AnimFXActivateOn(papp->vecSourcePos,papp->iPartitionIdx,pafx,pcharTarget,vecLocationFXTarget,quatLocationFXTarget,papp->uiTimestampAnim,papp->uiApplyID);
			}
		}
	}

	{
		int j=0, iTargets, iHit=0, iMissed=0;
		F32 fProjectileDelayShared = 0;
		F32 fHitChanceOverflow = 0;
		S32 bEvaluateHitChance = true;
		U32 bAppliedHitPause = false;

#ifdef GAMECLIENT
		bEvaluateHitChance = g_CombatConfig.bClientPredictHit;
#endif



		// Find potentially affected target Characters
		papp->iNumTargets = iTargets = GetApplyTargets(iPartitionIdx, papp, pentRealTarget, vecRealTarget, &s_ppTargets);
		PowersDebugPrintEnt(EPowerDebugFlags_APPLY, pentRealSource, "Apply %d: found %d targets\n", papp->uiActID, iTargets);

		if(pact)
		{
			ea32Copy(&s_perTargetsHitLast,&pact->perTargetsHit);
			eaiClear(&pact->perTargetsHit);
		}

		// Reset the Apply context (just this once)
		combateval_ContextReset(kCombatEvalContext_Apply);

		for(i=0; i<iTargets; i++)
		{
			Character *pcharTarget = s_ppTargets[i]->pChar;
			F32 fProjectileDelay = 0.0f;
			F32 fMeleeSwingDelay = 0.f;
			S32 bHit = true;
			Vec3 vecTargetPos;

			if(pcharTarget->iLevelCombat <= 0)
			{
				if (isDevelopmentMode())
				{
					assertmsg(0,"ApplyPowerEx target without a valid combat level");
				}
				else
				{
					ErrorDetailsf("%s %d %d %s %s",CHARDEBUGNAME(pcharTarget),pcharTarget->iLevelCombat,entGetFlagBits(pcharTarget->pEntParent),CHARDEBUGNAME(papp->pcharSource),pdef->pchName);
					ErrorfForceCallstack("ApplyPowerEx target without a valid combat level");
				}
			}

			// Set up the Application's PrimaryTarget flag and Angle
			papp->bPrimaryTarget = (pcharTarget->pEntParent==papp->pentTargetEff);
			if(pcharTarget != papp->pcharSource)
			{
				Vec3 vecTo;
				F32 fAngleDir, fAngleTo, fHorizontalDirLen;
				Vec2 pyFace;

				entGetCombatPosDir(pcharTarget->pEntParent,NULL,vecTargetPos,NULL);
				entGetFacePY(pcharTarget->pEntParent,pyFace);
				subVec3(papp->vecSourcePos,vecTargetPos,vecTo);
				fHorizontalDirLen = lengthVec3XZ(vecTo);
				
				// Angle to Source from Target - horizontal
				fAngleTo = atan2(vecTo[0],vecTo[2]);
				fAngleDir = pyFace[1];
				papp->fAngleToSource = DEG(subAngle(fAngleTo,fAngleDir));

				// Angle to Target from Source - horizontal
				fAngleTo = atan2(-vecTo[0],-vecTo[2]);
				fAngleDir = atan2(papp->vecSourceDir[0],papp->vecSourceDir[2]);
				papp->fAngleToTarget = DEG(subAngle(fAngleTo,fAngleDir));

				//Angle to Source from Target - vertical
				fAngleTo = atan2(vecTo[1],fHorizontalDirLen);
				fAngleDir = pyFace[0];
				papp->fAngleToSourceVertical = DEG(subAngle(fAngleTo,fAngleDir));

				//Angle to Target from Source - vertical
				fAngleTo = atan2(-vecTo[1],fHorizontalDirLen);
				if (!nearSameF32(papp->vecSourceDir[1], 0.0f)) {
					fAngleDir = atan2(papp->vecSourceDir[1],lengthVec3XZ(papp->vecSourceDir));
				} else {
					fAngleDir = 0.0f;
				}
				papp->fAngleToTargetVertical = DEG(subAngle(fAngleTo,fAngleDir));
			}
			else
			{
				papp->fAngleToSource = 0;
				papp->fAngleToTarget = 0;
				papp->fAngleToSourceVertical = 0;
				papp->fAngleToTargetVertical = 0;
				zeroVec3(vecTargetPos);
			}

			PowersDebugPrintEnt(EPowerDebugFlags_APPLY, pentRealSource, "Apply %d: target %d %s\n",papp->uiActID,i,CHARDEBUGNAME(pcharTarget));

			// Set up the Apply context for this target
			combateval_ContextSetupApply(papp->pcharSource,pcharTarget,NULL,papp);

			// todo: this will be a problem if one of the targets fails
			// If there is a requires expression for the application make sure it returns true for each target
			if (pdef->pExprRequiresApply &&
				!combateval_EvalNew(iPartitionIdx, pdef->pExprRequiresApply, kCombatEvalContext_Apply,NULL))
			{
				bApplied = false;
				continue;
			}

#ifdef GAMESERVER
			// Server update the combat timers and sets things in combat
			UpdateCombatTimer(iPartitionIdx,papp->pcharSource,pcharTarget,pdef);
#endif

			COMBAT_PERF_START_EXCLUDE();
			character_AnimFXTargeted(iPartitionIdx,pcharTarget,s_ppTargets[i]->vecHitPos,papp);
			COMBAT_PERF_STOP_EXCLUDE();

			// Figure delay before the actual 'hit' will occur
			//  TODO(JW): Prediction: Client and server need to sync this properly if the target is moving
			if(pafx && pafx->bDelayedHit)
			{
				// If the delay is shared, and this isn't the first target, use the shared value
				if (pafx->fProjectileSpeed>0.0f)
				{
					if(pafx->bDelayedHitShared && i>0)
					{
						fProjectileDelay = fProjectileDelayShared;
					}
					else
					{
						F32 fDistDelay;
						Vec3 vecHitTarget;

						if (pdef->fRangeSecondary && !ISZEROVEC3(papp->vecTargetSecondary))
						{
							copyVec3(papp->vecTargetSecondary, vecHitTarget);
						}
						else
						{
							entGetCombatPosDir(pcharTarget->pEntParent,NULL,vecHitTarget,NULL);
						}
						fDistDelay = distance3(papp->vecSourcePos, vecHitTarget);
						fDistDelay /= pafx->fProjectileSpeed;
						fProjectileDelay += fDistDelay;
						if(pafx->bDelayedHitShared)
						{
							fProjectileDelayShared = fProjectileDelay;
						}
					}
				}

				// if this has a melee swing speed, we want to see where the target is in relation to the 
				// melee swing arc and then delay the hit to where it is in the arc.
				if (pafx->fMeleeSwingAnglePerSecond && pcharTarget != papp->pcharSource)
				{
					Vec3 vSourceToTarget;
					F32 fYawToTarget;
					F32 fYawSourceDir;
					F32 fStartYaw;
					F32 fAngleDiff;
					
					subVec3(vecTargetPos, papp->vecSourcePos, vSourceToTarget);
					fYawToTarget = getVec3Yaw(vSourceToTarget);
					fYawSourceDir = getVec3Yaw(papp->vecSourceDir);

					fStartYaw = addAngle(fYawSourceDir, RAD(pafx->fMeleeSwingStartAngle));
					fAngleDiff = subAngle(fYawToTarget, fStartYaw);
					
					fMeleeSwingDelay = ABS(fAngleDiff) / RAD(pafx->fMeleeSwingAnglePerSecond);
					
					// if we have a hit-pause, we're adding the a small delay to the pause if the target is on the opposite 
					// side of the source direction from the start 
					if (pafx->fMeleeSwingHitPauseTime && 
						crossVec3ZX(vSourceToTarget, papp->vecSourceDir) > 0.f && 
						pafx->fMeleeSwingStartAngle > 0.f)
					{
						fMeleeSwingDelay += pafx->fMeleeSwingHitPauseTime;
					} 
				}
			}

			// Calculate the chance to hit
			if(bEvaluateHitChance)
				bHit = combat_HitTest(iPartitionIdx,papp,papp->pact,papp->pcharSource,pcharTarget,papp->ppow,bEvalHitChanceWithoutPower,&fHitChanceOverflow);

			if(pdef->bHitChanceOneTime && pact)
			{
				// Save bHit result to Activation for re-use (re-saves, but that's fine)
				pact->bHitPrior = !!bHit;
			}

			if(bHit)
			{
				U32 bHitBits = true;
				CombatTrackerFlag eFlagsApplied = 0;

				PERFINFO_AUTO_START("Hit", 1);

				if (iHit == 0 &&
					pafx &&
					GET_REF(pafx->lurch.hMovementGraph) &&
					papp->pcharSource && 
					papp->pact)
				{
					// Set the hit flag for the lurch
					pmLurchSetHitFlag(papp->pcharSource->pPowersMovement, papp->pact->uchID, true);
				}				

				iHit++;
				papp->iNumTargetsHit = iHit;
				if (papp->pcharSource == pcharTarget)
					papp->bHitSelf = true;

				// If we were supposed to delay the activate until the hit check, play it now
				if(papp->bAnimActivate && pact && pact->bDelayActivateToHitCheck && pafx)
				{
					if(pentRealSource && pentRealSource->pChar)
					{
						character_AnimFXActivateOn(iPartitionIdx,pentRealSource->pChar,papp,papp->pact,papp->ppow,pcharTarget,vecRealTarget,papp->uiTimestampAnim,papp->uiActID,papp->uiActSubID,kPowerFXHitType_Hit);
					}
					else
					{
						Vec3 vecLocationFXTarget;
						Quat quatLocationFXTarget;
						GetLocationFXTarget(papp,vecRealTarget,vecLocationFXTarget,quatLocationFXTarget);
						location_AnimFXActivateOn(papp->vecSourcePos,papp->iPartitionIdx,pafx,pcharTarget,vecLocationFXTarget,quatLocationFXTarget,papp->uiTimestampAnim,papp->uiApplyID);
					}
				}

				// Determine if we want the bits portion of the hit bits/fx
				if(pafx && pafx->bNoPeriodicHitBits && papp->uiActSubID)
				{
					bHitBits = false;
				}

				if(bHitBits && pentRealSource && pentRealSource->mm.movement && pcharTarget->pEntParent && pcharTarget->pEntParent->mm.movement)
				{
					F32 fSourceRadius, fTargetRadius;
					if(!gConf.bNewAnimationSystem
						&& mmGetCollisionRadius(pentRealSource->mm.movement,&fSourceRadius)
						&& mmGetCollisionRadius(pcharTarget->pEntParent->mm.movement,&fTargetRadius)
						&& fTargetRadius > 3.f * fSourceRadius)
					{
						bHitBits = false;
					}
					//If the critter is specifically marked not to hit react, then don't
					else if(pcharTarget->pEntParent->pCritter && pcharTarget->pEntParent->pCritter->bIgnoreExternalAnimBits)
					{
						bHitBits = false;
					}
				}

				// see if we should play the animFXHit, 
				if (ApplyPower_ShouldPlayHitFx(pcharTarget))
				{
					COMBAT_PERF_START_EXCLUDE();
					character_AnimFXHit(iPartitionIdx, pcharTarget, s_ppTargets[i]->vecHitPos, papp, 
										fMeleeSwingDelay + fProjectileDelay, bHitBits, &bAppliedHitPause);
					COMBAT_PERF_STOP_EXCLUDE();
				}

				if(pact)
				{
					eaiPush(&pact->perTargetsHit,entGetRef(pcharTarget->pEntParent));
				}

#ifdef GAMESERVER
				//Evaluate the critical chance numbers, cannot have a critical if there is no power?
				if(papp->ppow)
				{
					EvalCritChance(iPartitionIdx,papp->pcharSource,papp->ppow,papp,pcharTarget,fHitChanceOverflow);
				}

				// Evaluate the avoidance system
				if(g_CombatConfig.pAvoidChance)
					EvalAvoidChance(iPartitionIdx,g_CombatConfig.pAvoidChance,pcharTarget,papp);
				else
					EvalDodgeChance(pcharTarget,papp);

				// Actually make and fill out mods from this def on the proper targets
				character_ApplyModsFromPowerDef(iPartitionIdx,
												pcharTarget,
												papp,
												fMeleeSwingDelay + fProjectileDelay + fCheckDelay,
												fCheckDelay,
												kModTarget_Self|kModTarget_Target,
												false,
												&eFlagsApplied,
												ppOutContext,
												lTimerComplete ? &lTimerExcludeAccum : NULL);

				// Untimed Crit/Dodge Events
				if(eFlagsApplied & kCombatTrackerFlag_Critical)
				{
					character_CombatEventTrackInOut(pcharTarget, kCombatEvent_CriticalIn, kCombatEvent_CriticalOut,
													pentRealSource, pdef, NULL, 0, 0, NULL, NULL);
				}
				if(eFlagsApplied & kCombatTrackerFlag_Dodge)
				{
					character_CombatEventTrackInOut(pcharTarget, kCombatEvent_DodgeIn, kCombatEvent_DodgeOut, 
													pentRealSource, pdef, NULL, 0, 0, NULL, NULL);
				}

#endif

				PERFINFO_AUTO_STOP();
			}
			else
			{
				//  TODO(JW): The mapping of flag to event is a bit too hardcoded here
				F32 fTotalDelay = fMeleeSwingDelay + fProjectileDelay + fCheckDelay;
				CombatTrackerFlag eFlagMiss = (g_CombatConfig.pHitChance && g_CombatConfig.pHitChance->eFlag) ? g_CombatConfig.pHitChance->eFlag : kCombatTrackerFlag_Miss;
				S32 bDodge = (eFlagMiss==kCombatTrackerFlag_Dodge);
				CombatTrackerFlag eAdditionalFlags = 0;
				PERFINFO_AUTO_START("Miss", 1);

				iMissed++;
				papp->iNumTargetsMissed = iMissed;

				// If we were supposed to delay the activate until the hit check, play it now
				if(papp->bAnimActivate && pact && pact->bDelayActivateToHitCheck)
				{
					// We actually want to target a location near the target character in this case
					if(pentRealSource && pentRealSource->pChar)
					{
						character_AnimFXActivateOn(iPartitionIdx,pentRealSource->pChar,papp,papp->pact,papp->ppow,pcharTarget,vecRealTarget,papp->uiTimestampAnim,papp->uiActID,papp->uiActSubID,kPowerFXHitType_Miss);
					}
					else
					{
						Vec3 vecLocationFXTarget;
						Quat quatLocationFXTarget;
						GetLocationFXTarget(papp,vecRealTarget,vecLocationFXTarget,quatLocationFXTarget);
						location_AnimFXActivateOn(papp->vecSourcePos,papp->iPartitionIdx,pafx,pcharTarget,vecLocationFXTarget,quatLocationFXTarget,papp->uiTimestampAnim,papp->uiApplyID);
					}
				}

				COMBAT_PERF_START_EXCLUDE();
				character_AnimFXBlock(iPartitionIdx,pcharTarget,s_ppTargets[i]->vecHitPos,papp,fMeleeSwingDelay+fProjectileDelay);
				COMBAT_PERF_STOP_EXCLUDE();

#ifdef GAMESERVER
				if(papp->bMissMods)
				{
					// Actually make and fill out mods from this def on the proper targets
					character_ApplyModsFromPowerDef(iPartitionIdx,
						pcharTarget,
						papp,
						fMeleeSwingDelay + fProjectileDelay + fCheckDelay,
						fCheckDelay,
						kModTarget_Self|kModTarget_Target,
						true,
						NULL,
						ppOutContext,
						lTimerComplete ? &lTimerExcludeAccum : NULL);
				}

				// Track the untimed combat event
				character_CombatEventTrackInOut(pcharTarget, 
												(bDodge?kCombatEvent_DodgeIn:kCombatEvent_MissIn),
												(bDodge?kCombatEvent_DodgeOut:kCombatEvent_MissOut),
												pentRealSource, pdef, NULL, 0, 0, NULL, NULL);

				if(fTotalDelay<=0)
				{
					// Track the timed combat event
					character_CombatEventTrackInOut(pcharTarget, 
													(bDodge?kCombatEvent_DodgeInTimed:kCombatEvent_MissInTimed),
													(bDodge?kCombatEvent_DodgeOutTimed:kCombatEvent_MissOutTimed),
													pentRealSource, pdef, NULL, 0, 0, NULL, NULL);
				}

				// Make miss combat tracker
				character_CombatTrackerAdd(pcharTarget, pdef, papp->erModOwner, papp->erModSource, NULL, 0,0,0, eFlagMiss | eAdditionalFlags,fTotalDelay, true);
#endif

				PERFINFO_AUTO_STOP();
			}
		}

		// If we had no target characters
		if(iTargets==0)
		{
			PowerTarget *ppowTargetMain = GET_REF(pdef->hTargetMain);

			// If there is a requires expression for the application make sure it returns true
			if (pdef->pExprRequiresApply)				
			{
				// Set up the Apply context
				combateval_ContextSetupApply(papp->pcharSource,NULL,NULL,papp);

				if (!combateval_EvalNew(iPartitionIdx, pdef->pExprRequiresApply, kCombatEvalContext_Apply,NULL))
				{
					bApplied = false;
				}				
			}

#ifdef GAMESERVER
			// TODO(JW): Hack:
			// If the PowerDef has ApplyObjectDeath mods
			if(bApplied && pdef->bApplyObjectDeath && papp->pcharSource)
			{
				int iCurrentPending = eaSize(&papp->pcharSource->modArray.ppModsPending);
				F32 fHitDelay = fCheckDelay;
				AttribMod **ppMods = NULL;

				PERFINFO_AUTO_START("ApplyObjectDeath", 1);

				if(pafx && pafx->bDelayedHit && pafx->fProjectileSpeed>0)
				{
					F32 fDistDelay;
					fDistDelay = distance3(papp->vecSourcePos, vecRealTarget);
					fDistDelay /= pafx->fProjectileSpeed;
					fHitDelay += fDistDelay;
				}
								
				// Set up the Apply context for this special case
				combateval_ContextSetupApply(papp->pcharSource,NULL,NULL,papp);

				// Apply Target mods to the source
				character_ApplyModsFromPowerDef(iPartitionIdx,
												papp->pcharSource,
												papp,
												fHitDelay,
												fCheckDelay,
												kModTarget_Target,
												false,
												NULL,
												NULL,
												lTimerComplete ? &lTimerExcludeAccum : NULL);

				// Go through all the new pending mods, pull out and save any ApplyObjectDeath ones,
				//  destroy the rest
				for(j=eaSize(&papp->pcharSource->modArray.ppModsPending)-1; j>=iCurrentPending; j--)
				{
					AttribMod *pmod = papp->pcharSource->modArray.ppModsPending[j];

					if(pmod->pDef->offAttrib==kAttribType_ApplyObjectDeath)
					{
						eaPush(&ppMods,pmod);
					}
					else
					{
						mod_Destroy(pmod);
					}
					eaRemoveFast(&papp->pcharSource->modArray.ppModsPending,j);
				}

				if(ppMods)
				{
					for(j=eaSize(&ppMods)-1; j>=0; j--)
					{
						copyVec3(papp->vecTargetEff,ppMods[i]->vecSource);
						combat_GlobalAttribModAdd(ppMods[i],papp->iPartitionIdx);
					}
					eaDestroy(&ppMods);
				}

				PERFINFO_AUTO_STOP();
			}
#endif

			// See if we can still blow up the target location
			if(bApplied && pafx && (pafx->bLocationHit || (ppowTargetMain && ppowTargetMain->bUseLocationHitIfNoTarget)))
			{
				F32 fHitDelay = fCheckDelay;
				if(pafx->bDelayedHit && pafx->fProjectileSpeed>0)
				{
					F32 fDistDelay;
					fDistDelay = distance3(papp->vecSourcePos, vecRealTarget);
					fDistDelay /= pafx->fProjectileSpeed;
					fHitDelay += fDistDelay;
				}

				location_AnimFXHit(vecRealTarget,papp,fHitDelay);
			}

#ifdef GAMESERVER
			// We may still want to update the timer and go into combat
			UpdateCombatTimer(iPartitionIdx,papp->pcharSource,NULL,pdef);
#endif
		}

#ifdef GAMESERVER
		if(bApplied && papp->bSelfOnce && papp->pcharSource)
		{
			PERFINFO_AUTO_START("SelfOnce", 1);

			// Set up the Application's PrimaryTarget flag and Angle
			papp->bPrimaryTarget = (papp->pcharSource->pEntParent==papp->pentTargetEff);
			papp->fAngleToSource = 0.f;
			papp->fAngleToTarget = 0.f;
			papp->fAngleToSourceVertical = 0.f;
			papp->fAngleToTargetVertical = 0.f;

			// Set up the Apply context for SelfOnce
			combateval_ContextSetupApply(papp->pcharSource,papp->pcharSource,NULL,papp);

			// Actually make and fill out mods from this def on the proper targets
			//  Just Self-Once AttribMods this time
			character_ApplyModsFromPowerDef(iPartitionIdx,
				papp->pcharSource,
				papp,
				fCheckDelay,
				fCheckDelay,
				kModTarget_SelfOnce,
				false,
				NULL,
				ppOutContext,
				lTimerComplete ? &lTimerExcludeAccum : NULL);

			PERFINFO_AUTO_STOP();
		}
#endif

	}

	for(i=eaSize(&s_ppTargets)-1; i>=0; i--)
	{
		REMOVE_HANDLE(s_ppTargets[i]->hObjectNode);
		MP_FREE(CombatTarget,s_ppTargets[i]);
	}
	eaClearFast(&s_ppTargets);

	if(bApplied && papp->pcharSource && pact && ea32Size(&s_perTargetsHitLast))
	{
		for(i=ea32Size(&pact->perTargetsHit)-1; i>=0; i--)
			ea32FindAndRemoveFast(&s_perTargetsHitLast,pact->perTargetsHit[i]);

		if(ea32Size(&s_perTargetsHitLast))
			character_AnimFXHitStickyFXOff(iPartitionIdx,papp->pcharSource,pact,pmTimestamp(0),s_perTargetsHitLast);
	}

	// Logging and timing
	if(lTimerComplete)
	{
		S64 lTimerClose,lTimerIncomplete;
		GET_CPU_TICKS_64(lTimerClose);
		
		lTimerComplete = lTimerClose - lTimerComplete;
		lTimerIncomplete = lTimerComplete-lTimerExcludeAccum;
		
		if(g_uiCombatDebugPerf)
		{
			combatdebug_PerfTrack(pdef->pchName,1,lTimerIncomplete,lTimerComplete);
		}

#ifdef GAMESERVER
		if (gConf.bLogEncounterSummary && pact && pdef->eType!=kPowerType_Passive )
		{
			gslEncounterLog_AddPowerActivation(entGetRef(pentRealSource), pdef);
		}

		if (gbEnablePowersDataLogging)
		{
			if(pentRealSource)
			{
				entLog(LOG_COMBAT,pentRealSource,"PowerApplyPerf","Power %s, Target \"%s\", Ticks %lld, TicksComplete %lld",pdef->pchName,papp->pentTargetEff ? ENTDEBUGNAME(papp->pentTargetEff) : "NULL",lTimerIncomplete,lTimerComplete);		
			}
			else
			{
				objLog(LOG_COMBAT,GLOBALTYPE_ENTITY,0,0,"",&papp->vecSourcePos,NULL,"PowerApplyPerf",NULL,"Power %s, Target \"%s\", Ticks %lld, TicksComplete %lld",pdef->pchName,papp->pentTargetEff ? ENTDEBUGNAME(papp->pentTargetEff) : "NULL",lTimerIncomplete,lTimerComplete);
			}
		}
#endif
	}
	else
	{
		// No longer do the individual power log by 
#ifdef GAMESERVER
		if (gConf.bLogEncounterSummary && pact && pdef->eType!=kPowerType_Passive )
		{
			gslEncounterLog_AddPowerActivation(entGetRef(pentRealSource), pdef);
		}	

/*		if(pentRealSource)
		{			
			entLog(LOG_COMBAT,pentRealSource,"PowerApply","Power %s, Target %s",pdef->pchName,papp->pentTargetEff ? ENTDEBUGNAME(papp->pentTargetEff) : "NULL");	
		}
		else
		{
			objLog(LOG_COMBAT,GLOBALTYPE_ENTITY,0,0,"",&papp->vecSourcePos,NULL,"PowerApply",NULL,"Power %s, Target %s",pdef->pchName,papp->pentTargetEff ? ENTDEBUGNAME(papp->pentTargetEff) : "NULL");
		}*/
#endif


	}

	PERFINFO_AUTO_STOP();
#endif

	return bApplied;
}


static PAPowerAnimFX *PAPowerAnimFX_Create(void)
{
	return MP_ALLOC(PAPowerAnimFX);
}

static void PAPowerAnimFX_Destroy(PAPowerAnimFX *pafx)
{
	if(pafx) MP_FREE(PAPowerAnimFX,pafx);
}

static void PowerApplicationCleanup(SA_PARAM_NN_VALID PowerApplication *papp)
{
	eaDestroyEx(&papp->ppafxEnhancements,PAPowerAnimFX_Destroy);
	eaDestroy(&papp->pppowEnhancements);
	eaDestroy(&papp->ppEquippedItems);
}

// Causes the character to apply all powers from the item. This treats the powers as unowned.
void character_ApplyUnownedPowersFromItem(int iPartitionIdx,
										  Character *pchar, 
										  Item *pItem,
										  GameAccountDataExtract *pExtract)
{
	int i, iNumPowers = item_GetNumItemPowerDefs(pItem, true);

	for(i=0; i<iNumPowers; i++)
	{
		PowerDef *pPowerDef = item_GetPowerDef(pItem, i);

		if (pPowerDef)
		{
			ApplyUnownedPowerDefParams	applyParams = {0};

			applyParams.erTarget = entGetRef(pchar->pEntParent);
			applyParams.pclass = character_GetClassCurrent(pchar);
			applyParams.iLevel = item_GetLevel(pItem);
			applyParams.fTableScale = 1.f;
			applyParams.erModOwner = entGetRef(pchar->pEntParent);
			applyParams.pExtract = pExtract;

			character_ApplyUnownedPowerDef(iPartitionIdx, pchar, pPowerDef, &applyParams);
		}
	}
}

// helper function for character_ApplyPower for getting the owner entityRef for an application
static EntityRef character_ApplyPowerGetModOwnerHelper(int iPartitionIdx, Character *pchar)
{
	S32 iSanity = 0;
	
	do {
		Entity *eOwner;
		if (!pchar->bModsOwnedByOwner || !pchar->pEntParent->erOwner)
		{
			return entGetRef(pchar->pEntParent);
		}

		eOwner = entFromEntityRef(iPartitionIdx, pchar->pEntParent->erOwner);
		if (eOwner && eOwner->pChar)
		{
			pchar = eOwner->pChar;
		}
		else 
			return entGetRef(pchar->pEntParent);
	
	} while (++iSanity < 3);

	return entGetRef(pchar->pEntParent);
}

// Causes the character to apply the given power at the given target
bool character_ApplyPower(int iPartitionIdx,
						  Character *pchar,
						  Power *ppow,
						  PowerActivation *pact,
						  EntityRef erTarget,
						  Vec3 vecTarget,
						  Vec3 vecTargetSecondary,
						  S32 bPlayActivate,
						  S32 iFramesBeforeHitAdjust,
						  GameAccountDataExtract *pExtract,
						  ExprContext **ppOutContext)
{
	PowerApplication app = {0};
	bool bApplied = false;

	PERFINFO_AUTO_START_FUNC();

	// First, the one thing we must have
	app.pdef = GET_REF(ppow->hDef);
	if(verify(app.pdef))
	{
		int i;

		app.uiApplyID = powerapp_NextID();
		app.ppow = ppow;

		// Fill in the Activation's refs to Enhancements, save the actual pointers
		//  for the Application.  Do the same with the PowerAnimFX.
		if(app.pdef->eType != kPowerType_Instant)
		{
			character_ActRefEnhancements(iPartitionIdx,pchar,pact,&app.pppowEnhancements);
			character_ActRefAnimFX(pchar,pact);
		}

#ifdef GAMESERVER
		// Use a charge on each Enhancement (using the charge of the main power happens earlier)
		for(i=eaSize(&app.pppowEnhancements)-1; i>=0; i--)
		{
			character_PowerUseCharge(pchar, app.pppowEnhancements[i]);
		}
#endif

		app.pcharSource = pchar;
		app.iPartitionIdx = iPartitionIdx;
		entGetCombatPosDir(pchar->pEntParent,pact,app.vecSourcePos,app.vecSourceDir);
		
		entGetActivationSourcePosDir(pchar->pEntParent, pact, app.pdef, app.vecSourcePos, app.vecSourceDir);
		if (app.pdef->bHasEffectAreaPositionOffsets)
		{
			app.bOffsetEffectArea = true;
		}
				

		if(pchar->ppApplyStrengths && pchar->pEntParent->erOwner)
		{
			Entity *eOwner = entFromEntityRef(iPartitionIdx, pchar->pEntParent->erOwner);
			if(eOwner && eOwner->pChar)
			{
				app.pclass = character_GetClassCurrent(eOwner->pChar);
			}
		}
		if(!app.pclass)
		{
			app.pclass = character_GetClassCurrent(pchar);
		}
		app.iLevel = entity_GetCombatLevel(pchar->pEntParent);
#ifdef GAMESERVER
		if(app.pdef->bWeaponBased)
		{
			// Add all the items related to this power application
			character_WeaponPick(pchar, app.pdef, &app.ppEquippedItems, pExtract);
		}
#endif
		app.ppStrengths = pchar->ppApplyStrengths;
		app.iIdxMulti = ppow->iIdxMultiTable;
		app.fTableScale = ppow->fTableScale;
		app.erModSource = entGetRef(pchar->pEntParent);
		
		app.erModOwner = character_ApplyPowerGetModOwnerHelper(iPartitionIdx, pchar);
		
		app.pcharSourceTargetType = app.pcharSource;

		app.erTarget = erTarget;
		// app.pppvolTarget = NULL;
		copyVec3(vecTarget,app.vecTarget);
		if (vecTargetSecondary && !ISZEROVEC3(vecTargetSecondary))
			copyVec3(vecTargetSecondary, app.vecTargetSecondary);
		app.pSubtarget = (pchar->pSubtarget && !powerdef_IgnoresAttrib(app.pdef,kAttribType_SubtargetSet)) ? StructClone(parse_PowerSubtargetChoice,pchar->pSubtarget) : NULL;
		if(app.pSubtarget)
			app.pSubtarget->fAccuracy += character_PowerBasicAttrib(iPartitionIdx,pchar,ppow,kAttribType_SubtargetAccuracy,0);

		app.pact = pact;
		app.uiRandomSeedActivation = pact->uiRandomSeed;
		app.uiSeedSBLORN = PowerActSBLORNSeedForApply(pact);
		app.pafx = GET_REF(app.pdef->hFX);
		app.fHue = powerapp_GetHue(pchar,ppow,pact,app.pdef);
		if(app.pafx)
		{
			int s=eaSize(&app.pppowEnhancements);
			for(i=0; i<s; i++)
			{
				PowerDef *pdef = GET_REF(app.pppowEnhancements[i]->hDef);
				PowerAnimFX *pafx = pdef ? GET_REF(pdef->hFX) : NULL;
				if(pafx)
				{
					PAPowerAnimFX *pafxWrap = PAPowerAnimFX_Create();
					pafxWrap->pafx = pafx;
					pafxWrap->fHue = powerapp_GetHue(pchar,app.pppowEnhancements[i],pact,pdef);
					if(!pafxWrap->fHue) pafxWrap->fHue = app.fHue;
					eaPush(&app.ppafxEnhancements,pafxWrap);
				}
			}
		}
		app.uiTimestampAnim = pact->uiPeriod ? pact->uiTimestampActivatePeriodic : pact->uiTimestampActivate;
#ifdef GAMESERVER
		if (s_iPowersAEClientViewEnabled && !g_CombatConfig.bPowersAEClientViewDisabled)
		{	// offset the client view time to the time when this will need to get apply targets 
			F32 fSpeed = character_GetSpeedPeriod(iPartitionIdx, pchar, ppow);
			app.uiTimestampClientView = pact->uiTimestampCurrented + (app.pdef->fTimePreactivate + pact->fTimeChargedTotal + 
										pact->fTimeActivating + (pact->fTimerActivate / fSpeed)) * MM_PROCESS_COUNTS_PER_SECOND;

			if (!entValidateClientViewTimestamp(app.uiTimestampClientView))
			{	// pre-check the timestamp and throw it out if it's already invalid
				app.uiTimestampClientView = 0;
			}
		}
#endif
		app.uiPeriod = pact->uiPeriod;
		app.uiActID = pact->uchID ? pact->uchID : power_AnimFXID(ppow);
		app.uiActSubID = pact->uiPeriod;
		app.iSrcItemID = ppow->pSourceItem ? ppow->pSourceItem->id : 0;
		app.bAnimActivate = bPlayActivate;
		app.iFramesBeforeHitAdjust = iFramesBeforeHitAdjust;
		app.bPredict = !!entCheckFlag(pchar->pEntParent,ENTITYFLAG_IS_PLAYER);
		app.bSelfOnce = app.pdef->bSelfOnce;
		app.bMissMods = app.pdef->bMissMods;
		{
			int s=eaSize(&app.pppowEnhancements);
			for(i=0; i<s; i++)
			{
				PowerDef *pdef = GET_REF(app.pppowEnhancements[i]->hDef);
				if(pdef)
				{
					app.bSelfOnce |= pdef->bSelfOnce;
					app.bMissMods |= pdef->bMissMods;
				}
			}
		}
		app.bLevelAdjusting = pchar->bLevelAdjusting;
		app.erProximityAssistTarget = pact->erProximityAssistTarget;

		bApplied = ApplyPowerEx(iPartitionIdx,&app,ppOutContext,false);

		// Note if we hit any targets
		if(!pact->bHitTargets && app.iNumTargetsHit)
			pact->bHitTargets = true;
				
		// Cleanup
		PowerApplicationCleanup(&app);
	}

	PERFINFO_AUTO_STOP();

	return bApplied;
}

// Causes an application of the given power at the given target, from a location
void location_ApplyPowerDef(Vec3 vecSource,
							int iPartitionIdx,
							PowerDef *pdef,
							EntityRef erTarget,
							Vec3 vecTarget,
							WorldVolume*** pppvolTarget,
							Character *pcharSourceTargetType,
							CharacterClass *pclass,
							int iLevel,
							EntityRef erModOwner)
{
#if !GAMESERVER && !GAMECLIENT
	assert(0);
#else
	static ExprContext *s_pContext = NULL;

	PowerApplication app = {0};

	PERFINFO_AUTO_START_FUNC();

	// Set up statics
	if(!s_pContext)
	{
		s_pContext = exprContextCreate();
	}

	app.pdef = pdef;
	app.uiApplyID = powerapp_NextID();
	// app.ppow = NULL;
	// app.pppowEnhancements = NULL;

	// app.pcharSource = NULL;
	app.iPartitionIdx = iPartitionIdx;
	copyVec3(vecSource,app.vecSourcePos);
	// app.vecSourceDir;
	app.pclass = pclass;
	app.iLevel = MAX(1,iLevel);
	// app.pitem = NULL;
	// app.ppStrengths = NULL;
	// app.iIdxMulti = 0;
	app.fTableScale = 1.f;
	// app.erModSource = 0;
	app.erModOwner = erModOwner;
	app.pcharSourceTargetType = pcharSourceTargetType ? pcharSourceTargetType : app.pcharSource;

	app.erTarget = erTarget;
	app.pppvolTarget = pppvolTarget;
	if(vecTarget)
	{
		copyVec3(vecTarget,app.vecTarget);
	}
	else if(erTarget)
	{
		Entity *e = entFromEntityRef(iPartitionIdx, erTarget);
		if(e)
		{
			entGetCombatPosDir(e,NULL,app.vecTarget,NULL);
		}
	}
	// app.pSubtarget = NULL;

	// app.pact = NULL;
	// app.uiRandomSeedActivation = 0;
	app.uiSeedSBLORN = randomU32();
	app.pafx = GET_REF(app.pdef->hFX);
	app.fHue = powerapp_GetHue(NULL,NULL,NULL,app.pdef);
	app.uiTimestampAnim = pmTimestamp(0);
	app.uiActID = poweract_NextID();
	// app.uiPeriod = 0;
	// app.uiActSubID = 0;
	app.bAnimActivate = true;
	// app.iFramesBeforeHitAdjust = 0;
	// app.bPredict = false;
	// app.bSelfOnce = false;
	// app.bMissMods = false;
	// app.bLevelAdjusting = false;

	ApplyPowerEx(iPartitionIdx,&app,&s_pContext,false);

	// Cleanup
	PowerApplicationCleanup(&app);
	exprContextClear(s_pContext);

	PERFINFO_AUTO_STOP();
#endif
}


// Causes the character to apply the given power def to the target.  Requires a bunch of specific additional data.
void character_ApplyUnownedPowerDef(int iPartitionIdx,
									Character *pchar,
									PowerDef *pdef,
									const ApplyUnownedPowerDefParams *pParams)
{
#if !GAMESERVER && !GAMECLIENT
	assert(0);
#else
	PowerApplication app = {0};

	Entity *pOwnerBaseEnt = pParams->erModOwner ? entFromEntityRef(iPartitionIdx, pParams->erModOwner) : NULL;
	Character *pcharOwner = pOwnerBaseEnt ? pOwnerBaseEnt->pChar : NULL;

	PERFINFO_AUTO_START_FUNC();

	app.pdef = pdef;
	app.pmodEvent = pParams->pmod;

	if (pParams->uiApplyID && !app.pdef->bGenerateUniqueApplyID)
		app.uiApplyID = pParams->uiApplyID;
	else 
		app.uiApplyID = powerapp_NextID();
	
	if (pParams->pppowEnhancements && eaSize(&pParams->pppowEnhancements))
	{
		eaCopy(&app.pppowEnhancements, &pParams->pppowEnhancements);
	}

	app.pcharSource = pchar;
	app.iPartitionIdx = entGetPartitionIdx(pchar->pEntParent);
	entGetCombatPosDir(pchar->pEntParent, NULL, app.vecSourcePos, app.vecSourceDir);
		
	entGetActivationSourcePosDir(pchar->pEntParent, NULL, app.pdef, app.vecSourcePos, app.vecSourceDir);
	if (app.pdef->bHasEffectAreaPositionOffsets)
	{
		app.bOffsetEffectArea = true;
	}

	app.pclass = pParams->pclass;
	app.iLevel = pParams->iLevel;
#ifdef GAMESERVER
	if(app.pdef->bWeaponBased)
	{
		character_WeaponPick(	((pParams->pCharWeaponPicker == NULL) ? pchar : pParams->pCharWeaponPicker), 
								app.pdef, 
								&app.ppEquippedItems, 
								pParams->pExtract);
	}
#endif
	app.ppStrengths = pParams->ppStrengths;
	app.iIdxMulti = pParams->iIdxMulti;
	app.fTableScale = pParams->fTableScale;
	app.iSrcItemID = pParams->iSrcItemID;
	app.erModSource = entGetRef(pchar->pEntParent);
	app.erModOwner = pParams->erModOwner ? pParams->erModOwner : character_ApplyPowerGetModOwnerHelper(iPartitionIdx, pchar);
	app.pcharSourceTargetType = pParams->pcharSourceTargetType ? pParams->pcharSourceTargetType : app.pcharSource;

	app.erTarget = pParams->erTarget;
	// app.pppvolTarget = NULL;
	if(pParams->pVecTarget)
	{
		copyVec3(pParams->pVecTarget, app.vecTarget);
	}

	if (pParams->pSubtarget && !powerdef_IgnoresAttrib(app.pdef,kAttribType_SubtargetSet))
	{
		app.pSubtarget =  StructClone(parse_PowerSubtargetChoice, pParams->pSubtarget);
	}

	if(pParams->pCritical) 
	{
		StructCopyFields(parse_PACritical, pParams->pCritical, &app.critical, 0, 0);
	}

	if(pParams->pTrigger)
	{
		StructCopyFields(parse_PATrigger, pParams->pTrigger, &app.trigger, 0, 0);
		app.trigger.pCombatEventTracker = pParams->pTrigger->pCombatEventTracker;
	}
	
	app.uiSeedSBLORN = randomU32();
	app.pafx = GET_REF(pdef->hFX);
	app.fHue = pParams->fHue;
	app.uiTimestampAnim = pmTimestamp(0);
	app.uiActID = poweract_NextID();
	app.bAnimActivate = true;
	app.bSelfOnce = app.pdef->bSelfOnce;
	app.bMissMods = app.pdef->bMissMods;
	app.bLevelAdjusting = pParams->bLevelAdjusting;
	
	app.bCountModsAsPostApplied = pParams->bCountModsAsPostApplied;

	ApplyPowerEx(iPartitionIdx, &app, NULL, pParams->bEvalHitChanceWithoutPower);

	// Cleanup
	PowerApplicationCleanup(&app);

	PERFINFO_AUTO_STOP();
#endif
}

// Causes the Character to apply the given PowerDef to themselves with basic parameters.
void character_ApplyUnownedPowerDefToSelf(int iPartitionIdx, Character *pchar, PowerDef *pdef, GameAccountDataExtract *pExtract)
{
	ApplyUnownedPowerDefParams applyParams = {0};

	applyParams.erTarget = entGetRef(pchar->pEntParent);
	applyParams.pclass = character_GetClassCurrent(pchar);
	applyParams.iLevel = entity_GetCombatLevel(pchar->pEntParent);
	applyParams.fTableScale = 1.f;
	applyParams.erModOwner = entGetRef(pchar->pEntParent);
	applyParams.pExtract = pExtract;

	character_ApplyUnownedPowerDef(iPartitionIdx, pchar, pdef, &applyParams);
}

// Causes the Character to take appropriate falling damage based on impact speed
void character_ApplyFalling(Character *pchar, F32 fImpactSpeed)
{
	PERFINFO_AUTO_START_FUNC();
	fImpactSpeed -= g_CombatConfig.fFallDamageSpeedThreshold + MAX(0,1.5 * (pchar->pattrBasic->fHeightJumping-10));
	if(fImpactSpeed > 0)
	{
		PERFINFO_AUTO_START("Falling",1);
		if(g_CombatConfig.bFallingDamage
			&& !pchar->bInvulnerable && !pchar->bUsingDoor
			&& (entIsAlive(pchar->pEntParent) || pchar->pattrBasic->fHitPoints > 0)
			&& !encounter_IsDamageDisabled(entGetPartitionIdx(pchar->pEntParent))
			&& (!pchar->pEntParent->pCritter || !critterRankIgnoresFallingDamage(pchar->pEntParent->pCritter->pcRank)))
		{
			PowerDef *pFallingDamagePowerDef;
			if (g_CombatConfig.pchFallingDamagePower &&
				g_CombatConfig.pchFallingDamagePower[0])
			{
				if (pFallingDamagePowerDef = powerdef_Find(g_CombatConfig.pchFallingDamagePower))
				{
					Vec3 vDamagePos;				
					Entity *pEnt = pchar->pEntParent;

					// Get the damage position
					entGetPos(pEnt, vDamagePos);

					// Store the last falling impact speed on the character
					pchar->fLastFallingImpactSpeed = g_CombatConfig.fFallDamageScale * fImpactSpeed;

					// Apply the power to the entity
					location_ApplyPowerDef(vDamagePos, 
						entGetPartitionIdx(pEnt), 
						pFallingDamagePowerDef, 
						entGetRef(pEnt), 
						NULL, 
						NULL, 
						NULL, 
						character_GetClassCurrent(pchar),
						entity_GetCombatLevel(pEnt),
						0);
				}
			}
			else
			{
				// Apply the usual falling damage algorithm
				DamageTracker *pTracker;
				F32 fDamage = g_CombatConfig.fFallDamageScale * fImpactSpeed * pchar->pattrBasic->fHitPointsMax / 100.f;

				// fall damage can't kill you (if you're a player)
				if(!g_CombatConfig.bFallingDamageIsFatal && !pchar->pEntParent->pCritter)
					MIN1(fDamage,pchar->pattrBasic->fHitPoints-1);

				if(fDamage > 0)
				{
					pchar->pattrBasic->fHitPoints -= fDamage;
					entity_SetDirtyBit(pchar->pEntParent,parse_CharacterAttribs,pchar->pattrBasic,0);
					entity_SetDirtyBit(pchar->pEntParent,parse_Character,pchar,0);
					pTracker = damageTracker_AddTick(entGetPartitionIdx(pchar->pEntParent),pchar,0,0,0,fDamage,fDamage,kAttribType_HitPoints,0,NULL,0,NULL,0);
					pTracker->pchDisplayNameKey = "AutoDesc.CombatEvent.Falling";
					character_Wake(pchar);
				}
			}
		}
		PERFINFO_AUTO_STOP(); // Falling
	}
	PERFINFO_AUTO_STOP();
}

// Causes the character to place the given power in their queue and deal with other activation work
SA_RET_NN_VALID static PowerActivation *CharacterQueuePower(int iPartitionIdx,
														SA_PARAM_NN_VALID Character *pchar,
														Entity *eTarget,
														WorldInteractionNode *pObject,
														const Vec3 vecSourceDirection,
														const Vec3 vecTarget,
														const Vec3 vecTargetSecondary,
														Power *ppow,
														PowerDef *pdef,
														U32 uiTimeQueue,
														U32 uiTimeEnterStance,
														bool bAnimateNow,
														U8 uchID,
														int iPredictedIdx,
														S32 iLungePrediction,
														U32 **ppuiCanceledIDsInOut)
{
#if !GAMESERVER && !GAMECLIENT
	assert(0);
#else
	PowerActivation *pact;
	S32 bOverflow = false;
	Entity *pent = pchar->pEntParent;
	PowerTarget *ppowTarget = GET_REF(pdef->hTargetMain);
	PowerAnimFX *pafx = GET_REF(pdef->hFX);
	U32 uiTimeActivate = uiTimeEnterStance;
	F32 fTimeRemainInQueue = 0.0f;
	
	S32 iCanceledIDs = ea32Size(ppuiCanceledIDsInOut);
	U8 uchCanceledIDIn0 = iCanceledIDs > 0 ? (*ppuiCanceledIDsInOut)[0] : 0;
	U8 uchCanceledIDIn1 = iCanceledIDs > 1 ? (*ppuiCanceledIDsInOut)[1] : 0;

	// Bunch of data for debugging
	S32 iQueueDebugFlags = 0;
	ChargeMode eChargeModeEntry = pchar->eChargeMode;
	PowerDef *pdefCurrent = pchar->pPowActCurrent ? GET_REF(pchar->pPowActCurrent->hdef) : NULL;
	U8 uchCurrent = pchar->pPowActCurrent ? pchar->pPowActCurrent->uchID : 0;
	PowerDef *pdefQueued = pchar->pPowActQueued ? GET_REF(pchar->pPowActQueued->hdef) : NULL;
	U8 uchQueued = pchar->pPowActQueued ? pchar->pPowActQueued->uchID : 0;
	PowerDef *pdefOverflow = pchar->pPowActOverflow ? GET_REF(pchar->pPowActOverflow->hdef) : NULL;
	U8 uchOverflow = pchar->pPowActOverflow ? pchar->pPowActOverflow->uchID : 0;

#define SetQueueDebugFlag(i) iQueueDebugFlags |= (1<<(i))

	devassert(pdef==GET_REF(ppow->hDef));
	devassert(iCanceledIDs <= 2);

	// Stop interaction and pathing if they used a power
#ifdef GAMESERVER
	if( pent && pent->pPlayer &&
		(interaction_IsPlayerInteracting(pent) || interaction_IsPlayerInDialog(pent)) &&
		(pent->pPlayer->InteractStatus.bInteractBreakOnPower || pent->pPlayer->InteractStatus.fTimerInteract <= 0))
	{
		im_InteractDestroyPathing(pent);
		interaction_EndInteractionAndDialog(entGetPartitionIdx(pent), pent, false, true, true);
	}
#endif

	if(pdef->eType != kPowerType_Instant)
	{
		if(entIsServer())
		{
			if(pchar->pPowActOverflow)
			{
				// Overflow is always canceled if unpredicted, otherwise it's canceled if the client tried
				//  to cancel it
				if(pchar->pPowActOverflow->bUnpredicted)
				{
					character_ActOverflowCancel(iPartitionIdx,pchar,pdef,0);
					SetQueueDebugFlag(0);
				}
				else if(iCanceledIDs)
				{
					if(uchCanceledIDIn0 && character_ActOverflowCancel(iPartitionIdx,pchar,pdef,uchCanceledIDIn0))
						SetQueueDebugFlag(1);
					else if(uchCanceledIDIn1 && character_ActOverflowCancel(iPartitionIdx,pchar,pdef,uchCanceledIDIn1))
						SetQueueDebugFlag(2);
				}
			}

			if(pchar->pPowActQueued)
			{
				// Queued is canceled if unpredicted and not committed, otherwise it's canceled if
				//  the client tried to cancel it
				if(pchar->pPowActQueued->bUnpredicted && !pchar->pPowActQueued->bCommit)
				{
					character_ActQueuedCancel(iPartitionIdx,pchar,pdef,0);
					SetQueueDebugFlag(3);
				}
				else if(iCanceledIDs)
				{
					if(uchCanceledIDIn0 && character_ActQueuedCancel(iPartitionIdx,pchar,pdef,uchCanceledIDIn0))
						SetQueueDebugFlag(4);
					else if(uchCanceledIDIn1 && character_ActQueuedCancel(iPartitionIdx,pchar,pdef,uchCanceledIDIn1))
						SetQueueDebugFlag(5);
				}
			}

			if(pchar->pPowActQueued)
			{
				// Still have something queued, so this goes into the overflow
				bOverflow = true;
				SetQueueDebugFlag(6);
			}
			else
			{
				// Didn't have anything queued, but we'd better not have anything in
				//  overflow or something has gone terribly wrong (e.g. the queued
				//  activation was cancelled but the overflow wasn't, and the overflow
				//  wasn't pushed forward).
				ErrorDetailsf("%s; CM %d %d; QDF %d; CID %d %d; %s %d; %s %d; %s %d; %d %d %d",
					pdef->pchName,eChargeModeEntry,pchar->eChargeMode,iQueueDebugFlags,uchCanceledIDIn0,uchCanceledIDIn1,
					pdefCurrent ? pdefCurrent->pchName : "", uchCurrent,
					pdefQueued ? pdefQueued->pchName : "", uchQueued,
					pdefOverflow ? pdefOverflow->pchName : "", uchOverflow,
					pchar->pPowActCurrent?pchar->pPowActCurrent->uchID:0,
					pchar->pPowActQueued?pchar->pPowActQueued->uchID:0,
					pchar->pPowActOverflow?pchar->pPowActOverflow->uchID:0);

				if(!verify(pchar->pPowActOverflow==NULL))
				{
					character_ActOverflowCancel(iPartitionIdx,pchar,pdef,0);
					SetQueueDebugFlag(7);
				}
			}
		}
		else
		{
			// Client attempts to cancel both queued and overflow, unless they are committed
			if(pchar->pPowActQueued && !pchar->pPowActQueued->bCommit)
			{
				U8 uchCanceledID = character_ActQueuedCancel(iPartitionIdx,pchar,pdef,0);
				if(uchCanceledID)
					ea32Push(ppuiCanceledIDsInOut,uchCanceledID);
			}

			if(pchar->pPowActOverflow && !pchar->pPowActOverflow->bCommit)
			{
				U8 uchCanceledID = character_ActOverflowCancel(iPartitionIdx,pchar,pdef,0);
				if(uchCanceledID)
					ea32Push(ppuiCanceledIDsInOut,uchCanceledID);
			}

			// Check to make sure things are sane, which basically means nothing in overflow
			devassert(pchar->pPowActOverflow==NULL);

			// If after attempting to clear the queue, there is still something queued, we'll
			//  let the client use the overflow, but this really should happen almost never.
			if(pchar->pPowActQueued)
				bOverflow = true;
		}

		// Verify that the attempts to cancel what is going to get replaced is successful, and if not
		//  blow them away.
		if(bOverflow)
		{
			if(pchar->pPowActOverflow)
			{
				PowersDebugPrintEnt(EPowerDebugFlags_ACTIVATE, pchar->pEntParent, "Queue %d: Queued into overflow queue without clearing overflow\n",uchID);
				character_ActOverflowCancel(iPartitionIdx,pchar,pdef,0);
				SetQueueDebugFlag(8);
			}
		}
		else
		{
			if(pchar->pPowActQueued)
			{
				PowersDebugPrintEnt(EPowerDebugFlags_ACTIVATE, pchar->pEntParent, "Queue %d: Queued into queue without clearing\n",uchID);
				character_ActQueuedCancel(iPartitionIdx,pchar,pdef,0);
				SetQueueDebugFlag(9);
			}
		}


		if(bOverflow && pchar->pPowActQueued && pchar->eChargeMode!=kChargeMode_None)
		{
			U8 uchIDOld = pchar->pPowActQueued->uchID;
			ChargeMode eChargeModeOld = pchar->eChargeMode;
			PowersDebugPrintEnt(EPowerDebugFlags_ACTIVATE, pchar->pEntParent, "Deactivate %d: Overflowing new power, queued was not cancelled\n", uchIDOld);
			character_ActDeactivate(iPartitionIdx,pchar,&uchIDOld,&eChargeModeOld,uiTimeQueue,uiTimeQueue,true);
			SetQueueDebugFlag(10);
		}

		if(pchar->pPowActCurrent && pchar->eChargeMode!=kChargeMode_None)
		{
			U8 uchIDOld = pchar->pPowActCurrent->uchID;
			ChargeMode eChargeModeOld = pchar->eChargeMode;
			PowersDebugPrintEnt(EPowerDebugFlags_ACTIVATE, pchar->pEntParent, "Deactivate %d: Queuing new power, current was not deactivated\n",uchIDOld);
			character_ActDeactivate(iPartitionIdx,pchar,&uchIDOld,&eChargeModeOld,uiTimeQueue,uiTimeQueue,true);
			SetQueueDebugFlag(11);
		}
	}

	pact = poweract_Create();
	if(eTarget)
	{
		WorldInteractionNode *pNode;
		pact->erTarget = entGetRef(eTarget);
		if(pNode = GET_REF(eTarget->hCreatorNode))
		{
			character_FindNearestPointForObject(pchar,NULL,pNode,pact->vecTarget,true);
		}
		else if (eTarget != pchar->pEntParent)
		{
			entGetPos(eTarget, pact->vecTarget);
		}
	}
	else if(pObject)
	{
		SET_HANDLE_FROM_REFERENT(INTERACTION_DICTIONARY, pObject, pact->hTargetObject);
		character_FindNearestPointForObject(pchar,NULL,pObject,pact->vecTarget,true);
		//wlInteractionNodeGetWorldMid(pObject,pact->vecTarget);
	}
	else if(vecTarget && pent && entGetType(pent)==GLOBALTYPE_ENTITYCRITTER)
	{
		copyVec3(vecTarget,pact->vecTarget);
	}
	
	if(vecTarget && 
		((pchar->bUseCameraTargeting && 
		(!ppowTarget || !ppowTarget->bDoNotTargetUnlessRequired)) || 
		!character_PowerRequiresValidTarget(pchar, pdef)) && 
		(!eTarget || eTarget==pent) && !pObject)
	{
		copyVec3(vecTarget,pact->vecTarget);
		pact->bRange = true;
	}

	if (vecTargetSecondary && !ISZEROVEC3(vecTargetSecondary))
	{
		copyVec3(vecTargetSecondary, pact->vecTargetSecondary);
	}

	if (pent)
	{
		entGetPos(pent,pact->vecCharStartPos);
	}

	if (pafx)
	{
		if (pafx->fStanceTransitionTime > 0.0f)
		{
			S32 bDisableStanceTransitions = false;

			if (g_CombatConfig.tactical.aim.bNoStanceTransitionsWhileAiming)
			{
				S32 bIsAiming = false;
				if (mrTacticalGetAimState(SAFE_MEMBER(pent, mm.mrTactical), &bIsAiming, NULL) && bIsAiming)
				{
					bDisableStanceTransitions = true;
				}
			}
			if (!bDisableStanceTransitions)
			{
				PowerDef *pdefStanceCurrent = pchar->pPowerRefStance ? GET_REF(pchar->pPowerRefStance->hdef) : NULL;
				PowerAnimFX *pafxStanceCurrent = pdefStanceCurrent ? GET_REF(pdefStanceCurrent->hFX) : NULL;
				if (!pdefStanceCurrent || pafxStanceCurrent->uiStanceID != pafx->uiStanceID)
				{
					fTimeRemainInQueue = pafx->fStanceTransitionTime;
					uiTimeActivate = pmTimestampFrom(uiTimeEnterStance, pafx->fStanceTransitionTime);
				}
			}
		}
		
		if (eaiSize(&pafx->piFramesBeforeHit))
		{
			pact->fActHitTime = pafx->piFramesBeforeHit[0]/PAFX_FPS;
		}
	}


	pact->fTimeFinished = 0.f;
	pact->fTimerRemainInQueue = fTimeRemainInQueue;
	pact->uiTimestampQueued = uiTimeQueue;
	pact->uiTimestampCurrented = uiTimeActivate;
	pact->uiTimestampEnterStance = uiTimeEnterStance;
	pact->uiTimestampActivate = bAnimateNow ? uiTimeQueue : uiTimeActivate;
	pact->uchID = uchID;
	pact->uiIDServer = poweract_NextIDServer();
	pact->iPredictedIdx = iPredictedIdx;
	pact->uiRandomSeed = randomU32();
	poweract_SetPower(pact,ppow);

	// Get the minimum charge time from the parent
	pact->fTimeChargeRequiredCombo = 0.0f;
	if(ppow->pParentPower && pdef->fTimeCharge)
	{
		PowerDef *pParentDef = GET_REF(ppow->pParentPower->hDef);
		if(pParentDef)
		{
			int i;
			for(i=eaSize(&pParentDef->ppOrderedCombos)-1; i>=0; i--)
			{
				if(GET_REF(pParentDef->ppOrderedCombos[i]->hPower)==pdef)
					break;
			}
			if(i>=0)
				pact->fTimeChargeRequiredCombo = pdef->fTimeCharge * pParentDef->ppOrderedCombos[i]->fPercentChargeRequired;
		}
	}

	pact->fTimeChargeRequired = pdef->fTimeCharge * pdef->fChargeRequire;

	if(pdef->eTracking==kTargetTracking_UntilCurrent)
	{
		pact->bChargeAtVecTarget = true;
	}

	// Perform adjustments to timing for lunging now, if it doesn't charge
	if(pdef->fTimeCharge==0.0f)
	{
		character_ActLungeInit(iPartitionIdx,pchar,pact,pact->uiTimestampActivate,iLungePrediction);
		poweract_GrabInit(pact);
	}

	ErrorDetailsf("%s; CM %d %d; QDF %d; CID %d %d; %s %d; %s %d; %s %d; %d %d %d",
		pdef->pchName,eChargeModeEntry,pchar->eChargeMode,iQueueDebugFlags,uchCanceledIDIn0,uchCanceledIDIn1,
		pdefCurrent ? pdefCurrent->pchName : "", uchCurrent,
		pdefQueued ? pdefQueued->pchName : "", uchQueued,
		pdefOverflow ? pdefOverflow->pchName : "", uchOverflow,
		pchar->pPowActCurrent?pchar->pPowActCurrent->uchID:0,
		pchar->pPowActQueued?pchar->pPowActQueued->uchID:0,
		pchar->pPowActOverflow?pchar->pPowActOverflow->uchID:0);

	// Queue it up and set charge mode
	if(pdef->eType == kPowerType_Instant)
	{
		eaPush(&pchar->ppPowerActInstant,pact);
	}
	else
	{
		if(bOverflow)
		{
			if(!verify(pchar->pPowActOverflow==NULL))
			{
				poweract_DestroySafe(&pchar->pPowActOverflow);
				SetQueueDebugFlag(12);
			}
			pchar->pPowActOverflow = pact;

			ErrorDetailsf("%s; CM %d %d; QDF %d; CID %d %d; %s %d; %s %d; %s %d; %d %d %d",
				pdef->pchName,eChargeModeEntry,pchar->eChargeMode,iQueueDebugFlags,uchCanceledIDIn0,uchCanceledIDIn1,
				pdefCurrent ? pdefCurrent->pchName : "", uchCurrent,
				pdefQueued ? pdefQueued->pchName : "", uchQueued,
				pdefOverflow ? pdefOverflow->pchName : "", uchOverflow,
				pchar->pPowActCurrent?pchar->pPowActCurrent->uchID:0,
				pchar->pPowActQueued?pchar->pPowActQueued->uchID:0,
				pchar->pPowActOverflow?pchar->pPowActOverflow->uchID:0);

			if(pdef->fTimeCharge > 0.0f)
			{
				devassert(pchar->eChargeMode==kChargeMode_None);
				pchar->eChargeMode = kChargeMode_Overflow;
			}
			else if(pdef->eType==kPowerType_Maintained)
			{
				devassert(pchar->eChargeMode==kChargeMode_None);
				pchar->eChargeMode = kChargeMode_OverflowMaintain;
			}

			if(pent) PowersDebugPrintEnt(EPowerDebugFlags_ACTIVATE, pchar->pEntParent, "Queue %d: Queued into overflow queue targeting %d\n",uchID,pact->erTarget);
		}
		else
		{
			if(!verify(pchar->pPowActQueued==NULL))
			{
				poweract_DestroySafe(&pchar->pPowActQueued);
				SetQueueDebugFlag(13);
			}
			pchar->pPowActQueued = pact;

			ErrorDetailsf("%s; CM %d %d; QDF %d; CID %d %d; %s %d; %s %d; %s %d; %d %d %d",
				pdef->pchName,eChargeModeEntry,pchar->eChargeMode,iQueueDebugFlags,uchCanceledIDIn0,uchCanceledIDIn1,
				pdefCurrent ? pdefCurrent->pchName : "", uchCurrent,
				pdefQueued ? pdefQueued->pchName : "", uchQueued,
				pdefOverflow ? pdefOverflow->pchName : "", uchOverflow,
				pchar->pPowActCurrent?pchar->pPowActCurrent->uchID:0,
				pchar->pPowActQueued?pchar->pPowActQueued->uchID:0,
				pchar->pPowActOverflow?pchar->pPowActOverflow->uchID:0);

			if(pdef->fTimeCharge > 0.0f)
			{
				devassert(pchar->eChargeMode==kChargeMode_None);
				pchar->eChargeMode = kChargeMode_Queued;
			}
			else if(pdef->eType==kPowerType_Maintained)
			{
				devassert(pchar->eChargeMode==kChargeMode_None);
				pchar->eChargeMode = kChargeMode_QueuedMaintain;
			}

			if(pent) PowersDebugPrintEnt(EPowerDebugFlags_ACTIVATE, pchar->pEntParent, "Queue %d: Queued into main queue targeting %d\n",uchID,pact->erTarget);
		}
	}
	// Clear the details in case we didn't actually generate an error
	ErrorDetailsf("");

	// Note the Interact if the target is NearDeath, and force the target to face
	if(eTarget && eTarget->pChar && eTarget->pChar->pNearDeath)
	{
		EntityRef er = entGetRef(pchar->pEntParent);
		Character *pcharTarget = eTarget->pChar;
				
		if (critter_IsFactionKOS(iPartitionIdx, eTarget, pchar->pEntParent))
		{
			eaiPushUnique(&pcharTarget->pNearDeath->perHostileInteracts, er);
		}
		else
		{
			eaiPushUnique(&pcharTarget->pNearDeath->perFriendlyInteracts, er);
		}
			
		entity_SetDirtyBit(pcharTarget->pEntParent, parse_Character, pcharTarget, false);
		 
		if (g_CombatConfig.bNearDeathTargetsFaceInteractee && 
			(eaiSize(&pcharTarget->pNearDeath->perFriendlyInteracts) + 
				eaiSize(&pcharTarget->pNearDeath->perHostileInteracts)) == 1)
		{
			PM_CREATE_SAFE(pcharTarget); 
			pmFaceStart(pcharTarget->pPowersMovement, 
						PMOVE_NEARDEATH,
						pact->uiTimestampActivate,
						pmTimestampFrom(pact->uiTimestampActivate,.25f),
						er,
						NULL,
						false, 
						false);
		}
		
	}

	character_Wake(pchar);

	return pact;
#endif
}



void character_confuseSetTarget(Character *p, PowerActivation *pAct)
{
	if(pAct->erConfuseTarget)
	{
		pAct->erTarget = pAct->erConfuseTarget;
	}
}

#ifdef GAMESERVER
static void character_FailedCooldown(Character *pchar,U32 uiActivationID, Power *ppow)
{
	int i; 
	PowerDef *pDef = GET_REF(ppow->hDef);

	if(!pDef)
		return;

	for(i=0;i<ea32Size(&pDef->piCategories);i++)
	{
		CooldownTimer *pTimer = character_GetCooldownTimerForCategory(pchar,pDef->piCategories[i]);

		if(pTimer && pTimer->fCooldown > 0)
		{
			ClientCmd_PowerActivationFailedCooldown(pchar->pEntParent,uiActivationID,pDef->piCategories[i],pTimer->fCooldown);
			return;
		}
	}
}
#endif

void character_ScheduleAnimFxCharge(int iPartitionIdx, 
									Character *pChar, 
									PowerActivation *pAct, 
									Power *pPower)
{
	Vec3 vecSourcePos, vecSourceDir, vecTargetFX;
	Entity *pEntTarget = NULL;

	entGetCombatPosDir(pChar->pEntParent, pAct, vecSourcePos, vecSourceDir);
	character_ActFindTarget(iPartitionIdx, pChar, pAct, vecSourcePos, vecSourceDir, &pEntTarget, vecTargetFX);

	// Play charge bits/fx/root
	character_AnimFXChargeOn(iPartitionIdx, pChar, pAct, pPower, vecTargetFX);
}

// Attempts to activate the power based on the power activation structure.  Called by the server.
S32 character_ActivatePowerServer(	int iPartitionIdx, 
									Character *pchar, 
									PowerActivationRequest *pActReq, 
									U32 bClient, 
									GameAccountDataExtract *pExtract)
{
#if !GAMESERVER && !GAMECLIENT
	assert(0);
#else
	Power *ppow;
	S32 bSuccess = true;
	PowerDef *pdef = NULL;
	char *pchName = "NULL";

	Entity *eTarget = pActReq->erTarget ? entFromEntityRef(iPartitionIdx, pActReq->erTarget) : NULL;
	Entity *eTargetPicking = (pActReq->erTargetPicking && eTarget==pchar->pEntParent) ? entFromEntityRef(iPartitionIdx, pActReq->erTargetPicking) : NULL;
	WorldInteractionNode *pNode = NULL;

	PERFINFO_AUTO_START_FUNC();

	PERFINFO_AUTO_START("pre-Activate", 1);
	if(eTarget
		&& eTarget->pChar != pchar
		&& (!entIsSelectable(pchar,eTarget)
			|| !entIsTargetable(eTarget)))
	{
		pActReq->erTarget = 0;
		eTarget = NULL;
	}

#ifdef GAMESERVER
	if(!eTarget)
	{
		if(pNode = GET_REF(pActReq->hObjectNodeKey))
		{
			eTarget = im_InteractionNodeToEntity(iPartitionIdx, pNode);
			//convert node target to entity target
			if(eTarget)
			{
				if (pNode == GET_REF(pchar->currentTargetHandle))
				{							
					entity_SetTarget(pchar->pEntParent, entGetRef(eTarget));
				}
				pActReq->erTarget = entGetRef(eTarget);
			}
			else
			{
				// Failed for some reason
				pNode = NULL;
			}
		}
	}
#endif

	PowersDebugPrintEnt(EPowerDebugFlags_ACTIVATE, pchar->pEntParent, "Activate %d: Received request %s (%s @ %d)\n",pActReq->uchActID,pActReq->bActivate?"START":"STOP",pActReq->pchPowerName?pActReq->pchPowerName:"NULL",pActReq->erTarget);

	// Basic init
	ppow = character_FindPowerByIDAndName(pchar, pActReq->uiPowerID, pActReq->pchPowerName);

	// check to see if we are in a power link mode to change the actual power we queue
	if (pchar->pCombatPowerStateInfo)
	{
		Power *pSwitchedPower = NULL;
		pSwitchedPower = CombatPowerStateSwitching_GetSwitchedStatePower(pchar, ppow);
		if (pSwitchedPower)
		{
			ppow = pSwitchedPower;
		}
	}

	if(ppow) pdef = GET_REF(ppow->hDef);
	if(pdef) pchName = pdef->pchName;

	PERFINFO_AUTO_STOP();// pre-Activate

	if(pActReq->bActivate)
	{
		ActivationFailureReason eFail = kActivationFailureReason_None;

		PERFINFO_AUTO_START("Activate", 1);

		if(pActReq->bCancelExisting)
		{
			// Manually cancel all existing activations, but try to refund the current
			character_ActOverflowCancel(iPartitionIdx, pchar, NULL, 0);
			character_ActQueuedCancel(iPartitionIdx, pchar, NULL, 0);
			character_ActCurrentCancelReason(iPartitionIdx, pchar, 
											g_CombatConfig.alwaysQueue.bCurrentPowerForceCancel,
											g_CombatConfig.alwaysQueue.bCurrentPowerRefundCost,
											g_CombatConfig.alwaysQueue.bCurrentPowerRechargePower, 
											kAttribType_Null);
		}

		if(ppow)
		{
			Power *ppowQueue;

			// Check if we're talking about a toggle, because this might actually be the depress used to turn it off

			// It is probably currently impossible to deactivate a "CheckComboBeforeToggle" toggle power.  Technically it probably should be possible,
			// but in practice, we aren't going to do that right now.  More work could be done to make this a thing.  [RMARR - 3/6/13]
			if(pdef->eType==kPowerType_Toggle || (pdef->bComboToggle && !pdef->bCheckComboBeforeToggle))
			{
				int i;
				U32 uiIDToggle = ppow->pParentPower ? ppow->pParentPower->uiID : ppow->uiID;
				PowerActivation *pactToggle = NULL;
				S32 bToggleActive = false;

				// Find it in list of active toggles
				for(i=eaSize(&pchar->ppPowerActToggle)-1; i>=0; i--)
				{
					if(uiIDToggle == pchar->ppPowerActToggle[i]->ref.uiID)
					{
						pactToggle = pchar->ppPowerActToggle[i];
						bToggleActive = true;
						break;
					}
				}

				// Didn't find it in active list, check for current/queued/overflow activations
				if(!pactToggle)
				{
					if(pchar->pPowActCurrent
						&& pchar->pPowActCurrent->ref.uiID==uiIDToggle
						&& poweract_IsToggle(pchar->pPowActCurrent))
					{
						pactToggle = pchar->pPowActCurrent;
					}
					else if(pchar->pPowActQueued
						&& pchar->pPowActQueued->ref.uiID==uiIDToggle
						&& poweract_IsToggle(pchar->pPowActQueued))
					{
						pactToggle = pchar->pPowActQueued;
					}
					else if(pchar->pPowActOverflow
						&& pchar->pPowActOverflow->ref.uiID==uiIDToggle
						&& poweract_IsToggle(pchar->pPowActOverflow))
					{
						pactToggle = pchar->pPowActOverflow;
					}
				}

				if(pactToggle)
				{
					// Found an active or to-be active toggle with the same id (or parent id), so that
					//  should mean this is a deactivate
					if(bClient && !pActReq->bToggleDeactivate)
					{
						// Client was trying to activate, but it's already active, so do nothing
						PowersDebugPrintEnt(EPowerDebugFlags_ACTIVATE, pchar->pEntParent, "Activate %d: Toggle already active\n", pActReq->uchActID);
#ifdef GAMESERVER
						ClientCmd_PowerActivationFailedOther(pchar->pEntParent,pActReq->uchActID);
#endif
						PERFINFO_AUTO_STOP();// Activate
						PERFINFO_AUTO_STOP();// FUNC
						return false;
					}
					
					if(!(pdef->eInterrupts&kPowerInterruption_Requested))
					{
						PowersDebugPrintEnt(EPowerDebugFlags_ACTIVATE, pchar->pEntParent, "Activate %d: Unable to Deactivate toggle: Toggle does not allow requested Deactivation\n", pActReq->uchActID);
#ifdef GAMESERVER
						ClientCmd_PowerActivationFailedOther(pchar->pEntParent,pActReq->uchActID);
#endif
						PERFINFO_AUTO_STOP();// Activate
						PERFINFO_AUTO_STOP();// FUNC
						return false;
					}
					
					PowersDebugPrintEnt(EPowerDebugFlags_ACTIVATE, pchar->pEntParent, "Activate %d: Deactivating toggle\n", pActReq->uchActID);

					if(bToggleActive)
					{
						// Deactivate it (pass NULL for the Power so it properly picks it up from the PowerActivation)
						character_DeactivateToggle(iPartitionIdx,pchar,pactToggle,NULL,pActReq->uiTimeCurrented,true);
					}
					else
					{
						// Mark it to deactivate
						pactToggle->bDeactivate = true;

						// If it wasn't even current yet, just try to cancel it
						if(pactToggle==pchar->pPowActOverflow)
						{
							character_ActOverflowCancel(iPartitionIdx,pchar,NULL,pactToggle->uchID);
						}
						else if(pactToggle==pchar->pPowActQueued)
						{
							character_ActQueuedCancel(iPartitionIdx,pchar,NULL,pactToggle->uchID);
						}
					}

					PERFINFO_AUTO_STOP();// Activate
					PERFINFO_AUTO_STOP();// FUNC
					return bSuccess;
				}
				else
				{
					if(bClient && pActReq->bToggleDeactivate)
					{
						// Client was trying to deactivate a toggle, but it's not active, so do nothing
						PowersDebugPrintEnt(EPowerDebugFlags_ACTIVATE, pchar->pEntParent, "Activate %d: Toggle already inactive\n", pActReq->uchActID);
						PERFINFO_AUTO_STOP();// Activate
						PERFINFO_AUTO_STOP();// FUNC
						return false;
					}
				}
			}

			PERFINFO_AUTO_START("activate-main", 1);
			// If there's nothing current right now and we're not paused, try to flush the queue
			if(pdef->eType != kPowerType_Instant && !pchar->pPowActCurrent && !mapState_IsMapPaused(mapState_FromPartitionIdx(iPartitionIdx)))
			{
				character_TickQueue(iPartitionIdx,pchar,0.f);
			}

			// Check and see if we're allowed to queue, and what we would be queuing
			ppowQueue = character_CanQueuePower(iPartitionIdx,pchar,ppow,eTarget,pActReq->vecTargetSecondary,pNode,eTargetPicking,NULL,NULL,NULL,pActReq->uiTimeQueued,pActReq->iPredictedIdx,&eFail,true,false,!g_CombatConfig.bAllowRechargingPowersInQueue,pExtract);

			// I think I can activate this
			if(ppowQueue && GET_REF(ppowQueue->hDef))
			{
				PowerActivation *pact;
				PowerDef *pdefQueue = GET_REF(ppowQueue->hDef);
				
				// Copied from above, since the toggle may be inside a combo
				if(pdefQueue->eType==kPowerType_Toggle)
				{
					// "Activating" a toggle might actually mean deactivate
					int i = poweract_FindPowerInArray(&pchar->ppPowerActToggle,ppowQueue);
					if(i>=0)
					{
						PowersDebugPrintEnt(EPowerDebugFlags_ACTIVATE, pchar->pEntParent, "Activate %d: Deactivating child toggle\n", pActReq->uchActID);
						character_DeactivateToggle(iPartitionIdx,pchar,pchar->ppPowerActToggle[i],NULL,pActReq->uiTimeCurrented,true);
						PERFINFO_AUTO_STOP();// activate-main
						PERFINFO_AUTO_STOP();// Activate
						PERFINFO_AUTO_STOP();// FUNC
						return bSuccess;
					}
				}

				// Start it up
				pact = CharacterQueuePower(	iPartitionIdx,
											pchar,
											eTarget,
											NULL,
											(pActReq->bUseSourceDir ? pActReq->vecSourceDir : NULL),	
											(pActReq->bUseVecTarget ? pActReq->vecTarget : NULL),
											pActReq->vecTargetSecondary,
											ppowQueue,
											pdefQueue,
											pActReq->uiTimeQueued,
											pActReq->uiTimeEnterStance,
											pActReq->bAnimateNow,
											pActReq->uchActID,
											pActReq->iPredictedIdx,
											(pActReq->bUnpredicted?-1:pActReq->iLungePrediction),
											&pActReq->puiActIDsCanceled);

				copyVec3(pActReq->vecTargetSecondary, pact->vecTargetSecondary);
				pact->erProximityAssistTarget = pActReq->erProximityAssistTarget;
				pact->eInputDirectionBits = pActReq->eInputDirectionBits;

				// do Confuse targeting
				if (!pdefQueue->bDisableConfuseTargeting)
				{
					EntityRef *perConfuseList = NULL;
					U32 uiConfuseSeed = pchar->uiConfuseSeed;
					EntityRef erConfuseTarget = character_serverCalulateConfuse(pchar,&perConfuseList,&uiConfuseSeed);

					if(pActReq->erConfuseTarget && pActReq->erConfuseTarget == erConfuseTarget)
					{
						pact->erConfuseTarget = pActReq->erConfuseTarget;
						pchar->uiConfuseSeed = uiConfuseSeed;
						entity_SetDirtyBit(pchar->pEntParent, parse_Character, pchar, false);
						entity_SetTarget(pchar->pEntParent, pact->erConfuseTarget);
						PowersDebugPrintEnt(EPowerDebugFlags_ACTIVATE, pchar->pEntParent, "Character confuse DOES AGREE: %d \n\n",pActReq->erConfuseTarget);
					}
					else if(pActReq->erConfuseTarget || erConfuseTarget)
					{
						pact->erConfuseTarget = erConfuseTarget;
						PowersDebugPrintEnt(EPowerDebugFlags_ACTIVATE, pchar->pEntParent, "Character confuse does not agree: %d : %d\n\n",pActReq->erConfuseTarget,erConfuseTarget);
					}

					//Check confuse targeting
					character_confuseSetTarget(pchar,pact);
				}

				// Automatically mark as committed
				if(pActReq->bAutoCommit)
				{
					pact->bCommit = true;
					
					// AutoCommit isn't used by players, but just in case it is, we'll overwrite
					//  the uiSeedSBLORN with a random number and make sure it isn't a player
					pact->uiSeedSBLORN = randomU32();
					devassertmsg(!entCheckFlag(pchar->pEntParent,ENTITYFLAG_IS_PLAYER),"Player attempting to use bAutoCommit, which is not currently supported");
				}

				// Copy the unpredicted flag
				if(pActReq->bUnpredicted)
				{
					pact->bUnpredicted = true;
					
					// Since this isn't predicted, we can overwrite the uiSeedSBLORN
					//  with a random number
					pact->uiSeedSBLORN = randomU32();
				}

				// Now take a look at the actual power being activated
				ppow = character_ActGetPower(pchar,pact);
				pdef = GET_REF(ppow->hDef);
				pchName = pdef->pchName;

				// Start up the appropriate anim/fx
				if(pdef->eType != kPowerType_Instant)
				{
					character_ActRefEnhancements(iPartitionIdx,pchar,pact,NULL);
					character_ActRefAnimFX(pchar,pact);
				}
				
				
				// Ready appropriate Items
				if(gConf.bItemArt)
				{
					int i;
					for(i=eaiSize(&pdef->piCategories)-1; i>=0; i--)
					{
						PowerCategory* pcat = eaGet(&g_PowerCategories.ppCategories,pdef->piCategories[i]);
						if(pcat && pcat->bWeaponBased)
						{
							entity_ReadyItemsForPowerCat(pchar->pEntParent, pcat);
							break;
						}
					}
				}

				// Enter the stance
				character_EnterStance(iPartitionIdx,pchar,ppow,pact,true,pact->uiTimestampEnterStance);
				character_EnterPersistStance(iPartitionIdx,pchar,ppow,pdef,pact,pact->uiTimestampEnterStance,0,false);

				// Turn to face the target
				character_MoveFaceStart(iPartitionIdx,pchar,pact,kPowerAnimFXType_None);

				if(pact->pRefAnimFXMain)
				{
					PERFINFO_AUTO_START("AnimFX", 1);
					if(pchar->eChargeMode==kChargeMode_Queued)
					{
						if(pActReq->bUseVecTarget) pact->bChargeAtVecTarget = true;
						
						character_ScheduleAnimFxCharge(iPartitionIdx, pchar, pact, ppow); 
					}
					else if(pchar->eChargeMode==kChargeMode_Overflow)
					{
						// Less common case, we got a charged power into the overflow queue
						if(pActReq->bUseVecTarget) pact->bChargeAtVecTarget = true;
						
						character_ScheduleAnimFxCharge(iPartitionIdx, pchar, pact, ppow); 
					}
					else if (pdef==NULL || pdef->fTimePreactivate == 0.0f)
					{
						if(pact->bUnpredicted
							&& g_CombatConfig.pHitChance
							&& pdef->eEffectArea == kEffectArea_Character
							&& GET_REF(pact->pRefAnimFXMain->hFX)
							&& GET_REF(pact->pRefAnimFXMain->hFX)->bDelayedHit)
						{
							pact->bDelayActivateToHitCheck = true;
						}
						else if(pdef->eTracking != kTargetTracking_UntilCurrent && 
								(!powerddef_ShouldDelayTargeting(pdef) || pActReq->bDontDelayTargeting))
						{
							// Play activation fx
							Vec3 vecSourcePos, vecSourceDir, vecTarget;
							Entity *pentTarget = NULL;
							entGetCombatPosDir(pchar->pEntParent, pact, vecSourcePos, vecSourceDir);
														
							// Requesting that we use the sent source vector
							if(pActReq->bUseVecSource)
							{
								Vec3 vecSourcePosDelta;
								subVec3(vecSourcePos,pActReq->vecSourcePos,vecSourcePosDelta);
								if(lengthVec3Squared(vecSourcePosDelta)<25.0f)
								{
									// What client claimed as position was reasonably close to
									//  what the server sees, so we'll animate the activation
									//  based on that instead of the server's data
									copyVec3(pActReq->vecSourcePos,vecSourcePos);
									copyVec3(pActReq->vecSourceDir,vecSourceDir);
								}
							}

							entGetActivationSourcePosDir(pchar->pEntParent, pact, pdef, vecSourcePos, vecSourceDir);

							if(character_ActFindTarget(iPartitionIdx,pchar,pact,vecSourcePos,vecSourceDir,&pentTarget,vecTarget))
							{
								// Requesting that we're targeting a point in space
								if(pActReq->bUseVecTarget)
								{
									Vec3 vecTargetDelta;
									subVec3(pActReq->vecTarget, vecTarget, vecTargetDelta);
									if(lengthVec3Squared(vecTargetDelta) < 25.0f)
									{
										// What client claimed as target position was reasonably
										//  close to what the server sees, so we'll animate the
										//  activation based on that instead of the server's data
										copyVec3(pActReq->vecTarget, vecTarget);
									}
								}

								character_MoveLungeStart(pchar, pact);
								character_AnimFXLunge(iPartitionIdx, pchar, pact);
								character_AnimFXActivateOn(	iPartitionIdx,
															pchar,
															NULL,
															pact,
															ppow,
															(pentTarget ? pentTarget->pChar : NULL),
															vecTarget,
															pact->uiTimestampActivate,
															pact->uchID,
															pact->uiPeriod,
															0);
							}
						}
						else if (powerddef_ShouldDelayTargeting(pdef))
						{
							PowersDebugPrintEnt(EPowerDebugFlags_ACTIVATE, pchar->pEntParent, "DelayedTargeting Power at %d\n",pact->uiTimestampActivate);
						}
					}
					PERFINFO_AUTO_STOP();// AnimFX
				}
			}
			else
			{
				PowersDebugPrintEnt(EPowerDebugFlags_ACTIVATE, pchar->pEntParent,"Activate %d: FAIL: Queue denied\n",pActReq->uchActID);
				if(!eFail)
				{
					eFail = kActivationFailureReason_Other;
				}
				bSuccess = false;
			}

			PERFINFO_AUTO_STOP();// activate-main

#ifdef GAMESERVER
			if(eFail)
			{
				PERFINFO_AUTO_START("fail clientcmd", 1);
				PowersSelectDebug("Power activation %d failed: Reason %d\n",pActReq->uchActID,eFail);
				switch(eFail)
				{
				case kActivationFailureReason_NoChargesRemaining:
					ClientCmd_PowerActivationFailedNoChargesRemaining(pchar->pEntParent,pActReq->uchActID);
				case kActivationFailureReason_ComboMispredict:
					ClientCmd_PowerActivationComboMispredict(pchar->pEntParent,pActReq->uchActID);
					break;
				case kActivationFailureReason_Recharge:
				{
					U32 uiID; 
					S32 iSubIdx; 
					S16 iLinkedSubIdx;
					power_GetIDAndSubIdx(ppow, &uiID, &iSubIdx, &iLinkedSubIdx);
					ClientCmd_PowerActivationFailedRecharge(pchar->pEntParent,pActReq->uchActID,uiID,power_GetRecharge(ppow));
				} break;

				case kActivationFailureReason_Cost:
					ClientCmd_PowerActivationFailedCost(pchar->pEntParent,pActReq->uchActID);
					break;
				case kActivationFailureReason_Cooldown:
					character_FailedCooldown(pchar,pActReq->uchActID,ppow);
					break;
				case kActivationFailureReason_PowerModeDisallowsUsage:
				case kActivationFailureReason_PriorActNonInterrupting:
				case kActivationFailureReason_TargetOutOfRange:
				case kActivationFailureReason_TargetOutOfRangeMin:
				case kActivationFailureReason_TargetNotInArc:
				case kActivationFailureReason_TargetInvalid:
				case kActivationFailureReason_TargetImperceptible:
				case kActivationFailureReason_TargetLOSFailed:
				case kActivationFailureReason_DoesNotHaveRequiredItemEquipped:
				case kActivationFailureReason_Disabled:
				case kActivationFailureReason_Knocked:
				case kActivationFailureReason_Other:
					ClientCmd_PowerActivationFailedOther(pchar->pEntParent,pActReq->uchActID);
				}
				PERFINFO_AUTO_STOP();// fail clientcmd
			}
#endif GAMESERVER
		}
		else
		{
			PowersDebugPrintEnt(EPowerDebugFlags_ACTIVATE, pchar->pEntParent, "Activate %d FAIL: Character doesn't own power\n",pActReq->uchActID);
#ifdef GAMESERVER
			ClientCmd_PowerActivationFailedOther(pchar->pEntParent,pActReq->uchActID);
#endif
			bSuccess = false;
		}

		PERFINFO_AUTO_STOP();// Activate
	}
	else
	{
		// Request was to deactivate
		if (pdef &&	// A be-critter power might have been removed ... if so pdef == NULL
			pdef->bUpdateChargeTargetOnDeactivate && 
			pchar->pPowActCurrent && 
			pchar->eChargeMode == kChargeMode_Current && 
			pActReq->bUpdateChargeVecTarget)
		{
			character_CurrentChargePowerUpdateVecTarget(pchar,pActReq->uchActID,pActReq->vecTarget,pActReq->erTarget);
		}

		character_ActDeactivate(iPartitionIdx,pchar,&pActReq->uchActID,&pActReq->eModeDeactivate,pActReq->uiTimeQueued,pActReq->uiTimeCurrented,false);
	}

	PERFINFO_AUTO_STOP();// FUNC
	return bSuccess;
#endif
}


EntityRef character_serverValidateConfuse(int iPartitionIdx, Character *p,EntityRef erServerTarget, EntityRef erClientTarget, EntityRef *perServerList, EntityRef *perClientList)
{
	//Undisputed, check to see if character is confused on server
	if(p->pattrBasic->fConfuse > 0)
	{
		F32 fRand;
		
		fRand = randomF32Seeded(&p->uiConfuseSeed,RandType_BLORN);

		if(fRand < p->pattrBasic->fConfuse)
		{
			Vec3 vPos;
			int i,iRand;
			U32 uiSeedOffset=0;
			bool bValid = true;

			//Get the characters position
			entGetPos(p->pEntParent,vPos);

			//Validate the two lists
			for(i=ea32Size(&perServerList)-1;i>=0;i--)
			{
				if(ea32Find(&perClientList,perServerList[i]))
					continue;

				//Server picked up an ent that the client did not find
				//Distance check
				{
					Vec3 vEntPos,vTarget;
					F32 fDist;
					Entity *pEnt;

					pEnt = entFromEntityRef(iPartitionIdx, perServerList[i]);

					if(!pEnt)
					{
						ea32Remove(&perServerList,i);
						continue;
					}

					entGetPos(pEnt,vEntPos);

					subVec3(vPos,vEntPos,vTarget);
					fDist = lengthVec3(vTarget);

					if(fDist > 46.0f)
					{
						ea32Remove(&perServerList,i);
						continue;
					}

					bValid = false;
				}
			}

			for(i=ea32Size(&perClientList)-1;i>=0;i--)
			{
				if(ea32Find(&perServerList,perClientList[i]))
					continue;

				//Server did not pick up an ent that the client found
				//Distance check
				{
					Vec3 vEntPos,vTarget;
					F32 fDist;
					Entity *pEnt;

					pEnt = entFromEntityRef(iPartitionIdx, perClientList[i]);

					if(!pEnt)
					{
						bValid = false;
						continue;
					}

					entGetPos(pEnt,vEntPos);

					subVec3(vPos,vEntPos,vTarget);
					fDist = lengthVec3(vTarget);

					if(fDist < 54.0f)
					{
						ea32Push(&perServerList,perClientList[i]);
						continue;
					}

					bValid = false;
				}
			}
			if(!perServerList || ea32Size(&perServerList)==0)
				return 0;

			sortEntRefList(&perServerList);

			for(i=0;i<ea32Size(&perServerList);i++)
			{
				uiSeedOffset += perServerList[i];
			}

			iRand = randomIntRangeSeeded(&p->uiConfuseSeed,RandType_BLORN,0,ea32Size(&perServerList)-1);
			return perServerList? perServerList[iRand] : 0;	
		}
	}
	return 0;
}

void character_clientCalulateConfuse(Character *p, PowerActivation *pAct)
{
	//Check to see if the player is confused
	if(p->pattrBasic->fConfuse > 0)
	{
		F32 fRand;
		pAct->uiConfuseSeed = p->uiConfuseSeed;

		fRand = randomF32Seeded(&p->uiConfuseSeed,RandType_BLORN);

		if(fRand < 0.0f)
			fRand *= -1;

		if(fRand < p->pattrBasic->fConfuse)
		{
			EntityIterator * iter = entGetIteratorAllTypesAllPartitions(0,ENTITYFLAG_IGNORE|ENTITYFLAG_DONOTDRAW|ENTITYFLAG_UNTARGETABLE);
			//Get the list of near by entities
			Entity *currEnt = NULL;
			Vec3 vPos;
			int iRand;
			U32 uiSeedOffset=0;

			entGetPos(p->pEntParent,vPos);

			while ((currEnt = EntityIteratorGetNext(iter)))
			{
				Vec3 currEntPos,vTarget;
				F32 fDist;

				//Distance check
				entGetPos(currEnt, currEntPos);
				subVec3(vPos,currEntPos,vTarget);
				fDist = lengthVec3(vTarget);

				if(fDist > 50.0f)
					continue;

				if(currEnt == p->pEntParent)
					continue;
				if(entGetRef(currEnt) == p->currentTargetRef)
					continue;
				//No destructible objects allowed
				if(IS_HANDLE_ACTIVE(currEnt->hCreatorNode))
					continue;

				ea32Push(&pAct->perConfuseTargetList,entGetRef(currEnt));
				uiSeedOffset += entGetRef(currEnt);
			}

			sortEntRefList(&pAct->perConfuseTargetList);

			if(ea32Size(&pAct->perConfuseTargetList) == 0)
			{
				EntityIteratorRelease(iter);
				return;
			}

			p->uiConfuseSeed += uiSeedOffset;
			iRand = randomIntRangeSeeded(&p->uiConfuseSeed,RandType_BLORN,0,ea32Size(&pAct->perConfuseTargetList)-1);
			entity_SetDirtyBit(p->pEntParent, parse_Character, p, false);
			pAct->erConfuseTarget = pAct->perConfuseTargetList[iRand];	
			entity_SetTarget(p->pEntParent, pAct->erConfuseTarget); //Server will select the target itself
			EntityIteratorRelease(iter);
		}
	}
}

static void character_GetVecTargetProximityAssistClient(int iPartitionIdx, 
														Character *pchar, 
														Power *ppow,
														PowerDef *pdef, 
														SA_PARAM_NN_VALID Vec3 vecTargetOut,
														SA_PARAM_OP_VALID Vec3 vAimTrajectoryPosOut,
														SA_PARAM_NN_VALID Vec3 vecSourceDirOut,
														SA_PARAM_OP_VALID Entity **ppProxAssistTargetOut)
{
#ifdef GAMECLIENT
	Vec3 vPlayerPosition, vPowerDirection, vecSource;
	Entity *ent = pchar->pEntParent;
	bool bIgnorePitch = false;
	F32 fRange;

	if(!ent->pPlayer || !ent->pPlayer->bUseFacingPitch || powerdef_ignorePitch(pdef))
	{
		bIgnorePitch = true;
	}

	entGetCombatPosDir(ent, NULL, vPlayerPosition, NULL);

	clientTarget_GetVecTargetingDirection(ent, pdef, vPowerDirection);
	
	if (bIgnorePitch)
	{
		vPowerDirection[1] = 0.f;
		normalVec3(vPowerDirection);
	}

	fRange = power_GetRange(ppow, pdef);


	if(bIgnorePitch)
	{
		scaleAddVec3(vPowerDirection, fRange, vPlayerPosition, vecTargetOut);
		copyVec3(vPlayerPosition, vecSource);
	}
	else
	{
		Vec3 vecCameraPosition;
		F32 fDistance;
		gfxGetActiveCameraPos(vecCameraPosition);
		fDistance = distance3(vecCameraPosition,vPlayerPosition) + 5.f;
		fRange = max(fRange, fDistance);
		scaleAddVec3(vPowerDirection, fRange, vecCameraPosition, vecTargetOut);
		copyVec3(vecCameraPosition, vecSource);
	}

	
	if(pdef->bUseCameraTargetingVecTargetAssist)
	{
		Entity *eTarget;

		eTarget = clientTarget_FindProximityTargetingAssistEnt(ent, vPowerDirection);
		if (eTarget)
		{
			Vec3 vTargetPos;
			entGetCombatPosDir(eTarget, NULL, vTargetPos, NULL);
			subVec3(vTargetPos, vPlayerPosition, vPowerDirection);
			
			// don't ignore pitch, even if bIgnorePitch is set 
			normalVec3(vPowerDirection);
			scaleAddVec3(vPowerDirection, fRange, vPlayerPosition, vecTargetOut);
			copyVec3(vPlayerPosition, vecSource);
		}

		if (ppProxAssistTargetOut)
			*ppProxAssistTargetOut = eTarget;
	}
	else if (ppProxAssistTargetOut)
	{
		*ppProxAssistTargetOut = NULL;
	}
	

	if (pdef->eTargetVisibilityMain == kTargetVisibility_LineOfSight)
	{
		Vec3 vecResult;
		// Check if we hit the world
		if(!combat_CheckLoS(iPartitionIdx, vecSource, vecTargetOut, ent, NULL, NULL, false, false, vecResult))
		{
			// Use world hit location as the locational target
			copyVec3(vecResult,vecTargetOut);
		}
	}

	copyVec3(vPowerDirection, vecSourceDirOut);
	if (vAimTrajectoryPosOut)
		copyVec3(vecSource, vAimTrajectoryPosOut);

	PowersDebugPrintEnt(EPowerDebugFlags_ACTIVATE, pchar->pEntParent, "Player Camera TargetPosition: p(%1.2f, %1.2f, %1.2f)\n", vecParamsXYZ(vecTargetOut));
	

#endif
}

void character_UpdateTargetingClient(int iPartitionIdx, Character *pchar, PowerDef *pdef, Power *ppow, Vec3 vecTargetOut, Entity **ppTarget)
{
#ifdef GAMECLIENT
	Entity *eTarget = NULL;
	PowerTarget *ppowTarget = pdef ? GET_REF(pdef->hTargetMain) : NULL;
	Vec3 vDir; 
	character_GetVecTargetProximityAssistClient(iPartitionIdx, pchar, ppow, pdef, vecTargetOut, NULL, vDir, NULL);
	
	// Figure out who to target!
	if(character_PowerRequiresValidTarget(pchar, pdef) || 
		!ppowTarget || 
		!ppowTarget->bDoNotTargetUnlessRequired)
	{
		//Get the current soft target and determine if it should be the new hard target
		ClientTargetDef *pTargetDef = clientTarget_SelectBestTargetForPower(pchar->pEntParent,ppow,NULL);

		if(pTargetDef)
		{
			eTarget = entFromEntityRef(iPartitionIdx, pTargetDef->entRef);
		}
	}

	*ppTarget = eTarget;
#endif
}


static S32 CharacterActivatePowerClient_GetTeleportTargets(	Character *pChar, 
															Entity *eTarget, 
															PowerDef *pPowerDef,
															Vec3 vTeleportBasePosOut,
															Vec3 vTeleportPYROut)
{
#ifdef GAMECLIENT
	// and use the vecTarget to tell where we thought the target was for validation 
	AttribModDef *pTeleportAttribModDef = powerdef_GetTeleportAttribMod(pPowerDef);
	
	// further target overriding for teleport
	if (pTeleportAttribModDef)
	{
		return ModTeleportGetTeleportTargetTranslations(pChar, eTarget, pTeleportAttribModDef, vTeleportBasePosOut, vTeleportPYROut);
	}
	// 
#endif
	return false;
}

// set movement input bits that may be used for lurches
#ifdef GAMECLIENT
U32 CharacterActivatePowerClient_GetInputDirectionBits() 
{
	U32 bits = 0;
	
	if (getControlButtonState(MIVI_BIT_FORWARD))
		bits |= kMovementInputBits_Forward;
	if (getControlButtonState(MIVI_BIT_BACKWARD))
		bits |= kMovementInputBits_Back;
	if (getControlButtonState(MIVI_BIT_LEFT))
		bits |= kMovementInputBits_Left;
	if (getControlButtonState(MIVI_BIT_RIGHT))
		bits |= kMovementInputBits_Right;

	return bits;
}

#endif


static S32 CharacterActivatePowerClient(int iPartitionIdx,
										Character *p,
										Entity *eTarget,
										Vec3 vecTargetPrimary,
										Vec3 vecTargetSecondary,
										SA_PARAM_NN_VALID Power *ppow,
										S32 bStarting,
										GameAccountDataExtract *pExtract)
{
#if !GAMESERVER && !GAMECLIENT
	assert(0);
#else
	S32 bSuccess = true;

#ifdef GAMECLIENT

	Character *pchar = p;
	WorldInteractionNode *pObjectTarget = NULL;

	PowerDef *pdef = NULL;
	char *pchName = "NULL";
	
	// Structure used to communicate the request to the server
	PowerActivationRequest *pActReq;

	// This event happens now
	U32 uiTimeOfEvent = pmTimestamp(0);

	Entity *ent = pchar->pEntParent;

	// Validate the parameters
	devassertmsg(!(eTarget && vecTargetPrimary), "You should use either a target entity or a target position. Setting both is not a valid option");

	if (g_CombatConfig.bStopAutoForwardOnPowerActivation)
	{
		gclPlayerControl_SetAutoForward(false);
	}

#ifdef GAMECLIENT
	//Predict a dismount
	gclCharacter_ForceDismount(pchar);
#endif

	if (eTarget && vecTargetPrimary)
	{
		return false;
	}

	if (ent->pPlayer && (ent->pPlayer->iStasis || ent->pPlayer->bStuckRespawn || ent->pPlayer->bIgnoreClientPowerActivations))
	{
		// Players in stasis or stuck can't use any powers.
		return false;
	}

	if(g_bCombatPredictionDisabled)
	{
		// Could use the request structure here, but for now just a direct call
		ServerCmd_entUsePowerServerUnpredicted(ppow->uiID, eTarget ? entGetRef(eTarget) : p->currentTargetRef, bStarting);
		return true;
	}

	pdef = GET_REF(ppow->hDef);
	if(pdef && pdef->bUnpredicted)
	{
		// This Power specifically is Unpredicted
		// Could use the request structure here, but for now just a direct call
		EntityRef erTarget = 0;
		if(eTarget)
		{
			erTarget = entGetRef(eTarget);
		}
		else
		{
			 ClientTargetDef *pTarget = clientTarget_SelectBestTargetForPower(p->pEntParent,ppow,false);
			 if(pTarget)
				 erTarget = pTarget->entRef;
		}
		ServerCmd_entUsePowerServerUnpredicted(ppow->uiID, erTarget, bStarting);
		return true;
	}

	pActReq = StructAlloc(parse_PowerActivationRequest);

	if (ent != entActivePlayerPtr())
	{
		if (ent == entGetPrimaryPet(entActivePlayerPtr()))
		{
			pActReq->bPrimaryPet = true;
		}
		else
		{
			assertmsg(0, "Client Power Requested by invalid entity");
			return false;
		}
	}


	// Early PowerReplace check
	//  This happens again in character_PickActivatedPower(), but doing it here ensures the client-side target picking
	//  and such is always using the proper power.
	if(ppow && ppow->uiReplacementID)
	{
		Power *ppowReplace = character_FindPowerByID(pchar,ppow->uiReplacementID);
		if(ppowReplace)
		{
			ppow = ppowReplace;
		}
	}

	// Basic init
	pActReq->bActivate = bStarting;
	pActReq->uiPowerID = ppow ? ppow->uiID : 0;

	// Re-fetch the PowerDef
	if(ppow)
		pdef = GET_REF(ppow->hDef);

	if(pdef)
	{
		pchName = pdef->pchName;
	}
	else
	{
		PowersDebugPrintEnt(EPowerDebugFlags_ACTIVATE, p->pEntParent, "Activate FAIL: Received request %s for power with unknown def\n",bStarting?"START":"STOP");
		StructDestroy(parse_PowerActivationRequest,pActReq);
		return false;
	}


	PowersDebugPrintEnt(EPowerDebugFlags_ACTIVATE, ent, "Activate: Received request %s %s\n", (bStarting?"START":"STOP"), pchName);

	if(bStarting)
	{
		ActivationFailureReason eFail = kActivationFailureReason_None;
		U8 uchID = poweract_NextID();
		pActReq->uchActID = uchID;

		// check to see if we are in a power link mode to change the actual power we queue
		if (p->pCombatPowerStateInfo)
		{
			Power *pSwitchedPower = NULL;
			pSwitchedPower = CombatPowerStateSwitching_GetSwitchedStatePower(p, ppow);
			if (pSwitchedPower)
			{
				PowerDef *pSwitchedDef = GET_REF(pSwitchedPower->hDef);
				if (pSwitchedDef)
				{
					ppow = pSwitchedPower;
					pdef = pSwitchedDef;
				}
			}
		}

		if(ppow)
		{
			Power *ppowQueue;
			Entity *pentQueue = NULL;
			WorldInteractionNode *pnodeQueue = NULL;
			bool pbShouldSetHardTarget = false;
			PowerTarget *ppowtarget = GET_REF(pdef->hTargetMain);

			if (g_CombatConfig.tactical.bDisablePowersUsageWhileAimIsHeld && gclPlayerControl_IsHoldingAim())
			{
				bool bAllowed = false;
				if (g_CombatConfig.tactical.piAllowedCategoriesWhileAimIsHeld)
				{
					S32 i;
					for (i = eaiSize(&g_CombatConfig.tactical.piAllowedCategoriesWhileAimIsHeld) - 1; i >= 0; --i)
					{
						if (eaiFind(&pdef->piCategories, g_CombatConfig.tactical.piAllowedCategoriesWhileAimIsHeld[i]) != -1)
						{
							bAllowed = true;
							break;
						}
					}
				}
			
				if (!bAllowed)
					return false;
			}
			
						
			// Check if we're talking about a toggle, because this might actually be the depress used to turn it off

			// It is probably currently impossible to deactivate a "CheckComboBeforeToggle" toggle power.  Technically it probably should be possible,
			// but in practice, we aren't going to do that right now.  More work could be done to make this a thing.  [RMARR - 3/6/13]
			if(pdef->eType==kPowerType_Toggle || (pdef->bComboToggle && !pdef->bCheckComboBeforeToggle))
			{
				int i;
				U32 uiIDToggle = ppow->pParentPower ? ppow->pParentPower->uiID : ppow->uiID;
				PowerActivation *pactToggle = NULL;
				S32 bToggleActive = false;

				// Find it in list of active toggles
				for(i=eaSize(&pchar->ppPowerActToggle)-1; i>=0; i--)
				{
					if(uiIDToggle == pchar->ppPowerActToggle[i]->ref.uiID)
					{
						pactToggle = pchar->ppPowerActToggle[i];
						bToggleActive = true;
						break;
					}
				}

				// Didn't find it in active list, check for current/queued activations
				if(!pactToggle)
				{
					if(pchar->pPowActCurrent
						&& pchar->pPowActCurrent->ref.uiID==uiIDToggle
						&& poweract_IsToggle(pchar->pPowActCurrent))
					{
						pactToggle = pchar->pPowActCurrent;
					}
					else if(pchar->pPowActQueued
						&& pchar->pPowActQueued->ref.uiID==uiIDToggle
						&& poweract_IsToggle(pchar->pPowActQueued))
					{
						pactToggle = pchar->pPowActQueued;
					}
				}

				if(pactToggle)
				{
					PowerDef *pdefToggle = GET_REF(pactToggle->hdef);

					PowersDebugPrintEnt(EPowerDebugFlags_ACTIVATE, pchar->pEntParent, "Activate %d: Attempting to deactivate toggle\n", uchID);

					if(!(pdefToggle->eInterrupts & kPowerInterruption_Requested))
					{
						PowersDebugPrintEnt(EPowerDebugFlags_ACTIVATE, pchar->pEntParent, "Activate %d: Unable to Deactivate toggle: Power doesn't interrupt on request", uchID);
						// Cleanup and return
						StructDestroy(parse_PowerActivationRequest,pActReq);
						return false;
					}

					if(bToggleActive)
					{
						// Deactivate it (pass NULL for the Power so it properly picks it up from the PowerActivation)
						character_DeactivateToggle(iPartitionIdx,pchar,pactToggle,NULL,pActReq->uiTimeCurrented,true);
					}
					else
					{
						// Mark it to deactivate
						pactToggle->bDeactivate = true;

						// If it wasn't even current yet, just try to cancel it
						if(pactToggle==pchar->pPowActQueued)
						{
							character_ActQueuedCancel(iPartitionIdx,pchar,NULL,pactToggle->uchID);
						}
					}

					// Send the deactivation to the server
					pActReq->pchPowerName = StructAllocString(pchName);
					pActReq->uiTimeCurrented = uiTimeOfEvent;
					pActReq->bToggleDeactivate = true; // This is a Toggle DEACTIVATE request
					poweractreq_FixCmdSend(pActReq);
					ServerCmd_entUsePowerServer(pActReq);

					// Cleanup and return
					StructDestroy(parse_PowerActivationRequest,pActReq);
					return bSuccess;
				}
				else
				{
					// Must be a request to activate either a toggle, or a part of a combo that isn't a toggle
					pActReq->bToggleDeactivate = false;
				}
			}

			// Figure out who to target!
			if(!eTarget && 
				(character_PowerRequiresValidTarget(pchar, pdef) || 
				 !ppowtarget || 
				 !ppowtarget->bDoNotTargetUnlessRequired))
			{
				//Get the current soft target and determine if it should be the new hard target
				ClientTargetDef *pTargetDef = clientTarget_SelectBestTargetForPower(pchar->pEntParent,ppow,&pbShouldSetHardTarget);

				if(pTargetDef)
				{
					eTarget = entFromEntityRef(iPartitionIdx, pTargetDef->entRef);
					pObjectTarget = GET_REF(pTargetDef->hInteractionNode);
				}

				if(eTarget)
				{
					PowersDebugPrintEnt(EPowerDebugFlags_ACTIVATE, pchar->pEntParent, "Activate %d: Selected target %s\n", uchID,eTarget->debugName);
				}
				else if(pObjectTarget)
				{
					PowersDebugPrintEnt(EPowerDebugFlags_ACTIVATE, pchar->pEntParent, "Activate %d: Selected object %s\n", uchID,wlInteractionNodeGetKey(pObjectTarget));
				}
				else
				{
					PowersDebugPrintEnt(EPowerDebugFlags_ACTIVATE, pchar->pEntParent, "Activate %d: Unable to find good target\n",uchID);
				}
			}
			//If a valid target isn't required, and the target is out of range and we're checking range, clear the targets
			if (!character_PowerRequiresValidTarget(pchar, pdef) &&
				((g_CombatConfig.bDisableOutOfRange &&
				!character_TargetInPowerRange(pchar, ppow, pdef, eTarget, pObjectTarget)) ||
				(ppowtarget && ppowtarget->bDoNotTargetUnlessRequired)))
			{
				eTarget = NULL;
				pObjectTarget = NULL;
			}

			// If there's nothing current right now and we're not paused, try to flush the queue
			if(!p->pPowActCurrent && !mapState_IsMapPaused(mapState_FromPartitionIdx(iPartitionIdx)))
			{
				character_TickQueue(iPartitionIdx,p,0.f);
			}

			// Check and see if we're allowed to queue, and what we would be queuing
			ppowQueue = character_CanQueuePower(iPartitionIdx,p,ppow,eTarget,vecTargetSecondary,pObjectTarget,NULL,&pentQueue,&pnodeQueue,&pbShouldSetHardTarget,uiTimeOfEvent,-1,&eFail,true,false,!g_CombatConfig.bAllowRechargingPowersInQueue,pExtract);
			if (!ppowQueue && g_CombatConfig.bCooldownPowersGetMultiExecedWhenActivated && (eFail == kActivationFailureReason_Cooldown || eFail == kActivationFailureReason_Recharge))
			{
				F32 powerCooldown = 0;
				MAX1(powerCooldown, power_GetRecharge(ppow));
				MAX1(powerCooldown, character_GetCooldownFromPowerDef(pchar, pdef));
				if (powerCooldown < g_CombatConfig.fMultiExecListClearTimer)//don't add if it would never conceivably survive the list purge
				{
					clientTarget_AddMultiPowerExecEx(ppow->uiID, false, true);
					StructDestroy(parse_PowerActivationRequest,pActReq);
					return true;//We've put it in the multiexec list, it will be queued eventually.
				}
			}
			// Queuing caused us to pick a different target
			if(pentQueue)
			{
				if(eTarget!=pentQueue && eTarget)
					pActReq->erTargetPicking = entGetRef(eTarget);
				eTarget = pentQueue;
				PowersDebugPrintEnt(EPowerDebugFlags_ACTIVATE, pchar->pEntParent, "Activate %d: Switched target %s\n",uchID,eTarget->debugName);
			}
			else if(pnodeQueue)
			{
				pObjectTarget = pnodeQueue;
				PowersDebugPrintEnt(EPowerDebugFlags_ACTIVATE, pchar->pEntParent, "Activate %d: Switched target %s\n",uchID,wlInteractionNodeGetKey(pObjectTarget));
			}

			if(ppowQueue)
			{
				PowerDef *pdefQueue = GET_REF(ppowQueue->hDef);
				bool bHadHardTarget =  clientTarget_HasHardTarget(ent) || (eTarget && pchar->erTargetDual==entGetRef(eTarget));

				// If we're auto hard-targeting, set the hard target
				if((g_CurrentScheme.bAutoHardTarget || (g_CurrentScheme.bAutoHardTargetIfNoneExists && !bHadHardTarget)) && pbShouldSetHardTarget)
				{
					if(eTarget && (eTarget != ent))
					{
						EntityRef er = entGetRef(eTarget);
						if(pchar->currentTargetRef != er)
						{
							entity_SetTarget(ent, er);
							bHadHardTarget = false;
						}
					}
					else if(pObjectTarget)
					{
						if(GET_REF(pchar->currentTargetHandle) != pObjectTarget)
						{
							entity_SetTargetObject(ent, wlInteractionNodeGetKey(pObjectTarget));
							bHadHardTarget = false;
						}
					}
				}

				if (g_CurrentScheme.bSnapCameraOnAttack)
				{				
					if (eTarget && !gbNoGraphics && !entIsVisible(eTarget))
						gclCamera_TurnToFaceTarget();
					else if (pObjectTarget && !gbNoGraphics && !wlNodeIsVisible(pObjectTarget))
						gclCamera_TurnToFaceTarget();
				}

				if((!pdefQueue || !pdefQueue->bApplyObjectDeath || (eTarget || pObjectTarget)) && 
					g_CurrentScheme.bRequireHardTargetToExec && 
					character_PowerRequiresValidTarget(pchar, pdef) && !bHadHardTarget && eTarget!=ent)
				{
					if (g_CurrentScheme.eAutoAttackType == kAutoAttack_Toggle)
					{
						gclAutoAttack_Disable();
					}
					ppowQueue = NULL;
				}
			}

			// I think I can activate this
			if(ppowQueue && GET_REF(ppowQueue->hDef))
			{
				PowerActivation *pact;
				Entity *pProxAssistTarget = NULL;
				U32 uiTimeEnterStance;
				PowerDef *pdefQueue = GET_REF(ppowQueue->hDef);
				Vec3 vecTarget = {0};
				Vec3 vecTargetSeconardyOverride = {0};
				Vec3 vOverrideSourceDir = {0};
				bool bUseVecTarget = false;
				bool bUseVecSecondary = false;
				bool bUseVecSourceDir = false;
				bool bUseAimTrajectoryPos = false;
				bool bSelfTargeting =  (!eTarget || eTarget == ent) && !pObjectTarget &&
										(!ppowtarget || !ppowtarget->bDoNotTargetUnlessRequired);

				if(pdef->bAlwaysQueue)
				{
					if (pchar->pPowActCurrent)
					{
						Power *pCurrPower = character_FindPowerByRef(pchar, &pchar->pPowActCurrent->ref);
						PowerDef *pCurPowerDef; 
						if (pCurrPower->pParentPower)
						{
							pCurrPower = pCurrPower->pParentPower;
						}

						pCurPowerDef = GET_REF(pCurrPower->hDef);
						if (!pCurPowerDef || !pCurPowerDef->bAlwaysQueue)
						{
							// Manually cancel all existing activations, but try to refund the current
							character_ActOverflowCancel(iPartitionIdx, pchar, NULL, 0);
							character_ActQueuedCancel(iPartitionIdx, pchar, NULL, 0);
							
							character_ActCurrentCancelReason(iPartitionIdx, pchar, 
													g_CombatConfig.alwaysQueue.bCurrentPowerForceCancel,
													g_CombatConfig.alwaysQueue.bCurrentPowerRefundCost,
													g_CombatConfig.alwaysQueue.bCurrentPowerRechargePower, 
													kAttribType_Null);
							pActReq->bCancelExisting = true;
						}
					}

				}
				
				if (eTarget && pdefQueue->bHasTeleportAttrib)
				{	// we are going to teleport, use the vecSecondary to tell the activation where we are going to teleport
					// and use the vecTarget to tell where we thought the target was for validation 
					if (CharacterActivatePowerClient_GetTeleportTargets(pchar, 
																		eTarget, 
																		pdefQueue, 
																		vecTargetSeconardyOverride, 
																		vecTarget))
					{
						bUseVecTarget = true;
						bUseVecSecondary = true;
						if (!vecTargetSecondary)
							vecTargetSecondary = vecTargetSeconardyOverride;
					}
					else
					{
						// failed to get the teleport target, we need to cancel this power - validate this?
					}
				}
				else if (vecTargetPrimary && !character_PowerRequiresValidTarget(pchar, pdef))
				{
					copyVec3(vecTargetPrimary, vecTarget);
					bUseVecTarget = true;
				}
				else if	(bSelfTargeting && pdefQueue->fRange && pchar->bUseCameraTargeting &&
					(g_CombatConfig.autoAttack.bDeactivateSelfTargetedMaintainsCancelOnTargetSwitch || pdef->eType != kPowerType_Maintained))
				{
					character_GetVecTargetProximityAssistClient(iPartitionIdx, 
																pchar, 
																ppowQueue,
																pdefQueue, 
																vecTarget, 
																vecTargetSeconardyOverride,
																vOverrideSourceDir,
																&pProxAssistTarget);
					if (!pdefQueue->bHasEffectAreaOffsets)
					{
						bUseVecTarget = true;
					}
					else
					{
						bUseVecSourceDir = true;
					}

					if (pdefQueue->bHasProjectileCreateAttrib)
					{
						bUseVecSecondary = true;
						vecTargetSecondary = vecTargetSeconardyOverride;
					}
				}
				else if	(bSelfTargeting && pdefQueue->bHasEffectAreaOffsets)
				{
					entGetCombatPosDir(pchar->pEntParent,NULL,NULL,vOverrideSourceDir);
					bUseVecSourceDir = true;
				}
								
				// The character will enter a stance immediately, however activation may be delayed if the stance has a transition time
				{
					F32 fDelay = 0.f;
					uiTimeEnterStance = character_PredictTimeForNewActivation(pchar,pdefQueue->bOverrides, pdefQueue->bCooldownGlobalNotChecked, ppowQueue, &fDelay);
				}

				// Start it up
				if(pdef->eType == kPowerType_Instant)
				{
					pActReq->bAnimateNow = false;
				}
				else
				{
					pActReq->bAnimateNow = !pchar->pPowActCurrent && pchar->fCooldownGlobalTimer<0;
				}
				
				pActReq->iPredictedIdx = eaFind(&ppow->ppSubPowers,ppowQueue);
				pact = CharacterQueuePower(	iPartitionIdx,
											pchar,
											eTarget,
											pObjectTarget,
											(bUseVecSourceDir ? vOverrideSourceDir : NULL),
											(bUseVecTarget ? vecTarget : NULL),
											vecTargetSecondary,
											ppowQueue,
											pdefQueue,
											uiTimeOfEvent,
											uiTimeEnterStance,
											pActReq->bAnimateNow,
											pActReq->uchActID,
											pActReq->iPredictedIdx,
											kLungeMode_None,
											&pActReq->puiActIDsCanceled);

				// Fill in some other stuff we need to send to the server
				if (!pdefQueue->bDisableConfuseTargeting)
				{
					character_clientCalulateConfuse(p,pact);
				}
				
				// set movement input bits that may be used for lurches
				pact->eInputDirectionBits = CharacterActivatePowerClient_GetInputDirectionBits();
				pActReq->eInputDirectionBits = pact->eInputDirectionBits;
				
				pActReq->erTarget = pact->erTarget;
				COPY_HANDLE(pActReq->hObjectNodeKey, pact->hTargetObject);
				pActReq->uiTimeQueued = pact->uiTimestampQueued;
				pActReq->uiTimeEnterStance = pact->uiTimestampEnterStance;
				pActReq->uiTimeCurrented = pact->uiTimestampCurrented;
				pActReq->pchPowerName = StructAllocString(pchName);
				pActReq->iLungePrediction = pact->eLungeMode;
				pActReq->erConfuseTarget = pact->erConfuseTarget;
				pActReq->perConfuseTargetList = pact->perConfuseTargetList;
				pActReq->uiConfuseSeed = pact->uiConfuseSeed;
				if (pProxAssistTarget)
				{
					pActReq->erProximityAssistTarget = entGetRef(pProxAssistTarget);
					pact->erProximityAssistTarget = pActReq->erProximityAssistTarget;
				}

				if (bUseVecTarget)
				{
					copyVec3(pact->vecTarget,pActReq->vecTarget);
					pActReq->bUseVecTarget = true;
				}
				if (bUseVecSourceDir)
				{
					copyVec3(pact->vecSourceDirection,pActReq->vecSourceDir);
					pActReq->bUseSourceDir = true;
				}
				
				if(pdef->fRangeSecondary || bUseVecSecondary)
				{
					if (vecTargetSecondary && !ISZEROVEC3(vecTargetSecondary))
						copyVec3(vecTargetSecondary, pActReq->vecTargetSecondary);
					else
						clientTarget_GetSimpleSecondaryRangeTarget(pdef->fRangeSecondary,pActReq->vecTargetSecondary);					
				}
				
				//Check confuse targeting
				character_confuseSetTarget(p,pact);

				// Now take a look at the actual power being activated
				ppow = character_ActGetPower(pchar,pact);
				pdef = GET_REF(ppow->hDef);
				pchName = pdef->pchName;

				// if this activation has a target and is not self targeted, inform the camera about the activation
				// the camera will react if it enables power activation lookat
				if (pact->erTarget && pact->erTarget != entGetRef(ent))
				{
					gclCamera_SetPowerActivationLookatOverride(pact);
				}


				// Start up the appropriate anim/fx
				if(pdef->eType != kPowerType_Instant)
				{
					character_ActRefEnhancements(iPartitionIdx,pchar,pact,NULL);
					character_ActRefAnimFX(pchar,pact);
					character_ActEventCreate(pchar,pact);

					// Enter the stance
					character_EnterStance(iPartitionIdx,pchar,ppow,pact,true,pact->uiTimestampEnterStance);
					character_EnterPersistStance(iPartitionIdx,pchar,ppow,pdef,pact,pact->uiTimestampEnterStance,0,false);

					// Turn to face the target
					character_MoveFaceStart(iPartitionIdx,pchar,pact,kPowerAnimFXType_None);
				}
				
				if(pact->pRefAnimFXMain)
				{
					if(pchar->eChargeMode==kChargeMode_Queued)
					{
						character_ScheduleAnimFxCharge(iPartitionIdx, pchar, pact, ppow);
					}
					else if(pchar->eChargeMode==kChargeMode_Overflow)
					{
						// Shouldn't happen on the client, but we'll handle it just in case
						character_ScheduleAnimFxCharge(iPartitionIdx, pchar, pact, ppow);
					}
					else if(pdef->eTracking!=kTargetTracking_UntilCurrent && 
							(!powerddef_ShouldDelayTargeting(pdef) ||
								CharacterQueuedPowerWillBecomeCurrentSoon(pchar)))
					{
						// Play activation fx
						Vec3 vecSourcePos,vecSourceDir,vecTargetFX;
						Entity *pentTarget = NULL;
						
						entGetCombatPosDir(pchar->pEntParent, pact, vecSourcePos, vecSourceDir);
						if (!pActReq->bUseSourceDir)
							copyVec3(vecSourceDir, pActReq->vecSourceDir);
						else
							copyVec3(pActReq->vecSourceDir, vecSourceDir);

						copyVec3(vecSourcePos, pActReq->vecSourcePos);
						entGetActivationSourcePosDir(pchar->pEntParent, pact, pdefQueue, vecSourcePos, vecSourceDir);
						pActReq->bDontDelayTargeting = true;

						if(character_ActFindTarget(iPartitionIdx,pchar,pact,
													vecSourcePos,vecSourceDir,
													&pentTarget,vecTargetFX))
						{
							character_MoveLungeStart(pchar,pact);
							character_AnimFXLunge(iPartitionIdx,pchar,pact);
							character_AnimFXActivateOn(	iPartitionIdx,
														pchar,
														NULL,
														pact,
														ppow,
														(pentTarget && pdef->fRangeSecondary <= 0.0f ? pentTarget->pChar : NULL),
														vecTargetFX,
														pact->uiTimestampActivate,
														pact->uchID,
														pact->uiPeriod,
														0);
						}
					}
					else if (powerddef_ShouldDelayTargeting(pdef))
					{
						PowersDebugPrintEnt(EPowerDebugFlags_ACTIVATE, pchar->pEntParent, "DelayedTargeting Power at %d\n",pact->uiTimestampActivate);
					}

				}

				// Send the request to the server
				poweractreq_FixCmdSend(pActReq);
				ServerCmd_entUsePowerServer(pActReq);
			}
			else
			{
				PowersDebugPrintEnt(EPowerDebugFlags_ACTIVATE, pchar->pEntParent, "Activate %d: FAIL: Queue denied\n", uchID);
				bSuccess = false;

				// Queuing was denied.  If it's because something else is queued that
				//  blocked this, then MAKE this one Power the MultiExec queue.
				if(eFail==kActivationFailureReason_PriorActNonInterrupting)
				{
					clientTarget_AddMultiPowerExecEx(pActReq->uiPowerID,false,true);
				}
			}
		}
		else
		{
			PowersDebugPrintEnt(EPowerDebugFlags_ACTIVATE, pchar->pEntParent, "Activate %d: FAIL: Character doesn't own power\n", uchID);
			bSuccess = false;
		}
	}
	else
	{
		int bSend = true;

		// Catch the case where we started activating a new charge/maintain without deactivating the
		//  prior activation; in that case avoid sending a deactivate for the old (which would cancel
		//  the new rather than the old, since the old has been deactivated by implication)
		if(pchar->eChargeMode > kChargeMode_None && ppow)
		{
			if(pchar->eChargeMode >= kChargeMode_QueuedMaintain
				&& pchar->pPowActQueued
				&& ppow->uiID!=pchar->pPowActQueued->ref.uiID
				&& (!pchar->pPowActCurrent
					|| ppow->uiID==pchar->pPowActCurrent->ref.uiID) )
			{
				// Charging the queue (or overflow) and the deactivate doesn't match the queue but
				//  does match the current
				bSend = false;
			}
			else if(pchar->eChargeMode < kChargeMode_QueuedMaintain
				&& pchar->pPowActCurrent
				&& ppow->uiID!=pchar->pPowActCurrent->ref.uiID )
			{
				// Charging the current and the deactivate doesn't match it but it does match a
				//  recently finished
				bSend = false;
			}
		}

		if(bSend)
		{
			U32 uiTimeCurrentedNew = character_PredictTimeForNewActivation(pchar,false,false,NULL,NULL);
			// Deactivate it
			pActReq->uchActID = 0;
			pActReq->eModeDeactivate = kChargeMode_None;

			if (pdef->bUpdateChargeTargetOnDeactivate && 
				pchar->pPowActCurrent && 
				pchar->eChargeMode == kChargeMode_Current)
			{	
				eTarget = NULL;					
				character_UpdateTargetingClient(iPartitionIdx, pchar, pdef, ppow, pActReq->vecTarget, &eTarget);
				pActReq->erTarget = (eTarget) ? entGetRef(eTarget) : 0;
				if (character_CurrentChargePowerUpdateVecTarget(pchar,pchar->pPowActCurrent->uchID,pActReq->vecTarget,pActReq->erTarget))
				{
					pActReq->bUpdateChargeVecTarget = true;
				}
			}

			character_ActDeactivate(iPartitionIdx,pchar,&pActReq->uchActID,&pActReq->eModeDeactivate,uiTimeOfEvent,uiTimeCurrentedNew,false);

			// Send the request to the server
			if(pActReq->uchActID)
			{
				pActReq->pchPowerName = StructAllocString(pchName);
				pActReq->uiTimeQueued = uiTimeOfEvent;
				pActReq->uiTimeCurrented = uiTimeCurrentedNew;
				poweractreq_FixCmdSend(pActReq);
				ServerCmd_entUsePowerServer(pActReq);
			}
		}
	}

	// Cleanup and return
	StructDestroy(parse_PowerActivationRequest,pActReq);

#endif

	return bSuccess;
#endif
}

static S32 DeactivatePowerServer(int iPartitionIdx, Character *pchar, Power *ppow, PowerActivation *pPowAct, GameAccountDataExtract *pExtract)
{
#if !GAMESERVER && !GAMECLIENT
	assert(0);
#else
	PowerActivationRequest req = {0};
	req.uiPowerID = ppow->uiID;
	req.bActivate = false;
	req.uiTimeQueued = pmTimestamp(0);
	req.uiTimeCurrented = character_PredictTimeForNewActivation(pchar,false,false,NULL,NULL);
	req.uchActID = pPowAct->uchID;
	req.eModeDeactivate = (pchar->eChargeMode<=kChargeMode_Current) ? pchar->eChargeMode : kChargeMode_None;

	return character_ActivatePowerServer(iPartitionIdx, pchar,&req,false,pExtract);
#endif
}

// Starts the given power at the given target.  Makes a lot of assumptions, generally used for AI power activation
S32 character_ActivatePowerServerBasic(int iPartitionIdx, Character *pchar, Power *ppow, Entity *eTarget, const Vec3 vecTarget, S32 bStart, S32 bCancelExisting, GameAccountDataExtract *pExtract)
{
#if !GAMESERVER && !GAMECLIENT
	assert(0);
#else
	if(bStart)
	{
		PowerActivationRequest req = {0};
		PowerDef *pdef = GET_REF(ppow->hDef);
		
		if (!eTarget && !vecTarget)
			return false;

		if(eTarget)
			req.erTarget = entGetRef(eTarget);
		else if(vecTarget)
		{
			copyVec3(vecTarget, req.vecTarget);
			req.bUseVecTarget = true;

			if (pdef && pdef->fRangeSecondary > 0)
			{
				copyVec3(vecTarget, req.vecTargetSecondary);
			}
		}

		req.uiPowerID = ppow->uiID;
		req.iPredictedIdx = -1;
		req.bActivate = bStart;
		req.uiTimeQueued = pmTimestamp(0);
		req.uiTimeCurrented = character_PredictTimeForNewActivation(pchar,pdef && pdef->bOverrides, pdef && pdef->bCooldownGlobalNotChecked, NULL, NULL);
		req.uiTimeEnterStance = req.uiTimeCurrented;
		req.bAnimateNow = false;
		req.bUnpredicted = true;
		req.bCancelExisting = !!bCancelExisting;
		if(!entCheckFlag(pchar->pEntParent,ENTITYFLAG_IS_PLAYER))
		{
			req.bAutoCommit = true;
		}
		req.uchActID = poweract_NextID();

		PowersDebugPrintEnt(EPowerDebugFlags_ACTIVATE, pchar->pEntParent, "Activate: Unpredicted activation request at %d/%d\n", req.uiTimeQueued,req.uiTimeCurrented);

		// For non-Players, update the AutoAttackServer state
		if(pdef && !entCheckFlag(pchar->pEntParent,ENTITYFLAG_IS_PLAYER))
		{
			if(pdef->bAutoAttackEnabler && !pchar->bAutoAttackServer)
			{
				pchar->bAutoAttackServer = true;
				pchar->bAutoAttackServerCheck = true;
			}
			else if(pdef->bAutoAttackDisabler && pchar->bAutoAttackServer)
			{
				pchar->bAutoAttackServer = false;
			}
		}

		return character_ActivatePowerServer(iPartitionIdx, pchar,&req,false,pExtract);
	}
	// otherwise for now we'll assume we're trying to deactivate a power
	else if(pchar->pPowActCurrent && pchar->pPowActCurrent->ref.uiID==ppow->uiID) 
	{
		return DeactivatePowerServer(iPartitionIdx, pchar,ppow,pchar->pPowActCurrent,pExtract);
	} 
	else if(pchar->pPowActQueued && pchar->pPowActQueued->ref.uiID==ppow->uiID) 
	{
		return DeactivatePowerServer(iPartitionIdx, pchar,ppow,pchar->pPowActQueued,pExtract);
	} 

	return false;
#endif
}

// Starts the given power at the given target.  Makes a lot of assumptions, generally used for AI power activation
bool character_ActivatePowerByNameServerBasic(int iPartitionIdx, Character *pchar, const char *pchName, Entity *eTarget, Vec3 vecTarget, bool bStart, GameAccountDataExtract *pExtract)
{
	Power *ppow = character_FindPowerByName(pchar,pchName);
	if(ppow)
	{
		return character_ActivatePowerServerBasic(iPartitionIdx,pchar,ppow,eTarget,vecTarget,bStart,false,pExtract);
	}
	else
	{
		return false;
	}
}


// Function to activate a power, usually called by the client
bool character_ActivatePowerByNameClient(int iPartitionIdx, Character *pchar, const char *pchPower, Entity *eTarget, Vec3 vecTargetSecondary, bool bStarting, GameAccountDataExtract *pExtract)
{
	Power *ppow = NULL;
	PowerDef *pdef = NULL;
	int i;
	
	PowersDebugPrintEnt(EPowerDebugFlags_ACTIVATE, pchar->pEntParent, "Activate: Starting power activation request %s by name %s\n",bStarting?"START":"STOP",pchPower);
	for(i=eaSize(&pchar->ppPowers)-1; i>=0; i--)
	{
		ppow = pchar->ppPowers[i];
		pdef = GET_REF(ppow->hDef);
		if(pdef && 0==stricmp(pdef->pchName,pchPower))
			break;
	}

	if(i>=0)
	{
		return CharacterActivatePowerClient(iPartitionIdx, pchar, eTarget, NULL, vecTargetSecondary, ppow, bStarting, pExtract);
	}
	else
	{
		return false;
	}

}

// Function to activate a power, usually called by the client
bool character_ActivatePowerByIDClient(int iPartitionIdx, Character *pchar, U32 uiID, Entity *eTarget, Vec3 vecTargetSecondary, bool bStarting, GameAccountDataExtract *pExtract)
{
	Power *ppow = NULL;

	PowersDebugPrintEnt(EPowerDebugFlags_ACTIVATE, pchar->pEntParent, "Activate: Starting power activation request %s by ID %d\n",bStarting?"START":"STOP",uiID);
	ppow = character_FindPowerByID(pchar,uiID);
	if(ppow)
	{
		return CharacterActivatePowerClient(iPartitionIdx, pchar, eTarget, NULL, vecTargetSecondary, ppow, bStarting, pExtract);
	}
	else
	{
		return false;
	}
}

// Function to activate a power, usually called by the client. The power is executed on an arbitrary point
bool character_ActivatePowerByIDClientOnArbitraryPoint(int iPartitionIdx, Character *pchar, U32 uiID, Vec3 vecTarget, Vec3 vecTargetSecondary, bool bStarting, GameAccountDataExtract *pExtract)
{
	Power *ppow = NULL;

	PowersDebugPrintEnt(EPowerDebugFlags_ACTIVATE, pchar->pEntParent, "Activate: Starting power activation request %s by ID %d\n", bStarting?"START":"STOP",uiID);
	ppow = character_FindPowerByID(pchar,uiID);
	if(ppow)
	{
		return CharacterActivatePowerClient(iPartitionIdx, pchar, NULL, vecTarget, vecTargetSecondary, ppow, bStarting, pExtract);
	}
	else
	{
		return false;
	}
}

// Finds the first item to use as the basis of a PowerApplication for a particular PowerDef
Item * character_DDWeaponPickSlot(Character *pchar, PowerDef *pdef, GameAccountDataExtract *pExtract, int slot)
{
	Item *pItem = NULL;
	Item **ppEquippedWeapons = NULL;

	character_WeaponPick(pchar, pdef, &ppEquippedWeapons, pExtract);

	if (ppEquippedWeapons && eaSize(&ppEquippedWeapons) > slot)
	{
		pItem = ppEquippedWeapons[slot];
	}

	eaDestroy(&ppEquippedWeapons);

	return pItem;
}

// Finds the Item to use as the basis of a PowerApplication for a particular PowerDef
void character_WeaponPick(Character *pchar, PowerDef *pdef, Item ***pppItems, GameAccountDataExtract *pExtract)
{
	if (pppItems && pchar)
	{
		Item *pItem = NULL;
		PowerCategory *pcat = NULL;
		int i = 0;
		
		// a hard-rule for entities created by a power's entCreate, use their creator's weapons
		do {
			if (pchar->uiPowersCreatedEntityTime && pchar->pEntParent->erCreator)
			{
				Entity *pCreator = entFromEntityRef(entGetPartitionIdx(pchar->pEntParent), pchar->pEntParent->erCreator);
				if (pCreator && pCreator->pChar)
				{
					pchar = pCreator->pChar;
				}
			}
		} while(++i < 2);

		for(i=eaiSize(&pdef->piCategories)-1; i>=0; i--)
		{
			pcat = eaGet(&g_PowerCategories.ppCategories,pdef->piCategories[i]);
			if (pcat && pcat->bWeaponBased)
			{
				inv_ent_FindEquippedItemsWithCategories(pchar->pEntParent, pcat->piRequiredItemCategories, pppItems, pExtract);
				return;
			}
		}
	}
}

// Sets the first weapon based category in the power in estrCatNameOut. If there is no weapon based category, the string is not touched.
bool character_GetWeaponBasedCategoryFromPowerDef(SA_PARAM_NN_VALID PowerDef *pdef, SA_PARAM_NN_VALID char **estrCatNameOut)
{
	if (pdef && eaiSize(&pdef->piCategories) > 0 && estrCatNameOut)
	{
		PowerCategory *pcat = NULL;
		S32 i;

		for (i = eaiSize(&pdef->piCategories) - 1; i >= 0; i--)
		{
			pcat = eaGet(&g_PowerCategories.ppCategories,pdef->piCategories[i]);
			if (pcat && pcat->bWeaponBased)
			{
				estrCopy2(estrCatNameOut, pcat->pchName);
				return true;
			}
		}
	}

	return false;
}

bool character_HasAllRequiredItemsEquipped(Character *pchar, PowerDef *pdef, GameAccountDataExtract *pExtract, char const ** ppchMissingCategory)
{
	Item *pItem = NULL;
	int* piCategories = NULL;
	PowerCategory* pcat = NULL;
	int i, j;
	int iMissingCategory;
	bool ret = false;

	for(i=eaiSize(&pdef->piCategories)-1; i>=0; i--)
	{
		pcat = eaGet(&g_PowerCategories.ppCategories,pdef->piCategories[i]);
		if (pcat)
		{
			for (j = 0; j < eaiSize(&pcat->piRequiredItemCategories); j++)
			{
				eaiPushUnique(&piCategories, pcat->piRequiredItemCategories[j]);
			}
		}
	}

	ret = inv_ent_HasAllItemCategoriesEquipped(pchar->pEntParent,piCategories,pExtract,&iMissingCategory);
	eaiDestroy(&piCategories);

	if (ret == 0)
	{
		Message * pResultMsg = StaticDefineGetMessage(ItemCategoryEnum, iMissingCategory);
		*ppchMissingCategory = TranslateMessagePtr(pResultMsg);
	}

	return ret;
}
