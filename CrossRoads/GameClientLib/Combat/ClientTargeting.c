#include "ClientTargeting.h"
#include "Entity.h"
#include "EntityClient.h"
#include "EntitySavedData.h"
#include "error.h"
#include "FolderCache.h"
#include "Character.h"
#include "CombatEval.h"
#include "PowerActivation.h"
#include "PowerAnimFX.h"
#include "ClientTargeting.h"
#include "Character_target.h"
#include "CharacterClass.h"
#include "cmdClient.h"
#include "GameAccountDataCommon.h"
#include "ItemCommon.h"
#include "UICore.h"
#include "UITray.h"
#include "PowerModes.h"
#include "RegionRules.h"
#include "chat/gclChatLog.h"
#include "CombatConfig.h"
#include "mapstate_common.h"
#include "gclMapState.h"

// InputLib
#include "inputGamepad.h"
#include "inputLib.h"

// EditLib
#include "EditLib.h"

// EntityLib
#include "EntityIterator.h"
#include "EntityLib.h"
#include "EntityMovementManager.h"
#include "entCritter.h"
#include "Player.h"
#include "SavedPetCommon.h"

// gameClientLib
#include "GameClientLib.h"
#include "gclEntity.h"
#include "gclUtils.h"
#include "gclCamera.h"
#include "gclControlScheme.h"
#include "gclCombatDeathPrediction.h"
#include "gclCursorMode.h"
#include "gclReticle.h"
#include "gclPlayerControl.h"
#include "interactionClient.h"
#include "InteractionUI.h"
#include "EditorManager.h"
#include "PowerSlots.h"
#include "Character_combat.h"
#include "PowersMovement.h"
#include "gclCommandParse.h"
#include "UIGen.h"

// Utilities lib
#include "ScratchStack.h"
#include "CmdParse.h"
#include "StringCache.h"
#include "LineDist.h"

// WorldLib
#include "WorldLib.h"
#include "WorldGrid.h"
#include "WorldColl.h"
#include "wlInteraction.h"
#include "dynFxInterface.h"
#include "dynNode.h"
#include "dynNodeInline.h"

// GraphicsLib
#include "GfxSprite.h"
#include "GfxSpriteText.h"
#include "dynFxInfo.h"
#include "CharacterAttribs.h"
#include "GfxTexAtlas.h"
#include "GraphicsLib.h"

#include "AutoGen/ClientTargeting_c_ast.h"
#include "Autogen/GameServerLib_autogen_ServerCmdWrappers.h"

#include "mission_common.h"

AUTO_RUN_ANON(memBudgetAddMapping(__FILE__, BUDGET_GameSystems););

U32 iBitMaskDes = 0;
U32 iBitMaskNamed = 0;
U32 iBitMaskClick = 0;
U32 iBitMaskThrow = 0;
bool g_bDebugTargeting = false;
bool g_bDebugNearestPoint = false;
bool g_bDebugRayCastMaterials = false;
bool g_bDebugDrawTargetBoxes = false;

#define PLAYER_ONCLICK_REPORT_TIMEOUT	1
#define SOFT_TARGET_EXC 0

#define HIGH_TARGET_DIST 30

extern bool g_bSelectAnyEntity = false;
extern bool g_aiDebugShowing;

static ClientTargetMutableDef s_LastTargetDef;
static ClientTargetMutableDef s_TargetDef;
static ClientTargetMutableDef s_HardTargetDef;
static ClientTargetMutableDef s_TargetDual;
static ClientTargetDef s_BestTargetDef;
static U32 s_LastDeadTargetChange;
static U32 s_LastTargetMs;

typedef struct TargetCursorUpdateFxState
{
	// Handle to the active FX
	dtFx hActiveFx;

	// The entity we're playing the FX for
	EntityRef erActiveFxEntRef;
} TargetCursorUpdateFxState;

typedef struct TargetSearchDetails
{
	Vec3 analog_direction;
	Vec2 screen_Loc;
} TargetSearchDetails;

typedef struct TargetEntityPriority
{
	Entity *pEntity;
	F32 fPriority;
	Vec3 vCollisionPoint;
	U32 bIsInteractable : 1;
} TargetEntityPriority;

static TargetSearchDetails s_SearchDetails;

static int s_iBoxWidth = 10; // defines the tolerance of the lookat targeting
static int s_iBoxHeight = 10;
static F32 s_fMultiExecTimer = -1;

static bool s_bTargetIsNew = false; // This is only for displaying feedback when a player selects a new target.
static EntityRef s_erLastAttacker = 0;

static F32 s_fStickyHardTargetTime = 0.f;

// todo: put more of the static variables in here to clean things up.
static struct 
{
	S32		bCameraTargetingUsesDirectionKeysOverride;

	U32		bDirectionKeysOverrideSet : 1;
} s_ClientTargetingData = {0};

AUTO_STRUCT;
typedef struct ClientInteractContactCursorDef
{
	ContactIndicator eContactIndicator; AST(NAME(ContactIndicator) SUBTABLE(ContactIndicatorEnum))
		// The contact indicator to associate with this cursor
	REF_TO(UICursor) hCursor;			AST(NAME(Cursor))
	REF_TO(UICursor) hCursorDisabled;	AST(NAME(CursorDisabled))
} ClientInteractContactCursorDef;

AUTO_STRUCT;
typedef struct ClientInteractObjectCursorDef
{
	const char* pchInteractableCategory;	AST(NAME(InteractableCategory) POOL_STRING)
		// The interactable category to assocate with this cursor
	REF_TO(UICursor) hCursor;				AST(NAME(Cursor))
	REF_TO(UICursor) hCursorDisabled;		AST(NAME(CursorDisabled))
} ClientInteractObjectCursorDef;

AUTO_STRUCT;
typedef struct ClientTargetCursorModeDef
{
	// Default cursors
	REF_TO(UICursor) hCursorDefault;				AST(NAME(CursorDefault) REFDICT(UICursor) NON_NULL_REF)
	REF_TO(UICursor) hCursorPause;					AST(NAME(CursorPause) REFDICT(UICursor))
	REF_TO(UICursor) hCursorCombat;					AST(NAME(CursorCombat) REFDICT(UICursor))
	REF_TO(UICursor) hCursorCombatDisabled;			AST(NAME(CursorCombatDisabled) REFDICT(UICursor))
	REF_TO(UICursor) hCursorLoot;					AST(NAME(CursorLoot) REFDICT(UICursor))
	REF_TO(UICursor) hCursorLootDisabled;			AST(NAME(CursorLootDisabled) REFDICT(UICursor))
	REF_TO(UICursor) hCursorDestructible;			AST(NAME(CursorDestructible) REFDICT(UICursor))
	REF_TO(UICursor) hCursorPlayerGroup;			AST(NAME(CursorPlayerGroup) REFDICT(UICursor))
	REF_TO(UICursor) hCursorPlayerSolo;				AST(NAME(CursorPlayerSolo,hCursorPlayerSolo) REFDICT(UICursor))
	REF_TO(UICursor) hCursorInteractEntity;			AST(NAME(CursorInteractEntity) REFDICT(UICursor))
	REF_TO(UICursor) hCursorInteractEntityDisabled;	AST(NAME(CursorInteractEntityDisabled) REFDICT(UICursor))
	REF_TO(UICursor) hCursorInteractObject;			AST(NAME(CursorInteractObject) REFDICT(UICursor))
	REF_TO(UICursor) hCursorInteractObjectDisabled;	AST(NAME(CursorInteractObjectDisabled) REFDICT(UICursor))
	
	// Custom cursors
	ClientInteractContactCursorDef** eaContactDefs;	AST(NAME(CustomContactCursor))
	ClientInteractObjectCursorDef** eaObjectDefs;	AST(NAME(CustomObjectCursor))

	bool bAllowUntargetableEnts;

	// FX played on interactables when the player mouse over them
	const char *pchInteractableMouseOverFX;			AST(NAME(InteractMouseOverFX) POOL_STRING)

} ClientTargetCursorModeDef;

static ClientTargetCursorModeDef s_TargetCursorModeDef;

AUTO_CMD_INT(g_bDebugTargeting,DebugTargeting);
AUTO_CMD_INT(g_bDebugNearestPoint,DebugNearPoint);
AUTO_CMD_INT(g_bDebugRayCastMaterials,RayCastMaterialDebug);
AUTO_CMD_INT(g_bDebugDrawTargetBoxes,drawTargetBoxes);

void gclClientTarget_Shutdown()
{
	s_ClientTargetingData.bDirectionKeysOverrideSet = false;
}


static void CopyClientTargetDefToMutable( ClientTargetMutableDef* pDst, const ClientTargetDef* pSrc )
{
	REMOVE_HANDLE(pDst->hInteractionNode);

	if (pSrc)
	{	
		pDst->entRef = pSrc->entRef;
		pDst->fDist = pSrc->fDist;
		pDst->fSortDist = pSrc->fSortDist;
		pDst->bSoft = pSrc->bSoft;

		COPY_HANDLE(pDst->hInteractionNode, pSrc->hInteractionNode);
	}
	else
	{
		pDst->entRef = 0;
		pDst->fDist = 0;
		pDst->fSortDist = 0;
		pDst->bSoft = 0;
	}
}

static void CopyClientTargetDef( ClientTargetDef* pDst, const ClientTargetDef* pSrc )
{
	REMOVE_HANDLE(pDst->hInteractionNode);

	if (pSrc)
	{	
		pDst->entRef = pSrc->entRef;
		pDst->fDist = pSrc->fDist;
		pDst->fSortDist = pSrc->fSortDist;
		pDst->bSoft = pSrc->bSoft;

		COPY_HANDLE(pDst->hInteractionNode, pSrc->hInteractionNode);
	}
	else
	{
		pDst->entRef = 0;
		pDst->fDist = 0;
		pDst->fSortDist = 0;
		pDst->bSoft = 0;
	}
}



int clientTarget_CheckAngleArc(Entity *entSource, ClientTargetDef *pTarget, float ang, bool forward, bool rot90arc, bool bothArcs);
static int clientTarget_IsTargetWithinDirAngleArc(Entity *entSource, ClientTargetDef *pTarget, const Vec3 vDir, F32 ang, F32 *pfAngleBetweenOut);

void clientTarget_ResetTargetChangeTimer(void)
{
	s_LastDeadTargetChange = 0;
}

AUTO_STARTUP(ClientTargeting) ASTRT_DEPS(CombatConfig);
void initMultiExecTimer(void)
{
	s_fMultiExecTimer = g_CombatConfig.fMultiExecListClearTimer;
}

AUTO_STARTUP(ClientInteraction) ASTRT_DEPS(WorldLibZone);
void setClientBitmasks(void)
{
	if(!iBitMaskDes || !iBitMaskNamed || !iBitMaskClick || !iBitMaskThrow)
	{
		iBitMaskDes = wlInteractionClassNameToBitMask("Destructible");
		iBitMaskNamed = wlInteractionClassNameToBitMask("NamedObject");
		iBitMaskClick = wlInteractionClassNameToBitMask("Clickable");
		iBitMaskThrow = wlInteractionClassNameToBitMask("Throwable");
	}
}

// Debug data to draw cones to represent your targeting selections
#include "GfxPrimitive.h"

#define DEBUGWRITELINE(a) gfxfont_PrintEx(&g_font_Sans, 30, iFirstLine + iPixPerLine*(iCurrentLine++), 0, 1.1, 1.1, 0, a, (int)strlen(a), colors)

void target_debugdrawnearestpoint(void)
{
	Vec3 vNearPoint;
	Entity *pEntPlayer = entActivePlayerPtr();
	const int iFirstLine = 170;
	const int iPixPerLine = 20;
	int iCurrentLine = 0;
	char achPrintline[1024];
	int colors[] = {-1, -1, -1, -1};

	if(!pEntPlayer || !pEntPlayer->pChar)
		return;
	
	if ( IS_HANDLE_ACTIVE(pEntPlayer->pChar->currentTargetHandle) || pEntPlayer->pChar->currentTargetRef )
	{
		character_FindNearestPointForTarget(PARTITION_CLIENT, pEntPlayer->pChar,vNearPoint);
	}
	else if ( eaSize(&pEntPlayer->pPlayer->InteractStatus.eaPromptedInteractOptions) )
	{
		Vec3 vPos;
		WorldInteractionNode* pNode = GET_REF(pEntPlayer->pPlayer->InteractStatus.eaPromptedInteractOptions[0]->hNode);

		entGetCombatPosDir(pEntPlayer,NULL,vPos,NULL);

		if ( pNode )
		{
			wlInterationNode_FindNearestPoint( vPos, pNode, vNearPoint );
		}
	}

	if(vec3IsZero(vNearPoint))
		return;

	gfxDrawSphere3D(vNearPoint,.2,20,ColorRed,0);

	sprintf(achPrintline,"Calculated Near Point: %f, %f, %f",vNearPoint[0],vNearPoint[1],vNearPoint[2]);
	DEBUGWRITELINE(achPrintline);

	if(IS_HANDLE_ACTIVE(pEntPlayer->pChar->currentTargetHandle))
	{
		WorldInteractionNode *pTarget = GET_REF(pEntPlayer->pChar->currentTargetHandle);
		
		if(pTarget)
			wlInteractionNodeGetWorldMid(pTarget,vNearPoint);
	}
	else if(pEntPlayer->pChar->currentTargetRef)
	{
		Entity *eTarget = entFromEntityRefAnyPartition(pEntPlayer->pChar->currentTargetRef);

		if(eTarget && IS_HANDLE_ACTIVE(eTarget->hCreatorNode))
		{
			WorldInteractionNode *pTarget = GET_REF(eTarget->hCreatorNode);

			if(pTarget)
				wlInteractionNodeGetWorldMid(pTarget,vNearPoint);
		}
	}
	gfxDrawSphere3D(vNearPoint,.2,20,ColorBlue,0);
	sprintf(achPrintline,"Center of selected object: %f, %f, %f",roundFloatWithPrecision(vNearPoint[0],3),roundFloatWithPrecision(vNearPoint[1],3),roundFloatWithPrecision(vNearPoint[2],3));
	DEBUGWRITELINE(achPrintline);
}

void target_debugdrawarcs(void)
{
	int i;
	Vec3 vecPos,vecPYR;
	Entity *e = entActivePlayerPtr();

	if(!(e && e->pChar->bDisableFaceActivate))
		return;

	entGetCombatPosDir(e,NULL,vecPos,NULL);
	entGetFacePY(e,vecPYR);
	vecPYR[2] = 0;
	vecPYR[0] = RAD(90);

	for(i=eaSize(&e->pChar->ppPowers)-1; i>=0; i--)
	{
		Vec3 vecPYRPow;
		Quat qRotPow;
		Mat4 mFinal;
		Power *ppow = e->pChar->ppPowers[i];
		PowerDef *pdef = GET_REF(ppow->hDef);
		F32 fRange;

		if(!(pdef && pdef->fTargetArc))
			continue;

		copyVec3(vecPYR,vecPYRPow);
		vecPYRPow[1] += ppow->fYaw;
		PYRToQuat(vecPYRPow,qRotPow);
		quatToMat(qRotPow,mFinal);
		copyVec3(vecPos,mFinal[3]);

		fRange = power_GetRange(ppow, pdef);
		gfxDrawCone3D(mFinal,fRange,RAD(pdef->fTargetArc),10,ColorGreen);
	}
}


static WorldInteractionNode *s_pnodePotentialLegal = NULL;
S32 target_IsLegalTargetForExpression(Entity *e, Expression *pExprLegal, Entity *pEnt, WorldInteractionNode *pNode)
{
	F32 fLegal = 0.f;
	s_pnodePotentialLegal = pNode;
	combateval_ContextSetupActivate(e->pChar,pEnt?pEnt->pChar:NULL,NULL,kCombatEvalPrediction_None);
	fLegal = combateval_EvalNew(entGetPartitionIdx(e),pExprLegal,kCombatEvalContext_Activate,NULL);
	s_pnodePotentialLegal = NULL;
	return (fLegal!=0.f);
}

WorldInteractionNode *target_GetPotentialLegalNode(void)
{
	return s_pnodePotentialLegal;
}


static struct {
	S32		enabled;
	char*	posCmd;
	char*	entCmd;
	char*	wcoCmd;
} cmdOnClick;

// Enables running a command when clicking in the 3D view.
// Takes two command parameters.  One can contain "<pos>" and the other "<ent>".
// When clicking, it is determined if you are clicking a position or an entity and the
// appropriate command is executed, filling in that position or entity in place
// of "<pos>" or "<ent>".  If you don't have an "<ent>" command, then it will use the
// "<pos>" command even if you click on an entity.
// Usage: bind lshift +cmdOnClick "ec selected setpos <pos>" "ec selected setPosAtEnt <ent>"
AUTO_COMMAND ACMD_NAME(cmdOnClick);
void cmdCmdOnClick(S32 enabled, const char* cmd1, const char* cmd2)
{
	cmdOnClick.enabled = !!enabled;
	
	if(enabled)
	{
		const char* cmds[2] = {cmd1, cmd2};
		
		estrCopy2(&cmdOnClick.posCmd, "");
		estrCopy2(&cmdOnClick.entCmd, "");
		estrCopy2(&cmdOnClick.wcoCmd, "");
		
		ARRAY_FOREACH_BEGIN(cmds, i);
			const char* cmd = cmds[i];
			
			if(strstri(cmd, "<pos>")){
				estrCopy2(&cmdOnClick.posCmd, cmd);
			}
			else if(strstri(cmd, "<ent>")){
				estrCopy2(&cmdOnClick.entCmd, cmd);
			}
			else if(strstri(cmd, "<wco>")){
				estrCopy2(&cmdOnClick.wcoCmd, cmd);
			}
		ARRAY_FOREACH_END;
	}
}


#define TARGET_MAX_NODE_DISTANCE 10000

//checks to see if this node is in the list of targetable nodes, then performs a LoS check
bool target_IsObjectTargetable(Entity* e, WorldInteractionNode *pTargetNode)
{
	U32 i, iSize = eaSize( &e->pPlayer->InteractStatus.ppTargetableNodes );
	
	for ( i = 0; i < iSize; i++ )
	{
		WorldInteractionNode* pNode = GET_REF(e->pPlayer->InteractStatus.ppTargetableNodes[i]->hNode);

		if ( pNode==NULL )
			continue;
		
		if ( pNode == pTargetNode )
		{
			Vec3 vPos, vMid;
			WorldInteractionEntry* pEntry = wlInteractionNodeGetEntry(pNode);

			entGetCombatPosDir(e, NULL, vPos, NULL);
			wlInteractionNodeGetWorldMid(pNode,vMid);

			if(combat_CheckLoS(PARTITION_CLIENT, vPos,vMid,e,NULL,pEntry,0,false,NULL))
			{
				return true;
			}

			return false;
		}
	}

	return false;
}

static bool _rayCastChildNode(WorldInteractionNode *pNode,const Vec3 vStart,const Vec3 vEnd,Vec3 vIntersectPoint)
{
	Mat4 invMat;
	Vec3 vMin, vMax, vMid;

	WorldInteractionEntry* pBoundsEntry = wlInteractionNodeGetEntry(pNode);
	if (pBoundsEntry == NULL)
		return false;  // entry not received yet

	// first, do a gross check against the octree AABB
	wlInteractionNodeGetWorldBounds(pNode, vMin, vMax, vMid);
	if (!lineBoxCollision(vStart, vEnd, vMin, vMax, vIntersectPoint))
		return false;

	invertMat4(pBoundsEntry->base_entry.bounds.world_matrix, invMat);
	// if we passed the octree AABB test, now do an OBB test
	if (!lineOrientedBoxCollision(vStart,vEnd,pBoundsEntry->base_entry.bounds.world_matrix,invMat,pBoundsEntry->base_entry.shared_bounds->local_min,pBoundsEntry->base_entry.shared_bounds->local_max,vIntersectPoint))
		return false;

	return true;
}

bool target_InteractionRaycastNode(WorldInteractionNode *pNode,const Vec3 vStart,const Vec3 vEnd,Vec3 vIntersectPoint)
{
	WorldInteractionNode * pBoundsNode = pNode;

	if (wlInteractionNodeUseChildBounds(pNode))
	{
		int i;
		WorldInteractionEntry* pEntry = wlInteractionNodeGetEntry(pNode);
		if (pEntry == NULL)
			return false;  // entry not received yet

		for (i = 0; i < eaSize(&pEntry->child_entries); ++i)
		{
			if (pEntry->child_entries[i]->type == WCENT_INTERACTION)
			{
				WorldInteractionNode * pChildNode = GET_REF(((WorldInteractionEntry*) pEntry->child_entries[i])->hInteractionNode);

				if (pChildNode && !mapState_IsNodeHiddenOrDisabled(pChildNode))
				{
					// found a node we want to check
					bool bHitNode = _rayCastChildNode(pChildNode,vStart,vEnd,vIntersectPoint);
					if (bHitNode)
					{
						return true;
					}
				}
			}
		}
	}
	else
	{
		return _rayCastChildNode(pNode,vStart,vEnd,vIntersectPoint);
	}

	return false;
}

static void target_getInteractionNodeBounding(WorldInteractionNode* pNode, Vec3 vMin, Vec3 vMax)
{
	Mat4 vWorldMtx;
	F32 radius;
	Vec3 vBoundingSize;

	wlInteractionNodeGetLocalBounds(pNode, vMin, vMax, vWorldMtx);
	radius = wlInteractionNodeGetRadius(pNode);
	setVec3same(vBoundingSize, radius);

	subVec3(vWorldMtx[3], vBoundingSize, vMin);
	addVec3(vWorldMtx[3], vBoundingSize, vMax);
}


// -------------------------------------------------------------------------------------------------------------------------
void target_GetCursorRayEx(Entity* e, Vec3 vCursorStart, Vec3 vCursorDir, bool bCheckShowMouseLookReticle, bool bCombatEntityTargeting)
{
	Mat4 xCamMat;
	if ((!bCheckShowMouseLookReticle || g_CurrentScheme.bShowMouseLookReticle) &&
		g_CurrentScheme.bMouseLookHardTarget && 
		gclPlayerControl_IsMouseLooking())
	{
		ClientReticle reticle = { 0 };
		Vec3 vPos, vCursorEnd;
		gclReticle_GetReticle(&reticle, bCombatEntityTargeting);
		gfxGetActiveCameraMatrix(xCamMat);

		editLibCursorRayEx(xCamMat, reticle.iReticlePosX, reticle.iReticlePosY, vCursorStart, vCursorDir);

		if (e)
		{
			const F32 fPlaneLineDist = 1000.0f;
			Vec3 vPi;
			Vec4 vPlane;
			entGetPos(e,vPos);
			setVec3(vPlane, vCursorDir[0], 0.0f, vCursorDir[2]);
			normalVec3(vPlane);
			vPlane[3] = dotVec3(vPlane, vPos);
			scaleAddVec3(vCursorDir, fPlaneLineDist, vCursorStart, vCursorEnd);
			if (intersectPlane(vCursorStart, vCursorEnd, vPlane, vPi))
			{
				copyVec3(vPi, vCursorStart);
			}
		}
	}
	else
	{
		int mx, my;

		gfxGetActiveCameraMatrix(xCamMat);
		mousePosCurrent(&mx, &my);
		editLibCursorRayEx(xCamMat, mx, my, vCursorStart, vCursorDir);
	}
}

WorldInteractionNode * target_SelectObjectUnderMouse(Entity *e, U32 interaction_class_mask)
{
	WorldInteractionNode *currObject, *bestObject = 0;
	Vec3 vCursorStart, vCursorDir, vCursorEnd;
	F32 closest=FLT_MAX;
	Vec3 vBestHit;
	int i;

	currObject = GET_REF(e->pChar->currentTargetHandle);

	target_GetCursorRay(e, vCursorStart, vCursorDir);
	scaleAddVec3(vCursorDir,TARGET_MAX_NODE_DISTANCE,vCursorStart,vCursorEnd);

	for(i=eaSize(&e->pPlayer->InteractStatus.ppTargetableNodes)-1;i>=0;i--)
	{
		Vec3 vCollision;
		F32 dist;

		WorldInteractionNode* pNode = GET_REF(e->pPlayer->InteractStatus.ppTargetableNodes[i]->hNode);

		if (!pNode)
			continue;

		if ( interaction_class_mask && !wlInteractionClassMatchesMask( pNode, interaction_class_mask ) )
			continue;

		if (!target_InteractionRaycastNode(pNode,vCursorStart,vCursorEnd,vCollision))
			continue;

		dist = distance3Squared(vCollision, vCursorStart);

		if(dist < closest)
		{
			closest = dist;
			bestObject = pNode;
			copyVec3( vCollision, vBestHit );
		}
	}

	if(!bestObject && currObject)
	{
		Vec3 vCollision;

		if (target_InteractionRaycastNode(currObject,vCursorStart,vCursorEnd,vCollision))
		{
			bestObject = currObject;
			copyVec3( vCollision, vBestHit );
		}
	}

	if ( bestObject )
	{
		if(!wlInteractionNodeCheckLineOfSight(PARTITION_CLIENT,bestObject,vCursorStart))
		{
			return NULL;
		}
	}

	return bestObject;
}

WorldInteractionNode * target_SelectTooltipObjectUnderMouse(Entity *e)
{
	WorldInteractionNode *currObject, *bestObject = 0;
	Vec3 vCursorStart, vCursorDir, vCursorEnd;
	F32 closest=FLT_MAX;
	Vec3 vBestHit;
	int i;

	currObject = GET_REF(e->pChar->currentTargetHandle);

	target_GetCursorRay(e, vCursorStart, vCursorDir);
	scaleAddVec3(vCursorDir,TARGET_MAX_NODE_DISTANCE,vCursorStart,vCursorEnd);

	for(i=eaSize(&e->pPlayer->InteractStatus.ppTooltipNodes)-1;i>=0;i--)
	{
		Vec3 vMin, vMax, vCollision;
		F32 dist;

		WorldInteractionNode* pNode = GET_REF(e->pPlayer->InteractStatus.ppTooltipNodes[i]->hNode);

		WorldInteractionEntry* pEntry = pNode ? wlInteractionNodeGetEntry(pNode) : NULL;

		if(pEntry==NULL)
			continue;

		wlInteractionNodeGetWorldMin(pNode,vMin);
		wlInteractionNodeGetWorldMax(pNode,vMax);

		if (!lineBoxCollision(vCursorStart, vCursorEnd, vMin, vMax, vCollision))
			continue;

		dist = distance3Squared(vCollision, vCursorStart);

		if(dist < closest)
		{
			closest = dist;
			bestObject = pNode;
			copyVec3( vCollision, vBestHit );
		}
	}

	if(!bestObject && currObject)
	{
		Vec3 vMin, vMax, vCollision;

		wlInteractionNodeGetWorldMin(currObject,vMin);
		wlInteractionNodeGetWorldMax(currObject,vMax);

		if ( lineBoxCollision( vCursorStart,vCursorEnd, vMin, vMax, vCollision ) )
		{
			bestObject = currObject;
			copyVec3( vCollision, vBestHit );
		}
	}

	if ( bestObject )
	{
		WorldInteractionEntry* pEntry = wlInteractionNodeGetEntry(bestObject);

		if(!combat_CheckLoS(PARTITION_CLIENT, vCursorStart,vBestHit,e,NULL,pEntry,0,true,NULL))
		{
			return NULL;
		}
	}

	return bestObject;
}


