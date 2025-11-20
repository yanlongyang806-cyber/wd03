/***************************************************************************
*     Copyright (c) 2009, Cryptic Studios
*     All Rights Reserved
*     Confidential Property of Cryptic Studios
***************************************************************************/

#include "InteractionUI.h"
#include "EntityInteraction.h"
#include "UIGen.h"
#include "Entity.h"
#include "GameAccountDataCommon.h"
#include "mechanics_common.h"
#include "Player.h"
#include "Team.h"
#include "cmdparse.h"
#include "Expression.h"
#include "PowerActivation.h"
#include "RegionRules.h"
#include "StringCache.h"
#include "WorldGrid.h"
#include "Character.h"
#include "Character_combat.h"
#include "gclControlScheme.h"
#include "gclEntity.h"
#include "gclUtils.h"
#include "gclMapState.h"
#include "inputMouse.h"
#include "ClientTargeting.h"
#include "CharacterAttribs.h"
#include "contact_common.h"
#include "cmdClient.h"
#include "GameClientLib.h"
#include "GameStringFormat.h"
#include "Character_target.h"
#include "interaction_common.h"
#include "mission_common.h"
#include "AutoGen/InteractionUI_c_ast.h"
#include "AutoGen/InteractionUI_h_ast.h"
#include "wlInteraction.h"
#include "NotifyCommon.h"
#include "PowersAutoDesc.h"
#include "CombatConfig.h"
#include "gclPlayerControl.h"

#include "AutoGen/GameServerLib_autogen_ServerCmdWrappers.h"

AUTO_RUN_ANON(memBudgetAddMapping(__FILE__, BUDGET_UISystem););

// ---------------------------------------------------------------------------
// Logic to pick best interact on the client
// ---------------------------------------------------------------------------

#define ENT_FRONT_FACING_ANGLE RAD(45)

//static REF_TO(WorldInteractionNode) s_tooltipNodeRef;

// Used to sort interaction options
static int interaction_CompareOptions(const InteractOption **ppLeft, const InteractOption **ppRight)
{
	// Sort by priority first
	if ((*ppLeft)->iPriority > (*ppRight)->iPriority) {
		return -1;
	} else if ((*ppLeft)->iPriority < (*ppRight)->iPriority) {
		return 1;
	}

	// Sort entities, then volumes, then nodes, then none
	if ((*ppLeft)->entRef && !(*ppRight)->entRef) {
		return -1;
	} else if (!(*ppLeft)->entRef && (*ppRight)->entRef) {
		return 1;
	}
	if ((*ppLeft)->pcVolumeName && !(*ppRight)->pcVolumeName) {
		return -1;
	} else if (!(*ppLeft)->pcVolumeName && (*ppRight)->pcVolumeName) {
		return 1;
	}
	if (IS_HANDLE_ACTIVE((*ppLeft)->hNode) && !IS_HANDLE_ACTIVE((*ppRight)->hNode)) {
		return -1;
	} else if (!IS_HANDLE_ACTIVE((*ppLeft)->hNode) && IS_HANDLE_ACTIVE((*ppRight)->hNode)) {
		return 1;
	}

	// Then sort by display string
	return stricmp((*ppLeft)->pcInteractString ? (*ppLeft)->pcInteractString : "", (*ppRight)->pcInteractString ? (*ppRight)->pcInteractString : "");
}


__forceinline static void entity_ClearOverrideInteract(Entity *ent)
{
	ent->pPlayer->InteractStatus.overrideRef = 0;
	REMOVE_HANDLE(ent->pPlayer->InteractStatus.hOverrideNode);
	ent->pPlayer->InteractStatus.overrideSet = 0;

}

//internal helper function that assigns the proper entity values
//NOTE: assumes either pCloseEnt or pCloseNode is valid, but not both
__forceinline static void entity_SetBestInteract( Entity* ent, EntityRef iCloseEnt, WorldInteractionNode* pCloseNode )
{
	int i;

	// Clear data
	eaClear(&ent->pPlayer->InteractStatus.eaPromptedInteractOptions);
	ent->pPlayer->InteractStatus.promptPickup = false;
 	ent->pPlayer->InteractStatus.promptInteraction = true;

	// Reload based on the new target
	if (iCloseEnt) {
		ent->pPlayer->InteractStatus.preferredTargetEntity = iCloseEnt;
		REMOVE_HANDLE(ent->pPlayer->InteractStatus.hPreferredTargetNode);

		for(i=0; i<eaSize(&ent->pPlayer->InteractStatus.interactOptions.eaOptions); ++i) {
			InteractOption *pOption = ent->pPlayer->InteractStatus.interactOptions.eaOptions[i];
			if (pOption->entRef == iCloseEnt) {
				eaPush(&ent->pPlayer->InteractStatus.eaPromptedInteractOptions, pOption);
			}
		}
	} else {
		ent->pPlayer->InteractStatus.preferredTargetEntity = -1;
		SET_HANDLE_FROM_REFERENT(INTERACTION_DICTIONARY, pCloseNode, ent->pPlayer->InteractStatus.hPreferredTargetNode);

		for(i=0; i<eaSize(&ent->pPlayer->InteractStatus.interactOptions.eaOptions); ++i) {
			InteractOption *pOption = ent->pPlayer->InteractStatus.interactOptions.eaOptions[i];
			if (GET_REF(pOption->hNode) == pCloseNode) {
				eaPush(&ent->pPlayer->InteractStatus.eaPromptedInteractOptions, pOption);
				if (pOption->bCanPickup) {
					ent->pPlayer->InteractStatus.promptPickup = true;
				}
			}
		}
	}

	if (eaSize(&ent->pPlayer->InteractStatus.eaPromptedInteractOptions) > 1) {
		eaQSort(ent->pPlayer->InteractStatus.eaPromptedInteractOptions, interaction_CompareOptions);
	}
}

__forceinline static void entity_ClearBestInteract( Entity* ent )
{
	eaClear(&ent->pPlayer->InteractStatus.eaPromptedInteractOptions);
	ent->pPlayer->InteractStatus.preferredTargetEntity = -1;
	REMOVE_HANDLE(ent->pPlayer->InteractStatus.hPreferredTargetNode);
	ent->pPlayer->InteractStatus.promptInteraction = 0;
	ent->pPlayer->InteractStatus.promptPickup = 0;
}

//this struct keeps track of possible interaction entities/nodes in order
//to optimally find that best entity or node
AUTO_STRUCT;
typedef struct InteractTargetInfo
{
	void*	pData;		NO_AST
	void*	pTarget;	NO_AST
	F32		fDistance;
	F32		fAngle;
	Vec3	vTarget;
} InteractTargetInfo;

static F32 interactTargetInfo_GetWeightedDistance(const InteractTargetInfo *pTargetInfo)
{
	// Something behind you counts as 3x as far away as something directly in front of you
	return (pTargetInfo->fDistance * (1+2*(pTargetInfo->fAngle/PI)));
}

S32 cmpSortInteractTargetInfoByWeightedDistance( const InteractTargetInfo** pInfoA, const InteractTargetInfo** pInfoB )
{
	// Adjust the effective distance according to the angle
	F32 fDistanceA = interactTargetInfo_GetWeightedDistance(*pInfoA);
	F32 fDistanceB = interactTargetInfo_GetWeightedDistance(*pInfoB);

	if ( fDistanceA < fDistanceB )
		return -1;
	if ( fDistanceA > fDistanceB )
		return 1;
	return 0;
}

__forceinline static void qsortInteractTargetInfoInIndexRange(InteractTargetInfo** ppArray,
																		S32 iStart, S32 iEnd )
{
	if ( ppArray != NULL )
	{
		InteractTargetInfo** ppArrayOffset = ppArray + iStart;

		S32 iRange = ABS( iEnd - iStart );

		eaQSortCPP( ppArrayOffset, cmpSortInteractTargetInfoByWeightedDistance, iRange );
	}
}

//searches for the best ent or node in an index range
static bool entity_SearchForBestInteract(	Entity* ent,
											S32 iEntStart, S32 iEntEnd,
											S32 iNodeStart, S32 iNodeEnd,
											Vec3 vSource,
											InteractTargetInfo** ppEntInfoArray,
											InteractTargetInfo** ppNodeInfoArray,
											EntityRef* piCloseEnt,
											WorldInteractionNode** ppCloseNode )
{
	S32 iCurrEntPos = iEntStart;
	S32 iCurrNodePos = iNodeStart;

	if ( iEntStart == iEntEnd && iNodeStart == iNodeEnd )
		return false;

	//sort the specified index ranges based on distance
	qsortInteractTargetInfoInIndexRange( ppEntInfoArray, iEntStart, iEntEnd );
	qsortInteractTargetInfoInIndexRange( ppNodeInfoArray, iNodeStart, iNodeEnd );

	//search through the lists of ents and nodes and find the closest ent or node within "Line-of-Sight"
	while ( iCurrEntPos < iEntEnd || iCurrNodePos < iNodeEnd )
	{
		F32 fEntDist = iCurrEntPos < iEntEnd ? interactTargetInfo_GetWeightedDistance(ppEntInfoArray[iCurrEntPos]) : FLT_MAX;
		F32 fNodeDist = iCurrNodePos < iNodeEnd ? interactTargetInfo_GetWeightedDistance(ppNodeInfoArray[iCurrNodePos]) : FLT_MAX;

		if ( fEntDist <= fNodeDist ) //if the ent dist is less or equal (ents have priority): the ent wins
		{
			InteractTargetInfo* pInfo = ppEntInfoArray[iCurrEntPos];
			Entity* pEnt = (Entity*)(pInfo->pData);
			InteractOption* pTargetEnt = (InteractOption*)(pInfo->pTarget);

			if(combat_CheckLoS(PARTITION_CLIENT,vSource,pInfo->vTarget,ent,pEnt,NULL,0,false,NULL))
			{
				(*piCloseEnt) = pTargetEnt->entRef;
				(*ppCloseNode) = NULL;
				return true;
			}
			else
			{
				iCurrEntPos++;
			}
		}
		else //otherwise, consider the node
		{
			InteractTargetInfo* pInfo = ppNodeInfoArray[iCurrNodePos];
			WorldInteractionNode* pNode = (WorldInteractionNode*)(pInfo->pData);
			InteractOption* pTargetNode = (InteractOption*)(pInfo->pTarget);
			WorldInteractionEntry* pEntry = pTargetNode ? wlInteractionNodeGetEntry(pNode) : NULL;

			if(combat_CheckLoS(PARTITION_CLIENT,vSource,pInfo->vTarget,ent,NULL,pEntry,0,false,NULL))
			{
				(*ppCloseNode) = pNode;
				(*piCloseEnt) = 0;
				return true;
			}
			else
			{
				iCurrNodePos++;
			}
		}
	}

	return false;
}

