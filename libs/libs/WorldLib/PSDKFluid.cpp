#define NO_MEMCHECK_OK // to allow including stdtypes.h in headers

#define MEM_DBG_PARMS , const char *caller_fname, int line
#define MEM_DBG_PARMS_VOID const char *caller_fname, int line
#define MEM_DBG_PARMS_CALL , caller_fname, line
#define MEM_DBG_PARMS_CALL_VOID caller_fname, line
#define MEM_DBG_PARMS_INIT , __FILE__, __LINE__
#define MEM_DBG_PARMS_INIT_VOID __FILE__, __LINE__
#define MEM_DBG_STRUCT_PARMS const char *caller_fname; int line;
#define MEM_DBG_STRUCT_PARMS_INIT(struct_ptr) ((struct_ptr)->caller_fname = caller_fname, (struct_ptr)->line = line)
#define MEM_DBG_STRUCT_PARMS_CALL(struct_ptr) , (struct_ptr)->caller_fname, (struct_ptr)->line
#define MEM_DBG_STRUCT_PARMS_CALL_VOID(struct_ptr) (struct_ptr)->caller_fname, (struct_ptr)->line

#include "PhysicsSDKPrivate.h"

AUTO_RUN_ANON(memBudgetAddMapping(__FILE__, BUDGET_Physics););

#if !PSDK_DISABLED

#include "PhysXLoader.h"
#include "NxPhysics.h"
#include "NxPhysicsSDK.h"
#include "NxScene.h"

#include "earray.h"
#include "mathutil.h"

S32 psdkFluidDescCreate(PSDKFluidDesc **fluidDescOut)
{
	PSDKFluidDesc *fluidDesc;

	*fluidDescOut = fluidDesc = callocStruct(PSDKFluidDesc);

	if(!fluidDesc)
	{
		return 0;
	}

	fluidDesc->collisionDistanceMultiplier = 1;
	fluidDesc->kernelRadiusMultiplier = 1;
	fluidDesc->maxParticles = 1000;
	fluidDesc->motionLimitMultiplier = 3;
	fluidDesc->restDensity = 100;

	fluidDesc->disableGravity = true;

	return 1;
}

S32	psdkFluidEmitterDescCreate(PSDKFluidEmitterDesc **descOut)
{
	PSDKFluidEmitterDesc *fluidEmitterDesc;

	if(!descOut)
	{
		return 0;
	}

	*descOut = fluidEmitterDesc = callocStruct(PSDKFluidEmitterDesc);

	if(!fluidEmitterDesc)
	{
		return 0;
	}

	fluidEmitterDesc->maxParticles = 0;

	return 1;
}

S32	psdkFluidEmitterDescDestroy(PSDKFluidEmitterDesc **descInOut)
{
	if(!descInOut || 
		!*descInOut)
	{
		return 0;
	}

	SAFE_FREE(descInOut);

	return 1;
}