bool target_ClampToMinimumInteractBox( CBox* pBox )
{
	const F32 fMinScreenSize= 0.05f;
	F32 fMinWidth, fMinHeight;
	S32 iWidth, iHeight;
	bool bChanged = false;

	gfxGetActiveSurfaceSize(&iWidth, &iHeight);
	fMinWidth = iWidth * fMinScreenSize;
	fMinHeight = iHeight * fMinScreenSize;

	if (CBoxWidth(pBox) < fMinWidth)
	{
		CBoxSetX(pBox, (pBox->lx + pBox->hx - fMinWidth) / 2, fMinWidth);
		bChanged = true;
	}
	if (CBoxHeight(pBox) < fMinHeight)
	{
		CBoxSetY(pBox, (pBox->ly + pBox->hy - fMinHeight) / 2, fMinHeight);
		bChanged = true;
	}

	return bChanged;
}

//The following 2 functions return a value between 1 and 1.5 based on the distance from 0 to a clamped maximum
static F32 target_GetEntitySelectPriorityForDistSq(F32 fDistSq)
{
	F32 fDistClamp = CLAMPF32(sqrtf(fDistSq), 0, HIGH_TARGET_DIST);

	return 1.5f - fDistClamp / (HIGH_TARGET_DIST * 2);
}

static F32 target_GetEntitySelectPriorityForDist(F32 fDist)
{
	F32 fDistClamp = CLAMPF32(fDist, 0, HIGH_TARGET_DIST);
	
	return 1.5f - fDistClamp / (HIGH_TARGET_DIST * 2);
}


// -------------------------------------------------------------------------------------------------------------------------
//This function returns the world distance from the camera position
//to the center of the entity's bounding box
//result is -FLT_MAX if:
//	-the mouse does not intersect the entity's screen bounds
//	-bCheckLoS is true and the entity is not in line of sight
static F32 target_GetEntitySelectPriority(	Entity *currEnt, 
											Vec3 vStart, Vec3 vDir, 
											bool bUseCapsule, 
											bool* pbExpandedBounds,
											Vec3 vCollisionPoint,
											bool bCombatEntityTargeting)
{
	ClientReticle reticle = {0};
	CBox entBox, expBox;
	F32 fSBDistance = -1;
	bool bFarEncounter = false;
	
	
	PERFINFO_AUTO_START_FUNC();

	if (g_CombatConfig.bCamTargetingIgnoresFriendlyPlayerOwnedEntities)
	{
		Entity *pOwner = entFromEntityRef(PARTITION_CLIENT, currEnt->erOwner);
		if (pOwner && entIsPlayer(pOwner))
		{
			Entity *e = entActivePlayerPtr();
			if (e != pOwner && !critter_IsKOS(PARTITION_CLIENT, e, currEnt))
				return -FLT_MAX;
		}
	}
	
	gclReticle_GetReticle(&reticle, bCombatEntityTargeting);
	
	if (SAFE_MEMBER(currEnt->pEntUI, pGen) && SAFE_MEMBER(currEnt->pCritter, bEncounterFar))
	{
		entBox = currEnt->pEntUI->pGen->ScreenBox;
		fSBDistance = 1.0f;
		bFarEncounter = true;
	}
	else
	{
		entGetScreenBoundingBox(currEnt, &entBox, &fSBDistance, false);
	}

	if (g_bDebugDrawTargetBoxes)
		gfxDrawCBox(&entBox, 0, ARGBToColor(0xFFFF00FF));

	if ( fSBDistance > 0.01f )
	{
		bool bExpandedBounds;

		expBox.lx = entBox.lx;
		expBox.ly = entBox.ly;
		expBox.hx = entBox.hx;
		expBox.hy = entBox.hy;
		
		bExpandedBounds = target_ClampToMinimumInteractBox( &expBox );
		
		if (g_bDebugDrawTargetBoxes)
		{
			gfxDrawCBox(&expBox, 0.0f, colorFromRGBA(0x0000FFFF));
			gclReticle_DebugDraw(&reticle);
		}
		
		if (gclReticle_IsTargetInReticle(&reticle, &expBox))
		{
			F32 fBias;
						
			fBias = gclReticle_GetTargetingBias(&reticle, &expBox, bCombatEntityTargeting);
			
			if ( pbExpandedBounds )
				(*pbExpandedBounds) = bExpandedBounds;

			if (bFarEncounter)
			{
				Vec3 vTarget;
				entGetPos(currEnt,vTarget);
				vTarget[1] += 5.f;
				copyVec3(vTarget, vCollisionPoint);
				// If this is a far encounter, just return the squared distance to the target
				PERFINFO_AUTO_STOP();
				return target_GetEntitySelectPriorityForDistSq(distance3Squared(vStart, vTarget))*fBias;
			}
			// We expand bounds if the calculated bounds would be too small, and in that case just use the expanded screen space square
			if ( bExpandedBounds )
			{
				Vec3 vTarget;
				entGetPos(currEnt,vTarget);
				vTarget[1] += 5.f;
				copyVec3(vTarget, vCollisionPoint);

				//If we hit the tight-fitting bounding box, then give this entity higher priority
				if ( point_cbox_clsn( reticle.iReticlePosX, reticle.iReticlePosY, &entBox ) )
				{
					PERFINFO_AUTO_STOP();
					return target_GetEntitySelectPriorityForDistSq(distance3Squared(vStart, vTarget)) * 2.0f * fBias;
				}

				if(reticle.eReticleShape != EClientReticleShape_NONE)
				{
					Vec3 vLoc;
					Vec3 vEnd;
					F32 fPriority = 0.0f;

					scaleAddVec3(vDir, currEnt->fEntitySendDistance, vStart, vEnd);
					entGetCombatPosDir(currEnt,NULL,vLoc,NULL);

					fPriority = target_GetEntitySelectPriorityForDistSq(pointLineDistSquared(vLoc,vStart,vEnd,vCollisionPoint)) * 0.1f * fBias;
					PERFINFO_AUTO_STOP();
					return fPriority;
				}
	
				PERFINFO_AUTO_STOP();
				return target_GetEntitySelectPriorityForDistSq(distance3Squared(vStart, vTarget)) * fBias;
			}
			// If we didn't expand the bounds, do a check against the oriented bounded box in 3d
			else
			{
				Vec3 vLocalMin, vLocalMax;
				Mat4 mEntMat, mEntMatInv;
				Vec3 vEnd;

				entGetLocalBoundingBox(currEnt, vLocalMin, vLocalMax, true);
				entGetVisualMat(currEnt, mEntMat);
				invertMat4(mEntMat, mEntMatInv);

				// Find line end point
				scaleAddVec3(vDir, currEnt->fEntitySendDistance, vStart, vEnd);
				if (lineOrientedBoxCollision(vStart, vEnd, mEntMat, mEntMatInv, vLocalMin, vLocalMax, vCollisionPoint))
				{
					/* Uncomment to draw a set of axes at the point of collision for debugging
					DynTransform xIntersect;
					dynTransformClearInline(&xIntersect);
					copyVec3(vCollisionPoint, xIntersect.vPos);
					gfxDrawAxesFromTransform(&xIntersect, 1.0f);
					*/

					PERFINFO_AUTO_STOP();
					return target_GetEntitySelectPriorityForDistSq(distance3Squared(vStart, vCollisionPoint)) * 2.0f * fBias;
				}			

				if(reticle.eReticleShape != EClientReticleShape_NONE)
				{
					Vec3 vRad;
					
					vRad[0] = reticle.iReticleRadius * 0.5f;
					vRad[1] = reticle.iReticleRadius * 0.5f;
					vRad[2] = reticle.iReticleRadius * 0.5f;

					addToVec3(vRad,vLocalMax);

					subVec3(vLocalMin,vRad,vLocalMin);

					if (lineOrientedBoxCollision(vStart, vEnd, mEntMat, mEntMatInv, vLocalMin, vLocalMax, vCollisionPoint))
					{
						Vec3 vLoc;

						entGetCombatPosDir(currEnt,NULL,vLoc,NULL);
						PERFINFO_AUTO_STOP();
						return target_GetEntitySelectPriorityForDistSq(pointLineDistSquared(vLoc,vStart,vEnd,vCollisionPoint))*0.1f*fBias;
					}	
				}

			}
		}
	}

	PERFINFO_AUTO_STOP();
	return -FLT_MAX;
}

// Indicates whether the entity is a combat entity in terms of the targeting system
static bool target_EntityIsCombatEntity(SA_PARAM_NN_VALID Entity *pEnt)
{
	return pEnt->pChar &&
		!inv_HasLoot(pEnt) && 
		!interaction_FindEntityInInteractOptions(pEnt->myRef);
}

Entity * target_SelectUsingCamera(Entity *e,
								  U32 target_type_req,
								  U32 target_type_exc,
								  bool bSelectable)
{
	Mat4 cam;
	Entity *currEnt, *bestEnt = 0;
	Vec3 vCamStart, vCamDir, vPos;
	Vec3 vCollisionPoint;
	F32 fBestPriority=-FLT_MAX;
	EntityIterator * iter = entGetIteratorAllTypesAllPartitions(0,ENTITYFLAG_IGNORE|ENTITYFLAG_DONOTDRAW);

	gfxGetActiveCameraMatrix(cam);
	copyVec3(cam[3],vCamStart);
	copyVec3(cam[2],vCamDir);

	scaleVec3(vCamDir,-1,vCamDir);

	while ((currEnt = EntityIteratorGetNext(iter)))
	{
		F32 fPriority = -1.0f;
		Vec3 vCollisionPointCandidate;
		F32 fDistToOriginSqr;
		Vec3 vDirToOrigin;
		bool bTargetable = true;


		if(!g_bSelectAnyEntity)
		{
			if ( currEnt == e || !clientTarget_MatchesTypeEx(e, currEnt, NULL, target_type_req, target_type_exc) )
			{
				if (bSelectable || !inv_HasLoot(currEnt))
					// Loot critters can be interacted with, despite having no type
					continue;
			}

			if ( g_CombatConfig.bTargetDeadEnts && !entIsAlive(currEnt) && critter_IsKOS(PARTITION_CLIENT,e,currEnt) )
				continue; //Hack to not target dead foes if you can target dead ents

			if( bSelectable && !entIsSelectable(e->pChar, currEnt) ) 
				continue;

			if ( gConf.bManageOffscreenGens )
			{
				if (	!currEnt->pEntUI 
					||	(currEnt->pCritter && currEnt->pCritter->bEncounterFar && (!currEnt->pEntUI->pEncounterData || currEnt->pEntUI->pEncounterData->erEnt != entGetRef(currEnt))))
				{
					continue;
				}
			}
		}

		entGetPos(currEnt, vPos);

		subVec3( vPos,vCamStart, vDirToOrigin );
		fDistToOriginSqr = lengthVec3Squared( vDirToOrigin );

		if ( isFacingDirection( vCamDir, vDirToOrigin ) == false )
			continue;

		if(IS_HANDLE_ACTIVE(currEnt->hCreatorNode))
		{
			WorldInteractionNode *pNode;
			Vec3 vMin, vMax, vCamEnd;

			pNode = GET_REF(currEnt->hCreatorNode);

			if(!pNode)
				continue;

			wlInteractionNodeGetWorldMax(pNode,vMax);
			wlInteractionNodeGetWorldMin(pNode,vMin); 

			scaleAddVec3(vCamDir,currEnt->fEntitySendDistance,vCamStart,vCamEnd);

			//TODO: expand node bounds, or not necessary?
			if(lineBoxCollision(vCamStart,vCamEnd,vMin,vMax,vCollisionPointCandidate))
			{
				fPriority = target_GetEntitySelectPriorityForDistSq(distance3Squared(vCollisionPointCandidate, vCamStart));
			}
		}
		else
		{	
			fPriority = target_GetEntitySelectPriority(currEnt,vCamStart,vCamDir,true,NULL, vCollisionPointCandidate, target_EntityIsCombatEntity(currEnt));
		}		
		
		if (fPriority > fBestPriority)
		{
			fBestPriority = fPriority;

			copyVec3(vCollisionPointCandidate, vCollisionPoint);

			bestEnt = currEnt;
		}
	}
	EntityIteratorRelease(iter);
	// Do a line of sight test from the screen to the best collision point on the target to see if you could see the entity at that point.
	if ( bestEnt )
	{
		Vec3 vEnd, vCamEnd;

		// We don't know if the vCollisionPoint is actually on the line or just the center of the target,
		// so project it onto the line for the LOS test
		scaleAddVec3(vCamDir,bestEnt->fEntitySendDistance,vCamStart,vCamEnd);
		pointProjectOnLine( vCamStart, vCamEnd, vCollisionPoint, vEnd );

		if(!combat_CheckLoS(PARTITION_CLIENT, vCamStart,vEnd,e,bestEnt,0,0,true,NULL))
		{
			return NULL;
		}
	}
	return bestEnt;
}


static int cmpTargetEntityHeuristics(const TargetEntityPriority* lhs, const TargetEntityPriority* rhs)
{
	if (lhs->bIsInteractable && rhs->bIsInteractable)
	{
		return (lhs->fPriority < rhs->fPriority) ? 1 : -1;
	}
	else if (lhs->bIsInteractable == rhs->bIsInteractable)
	{
		return (lhs->fPriority < rhs->fPriority) ? 1 : -1;
	}
	else if (lhs->bIsInteractable)
	{
		return -1;
	}
	else
	{
		return 1;
	}
}

Entity * target_SelectUnderMouseEx(	Entity *e,
									PowerTarget *pPowerTarget, 
									U32 target_type_req,
									U32 target_type_exc,
									Vec3 worldPos,
									bool bSelectable,
									bool bIsClick,
									bool preferInteractables)
{
	static TargetEntityPriority *s_pTargetsList = NULL;
	static S32 s_iTargetsListSize = 0;
	static S32 s_iTargetsListMaxSize = 0;
	
	Entity *currEnt, *pBestEnt = NULL;

	Vec3 vCursorStart, vCursorDir, vPos;
	EntityIterator * iter = entGetIteratorAllTypesAllPartitions(0,ENTITYFLAG_IGNORE|ENTITYFLAG_DONOTDRAW);
	S32 rayHit = 0;
	Vec3 rayHitPos;
	Vec3 rayHitNormal;
	WorldCollObject* wco = NULL;
	Entity *pCurrentTargetEnt;


	PERFINFO_AUTO_START_FUNC();

	PERFINFO_AUTO_START("Early",1);

	target_GetCursorRay(e, vCursorStart, vCursorDir);

	pCurrentTargetEnt = clientTarget_GetCurrentHardTargetEntity();


	s_iTargetsListSize = 0;

#if 0
	wlAddClientLine(NULL, vCursorStart, vCursorEnd, 0xffff0000);
#endif
 	
 	if(	cmdOnClick.enabled && bIsClick)
 	{
 		WorldCollCollideResults	results;
 		Vec3						target;
 		
 		scaleAddVec3(vCursorDir, 10000, vCursorStart, target);
 		
 		rayHit = worldCollideRay(PARTITION_CLIENT, vCursorStart, target, WC_QUERY_BITS_TARGETING, &results);
 		
 		if(rayHit)
		{
 			wco = results.wco;
 			
			if(vecY(vCursorStart)<vecY(target))
			{
				results.posWorldImpact[1] -= 0.01;
			}
			else
			{
				results.posWorldImpact[1] += 0.01;
			}
 			copyVec3(results.posWorldImpact, rayHitPos);
 			copyVec3(results.normalWorld, rayHitNormal);
			if (worldPos)
				copyVec3(results.posWorldImpact, worldPos);
 		}
 	}
	else if (worldPos)
	{
		WorldCollCollideResults	results;
 		Vec3						target;
 		
 		scaleAddVec3(vCursorDir, 10000, vCursorStart, target);
 		
 		if(worldCollideRay(PARTITION_CLIENT, vCursorStart, target, WC_QUERY_BITS_TARGETING, &results))
		{
			copyVec3(results.posWorldImpact, worldPos);
			//gfxDrawSphere3D(worldPos, 0.1f, 10, ARGBToColor(0xff0000ff), 0.0f);
 		}
		else
		{
			copyVec3(target,worldPos);
		}
	}

	PERFINFO_AUTO_STOP_START("Loop",1);

	while ((currEnt = EntityIteratorGetNext(iter)))
	{
		F32 fPriority = -1.0f;
		Vec3 vCollisionPointCandidate;
		F32 fDistToOriginSqr;
		Vec3 vDirToOrigin;
		bool bTargetable = true;

	
		if(!g_bSelectAnyEntity)
		{
			if ( currEnt == e || ((pPowerTarget || target_type_req || target_type_exc) && !clientTarget_MatchesTypeEx(e, currEnt, pPowerTarget, target_type_req, target_type_exc)) )
			{
				if (bSelectable || !inv_HasLoot(currEnt))
					// Loot critters can be interacted with, despite having no type
					continue;
			}

			if (bSelectable 
				&& ((g_CombatConfig.bTargetDeadEnts && !entIsAlive(currEnt) && (critter_IsKOS(PARTITION_CLIENT,e,currEnt) && !g_CombatConfig.bTargetDeadFoes))
				|| (!g_CombatConfig.bTargetDeadEnts && !entIsAlive(currEnt)))
				&& !inv_HasLoot(currEnt))
				continue; //Hack to not target dead foes if you can target dead ents

			if( bSelectable && !entIsSelectable(e->pChar, currEnt) ) 
				continue;

			if ( gConf.bManageOffscreenGens )
			{
				if (	!currEnt->pEntUI 
					||	(currEnt->pCritter && currEnt->pCritter->bEncounterFar && (!currEnt->pEntUI->pEncounterData || currEnt->pEntUI->pEncounterData->erEnt != entGetRef(currEnt))))
				{
					continue;
				}
			}
		}
		
		entGetPos(currEnt, vPos);

		subVec3( vPos, vCursorStart, vDirToOrigin );
		fDistToOriginSqr = lengthVec3Squared( vDirToOrigin );
		
		if(rayHit && distance3Squared(vCursorStart, rayHitPos) < fDistToOriginSqr)
			continue;

		//if ( fDistToOriginSqr > SQR(currEnt->fEntitySendDistance) )
		//	continue;

		if ( isFacingDirection( vCursorDir, vDirToOrigin ) == false )
			continue;

		if(IS_HANDLE_ACTIVE(currEnt->hCreatorNode))
		{
			WorldInteractionNode *pNode;
			Vec3 vMin, vMax, vCursorEnd;

			pNode = GET_REF(currEnt->hCreatorNode);

			if(!pNode)
				continue;

			PERFINFO_AUTO_START("Dist Node",1);

			wlInteractionNodeGetWorldMax(pNode,vMax);
			wlInteractionNodeGetWorldMin(pNode,vMin); 

			scaleAddVec3(vCursorDir,currEnt->fEntitySendDistance,vCursorStart,vCursorEnd);

			//TODO: expand node bounds, or not necessary?
			if(lineBoxCollision(vCursorStart,vCursorEnd,vMin,vMax,vCollisionPointCandidate))
			{
				if ( rayHit )
				{
					fPriority = 1000.0f; //?? 
				}
				else
				{
					fPriority = target_GetEntitySelectPriorityForDistSq(distance3Squared(vCollisionPointCandidate, vCursorStart));
				}
			}
			PERFINFO_AUTO_STOP();
		}
		else
		{
			PERFINFO_AUTO_START("Dist Ent",1);
			fPriority = rayHit ? 
				target_GetEntitySelectPriorityForDist(entLineDistanceEx(vCursorStart,0,vCursorDir,currEnt->fEntitySendDistance,currEnt,vCollisionPointCandidate,true))
				: 
				target_GetEntitySelectPriority(currEnt,vCursorStart,vCursorDir,true,NULL, vCollisionPointCandidate, target_EntityIsCombatEntity(currEnt));
			PERFINFO_AUTO_STOP();
		}		
#if 0
		wlAddClientPoint(NULL, vCursorClose, 0xFFFF00FF);
#endif
		
		if (rayHit && fPriority < 1.0f)
		{
			// if cmdOnClick.enabled and the ray hit the world, and if the priority for the entity is for some reason rejected being -1
			fPriority = target_GetEntitySelectPriorityForDistSq(distance3Squared(vPos, vCursorStart));
			copyVec3(vPos, vCollisionPointCandidate);
		}


		if (fPriority != -FLT_MAX)
		{
			TargetEntityPriority* target = dynArrayAddStruct(s_pTargetsList, s_iTargetsListSize, s_iTargetsListMaxSize);
			
			// add our sticky heuristic if this entity is our current hard target
			if (currEnt == pCurrentTargetEnt)
			{	
				fPriority += g_CombatConfig.fClientMouseTargetingHardTargetStickyHeuristic;
			}

			target->pEntity = currEnt;
			target->fPriority = fPriority;
			copyVec3(vCollisionPointCandidate, target->vCollisionPoint);

			if (preferInteractables)
			{	// if we prefer interactables set this flag and on the sorting we'll always prioritize interactables
				target->bIsInteractable = !!interaction_FindEntityInInteractOptions(currEnt->myRef);
			}
		}
		
	}
	EntityIteratorRelease(iter);

	PERFINFO_AUTO_STOP_START("Late",1);
	
	// sort the list of targets
	if (s_pTargetsList && s_iTargetsListSize)
	{
		ANALYSIS_ASSUME(s_pTargetsList);
		qsort(s_pTargetsList, s_iTargetsListSize, sizeof(TargetEntityPriority), cmpTargetEntityHeuristics);
	}
	
	if(	cmdOnClick.enabled && bIsClick)
	{
		char	replaceTarget[100];
		char*	cmdBuffer = NULL;
		
		if (s_iTargetsListSize)
		{
			pBestEnt = s_pTargetsList[0].pEntity;
		}
		
		estrStackCreate(&cmdBuffer);

		if(pBestEnt && strstri(cmdOnClick.entCmd, "<ent>")){
			sprintf(replaceTarget, "%d", entGetRef(pBestEnt));
			estrCopy(&cmdBuffer, &cmdOnClick.entCmd);
			estrReplaceOccurrences(&cmdBuffer, "<ent>", replaceTarget);
		}
		else if(wco && strstri(cmdOnClick.wcoCmd, "<wco>")){
			sprintf(replaceTarget, "%"FORM_LL"u", (U64)(uintptr_t)wco);
			estrCopy(&cmdBuffer, &cmdOnClick.wcoCmd);
			estrReplaceOccurrences(&cmdBuffer, "<wco>", replaceTarget);
		}
		else if(rayHit && strstri(cmdOnClick.posCmd, "<pos>")){
			Mat3 mat;
			Vec3 pyr;
			
			estrCopy(&cmdBuffer, &cmdOnClick.posCmd);

			copyVec3(rayHitNormal, mat[1]);
			if(distance3XZ(mat[1], unitmat[1]) > 0.01f){
				crossVec3(unitmat[1], mat[1], mat[0]);
				normalVec3(mat[0]);
				crossVec3(mat[0], mat[1], mat[2]);
				normalVec3(mat[2]);
			}else{
				crossVec3(unitmat[0], mat[1], mat[2]);
				normalVec3(mat[2]);
				crossVec3(mat[1], mat[2], mat[0]);
				normalVec3(mat[0]);
			}
			getMat3YPR(mat, pyr);
			
			// Replace <pos>.
			
			sprintf(replaceTarget, "%f %f %f", vecParamsXYZ(rayHitPos));
			estrReplaceOccurrences(&cmdBuffer, "<pos>", replaceTarget);

			// Replace <normal>.

			sprintf(replaceTarget, "%f %f %f", vecParamsXYZ(rayHitNormal));
			estrReplaceOccurrences(&cmdBuffer, "<normal>", replaceTarget);

			// Replace <pyr>.
			
			sprintf(replaceTarget, "%f %f %f", vecParamsXYZ(pyr));
			estrReplaceOccurrences(&cmdBuffer, "<pyr>", replaceTarget);
		}

		if(cmdBuffer[0]){
			globCmdParse(cmdBuffer);
		}
		
		estrDestroy(&cmdBuffer);

		pBestEnt = NULL;
	}
	
	// go through all the targets we care about from best to worst and 
	// do a line of sight test 
	if ( s_iTargetsListSize )
	{
		S32 i;

		for (i = 0; i < s_iTargetsListSize; ++i)
		{
			TargetEntityPriority *pTargetPriority = &s_pTargetsList[i];

			if(!gConf.bTargetSelectUnderMouseUsesPowersLOSCheck)
			{
				Vec3 vEnd, vCursorEnd;
				// from the screen to the best collision point on the target to see if you could see the entity at that point.			
				// We don't know if the vCollisionPoint is actually on the line or just the center of the target,
				// so project it onto the line for the LOS test
				scaleAddVec3(vCursorDir, pTargetPriority->pEntity->fEntitySendDistance, vCursorStart, vCursorEnd);
				pointProjectOnLine(vCursorStart, vCursorEnd, pTargetPriority->vCollisionPoint, vEnd);

				if(gGCLState.bUseFreeCamera || combat_CheckLoS(PARTITION_CLIENT, vCursorStart, vEnd, e, pTargetPriority->pEntity, 0, 0, true, NULL))
				{
					pBestEnt = pTargetPriority->pEntity;
					break;
				}
			}
			else
			{
				Vec3 vTargetEntPos, vPlayerEnt; 
				entGetCombatPosDir(pTargetPriority->pEntity, NULL, vTargetEntPos, NULL);
				entGetCombatPosDir(e, NULL, vPlayerEnt, NULL);
				// do a LoS check that more closely simulates the power's LoS check
				if(gGCLState.bUseFreeCamera || combat_CheckLoS(PARTITION_CLIENT, vPlayerEnt, vTargetEntPos, e, pTargetPriority->pEntity, NULL, false, true, NULL))
				{
					pBestEnt = pTargetPriority->pEntity;
					break;
				}
			}
		}
	}
	PERFINFO_AUTO_STOP(); // Late
	PERFINFO_AUTO_STOP(); // FUNC
	return pBestEnt;
}

Entity * target_SelectUnderMouse(	Entity *e,
									U32 target_type_req,
									U32 target_type_exc,
									Vec3 worldPos,
									bool bSelectable,
									bool bIsClick,
									bool preferInteractables)
{
	return target_SelectUnderMouseEx(e,NULL,target_type_req,target_type_exc,worldPos,bSelectable,bIsClick,preferInteractables);
}


