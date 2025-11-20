
#include "PhysicsSDKPrivate.h"
#include "MemoryBudget.h"
#include "mathutil.h"
#include "utils.h"

#if !PSDK_DISABLED
	PSDKState psdkState;
#endif

AUTO_RUN;
void psdkSetupBudgetMapping(void)
{
#if !PSDK_DISABLED
	char *s, buf[1024];

	strcpy(buf, __FILE__);
	s = strstri(buf, "\\libs\\");
	if (s)
	{
		*s = 0;
		strcat(buf, "\\3rdparty\\" PHYSX_VERSION_FOLDER "\\*");
		memBudgetAddDirFilterMapping(buf, BUDGET_Physics);
	}

	memBudgetAddDirFilterMapping("c:\\src\\3rdparty\\" PHYSX_VERSION_FOLDER "\\*", BUDGET_Physics);
	memBudgetAddDirFilterMapping("e:\\p4\\*", BUDGET_Physics);
	memBudgetAddDirFilterMapping("f:\\scmvista\\*", BUDGET_Physics);
#endif
}

AUTO_COMMAND ACMD_NAME("psdkPrintCookingClient") ACMD_CLIENTONLY;
void psdkCmdPrintCookingClient(S32 enabled){
#if !PSDK_DISABLED
	psdkState.flags.printCooking = !!enabled;
#endif
}

AUTO_COMMAND ACMD_NAME("psdkPrintCookingServer") ACMD_SERVERONLY;
void psdkCmdPrintCookingServer(S32 enabled){
#if !PSDK_DISABLED
	psdkState.flags.printCooking = !!enabled;
#endif
}

AUTO_COMMAND ACMD_NAME("psdkPrintNewActorBounds") ACMD_SERVERONLY;
void psdkCmdNewActorBounds(S32 enabled){
#if !PSDK_DISABLED
	psdkState.flags.printNewActorBounds = !!enabled;
#endif
}

AUTO_COMMAND ACMD_NAME("psdkPrintTestThing");
void psdkCmdPrintTestThing(S32 enabled){
#if !PSDK_DISABLED
	psdkState.flags.printTestThing = !!enabled;
#endif
}

AUTO_COMMAND ACMD_NAME("psdkFindHeightFieldPoint");
void psdkCmdFindHeightFieldPoint(	F32 radius,
									const Vec3 pos)
{
#if !PSDK_DISABLED
	if(pos){
		psdkState.debug.findHeightFieldPointRadius = radius;
		
		copyVec3(	pos,
					psdkState.debug.findHeightFieldPoint);
					
		psdkState.debug.flags.findHeightFieldPointEnabled = radius > 0.f;
	}
#endif
}

AUTO_COMMAND ACMD_CMDLINE;
void psdkDisableHardware(void){
#if !PSDK_DISABLED
	psdkState.flags.noHardwareSupport = 1;
#endif
}

AUTO_COMMAND ACMD_CMDLINE;
void psdkAssertOnFailure(S32 enabled){
#if !PSDK_DISABLED
	psdkState.flags.assertOnFailure = !!enabled;
#endif
}

AUTO_COMMAND;
void psdkSetLimits(	U32 maxActors,
					U32 maxBodies,
					U32 maxStaticShapes,
					U32 maxDynamicShapes,
					U32 maxJoints)
{
#if !PSDK_DISABLED
	psdkState.limits.maxActors = MINMAX(maxActors, 0, PSDK_MAX_ACTORS_PER_SCENE);
	psdkState.limits.maxBodies = MINMAX(maxBodies, 0, PSDK_MAX_ACTORS_PER_SCENE);
	psdkState.limits.maxStaticShapes = MINMAX(maxStaticShapes, 0, PSDK_MAX_ACTORS_PER_SCENE);
	psdkState.limits.maxDynamicShapes = MINMAX(maxDynamicShapes, 0, PSDK_MAX_ACTORS_PER_SCENE);
	psdkState.limits.maxJoints = MINMAX(maxJoints, 0, PSDK_MAX_ACTORS_PER_SCENE);
#endif
}

AUTO_COMMAND ACMD_CMDLINE ACMD_ACCESSLEVEL(9);
void psdkDisableErrors(S32 disable){
#if !PSDK_DISABLED
	psdkState.flags.disableErrors = !!disable;
#endif
}

AUTO_COMMAND;
void psdkSetDefaultLimits(void){
	psdkSetLimits(0, 0, 0, 0, 0);
}

AUTO_RUN_ANON(psdkSetDefaultLimits(););
