#include "aiCivilian.h"
#include "aiCivilianPrivate.h"
#include "aiCivMovement.h"
#include "aiCivilianTraffic.h"
#include "aiCivilianCar.h"
#include "aiCivilianPedestrian.h"
#include "aiCivilianTrolley.h"

#include "aiConfig.h"
#include "aiFCExprFunc.h"
#include "aiJobs.h"
#include "aiLib.h"
#include "aiMovement.h"
#include "aiStruct.h"
#include "aiStructCommon.h"
#include "aiTeam.h"

#include "EntitySystemInternal.h"
#include "Character.h"
#include "CostumeCommonEntity.h"
#include "GameEvent.h"
#include "gslCritter.h"
#include "gslHeatmaps.h"
#include "gslMapState.h"
#include "gslMechanics.h"
#include "gslInteractable.h"
#include "gslEntity.h"
#include "..\gslPartition.h"
#include "bounds.h"

#include "encounter_common.h"
#include "EntityIterator.h"
#include "EntityMovementManager.h"
#include "EntityGrid.h"
#include "EString.h"
#include "gslEventTracker.h"
#include "Expression.h"

#include "LineDist.h"
#include "MemoryPool.h"
#include "gslMission.h"
#include "Player.h"

#include "rand.h"

#include "StashTable.h"
#include "StateMachine.h"
#include "StringCache.h"

#include "wlEncounter.h"
#include "WorldColl.h"
#include "WorldGrid.h"
#include "WorldLib.h"
#include "wlEditorIncludes.h"
#include "TextParserSimpleInheritance.h"

#include "AILib_autogen_QueuedFuncs.h"
#include "aiCivilianPrivate_h_ast.h"
#include "AutoGen/GameClientLib_autogen_ClientCmdWrappers.h"

#include "cmdClientReport.h"

typedef struct AICivIntersectionManager AICivIntersectionManager;

// ---------------------------------------------------------------
extern EntityRef aiDebugEntRef;
extern EntityRef aider;

#define CIV_FILE_MAPDEF_SUFFIX			"civ_def"
#define CIV_FILE_ONCLICK_SUFFIX			"civclick"
#define CIV_FILE_EMOTEREACT_SUFFIX		"civemote"
#define CIV_DEFAULT_DISPLAYNAME_BASE	"civilian"

// consts and defines
#define LEG_SURFACEAREA_FACTOR	(1.0f/8.0f)
#define CIVILIAN_CHECKCOUNT_UPDATE_TIME	(1.0f)
#define CIVILIAN_DEBUGGING

#define PEDESTRIAN_DEFAULT_MIN_SPEED	5.0f
#define PEDESTRIAN_DEFAULT_SPEED_RANGE	5.6f

const S32 sWAYPOINTLIST_MAX_SIZE = 30;
const S32 sWAYPOINTLIST_TRUNCATE_AMOUNT = 15;

EAICivilianType g_eHeatMapSet = EAICivilianType_PERSON;

int g_bAICivVerbose = false;
AUTO_CMD_INT(g_bAICivVerbose, aiCivVerbose);

extern int acg_d_nodeGenerator;

F32 g_CivTickRates[EAICivilianType_COUNT] = 
			{	1.0f,		// Person
				0.1f,		// Car
				0.1f,		// Trolley
			};

// ---------------------------------------------------------------
// Structs
// ---------------------------------------------------------------

typedef struct BoundingBox
{
	Vec3	min;
	Vec3	max;
} BoundingBox;



// ---------------------------------------------------------------
// AICivilianWorldLegGrid
// A world partitioned grid for civlian legs, used for faster searching of legs 
// ---------------------------------------------------------------
typedef struct AICivClippedLegData
{
	AICivilianPathLeg	*leg;
	Vec3				start;
	Vec3				end;
	F32					area;

	S32					numProjectedCivilians;
} AICivClippedLegData;

// ---------------------------------------------------------------
typedef struct AICivWorldLegCellData
{
	AICivClippedLegData		*clippedLegs;
	S32					numClippedLegs;

	S32					numCivilians;
	S32					projectedNumCivilians;

	F32					leg_surface_area;
} AICivWorldLegCellData;

// ---------------------------------------------------------------
typedef struct AICivWorldLegGridCell
{
	AICivWorldLegCellData	data[EAICivilianType_COUNT];

} AICivWorldLegGridCell;


// ---------------------------------------------------------------
typedef struct AICivilianWorldLegGrid
{
	Vec3				world_min;
	Vec3				world_max;
	Vec3				vGridSize;

	U32					dimensionX;
	U32					dimensionZ;

	// grid is sorted by 0,0 is min,min.
	// dimensionX, dimensionZ is max,max
	AICivWorldLegGridCell*	grid;
	AICivilianPathLeg**	tmp_legs;

	bool				bUseActualNumCivs;

} AICivilianWorldLegGrid;

// ---------------------------------------------------------------
static AICivilianWorldLegGrid* aiCivilian_CreateAndInitWorldLegGrid(int iPartitionIdx, AICivilianRuntimePathInfo *pPathInfo);

static void aiCivilian_FreeWorldLegGrid(AICivilianWorldLegGrid** pWorldLegGrid);

static bool aiCivilian_GetSpawnPositionData(AICivilianPartitionState *pPartition, EAICivilianType type, AICivilianSpawningData *spawnData);
static void aiCivilianReportCivilianPosition(AICivilianWorldLegGrid *pWorldLegGrid, EAICivilianType type, const Vec3 prev_pos, const Vec3 cur_pos);
static void aiCivWorldLegGridResetCivilianGrid(AICivilianWorldLegGrid *pWorldLegGrid);
static void aiCivWorldLegGridInitSpawning(AICivilianWorldLegGrid *pWorldLegGrid);

static void aiCivSpawnVolumeRemoveRef(AICivilianSpawnVolume **ppSpawnVolume);
static AICivilianTypeDefRuntimeInfo* aiCiv_FindCivDefRuntimeInfo(AICivilianPartitionState *pPartition, EAICivilianType type, AICivilianDef *pDef);

// ---------------------------------------------------------------
// Civilian display name
// ---------------------------------------------------------------

// for the custom critter overrides
typedef struct AICivCritterDisplayName
{
	const char	*pszCritterDefName;		AST( STRING_POOLED )

	const char	**eaMaleNameMsgs;		AST( EARRAY_OF(STRING_POOLED) )
	const char	**eaFemaleNameMsgs;		AST( EARRAY_OF(STRING_POOLED) )

	// if the critter is a car, male names are used
	bool		isCar;

} AICivCritterDisplayName;

typedef struct AICivDisplayNameData
{
	StashTable	stashDisplayNames;		// keyed on the critter def name

	const char	**eaMaleNameMsgs;		AST( EARRAY_OF(STRING_POOLED) )
	const char	**eaFemaleNameMsgs;		AST( EARRAY_OF(STRING_POOLED) )
	const char	**eaCarNameMsgs;		AST( EARRAY_OF(STRING_POOLED) )

} AICivDisplayNameData;

static AICivDisplayNameData* aiCivDisplayNameData_CreateAndInit();
static void aiCivDisplayNameData_Free(AICivDisplayNameData **pData);
static Message* aiCivDisplayNameData_GetRandomName(const CritterDef *pCritterDef, EAICivilianType type, bool bFemale);

// ---------------------------------------------------------------
// global/static variables
// ---------------------------------------------------------------

static AICivilianMapDef					s_aiCivDefaultMapDef= {0};
static AICivilianDef					s_civDefDefault = {0};

AICivilianSharedState					g_civSharedState = {0};

static EntityRef						s_erEditingEntity = 0;

static struct
{
	EntityRef	lastCivRef;
	S32			lastWaypointIdx;
	S32			drawCivPath;
} s_debugDrawPath;

static AICivilianPartitionState			**s_eaCivilianPartitions = NULL;

MemoryPool MP_NAME(AICivilianWaypoint) = NULL;

MP_DEFINE(AICivilianPedestrian);
MP_DEFINE(AICivilianCar);
MP_DEFINE(AICivilianTrolley);


// ---------------------------------------------------------------
// functions
// ---------------------------------------------------------------
static void getWorldBoundsMinMax(int iPartitionIdx, Vec3 min, Vec3 max);
void aiCivReceivedEmoteEvent(void* structPtr, GameEvent *ev, GameEvent *specific, int value); // should be in a .h file

static AICivilianDef* aiCivDef_TypeFindByName(AICivilianMapDef *pCivMapDef, EAICivilianType type, const char *pszCivDef);
static AICivilianDef** aiCivDef_GetCivDefListForType(AICivilianMapDef *pCivMapDef, EAICivilianType type, S32 *piOutMaxWeight);
static S32 aiCivDef_ShouldDoStrictDistributionForType(AICivilianMapDef *pCivMapDef, EAICivilianType type);
//void aiCivEditing_ReportCivMapDefUnload();
static void aiCivEditing_ReportCivMapDefLoad();
static void aiCivilian_SetTypeRuntimeInfo(AICivilianPartitionState *pPartition, AICivilianMapDef *pCivMapDef);

static void aiCivilian_DestroyCivilianTypeRuntimeInfo(AICivilianPartitionState *pPartitionState);

static void aiCivEmoteReactDef_Destroy(AICivEmoteReactDef *emoteReactDef);
static bool aiCivEmoteReactDef_Validate(AICivEmoteReactDef *pDef, const char *pszFilename);
static void aiCivEmoteReactDef_Fixup(AICivEmoteReactDef *pDef);

void aiCivilian_LoadRuntimePathInfo(AICivilianRuntimePathInfo *pRTPathInfo, const AICivilianMapDef *pMapDef, const AICivilianPathInfo *pCivPathInfo); // should be in a .h file
static AIPedestrianOnClickDef* aiCivOnClickDef_LoadStaticDef(AICivilianSharedState *pSharedCivilianState);
static void aiCivilianHeatmapDumpLegsInGrid(AICivilianWorldLegGrid *pWorldLegGrid, gslHeatMapCBHandle *pHandle, char **ppErrorString, U32 x, U32 y);;
static void aiCivilian_InitializePOIManager(AICivilianPartitionState *pPartition);
static void aiCivilianFree(AICivilian *civ);

// ---------------------------------------------------------------
// Civilian Partitioning
// ---------------------------------------------------------------


// ------------------------------------------------------------------------------------------
AICivilianPartitionState* aiCivilian_GetPartitionState(int iPartitionIdx)
{
	return eaGet(&s_eaCivilianPartitions, iPartitionIdx);
}

// ------------------------------------------------------------------------------------------------------------------
AICivilianPartitionState* aiCivilian_GetAnyValidPartition()
{
	FOR_EACH_IN_EARRAY(s_eaCivilianPartitions, AICivilianPartitionState, pPartition)
	{
		if (pPartition)
		{
			return pPartition;
		}
	}
	FOR_EACH_END
	return NULL;
}

// ------------------------------------------------------------------------------------------------------------------
static int aiCivilian_InitializePartition(AICivilianPartitionState *pPartition)
{
	int i;
	if (!g_civSharedState.pMapDef || !g_civSharedState.pCivPathInfo || pPartition->pCivTeam)
		return false;
			
	// register for the emote events
	{
		pPartition->pEmoteGameEvent = StructCreate(parse_GameEvent);
		pPartition->pEmoteGameEvent->type = EventType_Emote;
		pPartition->pEmoteGameEvent->iPartitionIdx = pPartition->iPartitionIndex;
		eventtracker_StartTracking(pPartition->pEmoteGameEvent, NULL, pPartition->pEmoteGameEvent, 
									aiCivReceivedEmoteEvent, aiCivReceivedEmoteEvent);
	}
	
	// 
	pPartition->pCivTeam = aiTeamCreate(pPartition->iPartitionIndex, NULL, false);
	pPartition->pCivTeam->dontDestroy = 1;
	pPartition->pCivTeam->noUpdate = 1;
	pPartition->pCivTeam->collId = 0;

	aiCivilian_LoadRuntimePathInfo(&pPartition->pathInfo, g_civSharedState.pMapDef, g_civSharedState.pCivPathInfo);
		
	aiCivilian_SetTypeRuntimeInfo(pPartition, g_civSharedState.pMapDef);

	pPartition->pWorldLegGrid = aiCivilian_CreateAndInitWorldLegGrid(pPartition->iPartitionIndex, &pPartition->pathInfo);

	pPartition->pKillEventManager = aiCivPlayerKillEvents_Create(pPartition->iPartitionIndex);

	aiCivTraffic_Create(pPartition);

	aiCivilian_CalculateDesiredCivCounts(pPartition);
	
	aiCivilian_InitializePOIManager(pPartition);

	aiCivWanderArea_AddAllVolumesToPartition(pPartition);

	for(i=0; i<ARRAY_SIZE(pPartition->entBuckets); i++)
	{
		int buckets = ARRAY_SIZE(pPartition->entBuckets[0]);
		int j;
		for(j=0; j<buckets; j++)
		{
			pPartition->entBuckets[i][j].nextUpdate = 
				ABS_TIME_PARTITION(pPartition->iPartitionIndex)+
				SEC_TO_ABS_TIME(1.0*j/buckets);
		}
	}

	return true;
}

// ------------------------------------------------------------------------------------------------------------------
static void aiCivilian_ClearPartition(AICivilianPartitionState *pPartition)
{
	S32 i;
	if (!pPartition->pEmoteGameEvent)
	{	// if the pEmoteGameEvent isn't allocated, the partition is not initialized
		return; 
	}
	
	aiCivWanderArea_Shutdown(pPartition);
	
	g_civSharedState.clearingAllData = true;
	
	aiCivilian_ReleaseAllPOIUsers(pPartition);
	
	for(i = 0; i < ARRAY_SIZE(pPartition->civilians); i++)
		eaDestroyEx(&pPartition->civilians[i], aiCivilianFree);

	g_civSharedState.clearingAllData = false;
	
	aiCivPOIManager_Destroy(&pPartition->pPOIManager);
	
	aiCivTraffic_Destroy(pPartition);

	aiCivPlayerKillEvents_Destroy(&pPartition->pKillEventManager);

	aiCivilian_FreeWorldLegGrid(&pPartition->pWorldLegGrid );
	
	aiCivilian_DestroyCivilianTypeRuntimeInfo(pPartition);

	aiCivilian_DestroyRuntimePathInfo(&pPartition->pathInfo);

	// stop tracking the emote event
	eventtracker_StopTracking(pPartition->iPartitionIndex, pPartition->pEmoteGameEvent, pPartition->pEmoteGameEvent);
	StructDestroy(parse_GameEvent, pPartition->pEmoteGameEvent);
	pPartition->pEmoteGameEvent = NULL;
	// 

	eaDestroyEx(&pPartition->eaActivePopulationAreas, NULL);

	pPartition->pCivTeam->dontDestroy = 0;
	// skip explicitly destroying the team since once all the civilians are removed from it, it will destroy itself
	pPartition->pCivTeam = NULL;

	for(i=0; i<ARRAY_SIZE(pPartition->entBuckets); i++)
	{
		int j;
		for(j=0; j<ARRAY_SIZE(pPartition->entBuckets[0]); j++)
		{
			FOR_EACH_IN_EARRAY(pPartition->entBuckets[i][j].civilians, AICivilian, civ)
			{
				civ->entBucket = NULL;
			}
			FOR_EACH_END
			eaDestroy(&pPartition->entBuckets[i][j].civilians);
		}
	}
}

// ------------------------------------------------------------------------------------------------------------------
void aiCivilian_PartitionLoad(int iPartitionIdx, int bFullInit)
{
	AICivilianPartitionState *pPartition = NULL;

	PERFINFO_AUTO_START_FUNC();

	// Create stats is not present, otherwise reset stats
	pPartition = aiCivilian_GetPartitionState(iPartitionIdx);

	if (!pPartition)
	{
		pPartition = calloc(1, sizeof(AICivilianPartitionState));
		pPartition->iPartitionIndex = iPartitionIdx;
		eaSet(&s_eaCivilianPartitions, pPartition, iPartitionIdx);
	}

	if (bFullInit) 
	{
		aiCivilian_InitializePartition(pPartition);
	}

	PERFINFO_AUTO_STOP();
}

// ------------------------------------------------------------------------------------------
void aiCivilian_PartitionUnload(int iPartitionIdx)
{
	AICivilianPartitionState *pPartition = aiCivilian_GetPartitionState(iPartitionIdx);
	if (pPartition) 
	{
		aiCivilian_ClearPartition(pPartition);
		free(pPartition);
		eaSet(&s_eaCivilianPartitions, NULL, iPartitionIdx);
	}
}

// ------------------------------------------------------------------------------------------------------------------
static void aiCivilian_ClearPartitionWrapper(int iPartitionIdx)
{
	AICivilianPartitionState *pPartition = aiCivilian_GetPartitionState(iPartitionIdx);
	if (pPartition)
	{
		aiCivilian_ClearPartition(pPartition);
	}
}
void aiCivilian_ClearAllPartitionData()
{
	partition_ExecuteOnEachPartition(aiCivilian_ClearPartitionWrapper);
}

// ------------------------------------------------------------------------------------------------------------------
static void aiCivilian_LoadPartitionDataWrapper(int iPartitionIdx)
{
	AICivilianPartitionState *pPartition = aiCivilian_GetPartitionState(iPartitionIdx);
	if (pPartition)
	{
		aiCivilian_InitializePartition(pPartition);
	}
}

void aiCivilian_LoadAllPartitionData()
{
	partition_ExecuteOnEachPartition(aiCivilian_LoadPartitionDataWrapper);
}

// ------------------------------------------------------------------------------------------------------------------

// ------------------------------------------------------------------------------------------------------------------
int aiCivilian_CreateAndInitSharedData()
{
	if (g_civSharedState.bInit)
		return true;

	g_civSharedState.bInit = true;
	
	{
		ZeroStruct(&s_civDefDefault);
		s_civDefDefault.fSpeedMin = PEDESTRIAN_DEFAULT_MIN_SPEED;
		s_civDefDefault.fSpeedRange = PEDESTRIAN_DEFAULT_SPEED_RANGE;
	}


	MP_CREATE(AICivilianWaypoint, 1000);
	MP_CREATE(AICivilianPedestrian, 100);
	MP_CREATE(AICivilianCar, 100);
	MP_CREATE(AICivilianTrolley, 10);
	
	aiCivCarInitializeData();
	aiCivPedestrianInitializeData();
	aiCivTrolley_InitializeData();
	aiCivTraffic_InitStatic();
	return true;
}

// ------------------------------------------------------------------------------------------------------------------
void aiCivilian_DestroyStaticData()
{
	if (g_civSharedState.bInit)
	{
		eaDestroyEx(&g_civSharedState.eaSpawnVolumes, NULL);

		ZeroStruct(&g_civSharedState);
	}

	
}

// ------------------------------------------------------------------------------------------------------------------
static void aiCivilian_InitializePOIManager(AICivilianPartitionState *pPartition)
{
	GameInteractable **eaInteractables = interactable_GetAmbientJobInteractables();

	aiCivilian_ReleaseAllPOIUsers(pPartition);
	aiCivPOIManager_Destroy(&pPartition->pPOIManager);

	if (eaSize(&eaInteractables) > 0)
	{
		pPartition->pPOIManager = aiCivPOIManager_Create();

		FOR_EACH_IN_EARRAY(eaInteractables, GameInteractable, pInteractable)
		{
			if (!pInteractable->pAmbientJobProperties || pInteractable->pAmbientJobProperties->isForCivilians)
			{
				aiCivPOIManager_AddPOI(pPartition->iPartitionIndex, pPartition->pPOIManager, pInteractable);
			}
		}
		FOR_EACH_END
	}

}


// ------------------------------------------------------------------------------------------------------------------
static void aiCivilian_LoadPOIManagerWrapper(int iPartitionIdx)
{
	AICivilianPartitionState *pPartition = aiCivilian_GetPartitionState(iPartitionIdx);
	if (pPartition)
	{
		aiCivilian_InitializePOIManager(pPartition);
	}
}

// ------------------------------------------------------------------------------------------------------------------
static void aiCivilian_UnloadPOIManagerWrapper(int iPartitionIdx)
{
	AICivilianPartitionState *pPartition = aiCivilian_GetPartitionState(iPartitionIdx);
	if (pPartition)
	{
		aiCivilian_ReleaseAllPOIUsers(pPartition);
		aiCivPOIManager_Destroy(&pPartition->pPOIManager);
	}
}

// ------------------------------------------------------------------------------------------------------------------
void aiCivilian_MapLoad(ZoneMap *pZoneMap, bool bFullInit)
{
	if (!g_civSharedState.pCivPathInfo || !g_civSharedState.pMapDef)
	{
		aiCivilian_LoadCivLegsAndMapDef();
		g_civSharedState.pDisplayNameData = aiCivDisplayNameData_CreateAndInit();
		
	}	

	if (g_civSharedState.pCivPathInfo || g_civSharedState.pMapDef)
	{
		partition_ExecuteOnEachPartition(aiCivilian_LoadPOIManagerWrapper);
	}
}

