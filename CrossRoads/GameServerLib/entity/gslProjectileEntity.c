#include "ProjectileEntity.h"
#include "AttribMod.h"
#include "entCritter.h"
#include "EntityMovementManager.h"
#include "EntityMovementProjectileEntity.h"
#include "gslCostume.h"
#include "gslEntity.h"
#include "gslProjectileEntity.h"
#include "ResourceManager.h"
#include "ReferenceSystem.h"
#include "StringCache.h"
#include "Character.h"
#include "Character_combat.h"
#include "PowerEnhancements.h"
#include "PowersMovement.h"

#include "WorldColl.h"
#include "WorldLib.h"

#include "AutoGen/Entity_h_ast.h"
#include "AutoGen/Character_h_ast.h"

#define PROCESS_COUNTS_TILL_HIT		(MM_PROCESS_COUNTS_PER_SECOND/2)
#define CONSTRUCT_ENTITYHIT_AND_TIME(er,pc)		((((U64)(pc)) << 32) | (U64)(er))
#define GET_ENTITYHIT_AND_TIME(u,er,pc)			((er)=(U32)(u),(pc)=(U32)((u)>>32))

static void gslProjectile_HitScenery(Entity *pEnt, ProjectileEntity *pProj);
static bool gslProjectile_HitEntities(Entity *pEnt, ProjectileEntity *pProj);
void exprFuncProjectileResetHitEntities(ACMD_EXPR_SELF Entity *e);

static struct 
{
	const char *pchSelf;
	int hSelf;

	ExprContext*	pExprContext;

} g_ProjectileData = {0};


// -------------------------------------------------------------------------------------------------------------------
void gslProjectile_AutoRun()
{
	g_ProjectileData.pchSelf = allocAddStaticString("Self");
}


// -------------------------------------------------------------------------------------------------------------------
void gslProjectile_StaticInit()
{
	g_ProjectileData.pExprContext = exprContextCreate();
	
	exprContextSetAllowRuntimeSelfPtr(g_ProjectileData.pExprContext);

	// Functions
	//  Generic, Character, PowerDef, AttribMod
	{
		ExprFuncTable* stTable;
		stTable = exprContextCreateFunctionTable();
		exprContextAddFuncsToTableByTag(stTable, "projectile");
		exprContextSetFuncTable(g_ProjectileData.pExprContext, stTable);
	}

	// Data
	//  Self, SelfPtr - the character that is creating the critter
	exprContextSetSelfPtr(g_ProjectileData.pExprContext,NULL);
	exprContextSetPointerVarPooledCached(	g_ProjectileData.pExprContext,
											g_ProjectileData.pchSelf,
											NULL,
											parse_Entity,
											false,
											false, 
											&g_ProjectileData.hSelf);
	
}

// -------------------------------------------------------------------------------------------------------------------
bool gslProjectileDef_FixupExpressions(ProjectileEntityDef *pDef)
{
	int success = true;

	if (g_ProjectileData.pExprContext)
	{
		FOR_EACH_IN_EARRAY(pDef->ppExprOnQueuedDeath, Expression, pExpr)
			success &= exprGenerate(pExpr, g_ProjectileData.pExprContext);
		FOR_EACH_END

		FOR_EACH_IN_EARRAY(pDef->ppExprOnExpire, Expression, pExpr)
			success &= exprGenerate(pExpr, g_ProjectileData.pExprContext);
		FOR_EACH_END

		FOR_EACH_IN_EARRAY(pDef->ppExprOnCollideWorld, Expression, pExpr)
			success &= exprGenerate(pExpr, g_ProjectileData.pExprContext);
		FOR_EACH_END

		FOR_EACH_IN_EARRAY(pDef->ppExprOnHitEntity, Expression, pExpr)
			success &= exprGenerate(pExpr, g_ProjectileData.pExprContext);
		FOR_EACH_END
			
		FOR_EACH_IN_EARRAY(pDef->ppExprOnHitGotoEnt, Expression, pExpr)
			success &= exprGenerate(pExpr, g_ProjectileData.pExprContext);
		FOR_EACH_END
	}

	return success;
}

// -------------------------------------------------------------------------------------------------------------------
static Entity* gslProjectile_GetAttachEntity(S32 iPartitionIdx, 
											 EProjectileAttachOnCreate eAttachEnt, 
											 Entity *pCreatorEnt, 
											 EntityRef erTargetEnt)
{
	if (eAttachEnt == EProjectileAttachOnCreate_Owner)
		return pCreatorEnt;
	if (eAttachEnt == EProjectileAttachOnCreate_Target)
	{
		if (erTargetEnt)
		{
			return entFromEntityRef(iPartitionIdx, erTargetEnt);
		}
	}

	return NULL;
}

