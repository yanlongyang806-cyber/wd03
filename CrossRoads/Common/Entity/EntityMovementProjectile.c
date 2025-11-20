/***************************************************************************
*     Copyright (c) 2005-2007, Cryptic Studios
*     All Rights Reserved
*     Confidential Property of Cryptic Studios
***************************************************************************/

AUTO_RUN_ANON(memBudgetAddMapping(__FILE__, BUDGET_GameSystems););

#include "EntityMovementProjectile.h"
#include "EntityMovementManager.h"
#include "EntityMovementTactical.h"
#include "quat.h"
#include "rand.h"
#include "PhysicsSDK.h"

// Ragdoll data
#include "Character.h"
#include "Entity.h"
#include "dynRagdollData.h"
#include "wlSkelInfo.h"
#include "CostumeCommonEntity.h"
#include "CostumeCommonGenerate.h"
#include "dynSkeleton.h"
#include "dynNode.h"
#include "dynNodeInline.h"
#include "WorldColl.h"
#include "wlCostume.h"
#include "combatConfig.h"
#include "dynDraw.h"

#include "AutoGen/GameClientLib_autogen_ClientCmdWrappers.h"
#include "AutoGen/GameServerLib_autogen_ServerCmdWrappers.h"

#include "AutoGen/EntityMovementProjectile_c_ast.h"
#include "AutoGen/EntityMovementProjectile_h_ast.h"

#define MAX_RAGDOLL_AGE						6.0f
#define RAGDOLL_END_DISTANCE_THRESHOLD		0.3f
#define RAGDOLL_END_TIME					0.2f
#define CAN_CONTROL_RAGDOLL					0
#define RAGDOLL_MAX_ANG_VEL					18.0f
#define RAGDOLL_ITERATION_COUNT				8
#define RAGDOLL_SKIN_WIDTH					(0.025f/0.3048f)

S32 gbNeverEndRagdoll = 0;


AUTO_COMMAND ACMD_CATEGORY(dynAnimation) ACMD_SERVERCMD;
void neverStopRagdoll(int i)
{
	gbNeverEndRagdoll = i;
#ifdef GAMECLIENT
	ServerCmd_neverStopRagdoll(gbNeverEndRagdoll);
#endif
}

AUTO_RUN_MM_REGISTER_RESOURCE_MSG_HANDLER(	mmrSkeletonMsgHandler,
											"Skeleton",
											MMRSkeleton,
											MDC_BIT_POSITION_CHANGE);

AUTO_STRUCT;
typedef struct MMRSkeletonActivatedFG {
	MMRSkeletonPartStates		partStates;
} MMRSkeletonActivatedFG;

AUTO_STRUCT;
typedef struct MMRSkeletonActivatedBG {
	S32	unused;
} MMRSkeletonActivatedBG;

AUTO_STRUCT;
typedef struct MMRSkeletonState {
	MMRSkeletonPartStates		partStates;
} MMRSkeletonState;

static void mmrSkeletonPartStateInterp(	const MMRSkeletonPartState* psOlder,
										const MMRSkeletonPartState* psNewer,
										F32 interpOlderToNewer,
										MMRSkeletonPartState* psTarget)
{
	Quat rotOlder;
	Quat rotNewer;
	Quat rotTarget;

	interpVec3(	interpOlderToNewer,
				psOlder->pos,
				psNewer->pos,
				psTarget->pos);

	PYRToQuat(	psOlder->pyr,
				rotOlder);

	PYRToQuat(	psNewer->pyr,
				rotNewer);
	
	quatInterp(	interpOlderToNewer,
				rotOlder,
				rotNewer,
				rotTarget);

	quatToPYR(	rotTarget,
				psTarget->pyr);
}

void mmrSkeletonMsgHandler(const MovementManagedResourceMsg* msg){
	const MMRSkeletonConstant*		constant = msg->in.constant;
	const MMRSkeletonConstantNP*	constantNP = msg->in.constantNP;

	switch(msg->in.msgType){
		xcase MMR_MSG_GET_CONSTANT_DEBUG_STRING:{
			char** estrBuffer = msg->in.getDebugString.estrBuffer;
			
			if(constantNP){
				estrConcatf(estrBuffer,
							"\n"
							"Offset pos(%1.3f, %1.3f, %1.3f)"
							" [%8.8x, %8.8x, %8.8x]\n"
							"Offset pyr(%1.3f, %1.3f, %1.3f)"
							" [%8.8x, %8.8x, %8.8x]",
							vecParamsXYZ(constantNP->posOffsetToAnimRoot),
							vecParamsXYZ((S32*)constantNP->posOffsetToAnimRoot),
							vecParamsXYZ(constantNP->pyrOffsetToAnimRoot),
							vecParamsXYZ((S32*)constantNP->pyrOffsetToAnimRoot));
			}

			EARRAY_CONST_FOREACH_BEGIN(constant->parts, i, isize);
			{
				MMRSkeletonPart* part = constant->parts[i];
				
				estrConcatf(estrBuffer,
							"\nPart[%d]: Bone \"%s\", Parent \"%s\"",
							i,
							FIRST_IF_SET(part->boneName, "not set"),
							FIRST_IF_SET(part->parentBoneName, "not set"));
			}
			EARRAY_FOREACH_END;
		}
	
		xcase MMR_MSG_FG_LOG_STATE:{
			MMRSkeletonActivatedFG* activated = msg->in.activatedStruct;
			
			if(!constantNP){
				break;
			}
			
			EARRAY_CONST_FOREACH_BEGIN(constant->parts, i, isize);
				MMRSkeletonPart*		p = constant->parts[i];
				MMRSkeletonPartNP*		pnp = eaGet(&constantNP->parts, i);
				MMRSkeletonPartState*	ps = eaGet(&activated->partStates.states, i);
				
				if(!ps){
					break;
				}
				
				if(!i){
					// Draw the anim root relative to me.

					Mat4 matRootInHipsSpace;
					Mat4 matHips;
					Mat4 matRootWorld;
					
					createMat3YPR(matRootInHipsSpace, constantNP->pyrOffsetToAnimRoot);
					copyVec3(constantNP->posOffsetToAnimRoot, matRootInHipsSpace[3]);

					createMat3YPR(matHips, ps->pyr);
					copyVec3(ps->pos, matHips[3]);
					
					mulMat4(matHips, matRootInHipsSpace, matRootWorld);

					FOR_BEGIN(j, 3);
						Vec3 a;
						
						addVec3(matRootWorld[j], matRootWorld[3], a);
						
						mmrmLogSegment(	msg,
										NULL,
										"mmr.skeletonRoot",
										0xff000000 |
											(0xff << (8 * (2 - j))),
										matRootWorld[3],
										a);
					FOR_END;
				}
				
				if(pnp){
					Mat4 matPartWorld;

					createMat3YPR(matPartWorld, ps->pyr);
					copyVec3(ps->pos, matPartWorld[3]);

					if(pnp->isBox){
					}else{
						const Capsule*	c = &pnp->capsule;
						Vec3			dirCap;
						Vec3			posCap;
						Vec3			pos0;
						Vec3			pos1;

						// Translate capsule from actor space to world space.

						mulVecMat3(c->vDir, matPartWorld, dirCap);
						mulVecMat3(c->vStart, matPartWorld, posCap);
						
						// Extend to endpoints of capsule.
						
						scaleAddVec3(dirCap, -c->fRadius, posCap, pos0);
						scaleAddVec3(dirCap, c->fRadius + c->fLength, posCap, pos1);
					
						mmrmLogSegment(	msg,
										NULL,
										"mmr.skeleton",
										0xffff0000,
										pos0,
										pos1);
					}
				}
			EARRAY_FOREACH_END;
		}

		xcase MMR_MSG_FG_SET_STATE:{
			MMRSkeletonActivatedFG*	activated = msg->in.activatedStruct;
			S32						didSetNetState = 1;
			
			if(!activated->partStates.states){
				mmrmSetNoPredictedDestroyFG(msg);
			}

			if(msg->in.fg.setState.state.net.olderStruct){
				const MMRSkeletonPartStates* older = msg->in.fg.setState.state.net.olderStruct;
				const MMRSkeletonPartStates* newer = msg->in.fg.setState.state.net.newerStruct;

				if(!newer){
					StructCopyAll(	parse_MMRSkeletonPartStates,
									older,
									&activated->partStates);
				}else{
					F32 interpOlderToNewer = msg->in.fg.setState.state.net.interpOlderToNewer;

					StructCopyAll(	parse_MMRSkeletonPartStates,
									older,
									&activated->partStates);
									
					ANALYSIS_ASSUME(activated->partStates.states);

					EARRAY_CONST_FOREACH_BEGIN(older->states, i, isize);
					{
						MMRSkeletonPartState*	psNewer;
						MMRSkeletonPartState*	psOlder;
						MMRSkeletonPartState*	psCur;
						
						if(i >= eaSize(&newer->states)){
							break;
						}
						
						psNewer = newer->states[i];
						psOlder = older->states[i];
						psCur = activated->partStates.states[i];

						mmrSkeletonPartStateInterp(	psOlder,
													psNewer,
													interpOlderToNewer,
													psCur);
					}
					EARRAY_FOREACH_END;
				}
			}
			else if (msg->in.fg.setState.state.net.newerStruct){
				StructCopyAll(	parse_MMRSkeletonPartStates,
								msg->in.fg.setState.state.net.newerStruct,
								&activated->partStates);
			}else{
				didSetNetState = 0;
			}
			
			if(msg->in.fg.setState.interpLocalToNet < 1.f){
				const MMRSkeletonPartStates*	older = msg->in.fg.setState.state.local.olderStruct;
				const MMRSkeletonPartStates*	newer = msg->in.fg.setState.state.local.newerStruct;
				const F32						interpOlderToNewer = msg->in.fg.setState.state.local.interpOlderToNewer;

				if(newer){
					if(!older){
						if(!didSetNetState){
							StructCopyAll(	parse_MMRSkeletonPartStates,
											newer,
											&activated->partStates);
						}else{
							EARRAY_CONST_FOREACH_BEGIN(newer->states, i, isize);
							{
								MMRSkeletonPartState*	psNewer;
								MMRSkeletonPartState*	psCur;
								
								if(i >= eaSize(&activated->partStates.states)){
									break;
								}
								
								psNewer = newer->states[i];
								psCur = activated->partStates.states[i];

								mmrSkeletonPartStateInterp(	psNewer,
															psCur,
															msg->in.fg.setState.interpLocalToNet,
															psCur);
							}
							EARRAY_FOREACH_END;
						}
					}else{
						if(!didSetNetState){
							StructCopyAll(	parse_MMRSkeletonPartStates,
											older,
											&activated->partStates);
						}

						EARRAY_CONST_FOREACH_BEGIN(older->states, i, isize);
						{
							MMRSkeletonPartState*	psNewer;
							MMRSkeletonPartState*	psOlder;
							MMRSkeletonPartState	psTemp;
							MMRSkeletonPartState*	psCur;
							
							if(	i >= eaSize(&newer->states) ||
								i >= eaSize(&activated->partStates.states))
							{
								break;
							}
							
							psNewer = newer->states[i];
							psOlder = older->states[i];
							psCur = activated->partStates.states[i];

							mmrSkeletonPartStateInterp(	psOlder,
														psNewer,
														interpOlderToNewer,
														&psTemp);

							mmrSkeletonPartStateInterp(	&psTemp,
														psCur,
														msg->in.fg.setState.interpLocalToNet,
														psCur);
						}
						EARRAY_FOREACH_END;
					}
				}
				else if(older){
					if(!didSetNetState){
						StructCopyAll(	parse_MMRSkeletonPartStates,
										older,
										&activated->partStates);
					}else{
						EARRAY_CONST_FOREACH_BEGIN(older->states, i, isize);
						{
							MMRSkeletonPartState*	psOlder;
							MMRSkeletonPartState*	psCur;
							
							if(i >= eaSize(&activated->partStates.states)){
								break;
							}
							
							psOlder = older->states[i];
							psCur = activated->partStates.states[i];

							mmrSkeletonPartStateInterp(	psOlder,
														psCur,
														msg->in.fg.setState.interpLocalToNet,
														psCur);
						}
						EARRAY_FOREACH_END;
					}
				}
			}
		}

		xcase MMR_MSG_FG_DESTROYED:{
			MMRSkeletonActivatedFG* activated = msg->in.activatedStruct;

			StructDeInit(	parse_MMRSkeletonPartStates,
							&activated->partStates);
		}

		xcase MMR_MSG_FG_DEBUG_DRAW:{
			const MMRSkeletonActivatedFG* activated = msg->in.activatedStruct;
			
			if(	!activated ||
				!constantNP)
			{
				break;
			}

			EARRAY_CONST_FOREACH_BEGIN(constant->parts, i, isize);
			{
				MMRSkeletonPart*		p = constant->parts[i];
				MMRSkeletonPartNP*		pnp = eaGet(&constantNP->parts, i);
				MMRSkeletonPartState*	ps = eaGet(&activated->partStates.states, i);
				Mat4					matPart;

				if(!ps){
					continue;
				}

				copyVec3(ps->pos, matPart[3]);
				createMat3YPR(matPart, ps->pyr);

				if(pnp){
					if(pnp->isBox){
						if(msg->in.fg.debugDraw.drawFuncs->drawBox3D){
							Mat4 matBoxInPartSpace;
							Mat4 matBoxWorld;
							
							createMat3YPR(matBoxInPartSpace, pnp->pyrBox);
							copyVec3(pnp->posBox, matBoxInPartSpace[3]);
							
							mulMat4(matPart, matBoxInPartSpace, matBoxWorld);
							
							msg->in.fg.debugDraw.drawFuncs->drawBox3D(	pnp->xyzSizeBox,
																		matBoxWorld,
																		0x80ffff00);
						}
					}
					else if(pnp->isBody){
						MovementBody* b;
						
						if(mmBodyGetByIndex(&b, pnp->bodyIndex)){
							mmBodyDraw(	msg->in.fg.debugDraw.drawFuncs,
										b,
										matPart,
										0xff44ff00,
										0);
						}
					}
					else if(msg->in.fg.debugDraw.drawFuncs->drawCapsule3D){
						Vec3 pos;
						Vec3 dir;
						
						mulVecMat4(pnp->capsule.vStart, matPart, pos);
						mulVecMat3(pnp->capsule.vDir, matPart, dir);
						
						msg->in.fg.debugDraw.drawFuncs->drawCapsule3D(	pos,
																		dir,
																		pnp->capsule.fLength,
																		pnp->capsule.fRadius,
																		0x80ffff00);
					}
				}

				if(!i){
					// Draw the anim root relative to me.

					Mat4 matRootInHipsSpace;
					Mat4 matRootWorld;
					
					createMat3YPR(matRootInHipsSpace, constantNP->pyrOffsetToAnimRoot);
					copyVec3(constantNP->posOffsetToAnimRoot, matRootInHipsSpace[3]);

					mulMat4(matPart, matRootInHipsSpace, matRootWorld);

					FOR_BEGIN(j, 3);
						Vec3 a;
						
						addVec3(matRootWorld[j], matRootWorld[3], a);
						
						msg->in.fg.debugDraw.drawFuncs->drawLine3D(	matRootWorld[3],
																	0xffffffff,
																	a,
																	0xff000000 |
																		(0xff << (8 * (2 - j))));
					FOR_END;
				}
			}
			EARRAY_FOREACH_END;
		}
	}
}