//this function finds the ideal interaction entity or node given the current state of the player entity.
//we only consider entities and nodes within interaction range and in line of sight
//entities or nodes that fulfill the above requirements are prioritized as follows:
// 1.) closest entity or node directly in the path of the ray (vOrigin,vFacing)
// 2.) closest entity or node in "front" of the player
// 3.) closest entity or node "behind" the player
static bool entity_GetBestInteract(Entity* ent, Vec3 vOrigin, Vec3 vFacing, F32 fMaxSelectionDistance)
{
	int iOptionArraySize = 0;
	int	i;
	int iEntCount = 0, iEntFrontCount = 0, iEntFocusCount = 0;
	int iNodeCount = 0, iNodeFrontCount = 0, iNodeFocusCount = 0;
	bool bFoundOverride = false, bFoundValidOverride = false;

	static InteractTargetInfo** ppEntInfoArray = NULL;
	static InteractTargetInfo** ppNodeInfoArray = NULL;
	S32 iValidEntInfoArray = 0;
	S32 iValidNodeInfoArray = 0;

	EntityRef playerTargetedEnt = 0;
	EntityRef iCloseEnt = 0;
	WorldInteractionNode* pCloseNode = NULL;
	Vec3 vSource;
	Vec3 vSourceDir;
	F32 fSourceDirLength;
	InteractOption* pPrevious = NULL;
	F32 fInteractRangeToSetAsOverride = 0.f;

	if (ent && ent->pPlayer) {
		iOptionArraySize = eaSize( &ent->pPlayer->InteractStatus.interactOptions.eaOptions );
	}
	if ( (iOptionArraySize == 0) || !ent || !ent->pChar || (ent->pChar->pattrBasic->fOnlyAffectSelf > 0) )
	{
		entity_ClearBestInteract( ent );
		if (!ent->pPlayer->InteractStatus.bLockIntoCursorOverrides)
		{
			entity_ClearOverrideInteract(ent);
		}		
		return false;
	}

	entGetCombatPosDir(ent, NULL, vSource, vSourceDir);
	fSourceDirLength = lengthVec3(vSourceDir);

	if (ent->pPlayer->InteractStatus.overrideSet && !ent->pPlayer->InteractStatus.overrideRef && !IS_HANDLE_ACTIVE(ent->pPlayer->InteractStatus.hOverrideNode))
	{
		bFoundOverride = true;
		// If we explicitly have no override, consider it to be "found" so we don't clear it
		
		if (g_CurrentScheme.bMouseLookInteract && 
			gclPlayerControl_IsMouseLooking() &&
			g_CombatConfig.pInteractionConfig)
		{
			// Since there is no override entity and node, we're in 
			fInteractRangeToSetAsOverride = g_CombatConfig.pInteractionConfig->fInteractRangeToSetAsOverride;
		}
	}

	// Make sure we want to check all the interactables.
	// We do want to do the checks only if:
	// 1. Override is not set or
	// 2. Override is set and there is actually an entity/node overridden or
	// 3. Override is set and fInteractRangeToSetAsOverride distance is defined.
	if (!bFoundOverride || fInteractRangeToSetAsOverride > 0.f)
	{
		//sort entity interaction array: front-facing, in-range ents go to the front
		//and ents pointed at by the ray (vOrigin,vFacing) go in front of those
		for ( i = 0; i < iOptionArraySize; i++ )
		{
			Vec3 vTarget, vDir;
			F32 fDist;

			InteractOption* pTargetEnt = ent->pPlayer->InteractStatus.interactOptions.eaOptions[i];

			Entity *pEnt = entFromEntityRefAnyPartition(pTargetEnt->entRef);

			if ( !pEnt || (pTargetEnt == pPrevious) ) continue;

			pPrevious = pTargetEnt; // Same ent can appear more than once so use this as shortcut to avoid eval of same ent more than once

			if (pTargetEnt->entRef == ent->pPlayer->InteractStatus.overrideRef)
			{
				bFoundOverride = true;
			}

			fDist = entGetDistance( ent, NULL, pEnt, NULL, NULL );

			if ( fDist <= gclEntity_GetInteractRange(ent, pEnt, 0) && 
				(fInteractRangeToSetAsOverride == 0.f || fDist <= fInteractRangeToSetAsOverride) )
			{
				F32 fDistDir;
				F32 tmp;
				InteractTargetInfo* pInfo = eaGetStruct( &ppEntInfoArray, parse_InteractTargetInfo, iValidEntInfoArray++ );

				entGetCombatPosDir(pEnt, NULL, vTarget, NULL);

				pInfo->pData = pEnt;
				pInfo->pTarget = pTargetEnt;
				pInfo->fDistance = fDist;

				copyVec3( vTarget, pInfo->vTarget );

				subVec3( vTarget, vSource, vDir );
				fDistDir = lengthVec3( vDir );

				if (fSourceDirLength * fDistDir != 0){
					tmp = dotVec3(vSourceDir, vDir) / (fSourceDirLength * fDistDir);
					pInfo->fAngle = acosf( MINMAX(tmp, -1, 1) );
				} else {
					pInfo->fAngle = 0;
				}

				if (pTargetEnt->entRef == ent->pPlayer->InteractStatus.overrideRef)
				{
					bFoundValidOverride = true;
					iCloseEnt = pTargetEnt->entRef;
				}

				if (ent->pChar->currentTargetRef == pTargetEnt->entRef) {
					playerTargetedEnt = pTargetEnt->entRef;
				}

				if ( isFacingDirectionEx( vFacing, vDir, fDistDir, ENT_FRONT_FACING_ANGLE ) )
				{
					Vec3 vCollision;

					if ( iEntFrontCount != iEntCount )
					{
						eaSwap( &ppEntInfoArray, iEntFrontCount, iEntCount );
					}

					if ( entLineDistance(	vOrigin,
						0,
						vFacing,
						fMaxSelectionDistance,
						pEnt,
						vCollision) < 0.1f )
					{
						if ( iEntFocusCount != iEntFrontCount )
						{
							eaSwap( &ppEntInfoArray, iEntFocusCount, iEntFrontCount );
						}

						iEntFocusCount++;
					}

					iEntFrontCount++;
				}

				iEntCount++;
			}
		}

		//sort node interaction array: front-facing, in-range nodes go to the front
		//and nodes pointed at by the ray (vOrigin,vFacing) go in front of those
		for ( i = 0; i < iOptionArraySize; i++ )
		{
			Vec3 vDir, vClose;
			F32 fDist, fInteractRange;

			InteractOption* pTargetNode = ent->pPlayer->InteractStatus.interactOptions.eaOptions[i];

			WorldInteractionNode* pNode = GET_REF(pTargetNode->hNode);

			if ( !pNode || (pTargetNode == pPrevious) ) continue;

			pPrevious = pTargetNode; // Same node can appear more than once, so use this as shortcut to not eval same node again

			if (GET_REF(pTargetNode->hNode) == GET_REF(ent->pPlayer->InteractStatus.hOverrideNode))
			{
				bFoundOverride = true;
			}

			fInteractRange = (pTargetNode->bCanPickup) ? entity_GetPickupRange(ent) : gclEntity_GetInteractRange(ent, NULL, pTargetNode ? pTargetNode->uNodeInteractDist : 0);

			if ( entity_IsNodeInRange( ent, vSource, pNode, fInteractRange, pTargetNode->vNodePosFallback, pTargetNode->fNodeRadiusFallback, vClose, &fDist, false ) &&
				(fInteractRangeToSetAsOverride == 0.f || fDist <= fInteractRangeToSetAsOverride )) //fDist <= fInteractRange
			{
				F32 fDistDir, tmp;
				InteractTargetInfo* pInfo = eaGetStruct( &ppNodeInfoArray, parse_InteractTargetInfo, iValidNodeInfoArray++ );
				pInfo->pData = pNode;
				pInfo->pTarget = pTargetNode;
				pInfo->fDistance = fDist;

				copyVec3( vClose, pInfo->vTarget );

				subVec3( vClose, vSource, vDir );
				fDistDir = lengthVec3( vDir );

				if (fSourceDirLength * fDistDir != 0){
					tmp = dotVec3(vSourceDir, vDir) / (fSourceDirLength * fDistDir);
					pInfo->fAngle = acosf( MINMAX(tmp, -1, 1) );
				} else {
					pInfo->fAngle = 0;
				}

				if (GET_REF(pTargetNode->hNode) == GET_REF(ent->pPlayer->InteractStatus.hOverrideNode))
				{
					bFoundValidOverride = true;
					pCloseNode = GET_REF(pTargetNode->hNode);
				}

				if ( isFacingDirectionEx( vFacing, vDir, fDistDir, ENT_FRONT_FACING_ANGLE ) )
				{
					Vec3 vEnd, vMin, vMax, vCollision;

					if ( iNodeFrontCount != iNodeCount )
					{
						eaSwap( &ppNodeInfoArray, iNodeFrontCount, iNodeCount );
					}

					scaleVec3( vFacing, fMaxSelectionDistance, vEnd );
					addVec3( vOrigin, vEnd, vEnd );

					wlInteractionNodeGetWorldMin(pNode,vMin);
					wlInteractionNodeGetWorldMax(pNode,vMax);

					if ( lineBoxCollision(vOrigin, vEnd, vMin, vMax, vCollision) )
					{
						if ( iNodeFocusCount != iNodeFrontCount )
						{
							eaSwap( &ppNodeInfoArray, iNodeFocusCount, iNodeFrontCount );
						}

						iNodeFocusCount++;
					}

					iNodeFrontCount++;
				}

				iNodeCount++;
			}
		}
	}

	if ( iEntCount == 0 && iNodeCount == 0 )
	{
		entity_ClearBestInteract( ent );
		return false;
	}

	eaSetSizeStruct( &ppEntInfoArray, parse_InteractTargetInfo, iValidEntInfoArray );
	eaSetSizeStruct( &ppNodeInfoArray, parse_InteractTargetInfo, iValidNodeInfoArray );

	if (bFoundValidOverride)
	{
		entity_SetBestInteract( ent, iCloseEnt, pCloseNode );
		return true;
	}
	else if (ent->pPlayer->InteractStatus.overrideSet && bFoundOverride && fInteractRangeToSetAsOverride == 0.f)
	{
		if (!ent->pPlayer->InteractStatus.bLockIntoCursorOverrides)
		{
			entity_ClearBestInteract( ent );
		}		
		return true;
	}
	else if (playerTargetedEnt)
	{
		entity_SetBestInteract( ent, playerTargetedEnt, NULL );
		return true;
	}
	//search for the best entity or node, if there is one
	else if (	entity_SearchForBestInteract(	ent, 0, iEntFocusCount, 0, iNodeFocusCount,
		vSource, ppEntInfoArray, ppNodeInfoArray, &iCloseEnt, &pCloseNode )
		||	entity_SearchForBestInteract(	ent, iEntFocusCount, iEntFrontCount, iNodeFocusCount, iNodeFrontCount,
		vSource, ppEntInfoArray, ppNodeInfoArray, &iCloseEnt, &pCloseNode )
		||	entity_SearchForBestInteract(	ent, iEntFrontCount, iEntCount, iNodeFrontCount, iNodeCount,
		vSource, ppEntInfoArray, ppNodeInfoArray, &iCloseEnt, &pCloseNode ) )
	{
		entity_SetBestInteract( ent, iCloseEnt, pCloseNode );
		if (!ent->pPlayer->InteractStatus.bLockIntoCursorOverrides)
		{
			if (fInteractRangeToSetAsOverride == 0.f)
			{
				entity_ClearOverrideInteract(ent);
			}
			else
			{
				if (iCloseEnt)
				{
					ent->pPlayer->InteractStatus.overrideRef = iCloseEnt;
				}
				else
				{
					SET_HANDLE_FROM_REFERENT(INTERACTION_DICTIONARY, pCloseNode, ent->pPlayer->InteractStatus.hOverrideNode);
				}
			}
		}
		return true;
	}
	else
	{
		entity_ClearBestInteract( ent );
		if (!ent->pPlayer->InteractStatus.bLockIntoCursorOverrides)
		{
			entity_ClearOverrideInteract(ent);
		}		
		return false;
	}
}

void gclInteract_FindBest(Entity *ePlayer)
{
	Vec3 vFacing;
	Mat4 xMat;

	//flag interact options which are linked to locally-hidden nodes.
	InteractOption** theOptions = ePlayer->pPlayer->InteractStatus.interactOptions.eaOptions;

	//check to see if the player is holding something
	if ( ePlayer->pChar && IS_HANDLE_ACTIVE(ePlayer->pChar->hHeldNode) )
	{
		entity_ClearBestInteract(ePlayer);
		entity_ClearOverrideInteract(ePlayer);
	}
	else
	{

		//interact from the camera facing direction
		if ( gfxGetActiveCameraController() )
		{
			copyMat4( gfxGetActiveCameraController()->last_camera_matrix, xMat );

			vFacing[0] = -xMat[2][0];
			vFacing[1] = -xMat[2][1];
			vFacing[2] = -xMat[2][2];
		}
		else
		{
			entGetBodyMat( ePlayer, xMat );
			copyVec3( xMat[2], vFacing );
		}

		entity_GetBestInteract(ePlayer, xMat[3], vFacing, ePlayer->fEntitySendDistance);
	}
}

bool gclInteract_FindRecentQueueInteract(Entity *pEnt, const char *pchQueueName)
{
	bool bFound = false;
	if(pEnt && pEnt->pPlayer)
	{
		EntInteractStatus *pStatus = &pEnt->pPlayer->InteractStatus;
		bFound = eaIndexedFindUsingString(&pStatus->ppRecentQueueInteractions, pchQueueName) >= 0;
	}

	return(bFound);
}


// ---------------------------------------------------------------------------
// UI Gen Support Functions
// ---------------------------------------------------------------------------


InteractOption* interaction_FindNodeInInteractOptions(WorldInteractionNode* pNode)
{
	Entity* pPlayerEnt = entActivePlayerPtr();
	if (pPlayerEnt && pNode)
	{
		InteractOption** pOptions = SAFE_MEMBER2(pPlayerEnt, pPlayer, InteractStatus.interactOptions.eaOptions);
		int i, n = eaSize(&pOptions);
		for (i = 0; i < n; i++)
		{
			if (pNode == GET_REF(pOptions[i]->hNode))
				return pOptions[i];
		}
	}
	return NULL;
}

InteractOption* interaction_FindNodeInPromptedInteractOptions(WorldInteractionNode* pNode)
{
	Entity* pPlayerEnt = entActivePlayerPtr();
	if (pPlayerEnt && pNode)
	{
		InteractOption** pOptions = SAFE_MEMBER2(pPlayerEnt, pPlayer, InteractStatus.eaPromptedInteractOptions);
		int i, n = eaSize(&pOptions);
		for (i = 0; i < n; i++)
		{
			if (pNode == GET_REF(pOptions[i]->hNode))
				return pOptions[i];
		}
	}
	return NULL;
}

