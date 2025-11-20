#ifndef AIPOWERS_H
#define AIPOWERS_H

#include "referencesystem.h"

typedef struct AIConfig					AIConfig;
typedef struct AIVarsBase				AIVarsBase;
typedef struct AIPowerConfig			AIPowerConfig;
typedef struct AIPowerConfigList		AIPowerConfigList;
typedef struct AIPowerEntityInfo		AIPowerEntityInfo;
typedef struct AIPowerInfo				AIPowerInfo;
typedef struct AIPowerMultiTickAction	AIPowerMultiTickAction;
typedef struct AttribMod				AttribMod;
typedef struct AttribModDef				AttribModDef;
typedef struct CommandQueue				CommandQueue;	
typedef struct CritterPowerConfig		CritterPowerConfig;
typedef struct Entity					Entity;
typedef struct Expression				Expression;
typedef struct ExprLocalData			ExprLocalData;
typedef struct Power					Power;
typedef struct PowerDef					PowerDef;
typedef struct aiModifierDef			aiModifierDef;
typedef U32 EntityRef;

typedef void (*MultiTickActionClearedCallback)(Entity *e, Power *power, int bPowerUsed);

AUTO_STRUCT;
typedef struct AIAllPowerConfigLists{
	AIPowerConfigList** configs;		AST(NAME("AIPowerConfigList:"))
}AIAllPowerConfigLists;

typedef enum AIPowersRateOptions {
	AI_POWERS_RATE_DEAD				= 1 << 1,
	AI_POWERS_RATE_USABLE			= 1 << 2,
	AI_POWERS_RATE_PREFERRED		= 1 << 3,
	AI_POWERS_RATE_NOT_PREFERRED	= 1 << 4,
	AI_POWERS_RATE_IGNOREBONUSWEIGHT= 1 << 5,
	AI_POWERS_RATE_COUNT_VALID_FAILS= 1 << 6,
	AI_POWERS_RATE_IGNORE_WORLD		= BIT(7),		// Allows LOS and Range failures to pass
} AIPowersRateOptions;

typedef enum AIPowersExecuteFailureReason {
	AI_POWERS_EXECUTE_FAIL_NONE = 0, 
	AI_POWERS_EXECUTE_FAIL_RECHARGE, 
	AI_POWERS_EXECUTE_FAIL_COOLDOWN,
	AI_POWERS_EXECUTE_FAIL_COST,
	AI_POWERS_EXECUTE_FAIL_ISACTIVE,
	AI_POWERS_EXECUTE_FAIL_ISCHARGING,
	AI_POWERS_EXECUTE_FAIL_LOS,
	AI_POWERS_EXECUTE_FAIL_RANGE,
	AI_POWERS_EXECUTE_FAIL_PERCEIVE,
	AI_POWERS_EXECUTE_FAIL_OTHER
} AIPowersExecuteFailureReason;

typedef struct AIPowerRateOutput
{
	F32				rating;
	AIPowerInfo		*targetPower;
} AIPowerRateOutput;

void aiPowerConfigDefLoad(void);
void aiPowerConfigLoad(void);

AIPowerEntityInfo* aiPowerEntityInfoCreate(void);
static void aiPowerEntityInfoDestroy(AIPowerEntityInfo* powers);

void aiAddPowersFromCritterPowerConfigs(Entity* be, AIVarsBase* ai, CritterPowerConfig** critterPow, const char* blameFile);
void aiDestroyPowers(Entity* be, AIVarsBase* aib);
void aiAddPower(Entity *e, AIVarsBase* aib, Power* power);
void aiRemovePower(Entity* be, AIVarsBase* aib, Power *power);

// when doing a power reset in character_ResetPowersArray, 
void aiPowersResetPowersBegin(Entity *e, Power **ppOldPowers);
void aiPowersResetPowersEnd(Entity *e, Power **ppNewPowers);
bool aiPowersResetPowersIsInReset();


AIPowerInfo* aiPowersFindInfo(Entity* be, AIVarsBase* aib, const char* powName);
AIPowerInfo* aiPowersFindInfoByID(Entity* be, AIVarsBase* aib, U32 id);

static void aiApplyPowerConfigList(Entity* be, AIVarsBase* aib);

void aiPowersUpdateConfigSettings(Entity *e, AIVarsBase *aib, AIConfig *config);

int aiPowersGetBestPowerForTarget(Entity *e, Entity *target, U32 powerTypeFlags, int bIgnoreLeashing, SA_PARAM_NN_VALID AIPowerRateOutput *pOutput);
int aiPowersPickBuffPowerAndTarget(Entity* be, AIVarsBase* aib, int outOfCombat, SA_PARAM_NN_VALID AIPowerInfo **ppPowInfoOut, SA_PARAM_NN_VALID Entity **ppTargetOut);
int aiPowersPickControlPowerAndTarget(Entity* be, AIVarsBase* aib, SA_PARAM_NN_VALID AIPowerInfo **ppPowInfoOut, SA_PARAM_NN_VALID Entity **ppTargetOut);
AIPowerInfo* aiPowersGetCurePowerForAttribMod(Entity *e, AIVarsBase *aib, Entity *target, AttribMod *pMod, AttribModDef *pModDef);