// ------------------------------------------------------------------------------------------------------------------
void aiCivilian_MapUnload()
{
	aiCivilian_UnloadCivLegsAndMapDef();
	aiCivDisplayNameData_Free(&g_civSharedState.pDisplayNameData);
	
	partition_ExecuteOnEachPartition(aiCivilian_UnloadPOIManagerWrapper);
}

// ------------------------------------------------------------------------------------------------------------------
void aiCivilianWaypointFree(AICivilianWaypoint *wp)
{
	if (wp->pCrosswalkUser)
	{
		aiCivCrosswalk_ReleaseUser(&wp->pCrosswalkUser);
	}

	MP_FREE(AICivilianWaypoint, wp);
}

// ------------------------------------------------------------------------------------------------------------------
bool aiCivilian_ReportRequesterDestroyed(Entity *e, const MovementRequester *mr)
{
	if(!e || !e->pCritter || !e->pCritter->civInfo)
		return false;
	if (mr == e->pCritter->civInfo->requester)
	{
		e->pCritter->civInfo->requester = NULL;
		return true;
	}
	return false;
}

// ------------------------------------------------------------------------------------------------------------------
AICivilian* acgCivilianAlloc(EAICivilianType eType)
{
	AICivilian* ret;

	switch(eType)
	{
		xcase EAICivilianType_PERSON:
			ret = (AICivilian*)MP_ALLOC(AICivilianPedestrian);
			ret->fp = aiCivPedestrianGetFunctionSet();
		xcase EAICivilianType_CAR:
			ret = (AICivilian*)MP_ALLOC(AICivilianCar);
			ret->fp = aiCivCarGetFunctionSet();
		xcase EAICivilianType_TROLLEY:
			ret = (AICivilian*)MP_ALLOC(AICivilianTrolley);
			ret->fp = aiCivTrolley_GetFunctionSet();
		xdefault:
			assert(0);
	}
	
	return ret;
}

// ------------------------------------------------------------------------------------------------------------------
// this call is to defer the free'ing of the civilian's waypoints to ensure that the civilian's
// movement requester is done with the waypoints before we free them all
// returns true if the free was processed
static bool aiCivilian_DeferredFree(AICivilian *civ)
{
	if (ABS_TIME_SINCE_PARTITION(civ->iPartitionIdx, civ->lastUpdateTime) >= SEC_TO_ABS_TIME(0.5f))
	{
		eaDestroyEx(&civ->path.eaWaypoints, aiCivilianWaypointFree);
		eaDestroyEx(&civ->path.eaWaypointsToDelete, aiCivilianWaypointFree);

		switch(civ->type)
		{
			xcase EAICivilianType_PERSON:
				MP_FREE(AICivilianPedestrian, civ);
			xcase EAICivilianType_CAR:
				MP_FREE(AICivilianCar, civ);
			xcase EAICivilianType_TROLLEY:
				MP_FREE(AICivilianTrolley, civ);
		}
		return true;
	}

	return false;
}

// ------------------------------------------------------------------------------------------------------------------
static void aiCivilianFree(AICivilian *civ)
{
	Entity *ent = entFromEntityRefAnyPartition(civ->myEntRef);
	AICivilianPartitionState *pPartition = NULL;
	
	pPartition = aiCivilian_GetPartitionState(civ->iPartitionIdx);
	devassert(pPartition);

	if(ent)
	{
		if(entIsAlive(ent))
			entDie(ent, 0, 0, 0, NULL);
		ent->pCritter->civInfo = NULL;
		
	}

	if (civ->spawnVolume)
	{
		aiCivSpawnVolumeRemoveRef(&civ->spawnVolume);
	}

	if (civ->civDef)
	{
		AICivilianTypeDefRuntimeInfo *pTypeRTI = aiCiv_FindCivDefRuntimeInfo(pPartition, civ->type, civ->civDef);
		if (pTypeRTI)
			pTypeRTI->iCurrentCount--;
	}
	
	civ->fp->CivFree(civ);
	
	if (civ->pathInitialized == true)
	{
		aiCivilianReportCivilianPosition(pPartition->pWorldLegGrid, civ->type, civ->prevPos, NULL);
	}

	aiCivilianRemoveFromBucket(civ);

	eaDestroy(&civ->path.eaAddedWaypoints);
	
	if (ent) // assume the requester on the entity has already been released. 
		mrDestroy(&civ->requester);
	
	
	if (!g_civSharedState.clearingAllData)
	{
		// put this civilian on the list of civilians to free up later, 
		// so we give the requester time to be done with the waypoints
		civ->lastUpdateTime = ABS_TIME_PARTITION(civ->iPartitionIdx);
		eaPush(&g_civSharedState.eaDeferredCivilianFree, civ);
	}
	else
	{
		eaDestroyEx(&civ->path.eaWaypoints, aiCivilianWaypointFree);
		eaDestroyEx(&civ->path.eaWaypointsToDelete, aiCivilianWaypointFree);
	}

	
}

// ------------------------------------------------------------------------------------------------------------------
bool randomChance(F32 fPercentChance)
{
#define CHANCE_BASE	1000000.f
	F32 fRand = randomPositiveF32() * CHANCE_BASE;

	return (fRand <= fPercentChance * CHANCE_BASE);
}

// ------------------------------------------------------------------------------------------------------------------
void aiCiv_SendQueuedWaypoints(AICivilian *civ)
{
	if (civ->requester)
	{	
		mmAICivilianSendAdditionalWaypoints(civ->requester, civ->path.eaAddedWaypoints);
		eaClear(&civ->path.eaAddedWaypoints);
	}
}

// ------------------------------------------------------------------------------------------------------------------
void aiCivilianPauseMovement(AICivilian *civ, bool on)
{
	if (civ->bPaused != (U32)on)
	{
		civ->bPaused = on;
		mmAICivilianMovementSetPause(civ->requester, on);
	}
}

// ------------------------------------------------------------------------------------------------------------------
static AICivilianWaypoint* aiCivilianGetPreviousWaypoint(AICivilian *civ)
{
	if(civ->path.curWp-1>0 && civ->path.curWp-1<eaSize(&civ->path.eaWaypoints))
		return civ->path.eaWaypoints[civ->path.curWp-1];

	return NULL;
}

// ------------------------------------------------------------------------------------------------------------------
__forceinline static AICivilianWaypoint* aiCivilianGetPrevWaypoint(AICivilian *civ)
{
	if (civ->path.curWp - 1 >= 0)
	{
		return civ->path.eaWaypoints[civ->path.curWp - 1];
	}
	return NULL;
}

// ------------------------------------------------------------------------------------------------------------------
static AICivilianWaypoint* aiCivilianGetLastLegWaypoint(AICivilian *civ)
{
	int i = eaSize(&civ->path.eaWaypoints)-1;

	if(!civ->path.eaWaypoints)
		return NULL;

	while(!civ->path.eaWaypoints[i]->bIsLeg && i>0)
		i--;

	return civ->path.eaWaypoints[i];
}



static PathLegIntersect* aiCivilianACPIGetRandomPLI(AICivilianPathIntersection *acpi)
{
	return acpi->legIntersects[randomIntRange(0, eaSize(&acpi->legIntersects)-1)];
}


// ------------------------------------------------------------------------------------------------------------------
void aiCivilianMakeWpPos(const AICivilian *civ, const AICivilianPathLeg *leg, AICivilianWaypoint *wp, int end)
{
	F32  fDistFromLeg;
	const F32* pvLegPos;
	int forward = aiCivilianCalculateFutureForward(civ);

	if(wp->bReverse)
		forward = !forward;

			
	fDistFromLeg = civ->fp->CivInitNextWayPtDistFromLeg(civ, leg, wp, forward);
		// initializeNextWayPtDistFromLeg(civ, leg, wp, forward);
	pvLegPos = aiCivilianLegGetPos(leg, forward, end);
	
	// This is for cars only, really
	if((forward ^ end) && leg->bSkewed_Start)
	{	// we have a skewed line
		Vec3 vSkewedLaneDir;
		F32 fLength = acgLeg_GetSkewedLaneLength(leg);
		F32 fPercent = fDistFromLeg / (leg->width * .5f);
		F32 fOffset = fLength * fPercent;

		acgLeg_GetSkewedLaneDirection(leg, vSkewedLaneDir);
		
		scaleAddVec3(vSkewedLaneDir, fOffset, pvLegPos, wp->pos);
	}
	else
	{
		scaleAddVec3(leg->perp, fDistFromLeg, pvLegPos, wp->pos);
	}
}



// ------------------------------------------------------------------------------------------------------------------
// param: isectPt points to the intersection position
// returns true if the mid intersection is on the end of the current waypoint (and onto the middle of the next leg)
void aiCivMidIntersection_GetLegAndIsect(const AICivilianPathIntersection *mid_acpi, 
										 PathLegIntersect **intPli, 
										 PathLegIntersect **legPli)
{
	S32 i = 0;

	devassert( mid_acpi->bIsMidIntersection && eaSize(&mid_acpi->legIntersects) == MAX_PATHLEG_MID_INTERSECTIONS);

	*intPli = NULL;
	*legPli = NULL;

	do{
		PathLegIntersect *pli = mid_acpi->legIntersects[i];

		if(!(*intPli) && !vec3IsZero(pli->intersect))
		{
			*intPli = pli;
		}
		else
		{
			*legPli = pli;
		}

	} while(++i < MAX_PATHLEG_MID_INTERSECTIONS);

}


// ------------------------------------------------------------------------------------------------------------------
// given the possible paths we can take, filter them by a bias direction
// if none of the paths yield a good direction, then all of them are valid
static void civCarFilterByBias(const Vec3 vBiasDir,  // assumed normalized
								 const AICivilianPathLeg *pCurLeg,
								 S32 forward,
								 const AICivilianPathLeg **ppleg,
								 const AICivilianPathIntersection **eaMids,
								 const AICivilianPathIntersection **ppacpi)
{
	F32 fBestDirection = 0.0f;
	S32 reverse;
	const AICivilianPathLeg *bestLeg = NULL;
	const AICivilianPathIntersection *bestAcpi = NULL;

#define LEG_DOT_BIAS(ll)							\
		fDot = dotVec3((ll)->dir, vBiasDir);		\
		reverse = !calcLegFlow(pCurLeg, (ll));		\
		if ( !(reverse ^ forward) ) fDot = -fDot;	\

	if (*ppleg)
	{
		const AICivilianPathLeg *leg = *ppleg;
		F32 fDot;

		LEG_DOT_BIAS(leg);

		if (fDot > fBestDirection)
		{
			fBestDirection = fDot;
			bestLeg = leg;
		}
	}

	if (*ppacpi)
	{
		const AICivilianPathIntersection *acpi = *ppacpi;
		S32 x;

#define FIND_BEST_LEG_IN_INTERSECTION()						\
		x = 0;												\
		do {												\
			PathLegIntersect *pli = acpi->legIntersects[x];	\
			if (pli->continue_isvalid) {					\
				F32 fDot;									\
				LEG_DOT_BIAS(pli->leg);						\
				if (fDot > fBestDirection) {				\
					fBestDirection = fDot;					\
					bestAcpi = acpi;						\
					bestLeg = pli->leg;						\
				}											\
			}												\
		} while(++x < eaSize(&acpi->legIntersects))			\

		FIND_BEST_LEG_IN_INTERSECTION();
	}

	if (eaMids && eaSize(&eaMids))
	{
		S32 i;

		for(i = 0; i < eaSize(&eaMids); i++)
		{
			const AICivilianPathIntersection *acpi = eaMids[i];
			S32 x;

			FIND_BEST_LEG_IN_INTERSECTION();

		}

	}


	//
	if (bestAcpi)
	{
		if (bestAcpi == *ppacpi)
		{
			// clear all the other possible places we can go
			const AICivilianPathIntersection *acpi = *ppacpi;
			S32 x;

			eaClear(&eaMids);
			*ppleg = NULL;

			for(x = 0; x < eaSize(&acpi->legIntersects); x++)
			{
				PathLegIntersect *pli = acpi->legIntersects[x];
				if (pli->leg != bestLeg)
				{
					pli->continue_isvalid = false;
				}
			}
		}
		else
		{
			S32 x;
			// best intersection is one of the mid intersections
			*ppacpi = NULL;
			*ppleg = NULL;
			// remove the mid intersections that aren't our best
			for (x = eaSize(&eaMids) - 1; x >= 0; --x)
			{
				if (eaMids[x] != bestAcpi)
				{
					eaRemoveFast(&eaMids, x);
				}
			}
		}
	}
	else if (bestLeg)
	{
		eaClear(&eaMids);
		*ppacpi = NULL;
	}

}

// ------------------------------------------------------------------------------------------------------------------

// from our available choices, choose a random leg or intersection to traverse
void civContinuePath_GetRandomPath(const AICivPathSelectionInput *pInput, AICivPathSelectionOutput *pOutput)
{
	S32 count = (pInput->pLeg ? 1 : 0) + (pInput->pAcpi ? 1 : 0) + eaSize(pInput->eaMids);
	S32 i = randomIntRange(0, count - 1);

	pOutput->pAcpi = NULL;
	pOutput->pLeg = NULL;

	if(pInput->pLeg)
	{
		if(i==0)
		{
			pOutput->pLeg = pInput->pLeg;
			return;
		}
		i--;
	}

	if(pInput->pAcpi)
	{
		if(i==0)
		{
			pOutput->pAcpi = pInput->pAcpi;
			return;
		}
		i--;
	}

	devassert( eaSize(pInput->eaMids) > 0 );
	pOutput->pAcpi = eaGet(pInput->eaMids, i);
}





#if defined(CIVILIAN_DEBUGGING)
// ------------------------------------------------------------------------------------------------------------------
static void aiCivDebugDrawPath(AICivilian *civ)
{
	Entity *debugger;

	if (!civ->path.eaWaypoints)
		return;

	debugger = entFromEntityRefAnyPartition(aiCivDebugRef);
	if (debugger)
	{
		S32 i;
		AICivilianWaypoint *wp;

		Vec3 vTmpPos1, vTmpPos2;

		if (s_debugDrawPath.lastCivRef != civ->myEntRef)
		{
			Entity *ent = entFromEntityRefAnyPartition(civ->myEntRef);

			if (!ent)
				return;

			entGetPos(ent, vTmpPos1);

			// debugging a new civilian, draw the path from their position
			wp = civ->path.eaWaypoints[civ->path.curWp];
			copyVec3(wp->pos, vTmpPos2);

			vTmpPos1[1] += 2.5f;
			vTmpPos2[1] += 2.5f;
			wlAddClientLine(debugger, vTmpPos1, vTmpPos2, 0xFFFF0000);
		}


		wp = civ->path.eaWaypoints[civ->path.curWp];
		copyVec3(wp->pos, vTmpPos1);
		vTmpPos1[1] += 2.5f;


		for (i = civ->path.curWp + 1; i < eaSize(&civ->path.eaWaypoints); i++)
		{
			wp = civ->path.eaWaypoints[i];

			copyVec3(wp->pos, vTmpPos2);
			vTmpPos2[1] += 2.5f;

			if (wp->bWasStop)
			{
				wlAddClientLine(debugger, vTmpPos1, vTmpPos2, 0xFF00FF00);
			}
			else
			{
				wlAddClientLine(debugger, vTmpPos1, vTmpPos2, 0xFFFF0000);
			}
			

			copyVec3(vTmpPos2, vTmpPos1);
		}

	}

	s_debugDrawPath.lastCivRef = civ->myEntRef;
}
#endif

// ------------------------------------------------------------------------------------------------------------------
void aiCivilianPruneOldWaypoints(AICivilian *civ)
{
	// Clean up some but keep last 20 or so for debugging purposes
	if(eaSize(&civ->path.eaWaypoints) > sWAYPOINTLIST_MAX_SIZE)
	{
		S32 i;
		S32 numRemove = eaSize(&civ->path.eaWaypoints) - sWAYPOINTLIST_TRUNCATE_AMOUNT;

		if (numRemove <= civ->path.curWp)
		{
			ANALYSIS_ASSUME(civ->path.eaWaypoints);
			// these are stale waypoints, the background thread should never be using these
			for(i = 0; i < numRemove; i++)
				aiCivilianWaypointFree(civ->path.eaWaypoints[i]);
			eaRemoveRange(&civ->path.eaWaypoints, 0, numRemove);
			civ->path.curWp -= numRemove;
		}
	}
}

// ------------------------------------------------------------------------------------------------------------------
bool aiCivilianContinuePath(AICivilian *civ)
{
	bool bRet;

	aiCivilianPruneOldWaypoints(civ);

	if (eaSize(&civ->path.eaWaypoints) > 0) {
		bRet = civ->fp->CivContinuePath(civ);
	} else {
		bRet = false;
	}


#if defined(CIVILIAN_DEBUGGING)
	// debug drawing path
	if (s_debugDrawPath.drawCivPath && civ->myEntRef == aiDebugEntRef && aiCivDebugRef)
	{
		aiCivDebugDrawPath(civ);
	}
#endif

	return bRet;

}

// ------------------------------------------------------------------------------------------------------------------
static AICivilianSpawnVolume* aiCivGetSpawnVolume(WorldVolumeEntry *volumeEntry)
{
	U64 civVolumeID = (U64)volumeEntry->server_volume.civilian_volume_properties;
	
	FOR_EACH_IN_EARRAY(g_civSharedState.eaSpawnVolumes, AICivilianSpawnVolume, volume)
	{
		if (volume->volumePropertyID == civVolumeID)
		{
			return volume;
		}
	}
	FOR_EACH_END

	{
		Vec3 bboxMin, bboxMax;
		AICivilianSpawnVolume *newVolume = calloc(1, sizeof(AICivilianSpawnVolume));

		const WorldCellEntryBounds *bounds = &volumeEntry->base_entry.bounds;
		const WorldCellEntrySharedBounds *sharedBounds = volumeEntry->base_entry.shared_bounds;

		// get the transformed min/max for the bounding box
		mulVecMat4(sharedBounds->local_min, bounds->world_matrix, bboxMin);
		mulVecMat4(sharedBounds->local_max, bounds->world_matrix, bboxMax);

		newVolume->volumePropertyID = civVolumeID;
		vec3MinMax(bboxMin, bboxMax, newVolume->min, newVolume->max);

		interpVec3(0.5f, newVolume->min, newVolume->max, newVolume->vMid);

		eaPush(&g_civSharedState.eaSpawnVolumes, newVolume);
		return newVolume;
	}
}

// ------------------------------------------------------------------------------------------------------------------
static void aiCivSpawnVolumeAddRef(AICivilianSpawnVolume *spawnVolume)
{
	spawnVolume->refCount++;
}

// ------------------------------------------------------------------------------------------------------------------
static void aiCivSpawnVolumeRemoveRef(AICivilianSpawnVolume **ppSpawnVolume)
{
	if (*ppSpawnVolume)
	{
		AICivilianSpawnVolume *spawnVolume = *ppSpawnVolume;

		*ppSpawnVolume = NULL;

		spawnVolume->refCount--;
		if (spawnVolume->refCount <= 0)
		{
#if defined(CIVILIAN_SPAWN_VOLUME_PARANOID)
			S32 i;
			for(i=0; i < ARRAY_SIZE(g_aiCivilianState.civilians); i++)
			{
				S32 x;
				for (x = 0; x < eaSize(&g_aiCivilianState.civilians[i]); x++)
				{
					if (g_aiCivilianState.civilians[i][x])
					{
						devassert(g_aiCivilianState.civilians[i][x]->spawnVolume != spawnVolume);
					}
				}
			}
#endif
			eaFindAndRemove(&g_civSharedState.eaSpawnVolumes, spawnVolume);
			free(spawnVolume);
		}
	}
}

