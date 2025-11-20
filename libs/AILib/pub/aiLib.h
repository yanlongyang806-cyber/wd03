#pragma once
GCC_SYSTEM

#include "stdtypes.h"
#include "aiEnums.h"

typedef struct AIVolumeEntry				AIVolumeEntry;
typedef struct AICivilian					AICivilian;
typedef struct AIConfig						AIConfig;
typedef struct AICombatRolesTeamRole		AICombatRolesTeamRole;
typedef struct AIInitParams					AIInitParams;
typedef struct AIPartitionState				AIPartitionState;
typedef struct AIPowerInfo					AIPowerInfo;
typedef struct AIStatusTableEntry			AIStatusTableEntry;
typedef struct AITeamStatusEntry			AITeamStatusEntry;
typedef struct AITeam						AITeam;
typedef struct AIVarsBase					AIVarsBase;
typedef struct CritterDef					CritterDef;
typedef struct Entity						Entity;
typedef struct ExprContext					ExprContext;
typedef struct Expression					Expression;
typedef struct FSM							FSM;
typedef struct FSMContext					FSMContext;
typedef struct FSMLDSendMessage				FSMLDSendMessage;
typedef struct Message						Message;
typedef struct MovementRequester			MovementRequester;
typedef struct MovementRequesterMsg			MovementRequesterMsg;
typedef struct Power						Power;
typedef struct WorldCollCollideResults		WorldCollCollideResults;
typedef struct ZoneMap						ZoneMap;
typedef struct WorldColl					WorldColl;
typedef void *ReferenceHandle;

#define AI_PROX_NEAR_DIST 10
#define AI_PROX_MEDIUM_DIST 30
#define AI_PROX_FAR_DIST 60

#define AI_PRIORITY_TICK_RATE	0.1
#define AI_TICK_RATE			0.5
#define AI_TICK_RATE_INACTIVE	1.0

#define AI_STATUS_NO_PROCESS_DROP	2.0

#define AI_TICK_TIME(e)	(ENTACTIVE(e) ? AI_TICK_RATE : AI_TICK_RATE_INACTIVE)


void aiSetupExprContext(Entity* e, AIVarsBase* aib, int iPartitionIdx, ExprContext* context, int staticCheck);
ExprContext* aiGetStaticCheckExprContext(void);

void aiLibStartup(void);
void aiLibStartupAfterPowers(void);
void aiLibOncePerFrame(void);
void aiTickBuckets(void);
void aiPartitionUnload(int partition);
void aiPartitionLoad(int partitionIdx);
void aiMapLoad(bool fullInit);
void aiMapUnload(void);
void aiTick(Entity* be, AIVarsBase* aib);
void aiCivilianTick(Entity *e);
void aiDeadTick(Entity* be, AIVarsBase* aib);

AIPartitionState* aiPartitionStateGet(int partitionId, int create);
void aiPartitionStateDestroy(int partitionId);

void aiForceThinkTick(Entity* e, AIVarsBase* aib);

void aiMessageDestroyAll(FSMContext* fsmContext);

void aiSetAttackTarget(Entity* be, AIVarsBase* aib, Entity* newAttackTarget, AIStatusTableEntry* newAttackTargetStatus, int forceUpdate);
int aiSetPreferredAttackTarget(Entity *e, AIVarsBase *aib, Entity *target, int bAttackTarget);
void aiClearPreferredAttackTarget(Entity *e, AIVarsBase *aib);

typedef void (*AIPreferredTargetClearedCallback)(Entity *e);
void aiSetPreferredAttackTargetClearedCallback(AIPreferredTargetClearedCallback callback);

void aiSetBothered(Entity* e, AIVarsBase* aib, int on, const char* reason);
int aiEvalTargetingRequires(Entity *e, AIVarsBase *aib, AIConfig *config, Entity *target);

F32 aiGetGuardPointDistance(Entity* e, AIVarsBase* aib, Entity* target);

F32 aiGetSpawnPosDist(Entity *e, AIVarsBase *aib);
void aiGetSpawnPos(Entity *e, AIVarsBase *aib, Vec3 spawnPos);
int aiCloseEnoughToSpawnPos(Entity* e, AIVarsBase* aib);

