
#include "gclUIGenMapExpr.h"
#include "gclUIGenMap.h"
#include "gclMapState.h"
#include "mission_common.h"
#include "Entity.h"
#include "gclEntity.h"
#include "Player.h"
#include "WorldGrid.h"
#include "GfxCamera.h"
#include "GraphicsLib.h"
#include "MapDescription.h"
#include "mapstate_common.h"
#include "gclHUDOptions.h"
#include "GameClientLib.h"

#include "AutoGen/mission_common_h_ast.h"
#include "AutoGen/Player_h_ast.h"
#include "AutoGen/Entity_h_ast.h"
#include "AutoGen/GameServerLib_autogen_ServerCmdWrappers.h"

AUTO_RUN_ANON(memBudgetAddMapping(__FILE__, BUDGET_UISystem););


// Get the user's configured map icon flags.
AUTO_EXPR_FUNC(entityutil) ACMD_NAME(EntGetMapIconFlags);
U32 exprEntGetMapIconFlags(SA_PARAM_OP_VALID Entity *pEnt)
{
	PlayerHUDOptions* pHUDOptions = entGetCurrentHUDOptions(pEnt);
	return SAFE_MEMBER(pHUDOptions, eMapIconFlags);
}

// Add a "saved" waypoint.
AUTO_EXPR_FUNC(UIGen) ACMD_NAME(GenMapAddSavedWaypoint);
void exprGenMapAddSavedWaypoint(SA_PARAM_OP_VALID UIGenMapIcon *pIcon)
{
	if (pIcon)
		ServerCmd_AddSavedWaypoint(pIcon->v3WorldPos);
}

// Remove the "saved" waypoint.
AUTO_EXPR_FUNC(entityutil) ACMD_NAME(RemoveSavedWaypoint);
void exprRemoveSavedWaypoint(void)
{
	ServerCmd_RemoveSavedWaypoint();
}

// Center the map on this entity.
AUTO_EXPR_FUNC(UIGen) ACMD_NAME("GenMapCenterOnMissionWaypoint");
void ui_GenExprMapCenterOnMissionWaypoint(SA_PARAM_NN_VALID UIGen *pGen, SA_PARAM_OP_VALID Mission *pMission)
{
	if (UI_GEN_IS_TYPE(pGen, kUIGenTypeMap))
	{
		S32 i;
		Entity *pEnt = entActivePlayerPtr();
		const char *pchMapName = zmapInfoGetPublicName(NULL);
		bool foundWaypoints = false;
		// The box that contains the waypoints
		F32 fMaxFloat = 3.402823466e+38F;
		F32 fXMin = fMaxFloat;
		F32 fXMax = -fMaxFloat;
		F32 fYMin = fMaxFloat;
		F32 fYMax = -fMaxFloat;
		F32 fZMin = fMaxFloat;
		F32 fZMax = -fMaxFloat;
		MissionDef *pMissionDef = mission_GetDef(pMission);
		UIGenMapState *pState = UI_GEN_STATE(pGen, Map);
		MissionInfo *pMissionInfo = SAFE_MEMBER2(pEnt, pPlayer, missionInfo);
		MinimapWaypoint **eaWaypoints = NULL;

		// Get the mission's waypoints
		eaCopy(&eaWaypoints, &pMissionInfo->waypointList);
		for (i = 0; i < eaSize(&pEnt->pPlayer->ppMyWaypoints); i++)
		{
			if (pEnt->pPlayer->ppMyWaypoints[i]->MapCreatedOn
				&& zmapInfoGetPublicName(NULL)
				&& !stricmp(pchMapName, pEnt->pPlayer->ppMyWaypoints[i]->MapCreatedOn))
			{
				eaPush(&eaWaypoints, pEnt->pPlayer->ppMyWaypoints[i]);
			}
		}
		for (i = 0; i < eaSize(&eaWaypoints); i++)
		{
			MinimapWaypoint *pWaypoint = eaWaypoints[i];
			MissionDef *pWaypointsMissionDef = (pWaypoint && pWaypoint->pchMissionRefString) ? missiondef_DefFromRefString(pWaypoint->pchMissionRefString) : NULL;
			if (pWaypointsMissionDef)
			{
				while (pWaypointsMissionDef != pMissionDef && GET_REF(pWaypointsMissionDef->parentDef))
					pWaypointsMissionDef = GET_REF(pWaypointsMissionDef->parentDef);

				if (pWaypointsMissionDef == pMissionDef)
				{
					// Find a box that the waypoint will fit into
					F32 fRadius = MAX(pWaypoint->fXAxisRadius, pWaypoint->fYAxisRadius);
					MIN1(fXMin, pWaypoint->pos[0] - fRadius);
					MAX1(fXMax, pWaypoint->pos[0] + fRadius);
					MIN1(fYMin, pWaypoint->pos[1] - fRadius);
					MAX1(fYMax, pWaypoint->pos[1] + fRadius);
					MIN1(fZMin, pWaypoint->pos[2] - fRadius);
					MAX1(fZMax, pWaypoint->pos[2] + fRadius);
					pState->pchSelectedMissionRefString = pWaypointsMissionDef->pchRefString;
					foundWaypoints = true;
				}
			}
		}
		if (foundWaypoints)
		{
			// Find the center
			pState->v3Target[0] = (fXMax-fXMin)/2 + fXMin;
			pState->v3Target[1] = (fYMax-fYMin)/2 + fYMin;
			pState->v3Target[2] = (fZMax-fZMin)/2 + fZMin;
			pState->bFollowing = true;
		}
	}
}

