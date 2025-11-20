#include "dynFxManager.h"
#include "dynFxPhysics.h"
#include "dynFxManager.h"
#include "rand.h"
#include "timing.h"
#include "WorldCell.h"
#include "WorldCellEntry.h"
#include "wlState.h"

#include "dynWind.h"
#include "dynWindCell.h"

AUTO_RUN_ANON(memBudgetAddMapping(__FILE__, BUDGET_FXSystem););

AUTO_CMD_INT(dynDebugState.bDrawWindGrid, dfxDrawWindGrid);
AUTO_CMD_FLOAT(dynDebugState.fDrawWindGridForceYLevel, dfxDrawWindGridForceYLevel);

#if _PS3
#define USE_SPU_WIND 1
#endif

static F32 fGlobalTime = 0.0f;

static SimplexNoise3DTable_x4 noiseTable_x4;

static DynWindQueuedForce** eaQueuedForces = 0;
static WorldWindSourceEntry** eaOpenWindSources = 0;
static F32 fMaxDistanceFromCamSquared = 20000.0f;
static F32 fGridDivisionDistance = 5.0f;

static DynWindSettings dynCurrentWind;

AUTO_CMD_FLOAT(fMaxDistanceFromCamSquared, dfxWindMaxDistanceFromCamSquared);
AUTO_CMD_FLOAT(fGridDivisionDistance, dfxWindGridDivisionDistance);
AUTO_CMD_FLOAT(dynCurrentWind.fMag, dfxWindBaseMag);
AUTO_CMD_FLOAT(dynCurrentWind.fMagRange, dfxWindBaseMagRange);

static float dfxWindForceScale = 0.04f;
AUTO_CMD_FLOAT(dfxWindForceScale, dfxWindForceScale);

static float dfxWindQueuedForceScale = 0.5f;
static float dfxWindQueuedForceOffsetScale = 8.0f;
static float dfxWindQueuedForceSpeedScale = 0.01f;
static float dfxWindQueuedForceMaxMag = 2.5f;
AUTO_CMD_FLOAT(dfxWindQueuedForceScale, dfxWindQueuedForceScale);
AUTO_CMD_FLOAT(dfxWindQueuedForceOffsetScale, dfxWindQueuedForceOffsetScale);
AUTO_CMD_FLOAT(dfxWindQueuedForceSpeedScale, dfxWindQueuedForceSpeedScale);
AUTO_CMD_FLOAT(dfxWindQueuedForceMaxMag, dfxWindQueuedForceMaxMag);

static float dfxWindForceMaxPower = 10.0f;
static float dfxWindForceMaxDisplacement = 100.0f;
static float dfxWindForceMaxFreq = 5.0f;

AUTO_CMD_FLOAT(dfxWindForceMaxPower, dfxWindForceMaxPower);
AUTO_CMD_FLOAT(dfxWindForceMaxDisplacement, dfxWindForceMaxDisplacement);
AUTO_CMD_FLOAT(dfxWindForceMaxFreq, dfxWindForceMaxFreq);

#if _XBOX
static bool dfxForceDisableWind = true;
#else
static bool dfxForceDisableWind = false;
#endif
AUTO_CMD_INT(dfxForceDisableWind, disableWind) ACMD_CMDLINE ACMD_CATEGORY(Debug);

static int dfxWindGridSize = 0;
static DynWindSampleGridCell* dfxWindGrid = 0;
static Vec3 dfxWindLastCameraPos  = {0,0,0};
static Vec3 dfxWindLastUpdateCameraPos = {0,0,0};


static dynWindGlobalInputs windInputs;

static bool windWasOnLastUpdate = false;

void InitWindInputs(void);
void UpdateWindInputs( F32 fDeltaTime );
void FreeWindInputs( void );

#if USE_SPU_WIND
extern void RunSpuWind( const dynWindGlobalInputs *pInput, DynWindSampleGridCell *dstCells);
extern void WaitSpuWindComplete(void);
extern bool dynWindSpuJobsCreate();

#define WIND_ALIGNED_MALLOC(a,b) malloc_aligned(a,b,"Main:DynWind")
#define WIND_ALIGNED_FREE(a) free_aligned(a, "Main:DynWind")
#else
#define WIND_ALIGNED_MALLOC(a,b) malloc(a)
#define WIND_ALIGNED_FREE(a) free(a)
#endif