void aiSetLeashTypePos(Entity *e, AIVarsBase *aib, const Vec3 rallyPt);
void aiSetLeashTypeOwner(Entity *e, AIVarsBase *aib);
void aiSetLeashTypeEntity(Entity *e, AIVarsBase *aib, Entity *eLeashTo);
Entity* aiGetLeashEntity(Entity *e, AIVarsBase *aib);
// Returns the leash type for the AI
AILeashType aiGetLeashType(AIVarsBase *aib);
void aiGetLeashPosition(Entity *e, AIVarsBase *aib, Vec3 vOutPos);
const F32 * aiGetRallyPosition(AIVarsBase *aib);
F32 aiGetLeashingDistance(Entity *e, Entity* target, AIConfig *config, F32 powerRange);
F32 aiGetCoherencyDist(Entity *e, AIConfig *config);
int aiIsPositionWithinCoherency(Entity *e, AIVarsBase *aib, const Vec3 vPos);
int aiIsTargetWithinLeash(Entity *e, AIVarsBase *aib, Entity* target, F32 powerRange);

int aiIsInCombat(Entity *e); 

void aiForceProxUpdates(int on);
void aiUpdateProxEnts(Entity *be, AIVarsBase *aib);
void aiUpdateMeleeEnts(Entity *be, AIVarsBase *aib);
void aiClearMeleeEnts(Entity *be, AIVarsBase *aib);

void aiCombatInitRelativeLocs(Entity *be, AIVarsBase *aib);
void aiCombatCleanupRelLocs(Entity *be, AIVarsBase *aib);
void aiCombatReset(Entity *e, AIVarsBase *aib, int heal, int combat_reset, int ignoreLevels);
void aiCombatDoSocialAggroPulse(Entity* e, AIVarsBase* aib, int primary);
void aiCombatAddTeamToCombatTeam(Entity *e, AIVarsBase* aib, bool bDoSecondaryPulseOnAddedTeammates);
void aiCombatOnPowerExecuted(Entity *e, AIVarsBase *aib, AIPowerInfo *info);
void aiCombatEnableFaceTarget(Entity* e, S32 enable);
void aiCombatSetFaceTarget(Entity *pEnt, EntityRef erAttackTarget);


// in aiMessages.c for now
void aiNotify(Entity* be, Entity* sourceBE, AINotifyType notifyType, F32 damageVal, F32 damageValNoOverage, void *params, int uid);
void aiNotifyPowerExecuted(Entity* be, Power* power);
void aiNotifyPowerRecharged(Entity* e, Power* power);
void aiNotifyPowerEnded(Entity* be, Entity* sourceEnt, AINotifyType notifyType, int oldUid, int newUid, void *params);
void aiNotifyUpdateCombatTimer(Entity* source, Entity* target, bool bSourceOnly);
S32 aiNotifyPowerMissed(Entity* be, Entity* sourceEnt, F32 threatscale);

void aiNotifyInteracted(Entity* e, Entity* source);
void aiMessageProcessChat(Entity* e, Entity* src, const char *msg);
void aiMessageProcessTarget(Entity* e, Entity* src);
void aiMessageSendEntToEnt(Entity* be, AIVarsBase* aib, EntityRef target, const char* tag,
						   F32 value, Entity*** entArrayData, F32 maxDist, const char* anim,
						   FSMLDSendMessage* mydata);

SA_ORET_OP_VALID AIStatusTableEntry* aiStatusFind(Entity* e, AIVarsBase* aib, Entity* target, int create);
void aiStatusRemove(Entity* e, AIVarsBase* aib, Entity* target, const char* reason);
void aiCleanupStatus(Entity *e, AIVarsBase *aib);

void aiClearStatusTable(Entity* e);

void aiOnDeathCleanup(Entity* e);
void aiOnUndeath(Entity *e);

const char* aiGetState(Entity* e);
const char* aiGetCombatState(Entity *e);
// will return an override FSM context if there is one, otherwise returns the base fsmContext
FSMContext* aiGetCurrentBaseFSMContext(Entity *e);
// same as aiGetCurrentBaseFSMContext, but factors in the insideCombatFSM and the ai job
FSMContext* aiGetCurrentFSMContext(Entity *e);

typedef void (*aiStateChangeCallback)(Entity* e, const char* oldState, const char* newState);
typedef void (*aiAddExternVarCallback)(Entity* be, ExprContext* context);
extern aiStateChangeCallback stateChangeCallback;

void aiSetStateChangeCallback(aiStateChangeCallback callback);
void aiSetAddExternVarCallback(aiAddExternVarCallback callback);
void aiSetDebugExternVarCallback(aiAddExternVarCallback callback);

int checkWorldCollideFromAngleAndMe(Entity* be, AIVarsBase* aib, const F32* myPos,
									const F32* targetPos, const F32* idealVec,
									F32 angle, F32* outPos, F32* LOSFromTargetOnlyPos,
									int rayCastFromMyPos, int* LOSFromTargetOnly);


#define aiCollideRay(iPartitionIdx, e, sourcePos, target, targetPos, flags)\
		aiCollideRayEx(iPartitionIdx, e, sourcePos, target, targetPos, flags, AI_DEFAULT_STEP_LENGTH, NULL, NULL)