// ------------------------------------------------------------------------------------------------------------------
// gets a civilian definition to spawn
static AICivilianDef* aiCivilian_GetCivDef(AICivilianPartitionState *pPartition, const Vec3 vSpawnPos, EAICivilianType type, AICivilianSpawnVolume **pSpawnVolume)
{
	S32 x;
	Vec3 localpos, tmpPos;

	devassert(type > EAICivilianType_NULL && type < EAICivilianType_COUNT);
	*pSpawnVolume = NULL;

	// check if there are any civilian volumes, and see if our spawn position lies within one of the volumes
	// if it does, we'll use the civilian def if we match the needed type
	
	for (x = 0; x < eaSize(&g_civSharedState.eaCivilianVolumes); x++)
	{
		WorldVolumeEntry *entry = g_civSharedState.eaCivilianVolumes[x];
		WorldCivilianVolumeProperties *civVolume = entry->server_volume.civilian_volume_properties;

		devassert(civVolume);
		
		if (((type == EAICivilianType_PERSON) && civVolume->pedestrian_total_weight > 0) ||
			((type == EAICivilianType_CAR) && civVolume->car_total_weight > 0) )
		{
			subVec3(vSpawnPos, entry->base_entry.bounds.world_matrix[3], tmpPos);
			mulVecMat3Transpose(tmpPos, entry->base_entry.bounds.world_matrix, localpos);

			if (pointBoxCollision(localpos, entry->base_entry.shared_bounds->local_min, entry->base_entry.shared_bounds->local_max))
			{
				// we are spawning within this volume
				F32 randomWeight, weightSum = 0.0f;
				S32 i;
				CivilianCritterSpawn *pChosenSpawn = NULL;
				
				// choose a random civilian def
				if (type == EAICivilianType_PERSON)
				{
					randomWeight = civVolume->pedestrian_total_weight;
				}
				else
				{
					randomWeight = civVolume->car_total_weight;
				}
				randomWeight *= randomPositiveF32();
				
				for (i = 0; i < eaSize(&civVolume->critter_spawns); i++)
				{
					CivilianCritterSpawn *pSpawn = civVolume->critter_spawns[i];
					
					if(type == EAICivilianType_PERSON)
					{
						if (pSpawn->is_car) 
							continue;
					}
					else
					{
						if (! pSpawn->is_car)
							continue;
					}

					weightSum += pSpawn->spawn_weight;
					if (randomWeight <= weightSum)
					{
						pChosenSpawn = pSpawn;
						break;
					}
				}

				if (pChosenSpawn)
				{
					AICivilianDef *pCivDef;

					pCivDef = aiCivDef_TypeFindByName(g_civSharedState.pMapDef, type, pChosenSpawn->critter_name);
					if (!pCivDef)
						break;

					if (pChosenSpawn->restricted_to_volume)
						(*pSpawnVolume) = aiCivGetSpawnVolume(entry);
					
					return pCivDef;
				}
			}
		}
	}

	// if we get here, there was no civilian volume that overrode the critter def
	{
		AICivilianDef **eaDefList;
		S32 iRandMaxWeight = 0;
		
		eaDefList = aiCivDef_GetCivDefListForType(g_civSharedState.pMapDef, type, &iRandMaxWeight);
		if (eaSize(&eaDefList) == 0 || !iRandMaxWeight)
			return NULL;

		if (aiCivDef_ShouldDoStrictDistributionForType(g_civSharedState.pMapDef, type))
		{
			F32 highestDelta = -FLT_MAX; 
			AICivilianDef *pBestDef = NULL;
			AICivilianTypeRuntimeInfo *pRTI = &pPartition->civTypeRuntimeInfo[type];
			
			S32 numCivs = eaSize(&pPartition->civilians[type]);
			if (numCivs == 0)
				numCivs = 1;

			FOR_EACH_IN_EARRAY(pRTI->eaCivilianTypes, AICivilianTypeDefRuntimeInfo, pTypeRTI)
			{
				AICivilianDef *pCivDef = aiCivDef_TypeFindByName(g_civSharedState.pMapDef, type, pTypeRTI->pchCivDefName);
					
				if (pCivDef)
				{
					F32 currentPercent = (F32)pTypeRTI->iCurrentCount / (F32)numCivs;
					F32 delta = pCivDef->fTargetDistributionPercent - currentPercent;

					if (delta > highestDelta)
					{
						pBestDef = pCivDef;
						highestDelta = delta;
					}
				}
			}
			FOR_EACH_END


			return pBestDef;
		}
		else
		{
			S32 sum;
			S32 randomWeight = randomIntRange(0, iRandMaxWeight - 1);

			sum = 0;
			FOR_EACH_IN_EARRAY_FORWARDS(eaDefList, AICivilianDef, pDef)
				sum += pDef->iSpawnChanceWeight;	
				if (randomWeight < sum)
				{
					return pDef;
				}
			FOR_EACH_END
		}

		return NULL;
	}
}

// ------------------------------------------------------------------------------------------------------------------
static void aiCiv_InitializePosAndPathViaLeg(Entity *e, AICivilian *civ, AICivilianSpawningData *spawnData)
{
	AICivilianWaypoint *wp = NULL;

	civ->forward = spawnData->forward;
	
	wp = aiCivilianWaypointAlloc();
	wp->leg = spawnData->leg;
	wp->bIsLeg = 1;
	wp->medianDist = spawnData->leg->median_width;

	switch (civ->type)
	{
		xcase EAICivilianType_PERSON:
			aiCivPedestrian_InitWaypointDefault((AICivilianPedestrian*)civ, wp);
		xcase EAICivilianType_CAR:
			aiCivCar_InitWaypointDefault((AICivilianCar*)civ, wp);
		xdefault:
			devassert(0);
	}
	
	aiCiv_AddNewWaypointToPath(&civ->path, wp);
	
	// initialize the civilian's spawn position
	{
		Vec3 vInitialPosition;
		F32 fDistFromLeg;
		AICivilianPartitionState *pPartition = aiCivilian_GetPartitionState(civ->iPartitionIdx);
		devassert(pPartition);

		fDistFromLeg = civ->fp->CivGetDesiredDistFromLeg(civ, wp, civ->forward);

		scaleAddVec3(spawnData->leg->perp, fDistFromLeg, spawnData->pos, vInitialPosition);
		entSetPos(e, vInitialPosition, 1, "Spawn");
		copyVec3(vInitialPosition, civ->prevPos);

		aiCivilianReportCivilianPosition(pPartition->pWorldLegGrid, civ->type, NULL, vInitialPosition);
	}
	
	aiCivilianMakeWpPos(civ, spawnData->leg, wp, 1);
}

// ------------------------------------------------------------------------------------------------------------------
static void aiCiv_InitializePosAndPathViaPathPoint(Entity *e, AICivilian *civ, AICivilianSpawningData *spawnData)
{
	AICivilianWaypoint *wp = NULL;
	AICivilianPathPoint *pNextPathPoint = NULL;
	
	devassert(spawnData->pPathPoint);
	pNextPathPoint = spawnData->pPathPoint->pNextPathPoint;
	if (!pNextPathPoint)
		pNextPathPoint = spawnData->pPathPoint;

	wp = aiCivilianWaypointAlloc();
	wp->pPathPoint = pNextPathPoint;
	copyVec3(pNextPathPoint->vPos, wp->pos);

	// redundant 
	aiCiv_AddNewWaypointToPath(&civ->path, wp);
	
	// redundant 
	entSetPos(e, spawnData->pPathPoint->vPos, 1, "Spawn");
	copyVec3(spawnData->pPathPoint->vPos, civ->prevPos);
}


// ------------------------------------------------------------------------------------------------------------------
void aiCiv_GetCurrentWayPointDirection(Entity *e, AICivilian *pCiv, Vec3 vDir)
{
	AICivilianWaypoint *curWp = aiCivilianGetCurrentWaypoint(pCiv);
	AICivilianWaypoint *nextWp = aiCivilianGetNextWaypoint(pCiv);

	if (curWp && nextWp)
	{
		subVec3(nextWp->pos, curWp->pos, vDir);
		normalVec3(vDir);
	}
	else
	{
		Vec2 v2PitchYaw;
		if (!e)
		{
			e = entFromEntityRefAnyPartition(pCiv->myEntRef);
			if (!e)
			{
				copyVec3(forwardvec, vDir);
				return;
			}
		}

		entGetFacePY(e, v2PitchYaw);
		sincosf(v2PitchYaw[1], &vDir[0], &vDir[2]);
		vDir[1] = 0.0f;
	}
}

// ------------------------------------------------------------------------------------------------------------------
static void aiCivInitializePosAndPath(Entity *e, AICivilian *civ, AICivilianSpawningData *spawnData)
{
	switch(civ->type)
	{
		case EAICivilianType_PERSON:
		case EAICivilianType_CAR:
			aiCiv_InitializePosAndPathViaLeg(e, civ, spawnData);

		xcase EAICivilianType_TROLLEY:
			aiCiv_InitializePosAndPathViaPathPoint(e, civ, spawnData);

		xdefault:
			devassert(0);
	}

	// one future step so we have more than one waypoint
	aiCivilianContinuePath(civ);
	
	{
		Vec3 vDir;
		Quat qRot;
		Vec3 pyr;

		aiCiv_GetCurrentWayPointDirection(e, civ, vDir);

		pyr[0] = getVec3Pitch(vDir);
		pyr[1] = getVec3Yaw(vDir);
		pyr[2] = 0.f;
				
		PYRToQuat(pyr, qRot);

		entSetRot(e, qRot,  false, "Spawn" );
		entSetFacePY(e, pyr, "Spawn" );
	}
	

	aiCiv_SendQueuedWaypoints(civ);
	
	civ->pathInitialized = true;
	return;
}

// ------------------------------------------------------------------------------------------------------------------
static Entity* aiCivCreate(AICivilianPartitionState *pPartition, EAICivilianType type)
{
	// Create us a new 'un!
	Entity *e = NULL;

	devassert(type > EAICivilianType_NULL && type < EAICivilianType_COUNT);

	{
		AICivilianDef*	pCivDef;
		const char *pszCritterDef = NULL;
		AICivilianSpawningData spawnData = {0};
		AICivilianSpawnVolume *spawnVolume = NULL;

		if (! aiCivilian_GetSpawnPositionData(pPartition, type, &spawnData))
			return NULL;
		
		pCivDef = aiCivilian_GetCivDef(pPartition, spawnData.pos, type, &spawnVolume);
		if (! pCivDef || !pCivDef->pchCritterDef)
			return NULL;
		
		e = critter_Create(pCivDef->pchCritterDef, NULL, GLOBALTYPE_ENTITYCRITTER, pPartition->iPartitionIndex, "DoNothing", 1, 1, 0, NULL, NULL, NULL, NULL, NULL, 0, pPartition->pCivTeam, NULL);
		if(e)
		{
			const CritterDef* pCritterDef = GET_REF(e->pCritter->critterDef);
			AICivilian *civ = acgCivilianAlloc(type);
			
			civ->iPartitionIdx = pPartition->iPartitionIndex;

			{
				AICivilianTypeDefRuntimeInfo *ptypeRTI = aiCiv_FindCivDefRuntimeInfo(pPartition, type, pCivDef);
				if (ptypeRTI)
					ptypeRTI->iCurrentCount++;
			}
			
			e->aibase->disableAmbientTether = true;
						
			// add the civilian to the list
			eaPush(&pPartition->civilians[type], civ);

			e->pCritter->civInfo = civ;
			switch (type)
			{
				//xcase EAICivilianType_PERSON:
					//e->pCritter->eCritterSubType = CritterSubType_CIVILIAN_PEDESTRIAN;
				xcase EAICivilianType_CAR:
					e->pCritter->eCritterSubType = CritterSubType_CIVILIAN_CAR;
				//xcase EAICivilianType_TROLLEY:
					//e->pCritter->eCriterSubType = CritterSubType_CIVILIAN_TROLLEY;
			}
			
			civ->type = type;
			civ->civDef = pCivDef;
			civ->myEntRef = e->myRef;
			
			// if the critter def has a the civilian behavior flag set, they have already been given a random name
			// otherwise, we override the display name
			if (! pCritterDef->bRandomCivilianName)
			{
				Message *pOverrideName = NULL;

				pOverrideName = aiCivDisplayNameData_GetRandomName(pCritterDef, civ->type, Gender_Female == costumeEntity_GetEffectiveCostumeGender(e));
				critter_OverrideDisplayMessage(e, pOverrideName, 0);
			}
			
			if (spawnVolume)
			{
				civ->spawnVolume = spawnVolume;
				aiCivSpawnVolumeAddRef(civ->spawnVolume);
			}

			// PROBABLY NOT NEEDED FOR TROLLEY, maybe not even for cars?
			// this is needed to counter-act the decrement of this variable in aiCivilianBeCheap. 
			pPartition->iNormalCivilianCount++;	
			
			aiCivilianBeCheap(e, civ);
			
			entSetCodeFlagBits(e, ENTITYFLAG_CIVILIAN);


			civ->fp->CivInitialize(e, civ, &spawnData);
			
			mmAICivilianInitMovement(civ->requester, civ->type);

			if (e->pChar && e->pChar->pattrBasic && e->pChar->pattrBasic->fSpeedRunning)
				mmAICivilianMovementSetCritterMoveSpeed(e->pCritter->civInfo->requester, e->pChar->pattrBasic->fSpeedRunning);
				
			// initialize our path and position
			aiCivInitializePosAndPath(e, civ, &spawnData);

			return e;
		}
	}

	return NULL;
}

// ------------------------------------------------------------------------------------------------------------------
// go through the list of civilians that are to be free'd 
static void aiCivDoDeferredFree()
{
	S32 i;
	for (i = eaSize(&g_civSharedState.eaDeferredCivilianFree) - 1; i >= 0; --i)
	{
		AICivilian* pCiv = g_civSharedState.eaDeferredCivilianFree[i];
		if (aiCivilian_DeferredFree(pCiv))
		{
			eaRemoveFast(&g_civSharedState.eaDeferredCivilianFree, i);
		}
	}

}

// ------------------------------------------------------------------------------------------------------------------
static void aiCivilian_EnableAllLegsInArea(AICivilianPartitionState *pPartition, const Vec3 vWorldMin, const Vec3 vWorldMax)
{
	S32 i;
	bool bEnabled = false;

	for (i = 0; i < EAICivilianLegType_COUNT; ++i)
	{
		FOR_EACH_IN_EARRAY(pPartition->pathInfo.legs[i], AICivilianPathLeg, pLeg)
		{
			if (pLeg->bSpawningDisabled)
			{
				// for now, we are just naively checking if the start point of the leg is in the bounding box, 
				if (pointBoxCollision(pLeg->start, vWorldMin, vWorldMax))
				{
					pLeg->bSpawningDisabled = false;
					bEnabled = true;
				}
			}
		}
		FOR_EACH_END
	}

	if (bEnabled)
		aiCivilian_CalculateDesiredCivCounts(pPartition);
}

// ------------------------------------------------------------------------------------------------------------------
static void aiCivilian_CheckActiveAreas(AICivilianPartitionState *pPartition)
{
	if (g_civSharedState.pMapDef && g_civSharedState.pMapDef->bEnableLegsWhenPlayerEntersRegion && 
		ABS_TIME_SINCE_PARTITION(pPartition->iPartitionIndex, pPartition->iLastActiveAreaCheck) > SEC_TO_ABS_TIME(1.f))
	{
		// regions
		WorldRegion **eaWorldRegions = worldGetAllWorldRegions();

		pPartition->iLastActiveAreaCheck = ABS_TIME_PARTITION(pPartition->iPartitionIndex);

		FOR_EACH_IN_EARRAY(eaWorldRegions, WorldRegion, pRegion)
		{
			bool bActive = false;
			FOR_EACH_IN_EARRAY(pPartition->eaActivePopulationAreas, AICivilianActiveAreas, pActiveArea)
				// pooled string compare
				if ((pActiveArea->pchRegionName && pActiveArea->pchRegionName == pRegion->name) || 
					pActiveArea->pRegion == pRegion)
				{
					bActive = true;
					break;
				}
			FOR_EACH_END

			if (!bActive)
			{	// not active, see if any players are in this region. 
				EntityIterator *entIter;
				Entity *pCurrEnt;
				Vec3 vRegionMin, vRegionMax;
				bool bInRegion = false;
				if(worldRegionGetBounds(pRegion, vRegionMin, vRegionMax))
				{
					entIter = entGetIteratorSingleType(pPartition->iPartitionIndex, 0, 0, GLOBALTYPE_ENTITYPLAYER);
					while(pCurrEnt = EntityIteratorGetNext(entIter))
					{
						Vec3 vEntPos;
						entGetPos(pCurrEnt, vEntPos);
						if (pointBoxCollision(vEntPos, vRegionMin, vRegionMax))
						{
							bInRegion = true;
							break;
						}
					}
					EntityIteratorRelease(entIter);
				}

				if (bInRegion)
				{
					AICivilianActiveAreas *pActiveArea = calloc(1, sizeof(AICivilianActiveAreas));
					pActiveArea->pchRegionName = pRegion->name;
					pActiveArea->pRegion = pRegion;
					eaPush(&pPartition->eaActivePopulationAreas, pActiveArea);

					aiCivilian_EnableAllLegsInArea(pPartition, vRegionMin, vRegionMax);
				}
			}
		}
		FOR_EACH_END
		
	}
}

// ------------------------------------------------------------------------------------------------------------------
static void aiCivilian_CheckCounts(AICivilianPartitionState *pPartition)
{
	S32 type;
	bool bAtCapacity = true;
	S64 timeSince;
	if(g_civSharedState.bIsDisabled)
		return;

	PERFINFO_AUTO_START_FUNC();

	timeSince = ABS_TIME_SINCE_PARTITION(pPartition->iPartitionIndex, g_civSharedState.lastDeadCivUpdateTime);
	if (timeSince < 0 || timeSince > SEC_TO_ABS_TIME(CIVILIAN_CHECKCOUNT_UPDATE_TIME))
	{
		g_civSharedState.lastDeadCivUpdateTime = ABS_TIME_PARTITION(pPartition->iPartitionIndex);

		for(type = 0; type < EAICivilianType_COUNT; type++)
		{
			S32 j;
			for (j = eaSize(&pPartition->civilians[type]) -1; j >= 0; --j)
			{
				AICivilian *civ = pPartition->civilians[type][j];
				Entity *e = entFromEntityRef(pPartition->iPartitionIndex, civ->myEntRef);

				if(!e)
				{
					eaRemoveFast(&pPartition->civilians[type], j);
					aiCivilianFree(civ);
				}
				else if(j >= pPartition->civTypeRuntimeInfo[type].targetCount && entIsAlive(e))
				{
					entDie(e, 0, 0, 0, NULL);
				}
			}

		}
	}
	
	
	for(type = 0; type < EAICivilianType_COUNT; type++)
	{
		switch (type)
		{
			xcase EAICivilianType_PERSON:
				if (eaSize(&pPartition->pathInfo.legs[type]) == 0)
					continue;
			xcase EAICivilianType_CAR:
				if (eaSize(&pPartition->pathInfo.legs[type]) == 0 || g_civSharedState.bCarsDisabled)
					continue;
			xcase EAICivilianType_TROLLEY:
				if(eaSize(&pPartition->pathInfo.eaPathPoints) == 0)
					continue;

		}
		
		if( eaSize(&pPartition->civilians[type]) < pPartition->civTypeRuntimeInfo[type].targetCount)
		{
			Entity *e = NULL;
			
			e = aiCivCreate(pPartition, type);

			aiCivilianAddToBucket(e->pCritter->civInfo);

			bAtCapacity = false;
		}
	}

	if (bAtCapacity && pPartition->pWorldLegGrid && ! pPartition->pWorldLegGrid->bUseActualNumCivs)
	{
		pPartition->pWorldLegGrid->bUseActualNumCivs = true;
	}

	PERFINFO_AUTO_STOP();
}

// ------------------------------------------------------------------------------------------------------------------
static void aiCivilian_OncerPerFrame_PerPartitionCB(int iPartitionIdx)
{
	AICivilianPartitionState *pPartition = aiCivilian_GetPartitionState(iPartitionIdx);
	
	aiCivilian_CheckActiveAreas(pPartition);

	aiCivilian_CheckCounts(pPartition);
	aiCivTraffic_OncePerFrame(pPartition);
	aiCivPlayerKillEvents_Update(pPartition->pKillEventManager);
	aiCivPOIManager_Update(pPartition->pPOIManager);
	aiCivWanderArea_Update(pPartition);
}

// ------------------------------------------------------------------------------------------------------------------
void aiCivilian_OncePerFrame(void)
{
	int print_load = 0;

	if (!g_civSharedState.bInit)
		return;
	
	
	if (g_civSharedState.queuedCivReload)
	{
		acgReloadMap(worldGetAnyCollPartitionIdx());
		g_civSharedState.queuedCivReload = false;
	}

	PERFINFO_AUTO_START_FUNC();
	
	if (g_civSharedState.pMapDef != NULL)
	{
		aiCivDoDeferredFree();
		aiCivPedestrianOncePerFrame();

		partition_ExecuteOnEachPartition(aiCivilian_OncerPerFrame_PerPartitionCB);
	}

	PERFINFO_AUTO_STOP();
}

void aiCivilianAddToBucket(AICivilian *civ)
{
	int i;
	AICivilianPartitionState *partition = aiCivilian_GetPartitionState(civ->iPartitionIdx);
	int minBucketCnt = INT_MAX;
	AICivilianBucket *minBucket = NULL;

	if(!partition)
		return;

	for(i=0; i<ARRAY_SIZE(partition->entBuckets[0]); i++)
	{
		if(eaSize(&partition->entBuckets[civ->type][i].civilians)<minBucketCnt)
		{
			minBucketCnt = eaSize(&partition->entBuckets[civ->type][i].civilians);
			minBucket = &partition->entBuckets[civ->type][i];
		}
	}

	if(minBucket)
	{
		devassert(!civ->entBucket);
		civ->entBucket = minBucket;
		eaPush(&minBucket->civilians, civ);
	}
}

void aiCivilianRemoveFromBucket(AICivilian *civ)
{
	if(civ->entBucket)
	{
		eaFindAndRemoveFast(&civ->entBucket->civilians, civ);
		civ->entBucket = NULL;
	}
}