__forceinline static void CopyRowLeft(int srcRow, int dstRow, int offset)
{
	// work left to right
	int						numCells = dfxWindGridSize - offset;
	DynWindSampleGridCell*	pSrc = &dfxWindGrid[srcRow * dfxWindGridSize + offset];
	DynWindSampleGridCell*	pDst = &dfxWindGrid[dstRow * dfxWindGridSize];
	int						i;

	for(i=0;i<numCells;i++)
	{
		memcpy(pDst, pSrc, sizeof(DynWindSampleGridCell));
		pDst++;
		pSrc++;
	}
	
	// zero out right part of row
	memset(&dfxWindGrid[dstRow*dfxWindGridSize + (dfxWindGridSize - offset)], 0, sizeof(DynWindSampleGridCell)*offset);
}

__forceinline static void CopyRowRight(int srcRow, int dstRow, int offset)
{
	// work right to left
	int numCells = dfxWindGridSize - offset;
	DynWindSampleGridCell*	pSrc = &dfxWindGrid[srcRow*dfxWindGridSize + (dfxWindGridSize - offset - 1)];
	DynWindSampleGridCell*	pDst = &dfxWindGrid[dstRow*dfxWindGridSize + dfxWindGridSize - 1];
	int						i;

	for(i=0;i<numCells;i++)
	{
		memcpy(pDst, pSrc, sizeof(DynWindSampleGridCell));
		pDst--;
		pSrc--;
	}

	// zero out left part of row
	memset(&dfxWindGrid[dstRow*dfxWindGridSize], 0, sizeof(DynWindSampleGridCell)*offset);
}

static void dynWindUpdateGrid(F32 fDeltaTime)
{
	int newWindGridSize = (int)(sqrtf(fMaxDistanceFromCamSquared)*2.0f/fGridDivisionDistance) + 1;

#if USE_SPU_WIND
	// on PS3 we are going to DMA this and need it QW aligned.  This works if we round to mulitple of 4 cells
	newWindGridSize = (newWindGridSize + 2)&(~0x3);
	assert((newWindGridSize*sizeof(DynWindSampleGridCell)&0xf) == 0);
#endif
	
	dfxWindLastCameraPos[0] = floor(dfxWindLastCameraPos[0] / fGridDivisionDistance) * fGridDivisionDistance;
	dfxWindLastCameraPos[1] = floor(dfxWindLastCameraPos[1] / fGridDivisionDistance) * fGridDivisionDistance;
	dfxWindLastCameraPos[2] = floor(dfxWindLastCameraPos[2] / fGridDivisionDistance) * fGridDivisionDistance;

	if (!dfxWindGrid || newWindGridSize != dfxWindGridSize)
	{
		if (dfxWindGrid)
			WIND_ALIGNED_FREE(dfxWindGrid);
		
		dfxWindGridSize = newWindGridSize;
		dfxWindGrid = WIND_ALIGNED_MALLOC(newWindGridSize*newWindGridSize*sizeof(DynWindSampleGridCell),16);
		memset(dfxWindGrid, 0, newWindGridSize*newWindGridSize*sizeof(DynWindSampleGridCell));
	}
	else
	{
		int cellOffsetX = -(int)((dfxWindLastCameraPos[0] - dfxWindLastUpdateCameraPos[0])/fGridDivisionDistance);
		int cellOffsetZ = -(int)((dfxWindLastCameraPos[2] - dfxWindLastUpdateCameraPos[2])/fGridDivisionDistance);

		if (cellOffsetX >= dfxWindGridSize || cellOffsetZ >= dfxWindGridSize || cellOffsetX <= -dfxWindGridSize || cellOffsetZ <= -dfxWindGridSize )
		{
			// shift is greater than grid size, just clear it
			memset(dfxWindGrid, 0, newWindGridSize*newWindGridSize*sizeof(DynWindSampleGridCell));
		}
		else if (cellOffsetX || cellOffsetZ)
		{
			int z;
			assert(!(sizeof(DynWindSampleGridCell)&3));		// size is multiple of 4 bytes

			if (cellOffsetZ < 0)
			{
				// moving up, work top to bottom
				if(cellOffsetX>=0)
				{
					for(z = -cellOffsetZ ; z < dfxWindGridSize ; z++)
						CopyRowRight(z, z+cellOffsetZ, cellOffsetX);
				}
				else
				{
					for(z = -cellOffsetZ ; z < dfxWindGridSize ; z++)
						CopyRowLeft(z, z+cellOffsetZ, -cellOffsetX);
				}
				// zero out bottom block
				memset(&dfxWindGrid[(dfxWindGridSize+cellOffsetZ) * dfxWindGridSize], 0, (-cellOffsetZ)*dfxWindGridSize*sizeof(DynWindSampleGridCell));
			}
			else
			{
				// moving down, work bottom to top
				if(cellOffsetX>=0)
				{
					for(z = dfxWindGridSize - cellOffsetZ -1; z>=0 ; z--)
						CopyRowRight(z, z+cellOffsetZ, cellOffsetX);
				}
				else
				{
					for(z = dfxWindGridSize - cellOffsetZ -1; z>=0 ; z--)
						CopyRowLeft(z, z+cellOffsetZ, -cellOffsetX);
				}
				// zero out top block
				memset(&dfxWindGrid[0], 0, cellOffsetZ*dfxWindGridSize*sizeof(DynWindSampleGridCell));
			}
		}
	}

	copyVec3(dfxWindLastCameraPos, dfxWindLastUpdateCameraPos);

	//update the cells
	{
#if !USE_SPU_WIND
		int x,z;

		UpdateWindInputs( fDeltaTime );
		for (z = 0; z < dfxWindGridSize; z++)
		{
			DynWindSampleGridCell *pCell = &dfxWindGrid[z * dfxWindGridSize];
			for (x = 0; x < dfxWindGridSize; x++)
			{
				int signedx = x - (dfxWindGridSize-1)/2;
				int signedz = z - (dfxWindGridSize-1)/2;
				Vec3 pos = {(float)signedx * fGridDivisionDistance, 0, (float)signedz * fGridDivisionDistance};
				addVec3(pos, dfxWindLastUpdateCameraPos, pos);

				dynWindUpdateGridCell(&windInputs, pCell, pos);

				pCell++;
			}
		}
#else
		UpdateWindInputs( fDeltaTime );
		RunSpuWind( &windInputs, dfxWindGrid);
#endif
		FreeWindInputs();
	}
}

