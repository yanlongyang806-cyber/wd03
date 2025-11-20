#include "WorldColl.h"
#include "WorldCollPrivate.h"

AUTO_RUN_ANON(memBudgetAddMapping(__FILE__, BUDGET_Physics););

S32	wcFluidCreate(	const WorldCollIntegrationMsg* msg,
					WorldCollScene *scene,
					WorldCollFluid **wcFluidOut,
					void* userPointer,
					PSDKFluidDesc *fluidDesc)
{
#if PSDK_DISABLED
	return 0;
#else 
	WorldCollFluid *fluid;

	if(	msg->msgType != WCI_MSG_NOBG_WHILE_SIM_SLEEPS &&
		msg->msgType != WCI_MSG_BG_BETWEEN_SIM
		||
		!scene)
	{
		return 0;
	}

	fluid = callocStruct(WorldCollFluid);

	if(!fluid)
	{
		return 0;
	}

	fluid->userPointer = userPointer;
	fluid->scene = scene;

	if(!psdkFluidCreate(&fluid->psdkFluid,
						wcgState.psdkSimulationOwnership,
						scene->psdkScene, 
						fluidDesc))
	{
		return 0;
	}

	eaPush(&scene->fluids, fluid);
	*wcFluidOut = fluid;

	return 1;
#endif
}

S32	wcFluidDestroy(	const WorldCollIntegrationMsg* msg,
					WorldCollFluid **wcFluidInOut)
{
#if PSDK_DISABLED
	return 0;
#else
	WorldCollFluid* fluid = SAFE_DEREF(wcFluidInOut);

	if(	msg->msgType != WCI_MSG_NOBG_WHILE_SIM_SLEEPS &&
		msg->msgType != WCI_MSG_BG_BETWEEN_SIM
		||
		!fluid)
	{
		return 0;
	}

	if(eaFindAndRemove(&fluid->scene->fluids, fluid) < 0){
		assert(0);
	}

	psdkFluidDestroy(	&fluid->psdkFluid,
						wcgState.psdkSimulationOwnership);

	SAFE_FREE(fluid);
	*wcFluidInOut = NULL;

	return 1;
#endif
}

S32 wcFluidSetMaxParticles(const WorldCollIntegrationMsg* msg,
						   WorldCollFluid* wcFluid,
						   U32 maxParticles)
{
#if PSDK_DISABLED
	return 0;
#else
	if(	msg->msgType != WCI_MSG_NOBG_WHILE_SIM_SLEEPS &&
		msg->msgType != WCI_MSG_BG_BETWEEN_SIM
		||
		!wcFluid)
	{
		return 0;
	}

	return psdkFluidSetMaxParticles(wcFluid->psdkFluid, 
									wcgState.psdkSimulationOwnership, 
									maxParticles);
#endif
}

S32	wcFluidSetDamping(	const WorldCollIntegrationMsg* msg,
						WorldCollFluid *wcFluid, 
						F32 damping)
{
#if PSDK_DISABLED
	return 0;
#else
	if(	msg->msgType != WCI_MSG_NOBG_WHILE_SIM_SLEEPS &&
		msg->msgType != WCI_MSG_BG_BETWEEN_SIM
		||
		!wcFluid)
	{
		return 0;
	}

	return psdkFluidSetDamping(	wcFluid->psdkFluid, 
								wcgState.psdkSimulationOwnership, 
								damping);
#endif
}

S32	wcFluidSetViscosity(const WorldCollIntegrationMsg* msg,
						WorldCollFluid *wcFluid, F32 viscosity)
{
#if PSDK_DISABLED
	return 0;
#else
	if(	msg->msgType != WCI_MSG_NOBG_WHILE_SIM_SLEEPS &&
		msg->msgType != WCI_MSG_BG_BETWEEN_SIM
		||
		!wcFluid)
	{
		return 0;
	}

	return psdkFluidSetViscosity(	wcFluid->psdkFluid, 
									wcgState.psdkSimulationOwnership, 
									viscosity);
#endif
}

S32	wcFluidSetStiffness(const WorldCollIntegrationMsg* msg,
						WorldCollFluid *wcFluid, F32 stiffness)
{
#if PSDK_DISABLED
	return 0;
#else
	if(	msg->msgType != WCI_MSG_NOBG_WHILE_SIM_SLEEPS &&
		msg->msgType != WCI_MSG_BG_BETWEEN_SIM
		||
		!wcFluid)
	{
		return 0;
	}

	return psdkFluidSetStiffness(	wcFluid->psdkFluid, 
									wcgState.psdkSimulationOwnership, 
									stiffness);
#endif
}