Entity* getEntityUnderMouse(bool bExcludePlayer)
{
	Entity *currEnt, *bestEnt = 0;
	Vec3 vCursorStart, vCursorDir, vPos;
	F32 fBestPriority = -FLT_MAX;
	Vec3 vCollisionPoint;
	EntityIterator * iter;
	Mat4 cam;
			
	Entity *entSource = entActivePlayerPtr();
	if (!entSource)
		return 0;

	iter = entGetIteratorAllTypesAllPartitions(0,ENTITYFLAG_IGNORE|ENTITYFLAG_DONOTDRAW);

	gfxGetActiveCameraMatrix(cam);
	target_GetCursorRay(entSource, vCursorStart, vCursorDir);

	while ((currEnt = EntityIteratorGetNext(iter)))
	{
		F32 fPriority;
		Vec3 vDirToOrigin;
		Vec3 vCollisionPointCandidate;

		if ( bExcludePlayer && currEnt == entSource )
			continue;

		if ( gConf.bManageOffscreenGens )
		{
			if (	!currEnt->pEntUI 
				||	(currEnt->pCritter && currEnt->pCritter->bEncounterFar && (!currEnt->pEntUI->pEncounterData || currEnt->pEntUI->pEncounterData->erEnt != entGetRef(currEnt))))
			{
				continue;
			}
		}

		entGetPos(currEnt, vPos);

		subVec3( vPos, vCursorStart, vDirToOrigin );

		if ( isFacingDirection( vCursorDir, vDirToOrigin ) == false )
			continue;

		fPriority = target_GetEntitySelectPriority(currEnt,vCursorStart,vCursorDir,true,NULL, vCollisionPointCandidate, target_EntityIsCombatEntity(currEnt));

		if (fPriority > fBestPriority)
		{
			fBestPriority = fPriority;
			bestEnt = currEnt;
			copyVec3(vCollisionPointCandidate, vCollisionPoint);
		} 
	}

	EntityIteratorRelease(iter);

	if (bestEnt)
	{
		if(!combat_CheckLoS(PARTITION_CLIENT, vCursorStart,vCollisionPoint,entSource,bestEnt,0,0,true,NULL))
		{
			return NULL;
		}
	}

	return bestEnt;
}

int wlInteractionNodeGetWindowScreenPos(WorldInteractionNode *pNode, Vec2 pixel_pos, F32 yOffsetInFeet)
{
	int		w,h;
	F32 zdist, radius;
	GfxCameraView *view = gfxGetActiveCameraView();
	Vec3 t, pos3d;
	Vec4 vViewZ;

	gfxGetActiveSurfaceSize(&w, &h);
	wlInteractionNodeGetWorldMid(pNode,t);
	radius = wlInteractionNodeGetRadius(pNode);
	t[1] += radius + yOffsetInFeet;
	mulVecMat4(t, view->frustum.viewmat, pos3d);

	if (pos3d[2] > view->frustum.znear)
		return 0;

	getMatRow(view->frustum.viewmat, 2, vViewZ);
	zdist = dotVec3(vViewZ, pos3d) + vViewZ[3];
	frustumGetScreenPosition(&view->frustum, w, h, pos3d, pixel_pos);

	if(zdist < 0 )
		return 1;
	else 
		return 0;
}
// Replaces <ent> with entity ref and <wco> with wco ref
AUTO_COMMAND ACMD_ACCESSLEVEL(0) ACMD_CATEGORY(Interface, Powers) ACMD_HIDE;
void runCmdOnTarget(ACMD_SENTENCE pcmd)
{
	Entity *e = entActivePlayerPtr();

	if(e)
	{
		char*	cmdBuffer = NULL;
		char	replaceTarget[100];

		estrStackCreate(&cmdBuffer);

		if(e->pChar->currentTargetRef && strstri(pcmd, "<ent>")){
			sprintf(replaceTarget, "%d", e->pChar->currentTargetRef);
			estrCopy2(&cmdBuffer, pcmd);
			estrReplaceOccurrences(&cmdBuffer, "<ent>", replaceTarget);
		}
		else if(IS_HANDLE_ACTIVE(e->pChar->currentTargetHandle) && strstri(pcmd, "<wco>")){
			WorldInteractionNode *pTargetObject = GET_REF(e->pChar->currentTargetHandle);
			if(pTargetObject)
			{
				sprintf(replaceTarget, "%"FORM_LL"u", (U64)(uintptr_t)pTargetObject);
				estrCopy2(&cmdBuffer, pcmd);
				estrReplaceOccurrences(&cmdBuffer, "<wco>", replaceTarget);
			}
		}

		if(cmdBuffer[0]){
			globCmdParse(cmdBuffer);
		}

		estrDestroy(&cmdBuffer);
	}
}

// Replaces <ent> with entity ref and <wco> with wco ref
AUTO_COMMAND ACMD_ACCESSLEVEL(0) ACMD_CATEGORY(Interface, Powers) ACMD_HIDE;
void runCmdOnCursor(ACMD_SENTENCE pcmd)
{
	Entity *target, *e = entActivePlayerPtr();
	WorldInteractionNode *pTargetObject = NULL;

	if(e && !mouseIsLocked())
	{
		char*	cmdBuffer = NULL;
		char	replaceTarget[100];

		estrStackCreate(&cmdBuffer);

		target = target_SelectUnderMouse(e,0,kTargetType_Self,NULL,false,false, false);
		pTargetObject = target_SelectObjectUnderMouse(e,0);

		if(target && strstri(pcmd, "<ent>")){
			ANALYSIS_ASSUME(target);
			sprintf(replaceTarget, "%d", entGetRef(target));
			estrCopy2(&cmdBuffer, pcmd);
			estrReplaceOccurrences(&cmdBuffer, "<ent>", replaceTarget);
		}
		else if(pTargetObject && strstri(pcmd, "<wco>")){
			sprintf(replaceTarget, "%"FORM_LL"u", (U64)(uintptr_t)pTargetObject);
			estrCopy2(&cmdBuffer, pcmd);
			estrReplaceOccurrences(&cmdBuffer, "<wco>", replaceTarget);
		}

		if(cmdBuffer[0]){
			globCmdParse(cmdBuffer);
		}
		
		estrDestroy(&cmdBuffer);
	}
}

AUTO_EXPR_FUNC(UIGen) ACMD_NAME("TargetIsNewSinceLastCheck");
U32 exprTargetIsNewSinceLastCheck(void)
{
	bool ret = s_bTargetIsNew;
	s_bTargetIsNew = false;
	return ret;
}

AUTO_EXPR_FUNC(UIGen) ACMD_NAME("TargetWasSetThisFrame");
bool exprTargetWasSetThisFrame(void)
{
	return s_LastTargetMs == gGCLState.totalElapsedTimeMs;
}

void cursorModeDefault_OnClickEx(bool bDown, Entity** pTargetOut, WorldInteractionNode** pNodeOut, Vec3 vLocOut)
{
	Entity *target, *e = entActivePlayerPtr();
	WorldInteractionNode *pTargetObject = NULL;
	char message[500];

	if (!bDown && !vLocOut && !pTargetOut)
		return;

	if (e && !mouseIsLocked())//devassertmsg(e, "No player found (are you outside of gclGameplay?"))
	{
		U32 iTypeReq = ( g_CombatConfig.bTargetDeadEnts ) ? 0 : kTargetType_Alive;
		U32 iTypeExclude = kTargetType_Self;

		target = target_SelectUnderMouse(e, iTypeReq, iTypeExclude, vLocOut, true, true, false);

		if (!bDown)
		{
			if (pTargetOut)
				*pTargetOut = target;
			return;
		}

		if(!cmdOnClick.enabled)
		{
			if(target)
			{
				if (timeSecondsSince2000() > e->pPlayer->lastOnClickReportTime + PLAYER_ONCLICK_REPORT_TIMEOUT)
				{
					ServerCmd_entCmdReportOnClickTarget(entGetRef(target));
					e->pPlayer->lastOnClickReportTime = timeSecondsSince2000();
				}

				clientTarget_ResetTargetChangeTimer();
				entity_SetTarget(e, entGetRef(target));
				if (pTargetOut)
					*pTargetOut = target;
				s_bTargetIsNew = true;
				s_LastTargetMs = gGCLState.totalElapsedTimeMs;
			}
			else
			{
				pTargetObject = target_SelectObjectUnderMouse(e,iBitMaskDes | iBitMaskThrow);
				if(pTargetObject)
				{
					clientTarget_ResetTargetChangeTimer();
					entity_SetTargetObject(e, wlInteractionNodeGetKey(pTargetObject));
					if (pNodeOut)
						*pNodeOut = pTargetObject;
					s_bTargetIsNew = true;
					s_LastTargetMs = gGCLState.totalElapsedTimeMs;
				}
				else if (g_CurrentScheme.bCancelTargetOnOffClick)
				{
					entity_SetTarget(e, 0);
				}
			}

			if(g_bDebugTargeting)
			{
				if(target)		
					sprintf( message, "Character Selected: %s %i", target->debugName, entGetRef(target) );
				else if(pTargetObject)
					sprintf( message, "Object Selected: %s", wlInteractionNodeGetKey(pTargetObject));
				else
					sprintf( message, "No New Target" );
				ChatLog_AddSystemMessage(message, NULL);
			}
		}
	}
}

void cursorModeDefault_OnClick(bool bDown)
{
	cursorModeDefault_OnClickEx(bDown, NULL, NULL, NULL);
}

static REF_TO(UICursor) s_hCurTargetCursor = {0};
static bool targetCursor_Change(UICursor* pCursor)
{
	if (pCursor)
	{
		SET_HANDLE_FROM_REFERENT(s_CursorDict, pCursor, s_hCurTargetCursor);
		ui_SetCursorByPointer(pCursor);
		return true;
	}
	return false;
}

// Returns the cursor set by the client targeting code
UICursor * targetCursor_GetCurrent(void)
{
	return GET_REF(s_hCurTargetCursor);
}

static bool targetCursor_UpdateCustomObjectCursor(Entity* pPlayerEnt, WorldInteractionNode* pTargetNode, bool* pbFound)
{
	if (pPlayerEnt->pPlayer)
	{
		ClientTargetCursorModeDef* pDef = &s_TargetCursorModeDef;
		TargetableNode* pNode = NULL;
		int i;
		
		for (i = eaSize(&pPlayerEnt->pPlayer->InteractStatus.ppTargetableNodes)-1; i >= 0; i--)
		{
			if (GET_REF(pPlayerEnt->pPlayer->InteractStatus.ppTargetableNodes[i]->hNode) == pTargetNode)
			{
				pNode = pPlayerEnt->pPlayer->InteractStatus.ppTargetableNodes[i];
				break;
			}
		}
		if (pNode)
		{
			for (i = eaSize(&pDef->eaObjectDefs)-1; i >= 0; i--)
			{
				ClientInteractObjectCursorDef* pObjectCursorDef = pDef->eaObjectDefs[i];
				
				if (eaFind(&pNode->eaCategories, pObjectCursorDef->pchInteractableCategory) >= 0)
				{
					InteractOption* pOption = interaction_FindNodeInInteractOptions(pTargetNode);

					(*pbFound) = true;

					if (pOption && entity_VerifyInteractTarget(PARTITION_CLIENT,
															   pPlayerEnt,NULL,
															   pTargetNode,
															   pOption->uNodeInteractDist,
															   pOption->vNodePosFallback,
															   pOption->fNodeRadiusFallback,
															   pOption->bCanPickup,
															   NULL))
					{
						return targetCursor_Change(GET_REF(pObjectCursorDef->hCursor));
					}
					return targetCursor_Change(GET_REF(pObjectCursorDef->hCursorDisabled));
				}
			}
		}
	}
	return false;
}

static bool targetCursor_UpdateCustomContactCursor(Entity* pPlayerEnt, Entity* pTargetEnt, ContactInfo* pContactInfo, bool* pbFound)
{
	if (pPlayerEnt->pPlayer)
	{
		ClientTargetCursorModeDef* pDef = &s_TargetCursorModeDef;
		int i;
		for (i = eaSize(&pDef->eaContactDefs)-1; i >= 0; i--)
		{
			ClientInteractContactCursorDef* pContactCursorDef = pDef->eaContactDefs[i];
			
			if (pContactCursorDef->eContactIndicator == pContactInfo->currIndicator)
			{
				InteractOption* pOption = interaction_FindEntityInInteractOptions(entGetRef(pTargetEnt));
			
				(*pbFound) = true;
			
				if (pOption && entity_VerifyInteractTarget(PARTITION_CLIENT,pPlayerEnt,pTargetEnt,NULL,0,0,0,false,NULL))
				{
					return targetCursor_Change(GET_REF(pContactCursorDef->hCursor));
				}
				return targetCursor_Change(GET_REF(pContactCursorDef->hCursorDisabled));
			}
		}
	}
	return false;
}

static bool targetCursor_UpdateInternal(Entity* pPlayerEnt)
{
	// This variable keeps the FX state so we know when to create/kill FX
	static TargetCursorUpdateFxState s_TargetCursorUpdateFxState = { 0 };

	ClientTargetCursorModeDef* pDef = &s_TargetCursorModeDef;
	bool bUpdated = false;
	Entity* pTargetEnt = NULL;
	WorldInteractionNode* pTargetNode = NULL;
	ContactDialog *pDialog;
	EntityRef erNewFXEnt = 0;

	// Make sure the interact override is set properly
	interactSetOverrideAtCursor(pPlayerEnt, false, &pTargetEnt, &pTargetNode);

	// If pDialog is not NULL then the player is in a contact dialog and the cursor should not change
	pDialog = SAFE_MEMBER3(pPlayerEnt, pPlayer, pInteractInfo, pContactDialog);
	
	if (pTargetEnt && !pDialog) //Entities
	{
		ContactInfo* pContactInfo;

		if (entGetFlagBits(pTargetEnt) & ENTITYFLAG_UNSELECTABLE)
		{
			// Do nothing
		}
		else if (GET_REF(pDef->hCursorLoot) && 
			!entIsAlive(pTargetEnt) && 
			inv_HasLoot(pTargetEnt) && 
			pTargetEnt->pCritter && pTargetEnt->pCritter->bIsInteractable &&
			reward_MyDrop(pPlayerEnt, pTargetEnt))
		{
			InteractOption* pOption = interaction_FindEntityInInteractOptions(entGetRef(pTargetEnt));

			// Set the FX entity
			erNewFXEnt = entGetRef(pTargetEnt);
			
			if (pOption && entity_VerifyInteractTarget(PARTITION_CLIENT,pPlayerEnt,pTargetEnt,NULL,0,0,0,false,NULL))
			{
				bUpdated = targetCursor_Change(GET_REF(pDef->hCursorLoot));
			}
			else
			{
				bUpdated = targetCursor_Change(GET_REF(pDef->hCursorLootDisabled));
			}
		}
		else if (gclEntGetIsFoe(pPlayerEnt, pTargetEnt) || gclEntGetIsPvPOpponent(pPlayerEnt, pTargetEnt))
		{
			if (entIsAlive(pTargetEnt))
			{
				bUpdated = targetCursor_Change(GET_REF(pDef->hCursorCombat));
			}
			else
			{
				bUpdated = targetCursor_Change(GET_REF(pDef->hCursorCombatDisabled));
			}
		}
		else if (entIsPlayer(pTargetEnt))
		{
			if (team_IsMember(pTargetEnt))
			{
				bUpdated = targetCursor_Change(GET_REF(pDef->hCursorPlayerGroup));
			}
			else
			{
				bUpdated = targetCursor_Change(GET_REF(pDef->hCursorPlayerSolo));
			}
		}
		else if (pContactInfo = gclEntGetContactInfoForPlayer(pPlayerEnt,pTargetEnt))
		{
			bool bFound = false;
			bUpdated = targetCursor_UpdateCustomContactCursor(pPlayerEnt,pTargetEnt,pContactInfo,&bFound);

			// Set the FX entity
			erNewFXEnt = entGetRef(pTargetEnt);

			if (!bFound)
			{
				InteractOption* pOption = interaction_FindEntityInInteractOptions(entGetRef(pTargetEnt));
				if (pOption && entity_VerifyInteractTarget(PARTITION_CLIENT,pPlayerEnt,pTargetEnt,NULL,0,0,0,false,NULL))
				{
					bUpdated = targetCursor_Change(GET_REF(pDef->hCursorInteractEntity));
				}
				else
				{
					bUpdated = targetCursor_Change(GET_REF(pDef->hCursorInteractEntityDisabled));
				}
			}
		}
	}
	else if (pTargetNode) // Objects
	{
		bool bFound = false;
		bUpdated = targetCursor_UpdateCustomObjectCursor(pPlayerEnt, pTargetNode, &bFound);

		if (!bFound)
		{
			if (wlInteractionClassMatchesMask(pTargetNode,iBitMaskDes))
			{
				bUpdated = targetCursor_Change(GET_REF(pDef->hCursorDestructible));
			}
			else
			{
				InteractOption* pOption = interaction_FindNodeInInteractOptions(pTargetNode);
				if (pOption && entity_VerifyInteractTarget(PARTITION_CLIENT,
					pPlayerEnt,NULL,
					pTargetNode,
					pOption->uNodeInteractDist,
					pOption->vNodePosFallback,
					pOption->fNodeRadiusFallback,
					pOption->bCanPickup,
					NULL))
				{
					bUpdated = targetCursor_Change(GET_REF(pDef->hCursorInteractObject));
				}
				else
				{
					bUpdated = targetCursor_Change(GET_REF(pDef->hCursorInteractObjectDisabled));
				}
			}
		}
	}
	if (!bUpdated)
	{
		if (pPlayerEnt->pPlayer->bTimeControlPause)
		{
			bUpdated = targetCursor_Change(GET_REF(pDef->hCursorPause));
		}
		if (!bUpdated)
		{
			targetCursor_Change(GET_REF(pDef->hCursorDefault));
		}
	}

	if (s_TargetCursorModeDef.pchInteractableMouseOverFX &&
		s_TargetCursorModeDef.pchInteractableMouseOverFX[0])
	{
		if (s_TargetCursorUpdateFxState.hActiveFx && 
			s_TargetCursorUpdateFxState.erActiveFxEntRef != erNewFXEnt)
		{
			// Kill the existing FX because we are no longer using the same entity
			dtFxKill(s_TargetCursorUpdateFxState.hActiveFx);

			s_TargetCursorUpdateFxState.hActiveFx = 0;
		}

		if (erNewFXEnt && s_TargetCursorUpdateFxState.hActiveFx == 0)
		{
			s_TargetCursorUpdateFxState.hActiveFx = dtAddFx(pTargetEnt->dyn.guidFxMan, s_TargetCursorModeDef.pchInteractableMouseOverFX, 
				NULL, 0, 0, 0.0f, 0, NULL, eDynFxSource_HardCoded, NULL, NULL);		
		}
		s_TargetCursorUpdateFxState.erActiveFxEntRef = erNewFXEnt;
	}

	return bUpdated;
}

void targetCursor_Update(void)
{
	Entity* pPlayerEnt = entActivePlayerPtr();
	static U32 s_uiLastElapsedTime = 0;
	static bool s_bCursorUpdatedLastTick = false;
	static bool s_bInputHandledLastTick = false;
	static bool s_bWasInEditor = false;
	const U32 uiCurrentElapsedTime = g_ui_State.totalTimeInMs;

	if(emIsEditorActive() || g_ui_State.bInUGCEditor)
		s_bWasInEditor = true;
	if(s_bWasInEditor && !emIsEditorActive() && !g_ui_State.bInUGCEditor) {
		s_bWasInEditor = false;
		ui_SetCursorByPointer(GET_REF(s_hCurTargetCursor));
	}

	if (!pPlayerEnt || 
		!pPlayerEnt->pChar || 
		!pPlayerEnt->pPlayer || 
		 pPlayerEnt->pPlayer->pCutscene ||
		 (mouseIsLocked() && !gclPlayerControl_IsAlwaysUsingMouseLookForced()))
	{
		return;
	}

	if (inpCheckHandled() || emIsEditorActive() || g_ui_State.bInUGCEditor)
	{
		if (s_bCursorUpdatedLastTick)
		{
			targetCursor_Change(GET_REF(s_TargetCursorModeDef.hCursorDefault));
			s_bCursorUpdatedLastTick = false;
		}
		if (inpCheckHandled())
		{
			s_bInputHandledLastTick = true;
		}
		return;
	}

	if (uiCurrentElapsedTime - s_uiLastElapsedTime < 100)
	{
		targetCursor_Change(GET_REF(s_hCurTargetCursor));
		return;
	}

	if (mouseDidAnything() || !s_bInputHandledLastTick)
	{
		s_bCursorUpdatedLastTick = targetCursor_UpdateInternal(pPlayerEnt);
		s_bInputHandledLastTick = false;
		s_uiLastElapsedTime = uiCurrentElapsedTime;
	}
}

static void targetCursor_Load(const char *pchPath, S32 iWhen)
{
	loadstart_printf("Loading TargetCursorMode... ");

	StructReset(parse_ClientTargetCursorModeDef, &s_TargetCursorModeDef);

	ParserLoadFiles("ui",
					"TargetCursorMode.def",
					"TargetCursorMode.bin",
					PARSER_OPTIONALFLAG,
					parse_ClientTargetCursorModeDef,
					&s_TargetCursorModeDef);

	loadend_printf(" done.");
}

AUTO_STARTUP(ClientTargeting) ASTRT_DEPS(UILib);
void ClientTargetingCursorModeLoad(void)
{
	UICursor* pDefaultCursor;

	targetCursor_Load(NULL, 0);
	FolderCacheSetCallback(FOLDER_CACHE_CALLBACK_UPDATE, "ui/TargetCursorMode.def", targetCursor_Load);

	if (pDefaultCursor = GET_REF(s_TargetCursorModeDef.hCursorDefault))
	{
		const char* pchName = ui_GetCursorName(pDefaultCursor);
		gclCursorMode_Register("TargetCursorMode", pchName, cursorModeDefault_OnClick, NULL, NULL, targetCursor_Update);
		gclCursorMode_SetDefault("TargetCursorMode");
	}
	else
	{
		gclCursorMode_Register("default", "Default", cursorModeDefault_OnClick, NULL, NULL, NULL);
		gclCursorMode_SetDefault("default");
	}
}

// Target the entity clicked on.
AUTO_COMMAND ACMD_ACCESSLEVEL(0)  ACMD_CATEGORY(Interface, Powers);
void targetCursor()
{
	cursorModeDefault_OnClick(true);
}


// Target the entity clicked on.
AUTO_COMMAND ACMD_ACCESSLEVEL(0)  ACMD_CATEGORY(Interface, Powers);
void targetCursorOrAutoAttack(void )
{
	Entity *target, *e = entActivePlayerPtr();
	WorldInteractionNode *pTargetObject = NULL;

	if (e && !mouseIsLocked())
	{
		target = target_SelectUnderMouse(e,kTargetType_Alive,kTargetType_Self, NULL, true, false, false);
		if(target)
		{
			if (entGetRef(target) == e->pChar->currentTargetRef)
			{
				if(g_CurrentScheme.eAutoAttackType != kAutoAttack_None 
					&& g_CurrentScheme.eAutoAttackType != kAutoAttack_Maintain)
				{
					gclAutoAttack_DefaultAutoAttack(1);
				}
				cmdFollowTarget();
			}
			else
			{
				clientTarget_ResetTargetChangeTimer();
				entity_SetTarget(e, entGetRef(target));
				s_bTargetIsNew = true;
			}
			s_LastTargetMs = gGCLState.totalElapsedTimeMs;
		}
		else
		{
			RegionRules* pRules = getRegionRulesFromRegionType( entGetWorldRegionTypeOfEnt(e) );
			U32 uiObjectMask = pRules && pRules->bClickablesTargetable ? 0 : iBitMaskDes | iBitMaskThrow;
			pTargetObject = target_SelectObjectUnderMouse(e, uiObjectMask);
			if(pTargetObject)
			{
				clientTarget_ResetTargetChangeTimer();
				entity_SetTargetObject(e, wlInteractionNodeGetKey(pTargetObject));
				s_bTargetIsNew = true;
				s_LastTargetMs = gGCLState.totalElapsedTimeMs;
			}
			else if (g_CurrentScheme.bCancelTargetOnOffClick)
			{
				entity_SetTarget(e, 0);
			}
		}		
	}
}



// NearDeath utility function, gets the best NearDeath Power and optionally the target
//  based on client targeting rules
U32 clientTarget_GetBestNearDeathPower(Entity *pEnt, EntityRef *pEntRefTargetOut, bool bConfineToCursor)
{
	U32 uiID = 0;
	static ClientTargetDef s_NearDeathTarget;

	if(pEnt->pChar)
	{
		int i;
		CopyClientTargetDef(&s_NearDeathTarget,NULL);
		for(i=eaSize(&pEnt->pChar->ppPowers)-1; i>=0; i--)
		{
			PowerDef *pdef = GET_REF(pEnt->pChar->ppPowers[i]->hDef);
			PowerTarget *pPowerTarget = pdef ? GET_REF(pdef->hTargetMain) : NULL;
			if(pPowerTarget && pPowerTarget->bAllowNearDeath)
			{
				Entity *pEntTarget = NULL;
				F32 fDist = 0.f;
				if (bConfineToCursor)
				{
					pEntTarget = target_SelectUnderMouseEx(pEnt, pPowerTarget, 0, 0, NULL, true, false, false);
				}
				else
				{
					ClientTargetDef *pClientTargetDef = clientTarget_SelectBestTargetForPower(pEnt,pEnt->pChar->ppPowers[i],false);
					pEntTarget = entFromEntityRefAnyPartition(pClientTargetDef->entRef);
					fDist = pClientTargetDef->fDist;
				}

				if(pEntTarget)
				{
					S32 bValid = pEntTarget && (!s_NearDeathTarget.entRef || s_NearDeathTarget.fDist >= fDist);

					if(bValid)
					{
						bValid = pEntTarget->pChar && pEntTarget->pChar->pNearDeath;
					}

					if(bValid)
					{
						GameAccountDataExtract *pExtract = entity_GetCachedGameAccountDataExtract(pEnt);
						bValid = !!character_CanQueuePower(entGetPartitionIdx(pEnt), pEnt->pChar, pEnt->pChar->ppPowers[i], 
															pEntTarget, NULL, NULL, NULL, NULL, NULL, NULL,
															pmTimestamp(0), -1, NULL, true, true, true, pExtract);
					}

					if(!bValid)
					{
						continue;
					}

					uiID = pEnt->pChar->ppPowers[i]->uiID;
					
					s_NearDeathTarget.entRef = entGetRef(pEntTarget);
					s_NearDeathTarget.fDist = fDist;
				}
			}
		}
	}

	if(pEntRefTargetOut)
	{
		*pEntRefTargetOut = s_NearDeathTarget.entRef;
	}

	return uiID;
}

// --------------------------------------------------------------------------------------------------------------------
// Auto Attack
// --------------------------------------------------------------------------------------------------------------------

// Indicates if AutoAttack is currently engaged or not.  Technically it's legal for
//  there to be a Power or Powers ready to AutoAttack, but the system to have turned
//  itself off because you switched targets, stopping holding down a button, etc.
// This makes it entirely different than a simpler AutoExec concept, which could
//  support automatically executing buffs and such.
static S32 s_bAutoAttackEnabled = false;

// ORDERED array of PowerIDs that should automatically fire when s_bAutoAttackEnabled
//  is true and a variety of other standard legality conditions are met.  The order
//  indicates priority.
static U32 *s_puiAutoAttackIDs = NULL;

// if this is set, then this is the only auto-attack power that is going to be attempted to be fired
// of the ones in the list
#define UI_AUTO_ATTACK_POWER_ID_INVALID 0xFFFFFFFF
static U32 s_uiAutoAttackPowerID = UI_AUTO_ATTACK_POWER_ID_INVALID;