void dynWindUpdate(F32 fDeltaTime)
{
	const WorldRegionWindRules* windRules;
	PERFINFO_AUTO_START("dynWindUpdate", 1);

	fGlobalTime += fDeltaTime;

	copyVec3(wl_state.last_camera_frustum.cammat[3], dfxWindLastCameraPos);
	windRules = worldRegionRulesGetWindRules(worldRegionGetRules(worldGetWorldRegionByPos(dfxWindLastCameraPos)));
	windWasOnLastUpdate = !(dfxForceDisableWind || dynCurrentWind.bDisabled || windRules->disabled);

	if (windWasOnLastUpdate)
	{
		dynWindUpdateGrid(fDeltaTime);
	}
	
	//clear out the queued forces
	eaClearEx(&eaQueuedForces, 0);

	PERFINFO_AUTO_STOP();
}

void dynWindWaitUpdateComplete(void)
{
#if USE_SPU_WIND
	WaitSpuWindComplete();
#endif
}

extern void WindNoiseInit();
extern void WindNoiseDestroy();

void dynWindStartup(void)
{
	fGlobalTime = 0.0f;

	simplexNoise3DTable_x4Init(noiseTable_x4);
	WindNoiseInit();

	setVec3(dynCurrentWind.vDir, 0.707f, 0, 0.707f);
	setVec3(dynCurrentWind.vDirRange, 0.5f, 0.4f, 0.5f);
	dynCurrentWind.fMag = 1.5f;
	dynCurrentWind.fMagRange = 1.5f;
	dynCurrentWind.fChangeRate = 0.5f;
	dynCurrentWind.bDisabled = true;

	InitWindInputs();

#if USE_SPU_WIND
	dynWindSpuJobsCreate();
#endif
}

__forceinline static F32 frac(F32 v)
{
	return v - floorf(v);
}

