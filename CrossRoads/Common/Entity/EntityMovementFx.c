/***************************************************************************
*     Copyright (c) 2005-2007, Cryptic Studios
*     All Rights Reserved
*     Confidential Property of Cryptic Studios
***************************************************************************/

#include "EntityMovementFx.h"
#include "EntityMovementManager.h"
#include "dynFxInfo.h"
#include "AutoGen\dynFxInfo_h_ast.h"
#include "dynFxInterface.h"
#include "dynAnimInterface.h"
#include "dynSkeleton.h"
#include "EntityLib.h"
#include "entCritter.h"
#include "..\..\..\libs\WorldLib\wlState.h"
#include "EString.h"
#include "LineDist.h"
#include "PowerActivation.h"
#include "PowerAnimFX.h"
#include "WorldGrid.h"
#include "AutoGen\Entity_h_ast.h"
#include "Capsule.h"
#include "rand.h"
#include "WorldColl.h"
#include "WorldGrid.h"
#include "WorldLib.h"

#ifdef GAMECLIENT
#include "entEnums.h"
#include "Character_target.h"
#include "gclEntity.h"
#include "GfxPrimitive.h"
#include "gclDemo.h"
#endif

AUTO_RUN_ANON(memBudgetAddMapping(__FILE__, BUDGET_GameSystems););

extern ParseTable parse_DynParamBlock[];
#define TYPE_parse_DynParamBlock DynParamBlock

static const S32 g_FXTriggerTimeoutTime = 5;

//--- Generic Fx Resource --------------------------------------------------------------------------

AUTO_RUN_MM_REGISTER_RESOURCE_MSG_HANDLER(	mmrFxMsgHandler,
											"Fx",
											MMRFx,
											MDC_BIT_ANIMATION);

AUTO_STRUCT;
typedef struct MMRFxActivatedFG {
	dtFx									dynFx; AST(INT)
	U32										triggerHappened : 1;
	U32										timedOut		: 1;
} MMRFxActivatedFG;

AUTO_STRUCT;
typedef struct MMRFxActivatedBG {
	S32										unused;
} MMRFxActivatedBG;

static S32 mmrFxTimeoutIsDestroy = 1;
AUTO_CMD_INT(mmrFxTimeoutIsDestroy, mmrFxTimeoutIsDestroy);

