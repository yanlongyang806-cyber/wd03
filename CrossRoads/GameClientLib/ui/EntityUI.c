/***************************************************************************
*     Copyright (c) 2006-2008, Cryptic Studios
*     All Rights Reserved
*     Confidential Property of Cryptic Studios
***************************************************************************/

#include "GlobalStateMachine.h"
#include "Expression.h"
#include "estring.h"
#include "rand.h"
#include "StringCache.h"

#include "Entity.h"
#include "Entity_h_ast.h"
#include "EntitySavedData.h"
#include "EntitySavedData_h_ast.h"
#include "entity/EntityClient.h"
#include "EntityIterator.h"
#include "EntityLib.h"
#include "entCritter.h"
#include "Character.h"
#include "Character_Target.h"
#include "Player.h"
#include "Player_h_ast.h"
#include "PowersMovement.h"
#include "PowerActivation.h"
#include "RegionRules.h"
#include "GameClientLib.h"
#include "gclControlScheme.h"
#include "gclEntity.h"
#include "gclNotify.h"
#include "gclUIGen.h"
#include "wlInteraction.h"
#include "CharacterAttribs.h"
#include "DamageTracker.h"
#include "DamageTracker_h_ast.h"
#include "interactionClient.h"
#include "CharacterClass.h"
#include "WorldGrid.h"
#include "Guild.h"

#include "inputLib.h"
#include "inputMouse.h"

#include "UIGen_h_ast.h"

#include "GraphicsLib.h"
#include "GfxDebug.h"
#include "GfxSpriteText.h"
#include "GfxHeadshot.h"

#include "gclBaseStates.h"

#include "Combat/ClientTargeting.h"

#include "gclUIGen.h"
#include "gclUtils.h"

#include "chat/gclChatLog.h"
#include "chatCommonStructs.h"

#include "DamageFloaters.h"
#include "ChatBubbles.h"
#include "PowersAutoDesc.h"

#include "Team.h"
#include "gclCamera.h"

#include "SharedBankCommon.h"

#include "entEnums_h_ast.h"
#include "AutoGen/GameServerLib_autogen_ServerCmdWrappers.h"

AUTO_RUN_ANON(memBudgetAddMapping(__FILE__, BUDGET_UISystem););

static bool s_bHideEntityHUD = false;
static bool s_bHideChatBubbles = false;

static EntityRef s_iCritterDescriptionRef = 0;
static char *s_estrCritterDescription = NULL;

static EntityRef s_iCritterGroupDescriptionRef = 0;
static char *s_estrCritterGroupDescription = NULL;

static bool s_bDisableEntityHUD = false;
static bool s_bDisableChatBubbles = false;

static UIGen s_EntFakeParentGen;

static U32 s_uiEntityGenCheckInterval = 500;

CombatLogClientFilters g_CombatLogFilters = {0};

// Whether to show or hide information above entity's heads, except for chat bubbles / damage counters.
// This is used by demo recording and testing scripts.
AUTO_CMD_INT(s_bDisableEntityHUD, DisableEntityHUD) ACMD_ACCESSLEVEL(9) ACMD_CATEGORY(Interface) ACMD_CMDLINEORPUBLIC;

// Whether to show/hide chat bubbles.
// This is used by demo recording and testing scripts.
AUTO_CMD_INT(s_bDisableChatBubbles, DisableChatBubbles) ACMD_ACCESSLEVEL(9) ACMD_CATEGORY(Interface) ACMD_CMDLINEORPUBLIC;

// Whether to show or hide information above entity's heads, except for chat bubbles / damage counters.
AUTO_CMD_INT(s_bHideEntityHUD, HideEntityHUD) ACMD_ACCESSLEVEL(0) ACMD_CATEGORY(Interface) ACMD_HIDE;

// Whether to show/hide chat bubbles.
AUTO_CMD_INT(s_bHideChatBubbles, HideChatBubbles) ACMD_ACCESSLEVEL(0) ACMD_CATEGORY(Interface) ACMD_HIDE;

// Re-run expressions for visible non-UI-having entities this often.
AUTO_CMD_INT(s_uiEntityGenCheckInterval, EntityGenCheckInterval) ACMD_ACCESSLEVEL(0) ACMD_CATEGORY(Interface) ACMD_HIDE;

#define ShowChatBubbles() (!(s_bDisableChatBubbles || s_bHideChatBubbles))
#define ShowEntityHUD() (!(s_bDisableEntityHUD|| s_bHideEntityHUD))

void gclCombatDebugMeters_Notify(CombatTrackerNet *pNet);



AUTO_RUN;
void EntityTypes_Init(void)
{
	ui_GenInitStaticDefineVars(PlayerTypeEnum, "PlayerType");
}

AUTO_STARTUP(EntityUI) ASTRT_DEPS(AS_CharacterClassTypes);
void gclEntUIStartup(void)
{
	if(!gbNoGraphics)
	{
		ui_GenInitStaticDefineVars(CharClassCategoryEnum, "ClassCategory_");
	}

	loadstart_printf("Loading CombatLogFilters...");
	ParserLoadFiles(NULL, "defs/config/CombatLogFilters.def", "CombatLogFilters.bin", PARSER_OPTIONALFLAG, parse_CombatLogClientFilters, &g_CombatLogFilters);
	loadend_printf("done");
}

static bool gclCheckCombatTrackerAgainstClientFilters(CombatTrackerNet* pTracker)
{
	int i;

	if (pTracker->eFlags & kCombatTrackerFlag_ShowPowerDisplayName)
		return true;

	for (i = 0; i < eaSize(&g_CombatLogFilters.ppFilters); i++)
	{
		if (attrib_Matches(pTracker->eType, g_CombatLogFilters.ppFilters[i]->eAttrib))
			return false;
	}
	return true;
}

static void EntityGenRefresh(enumResourceEventType eType, const char *pDictName, const char *pchName, UIGen *pGen, void *pUserData)
{
	if (eType == RESEVENT_RESOURCE_MODIFIED && UI_GEN_IS_TYPE(pGen, kUIGenTypeEntity))
	{
		Entity *pEnt;
		EntityIterator *pIter = entGetIteratorAllTypesAllPartitions(0, 0);
		while ((pEnt = EntityIteratorGetNext(pIter)) && pEnt->pEntUI)
		{
			StructDestroySafe(parse_UIGen, &pEnt->pEntUI->pGen);
		}
		EntityIteratorRelease(pIter);
	}
}

static void entui_Register(void)
{
	StructInit(parse_UIGen, &s_EntFakeParentGen);
	s_EntFakeParentGen.pResult = StructCreate(parse_UIGenInternal);
	s_EntFakeParentGen.bIsRoot = true;
	resDictRegisterEventCallback(UI_GEN_DICTIONARY, EntityGenRefresh, NULL);
}

// this function actually updates the ScreenBox of the entity gen in s_EntFakeParentGen
// returns whether entity is on-screen
static bool UpdateEntityGenBox(Entity *pEnt, ExprContext *pContext, CBox *pScreen, F32* pfScreenDist, bool bOverheadEntityGens)
{
	GfxCameraView *pView = gfxGetActiveCameraView();
	CBox *pBox = &s_EntFakeParentGen.ScreenBox;
	WorldInteractionNode* pCreatorNode = GET_REF(pEnt->hCreatorNode);
	Entity* pPlayer = entActivePlayerPtr(); 
	S32 iWidth, iHeight;
	Vec3 v3Min, v3Max;
	Mat4 mEntWorld;
	bool bOnScreen = false;
	bool bClampToMinInteractSize = true;

	PERFINFO_AUTO_START_FUNC_PIX();

	gfxGetActiveSurfaceSize(&iWidth, &iHeight);

	(*pfScreenDist) = -1.0f; // Only set this to a positive value if pBox is OnScreen

	//If entity is in a different region, point to a door to that region (if 
	//possible).  This is mostly for teammates.
	if(pPlayer->astrRegion != pEnt->astrRegion){
		entGetLocalBoundingBox(pEnt, v3Min, v3Max, false);
		identityMat3(mEntWorld);
		entGetPosClampedToRegion(pPlayer, pEnt, mEntWorld[3]);
	}
	else{
		entGetLocalBoundingBox(pEnt, v3Min, v3Max, false);
		entGetVisualMat(pEnt, mEntWorld);
	}

	if ( pCreatorNode )
	{
		//if there is a creator node, use its bounds instead so that transitions from destructible to entity aren't jarring
		objGetScreenBoundingBox(pCreatorNode, pBox, pfScreenDist, false, false);
	}
	else if (bOverheadEntityGens)
	{
		Vec3 vTop = {0};
		Vec3 vBonePos;
		Vec3 vBonePosVS; // view space
		Vec3 vCapsuleMin,  vCapsuleMax;
				static Entity * pTargetEnt= 0;

		if (!entGetBonePos(pEnt,gConf.pchOverheadGenBone,vBonePos))
		{
			entGetPos(pEnt,vBonePos);
		}
		mulVecMat4(vBonePos, pView->frustum.viewmat, vBonePosVS);
		copyVec3(vBonePos,vTop);

		if (entGetPrimaryCapsuleWorldSpaceBounds(pEnt, vCapsuleMin, vCapsuleMax))
		{
			vTop[1] = vCapsuleMax[1];
		}

		if( gfxWorldToScreenSpaceVector(pView, vTop, vTop, false /*bClamp*/) )
		{
			*pfScreenDist = -vBonePosVS[2];
		}

		pBox->left = floorf(vTop[0]-40.0f);
		pBox->right = floorf(vTop[0]+40.0f);
		pBox->top = floorf(vTop[1]-20.0f);
		pBox->bottom = floorf(vTop[1]);

		bClampToMinInteractSize = false;

		if (pEnt == pTargetEnt)
		{
			printf("(%f,%f),(%f,%f) - iWidth: %f, iHeight: %f\n",pBox->lx,pBox->ly,pBox->hx,pBox->hy,pBox->hx-pBox->lx,pBox->hy-pBox->ly);
		}
	}
	else
	{
		Vec2 v2Min, v2Max;

		if (gfxCapsuleGetScreenExtents(&pView->frustum, 
										pView->projection_matrix, 
										mEntWorld, 
										v3Min, 
										v3Max, 
										v2Min, 
										v2Max, 
										pfScreenDist, 
										true,
										false))
		{
			pBox->lx = v2Min[0] * iWidth;
			pBox->hx = v2Max[0] * iWidth;
			pBox->ly = iHeight - v2Min[1] * iHeight;
			pBox->hy = iHeight - v2Max[1] * iHeight;
			CBoxNormalize(pBox);
		}
	}

	if (bClampToMinInteractSize)
	{
		target_ClampToMinimumInteractBox( pBox );
	}

	// If the target is in front of the camera.
	if ((*pfScreenDist) >= ENTUI_MIN_FEET_FROM_CAMERA)
	{
		// If we don't do some sort of clipping here, entity gens can get very huge
		// and trip the sanity check during rendering. 
		// Entity gens are allowed to go off screen, so clip outside that area.
		CBox SanityBox =  { -iWidth, -iHeight, 2 * iWidth , 2 * iHeight };
		CBoxClipTo(&SanityBox, pBox);
		PERFINFO_AUTO_STOP_FUNC_PIX();
		return true;
	}
	
	if ( gConf.bManageOffscreenGens )
	{
		Vec3 vPos;
		entGetPosClampedToRegion(pPlayer,pEnt,vPos);
		bOnScreen = ProjectCBoxOnScreen(vPos, pView, pBox, pScreen, 1, 1);
	}

	CBoxClipTo(pScreen, pBox);

	PERFINFO_AUTO_STOP_FUNC_PIX();
	return bOnScreen;
}

static bool gclEntity_CleanupEntUI(SA_PARAM_NN_VALID Entity* pEnt)
{
	if (pEnt->pEntUI && pEnt->pEntUI->pGen)
	{
		StructDestroySafe(parse_UIGen, &pEnt->pEntUI->pGen);
		pEnt->uiUpdateInactiveEntUI = 0;
		return true;
	}
	return false;
}