F32 dynWindGetAtPositionPastEdge( const Vec3 vPosition, Vec3 vWindDir, bool isSmallObject) {

	F32 signedx = (vPosition[0] - dfxWindLastUpdateCameraPos[0])/fGridDivisionDistance;
	F32 signedz = (vPosition[2] - dfxWindLastUpdateCameraPos[2])/fGridDivisionDistance;
	F32 x = signedx + (F32)((dfxWindGridSize-1)/2);
	F32 z = signedz + (F32)((dfxWindGridSize-1)/2);
	Vec3 vEdgePos = {0, 0, 0};

	if (x < 0)
		x = 0;
	if (x >= dfxWindGridSize)
		x = dfxWindGridSize - 1;
	if (z < 0)
		z = 0;
	if (z >= dfxWindGridSize)
		z = dfxWindGridSize - 1;

	x = x - (F32)((dfxWindGridSize-1)/2);
	z = z - (F32)((dfxWindGridSize-1)/2);

	vEdgePos[0] = (x * fGridDivisionDistance) + dfxWindLastUpdateCameraPos[0];
	vEdgePos[1] = vPosition[1];
	vEdgePos[2] = (z * fGridDivisionDistance) + dfxWindLastUpdateCameraPos[2];

	return dynWindGetAtPosition(vEdgePos, vWindDir, isSmallObject);

}

F32 dynWindGetAtPosition( const Vec3 vPosition, Vec3 vWindDir, bool isSmallObject)
{
	F32 signedx = (vPosition[0] - dfxWindLastUpdateCameraPos[0])/fGridDivisionDistance;
	F32 signedz = (vPosition[2] - dfxWindLastUpdateCameraPos[2])/fGridDivisionDistance;
	F32 x = signedx + (F32)((dfxWindGridSize-1)/2);
	F32 z = signedz + (F32)((dfxWindGridSize-1)/2);

	if (x >= 0 && x < dfxWindGridSize && z >= 0 && z < dfxWindGridSize)
	{
		int intx = (int)x;
		int intz = (int)z;
		//do a bilinear interpolation
		F32 topLeftAmt = (1.0-frac(x)) * (1.0-frac(z));
		F32 bottomRightAmt = frac(x) * frac(z);
		F32 bottomLeftAmt = (1.0-frac(x)) * frac(z);
		F32 topRightAmt = frac(x) * (1.0-frac(z));

		setVec3same(vWindDir, 0);

		//top left sample
		scaleAddVec3(dfxWindGrid[intz*dfxWindGridSize+intx].currentWindVec, topLeftAmt, vWindDir, vWindDir);
		if (isSmallObject)
		{
			scaleAddVec3(dfxWindGrid[intz*dfxWindGridSize+intx].currentWindVecSmall, topLeftAmt, vWindDir, vWindDir);
		}

		//bottom right sample
		if (intz + 1 < dfxWindGridSize && intx + 1 < dfxWindGridSize)
		{
			scaleAddVec3(dfxWindGrid[(intz+1)*dfxWindGridSize+intx+1].currentWindVec, bottomRightAmt, vWindDir, vWindDir);
			if (isSmallObject)
			{
			scaleAddVec3(dfxWindGrid[(intz+1)*dfxWindGridSize+intx+1].currentWindVecSmall, bottomRightAmt, vWindDir, vWindDir);
		}
		}

		//bottom left sample
		if (intz + 1 < dfxWindGridSize)
		{
			scaleAddVec3(dfxWindGrid[(intz+1)*dfxWindGridSize+intx].currentWindVec, bottomLeftAmt, vWindDir, vWindDir);
			if (isSmallObject)
			{
				scaleAddVec3(dfxWindGrid[(intz+1)*dfxWindGridSize+intx].currentWindVecSmall, bottomLeftAmt, vWindDir, vWindDir);
		}
		}
		
		//top right sample
		if (intx + 1 < dfxWindGridSize)
		{
			scaleAddVec3(dfxWindGrid[(intz)*dfxWindGridSize+intx+1].currentWindVec, topRightAmt, vWindDir, vWindDir);
			if (isSmallObject)
			{
				scaleAddVec3(dfxWindGrid[(intz)*dfxWindGridSize+intx+1].currentWindVecSmall, topRightAmt, vWindDir, vWindDir);
			}
		}
		return normalVec3(vWindDir);
	}
	else
	{
		setVec3same(vWindDir, 0);
		return 0;
	}
}