static U32 mmrFxJitterListNodeSelection(DynJitterList* pJList, MMRFxConstantJitterListData* pData)
{
	U32 uiResult = 0;
	if (pData) {
		switch (pJList->uiTokenIndex) {
			xcase PARSE_DYNPARENTBHVR_AT_INDEX:
			acase PARSE_DYNPARENTBHVR_GOTO_INDEX:
			acase PARSE_DYNPARENTBHVR_ORIENTTO_INDEX:
			acase PARSE_DYNPARENTBHVR_SCALETO_INDEX:
			{
				Entity* pEntSource = entFromEntityRefAnyPartition(pData->erSource);
				Entity* pEntTarget = entFromEntityRefAnyPartition(pData->erTarget);
				const DynSkeleton* pSkel = pEntTarget ? dynSkeletonFromGuid(pEntTarget->dyn.guidSkeleton) : NULL;

				if (pEntSource && pEntTarget && pSkel) {
					static DynListNode** s_eaSubList = NULL;
					int i, iListSize = eaSize(&pJList->pList->eaNodes);
					Vec3 vSourcePos, vSourceDir, vSourcePYR;
					F32 fHalfArc = pData->fArc * 0.5f;
					F32 fRange = pData->fRange;
					U32 uiClosestNodeIndex = 0;
					F32 fClosestNodeDistSqr = -1.0f;
					S32 iClosestNodeIndexInRangeAndArc = -1;
					F32 fClosestNodeDistSqrInRangeAndArc = -1.0f;

					eaClearFast(&s_eaSubList);

					entGetCombatPosDir(pEntSource,NULL,vSourcePos,NULL);
					entGetFacePY(pEntSource, vSourcePYR);
					vSourcePYR[2] = 0;
					vSourcePYR[1] = addAngle(vSourcePYR[1], pData->fYaw);
					createMat3_2_YP(vSourceDir, vSourcePYR);
					
					// Iterate over all nodes and get a sublist that is in the specified range and arc
					for (i = 0; i < iListSize; i++) {
						bool bSuccess = false;
						DynListNode* pListNode = pJList->pList->eaNodes[i];
						char* pchNodeName = (char*)MultiValGetAscii(&pListNode->mvVal, &bSuccess);

						if (bSuccess && strnicmp(pchNodeName, "T_", 2) == 0) {
							pchNodeName += 2;
						}
						if (bSuccess) {
							const DynNode* pDynNode = dynSkeletonFindNode(pSkel, pchNodeName);
							if (pDynNode) {
								Vec3 vNodePos, vTargetDir;
								F32 fDistToNodeSqr;
								dynNodeGetWorldSpacePos(pDynNode, vNodePos);
								subVec3(vNodePos, vSourcePos, vTargetDir);
								fDistToNodeSqr = lengthVec3Squared(vTargetDir);

								// Keep track of the closest node
								if (fClosestNodeDistSqr < 0.0f || fDistToNodeSqr < fClosestNodeDistSqr) {
									fClosestNodeDistSqr = fDistToNodeSqr;
									uiClosestNodeIndex = (U32)i;
								}
								if (fDistToNodeSqr <= fRange * fRange) {
									bool bValid = false;
									if (pData->fArc <= 0.0f || pData->fArc >= TWOPI) {
										// If the arc is 0, then it is considered to be a 360 degree arc
										bValid = true;
									} else if (pData->fArc >= PI - 0.0001f) {
										// Special handling for arcs greater than 180 degrees
										if (dotVec3(vSourceDir, vTargetDir) >= 0.0f) {
											// Early out if the node is in front of the entity
											bValid = true;
										} else if (pData->fArc > PI + 0.0001f) {
											Vec3 vConeDir;
											setVec3(vConeDir, -vSourceDir[0], -vSourceDir[1], -vSourceDir[2]);

											if (fDistToNodeSqr > FLT_EPSILON) {
												F32 fDistToNode = sqrtf(fDistToNodeSqr);
												if (!isFacingDirectionEx(vConeDir, vTargetDir, fDistToNode, PI - fHalfArc)) {
													bValid = true;
												}
											}
										}
									} else if (fDistToNodeSqr > FLT_EPSILON) {
										// The arc is less than 180 degrees, check to see if the node is in the arc
										F32 fDistToNode = sqrtf(fDistToNodeSqr);
										if (isFacingDirectionEx(vSourceDir, vTargetDir, fDistToNode, fHalfArc)) {
											bValid = true;
										}
									}
									if (bValid) {
										if (pData->eSelectType != kMMRFxJitterListSelectType_Closest) {
											eaPush(&s_eaSubList, pListNode);
										} else {
											if (iClosestNodeIndexInRangeAndArc < 0 || fDistToNodeSqr < fClosestNodeDistSqrInRangeAndArc) {
												fClosestNodeDistSqrInRangeAndArc = fDistToNodeSqr;
												iClosestNodeIndexInRangeAndArc = i;
											}
										}
									}
								}
							}
						}
					}
					if (eaSize(&s_eaSubList)) {
						int iIdx;

						switch (pData->eSelectType) {
							xcase kMMRFxJitterListSelectType_Random:
							{
								// Select a random node in the sublist
								uiResult = dynFxSelectRandomDynListNode(s_eaSubList, pJList->pList->bEqualChance);
							}
							xcase kMMRFxJitterListSelectType_RandomSeeded:
							{
								U32 uNumNodes = eaUSize(&s_eaSubList);
								U32 uSeed = pData->uSeed;
								// Select a random node in the sublist using the seed
								uiResult = randomU32Seeded(&uSeed, RandType_LCG) % uNumNodes;
							}
						}

						iIdx = eaFind(&pJList->pList->eaNodes, s_eaSubList[uiResult]);
						if (iIdx >= 0) {
							uiResult = (U32)iIdx;
						}
					} else {
						if (iClosestNodeIndexInRangeAndArc >= 0) {
							uiResult = (U32)iClosestNodeIndexInRangeAndArc;
						} else {
							uiResult = uiClosestNodeIndex;
						}
					}
				}
			}
		}
	}
	return uiResult;
}

static bool s_bShowLocalFX;
AUTO_CMD_INT(s_bShowLocalFX, showAllLocalFX) ACMD_ACCESSLEVEL(7) ACMD_COMMANDLINE;