static const char* CheckShouldUpdateEntityGen(Entity *pPlayerEnt, Entity *pEnt, ExprContext *pContext,
											  F32 fScreenDist, bool bOnScreen)
{
	bool bShouldBeShown = false;
	static S32 s_iEntityVar;
	MultiVal mv;
	static const char *s_pchEntityVar;
	static const char *s_pchIsOnscreenVar;
	static const char *s_pchDistanceVar;

	// If the entity wasn't onscreen before but is now, force a refresh.
	// Otherwise there's a random delay before the UI appears.
	if (bOnScreen && pEnt->pEntUI && !pEnt->pEntUI->bWasOnscreen)
		pEnt->uiUpdateInactiveEntUI = 0;
	if (pEnt->pEntUI)
		pEnt->pEntUI->bWasOnscreen = bOnScreen;
	// If there's no UI for a particular entity, don't check again every frame.
	// Unless the mouse is over it.
	if(pEnt->uiUpdateInactiveEntUI == -1)
		pEnt->uiUpdateInactiveEntUI = 0;
	else if (!(bOnScreen && mouseCollision(&s_EntFakeParentGen.ScreenBox)) && pEnt->uiUpdateInactiveEntUI > g_ui_State.totalTimeInMs)
		return NULL;
	else if (!(pEnt->pEntUI && pEnt->pEntUI->pGen))
	{
		pEnt->uiUpdateInactiveEntUI += s_uiEntityGenCheckInterval;
		// If this still doesn't catch us up, assign to a random time in the future,
		// to prevent lock-step updates.
		if (pEnt->uiUpdateInactiveEntUI <= g_ui_State.totalTimeInMs)
			pEnt->uiUpdateInactiveEntUI = g_ui_State.totalTimeInMs + randomPositiveF32() * s_uiEntityGenCheckInterval;
	}

	if (!s_pchEntityVar)
		s_pchEntityVar = allocAddString("Entity");
	if (!s_pchIsOnscreenVar)
		s_pchIsOnscreenVar = allocAddString("IsOnscreen");
	if (!s_pchDistanceVar)
		s_pchDistanceVar = allocAddString("Distance");

	if (gConf.bIgnoreEntityGenOffscreenExpression)
	{
		exprContextSetPointerVarPooledCached(pContext, s_pchEntityVar, pEnt, parse_Entity, true, true, &s_iEntityVar);
		exprContextSetIntVarPooledCached(pContext, s_pchIsOnscreenVar, bOnScreen, NULL);
		exprContextSetFloatVarPooledCached(pContext, s_pchDistanceVar, fScreenDist, NULL);
		bShouldBeShown = 1;
	}
	else if (g_EntityGenOffscreenExpression.pExpression)
	{
		exprContextSetPointerVarPooledCached(pContext, s_pchEntityVar, pEnt, parse_Entity, true, true, &s_iEntityVar);
		exprContextSetIntVarPooledCached(pContext, s_pchIsOnscreenVar, bOnScreen, NULL);
		exprContextSetFloatVarPooledCached(pContext, s_pchDistanceVar, fScreenDist, NULL);
		exprEvaluate(g_EntityGenOffscreenExpression.pExpression, pContext, &mv);
		bShouldBeShown = !!mv.intval;
	}
	else if (bOnScreen)
	{
		exprContextSetPointerVarPooledCached(pContext, s_pchEntityVar, pEnt, parse_Entity, true, true, &s_iEntityVar);
		bShouldBeShown = ((fScreenDist >= ENTUI_MIN_FEET_FROM_CAMERA) && (fScreenDist <= ENTUI_MAX_FEET_FROM_CAMERA))
			|| team_OnSameTeam(pPlayerEnt, pEnt);
	}

	if (bShouldBeShown)
	{
		const char* pchGenName;
		s_EntFakeParentGen.UnpaddedScreenBox = s_EntFakeParentGen.ScreenBox;
		ui_GenTickMouse(&s_EntFakeParentGen);
		exprEvaluate(g_EntityGenExpression.pExpression, pContext, &mv);
		pchGenName = MultiValGetString(&mv, NULL);

		if (pEnt->pEntUI && pEnt->pEntUI->pGen && (!pchGenName || stricmp(pEnt->pEntUI->pGen->pchName, pchGenName)))
		{
			gclEntity_CleanupEntUI(pEnt);
		}

		if (pchGenName && *pchGenName)
		{
			pEnt->uiUpdateInactiveEntUI = 0;
		}

		return pchGenName;
	}
	else
	{
		gclEntity_CleanupEntUI(pEnt);
	}
	return NULL;
}

static UIGen *UpdateEntityGenInternal(Entity *pPlayerEnt, Entity *pEnt, CBox *pScreen, F32 fScreenDist, 
									  bool bOnScreen, EncounterUIData* pEncData, OffscreenUIData* pOffscreenData,
									  Vec2 vPos, const char* pchGenName)
{
	S32 i;

	if (pchGenName && *pchGenName)
	{
		if (!pEnt->pEntUI)
			pEnt->pEntUI = StructCreate(parse_EntityUI);
		if (!pEnt->pEntUI->pGen)
			pEnt->pEntUI->pGen = StructCloneFields(parse_UIGen, RefSystem_ReferentFromString(UI_GEN_DICTIONARY, pchGenName));
		if (pEnt->pEntUI->pGen)
		{
			UIGenEntityState *pState = UI_GEN_STATE(pEnt->pEntUI->pGen, Entity);

			pEnt->pEntUI->pEncounterData = pEncData;
			pEnt->pEntUI->pOffscreenData = pOffscreenData;

			if (!bOnScreen)
			{
				F32 fWidth = pEnt->pEntUI->pGen->pBase->pos.Width.fMagnitude;
				F32 fHeight = pEnt->pEntUI->pGen->pBase->pos.Height.fMagnitude;

				if ( vPos )
				{
					CreateScreenBoxFromScreenPosition(&pEnt->pEntUI->pGen->ScreenBox, pScreen, vPos, fWidth, fHeight);
				}
				else
				{
					GfxCameraView *pView = gfxGetActiveCameraView();
					Vec3 vEntPos;
					entGetPos(pEnt,vEntPos);
					ProjectCBoxOnScreen(vEntPos, pView, &pEnt->pEntUI->pGen->ScreenBox, pScreen, fWidth, fHeight);
				}

				// arbitrary distance to make sure it passes further cull checks
				// (though there shouldn't be any)
				fScreenDist = ENTUI_MIN_FEET_FROM_CAMERA + 1.f;
			}
			else
			{
				pEnt->pEntUI->pGen->ScreenBox = s_EntFakeParentGen.ScreenBox;
			}
			pState->hEntity = entGetRef(pEnt);
			pState->fScreenDist = fScreenDist;

			// Copy server variables into gen variables.
			for (i = 0; i < eaSize(&pEnt->UIVars); i++)
			{
				UIVar *pVar = pEnt->UIVars[i];
				UIGenVarTypeGlob *pGlob;

				//Filter out old var names and only copy the newer vars into gens
				//TODO: this should be done when loading the UIVars instead of during update
				if ( !stricmp( pVar->pchName, "BombExplode" ) )
				{
					pGlob = eaIndexedGetUsingString(&pEnt->pEntUI->pGen->eaVars, "Timer");
				}
				else if ( !stricmp( pVar->pchName, "BombArmProgress" ) )
				{
					pGlob = eaIndexedGetUsingString(&pEnt->pEntUI->pGen->eaVars, "Progress");
				}
				else
				{
					pGlob = eaIndexedGetUsingString(&pEnt->pEntUI->pGen->eaVars, pVar->pchName);
				}

				if (pGlob)
				{
					const char *pchString = MultiValGetString(&pVar->Value, NULL);
					pGlob->iInt = MultiValGetInt(&pVar->Value, NULL);
					pGlob->fFloat = MultiValGetFloat(&pVar->Value, NULL);
					if (pchString)
						estrCopy2(&pGlob->pchString, pchString);
				}
			}
		}
		return pEnt->pEntUI->pGen;
	}
	return NULL;
}

__forceinline static UIGen *UpdateEntityGen(Entity *pPlayerEnt, Entity *pEnt, UIGen*** peaGens, CBox *pScreen,
											ExprContext *pContext, F32 fScreenDist, bool bOnScreen,
											EncounterUIData* pEncData, OffscreenUIData* pOffscreenData,
											Vec2 vPos, const char* pchGenName )
{

	UIGen *pGen;
	if (!pchGenName || !pchGenName[0])
		return NULL;
	PERFINFO_AUTO_START_FUNC();
	if (pGen = UpdateEntityGenInternal(pPlayerEnt,pEnt,pScreen,fScreenDist,bOnScreen,pEncData,pOffscreenData,vPos,pchGenName))
	{
		eaPush(peaGens, pGen);
		pEnt->pEntUI->bDraw = true;
	}
	PERFINFO_AUTO_STOP_FUNC();
	return pGen;
}

typedef struct EntSortData
{
	GfxCameraController *pCamera;
	Entity *pPlayerEnt;
} EntSortData;

static int SortByDistance(GfxCameraController *pCamera, const Entity **ppEnt1, const Entity **ppEnt2)
{
	Entity *pEnt1 = (Entity*)*ppEnt1;
	Entity *pEnt2 = (Entity*)*ppEnt2;
	Vec3 v3Pos1;
	Vec3 v3Pos2;
	entGetPos(pEnt1, v3Pos1);
	entGetPos(pEnt2, v3Pos2);
	if (!pCamera)
		return (pEnt1 < pEnt2) ? -1 : 1;
	else
	{
		F32 fDist1 = distance3Squared(pCamera->camcenter, v3Pos1);
		F32 fDist2 = distance3Squared(pCamera->camcenter, v3Pos2);
		return (fDist1 < fDist2) ? -1 : ((fDist1 > fDist2) ? 1 : 0);
	}
}

static int SortEntsByPriority(EntSortData *pData, const Entity **ppEnt1, const Entity **ppEnt2)
{
	Entity *pPlayerEnt = pData->pPlayerEnt;
	Entity *pEnt1 = (Entity*)*ppEnt1;
	Entity *pEnt2 = (Entity*)*ppEnt2;
	EntityRef erTarget = entity_GetTargetRef(pPlayerEnt);
	if (erTarget)
	{
		if (entGetRef(pEnt1) == erTarget)
			return -1;
		if (entGetRef(pEnt2) == erTarget)
			return 1;
	}
	return SortByDistance(pData->pCamera, ppEnt1, ppEnt2);
}

static int SortOffscreenDataForCombining(const OffscreenUIData** pDataA, const OffscreenUIData** pDataB)
{
	const OffscreenUIData* pData1 = *pDataA;
	const OffscreenUIData* pData2 = *pDataB;

	if ( pData1->iEdge < pData2->iEdge )
		return -1;
	if ( pData1->iEdge > pData2->iEdge )
		return 1;

	if ( pData1->fPosition < pData2->fPosition )
		return -1;
	if ( pData1->fPosition > pData2->fPosition )
		return 1;

	return 0;
}

