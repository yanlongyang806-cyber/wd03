#ifndef AITEAM_H
#define AITEAM_H

#include "StashTable.h"

typedef struct AICombatRolesDef		AICombatRolesDef;
typedef struct AIJob				AIJob;
typedef struct AIJobDesc			AIJobDesc;
typedef struct AITeam				AITeam;
typedef struct AITeamMember			AITeamMember;
typedef struct AITeamMemberAssignment AITeamMemberAssignment;
typedef struct AIStatusTableEntry	AIStatusTableEntry;
typedef struct AITeamStatusEntry	AITeamStatusEntry;
typedef struct AIVarsBase			AIVarsBase;
typedef struct AttribMod			AttribMod;
typedef struct AttribModDef			AttribModDef;
typedef struct Entity				Entity;
typedef struct Team					Team;

SA_RET_NN_VALID AITeam* aiTeamCreate(int partitionIdx, SA_PARAM_OP_VALID Entity* teamOwner, int combat);
void aiTeamDestroy(SA_PRE_NN_VALID SA_POST_P_FREE AITeam* team);
void aiTeamUpdatePlayerMembership(Entity * pEnt);

S32 aiTeamGetMemberCount(AITeam *team);

// Returns the combat team for the critter - which defaults to the normal team if the critter
//  does not have a combat team yet or combat teams are disabled.  Use this when in
//  doubt, since the creation of a combat team will transfer settings over.
AITeam* aiTeamGetCombatTeam(Entity* e, AIVarsBase* aib);
AITeam* aiTeamGetAmbientTeam(Entity* e, AIVarsBase* aib);

// Returns the AITeamMember on the given AIVarsBase for the given AITeam
AITeamMember* aiTeamGetMember(Entity* e, AIVarsBase* aib, AITeam* team);

// Given the entity, return the appropriate AITeamMember depending if we are on a combat team or not 
AITeamMember* aiGetTeamMember(Entity* e, AIVarsBase* aib);

// Same as above, with TeamStatus
AITeamStatusEntry* aiGetTeamStatus(Entity* e, AIVarsBase* aib, AIStatusTableEntry *status);

void aiTeamUpdate(SA_PARAM_NN_VALID AITeam* team);
void aiTeamPerTickUpdate(AITeam* team);

SA_ORET_NN_VALID AITeamMember* aiTeamAdd(SA_PARAM_NN_VALID AITeam* team, SA_PARAM_NN_VALID Entity* e);

// The old team will be deleted if this is the last member of the team
void aiTeamRemove(SA_PARAM_NN_VALID AITeam** team, SA_PARAM_NN_VALID Entity* e);
void aiTeamRescanSettings(SA_PARAM_NN_VALID AITeam* team);
void aiTeamSetRoamingLeash(SA_PARAM_NN_VALID AITeam* team, int on);
void aiTeamCalculateSpawnPos(SA_PARAM_NN_VALID AITeam* team);
void aiTeamCopyTeamSettingsToCombatTeam(AITeam* team, AITeam *combatTeam);

// gives the position the team should be leashing to, either it's spawn position or roaming leash
void aiTeamGetLeashPosition(const AITeam *team, Vec3 vOut);
void aiTeamGetAveragePosition(const AITeam *team, Vec3 vPos);

void aiTeamSetAssignedTarget(AITeam *team, AITeamMember *member, AITeamStatusEntry *status, U32 entRef);

void aiTeamAddJobs(SA_PARAM_NN_VALID AITeam* team, AIJobDesc** jobs, const char *filename);
void aiTeamClearJobs(SA_PARAM_NN_VALID AITeam* team, AIJobDesc** jobDescs);


int aiTeamActionGetCount(SA_PARAM_NN_VALID AITeam* team, SA_PARAM_NN_STR const char* actionDesc);
void aiTeamActionIncrementCount(SA_PARAM_NN_VALID AITeam* team, SA_PARAM_NN_STR const char* actionDesc);
void aiTeamActionResetCount(SA_PARAM_NN_VALID AITeam* team, SA_PARAM_NN_STR const char* actionDesc);

SA_RET_OP_VALID AITeamStatusEntry* aiTeamStatusFind(SA_PARAM_NN_VALID AITeam* team, SA_PARAM_NN_VALID Entity* target, int create, int legalTarget);
void aiTeamStatusRemove(SA_PARAM_NN_VALID AITeam* team, SA_PARAM_NN_VALID Entity* target, const char* reason);

void aiTeamLeash(SA_PARAM_NN_VALID AITeam* team);
int aiTeamInCombat(SA_PARAM_NN_VALID AITeam* team);
void aiTeamEnterCombat(SA_PARAM_NN_VALID AITeam* team);
void aiTeamTriggerStaredown(SA_PARAM_NN_VALID AITeam* team);
void aiTeamRequestReinforcements(SA_PARAM_NN_VALID Entity* e, SA_PARAM_NN_VALID AIVarsBase* aib, SA_PARAM_NN_VALID AITeam* sourceTeam, SA_PARAM_NN_VALID AITeam* targetTeam);

void aiTeamSetReinforceTarget(SA_PARAM_NN_VALID Entity* e, SA_PARAM_NN_VALID AIVarsBase* aib, SA_PARAM_NN_VALID AITeam* team, SA_PARAM_NN_VALID AITeam* reinforceTeam, SA_PARAM_NN_VALID Entity* reinforceTarget);
void aiTeamClearReinforceTarget(SA_PARAM_OP_VALID Entity* e, SA_PARAM_OP_VALID AIVarsBase* aib, SA_PARAM_NN_VALID AITeam* team, SA_PARAM_OP_VALID AITeam* reinforceTeam, int clearReinforcements, int resetReinforce);

int aiTeamTargetWithinLeash(SA_PARAM_OP_VALID AITeamMember* member, SA_PARAM_NN_VALID AITeam* team, SA_PARAM_NN_VALID Entity* target, SA_PARAM_OP_VALID F32* dist);
void aiTeamAddLegalTarget(SA_PARAM_NN_VALID AITeam* team, SA_PARAM_NN_VALID Entity* target);

int aiTeamIsTargetLegalTarget(SA_PARAM_NN_VALID AITeam* team, SA_PARAM_NN_VALID Entity *target);

Entity* aiTeamGetLeader(AITeam *team);

void aiTeamRequestResurrectForMember(AITeam *team, Entity *e);
AITeamMemberAssignment* aiTeamGetAssignmentForMember(SA_PARAM_NN_VALID AITeam* team, Entity* eHealer);
AITeamMember* aiTeamFindMemberByEntity(SA_PARAM_NN_VALID AITeam* team, Entity* e);

void aiTeamNotifyNewAttribMod(SA_PARAM_NN_VALID AITeam* team, SA_PARAM_NN_VALID Entity* e, SA_PARAM_NN_VALID AttribMod* mod, SA_PARAM_NN_VALID AttribModDef* def);

// Clears the status table for the team
void aiTeamClearStatusTable(AITeam* team, const char* reason);

#endif
