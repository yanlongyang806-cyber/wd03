#include "EntityMovementProjectileEntity.h"
#include "../wcoll/collide.h"
#include "EntityMovementManagerPrivate.h"
#include "Entity.h"
#include "entCritter.h"
#include "EntityMovementManager.h"
#include "gslProjectileEntity.h"
#include "mathutil.h"
#include "ProjectileEntity.h"
#include "WorldColl.h"

AUTO_RUN_ANON(memBudgetAddMapping(__FILE__, BUDGET_Physics););



AUTO_RUN_MM_REGISTER_REQUESTER_MSG_HANDLER(	mrProjectileEntMsgHandler,
											"ProjectileEntMovement",
											ProjectileEnt);

typedef enum EProjectileMovementType
{
	EProjectileMovementType_LINEAR,
	EProjectileMovementType_VELOCITY,
} EProjectileMovementType;

AUTO_STRUCT;
typedef struct ProjectileHitTarget
{
	EntityRef	erEnt;
	F32			fHitTime;
} ProjectileHitTarget;


// ----------------------------------------------------------------
AUTO_STRUCT;
typedef struct ProjectileEntBG 
{
	// state
	Vec3	vVelocity;
	Vec3	vDirection;

	// vVecParam1 - used for different features:
	// TrajectoryMatching: the direction the projectile will be matching
	// EntityAttaching: the offset from the entity
	Vec3	vVecParam1;

	// vVecParam2 - used for different features:
	// TrajectoryMatching: the base position the trajectory originates from
	// SphereSweep: the offset from the current position
	Vec3	vVecParam2;

	F32		fTravelDist;
	F32		fSweptSphereDist;
	
	F32		fLinearGravity;
	EntityRef erCreator;
	EntityRef erGotoEnt;

	EntityRef *eaiHitEntities;
	ProjectileHitTarget **eaFrameHitTargets;
	ProjectileHitTarget **eaAllocedHitTargets;

	U32		spcProjectileInit;
	U32		spcProjectileDelayHit;

	ProjectileEntityMMSettings		settings;

	U32		bOnGround : 1;
	U32		bMatchTrajectory : 1;
	U32		bExpiredOnHitScenery : 1;
	U32		bExpired : 1;
	U32		bAsSphereSwept : 1;
	U32		bAttachedToTargetEnt : 1;
} ProjectileEntBG;

// ----------------------------------------------------------------
AUTO_STRUCT;
typedef struct ProjectileEntLocalBG 
{
	S32 unused;
} ProjectileEntLocalBG;

// ----------------------------------------------------------------
AUTO_STRUCT;
typedef struct ProjectileEntToFG 
{
	EntityRef *eaiHitEntities;

	U32		bHitEntities : 1;
	U32		bHitGotoEntity : 1;

	U32		bExpireOnTimeOrRange : 1;	
	U32		bExpiredOnHitScenery : 1;
	U32		bToFGEnabled : 1;
} ProjectileEntToFG;

// ----------------------------------------------------------------
AUTO_STRUCT;
typedef struct ProjectileEntToBGFlags 
{
	U32			bStartHugGround : 1;
	U32			bResetExpires : 1;
	U32			bResetHitEntities : 1;

	// on initialization, several fields in ProjectileEntToBG are assumed set
	// some of these flags override or hide certain functionality
	U32			bInitialization : 1;	
	U32			bDirectionSet : 1;
	U32			bMatchTrajectorySet : 1;
	U32			bGotoEntitySet : 1;
	U32			bOverridesSet : 1;
	U32			bInitAsSweptSphere : 1;	
	U32			bAttachEntitySet : 1;
	U32			bDelayHitEntities : 1;
	
} ProjectileEntToBGFlags;

// ----------------------------------------------------------------
AUTO_STRUCT;
typedef struct ProjectileEntToBG 
{
	Vec3		vDirection;
	Vec3		vVecParam1;
	Vec3		vVecParam2;
	EntityRef	*eaiClearHitEnts;
	EntityRef	erCreator;
	EntityRef	erGotoEnt;
	ProjectileEntityMMSettings	settings;

	F32			fOverrideRange;
	F32			fDelayHitTime;
		
	ProjectileEntToBGFlags	flags;
	
} ProjectileEntToBG;

// ----------------------------------------------------------------
AUTO_STRUCT;
typedef struct ProjectileEntFG 
{
	ProjectileEntToBG			toBG;

} ProjectileEntFG;


AUTO_STRUCT;
typedef struct ProjectileEntSync
{
	S32	unused;
	 
} ProjectileEntSync;



// -------------------------------------------------------------------------------------------------------------------------------
static void mrProjectileEnt_EnableMsgUpdatedToFG(const MovementRequesterMsg* msg, ProjectileEntToFG* toFG)
{
	if(FALSE_THEN_SET(toFG->bToFGEnabled))
	{
		mrmEnableMsgUpdatedToFG(msg);
	}
}

// -------------------------------------------------------------------------------------------------------------------------------
static __forceinline S32 mrProjectileEnt_ShouldCollideFaction(MovementManager* mm, MovementManager* other, ProjectileEntBG *bg)
{
	if (mgState.factionMatrix)
	{
		EntityRelation relation = kEntityRelation_Friend;

		if (mm->fg.factionIndex < CFM_MAX_FACTIONS && other->fg.factionIndex < CFM_MAX_FACTIONS)
		{
			relation = mgState.factionMatrix->relations[mm->fg.factionIndex][other->fg.factionIndex];
		}
		
		if (bg->settings.bHitsEnemies && (relation == kEntityRelation_Foe || relation == kEntityRelation_FriendAndFoe))
			return true;

		if (bg->settings.bHitsFriendlies && (relation == kEntityRelation_Friend || relation == kEntityRelation_FriendAndFoe))
			return true;

		if (bg->settings.bHitsOwner && other->entityRef == bg->erCreator)
			return true;

		return false;
	}
	return true;
}