static EncounterUIData* GetEncounterInfo( Entity* pPlayerEnt, Entity* pEntity, const char* pchGenName,
										F32 fFarDistSqr, F32 fMaxSepSqr,
										EncounterUIData*** peaEncounterData, F32 fScreenDist,
										bool bOnScreen, bool bCreateNew, bool* pbAlreadyExists, S32* piEncCount )
{
	const U32 uiMaxEncCount = 8;

	if ( pPlayerEnt && pEntity->pCritter && pEntity->pEntUI && pEntity->pEntUI->pGen && !pEntity->bImperceptible )
	{
		static StashTable pEncKeyStash = NULL;
		S32 i, iKey = pEntity->pCritter->iEncounterKey;
		EncounterUIData* pEncData = NULL;
		Vec3 vPlayerPos;
		Vec3 vEntPos;
		F32 fDistActual;

		if ( iKey == 0 ) //the key should never be zero
			return NULL;

		//check to see if this entity is far enough from the player to warrant an encounter reticle
		fDistActual = entGetDistance( pPlayerEnt, NULL, pEntity, NULL, NULL );
		if ( fFarDistSqr > SQR(fDistActual) )
		{
			pEntity->pCritter->bEncounterFar = false;
			return NULL;
		}

		pEntity->pCritter->bEncounterFar = true;

		if ( pEncKeyStash==NULL )
		{
			pEncKeyStash = stashTableCreateInt(gConf.iMaxOffscreenIconsPlayers+gConf.iMaxOffscreenIconsCritters);
		}
		else if ( (*piEncCount)==0 )
		{
			StashTableIterator iter;
			StashElement elem;
			stashGetIterator(pEncKeyStash, &iter);
			while (stashGetNextElement(&iter, &elem))
				stashElementSetPointer(elem,NULL);
		}

		entGetPos(pPlayerEnt, vPlayerPos);
		entGetPos(pEntity,vEntPos);

		//check to see if this entity belongs to an existing encounter reticle group
		stashIntFindPointer(pEncKeyStash, iKey, &pEncData);

		if ( pEncData == NULL )
		{
			if (!bCreateNew)
			{
				return NULL;
			}

			pEncData = eaGetStruct( peaEncounterData, parse_EncounterUIData, (*piEncCount)++ );

			stashIntAddPointer(pEncKeyStash, iKey, pEncData, true);

			pEncData->erEnt = entGetRef(pEntity);
			pEncData->fScreenDist = bOnScreen ? fScreenDist : -1;
			pEncData->pchGenName = allocAddString(pchGenName);
			pEncData->iCount = 1;
			pEncData->iValidChildren = 0;

			copyVec3( vEntPos, pEncData->vPosSum );

			if ( bOnScreen )
			{
				copyVec2( s_EntFakeParentGen.ScreenBox.upperLeft, pEncData->vMin );
				copyVec2( s_EntFakeParentGen.ScreenBox.lowerRight, pEncData->vMax );
			}
			else
			{
				setVec2( pEncData->vMin, FLT_MAX, FLT_MAX );
				setVec2( pEncData->vMax, 0, 0 );
			}
		}
		else
		{
			Vec3 vCenter;

			if ( pEncData->erEnt==0 )
				return NULL;

			scaleVec3(pEncData->vPosSum, 1/(F32)pEncData->iCount, vCenter);

			//check to see if this entity is too far from the main group and needs a new reticle
			if ( pEncData->iCount >= uiMaxEncCount || distance3Squared(vCenter,vEntPos) > fMaxSepSqr )
			{
				EncounterUIData* pEncChild = NULL;

				for ( i = 0; i < pEncData->iValidChildren; i++ )
				{
					EncounterUIData* pEncChildCur = (EncounterUIData*)pEncData->eaChildren[i];

					scaleVec3(pEncChildCur->vPosSum, 1/(F32)pEncChildCur->iCount, vCenter);

					if ( pEncChildCur->iCount < uiMaxEncCount && distance3Squared(vCenter,vEntPos) < fMaxSepSqr )
					{
						pEncChild = pEncChildCur;
						break;
					}
				}

				if ( pEncChild==NULL )
				{
					if (!bCreateNew)
					{
						return NULL;
					}

					pEncChild = eaGetStruct( &pEncData->eaChildren, parse_EncounterUIData, pEncData->iValidChildren++ );

					pEncChild->erEnt = entGetRef(pEntity);
					pEncChild->fScreenDist = bOnScreen ? fScreenDist : -1;
					pEncChild->pchGenName = allocAddString(pchGenName);
					pEncChild->iCount = 1;
					pEncChild->eaChildren = NULL;

					copyVec3( vEntPos, pEncChild->vPosSum );

					if ( bOnScreen )
					{
						copyVec2( s_EntFakeParentGen.ScreenBox.upperLeft, pEncChild->vMin );
						copyVec2( s_EntFakeParentGen.ScreenBox.lowerRight, pEncChild->vMax );
					}
					else
					{
						setVec2( pEncChild->vMin, FLT_MAX, FLT_MAX );
						setVec2( pEncChild->vMax, 0, 0 );
					}
				}
				else
				{
					pEncChild->iCount++;

					if (pbAlreadyExists)
						(*pbAlreadyExists) = true;

					//combine box
					if ( bOnScreen )
					{
						pEncChild->vMin[0] = min( pEncChild->vMin[0], s_EntFakeParentGen.ScreenBox.lx );
						pEncChild->vMin[1] = min( pEncChild->vMin[1], s_EntFakeParentGen.ScreenBox.ly );
						pEncChild->vMax[0] = max( pEncChild->vMax[0], s_EntFakeParentGen.ScreenBox.hx );
						pEncChild->vMax[1] = max( pEncChild->vMax[1], s_EntFakeParentGen.ScreenBox.hy );
					}

					addVec3( pEncChild->vPosSum, vEntPos, pEncChild->vPosSum );
				}

				if ( pEncChild->erEnt && pEncChild->erEnt != entGetRef(pEntity) && entGetRef(pEntity) == pPlayerEnt->pChar->currentTargetRef )
				{
					entity_SetTarget( pPlayerEnt, pEncChild->erEnt );
				}

				return pEncData;
			}
			else
			{
				pEncData->iCount++;

				if (pbAlreadyExists)
					(*pbAlreadyExists) = true;

				//combine box
				if ( bOnScreen )
				{
					pEncData->vMin[0] = min( pEncData->vMin[0], s_EntFakeParentGen.ScreenBox.lx );
					pEncData->vMin[1] = min( pEncData->vMin[1], s_EntFakeParentGen.ScreenBox.ly );
					pEncData->vMax[0] = max( pEncData->vMax[0], s_EntFakeParentGen.ScreenBox.hx );
					pEncData->vMax[1] = max( pEncData->vMax[1], s_EntFakeParentGen.ScreenBox.hy );
				}

				addVec3( pEncData->vPosSum, vEntPos, pEncData->vPosSum );
			}
		}

		if ( pEncData->erEnt && pEncData->erEnt != entGetRef(pEntity) && entGetRef(pEntity) == pPlayerEnt->pChar->currentTargetRef )
		{
			entity_SetTarget( pPlayerEnt, pEncData->erEnt );
		}

		return pEncData;
	}

	return NULL;
}

static void CombineOffscreenGens( OffscreenUIData* pData, OffscreenUIData* pPrevData )
{
	Entity* pEnt = entFromEntityRefAnyPartition(pData->erEnt);
	Entity* pEntPrev = entFromEntityRefAnyPartition(pPrevData->erEnt);

	if ( pEnt==NULL || pEntPrev==NULL )
		return;

	if ( (!pData->bSame || !pPrevData->bSame) || (!pEnt->pCritter || !pEntPrev->pCritter) ||
			stricmp(entGetLocalName(pEnt),entGetLocalName(pEntPrev))!=0 )
	{
		pData->bSame = pPrevData->bSame = false;
	}

	if ( ++pPrevData->iCombined % 2 == 0 )
	{
		if ( pPrevData->iDistanceIndex < pData->iDistanceIndex )
		{
			pData->erEnt = pPrevData->erEnt;
			pData->iDistanceIndex = pPrevData->iDistanceIndex;
		}

		pData->iCombined = pPrevData->iCombined;
		pPrevData->erEnt = 0;
		pData->iCount += pPrevData->iCount;
	}
	else
	{
		if ( pData->iDistanceIndex < pPrevData->iDistanceIndex )
		{
			pPrevData->erEnt = pData->erEnt;
			pPrevData->iDistanceIndex = pData->iDistanceIndex;
		}

		pData->erEnt = 0;
		pPrevData->fPosition = pData->fPosition;
		pPrevData->iCount += pData->iCount;
	}
}
static bool TryCombineOffscreenGens( OffscreenUIData* pData, OffscreenUIData* pPrevData, F32 fVertCombineDist )
{
	if ( pPrevData && pPrevData->iType != OffscreenType_Target
		&& pData->iEdge == pPrevData->iEdge && pData->iType == pPrevData->iType )
	{
		F32 fCombineDist = ( pData->iEdge % 2 == 0 ) ? gConf.fOffscreenIconCombineDistance : fVertCombineDist;
		if ( pData->fPosition - pPrevData->fPosition <= fCombineDist )
		{
			CombineOffscreenGens( pData, pPrevData );
			return true;
		}
	}
	return false;
}

static void ApplyDistanceConstraints( OffscreenUIData* pA, OffscreenUIData* pB, F32 fVertCombineDist )
{
	F32 fDist = ABS(pA->fPosition - pB->fPosition);
	F32 fSep = ( pA->iEdge % 2 == 0 ) ? gConf.fOffscreenIconCombineDistance : fVertCombineDist;

	if ( fDist < fSep )
	{
		F32 fAlpha = MAXF( fSep / (fDist+0.001f), 0.01f );
		F32 fMA = 0.1f + (pA->iCount) * 0.1f;
		F32 fMB = 0.1f + (pB->iCount) * 0.1f;

		F32 fCM = ( fMA * pA->fPosition + fMB * pB->fPosition ) * ( 1 / ( fMA + fMB ) ) * ( 1.0f - fAlpha );

		pA->fPosition = CLAMPF32(fCM + fAlpha * pA->fPosition, 0.0f, 1.0f);
		pB->fPosition = CLAMPF32(fCM + fAlpha * pB->fPosition, 0.0f, 1.0f);
	}
}

//spread out any remaining overlapping offscreen gens
static void DisperseOverlappingOffscreenGens( OffscreenUIData*** peaOffscreenData, F32 fVertCombineDist )
{
	OffscreenUIData** eaOffscreenData = (*peaOffscreenData);
	S32 c, p;

	for ( c = 1; c < eaSize(peaOffscreenData); c++ )
	{
		OffscreenUIData* pCurrData = eaOffscreenData[c];
		OffscreenUIData* pPrevData = NULL;

		if ( pCurrData->erEnt==0 )
			continue;

		for (p = c-1; p >= 0; p--)
		{
			if ( eaOffscreenData[p]->iEdge != pCurrData->iEdge )
				break;

			if ( eaOffscreenData[p]->erEnt )
			{
				pPrevData = eaOffscreenData[p];
				break;
			}
		}

		if ( pPrevData )
		{
			ApplyDistanceConstraints( pCurrData, pPrevData, fVertCombineDist );
		}
	}
}

static EncounterUIData* EntityFindEncounter( Entity* pEntity, EncounterUIData* pEncData )
{
	if ( pEncData )
	{
		if ( entGetRef(pEntity) == pEncData->erEnt )
		{
			return pEncData;
		}
		else
		{
			S32 i;
			for ( i = pEncData->iValidChildren - 1; i >= 0; i-- )
			{
				EncounterUIData* pEncChild = (EncounterUIData* )pEncData->eaChildren[i];

				if ( entGetRef(pEntity) == pEncChild->erEnt )
				{
					return pEncChild;
				}
			}
		}
	}
	return NULL;
}

static bool EntityGensCheckOffscreenMax(SA_PARAM_NN_VALID Entity* pEnt, U32* piPlayerCount, U32* piCritterCount)
{
	if ( pEnt->myEntityType == GLOBALTYPE_ENTITYPLAYER )
	{
		if ( ++(*piPlayerCount) > gConf.iMaxOffscreenIconsPlayers )
		{
			return true;
		}
	}
	else
	{
		if ( ++(*piCritterCount) > gConf.iMaxOffscreenIconsCritters )
		{
			return true;
		}
	}
	return false;
}

__forceinline static void CleanupEntityUIData(EntityRef erEnt)
{
	Entity* pEncEnt = entFromEntityRefAnyPartition(erEnt);
	if (pEncEnt && pEncEnt->pEntUI)
	{
		pEncEnt->pEntUI->pEncounterData = NULL;
		pEncEnt->pEntUI->pOffscreenData = NULL;
	}
}

