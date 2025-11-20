/***************************************************************************
*     Copyright (c) 2006, Cryptic Studios
*     All Rights Reserved
*     Confidential Property of Cryptic Studios
***************************************************************************/

#include "entCritter.h"
#include "Entity.h"
#include "Expression.h"
#include "gslNamedPoint.h"
#include "gslOldEncounter.h"
#include "mathutil.h"
#include "oldencounter_common.h"
#include "StringCache.h"
#include "wlEncounter.h"
#include "WorldGrid.h"


// ----------------------------------------------------------------------------------
// Static Data
// ----------------------------------------------------------------------------------

static GameNamedPoint **s_eaNamedPoints = NULL;


// ----------------------------------------------------------------------------------
// Sole interfaces to searching s_eaNamedPoints.  No other function
// than namedpoint_GetByName should be searching s_eaNamedPoints.
// ----------------------------------------------------------------------------------
GameNamedPoint *namedpoint_GetByPosition(const Vec3 vPos)
{
	int i;

	for(i=eaSize(&s_eaNamedPoints)-1; i>=0; --i) {
		if (s_eaNamedPoints[i]->pWorldPoint &&
			sameVec3(s_eaNamedPoints[i]->pWorldPoint->point_pos,vPos)) {
			return s_eaNamedPoints[i];
		}
	}

	return NULL;
}

GameNamedPoint *namedpoint_GetByEntry(WorldNamedPoint *pNamedPoint)
{
	int i;

	for(i=eaSize(&s_eaNamedPoints)-1; i>=0; --i) {
		if (SAFE_MEMBER(s_eaNamedPoints[i], pWorldPoint) == pNamedPoint) {
			return s_eaNamedPoints[i];
		}
	}

	return NULL;
}

GameNamedPoint *namedpoint_GetByName(const char *pcNamedPointName, const WorldScope *pScope)
{
	if (pScope && gUseScopedExpr) {
		WorldEncounterObject *pObject = worldScopeGetObject(pScope, pcNamedPointName);

		if (pObject && pObject->type == WL_ENC_NAMED_POINT) {
			WorldNamedPoint *pNamedPoint = (WorldNamedPoint *)pObject;
			GameNamedPoint *pGameNamedPoint = namedpoint_GetByEntry(pNamedPoint);
			if (pGameNamedPoint) {
				return pGameNamedPoint;
			}
		}
	} else {
		int i;
		for(i=eaSize(&s_eaNamedPoints)-1; i>=0; --i) {
			if (stricmp(pcNamedPointName, s_eaNamedPoints[i]->pcName) == 0) {
				return s_eaNamedPoints[i];
			}
		}
	}

	return NULL;
}


#define FOR_EACH_NAMED_POINT(it) { int i##it##Index; for(i##it##Index=eaSize(&s_eaNamedPoints)-1; i##it##Index>=0; --i##it##Index) { GameNamedPoint *it = s_eaNamedPoints[i##it##Index];
#define FOR_EACH_NAMED_POINT2(outerIt, it) { int i##it##Index; for(i##it##Index=i##outerIt##Index-1; i##it##Index>=0; --i##it##Index) { GameNamedPoint *it = s_eaNamedPoints[i##it##Index];
// ----------------------------------------------------------------------------------
// End of sole interfaces to searching s_eaNamedPoints.
// ----------------------------------------------------------------------------------

// ----------------------------------------------------------------------------------
// Expression System Interaction
// ----------------------------------------------------------------------------------