// -------------------------------------------------------------------------------------------------------------------------------
static S32 mrProjectileEnt_DoCollisionEntsInCell(	MovementManager* mm,
													ProjectileEntBG *bg,
													const MovementManagerGridCell* cell,
													const Vec3 vStartPos,
													const Vec3 vEndPos,
													F32 fProjRadius, 
													const Vec3 vSweepMid,
													F32 fSweepRadius)
{
	S32 result = 0;

	EARRAY_CONST_FOREACH_BEGIN(cell->managers, i, size);
	{
		MovementManager* mmOther = cell->managers[i];
		bool bGotoEnt = (mmOther->entityRef == bg->erGotoEnt) && !bg->bAttachedToTargetEnt;
		
		if(	!mmOther->bg.flags.noCollision && 
			(bGotoEnt || (bg->settings.bHitsOwner || mmOther->entityRef != bg->erCreator)) &&
			mm != mmOther &&	// don't collide with itself
			(bGotoEnt || mrProjectileEnt_ShouldCollideFaction(mm, mmOther, bg)) && // check faction 
			(eaiFind(&bg->eaiHitEntities, mmOther->entityRef) < 0) // make sure we haven't hit it yet
			)
		{
			Vec3					pos0;
			Quat					rot0;
			const Capsule*const*	mmOtherCaps = NULL;
			U32						mmOtherCapSet = 0;
			F32						fMinHitTime = FLT_MAX;
			
			copyVec3(mmOther->bg.pos, pos0);
			copyQuat(mmOther->bg.rot, rot0);
			
			if (!sphereSphereCollision(vSweepMid, fSweepRadius, pos0, mmOther->bg.bodyRadius))
				continue;	

			mmGetCapsulesBG(mmOther, &mmOtherCaps);

			// Negative collision set means to use the alternate capsule set
			if(mmOther->fg.collisionSet < 0)
			{
				EARRAY_CONST_FOREACH_BEGIN(mmOtherCaps, j, jSize);
				{
					if(mmOtherCaps[j]->iType == 1)
					{
						mmOtherCapSet = 1;
					}
				}
				EARRAY_FOREACH_END;
			}
		

			EARRAY_CONST_FOREACH_BEGIN(mmOtherCaps, k, kSize);
			{
				const Capsule*	cap0 = mmOtherCaps[k];
				F32				fHitTime;

				if(cap0->iType != mmOtherCapSet)
					continue;
				
				if (SphereSweepVsCapsule(cap0, pos0, rot0, vStartPos, vEndPos, fProjRadius, &fHitTime))
				{
					if (fHitTime < fMinHitTime)
					{
						fMinHitTime = fHitTime;
					}
					result = 1;
				}
			}
			EARRAY_FOREACH_END;

			// todo: keep track of the hit time and hit position for this entity
			if (fMinHitTime != FLT_MAX)
			{
				if (bg->settings.iMaxHitEntities)
				{	// if we have a maxnumber of hit entities, we need to record the time the entity was hit at
					// so we can resolve any multiple entities hit per frame
					ProjectileHitTarget *pHitTarget = eaPop(&bg->eaAllocedHitTargets);
					if (!pHitTarget)
						pHitTarget = malloc(sizeof(ProjectileHitTarget));

					if (pHitTarget)
					{
						pHitTarget->erEnt = mmOther->entityRef;
						pHitTarget->fHitTime = fMinHitTime;
						eaPush(&bg->eaFrameHitTargets, pHitTarget);
					}
				}
				else
				{	// just add it immediately since we don't care how many we hit
					eaiPush(&bg->eaiHitEntities, mmOther->entityRef);
				}
			}
		}
	}
	EARRAY_FOREACH_END;
	
	return result;
}


// -------------------------------------------------------------------------------------------------------------------------------
static S32 mrProjectileEnt_DoCollisionEnts(	MovementManager* mm, 
											ProjectileEntBG *bg, 
											const Vec3 vStartPos, 
											const Vec3 vEndPos)
{
	S32		result = 0;
	Vec3	vMin, vMax, vMid;
	F32		fSweepRadius;
	IVec3	lo, hi;

	if(!mm->space)
		return 0;

	PERFINFO_AUTO_START_FUNC();
	
	// calculate the min/max bounding box for the projectile
	FOR_BEGIN(i, 3);
		if (vStartPos[i] < vEndPos[i])
		{		
			vMin[i] = vStartPos[i];		
			vMax[i] = vEndPos[i];		
		}
		else
		{							
			vMin[i] = vEndPos[i];		
			vMax[i] = vStartPos[i];		
		}
	FOR_END;

	interpVec3(0.5f, vMin, vMax, vMid);
	fSweepRadius = distance3(vMid, vMin) + bg->settings.fEntityCollisionRadius;
	
	ARRAY_FOREACH_BEGIN(mmGridSizeGroups, sizeIndex);
	{
		S32 x, y, z;
		MovementManagerGrid*	grid = mm->space->mmGrids + sizeIndex;
		const F32				cellSize = mmGridSizeGroups[sizeIndex].cellSize;

		if(cellSize)
		{
			F32 offset = bg->settings.fEntityCollisionRadius + mmGridSizeGroups[sizeIndex].maxBodyRadius;

			FOR_BEGIN(i, 3);
			{
				lo[i] = (S32)floor((vMin[i] - offset) / cellSize);
				hi[i] = (S32)floor((vMax[i] + offset) / cellSize) + 1;
			}
			FOR_END;
		}
		else
		{
			setVec3same(lo, 0);
			setVec3same(hi, 1);
		}


		x = lo[0];
		do {
			y = lo[1];
			do {
				z = lo[2];
				do {
					MovementManagerGridCell*	cell;
					IVec3						posGrid = {x, y, z};

					if(!mmGridGetCellByGridPosBG(grid, &cell, posGrid, 0))
					{
						continue;
					}

					if (mrProjectileEnt_DoCollisionEntsInCell(	mm, 
																bg, 
																cell, 
																vStartPos, 
																vEndPos, 
																bg->settings.fEntityCollisionRadius, 
																vMid, 
																fSweepRadius))
					{
						result = 1;
					}

				} while (++z < hi[2]);	
			} while (++y < hi[1]);
		} while(++x < hi[0]);
	}
	ARRAY_FOREACH_END;

	PERFINFO_AUTO_STOP();

	return result;
}




// -------------------------------------------------------------------------------------------------------------------------------
__forceinline static S32 mrProjectileEnt_WouldCollObstruct(	ProjectileEntBG* bg, 
															CollInfo *pColl,
															const Vec3 vNormalizedDirection)
{
	F32 fAngle;
	if (bg->settings.bWallsObstruct)
	{
		F32 fSurfaceWallAngle = getAngleBetweenNormalizedUpVec3(pColl->ctri->norm);
		fSurfaceWallAngle -= HALFPI;
		#define WALL_ANGLE_THRESHOLD RAD(45.f)
		if (ABS(fSurfaceWallAngle) < WALL_ANGLE_THRESHOLD)
			return true;
	}

	fAngle = getAngleBetweenNormalizedVec3(pColl->ctri->norm, vNormalizedDirection); 
	fAngle = subAngle(fAngle, HALFPI);
	if (fAngle >= RAD(bg->settings.fSurfaceObstructAngle))
	{	// the wall we hit will obstruct us
		return true;
	}

	return false;
}

// -------------------------------------------------------------------------------------------------------------------------------
__forceinline static void mrProjectileEnt_SnapToGroundNormal(	ProjectileEntBG* bg, 
																const Vec3 vNormal)
{
	if (bg->settings.bLinearVelocity)
	{
		F32 fResultPitch;
		Vec3 vResultVel;
		F32 fOriginalYaw = getVec3Yaw(bg->vVelocity);
		perpedicularProjectionNormalizedAOntoVec3(vNormal, bg->vVelocity, vResultVel);

		fResultPitch = getVec3Pitch(vResultVel);

		sphericalCoordsToVec3(bg->vVelocity, fOriginalYaw, HALFPI-fResultPitch, 1.f);

		perpedicularProjectionNormalizedAOntoVec3(vNormal, bg->vVelocity, bg->vVelocity);

		bg->fLinearGravity = 0.f;
	}
	else
	{
		// change the direction of the velocity to be along the plane of the triangle
		perpedicularProjectionNormalizedAOntoVec3(vNormal, bg->vVelocity, bg->vVelocity);
		copyVec3(bg->vVelocity, bg->vDirection);
		normalVec3(bg->vDirection);
	}
}

