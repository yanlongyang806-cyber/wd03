/***************************************************************************
*     Copyright (c) 2006-2008, Cryptic Studios
*     All Rights Reserved
*     Confidential Property of Cryptic Studios
***************************************************************************/

#include "Character_Target.h"
#include "ClientTargeting.h"
#include "Entity.h"
#include "Expression.h"
#include "gclBaseStates.h"
#include "gclEntity.h"
#include "gclUIGen.h"
#include "GfxCamera.h"
#include "GraphicsLib.h"
#include "GlobalStateMachine.h"
#include "interactionClient.h"
#include "mechanics_common.h"
#include "Player.h"
#include "RegionRules.h"
#include "StringCache.h"
#include "StringUtil.h"
#include "WorldGrid.h"
#include "GameClientLib.h"

#include "ClientTargeting.h" //used for function clientTarget_InteractionNodeGetWindowScreenPos <-- Move this function>

#include "UIGen_h_ast.h"

AUTO_RUN_ANON(memBudgetAddMapping(__FILE__, BUDGET_UISystem););

static UIGen s_ObjFakeParentGen;
static StashTable s_stNodesToGens;

F32 exprEntGetHealthPercent(Entity* pEntity);
bool gclIsNodeDestructible(SA_PARAM_OP_VALID WorldInteractionNode *pNode);

static void ObjectGenRefresh(enumResourceEventType eType, const char *pDictName, const char *pchName, UIGen *pGen, void *pUserData)
{
	if (eType == RESEVENT_RESOURCE_MODIFIED && UI_GEN_IS_TYPE(pGen, kUIGenTypeObject))
	{
		// delete all object gens from whatever storage they're in.
	}
}

static void objui_Register(void)
{
	s_stNodesToGens = stashTableCreateAddress(64);
	StructInit(parse_UIGen, &s_ObjFakeParentGen);
	s_ObjFakeParentGen.pResult = StructCreate(parse_UIGenInternal);
	s_ObjFakeParentGen.bIsRoot = true;
	resDictRegisterEventCallback(UI_GEN_DICTIONARY, ObjectGenRefresh, NULL);
}