AUTO_EXPR_FUNC(UIGen) ACMD_NAME("GenMapCenterOnMissionWaypointDelayed");
void ui_GenExprMapCenterOnMissionWaypointDelayed(SA_PARAM_NN_VALID UIGen *pGen, SA_PARAM_OP_VALID Mission *pMission)
{
	if (UI_GEN_IS_TYPE(pGen, kUIGenTypeMap))
	{
		S32 i;
		Entity *pEnt = entActivePlayerPtr();
		const char *pchMapName = zmapInfoGetPublicName(NULL);
		bool foundWaypoints = false;
		// The box that contains the waypoints
		F32 fMaxFloat = 3.402823466e+38F;
		F32 fXMin = fMaxFloat;
		F32 fXMax = -fMaxFloat;
		F32 fYMin = fMaxFloat;
		F32 fYMax = -fMaxFloat;
		F32 fZMin = fMaxFloat;
		F32 fZMax = -fMaxFloat;
		MissionDef *pMissionDef = mission_GetDef(pMission);
		UIGenMapState *pState = UI_GEN_STATE(pGen, Map);
		MissionInfo *pMissionInfo = SAFE_MEMBER2(pEnt, pPlayer, missionInfo);
		MinimapWaypoint **eaWaypoints = NULL;

		// Get the mission's waypoints
		eaCopy(&eaWaypoints, &pMissionInfo->waypointList);
		for (i = 0; i < eaSize(&pEnt->pPlayer->ppMyWaypoints); i++)
		{
			if (pEnt->pPlayer->ppMyWaypoints[i]->MapCreatedOn
				&& zmapInfoGetPublicName(NULL)
				&& !stricmp(pchMapName, pEnt->pPlayer->ppMyWaypoints[i]->MapCreatedOn))
			{
				eaPush(&eaWaypoints, pEnt->pPlayer->ppMyWaypoints[i]);
			}
		}
		for (i = 0; i < eaSize(&eaWaypoints); i++)
		{
			MinimapWaypoint *pWaypoint = eaWaypoints[i];
			MissionDef *pWaypointsMissionDef = (pWaypoint && pWaypoint->pchMissionRefString) ? missiondef_DefFromRefString(pWaypoint->pchMissionRefString) : NULL;
			if (pWaypointsMissionDef)
			{
				while (pWaypointsMissionDef != pMissionDef && GET_REF(pWaypointsMissionDef->parentDef))
					pWaypointsMissionDef = GET_REF(pWaypointsMissionDef->parentDef);

				if (pWaypointsMissionDef == pMissionDef)
				{
					// Find a box that the waypoint will fit into
					F32 fRadius = MAX(pWaypoint->fXAxisRadius, pWaypoint->fYAxisRadius);
					MIN1(fXMin, pWaypoint->pos[0] - fRadius);
					MAX1(fXMax, pWaypoint->pos[0] + fRadius);
					MIN1(fYMin, pWaypoint->pos[1] - fRadius);
					MAX1(fYMax, pWaypoint->pos[1] + fRadius);
					MIN1(fZMin, pWaypoint->pos[2] - fRadius);
					MAX1(fZMax, pWaypoint->pos[2] + fRadius);
					pState->pchSelectedMissionRefString = pWaypointsMissionDef->pchRefString;
					foundWaypoints = true;
				}
			}
		}
		if (foundWaypoints)
		{
			// Find the center
			pState->v3DelayedTarget[0] = (fXMax-fXMin)/2 + fXMin;
			pState->v3DelayedTarget[1] = (fYMax-fYMin)/2 + fYMin;
			pState->v3DelayedTarget[2] = (fZMax-fZMin)/2 + fZMin;
			pState->bHasDelayedTarget = true;
		}
	}
}

