/***************************************************************************
*     Copyright (c) 2012, Cryptic Studios
*     All Rights Reserved
*     Confidential Property of Cryptic Studios
***************************************************************************/

#include "textparser.h"
#include "mathutil.h"
#include "error.h"
#include "gclDebugDrawPrimitives.h"
#include "Entity.h"
#include "Player.h"
#include "gclEntity.h"
#include "EntityLib.h"
#include "dynFx.h"
#include "dynFxInterface.h"
#include "WorldGrid.h"
#include "beaconAStar.h"
#include "mission_common.h"
#include "StringCache.h"
#include "gclGoldenPath.h"
#include "gclWorldDebug.h"
#include "rand.h"
#include "mapstate_common.h"
#include "gclMapState.h"
#include "LineDist.h"
#include "StaticWorld/ZoneMapLayer.h"
#include "Octree.h"

#include "AutoGen/gclGoldenPath_h_ast.h"

#define COST_FACTOR 1000
#define NODE_DISTANCE_THRESHOLD .007
#define FORCE_UPDATE_TICK_COUNT 30
#define VERTICAL_BIAS 20

//This may need some tweaking to get right
//We may want to put it in the config file
#define PATH_FX_INCREMENT 10

AUTO_RUN_ANON(memBudgetAddMapping(__FILE__, BUDGET_GameSystems););

static dtFx *s_ea32FX = NULL;
static GoldenPathNode **s_eaGoldenPathNodes = NULL;
static GoldenPathNode **s_eaGPNodesOnPath = NULL;
static S32 s_bGPDebug = false;
static Vec3 s_v3TargetPos = {0.0f, 0.0f, 0.0f};
static S32 s_bShowGoldenpath = false;
static const char *s_pchMissionToPathTo = NULL;

GoldenPathStatus *goldenPath_GetStatus()
{
	static GoldenPathStatus *pStatus = NULL;

	if (!pStatus)
		pStatus = StructCreate(parse_GoldenPathStatus);

	if (!s_bShowGoldenpath)
	{
		pStatus->pTargetWaypoint = NULL;
		pStatus->pFirstNodeInArea = NULL;
	}

	return pStatus;
}

void goldenPath_FillPathNodeList( GoldenPathNode ***peaNodes, bool bRemoveTempNodes )
{
	eaClear(peaNodes);

	eaCopy(peaNodes, &s_eaGPNodesOnPath);

	if (bRemoveTempNodes)
	{
		S32 i;

		for (i = 0; i < eaSize(peaNodes); ++i)
		{
			if ((*peaNodes)[i]->bIsTemporary)
			{
				eaRemove(peaNodes, i--);
			}
		}
	}
}


//Returns the mission that has the most recent start time
// Returns a pooled string
const char *entGetMostRecentMission(Entity *pEnt)
{
	const char *pchMostRecentMission = NULL;

	if(pEnt)
	{
		MissionInfo *pMInfo = mission_GetInfoFromPlayer(pEnt);
		if(pMInfo)
		{
			U32 uLatestStartTime = 0;
			int i;

			for(i = 0; i < eaSize(&pMInfo->missions); ++i)
			{
				Mission *pMission = pMInfo->missions[i];
				MissionDef *pDef = mission_GetDef(pMission);
				if(pMission && pMission->startTime > uLatestStartTime && pDef && pDef->missionType != MissionType_Perk && pDef->missionType != MissionType_OpenMission)
				{
					pchMostRecentMission = pMission->missionNameOrig;
					uLatestStartTime = pMission->startTime;
				}
			}
		}
	}

	return pchMostRecentMission;
}

static WorldRegion *entGetRegion(Entity *pEntity)
{
	if (pEntity)
	{
		Vec3 v3EntPos;

		entGetPos(pEntity, v3EntPos);

		return worldGetWorldRegionByPos(v3EntPos);
	}

	return NULL;
}

static WorldPathNode **getWorldPathNodes()
{
	WorldRegion *pRegion = entGetRegion(entActivePlayerPtr());

	if (pRegion)
	{
		return worldRegionGetPathNodes(pRegion);
	}

	return NULL;
}

static void resetFXConnections( GoldenPathStatus * pStatus ) 
{
	int i;

	for (i = 0; i < eaSize(&pStatus->eaPathFX); ++i)
	{
		GoldenPathFXConnection *pConnection = pStatus->eaPathFX[i];

		if (pConnection)
		{
			pConnection->bIsSame = false;
		}
	}
}

//Instead of having one FX for each path section, we draw multiple FX at known intervals.
//This is to counter the fact that the Golden Path FX did not handle long connections well
static void addFXConnection(GoldenPathFXConnection *pConnection, Vec3 v3Source, Vec3 v3Target, const char *pchFX)
{
	GoldenPathStatus *pStatus = goldenPath_GetStatus();
	DynFxManager *pManager;
	Vec3 v3EntPos;
	F32 fDist = distance3(v3Source, v3Target);

	PERFINFO_AUTO_START_FUNC();

	copyVec3(v3Source, pConnection->v3Source);
	pConnection->v3Source[1] -= pStatus->fVerticalPathOffset;
	copyVec3(v3Target, pConnection->v3Target);
	pConnection->v3Target[1] -= pStatus->fVerticalPathOffset;

	if (fDist > 0)
	{
		F32 fTotal = MAX(1, (int)floor(fDist) / PATH_FX_INCREMENT);
		Vec3 v3Step, v3CurrentSource, v3CurrentTarget;
		int i;
		bool bNeedsRetry = false;
		Quat q;

		zeroQuat(q);

		entGetPos(entActivePlayerPtr(), v3EntPos);
		pManager = dynFxGetGlobalFxManager(v3EntPos);

		if(!pConnection->bIsArea)
		{
			subVec3(v3Target, v3Source, v3Step);
			normalVec3(v3Step);
			scaleVec3(v3Step, fDist / fTotal, v3Step);

			copyVec3(v3Source, v3CurrentSource);

			for (i = 0; i < fTotal; ++i)
			{
				addVec3(v3CurrentSource, v3Step, v3CurrentTarget);
				ea32Push(&pConnection->eaFX, dtAddFxAtLocation(pManager->guid, pchFX, dynParamBlockCreate(), v3CurrentSource, v3CurrentTarget, q, 0, 0, &bNeedsRetry, eDynFxSource_UI));
				addVec3(v3CurrentSource, v3Step, v3CurrentSource);
			}
		}
		else
		{
			ea32Push(&pConnection->eaFX, dtAddFxAtLocation(pManager->guid, pchFX, dynParamBlockCreate(), v3Source, v3Target, q, 0, 0, &bNeedsRetry, eDynFxSource_UI));
		}
	}
}

//A companion to the above function
static void killFXConnection(GoldenPathFXConnection *pConnection)
{
	int i;

	for (i = 0; i < ea32Size(&pConnection->eaFX); ++i)
	{
		dtFxKill(pConnection->eaFX[i]);
	}

	ea32Clear(&pConnection->eaFX);
}

S32 goldenPath_FindFX(GoldenPathFXConnection ***peaFX, GoldenPathNode *pNode)
{
	S32 i;

	for (i = 0; i < eaSize(peaFX); ++i)
	{
		GoldenPathFXConnection *pFX = (*peaFX)[i];

		if (pFX)
		{
			F32 fDist = distance3(pNode->v3Pos, pFX->v3Source);
			if (fDist < .1)
			{
				return i;
			}
		}
	}

	return -1;
}

S32 goldenPath_FindFXVec(GoldenPathFXConnection ***peaFX, Vec3 v3Source)
{
	S32 i;

	for (i = 0; i < eaSize(peaFX); ++i)
	{
		GoldenPathFXConnection *pFX = (*peaFX)[i];

		if (pFX)
		{
			F32 fDist = distance3Squared(v3Source, pFX->v3Source);
			if (fDist < .1)
			{
				return i;
			}
		}
	}

	return -1;
}