// -------------------------------------------------------------------------------------------------------------------------------
static void mrProjectileEnt_DoGroundHug(MovementManager* mm, 
										ProjectileEntBG* bg,
										Vec3 vPosInOut, 
										F32 fRadius)

{
	WorldCollGridCell *wcCell = NULL;
	
	PERFINFO_AUTO_START_FUNC();
	if (mmGetWorldCollGridCellBG(mm, vPosInOut, &wcCell))
	{
		CollInfo	coll = {0};
		coll.filterBits = WC_QUERY_BITS_ENTITY_MOVEMENT;

		if(	wcCellGetWorldColl(wcCell, &coll.wc) &&
			wcCellGetSceneAndOffset(wcCell, &coll.psdkScene, coll.sceneOffset))
		{
			Vec3 vTestPos;
			Vec3 vDirection = {0};
			copyVec3(vPosInOut, vTestPos);
			vDirection[1] = -4.2f; // default ground snap distance
			vTestPos[1] += vDirection[1];
			if (collide(vPosInOut, vTestPos, &coll, fRadius, COLL_TIMEOFIMPACT))
			{
				// calculate the new pos from the time of collision
				scaleAddVec3(vDirection, coll.toi, vPosInOut, vPosInOut);
				// knock out the collision a bit from the normal
				scaleAddVec3(coll.ctri_copy.norm, 0.2f, vPosInOut, vPosInOut);
				
				normalVec3(vDirection);
				if (!mrProjectileEnt_WouldCollObstruct(bg, &coll, vDirection))
				{
					mrProjectileEnt_SnapToGroundNormal(bg, coll.ctri->norm);
				}
			}
			else
			{	// nothing in our threshold, not on ground anymore
				bg->bOnGround = false;
			}
		}
	}
	PERFINFO_AUTO_STOP();
}

// -------------------------------------------------------------------------------------------------------------------------------
static S32 mrProjectileEnt_DoWorldCollision(MovementManager* mm, 
											ProjectileEntBG* bg,
											const Vec3 vStartPos, 
											Vec3 vEndPosInOut, 
											F32 fRadius)
{
	WorldCollGridCell *wcCell = NULL;
	
	PERFINFO_AUTO_START_FUNC();

	if (mmGetWorldCollGridCellBG(mm, vEndPosInOut, &wcCell))
	{
		CollInfo	coll = {0};
		coll.filterBits = WC_QUERY_BITS_ENTITY_MOVEMENT;

		if(	wcCellGetWorldColl(wcCell, &coll.wc) &&
			wcCellGetSceneAndOffset(wcCell, &coll.psdkScene, coll.sceneOffset))
		{
			bool bDoneOnce = false;
			Vec3 vStartTestPos; 
			copyVec3 (vStartPos, vStartTestPos);
			do {

				Vec3 vDirection; 
				F32 fDistTravel;
				bool bWillNotObstruct = false;
				bool bStepped = false;
				bool bWouldObstruct = false;

				if (!collide(vStartTestPos, vEndPosInOut, &coll, fRadius, COLL_TIMEOFIMPACT)) 
				{
					PERFINFO_AUTO_STOP();
					return false;
				}
				
 				bg->bMatchTrajectory = false;

				subVec3(vEndPosInOut, vStartPos, vDirection);

				// set the end pos at the time of impact
				scaleAddVec3(vDirection, coll.toi, vStartPos, vEndPosInOut);

				if (bg->settings.fSurfaceObstructAngle == 0)
				{	// hitting any wall causes us to do the OnCollideWorld event
					PERFINFO_AUTO_STOP();
					return true;
				}
				
				if (bg->settings.fStepHeight > 0.f)
				{	
					Vec3 vToCollPt;
					F32 fStepDist;
					subVec3(coll.vHitPos, vEndPosInOut, vToCollPt);
					if (vToCollPt[1] < 0.f)
					{
						fStepDist = fRadius - -vToCollPt[1];
						if (fStepDist < bg->settings.fStepHeight)
						{
							bg->bOnGround = true;
							bWillNotObstruct = true;

							if (fStepDist < -0.005f || fStepDist > 0.005f)
							{
								vEndPosInOut[1] += fStepDist;
								bStepped = true;
							}
						}
					}
				}

				fDistTravel = normalVec3(vDirection);
				bWouldObstruct = mrProjectileEnt_WouldCollObstruct(bg, &coll, vDirection);
				if (!bWillNotObstruct && bWouldObstruct)
				{
					PERFINFO_AUTO_STOP();
					return true;
				}
				
				
				// knock out the collision a bit from the normal
				scaleAddVec3(coll.ctri_copy.norm, 0.2f, vEndPosInOut, vEndPosInOut);

				// elasticity ?
				bg->bOnGround = true;

				// 
				if (!bWouldObstruct)
				{
					mrProjectileEnt_SnapToGroundNormal(bg, coll.ctri->norm);
				}
				
				if (bDoneOnce)
				{
					PERFINFO_AUTO_STOP();
					return false;
				}
				
				bDoneOnce = true;

				{
					F32 fDistToColl = coll.toi * fDistTravel;
					F32 fDistRemain = fDistTravel - fDistToColl;

					if (fDistRemain < 5.f*MM_SECONDS_PER_STEP)
					{
						PERFINFO_AUTO_STOP();
						return false;
					}

					copyVec3(vEndPosInOut, vStartTestPos);
					if (bg->settings.bLinearVelocity)
					{
						scaleAddVec3(bg->vVelocity, fDistRemain, vEndPosInOut, vEndPosInOut);
					}
					else
					{
						scaleAddVec3(bg->vDirection, fDistRemain, vEndPosInOut, vEndPosInOut);
					}
				}
					

			} while (1);
		}
		return false;
	}
	
	PERFINFO_AUTO_STOP();


	return false;
		
}

// -------------------------------------------------------------------------------------------------------------------------------
static void mrProjectileEnt_ExpireOnHitScenery(	const MovementRequesterMsg* msg,
												ProjectileEntBG* bg,
												ProjectileEntToFG* toFG)
{
	bg->bExpired = true;
	bg->bExpiredOnHitScenery = true;

	toFG->bExpiredOnHitScenery = true;
	mrProjectileEnt_EnableMsgUpdatedToFG(msg, toFG);
}

