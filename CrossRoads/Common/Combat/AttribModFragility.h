#pragma once
GCC_SYSTEM
/***************************************************************************
*     Copyright (c) 2008, Cryptic Studios
*     All Rights Reserved
*     Confidential Property of Cryptic Studios
***************************************************************************/

// AttribMods can be fragile - that is, they can have their own pool of health.
//  The health pool can be damaged or healed, which can translate into changes in the
//  magnitude and/or duration of the AttribMod in some analog manner.  This code
//  manages all the relevant data loading, validation and bookkeeping.

// Please note that the special Shield attribute uses the fragility data structures
//  for customization and bookkeeping, but currently uses custom code to actually do
//  all the work.  Therefore, if you add new systems/fields to fragility, consider
//  updating the Shield code to use them.

#include "CombatPool.h"	// For CombatPool structure
#include "referencesystem.h" // For REF_TO
#include "structDefines.h" // For StaticDefineInt
#include "AttribMod.h" // For PowerTagsStruct structure

#include "CharacterAttribsMinimal.h" // For enums

// Forward declarations
typedef struct	AttribMod			AttribMod;
typedef struct	AttribModDef		AttribModDef;
typedef struct	Character			Character;
typedef struct	CharacterAttribs	CharacterAttribs;
typedef struct	Expression			Expression;
typedef struct	PowerDef			PowerDef;
extern StaticDefineInt PowerTagsEnum[];

// Defines the scale applied to a specific attrib
AUTO_STRUCT;
typedef struct FragileScale
{
	AttribType offAttrib;			AST(STRUCTPARAM, SUBTABLE(AttribTypeEnum))
		// The attrib the scale is used for

	F32 fScale;						AST(STRUCTPARAM)
		// The scale applied

} FragileScale;

// TODO(JW): Fragility: Remove
AUTO_ENUM;
typedef enum FragileTargetType
{
	kFragileTarget_TargetOnly = 1 << 0,
	kFragileTarget_SourceOnly = 1 << 1,
}FragileTargetType;

// Definition of a set of scales for fragile AttribMods
AUTO_STRUCT;
typedef struct FragileScaleSet
{
	char *pchName;						AST(STRUCTPARAM, KEY)
		// Internal name of the set

	F32 fScaleDefault;					AST(NAME(DefaultScale))
		// Scale applied to everything initially

	FragileScale **ppScales;			AST(NAME(Scale))
		// Ordered EArray of per-attrib fragile scales

	CharacterAttribs *pattrScale;		AST(NO_TEXT_SAVE)
		// CharacterAttribs struct to track compiled scales

	int eTargetType;					AST(NAME(TargetType), FLAGS, SUBTABLE(FragileTargetTypeEnum))
		// TODO(JW): Fragility: Remove

	int eIgnoreTags;					AST(NAME(IgnoreTags), FLAGS, SUBTABLE(PowerTagsEnum))
		// TODO(JW): Fragility: Remove

} FragileScaleSet;



