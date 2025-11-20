#include "dynFxFastParticle.h"

#include "rand.h"
#include "fileutil.h"
#include "FolderCache.h"
#include "StringCache.h"
#include "timing.h"
#include "quat.h"
#include "WorldGridPrivate.h"
#include "wlState.h"
#include "wlModelLoad.h"

#include "dynFxInfo.h"
#include "DynFxInterface.h"
#include "structPack.h"
#include "UnitSpec.h"
#include "AutoGen/dynFxFastParticle_c_ast.h"
#include "DynFx.h"
#include "DynFxParticle.h"

#define hDynFxInfoDict REFERENCING_WRONG_DICT

AUTO_RUN_ANON(memBudgetAddMapping(__FILE__, BUDGET_FXSystem););

static int iNumFastParticleUnpacked = 0;

extern bool gbIgnoreTextureErrors;

MP_DEFINE(DynFxFastParticleSet);


DictionaryHandle hFxParticleDict;

const F32 fOneOver360 = 0.0027777777777777777777777777777778;
const F32 fMaxFeetPerSecondForEmission = 20.0f;
const F32 fMaxLengthForEmission = 50.0f; 

#define MAX_ONE(a) a = MAXF(a, 1.0f);

#define FAST_PARTICLE_ALIGNMENT 0

static void jitterAndRotate( Vec3 vValue, const Vec3 vOffset, const Vec3 vJitter, const Quat qRot, U32* puiSeed, DynParticleEmitFlag eRotFlag);


static bool dynFxFastParticleSetInitQueue( DynFxFastParticleSet* pSet)
{
	if (pSet->bEnvironmentFX)
	{
		if (dynDebugState.uiNumAllocatedFastParticlesEnvironment + pSet->uiNumAllocated > MAX_ALLOCATED_FAST_PARTICLES_ENVIRONMENT)
		{
			dynDebugState.bTooManyFastParticlesEnvironment = true;
			if (pSet->iPriorityLevel != edpOverride)
				return false; // Overflow!
		}
	}
	else
	{
		if (dynDebugState.uiNumAllocatedFastParticlesEntities + pSet->uiNumAllocated > MAX_ALLOCATED_FAST_PARTICLES_ENTITY)
		{
			dynDebugState.bTooManyFastParticles = true;
			if (pSet->iPriorityLevel != edpOverride)
				return false; // Overflow!
		}
	}

	poolQueueInit(&pSet->particleQueue, sizeof(DynFxFastParticle), pSet->uiNumAllocated, FAST_PARTICLE_ALIGNMENT);

	if (pSet->bEnvironmentFX)
	{
		dynDebugState.uiNumAllocatedFastParticlesEnvironment += pSet->uiNumAllocated;
	}
	else
	{
		dynDebugState.uiNumAllocatedFastParticlesEntities += pSet->uiNumAllocated;
	}

	return true;
}

static void dynFxFastParticleSetDeinitQueue( DynFxFastParticleSet* pSet)
{
	if (pSet->bEnvironmentFX)
	{
		dynDebugState.uiNumAllocatedFastParticlesEnvironment -= pSet->uiNumAllocated;
	}
	else
	{
		dynDebugState.uiNumAllocatedFastParticlesEntities -= pSet->uiNumAllocated;
	}
	poolQueueDeinit(&pSet->particleQueue);
}

void dynFxFastParticleSetOrphan(DynFxFastParticleSet* pSet, DynFxRegion* pFxRegion)
{
	pSet->bStopEmitting = true;
	eaPush(&pFxRegion->eaOrphanedSets, pSet);
}

void dynFxFastParticleSetOrphansUpdate(F32 fDeltaTime)
{
	WorldRegion** eaWorldRegions = worldGetAllWorldRegions();
	PERFINFO_AUTO_START_FUNC();

	dynDebugState.uiNumAllocatedFastParticleSets = (U32)mpGetAllocatedCount(MP_NAME(DynFxFastParticleSet));

	FOR_EACH_IN_EARRAY(eaWorldRegions, WorldRegion, pWorldRegion)
	{
		DynFxRegion* pFxRegion = &pWorldRegion->fx_region;
		int iIndex;
		for (iIndex=eaSize(&pFxRegion->eaOrphanedSets)-1; iIndex>= 0; --iIndex)
		{
			DynFxFastParticleSet* pSet = pFxRegion->eaOrphanedSets[iIndex];
			if (!pSet->particleQueue.pStorage || ((poolQueueGetNumElements(&pSet->particleQueue) == 0) && pSet->bEmitted))
			{
				dynFxFastParticleSetDestroy(pSet);
				eaRemove(&pFxRegion->eaOrphanedSets, iIndex);
			}
			else
			{
				dynFxFastParticleSetUpdate(pSet, zerovec3, fDeltaTime, false, true);
			}
		}
	}
	FOR_EACH_END;
	PERFINFO_AUTO_STOP_FUNC();
}

void dynFxFastParticleSetCalculateRadius(DynFxFastParticleInfo* pInfo)
{
	int i;
	F32 fDrag = MAX(pInfo->fDrag - pInfo->fDragJitter, 0.0f);
	F32 fMaxScale;
	Vec3 vMaxDistance;
	zeroVec3(vMaxDistance);

	// Simulate a worst-case particle in each dimension (i) and then positive or negative within that dimension (j)
	for (i=0; i<3; ++i)
	{
		int j;
		for (j=0; j<2; ++j)
		{
			F32 fAccel = pInfo->vAcceleration[i] + (j?pInfo->vAccelerationJitter[i]:-pInfo->vAccelerationJitter[i]);
			F32 fVel = pInfo->vVelocity[i] + (j?pInfo->vVelocityJitter[i]:-pInfo->vVelocityJitter[i]);
			F32 fPos = pInfo->vPosition[i] + (j?pInfo->vPositionJitter[i]:-pInfo->vPositionJitter[i]);

			// Now simulate a particle with this worst-case jitter
			F32 fTime = 0.0f;
			F32 fDeltaTime = 1/20.0f;

			while (fTime < pInfo->fLifeSpan)
			{
				fVel += fAccel * fDeltaTime;

				{
					F32 fDragAccel = 1.0f - MIN(fabsf(fVel) * fDrag * fDeltaTime, 1.0f);
					fVel *= fDragAccel;
				}

				fPos += fVel * fDeltaTime;

				MAX1(vMaxDistance[i], fabsf(fPos));
				fTime += fDeltaTime;
			}
		}
	}


	fMaxScale = lengthVec2(pInfo->curvePath[0].vScale);
	for (i=1; i<4; ++i)
	{
		fMaxScale = MAX(fMaxScale, lengthVec2(pInfo->curvePath[i].vScale));
	}
	pInfo->fRadius = fMaxScale * 0.5f + lengthVec3(vMaxDistance);
	assert(fabsf(pInfo->fRadius) < 10000.0f);
}

extern int bDynListAllUnusedTexturesAndGeometry;


bool dynFxFastParticleInfoInit(DynFxFastParticleInfo* pInfo)
{
	int i;

	/*
	copyVec3(pInfo->vVelocity, pInfo->compiled.vVelocity);
	copyVec3(pInfo->vVelocityJitter, pInfo->compiled.vVelocityJitter);
	copyVec3(pInfo->vAcceleration, pInfo->compiled.vAcceleration);
	pInfo->compiled.vAcceleration[3] = pInfo->fDrag;
	copyVec3(pInfo->vAccelerationJitter, pInfo->compiled.vAccelerationJitter);
	*/

	{
		MAX_ONE(pInfo->fLoopHue);
		MAX_ONE(pInfo->fLoopSaturation);
		MAX_ONE(pInfo->fLoopValue);
		MAX_ONE(pInfo->fLoopAlpha);
		MAX_ONE(pInfo->fLoopScaleX);
		MAX_ONE(pInfo->fLoopScaleY);
		MAX_ONE(pInfo->fLoopRotation);
		MAX_ONE(pInfo->fLoopSpin);

		pInfo->compiled.vColorTimeScale[0] = pInfo->fLoopHue;
		pInfo->compiled.vColorTimeScale[1] = pInfo->fLoopSaturation;
		pInfo->compiled.vColorTimeScale[2] = pInfo->fLoopValue;
		pInfo->compiled.vColorTimeScale[3] = pInfo->fLoopAlpha;

		pInfo->compiled.vScaleRotTimeScale[0] = pInfo->fLoopScaleX;
		pInfo->compiled.vScaleRotTimeScale[1] = pInfo->fLoopScaleY;
		pInfo->compiled.vScaleRotTimeScale[2] = pInfo->fLoopRotation;
		pInfo->compiled.vScaleRotTimeScale[3] = pInfo->fLoopSpin;
	}


	for (i=0; i<4; ++i)
	{
		copyVec4(pInfo->curvePath[i].vColor, pInfo->compiled.vColor[i]);
		copyVec4(pInfo->curveJitter[i].vColor, pInfo->compiled.vColorJitter[i]);
		copyVec4(pInfo->curveTime[i].vColor, pInfo->compiled.vColorTime[i]);

		copyVec2(pInfo->curvePath[i].vScale, pInfo->compiled.vScaleRot[i]);
		copyVec2(pInfo->curveJitter[i].vScale, pInfo->compiled.vScaleRotJitter[i]);
		copyVec2(pInfo->curveTime[i].vScale, pInfo->compiled.vScaleRotTime[i]);

		pInfo->compiled.vScaleRot[i][2] = RAD(pInfo->curvePath[i].fRot);
		pInfo->compiled.vScaleRotJitter[i][2] = RAD(pInfo->curveJitter[i].fRot);
		pInfo->compiled.vScaleRotTime[i][2] = pInfo->curveTime[i].fRot;

		pInfo->compiled.vScaleRot[i][3] = RAD(pInfo->curvePath[i].fSpin);
		pInfo->compiled.vScaleRotJitter[i][3] = RAD(pInfo->curveJitter[i].fSpin);
		pInfo->compiled.vScaleRotTime[i][3] = pInfo->curveTime[i].fSpin;

		pInfo->compiled.vColor[i][0] *= fOneOver360;
		pInfo->compiled.vColorJitter[i][0] *= fOneOver360;


		if (i>0)
		{
			int j;
			for (j=0; j<4; ++j)
			{
				if (pInfo->compiled.vColorTime[i][j] <= 0.0f)
				{
					// hack for now
					pInfo->compiled.vColorTime[i][j] = FLT_MAX;
				}
				if (pInfo->compiled.vScaleRotTime[i][j] <= 0.0f)
				{
					// hack for now
					pInfo->compiled.vScaleRotTime[i][j] = FLT_MAX;
				}
			}
		}
	}
	zeroVec4(pInfo->compiled.vColorTime[0]);
	zeroVec4(pInfo->compiled.vScaleRotTime[0]);
	pInfo->compiled.vTexParams[0] = pInfo->bHFlipTex?1.0f:0.0f;
	pInfo->compiled.vTexParams[1] = pInfo->bVFlipTex?1.0f:0.0f;
	pInfo->compiled.vTexParams[2] = pInfo->bQuadTex?1.0f:0.0f;
	pInfo->compiled.vTexParams[3] = (int)pInfo->eStreakMode;

	if(pInfo->bAnimatedTexture) {
		pInfo->compiled.vScrollAndAnimation[0] = pInfo->vAnimParams[0];
		pInfo->compiled.vScrollAndAnimation[1] = pInfo->vAnimParams[1];
		pInfo->compiled.vScrollAndAnimation[2] = 0;
		pInfo->compiled.vScrollAndAnimation[3] = 0;
	} else {
		pInfo->compiled.vScrollAndAnimation[0] = pInfo->vScroll[0];
		pInfo->compiled.vScrollAndAnimation[1] = pInfo->vScroll[1];
		pInfo->compiled.vScrollAndAnimation[2] = pInfo->vScrollJitter[0];
		pInfo->compiled.vScrollAndAnimation[3] = pInfo->vScrollJitter[1];
	}

	pInfo->compiled.vMoreParams[0] = pInfo->fZBias;

	pInfo->compiled.vSpinIntegrals[0] = 0.0f;
	for (i=1; i<4; ++i)
	{
		pInfo->compiled.vSpinIntegrals[i] = pInfo->compiled.vSpinIntegrals[i-1];
		if (pInfo->curveTime[i].fSpin > 0.0f)
		{
			F32 fTimePassed = pInfo->curveTime[i].fSpin - pInfo->curveTime[i-1].fSpin;
			pInfo->compiled.vSpinIntegrals[i] += (pInfo->curvePath[i-1].fSpin + (pInfo->curvePath[i].fSpin - pInfo->curvePath[i-1].fSpin) * 0.5f) * fTimePassed;
		}
	}

	// check jittersphere
	pInfo->bJitterSphere = !vec3IsZero(pInfo->vPositionSphereJitter);

	// Normalize hue

	pInfo->fLifeSpan = MAX(pInfo->fLifeSpan, 0.1f);
	pInfo->fLifeSpanInv = 1.0f / pInfo->fLifeSpan;

	pInfo->fVelocityJitterUpdateInverse = pInfo->fVelocityJitterUpdate?1.0f / pInfo->fVelocityJitterUpdate:0.0f;
	pInfo->fAccelerationJitterUpdateInverse = pInfo->fAccelerationJitterUpdate?1.0f / pInfo->fAccelerationJitterUpdate:0.0f;

	pInfo->fEmissionRateJitter = MAX(MIN(pInfo->fEmissionRateJitter, pInfo->fEmissionRate - 0.1f), 0);
	pInfo->uiEmissionCount = MAX(pInfo->uiEmissionCount, 1);
	pInfo->uiEmissionCountJitter = MAX(MIN(pInfo->uiEmissionCountJitter, pInfo->uiEmissionCount), 0);

	// calculate radius
	dynFxFastParticleSetCalculateRadius(pInfo);

	if (bDynListAllUnusedTexturesAndGeometry)
	{
		dynFxMarkTextureAsUsed(pInfo->pcTexture);
	}

	pInfo->bInited = true;

	return true;
}