int aiCollideRayEx(	int iPartitionIdx, Entity* e, const Vec3 sourcePos1, Entity* target,
					const Vec3 targetPos1, AICollideRayFlag flags, 
					F32 stepLength, AICollideRayResult *resultOut, Vec3 collPtOut);

#define aiCollideRayWorldColl(iPartitionIdx, e, sourcePos, target, targetPos, flags)\
		aiCollideRayWorldCollEx(iPartitionIdx, e, sourcePos, target, targetPos, flags, AI_DEFAULT_STEP_LENGTH, NULL, NULL)

int aiCollideRayWorldCollEx(WorldColl* wc, Entity* e, const Vec3 sourcePos1, Entity* target,
							const Vec3 targetPos1, AICollideRayFlag flags, 
							F32 stepLength, AICollideRayResult *resultOut, Vec3 collPtOut);

// returns -FLT_MAX if ground is not found.	
// Uses the height cache 
F32 aiFindGroundDistance(WorldColl* wc, Vec3 sourcePos, F32* groundPos);

// Uses a world raycast downwards to find the ground position
int aiFindGroundPosition(WorldColl* wc, const Vec3 vSourcePos, Vec3 vOutGroundPos);

void aiHandleRegionChange(Entity *e, S32 prevRegion, S32 curRegion);

const char* aiGetStatePathWithJob(Entity* be);

void aiDisableSleep(int disable);

void aiCheckOfftickActions(Entity* e, AIVarsBase* aib, AIConfig* baseConfig);

// Returns whether this entity is valid to be considered as a target
// if teamStatus is NULL, level difference results in false ignoring the legalTarget flag
int aiIsValidTarget(Entity * pEnt, SA_PARAM_NN_VALID AITeam* team, SA_PARAM_NN_VALID Entity* target, AITeamStatusEntry* teamStatus);

// Returns true if the specified Entity is either a valid attack target or has a valid attack target
// If range is non 0, things with aggro must be within range to count
// bDisallowPlayerTeamAggro- will disregard any teams that has a player as a leader when checking aggro
int aiIsOrHasLegalTarget(Entity* e, F32 range, bool bDisallowPlayerTeamAggro,
							SA_PARAM_OP_VALID Entity **ppAggroingEntityOut);

// Adds the target as a legal target.  This function chooses whether to do the combat team or normal team
//  creating the combat team if necessary.
void aiAddLegalTarget(Entity* e, AIVarsBase* aib, Entity* target);

void aiTaunt(Entity* e, Entity* target, U32 applyID);
void aiTauntRemove(Entity* e, U32 applyID);

void aiFillInDefaultDangerFactors(AIConfig* config);

void aiAvoidVolumeRemove(int partitionIdx, void *pIdPtr);
void aiAvoidVolumeRemoveAll(int partitionIdx);

// Functions for Cutscenes
typedef bool (*aiIsEntFrozenCallback) (Entity *e);
void aiRegisterIsEntFrozenCallback(aiIsEntFrozenCallback entFrozenCB);
void aiSetCheckForFrozenEnts(int partitionId, bool check);
void aiRewindAIFSMTime(int partitionIdx, S64 timeToRewind);
void aiCutsceneEntStartCallback(Entity *pEnt);
void aiCutsceneEntEndCallback(Entity *pEnt);

int aiPowersGenerateConfigExpression(Expression* expr);

// Functions for NPC Callout

// Returns TRUE if the Civilian can do a Callout for this player
bool aiCivilianCanDoCallout(Entity *e, AICivilian *civ, Entity *pPlayer);

// Tells the Civilian to say the specified message
void aiCivilianDoCallout(Entity *e, AICivilian *civ, Entity *pPlayer, const char *fsm, const char *messageKey, U64 sourceItem);

// This puts the civilian on cooldown for a specified player, as if the civilian
// had just said a Callout string for that player
void aiCivilianAddCooldownForPlayer(AICivilian *civ, Entity *pPlayer);

void aiCivilianOnClick(Entity *civEntity, Entity *targetEntity);

// when a player just killed a critter, report to the civilian system
// and a nearby civilian may congratulate him
void aiCivilianReportPlayerKillEvent(Entity *pPlayerEnt);

// Sets the entity's isHostileToCivilians flag based on its current faction
void aiCivilianUpdateIsHostile(Entity *pEnt);

void aiCivilian_MapLoad(ZoneMap *pZoneMap, bool bFullInit);
void aiCivilian_MapUnload();
void aiCivilian_PartitionLoad(int iPartitionIdx, int bFullInit);
void aiCivilian_PartitionUnload(int iPartitionIdx);
void aiCivilianTickBuckets(void);

