/***************************************************************************
*     Copyright (c) 2011, Cryptic Studios
*     All Rights Reserved
*     Confidential Property of Cryptic Studios
***************************************************************************/

#include "Cbox.h"
#include "Entity.h"
#include "Expression.h"
#include "GameAccountDataCommon.h"
#include "gclBaseStates.h"
#include "gclEntity.h"
#include "gclUIGen.h"
#include "GfxCamera.h"
#include "GlobalStateMachine.h"
#include "GraphicsLib.h"
#include "itemCommon.h"
#include "inventoryCommon.h"
#include "mission_common.h"
#include "Player.h"
#include "StringCache.h"
#include "WorldGrid.h"
#include "AutoGen/mission_common_h_ast.h"
#include "AutoGen/UIGen_h_ast.h"
#include "AutoGen/WaypointUI_c_ast.h"

AUTO_RUN_ANON(memBudgetAddMapping(__FILE__, BUDGET_UISystem););

AUTO_STRUCT;
typedef struct WaypointData
{
	const char* pchName;	AST(UNOWNED)
	const char* pchIcon;	AST(UNOWNED)
	F32			fX;
	F32			fY;
	F32			fScale;
	F32			fDistance;
	F32			fWorldX;
	F32			fWorldY;
	F32			fWorldZ;
	bool		bShowIcon;
	bool		bLeftEdge;
	bool		bRightEdge;
} WaypointData;

static UIGen s_WaypointFakeParentGen;
static StashTable s_stWaypointsToGens;
static MinimapWaypoint **s_eaItemWaypoints = NULL;
static ContainerID s_uiEntID = 0;

AUTO_RUN;
void gclWaypoint_Init(void)
{
	s_stWaypointsToGens = stashTableCreateAddress(32);
	StructInit(parse_UIGen, &s_WaypointFakeParentGen);
	s_WaypointFakeParentGen.pResult = StructCreate(parse_UIGenInternal);
	s_WaypointFakeParentGen.bIsRoot = true;
}

static UIGen* gclWaypoint_GetUIGen(MinimapWaypoint* pWaypoint, S32 iWaypointCount, CBox* pScreenBox, ExprContext* pContext)
{
	static const char *s_pchWaypointVar;
	static const char *s_pchIsOnscreenVar;
	static const char *s_pchWaypointCount;
	static S32 s_iWaypointVar;
	const char *pchGenName = NULL;
	GfxCameraView *pView = gfxGetActiveCameraView();
	Vec2 vScreenPos;
	bool bShouldBeShown = false;
	bool bOnScreen;
	bool bFound;
	MultiVal mv;
	StashElement Elem;
	
	bOnScreen = ProjectPointOnScreen(pWaypoint->pos, pView, &s_WaypointFakeParentGen.ScreenBox, pScreenBox, vScreenPos);

	if (!s_pchWaypointVar)
		s_pchWaypointVar = allocAddString("Waypoint");
	if (!s_pchIsOnscreenVar)
		s_pchIsOnscreenVar = allocAddString("IsOnscreen");
	if (!s_pchWaypointCount)
		s_pchWaypointCount = allocAddString("WaypointCount");

	exprContextSetPointerVarPooledCached(pContext, s_pchWaypointVar, pWaypoint, parse_MinimapWaypoint, true, true, &s_iWaypointVar);
	exprContextSetIntVarPooledCached(pContext, s_pchWaypointCount, iWaypointCount, NULL);
	exprContextSetIntVarPooledCached(pContext, s_pchIsOnscreenVar, bOnScreen, NULL);

	if (g_WaypointGenOffscreenExpression.pExpression)
	{
		ui_GenTickMouse(&s_WaypointFakeParentGen);
		exprEvaluate(g_WaypointGenOffscreenExpression.pExpression, pContext, &mv);
		bShouldBeShown = !!mv.intval;
	}
	else if (bOnScreen)
	{
		bShouldBeShown = true;
	}

	if (bShouldBeShown)
	{
		exprEvaluate(g_WaypointGenExpression.pExpression, pContext, &mv);
		pchGenName = allocFindString(MultiValGetString(&mv, NULL));
	}
	
	bFound = stashAddressFindElement(s_stWaypointsToGens, pWaypoint, &Elem);
	if (pchGenName && *pchGenName)
	{
		UIGen *pGen = NULL;
		if (bFound)
		{
			UIGenWaypointState* pState;
			pGen = stashElementGetPointer(Elem);
			pState = pGen ? UI_GEN_STATE(pGen, Waypoint) : NULL;
			if (!pGen || pGen->pchName != pchGenName || pState->pWaypoint != pWaypoint)
			{
				UIGen *pToClone = RefSystem_ReferentFromString(UI_GEN_DICTIONARY, pchGenName);
				StructDestroySafe(parse_UIGen, &pGen);
				if (pToClone)
				{
					pGen = ui_GenClone(pToClone);
					stashElementSetPointer(Elem, pGen);
				}
				else
					stashRemovePointer(s_stWaypointsToGens, pWaypoint, NULL);
			}
		}
		else
		{
			UIGen *pToClone = RefSystem_ReferentFromString(UI_GEN_DICTIONARY, pchGenName);
			if (pToClone)
				pGen = ui_GenClone(pToClone);
			stashAddPointer(s_stWaypointsToGens, pWaypoint, pGen, false);
		}
		if (pGen)
		{
			F32 fWidth = pGen->pBase->pos.Width.fMagnitude;
			F32 fHeight = pGen->pBase->pos.Height.fMagnitude;
			UIGenWaypointState *pState = UI_GEN_STATE(pGen, Waypoint);
			pState->pWaypoint = pWaypoint;
			pGen->ScreenBox = s_WaypointFakeParentGen.ScreenBox;

			CreateScreenBoxFromScreenPosition(&pGen->ScreenBox, pScreenBox, vScreenPos, fWidth, fHeight);
		}
		return pGen;
	}
	else if (bFound)
	{
		UIGen *pGen = stashElementGetPointer(Elem);
		StructDestroySafe(parse_UIGen, &pGen);
		stashElementSetPointer(Elem, NULL);
	}
	return NULL;
}