// If set, this overrides the "first power in tray" behavior of ChO auto attack
static U32 s_iOverrideAutoAttackID = 0;

static U32 s_iFirstAutoAttack = false;
static U32 s_iInitialAutoAttackActivations[2] = {0};
static S32 s_iInitialAutoAttackIdx = 0;
static U32 s_iAutoAttackWarned = false;

// --------------------------------------------------------------------------------------------------------------------
bool gclAutoAttack_IsEnabled()
{
	return s_bAutoAttackEnabled;
}

// --------------------------------------------------------------------------------------------------------------------
void gclAutoAttack_SetExplicitPowerEnabledID(U32 powerID)
{
	if (g_CombatConfig.autoAttack.bUseExplicitPower)
		s_uiAutoAttackPowerID = powerID;
}

// --------------------------------------------------------------------------------------------------------------------
bool gclAutoAttack_IsExplicitPowerEnabled(U32 powerID)
{
	if (g_CombatConfig.autoAttack.bUseExplicitPower)
	{
		return (s_uiAutoAttackPowerID != UI_AUTO_ATTACK_POWER_ID_INVALID && s_uiAutoAttackPowerID == powerID);
	}
	
	return true;
}

// --------------------------------------------------------------------------------------------------------------------
bool gclAutoAttack_IsExplicitPowerSet()
{
	return (g_CombatConfig.autoAttack.bUseExplicitPower && s_uiAutoAttackPowerID != UI_AUTO_ATTACK_POWER_ID_INVALID);
}

// --------------------------------------------------------------------------------------------------------------------
// returns 0 if not set
U32 gclAutoAttack_GetExplicitPowerID()
{
	if (g_CombatConfig.autoAttack.bUseExplicitPower && s_uiAutoAttackPowerID != UI_AUTO_ATTACK_POWER_ID_INVALID)
		return s_uiAutoAttackPowerID;

	return 0;
}

// --------------------------------------------------------------------------------------------------------------------
static bool gclAutoAttack_ShouldCancelQueuedActivation(SA_PARAM_NN_VALID PowerActivation *pActivation)
{
	if (pActivation->bCommit == true)
		return false;

	{
		PowerDef *pDef = GET_REF(pActivation->hdef);
		if (pDef && pDef->fTimeCharge)
		{
			return true;
		}
	}

	if (g_CombatConfig.autoAttack.bAllowInitialAttackToFinish)
	{
		return (pActivation->uchID != s_iInitialAutoAttackActivations[0] && 
				pActivation->uchID != s_iInitialAutoAttackActivations[1]);
	}


	return true;
}

// --------------------------------------------------------------------------------------------------------------------
// Apparently AutoAttack tries to run off the uiReplacementID?  I'm not sure
//  why, since it shouldn't be necessary, but I've already re-written so much
//  of this code I'm going to leave this behavior intact for now, since it
//  seems to work.
static U32 gclAutoAttack_GetEffectiveID(SA_PARAM_NN_VALID Character *pchar, U32 uiID)
{
	if(uiID)
	{
		Power *ppow = character_FindPowerByID(pchar, uiID);

		if(ppow && ppow->uiReplacementID)
			ppow = character_FindPowerByID(pchar, ppow->uiReplacementID);

		if(ppow)
		{
			return ppow->uiID;
		}
	}

	return 0;
}

// --------------------------------------------------------------------------------------------------------------------
// Fills a static U32 EArray with PowerIDs that are legal for autoattack
//  "Legal" is perhaps a misnomer, since this is all client-side, it's
//  just the Powers that should autoattack if you turned everything on.
static U32 *gclAutoAttack_LegalIDs(SA_PARAM_NN_VALID Character *pchar)
{
	int i;
	static U32 *s_puiAutoAttackIDsLegal = NULL;
	U32 uiIDEff = 0;

	ea32ClearFast(&s_puiAutoAttackIDsLegal);

	// TODO: make this work with non-slot based characters
	//  This is the basic "first slot" check, which is the ChO-specific legal test.
	if(s_iOverrideAutoAttackID)
	{
		uiIDEff = gclAutoAttack_GetEffectiveID(pchar, s_iOverrideAutoAttackID);
	}
	else if (gConf.bAutoAttackFirstSlot)
	{
		uiIDEff = gclAutoAttack_GetEffectiveID(pchar, character_PowerSlotGetFromTray(pchar, -1, 0));
	}
	
	if(uiIDEff)
	{
		ea32Push(&s_puiAutoAttackIDsLegal, uiIDEff);
	}

	// get the list of valid powers based on slots if set on the combatConfig's autoattack config
	if (g_CombatConfig.autoAttack.piAutoAttackPowerSlots)
	{
		for (i = eaiSize(&g_CombatConfig.autoAttack.piAutoAttackPowerSlots) - 1; i >= 0; --i)
		{
			S32 slot = g_CombatConfig.autoAttack.piAutoAttackPowerSlots[i];
			if (slot >= 0)
			{
				uiIDEff = gclAutoAttack_GetEffectiveID(pchar, character_PowerSlotGetFromTray(pchar, -1, slot));
				if(uiIDEff && eaiFind(&s_puiAutoAttackIDsLegal, uiIDEff) < 0)
					ea32Push(&s_puiAutoAttackIDsLegal, uiIDEff);
			}
		}
	}


	// Fetch the AutoAttack TrayElems from the Entity's EntitySavedData
	if(pchar->pEntParent->pSaved && !g_CurrentScheme.bDisableTrayAutoAttack)
	{
		SavedTray* pTray = entity_GetActiveTray(pchar->pEntParent);
		if (pTray)
		{
			int s = eaSize(&pTray->ppAutoAttackElems);
			for(i=0; i<s; i++)
			{
				Power *ppow = entity_TrayGetPower(pchar->pEntParent, pTray->ppAutoAttackElems[i]);
				if(ppow)
				{
					uiIDEff = gclAutoAttack_GetEffectiveID(pchar, ppow->uiID);
					if(uiIDEff)
					{
						ea32PushUnique(&s_puiAutoAttackIDsLegal, uiIDEff);
					}
				}
			}
		}
	}

	// Check all Powers for AutoAttackServers
	for(i=eaSize(&pchar->ppPowers)-1; i>=0; i--)
	{
		PowerDef *pdef = GET_REF(pchar->ppPowers[i]->hDef);
		if(pdef && pdef->bAutoAttackServer)
		{
			uiIDEff = gclAutoAttack_GetEffectiveID(pchar, pchar->ppPowers[i]->uiID);
			ea32PushUnique(&s_puiAutoAttackIDsLegal, uiIDEff);
		}
	}
	return s_puiAutoAttackIDsLegal;
}

// --------------------------------------------------------------------------------------------------------------------
// Returns true if this PowerID is legal for AutoAttack.  Uses the cached calculation.
S32 gclAutoAttack_PowerIDLegal(U32 uiID)
{
	S32 i = ea32Find(&s_puiAutoAttackIDs,uiID);
	return i!=-1;
}

// --------------------------------------------------------------------------------------------------------------------
// Cleans up the current list of IDs for AutoAttack from the legal
//  list while keeping the order of the current list intact.
static void gclAutoAttack_RefreshIDs(SA_PARAM_NN_VALID Character *pchar)
{
	int i, s;
	U32 *puiLegal;
	
	puiLegal = gclAutoAttack_LegalIDs(pchar);

	// The list that we have when this function is done is a subset of the legal list, but we want to maintain order where possible

	// First, remove any non-legal Powers from the current list,
	//  keeping current list order intact
	for(i=ea32Size(&s_puiAutoAttackIDs)-1; i>=0; i--)
	{
		if(-1==ea32Find(&puiLegal, s_puiAutoAttackIDs[i]))
		{
			ea32Remove(&s_puiAutoAttackIDs, i);
		}
	}

	// Append any legal items that aren't in the current list, in
	//  the order they appear in the legal list
	s = ea32Size(&puiLegal);
	for(i=0; i<s; i++)
	{
		U32 uiIDLegal = puiLegal[i];
		if(-1==ea32Find(&s_puiAutoAttackIDs, uiIDLegal))
		{
			ea32Push(&s_puiAutoAttackIDs, uiIDLegal);
		}
	}

	// Now, remove any Powers that the character does not own from the current list,
	//  keeping current list order intact
	for(i=ea32Size(&s_puiAutoAttackIDs)-1; i>=0; i--)
	{
		U32 uiIDCurrent = s_puiAutoAttackIDs[i];
		if(!character_FindPowerByID(pchar, s_puiAutoAttackIDs[i]))
		{
			ea32Remove(&s_puiAutoAttackIDs, i);
		}
	}
}

// --------------------------------------------------------------------------------------------------------------------
void gclAutoAttack_SetOverrideID(U32 uiID)
{
	Entity *pEntity = entActivePlayerPtr();
	if (pEntity && pEntity->pChar)
	{	
		s_iOverrideAutoAttackID = uiID;
		gclAutoAttack_RefreshIDs(pEntity->pChar);
	}
}

// --------------------------------------------------------------------------------------------------------------------
U32 gclAutoAttack_GetOverrideID(void)
{
	return s_iOverrideAutoAttackID;
}

// --------------------------------------------------------------------------------------------------------------------
// Returns true if AutoAttack is currently enabled and including this PowerID
S32 gclAutoAttack_PowerIDAttacking(U32 uiID)
{
	if (s_bAutoAttackEnabled)
	{
		if (g_CombatConfig.autoAttack.bUseExplicitPower)
		{
			return (s_uiAutoAttackPowerID != UI_AUTO_ATTACK_POWER_ID_INVALID && s_uiAutoAttackPowerID == uiID);
		}

		return -1!=ea32Find(&s_puiAutoAttackIDs,uiID);
	}
	return false;
}

// --------------------------------------------------------------------------------------------------------------------
static bool gclAutoAttack_ShouldDeactivatePower(PowerDef *pPowerDef, bool bTargetSwitch)
{
	if (!bTargetSwitch)
		return true;

	if (!g_CombatConfig.autoAttack.bDeactivateTargetedMaintainsCancelOnTargetSwitch &&
		pPowerDef->eType == kPowerType_Maintained && pPowerDef->eEffectArea == kEffectArea_Character)
		return false;

	if (!g_CombatConfig.autoAttack.bDeactivateSelfTargetedMaintainsCancelOnTargetSwitch &&
		pPowerDef && pPowerDef->eType == kPowerType_Maintained)
	{
		PowerTarget *pPowerTarget = GET_REF(pPowerDef->hTargetMain);
		if (pPowerTarget && pPowerTarget->bRequireSelf)
			return false;
	}

	if (g_CombatConfig.autoAttack.bDeactivateChargePowersOnTargetSwitch)
		return true;
	

	if (pPowerDef && pPowerDef->fTimeCharge) 
		return false;

	return true;
}

// --------------------------------------------------------------------------------------------------------------------
static bool gclAutoAttack_IsActivationRelatedToPower(Power *pPower, SA_PARAM_NN_VALID PowerActivation *pAct)
{
	if (pAct && pPower->uiID == pAct->ref.uiID)
		return true;

	if (pPower->ppSubCombatStatePowers)
	{
		U32 uiID = 0;
		int iIdxSub = -1;
		S16 iLinkedIdx = -1;
		FOR_EACH_IN_EARRAY(pPower->ppSubCombatStatePowers, Power, pLinkedPower)
		{
			power_GetIDAndSubIdx(pLinkedPower, &uiID, &iIdxSub, &iLinkedIdx);
			
			if(uiID == pAct->ref.uiID && 
				(iIdxSub==-1 || iIdxSub == pAct->ref.iIdxSub) &&
				(iLinkedIdx == -1 || iLinkedIdx == pAct->ref.iLinkedSub))
				return true;
		}
		FOR_EACH_END
	}

	return false;
}

// --------------------------------------------------------------------------------------------------------------------
// Attempts to stop any current or queued instances of autoattack Activations
void gclAutoAttack_StopActivations(bool bTargetSwitch, bool bPowerSwitch)
{
	Entity *pEntity = entActivePlayerPtr();
	if(pEntity && pEntity->pChar && s_bAutoAttackEnabled)
	{
		int i,j;
		Character *pchar = pEntity->pChar;
		int iPartitionIdx = entGetPartitionIdx(pEntity);

		for(i=ea32Size(&s_puiAutoAttackIDs)-1; i>=0; i--)
		{
			// Try to finish any current activations and cancel any queued activations
			U32 uiID = s_puiAutoAttackIDs[i];
			Power *pPower;

			if (!gclAutoAttack_IsExplicitPowerEnabled(uiID))
				continue;
						
			pPower = character_FindPowerByID(pchar, uiID);
			if (!pPower)
				continue;

			if(pchar->pPowActCurrent && gclAutoAttack_IsActivationRelatedToPower(pPower, pchar->pPowActCurrent))
			{
				// We were actually in the middle of activating this, so stop by sending
				//  a deactivate (if it's not a toggle) or an activate (if it is, since
				//  that is how to deactivate them)
				PowerDef *pdef = GET_REF(pchar->pPowActCurrent->hdef);

				if (gclAutoAttack_ShouldDeactivatePower(pdef, bTargetSwitch))
				{
					if (bPowerSwitch && pchar->eChargeMode == kChargeMode_Current)
					{
						character_ActCurrentCancel(iPartitionIdx, pchar, true, false);
					}
					else
					{
						entUsePowerID((pdef && pdef->eType==kPowerType_Toggle), uiID);
					}
				}
			}
			
			// Check all active toggles
			for(j=eaSize(&pchar->ppPowerActToggle)-1; j>=0; j--)
			{
				if(pchar->ppPowerActToggle[j] && gclAutoAttack_IsActivationRelatedToPower(pPower, pchar->ppPowerActToggle[j]))
				{
					// Send an activate to deactivate the toggle
					entUsePowerID(true,uiID);
				}
			}
						
			if(pchar->pPowActQueued && gclAutoAttack_IsActivationRelatedToPower(pPower, pchar->pPowActQueued))
			{
				if (gclAutoAttack_ShouldCancelQueuedActivation(pchar->pPowActQueued))
				{
					// We had it queued, try and cancel it and send that to the server if it worked
					U8 uchActCanceled = character_ActQueuedCancel(iPartitionIdx, pchar, NULL, 0);
					if(uchActCanceled)
						ServerCmd_PowersActCancelServer(uchActCanceled, NULL);
				}
				else if (!pchar->pPowActCurrent)
				{
					PowerDef *pdef = GET_REF(pchar->pPowActQueued->hdef);
					if (gclAutoAttack_ShouldDeactivatePower(pdef, bTargetSwitch))
					{
						entUsePowerID((pdef && pdef->eType==kPowerType_Toggle), uiID);
					}
				}
			}

			if(pchar->pPowActOverflow && gclAutoAttack_IsActivationRelatedToPower(pPower, pchar->pPowActOverflow))
			{
				// We had it queued in overflow, try and cancel it and send that to the server if it worked
				U8 uchActCanceled = character_ActOverflowCancel(iPartitionIdx, pchar, NULL, 0);
				if(uchActCanceled)
					ServerCmd_PowersActCancelServer(uchActCanceled, NULL);
			}
		}
	}
}

// --------------------------------------------------------------------------------------------------------------------
// Disables AutoAttack and does some misc cleanup, informs the server
void gclAutoAttack_Disable(void)
{
	gclAutoAttack_StopActivations(false, false);
	s_bAutoAttackEnabled = false;
	s_iFirstAutoAttack = false;
	s_iAutoAttackWarned = 0;
	s_iOverrideAutoAttackID = 0;
	
	if (g_CombatConfig.autoAttack.bUseExplicitPower)
		gclAutoAttack_SetExplicitPowerEnabledID(-1);

	ServerCmd_SetAutoAttackServer(s_bAutoAttackEnabled);
}

// --------------------------------------------------------------------------------------------------------------------
// Enables AutoAttack, informs the server
static void gclAutoAttack_Enable(void)
{
	s_bAutoAttackEnabled = true;
	s_iFirstAutoAttack = true;
	s_iAutoAttackWarned = 0;
	ServerCmd_SetAutoAttackServer(s_bAutoAttackEnabled);

	{
		Entity *pPlayerEnt = entActivePlayerPtr();
		// attempt to dismount if we are mounted
		if (pPlayerEnt && pPlayerEnt->pChar)
			gclCharacter_ForceDismount(pPlayerEnt->pChar);
	}
	
}

// --------------------------------------------------------------------------------------------------------------------
// gclAutoAttack_DefaultAutoAttack <1/0>: Enable or disable auto attack
AUTO_COMMAND ACMD_ACCESSLEVEL(0) ACMD_NAME("DefaultAutoAttack");
void gclAutoAttack_DefaultAutoAttack(int enable)
{
	if(enable)
	{
		Entity *pPlayer = entActivePlayerPtr();
		if(pPlayer && pPlayer->pChar && g_CurrentScheme.eAutoAttackType != kAutoAttack_None)
		{
			int i;

			// Replace the old list with the new legal list
			ea32ClearFast(&s_puiAutoAttackIDs);
			gclAutoAttack_RefreshIDs(pPlayer->pChar);

			// Only actually enable autoattack if at least one Power has a valid target
			// TODO(JW): Not sure why this check happens
			if (g_CurrentScheme.eAutoAttackType == kAutoAttack_ToggleNoCancel)
			{
				gclAutoAttack_Enable();
			}
			else
			{			
				for(i=ea32Size(&s_puiAutoAttackIDs)-1; i>=0; i--)
				{
					Power *ppow = NULL;
					U32 uiID = s_puiAutoAttackIDs[i];
										
					if (!gclAutoAttack_IsExplicitPowerEnabled(uiID))
						continue;

					if(uiID)
					{
						ppow = character_FindPowerByID(pPlayer->pChar, uiID);
					}

					if(ppow)
					{
						if (g_CombatConfig.autoAttack.bAlwaysEnableEvenIfNoValidTarget)
						{
							gclAutoAttack_Enable();
							break;
						}
						
						if (!character_PowerRequiresValidTarget(pPlayer->pChar, GET_REF(ppow->hDef)))
						{
							// Enable AutoAttack and break
							gclAutoAttack_Enable();
							break;
						}
						else
						{
							// See if there's a target
							ClientTargetDef *pTarget = clientTarget_SelectBestTargetForPower(pPlayer,ppow,NULL);
							if(pTarget && (pTarget->entRef || GET_REF(pTarget->hInteractionNode)))
							{
								// Enable AutoAttack and break
								gclAutoAttack_Enable();
								break;
							}
						}
					}
				}
			}
		}
	}
	else
	{
		gclAutoAttack_Disable();
	}
}


// --------------------------------------------------------------------------------------------------------------------
// ToggleDefaultAutoAttack: Toggles the state of auto attack
AUTO_COMMAND ACMD_ACCESSLEVEL(0) ACMD_NAME("ToggleDefaultAutoAttack");
void gclAutoAttack_ToggleDefaultAutoAttack(void)
{
	gclAutoAttack_DefaultAutoAttack(!s_bAutoAttackEnabled);
}

// --------------------------------------------------------------------------------------------------------------------
// AUTO ATTACK END
// --------------------------------------------------------------------------------------------------------------------


// MultiExec PowerID array and stall flag
static U32* s_puiMultiExec = NULL;
static S32 s_bMultiExecStall = false;

// Adds a Power ID to the list of Powers to attempt to exec all at once
// Also enables autoattack if the Power ID is a legal one for autoattack
void clientTarget_AddMultiPowerExec(U32 uiID)
{
	clientTarget_AddMultiPowerExecEx(uiID, true, false);
}

// Internal version of the standard MultiPowerExec call.  AutoAttack enabling
//  is optional, as well as clearing the existing MultiExec list.
void clientTarget_AddMultiPowerExecEx(U32 uiID, S32 bEnableAutoAttack, S32 bClear)
{
	if(bClear)
		ea32ClearFast(&s_puiMultiExec);

	ea32PushUnique(&s_puiMultiExec,uiID);

	if(bEnableAutoAttack
		&& g_CurrentScheme.eAutoAttackType!=kAutoAttack_None
		&& !s_bAutoAttackEnabled
		&& gclAutoAttack_PowerIDLegal(uiID))
	{
		gclAutoAttack_Enable();
	}
}

Power* clientTarget_GetMultiExecPower(int index)
{
	Character* pchar = entActivePlayerPtr() ? entActivePlayerPtr()->pChar : NULL;
	if (index < ea32Size(&s_puiMultiExec) && pchar)
		return character_FindPowerByID(pchar, s_puiMultiExec[index]);

	return NULL;
}

AUTO_EXPR_FUNC(UIGen) ACMD_NAME(GetMultiExecPowerName);
const char* exprGetMultiExecPowerName(int index)
{
	Power* ppow = clientTarget_GetMultiExecPower(index);
	PowerDef* pdef = ppow ? GET_REF(ppow->hDef) : NULL;
	return pdef ? pdef->pchName : "";
}

// Returns true if the PowerID is in the MultiExec list
S32 clientTarget_IsMultiPowerExec(U32 uiID)
{
	return -1!=ea32Find(&s_puiMultiExec,uiID);
}

bool wlNodeIsVisible(WorldInteractionNode *pNode)
{
	Vec3 vBoundMin, vBoundMax, vCamSrc, vCamTarget;
	F32 radius;
	GfxCameraView *view = gfxGetActiveCameraView();

	radius = wlInteractionNodeGetRadius(pNode);

	wlInteractionNodeGetWorldMin(pNode,vBoundMin);
	wlInteractionNodeGetWorldMax(pNode,vBoundMax);

	if( !frustumCheckBoundingBox(&view->frustum, vBoundMin, vBoundMax, 0, false) )
		return 0;

	// Cast some rays to see if target is visible
	wlInteractionNodeGetWorldMid(pNode,vCamTarget);
	gfxGetActiveCameraPos(vCamSrc);

	// Base
	if(!worldCollideRay(PARTITION_CLIENT, vCamSrc, vCamTarget, WC_QUERY_BITS_TARGETING, NULL ))
		return 1;

	// Top
	vCamTarget[1] += radius;
	if(!worldCollideRay(PARTITION_CLIENT, vCamSrc, vCamTarget, WC_QUERY_BITS_TARGETING, NULL ))
		return 1;

	// Center
	vCamTarget[1] -= radius/2;
	if(!worldCollideRay(PARTITION_CLIENT, vCamSrc, vCamTarget, WC_QUERY_BITS_TARGETING, NULL ))
		return 1;

	// Right
	vCamTarget[1] -= radius/2;
	moveVinX(vCamTarget, view->frustum.cammat, radius/2);
	if(!worldCollideRay(PARTITION_CLIENT, vCamSrc, vCamTarget, WC_QUERY_BITS_TARGETING, NULL ))
		return 1;

	// Left
	moveVinX(vCamTarget, view->frustum.cammat, -radius);
	if(!worldCollideRay(PARTITION_CLIENT, vCamSrc, vCamTarget, WC_QUERY_BITS_TARGETING, NULL ))
		return 1;

	return 0;
}

static void target_debugRaycastMaterials(void)
{
	Entity* e = entActivePlayerPtr();
	Vec3 vCursorStart, vCursorDir;
	WorldCollCollideResults	results;
	Vec3 vTarget;
	char cMaterialOutput[256];
	int colors[] = {-1, -1, -1, -1};

	
	// Generate ray from mouse cursor to ~inf.
	target_GetCursorRay(e, vCursorStart, vCursorDir);
	scaleAddVec3(vCursorDir, 10000, vCursorStart, vTarget);

	// Cast ray, if it hits, print that out
	if (worldCollideRay(PARTITION_CLIENT, vCursorStart, vTarget, WC_QUERY_BITS_TARGETING, &results))
	{
		const char* pcPhysPropName = allocAddString(wcoGetPhysicalPropertyName(results.wco, results.tri.index, results.posWorldImpact));
		if (pcPhysPropName)
			sprintf(cMaterialOutput, "PhysProp - '%s'", pcPhysPropName);
		else
			sprintf(cMaterialOutput, "PhysProp - 'NONE FOUND'");
	}
	else
	{
		sprintf(cMaterialOutput, "PhysProp - NOTHING HIT");
	}
	gfxfont_PrintEx(&g_font_Sans, 30, 800, 0, 1.1, 1.1, 0, cMaterialOutput, (int)strlen(cMaterialOutput), colors);
}

AUTO_EXPR_FUNC(UIGen);
int GetMouseLookTargeting(void)
{
	return g_CurrentScheme.bShowMouseLookReticle && gclPlayerControl_IsMouseLooking();
}


AUTO_EXPR_FUNC(UIGen);
int IsMouseLookTargetingTemporarilyDisabled(void)
{
	
	return g_CurrentScheme.bShowMouseLookReticle && !gclPlayerControl_IsMouseLooking();
}

static ClientTargetDef *NextStaticTarget(void)
{
	static ClientTargetDef *s_pTargets = NULL;
	static int s_iTargets = 0;

	if(!s_pTargets)
		s_pTargets = malloc(sizeof(ClientTargetDef) * MAX_ENTITIES_PRIVATE);

	if(s_iTargets >= MAX_ENTITIES_PRIVATE)
		s_iTargets = 0;

	s_iTargets++;
	return &s_pTargets[s_iTargets-1];
}

bool clientTarget_NodeIsVisible(WorldInteractionNode *pNode)
{
	if (mapState_IsNodeHiddenOrDisabled(pNode))
		return false;

	return true;
}

F32 clientTarget_GetCamDist(Vec3 vPoint, Vec3 vColOut)
{
	Vec3 vCameraPos, vCameraRot;
	Quat qCameraRot;

	gfxGetActiveCameraPos(vCameraPos);
	gfxGetActiveCameraYPR(vCameraRot);

	PYRToQuat(vCameraRot,qCameraRot);

	quatToMat3_2(qCameraRot,vCameraRot);
	scaleVec3(vCameraRot,-1.f,vCameraRot);
	return PointLineDistSquared(vPoint,vCameraPos,vCameraRot,500,vColOut);
}

F32 clientTarget_GetCamDistTarget(ClientTargetDef *pTarget, Vec3 vColOut)
{
	Vec3 vPos;

	zeroVec3(vPos);

	if(IS_HANDLE_ACTIVE(pTarget->hInteractionNode))
	{
		Vec3 vCameraPos;
		WorldInteractionNode *pNode = GET_REF(pTarget->hInteractionNode);

		gfxGetActiveCameraPos(vCameraPos);

		if(pNode)
			wlInteractionNodeGetWorldMid(pNode,vPos);
	}
	else
	{
		Entity *pEnt = entFromEntityRefAnyPartition(pTarget->entRef);

		if(pEnt)
			entGetPos(pEnt,vPos);
	}

	return clientTarget_GetCamDist(vPos,vColOut);
}

F32 clientTarget_GetDistSquared(ClientTargetDef *pTarget, Vec3 vSource)
{
	Vec3 vPos;

	zeroVec3(vPos);

	if(IS_HANDLE_ACTIVE(pTarget->hInteractionNode))
	{
		WorldInteractionNode *pNode = GET_REF(pTarget->hInteractionNode);

		if(pNode)
		{
			wlInterationNode_FindNearestPoint(vSource,pNode,vPos);

			return distance3Squared(vSource,vPos);
		}
	}
	else if(pTarget->entRef)
	{
		Entity *pEnt = entFromEntityRefAnyPartition(pTarget->entRef);

		if(pEnt)
		{
			entGetPos(pEnt,vPos);

			return distance3Squared(vSource,vPos);
		}
	}

	return -1.f;
}