static U32 mmrSkeletonGetResourceID(void){
	static U32 id;

	if(!id){
		if(!mmGetManagedResourceIDByMsgHandler(mmrSkeletonMsgHandler, &id)){
			assert(0);
		}
	}

	return id;
}

S32 mmrSkeletonCreateBG(const MovementRequesterMsg* msg,
						const MMRSkeletonConstant* constant,
						const MMRSkeletonConstantNP* constantNP,
						const MMRSkeletonState* state,
						U32* handleOut)
{
	if(mrmResourceCreateBG(	msg,
							handleOut,
							mmrSkeletonGetResourceID(),
							constant,
							constantNP,
							state))
	{
		mrmResourceSetNoAutoDestroyBG(	msg,
										mmrSkeletonGetResourceID(),
										*handleOut);
		
		return 1;
	}
	
	return 0;
}

S32 mmrSkeletonDestroyBG(	const MovementRequesterMsg* msg,
							U32* handleInOut)
{
	return mrmResourceDestroyBG(msg,
								mmrSkeletonGetResourceID(),
								handleInOut);
}

S32 mmrSkeletonCreateStateBG(	const MovementRequesterMsg* msg,
								U32 handle,
								const MMRSkeletonState* state)
{
	return mrmResourceCreateStateBG(msg, mmrSkeletonGetResourceID(), handle, state);
}

S32 mmrSkeletonGetStateFG(	MovementManager* mm,
							const MMRSkeletonConstant** constantOut,
							const MMRSkeletonConstantNP** constantNPOut,
							const MMRSkeletonPartStates** partStatesOut)
{
	MMRSkeletonActivatedFG* activated;

	if(mmResourceFindFG(mm,
						NULL,
						mmrSkeletonGetResourceID(),
						constantOut,
						constantNPOut,
						&activated))
	{
		if(!SAFE_MEMBER(activated, partStates.states)){
			return 0;
		}
		
		if(partStatesOut){
			*partStatesOut = &activated->partStates;
		}
		
		return 1;
	}
	
	return 0;
}

AUTO_RUN_MM_REGISTER_REQUESTER_MSG_HANDLER(	mrProjectileMsgHandler,
											"ProjectileMovement",
											Projectile);

AUTO_STRUCT;
typedef struct ProjectileToBG {
	U32				minHold;
	Vec3			vel;
	Vec3			target;
	F32				timer;
	F32				impactTimer;
	F32				getupTimer;
	U32				spcStart;
	U32				useTarget	: 1;
	U32				lowAngle	: 1;
	U32				isInFlight	: 1;
	U32				proneAtEnd  : 1;
	U32				nearDeath	: 1; AST(NAME(NearDeath))
	U32				instantFacePlant : 1; AST(NAME(InstantFacePlant))
	U32				ignoreTravelTime : 1; AST(NAME(IgnoreTravelTime))
	U32				resetAnimBits : 1;
} ProjectileToBG;

AUTO_STRUCT;
typedef struct ProjectileFG {
	ProjectileToBG	toBG;
} ProjectileFG;

AUTO_STRUCT;
typedef struct ProjectileBG {
	ProjectileToBG	toBG;
	U32				spcSettled;
	U32				spcPlayedKeyword;
	U32				spcPlayedImpact;
	U32				spcPlayedGetup;
	U32				spcPlayedNearDeath;
	U32				doStart				: 1;
	U32				started				: 1;
	U32				inAir				: 1;
	U32				isFalling			: 1;
	U32				settled				: 1;
	U32				held				: 1;
	U32				fellDown			: 1;
	U32				getup				: 1;
	U32				enterNearDeath		: 1;
	U32				playedKeyword		: 1;
	U32				playedFlagRising	: 1;
	U32				playedFlagFalling	: 1;
	U32				playedFlagImpact	: 1;
	U32				playedFlagProne		: 1;
	U32				playedFlagGetup		: 1;
	U32				playedFlagNearDeath : 1;
} ProjectileBG;

AUTO_STRUCT;
typedef struct ProjectileLocalBG {
	S32 unused;
} ProjectileLocalBG;

AUTO_STRUCT;
typedef struct ProjectileToFG {
	S32 unused;
} ProjectileToFG;

AUTO_STRUCT;
typedef struct ProjectileSync {
	S32 unused;
} ProjectileSync;

static void GetKnockToVelocityNoFlight(const Vec3 vToTarget, Vec3 vVelOut, bool lowAngle)
{
	F32 fDist = lengthVec3(vToTarget);

	// target does not have flight, include some vertical velocity
	if(fDist > 0.f){
		if(fDist > 200.f){
			scaleVec3(vToTarget, 200.f/fDist, vVelOut);
		} else if(fDist < 10) {
			scaleVec3(vToTarget, 20.f/fDist, vVelOut);
		} else {
			scaleVec3(vToTarget, 1.1f, vVelOut);
		}
	} else {
		zeroVec3(vVelOut);
	}

	// 
	if (fDist > 10.f) { 
		MAX1(vVelOut[1], 40.f);
	}

	if(lowAngle)
	{
		if(fDist < 20)
		{
			vVelOut[0] *= 1.5;
			vVelOut[1] *= .5;
			vVelOut[2] *= 1.5;
		}
		else if(fDist < 40)
		{
			vVelOut[0] *= 1.25;
			vVelOut[1] *= .75;
			vVelOut[2] *= 1.25;
		}
	}
}

static void GetKnockToVelocityWithFlight(const Vec3 vToTarget, Vec3 vVelOut)
{
	F32 fDist = lengthVec3(vToTarget);
	
	if(fDist > 0.f){
		F32 fVel = sqrtf(fDist * 40*1.5f * 2.f);
		scaleVec3(vToTarget, 1.85f, vVelOut);
	} else {
		zeroVec3(vVelOut);
	}
}