void aiRatePowers(Entity *e, AIVarsBase *aib, Entity *target, F32 targetDistSQR, AIPowersRateOptions options, AIPowerInfo ***usablePowersOut, F32 *totalRatingOut);
U32 aiIsUsingPowers(Entity *e, AIVarsBase *aib);
void aiStopAttackPowersOnTarget(Entity *e, Entity *target);
S64 aiPowersGetPostRechargeTime(Entity* e, AIVarsBase* aib, AIConfig* config);
void aiUsePowers(Entity* be, AIVarsBase* aib, AIConfig* config, U32 dead, int randomizeExecutionTime, S64 minTimeToUse);
void aiUsePower(Entity* be, AIVarsBase* aib, AIPowerInfo* powerInfo, Entity* target, Entity* secondaryTarget, const Vec3 vTargetPos, int doPowersCheck, AIPowerInfo* chainSource, int forceTarget, int cancelExisting);

void aiQueuePower(Entity* be, AIVarsBase* aib, AIPowerInfo* powerInfo, Entity* target, Entity* secondaryTarget);
void aiQueuePowerAtTime(Entity* be, AIVarsBase* aib, AIPowerInfo* powerInfo, Entity* target, Entity* secondaryTarget, S64 execTime, AIPowerInfo* chainSource);
void aiQueuePowerTargetedPosAtTime(	Entity* e, AIVarsBase* aib, AIPowerInfo* powerInfo, const Vec3 vTargetPos, S64 execTime);
void aiClearAllQueuedPowers(Entity* e, AIVarsBase* aib);

AIPowerConfig* aiGetPowerConfig(Entity* be, AIVarsBase* aib, AIPowerInfo* powInfo);

void aiCheckQueuedPowers(Entity* be, AIVarsBase* aib);

void aiKnockDownCritter(Entity* be);

F32 aiGetPreferredMinRange(Entity* be, AIVarsBase* aib);
F32 aiGetPreferredMaxRange(Entity* be, AIVarsBase* aib);

void aiPowersRunAIExpr(Entity* be, Entity* modowner, Entity* modsource, Expression* expr, CommandQueue **cleanupHandlers, ExprLocalData ***localData);

Entity* aiPowersGetTarget(Entity* e, AIVarsBase* aib, Entity* target, AIPowerInfo* powInfo, AIPowerConfig* powConfig, int updateOverrideTarget);

int aiPowersAllowedToExecutePower(Entity* e, AIVarsBase* aib, Entity* target, AIPowerInfo* powInfo, AIPowerConfig* powConfig, SA_PARAM_OP_VALID AIPowersExecuteFailureReason *preason);



void aiAddPowerConfigModByTagBit(Entity *e, AIVarsBase *aib, U32 **idsOut, int powerAITagBitRequire, int powerAITagBitExclude, const char *objPath, const char *val);
void aiAddPowerConfigModByTag(Entity *e, AIVarsBase *aib, U32 **idsOut, const char* tagRequire, const char* tagExclude, const char *objPath, const char *val);
void aiPowerConfigModRemove(Entity* e, AIVarsBase* aib, int handle);
int aiPowerConfigCheckSettingName(const char* setting);


// Calculate Preferred Dynamic Range and update aib - return true if update occurred
bool aiPowersCalcDynamicPreferredRange(Entity *e, AIVarsBase *aib);

// Calculate Preferred Dynamic Range (returns true if valid range is determined and sets rangeMinOut and rangeMaxOut to range)
bool aiPowersCalcDynamicPreferredRangeEx(Entity *e, AIVarsBase *aib, AIConfig *config, F32 *rangeMinOut, F32 *rangeMaxOut);

void aiPowersGetCureTagUnionList(Entity *e, AIVarsBase *aib, U32 **ppeaInOutList);

int aiCheckPowersEndConditionExpr(Entity* e, AIVarsBase* aib, Entity* target, AIPowerInfo* powInfo, Expression* expr, const char* blamefile);
void aiTurnOffPower(Entity* be, Power *power);
void aiCancelQueuedPower(Entity *e);


int aiPowerForceUsePower(SA_PARAM_NN_VALID Entity* e, SA_PARAM_NN_VALID const char *pchName, SA_PARAM_OP_VALID MultiTickActionClearedCallback cb);

// Indicates whether the entity can use/queue a power right now. Ignores the distance to the target.
bool aiCanUseOrQueuePowerNow(Entity* e, AIVarsBase* aib, AIConfig* config);

#endif