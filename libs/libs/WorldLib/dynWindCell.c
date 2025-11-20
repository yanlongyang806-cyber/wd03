#include "dynFxPhysics.h"
#include "TriCube/vec.h"
#include "dynWind.h"
#include "dynWindCell.h"


__forceinline static F32 RandomF32(U32 *seed)
{
	U32 uiTemp = (*seed * 214013L + 2531011L) & 0xFFFFFFFF;
	*seed = ((uiTemp * 214013L + 2531011L) & 0xFFFF0000) >> 16 | (uiTemp & 0xFFFF0000);

	return ((S32)*seed) * (1.f / (F32)0x7fffffffUL);
}

static void dynWindUpdateOscillator(const dynWindGlobalInputs *pInput, DynWindOscillator* pOss, bool bAddNoise)
{
	F32 fNewValue;
	if (bAddNoise)
	{
		pOss->velocity += RandomF32(&pOss->seed)*0.005f*pInput->fDeltaTime;
		pOss->velocity = pOss->velocity*0.9f + 0.1f;
		pOss->amplitude += RandomF32(&pOss->seed)*0.005f*pInput->fDeltaTime;
		pOss->amplitude = pOss->amplitude*0.9f + 0.1f;
	}
	
	fNewValue = cos(pInput->fGlobalTime * pOss->velocity) * pOss->amplitude + pOss->offset;
	pOss->value = pOss->value*0.75f + fNewValue*0.25f;	
}

__forceinline static F32 smoothstep(F32 minVal, F32 maxVal, F32 val)
{
	F32 f = saturate((val - minVal)/(maxVal - minVal));
	return f * f * (3.0f - 2.0f * f);
}

__forceinline static F32 linstep(F32 minVal, F32 maxVal, F32 val)
{
	F32 f = saturate((val - minVal)/(maxVal - minVal));
	return f;
}

__forceinline static F32 getSourceEntryAtten(WindSourceInput* pSource, const Vec3 vPos)
{
	Vec3 vTmp;
	F32 fLen;
	subVec3(pSource->world_mid, vPos, vTmp);
	fLen = lengthVec3XZ(vTmp);
	return smoothstep(pSource->radius, pSource->radius_inner, fLen);
	//for some reason the profiler says using the linear step is slower even though it's simpler
	//return linstep(pEntry->source_data.radius, pEntry->source_data.radius_inner, fLen);
}

static void getBaseRandomVectorAtPosition(const Vec4 noise, DynWindSettings* pCurSettings, Vec3 vWindDir)
{
	F32 fWindMag;

	vWindDir[0] = pCurSettings->vDir[0] + pCurSettings->vDirRange[0]*noise[0];
	vWindDir[1] = pCurSettings->vDir[1] + pCurSettings->vDirRange[1]*noise[1];
	vWindDir[2] = pCurSettings->vDir[2] + pCurSettings->vDirRange[2]*noise[2];

	normalVec3(vWindDir);

	//take the absolute value since having wind go in the other direction causes weird snapping artifacts since other stuff uses it
	//due to the grid interpolation
	fWindMag = fabsf(pCurSettings->fMag + pCurSettings->fMagRange * noise[3]); 
	scaleVec3(vWindDir, fWindMag, vWindDir);
}

#define TABLE_SIZE  128

static SimplexNoiseTable * g_pNoiseTable;

static Vec4 g_afTable[TABLE_SIZE*TABLE_SIZE];

void WindNoiseInit()
{
	int i,j,k;

	g_pNoiseTable = simplexNoiseTableCreate(64);

	for (i=0;i<TABLE_SIZE;i++)
	{
		for (j=0;j<TABLE_SIZE;j++)
		{
			for (k=0;k<4;k++)
			{
				g_afTable[i+j*TABLE_SIZE][k] = (rule30Float()+1.0f)*0.5f;
			}
		}
	}
}