static UIGen *GetObjectGen(WorldInteractionNode *pNode, CBox *pScreen, ExprContext *pContext)
{
	WorldInteractionEntry *pEntry = wlInteractionNodeGetEntry(pNode);
	static const char *s_pchObjectVar;
	static const char *s_pchIsOnscreenVar;
	static const char *s_pchDistanceVar;
	static S32 s_iObjectVar;
	const char *pchGenName = NULL;
	F32 fScreenDist = -1;
	bool bShouldBeShown = false;
	bool bOnscreen = false;
	bool bFound;
	MultiVal mv;
	StashElement elem;

	if (!pEntry)
		return NULL;

	if (!s_pchObjectVar)
		s_pchObjectVar = allocFindString("Object");
	if (!s_pchIsOnscreenVar)
		s_pchIsOnscreenVar = allocFindString("IsOnscreen");
	if (!s_pchDistanceVar)
		s_pchDistanceVar = allocFindString("Distance");

	objGetScreenBoundingBox(pNode, &s_ObjFakeParentGen.ScreenBox, &fScreenDist, false, gProjectGameClientConfig.bUseObjectCenterAsBoundingBoxForObjectGens);

	if (fScreenDist >= ENTUI_MIN_FEET_FROM_CAMERA)
	{
		bOnscreen = true;
		CBoxClipTo(pScreen, &s_ObjFakeParentGen.ScreenBox);
	}

	if (g_ObjectGenOffscreenExpression.pExpression)
	{
		exprContextSetPointerVarPooledCached(pContext, s_pchObjectVar, pNode, parse_WorldInteractionNode, true, true, &s_iObjectVar);
		exprContextSetIntVarPooledCached(pContext, s_pchIsOnscreenVar, bOnscreen, NULL);
		exprContextSetFloatVarPooledCached(pContext, s_pchDistanceVar, fScreenDist, NULL);
		ui_GenTickMouse(&s_ObjFakeParentGen);
		exprEvaluate(g_ObjectGenOffscreenExpression.pExpression, pContext, &mv);
		bShouldBeShown = !!mv.intval;
	}
	else if (bOnscreen)
	{
		bShouldBeShown = (fScreenDist >= ENTUI_MIN_FEET_FROM_CAMERA) && (fScreenDist <= ENTUI_MAX_FEET_FROM_CAMERA);
		if (bShouldBeShown)
			exprContextSetPointerVarPooledCached(pContext, s_pchObjectVar, pNode, parse_WorldInteractionNode, true, true, &s_iObjectVar);
	}

	if (bShouldBeShown)
	{
		exprEvaluate(g_ObjectGenExpression.pExpression, pContext, &mv);
		pchGenName = allocFindString(MultiValGetString(&mv, NULL));
	}

	bFound = stashAddressFindElement(s_stNodesToGens, pNode, &elem);
	if (pchGenName && *pchGenName)
	{
		UIGen *pGen = NULL;
		if (bFound)
		{
			UIGenObjectState *pState;
			pGen = stashElementGetPointer(elem);
			pState = pGen ? UI_GEN_STATE(pGen, Object) : NULL;
			if (!(pGen && pGen->pchName == pchGenName && GET_REF(pState->hKey) == pNode))
			{
				UIGen *pToClone = RefSystem_ReferentFromString(UI_GEN_DICTIONARY, pchGenName);
				StructDestroySafe(parse_UIGen, &pGen);
				if (pToClone)
				{
					pGen = ui_GenClone(pToClone);
					stashElementSetPointer(elem, pGen);
				}
				else
					stashRemovePointer(s_stNodesToGens, pNode, NULL);
			}
		}
		else
		{
			UIGen *pToClone = RefSystem_ReferentFromString(UI_GEN_DICTIONARY, pchGenName);
			if (pToClone)
				pGen = ui_GenClone(pToClone);
			stashAddPointer(s_stNodesToGens, pNode, pGen, false);
		}
		if (pGen)
		{
			UIGenObjectState *pState = UI_GEN_STATE(pGen, Object);
			F32 fWidth = pGen->pBase->pos.Width.fMagnitude;
			F32 fHeight = pGen->pBase->pos.Height.fMagnitude;
			
			pState->fScreenDist = fScreenDist;
			if (pNode != GET_REF(pState->hKey))
			{
				SET_HANDLE_FROM_REFERENT(INTERACTION_DICTIONARY, pNode, pState->hKey);
			}

			if (bOnscreen)
			{
				pGen->ScreenBox = s_ObjFakeParentGen.ScreenBox;
			}
			else
			{
				GfxCameraView *pView = gfxGetActiveCameraView();
				Vec3 vPos;
				wlInteractionNodeGetWorldMid(pNode, vPos);
				ProjectCBoxOnScreen(vPos, pView, &pGen->ScreenBox, pScreen, fWidth, fHeight);
			}
		}
		return pGen;
	}
	else if (bFound)
	{
		UIGen *pGen = stashElementGetPointer(elem);
		StructDestroySafe(parse_UIGen, &pGen);
		stashElementSetPointer(elem, NULL);
	}
	return NULL;
}

