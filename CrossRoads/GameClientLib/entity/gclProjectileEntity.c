#include "gclProjectileEntity.h"
#include "Entity.h"
#include "dynFxInfo.h"
#include "dynFxInterface.h"
#include "StringCache.h"

// -------------------------------------------------------------------------------------------------------------------
void gclProjectile_UpdateProjectile(F32 fDTime, Entity *pEnt)
{
	if (entGetType(pEnt) == GLOBALTYPE_ENTITYPROJECTILE && !entIsAlive(pEnt) && !pEnt->bVisionEffectDeath)
	{
		// todo(rp): don't use this variable...
		pEnt->bVisionEffectDeath = true;

		// remove all the maintained FX- let the projectile die out 
		dtFxManRemoveMaintainedFx(pEnt->dyn.guidFxMan, NULL);
	}

}