static void UpdateEntityGensAdvanced(Entity* pPlayerEnt, Entity ***peaEnts, UIGen*** peaGens)
{
	static OffscreenUIData** eaOffscreenData = NULL;
	static EncounterUIData** eaEncounterData = NULL;
	Entity **eaEnts = *peaEnts;
	S32 i, j, iScreenWidth, iScreenHeight;
	S32 iOffscreenCount = 0;
	S32 iEncounterCount = 0;
	U32 iOffscreenPlayerCount = 0;
	U32 iOffscreenCritterCount = 0;
	F32 fVerticalCombineDistance, fEncMaxSepSqr = 0, fEncFarDistSqr = 0;
	CBox ScreenBox = { 0, 0, 0, 0};
	ExprContext *pContext = ui_GenGetContext(&s_EntFakeParentGen);
	RegionRules* pRegionRules;
	int iBottomInset = StaticDefineIntGetInt(UISizeEnum, "OffscreenEntityGenBottomInset");
	bool bOverheadEntityGens = false;

	gfxGetActiveSurfaceSize(&iScreenWidth, &iScreenHeight);

	iScreenHeight -= iBottomInset;

	ScreenBox.hx = iScreenWidth;
	ScreenBox.hy = iScreenHeight;

	for ( i = eaSize(&eaEncounterData)-1; i >= 0; i-- )
	{
		EncounterUIData* pEncData = eaEncounterData[i];
		CleanupEntityUIData( pEncData->erEnt );
		for ( j = pEncData->iValidChildren-1; j >= 0; j-- )
		{
			CleanupEntityUIData( pEncData->eaChildren[j]->erEnt );
		}
	}
	for ( i = eaSize(&eaOffscreenData)-1; i >= 0; i-- )
	{
		CleanupEntityUIData( eaOffscreenData[i]->erEnt );
	}

	fVerticalCombineDistance = ((iScreenWidth * gConf.fOffscreenIconCombineDistance) / (F32)iScreenHeight);

	pRegionRules = pPlayerEnt ? getRegionRulesFromRegionType( entGetWorldRegionTypeOfEnt(pPlayerEnt) ) : NULL;
	
	if (gConf.bOverheadEntityGens)
	{
		bOverheadEntityGens = true;
	}
	else
	{
		bOverheadEntityGens = SAFE_MEMBER(pRegionRules, bUseOverheadEntityGens);
	}
	if ( pRegionRules && pRegionRules->fEncounterFarDistance > 0.01f )
	{
		fEncFarDistSqr = SQR(pRegionRules->fEncounterFarDistance);
		fEncMaxSepSqr = SQR(pRegionRules->fEncounterMaxSeparation);
	}

	//update onscreen icons, preprocess offscreen icons
	for (i = 0; i < eaSize(&eaEnts); i++)
	{
		Entity* pCurrEnt = eaEnts[i];
		EncounterUIData* pEncData = NULL;
		const char* pchGenName;
		F32 fScreenDist = -1.0f;

		// this function actually updates the ScreenBox of the entity gen in s_EntFakeParentGen
		bool bOnScreen = UpdateEntityGenBox(pCurrEnt, pContext, &ScreenBox, &fScreenDist,bOverheadEntityGens);
		bool bSkipMaxCheck = false;

		if ( pCurrEnt->pEntUI )
		{
			pCurrEnt->pEntUI->bDraw = false;
			pCurrEnt->pEntUI->pEncounterData = NULL;
			pCurrEnt->pEntUI->pOffscreenData = NULL;
		}

		pchGenName = CheckShouldUpdateEntityGen(pPlayerEnt,pCurrEnt,pContext,fScreenDist,bOnScreen);

		if (!pchGenName || !pchGenName[0])
			continue;

		if ( fEncFarDistSqr > FLT_EPSILON )
		{
			bool bTryCreateNew = pCurrEnt->pCritter ? iOffscreenCritterCount < gConf.iMaxOffscreenIconsCritters : false;
			pEncData = GetEncounterInfo(pPlayerEnt,pCurrEnt,pchGenName,fEncFarDistSqr,fEncMaxSepSqr,&eaEncounterData,
										fScreenDist,bOnScreen,bTryCreateNew,&bSkipMaxCheck,&iEncounterCount);
		}

		if (!bOnScreen && !bSkipMaxCheck 
			&& EntityGensCheckOffscreenMax(pCurrEnt,&iOffscreenPlayerCount,&iOffscreenCritterCount))
			continue;

		if ( bOnScreen && (pEncData==NULL || pEncData->erEnt == 0) )
		{
			UpdateEntityGen(pPlayerEnt,pCurrEnt,peaGens,&ScreenBox,pContext,fScreenDist,bOnScreen,NULL,NULL,NULL,pchGenName);
		}
		else if ( !bOnScreen && (!pEncData || (pEncData = EntityFindEncounter(pCurrEnt,pEncData))) )
		{
			F32 cx,cy;
			OffscreenUIData* pData;
			pData = eaGetStruct(&eaOffscreenData, parse_OffscreenUIData, iOffscreenCount++);
			pData->erEnt = entGetRef(pCurrEnt);
			pData->iCount = 0;
			pData->iCombined = 0;
			pData->iDistanceIndex = i;
			pData->pEncData = pEncData;
			pData->bSame = true;
			pData->bEncounter = pEncData && pEncData->erEnt;
			pData->pchGenName = allocAddString(pchGenName);

			//get the type of entity this gen will be associated with for combining purposes
			if ( pPlayerEnt == NULL )
				pData->iType = OffscreenType_CritterEnemy;
			else if ( entGetRef(pCurrEnt) == pPlayerEnt->pChar->currentTargetRef )
				pData->iType = OffscreenType_Target;
			else if ( team_OnSameTeam( pPlayerEnt, pCurrEnt ) )
				pData->iType = OffscreenType_Team;
			else if ( entGetType(pCurrEnt) == GLOBALTYPE_ENTITYPLAYER )
				pData->iType = OffscreenType_Player;
			else if ( entity_GetRelation(PARTITION_CLIENT, pPlayerEnt, pCurrEnt) == kEntityRelation_Friend )
				pData->iType = OffscreenType_CritterFriendly;
			else
				pData->iType = OffscreenType_CritterEnemy;

			cx = s_EntFakeParentGen.ScreenBox.lx;
			cy = s_EntFakeParentGen.ScreenBox.ly;

			//offscreen icons are combined based on what "screen edge" they reside on
			if ( s_EntFakeParentGen.ScreenBox.ly < 5 )
			{
				pData->iEdge = 0;
				pData->fPosition = cx / (F32)iScreenWidth;
			}
			else if ( s_EntFakeParentGen.ScreenBox.hx > iScreenWidth - 5 )
			{
				pData->iEdge = 1;
				pData->fPosition = cy / (F32)iScreenHeight;
			}
			else if ( s_EntFakeParentGen.ScreenBox.hy > iScreenHeight - 5 )
			{
				pData->iEdge = 2;
				pData->fPosition = cx / (F32)iScreenWidth;
			}
			else
			{
				pData->iEdge = 3;
				pData->fPosition = cy / (F32)iScreenHeight;
			}
		}
	}

	if ( !gConf.bManageOffscreenGens || (eaSize(&eaOffscreenData) == 0 && eaSize(&eaEncounterData) == 0) )
		return;

	//eliminate excess offscreen/encounter elements
	while (eaSize(&eaOffscreenData) > iOffscreenCount)
		StructDestroy(parse_OffscreenUIData, eaPop(&eaOffscreenData));
	while (eaSize(&eaEncounterData) > iEncounterCount)
		StructDestroy(parse_EncounterUIData, eaPop(&eaEncounterData));

	//update onscreen encounter gens
	for ( i = 0; i < eaSize( &eaEncounterData ); i++ )
	{
		EncounterUIData* pEncData = eaEncounterData[i];

		if ( pEncData->erEnt==0 )
			continue;

		if ( pEncData->fScreenDist >= ENTUI_MIN_FEET_FROM_CAMERA )
		{
			copyVec2( pEncData->vMin, s_EntFakeParentGen.ScreenBox.upperLeft );
			copyVec2( pEncData->vMax, s_EntFakeParentGen.ScreenBox.lowerRight );

			UpdateEntityGen(pPlayerEnt,entFromEntityRefAnyPartition(pEncData->erEnt),peaGens,&ScreenBox,pContext,
							pEncData->fScreenDist,true,pEncData,NULL,NULL,pEncData->pchGenName);
		}

		for ( j = 0; j < pEncData->iValidChildren; j++ )
		{
			EncounterUIData* pEncChild = (EncounterUIData*)pEncData->eaChildren[j];

			if ( pEncChild->fScreenDist >= ENTUI_MIN_FEET_FROM_CAMERA )
			{
				copyVec2( pEncChild->vMin, s_EntFakeParentGen.ScreenBox.upperLeft );
				copyVec2( pEncChild->vMax, s_EntFakeParentGen.ScreenBox.lowerRight );

				UpdateEntityGen(pPlayerEnt,entFromEntityRefAnyPartition(pEncChild->erEnt),peaGens,&ScreenBox,pContext,
								pEncChild->fScreenDist,true,pEncChild,NULL,NULL,pEncChild->pchGenName);
			}
		}
	}

	eaQSort( eaOffscreenData, SortOffscreenDataForCombining );

	//try to combine offscreen icons
	for ( i = 0; i < eaSize( &eaOffscreenData ); i++ )
	{
		OffscreenUIData* pData = eaOffscreenData[i];
		OffscreenUIData* pPrevData = NULL;
		S32 c = i;

		pData->iCount = pData->pEncData ? pData->pEncData->iCount : 1;
		pData->pEncData = NULL;

		for ( c = i-1; c >= 0; c-- )
		{
			OffscreenUIData* pCurrData = eaOffscreenData[c];

			if ( pCurrData->iEdge != pData->iEdge )
				break;
			if ( pCurrData->erEnt == 0 )
				continue;

			if ( pCurrData->iType == pData->iType && pCurrData->bEncounter == pData->bEncounter )
			{
				pPrevData = eaOffscreenData[c];
				break;
			}
		}

		TryCombineOffscreenGens( pData, pPrevData, fVerticalCombineDistance );
	}

	DisperseOverlappingOffscreenGens( &eaOffscreenData, fVerticalCombineDistance );

	//update offscreen icons
	for ( i = 0; i < eaSize( &eaOffscreenData ); i++ )
	{
		OffscreenUIData* pData = eaOffscreenData[i];
		Vec2 vTempPos;
		F32 fScreenDist = -1.0f;

		if ( pData->erEnt==0 )
			continue;

		switch ( pData->iEdge )
		{
			xcase 0: setVec2( vTempPos, pData->fPosition, 0 );
			xcase 1: setVec2( vTempPos, (iScreenWidth-1)/(F32)(iScreenWidth), pData->fPosition );
			xcase 2: setVec2( vTempPos, pData->fPosition, (iScreenHeight-1)/(F32)(iScreenHeight) );
			xcase 3: setVec2( vTempPos, 0, pData->fPosition );
			xdefault: zeroVec2(vTempPos);
		}

		UpdateEntityGen(pPlayerEnt,entFromEntityRefAnyPartition(pData->erEnt),peaGens,&ScreenBox,pContext,
						fScreenDist,false,NULL,pData,vTempPos,pData->pchGenName);
	}
}

static void UpdateEntityGens(Entity* pPlayerEnt, Entity ***peaEnts, UIGen*** peaGens)
{
	Entity **eaEnts = *peaEnts;
	S32 i, iScreenWidth, iScreenHeight;
	CBox ScreenBox = { 0, 0, 0, 0};
	ExprContext *pContext = ui_GenGetContext(&s_EntFakeParentGen);
	RegionRules* pRegionRules;
	bool bOverheadEntityGens = false;

	gfxGetActiveSurfaceSize(&iScreenWidth, &iScreenHeight);
	ScreenBox.hx = iScreenWidth;
	ScreenBox.hy = iScreenHeight;

	if (gConf.bOverheadEntityGens)
	{
		bOverheadEntityGens = true;
	}
	else
	{
		pRegionRules = pPlayerEnt ? getRegionRulesFromEnt(pPlayerEnt) : NULL;
		bOverheadEntityGens = SAFE_MEMBER(pRegionRules, bUseOverheadEntityGens);
	}

	//update onscreen icons, preprocess offscreen icons
	for (i = 0; i < eaSize(&eaEnts); i++)
	{
		Entity* pCurrEnt = eaEnts[i];
		const char* pchGenName;
		F32 fScreenDist = -1.0f;
		bool bOnScreen = UpdateEntityGenBox(pCurrEnt, pContext, &ScreenBox, &fScreenDist,bOverheadEntityGens);
		if ( pCurrEnt->pEntUI )
		{
			pCurrEnt->pEntUI->bDraw = false;
			pCurrEnt->pEntUI->pEncounterData = NULL;
			pCurrEnt->pEntUI->pOffscreenData = NULL;
		}
		pchGenName = CheckShouldUpdateEntityGen(pPlayerEnt, pCurrEnt, pContext, fScreenDist, bOnScreen);
		UpdateEntityGen(pPlayerEnt, pCurrEnt, peaGens, &ScreenBox, pContext, fScreenDist, bOnScreen, NULL, NULL, NULL, pchGenName);
	}
}

static void RunGensForEnts(Entity* pPlayerEnt, Entity ***peaEnts)
{
	Entity **eaEnts = *peaEnts;
	static UIGen **s_eaGens;
	static bool s_bInit = false;
	S32 i;

	if (!s_bInit)
	{
		entui_Register();
		s_bInit = true;
	}

	PERFINFO_AUTO_START(__FUNCTION__ ": Updating Entity Gens", eaSize(&eaEnts));
	eaClearFast(&s_eaGens);
	if ( gConf.bManageOffscreenGens )
		UpdateEntityGensAdvanced(pPlayerEnt, peaEnts, &s_eaGens);
	else
		UpdateEntityGens(pPlayerEnt,peaEnts,&s_eaGens);
	PERFINFO_AUTO_STOP();

	PERFINFO_AUTO_START(__FUNCTION__ ": Entity Gen Layout", eaSize(&s_eaGens));
	for (i = 0; i < eaSize(&s_eaGens); i++)
	{
		UIGen *pGen = s_eaGens[i];
		s_EntFakeParentGen.UnpaddedScreenBox = s_EntFakeParentGen.ScreenBox = pGen->ScreenBox;
		ui_GenPointerUpdateCB(pGen, &s_EntFakeParentGen);
		ui_GenUpdateCB(pGen, &s_EntFakeParentGen);
		ui_GenLayoutCB(pGen, &s_EntFakeParentGen);
	}
	PERFINFO_AUTO_STOP();

	if (inpDidAnything())
	{
		PERFINFO_AUTO_START(__FUNCTION__ ": Entity Gen Tick", eaSize(&s_eaGens));
		for (i = 0; i < eaSize(&s_eaGens); i++)
			ui_GenTickCB(s_eaGens[i], &s_EntFakeParentGen);
		PERFINFO_AUTO_STOP();
	}

	// Drawing the gens is interleaved with drawing damage floats
	// and chat bubbles, in order to get correct Z order.
}