//This is the new golden path drawing code
//I will keep the old code in for a while and if there are no major issues I will take it out and remove this comment ~DHOGBERG 8/6/2012
void goldenPath_NewDrawPath()
{
	GoldenPathStatus *pStatus = goldenPath_GetStatus();

	PERFINFO_AUTO_START_FUNC();

	if (pStatus->bFXHidden)
	{
		return;
	}

	resetFXConnections(pStatus);

	if(s_eaGPNodesOnPath && eaSize(&s_eaGPNodesOnPath) > 0)
	{
		WorldCollCollideResults wcResults = {0};
		Vec3 vSource, vTarget;
		int i;
		int iStart = MAX(eaSize(&s_eaGPNodesOnPath) - 30, 0); //TODO: evaluate if this is a good number and possibly move it to either a DEFINE or into the config file ~DHOGBERG 8/9/2012


		for (i = iStart; i < eaSize(&s_eaGPNodesOnPath) - 1; ++i)
		{
			GoldenPathNode *pNode = s_eaGPNodesOnPath[i];
			GoldenPathNode *pPrevNode = s_eaGPNodesOnPath[i + 1];

			if(pNode && pPrevNode)
			{
				S32 iFXindex = goldenPath_FindFX(&pStatus->eaPathFX, pPrevNode);
				bool bIsTeleport = false;

				copyVec3(pNode->v3Pos, vTarget);
				copyVec3(pPrevNode->v3Pos, vSource);
				vTarget[1] += pStatus->fVerticalPathOffset;
				vSource[1] += pStatus->fVerticalPathOffset;

				if (pNode->iTeleportID != 0 && pNode->iTeleportID == pPrevNode->iTeleportID)
				{
					bIsTeleport = true;
				}

				//Check to see if the FX already exists
				if (iFXindex >= 0 && iFXindex < eaSize(&pStatus->eaPathFX))
				{
					GoldenPathFXConnection *pConnection = pStatus->eaPathFX[iFXindex];

					if(pConnection)
					{
						bool bCheckObstruction = false;

						pConnection->bIsSame = true;

						//Update the direction if needed
						if (!pConnection->bIsObstructed && distance3Squared(pConnection->v3Target,pNode->v3Pos) > .1)
						{
							killFXConnection(pConnection);
							addFXConnection(pConnection, vSource, vTarget, pStatus->pchGoldenPathFX);
							pConnection->bIsObstructed = false;
						}

						if (pNode->bIsTemporary && i > 0 && s_eaGPNodesOnPath[i - 1] && s_eaGPNodesOnPath[i - 1]->bCanBeObstructed)
						{
							bCheckObstruction = true;
						}

						if (pPrevNode->bIsTemporary && i < eaSize(&s_eaGPNodesOnPath) - 2 &&
							s_eaGPNodesOnPath[i + 2] && s_eaGPNodesOnPath[i + 2]->bCanBeObstructed)
						{
							bCheckObstruction = true;
						}

						//Obstructed nodes need to be updated
						if (pNode->bCanBeObstructed || pPrevNode->bCanBeObstructed || bCheckObstruction)
						{
							Entity *pPlayerEnt = entActivePlayerPtr();

							if(pPlayerEnt)
							{
								wcRayCollide(worldGetActiveColl(entGetPartitionIdx(pPlayerEnt)), pPrevNode->v3Pos, pNode->v3Pos, WC_FILTER_BIT_MOVEMENT, &wcResults);

								if (!wcResults.hitSomething)
								{
									wcRayCollide(worldGetActiveColl(entGetPartitionIdx(pPlayerEnt)), pNode->v3Pos, pPrevNode->v3Pos, WC_FILTER_BIT_MOVEMENT, &wcResults);
								}
							}

							if (wcResults.hitSomething)
							{
								if (distance3Squared(wcResults.posWorldImpact, pConnection->v3ObstructedTarget) > NODE_DISTANCE_THRESHOLD || !pConnection->bIsObstructed && !bIsTeleport)
								{
									copyVec3(wcResults.posWorldImpact, vTarget);
									copyVec3(vTarget, pConnection->v3ObstructedTarget);
									vTarget[1] += pStatus->fVerticalPathOffset;
									killFXConnection(pConnection);
									addFXConnection(pConnection, vSource, vTarget, pStatus->pchGoldenPathObstructedFX);
									pConnection->bIsObstructed = true;
								}
							}
							else
							{
								if (pConnection->bIsObstructed && !bIsTeleport)
								{
									killFXConnection(pConnection);
									addFXConnection(pConnection, vSource, vTarget, pStatus->pchGoldenPathFX);
									pConnection->bIsObstructed = false;
								}
							}
						}
					}
				}
				else //We need to add a new FX
				{
					GoldenPathFXConnection *pConnection = StructCreate(parse_GoldenPathFXConnection);

					pConnection->bIsSame = true;
					pConnection->bIsTeleport = bIsTeleport;
					pConnection->bIsArea = false;

					if (!bIsTeleport)
					{
						if (pNode->bCanBeObstructed || pPrevNode->bCanBeObstructed)
						{
							Entity *pPlayerEnt = entActivePlayerPtr();

							if(pPlayerEnt)
							{
								wcRayCollide(worldGetActiveColl(entGetPartitionIdx(pPlayerEnt)), pPrevNode->v3Pos, pNode->v3Pos, WC_FILTER_BIT_MOVEMENT, &wcResults);

								if (!wcResults.hitSomething)
								{
									wcRayCollide(worldGetActiveColl(entGetPartitionIdx(pPlayerEnt)), pNode->v3Pos, pPrevNode->v3Pos, WC_FILTER_BIT_MOVEMENT, &wcResults);
								}
							}

							if (wcResults.hitSomething)
							{
								copyVec3(wcResults.posWorldImpact, vTarget);
								vTarget[1] -= pStatus->fVerticalPathOffset;
								addFXConnection(pConnection, vSource, vTarget, pStatus->pchGoldenPathObstructedFX);
								pConnection->bIsObstructed = true;
							}
							else
							{
								addFXConnection(pConnection, vSource, vTarget, pStatus->pchGoldenPathFX);
								pConnection->bIsObstructed = false;
							}
						}
						else
						{
							addFXConnection(pConnection, vSource, vTarget, pStatus->pchGoldenPathFX);
							pConnection->bIsObstructed = false;
						}
					}
					eaPush(&pStatus->eaPathFX, pConnection);
				}
			}
		}

		//Here we draw area waypoints
		//Instead of the normal line FX, we draw a fan out FX from the last point outside of the area to the next point on the path
		if (pStatus->pchGoldenPathAreaFX && pStatus->pchGoldenPathAreaFX[0] && 
			eaSize(&s_eaGPNodesOnPath) > 0 && s_eaGPNodesOnPath[0] && 
			pStatus->pTargetWaypoint && pStatus->pFirstNodeInArea &&
			pStatus->pTargetWaypoint->fXAxisRadius > 0 && pStatus->pTargetWaypoint->fYAxisRadius > 0)
		{
			Quat q;
			Entity *pPlayerEnt = entActivePlayerPtr();
			copyVec3(pStatus->pFirstNodeInArea->v3Pos, vTarget);
			copyVec3(s_eaGPNodesOnPath[0]->v3Pos, vSource);
			zeroQuat(q);

			vSource[1] += pStatus->fVerticalPathOffset;
			vTarget[1] += pStatus->fVerticalPathOffset;

			{
				int iFXindex = goldenPath_FindFX(&pStatus->eaPathFX, s_eaGPNodesOnPath[0]);

				if (iFXindex >= 0 && iFXindex < eaSize(&pStatus->eaPathFX))
				{
					GoldenPathFXConnection *pConnection = pStatus->eaPathFX[iFXindex];
					if(distance3Squared(pConnection->v3Target, pStatus->pFirstNodeInArea->v3Pos) < .1)
					{
						pConnection->bIsSame = 1;
					}
					else
					{
						pConnection->bIsSame = 0;
					}
				}
				else
				{
					GoldenPathFXConnection *pConnection = StructCreate(parse_GoldenPathFXConnection);

					pConnection->bIsSame = 1;
					pConnection->bIsTeleport = false;
					pConnection->bIsArea = true;

					addFXConnection(pConnection, vSource, vTarget, pStatus->pchGoldenPathAreaFX);
					pConnection->bIsObstructed = false;

					eaPush(&pStatus->eaPathFX, pConnection);
				}
			}
		}
	}

	//Now we kill all of the leftover fx
		{
			int i = 0;
			while (i < eaSize(&pStatus->eaPathFX))
			{
				GoldenPathFXConnection *pConnection = pStatus->eaPathFX[i];

				if (pConnection)
				{
					if (!pConnection->bIsSame)
					{
						//dtFxKill(pConnection->fx);
						killFXConnection(pConnection);
						eaRemove(&pStatus->eaPathFX, goldenPath_FindFXVec(&pStatus->eaPathFX, pConnection->v3Source));
					}
					else
					{
						++i;
					}
				}
			}
		}

	PERFINFO_AUTO_STOP();
}

//This function checks whether the player has crossed the threshold over the area waypoint FX.
// When the player crosses, a new FX is played.
static void goldenPath_DrawAreaDeath()
{
	GoldenPathStatus *pStatus = goldenPath_GetStatus();
	DynFxManager *pManager;
	static S32 s_iLastSize = -1;
	S32 iThisSize = -1;
	Vec3 v3EntPos;
	static Vec3 s_v3LastPos = {0,0,0};
	Quat q;

	if (pStatus->pTargetWaypoint && pStatus->bFoundPath &&
		pStatus->pFirstNodeInArea &&
		pStatus->pTargetWaypoint->fXAxisRadius > 0 && pStatus->pTargetWaypoint->fYAxisRadius > 0 &&
		pStatus->pchGoldenPathAreaDeathFX && pStatus->pchGoldenPathAreaDeathFX[0])
	{
		bool bNeedsRetry = false;
		F32 t = -1.0f;
		static F32 s_t = -1.0f;
		Vec3 v3Closest;

		entGetPos(entActivePlayerPtr(), v3EntPos);
		pManager = dynFxGetGlobalFxManager(v3EntPos);

		zeroQuat(q);

		iThisSize = eaSize(&s_eaGPNodesOnPath);

		PointLineSegClosestPoint(v3EntPos, s_v3LastPos, pStatus->pFirstNodeInArea->v3Pos, &t, v3Closest);

		if (s_iLastSize == 1 && t > 0 && s_t <= 0)
		{
			static dtFx fx = 0;
			Vec3 v3Source, v3Target;
			copyVec3(s_v3LastPos, v3Source);
			v3Source[1] += pStatus->fVerticalPathOffset;
			copyVec3(pStatus->pFirstNodeInArea->v3Pos, v3Target);
			v3Target[1] += pStatus->fVerticalPathOffset;

			if (fx != 0)
			{
				dtFxKill(fx);
			}
			fx = dtAddFxAtLocation(pManager->guid, pStatus->pchGoldenPathAreaDeathFX, dynParamBlockCreate(), v3Source, v3Target, q, 0, 0, &bNeedsRetry, eDynFxSource_UI);
		}

		if (!bNeedsRetry)
		{
			s_iLastSize = iThisSize;

			s_t = t;

			if (iThisSize > 0)
			{
				copyVec3(s_eaGPNodesOnPath[0]->v3Pos, s_v3LastPos);
			}
		}
	}
}


//DEPRECATED
void goldenPath_DrawPath()
{
	GoldenPathStatus *pStatus = goldenPath_GetStatus();
	int i;
	Vec3 v3EntPos;
	Quat q;
	DynFxManager *pManager;

	PERFINFO_AUTO_START_FUNC();

	if (!pStatus)
	{
		PERFINFO_AUTO_STOP();
		return;
	}
	
	entGetPos(entActivePlayerPtr(), v3EntPos);
	pManager = dynFxGetGlobalFxManager(v3EntPos);

	zeroQuat(q);
	if(!s_ea32FX)
	{
		ea32Create(&s_ea32FX);
	}
	else
	{
		if(pStatus->iFXStartPoint < 0)
		{
			for (i = 0; i < ea32Size(&s_ea32FX); ++i)
			{
				if(s_ea32FX[i] != 0)
					dtFxKill(s_ea32FX[i]);
			}
			ea32Clear(&s_ea32FX);
		}
		else
		{
			for (i = pStatus->iFXStartPoint; i < ea32Size(&s_ea32FX); ++i)
			{
				if(s_ea32FX[i] != 0)
					dtFxKill(s_ea32FX[i]);
			}
			if(pStatus->iFXStartPoint - 1 < ea32Size(&s_ea32FX))
				ea32RemoveRange(&s_ea32FX, pStatus->iFXStartPoint, ea32Size(&s_ea32FX) - pStatus->iFXStartPoint);
		}
	}

	if(s_eaGPNodesOnPath && eaSize(&s_eaGPNodesOnPath) > 0)
	{
		Vec3 v1, v2;
		bool bNeedsRetry = false;
		WorldCollCollideResults wcResults = {0};

		if(pStatus->iFXStartPoint < 0)
			pStatus->iFXStartPoint = 0;

		//for (i = pStatus->iFXStartPoint; i < eaSize(&s_eaGPNodesOnPath) - 1; ++i)
		for (i = 0; i < eaSize(&s_eaGPNodesOnPath) - 1; ++i)
		{
			GoldenPathNode *pNode = s_eaGPNodesOnPath[i];
			GoldenPathNode *pPrevNode = s_eaGPNodesOnPath[i + 1];

			if(pNode && pPrevNode)
			{
				bool bIsTeleport = false;
				copyVec3(pNode->v3Pos, v1);
				copyVec3(pPrevNode->v3Pos, v2);
				v1[1] += pStatus->fVerticalPathOffset;
				v2[1] += pStatus->fVerticalPathOffset;

				if (pNode->iTeleportID != 0 && pNode->iTeleportID == pPrevNode->iTeleportID)
				{
					bIsTeleport = true;
				}

				if (pNode->bCanBeObstructed || pPrevNode->bCanBeObstructed)
				{
					Entity *pPlayerEnt = entActivePlayerPtr();

					if(pPlayerEnt)
					{
						wcRayCollide(worldGetActiveColl(entGetPartitionIdx(pPlayerEnt)), pPrevNode->v3Pos, pNode->v3Pos, WC_FILTER_BIT_MOVEMENT, &wcResults);

						if (!wcResults.hitSomething)
						{
							wcRayCollide(worldGetActiveColl(entGetPartitionIdx(pPlayerEnt)), pNode->v3Pos, pPrevNode->v3Pos, WC_FILTER_BIT_MOVEMENT, &wcResults);
						}
					}
					
					if (wcResults.hitSomething)
					{
						copyVec3(wcResults.posWorldImpact, v1);
						if (bIsTeleport)
						{
							ea32Push(&s_ea32FX, 0);
						}
						else
						{
							dtFxKill(s_ea32FX[i]);
							ea32Push(&s_ea32FX, dtAddFxAtLocation(pManager->guid, pStatus->pchGoldenPathObstructedFX, dynParamBlockCreate(), v2, v1, q, 0, 0, &bNeedsRetry, eDynFxSource_UI));
						}
					}
					else if (i > pStatus->iFXStartPoint)
					{
						if (bIsTeleport)
						{
							ea32Push(&s_ea32FX, 0);
						}
						else
						{
							dtFxKill(s_ea32FX[i]);
							ea32Push(&s_ea32FX, dtAddFxAtLocation(pManager->guid, pStatus->pchGoldenPathFX, dynParamBlockCreate(), v2, v1, q, 0, 0, &bNeedsRetry, eDynFxSource_UI));
						}
					}
				}
				else if (i > pStatus->iFXStartPoint)
				{
					if (bIsTeleport)
					{
						ea32Push(&s_ea32FX, 0);
					}
					else
					{
						ea32Push(&s_ea32FX, dtAddFxAtLocation(pManager->guid, pStatus->pchGoldenPathFX, dynParamBlockCreate(), v2, v1, q, 0, 0, &bNeedsRetry, eDynFxSource_UI));
					}
				}
			}
		}

		//Here we draw area waypoints
		//Instead of the normal line FX, we draw a fan out FX from the last point outside of the area to the next point on the path
		if (pStatus->pchGoldenPathAreaFX && pStatus->pchGoldenPathAreaFX[0] && 
			eaSize(&s_eaGPNodesOnPath) > 0 && s_eaGPNodesOnPath[0] && 
			pStatus->pTargetWaypoint && pStatus->pFirstNodeInArea &&
			pStatus->pTargetWaypoint->fXAxisRadius > 0 && pStatus->pTargetWaypoint->fYAxisRadius > 0)
		{
			Entity *pPlayerEnt = entActivePlayerPtr();
			copyVec3(pStatus->pFirstNodeInArea->v3Pos, v1);
			copyVec3(s_eaGPNodesOnPath[0]->v3Pos, v2);
			quatLookAt(v2, v1, q);
			zeroQuat(q);

			v1[1] += pStatus->fVerticalPathOffset;
			v2[1] += pStatus->fVerticalPathOffset;
			ea32Push(&s_ea32FX, dtAddFxAtLocation(pManager->guid, pStatus->pchGoldenPathAreaFX, dynParamBlockCreate(), v2, v1, q, 0, 0, &bNeedsRetry, eDynFxSource_UI));
		}
	}

	PERFINFO_AUTO_STOP();
}