static S32 mmrFxCreateDynFx(Entity* e,
							const MMRFxConstant* constant,
							const MMRFxConstantNP* constantNP,
							dtFx* dynFxOut,
							S32* needsRetryOut)
{
	Entity* eSource =	constant->erSource ?
							entFromEntityRefAnyPartition(constant->erSource) :
							constant->noSourceEnt ?
								NULL :
								e;

	if(	!dynDebugState.bNoNewFx &&
		(	!eSource ||
			eSource->dyn.guidFxMan)
		)
	{
		Entity*				eTarget = entFromEntityRefAnyPartition(constant->erTarget);
		dtFx				dynFx;
		DynParamBlock*		fxParams = NULL;
		bool				bNeedsRetry = false, bMiss = false;
		const char*			fxName = constant->fxName;
		Vec3 vecMiss;

		#ifdef GAMECLIENT
		
		// Determine alternate (non target, target only, source only, or
		// relation difference) or normal effect.

		REF_TO(DynFxInfo)	hFxInfo;
		DynFxInfo*			fxInfo = NULL;
		int					iFxRedirects = 4;
		bool				bFxChanged = true;

		while(	iFxRedirects &&
				bFxChanged)
		{
			bFxChanged = false;

			if(SET_HANDLE_FROM_STRING(hDynFxInfoDict, constant->fxName, hFxInfo)){
				fxInfo = GET_REF(hFxInfo);

				if (!s_bShowLocalFX)
				{
					if(!entIsLocalPlayer(eSource) &&
						!entIsLocalPlayer(eTarget))
					{
						// This isn't from or to the local player entity. Use a subdued version if it exists.

						if(SAFE_MEMBER(fxInfo, pcNonTargetVersion)){
							// There's a subdued version for this FX. Use that instead.

							fxName = fxInfo->pcNonTargetVersion;
							bFxChanged = true;
						}
					}else{
						// Handle different versions for source and target. 

						if(	SAFE_MEMBER(fxInfo, pcSourcePlayerVersion) &&
							entIsLocalPlayer(eSource))
						{
							fxName = fxInfo->pcSourcePlayerVersion;
							bFxChanged = true;
						}

						if(	SAFE_MEMBER(fxInfo, pcTargetPlayerVersion) &&
							entIsLocalPlayer(eTarget))
						{
							fxName = fxInfo->pcTargetPlayerVersion;
							bFxChanged = true;
						}
					}

					// Handle entity-relation-based FX changes.

					if(SAFE_MEMBER(fxInfo, pcEnemyVersion)){
						Entity *ePlayer = entActivePlayerPtr();

						if(	ePlayer &&
							eSource)
						{
							EntityRelation relation = entity_GetRelationEx(entGetPartitionIdx(ePlayer), ePlayer, eSource, false);

							if(relation == kEntityRelation_Foe){
								fxName = fxInfo->pcEnemyVersion;
								bFxChanged = true;
							}
						}
					}
				}

				REMOVE_HANDLE(hFxInfo);

				iFxRedirects--;
			}
		}

		#endif

		if(SAFE_MEMBER(constantNP, fxParams)){
			fxParams = dynParamBlockCopy(constantNP->fxParams);
		}

		if(constant->miss)
		{
			zeroVec3(vecMiss);
			if(eTarget)
			{
				const Capsule*const*	capsules;
				Vec3 vecEnt, vecLineStart, vecTargetDir;
				Quat quatEnt;
				entGetPos(eTarget,vecEnt);
				entGetRot(eTarget,quatEnt);
				if(eSource)
					entGetCombatPosDir(eSource,NULL,vecLineStart,NULL);
				else
					copyVec3(constant->vecSource,vecLineStart);
				subVec3(vecEnt,vecLineStart,vecTargetDir);
				normalVec3(vecTargetDir);

				if(mmGetCapsules(eTarget->mm.movement, &capsules))
				{
					const Capsule* pCapsule = capsules[0];
					WorldCollCollideResults wcResults;
					Vec3 vecDir, vecDirAdjust, vecLineDir;
					F32 fRadius = pCapsule->fRadius * (randomPositiveF32() * 0.35 + 1.15);

					quatRotateVec3(quatEnt,pCapsule->vDir,vecDir);
					crossVec3(vecDir, vecTargetDir, vecDirAdjust);

					if (normalVec3(vecDirAdjust) < 0.001f)
					{
						F32 fT;
						F32 fAngle = randomPositiveF32() * TWOPI;
						Vec3 vecCapStart, vecCapEnd;
						Mat3 xMat;

						// Special case for when the direction to the target and the capsule direction are colinear
						orientMat3(xMat, vecTargetDir);
						CapsuleMidlinePoint(pCapsule, vecEnt, quatEnt, 0.0f, vecCapStart);
						CapsuleMidlinePoint(pCapsule, vecEnt, quatEnt, 1.0f, vecCapEnd);
						PointLineSegClosestPoint(vecLineStart, vecCapStart, vecCapEnd, &fT, vecMiss); 
						rotateVecAboutAxis(fAngle, xMat[2], xMat[1], vecDirAdjust); 
						scaleAddVec3(vecDirAdjust, fRadius, vecMiss, vecMiss);
					}
					else
					{
						F32 fTotalLength = (pCapsule->fRadius*2+pCapsule->fLength);
						F32 fBorderLength = pCapsule->fLength + PI * pCapsule->fRadius;
						F32 fSign = randomPositiveF32() <= 0.5f ? -1 : 1;
						F32 fRand = randomPositiveF32() * fBorderLength;
						
						// Jitter miss directions around the silhouette of the capsule
						if (fRand < pCapsule->fLength)
						{
							F32 fRatio = fSign < 0 ? 0 : 1;
							fRatio -= fSign * (fRand / fTotalLength + pCapsule->fRadius / fTotalLength);
							CapsuleMidlinePoint(pCapsule, vecEnt, quatEnt, fRatio, vecMiss);
							scaleAddVec3(vecDirAdjust, fRadius * fSign, vecMiss, vecMiss);
						}
						else
						{
							F32 fRatio = fSign < 0 ? 1 : 0;
							F32 fAngle = PI - (fRand - pCapsule->fLength) / pCapsule->fRadius;
							fRatio += fSign * (pCapsule->fRadius / fTotalLength);
							CapsuleMidlinePoint(pCapsule, vecEnt, quatEnt, fRatio, vecMiss);
							rotateVecAboutAxis(fAngle,vecTargetDir,vecDirAdjust,vecDirAdjust); 
							scaleAddVec3(vecDirAdjust, fRadius * -fSign, vecMiss, vecMiss);
						}
					}

					// Now we've got a random target point somewhere not too far outside the main capsule,
					// extend the miss point out to the range of the power
					subVec3(vecMiss,vecLineStart,vecLineDir);
					if (constant->fRange > normalVec3(vecLineDir))
					{
						scaleAddVec3(vecLineDir, constant->fRange, vecLineStart, vecMiss);
					}

					// Make sure that the miss point isn't inside world geometry
					if (worldCollideRay(PARTITION_CLIENT, vecLineStart, vecMiss, WC_QUERY_BITS_WORLD_ALL, &wcResults))
					{
						copyVec3(wcResults.posWorldImpact, vecMiss);
					}
				}
				else
				{
					copyVec3(vecEnt,vecMiss);
				}

				eTarget = NULL;
				bMiss = true;
			}
		}

		if(constant->useTargetNode){
			dtNode guidTarget = 0;

			if(e){
				// Try to find a node that has been created with the same range value.

				EARRAY_FOREACH_REVERSE_BEGIN(e->dyn.eaTargetFXNodes, i);
				{
					if(nearSameF32(e->dyn.eaTargetFXNodes[i]->fRange, constant->fRange)){
						guidTarget = e->dyn.eaTargetFXNodes[i]->guidTarget;
						break;
					}
				}
				EARRAY_FOREACH_END;

				if(	!guidTarget &&
					(guidTarget = dtNodeCreate()))
				{
					// Create the target node with a new guid and range.

					EntityClientTargetFXNode*	pTargetNode;
					Vec3						vTargetDir;
					Vec3						vTargetPos;
					Vec3						vPos;
					Vec3						vResult;
					
					pTargetNode = StructCreate(parse_EntityClientTargetFXNode);
					pTargetNode->guidTarget = guidTarget;
					pTargetNode->fRange = constant->fRange;
					eaPush(&e->dyn.eaTargetFXNodes, pTargetNode);
						
					// Set the position of the target node.

					entGetCombatPosDir(e, NULL, vPos, vTargetDir);
					scaleAddVec3(vTargetDir, constant->fRange, vPos, vTargetPos);

					if(!combat_CheckLoS(PARTITION_CLIENT, vPos, vTargetPos, e, NULL, NULL, false, false, vResult)){
						copyVec3(vResult, vTargetPos);
					}

					dtNodeSetPos(guidTarget, vTargetPos);
				}
			}

			dynFx = dtAddFx(SAFE_MEMBER(e, dyn.guidFxMan),
							fxName,
							fxParams,
							guidTarget,
							0,
							constant->fHue,
							constant->sendTrigger ? constant->triggerID : 0,
							&bNeedsRetry,
							eDynFxSource_Power,
							NULL,
							NULL);
		}
		else if(!eSource){
			dynFx = dtAddFxFromLocation(fxName,
										fxParams,
										SAFE_MEMBER(eTarget, dyn.guidRoot),
										constant->vecSource,
										constant->vecTarget,
										constant->quatTarget,
										constant->fHue,
										constant->sendTrigger ? constant->triggerID : 0,
										eDynFxSource_Power);
		}
		else if(eTarget){
			MMRFxConstantJitterListData* cbData = NULL;
			if (constant->erSource != constant->erTarget &&
				((eTarget->pCritter && eTarget->pCritter->bUseClosestPowerAnimNode) ||
				(constant->nodeSelectionType != kPowerAnimNodeSelectionType_Default &&
				constant->nodeSelectionType != kPowerAnimNodeSelectionType_Random))) {
				cbData = calloc(1, sizeof(MMRFxConstantJitterListData));
				cbData->erSource = constant->erSource;
				cbData->erTarget = constant->erTarget;
				cbData->fRange = constant->fRange;
				cbData->fArc = constant->fArc;
				cbData->fYaw = constant->fYaw;
				cbData->uSeed = constant->pmID;
				
				if ((eTarget->pCritter && eTarget->pCritter->bUseClosestPowerAnimNode) ||
					constant->nodeSelectionType == kPowerAnimNodeSelectionType_ClosestInRangeAndArc) {
					cbData->eSelectType = kMMRFxJitterListSelectType_Closest;
				} else if (constant->alwaysSelectSameNode) {
					cbData->eSelectType = kMMRFxJitterListSelectType_RandomSeeded;
				} else {
					cbData->eSelectType = kMMRFxJitterListSelectType_Random;
				}
			}
			dynFx = dtAddFx(eSource->dyn.guidFxMan,
							fxName,
							fxParams,
							SAFE_MEMBER(eTarget, dyn.guidRoot),
							0,
							constant->fHue,
							constant->sendTrigger ? constant->triggerID : 0,
							&bNeedsRetry,
							eDynFxSource_Power,
							cbData ? mmrFxJitterListNodeSelection : NULL,
							cbData);
			if (!dynFx && cbData){
				SAFE_FREE(cbData);
			}
		}
		else if(constant->erTarget &&
				constantNP)
		{
			dynFx = dtAddFxAtLocation(	eSource->dyn.guidFxMan,
										fxName,
										fxParams,
										NULL,
										bMiss ? vecMiss : constantNP->vecLastKnownTarget,
										constant->quatTarget,
										constant->fHue,
										constant->sendTrigger ? constant->triggerID : 0,
										&bNeedsRetry,
										eDynFxSource_Power);
		}else{
			dynFx = dtAddFxAtLocation(	eSource->dyn.guidFxMan,
										fxName,
										fxParams,
										NULL,
										bMiss ? vecMiss : constant->vecTarget,
										constant->quatTarget,
										constant->fHue,
										constant->sendTrigger ? constant->triggerID : 0,
										&bNeedsRetry,
										eDynFxSource_Power);
		}

		if(dynFxOut){
			*dynFxOut = dynFx;
		}

		*needsRetryOut = !!bNeedsRetry;

		return !!dynFx;
	}

	return 0;
}