void aiCivilianTickBuckets(void)
{
	int idx;

	if(g_civSharedState.bIsDisabled)
		return;
	
	for(idx=0; idx<eaSize(&s_eaCivilianPartitions); idx++)
	{
		int i;
		AICivilianPartitionState *partition = s_eaCivilianPartitions[idx];

		if(!partition)
			continue;

		for(i=0; i<ARRAY_SIZE(partition->entBuckets); i++)
		{
			int j;
			F32 typeTickRate = g_CivTickRates[i];

			for(j=0; j<ARRAY_SIZE(partition->entBuckets[i]); j++)
			{
				AICivilianBucket *bkt = &partition->entBuckets[i][j];
				if(ABS_TIME_PARTITION(idx) > bkt->nextUpdate)
				{
					bkt->nextUpdate = bkt->nextUpdate + SEC_TO_ABS_TIME(typeTickRate);
					FOR_EACH_IN_EARRAY(bkt->civilians, AICivilian, civ)
					{
						Entity *e = entFromEntityRef(idx, civ->myEntRef);

						if(!e)
						{
							aiCivilianRemoveFromBucket(civ);
							continue;
						}

						aiCivilianTick(e);
					}
					FOR_EACH_END
				}
			}
		}
	}
}

// ------------------------------------------------------------------------------------------------------------------
const char* aiCivGetPedestrianFaction()
{
	if (g_civSharedState.pMapDef)
	{
		return g_civSharedState.pMapDef->pszPedestrianFaction;
	}

	return NULL;
}

// ------------------------------------------------------------------------------------------------------------------
bool isLegKosherForSpawning(const AICivilianPathLeg *leg, EAICivilianType type, bool desired_forward)
{
	if (type == EAICivilianType_PERSON)
	{
		if (leg->bIsCrosswalk)
			return false;

		return true;
	}
	else
	{
		// can only spawn on a one way street if we are going in its direction
		if (leg->max_lanes == 0)
		{
			return desired_forward;
		}

		// there must be an available leg or intersection in the desired direction (ignoring mid intersections)
		return (desired_forward ? leg->next || leg->nextInt : leg->prev || leg->prevInt);
	}
}


// ------------------------------------------------------------------------------------------------------------------
static AICivilianPathLeg* aiCivilianGetRandomLeg(AICivilianPathLeg **legs, EAICivilianType type, bool desired_forward)
{
	AICivilianPathLeg *returnedLeg;
	int count = eaSize(&legs);
	S32 safetyCounter = 10;

	if(count==0)
		return NULL;

	ANALYSIS_ASSUME(legs);

	do {
		returnedLeg = legs[randomIntRange(0, count-1)];

		// check if leg can be spawned on
		if (isLegKosherForSpawning(returnedLeg, type, desired_forward))
		{
			return returnedLeg;
		}

	} while(--safetyCounter > 0);

	return NULL;
}

// ------------------------------------------------------------------------------------------------------------------
static bool aiCivilianShouldRecalculatePath(AICivilian *civ)
{
	S32 numWayPts;
	S32 numRequired = (civ->type == EAICivilianType_PERSON) ? 3 : 6;
	
	numWayPts = eaSize(&civ->path.eaWaypoints);
	if (numWayPts - civ->path.curWp < numRequired)
	{
		return true;
	}

	return false;
}


// ------------------------------------------------------------------------------------------------------------------
void aiCivilianCheckReachedWpAndContinue(Entity *e, AICivilian *civ, const Vec3 pos)
{
	S32 numReached;

	numReached = mmAICivilianGetAndClearReachedWp(civ->requester);

	if (numReached > 0)
	{
		AICivilianWaypoint *curWp = aiCivilianGetCurrentWaypoint(civ);
		S32 numWayPts, i;
		bool bRemoveStopSignUser = false;

		numWayPts = eaSize(&civ->path.eaWaypoints);
		i = numReached;

		while(curWp && i--)
		{
			civ->fp->CivReachedWaypoint(civ, curWp);
			
			civ->path.curWp++;
			if (civ->path.curWp < numWayPts)
			{
				ANALYSIS_ASSUME(civ->path.eaWaypoints);
				curWp = civ->path.eaWaypoints[civ->path.curWp];

				if(curWp && curWp->bReverse)
					civ->forward = !civ->forward;
			}
			else
			{
				// the BG finished the whole path...
				// devassert(i == 0);
				break;
			}
		}

		/*
		if (curWp)
		{
			if (!curWp->bIsLeg && civ->stopSign_user && aiCivStopSignUserCheckACPI(civ->stopSign_user, curWp->acpi))
			{
				bRemoveStopSignUser = false;
			}
		}

		if (bRemoveStopSignUser)
		{
			// just passed the intersection, free our stop sign user
			aiCivStopSignUserFree(&civ->stopSign_user);
		}
		*/
	}

	if (aiCivilianShouldRecalculatePath(civ))
	{
		bool bHadWaypoints = civ->path.curWp < eaSize(&civ->path.eaWaypoints);

		if (aiCivilianContinuePath(civ))
		{
			if (bHadWaypoints == false)
			{	// if we had an empty path, apply the current waypoint reversal
				AICivilianWaypoint *curWp = aiCivilianGetCurrentWaypoint(civ);
				if(curWp && curWp->bReverse)
					civ->forward = !civ->forward;
			}

			aiCiv_SendQueuedWaypoints(civ);
		}
	}
}


// ------------------------------------------------------------------------------------------------------------------
void aiCivilianProcessPath(Entity *e, AICivilian *civ, const Vec3 vPos, const Vec3 vFacingDir)
{
	AICivilianPartitionState *pPartition = aiCivilian_GetPartitionState(entGetPartitionIdx(e));
	
	devassert(pPartition);

	PERFINFO_AUTO_START_FUNC();
	

	// Check if we have waypoints to delete,
	if (civ->path.eaWaypointsToDelete)
	{
		U32 releaseID = mmAICivilianGetAckReleasedWaypointsID(civ->requester);
		// if so- check if the BG has acknowledged that it has no longer referencing them
		if (releaseID >= civ->path.waypointClearID)
		{
			eaDestroyEx(&civ->path.eaWaypointsToDelete, aiCivilianWaypointFree);
		}
	}

	civ->fp->CivProcessPath(e, civ, vPos, vFacingDir);

	aiCivilianReportCivilianPosition(pPartition->pWorldLegGrid, civ->type, civ->prevPos, vPos);
	copyVec3(vPos, civ->prevPos);

#if defined(CIVILIAN_DEBUGGING)
	if (s_debugDrawPath.drawCivPath && aiCivDebugRef &&
		civ->myEntRef == aiDebugEntRef && s_debugDrawPath.lastCivRef != civ->myEntRef)
	{
		aiCivDebugDrawPath(civ);
	}
#endif

	PERFINFO_AUTO_STOP();
}

// ------------------------------------------------------------------------------------------------------------------
void aiCivilianTick(Entity *e)
{
	AICivilian *civ;
	
	PERFINFO_AUTO_START_FUNC();

	PERFINFO_AUTO_START("civtick1", 1);

	if (!entIsAlive(e))
	{
		civ = e->pCritter->civInfo;
		if (civ && civ->requester)
		{
			entClearCodeFlagBits(e, ENTITYFLAG_CIV_PROCESSING_ONLY);
			aiRemoveFromBucket(e, e->aibase);
			mrDestroy(&civ->requester);
		}

		PERFINFO_AUTO_STOP();
		PERFINFO_AUTO_STOP();
		return; // don't update if we're dead
	}

	PERFINFO_AUTO_STOP_START("civtick2", 1);
	
	civ = e->pCritter->civInfo;
	
	if (civ->flaggedForKill)
	{
		if(entIsAlive(e)) 
			entDie(e, -1, false, false, NULL);

		PERFINFO_AUTO_STOP();
		PERFINFO_AUTO_STOP();
		return;
	}

	PERFINFO_AUTO_STOP();
		
	switch(civ->type)
	{
		xcase EAICivilianType_PERSON: {
			aiCivPedestrian_Tick(e, (AICivilianPedestrian*)civ);
		}
		xcase EAICivilianType_CAR: {
			aiCivCar_Tick(e, (AICivilianCar*)civ);
		}
		xcase EAICivilianType_TROLLEY: {
			aiCivTrolley_Tick(e, (AICivilianTrolley*)civ);
		}
		xdefault: {
			assert(0);
		}
	}

	PERFINFO_AUTO_STOP();
}

// ------------------------------------------------------------------------------------------------------------------
// this will queue waypoints to be deleted, as well as clear any added waypoints that are added or queued to be added
// in the movement manager
void aiCivilian_ClearWaypointList(AICivilian *civ)
{
	S32 i, count;
	eaPushEArray(&civ->path.eaWaypointsToDelete, &civ->path.eaWaypoints);

	count = eaSize(&civ->path.eaWaypoints);
	for (i = civ->path.curWp; i < count; ++i)
	{
		AICivilianWaypoint *pWaypoint = civ->path.eaWaypoints[i];

		if (pWaypoint->pCrosswalkUser)
		{
			aiCivCrosswalk_ReleaseUser(&pWaypoint->pCrosswalkUser);
		}
	}

	// todo: add paranoid check that all pCrosswalkUser were really destroyed, since we are only processing the 
	// remainder of our current list

	eaClear(&civ->path.eaWaypoints);
	eaClear(&civ->path.eaAddedWaypoints);
	civ->path.curWp = 0;

	civ->path.waypointClearID = mmAICivilianClearWaypoints(civ->requester);
}

// ------------------------------------------------------------------------------------------------------------------
void aiCivilianBeNormal(Entity *e, AICivilian *civ)
{
	if(aiCivilian_IsDoingCivProcessingOnly(e, civ))
	{
		AICivilianPartitionState *pPartition = aiCivilian_GetPartitionState(entGetPartitionIdx(e));
		devassert(pPartition);
		pPartition->iNormalCivilianCount++;

		e->aibase->disableAI = 0;
		gslEntMovementCreateSurfaceRequester(e);
		
		civ->timeatLastDefaultRequester = ABS_TIME_PARTITION(pPartition->iPartitionIndex);
		
		// when we go to normal mode, always clear the civilian waypoints
		aiCivilian_ClearWaypointList(civ);

		// disable the civilian movement requester
		mmAICivilianMovementEnable(civ->requester, false);

		entClearCodeFlagBits(e, ENTITYFLAG_CIV_PROCESSING_ONLY);
		aiAddToBucket(e, e->aibase);
	}
}

// ------------------------------------------------------------------------------------------------------------------
void aiCivilianBeCheap(Entity *e, AICivilian *civ)
{
	if(!aiCivilian_IsDoingCivProcessingOnly(e, civ))
	{
		AICivilianPartitionState *pPartition = aiCivilian_GetPartitionState(entGetPartitionIdx(e));
		devassert(pPartition);
		pPartition->iNormalCivilianCount--;
		
		e->aibase->disableAI = 1;
	
		aiMovementSetWalkRunDist(e, e->aibase, 0, 0, 0);
		aiMovementResetPath(e, e->aibase);
		aiMovementClearOverrideSpeed(e, e->aibase);

		// create the civilian movement requester if we don't have it yet
		// otherwise enable it 
		if (!civ->requester)
		{	
			mmRequesterCreateBasicByName(e->mm.movement, &civ->requester, "AICivilianMovement");
		}
		else
		{
			mmAICivilianMovementEnable(civ->requester, true);
		}

		mmAICivilianMovementSetCollision(civ->requester, e->nearbyPlayer);

		civ->timeatLastDefaultRequester = ABS_TIME_PARTITION(pPartition->iPartitionIndex);
		
		// for pedestrians, don't destroy the surface requester, instead hold onto it for X seconds
		if (civ->type != EAICivilianType_PERSON)
		{
			gslEntMovementDestroySurfaceRequester(e);
		}

		entSetCodeFlagBits(e, ENTITYFLAG_CIV_PROCESSING_ONLY);
		aiRemoveFromBucket(e, e->aibase);
	}
}

// ------------------------------------------------------------------------------------------------------------------
void aiCivilianSetSpeed(Entity *e, F32 fSpeed)
{
	if (e->pCritter && e->pCritter->civInfo && e->pCritter->civInfo->requester)
	{
		mmAICivilianMovementSetCritterMoveSpeed(e->pCritter->civInfo->requester, fSpeed);
	}
}

// ------------------------------------------------------------------------------------------------------------------
void aiCivilianSetKnocked(Entity *e)
{
	if (e->pCritter && e->pCritter->civInfo)
	{
		e->pCritter->civInfo->bIsKnocked = true;
		if (e->pCritter->civInfo->requester)
		{
			mmAICivilianMovementEnable(e->pCritter->civInfo->requester, false);
		}
	}
}

// ------------------------------------------------------------------------------------------------------------------
void aiCivHeatmapGetBoundingSizeAndUnitsPerPixel(Vec3 min, Vec3 max, S32 *unitsPerPixel, bool playable_area)
{
	Vec3 size;

	const F32 MAX_PIXELS = 3000.0f;

	if (!playable_area || ! aiCivGetPlayableBounds(min, max))
	{
		// no playable bounds specified, just use the world min/max
		gslHeatmapGetRegionBounds(min, max, NULL);
	}

	subVec3(max, min, size);

	if (size[0] * size[2] > MAX_PIXELS * MAX_PIXELS)
	{
		F32 max_size = MAX(size[0], size[2]);
		*unitsPerPixel = (S32)ceilf(max_size / MAX_PIXELS);
	}
	else
	{
		*unitsPerPixel = 1;
	}
}

// ------------------------------------------------------------------------------------------------------------------
bool aiCivHeatmapGatherCivs(gslHeatMapCBHandle *pHandle, char **ppErrorString)
{
	EntityIterator *pIter = entGetIteratorSingleTypeAllPartitions(ENTITYFLAG_CIVILIAN, ENTITYFLAG_IGNORE, GLOBALTYPE_ENTITYCRITTER);
	Entity *pEnt;

	while(pEnt = EntityIteratorGetNext(pIter))
	{
		if (pEnt->pCritter && pEnt->pCritter->civInfo && pEnt->pCritter->civInfo->type == g_eHeatMapSet)
		{
			Vec3 pos;
			entGetPos(pEnt, pos);

			if (gslHeatMapIsPointIn(pHandle, pos))
			{
				gslHeatMapAddPoint(pHandle, pos, 1);
			}
		}
	}

	EntityIteratorRelease(pIter);
	return true;
}




// ------------------------------------------------------------------------------------------------------------------
// Resets all runtime data of the partition, currently only the counts
static void aiCivWorldLegGridResetCivilianGrid(AICivilianWorldLegGrid *pWorldLegGrid)
{
	S32 gridNum = pWorldLegGrid->dimensionX * pWorldLegGrid->dimensionZ;
	while (--gridNum)
	{
		U32 type;
		for(type = 0; type < EAICivilianType_COUNT; type++)
		{
			S32 x;
			pWorldLegGrid->grid[gridNum].data[type].numCivilians = 0;
			pWorldLegGrid->grid[gridNum].data[type].projectedNumCivilians = 0;

			for (x = 0; x < pWorldLegGrid->grid[gridNum].data[type].numClippedLegs; x++)
			{
				pWorldLegGrid->grid[gridNum].data[type].clippedLegs[x].numProjectedCivilians = 0;
			}
		}
	}
}


// ------------------------------------------------------------------------------------------------------------------
//
static void aiCivWorldLegGridInitSpawning(AICivilianWorldLegGrid *pWorldLegGrid)
{
	if (pWorldLegGrid)
	{
		S32 gridNum = pWorldLegGrid->dimensionX * pWorldLegGrid->dimensionZ;
		while (--gridNum)
		{
			U32 type;
			for(type = 0; type < EAICivilianType_COUNT; type++)
			{
				S32 x;
				pWorldLegGrid->grid[gridNum].data[type].projectedNumCivilians = pWorldLegGrid->grid[gridNum].data[type].numCivilians;

				for (x = 0; x < pWorldLegGrid->grid[gridNum].data[type].numClippedLegs; x++)
				{
					pWorldLegGrid->grid[gridNum].data[type].clippedLegs[x].numProjectedCivilians = 0;
				}
			}
		}

		pWorldLegGrid->bUseActualNumCivs = false;
	}
}

// ------------------------------------------------------------------------------------------------------------------
// returns the AICivWorldLegGridCell* given the index
static AICivWorldLegGridCell* aiCivWorldLegGridGetCell(AICivilianWorldLegGrid *pWorldLegGrid, U32 x, U32 z)
{
	if (x < pWorldLegGrid->dimensionX && z < pWorldLegGrid->dimensionZ)
	{
		return &pWorldLegGrid->grid[pWorldLegGrid->dimensionX * z + x];
	}
	return NULL;
}

// ------------------------------------------------------------------------------------------------------------------
__forceinline static AICivWorldLegGridCell* aiCivWorldLegGridGetCellFromPos(AICivilianWorldLegGrid *pWorldLegGrid, const Vec3 pos)
{
	U32 x = floor((pos[0] - pWorldLegGrid->world_min[0]) / pWorldLegGrid->vGridSize[0]);

	if (x < pWorldLegGrid->dimensionX)
	{
		U32 z = floor((pos[2] - pWorldLegGrid->world_min[2]) / pWorldLegGrid->vGridSize[2]);
		if (z < pWorldLegGrid->dimensionZ )
		{
			return pWorldLegGrid->grid + (pWorldLegGrid->dimensionX * z + x);
		}
	}

	return NULL;
}

// ------------------------------------------------------------------------------------------------------------------
// given the x,z index of the grid, get the bounding min/max of the grid
static void aiCivilian_WorldLegGridGetCellBounding(const AICivilianWorldLegGrid *pWorldLegGrid, U32 x, U32 z, Vec3 vMin, Vec3 vMax)
{
	if (x < pWorldLegGrid->dimensionX && z < pWorldLegGrid->dimensionZ)
	{
		vMin[0] = pWorldLegGrid->world_min[0] + x * (pWorldLegGrid->vGridSize[0]);
		vMin[1] = pWorldLegGrid->world_min[1];
		vMin[2] = pWorldLegGrid->world_min[2] + z * (pWorldLegGrid->vGridSize[2]);

		addVec3(pWorldLegGrid->vGridSize, vMin, vMax);
	}
	else
	{
		zeroVec3(vMin);
		setVec3same(vMax, 1.0f);
	}
}


// ------------------------------------------------------------------------------------------------------------------
bool civClipLineToBox(const Vec3 vBoxMin, const Vec3 vBoxMax, const Vec3 vLineStart, const Vec3 vLineDir, F32 fLineLen,
						Vec3 vClippedStart, Vec3 vClippedEnd)
{
	F32 fDotDir, fDot1;
	F32 fISectTime, fBestTime = FLT_MAX;

	//
	copyVec3(vLineStart, vClippedStart);
	scaleAddVec3(vLineDir, fLineLen, vLineStart, vClippedEnd);

	// todo: trivial rejection of the intersection.

#define CLIP_LINE_MACRO()					\
	if (fDotDir != 0.0f)					\
	{	/* line is not perpendicular */		\
	fISectTime = -(fDot1 / fDotDir);	\
	if (fISectTime >= 0.0f && fISectTime <= fLineLen){\
	if (fDotDir < 0.0f)\
	scaleAddVec3(vLineDir, fISectTime, vLineStart, vClippedStart);\
			else\
			scaleAddVec3(vLineDir, fISectTime, vLineStart, vClippedEnd);\
	}\
	}\

	// normal . (-1, 0, 0)
	//subVec3(vPt1, vMin, vBoxToPt1) . normal;
	fDot1 = -(vLineStart[0]-vBoxMin[0]);
	// normal . lineDirection
	fDotDir = -vLineDir[0];
	CLIP_LINE_MACRO();

	// normal . (1, 0, 0)
	//subVec3(vPt1, vMin, vBoxToPt1) . normal;
	fDot1 = (vLineStart[0]-vBoxMax[0]);
	// normal . lineDirection
	fDotDir = vLineDir[0];
	CLIP_LINE_MACRO();

	// normal . (0, -1, 0)
	//subVec3(vPt1, vMin, vBoxToPt1) . normal;
	fDot1 = -(vLineStart[1]-vBoxMin[1]);
	// normal . lineDirection
	fDotDir = -vLineDir[1];
	CLIP_LINE_MACRO();

	// normal . (0, 1, 0)
	//subVec3(vPt1, vMin, vBoxToPt1) . normal;
	fDot1 = (vLineStart[1]-vBoxMax[1]);
	// normal . lineDirection
	fDotDir = vLineDir[1];
	CLIP_LINE_MACRO();

	// normal . (0, 0, -1)
	//subVec3(vPt1, vMin, vBoxToPt1) . normal;
	fDot1 = -(vLineStart[2]-vBoxMin[2]);
	// normal . lineDirection
	fDotDir = -vLineDir[2];
	CLIP_LINE_MACRO();

	// normal . (0, 0, 1)
	//subVec3(vPt1, vMin, vBoxToPt1) . normal;
	fDot1 = (vLineStart[2]-vBoxMax[2]);
	// normal . lineDirection
	fDotDir = vLineDir[2];
	CLIP_LINE_MACRO();

	if (pointBoxCollision(vClippedStart, vBoxMin, vBoxMax) &&
		pointBoxCollision(vClippedEnd, vBoxMin, vBoxMax) )
	{
		return true;
	}

	return false;
}