void dynFxInitNewFastParticleInfo(DynFxFastParticleInfo* pInfo)
{
	int i;
	pInfo->pcTexture = allocAddString("checker64");
	pInfo->pTexture = NULL;
	for (i=1; i<4; ++i)
	{
		pInfo->curveTime[i].vColor[0] = -1.0f;
		pInfo->curveTime[i].vColor[1] = -1.0f;
		pInfo->curveTime[i].vColor[2] = -1.0f;
		pInfo->curveTime[i].vColor[3] = -1.0f;
		pInfo->curveTime[i].vScale[0] = -1.0f;
		pInfo->curveTime[i].vScale[1] = -1.0f;
		pInfo->curveTime[i].fRot = -1.0f;
		pInfo->curveTime[i].fSpin = -1.0f;
	}
	copyVec4(unitvec4, pInfo->curvePath[0].vColor);
	copyVec2(unitvec4, pInfo->curvePath[0].vScale);
	pInfo->fLifeSpan = 1.0f;
	pInfo->uiEmissionCount = 1;
	pInfo->uiEmissionCountJitter = 0;
	pInfo->fEmissionRate = 1.0f;
	pInfo->fEmissionRateJitter = 0.0f;
}

AUTO_STRUCT;
typedef struct DynFxFastParticleDataInfoSerialize
{
	const char *pcName; AST( POOL_STRING )
	U32 data_offset;
	const char *pcFileName; AST( POOL_STRING FILENAME)
	const char *pcTexture; AST( POOL_STRING ) // Just for dependency checking
} DynFxFastParticleDataInfoSerialize;


AUTO_STRUCT AST_FIXUPFUNC(fixupDynFxFastParticleInitialLoad);
typedef struct DynFxFastParticleInitialLoad
{
	// Parsed
	DynFxFastParticleInfo** eaFxInfo; AST(NAME("DynParticle"))

	// Written to .bin and read in, converted, freed
	SerializablePackedStructStream *packed_data_serialize; AST( NO_TEXT_SAVE )
	DynFxFastParticleDataInfoSerialize **data_infos_serialize; AST( NO_TEXT_SAVE )

	// Run-time referenced
	PackedStructStream packedFxStream; NO_AST

} DynFxFastParticleInitialLoad;

static DynFxFastParticleInitialLoad fxFastParticleLoadData={0};


AUTO_FIXUPFUNC;
TextParserResult fixupFastParticle(DynFxFastParticleInfo* pFastParticle, enumTextParserFixupType eType, void *pExtraData)
{
	switch (eType)
	{
		xcase FIXUPTYPE_POST_TEXT_READ:
		{
			char cName[256];
			getFileNameNoExt(cName, pFastParticle->pcFileName);
			pFastParticle->pcName = allocAddString(cName);
		}
		case FIXUPTYPE_POST_BIN_READ:
			{
				if (!dynFxFastParticleInfoInit(pFastParticle))
				{
					return PARSERESULT_INVALID; // remove this from the costume list
				}
			}
		xcase FIXUPTYPE_POST_RELOAD:
			if (pFastParticle->pcTexture && wl_state.tex_find_func && !wl_state.tex_find_func(pFastParticle->pcTexture, 0, WL_FOR_FX))
				ErrorFilenameGroupf(pFastParticle->pcFileName, "Art", 14, "References non-existent texture %s", pFastParticle->pcTexture);
	}

	return PARSERESULT_SUCCESS;
}

TextParserResult fxFastParticleLoadDataPreProcessor(DynFxFastParticleInitialLoad *pFxLoadInfo)
{
	// Pack data, serialize, free parsed
	TextParserResult ret = PARSERESULT_SUCCESS;
	U32 packed_size;

	loadend_printf("done (%d DynFxFastParticles Loaded)", eaSize(&pFxLoadInfo->eaFxInfo));

	loadstart_printf("Packing DynFxFastParticles... ");

	// Verify and pack
	PackedStructStreamInit(&pFxLoadInfo->packedFxStream, STRUCT_PACK_BITPACK);
	FOR_EACH_IN_EARRAY(pFxLoadInfo->eaFxInfo, DynFxFastParticleInfo, pInfo)
	{
		DynFxFastParticleDataInfoSerialize *info = StructAlloc(parse_DynFxFastParticleDataInfoSerialize);
		info->data_offset = StructPack(parse_DynFxFastParticleInfo, pInfo, &pFxLoadInfo->packedFxStream);
		info->pcName = pInfo->pcName;
		info->pcFileName = pInfo->pcFileName;
		info->pcTexture = pInfo->pcTexture;
		eaPush(&pFxLoadInfo->data_infos_serialize, info);
	}
	FOR_EACH_END;
	PackedStructStreamFinalize(&pFxLoadInfo->packedFxStream);
	packed_size = PackedStructStreamGetSize(&pFxLoadInfo->packedFxStream);
	eaDestroyStruct(&pFxLoadInfo->eaFxInfo, parse_DynFxFastParticleInfo);

	// Serialize
	pFxLoadInfo->packed_data_serialize = PackedStructStreamSerialize(&pFxLoadInfo->packedFxStream);
	PackedStructStreamDeinit(&pFxLoadInfo->packedFxStream);

	loadend_printf("done (%s)", friendlyBytes(packed_size));
	loadstart_printf("Writing .bin... ");

	return ret;
}

TextParserResult fxFastParticleLoadDataPostProcessor(DynFxFastParticleInitialLoad *pFxLoadInfo)
{
	// Deserialize
	loadend_printf("done.");
	loadstart_printf("Setting up DynFxFastParticle run-time data... ");

	allocAddStringMapRecentMemory("SerializablePackedStructStream", __FILE__, __LINE__);
	PackedStructStreamDeserialize(&pFxLoadInfo->packedFxStream, pFxLoadInfo->packed_data_serialize);
	StructDestroySafe(parse_SerializablePackedStructStream, &pFxLoadInfo->packed_data_serialize);

	FOR_EACH_IN_EARRAY(pFxLoadInfo->data_infos_serialize, DynFxFastParticleDataInfoSerialize, pInfo)
	{
		if(!gbIgnoreTextureErrors && pInfo->pcTexture && wl_state.tex_find_func && !wl_state.tex_find_func(pInfo->pcTexture, 0, WL_FOR_FX))
			ErrorFilenameGroupf(pInfo->pcFileName, "Art", 14, "References non-existent texture %s", pInfo->pcTexture);
		if (isDevelopmentMode())
			resUpdateInfo(hFxParticleDict, pInfo->pcName, parse_DynFxFastParticleDataInfoSerialize, pInfo, NULL, NULL, NULL, NULL, NULL, false, false);
		resSetLocationID(hFxParticleDict, pInfo->pcName, pInfo->data_offset + 1); // Add 1 so 0 can be invalid
	}
	FOR_EACH_END;

	eaDestroyStruct(&pFxLoadInfo->data_infos_serialize, parse_DynFxFastParticleDataInfoSerialize);

	return PARSERESULT_SUCCESS;
}


TextParserResult fixupDynFxFastParticleInitialLoad(DynFxFastParticleInitialLoad *pFxLoadInfo, enumTextParserFixupType eFixupType, void *pExtraData)
{
	switch (eFixupType)
	{
	case FIXUPTYPE_POST_ALL_TEXT_READING_AND_INHERITANCE_DURING_LOADFILES:
		// Verify internal consistency
		// Pack data, serialize, free parsed
		return fxFastParticleLoadDataPreProcessor(pFxLoadInfo);

	case FIXUPTYPE_POST_BINNING_DURING_LOADFILES:
		// Deserialize
		return fxFastParticleLoadDataPostProcessor(pFxLoadInfo);

	}

	return PARSERESULT_SUCCESS;
}


static void fastParticleReloadCallback(const char *relpath, int when)
{

	loadstart_printf("Reloading DynFxParticles...");
	fileWaitForExclusiveAccess(relpath);
	errorLogFileIsBeingReloaded(relpath);

	if (!fileExists(relpath))
		; // File was deleted, do we care here?

	if(!ParserReloadFileToDictionary(relpath,hFxParticleDict))
	{
		ErrorFilenamef(relpath, "Error reloading particle file: %s", relpath);
	}

	// Change the number to keep to 0 (meaning infinity) so that we don't ever lose our reloaded fx.
	resDictSetMaxUnreferencedResources(hFxParticleDict, RES_DICT_KEEP_ALL);

	loadend_printf("done");
}

void AutoUnpackFxFastParticleInfo(DictionaryHandleOrName dictHandle, int command, ConstReferenceData pRefData, Referent pReferent, const char* reason)
{
	resUnpackHandleRequest(dictHandle, command, pRefData, pReferent, &fxFastParticleLoadData.packedFxStream);
	++iNumFastParticleUnpacked;
}

// Print out information about the current state of the dynfxFastParticleInfo dictionary
AUTO_COMMAND  ACMD_CATEGORY(dynFx);
void dfxPrintUnpackedParticleList(void)
{
	RefDictIterator iter;
	int i = 0;
	const char* pcRef;
	RefSystem_InitRefDictIterator(hFxParticleDict, &iter);
	while ( (pcRef = RefSystem_GetNextReferentFromIterator(&iter)) != NULL)
	{
		++i;
	}
	printf("Counted %d Referents\n", i);

	RefSystem_InitRefDictIterator(hFxParticleDict, &iter);
}

static void dynFxFastParticleInfoRefDictCallback(enumResourceEventType eType, const char *pDictName, const char *pRefData, Referent pReferent, void *pUserData)
{
	if (eType == RESEVENT_RESOURCE_ADDED)
	{
		DynFxFastParticleInfo* pInfo = (DynFxFastParticleInfo*)pReferent;
		if (!pInfo->bInited)
			dynFxFastParticleInfoInit(pInfo);
	}
	else if (eType == RESEVENT_RESOURCE_REMOVED)
	{
		// No special freeing code here?
// 		DynFxFastParticleInfo* pInfo = (DynFxFastParticleInfo*)pReferent;
	}
}


AUTO_RUN;
void registerFastParticleDictionary(void)
{
	hFxParticleDict = RefSystem_RegisterSelfDefiningDictionary("DynParticle", false, parse_DynFxFastParticleInfo, true, true, NULL);

	resDictRequestMissingResources(hFxParticleDict, RES_DICT_KEEP_ALL, false, AutoUnpackFxFastParticleInfo);
	resDictRegisterEventCallback(hFxParticleDict, dynFxFastParticleInfoRefDictCallback, NULL);
}



void dynFxFastParticleLoadAll(void)
{
	loadstart_printf("DynFxFastParticles Startup... ");

	loadstart_printf("Loading DynFxParticles...");
	ParserLoadFiles("dyn/fx", ".part", "DynParticle.bin", PARSER_OPTIONALFLAG|PARSER_BINS_ARE_SHARED, parse_DynFxFastParticleInitialLoad, &fxFastParticleLoadData);
//	ParserLoadFilesToDictionary("dyn/fx", ".part", "DynParticle.bin", PARSER_OPTIONALFLAG | PARSER_BINS_ARE_SHARED, hFxParticleDict);
	assert(!eaSize(&fxFastParticleLoadData.data_infos_serialize));
	assert(!eaSize(&fxFastParticleLoadData.eaFxInfo));
	assert(!fxFastParticleLoadData.packed_data_serialize);
	loadend_printf("done.");

	loadstart_printf("Unpacking referenced DynFX... ");
	iNumFastParticleUnpacked = 0;
	resReRequestMissingResources(hFxParticleDict);
	loadend_printf("done (%d Unpacked)", iNumFastParticleUnpacked);

	// Reload callbacks
	if(isDevelopmentMode())
	{
		FolderCacheSetCallback(FOLDER_CACHE_CALLBACK_UPDATE, "dyn/fx/*.part", fastParticleReloadCallback);
	}

	loadend_printf("done (%d Particles)", resGetNumberOfInfosEvenPacked(hFxParticleDict));
}

typedef struct DynNodeRef
{
	const char* pcNode;
	REF_TO(DynNode) hNode;
} DynNodeRef;

const DynNode* dynFxFastParticleSetGetAtNode(DynFxFastParticleSet* pSet, U32 uiNodeIndex)
{
	DynNodeRef* pNodeRef = &pSet->pAtNodes[uiNodeIndex];
	const DynNode* pNode = GET_REF(pNodeRef->hNode);
	if (!pNode)
	{
		REMOVE_HANDLE(pNodeRef->hNode);
		pNode = dynFxNodeByName(pNodeRef->pcNode, GET_REF(pSet->hParentFX));
		if (pNode)
		{
			ADD_SIMPLE_POINTER_REFERENCE_DYN(pNodeRef->hNode, pNode);
		}
	}

	return pNode;
}

static const DynNode* dynFxFastParticleSetGetEmitTargetNode(DynFxFastParticleSet* pSet, U32 uiNodeIndex)
{
	DynNodeRef* pNodeRef = &pSet->pEmitTargetNodes[uiNodeIndex];
	const DynNode* pNode = GET_REF(pNodeRef->hNode);
	if (!pNode)
	{
		REMOVE_HANDLE(pNodeRef->hNode);
		pNode = dynFxNodeByName(pNodeRef->pcNode, GET_REF(pSet->hParentFX));
		if (pNode)
		{
			ADD_SIMPLE_POINTER_REFERENCE_DYN(pNodeRef->hNode, pNode);
		}
	}

	return pNode;
}

DynFxFastParticleInfo* dynFxFastParticleInfoFromNameNonConst( const char* pcName )
{
	DynFxFastParticleInfo* pInfo = RefSystem_ReferentFromString(hFxParticleDict, pcName);
	if (!pInfo && resGetLocationID(hFxParticleDict, (char*)pcName))
	{
		// It clearly exists, but somehow our reference has broken down. Fix it up.
		resNotifyRefsExist(hFxParticleDict, pcName);
		pInfo = RefSystem_ReferentFromString(hFxParticleDict, pcName);
	}
	return pInfo;
}

const DynFxFastParticleInfo* dynFxFastParticleInfoFromName( const char* pcName )
{
	return dynFxFastParticleInfoFromNameNonConst(pcName);
}