F32 dynWindGetMaxWind( void )
{
	return dynCurrentWind.fMag + fabsf(dynCurrentWind.fMagRange);
}

__forceinline static void setupCurrentWindParams(const Vec4 wind_params, const Vec3 pos, Vec4 out_inst_params, bool is_small_object)
{
	//wind params are [0] = wind amt, [1] = bending, [3] = flapping, [2] = pivot offset
	Vec3 wind_vec;
	U32 *pos_vec = (U32*)pos;
	U32 pos_seed = pos_vec[0] ^ pos_vec[1] ^ pos_vec[2];
	F32 mag = dynWindGetAtPosition(pos, wind_vec, is_small_object) * wind_params[0];

	out_inst_params[0] = wind_params[1] / 2.0001f + floor(wind_params[3] / 2.0f * 100.0f); //wind bend amt and rustling amount mixed together (they both are 0-2)
	out_inst_params[1] = atan2f(wind_vec[0], wind_vec[2]); //wind with mag inside 
	out_inst_params[2] = (float)pos_seed; //wind phase
	out_inst_params[3] = mag; //this is multiplied by the wind fading code later
}


void dynWindUpdateCurrentWindParamsForWorldCell( WorldCell* cell, const F32 *camera_positions, int camera_position_count )
{
	void*** drawableLists[] = {&cell->drawable.drawable_entries, &cell->drawable.near_fade_entries, &cell->drawable.editor_only_entries};
	int i,j;
	F32 smallObjRadius = zmapInfoGetWindLargeObjectRadiusThreshold(NULL);

	PERFINFO_AUTO_START("dynWindUpdateCurrentWindParamsForWorldCell", 1);

	//assume the first camera is the user's camera
	if (windWasOnLastUpdate)
	{
		for (i = 0; i < ARRAY_SIZE(drawableLists); ++i)
		{
			for (j = eaSize(drawableLists[i]) - 1; j >= 0; --j)
			{
				WorldDrawableEntry* pDrawable = (*drawableLists[i])[j];
				F32	drawable_dist_sqrd = 8e16;
				int k;

				for (k = 0; k < camera_position_count; ++k)
				{
					Vec2 worldMidXZ = {pDrawable->base_entry.bounds.world_mid[0], pDrawable->base_entry.bounds.world_mid[2]};
					Vec2 camPosXZ = {camera_positions[k*3], camera_positions[k*3+2]};

					F32 dist_sqrd = distance2Squared(worldMidXZ, camPosXZ);
					MIN1(drawable_dist_sqrd, dist_sqrd);
				}

				if (drawable_dist_sqrd > fMaxDistanceFromCamSquared) continue;

				if (pDrawable->base_entry.type == WCENT_MODEL)
				{
					WorldModelEntry* pModel = (WorldModelEntry*)pDrawable;
					bool smallObj = false;

					if (nearSameF32(pModel->wind_params[0], 0)) continue; //there is no wind contribution here
					smallObj = (pModel->base_drawable_entry.base_entry.shared_bounds->radius < smallObjRadius);
					setupCurrentWindParams(pModel->wind_params, pModel->base_drawable_entry.base_entry.bounds.world_matrix[3], pModel->current_wind_parameters, smallObj);			
				}
				else if (pDrawable->base_entry.type == WCENT_MODELINSTANCED)
				{
					WorldModelInstanceEntry* pModelInst = (WorldModelInstanceEntry*)pDrawable;
					bool smallObj = false;
					FOR_EACH_IN_EARRAY(pModelInst->instances, WorldModelInstanceInfo, pInstanceInfo)
					{
						if (nearSameF32(pModelInst->wind_params[0], 0)) continue; //there is no wind contribution here
						smallObj = (pModelInst->base_drawable_entry.base_entry.shared_bounds->radius < smallObjRadius);
						setupCurrentWindParams(pModelInst->wind_params, pInstanceInfo->world_mid, pInstanceInfo->current_wind_parameters, smallObj);			
					}
					FOR_EACH_END;
				}
			}
		}
	}
	PERFINFO_AUTO_STOP();
}


void dynWindStartWindSource( WorldWindSourceEntry* wind_entry )
{
	eaPush(&eaOpenWindSources, wind_entry);
}