// -------------------------------------------------------------------------------------------------------------------------------
static void mrProjectileEnt_ExpireRange(const MovementRequesterMsg* msg,
										ProjectileEntBG* bg,
										ProjectileEntToFG* toFG)
{
	bool bDeferrHitExpire = false;

	if (!bDeferrHitExpire)
	{
		bg->bExpired = true;
		
		toFG->bExpireOnTimeOrRange = true;
		mrProjectileEnt_EnableMsgUpdatedToFG(msg, toFG);
	}
}

// -------------------------------------------------------------------------------------------------------------------------------
static S32 cmpProjectileHitTargetTime(const ProjectileHitTarget** lhs, const ProjectileHitTarget** rhs)
{
	return (*rhs)->fHitTime < (*lhs)->fHitTime ? 1 : -1;
}

// -------------------------------------------------------------------------------------------------------------------------------
static S32 mrProjectileEnt_DoCollision(	const MovementRequesterMsg* msg,
										ProjectileEntBG* bg,
										ProjectileEntToFG* toFG,
										const Vec3 vStartPos, 
										Vec3 vEndPosInOut)
{
	MovementManager*	mm;
	MovementRequesterMsgPrivateData*	pd = MR_MSG_TO_PD(msg);
	
	if(	!pd ||
		pd->msgType != MR_MSG_BG_CREATE_OUTPUT ||
		pd->in.bg.createOutput.dataClassBit != MDC_BIT_POSITION_CHANGE)
	{
		return 0;
	}

	mm = pd->mm;
	if (bg->settings.fWorldCollisionRadius && !bg->bExpiredOnHitScenery)
	{
		if (mrProjectileEnt_DoWorldCollision(mm, bg, vStartPos, vEndPosInOut, bg->settings.fWorldCollisionRadius))
		{
			mrProjectileEnt_ExpireOnHitScenery(msg, bg, toFG);
		} 

		if (bg->bOnGround && bg->settings.bHugGround)
		{
			mrProjectileEnt_DoGroundHug(mm, bg, vEndPosInOut, bg->settings.fWorldCollisionRadius);
		}
		
		// todo(RP): get hit time of scenery- and ignore collision with ents if they come after the collision with scenery
	}

	if (bg->settings.fEntityCollisionRadius && 
		(!bg->spcProjectileDelayHit || mrmProcessCountHasPassedBG(msg, bg->spcProjectileDelayHit)))
	{
		bg->spcProjectileDelayHit = 0;

		if (!bg->settings.iMaxHitEntities || eaiSize(&bg->eaiHitEntities) < bg->settings.iMaxHitEntities)
		{
			S32 preNumHitEntities = eaiSize(&bg->eaiHitEntities);

			eaCopy(&bg->eaAllocedHitTargets, &bg->eaFrameHitTargets);
			eaClear(&bg->eaFrameHitTargets);
		
			mrProjectileEnt_DoCollisionEnts(mm, bg, vStartPos, vEndPosInOut);
			
			if (!bg->settings.iMaxHitEntities || preNumHitEntities < bg->settings.iMaxHitEntities)
			{	// we haven't hit our max number of entities yet

				if (bg->settings.iMaxHitEntities)
				{	// we can only hit X entities, so get check the entities that we hit this frame
					//S32 numHitTotal = eaiSize(&bg->eaiHitEntities);
					S32 numHitEntities = eaSize(&bg->eaFrameHitTargets);
					if (numHitEntities)
					{
						S32 i;

						if(preNumHitEntities + numHitEntities > bg->settings.iMaxHitEntities)
						{
							// can't take them all, sort the list and get the first numHitEntities
							numHitEntities = bg->settings.iMaxHitEntities - preNumHitEntities;
							eaQSort(bg->eaFrameHitTargets, cmpProjectileHitTargetTime);
						}

						i = 0;
						do{
							eaiPush(&toFG->eaiHitEntities, bg->eaFrameHitTargets[i]->erEnt);
							eaiPush(&bg->eaiHitEntities, bg->eaFrameHitTargets[i]->erEnt);
						} while (++i < numHitEntities);

						toFG->bHitEntities = true;
						mrProjectileEnt_EnableMsgUpdatedToFG(msg, toFG);

						if( numHitEntities >= bg->settings.iMaxHitEntities)
						{
							bg->bExpired = true;
						}
					}
				}
				else
				{
					S32 numHitEntities = eaiSize(&bg->eaiHitEntities);
					if (preNumHitEntities < numHitEntities)
					{
						S32 i = preNumHitEntities;
						do{
							eaiPush(&toFG->eaiHitEntities, bg->eaiHitEntities[i]);
						} while (++i < numHitEntities);

						toFG->bHitEntities = true;
						mrProjectileEnt_EnableMsgUpdatedToFG(msg, toFG);
					}
				}
			}
		}

	}
	
	return 1;
}

// -------------------------------------------------------------------------------------------------------------------------------
static void mrProjectileEnt_HandleLinearSpeedUpdate(ProjectileEntBG* bg,
													ProjectileEntityMMSettings *settings,
													Vec3 vVelStepOut,
													F32 *pfCurSpeedOut)
{
	if (settings->fSpeed != settings->fTargetSpeed)
	{
		F32 fSpeedAccel = settings->fAcceleration * MM_SECONDS_PER_STEP;

		if (settings->fSpeed > settings->fTargetSpeed)
		{
			settings->fSpeed -= fSpeedAccel;
			if (settings->fSpeed < settings->fTargetSpeed)
				settings->fSpeed = settings->fTargetSpeed;
		}
		else
		{
			settings->fSpeed += fSpeedAccel;
			if (settings->fSpeed > settings->fTargetSpeed)
				settings->fSpeed = settings->fTargetSpeed;
		}

		// using velocity as direction here for linear acceleration 
		scaleVec3(bg->vVelocity, settings->fSpeed, vVelStepOut);
	}
	else
	{
		scaleVec3(bg->vVelocity, settings->fSpeed, vVelStepOut);
	}

	if (settings->fGravity)
	{
		bg->fLinearGravity -= settings->fGravity * MM_SECONDS_PER_STEP;
		vVelStepOut[1] += bg->fLinearGravity;
	}
	
	*pfCurSpeedOut = settings->fSpeed + bg->fLinearGravity;
}

// -------------------------------------------------------------------------------------------------------------------------------
static void mrProjectileEnt_HandleVelocitySpeedUpdate(	ProjectileEntBG* bg,
														ProjectileEntityMMSettings *settings,
														Vec3 vVelStepOut,
														F32 *pfCurSpeedOut)
{
	*pfCurSpeedOut = lengthVec3(bg->vVelocity);

	// if (fCurSpeed < SQR(settings->fTargetSpeed))
	if (settings->fAcceleration)
	{
		F32 fSpeedAccel = settings->fAcceleration * MM_SECONDS_PER_STEP;

		if (*pfCurSpeedOut + fSpeedAccel > settings->fTargetSpeed)
		{
			fSpeedAccel = settings->fTargetSpeed - *pfCurSpeedOut;
		}

		if (fSpeedAccel > 0.f)
		{
			*pfCurSpeedOut += fSpeedAccel;
			scaleAddVec3(bg->vDirection, fSpeedAccel, bg->vVelocity, bg->vVelocity);
		}
	}

	if (settings->fGravity)
		bg->vVelocity[1] -= settings->fGravity * MM_SECONDS_PER_STEP;

	copyVec3(bg->vVelocity, vVelStepOut);
}