static F32 clientTarget_GetDistForSortByDir(Entity *A, WorldInteractionNode *pNode)
{
	Vec2 Loc;
	F32 mul, x, y, minDist, fDistance;
	CBox Box;
	if (!A)
		return 0.0f; // really bad things have happened
	entGetWindowScreenPos(A, Loc, 0.0f);
	mul = (Loc[0]*s_SearchDetails.analog_direction[0]) + (Loc[1]*s_SearchDetails.analog_direction[1]) + s_SearchDetails.analog_direction[2];
	//return mul;
	if (mul > 0.0f)
	{
		mul = 1.0f;
	}
	else
	{
		mul = -1.0f;
	}
	x = (Loc[0]-s_SearchDetails.screen_Loc[0]);
	y = (Loc[1]-s_SearchDetails.screen_Loc[1]);
	minDist = (x*x + y*y);

	entGetScreenBoundingBox(A, &Box, &fDistance, true);

	x = Box.lx - s_SearchDetails.screen_Loc[0];
	y = Box.ly - s_SearchDetails.screen_Loc[1];
	fDistance = (x*x + y*y);
	if (fDistance < minDist)
	{
		minDist = fDistance;
	}

	x = Box.hx - s_SearchDetails.screen_Loc[0];
	y = Box.ly - s_SearchDetails.screen_Loc[1];
	fDistance = (x*x + y*y);
	if (fDistance < minDist)
	{
		minDist = fDistance;
	}

	x = Box.lx - s_SearchDetails.screen_Loc[0];
	y = Box.hy - s_SearchDetails.screen_Loc[1];
	fDistance = (x*x + y*y);
	if (fDistance < minDist)
	{
		minDist = fDistance;
	}

	x = Box.hx - s_SearchDetails.screen_Loc[0];
	y = Box.hy - s_SearchDetails.screen_Loc[1];
	fDistance = (x*x + y*y);
	if (fDistance < minDist)
	{
		minDist = fDistance;
	}

	return mul*minDist;
	// this returns the distance of the entity's screen location from the plane equation, assuming the plane is normalized.
	// for the purposes of sorting, the plane does not need to be normalized.
}

static F32 clientTarget_GetDistForSortByCameraAimDistance(Entity *A, WorldInteractionNode *pNode)
{
	F32 fDistance;
	CBox Box;

	if (!A)
		return 0.0f; // really bad things have happened

	entGetScreenBoundingBox(A, &Box, &fDistance, true); // get the bounding box of the entity

	return fDistance;

}

static F32 clientTarget_GetDistFromCameraCenter(Entity *entTarget, WorldInteractionNode *pNode)
{
	if(entTarget)
	{
		F32 fScreenDist;		
		F32 fDistance;
		F32 fCentX, fCentY;
		CBox Box;
		S32 iScreenWidth;
		S32 iScreenHeight;

		entGetScreenBoundingBox(entTarget, &Box, &fDistance, true); // get the bounding box of the entity

		gfxGetActiveSurfaceSize(&iScreenWidth, &iScreenHeight);

		CBoxGetCenter(&Box, &fCentX, &fCentY);

		fScreenDist = SQR(iScreenWidth/2 - fCentX) + SQR(iScreenHeight/2 - fCentY);
		fScreenDist -= MAX(CBoxHeight(&Box), CBoxWidth(&Box));
		return fScreenDist;
	}

	if(pNode)
	{
		F32 fScreenDist;		
		F32 fDistance;
		F32 fCentX, fCentY;
		CBox Box;
		S32 iScreenWidth;
		S32 iScreenHeight;

		objGetScreenBoundingBox(pNode, &Box, &fDistance, true, false); // get the bounding box of the entity

		gfxGetActiveSurfaceSize(&iScreenWidth, &iScreenHeight);

		CBoxGetCenter(&Box, &fCentX, &fCentY);

		fScreenDist = SQR(iScreenWidth/2 - fCentX) + SQR(iScreenHeight/2 - fCentY);
		fScreenDist -= MAX(CBoxHeight(&Box), CBoxWidth(&Box));
		return fScreenDist;
	}

	return 0.0f;
}

F32 gclClientTarget_GetActiveWeaponRangeEx(Entity* pEnt, S32 iActiveWeaponIndex, S32 iPowerIdx)
{
	static InvBagIDs s_eBagID = InvBagIDs_None;
	F32 fActiveWeaponDistance = 0.0f;
	F32 fDefaultDistance = g_CurrentScheme.fDefaultActiveWeaponRange;
	InventoryBag* pBag;
	const InvBagDef* pBagDef;
	GameAccountDataExtract *pExtract;
	S32 i;
	
	pExtract = entity_GetCachedGameAccountDataExtract(pEnt);
	pBag = (InventoryBag*)inv_GetBag(CONTAINER_NOCONST(Entity, pEnt), s_eBagID, pExtract);

	pBagDef = invbag_def(pBag);

	if (!pEnt->pInventoryV2)
	{
		return fDefaultDistance;
	}
	if (!pBagDef || !(pBagDef->flags & InvBagFlag_ActiveWeaponBag))
	{
		for (i = eaSize(&pEnt->pInventoryV2->ppInventoryBags)-1; i >= 0; i--)
		{
			pBag = pEnt->pInventoryV2->ppInventoryBags[i];
			pBagDef = invbag_def(pBag);
			if (pBagDef && (pBagDef->flags & InvBagFlag_ActiveWeaponBag))
			{
				s_eBagID = pBag->BagID;
				break;
			}
		}
		if (i < 0)
		{
			return fDefaultDistance;
		}
	}
	
	if (pBagDef)
	{
		S32 activeSlot = invbag_GetActiveSlot(pBag, iActiveWeaponIndex);
		Item* pItem = inv_bag_GetItem(pBag, activeSlot);
		if (pItem)
		{
			if (iPowerIdx >= 0)
			{
				Power* pPowItem = item_GetPower(pItem, iPowerIdx);
				PowerDef* pPowDef = pPowItem ? GET_REF(pPowItem->hDef) : NULL;
				if (pPowDef)
				{
					fActiveWeaponDistance = power_GetRange(pPowItem, pPowDef);
				}
			}
			else
			{
				for (i = item_GetNumItemPowerDefs(pItem, true)-1; i >= 0; i--)
				{
					Power* pPowItem = item_GetPower(pItem, i);
					PowerDef* pPowDef = pPowItem ? GET_REF(pPowItem->hDef) : NULL;
					F32 fRange = pPowDef ? power_GetRange(pPowItem, pPowDef) : 0.f;

					if (fRange > fActiveWeaponDistance)
					{
						fActiveWeaponDistance = fRange;
					}
				}
			}
			return fActiveWeaponDistance;
		}
	}
	return fDefaultDistance;
}

static F32 clientTarget_GetDistFromScreenSide(Entity *entTarget, WorldInteractionNode *pNode)
{
	if(entTarget)
	{
		F32 fDistance;
		CBox Box;
		S32 iScreenWidth;
		S32 iScreenHeight;
		F32 fCentX, fCentY;

		gfxGetActiveSurfaceSize(&iScreenWidth, &iScreenHeight);

		entGetScreenBoundingBox(entTarget, &Box, &fDistance, true); // get the bounding box of the entity

		CBoxGetCenter(&Box, &fCentX, &fCentY);

		// Weird mapping to match up with old command
		if (s_SearchDetails.analog_direction[0] > 0)
			return fCentX;
		else if (s_SearchDetails.analog_direction[0] < 0)
			return iScreenWidth - fCentX;
		else if (s_SearchDetails.analog_direction[1] > 0)
			return fCentY;
		else
			return iScreenHeight - fCentY;

	}

	if(pNode)
	{
		F32 fDistance;
		CBox Box;
		S32 iScreenWidth;
		S32 iScreenHeight;
		F32 fCentX, fCentY;

		gfxGetActiveSurfaceSize(&iScreenWidth, &iScreenHeight);

		objGetScreenBoundingBox(pNode, &Box, &fDistance, true, false); // get the bounding box of the entity

		CBoxGetCenter(&Box, &fCentX, &fCentY);

		// Weird mapping to match up with old command
		if (s_SearchDetails.analog_direction[0] > 0)
			return fCentX;
		else if (s_SearchDetails.analog_direction[0] < 0)
			return iScreenWidth - fCentX;
		else if (s_SearchDetails.analog_direction[1] > 0)
			return fCentY;
		else
			return iScreenHeight - fCentY;

	}

	return 0.0f;
}

int clientTarget_SortByDist(const ClientTargetDef **DefA, const ClientTargetDef **DefB)
{
	F32 f;
	if ( (*DefB)->bIsOffscreen )
		return -1;
	if ( (*DefA)->bIsOffscreen )
		return 1;
	f = (*DefA)->fDist - (*DefB)->fDist;
	return f<0 ? -1 : (f>0 ? 1 : 0);
}

static int clientTarget_SortBySortDist(const ClientTargetDef **DefA, const ClientTargetDef **DefB)
{
	F32 f;
	if ( (*DefB)->bIsOffscreen )
		return -1;
	if ( (*DefA)->bIsOffscreen )
		return 1;
	f = (*DefA)->fSortDist - (*DefB)->fSortDist;
	if (ABS(f) > FLT_EPSILON)
	{
		return f<0 ? -1 : (f>0 ? 1 : 0);
	}
	f = (*DefA)->fDist - (*DefB)->fDist;
	return f<0 ? -1 : (f>0 ? 1 : 0);
}

static int clientTarget_SortByLuckyCharms(const ClientTargetDef **DefA, const ClientTargetDef **DefB)
{
	F32 f;
	int indexDelta = (*DefA)->luckyCharmIndex - (*DefB)->luckyCharmIndex;
	f = (*DefA)->fSortDist - (*DefB)->fSortDist;
	if (indexDelta == 0 && (((*DefA)->luckyCharmType > 0) == ((*DefB)->luckyCharmType > 0)))
	{
		if (ABS(f) > FLT_EPSILON)
		{
			return f<0 ? -1 : (f>0 ? 1 : 0);
		}
		f = (*DefA)->fDist - (*DefB)->fDist;
		return f<0 ? -1 : (f>0 ? 1 : 0);
	}

	if ((*DefA)->luckyCharmType <= 0)
		return 1;
	if ((*DefB)->luckyCharmType <= 0)
		return -1;

	return indexDelta<0 ? -1 : (indexDelta>0 ? 1 : 0);

}

F32 wlGetWindowScreenPosAndDist(WorldInteractionNode *pNode, Vec2 pixel_pos, F32 yOffsetInFeet)
{
	int		w,h;
	GfxCameraView *view = gfxGetActiveCameraView();
	Vec3 t, pos3d;

	gfxGetActiveSurfaceSize(&w, &h);
	wlInteractionNodeGetWorldMid(pNode,t);
	t[1] += yOffsetInFeet;
	mulVecMat4(t, view->frustum.viewmat, pos3d);
	frustumGetScreenPosition(&view->frustum, w, h, pos3d, pixel_pos);
	return view->frustum.znear - pos3d[2];
}

bool clientTarget_InteractionNodeGetWindowScreenPos(WorldInteractionNode *pNode, Vec2 pixel_pos, F32 yOffsetInFeet)
{

	S32 w, h, z;
	z = wlGetWindowScreenPosAndDist(pNode, pixel_pos, yOffsetInFeet);
	gfxGetActiveSurfaceSize(&w, &h);
	return (z > 0) && (pixel_pos[0] < w && pixel_pos[0] > 0) && (pixel_pos[1] < h && pixel_pos[1] > 0);
}

int clientTarget_IsOnScreenCameraTarget(Entity *entTarget, WorldInteractionNode *pNode)
{
	if(entTarget)
	{
		F32 fDistance;
		CBox Box;
		CBox ScreenBox = {0, 0, 0, 0};
		S32 iScreenWidth;
		S32 iScreenHeight;

		entGetScreenBoundingBox(entTarget, &Box, &fDistance, true); // get the bounding box of the entity

		gfxGetActiveSurfaceSize(&iScreenWidth, &iScreenHeight);
		ScreenBox.hx = (iScreenWidth+s_iBoxWidth)/2;
		ScreenBox.hy = (iScreenHeight+s_iBoxHeight)/2;
		ScreenBox.lx = ScreenBox.hx-s_iBoxWidth;
		ScreenBox.ly = ScreenBox.hy-s_iBoxHeight; // making a tiny box at the center of the screen
		if (CBoxIntersects(&ScreenBox, &Box) && (fDistance >= 0.1))
			return true;
	}

	if(pNode)
	{
		F32 fDistance;
		CBox Box;
		CBox ScreenBox = {0, 0, 0, 0};
		S32 iScreenWidth;
		S32 iScreenHeight;

		objGetScreenBoundingBox(pNode, &Box, &fDistance, true, false); // get the bounding box of the entity

		gfxGetActiveSurfaceSize(&iScreenWidth, &iScreenHeight);
		ScreenBox.hx = iScreenWidth/2;
		ScreenBox.hy = iScreenHeight/2;
		ScreenBox.lx = ScreenBox.hx-1;
		ScreenBox.ly = ScreenBox.hy-1; // making a tiny box at the center of the screen
		if (CBoxIntersects(&ScreenBox, &Box) && (fDistance >= 0.1))
			return true;
	}

	return false;
}
int clientTarget_IsOnScreenBoxCheck(Entity *entTarget, WorldInteractionNode *pNode)
{
	if(entTarget || pNode)
	{
		F32 fDistance;
		CBox targetBox;
		CBox screenBox = {0, 0, 0, 0};
		S32 iWidth, iHeight;
		gfxGetActiveSurfaceSize(&iWidth, &iHeight);
		screenBox.hx = iWidth; screenBox.hy = iHeight;

		if(entTarget && !gbNoGraphics)
		{
			if (entGetScreenBoundingBox(entTarget, &targetBox, &fDistance, true)) // get the bounding box of the entity
			{
				if(CBoxIntersects(&screenBox, &targetBox) && (fDistance >= 0.1))
					return true;
			}
		}

		if(pNode && !gbNoGraphics)
		{
			objGetScreenBoundingBox(pNode, &targetBox, &fDistance, true, false); // get the bounding box of the node
			if(CBoxIntersects(&screenBox, &targetBox) && (fDistance >= 0.1))
				return true;
		}
	}

	return false;
}

int clientTarget_IsMouseOverBoxCheck(Entity *entTarget, WorldInteractionNode *pNode)
{
	if(gbNoGraphics)
	{
		return false;
	}

	if(entTarget)
	{
		F32 fDistance;
		CBox Box;
		int mx, my;
		mousePos(&mx, &my);

		entGetScreenBoundingBox(entTarget, &Box, &fDistance, true); // get the bounding box of the entity

		if (point_cbox_clsn(mx, my, &Box))
			return true;
	}

	if(pNode)
	{
		F32 fDistance;
		CBox Box;
		int mx, my;
		mousePos(&mx, &my);

		objGetScreenBoundingBox(pNode, &Box, &fDistance, true, false); // get the bounding box of the entity

		if (point_cbox_clsn(mx, my, &Box))
			return true;
	}

	return false;
}

int clientTarget_IsOnScreen(Entity *entTarget, WorldInteractionNode *pNode)
{
	Vec2 vScreenPos;

	if(entTarget && !gbNoGraphics)
	{
		return entGetWindowScreenPos(entTarget,vScreenPos,0);
	}

	if(pNode && !gbNoGraphics)
	{
		return clientTarget_InteractionNodeGetWindowScreenPos(pNode,vScreenPos,0);
	}

	return false;
}

int clientTarget_IsSameTarget(const ClientTargetDef *pDefA, const ClientTargetDef *pDefB)
{
	if(pDefA == pDefB)
		return true;

	if(pDefA->entRef || pDefB->entRef)
	{
		if(pDefA->entRef == pDefB->entRef)
			return true;

		return false;
	}

	return GET_REF(pDefA->hInteractionNode) == GET_REF(pDefB->hInteractionNode);
}

#define NON_THREATENING_PENALTY 1000000.f
#define OBJECT_PENALTY 2000000.f

//Gets all valid ent targets, returns the number of targets, and a list of clientTargetDefs in order of range
int clientTarget_FindAllValidTargets(Entity *e, PowerTarget *pPowerTarget, U32 target_type_req, U32 target_type_exc, ClientTargetDef ***pppTargetsOut, ClientTargetVisibleCheck fVisibleCheckFunc, ClientTargetSortDistCheck fSortDistFunc, ClientTargetSortFunc fSortFunc, ClientTargetFilterCheck cbFilterFunc, bool bTargetAllObjects, bool bCheckOffscreen, bool bSoft)
{
	EntityIterator *iter;
	Entity *currEnt;
	F32 fActiveWeaponRange = 0.0f;

	int i;
	int iTargets=0;

	Vec3 vEntPos;

	entGetPos(e,vEntPos);

	if (g_CurrentScheme.bCheckActiveWeaponRangeForTargeting)
	{
		fActiveWeaponRange = gclClientTarget_GetActiveWeaponRange(e);
	}

	//Get all ents
	iter = entGetIteratorAllTypesAllPartitions(0, gbNoGraphics ? ENTITYFLAG_IGNORE : ENTITYFLAG_IGNORE|ENTITYFLAG_DONOTDRAW);
	while ((currEnt = EntityIteratorGetNext(iter)))
	{
		F32 fDist, fSortDist;
		ClientTargetDef *pNewDef = NULL;
		WorldInteractionNode *pCreatorNode = NULL;
		bool bIsNormalObject = false;
		bool bIsOffscreen = false;

		if(pPowerTarget)
		{
			if(!character_TargetMatchesPowerType(PARTITION_CLIENT, e->pChar, currEnt->pChar, pPowerTarget) && !g_bSelectAnyEntity)
				continue;
		}
		else
		{
			if(!character_TargetMatchesType(PARTITION_CLIENT, e->pChar, currEnt->pChar, target_type_req, target_type_exc) && !g_bSelectAnyEntity)
				continue;
		}

		if(!entIsSelectable(e->pChar, currEnt) && !g_bSelectAnyEntity)
			continue;

		if(currEnt->erOwner && g_CurrentScheme.bNeverTargetPets)
			continue;

		if(cbFilterFunc && !cbFilterFunc(currEnt, NULL))
			continue;

		if ( gConf.bManageOffscreenGens )
		{
			if (	!currEnt->pEntUI 
				||	(currEnt->pCritter && currEnt->pCritter->bEncounterFar && (!currEnt->pEntUI->pEncounterData || currEnt->pEntUI->pEncounterData->erEnt != entGetRef(currEnt))))
			{
				continue;
			}
		}

		bIsOffscreen = fVisibleCheckFunc && !fVisibleCheckFunc(currEnt,NULL);

		if(!bCheckOffscreen && bIsOffscreen)
			continue;

		//if(!entIsVisible(currEnt) && !g_bSelectAnyEntity) // Remove isVisibleCheck, and do it after sorting
		//	continue;

		if(pCreatorNode = GET_REF(currEnt->hCreatorNode))
		{
			Vec3 vTargetPos;
			character_FindNearestPointForObject(e->pChar,NULL,pCreatorNode,vTargetPos,true);
			fDist = entGetDistance(e, NULL, NULL, vTargetPos, NULL);

			if (!wlInteractionCanTabSelectNode(pCreatorNode))
				bIsNormalObject = true;
			if (bIsNormalObject && !bTargetAllObjects)
				continue;
			if (g_CurrentScheme.bNeverTargetObjects)
				continue;
		}
		else
		{
			fDist = entGetDistance(e, NULL, currEnt, NULL, NULL);
		}

		if (g_CurrentScheme.bCheckActiveWeaponRangeForTargeting)
		{
			if (fDist > fActiveWeaponRange)
			{
				continue;
			}
		}
		if (g_CurrentScheme.fTargetMaxAngleFromPlayerFacing > FLT_EPSILON)
		{
			Vec3 vTargetPos, vTargetDir;
			Vec2 vPlayerPY;
			F32 fTargetYaw;
			entGetFacePY(e, vPlayerPY);
			entGetPos(currEnt, vTargetPos);
			subVec3(vTargetPos, vEntPos, vTargetDir);
			fTargetYaw = getVec3Yaw(vTargetDir);
			fTargetYaw = subAngle(fTargetYaw, vPlayerPY[1]);
			if (ABS(fTargetYaw) > RAD(g_CurrentScheme.fTargetMaxAngleFromPlayerFacing))
			{
				continue;
			}
		}

		if (fSortDistFunc)
		{
			fSortDist = fSortDistFunc(currEnt, NULL);
		}
		else
		{
			fSortDist = fDist;
		}
		if (fSortDist < 0.0f)
		{
			continue;
		}
		pNewDef = NextStaticTarget();

		pNewDef->entRef = entGetRef(currEnt);
		REMOVE_HANDLE(pNewDef->hInteractionNode);
		COPY_HANDLE(pNewDef->hInteractionNode, currEnt->hCreatorNode);

		pNewDef->fDist = fDist;
		pNewDef->bIsOffscreen = bIsOffscreen;
		pNewDef->fSortDist = fSortDist;
		entGetLuckyCharmInfo(e, currEnt, &pNewDef->luckyCharmType, &pNewDef->luckyCharmIndex);

		if (g_CurrentScheme.bTargetObjectsLast && bIsNormalObject)
		{
			pNewDef->fSortDist += OBJECT_PENALTY;
		}
		else if (g_CurrentScheme.bTargetThreateningFirst && !character_IsLegalTarget(currEnt->pChar, entGetRef(e)))
		{
			pNewDef->fSortDist += NON_THREATENING_PENALTY;
		}

		if(pppTargetsOut)
			eaPush(pppTargetsOut,pNewDef);

		iTargets++;
	}
	EntityIteratorRelease(iter);

	if(g_CurrentScheme.bNeverTargetObjects || !e->pPlayer)
	{
		if(fSortFunc && pppTargetsOut)
			eaQSort(*pppTargetsOut,fSortFunc);

		return iTargets;
	}

	//Get all destructible objects
	for(i=0;i<eaSize(&e->pPlayer->InteractStatus.ppTargetableNodes);i++)
	{
		F32 fDist, fSortDist;
		Vec3 vObjPos;
		ClientTargetDef *pNewDef = NULL;
		bool bNormalObject = true;
		bool bIsOffscreen = false;

		WorldInteractionNode* pNode = GET_REF(e->pPlayer->InteractStatus.ppTargetableNodes[i]->hNode);

		if ( pNode==NULL )
			continue;

		if(!clientTarget_NodeIsVisible(pNode))
			continue;

		if(cbFilterFunc && !cbFilterFunc(NULL, pNode))
			continue;

		bIsOffscreen = fVisibleCheckFunc && !fVisibleCheckFunc(NULL,pNode);

		if(!bCheckOffscreen && bIsOffscreen)
			continue;

		if ( !wlInteractionCanTabSelectNode(pNode))
		{
			if (!bTargetAllObjects)
				continue;
		}
		else
		{
			bNormalObject = false;
		}

		if ( !wlInteractionClassMatchesMask( pNode, (iBitMaskDes | iBitMaskThrow) ) )
			continue;

		character_FindNearestPointForObject(e->pChar,NULL,pNode,vObjPos,true);
		fDist = entGetDistance(e, NULL, NULL, vObjPos, NULL);
		
		if (g_CurrentScheme.bCheckActiveWeaponRangeForTargeting)
		{
			if (fDist > fActiveWeaponRange)
			{
				continue;
			}
		}
		if (g_CurrentScheme.fTargetMaxAngleFromPlayerFacing > FLT_EPSILON)
		{
			Vec3 vTargetDir;
			Vec2 vPlayerPY;
			F32 fTargetYaw;
			entGetFacePY(e, vPlayerPY);
			subVec3(vObjPos, vEntPos, vTargetDir);
			fTargetYaw = getVec3Yaw(vTargetDir);
			fTargetYaw = subAngle(fTargetYaw, vPlayerPY[1]);
			if (ABS(fTargetYaw) > RAD(g_CurrentScheme.fTargetMaxAngleFromPlayerFacing))
			{
				continue;
			}
		}

		if (fSortDistFunc)
		{
			fSortDist = fSortDistFunc(NULL, pNode);
		}
		else
		{
			fSortDist = fDist;
		}
		if (fSortDist < 0.0f)
		{
			continue;
		}

		pNewDef = NextStaticTarget();
		pNewDef->entRef = 0;
		SET_HANDLE_FROM_REFERENT(INTERACTION_DICTIONARY, pNode, pNewDef->hInteractionNode);

		pNewDef->fDist = fDist;
		pNewDef->fSortDist = fSortDist;
		pNewDef->bIsOffscreen = bIsOffscreen;

		if (g_CurrentScheme.bTargetObjectsLast && bNormalObject)
		{
			pNewDef->fSortDist += OBJECT_PENALTY;
		}
		else if (g_CurrentScheme.bTargetThreateningFirst)
		{
			pNewDef->fSortDist += NON_THREATENING_PENALTY;
		}


		if(pppTargetsOut)
			eaPush(pppTargetsOut,pNewDef);

		iTargets++;
	}

	if(fSortFunc && pppTargetsOut)
	{
		eaQSort(*pppTargetsOut,fSortFunc);
	}

	return iTargets;
}

// If you target threatening targets first, this delays retargeting if your target was cancelled
//  because it died (or similar, checked against s_LastDeadTargetChange)
#define NON_THREATENING_DELAY 1

static ClientTargetDef *ClientTargetFindSoftTarget(PowerTarget *pPowerTarget, U32 target_type_req, U32 target_type_exc, bool bAllObjectsValid, bool bCheckOffscreen, F32 fMinRange)
{
	Entity *pEntPlayer = entActivePlayerPtr();
	static ClientTargetDef **s_ppTargets = NULL;
	ClientTargetVisibleCheck pVisCheck = NULL;
	ClientTargetSortFunc pSortCheck = NULL;
	ClientTargetSortDistCheck pSortDistCheck = NULL;
	int iCntTargets;

	eaClear(&s_ppTargets);

	pVisCheck = clientTarget_IsOnScreenBoxCheck;

	switch (g_CurrentScheme.eInitialTargetOrder)
	{
	xcase kTargetOrder_NearToFar:
		pSortCheck = clientTarget_SortBySortDist;
	xcase kTargetOrder_LeftToRight:
		pSortDistCheck = clientTarget_GetDistFromScreenSide;
		pSortCheck = clientTarget_SortBySortDist;
		s_SearchDetails.analog_direction[0] = 1;
		s_SearchDetails.analog_direction[1] = 0;
	xcase kTargetOrder_NearestToCenter:
		pSortDistCheck = clientTarget_GetDistFromCameraCenter;
		pSortCheck = clientTarget_SortBySortDist;
	xcase kTargetOrder_OnlyCenter:
		pSortDistCheck = clientTarget_GetDistForSortByCameraAimDistance;
		pVisCheck = clientTarget_IsOnScreenCameraTarget;
		pSortCheck = clientTarget_SortBySortDist;
	xcase kTargetOrder_LuckyCharms:
		pSortCheck = clientTarget_SortByLuckyCharms;
	xdefault:
		assertmsgf(0,"Invalid TargetType %d",g_CurrentScheme.eInitialTargetOrder);
	}

	if(iCntTargets = clientTarget_FindAllValidTargets(pEntPlayer,pPowerTarget,target_type_req,target_type_exc,&s_ppTargets,pVisCheck, pSortDistCheck, pSortCheck, NULL, bAllObjectsValid, bCheckOffscreen, true))
	{
		int i;
		U32 time = timeSecondsSince2000();
		for(i=0; i < iCntTargets;i++)
		{			
			if(clientTarget_IsTargetInLoS(pEntPlayer, NULL, entFromEntityRefAnyPartition(s_ppTargets[i]->entRef), GET_REF(s_ppTargets[i]->hInteractionNode)) &&
				(fMinRange <= 0 || s_ppTargets[i]->fDist < fMinRange) && 
				(!g_CurrentScheme.bTargetThreateningFirst || s_ppTargets[i]->fSortDist < NON_THREATENING_PENALTY || time >= s_LastDeadTargetChange + NON_THREATENING_DELAY))
			{
				clientTarget_ResetTargetChangeTimer();
				return s_ppTargets[i];
			}
		}
	}

	return NULL;
}