static int gclWasSourceFromPlayer(CombatTrackerNet *pNet)
{
	EntityRef erPlayer = entPlayerRef(0);
	return (pNet->erSource == erPlayer || pNet->erOwner == erPlayer);
}

static EUIDamageDirection getUIDamageDirectionFromAngle(F32 fAngle)
{
#define FRONT_HALF_ARC	RAD(45)
#define REAR_HALF_ARC	RAD(50)
	// if these need to change due to project specific needs, or need tweaking often, 
	// please move to a date file
	
	if (ABS(fAngle) < FRONT_HALF_ARC)
		return EUIDamageDirection_FRONT;

	if (ABS(fAngle) + REAR_HALF_ARC > PI)
		return EUIDamageDirection_BACK;

	return (fAngle < 0.f) ? EUIDamageDirection_RIGHT : EUIDamageDirection_LEFT;
}

__forceinline static void gclCombatConvertToFloater(CombatTrackerNet *pNet, Entity *pTarget)
{
	EntityUI * pEntUI = pTarget->pEntUI;

	gclDamageFloatCreate(&g_DamageFloatTemplate, pNet, pTarget, 0, 1);

	if (pNet->fMagnitude > 0)
	{
		F32 fDamageAngle = -1.0f;
		F32 fTangentAngle = 0.0f;

		if (gclWasSourceFromPlayer(pNet))
		{
			pEntUI->uiLastDamagedByPlayer = gGCLState.totalElapsedTimeMs;
		}

		pEntUI->uiLastDamaged = gGCLState.totalElapsedTimeMs;

		// Calculate the attack angle if the current control scheme asks for it
		if (g_CurrentScheme.bGetAttackAngleWhenDamaged && pTarget == entActivePlayerPtr())
		{
			Entity* pSource = pNet->erSource ? entFromEntityRefAnyPartition(pNet->erSource) : NULL;
			if (!pSource)
			{
				pSource = entFromEntityRefAnyPartition(pNet->erOwner);
			}

			if (pSource && entGetRef(pSource) != entGetRef(pTarget))
			{
				S32 angleIndex;
				Vec3 pyrFace = {0, 0, 0};
				Vec3 vSourcePos, vTargetPos, vToSource;
				F32 yawToSource;
				entGetPos(pSource, vSourcePos);
				entGetPos(pTarget, vTargetPos);
				
				subVec3(vSourcePos, vTargetPos, vToSource);
				yawToSource = getVec3Yaw(vToSource);

				entGetFacePY(pTarget, pyrFace);

				fDamageAngle = subAngle(pyrFace[1], yawToSource);

				angleIndex = getUIDamageDirectionFromAngle(fDamageAngle);
				pEntUI->uiLastDamageDirectionTimes[angleIndex] = gGCLState.totalElapsedTimeMs;
			}
		}
		pEntUI->fLastDamageAngle = fDamageAngle;
		pEntUI->fLastDamageTangentAngle = fTangentAngle;
	}
}

AUTO_COMMAND ACMD_NAME(TestDamageFloaters) ACMD_ACCESSLEVEL(9) ACMD_CLIENTONLY;
void gclCombatTestDamageFloaters(int mag, int num)
{
	CombatTrackerNet tempTracker;
	Entity* pEnt = entActivePlayerPtr();
	Entity* pTarget = entity_GetTarget(pEnt);
	int i;
	if (pEnt)
	{
		if (!pTarget)
			pTarget = pEnt;
		StructInit(parse_CombatTrackerNet, &tempTracker);
		tempTracker.erOwner = pEnt->myRef;
		// The owner entity

		tempTracker.erSource = pEnt->myRef;
		// The source entity, if different from the owner

		tempTracker.fMagnitude = mag;

 		for(i = 0; i < num; i++)
		{
 			gclDamageFloatCreate(&g_DamageFloatTemplate, &tempTracker, pTarget, i*0.5, 1.0);
		}
		gclDamageFloatLayout(&g_DamageFloatTemplate, pTarget);
	}
}


// Probably doesn't belong here long-term
static S32 s_bCombatLog = 0;

AUTO_CMD_INT(s_bCombatLog, CombatLog)  ACMD_ACCESSLEVEL(0);

AUTO_RUN;
void AutoEnableCombatLogging(void)
{
	if (gConf.bAutoCombatLogging)
		s_bCombatLog = 1;
}

// ------------------------------------------------------------------------------------------------------------------------------
__forceinline static void gclEntityProcessCombatTracker(Entity *pent)
{
	int i,s;
	Entity *pentSelf = entActivePlayerPtr();
	if(pent->pChar && 0!=(s = eaSize(&pent->pChar->combatTrackerNetList.ppEventsBuffer)))
	{
		EntityRef erTarget = entGetRef(pent);

		for (i=0; i<s; i++)
		{
			CombatTrackerNet *pNet = pent->pChar->combatTrackerNetList.ppEventsBuffer[i];
			char *pchMessage = NULL;

			if(pent->pEntUI && pNet->eFlags&kCombatTrackerFlag_Flank)
			{
				// The Entity was hit with something flagged as Flank, update the time
				//  on the EntityUI
				pent->pEntUI->uiLastFlank = gGCLState.totalElapsedTimeMs;
			}

			if (gclCheckCombatTrackerAgainstClientFilters(pNet))
			{
				if (g_DamageFloatTemplate.bCombineSimilar)
				{
					// Using fDelay as a temporary marker for things that were combined
					if (pNet->fDelay == 0)
					{
						int j;
						CombatTrackerNet *pTemp = NULL;
						for (j = i + 1; j < s; j++)
						{
							CombatTrackerNet *pFound = pent->pChar->combatTrackerNetList.ppEventsBuffer[j];
							if (pNet->erSource == pFound->erSource && pNet->eFlags == pFound->eFlags &&
								(pNet->eType == pFound->eType || (ATTRIB_DAMAGE(pNet->eType) && ATTRIB_DAMAGE(pFound->eType)) ))
							{
								// Try to combine similar damage floats
								//CMILLER TODO: This is going to interact strangely with the new split floaters tech.
								if (!pTemp)
								{
									pTemp = StructClone(parse_CombatTrackerNet, pNet);
								}
								pTemp->fMagnitude += pFound->fMagnitude;
								pTemp->fMagnitudeBase += pFound->fMagnitudeBase;

								pFound->fDelay = 1;
							}
						}
						if (pTemp)
						{
							gclCombatConvertToFloater(pTemp, pent);
							StructDestroy(parse_CombatTrackerNet, pTemp);
						}
						else
						{
							gclCombatConvertToFloater(pNet, pent);
						}
					}
				}
				else
				{
					gclCombatConvertToFloater(pNet, pent);
				}
				
				combatevent_AutoDesc(&pchMessage,erTarget,pNet->erOwner,pNet->erSource,pNet->eType,fabs(pNet->fMagnitude),fabs(pNet->fMagnitudeBase),GET_REF(pNet->hDisplayName),GET_REF(pNet->hSecondaryDisplayName),pNet->eFlags,pNet->fMagnitude<=0);

				{
					ChatLogEntryType eType = kChatLogEntryType_CombatOther;

					if (pentSelf==pent
						|| pentSelf==entFromEntityRefAnyPartition(pNet->erSource)
						|| pentSelf==entFromEntityRefAnyPartition(pNet->erOwner))
					{
						eType = kChatLogEntryType_CombatSelf;
					}
					else if (team_OnSameTeam(pentSelf,pent)
						|| team_OnSameTeam(pentSelf,entFromEntityRefAnyPartition(pNet->erSource))
						|| team_OnSameTeam(pentSelf,entFromEntityRefAnyPartition(pNet->erOwner)))
					{
						eType = kChatLogEntryType_CombatTeam;
					}

					ChatLog_AddChatMessage(eType, pchMessage, NULL);
				}

				estrDestroy(&pchMessage);
			}

			if(s_bCombatLog && pNet)
			{
				combattracker_CombatLog(pNet,erTarget);

				gclCombatDebugMeters_Notify(pNet);
			}

			if (pNet->eType == kAttribType_Power && 
				pNet->fMagnitude < 0.f && 
				gclWasSourceFromPlayer(pNet) && 
				gclNotifyIsHandled(kNotifyType_PowerAttribGained, NULL))
			{
				gclNotifyReceiveWithEntityRef(kNotifyType_PowerAttribGained, "PowerAttribGained", NULL, -(S32)floorf(pNet->fMagnitude), pNet->erSource);
			}

			if (pentSelf == pent)
			{
				Entity *pSource = entFromEntityRefAnyPartition(pNet->erSource);
				if (pSource)
					clientTarget_NotifyAttacked(pSource);
			}
		}

		gclDamageFloatLayout(&g_DamageFloatTemplate, pent);

		// Clear the buffer
		eaDestroyStruct(&pent->pChar->combatTrackerNetList.ppEventsBuffer,parse_CombatTrackerNet);
	}
}

int gclSortEntities(const Entity** pEntA, const Entity** pEntB)
{
	if (pEntA && pEntB)
	{
		UIGen *pGenA = SAFE_MEMBER2(*pEntA, pEntUI, pGen);
		UIGen *pGenB = SAFE_MEMBER2(*pEntB, pEntUI, pGen);
		if (pGenA && pGenB)
		{
			UIGenEntityState *pStateA = UI_GEN_STATE(pGenA, Entity);
			UIGenEntityState *pStateB = UI_GEN_STATE(pGenB, Entity);
			return (SAFE_MEMBER(pStateA, fScreenDist) - SAFE_MEMBER(pStateB, fScreenDist) > 0) ? 1 : -1;
		}
	}
	return 0;
}

static bool gclEntity_ShouldUpdateEntUI(Entity* pPlayerEnt, Entity* pEnt, bool bIgnoreNoDraw)
{
	if (!ShowEntityHUD())
		return false;

	if (pEnt->myEntityFlags & ENTITYFLAG_DOOR_SEQUENCE_IN_PROGRESS)
		return false;

	if (!bIgnoreNoDraw && (pEnt->myEntityFlags & ENTITYFLAG_DONOTDRAW) && !team_OnSameTeam(pPlayerEnt, pEnt))
		return false;

	return true;
}