// -------------------------------------------------------------------------------------------------------------------------------
static void _matchTrajectory(	ProjectileEntBG* bg, 
								Vec3 vCurrPosInOut)
{
	Vec3 vProjPosition, vNewPos;
	Vec3 vTrajPosToCurrPos;
	F32 interpSpeed;
	
	if (bg->bExpiredOnHitScenery)
	{
		bg->bMatchTrajectory = false;
		return;
	}

	interpSpeed = bg->settings.fSpeed / 200.f;
	interpSpeed *= 10.f;
	interpSpeed = CLAMP(interpSpeed, 1.f, 10.f);

	subVec3(vCurrPosInOut, bg->vVecParam2, vTrajPosToCurrPos);
	projectBOntoNormalizedVec3(bg->vVecParam1, vTrajPosToCurrPos, vProjPosition);
	addVec3(bg->vVecParam2, vProjPosition, vProjPosition);
	interpVec3(interpSpeed * MM_SECONDS_PER_STEP, vCurrPosInOut, vProjPosition, vNewPos);

	if (bg->settings.bLinearVelocity)
	{
		copyVec3(bg->vVecParam1, bg->vVelocity); 
		copyVec3(bg->vVecParam1, bg->vDirection); 
	}
	else
	{
		F32 speed;
		speed = lengthVec3(bg->vVelocity);
		scaleVec3(bg->vVecParam1, speed, bg->vVelocity); 
		copyVec3(bg->vVecParam1, bg->vDirection); 
	}
	
	copyVec3(vNewPos, vCurrPosInOut); 

	if (distance3Squared(vNewPos, vProjPosition) < SQR(0.25f))
	{
		bg->bMatchTrajectory = false;
	}
	
}

// -------------------------------------------------------------------------------------------------------------------------------
static bool mrProjectileEnt_UpdateAttachedToEntity(const MovementRequesterMsg* msg, 
													ProjectileEntBG* bg, 
													Vec3 vPosOut,
													Vec3 pyrFaceOut,
													Quat qFaceRotOut)
{
	pyrFaceOut[2] = 0.f;

	if(mrmGetEntityPositionAndFacePitchYawBG(msg, bg->erGotoEnt, vPosOut, pyrFaceOut))
	{
		PYRToQuat(pyrFaceOut, qFaceRotOut);
		
		if (!vec3IsZero(bg->vVecParam1))
		{
			Vec3 vOffset;
			// rotate the offset by the facing 
			quatRotateVec3(qFaceRotOut, bg->vVecParam1, vOffset);
			addVec3(vPosOut, vOffset, vPosOut);
		}

		return true;
	}
	
	bg->erGotoEnt = 0;
	zeroVec3(vPosOut);
	zeroVec2(pyrFaceOut);
	unitQuat(qFaceRotOut);
	return false;
}

// -------------------------------------------------------------------------------------------------------------------------------
static void mrProjectileEnt_CreateOutputPos_AttachedDefault(const MovementRequesterMsg* msg,
															ProjectileEntBG* bg,
															ProjectileEntToFG* toFG )
{
	Vec3	vCurAttachedPos, vCurPos, pyrFace;
	Quat	qRot;
	bool	bDoCollision = true;

	ProjectileEntityMMSettings *settings = &bg->settings;

	mrmGetPositionBG(msg, vCurPos);
			
	if (!mrProjectileEnt_UpdateAttachedToEntity(msg, bg, vCurAttachedPos, pyrFace, qRot))
	{
		// failed to find the goto entity, expire myself
		toFG->bExpireOnTimeOrRange = true;
		bg->bExpired = true;
		mrProjectileEnt_EnableMsgUpdatedToFG(msg, toFG);
		return;
	}


	if (bg->settings.bIgnoreHitWhileStationary && sameVec3(vCurPos, vCurAttachedPos))
	{
		bDoCollision = false;
	}

	copyVec3(vCurAttachedPos, vCurPos);
	
	// this is a big hack to get around the projectile attachment not keeping up with the attached entity
	// get the last step of the entity and add that to our current position
	// using vVecParam2 to be the last position of the entity we were attached to
	if (!vec3IsZero(bg->vVecParam2))
	{
		Vec3 vDelta;
		F32 fSpeedPerStepSQR;
		
		subVec3(vCurPos, bg->vVecParam2, vDelta);

		fSpeedPerStepSQR = lengthVec3Squared(vDelta);

		if (fSpeedPerStepSQR > 0.f && fSpeedPerStepSQR < SQR(5.f))
		{
			scaleAddVec3(vDelta, 1.5f, vCurPos, vCurPos);
		}
	}

	copyVec3(vCurAttachedPos, bg->vVecParam2);
	

	

	// we're only doing collision based on the current position, 
	// we might want to do a sweep, but we're not for now
	if (bDoCollision)
		mrProjectileEnt_DoCollision(msg, bg, toFG, vCurPos, vCurPos);
	
	// update with the new position
	mrmSetPositionBG(msg, vCurPos);
}