static S32 mmrFxDestroyDynFx(	const MovementManagedResourceMsg* msg,
								MMRFxActivatedFG* activated,
								const MMRFxConstant* constant)
{
	if(activated->dynFx){
		if(	msg->in.handle ||
			!dynFxInfoSelfTerminates(constant->fxName))
		{
			dtFxKill(activated->dynFx);
		}

		activated->dynFx = 0;
		
		return 1;
	}
	return 0;
}

void mmrFxMsgHandler(const MovementManagedResourceMsg* msg){
	const MMRFxConstant*	constant = msg->in.constant;
	const MMRFxConstantNP*	constantNP = msg->in.constantNP;
	
	switch(msg->in.msgType){
		xcase MMR_MSG_FG_TEMP_FIX_FOR_DEMO:{
			MMRFxConstant*		c = msg->in.fg.tempFixForDemo.constant;
			MMRFxConstantNP*	cNP = msg->in.fg.tempFixForDemo.constantNP;
			
			if(c->fxParams_NEVER_SET_THIS){
				cNP->fxParams = c->fxParams_NEVER_SET_THIS;
				c->fxParams_NEVER_SET_THIS = NULL;
			}
		}
		
		xcase MMR_MSG_FG_FIXUP_CONSTANT_AFTER_COPY:{
			MMRFxConstant*		c = msg->in.fg.fixupConstantAfterCopy.constant;
			MMRFxConstantNP*	cNP = msg->in.fg.fixupConstantAfterCopy.constantNP;
			
			if(c->erSource == msg->in.fg.fixupConstantAfterCopy.erSource){
				c->erSource = msg->in.fg.fixupConstantAfterCopy.er;
			}

			if(c->erTarget == msg->in.fg.fixupConstantAfterCopy.erSource){
				c->erTarget = msg->in.fg.fixupConstantAfterCopy.er;
			}
		}

		xcase MMR_MSG_GET_CONSTANT_DEBUG_STRING:{
			char** estrBuffer = msg->in.getDebugString.estrBuffer;

			estrConcatf(estrBuffer,
						"\"%s\""
						" pm(%d,%d,%d)"
						" erTarget %d"
						" erSource %d"
						" vt(%1.2f, %1.2f, %1.2f)"
						" vt[%8.8x, %8.8x, %8.8x]"
						" vs(%1.2f, %1.2f, %1.2f)"
						" vs[%8.8x, %8.8x, %8.8x]"
						" hue %f [%d]"
						" noSource %d"
						" %s 0x%8.8x"
						" wakeAfterThisManyPCs %d"
						,
						constant->fxName,
						constant->pmType,
						constant->pmID,
						constant->pmSubID,
						constant->erTarget,
						constant->erSource,
						vecParamsXYZ(constant->vecTarget),
						vecParamsXYZ((S32*)constant->vecTarget),
						vecParamsXYZ(constant->vecSource),
						vecParamsXYZ((S32*)constant->vecSource),
						constant->fHue,
						*(S32*)&constant->fHue,
						constant->noSourceEnt,
						constant->waitForTrigger ?
							constant->triggerIsEntityID ?
								"waitForEntityTrigger" :
								"waitForEventTrigger" :
							constant->sendTrigger ?
								"sendTrigger" :
								"noTrigger",
						constant->triggerID,
						constant->wakeAfterThisManyPCs
						);

			if(SAFE_MEMBER(constantNP, fxParams)){
				char* estr = NULL;
				
				estrStackCreate(&estr);

				ParserWriteText(&estr, parse_DynParamBlock, constantNP->fxParams, 0, 0, 0);
				
				while(	estrLength(&estr) &&
						estr[estrLength(&estr) - 1] == '\n')
				{
					estrSetSize(&estr, estrLength(&estr) - 1);
				}

				estrConcatf(estrBuffer,
							"\nFxParams: %s",
							estr);
				
				estrDestroy(&estr);
			}
		}
		
		xcase MMR_MSG_FG_GET_STATE_DEBUG_STRING:{
			char**				estrBuffer = msg->in.fg.getStateDebugString.estrBuffer;
			MMRFxActivatedFG*	activated = msg->in.activatedStruct;

			estrConcatf(estrBuffer,
						"DynFx handle: 0x%8.8x"
						", Flags: %s",
						activated->dynFx,
						activated->triggerHappened ? "triggerHappened, " : "");
		}

		xcase MMR_MSG_FG_SET_STATE:{
			MMRFxActivatedFG*	activated = msg->in.activatedStruct;
			dtFx				dynFx;
			
			if(entIsServer()){
				break;
			}
			
			if(	constant->waitForTrigger &&
				(	mmrFxTimeoutIsDestroy &&
					!activated->triggerHappened
					||
					!mmrFxTimeoutIsDestroy &&
					!activated->timedOut)
				)
			{
				if(	!activated->timedOut ||
					!mmrFxTimeoutIsDestroy)
				{
					// Wait for trigger and set to time out.
					
					U32 wakeAfterPCs = FIRST_IF_SET(constant->wakeAfterThisManyPCs,
													MM_PROCESS_COUNTS_PER_SECOND * g_FXTriggerTimeoutTime);
												
					mmrmSetWaitingForTriggerFG(msg, 1);
					
					if(msg->in.fg.setState.state.local.spcStart){
						mmrmSetWaitingForWakeFG(msg,
												msg->in.fg.setState.state.local.spcStart +
													wakeAfterPCs);
					}
					else if(msg->in.fg.setState.state.net.spcStart){
						mmrmSetWaitingForWakeFG(msg,
												msg->in.fg.setState.state.net.spcStart +
													wakeAfterPCs);
					}else{
						mmrmSetNeedsSetStateFG(msg);
					}

					break;
				}
				else if(msg->in.handle){
					// Timed out, but it has a handle, so it has to be created.

					activated->triggerHappened = 1;
				}else{
					// Timed out and has no handle, so die silently.

					break;
				}
			}
			
			if(	!msg->in.handle ||
				!activated->dynFx)
			{
				S32 needsRetry = 0;

				if(mmrFxCreateDynFx(msg->in.fg.mmUserPointer,
									constant,
									constantNP,
									&dynFx,
									&needsRetry))
				{
					activated->dynFx = dynFx;
				}
				else if(wl_state.dynEnabled){
					msg->out->fg.setState.flags.needsRetry = !!needsRetry;
				}
			}
		}
		
		xcase MMR_MSG_FG_CHECK_FOR_INVALID_STATE:{
			MMRFxActivatedFG* activated = msg->in.activatedStruct;
			
			if(	(!activated->dynFx ||
				 !dynFxFromGuid(activated->dynFx)) && 
				 !constant->isFlashedFX)
			{
				activated->dynFx = 0;
				mmrmSetNeedsSetStateFG(msg);
			}
		}

		xcase MMR_MSG_FG_DESTROYED:{
			MMRFxActivatedFG* activated = msg->in.activatedStruct;

			if (!msg->in.flags.clear)
			{
				mmrFxDestroyDynFx(msg, activated, constant);
			}
		}

		xcase MMR_MSG_FG_TRIGGER:{
			MMRFxActivatedFG*		activated = msg->in.activatedStruct;
			const MovementTrigger*	t = msg->in.fg.trigger.trigger;

			if(!activated->triggerHappened){
				if(	constant->triggerIsEntityID == t->flags.isEntityID &&
					#if GAMECLIENT
					(	constant->triggerID == t->triggerID ||
						(	//the odd masking is to match MM_ENTITY_HIT_REACT_ID(er,id) for the case of the catch-all power id of 0 being used during demo playback
							gConf.bNewAnimationSystem &&
							demo_playingBack() &&
							((constant->triggerID & 0xFFFF00FF) == (t->triggerID & 0xFFFF00FF)) &&
							(((constant->triggerID & 0x0000FF00)|(t->triggerID & 0x0000FF00)) == (constant->triggerID & 0x0000FF00))
					)))
					#else
					constant->triggerID == t->triggerID)
					#endif
				{
					activated->triggerHappened = 1;
					
					mmrmSetNeedsSetStateFG(msg);
				}else{
					mmrmSetWaitingForTriggerFG(msg, 1);
				}
			}
		}
		
		xcase MMR_MSG_FG_WAKE:{
			MMRFxActivatedFG* activated = msg->in.activatedStruct;

			if(FALSE_THEN_SET(activated->timedOut)){
				mmrmSetNeedsSetStateFG(msg);
				mmrmSetWaitingForTriggerFG(msg, 0);
			}
		}
	}
}

