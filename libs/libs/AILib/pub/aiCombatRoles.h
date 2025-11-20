#ifndef AICOMBATROLES_H
#define AICOMBATROLES_H

#include "aiEnums.h"
#include "referencesystem.h"
#include "mapstate_common.h"

typedef struct AICombatRolesGuardSlot AICombatRolesGuardSlot;
typedef struct AITeam AITeam;
typedef struct AITeamMember AITeamMember;
typedef struct AIVarsBase AIVarsBase;
typedef struct Entity Entity;
typedef struct FSMContext FSMContext;

AST_PREFIX(WIKI(AUTO))

// ----------------------------------------------------------------------------------------------------------------
// def structs
// ----------------------------------------------------------------------------------------------------------------

AUTO_STRUCT;
typedef struct AICombatRole
{
	// the name of the role
	const char *pchName;						AST(REQUIRED POOL_STRING)

	// the name of the that this will try and guard
	const char *pchRoleToGuard;					AST(POOL_STRING)

	// For guards, the desired number of guards that would get assigned
	// before filling guards for the next guy 
	S32 desiredNumGuards;

	// the offset from the ent this role is guarding
	F32 fGuardOffset;							AST(DEFAULT(12))

	// The types that will get sent damage/threat aggro this type receives
	const char **ppchShareAggroWithRole;		AST(POOL_STRING NAME("ShareAggroWithRole"))

	// if specified, will completely override the ai config 
	const char *pchAIConfigOverride;			AST(POOL_STRING NAME("AIConfigOverride"))

	// config mods applied (after the pchAIConfigOverride override)
	// example line for a conig mod:
	// ConfigMod leashRangeCurrentTargetDistAdd 15
	AIConfigMod** configMods; 					AST(NAME("ConfigMod"))

	// the combat FSM that is overridden
	const char *pchCombatFSMOverride;			AST(POOL_STRING)

	// a priority list of target tags that this role responds to in order of importance.
	PetTargetType *peTargetTags;				AST(NAME("TargetTags") SUBTABLE(PetTargetTypeEnum))

	// when aggro'ed, this role will use the team's leash position as the coherency 
	// note that 'combatPositioningUseCoherency' in the aiConfig must be set for this to work
	U32 bUseLeashPositionCoherency : 1;

	// AI partners with this flag active will prefer to let somebody else run off first in response to a "Charge!"
	// command. Dealt with in gslpetcommands_entercombat()
	U32 bUnwillingToInitiateCombat : 1;
} AICombatRole;


AUTO_STRUCT;
typedef struct AICombatFormationSlotDef
{
	// positive X is to the right
	F32 x;

	// positive Z is forward
	F32	z;

} AICombatFormationSlotDef;

AUTO_STRUCT;
typedef struct AICombatFormationRoleDef
{
	// the role name these formations pertain to
	const char *pchName;						AST(POOL_STRING)

	// list of potential 2d positions , that the roles will use.
	// (vertical ignored)
	AICombatFormationSlotDef	**eaSlots;		AST(NAME("Slot"))

} AICombatFormationRoleDef;

AUTO_STRUCT;
typedef struct AICombatFormationDef
{
	// the role & formation slots 
	AICombatFormationRoleDef **eaRoleFormations; AST(NAME("Role"))

	Vec2 vFormationCenter;						NO_AST

	// if set, the formation center will NOT be recalculated to be the barycenter of all formation slots
	U32 bDoNotRecenterFormation : 1;

} AICombatFormationDef;


AUTO_STRUCT WIKI("AICombatRolesDef");
typedef struct AICombatRolesDef
{
	// list of different roles
	AICombatRole **eaRoles;						AST(NAME("Role"))

	// formation definition for the combat fole
	AICombatFormationDef *pFormation;			AST(NAME("Formation"))

	const char* pchName;						AST(KEY, STRUCTPARAM)
	char* pchFilename;							AST(CURRENTFILE)
		
	// if true, shares the aggro with the person that is guarding us (not supported in aggro2 yet.)
	U32 bShareAggroWithGuarders : 1;

	// If set, when a guardee dies that a guarder is guarding, the guarder will try to find
	// another entity of the role to guard
	U32 bPickUpGuardeeOnDeath : 1;				AST(DEFAULT(1))

} AICombatRolesDef;


// ----------------------------------------------------------------------------------------------------------------
// start run-time data structs
// ----------------------------------------------------------------------------------------------------------------

typedef struct AICombatRoleFormationSlot
{
	// the true position in the world after offset/transformations applied
	Vec3 vWorldPos;
	
	// what entity is currently using this slot
	EntityRef erUsed;
		
	U32 bValidSlot : 1;
} AICombatRoleFormationSlot;

ParseTable parse_AICombatRolesTeamRole[];