U32 dynFxFastParticleMaxPossibleRate(const DynFxFastParticleInfo* pInfo, F32 fMaxFeetPerSecond)
{
	F32 fMaxEmissionRate = pInfo->fEmissionRate + pInfo->fEmissionRateJitter;
	F32 fMaxEmissionRatePerFoot = pInfo->fEmissionRatePerFoot + pInfo->fEmissionRatePerFootJitter;
	return ceil(MAX(fMaxEmissionRate, fMaxEmissionRatePerFoot * (fMaxFeetPerSecond>0.0f?fMaxFeetPerSecond:fMaxFeetPerSecondForEmission)));
}

static U32 dynFxFastParticleInfoParticleCount(const DynFxFastParticleInfo* pInfo, F32 fMaxFeetPerSecond)
{
	//New Approach: Calculate counts, but don't worry about distance-based emission, as we'll dynamically resize for those
	U32 uiEmissionCount = pInfo->uiEmissionCount;
	U32 uiMaxIntegral = pInfo->fLifeSpan * uiEmissionCount * (pInfo->fEmissionRate + pInfo->fEmissionRateJitter);

	return MAX(uiEmissionCount, uiMaxIntegral);
}

static int iFPBufferMax = 0;
static int iFPErrorOnOverflow = 0;

// If set to one, fast particle buffer is set to max rather than calculated. for debugging only
AUTO_CMD_INT(iFPBufferMax, dfxFPBufferMax) ACMD_CATEGORY(dynFx);

// If set to one, then any buffer overflows in the fast particle system will report the error
AUTO_CMD_INT(iFPErrorOnOverflow, dfxFPErrorOnOverflow) ACMD_CATEGORY(dynFx);


static void createEmitPointsFromModel( DynFxFastParticleSet* pSet, const GeoMeshTempData* pMeshTempData) {

	// Copy vertex data.
	pSet->pvModelEmitOffsetVerts = calloc(pMeshTempData->vert_count, sizeof(Vec3));
	pSet->uiNumEmitVerts = pMeshTempData->vert_count;
	memcpy(pSet->pvModelEmitOffsetVerts, pMeshTempData->verts, sizeof(Vec3) * pMeshTempData->vert_count);

}

static void createEmitTrisFromModel( DynFxFastParticleSet* pSet, const GeoMeshTempData* pMeshTempData) {

	int i;

	createEmitPointsFromModel(pSet, pMeshTempData);

	// Copy triangle data.
	pSet->uiNumTriangles = pMeshTempData->tri_count;
	pSet->pModelEmitOffsetTris = calloc(pMeshTempData->tri_count * 3, sizeof(S32));
	pSet->pModelEmitOffsetTriSizes = calloc(pMeshTempData->tri_count, sizeof(F32));
	memcpy(pSet->pModelEmitOffsetTris, pMeshTempData->tris, sizeof(S32) * pMeshTempData->tri_count * 3);

	// Compute surface area of each triangle.
	for(i = 0; i < pMeshTempData->tri_count; i++) {

		Vec3 ab;
		Vec3 ac;
		Vec3 crossed;

		subVec3(pMeshTempData->verts[pMeshTempData->tris[i*3]], pMeshTempData->verts[pMeshTempData->tris[i*3 + 1]], ab);
		subVec3(pMeshTempData->verts[pMeshTempData->tris[i*3]], pMeshTempData->verts[pMeshTempData->tris[i*3 + 2]], ac);

		crossVec3(ab, ac, crossed);

		pSet->pModelEmitOffsetTriSizes[i] = lengthVec3(crossed);

		pSet->fModelTotalArea += pSet->pModelEmitOffsetTriSizes[i];
	}

}

DynFxFastParticleSet* dynFxFastParticleSetCreate( DynFPSetParams* pParams )
{
	DynFxFastParticleSet* pSet;
	U32 uiNumFastParticlesInSet;
	if (!pParams->pInfo)
		return NULL;
	PERFINFO_AUTO_START_FUNC();

	if (pParams->bMaxBuffer || iFPBufferMax)
		uiNumFastParticlesInSet = MAX_PARTICLES_PER_SET;
	else
		uiNumFastParticlesInSet = MIN(MAX_PARTICLES_PER_SET, ceil(dynFxFastParticleInfoParticleCount(pParams->pInfo, 0.0f) * 1.15f));


	MP_CREATE(DynFxFastParticleSet, MAX_FAST_PARTICLE_SETS);
	if (mpGetAllocatedCount(MP_NAME(DynFxFastParticleSet)) >= MAX_FAST_PARTICLE_SETS)
	{
		PERFINFO_AUTO_STOP_FUNC();
		return NULL;
	}

	pSet = MP_ALLOC(DynFxFastParticleSet);
	SET_HANDLE_FROM_REFERENT(hFxParticleDict, (DynFxFastParticleInfo*)pParams->pInfo, pSet->hInfo);
	ADD_SIMPLE_POINTER_REFERENCE_DYN(pSet->hLocation, pParams->pLocation);
	if (pParams->pMagnet)
		ADD_SIMPLE_POINTER_REFERENCE_DYN(pSet->hMagnet, pParams->pMagnet);
	if (pParams->pEmitTarget)
		ADD_SIMPLE_POINTER_REFERENCE_DYN(pSet->hEmitTarget, pParams->pEmitTarget);
	if (pParams->pParentFX)
		ADD_SIMPLE_POINTER_REFERENCE_DYN(pSet->hParentFX, pParams->pParentFX);
	if (pParams->pTransformTarget)
		ADD_SIMPLE_POINTER_REFERENCE_DYN(pSet->hTransformTarget, pParams->pTransformTarget);

	pSet->fSetTime = 0.0f;
	pSet->fLastEmissionTime = 0.0f;
	pSet->bEmitted = false;
	pSet->fHueShift = 0.0f;
	pSet->fSystemAlpha = 1.0f;
	pSet->ePosFlag = pParams->ePosFlag;
	pSet->eRotFlag = pParams->eRotFlag;
	pSet->eScaleFlag = pParams->eScaleFlag;
	pSet->fScalePosition = pParams->fScalePosition;
	pSet->fScaleSprite   = pParams->fScaleSprite;
	pSet->iPriorityLevel = pParams->iPriorityLevel;
	pSet->fDrawDistance = pParams->fDrawDistance;
	pSet->fMinDrawDistance = pParams->fMinDrawDistance;
	pSet->fFadeDistance = pParams->fDrawDistance * 0.9f;
	pSet->fMinFadeDistance = pParams->fMinDrawDistance * 1.1f;
	pSet->pcEmitterName = pParams->pcEmitterName;
	pSet->uiNumAllocated = uiNumFastParticlesInSet;
	pSet->bApplyCountEvenly = pParams->bApplyCountEvenly;
	pSet->bSoftKill = pParams->bSoftKill;
	pSet->fHueShift = pParams->fHueShift;
	pSet->fSaturationShift = pParams->fSaturationShift;
	pSet->fValueShift = pParams->fValueShift;
	pSet->bEnvironmentFX = pParams->bEnvironmentFX;
	pSet->b2D = pParams->b2D;
	pSet->bJumpStart = pParams->bJumpStart;
	pSet->bUseModelTriangles = pParams->bPatternModelUseTriangles;
	pSet->bOverrideSpecialParam = pParams->bOverrideSpecialParam;
	pSet->bLightModulation = pParams->bLightModulation;
	pSet->bColorModulation = pParams->bColorModulation;
	pSet->pcModelPattern = pParams->pcPatternModelName;
	pSet->bUseModel = pParams->bUseModel;
	pSet->fCutoutDepthScale = pParams->fCutoutDepthScale;
	pSet->fParticleMass = pParams->fParticleMass;
	pSet->fSystemAlphaFromFx = pParams->fSystemAlphaFromFx;
	setVec4same(pSet->vModulateColor, 1);
	pSet->bNormalizeTransformTarget = pParams->bNormalizeTransformTarget;
	pSet->bNormalizeTransformTargetOtherAxes = pParams->bNormalizeTransformTargetOtherAxes;

	if(pParams->pTransformTarget) {
		dynNodeGetWorldSpacePos(pParams->pTransformTarget, pSet->vLastEmitTargetLocation);
	} else {
		setVec3same(pSet->vLastEmitTargetLocation, 0.0f);
	}
	dynNodeGetWorldSpacePos(pParams->pLocation, pSet->vLastLocation);

	dynNodeGetWorldSpacePos(pParams->pLocation, pSet->vPos);
	dynNodeGetWorldSpaceRot(pParams->pLocation, pSet->qRot);
	if (pSet->eScaleFlag == DynParticleEmitFlag_Ignore)
		unitVec3(pSet->vScale);
	else
		dynNodeGetWorldSpaceScale(pParams->pLocation, pSet->vScale);


	pSet->uiNumAtNodes = pParams->peaAtNodes?eaSize(pParams->peaAtNodes):0;
	if (pSet->uiNumAtNodes > 0)
	{
		U32 uiAtNodeAllocSize = pSet->uiNumAtNodes * sizeof(DynNodeRef);
		U32 uiWeightAllocSize = pParams->peaWeights?(ea32Size(pParams->peaWeights) * sizeof(F32)):0;
		U32 uiEmitTargetAllocSize = pParams->peaEmitTargetNodes?(eaSize(pParams->peaEmitTargetNodes) * sizeof(DynNodeRef)):0;

		char* pcAlloc = calloc( uiAtNodeAllocSize + uiWeightAllocSize + uiEmitTargetAllocSize, 1);
		pSet->pAtNodes = (DynNodeRef*)(pcAlloc);
		pSet->pfWeights = uiWeightAllocSize?((F32*)(pcAlloc + uiAtNodeAllocSize)):NULL;
		pSet->pEmitTargetNodes = uiEmitTargetAllocSize?((DynNodeRef*)(pcAlloc + uiAtNodeAllocSize + uiWeightAllocSize)):NULL;

		FOR_EACH_IN_EARRAY((*pParams->peaAtNodes), const char, pcAtNode)
		{
			const DynNode* pNode;
			pSet->pAtNodes[ipcAtNodeIndex].pcNode = pcAtNode;

			if (pNode = dynFxNodeByName(pcAtNode, pParams->pParentFX))
			{
				ADD_SIMPLE_POINTER_REFERENCE_DYN(pSet->pAtNodes[ipcAtNodeIndex].hNode, pNode);
			}

			if (uiWeightAllocSize)
				pSet->pfWeights[ipcAtNodeIndex] = (*pParams->peaWeights)[ipcAtNodeIndex];

			if (uiEmitTargetAllocSize)
			{
				pSet->pEmitTargetNodes[ipcAtNodeIndex].pcNode = (*pParams->peaEmitTargetNodes)[ipcAtNodeIndex];
				if (pNode = dynFxNodeByName((*pParams->peaEmitTargetNodes)[ipcAtNodeIndex], pParams->pParentFX))
				{
					ADD_SIMPLE_POINTER_REFERENCE_DYN(pSet->pEmitTargetNodes[ipcAtNodeIndex].hNode, pNode);
				}
			}
		}
		FOR_EACH_END;

	}


	++dynDebugState.uiNumFastParticleSets;

	PERFINFO_AUTO_STOP_FUNC();
	return pSet;
}

void dynFxFastParticleSetChangeInfo( DynFxFastParticleSet* pSet, const DynFxFastParticleInfo* pInfo )
{
	REMOVE_HANDLE(pSet->hInfo);
	SET_HANDLE_FROM_REFERENT(hFxParticleDict, (DynFxFastParticleInfo*)pInfo, pSet->hInfo);
}

void dynFxFastParticleSetDestroy( DynFxFastParticleSet* pSet)
{
	if (pSet->particleQueue.pStorage)
		dynFxFastParticleSetDeinitQueue(pSet);
	REMOVE_HANDLE(pSet->hInfo);
	REMOVE_HANDLE(pSet->hLocation);
	REMOVE_HANDLE(pSet->hMagnet);
	REMOVE_HANDLE(pSet->hEmitTarget);
	REMOVE_HANDLE(pSet->hTransformTarget);
	REMOVE_HANDLE(pSet->hParentFX);
	if (pSet->uiNumAtNodes > 0)
	{
		U32 uiNodeIndex;
		for (uiNodeIndex=0; uiNodeIndex<pSet->uiNumAtNodes; ++uiNodeIndex)
		{
			REMOVE_HANDLE(pSet->pAtNodes[uiNodeIndex].hNode);
			if (pSet->pEmitTargetNodes)
				REMOVE_HANDLE(pSet->pEmitTargetNodes[uiNodeIndex].hNode);
		}
		
		free(pSet->pAtNodes); // includes pfWeights and pEmitTargetNodes
	}

	if(pSet->pvModelEmitOffsetVerts) {
		free(pSet->pvModelEmitOffsetVerts);
	}
	if(pSet->pModelEmitOffsetTris) {
		free(pSet->pModelEmitOffsetTris);
		free(pSet->pModelEmitOffsetTriSizes);
	}

	MP_FREE(DynFxFastParticleSet, pSet);
	--dynDebugState.uiNumFastParticleSets;
}