// guess we never shut down
/*void WindNoiseDestroy()
{
	simplexNoiseTableDestroy(g_pNoiseTable);
}*/

// this function is super trusting
static F32 DumbModf(F32 fVal,F32 fDivisor)
{
	F32 fQuotient = floorf(fVal / fDivisor);

	return fVal - fQuotient*fDivisor;
}

// This function is a big fat fake-up that I made up.  Calling simplexNoise3D_x4 every frame is slow.  If you can replace this with
// something that is both "better" and fast, please do.  [RMARR - 5/22/12]
static void FastWindNoise(F32 fTime, F32 x,F32 z, Vec4 vNoise)
{
	int i;
	// this offsetting is pretty crappy
	int iIndexX1 = (int)((x+3e4f)*10.0f)%TABLE_SIZE;
	int iIndexZ1 = (int)((z+3e4f)*10.0f)%TABLE_SIZE;

	F32 fFudge = (simplexNoise2D(g_pNoiseTable,fTime,iIndexX1+iIndexZ1)+1.0f)*0.1f;
	F32 fUseTime = fTime+fFudge;
	for (i=0;i<4;i++)
	{
		vNoise[i] = DumbModf(g_afTable[iIndexX1+iIndexZ1*TABLE_SIZE][i]*4.0f+fUseTime*3.0f,4.0f);
		if (vNoise[i] > 2.0f)
		{
			vNoise[i] = 4.0f-vNoise[i];
		}
		vNoise[i] -= 1.0f;
	}
}

static void dynWindGetBaseVectorAtPosition( const dynWindGlobalInputs *pInput, const Vec3 vPosition, Vec3 vWindDir)
{
	DynWindSettings effectiveSettings = pInput->settings;
	int i;
	WindSourceInput	*pSource;
	Vec4 noise;
	bool noiseInit = false;

	// check if there are any wind overrides within range of this cell and override global wind
	for(i=0, pSource=pInput->pWindSources ; i<pInput->numWindSources ; i++, pSource++)
	{
		F32 fAtten;
		Vec3 vDirVec = {0,0,-1};
		Vec3 vWorldDirVec;

		if (pSource->effect_type != WorldWindEffectType_Override) continue;
		fAtten = getSourceEntryAtten(pSource, vPosition);
		if (fAtten < 0.001f) continue;
		
		mulVecMat3(vDirVec, pSource->world_matrix, vWorldDirVec);
		normalVec3(vWorldDirVec);

		effectiveSettings.bDisabled = false;
		effectiveSettings.fChangeRate = lerp(effectiveSettings.fChangeRate, pSource->turbulence, fAtten);
		effectiveSettings.fMag = lerp(effectiveSettings.fMag, pSource->speed, fAtten);
		effectiveSettings.fMagRange = lerp(effectiveSettings.fMagRange, pSource->speed_variation, fAtten);
		LERP3(effectiveSettings.vDir, effectiveSettings.vDir, vWorldDirVec, fAtten);
		LERP3(effectiveSettings.vDirRange, effectiveSettings.vDirRange, pSource->direction_variation, fAtten);

		break; //only do one override. There's no obvious way to combine them except picking the largest and that's too slow here I think
	}

	//get the base global wind 
	if (!effectiveSettings.bDisabled)
	{
		// calc simplex noise.  This is done separately so it can be reused later if needed
		F32 fTime = pInput->fGlobalTime*effectiveSettings.fChangeRate;
		const F32 fSpatialScale = effectiveSettings.fSpatialScale;
		FastWindNoise(fTime, fSpatialScale * vPosition[0], fSpatialScale * vPosition[2], noise);
		noiseInit = true;

		getBaseRandomVectorAtPosition(noise, &effectiveSettings, vWindDir);
	}
	else
	{
		vWindDir[0] = effectiveSettings.vDir[0];
		vWindDir[1] = effectiveSettings.vDir[1];
		vWindDir[2] = effectiveSettings.vDir[2];

		normalVec3(vWindDir);

		scaleVec3(vWindDir, effectiveSettings.fMag, vWindDir);
	}

	for(i=0, pSource=pInput->pWindSources ; i<pInput->numWindSources ; i++, pSource++)
	{
		F32 fAtten;
		Vec3 vDirVec = {0,0,-1};
		Vec3 vCurVec;

		if (pSource->effect_type == WorldWindEffectType_Override) continue; //skip the overrides since we did those before
		fAtten = getSourceEntryAtten(pSource, vPosition);
		if (fAtten < 0.001f) continue;
		
		mulVecMat3(vDirVec, pSource->world_matrix, effectiveSettings.vDir);
		normalVec3(effectiveSettings.vDir);

		effectiveSettings.bDisabled = false;
		effectiveSettings.fChangeRate = pSource->turbulence;
		effectiveSettings.fMag = pSource->speed;
		effectiveSettings.fMagRange = pSource->speed_variation;
		copyVec3(pSource->direction_variation, effectiveSettings.vDirRange);

		if(!noiseInit)
		{
			F32 fTime = pInput->fGlobalTime*effectiveSettings.fChangeRate;
			const F32 fSpatialScale = effectiveSettings.fSpatialScale;

			FastWindNoise( fTime, fSpatialScale * vPosition[0], fSpatialScale * vPosition[2], noise);
			noiseInit = true;
		}

		getBaseRandomVectorAtPosition(noise, &effectiveSettings, vCurVec);

		if (pSource->effect_type == WorldWindEffectType_Add)
		{
			scaleAddVec3(vCurVec, fAtten, vWindDir, vWindDir);
		}
		else if (pSource->effect_type == WorldWindEffectType_Multiply)
		{
			vCurVec[0] *= vWindDir[0];
			vCurVec[1] *= vWindDir[1];
			vCurVec[2] *= vWindDir[2];
			LERP3(vWindDir, vWindDir, vCurVec, fAtten);
		}
		else
		{
			assert(0); //unknown type of wind
		}
	}
}