static U32 mmrFxGetClassID(void){
	static U32 id;
	
	if(!id){
		if(!mmGetManagedResourceIDByMsgHandler(mmrFxMsgHandler, &id)){
			assert(0);
		}
	}
	
	return id;
}

S32 mmrFxCreateBG(	const MovementRequesterMsg* msg,
					U32* handleOut,
					const MMRFxConstant* constant,
					const MMRFxConstantNP* constantNP)
{
	if(constant){
		assert(FINITEVEC3(constant->vecSource));
		assert(FINITEVEC3(constant->vecTarget));
		assert(FINITEQUAT(constant->quatTarget));
	}

	return mrmResourceCreateBG(	msg,
								handleOut,
								mmrFxGetClassID(),
								constant,
								constantNP,
								NULL);
}

S32 mmrFxDestroyBG(	const MovementRequesterMsg* msg,
					U32* handleInOut)
{
	return mrmResourceDestroyBG(msg,
								mmrFxGetClassID(),
								handleInOut);
}

S32 mmrFxClearBG(	const MovementRequesterMsg* msg,
					U32* handleInOut)
{
	return mrmResourceClearBG(	msg,
								mmrFxGetClassID(),
								handleInOut);
}

S32 mmrFxCopyAllFromManager(MovementManager* mm,
							const MovementManager* mmSource)
{
	return mmResourcesCopyFromManager(	mm,
										mmSource,
										mmrFxGetClassID());
}