// -------------------------------------------------------------------------------------------------------------------
Entity* gslProjectile_CreateProjectile(S32 iPartitionIdx, ProjCreateParams *pCreateParams)
{
	Entity *pProjectileEnt = NULL;
	PERFINFO_AUTO_START_FUNC();

	
	if (pCreateParams && pCreateParams->pDef && pCreateParams->pvStartPos)
	{
		ProjectileEntityDef *pDef = pCreateParams->pDef;
		NOCONST(Entity) * nce;
		ProjectileEntity *pProj = NULL;
		Vec3 vInitialDirection = {0};
		Vec3 vSphereSweptOffset = {0};

		pProjectileEnt = gslCreateEntity(GLOBALTYPE_ENTITYPROJECTILE, iPartitionIdx);
		if (!pProjectileEnt)
			return NULL;
		nce = CONTAINER_NOCONST(Entity, pProjectileEnt);
		
		pProj = SAFE_MEMBER(pProjectileEnt, pProjectile);
		devassert(pProj);		
		
		// ENTITYFLAG_IGNORE ? todo find if we really want ignore or if ENTITYFLAG_UNTARGETABLE is enough
		entSetCodeFlagBits(pProjectileEnt,	ENTITYFLAG_PROJECTILE|ENTITYFLAG_UNTARGETABLE|
											ENTITYFLAG_UNSELECTABLE|ENTITYFLAG_DONOTFADE); 

		pProjectileEnt->fHue = pCreateParams->fHue;

		// initialize entity stuff
		costumeEntity_SetCostume(pProjectileEnt, GET_REF(pDef->hCostume), false);
		
		//  set up stuff from the owner 
		if (pCreateParams->pOwner)
		{
			CritterFaction *pFaction = entGetFaction(pCreateParams->pOwner);

			pProjectileEnt->erOwner = entGetRef(pCreateParams->pOwner);
			pProjectileEnt->erCreator = pProjectileEnt->erOwner;

			if (pFaction)
				SET_HANDLE_FROM_STRING(g_hCritterFactionDict, pFaction->pchName, nce->hFaction);
			
			gslEntity_UpdateMovementMangerFaction(iPartitionIdx, pProjectileEnt);
		}

		// create the character
		{
			if (!pProjectileEnt->pChar)
			{
				nce->pChar = StructCreateNoConst(parse_Character);
			}

			pProjectileEnt->pChar->bInvulnerable = true;
			pProjectileEnt->pChar->bUnstoppable = true;
			pProjectileEnt->pChar->bUnkillable = true;
			pProjectileEnt->pChar->bModsOwnedByOwner = true;
			pProjectileEnt->pChar->uiPowersCreatedEntityTime = pmTimestamp(0.f);

			if (pCreateParams->pOwner && pCreateParams->pOwner->pChar)
			{
				pProjectileEnt->pChar->iLevelCombat = pCreateParams->pOwner->pChar->iLevelCombat;
			}
			else
			{
				pProjectileEnt->pChar->iLevelCombat = 1;
			}
			
			character_Reset(iPartitionIdx, pProjectileEnt->pChar, pProjectileEnt, NULL);

			if (pProjectileEnt->pChar->pattrBasic)
			{
				pProjectileEnt->pChar->pattrBasic->fPerception = FLT_MAX * 0.5f;
				pProjectileEnt->pChar->pattrBasic->fPerceptionStealth = FLT_MAX * 0.5f;
			}
			
			// pProjectileEnt->pChar->bUnkillable = critter->bUnkillable;

		}

		// create and initialize the requester 
		{
			mmRequesterCreateBasic(pProjectileEnt->mm.movement, &pProj->pRequester, mrProjectileEntMsgHandler);
			devassert(pProj->pRequester);

			mmNoCollHandleCreateFG(pProjectileEnt->mm.movement, &pProj->pNoCollHandle, __FILE__, __LINE__);
			
			if (pDef->fInitialEntityHitDelay > 0)
			{
				mrProjectileEnt_InitialEntityHitDelay(pProj->pRequester, pDef->fInitialEntityHitDelay);
			}

			if (pDef->eProjectileType == EProjectileType_DEFAULT)
			{
				mrProjectileEnt_InitializeSettings(pProj->pRequester, pProjectileEnt->erOwner, &pDef->mmSettings);

				if (pDef->eAttachOnCreate == EProjectileAttachOnCreate_None)
				{
					// assuming only directional now
					if (pCreateParams->pvTargetPos)
					{
						subVec3(pCreateParams->pvTargetPos, pCreateParams->pvStartPos, vInitialDirection);
						mrProjectileEnt_SetDirection(pProj->pRequester, vInitialDirection);
					}
					else if (pCreateParams->pvDirection)
					{
						mrProjectileEnt_SetDirection(pProj->pRequester, pCreateParams->pvDirection);
						copyVec3(pCreateParams->pvDirection, vInitialDirection);
					}

					if (pCreateParams->pvTrajectorySourcePos && pCreateParams->pvTrajectorySourceDir)
					{
						mrProjectileEnt_MatchTrajectory(pProj->pRequester, pCreateParams->pvTrajectorySourcePos, pCreateParams->pvTrajectorySourceDir);
					}
				}
				else 
				{
					Entity *pAttachEnt = gslProjectile_GetAttachEntity(	iPartitionIdx, 
																		pDef->eAttachOnCreate, 
																		pCreateParams->pOwner, 
																		pCreateParams->erTarget);
					if (pAttachEnt)
					{
						mrProjectileEnt_AttachToEnt(pProj->pRequester,  
													entGetRef(pAttachEnt), 
													pCreateParams->pvCreateOffset);
					}
					else
					{	// could not find the attach entity, what do we do? 
						// for now just destroy the projectile
						gslProjectile_Destroy(pProjectileEnt);
						return NULL;
					}
				}
			}
			else if (pDef->eProjectileType == EProjectileType_BEAM)
			{	
				if (pCreateParams->pvTargetPos)
				{
					subVec3(pCreateParams->pvTargetPos, pCreateParams->pvStartPos, vSphereSweptOffset);
				}
				else if (pCreateParams->pvDirection)
				{
					scaleVec3(pCreateParams->pvDirection, pDef->mmSettings.fRange, vSphereSweptOffset);
				}

				mrProjectileEnt_InitializeAsSweptSphere(pProj->pRequester, 
														pProjectileEnt->erOwner, 
														&pDef->mmSettings, 
														vSphereSweptOffset);
			}
			else
			{
				devassert(0);
			}
		}

		// todo: probably need MovementBodyDesc to also allow spheres, but for now just use the capsules
		{
			MovementBodyDesc* bd;
	
			mmBodyDescCreate(&bd);

			// create the entity collision capsule
			{
				Capsule entCap = {0};
				entCap.fRadius = pDef->mmSettings.fEntityCollisionRadius ? pDef->mmSettings.fEntityCollisionRadius : 0.5f;

				if (pDef->eProjectileType == EProjectileType_DEFAULT)
				{
					entCap.fLength = 0.f;
					setVec3(entCap.vDir, 0.f, 1.f, 0.f);
				}
				else if (pDef->eProjectileType == EProjectileType_BEAM)
				{
					copyVec3(vSphereSweptOffset, entCap.vDir);
					if (lengthVec3Squared(entCap.vDir) > 0)
					{
						entCap.fLength = normalVec3(entCap.vDir);
					}
					else
					{
						entCap.fLength = 0.f;
						setVec3(entCap.vDir, 0.f, 1.f, 0.f);
					}
				}

				entCap.iType = 0;
				mmBodyDescAddCapsule(bd, &entCap);
			}
						
			if (pDef->mmSettings.fWorldCollisionRadius)
			{
				Capsule worldCap = {0};
				worldCap.fLength = 0.f;
				worldCap.fRadius = pDef->mmSettings.fWorldCollisionRadius;
				setVec3(worldCap.vDir, 0.f, 1.f, 0.f);
				worldCap.iType = 1;
				mmBodyDescAddCapsule(bd, &worldCap);
			}

			{
				MovementBody* b;
				mmBodyCreate(&b, &bd);

				mmrBodyCreateFG(pProjectileEnt->mm.movement,
								&pProjectileEnt->mm.movementBodyHandle,
								b,
								false,
								false);
			}
		}
				
		// ?
		//objSetDebugName(nce->debugName, MAX_NAME_LEN, GLOBALTYPE_ENTITYCRITTER, e->myContainerID, 0, def->pchName, NULL);

		if (pDef->fResetHitEntsTimer)
		{
			pProj->mfResetHitEntsTime = pDef->fResetHitEntsTimer;
		}
		

		// set the projectile def
		SET_HANDLE_FROM_STRING(g_hProjectileDefDict, pCreateParams->pDef->pchName, pProj->hProjectileDef);
		devassert(REF_HANDLE_IS_ACTIVE(pProj->hProjectileDef));
		
		{
			Vec3 vOverrideStartPos = {0};

			if (pCreateParams->bSnapToGround)
			{
				WorldCollCollideResults wcResults = {0};

				Vec3 vSnapToTarget;
				Vec3 vStartCast;

				copyVec3(pCreateParams->pvStartPos, vStartCast);
				vStartCast[1] += 3.f;
				copyVec3(pCreateParams->pvStartPos, vSnapToTarget);
				vSnapToTarget[1] -= 10.f;
				if (worldCollideRay(iPartitionIdx, vStartCast, vSnapToTarget, WC_FILTER_BIT_MOVEMENT, &wcResults))
				{
					scaleAddVec3(wcResults.normalWorld, pDef->mmSettings.fWorldCollisionRadius, wcResults.posWorldImpact, vOverrideStartPos);
					pCreateParams->pvStartPos = vOverrideStartPos;
					mrProjectileEnt_BeginHugGround(pProj->pRequester);
				}
			}

			entSetPos(pProjectileEnt, pCreateParams->pvStartPos, true, "ProjCreateAttribMod");
		}
		


		if (!vec3IsZero(vInitialDirection))
		{
			Vec3 pyr = {0};
			Quat rot;
			pyr[1] = getVec3Yaw(vInitialDirection);
			pyr[0] = getVec3Pitch(vInitialDirection);
			PYRToQuat(pyr, rot);
			entSetRot(pProjectileEnt, rot, true, "ProjCreateAttribMod");
		}
		else if (pCreateParams->pqRot)
		{
			entSetRot(pProjectileEnt, pCreateParams->pqRot, true, "ProjCreateAttribMod");
		}
	}

	PERFINFO_AUTO_STOP();
	return pProjectileEnt;
}