void dynWindStopWindSource( WorldWindSourceEntry* wind_entry )
{
	eaFindAndRemoveFast(&eaOpenWindSources, wind_entry);
}

void dynWindQueueMovingObjectForce( const Vec3 vPosition, const Vec3 vVelocity, F32 fRadius, bool onlyAffectSmallObjects )
{
	DynWindQueuedForce* pQf = calloc(1, sizeof(DynWindQueuedForce));
	copyVec3(vPosition, pQf->pos);
	copyVec3(vVelocity, pQf->velocity);
	pQf->radius = fRadius;
	pQf->onlySmallObjects = onlyAffectSmallObjects;

	eaPush(&eaQueuedForces, pQf);
}

const DynWindSettings* dynWindGetCurrentSettings( void )
{
	return &dynCurrentWind;
}

void dynWindSetCurrentSettings( DynWindSettings* settings )
{
	memcpy(&dynCurrentWind, settings, sizeof(DynWindSettings));
}

F32 dynWindGetSampleGridExtents()
{
	return sqrtf(fMaxDistanceFromCamSquared);
}

F32 dynWindGetSampleGridDivSize()
{
	return fGridDivisionDistance;
}

F32 dynWindGetSampleGridExtentsSqrd()
{
	return fMaxDistanceFromCamSquared;
}

bool dynWindGetEnabled( void )
{
	return !(dfxForceDisableWind);
}

void dynWindSetEnabled( bool enabled )
{
	dfxForceDisableWind = !enabled;
}

bool dynWindIsWindOn( void )
{
	return windWasOnLastUpdate;
}

void InitWindInputs(void)
{
	memset(&windInputs, 0,  sizeof(windInputs));
	windInputs.fGlobalTime = fGlobalTime;

	memcpy(windInputs.noiseTable_x4, noiseTable_x4, sizeof(SimplexNoise3DTable_x4));   // todo: just init table here
}