InteractOption* interaction_FindEntityInInteractOptions(EntityRef erEnt)
{
	Entity* pPlayerEnt = entActivePlayerPtr();
	if (pPlayerEnt)
	{
		InteractOption** pOptions = SAFE_MEMBER2(pPlayerEnt, pPlayer, InteractStatus.interactOptions.eaOptions);
		int i, n = eaSize(&pOptions);
		for (i = 0; i < n; i++)
		{
			if (erEnt == pOptions[i]->entRef)
				return pOptions[i];
		}
	}
	return NULL;
}

AUTO_EXPR_FUNC(UIGen) ACMD_NAME(FindInteractOption);
bool interaction_ExprFindInteractOption(SA_PARAM_NN_VALID Entity *pEnt, SA_PARAM_OP_VALID InteractOption* pOption)
{
	S32 i;
	for(i=eaSize(&pEnt->pPlayer->InteractStatus.interactOptions.eaOptions)-1; i>=0; --i) {
		if (pOption == pEnt->pPlayer->InteractStatus.interactOptions.eaOptions[i]) {
			return true;
		}
	}
	return false;
}

AUTO_EXPR_FUNC(entityutil) ACMD_NAME(EntIsPossibleInteractTarget);
bool interaction_ExprEntIsPossibleInteractTarget(SA_PARAM_OP_VALID Entity *pEnt, SA_PARAM_OP_VALID Entity *pTarget)
{
	int i;

	if (pEnt && pEnt->pPlayer && pTarget)
	{
		EntInteractStatus *pStatus = &pEnt->pPlayer->InteractStatus;

		// First check if any of the prompted ones matches the provided entity
		for(i=eaSize(&pStatus->eaPromptedInteractOptions)-1; i>=0; --i) {
			Entity *pDefaultTarget = entFromEntityRefAnyPartition(pStatus->eaPromptedInteractOptions[i]->entRef);
			if (pDefaultTarget == pTarget) {
				return entity_VerifyInteractTarget(PARTITION_CLIENT, pEnt, pTarget, NULL, 0, 0, 0, false, NULL);
			}
		}

		// Then check if any of the unprompted ones matches the provided entity
		for (i = 0; i < eaSize(&pStatus->interactOptions.eaOptions); i++)
			if (entFromEntityRefAnyPartition(pStatus->interactOptions.eaOptions[i]->entRef) == pTarget)
				return entity_VerifyInteractTarget(PARTITION_CLIENT, pEnt, pTarget, NULL, 0, 0, 0, false, NULL);
	}
	return false;
}

bool interaction_IsTooltipNodeInRange(WorldInteractionNode* pNode)
{
	F32 fInteractDist = (F32)wlInteractionGetInteractDistForNode(pNode);
	Entity* ent = entActivePlayerPtr();
	if (ent)
	{
		RegionRules* pRules = getRegionRulesFromEnt(ent);
		Vec3 vClose;
		F32 fDist;

		if ( fInteractDist <= 0.0f )
		{
			if ( pRules && pRules->fDefaultInteractDist > 0.0f )
			{
				fInteractDist = pRules->fDefaultInteractDist;
			}
			else
			{
				fInteractDist =  DEFAULT_NODE_INTERACT_DIST;
			}
		}
		return entity_IsNodeInRange( ent, NULL, pNode, fInteractDist, 0, 0, vClose, &fDist, false );
	}
	return false;
}


void interaction_setTooltipFromTooltipNode(TooltipNode* pTooltipNode, MouseoverInteractableInfo* pInfo)
{
	pInfo->pcName = wlInteractionNodeGetDisplayName(SAFE_GET_REF(pTooltipNode, hNode));
	pInfo->eValid = kInteractValidity_NameOnly;
}

void interaction_setTooltipFromTargetableNode(TargetableNode* pTargetableNode, MouseoverInteractableInfo* pInfo)
{
	InteractOption* pOption;
	WorldInteractionNode* pNode = SAFE_GET_REF(pTargetableNode, hNode);
	Entity* ent = entActivePlayerPtr();
	EntInteractStatus* pStatus = ent && ent->pPlayer ? &(ent->pPlayer->InteractStatus) : NULL;
	if (pTargetableNode)
	{
		char* estrRequirementMsgKey = NULL;
		InteractValidity eFail = kInteractValidity_Nonexistant;
		pOption = interaction_FindNodeInInteractOptions(pNode);
		if (!pOption)
			pOption = interaction_FindNodeInPromptedInteractOptions(pNode);

		if (pOption && pOption->pcUsabilityString && pOption->pcUsabilityString[0]!=0)
		{
			// We have a usability string. 
			// It's translated on the server so we should be able to just use it.
			// The uigens may choose to use this to override the requirementname (which is only for skills currently)
			pInfo->pchUsabilityString = pOption->pcUsabilityString;
		}
		
		if (pTargetableNode->pchRequirementName && pTargetableNode->pchRequirementName[0])
		{
			// A requirements fragment. Likely just the name of a required skill. May be overriden in the display uigen by the usability string
			estrStackCreate(&estrRequirementMsgKey);
			estrPrintf(&estrRequirementMsgKey, "Requirement.%s", pTargetableNode->pchRequirementName);
			pInfo->pchRequirementString = pTargetableNode->pchRequirementName ? TranslateMessageKey(estrRequirementMsgKey) : "";
			if (!pInfo->pchRequirementString)
				pInfo->pchRequirementString = pTargetableNode->pchRequirementName;
				estrDestroy(&estrRequirementMsgKey);
		}
		pInfo->pcName = wlInteractionNodeGetDisplayName(pNode);
		pInfo->pcString = (pOption && pOption->pcInteractString) ? pOption->pcInteractString : NULL;
		if (pStatus && pStatus->bInteracting)
			pInfo->eValid = kInteractValidity_CurrentlyInteracting;
		else if (entity_VerifyInteractTarget(PARTITION_CLIENT, ent, NULL, pNode, 0, 0, 0, false, &eFail))
		{
			if (pOption)
			{
				if (!pOption->bAttemptable)
					//		if (pTargetableNode->pchRequirementName && pTargetableNode->pchRequirementName[0] &&
					//			!character_FindPowerByName(ent->pChar, pTargetableNode->pchRequirementName))
					//  We used to check the requirement skill explicitly here. Let's use the Attemptable field now so we get compound stuff
					//   dealt with. And so our unusable string can show up.  WOLF[5Oct2012]
				{
					pInfo->eValid = kInteractValidity_FailedRequirement;
				}
				else
				{
					pInfo->eValid = kInteractValidity_Valid;
				}
			}
			else
				pInfo->eValid = kInteractValidity_InvalidUnknown;
		}
		else
		{
			pInfo->eValid = eFail;
		}
	}
	return;
}

void interaction_setTooltipFromEntity(Entity* pEnt, bool bHasLoot, MouseoverInteractableInfo* pInfo)
{
	Entity* ent = entActivePlayerPtr();
	InteractTarget* pTarget = ent && ent->pPlayer ? &(ent->pPlayer->InteractStatus.interactTarget) : NULL;
	EntInteractStatus* pStatus = ent && ent->pPlayer ? &(ent->pPlayer->InteractStatus) : NULL;
	InteractOption* pOption = NULL;
	if (pEnt)
	{
		pOption = interaction_FindEntityInInteractOptions(pEnt->myRef);
	}
	if (!bHasLoot || reward_MyDrop(ent, pEnt))
	{
		InteractValidity eFail = kInteractValidity_Nonexistant;
		pInfo->pcName = pEnt ? entGetLocalName(pEnt) : "Target";
		pInfo->pcString = pOption ? (bHasLoot ? "Loot" : "Interact") : NULL;

		pInfo->pcString = NULL;
		if (pOption)
		{
			// Set up the "Press F to <X>" string. (which is imaginatively called pcString)
			if (bHasLoot)
			{
				pInfo->pcString = "Loot";
			}
			else
			{
				if (pOption->pcInteractString)
				{
					pInfo->pcString = pOption->pcInteractString;
				}
				else
				{
					pInfo->pcString = "Interact";
				}
			}

			if (pOption->pcUsabilityString && pOption->pcUsabilityString[0]!=0)
			{
				// We have a usability string. 
				// It's translated on the server so we should be able to just use it.
				pInfo->pchUsabilityString = pOption->pcUsabilityString;
			}
		}
		
		if ((pTarget && pEnt && (pEnt->myRef == pTarget->entRef)) || (pStatus && pStatus->bInteracting))
		{
			pInfo->eValid = kInteractValidity_CurrentlyInteracting;
		}
		else if (entity_VerifyInteractTarget(PARTITION_CLIENT, ent, pEnt, NULL, 0, 0, 0, false, &eFail))
		{
			if (pOption)
			{
 				if (!pOption->bAttemptable && !bHasLoot)
				{
					pInfo->eValid = kInteractValidity_FailedRequirement;
				}
				else
				{
					pInfo->eValid = kInteractValidity_Valid;
				}
				pInfo->eaLootBags = pOption->eaLootBags;
			}
			else
				pInfo->eValid = kInteractValidity_InvalidUnknown;
		}
		else
		{
			pInfo->eValid = eFail;
		}
	}
}

AUTO_RUN;
void RegisterUIInteractTypeForGens(void)
{
	ui_GenInitStaticDefineVars(UIInteractTypeEnum, "UIInteractType_");
	ui_GenInitStaticDefineVars(InteractValidityEnum, "InteractValidity_");
}

S32 interaction_findMouseoverInteractableEx(Entity **pInteractableEntity, MouseoverInteractableInfo* pInfoOut)
{
	//set mouseover nodes
	Entity* pEnt = entActivePlayerPtr();
	if (pEnt)
	{
		Player* pPlayer = pEnt->pPlayer;
		WorldInteractionNode* pMouseNode;
		Entity* pTarget;
		S32 i = 0;

		interactSetOverrideAtCursor(pEnt, false, &pTarget, &pMouseNode);

		if (pMouseNode)
		{
			for (i = 0; i < eaSize(&(pPlayer->InteractStatus.ppTargetableNodes)); i++)
			{
				if (GET_REF(pPlayer->InteractStatus.ppTargetableNodes[i]->hNode) == pMouseNode)
				{
					if (pInfoOut)
						interaction_setTooltipFromTargetableNode(pPlayer->InteractStatus.ppTargetableNodes[i], pInfoOut);
					if (gConf.pchCategoryNameForDoors &&
						gConf.pchCategoryNameForDoors[0] &&
						eaFindString(&pPlayer->InteractStatus.ppTargetableNodes[i]->eaCategories, gConf.pchCategoryNameForDoors) >= 0)
					{
						return UIInteractType_Door;
					}

					return UIInteractType_Clicky;
				}
			}

			for (i = 0; i < eaSize(&(pPlayer->InteractStatus.ppTooltipNodes)); i++)
			{
				if (GET_REF(pPlayer->InteractStatus.ppTooltipNodes[i]->hNode) == pMouseNode)
				{
					if (pInfoOut)
						interaction_setTooltipFromTooltipNode(pPlayer->InteractStatus.ppTooltipNodes[i], pInfoOut);
					return UIInteractType_NamedPoint;
				}
			}
		}

		if (pTarget && pTarget->pCritter && pTarget->pCritter->bIsInteractable)
		{
			bool bIsContact = (gclEntGetContactInfoForPlayer(pEnt, pTarget) != 0);
			bool bIsInteractableCritter = !!gclEntGetInteractableCritterInfo(pEnt, pTarget);
			if (bIsContact || bIsInteractableCritter)
			{
				if (pInfoOut)
					interaction_setTooltipFromEntity(pTarget, false, pInfoOut);

				// Set the interactable entity
				if (pInteractableEntity)
				{
					*pInteractableEntity = pTarget;
				}

				return bIsContact ? UIInteractType_Contact : UIInteractType_Clicky;
			}
			else
			{
				if (!entIsAlive(pTarget))
				{
					if (pInfoOut)
						interaction_setTooltipFromEntity(pTarget, true, pInfoOut);

					// Set the interactable entity
					if (pInteractableEntity)
					{
						*pInteractableEntity = pTarget;
					}

					return reward_MyDrop(pEnt, pTarget) ? UIInteractType_Loot : UIInteractType_None;
				}
				else
				{
					return UIInteractType_None;
				}
			}
		}
	}
	return UIInteractType_None;
}

AUTO_EXPR_FUNC(UIGen);
S32 interaction_findMouseoverInteractable(void)
{
	return interaction_findMouseoverInteractableEx(NULL,NULL);
}

AUTO_EXPR_FUNC(UIGen);
SA_RET_OP_VALID Entity * interaction_GetMouseoverInteractableEntity(void)
{
	Entity *pEnt = NULL;

	interaction_findMouseoverInteractableEx(&pEnt, NULL);

	return pEnt;
}