AUTO_EXPR_FUNC(UIGen) ACMD_NAME("GenMapHasWaypointForMission");
bool ui_GenExprMapHasWaypointForMission(SA_PARAM_NN_VALID UIGen *pGen, SA_PARAM_OP_VALID Mission *pMission)
{
	if (UI_GEN_IS_TYPE(pGen, kUIGenTypeMap))
	{
		S32 i;
		Entity *pEnt = entActivePlayerPtr();
		const char *pchMapName = zmapInfoGetPublicName(NULL);
		MissionDef *pMissionDef = mission_GetDef(pMission);
		UIGenMapState *pState = UI_GEN_STATE(pGen, Map);
		MissionInfo *pMissionInfo = SAFE_MEMBER2(pEnt, pPlayer, missionInfo);
		MinimapWaypoint **eaWaypoints = NULL;

		// Get the mission's waypoints
		eaCopy(&eaWaypoints, &pMissionInfo->waypointList);
		for (i = 0; i < eaSize(&pEnt->pPlayer->ppMyWaypoints); i++)
		{
			if (pEnt->pPlayer->ppMyWaypoints[i]->MapCreatedOn
				&& zmapInfoGetPublicName(NULL)
				&& !stricmp(pchMapName, pEnt->pPlayer->ppMyWaypoints[i]->MapCreatedOn))
			{
				eaPush(&eaWaypoints, pEnt->pPlayer->ppMyWaypoints[i]);
			}
		}
		for (i = 0; i < eaSize(&eaWaypoints); i++)
		{
			MinimapWaypoint *pWaypoint = eaWaypoints[i];
			MissionDef *pWaypointsMissionDef = (pWaypoint && pWaypoint->pchMissionRefString) ? missiondef_DefFromRefString(pWaypoint->pchMissionRefString) : NULL;
			MissionDef *pParentDef = pWaypointsMissionDef;

			if (pWaypointsMissionDef && (pWaypointsMissionDef == pMissionDef))
				return true;

			while (pParentDef && GET_REF(pParentDef->parentDef))
			{
				pParentDef = GET_REF(pParentDef->parentDef);

				if (pParentDef && (pParentDef == pMissionDef))
					return true;
			}
		}
	}
	return false;
}

// Center the map on this entity.
AUTO_EXPR_FUNC(UIGen) ACMD_NAME("GenMapCenterOnDelayedTarget");
void ui_GenExprMapCenterOnDelayedTarget(SA_PARAM_NN_VALID UIGen *pGen)
{
	if (UI_GEN_IS_TYPE(pGen, kUIGenTypeMap))
	{
		UIGenMapState *pState = UI_GEN_STATE(pGen, Map);
		if(pState->bHasDelayedTarget)
		{
			copyVec3(pState->v3DelayedTarget, pState->v3Target);
			pState->bHasDelayedTarget = false;
		}
	}
}

//Center a map on the map icon corresponding to the passed key icon
AUTO_EXPR_FUNC(UIGen) ACMD_NAME("GenMapCenterOnKeyIcon");
void ui_GenExprMapCenterOnMapKeyIcon(SA_PARAM_NN_VALID UIGen *pGen, UIGenMapKeyIcon *pKeyIcon){

	if (UI_GEN_IS_TYPE(pGen, kUIGenTypeMap) && pKeyIcon && !pKeyIcon->bIsHeader)
	{
		UIGenMapState *pState = UI_GEN_STATE(pGen, Map);
		Vec3 v3Pos;

		if(pState && GenMapGetNextPositionForKeyIndex(pGen, pKeyIcon, &v3Pos)){
			copyVec3(v3Pos, pState->v3Target);
			pState->bFollowing = true;
		}
	}

}