static int SortWaypointsByDistance(const MinimapWaypoint **w1, const MinimapWaypoint **w2)
{
	if ((*w1)->distSquared - (*w2)->distSquared > 0.001)
		return 1;
	if ((*w1)->distSquared - (*w2)->distSquared < -0.001)
		return -1;
	return 0;
}

static MinimapWaypoint** gclWaypoint_GetWaypointList(Entity* pEnt)
{
	static MinimapWaypoint** s_eaWaypoints = NULL;
	const char* pchMapName = zmapInfoGetPublicName(NULL);
	MissionInfo* pInfo = mission_GetInfoFromPlayer(pEnt);
	Vec3 vEntPos;
	S32 i;
	
	eaClearFast(&s_eaWaypoints);

	entGetPos(pEnt, vEntPos);

	if (pEnt->pPlayer)
	{
		for (i = 0; i < eaSize(&pEnt->pPlayer->ppMyWaypoints); i++)
		{
			MinimapWaypoint* pWaypoint = pEnt->pPlayer->ppMyWaypoints[i];
			if (pWaypoint->MapCreatedOn && pchMapName && !stricmp(pchMapName, pWaypoint->MapCreatedOn))
			{
				pWaypoint->distSquared = SQR(vEntPos[0] - pWaypoint->pos[0]) + SQR(vEntPos[2] - pWaypoint->pos[2]);
				eaPush(&s_eaWaypoints, pWaypoint);
			}
		}
	}
	if (pInfo)
	{
		for (i = 0; i < eaSize(&pInfo->waypointList); i++)
		{
			MinimapWaypoint* pWaypoint = pInfo->waypointList[i];
			pWaypoint->distSquared = SQR(vEntPos[0] - pWaypoint->pos[0]) + SQR(vEntPos[2] - pWaypoint->pos[2]);
			eaPush(&s_eaWaypoints, pWaypoint);
		}
	}

	eaQSort(s_eaWaypoints, SortWaypointsByDistance);
	return s_eaWaypoints;
}