AUTO_EXPR_FUNC(UIGen) ACMD_NAME(GenGetMouseoverInteractionInfo);
void interaction_GenGetMouseoverInteractionInfo(UIGen* pGen)
{
	static MouseoverInteractableInfo* pInfo = NULL;

	if (!pInfo)
		pInfo = StructCreate(parse_MouseoverInteractableInfo);

	pInfo->pchRequirementString = "";
	pInfo->pchUsabilityString = "";
	pInfo->pcName = NULL;
	pInfo->pcString = NULL;
	pInfo->eValid = kInteractValidity_Nonexistant;

	pInfo->eType = interaction_findMouseoverInteractableEx(NULL, pInfo);

	// Set the cursor properties
	pInfo->pchCursorName = allocAddString(ui_GetCursorName(targetCursor_GetCurrent()));
	pInfo->pchCursorTexture = allocAddString(ui_GetCursorTexture(targetCursor_GetCurrent()));
	pInfo->siCursorHotSpotX = ui_GetCursorHotSpotX(targetCursor_GetCurrent());
	pInfo->siCursorHotSpotY = ui_GetCursorHotSpotY(targetCursor_GetCurrent());

	ui_GenSetPointer(pGen, pInfo, parse_MouseoverInteractableInfo);
}


AUTO_EXPR_FUNC(UIGen);
SA_RET_OP_VALID Entity * interaction_GetMouseoverEntity(void)
{
	Entity *pEnt = NULL;
	return target_SelectUnderMouse(entActivePlayerPtr(),0,0,NULL,false,false,false);
}

AUTO_EXPR_FUNC(UIGen) ACMD_NAME(IsNearContact);
bool interaction_ExprIsNearContact(SA_PARAM_OP_VALID Entity *pEnt, const char* pchType)
{
	S32 iType = StaticDefineIntGetInt( ContactFlagsEnum, pchType );

	if (!pEnt) return false;
	return interaction_IsPlayerNearContact( pEnt, iType );
}


AUTO_EXPR_FUNC(entityutil) ACMD_NAME(NodeIsPossibleInteractTarget);
bool interaction_ExprNodeIsPossibleInteractTarget(SA_PARAM_OP_VALID Entity *pEnt, SA_PARAM_OP_VALID WorldInteractionNode *pTarget)
{
	if (pEnt && pEnt->pPlayer && pTarget)
	{
		int i;
		EntInteractStatus *pStatus = &pEnt->pPlayer->InteractStatus;
		for (i = eaSize(&pStatus->interactOptions.eaOptions)-1; i >= 0; i--)
		{
			InteractOption* pOption = pStatus->interactOptions.eaOptions[i];
			if (GET_REF(pOption->hNode) == pTarget)
			{
				return entity_VerifyInteractTarget(PARTITION_CLIENT, pEnt, NULL, pTarget, pOption->uNodeInteractDist, pOption->vNodePosFallback, pOption->fNodeRadiusFallback, pOption->bCanPickup, NULL);
			}
		}
	}
	return false;
}

AUTO_EXPR_FUNC(UIGen) ACMD_NAME(NodeGetDetailTexture);
const char *interaction_ExprNodeGetDetailTexture(SA_PARAM_OP_VALID Entity *pPlayerEnt, SA_PARAM_OP_VALID WorldInteractionNode *pTargetNode)
{
	int i, iSize;

	if (!pPlayerEnt || !pTargetNode)
		return NULL;

	iSize = eaSize(&pPlayerEnt->pPlayer->InteractStatus.ppTargetableNodes);
	for (i = 0; i < iSize; i++)
	{
		TargetableNode *pTargetableNode = pPlayerEnt->pPlayer->InteractStatus.ppTargetableNodes[i];
		WorldInteractionNode* pNode = SAFE_GET_REF(pTargetableNode, hNode);

		if (!pNode)
			continue;

		if (pNode == pTargetNode)
			return pTargetableNode->pcDetailTexture;
	}

	return NULL;
}

AUTO_EXPR_FUNC(UIGen) ACMD_NAME(GenGetInteractionPromptList);
void interaction_ExprGenGetInteractionPromptList(SA_PARAM_NN_VALID UIGen *pGen, SA_PARAM_NN_VALID Entity *pEnt)
{
	InteractString ***peaData = ui_GenGetManagedListSafe(pGen, InteractString);
	S32 i, size = 0;
	EntInteractStatus *pStatus;
	if(pEnt && pEnt->pPlayer)
	{
		pStatus = &pEnt->pPlayer->InteractStatus;

		size = eaSize(&pStatus->eaPromptedInteractOptions);
		for (i = 0; i < size; i++)
		{
			InteractString *pInteractString = eaGetStruct(peaData, parse_InteractString, i);
			estrCopy2(&pInteractString->string, pStatus->eaPromptedInteractOptions[i]->pcInteractString);
		}

		while (eaSize(peaData) > size)
		{
			StructDestroy(parse_InteractString, eaPop(peaData));
		}

		ui_GenSetManagedListSafe(pGen, peaData, InteractString, true);
	}
	else
	{
		ui_GenSetManagedListSafe(pGen, NULL, InteractString, true);
	}
}

static MapSummary* interaction_InteractOptionFindMapWithTeamOpenToInvites( Entity *pEnt, InteractOption* pOption )
{
	EntInteractStatus *pStatus = pEnt && pEnt->pPlayer ? &pEnt->pPlayer->InteractStatus : NULL;
	WorldInteractionNode* pNode = pOption ? GET_REF(pOption->hNode) : NULL;
	if ( pStatus && pNode )
	{
		if ( !team_IsMember(pEnt) )
		{
			S32 c, n = eaIndexedFindUsingString(&pStatus->ppDoorStatusNodes,wlInteractionNodeGetKey(pNode));
			if ( n >= 0 )
			{
				for ( c = eaSize(&pStatus->ppDoorStatusNodes[n]->eaDestinations)-1; c >= 0; c-- )
				{
					MapSummary* pDestination = pStatus->ppDoorStatusNodes[n]->eaDestinations[c];
					if ( pDestination->iPropIndex == pOption->iIndex && pDestination->iNumEnabledOpenInstancing > 0 )
					{
						return pDestination;
					}
				}
			}
		}
	}
	return NULL;
}

AUTO_EXPR_FUNC(UIGen) ACMD_NAME(InteractOptionAllowJoinTeamAtDoor);
bool interaction_ExprInteractOptionAllowJoinTeamAtDoor(SA_PARAM_NN_VALID Entity *pEnt, SA_PARAM_OP_VALID InteractOption* pOption)
{
	return interaction_InteractOptionFindMapWithTeamOpenToInvites( pEnt, pOption ) != NULL;
}

AUTO_EXPR_FUNC(UIGen) ACMD_NAME(InteractOptionJoinTeamAtDoor);
bool interaction_ExprInteractOptionJoinTeamAtDoor(SA_PARAM_NN_VALID Entity *pEnt, SA_PARAM_OP_VALID InteractOption* pOption)
{
	MapSummary* pDestination = interaction_InteractOptionFindMapWithTeamOpenToInvites( pEnt, pOption );

	if ( pDestination )
	{
		ServerCmd_Team_RequestByMap( pDestination->pchMapName, pDestination->pchMapVars );
		return true;
	}
	return false;
}

AUTO_EXPR_FUNC(UIGen) ACMD_NAME(IsInteractOptionPrompted);
bool interaction_ExprIsInteractOptionPrompted(SA_PARAM_OP_VALID Entity *pEnt, InteractOption *pOption)
{
	if (pEnt && pEnt->pPlayer) {
		if (eaFind(&pEnt->pPlayer->InteractStatus.eaPromptedInteractOptions, pOption) >= 0){
			return true;
		}
	}
	return false;
}

AUTO_EXPR_FUNC(UIGen) ACMD_NAME(IsInteractOptionFirstPrompted);
bool interaction_ExprIsInteractOptionFirstPrompted(SA_PARAM_OP_VALID Entity *pEnt, InteractOption *pOption)
{
	if (pEnt && pEnt->pPlayer) {
		if (eaFind(&pEnt->pPlayer->InteractStatus.eaPromptedInteractOptions, pOption) == 0){
			return true;
		}
	}
	return false;
}

AUTO_EXPR_FUNC(UIGen) ACMD_NAME(GetInteractOption);
SA_RET_OP_VALID InteractOption *interaction_ExprGetInteractOption(SA_PARAM_OP_VALID Entity *pEnt, S32 iIndex)
{
	InteractOption *pOption = NULL;
	if (pEnt && pEnt->pPlayer) {
		pOption = eaGet(&pEnt->pPlayer->InteractStatus.interactOptions.eaOptions, iIndex);
	}
	return pOption;
}

AUTO_EXPR_FUNC(UIGen) ACMD_NAME(GenGetInteractOption);
SA_RET_OP_VALID InteractOption *interaction_ExprGenGetInteractOption(SA_PARAM_NN_VALID UIGen *pGen, SA_PARAM_OP_VALID Entity *pEnt, S32 iIndex)
{
	InteractOption *pOption = NULL;
	if (pEnt && pEnt->pPlayer) {
		pOption = eaGet(&pEnt->pPlayer->InteractStatus.interactOptions.eaOptions, iIndex);
	}
	ui_GenSetPointer(pGen, pOption, parse_InteractOption);
	return pOption;
}

AUTO_STRUCT;
typedef struct InteractOptionPersistData
{
	U32 uiTimeMs;
	U32 uiLastCheckFrame;
	//keep track of interact option data
	REF_TO(WorldInteractionNode) hNode;
	U32	entRef;
	const char* pcVolumeName;							AST( POOL_STRING )
	int iIndex;
} InteractOptionPersistData;

static InteractOptionPersistData **s_eaPersistTimes = NULL;

static const char** interaction_GetInteractCategoryListFromString(const char* pchCategories)
{
	static const char** s_ppchCategories = NULL;
	char* pchContext;
	char* pchStart;
	char* pchCategoriesCopy;

	eaClear(&s_ppchCategories);
	if (pchCategories && pchCategories[0])
	{
		strdup_alloca(pchCategoriesCopy, pchCategories);
		pchStart = strtok_r(pchCategoriesCopy, " ,\t\r\n", &pchContext);
		do
		{
			if (pchStart && pchStart[0] && allocFindString(pchStart))
				eaPush(&s_ppchCategories, allocAddString(pchStart));
		} while (pchStart = strtok_r(NULL, " ,\t\r\n", &pchContext));
	}
	return s_ppchCategories;
}