void mrProjectileMsgHandler(const MovementRequesterMsg* msg){
	ProjectileFG*		fg;
	ProjectileBG*		bg;
	ProjectileLocalBG*	localBG;
	ProjectileToFG*		toFG;
	ProjectileToBG*		toBG;
	ProjectileSync*		sync;

	MR_MSG_HANDLER_GET_DATA_DEFAULT(msg, Projectile);

	switch(msg->in.msgType){
		xcase MR_MSG_FG_CREATE_TOBG:{
			mrmEnableMsgUpdatedToBG(msg);
			*toBG = fg->toBG;
		}

		xcase MR_MSG_BG_INITIALIZE:{
			const U32 handledMsgs =	MR_HANDLED_MSG_DISCUSS_DATA_OWNERSHIP;

			mrmHandledMsgsAddBG(msg, handledMsgs);
		}

		xcase MR_MSG_BG_GET_DEBUG_STRING:{
			char*	buffer = msg->in.bg.getDebugString.buffer;
			U32		bufferLen = msg->in.bg.getDebugString.bufferLen;

			snprintf_s(	buffer,
						bufferLen,
						"%s "
						" (%1.2f, %1.2f, %1.2f)"
						" [%8.8x, %8.8x, %8.8x]"
						"\n"
						"StartPC: %u,"
						" SettledPC: %u"
						"\n"
						"Flags: %s%s%s%s%s"
						,
						bg->toBG.useTarget ? "target" : "vel",
						vecParamsXYZ(bg->toBG.useTarget ? bg->toBG.target : bg->toBG.vel),
						vecParamsXYZ((S32*)(bg->toBG.useTarget ? bg->toBG.target : bg->toBG.vel)),
						bg->toBG.spcStart,
						bg->spcSettled,
						bg->doStart ? "doStart, " : "",
						bg->started ? "started, " : "",
						bg->inAir ? "inAir, " : "",
						bg->inAir && bg->isFalling ? "isFalling, " : "",
						bg->settled ? "settled, " : "");
		}

		xcase MR_MSG_BG_DATA_WAS_RELEASED:{
			mrmDestroySelf(msg);
		}

		xcase MR_MSG_FG_BEFORE_DESTROY:{
			Entity* e;
			if(mrmGetManagerUserPointerFG(msg, &e)){
				if (e->mm.mrTactical){
					mrTacticalNotifyPowersStop(	e->mm.mrTactical,
												TACTICAL_KNOCK_UID,
												mmGetProcessCountAfterMillisecondsFG(0));
				}
			}
		}

		xcase MR_MSG_BG_UPDATED_TOBG:{
			bg->toBG = *toBG;
			bg->doStart = 1;
		}
		
		xcase MR_MSG_BG_DATA_RELEASE_REQUESTED:{
			// This lets DeadMovement interrupt once settled.

			if(!bg->settled){
				msg->out->bg.dataReleaseRequested.denied = 1;
			}
		}

		xcase MR_MSG_BG_DISCUSS_DATA_OWNERSHIP:{
			if(!bg->started){
				if(	bg->doStart &&
					mrmProcessCountHasPassedBG(msg, bg->toBG.spcStart))
				{
					bg->doStart = 0;

					if(!mrmAcquireDataOwnershipBG(	msg,
													MDC_BITS_TARGET_ALL | MDC_BIT_ANIMATION,
													1,
													NULL,
													NULL))
					{
						mrmDestroySelf(msg);
					}
				}
			}
			else if (!bg->settled)
			{
				if (gConf.bNewAnimationSystem)
				{
					if (bg->playedFlagImpact &&
						mrmProcessCountPlusSecondsHasPassedBG(msg, bg->spcPlayedImpact, bg->toBG.impactTimer))
					{
						bg->fellDown = 1;
					}
				}
			}
			else if(bg->settled)
			{
				if (!gConf.bNewAnimationSystem)
				{
					F32 seconds = FIRST_IF_SET(g_CombatConfig.fKnockbackProneTimer, 1.f);

					if (!bg->toBG.proneAtEnd ||
						mrmProcessCountPlusSecondsHasPassedBG(msg, bg->spcSettled, seconds))
					{
						mrmReleaseAllDataOwnershipBG(msg);
						mrmDestroySelf(msg);
					}
				}
				else
				{
					if (!bg->held &&
						mrmAcquireDataOwnershipBG(msg, MDC_BIT_POSITION_TARGET, 1, NULL, NULL))
					{
						bg->held = 1;
					}

					if (bg->playedFlagGetup &&
						mrmProcessCountPlusSecondsHasPassedBG(msg, bg->spcPlayedGetup, bg->toBG.getupTimer))
					{
						mrmReleaseAllDataOwnershipBG(msg);
						mrmDestroySelf(msg);
					}
					else if(bg->playedFlagNearDeath &&
							mrmProcessCountPlusSecondsHasPassedBG(msg, bg->spcPlayedNearDeath, bg->toBG.getupTimer))
					{
						mrmReleaseAllDataOwnershipBG(msg);
						mrmDestroySelf(msg);
					}
					else if (!bg->toBG.instantFacePlant &&
							 bg->playedFlagImpact &&
							 mrmProcessCountPlusSecondsHasPassedBG(msg, bg->spcPlayedImpact, bg->toBG.impactTimer))
					{
						bg->fellDown = 1;
					}
					else if (bg->toBG.instantFacePlant &&
							 bg->playedKeyword &&
							 mrmProcessCountPlusSecondsHasPassedBG(msg, bg->spcPlayedKeyword, bg->toBG.impactTimer))
					{
						bg->fellDown = 1;
					}

					if (bg->fellDown)
					{
						if (bg->toBG.nearDeath &&
							!bg->enterNearDeath)
						{
							bg->enterNearDeath = 1;
						}
						else if(!bg->toBG.nearDeath &&
								!bg->getup	 &&
								(	!bg->toBG.proneAtEnd ||
									bg->toBG.instantFacePlant && mrmProcessCountPlusSecondsHasPassedBG(msg, bg->spcPlayedKeyword, bg->toBG.timer) ||
									!bg->toBG.instantFacePlant && !bg->toBG.ignoreTravelTime && mrmProcessCountPlusSecondsHasPassedBG(msg, bg->spcPlayedKeyword, bg->toBG.timer) ||
									!bg->toBG.instantFacePlant &&  bg->toBG.ignoreTravelTime && mrmProcessCountPlusSecondsHasPassedBG(msg, bg->spcSettled, bg->toBG.timer)
								))
						{
							if (bg->toBG.proneAtEnd) {
								bg->getup = 1;
							} else {
								mrmReleaseAllDataOwnershipBG(msg);
								mrmDestroySelf(msg);
							}
						}
					}
				}
			}
		}

		xcase MR_MSG_BG_CREATE_OUTPUT:{
			switch(msg->in.bg.createOutput.dataClassBit){
				xcase MDC_BIT_POSITION_TARGET:{
					ASSERT_FALSE_AND_SET(bg->started);

					mrmHandledMsgsRemoveBG(msg, MR_HANDLED_MSG_OUTPUT_POSITION_TARGET);

					if(bg->toBG.useTarget){
						Vec3 pos;
						Vec3 vToTarget;

						mrmGetPositionBG(msg, pos);
						subVec3(bg->toBG.target, pos, vToTarget);
														
						if(!bg->toBG.isInFlight){
							GetKnockToVelocityNoFlight(vToTarget, bg->toBG.vel, bg->toBG.lowAngle);
						}else{
							GetKnockToVelocityWithFlight(vToTarget, bg->toBG.vel);
						}
					}else{
						// if there isn't enough vertical velocity, just re-target 
						// and knock horizontally across the ground.

						if(	bg->toBG.vel[1] > 0 && 
							bg->toBG.vel[1] <= 30.f && 
							lengthVec3SquaredXZ(bg->toBG.vel) > 0.001f)
						{

							F32 vertLen = bg->toBG.vel[1];
							F32 len;
								
							bg->toBG.vel[1] = 0.f;
							len = normalVec3(bg->toBG.vel);
							scaleVec3(bg->toBG.vel, len+(vertLen*0.4f), bg->toBG.vel);
						}
					}

					mrmSetAdditionalVelBG(msg, bg->toBG.vel, 0, 1);
				}

				xcase MDC_BIT_ROTATION_TARGET:{
					Vec3 dir;
					scaleVec3(bg->toBG.vel, -1.f, dir);
					mrmRotationTargetSetAsDirectionBG(msg, dir);

					if (gConf.bNewAnimationSystem) {
						//this auto-reverts to normal the next frame when mmSendMsgsCreateOutputBG is called
						//note: the rate is set to TWOPI/MM_SECONDS_PER_STEP so that the knocked entity can rotate up to TWOPI radians per frame
						mrmRotationTargetSetTurnRateAsOverrideBG(msg, TWOPI/MM_SECONDS_PER_STEP);
					}
				}

				xcase MDC_BIT_ANIMATION:{

					if (gConf.bNewAnimationSystem &&
						TRUE_THEN_RESET(bg->toBG.resetAnimBits))
					{
						bg->settled		= 0;
						bg->inAir		= 0;
						bg->isFalling	= 0;
						bg->fellDown	= 0;
						bg->getup		= 0;
						bg->enterNearDeath = 0;
						
						bg->playedKeyword		= 0;
						bg->playedFlagRising	= 0;
						bg->playedFlagFalling	= 0;
						bg->playedFlagImpact	= 0;
						bg->playedFlagGetup		= 0;
						bg->playedFlagNearDeath = 0;
					}

					{
						Vec3	normal;
						S32		onGround;
						
						if(mrmGetOnGroundBG(msg, &onGround, normal)){
							bg->inAir = !onGround;
		
							if(bg->inAir){
								Vec3 vel;
								mrmGetVelocityBG(msg, vel);
								bg->isFalling = (vel[1] <= 0);
							}
						}
					}

					if(!bg->settled){
						S32 isSettled;

						mrmGetIsSettledBG(msg, &isSettled);

						if(isSettled){
							bg->settled = 1;

							mrmGetProcessCountBG(msg,
												&bg->spcSettled);
						}
					}

					if(!gConf.bNewAnimationSystem){
						if(!bg->settled){
							mrmAnimAddBitBG(msg, mmAnimBitHandles.knockback);

							if(bg->inAir){
								mrmAnimAddBitBG(msg, mmAnimBitHandles.air);

								if(bg->isFalling){
									mrmAnimAddBitBG(msg, mmAnimBitHandles.falling);
								}else{
									mrmAnimAddBitBG(msg, mmAnimBitHandles.rising);
								}
							}
						}
						else if(g_CombatConfig.fKnockbackProneTimer &&
								bg->toBG.proneAtEnd)
						{
							mrmAnimAddBitBG(msg, mmAnimBitHandles.prone);
						}
					}
					else //New Animation System
					{
						if (bg->toBG.instantFacePlant)
						{
							//faceplant version, where we ignore the air time portion of the knockback animation

							if (FALSE_THEN_SET(bg->playedKeyword))
							{
								mrmAnimStartBG(msg, mmAnimBitHandles.knockdown, 0);
								mrmGetProcessCountBG(msg, &bg->spcPlayedKeyword);
							}
							else if(bg->enterNearDeath &&
									FALSE_THEN_SET(bg->playedFlagNearDeath))
							{
								mrmAnimStartBG(msg, mmAnimBitHandles.neardeath_impact, 0);
								mrmGetProcessCountBG(msg, &bg->spcPlayedNearDeath);
							}
							else if(bg->getup &&
									FALSE_THEN_SET(bg->playedFlagGetup))
							{
								mrmAnimPlayFlagBG(msg, mmAnimBitHandles.getUp, 0);
								mrmGetProcessCountBG(msg, &bg->spcPlayedGetup);
							}
						}
						else
						{
							//regular version, where the character flys through the air from a knockback / knockto / knockup
							if (FALSE_THEN_SET(bg->playedKeyword))
							{
								if (bg->toBG.proneAtEnd) {
									mrmAnimStartBG(msg, mmAnimBitHandles.knockback, 0);
								} else {
									mrmAnimStartBG(msg, mmAnimBitHandles.pushback, 0);
								}
								mrmGetProcessCountBG(msg, &bg->spcPlayedKeyword);
							}

							if(!bg->settled)
							{
								if(bg->inAir)
								{
									if(	bg->isFalling &&
										FALSE_THEN_SET(bg->playedFlagFalling))
									{
										mrmAnimPlayFlagBG(msg, mmAnimBitHandles.falling, 0);
									}
									else if (FALSE_THEN_SET(bg->playedFlagRising))
									{
										mrmAnimPlayFlagBG(msg, mmAnimBitHandles.rising, 0);
									}
								}
								else if(FALSE_THEN_SET(bg->playedFlagImpact))
								{
									mrmAnimPlayFlagBG(msg, mmAnimBitHandles.impact, 0);
									mrmGetProcessCountBG(msg, &bg->spcPlayedImpact);
								}
							}
							else
							{
								if (FALSE_THEN_SET(bg->playedFlagImpact))
								{
									mrmAnimPlayFlagBG(msg, mmAnimBitHandles.impact, 0);
									mrmGetProcessCountBG(msg, &bg->spcPlayedImpact);
								}
								else if(bg->enterNearDeath &&
										FALSE_THEN_SET(bg->playedFlagNearDeath))
								{
									if (bg->toBG.proneAtEnd) {
										mrmAnimStartBG(msg, mmAnimBitHandles.neardeath_impact, 0);
									} else {
										mrmAnimStartBG(msg, mmAnimBitHandles.neardeath, 0);
									}
									
									mrmGetProcessCountBG(msg, &bg->spcPlayedNearDeath);
								}
								else if(bg->getup &&
										FALSE_THEN_SET(bg->playedFlagGetup))
								{
									mrmAnimPlayFlagBG(msg, mmAnimBitHandles.getUp, 0);
									mrmGetProcessCountBG(msg, &bg->spcPlayedGetup);
								}
							}
						}
					}
				}
			}
		}

		xcase MR_MSG_BG_IS_MISPREDICTED:{
			mrmDestroySelf(msg);
		}
	}
}

// damps the vector given by a factor until it reaches zero
// returns non-zero if the vector has reached zero
S32 mrProjectileApplyFriction(Vec3 velInOut, 
							  U32 onGround)
{
#define MIN_SPEED_FRICTION	40.f
	// these friction values might need to be exposed at some level
#define FRICTION_GROUND		1.50f
#define FRICTION_AIR		0.8f


	F32 len = normalVec3(velInOut);
	F32	additive_friction = MAX(len, MIN_SPEED_FRICTION) * MM_SECONDS_PER_STEP;

	if(onGround) additive_friction *= FRICTION_GROUND;
	else additive_friction *= FRICTION_AIR;

	if(len > additive_friction){
		len -= additive_friction;
		scaleVec3(	velInOut,
					len,
					velInOut);
		return false;
	}else{
		// 
		return true;
	}
}

void mrProjectileStartWithVelocity(	MovementRequester* mr,
									Entity *e,
									const Vec3 vel,
									U32 spcStart,
									S32 instantFacePlant,
									S32 proneAtEnd,
									F32 timer,
									S32 ignoreTravelTime)
{
	ProjectileFG* fg = NULL;

	if(mrGetFG(mr, mrProjectileMsgHandler, &fg)){
		mrEnableMsgCreateToBG(mr);
		fg->toBG.useTarget = 0;
		fg->toBG.lowAngle = 0;
		copyVec3(vel, fg->toBG.vel);
		fg->toBG.spcStart = FIRST_IF_SET(spcStart, mmGetProcessCountAfterSecondsFG(0));
		fg->toBG.minHold = 60 * 5;
		fg->toBG.instantFacePlant = !!instantFacePlant;
		fg->toBG.proneAtEnd = !!proneAtEnd;
		fg->toBG.timer = timer;
		fg->toBG.ignoreTravelTime = !!ignoreTravelTime;
		fg->toBG.nearDeath = 0;
		fg->toBG.resetAnimBits = 1;

		fg->toBG.impactTimer = (fg->toBG.proneAtEnd ? 1.0 : 0.333);
		fg->toBG.getupTimer  = 1.0;
		if(e){
			PlayerCostume *pPC;

			if (e->mm.mrTactical){
				mrTacticalNotifyPowersStart(e->mm.mrTactical,
											TACTICAL_KNOCK_UID, 
											TDF_ALL,
											fg->toBG.spcStart);
			}

			if      (e->costumeRef.pEffectiveCostume ) pPC = e->costumeRef.pEffectiveCostume;
			else if (e->costumeRef.pStoredCostume    ) pPC = e->costumeRef.pStoredCostume;
			else if (e->costumeRef.pSubstituteCostume) pPC = e->costumeRef.pSubstituteCostume;
			else if (pPC = GET_REF(e->costumeRef.hReferencedCostume)) ;
			else pPC = NULL;

			if (pPC) {
				PCSkeletonDef *pSkel = GET_REF(pPC->hSkeleton);
				if (pSkel) {
					if (fg->toBG.proneAtEnd) {
						if (pSkel->fImpactTime_Knock > 0) fg->toBG.impactTimer = pSkel->fImpactTime_Knock;
					} else {
						if (pSkel->fImpactTime_Push > 0) fg->toBG.impactTimer = pSkel->fImpactTime_Push;
					}
					if (pSkel->fGetupTime > 0) fg->toBG.getupTimer  = pSkel->fGetupTime;
				}
			}
		}
	}
}