AUTO_COMMAND ACMD_HIDE ACMD_ACCESSLEVEL(0);
void goldenPath_HideFx()
{
	GoldenPathStatus *pStatus = goldenPath_GetStatus();
	int i;

	for (i = 0; i < eaSize(&pStatus->eaPathFX); ++i)
	{
		if(!pStatus->eaPathFX[i]->bIsTeleport)
			killFXConnection(pStatus->eaPathFX[i]);
		//dtFxKill(pStatus->eaPathFX[i]->fx);
	}
	eaClearStruct(&pStatus->eaPathFX, parse_GoldenPathFXConnection);

	pStatus->bFXHidden = true;
}

AUTO_COMMAND ACMD_HIDE ACMD_ACCESSLEVEL(0);
void goldenPath_UnHideFx()
{
	GoldenPathStatus *pStatus = goldenPath_GetStatus();
	
	pStatus->bFXHidden = false;
}

static F32 distance3SquaredVerticalBias(Vec3 a, Vec3 b)
{
	return distance3XZSquared(a, b) + VERTICAL_BIAS * ((a[1] - b[1]) * (a[1] - b[1]));
}

GoldenPathNode *goldenPath_FindClosestNode(Vec3 v3Target, GoldenPathNode ***peaNodes)
{
	//TODO make faster -dhogberg
	int i;
	F32 fClosestDist = FLT_MAX;
	GoldenPathNode *pClosestNode = NULL;

	if (peaNodes)
	{
		Vec3 v3Node;
		F32 fDist = 0;

		for (i = 0; i < eaSize(peaNodes); ++i)
		{
			GoldenPathNode *pNode = (*peaNodes)[i];

			if(pNode && !pNode->bIsTemporary)
			{
				copyVec3(pNode->v3Pos, v3Node);
				fDist = distance3SquaredVerticalBias(v3Target, v3Node);

				if (fDist < fClosestDist)
				{
					pClosestNode = pNode;
					fClosestDist = fDist;
				}
			}
		}
	}
	return pClosestNode;
}

//This value is used in calculations where all the distances are squared
static F32 s_fStickinessFactor = 625;
AUTO_CMD_FLOAT(s_fStickinessFactor, goldenPathStickinessFactor);

static GoldenPathNode *goldenPath_FindClosestNodeToPlayer()
{
	Entity *pPlayerEnt = entActivePlayerPtr();

	if (pPlayerEnt)
	{
		Vec3 v3EntPos;
		GoldenPathNode *pClosestNode = NULL;
		GoldenPathNode *pClosestNodeOnPath = NULL;
		F32 fDistToClosest = FLT_MAX;
		F32 fDistToPathClosest = FLT_MAX;

		entGetPos(pPlayerEnt, v3EntPos);

		pClosestNode = goldenPath_FindClosestNode(v3EntPos, &s_eaGoldenPathNodes);

		if (pClosestNode)
		{
			pClosestNodeOnPath = goldenPath_FindClosestNode(v3EntPos, &s_eaGPNodesOnPath);

			if (pClosestNodeOnPath)
			{
				if (pClosestNodeOnPath == pClosestNode)
				{
					return pClosestNode;
				}

				fDistToClosest = distance3SquaredVerticalBias(v3EntPos, pClosestNode->v3Pos);
				fDistToPathClosest = distance3SquaredVerticalBias(v3EntPos, pClosestNodeOnPath->v3Pos);

				if (fDistToPathClosest - fDistToClosest < s_fStickinessFactor)
				{
					return pClosestNodeOnPath;
				}
				else
				{
					return pClosestNode;
				}
			}
			else
			{
				return pClosestNode;
			}
		}
	}

	return NULL;
}


//Returns the GoldenPathNode at a specified position, if one exists
GoldenPathNode *goldenPath_FindNodeForPos(Vec3 v3Pos)
{
	GoldenPathStatus *pStatus = goldenPath_GetStatus();
	GoldenPathNode *pNode = NULL;
	static GoldenPathNode **eaEntries = NULL;

	eaClear(&eaEntries);
		
	octreeFindInSphereEA(pStatus->pOctree, &eaEntries, v3Pos, .1, NULL, NULL);

	if (eaSize(&eaEntries) > 0 && eaEntries[0])
	{
		S32 i;

		for (i = 0; i < eaSize(&eaEntries); ++i)
		{
			pNode = eaEntries[0];
			if (pNode && distance3Squared(pNode->v3Pos, v3Pos) < .1)
			{
				return pNode;
			}
		}
	}

	return NULL;
}

//Adds a golden path node to the current list of nodes
// If one already exists for that position, it returns the existing node
GoldenPathNode *goldenPath_AddNode(Vec3 v3Pos)
{
	GoldenPathNode *pNewNode = NULL;
	static S32 iIndex = 1;
	GoldenPathStatus *pStatus = goldenPath_GetStatus();

	PERFINFO_AUTO_START_FUNC();

	pNewNode = goldenPath_FindNodeForPos(v3Pos);

	if(pNewNode == NULL)
	{
		pNewNode = StructCreate(parse_GoldenPathNode);

		if(s_eaGoldenPathNodes && eaSize(&s_eaGoldenPathNodes) > 0)
		{
			iIndex = s_eaGoldenPathNodes[eaSize(&s_eaGoldenPathNodes) - 1]->uID + 1;
		}
		else
		{
			iIndex = 1;
		}

		pNewNode->uID = iIndex;
		copyVec3(v3Pos, pNewNode->v3Pos);

		pNewNode->octreeEntry.node = pNewNode;
		pNewNode->octreeEntry.bounds.radius = 0;
		copyVec3(v3Pos, pNewNode->octreeEntry.bounds.min);
		copyVec3(v3Pos, pNewNode->octreeEntry.bounds.mid);
		copyVec3(v3Pos, pNewNode->octreeEntry.bounds.max);

		octreeAddEntry(pStatus->pOctree, &pNewNode->octreeEntry, OCT_MEDIUM_GRANULARITY);

		eaIndexedAdd(&s_eaGoldenPathNodes, pNewNode);
	}

	PERFINFO_AUTO_STOP();

	return pNewNode;
}

//Adds a GoldenPathEdge to each of the passed in nodes
void goldenPath_AddConnection(GoldenPathNode *pNode1, GoldenPathNode *pNode2, F32 fCost)
{
	GoldenPathEdge *pEdge1 = eaIndexedGetUsingInt(&pNode1->eaConnections, pNode2->uID);
	GoldenPathEdge *pEdge2 = eaIndexedGetUsingInt(&pNode2->eaConnections, pNode1->uID);

	PERFINFO_AUTO_START_FUNC();

	if (pNode1 == pNode2)
	{
		return;
	}

	if (!pEdge1)
	{
		pEdge1 = StructCreate(parse_GoldenPathEdge);
		pEdge1->uOtherID = pNode2->uID;
		pEdge1->fCost = fCost;
		pEdge1->pOther = pNode2;
		eaIndexedAdd(&pNode1->eaConnections, pEdge1);
	}

	if (!pEdge2)
	{
		pEdge2 = StructCreate(parse_GoldenPathEdge);
		pEdge2->uOtherID = pNode1->uID;
		pEdge2->fCost = fCost;
		pEdge2->pOther = pNode1;
		eaIndexedAdd(&pNode2->eaConnections, pEdge2);
	}

	PERFINFO_AUTO_STOP();
}