void gclDrawStuffOverEntities(void)
{
	F32 elapsed = g_ui_State.timestep;
	EntityIterator *pIter;
	static Entity **s_eaEnts;
	Entity *pEnt = NULL;
	Entity *pPlayerEnt = entActivePlayerPtr();
	S32 iChatBubblesDrawn = 0;
	S32 k;
	RegionRules* pRegionRules = pPlayerEnt ? getRegionRulesFromEnt(pPlayerEnt) : NULL;
	bool bIgnoreNoDraw = pRegionRules && pRegionRules->bIgnoreNoDraw ? true : false;
	int not_drawn=0;
	int skipped=0;

	// If we are not in Gameplay, the player pointer is bad and cannot be used.
	if (!(GSM_IsStateActive(GCL_GAMEPLAY) || GSM_IsStateActive(GCL_DEMO_PLAYBACK)))
		return;

	eaClearFast(&s_eaEnts);

	if (g_ui_State.bInEditor)
		return;

	if (ShowEntityHUD())
	{
		PERFINFO_AUTO_START("gclDrawStuffOverEntities: Destructible Object Stuff", 1);
		gclDrawStuffOverObjects();
		PERFINFO_AUTO_STOP();

		PERFINFO_AUTO_START("gclDrawStuffOverEntities: Waypoint Gens", 1);
		gclWaypoint_UpdateGens();
		PERFINFO_AUTO_STOP();
	}

	PERFINFO_AUTO_START("gclDrawStuffOverEntities: Building Ent List", 1);
	pIter = entGetIteratorAllTypesAllPartitions(0, ENTITYFLAG_IGNORE);
	while (pEnt = EntityIteratorGetNext(pIter))
	{
		if (gclEntity_ShouldUpdateEntUI(pPlayerEnt, pEnt, bIgnoreNoDraw))
		{
			eaPush(&s_eaEnts, pEnt);
		}
		else
		{
			gclEntity_CleanupEntUI(pEnt);
		}
	}
	EntityIteratorRelease(pIter);

	// We want cutscene created entities to be able to display chat bubbles
	gclClientOnlyEntitiyGetCutsceneEnts(&s_eaEnts);

	if (eaSize(&s_eaEnts) > 0)
	{
		if (gConf.bManageOffscreenGens)
		{
			EntSortData SortData;
			SortData.pPlayerEnt = entActivePlayerPtr();
			SortData.pCamera = gfxGetActiveCameraController();
			eaQSort_s(s_eaEnts, SortEntsByPriority, &SortData);
		}
		else
		{
			eaQSort_s(s_eaEnts, SortByDistance, gfxGetActiveCameraController());
		}
	}
	PERFINFO_AUTO_STOP();

	gclChatBubbleResetTacks();
	
	eaQSort(s_eaEnts, gclSortEntities);

	if (ShowEntityHUD())
	{
		PERFINFO_AUTO_START("RunGensForEnts", 1);
		RunGensForEnts(pPlayerEnt, &s_eaEnts);
		PERFINFO_AUTO_STOP();
	}

	for (k = eaSize(&s_eaEnts) - 1; k >= 0; k--)
	{
		pEnt = s_eaEnts[k];

		if (!gConf.bDamageFloatsDrawOverEntityGen)
		{
			PERFINFO_AUTO_START("gclDrawStuffOverEntities: Combat Tracker / Damage Floaters", 1);

			gclEntityProcessCombatTracker(pEnt);
			if (pEnt->pChar && ShowEntityHUD())
				gclDrawDamageFloaters(pEnt, 1.f);

			PERFINFO_AUTO_STOP();
		}

		if (pEnt->pEntUI && pEnt->pEntUI->pGen && ShowEntityHUD())
		{
			PERFINFO_AUTO_START(__FUNCTION__ ": Entity Gen Draw", 1);
			if ( pEnt->pEntUI->bDraw )
				ui_GenDrawCB(pEnt->pEntUI->pGen, NULL);
			PERFINFO_AUTO_STOP();
		}

		if (gConf.bDamageFloatsDrawOverEntityGen)
		{
			PERFINFO_AUTO_START("gclDrawStuffOverEntities: Combat Tracker / Damage Floaters", 1);

			gclEntityProcessCombatTracker(pEnt);
			if (pEnt->pChar && ShowEntityHUD())
				gclDrawDamageFloaters(pEnt, 1.f);

			PERFINFO_AUTO_STOP();
		}

		PERFINFO_AUTO_START(__FUNCTION__ ": Chat Bubbles Drawing", 1);
		ChatBubbleStack_Process(pEnt, gGCLState.frameElapsedTime);

		if (ShowChatBubbles() && iChatBubblesDrawn < FC_MAX_CHAT_BUBBLES)
		{
			F32 fMinZ = UI_GET_Z();
			F32 fMaxZ = UI_GET_Z();
			F32 fStepZ = (fMaxZ - fMinZ) / FC_MAX_CHAT_BUBBLES;
			iChatBubblesDrawn += !!ChatBubble_DrawFor(pEnt, fMaxZ - fStepZ * iChatBubblesDrawn);
		}

		PERFINFO_AUTO_STOP();
	}
}

AUTO_EXPR_FUNC(UIGen) ACMD_NAME(HeadshotRelease);
SA_RET_OP_VALID BasicTexture *gclExpr_HeadshotRelease(SA_PARAM_OP_VALID BasicTexture *pTexture)
{
	if (pTexture)
		gfxHeadshotRelease(pTexture);
	return NULL;
}

AUTO_EXPR_FUNC(UIGen) ACMD_NAME(AnimatedHeadshotRelease);
SA_RET_OP_VALID BasicTexture *gclExpr_AnimatedHeadshotRelease(SA_PARAM_OP_VALID BasicTexture *pTexture)
{
	return gclExpr_HeadshotRelease(pTexture);
}

AUTO_EXPR_FUNC(UIGen) ACMD_NAME(CameraGetYaw);
F32 gclExpr_CameraGetYaw(void)
{
	Vec3 v3PYR;
	gfxGetActiveCameraYPR(v3PYR);
	return v3PYR[1];
}

// Return true if the given entity is alive.
AUTO_EXPR_FUNC(entityutil) ACMD_NAME(EntIsAlive);
bool gclExprEntIsAlive(SA_PARAM_OP_VALID Entity *pEntity)
{
	return pEntity ? entIsAlive(pEntity) : false;
}

// Return true if the given entity is visible. This check is expensive; don't
// call it unless you're sure you need the result.
AUTO_EXPR_FUNC(entityutil) ACMD_NAME(EntIsVisible);
bool gclExprEntIsVisible(SA_PARAM_OP_VALID Entity *pEntity)
{
	return pEntity ? entIsVisible(pEntity) : false;
}

//cached LoS for this frame if EntCalcTargetInLoS has been called this frame
AUTO_EXPR_FUNC(entityutil) ACMD_NAME(EntIsMyTargetInLoS);
bool gclExprEntIsMyTargetInLoS(SA_PARAM_OP_VALID Entity *pEntity)
{
	return clientTarget_IsMyTargetInLoS(pEntity);
}

AUTO_EXPR_FUNC(entityutil) ACMD_NAME(PlayerIsOffscreenEntInLoS);
bool gclExprPlayerIsOffscreenEntInLoS(SA_PARAM_OP_VALID Entity *pEntity)
{
	Entity* pPlayerEnt = entActivePlayerPtr();

	if ( pPlayerEnt && pEntity && pEntity->pEntUI && pEntity->pEntUI->pOffscreenData )
	{
		if ( g_ui_State.totalTimeInMs < pEntity->pEntUI->uiLastOffscreenLoSCheck + 100 )
		{
			return pEntity->pEntUI->bLastOffscreenLoS;
		}
		else
		{
			Vec3 vPlayerPos, vTargetPos;
			entGetCombatPosDir( pPlayerEnt, NULL, vPlayerPos, NULL );
			entGetCombatPosDir( pEntity, NULL, vTargetPos, NULL );
			pEntity->pEntUI->bLastOffscreenLoS = combat_CheckLoS(PARTITION_CLIENT, vPlayerPos,vTargetPos,pPlayerEnt,pEntity,NULL,false,false,NULL);
			pEntity->pEntUI->uiLastOffscreenLoSCheck = g_ui_State.totalTimeInMs;
			return pEntity->pEntUI->bLastOffscreenLoS;
		}
	}
	return false;
}

// should we display a timer? we can't look at the gen directly because its vars
// may not have been filled in if this was called from the EntityGen expression.
AUTO_EXPR_FUNC(entityutil) ACMD_NAME(EntShouldDisplayTimer);
bool gclExprEntShouldDisplayTimer(SA_PARAM_NN_VALID Entity *pEnt)
{
	return pEnt && (eaIndexedFindUsingString(&pEnt->UIVars, "Timer") >= 0
		|| eaIndexedFindUsingString(&pEnt->UIVars, "BombExplode") >= 0);
}

// should we display a progress bar? we can't look at the gen directly because its
// vars may not have been filled in if this was called from the EntityGen expression.
AUTO_EXPR_FUNC(entityutil) ACMD_NAME(EntShouldDisplayProgressBar);
bool gclExprEntShouldDisplayProgressBar(SA_PARAM_NN_VALID Entity *pEnt)
{
	return pEnt && (eaIndexedFindUsingString(&pEnt->UIVars, "Progress") >= 0
		|| eaIndexedFindUsingString(&pEnt->UIVars, "BombArmProgress") >= 0);
}

/***************SAVED ENTITY INFORMATION************************/

AUTO_EXPR_FUNC(entityutil) ACMD_NAME(EntGetActivePets);
void ui_GenExprGetPetsInState(ExprContext *pContext, SA_PARAM_NN_VALID Entity *pEntity, const char *pchState)
{
	OwnedContainerState eRelationship = StaticDefineIntGetInt(OwnedContainerStateEnum,pchState);
	UIGen *pGen = exprContextGetUserPtr(pContext, parse_UIGen);
	static PetRelationship **s_eaPets;

	eaClearFast(&s_eaPets);

	if (!pGen)
		return;
	if (pEntity)
	{
		S32 i;
		for (i = 0; i < eaSize(&pEntity->pSaved->ppOwnedContainers); i++)
		{
			PetRelationship *pPet = eaGet(&pEntity->pSaved->ppOwnedContainers, i);
			if (pPet->eRelationship == eRelationship)
				eaPush(&s_eaPets, pPet);
		}
	}
	ui_GenSetList(pGen, &s_eaPets, parse_PetRelationship);
}


// get the critter group name message
AUTO_EXPR_FUNC(entityutil) ACMD_NAME(EntGetGroupName);
const char * gclExprEntGetGroupName(SA_PARAM_OP_VALID Entity *pEnt)
{
	if (!pEnt || !pEnt->pCritter)
		return "";

	return TranslateMessageRefDefault(pEnt->pCritter->hGroupDisplayNameMsg, "");
}

AUTO_EXPR_FUNC(entityutil) ACMD_NAME(EntGetCritterDescription);
const char * gclExprEntGetCritterDescription(ExprContext *pContext, EntityRef iRef)
{
	if (s_estrCritterDescription && s_iCritterDescriptionRef == iRef)
	{
		const char *pDesc = exprContextAllocString(pContext, s_estrCritterDescription);
		estrDestroy(&s_estrCritterDescription);
		return pDesc;
	}

	return "";
}

AUTO_COMMAND ACMD_PRIVATE ACMD_CLIENTCMD ACMD_ACCESSLEVEL(0) ACMD_NAME(SetCritterDescription);
void gclCmdSetCritterDescription(EntityRef iRef, const char *pchDescription)
{
	if (!s_estrCritterDescription)
	{
		estrCreate(&s_estrCritterDescription);
	}

	s_iCritterDescriptionRef = iRef;
	estrClear(&s_estrCritterDescription);
	estrAppend2(&s_estrCritterDescription, NULL_TO_EMPTY(pchDescription));
}

AUTO_EXPR_FUNC(entityutil) ACMD_NAME(EntGetGroupDescription);
const char * gclExprEntGetGroupDescription(ExprContext *pContext, EntityRef iRef)
{
	if (s_estrCritterGroupDescription && s_iCritterGroupDescriptionRef == iRef)
	{
		const char *pDesc = exprContextAllocString(pContext, s_estrCritterGroupDescription);
		estrDestroy(&s_estrCritterGroupDescription);
		return pDesc;
	}

	return "";
}

AUTO_COMMAND ACMD_PRIVATE ACMD_CLIENTCMD ACMD_ACCESSLEVEL(0) ACMD_NAME(SetCritterGroupDescription);
void gclCmdSetCritterGroupDescription(EntityRef iRef, const char *pchDescription)
{
	if (!s_estrCritterGroupDescription)
	{
		estrCreate(&s_estrCritterGroupDescription);
	}

	s_iCritterGroupDescriptionRef = iRef;
	estrClear(&s_estrCritterGroupDescription);
	estrAppend2(&s_estrCritterGroupDescription, NULL_TO_EMPTY(pchDescription));
}

AUTO_EXPR_FUNC(entityutil) ACMD_NAME("EntGetAttribModMagnitude");
S32 gclExprEntGetAttribModMagnitude(SA_PARAM_OP_VALID Entity *pent,
									 const char* attribName,
									 S32 index)
{
	return AttribModMagnitudeEx(pent, attribName, index, NULL);
}



AUTO_EXPR_FUNC(entityutil) ACMD_NAME("EntGetAttribModMagnitudeByTag");
S32 gclExprEntGetAttribModMagnitudeByTag(SA_PARAM_OP_VALID Entity *pent,
										 const char* attribName,
										 S32 index,
										 char *pchTags)
{
	static S32 *s_piTags = NULL;

	eaiClearFast(&s_piTags);

	if (pchTags)
		StaticDefineIntParseStringForInts(PowerTagsEnum, pchTags, &s_piTags, NULL);
	
	return AttribModMagnitudeEx(pent,attribName,index, s_piTags);
}


AUTO_EXPR_FUNC(entityutil) ACMD_NAME("EntGetOfflineAttribModMagnitude");
S32 gclExprEntGetOfflineAttribModMagnitude(	SA_PARAM_OP_VALID Entity *pent,
											const char* attribName,
											S32 index)
{
	if (!pent) return 0;
	if (entGetType(pent) != GLOBALTYPE_ENTITYSAVEDPET)
	{
		return AttribModMagnitudeEx(pent, attribName, index, NULL);
	}
	return AttribModMagnitudeEx(savedpet_GetOfflineOrNotCopy(entGetContainerID(pent)), attribName, index, NULL);
}