void mrProjectileStartWithTarget(	MovementRequester* mr,
									Entity *e,
									const Vec3 target,
									U32 spcStart,
									S32 lowAngle,
									S32 targetHasFlight,
									S32 instantFacePlant,
									S32 proneAtEnd,
									F32 timer,
									S32 ignoreTravelTime)
{
	ProjectileFG* fg = NULL;

	if(mrGetFG(mr, mrProjectileMsgHandler, &fg)){
		mrEnableMsgCreateToBG(mr);
		fg->toBG.useTarget = 1;
		fg->toBG.lowAngle = !!lowAngle;
		fg->toBG.isInFlight = !!targetHasFlight;

		copyVec3(target, fg->toBG.target);
		fg->toBG.spcStart = FIRST_IF_SET(spcStart, mmGetProcessCountAfterSecondsFG(0));
		fg->toBG.minHold = 60 * 5;
		fg->toBG.instantFacePlant = !!instantFacePlant;
		fg->toBG.proneAtEnd = !!proneAtEnd;
		fg->toBG.timer = timer;
		fg->toBG.ignoreTravelTime = !!ignoreTravelTime;
		fg->toBG.nearDeath = 0;
		fg->toBG.resetAnimBits = 1;

		fg->toBG.impactTimer = (fg->toBG.proneAtEnd ? 1.0 : 0.333);
		fg->toBG.getupTimer  = 1.0;
		if(e){
			PlayerCostume *pPC;

			if (e->mm.mrTactical){
				mrTacticalNotifyPowersStart(e->mm.mrTactical,
											TACTICAL_KNOCK_UID, 
											TDF_ALL,
											fg->toBG.spcStart);
			}

			if      (e->costumeRef.pEffectiveCostume ) pPC = e->costumeRef.pEffectiveCostume;
			else if (e->costumeRef.pStoredCostume    ) pPC = e->costumeRef.pStoredCostume;
			else if (e->costumeRef.pSubstituteCostume) pPC = e->costumeRef.pSubstituteCostume;
			else if (pPC = GET_REF(e->costumeRef.hReferencedCostume)) ;
			else pPC = NULL;

			if (pPC) {
				PCSkeletonDef *pSkel = GET_REF(pPC->hSkeleton);
				if (pSkel) {
					if (fg->toBG.proneAtEnd) {
						if (pSkel->fImpactTime_Knock) fg->toBG.impactTimer = pSkel->fImpactTime_Knock;
					} else {
						if (pSkel->fImpactTime_Push) fg->toBG.impactTimer = pSkel->fImpactTime_Push;
					}
					if (pSkel->fGetupTime  > 0) fg->toBG.getupTimer  = pSkel->fGetupTime;
				}
			}
		}
	}
}

void mrProjectileSetNearDeath(MovementRequester *mr)
{
	ProjectileFG* fg = NULL;

	if(mrGetFG(mr, mrProjectileMsgHandler, &fg)){
		mrEnableMsgCreateToBG(mr);
		fg->toBG.nearDeath = 1;
	}
}


AUTO_RUN_MM_REGISTER_REQUESTER_MSG_HANDLER(	mrSimBodyMsgHandler,
											"SimBodyMovement",
											SimBody);

AUTO_STRUCT;
typedef struct SimBodyToBG {
	S32						unused;
} SimBodyToBG;

AUTO_STRUCT;
typedef struct SimBodyFG {
	U32						unused;
} SimBodyFG;

AUTO_STRUCT;
typedef struct SimBodyActorValues {
	Vec3					vel;
	Vec3					angVel;
	Vec3					pos;
	Vec3					pyr;
} SimBodyActorValues;

AUTO_STRUCT;
typedef struct SimBodyBG {
	SimBodyActorValues**	sbavs;

	U32						mmrSkeletonHandle;

	U32						upDown : 1;
	U32						forwardDown : 1;
} SimBodyBG;

AUTO_STRUCT;
typedef struct SimBodyActor {
	U32						simBodyHandle;
} SimBodyActor;

AUTO_STRUCT;
typedef struct SimBodyLocalBG {
	SimBodyActor**			actors;
} SimBodyLocalBG;

AUTO_STRUCT;
typedef struct SimBodyToFG {
	S32						unused;
} SimBodyToFG;

AUTO_STRUCT;
typedef struct SimBodySync {
	U32						bodyIndex;
	U32						isEnabled : 1;
} SimBodySync;

void mrSimBodyMsgHandler(const MovementRequesterMsg* msg){
#if !PSDK_DISABLED
	SimBodyFG*		fg;
	SimBodyBG*		bg;
	SimBodyLocalBG*	localBG;
	SimBodyToFG*	toFG;
	SimBodyToBG*	toBG;
	SimBodySync*	sync;

	MR_MSG_HANDLER_GET_DATA_DEFAULT(msg, SimBody);

	switch(msg->in.msgType){
		xcase MR_MSG_BG_INITIALIZE:{
			mrmHandledMsgsAddBG(msg, MR_HANDLED_MSG_DISCUSS_DATA_OWNERSHIP);
		}

		xcase MR_MSG_BG_DISCUSS_DATA_OWNERSHIP:{
			if(sync->isEnabled){
				mrmAcquireDataOwnershipBG(msg, MDC_BITS_CHANGE_ALL, 1, NULL, NULL);
			}
		}

		xcase MR_MSG_BG_DATA_RELEASE_REQUESTED:{
			msg->out->bg.dataReleaseRequested.denied = 1;
		}

		xcase MR_MSG_BG_FORCE_CHANGED_POS:
		acase MR_MSG_BG_FORCE_CHANGED_ROT:{
			Mat4 mat;
			Quat rot;

			mrmGetPositionBG(msg, mat[3]);
			mrmGetRotationBG(msg, rot);
			quatToMat(rot, mat);

			EARRAY_CONST_FOREACH_BEGIN(localBG->actors, i, isize);
				SimBodyActor*	sba = localBG->actors[i];
				PSDKActor*		psdkActor;

				if(mrmSimBodyGetPSDKActorBG(msg, sba->simBodyHandle, &psdkActor)){
					psdkActorSetMat(psdkActor, mat);
				}
			EARRAY_FOREACH_END;
		}

		xcase MR_MSG_BG_INIT_REPREDICT_SIM_BODY:{
			U32 handle = msg->in.bg.initRepredictSimBody.handle;
			S32 found = 0;

			EARRAY_CONST_FOREACH_BEGIN(localBG->actors, i, isize);
				SimBodyActor* sba = localBG->actors[i];

				if(handle == sba->simBodyHandle){
					SimBodyActorValues*	sbav = eaGet(&bg->sbavs, i);
					PSDKActor*			psdkActor;
					Mat4				mat;

					if(!sbav){
						break;
					}

					found = 1;

					mrmSimBodyGetPSDKActorBG(msg, handle, &psdkActor);

					copyVec3(sbav->pos, mat[3]);
					createMat3YPR(mat, sbav->pyr);

					psdkActorSetMat(psdkActor,
									mat);

					psdkActorSetVels(	psdkActor,
										sbav->vel,
										sbav->angVel);

					break;
				}
			EARRAY_FOREACH_END;

			if(!found){
				// Handle wasn't found, so delete it.

				msg->out->bg.initRepredictSimBody.unused = 1;
			}
		}

		xcase MR_MSG_BG_SIM_BODIES_DO_CREATE:{
			if(eaSize(&localBG->actors) == 2){
				break;
			}

			FOR_BEGIN(i, 2);
			{
				SimBodyActor*	sba = StructAlloc(parse_SimBodyActor);
				Quat			rot;
				Mat4			mat;

				mrmGetPositionAndRotationBG(msg, mat[3], rot);
				quatToMat(rot, mat);

				mat[3][1] += i * 20;

				eaPush(&localBG->actors, sba);

				mrmSimBodyCreateFromIndexBG(msg,
											&sba->simBodyHandle,
											sync->bodyIndex,
											0,
											mat);
			}
			FOR_END;

			EARRAY_CONST_FOREACH_BEGIN(localBG->actors, i, isize);
				SimBodyActor*		sba = localBG->actors[i];
				SimBodyActor*		sbaNext;
				PSDKActor*			psdkActor;
				PSDKActor*			psdkActorNext;

				if(i + 1 >= isize){
					continue;
				}

				sbaNext = localBG->actors[i + 1];

				if(	mrmSimBodyGetPSDKActorBG(msg, sba->simBodyHandle, &psdkActor) &&
					mrmSimBodyGetPSDKActorBG(msg, sbaNext->simBodyHandle, &psdkActorNext))
				{
					PSDKJointDesc jd = {0};

					setVec3(jd.anchor0, 0, 10, 0);
					setVec3(jd.anchor1, 0, -10, 0);

					jd.actor0 = psdkActor;
					jd.actor1 = psdkActorNext;

					psdkJointCreate(&jd, 0, 0);
				}
			EARRAY_FOREACH_END;
		}

		xcase MR_MSG_BG_CREATE_OUTPUT:{
			switch(msg->in.bg.createOutput.dataClassBit){
				xcase MDC_BIT_POSITION_CHANGE:{
					if(!eaSize(&localBG->actors)){
						mrmNeedsSimBodyCreateBG(msg);
					}

					EARRAY_CONST_FOREACH_BEGIN(localBG->actors, i, isize);
						SimBodyActor*		sba = localBG->actors[i];
						SimBodyActorValues* sbav;
						PSDKActor*			psdkActor;
						Mat4				mat;

						if(!mrmSimBodyGetPSDKActorBG(msg, sba->simBodyHandle, &psdkActor)){
							continue;
						}

						// Get/create actor values.

						while(i >= eaSize(&bg->sbavs)){
							sbav = StructAlloc(parse_SimBodyActorValues);
							eaPush(&bg->sbavs, sbav);
						}

						sbav = bg->sbavs[i];

						// Do some stuff with controls.

						if(mrmGetInputValueBitBG(msg, MIVI_BIT_UP)){
							if(FALSE_THEN_SET(bg->upDown)){
								Vec3 vel = {0.f, 30.f, 0.f};

								psdkActorSetVels(	psdkActor,
													vel,
													NULL);
							}
						}else{
							bg->upDown = 0;
						}

						if(mrmGetInputValueBitBG(msg, MIVI_BIT_FORWARD)){
							Vec3	pyr = {0, mrmGetInputValueF32BG(msg, MIVI_F32_MOVE_YAW), 0};
							Vec3	angVel;

							createMat3_0_YPR(angVel, pyr);

							scaleVec3(angVel, PI, angVel);

							psdkActorSetVels(	psdkActor,
												NULL,
												angVel);
				
						}

						if(psdkActorGetMat(psdkActor, mat)){
							copyVec3(mat[3], sbav->pos);
							getMat3YPR(mat, sbav->pyr);

							if(!i){
								mrmSetPositionBG(msg, mat[3]);
							}
						}

						psdkActorGetVels(	psdkActor,
											sbav->vel,
											sbav->angVel);
					EARRAY_FOREACH_END;

					if(eaSize(&bg->sbavs)){
						MMRSkeletonState state = {0};
						
						if(!bg->mmrSkeletonHandle){
							MMRSkeletonConstant		constant = {0};
							MMRSkeletonConstantNP	constantNP = {0};
							
							EARRAY_CONST_FOREACH_BEGIN(bg->sbavs, i, isize);
							{
								MMRSkeletonPart*		part;
								MMRSkeletonPartNP*		partNP;
								MMRSkeletonPartState*	partState;
								SimBodyActorValues*		sbav = bg->sbavs[i];

								part = StructAlloc(parse_MMRSkeletonPart);
								eaPush(&constant.parts, part);
								
								partNP = StructAlloc(parse_MMRSkeletonPartNP);
								partNP->bodyIndex = sync->bodyIndex;
								partNP->isBody = 1;
								eaPush(&constantNP.parts, partNP);

								partState = StructAlloc(parse_MMRSkeletonPartState);
								copyVec3(sbav->pos, partState->pos);
								copyVec3(sbav->pyr, partState->pyr);
								eaPush(&state.partStates.states, partState);
							}
							EARRAY_FOREACH_END;

							mmrSkeletonCreateBG(msg,
												&constant,
												&constantNP,
												&state,
												&bg->mmrSkeletonHandle);

							StructDeInit(	parse_MMRSkeletonConstant,
											&constant);
						}else{
							EARRAY_CONST_FOREACH_BEGIN(bg->sbavs, i, isize);
							{
								MMRSkeletonPartState*	partState;
								SimBodyActorValues*		sbav = bg->sbavs[i];

								partState = StructAlloc(parse_MMRSkeletonPartState);

								copyVec3(sbav->pos, partState->pos);
								copyVec3(sbav->pyr, partState->pyr);

								eaPush(&state.partStates.states, partState);
							}
							EARRAY_FOREACH_END;

							if(!mmrSkeletonCreateStateBG(	msg,
															bg->mmrSkeletonHandle,
															&state))
							{
								mrmLog(	msg,
										NULL,
										"Failed to create skeleton resource %d state",
										bg->mmrSkeletonHandle);
							}
						}

						StructDeInit(	parse_MMRSkeletonState,
										&state);
					}
				}

				xcase MDC_BIT_ROTATION_CHANGE:{
					if(eaSize(&localBG->actors)){
						SimBodyActor*	sba = localBG->actors[0];
						PSDKActor*		psdkActor;

						if(mrmSimBodyGetPSDKActorBG(msg, sba->simBodyHandle, &psdkActor)){
							Mat4 mat;

							if(psdkActorGetMat(psdkActor, mat)){
								Quat rot;
								mat3ToQuat(mat, rot);
								mrmSetRotationBG(msg, rot);
							}
						}
					}
				}
			}
		}
	}
#endif
}