// -------------------------------------------------------------------------------------------------------------------------------
// create output position update for the default projectile behavior, 
// travel in some direction doing collision 
static void mrProjectileEnt_CreateOutputPos_UpdateDefault(	const MovementRequesterMsg* msg,
															ProjectileEntBG* bg,
															ProjectileEntToFG* toFG)
{
	Vec3	vCurPos;
	Vec3	velStep = {0};
	Vec3	vLastPos;
	F32		fCurSpeed = 0.f;
	bool bDoCollision = true;
	ProjectileEntityMMSettings *settings = &bg->settings;
	
	mrmGetPositionBG(msg, vLastPos);

	// if we have an entity we're traveling towards, 
	// get its position and then set the projectile's direction towards the entity
	if (bg->erGotoEnt)
	{
		Vec3 vTargetPos;
		if(mrmGetEntityPositionBG(msg, bg->erGotoEnt, vTargetPos))
		{	
			Vec3 vToTargetPos;
			// offset the target pos by some arbitrary amount, so we don't go straight to its root position
			// todo(rp): get the main capsule height and go to the mid point
			vTargetPos[1] += 3.f;

			subVec3(vTargetPos, vLastPos, vToTargetPos);

			if (settings->bLinearVelocity)
			{
				normalizeCopyVec3(vToTargetPos, bg->vDirection);
				copyVec3(bg->vDirection, bg->vVelocity);
			}
			else
			{
				normalizeCopyVec3(vToTargetPos, bg->vDirection);
			}
		}
		else
		{	// failed to get the gotoEnt pos, clear it out
			bg->erGotoEnt = 0;
		}
	}

	// update our speed
	if (settings->bLinearVelocity)
	{
		mrProjectileEnt_HandleLinearSpeedUpdate(bg, settings, velStep, &fCurSpeed);
	}
	else
	{	
		mrProjectileEnt_HandleVelocitySpeedUpdate(bg, settings, velStep, &fCurSpeed);
	}

	// calculate our current position based on our velocity
	scaleAddVec3(velStep, MM_SECONDS_PER_STEP, vLastPos, vCurPos);

	// check if the projectile has a range, and if so make sure we're not traveling past the range
	if (settings->fRange > 0)
	{
		F32	fTempNewTravelDistSQR;

		fTempNewTravelDistSQR = bg->fTravelDist + distance3Squared(vLastPos, vCurPos);
		if (fTempNewTravelDistSQR >= SQR(settings->fRange))
		{
			F32 newStep =  settings->fRange - bg->fTravelDist + 1.f;
			normalizeCopyVec3(bg->vVelocity, velStep);
			scaleAddVec3(velStep, newStep, vLastPos, vCurPos);
		}
	}
	
	// if the match trajectory flag is set, we're attempting to follow match the trajectory line
	if (bg->bMatchTrajectory)
	{
		_matchTrajectory(bg, vCurPos);
	}

	if (bg->settings.bIgnoreHitWhileStationary && sameVec3(vLastPos, vCurPos))
	{
		bDoCollision = false;
	}

	if (bDoCollision)
		mrProjectileEnt_DoCollision(msg, bg, toFG, vLastPos, vCurPos);

	// after collision is calculate re-check the distance we traveled, 
	// and if we're past the range expire the projectile
	if (settings->fRange > 0) 
	{		
		bg->fTravelDist += distance3(vLastPos, vCurPos);
		if (bg->fTravelDist >= settings->fRange)
		{
			// we've gone past our range, set ourselves to expire
			toFG->bExpireOnTimeOrRange = true;
			bg->bExpired = true;
			mrProjectileEnt_EnableMsgUpdatedToFG(msg, toFG);
		}
	}

	// update with the new position
	mrmSetPositionBG(msg, vCurPos);
}

// -------------------------------------------------------------------------------------------------------------------------------
// update a projectile that is marked as bAsSphereSwept 
// these are generally stationary capsules, but can be attached to entities
static void mrProjectileEnt_CreateOutputPos_UpdateSphereSwept(	const MovementRequesterMsg* msg,
																ProjectileEntBG* bg,
																ProjectileEntToFG* toFG)
{
	// if we are attached to an entity
	if (bg->bAttachedToTargetEnt && bg->erGotoEnt)
	{
		Vec3	vSphereEndPoint, vCurPos, vOffset;
		Vec3	pyrFace = {0};
		Quat	qFaceRot;

		// update the position with the attachment 
		if (mrProjectileEnt_UpdateAttachedToEntity(msg, bg, vCurPos, pyrFace, qFaceRot))
		{
			// update the position with the movement system
			mrmSetPositionBG(msg, vCurPos);

			// get the endpoint for the sweptSphere
			quatRotateVec3(qFaceRot, bg->vVecParam2, vOffset);
			addVec3(vCurPos, vOffset, vSphereEndPoint);

			mrProjectileEnt_DoCollision(msg, bg, toFG, vCurPos, vSphereEndPoint);
		}
		else
		{	// the entity we are attached to wasn't found, lets expire the projectile
			// we've expired due to time, early out since we should be done.
			toFG->bExpireOnTimeOrRange = true;
			bg->bExpired = true;
			mrProjectileEnt_EnableMsgUpdatedToFG(msg, toFG);
			return;
		}
	}
	else
	{
		Vec3	vCurPos;
		Vec3	vSphereEndPoint;

		mrmGetPositionBG(msg, vCurPos);
		
		scaleAddVec3(bg->vDirection, bg->fSweptSphereDist, vCurPos, vSphereEndPoint);
				
		mrProjectileEnt_DoCollision(msg, bg, toFG, vCurPos, vSphereEndPoint);
	}
}

// -------------------------------------------------------------------------------------------------------------------------------
static void mrProjectileEnt_CreateOutputPositionChangeBG(	const MovementRequesterMsg* msg, 
															ProjectileEntBG* bg, 
															ProjectileEntToFG* toFG)
{

	if (bg->bExpired)
		return;

	if (bg->settings.fLifetime && 
			mrmProcessCountPlusSecondsHasPassedBG(	msg, 
													bg->spcProjectileInit, 
													bg->settings.fLifetime))
	{
		// we've expired due to time, early out since we should be done.
		toFG->bExpireOnTimeOrRange = true;
		bg->bExpired = true;
		mrProjectileEnt_EnableMsgUpdatedToFG(msg, toFG);
		return;
	}


	if (!bg->bAsSphereSwept)
	{
		if (!bg->bAttachedToTargetEnt)
		{
			mrProjectileEnt_CreateOutputPos_UpdateDefault(msg, bg, toFG);
		}
		else
		{
			mrProjectileEnt_CreateOutputPos_AttachedDefault(msg, bg, toFG);
		}
	}
	else
	{
		mrProjectileEnt_CreateOutputPos_UpdateSphereSwept(msg, bg, toFG);
	}
}

// -------------------------------------------------------------------------------------------------------------------------------
static void mrProjectileEnt_CreateOutputRotationChangeBG(	const MovementRequesterMsg* msg, 
															ProjectileEntBG* bg)
{
	Vec3 pyr = {0};
	Quat rot;
	pyr[1] = getVec3Yaw(bg->vVelocity);
	pyr[0] = getVec3Pitch(bg->vVelocity);
	
	PYRToQuat(pyr, rot);
	
	mrmSetRotationBG(msg, rot);
	mrmSetFacePitchYawBG(msg, pyr);
}