// ------------------------------------------------------------------------------------------------------------------
static void gridAddLegsIntersectBox(AICivilianRuntimePathInfo *pPathInfo, AICivilianWorldLegGrid *pWorldLegGrid, AICivWorldLegGridCell  *pGrid, const Vec3 vMin, const Vec3 vMax)
{
	U32 type;
	Vec3 vClippedStart, vClippedEnd;

	// test if the leg intersects with the box.
	for(type = 0; type < EAICivilianType_COUNT; type++)
	{
		S32 j;
		for(j = 0; j < eaSize(&pPathInfo->legs[type]); j++)
		{
			AICivilianPathLeg *leg = pPathInfo->legs[type][j];

			if (leg->bIsCrosswalk)
				continue; // do not add crosswalks to partition

			if (type != 0)
			{
				if ( (!leg->next && !leg->nextInt && !leg->prev && !leg->prevInt))
				{
					continue;
				}
			}

			if (civClipLineToBox(vMin, vMax, leg->start, leg->dir, leg->len, vClippedStart, vClippedEnd))
			{
				// the leg intersects with the world grid.
				// TODO:
				// test to make sure the clipped length vs the actual length is above a threshold

				eaPush(&pWorldLegGrid->tmp_legs, leg);
				// eaPush(&pGrid->data[type].legs_in_grid, leg);
			}
		}

		// if we have legs that are intersecting with this grid, create the clipped legs
		if (eaSize(&pWorldLegGrid->tmp_legs))
		{
			AICivClippedLegData *clippedLeg;

			pGrid->data[type].numClippedLegs = eaSize(&pWorldLegGrid->tmp_legs);
			pGrid->data[type].clippedLegs = calloc(pGrid->data[type].numClippedLegs, sizeof(AICivClippedLegData));

			clippedLeg = pGrid->data[type].clippedLegs;
			for (j = 0; j < eaSize(&pWorldLegGrid->tmp_legs); j++, clippedLeg++)
			{
				Vec3 dir;
				AICivilianPathLeg *leg = pWorldLegGrid->tmp_legs[j];

				civClipLineToBox(vMin, vMax, leg->start, leg->dir, leg->len, clippedLeg->start, clippedLeg->end);

				clippedLeg->leg = leg;

				subVec3(clippedLeg->end, clippedLeg->start, dir);
				clippedLeg->area = lengthVec3(dir) * leg->width * LEG_SURFACEAREA_FACTOR;

				// add the surface area of the clipped leg to the grid
				pGrid->data[type].leg_surface_area += clippedLeg->area;
			}

			eaClear(&pWorldLegGrid->tmp_legs);
		}


	}
	
	/*
	// get the surface area of the leg
	F32 surfaceArea = aiCivilianLegGetFactoredSurfaceArea(leg);
	if (surfaceArea > pGrid->data[type].largest_leg_surface_area)
	{
	pGrid->data[type].largest_leg_surface_area = surfaceArea;
	}
	pGrid->data[type].leg_surface_area += surfaceArea;
	*/
}

// ------------------------------------------------------------------------------------------------------------------
static void aiCivilian_FreeWorldLegGrid(AICivilianWorldLegGrid** pWorldLegGrid)
{
	devassert(pWorldLegGrid);

	if (*pWorldLegGrid)
	{
		S32 i;
		S32 numGrids = (*pWorldLegGrid)->dimensionX * (*pWorldLegGrid)->dimensionZ;
		AICivWorldLegGridCell *grid = (*pWorldLegGrid)->grid;

		for (i = 0; i < numGrids; i++, grid++)
		{
			U32 type;
			for(type = 0; type < EAICivilianType_COUNT; type++)
			{
				if (grid->data[type].clippedLegs)
				{
					free(grid->data[type].clippedLegs);
					grid->data[type].clippedLegs = NULL;
				}

			}


		}

		free(*pWorldLegGrid);
		*pWorldLegGrid = NULL;
	}
}



// ------------------------------------------------------------------------------------------------------------------
static const F32 DEFAULT_GRID_SIZE = 2048.0f; // X/Z axis
static const F32 DEFAULT_GRID_HEIGHT = 1024.0f; // Y- axis
static const U32 DEFAULT_MAX_GRIDS = 32 * 32;

void aiCivilian_WorldLegGridGetBoundingBox(int iPartitionIdx, AICivilianWorldLegGrid *pWorldLegGrid, S32 x, S32 z, S32 xRun, S32 zRun, Vec3 vMin, Vec3 vMax)
{
	Vec3 world_min, world_max, vGridSize;

	if (pWorldLegGrid)
	{
		copyVec3(pWorldLegGrid->world_min, world_min);
		copyVec3(pWorldLegGrid->world_max, world_max);
		copyVec3(pWorldLegGrid->vGridSize, vGridSize);
	}
	else
	{
		if (! aiCivGetPlayableBounds(world_min, world_max))
		{
			getWorldBoundsMinMax(iPartitionIdx, world_min, world_max);
		}
		setVec3(vGridSize, DEFAULT_GRID_SIZE, DEFAULT_GRID_HEIGHT, DEFAULT_GRID_SIZE);
	}


	{
		Vec3 vBoundSize;

		vMin[0] = world_min[0] + x * vGridSize[0];
		vMin[1] = world_min[1];
		vMin[2] = world_min[2] + z * vGridSize[2];

		vBoundSize[0] = vGridSize[0] * (xRun + 1);
		vBoundSize[1] = world_max[1];
		vBoundSize[2] = vGridSize[2] * (zRun + 1);

		addVec3(vBoundSize, vMin, vMax);
	}
}

// ------------------------------------------------------------------------------------------------------------------
void aiCivilian_WorldLegGridGetBoundingMin(AICivilianWorldLegGrid *pWorldLegGrid, Vec3 vMin)
{
	if (pWorldLegGrid)
	{
		copyVec3(pWorldLegGrid->world_min, vMin);
	}
	else
	{
		zeroVec3(vMin);
	}

}

// ------------------------------------------------------------------------------------------------------------------
void aiCivilian_WorldLegGridGetGridCoordsFromPos(int iPartitionIdx, AICivilianWorldLegGrid *pWorldLegGrid, Vec3 vPos, S32 *x, S32 *z)
{
	if(pWorldLegGrid)
	{
		*x = floor((vPos[0] - pWorldLegGrid->world_min[0]) / pWorldLegGrid->vGridSize[0]);
		*x = CLAMP(*x, 0, (S32)(pWorldLegGrid->dimensionX - 1));
		*z = floor((vPos[2] - pWorldLegGrid->world_min[2]) / pWorldLegGrid->vGridSize[2]);
		*z = CLAMP(*z, 0, (S32)(pWorldLegGrid->dimensionZ - 1));
	}
	else
	{
		Vec3 vWorldMin, vWorldMax, vWorldExtent;
		U32 dimensionX, dimensionZ;
		if (! aiCivGetPlayableBounds(vWorldMin, vWorldMax))
		{
			getWorldBoundsMinMax(iPartitionIdx, vWorldMin, vWorldMax);
		}

		subVec3(vWorldMax, vWorldMin, vWorldExtent);
		dimensionX = (U32)ceil(vWorldExtent[0] / DEFAULT_GRID_SIZE);
		dimensionZ = (U32)ceil(vWorldExtent[2] / DEFAULT_GRID_SIZE);

		*x = floor((vPos[0] - vWorldMin[0]) / DEFAULT_GRID_SIZE);
		*x = CLAMP(*x, 0, (S32)(dimensionX - 1));
		*z = floor((vPos[2] - vWorldMin[2]) / DEFAULT_GRID_SIZE);
		*z = CLAMP(*z, 0, (S32)(dimensionZ - 1));
	}
}


// ------------------------------------------------------------------------------------------------------------------
static AICivilianWorldLegGrid* aiCivilian_CreateAndInitWorldLegGrid(int iPartitionIdx, AICivilianRuntimePathInfo *pPathInfo)
{
	AICivilianWorldLegGrid *pWorldLegGrid;
	Vec3	world_extent;
		
	// no legs, the pWorldLegGrid will not be created
	if (eaSize(&pPathInfo->legs[0]) == 0 && eaSize(&pPathInfo->legs[1]) == 0)
	{
		return NULL;
	}

	pWorldLegGrid = calloc(1, sizeof(AICivilianWorldLegGrid));

	if (! aiCivGetPlayableBounds(pWorldLegGrid->world_min, pWorldLegGrid->world_max))
	{
		getWorldBoundsMinMax(iPartitionIdx, pWorldLegGrid->world_min, pWorldLegGrid->world_max);
	}


	// (might want to add on some padding to the world bounds)

	subVec3(pWorldLegGrid->world_max, pWorldLegGrid->world_min, world_extent);
	pWorldLegGrid->dimensionX = (U32)ceil(world_extent[0] / DEFAULT_GRID_SIZE);
	pWorldLegGrid->dimensionZ = (U32)ceil(world_extent[2] / DEFAULT_GRID_SIZE);

	if (pWorldLegGrid->dimensionZ * pWorldLegGrid->dimensionX >= DEFAULT_MAX_GRIDS)
	{
		printf("AI Civilian Partition: World is too large!! Cannot create the civilian world leg grid.\n");
		free (pWorldLegGrid);
		return NULL;
	}

	pWorldLegGrid->vGridSize[0] = DEFAULT_GRID_SIZE;
	pWorldLegGrid->vGridSize[1] = MAX(DEFAULT_GRID_HEIGHT, world_extent[1]);
	pWorldLegGrid->vGridSize[2] = DEFAULT_GRID_SIZE;

	// allocate the grid
	pWorldLegGrid->grid = calloc(pWorldLegGrid->dimensionX*pWorldLegGrid->dimensionZ, sizeof(AICivWorldLegGridCell));

	// sort all the legs into the world partition...

	// using a brute force method for sorting the legs into each grid.
	{
		AICivWorldLegGridCell  *pGrid;
		U32 z;
		Vec3 vMin, vMax;

		// (min, min)
		pGrid = pWorldLegGrid->grid;
		for(z = 0; z < pWorldLegGrid->dimensionZ; z++)
		{
			U32 x;

			for(x = 0; x < pWorldLegGrid->dimensionX; x++)
			{
				aiCivilian_WorldLegGridGetCellBounding(pWorldLegGrid, x, z, vMin, vMax);

				gridAddLegsIntersectBox(pPathInfo, pWorldLegGrid, pGrid, vMin, vMax);

				pGrid++;
			}
		}
	}

	eaDestroy(&pWorldLegGrid->tmp_legs);
	return pWorldLegGrid;
}

// ------------------------------------------------------------------------------------------------------------------
static AICivClippedLegData* partitionGetBestLeg(AICivWorldLegCellData *gridData)
{
	S32 count = gridData->numClippedLegs;
	F32 fSum = 0;
	F32 fTarget;
	AICivClippedLegData *clippedLeg;
	F32 fWorstRatio = FLT_MAX;
	AICivClippedLegData *bestLeg = NULL;

	if(count==0)
	{
		return NULL;
	}

	clippedLeg = gridData->clippedLegs;

	fTarget = randomPositiveF32() * gridData->leg_surface_area;

	while(--count >= 0)
	{
		// check if leg can be spawned on
		if (!clippedLeg->leg->bSpawningDisabled)
		{
			// save the worst ratio, just in case our random weighted leg fails.
			F32 ratio = (F32)clippedLeg->numProjectedCivilians / clippedLeg->area;
			if (ratio < fWorstRatio)
			{
				fWorstRatio = ratio;
				bestLeg = clippedLeg;
			}

			if (fSum + clippedLeg->area >= fTarget)
			{
				return clippedLeg;
			}
		}

		fSum += clippedLeg->area;

		clippedLeg ++;
	}

	// we failed to get any by the random weight method, just return a random leg
	return bestLeg;
}

// ------------------------------------------------------------------------------------------------------------------
static bool aiCivilian_GetBestLegForSpawning(	const AICivilianWorldLegGrid *pWorldLegGrid, 
												const AICivilianRuntimePathInfo *pPathInfo,
												EAICivilianType type,
												AICivilianSpawningData *spawnData)
{
	Vec3 vLegStart, vLegEnd;

	assert(type < EAICivilianType_COUNT && type > EAICivilianType_NULL);

	// get Lowest population grid to spawn in by just looping through all the grids in the list
	if (pWorldLegGrid)
	{
		AICivWorldLegGridCell	*pBestGrid = NULL;
		S32 numPartitions = (S32)(pWorldLegGrid->dimensionZ * pWorldLegGrid->dimensionX);
		AICivWorldLegGridCell  *pGrid;
		F32 worstRatio = FLT_MAX;
		S32 gridID = -1;

		// looping through the entire grid to get the lowest population.
		while(--numPartitions >= 0)
		{
			pGrid = &pWorldLegGrid->grid[numPartitions];
			if(pGrid->data[type].leg_surface_area > 0.0f)
			{
				F32 fRatio;

				if (!pWorldLegGrid->bUseActualNumCivs)
					fRatio = (F32)pGrid->data[type].projectedNumCivilians;
				else
					fRatio = (F32)pGrid->data[type].numCivilians;

				fRatio = fRatio / pGrid->data[type].leg_surface_area;
				if (fRatio < worstRatio)
				{
					worstRatio = fRatio;
					pBestGrid = pGrid;
					gridID = numPartitions;
				}
				else if(fRatio == worstRatio && randomBool())
				{	// if the same as the lowest pick a random one
					pBestGrid = pGrid;
					gridID = numPartitions;
				}
			}
		}

		if(pBestGrid)
		{
			AICivClippedLegData *clippedLeg;

			clippedLeg = partitionGetBestLeg(&pBestGrid->data[type]);
			if ( clippedLeg )
			{
				clippedLeg->numProjectedCivilians++;
				pBestGrid->data[type].projectedNumCivilians ++;

				spawnData->leg = clippedLeg->leg;

				copyVec3(clippedLeg->start, vLegStart);
				copyVec3(clippedLeg->end, vLegEnd);

				// determine which direction is kosher for this leg
				spawnData->forward = !!randomBool();
				if (! isLegKosherForSpawning(spawnData->leg, type, spawnData->forward))
				{
					// we can't spawn on this leg going this direction, change our forward.
					spawnData->forward = !spawnData->forward;
					devassert(isLegKosherForSpawning(spawnData->leg, type, spawnData->forward));
				}

			}
			else
			{
			#ifdef CIVILIAN_DEBUGGING
				printf("Civilian: Failed to find a leg to spawn on!\n");
			#endif
			}
		}
	}

	

	if (!spawnData->leg)
	{
		spawnData->forward = randomBool();
		spawnData->leg = aiCivilianGetRandomLeg(pPathInfo->legs[type], type, spawnData->forward);
		if (! spawnData->leg )
		{
			return false;
		}
		copyVec3(spawnData->leg->start, vLegStart);
		copyVec3(spawnData->leg->end, vLegEnd);
	}


	if (spawnData->leg)
	{
		interpVec3(randomPositiveF32(), vLegStart, vLegEnd, spawnData->pos);
		return true;
	}

	return false;
}

// ------------------------------------------------------------------------------------------------------------------
static bool aiCivilian_GetSpawnPositionData(AICivilianPartitionState *pPartition, EAICivilianType type, AICivilianSpawningData *spawnData)
{
	if (type == EAICivilianType_CAR || type == EAICivilianType_PERSON)
	{
		return aiCivilian_GetBestLegForSpawning(pPartition->pWorldLegGrid, &pPartition->pathInfo, type, spawnData);
	}
	else if (type == EAICivilianType_TROLLEY)
	{
		S32 randPoint;
		if (! eaSize(&pPartition->pathInfo.eaPathPoints) )
			return false;

		randPoint = randomIntRange(0, eaSize(&pPartition->pathInfo.eaPathPoints) - 1);
		spawnData->pPathPoint = pPartition->pathInfo.eaPathPoints[randPoint];
		copyVec2(spawnData->pPathPoint->vPos, spawnData->pos);

		return true;
	}

	return false;
}

// ------------------------------------------------------------------------------------------------------------------
// reports to the partition where the civilian is
static void aiCivilianReportCivilianPosition(AICivilianWorldLegGrid *pWorldLegGrid, EAICivilianType type, const Vec3 prev_pos, const Vec3 cur_pos)
{
	AICivWorldLegGridCell *grid;

	if (pWorldLegGrid)
	{
		devassert(type > EAICivilianType_NULL && type < EAICivilianType_COUNT);

		if (prev_pos)
		{
			grid = aiCivWorldLegGridGetCellFromPos(pWorldLegGrid, prev_pos);
			if (grid)
			{
				grid->data[type].numCivilians--;
				if (grid->data[type].numCivilians < 0) grid->data[type].numCivilians = 0;
			}
		}

		if (cur_pos)
		{
			grid = aiCivWorldLegGridGetCellFromPos(pWorldLegGrid, cur_pos);
			if (grid)
			{
				grid->data[type].numCivilians++;
			}
		}
	}
}

// ------------------------------------------------------------------------------------------------------------------
static AICivilianPathLeg* aiCivilian_GetNearestLegInCellData(AICivWorldLegCellData *data, const Vec3 pos, Vec3 pvClosestPt, F32 *pfDistSQ)
{
	AICivClippedLegData *closest = NULL;
	F32 closestDist = FLT_MAX;
	Vec3 vClosestPt;
	S32 i;

	for(i = 0;  i < data->numClippedLegs; i++)
	{
		Vec3 coll;
		F32 distSq;
		AICivClippedLegData *cld = &data->clippedLegs[i];

		distSq = PointLineDistSquared(pos, cld->leg->start, cld->leg->dir, cld->leg->len, coll);

		if (distSq < closestDist)
		{
			closestDist = distSq;
			closest = cld;
			copyVec3(coll, vClosestPt);
		}
	}

	if(!closest)
		return NULL;

	if (pfDistSQ)
		*pfDistSQ = closestDist;

	if (pvClosestPt)
		copyVec3(vClosestPt, pvClosestPt);

	return closest->leg;
}

// ------------------------------------------------------------------------------------------------------------------
static AICivilianPathLeg* aiCivilian_GetNearestLeg(AICivilianWorldLegGrid *pWorldLegGrid, EAICivilianType type, const Vec3 vPos, Vec3 pvClosestPt, F32 *pfClosestDistSQ)
{
	AICivWorldLegGridCell *grid;

	grid = aiCivWorldLegGridGetCellFromPos(pWorldLegGrid, vPos);
	if (grid)
	{
		AICivWorldLegCellData *data = &grid->data[type];
		AICivilianPathLeg *pClosestLeg = aiCivilian_GetNearestLegInCellData(data, vPos, pvClosestPt, pfClosestDistSQ);
		return pClosestLeg;
	}

	return NULL;
}

// ------------------------------------------------------------------------------------------------------------------
AICivilianPathLeg* aiCivilian_GetLegByPos(AICivilianWorldLegGrid *pWorldLegGrid, EAICivilianType type, const Vec3 pos, Vec3 pvClosestPt)
{
	devassert(type < EAICivilianType_COUNT && type > EAICivilianType_NULL);

	if(pWorldLegGrid)
	{
		AICivilianPathLeg *pClosestLeg = NULL;

		pClosestLeg = aiCivilian_GetNearestLeg(pWorldLegGrid, type, pos, pvClosestPt, NULL);
		if (pClosestLeg)
			return pClosestLeg;

		// couldn't find a leg in the grid the position resided in, look at adjacent grids
		{
			const Vec3 vOffsets[4] = { 
				{pWorldLegGrid->vGridSize[0], 0.f, 0.f},
				{-pWorldLegGrid->vGridSize[0], 0.f, 0.f},
				{0.f, 0.f, pWorldLegGrid->vGridSize[2]},
				{0.f, 0.f, -pWorldLegGrid->vGridSize[2]} };

				Vec3 vGridSearchPos;
				Vec3 vTmp;
				AICivilianPathLeg *pLeg = NULL;
				F32 fClosest = FLT_MAX;
				F32 fDistSQR;
				const F32 fClosestThreshold = SQR(100.f);
				S32 i;

				for (i = 0; i < 4; ++i)
				{
					addVec3(pos, vOffsets[i], vGridSearchPos);

					pLeg = aiCivilian_GetNearestLeg(pWorldLegGrid, type, vGridSearchPos, vTmp, &fDistSQR);
					if (pLeg)
					{
						if (fDistSQR < fClosestThreshold)
						{
							if (pvClosestPt)
								copyVec3(vTmp, pvClosestPt);
							return pLeg;
						}
						else if (fDistSQR < fClosest)
						{
							fClosest = fDistSQR;
							pClosestLeg = pLeg;
							if (pvClosestPt)
								copyVec3(vTmp, pvClosestPt);
						}
					}
				}

				return pClosestLeg;
		}

	}

	return NULL;
}