const ClientTargetDef *clientTarget_FindNearest(Entity *e, U32 target_type_req, U32 target_type_exc, float arc, bool bForwardArc, bool bRotArc90, bool bBothArcs)
{
	bool bCheckOffscreen = g_CurrentScheme.bTabTargetOffscreen;
	int iCntTargets;
	static ClientTargetDef **s_ppTargets = NULL;
	eaClear(&s_ppTargets);

	if(iCntTargets = clientTarget_FindAllValidTargets(e,NULL,target_type_req,target_type_exc,&s_ppTargets,clientTarget_IsOnScreenBoxCheck, NULL, clientTarget_SortBySortDist, NULL, true, bCheckOffscreen, false))
	{
		int i;

		arc = CLAMP(arc,0,360);
		if (arc < 360)
		{
			arc = cosf(RAD(arc/2.0f));
		}

		for(i=0; i < iCntTargets;i++)
		{			
			if(clientTarget_IsTargetInLoS(e, NULL, entFromEntityRefAnyPartition(s_ppTargets[i]->entRef), GET_REF(s_ppTargets[i]->hInteractionNode)) )
			{
				if (arc == 360)
				{
					return s_ppTargets[i];
				}
				else
				{
					if (clientTarget_CheckAngleArc(e, s_ppTargets[i], arc, bForwardArc, bRotArc90, bBothArcs))
					{
						return s_ppTargets[i];
					}
				}
			}
		}
	}

	return NULL;
}


static const ClientTargetDef *clientTarget_FindBestInRange(	Entity *e, 
															U32 target_type_req, 
															U32 target_type_exc, 
															F32 fMaxAngle, 
															F32 fMaxDistance, 
															F32 fDistanceBias,
															const Vec3 vDirection)
{
	static ClientTargetDef **s_ppTargets = NULL;
	int iCntTargets;

	eaClear(&s_ppTargets);

	fDistanceBias = CLAMP(fDistanceBias, 0.f, 1.f);

	iCntTargets = clientTarget_FindAllValidTargets(e, NULL, target_type_req, target_type_exc,
													&s_ppTargets, clientTarget_IsOnScreenBoxCheck, NULL, 
													clientTarget_SortBySortDist, NULL,
													true, true, false);
	if(iCntTargets > 0)
	{
		int i;
		F32 fBestHeuristic = FLT_MAX;
		ClientTargetDef *pBestTarget = NULL;
		
		fMaxAngle = CLAMP(fMaxAngle, 0.f, 360.0f);
		fMaxAngle = RAD(fMaxAngle * 0.5f);

		for(i=0; i < iCntTargets;i++)
		{		
			ClientTargetDef *pTarget = s_ppTargets[i];
			Entity *pTargetEnt;
			if (pTarget->fDist > fMaxDistance)
				break;

			pTargetEnt = entFromEntityRefAnyPartition(pTarget->entRef);

			if(clientTarget_IsTargetInLoS(e, NULL, pTargetEnt, GET_REF(pTarget->hInteractionNode)) )
			{
				if (pTargetEnt)
				{
					F32 fAngleDiff = PI;
					
					if (clientTarget_IsTargetWithinDirAngleArc(e, pTarget, vDirection, fMaxAngle, &fAngleDiff))
					{
						F32 fHeuristic = (1.f - fDistanceBias) * (fAngleDiff/fMaxAngle) +
											fDistanceBias * (pTarget->fDist/fMaxDistance);
						if (fHeuristic < fBestHeuristic)
						{
							fBestHeuristic = fHeuristic;
							pBestTarget = pTarget;
						}
					}
				}
			}
			
		}

		return pBestTarget;
	}

	return NULL;
}

// --------------------------------------------------------------------------------------------------------------------
Entity* clientTarget_FindProximityTargetingAssistEnt(Entity *pPlayerEnt, const Vec3 vDirection)
{
	// check if we should not ignore the hard target, 
	if (!g_CombatConfig.bCameraTargetingVecTargetAssistIgnoreHardTarget)
	{
		Entity *eTarget = entity_GetTarget(pPlayerEnt);
		if (eTarget)
		{
			return eTarget;
		}
	}
	
	
	{
		const ClientTargetDef *pTarget;
		Vec3 vCameraDirection;
		F32 fTargetAssistDistBias = g_CombatConfig.fCameraTargetingVecTargetAssistDistBias;

		GfxCameraController* pCamera = gfxGetActiveCameraController();
		gclCamera_GetFacingDirection(pCamera, false, vCameraDirection);

		pTarget = clientTarget_FindBestInRange(	pPlayerEnt, 
												kTargetType_Alive|kTargetType_Foe, kTargetType_NearDeath, 
												g_CombatConfig.fCameraTargetingVecTargetAssistAngle,
												g_CombatConfig.fCameraTargetingVecTargetAssistDist,
												fTargetAssistDistBias,	
												vDirection);
		if (pTarget)
		{
			return entFromEntityRefAnyPartition(pTarget->entRef);
		}
	}

	return NULL;
}

// --------------------------------------------------------------------------------------------------------------------
void clientTarget_SetCameraTargetingUsesDirectionKeysOverride(S32 bEnabled, S32 bUseDirectionalKeys)
{
	s_ClientTargetingData.bDirectionKeysOverrideSet = !!bEnabled;
	if (s_ClientTargetingData.bDirectionKeysOverrideSet)
	{
		s_ClientTargetingData.bCameraTargetingUsesDirectionKeysOverride = bUseDirectionalKeys;
	}
	else
	{
		s_ClientTargetingData.bCameraTargetingUsesDirectionKeysOverride = 0;
	}
}

// --------------------------------------------------------------------------------------------------------------------
static S32 clientTarget_CameraTargetingUsesDirectionKeys()
{
	if (s_ClientTargetingData.bDirectionKeysOverrideSet)
		return s_ClientTargetingData.bCameraTargetingUsesDirectionKeysOverride;

	return g_CurrentScheme.bCameraTargetingUsesDirectionKeys;
}

// --------------------------------------------------------------------------------------------------------------------
void clientTarget_GetVecTargetingDirection(Entity *pPlayerEnt, SA_PARAM_OP_VALID PowerDef *pdef, Vec3 vDirectionOut)
{
	if (!g_CombatConfig.bCameraTargetingGetsPlayerFacing && 
		((pdef && !pdef->bUseCameraTargetingVecTargetAssist) || !clientTarget_CameraTargetingUsesDirectionKeys()))
	{
		GfxCameraController* pCamera = gfxGetActiveCameraController();
		gclCamera_GetFacingDirection(pCamera, false, vDirectionOut);
	}
	else if (clientTarget_CameraTargetingUsesDirectionKeys())
	{
		Vec3 vTemp, vCam, vAxis = {0,1,0};
		
		GfxCameraController* pCamera = gfxGetActiveCameraController();
		gclCamera_GetFacingDirection(pCamera, false, vCam);

		vTemp[0] = -getControlButtonState(MIVI_BIT_LEFT) + getControlButtonState(MIVI_BIT_RIGHT);
		vTemp[1] = 0;
		vTemp[2] = -getControlButtonState(MIVI_BIT_BACKWARD) + getControlButtonState(MIVI_BIT_FORWARD);
		if (vTemp[0] || vTemp[1] || vTemp[2])
		{
			rotateVecAboutAxis(atan2f(vCam[0], vCam[2]), vAxis, vTemp, vDirectionOut);
		}
		else if (g_CombatConfig.bCameraTargetingGetsPlayerFacing)
		{
			entGetCombatPosDir(pPlayerEnt, NULL, NULL, vDirectionOut);
		}
		else
		{
			copyVec3(vCam, vDirectionOut);
		}
	}
	else
	{
		entGetCombatPosDir(pPlayerEnt, NULL, NULL, vDirectionOut);
	}
}


AUTO_EXPR_FUNC(entityutil);
void clientTarget_SetTarget(SA_PARAM_NN_VALID Entity* eTarget)
{
	Entity *e = entActivePlayerPtr();

	if(!e)
		return;
	
	clientTarget_ResetTargetChangeTimer();
	entity_SetTarget(e, entGetRef(eTarget));
	s_bTargetIsNew = true;
	s_LastTargetMs = gGCLState.totalElapsedTimeMs;
}

AUTO_EXPR_FUNC(entityutil);
void clientTarget_SetFocusTarget(SA_PARAM_OP_VALID Entity* eTarget)
{
	Entity *e = entActivePlayerPtr();

	if(!e || !eTarget)
		return;

	entity_SetFocusTarget(e, entGetRef(eTarget));
}

AUTO_EXPR_FUNC(entityutil);
void clientTarget_ClearFocusTarget()
{
	Entity *e = entActivePlayerPtr();

	if(!e)
		return;

	entity_ClearFocusTarget(e);
}

bool clientTarget_IsTargetActive(const ClientTargetDef *pTarget)
{
	if (!pTarget)
	{
		return false;
	}
	return pTarget->entRef || IS_HANDLE_ACTIVE(pTarget->hInteractionNode);
}

bool clientTarget_HasHardTarget(Entity *e)
{
	return e->pChar->currentTargetRef || IS_HANDLE_ACTIVE(e->pChar->currentTargetHandle);
}

bool clientTarget_IsTargetHard(void)
{
	Entity *pEntPlayer = entActivePlayerPtr();

	return pEntPlayer && clientTarget_HasHardTarget(pEntPlayer);
}


const ClientTargetDef *clientTarget_GetCurrentHardTarget(void)
{
	Entity *pEntPlayer = entActivePlayerPtr();
	if (pEntPlayer && pEntPlayer->pChar)
	{
		s_HardTargetDef.entRef = pEntPlayer->pChar->currentTargetRef;
		COPY_HANDLE(s_HardTargetDef.hInteractionNode, pEntPlayer->pChar->currentTargetHandle);
	}
	else
	{
		s_HardTargetDef.entRef = 0;
		REMOVE_HANDLE(s_HardTargetDef.hInteractionNode);
	}
	return (ClientTargetDef*)(&s_HardTargetDef);
}

Entity* clientTarget_GetCurrentHardTargetEntity()
{
	const ClientTargetDef *pClientTargetDef = clientTarget_GetCurrentHardTarget();
	if (pClientTargetDef)
	{
		return entFromEntityRefAnyPartition(pClientTargetDef->entRef);
	}
	return NULL;
}


static const ClientTargetDef *ClientTarget_GetTargetDual(Entity *e)
{
	if(e && e->pChar)
		s_TargetDual.entRef = e->pChar->erTargetDual;
	else
		s_TargetDual.entRef = 0;

	REMOVE_HANDLE(s_TargetDual.hInteractionNode);
	return (ClientTargetDef*)(&s_TargetDual);
}



bool clientTarget_IsTargetSoft(void)
{
	return (s_TargetDef.entRef || IS_HANDLE_ACTIVE(s_TargetDef.hInteractionNode)) && !clientTarget_IsTargetHard();
}

static F32 s_OffscreenTime;

void clientTarget_ClearIfOffscreen(F32 fTickTime)
{
	Entity *e = entActivePlayerPtr();
	Entity *ecurrTarget = 0;
	float fDist;

	if (!e || !e->pChar)
	{
		return;
	}

	ecurrTarget = entFromEntityRefAnyPartition(e->pChar->currentTargetRef);
	if (!ecurrTarget)
	{
		return;
	}

	fDist = entGetDistance(e, NULL, ecurrTarget, NULL, NULL);
	if (!clientTarget_IsOnScreenBoxCheck(ecurrTarget, 0))
	{
		s_OffscreenTime += fTickTime;
		if (s_OffscreenTime >= g_CombatConfig.fIgnoreOrClearOffscreenTargetTime)
		{
			entity_FaceSelectedIgnoreTarget(e, true);

			if (!g_bSelectAnyEntity && !g_aiDebugShowing && g_CurrentScheme.bDeselectIfOffScreen) 
				// making sure you're not in SelectAnyEntity or AIDebug mode or the switch hasn't been set
			{
				clientTarget_Clear();
			}
			return;
		}
	}
	else
	{
		s_OffscreenTime = 0.0f;
	}

	entity_FaceSelectedIgnoreTarget(e, false);
}

void clientTarget_NotifyAttacked(Entity *pEnt)
{
	Entity *e = entActivePlayerPtr();
	if (!e || !pEnt)
		return;

	if (!clientTarget_IsTargetHard() && g_CurrentScheme.bTargetAttackerIfAttacked)
	{	
		TargetType eTargetFlags = g_CombatConfig.eSoftTargetFlags|kTargetType_Foe;
		if (clientTarget_MatchesType(pEnt,eTargetFlags,SOFT_TARGET_EXC)) {
			entity_SetTarget(e, entGetRef(pEnt));
			s_bTargetIsNew = true;
		}
	}
	s_erLastAttacker = entGetRef(pEnt);
}

void clientTarget_ResetFaceTimeout(void)
{
	s_OffscreenTime = 0;
	
	if (!g_CurrentScheme.bEnableShooterCamera)
	{
		entity_FaceSelectedIgnoreTarget(entActivePlayerPtr(), false);
	}
}

bool gclClientTarget_TargetCyclingDisabled(void)
{
	return g_CurrentScheme.bEnableShooterCamera;
}

void clientTarget_CycleEx(bool bBackwards, U32 targetRequirements, U32 targetExclusions, ClientTargetFilterCheck cbFilterFunc)
{
	const ClientTargetDef *pTarget = NULL;
	Entity *pEntPlayer = entActivePlayerPtr();

	if (!pEntPlayer || gclClientTarget_TargetCyclingDisabled())
	{
		return;
	}

	pTarget = clientTarget_FindNext(pEntPlayer, bBackwards, targetRequirements, targetExclusions, cbFilterFunc);

	if(pTarget)
	{
		if(pTarget->entRef)
			entity_SetTarget(pEntPlayer,pTarget->entRef);
		else if(IS_HANDLE_ACTIVE(pTarget->hInteractionNode))
			entity_SetTargetObject(pEntPlayer,REF_STRING_FROM_HANDLE(pTarget->hInteractionNode));
		else
			clientTarget_Clear();
	}
	else
	{
		clientTarget_Clear();
	}
}

#define TAB_TARGET_TIMEOUT 3

const ClientTargetDef *clientTarget_FindNext(Entity *e, bool bBackwards, U32 target_type_req, U32 target_type_exc, ClientTargetFilterCheck cbFilterFunc)
{
	static ClientTargetDef **s_ppTargets = NULL;
	static U32 lastCheckTime;
	U32 currentTime;
	bool bCheckOffscreen = g_CurrentScheme.bTabTargetOffscreen;

	//Find the current target to compare to
	Entity *ecurrTarget = entFromEntityRefAnyPartition(e->pChar->currentTargetRef);
	WorldInteractionNode *pcurrNode = GET_REF(e->pChar->currentTargetHandle);
	ClientTargetDef currTarget = {0};
	Vec3 vCamera;
	ClientTargetSortFunc pSortCheck = NULL;
	ClientTargetSortDistCheck pSortDistCheck = NULL;	

	int i;
	int oldIndex;
	int iCntTargets;

	eaClear(&s_ppTargets);

	currTarget.entRef = e->pChar->currentTargetRef;
	COPY_HANDLE(currTarget.hInteractionNode, e->pChar->currentTargetHandle);

	currentTime = timeSecondsSince2000();

	// If we either don't have a hard target, or we need to reset it, set to the auto target
	if (!clientTarget_HasHardTarget(e))
	{	
		WorldInteractionNode *pNode = NULL;
		const ClientTargetDef *pSoftTarget = NULL;
		if (clientTarget_IsTargetSoft())
		{
			const ClientTargetDef *pDef = clientTarget_GetCurrentTarget();

			if(clientTarget_MatchesType(entFromEntityRefAnyPartition(pDef->entRef),target_type_req,target_type_exc))
			{
				pSoftTarget = pDef;				
			}
			else if((target_type_req & kTargetType_Foe) && IS_HANDLE_ACTIVE(pDef->hInteractionNode))
			{
				pSoftTarget = pDef;
			}
		}
		if (pSoftTarget)
		{
			if (pSoftTarget->entRef)
			{
				Entity *pEnt = entFromEntityRefAnyPartition(pSoftTarget->entRef);
				if (pEnt)
				{
					pNode = GET_REF(pEnt->hCreatorNode);
				}
			}
			else 
			{
				pNode = GET_REF(pSoftTarget->hInteractionNode);
			}
		}
		if (pNode && !wlInteractionCanTabSelectNode(pNode))
		{
			// Cancel soft target if it's a non-tabbable object
			pSoftTarget = NULL;
		}
		/* This is commented out now because it makes no sense.  If you have no soft target, and you want to cycle
			targets, you should be picking a new hard target based on cycle target rules, not picking the first
			target of the cycle using soft target rules.  IF you already have a soft target, that makes a little
			more sense.  Sorta.
		if (!pSoftTarget)
		{
			pSoftTarget = ClientTargetFindSoftTarget(NULL,target_type_req,target_type_exc, false, g_CurrentScheme.bTabTargetOffscreen, g_CurrentScheme.fTabTargetMaxDist);
		}
		*/
		if (pSoftTarget && !clientTarget_IsSameTarget(pSoftTarget, &currTarget))
		{		
			REMOVE_HANDLE(currTarget.hInteractionNode);
			lastCheckTime = currentTime;
			return pSoftTarget;
		}
	}

	switch (g_CurrentScheme.eTabTargetOrder)
	{
	xcase kTargetOrder_NearToFar:
		pSortCheck = clientTarget_SortBySortDist;
	xcase kTargetOrder_LeftToRight:
		pSortDistCheck = clientTarget_GetDistFromScreenSide;
		pSortCheck = clientTarget_SortBySortDist;
		s_SearchDetails.analog_direction[0] = 1;
		s_SearchDetails.analog_direction[1] = 0;
	xcase kTargetOrder_LuckyCharms:
		pSortCheck = clientTarget_SortByLuckyCharms;
	xcase kTargetOrder_NearestToCenter:
	case kTargetOrder_OnlyCenter:
		pSortDistCheck = clientTarget_GetDistFromCameraCenter;
		pSortCheck = clientTarget_SortBySortDist;
	xdefault:
		assertmsgf(0,"Invalid TargetType %d",g_CurrentScheme.eInitialTargetOrder);	
	}

	iCntTargets = clientTarget_FindAllValidTargets(e,NULL,target_type_req,target_type_exc,&s_ppTargets,clientTarget_IsOnScreenBoxCheck,pSortDistCheck,pSortCheck,cbFilterFunc,false, bCheckOffscreen, false);
	if(!iCntTargets)
	{
		REMOVE_HANDLE(currTarget.hInteractionNode);
		return NULL;
	}

	ANALYSIS_ASSUME(s_ppTargets);

	// Set up the default "old" index, which if we're moving backwards is one after the last, or if we're moving
	//  forwards it's one before the first
	oldIndex = bBackwards ? iCntTargets : -1;

	// See if our old target is actually in the list somewhere.
	if (!g_CurrentScheme.bResetTabOverTime || lastCheckTime + TAB_TARGET_TIMEOUT >= currentTime)
	{
		if(ecurrTarget || pcurrNode)
		{
			for(i=0; i<iCntTargets; i++)
			{
				if(clientTarget_IsSameTarget(s_ppTargets[i],&currTarget))
				{
					// This is who we already had targeted.  So instead of the default old index, we use this index
					oldIndex = i;
					break;
				}					
			}
		}
	}

	lastCheckTime = currentTime;

	REMOVE_HANDLE(currTarget.hInteractionNode);

	// Do LoS check against both your combat position and the camera position - if either have LoS you're ok

	gfxGetActiveCameraPos(vCamera);

	// Loop through all possible targets, starting one above/below the old index.
	//  If we had no old target, this means we just loop through everything like normal.
	//  If we had an old target then when we've wrapped around, we stop early.
	for(i=1; i<=iCntTargets; i++)
	{
		int realIndex = (iCntTargets + oldIndex + (bBackwards ? -i : i)) % iCntTargets;
		bool bActiveCombatant = false;// Extra iCntTargets is so % works even when just below 0
		Entity* pTarget = s_ppTargets[realIndex] ? entFromEntityRefAnyPartition(s_ppTargets[realIndex]->entRef) : NULL;
		// The break check - if we're moving backwards but what we're looking at is after or at where we started,
		//  or if we're moving forwards but what we're looking at is before or at where we started, then we stop.
		if(((bBackwards && realIndex>=oldIndex) || (!bBackwards && realIndex<=oldIndex)) && !gConf.bTabTargetingLoopsWithNoBreaks)
			break;
		if (!s_ppTargets[realIndex])
			continue;
		bActiveCombatant = (gConf.bTabTargetingAlwaysIncludesActiveCombatants && e && e->pChar && character_GetRelativeDangerValue(SAFE_MEMBER(pTarget, pChar), e->myRef));
			
		if(bActiveCombatant || (clientTarget_IsTargetInLoS(e, NULL, entFromEntityRefAnyPartition(s_ppTargets[realIndex]->entRef), GET_REF(s_ppTargets[realIndex]->hInteractionNode))
			|| clientTarget_IsTargetInLoS(NULL, vCamera, entFromEntityRefAnyPartition(s_ppTargets[realIndex]->entRef), GET_REF(s_ppTargets[realIndex]->hInteractionNode)))
		   && (g_CurrentScheme.fTabTargetMaxDist == 0.f || s_ppTargets[realIndex]->fDist < g_CurrentScheme.fTabTargetMaxDist))
		{
			return s_ppTargets[realIndex];
		}
	}

	return NULL;
}

const ClientTargetDef *clientTarget_FindNextManual(Entity *e, bool bBackwards, U32 target_type_req, U32 target_type_exc, Vec2 dir) // this handles getting the next entity by direction from your current target
{
	//Find the current target to compare to
	Entity *ecurrTarget = entFromEntityRefAnyPartition(e->pChar->currentTargetRef);
	WorldInteractionNode *pcurrNode = GET_REF(e->pChar->currentTargetHandle);
	ClientTargetDef currTarget = {0};
	static ClientTargetDef **s_ppTargets = NULL;
	Vec3 vSource;
	S32 oldIndex = 0;
	bool bCheckOffscreen = g_CurrentScheme.bTabTargetOffscreen;

	s_SearchDetails.analog_direction[0] = dir[0];
	s_SearchDetails.analog_direction[1] = dir[1]; // storing the direction in a global, because it's tricky to get the variable to a sort function otherwise.

	eaClear(&s_ppTargets);

	currTarget.entRef = e->pChar->currentTargetRef;
	COPY_HANDLE(currTarget.hInteractionNode, e->pChar->currentTargetHandle);

	entGetPos(e,vSource);

	if(ecurrTarget || pcurrNode)
	{
		Vec3 vTarget;
		int i;
		int iCntTargets;

		if(ecurrTarget)
		{
			entGetPos(ecurrTarget,vTarget);
			entGetWindowScreenPos(ecurrTarget, s_SearchDetails.screen_Loc, 0);
		}
		else
		{
			character_FindNearestPointForObject(e->pChar,vSource,pcurrNode,vTarget,true);
			clientTarget_InteractionNodeGetWindowScreenPos(pcurrNode,s_SearchDetails.screen_Loc,0);
		}

		currTarget.fDist = distance3(vTarget,vSource);
		s_SearchDetails.analog_direction[2] = -((s_SearchDetails.screen_Loc[0]*dir[0])+(s_SearchDetails.screen_Loc[1]*dir[1])); // setting up a 2D plane equation, which all future points will be sorted by

		iCntTargets = clientTarget_FindAllValidTargets(e,NULL,target_type_req,target_type_exc,&s_ppTargets,clientTarget_IsOnScreenBoxCheck,clientTarget_GetDistForSortByDir,clientTarget_SortBySortDist,NULL,false, bCheckOffscreen, false);
		if(!iCntTargets)
		{
			REMOVE_HANDLE(currTarget.hInteractionNode);
			return NULL;
		}

		ANALYSIS_ASSUME(s_ppTargets);

		if (bBackwards)
		{
			oldIndex = iCntTargets;
		}
		else
		{
			oldIndex = -1;
		}

		if(ecurrTarget || pcurrNode)
		{
			for(i=0;i < iCntTargets;i++)
			{
				if(clientTarget_IsSameTarget(s_ppTargets[i],&currTarget))
				{
					oldIndex = i;
				}					
			}
			// No Wrap Around
			if (bBackwards)
			{
				if (oldIndex == 0)
				{
					REMOVE_HANDLE(currTarget.hInteractionNode);
					return s_ppTargets[oldIndex];
				}
			}
			else
			{
				if (oldIndex == iCntTargets - 1)
				{
					REMOVE_HANDLE(currTarget.hInteractionNode);
					return s_ppTargets[oldIndex];
				}
			}
		}

		if(bBackwards)
		{
			for(i=oldIndex-1;i>=0;i--)
			{
				if(clientTarget_IsTargetInLoS(e, NULL, entFromEntityRefAnyPartition(s_ppTargets[i]->entRef), GET_REF(s_ppTargets[i]->hInteractionNode)) )
				{
					REMOVE_HANDLE(currTarget.hInteractionNode);
					return s_ppTargets[i];
				}
			}
		}
		else
		{
			for(i=oldIndex+1;i < iCntTargets;i++)
			{
				if(clientTarget_IsTargetInLoS(e, NULL, entFromEntityRefAnyPartition(s_ppTargets[i]->entRef), GET_REF(s_ppTargets[i]->hInteractionNode)) )
				{
					REMOVE_HANDLE(currTarget.hInteractionNode);
					return s_ppTargets[i];
				}
			}
		}
		REMOVE_HANDLE(currTarget.hInteractionNode);
		return NULL;
	}
	else
	{
		REMOVE_HANDLE(currTarget.hInteractionNode);
		return clientTarget_FindNext(e, bBackwards, target_type_req, target_type_exc, NULL);
	}
}

const ClientTargetDef *clientTarget_GetCurrentTarget(void)
{
	return (ClientTargetDef*)(&s_TargetDef);
}

ClientTargetMutableDef *clientTarget_GetCurrentTargetToModify(void)
{
	return &s_TargetDef;
}