static void dynFxFastParticleUpdate( DynFxFastParticle* pParticle, F32 fDeltaTime, DynFxFastParticleInfo* pInfo, DynFxFastParticleSet* pSet, const Vec3 vMagnetPos)
{
	Vec3 vVel; // using a stack variable to avoid load-hit-store penalties on the xbox
	Vec3 vPos;
	if (pParticle->bInvisible)
		return;

	if (pInfo->bLockEnds && (pParticle->bFirstInLine || pParticle->bLastInLine) )
	{
		return;
	}

	scaleAddVec3(pParticle->vAccel, fDeltaTime, pParticle->vVel, vVel);

	if(pParticle->fMass) {		
		Vec3 vWindAccel = {0};
		Vec3 vParticleWorldPos;
		F32 fWindMag = 0;
		if(pSet->ePosFlag == DynParticleEmitFlag_Update) {
			addVec3(pParticle->vPos, pSet->vPos, vParticleWorldPos);
		} else {
			copyVec3(pParticle->vPos, vParticleWorldPos);
		}
		fWindMag = dynWindGetAtPositionPastEdge(vParticleWorldPos, vWindAccel, true);
		scaleAddVec3(vWindAccel, (fWindMag * fDeltaTime) / pParticle->fMass, vVel, vVel);
	}

	if (pParticle->fDrag)
	{
		F32 fSpeed = lengthVec3(vVel);
		F32 fDragAccel = CLAMP(1.0f - MIN(fSpeed * pParticle->fDrag * fDeltaTime, 1.0f), 0.0f, 1.0f);
		scaleVec3(vVel, fDragAccel, vVel);
	}

	scaleAddVec3(vVel, fDeltaTime, pParticle->vPos, vPos);

	addVec3(vPos, pSet->vStickiness, vPos);


	if (pInfo->fVelocityJitterUpdate > 0.0f)
	{
		pParticle->fTimeSinceVelUpdate += fDeltaTime;
		if (pParticle->fTimeSinceVelUpdate > pInfo->fVelocityJitterUpdateInverse)
		{
			// Reapply jitter
			jitterAndRotate(vVel, pInfo->vVelocity, pInfo->vVelocityJitter, pParticle->qRot, NULL, pSet->eRotFlag);
			pParticle->fTimeSinceVelUpdate -= pInfo->fVelocityJitterUpdateInverse;
			// If we're still greater, then just zero it rather than looping
			if (pParticle->fTimeSinceVelUpdate > pInfo->fVelocityJitterUpdateInverse)
			{
				pParticle->fTimeSinceVelUpdate = 0.0f;
			}
		}
	}
	if (pInfo->fAccelerationJitterUpdate > 0.0f)
	{
		pParticle->fTimeSinceAccUpdate += fDeltaTime;
		if (pParticle->fTimeSinceAccUpdate > pInfo->fAccelerationJitterUpdateInverse)
		{
			// Reapply jitter
			jitterAndRotate(pParticle->vAccel, pInfo->vAcceleration, pInfo->vAccelerationJitter, pParticle->qRot, NULL, pSet->eRotFlag);
			pParticle->fTimeSinceAccUpdate -= pInfo->fAccelerationJitterUpdateInverse;
			// If we're still greater, then just zero it rather than looping
			if (pParticle->fTimeSinceAccUpdate > pInfo->fAccelerationJitterUpdateInverse)
			{
				pParticle->fTimeSinceAccUpdate = 0.0f;
			}
		}
	}
	if (pInfo->fMagnetism || pInfo->fGoTo)
	{
		Vec3 vDiff;
		F32 fDist;
		subVec3(vPos, vMagnetPos, vDiff);
		fDist = lengthVec3(vDiff);
		if (fDist > 0.0f)
		{
			if (pInfo->fKillRadius > 0.0f && fDist < pInfo->fKillRadius)
			{
				pParticle->bInvisible = 1;
				return;
			}
			if (pInfo->fMagnetism)
			{
				Vec3 vMagnetDeltaV;
				scaleVec3(vDiff, ( (-pInfo->fMagnetism * fDeltaTime) / (fDist * fDist ) ), vMagnetDeltaV);
				addVec3(vVel, vMagnetDeltaV, vVel);
			}
			if (pInfo->fGoTo)
			{
				Vec3 vIntendedV, vDiffV;
				scaleVec3(vDiff, ( (-pInfo->fGoTo * 0.1f) / (fDist ) ), vIntendedV);
				subVec3(vIntendedV, vVel, vDiffV);
				scaleVec3(vDiffV, MIN(fDeltaTime, 1.0f), vDiffV);
				addVec3(vVel, vDiffV, vVel);
			}
		}
	}
	copyVec3(vVel, pParticle->vVel);
	copyVec3(vPos, pParticle->vPos);
}

#ifdef USE_VECTOR_OPTIMIZATIONS
// collection of neccessary stuff for fast vector based init and update to run
typedef struct FastParticleInfoVector
{
	vec_float4	vPosition;
	vec_float4	vPositionJitter;
	vec_float4	vVelocity;
	vec_float4	vVelocityJitter;
	vec_float4	vAcceleration;
	vec_float4	vAccelerationJitter;
	vec_float4	vPositionSphereJitter;
	vec_float4	vMagnetPosn;
	vec_float4	vMagnetism;
	vec_float4	vGoTo;
	vec_float4	vStickiness;
	vec_float4	vKillRadiusSq;
	vec_float4	vScale;
	float fVelocityJitterUpdateInverse;
	float fAccelerationJitterUpdateInverse;
	float fDrag; 
	float fDragJitter;
	float fGravity; 
	float fGravityJitter;

	DynParticleEmitFlag eRotFlag;
	DynParticleEmitFlag eScaleFlag;
	bool bLockEnds;
	bool bDoAttractor;
	bool bJitterSphere;
	bool bJitterSphereShell; 
} FastParticleInfoVector;
	
static void InitFastParticleInfoVector(const DynFxFastParticleSet* pSet, const DynFxFastParticleInfo* pInfo, const Vec3 vMagnetPos, FastParticleInfoVector *pVInfo )
{
	((float *)&pVInfo->vMagnetPosn)[0] = vMagnetPos[0];
	((float *)&pVInfo->vMagnetPosn)[1] = vMagnetPos[1];
	((float *)&pVInfo->vMagnetPosn)[2] = vMagnetPos[2];
	((float *)&pVInfo->vMagnetPosn)[3] = 0.0f;	
	
	((float *)&pVInfo->vPosition)[0] = pInfo->vPosition[0];
	((float *)&pVInfo->vPosition)[1] = pInfo->vPosition[1];
	((float *)&pVInfo->vPosition)[2] = pInfo->vPosition[2];
	((float *)&pVInfo->vPosition)[3] = 0.0f;
	
	((float *)&pVInfo->vPositionJitter)[0] = pInfo->vPositionJitter[0];
	((float *)&pVInfo->vPositionJitter)[1] = pInfo->vPositionJitter[1];
	((float *)&pVInfo->vPositionJitter)[2] = pInfo->vPositionJitter[2];
	((float *)&pVInfo->vPositionJitter)[3] = 0.0f;

	((float *)&pVInfo->vVelocity)[0] = pInfo->vVelocity[0];
	((float *)&pVInfo->vVelocity)[1] = pInfo->vVelocity[1];
	((float *)&pVInfo->vVelocity)[2] = pInfo->vVelocity[2];
	((float *)&pVInfo->vVelocity)[3] = 0.0f;
	
	((float *)&pVInfo->vVelocityJitter)[0] = pInfo->vVelocityJitter[0];
	((float *)&pVInfo->vVelocityJitter)[1] = pInfo->vVelocityJitter[1];
	((float *)&pVInfo->vVelocityJitter)[2] = pInfo->vVelocityJitter[2];
	((float *)&pVInfo->vVelocityJitter)[3] = 0.0f;	

	((float *)&pVInfo->vAcceleration)[0] = pInfo->vAcceleration[0];
	((float *)&pVInfo->vAcceleration)[1] = pInfo->vAcceleration[1];
	((float *)&pVInfo->vAcceleration)[2] = pInfo->vAcceleration[2];
	((float *)&pVInfo->vAcceleration)[3] = 0.0f;
	
	((float *)&pVInfo->vAccelerationJitter)[0] = pInfo->vAccelerationJitter[0];
	((float *)&pVInfo->vAccelerationJitter)[1] = pInfo->vAccelerationJitter[1];
	((float *)&pVInfo->vAccelerationJitter)[2] = pInfo->vAccelerationJitter[2];
	((float *)&pVInfo->vAccelerationJitter)[3] = 0.0f;	
	
	((float *)&pVInfo->vPositionSphereJitter)[0] = pInfo->vPositionSphereJitter[0];
	((float *)&pVInfo->vPositionSphereJitter)[1] = pInfo->vPositionSphereJitter[1];
	((float *)&pVInfo->vPositionSphereJitter)[2] = pInfo->vPositionSphereJitter[2];
	((float *)&pVInfo->vPositionSphereJitter)[3] = 0.0f;	

	((float *)&pVInfo->vMagnetism)[0] = -pInfo->fMagnetism;
	((float *)&pVInfo->vMagnetism)[1] = -pInfo->fMagnetism;
	((float *)&pVInfo->vMagnetism)[2] = -pInfo->fMagnetism;
	((float *)&pVInfo->vMagnetism)[3] = 0.0f;	
	
	((float *)&pVInfo->vGoTo)[0] = -0.1f*pInfo->fGoTo;
	((float *)&pVInfo->vGoTo)[1] = -0.1f*pInfo->fGoTo;
	((float *)&pVInfo->vGoTo)[2] = -0.1f*pInfo->fGoTo;
	((float *)&pVInfo->vGoTo)[3] = 0.0f;

	((float *)&pVInfo->vKillRadiusSq)[0] = pInfo->fKillRadius*pInfo->fKillRadius;
	((float *)&pVInfo->vKillRadiusSq)[1] = 0.0f;
	((float *)&pVInfo->vKillRadiusSq)[2] = 0.0f;
	((float *)&pVInfo->vKillRadiusSq)[3] = 0.0f;
	
	pVInfo->fVelocityJitterUpdateInverse = pInfo->fVelocityJitterUpdateInverse;
	pVInfo->fAccelerationJitterUpdateInverse = pInfo->fAccelerationJitterUpdateInverse;

	pVInfo->fDrag = pInfo->fDrag; 
	pVInfo->fDragJitter = pInfo->fDragJitter;
	pVInfo->fGravity = pInfo->fGravity; 
	pVInfo->fGravityJitter = pInfo->fGravityJitter;

	pVInfo->bLockEnds = pInfo->bLockEnds;
	pVInfo->bDoAttractor = pInfo->fMagnetism || pInfo->fGoTo;
	pVInfo->bJitterSphere = pInfo->bJitterSphere;
	pVInfo->bJitterSphereShell = pInfo->bJitterSphereShell;


	((float *)&pVInfo->vScale)[0] = pSet->vScale[0];
	((float *)&pVInfo->vScale)[1] = pSet->vScale[1];
	((float *)&pVInfo->vScale)[2] = pSet->vScale[2];
	((float *)&pVInfo->vScale)[3] = 0.0f;	
	
	((float *)&pVInfo->vStickiness)[0] = pSet->vStickiness[0];
	((float *)&pVInfo->vStickiness)[1] = pSet->vStickiness[1];
	((float *)&pVInfo->vStickiness)[2] = pSet->vStickiness[2];
	((float *)&pVInfo->vStickiness)[3] = 0.0f;	

	pVInfo->eRotFlag = pSet->eRotFlag;
	pVInfo->eScaleFlag = pSet->eScaleFlag;
}

static void dynFxFastParticleUpdateVector( DynFxFastParticle* pParticle, FastParticleInfoVector *pVInfo, F32 fDeltaTime )
{
	if (pParticle->bInvisible || (pVInfo->bLockEnds && (pParticle->bFirstInLine || pParticle->bLastInLine) ))
		return;

	{
		const vec_float4  vDeltaTime = vec_splats(fDeltaTime);	
		const vector bool vi1110 = {0xffffffff, 0xffffffff, 0xffffffff, 0 };
		
		vec_float4	vVel;	
		vec_float4	vPos;

		// pull packed vec3s into vec4s, zero out w 
		vec_float4	*pPartVec = (vec_float4 *)pParticle->vec;
		vec_float4	vPartQRot = pPartVec[0];
		vec_float4	vPartPos   = vec_and( vi1110, pPartVec[1] );
		vec_float4	vPartVel   = vec_and( vi1110, vec_sld(pPartVec[1], pPartVec[2], 12) );
		vec_float4	vPartAccel = vec_and( vi1110, vec_sld(pPartVec[2], pPartVec[3], 8) );
		vec_float4	vPartScale = vec_sld(pPartVec[3], __vzero(), 4); 
		vec_float4	vPartMisc = pPartVec[4];
		
		vVel = vec_madd(vPartAccel, vDeltaTime, vPartVel);

		// apply drag to velocity
		if (pParticle->fDrag)
		{
			// calculate speed
			register vec_float4 vSpeed = vec_madd( vVel, vVel, __vzero() );
			vSpeed = vec_add( vSpeed, vec_sld(vSpeed, vSpeed, 4));	// x = x+z, y =y+w
			vSpeed = vec_add( vSpeed, vec_sld(vSpeed, vSpeed, 8));	// x = x+y
			vSpeed = vec_re( vec_rsqrte(vSpeed) );			//  vSpeed = sqrt(vSpeed);

			vSpeed = vec_madd(vSpeed, vec_splat(vPartMisc, 1), __vzero());		//speed*drag*deltaTime
			vSpeed = vec_madd(vSpeed, vDeltaTime, __vzero());
			vSpeed = vec_min(vSpeed, __vone());
			vSpeed = vec_sub(__vone(), vSpeed);			// 1-min(speed,1)
			vVel = vec_madd(vSpeed, vVel, __vzero());
		}

		vPos = vec_madd(vVel, vDeltaTime, vec_add(vPartPos, pVInfo->vStickiness));  // vPos = vVel*deltaTime + PartPos + stickiness

		if (pVInfo->fVelocityJitterUpdateInverse > 0.0f)
		{
			pParticle->fTimeSinceVelUpdate += fDeltaTime;
			if (pParticle->fTimeSinceVelUpdate > pVInfo->fVelocityJitterUpdateInverse)
			{
				// Reapply jitter
				vVel = jitterAndRotateVec4HNoSeed(pVInfo->vVelocity, pVInfo->vVelocityJitter, vPartQRot, pVInfo->eRotFlag);
				pParticle->fTimeSinceVelUpdate -= pVInfo->fVelocityJitterUpdateInverse;
				// If we're still greater, then just zero it rather than looping
				if (pParticle->fTimeSinceVelUpdate > pVInfo->fVelocityJitterUpdateInverse)
				{
					pParticle->fTimeSinceVelUpdate = 0.0f;
				}
			}
		}

		if (pVInfo->fAccelerationJitterUpdateInverse > 0.0f)
		{
			pParticle->fTimeSinceAccUpdate += fDeltaTime;
			if (pParticle->fTimeSinceAccUpdate > pVInfo->fAccelerationJitterUpdateInverse)
			{
				// Reapply jitter
				vPartAccel = jitterAndRotateVec4HNoSeed(pVInfo->vAcceleration, pVInfo->vAccelerationJitter, vPartQRot, pVInfo->eRotFlag);
				pParticle->fTimeSinceAccUpdate -= pVInfo->fAccelerationJitterUpdateInverse;
				
				// If we're still greater, then just zero it rather than looping
				if (pParticle->fTimeSinceAccUpdate > pVInfo->fAccelerationJitterUpdateInverse)
				{
					pParticle->fTimeSinceAccUpdate = 0.0f;
				}
			}
		}

		if (pVInfo->bDoAttractor)
		{
			vec_float4 vDiff = vec_sub(vPos, pVInfo->vMagnetPosn);
			register vec_float4 distSq = vec_madd(vDiff, vDiff, __vzero());

			distSq = vec_add( distSq, vec_sld(distSq, distSq, 4));	// x = x+z, y =y+w
			distSq = vec_add( distSq, vec_sld(distSq, distSq, 8));	// x = x+y

			if ( vec_any_gt(distSq, __vzero()) )
			{
				register vec_float4 deltaV;
				register vec_float4 vDiffV;

				if (vec_any_lt(distSq, pVInfo->vKillRadiusSq))
				{
					pParticle->bInvisible = 1;
					return;
				}

				// do pInfo->fMagnetism
				deltaV = vec_madd(pVInfo->vMagnetism, vDeltaTime, __vzero());
				deltaV = vec_madd(deltaV, vec_re(distSq), __vzero());
				deltaV = vec_madd(deltaV, vDiff, __vzero());

				// do pInfo->fGoTo
				vDiffV = vec_madd(vDiff, pVInfo->vGoTo, __vzero());
				vDiffV = vec_madd(vDiffV, vec_rsqrte(distSq), __vzero());
				vDiffV = vec_sub(vDiffV, vVel);
				vDiffV = vec_madd( vDiffV, vec_min(vDeltaTime, __vone()), deltaV);
				
				vVel = vec_add(vVel, vDiffV);
			}
		}

		// copy updated position and veloctity to particle
		pPartVec[1] = vec_perm( vPos, vVel, _MATH_PERM_XYZA );
		pPartVec[2] = vec_perm( vVel, vPartAccel, _MATH_PERM_YZAB );
	}
}
#endif

