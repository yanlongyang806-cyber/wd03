#pragma once
GCC_SYSTEM

typedef struct Entity Entity;
typedef struct StashTableImp* StashTable;

AUTO_STRUCT;
typedef struct VisionModeEffectDef
{
	// the rank of the critter
	const char *pchCritterRank;					AST(NAME("CritterRank") POOL_STRING)
	
	// the effect to attach to the entity
	const char *pchEffect;						AST(NAME("Effect") POOL_STRING)
		
} VisionModeEffectDef;

AUTO_STRUCT;
typedef struct VisionModeEffectsDef
{
	// the mode on the player that will trigger the perception
	const char *pchPlayerMode;					AST(NAME("PlayerMode") POOL_STRING)
	
	// list of entity effects
	VisionModeEffectDef **eaEntityEffects;		AST(NAME("EntityEffect"))
			
	U32		bDoDesaturationEffect : 1;			AST(NAME("DoDesaturationEffect"))
	F32		fDesaturationBlendTimeIn;			AST(NAME("DesaturationBlendTimeIn"))
	F32		fDesaturationBlendTimeOut;			AST(NAME("DesaturationBlendTimeOut"))

	const char *pchDeathEffectMessage;			AST(NAME("DeathEffectMessage") POOL_STRING)

	AST_STOP
	S32			iPlayerMode;

	// currently maps a pooled string rank to EntityPerceptionEffectDef
	StashTable  perceptEffectMap;
	
} VisionModeEffectsDef;


void gclVisionModeEffects_OncePerFrame(F32 fDTime);
void gclVisionModeEffects_UpdateEntity(Entity *e);
void gclVisionModeEffects_InitEntity(Entity *e);
