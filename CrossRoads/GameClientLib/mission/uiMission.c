/***************************************************************************
*     Copyright (c) 2006, Cryptic Studios
*     All Rights Reserved
*     Confidential Property of Cryptic Studios
***************************************************************************/

#include "encounter_common.h"
#include "gfxdebug.h"
#include "mission_common.h"
#include "gclEntity.h"
#include "Player.h"
#include "StringCache.h"

#include "GlobalTypes.h"
#include "dynFxInfo.h"
#include "dynFxManager.h"
#include "dynFxInterface.h"

#include "autogen/dynFxInfo_h_ast.h"


// Creates a client dictionary for missions so that refs will be sent to the server
// We aren't doing this any more and instead loading directly onto the client
// Currently this style of requesting won't work until we change the editor
/*AUTO_RUN_LATE;
int MissionInitClientDictionary(void)
{
	// TODO: Set a limit at some point when the max mission stuff is better defined
	//		 Can we estimate an appropriate number due to submissions?
	RefSystem_SetDictionaryShouldRequestMissingData(g_MissionDictionary, 0, RefClient_RequestSendReferentCommand, NULL);
	return 1;
}*/

AUTO_RUN_ANON(memBudgetAddMapping(__FILE__, BUDGET_GameSystems););

static MissionInfo* GetActivePlayerMissionInfo(void)
{
	return mission_GetInfoFromPlayer(entActivePlayerPtr());
}

static float missiondebug_Display(Mission* toDisplay, int xOffset, float height)
{
	MissionDef* missionDef = mission_GetDef(toDisplay);
	int i, n = eaSize(&toDisplay->children);
	int r = ((toDisplay->state == MissionState_Failed)) * 0xFF;
	int g = ((toDisplay->state != MissionState_Failed) && (toDisplay->state != MissionState_Succeeded)) * 0xFF;
	int b = ((toDisplay->state == MissionState_Succeeded)) * 0xFF;
	int retHeight = height;
	char* state;
	if (toDisplay->state == MissionState_Succeeded)
		state = xOffset ? "Objective Complete" : "Mission Complete";
	else if (toDisplay->state == MissionState_Failed)
		state = xOffset ? "Objective Failed" : "Mission Failed";
	else
		state = "In Progress";

	if (missionDef && missionDef->name)
		gfxXYprintfColor(1 + xOffset, retHeight++, r, g, b, 0xFF, "%s: %s", missionDef->name, state);
	else
		gfxXYprintfColor(1 + xOffset, retHeight++, r, g, b, 0xFF, "%s", state);

	if (missionDef && GET_REF(missionDef->uiStringMsg.hMessage))
		gfxXYprintfColor(1 + xOffset, retHeight++, 0xFF, 0xFF, 0, 0xFF, "%s", TranslateMessageRef(missionDef->uiStringMsg.hMessage));

	if (missionDef && missionDef->uTimeout)
	{
		int timeLeft = missionDef->uTimeout + toDisplay->startTime - timeServerSecondsSince2000();
		gfxXYprintfColor(1 + xOffset, retHeight++, 0xFF, 0xFF, 0xFF, 0xFF, "TimeLeft: %i", max(timeLeft, 0));
	}
	/* This should somehow be sent debug only
	if (missionDef)
	{
		int numStats = eaSize(&missionDef->trackedEvents);
		for (i = 0; i < numStats; i++)
			gfxXYprintfColor(1 + xOffset, retHeight++, 0xFF, 0xFF, 0xFF, 0xFF, "%s: %i", missionDef->trackedEvents[i], 0);// Lookup val);
	}*/

	for (i = 0; i < n; i++)
		retHeight = missiondebug_Display(toDisplay->children[i], xOffset + 3, ++retHeight);
    return retHeight;
}

void missiondebug_DisplayAll(void)	
{
	float height = 1;
	MissionInfo* missionInfo = GetActivePlayerMissionInfo();
	if (missionInfo)
	{
		int i, n = eaSize(&missionInfo->missions);
		for (i = 0; i < n; i++)
			height = missiondebug_Display(missionInfo->missions[i], 0, height) + 1;
	}
}

#include "graphicslib.h"
#include "oldencounter_common.h"
#include "gfxprimitive.h"

static void encVert(Vec3 pos, F32 angle, Vec3 result, int off)
{
	Vec3 v = {0, 5, 0};
	v[0] = fcos(angle) * (off? 2.001: 2);
	v[2] = fsin(angle) * (off? 2.001: 2);
	addVec3(pos, v, result);
}