// Center the minimap on this entity.
AUTO_EXPR_FUNC(UIGen) ACMD_NAME("GenMapCenterOnEntity");
void ui_GenExprMapCenterOnEntity(SA_PARAM_NN_VALID UIGen *pGen, SA_PARAM_OP_VALID Entity *pEnt)
{
	if (UI_GEN_IS_TYPE(pGen, kUIGenTypeMap) && pEnt)
	{
		UIGenMapState *pState = UI_GEN_STATE(pGen, Map);
		entGetPos(pEnt, pState->v3Target);
		pState->bFollowing = true;
	}
}

// Adjust the minimap scale.
AUTO_EXPR_FUNC(UIGen) ACMD_NAME("GenMapAdjustScale");
void ui_GenExprMapAdjustScale(SA_PARAM_NN_VALID UIGen *pGen, F32 fAdjust)
{
	if (UI_GEN_IS_TYPE(pGen, kUIGenTypeMap))
	{
		UIGenMap *pMap = UI_GEN_RESULT(pGen, Map);
		UIGenMapState *pState = UI_GEN_STATE(pGen, Map);
		if (pMap)
		{
			if (pMap->eScaleMode == UIGenMapScalePixelsPerWorldUnit)
				ui_GenMapSetPixelsPerWorldUnit(pGen, pState->fPixelsPerWorldUnit - fAdjust, true);
			else
				ui_GenMapSetScale(pGen, pState->fMapScale + fAdjust, true);

			pState->bFollowing = false;
			pState->hTarget = -1;
		}
	}
}

// Set the minimap scale.
AUTO_EXPR_FUNC(UIGen) ACMD_NAME("GenMapSetScale");
void ui_GenExprMapSetScale(SA_PARAM_NN_VALID UIGen *pGen, F32 fScale)
{
	if (UI_GEN_IS_TYPE(pGen, kUIGenTypeMap))
	{
		UIGenMap *pMap = UI_GEN_RESULT(pGen, Map);
		UIGenMapState *pState = UI_GEN_STATE(pGen, Map);
		if (pMap)
		{
			if (pMap->eScaleMode == UIGenMapScalePixelsPerWorldUnit)
				ui_GenMapSetPixelsPerWorldUnit(pGen, fScale, true);
			else
				ui_GenMapSetScale(pGen, fScale, true);

			pState->bFollowing = false;
			pState->hTarget = -1;
		}
	}
}

AUTO_EXPR_FUNC(UIGen) ACMD_NAME("GenMapGetWidth");
F32 ui_GenExprMapGetWidth(SA_PARAM_NN_VALID UIGen *pGen)
{
	if (UI_GEN_IS_TYPE(pGen, kUIGenTypeMap))
	{
		UIGenMap *pMap = UI_GEN_RESULT(pGen, Map);
		UIGenMapState *pState = UI_GEN_STATE(pGen, Map);
		if(pMap && pState)
		{
			F32 fMapWidth = 0.0f;

			if(pState && pState->pReveal)
			{
				fMapWidth = pState->pReveal->v3RegionMax[0] - pState->pReveal->v3RegionMin[0];
				return fMapWidth;
			}
		}
	}

	return 0.0f;
}

AUTO_EXPR_FUNC(UIGen) ACMD_NAME("GenMapGetScaleMin");
F32 ui_GenExprMapGetScaleMin(SA_PARAM_NN_VALID UIGen *pGen)
{
	if (UI_GEN_IS_TYPE(pGen, kUIGenTypeMap))
	{
		UIGenMap *pMap = UI_GEN_RESULT(pGen, Map);
		UIGenMapState *pState = UI_GEN_STATE(pGen, Map);
		if (pMap && pState)
		{
			MapRevealInfo *pReveal = pState->pReveal;
			
			if (pMap->eMapZoomMode != UIGenMapZoomModeNone && pReveal)
			{
				F32 fRegionWidth = (pState->pReveal->v3RegionMax[0] - pState->pReveal->v3RegionMin[0]);
				F32 fRegionHeight = (pState->pReveal->v3RegionMax[2] - pState->pReveal->v3RegionMin[2]);
				F32 fHorizontalMinScale = fRegionWidth ? (CBoxWidth(&pGen->ScreenBox) / fRegionWidth) : 1;
				F32 fVerticalMinScale = fRegionHeight ? (CBoxHeight(&pGen->ScreenBox) / fRegionHeight) : 1;
				F32 fBest;

				// Make sure scale is acceptable
				if (pMap->eMapZoomMode == UIGenMapZoomModeFilled)
				{
					fBest = max(fHorizontalMinScale, fVerticalMinScale);
				} 
				else
				{
					fBest = min(fHorizontalMinScale, fVerticalMinScale);
				}
				fBest = MAX(pMap->fScaleMin, fBest);

				return fBest;
			}
			else
			{
				return pMap->fScaleMin;
			}
		}
	}
	return 0;
}