//To be a "SmartTarget" the current hard target must be on screen, in range of the current power
bool clientTarget_IsSmartTarget(Entity *ePlayer, const ClientTargetDef *pCurTarget, Power *pPower, PowerDef *pPowerDef, F32 fMaxDist)
{
	Vec3 vPlayerPos, vTargetPos;
	Entity *pEntTarget = entFromEntityRefAnyPartition(pCurTarget->entRef);
	WorldInteractionNode *pNodeTarget = GET_REF(pCurTarget->hInteractionNode);
	F32 fDist;
	F32 fRange = power_GetRange(pPower, pPowerDef);

//	if(clientTarget_IsOnScreen(pEntTarget,pNodeTarget))
//		return true;

	if(pEntTarget)
	{
		fDist = entGetDistance(ePlayer, NULL, pEntTarget, NULL, NULL);
	}
	else if(pNodeTarget)
	{
		entGetPos(ePlayer,vPlayerPos);
		character_FindNearestPointForObject(ePlayer->pChar,vPlayerPos,pNodeTarget,vTargetPos,true);
		fDist = entGetDistance(ePlayer, NULL, NULL, vTargetPos, NULL);
	}
	else
	{
		return false;
	}

	if (fDist <= MAX(fRange, fMaxDist))
		return true;
	return false;
}

static bool IsMeleePower(PowerDef *pDef)
{
	PowerAnimFX *pAfx;
	if (!pDef)
	{
		return false;
	}
	if (pDef->fRange >= 12.0)
	{
		return false;
	}
	pAfx = GET_REF(pDef->hFX);
	if (pAfx && pAfx->pLunge && pAfx->pLunge->fRange > 12)
	{
		return false;
	}
	return true;
}

TargetingAssist gclPlayerGetTargetingAssist()
{
	if (g_CurrentScheme.eTargetAssistOverride > CombatTargetingAssist_UseCombatconfig)
		return g_CurrentScheme.eTargetAssistOverride;

	return g_CombatConfig.eClientTargetAssist;
}

ClientTargetDef *clientTarget_SelectBestTargetForPowerEx(Entity *pEntPlayer, Power *ppow, PowerTarget *pPowTargetOverride, bool *pShouldSetHard)
{
	PowerDef *pdef = ppow ? GET_REF(ppow->hDef) : NULL;
	PowerTarget *ppowTarget = pPowTargetOverride ? pPowTargetOverride : (pdef ? (PowerTarget *)GET_REF(pdef->hTargetMain) : NULL);
	bool skip = false;
	bool bUseSelf;
	const ClientTargetDef *pCurTarget;

	if(!pEntPlayer || !pdef || !ppowTarget)
		return NULL;

	if(pdef->eType==kPowerType_Combo)
	{
		if(pdef->bComboTargetRules)
		{
			// Combo with client-side targeting expression.  Don't bother finding a target now,
			//  character_PickActivatedPower() will find a legal target automatically.
			return NULL;
		}
	}

	if (pShouldSetHard)
		*pShouldSetHard = true;

	if(gConf.bTargetDual && entIsPlayer(pEntPlayer) && !ppowTarget->bAllowFoe)
	{
		pCurTarget = ClientTarget_GetTargetDual(pEntPlayer);
		if(pShouldSetHard)
			*pShouldSetHard = false;
	}
	else if (clientTarget_IsTargetHard())
		pCurTarget = clientTarget_GetCurrentHardTarget();
	else
		pCurTarget = clientTarget_GetCurrentTarget();

	if (!clientTarget_IsTargetActive(pCurTarget))
		pCurTarget = NULL;

	if(gclPlayerGetTargetingAssist() == CombatTargetingAssist_Full)
	{
		if(pCurTarget && pCurTarget->entRef)
		{
			Entity *pEnt = entFromEntityRefAnyPartition(pCurTarget->entRef);
			if (!pEnt)
			{
				pCurTarget = NULL;
			}
			else if (!character_TargetMatchesPowerType(PARTITION_CLIENT,pEntPlayer->pChar,pEnt->pChar,ppowTarget))
			{
				if (	g_CurrentScheme.bAssistTargetIfInvalid
					&& (!g_CombatConfig.bAssistChecksTargetSelfFirst || !ppowTarget->bAllowSelf))
				{
					if(pEnt && pEnt->pChar && pEnt->pChar->currentTargetRef)
					{
						Entity *pEntFinal = entFromEntityRefAnyPartition(pEnt->pChar->currentTargetRef);
						if (pEntFinal && character_TargetMatchesPowerType(PARTITION_CLIENT,pEntPlayer->pChar,pEntFinal->pChar,ppowTarget))
						{
							s_BestTargetDef.entRef = pEnt->pChar->currentTargetRef;
							REMOVE_HANDLE(s_BestTargetDef.hInteractionNode);
							s_BestTargetDef.fDist = 0.f;
							return &s_BestTargetDef;
						}									
					}
				}
				if (!entIsAlive(pEnt))
				{
					// Immediately cancel if target is dead
					pCurTarget = NULL;
					s_LastDeadTargetChange = timeSecondsSince2000();

					if (g_CurrentScheme.eAutoAttackType == kAutoAttack_Toggle)
					{
						gclAutoAttack_Disable();
					}
				}
				pCurTarget = NULL;
			}
			if (pCurTarget && g_CurrentScheme.bMeleeIgnoreBadHardTarget && IsMeleePower(pdef) &&
					   !clientTarget_IsSmartTarget(pEntPlayer,pCurTarget,ppow, pdef, 20))
			{
				pCurTarget = NULL;
				if (pShouldSetHard)
				{
					*pShouldSetHard = false;
				}
			}
		}
		else if (pCurTarget && !character_TargetMatchesPowerTypeNode(pEntPlayer->pChar, ppowTarget))
		{
			pCurTarget = NULL;
		}
	}

	bUseSelf = false;

	// If it only targets self
	if (ppowTarget->bRequireSelf)
	{
		bUseSelf = true;
	}
	else
	{
		if(gclPlayerGetTargetingAssist() == CombatTargetingAssist_MouseLook && 
			(!gclPlayerControl_IsHardLockPressed() || !pCurTarget) )
		{
			Entity *pEnt = target_SelectUnderMouseEx(pEntPlayer, ppowTarget, 0, 0, NULL, true, false, false);

			if(pEnt)
			{
				s_BestTargetDef.entRef = entGetRef(pEnt);
				REMOVE_HANDLE(s_BestTargetDef.hInteractionNode);
				s_BestTargetDef.fDist = entGetDistance(pEntPlayer, NULL, pEnt, NULL, NULL);

				return &s_BestTargetDef;
			}
			else
				pCurTarget = NULL;
		}

		if (pCurTarget)
		{
			CopyClientTargetDef(&s_BestTargetDef, pCurTarget);			
		}
		else
		{
			CopyClientTargetDef(&s_BestTargetDef, NULL);

			if(gclPlayerGetTargetingAssist() == CombatTargetingAssist_Full)
			{
				F32 fPowerRange = power_GetRange(ppow, pdef);
				pCurTarget = ClientTargetFindSoftTarget(ppowTarget, 0, 0, true, false, fPowerRange);

				if(pCurTarget && (!pdef || pdef->bApplyObjectDeath || pCurTarget->fDist < fPowerRange) )
					CopyClientTargetDef(&s_BestTargetDef, pCurTarget);
			}

			// stil no target, let's see if Self is a valid choice
			if (!pCurTarget && character_TargetMatchesPowerType(PARTITION_CLIENT,pEntPlayer->pChar,pEntPlayer->pChar,ppowTarget))
			{
				bUseSelf = true;
			}
		}
	}

	if (bUseSelf)
	{
		s_BestTargetDef.entRef = entGetRef(pEntPlayer);
		REMOVE_HANDLE(s_BestTargetDef.hInteractionNode);
		s_BestTargetDef.fDist = 0.f;
	}

	return &s_BestTargetDef;
}

ClientTargetDef *clientTarget_SelectBestTargetForPower(Entity *pEntPlayer, Power *ppow, bool *pShouldSetHard)
{
	return clientTarget_SelectBestTargetForPowerEx(pEntPlayer, ppow, NULL, pShouldSetHard);
}

bool clientTarget_MatchesTypeEx(Entity *ePlayer, Entity *eTarget, PowerTarget *pPowerTarget, U32 target_type_req, U32 target_type_exc)
{
	if (eTarget)
	{
		if (eTarget->pChar)
		{
			if(pPowerTarget)
				return character_TargetMatchesPowerType(PARTITION_CLIENT,ePlayer->pChar, eTarget->pChar, pPowerTarget);
			else
				return character_TargetMatchesType(PARTITION_CLIENT,ePlayer->pChar, eTarget->pChar, target_type_req, target_type_exc);
		}
		else if (entCheckFlag(eTarget, ENTITYFLAG_CIVILIAN) == ENTITYFLAG_CIVILIAN)
		{
			TargetType eRel;

			eRel = kTargetType_Alive;

			return ((target_type_req & eRel)==target_type_req && !(target_type_exc & eRel));
		}
		else if (target_type_req == 0 && pPowerTarget == NULL)
		{
			// we're only trying to exclude things, so whatever this guy is, he's fine.
			return true;
		}
	}


	return false;
}


bool clientTarget_MatchesType(Entity *eTarget, U32 target_type_req, U32 target_type_exc)
{
	Entity *ePlayer = entActivePlayerPtr();

	if (ePlayer && ePlayer->pChar && eTarget && eTarget->pChar)
	{
		return character_TargetMatchesType(PARTITION_CLIENT,ePlayer->pChar, eTarget->pChar, target_type_req, target_type_exc);
	}
	return false;
}

// Finds a target location a given distance from the player in the direction of the center of the screen
S32 clientTarget_GetSimpleSecondaryRangeTarget(F32 fRange, Vec3 vecTargetOut)
{
	S32 bGood = false;
	Entity *e = entActivePlayerPtr();
	if(e)
	{
		Mat4 matCam;
		Vec3 vecPos, vecTarget;
		WorldCollCollideResults wcResults;

		gfxGetActiveCameraMatrix(matCam);
		entGetPos(e,vecPos);
		copyVec3(vecPos,vecTarget);
		moveVinZ(vecTarget,matCam,-fRange);

		if(worldCollideRay(PARTITION_CLIENT, vecPos, vecTarget, WC_QUERY_BITS_TARGETING, &wcResults))
		{
			copyVec3(wcResults.posWorldImpact,vecTarget);
		}

		copyVec3(vecTarget,vecTargetOut);
		bGood = true;
	}

	return bGood;
}


void clientTarget_tick(F32 fTickTime)
{
	bool bTargetChanged = false;
	Entity *pEntPlayer = entActivePlayerPtr();
	static F32 fTimeTargetIsDown = 0.0f;
	static F32 fTimeFocusTargetIsDown = 0.0f;
	static bool s_bWasInCombatLastFrame = false;
	const ClientTargetDef *pCurTargetDef = clientTarget_GetCurrentTarget();
	bool bCanQueueMultiExecPower = false;
	bool bIsPlayerInCombat;

	if(!pEntPlayer)
		return;

	PERFINFO_AUTO_START_FUNC_PIX();

	bIsPlayerInCombat = entIsInCombat(pEntPlayer);

	if(!s_bAutoAttackEnabled && pEntPlayer->pChar)
	{
		// Refresh the AutoAttack list.  If AutoAttack is enabled we may
		//  do this later as well, but in that case we want to keep the
		//  current list for just a bit.
		gclAutoAttack_RefreshIDs(pEntPlayer->pChar);
	}

	if (s_fStickyHardTargetTime > 0.f)
	{
		s_fStickyHardTargetTime -= fTickTime;
		if (s_fStickyHardTargetTime < 0.f)
			s_fStickyHardTargetTime = 0.f;
	}
	

	if(g_CurrentScheme.bMouseLookHardTarget && gclPlayerControl_IsMouseLooking())
	{
		Entity *pTarget = NULL;
		WorldInteractionNode *pTargetNode = NULL;
		
		if (gclPlayerControl_IsHardLockPressed())
		{
			pTarget = clientTarget_GetCurrentHardTargetEntity();
			if (pTarget && !entIsAlive(pTarget))
			{
				pTarget = NULL;
			}
		}
		
		if (!pTarget)
		{
			pTarget = target_SelectUnderMouse(	pEntPlayer,
												(g_CurrentScheme.bMouseLookHardTargetExcludeCorpses ? kTargetType_Alive : 0),
												0,
												NULL,
												true,
												false,
												false);
		}

		if (!pTarget)
			pTargetNode = target_SelectObjectUnderMouse(pEntPlayer, iBitMaskDes|iBitMaskThrow);

		// if we have hard target stickiness on, update it
		if (g_CombatConfig.fMouseLookHardTargetStickyTime)
		{
			if (pTarget || pTargetNode)
			{
				s_fStickyHardTargetTime = g_CombatConfig.fMouseLookHardTargetStickyTime;
			}
			else if (s_fStickyHardTargetTime > 0.f)
			{
				Entity *pCurrentHardTarget = clientTarget_GetCurrentHardTargetEntity();
				if (pCurrentHardTarget && entIsAlive(pCurrentHardTarget))
					pTarget = pCurrentHardTarget;
			}
		}
		

		if (g_CurrentScheme.bCheckActiveWeaponRangeForTargeting && (pTarget || pTargetNode))
		{
			F32 fDist;
			if (pTarget)
			{
				fDist = entGetDistance(pEntPlayer, NULL, pTarget, NULL, NULL);
			}
			else
			{
				Vec3 vNodePos;
				character_FindNearestPointForObject(pEntPlayer->pChar,NULL,pTargetNode,vNodePos,true);
				fDist = entGetDistance(pEntPlayer, NULL, NULL, vNodePos, NULL);
			}

			if (fDist > gclClientTarget_GetActiveWeaponRange(pEntPlayer))
			{
				pTarget = NULL;
				pTargetNode = NULL;
			}
		}
		if (!gclControlSchemeIsChangingCurrent())
		{
			if (pTarget)
			{
				entity_SetTarget(pEntPlayer,entGetRef(pTarget));
			}
			else if (pTargetNode)
			{
				entity_SetTargetObject(pEntPlayer, wlInteractionNodeGetKey(pTargetNode));
			}
			else
			{
				entity_SetTarget(pEntPlayer, 0);
			}
		}
	}

	if(g_CurrentScheme.bMouseLookInteract && gclPlayerControl_IsMouseLooking())
	{
		interactOverrideCursor();
	}

	// Fixup the main/dual targets if needed
	if(gConf.bTargetDual && pEntPlayer->pChar)
	{
		EntityRef erTarget = pEntPlayer->pChar->currentTargetRef;
		EntityRef erTargetDual = pEntPlayer->pChar->erTargetDual;
		Entity *pTarget = entFromEntityRefAnyPartition(erTarget);
		Entity *pTargetDual = entFromEntityRefAnyPartition(erTargetDual);

		if(pTarget && !character_TargetIsFoe(PARTITION_CLIENT,pEntPlayer->pChar,pTarget->pChar))
		{
			// What was our main target isn't a foe, cancel and then try and
			//  then try and re-set if if there's no dual target
			entity_SetTarget(pEntPlayer,0);
			if(!pTargetDual)
				entity_SetTarget(pEntPlayer,erTarget);
		}

		if(pTargetDual && character_TargetIsFoe(PARTITION_CLIENT,pEntPlayer->pChar,pTargetDual->pChar))
		{
			// What was our dual target is a foe, cancel the dual target and
			//  then try and re-set if there's no main target
			entity_ClearTargetDual(pEntPlayer);
			if(!entity_GetTarget(pEntPlayer))
				entity_SetTarget(pEntPlayer,erTargetDual);
		}
	}

	// update class specific targeting 
	if (pEntPlayer->pChar)
	{
		CharacterClass* pClass = GET_REF(pEntPlayer->pChar->hClass);
		
		if (pClass)
		{
			gclReticle_UseReticleDefByName(pClass->pchReticleDef);

			// check out current proximity targeting assist. 
			if (pClass->bUseProximityTargetingAssistEnt)
			{
				Vec3 vProximityTargetDir = {0};
				Entity *pProximityAssistEnt;
				Entity *pOldAssistTarget = NULL;

				if (pEntPlayer->pChar->erProxAssistTaget)
				{
					pOldAssistTarget = entFromEntityRefAnyPartition(pEntPlayer->pChar->erProxAssistTaget);
				}
				
				pEntPlayer->pChar->erProxAssistTaget = 0;
				
				clientTarget_GetVecTargetingDirection(pEntPlayer, NULL, vProximityTargetDir);
				pProximityAssistEnt = clientTarget_FindProximityTargetingAssistEnt(pEntPlayer, vProximityTargetDir);
				if (pProximityAssistEnt)
				{
					pEntPlayer->pChar->erProxAssistTaget = entGetRef(pProximityAssistEnt);
				}
				else if (pOldAssistTarget && !entIsAlive(pOldAssistTarget) && gclAutoAttack_IsEnabled())
				{
					gclAutoAttack_Disable();
				}
			}
			
		}
	}
	

	//Check if we're looking for a soft or hard target to highlight
	if (g_CurrentScheme.bSoftTarget && !clientTarget_HasHardTarget(pEntPlayer))
	{
		//Player needs a soft target
		TargetType eTargetFlags = g_CombatConfig.eSoftTargetFlags;
		ClientTargetDef *pSoftTargetDef = ClientTargetFindSoftTarget(NULL,eTargetFlags,SOFT_TARGET_EXC,true,false,g_CurrentScheme.fAutoTargetMaxDist);
		fTimeTargetIsDown = 0.0f;

		// If I have a target and I can't find a soft target
		//    or I found a soft target and (it's not the target I already have or I was previously hard targeted)
		if(((pCurTargetDef->entRef || IS_HANDLE_ACTIVE(pCurTargetDef->hInteractionNode)) && !pSoftTargetDef) 
			|| pSoftTargetDef && (!clientTarget_IsSameTarget(pCurTargetDef, pSoftTargetDef) || !pCurTargetDef->bSoft))
		{
			if(pSoftTargetDef)
			{
				Entity *pEntNewTarget = entFromEntityRefAnyPartition(pSoftTargetDef->entRef);
				WorldInteractionNode *pNodeNewTarget = GET_REF(pSoftTargetDef->hInteractionNode);
				pSoftTargetDef->bSoft = 1;
				
				if (pEntNewTarget && GET_REF(pEntNewTarget->hCreatorNode) && GET_REF(pEntNewTarget->hCreatorNode) == GET_REF(pCurTargetDef->hInteractionNode))
				{
					// Transitioned destructible
					bTargetChanged = false;
				}
				else
				{
					bTargetChanged = true;
				}

				CopyClientTargetDefToMutable( clientTarget_GetCurrentTargetToModify(), pSoftTargetDef );				
			}
			else
			{
				bTargetChanged = true;
				CopyClientTargetDefToMutable( clientTarget_GetCurrentTargetToModify(), NULL);
			}
		}
	}
	else if(pEntPlayer->pChar)
	{
		// Player has a hard target or no target
		ClientTargetDef curHardTarget = {0};

		// Untarget dead entities and missing objects
		if (pEntPlayer->pChar->currentTargetRef)
		{
			Entity *pentHardTarget = entFromEntityRefAnyPartition(pEntPlayer->pChar->currentTargetRef);
			if(!pentHardTarget
				|| (!g_bSelectAnyEntity && !entIsCivilian(pentHardTarget)
				&& ((!g_CombatConfig.bTargetDeadEnts || (!entIsAlive(pentHardTarget) && critter_IsKOS(PARTITION_CLIENT,pEntPlayer,pentHardTarget)))
				&& !clientTarget_MatchesTypeEx(pEntPlayer, pentHardTarget, NULL, kTargetType_Alive, 0))
				|| (!g_bSelectAnyEntity && !entIsSelectable(pEntPlayer->pChar, pentHardTarget))
				|| !gclExternEntityTargetable(pEntPlayer,pentHardTarget)))
			{
				if (!entHasRefExistedRecently(pEntPlayer->pChar->currentTargetRef) && fTimeTargetIsDown < 1.0f)
				{
					fTimeTargetIsDown += fTickTime;
				}
				else if(!g_bSelectAnyEntity && ((fTimeTargetIsDown > 1.0f) || !pentHardTarget || !entIsSelectable(pEntPlayer->pChar, pentHardTarget)))
				{
					fTimeTargetIsDown = 0.0f;
					s_LastDeadTargetChange = timeSecondsSince2000();
					entity_SetTarget(pEntPlayer, 0);
				}
				else
				{
					fTimeTargetIsDown += fTickTime;
				}
			}
		}
		else if (IS_HANDLE_ACTIVE(pEntPlayer->pChar->currentTargetHandle))
		{
			WorldInteractionNode *pobjHardTarget = GET_REF(pEntPlayer->pChar->currentTargetHandle);

			if(!pobjHardTarget || mapState_IsNodeHiddenOrDisabled(pobjHardTarget))
			{
				entity_SetTargetObject(pEntPlayer, NULL);
				ServerCmd_character_CheckTargetObject();
			}
		}
		else
		{
			fTimeTargetIsDown = 0.0f;
		}

		curHardTarget.entRef = pEntPlayer->pChar->currentTargetRef;
		COPY_HANDLE(curHardTarget.hInteractionNode, pEntPlayer->pChar->currentTargetHandle);
		curHardTarget.fDist = 0; //Figure this out later if we need to
		curHardTarget.bSoft = 0;

		if(!clientTarget_IsSameTarget(&curHardTarget, pCurTargetDef) || (pCurTargetDef && pCurTargetDef->bSoft) )
		{
			Entity *pEntNewTarget = entFromEntityRefAnyPartition(curHardTarget.entRef);
			WorldInteractionNode *pNodeNewTarget = GET_REF(curHardTarget.hInteractionNode);

			if (curHardTarget.entRef && !entHasRefExistedRecently(curHardTarget.entRef))
			{
				// Pending destructible
				bTargetChanged = false;
			}
			else if (pEntNewTarget && GET_REF(pEntNewTarget->hCreatorNode) && GET_REF(pEntNewTarget->hCreatorNode) == GET_REF(pCurTargetDef->hInteractionNode))
			{
				// Transitioned destructible
				bTargetChanged = false;
			}
			else if (clientTarget_IsTargetActive(pCurTargetDef) && !clientTarget_IsSameTarget(&curHardTarget, pCurTargetDef))
			{
				bTargetChanged = true;
			}
			CopyClientTargetDefToMutable( clientTarget_GetCurrentTargetToModify(), &curHardTarget );				
		}

		REMOVE_HANDLE(curHardTarget.hInteractionNode);
	}

	// Check to see if we need to clear the focus target
	if (pEntPlayer->pChar && pEntPlayer->pChar->erTargetFocus)
	{
		Entity *pentFocusTarget = entFromEntityRefAnyPartition(pEntPlayer->pChar->erTargetFocus);
		if(!pentFocusTarget
			|| (!g_bSelectAnyEntity && !entIsCivilian(pentFocusTarget)
			&& ((!g_CombatConfig.bTargetDeadEnts || (!entIsAlive(pentFocusTarget) && critter_IsKOS(PARTITION_CLIENT,pEntPlayer,pentFocusTarget)))
			&& !clientTarget_MatchesTypeEx(pEntPlayer, pentFocusTarget, NULL, kTargetType_Alive, 0))
			|| (!g_bSelectAnyEntity && !entIsSelectable(pEntPlayer->pChar, pentFocusTarget))
			|| !gclExternEntityTargetable(pEntPlayer,pentFocusTarget)))
		{
			if (!entHasRefExistedRecently(pEntPlayer->pChar->currentTargetRef) && fTimeFocusTargetIsDown < 1.0f)
			{
				fTimeFocusTargetIsDown += fTickTime;
			}
			else if(!g_bSelectAnyEntity && ((fTimeFocusTargetIsDown > 1.0f) || !pentFocusTarget || !entIsSelectable(pEntPlayer->pChar, pentFocusTarget)))
			{
				fTimeFocusTargetIsDown = 0.0f;
				entity_ClearFocusTarget(pEntPlayer);
			}
			else
			{
				fTimeFocusTargetIsDown += fTickTime;
			}
		}
	}
	else
	{
		fTimeFocusTargetIsDown = 0.0f;
	}

	if(!gbNoGraphics)
	{
		if (g_CurrentScheme.bEnableShooterCamera)
		{
			// Permanently disable run and gun when ShooterCamera is active
			entity_FaceSelectedIgnoreTarget(pEntPlayer, true);
		}
		else
		{
			clientTarget_ClearIfOffscreen(fTickTime);
		}
	}

	if(pCurTargetDef->entRef)
	{
		Entity *pCurrEnt = entFromEntityRefAnyPartition(pCurTargetDef->entRef);
		dtFxManSetTestTargetNode(pEntPlayer->dyn.guidFxMan,pCurrEnt ? pCurrEnt->dyn.guidRoot : 0, pCurrEnt?pCurrEnt->dyn.guidFxMan:0);
	}
	else
	{
		dtFxManSetTestTargetNode(pEntPlayer->dyn.guidFxMan, 0, 0);
	}

	//debug check
	if(g_bDebugTargeting)
	{
		target_debugdrawarcs();
	}

	if(g_bDebugNearestPoint)
	{
		target_debugdrawnearestpoint();
	}

	if (g_bDebugRayCastMaterials)
		target_debugRaycastMaterials();

	// MultiExec: execute all powers in the array s_puiMultiExec (one at a time)
	if (pEntPlayer->pChar && ea32Size(&s_puiMultiExec) > 0)
	{
		static F32 s_fLastProcessTime = -1.0f;
		S32 i;
		F32 fLastTimerValue = s_fMultiExecTimer;
		
		// Check if we can stop stalling MultiExec, which is basically when we're done charging/maintaining anything
		if(s_bMultiExecStall)
		{
			if(pEntPlayer->pChar->eChargeMode==kChargeMode_None)
				s_bMultiExecStall = false;
		}

		if ( s_fMultiExecTimer <= 0)
		{
			ea32Clear(&s_puiMultiExec);
		}
		else
		{
			s_fMultiExecTimer -= fTickTime;
		}

		if ( !s_bMultiExecStall && (s_fLastProcessTime < 0.0f || s_fLastProcessTime - s_fMultiExecTimer >= 0.05f ))
		{
			for ( i = ea32Size(&s_puiMultiExec)-1; i >= 0; i-- )
			{
				U32 uiPowID = s_puiMultiExec[i];
				Power* pPow = character_FindPowerByID( pEntPlayer->pChar, uiPowID );
				ClientTargetDef *pTarget = pPow ? clientTarget_SelectBestTargetForPower(pEntPlayer,pPow,NULL) : NULL;

				if ( pTarget )
				{
					Entity* pEntTargetOut = NULL;
					WorldInteractionNode* pNodeTargetOut = NULL;
					Entity* pEntTarget = pTarget->entRef ? entFromEntityRefAnyPartition( pTarget->entRef ) : NULL;
					WorldInteractionNode* pNodeTarget = pEntTarget==NULL ? GET_REF(pTarget->hInteractionNode) : NULL;

					if ( pEntTarget || pNodeTarget )
					{
						GameAccountDataExtract *pExtract = entity_GetCachedGameAccountDataExtract(pEntPlayer);
						ActivationFailureReason eFail = kActivationFailureReason_None;
						if (character_CanQueuePower(entGetPartitionIdx(pEntPlayer),pEntPlayer->pChar,pPow,pEntTarget,NULL,pNodeTarget,NULL,&pEntTargetOut,&pNodeTargetOut,NULL,pmTimestamp(0),-1,&eFail,false,true,true,pExtract))
						{
							bCanQueueMultiExecPower = true;
						}
						else if(eFail==kActivationFailureReason_PriorActNonInterrupting)
						{
							// If we failed because we're blocked by a non-interrupting activation, stall and break
							s_bMultiExecStall = true;
							break;
						}
					}
					else if (g_CombatConfig.bRemoveMultiExecPowersIfInvalidTarget)//if we don't have any eligible targets, toss this out of the queue.
					{
						ea32RemoveFast( &s_puiMultiExec, i );
					}
				}

				if ( bCanQueueMultiExecPower )
				{
					entUsePowerID( true, uiPowID );
					ea32RemoveFast( &s_puiMultiExec, i );
					break;
				}
			}

			s_fLastProcessTime = s_fMultiExecTimer;
		}

		if ( s_bMultiExecStall || ea32Size(&s_puiMultiExec)==0 )
		{
			s_fMultiExecTimer = g_CombatConfig.fMultiExecListClearTimer;
			s_fLastProcessTime = -1.0f;
		}
	}
	if(s_bAutoAttackEnabled && !bCanQueueMultiExecPower) // Auto attack
	{
		Entity *pEntity;

		pEntity = pEntPlayer; // TODO: make work with pets again

		if(!pEntity
			|| !pEntity->pChar
			|| g_CurrentScheme.eAutoAttackType==kAutoAttack_None
			|| (bTargetChanged && g_CurrentScheme.eAutoAttackType==kAutoAttack_Toggle)
			|| (s_bWasInCombatLastFrame && pmTimeUntil(pEntity->pChar->uiTimeCombatExit) < 0.1f && g_CurrentScheme.eAutoAttackType==kAutoAttack_ToggleCombat))
		{
			gclAutoAttack_Disable();
		}
		else
		{
			int i,s;
			Character *pchar = pEntity->pChar;

			// Refresh the list
			gclAutoAttack_RefreshIDs(pchar);

			// If the resulting list is empty, cancel autoattack
			if(!ea32Size(&s_puiAutoAttackIDs))
			{
				gclAutoAttack_Disable();
			}
			
			if(s_bAutoAttackEnabled
				&& bTargetChanged
				&& g_CurrentScheme.eAutoAttackType!=kAutoAttack_Toggle)
			{
				// If we're still autoattacking, and we switched targets, and our
				//  autoattack doesn't disable automatically on a target switch, we
				//  try to stop any autoattack Activations, but don't actually
				//  disable autoattack
				gclAutoAttack_StopActivations(true, false);
			}

			if(s_bAutoAttackEnabled
				&& !pchar->pPowActQueued
				&& !pchar->pPowActOverflow
				&& pchar->eChargeMode == kChargeMode_None)
			{
				F32 fFinishedDelay = g_CombatConfig.autoAttack.bClientSchemeFinishDelay ? g_CurrentScheme.fAutoAttackDelay : .25f;
				GameAccountDataExtract *pExtract;

				bool bNoPowerFeedBack = true;
				s = ea32Size(&s_puiAutoAttackIDs);
				
				// TODO(JW): In theory this is where we'd put the "try to avoid
				//  interrupting another 'combo' in progress", except there's no
				//  way we can get that to work right long term or across multiple
				//  games without annotating the PowerDefs, since any finished
				//  Activation could be a lead in to a "combo" for any of your other
				//  Combo powers, and inserting a global 0.5s delay for autoattack
				//  is garbage.  Hell, your autoattack Power itself might combo
				//  of off one of your other Powers.
				// The best I can offer at the moment, is if your current Power
				//  (or finished Power, if nothing is current) ISN'T one of your
				//  autoattack Powers, we'll avoid using autoattack for a tiny bit.
				if (!g_CombatConfig.autoAttack.bNeverDelay)
				{
					if((pchar->pPowActCurrent 
						&& -1==ea32Find(&s_puiAutoAttackIDs,pchar->pPowActCurrent->ref.uiID))
						|| (!pchar->pPowActCurrent
							&& pchar->pPowActFinished 
							&& pchar->pPowActFinished->fTimeFinished<fFinishedDelay
							&& -1==ea32Find(&s_puiAutoAttackIDs,pchar->pPowActFinished->ref.uiID))
							//Disables auto attack for toggle no cancel when you have no hard target
						|| (!pCurTargetDef->entRef && !GET_REF(pCurTargetDef->hInteractionNode) && g_CurrentScheme.eAutoAttackType == kAutoAttack_ToggleNoCancel)
						|| (g_CombatConfig.fCooldownGlobal && pchar->fCooldownGlobalTimer > 0)
						|| (g_CurrentScheme.bDelayAutoAttackUntilCombat && !pEntity->pChar->uiTimeCombatExit && !bIsPlayerInCombat ))
					{
						// Simple way to avoid autoattacking, set the cached size of the earray to 0
						s = 0;
					}
				}

				// We might actually want to queue up an autoattack now!  Walk
				//  the list in order and see if we can find anything reasonable
				//  to activate.
				pExtract = entity_GetCachedGameAccountDataExtract(pEntity);
				for(i=0; i<s; i++)
				{
					U32 uiID = s_puiAutoAttackIDs[i];
					Power *ppow, *ppowToQueue;
					PowerDef *pdefAuto;
					ClientTargetDef *pTarget;
					Entity *pEntTarget, *pEntTargetOut = NULL;
					WorldInteractionNode *pNode, *pNodeTargetOut = NULL;

					if (!gclAutoAttack_IsExplicitPowerEnabled(uiID))
						continue;

					ppow = character_FindPowerByID(pchar,uiID);
					pdefAuto = GET_REF(ppow->hDef);
					
					if(pdefAuto->bAutoAttackServer)
						continue;

					// Pick a target
					pTarget = clientTarget_SelectBestTargetForPower(pEntity,ppow,NULL);
					pEntTarget = pTarget ? entFromEntityRefAnyPartition(pTarget->entRef) : NULL;
					pNode = pTarget ? GET_REF(pTarget->hInteractionNode) : NULL;

					if (pEntTarget && powerdef_hasCategory(pdefAuto, g_CombatConfig.autoAttack.piPowerCategoriesCanceledByPredictedDeath) && 
						gclCombatDeathPrediction_IsDeathPredicted(pEntTarget))
						continue;

					// See if we can queue the Power with the given target
					ANALYSIS_ASSUME(ppow); // We checked this earlier when removing invalid Powers from the list
					if (g_CombatConfig.autoAttack.bProvideFeedbackOnAutoAttackFail)
					{
						bNoPowerFeedBack = s_iAutoAttackWarned;
					}

					ppowToQueue = character_CanQueuePower(entGetPartitionIdx(pEntity), pchar, ppow, pEntTarget, NULL, pNode, 
															NULL, &pEntTargetOut, &pNodeTargetOut, NULL, pmTimestamp(0), 
															-1, NULL, true, bNoPowerFeedBack, true, pExtract);


					if(ppowToQueue)
					{
						static F32 s_fTimer = -1.0f;
						static F32 s_fLastProcessTime = -1.0f;
						PowerDef *pdef = GET_REF(ppowToQueue->hDef);
						if(pdef && pdef->eType==kPowerType_Toggle && ppowToQueue->bActive)
						{
							// It's an active toggle Power, so we're not going to try and activate it
							//  This isn't quite the perfect check, since it could be a combo Power with
							//  some toggles and some non-toggles, but those switches are generally static
							//  so it's probably not worth the effort to check for them.
							continue;
						}
						
						// If auto attack delay is set, make sure we wait the extra time
						if (g_CurrentScheme.fAutoAttackDelay && s_fTimer > 0)
						{
							s_fTimer -= fTickTime;
						}
						else if (g_CurrentScheme.fAutoAttackDelay && s_fTimer < 0 && s_fLastProcessTime < 0)
						{
							s_fTimer = g_CurrentScheme.fAutoAttackDelay;
							s_fLastProcessTime = s_fTimer;
						}
						else
						{		
							bool bActivated; 
							s_fLastProcessTime = -1.0f;
							
							bActivated = character_ActivatePowerByIDClient(	entGetPartitionIdx(pEntity), 
																			pEntity->pChar, 
																			ppow->uiID, 
																			NULL, 
																			NULL, 
																			true, 
																			pExtract);

							// with the combatConfig bAutoAttackAllowInitialAttackToFinish
							// check if we successfully activated the power, and if it's the first autoattack
							// save the activation ID so we know not to cancel this activation if the player stops autoattack
							if (bActivated && g_CombatConfig.autoAttack.bAllowInitialAttackToFinish)
							{	
								if (s_iFirstAutoAttack)
								{
									s_iFirstAutoAttack = false;
									if (pEntity->pChar->pPowActQueued)
									{
										s_iInitialAutoAttackActivations[s_iInitialAutoAttackIdx] = pEntity->pChar->pPowActQueued->uchID;
										if (++s_iInitialAutoAttackIdx >= 2)
											s_iInitialAutoAttackIdx = 0;
									}
								}
							}
						}
						break;
					}
					else
					{
						s_iAutoAttackWarned = true;
					}
				}
			}
		}
	}
	s_bWasInCombatLastFrame = bIsPlayerInCombat;
	PERFINFO_AUTO_STOP_FUNC_PIX();
}