#define GET_SYNC(sync)	MR_GET_SYNC(mr, mrSimBodyMsgHandler, SimBody, sync)

S32 mrSimBodySetEnabled(MovementRequester* mr,
						U32 bodyIndex,
						S32 isEnabled)
{
	SimBodySync* sync;

	if(GET_SYNC(&sync)){
		MR_SYNC_SET_IF_DIFF(mr, sync->bodyIndex, bodyIndex);
		MR_SYNC_SET_IF_DIFF(mr, sync->isEnabled, (U32)isEnabled);

		return 1;
	}

	return 0;
}

#undef GET_SYNC

AUTO_RUN_MM_REGISTER_REQUESTER_MSG_HANDLER(	mrRagDollMsgHandler,
											"RagDollMovement",
											MRRagDoll);

AUTO_STRUCT;
typedef struct MRRagDollToBG {
	MRRagDollParts*			parts;
	Vec3					vel;
	Vec3					angVel;
	U32						spcStart;
	U32						material_index;
	U32						deathDirectionBit;
	U32						addVel				: 1;
	U32						dead				: 1;
	U32						update_dead_state	: 1;
	U32						endRequestApproved	: 1;
	U32						update_deathDirectionBit	: 1;
} MRRagDollToBG;

AUTO_STRUCT;
typedef struct MRRagDollFG {
	MRRagDollToBG			toBG;
	U32						ragdollOver		: 1;
} MRRagDollFG;

AUTO_STRUCT;
typedef struct MRRagDollPartState {
	Vec3					vel;
	Vec3					angVel;
	Vec3					pos;
	Vec3					pyr;
} MRRagDollPartState;

AUTO_STRUCT;
typedef struct MRRagDollPartStates {
	MRRagDollPartState**	states;
} MRRagDollPartStates;

AUTO_STRUCT;
typedef struct MRRagDollBG {
	MRRagDollParts*			parts;
	U32						material_index;
	MRRagDollPartStates		partStates;
	Vec3					vel;
	Vec3					angVel;
	U32						spcStart;
	Vec3					last_root_pos;

	Vec3					posOffsetToAnimRoot;
	Vec3					pyrOffsetToAnimRoot;

	U32						initial_pc; // when object is created
	U32						root_pos_update_pc; // when we last set last_root_pos
	U32						end_pc; // when we decided to terminate ragdoll
	U32						mmrSkeletonHandle;

	U32						deathDirectionBit;
	
	U32						setOffsetToAnimRoot	: 1;
	U32						addVel				: 1;
	U32						ragdollOver 		: 1;
	U32						skippedGetupOneStep	: 1;
	U32						snappedToGround		: 1;
	U32						getupOnBack 		: 1;
	U32						flashedGetupBits	: 1;
	U32						setGetupRotation	: 1;
	U32						didNoInterp			: 1;
	U32						dead				: 1;
	U32						endRequestApproved	: 1;

	U32						playAnimDeath	: 1;
	U32						playAnimRevive	: 1;
} MRRagDollBG;

AUTO_STRUCT;
typedef struct MRRagDollLocalBG {
	U32*					simBodyHandles;
} MRRagDollLocalBG;

AUTO_STRUCT;
typedef struct MRRagDollToFG {
	U32						requestEnd			: 1;
	U32						ragdollOver			: 1;
} MRRagDollToFG;

AUTO_STRUCT;
typedef struct MRRagDollSync {
	U32						isEnabled : 1;
} MRRagDollSync;