S32 psdkFluidCreate(PSDKFluid **fluidOut,
					PSDKSimulationOwnership *so,
					PSDKScene *scene,
					PSDKFluidDesc *fluidDesc)
{
	PSDKFluid *fluid;
	NxParticleData nxParticleData;
	NxFluidDesc nxFluidDesc;
	NxFluidEmitterDesc nxFluidEmitterDesc;

	if(	!fluidOut ||
		!fluidDesc ||
		!scene ||
		!psdkCanModify(so))
	{
		return 0;
	}

	*fluidOut = fluid = callocStruct(PSDKFluid);

	if(!fluid)
	{
		return 0;
	}

	if(fluidDesc->particleCount)
	{
		*fluidDesc->particleCount = 0;
		nxParticleData.numParticlesPtr = fluidDesc->particleCount;
		nxParticleData.bufferPos = &fluidDesc->particleData[0].pos[0];
		nxParticleData.bufferPosByteStride = sizeof(PSDKFluidParticle);
		nxParticleData.bufferVel = &fluidDesc->particleData[0].vel[0];
		nxParticleData.bufferVelByteStride = sizeof(PSDKFluidParticle);
		/*
		nxParticleData.bufferDensity = &mParticleBuffer[0].density;
		nxParticleData.bufferDensityByteStride = sizeof(ParticleSDK);
		nxParticleData.bufferLife = &mParticleBuffer[0].lifetime;
		nxParticleData.bufferLifeByteStride = sizeof(ParticleSDK);
		nxParticleData.bufferId = &mParticleBuffer[0].id;
		nxParticleData.bufferIdByteStride = sizeof(ParticleSDK);
		nxParticleData.bufferDensity = &mParticleBuffer[0].density;
		nxParticleData.bufferDensityByteStride = sizeof(ParticleSDK);
		*/

		nxFluidDesc.particlesWriteData = nxParticleData;
	}

	FOR_EACH_IN_EARRAY(fluidDesc->emitters, PSDKFluidEmitterDesc, emitter)
	{
		nxFluidEmitterDesc.type = NX_FE_CONSTANT_FLOW_RATE;
		nxFluidEmitterDesc.randomAngle = HALFPI;
		nxFluidEmitterDesc.flags = NX_FEF_ENABLED;
		nxFluidEmitterDesc.maxParticles = emitter->maxParticles;
		nxFluidEmitterDesc.randomPos = NxVec3(4.0f,4.0f,4.0f);
		nxFluidEmitterDesc.dimensionX = 0.3f;
		nxFluidEmitterDesc.dimensionY = 0.3f;
		nxFluidEmitterDesc.rate = emitter->rate;
		nxFluidEmitterDesc.fluidVelocityMagnitude = 1.0f;
		nxFluidEmitterDesc.particleLifetime = 0.0f;
		nxFluidEmitterDesc.shape = NX_FE_RECTANGULAR;
		setNxMat34FromMat4(nxFluidEmitterDesc.relPose, unitmat);
		setNxVec3FromVec3(nxFluidEmitterDesc.relPose.t, emitter->center_pos);

		nxFluidDesc.emitters.pushBack(nxFluidEmitterDesc);
	}
	FOR_EACH_END

	nxFluidDesc.maxParticles					= fluidDesc->maxParticles;
	nxFluidDesc.fadeInTime						= fluidDesc->fadeInTime;
	nxFluidDesc.kernelRadiusMultiplier			= fluidDesc->kernelRadiusMultiplier;
	nxFluidDesc.restParticlesPerMeter			= fluidDesc->restParticlesPerMeter;
	nxFluidDesc.motionLimitMultiplier			= fluidDesc->motionLimitMultiplier;
	nxFluidDesc.packetSizeMultiplier			= 32;
	nxFluidDesc.collisionDistanceMultiplier		= fluidDesc->collisionDistanceMultiplier;
	nxFluidDesc.stiffness						= fluidDesc->stiffness;
	nxFluidDesc.viscosity						= fluidDesc->viscosity;
	nxFluidDesc.restDensity						= fluidDesc->restDensity;
	nxFluidDesc.damping							= fluidDesc->damping;
	nxFluidDesc.restitutionForStaticShapes		= fluidDesc->restitutionForStaticShapes;
	nxFluidDesc.dynamicFrictionForStaticShapes	= fluidDesc->dynamicFrictionForStaticShapes;
	nxFluidDesc.surfaceTension					= fluidDesc->surfaceTension;
	nxFluidDesc.simulationMethod				= NX_F_SPH;
	nxFluidDesc.flags = NX_FF_ENABLED | 
						(fluidDesc->disableGravity ? NX_FF_DISABLE_GRAVITY : 0) |
						(!psdkState.flags.noHardwareSupport ? NX_FF_HARDWARE : 0);

	fluid->nxFluid = scene->nxScene->createFluid(nxFluidDesc);

	if(!fluid->nxFluid)
	{
		free(fluid);
		*fluidOut = NULL;

		return 0;
	}

	fluid->nxFluid->userData = fluid;

	return 1;
}

S32	psdkFluidDestroy(	PSDKFluid **fluidInOut,
						PSDKSimulationOwnership *so)
{
	PSDKFluid *fluid = SAFE_DEREF(fluidInOut);
	
	if(	!fluid)
	{
		return 0;
	}

	*fluidInOut = NULL;

	if(!psdkCanModify(so))
	{
		return 0;
	}

	if(fluid->nxFluid)
	{
		NxScene& scene = fluid->nxFluid->getScene();

		fluid->nxFluid->userData = NULL;
		scene.releaseFluid(*fluid->nxFluid);
	}

	SAFE_FREE(fluid);
	*fluidInOut = NULL;

	return 1;
}

S32 psdkFluidSetMaxParticles(	PSDKFluid *fluid,
								PSDKSimulationOwnership *so, 
								U32 maxParticles)
{
	if(!fluid || 
		!fluid->nxFluid)
	{
		return 0;
	}

	if(!psdkCanModify(so))
	{
		return 0;
	}

	fluid->nxFluid->setCurrentParticleLimit(maxParticles);

	return 1;
}

S32	psdkFluidSetDamping(PSDKFluid *fluid,
						PSDKSimulationOwnership *so, 
						F32 damping)
{
	if(!fluid || 
		!fluid->nxFluid)
	{
		return 0;
	}

	if(!psdkCanModify(so))
	{
		return 0;
	}

	fluid->nxFluid->setDamping(damping);

	return 1;
}

S32	psdkFluidSetStiffness(PSDKFluid *fluid,
						  PSDKSimulationOwnership *so, 
						  F32 stiffness)
{
	if(!fluid || 
		!fluid->nxFluid)
	{
		return 0;
	}

	if(!psdkCanModify(so))
	{
		return 0;
	}

	fluid->nxFluid->setStiffness(stiffness);
	
	return 1;
}

S32	psdkFluidSetViscosity(PSDKFluid *fluid,
						  PSDKSimulationOwnership *so, 
						  F32 viscosity)
{
	if(!fluid || 
		!fluid->nxFluid)
	{
		return 0;
	}

	if(!psdkCanModify(so))
	{
		return 0;
	}

	fluid->nxFluid->setViscosity(viscosity);

	return 1;
}

#endif // !PSDK_DISABLED