// -------------------------------------------------------------------------------------------------------------------
void gslProjectile_Destroy(Entity *pEnt)
{
	ProjectileEntity *pProj = SAFE_MEMBER(pEnt, pProjectile);
	if (pProj)
	{
		REMOVE_HANDLE(pProj->hProjectileDef);
		if (pProj->pNoCollHandle)
		{
			mmNoCollHandleDestroyFG(&pProj->pNoCollHandle);
		}

		if (pProj->pRequester)
		{
			mrDestroy(&pProj->pRequester);
		}

		if (pProj->eaEntitiesHitAndTime)
		{
			ea64Destroy(&pProj->eaEntitiesHitAndTime);
		}
				

		free(pProj);
		pEnt->pProjectile = NULL;
	}
}


#define PROJECTILEFLAG_DEAD	(ENTITYFLAG_DEAD|ENTITYFLAG_DESTROY)

// -------------------------------------------------------------------------------------------------------------------
static void gslProjectile_Die(SA_PARAM_NN_VALID Entity *pEnt)
{
	ProjectileEntity *pProj = SAFE_MEMBER(pEnt, pProjectile);

	// flag it as dead
	entSetCodeFlagBits(pEnt, ENTITYFLAG_DEAD);
	
	if (pProj->pRequester)
	{
		mrDestroy(&pProj->pRequester);
		pProj->pRequester = NULL;
	}

	pProj->fTimeToLinger = 3.f;

}

