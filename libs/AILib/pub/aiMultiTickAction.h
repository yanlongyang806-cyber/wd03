#pragma once


typedef struct AIVarsBase AIVarsBase;
typedef struct Entity Entity;
typedef struct AIPowerInfo AIPowerInfo;
typedef struct Power Power;

typedef void (*MultiTickActionClearedCallback)(Entity *e, Power *power, int bPowerUsed);

typedef enum AIMutliTickActionFlag
{
	MTAFlag_NONE = 0, 
	MTAFlag_FORCEUSETARGET		= (1 << 0),
	MTAFlag_USERFORCEDACTION	= (1 << 1),
	MTAFlag_COMBATNOMOVE		= (1 << 2)
} MTAQueuePowerFlag;

// 
int aiMultiTickAction_QueuePower(Entity* e, AIVarsBase* aib, Entity* target,
								 S32 actionType, AIPowerInfo* powerInfo, U32 flags,
								 SA_PARAM_OP_VALID MultiTickActionClearedCallback cb);

int aiMultiTickAction_QueueGotoPos(Entity *e, AIVarsBase *aib, const Vec3 vGotoPos);

int aiMultiTickAction_QueueCombatRoll(Entity *e, AIVarsBase *aib, const Vec3 vRollVec);

int aiMultiTickAction_QueueSafetyTeleport(Entity *e, AIVarsBase *aib, const Vec3 vTeleportPos);


// Clears the MTA queue of the AI
void aiMultiTickAction_ClearQueueEx(Entity *e, AIVarsBase *aib, bool bForceClearAll);
#define aiMultiTickAction_ClearQueue(e, aib) aiMultiTickAction_ClearQueueEx(e, aib, false)

// destroys the MTA queue
void aiMultiTickAction_DestroyQueue(Entity *e, AIVarsBase *aib);

// returns true if the AI has an action in it's MTA queue
int aiMultiTickAction_HasAction(Entity* e, AIVarsBase* aib);
// returns true if there is a forced action (in most cases a player told this AI to do something)
int aiMultiTickAction_HasForcedActionQueued(Entity* e, AIVarsBase* aib);

// removes all queued actions that have the given power info as the power to use
void aiMultiTickAction_RemoveQueuedAIPowerInfos(Entity* e, AIVarsBase* aib, AIPowerInfo *powInfo);

// update current queue of actions
int aiMultiTickAction_ProcessActions(Entity* e, AIVarsBase* aib);