void mrRagDollMsgHandler(const MovementRequesterMsg* msg){
#if !PSDK_DISABLED
	MRRagDollFG*		fg;
	MRRagDollBG*		bg;
	MRRagDollLocalBG*	localBG;
	MRRagDollToFG*		toFG;
	MRRagDollToBG*		toBG;
	MRRagDollSync*		sync;

	MR_MSG_HANDLER_GET_DATA_DEFAULT(msg, MRRagDoll);

	switch(msg->in.msgType){
		xcase MR_MSG_FG_CREATE_TOBG:{
			mrmEnableMsgUpdatedToBG(msg);

			*toBG = fg->toBG;
			fg->toBG.parts = NULL;
			fg->toBG.addVel = 0;
			fg->toBG.update_dead_state = 0;
			fg->toBG.update_deathDirectionBit = 0;
			fg->toBG.endRequestApproved = 0;
		}
		

		xcase MR_MSG_BG_GET_DEBUG_STRING:{
			#define FLAG(x) bg->x ? #x", " : ""
			snprintf_s(	msg->in.bg.getDebugString.buffer,
						msg->in.bg.getDebugString.bufferLen,
						"Vel (%1.3f, %1.3f, %1.3f) [%8.8x, %8.8x, %8.8x]\n"
						"AngVel (%1.3f, %1.3f, %1.3f) [%8.8x, %8.8x, %8.8x]\n"
						"StartPC: %d\n"
						"Flags: %s%s",
						vecParamsXYZ(bg->vel),
						vecParamsXYZ((S32*)bg->vel),
						vecParamsXYZ(bg->angVel),
						vecParamsXYZ((S32*)bg->angVel),
						bg->spcStart,
						FLAG(addVel),
						FLAG(ragdollOver)
						);
			#undef FLAG
		}

		xcase MR_MSG_BG_INITIALIZE:{
			mrmHandledMsgsAddBG(msg, MR_HANDLED_MSG_DISCUSS_DATA_OWNERSHIP);
			mrmHandledMsgsRemoveBG(msg, MR_HANDLED_MSG_OUTPUT_ROTATION_TARGET);
			mrmGetProcessCountBG(msg, &bg->initial_pc);
			mrmGetProcessCountBG(msg, &bg->root_pos_update_pc);
		}

		xcase MR_MSG_BG_DISCUSS_DATA_OWNERSHIP:{
			if(	!bg->parts ||
				!eaSize(&bg->parts->parts))
			{
				mrmDestroySelf(msg);
			}
			else if(mrmProcessCountHasPassedBG(msg, bg->spcStart)){
				if(!mrmAcquireDataOwnershipBG(msg, MDC_BITS_ALL, 1, NULL, NULL)){
					mrmDestroySelf(msg);
				}else{
					mrmHandledMsgsRemoveBG(msg, MR_HANDLED_MSG_DISCUSS_DATA_OWNERSHIP);
				}
			}
		}
		
		xcase MR_MSG_BG_PREDICT_DISABLED:{
			mrmDestroySelf(msg);
		}

		xcase MR_MSG_BG_DATA_RELEASE_REQUESTED:{
			msg->out->bg.dataReleaseRequested.denied = 1;
		}
		
		xcase MR_MSG_BG_RECEIVE_OLD_DATA:{
			const MovementSharedData* sd = msg->in.bg.receiveOldData.sharedData;
			
			switch(sd->dataType){
				xcase MSDT_VEC3:{
					if(	!stricmp(sd->name, "Velocity") &&
						FALSE_THEN_SET(bg->addVel))
					{
						copyVec3(	sd->data.vec3,
									bg->vel);
					}
				}
			}
		}

		xcase MR_MSG_BG_UPDATED_TOBG:{
			if(toBG->parts){
				StructDestroySafe(parse_MRRagDollParts, &bg->parts);
				StructDeInit(parse_MRRagDollPartStates, &bg->partStates);

				bg->parts = toBG->parts;
				bg->material_index = toBG->material_index;
				toBG->parts = NULL;

				EARRAY_INT_CONST_FOREACH_BEGIN(localBG->simBodyHandles, i, isize);
					mrmSimBodyDestroyBG(msg, &localBG->simBodyHandles[i]);
				EARRAY_FOREACH_END;

				eaiDestroy(&localBG->simBodyHandles);
			}
			if(toBG->addVel){
				bg->spcStart = toBG->spcStart;
				copyVec3(toBG->vel, bg->vel);
				copyVec3(toBG->angVel, bg->angVel);
				bg->addVel = 1;
				toBG->addVel = 0;
			}
			if(toBG->update_dead_state){
				if (bg->dead != toBG->dead) {
					if (toBG->dead)
						bg->playAnimDeath  = 1;
					else
						bg->playAnimRevive = 1;
				}
				bg->dead = toBG->dead;
				toBG->update_dead_state = 0;
			}
			if(toBG->update_deathDirectionBit){
				bg->deathDirectionBit = toBG->deathDirectionBit;
				toBG->update_deathDirectionBit = 0;
			}

			bg->endRequestApproved = toBG->endRequestApproved;
			toBG->endRequestApproved = 0;
		}

		xcase MR_MSG_FG_UPDATED_TOFG:{
			if(	toFG->requestEnd &&
				!fg->toBG.addVel)
			{
				fg->toBG.endRequestApproved = 1;
				mrmEnableMsgCreateToBG(msg);
			}

			fg->ragdollOver = toFG->ragdollOver;

			toFG->requestEnd = 0;
			toFG->ragdollOver = 0;
		}

		xcase MR_MSG_BG_MMR_HANDLE_CHANGED:{
			if(msg->in.bg.mmrHandleChanged.handleOld == bg->mmrSkeletonHandle){
				bg->mmrSkeletonHandle = msg->in.bg.mmrHandleChanged.handleNew;
			}
		}

		xcase MR_MSG_BG_FORCE_CHANGED_POS:
		acase MR_MSG_BG_FORCE_CHANGED_ROT:{
			Mat4 mat;
			Quat rot;

			mrmGetPositionBG(msg, mat[3]);
			mrmGetRotationBG(msg, rot);
			quatToMat(rot, mat);

			EARRAY_INT_CONST_FOREACH_BEGIN(localBG->simBodyHandles, i, isize);
				U32			simBodyHandle = localBG->simBodyHandles[i];
				PSDKActor*	psdkActor;

				if(mrmSimBodyGetPSDKActorBG(msg, simBodyHandle, &psdkActor)){
					psdkActorSetMat(psdkActor, mat);
				}
			EARRAY_FOREACH_END;
		}

		xcase MR_MSG_BG_INIT_REPREDICT_SIM_BODY:{
			const U32	simBodyHandleToFind = msg->in.bg.initRepredictSimBody.handle;
			S32			found = 0;

			EARRAY_INT_CONST_FOREACH_BEGIN(localBG->simBodyHandles, i, isize);
				const U32 simBodyHandle = localBG->simBodyHandles[i];

				if(simBodyHandleToFind == simBodyHandle){
					MRRagDollPartState*	ps = eaGet(&bg->partStates.states, i);
					PSDKActor*			psdkActor;
					Mat4				mat;
					if(!ps){
						break;
					}

					found = 1;

					mrmSimBodyGetPSDKActorBG(msg, simBodyHandle, &psdkActor);

					copyVec3(ps->pos, mat[3]);
					createMat3YPR(mat, ps->pyr);

					psdkActorSetMat(psdkActor,
									mat);

					psdkActorSetVels(	psdkActor,
										ps->vel,
										ps->angVel);
					break;
				}
			EARRAY_FOREACH_END;

			if(!found){
				// Handle wasn't found, so delete it.

				msg->out->bg.initRepredictSimBody.unused = 1;
			}
		}

		xcase MR_MSG_BG_SIM_BODIES_DO_CREATE:{
			Mat4 mat;
			Quat rot;

			EARRAY_INT_CONST_FOREACH_BEGIN(localBG->simBodyHandles, i, isize);
				mrmSimBodyDestroyBG(msg, &localBG->simBodyHandles[i]);
			EARRAY_FOREACH_END;

			eaiDestroy(&localBG->simBodyHandles);

			// Construct a quat from the py and rot of the entity
			{
				Vec3 norm;
				Vec3 pyr;
				Vec3 forward;
				Mat3 temp_mat;
				mrmGetPositionAndRotationBG(msg, mat[3], rot);
				mrmGetFacePitchYawBG(msg, pyr);
				pyr[2] = 0.0f;
				createMat3YPR(temp_mat, pyr);
				copyVec3(temp_mat[2], forward);
				quatRotateVec3Inline(rot, upvec, norm);
				orientMat3ToNormalAndForward(mat, norm, forward);
			}

			if(bg->parts){
				EARRAY_CONST_FOREACH_BEGIN(bg->parts->parts, i, isize);
				{
					MRRagDollPart*	p = bg->parts->parts[i];
					U32				simBodyHandle = 0;

					Mat4 partMat;
					Mat4 partMatWorld;

					quatToMat(p->rot, partMat);

					copyVec3(p->pos, partMat[3]);

					mulMat4(mat, partMat, partMatWorld);

					if(p->isBox){
						Vec3 xyzHalfSize;
						
						scaleVec3(p->xyzSizeBox, 0.5f, xyzHalfSize);
						
						mrmSimBodyCreateFromBoxBG(	msg,
													&simBodyHandle,
													xyzHalfSize,
													p->matBox,
													bg->material_index,
													p->density,
													partMatWorld);
					}else{
						mrmSimBodyCreateFromCapsuleBG(	msg,
														&simBodyHandle,
														&p->capsule,
														bg->material_index,
														p->density,
														partMatWorld);
					}

					if(simBodyHandle){
						eaiPush(&localBG->simBodyHandles,
								simBodyHandle);
					}
				}
				FOR_END;

				EARRAY_CONST_FOREACH_BEGIN(bg->parts->parts, i, isize);
				{
					MRRagDollPart*		p = bg->parts->parts[i];
					MRRagDollPart*		pParent = eaGet(&bg->parts->parts, p->parentIndex);
					PSDKActor*			psdkActor;
					PSDKActor*			psdkActorParent;

					if(!pParent){
						continue;
					}

					if(	mrmSimBodyGetPSDKActorBG(	msg,
													localBG->simBodyHandles[i],
													&psdkActor) &&
						mrmSimBodyGetPSDKActorBG(	msg,
													localBG->simBodyHandles[p->parentIndex],
													&psdkActorParent))
					{
						PSDKJointDesc jd = {0};

						jd.actor0 = psdkActor;
						jd.actor1 = psdkActorParent;

						copyVec3(p->selfAnchor, jd.anchor0);
						copyVec3(p->parentAnchor, jd.anchor1);

						jd.tuning = p->tuning;

						jd.volumeScale = p->volumeScale;

						psdkJointCreate(&jd, 0, 1);
					}
				}
				EARRAY_FOREACH_END;
				

				// Now loop through and set orientations based on initial pose. This must be done after joints are set up
				EARRAY_CONST_FOREACH_BEGIN(bg->parts->parts, i, isize);
				{
					PSDKActor*		psdkActor;
					MRRagDollPart*	p = bg->parts->parts[i];

					if(mrmSimBodyGetPSDKActorBG(	msg,
													localBG->simBodyHandles[i],
													&psdkActor))
					{
						if(p->pose){
							psdkActorSetRot(psdkActor, p->pose_rot);
							psdkActorSetPos(psdkActor, p->pose_pos);
						}

						psdkActorSetSkinWidth(psdkActor, p->skinWidth);
						psdkActorSetSleepVelocities(psdkActor, 1.1f, 1.1f);
						psdkActorSetMaxAngularVel(psdkActor, RAGDOLL_MAX_ANG_VEL);
						psdkActorSetSolverIterationCount(psdkActor, RAGDOLL_ITERATION_COUNT);
						psdkActorCreateAndAddCCDSkeleton(psdkActor, zerovec3);
						psdkActorSetCollisionGroup(psdkActor, p->isBox ? WC_SHAPEGROUP_RAGDOLL_BODY : WC_SHAPEGROUP_RAGDOLL_LIMB);
					}
				}
				EARRAY_FOREACH_END;
			}
		}

		xcase MR_MSG_BG_CREATE_OUTPUT:{
			switch(msg->in.bg.createOutput.dataClassBit){
				xcase MDC_BIT_POSITION_TARGET:{
					if(bg->addVel){
						// Set anything just to force prediction to run.

						mrmTargetSetAsVelocityBG(msg, zerovec3);
					}
				}

				xcase MDC_BIT_POSITION_CHANGE:{
					if(bg->ragdollOver){
						if(FALSE_THEN_SET(bg->snappedToGround)){
							Vec3 vel = {0.f, -3.5f, 0.f};
							mrmTranslatePositionBG(msg, vel, 1, 0);
						}
						
						if(FALSE_THEN_SET(bg->didNoInterp)){
							mrmSetStepIsNotInterpedBG(msg);
						}
					}
					else if(bg->endRequestApproved){
						// End the ragdoll since the FG says it's ok.
						mrmGetProcessCountBG(msg, &bg->end_pc);
						bg->ragdollOver = 1;
						bg->didNoInterp = 0;
						mmrSkeletonDestroyBG(msg, &bg->mmrSkeletonHandle);

						// Let the FG know that we did it
						toFG->ragdollOver = 1;
						mrmEnableMsgUpdatedToFG(msg);
					}
					else
					{
						Vec3 root_pos;
						bool bSetVel = false;

						if(!eaiSize(&localBG->simBodyHandles)){
							mrmNeedsSimBodyCreateBG(msg);
						}

						EARRAY_INT_CONST_FOREACH_BEGIN(localBG->simBodyHandles, i, isize);
						{
							U32					simBodyHandle = localBG->simBodyHandles[i];
							MRRagDollPartState*	ps;
							PSDKActor*			psdkActor;
							Mat4				matActor;

							if(!mrmSimBodyGetPSDKActorBG(msg, simBodyHandle, &psdkActor)){
								continue;
							}

							// Get/create actor values.

							while(i >= eaSize(&bg->partStates.states)){
								ps = StructAlloc(parse_MRRagDollPartState);
								eaPush(&bg->partStates.states, ps);
							}

							ps = bg->partStates.states[i];

							if(psdkActorGetMat(psdkActor, matActor)){
								copyVec3(matActor[3], ps->pos);
								getMat3YPR(matActor, ps->pyr);

								if(!i){
									if(FALSE_THEN_SET(bg->setOffsetToAnimRoot)){
										Quat rotCur;
										Mat4 matCur;
										Mat4 matActorInvert;
										Mat4 matCurInActorSpace;
										
										mrmGetPositionAndRotationBG(msg, matCur[3], rotCur);

										quatToMat(rotCur, matCur);

										invertMat4(matActor, matActorInvert);
										mulMat4(matActorInvert, matCur, matCurInActorSpace);

										getMat3YPR(matCurInActorSpace, bg->pyrOffsetToAnimRoot);
										copyVec3(matCurInActorSpace[3], bg->posOffsetToAnimRoot);

										assert(FINITEVEC3(bg->posOffsetToAnimRoot));
										assert(FINITEVEC3(bg->pyrOffsetToAnimRoot));
									}

									mrmSetPositionBG(msg, matActor[3]);
									copyVec3(matActor[3], root_pos);
									bg->getupOnBack = (matActor[2][1] > 0.0f);
									
									if(FALSE_THEN_SET(bg->didNoInterp)){
										mrmSetStepIsNotInterpedBG(msg);
									}
								}
							}

							#if CAN_CONTROL_RAGDOLL
							if(!i){
								if(mrmGetInputValueBitBG(msg, MIVI_BIT_FORWARD)){
									Vec3	vel = {0, 0, 100};
									Vec3	pyr = {mrmGetInputValueF32BG(msg, MIVI_F32_PITCH), mrmGetInputValueF32BG(msg, MIVI_F32_MOVE_YAW), 0};
									Vec3	out;

									createMat3YPR(mat, pyr);

									mulVecMat3(vel, mat, out);

									psdkActorAddForce(	psdkActor,
														out,
														true,
														true
														);
								}

								if(mrmGetInputValueBitBG(msg, MIVI_BIT_LEFT)){
									Vec3	vel = {-100, 0, 0};
									Vec3	pyr = {mrmGetInputValueF32BG(msg, MIVI_F32_PITCH), mrmGetInputValueF32BG(msg, MIVI_F32_MOVE_YAW), 0};
									Vec3	out;

									createMat3YPR(mat, pyr);

									mulVecMat3(vel, mat, out);

									psdkActorAddForce(	psdkActor,
														out,
														true,
														true
														);
								}

								if(mrmGetInputValueBitBG(msg, MIVI_BIT_RIGHT)){
									Vec3	vel = {100, 0, 0};
									Vec3	pyr = {mrmGetInputValueF32BG(msg, MIVI_F32_PITCH), mrmGetInputValueF32BG(msg, MIVI_F32_MOVE_YAW), 0};
									Vec3	out;

									createMat3YPR(mat, pyr);

									mulVecMat3(vel, mat, out);

									psdkActorAddForce(	psdkActor,
														out,
														true,
														true
														);
								}

								if(mrmGetInputValueBitBG(msg, MIVI_BIT_BACKWARD)){
									Vec3	vel = {0, 0, -100};
									Vec3	pyr = {mrmGetInputValueF32BG(msg, MIVI_F32_PITCH), mrmGetInputValueF32BG(msg, MIVI_F32_MOVE_YAW), 0};
									Vec3	out;

									createMat3YPR(mat, pyr);

									mulVecMat3(vel, mat, out);

									psdkActorAddForce(	psdkActor,
														out,
														true,
														true
														);
								}

								if(mrmGetInputValueBitBG(msg, MIVI_BIT_UP)){
									Vec3	vel = {0, 200, 0};
									Vec3	pyr = {mrmGetInputValueF32BG(msg, MIVI_F32_PITCH), mrmGetInputValueF32BG(msg, MIVI_F32_MOVE_YAW), 0};
									Vec3	out;

									createMat3YPR(mat, pyr);

									mulVecMat3(vel, mat, out);

									psdkActorAddForce(	psdkActor,
														out,
														true,
														true
														);
								}
							}
							#endif // CAN_CONTROL_RAGDOLL

							// Apply any forces that we've been sent
							if(bg->addVel){
								psdkActorAddVel(psdkActor,
												bg->vel);

								if(	!i &&
									!vec3IsZero(bg->angVel))
								{
									//psdkActorAddAngVel( psdkActor,
									//					bg->angVel);
								}

								bSetVel = true;
							}

							psdkActorGetVels(	psdkActor,
												ps->vel,
												ps->angVel);
						}
						EARRAY_FOREACH_END;

						if (bSetVel && bg->addVel)
						{
							bg->addVel = 0;

							// reset the counters since we've got a new impulse
							mrmGetProcessCountBG(msg, &bg->root_pos_update_pc);
							mrmGetProcessCountBG(msg, &bg->initial_pc);
						}


						// Now do test to see if it's time to terminate ragdoll
						{
							U32 current_pc;
							F32 age_seconds;

							mrmGetProcessCountBG(msg, &current_pc);
							age_seconds = (F32)(current_pc - bg->initial_pc) / MM_PROCESS_COUNTS_PER_SECOND;

							if(!gbNeverEndRagdoll && age_seconds > MAX_RAGDOLL_AGE && !bg->dead){
								// Request the FG permission to end ragdoll
								toFG->requestEnd = 1;
								mrmEnableMsgUpdatedToFG(msg);
							}else{
								// Look to see if we've moved more than a certain amount
								Vec3 vDist;
								subVec3(bg->last_root_pos, root_pos, vDist);
								if (lengthVec3Squared(vDist) > SQR(RAGDOLL_END_DISTANCE_THRESHOLD))
								{
									// We've moved more than the threshold, so reset the timer and position
									copyVec3(root_pos, bg->last_root_pos);
									mrmGetProcessCountBG(msg, &bg->root_pos_update_pc);
								}
								else
								{
									// still within threshold, so check timer
									F32 still_seconds = (current_pc - bg->root_pos_update_pc) / MM_PROCESS_COUNTS_PER_SECOND;
									if (!gbNeverEndRagdoll && still_seconds > RAGDOLL_END_TIME && !bg->dead)
									{
										// Request the FG permission to end ragdoll
										toFG->requestEnd = 1;
										mrmEnableMsgUpdatedToFG(msg);
									}
								}
							}
						}

						// Create the visible skeleton.

						if(	!bg->ragdollOver &&
							eaSize(&bg->partStates.states))
						{
							MMRSkeletonState state = {0};

							if(!bg->mmrSkeletonHandle){
								MMRSkeletonConstant		constant = {0};
								MMRSkeletonConstantNP	constantNP = {0};

								mrmLog(	msg,
									NULL,
										"Creating skeleton resource");

								copyVec3(	bg->posOffsetToAnimRoot,
											constantNP.posOffsetToAnimRoot);

								copyVec3(	bg->pyrOffsetToAnimRoot,
											constantNP.pyrOffsetToAnimRoot);

								assert(FINITEVEC3(constantNP.posOffsetToAnimRoot));
								assert(FINITEVEC3(constantNP.pyrOffsetToAnimRoot));

								EARRAY_CONST_FOREACH_BEGIN(bg->partStates.states, i, isize);
								{
									MRRagDollPart*			p = bg->parts->parts[i];
									MRRagDollPartState*		ps = bg->partStates.states[i];
									MMRSkeletonPart*		sp;
									MMRSkeletonPartNP*		spnp;
									MMRSkeletonPartState*	sps;

									sp = StructAlloc(parse_MMRSkeletonPart);
									spnp = StructAlloc(parse_MMRSkeletonPartNP);
									sps = StructAlloc(parse_MMRSkeletonPartState);

									spnp->isBox = p->isBox;

									if(spnp->isBox){
										copyVec3(p->xyzSizeBox, spnp->xyzSizeBox);
										copyVec3(p->matBox[3], spnp->posBox);
										getMat3YPR(p->matBox, spnp->pyrBox);
									}else{
										spnp->capsule = p->capsule;
									}
									
									sp->boneName = p->boneName;
									sp->parentBoneName = p->parentBoneName;

									eaPush(&constant.parts, sp);
									eaPush(&constantNP.parts, spnp);

									copyVec3(ps->pos, sps->pos);
									copyVec3(ps->pyr, sps->pyr);

									eaPush(&state.partStates.states, sps);
								}
								EARRAY_FOREACH_END;

								mmrSkeletonCreateBG(msg,
													&constant,
													&constantNP,
													&state,
													&bg->mmrSkeletonHandle);

								StructDeInit(	parse_MMRSkeletonConstant,
												&constant);
								
								StructDeInit(	parse_MMRSkeletonConstantNP,
												&constantNP);
							}else{
								mrmLog(	msg,
										NULL,
										"Creating skeleton resource %d state",
										bg->mmrSkeletonHandle);

								EARRAY_CONST_FOREACH_BEGIN(bg->partStates.states, i, isize);
								{
									MRRagDollPartState*		ps = bg->partStates.states[i];
									MMRSkeletonPartState*	sps;

									sps = StructAlloc(parse_MMRSkeletonPartState);

									copyVec3(ps->pos, sps->pos);
									copyVec3(ps->pyr, sps->pyr);

									eaPush(&state.partStates.states, sps);
								}
								EARRAY_FOREACH_END;

								mmrSkeletonCreateStateBG(	msg,
															bg->mmrSkeletonHandle,
															&state);
							}

							StructDeInit(	parse_MMRSkeletonState,
											&state);
						}
					}
				}

				xcase MDC_BIT_ROTATION_CHANGE:{
					if(eaiSize(&localBG->simBodyHandles)){
						U32			simBodyHandle = localBG->simBodyHandles[0];
						PSDKActor*	psdkActor;

						if(mrmSimBodyGetPSDKActorBG(msg, simBodyHandle, &psdkActor)){
							Mat4 mat;
							
							if(psdkActorGetMat(psdkActor, mat)){
								if(bg->ragdollOver){
									if(!bg->setGetupRotation){
										Mat3 newMat;
										Quat rot;
										Vec2 pyFace;
										Vec3 facingDir;
										if (bg->getupOnBack)
											scaleVec3(mat[1], -1.0f, facingDir);
										else
											copyVec3(mat[1], facingDir);
										orientMat3ToNormalAndForward(newMat, upvec, facingDir);
										mat3ToQuat(newMat, rot);
										pyFace[0] = 0.0f;
										pyFace[1] = getVec3Yaw(facingDir);
										mrmSetRotationBG(msg, rot);
										mrmSetFacePitchYawBG(msg, pyFace);
										bg->setGetupRotation = 1;
									}
								}else{
									Quat rot;
									mat3ToQuat(mat, rot);
									mrmSetRotationBG(msg, rot);
								}
							}
						}
					}
				}

				xcase MDC_BIT_ANIMATION:{

					//F32 fMaxKE = 0.0f, fMaxLV = 0.0f, fMaxAV = 0.0f;

					EARRAY_INT_CONST_FOREACH_BEGIN(localBG->simBodyHandles, i, isize);
					{
						U32					simBodyHandle = localBG->simBodyHandles[i];
						PSDKActor*			psdkActor;
						F32					fAngDamp;

						if(!mrmSimBodyGetPSDKActorBG(msg, simBodyHandle, &psdkActor)){
							continue;
						}

						fAngDamp =	psdkActorGetLinearVelocity(psdkActor) < 5.0f &&
									psdkActorGetAngularVelocity(psdkActor) < 5.0f ?
										2.0f*(5.0f-psdkActorGetLinearVelocity(psdkActor)) :
										0.0f;
						fAngDamp *= fAngDamp;
						psdkActorSetAngularDamping(psdkActor, fAngDamp);
						
						//if (psdkActorGetKineticEnergy(psdkActor) > fMaxKE)
						//	fMaxKE = psdkActorGetKineticEnergy(psdkActor);

						//if (psdkActorGetLinearVelocity(psdkActor) > fMaxLV)
						//	fMaxLV = psdkActorGetLinearVelocity(psdkActor);

						//if (psdkActorGetAngularVelocity(psdkActor) > fMaxAV)
						//	fMaxAV = psdkActorGetAngularVelocity(psdkActor);
					}
					EARRAY_FOREACH_END;

					//printf("Max KE = %f\t:\tLV = %f\t:\tAV = %f\n", fMaxKE, fMaxLV, fMaxAV);

					if(bg->ragdollOver){
						U32 current_pc;
						F32 overSeconds;
						mrmGetProcessCountBG(msg, &current_pc);
						overSeconds = (F32)(current_pc - bg->end_pc) / MM_PROCESS_COUNTS_PER_SECOND;
						if(overSeconds > 3.0f){
							mrmDestroySelf(msg);
						}
						else if (!FALSE_THEN_SET(bg->skippedGetupOneStep)){
							if (!bg->flashedGetupBits){
								mrmAnimAddBitBG(msg, mmAnimBitHandles.getUp);
								if (bg->getupOnBack)
									mrmAnimAddBitBG(msg, mmAnimBitHandles.getUpBack);
								bg->flashedGetupBits = 1;
							}
						}
					}
					else if (gConf.bNewAnimationSystem) {
						if (TRUE_THEN_RESET(bg->playAnimDeath)) {
							mrmAnimStartBG(msg, mmAnimBitHandles.death, 0);
							mrmAnimPlayFlagBG(msg, bg->deathDirectionBit, 0);
						}
						else if (TRUE_THEN_RESET(bg->playAnimRevive)) {
							mrmAnimStartBG(msg, mmAnimBitHandles.revive, 0);
						}
					}
				}
			}
		}

		xcase MR_MSG_BG_DATA_WAS_RELEASED:{
			U32 dataClassBits = msg->in.bg.dataWasReleased.dataClassBits;
			mrmDestroySelf(msg);

			if(dataClassBits & MDC_BIT_ROTATION_CHANGE){
				Quat rot;
				Vec3 pyFace;
				mrmGetRotationBG(msg, rot);
				mrmGetFacePitchYawBG(msg, pyFace);
				mrmShareOldF32BG(msg, "TargetFaceYaw", pyFace[1]);
				mrmShareOldQuatBG(msg, "TargetRotation", rot);
			}

			mrmReleaseAllDataOwnershipBG(msg);
		}
	}
#endif
}