static void RunGenForNodes(WorldInteractionNode **ppNodes)
{
	static bool s_bInit = false;
	CBox ScreenBox = {0, 0, 0, 0};
	S32 iScreenWidth;
	S32 iScreenHeight;
	static UIGen **s_eaGens;
	ExprContext *pContext;
	int i;
	
	if ( ppNodes==NULL )
		return;

	gfxGetActiveSurfaceSize(&iScreenWidth, &iScreenHeight);
	ScreenBox.hx = iScreenWidth;
	ScreenBox.hy = iScreenHeight;

	if (!s_bInit)
	{
		objui_Register();
		s_bInit = true;
	}

	PERFINFO_AUTO_START("RunGenForNodes: Updating Object Gens", eaSize(&ppNodes));
	eaClearFast(&s_eaGens);
	pContext = ui_GenGetContext(&s_ObjFakeParentGen);
	for(i = 0; i < eaSize(&ppNodes); i++)
	{
		UIGen *pGen = GetObjectGen(ppNodes[i], &ScreenBox, pContext);
		if (pGen)
			eaPush(&s_eaGens, pGen);
	}
	PERFINFO_AUTO_STOP();

	PERFINFO_AUTO_START("RunGenForNodes: Object Gen Layout", eaSize(&s_eaGens));
	for (i = 0; i < eaSize(&s_eaGens); i++)
	{
		s_ObjFakeParentGen.ScreenBox = s_eaGens[i]->ScreenBox;
		ui_GenPointerUpdateCB(s_eaGens[i], &s_ObjFakeParentGen);
		ui_GenUpdateCB(s_eaGens[i], &s_ObjFakeParentGen);
		ui_GenLayoutCB(s_eaGens[i], &s_ObjFakeParentGen);
	}
	PERFINFO_AUTO_STOP();

	PERFINFO_AUTO_START("RunGenForNodes: Object Gen Tick", eaSize(&s_eaGens));
	for (i = 0; i < eaSize(&s_eaGens); i++)
		ui_GenTickCB(s_eaGens[i], &s_ObjFakeParentGen);
	PERFINFO_AUTO_STOP();

	PERFINFO_AUTO_START("RunGenForNodes: Object Gen Draw", eaSize(&s_eaGens));
	for (i = eaSize(&s_eaGens) - 1; i >= 0; i--)
		ui_GenDrawCB(s_eaGens[i], &s_ObjFakeParentGen);
	PERFINFO_AUTO_STOP();
}

static int SortNodesByDistance(Vec3 v3Center, const WorldInteractionNode **ppNode1, const WorldInteractionNode **ppNode2)
{
	const WorldInteractionNode *pNode1 = *ppNode1;
	const WorldInteractionNode *pNode2 = *ppNode2;
	Vec3 v3Pos1;
	Vec3 v3Pos2;
	wlInteractionNodeGetWorldMid(pNode1, v3Pos1);
	wlInteractionNodeGetWorldMid(pNode2, v3Pos2);
	{
		F32 fDist1 = distance3Squared(v3Center, v3Pos1);
		F32 fDist2 = distance3Squared(v3Center, v3Pos2);
		return (fDist1 < fDist2) ? -1 : ((fDist1 > fDist2) ? 1 : 0);
	}
}

void gclDrawStuffOverObjects(void)
{
	Entity* pPlayerEnt = entActivePlayerPtr();
	U32 i, iSize;
	static WorldInteractionNode **s_ppNodes = NULL;
	RegionRules* pRules;
	bool bShowClickableGens;

	Vec3 vEntPos;
	StashTableIterator iter;
	StashElement elem;

	if (!GSM_IsStateActive(GCL_GAMEPLAY) || GSM_DoesStateHaveChildStates(GCL_GAMEPLAY))
		return;

	if ( !pPlayerEnt )
		return;

	pRules = getRegionRulesFromRegionType( entGetWorldRegionTypeOfEnt(pPlayerEnt) );
	bShowClickableGens = pRules ? pRules->bClickablesTargetable : true;

	entGetPos( pPlayerEnt, vEntPos );

	PERFINFO_AUTO_START("gclDrawStuffOverObjects: Object Gen \"Organization\"", 1);
	eaClearFast(&s_ppNodes);

	iSize = eaUSize( &pPlayerEnt->pPlayer->InteractStatus.ppTargetableNodes );
	for ( i = 0; i < iSize; i++ )
	{
		WorldInteractionNode* pNode = GET_REF( pPlayerEnt->pPlayer->InteractStatus.ppTargetableNodes[i]->hNode );
		if ( pNode && (bShowClickableGens || gclIsNodeDestructible(pNode)) )
			eaPush( &s_ppNodes, pNode );
	}
	iSize = eaUSize( &pPlayerEnt->pPlayer->InteractStatus.ppDoorStatusNodes );
	for ( i = 0; i < iSize; i++ )
	{
		WorldInteractionNode* pNode = GET_REF( pPlayerEnt->pPlayer->InteractStatus.ppDoorStatusNodes[i]->hNode );
		if ( pNode )
			eaPush( &s_ppNodes, pNode );
	}

	if ( s_ppNodes )
		eaQSort_s(s_ppNodes, SortNodesByDistance, vEntPos);
	
	stashGetIterator(s_stNodesToGens, &iter);
	while (stashGetNextElement(&iter, &elem))
	{
		WorldInteractionNode *pNode = stashElementGetKey(elem);
		UIGen *pGen = stashElementGetPointer(elem);
		if (!pGen || !pNode || eaFind(&s_ppNodes, pNode) < 0)
		{
			stashRemovePointer(s_stNodesToGens, pNode, NULL);
			if (pGen)
				StructDestroySafe(parse_UIGen, &pGen);
		}
	}
	PERFINFO_AUTO_STOP();

	RunGenForNodes(s_ppNodes);
}