extern S32 AttribModMagnitudeOriginal(Entity *pent, const char* attribName, S32 index);
AUTO_EXPR_FUNC(entityutil) ACMD_NAME("EntGetAttribModMagnitudeOriginal");
S32 gclExprEntGetAttribModMagnitudeOriginal(	SA_PARAM_OP_VALID Entity *pent,
												const char* attribName,
												S32 index)
{
	return AttribModMagnitudeOriginal(pent,attribName,index);
}

AUTO_EXPR_FUNC(entityutil) ACMD_NAME("EntGetOfflineAttribModMagnitudeOriginal");
S32 gclExprEntGetOfflineAttribModMagnitudeOriginal(	SA_PARAM_OP_VALID Entity *pent,
													const char* attribName,
													S32 index)
{
	if (!pent) return 0;
	if (entGetType(pent) != GLOBALTYPE_ENTITYSAVEDPET)
	{
		return AttribModMagnitudeOriginal(pent,attribName,index);
	}
	return AttribModMagnitudeOriginal(savedpet_GetOfflineOrNotCopy(entGetContainerID(pent)),attribName,index);
}

AUTO_EXPR_FUNC(entityutil) ACMD_NAME("EntGetTrueOfflineAttribModMagnitudeOriginal");
S32 gclExprEntGetTrueOfflineAttribModMagnitudeOriginal(	SA_PARAM_OP_VALID Entity *pent,
													    const char* attribName,
													    S32 index)
{
	if (!pent) return 0;
	return AttribModMagnitudeOriginal(savedpet_GetOfflineCopy(entGetContainerID(pent)),attribName,index);
}

AUTO_EXPR_FUNC(entityUtil) ACMD_NAME("EntGetTotalAttribModMagnitudeByTag");
F32 gclExprEntGetTotalAttribModMagnitudeByTag(	SA_PARAM_OP_VALID Entity *pent,
												const char* attribName,
												char *pchTags)
{

	if (pent && pent->pChar)
	{
		static S32 *s_piTags = NULL;
		int attrib = StaticDefineIntGetInt(AttribTypeEnum,attribName);
		eaiClearFast(&s_piTags);

		if (pchTags)
			StaticDefineIntParseStringForInts(PowerTagsEnum, pchTags, &s_piTags, NULL);

		return character_ModsNetGetTotalMagnitudeByTag(pent->pChar, attrib, s_piTags, NULL);
	}
	
	return 0.f;
}

extern F32 AttribResist(Character *character, const char *attribName);
AUTO_EXPR_FUNC(UIGen) ACMD_NAME("EntGetAttribModResist");
F32 gclExprEntGetAttribModResist(SA_PARAM_OP_VALID Entity* pEntity, const char* pchAttrib)
{
	if (!pEntity) return 0.0f;
	if (!pEntity->pChar) return 0.0f;
	if (!pEntity->pChar->pEntParent) pEntity->pChar->pEntParent = pEntity;
	return (1.0f - (1.0f/AttribResist( pEntity->pChar, pchAttrib ))) * 100.0f;
}

AUTO_EXPR_FUNC(UIGen) ACMD_NAME("EntGetAggro");
F32 gclExprEntGetAggro(SA_PARAM_OP_VALID Entity* pSource, SA_PARAM_OP_VALID Entity *pTarget)
{
	if (!pSource || !pTarget) return 0.0f;
	if (!pSource->pChar || !pTarget->pChar) return 0.0f;
	
	return character_GetRelativeDangerValue(pSource->pChar, entGetRef(pTarget));	
}

AUTO_EXPR_FUNC(UIGen) ACMD_NAME("EntGetOfflineAttribModResist");
F32 gclExprEntGetOfflineAttribModResist(SA_PARAM_OP_VALID Entity* pEntity, const char* pchAttrib)
{
	if (!pEntity) return 0;
	if (entGetType(pEntity) != GLOBALTYPE_ENTITYSAVEDPET)
	{
		return gclExprEntGetAttribModResist(pEntity,pchAttrib);
	}
	return gclExprEntGetAttribModResist(savedpet_GetOfflineCopy(entGetContainerID(pEntity)),pchAttrib);
}

AUTO_EXPR_FUNC(UIGen) ACMD_NAME("EntGetVelocity");
F32 gclExprEntGetVelocity(SA_PARAM_OP_VALID Entity *pEntity)
{
	if (pEntity)
	{
		Vec3 vVel;
		entCopyVelocityFG(pEntity, vVel);
		return normalVec3(vVel);
	}
	return 0.0;
}

AUTO_EXPR_FUNC(entityutil) ACMD_NAME("EntCanPerceive");
bool gclExprEntCanPerceive(SA_PARAM_OP_VALID Entity* pEntity)
{
	return pEntity ? !pEntity->bImperceptible : false;
}

AUTO_EXPR_FUNC(entityutil) ACMD_NAME("EntHasCutscene");
bool gclExprEntHasCutscene(SA_PARAM_OP_VALID Entity* pEntity)
{
	return pEntity && pEntity->pPlayer && pEntity->pPlayer->pCutscene;
}

AUTO_EXPR_FUNC(entityutil) ACMD_NAME("EntGetRegionType");
const char* gclExprEntGetRegionType(SA_PARAM_OP_VALID Entity* pEntity)
{
	return pEntity ? StaticDefineIntRevLookup(WorldRegionTypeEnum,entGetWorldRegionTypeOfEnt(pEntity)) : NULL;
}

AUTO_EXPR_FUNC(entityutil) ACMD_NAME("EntRegionMatchesType");
bool gclExprEntRegionMatchesType(SA_PARAM_OP_VALID Entity* pEntity, const char* pchRegionType)
{
	if ( pEntity )
	{
		WorldRegionType eType = entGetWorldRegionTypeOfEnt(pEntity);
		return stricmp( StaticDefineIntRevLookup(WorldRegionTypeEnum,eType), pchRegionType ) == 0;
	}

	return false;
}

AUTO_EXPR_FUNC(entityutil) ACMD_NAME(EntRegionGetNumAllowedPets);
S32 gclExprEntRegionGetNumAllowedPets(SA_PARAM_OP_VALID Entity* pEntity)
{
	if (pEntity)
	{
		Vec3 vEntPos;
		RegionRules *pRegionRules;

		entGetPos(pEntity, vEntPos);
		
		pRegionRules = RegionRulesFromVec3(vEntPos);
		if (pRegionRules)
			return pRegionRules->iAllowedPetsPerPlayer;
	}

	return 0;
}

// Get an Entity's default language. If this is a player it is the language
// of the player's client; otherwise it is the language of the local client.
AUTO_EXPR_FUNC(entityutil) ACMD_NAME(EntGetLanguage);
S32 gclExprEntGetLanguage(SA_PARAM_OP_VALID Entity* pEntity)
{
	return entGetLanguage(pEntity);
}

AUTO_EXPR_FUNC(entityutil) ACMD_NAME("EntGetEncounterReticleEntity");
SA_RET_OP_VALID Entity* gclExprEntGetEncounterReticleEntity( SA_PARAM_OP_VALID Entity* pEnt )
{
	return pEnt && pEnt->pEntUI && pEnt->pEntUI->pEncounterData ? entFromEntityRefAnyPartition(pEnt->pEntUI->pEncounterData->erEnt) : NULL;
}

AUTO_EXPR_FUNC(entityutil) ACMD_NAME("EntGetOffscreenReticleEntity");
SA_RET_OP_VALID Entity* gclExprEntGetOffscreenReticleEntity( SA_PARAM_OP_VALID Entity* pEnt )
{
	return pEnt && pEnt->pEntUI && pEnt->pEntUI->pOffscreenData ? entFromEntityRefAnyPartition(pEnt->pEntUI->pOffscreenData->erEnt) : NULL;
}

AUTO_EXPR_FUNC(entityutil) ACMD_NAME("EntGetEncounterReticleCount");
S32 gclExprEntGetEncounterReticleCount( SA_PARAM_OP_VALID Entity* pEnt )
{
	return pEnt && pEnt->pEntUI && pEnt->pEntUI->pEncounterData ? pEnt->pEntUI->pEncounterData->iCount : 0;
}

AUTO_EXPR_FUNC(entityutil) ACMD_NAME("EntGetOffscreenReticleCount");
S32 gclExprEntGetOffscreenReticleCount( SA_PARAM_OP_VALID Entity* pEnt )
{
	return pEnt && pEnt->pEntUI && pEnt->pEntUI->pOffscreenData ? pEnt->pEntUI->pOffscreenData->iCount : 0;
}

AUTO_EXPR_FUNC(entityutil) ACMD_NAME("EntAreOffscreenReticleTypesTheSame");
bool gclExprEntAreOffscreenReticleTypesTheSame( SA_PARAM_OP_VALID Entity* pEnt )
{
	return pEnt && pEnt->pEntUI && pEnt->pEntUI->pOffscreenData ? pEnt->pEntUI->pOffscreenData->bSame : false;
}

AUTO_EXPR_FUNC(entityutil) ACMD_NAME("EntGetOffscreenReticleType");
const char* gclExprGetOffscreenType( SA_PARAM_OP_VALID Entity* pEnt )
{
	if ( pEnt && pEnt->pEntUI && pEnt->pEntUI->pOffscreenData )
	{
		return StaticDefineIntRevLookup( OffscreenTypeEnum, pEnt->pEntUI->pOffscreenData->iType );
	}
	return "";
}

AUTO_EXPR_FUNC(entityutil) ACMD_NAME("EntIsFarEncounter");
bool gclExprEntIsFarEncounter( SA_PARAM_OP_VALID Entity *pEntity )
{
	return pEntity && pEntity->pCritter && pEntity->pCritter->bEncounterFar;
}

AUTO_EXPR_FUNC(entityutil) ACMD_NAME("EntHasGen");
SA_RET_OP_VALID UIGen *gclExprEntHasGen(SA_PARAM_OP_VALID Entity *pEntity)
{
	if ( pEntity==NULL || pEntity->pEntUI==NULL )
		return NULL;

	return pEntity->pEntUI->pGen;
}

AUTO_EXPR_FUNC(entityutil) ACMD_NAME("EntGetGen");
SA_RET_NN_VALID UIGen *gclExprEntGetGen(SA_PARAM_OP_VALID Entity *pEntity)
{
	devassert(pEntity && pEntity->pEntUI && pEntity->pEntUI->pGen);
	return pEntity->pEntUI->pGen;
}
/****************************************************************/

//
// Format the name of a space encounter reticle
// Note that the message strings are defined in data/ui/gens/HUD/Entities/ReticleStrings.ms
//
AUTO_EXPR_FUNC(entityutil) ACMD_NAME("EntFormatEncounterReticleNameString");
const char * gclExprEntFormatEncounterReticleString( SA_PARAM_OP_VALID Entity* pEnt )
{
	Entity *pPlayer = entActivePlayerPtr();
	char *messageKey = NULL;
	const char *translatedType = NULL;
	static char *s_ReturnString = NULL;

	estrClear(&s_ReturnString);

	if ( ( pPlayer != NULL ) && ( pEnt->pEntUI != NULL ) && ( pEnt->pEntUI->pEncounterData ) )
	{
		int count = pEnt->pEntUI->pEncounterData->iCount;
		bool multiple = count > 1;
		if ( team_OnSameTeam(pPlayer, pEnt) )
		{
			messageKey = "EncounterReticle.Type.Teammates";
		}
		else if ( gclEntGetIsFriend( pPlayer, pEnt ) )
		{
			if ( multiple )
			{
				messageKey = "EncounterReticle.Type.Allies";
			}
			else
			{
				messageKey = "EncounterReticle.Type.Ally";
			}
		}
		else
		{
			if ( multiple )
			{
				messageKey = "EncounterReticle.Type.Enemies";
			}
			else
			{
				messageKey = "EncounterReticle.Type.Enemy";
			}
		}
		translatedType = TranslateMessageKeyDefault(messageKey, "[UNTRANSLATED]EncounterReticle");

		if ( multiple )
		{
			estrPrintf(&s_ReturnString, "%s (%d)", translatedType, count);
		}
		else
		{
			estrCopy2(&s_ReturnString, translatedType);
		}
	}

	return s_ReturnString;
}

// Check whether an ent has an artificial level adjustment (i.e. "scary monster tech").
AUTO_EXPR_FUNC(entityutil) ACMD_NAME(EntIsLevelAdjusting);
bool gclExprEntIsLevelAdjusting(SA_PARAM_OP_VALID Entity *pEnt)
{
	return SAFE_MEMBER2(pEnt, pChar, bLevelAdjusting);
}

// Check whether an ent has available research.
AUTO_EXPR_FUNC(entityutil) ACMD_NAME(EntHasResearch);
bool gclExprEntHasResearch(SA_PARAM_OP_VALID Entity *pEnt)
{
	return (pEnt && pEnt->pChar) ? pEnt->pChar->bHasAvailableResearch : false;
}