//Initializes the golden path, which involves taking the world path nodes from the bins and converting them into golden path nodes
AUTO_COMMAND;
void goldenPath_Init()
{
	GoldenPathStatus *pStatus = goldenPath_GetStatus();
	Entity *pPlayerEnt = entActivePlayerPtr();
	WorldRegion *pRegion = NULL;
	WorldPathNode **eaPathNodes = NULL;

	PERFINFO_AUTO_START_FUNC();

	loadstart_printf("Initializing golden path...");

	pStatus->bFoundPath = false;
	
	if(pPlayerEnt)
	{
		pRegion = entGetRegion(pPlayerEnt);

		if(pRegion)
		{
			eaPathNodes = worldRegionGetPathNodes(pRegion);
		}
	}

	pStatus->iFXStartPoint = -1;

	if(s_eaGPNodesOnPath)
	{
		eaClear(&s_eaGPNodesOnPath);
	}

	if(s_ea32FX)
	{
		int i;

		for (i = 0; i < ea32Size(&s_ea32FX); ++i)
		{
			if(s_ea32FX[i] != 0)
				dtFxKill(s_ea32FX[i]);
		}

		ea32Clear(&s_ea32FX);
	}

	if(pStatus->eaPathFX)
	{
		int i;

		for (i = 0; i < eaSize(&pStatus->eaPathFX); ++i)
		{
			if(!pStatus->eaPathFX[i]->bIsTeleport)
				killFXConnection(pStatus->eaPathFX[i]);
				//dtFxKill(pStatus->eaPathFX[i]->fx);
		}
		eaClearStruct(&pStatus->eaPathFX, parse_GoldenPathFXConnection);
	}

	if(eaPathNodes && eaSize(&eaPathNodes))
	{
		int i;

		if (pStatus->pOctree)
		{
			octreeDestroy(pStatus->pOctree);
		}
		pStatus->pOctree = octreeCreate();

		if(!s_eaGoldenPathNodes)
		{
			eaCreate(&s_eaGoldenPathNodes);
			eaIndexedEnable(&s_eaGoldenPathNodes, parse_GoldenPathNode);
		}
		else
		{
			eaClearStruct(&s_eaGoldenPathNodes, parse_GoldenPathNode);
		}

		for (i = 0; i < eaSize(&eaPathNodes); ++i)
		{
			WorldPathNode *pWPNode = eaPathNodes[i];
			if (pWPNode && pWPNode->properties.eaConnections)
			{
				GoldenPathNode *pGNode = NULL;
				int j;

				pGNode = goldenPath_AddNode(pWPNode->position);

				if(pGNode)
				{
					pGNode->bCanBeObstructed = pWPNode->properties.bCanBeObstructed;
					pGNode->bIsSecret = pWPNode->properties.bIsSecret;
					pGNode->iTeleportID = pWPNode->properties.iTeleportID;

					for (j = 0; j < eaSize(&pWPNode->properties.eaConnections); ++j)
					{
						WorldPathEdge *pEdge = pWPNode->properties.eaConnections[j];

						if(pEdge && !(pEdge->v3Other[0] == 0 && pEdge->v3Other[1] == 0 && pEdge->v3Other[2] == 0))
						{
							//We need the other node so we can add both connections, since addNode gives us the existing node
							//  if one exists, we can call it without worrying about creating duplicates
							GoldenPathNode *pOtherNode = goldenPath_AddNode(pEdge->v3Other);
							F32 fCost = distance3(pWPNode->position, pEdge->v3Other);
							goldenPath_AddConnection(pGNode, pOtherNode, fCost);
						}
					}

					if (pGNode->iTeleportID != 0)
					{
						for (j = 0; j < eaSize(&eaPathNodes); ++j)
						{
							WorldPathNode *pOtherNode = eaPathNodes[j];

							if (pOtherNode && pOtherNode->properties.iTeleportID == pGNode->iTeleportID)
							{
								GoldenPathNode *pOtherGPNode = goldenPath_AddNode(pOtherNode->position);
								goldenPath_AddConnection(pGNode, pOtherGPNode, 0.001);
							}
						}
					}
				}
			}
		}
	}

	pStatus->pTargetWaypoint = NULL;
	pStatus->bFoundPath = false;
	pStatus->bHasArrived = false;
	pStatus->pFirstNodeInArea = NULL;

	loadend_printf(" done.");

	PERFINFO_AUTO_STOP();
}

AUTO_COMMAND;
void goldenPath_HowManyNodes()
{
	Entity *pEnt = entActivePlayerPtr();
	Vec3 v3Pos;

	if(pEnt)
	{
		WorldRegion *pRegion = NULL;
		entGetPos(pEnt, v3Pos);

		pRegion = worldGetWorldRegionByPos(v3Pos);

		if(pRegion)
		{
			WorldPathNode **eaNodes = worldRegionGetPathNodes(pRegion);

			if(eaNodes)
			{
				printf("There are %d world path nodes and %d golden nodes\n", eaSize(&eaNodes), eaSize(&s_eaGoldenPathNodes));
			}
		}

	}
}

//Set this to one to enable golden path debug features. Doesn't do anything right now.
AUTO_CMD_INT(s_bGPDebug, goldenPathDebug);

static void resetNodesVisited()
{
	int i;

	for (i = 0; i < eaSize(&s_eaGoldenPathNodes); ++i)
	{
		s_eaGoldenPathNodes[i]->bHasBeenVisited = false;
	}
}

static int goldenPath_TraverseMesh(GoldenPathNode *pNode, U32 color)
{
	int iTotal = 1;
	int i;

	pNode->bHasBeenVisited = true;

	worldDebugAddPoint(pNode->v3Pos, color);

	for (i = 0; i < eaSize(&pNode->eaConnections); ++i)
	{
		worldDebugAddLine(pNode->v3Pos, pNode->eaConnections[i]->pOther->v3Pos, color);
		if(!pNode->eaConnections[i]->pOther->bHasBeenVisited)
		{
			iTotal += goldenPath_TraverseMesh(pNode->eaConnections[i]->pOther, color);
		}
	}

	return iTotal;
}

AUTO_COMMAND;
void goldenPath_PrintNodes()
{
	int i, j;
	int iEdges = 0;

	for(i = 0; i < eaSize(&s_eaGoldenPathNodes); ++i)
	{
		GoldenPathNode *pNode = s_eaGoldenPathNodes[i];

		if(pNode)
		{
			printf("Node:%d\n%p\nPosition: %f %f %f\nConnections:\n", pNode->uID, pNode, pNode->v3Pos[0], pNode->v3Pos[1], pNode->v3Pos[2]);

			for (j = 0; j < eaSize(&pNode->eaConnections); ++j)
			{
				GoldenPathEdge *pEdge = pNode->eaConnections[j];

				if(pEdge)
				{
					iEdges++;
					printf("    Edge To: %d    %p\n      Cost: %f\n", pEdge->uOtherID, pEdge->pOther, pEdge->fCost);
				}
			}
		}
	}
	printf("\n\nTOTAL: %d Nodes, %d Edges\n", i + 1, iEdges);

	resetNodesVisited();
	j = 0;
	for (i = 0; i < eaSize(&s_eaGoldenPathNodes); ++i)
	{
		if (!s_eaGoldenPathNodes[i]->bHasBeenVisited)
		{
			U32 color = randomU32();
			color |= 0xFF;
			printf("Section #%d - Size: %d nodes\n", j + 1, goldenPath_TraverseMesh(s_eaGoldenPathNodes[i], color));
			j++;
		}
	}
	printf("There are %d different sections\n", j);
}

/*************************************************
* A* search functions
*************************************************/

int goldenPath_CostToTargetCallback(AStarSearchData *pData, GoldenPathNode *pNodeParent, GoldenPathNode *pNode, GoldenPathEdge *pConnectionToNode)
{
	if (pData && pNode)
	{
		GoldenPathNode *pTargetNode = (GoldenPathNode *)pData->targetNode;

		if(pTargetNode)
		{
			int result = round(distance3(pNode->v3Pos, pTargetNode->v3Pos) * COST_FACTOR);
			return result;
		}
	}

	return INT_MAX;
}

int goldenPath_CostCallback(AStarSearchData *pData, GoldenPathNode *pPrevNode,
							GoldenPathEdge *pConnFromPrev, GoldenPathNode *pSourceNode, GoldenPathEdge *pConnection)
{
	if (pConnection)
	{
		int result = round(pConnection->fCost * COST_FACTOR);
		//printf("Cost for connection to %d is %f, returning %d\n", pConnection->uOtherID, pConnection->fCost, result);
		return result;
	}

	return INT_MAX;
}

int goldenPath_GetConnectionsCallback(AStarSearchData *pData, GoldenPathNode *pNode, GoldenPathEdge ***pppConnectionBuffer,
										GoldenPathNode ***pppNodeBuffer, int *position, int *count)
{
	int i;
	*count = 0;

	if (!pData || !pNode || !pppConnectionBuffer || !pppNodeBuffer)
		return false;

	for (i = *position; i < eaSize(&pNode->eaConnections); ++i)
	{
		GoldenPathEdge *pEdge = pNode->eaConnections[i];

		if(pEdge)
		{
			//Check collision for special nodes
			WorldCollCollideResults wcResults = {0};

			if(pEdge->pOther && (pEdge->pOther->bIsSecret || pNode->bIsSecret))
			{
				Entity *pPlayerEnt = entActivePlayerPtr();

				if (pPlayerEnt)
				{
					wcRayCollide(worldGetActiveColl(entGetPartitionIdx(pPlayerEnt)), pNode->v3Pos, pEdge->pOther->v3Pos, WC_FILTER_BIT_MOVEMENT, &wcResults);

					if (!wcResults.hitSomething)
					{
						wcRayCollide(worldGetActiveColl(entGetPartitionIdx(pPlayerEnt)), pEdge->pOther->v3Pos, pNode->v3Pos, WC_FILTER_BIT_MOVEMENT, &wcResults);
					}
				}
			}

			if (!wcResults.hitSomething)
			{
				(*pppNodeBuffer)[*count] = pEdge->pOther;
				(*pppConnectionBuffer)[*count] = pEdge;

				(*count)++;
				(*position)++;
			}
			else
			{
				(*position)++;
			}
		}
	}

	return *count > 0;
}

//Returns true if the passed waypoint is an area waypoint and the passed point falls inside of it
static bool goldenPath_IsPointInArea(Vec3 v3Point, MinimapWaypoint *pWaypoint)
{
	if (pWaypoint && pWaypoint->fXAxisRadius > 0 && pWaypoint->fYAxisRadius > 0)
	{
		Vec3 v3Temp, v3Temp2;
		Vec3 upVec3 = {0, 1, 0};
		F32 fDist = 10;

		copyVec3(v3Point, v3Temp);

		v3Temp[0] -= pWaypoint->pos[0];
		v3Temp[1] = 0;
		v3Temp[2] -= pWaypoint->pos[2];

		rotateVecAboutAxis(pWaypoint->fRotation, upVec3, v3Temp, v3Temp2);

		v3Temp2[0] /= pWaypoint->fXAxisRadius;
		v3Temp2[2] /= pWaypoint->fYAxisRadius;

		fDist = distance3XZSquared(v3Temp2, zerovec3);

		if (fDist < 1 && FINITE(fDist))
		{
			return true;
		}
	}

	return false;
}

static void goldenPath_SmoothPath(bool bClearTemp, F32 f1, F32 f2)
{
	GoldenPathNode **eaNewPath = NULL;
	static GoldenPathNode **eaTempNodes = NULL;
	S32 i;

	if (bClearTemp)
		eaClearStruct(&eaTempNodes, parse_GoldenPathNode);

	for (i = 0; i < eaSize(&s_eaGPNodesOnPath) - 2; ++i)
	{
		GoldenPathNode *pStartNode = s_eaGPNodesOnPath[i];
		GoldenPathNode *pCornerNode = s_eaGPNodesOnPath[i + 1];
		GoldenPathNode *pEndNode = s_eaGPNodesOnPath[i + 2];

		if (pStartNode && pCornerNode && pEndNode)
		{
			GoldenPathNode *pNewNode1 = StructCreate(parse_GoldenPathNode);
			GoldenPathNode *pNewNode2 = StructCreate(parse_GoldenPathNode);

			Vec3 v3Temp;
			Vec3 v3PushVec;
			F32 fTemp;

			if (eaSize(&eaTempNodes) == 0)
			{
				pNewNode1->uID = 100000 + i;
			}
			else
			{
				pNewNode1->uID = eaTempNodes[eaSize(&eaTempNodes) - 1]->uID + 1;
			}
			pNewNode2->uID = pNewNode1->uID + 1;
			pNewNode1->bIsTemporary = true;
			pNewNode2->bIsTemporary = true;
			eaPush(&eaTempNodes, pNewNode1);
			eaPush(&eaTempNodes, pNewNode2);

			//Create new control point
			subVec3(pCornerNode->v3Pos, pStartNode->v3Pos, v3Temp);
			normalVec3(v3Temp);
			fTemp = distance3(pStartNode->v3Pos, pCornerNode->v3Pos);
			scaleVec3(v3Temp, fTemp * f1, v3Temp);
			subVec3(pCornerNode->v3Pos, v3Temp, v3Temp);
			//Now push it out
			subVec3(pEndNode->v3Pos, pCornerNode->v3Pos, v3PushVec);
			normalVec3(v3PushVec);
			fTemp = distance3(pStartNode->v3Pos, pCornerNode->v3Pos);
			scaleVec3(v3PushVec, fTemp * f2, v3PushVec); //Consider recalculating distance
			subVec3(v3Temp, v3PushVec, v3Temp);

			copyVec3(v3Temp, pNewNode1->v3Pos);

			//Create new control point
			subVec3(pCornerNode->v3Pos, pEndNode->v3Pos, v3Temp);
			normalVec3(v3Temp);
			fTemp = distance3(pEndNode->v3Pos, pCornerNode->v3Pos);
			scaleVec3(v3Temp, fTemp * f1, v3Temp);
			subVec3(pCornerNode->v3Pos, v3Temp, v3Temp);
			//Now push it out
			subVec3(pStartNode->v3Pos, pCornerNode->v3Pos, v3PushVec);
			normalVec3(v3PushVec);
			fTemp = distance3(pEndNode->v3Pos, pCornerNode->v3Pos);
			scaleVec3(v3PushVec, fTemp * f2, v3PushVec); //Consider recalculating distance
			subVec3(v3Temp, v3PushVec, v3Temp);

			copyVec3(v3Temp, pNewNode2->v3Pos);

			if(i == 0)
				eaPush(&eaNewPath, pStartNode);
			eaPush(&eaNewPath, pNewNode1);
			eaPush(&eaNewPath, pCornerNode);
			eaPush(&eaNewPath, pNewNode2);
		}
	}

	if (eaNewPath && eaSize(&eaNewPath) > 0)
	{
		eaPush(&eaNewPath, s_eaGPNodesOnPath[eaSize(&s_eaGPNodesOnPath) - 1]);
		eaClear(&s_eaGPNodesOnPath);
		eaPushEArray(&s_eaGPNodesOnPath, &eaNewPath);
	}
}