AUTO_EXPR_FUNC(UIGen) ACMD_NAME("GenMapGetNeighborhood");
const char* ui_GenExprMapGetNeighborhood(SA_PARAM_OP_VALID Entity *pEnt)
{
	if (pEnt && pEnt->currentNeighborhood && GET_REF(pEnt->currentNeighborhood->hMessage))
	{
		return TranslateMessageRef(pEnt->currentNeighborhood->hMessage);
	}
	else
	{
		DisplayMessage *pMessage = zmapInfoGetDisplayNameMessage(NULL);
		const char *pch = pMessage ? TranslateMessageRef(pMessage->hMessage) : NULL;
		return pch ? pch : zmapInfoGetPublicName(NULL);
	}
}

AUTO_EXPR_FUNC(UIGen) ACMD_NAME("GenMapGetNeighborhoodWithInstance");
const char* ui_GenExprMapGetNeighborhoodWithInstance(SA_PARAM_OP_VALID Entity *pEnt)
{
	static char *esOutput = NULL;
	int iInstanceNumber = gclGetCurrentInstanceIndex();


	if(esOutput)
	{
		estrClear(&esOutput);
	}

	if (pEnt && pEnt->currentNeighborhood && GET_REF(pEnt->currentNeighborhood->hMessage))
	{
		estrPrintf(&esOutput, "%s #%d", TranslateMessageRef(pEnt->currentNeighborhood->hMessage), iInstanceNumber);
	}
	else
	{
		DisplayMessage *pMessage = zmapInfoGetDisplayNameMessage(NULL);
		const char *pch = pMessage ? TranslateMessageRef(pMessage->hMessage) : NULL;
		estrPrintf(&esOutput, "%s #%d", pch ? pch : zmapInfoGetPublicName(NULL), iInstanceNumber);
	}

	return esOutput;
}

AUTO_EXPR_FUNC(UIGen) ACMD_NAME("GetCurrentMapDisplayName");
const char* ui_GenExprGetCurrentMapDisplayName(void)
{
	const char* pchMapName = TranslateMessagePtr(zmapInfoGetDisplayNameMessagePtr(NULL));
	return NULL_TO_EMPTY(pchMapName);
}

AUTO_EXPR_FUNC(UIGen) ACMD_NAME("GetCurrentMapLogicalName");
const char* ui_GenExprGetCurrentMapLogicalName(void)
{
	return zmapInfoGetCurrentName(NULL);
}

AUTO_EXPR_FUNC(UIGen) ACMD_NAME("GetCurrentMapTransitionText");
const char* ui_GetCurrentMapTransitionText(void)
{
	MapState *state = mapStateClient_Get();
	WorldVariable* pMapVar = mapState_GetPublicVarByName(state, "MapTransitionText");
	Message* pTransitionMapVarMsg = pMapVar ? GET_REF(pMapVar->messageVal.hMessage) : NULL;
	const char* pchEpisodeText = pTransitionMapVarMsg ? TranslateMessagePtr(pTransitionMapVarMsg) : NULL;

	if (isDevelopmentMode() && pMapVar && pMapVar->eType != WVAR_MESSAGE)
	{
		Errorf("Error: MapTransitionText variable is not of type 'Message' on map %s",
			ui_GenExprGetCurrentMapDisplayName());
	}
	return NULL_TO_EMPTY(pchEpisodeText);
}