// -------------------------------------------------------------------------------------------------------------------------------
void mrProjectileEntMsgHandler(const MovementRequesterMsg* msg)
{
	ProjectileEntFG*		fg;
	ProjectileEntBG*		bg;
	ProjectileEntLocalBG*	localBG;
	ProjectileEntToFG*		toFG;
	ProjectileEntToBG*		toBG;
	ProjectileEntSync*		sync;

	MR_MSG_HANDLER_GET_DATA_DEFAULT(msg, ProjectileEnt);

	switch(msg->in.msgType){
		
		xcase MR_MSG_BG_GET_DEBUG_STRING:
		{
			char*	buffer = msg->in.bg.getDebugString.buffer;
			U32		bufferLen = msg->in.bg.getDebugString.bufferLen;

			/*
			sprintf_s(	buffer,
						bufferLen,
						"");
			*/
				
		}
		
		xcase MR_MSG_BG_INITIALIZE:
		{
			mrmHandledMsgsAddBG(msg, MR_HANDLED_MSG_DISCUSS_DATA_OWNERSHIP);
		}

		xcase MR_MSG_FG_CREATE_TOBG:
		{
			*toBG = fg->toBG;
			fg->toBG.flags.bDirectionSet = false;
			fg->toBG.flags.bInitialization = false;
			fg->toBG.flags.bInitAsSweptSphere = false;
			fg->toBG.flags.bMatchTrajectorySet = false;
			fg->toBG.flags.bGotoEntitySet	= false;
			fg->toBG.flags.bStartHugGround = false;
			fg->toBG.flags.bResetExpires = false;
			fg->toBG.flags.bResetHitEntities = false;
			fg->toBG.flags.bOverridesSet = false;
			fg->toBG.flags.bAttachEntitySet = false;
			fg->toBG.flags.bDelayHitEntities = false;
			fg->toBG.eaiClearHitEnts = NULL;
						
			mrmEnableMsgUpdatedToBG(msg);
		}
		
		xcase MR_MSG_BG_UPDATED_TOBG:
		{
			if (TRUE_THEN_RESET(toBG->flags.bInitialization))
			{
				bg->settings = toBG->settings;
				bg->erCreator = toBG->erCreator;
				
				bg->bAsSphereSwept = toBG->flags.bInitAsSweptSphere;

				if (TRUE_THEN_RESET(toBG->flags.bInitAsSweptSphere))
				{
					copyVec3(toBG->vVecParam2, bg->vVecParam2);
				}
				else
				{
					if (bg->settings.bLinearVelocity)
					{
						copyVec3(bg->vDirection,bg->vVelocity);
					}
					else
					{
						scaleVec3(bg->vDirection, bg->settings.fSpeed, bg->vVelocity);
					}
				}
								
				mrmGetProcessCountBG(msg, &bg->spcProjectileInit);
			}

			if (TRUE_THEN_RESET(toBG->flags.bDirectionSet))
			{
				copyVec3(toBG->vDirection, bg->vDirection);
				if (bg->settings.bLinearVelocity)
				{
					copyVec3(bg->vDirection,bg->vVelocity);
				}
				else
				{
					scaleVec3(bg->vDirection, bg->settings.fSpeed, bg->vVelocity);
				}
			}

			if (TRUE_THEN_RESET(toBG->flags.bMatchTrajectorySet))
			{
				if (!bg->bExpiredOnHitScenery)
				{
					bg->bMatchTrajectory = true;
					copyVec3(toBG->vVecParam1, bg->vVecParam1);
					copyVec3(toBG->vVecParam2, bg->vVecParam2);
				}
			}

			if (TRUE_THEN_RESET(toBG->flags.bGotoEntitySet))
			{
				bg->erGotoEnt = toBG->erGotoEnt;
				bg->bAttachedToTargetEnt = false;
			}
			else if (TRUE_THEN_RESET(toBG->flags.bAttachEntitySet))
			{
				bg->erGotoEnt = toBG->erGotoEnt;
				bg->bAttachedToTargetEnt = (toBG->erGotoEnt != 0);
				copyVec3(toBG->vVecParam1, bg->vVecParam1);
			}
			
			if (TRUE_THEN_RESET(toBG->flags.bStartHugGround))
			{
				bg->bOnGround = true;
			}

			if (TRUE_THEN_RESET(toBG->flags.bResetExpires))
			{
				bg->fTravelDist = 0.f;
				bg->bExpired = false;
				bg->bExpiredOnHitScenery = false;
				mrmGetProcessCountBG(msg, &bg->spcProjectileInit);
			}

			if (TRUE_THEN_RESET(toBG->flags.bResetHitEntities))
			{
				eaiClear(&bg->eaiHitEntities);
			}

			if (toBG->eaiClearHitEnts)
			{
				S32 i;
				for (i = eaiSize(&toBG->eaiClearHitEnts) - 1; i >= 0; --i)
				{
					eaiFindAndRemoveFast(&bg->eaiHitEntities, toBG->eaiClearHitEnts[i]);
				}

				eaiDestroy(&toBG->eaiClearHitEnts);
			}

			if (TRUE_THEN_RESET(toBG->flags.bOverridesSet))
			{
				bg->settings.fRange = toBG->fOverrideRange;
			}

			if (TRUE_THEN_RESET(toBG->flags.bDelayHitEntities))
			{
				mrmGetProcessCountBG(msg, &bg->spcProjectileDelayHit);
				bg->spcProjectileDelayHit += (toBG->fDelayHitTime * MM_PROCESS_COUNTS_PER_SECOND);
			}
		}

		xcase MR_MSG_FG_UPDATED_TOFG:
		{
			Entity* pEnt;
			if(mrmGetManagerUserPointerFG(msg, &pEnt))
			{
				if (TRUE_THEN_RESET(toFG->bHitEntities))
				{
					gslProjectile_HitEntitiesToFG(pEnt, toFG->eaiHitEntities);
					// todo: figure out if I can just clear this instead of destroying it, 
					// and then destroy when the requester goes away
					eaiDestroy(&toFG->eaiHitEntities);
				}
				
				if (TRUE_THEN_RESET(toFG->bExpireOnTimeOrRange))
				{
					gslProjectile_Expire(pEnt, false);
				}

				if (TRUE_THEN_RESET(toFG->bExpiredOnHitScenery))
				{
					gslProjectile_HitSceneryToFG(pEnt);
				}
				
				toFG->bToFGEnabled = false;
			}

		}

		xcase MR_MSG_BG_DATA_WAS_RELEASED:{
			//
		}

		xcase MR_MSG_BG_DATA_RELEASE_REQUESTED:{
			// probably should always disallow - for now
			msg->out->bg.dataReleaseRequested.denied = 1;
		}
		
		xcase MR_MSG_BG_DISCUSS_DATA_OWNERSHIP:
		{
			// acquire all the ownership then stop requesting MR_MSG_BG_DISCUSS_DATA_OWNERSHIP
			if(mrmAcquireDataOwnershipBG(msg, MDC_BITS_CHANGE_ALL, 1, NULL, NULL)) // MDC_BITS_ALL
			{
				mrmHandledMsgsRemoveBG(msg, MR_HANDLED_MSG_DISCUSS_DATA_OWNERSHIP);
			}

		}

		xcase MR_MSG_BG_CREATE_OUTPUT:
		{
			// create the good stuff
			switch(msg->in.bg.createOutput.dataClassBit){
				xcase MDC_BIT_POSITION_CHANGE:
					mrProjectileEnt_CreateOutputPositionChangeBG(msg, bg, toFG);
				xcase MDC_BIT_ROTATION_CHANGE:
					mrProjectileEnt_CreateOutputRotationChangeBG(msg, bg);
			}
		}

		xcase MR_MSG_FG_BEFORE_DESTROY:
		{
			if(bg)
			{
				eaiDestroy(&bg->eaiHitEntities);
				eaDestroyEx(&bg->eaFrameHitTargets, NULL);
				eaDestroyEx(&bg->eaAllocedHitTargets, NULL);
			}

			if (toFG)
			{
				eaiDestroy(&toFG->eaiHitEntities);
				
			}

			if (toBG)
			{
				eaiDestroy(&toBG->eaiClearHitEnts);
			}
		}

		xcase MR_MSG_BG_CREATE_DETAILS:
		{
		}
	}
}