#define COLL_DIST_THRESHOLD 9

void goldenPath_OutputPathCallback(AStarSearchData *pData, AStarInfo *pTailInfo)
{
	GoldenPathStatus *pStatus = goldenPath_GetStatus();
	AStarInfo *pCurInfo = pTailInfo;
	int i = 0;

	pStatus->iFXStartPoint = -1;

	if(!s_eaGPNodesOnPath)
	{
		eaCreate(&s_eaGPNodesOnPath);
	}

	pStatus->pFirstNodeInArea = NULL;

	for(;;)
	{
		GoldenPathNode *pNode = (GoldenPathNode *)pCurInfo->ownerNode;

		if(pStatus && pStatus->pTargetWaypoint)
		{
			if (!goldenPath_IsPointInArea(pNode->v3Pos, pStatus->pTargetWaypoint))
			{

				if (i < eaSize(&s_eaGPNodesOnPath))
				{
					if (s_eaGPNodesOnPath[i] == pNode)
					{
						pStatus->iFXStartPoint = i;
					}
					else
					{
						s_eaGPNodesOnPath[i] = pNode;
					}
				}
				else
				{
					eaPush(&s_eaGPNodesOnPath, pNode);
				}
				i++;
			}
			else
			{
				eaClear(&s_eaGPNodesOnPath);
				i = 0;
				pStatus->pFirstNodeInArea = pNode;
			}
		}

		//If pNode is the source node, we are finished looping through all the nodes in the path
		if(pNode == pData->sourceNode)
		{
			eaSetCapacity(&s_eaGPNodesOnPath, i);
			goldenPath_SmoothPath(true, .1, .04);
			
			//If the waypoint isn't an area waypoint, try to move the final node to the exact location of the waypoint
			if(pStatus->pTargetWaypoint->fXAxisRadius == 0 && pStatus->pTargetWaypoint->fYAxisRadius == 0)
			{
				static GoldenPathNode *pTempFinalNode = NULL;
				WorldCollCollideResults wcResults = {0};

				if (!pTempFinalNode)
				{
					pTempFinalNode = StructCreate(parse_GoldenPathNode);
					pTempFinalNode->bIsTemporary = 1;
				}

				copyVec3(pStatus->pTargetWaypoint->pos, pTempFinalNode->v3Pos);
				//Waypoints tend to be at ground level for contacts, so raise the point up a bit
				if (pStatus->pTargetWaypoint->pchContactName)
				{
					pTempFinalNode->v3Pos[1] += 2.5;
				}

				if (eaSize(&s_eaGPNodesOnPath) >= 2 && s_eaGPNodesOnPath[1] && distance3Squared(s_eaGPNodesOnPath[0]->v3Pos, pTempFinalNode->v3Pos) > 1)
				{
					Vec3 v1, v2;

					subVec3(s_eaGPNodesOnPath[0]->v3Pos, s_eaGPNodesOnPath[1]->v3Pos, v1);
					subVec3(pTempFinalNode->v3Pos, s_eaGPNodesOnPath[0]->v3Pos, v2);

					//We don't want the path to go past the waypoint and then turn back, so check the angle
					//and if it is less than 90 degrees, replace the final node instead of inserting a new one
					if (dotVec3(v1, v2) >= 0)
					{
						wcRayCollide(worldGetActiveColl(entGetPartitionIdx(entActivePlayerPtr())), pTempFinalNode->v3Pos, s_eaGPNodesOnPath[0]->v3Pos, WC_FILTER_BIT_MOVEMENT, &wcResults);

						if (!wcResults.hitSomething || distance3Squared(wcResults.posWorldImpact, pTempFinalNode->v3Pos) < COLL_DIST_THRESHOLD)
						{
							wcRayCollide(worldGetActiveColl(entGetPartitionIdx(entActivePlayerPtr())), s_eaGPNodesOnPath[0]->v3Pos, pTempFinalNode->v3Pos, WC_FILTER_BIT_MOVEMENT, &wcResults);
						}

						if (!wcResults.hitSomething || distance3Squared(wcResults.posWorldImpact, pTempFinalNode->v3Pos) < COLL_DIST_THRESHOLD)
						{
							eaInsert(&s_eaGPNodesOnPath, pTempFinalNode, 0);
						}
					}
					else
					{
						wcRayCollide(worldGetActiveColl(entGetPartitionIdx(entActivePlayerPtr())), pTempFinalNode->v3Pos, s_eaGPNodesOnPath[1]->v3Pos, WC_FILTER_BIT_MOVEMENT, &wcResults);

						if (!wcResults.hitSomething || distance3Squared(wcResults.posWorldImpact, pTempFinalNode->v3Pos) < COLL_DIST_THRESHOLD)
						{
							wcRayCollide(worldGetActiveColl(entGetPartitionIdx(entActivePlayerPtr())), s_eaGPNodesOnPath[1]->v3Pos, pTempFinalNode->v3Pos, WC_FILTER_BIT_MOVEMENT, &wcResults);
						}

						if (!wcResults.hitSomething || distance3Squared(wcResults.posWorldImpact, pTempFinalNode->v3Pos) < COLL_DIST_THRESHOLD)
						{
							s_eaGPNodesOnPath[0] = pTempFinalNode;
						}
					}
				}
			}

			return;
		}
		pCurInfo = pCurInfo->parentInfo;
	}
}

bool goldenPath_PerformSearch(GoldenPathNode *pSourceNode, GoldenPathNode *pTargetNode)
{
	NavSearchFunctions goldenPath_NavSearchFunctions =
	{
		(NavSearchCostToTargetFunction)goldenPath_CostToTargetCallback,
		(NavSearchCostFunction)goldenPath_CostCallback,
		(NavSearchGetConnectionsFunction)goldenPath_GetConnectionsCallback,
		(NavSearchOutputPath)goldenPath_OutputPathCallback,
		NULL, NULL, NULL
	};
	AStarSearchData *pAStarData = createAStarSearchData();
	bool bFoundPath = false;

	PERFINFO_AUTO_STOP();

	initAStarSearchData(pAStarData);
	pAStarData->nodeAStarInfoOffset = offsetof(GoldenPathNode, astar_info);
	pAStarData->sourceNode = pSourceNode;
	pAStarData->targetNode = pTargetNode;

	AStarSearch(pAStarData, &goldenPath_NavSearchFunctions);

	if (pAStarData->pathWasOutput)
	{
		bFoundPath = true;
	}

	destroyAStarSearchData(pAStarData);

	PERFINFO_AUTO_STOP();

	return bFoundPath;
}

AUTO_COMMAND;
void goldenPath_FindPathFromPlayerToTarget(F32 x, F32 y, F32 z)
{
	Vec3 v3Target = {x, y, z};
	goldenPath_PerformSearch(goldenPath_FindClosestNodeToPlayer(), goldenPath_FindClosestNode(v3Target, &s_eaGoldenPathNodes));
}


/***************/

//Checks the region to see if the path nodes are the same
bool goldenPath_DoNodesNeedUpdate(bool *pbRegionHasNodes)
{
	static WorldPathNode **eaWPNodes = NULL;
	WorldPathNode **eaNewWPNodes = NULL;
	Entity *pEntity = entActivePlayerPtr();
	WorldRegion *pRegion = NULL;
	bool bShouldUpdate = false;
	static S32 iNumNodes = 0;

	if(pEntity)
	{
		pRegion = entGetRegion(pEntity);

		if(pRegion)
		{
			eaNewWPNodes = worldRegionGetPathNodes(pRegion);

			*pbRegionHasNodes = eaNewWPNodes != NULL && eaSize(&eaNewWPNodes) > 0;

			if (eaNewWPNodes != eaWPNodes || iNumNodes != eaSize(&eaNewWPNodes))
			{
				bShouldUpdate = true;
				eaWPNodes = eaNewWPNodes;
				iNumNodes = eaSize(&eaNewWPNodes);
			}
		}
	}

	return bShouldUpdate;
}

/******************************************************************************************************************/

S32 goldenPath_CompareWaypointsByDistance(MinimapWaypoint *pWaypoint1, MinimapWaypoint *pWaypoint2)
{
	if (pWaypoint1 && pWaypoint2)
	{
		Entity *pEnt = entActivePlayerPtr();
		Vec3 v3EntPos;

		if (pEnt)
		{
			entGetPos(pEnt, v3EntPos);

			return distance3Squared(v3EntPos, pWaypoint2->pos) - distance3Squared(v3EntPos, pWaypoint1->pos);
		}
	}
	return 0;
}

//Area waypoints are given lower priority than regular waypoints
S32 goldenPath_CompareWaypointsByArea(MinimapWaypoint *pWaypoint1, MinimapWaypoint *pWaypoint2)
{
	if (pWaypoint1 && pWaypoint2)
	{
		if (pWaypoint1->fXAxisRadius > 0 && pWaypoint1->fYAxisRadius > 0 && pWaypoint2->fXAxisRadius == 0 && pWaypoint2->fYAxisRadius == 0)
		{
			return 1;
		} 
		else if (pWaypoint1->fXAxisRadius == 0 && pWaypoint1->fYAxisRadius == 0 && pWaypoint2->fXAxisRadius > 0 && pWaypoint2->fYAxisRadius > 0)
		{
			return -1;
		}
		else
		{
			return 0;
		}
	}
	return 0;
}

//Check which waypoint is more recent
S32 goldenPath_CompareWaypointsByTime(MinimapWaypoint *pWaypoint1, MinimapWaypoint *pWaypoint2)
{
	if (pWaypoint1 && pWaypoint2)
	{
		Entity *pEnt = entActivePlayerPtr();

		if (pEnt)
		{
			MissionInfo *pInfo = mission_GetInfoFromPlayer(pEnt);

			if (pInfo)
			{
				Mission *pMission1 = mission_GetMissionOrSubMissionByName(pInfo, pWaypoint1->pchMissionRefString);
				Mission *pMission2 = mission_GetMissionOrSubMissionByName(pInfo, pWaypoint2->pchMissionRefString);

				if (pMission1 && pMission2)
				{
					return pMission1->startTime - pMission2->startTime;
				}
				else if (pMission1 && !pMission2)
				{
					return 1;
				}
				else if (!pMission1 && pMission2)
				{
					return -1;
				}
				return 0;
			}
		}
	}
	return 0;
}

