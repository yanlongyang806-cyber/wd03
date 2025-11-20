#pragma once

typedef struct Entity Entity;
typedef struct GameInteractLocation GameInteractLocation;
typedef struct GameInteractable GameInteractable;
typedef struct FSMLDCombatJob FSMLDCombatJob;
typedef struct ExprContext ExprContext;
typedef struct AIVarsBase AIVarsBase;

AST_PREFIX(WIKI(AUTO))
AUTO_STRUCT WIKI("AICombatJob");
typedef struct AICombatJob
{
	const char* pchFileName;			AST( CURRENTFILE )

	char* pchName;						AST(KEY POOL_STRING STRUCTPARAM)

	// How far a critter will look for a combat job
	F32 fCombatJobAwarenessRadius;		AST(NAME("CombatJobAwarenessRadius") DEFAULT(80.0))

	// The average cooldown on the job. Gets set once all the job locations have been filled on a combat job
	F32 fCombatJobCooldown;				AST(DEFAULT(10.f))

	// The average cooldown on the actual location
	F32 fCombatJobLocationCooldown;			AST(DEFAULT(10.f))

} AICombatJob;

// If possible assigns a combat job to the given critter
bool aiCombatJob_AssignToCombatJob(SA_PARAM_NN_VALID Entity *e, SA_PARAM_NN_VALID FSMLDCombatJob *pCombatJobData, bool bInitialJobsOnly);

// Runs the assigned combat job for the given critter
void aiCombatJob_RunJob(SA_PARAM_NN_VALID Entity* e, SA_PARAM_NN_VALID ExprContext* context, int doCombatMovement, SA_PARAM_NN_VALID FSMLDCombatJob *pCombatJobData, char **errString);

// Handles death
void aiCombatJob_OnDeathCleanup(SA_PARAM_NN_VALID Entity *e, SA_PARAM_NN_VALID AIVarsBase *aib);

// Handles exiting team fight state
void aiCombatJob_TeamExitFightState(SA_PARAM_NN_VALID Entity *e, SA_PARAM_NN_VALID AIVarsBase *aib);

// Handles the case where the critter exits an ambient job
void aiCombatJob_OnAmbientJobExit(SA_PARAM_NN_VALID Entity *e, SA_PARAM_NN_VALID AIVarsBase *aib);

// Assigns a specific job location to the entity
void aiCombatJob_AssignLocation(SA_PARAM_NN_VALID Entity *e, SA_PARAM_NN_VALID GameInteractable *pTargetJobInteractable, SA_PARAM_NN_VALID GameInteractLocation *pTargetLocation, bool bIssueMovementOrder);

extern ParseTable parse_AICombatJob[];