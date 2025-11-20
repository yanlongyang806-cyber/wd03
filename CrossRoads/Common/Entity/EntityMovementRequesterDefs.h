#pragma once

#include "referencesystem.h"

typedef struct Entity Entity;
typedef struct CharacterClass CharacterClass;
typedef struct TacticalRequesterAimDef TacticalRequesterAimDef;
typedef struct TacticalRequesterRollDef TacticalRequesterRollDef;
typedef struct TacticalRequesterSprintDef TacticalRequesterSprintDef;

AUTO_ENUM;
typedef enum MovementRequesterType
{
	MovementRequesterType_DEFAULTSURFACE,
	
	MovementRequesterType_DRAGON

} MovementRequesterType;

extern StaticDefineInt MovementRequesterTypeEnum[];

// Parent definition of AttribModDef parameters
AUTO_STRUCT;
typedef struct MovementRequesterParams
{
	MovementRequesterType eType;				AST(SUBTABLE(MovementRequesterTypeEnum), POLYPARENTTYPE)
		
} MovementRequesterParams;

AUTO_STRUCT;
typedef struct DragonMovementDef
{
	MovementRequesterParams params;				AST(POLYCHILDTYPE(MovementRequesterType_DRAGON))

	// the average turn rate, degrees per second
	F32		fTurnRate;							AST(DEFAULT(120.f))

	// the max rate of turning in degrees per second
	F32		fMaxTurnRate;						AST(DEFAULT(180.f))

	// the min turn rate in degrees per second
	F32		fMinTurnRate;						AST(DEFAULT(30.f))

	// for damping the turn rate
	F32		fTurnRateBasis;						AST(DEFAULT(18.f)) // ~ONEOVERPI

	// the facing angle to the desired location before forward movement is allowed
	F32		fMovementFacingAngleThreshold;		AST(DEFAULT(30.f))

	// The allowed facing angle before the dragon tries to reorient to target facing
	F32		fReadjustFacingAngleThreshold;		AST(DEFAULT(30.f))

	// the average turn rate of the head
	F32		fHeadTurnRate;						AST(DEFAULT(120.f))

	// for damping the turn rate
	F32		fHeadTurnBasis;						AST(DEFAULT(30.f))
	
	// the min turn rate in degrees per second
	F32		fHeadTurnRateMin;					AST(DEFAULT(30.f))
	
	// the max rate of turning in degrees per second
	F32		fHeadTurnRateMax;					AST(DEFAULT(180.f))

	// the max degree difference the head can be from the forward facing
	F32		fHeadVsRootMaxDelta;				AST(DEFAULT(30.f))

	// the max degree difference the head can be from the forward facing when overridden by powers
	F32		fHeadVsRootMaxDeltaOnOverride;		AST(DEFAULT(60.f))

} DragonMovementDef;

// all rates should be defined in degrees per second. 
// The requester will transform them to radians when the parameters are set
AUTO_STRUCT;
typedef struct SurfaceMovementTurnDef
{
	F32		fFaceMinTurnRate;
	
	F32		fFaceMaxTurnRate;
	
	F32		fFaceTurnRate;


} SurfaceMovementTurnDef;

AUTO_STRUCT;
typedef struct SurfaceMovementDef
{
	MovementRequesterParams params;				AST(POLYCHILDTYPE(MovementRequesterType_DEFAULTSURFACE))

	SurfaceMovementTurnDef	*pTurn;				AST(NAME("turn"))

} SurfaceMovementDef;

// Parent definition of AttribModDef parameters
AUTO_STRUCT;
typedef struct MovementRequesterDef
{	
	char* pchName;								AST(STRUCTPARAM KEY POOL_STRING)		

	char* pchFilename;							AST(CURRENTFILE)

	MovementRequesterParams	*pParams;

} MovementRequesterDef;


extern DictionaryHandle g_hMovementRequesterDefDict;

// RP: the TacticalSprintDef & TacticalSprintDef need to go in the combatConfig.h file for now because AST(EMBEDDED_FLAT)
// did not like the struct being in a separate .h for some reason.
// I plan on doing a CombatConfig pass and updating the projects data files soon(tm)
void mrFixupTacticalRollDef(TacticalRequesterRollDef *pDef);
void mrFixupTacticalSprintDef(TacticalRequesterSprintDef *pDef);

// if the CharacterClass is supplied, will use that, otherwise will get the CharacterClass from the given entity
// otherwise gets the def from the combatConfig
TacticalRequesterRollDef* mrRequesterDef_GetRollDefForEntity(SA_PARAM_OP_VALID Entity *e, SA_PARAM_OP_VALID CharacterClass *pClass);
TacticalRequesterSprintDef* mrRequesterDef_GetSprintDefForEntity(SA_PARAM_OP_VALID Entity *e, SA_PARAM_OP_VALID CharacterClass *pClass);
TacticalRequesterAimDef* mrRequesterDef_GetAimDefForEntity(SA_PARAM_OP_VALID Entity *e, SA_PARAM_OP_VALID CharacterClass *pClass);