// -------------------------------------------------------------------------------------------------------------------------------
S32 mrProjectileEnt_SetDirection(MovementRequester* mr, const Vec3 vDirection)
{
	ProjectileEntFG* fg = NULL;

	if (mrGetFG(mr, mrProjectileEntMsgHandler, &fg))
	{
		copyVec3(vDirection, fg->toBG.vDirection);
		normalVec3(fg->toBG.vDirection);
		fg->toBG.flags.bDirectionSet = true;
		return mrEnableMsgCreateToBG(mr);
	}
	return 0;
}


// -------------------------------------------------------------------------------------------------------------------------------
S32 mrProjectileEnt_InitializeSettings(MovementRequester* mr, EntityRef erCreator, ProjectileEntityMMSettings *pSettings)
{
	ProjectileEntFG* fg = NULL;

	if (pSettings && mrGetFG(mr, mrProjectileEntMsgHandler, &fg))
	{
		fg->toBG.settings = *pSettings;
		fg->toBG.erCreator = erCreator;
		fg->toBG.flags.bInitialization = true;

		return mrEnableMsgCreateToBG(mr);
	}

	return 0;
}

// -------------------------------------------------------------------------------------------------------------------------------
S32 mrProjectileEnt_MatchTrajectory(MovementRequester* mr, const Vec3 vBasePos, const Vec3 vDirection)
{
	ProjectileEntFG* fg = NULL;

	if (mrGetFG(mr, mrProjectileEntMsgHandler, &fg))
	{
		copyVec3(vBasePos, fg->toBG.vVecParam2);
		normalizeCopyVec3(vDirection, fg->toBG.vVecParam1);
		fg->toBG.flags.bMatchTrajectorySet = true;

		return mrEnableMsgCreateToBG(mr);
	}

	return 0;
}

// -------------------------------------------------------------------------------------------------------------------------------
S32 mrProjectileEnt_GotoEntity(MovementRequester* mr, EntityRef er)
{
	ProjectileEntFG* fg = NULL;

	if (mrGetFG(mr, mrProjectileEntMsgHandler, &fg))
	{
		fg->toBG.erGotoEnt = er;
		fg->toBG.flags.bGotoEntitySet = true;

		// bGotoEntitySet overrides bAttachEntitySet
		fg->toBG.flags.bAttachEntitySet = false;
		return mrEnableMsgCreateToBG(mr);
	}

	return 0;
}

// -------------------------------------------------------------------------------------------------------------------------------
S32 mrProjectileEnt_BeginHugGround(MovementRequester* mr)
{
	ProjectileEntFG* fg = NULL;

	if (mrGetFG(mr, mrProjectileEntMsgHandler, &fg))
	{
		fg->toBG.flags.bStartHugGround = true;

		return mrEnableMsgCreateToBG(mr);
	}

	return 0;
}

// -------------------------------------------------------------------------------------------------------------------------------
S32 mrProjectileEnt_ResetExpirations(MovementRequester* mr)
{
	ProjectileEntFG* fg = NULL;

	if (mrGetFG(mr, mrProjectileEntMsgHandler, &fg))
	{
		fg->toBG.flags.bResetExpires = true;
		return mrEnableMsgCreateToBG(mr);
	}

	return 0;
}

// -------------------------------------------------------------------------------------------------------------------------------
S32 mrProjectileEnt_ResetHitEntities(MovementRequester* mr)
{
	ProjectileEntFG* fg = NULL;

	if (mrGetFG(mr, mrProjectileEntMsgHandler, &fg))
	{
		fg->toBG.flags.bResetHitEntities = true;
		return mrEnableMsgCreateToBG(mr);
	}

	return 0;
}

// -------------------------------------------------------------------------------------------------------------------------------
S32 mrProjectileEnt_ClearHitEntity(MovementRequester* mr, EntityRef er)
{
	ProjectileEntFG* fg = NULL;

	if (mrGetFG(mr, mrProjectileEntMsgHandler, &fg))
	{
		eaiPush(&fg->toBG.eaiClearHitEnts, er);
		return mrEnableMsgCreateToBG(mr);
	}

	return 0;
}


// -------------------------------------------------------------------------------------------------------------------------------
S32 mrProjectileEnt_OverrideRange(MovementRequester* mr, F32 fRange)
{
	ProjectileEntFG* fg = NULL;

	if (mrGetFG(mr, mrProjectileEntMsgHandler, &fg))
	{
		fg->toBG.fOverrideRange = fRange;
		fg->toBG.flags.bOverridesSet = true;
		return mrEnableMsgCreateToBG(mr);
	}

	return 0;
}

// -------------------------------------------------------------------------------------------------------------------------------
S32 mrProjectileEnt_InitializeAsSweptSphere(MovementRequester* mr, 
											EntityRef erCreator, 
											ProjectileEntityMMSettings *pSettings,
											const Vec3 vOffset)
{
	ProjectileEntFG* fg = NULL;

	if (pSettings && mrGetFG(mr, mrProjectileEntMsgHandler, &fg))
	{
		fg->toBG.settings = *pSettings;
		fg->toBG.erCreator = erCreator;
		copyVec3(vOffset, fg->toBG.vVecParam2);
		
		fg->toBG.flags.bInitialization = true;
		fg->toBG.flags.bInitAsSweptSphere = true;
		return mrEnableMsgCreateToBG(mr);
	}

	return 0;
}

// -------------------------------------------------------------------------------------------------------------------------------
S32 mrProjectileEnt_AttachToEnt(MovementRequester *mr, EntityRef erAttached, const Vec3 vOffset)
{
	ProjectileEntFG* fg = NULL;

	if (mrGetFG(mr, mrProjectileEntMsgHandler, &fg))
	{
		fg->toBG.erGotoEnt = erAttached;
		if (vOffset && fg->toBG.erGotoEnt)
			copyVec3(vOffset, fg->toBG.vVecParam1);
		else
			zeroVec3(fg->toBG.vDirection);

		fg->toBG.flags.bAttachEntitySet = true;
		
		// bAttachEntitySet overrides bGotoEntitySet
		fg->toBG.flags.bGotoEntitySet = false;
		return mrEnableMsgCreateToBG(mr);
	}

	return 0;
}

// -------------------------------------------------------------------------------------------------------------------------------
S32 mrProjectileEnt_InitialEntityHitDelay(MovementRequester *mr, F32 fSeconds)
{
	ProjectileEntFG* fg = NULL;

	if (mrGetFG(mr, mrProjectileEntMsgHandler, &fg))
	{
		fg->toBG.fDelayHitTime = fSeconds;
		fg->toBG.flags.bDelayHitEntities = true;
		return mrEnableMsgCreateToBG(mr);
	}

	return 0;
}

#include "AutoGen/EntityMovementProjectileEntity_c_ast.c"