static void gclWaypoint_RunGens(MinimapWaypoint** eaWaypointList)
{
	static UIGen** s_eaGens = NULL;
	ExprContext *pContext;
	CBox ScreenBox = {0, 0, 0, 0};
	S32 i, iScreenWidth, iScreenHeight;

	gfxGetActiveSurfaceSize(&iScreenWidth, &iScreenHeight);
	ScreenBox.hx = iScreenWidth;
	ScreenBox.hy = iScreenHeight;

	PERFINFO_AUTO_START("gclWaypoint_RunGens: Updating Gens", eaSize(&eaWaypointList));
	eaClearFast(&s_eaGens);
	pContext = ui_GenGetContext(&s_WaypointFakeParentGen);

	for (i = 0; i < eaSize(&eaWaypointList); i++)
	{
		UIGen* pGen = gclWaypoint_GetUIGen(eaWaypointList[i], eaSize(&s_eaGens)+1, &ScreenBox, pContext);
		if (pGen)
			eaPush(&s_eaGens, pGen);
	}

	for (i = 0; i < eaSize(&s_eaGens); i++)
	{
		s_WaypointFakeParentGen.ScreenBox = s_eaGens[i]->ScreenBox;
		ui_GenPointerUpdateCB(s_eaGens[i], &s_WaypointFakeParentGen);
		ui_GenUpdateCB(s_eaGens[i], &s_WaypointFakeParentGen);
		ui_GenLayoutCB(s_eaGens[i], &s_WaypointFakeParentGen);
	}

	for (i = 0; i < eaSize(&s_eaGens); i++)
	{
		ui_GenTickCB(s_eaGens[i], &s_WaypointFakeParentGen);
	}

	for (i = eaSize(&s_eaGens)-1; i >= 0; i--)
	{
		ui_GenDrawCB(s_eaGens[i], &s_WaypointFakeParentGen);
	}
	PERFINFO_AUTO_STOP();
}

void gclWaypoint_UpdateGens(void)
{
	MinimapWaypoint** eaWaypointList;
	Entity* pEnt = entActivePlayerPtr();
	StashTableIterator Iter;
	StashElement Elem;

	if (!GSM_IsStateActive(GCL_GAMEPLAY) || GSM_DoesStateHaveChildStates(GCL_GAMEPLAY))
		return;

	if (!pEnt)
		return;

	eaWaypointList = gclWaypoint_GetWaypointList(pEnt);

	stashGetIterator(s_stWaypointsToGens, &Iter);
	while (stashGetNextElement(&Iter, &Elem))
	{
		MinimapWaypoint* pWaypoint = stashElementGetKey(Elem);
		UIGen* pGen = stashElementGetPointer(Elem);
		if (!pGen || !pWaypoint || eaFind(&eaWaypointList, pWaypoint) < 0)
		{
			stashRemovePointer(s_stWaypointsToGens, pWaypoint, NULL);
			if (pGen)
				StructDestroySafe(parse_UIGen, &pGen);
		}
	}

	if (!eaSize(&eaWaypointList))
	{
		return;
	}

	gclWaypoint_RunGens(eaWaypointList);
}

static const char* gclWaypointGetName(MinimapWaypoint* pWaypoint)
{
	if (GET_REF(pWaypoint->hDisplayNameMsg))
	{
		const char* pchName = TranslateMessageRef(pWaypoint->hDisplayNameMsg);

		// HACK: This is a dirty hack to skip the alignment tag, but I'm being expedient for Trek
		if(strnicmp(pchName, "<align", 6)==0)
		{
			const char *pos = strchr(pchName+6, '>');
			if(pos)
			{
				pchName = pos+1;
			}
		}
		return pchName;
	}
	else if (pWaypoint->pchMissionRefString)
	{
		MissionDef *pDef = missiondef_DefFromRefString(pWaypoint->pchMissionRefString);
		while (pDef && GET_REF(pDef->parentDef))
			pDef = GET_REF(pDef->parentDef);
		return pDef ? TranslateDisplayMessage(pDef->displayNameMsg) : NULL;
	}
	else if (pWaypoint->pchDescription)
	{
		return pWaypoint->pchDescription;
	}
	return NULL;
}