#define GET_SYNC(sync)	MR_GET_SYNC(mr, mrRagDollMsgHandler, MRRagDoll, sync)

S32 mrRagDollSetEnabled(MovementRequester* mr,
						S32 isEnabled)
{
	MRRagDollSync* sync;

	if(GET_SYNC(&sync)){
		MR_SYNC_SET_IF_DIFF(mr, sync->isEnabled, (U32)isEnabled);

		return 1;
	}

	return 0;
}

S32 mrRagDollSetParts(	MovementRequester* mr,
						const MRRagDollParts* parts)
{
	MRRagDollFG* fg;

	if(mrGetFG(mr, mrRagDollMsgHandler, &fg)){
		mrEnableMsgCreateToBG(mr);

		StructDestroySafe(parse_MRRagDollParts, &fg->toBG.parts);

		fg->toBG.parts = StructAlloc(parse_MRRagDollParts);

		StructCopyAll(	parse_MRRagDollParts,
						parts,
						fg->toBG.parts);

		{
			WorldCollMaterial* wcm;
			if (wcMaterialFromPhysicalPropertyName(&wcm, "Ragdoll"))
			{
				wcMaterialGetIndex(wcm, &fg->toBG.material_index);
			}
		}

		return 1;
	}
	
	return 0;
}

static S32 getCapsuleBodyIndex(	MovementRequester* mr,
								U32* bodyIndexOut,
								F32 radius,
								F32 length,
								const Vec3 dir,
								const Vec3 pos)
{
	MovementBodyDesc*	bd;
	MovementBody*		b;
	Capsule				c;
	
	copyVec3(pos, c.vStart);
	normalizeCopyVec3(dir, c.vDir);
	c.fLength = length;
	c.fRadius = radius;

	mmBodyDescCreate(&bd);
	mmBodyDescAddCapsule(bd, &c);
	
	if(!mmBodyCreate(&b, &bd)){
		mrLog(	mr,
				NULL,
				"Failed to create capsule:\n"
				"  start(%1.3f, %1.3f, %1.3f)\n"
				"  dir(%1.3f, %1.3f, %1.3f)\n"
				"  length %1.3f\n"
				"  radius %1.3f",
				vecParamsXYZ(pos),
				vecParamsXYZ(dir),
				length,
				radius);

		return 0;
	}
	
	mmBodyGetIndex(b, bodyIndexOut);
	
	return 1;
}