AUTO_EXPR_FUNC(UIGen) ACMD_NAME(NodeGetName);
const char *gclNodeGetName(SA_PARAM_OP_VALID WorldInteractionNode *pNode)
{

	WorldInteractionEntry *pEntry = pNode ? wlInteractionNodeGetEntry(pNode) : NULL;
	const char *holder = NULL;

	if(pEntry)
		holder = TranslateMessagePtr(GET_REF(pEntry->base_interaction_properties->hDisplayNameMsg));
	if(!holder)
		return TranslateMessageKeyDefault("MechanicsUI.DefaultObject", "Object");
	return holder;
}

// Returns true if the given WorldInteractionNode is the player's soft target
AUTO_EXPR_FUNC(UIGen) ACMD_NAME(IsNodeCurrentSoftTarget);
bool gclIsNodeCurrentSoftTarget(SA_PARAM_NN_VALID WorldInteractionNode *pNode)
{
	const ClientTargetDef *pCurTarget = clientTarget_GetCurrentTarget();
	if (pCurTarget && IS_HANDLE_ACTIVE(pCurTarget->hInteractionNode) && !clientTarget_IsTargetHard())
	{
		if (pNode == GET_REF(pCurTarget->hInteractionNode))
			return true;
	}
	return false;
}

AUTO_EXPR_FUNC(UIGen) ACMD_NAME(IsNodeTargetable);
bool gclIsNodeTargetable(SA_PARAM_OP_VALID Entity* pPlayerEnt, SA_PARAM_NN_VALID WorldInteractionNode *pNode)
{
	if ( pPlayerEnt==NULL )
		return false;

	return target_IsObjectTargetable(pPlayerEnt,pNode);
}

AUTO_EXPR_FUNC(UIGen) ACMD_NAME(IsMouseOverNode);
bool gclIsMouseOverNode(SA_PARAM_OP_VALID Entity* pPlayerEnt, SA_PARAM_NN_VALID WorldInteractionNode *pNode)
{
	return pPlayerEnt && 
		pPlayerEnt->pPlayer && 
		GET_REF(pPlayerEnt->pPlayer->InteractStatus.hOverrideNode) == pNode;
}

AUTO_EXPR_FUNC(UIGen) ACMD_NAME(IsNodeDestructible);
bool gclIsNodeDestructible(SA_PARAM_OP_VALID WorldInteractionNode *pNode)
{
	WorldInteractionEntry *pEntry = pNode ? wlInteractionNodeGetEntry(pNode) : NULL;
	if ( pEntry )
	{
		return wlInteractionBaseIsDestructible( pEntry->base_interaction_properties );
	}
	return false;
}

AUTO_EXPR_FUNC(UIGen) ACMD_NAME(NodeGetDistanceFromPlayer);
F32 gclNodeGetDistanceFromPlayer(SA_PARAM_OP_VALID Entity* pPlayerEnt, SA_PARAM_NN_VALID WorldInteractionNode *pNode)
{
	Vec3 vClose;

	if ( pPlayerEnt==NULL )
		return 0;

	character_FindNearestPointForObject(pPlayerEnt->pChar,NULL,pNode,vClose,true);

	return entConvertUOM(pPlayerEnt,entGetDistance(pPlayerEnt,NULL,NULL,vClose,NULL),NULL,false);
}