S32 goldenPath_CompareWaypointsByMap(MinimapWaypoint *pWaypoint1, MinimapWaypoint *pWaypoint2)
{
	if (!pWaypoint1->bIsDoorWaypoint && pWaypoint2->bIsDoorWaypoint)
	{
		return 1;
	}
	else if (!pWaypoint2->bIsDoorWaypoint && pWaypoint1->bIsDoorWaypoint)
	{
		return -1;
	}
	else
	{
		return 0;
	}
}

static S32 getWaypointSortPriority(MinimapWaypoint *pWaypoint)
{
	if (pWaypoint && pWaypoint->pchMissionRefString)
	{
		Entity *pEnt = entActivePlayerPtr();

		if (pEnt)
		{
			MissionInfo *pInfo = mission_GetInfoFromPlayer(pEnt);

			if (pInfo)
			{
				Mission *pMission = mission_GetMissionOrSubMissionByName(pInfo, pWaypoint->pchMissionRefString);

				if (pMission)
				{
					MissionDef *pDef = mission_GetDef(pMission);

					if (pDef)
					{
						return pDef->iSortPriority;
					}
				}
			}
		}
	}

	return 0;
}

//Compares waypoints by the sort priority on the mission
S32 goldenPath_CompareWaypointsByPriority(MinimapWaypoint *pWaypoint1, MinimapWaypoint *pWaypoint2)
{
	if (pWaypoint1 && pWaypoint2)
	{
		Entity *pEnt = entActivePlayerPtr();
		
		if (pEnt)
		{
			MissionInfo *pInfo = mission_GetInfoFromPlayer(pEnt);
			
			if (pInfo)
			{
				Mission *pMission1 = mission_GetMissionOrSubMissionByName(pInfo, pWaypoint1->pchMissionRefString);
				Mission *pMission2 = mission_GetMissionOrSubMissionByName(pInfo, pWaypoint2->pchMissionRefString);

				if (pMission1 && pMission2)
				{
					//return pMission1->displayOrder - pMission2->displayOrder;
					MissionDef *pDef1 = mission_GetDef(pMission1);
					MissionDef *pDef2 = mission_GetDef(pMission2);

					if (pDef1 && pDef2)
					{
						if (pDef2->iSortPriority == pDef1->iSortPriority)
						{
							return 0;
						}
						if (pDef2->iSortPriority == 0)
						{
							return 1;
						}
						if (pDef1->iSortPriority == 0)
						{
							return -1;
						}
						return pDef2->iSortPriority - pDef1->iSortPriority;
					}
				}
				else if (pMission1 && !pMission2)
				{
					return 1;
				}
				else if (!pMission1 && pMission2)
				{
					return -1;
				}
				return 0;
			}
		}
	}
	return 0;
}

static bool isWaypointRepeatable(MinimapWaypoint *pWaypoint)
{
	Entity *pPlayerEnt = entActivePlayerPtr();

	if (pPlayerEnt && pWaypoint && pWaypoint->pchMissionRefString)
	{
		MissionInfo *pInfo = mission_GetInfoFromPlayer(pPlayerEnt);
		
		if (pInfo)
		{
			Mission *pMission = mission_GetMissionOrSubMissionByName(pInfo, pWaypoint->pchMissionRefString);

			if (pMission)
			{
				MissionDef *pDef = mission_GetDef(pMission);

				if (pDef)
				{
					return pDef->repeatable;
				}
			}
		}
	}

	return false;
}

static bool isWaypointMissionReturn(MinimapWaypoint *pWaypoint)
{
	Entity *pPlayerEnt = entActivePlayerPtr();

	if (pPlayerEnt && pWaypoint && pWaypoint->pchMissionRefString)
	{
		MissionInfo *pInfo = mission_GetInfoFromPlayer(pPlayerEnt);

		if (pInfo)
		{
			return mission_StateSucceeded(pInfo, pWaypoint->pchMissionRefString);
		}
	}

	return false;
}

S32 goldenPath_CompareWaypointsBySuccess(MinimapWaypoint *pWaypoint1, MinimapWaypoint *pWaypoint2)
{
	bool bIsOneReturn = isWaypointMissionReturn(pWaypoint1);
	bool bIsTwoReturn = isWaypointMissionReturn(pWaypoint2);

	if (bIsOneReturn && !bIsTwoReturn)
	{
		return -1;
	}
	else if (bIsTwoReturn && !bIsOneReturn)
	{
		return 1;
	}
	else
	{
		return 0;
	}
}

static bool isWaypointPrimaryMission(MinimapWaypoint *pWaypoint, bool *pbHasActivePrimaryMission, bool * pbDoesPrimaryMissionNeedReturn)
{
	Entity *pPlayerEnt = entActivePlayerPtr();

	if (pPlayerEnt && pWaypoint && pWaypoint->pchMissionRefString)
	{
		MissionInfo *pInfo = mission_GetInfoFromPlayer(pPlayerEnt);
		const char *pchPrimaryMission = entGetPrimaryMission(pPlayerEnt);

		if (pInfo && pchPrimaryMission && pchPrimaryMission[0])
		{
			bool bPrimaryComplete = mission_GetNumTimesCompletedByName(pInfo, pchPrimaryMission) > 0;
			Mission *pPrimaryMission = mission_GetMissionOrSubMissionByName(pInfo, pchPrimaryMission);

			*pbDoesPrimaryMissionNeedReturn = mission_StateSucceeded(pInfo, pchPrimaryMission);

			if (bPrimaryComplete || !pPrimaryMission)
			{
				*pbHasActivePrimaryMission = false;
				return false;
			}
			else
			{
				const char* pchColon = strchr(pWaypoint->pchMissionRefString, ':');
				S32 iLen = pchColon ? (pchColon - pWaypoint->pchMissionRefString) : (S32)strlen(pWaypoint->pchMissionRefString);

				*pbHasActivePrimaryMission = true;
				return strnicmp(pWaypoint->pchMissionRefString, pchPrimaryMission, iLen) == 0;
			}
		}
	}
	*pbHasActivePrimaryMission = false;
	return false;
}

//Check if the waypoints are related to the player's primary mission
S32 goldenPath_CompareWaypointsByPrimaryMission(MinimapWaypoint *pWaypoint1, MinimapWaypoint *pWaypoint2)
{
	Entity *pPlayerEnt = entActivePlayerPtr();

	if (pPlayerEnt && pWaypoint1 && pWaypoint2)
	{
		MissionInfo *pInfo = mission_GetInfoFromPlayer(pPlayerEnt);
		const char *pchPrimaryMission = entGetPrimaryMission(pPlayerEnt);

		if (pInfo && pchPrimaryMission && pchPrimaryMission[0])
		{
			bool bPrimaryComplete = mission_GetNumTimesCompletedByName(pInfo, pchPrimaryMission) > 0;

			if (bPrimaryComplete)
			{
				return 0;
			}
			else
			{
				const char* pchColon = strchr(pWaypoint1->pchMissionRefString, ':');
				S32 iLen = pchColon ? (pchColon - pWaypoint1->pchMissionRefString) : (S32)strlen(pWaypoint1->pchMissionRefString);
				bool bWaypoint1_IsPrimary = false;
				bool bWaypoint2_IsPrimary = false;

				bWaypoint1_IsPrimary = strnicmp(pWaypoint1->pchMissionRefString, pchPrimaryMission, iLen) == 0;

				pchColon = strchr(pWaypoint2->pchMissionRefString, ':');
				iLen = pchColon ? (pchColon - pWaypoint2->pchMissionRefString) : (S32)strlen(pWaypoint2->pchMissionRefString);

				bWaypoint2_IsPrimary = strnicmp(pWaypoint2->pchMissionRefString, pchPrimaryMission, iLen) == 0;

				if (bWaypoint1_IsPrimary && !bWaypoint2_IsPrimary)
				{
					return 1;
				}
				else if (!bWaypoint1_IsPrimary && bWaypoint2_IsPrimary)
				{
					return -1;
				} 
				else
				{
					return 0;
				}
			}
		}
	}

	return 0;
}

static bool isWaypointOpenMission(MinimapWaypoint *pWaypoint)
{
	Entity *pPlayerEnt = entActivePlayerPtr();

	if (pPlayerEnt && pWaypoint && pWaypoint->pchMissionRefString && pWaypoint->pchMissionRefString[0])
	{
		MissionInfo *pInfo = mission_GetInfoFromPlayer(pPlayerEnt);
		const char *pchOpenMission = (pInfo ? pInfo->pchCurrentOpenMission : NULL);

		if (pchOpenMission)
		{
			const char* pchColon = strchr(pWaypoint->pchMissionRefString, ':');
			S32 iLen = pchColon ? (pchColon - pWaypoint->pchMissionRefString) : (S32)strlen(pWaypoint->pchMissionRefString);

			return strnicmp(pWaypoint->pchMissionRefString, pchOpenMission, iLen) == 0;
		}
	}

	return false;
}

S32 goldenPath_CompareWaypointsByOpenMission(MinimapWaypoint *pWaypoint1, MinimapWaypoint *pWaypoint2)
{
	Entity *pPlayerEnt = entActivePlayerPtr();

	if (pPlayerEnt && pWaypoint1 && pWaypoint2)
	{
		MissionInfo *pInfo = mission_GetInfoFromPlayer(pPlayerEnt);
		const char *pchOpenMission = (pInfo ? pInfo->pchCurrentOpenMission : NULL);

		if (pchOpenMission)
		{
			const char* pchColon = strchr(pWaypoint1->pchMissionRefString, ':');
			S32 iLen = pchColon ? (pchColon - pWaypoint1->pchMissionRefString) : (S32)strlen(pWaypoint1->pchMissionRefString);
			bool bWaypoint1_IsOpen = false;
			bool bWaypoint2_IsOpen = false;

			bWaypoint1_IsOpen = strnicmp(pWaypoint1->pchMissionRefString, pchOpenMission, iLen) == 0;

			pchColon = strchr(pWaypoint2->pchMissionRefString, ':');
			iLen = pchColon ? (pchColon - pWaypoint2->pchMissionRefString) : (S32)strlen(pWaypoint2->pchMissionRefString);

			bWaypoint2_IsOpen = strnicmp(pWaypoint2->pchMissionRefString, pchOpenMission, iLen) == 0;

			if (bWaypoint1_IsOpen && !bWaypoint2_IsOpen)
			{
				return 1;
			}
			else if (!bWaypoint1_IsOpen && bWaypoint2_IsOpen)
			{
				return -1;
			} 
			else
			{
				return 0;
			}
		}
	}

	return 0;
}

S32 goldenPath_CompareWaypointsByNull(MinimapWaypoint *pWaypoint1, MinimapWaypoint *pWaypoint2)
{
	if (pWaypoint1 == NULL && pWaypoint2 != NULL && pWaypoint2->pchMissionRefString && pWaypoint2->pchMissionRefString[0])
	{
		return -1;
	}
	else if (pWaypoint1 != NULL && pWaypoint2 == NULL && pWaypoint1->pchMissionRefString && pWaypoint1->pchMissionRefString[0])
	{
		return 1;
	}
	else
	{
		return 0;
	}
}