// -------------------------------------------------------------------------------------------------------------------
__forceinline static void gslProjectile_ApplyPower(S32 iPartitionIdx, Entity *pEnt, PowerDef *pPowerDef, EntityRef erTargetEnt)
{
	static Power **s_eaPowEnhancements = NULL;
	ApplyUnownedPowerDefParams applyParams = {0};
		
	// go through all the enhancement powers on the projectile (they should probably all just be enhancements?) 
	// and see what ones would attach to the given powerDef 
	FOR_EACH_IN_EARRAY(pEnt->pChar->ppPowers, Power, pPower)
	{
		PowerDef *pEnhancePowerDef = GET_REF(pPower->hDef);
		if(pEnhancePowerDef && pEnhancePowerDef->eType == kPowerType_Enhancement)
		{
			if(power_EnhancementAttachIsAllowed(iPartitionIdx,pEnt->pChar,pEnhancePowerDef,pPowerDef,false))
			{
				eaPush(&s_eaPowEnhancements, pPower);
			}
		}
	}
	FOR_EACH_END

	applyParams.pppowEnhancements = s_eaPowEnhancements;
	applyParams.erTarget = erTargetEnt;
	applyParams.pcharSourceTargetType = pEnt->pChar;
	applyParams.iLevel = pEnt->pChar->iLevelCombat;
	applyParams.fTableScale = 1.f;
	applyParams.ppStrengths = pEnt->pChar->ppApplyStrengths;
	applyParams.fHue = pEnt->fHue;
	
	character_ApplyUnownedPowerDef(iPartitionIdx, pEnt->pChar, pPowerDef, &applyParams);
		
	eaClear(&s_eaPowEnhancements);
}