static void dynFxFastParticleJumpStartParticle(DynFxFastParticle* pParticle, DynFxFastParticleInfo* pInfo, DynFxFastParticleSet* pSet, const Vec3 vMagnetPos, F32 fTimeToPass)
{
	F32 fTimeStep = 1/60.0f;
	F32 fTime = fTimeStep;
	if (!dynDebugState.bEditorHasOpened)
	{
		if (fTimeToPass > 0.05f)
			fTimeStep = fTimeToPass / 3.0f;
	}
	while (fTime < fTimeToPass)
	{
		dynFxFastParticleUpdate(pParticle, fTimeStep, pInfo, pSet, vMagnetPos);
		fTime += fTimeStep;
	}
	if (fTime > fTimeToPass)
	{
		fTime -= fTimeStep;
		dynFxFastParticleUpdate(pParticle, fTimeToPass - fTime, pInfo, pSet, vMagnetPos);
	}
}



static void applyJitter( Vec3 vValue, const Vec3 vOffset, const Vec3 vJitter, U32* puiSeed, bool bJitterSphere, bool bJitterSphereShell, F32 fUniformValue )
{
	Vec3 vRand;
	Vec3 vJOffset;
	if (bJitterSphere)
	{
		if (bJitterSphereShell)
		{
			if (fUniformValue >= 0.0f)
				uniformSphereShellSlice(fUniformValue * 2.0f - 1.0f, RAD(vJitter[0]), vJitter[1]/180.0f, vJitter[2], vJOffset);
			else
				randomSphereShellSliceSeeded(puiSeed, RandType_BLORN, RAD(vJitter[0]), vJitter[1]/180.0f, vJitter[2], vJOffset);
		}
		else
			randomSphereSliceSeeded(puiSeed, RandType_BLORN, RAD(vJitter[0]), RAD(vJitter[1]), vJitter[2], vJOffset);
	}
	else
	{
		if (fUniformValue >= 0.0f)
		{
			setVec3same(vRand, fUniformValue * 2.0f - 1.0f);
		}
		else
		{
			randomVec3Seeded(puiSeed, RandType_BLORN, vRand);
		}
		mulVecVec3(vJitter, vRand, vJOffset);
	}

	addVec3(vOffset, vJOffset, vValue);
}

static void applyRotate( Vec3 vValue, const Quat qRot, DynParticleEmitFlag eRotFlag)
{
	if (eRotFlag == DynParticleEmitFlag_Inherit)
	{
		Vec3 vTemp;
		copyVec3(vValue, vTemp);
		quatRotateVec3(qRot, vTemp, vValue);
	}
}

static void jitterAndRotate( Vec3 vValue, const Vec3 vOffset, const Vec3 vJitter, const Quat qRot, U32* puiSeed, DynParticleEmitFlag eRotFlag)
{
	applyJitter(vValue, vOffset, vJitter, puiSeed, false, false, -1.0f);
	applyRotate(vValue, qRot, eRotFlag);
}

#ifdef USE_VECTOR_OPTIMIZATIONS

inline vec_float4 quatRotateVec4H(vec_float4 q, vec_float4 v)
{
	// this function is a literal interpretation of quatRotateVec3() adapted for vectors
	// It doesn't exactly match any textbook definitions for rotating a vector by quaternion
	vec_float4 out;

	vec_float4 vN2220 = {-2.0f, -2.0f, -2.0f, 0.0f };
	vec_float4 v2220 = { 2.0f, 2.0f, 2.0f, 0.0f };
	vec_float4 v1N1N10 = {  1.0f, -1.0f, -1.0f, 0.0f };
	vec_float4 vN11N10 = { -1.0f,  1.0f, -1.0f, 0.0f };
	vec_float4 vN1N110 = { -1.0f, -1.0f,  1.0f, 0.0f };

	// cross product terms
	vec_float4 qc1 = vec_perm(q, q, _MATH_PERM_YZXx);
	vec_float4 qc2 = vec_perm(q, q, _MATH_PERM_ZXYx);
	vec_float4 vc1 = vec_perm(v, v, _MATH_PERM_ZXYx);
	vec_float4 vc2 = vec_perm(v, v, _MATH_PERM_YZXx);
	vec_float4 c1 = vec_madd(qc1, vc1, __vzero());
	vec_float4 c2 = vec_madd(qc2, vc2, __vzero());
	vec_float4 c = vec_sub(c1, c2);					// c should now contain q cross v

	vec_float4 t1 = vec_madd(vN2220, vec_splat(q, 3), __vzero() );	// t1 = < -2w, -2w, -2w, 0  >
	
	vec_float4 t2 = vec_madd(q, v2220, __vzero() );					// t2 = < 2Qx, 2Qy, 2Qz, 0 >
	
	// t4 = < QyVy + QzVz, QxVx + QzVz, QxVx + QyVy, 0 >
	vec_float4 t3 = vec_madd( q, v, __vzero() );					// t3 = < QxVx, QyVy, QzVz >
	vec_float4 t4 = vec_add( vec_perm(t3, t3, _MATH_PERM_YXXx), vec_perm(t3, t3,_MATH_PERM_ZZYx)); 

	// collect the q squared terms
	register vec_float4 t5 = vec_madd(q, q, __vzero());						// t5 = < QxQx, QyQy, QzQz, QwQw >
	register vec_float4 t6 = vec_madd( vec_splat(t5,0), v1N1N10, vec_splat(t5, 3));
	t6 = vec_madd( vec_splat(t5,1), vN11N10, t6);
	t6 = vec_madd( vec_splat(t5,2), vN1N110, t6);

	// final calculations
	out = vec_madd(v, t6, __vzero());
	out = vec_madd(t1, c, out);
	out = vec_madd(t2, t4, out);
		
	return (out);
}

static vec_float4 jitterAndRotateVec4HSeed( const vec_float4 vOffset, const vec_float4 vJitter, const vec_float4 qRot, U32* puiSeed, DynParticleEmitFlag eRotFlag)
{
	vec_float4 vValue = vec_madd(vJitter, random3Vec4HFastBlornSeed(puiSeed), vOffset );

	if (eRotFlag == DynParticleEmitFlag_Inherit)
		vValue = quatRotateVec4H(qRot, vValue);

	return(vValue);
}

static vec_float4 jitterAndRotateVec4HNoSeed( const vec_float4 vOffset, const vec_float4 vJitter, const vec_float4 qRot, DynParticleEmitFlag eRotFlag)
{
	vec_float4 vValue = vec_madd(vJitter, random3Vec4HFastBlorn(), vOffset );

	if (eRotFlag == DynParticleEmitFlag_Inherit)
		vValue = quatRotateVec4H(qRot, vValue);

	return(vValue);
}

static void dynFxFastParticleInitParticle( DynFxFastParticle* pParticle, FastParticleInfoVector *pVInfo, U32 uiSeed, const Vec3 vInheritVelocity, bool bLineEmission, const Vec3 vLineOffset, F32 fUniformValue, F32 fUniformLine, F32 fTimeSinceBirth) 
{
	register vec_float4 vPos;
	U32 uiCopySeed = uiSeed;

	// get particle  numbers into vectors
	const vector bool vi1110 = {0xffffffff, 0xffffffff, 0xffffffff, 0 };
	vec_float4 *pPartVec	= (vec_float4 *)pParticle->vec;
	vec_float4 vPartQRot	= pPartVec[0];
	vec_float4 vPartPos		= vec_and( vi1110, pPartVec[1] );	// zero out w word
	vec_float4 vPartVel;  
	vec_float4 vPartAccel;

	const vec_float4 vInheritVel = { vInheritVelocity[0], vInheritVelocity[1], vInheritVelocity[2], 0.0f };
	const vec_float4 vLineOffs = { vLineOffset[0], vLineOffset[1], vLineOffset[2], 0.0f };
	vec_float4 vRand;

	vRand = (fUniformValue > 0.0f)? vec_splats(fUniformValue * 2.0f - 1.0f) : random3Vec4HFastBlornSeed(&uiCopySeed);
	vPos = vec_madd( pVInfo->vPositionJitter, vRand, pVInfo->vPosition );

	if (pVInfo->bJitterSphere)
	{
		float* vJitter = (float *)&pVInfo->vPositionSphereJitter;
		vec_float4 vJOffset;

		if(pVInfo->bJitterSphereShell)
		{
			if (fUniformValue >= 0.0f)
				uniformSphereShellSlice(fUniformValue * 2.0f - 1.0f, RAD(vJitter[0]), vJitter[1]/180.0f, vJitter[2], (float *)&vJOffset);
			else
				randomSphereShellSliceSeeded(&uiCopySeed, RandType_BLORN, RAD(vJitter[0]), vJitter[1]/180.0f, vJitter[2], (float *)&vJOffset);
		}
		else
		{
			randomSphereSliceSeeded(&uiCopySeed, RandType_BLORN, RAD(vJitter[0]), RAD(vJitter[1]), vJitter[2], (float *)&vJOffset);
		}
		vPos = vec_add(vPos, vJOffset);
	}
		
	if (pVInfo->eScaleFlag != DynParticleEmitFlag_Ignore)
	{
		vPos = vec_madd(vPos, pVInfo->vScale, __vzero());
	}

	if(pVInfo->eRotFlag)
	{
		vPos = quatRotateVec4H( vPartQRot, vPos);	
	}

	if (bLineEmission)
	{
		F32 fRandomScale = fUniformLine >= 0.0f ? fUniformLine : randomPositiveF32Seeded(&uiCopySeed,RandType_BLORN);
		vec_float4 vOffset = vec_madd(vLineOffs, vec_splats(fRandomScale), __vzero() );

		if (pVInfo->bLockEnds && ( fUniformLine == 0.0f || fUniformLine == 1.0f) )
		{
			vPartPos = vec_add(vPartPos, vOffset);
		}
		else
		{
			vPartPos = vec_add(vPos, vPartPos);
			vPartPos = vec_add(vOffset, vPartPos);
		}
		if (fUniformLine == 0.0f)
		{
			pParticle->bFirstInLine = 1;
		}
		else if (fUniformLine == 1.0f)
		{
			pParticle->bLastInLine = 1;
		}
	}
	else
		vPartPos = vec_add(vPos, vPartPos);


	vPartVel = jitterAndRotateVec4HSeed( pVInfo->vVelocity, pVInfo->vVelocityJitter, vPartQRot, &uiCopySeed, pVInfo->eRotFlag);
	vPartAccel = jitterAndRotateVec4HSeed( pVInfo->vAcceleration, pVInfo->vAccelerationJitter, vPartQRot, &uiCopySeed, pVInfo->eRotFlag);

	pParticle->fDrag = CLAMP(pVInfo->fDrag + pVInfo->fDragJitter * randomF32Seeded(&uiCopySeed,RandType_BLORN), 0.0f, 1.0f);

	vPartVel = vec_add(vInheritVel, vPartVel);

	// copy updated position and veloctity to particle
	pPartVec[1] = vec_perm( vPartPos, vPartVel, _MATH_PERM_XYZA );
	pPartVec[2] = vec_perm( vPartVel, vPartAccel, _MATH_PERM_YZAB );

	// override accel for gravity (not rotated, always down)
	pParticle->vAccel[1] -= pVInfo->fGravity + pVInfo->fGravityJitter * randomF32Seeded(&uiCopySeed,RandType_BLORN);

	// Jump Start Particle
	{
		F32 fTimeStep = 1/60.0f;
		F32 fTime = fTimeStep;
		if (!dynDebugState.bEditorHasOpened)
		{
			if (fTimeSinceBirth > 0.05f)
				fTimeStep = fTimeSinceBirth / 3.0f;
		}
		while (fTime < fTimeSinceBirth)
		{
			dynFxFastParticleUpdateVector( pParticle, pVInfo, fTimeStep );
			fTime += fTimeStep;
		}
		if (fTime > fTimeSinceBirth)
		{
			fTime -= fTimeStep;
			dynFxFastParticleUpdateVector( pParticle, pVInfo, fTimeSinceBirth - fTime);
		}
	}
}