AUTO_EXPR_FUNC(UIGen) ACMD_NAME("GenMapSetIconVisible");
void ui_GenExprMapSetIconVisible(SA_PARAM_OP_VALID Entity *pEnt, U32 eType, bool bShow)
{
	PlayerHUDOptions* pHUDOptions = entGetCurrentHUDOptions(pEnt);
	if (bShow)
	{
		pHUDOptions->eMapIconFlags |= eType;
	}
	else
	{
		pHUDOptions->eMapIconFlags &= ~eType;
	}
	ServerCmd_SetHudOptionsField(pHUDOptions);
}

AUTO_EXPR_FUNC(UIGen) ACMD_NAME("GenMapIsIconVisible");
bool ui_GenExprMapIsIconVisible(SA_PARAM_OP_VALID Entity *pEnt, U32 eType)
{
	PlayerHUDOptions* pHUDOptions = entGetCurrentHUDOptions(pEnt);
	return !!(SAFE_MEMBER(pHUDOptions, eMapIconFlags) & eType);
}

AUTO_EXPR_FUNC(UIGen) ACMD_NAME("GetMapLeaveDestinationName");
const char* ui_GenExprGetMapLeaveDestinationName(SA_PARAM_OP_VALID Entity *pEnt)
{
	if ( pEnt )
	{
		SavedMapDescription* pPossibleDest = entity_GetMapLeaveDestination( pEnt );

		if(zmapInfoGetParentMapName(NULL)) {
			if(zmapInfoGetByPublicName(zmapInfoGetParentMapName(NULL))) {
				DisplayMessage* pDisplayMessage = zmapInfoGetDisplayNameMessage(zmapInfoGetByPublicName(zmapInfoGetParentMapName(NULL)));
				const char* pchName = langTranslateDisplayMessage(entGetLanguage(pEnt),*pDisplayMessage);

				if ( pchName )
					return pchName;
			}
		} else if ( pPossibleDest && pPossibleDest->pZoneMapInfo ) {
			DisplayMessage* pDisplayMessage = zmapInfoGetDisplayNameMessage((ZoneMapInfo*)pPossibleDest->pZoneMapInfo);
			const char* pchName = langTranslateDisplayMessage(entGetLanguage(pEnt),*pDisplayMessage);

			if ( pchName )
				return pchName;
		}
	}
	return "";
}

AUTO_EXPR_FUNC(UIGen) ACMD_NAME("GetReturnFromMissionMapErrorString");
const char* ui_GenExprGetReturnFromMissionMapErrorString(SA_PARAM_OP_VALID Entity *pEnt)
{
	if ( pEnt )
	{
		return StaticDefineIntRevLookup(MissionReturnErrorTypeEnum, entity_GetReturnFromMissionMapError(pEnt));
	}
	return "";
}

AUTO_EXPR_FUNC(UIGen) ACMD_NAME("IsReturnFromMissionMapAvailable");
bool ui_GenExprIsReturnFromMissionMapAvailable(void)
{
	return entity_IsMissionReturnAvailable();
}

AUTO_EXPR_FUNC(UIGen) ACMD_NAME("IsReturnFromMissionMapEnabled");
bool ui_GenExprIsReturnFromMissionMapEnabled(SA_PARAM_OP_VALID Entity *pEnt)
{
	return entity_IsMissionReturnEnabled(pEnt);
}

AUTO_EXPR_FUNC(UIGen) ACMD_NAME("CurrentMapAreDuelsAllowed");
bool ui_GenExprCurrentMapAreDuelsAllowed(void)
{
	return !zmapInfoGetDisableDuels(NULL);
}

AUTO_EXPR_FUNC(UIGen) ACMD_NAME("CurrentMapAreUpsellFeaturesEnabled");
bool ui_GenExprCurrentMapAreUpsellFeaturesEnabled(void)
{
	return zmapInfoGetEnableUpsellFeatures(NULL);
}

AUTO_EXPR_FUNC(UIGen) ACMD_NAME("CurrentMapMatchesType");
bool ui_GenExprCurrentMapMatchesType(U32 eMapType)
{
	return zmapInfoGetMapType(NULL) == (ZoneMapType)eMapType;
}

int ui_MapGetInstanceNumber(void)
{
	Entity *pEnt = entActivePlayerPtr();
	SavedMapDescription *pCurrentMap = entity_GetLastMap(pEnt);
	if(pCurrentMap)
		return (pCurrentMap->mapInstanceIndex);
	return 0;
}

