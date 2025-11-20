#pragma once
GCC_SYSTEM
/***************************************************************************
*     Copyright (c) 2006-2008, Cryptic Studios
*     All Rights Reserved
*     Confidential Property of Cryptic Studios
***************************************************************************/

#include "CharacterAttribsMinimal.h"
#include "ExpressionMinimal.h"

// Forward declarations
typedef struct AttribMod			AttribMod;
typedef struct AttribModDef			AttribModDef;
typedef struct AttribModNet			AttribModNet;
typedef struct Character			Character;
typedef struct CombatEventTracker	CombatEventTracker;
typedef struct CritterOverrideDef	CritterOverrideDef;
typedef struct Entity				Entity;
typedef struct ExprContext			ExprContext;
typedef struct Expression			Expression;
typedef struct Item					Item;
typedef struct MultiVal				MultiVal;
typedef struct PowerDef				PowerDef;
typedef struct PowerActivation		PowerActivation;
typedef struct PowerApplication		PowerApplication;
typedef struct PowerDef PowerDef_ForExpr;
typedef struct ItemDef				ItemDef;

// Enums

// Types of contexts
typedef enum CombatEvalContext
{
	kCombatEvalContext_Simple,
		// Context used to find simple AttribMod magnitudes, such as those on innate
		//  powers or the non-extending basic aspect mods on enhancements.

	kCombatEvalContext_Enhance,
		// Context used to attach enhancements

	kCombatEvalContext_Activate,
		// Context used during activation (paying costs, charge requirements, etc)

	kCombatEvalContext_Target,
		// Context used during the location of valid targets (radius, arc, etc)

	kCombatEvalContext_Apply,
		// Context used during apply (radius, magnitude, duration, health, etc)

	kCombatEvalContext_Affects,
		// Context used for affect checks (enhance apply, strength, resist, etc)

	kCombatEvalContext_Expiration,
		// Context used for expiration requirement expression

	kCombatEvalContext_EntCreateEnhancements,
		// Context used for determining if an enhancement should be applied to an entCreate

	kCombatEvalContext_Teleport,
		// Context used for determining teleport base entity

} CombatEvalContext;

// Client CombatEval Prediction
typedef enum CombatEvalPrediction
{
	kCombatEvalPrediction_None = 0,
	// No prediction made

	kCombatEvalPrediction_False = 1,
	// Client prediction of false

	kCombatEvalPrediction_True = 2,
	// Client prediction of true
} CombatEvalPrediction;

typedef enum CombatEvalMagnitudeCalculationMethod
{
	// Use the normal calculation method
	kCombatEvalMagnitudeCalculationMethod_Normal,

	// Calculate the minimum magnitude
	kCombatEvalMagnitudeCalculationMethod_Min,

	// Calculate the maximum magnitude
	kCombatEvalMagnitudeCalculationMethod_Max,

	// Calculate the average magnitude (ignores variance)
	kCombatEvalMagnitudeCalculationMethod_Average,
} CombatEvalMagnitudeCalculationMethod;


// Structure that allows systems that use CombatEval to override some of the behavior in order to do
//  irritating things like get a description of a PowerDef in a PowerTree as if you actually owned the 
//  next rank of the node, etc.
typedef struct CombatEvalOverrides
{
	U32 bEnabled : 1;
		// Enables the various overrides

	U32 bNodeRank : 1;
		// Enables the NodeRank field

	U32 bAttrib : 1;
	// Enables the attribs field

	U32 bMinDamage : 1;
		// Calculate minimum damage possible

	U32 bMaxDamage : 1;
		// Calculate maximum damage possible

	S32 iNodeRank;
		// Overrides the rank returned for the NodeRank expression function

	CharacterAttribs attribs;
		// Overrides the value returned for the Attrib expression function

	S32 iAutoDescTableAppHack;
		// Hack override for the TableApp expression function during autodesc.
		//  Automatically enabled if non-zero.  Horrible and hateful.

	S32 iAutoDescTableNodeRankHack;
		// Same deal as iAutoDescTableAppHack

	Item** ppWeapons;
		// For D&D weapon-based powers, a place to put the appropriate weapon 
		//  so we needn't bother with spoofing a poweract struct on the client.

} CombatEvalOverrides;

// Globally exposed CombatEvalOverrides
extern CombatEvalOverrides g_CombatEvalOverrides;

// Global flag to suppress errors during evaluation
extern int g_bCombatEvalSuppressErrors;

// Retrieves the given context
ExprContext *combateval_ContextGet(CombatEvalContext eContext);

// Resets the given context
void combateval_ContextReset(CombatEvalContext eContext);

// Generates an expression based on the given context, returns success
int combateval_Generate(SA_PARAM_OP_VALID Expression *pExpr, CombatEvalContext eContext);

// Setup functions to prep the contexts for evaluation

// Setup the Simple context for evaluation
void combateval_ContextSetupSimple(SA_PARAM_OP_VALID Character *pchar,
								   S32 iLevel,
								   SA_PARAM_OP_VALID  Item * pItem);

// Setup the Enhance context for evaluation
void combateval_ContextSetupEnhance(SA_PARAM_OP_VALID Character *pchar,
									SA_PARAM_NN_VALID PowerDef *pdefTarget,
									int bIsUnownedPower);