#else

void dynFxFastParticleChangeModel(DynFxFastParticleSet* pSet, const char *pcModelName) {
	
	pSet->pcModelPattern = pcModelName;

	if(pSet->pModelEmitOffsetTris) {
		free(pSet->pModelEmitOffsetTris);
		pSet->pModelEmitOffsetTris = NULL;
	}

	if(pSet->pModelEmitOffsetTriSizes) {
		free(pSet->pModelEmitOffsetTriSizes);
		pSet->pModelEmitOffsetTriSizes = NULL;
	}

	if(pSet->pvModelEmitOffsetVerts) {
		free(pSet->pvModelEmitOffsetVerts);
		pSet->pvModelEmitOffsetVerts = NULL;
	}

	pSet->uiNumEmitVerts = 0;
	pSet->uiNumTriangles = 0;
	pSet->fModelTotalArea = 0;
}

static void dynFxFastParticleInitParticle( DynFxFastParticle* pParticle, DynFxFastParticleInfo* pInfo, DynFxFastParticleSet* pSet, U32 uiSeed, const Vec3 vInheritVelocity, const Vec3 vMagnetPos, bool bLineEmission, const Vec3 vLineOffset, F32 fUniformValue, F32 fUniformLine, F32 fTimeSinceBirth) 
{
	Vec3 vPos;
	F32 fGrav;
	U32 uiCopySeed = uiSeed;

	applyJitter(vPos, pInfo->vPosition, pInfo->vPositionJitter, &uiCopySeed, false, false, fUniformValue);
	if (pInfo->bJitterSphere)
		applyJitter(vPos, vPos, pInfo->vPositionSphereJitter, &uiCopySeed, true, pInfo->bJitterSphereShell, fUniformValue);

	if(pSet->pcModelPattern && pSet->pcModelPattern[0] && !pSet->uiNumEmitVerts) {

		// Model emitter specified but not yet loaded.

		// Save model vertices to use as offsets.
		ModelHeader* pAPI = wlModelHeaderFromName(pSet->pcModelPattern);
		Model *pModel;
		ModelLOD *pModelLod = NULL;

		if(!pAPI) {
			Errorf("Emitter pattern model not found: %s", pSet->pcModelPattern);
			pSet->pcModelPattern = NULL;
		} else {
			pModel = modelFromHeader(pAPI, true, WL_FOR_FX);
			if(pModel) pModelLod = modelLODLoadAndMaybeWait(pModel, 0, false);
		}

		if(pModelLod) {
			if(pSet->bUseModelTriangles) {
				geoProcessTempData(createEmitTrisFromModel, pSet, pModel, 0, NULL, true, true, true, true, NULL);
			} else {
				geoProcessTempData(createEmitPointsFromModel, pSet, pModel, 0, NULL, true, true, true, true, NULL);
			}
		}
	}

	if(pSet->uiNumTriangles) {

		F32 selectedPt = randomPositiveF32() * pSet->fModelTotalArea;
		F32 area = 0;
		unsigned int i;

		for(i = 0; i < pSet->uiNumTriangles && area < selectedPt; i++) {
			area += pSet->pModelEmitOffsetTriSizes[i];
		}

		if(i > 0) {

			Vec3 ab;
			Vec3 ac;
			Vec3 offset;
			Vec3 vTmp;

			// X and Y positions using two edges of the triangle as axes.
			F32 x = randomPositiveF32();
			F32 y = randomPositiveF32();

			// Constrain X and Y to the area of the triangle.
			if(x + y > 1) {
				x = 1.0 - x;
				y = 1.0 - y;
			}

			i--;

			subVec3(pSet->pvModelEmitOffsetVerts[pSet->pModelEmitOffsetTris[i*3 + 1]], pSet->pvModelEmitOffsetVerts[pSet->pModelEmitOffsetTris[i*3]], ab);
			subVec3(pSet->pvModelEmitOffsetVerts[pSet->pModelEmitOffsetTris[i*3 + 2]], pSet->pvModelEmitOffsetVerts[pSet->pModelEmitOffsetTris[i*3]], ac);

			scaleVec3(ab, x, offset);
			scaleAddVec3(ac, y, offset, offset);
			addVec3(offset, pSet->pvModelEmitOffsetVerts[pSet->pModelEmitOffsetTris[i*3]], vTmp);
			addVec3(vTmp, vPos, vPos);

		}

	} else {

		if(pSet->uiNumEmitVerts) {
			// Pick a random vertex on the model to start from.
			Vec3 *randomVert = &(pSet->pvModelEmitOffsetVerts[rand() % pSet->uiNumEmitVerts]);
			addVec3(*randomVert, vPos, vPos);
		}

	}

	if (pSet->eScaleFlag != DynParticleEmitFlag_Ignore)
	{
		Vec3 vRealScale;
		interpVec3(pSet->fScalePosition, unitvec3, pSet->vScale, vRealScale);
		mulVecVec3(vPos, vRealScale, vPos);
	}

	applyRotate(vPos, pParticle->qRot, pSet->eRotFlag);

	if (bLineEmission)
	{
		F32 fRandomScale = fUniformLine >= 0.0f ? fUniformLine : randomPositiveF32Seeded(&uiCopySeed,RandType_BLORN);
		Vec3 vOffset;
		scaleVec3(vLineOffset, fRandomScale, vOffset);
		if (pInfo->bLockEnds && ( fUniformLine == 0.0f || fUniformLine == 1.0f) )
		{
			addVec3(pParticle->vPos, vOffset, pParticle->vPos);
		}
		else
		{
			addVec3(vPos, pParticle->vPos, pParticle->vPos);
			addVec3(pParticle->vPos, vOffset, pParticle->vPos);
		}
		if (fUniformLine == 0.0f)
		{
			pParticle->bFirstInLine = 1;
		}
		else if (fUniformLine == 1.0f)
		{
			pParticle->bLastInLine = 1;
		}
	}
	else
		addVec3(vPos, pParticle->vPos, pParticle->vPos);

	if (pInfo->fVelocityOut)
	{
		Vec3 vDiff;
		Vec3 vInitialVel;
		subVec3(pParticle->vPos, vMagnetPos, vDiff);
		normalVec3(vDiff);
		scaleAddVec3(vDiff, pInfo->fVelocityOut, pInfo->vVelocity, vInitialVel);
		jitterAndRotate(pParticle->vVel, vInitialVel, pInfo->vVelocityJitter, pParticle->qRot, &uiCopySeed, pSet->eRotFlag);
	}
	else
		jitterAndRotate(pParticle->vVel, pInfo->vVelocity, pInfo->vVelocityJitter, pParticle->qRot, &uiCopySeed, pSet->eRotFlag);
	jitterAndRotate(pParticle->vAccel, pInfo->vAcceleration, pInfo->vAccelerationJitter, pParticle->qRot, &uiCopySeed, pSet->eRotFlag);

	pParticle->fDrag = CLAMP(pInfo->fDrag + pInfo->fDragJitter * randomF32Seeded(&uiCopySeed,RandType_BLORN), 0.0f, 1.0f);

	// Gravity is not rotated, always down
	fGrav = pInfo->fGravity + pInfo->fGravityJitter * randomF32Seeded(&uiCopySeed,RandType_BLORN);
	pParticle->vAccel[1] -= fGrav;
	addVec3(vInheritVelocity, pParticle->vVel, pParticle->vVel);

	dynFxFastParticleJumpStartParticle(pParticle, pInfo, pSet, vMagnetPos, fTimeSinceBirth);
}
#endif
static void dynFxFastParticleSetGetMagnetPos(DynFxFastParticleSet* pSet, Vec3 vMagnetPos)
{
	DynNode* pMagnet = GET_REF(pSet->hMagnet);
	if (pSet->ePosFlag == DynParticleEmitFlag_Inherit)
	{
		if (pMagnet)
		{
			if (pSet->eRotFlag != DynParticleEmitFlag_Update)
				dynNodeGetWorldSpacePos(pMagnet, vMagnetPos);
			else
			{
				Vec3 vTempPos;
				Quat qInvRot;
				dynNodeGetWorldSpacePos(pMagnet, vTempPos);
				quatInverse(pSet->qRot, qInvRot);
				quatRotateVec3(qInvRot, vTempPos, vMagnetPos);
			}
		}
		else
			copyVec3(pSet->vPos, vMagnetPos);
	}
	else
	{
		Vec3 vTempPos;
		if (pMagnet)
		{
			if (pSet->eRotFlag != DynParticleEmitFlag_Update)
			{
				dynNodeGetWorldSpacePos(pMagnet, vTempPos);
				subVec3(vTempPos, pSet->vPos, vMagnetPos);
			}
			else
			{
				Quat qInvRot;
				dynNodeGetWorldSpacePos(pMagnet, vMagnetPos);
				subVec3(vMagnetPos, pSet->vPos, vTempPos);
				quatInverse(pSet->qRot, qInvRot);
				quatRotateVec3(qInvRot, vTempPos, vMagnetPos);
			}
		}
		else
			zeroVec3(vMagnetPos);
	}
}

static bool dynFxFastParticleSetGetLineOffset(DynFxFastParticleSet* pSet, Vec3 vLineOffset)
{
	DynNode* pEmitTarget = GET_REF(pSet->hEmitTarget);
	Vec3 vEmitTargetPos;

	if (!pEmitTarget)
		return false;

	dynNodeGetWorldSpacePos(pEmitTarget, vEmitTargetPos);
	subVec3(vEmitTargetPos, pSet->vPos, vLineOffset);
	return true;
}

void dynFxFastParticleSetRecalculate(DynFxFastParticleSet* pSet, const Vec3 vInitPos, const Vec3 vInheritVelocity)
{
	DynFxFastParticleInfo* pInfo = GET_REF(pSet->hInfo);
	PoolQueueIterator iter;
	DynFxFastParticle* pParticle;
	Vec3 vMagnetPos;
	Vec3 vLineOffset;
	bool bLineEmission;
#ifdef USE_VECTOR_OPTIMIZATIONS
	FastParticleInfoVector VInfo;
#endif
	dynFxFastParticleSetGetMagnetPos(pSet, vMagnetPos);

#ifdef USE_VECTOR_OPTIMIZATIONS
	InitFastParticleInfoVector(pSet, pInfo, vMagnetPos, &VInfo );		// TODO: move magnet position into here
#endif

	bLineEmission = dynFxFastParticleSetGetLineOffset(pSet, vLineOffset);
	poolQueueGetIterator(&pSet->particleQueue, &iter);
	while (poolQueueGetNextElement(&iter, &pParticle))
	{
		F32 fTotalTime = pSet->fSetTime - pParticle->fTime;
		U32 uiSeed = pParticle->uiMovementSeed;
		// First, init
		copyVec3(vInitPos, pParticle->vPos);
#ifdef USE_VECTOR_OPTIMIZATIONS
		dynFxFastParticleInitParticle(pParticle, &VInfo, uiSeed, vInheritVelocity, bLineEmission, vLineOffset, -1.0f, -1.0f, fTotalTime);
#else
		dynFxFastParticleInitParticle(pParticle, pInfo, pSet, uiSeed, vInheritVelocity, vMagnetPos, bLineEmission, vLineOffset, -1.0f, -1.0f, fTotalTime);
#endif
	}
}

void dynFxFastParticleSetCalcCurrentEmissionRate( DynFxFastParticleSet* pSet, DynFxFastParticleInfo* pInfo ) 
{
	if (pSet->fCurrentDistanceEmissionRate > 0.0f)
		pSet->fCurrentEmissionRate = pSet->fCurrentDistanceEmissionRate + pInfo->fEmissionRatePerFootJitter * randomF32Seeded(NULL, RandType_BLORN);
	else
		pSet->fCurrentEmissionRate = pInfo->fEmissionRate + pInfo->fEmissionRateJitter * randomF32Seeded(NULL, RandType_BLORN);
	pSet->fCurrentEmissionRateInv = (pSet->fCurrentEmissionRate > 0.0f ) ? 1.0f / pSet->fCurrentEmissionRate: 100000.0f;
}

void dynFxFastParticleSetReset(DynFxFastParticleSet* pSet)
{
	DynFxFastParticleInfo* pInfo = GET_REF(pSet->hInfo);
	poolQueueClear(&pSet->particleQueue);
	pSet->fSetTime = 0.0f;
	pSet->fLastEmissionTime = 0.0f;
	if (pInfo)
	{
		dynFxFastParticleSetCalcCurrentEmissionRate(pSet, pInfo);
	}
	pSet->bEmitted = false;
}

typedef struct DynFPBurstParams
{
#ifdef USE_VECTOR_OPTIMIZATIONS
	FastParticleInfoVector* pVInfo;
#endif
	DynFxFastParticleInfo* pInfo;
	DynFxFastParticleSet* pSet;
	const F32* vPos;
	const F32* qRot;
	const F32* vScale;
	const F32* vVelocity;
	const F32* vMagnetPos;
	bool bLineEmission;
	const F32* vLineOffset;
} DynFPBurstParams;