// -------------------------------------------------------------------------------------------------------------------
static ProjectileEntityDef* gslProjectile_GetProjectileDef(SA_PARAM_NN_VALID Entity *pEnt)
{
	ProjectileEntity *pProj = SAFE_MEMBER(pEnt, pProjectile);

	devassertmsg(entGetType(pEnt)==GLOBALTYPE_ENTITYPROJECTILE, "A NON-Projectile entity used for glsProjectile_Expire!");
	if (!pProj || !pEnt->pChar)
	{		
		// spit out error
		return NULL;
	}

	return GET_REF(pProj->hProjectileDef);
}

// -------------------------------------------------------------------------------------------------------------------
void gslProjectile_UpdateProjectile(F32 fDTime, Entity *pEnt)
{
	if (entIsAlive(pEnt))
	{
		ProjectileEntity *pProj = SAFE_MEMBER(pEnt, pProjectile);
		
		if (!pProj)
		{
			gslProjectile_Die(pEnt);
			return;
		}
		
		if (pProj->muQueueDieTime)
		{
			if (mmGetProcessCountAfterMillisecondsFG(0) >= pProj->muQueueDieTime)
			{
				gslProjectile_Die(pEnt);
			}
		}
		else if (pProj->mfResetHitEntsTime)
		{
			U32 currTime = mmGetProcessCountAfterMillisecondsFG(0);
			S32 i;
			for (i = ea64Size(&pProj->eaEntitiesHitAndTime) - 1; i >= 0; --i)
			{
				U64 entityHitAndTime = pProj->eaEntitiesHitAndTime[i];
				EntityRef erRef;
				U32 hitTime;
				
				GET_ENTITYHIT_AND_TIME(entityHitAndTime, erRef, hitTime);
				
				if (currTime > (hitTime + pProj->mfResetHitEntsTime * MM_PROCESS_COUNTS_PER_SECOND))
				{
					ProjectileEntityDef *pDef = GET_REF(pProj->hProjectileDef);
					
					ea64RemoveFast(&pProj->eaEntitiesHitAndTime, i);

					if (!pDef || pDef->bDecrementNumEntitiesHitCounterOnReset)
						pProj->iNumEntitiesHit--;

					mrProjectileEnt_ClearHitEntity(pProj->pRequester, erRef);
				}
			}
			
			
		}
	}
	else
	{
		ProjectileEntity *pProj = SAFE_MEMBER(pEnt, pProjectile);
		if (pProj)
		{
			pProj->fTimeToLinger -= fDTime;
		}
	}
}

// -------------------------------------------------------------------------------------------------------------------
static void gslProjectile_Eval(Entity *pEnt, Expression *pExpr)
{
	if(g_ProjectileData.pExprContext)
	{
		bool bValid = false;
		MultiVal mv = {0};
		
		exprContextSetSelfPtr(g_ProjectileData.pExprContext, pEnt);
		exprContextSetPartition(g_ProjectileData.pExprContext, entGetPartitionIdx(pEnt));
		exprEvaluate(pExpr, g_ProjectileData.pExprContext, &mv);
	}
	
	exprContextSetSelfPtr(g_ProjectileData.pExprContext, NULL);
}

// -------------------------------------------------------------------------------------------------------------------
#define gslProjectile_OnQueueDieTime(pEnt, pDef, pProj) gslProjectile_OnQueueDieTimeEx(pEnt,pDef,pProj,false)
static void gslProjectile_OnQueueDieTimeEx(Entity *pEnt, ProjectileEntityDef *pDef, ProjectileEntity *pProj, bool bIgnoreEventExpr)
{
	// if we're still queued for death, send the on death event
	if (!bIgnoreEventExpr)
	{
		Expression *pExpr = eaGet(&pDef->ppExprOnQueuedDeath, pProj->miOnQueueDeathIndex);
		pProj->miOnQueueDeathIndex++;
		if (pExpr)
		{
			gslProjectile_Eval(pEnt, pExpr);
		}
	}
	

	if (pProj->muQueueDieTime)
	{	// still dying, play the apply power 
		PowerDef *pPowerDef = GET_REF(pDef->hPowerOnDeath);
		
		if (pPowerDef)
		{
			gslProjectile_ApplyPower(entGetPartitionIdx(pEnt), pEnt, pPowerDef, pEnt->erCreator);	
		}
	}
}

