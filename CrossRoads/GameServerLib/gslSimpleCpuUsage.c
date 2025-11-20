#include "gslSimpleCpuUsage.h"
#include "SimpleCpuUsage.h"

#include "EntityLib.h"
#include "Player.h"

AUTO_RUN_ANON(memBudgetAddMapping(__FILE__, BUDGET_GameSystems););

static ContainerID s_EntityPlayerContainerID_DesiringCapturedFrames = 0;

AUTO_COMMAND ACMD_SERVERCMD ACMD_NAME("ClearEntityDesiringCapturedFrames") ACMD_ACCESSLEVEL(3) ACMD_HIDE ACMD_PRIVATE;
void gslSimpleCpu_ClearEntityDesiringCapturedFrames()
{
	if(s_EntityPlayerContainerID_DesiringCapturedFrames)
	{
		Entity *pEntity = entFromContainerIDAnyPartition(GLOBALTYPE_ENTITYPLAYER, s_EntityPlayerContainerID_DesiringCapturedFrames);
		if(pEntity)
		{
			StructDestroySafe(parse_SimpleCpuData, &pEntity->pPlayer->pSimpleCpuData);

			pEntity->pPlayer->dirtyBit = 1;

			s_EntityPlayerContainerID_DesiringCapturedFrames = 0;
		}
	}

	simpleCpu_SetEnabled(false);
}

AUTO_COMMAND ACMD_SERVERCMD ACMD_NAME("SetEntityDesiringCapturedFrames") ACMD_ACCESSLEVEL(3) ACMD_HIDE ACMD_PRIVATE;
void gslSimpleCpu_SetEntityDesiringCapturedFrames(Entity *pEntity)
{
	if(pEntity && pEntity->myEntityType == GLOBALTYPE_ENTITYPLAYER)
	{
		gslSimpleCpu_ClearEntityDesiringCapturedFrames();

		s_EntityPlayerContainerID_DesiringCapturedFrames = entGetContainerID(pEntity);

		simpleCpu_SetEnabled(true);
	}
}

AUTO_COMMAND ACMD_SERVERCMD ACMD_NAME("PauseCapturing") ACMD_ACCESSLEVEL(3) ACMD_HIDE ACMD_PRIVATE;
void gslSimpleCpu_PauseCapturing()
{
	if(s_EntityPlayerContainerID_DesiringCapturedFrames)
		simpleCpu_SetEnabled(false);
}

AUTO_COMMAND ACMD_SERVERCMD ACMD_NAME("ResumeCapturing") ACMD_ACCESSLEVEL(3) ACMD_HIDE ACMD_PRIVATE;
void gslSimpleCpu_ResumeCapturing()
{
	if(s_EntityPlayerContainerID_DesiringCapturedFrames)
		simpleCpu_SetEnabled(true);
}

static void gslSimpleCpu_CaptureFramesForEntityPlayer(Entity *pEntity)
{
	SimpleCpuThreadData ***peaSimpleCpuThreadData;

	if(simpleCpu_IsEnabled())
	{
		if(!pEntity || !pEntity->pPlayer)
		{
			SimpleCpuThreadData **eaSimpleCpuThreadData = NULL;
			simpleCpu_CaptureFrames(&eaSimpleCpuThreadData);
			eaDestroy(&eaSimpleCpuThreadData);
		}
		else
		{
			if(!pEntity->pPlayer->pSimpleCpuData)
			{
				pEntity->pPlayer->pSimpleCpuData = StructCreate(parse_SimpleCpuData);
				eaSetSizeStruct(&pEntity->pPlayer->pSimpleCpuData->eaSimpleCpuFrameData, parse_SimpleCpuFrameData, SIMPLE_CPU_DATA_MAX_FRAME_COUNT);
			}

			peaSimpleCpuThreadData = &pEntity->pPlayer->pSimpleCpuData->eaSimpleCpuFrameData[pEntity->pPlayer->pSimpleCpuData->iNextFrameIndex]->eaSimpleCpuThreadData;
			eaDestroy(peaSimpleCpuThreadData);
			*peaSimpleCpuThreadData = NULL;

			simpleCpu_CaptureFrames(peaSimpleCpuThreadData);

			pEntity->pPlayer->pSimpleCpuData->iNextFrameIndex++;
			if(pEntity->pPlayer->pSimpleCpuData->iNextFrameIndex >= SIMPLE_CPU_DATA_MAX_FRAME_COUNT)
				pEntity->pPlayer->pSimpleCpuData->iNextFrameIndex = 0;

			pEntity->pPlayer->pSimpleCpuData->fMaxCyclesFor30Fps = timerCpuSpeed64() / 30;

			pEntity->pPlayer->dirtyBit = 1;
		}
	}
}

void gslSimpleCpu_CaptureFrames(void)
{
	Entity *pEntity = entFromContainerIDAnyPartition(GLOBALTYPE_ENTITYPLAYER, s_EntityPlayerContainerID_DesiringCapturedFrames);
	if(pEntity)
		gslSimpleCpu_CaptureFramesForEntityPlayer(pEntity);
}