// ------------------------------------------------------------------------------------------------------------------
__forceinline static void partitionGetGridIndexByPos(AICivilianWorldLegGrid *pWorldLegGrid, const Vec3 vPos, S32 *x, S32 *z)
{
	*x = (S32)floor((vPos[0] - pWorldLegGrid->world_min[0]) / pWorldLegGrid->vGridSize[0]);
	if (*x < 0)
	{
		*x = 0;
	}
	else if (*x > (S32)pWorldLegGrid->dimensionX)
	{
		*x = (S32)pWorldLegGrid->dimensionX - 1;
	}

	*z = (S32)floor((vPos[2] - pWorldLegGrid->world_min[2]) / pWorldLegGrid->vGridSize[2]);
	if (*z < 0)
	{
		*z = 0;
	}
	else if (*z > (S32)pWorldLegGrid->dimensionZ)
	{
		*z = (S32)pWorldLegGrid->dimensionZ - 1;
	}
}

// ------------------------------------------------------------------------------------------------------------------
typedef bool (*PartitionTravereFunc)(const AICivWorldLegGridCell *grid, void *data);
// Returning true will from the callback will stop traversal and exit the function
static void aiCivWorldLegGridTraverseByBox(AICivilianWorldLegGrid *pWorldLegGrid, const Vec3 vMin, const Vec3 vMax,
											 PartitionTravereFunc callback, void *data)
{
	if (pWorldLegGrid && callback)
	{
		if (boxBoxCollision(vMin, vMax, pWorldLegGrid->world_min, pWorldLegGrid->world_max))
		{
			S32 xMin, zMin;
			S32 xMax, zMax;
			S32 x, z;

			partitionGetGridIndexByPos(pWorldLegGrid, vMin, &xMin, &zMin);
			partitionGetGridIndexByPos(pWorldLegGrid, vMax, &xMax, &zMax);

#ifdef CIVDEBUG_SANITY
			if (((xMax - xMin) * (zMax - zMin)) > 10)
			{
				printf("AICivilian: World Partition traversal size might be too big.\n");
			}
#endif
			z = zMin;
			do {
				x = xMin;
				do {
					AICivWorldLegGridCell *pGrid = aiCivWorldLegGridGetCell(pWorldLegGrid, x, z);

					if (callback(pGrid, data))
					{
						return;
					}

				} while (++x <= xMax);

			} while (++z <= zMax);

		}
	}
}


// ------------------------------------------------------------------------------------------------------------------
void aiCivilian_HeatmapDumpPartitionLines(AICivilianWorldLegGrid *pWorldLegGrid, gslHeatMapCBHandle *pHandle, char **ppErrorString)
{
	Vec3 p1, dir;
	F32 worldLen;
	U32 x;

	if (!pWorldLegGrid)
		return;

	// 'horizontal' lines first
	copyVec3(pWorldLegGrid->world_min, p1);
	setVec3(dir, 1.0f, 0.0f, 0.0f);

	worldLen = pWorldLegGrid->world_max[0] - pWorldLegGrid->world_min[0];

	for (x = 0; x < pWorldLegGrid->dimensionZ; x++)
	{
		gslHeatMapAddLineEx(pHandle, p1, dir, worldLen, 20);
		p1[2] += pWorldLegGrid->vGridSize[2];
	}

	// 'vertical' lines second
	copyVec3(pWorldLegGrid->world_min, p1);
	setVec3(dir, 0.0f, 0.0f, 1.0f);

	worldLen = pWorldLegGrid->world_max[2] - pWorldLegGrid->world_min[2];

	for (x = 0; x < pWorldLegGrid->dimensionX; x++)
	{
		gslHeatMapAddLineEx(pHandle, p1, dir, worldLen, 20);
		p1[0] += pWorldLegGrid->vGridSize[0];
	}

}

// ------------------------------------------------------------------------------------------------------------------
static void aiCivilianHeatmapDumpLegsInGrid(AICivilianWorldLegGrid *pWorldLegGrid, gslHeatMapCBHandle *pHandle, char **ppErrorString, U32 x, U32 y)
{
	AICivWorldLegGridCell *grid;
	Vec3 vMin, vMax, vLegStart, vLegEnd;
	S32 i;
	S32 typeIdx = (g_eHeatMapSet == EAICivilianType_PERSON) ? 0 : 1;

	if (!pWorldLegGrid)
		return;


	grid = aiCivWorldLegGridGetCell(pWorldLegGrid, x, y);
	aiCivilian_WorldLegGridGetCellBounding(pWorldLegGrid, x, y, vMin, vMax);

	zeroVec3(vLegEnd);
	zeroVec3(vLegStart);

	{
		Vec3 v1, v2;
		v1[1] = vMin[1];
		v2[1] = vMin[1];

		v1[0] = vMin[0];
		v1[2] = vMin[2];
		v2[0] = vMax[0];
		v2[2] = vMin[2];
		gslHeatMapAddLine(pHandle, v1, v2, 10);

		v1[0] = vMax[0];
		v1[2] = vMin[2];
		v2[0] = vMax[0];
		v2[2] = vMax[2];
		gslHeatMapAddLine(pHandle, v1, v2, 10);

		v1[0] = vMax[0];
		v1[2] = vMax[2];
		v2[0] = vMin[0];
		v2[2] = vMax[2];
		gslHeatMapAddLine(pHandle, v1, v2, 10);

		v1[0] = vMin[0];
		v1[2] = vMax[2];
		v2[0] = vMin[0];
		v2[2] = vMin[2];
		gslHeatMapAddLine(pHandle, v1, v2, 10);
	}

	// draw all the legs
	for (i = 0; i < grid->data[typeIdx].numClippedLegs; i++)
	{
		AICivClippedLegData *clippedLeg = grid->data[typeIdx].clippedLegs + i;
		AICivilianPathLeg *pLeg = clippedLeg->leg;

#if 0
		if (ClipLineToBox(vMin, vMax, pLeg->start, pLeg->dir, pLeg->len, vLegStart, vLegEnd))
		{
			gslHeatMapAddLine(pHandle, vLegStart, vLegEnd, 1);
		}
		else
#endif
		{
			gslHeatMapAddLine(pHandle, pLeg->start, pLeg->end, 20);
		}
	}


}

// ------------------------------------------------------------------------------------------------------------------
static void aiMeasureWorld(BoundingBox *bbox, const WorldCollObjectTraverseParams *params)
{
	const WorldCollStoredModelData*	smd = NULL;
	const WorldCollModelInstanceData* inst = NULL;

	if(wcoGetStoredModelData(&smd, &inst, params->wco, WC_QUERY_BITS_WORLD_ALL))
	{
		Vec3 world_min, world_max;
		assert(inst);
		mulBoundsAA(smd->min, smd->max, inst->world_mat, world_min,	world_max);
		MINVEC3(bbox->min, world_min, bbox->min);
		MAXVEC3(bbox->max, world_max, bbox->max);
	}

	SAFE_FREE(inst);
}

// 2.6.09
// rrp - TODO: change the aiCivilians to use this function (want to have one function to get the world bounds)
static void getWorldBoundsMinMax(int iPartitionIdx, Vec3 min, Vec3 max)
{
	BoundingBox	bbox;

	setVec3same(bbox.min, FLT_MAX);
	setVec3same(bbox.max, -FLT_MAX);

	wcTraverseObjects(worldGetActiveColl(iPartitionIdx), aiMeasureWorld, &bbox, NULL, NULL, /*unique=*/0, WCO_TRAVERSE_STATIC);

	if (bbox.max[1] - bbox.min[1] < 128.f)
	{
		bbox.min[1] -= 128.f;
		bbox.max[1] += 128.f;
	}

	copyVec3(bbox.min, min);
	copyVec3(bbox.max, max);
}

// ------------------------------------------------------------------------------------------------------------------
static F32 aiCiv_GetCivilianTypeLinearDist(AICivilianRuntimePathInfo *pPathInfo, EAICivilianType type)
{
	F32 fDist = 0.f;
	if (type == EAICivilianType_PERSON || type == EAICivilianType_CAR)
	{	
		// EAICivilianType person/car matches EAICivilianLegType
		AICivilianPathLeg **eaLegs = pPathInfo->legs[type];

		FOR_EACH_IN_EARRAY(eaLegs, AICivilianPathLeg, pLeg)
		{
			if (!pLeg->bSpawningDisabled)
				fDist += pLeg->len;
		}
		FOR_EACH_END
	}
	else
	{
		FOR_EACH_IN_EARRAY(pPathInfo->eaPathPoints, AICivilianPathPoint, pPathPoint)
		{
			if (pPathPoint->pNextPathPoint)
			{
				fDist += distance3(pPathPoint->vPos, pPathPoint->pNextPathPoint->vPos);
			}
		}
		FOR_EACH_END
	}
	
	return fDist;
}


// ------------------------------------------------------------------------------------------------------------------
// CivDef File
// ------------------------------------------------------------------------------------------------------------------

static int aiCivMapDef_ValidateDef(AICivilianMapDef *pMapDef, const char* pResourceName);
static void aiCivMapDef_Fixup(AICivilianMapDef *pMapDef);

// ------------------------------------------------------------------------------------------------------------------
static int aiCivMapDef_ValidateCallback(enumResourceValidateType eType, const char* pDictName, 
										 const char* pResourceName, void* pResource, U32 userID)
{
	switch (eType)
	{
		case RESVALIDATE_POST_TEXT_READING:
		{
			AICivilianMapDef *pMapDef = (AICivilianMapDef*)pResource;
			aiCivMapDef_ValidateDef(pMapDef, pResourceName);
		}
		return VALIDATE_HANDLED;
	}

	return VALIDATE_NOT_HANDLED;
}

// ------------------------------------------------------------------------------------------------------------------
static int aiCivMapDef_InheritanceFunc(ParseTable *pti, int column, void *dst, void *src, void *unused)
{
	if(pti==parse_AICivilianMapDef)
	{
		
	}
	else if (pti == parse_AICivPedestrianTypeDef)
	{
		if(!stricmp(pti[column].name, "CivDef"))
		{	// the civdefs are a special case inheritance 
			// only copy over from the source if the dest has none
			AICivPedestrianTypeDef *pedTypeSrc = src;
			AICivPedestrianTypeDef *pedTypeDst = dst;

			if (eaSize(&pedTypeSrc->eaCivDefs) && !eaSize(&pedTypeDst->eaCivDefs))
			{
				FOR_EACH_IN_EARRAY_FORWARDS(pedTypeSrc->eaCivDefs, AICivPedestrianDef, pSrcPedDef)
				{
					AICivPedestrianDef *pPedDefDst = StructCreate(parse_AICivPedestrianDef);
					
					StructCopyAll(parse_AICivPedestrianDef, pSrcPedDef, pPedDefDst);
					
					eaPush(&pedTypeDst->eaCivDefs, pPedDefDst);
				}
				FOR_EACH_END
			}

			return 1;
		}
	}
	else if (pti == parse_AICivVehicleTypeDef)
	{
		if(!stricmp(pti[column].name, "CivDef"))
		{
			// the civdefs are a special case inheritance 
			// only copy over from the source if the dest has none

			AICivVehicleTypeDef *vehicleTypeSrc = src;
			AICivVehicleTypeDef *vehicleTypeDst = dst;
			if (eaSize(&vehicleTypeSrc->eaCivDefs) && !eaSize(&vehicleTypeDst->eaCivDefs))
			{
				FOR_EACH_IN_EARRAY_FORWARDS(vehicleTypeSrc->eaCivDefs, AICivilianDef, pSrcCivDef)
				{
					AICivilianDef *pDstCivDef = StructCreate(parse_AICivilianDef);

					StructCopyAll(parse_AICivilianDef, pSrcCivDef, pDstCivDef);

					eaPush(&vehicleTypeDst->eaCivDefs, pDstCivDef);
				}
				FOR_EACH_END
			}

			return 1;
		}
	}
	else if (pti == parse_AICivMapDefLegInfo)
	{
		if(!stricmp(pti[column].name, "LegDef"))
		{
			AICivMapDefLegInfo *legInfoSrc = src;
			AICivMapDefLegInfo *legInfoDst = dst;

			if (eaSize(&legInfoSrc->eaLegDef) && !eaSize(&legInfoDst->eaLegDef))
			{
				FOR_EACH_IN_EARRAY_FORWARDS(legInfoSrc->eaLegDef, AICivLegDef, pSrcLegDef)
				{
					AICivLegDef *pDstLegDef = StructCreate(parse_AICivLegDef);

					StructCopyAll(parse_AICivLegDef, pSrcLegDef, pDstLegDef);

					eaPush(&legInfoDst->eaLegDef, pDstLegDef);
				}
				FOR_EACH_END
			}

			return 1;
		}
		

	}
	
	return 0;
}

// ------------------------------------------------------------------------------------------------------------------
static void aiCivMapDef_InheritanceApply(AICivilianMapDef *pMapDef)
{
	S32 i;
	for(i = 0; i < eaSize(&pMapDef->inheritConfigs); i++)
	{
		AICivilianMapDef *parent = RefSystem_ReferentFromString(g_pcAICivDefDictName, pMapDef->inheritConfigs[i]);

		if(!parent)
		{
			ErrorFilenamef(pMapDef->pchFilename, "AICivilianMapDef: Unable to find parent to inherit from: %s", 
								pMapDef->inheritConfigs[i]);
			continue;
		}

		SimpleInheritanceApply(parse_AICivilianMapDef, pMapDef, parent, aiCivMapDef_InheritanceFunc, NULL);
	}
}

// ------------------------------------------------------------------------------------------------------------------
static void _checkActiveHandleReload(AICivilianMapDef* mapDef)
{
	if (IS_HANDLE_ACTIVE(g_civSharedState.hMapDef))
	{
		const char* pcDefKey = REF_STRING_FROM_HANDLE(g_civSharedState.hMapDef);
		if (!stricmp(pcDefKey, mapDef->pchName))
		{
			g_civSharedState.pMapDef = GET_REF(g_civSharedState.hMapDef);
			g_civSharedState.queuedCivReload = true;
		}
	}
}

AUTO_FIXUPFUNC;
TextParserResult fixupAICivilianMapDef(AICivilianMapDef* mapDef, enumTextParserFixupType eType, void *pExtraData)
{
	switch(eType)
	{
		case FIXUPTYPE_POST_BIN_READ:
		case FIXUPTYPE_POST_TEXT_READ:
		{
			aiCivMapDef_Fixup(mapDef);
			_checkActiveHandleReload(mapDef);
		}
		xcase FIXUPTYPE_POST_RELOAD:
		{
			aiCivMapDef_InheritanceApply(mapDef);
			_checkActiveHandleReload(mapDef);
		}
		xcase FIXUPTYPE_POST_ALL_TEXT_READING_AND_INHERITANCE_DURING_LOADFILES:
			aiCivMapDef_InheritanceApply(mapDef);
	}

	return PARSERESULT_SUCCESS;
}

// ------------------------------------------------------------------------------------------------------------------
AUTO_STARTUP(AICivilian) ASTRT_DEPS(Critters);
void aiCivMapDef_Startup(void)
{
	RefSystem_RegisterSelfDefiningDictionary(g_pcAICivDefDictName, false, 
												parse_AICivilianMapDef, true, false, NULL);

	resDictManageValidation(g_pcAICivDefDictName, aiCivMapDef_ValidateCallback);

	if (isDevelopmentMode() || isProductionEditMode()) 
		resDictMaintainInfoIndex(g_pcAICivDefDictName, ".Name", NULL, NULL, NULL, NULL);

	resLoadResourcesFromDisk(g_pcAICivDefDictName, "ai/civilian", ".civ_def", "AICivilianMapDef.bin", 
								PARSER_FORCEREBUILD | RESOURCELOAD_SHAREDMEMORY | PARSER_OPTIONALFLAG);

	resDictProvideMissingResources(g_pcAICivDefDictName);
	resDictProvideMissingRequiresEditMode(g_pcAICivDefDictName);
}

// 
// ------------------------------------------------------------------------------------------------------------------
static int aiCivMapDef_ValidateCivLegDefs(AICivMapDefLegInfo *pLegDefInfo, const char *pszFilename)
{
	FOR_EACH_IN_EARRAY(pLegDefInfo->eaLegDef, AICivLegDef, pDef)
	{
		if (!pDef->pchName)
		{
			ErrorFilenamef(pszFilename, "A LegDef does not have a name.");
			return false;
		}

		// search for dupe names
		FOR_EACH_IN_EARRAY(pLegDefInfo->eaLegDef, AICivLegDef, pInnerDef)
		{
			if (pDef != pInnerDef && pInnerDef->pchName == pDef->pchName)
			{
				ErrorFilenamef(pszFilename, "LegDef (%s) duplicate name found.", pDef->pchName);
				return false;
			}
		}
		FOR_EACH_END
	}
	FOR_EACH_END

	return true;
}

// ------------------------------------------------------------------------------------------------------------------
static int aiCivMapDef_ValidateCivDef(AICivilianDef *pCivDef, const char *pszFilename)
{
	if (!pCivDef->pchCritterDef)
	{
		ErrorFilenamef(pszFilename, "CivDef (%s) does not specify a CritterDef.", GET_CIV_DEF_NAME(pCivDef));
		return false;
	}

	if(!critter_DefGetByName(pCivDef->pchCritterDef))
	{
		ErrorFilenamef(pszFilename, "CivDef (%s) cannot find critter def by name (%s).", 
			GET_CIV_DEF_NAME(pCivDef), pCivDef->pchCritterDef);
		return false;
	}

	return true;
}

// ------------------------------------------------------------------------------------------------------------------
static int aiCivMapDef_ValidateDef(AICivilianMapDef *pMapDef, const char* pResourceName)
{
	S32 i;
	if (!aiCivMapDef_ValidateCivLegDefs(&pMapDef->legInfo, pResourceName))
		return false;

	if (!aiCivPedestrian_ValidateTypeDef(&pMapDef->pedestrian, pResourceName))
		return false;
	if (!aiCivCar_ValidateTypeDef(&pMapDef->car, pResourceName))
		return false;
	if (!aiCivTrolley_ValidateTypeDef(&pMapDef->trolley, pResourceName))
		return false;
	
	// validate each internal civilian def 
	for (i = 0; i < EAICivilianType_COUNT; i++)
	{
		AICivilianDef** eaCivDef = aiCivDef_GetCivDefListForType(pMapDef, i, NULL);

		FOR_EACH_IN_EARRAY_FORWARDS(eaCivDef, AICivilianDef, pCivDef)
		{
			if (!aiCivMapDef_ValidateCivDef(pCivDef, pResourceName) )
				return false;
			
			if (pCivDef->pchCivDefName && 
				aiCivDef_ShouldDoStrictDistributionForType(pMapDef, i) )
			{ // check for duplicate names, 
				// but right now we only care about this if we are using strict distribution
				FOR_EACH_IN_EARRAY(eaCivDef, AICivilianDef, pOtherDef)
				{
					if (pOtherDef == pCivDef)
						continue;
					if (pCivDef->pchCivDefName == pOtherDef->pchCivDefName)
					{
						ErrorFilenamef(pResourceName, 
										"Found multiple CivDef CivilianDefName named %s. "
										"Names must be unique", 
										pCivDef->pchCivDefName);
						return false;
					}
				}
				FOR_EACH_END
			}
			
			
			switch(i)
			{
				case EAICivilianType_PERSON:
				{
					if (!aiCivPedestrian_ValidatePedDef((AICivPedestrianDef*)pCivDef, pResourceName))
						return false;
				} 
				xcase EAICivilianType_CAR:
				{
					if (!aiCivCar_ValidateCarDef(pCivDef, pResourceName))
						return false;
				}
				xcase EAICivilianType_TROLLEY:
				{
					if (!aiCivTrolley_ValidateTrolleyDef(pCivDef, pResourceName))
						return false;
				}
			}
		}
		FOR_EACH_END
	}

	if (pMapDef->pedestrian.emoteReaction)
	{
		aiCivEmoteReactDef_Validate(pMapDef->pedestrian.emoteReaction, pResourceName);
	}
	if (pMapDef->pedestrian.onClick)
	{
		aiCivOnClickDef_Validate(pMapDef->pedestrian.onClick, pResourceName);
	}
	
	return true;

}


// ------------------------------------------------------------------------------------------------------------------
static void aiCivDef_Fixup(AICivilianDef *pCivDef)
{
	if (pCivDef->iSpawnChanceWeight <= 0)
	{
		pCivDef->iSpawnChanceWeight = 1;
	}
}