int namedpoint_ExprLocationResolveNamedPoint(ExprContext *pContext, const char *pcName, Mat4 matOut, const char *pcBlamefile)
{
	Entity *pEnt = exprContextGetSelfPtr(pContext);
	GameNamedPoint *pPoint;
	WorldScope *pScope = exprContextGetScope(pContext);

	pPoint = namedpoint_GetByName(pcName, pScope);
	if (pPoint) {
		Vec3 vPos;
		Quat qRot;
		namedpoint_GetPosition(pPoint, vPos, qRot);
		quatVecToMat4(qRot, vPos, matOut);
		return true;
	}

	if (gConf.bAllowOldEncounterData && pEnt && pEnt->pCritter) {
		if (pEnt->pCritter->encounterData.parentEncounter) {
			OldStaticEncounter *pEnc = GET_REF(pEnt->pCritter->encounterData.parentEncounter->staticEnc);
			if (pEnc) {
				EncounterDef *pSpawnRule = pEnc->spawnRule;
				int i;
				
				for(i=eaSize(&pSpawnRule->namedPoints)-1; i>=0; --i) {
					if (pSpawnRule->namedPoints[i]->pointName && (stricmp(pSpawnRule->namedPoints[i]->pointName, pcName) == 0)) {
						OldNamedPointInEncounter *pEncPoint = pSpawnRule->namedPoints[i];
						devassert(pEncPoint->hasAbsLoc);
						copyMat4(pEncPoint->absLocation, matOut);
						return true;
					}
				}
			}
		}
	}

	return false;
}


// ----------------------------------------------------------------------------------
// Named Point List Logic
// ----------------------------------------------------------------------------------


static void namedpoint_Free(GameNamedPoint *pGamePoint)
{
	free(pGamePoint);
}


static void namedpoint_AddPoint(const char *pcName, WorldNamedPoint *pWorldPoint)
{
	GameNamedPoint *pGamePoint = calloc(1,sizeof(GameNamedPoint));
	if (pcName) {
		pGamePoint->pcName = allocAddString(pcName);
	}
	pGamePoint->pWorldPoint = pWorldPoint;
	eaPush(&s_eaNamedPoints, pGamePoint);
}


static void namedpoint_ClearList(void)
{
	eaDestroyEx(&s_eaNamedPoints, namedpoint_Free);
}


// Check if a named point exists
bool namedpoint_NamedPointExists(const char *pcName, const WorldScope *pScope)
{
	return namedpoint_GetByName(pcName, pScope) != NULL;
}


// Get position.  Returns true if it exists
bool namedpoint_GetPosition(GameNamedPoint *pPoint, Vec3 vPosition, Quat qRot)
{
	if (pPoint) {
		if (vPosition) {
			copyVec3(pPoint->pWorldPoint->point_pos, vPosition);
		}
		if (qRot) {
			copyVec4(pPoint->pWorldPoint->point_rot, qRot);
		}
		return true;
	}

	return false;
}


// Get position.  Returns true if it exists
bool namedpoint_GetPositionByName(const char *pcName, Vec3 vPosition, Quat qRot)
{
	GameNamedPoint *pPoint = namedpoint_GetByName(pcName, NULL);
	return namedpoint_GetPosition(pPoint, vPosition, qRot);
}



// ----------------------------------------------------------------------------------
// Map Load Logic
// ----------------------------------------------------------------------------------


void namedpoint_MapValidate(ZoneMap *pZoneMap)
{
	// Nothing to do
}


void namedpoint_MapLoad(ZoneMap *pZoneMap)
{
	WorldZoneMapScope *pScope;
	int i;

	// Clear all data
	namedpoint_ClearList();

	// Get zone map scope
	pScope = zmapGetScope(pZoneMap);

	// Find all named points in all scopes
	if(pScope) {
		for(i=eaSize(&pScope->named_points)-1; i>=0; --i) {
			const char *pcName = worldScopeGetObjectName(&pScope->scope, &pScope->named_points[i]->common_data);
			namedpoint_AddPoint(pcName, pScope->named_points[i]);
		}
	}
}


void namedpoint_MapUnload(void)
{
	namedpoint_ClearList();
}


AUTO_RUN;
void namedpoint_Init(void)
{
	exprRegisterLocationPrefix("namedpoint", namedpoint_ExprLocationResolveNamedPoint, false);
}