void encounterdebug_DrawBeacons(void)
{
	EncounterDebug* encDebug;
	Entity* playerEnt = entActivePlayerPtr();
	PlayerDebug* pDebug = playerEnt ? entGetPlayerDebug(playerEnt, false) : NULL;
	if (pDebug && (encDebug = pDebug->encDebug))
	{
		#define NUMSIDES 4
		U32 speed, currFrameCount = gfxGetFrameCount();
		int i, j, n = eaSize(&encDebug->eaEncBeacons);
		for (i = 0; i < n; i++)
		{
			F32 angle;
			Color color;			
			EncounterDebugBeacon* encBeacon = encDebug->eaEncBeacons[i];

			// Determine color and speed based off the current state of the encounter
			switch (encBeacon->eCurrState)
			{
				case EncounterState_Asleep: color = ColorHalfBlack; speed = 10; break;
				case EncounterState_Waiting: color = ColorHalfBlack; speed = 10; break;

				case EncounterState_Spawned: color = ColorWhite; speed = 20; break;
				case EncounterState_Active: color = ColorYellow; speed = 25; break;
				case EncounterState_Aware: color = ColorOrange; speed = 30; break;

				case EncounterState_Success: color = ColorGreen; speed = 0; break;
				case EncounterState_Failure: color = ColorRed; speed = 0; break;
				case EncounterState_Off: color = ColorBlue; speed = 0; break;
				
				default: assertmsg(0, "Programmer Error: Someone added a new state without defining the beacon colors");
			}

			// Draw diamond shaped encounter beacons for all nearby encounters
			angle = fixAngle(RAD(currFrameCount * speed / 5));
			for (j = 0; j < NUMSIDES; j++)
			{
				Vec3 p1, p2, top;

				encVert(encBeacon->vEncPos, angle + 2*PI/NUMSIDES*j, p1, 0);
				encVert(encBeacon->vEncPos, angle + 2*PI/NUMSIDES*(j+1), p2, 0);
				copyVec3(encBeacon->vEncPos, top);
				top[1] += 7;
				gfxDrawTriangle3D_3(encBeacon->vEncPos, p1, p2, ColorWhite, color, color);
				gfxDrawTriangle3D_3(top, p1, p2, ColorWhite, color, color);
			}
		}
	}
}

AUTO_COMMAND ACMD_CATEGORY(Interface, Social);
void mission_PrintWaypoints(void)
{
	int i, size;
	MissionInfo *info = mission_GetInfoFromPlayer(entActivePlayerPtr());

	// Print this so I know this was called
	printf("Printing waypoints for player\n");
	
	if (info)
	{
		// Get size of list
		size = eaSize(&info->waypointList);

		// Print all waypoints (hopefully)
		for (i = 0; i < size; ++i)
		{
			printf("Waypoint %d: %f %f %f\n", i, info->waypointList[i]->pos[0], info->waypointList[i]->pos[1], info->waypointList[i]->pos[2]);
		}
	}
	else
	{
		printf("Couldn't find mission info!");
	}
}

dtNode s_hTargetReticleNode = 0;
dtFx s_hTargetReticleFx = 0;

MissionDef *pFXSplatMission = NULL;

/*
AUTO_COMMAND ACMD_ACCESSLEVEL(0) ACMD_CLIENTCMD;
void mission_printMissionNameSplat(const char *pchMissionName)
{
	Entity *pPlayer = entActivePlayerPtr();
	MissionDef *pMissionDef = missiondef_DefFromRefString(pchMissionName);
	Vec3 vPlayerPos;
	Quat qPlayerRot;
	char achCustomSplatText[1024];

	if(!pPlayer)
		return;

	entGetPos(pPlayer,vPlayerPos);
	entGetRot(pPlayer,qPlayerRot);

	vPlayerPos[1] += 0.01;

	if(pMissionDef && pMissionDef->pchSplatFX)
	{
		DynFxManager *pManager = dynFxGetGlobalFxManager(vPlayerPos);

		DynParamBlock *pFxParams = dynParamBlockCreate(); 
		

		if(s_hTargetReticleNode)
		{
			dtNodeDestroy(s_hTargetReticleNode);
		}

		

		s_hTargetReticleNode = dtNodeCreate();

		dtNodeSetPos(s_hTargetReticleNode, vPlayerPos);
		dtNodeSetRot(s_hTargetReticleNode, qPlayerRot);

		s_hTargetReticleFx = dtAddFx(pManager->guid, 
			pMissionDef->pchSplatFX, 
			pFxParams, 0, s_hTargetReticleNode, 
			1.f, 0, NULL, eDynFxSource_UI);
	}
	else
	{
		if(pMissionDef)
			Errorf("Mission Splat requested with no mission splat fx specified: %s", pchMissionName);
		else
			Errorf("Mission splat requesd with no mission matching name %s", pchMissionName);
	}
}
*/

void playFXSplat(const char *splatFX, const char *pchCustomMessage)
{
	Entity *pPlayer = entActivePlayerPtr();
	Vec3 vPlayerPos;
	Quat qPlayerRot;
	DynFxManager *pManager = NULL;
	DynParamBlock *pFxParams = NULL;
	DynDefineParam *param = NULL;
	char achCustomSplatText[1024];

	if(!pPlayer || !splatFX)
		return;

	entGetPos(pPlayer,vPlayerPos);
	entGetRot(pPlayer,qPlayerRot);

	achCustomSplatText[0] = 0;
	
	if(pchCustomMessage && strlen(pchCustomMessage) > 0)
	{
		strcpy(achCustomSplatText,pchCustomMessage);
	}

	pFxParams = dynParamBlockCreate();

	if(s_hTargetReticleNode)
	{
		dtNodeDestroy(s_hTargetReticleNode);
	}

	param = StructCreate(parse_DynDefineParam);
	param->pcParamName = allocAddString("TextWordsParam");
	MultiValSetString(&param->mvVal, &achCustomSplatText[0]);
	eaPush(&pFxParams->eaDefineParams, param);

	s_hTargetReticleNode = dtNodeCreate();

	dtNodeSetPos(s_hTargetReticleNode, vPlayerPos);
	dtNodeSetRot(s_hTargetReticleNode, qPlayerRot);

	s_hTargetReticleFx = dtAddFx(pPlayer->dyn.guidFxMan, 
		splatFX, 
		pFxParams, 0, s_hTargetReticleNode, 
		1.f, 0, NULL, eDynFxSource_UI, NULL, NULL);
}