static void aiCivMapDef_Fixup(AICivilianMapDef *pMapDef)
{
	S32 i;

	aiCivPedestrian_FixupTypeDef(&pMapDef->pedestrian);
	aiCivCar_FixupTypeDef(&pMapDef->car);
	aiCivTrolley_FixupTypeDef(&pMapDef->trolley);

	// go through the civilian types and do fixup and validation
	for (i = 0; i < EAICivilianType_COUNT; i++)
	{
		// get the list of civilian defs
		AICivilianDef** eaCivDef = aiCivDef_GetCivDefListForType(pMapDef, i, NULL);
		S32 iMaxWeight = 0;

		FOR_EACH_IN_EARRAY_FORWARDS(eaCivDef, AICivilianDef, pCivDef)
		{
			if (!pCivDef->pchCivDefName)
			{	// no name given, but we need one for lookups when storing runtime information 
				char civilianName[256];
				sprintf(civilianName, "%dciv%d", i, ipCivDefIndex);
				pCivDef->pchCivDefName = allocAddString(civilianName);
			}

			// do type specific AICivilianDef fixup and validation 
			switch(i)
			{
				case EAICivilianType_PERSON:
				{
					aiCivPedestrian_FixupPedDef((AICivPedestrianDef*)pCivDef);
				} 
				xcase EAICivilianType_CAR:
				{
					aiCivCar_FixupCarDef(pCivDef);
				}
				xcase EAICivilianType_TROLLEY:
				{
					aiCivTrolley_FixupTrolleyDef(pCivDef);
				}
			}

			iMaxWeight += pCivDef->iSpawnChanceWeight;
		}
		FOR_EACH_END

		if (iMaxWeight == 0)
			iMaxWeight = 1;

		switch (i)
		{
			case EAICivilianType_PERSON:
				pMapDef->pedestrian.iCivDefsMaxWeight = iMaxWeight;
			xcase EAICivilianType_CAR:
				pMapDef->car.iCivDefsMaxWeight = iMaxWeight;
			xcase EAICivilianType_TROLLEY:
				pMapDef->trolley.iCivDefsMaxWeight = iMaxWeight;
		}

		FOR_EACH_IN_EARRAY(eaCivDef, AICivilianDef, pCivDef)
		{
			pCivDef->fTargetDistributionPercent = (F32)pCivDef->iSpawnChanceWeight / (F32)iMaxWeight;
		}
		FOR_EACH_END
	}

	if (pMapDef->pedestrian.emoteReaction)
	{
		aiCivEmoteReactDef_Fixup(pMapDef->pedestrian.emoteReaction);
	}
	if (pMapDef->pedestrian.onClick)
	{
		aiCivOnClickDef_Fixup(pMapDef->pedestrian.onClick);
	}
}


// ------------------------------------------------------------------------------------------------------------------
static int aiCivMapDef_LoadFile(const char *pszFilename, AICivilianMapDef *pMapDef)
{
	if (!ParserReadTextFile(pszFilename, parse_AICivilianMapDef, pMapDef, 0))
	{
		ErrorFilenamef(pszFilename, "Failed to load file.");
		return false;
	}

	if (!aiCivMapDef_ValidateDef(pMapDef, pszFilename))
	{
		return false;	
	}

	aiCivMapDef_Fixup(pMapDef);
	
	return true;
}

// ------------------------------------------------------------------------------------------------------------------
static void aiCivMapDef_SetCurrent(const char *pcMapDef)
{
	if (IS_HANDLE_ACTIVE(g_civSharedState.hMapDef))
	{
		REMOVE_HANDLE(g_civSharedState.hMapDef);
	}

	if (pcMapDef)
	{
		SET_HANDLE_FROM_STRING(g_pcAICivDefDictName, pcMapDef, g_civSharedState.hMapDef);
	}
}

// ------------------------------------------------------------------------------------------------------------------
bool aiCivilian_LoadMapDef()
{
	const char *pcMapDef;

	aiCivMapDef_Free();
		
	// first check the zone info if we have a civilian def on the map properties
	// if not, see if we have a file in the server path
	pcMapDef = zmapInfoGetCivilianMapDefKey(NULL);
	if (pcMapDef)
	{	// there is a def 
		aiCivMapDef_SetCurrent(pcMapDef);
				
		g_civSharedState.pMapDef = GET_REF(g_civSharedState.hMapDef);
		if (!g_civSharedState.pMapDef)
		{
			Errorf("Failed to find civilian map def named: %s", pcMapDef);
		}
	}
	
	// no map def, or failed to find def, check the server directory 
	if (!g_civSharedState.pMapDef)
	{
		pcMapDef = aiCivilianGetSuffixedFileName(1, 0, CIV_FILE_MAPDEF_SUFFIX);
		if(fileExists(pcMapDef))
		{
			if(!g_civSharedState.pMapDef || g_civSharedState.pMapDef == &s_aiCivDefaultMapDef)
			{
				g_civSharedState.pMapDef = StructCreate(parse_AICivilianMapDef);
				g_civSharedState.mapDefOwned = true;
			}

			if (!aiCivMapDef_LoadFile(pcMapDef, g_civSharedState.pMapDef))
			{
				return false;
			}
		}
		else
		{
			// only throw an error when a user is regenerating
			
			if (acg_d_nodeGenerator && g_civSharedState.pCivPathInfo)
			{	// has valid path information, throw an error that we can't find the civ def file
				Errorf("AI Civilian: Could not find civilian map definition file: %s\n", pcMapDef);
			}
			g_civSharedState.pMapDef = &s_aiCivDefaultMapDef;
		}
	}

	if (g_civSharedState.pMapDef)
	{
		g_civSharedState.onclick_def = aiCivOnClickDef_LoadStaticDef(&g_civSharedState);
		
		aiCivEditing_ReportCivMapDefLoad();
	}

	return true;
}

// ------------------------------------------------------------------------------------------------------------------
void aiCivMapDef_Free()
{
	if (g_civSharedState.pMapDef && g_civSharedState.mapDefOwned)
	{
		aiCivEmoteReactDef_Destroy(g_civSharedState.pMapDef->pedestrian.emoteReaction);
		StructDestroy(parse_AICivilianMapDef, g_civSharedState.pMapDef);
	}
	else
	{
		aiCivMapDef_SetCurrent(NULL);
	}

	StructDestroySafe(parse_AIPedestrianOnClickDef, &g_civSharedState.onclick_def);

	g_civSharedState.mapDefOwned = false;
	g_civSharedState.pMapDef = NULL;
}


// ------------------------------------------------------------------------------------------------------------------
// civ_def
static AICivilianDef* aiCivDef_FindByName(AICivilianDef **peaCivDefs, const char *pszCivDef)
{
	FOR_EACH_IN_EARRAY(peaCivDefs, AICivilianDef, pCivDef)
	{
		// these are pooled strings
		if (pszCivDef == pCivDef->pchCivDefName)
			return pCivDef;
	}
	FOR_EACH_END

	return NULL;
}

// ------------------------------------------------------------------------------------------------------------------
static AICivilianDef** aiCivDef_GetCivDefListForType(AICivilianMapDef *pCivMapDef, EAICivilianType type, S32 *piOutMaxWeight)
{
	if (!pCivMapDef)
		return NULL;

	switch (type)
	{
		acase EAICivilianType_PERSON:
			// safe to cast since first member of AICivPedestrianDef is AICivilianDef
			if (piOutMaxWeight)
				*piOutMaxWeight = pCivMapDef->pedestrian.iCivDefsMaxWeight;

			return (AICivilianDef**)pCivMapDef->pedestrian.eaCivDefs;

		xcase EAICivilianType_CAR:
			if (piOutMaxWeight)
				*piOutMaxWeight = pCivMapDef->car.iCivDefsMaxWeight;

			return pCivMapDef->car.eaCivDefs;

		xcase EAICivilianType_TROLLEY:
			if (piOutMaxWeight)
				*piOutMaxWeight = pCivMapDef->trolley.iCivDefsMaxWeight;

			return pCivMapDef->trolley.eaCivDefs;

		xdefault:
			devassertmsgf(0 , "Unknown civilian type index (%d)\n", type);
	}

	return NULL;
}

// ------------------------------------------------------------------------------------------------------------------
static S32 aiCivDef_ShouldDoStrictDistributionForType(AICivilianMapDef *pCivMapDef, EAICivilianType type)
{
	switch (type)
	{
		xcase EAICivilianType_PERSON:
			return pCivMapDef->pedestrian.bUseStrictDistribution;
		xdefault:
			return false;
	}
}

// ------------------------------------------------------------------------------------------------------------------
static AICivilianDef* aiCivDef_TypeFindByName(AICivilianMapDef *pCivMapDef, EAICivilianType type, const char *pszCivDef)
{
	AICivilianDef **eaList = aiCivDef_GetCivDefListForType(pCivMapDef, type, NULL);

	return aiCivDef_FindByName(eaList, pszCivDef);
}

// ------------------------------------------------------------------------------------------------------------------
const AICivLegDef* aiCivMapDef_GetCivLegDef(const AICivilianMapDef *pCivMapDef, const char *pchName)
{
	FOR_EACH_IN_EARRAY(pCivMapDef->legInfo.eaLegDef, AICivLegDef, pDef)
	{
		if(pDef->pchName == pDef->pchName)
		{
			return pDef;
		}
	}
	FOR_EACH_END

	return NULL;
}


// ------------------------------------------------------------------------------------------------------------------
void aiCivilian_CalculateDesiredCivCounts(AICivilianPartitionState *pPartition)
{
	S32 iSanityCounts[EAICivilianType_COUNT] = {2000, 500, 30};
	S32 i;
	for(i = 0; i < EAICivilianType_COUNT; ++i)
	{
		if (g_civSharedState.pMapDef->desired[i])
		{	// absolute counts take priority over density
			pPartition->civTypeRuntimeInfo[i].targetCount = g_civSharedState.pMapDef->desired[i];
		}
		else if (g_civSharedState.pMapDef->desiredUnitsPerType[i] > 0.f)
		{
			EAICivilianType type = (EAICivilianType)i;
			F32 linearDist = aiCiv_GetCivilianTypeLinearDist(&pPartition->pathInfo, type);

			pPartition->civTypeRuntimeInfo[i].targetCount = (S32)ceil(linearDist / g_civSharedState.pMapDef->desiredUnitsPerType[i]);
			if (pPartition->civTypeRuntimeInfo[i].targetCount > iSanityCounts[type])
				pPartition->civTypeRuntimeInfo[i].targetCount = iSanityCounts[type];
		}
	}
}

// ------------------------------------------------------------------------------------------------------------------
static AICivilianTypeDefRuntimeInfo* aiCiv_FindCivDefRuntimeInfo(AICivilianPartitionState *pPartition, EAICivilianType type, AICivilianDef *pDef)
{
	
	FOR_EACH_IN_EARRAY(pPartition->civTypeRuntimeInfo[type].eaCivilianTypes, AICivilianTypeDefRuntimeInfo, pDefRTI)
	{
		if (pDefRTI->pchCivDefName == pDef->pchCivDefName)
			return pDefRTI;
	}
	FOR_EACH_END

	return false;
}

// ------------------------------------------------------------------------------------------------------------------
static void aiCivilian_DestroyCivilianTypeRuntimeInfo(AICivilianPartitionState *pPartitionState)
{
	S32 i;
	for(i = 0; i < EAICivilianType_COUNT; ++i)
	{
		eaDestroyEx(&pPartitionState->civTypeRuntimeInfo[i].eaCivilianTypes, NULL);
		pPartitionState->civTypeRuntimeInfo[i].targetCount = 0;
	}
}

// ------------------------------------------------------------------------------------------------------------------
static void aiCivilian_SetTypeRuntimeInfo(AICivilianPartitionState *pPartition, AICivilianMapDef *pCivMapDef)
{
	S32 type;

	aiCivilian_DestroyCivilianTypeRuntimeInfo(pPartition);

	for(type = 0; type < EAICivilianType_COUNT; ++type)
	{
		AICivilianDef** eaCivDefs = aiCivDef_GetCivDefListForType(pCivMapDef, type, NULL);
		FOR_EACH_IN_EARRAY(eaCivDefs, AICivilianDef, pDef)
		{
			AICivilianTypeDefRuntimeInfo *rti = malloc(sizeof(AICivilianTypeDefRuntimeInfo));
			rti->pchCivDefName = pDef->pchCivDefName;
			rti->iCurrentCount = 0;
			eaPush(&pPartition->civTypeRuntimeInfo[type].eaCivilianTypes, rti);
		}
		FOR_EACH_END
	}

	// go through all the current civilians and tally our count
	for(type = 0; type < EAICivilianType_COUNT; ++type)
	{
		FOR_EACH_IN_EARRAY(pPartition->civilians[type], AICivilian, pCiv)
		{
			AICivilianTypeDefRuntimeInfo *pRTI = aiCiv_FindCivDefRuntimeInfo(pPartition, type, pCiv->civDef);
			if (pRTI) 
				pRTI->iCurrentCount++;
		}
		FOR_EACH_END
	}


	// do this only if we have path info
	if (g_civSharedState.pCivPathInfo)
		aiCivilian_CalculateDesiredCivCounts(pPartition);
}


// ------------------------------------------------------------------------------------------------------------------
// OnClick File Def
// ------------------------------------------------------------------------------------------------------------------
bool aiCivOnClickDef_Validate(AIPedestrianOnClickDef *pDef, const char *pszFilename)
{
	FOR_EACH_IN_EARRAY(pDef->eaOnClickInfo, AIPedestrianOnClickInfo, pPedOnClick)
	{
		Referent *p = RefSystem_ReferentFromString(gFSMDict, pPedOnClick->pchFsm);
		if (!p)
		{
			ErrorFilenamef(pszFilename, "Cannot find the OnClick FSM named (%s).", pPedOnClick->pchFsm);
			return false;
		}
	}
	FOR_EACH_END

	return true;
}

// ------------------------------------------------------------------------------------------------------------------
void aiCivOnClickDef_Fixup(AIPedestrianOnClickDef *pDef)
{
	pDef->iOnClickMaxWeight = 0;
	FOR_EACH_IN_EARRAY(pDef->eaOnClickInfo, AIPedestrianOnClickInfo, pPedOnClick)
	{
		pDef->iOnClickMaxWeight += pPedOnClick->weight;
	}
	FOR_EACH_END
}

// ------------------------------------------------------------------------------------------------------------------
static AIPedestrianOnClickDef* aiCivOnClickDef_LoadStaticDef(AICivilianSharedState *pSharedCivilianState)
{
	const char *onclick_def;
	AICivilianMapDef *pMapDef = pSharedCivilianState->pMapDef;
	
	if (pMapDef && pMapDef->pedestrian.onClick)
		return NULL;

	onclick_def = aiCivilianGetSuffixedFileName(1, 0, CIV_FILE_ONCLICK_SUFFIX);
	if (fileExists(onclick_def))
	{
		AIPedestrianOnClickDef *pDef = NULL;
		StructDestroySafe(parse_AIPedestrianOnClickDef, &pSharedCivilianState->onclick_def);
		pDef = StructCreate(parse_AIPedestrianOnClickDef);

		if (! ParserReadTextFile(onclick_def, parse_AIPedestrianOnClickDef, pDef, 0))
		{
			printf("\nAICivilian: Error reading onclick def file: %s\n", onclick_def);
			return NULL;
		}

		aiCivOnClickDef_Validate(pDef, onclick_def);
		aiCivOnClickDef_Fixup(pDef);
		return pDef;
	}

	return NULL;
}

// ------------------------------------------------------------------------------------------------------------------
const char* aiCivOnClickDef_GetRandomFSM(AIPedestrianOnClickDef *pOnClickDef)
{
	if (pOnClickDef && eaSize(&pOnClickDef->eaOnClickInfo))
	{
		if (pOnClickDef->iOnClickMaxWeight == 0)
		{
			return pOnClickDef->eaOnClickInfo[0]->pchFsm;
		}
		else
		{
			S32 sum = 0;
			S32 i;
			S32 randWeight = randomIntRange(1, pOnClickDef->iOnClickMaxWeight);

			for (i = 0; i < eaSize(&pOnClickDef->eaOnClickInfo); i++)
			{
				AIPedestrianOnClickInfo *pInfo = pOnClickDef->eaOnClickInfo[i];
				sum += pInfo->weight;

				if (randWeight <= sum)
				{
					return pInfo->pchFsm;
				}
			}
		}

		return pOnClickDef->eaOnClickInfo[0]->pchFsm;
	}

	return NULL;
}

// ------------------------------------------------------------------------------------------------------------------
// Emote React Def
// ------------------------------------------------------------------------------------------------------------------

static bool aiCivEmoteReactDef_Validate(AICivEmoteReactDef *pDef, const char *pszFilename)
{
	FOR_EACH_IN_EARRAY(pDef->eaEmoteReaction, AICivEmoteReactInfo, pEmoteReact)
	{
		Referent *p = RefSystem_ReferentFromString(gFSMDict, pEmoteReact->pszFsm);
		if (!p)
		{
			ErrorFilenamef(pszFilename, "Emote reaction cannot find the FSM named (%s).", pEmoteReact->pszFsm);
			return false;
		}
	}
	FOR_EACH_END
	

	return true;
}

// ------------------------------------------------------------------------------------------------------------------
static void aiCivEmoteReactDef_Fixup(AICivEmoteReactDef *pDef)
{
	
}

// ------------------------------------------------------------------------------------------------------------------
static void aiCivEmoteReactDef_Destroy(AICivEmoteReactDef *emoteReactDef)
{
	
}

AICivEmoteReactInfo *aiCivEmoteReactDef_FindReactInfo(AICivEmoteReactDef *emoteReactDef, const char *pchEmote)
{
	pchEmote = allocFindString(pchEmote);
	FOR_EACH_IN_EARRAY(emoteReactDef->eaEmoteReaction, AICivEmoteReactInfo, pEmoteInfo)
		if (pEmoteInfo->pszEmoteName == pchEmote)
			return pEmoteInfo;
	FOR_EACH_END

	return NULL;
}


// ------------------------------------------------------------------------------------------------------------------
// AICivDisplayNameData
// ------------------------------------------------------------------------------------------------------------------

static void aiCivDisplayNameData_FillDisplayNamesForCritter(const char ***eaDisplayNameMsgList, const char *pszBasename, const char *pszType)
{
#define MAX_CRITTER_NAMES		(1024+1)
#define MISS_COUNT_THRESHOLD	3

	char messageKey[1024], messageNameBase[1024];
	S32 i, missCount;

	sprintf(messageNameBase, "%s.%s.", pszBasename, pszType);

	missCount = 0;
	// search for all the possible names
	// I'm not sure of a better way to do this other than just brute force find all names
	for (i = 1; i < MAX_CRITTER_NAMES; i++)
	{
		Message *pMessage;

		sprintf(messageKey, "%s%d", messageNameBase, i);

		pMessage = RefSystem_ReferentFromString(gMessageDict, messageKey);
		if (pMessage)
		{
			missCount = 0;
			eaPush(eaDisplayNameMsgList, pMessage->pcMessageKey);
		}
		else
		{
			missCount++;
			if (missCount >= MISS_COUNT_THRESHOLD)
			{	// too many consecutive name misses, we'll assume we reached the end of the list of names
				break;
			}
		}

	}
}

// ------------------------------------------------------------------------------------------------------------------
static void aiCivDisplayNameData_AddDisplayNames(AICivDisplayNameData *pData, EAICivilianType type, const char *pszCritterDefName)
{
	AICivCritterDisplayName critterNames = {0};
	CritterDef *critterDef;

	if (! stricmp(pszCritterDefName, CIV_DEFAULT_DISPLAYNAME_BASE))
		return;

	critterDef = critter_DefGetByName(pszCritterDefName);
	if (!critterDef)
		return;

	if (stashFindPointer(pData->stashDisplayNames, critterDef->pchName, NULL))
		return;

	critterNames.pszCritterDefName = critterDef->pchName;


	if (type == EAICivilianType_PERSON)
	{
		aiCivDisplayNameData_FillDisplayNamesForCritter(&critterNames.eaMaleNameMsgs, pszCritterDefName, "male");
		aiCivDisplayNameData_FillDisplayNamesForCritter(&critterNames.eaFemaleNameMsgs, pszCritterDefName, "female");
	}
	else
	{
		aiCivDisplayNameData_FillDisplayNamesForCritter(&critterNames.eaMaleNameMsgs, pszCritterDefName, "car" );
		critterNames.isCar = true;
	}

	if (eaSize(&critterNames.eaMaleNameMsgs) || eaSize(&critterNames.eaFemaleNameMsgs))
	{
		AICivCritterDisplayName *pCritterName = malloc(sizeof(AICivCritterDisplayName));

		memcpy(pCritterName, &critterNames, sizeof(AICivCritterDisplayName));

		stashAddPointer(pData->stashDisplayNames, critterNames.pszCritterDefName, pCritterName, false);
	}
}