AUTO_EXPR_FUNC(UIGen) ACMD_NAME(GenGetInteractionListByImportance);
void interaction_ExprGetInteractionListByImportance(SA_PARAM_NN_VALID UIGen *pGen, SA_PARAM_OP_VALID Entity *pEnt, const char *pcType, const char *pchExcludeCategories, int iPriority, F32 fTime, bool bGetHighImportanceItems)
{
	static InteractOption **s_eaOptions = NULL;
	InteractOptionPersistData *pPersistOption = NULL;
	InteractOption ***peaOptionsToSearch;
	InteractOption *pPreviousOption = NULL;
	const char **ppchExcludeCategories = NULL;
	bool bPreviousTest = false;
	F32 fDeltaTime;
	U32 uiThisFrame;
	int i, j;

	eaClear(&s_eaOptions);

	frameLockedTimerGetTotalFrames(gGCLState.frameLockedTimer, &uiThisFrame);

	if ( eaSize(&s_eaPersistTimes) > 0 )
	{
		for ( i = eaSize(&s_eaPersistTimes)-1; i >= 0; i-- ) {
			if ( uiThisFrame > s_eaPersistTimes[i]->uiLastCheckFrame + 1 ) {
				StructDestroy( parse_InteractOptionPersistData, eaRemoveFast(&s_eaPersistTimes,i) );
			}
		}
	}

	if (pGen && pEnt && pEnt->pPlayer) {
		// Figure out what list to search
		peaOptionsToSearch = &pEnt->pPlayer->InteractStatus.interactOptions.eaOptions;
		if (pcType && (stricmp(pcType, "Prompted") == 0)) {
			peaOptionsToSearch = &pEnt->pPlayer->InteractStatus.eaPromptedInteractOptions;
			pcType = NULL; // Means not to match on type
		}
		if (pcType && (stricmp(pcType, "All") == 0)) {
			pcType = NULL; // Means not to match on type
		}

		ppchExcludeCategories = interaction_GetInteractCategoryListFromString(pchExcludeCategories);

		for(i=0; i<eaSize(peaOptionsToSearch); ++i) {
			InteractOption *pOption = (*peaOptionsToSearch)[i];
			if (pcType && pcType[0]) {
				if (stricmp(pcType, "Volume") == 0) {
					if (!pOption->pcVolumeName) {
						// Match volumes, but not a volume
						continue;
					}
				} else if (stricmp(pcType, "Node") == 0) {
					if (!GET_REF(pOption->hNode)) {
						// Match nodes, but not a node
						continue;
					}
				} else if (stricmp(pcType, "Entity") == 0) {
					if (!pOption->entRef) {
						// Match entities, but not an entity
						continue;
					}
				} else {
					Errorf("Unexpected search type for GenGetInteractionList : %s", pcType);
					break;
				}
			}
			if (eaSize(&ppchExcludeCategories)) {
				if (pOption->pcCategory && (eaFind(&ppchExcludeCategories, pOption->pcCategory) >= 0)) {
					continue;
				}
			}
			// Test for entity distance and line of sight
			if (pOption->entRef) {
				if (pPreviousOption && (pOption->entRef == pPreviousOption->entRef)) {
					// Previous was same entity, so use previous test result
					if (!bPreviousTest) {
						continue;
					}
				} else if (!entity_VerifyInteractTarget(PARTITION_CLIENT, pEnt, entFromEntityRefAnyPartition(pOption->entRef), NULL, 0, 0, 0, false, NULL)) {
					// Don't have line of sight and distance on entity
					pPreviousOption = pOption;
					bPreviousTest = false;
					continue;
				}
			}

			// Test for time value
			if (fTime) {
				for ( j = eaSize(&s_eaPersistTimes)-1; j >= 0; j-- ) {
					if ( s_eaPersistTimes[j]->iIndex != pOption->iIndex ) {
						continue;
					}
					if ( pOption->pcVolumeName ) {
						if ( stricmp(s_eaPersistTimes[j]->pcVolumeName,pOption->pcVolumeName)==0 ) {
							break;
						}
					} else if ( GET_REF(pOption->hNode) ) {
						if ( REF_COMPARE_HANDLES(s_eaPersistTimes[j]->hNode,pOption->hNode) ) {
							break;
						}
					} else if ( pOption->entRef ) {
						if ( s_eaPersistTimes[j]->entRef == pOption->entRef ) {
							break;
						}
					}
				}

				if ( j >= 0 ) {
					pPersistOption = s_eaPersistTimes[j];

				} else {
					pPersistOption = StructCreate( parse_InteractOptionPersistData );
					pPersistOption->pcVolumeName = allocAddString( pOption->pcVolumeName );
					COPY_HANDLE(pPersistOption->hNode,pOption->hNode);
					pPersistOption->entRef = pOption->entRef;
					pPersistOption->iIndex = pOption->iIndex;
					pPersistOption->uiTimeMs = gGCLState.totalElapsedTimeMs;
					eaPush( &s_eaPersistTimes, pPersistOption );
				}

				pPersistOption->uiLastCheckFrame = uiThisFrame;
				fDeltaTime = (gGCLState.totalElapsedTimeMs - pPersistOption->uiTimeMs) / 1000.0;

				if ( bGetHighImportanceItems ) {
					if ( (fTime > 0.0f && fTime < fDeltaTime) || (iPriority >= 0 && iPriority > pOption->iPriority) ) {
						continue;
					}
				} else {
					if ( (fTime <= 0.0f || fTime >= fDeltaTime) && (iPriority < 0 || iPriority <= pOption->iPriority) ) {
						continue;
					}
				}
			}

			// Test for node distance and line of sight
			if (GET_REF(pOption->hNode)) {
				if (pPreviousOption && (GET_REF(pOption->hNode) == GET_REF(pPreviousOption->hNode))) {
					// Previous was same node, so use previous test result
					if (!bPreviousTest) {
						continue;
					}
				} else if (!entity_VerifyInteractTarget(PARTITION_CLIENT, pEnt, NULL, GET_REF(pOption->hNode), pOption->uNodeInteractDist, pOption->vNodePosFallback, pOption->fNodeRadiusFallback, pOption->bCanPickup, NULL)) {
					// Don't have line of sight and distance on node
					pPreviousOption = pOption;
					bPreviousTest = false;
					continue;
				}
			}

			pPreviousOption = pOption;
			bPreviousTest = true;
			eaPush(&s_eaOptions, pOption);
		}

		eaQSort(s_eaOptions, interaction_CompareOptions);

	}

	ui_GenSetListSafe(pGen, &s_eaOptions, InteractOption);
}

static S32 interaction_PlayerGetInteractionOptionsCountByImportance(SA_PARAM_OP_VALID Entity *pEnt, const char *pcType, const char *pchExcludeCategories, int iPriority, F32 fTime, bool bGetHighImportanceItems, bool bCalculateCount)
{
	InteractOption ***peaOptionsToSearch;
	InteractOption *pPreviousOption = NULL;
	InteractOptionPersistData *pPersistOption = NULL;
	const char **ppchExcludeCategories = NULL;
	bool bPreviousTest = false;
	F32 fDeltaTime;
	int i,j;
	S32 iOptionCount = 0;

	if (pEnt && pEnt->pPlayer) {
		// Figure out what list to search
		peaOptionsToSearch = &pEnt->pPlayer->InteractStatus.interactOptions.eaOptions;
		if (pcType && (stricmp(pcType, "Prompted") == 0)) {
			peaOptionsToSearch = &pEnt->pPlayer->InteractStatus.eaPromptedInteractOptions;
			pcType = NULL; // Means not to match on type
		}
		if (pcType && (stricmp(pcType, "All") == 0)) {
			pcType = NULL; // Means not to match on type
		}

		ppchExcludeCategories = interaction_GetInteractCategoryListFromString(pchExcludeCategories);

		for(i=0; i<eaSize(peaOptionsToSearch); ++i) {
			InteractOption *pOption = (*peaOptionsToSearch)[i];
			if (pcType && pcType[0]) {
				if (stricmp(pcType, "Volume") == 0) {
					if (!pOption->pcVolumeName) {
						// Match volumes, but not a volume
						continue;
					}
				} else if (stricmp(pcType, "Node") == 0) {
					if (!GET_REF(pOption->hNode)) {
						// Match nodes, but not a node
						continue;
					}
				} else if (stricmp(pcType, "Entity") == 0) {
					if (!pOption->entRef) {
						// Match entities, but not an entity
						continue;
					}
				} else {
					Errorf("Unexpected search type for GenGetInteractionList : %s", pcType);
					break;
				}
			}
			if (eaSize(&ppchExcludeCategories)) {
				if (pOption->pcCategory && (eaFind(&ppchExcludeCategories, pOption->pcCategory) >= 0)) {
					continue;
				}
			}
			// Test for entity distance and line of sight
			if (pOption->entRef) {
				if (pPreviousOption && (pOption->entRef == pPreviousOption->entRef)) {
					// Previous was same entity, so use previous test result
					if (!bPreviousTest) {
						continue;
					}
				} else if (!entity_VerifyInteractTarget(PARTITION_CLIENT, pEnt, entFromEntityRefAnyPartition(pOption->entRef), NULL, 0, 0, 0, false, NULL)) {
					// Don't have line of sight and distance on entity
					pPreviousOption = pOption;
					bPreviousTest = false;
					continue;
				}
			}

			// Test for time value
			if (fTime) {
				for ( j = eaSize(&s_eaPersistTimes)-1; j >= 0; j-- ) {
					if ( s_eaPersistTimes[j]->iIndex != pOption->iIndex ) {
						continue;
					}
					if ( pOption->pcVolumeName ) {
						if ( stricmp(s_eaPersistTimes[j]->pcVolumeName,pOption->pcVolumeName)==0 ) {
							break;
						}
					} else if ( GET_REF(pOption->hNode) ) {
						if ( REF_COMPARE_HANDLES(s_eaPersistTimes[j]->hNode,pOption->hNode) ) {
							break;
						}
					} else if ( pOption->entRef ) {
						if ( s_eaPersistTimes[j]->entRef == pOption->entRef ) {
							break;
						}
					}
				}

				if ( j >= 0 ) {
					fDeltaTime = (gGCLState.totalElapsedTimeMs - s_eaPersistTimes[j]->uiTimeMs) / 1000.0;
				} else {
					fDeltaTime = 0.0f;
				}

				if ( bGetHighImportanceItems ) {
					if ( (fTime > 0.0f && fTime < fDeltaTime) || (iPriority >= 0 && iPriority > pOption->iPriority) ) {
						continue;
					}
				} else {
					if ( (fTime <= 0.0f || fTime >= fDeltaTime) && (iPriority < 0 || iPriority <= pOption->iPriority) ) {
						continue;
					}
				}
			}

			// Test for node distance and line of sight
			if (GET_REF(pOption->hNode)) {
				if (pPreviousOption && (GET_REF(pOption->hNode) == GET_REF(pPreviousOption->hNode))) {
					// Previous was same node, so use previous test result
					if (!bPreviousTest) {
						continue;
					}
				} else if (!entity_VerifyInteractTarget(PARTITION_CLIENT, pEnt, NULL, GET_REF(pOption->hNode), pOption->uNodeInteractDist, pOption->vNodePosFallback, pOption->fNodeRadiusFallback, pOption->bCanPickup, NULL)) {
					// Don't have line of sight and distance on node
					pPreviousOption = pOption;
					bPreviousTest = false;
					continue;
				}
			}

			if (bCalculateCount)
			{
				++iOptionCount;
			}
			else
			{
				return 1;
			}			
		}
	}
	return iOptionCount;
}

AUTO_EXPR_FUNC(UIGen) ACMD_NAME(PlayerGetInteractionOptionsCountByImportance);
S32 interaction_ExprPlayerGetInteractionOptionsCountByImportance(SA_PARAM_OP_VALID Entity *pEnt, const char *pcType, const char *pchExcludeCategories, int iPriority, F32 fTime, bool bGetHighImportanceItems)
{
	return interaction_PlayerGetInteractionOptionsCountByImportance(pEnt, pcType, pchExcludeCategories, iPriority, fTime, bGetHighImportanceItems, true);
}

AUTO_EXPR_FUNC(UIGen) ACMD_NAME(PlayerHasInteractionOptionsByImportance);
bool interaction_ExprPlayerHasInteractionOptionsByImportance(SA_PARAM_OP_VALID Entity *pEnt, const char *pcType, const char *pchExcludeCategories, int iPriority, F32 fTime, bool bGetHighImportanceItems)
{
	return interaction_PlayerGetInteractionOptionsCountByImportance(pEnt, pcType, pchExcludeCategories, iPriority, fTime, bGetHighImportanceItems, false);
}