// -------------------------------------------------------------------------------------------------------------------
static void gslProjectile_HitScenery(Entity *pEnt, ProjectileEntity *pProj)
{
	S32 iPartitionIdx = entGetPartitionIdx(pEnt);
	PowerDef *pPowerDef;
	ProjectileEntityDef *pDef;

	pDef = gslProjectile_GetProjectileDef(pEnt);
	if (!pDef)
	{	// spit out error
		gslProjectile_Die(pEnt);
		return;
	}

	// apply the power
	pPowerDef = GET_REF(pDef->hPowerOnCollideWorld);
	if (pPowerDef)
	{
		gslProjectile_ApplyPower(iPartitionIdx, pEnt, pPowerDef, 0);	
	}

	// Send the events
	pProj->muQueueDieTime = mmGetProcessCountAfterMillisecondsFG(PROCESS_COUNTS_TILL_HIT);
	
	{
		Expression *pExpr = eaGet(&pDef->ppExprOnCollideWorld, pProj->miOnCollideWorldIndex);
		pProj->miOnCollideWorldIndex++;
		if (pExpr)
			gslProjectile_Eval(pEnt, pExpr);
	}

	if (pProj->muQueueDieTime)
		gslProjectile_OnQueueDieTime(pEnt, pDef, pProj);
}


// -------------------------------------------------------------------------------------------------------------------
void gslProjectile_HitSceneryToFG(Entity *pEnt)
{
	ProjectileEntity *pProj = SAFE_MEMBER(pEnt, pProjectile);

	if (!entIsAlive(pEnt) || !pProj)
		return;

	gslProjectile_HitScenery(pEnt, pProj);
}

// -------------------------------------------------------------------------------------------------------------------
void gslProjectile_Expire(Entity *pEnt, bool bIgnoreExpireEventExpression)
{
	S32 iPartitionIdx = entGetPartitionIdx(pEnt);
	PowerDef *pPowerDef;
	ProjectileEntityDef *pDef;
	ProjectileEntity *pProj = SAFE_MEMBER(pEnt, pProjectile);
	
	if (!entIsAlive(pEnt))
		return;

	pDef = gslProjectile_GetProjectileDef(pEnt);
	if (!pDef)
	{
		// spit out error
		gslProjectile_Die(pEnt);
		return;
	}

	pPowerDef = GET_REF(pDef->hPowerOnExpire);
	if (pPowerDef)
	{
		gslProjectile_ApplyPower(iPartitionIdx, pEnt, pPowerDef, 0);	
	}

	if (pProj)
	{
		pProj->muQueueDieTime = mmGetProcessCountAfterMillisecondsFG(PROCESS_COUNTS_TILL_HIT);
		
		// call the expire event expression
		if (bIgnoreExpireEventExpression)
		{
			Expression *pExpr = eaGet(&pDef->ppExprOnExpire, pProj->miOnExpireIndex);
			pProj->miOnExpireIndex++;
			if (pExpr)
				gslProjectile_Eval(pEnt, pExpr);
		}

		if (pProj->muQueueDieTime)
			gslProjectile_OnQueueDieTimeEx(pEnt, pDef, pProj, bIgnoreExpireEventExpression);
	}

}