static DynFxFastParticle* dynFxFastParticleCreate(DynFPBurstParams* pParams, const DynNode* pAtNode, U32 uiNodeIndex)
{
	DynFxFastParticle* pNew;
	if (poolQueueIsFull(&pParams->pSet->particleQueue))
	{
		// Some kinds of FX (distance-based emission, for example) can not accurately predict the particle counts they will need, so we use a doubling strategy for those
		U32 uiNewSize = pow2(pParams->pSet->uiNumAllocated+1);
		if (uiNewSize > MAX_PARTICLES_PER_SET)
		{
			if (iFPErrorOnOverflow && !pParams->pSet->bOverflowError)
			{
				Errorf("Fast Particle Set %s had a buffer overflow. Oldest particle killed prematurely.", pParams->pInfo->pcName);
				pParams->pSet->bOverflowError = true;
			}
			poolQueueDequeue(&pParams->pSet->particleQueue, NULL);
		}
		else
		{
			// Check whether we have room to grow
			bool bGrow = true;
			if (pParams->pSet->bEnvironmentFX)
			{
				if ( (dynDebugState.uiNumAllocatedFastParticlesEnvironment + uiNewSize - pParams->pSet->uiNumAllocated) > MAX_ALLOCATED_FAST_PARTICLES_ENVIRONMENT )
					bGrow = false;
				else
					dynDebugState.uiNumAllocatedFastParticlesEnvironment += (uiNewSize - pParams->pSet->uiNumAllocated);
			}
			else
			{
				if ( (dynDebugState.uiNumAllocatedFastParticlesEntities + uiNewSize - pParams->pSet->uiNumAllocated) > MAX_ALLOCATED_FAST_PARTICLES_ENTITY )
					bGrow = false;
				else
					dynDebugState.uiNumAllocatedFastParticlesEntities += (uiNewSize - pParams->pSet->uiNumAllocated);
			}

			if (bGrow)
			{
				// Grow the pool to uiNewSize
				poolQueueGrow(&pParams->pSet->particleQueue, uiNewSize);

				pParams->pSet->uiNumAllocated = uiNewSize;
			}
			else
			{
				poolQueueDequeue(&pParams->pSet->particleQueue, NULL);
			}
		}

		assert(!poolQueueIsFull(&pParams->pSet->particleQueue));
	}

	pNew = poolQueuePreEnqueue(&pParams->pSet->particleQueue);
	pNew->fTime = pParams->pSet->fLastEmissionTime;
	pNew->fSeed = randomPositiveF32();
	pNew->bInvisible = 0;
	pNew->bFirstInLine = pNew->bLastInLine = 0;
	pNew->fTimeSinceAccUpdate = pNew->fTimeSinceVelUpdate = 0.0f;
	pNew->uiNodeIndex = uiNodeIndex;
	pNew->uiMovementSeed = randomU32Seeded(NULL, RandType_BLORN);
	pNew->fMass = pParams->pSet->fParticleMass;

	if (pParams->pSet->ePosFlag != DynParticleEmitFlag_Inherit)
		zeroVec3(pNew->vPos);
	else if (!pAtNode)
		copyVec3(pParams->vPos, pNew->vPos);
	else
		dynNodeGetWorldSpacePos(pAtNode, pNew->vPos);

	if (pParams->pSet->eRotFlag != DynParticleEmitFlag_Inherit)
		unitQuat(pNew->qRot);
	else if (!pAtNode)
		copyQuat(pParams->qRot, pNew->qRot);
	else
		dynNodeGetWorldSpaceRot(pAtNode, pNew->qRot);

	if (pParams->pSet->eScaleFlag != DynParticleEmitFlag_Inherit)
		copyVec3(pParams->vScale, pNew->vScale);
	else if (!pAtNode)
		unitVec3(pNew->vScale);
	else
		dynNodeGetWorldSpaceScale(pAtNode, pNew->vScale);

	return pNew;
}

void dynFxFastParticleSetEmitWithAtNode( DynFPBurstParams* pParams, U32 uiNodeIndex, const DynNode* pAtNode, Vec3 vInheritVelocity, DynFx* pParentFx, F32 fUniformValue, bool bLineEmission, F32 fUniformLine, const Vec3 vLineOffset)
{
	DynFxFastParticle* pNew;
	U32 uiSeed;
	pNew = dynFxFastParticleCreate(pParams, pAtNode, uiNodeIndex);
	uiSeed = pNew->uiMovementSeed;
#ifdef USE_VECTOR_OPTIMIZATIONS
	dynFxFastParticleInitParticle(pNew, pParams->pVInfo, uiSeed, vInheritVelocity, bLineEmission, vLineOffset, fUniformValue, fUniformLine, pParams->pSet->fSetTime - pParams->pSet->fLastEmissionTime);
#else
	dynFxFastParticleInitParticle(pNew, pParams->pInfo, pParams->pSet, uiSeed, vInheritVelocity, pParams->vMagnetPos, bLineEmission, vLineOffset, fUniformValue, fUniformLine, pParams->pSet->fSetTime - pParams->pSet->fLastEmissionTime);
#endif
}

bool dynFxFastParticleCalcEmitTargetOffset( DynFPBurstParams* pParams, DynFx* pParentFx, const DynNode* pAtNode, U32 uiNodeIndex, Vec3 vLineOffset) 
{
	if (pParams->pSet->pEmitTargetNodes)
	{
		Vec3 vAtPos;
		Vec3 vEmitPos;
		const DynNode* pEmitTargetNode = dynFxFastParticleSetGetEmitTargetNode(pParams->pSet, uiNodeIndex);
		if (!pAtNode || !pEmitTargetNode)
			return false;
		dynNodeGetWorldSpacePos(pEmitTargetNode, vEmitPos);
		dynNodeGetWorldSpacePos(pAtNode, vAtPos);
		if (pParams->pSet->eRotFlag == DynParticleEmitFlag_Update)
		{
			Vec3 vTemp;
			Quat qTempRot, qInv;
			dynNodeGetWorldSpaceRot(pAtNode, qTempRot);
			quatInverse(qTempRot, qInv);
			subVec3(vEmitPos, vAtPos, vTemp);
			quatRotateVec3(qInv, vTemp, vLineOffset);
		}
		else
			subVec3(vEmitPos, vAtPos, vLineOffset);
		return true;
	}
	return false;
}


static void dynFxFastParticleSetEmitBurst( DynFPBurstParams* pParams )
{
	Vec3 vInheritVelocity;
	Vec3 vTmpSticky;
	scaleVec3(pParams->vVelocity, pParams->pInfo->fInheritVelocity, vInheritVelocity);

	// Remove the stickiness value because it'll interfere with some jumpstarting.
	copyVec3(pParams->pSet->vStickiness, vTmpSticky);
	zeroVec3(pParams->pSet->vStickiness);

	if (pParams->pSet->uiNumAtNodes > 0)
	{
		DynFx* pParentFx = GET_REF(pParams->pSet->hParentFX);
		if (pParentFx)
		{
			if (pParams->pSet->bApplyCountEvenly)
			{
				U32 uiNodeIndex;
				for (uiNodeIndex=0; uiNodeIndex<pParams->pSet->uiNumAtNodes; ++uiNodeIndex)
				{
					U32 i;
					U32 uiEmitCount;
					S32 iEmitCount = round(pParams->pInfo->uiEmissionCountJitter * randomF32Seeded(NULL, RandType_BLORN));
					const DynNode* pAtNode = dynFxFastParticleSetGetAtNode(pParams->pSet, uiNodeIndex);

					// Calculate the emit target offset
					Vec3 vLineOffset;
					bool bLineOffset = dynFxFastParticleCalcEmitTargetOffset(pParams, pParentFx, pAtNode, uiNodeIndex, vLineOffset);
					// If it's count by distance, calculate the length and use the count-per-foot
					if (bLineOffset && pParams->pInfo->bCountByDistance)
					{
						F32 fLength = lengthVec3(vLineOffset);
						fLength = MIN(fLength, 10.0f);
						iEmitCount += round(pParams->pInfo->fCountPerFoot * fLength);

						if(!iEmitCount) {
							iEmitCount += !!((fLength * pParams->pInfo->fCountPerFoot * 128) > (rand() % 128));
						}
					}
					else // otherwise, use the static emission count
					{
						iEmitCount += pParams->pInfo->uiEmissionCount;

						// If there are weights, multiply the weights by the count
						if (pParams->pSet->pfWeights)
							iEmitCount = round((F32)iEmitCount * pParams->pSet->pfWeights[uiNodeIndex]);
						else
							iEmitCount = round((F32)iEmitCount / (F32)pParams->pSet->uiNumAtNodes);
					}

					uiEmitCount = MAX(iEmitCount, 0);

					if (uiEmitCount > MAX_PARTICLES_PER_SET)
					{
						Errorf("Fast particle emission of %d particles exceeds allowed maximum of %d!", uiEmitCount, MAX_PARTICLES_PER_SET);
						uiEmitCount = MAX_PARTICLES_PER_SET;
					}

					for (i=0; i<uiEmitCount; ++i)
					{
						F32 fUniformValue = -1.0f;
						F32 fUniformLine = -1.0f;
						if (pParams->pInfo->bUniformJitter)
						{
							if (uiEmitCount > 1)
								fUniformValue = (F32)(i) / (F32)(uiEmitCount-1);
							else
								fUniformValue = 0.5f;
						}
						if (pParams->pInfo->bUniformLine)
						{
							if (uiEmitCount > 1)
								fUniformLine = (F32)(i) / (F32)(uiEmitCount-1);
							else
								fUniformLine = 0.5f;
						}
						dynFxFastParticleSetEmitWithAtNode(pParams, uiNodeIndex, pAtNode, vInheritVelocity, pParentFx, fUniformValue, bLineOffset, fUniformLine, vLineOffset);
					}
				}
			}
			else
			{
				U32 i;
				U32 uiEmitCount = pParams->pInfo->uiEmissionCount + round(pParams->pInfo->uiEmissionCountJitter * randomF32Seeded(NULL, RandType_BLORN));
				for (i=0; i<uiEmitCount; ++i)
				{
					U32 uiNodeIndex = pParams->pSet->pfWeights?randomWeightedArrayIndex(pParams->pSet->pfWeights, pParams->pSet->uiNumAtNodes): randomIntRange(0, pParams->pSet->uiNumAtNodes-1);
					const DynNode* pAtNode = dynFxFastParticleSetGetAtNode(pParams->pSet, uiNodeIndex);
					F32 fUniformValue = -1.0f;
					Vec3 vLineOffset;
					bool bLineOffset = dynFxFastParticleCalcEmitTargetOffset(pParams, pParentFx, pAtNode, uiNodeIndex, vLineOffset);
					if (pParams->pInfo->bUniformJitter)
					{
						if (uiEmitCount > 1)
							fUniformValue = (F32)(i) / (F32)(uiEmitCount-1);
						else
							fUniformValue = 0.5f;
					}
					dynFxFastParticleSetEmitWithAtNode(pParams, uiNodeIndex, pAtNode, vInheritVelocity, pParentFx, fUniformValue, bLineOffset, -1.0f, vLineOffset);
				}
			}
		}
	}
	else
	{
		U32 i;
		U32 uiEmitCount;
		if (pParams->bLineEmission && pParams->pInfo->bCountByDistance) {
			uiEmitCount = round(pParams->pInfo->fCountPerFoot * lengthVec3(pParams->vLineOffset));
		} else if(pParams->pInfo->bCountByDistance && pParams->pSet->bApplyCountEvenly && pParams->pSet->uiNumTriangles) {
			
			F32 fSurfaceScale = (
				pParams->pSet->vScale[0] +
				pParams->pSet->vScale[1] +
				pParams->pSet->vScale[2]) / 3.0;

			fSurfaceScale *= fSurfaceScale;

			uiEmitCount =
				0.01 *
				fSurfaceScale *
				pParams->pSet->fModelTotalArea *
				pParams->pInfo->fCountPerFoot;

			if(!uiEmitCount) {
				uiEmitCount += !!((fSurfaceScale * pParams->pSet->fModelTotalArea * pParams->pInfo->fCountPerFoot * 128) > (rand() % 128));
			}

		} else {
			uiEmitCount = pParams->pInfo->uiEmissionCount + round(pParams->pInfo->uiEmissionCountJitter * randomF32Seeded(NULL, RandType_BLORN));
		}

		if(pParams->pSet->bUseModel && !pParams->pSet->pcModelPattern) {
			// Don't have a model yet!
			uiEmitCount = 0;
		}

		if(uiEmitCount > MAX_PARTICLES_PER_SET) {
			uiEmitCount = MAX_PARTICLES_PER_SET;
		}

		for (i=0; i<uiEmitCount; ++i)
		{
			DynFxFastParticle* pNew = dynFxFastParticleCreate(pParams, NULL, 0);
			F32 fUniformValue = -1.0f;
			F32 fUniformLine = -1.0f;
			U32 uiSeed = pNew->uiMovementSeed;
			if (pParams->pInfo->bUniformJitter)
			{
				if (uiEmitCount > 1)
					fUniformValue = (F32)(i) / (F32)(uiEmitCount-1);
				else
					fUniformValue = 0.5f;
			}
			if (pParams->pInfo->bUniformLine)
			{
				if (uiEmitCount > 1)
					fUniformLine = (F32)(i) / (F32)(uiEmitCount-1);
				else
					fUniformLine = 0.5f;
			}
#ifdef USE_VECTOR_OPTIMIZATIONS
			dynFxFastParticleInitParticle(pNew, pParams->pVInfo, uiSeed, vInheritVelocity, pParams->bLineEmission, pParams->vLineOffset, fUniformValue, fUniformLine, pParams->pSet->fSetTime - pParams->pSet->fLastEmissionTime);
#else
			dynFxFastParticleInitParticle(pNew, pParams->pInfo, pParams->pSet, uiSeed, vInheritVelocity, pParams->vMagnetPos, pParams->bLineEmission, pParams->vLineOffset, fUniformValue, fUniformLine, pParams->pSet->fSetTime - pParams->pSet->fLastEmissionTime);
#endif
		}
	}

	dynFxFastParticleSetCalcCurrentEmissionRate(pParams->pSet, pParams->pInfo);
	pParams->pSet->bEmitted = true;

	// Restore the stickiness value.
	copyVec3(vTmpSticky, pParams->pSet->vStickiness);
}

void dynFxFastParticleFakeVelocity(DynFxFastParticleSet* pSet, const Vec3 vFakeVelocity, F32 fDeltaTime)
{
	Vec3 vOffset;
	scaleVec3(vFakeVelocity, -fDeltaTime, vOffset);
	{
		PoolQueueIterator iter;
		DynFxFastParticle* pParticle;
		poolQueueGetIterator(&pSet->particleQueue, &iter);
		while (poolQueueGetNextElement(&iter, &pParticle))
		{
			addVec3(pParticle->vPos, vOffset, pParticle->vPos);
		}
	}
}