// Generates the list of Interaction options into the UIGen, returns true if there are any.
//  If pGen is NULL it returns after finding the first option, rather than making the whole list.
static bool GetInteractionList(SA_PARAM_OP_VALID UIGen *pGen, SA_PARAM_NN_VALID Entity *pEnt, const char *pcType, const char *pcCategory, int iPriority)
{
	static InteractOption **s_eaOptions = NULL;
	InteractOption ***peaOptionsToSearch;
	InteractOption *pPreviousOption = NULL;
	bool bPreviousTest = false;
	int i;

	if (pEnt && pEnt->pPlayer && pEnt->pChar) {
		// Figure out what list to search
		peaOptionsToSearch = &pEnt->pPlayer->InteractStatus.interactOptions.eaOptions;
		if (pcType && (stricmp(pcType, "Prompted") == 0)) {
			peaOptionsToSearch = &pEnt->pPlayer->InteractStatus.eaPromptedInteractOptions;
			pcType = NULL; // Means not to match on type
		}
		if (pcType && (stricmp(pcType, "All") == 0)) {
			pcType = NULL; // Means not to match on type
		}

		eaClear(&s_eaOptions);
		for(i=0; i<eaSize(peaOptionsToSearch); ++i) {
			InteractOption *pOption = (*peaOptionsToSearch)[i];

			//If the option is not to throw something and you're holding something... skip it
			if(IS_HANDLE_ACTIVE(pEnt->pChar->hHeldNode) && !pOption->bCanThrow) {
				continue;
			}

			if (pcType && pcType[0]) {
				if (stricmp(pcType, "Volume") == 0) {
					if (!pOption->pcVolumeName) {
						// Match volumes, but not a volume
						continue;
					}
				} else if (stricmp(pcType, "Node") == 0) {
					if (!GET_REF(pOption->hNode)) {
						// Match nodes, but not a node
						continue;
					}
				} else if (stricmp(pcType, "Entity") == 0) {
					if (!pOption->entRef) {
						// Match entities, but not an entity
						continue;
					}
				} else {
					Errorf("Unexpected search type for GetInteractionList : %s", pcType);
					break;
				}
			}
			if (pcCategory && pcCategory[0]) {
				if (pOption->pcCategory && (stricmp(pOption->pcCategory, pcCategory) != 0)) {
					continue;
				} else if (!pOption->pcCategory && (stricmp(pcCategory, "None") != 0)) {
					continue;
				}
			}
			if (iPriority >= 0) {
				if (iPriority != pOption->iPriority) {
					continue; // Priority mismatch
				}
			}

			// Test for entity distance and line of sight
			if (pOption->entRef) {
				if (pPreviousOption && (pOption->entRef == pPreviousOption->entRef)) {
					// Previous was same entity, so use previous test result
					if (!bPreviousTest) {
						continue;
					}
				} else if (!entity_VerifyInteractTarget(PARTITION_CLIENT, pEnt, entFromEntityRefAnyPartition(pOption->entRef), NULL, 0, 0, 0, false, NULL)) {
					// Don't have line of sight and distance on entity
					pPreviousOption = pOption;
					bPreviousTest = false;
					continue;
				}
			}

			// Test for node distance and line of sight
			if (GET_REF(pOption->hNode)) {
				if (pPreviousOption && (GET_REF(pOption->hNode) == GET_REF(pPreviousOption->hNode))) {
					// Previous was same node, so use previous test result
					if (!bPreviousTest) {
						continue;
					}
				} else if (!entity_VerifyInteractTarget(PARTITION_CLIENT, pEnt, NULL, GET_REF(pOption->hNode), pOption->uNodeInteractDist, pOption->vNodePosFallback, pOption->fNodeRadiusFallback, pOption->bCanPickup, NULL)) {
					// Don't have line of sight and distance on node
					pPreviousOption = pOption;
					bPreviousTest = false;
					continue;
				}
			}

			if(pGen)
			{
				pPreviousOption = pOption;
				bPreviousTest = true;
				eaPush(&s_eaOptions, pOption);
			}
			else
			{
				return true;
			}
		}

		if(pGen)
		{
			eaQSort(s_eaOptions, interaction_CompareOptions);

			// Now remove any duplicate options
			if (eaSize(&s_eaOptions) > 1)
			{
				pPreviousOption = s_eaOptions[0];
				i = 1;
				while (i < eaSize(&s_eaOptions))
				{
					if (stricmp(s_eaOptions[i]->pcInteractString, pPreviousOption->pcInteractString) == 0)
					{
						eaRemove(&s_eaOptions, i);
					}
					else
					{
						pPreviousOption = s_eaOptions[i];
						i++;
					}
				}
			}

			ui_GenSetListSafe(pGen, &s_eaOptions, InteractOption);
		}
	}

	return (eaSize(&s_eaOptions) > 0);
}


AUTO_EXPR_FUNC(UIGen) ACMD_NAME(GenGetInteractionList);
void interaction_ExprGetInteractionList(SA_PARAM_NN_VALID UIGen *pGen, SA_PARAM_NN_VALID Entity *pEnt, const char *pcType, const char *pcCategory, int iPriority)
{
	GetInteractionList(pGen,pEnt,pcType,pcCategory,iPriority);
}

AUTO_EXPR_FUNC(UIGen) ACMD_NAME(PlayerHasInteractionOptions);
bool interaction_ExprPlayerHasInteractionOptions(SA_PARAM_NN_VALID Entity *pEnt, const char *pcType, const char *pcCategory, int iPriority)
{
	return GetInteractionList(NULL,pEnt,pcType,pcCategory,iPriority);
}

// Returns true if the entity has interaction properties (even if the player can't interact right now)
AUTO_EXPR_FUNC(entityutil) ACMD_NAME("EntIsInteractTarget");
bool exprEntIsInteractTarget(EntityRef entRef)
{
	Entity *pPlayerEnt = entActivePlayerPtr();

	if(pPlayerEnt && pPlayerEnt->pPlayer && entRef)
	{
		return interaction_FindEntityInInteractOptions( entRef ) != NULL;
	}
	return false;
}

// Returns true if the node has interaction properties (even if the player can't interact right now)
AUTO_EXPR_FUNC(entityutil) ACMD_NAME("NodeIsInteractTarget");
bool exprNodeIsInteractTarget(U64 uiNodePtr)
{
	Entity *pPlayerEnt = entActivePlayerPtr();

	if(pPlayerEnt && pPlayerEnt->pPlayer && uiNodePtr)
	{
		WorldInteractionNode *pTargetObject = (WorldInteractionNode*)(uintptr_t)uiNodePtr;
		if ( pTargetObject )
		{
			return interaction_FindNodeInInteractOptions( pTargetObject ) != NULL;
		}
	}
	return false;
}

// Returns whether the player's current entity target or node target is interactable
AUTO_EXPR_FUNC(entityutil) ACMD_NAME("PlayerIsCurrentTargetInteractable");
bool exprPlayerIsCurrentTargetInteractable( void )
{
	Entity *pPlayerEnt = entActivePlayerPtr();

	if(pPlayerEnt && pPlayerEnt->pPlayer && pPlayerEnt->pChar)
	{
		EntityRef erTarget = pPlayerEnt->pChar->currentTargetRef;
		Entity* pEntTarget = entFromEntityRefAnyPartition(erTarget);
		WorldInteractionNode* pNodeTarget = GET_REF(pPlayerEnt->pChar->currentTargetHandle);

		if ( pEntTarget )
		{
			if ( entGetType(pEntTarget) == GLOBALTYPE_ENTITYPLAYER ) //Players aren't interactable
				return false;

			return interaction_FindEntityInInteractOptions(erTarget) != NULL;

		}
		else if ( pNodeTarget )
		{
			return interaction_FindNodeInInteractOptions(pNodeTarget) != NULL;
		}
	}
	return false;
}

// Returns true if the entity is physically holding an object
AUTO_EXPR_FUNC(entityutil) ACMD_NAME(EntIsHoldingObject);
S32 exprEntIsHoldingObject(ExprContext* pContext, Entity *pEnt)
{
	S32 bSuccess = (pEnt && pEnt->pChar && IS_HANDLE_ACTIVE(pEnt->pChar->hHeldNode));
	return bSuccess;
}

// Returns true if there is a nearby NearDeath Entity to use a Power on
AUTO_EXPR_FUNC(entityutil);
S32 NearbyNearDeathExists(void)
{
	Entity *pEnt = entActivePlayerPtr();
	if(pEnt)
		return 0!=clientTarget_GetBestNearDeathPower(pEnt,NULL, true);
	return false;
}

// Returns true if there is a nearby NearDeath Entity is a foe
AUTO_EXPR_FUNC(entityutil);
S32 NearbyNearDeathIsFoe(void)
{
	Entity *pEnt = entActivePlayerPtr();
	if(pEnt)
	{
		EntityRef erTargetOut = 0;
		Entity *pEntTarget;

		clientTarget_GetBestNearDeathPower(pEnt, &erTargetOut, true);
		
		pEntTarget = erTargetOut ? entFromEntityRefAnyPartition(erTargetOut) : NULL;
		if(pEntTarget)
		{
			return character_TargetIsFoe(PARTITION_CLIENT, pEnt->pChar, pEntTarget->pChar);
		}
	}
	return false;
}

// Returns the description of the nearby NearDeath Power activation you could perform
AUTO_EXPR_FUNC(entityutil);
const char *NearbyNearDeathDescription(SA_PARAM_NN_STR const char *pchMsgKeyFriend, SA_PARAM_NN_STR const char *pchMsgKeyFoe)
{
	static char *s_pchNearDeathDescription = NULL;
	Entity *pEnt = entActivePlayerPtr();
	EntityRef er = 0;
	U32 uiID = 0;

	const char *pchEntityName = NULL;
	const char *pchKey = pchMsgKeyFriend;

	if(pEnt && 0!=(uiID=clientTarget_GetBestNearDeathPower(pEnt,&er, true)))
	{
		Entity *pEntTarget = entFromEntityRefAnyPartition(er);
		if(pEntTarget)
		{
			pchEntityName = pEntTarget ? entGetLocalName(pEntTarget) : NULL;
			if(character_TargetIsFoe(PARTITION_CLIENT,pEnt->pChar,pEntTarget->pChar))
				pchKey = pchMsgKeyFoe;
		}
	}

	if(!pchEntityName)
		pchEntityName = TranslateMessageKeySafe("InvalidEntityName");

	estrClear(&s_pchNearDeathDescription);

	FormatGameMessageKey(&s_pchNearDeathDescription,pchKey,
		STRFMT_STRING("Entity",pchEntityName),
		STRFMT_END);

	return s_pchNearDeathDescription;
}

// Activates the appropriate NearDeath-related Power
AUTO_COMMAND ACMD_ACCESSLEVEL(0) ACMD_CATEGORY(Interface, Powers);
void Power_Exec_NearDeath(void)
{
	Entity *pEnt = entActivePlayerPtr();
	if(pEnt && pEnt->pChar)
	{
		EntityRef erTarget = 0;
		U32 uiID = clientTarget_GetBestNearDeathPower(pEnt, &erTarget, true);
		if(uiID)
		{
			GameAccountDataExtract *pExtract = entity_GetCachedGameAccountDataExtract(pEnt);

			character_ActivatePowerByIDClient(PARTITION_CLIENT, pEnt->pChar,uiID,entFromEntityRefAnyPartition(erTarget),NULL,true, pExtract);
		}
	}
}

// ---------------------------------------------------------------------------
// Commands to perform an interaction
// ---------------------------------------------------------------------------

bool interaction_InteractWithOption(SA_PARAM_NN_VALID Entity *pEnt, InteractOption *pOption, bool bClientValidation)
{
	bool bResult = false;

	if (pEnt && pEnt->pChar)
	{
		if (character_AffectedBy(pEnt->pChar, kAttribType_Hold) ||
			character_AffectedBy(pEnt->pChar, kAttribType_Root) ||
			character_AffectedBy(pEnt->pChar, kAttribType_Disable))
		{
			// If held or rooted or disabled, struggle instead of interacting
			Power *pPower = character_FindPowerByCategory(pEnt->pChar,"Struggle");
			PowerDef *pDef = pPower ? GET_REF(pPower->hDef) : NULL;
			if (pDef)
			{
				entUsePowerID(1, pPower->uiID);
				bResult = true;
			}
		}
		else if (pOption && pOption->bCanThrow && IS_HANDLE_ACTIVE(pEnt->pChar->hHeldNode))
		{
			// If currently holding something, throw it instead of interacting
			Power *pPower = character_FindPowerByCategory(pEnt->pChar,"PickUp");
			PowerDef *pDef = pPower ? GET_REF(pPower->hDef) : NULL;
			if (pDef){
				if (pEnt->pPlayer && GET_REF(pEnt->pPlayer->InteractStatus.hPreferredTargetNode))
				{
					clientTarget_ResetTargetChangeTimer();
					entity_SetTargetObject(pEnt, REF_STRING_FROM_HANDLE(pEnt->pPlayer->InteractStatus.hPreferredTargetNode));
				}
				entUsePowerID(1, pPower->uiID);
				bResult = true;
			}
		}
		else if (pOption && pOption->bCanPickup)
		{
			// If pickup interact option, attempt pickup
			Power *pPower = character_FindPowerByCategory(pEnt->pChar,"PickUp");
			PowerDef *pDef = pPower ? GET_REF(pPower->hDef) : NULL;
			if (pDef){
				clientTarget_ResetTargetChangeTimer();
				entity_SetTargetObject(pEnt, REF_STRING_FROM_HANDLE(pOption->hNode));
				entUsePowerID(1, pPower->uiID);
				bResult = true;
			}
		}
		else if (pOption)
		{
			// Normal interaction
			bResult = gclHandleInteractTarget(pEnt, entFromEntityRefAnyPartition(pOption->entRef), GET_REF(pOption->hNode), pOption->pcVolumeName, pOption->iIndex, pOption->iTeammateType, pOption->iTeammateID, pOption->uNodeInteractDist, pOption->vNodePosFallback, pOption->fNodeRadiusFallback, bClientValidation);
		}

		if ( g_CurrentScheme.bStopMovingOnInteract )
		{
			// if the user is auto-running, turn it off
			globCmdParse("autoForward 0");
		}

		entity_ClearOverrideInteract(pEnt);
	}
	return bResult;
}


AUTO_EXPR_FUNC(UIGen) ACMD_NAME(PerformInteraction);
bool interaction_ExprPerformInteraction(SA_PARAM_NN_VALID Entity *pEnt, InteractOption *pOption)
{
	int i;

	if( !pEnt || !pEnt->pPlayer )
		return false;

	if (pOption == NULL) {
		return interaction_InteractWithOption(pEnt, NULL, true);
	}

	for(i=eaSize(&pEnt->pPlayer->InteractStatus.interactOptions.eaOptions)-1; i>=0; --i) {
		if (pOption == pEnt->pPlayer->InteractStatus.interactOptions.eaOptions[i]) {
			if (pOption->bDisabled) {
				return false;
			}

			return interaction_InteractWithOption(pEnt, pOption, true);
		}
	}
	return false;
}