AUTO_EXPR_FUNC(UIGen) ACMD_NAME("NodeGetDistanceFromPlayerAsString");
const char *gclNodeGetDistanceFromPlayerAsString(SA_PARAM_NN_VALID Entity *pPlayerEnt, SA_PARAM_NN_VALID WorldInteractionNode *pNode)
{
	if (pPlayerEnt && pPlayerEnt->pChar)
	{
		static char pchBuffer[260];
		const char* pchUnits = NULL;
		Vec3 vClose;
		F32 fDistance;
		character_FindNearestPointForObject(pPlayerEnt->pChar,NULL,pNode,vClose,true);
		fDistance = entConvertUOM(pPlayerEnt,entGetDistance(pPlayerEnt,NULL,NULL,vClose,NULL),&pchUnits,true);
		StringFormatNumberSignificantDigits(pchBuffer, fDistance, 3, true, true);
		sprintf(pchBuffer,"%s %s", pchBuffer, pchUnits);
		return pchBuffer;
	}
	return "";
}

// Returns true if the given WorldInteractionNode is the player's hard target
AUTO_EXPR_FUNC(UIGen) ACMD_NAME(IsNodeCurrentHardTarget);
bool gclIsNodeCurrentHardTarget(SA_PARAM_NN_VALID WorldInteractionNode *pNode)
{
	const ClientTargetDef *pCurTarget = clientTarget_GetCurrentTarget();
	WorldInteractionNode *pHolder = NULL;
	if (pCurTarget && IS_HANDLE_ACTIVE(pCurTarget->hInteractionNode) && clientTarget_IsTargetHard())
	{
		if (pNode == GET_REF(pCurTarget->hInteractionNode))
			return true;
	}
	return false;
}
AUTO_EXPR_FUNC(UIGen) ACMD_NAME("NodeMatchesType");
bool gclNodeMatchesType( SA_PARAM_OP_VALID WorldInteractionNode* pNode, const char* pchType )
{
	return wlInteractionClassMatchesMask( pNode, wlInteractionClassNameToBitMask( pchType ) );
}

AUTO_EXPR_FUNC(UIGen) ACMD_NAME("NodeRefMatchesType");
bool gclNodeRefMatchesType( const char* pchNodeRef, const char* pchType )
{
	return gclNodeMatchesType( RefSystem_ReferentFromString(INTERACTION_DICTIONARY, pchNodeRef), pchType );
}

AUTO_EXPR_FUNC(UIGen) ACMD_NAME("NodeRayCanHit");
bool gclNodeRayCanHit( SA_PARAM_OP_VALID WorldInteractionNode* pNode )
{
	Entity* pEnt = entActivePlayerPtr();
	if ( pEnt && pNode )
	{
		if ( pNode == target_SelectObjectUnderMouse(pEnt, 0) )
		{
			return true;
		}
	}
	return false;
}

AUTO_EXPR_FUNC(UIGen) ACMD_NAME("NodeRefRayCanHit");
bool gclNodeRefRayCanHit( const char* pchNodeRef )
{
	return gclNodeRayCanHit( RefSystem_ReferentFromString(INTERACTION_DICTIONARY, pchNodeRef) );
}

AUTO_EXPR_FUNC(UIGen) ACMD_NAME(GetNodeHealth);
float gclGetNodeHealth(SA_PARAM_NN_VALID WorldInteractionNode *pNode, SA_PARAM_OP_VALID Entity *pEnt)
{	
	if (!pEnt)
		return 1.0f;
	else
		return exprEntGetHealthPercent(pEnt);
}


AUTO_EXPR_FUNC(UIGen) ACMD_NAME(IsDoorCollectDestStatusNode);
bool gclIsDoorCollectDestStatusNode(SA_PARAM_NN_VALID WorldInteractionNode *pNode)
{
	Entity* pEnt = entActivePlayerPtr();
	const char* pchNodeKey = RefSystem_StringFromReferent(pNode);

	if ( pEnt==NULL || pEnt->pPlayer==NULL || pchNodeKey==NULL )
		return false;

	return eaIndexedFindUsingString(&pEnt->pPlayer->InteractStatus.ppDoorStatusNodes,pchNodeKey) >= 0;
}