void UpdateWindInputs( F32 fDeltaTime )
{
	windInputs.fDeltaTime = fDeltaTime;
	windInputs.fGlobalTime += fDeltaTime;
	windInputs.settings = dynCurrentWind;

	windInputs.gridSize = dfxWindGridSize;
	windInputs.gridDivisionDistance = fGridDivisionDistance;

	copyVec3(dfxWindLastUpdateCameraPos, windInputs.cameraPos);
	// check for special case of vectors = 0 and disable (avoids simplex lookup)
	if (dynCurrentWind.vDirRange[0]+dynCurrentWind.vDirRange[1]+dynCurrentWind.vDirRange[2] == 0.0f)
	{
		windInputs.settings.bDisabled = true;
	}
	else
	{
		windInputs.settings.bDisabled = false;
	}

	// copy over constants (even though they change infrequently
	windInputs.queuedForceScale			= dfxWindQueuedForceScale;
	windInputs.queuedForceMaxMag		= dfxWindQueuedForceMaxMag;
	windInputs.queuedForceSpeedScale	= dfxWindQueuedForceSpeedScale;
	windInputs.queuedForceOffsetScale	= dfxWindQueuedForceOffsetScale;
	windInputs.forceMaxPower			= dfxWindForceMaxPower;
	windInputs.forceMaxDisplacement		= dfxWindForceMaxDisplacement;
	windInputs.forceMaxFreq				= dfxWindForceMaxFreq;
	windInputs.forceScale				= dfxWindForceScale;
	
	// Additional Wind SourcesdynCurrentWind
	windInputs.numWindSources = eaSize(&eaOpenWindSources);
	if(windInputs.numWindSources > 0)
	{
		WindSourceInput		*pW;

		windInputs.pWindSources = (WindSourceInput *)WIND_ALIGNED_MALLOC(sizeof(WindSourceInput)*windInputs.numWindSources,16);
		assert(windInputs.pWindSources);
		pW = windInputs.pWindSources;
	
		FOR_EACH_IN_EARRAY(eaOpenWindSources, WorldWindSourceEntry, pSource)
		{
			pW->effect_type		= pSource->source_data.effect_type;
			pW->radius			= pSource->source_data.radius;
			pW->radius_inner	= pSource->source_data.radius_inner;
			copyVec3(pSource->base_entry.bounds.world_mid, pW->world_mid);

			copyMat4(pSource->base_entry.bounds.world_matrix, pW->world_matrix);
			pW->turbulence = pSource->source_data.turbulence;
			pW->speed = pSource->source_data.speed;
			pW->speed_variation = pSource->source_data.speed_variation;
			copyVec3(pSource->source_data.direction_variation, pW->direction_variation);

			pW++;
		}
		FOR_EACH_END;
	}

	// queued forces
	windInputs.numQueuedForces = eaSize(&eaQueuedForces);
	if(windInputs.numQueuedForces > 0)
	{
		DynWindQueuedForce	*pQ;

		windInputs.pQueuedForces = (DynWindQueuedForce *)WIND_ALIGNED_MALLOC(sizeof(DynWindQueuedForce)*windInputs.numQueuedForces,16);
		assert(windInputs.pQueuedForces);
		pQ = windInputs.pQueuedForces;

		FOR_EACH_IN_EARRAY(eaQueuedForces, DynWindQueuedForce, pForce)
		{
			memcpy(pQ, pForce, sizeof(DynWindQueuedForce));
			pQ++;
		}
		FOR_EACH_END;
	}

	// force payloads : search through regions for for special effects that have forces that get appied to wind oscillators
	{
		DynFxManager* pFxMgr = dynFxGetGlobalFxManager(dfxWindLastUpdateCameraPos);
		if (pFxMgr)
		{
			DynFxRegion* pFxRegion = dynFxManGetDynFxRegion(pFxMgr);
			if (pFxRegion)
			{
				int numPayloads = eaSize(&pFxRegion->eaForcePayloads[pFxRegion->uiCurrentPayloadArray]);
				
				if(numPayloads > 0)
				{
					DynForcePayload *pF;
					int numForcePayloadsInRange = 0;
					int i = 0;
					char *payloadsInRangeMask = (char *)alloca(numPayloads);

					// make a pass through force payloads and mark any that are in range of camera.  I hope hard coded array
					FOR_EACH_IN_EARRAY(pFxRegion->eaForcePayloads[pFxRegion->uiCurrentPayloadArray], DynForcePayload, pForcePayload)
					{
						Vec3 diff;
						subVec3(dfxWindLastUpdateCameraPos, pForcePayload->vForcePos, diff);
						if (lengthVec3Squared(diff) < fMaxDistanceFromCamSquared + (pForcePayload->force.fForceRadius*pForcePayload->force.fForceRadius))
						{
							payloadsInRangeMask[i] = 1;
							numForcePayloadsInRange++;
						}
						else
						{
							payloadsInRangeMask[i] = 0;
						}
						i++;
					}
					FOR_EACH_END;

					windInputs.numForcePayloads = numForcePayloadsInRange;

					// now loop through and add subset that are in range into list
					if(windInputs.numForcePayloads > 0)
					{
						windInputs.pForcePayloads = (DynForcePayload *)WIND_ALIGNED_MALLOC(windInputs.numForcePayloads*sizeof(DynForcePayload),16);
						assert(windInputs.pForcePayloads);
						pF = windInputs.pForcePayloads;

						i=0;
						FOR_EACH_IN_EARRAY(pFxRegion->eaForcePayloads[pFxRegion->uiCurrentPayloadArray], DynForcePayload, pForcePayload)
						{
							if(payloadsInRangeMask[i])
							{
								memcpy(pF, pForcePayload, sizeof(DynForcePayload));
								pF++;
							}
							i++;
						}
						FOR_EACH_END;
					}
				}
			}
		}
	}
}


void FreeWindInputs( void )
{
	windInputs.numWindSources = 0;
	windInputs.numForcePayloads = 0;
	windInputs.numQueuedForces = 0;

	if(windInputs.pWindSources)
	{
		WIND_ALIGNED_FREE(windInputs.pWindSources);
		windInputs.pWindSources = 0;
	}

	if(windInputs.pQueuedForces)
	{
		WIND_ALIGNED_FREE(windInputs.pQueuedForces);
		windInputs.pQueuedForces = 0;
	}

	if(windInputs.pForcePayloads)
	{
		WIND_ALIGNED_FREE(windInputs.pForcePayloads);
		windInputs.pForcePayloads = 0;
	}
}

#include "dynWind_h_ast.c"