// -------------------------------------------------------------------------------------------------------------------
static bool gslProjectile_HitEntities(Entity *pEnt, ProjectileEntity *pProj)
{
	S32 i, s = ea64Size(&pProj->eaEntitiesHitAndTime);
	S32 iMaxHitThisFrame = INT_MAX, iHitThisFrame = 0;
	S32 maxAllowed = INT_MAX;
	S32 iPartitionIdx = entGetPartitionIdx(pEnt);
	ProjectileEntityDef *pDef;
	PowerDef *pPowerDef = NULL;
	U32 currTime;
	bool bHitGotoEnt = false;
	
	if (pProj->muQueueDieTime)
		return false; // already queued for death

	pDef = gslProjectile_GetProjectileDef(pEnt);
	if (!pDef)
	{
		return false;
	}

	if (pDef->mmSettings.iMaxHitEntities)
	{	// make sure we aren't going to hit more than the max number of entities
		maxAllowed = pDef->mmSettings.iMaxHitEntities - pProj->iNumEntitiesHit;
		if (!maxAllowed)
			return false; // already hit max
		iMaxHitThisFrame = MIN(maxAllowed, s);
	}

	currTime = mmGetProcessCountAfterMillisecondsFG(0);
	
	pProj->muQueueDieTime = mmGetProcessCountAfterMillisecondsFG(PROCESS_COUNTS_TILL_HIT);

	for (i = 0; i < s; ++i)
	{
		U64 entityHitAndTime = pProj->eaEntitiesHitAndTime[i];
		EntityRef erRef;
		U32 hitTime;
		Entity *e;
		
		GET_ENTITYHIT_AND_TIME(entityHitAndTime, erRef, hitTime);
		if (hitTime <= pProj->uiLastProcessHitTime)
		{
			continue;
		}

		e = entFromEntityRef(iPartitionIdx, erRef);
		if (!e)
		{
			pProj->iNumEntitiesHit++;
			continue;
		}

		if (currTime >= hitTime)
		{
			iHitThisFrame++;
			PowersDebugPrintEnt(EPowerDebugFlags_PROJECTILE, pEnt, "Projectile: Applying Power To Hit Entity %d\n", erRef);

			if (pProj->mfResetHitEntsTime)
			{
				// if we are resetting the hit list, set the hit time to when we're actually applying the power
				U64 *pentityHitAndTime = pProj->eaEntitiesHitAndTime + i;
				(*pentityHitAndTime) = CONSTRUCT_ENTITYHIT_AND_TIME(erRef, currTime);
			}
			
			if (!pPowerDef)
			{
				pPowerDef = GET_REF(pDef->hPowerOnHitEnt);
			}

			if (pProj->erGotoEnt == erRef)
			{
				PowerDef *pOnHitEntPowerDef = GET_REF(pDef->hPowerOnHitGotoEnt);

				bHitGotoEnt = true;

				// use the OnHitEntPowerDef? otherwise default the hPowerOnHitEnt
				if (!pOnHitEntPowerDef)
				{
					pOnHitEntPowerDef = pPowerDef;
				}

				if (pOnHitEntPowerDef)
				{
					gslProjectile_ApplyPower(iPartitionIdx, pEnt, pOnHitEntPowerDef, erRef);	
				}

				// do the hit gotoEnt expression
				{
					Expression *pExpr = eaGet(&pDef->ppExprOnHitGotoEnt, pProj->miOnHitGotoEnt);
					pProj->miOnHitGotoEnt++;
					if (pExpr)
						gslProjectile_Eval(pEnt, pExpr);
				}
			}
			else if (pPowerDef)
			{
				gslProjectile_ApplyPower(iPartitionIdx, pEnt, pPowerDef, erRef);	
			}

			// do the hit entity expression
			{
				Expression *pExpr = eaGet(&pDef->ppExprOnHitEntity, pProj->miOnHitEntityIndex);
				pProj->miOnHitEntityIndex++;
				if (pExpr)
					gslProjectile_Eval(pEnt, pExpr);
			}
						
			pProj->iNumEntitiesHit++;

			if (iHitThisFrame >= iMaxHitThisFrame)
				break;
		}
	}

	pProj->uiLastProcessHitTime = currTime;

		
	if (bHitGotoEnt || 
		(pDef->mmSettings.iMaxHitEntities && pProj->iNumEntitiesHit >= pDef->mmSettings.iMaxHitEntities))
	{
		if (pProj->muQueueDieTime)	// if the queued die wasn't cleared, call the expression event
			gslProjectile_OnQueueDieTime(pEnt, pDef, pProj);
		return false;
	}
	else
	{
		pProj->muQueueDieTime = 0;
	}

	
	return true;
}

// -------------------------------------------------------------------------------------------------------------------
void gslProjectile_HitEntitiesToFG(Entity *pEnt, EntityRef *eaiHitEntities)
{
	ProjectileEntity *pProj = SAFE_MEMBER(pEnt, pProjectile);
	U32 time;
	S32 i;
	if (!entIsAlive(pEnt) || !pProj)
		return;

	time = mmGetProcessCountAfterMillisecondsFG(0);
	for(i = eaiSize(&eaiHitEntities) - 1; i >= 0; --i)
	{
		EntityRef erRef = eaiHitEntities[i];
		U64 entityHitAndTime = CONSTRUCT_ENTITYHIT_AND_TIME(erRef,time);

		ea64Push(&pProj->eaEntitiesHitAndTime, entityHitAndTime);
	}
	
	gslProjectile_HitEntities(pEnt, pProj);
}