// Deprecated, use waypoint gens
AUTO_EXPR_FUNC(UIGen) ACMD_NAME("SetWaypointHUDIcon");
bool ui_GenExprSetWaypointHUDIcon(SA_PARAM_NN_VALID UIGen *pGen)
{
	static WaypointData* s_pData = NULL;
	CBox ScreenBox = {0, 0, 0, 0};
	S32 iScreenWidth;
	S32 iScreenHeight;
	S32 i;
	F32 height, width;
	Entity *pEntity = entActivePlayerPtr();
	GfxCameraView *pView = gfxGetActiveCameraView();
	const char* mapname = zmapInfoGetPublicName(NULL);
	MinimapWaypoint *pWaypoint = 0;

	// Drop out if no player yet
	if (!pEntity || !pEntity->pPlayer)
		return false;

	if (mapname && *mapname)
	{
		gfxGetActiveSurfaceSize(&iScreenWidth, &iScreenHeight);
		ScreenBox.hx = iScreenWidth;
		ScreenBox.hy = iScreenHeight;
		height = pGen->pBase->pos.Height.fMagnitude;
		width = pGen->pBase->pos.Width.fMagnitude;

		for (i = 0; i < eaSize(&pEntity->pPlayer->ppMyWaypoints); i++)
		{
			if (!strcmp(pEntity->pPlayer->ppMyWaypoints[i]->MapCreatedOn, mapname))
			{
				pWaypoint = pEntity->pPlayer->ppMyWaypoints[i]; // found the waypoint of the map we're on;
				break;
			}
		}

		if (pWaypoint) // a waypoint was found, so let's put the UIGen at the right location.
		{
			if (s_pData == NULL)
				s_pData = StructCreate(parse_WaypointData);

			ProjectCBoxOnScreen(pWaypoint->pos, pView, &pGen->ScreenBox, &ScreenBox, width, height);

			s_pData->fX = pGen->ScreenBox.left;
			s_pData->fY = pGen->ScreenBox.top;
			s_pData->fDistance = sqrt(pWaypoint->distSquared);
			s_pData->fWorldX = pWaypoint->pos[0];
			s_pData->fWorldY = pWaypoint->pos[1];
			s_pData->fWorldZ = pWaypoint->pos[2];

			ui_GenSetPointer(pGen, s_pData, parse_WaypointData);

			return true;
		}
		else
		{
			// no waypoint found, so just move it off screen
			pGen->ScreenBox.right = -1000;
			pGen->ScreenBox.left = -1000;
			pGen->ScreenBox.bottom = -1000;
			pGen->ScreenBox.top = -1000;

			StructDestroySafe(parse_WaypointData, &s_pData);

			s_pData = NULL;
		}
	}

	return false;
}

AUTO_EXPR_FUNC(UIGen) ACMD_NAME("GetWaypointListByType");
void ui_GenExprGetWaypointListByType(SA_PARAM_NN_VALID UIGen *pGen, S32 eWaypointType)
{
	static WaypointData** s_eaData = NULL;
	Entity *pPlayer = entActivePlayerPtr();
	MissionInfo *pInfo = mission_GetInfoFromPlayer(pPlayer);
	S32 i;

	eaClearStruct(&s_eaData, parse_WaypointData);

	if (pInfo)
	{
		for (i = 0; i < eaSize(&pInfo->waypointList); i++)
		{
			MinimapWaypoint *pWaypoint = pInfo->waypointList[i];

			if (pWaypoint->type == eWaypointType)
			{
				WaypointData* pData = StructCreate(parse_WaypointData);

				pData->pchName = gclWaypointGetName(pWaypoint);
				pData->pchIcon = pWaypoint->pchIconTexName;
				pData->fWorldX = pWaypoint->pos[0];
				pData->fWorldY = pWaypoint->pos[1];
				pData->fWorldZ = pWaypoint->pos[2];

				eaPush(&s_eaData,pData);
			}
		}
	}
	ui_GenSetManagedListSafe(pGen, &s_eaData, WaypointData, false);
}