AUTO_EXPR_FUNC(UIGen) ACMD_NAME(PerformInteractionUsingInfo);
bool interaction_ExprPerformInteractionUsingInfo(SA_PARAM_NN_VALID Entity *pEnt, U32 entRef, const char *pcNodeKey, const char *pcVolumeName, int iIndex, int iTeammateType, int iTeammateID)
{
	int i;

	if( !pEnt || !pEnt->pPlayer)
		return false;

	// Index < 0 means interact with nothing
	if (iIndex < 0) {
		return interaction_InteractWithOption(pEnt, NULL, true);
	}

	for(i=eaSize(&pEnt->pPlayer->InteractStatus.interactOptions.eaOptions)-1; i>=0; --i) {
		InteractOption *pOption = pEnt->pPlayer->InteractStatus.interactOptions.eaOptions[i];
		if (((entRef && (pOption->entRef == entRef)) ||
			 (pcNodeKey && pcNodeKey[0] && GET_REF(pOption->hNode) && (stricmp(pcNodeKey, REF_STRING_FROM_HANDLE(pOption->hNode)) == 0)) ||
			 (pcVolumeName && pcVolumeName[0] && (stricmp(pOption->pcVolumeName, pcVolumeName) == 0))
			 ) && (pOption->iIndex == iIndex) && (pOption->iTeammateID == (ContainerID)iTeammateID) && (pOption->iTeammateType == (GlobalType)iTeammateType)) {
			if (pOption->bDisabled) {
				return false;
			}

			return interaction_InteractWithOption(pEnt, pOption, true);
		}
	}
	return false;
}

AUTO_EXPR_FUNC(UIGen) ACMD_NAME(GetOptionalActionCategoryIcon);
char *interaction_ExprGetOptionalActionCategoryIcon(SA_PARAM_NN_VALID UIGen *pGen, SA_PARAM_NN_VALID char *pcCategoryName)
{
	int i;

	if ( !pcCategoryName )
		return NULL;

	for(i=eaSize(&g_eaOptionalActionCategoryDefs)-1; i>=0; --i) {
		if (stricmp(pcCategoryName, g_eaOptionalActionCategoryDefs[i]->pcName) == 0) {
			return (char*)g_eaOptionalActionCategoryDefs[i]->pcIcon;
		}
	}
	return NULL;
}

AUTO_EXPR_FUNC(UIGen, Entity) ACMD_NAME(IsEntityInteractable);
bool interaction_ExprIsEntityInteractable(SA_PARAM_NN_VALID Entity *pEnt)
{
	if(pEnt->pCritter && pEnt->pCritter->bIsInteractable)
		return true;

	return false;
}

AUTO_EXPR_FUNC(UIGen) ACMD_NAME(GetOverrideInteractTargetDisplayString);
const char *interaction_ExprGetOverrideInteractTargetDisplayString()
{
	Entity *pPlayer = entActivePlayerPtr();

	if(pPlayer && pPlayer->pPlayer && pPlayer->pPlayer->InteractStatus.overrideSet)
	{
		int i;
		EntityRef eRef = pPlayer->pPlayer->InteractStatus.overrideRef;
		WorldInteractionNode *pNode = GET_REF(pPlayer->pPlayer->InteractStatus.hOverrideNode);

		for(i=0;i<eaSize(&pPlayer->pPlayer->InteractStatus.eaPromptedInteractOptions);i++)
		{
			if(pPlayer->pPlayer->InteractStatus.eaPromptedInteractOptions[i]->entRef == eRef
				&& GET_REF(pPlayer->pPlayer->InteractStatus.eaPromptedInteractOptions[i]->hNode) == pNode)
			{
				return pPlayer->pPlayer->InteractStatus.eaPromptedInteractOptions[i]->pcInteractString;
			}

		}
	}

	return NULL;
}

AUTO_EXPR_FUNC(UIGen) ACMD_NAME(PlayerHasOverrideInteractTarget);
bool interaction_ExprPlayerHasOverrideInteractTarget()
{
	Entity *pPlayer = entActivePlayerPtr();

	if(pPlayer && pPlayer->pPlayer && pPlayer->pPlayer->InteractStatus.overrideSet)
	{
		if(pPlayer->pPlayer->InteractStatus.overrideRef || IS_HANDLE_ACTIVE(pPlayer->pPlayer->InteractStatus.hOverrideNode))
			return true;
	}
	return false;
}

AUTO_EXPR_FUNC(UIGen, Entity) ACMD_NAME(IsEntityCurrentOverrideInteract);
bool interaction_ExprIsEntityCurrentOverrideInteract(SA_PARAM_NN_VALID Entity *pEnt)
{
	Entity *pPlayer = entActivePlayerPtr();

	if(pPlayer && pPlayer->pPlayer)
	{
		if(pPlayer->pPlayer->InteractStatus.overrideRef == pEnt->myRef)
			return true;
	}

	return false;
}

// Clears current interact override
AUTO_COMMAND ACMD_ACCESSLEVEL(0) ACMD_CATEGORY(Interface, Powers);
void interactOverrideClear(void)
{
	Entity *e = entActivePlayerPtr();
	if (e)
	{
		entity_ClearOverrideInteract(e);
	}
}

bool interactSetOverrideAtCursor(Entity *e, 
	bool bDoInteract,
	Entity **ppEntAtCursorOut, 
	WorldInteractionNode **ppNodeAtCursorOut)
{		
	bool bSetOverride = false;

	if (ppEntAtCursorOut)
	{
		*ppEntAtCursorOut = NULL;
	}

	if (ppNodeAtCursorOut)
	{
		*ppNodeAtCursorOut = NULL;
	}

	if(e)
	{
		Entity *target;
		WorldInteractionNode *pTargetObject = NULL;
		WorldInteractionNode *pOverrideNode = NULL;
		EntityRef erOverrideEnt = 0;
		RegionRules* pRules = getRegionRulesFromRegionType( entGetWorldRegionTypeOfEnt(e) );
		U32 uiObjectMask = pRules && pRules->bClickablesTargetable ? 0 : iBitMaskNamed;
		Entity *pTargetPlayer = NULL;

		if (!e->pPlayer->InteractStatus.bLockIntoCursorOverrides)
		{
			entity_ClearOverrideInteract(e);
			e->pPlayer->InteractStatus.overrideSet = 1;	
			target = target_SelectUnderMouse(e, 0, kTargetType_Player, NULL, false, false, true);		
			pTargetPlayer = target_SelectUnderMouse(e, kTargetType_Player, 0, NULL, false, false, false);

			if (bDoInteract) // We only care about valid interaction options in interact mode
			{
				if(target && target->pCritter && target->pCritter->bIsInteractable)
				{
					//Check to see if the character can interact with this target
					EntityRef targetRef = entGetRef(target);

					S32 i;

					for (i = 0; i < eaSize(&e->pPlayer->InteractStatus.interactOptions.eaOptions); i++)
					{
						if (e->pPlayer->InteractStatus.interactOptions.eaOptions[i]->entRef == targetRef )
						{
							e->pPlayer->InteractStatus.overrideRef = targetRef;
							bSetOverride = true;
							break;
						}
					}
					if (!bSetOverride && GET_REF(target->hCreatorNode))
					{
						for(i=0; i<eaSize(&e->pPlayer->InteractStatus.interactOptions.eaOptions); ++i)
						{
							if (GET_REF(e->pPlayer->InteractStatus.interactOptions.eaOptions[i]->hNode) == GET_REF(target->hCreatorNode))
							{
								COPY_HANDLE(e->pPlayer->InteractStatus.hOverrideNode,e->pPlayer->InteractStatus.interactOptions.eaOptions[i]->hNode);
								bSetOverride = true;
								break;
							}
						}
					}
				}
				
				if (!bSetOverride)
				{				
					S32 i;

					pTargetObject = target_SelectObjectUnderMouse(e, uiObjectMask);

					if (pTargetObject == NULL)
					{
						pTargetObject = target_SelectTooltipObjectUnderMouse(e);
					}

					if (pTargetObject)
					{
						for(i=0; i<eaSize(&e->pPlayer->InteractStatus.interactOptions.eaOptions); ++i)
						{
							if (GET_REF(e->pPlayer->InteractStatus.interactOptions.eaOptions[i]->hNode) == pTargetObject)
							{
								COPY_HANDLE(e->pPlayer->InteractStatus.hOverrideNode,e->pPlayer->InteractStatus.interactOptions.eaOptions[i]->hNode);
								bSetOverride = true;
								break;
							}
						}
					}
				}
			}
			else
			{
				if(target)
				{
					// Is this critter interactable at all
					if (target->pCritter && target->pCritter->bIsInteractable)
					{
						e->pPlayer->InteractStatus.overrideRef = entGetRef(target);
						bSetOverride = true;
					}
				}
				if (!bSetOverride)
				{
					pTargetObject = target_SelectObjectUnderMouse(e, uiObjectMask);

					if (pTargetObject == NULL)
					{
						pTargetObject = target_SelectTooltipObjectUnderMouse(e);
					}

					if (pTargetObject)
					{
						SET_HANDLE_FROM_REFERENT(INTERACTION_DICTIONARY, pTargetObject, e->pPlayer->InteractStatus.hOverrideNode);
						bSetOverride = true;
					}
				}

				if (!bSetOverride)
				{
					if (pTargetPlayer)
					{
						e->pPlayer->InteractStatus.overrideRef = entGetRef(pTargetPlayer);
						bSetOverride = true;
					}
				}
			}

			if (bDoInteract || !bSetOverride)
			{
				// This function can also set an override if InteractRangeToSetAsOverride is defined in the combat config interact options
				gclInteract_FindBest(e);
			}
		}

		// Get the override entity and the node
		erOverrideEnt = e->pPlayer->InteractStatus.overrideRef;
		pOverrideNode = GET_REF(e->pPlayer->InteractStatus.hOverrideNode);

		// Set the output parameters
		if (ppEntAtCursorOut)
		{
			*ppEntAtCursorOut = entFromEntityRefAnyPartition(erOverrideEnt);
		}
		if (ppNodeAtCursorOut)
		{
			*ppNodeAtCursorOut = pOverrideNode;
		}

		if (bDoInteract && (erOverrideEnt || pOverrideNode))
		{
			// Find the interact override in the options and interact with it
			S32 i;
			for (i = 0; i < eaSize(&e->pPlayer->InteractStatus.interactOptions.eaOptions); ++i)
			{
				if ((pOverrideNode && pOverrideNode == GET_REF(e->pPlayer->InteractStatus.interactOptions.eaOptions[i]->hNode)) ||
					(erOverrideEnt && erOverrideEnt == e->pPlayer->InteractStatus.interactOptions.eaOptions[i]->entRef))
				{
					interaction_InteractWithOption(e, e->pPlayer->InteractStatus.interactOptions.eaOptions[i], false);
					return true;
				}
			}
		}
	}
	return bDoInteract ? true: bSetOverride;
}

// Interacts with the object under the cursor.
AUTO_COMMAND ACMD_ACCESSLEVEL(0) ACMD_CATEGORY(Interface, Powers);
bool unifiedInteractAtCursor()
{
	return interactSetOverrideAtCursor(entActivePlayerPtr(), true, NULL, NULL);
}

// Set entity/object under cursor to be interact target
AUTO_COMMAND ACMD_ACCESSLEVEL(0) ACMD_CATEGORY(Interface, Powers);
bool interactOverrideCursor(void)
{
	return interactSetOverrideAtCursor(entActivePlayerPtr(), false, NULL, NULL);
}

AUTO_EXPR_FUNC(UIGen) ACMD_NAME(InteractOverrideCursor);
bool exprInteractOverrideCursor()
{
	return interactOverrideCursor();
}

static bool interactInternal(Entity *pEnt, S32 iSelectionIndex)
{
	if (pEnt->pPlayer && pEnt->pPlayer->InteractStatus.promptInteraction
		&& iSelectionIndex >= 0
		&& iSelectionIndex < eaSize(&pEnt->pPlayer->InteractStatus.eaPromptedInteractOptions))
	{
		InteractOption *pOption = pEnt->pPlayer->InteractStatus.eaPromptedInteractOptions[iSelectionIndex];
		if(pOption)
		{
			return interaction_InteractWithOption(pEnt,pOption, true);
		}
	}
	else
	{
		// If no interact option was actually prompted, try the default behavior on the server
		return interaction_InteractWithOption(pEnt, NULL, true);
	}
	return false;
}

// Interact with the nearest interactable entity within range.
AUTO_COMMAND ACMD_ACCESSLEVEL(0) ACMD_CATEGORY(Interface);
void interact(S32 iSelectionIndex)
{
	Entity *pEnt = entActivePlayerPtr();

	// If promptInteraction is set, tell the server that the player is interacting
	if (pEnt && pEnt->pChar)
	{
		interactInternal(pEnt, iSelectionIndex);
	}
}



