#ifndef AIBRAWLERCOMBAT_H
#define AIBRAWLERCOMBAT_H

typedef struct AIConfig AIConfig;
typedef struct AIVarsBase AIVarsBase;
typedef struct Entity Entity;

AST_PREFIX(WIKI(AUTO))


AUTO_STRUCT;
typedef struct BrawlerRoleConfig
{
	// the tag to be referenced by the aiConfig's BrawlerCombatConfig
	const char *pchTag;							AST(NAME(Tag), POOL_STRING)

	// any other brawler roles this role should care about when considering its MaxEngaged
	const char **pchSiblingTags;				AST(NAME(SiblingTags), POOL_STRING)

	// The max this role cares to have engaged
	S32	iMaxEngaged;							AST(DEFAULT(1))

	// The minimum range to keep while in waiting state
	F32 fMinDistanceToTargetWhileWaiting;		AST(DEFAULT(10))

	// The maximum range to keep while in waiting state
	F32 fMaxDistanceToTargetWhileWaiting;		AST(DEFAULT(20))

	// The distance to back away once the entity hits their target.
	// The value must be between fMinDistanceToTargetWhileWaiting and fMaxDistanceToTargetWhileWaiting
	F32 fBackAwayDistance;						AST(DEFAULT(15))
	
	// the percent chance per tick that this guy will be forced to run in to engage. 
	// This will not occur if there are already more than the MaxEngaged forced to engage.
	F32 fPercentChancePerTickToEngage;

	U32 usedFields[1];							AST(USEDFIELD)

} BrawlerRoleConfig;
extern ParseTable parse_BrawlerRoleConfig[];


AUTO_STRUCT WIKI("BrawlerCombatGlobalConfig");
typedef struct BrawlerCombatGlobalConfig
{
	BrawlerRoleConfig		**eaBrawlerRoles;	AST(NAME(BrawlerRole))
		
	// The time after being damaged that the forced engaged can end. 
	// Note that when forced to engage, the AI will keep engaging until it performs an attack power on the target
	F32 fForceEngageTimeOnDamage;				AST(DEFAULT(3))

	
} BrawlerCombatGlobalConfig;

AUTO_STRUCT WIKI("BrawlerCombatConfig");
typedef struct BrawlerCombatConfig
{
	// the Tag that should correspond to a role tag in the BrawlerConfig
	const char *pchTag;							AST(NAME(Tag), POOL_STRING)	
	
	BrawlerRoleConfig	*pOverride;

	U32 usedFields[1];							AST(USEDFIELD)
} BrawlerCombatConfig;


typedef enum EBrawlerCombatState 
{
	EBrawlerCombatState_ATTEMPTING_ENGAGE,

	EBrawlerCombatState_ENGAGED,

	EBrawlerCombatState_DISENGAGED,

	EBrawlerCombatState_MAX
} EBrawlerCombatState;

typedef struct BrawlerCombatData
{
	// pointer to the pooled string tag
	const char *pchTag;				AST(POOL_STRING)	

	// point we found to go wait at
	Vec3 vWaitingPoint;
	
	// time at which force engagement can end.
	S64 forceEngageTimeEnd;

	// used to throttle the check to move back towards the enemy.
	S64 lastMoveTowardsEnemyTime;
	
	EBrawlerCombatState eState;

	// disengaged, backing away
	U32 bBackingAway		: 1;

	// disengaged, but trying to get closer to the target
	U32 bGettingCloser		: 1;
		
} BrawlerCombatData;

void aiBrawlerCombat_InheritAIGlobalSettings(BrawlerCombatConfig *pConfig);

void aiBrawlerCombat_ValidateConfig(AIConfig *pConfig);
void aiBrawlerCombat_ValidateGlobalConfig(const char *pchFilename, BrawlerCombatGlobalConfig *pConfig);

void aiBrawlerCombat_SetState(AIVarsBase *aib, EBrawlerCombatState eState);
void aiBrawlerCombat_Update(Entity *e, AIVarsBase *aib, AIConfig* config);
void aiBrawlerCombat_Shutdown(AIVarsBase *aib);
bool aiBrawlerCombat_ShouldDisengage(Entity *e, AIVarsBase *aib, AIConfig* config);
void aiBrawlerCombat_DoBackAwayAndWanderMovement(Entity *e, AIConfig* pAIConfig, const Vec3 vAttackTargetPos);
void aiBrawlerCombat_ForceEngage(S32 iPartitionIdx, AIVarsBase *aib, F32 fForceTime);

#endif