// Setup the Activate context for evaluation
//  Source and Activation shouldn't be NULL unless you like errors
void combateval_ContextSetupActivate(SA_PARAM_OP_VALID Character *pcharSource,
									 SA_PARAM_OP_VALID Character *pcharTarget,
									 SA_PARAM_OP_VALID PowerActivation *pact,
									 CombatEvalPrediction ePrediction);

// Setup the Target context for evaluation
//  Application shouldn't be NULL unless you like errors
void combateval_ContextSetupTarget(SA_PARAM_OP_VALID Character *pcharSource,
								   SA_PARAM_OP_VALID Character *pcharTarget,
								   SA_PARAM_OP_VALID PowerApplication *papp);

// Setup the Apply context for evaluation
//  Target and Application shouldn't be NULL unless you like errors
void combateval_ContextSetupApply(SA_PARAM_OP_VALID Character *pcharSource,
									SA_PARAM_OP_VALID Character *pcharTarget,
									SA_PARAM_OP_VALID Item * pItem,
								    SA_PARAM_OP_VALID PowerApplication *papp);

// Setup the Affects context for evaluation
void combateval_ContextSetupAffects(SA_PARAM_OP_VALID Character *pchar,
									SA_PARAM_OP_VALID AttribMod *pmodAffects,
									SA_PARAM_OP_VALID PowerDef *ppowdefTarget,
									SA_PARAM_OP_VALID AttribModDef *pmoddefTarget,
									SA_PARAM_OP_VALID AttribMod *pmodTarget,
									SA_PARAM_OP_VALID CombatEventTracker *pTriggerEvent);

// Setup the Expiration context for evaluation
void combateval_ContextSetupExpiration(Character *pchar,
									   AttribMod *pmod,
									   AttribModDef *pmoddef,
									   PowerDef *ppowdef);

void combateval_ContextSetEnhancementSourceItem(Item *pSourceItem);

// Setup the EnhancementAttach context for evaluation
void combateval_ContextSetupEntCreateEnhancements(	Character *powner,
													PowerDef *ppowDef,
													Character *pcreatedChar);

// Setup the EnhancementAttach context for evaluation
void combateval_ContextSetupTeleport(	Character *pchar,
										Character *ptarget);

// Evaluate an expression, returns the result as a float
F32 combateval_EvalNew(int iPartitionIdx,
					   SA_PARAM_NN_VALID Expression *pExpr,
					   CombatEvalContext eContext,
					   SA_PARAM_OP_VALID char **ppchErrorOut);

// evaluate an expression that must return an entity*
Entity* combateval_EvalReturnEntity(int iPartitionIdx, 
									Expression *pExpr,
									CombatEvalContext eContext,
									char **ppchErrorOut);

// Initialize the combateval contexts
void combateval_Init(S32 bInit);

// Utility lookup functions
CombatEvalPrediction combateval_GetPrediction(SA_PARAM_NN_VALID ExprContext *pContext);

// Normalizes attribs for STO so that auto descriptions using them can display correctly
void STONormalizeAttribs(Entity *pEnt);

// Determines whether a character is affected by a power (exposed for AI usage)
int AffectedByPower(SA_PARAM_OP_VALID Character *character,
					ACMD_EXPR_DICT(PowerDef) const char* powerDefName);


int HasPowerCat(SA_PARAM_OP_VALID PowerDef_ForExpr *powerDef, ACMD_EXPR_ENUM(PowerCategory) const char *categoryName);

// Character
//  Inputs: Character, PowerDef name, Attrib name
//  Return: 1 if the Character has an AttribMod on them from the named PowerDef that is of the specific Attrib, otherwise 0
int AffectedByPowerAttrib(SA_PARAM_OP_VALID Character *character,
						  ACMD_EXPR_DICT(PowerDef) const char* powerDefName,
						  ACMD_EXPR_ENUM(AttribType) const char *attribName);

// Character
//  Inputs: Character, PowerDef name, Attrib name, Entity attribOwner
//  Return: 1 if the Character has an AttribMod on them that is owned by the given entity, from the named PowerDef that is of the specific Attrib, otherwise 0
int AffectedPowerAttribFromOwner(SA_PARAM_OP_VALID Character *character,
								 ACMD_EXPR_DICT(PowerDef) const char* powerDefName,
								 ACMD_EXPR_ENUM(AttribType) const char *attribName,
								 SA_PARAM_NN_VALID Entity *eAttribOwner);

// Special version of AttribModMagnitudePct which filters ModNets by arc/yaw coverage vs
//  a specific input angle.  Used to find directional Shields.
AttribModNet* ShieldPctOrientedAttrib(SA_PARAM_OP_VALID Character *pChar, F32 fAngle);
F32 ShieldPct(SA_PARAM_OP_VALID Character *pChar, F32 fAngle);

// Returns the damage for a single weapon
F32 ItemWeaponDamageFromItemDef(int iPartitionIdx, SA_PARAM_OP_VALID PowerApplication *papp, SA_PARAM_OP_VALID ItemDef *pItemDef, CombatEvalMagnitudeCalculationMethod eCalcMethod);