AUTO_COMMAND ACMD_ACCESSLEVEL(0) ACMD_CATEGORY(Interface);
void interactOptionPower(const char *pchPowerName)
{
	Entity *pEnt = entActivePlayerPtr();

	if (pEnt && pEnt->pChar)
	{
		if (pchPowerName)
		{
			Power *pPower = character_FindPowerByName(pEnt->pChar, pchPowerName);
			if (pPower)
			{
				// try and find a valid target first
				ClientTargetDef *target;
				int iPartitionIdx = entGetPartitionIdx(pEnt);
				target = clientTarget_SelectBestTargetForPower(pEnt, pPower, NULL);
				if (target)
				{
					Entity *pEntTarget = entFromEntityRefAnyPartition(target->entRef);
					if (pEntTarget)
					{
						GameAccountDataExtract *pExtract = entity_GetCachedGameAccountDataExtract(pEnt);
						character_ActivatePowerByIDClient(iPartitionIdx, pEnt->pChar, pPower->uiID, pEntTarget, NULL, true, pExtract);
						return;
					}
				}

				// no valid power found, try to interact
				if (!interactInternal(pEnt, 0))
				{
					// nothing to interact with, just fire off the power with no target
					GameAccountDataExtract *pExtract = entity_GetCachedGameAccountDataExtract(pEnt);
					character_ActivatePowerByIDClient(iPartitionIdx, pEnt->pChar, pPower->uiID, NULL, NULL, true, pExtract);
				}
				return;
			}
		}

		interactInternal(pEnt, 0);
	}


}

// Calls the default interaction
AUTO_COMMAND ACMD_I_AM_THE_ERROR_FUNCTION_FOR(interact);
void interactDefault(void)
{
	interact(0);
}

// Interact with a non-prompted entity or interactable in range.
AUTO_COMMAND ACMD_ACCESSLEVEL(1) ACMD_CATEGORY(Interface) ACMD_HIDE;
void interactnonprompted(S32 iSelectionIndex)
{
	Entity *pEnt = entActivePlayerPtr();

	if (pEnt && pEnt->pChar)
	{
		if (pEnt->pPlayer && pEnt->pPlayer->InteractStatus.interactOptions.eaOptions
			&& iSelectionIndex >= 0
			&& iSelectionIndex < eaSize(&pEnt->pPlayer->InteractStatus.interactOptions.eaOptions))
		{
			InteractOption *pOption = pEnt->pPlayer->InteractStatus.interactOptions.eaOptions[iSelectionIndex];
			if(pOption)
			{
				interaction_InteractWithOption(pEnt, pOption, true);
			}
		}
		else
		{
			// If no interact option was actually prompted, try the default behavior on the server
			interaction_InteractWithOption(pEnt, NULL, true);
		}
	}
}

AUTO_COMMAND ACMD_ACCESSLEVEL(1) ACMD_CATEGORY(Interface) ACMD_HIDE;
void interactremote(S32 iSelectionIndex)
{
	Entity *pEnt = entActivePlayerPtr();

	if (pEnt && pEnt->pChar)
	{
		if (pEnt->pPlayer && pEnt->pPlayer->pInteractInfo && pEnt->pPlayer->pInteractInfo->eaRemoteContacts
			&& iSelectionIndex >= 0
			&& iSelectionIndex < eaSize(&pEnt->pPlayer->pInteractInfo->eaRemoteContacts))
		{
			RemoteContact *pRemoteContact = pEnt->pPlayer->pInteractInfo->eaRemoteContacts[iSelectionIndex];
			if(pRemoteContact)
			{
				ServerCmd_contact_StartRemoteContact(pRemoteContact->pchContactDef);
			}
		}
	}
}

AUTO_EXPR_FUNC(UIGen) ACMD_NAME(InteractionHasContactDialog);
bool interaction_ExprInteractHasContactDialogue();

AUTO_EXPR_FUNC(UIGen) ACMD_NAME(InteractionHasContactDialogue);
bool interaction_ExprInteractHasContactDialogue()
{
	Entity *pPlayerEnt = entActivePlayerPtr();
	if(pPlayerEnt && pPlayerEnt->pPlayer && pPlayerEnt->pPlayer->pInteractInfo)
	{
		return !!(pPlayerEnt->pPlayer->pInteractInfo->pContactDialog);
	}
	return false;
}

AUTO_EXPR_FUNC(UIGen) ACMD_NAME(InteractWithNode);
bool interaction_ExprInteractWithNode(SA_PARAM_OP_VALID WorldInteractionNode* pNode)
{
	Entity *pPlayerEnt = entActivePlayerPtr();
	if(pPlayerEnt && pPlayerEnt->pPlayer && pNode)
	{
		return interaction_InteractWithOption(pPlayerEnt, interaction_FindNodeInInteractOptions(pNode), true);
	}
	return false;
}

AUTO_COMMAND ACMD_ACCESSLEVEL(0) ACMD_CATEGORY(Interface) ACMD_HIDE;
void interactWithNode(U64 pNodePtr)
{
	interaction_ExprInteractWithNode((WorldInteractionNode*)(uintptr_t)pNodePtr);
}

AUTO_COMMAND ACMD_ACCESSLEVEL(0) ACMD_CATEGORY(Interface) ACMD_HIDE;
void interactWithEnt(EntityRef entRef)
{
	Entity *pPlayerEnt = entActivePlayerPtr();

	if(pPlayerEnt && pPlayerEnt->pPlayer && entRef)
	{
		interaction_InteractWithOption(pPlayerEnt, interaction_FindEntityInInteractOptions(entRef), true);
	}
}

AUTO_EXPR_FUNC(UIGen) ACMD_NAME(interactWithTargetUnderMouse);
bool interaction_ExprInteractWithTargetUnderMouse(void)
{
	Entity* pPlayerEnt = entActivePlayerPtr();
	if (pPlayerEnt)
	{
		Entity* pTargetEnt = target_SelectUnderMouse(pPlayerEnt, 0, kTargetType_Self, NULL, true, false, true);
		if (pTargetEnt)
		{
			return interaction_InteractWithOption(pPlayerEnt, interaction_FindEntityInInteractOptions(entGetRef(pTargetEnt)), true);
		}
		else
		{
			WorldInteractionNode* pTargetNode = target_SelectObjectUnderMouse(pPlayerEnt, 0);
			if (pTargetNode)
			{
				return interaction_InteractWithOption(pPlayerEnt, interaction_FindNodeInInteractOptions(pTargetNode), true);
			}
		}
	}
	return false;
}

AUTO_COMMAND ACMD_ACCESSLEVEL(0) ACMD_CATEGORY(Interface) ACMD_HIDE;
void interactWithCurrentTarget(void)
{
	Entity *pPlayerEnt = entActivePlayerPtr();

	if ( pPlayerEnt && pPlayerEnt->pChar && pPlayerEnt->pPlayer )
	{
		EntityRef erTarget = pPlayerEnt->pChar->currentTargetRef;
		Entity* pEntTarget = entFromEntityRefAnyPartition(erTarget);
		WorldInteractionNode* pNodeTarget = GET_REF(pPlayerEnt->pChar->currentTargetHandle);
		if ( pEntTarget )
		{
			interaction_InteractWithOption(pPlayerEnt, interaction_FindEntityInInteractOptions(erTarget), true);
		}
		else if ( pNodeTarget )
		{
			interaction_InteractWithOption(pPlayerEnt, interaction_FindNodeInInteractOptions(pNodeTarget), true);
		}
	}
}

// Interact with the nearest interactable entity within range, followed by volume
AUTO_COMMAND ACMD_ACCESSLEVEL(0) ACMD_CATEGORY(Interface);
void interactIncludeVolume()
{
	Entity *pEnt = entActivePlayerPtr();

	// If promptInteraction is set, tell the server that the player is interacting
	if (pEnt && pEnt->pChar)
	{
		InteractOption *pOption = NULL;

		if (pEnt->pPlayer)
		{
			if (pEnt->pPlayer->InteractStatus.promptInteraction &&
				eaSize(&pEnt->pPlayer->InteractStatus.eaPromptedInteractOptions))
				pOption = pEnt->pPlayer->InteractStatus.eaPromptedInteractOptions[0];
			else  if (eaSize(&pEnt->pPlayer->InteractStatus.interactOptions.eaOptions))
				pOption = pEnt->pPlayer->InteractStatus.interactOptions.eaOptions[0];

		}

		interaction_InteractWithOption(pEnt, pOption, true);
	}
}

// Indicates whether the player should be locked into the current interact overrides at the cursor
AUTO_EXPR_FUNC(UIGen) ACMD_NAME(SetLockStatusForInteractOverridesAtCursor);
void interaction_ExprSetLockStatusForInteractOverridesAtCursor(SA_PARAM_NN_VALID Entity *pEnt, bool bLocked)
{
	pEnt->pPlayer->InteractStatus.bLockIntoCursorOverrides = bLocked;
}

AUTO_EXPR_FUNC(UIGen) ACMD_NAME(GetInteractionListAtCursor);
S32 interaction_ExprGetInteractionListAtCursor(SA_PARAM_OP_VALID UIGen *pGen, SA_PARAM_OP_VALID Entity *pEnt, bool bIncludeVolumeInteractions, const char *pchExcludeCategories)
{
	static InteractOption **s_eaOptions = NULL;

	eaClear(&s_eaOptions);

	if (pEnt && pEnt->pPlayer) 
	{
		InteractOption *pPreviousOption = NULL;
		const char **ppchExcludeCategories = interaction_GetInteractCategoryListFromString(pchExcludeCategories);
		bool bPreviousTest = false;
		S32 i;

		for (i = 0; i < eaSize(&pEnt->pPlayer->InteractStatus.interactOptions.eaOptions); ++i) 
		{
			InteractOption *pOption = pEnt->pPlayer->InteractStatus.interactOptions.eaOptions[i];

			if ((pOption->entRef && pOption->entRef != pEnt->pPlayer->InteractStatus.overrideRef) || // Entity interaction but does not match entity override
				(IS_HANDLE_ACTIVE(pOption->hNode) && !REF_COMPARE_HANDLES(pEnt->pPlayer->InteractStatus.hOverrideNode, pOption->hNode)) || // Node interaction but does not match node override
				(!bIncludeVolumeInteractions && pOption->pcVolumeName))
			{
				// This is not an interaction we're interested
				continue;
			}

			if (eaSize(&ppchExcludeCategories)) 
			{
				if (pOption->pcCategory && (eaFind(&ppchExcludeCategories, pOption->pcCategory) >= 0)) {
					continue;
				}
			}
			// Test for entity distance and line of sight
			if (pOption->entRef) {
				if (pPreviousOption && (pOption->entRef == pPreviousOption->entRef)) {
					// Previous was same entity, so use previous test result
					if (!bPreviousTest) {
						continue;
					}
				} else if (!entity_VerifyInteractTarget(PARTITION_CLIENT, pEnt, entFromEntityRefAnyPartition(pOption->entRef), NULL, 0, 0, 0, false, NULL)) {
					// Don't have line of sight and distance on entity
					pPreviousOption = pOption;
					bPreviousTest = false;
					continue;
				}
			}

			// Test for node distance and line of sight
			if (GET_REF(pOption->hNode)) {
				if (pPreviousOption && (GET_REF(pOption->hNode) == GET_REF(pPreviousOption->hNode))) {
					// Previous was same node, so use previous test result
					if (!bPreviousTest) {
						continue;
					}
				} else if (!entity_VerifyInteractTarget(PARTITION_CLIENT, pEnt, NULL, GET_REF(pOption->hNode), pOption->uNodeInteractDist, pOption->vNodePosFallback, pOption->fNodeRadiusFallback, pOption->bCanPickup, NULL)) {
					// Don't have line of sight and distance on node
					pPreviousOption = pOption;
					bPreviousTest = false;
					continue;
				}
			}

			pPreviousOption = pOption;
			bPreviousTest = true;
			eaPush(&s_eaOptions, pOption);
		}

		eaQSort(s_eaOptions, interaction_CompareOptions);

	}

	if (pGen)
	{
		ui_GenSetListSafe(pGen, &s_eaOptions, InteractOption);
	}

	return eaSize(&s_eaOptions);
}

AUTO_EXPR_FUNC(UIGen) ACMD_NAME(GetInteractionListCountAtCursor);
S32 interaction_ExprGetInteractionListCountAtCursor(SA_PARAM_OP_VALID Entity *pEnt, bool bIncludeVolumeInteractions, const char *pchExcludeCategories)
{
	return interaction_ExprGetInteractionListAtCursor(NULL, pEnt, bIncludeVolumeInteractions, pchExcludeCategories);
}

#include "AutoGen/InteractionUI_c_ast.c"
#include "AutoGen/InteractionUI_h_ast.c"