// Set a fake zone image to show instead of the zone the player is actually in.
// The fake zone must be defined by a FakeZone clause in the UIGenMiniMap.
AUTO_EXPR_FUNC(UIGen) ACMD_NAME(GenMapSetFakeZone);
bool ui_GenMapSetFakeZone(ExprContext *pContext, SA_PARAM_OP_VALID UIGen *pGen, const char *pchFakeZone)
{
	if (UI_GEN_IS_TYPE(pGen, kUIGenTypeMap))
	{
		if (UI_GEN_READY(pGen))
		{
			UIGenMap *pMap = UI_GEN_RESULT(pGen, Map);
			UIGenMapState *pState = UI_GEN_STATE(pGen, Map);
			UIGenMapFakeZone *pFakeZone = eaIndexedGetUsingString(&pMap->eaFakeZones, pchFakeZone);

			if (!pFakeZone && pchFakeZone && *pchFakeZone)
			{
				ErrorFilenamef(exprContextGetBlameFile(pContext), "No fake zone %s found in map %s",
					pchFakeZone, pGen->pchName);
			}
			else if (pFakeZone)
			{
				zeroVec3(pState->v3WorldCenter);
			}
			pState->pchFakeZone = pFakeZone ? pFakeZone->pchName : NULL;
			return !!pFakeZone;
		}
		else
		{
			// Don't know if it's correct yet, assume yes.
			UIGenMapState *pState = UI_GEN_STATE(pGen, Map);
			pState->pchFakeZone = pchFakeZone;
			zeroVec3(pState->v3WorldCenter);
			return false;
		}
	}
	else
		return false;
}

// Allows the map UI to query whether the "Change Instance" button should be
//  enabled for this map.
AUTO_EXPR_FUNC(UIGen) ACMD_NAME("MapInstanceChangeAllowed");
bool ui_GenExprMapInstanceChangeAllowed(void)
{
	return gclGetInstanceSwitchingAllowed();
}

// Add a "saved" waypoint from a string representation.
AUTO_EXPR_FUNC(UIGen) ACMD_NAME(GenMapCreateSavedWaypoint);
void exprGenMapCreateSavedWaypoint(const char *pchName)
{
	if (pchName && *pchName)
	{
		Vec3 v3Pos;
		if (sscanf(pchName, "%f %f %f", &v3Pos[0], &v3Pos[1], &v3Pos[2]) == 3)
		{
			ServerCmd_AddSavedWaypoint(v3Pos);
		}
	}
}

// Add a "saved" waypoint from a string representation.
AUTO_EXPR_FUNC(UIGen) ACMD_NAME(TranslateMapName);
const char *exprTranslateMapName(const char *pchLogicalName)
{
	const char* pchMapName = TranslateMessagePtr(zmapInfoGetDisplayNameMessagePtr(zmapInfoGetByPublicName(pchLogicalName)));
	return NULL_TO_EMPTY(pchMapName);
}

//Returns X value of the entity's position
AUTO_EXPR_FUNC(UIGen) ACMD_NAME("EntGetMapPosX");
F32 exprEntGetMapPosX(SA_PARAM_OP_VALID Entity *pEnt)
{
	Vec3 v3Position = {0};
	if(pEnt)
		entGetPos(pEnt, v3Position);
	return v3Position[0];
}

//Returns Y value of the entity's position
AUTO_EXPR_FUNC(UIGen) ACMD_NAME("EntGetMapPosY");
F32 exprEntGetMapPosY(SA_PARAM_OP_VALID Entity *pEnt)
{
	Vec3 v3Position = {0};
	if(pEnt)
		entGetPos(pEnt, v3Position);
	return v3Position[1];
}

//Returns Z value of the entity's position
AUTO_EXPR_FUNC(UIGen) ACMD_NAME("EntGetMapPosZ");
F32 exprEntGetMapPosZ(SA_PARAM_OP_VALID Entity *pEnt)
{
	Vec3 v3Position = {0};
	if(pEnt)
		entGetPos(pEnt, v3Position);
	return v3Position[2];
}

//A model call to set the list for all the items to show up in the map key
AUTO_EXPR_FUNC(UIGen) ACMD_NAME("GenGetMapKeyIcons");
void ui_GenExprGetMapKeyIcons(SA_PARAM_NN_VALID UIGen *pGen) {
	gclGenGetMapKeyIcons(pGen);
}