void dynFxFastParticleFakeVelocityRecalculate(DynFxFastParticleSet* pSet, const Vec3 vFakeVelocity)
{
	{
		PoolQueueIterator iter;
		DynFxFastParticle* pParticle;
		poolQueueGetIterator(&pSet->particleQueue, &iter);
		while (poolQueueGetNextElement(&iter, &pParticle))
		{
			Vec3 vOffset;
			F32 fDeltaTime = pSet->fSetTime - pParticle->fTime;
			pParticle->bInvisible = 0;
			scaleVec3(vFakeVelocity, -fDeltaTime, vOffset);
			addVec3(pParticle->vPos, vOffset, pParticle->vPos);
		}
	}

}

static void makeAxisShiftMatrix(
	Vec3 oldStart, Vec3 oldEnd,
	Vec3 newStart, Vec3 newEnd,
	Mat4 out,
	bool normalizeAxis,
	bool normalizeOtherAxes) {

	Mat4 offset;

	Mat4 coordSpaceFrom;
	Mat4 coordSpaceFrom2;
	Mat4 coordSpaceFromOld;
	Mat4 coordSpaceToOld;
	Mat4 coordSpaceToOld2;

	// Bail out early if there's nothing to do.
	if(	!cmpVec3XYZ(newStart, newEnd) ||
		!cmpVec3XYZ(oldStart, oldEnd)) {

		copyMat4(unitmat, out);
		return;
	}


	// Build the transformation going from the coordinate space where the line defines one
	// of the axes back to the world coordinate space.

	// Up axis.
	setVec3(coordSpaceFrom[1], 0.0f, 1.0f, 0.0f);

	// The line axis we're doing everthing
	// around.
	subVec3(newEnd, newStart, coordSpaceFrom[0]);
	if(normalizeAxis) {
		normalVec3(coordSpaceFrom[0]);
	}

	// Third axis is just perpendicular to those.
	crossVec3(
		coordSpaceFrom[1],
		coordSpaceFrom[0],
		coordSpaceFrom[2]);

	if(normalizeOtherAxes) {
		normalVec3(coordSpaceFrom[1]);
		normalVec3(coordSpaceFrom[2]);
	}

	zeroVec3(coordSpaceFrom[3]);

	// Offset from the start position being the origin to the world origin.
	copyMat4(unitmat, offset);
	copyVec3(newStart, offset[3]);
	mulMat4Inline(offset, coordSpaceFrom, coordSpaceFrom2);


	// Build the transformation from world coordinate space to the coordinate space where
	// the line defines one of the axes. Use the old version of the line to do this. Same
	// as above, but inverts it.

	// Up axis.
	setVec3(coordSpaceFromOld[1], 0.0f, 1.0f, 0.0f);
	copyMat4(unitmat, coordSpaceFromOld);

	// Line axis.
	subVec3(oldEnd, oldStart, coordSpaceFromOld[0]);
	if(normalizeAxis) {
		normalVec3(coordSpaceFromOld[0]);
	}

	// Arbitrary perpendicular axis.
	crossVec3(
		coordSpaceFromOld[1],
		coordSpaceFromOld[0],
		coordSpaceFromOld[2]);

	if(normalizeOtherAxes) {
		normalVec3(coordSpaceFromOld[1]);
		normalVec3(coordSpaceFromOld[2]);
	}

	invertMat4(coordSpaceFromOld, coordSpaceToOld);

	// Offset from the world origin to the start origin.
	copyMat4(unitmat, offset);
	scaleVec3(oldStart, -1.0f, offset[3]);
	mulMat4Inline(coordSpaceToOld, offset, coordSpaceToOld2);


	// Combine the two sections into a single transformation.
	mulMat4Inline(coordSpaceFrom2, coordSpaceToOld2, out);

}

void dynFxFastParticleSetUpdate(DynFxFastParticleSet* pSet, const Vec3 vVelocity, F32 fDeltaTime, bool bTestVelocity, bool bIsVisible)
{
	bool bPruningDone = false;
	DynFxFastParticleInfo* pInfo = GET_REF(pSet->hInfo);
	DynNode *pLocation;
	DynNode *pTransformTarget = NULL;
	DynNode loc;
	Vec3 vOldPos, vPosChange;
	Vec3 vMagnetPos;
	Vec3 vLineOffset;
	bool bLineEmission;
	bool bMoved = false;
	Mat4 shiftMatrix;
#ifdef USE_VECTOR_OPTIMIZATIONS
	FastParticleInfoVector vInfo;
#endif
	PERFINFO_AUTO_START_FUNC();

	pSet->fSetTime += fDeltaTime * dynDebugState.fDynFxRate;

	if (bIsVisible || dynDebugState.bFastParticleForceUpdate)
	{
		if (!pSet->particleQueue.pStorage)
		{
			// Attempt to create a queue. If successful we continue, otherwise we abort update
			if (!dynFxFastParticleSetInitQueue(pSet))
			{
				PERFINFO_AUTO_STOP_FUNC();
				return;
			}

			if (pSet->bJumpStart)
				pSet->fLastEmissionTime = pSet->fSetTime - pInfo->fLifeSpan;
			else
				pSet->fLastEmissionTime = pSet->fSetTime;
		}
	}
	else
	{
		if (pSet->particleQueue.pStorage)
		{
			pSet->bJumpStart = true;
			dynFxFastParticleSetDeinitQueue(pSet);
		}

		PERFINFO_AUTO_STOP_FUNC();
		return;
	}
	assert(pSet->particleQueue.pStorage);


	assert(pInfo);

	pLocation = GET_REF(pSet->hLocation);
	if (!pLocation)
	{
		dynNodeReset(&loc);
		dynNodeSetPos(&loc, pSet->vPos);
		dynNodeSetRot(&loc, pSet->qRot);
		dynNodeSetScale(&loc, pSet->vScale);
		pLocation = &loc;
	}

	dynFxFastParticleSetGetMagnetPos(pSet, vMagnetPos);
	bLineEmission = dynFxFastParticleSetGetLineOffset(pSet, vLineOffset);

	if (pSet->ePosFlag == DynParticleEmitFlag_Inherit)
	{
		copyVec3(pSet->vPos, vOldPos);
		dynNodeGetWorldSpacePos(pLocation, pSet->vPos);
		subVec3(pSet->vPos, vOldPos, vPosChange);
		bMoved = !vec3IsZero(vPosChange);
		if (pInfo->fStickiness > 0.0f)
		{
			scaleVec3(vPosChange, pInfo->fStickiness, pSet->vStickiness);
		}
	}
	else if (pSet->ePosFlag == DynParticleEmitFlag_Update)
	{
		dynNodeGetWorldSpacePos(pLocation, pSet->vPos);
	}
	else if (!pSet->bEmitted)
	{
		// update pos once if position is set to ignore
		dynNodeGetWorldSpacePos(pLocation, pSet->vPos);
	}

	if (bTestVelocity)
	{
		scaleVec3(vVelocity, fDeltaTime, vPosChange);
		bMoved = !vec3IsZero(vPosChange);
		if (pInfo->fStickiness > 0.0f)
		{
			scaleVec3(vPosChange, pInfo->fStickiness, pSet->vStickiness);
		}
	}

	dynNodeGetWorldSpaceRot(pLocation, pSet->qRot);

	if (pSet->eScaleFlag == DynParticleEmitFlag_Update)
		dynNodeGetWorldSpaceScale(pLocation, pSet->vScale);

	// Build a matrix for the transform target stuff, if we need to.
	pTransformTarget = GET_REF(pSet->hTransformTarget);
	if(pTransformTarget) {

		Vec3 vNewLocation;
		Vec3 vNewTargetLocation;

		dynNodeGetWorldSpacePos(pLocation, vNewLocation);
		dynNodeGetWorldSpacePos(pTransformTarget, vNewTargetLocation);

		if(pSet->ePosFlag == DynParticleEmitFlag_Update) {
			subVec3(vNewTargetLocation, vNewLocation, vNewTargetLocation);
			zeroVec3(vNewLocation);
		} else {
			copyVec3(vNewLocation, pSet->vLastLocation);
		}

		makeAxisShiftMatrix(
			pSet->vLastLocation, pSet->vLastEmitTargetLocation,
			vNewLocation, vNewTargetLocation, shiftMatrix,
			pSet->bNormalizeTransformTarget,
			pSet->bNormalizeTransformTargetOtherAxes);

		copyVec3(vNewLocation, pSet->vLastLocation);
		copyVec3(vNewTargetLocation, pSet->vLastEmitTargetLocation);

	} else {
		copyMat4(unitmat, shiftMatrix);
	}

	while (!bPruningDone)
	{
		DynFxFastParticle* pOldestPart = NULL;
		bPruningDone = true;
		if (poolQueuePeek(&pSet->particleQueue, &pOldestPart))
		{
			//PREFETCH(pOldestPart + sizeof(DynFxFastParticle) * 2); // prefetch a couple forward
			if (pOldestPart->bInvisible || pSet->fSetTime - pOldestPart->fTime > pInfo->fLifeSpan)
			{
				poolQueueDequeue(&pSet->particleQueue, NULL);
				bPruningDone = false;
			}
		}
	}
	{
#ifdef USE_VECTOR_OPTIMIZATIONS
		InitFastParticleInfoVector(pSet, pInfo, vMagnetPos, &vInfo );
#endif
		
		PoolQueueIterator iter;
		DynFxFastParticle* pParticle, *pParticleNext;
		poolQueueGetIterator(&pSet->particleQueue, &iter);
		if (!poolQueueGetNextElement(&iter, &pParticle))
			pParticle = NULL;
		while (pParticle)
		{
			if (!poolQueueGetNextElement(&iter, &pParticleNext))
				pParticleNext = NULL;

			if (pParticleNext)
			{
				PREFETCH(pParticleNext);
			}

			// Handle transform target stuff before anything else.
			if(pTransformTarget) {
				Vec3 tmp;
				mulVecMat4(pParticle->vPos, shiftMatrix, tmp);
				copyVec3(tmp, pParticle->vPos);
			}

#ifdef USE_VECTOR_OPTIMIZATIONS
			dynFxFastParticleUpdateVector(pParticle, &vInfo, fDeltaTime * dynDebugState.fDynFxRate);
#else
			dynFxFastParticleUpdate(pParticle, fDeltaTime*dynDebugState.fDynFxRate, pInfo, pSet, vMagnetPos);
#endif
			
			pParticle = pParticleNext;
		}
	}

	if (!pSet->bStopEmitting || !pSet->bEmitted)
	{
		F32 fEmissionRate = pInfo->fEmissionRate;
		DynFPBurstParams params = {0};
		params.pInfo = pInfo;
		params.pSet = pSet;
		params.vPos = pSet->vPos;
		params.qRot = pSet->qRot;
		params.vScale = pSet->vScale;
		params.vVelocity = vVelocity;
		params.vMagnetPos = vMagnetPos;
		params.bLineEmission = bLineEmission;
		params.vLineOffset = vLineOffset;
#ifdef USE_VECTOR_OPTIMIZATIONS
		params.pVInfo = &vInfo;
#endif

		// Convert emission distance into emission rate
		pSet->fCurrentDistanceEmissionRate = 0.0f;
		if (pInfo->fEmissionRatePerFoot > 0.0f && bMoved && fDeltaTime > 0.0001f)
		{
			F32 fDistanceTravelled = lengthVec3(vPosChange);
			F32 fCappedDistance = MIN(fDistanceTravelled, 10.0f);
			F32 fCappedSpeed = fCappedDistance / fDeltaTime;
			F32 fDistanceRate = pInfo->fEmissionRatePerFoot * fCappedSpeed;
			if (fDistanceRate > fEmissionRate && fCappedSpeed > pInfo->fMinEmissionSpeed)
			{
				fEmissionRate = fDistanceRate;
				pSet->fCurrentDistanceEmissionRate = fDistanceRate;
			}
		}

		dynFxFastParticleSetCalcCurrentEmissionRate(pSet, pInfo);

		if (fEmissionRate > 0.0f)
		{
			Vec3 vLastPos, vDeltaPos;
			bool bInterpPos = false;
			int iEmitCount = 0;
			F32 fTimeToPass = pSet->fSetTime - pSet->fLastEmissionTime;
			if (bMoved)
			{
				int iNumTimes = qtrunc((pSet->fSetTime - pSet->fLastEmissionTime) * fEmissionRate);
				if (iNumTimes > 1)
				{
					F32 fMultiple = 1.0f / (F32)iNumTimes;
					bInterpPos = true;
					scaleVec3(vPosChange, fMultiple, vDeltaPos);
					copyVec3(vOldPos, vLastPos);
				}
			}
			while (pSet->fSetTime - pSet->fLastEmissionTime > pSet->fCurrentEmissionRateInv)
			{
				if (++iEmitCount > MAX_PARTICLES_PER_SET)
				{
					//Errorf("Somehow emitting too many particles (%d) in fast particle system %s. Set time is %.1f. Resetting particle set.", iEmitCount, pInfo->pcName, pSet->fSetTime);
					dynFxFastParticleSetReset(pSet);
					break;
				}
				pSet->fLastEmissionTime += pSet->fCurrentEmissionRateInv;
				if (bInterpPos)
				{
					addVec3(vDeltaPos, vLastPos, vLastPos);
					params.vPos = vLastPos;
					dynFxFastParticleSetEmitBurst(&params);
				}
				else
				{
					params.vPos = pSet->vPos;
					dynFxFastParticleSetEmitBurst(&params);
				}
			}
		}
		else
			pSet->fLastEmissionTime = pSet->fSetTime;

		if (!pSet->bEmitted)
			dynFxFastParticleSetEmitBurst(&params);
	}
	dynDebugState.uiNumFastParticles += poolQueueGetNumElements(&pSet->particleQueue);
	PERFINFO_AUTO_STOP_FUNC();
}

bool dynFxFastParticleUseConstantScreenSize(DynFxFastParticleSet* pSet) {
	if(pSet) {
		DynFx *pFx = GET_REF(pSet->hParentFX);
		if(pFx) {
			return pFx->pParticle->pDraw->bFixedAspectRatio;
		}
	}
	return false;
}

#include "dynFxFastParticle_h_ast.c"
#include "AutoGen/dynFxFastParticle_c_ast.c"