//Returns true if both missions are not NULL and are both related to missions
bool goldenPath_AreBothWaypointsValid(MinimapWaypoint *pWaypoint1, MinimapWaypoint *pWaypoint2)
{
	if (pWaypoint1 && pWaypoint1->pchMissionRefString && pWaypoint1->pchMissionRefString[0] &&
		pWaypoint2 && pWaypoint2->pchMissionRefString && pWaypoint2->pchMissionRefString[0])
	{
		return true;
	}

	return false;
}

//The order of these will determine priority when choosing which waypoint to take the player to
typedef enum GPWaypointType
{
	kGPWaypointType_Ignore = 0,
	kGPWaypointType_OffMapMission,
	kGPWaypointType_OffMapMissionReturn,
	kGPWaypointType_OnMapReturn,
	kGPWaypointType_OnMapMissionArea,
	kGPWaypointType_OnMapMission,
	kGPWaypointType_PrimaryMissionReturn,
	kGPWaypointType_PrimaryMission,
	kGPWaypointType_OpenMission,
	kGPWaypointType_TeamCorral,
	kGPWaypointType_UserWaypoint
} GPWaypointType;

S32 goldenPath_GetWaypointSortValue(MinimapWaypoint *pWaypoint)
{
	GPWaypointType type = kGPWaypointType_Ignore;

	if (pWaypoint)
	{
		if (pWaypoint->pchMissionRefString && pWaypoint->pchMissionRefString[0])
		{
			bool bHasPrimary = false;
			bool bIsPrimaryReturn = false;
			bool bIsPrimary = isWaypointPrimaryMission(pWaypoint, &bHasPrimary, &bIsPrimaryReturn);
			bool bIsReturn = isWaypointMissionReturn(pWaypoint);
			bool bIsOnMap = !pWaypoint->bIsDoorWaypoint;
			bool bIsArea = pWaypoint->fXAxisRadius > 0 && pWaypoint->fYAxisRadius > 0;
			bool bIsOpenMission = isWaypointOpenMission(pWaypoint);

			if (bIsOpenMission)
			{
				type = kGPWaypointType_OpenMission;
			} 
			else
			{
				if (bHasPrimary && bIsPrimary && !bIsReturn)
				{
					type = kGPWaypointType_PrimaryMission;
				}
				else if (bHasPrimary && bIsPrimary && bIsReturn)
				{
					type = kGPWaypointType_PrimaryMissionReturn;
				}
				else if (bIsOnMap)
				{
					if (bIsReturn)
					{
						type = kGPWaypointType_OnMapReturn;
					} 
					else
					{
						if (bIsArea)
						{
							type = kGPWaypointType_OnMapMissionArea;
						} 
						else
						{
							type = kGPWaypointType_OnMapMission;
						}
					}
				}
				else if (!bIsOnMap)
				{
					if (bIsReturn)
					{
						type = kGPWaypointType_OffMapMissionReturn;
					} 
					else
					{
						type = kGPWaypointType_OffMapMission;
					}
				}
			}
		}
		else if (pWaypoint->type == MinimapWaypointType_SavedWaypoint)
		{
			type = kGPWaypointType_UserWaypoint;
		}
		else if (pWaypoint->type == MinimapWaypointType_TeamCorral)
		{
			type = kGPWaypointType_TeamCorral;
		}
	}

	return (S32)type;
}

S32 goldenPath_CompareWaypointsNew(MinimapWaypoint *pWaypoint1, MinimapWaypoint *pWaypoint2)
{
	S32 iResult = 0;

	iResult = goldenPath_GetWaypointSortValue(pWaypoint1) - goldenPath_GetWaypointSortValue(pWaypoint2);

	if (iResult == 0)
	{
		iResult = goldenPath_CompareWaypointsByPriority(pWaypoint1, pWaypoint2);

		if (iResult == 0)
		{
			iResult = goldenPath_CompareWaypointsByDistance(pWaypoint1, pWaypoint2);
		}
	}

	return iResult;
}

S32 goldenPath_CompareWaypoints(MinimapWaypoint *pWaypoint1, MinimapWaypoint *pWaypoint2)
{
	S32 iResult = 0;
	
	if (pWaypoint1 == NULL && pWaypoint2 == NULL)
	{
		iResult = 0;
	}
	else 
	{
		iResult = goldenPath_CompareWaypointsByNull(pWaypoint1, pWaypoint2);

		if (iResult == 0 && goldenPath_AreBothWaypointsValid(pWaypoint1, pWaypoint2))
		{
			iResult = goldenPath_CompareWaypointsByOpenMission(pWaypoint1, pWaypoint2);

			if (iResult == 0)
			{
				iResult = goldenPath_CompareWaypointsByPrimaryMission(pWaypoint1, pWaypoint2);

				if (iResult == 0)
				{
					iResult = goldenPath_CompareWaypointsByPriority(pWaypoint1, pWaypoint2);

					if (iResult == 0)
					{
						iResult = goldenPath_CompareWaypointsByTime(pWaypoint1, pWaypoint2);

						if (iResult == 0)
						{
							iResult = goldenPath_CompareWaypointsByArea(pWaypoint1, pWaypoint2);

							if (iResult == 0)
							{
								iResult = goldenPath_CompareWaypointsByDistance(pWaypoint1, pWaypoint2);
							}
						}
					}
				}
			}
		}
	}

	return iResult;
}

//Chose a new target
// This runs through all of a player's waypoints and compares them to determine which is the best
// Each criteria for comparison has its own 
bool goldenPath_UpdateTargetNew(GoldenPathStatus *pStatus)
{
	Entity *pPlayerEnt = entActivePlayerPtr();

	PERFINFO_AUTO_START_FUNC();

	if(pStatus && pPlayerEnt)
	{
		MissionInfo *pInfo = mission_GetInfoFromPlayer(pPlayerEnt);

		if(pInfo)
		{
			MinimapWaypoint *pBestWaypoint = NULL;
			int i;

			for (i = 0; i < eaSize(&pInfo->waypointList); ++i)
			{
				MinimapWaypoint *pCurrentWaypoint = pInfo->waypointList[i];

				//Possibly change isWaypointRepeatable to check for a priority higher than 100
				if (getWaypointSortPriority(pCurrentWaypoint) < 100 && goldenPath_CompareWaypointsNew(pCurrentWaypoint, pBestWaypoint) > 0)
				{
					pBestWaypoint = pCurrentWaypoint;
				}
			}

			if (pPlayerEnt->pPlayer && pPlayerEnt->pPlayer->ppMyWaypoints)
			{
				for (i = 0; i < eaSize(&pPlayerEnt->pPlayer->ppMyWaypoints); ++i)
				{
					MinimapWaypoint *pCurrentWaypoint = pPlayerEnt->pPlayer->ppMyWaypoints[i];

					if (goldenPath_CompareWaypointsNew(pCurrentWaypoint, pBestWaypoint) > 0)
					{
						pBestWaypoint = pCurrentWaypoint;
					}
				}
			}

			if (pBestWaypoint)
			{
				bool bHasActivePrimaryMission = false;
				bool bDoesPrimaryMissionNeedReturn = false;
				bool bIsWaypointPrimaryMission = isWaypointPrimaryMission(pBestWaypoint, &bHasActivePrimaryMission, &bDoesPrimaryMissionNeedReturn);

				if (!bHasActivePrimaryMission || bDoesPrimaryMissionNeedReturn || (bHasActivePrimaryMission && bIsWaypointPrimaryMission) ||
					(mapState_OpenMissionFromName(mapStateClient_Get(), pInfo->pchCurrentOpenMission) && zmapInfoGetMapType(NULL) != ZMTYPE_STATIC) ||
					pBestWaypoint->type == MinimapWaypointType_SavedWaypoint)
				{
					copyVec3(pBestWaypoint->pos, s_v3TargetPos);
					s_pchMissionToPathTo = pBestWaypoint->pchMissionRefString;
					pStatus->pTargetWaypoint = pBestWaypoint;
					PERFINFO_AUTO_STOP();
					return true;
				}
			}
		}
	}
	s_pchMissionToPathTo = NULL;
	if (pStatus)
		pStatus->pTargetWaypoint = NULL;
	PERFINFO_AUTO_STOP();
	return false;
}

//This is deprecated - I will take it out when we are sure the new version works -DHOGBERG 6/14/2012
bool goldenPath_UpdateTarget(bool bForceUpdate)
{
	GoldenPathStatus *pStatus = goldenPath_GetStatus();
	Entity *pPlayerEnt = entActivePlayerPtr();
	bool bFoundTarget = false;

	//This isn't used right now, I need to decide if it is worth it to decide ~DHOGBERG 4/27/2012
	bool bShouldUpdate = false;

	if(pPlayerEnt && pPlayerEnt->pPlayer && pStatus)
	{
		MissionInfo *pInfo = mission_GetInfoFromPlayer(pPlayerEnt);
		Vec3 v3EntPos;

		entGetPos(pPlayerEnt, v3EntPos);

		if (pInfo)
		{
			const char *pchPrimaryMission = entGetPrimaryMission(pPlayerEnt);
			const char *pchMostRecentMission = entGetMostRecentMission(pPlayerEnt);
			static const char *pchOldPrimaryMission = NULL;
			static const char *pchOldMostRecentMission = NULL;
			static int iOldPrimaryTarget = 0;
			static int iOldRecentTarget = 0;
			F32 fDistToTarget = distance3Squared(v3EntPos, s_v3TargetPos);
			bool bFoundPrimaryMission = false;
			bool bFoundRecentMission = false;
			bool bPrimaryComplete = false;
			int i;

			if (pchPrimaryMission && pchPrimaryMission[0])
			{
				Mission *pPrimary = mission_GetMissionOrSubMissionByName(pInfo, pchPrimaryMission);
				bPrimaryComplete = mission_GetNumTimesCompletedByName(pInfo, pchPrimaryMission) > 0;
			}

			if (bForceUpdate || bShouldUpdate)
			{
				U32 uLatestTime = 0;

				for (i = 0; i < eaSize(&pInfo->waypointList); ++i)
				{
					MinimapWaypoint *pWaypoint = pInfo->waypointList[i];

					fDistToTarget = distance3Squared(v3EntPos, s_v3TargetPos);

					if (pWaypoint && pWaypoint->pchMissionRefString)
					{
						const char* pchColon = strchr(pWaypoint->pchMissionRefString, ':');
						S32 iLen = pchColon ? (pchColon - pWaypoint->pchMissionRefString) : (S32)strlen(pWaypoint->pchMissionRefString);
						F32 fDistToWaypoint = distance3Squared(v3EntPos, pWaypoint->pos);

						//Check to see if the waypoint is the primary mission
						if (pchPrimaryMission && !bPrimaryComplete)
						{
							//Is this waypoint related to my primary mission
							if(strnicmp(pWaypoint->pchMissionRefString, pchPrimaryMission, iLen) == 0)
							{
								//If this is not the first primary sub-mission we have found,
								// we want to make it the target only if it is closer than the current target
								if (bFoundPrimaryMission && fDistToWaypoint < fDistToTarget)
								{
									copyVec3(pWaypoint->pos, s_v3TargetPos);
									s_pchMissionToPathTo = pWaypoint->pchMissionRefString;
									pStatus->pTargetWaypoint = pWaypoint;
									bFoundTarget = true;
								}
								else if (!bFoundPrimaryMission) //If this is the first sub-mission, just make it the target
								{
									copyVec3(pWaypoint->pos, s_v3TargetPos);
									s_pchMissionToPathTo = pWaypoint->pchMissionRefString;
									pStatus->pTargetWaypoint = pWaypoint;
									bFoundTarget = true;
								}

								bFoundPrimaryMission = true;
							}
						}

						//We do the same thing for most recent mission
						if((!pchPrimaryMission || !bFoundPrimaryMission) && pchMostRecentMission)
						{
							if(strnicmp(pWaypoint->pchMissionRefString, pchMostRecentMission, iLen) == 0)
							{
								//If this is not the first primary sub-mission we have found,
								// we want to make it the target only if it is closer than the current target
								if (bFoundRecentMission && fDistToWaypoint < fDistToTarget)
								{
									copyVec3(pWaypoint->pos, s_v3TargetPos);
									s_pchMissionToPathTo = pWaypoint->pchMissionRefString;
									pStatus->pTargetWaypoint = pWaypoint;
									bFoundTarget = true;
								}
								else if (!bFoundRecentMission)
								{
									copyVec3(pWaypoint->pos, s_v3TargetPos);
									s_pchMissionToPathTo = pWaypoint->pchMissionRefString;
									pStatus->pTargetWaypoint = pWaypoint;
									bFoundTarget = true;
								}

								bFoundRecentMission = true;
							}
						}

						if(!bFoundRecentMission && !bFoundPrimaryMission)
						{
							Mission *pMission = mission_GetMissionOrSubMissionByName(pInfo, pWaypoint->pchMissionRefString);

							if (pMission && pMission->startTime > uLatestTime)
							{
								uLatestTime = pMission->startTime;
								copyVec3(pWaypoint->pos, s_v3TargetPos);
								s_pchMissionToPathTo = pWaypoint->pchMissionRefString;
								pStatus->pTargetWaypoint = pWaypoint;
								bFoundTarget = true;
							}
							else if (pMission && pMission->startTime == uLatestTime && bFoundTarget && fDistToWaypoint < fDistToTarget)
							{
								uLatestTime = pMission->startTime;
								copyVec3(pWaypoint->pos, s_v3TargetPos);
								s_pchMissionToPathTo = pWaypoint->pchMissionRefString;
								pStatus->pTargetWaypoint = pWaypoint;
								bFoundTarget = true;
							}
						}
					}
				}

				if(!bFoundPrimaryMission && pchPrimaryMission && pchPrimaryMission[0] && !bPrimaryComplete)
				{
					bFoundTarget = false;
					s_pchMissionToPathTo = pchPrimaryMission;
					pStatus->pTargetWaypoint = NULL;
				}
			}
		}
	}

	return (bShouldUpdate || bForceUpdate ? bFoundTarget : true);
}