static void mrRagdollInitMRParts(	MovementRequester* mr,
									const DynRagdollData* pData,
									MRRagDollParts* pParts,
									DynSkeleton* pSkeleton,
									U32 uiSeed)
{
	DynBaseSkeleton* pBaseSkeleton = dynScaledBaseSkeletonCreate(pSkeleton);
	bool bCreatedScaledBase = false;
	bool bPoseAnimTrackReady = false;
	DynTransform xRoot = xIdentity;
	if (!pBaseSkeleton)
		pBaseSkeleton = GET_REF(pSkeleton->hBaseSkeleton);
	else
		bCreatedScaledBase = true;

	if (pData->pcPoseAnimTrack)
	{
		int iNumPoses = eafSize(&pData->eaPoseFrames);
		if (iNumPoses)
		{
			U32 uiPose = uiSeed % iNumPoses;
			bPoseAnimTrackReady = dynSkeletonForceAnimation(pSkeleton, pData->pcPoseAnimTrack, pData->eaPoseFrames[uiPose]);
		}
		else
			bPoseAnimTrackReady = dynSkeletonForceAnimation(pSkeleton, pData->pcPoseAnimTrack, 1.0f);
	}

	if (!bPoseAnimTrackReady) {
		dynNodeGetWorldSpaceTransform(pSkeleton->pRoot, &xRoot);
	}

	FOR_EACH_IN_EARRAY_FORWARDS(pData->eaShapes, DynRagdollShape, pShape)
	{
		MRRagDollPart*	p = StructAlloc(parse_MRRagDollPart);
		p->boneName = pShape->pcBone;
		p->parentBoneName = pShape->pcParentBone;
		p->tuning = &pShape->tuning;
		switch (pShape->eShape)
		{
			xcase eRagdollShape_Box:
			{
				Vec3 vDir;
				const DynNode* pNode = NULL;
				Vec3 vRotatedOffset, vScaledOffset;
				Vec3 vNodeScale;


				// Now calculate original position in base skeleton
				pNode = dynBaseSkeletonFindNode(pBaseSkeleton, pShape->pcBone);
				if (pNode)
				{
					DynTransform trans;
					const DynNode* pParentNode;
					dynNodeGetWorldSpaceTransform(pNode, &trans);
					copyVec3(trans.vPos, p->pos);
					copyQuat(trans.qRot, p->rot);
					copyVec3(trans.vScale, vNodeScale);
					if (pShape->pcParentBone && (pParentNode = dynBaseSkeletonFindNode(pBaseSkeleton, pShape->pcParentBone)) )
					{
						DynTransform parentTrans, parentTransInv, localTrans;
						dynNodeGetWorldSpaceTransform(pParentNode, &parentTrans);
						dynTransformInverse(&parentTrans, &parentTransInv);
						dynTransformMultiply(&trans, &parentTransInv, &localTrans);
						copyVec3(localTrans.vPos, p->parentAnchor);
					}
					else
					{
						copyVec3(trans.vPos, p->parentAnchor);
					}
					setVec3(p->selfAnchor, 0.0f, 0.0f, 0.0f);
				}
				else
				{
					zeroVec3(p->pos);
					unitQuat(p->rot);
					zeroVec3(p->selfAnchor);
					unitVec3(vNodeScale);
				}

				p->isBox = 1;
				mulVecVec3(pShape->vOffset, vNodeScale, vScaledOffset);
				quatRotateVec3(pShape->qRotation, vScaledOffset, vRotatedOffset);
				quatRotateVec3(pShape->qRotation, upvec, vDir);

				quatToMat(pShape->qRotation, p->matBox);
				copyVec3(vRotatedOffset, p->matBox[3]);
				{
					Vec3 vCenter;
					Vec3 vCenterInBoxSpace;
					Vec3 vScaledMin, vScaledMax;
					mulVecVec3(pShape->vMax, vNodeScale, vScaledMax);
					mulVecVec3(pShape->vMin, vNodeScale, vScaledMin);
					addVec3(vScaledMax, vScaledMin, vCenter);
					scaleVec3(vCenter, 0.5f, vCenter);
					mulVecMat3(vCenter, p->matBox, vCenterInBoxSpace);
					addVec3(p->matBox[3], vCenterInBoxSpace, p->matBox[3]);
					subVec3(vScaledMax, vScaledMin, p->xyzSizeBox);
				}

				{
					Vec3 vSizeBoxUnscaled;
					subVec3(pShape->vMax, pShape->vMin, vSizeBoxUnscaled);
					p->volumeScale =	(p->xyzSizeBox[0]*p->xyzSizeBox[1]*p->xyzSizeBox[2]) /
										(vSizeBoxUnscaled[0]*vSizeBoxUnscaled[1]*vSizeBoxUnscaled[2]);
				}

				p->skinWidth = MIN(vNodeScale[0], MIN(vNodeScale[1], vNodeScale[2])) * RAGDOLL_SKIN_WIDTH;
				p->density = pShape->fDensity;
				p->parentIndex = pShape->iParentIndex;

				eaPush(&pParts->parts, p);
			}
			xcase eRagdollShape_Capsule:
			{
				Vec3 vDir;
				const DynNode* pNode = NULL;
				Vec3 vRotatedOffset, vScaledOffset;
				Vec3 vNodeScale;
				pNode = dynBaseSkeletonFindNode(pBaseSkeleton, pShape->pcBone);
				if (pNode)
				{
					DynTransform trans;
					const DynNode* pParentNode;
					dynNodeGetWorldSpaceTransform(pNode, &trans);
					copyVec3(trans.vPos, p->pos);
					copyQuat(trans.qRot, p->rot);
					copyVec3(trans.vScale, vNodeScale);
					if (pShape->pcParentBone && (pParentNode = dynBaseSkeletonFindNode(pBaseSkeleton, pShape->pcParentBone)))
					{
						DynTransform parentTrans, parentTransInv, localTrans;
						dynNodeGetWorldSpaceTransform(pParentNode, &parentTrans);
						dynTransformInverse(&parentTrans, &parentTransInv);
						dynTransformMultiply(&trans, &parentTransInv, &localTrans);
						copyVec3(localTrans.vPos, p->parentAnchor);
					}
					else
					{
						copyVec3(trans.vPos, p->parentAnchor);
					}
					setVec3(p->selfAnchor, 0, 0, 0);
				}
				else
				{
					zeroVec3(p->pos);
					unitQuat(p->rot);
					zeroVec3(p->selfAnchor);
					unitVec3(vNodeScale);
				}
				
				mulVecVec3(vNodeScale, pShape->vOffset, vScaledOffset);
				quatRotateVec3(pShape->qRotation, pShape->vOffset, vRotatedOffset); //should be scaled offset?
				quatRotateVec3(pShape->qRotation, upvec, vDir);

				{
					F32 fScaleDirDot = dotVec3(vDir, vNodeScale);
					F32 fMaxWidthScale;
					F32 fVolume, fVolumeScaled;
					F32 fLength;
					Vec3 vAntiDir;
					Vec3 vWidthScale;

					fLength = pShape->fHeightMax-pShape->fHeightMin;
					p->capsule.fLength = fabsf(fLength * fScaleDirDot);

					// width is length of projection of scale vector into width plane
					scaleVec3(vDir, -fScaleDirDot, vAntiDir);
					addVec3(vNodeScale, vAntiDir, vWidthScale);
					fMaxWidthScale = vec3MaxComponent(vWidthScale);
					p->capsule.fRadius = fMaxWidthScale * pShape->fRadius;

					fVolume = (PI*pShape->fRadius*pShape->fRadius) * (4.0f/3.0f*pShape->fRadius + fLength);
					fVolumeScaled = (PI*p->capsule.fRadius*p->capsule.fRadius) * (4.0f/3.0f*p->capsule.fRadius + p->capsule.fLength);
					p->volumeScale = fVolumeScaled/fVolume;

					p->skinWidth = fMaxWidthScale * RAGDOLL_SKIN_WIDTH;
				}

				copyVec3(vDir, p->capsule.vDir);
				copyVec3(vRotatedOffset, p->capsule.vStart);

				p->density = pShape->fDensity;

				p->parentIndex = pShape->iParentIndex;
				eaPush(&pParts->parts, p);
			}
		}


		// Now, calculate original pose
		if(p){
			const DynNode* pPosedBone;
			if (bPoseAnimTrackReady && (pPosedBone = dynSkeletonFindNode(pSkeleton, pShape->pcBone)))
			{
				DynTransform xBone;
				dynNodeGetWorldSpaceTransform(pPosedBone, &xBone);
				copyVec3(xBone.vPos, p->pose_pos);
				copyQuat(xBone.qRot, p->pose_rot);
				p->pose = 1;
			}
			else if (!bPoseAnimTrackReady && (pPosedBone = dynBaseSkeletonFindNode(pBaseSkeleton, pShape->pcBone)))
			{
				DynTransform xBone;
				dynNodeGetWorldSpaceTransform(pPosedBone, &xBone);
				addVec3(xRoot.vPos, xBone.vPos, p->pose_pos);
				quatMultiply(xRoot.qRot, xBone.qRot, p->pose_rot);
				p->pose = 1;
			}
			else
			{
				unitQuat(p->pose_rot);
				zeroVec3(p->pose_pos);
			}
		}
	}
	FOR_EACH_END;

	if (bCreatedScaledBase && pBaseSkeleton)
		dynBaseSkeletonFree(pBaseSkeleton);
}


S32 mrRagdollSetup(	MovementRequester* mr,
					Entity* e,
					U32 spc)

{
	PlayerCostume* playerCostume = costumeEntity_GetEffectiveCostume(e);
	WLCostume* pCostume;
	const DynRagdollData* ragdollData = NULL;
	DynSkeleton* pSkeleton = NULL;
	MRRagDollParts	parts = {0};
	DynNode entNode = {0};

	if (playerCostume)
	{
		PCSkeletonDef* skelDef = GET_REF(playerCostume->hSkeleton);
		if (skelDef)
		{
			SkelInfo* skelInfo = RefSystem_ReferentFromString("SkelInfo", skelDef->pcSkeleton);
			if (skelInfo)
			{
				ragdollData = GET_REF(skelInfo->hRagdollData);
			}
		}
		if (ragdollData)
		{
			pCostume = costumeGenerate_CreateWLCostume(playerCostume, e->pChar ? GET_REF(e->pChar->hSpecies) : NULL, e->pChar ? GET_REF(e->pChar->hClass) : NULL, NULL, costumeEntity_GetActiveSavedSlotType(e),
				GET_REF(e->costumeRef.hMood), NULL, "Entity.", e->myEntityType, e->myContainerID, e->pPlayer != NULL, NULL); // Use container ID
			wlCostumeAddToDictionary(pCostume, pCostume->pcName);
			pSkeleton = dynSkeletonCreate(pCostume, false, true, true, false, false, NULL);

			{
				Quat qEntRootRot;
				Vec3 vEntPos;
				entGetFaceSpaceQuat(e, qEntRootRot);
				entGetPos(e, vEntPos);
				dynNodeInitPersisted(&entNode);
				dynNodeSetRot(&entNode, qEntRootRot);
				dynNodeSetPos(&entNode, vEntPos);
				dynNodeParent(pSkeleton->pRoot, &entNode);
			}

			// Flag the ragdoll bones as 'critical' so they get posed
			FOR_EACH_IN_EARRAY(ragdollData->eaShapes, DynRagdollShape, pShape)
			{
				DynNode* pNode = dynSkeletonFindNodeNonConst(pSkeleton, pShape->pcBone);
				dynNodeSetCriticalBit(pNode);
			}
			FOR_EACH_END;
			dynNodeFindCriticalTree(pSkeleton->pRoot);
			
			dynDrawSetupAnimBoneInfo(pSkeleton, true, true);
			
			dynNodeCleanDirtyBits(&entNode);
		}
	}

	if (ragdollData && pSkeleton)
	{
		U32 uiSeed = spc ^ e->myRef;

		ANALYSIS_ASSUME(pSkeleton);
		mrRagdollInitMRParts(mr, ragdollData, &parts, pSkeleton, uiSeed);
		mrRagDollSetParts(mr, &parts);

		// Cleanup
		StructDeInit(parse_MRRagDollParts, &parts);
		dynSkeletonFree(pSkeleton);
		return 1;
	}
	return 0;
}

S32 mrRagdollEnded( MovementRequester* mr)
{
	MRRagDollFG* fg = NULL;
	if(mrGetFG(mr, mrRagDollMsgHandler, &fg)){
		if (fg->ragdollOver)
		{
			return 1;
		}
	}
	return 0;
}

void mrRagdollSetVelocity(	MovementRequester* mr,
							Entity* e,
							const Vec3 vel,
							const Vec3 angVel,
							F32 randomAngVel,
							U32 spcStart)
{
	MRRagDollFG* fg = NULL;

	// Note: needs to reinitialize the ragdoll mr if it's no longer running (post-ragdoll state)
	if(mrGetFG(mr, mrRagDollMsgHandler, &fg)){
		mrEnableMsgCreateToBG(mr);
		copyVec3(vel, fg->toBG.vel);
		fg->toBG.addVel = 1;
		copyVec3(angVel, fg->toBG.angVel);
		if (randomAngVel > 0.0f)
		{
			U32 uiSeed = spcStart ^ e->myRef;
			Vec3 randResult;
			randomSphereSeeded(&uiSeed, RandType_LCG, randomAngVel, randResult);
			addVec3(fg->toBG.angVel, randResult, fg->toBG.angVel);
		}
		fg->toBG.spcStart = spcStart;
		fg->toBG.endRequestApproved = 0;
	}
}

void mrRagdollSetDead(	MovementRequester* mr,
									S32 dead )
{
	MRRagDollFG* fg = NULL;

	// Note: needs to reinitialize the ragdoll mr if it's no longer running (post-ragdoll state)
	if(mrGetFG(mr, mrRagDollMsgHandler, &fg)){
		mrEnableMsgCreateToBG(mr);
		fg->toBG.dead = !!dead;
		fg->toBG.update_dead_state = 1;
	}
}

void mrRagdollSetDeathDirection(	MovementRequester *mr,
									const char *pcDirection)
{
	MRRagDollFG *fg = NULL;

	if (mrGetFG(mr, mrRagDollMsgHandler, &fg)) {
		mrEnableMsgCreateToBG(mr);
		fg->toBG.deathDirectionBit = pcDirection ? mmGetAnimBitHandleByName(pcDirection, 1) : 0;
		fg->toBG.update_deathDirectionBit = 1;
	}
}

#undef GET_SYNC

#include "AutoGen/EntityMovementProjectile_c_ast.c"
#include "AutoGen/EntityMovementProjectile_h_ast.c"