// Deprecated, use waypoint gens
AUTO_EXPR_FUNC(UIGen) ACMD_NAME("GetWaypointList");
void ui_GenExprGetWaypointList(SA_PARAM_NN_VALID UIGen *pGen,bool bUpdateList,bool bGetLandmarks,
							   S32 iSize, S32 iFixedHeight,
							   F32 fDistanceScaleMin, S32 iCount,
							   S32 iIconMinRange, S32 iMaxRange)
{
	static WaypointData** s_eaData[2] = { NULL, NULL };
	static MinimapWaypoint** s_eaWaypoints = NULL;
	S32 i, iScreenWidth, iScreenHeight;
	Vec3 v3Pos;
	CBox ScreenBox = {0, 0, 0, 0};
	Entity *pPlayer = entActivePlayerPtr();
	MissionInfo *pInfo = mission_GetInfoFromPlayer(pPlayer);
	GfxCameraView *pView = gfxGetActiveCameraView();

	gfxGetActiveSurfaceSize(&iScreenWidth, &iScreenHeight);
	ScreenBox.hx = iScreenWidth;
	ScreenBox.hy = iScreenHeight;

	eaClearStruct(&s_eaData[bGetLandmarks], parse_WaypointData);

	if (pInfo && bUpdateList)
	{
		eaClear(&s_eaWaypoints);

		entGetPos(pPlayer, v3Pos);

		for (i = 0; i < eaSize(&pInfo->waypointList); i++)
		{
			MinimapWaypoint *pWaypoint = pInfo->waypointList[i];
			pWaypoint->distSquared = SQR(v3Pos[0] - pWaypoint->pos[0]) + SQR(v3Pos[2] - pWaypoint->pos[2]);
			eaPush(&s_eaWaypoints, pWaypoint);
		}

		eaQSort(s_eaWaypoints, SortWaypointsByDistance);
	}

	for (i = 0; i < eaSize(&s_eaWaypoints); i++)
	{
		MinimapWaypoint *pWaypoint = s_eaWaypoints[i];
		if ((bGetLandmarks && pWaypoint->type == MinimapWaypointType_Landmark)
			||
			(!bGetLandmarks && pWaypoint->type != MinimapWaypointType_Landmark))
		{
			F32 fScaledSize;
			F32 fDistance, fScale;
			bool bOnScreen;
			CBox pBox;
			WaypointData* pData;

			fDistance = sqrt(pWaypoint->distSquared);

			if (fDistance > iMaxRange)
				break;

			if (fDistance >= iIconMinRange && fDistanceScaleMin >= 0.0f)
			{
				F32 fDistanceRatio = (iMaxRange - fDistance) / (F32)(iMaxRange - iIconMinRange);

				fScale = fDistanceScaleMin + (1.0f - fDistanceScaleMin) * fDistanceRatio;
			}
			else
			{
				fScale = 1.0f;
			}

			fScaledSize = iSize*fScale;
			bOnScreen = ProjectCBoxOnScreen(pWaypoint->pos, pView, &pBox, &ScreenBox, fScaledSize, fScaledSize+iFixedHeight);

			pData = StructCreate( parse_WaypointData );
			pData->fX = pBox.left;
			pData->fY = pBox.top;
			pData->fDistance = fDistance;
			pData->fScale = fScale;
			pData->fWorldX = pWaypoint->pos[0];
			pData->fWorldY = pWaypoint->pos[1];
			pData->fWorldZ = pWaypoint->pos[2];
			pData->bLeftEdge = pBox.left < 5;
			pData->bRightEdge = pBox.left + fScaledSize > iScreenWidth - 5;

			pData->pchName = gclWaypointGetName(pWaypoint);
			pData->pchIcon = pWaypoint->pchIconTexName;
			pData->bShowIcon = (!bOnScreen || pData->fDistance > iIconMinRange);

			eaPush(&s_eaData[bGetLandmarks], pData);

			if (iCount > 0 && eaSize(&s_eaData[bGetLandmarks]) >= iCount)
				break;
		}
	}

	ui_GenSetManagedListSafe(pGen, &s_eaData[bGetLandmarks], WaypointData, false);
}


void ui_AddWaypointFromItem(Item* pItem)
{
	int i;
	bool bFound = false;
	if(!pItem || !pItem->pSpecialProps || !pItem->pSpecialProps->pDoorKey || !pItem->pSpecialProps->pDoorKey->pchMap || (!pItem->pSpecialProps->pDoorKey->vPos[0] && !pItem->pSpecialProps->pDoorKey->vPos[1] && !pItem->pSpecialProps->pDoorKey->vPos[2])) {
		return;
	}

	for(i = 0; i < eaSize(&s_eaItemWaypoints) && !bFound; i++) {
		if(stricmp(s_eaItemWaypoints[i]->MapCreatedOn, pItem->pSpecialProps->pDoorKey->pchMap) == 0 && sameVec3(pItem->pSpecialProps->pDoorKey->vPos, s_eaItemWaypoints[i]->pos)) {
			bFound = true;
		}
	}

	if(!bFound) {
		MinimapWaypoint* pNewWaypoint = StructCreate(parse_MinimapWaypoint);
		pNewWaypoint->MapCreatedOn = strdup(pItem->pSpecialProps->pDoorKey->pchMap);
		copyVec3(pItem->pSpecialProps->pDoorKey->vPos, pNewWaypoint->pos);
		pNewWaypoint->type = MinimapWaypointType_SavedWaypoint;
		eaPush(&s_eaItemWaypoints, pNewWaypoint);
	}
}