// Defines how an AttribModDef is fragile
AUTO_STRUCT;
typedef struct ModDefFragility
{
	Expression *pExprHealth;			AST(NAME(ExprBlockHealth), LATEBIND)
		// Expression that defines the max health

	const char *cpchTableHealth;		AST(NAME(TableHealth), POOL_STRING)
		// If not null, this provides a table lookup that is multiplied as a final step for
		//  calculating max health

	CombatPoolDef pool;					AST(NAME(Pool))
		// The rules for the health of the AttribMod

	F32 fProportion;
		// Defines the relationship health has on the duration and/or magnitude.  1.0 means
		//  directly proportional: losing 50% health would cost the AttribMod 50% of its
		//  magnitude or duration.  -0.5 would mean losing 50% health would cause the
		//  AttribMod to gain 25% magnitude or duration.

	REF_TO(FragileScaleSet) hScaleIn;	AST(NAME(ScaleIn))
		// Optional scale on incoming effects before being applied to the AttribMod's health.

	REF_TO(FragileScaleSet) hScaleOut;	AST(NAME(ScaleOut))
		// Optional scale on outgoing effects before being applied to the AttribMod's health.


	PowerTagsStruct tagsExclude;		AST(NAME(TagsExclude))
		// Exclude specific effects from AttribMods with a matching PowerTag
		//  TODO(JW): Fragility: If this gets more complex we should just make an expression.


	U32 bMagnitudeIsHealth : 1;
		// If true, the AttribMod's magnitude is used to define its max health, instead
		//  of ExprHealth and TableHealth.

	U32 bSourceOnlyIn : 1;
		// If true, any incoming effects must come from the source of the AttribMod in order
		//  to affect the AttribMod's health

	U32 bSourceOnlyOut : 1;
		// If true, any outgoing effects must target the source of the AttribMod in order
		//  to affect the AttribMod's health
	
	U32 bUseResistIn : 1;
		// If true, any incoming effects are resisted by the target Character before
		//  being applied to the AttribMod's health

	U32 bUseResistOut : 1;
		// If true, any outgoing effects are resisted by the target Character before
		//  being applied to the AttribMod's health

	U32 bFragileWhileDelayed : 1;
		// If true, the AttribMod can have its health changed even while delayed
		//  (but after it's passed the source check)

	U32 bFragileToSameApply : 1;
		// If true, the AttribMod can have its health changed by other AttribMods with
		//  the same ApplyID. That means the AttribMod can be damaged by the power
		//  (or power chain) that created it.

	U32 bUnkillable : 1;
		// If true, the AttribMod is not expired (duration set to zero) when its health
		//  reaches zero.  The only time this should be true is when the AttribMod can be
		//  healed or regenerated and the intent is to allow it to 'rise from the dead'.

	U32 bReplaceKeepsHealth : 1;
		// If true, when the AttribMod is replaced with another instance of the same
		//  AttribModDef, the replacement keeps the health percentage of the AttribMod
		//  being replaced.

} ModDefFragility;


// Tracks data relevant to an instance of a fragile AttribMod
AUTO_STRUCT AST_CONTAINER;
typedef struct ModFragility
{
	F32 fHealth;			AST(PERSIST, NO_TRANSACT)
		// Current health

	F32 fHealthMax;			AST(PERSIST, NO_TRANSACT)
		// Original health after any AttribModFragilityHealth mods.  Used to clamp fHealth.

	F32 fHealthOriginal;	AST(PERSIST, NO_TRANSACT)
		// Original health

} ModFragility;

// Directly applies the magnitude to the fragile AttribMod's health, with the appropriate side-effect.
//  This should almost never be called directly.
void mod_FragileAffect(SA_PARAM_NN_VALID AttribMod *pmod, F32 fMagnitude);

// Returns the scale applied to damage of a particular type for a fragile AttribMod
F32 character_FragileModScale(int iPartitionIdx, SA_PARAM_NN_VALID Character *pchar, SA_PARAM_NN_VALID AttribMod *pmod, AttribType eDamageType, int bIncoming);

// Applies incoming or outgoing damage to the character's list of fragile AttribMods
void character_FragileModsDamage(int iPartitionIdx, SA_PARAM_NN_VALID Character *pchar, int bIncoming);

// Updates the max health, clamps and expires all the AttribMods in the the earray (which should be fragile).
void character_FragileModsFinalizeHealth(int iPartitionIdx, SA_PARAM_NN_VALID Character *pchar, SA_PARAM_OP_VALID AttribMod **ppmods);

// Validates an AttribModDef's fragility data, returns true if valid
int moddef_ValidateFragility(SA_PARAM_NN_VALID AttribModDef *pmoddef, SA_PARAM_NN_VALID PowerDef *ppowdef);

// Loads the FragileScaleSet def file
void fragileScaleSets_Load(void);