// Has this entity been damaged lately by the local player?  If we need this information for other damagers,
// we will have to write a more complicated system
AUTO_EXPR_FUNC(entityutil) ACMD_NAME(EntWasDamagedByPlayerWithinTime);
bool gclExprEntWasDamagedByPlayerWithinTime(SA_PARAM_OP_VALID Entity *pEnt,F32 fTimeWindow)
{
	if(pEnt && pEnt->pEntUI && pEnt->pEntUI->uiLastDamagedByPlayer)
	{
		F32 fTime = (gGCLState.totalElapsedTimeMs - pEnt->pEntUI->uiLastDamagedByPlayer) / 1000.0;
		if (fTime < fTimeWindow)
		{
			return true;
		}
	}
	return false;
}

// Angle is in degrees
// This is bound by the resolution of how we track last damaged directions. 
// Currently it is just four cardinal directions
AUTO_EXPR_FUNC(UIGen) ACMD_NAME(EntTimeSinceDamagedInDirection);
F32 gclExprEntTimeSinceDamagedInDirection(SA_PARAM_OP_VALID Entity *pEnt, F32 angle)
{
	if(pEnt && pEnt->pEntUI)
	{
		EUIDamageDirection eDir = getUIDamageDirectionFromAngle(RAD(angle));

		devassert(eDir >= 0 && eDir < EUIDamageDirection_COUNT);

		if (pEnt->pEntUI->uiLastDamageDirectionTimes[eDir])
		{
			return (gGCLState.totalElapsedTimeMs - pEnt->pEntUI->uiLastDamageDirectionTimes[eDir]) / 1000.f;
		}
	}
	return FLT_MAX;
}

// Move iToBank amount of numeric, negative values move it from the sharedbank
AUTO_EXPR_FUNC(UIGen) ACMD_NAME(SharedBankAddNumeric);
void exprSharedBankAddNumeric(SA_PARAM_OP_VALID Entity *pEnt, int iToBank, const char *pcNumeric)
{
	if(pEnt && pEnt->pPlayer && SharedBank_ValidateNumericTransfer(pEnt, iToBank, pcNumeric) == SharedBankError_None)
	{
		ServerCmd_SharedBank_AddNumeric(iToBank, pcNumeric);
	}
}

typedef struct UserEntity
{
	Entity *pEntityCopy;
	REF_TO(Entity) hEntityCopy;
	EntityRef erEntityRef;
	ContainerID iTeamMemberID;
	GlobalType eSubEntType;
	ContainerID eSubEntID;
	bool bPlayer : 1;
	bool bGuildBank : 1;
	bool bSharedBank : 1;
	bool bSubEntityRef : 1;
} UserEntity;

static UserEntity s_aEntities[2];

static void ResetUserEntity(SA_PARAM_NN_VALID UserEntity *pUserEnt)
{
	if (IS_HANDLE_ACTIVE(pUserEnt->hEntityCopy))
		REMOVE_HANDLE(pUserEnt->hEntityCopy);
	if (pUserEnt->pEntityCopy)
		StructDestroySafe(parse_Entity, &pUserEnt->pEntityCopy);
	ZeroStruct(pUserEnt);
}

static void UpdateUserEntityPointer(SA_PARAM_NN_VALID UserEntity *pUserEnt)
{
	Entity *pEnt = NULL;
	int i;
	char achName[256];

	for (i = 0; i < ARRAY_SIZE(s_aEntities); i++)
	{
		if (&s_aEntities[i] == pUserEnt)
		{
			break;
		}
	}

	if (i == ARRAY_SIZE(s_aEntities))
	{
		return;
	}

	if (pUserEnt->pEntityCopy)
	{
		pEnt = pUserEnt->pEntityCopy;
	}
	else if (pUserEnt->bPlayer)
	{
		pEnt = entActivePlayerPtr();
	}
	else if (pUserEnt->bGuildBank)
	{
		Entity *pPlayer = entActivePlayerPtr();
		pEnt = pPlayer ? guild_GetGuildBank(pPlayer) : NULL;
	}
	else if (pUserEnt->eSubEntType && pUserEnt->eSubEntID)
	{
		if (pUserEnt->bSubEntityRef)
		{
			Entity *pPlayer = entActivePlayerPtr();
			int j;
			if (pPlayer && pPlayer->pSaved && eaSize(&pPlayer->pSaved->ppOwnedContainers))
			{
				for (j = eaSize(&pPlayer->pSaved->ppOwnedContainers) - 1; j >= 0; j--)
				{
					Entity *pSubEntity = GET_REF(pPlayer->pSaved->ppOwnedContainers[j]->hPetRef);
					if (pSubEntity && entGetType(pSubEntity) == pUserEnt->eSubEntType && entGetContainerID(pSubEntity) == pUserEnt->eSubEntID)
					{
						pEnt = pSubEntity;
						break;
					}
				}
			}
		}
		else
			pEnt = entity_GetSubEntity(PARTITION_CLIENT, entActivePlayerPtr(), pUserEnt->eSubEntType, pUserEnt->eSubEntID);
	}
	else if (pUserEnt->iTeamMemberID)
	{
		pEnt = entFromContainerIDAnyPartition(GLOBALTYPE_ENTITYPLAYER, pUserEnt->iTeamMemberID);

		if (!pEnt)
		{
			Entity *pPlayer = entActivePlayerPtr();
			Team *pTeam = pPlayer ? team_GetTeam(pPlayer) : NULL;
			S32 j;
			if (pTeam)
			{
				for (j = eaSize(&pTeam->eaMembers) - 1; j >= 0; j--)
				{
					if (pTeam->eaMembers[j]->iEntID == pUserEnt->iTeamMemberID)
					{
						pEnt = GET_REF(pTeam->eaMembers[j]->hEnt);
						break;
					}
				}
			}
		}
	}
	else if (IS_HANDLE_ACTIVE(pUserEnt->hEntityCopy))
	{
		pEnt = GET_REF(pUserEnt->hEntityCopy);
	}
	else if (pUserEnt->erEntityRef)
	{
		pEnt = entFromEntityRefAnyPartition(pUserEnt->erEntityRef);
	}

	// Set entity pointer
	sprintf(achName, "UserEntity%d", i);
	ui_GenSetPointerVar(i > 0 ? achName : "UserEntity", pEnt, parse_Entity);
}

AUTO_EXPR_FUNC(UIGen) ACMD_NAME(SetUserEntityToSubEntity);
void gclExprSetUserEntityToSubEntity(S32 iEntity, S32 iType, S32 iEntID)
{
	UserEntity *pUserEnt;

	if (iEntity < 0 || ARRAY_SIZE(s_aEntities) <= iEntity)
		return;

	pUserEnt = &s_aEntities[iEntity];
	ResetUserEntity(pUserEnt);

	pUserEnt->eSubEntType = iType;
	pUserEnt->eSubEntID = iEntID;
	UpdateUserEntityPointer(pUserEnt);
}

AUTO_EXPR_FUNC(UIGen) ACMD_NAME(SetUserEntityFake);
void gclExprSetUserEntityCopy(S32 iEntity, SA_PARAM_OP_VALID Entity *pOwner, SA_PARAM_OP_VALID Entity *pEnt)
{
	UserEntity *pUserEnt;

	if (iEntity < 0 || ARRAY_SIZE(s_aEntities) <= iEntity)
		return;

	pUserEnt = &s_aEntities[iEntity];
	ResetUserEntity(pUserEnt);

	pUserEnt->pEntityCopy = entity_CreateOwnerCopy(pOwner, pEnt, false, true, true, true, true);
	UpdateUserEntityPointer(pUserEnt);
}

AUTO_EXPR_FUNC(UIGen) ACMD_NAME(SetUserEntityRef);
void gclExprSetUserEntityRef(S32 iEntity, bool bWantRef)
{
	UserEntity *pUserEnt;

	if (iEntity < 0 || ARRAY_SIZE(s_aEntities) <= iEntity)
		return;

	pUserEnt = &s_aEntities[iEntity];

	bWantRef = !!bWantRef;
	if (pUserEnt->bSubEntityRef != bWantRef)
	{
		pUserEnt->bSubEntityRef = bWantRef;
		UpdateUserEntityPointer(pUserEnt);
	}
}

AUTO_EXPR_FUNC(UIGen) ACMD_NAME(SetUserEntity);
void gclExprSetUserEntity(S32 iEntity, SA_PARAM_OP_VALID Entity *pEnt)
{
	Entity *pPlayer = entActivePlayerPtr();
	Entity *pGuildBank = pPlayer ? guild_GetGuildBank(pPlayer) : NULL;
	Team *pTeam = pPlayer ? team_GetTeam(pPlayer) : NULL;
	UserEntity *pUserEnt;
	S32 i;
	char idBuf[128];

	if (iEntity < 0 || ARRAY_SIZE(s_aEntities) <= iEntity)
		return;

	pUserEnt = &s_aEntities[iEntity];
	ResetUserEntity(pUserEnt);

	if (!pEnt)
	{
		UpdateUserEntityPointer(pUserEnt);
		return;
	}

	// Here follows bloody black magic to figure out how to track the
	// entity. It's designed to be fairly smart about what entity to
	// use in gclSetUserEntities (e.g. if you refer to an owned entity
	// and that owned entity is the active entity, use that instead
	// or use the pet ref if it's not the active entity).

	if (pPlayer && pEnt == pPlayer)
	{
		pUserEnt->bPlayer = true;
		UpdateUserEntityPointer(pUserEnt);
		return;
	}

	if (pGuildBank == pEnt)
	{
		pUserEnt->bGuildBank = true;
		UpdateUserEntityPointer(pUserEnt);
		return;
	}

	if (pPlayer && pPlayer->pSaved && eaSize(&pPlayer->pSaved->ppOwnedContainers))
	{
		for (i = eaSize(&pPlayer->pSaved->ppOwnedContainers) - 1; i >= 0; i--)
		{
			if (GET_REF(pPlayer->pSaved->ppOwnedContainers[i]->hPetRef) == pEnt)
			{
				pUserEnt->eSubEntType = entGetType(pEnt);
				pUserEnt->eSubEntID = entGetContainerID(pEnt);
				UpdateUserEntityPointer(pUserEnt);
				return;
			}
		}
	}

	if (pTeam && entGetType(pEnt) == GLOBALTYPE_ENTITYPLAYER)
	{
		for (i = eaSize(&pTeam->eaMembers) - 1; i >= 0; i--)
		{
			if (pTeam->eaMembers[i]->iEntID == entGetContainerID(pEnt))
			{
				pUserEnt->iTeamMemberID = entGetContainerID(pEnt);
				UpdateUserEntityPointer(pUserEnt);
				return;
			}
		}
	}

	if (RefSystem_ReferentFromString(GlobalTypeToCopyDictionaryName(entGetType(pEnt)), ContainerIDToString(entGetContainerID(pEnt), idBuf)) == pEnt)
	{
		SET_HANDLE_FROM_REFDATA(GlobalTypeToCopyDictionaryName(entGetType(pEnt)), pEnt, pUserEnt->hEntityCopy);
		UpdateUserEntityPointer(pUserEnt);
		return;
	}

	pUserEnt->erEntityRef = entGetRef(pEnt);
	UpdateUserEntityPointer(pUserEnt);
}

AUTO_EXPR_FUNC(UIGen) ACMD_NAME(ClearUserEntity);
void gclExprClearUserEntity(S32 iEntity)
{
	UserEntity *pUserEnt;

	if (iEntity < 0 || ARRAY_SIZE(s_aEntities) <= iEntity)
		return;

	pUserEnt = &s_aEntities[iEntity];
	ResetUserEntity(pUserEnt);
	UpdateUserEntityPointer(pUserEnt);
}

void gclSetUserEntities(void)
{
	S32 i;

	for (i = 0; i < ARRAY_SIZE(s_aEntities); i++)
	{
		UpdateUserEntityPointer(&s_aEntities[i]);
	}
}

void gclClearUserEntities(void)
{
	char achName[256];
	S32 i;

	for (i = 0; i < ARRAY_SIZE(s_aEntities); i++)
	{
		sprintf(achName, "UserEntity%d", i);
		ui_GenSetPointerVar(i > 0 ? achName : "UserEntity", NULL, parse_Entity);
	}
}

AUTO_RUN;
void gclInitializeUserEntities(void)
{
	char achName[256];
	S32 i;

	for (i = 0; i < ARRAY_SIZE(s_aEntities); i++)
	{
		sprintf(achName, "UserEntity%d", i);
		ui_GenInitPointerVar(i > 0 ? achName : "UserEntity", parse_Entity);
	}
}