AUTO_EXPR_FUNC(UIGen) ACMD_NAME(ScanInventoryForWaypoints);
void ui_GenExprScanInventoryForWaypoints(SA_PARAM_OP_VALID Entity *pEnt)
{
	Item** eaItems = NULL;
	int i;
	GameAccountDataExtract *pExtract;

	if(!pEnt)
		return;

	if(!s_eaItemWaypoints) {
		eaCreate(&s_eaItemWaypoints);
		s_uiEntID = entGetContainerID(pEnt);
	} else if(entGetContainerID(pEnt) != s_uiEntID) {
		eaClearStruct(&s_eaItemWaypoints, parse_MinimapWaypoint);
		s_uiEntID = entGetContainerID(pEnt);
	}

	pExtract = entity_GetCachedGameAccountDataExtract(pEnt);

	inv_ent_GetSimpleItemList(pEnt, InvBagIDs_LocationData, &eaItems, false, pExtract);
	for(i = 0; i < eaSize(&eaItems); i++) {
		if(eaItems[i]->pSpecialProps && eaItems[i]->pSpecialProps->pDoorKey && eaItems[i]->pSpecialProps->pDoorKey->pchMap && (eaItems[i]->pSpecialProps->pDoorKey->vPos[0] || eaItems[i]->pSpecialProps->pDoorKey->vPos[1] || eaItems[i]->pSpecialProps->pDoorKey->vPos[2])) {
			ui_AddWaypointFromItem(eaItems[i]);
		}
	}

	inv_ent_GetSimpleItemList(pEnt, InvBagIDs_HiddenLocationData, &eaItems, false, pExtract);
	for(i = 0; i < eaSize(&eaItems); i++) {
		if(eaItems[i]->pSpecialProps && eaItems[i]->pSpecialProps->pDoorKey && eaItems[i]->pSpecialProps->pDoorKey->pchMap && (eaItems[i]->pSpecialProps->pDoorKey->vPos[0] || eaItems[i]->pSpecialProps->pDoorKey->vPos[1] || eaItems[i]->pSpecialProps->pDoorKey->vPos[2])) {
			ui_AddWaypointFromItem(eaItems[i]);
		}
	}
}

AUTO_COMMAND ACMD_CLIENTCMD ACMD_PRIVATE;
void ui_ScanInventoryForWaypoints(SA_PARAM_OP_VALID Entity *pEnt, bool bClearList)
{
	if(bClearList && s_eaItemWaypoints) {
		eaClearStruct(&s_eaItemWaypoints, parse_MinimapWaypoint);
	}

	ui_GenExprScanInventoryForWaypoints(pEnt);
}

AUTO_EXPR_FUNC(UIGen) ACMD_NAME("GetWaypointName");
const char *ui_GenExprGetWaypointName(MinimapWaypoint *pWaypoint)
{
	const char *pchDescription = NULL;

	if (pWaypoint->type == MinimapWaypointType_SavedWaypoint)
	{
		pchDescription = TranslateMessageKey("Map_SavedWaypoint");
	}
	else
	{
		pchDescription = gclWaypointGetName(pWaypoint);
	}

	return NULL_TO_EMPTY(pchDescription);
}

AUTO_EXPR_FUNC(UIGen) ACMD_NAME("WaypointGetDistanceFromPlayer");
F32 ui_GenExprWaypointGetDistanceFromPlayer(SA_PARAM_OP_VALID MinimapWaypoint *pWaypoint)
{
	if (pWaypoint)
	{
		return sqrt(pWaypoint->distSquared);
	}
	return 0.0f;
}

#include "AutoGen/WaypointUI_c_ast.c"