//--- Hit React Resource ---------------------------------------------------------------------------

AUTO_RUN_MM_REGISTER_RESOURCE_MSG_HANDLER(	mmrHitReactMsgHandler,
											"HitReact",
											MMRHitReact,
											MDC_BIT_ANIMATION);

AUTO_STRUCT;
typedef struct MMRHitReactActivatedFG {
	U32				triggerHappened : 1;
	U32				timedOut		: 1;
	const char*		dirBitName;				AST(POOL_STRING)
} MMRHitReactActivatedFG;

AUTO_STRUCT;
typedef struct MMRHitReactActivatedBG {
	S32		unused;
} MMRHitReactActivatedBG;

void mmrHitReactMsgHandler(const MovementManagedResourceMsg* msg){
	const MMRHitReactConstant*		constant = msg->in.constant;
	const MMRHitReactConstantNP*	constantNP = msg->in.constantNP;
	
	switch(msg->in.msgType){
		xcase MMR_MSG_GET_CONSTANT_DEBUG_STRING:{
			char** estrBuffer = msg->in.getDebugString.estrBuffer;

			if(constant->waitForTrigger){
				estrConcatf(estrBuffer,
							", waitForTrigger(%s 0x%8.8x)",
							constant->triggerIsEntityID ?
								"Entity" :
								"Event",
							constant->triggerID);
			}
			
			estrConcatf(estrBuffer, "Bits:\"");

			EARRAY_INT_CONST_FOREACH_BEGIN(constant->animBits, i, isize);
			{
				estrConcatf(estrBuffer,
							"%u%s",
							constant->animBits[i],
							i == isize - 1 ? "\"" : ",");
			}
			EARRAY_FOREACH_END;

			if(constant->wakeAfterThisManyPCs){
				estrConcatf(estrBuffer,
							", wakeAfterThisManyPCs %u",
							constant->wakeAfterThisManyPCs);
			}
		}
		
		xcase MMR_MSG_FG_TRANSLATE_SERVER_TO_CLIENT:{
			MMRHitReactConstant*	c = msg->in.fg.translateServerToClient.constant;
			MMRHitReactConstantNP*	cNP = msg->in.fg.translateServerToClient.constantNP;

			mmTranslateAnimBitsServerToClient(c->animBits,0);
		}
		
		xcase MMR_MSG_FG_SET_STATE:{
			MMRHitReactActivatedFG*	activated = msg->in.activatedStruct;
			
			if(entIsServer()){
				break;
			}

			if(	constant->waitForTrigger &&
				(	mmrFxTimeoutIsDestroy &&
					!activated->triggerHappened
					||
					!mmrFxTimeoutIsDestroy &&
					!activated->timedOut)
				)
			{
				if(	!activated->timedOut ||
					!mmrFxTimeoutIsDestroy)
				{
					U32 wakeAfterPCs = FIRST_IF_SET(constant->wakeAfterThisManyPCs,
													MM_PROCESS_COUNTS_PER_SECOND * g_FXTriggerTimeoutTime);
												
					mmrmSetWaitingForTriggerFG(msg, 1);
					
					if(msg->in.fg.setState.state.local.spcStart){
						mmrmSetWaitingForWakeFG(msg,
												msg->in.fg.setState.state.local.spcStart +
													wakeAfterPCs);
					}
					else if(msg->in.fg.setState.state.net.spcStart){
						mmrmSetWaitingForWakeFG(msg,
												msg->in.fg.setState.state.net.spcStart +
													wakeAfterPCs);
					}else{
						mmrmSetNeedsSetStateFG(msg);
					}
				}
			}
		}

		xcase MMR_MSG_FG_DESTROYED:{
			MMRHitReactActivatedFG* activated = msg->in.activatedStruct;
		}
		
		xcase MMR_MSG_FG_TRIGGER:{
			MMRHitReactActivatedFG*	activated = msg->in.activatedStruct;
			const MovementTrigger*	t = msg->in.fg.trigger.trigger;

			if(activated->triggerHappened){
				break;
			}

			if(	constant->triggerIsEntityID == t->flags.isEntityID &&
				#if GAMECLIENT
				(	constant->triggerID == t->triggerID ||
					(	//the odd masking is to match MM_ENTITY_HIT_REACT_ID(er,id) for the case of the catch-all power id of 0 being used during demo playback
						gConf.bNewAnimationSystem &&
						demo_playingBack() &&
						((constant->triggerID & 0xFFFF00FF) == (t->triggerID & 0xFFFF00FF)) &&
						(((constant->triggerID & 0x0000FF00)|(t->triggerID & 0x0000FF00)) == (constant->triggerID & 0x0000FF00))
				)))
				#else
				constant->triggerID == t->triggerID)
				#endif
			{
				Entity* e = msg->in.fg.mmUserPointer;
				
				activated->triggerHappened = 1;

				if(e){
					// Calculate direction bits.

					Mat3 matFaceSpace;
					
					entGetFaceSpaceMat3(e, matFaceSpace);

					activated->dirBitName = dtCalculateHitReactDirectionBit(matFaceSpace,
																			t->vel);
				}

				mmrmSetHasAnimBitFG(msg);
			}else{
				mmrmSetWaitingForTriggerFG(msg, 1);
			}
		}
		
		xcase MMR_MSG_FG_SET_ANIM_BITS:{
			MMRHitReactActivatedFG* activated = msg->in.activatedStruct;

			EARRAY_INT_CONST_FOREACH_BEGIN(constant->animBits, i, isize);
			{
				mmrmSetAnimBitFG(msg, constant->animBits[i], 1);
			}
			EARRAY_FOREACH_END;

			if(activated->dirBitName){
				mmrmSetAnimBitFG(msg, mmGetAnimBitHandleByName(activated->dirBitName, 1), 0);
			}
		}

		xcase MMR_MSG_FG_WAKE:{
			MMRHitReactActivatedFG* activated = msg->in.activatedStruct;

			if(FALSE_THEN_SET(activated->timedOut)){
				//mmrmSetHasAnimBitFG(msg);
				mmrmSetWaitingForTriggerFG(msg, 0);
			}
		}
	}
}

static U32 mmrHitReactGetID(void){
	static U32 id;

	if(!id){
		if(!mmGetManagedResourceIDByMsgHandler(mmrHitReactMsgHandler, &id)){
			assert(0);
		}
	}
	
	return id;
}

S32 mmrHitReactCreateBG(const MovementRequesterMsg* msg,
						U32* handleOut,
						const MMRHitReactConstant* constant,
						const MMRHitReactConstantNP* constantNP)
{
	return mrmResourceCreateBG(	msg,
								handleOut,
								mmrHitReactGetID(),
								constant,
								constantNP,
								NULL);
}

S32 mmrHitReactDestroyBG(	const MovementRequesterMsg* msg,
							U32* handleInOut)
{
	return mrmResourceDestroyBG(msg,
								mmrHitReactGetID(),
								handleInOut);
}

#include "autogen/EntityMovementFx_c_ast.c"
#include "autogen/EntityMovementFx_h_ast.c"
