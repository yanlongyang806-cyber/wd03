#include "aiConfig.h"
#include "aiDebug.h"
#include "aiExtern.h"
#include "aiLib.h"
#include "aiMovement.h"
#include "aiPowers.h"
#include "aiStruct.h"
#include "aiTeam.h"

#include "aiFCStruct.h"
#include "AttribModFragility.h"
#include "Entity.h"
#include "Character.h"
#include "character_target.h"
#include "earray.h"
#include "entCritter.h"
#include "Entity.h"
#include "EntityGrid.h"
#include "Expression.h"
#include "MemoryPool.h"
#include "oldencounter_common.h"
#include "Powers.h"
#include "StateMachine.h"
#include "timing.h"
#include "CharacterAttribs.h"
#include "StringCache.h"

F32 aiGlobalOverrideEnterCombatWaitTime = 0;
AUTO_CMD_FLOAT(aiGlobalOverrideEnterCombatWaitTime, aiGlobalOverrideEnterCombatWaitTime);

//int aiEnableWaitTimeBotheredModel = true;
//AUTO_CMD_INT(aiEnableWaitTimeBotheredModel, aiEnableWaitTimeBotheredModel);


static int aiUsePowersAggroRadii = true;
AUTO_CMD_INT(aiUsePowersAggroRadii, aiUsePowersAggroRadii);

static F32 aiGlobalOverrideAwareRatio = 0;
AUTO_CMD_FLOAT(aiGlobalOverrideAwareRatio, aiGlobalOverrideAwareRatio);

static F32 aiGlobalOverrideAggroRadius = 0;
AUTO_CMD_FLOAT(aiGlobalOverrideAggroRadius, aiGlobalOverrideAggroRadius);

#define AI_AWARE_RATIO 0.66666
#define AI_PERCEPT_RANGE 60

void aiExternUpdatePerceptionRadii(Entity* be, AIVarsBase* aib)
{
	AIConfig* config = NULL;
	PerfInfoGuard *guard;
	
	PERFINFO_AUTO_START_FUNC_GUARD(&guard);
	config = aiGetConfig(be, aib);

	if(be->pChar)
	{
		if(aiGlobalOverrideAwareRatio)
			aib->awareRatio = aiGlobalOverrideAwareRatio;
		else if(config->awareRatio>=0 && config->awareRatio<=1)
			aib->awareRatio = config->awareRatio;
		else
		{
			ErrorFilenamef(config->filename, "AwareRatio must be between 0 and 1");
			aib->awareRatio = AI_AWARE_RATIO;
		}

		aib->awareRatio = MINMAX(aib->awareRatio, 0, 1);

		if(aiGlobalOverrideAggroRadius)
			aib->aggroRadius = aiGlobalOverrideAggroRadius;
		if(config->overrideAggroRadius)
			aib->aggroRadius = config->overrideAggroRadius;
		else if(aiUsePowersAggroRadii)
		{
			// TODO: Remove this check
			if(be->pChar->pattrBasic->fAggro)
				aib->aggroRadius = be->pChar->pattrBasic->fAggro;
			else
				aib->aggroRadius = 100;
		}
		else
			aib->aggroRadius = AI_PERCEPT_RANGE;

		aib->confused = be->pChar->pattrBasic->fConfuse > 0;
	}
	else
	{
		aib->awareRatio = 0;
		aib->aggroRadius = 0;
	}

	aib->awareRadius = aib->awareRatio * aib->aggroRadius;

	if(config->overrideProximityRadius)
		aib->proximityRadius = config->overrideProximityRadius;
	else
		aib->proximityRadius = 100;

	if(aib->team && config->combatMaxReinforceDist > aib->proximityRadius && aib->member == aib->team->reinforceMember)
		aib->proximityRadius = config->combatMaxReinforceDist;

	aib->proximityRadius = MAX(aib->aggroRadius, aib->proximityRadius);

	PERFINFO_AUTO_STOP_GUARD(&guard);
}

Entity* aiDetermineAggroEntity(Entity *dmgTarget, Entity *dmgSource, Entity *dmgOwner)
{
	// If the source ent is a pet, give it the aggro, else give the aggro to the owner
	//   The latter is for the case of an attrib on another entity that deals damage
	if(!dmgSource)
		return dmgOwner;
	
	if (entGetType(dmgSource) == GLOBALTYPE_ENTITYPROJECTILE)
		return dmgOwner;

	if(!dmgOwner)
		return dmgSource;
	
	if(dmgOwner == dmgSource)
		return dmgOwner;
	else if(dmgSource->erOwner==entGetRef(dmgOwner))
		return dmgSource;
	else
		return dmgOwner;

	return NULL;
}