static int TargetChangeID;

static ClientTargetDef lastClientTarget;

void clientTarget_HandleServerChange(void)
{
	Entity *pEntPlayer = entActivePlayerPtr();

	if (!pEntPlayer || !pEntPlayer->pChar)
	{	
		TargetChangeID = 0;
		return;
	}

	if (pEntPlayer->pChar->targetChangeID > TargetChangeID)
	{
		TargetChangeID = pEntPlayer->pChar->targetChangeID;
	}
	else if (pEntPlayer->pChar->targetChangeID < TargetChangeID)
	{
		// If the server is out of date, reset change
		pEntPlayer->pChar->currentTargetRef = lastClientTarget.entRef;
		COPY_HANDLE(pEntPlayer->pChar->currentTargetHandle, lastClientTarget.hInteractionNode);
	
		pEntPlayer->pChar->targetChangeID = TargetChangeID;
	}
}

void clientTarget_ChangedClientTarget(void)
{
	Entity *pent;
	Entity *pEntPlayer = entActivePlayerPtr();	

	if (!pEntPlayer || !pEntPlayer->pChar)
	{
		TargetChangeID = 0;
		return;
	}
	TargetChangeID++;

	// Note to the UI that these need their UIs updated immediately
	if((pent = entFromEntityRefAnyPartition(lastClientTarget.entRef)) != 0)
	{
		pent->uiUpdateInactiveEntUI = -1;
	}
	if((pent = entFromEntityRefAnyPartition(pEntPlayer->pChar->currentTargetRef)) != 0)
	{
		pent->uiUpdateInactiveEntUI = -1;
	}

	lastClientTarget.entRef = pEntPlayer->pChar->currentTargetRef;
	COPY_HANDLE(lastClientTarget.hInteractionNode,pEntPlayer->pChar->currentTargetHandle);

	pEntPlayer->pChar->targetChangeID = TargetChangeID;
}

void clientTarget_ResetClientTarget(void)
{
	clientTarget_ResetTargetChangeTimer();
	lastClientTarget.entRef = 0;
	REMOVE_HANDLE(lastClientTarget.hInteractionNode);
	TargetChangeID = 0;
}

static int FocusTargetChangeID;

static ClientTargetDef lastClientFocusTarget;

void clientTarget_HandleServerFocusChange(void)
{
	Entity *pEntPlayer = entActivePlayerPtr();

	if (!pEntPlayer || !pEntPlayer->pChar)
	{	
		FocusTargetChangeID = 0;
		return;
	}

	if (pEntPlayer->pChar->focusTargetChangeID > FocusTargetChangeID)
	{
		FocusTargetChangeID = pEntPlayer->pChar->focusTargetChangeID;
	}
	else if (pEntPlayer->pChar->focusTargetChangeID < FocusTargetChangeID)
	{
		// If the server is out of date, reset change
		pEntPlayer->pChar->erTargetFocus = lastClientTarget.entRef;
		pEntPlayer->pChar->focusTargetChangeID = FocusTargetChangeID;
	}
}

void clientTarget_ChangedClientFocusTarget(void)
{
	Entity *pent;
	Entity *pEntPlayer = entActivePlayerPtr();	

	if (!pEntPlayer || !pEntPlayer->pChar)
	{
		FocusTargetChangeID = 0;
		return;
	}
	FocusTargetChangeID++;

	// Note to the UI that these need their UIs updated immediately
	if((pent = entFromEntityRefAnyPartition(lastClientFocusTarget.entRef)) != 0)
	{
		pent->uiUpdateInactiveEntUI = -1;
	}
	if((pent = entFromEntityRefAnyPartition(pEntPlayer->pChar->erTargetFocus)) != 0)
	{
		pent->uiUpdateInactiveEntUI = -1;
	}

	lastClientFocusTarget.entRef = pEntPlayer->pChar->erTargetFocus;
	pEntPlayer->pChar->focusTargetChangeID = FocusTargetChangeID;
}

void clientTarget_ResetClientFocusTarget(void)
{
	clientTarget_ResetTargetChangeTimer();
	lastClientFocusTarget.entRef = 0;
	FocusTargetChangeID = 0;
}

AUTO_STRUCT;
typedef struct CachedFrameTargetNode
{
	WorldInteractionNode*	pNode;		AST(UNOWNED LATEBIND)
	Vec3					vNear;
} CachedFrameTargetNode;

bool clientTarget_GetNearestPointForTargetNode(Entity* pPlayerEnt, WorldInteractionNode* pNodeTarget, Vec3 vNear)
{
	static CachedFrameTargetNode** s_eaFrameTargets = NULL; //could be made into a hash if the size gets big enough
	static U32 uiLastFrame = 0;
	U32 uiThisFrame;
	S32 i;
	CachedFrameTargetNode* pFrameTarget = NULL;

	if ( pPlayerEnt == NULL || pNodeTarget==NULL )
		return false;

	frameLockedTimerGetTotalFrames(gGCLState.frameLockedTimer, &uiThisFrame );

	if ( uiThisFrame != uiLastFrame )
	{
		for ( i = eaSize(&s_eaFrameTargets)-1; i >= 0; i-- )
		{
			s_eaFrameTargets[i]->pNode = NULL;
		}
		uiLastFrame = uiThisFrame;
	}

	if ( s_eaFrameTargets )
	{
		for ( i = eaSize(&s_eaFrameTargets)-1; i >= 0; i-- )
		{
			if ( s_eaFrameTargets[i]->pNode == pNodeTarget )
			{
				pFrameTarget = s_eaFrameTargets[i];
				break;
			}
			else if (pFrameTarget==NULL && s_eaFrameTargets[i]->pNode==NULL)
			{
				pFrameTarget = s_eaFrameTargets[i];
			}
		}
	}

	if ( pFrameTarget==NULL )
	{
		pFrameTarget = StructCreate( parse_CachedFrameTargetNode );
		eaPush( &s_eaFrameTargets, pFrameTarget );
	}

	if ( pFrameTarget->pNode==NULL )
	{
		pFrameTarget->pNode = pNodeTarget;

		character_FindNearestPointForObject(pPlayerEnt->pChar, NULL, pNodeTarget, pFrameTarget->vNear, true);
	}

	if ( vNear )
	{
		copyVec3( pFrameTarget->vNear, vNear );
	}

	return true;
}

AUTO_STRUCT;
typedef struct CachedFrameTargetLoS
{
	Entity*					pEntity;	AST(UNOWNED)
	WorldInteractionNode*	pNode;		AST(UNOWNED LATEBIND)
	bool					bCamera;
	bool					bLoS;
} CachedFrameTargetLoS;

bool clientTarget_IsTargetInLoS(Entity* pPlayerEnt, Vec3 vCamera, Entity* eTarget, WorldInteractionNode* pNodeTarget)
{
	static CachedFrameTargetLoS** s_eaFrameTargets = NULL; //could be made into a hash if the size gets big enough
	static U32 uiLastFrame = 0;
	U32 uiThisFrame;
	S32 i, bCamera = !pPlayerEnt;
	CachedFrameTargetLoS* pFrameTarget = NULL;
	
	if ( (pPlayerEnt==NULL && vCamera==NULL) || (eTarget==NULL && pNodeTarget==NULL) )
		return false;

	devassert(!!vCamera == bCamera); // Makes sure we only got one

	frameLockedTimerGetTotalFrames(gGCLState.frameLockedTimer, &uiThisFrame);

	if ( uiThisFrame != uiLastFrame )
	{
		for ( i = eaSize(&s_eaFrameTargets)-1; i >= 0; i-- )
		{
			s_eaFrameTargets[i]->pEntity = NULL;
			s_eaFrameTargets[i]->pNode = NULL;
			s_eaFrameTargets[i]->bCamera = false;
			s_eaFrameTargets[i]->bLoS = false;
		}
		uiLastFrame = uiThisFrame;
	}

	if ( s_eaFrameTargets )
	{
		for ( i = eaSize(&s_eaFrameTargets)-1; i >= 0; i-- )
		{
			if ( s_eaFrameTargets[i]->pEntity == eTarget && s_eaFrameTargets[i]->pNode == pNodeTarget && s_eaFrameTargets[i]->bCamera == bCamera)
			{
				pFrameTarget = s_eaFrameTargets[i];
				break;
			}
			else if (pFrameTarget==NULL && s_eaFrameTargets[i]->pEntity==NULL && s_eaFrameTargets[i]->pNode==NULL)
			{
				pFrameTarget = s_eaFrameTargets[i];
			}
		}
	}

	if ( pFrameTarget==NULL )
	{
		pFrameTarget = StructCreate( parse_CachedFrameTargetLoS );
		eaPush( &s_eaFrameTargets, pFrameTarget );
	}

	if ( pFrameTarget->pEntity==NULL && pFrameTarget->pNode==NULL )
	{
		Vec3 vSource, vTarget;
		
		pFrameTarget->pEntity = eTarget;
		pFrameTarget->pNode = pNodeTarget;

		if(bCamera)
		{
			pFrameTarget->bCamera = bCamera;
			copyVec3(vCamera,vSource);
		}
		else
		{
			entGetCombatPosDir( pPlayerEnt, NULL, vSource, NULL );
		}

		if (eTarget && GET_REF(eTarget->hCreatorNode))
		{
			pNodeTarget = GET_REF(eTarget->hCreatorNode);
			eTarget = NULL;
		}

		if(pPlayerEnt)
		{
			if(eTarget)
			{
				entGetCombatPosDir(eTarget,NULL,vTarget,NULL);
				pFrameTarget->bLoS = combat_CheckLoS( PARTITION_CLIENT, vSource, vTarget, pPlayerEnt, eTarget, NULL, false, false, NULL );
			}
			else
			{
				WorldInteractionEntry* pEntry = wlInteractionNodeGetEntry(pNodeTarget);
				clientTarget_GetNearestPointForTargetNode( pPlayerEnt, pNodeTarget, vTarget );
				pFrameTarget->bLoS = combat_CheckLoS( PARTITION_CLIENT, vSource, vTarget, pPlayerEnt, NULL, pEntry, false, false, NULL );
			}
		}
		else
		{
			// Camera check, do a more aggressive test, but only against entities, since camera tests against
			//  objects aren't really helpful.
			if(eTarget)
			{
				S32 bCapsules;
				Vec3 vEntPos;
				Quat qEntRot;
				Mat4 mCamera;
				const Capsule*const* capsules;
				F32 fRadius = 0;
				entGetPos(eTarget, vEntPos);
				if(bCapsules = mmGetCapsules(eTarget->mm.movement, &capsules))
				{
					entGetRot(eTarget, qEntRot);
					// Standard vertical capsules get 9-point test instead of 3-point
					if(capsules[0]->vDir[1] == 1)
					{
						fRadius = capsules[0]->fRadius * .75f;
						gfxGetActiveCameraMatrix(mCamera);
					}
				}

				// Check against top, center and root of main capsule, or lacking a capsule, root plus a few feet per step
				for(i=2; i>=0; i--)
				{
					Vec3 vTargetSub;
					if(bCapsules)
					{
						CapsuleMidlinePoint(capsules[0], vEntPos, qEntRot, i*.5f, vTargetSub);
					}
					else
					{
						copyVec3(vEntPos,vTargetSub);
						vTargetSub[1] += i*3;
					}

					if(pFrameTarget->bLoS = combat_CheckLoS(PARTITION_CLIENT, vSource, vTargetSub, NULL, eTarget, NULL, false, false, NULL))
						break;

					// If it's a vertical capsule, also check to left and right of the point in question
					if(fRadius)
					{
						moveVinX(vTargetSub,mCamera,-fRadius);
						if(pFrameTarget->bLoS = combat_CheckLoS(PARTITION_CLIENT, vSource, vTargetSub, NULL, eTarget, NULL, false, false, NULL))
							break;
						moveVinX(vTargetSub,mCamera,2*fRadius);
						if(pFrameTarget->bLoS = combat_CheckLoS(PARTITION_CLIENT, vSource, vTargetSub, NULL, eTarget, NULL, false, false, NULL))
							break;
					}
				}
			}
		}
	}

	return pFrameTarget->bLoS;
}

//this could be cached, but it looks like the function is pretty good about checking different targets
int clientTarget_CheckLOS(Entity *entSource, ClientTargetDef *pTarget)
{
	Vec3 vSource, vTarget;
	Entity *entTarget = entFromEntityRefAnyPartition(pTarget->entRef);
	WorldInteractionNode *pNode = GET_REF(pTarget->hInteractionNode);

	entGetCombatPosDir(entSource, NULL, vSource, NULL);

	if(entTarget)
	{
		pNode = GET_REF(entTarget->hCreatorNode);
		if (!pNode)
		{		
			entGetCombatPosDir(entTarget, NULL, vTarget, NULL);
		}
	}
	if(pNode)
	{
		character_FindNearestPointForObject(entSource->pChar,vSource,pNode,vTarget,true);
	}

	if(entTarget || pNode)
		return combat_CheckLoS(PARTITION_CLIENT, vSource,vTarget,entSource,entTarget,pNode ? wlInteractionNodeGetEntry(pNode): NULL,0,false,NULL);
	else
		return false;
}

//checks if the target in the correct angle arc; ang = cos(angle)
int clientTarget_CheckAngleArc(Entity *entSource, ClientTargetDef *pTarget, float ang, bool forward, bool rot90arc, bool bothArcs)
{
	Entity *entTarget = entFromEntityRefAnyPartition(pTarget->entRef);
	Vec3 vecSrc, vecTarget, vecLineTarget, vecEnt, vecLineEnt;
	WorldInteractionNode *pnodeTarget = NULL;
	float dot;

	if(!entTarget) return false;

	entGetPos(entSource,vecSrc);
	entGetPos(entTarget,vecTarget);
	subVec3(vecTarget,vecSrc,vecLineTarget);
	normalVec3(vecLineTarget);

	if(entTarget && IS_HANDLE_ACTIVE(entTarget->hCreatorNode))
	{
		pnodeTarget = GET_REF(entTarget->hCreatorNode);
		if(pnodeTarget)
			entTarget = NULL;
	}

	if(entTarget)
	{
		entGetCombatPosDir(entSource,NULL,vecEnt,vecLineEnt);
	}
	if(pnodeTarget)
	{
		character_FindNearestPointForObject(entSource?entSource->pChar:NULL,vecSrc,pnodeTarget,vecEnt,true);
		subVec3(vecEnt,vecSrc,vecLineEnt);
	}
	if (rot90arc)
	{
		dot = vecLineEnt[0];
		vecLineEnt[0] = vecLineEnt[2];
		vecLineEnt[2] = -dot;
	}
	dot = dotVec3(vecLineTarget,vecLineEnt)/lengthVec3(vecLineEnt);

	if (bothArcs)
	{
		return (fabs(dot) >= ang);
	}
	if (forward)
	{
		return (dot >= ang);
	}
	return (-dot >= ang);
}


//checks if the target in the correct angle arc 
static int clientTarget_IsTargetWithinDirAngleArc(	Entity *entSource, 
													ClientTargetDef *pTarget, 
													const Vec3 vDir, 
													F32 fAngle, 
													F32 *pfAngleBetweenOut)
{
	Entity *entTarget = entFromEntityRefAnyPartition(pTarget->entRef);
	Vec3 vSrc, vTarget, vToTarget;
	F32 angleBetween;
	
	if(!entTarget) 
		return false;

	entGetPos(entSource,vSrc);
	entGetPos(entTarget,vTarget);
	subVec3(vTarget,vSrc,vToTarget);
	angleBetween = getAngleBetweenVec3(vDir, vToTarget);
	
	if (pfAngleBetweenOut) *pfAngleBetweenOut = angleBetween;

	return angleBetween <= fAngle;
}


//specific check for Entity's target, calls clientTarget_IsTargetInLoS
bool clientTarget_IsMyTargetInLoS( Entity* pPlayerEnt )
{
	if ( pPlayerEnt )
	{
		Entity *eTarget = entFromEntityRefAnyPartition(pPlayerEnt->pChar->currentTargetRef);
		WorldInteractionNode *pNodeTarget = GET_REF(pPlayerEnt->pChar->currentTargetHandle);

		return clientTarget_IsTargetInLoS(pPlayerEnt, NULL, eTarget, pNodeTarget);
	}
	return false;
}

AUTO_EXPR_FUNC(entityutil) ACMD_NAME("PlayerGetLastAttackerRef");
U32 entExprPlayerGetLastAttackerRef( void )
{
	return s_erLastAttacker;
}

AUTO_STRUCT;
typedef struct ClientLuckyCharmInfo
{
	Entity*		pEnt;			AST(UNOWNED)
	S32 iType;
}ClientLuckyCharmInfo;

extern ParseTable parse_ClientLuckyCharmInfo[];
#define TYPE_parse_ClientLuckyCharmInfo ClientLuckyCharmInfo

// Activate a power by category
AUTO_EXPR_FUNC(UIGen) ACMD_NAME(PowerExecCategoryTarget);
void exprEntUsePowerCategoryOnTarget(int start, const char *cpchCategory, S32 targetRef)
{
	Entity *e = entActivePlayerPtr();
	Entity* pTarget = entFromEntityRefAnyPartition(targetRef);
	if(e && e->pChar)
	{
		Power *ppow = character_FindPowerByCategory(e->pChar,cpchCategory);
		if(ppow)
		{
			GameAccountDataExtract *pExtract = entity_GetCachedGameAccountDataExtract(e);
			if (!clientTarget_HasHardTarget(e) && g_CurrentScheme.bAutoHardTargetIfNoneExists && pTarget)
				clientTarget_SetTarget(pTarget);
			character_ActivatePowerByIDClient(entGetPartitionIdx(e), e->pChar, ppow->uiID, pTarget, NULL, start, pExtract);
		}
	}
}



#include "AutoGen/ClientTargeting_c_ast.c"