// 
AUTO_STRUCT;
typedef struct AICombatRolesTeamRole
{
	// pooled string name of role
	const char* pchName;

	AICombatRoleFormationSlot **eaFormationSlots;	NO_AST

	// tracked damage that was shared TO this role
	F32 trackedDamageRole[AI_NOTIFY_TYPE_TRACKED_COUNT];

	// used to not add in damage more than once per message
	U32 trackedMessageFlag;
	
	// the number of entities currently using this role
	// only valid at combat setup time 
	S32 numEntitiesInRole;
	
} AICombatRolesTeamRole;

typedef struct AICRole_AssignedTarget
{
	EntityRef	erTarget;
	S32			assignedCount;
} AICRole_AssignedTarget;

typedef struct AICombatRoleLuckyCharmInfo
{
	// list of targets and how many have been assigned to them
	AICRole_AssignedTarget	**eaAssignedTargets;		
} AICRole_LuckyCharmInfo;

// per AITeam
AUTO_STRUCT;
typedef struct AICombatRolesTeamInfo
{
	// list of team roles for runtime data
	AICombatRolesTeamRole		**eaTeamRolesInfo;
	AICombatRolesTeamRole		dummyRole;
	REF_TO(AICombatRolesDef)	hCombatRolesDef;
	
	// per target type, keep track of what targets have been assigned
	AICRole_LuckyCharmInfo		aLuckyCharmInfo[kPetTargetType_COUNT];		NO_AST

	Quat						qFormationRot;
	S64							lastEnterFormationTime;
	U32							bFormationSlotsAssigned : 1;
	
} AICombatRolesTeamInfo;

// per AITeamMember 
typedef struct AICombatRoleTeamMember
{
	// how many guards are on this guy
	S32		guardCount;
	
	// positions that are the desired place to have a guard for this member
	AICombatRolesGuardSlot **eaGuardSlots;
	
	AICombatRolesTeamRole *pTeamRole;

	// used for searching for best slot for a member
	AICombatRoleFormationSlot *pClosestSlot;
	F32				fClosestSlotDist;

	// the chosen target for this member given the lucky charm assignments
	EntityRef		erLuckyCharmTarget;

	U32		bAssignedFormationSlot : 1;

} AICombatRoleTeamMember;

// per AIVars
typedef struct AICombatRoleAIVars
{
	S32			*combatRoleConfigMods;
	FSMContext* combatFSMContext;			

} AICombatRoleAIVars;


AICombatRolesDef*  aiCombatRolesDef_GetDef(const char *pchDefName);
AICombatRole* aiCombatRolesDef_FindRoleByName(AICombatRolesDef* pDef, const char *pchName);

void aiCombatRoles_Startup();
void aiCombatRole_ApplyRoleConfigMods(Entity *e, AIVarsBase* aib, const AICombatRole *pRole);

// Combat roles setup for the team
void aiCombatRole_SetupStartCombat(AITeam *pTeam);

void aiTeamMemberCombatRole_Free(AICombatRoleTeamMember* role);

void aiCombatRole_TeamSetCombatRolesDef(SA_PARAM_NN_VALID AITeam* team, SA_PARAM_OP_VALID const char *combatRolesDef);
AICombatRolesDef* aiTeamGetCombatRolesDef(SA_PARAM_NN_VALID AITeam* team);
void aiCombatRole_TeamReleaseCombatRolesDef(SA_PARAM_NN_VALID AITeam* team);


void aiCombatRole_CombatTick(AITeam *pTeam);

int aiCombatRole_RequestGuardPosition(AITeam *pTeam, Entity *e);

EntityRef aiCombatRole_RequestPreferredTarget(Entity *e);
void aiCombatRole_ClearAllPreferredTargets(SA_PARAM_NN_VALID AITeam *pTeam);

// Sets up the team's formation slot positions and dirties the member's slot positions
void aiCombatRole_SetupTeamFormation(int iPartitionIdx, AITeam *pTeam);
void aiCombatRole_CleanupFormation(AITeam *pTeam);

int aiCombatRoleFormation_RequestSlot(	SA_PARAM_NN_VALID AITeam *pTeam, 
										SA_PARAM_NN_VALID AITeamMember* pMember, 
										SA_PARAM_OP_VALID AICombatRolesDef *pDef,
										int bUseRotation);

void aiCombatRole_SetCombatRole(Entity *e, 
								AIVarsBase *aib, 
								const char *pchNewCombatRole,
								AICombatRolesDef* pCombatRoleDefFile);
void aiCombatRole_DestroyCombatRoleVars(Entity *e, AIVarsBase *aib);


void aiCombatRole_GetEntitesInRole(AITeam *pTeam, const char* pchRoleName, Entity*** eaEntsOut);
AICombatRolesDef* aiCombatRole_GetTeamRolesDef(AITeam *pTeam);



#endif