#define IS_AVOID_ATTRIBASPECT(attrib, aspect)		((aspect)==kAttribAspect_BasicAbs && (attrib)==kAttribType_AIAvoid)
#define IS_SOFT_AVOID_ATTRIBASPECT(attrib, aspect)	((aspect)==kAttribAspect_BasicAbs && (attrib)==kAttribType_AISoftAvoid)
#define IS_THREAT_ATTRIBASPECT(attrib, aspect)		((aspect)==kAttribAspect_BasicAbs && (attrib)==kAttribType_AIThreat)
void aiFCNotify(Entity* e, AttribMod* mod, AttribModDef* moddef, F32 mag, F32 threatScale)
{
	int controlIdx = StaticDefineIntGetInt(PowerTagsEnum, "Control");
	int iPartitionIdx = entGetPartitionIdx(e);
	Entity *src;

	// In some cases we want to assign this to the owner instead of the source.  That's the case
	//  when they're different entities and the source is KoS to the owner.
	EntityRef erSource = mod->erSource;
	if(mod->erSource!=mod->erOwner)
	{
		Entity *eSource = entFromEntityRef(iPartitionIdx, mod->erSource);
		Entity *eOwner = entFromEntityRef(iPartitionIdx, mod->erOwner);
		Entity *eAggro = NULL;
		
		eAggro = aiDetermineAggroEntity(e, eSource, eOwner);

		if(eAggro)
			erSource = entGetRef(eAggro);
	}

	src = entFromEntityRef(iPartitionIdx, erSource);
	if(controlIdx != -1 && powertags_Check(&moddef->tags, controlIdx))
		aiNotify(e, src, AI_NOTIFY_TYPE_STATUS, mag, mag, NULL, mod->uiApplyID);

	if(IS_DAMAGE_ATTRIBASPECT(moddef->offAttrib, moddef->offAspect))
	{
		aiNotify(e, src, AI_NOTIFY_TYPE_DAMAGE,	mag * threatScale, mag * threatScale, NULL, mod->uiApplyID);
	}
	else if(IS_AVOID_ATTRIBASPECT(moddef->offAttrib, moddef->offAspect))
	{
		AIAvoidParams* params = (AIAvoidParams*)moddef->pParams;

		aiNotify(e, src, AI_NOTIFY_TYPE_AVOID, mag, mag, params, mod->uiApplyID);
	}
	else if(IS_SOFT_AVOID_ATTRIBASPECT(moddef->offAttrib, moddef->offAspect))
	{
		AISoftAvoidParams* params = (AISoftAvoidParams*)moddef->pParams;
		aiNotify(e, src, AI_NOTIFY_TYPE_SOFT_AVOID, mag, mag, params, mod->uiApplyID);
	}	
	else if(IS_THREAT_ATTRIBASPECT(moddef->offAttrib, moddef->offAspect))
	{
		aiNotify(e, src, AI_NOTIFY_TYPE_THREAT, mag * threatScale, mag * threatScale, NULL, mod->uiApplyID);
	}
	else if(IS_HEALING_ATTRIBASPECT(moddef->offAttrib, moddef->offAspect))
	{
		aiNotify(e, src, AI_NOTIFY_TYPE_HEALING, mag * threatScale, mag * threatScale, NULL, mod->uiApplyID);
	}
	
	if(e->aibase && e->aibase->team)
	{
		aiTeamNotifyNewAttribMod(e->aibase->team, e, mod, moddef);
	}
}

static F32 aiGlobalOverrideStealth = 0;
AUTO_CMD_FLOAT(aiGlobalOverrideStealth, aiGlobalOverrideStealth);

F32 aiExternGetStealth(Entity* be)
{
	if(aiGlobalOverrideStealth)
		return aiGlobalOverrideStealth;
	else
		return be->pChar->pattrBasic->fAggroStealth;
}

void aiExternGetHealth(const Entity* be, F32* health, F32* maxHealth)
{
	if(health)
		*health = be->pChar ? be->pChar->pattrBasic->fHitPoints : 0;
	if(maxHealth)
		*maxHealth = be->pChar ? be->pChar->pattrBasic->fHitPointsMax : 0;
}

void aiExternGetShields(const Entity* be, F32* pshield, F32* pmaxShield)
{
	if (be->pChar && eaSize(&be->pChar->ppModsShield))
	{
		F32 shield = 0.f, maxShield = 0.f;
		// loop through all the shields and get the sum of the shield health
		FOR_EACH_IN_EARRAY(be->pChar->ppModsShield, AttribMod, pShield)
			if (pShield->pFragility)
			{
				shield += pShield->pFragility->fHealth;
				maxShield += pShield->pFragility->fHealthMax;
			}
		FOR_EACH_END

		if(pshield)
			*pshield = shield;
		if(pmaxShield)
			*pmaxShield = maxShield;
	}
	else
	{
		if(pshield)
			*pshield = 0.f;
		if(pmaxShield)
			*pmaxShield = 0.f;
	}
}