void dynWindUpdateGridCell(const dynWindGlobalInputs *pInput, DynWindSampleGridCell* pCell, Vec3 vPos)
{
	Vec3 vBaseWindVec;
	Vec3 vSmallObjWindVec = {0,0,0};
	int i;
	DynWindQueuedForce *pForce;
	float fDeltaTime = pInput->fDeltaTime;

	dynWindGetBaseVectorAtPosition(pInput, vPos, vBaseWindVec);

	// update oscillator state
	for (i = 0; i < NUM_OSCILLATORS_PER_CELL; i++)
	{
		if (!pCell->forceInUse[i]) continue;

		pCell->forceOscillators[i].amplitude -= pCell->forceOscillators[i].amplitude * 0.7f * fDeltaTime;
		pCell->forceOscillators[i].velocity -= pCell->forceOscillators[i].velocity * 0.7f * fDeltaTime;
		pCell->forceOscillators[i].offset *= 0.9f * 1.0/(1.0f + fDeltaTime);
		if (pCell->forceOscillators[i].amplitude < 0.001f)
		{
			//turn this one off if it gets too low
			pCell->forceInUse[i] = false; 
			continue;
		}
	}

	//add up the wind contributions from the FX objects
	{
		DynForcePayload *pForcePayload;

		for( i=0, pForcePayload=pInput->pForcePayloads; i<pInput->numForcePayloads; i++, pForcePayload++)
		{
			Vec3 vForceVec;
			F32 fForceMag;
			F32 fForceMagX, fForceMagZ;
			Vec3 vTempVec;
			setVec3(vTempVec, vPos[0], pForcePayload->vForcePos[1], vPos[2]); //we only want to attenuate along xz
			dynFxGetForceEffect(&pForcePayload->force, pForcePayload->vForcePos, vTempVec, 1.0f, fDeltaTime, vForceVec, pForcePayload->qOrientation);
			fForceMag = normalVec3(vForceVec) * pInput->forceScale;

			fForceMagX = fForceMag * vForceVec[0];
			fForceMagZ = fForceMag * vForceVec[2];

			if (fForceMagX != 0)
			{
				bool fresh = !pCell->forceInUse[0];
				pCell->forceInUse[0] = true;
				setVec3(pCell->forceDirections[0], 1, 0, 0);
				
				if (fresh)
				{
					pCell->forceOscillators[0].amplitude = 0;
					pCell->forceOscillators[0].value = 0;
					pCell->forceOscillators[0].velocity = 0;
					pCell->forceOscillators[0].offset = 0;
					pCell->forceOscillators[0].seed = (U32)(intptr_t)pForcePayload; //use the pointer as the seed (it's good enough as a random #)
				}
			
				pCell->forceOscillators[0].amplitude += fabsf(fForceMagX * (1.0 / (fForceMagZ ? fForceMagZ : 1)) * fDeltaTime);
				pCell->forceOscillators[0].velocity += fabsf(0.1f * fForceMagX * fDeltaTime);
				//cell->forceOscillators[0].value += fForceMagX * fDeltaTime;
				pCell->forceOscillators[0].offset += 4.0f * fForceMagX * fDeltaTime;
			}

			if (fForceMagZ != 0)
			{
				bool fresh = !pCell->forceInUse[1];
				pCell->forceInUse[1] = true;
				setVec3(pCell->forceDirections[1], 0, 0, 1);

				if (fresh)
				{
					pCell->forceOscillators[1].amplitude = 0;
					pCell->forceOscillators[1].value = 0;
					pCell->forceOscillators[1].velocity = 0;
					pCell->forceOscillators[1].offset = 0;
					pCell->forceOscillators[1].seed = (U32)(intptr_t)pForcePayload; //use the pointer as the seed (it's good enough as a random #)
				}

				pCell->forceOscillators[1].amplitude += fabsf(fForceMagZ * (1.0 / (fForceMagX ? fForceMagX : 1)) * fDeltaTime);
				pCell->forceOscillators[1].velocity += fabsf(0.1f * fForceMagZ * fDeltaTime);
				//cell->forceOscillators[1].value += fForceMagZ * fDeltaTime;
				pCell->forceOscillators[1].offset += 4.0f * fForceMagZ * fDeltaTime;
			}
		}
	}


	//add up the wind contributions from the queued forces
	for(i=0, pForce=pInput->pQueuedForces ; i<pInput->numQueuedForces ; i++, pForce++)
	{
		Vec3 vForceVec, vDistance;
		F32 fForceMag, fForceMagX, fForceMagZ;

		//figure out which set of oscillators these go into
		int xoscillatorIdx = pForce->onlySmallObjects ? 2 : 0;	
		int zoscillatorIdx = pForce->onlySmallObjects ? 3 : 1;

		subVec3(vPos, pForce->pos, vDistance);
		if (lengthVec3XZ(vDistance) > pForce->radius) continue; //this one is too far away

		copyVec3(pForce->velocity, vForceVec);
		fForceMag = normalVec3(vForceVec) * pInput->queuedForceScale;

		{
			F32 signX = SIGN(vForceVec[0]);
			F32 signZ = SIGN(vForceVec[2]);
			fForceMagX = fForceMag * fabsf(vForceVec[0]);
			fForceMagZ = fForceMag * fabsf(vForceVec[2]);

			MIN1F(fForceMagX, pInput->queuedForceMaxMag);
			MIN1F(fForceMagZ, pInput->queuedForceMaxMag);

			fForceMagX *= signX;
			fForceMagZ *= signZ;
		}

		if (fForceMagX != 0)
		{
			DynWindOscillator* xosc = &pCell->forceOscillators[xoscillatorIdx];
			bool fresh = !pCell->forceInUse[xoscillatorIdx];
			pCell->forceInUse[xoscillatorIdx] = true;
			setVec3(pCell->forceDirections[xoscillatorIdx], 1, 0, 0);

			if (fresh)
			{
				xosc->amplitude = 0;
				xosc->value = 0;
				xosc->velocity = 0;
				xosc->offset = 0;
				xosc->seed = (U32)(intptr_t)pForce; //use the pointer as the seed (it's good enough as a random #)
			}

			xosc->amplitude += fabsf(fForceMagX * fDeltaTime);
			xosc->velocity += fabsf(pInput->queuedForceSpeedScale * fForceMagX * fDeltaTime);
			xosc->offset += pInput->queuedForceOffsetScale * fForceMagX * fDeltaTime;
		}

		if (fForceMagZ != 0)
		{
			DynWindOscillator* zosc = &pCell->forceOscillators[zoscillatorIdx];
			bool fresh = !pCell->forceInUse[zoscillatorIdx];
			pCell->forceInUse[zoscillatorIdx] = true;
			setVec3(pCell->forceDirections[zoscillatorIdx], 0, 0, 1);

			if (fresh)
			{
				zosc->amplitude = 0;
				zosc->value = 0;
				zosc->velocity = 0;
				zosc->offset = 0;
				zosc->seed = (U32)(intptr_t)pForce; //use the pointer as the seed (it's good enough as a random #)
			}

			zosc->amplitude += fabsf(fForceMagZ * fDeltaTime);
			zosc->velocity += fabsf(pInput->queuedForceSpeedScale * fForceMagZ * fDeltaTime);
			zosc->offset += pInput->queuedForceOffsetScale * fForceMagZ * fDeltaTime;
		}

	}

	//add the contributions of each force oscillator
	for (i = 0; i < NUM_OSCILLATORS_PER_CELL; i++)
	{
		Vec3 vCurVec;
		DynWindOscillator* pOsc = &pCell->forceOscillators[i];
		if (pCell->forceInUse[i] == 0) continue;

		pOsc->amplitude = MIN(pOsc->amplitude, pInput->forceMaxPower);
		pOsc->offset = MIN(fabsf(pOsc->offset), pInput->forceMaxDisplacement) * SIGN(pOsc->offset);
		pOsc->velocity = MIN(pOsc->velocity, pInput->forceMaxFreq);

		dynWindUpdateOscillator(pInput, pOsc, false);

		copyVec3(pCell->forceDirections[i], vCurVec);
		scaleVec3(vCurVec, pOsc->value, vCurVec);

		if (i > 1)
		{
			addVec3(vSmallObjWindVec, vCurVec, vSmallObjWindVec);
		}
		else
		{
			addVec3(vBaseWindVec, vCurVec, vBaseWindVec);
		}
	}

	//Smooth out the main force
	{
		Vec3 vNewWindVec = {0,0,0};
		scaleVec3(vBaseWindVec, 0.25f, vNewWindVec);
		scaleAddVec3(pCell->currentWindVec, 0.75f, vNewWindVec, pCell->currentWindVec);
	}

	//Smooth out the small object-only force
	{
		Vec3 vNewWindVec = {0,0,0};
		scaleVec3(vSmallObjWindVec, 0.25f, vNewWindVec);
		scaleAddVec3(pCell->currentWindVecSmall, 0.75f, vNewWindVec, pCell->currentWindVecSmall);
	}
}

// this is typically only executed from the SPU job on PS3 which works on a row at a time
void dynWindUpdateRow( const dynWindGlobalInputs *pInput, DynWindSampleGridCell *dstCells, float zPos)
{
	int x;

	for (x = 0; x < pInput->gridSize; x++)
	{
		int signedx = x - (pInput->gridSize-1)/2;

		Vec3 pos = {(float)signedx * pInput->gridDivisionDistance, 0, zPos};
		addVec3(pos, pInput->cameraPos, pos);

		dynWindUpdateGridCell(pInput, dstCells, pos);

		dstCells++;
	}
}