// -------------------------------------------------------------------------------------------------------------------
bool gslProjectile_ShouldDestroy(Entity *pEnt)
{
	ProjectileEntity *pProj = SAFE_MEMBER(pEnt, pProjectile);

	if (!entIsAlive(pEnt) && (!pProj || pProj->fTimeToLinger <= 0.f))
	{
		return true;
	}

	return false;
}

// -------------------------------------------------------------------------------------------------------------------
// expressions
// -------------------------------------------------------------------------------------------------------------------

AUTO_EXPR_FUNC(projectile) ACMD_NAME(ProjectileReturnToOwner);
void exprFuncProjectileReturnToOwner(ACMD_EXPR_SELF Entity *e)
{
	if (e && entIsProjectile(e) && e->pProjectile)
	{
		ProjectileEntity *pProj = e->pProjectile;

		pProj->erGotoEnt = e->erCreator;

		mrProjectileEnt_GotoEntity(pProj->pRequester, e->erCreator);
		pProj->muQueueDieTime = 0;
	}
	else
	{
		// error
	}
}

// -------------------------------------------------------------------------------------------------------------------

// Resets all things that would expire the projectile, like range and lifetime
AUTO_EXPR_FUNC(projectile) ACMD_NAME(ProjectileResetExpirations);
void exprFuncProjectileResetExpirations(ACMD_EXPR_SELF Entity *e)
{
	if (e && entIsProjectile(e) && e->pProjectile)
	{
		ProjectileEntity *pProj = e->pProjectile;

		mrProjectileEnt_ResetExpirations(pProj->pRequester);
		pProj->muQueueDieTime = 0;
	}
	else
	{
		// error
	}
}

// -------------------------------------------------------------------------------------------------------------------

// Resets the list of entities hit by the projectile and allows the projectile to hit them again
AUTO_EXPR_FUNC(projectile) ACMD_NAME(ProjectileResetHitEntities);
void exprFuncProjectileResetHitEntities(ACMD_EXPR_SELF Entity *e)
{
	if (e && entIsProjectile(e) && e->pProjectile)
	{
		ProjectileEntity *pProj = e->pProjectile;

		ea64Clear(&pProj->eaEntitiesHitAndTime);
		pProj->iNumEntitiesHit = 0;
		
		mrProjectileEnt_ResetHitEntities(pProj->pRequester);
		pProj->muQueueDieTime = 0;
	}
	else
	{
		// error
	}
}

// -------------------------------------------------------------------------------------------------------------------

// overrides the range of the projectile
AUTO_EXPR_FUNC(projectile) ACMD_NAME(ProjectileOverrideRange);
void exprFuncProjectileOverrideRange(ACMD_EXPR_SELF Entity *e, F32 fRange)
{
	if (e && entIsProjectile(e) && e->pProjectile)
	{
		ProjectileEntity *pProj = e->pProjectile;
		mrProjectileEnt_OverrideRange(pProj->pRequester, fRange);
		pProj->muQueueDieTime = 0;
	}
	else
	{
		// error
	}
}

// -------------------------------------------------------------------------------------------------------------------

// returns an entity that is a projectile and optionally 
AUTO_EXPR_FUNC(entity) ACMD_NAME(CharacterGetProjectileByTag);
SA_RET_OP_VALID Entity* exprFuncCharacterGetProjectileByAttribTag(ACMD_EXPR_SELF Entity *e, const char *pchTag)
{
	S32 iTag = -1;
	
	if (pchTag && pchTag[0] != 0)
		iTag = StaticDefineIntGetInt(PowerTagsEnum,pchTag);

	FOR_EACH_IN_EARRAY(e->pChar->modArray.ppMods, AttribMod, pMod)
	{
		if (pMod->erCreated)
		{
			AttribModDef *pDef = pMod->pDef;
			if (pDef->offAttrib == kAttribType_ProjectileCreate)
			{
				
				if (iTag == -1 || powertags_Check(&pDef->tags, iTag))
				{
					Entity *pEnt = entFromEntityRef(entGetPartitionIdx(e), pMod->erCreated);
					return pEnt;
				}
			}
		}
	}
	FOR_EACH_END

	return NULL;
}