void aiCivilianSetKnocked(Entity *e);

// Sets up a test critter to behave as expected
void aiSetupTestCritter(Entity* e);

void aiAddToFirstList(Entity *e, AIVarsBase *aib);
void aiAddToBucket(Entity *e, AIVarsBase *aib);
void aiRemoveFromBucket(Entity *e, AIVarsBase *aib);
void aiInit(Entity* be, CritterDef* critter, AIInitParams *pInitParams);
// aiInitTeam should be called after the critter's level gets set (currently in critterFC_AddCombat())
void aiInitTeam(Entity* e, AITeam* team);
void aiFirstTickInit(Entity* e, AIVarsBase *aib);
void aiResetForRespawn(Entity *e);
void aiDestroy(Entity* be);
void aiFlagForInitialFSM(Entity *e);

void aiSetCurrentCombatFSMContext(AIVarsBase *aib, int bUseCombatRoleFSM);
void aiSetFSM(Entity* e, SA_PARAM_NN_VALID FSM *fsm);
void aiSetFSMByName(Entity* e, const char *pcFSMName);
void aiCopyCurrentFSMHandle(SA_PARAM_NN_VALID Entity* e, SA_PARAM_NN_VALID ReferenceHandle *pRefToFSM);

// returns the override FSM ID
// returns 0 on error
U32 aiPushOverrideFSMByName(Entity *e, const char *pchFSMName);
int aiRemoveOverrideFSM(Entity *e, U32 id);
void aiRemoveAllOverrideFSMs(Entity *e);
void ClearAllPowersOverrideFSM(Entity *e);


void aiTargetingExprVarsAdd(SA_PARAM_OP_VALID Entity* e, SA_PARAM_OP_VALID AIVarsBase* aib, SA_PARAM_NN_VALID ExprContext* context, SA_PARAM_OP_VALID Entity* target);
void aiTargetingExprVarsRemove(SA_PARAM_OP_VALID Entity* be, SA_PARAM_OP_VALID AIVarsBase* aib, SA_PARAM_NN_VALID ExprContext* context);

// Used mostly for pet commands
void aiChangeState(Entity* e, const char* newState);

// returns true if the requester passed in is the civilian's movement requester
bool aiCivilian_ReportRequesterDestroyed(Entity *e, const MovementRequester *mr);

void aiAggroDecay(Entity* e, AIVarsBase* aib, AITeam* team, AICombatRolesTeamRole* teamRole, AIStatusTableEntry* status, int useRoleDamageSharing);
void aiAssignDangerValues(Entity* e, AIVarsBase* aib, F32 baseDangerFactor, bool bSendAggro);

// returns if the ent is alive, also checks the entity's character's nearDeath status
bool aiIsEntAlive(Entity* e);

// adds an entity to the critter's current seek target list. 
// These targets will get an overridden aggro radius applied to them if one is set in the aiconfig for the critter
void aiAddSeekTarget(Entity *e, Entity *target);
void aiRemoveSeekTarget(Entity *e, EntityRef erTarget);

// sets a target location usually set through the power's entCreation that will come from player targeting locations. 
// FSMs have an expression to get this location
void aiSetPowersEntCreateTargetLocation(Entity *e, const Vec3 vLoc);



extern int gDisableAI;
extern int gDisableAICombatMovement;
extern int gAIConstantCombatMovement;
extern int aiEnableAggroAvgBotheredModel;

extern int targetEntVarHandle;
extern const char* targetEntString;

extern int interactLocationVarHandle;
extern const char* interactLocationString;

extern int powInfoVarHandle;
extern const char* powInfoString;

extern int targetEntStatusVarHandle;
extern const char* targetEntStatusString;

extern int contextVarHandle;
extern const char* contextString;

extern int curStateTrackerVarHandle;
extern const char* curStateTrackerString;

extern int meVarHandle;
extern const char* meString;

extern int aiVarHandle;
extern const char* aiString;

extern int attribModDefHandle;
extern const char* attribModDefString;

extern bool aiDisableForUGC;

void aiExecuteNextTickCmdQueue(AIVarsBase *aib);

extern StaticDefineInt AINotifyTypeEnum[];

#define aiCheckIgnoreFlags(e) !!(entGetFlagBits(e) & (ENTITYFLAG_DEAD | ENTITYFLAG_DESTROY | \
		ENTITYFLAG_DONOTDRAW | ENTITYFLAG_DONOTSEND | ENTITYFLAG_IGNORE | \
		ENTITYFLAG_UNTARGETABLE | ENTITYFLAG_UNSELECTABLE))