// ------------------------------------------------------------------------------------------------------------------
static AICivDisplayNameData* aiCivDisplayNameData_CreateAndInit()
{
	AICivDisplayNameData *pData = calloc(1, sizeof(AICivDisplayNameData));

	
	devassert(g_civSharedState.pMapDef != NULL);

	aiCivDisplayNameData_FillDisplayNamesForCritter(&pData->eaMaleNameMsgs, CIV_DEFAULT_DISPLAYNAME_BASE, "male" );
	aiCivDisplayNameData_FillDisplayNamesForCritter(&pData->eaFemaleNameMsgs, CIV_DEFAULT_DISPLAYNAME_BASE, "female" );
	aiCivDisplayNameData_FillDisplayNamesForCritter(&pData->eaCarNameMsgs, CIV_DEFAULT_DISPLAYNAME_BASE, "car" );

	pData->stashDisplayNames = stashTableCreateAddress(5);

	// check if we have any special names for the default critter defs
	/* TODO: no one uses this, I'm sure. But I want to support it and let people know
	// 
	{
		S32 i;

		for (i = 0; i < EAICivilianType_COUNT; i++)
		{
			S32 x;

			for (x = 0; x < eaSize(&g_aiCivilianState.map_def->typeDefs[i].eaCivDefs); x++)
			{
				AICivilianDef *pDef  = g_aiCivilianState.map_def->typeDefs[i].eaCivDefs[x];

				const char *pszCritterDefName = pDef->critterDef;

				aiCivDisplayNameData_AddDisplayNames(pData, i+1, pszCritterDefName);
			}
		}
	}
	*/

	return pData;
}

// ------------------------------------------------------------------------------------------------------------------
static Message* getRandomMessageFromNameMsgList(const char **eaNameMsgList)
{
	S32 iNumNames;

	iNumNames = eaSize(&eaNameMsgList);
	if (iNumNames)
	{
		S32 iRandIdx;

		iRandIdx = 0;
		if (iNumNames > 1)
		{
			iRandIdx = randomIntRange(0, iNumNames-1);
		}

		return RefSystem_ReferentFromString(gMessageDict, eaNameMsgList[iRandIdx]);
	}

	return NULL;
}

// ------------------------------------------------------------------------------------------------------------------
static Message* aiCivDisplayNameData_GetRandomName(const CritterDef *pCritterDef, EAICivilianType type, bool bFemale)
{
	const char **eaNameMsgList = NULL;
	Message *pMessage;
	AICivCritterDisplayName *pDisplay = NULL;
	AICivDisplayNameData *displayNameData;
	
		
	if (!g_civSharedState.pDisplayNameData)
		return NULL;
	
	displayNameData = g_civSharedState.pDisplayNameData;
	
	if (stashFindPointer(displayNameData->stashDisplayNames, pCritterDef->pchName, &pDisplay))
	{
		if (type == EAICivilianType_PERSON)
		{
			if (!pDisplay->isCar)
			{
				eaNameMsgList = (bFemale) ? pDisplay->eaFemaleNameMsgs : pDisplay->eaMaleNameMsgs;
			}
		}
		else
		{
			if (pDisplay->isCar)
			{
				eaNameMsgList = pDisplay->eaMaleNameMsgs;
			}
		}

		pMessage = getRandomMessageFromNameMsgList(eaNameMsgList);
		if (pMessage)
			return pMessage;
	}


	if (type == EAICivilianType_PERSON)
	{
		// pCritterDef->pchName
		eaNameMsgList = (bFemale) ? displayNameData->eaFemaleNameMsgs : displayNameData->eaMaleNameMsgs;
	}
	else
	{
		eaNameMsgList = displayNameData->eaCarNameMsgs;
	}


	return getRandomMessageFromNameMsgList(eaNameMsgList);
}


// ------------------------------------------------------------------------------------------------------------------
// This function is meant to be called from the critter initialize, we always assume a person type civ
// 
void aiCivGiveCritterRandomName(Entity *e, const CritterDef* pCritterDef)
{
	Message *pOverrideName = NULL;

	pOverrideName = aiCivDisplayNameData_GetRandomName(pCritterDef, EAICivilianType_PERSON, Gender_Female == costumeEntity_GetEffectiveCostumeGender(e));
	if (pOverrideName)
		critter_OverrideDisplayMessage(e, pOverrideName, 0);
}



// ------------------------------------------------------------------------------------------------------------------
static void aiCivCritterDisplayName_Free(AICivCritterDisplayName *dat)
{
	eaDestroy(&dat->eaMaleNameMsgs);
	eaDestroy(&dat->eaFemaleNameMsgs);
	free(dat);
}

// ------------------------------------------------------------------------------------------------------------------
static void aiCivDisplayNameData_Free(AICivDisplayNameData **pData)
{
	if (*pData)
	{
		stashTableDestroyEx((*pData)->stashDisplayNames, NULL, aiCivCritterDisplayName_Free);

		eaDestroy(&(*pData)->eaMaleNameMsgs);
		eaDestroy(&(*pData)->eaFemaleNameMsgs);
		eaDestroy(&(*pData)->eaCarNameMsgs);

		free(*pData);
		*pData = NULL;
	}
}


// Sets the pedestrian count to be a scale of the amount defined in the civilian definition file
// The scales are clamped between 0 and 1
AUTO_EXPR_FUNC(ai, encounter_action) ACMD_NAME(SetCivilianCountScale);
void exprFuncSetCivilianCountScale(ACMD_EXPR_PARTITION iPartitionIdx, float pedestrianCountScale, float carCountScale)
{
	if(g_civSharedState.pMapDef)
	{
		AICivilianPartitionState *pPartition = aiCivilian_GetPartitionState(iPartitionIdx);
		bool bChanged = false;
		S32 lastCount;
		devassert(pPartition);

		pedestrianCountScale = CLAMP(pedestrianCountScale, 0.f, 1.f);
		carCountScale = CLAMP(carCountScale, 0.f, 1.f);
				
		aiCivilian_CalculateDesiredCivCounts(pPartition);
				
		lastCount = pPartition->civTypeRuntimeInfo[0].targetCount;
		pPartition->civTypeRuntimeInfo[0].targetCount = floor(pedestrianCountScale * (F32)lastCount);
		bChanged = (lastCount != pPartition->civTypeRuntimeInfo[0].targetCount);

		lastCount = pPartition->civTypeRuntimeInfo[1].targetCount;
		pPartition->civTypeRuntimeInfo[1].targetCount = floor(carCountScale * (F32)lastCount);
		bChanged = bChanged || (lastCount != pPartition->civTypeRuntimeInfo[1].targetCount);

		if (bChanged)
		{
			aiCivWorldLegGridInitSpawning(pPartition->pWorldLegGrid);
		}
	}
	
}

// ------------------------------------------------------------------------------------------------------------------
// 
// ------------------------------------------------------------------------------------------------------------------

void aiCivLeg_GetLegCornerPoints(const AICivilianPathLeg *leg, S32 bStart, Vec2 vStart, Vec2 vEnd)
{
	if (bStart)
	{
		if (leg->bSkewed_Start)
		{
			Vec3 vDir;
			F32 fLength;
			acgLeg_GetSkewedLaneDirection(leg, vDir);

			fLength = acgLeg_GetSkewedLaneLength(leg);
			scaleAddVec3(vDir, fLength, leg->start, vStart);
			scaleAddVec3(vDir, -fLength, leg->start, vEnd);	
		}
		else
		{
			F32 fhwidth = leg->width * .5f;
			scaleAddVec3(leg->perp, fhwidth, leg->start, vStart);
			scaleAddVec3(leg->perp, -fhwidth, leg->start, vEnd);	
		}
	}
	else
	{
		F32 fhwidth = leg->width * .5f;

		scaleAddVec3(leg->perp, fhwidth, leg->end, vStart);
		scaleAddVec3(leg->perp, -fhwidth, leg->end, vEnd);
		
	}
}

// ------------------------------------------------------------------------------------------------------------------
// AUTO COMMANDS
// ------------------------------------------------------------------------------------------------------------------

// ------------------------------------------------------------------------------------------------------------------
AUTO_COMMAND;
void aiCivilianDumpHeatmap(int set)
{
	#define FILESTRLEN 128
	char szFilename[FILESTRLEN];

	if (set == 0)
	{
		g_eHeatMapSet = EAICivilianType_PERSON;
	}
	else
	{
		g_eHeatMapSet = EAICivilianType_CAR;
	}


	// create the file name
	{
		char *p;
		U32 currentTime = timeSecondsSince2000();

		snprintf(szFilename, FILESTRLEN, "c:\\temp\\%s%s.jpg",
			(g_eHeatMapSet==EAICivilianType_PERSON) ? "pedestrianHeatmap_" : "carHeatmap_",
			timeGetDateStringFromSecondsSince2000(currentTime) );

		// fixup the file name by removing all spaces and colons from the string
		p = szFilename + strlen("c:\\temp\\");
		while(*p)
		{
			if (*p == ' ' || *p == ':')
			{
				*p = '_';
			}
			else
			{
				p++;
			}
		}
	}

	{
		Vec3 min, max;
		char *pErrorString = NULL;
		S32 unitsPerPixel = 1;
		const int PEN_SIZE = 4;
		const int YELLOW_CUTOFF = 4;
		const int RED_CUTOFF = 8;

		aiCivHeatmapGetBoundingSizeAndUnitsPerPixel(min, max, &unitsPerPixel, true);

		gslWriteJpegHeatMapEx(szFilename, "CivilianDensity", min, max, unitsPerPixel, PEN_SIZE,
			YELLOW_CUTOFF, RED_CUTOFF, 300, &pErrorString);

		estrDestroy(&pErrorString);
	}



}


// ------------------------------------------------------------------------------------------------------------------
AUTO_COMMAND;
void aiCivScareNearbyPedestrians(Entity *e, F32 distance)
{
	PERFINFO_AUTO_START_FUNC();

	MAX1(distance, 40.f);

	if (e)
	{
		S32 i;
		Vec3 vScarePos;
		static Entity **eaEnts = NULL;

		entGetPos(e, vScarePos);

		entGridProximityLookupExEArray(entGetPartitionIdx(e), vScarePos, &eaEnts, distance, ENTITYFLAG_CIV_PROCESSING_ONLY, 0, NULL);

		for (i = 0; i < eaSize(&eaEnts); i++)
		{
			Entity *otherEnt = eaEnts[i];

			if (entCheckFlag(otherEnt, ENTITYFLAG_CIV_PROCESSING_ONLY))
			{
				aiCivScarePedestrian(otherEnt, e, vScarePos);
			}
		}

		eaClear(&eaEnts);
	}


	PERFINFO_AUTO_STOP();
}

// ------------------------------------------------------------------------------------------------------------------
AUTO_COMMAND;
void aiCivDebug(Entity *eClient)
{
	aiCivDebugRef = eClient->myRef;
}


// ------------------------------------------------------------------------------------------------------------------
typedef struct AICivCountWrapperData
{
	S32 type;
	S32 count;
} AICivCountWrapperData;

static void aiCivilian_SetCountWrapper(int iPartitionIdx, void *p)
{
	AICivilianPartitionState *pPartition = aiCivilian_GetPartitionState(iPartitionIdx);
	AICivCountWrapperData *pdata = (AICivCountWrapperData*)p;
	if (pPartition)
	{
		pPartition->civTypeRuntimeInfo[pdata->type].targetCount = pdata->count;
	}
}
// adds the given volume to every partition
void aiCivilian_SetCountForEachPartition(S32 type, S32 count)
{
	AICivCountWrapperData data;
	data.type = CLAMP(type, 0, EAICivilianType_COUNT-1);
	data.count = count;
	partition_ExecuteOnEachPartitionWithData(aiCivilian_SetCountWrapper, &data);
}

AUTO_COMMAND;
void aiCivPrintCounts(Entity *e)
{
	int i;
	int part;
	static char *estr = NULL;

	if(!g_civSharedState.pMapDef)
		return;

	for(part=0; part<eaSize(&s_eaCivilianPartitions); part++)
	{
		if(!s_eaCivilianPartitions[part])
			continue;

		for(i=0; i<EAICivilianType_COUNT; i++)
		{
			estrPrintf(&estr, "P(%d): %s: %d", part, 
												StaticDefineIntRevLookup(EAICivilianTypeEnum, i), 
												eaSize(&s_eaCivilianPartitions[part]->civilians[i]));
			if(e)
				ClientCmd_clientConPrint(e, estr);
			else
				printf("%s", estr);
		}
	}
}

// ------------------------------------------------------------------------------------------------------------------
AUTO_COMMAND;
void aiCivSetCount(int set, int count)
{
	if(g_civSharedState.pMapDef)
	{
		if (set <= EAICivilianType_NULL || set >= EAICivilianType_COUNT)
			set = EAICivilianType_PERSON;

		aiCivilian_SetCountForEachPartition(set, count);
	}

}

// ------------------------------------------------------------------------------------------------------------------
AUTO_COMMAND;
void aiCivilianDisable(int disable)
{
	g_civSharedState.bIsDisabled = !!disable;
}

// ------------------------------------------------------------------------------------------------------------------
AUTO_COMMAND;
void aiCivilianDisableCars(int disable)
{
	g_civSharedState.bCarsDisabled = !!disable;
	if (g_civSharedState.bCarsDisabled && g_civSharedState.pMapDef)
	{
		aiCivilian_SetCountForEachPartition(EAICivilianType_CAR, 0);
	}

}

// ------------------------------------------------------------------------------------------------------------------
AUTO_COMMAND;
void aiCivDrawPath(int disabled)
{
	s_debugDrawPath.drawCivPath = disabled;
	s_debugDrawPath.lastCivRef = 0;
	s_debugDrawPath.lastWaypointIdx = -1;
}

// ------------------------------------------------------------------------------------------------------------------
AUTO_COMMAND;
void aiCivDebugOnClick(Entity *eClient)
{
	Entity *civEnt = entFromEntityRefAnyPartition(aiDebugEntRef);
	if (civEnt)
		aiCivilianOnClick(civEnt, eClient);
}

// ------------------------------------------------------------------------------------------------------------------
AUTO_COMMAND;
void aiCivSetPosToLegmapPixel(Entity *eClient, int pixelX, int pixelY)
{
	Vec3 vMin, vMax, vGotoPos;
	S32 unitsPerPixel = 1;
	S32 iYPixels;

	// a very hacky goto pos for myself so I can find places in the city based on the heatmap

	aiCivHeatmapGetBoundingSizeAndUnitsPerPixel(vMin, vMax, &unitsPerPixel, true);
	iYPixels = (vMax[2] - vMin[2]) / unitsPerPixel + 1;

	vGotoPos[0] = vMin[0] + pixelX * unitsPerPixel;
	vGotoPos[2] = vMin[2] + (iYPixels - pixelY) * unitsPerPixel;
	vGotoPos[1] = 300.0f;

	entSetPos(eClient, vGotoPos, true, __FUNCTION__);
}


// ------------------------------------------------------------------------------------------------------------------
AUTO_COMMAND;
void aiCivPause(Entity *pEnt, S32 bPause)
{
	Entity *pCivEnt = entFromEntityRefAnyPartition(aiDebugEntRef);
	if (pCivEnt && entCheckFlag(pCivEnt, ENTITYFLAG_CIVILIAN))
	{
		if (pCivEnt->pCritter && pCivEnt->pCritter->civInfo)
			aiCivilianPauseMovement(pCivEnt->pCritter->civInfo, bPause);
	}
}


// ------------------------------------------------------------------------------------------------------------------
AUTO_EXPR_FUNC(ai) ACMD_NAME(SetHostileToCivilians);
ExprFuncReturnVal exprFuncSetHostileToCivilians(ACMD_EXPR_SELF Entity* e, ExprContext *context, ACMD_EXPR_ERRSTRING errString)
{
	AIVarsBase *aib = e->aibase;

	if (aib->isHostileToCivilians == false)
	{
		CommandQueue* exitHandlers = NULL;
		
		exprContextGetCleanupCommandQueue(context, &exitHandlers, NULL);

		if(!exitHandlers)
		{
			estrPrintf(errString, "Trying to call SetHostileToCivilians func without exitHandler reference");
			return ExprFuncReturnError;
		}

		aib->isHostileToCivilians = true;

		QueuedCommand_aiCivClearHostileToCivilians(exitHandlers, e);
	}

	return ExprFuncReturnFinished;
}

// ------------------------------------------------------------------------------------------------------------------
AUTO_COMMAND_QUEUED();
void aiCivClearHostileToCivilians(ACMD_POINTER Entity *e)
{
	if (e && e->aibase)
		e->aibase->isHostileToCivilians = false;
}

// ------------------------------------------------------------------------------------------------------------------
// gets the editing entity, which is set from calls to aiCivEditing_RequestCivClientMapDefInfo
static Entity* _getEditingEnt()
{
	if (s_erEditingEntity)
	{
		Entity *e = entFromEntityRefAnyPartition(s_erEditingEntity);
		if (!e)
		{
			s_erEditingEntity = 0;
			return NULL;
		}

		return e;
	}
	return NULL;
}

// ------------------------------------------------------------------------------------------------------------------
void aiCivEditing_ReportCivMapDefUnload()
{
	Entity *e = _getEditingEnt();
	if (e)
	{
		ClientCmd_gclCivNotifyDirtyMapDefInfo(e);
	}
}

// ------------------------------------------------------------------------------------------------------------------
static void aiCivEditing_ReportCivMapDefLoad()
{
	Entity *e = _getEditingEnt();
	if (e)
	{
		ClientCmd_gclCivNotifyDirtyMapDefInfo(e);
	}
}

// ------------------------------------------------------------------------------------------------------------------
AUTO_COMMAND ACMD_ACCESSLEVEL(9) ACMD_CATEGORY(AI) ACMD_SERVERCMD ACMD_PRIVATE;
void aiCivEditing_RequestCivClientMapDefInfo(Entity *editingEnt)
{
	if (editingEnt)
	{
		AICivClientMapDefInfo	clientMapDefInfo = {0};
				
		if (g_civSharedState.pMapDef)
		{
			FOR_EACH_IN_EARRAY_FORWARDS(g_civSharedState.pMapDef->legInfo.eaLegDef, AICivLegDef, pDef)
			{
				eaPush(&clientMapDefInfo.eapcLegDefNames, pDef->pchName);
			}
			FOR_EACH_END
		}

		ClientCmd_gclCivReceiveClientMapDefInfo(editingEnt, &clientMapDefInfo);
		
		eaDestroy(&clientMapDefInfo.eapcLegDefNames);
		
		s_erEditingEntity = entGetRef(editingEnt);
	}
}


// ------------------------------------------------------------------------------------------------------------------
void aiCivTraffic_DumpMissingTrafficLights(AICivRegenReport *report);
void aiCivTraffic_DumpMisclassifiedIntersections(AICivRegenReport *report);
void acgLegGenerateion_DumpDeadEndRoads(AICivRegenReport *report);

// dumps a txt file to c:\temp that will describe
AUTO_COMMAND ACMD_ACCESSLEVEL(9) ACMD_CATEGORY(AI) ACMD_SERVERCMD ACMD_PRIVATE;
void aiCivSendProblemAreas(Entity *editingEnt)
{
	AICivRegenReport report = {0};

	if (!editingEnt)
		return;

	aiCivTraffic_DumpMissingTrafficLights(&report);
	aiCivTraffic_DumpMisclassifiedIntersections(&report);
	acgLegGenerateion_DumpDeadEndRoads(&report);
	report.problemAreaRequest = true;
	
	ClientCmd_gclCivEditor_NotifyComplete(editingEnt, &report);
	StructDeInit(parse_AICivRegenReport, &report);
}

static bool aiCiv_GetAudioAssets_HandleString(const char *pcAddString, const char ***peaStrings)
{
	if (pcAddString)
	{
		bool bDup = false;
		FOR_EACH_IN_EARRAY(*peaStrings, const char, pcHasString) {
			if (strcmpi(pcHasString, pcAddString) == 0) {
				bDup = true;
			}
		} FOR_EACH_END;
		if (!bDup) {
			eaPush(peaStrings, strdup(pcAddString));
		}
		return true;
	}
	return false;
}


static void aiCiv_GetAudioAssets(const char **ppcType, const char ***peaStrings, U32 *puiNumData, U32 *puiNumDataWithAudio)
{
	AICivilianMapDef *pAICivMapDef;
	ResourceIterator rI;

	*ppcType = strdup("AICivVehicleTypeDef");

	resInitIterator(g_pcAICivDefDictName, &rI);
	while (resIteratorGetNext(&rI, NULL, &pAICivMapDef))
	{
		bool bResourceHasAudio = false;

		bResourceHasAudio |= aiCiv_GetAudioAssets_HandleString(pAICivMapDef->car.pchCarHonkSoundName,		peaStrings);
		bResourceHasAudio |= aiCiv_GetAudioAssets_HandleString(pAICivMapDef->trolley.pchCarHonkSoundName,	peaStrings);

		*puiNumData = *puiNumData + 1;
		if (bResourceHasAudio) {
			*puiNumDataWithAudio = *puiNumDataWithAudio + 1;
		}
	}
	resFreeIterator(&rI);
}

AUTO_RUN;
void aiCiv_GetAudioAssets_RegisterCallback(void)
{
	reportAudioAssets_AiCivilian_Callback = aiCiv_GetAudioAssets;
}