//Stop drawing all golden path FX
static void goldenPath_KillFX()
{
	GoldenPathStatus *pStatus = goldenPath_GetStatus();
	int i;

	for(i = 0; i < ea32Size(&s_ea32FX); ++i)
	{
		if(s_ea32FX[i] != 0)
			dtFxKill(s_ea32FX[i]);
	}
	ea32Clear(&s_ea32FX);

	for (i = 0; i < eaSize(&pStatus->eaPathFX); ++i)
	{
		if(!pStatus->eaPathFX[i]->bIsTeleport)
			killFXConnection(pStatus->eaPathFX[i]);
			//dtFxKill(pStatus->eaPathFX[i]->fx);
	}
	eaClearStruct(&pStatus->eaPathFX, parse_GoldenPathFXConnection);
}

void goldenPath_OncePerFrame()
{
	GoldenPathStatus *pStatus = goldenPath_GetStatus();
	static GoldenPathNode *pPlayerNode = NULL;
	static GoldenPathNode *pTargetNode = NULL;
	GoldenPathNode *pNewPlayerNode = NULL;
	GoldenPathNode *pNewTargetNode = NULL;
	bool bPathNeedsUpdate = false;
	bool bForceNewTarget = false;
	bool bNodesInArea = false;
	static int iTicks = 0;
	Entity *pPlayerEnt = entActivePlayerPtr();

	PERFINFO_AUTO_START_FUNC();

	layerCheckUpdatePathNodeTrackers();

	if (!pStatus)
		return;

	if (pPlayerEnt && pPlayerEnt->pPlayer && pPlayerEnt->pPlayer->pUI)
	{
		s_bShowGoldenpath = pPlayerEnt->pPlayer->pUI->bGoldenPathActive;
	}

	//Check to see if we need to initialize our nodes
	if (s_bShowGoldenpath && goldenPath_DoNodesNeedUpdate(&bNodesInArea))
	{
		goldenPath_Init();
		bForceNewTarget = true;
	}

	//Find our target position, if we can't, then we should stop drawing the path
	if(s_bShowGoldenpath && !goldenPath_UpdateTargetNew(pStatus))
	{
		goldenPath_KillFX();
		return;
	}

	//If golden path is disabled, or this map doesn't have any node, stop drawing the path
	if (!s_bShowGoldenpath || !bNodesInArea)
	{
		goldenPath_KillFX();
		pPlayerNode = NULL;
		pTargetNode = NULL;
		return;
	}

	//If for some reason the list of nodes doesn't exist or is empty, stop drawing
	if (!s_eaGoldenPathNodes || eaSize(&s_eaGoldenPathNodes) == 0)
	{
		goldenPath_KillFX();
		return;
	}

	//Find the nodes closest to the player and the target position
	pNewTargetNode = goldenPath_FindClosestNode(s_v3TargetPos, &s_eaGoldenPathNodes);
	pNewPlayerNode = goldenPath_FindClosestNodeToPlayer();

	//Now we check to see whether we need to update the path or not
	if(pPlayerNode != pNewPlayerNode)
	{
		pPlayerNode = pNewPlayerNode;
		bPathNeedsUpdate = true;
	}

	if (pTargetNode != pNewTargetNode)
	{
		pTargetNode = pNewTargetNode;
		bPathNeedsUpdate = true;
		eaClear(&s_eaGPNodesOnPath);
		goldenPath_KillFX();
		pStatus->iFXStartPoint = -1;
	}

	if (iTicks++ >= FORCE_UPDATE_TICK_COUNT)
	{
		bPathNeedsUpdate = true;
		iTicks = 0;
	}

	if (bPathNeedsUpdate)
	{
		pStatus->bIsPathObstructed = false;

		//Do the A* search and if we find a path, draw it
		if (pStatus->bFoundPath = goldenPath_PerformSearch(pPlayerNode, pTargetNode))
		{
			Entity *pEnt = entActivePlayerPtr();
			Vec3 v3EntPos;
			goldenPath_NewDrawPath();

			if(pEnt)
			{
				entGetPos(pEnt, v3EntPos);

				if (pPlayerNode == pTargetNode || goldenPath_IsPointInArea(v3EntPos, pStatus->pTargetWaypoint))
				{
					pStatus->bHasArrived = true;
				}
				else
				{
					pStatus->bHasArrived = false;
				}
			}
			else
			{
				pStatus->bHasArrived = false;
			}
			
		}
		else
		{
			pStatus->bHasArrived = false;

			goldenPath_KillFX();
		}
	}

	if (pStatus->bFoundPath)
	{
		goldenPath_DrawAreaDeath();
	}

	PERFINFO_AUTO_STOP();
}

/*******************
* UIGen Expressions
********************/


// Returns whether or not the Golden Path feature is enabled
AUTO_EXPR_FUNC(UIGen) ACMD_NAME("GoldenPathGetIsEnabled");
bool exprGoldenPathGetIsEnabled()
{
	return s_bShowGoldenpath;
}

bool goldenPath_isPathingToMission(Mission *pMission)
{
	if (pMission)
	{
		const char *pchMissionName = pMission->missionNameOrig;

		if (pchMissionName && pchMissionName[0] && s_pchMissionToPathTo && s_pchMissionToPathTo[0])
		{
			if (pchMissionName == s_pchMissionToPathTo)
			{
				return true;
			}
			else
			{
				//Check to see if the mission name has the :: sub-mission indicator
				if(strchr(pchMissionName, ':'))
				{
					const char* pchColon = strchr(pchMissionName, ':');
					S32 iLen = pchColon ? (pchColon - pchMissionName) : (S32)strlen(pchMissionName);

					if(strnicmp(s_pchMissionToPathTo, pchMissionName, iLen) == 0)
					{
						return true;
					}
				}
				else
				{
					const char* pchColon = strchr(s_pchMissionToPathTo, ':');
					S32 iLen = pchColon ? (pchColon - s_pchMissionToPathTo) : (S32)strlen(s_pchMissionToPathTo);

					if(strnicmp(s_pchMissionToPathTo, pchMissionName, iLen) == 0)
					{
						return true;
					}
				}
			}
		}
	}

	return false;
}

// Returns whether the mission or sub-mission passed in is being pathed to
AUTO_EXPR_FUNC(UIGen) ACMD_NAME("GoldenPathIsPathingToMission");
bool exprGoldenPathIsPathingToMission(SA_PARAM_OP_VALID Mission *pMission)
{
	return goldenPath_isPathingToMission(pMission);
}

//Returns true if a valid path has been found to the player's target waypoint
AUTO_EXPR_FUNC(UIGen) ACMD_NAME("GoldenPathFoundPath");
bool exprGoldenPathFoundPath()
{
	GoldenPathStatus *pStatus = goldenPath_GetStatus();
	if (pStatus)
	{
		return pStatus->bFoundPath;
	}

	return false;
}

//Returns true if the player's closest node is the same as the target's closest node, or if the player is inside of their target area waypoint
AUTO_EXPR_FUNC(UIGen) ACMD_NAME("GoldenPathPlayerHasArrived");
bool exprGoldenPathPlayerHasArrived()
{
	GoldenPathStatus *pStatus = goldenPath_GetStatus();
	if (pStatus)
	{
		return pStatus->bHasArrived;
	}

	return false;
}

bool goldenPath_IsNoTargetAvailable() 
{
	GoldenPathStatus *pStatus = goldenPath_GetStatus();
	if (pStatus)
	{
		return pStatus->pTargetWaypoint == NULL;
	}

	return false;
}

//Returns true if there are no waypoints available to path to
AUTO_EXPR_FUNC(UIGen) ACMD_NAME("GoldenPathNoWaypoint");
bool exprGoldenPathNoWaypoint()
{
	return goldenPath_IsNoTargetAvailable();

}


AUTO_STARTUP(GoldenPath);
void goldenPath_StartUp(void)
{
	GoldenPathStatus *pStatus = goldenPath_GetStatus();
	GoldenPathConfig *pConfig = StructCreate(parse_GoldenPathConfig);

	if (!gConf.bEnableGoldenPath)
	{
		return;
	}

	ParserLoadFiles(NULL, "defs/config/GoldenPathConfig.def", "GoldenPathConfig.bin", PARSER_OPTIONALFLAG, parse_GoldenPathConfig, pConfig);

	if (pConfig)
	{
		pStatus->pchGoldenPathFX = allocAddString(pConfig->pchGoldenPathFX);
		pStatus->pchGoldenPathObstructedFX = allocAddString(pConfig->pchGoldenPathObstructedFX);
		pStatus->pchGoldenPathAreaFX = allocAddString(pConfig->pchGoldenPathAreaFX);
		pStatus->pchGoldenPathAreaDeathFX = allocAddString(pConfig->pchGoldenPathAreaDeathFX);
		pStatus->fVerticalPathOffset = pConfig->fVerticalPathOffset;

		if (!pStatus->pchGoldenPathFX || !pStatus->pchGoldenPathFX[0])
		{
			ErrorFilenamef("defs/config/GoldenPathConfig.def", "Could not find Golden Path FX name in config file.");
		}
	}
}

#include "AutoGen/gclGoldenPath_h_ast.c"
