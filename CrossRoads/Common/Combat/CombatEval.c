/***************************************************************************
*     Copyright (c) 2006-2008, Cryptic Studios
*     All Rights Reserved
*     Confidential Property of Cryptic Studios
***************************************************************************/
#include "CombatEval.h"

#include "Character.h"
#include "entCritter.h"
#include "entEnums_h_ast.h"
#include "CostumeCommonEntity.h"
#include "CostumeCommonTailor.h"
#include "EntityLib.h"
#include "Expression.h"
#include "InteractionManager_common.h"
#include "Player.h"
#include "StringCache.h"
#include "TriCube/vec.h"
#include "wlInteraction.h"
#include "ItemCommon.h"
#include "itemCommon_h_ast.h"
#include "ItemEnums.h"
#include "itemEnums_h_ast.h"
#include "inventoryCommon.h"

#include "AttribMod_h_ast.h"
#include "AttribModFragility.h"
#include "Character_h_ast.h"
#include "CharacterAttribs.h"
#include "CharacterClass.h"
#include "Character_target.h"
#include "Combat_DD.h"
#include "CombatEvents_h_ast.h"
#include "GameAccountDataCommon.h"
#include "PowersAutoDesc.h"
#include "PowerModes.h"
#include "PowerSlots.h"
#include "PowerTree.h"
#include "PowerTreeHelpers.h"
#include "PowerVars.h"
#include "PowerActivation.h"
#include "PowerActivation_h_ast.h"
#include "PowerApplication.h"
#include "PowerApplication_h_ast.h"
#include "EString.h"
#include "file.h"
#include "AbilityScores_DD.h"
#include "Guild.h"
#include "GroupProjectCommon.h"
#include "Login2Common.h"

#if GAMESERVER || GAMECLIENT
	#include "EntityMovementManager.h"
	#include "EntityMovementDefault.h"
	#include "PowersMovement.h"
#endif

#if GAMESERVER
	#include "gslActivity.h"
#endif
#if GAMECLIENT
	#include "ClientTargeting.h"
	#include "LoginCommon.h"
#endif

AUTO_RUN_ANON(memBudgetAddMapping(__FILE__, BUDGET_GameSystems););

extern Login2CharacterCreationData *g_CharacterCreationData;

// Globally exposed CombatEvalOverrides
CombatEvalOverrides g_CombatEvalOverrides = {0};

// Global flag to suppress errors during evaluation
int g_bCombatEvalSuppressErrors = false;

// Defining the static contexts
static ExprContext *s_pContextSimple = NULL;
static ExprContext *s_pContextEnhance = NULL;
static ExprContext *s_pContextActivate = NULL;
static ExprContext *s_pContextTarget = NULL;
static ExprContext *s_pContextApply = NULL;
static ExprContext *s_pContextAffects = NULL;
static ExprContext *s_pContextExpiration = NULL;
static ExprContext *s_pContextEntCreateEnhancements = NULL;
static ExprContext *s_pContextTeleport = NULL;

// Static PowerApplication for the Simple context
static PowerApplication *s_pPowerApplicationSimple = NULL;

typedef struct PowerDef PowerDef_ForExpr;
typedef struct AttribMod AttribMod_ForExpr;
typedef struct AttribModDef AttribModDef_ForExpr;


// Static pointers to pooled strings and their expression handles
//  StaticVars
#define CESTATICDEFINE(name) static const char *s_pch ## name = NULL; static int s_h ## name = 0;
CESTATICDEFINE(Activation)
CESTATICDEFINE(Application)
CESTATICDEFINE(Forever)
CESTATICDEFINE(Mod)
CESTATICDEFINE(ModAffects)
CESTATICDEFINE(ModDef)
CESTATICDEFINE(PowerDef)
CESTATICDEFINE(Prediction)
CESTATICDEFINE(Self)
CESTATICDEFINE(Source)
CESTATICDEFINE(SourceItem)
CESTATICDEFINE(Target)
CESTATICDEFINE(TriggerEvent)
CESTATICDEFINE(SourceOwner)
CESTATICDEFINE(IsUnownedPower)

// Initialize all the pooled strings
AUTO_RUN;
void ContextInitStrings(void)
{
	s_pchActivation = allocAddStaticString("Activation");
	s_pchApplication = allocAddStaticString("Application");
	s_pchForever = allocAddStaticString("Forever");
	s_pchMod = allocAddStaticString("Mod");
	s_pchModAffects = allocAddStaticString("ModAffects");
	s_pchModDef = allocAddStaticString("ModDef");
	s_pchPowerDef = allocAddStaticString("PowerDef");
	s_pchPrediction = allocAddStaticString("Prediction");
	s_pchSelf = allocAddStaticString("Self");
	s_pchSource = allocAddStaticString("Source");
	s_pchSourceItem = allocAddStaticString("SourceItem");
	s_pchTarget = allocAddStaticString("Target");
	s_pchTriggerEvent = allocAddStaticString("TriggerEvent");
	s_pchSourceOwner = allocAddStaticString("SourceOwner");
	s_pchIsUnownedPower = allocAddStaticString("IsUnownedPower");
}

// Initialization
static void ContextInitSimple(S32 bInit)
{
	devassert(bInit==!s_pContextSimple);

	if(bInit)
	{
		ExprFuncTable* stTable;

		s_pContextSimple = exprContextCreate();
		exprContextSetFloatVarPooledCached(s_pContextSimple,s_pchForever,POWERS_FOREVER,&s_hForever);
		exprContextSetAllowRuntimePartition(s_pContextSimple);

		s_pPowerApplicationSimple = StructCreate(parse_PowerApplication);

		// Functions
		//  Generic, Self, Character, ApplicationSimple
		stTable = exprContextCreateFunctionTable();
		exprContextAddFuncsToTableByTag(stTable, "util");
		exprContextAddFuncsToTableByTag(stTable, "CEFuncsGeneric");
		exprContextAddFuncsToTableByTag(stTable, "CEFuncsSelf");
		exprContextAddFuncsToTableByTag(stTable, "CEFuncsCharacter");
		exprContextAddFuncsToTableByTag(stTable, "CEFuncsApplicationSimple");
		exprContextSetFuncTable(s_pContextSimple, stTable);
	}

	// Data
	//  SelfPtr - the source Entity
	//  Source - the source Character (not Obj-pathable)
	//  SourceOwner - to mirror the Apply context. the source Character (not Obj-pathable)
	//  Application - the fake Application (not var-able or Obj-pathable)
	//  SourceItem - the immediate source item of the enhancement power on which expressions are being evaluated
	exprContextSetSelfPtr(s_pContextSimple,NULL);
	exprContextSetPointerVarPooledCached(s_pContextSimple,s_pchSource,NULL,parse_Character,true,false,&s_hSource);
	exprContextSetPointerVarPooledCached(s_pContextSimple,s_pchSourceOwner,NULL,parse_Character,true,true,&s_hSourceOwner);
	exprContextSetPointerVarPooledCached(s_pContextSimple,s_pchApplication,s_pPowerApplicationSimple,parse_PowerApplication,false,false,&s_hApplication);
	exprContextSetPointerVarPooledCached(s_pContextSimple,s_pchSourceItem, NULL, parse_Item,true,false,&s_hSourceItem);

}

static void ContextInitEnhance(S32 bInit)
{
	devassert(bInit==!s_pContextEnhance);

	if(bInit)
	{
		ExprFuncTable* stTable;

		s_pContextEnhance = exprContextCreate();
		exprContextSetFloatVarPooledCached(s_pContextEnhance,s_pchForever,POWERS_FOREVER,&s_hForever);

		// Functions
		//  Generic, PowerDef
		stTable = exprContextCreateFunctionTable();
		exprContextAddFuncsToTableByTag(stTable, "CEFuncsGeneric");
		exprContextAddFuncsToTableByTag(stTable, "CEFuncsPowerDef");
		exprContextSetFuncTable(s_pContextEnhance, stTable);
	}

	// Data
	//  PowerDef - the PowerDef of the Power that the Enhancement is trying to hook to
	exprContextSetPointerVarPooledCached(s_pContextEnhance,s_pchPowerDef,NULL,parse_PowerDef_ForExpr,true,true,&s_hPowerDef);
	exprContextSetIntVarPooledCached(s_pContextEnhance,s_pchIsUnownedPower,0,&s_hIsUnownedPower);

}

static void ContextInitActivate(S32 bInit)
{
	devassert(bInit==!s_pContextActivate);

	if(bInit)
	{
		ExprFuncTable* stTable;

		s_pContextActivate = exprContextCreate();
		exprContextSetFloatVarPooledCached(s_pContextActivate,s_pchForever,POWERS_FOREVER,&s_hForever);
		exprContextSetAllowRuntimePartition(s_pContextActivate);

		// Functions
		//  Generic, Self, Character
		stTable = exprContextCreateFunctionTable();
		exprContextAddFuncsToTableByTag(stTable, "util");
		exprContextAddFuncsToTableByTag(stTable, "gameutil");
		exprContextAddFuncsToTableByTag(stTable, "CEFuncsGeneric");
		exprContextAddFuncsToTableByTag(stTable, "CEFuncsSelf");
		exprContextAddFuncsToTableByTag(stTable, "CEFuncsCharacter");
		exprContextAddFuncsToTableByTag(stTable, "CEFuncsActivation");
		exprContextSetFuncTable(s_pContextActivate, stTable);
	}

	// Data
	//  SelfPtr - the source Entity
	//  Source, Target - the source and main target Characters for the activation (Target is OPT)
	//  Activation - the Activation
	//  Prediction - the client's predicted outcome - used internally to fudge (could just be made static)
	exprContextSetSelfPtr(s_pContextActivate,NULL);
	exprContextSetPointerVarPooledCached(s_pContextActivate,s_pchSource,NULL,parse_Character,true,true,&s_hSource);
	exprContextSetPointerVarPooledCached(s_pContextActivate,s_pchTarget,NULL,parse_Character,true,true,&s_hTarget);
	exprContextSetPointerVarPooledCached(s_pContextActivate,s_pchActivation,NULL,parse_PowerActivation, false, true,&s_hActivation);
	exprContextSetIntVarPooledCached(s_pContextActivate,s_pchPrediction,kCombatEvalPrediction_None,&s_hPrediction);
}

static void ContextInitTarget(S32 bInit)
{
	devassert(bInit==!s_pContextTarget);

	if(bInit)
	{
		ExprFuncTable* stTable;

		s_pContextTarget = exprContextCreate();
		exprContextSetFloatVarPooledCached(s_pContextTarget,s_pchForever,POWERS_FOREVER,&s_hForever);
		exprContextSetAllowRuntimePartition(s_pContextTarget);

		// Functions
		//  Generic, Self, Character
		stTable = exprContextCreateFunctionTable();
		exprContextAddFuncsToTableByTag(stTable, "util");
		exprContextAddFuncsToTableByTag(stTable, "CEFuncsGeneric");
		exprContextAddFuncsToTableByTag(stTable, "CEFuncsSelf");
		exprContextAddFuncsToTableByTag(stTable, "CEFuncsCharacter");
		exprContextAddFuncsToTableByTag(stTable, "CEFuncsActivation");
		exprContextSetFuncTable(s_pContextTarget, stTable);
	}

	// Data
	//  SelfPtr - the source Entity (OPT)
	//  Source, Target - the source and main target Characters for the activation (OPT)
	//  Activation - the Activation
	//  Application - the Application
	exprContextSetSelfPtr(s_pContextTarget,NULL);
	exprContextSetPointerVarPooledCached(s_pContextTarget,s_pchSource,NULL,parse_Character,true,true,&s_hSource);
	exprContextSetPointerVarPooledCached(s_pContextTarget,s_pchSourceOwner,NULL,parse_Character,true,true,&s_hSourceOwner);
	exprContextSetPointerVarPooledCached(s_pContextTarget,s_pchTarget,NULL,parse_Character,true,true,&s_hTarget);
	exprContextSetPointerVarPooledCached(s_pContextTarget,s_pchActivation,NULL,parse_PowerActivation, false, true,&s_hActivation);
	exprContextSetPointerVarPooledCached(s_pContextTarget,s_pchApplication,NULL,parse_PowerApplication,false,true,&s_hApplication);
}

static void ContextInitApply(S32 bInit)
{
	devassert(bInit==!s_pContextApply);

	if(bInit)
	{
		ExprFuncTable* stTable;

		s_pContextApply = exprContextCreate();
		exprContextSetFloatVarPooledCached(s_pContextApply,s_pchForever,POWERS_FOREVER,&s_hForever);
		exprContextSetAllowRuntimePartition(s_pContextApply);

		// Functions
		//  Generic, Self, Character, PowerDef
		stTable = exprContextCreateFunctionTable();
		exprContextAddFuncsToTableByTag(stTable, "util");
		exprContextAddFuncsToTableByTag(stTable, "CEFuncsGeneric");
		exprContextAddFuncsToTableByTag(stTable, "CEFuncsSelf");
		exprContextAddFuncsToTableByTag(stTable, "CEFuncsCharacter");
		exprContextAddFuncsToTableByTag(stTable, "CEFuncsApplication");
		exprContextAddFuncsToTableByTag(stTable, "CEFuncsActivation");
		exprContextAddFuncsToTableByTag(stTable, "CEFuncsPowerDef");
		exprContextAddFuncsToTableByTag(stTable, "CEFuncsAttribMod");
		exprContextAddFuncsToTableByTag(stTable, "CEFuncsTrigger");
		exprContextSetFuncTable(s_pContextApply, stTable);
	}

	// Data
	//  SelfPtr - the source Entity (OPT)
	//  Source, Target - the source and target Characters for the application (Source is OPT)
	//  SourceOwner - the source owner Character for the application (SourceOwner is OPT)
	//  Activation - the Activation
	//  Application - the Application
	//  PowerDef - the PowerDef being applied
	//  SourceItem - the immediate source item of the enhancement power on which expressions are being evaluated
	exprContextSetSelfPtr(s_pContextApply,NULL);
	exprContextSetPointerVarPooledCached(s_pContextApply,s_pchSource,NULL,parse_Character,true,true,&s_hSource);
	exprContextSetPointerVarPooledCached(s_pContextApply,s_pchSourceOwner,NULL,parse_Character,true,true,&s_hSourceOwner);
	exprContextSetPointerVarPooledCached(s_pContextApply,s_pchTarget,NULL,parse_Character,true,true,&s_hTarget);
	exprContextSetPointerVarPooledCached(s_pContextApply,s_pchActivation,NULL,parse_PowerActivation,false,true,&s_hActivation);
	exprContextSetPointerVarPooledCached(s_pContextApply,s_pchApplication,NULL,parse_PowerApplication,false,true,&s_hApplication);
	exprContextSetPointerVarPooledCached(s_pContextApply,s_pchPowerDef,NULL,parse_PowerDef_ForExpr,true,true,&s_hPowerDef);
	exprContextSetPointerVarPooledCached(s_pContextApply,s_pchMod, NULL, parse_AttribMod_ForExpr,true,true,&s_hMod);
	exprContextSetPointerVarPooledCached(s_pContextApply,s_pchModDef, NULL, parse_AttribModDef_ForExpr,true,true,&s_hModDef);
	exprContextSetPointerVarPooledCached(s_pContextApply,s_pchSourceItem, NULL, parse_Item,true,false,&s_hSourceItem);
}

static void ContextInitAffects(S32 bInit)
{
	devassert(bInit==!s_pContextAffects);

	if(bInit)
	{
		ExprFuncTable* stTable;

		s_pContextAffects = exprContextCreate();
		exprContextSetFloatVarPooledCached(s_pContextAffects,s_pchForever,POWERS_FOREVER,&s_hForever);
		exprContextSetAllowRuntimePartition(s_pContextAffects);

		// Functions
		//  Generic, PowerDef, AttribMod, Affects
		//  Self excluded unless we decide it's absolutely necessary
		stTable = exprContextCreateFunctionTable();
		exprContextAddFuncsToTableByTag(stTable, "util");
		exprContextAddFuncsToTableByTag(stTable, "CEFuncsGeneric");
		exprContextAddFuncsToTableByTag(stTable, "CEFuncsPowerDef");
		exprContextAddFuncsToTableByTag(stTable, "CEFuncsAttribMod");
		exprContextAddFuncsToTableByTag(stTable, "CEFuncsAffects");
		exprContextAddFuncsToTableByTag(stTable, "CEFuncsCharacter");
		exprContextAddFuncsToTableByTag(stTable, "CEFuncsTrigger");
		exprContextSetFuncTable(s_pContextAffects, stTable);
	}

	// Data
	//  SelfPtr - the Entity (OPT)
	//  ModAffects - the AttribMod (the one attempting to affect the other) (OPT) (Not accessible in expressions)
	//  PowerDef - the PowerDef of the Power that this AttribMod is trying to affect (OPT)
	//  ModDef - the AttribModDef of the AttribMod that this AttribMod is trying to affect (OPT)
	//  Mod - the AttribMod that this AttribMod is trying to affect (OPT)
	//  TriggerEvent - the CombatEventTracker that a TriggerComplex is checking (OPT)
	exprContextSetSelfPtr(s_pContextAffects,NULL);
	exprContextSetPointerVarPooledCached(s_pContextAffects,s_pchModAffects,NULL,parse_AttribMod_ForExpr,false,false,&s_hModAffects);
	exprContextSetPointerVarPooledCached(s_pContextAffects,s_pchPowerDef,NULL,parse_PowerDef_ForExpr,true,true,&s_hPowerDef);
	exprContextSetPointerVarPooledCached(s_pContextAffects,s_pchModDef,NULL,parse_AttribModDef_ForExpr,true,true,&s_hModDef);
	exprContextSetPointerVarPooledCached(s_pContextAffects,s_pchMod,NULL,parse_AttribMod_ForExpr,true,true,&s_hMod);
	exprContextSetPointerVarPooledCached(s_pContextAffects,s_pchTriggerEvent,NULL,parse_CombatEventTracker,false,false,&s_hTriggerEvent);
}

static void ContextInitExpiration(S32 bInit)
{
	devassert(bInit==!s_pContextExpiration);

	if(bInit)
	{
		ExprFuncTable* stTable;

		s_pContextExpiration = exprContextCreate();
		exprContextSetFloatVarPooledCached(s_pContextExpiration,s_pchForever,POWERS_FOREVER,&s_hForever);
		exprContextSetAllowRuntimePartition(s_pContextExpiration);

		// Functions
		//  Generic, Character, PowerDef, AttribMod
		stTable = exprContextCreateFunctionTable();
		exprContextAddFuncsToTableByTag(stTable, "util");
		exprContextAddFuncsToTableByTag(stTable, "CEFuncsGeneric");
		exprContextAddFuncsToTableByTag(stTable, "CEFuncsCharacter");
		exprContextAddFuncsToTableByTag(stTable, "CEFuncsPowerDef");
		exprContextAddFuncsToTableByTag(stTable, "CEFuncsAttribMod");
		exprContextSetFuncTable(s_pContextExpiration, stTable);
	}

	// Data
	//  SelfPtr - the Entity the expiring mod is on
	//  Self - The character the expiring mod is on
	//  PowerDef - the PowerDef of the AttribMod that is expiring
	//  ModDef - the AttribModDef that is expiring
	//  Mod - The AttribMod that is expiring
	exprContextSetSelfPtr(s_pContextExpiration,NULL);
	exprContextSetPointerVarPooledCached(s_pContextExpiration,s_pchSelf,NULL, parse_Character,true,true,&s_hSelf);
	exprContextSetPointerVarPooledCached(s_pContextExpiration,s_pchPowerDef,NULL,parse_PowerDef_ForExpr,true,true,&s_hPowerDef);
	exprContextSetPointerVarPooledCached(s_pContextExpiration,s_pchModDef,NULL,parse_AttribModDef_ForExpr,true,true,&s_hModDef);
	exprContextSetPointerVarPooledCached(s_pContextExpiration,s_pchMod,NULL,parse_AttribMod_ForExpr,true,true,&s_hMod);
}

static void ContextInitEntCreateEnhancements(S32 bInit)
{
	devassert(bInit==!s_pContextEntCreateEnhancements);

	if(bInit)
	{
		ExprFuncTable* stTable;

		s_pContextEntCreateEnhancements = exprContextCreate();
		exprContextSetFloatVarPooledCached(s_pContextEntCreateEnhancements,s_pchForever,POWERS_FOREVER,&s_hForever);
		exprContextSetAllowRuntimePartition(s_pContextEntCreateEnhancements);

		// Functions
		//  Generic, Character, PowerDef, AttribMod
		stTable = exprContextCreateFunctionTable();
		exprContextAddFuncsToTableByTag(stTable, "util");
		exprContextAddFuncsToTableByTag(stTable, "CEFuncsGeneric");
		exprContextAddFuncsToTableByTag(stTable, "CEFuncsCharacter");
		exprContextAddFuncsToTableByTag(stTable, "CEFuncsPowerDef");
		exprContextSetFuncTable(s_pContextEntCreateEnhancements, stTable);
	}

	// Data
	//  Self, SelfPtr - the character that is creating the critter
	//  Target - the entCreate character that is being created
	//  PowerDef - the PowerDef of the enhancement that may be applied to the critter
	exprContextSetSelfPtr(s_pContextEntCreateEnhancements,NULL);
	exprContextSetPointerVarPooledCached(s_pContextEntCreateEnhancements,s_pchSelf,NULL,parse_Character,true,true, &s_hSelf);
	exprContextSetPointerVarPooledCached(s_pContextEntCreateEnhancements,s_pchPowerDef,NULL,parse_PowerDef_ForExpr,true,true,&s_hPowerDef);
	exprContextSetPointerVarPooledCached(s_pContextEntCreateEnhancements,s_pchTarget,NULL,parse_Character,true,true,&s_hTarget);	
}


static void ContextInitTeleport(S32 bInit)
{
	devassert(bInit==!s_pContextTeleport);

	if(bInit)
	{
		ExprFuncTable* stTable;

		s_pContextTeleport = exprContextCreate();
		exprContextSetAllowRuntimePartition(s_pContextTeleport);
		
		// Functions
		//  Generic, Character, PowerDef, AttribMod
		stTable = exprContextCreateFunctionTable();
		exprContextAddFuncsToTableByTag(stTable, "util");
		exprContextAddFuncsToTableByTag(stTable, "CEFuncsTeleport");
		exprContextSetFuncTable(s_pContextTeleport, stTable);
	}

	// Data
	//  Self, SelfPtr - the character that is creating the critter
	//  Target - the entCreate character that is being created
	exprContextSetSelfPtr(s_pContextTeleport,NULL);
	exprContextSetPointerVarPooledCached(s_pContextTeleport,s_pchSelf,NULL,parse_Character,true,true, &s_hSelf);
	exprContextSetPointerVarPooledCached(s_pContextTeleport,s_pchTarget,NULL,parse_Character,true,true,&s_hTarget);	
}


// Retrieves the given context
ExprContext *combateval_ContextGet(CombatEvalContext eContext)
{
	ExprContext *pContext = NULL;

	switch(eContext)
	{
	case kCombatEvalContext_Simple:
		pContext = s_pContextSimple;
		break;
	case kCombatEvalContext_Enhance:
		pContext = s_pContextEnhance;
		break;
	case kCombatEvalContext_Activate:
		pContext = s_pContextActivate;
		break;
	case kCombatEvalContext_Target:
		pContext = s_pContextTarget;
		break;
	case kCombatEvalContext_Apply:
		pContext = s_pContextApply;
		break;
	case kCombatEvalContext_Affects:
		pContext = s_pContextAffects;
		break;
	case kCombatEvalContext_Expiration:
		pContext = s_pContextExpiration;
		break;
	case kCombatEvalContext_EntCreateEnhancements:
		pContext = s_pContextEntCreateEnhancements;
		break;
	case kCombatEvalContext_Teleport:
		pContext = s_pContextTeleport;
		break;
	}

	return pContext;
}

// Resets the given context
void combateval_ContextReset(CombatEvalContext eContext)
{
/* Nothing really to do here until expressions start having side effects in their contexts
	switch(eContext)
	{
	case kCombatEvalContext_Simple:
	case kCombatEvalContext_Enhance:
	case kCombatEvalContext_Activate:
	case kCombatEvalContext_Target:
	case kCombatEvalContext_Apply:
	case kCombatEvalContext_Affects:
	case kCombatEvalContext_Expiration:
	case kCombatEvalContext_EntCreateEnhancements:
	}
*/
}

// Generates an expression based on the given context
int combateval_Generate(Expression *pExpr, CombatEvalContext eContext)
{
	int bGood = true;
	if(pExpr)
	{
		bGood = false;
		switch(eContext)
		{
		case kCombatEvalContext_Simple:
			{
				bGood = exprGenerate(pExpr,s_pContextSimple);
			}
			break;
		case kCombatEvalContext_Enhance:
			{
				bGood = exprGenerate(pExpr,s_pContextEnhance);
			}
			break;
		case kCombatEvalContext_Activate:
			{
				bGood = exprGenerate(pExpr,s_pContextActivate);
			}
			break;
		case kCombatEvalContext_Target:
			{
				bGood = exprGenerate(pExpr,s_pContextTarget);
			}
			break;
		case kCombatEvalContext_Apply:
			{
				bGood = exprGenerate(pExpr,s_pContextApply);
			}
			break;
		case kCombatEvalContext_Affects:
			{
				bGood = exprGenerate(pExpr,s_pContextAffects);
			}
			break;
		case kCombatEvalContext_Expiration:
			{
				bGood = exprGenerate(pExpr,s_pContextExpiration);
			}
			break;
		case kCombatEvalContext_EntCreateEnhancements:
			{
				bGood = exprGenerate(pExpr,s_pContextEntCreateEnhancements);
			}
			break;
		case kCombatEvalContext_Teleport:
			{
				bGood = exprGenerate(pExpr,s_pContextTeleport);
			}
			break;
		}
	}

	return bGood;
}

// Setup functions to prep the contexts for evaluation

// Setup the Simple context for evaluation
void combateval_ContextSetupSimple(Character *pchar,
								   S32 iLevel,
								   Item * pItem)
{
	exprContextSetSelfPtr(s_pContextSimple,pchar?pchar->pEntParent:NULL);
	exprContextSetPointerVarPooledCached(s_pContextSimple,s_pchSource,pchar,parse_Character,true,false,&s_hSource);
	exprContextSetPointerVarPooledCached(s_pContextSimple,s_pchSourceOwner,pchar,parse_Character,true,true,&s_hSourceOwner);

	exprContextSetPointerVarPooledCached(s_pContextSimple,s_pchSourceItem, pItem, parse_Item,true,false,&s_hSourceItem);
	
	s_pPowerApplicationSimple->iLevelMod = iLevel;
}

// Setup the Enhance context for evaluation
void combateval_ContextSetupEnhance(Character *pChar, PowerDef *pdefTarget, int bIsUnownedPower)
{
	exprContextSetSelfPtr(s_pContextEnhance,pChar?pChar->pEntParent:NULL);
	exprContextSetPointerVarPooledCached(s_pContextEnhance,s_pchPowerDef,pdefTarget,parse_PowerDef_ForExpr,true,true,&s_hPowerDef);
	exprContextSetIntVarPooledCached(s_pContextEnhance,s_pchIsUnownedPower,bIsUnownedPower,&s_hIsUnownedPower);
}

// Setup the Activate context for evaluation
void combateval_ContextSetupActivate(Character *pcharSource,
									 Character *pcharTarget,
									 PowerActivation *pact,
									 CombatEvalPrediction ePrediction)
{
	exprContextSetSelfPtr(s_pContextActivate,pcharSource?pcharSource->pEntParent:NULL);
	exprContextSetPointerVarPooledCached(s_pContextActivate,s_pchSource,pcharSource,parse_Character,true,true,&s_hSource);
	exprContextSetPointerVarPooledCached(s_pContextActivate,s_pchTarget,pcharTarget,parse_Character,true,true,&s_hTarget);
	exprContextSetPointerVarPooledCached(s_pContextActivate,s_pchActivation,pact,parse_PowerActivation, false, true,&s_hActivation);
	exprContextSetIntVarPooledCached(s_pContextActivate,s_pchPrediction,ePrediction,&s_hPrediction);
}

static Character* combateval_GetSourceOwner(Character *pcharSource, PowerApplication *papp)
{
	if (papp && papp->erModOwner && (!pcharSource || pcharSource->pEntParent->myRef != papp->erModOwner))
	{
		Entity *pSourceOwnerEnt = entFromEntityRef(papp->iPartitionIdx, papp->erModOwner);
		if (pSourceOwnerEnt && pSourceOwnerEnt->pChar) 
			return pSourceOwnerEnt->pChar;
	}

	return pcharSource;
}

// Setup the Target context for evaluation
void combateval_ContextSetupTarget(Character *pcharSource,
								   Character *pcharTarget,
								   PowerApplication *papp)
{
	Character *pSourceOwner = pcharSource;

	exprContextSetSelfPtr(s_pContextTarget,pcharSource?pcharSource->pEntParent:NULL);
	
	pSourceOwner = combateval_GetSourceOwner(pcharSource, papp);
	

	exprContextSetPointerVarPooledCached(s_pContextTarget,s_pchSource,pcharSource,parse_Character,true,true,&s_hSource);
	exprContextSetPointerVarPooledCached(s_pContextTarget,s_pchSourceOwner, pSourceOwner, parse_Character, true, true, &s_hSourceOwner);

	exprContextSetPointerVarPooledCached(s_pContextTarget,s_pchTarget,pcharTarget,parse_Character,true,true,&s_hTarget);
	exprContextSetPointerVarPooledCached(s_pContextTarget,s_pchActivation,papp?papp->pact:NULL,parse_PowerActivation,false,true,&s_hActivation);
	exprContextSetPointerVarPooledCached(s_pContextTarget,s_pchApplication,papp,parse_PowerApplication,false,true,&s_hApplication);
}

// Setup the Apply context for evaluation
void combateval_ContextSetupApply(Character *pcharSource,
									Character *pcharTarget,
									Item * pItem,
									PowerApplication *papp)
{
	Character *pSourceOwner = pcharSource;

	exprContextSetSelfPtr(s_pContextApply,pcharSource?pcharSource->pEntParent:NULL);
	
	pSourceOwner = combateval_GetSourceOwner(pcharSource, papp);

	exprContextSetPointerVarPooledCached(s_pContextApply,s_pchSource,pcharSource,parse_Character,true,true,&s_hSource);
	exprContextSetPointerVarPooledCached(s_pContextApply,s_pchSourceOwner, pSourceOwner, parse_Character, true, true, &s_hSourceOwner);
	
	exprContextSetPointerVarPooledCached(s_pContextApply,s_pchTarget,pcharTarget,parse_Character,true,true,&s_hTarget);
	exprContextSetPointerVarPooledCached(s_pContextApply,s_pchActivation,papp?papp->pact:NULL,parse_PowerActivation,false,true,&s_hActivation);
	exprContextSetPointerVarPooledCached(s_pContextApply,s_pchApplication,papp,parse_PowerApplication,false,true,&s_hApplication);
	exprContextSetPointerVarPooledCached(s_pContextApply,s_pchPowerDef,papp?papp->pdef:NULL,parse_PowerDef_ForExpr,true,true,&s_hPowerDef);
	exprContextSetPointerVarPooledCached(s_pContextApply,s_pchMod,papp?papp->pmodEvent:NULL,parse_AttribMod_ForExpr,true,true,&s_hMod);
	exprContextSetPointerVarPooledCached(s_pContextApply,s_pchModDef,papp&&papp->pmodEvent?mod_GetDef(papp->pmodEvent):NULL,parse_AttribModDef_ForExpr,true,true,&s_hModDef);
	if (!pItem)
		exprContextSetPointerVarPooledCached(s_pContextApply,s_pchSourceItem, SAFE_MEMBER2(papp, ppow, pSourceItem), parse_Item,true,false,&s_hSourceItem);
	else
		exprContextSetPointerVarPooledCached(s_pContextApply,s_pchSourceItem, pItem, parse_Item,true,false,&s_hSourceItem);

}

// Setup the Affects context for evaluation
void combateval_ContextSetupAffects(Character *pchar,
									AttribMod *pmodAffects,
									PowerDef *ppowdefTarget,
									AttribModDef *pmoddefTarget,
									AttribMod *pmodTarget,
									CombatEventTracker *pTriggerEvent)
{
	exprContextSetSelfPtr(s_pContextAffects,pchar?pchar->pEntParent:NULL);
	exprContextSetPointerVarPooledCached(s_pContextAffects,s_pchModAffects,pmodAffects,parse_AttribMod_ForExpr,false,false,&s_hModAffects);
	exprContextSetPointerVarPooledCached(s_pContextAffects,s_pchPowerDef,ppowdefTarget,parse_PowerDef_ForExpr,true,true,&s_hPowerDef);
	exprContextSetPointerVarPooledCached(s_pContextAffects,s_pchModDef,pmoddefTarget,parse_AttribModDef_ForExpr,true,true,&s_hModDef);
	exprContextSetPointerVarPooledCached(s_pContextAffects,s_pchMod,pmodTarget,parse_AttribMod_ForExpr,true,true,&s_hMod);
	exprContextSetPointerVarPooledCached(s_pContextAffects,s_pchTriggerEvent,pTriggerEvent,parse_CombatEventTracker,false,false,&s_hTriggerEvent);
}

// Setup the Expiration context for evaluation
void combateval_ContextSetupExpiration(Character *pchar,
									   AttribMod *pmod,
									   AttribModDef *pmoddef,
									   PowerDef *ppowdef)
{
	exprContextSetSelfPtr(s_pContextExpiration,pchar?pchar->pEntParent:NULL);
	exprContextSetPointerVarPooledCached(s_pContextExpiration,s_pchSelf,pchar,parse_Character,true,true, &s_hSelf);
	exprContextSetPointerVarPooledCached(s_pContextExpiration,s_pchMod,pmod,parse_AttribMod_ForExpr,true,true,&s_hMod);
	exprContextSetPointerVarPooledCached(s_pContextExpiration,s_pchModDef,pmoddef,parse_AttribModDef_ForExpr,true,true,&s_hModDef);
	exprContextSetPointerVarPooledCached(s_pContextExpiration,s_pchPowerDef,ppowdef,parse_PowerDef_ForExpr,true,true,&s_hPowerDef);
}

void combateval_ContextSetEnhancementSourceItem(Item *pSourceItem)
{
	exprContextSetPointerVarPooledCached(s_pContextApply,s_pchSourceItem, pSourceItem, parse_Item,true,false,&s_hSourceItem);
	exprContextSetPointerVarPooledCached(s_pContextSimple,s_pchSourceItem, pSourceItem, parse_Item,true,false,&s_hSourceItem);
}

// Setup the EnhancementAttach context for evaluation
void combateval_ContextSetupEntCreateEnhancements(	Character *powner,
													PowerDef *ppowDef,
													Character *pcreatedChar)
{
	exprContextSetSelfPtr(s_pContextEntCreateEnhancements,powner?powner->pEntParent:NULL);
	exprContextSetPointerVarPooledCached(s_pContextEntCreateEnhancements,s_pchSelf,powner,parse_Character,true,true, &s_hSelf);
	exprContextSetPointerVarPooledCached(s_pContextEntCreateEnhancements,s_pchPowerDef,ppowDef,parse_PowerDef_ForExpr,true,true,&s_hPowerDef);
	exprContextSetPointerVarPooledCached(s_pContextEntCreateEnhancements,s_pchTarget,pcreatedChar,parse_Character,true,true,&s_hTarget);	
}

// Setup the EnhancementAttach context for evaluation
void combateval_ContextSetupTeleport(	Character *pchar,
										Character *ptarget)
{
	exprContextSetSelfPtr(s_pContextTeleport,pchar?pchar->pEntParent:NULL);
	exprContextSetPointerVarPooledCached(s_pContextTeleport,s_pchSelf,pchar,parse_Character,true,true, &s_hSelf);
	exprContextSetPointerVarPooledCached(s_pContextTeleport,s_pchTarget,ptarget,parse_Character,true,true,&s_hTarget);	
}

// Evaluate an expression
F32 combateval_EvalNew(int iPartitionIdx, 
					   Expression *pExpr,
					   CombatEvalContext eContext,
					   char **ppchErrorOut)
{
	F32 fResult = 0;
	ExprContext *pContext = combateval_ContextGet(eContext);

	// Overriding is not allowed on the server
#ifdef GAMESERVER
	if(!verify(!g_CombatEvalOverrides.bEnabled))
	{
		g_CombatEvalOverrides.bEnabled = false;
	}
#endif

	if(verify(pContext))
	{
		bool bValid = false;
		MultiVal mv = {0};
		exprContextSetPartition(pContext, iPartitionIdx);
		exprContextSetSilentErrors(pContext,g_bCombatEvalSuppressErrors);
		exprEvaluate(pExpr,pContext,&mv);
		fResult = MultiValGetFloat(&mv,&bValid);
		
		if(ppchErrorOut)
		{
			if(bValid)
			{
				estrClear(ppchErrorOut);
			}
			else if(mv.type==MULTI_INVALID)
			{
				estrCopy2(ppchErrorOut,mv.str);
			}
			else
			{
				estrCopy2(ppchErrorOut,"Unknown error");
			}
		}
	}
	else if(ppchErrorOut)
	{
		estrCopy2(ppchErrorOut,"NULL Context");
	}

	return fResult;
}

Entity* combateval_EvalReturnEntity(int iPartitionIdx, 
									Expression *pExpr,
									CombatEvalContext eContext,
									char **ppchErrorOut)
{
	Entity *pEntRet = NULL;
	ExprContext *pContext = combateval_ContextGet(eContext);

	if(verify(pContext))
	{
		bool bValid = false;
		MultiVal mv = {0};
		Entity*** ents;

		exprContextSetPartition(pContext, iPartitionIdx);
		exprContextSetSilentErrors(pContext,g_bCombatEvalSuppressErrors);
		exprEvaluate(pExpr,pContext,&mv);

		ents = MultiValGetEntityArray(&mv, &bValid);
		if(bValid)
		{
			if(eaSize(ents) == 1)
			{
				if (ppchErrorOut)
					estrClear(ppchErrorOut);
				
				pEntRet = (*ents)[0];
			}
			else if(eaSize(ents) > 1 && ppchErrorOut)
			{
				estrCopy2(ppchErrorOut,"Target override expression returns multiple entities");
			}
		}
		else if(ppchErrorOut)
		{
			if(mv.type==MULTI_INVALID)
			{
				estrCopy2(ppchErrorOut,mv.str);
			}
			else
			{
				estrCopy2(ppchErrorOut,"Unknown error");
			}
		}
	}
	else if(ppchErrorOut)
	{
		estrCopy2(ppchErrorOut,"NULL Context");
	}

	return pEntRet;
}









// Static check functions

static int StaticCheckPowerTable(ExprContext *context, MultiVal *pMV, char **estrError)
{
	if(!pMV->type==MULTI_STRING)
	{
		estrPrintf(estrError, "Must be a string");
		return false;
	}
	else if(!powertable_Find(pMV->str))
	{
		estrPrintf(estrError, "Invalid %s %s", "PowerTable", pMV->str);
		return false;
	}
	return true;
}

static int StaticCheckPowerVar(ExprContext *context, MultiVal *pMV, char **estrError)
{
	if(!pMV->type==MULTI_STRING)
	{
		estrPrintf(estrError, "Must be a string");
		return false;
	}
	else if(!powervar_Find(pMV->str))
	{
		estrPrintf(estrError, "Invalid %s %s", "PowerVar", pMV->str);
		return false;
	}
	return true;
}




// Generic
//  Inputs: PowerTable name, index
//  Return: The value in the generic table at the index (0-based), otherwise 0
AUTO_EXPR_FUNC(CEFuncsGeneric) ACMD_NAME(TableGeneric) ACMD_NOTESTCLIENT;
F32 TableGenericLookup(ACMD_EXPR_SC_TYPE(PowerTable) const char* tableName,
					   int index)
{
	return powertable_Lookup(tableName,MAX(index,0));
}


// Generic
//  Inputs: PowerTable name, index
//  Return: The value in the generic table at the index (0-based), otherwise 0
AUTO_EXPR_FUNC(CEFuncsGeneric) ACMD_NAME(TableSum) ACMD_NOTESTCLIENT;
F32 TableGenericSum(ACMD_EXPR_SC_TYPE(PowerTable) const char* tableName,
					   int iMin_Inclusive, int iMax_Exclusive)
{
	return powertable_SumMulti(tableName, iMin_Inclusive, iMax_Exclusive, 0);
}

// Character
//  Inputs: Character, PowerTable name
//  Return: The value in the Character's Class's table at the Character's combat level,
//   otherwise the generic value of the table at level 1, otherwise 0
AUTO_EXPR_FUNC(CEFuncsCharacter) ACMD_NAME(Table) ACMD_NOTESTCLIENT;
F32 TableLookup(SA_PARAM_OP_VALID Character *character,
				ACMD_EXPR_SC_TYPE(PowerTable) const char* tableName)
{
	F32 r = 0.f;

	if(character && tableName)
	{
		r = character_PowerTableLookupOffset(character, tableName, 0);
	}
	else if(tableName)
	{
		r = TableGenericLookup(tableName, 0);
	}

	return r;
}

// Character
//  Inputs: Character, PowerTable name
//  Return: The value in the Character's Class's table at the Character's combat level + 1,
//   otherwise the generic value of the table at level 1, otherwise 0.
//   Don't use this expression function.  It is wrong.
AUTO_EXPR_FUNC(CEFuncsCharacter) ACMD_NAME(TableWrong) ACMD_NOTESTCLIENT;
F32 TableLookupWrong(SA_PARAM_OP_VALID Character *character,
				ACMD_EXPR_SC_TYPE(PowerTable) const char* tableName)
{
	F32 r = 0.f;

	if(character && tableName)
	{
		r = character_PowerTableLookupOffset(character, tableName, 1);
	}
	else if(tableName)
	{
		r = TableGenericLookup(tableName, 0);
	}

	return r;
}

// Character
//  Inputs: Character, PowerTable name, index
//  Return: The value in the Character's Class's table at the index,
//   otherwise the generic value of the table at the index, otherwise 0
AUTO_EXPR_FUNC(CEFuncsCharacter) ACMD_NAME(TableAt) ACMD_NOTESTCLIENT;
F32 TableLookupAt(SA_PARAM_OP_VALID Character *character,
				  ACMD_EXPR_SC_TYPE(PowerTable) const char* tableName,
				  S32 index)
{
	F32 r = 0.f;
	CharacterClass *pClass = NULL;

	if(character && NULL!=(pClass = character_GetClassCurrent(character)))
	{
		r = class_powertable_Lookup(pClass,tableName,index);
	}
	else if(tableName)
	{
		r = TableGenericLookup(tableName, index);
	}

	return r;
}

// Character
//  Inputs: Character, PowerTable name, index
//  Return: The value in the Character's Class's table at the index, linearly interpolated, otherwise 0
AUTO_EXPR_FUNC(CEFuncsCharacter) ACMD_NAME(TableAtLerp) ACMD_NOTESTCLIENT;
F32 TableLookupAtLerp(SA_PARAM_OP_VALID Character *character,
					  ACMD_EXPR_SC_TYPE(PowerTable) const char* tableName,
					  F32 index)
{
	F32 r = 0.f;
	CharacterClass *pClass = NULL;

	if(character && NULL!=(pClass = character_GetClassCurrent(character)))
	{
		S32 iIndex = floor(index);
		r = class_powertable_Lookup(pClass,tableName,iIndex);
		if(index!=(F32)iIndex)
		{
			F32 r2 = class_powertable_Lookup(pClass,tableName,iIndex+1);
			r += (r2-r) * (index-(F32)iIndex);
		}
	}

	return r;
}

// Application, ApplicationSimple
//  Inputs: Character, PowerTable name
//  Return: The value in the Character's Class's table at the Application's level,
//   otherwise the generic value of the table at the Application's level, otherwise 0
AUTO_EXPR_FUNC(CEFuncsApplication, CEFuncsApplicationSimple) ACMD_NAME(TableApp);
F32 TableLookupApp(ExprContext *pContext,
				   SA_PARAM_OP_VALID Character *character,
				   ACMD_EXPR_SC_TYPE(PowerTable) const char* tableName)
{
	F32 r = 0.f;

	// Horrible custom hack for autodescription, since it uses passive detection of
	//  xpath errors to work, which means I can't actually put a PowerApplication in
	//  the context pre-emptively.
	if(g_CombatEvalOverrides.iAutoDescTableAppHack)
	{
		r = TableLookupAt(character, tableName, g_CombatEvalOverrides.iAutoDescTableAppHack - 1);
	}
	else
	{
		PowerApplication *papp = exprContextGetVarPointerUnsafePooled(pContext,s_pchApplication);

		if(papp)
		{
			r = TableLookupAt(character, tableName, papp->iLevelMod - 1);
		}
	}

	return r;
}

// Application
//  Inputs: PowerTable name
//  Return: The value in the generic PowerTable at the PowerTree rank of the Power being applied.
//   So a Rank 2 Power would return the second value in the PowerTable.  If there is no actual
//   Power, or the Power isn't from a PowerTree, it returns the first value in the PowerTable.
AUTO_EXPR_FUNC(CEFuncsApplication);
F32 TableNodeRank(ExprContext *pContext,
				  ACMD_EXPR_SC_TYPE(PowerTable) const char* tableName)
{
	F32 r = 0;
	
	// Same deal as the override for TableLookupApp
	if(g_CombatEvalOverrides.iAutoDescTableNodeRankHack)
	{
		r = TableGenericLookup(tableName,g_CombatEvalOverrides.iAutoDescTableNodeRankHack-1);
	}
	else
	{
		PowerApplication *papp = exprContextGetVarPointerUnsafePooled(pContext,s_pchApplication);

		if(papp && papp->ppow && papp->ppow->eSource==kPowerSource_PowerTree)
		{
			r = TableGenericLookup(tableName,papp->ppow->iIdxMultiTable);
		}
		else
		{
			r = TableGenericLookup(tableName,0);
		}
	}

	return r;
}

// Generic
//  Inputs: PowerVar name
//  Return: The generic numerical value of the variable, otherwise 0
AUTO_EXPR_FUNC(CEFuncsGeneric) ACMD_NOTESTCLIENT;
F32 VarGeneric(ACMD_EXPR_SC_TYPE(PowerVar) const char* varName)
{
	F32 r = 0.f;
	MultiVal *pMV = powervar_Find(varName);
	if(pMV)
	{
		r = MultiValGetFloat(pMV,NULL);
	}
	return r;
}

// Character
//  Inputs: Character, PowerVar name
//  Return: The numerical value of the variable according to the Character's class,
//   otherwise the generic numerical value of the variable, otherwise 0.
AUTO_EXPR_FUNC(CEFuncsCharacter) ACMD_NOTESTCLIENT;
F32 Var(SA_PARAM_OP_VALID Character *character,
		ACMD_EXPR_SC_TYPE(PowerVar) const char* varName)
{
	F32 r = 0.f;
	CharacterClass *pClass = NULL;

	if(character && NULL!=(pClass = character_GetClassCurrent(character)))
	{
		MultiVal *pMV = powervar_FindInClass(varName,pClass);
		if(pMV)
		{
			r = MultiValGetFloat(pMV,NULL);
		}
	}
	else
	{
		r = VarGeneric(varName);
	}

	return r;
}

// Generic
//  Inputs: PowerVar name
//  Return: The generic string value of the variable, otherwise NULL
AUTO_EXPR_FUNC(CEFuncsGeneric) ACMD_NOTESTCLIENT;
const char *StringVarGeneric(ACMD_EXPR_SC_TYPE(PowerVar) const char* varName)
{
	MultiVal *pMV = powervar_Find(varName);
	if(pMV)
	{
		return MultiValGetString(pMV,NULL);
	}
	return NULL;
}

// Character
//  Inputs: Character, PowerVar name
//  Return: The string value of the variable according to the Character's class,
//   otherwise the generic string value of the variable, otherwise NULL
AUTO_EXPR_FUNC(CEFuncsCharacter) ACMD_NOTESTCLIENT;
const char *StringVar(SA_PARAM_OP_VALID Character *character,
					  ACMD_EXPR_SC_TYPE(PowerVar) const char* varName)
{
	const char *r = NULL;
	CharacterClass *pClass = NULL;

	if(character && NULL!=(pClass = character_GetClassCurrent(character)))
	{
		MultiVal *pMV = powervar_FindInClass(varName,pClass);
		if(pMV)
		{
			r = MultiValGetString(pMV,NULL);
		}
	}
	else
	{
		r = StringVarGeneric(varName);
	}

	return r;
}


// PowerDef
//  Inputs: PowerDef, PowerDef name
//  Return: 1 if the PowerDef is the same as the named PowerDef, otherwise 0
AUTO_EXPR_FUNC(CEFuncsPowerDef);
int IsPowerDef(SA_PARAM_OP_VALID PowerDef_ForExpr *powerDef,
			   ACMD_EXPR_DICT(PowerDef) const char *powerDefName)
{
	int r = 0;
	PowerDef *pdef = powerdef_Find(powerDefName);
	r = (pdef && pdef==powerDef);
	return r;
}

// Affects
//  Inputs: none
//  Return: 1 if the AttribMod or PowerDef being affected is the same as the affector AttribMod's PowerDef, otherwise 0
AUTO_EXPR_FUNC(CEFuncsAffects);
int IsPowerDefThis(ExprContext *pContext)
{
	int r = 0;
	AttribMod *pmodAffects = exprContextGetVarPointerUnsafePooled(pContext,s_pchModAffects);
	AttribModDef *pmoddefAffects = mod_GetDef(pmodAffects);
	AttribModDef *pmoddefTarget = exprContextGetVarPointerUnsafePooled(pContext,s_pchModDef);
	PowerDef *ppowdefTarget = exprContextGetVarPointerUnsafePooled(pContext,s_pchPowerDef);
	if(pmoddefAffects && pmoddefAffects->pPowerDef)
	{
		r = (pmoddefTarget && pmoddefTarget->pPowerDef==pmoddefAffects->pPowerDef) || (ppowdefTarget && ppowdefTarget==pmoddefAffects->pPowerDef);
	}
	return r;
}

// TODO: Remove this and use Activity_IsActive instead
AUTO_EXPR_FUNC(CEFuncsAttribMod);
bool IsActivityActive(const char *pchActivityName)
{
#ifdef GAMESERVER
	return gslActivity_IsActive(pchActivityName);
#else
	Errorf("IsActivityActive: Cannot check an activity on the client");
	return false;
#endif
}

// AttribMod
//  Inputs: AttribModDef, PowerDef name
//  Return: 1 if the AttribModDef is from named PowerDef, otherwise 0
AUTO_EXPR_FUNC(CEFuncsAttribMod);
int ModDefFromPowerDef(SA_PARAM_OP_VALID AttribModDef_ForExpr *modDef,
					   ACMD_EXPR_DICT(PowerDef) const char *powerDefName)
{
	int r = 0;
	PowerDef *pdef = powerdef_Find(powerDefName);
	r = (pdef && modDef && pdef==modDef->pPowerDef);
	return r;
}

// AttribMod
//  Inputs: AttribMod, PowerDef name
//  Return: 1 if the AttribMod is from named PowerDef, otherwise 0
AUTO_EXPR_FUNC(CEFuncsAttribMod);
int ModFromPowerDef(SA_PARAM_NN_VALID AttribMod_ForExpr *mod,
					ACMD_EXPR_DICT(PowerDef) const char *powerDefName)
{
	int r = 0;
	PowerDef *pdef = powerdef_Find(powerDefName);
	r = (pdef && pdef==GET_REF(mod->hPowerDef));
	return r;
}

// Affects
//  Return: 1 if the AttribMod being affected and the affector AttribMod are from the same source, otherwise 0
AUTO_EXPR_FUNC(CEFuncsAffects);
int ModsFromSameSource(ExprContext *pContext)
{
	int r = 0;
	AttribMod *pmodAffects = exprContextGetVarPointerUnsafePooled(pContext,s_pchModAffects);
	AttribMod *pmodTarget = exprContextGetVarPointerUnsafePooled(pContext,s_pchMod);
	r = (pmodAffects && pmodTarget && pmodAffects->erSource && pmodAffects->erSource==pmodTarget->erSource);
	return r;
}


// Affects
//  Inputs: AttribMod
//  Return: Owner Character of the AttribMod
AUTO_EXPR_FUNC(CEFuncsAttribMod);
SA_RET_OP_VALID Character* ModGetOwner(SA_PARAM_NN_VALID AttribMod_ForExpr *pMod)
{
	Entity* pEnt = pMod ? entFromEntityRefAnyPartition(pMod->erOwner) : NULL;
	return pEnt ? pEnt->pChar : NULL;
}


// Affects
//  Inputs: AttribMod
//  Return: Source Character of the AttribMod
AUTO_EXPR_FUNC(CEFuncsAttribMod);
SA_RET_OP_VALID Character* ModGetSource(SA_PARAM_NN_VALID AttribMod_ForExpr *pMod)
{
	Entity* pEnt = pMod ? entFromEntityRefAnyPartition(pMod->erSource) : NULL;
	return pEnt ? pEnt->pChar : NULL;
}

// Affects
//  Inputs: AttribMod
//  Return: Target Character of the AttribMod
AUTO_EXPR_FUNC(CEFuncsAttribMod);
SA_RET_OP_VALID Character* ModGetTarget(ExprContext *pContext,SA_PARAM_NN_VALID AttribMod_ForExpr *pMod)
{
	if (pMod == NULL)
		return NULL;
	
	if (pMod->pDef && pMod->pDef->eTarget == kModTarget_Target)
	{
		return exprContextGetVarPointerUnsafePooled(pContext,s_pchTarget);
	}
	else
	{
		Entity * pEnt = entFromEntityRefAnyPartition(pMod->erSource);
		if (pEnt)
		{
			return pEnt->pChar;
		}
	}

	return NULL;
}

// Returns > 0 if apply 1 is older, < 0 if apply 2 is older, or 0 if they're the same, or if we can't tell.
// The rules here are a little tricky, so make sure you understand it before you use it.
static S32 ApplyIDRelativeAge(U32 uiApplyID1, U32 uiApplyID2)
{
	U32 uiLoaded1, uiLoaded2;
	S32 r = 0;
	
	// Same, or either one is unknown/invalid
	if(uiApplyID1==uiApplyID2 || !uiApplyID1 || !uiApplyID2)
		return 0;

	uiLoaded1 = uiApplyID1 & BIT(31);
	uiLoaded2 = uiApplyID2 & BIT(31);
	
	// Both loaded from the db
	if(uiLoaded1 && uiLoaded2)
		return 0;

	// 1 was loaded from the db but 2 is new to this map
	if(uiLoaded1)
		return 1;

	// 2 was loaded from the db but 1 is new to this map
	if(uiLoaded2)
		return -1;

	// Both new to the map, 1 is lower, so it's older
	if(uiApplyID1 < uiApplyID2)
		return 1;

	return -1; // Both new to the map, 2 is lower, so it's older
}

// Affects
//  Return: 1 if the AttribMod being affected is newer than the affect AttribMod, otherwise 0
//  Note that "newer" is in terms of the original application, NOT in terms of actual real-world time.
AUTO_EXPR_FUNC(CEFuncsAffects);
int ModAffectedIsNewer(ExprContext *pContext)
{
	int r = 0;
	AttribMod *pmodAffects = exprContextGetVarPointerUnsafePooled(pContext,s_pchModAffects);
	AttribMod *pmodTarget = exprContextGetVarPointerUnsafePooled(pContext,s_pchMod);
	if(pmodAffects && pmodTarget)
	{
		// See if affects is older than target
		S32 iAge = ApplyIDRelativeAge(pmodAffects->uiApplyID,pmodTarget->uiApplyID);
		r = (iAge > 0);
	}
	return r;
}

// AttribMod
//  Inputs: AttribMod
//  Return: Angle to the source position of the AttribMod, relative to your orientation.  Based on
//   your current position and orientation.  (-180 .. 180].  Returns 0 for failure cases.
AUTO_EXPR_FUNC(CEFuncsAttribMod);
F32 AngleModSource(ExprContext *pContext,
				   SA_PARAM_OP_VALID AttribMod_ForExpr *pmod)
{
	F32 fAngle = 0;
	Entity *pent = exprContextGetSelfPtr(pContext);

	if(pent && pmod)
	{
		ANALYSIS_ASSUME(pent != NULL); 
		fAngle = mod_AngleToSource(pmod,pent);
		fAngle = DEG(fAngle);
	}

	return fAngle;
}

// AttribMod
//  Inputs: AttribMod
//  Return: Gets the current period of the attrib mod
AUTO_EXPR_FUNC(CEFuncsAttribMod);
U32 ModPeriod(SA_PARAM_NN_VALID AttribMod_ForExpr *pmod)
{
	return pmod->uiPeriod;
}

// AttribMod
//  Inputs: AttribMod
//  Return: The magnitude of the AttribMod
AUTO_EXPR_FUNC(CEFuncsAttribMod);
F32 ModMag(SA_PARAM_NN_VALID AttribMod_ForExpr *pmod)
{
	return pmod->fMagnitude;
}

// AttribMod
//  Inputs: AttribMod
//  Return: The percentage health of the AttribMod.  Returns -1 if the AttribMod isn't fragile.
AUTO_EXPR_FUNC(CEFuncsAttribMod);
F32 ModHealthPct(SA_PARAM_NN_VALID AttribMod_ForExpr *pmod)
{
	if( pmod->pFragility )
	{
		return(pmod->pFragility->fHealth > 0 ?
			pmod->pFragility->fHealth / pmod->pFragility->fHealthMax :
		0.f);
	}
	return -1;
}

// AttribMod
//  Inputs: AttribMod
//  Return: The health of the AttribMod.  Returns -1 if the AttribMod isn't fragile.
AUTO_EXPR_FUNC(CEFuncsAttribMod);
F32 ModHealth(SA_PARAM_NN_VALID AttribMod_ForExpr *pmod)
{
	if( pmod->pFragility )
	{
		return(	pmod->pFragility->fHealth );
	}
	return -1;
}


// AttribMod
//  Inputs: AttribMod
//  Return: True if the attribMod is considered as having already applied itself at least once
AUTO_EXPR_FUNC(CEFuncsAttribMod);
int ModConsideredPostApplied(SA_PARAM_NN_VALID AttribMod_ForExpr *pmod)
{
	return pmod->bPostFirstTickApply;
}


// Generic
//  Inputs: PowerTag field (integer), PowerTag name
//  Return: 1 if the field includes the named tag, otherwise 0
AUTO_EXPR_FUNC(CEFuncsGeneric) ACMD_NOTESTCLIENT;
int HasPowerTag(SA_PARAM_NN_VALID PowerTagsStruct *tags,
				ACMD_EXPR_ENUM(PowerTag) const char *tagName)
{
	int eTag = StaticDefineIntGetInt(PowerTagsEnum,tagName);
	return (eTag>0 && powertags_Check(tags,eTag));
}

// Generic
//  Inputs: PowerTag field (integer), PowerTag name
//  Return: 1 if the field includes the named tag, otherwise 0
AUTO_EXPR_FUNC(CEFuncsGeneric) ACMD_NOTESTCLIENT;
int NotHasPowerTag(SA_PARAM_NN_VALID PowerTagsStruct *tags,
				   ACMD_EXPR_ENUM(PowerTag) const char *tagName)
{
	return !HasPowerTag(tags,tagName);
}

// PowerDef
//  Inputs: PowerDef, PowerCategory name
//  Return: 1 if the PowerDef includes the named category, otherwise 0
AUTO_EXPR_FUNC(CEFuncsPowerDef) ACMD_NOTESTCLIENT;
int HasPowerCat(SA_PARAM_OP_VALID PowerDef_ForExpr *powerDef,
				ACMD_EXPR_ENUM(PowerCategory) const char *categoryName)
{
	if(powerDef && powerDef->piCategories)
	{
		int i, eCat = StaticDefineIntGetInt(PowerCategoriesEnum,categoryName);
		for(i=eaiSize(&powerDef->piCategories)-1; i>=0; i--)
		{
			if(powerDef->piCategories[i]==eCat)
			{
				return 1;
			}
		}
	}
	return 0;
}

// PowerDef
//  Inputs: PowerDef, PowerType name
//  Return: 1 if the PowerDef is of the passed in type, otherwise 0
AUTO_EXPR_FUNC(CEFuncsPowerDef) ACMD_NOTESTCLIENT;
int HasPowerType(SA_PARAM_OP_VALID PowerDef_ForExpr *powerDef,
				 ACMD_EXPR_ENUM(PowerType) const char *powerTypeName)
{
	if (powerDef)
	{
		PowerType eType = StaticDefineIntGetInt(PowerTypeEnum, powerTypeName);
		if (powerDef->eType == eType)
			return 1;
	}
	return 0;
}

// PowerDef
//  Inputs: PowerDef, Attrib name, Aspect name
//  Return: 1 if the PowerDef includes a matching attrib, otherwise 0
AUTO_EXPR_FUNC(CEFuncsPowerDef) ACMD_NOTESTCLIENT;
int PowerHasAttrib(SA_PARAM_OP_VALID PowerDef_ForExpr *powerDef,
				   ACMD_EXPR_ENUM(AttribType) const char *attribName,
				   ACMD_EXPR_ENUM(AttribAspect) const char *aspectName)
{
	if(powerDef && powerDef->ppOrderedMods)
	{
		int i, eAttr = StaticDefineIntGetInt(AttribTypeEnum,attribName);
		int eAspect = StaticDefineIntGetInt(AttribAspectEnum,aspectName);
		for(i=eaSize(&powerDef->ppOrderedMods)-1; i>=0; i--)
		{
			if(powerDef->ppOrderedMods[i]->offAttrib==eAttr && powerDef->ppOrderedMods[i]->offAspect == eAspect)
			{
				return 1;
			}
		}
	}
	return 0;
}

// Generic
//  Inputs: Attrib name
//  Return: The integer of the attrib, otherwise -1
//   If the named attrib is an attrib in the code, return the integer that represents it's off attrib
AUTO_EXPR_FUNC(CEFuncsGeneric);
int AttribFromString(ACMD_EXPR_ENUM(AttribType) const char *attribName)
{
	return StaticDefineIntGetInt(AttribTypeEnum,attribName);
}

// Generic
//  Inputs: Attrib field (integer), Attrib name
//  Return: 1 if the Attrib field matches the named Attrib, otherwise 0.
//   If the named Attrib is an AttribSet then this will return 1 if the field is found in the set.
AUTO_EXPR_FUNC(CEFuncsGeneric);
int IsAttrib(int attrib,
			 ACMD_EXPR_ENUM(AttribType) const char *attribName)
{
	int eAttrib = StaticDefineIntGetInt(AttribTypeEnum,attribName);
	return attrib_Matches(attrib,eAttrib);
}

// Generic
//  Inputs: Aspect field (integer), Aspect name
//  Return: 1 if the field matches the name, otherwise 0
AUTO_EXPR_FUNC(CEFuncsGeneric);
int IsAspect(int aspect,
			 ACMD_EXPR_ENUM(AttribAspect) const char *aspectName)
{
	int eAspect = StaticDefineIntGetInt(AttribAspectEnum,aspectName);
	return (aspect == eAspect);
}

// Generic
//  Inputs: AttribModDef, PowerMode name
//  Return: 1 if the attrib mod is of type PowerMode, and the granted mode matches the inputed mode
AUTO_EXPR_FUNC(CEFuncsGeneric);
int IsAttribPowerMode(SA_PARAM_OP_VALID AttribModDef_ForExpr *modDef,
					  ACMD_EXPR_ENUM(PowerMode) const char *powerModeName)
{
	if(modDef && modDef->offAttrib == kAttribType_PowerMode)
	{
		int iMode = StaticDefineIntGetInt(PowerModeEnum,powerModeName);
		PowerModeParams *pParams = (PowerModeParams*)modDef->pParams;
		if(pParams && pParams->iPowerMode == iMode)
			return 1;
	}
	return 0;
}

// Generic
//  Inputs: AttribMod, ModExpirationReason name
//  Return: 1 if the attrib mod is expired and expired for the specified reason, otherwise 0
AUTO_EXPR_FUNC(CEFuncsGeneric);
int ExpirationReason(SA_PARAM_NN_VALID AttribMod_ForExpr *mod,
					 ACMD_EXPR_ENUM(ModExpirationReason) const char *reasonName)
{
	if(mod->fDuration < 0)
	{
		int iReason = StaticDefineIntGetInt(ModExpirationReasonEnum,reasonName);
		if((int)mod->fDuration == iReason)
		{
			return 1;
		}
	}

	return 0;
}

// Character
//  Inputs: Character source, Character target, PowerTarget name
//  Return: 1 if the relationship between the source and target Characters matches the named PowerTarget, otherwise 0
AUTO_EXPR_FUNC(CEFuncsCharacter) ACMD_NAME(TargetIsType) ACMD_NOTESTCLIENT;
int TargetIsType(ACMD_EXPR_PARTITION iPartitionIdx,
				 SA_PARAM_OP_VALID Character *sourceCharacter,
				 SA_PARAM_OP_VALID Character *targetCharacter,
				 ACMD_EXPR_RES_DICT(PowerTarget) const char *powerTargetName)
{
	int r = false;
	PowerTarget *pPowerTarget = (PowerTarget*)RefSystem_ReferentFromString(g_hPowerTargetDict,powerTargetName);
	if(pPowerTarget && character_TargetMatchesPowerType(iPartitionIdx,sourceCharacter,targetCharacter,pPowerTarget))
	{
		r = true;
	}
	return r;
}

// Character
//  Inputs: Character source, Character target
//  Return: 1 if the target Character is the hard target of the source Character, otherwise 0
AUTO_EXPR_FUNC(CEFuncsCharacter) ACMD_NAME(IsTarget, IsHardTarget);
int IsTarget(SA_PARAM_OP_VALID Character *sourceCharacter, SA_PARAM_OP_VALID Character *targetCharacter)
{
	if (sourceCharacter && targetCharacter && targetCharacter->pEntParent)
	{
		if (sourceCharacter->currentTargetRef == targetCharacter->pEntParent->myRef)
		{
			return true;
		}
	}
	return false;
}

// Character
//  Inputs: Character
//  Return: the Character's current target Character
AUTO_EXPR_FUNC(CEFuncsCharacter);
SA_RET_OP_VALID Character* GetTarget(SA_PARAM_OP_VALID Character *character)
{
	Character *r = NULL;
	if(character && character->currentTargetRef)
	{
		Entity *pentTarget = entFromEntityRef(entGetPartitionIdx(character->pEntParent),character->currentTargetRef);
		if(pentTarget)
			r = pentTarget->pChar;
	}
	return r;
}

// Character
//  Inputs: Character, CharacterClass name
//  Return: 1 if the Character is of the named class, otherwise 0
AUTO_EXPR_FUNC(CEFuncsCharacter);
int IsClass(SA_PARAM_OP_VALID Character *character,
			ACMD_EXPR_DICT(CharacterClass) const char *className)
{
	int r = 0;
	if(character)
	{
		if(IS_HANDLE_ACTIVE(character->hClassTemporary))
			r = !stricmp(REF_STRING_FROM_HANDLE(character->hClassTemporary),className);
		else if(IS_HANDLE_ACTIVE(character->hClass))
			r = !stricmp(REF_STRING_FROM_HANDLE(character->hClass),className);
	}
	return r;
}

// Character
//  Inputs: Character, CharacterClass name
//  Return: 0 if the Character is of the named class, otherwise 1
AUTO_EXPR_FUNC(CEFuncsCharacter);
int NotIsClass(SA_PARAM_OP_VALID Character *character,
			   ACMD_EXPR_DICT(CharacterClass) const char *className)
{
	return !IsClass(character,className);
}

// Character
//  Inputs: Character, CharClassCategory name
//  Return: 1 if the Character is of the named class category, otherwise 0
AUTO_EXPR_FUNC(CEFuncsCharacter);
int IsClassCategory(SA_PARAM_OP_VALID Character *character,
					ACMD_EXPR_ENUM(CharClassCategory) const char *classCategoryName)
{
	int r = 0;
	if(character && classCategoryName)
	{
		CharacterClass *pClass = GET_REF(character->hClass);
		CharClassCategory eCategory = StaticDefineIntGetInt(CharClassCategoryEnum, classCategoryName);
		if (pClass)
		{
			r = (pClass->eCategory == eCategory);
		}
	}
	return r;
}

// Character
//  Inputs: Character, Gender name
//  Return: 1 if the Character is of the named gender, otherwise 0
AUTO_EXPR_FUNC(CEFuncsCharacter);
int IsGender(SA_PARAM_OP_VALID Character *character,
			 ACMD_EXPR_ENUM(Gender) const char *genderName)
{
	int r = 0;
	if(character && character->pEntParent)
	{
		Gender eGender = StaticDefineIntGetInt(GenderEnum, genderName);
		r = (eGender==character->pEntParent->eGender);
	}
	return r;
}

// Character
//  Inputs: Character
//  Return: 1 if the Character is flagged as unstoppable, otherwise 0
AUTO_EXPR_FUNC(CEFuncsCharacter);
int IsUnstoppable(SA_PARAM_OP_VALID Character *character)
{
	int r = false;
	if(character)
	{
		r = !!character->bUnstoppable;
	}
	return r;
}

// Character
//  Inputs: Character
//  Return: 1 if the Character is flagged as invulnerable, otherwise 0
AUTO_EXPR_FUNC(CEFuncsCharacter);
int IsInvulnerable(SA_PARAM_OP_VALID Character *character)
{
	int r = false;
	if(character)
	{
		r = !!character->bInvulnerable;
	}
	return r;
}

// Character
//  Inputs: Character
//  Return: 1 if the Character is flagged as unkillable, otherwise 0
AUTO_EXPR_FUNC(CEFuncsCharacter);
int IsUnkillable(SA_PARAM_OP_VALID Character *character)
{
	int r = false;
	if(character)
	{
		r = !!character->bUnkillable;
	}
	return r;
}

// Character
//  Inputs: Character
//  Return: 1 if the Character is considered in combat, otherwise 0
AUTO_EXPR_FUNC(CEFuncsCharacter) ACMD_NOTESTCLIENT;
int InCombat(SA_PARAM_OP_VALID Character *character)
{
#if !GAMESERVER && !GAMECLIENT
	assert(0);
#else
	int r = false;
	if(character)
	{
		U32 uiNow = pmTimestamp(0);
		if(uiNow < character->uiTimeCombatExit)
		{
			r = true;
		}
	}
	return r;
#endif
}

// Character
//  Inputs: Character
//  Return: 1 if the Character is considered in combat, otherwise 0
AUTO_EXPR_FUNC(CEFuncsCharacter) ACMD_NOTESTCLIENT;
int NotInCombat(SA_PARAM_OP_VALID Character *character)
{
	return !InCombat(character);
}

// If bNot is set, then it's a NotHasMode() test instead of a HasMode() test
static int CharacterHasModePredicted(ExprContext *pContext,
									 SA_PARAM_OP_VALID Character *character,
									 const char *modeName,
									 int bNot)
{
	int bResult = bNot;
	if (character)
	{
		CombatEvalPrediction ePredict = combateval_GetPrediction(pContext);
		int iMode = StaticDefineIntGetInt(PowerModeEnum, modeName);

		bResult = character_HasMode(character,iMode);
		if(bNot)
			bResult = !bResult;

		if(ePredict!=kCombatEvalPrediction_None)
		{
			int bPredictedResult = (ePredict == kCombatEvalPrediction_True) ? true : false;

			if(bResult!=bPredictedResult)
			{
				int i;
				for(i = eaSize(&character->ppPowerModeHistory)-1; i>=0; i--)
				{
					PowerModes *pPowerModes = character->ppPowerModeHistory[i];
					bResult = eaiFind(&pPowerModes->piPowerModes, iMode) >= 0; // Since this is sorted we could do a faster search
					if(bNot)
						bResult = !bResult;

					if(bResult==bPredictedResult)
						break;
				}
			}
		}
	}
	return bResult;
}

// Character
//  Inputs: Character, PowerMode name
//  Return: 1 if the Character has the named mode, otherwise 0
//  Prediction: Server attempts to match prediction using recent PowerMode state of the Character
AUTO_EXPR_FUNC(CEFuncsCharacter);
int HasMode(ExprContext *context,
			SA_PARAM_OP_VALID Character *character,
			ACMD_EXPR_ENUM(PowerMode) const char *modeName)
{
	return CharacterHasModePredicted(context, character, modeName, false);
}

// Character
//  Inputs: Character, PowerMode name
//  Return: 0 if the Character has the named mode, otherwise 1
//  Prediction: Server attempts to match prediction using recent PowerMode state of the Character
AUTO_EXPR_FUNC(CEFuncsCharacter);
int NotHasMode(ExprContext *context,
			   SA_PARAM_OP_VALID Character *character,
			   ACMD_EXPR_ENUM(PowerMode) const char *modeName)
{
	return CharacterHasModePredicted(context, character, modeName, true);
}

// Character
//  Inputs: Character, PowerMode name, Character target
//  Return: 1 if the Character has the named mode associated with the specified target, otherwise 0
// TODO: This isn't predicted
AUTO_EXPR_FUNC(CEFuncsCharacter);
int HasModePersonal(SA_PARAM_OP_VALID Character *character,
					ACMD_EXPR_ENUM(PowerMode) const char *modeName,
					SA_PARAM_OP_VALID Character *characterTarget)
{
	int iMode = StaticDefineIntGetInt(PowerModeEnum,modeName);
	return character_HasModePersonal(character, iMode, characterTarget);
}

// Character
//  Inputs: Character, PowerMode name, Character target
//  Return: 1 if the Character has the named mode associated with the specified target, otherwise 0
// TODO: This isn't predicted
AUTO_EXPR_FUNC(CEFuncsCharacter);
int HasModePersonalAnyTarget(SA_PARAM_OP_VALID Character *character,
							 ACMD_EXPR_ENUM(PowerMode) const char *modeName)
{
	int iMode = StaticDefineIntGetInt(PowerModeEnum,modeName);
	return character_HasModePersonalAnyTarget(character, iMode);
}


// Character
//  Inputs: Character, PowerDef name
//  Return: 1 if the Character has an AttribMod on them from the named PowerDef, otherwise 0.
//   If the PowerDef is Innate, instead returns 1 if the Character has the named PowerDef, otherwise 0 - so it
//   is basically like OwnsPower() except it checks external sources like PowerVolumes as well.
AUTO_EXPR_FUNC(CEFuncsCharacter);
int AffectedByPower(SA_PARAM_OP_VALID Character *character,
					ACMD_EXPR_DICT(PowerDef) const char* powerDefName)
{
	if(character)
	{
		int i;
		PowerDef *pdef = powerdef_Find(powerDefName);
		if(pdef)
		{
			if(pdef->eType!=kPowerType_Innate)
			{
				if(entIsServer())
				{
					for(i=eaSize(&character->modArray.ppMods)-1; i>=0; i--)
					{
						if(pdef==GET_REF(character->modArray.ppMods[i]->hPowerDef))
						{
							return true;
						}
					}
				}
				else
				{
					for(i=eaSize(&character->ppModsNet)-1; i>=0; i--)
					{
						if(character->ppModsNet[i]->uiDurationOriginal && pdef==GET_REF(character->ppModsNet[i]->hPowerDef))
						{
							return true;
						}
					}
				}
			}
			else
			{
				if(character_FindPowerByDef(character,pdef))
				{
					return true;
				}
				
				if(character->pEntParent->externalInnate)
				{
					for(i=eaSize(&character->pEntParent->externalInnate->ppPowersExternalInnate)-1; i>=0; i--)
					{
						if(pdef==GET_REF(character->pEntParent->externalInnate->ppPowersExternalInnate[i]->hDef))
						{
							return true;
						}
					}
				}
			}
		}
	}
	return false;
}


// Character
//  Inputs: Character, PowerDef name, optional source Character, optional owner Character
//  Return: The "stack size" of the a PowerDef affecting a character.  If a source or owner is
//   specified, only counts mods with that source and/or owner.
//   Returns 0 if no stacks, or PowerDef isn't found or isn't valid (Innate or Combo).
AUTO_EXPR_FUNC(CEFuncsCharacter);
int AffectedByPowerEx(SA_PARAM_OP_VALID Character *character,
					ACMD_EXPR_DICT(PowerDef) const char* powerDefName,
					SA_PARAM_OP_VALID Character *characterSource,
					SA_PARAM_OP_VALID Character *characterOwner)
{
	static int *eaiAttribStacks = NULL;
	if(character)
	{
		int i;
		PowerDef *pdef = powerdef_Find(powerDefName);
		if(pdef && pdef->eType!=kPowerType_Innate && pdef->eType!=kPowerType_Combo && eaSize(&pdef->ppOrderedMods))
		{
			int iStackSize = 0;
			EntityRef erSource = characterSource ? entGetRef(characterSource->pEntParent) : 0;
			EntityRef erOwner = characterOwner ? entGetRef(characterOwner->pEntParent) : 0;

			// the increase in size will memset and initialize the array
			eaiSetSize(&eaiAttribStacks, eaSize(&pdef->ppOrderedMods));

			for(i=eaSize(&character->modArray.ppMods)-1; i>=0; i--)
			{
				AttribMod *pmod = character->modArray.ppMods[i];

				if(pdef==GET_REF(pmod->hPowerDef)
					&& (!erSource || erSource==pmod->erSource)
					&& (!erOwner || erOwner==pmod->erOwner))
				{
					eaiAttribStacks[pmod->uiDefIdx]++;
					if(eaiAttribStacks[pmod->uiDefIdx] > iStackSize)
						iStackSize = eaiAttribStacks[pmod->uiDefIdx];
				}
			}

			//Clear the array for the next time through
			eaiClearFast(&eaiAttribStacks);

			return(iStackSize);
		}
	}
	return 0;
}

// Character
//  Inputs: Character, PowerDef name
//  Return: The "stack size" of the a PowerDef affecting a character.
//   Returns 0 if no stacks, or PowerDef isn't found or isn't valid (Innate or Combo).
AUTO_EXPR_FUNC(CEFuncsCharacter, UIGen);
int AffectedByPowerCount(SA_PARAM_OP_VALID Character *character,
						 ACMD_EXPR_DICT(PowerDef) const char* powerDefName)
{
	return AffectedByPowerEx(character,powerDefName,NULL,NULL);
}

// Character
//  Inputs: Character, PowerDef name, Attrib name
//  Return: 1 if the Character has an AttribMod on them from the named PowerDef that is of the specific Attrib, otherwise 0
AUTO_EXPR_FUNC(CEFuncsCharacter);
int AffectedByPowerAttrib(SA_PARAM_OP_VALID Character *character,
						  ACMD_EXPR_DICT(PowerDef) const char* powerDefName,
						  ACMD_EXPR_ENUM(AttribType) const char *attribName)
{
	if(character)
	{
		PowerDef *pdef = powerdef_Find(powerDefName);
		int eAttrib = StaticDefineIntGetInt(AttribTypeEnum,attribName);
		if(pdef && eAttrib>=0)
		{
			int i;
			for(i=eaSize(&character->modArray.ppMods)-1; i>=0; i--)
			{
				if(pdef==GET_REF(character->modArray.ppMods[i]->hPowerDef))
				{
					AttribModDef *pmoddef = mod_GetDef(character->modArray.ppMods[i]);
					if(pmoddef && pmoddef->offAttrib==eAttrib)
					{
						return true;
					}
				}
			}
		}
	}
	return false;
}

// Character
//  Inputs: Character, PowerDef name, Attrib name, Entity attribOwner
//  Return: 1 if the Character has an AttribMod on them that is owned by the given entity, from the named PowerDef that is of the specific Attrib, otherwise 0
AUTO_EXPR_FUNC(CEFuncsCharacter);
int AffectedPowerAttribFromOwner(SA_PARAM_OP_VALID Character *character,
								 ACMD_EXPR_DICT(PowerDef) const char* powerDefName,
								 ACMD_EXPR_ENUM(AttribType) const char *attribName,
								 SA_PARAM_NN_VALID Entity *eAttribOwner)
{
	if(character && eAttribOwner)
	{
		PowerDef *pdef = powerdef_Find(powerDefName);
		EntityRef erAttribOwner = entGetRef(eAttribOwner);
		int eAttrib = StaticDefineIntGetInt(AttribTypeEnum,attribName);
		if(pdef && eAttrib>=0)
		{
			int i;
			for(i=eaSize(&character->modArray.ppMods)-1; i>=0; i--)
			{
				AttribMod *pMod = character->modArray.ppMods[i];
				if(pMod->erOwner==erAttribOwner && pdef==GET_REF(pMod->hPowerDef))
				{
					AttribModDef *pmoddef = mod_GetDef(pMod);
					if(pmoddef && pmoddef->offAttrib==eAttrib)
					{
						return true;
					}
				}
			}
		}
	}
	return false;
}

// Character
//  Inputs: Character, Attrib name
//  Return: 1 if the Character is under the effects of a mod affecting a specific attribute, otherwise 0
AUTO_EXPR_FUNC(CEFuncsCharacter, UIGen);
int AffectedByAttrib(SA_PARAM_OP_VALID Character *character,
					 ACMD_EXPR_ENUM(AttribType) const char *attribName)
{
	S32 eAttrib = StaticDefineIntGetInt(AttribTypeEnum,attribName);
	if(eAttrib >= 0 && IS_NORMAL_ATTRIB(eAttrib))
	{
		return(character_AffectedBy(character, eAttrib));
	}
	return false;
}

// Generalized AffectedByPowerTag function
static int AffectedByPowerTagEx(SA_PARAM_OP_VALID Character *character,
								const char *tagName,
								S32 bSingle,
								SA_PARAM_OP_VALID Character *characterSource,
								S32 bSourced)
{
	int iCount = 0;
	if(character)
	{
		S32 eTag = StaticDefineIntGetInt(PowerTagsEnum,tagName);
		if(eTag >= 0)
		{
			int i;
			if(entIsServer())
			{
				EntityRef erSource = characterSource ? entGetRef(characterSource->pEntParent) : 0;
				for(i=eaSize(&character->modArray.ppMods)-1; i>=0; i--)
				{
					if(!erSource || character->modArray.ppMods[i]->erSource==erSource)
					{
						AttribModDef *pmoddef = mod_GetDef(character->modArray.ppMods[i]);
						if(pmoddef && !pmoddef->bDerivedInternally && eaiFind(&pmoddef->tags.piTags,eTag) >= 0)
						{
							iCount++;
							if(bSingle)
								break;
						}
					}
				}
			}
			else if(!bSourced) // Client can't do correct source checks, so don't bother
			{
				for(i=eaSize(&character->ppModsNet)-1; i>=0; i--)
				{
					if(character->ppModsNet[i]->uiDurationOriginal)
					{
						AttribModDef *pmoddef = modnet_GetDef(character->ppModsNet[i]);
						if(pmoddef && !pmoddef->bDerivedInternally && eaiFind(&pmoddef->tags.piTags,eTag) >= 0)
						{
							iCount++;
							if(bSingle)
								break;
						}
					}
				}
			}
		}
	}
	return iCount;
}

// Character
//  Inputs: Character, PowerTag name
//  Return: 1 if the Character is under the effects of an underived AttribMod with the given PowerTag, otherwise 0
AUTO_EXPR_FUNC(CEFuncsCharacter);
int AffectedByPowerTag(SA_PARAM_OP_VALID Character *character,
					   ACMD_EXPR_ENUM(PowerTag) const char *tagName)
{
	return AffectedByPowerTagEx(character,tagName,true,NULL,false);
}

// Character
//  Inputs: Character, PowerTag name
//  Return: Number of underived AttribMods on the Character with the given PowerTag
AUTO_EXPR_FUNC(CEFuncsCharacter);
int AffectedByPowerTagCount(SA_PARAM_OP_VALID Character *character,
							ACMD_EXPR_ENUM(PowerTag) const char *tagName)
{
	return AffectedByPowerTagEx(character,tagName,false,NULL,false);
}

// Character
//  Inputs: Character, PowerTag name
//  Return: Number of underived AttribMods on the Character with the given PowerTag
AUTO_EXPR_FUNC(entityutil);
int EntIsAffectedByPowerTagCount(SA_PARAM_OP_VALID Entity* ent,
	ACMD_EXPR_ENUM(PowerTag) const char *tagName)
{
	return AffectedByPowerTagEx(ent ? ent->pChar : NULL,tagName,false,NULL,false);
}
// Character
//  Inputs: Character, PowerTag name, Character source
//  Return: Number of underived AttribMods on the Character with the given PowerTag from a specific source Character.
//    Always returns 0 on the Client.
AUTO_EXPR_FUNC(CEFuncsCharacter);
int AffectedByPowerTagCountSource(SA_PARAM_OP_VALID Character *character,
								  ACMD_EXPR_ENUM(PowerTag) const char *tagName,
								  SA_PARAM_OP_VALID Character *characterSource)
{
	return AffectedByPowerTagEx(character,tagName,false,characterSource,true);
}

// Character
//  Inputs: Character, PowerTag name, Character source
//  Return: True if any underived AttribMods on the Character with the given PowerTag from a specific source Character are found.
//    Always returns 0 on the Client.
AUTO_EXPR_FUNC(CEFuncsCharacter);
int AffectedByPowerTagSource(SA_PARAM_OP_VALID Character *character,
								ACMD_EXPR_ENUM(PowerTag) const char *tagName,
								SA_PARAM_OP_VALID Character *characterSource)	
{
	return AffectedByPowerTagEx(character, tagName, true, characterSource, true);
}

// Character
//  Inputs: Character, PowerCategory name
//  Return: 1 if the Character is under the effects of a mod from a power with the given PowerCategory, otherwise 0
AUTO_EXPR_FUNC(CEFuncsCharacter);
int AffectedByPowerCat(SA_PARAM_OP_VALID Character *character,
					   ACMD_EXPR_ENUM(PowerCategory) const char *categoryName)
{
	if(character)
	{
		S32 eCat = StaticDefineIntGetInt(PowerCategoriesEnum,categoryName);
		if(eCat >= 0)
		{
			int i;
			for(i=eaSize(&character->modArray.ppMods)-1; i>=0; i--)
			{
				PowerDef *pdef = GET_REF(character->modArray.ppMods[i]->hPowerDef);
				if(pdef && eaiFind(&pdef->piCategories,eCat) >= 0)
				{
					return true;
				}
			}
		}
	}
	return false;
}

// Application
//  Inputs: Character
//  Return: 1 if the Character is under the effects of a mod created by a previous application from this activation, otherwise 0
AUTO_EXPR_FUNC(CEFuncsApplication);
int AffectedByThisActivation(ExprContext *pContext,
							 SA_PARAM_OP_VALID Character *character)
{
	PowerApplication *papp = exprContextGetVarPointerUnsafePooled(pContext,s_pchApplication);
	if(character && papp && papp->pact && papp->pact->uiIDServer)
	{
		U32 uiID = papp->pact->uiIDServer;
		int i;
		for(i=eaSize(&character->modArray.ppMods)-1; i>=0; i--)
		{
			if(character->modArray.ppMods[i]->uiActIDServer==uiID)
				return true;
		}
	}
	return 0;
}

// Character
//  Inputs: Character, Attrib name
//  Return: Character's basic value for the Attrib
//  Override: May be overridden to specific values
AUTO_EXPR_FUNC(CEFuncsCharacter, UIGen) ACMD_NOTESTCLIENT;
F32 Attrib(SA_PARAM_OP_VALID Character *character,
		   ACMD_EXPR_ENUM(AttribType) const char *attribName)
{
	F32 r = 0;
	if(g_CombatEvalOverrides.bEnabled && g_CombatEvalOverrides.bAttrib)
	{
		S32 eAttrib = StaticDefineIntGetInt(AttribTypeEnum,attribName);
		if(eAttrib >= 0 && IS_NORMAL_ATTRIB(eAttrib))
		{
			r = *F32PTR_OF_ATTRIB(&g_CombatEvalOverrides.attribs,eAttrib);
		}
	}
	else if(character && character->pattrBasic)
	{
		S32 eAttrib = StaticDefineIntGetInt(AttribTypeEnum,attribName);
		if(eAttrib >= 0 && IS_NORMAL_ATTRIB(eAttrib))
		{
			r = *F32PTR_OF_ATTRIB(character->pattrBasic,eAttrib);
		}
	}
	return r;
}

// Character
//  Inputs: Character, Attrib name, boolean for self passive mods
//  Return: Character's basic value for the Attrib from generally self-derived sources - e.g. the
//   CharacterClass base value + innates + (optionally) self passive mods.
AUTO_EXPR_FUNC(CEFuncsCharacter);
F32 AttribBasicNatural(ACMD_EXPR_PARTITION iPartitionIdx,
					   SA_PARAM_OP_VALID Character *character,
					   ACMD_EXPR_ENUM(AttribType) const char *attribName,
					   S32 includeSelfPassives)
{
	// This is a fairly dumb function, since it doesn't take everything possible into
	//  account, but writing the full generic solution is a pain and this knock-off
	//  should be sufficient for intended usage.
	F32 r = 0;
	if(character)
	{
		EntityRef er = entGetRef(character->pEntParent);
		F32 fBasicBase, fBasicAbs, fBasicFactPos, fBasicFactNeg;
		S32 eAttrib = StaticDefineIntGetInt(AttribTypeEnum,attribName);
		AttribAccrualSet *pInnateAccrualSet = character_GetInnateAccrual(iPartitionIdx, character, NULL);

		fBasicBase = character_GetClassAttrib(character,kClassAttribAspect_Basic,eAttrib);
		fBasicAbs = pInnateAccrualSet ? *F32PTR_OF_ATTRIB(&pInnateAccrualSet->CharacterAttribs.attrBasicAbs,eAttrib) : 0;
		fBasicFactPos = pInnateAccrualSet ? *F32PTR_OF_ATTRIB(&pInnateAccrualSet->CharacterAttribs.attrBasicFactPos,eAttrib) : 0;
		fBasicFactNeg = pInnateAccrualSet ? *F32PTR_OF_ATTRIB(&pInnateAccrualSet->CharacterAttribs.attrBasicFactNeg,eAttrib) : 0;

		if(includeSelfPassives)
		{
			int i;
			for(i=eaSize(&character->modArray.ppMods)-1; i>=0; i--)
			{
				AttribModDef *pmoddefChar = mod_GetDef(character->modArray.ppMods[i]);
				if(pmoddefChar
					&& pmoddefChar->offAttrib==eAttrib 
					&& IS_BASIC_ASPECT(pmoddefChar->offAspect)
					&& !character->modArray.ppMods[i]->bIgnored
					&& pmoddefChar->pPowerDef->eType==kPowerType_Passive
					&& character->modArray.ppMods[i]->erSource == er)
				{
					F32 fMagnitude = mod_GetEffectiveMagnitude(iPartitionIdx, character->modArray.ppMods[i],pmoddefChar,character);

					switch(pmoddefChar->offAspect)
					{
					case kAttribAspect_BasicAbs:
						fBasicAbs += fMagnitude;
						break;
					case kAttribAspect_BasicFactPos:
						fBasicFactPos += fMagnitude;
						break;
					case kAttribAspect_BasicFactNeg:
						fBasicFactNeg += fMagnitude;
						break;
					}
				}
			}
		}

		r = fBasicBase * (1 + fBasicFactPos) / (1 + fBasicFactNeg) + fBasicAbs;
	}

	return r;
}


// Character
//   innates + self passive mods.
AUTO_EXPR_FUNC(CEFuncsCharacter);
F32 AttribBasicByTag(	ACMD_EXPR_PARTITION iPartitionIdx,
						SA_PARAM_OP_VALID Character *character,
						ACMD_EXPR_ENUM(AttribType) const char *attribName,
						ACMD_EXPR_ENUM(PowerTag) const char *tagName)
{
	S32 eTag = StaticDefineIntGetInt(PowerTagsEnum, tagName);
	S32 eAttrib = StaticDefineIntGetInt(AttribTypeEnum, attribName);
	F32 fBasicMag = 0;

	if (eTag >= 0 && eAttrib >= 0)
	{
		// first go through innates
		if (character->pInnateAttribModData)
		{
			FOR_EACH_IN_EARRAY(character->pInnateAttribModData->ppInnateAttribMods, InnateAttribMod, pInnateMod)
			{
				if (pInnateMod->eAttrib == eAttrib && pInnateMod->eAspect == kAttribAspect_BasicAbs)
				{
					PowerDef *pDef = GET_REF(pInnateMod->hPowerDef);
					if (pDef)
					{
						if (eaiFind(&pDef->tags.piTags, eTag) >= 0)
						{
							fBasicMag += pInnateMod->fMag;
						}
					}
				}
			}
			FOR_EACH_END
		}
		
	
		FOR_EACH_IN_EARRAY(character->modArray.ppMods, AttribMod, pAttribMod)
		{
			if (!pAttribMod->bIgnored)
			{
				AttribModDef *pModDef = mod_GetDef(pAttribMod);
				if(pModDef
					&& !pModDef->pExprAffects // don't allow anything with an affects to be counted
					&& pModDef->offAspect == kAttribAspect_BasicAbs
					&& pModDef->offAttrib == eAttrib 
					&& pModDef->pPowerDef->eType == kPowerType_Passive
					&& eaiFind(&pModDef->pPowerDef->tags.piTags, eTag) >= 0)
				{
					fBasicMag += pAttribMod->fMagnitude;
				}
			}
		}
		FOR_EACH_END
	}

	return fBasicMag;
}

// Character
//  Inputs: Character, Attrib name
//  Return: Character's basic value for the Attrib from the class table ONLY.
AUTO_EXPR_FUNC(CEFuncsCharacter);
F32 AttribBasicClass(ACMD_EXPR_PARTITION iPartitionIdx,
						SA_PARAM_OP_VALID Character *character,
						ACMD_EXPR_ENUM(AttribType) const char *attribName)
{
	F32 r = 0;
	if(character)
	{
		S32 eAttrib = StaticDefineIntGetInt(AttribTypeEnum,attribName);
		r = character_GetClassAttrib(character, kClassAttribAspect_Basic, eAttrib);
	}

	return r;
}

// Character
//  Inputs: Character, AttribModDef
//  Return: Character's basic value for the Attrib type of the ModDef from the class table ONLY.
AUTO_EXPR_FUNC(CEFuncsCharacter);
F32 AttribBasicClassByModDefAttrib(ACMD_EXPR_PARTITION iPartitionIdx,
									SA_PARAM_OP_VALID Character *character,
									SA_PARAM_OP_VALID AttribModDef_ForExpr *modDef)
{
	F32 r = 0;
	if(character && modDef)
	{
		r = character_GetClassAttrib(character, kClassAttribAspect_Basic, modDef->offAttrib);
	}

	return r;
}

// Character
//  Inputs: Character, Attrib name
//  Return: Character's generic strength for the Attrib
AUTO_EXPR_FUNC(CEFuncsCharacter, UIGen) ACMD_NOTESTCLIENT;
F32 AttribStrength(SA_PARAM_OP_VALID Character *character,
				   ACMD_EXPR_ENUM(AttribType) const char *attribName)
{
	F32 r = 1;
	if(character)
	{
		S32 eAttrib = StaticDefineIntGetInt(AttribTypeEnum,attribName);
		if(eAttrib >= 0 && IS_NORMAL_ATTRIB(eAttrib))
		{
			Entity *pEnt = character->pEntParent;
			r = character_GetStrengthGeneric(entGetPartitionIdx(pEnt),character,eAttrib, NULL);
		}
	}
	return r;
}

// Character
//  Inputs: Character, Attrib name
//  Return: Character's generic resistance for the Attrib
AUTO_EXPR_FUNC(CEFuncsCharacter, UIGen) ACMD_NOTESTCLIENT;
F32 AttribResist(SA_PARAM_OP_VALID Character *character,
				 ACMD_EXPR_ENUM(AttribType) const char *attribName)
{
	F32 r = 1;
	if(character)
	{
		S32 eAttrib = StaticDefineIntGetInt(AttribTypeEnum,attribName);
		if(eAttrib >= 0 && IS_NORMAL_ATTRIB(eAttrib))
		{
			Entity *pEnt = character->pEntParent;
			F32 fImmune = 0;
			F32 fResistTrue = 1.f;
			r = character_GetResistGeneric(entGetPartitionIdx(pEnt),character,eAttrib,&fResistTrue,&fImmune);
			if (r != 0.f)
				r = fResistTrue / r;
		}
	}
	return r;
}



//  Inputs: Character, Attrib, boolean for self passive mods
//  Return: Character's ResTrue value for the Attrib from generally self-derived sources - e.g. the
//   innates + (optionally) self passive mods. 
AUTO_EXPR_FUNC(CEFuncsCharacter);
F32 AttribResTrueNatural(	ACMD_EXPR_PARTITION iPartitionIdx,
							SA_PARAM_OP_VALID Character *character,
							ACMD_EXPR_ENUM(AttribType) const char *attribName,
							S32 includeSelfPassives)
{
	S32 eAttribType = StaticDefineIntGetInt(AttribTypeEnum, attribName);
	if(character && IS_NORMAL_ATTRIB(eAttribType))
	{
		EntityRef er = entGetRef(character->pEntParent);
		F32 fResTrue;
		AttribAccrualSet *pInnateAccrualSet = character_GetInnateAccrual(iPartitionIdx, character, NULL);
				
		fResTrue = pInnateAccrualSet ? *F32PTR_OF_ATTRIB(&pInnateAccrualSet->CharacterAttribs.attrResTrue,eAttribType) : 0.0f;
				
		if(includeSelfPassives)
		{
			FOR_EACH_IN_EARRAY(character->modArray.ppMods, AttribMod, pAttribMod)
			{
				if (!pAttribMod->bIgnored)
				{
					AttribModDef *pModDef = mod_GetDef(pAttribMod);
					if(pModDef
						&& !pModDef->pExprAffects // don't allow anything with an affects to be counted
						&& pModDef->offAspect == kAttribAspect_ResTrue
						&& pModDef->offAttrib == eAttribType 
						&& pModDef->pPowerDef->eType == kPowerType_Passive)
					{
						fResTrue += pAttribMod->fMagnitude;
					}
				}
			}
			FOR_EACH_END
		}

		return fResTrue;
	}

	return 0.f;
}


// Character
//  Inputs: Character, Attrib name
//  Return: 1 if Character has generic immunity for the Attrib, otherwise 0
AUTO_EXPR_FUNC(CEFuncsCharacter, UIGen) ACMD_NOTESTCLIENT;
S32 AttribImmune(SA_PARAM_OP_VALID Character *character,
				 ACMD_EXPR_ENUM(AttribType) const char *attribName)
{
	S32 r = 0;
	if(character)
	{
		S32 eAttrib = StaticDefineIntGetInt(AttribTypeEnum,attribName);
		if(eAttrib >= 0 && IS_NORMAL_ATTRIB(eAttrib))
		{
			Entity *pEnt = character->pEntParent;
			F32 fImmune = 0;
			character_GetResistGeneric(entGetPartitionIdx(pEnt),character,eAttrib,NULL,&fImmune);
			r = (fImmune > 0);
		}
	}
	return r;
}

// Character
//  Inputs: Character, Attrib name, boolean for self passive mods
//  Return: 1 if Character has generic immunity for the Attrib from generally self-derived sources, otherwise 0
AUTO_EXPR_FUNC(CEFuncsCharacter);
S32 AttribImmuneNatural(ACMD_EXPR_PARTITION iPartitionIdx,
						SA_PARAM_OP_VALID Character *character,
						ACMD_EXPR_ENUM(AttribType) const char *attribName,
						S32 includeSelfPassives)
{
	// Very similar to AttribBasicNatural, which is dumb
	F32 r = 0;
	if(character)
	{
		EntityRef er = entGetRef(character->pEntParent);
		F32 fImmune;
		S32 eAttrib = StaticDefineIntGetInt(AttribTypeEnum,attribName);
		AttribAccrualSet *pInnateAccrualSet = character_GetInnateAccrual(iPartitionIdx, character, NULL);

		fImmune = pInnateAccrualSet ? *F32PTR_OF_ATTRIB(&pInnateAccrualSet->CharacterAttribs.attrImmunity,eAttrib) : 0;

		if(includeSelfPassives)
		{
			int i;
			for(i=eaSize(&character->modArray.ppMods)-1; i>=0; i--)
			{
				AttribModDef *pmoddefChar = mod_GetDef(character->modArray.ppMods[i]);
				if(pmoddefChar
					&& pmoddefChar->offAttrib==eAttrib 
					&& pmoddefChar->offAspect==kAttribAspect_Immunity
					&& !character->modArray.ppMods[i]->bIgnored
					&& pmoddefChar->pPowerDef->eType==kPowerType_Passive
					&& character->modArray.ppMods[i]->erSource == er)
				{
					F32 fMagnitude = mod_GetEffectiveMagnitude(iPartitionIdx,character->modArray.ppMods[i],pmoddefChar,character);
					fImmune += fMagnitude;
				}
			}
		}

		r = (fImmune > 0);
	}

	return r;
}

// Character
//  Inputs: Character
//  Return: 1 if the Character is currently under the state of knockback/up, otherwise 0
AUTO_EXPR_FUNC(CEFuncsCharacter);
S32 Knocked(SA_PARAM_OP_VALID Character *character)
{
#if !GAMESERVER && !GAMECLIENT
	assert(0);
#else
	S32 r = 0;
	if(character)
	{
		r = !!pmKnockIsActive(character->pEntParent);
	}
	return r;
#endif
}


// Character
//  Inputs: Character, Attrib name
//  Return: Points spent on that attrib in the stats system
AUTO_EXPR_FUNC(CEFuncsCharacter, UIGen) ACMD_NOTESTCLIENT;
int StatPointsInAttrib(SA_PARAM_OP_VALID Character *character,
					   ACMD_EXPR_ENUM(AttribType) const char *attribName)
{
	int r = 0;
	if(character)
	{
		S32 eAttrib = StaticDefineIntGetInt(AttribTypeEnum,attribName);
		if(eAttrib >= 0 && IS_NORMAL_ATTRIB(eAttrib))
		{
			r = character_StatPointsSpentPerAttrib(character,eAttrib);
		}
	}

	return r;
}

#define SUPERSTATS_ALL "SuperStats"
#define SUPERSTATS_PRIMARY "SuperStats."
#define SUPERSTATS_SECONDARY "SuperStats_Secondary."

static int SuperStatAttrib(int iPartitionIdx,  SA_PARAM_OP_VALID Character *pchar, const char *superStatType, int iSubStatNum)
{
	if(pchar)
	{
		static U32* eaAttribs = NULL;
		CharacterClass *pClass = character_GetClassCurrent(pchar);
		PowerStat **ppStats = pClass && pClass->ppStatsFull ? pClass->ppStatsFull : g_PowerStats.ppPowerStats;
		int i,j;

		if(!eaAttribs)
			ea32Create(&eaAttribs);
		else
			ea32Clear(&eaAttribs);

		for(i=eaSize(&ppStats)-1; i>=0; i--)
		{
			PowerStat *pStat = ppStats[i];

			// If the stat doesn't require a "superstat" power tree node then continue
			if(!pStat->pchPowerTreeNodeRequired 
				|| strnicmp(superStatType, pStat->pchPowerTreeNodeRequired, strlen(superStatType)))
				continue;

			if(!powerstat_Active(pStat,pchar,NULL))
				continue;

			for(j=eaSize(&pStat->ppSourceStats)-1; j>=0; j--)
			{
				ea32PushUnique(&eaAttribs, pStat->ppSourceStats[j]->offSourceAttrib);
			}

			if(iSubStatNum < ea32Size(&eaAttribs))
			{
				return eaAttribs[iSubStatNum];
			}
		}
	}

	return -1;
}

// Returns the sum of the Character's SuperStats.  CO specific.
static F32 SuperStatSumEx(int iPartitionIdx, SA_PARAM_OP_VALID Character *pchar, S32 bNatural, const char *superStatType)
{
	F32 r = 0.f;
	
	if(pchar)
	{
		U32* eaAttribs = NULL;

		CharacterClass *pClass = character_GetClassCurrent(pchar);
		PowerStat **ppStats = pClass && pClass->ppStatsFull ? pClass->ppStatsFull : g_PowerStats.ppPowerStats;
		AttribAccrualSet *pInnateAccrualSet = bNatural ? character_GetInnateAccrual(iPartitionIdx,pchar,NULL) : NULL;
		int i, j, attribSize;

		for(i=eaSize(&ppStats)-1; i>=0; i--)
		{
			PowerStat *pStat = ppStats[i];

			// If the stat doesn't require a "superstat" power tree node then continue
			if(!pStat->pchPowerTreeNodeRequired 
				|| strnicmp(superStatType, pStat->pchPowerTreeNodeRequired, strlen(superStatType)))
				continue;

			if(!powerstat_Active(pStat,pchar,NULL))
				continue;

			for(j=eaSize(&pStat->ppSourceStats)-1; j>=0; j--)
			{
				attribSize = ea32Size(&eaAttribs);
				ea32PushUnique(&eaAttribs, pStat->ppSourceStats[j]->offSourceAttrib);
				if(attribSize == ea32Size(&eaAttribs))
					continue;

				if(bNatural)
				{
					r += pInnateAccrualSet ? *F32PTR_OF_ATTRIB(&pInnateAccrualSet->CharacterAttribs.attrBasicAbs,pStat->ppSourceStats[j]->offSourceAttrib) : 0;
					r += character_GetClassAttrib(pchar,kClassAttribAspect_Basic,pStat->ppSourceStats[j]->offSourceAttrib);
				}
				else
				{
					r += *F32PTR_OF_ATTRIB(pchar->pattrBasic,pStat->ppSourceStats[j]->offSourceAttrib);
				}
			}
		}

		ea32Destroy(&eaAttribs);
	}

	return r;
}

// Character
// Inputs: Character
// Return: The attrib that represents the primary super stat on that character. Current hard-coded to work for Champs.
AUTO_EXPR_FUNC(CEFuncsCharacter);
int GetPrimarySuperStat(ACMD_EXPR_PARTITION iPartitionIdx, SA_PARAM_OP_VALID Character *character, int iSubStatNum)
{
	return SuperStatAttrib(iPartitionIdx,character,SUPERSTATS_PRIMARY,iSubStatNum-1);
}

// Character
// Inputs: Character
// Return: The attrib that represents the secondary super stat on that character. Current hard-coded to work for Champs.
AUTO_EXPR_FUNC(CEFuncsCharacter);
int GetSecondarySuperStat(ACMD_EXPR_PARTITION iPartitionIdx, SA_PARAM_OP_VALID Character *character, int iSubStatNum)
{
	return SuperStatAttrib(iPartitionIdx,character,SUPERSTATS_SECONDARY,iSubStatNum-1);
}

// Character
//  Inputs: Character
//  Return: The sum of the primary super stats a character has.  Currently hard-coded to work for Champs.  
//   If they bought SuperInt and SuperCon, and the character has 75 Int and 80 Con, the function would return 155.
AUTO_EXPR_FUNC(CEFuncsCharacter);
F32 PrimarySuperStatSum(ACMD_EXPR_PARTITION iPartitionIdx, SA_PARAM_OP_VALID Character *character)
{
	return SuperStatSumEx(iPartitionIdx,character,false,SUPERSTATS_PRIMARY);
}

// Character
//  Inputs: Character
//  Return: The sum of the primary super stats a character has.  Currently hard-coded to work for Champs.  
//   If they bought SuperInt and SuperCon, and the character has 75 Int and 80 Con, the function would return 155.
AUTO_EXPR_FUNC(CEFuncsCharacter);
F32 PrimarySuperStatSumNatural(ACMD_EXPR_PARTITION iPartitionIdx, SA_PARAM_OP_VALID Character *character)
{
	return SuperStatSumEx(iPartitionIdx,character,true,SUPERSTATS_PRIMARY);
}

// Character
//  Inputs: Character
//  Return: The sum of the secondary super stats a character has.  Currently hard-coded to work for Champs.  
//   If they bought SuperInt and SuperCon, and the character has 75 Int and 80 Con, the function would return 155.
AUTO_EXPR_FUNC(CEFuncsCharacter);
F32 SecondarySuperStatSum(ACMD_EXPR_PARTITION iPartitionIdx, SA_PARAM_OP_VALID Character *character)
{
	return SuperStatSumEx(iPartitionIdx,character,false,SUPERSTATS_SECONDARY);
}

// Character
//  Inputs: Character
//  Return: The sum of the secondary super stats a character has.  Currently hard-coded to work for Champs.  
//   If they bought SuperInt and SuperCon, and the character has 75 Int and 80 Con, the function would return 155.
AUTO_EXPR_FUNC(CEFuncsCharacter);
F32 SecondarySuperStatSumNatural(ACMD_EXPR_PARTITION iPartitionIdx, SA_PARAM_OP_VALID Character *character)
{
	return SuperStatSumEx(iPartitionIdx,character,true,SUPERSTATS_SECONDARY);
}

// Character
//  Inputs: Character
//  Return: The sum of the super stats a character has.  Currently hard-coded to work for Champs.  
//   If they bought SuperInt and SuperCon, and the character has 75 Int and 80 Con, the function would return 155.
AUTO_EXPR_FUNC(CEFuncsCharacter);
F32 SuperStatSum(ACMD_EXPR_PARTITION iPartitionIdx, SA_PARAM_OP_VALID Character *character)
{
	return SuperStatSumEx(iPartitionIdx,character,false,SUPERSTATS_ALL);
}

// Character
//  Inputs: Character
//  Return: The sum of the natural super stats a character has.  Currently hard-coded to work for Champs.  
AUTO_EXPR_FUNC(CEFuncsCharacter);
F32 SuperStatSumNatural(ACMD_EXPR_PARTITION iPartitionIdx, SA_PARAM_OP_VALID Character *character)
{
	return SuperStatSumEx(iPartitionIdx, character,true,SUPERSTATS_ALL);
}


// Character
//  Inputs: Character
//  Return: Character's current health divided by their max health
//  TODO(JW): Deprecate with change to Attribute system
AUTO_EXPR_FUNC(CEFuncsCharacter) ACMD_NOTESTCLIENT;
F32 HealthPct(SA_PARAM_OP_VALID Character *character)
{
	F32 r = 0;
	if(character && character->pattrBasic && character->pattrBasic->fHitPointsMax>0)
	{
		r = character->pattrBasic->fHitPoints/character->pattrBasic->fHitPointsMax;
	}
	return r;
}

// Character
//  Inputs: Character
//  Return: Character's current power divided by their max power
//  TODO(JW): Deprecate with change to Attribute system
AUTO_EXPR_FUNC(CEFuncsCharacter) ACMD_NOTESTCLIENT;
F32 PowerPct(SA_PARAM_OP_VALID Character *character)
{
	F32 r = 0;
	if(character && character->pattrBasic && character->pattrBasic->fPowerMax>0)
	{
		r = character->pattrBasic->fPower/character->pattrBasic->fPowerMax;
	}
	return r;
}

// Special version of AttribModMagnitudePct which filters ModNets by arc/yaw coverage vs
//  a specific input angle.  Used to find directional Shields.
AttribModNet* ShieldPctOrientedAttrib(SA_PARAM_OP_VALID Character *pChar, F32 fAngle)
{
	static AttribModNet **s_ppModNets = NULL;
	eaClearFast(&s_ppModNets);

	if(pChar)
	{
		int i;

		fAngle = RAD(fAngle);

		for(i=eaSize(&pChar->ppModsNet)-1; i>=0; i--)
		{
			AttribModDef *pmoddef;
			AttribModNet *pNet = pChar->ppModsNet[i];

			if(!ATTRIBMODNET_VALID(pNet))
				continue;

			pmoddef = modnet_GetDef(pNet);

			if(pmoddef && pmoddef->offAttrib==kAttribType_Shield)
			{
				// If this affects a limited arc, see if it handles the fAngle we were given
				if(pmoddef->fArcAffects > 0)
				{
					F32 fAngleDiff = subAngle(fAngle,RAD(pmoddef->fYaw));
					if(fabs(fAngleDiff) > RAD(pmoddef->fArcAffects)/2.f)
						continue;
				}
				eaPush(&s_ppModNets,pNet);
			}
		}

		if(eaSize(&s_ppModNets))
		{
			eaQSort(s_ppModNets,modnet_CmpDurationDefIdx);
			return s_ppModNets[0];
		}
	}
	return NULL;
}

AUTO_EXPR_FUNC(CEFuncsCharacter);
F32 ShieldMag(SA_PARAM_OP_VALID Character *pChar, F32 fAngle)
{
	AttribModNet *pNet = ShieldPctOrientedAttrib(pChar, fAngle);
	if (pNet)
	{
		return (F32)pNet->iHealth;
	}
	return 0;
}

// Character
//  Inputs: Character, Angle(Degrees)
//  Return: Character's shield health at a particular angle
AUTO_EXPR_FUNC(CEFuncsCharacter);
F32 ShieldPct(SA_PARAM_OP_VALID Character *pChar, F32 fAngle)
{
	AttribModNet *pNet = ShieldPctOrientedAttrib(pChar, fAngle);
	if (pNet)
	{
		if(pNet->iHealthMax)
			return (F32)pNet->iHealth / (F32)pNet->iHealthMax;
		else
			return 1.0f;
	}
	return -1.0f;
}

// Character
//  Inputs: Character
//  Return: 1 if the Character is a Player, otherwise 0
AUTO_EXPR_FUNC(CEFuncsCharacter);
S32 CharIsPlayer(SA_PARAM_OP_VALID Character *character)
{
	S32 r = (character && entCheckFlag(character->pEntParent,ENTITYFLAG_IS_PLAYER));
	return r;
}


// Character
//  Inputs: Character, CritterTag name
//  Return: 1 if the Character is a critter with the named tag, otherwise 0
AUTO_EXPR_FUNC(CEFuncsCharacter) ACMD_NOTESTCLIENT;
int HasCritterTag(SA_PARAM_OP_VALID Character *character,
				  ACMD_EXPR_ENUM(CritterTag) const char *tagName)
{
	if(character && character->pEntParent && character->pEntParent->pCritter)
	{
		CritterDef *pdef = GET_REF(character->pEntParent->pCritter->critterDef);
		if(pdef)
		{
			int tag = StaticDefineIntGetInt(CritterTagsEnum,tagName);
			if ( ea32Find(&pdef->piTags, tag) >= 0 )
			{
				return 1;
			}
		}
	}

	return 0;
}

// Character
//  Inputs: Character, CritterTag name
//  Return: 0 if the Character is a critter with the named tag, otherwise 1
AUTO_EXPR_FUNC(CEFuncsCharacter) ACMD_NOTESTCLIENT;
int NotHasCritterTag(SA_PARAM_OP_VALID Character *character,
					 ACMD_EXPR_ENUM(CritterTag) const char *tagName)
{
	return !HasCritterTag(character,tagName);
}

// Character
//  Inputs: Character, CritterRank name
//  Return: 1 if the Character is a critter with the named rank, otherwise 0
AUTO_EXPR_FUNC(CEFuncsCharacter);
int IsCritterRank(SA_PARAM_OP_VALID Character *character,
				  const char *rankName)
{
	int r = 0;
	if(character && character->pEntParent && character->pEntParent->pCritter)
	{
		Critter *pCritter = character->pEntParent->pCritter;
		r = (stricmp(pCritter->pcRank, rankName) == 0);
	}
	return r;
}

// Character
//  Inputs: Character, CritterRank name
//  Return: 0 if the Character is a critter with the named rank, otherwise 1
AUTO_EXPR_FUNC(CEFuncsCharacter);
int NotIsCritterRank(SA_PARAM_OP_VALID Character *character,
					 const char *rankName)
{
	return !IsCritterRank(character,rankName);
}

// Character
//  Inputs: Character, CritterSubRank name
//  Return: 1 if the Character is a critter with the named subrank, otherwise 0
AUTO_EXPR_FUNC(CEFuncsCharacter);
int IsCritterSubRank(SA_PARAM_OP_VALID Character *character,
				  const char *subrankName)
{
	int r = 0;
	if(character && character->pEntParent && character->pEntParent->pCritter)
	{
		Critter *pCritter = character->pEntParent->pCritter;
		r = (stricmp(pCritter->pcSubRank, subrankName) == 0);
	}
	return r;
}

// Character
//  Inputs: Character, CritterSubRank name
//  Return: 0 if the Character is a critter with the named subrank, otherwise 1
AUTO_EXPR_FUNC(CEFuncsCharacter);
int NotIsCritterSubRank(SA_PARAM_OP_VALID Character *character,
					 const char *subrankName)
{
	return !IsCritterSubRank(character,subrankName);
}

// Character
//  Inputs: Character, CritterGroup name
//  Return: 1 if the Character is a critter with the named group, otherwise 0
AUTO_EXPR_FUNC(CEFuncsCharacter);
int IsCritterGroup(SA_PARAM_OP_VALID Character *character,
				   ACMD_EXPR_DICT(CritterGroup) const char *groupName)
{
	int r = 0;
	if(character && character->pEntParent && character->pEntParent->pCritter)
	{
		CritterDef *pdef = GET_REF(character->pEntParent->pCritter->critterDef);
		if(pdef && IS_HANDLE_ACTIVE(pdef->hGroup))
		{
			r = !stricmp(REF_STRING_FROM_HANDLE(pdef->hGroup),groupName);
		}
	}
	return r;
}

// Character
//  Inputs: Character, CritterGroup name
//  Return: 0 if the Character is a critter with the named group, otherwise 1
AUTO_EXPR_FUNC(CEFuncsCharacter);
int NotIsCritterGroup(SA_PARAM_OP_VALID Character *character,
					  ACMD_EXPR_DICT(CritterGroup) const char *groupName)
{
	return !IsCritterGroup(character,groupName);
}

// Character
//  Inputs: Character, CritterVar name
//  Return: The value of the CritterVar for the Character, or 0
AUTO_EXPR_FUNC(CEFuncsCharacter) ACMD_NAME(CritterVar);
F32 CritterVarLookup(SA_PARAM_OP_VALID Character *character, const char *critterVarName)
{
	F32 r = 0;
	if(character && character->pEntParent && character->pEntParent->pCritter)
	{
		CritterDef *pdef = GET_REF(character->pEntParent->pCritter->critterDef);
		if(pdef)
		{
			int i;
			for(i=eaSize(&pdef->ppCritterVars)-1; i>=0; i--)
			{
				if(!stricmp(pdef->ppCritterVars[i]->var.pcName,critterVarName))
				{
					if(pdef->ppCritterVars[i]->var.eType==WVAR_FLOAT)
						r = pdef->ppCritterVars[i]->var.fFloatVal;
					else if(pdef->ppCritterVars[i]->var.eType==WVAR_INT)
						r = pdef->ppCritterVars[i]->var.iIntVal;

					break;
				}
			}
		}
	}

	return r;
}

// Character
//  Inputs: Character, name
//  Return: 1 if the Character has the given name, otherwise 0
AUTO_EXPR_FUNC(CEFuncsCharacter) ACMD_NOTESTCLIENT;
int IsNamed(SA_PARAM_OP_VALID Character *character,
			const char *name)
{
	int r = 0;
	char *pchName = NULL;
	if(character && character->pEntParent && character->pEntParent->pCritter)
	{
		CritterDef *pdef = GET_REF(character->pEntParent->pCritter->critterDef);
		if(pdef)
		{
			pchName = pdef->pchName;
		}
	}

	if(pchName && !stricmp(name,pchName))
	{
		r = 1;
	}

	return r;
}

// Character
//  Inputs: Character, name
//  Return: 0 if the Character has the given name, otherwise 1
AUTO_EXPR_FUNC(CEFuncsCharacter) ACMD_NOTESTCLIENT;
int NotIsNamed(SA_PARAM_OP_VALID Character *character,
			   const char *name)
{
	return !IsNamed(character,name);
}

// Character
//  Inputs: Character, PowerDef name
//  Return: 1 if the Character owns a Power with the named PowerDef, otherwise 0
AUTO_EXPR_FUNC(CEFuncsCharacter) ACMD_NOTESTCLIENT;
int OwnsPower(SA_PARAM_OP_VALID Character *character,
			  ACMD_EXPR_DICT(PowerDef) const char* powerDefName)
{
	return (character && character_FindPowerByName(character,powerDefName));
}

// Character
//  Inputs: Character, PowerDef name
//  Return: 0 if the Character owns a Power with the named PowerDef, otherwise 1
AUTO_EXPR_FUNC(CEFuncsCharacter) ACMD_NOTESTCLIENT;
int NotOwnsPower(SA_PARAM_OP_VALID Character *character,
				 ACMD_EXPR_DICT(PowerDef) const char* powerDefName)
{
	return !OwnsPower(character,powerDefName);
}
// Character
// Inputs: Character
// Returns: Empty string if no costume, otherwise the name of the costume currently active
AUTO_EXPR_FUNC(CEFuncsCharacter);
const char *CostumeName(SA_PARAM_OP_VALID Character *character)
{
	if(character && character->pEntParent)
	{
		PlayerCostume *pCostume = costumeEntity_GetEffectiveCostume(character->pEntParent);
		if(pCostume)
		{
			return(pCostume->pcName);
		}
	}
	return("");
}

// Character
//  Inputs: Character
//  Return: Returns the characters number of controllable pets whose entity reference can be resolved
//  Note: Does not function on the client for character's other than the client's character
AUTO_EXPR_FUNC(CEFuncsCharacter) ACMD_NAME(PetControlledCount);
int ControlledPetCount(ACMD_EXPR_PARTITION iPartitionIdx, SA_PARAM_OP_VALID Character *character)
{
	int r = 0;
	if( character
		&& character->pEntParent
		&& character->pEntParent->pPlayer)
	{
		int i = 0;
		for(i=eaSize(&character->pEntParent->pPlayer->petInfo)-1; i>=0; i--)
		{
			if(entFromEntityRef(iPartitionIdx, character->pEntParent->pPlayer->petInfo[i]->iPetRef) != NULL)
				r++;
		}
	}

	return r;
}

// Character
//  Inputs: Character, variable name
//  Return: Returns the sum of the given variable for all of a character's controllable pets.
//  Note: Does not function on the client for characters other than the client's character
AUTO_EXPR_FUNC(CEFuncsCharacter);
F32 ControlledPetVarSum(ACMD_EXPR_PARTITION iPartitionIdx, SA_PARAM_OP_VALID Character *character, const char *pchVar)
{
	F32 f = 0.0f;

	if(character
		&& character->pEntParent
		&& character->pEntParent->pPlayer
		&& pchVar
		&& *pchVar)
	{
		int i = 0;
		for(i=eaSize(&character->pEntParent->pPlayer->petInfo)-1; i>=0; i--)
		{
			Entity *pent = entFromEntityRef(iPartitionIdx, character->pEntParent->pPlayer->petInfo[i]->iPetRef);
			if(pent != NULL && pent->pCritter != NULL)
			{
				CritterDef *pDef = GET_REF(pent->pCritter->critterDef);
				CritterGroup *pGroup;
				if(pDef)
				{
					bool bFound = false;
					int j;

					for(j=eaSize(&pDef->ppCritterVars)-1; j>=0; j--)
					{
						WorldVariable *pVar = &pDef->ppCritterVars[j]->var;
						if (pVar->pcName && (stricmp(pVar->pcName, pchVar) == 0))
						{
							if(pVar->eType == WVAR_FLOAT)
							{
								f += pVar->fFloatVal;
							}
							else if(pVar->eType == WVAR_INT)
							{
								f += (F32)pVar->iIntVal;
							}

							bFound = true;
							break;
						}
					}

					// If the var wasn't found on the critter, check the critter group
					if(!bFound && (pGroup=GET_REF(pDef->hGroup))!=NULL)
					{
						for(j=eaSize(&pGroup->ppCritterVars)-1; j>=0; j--)
						{
							WorldVariable *pVar = &pGroup->ppCritterVars[j]->var;
							if(pVar->pcName && (stricmp(pVar->pcName, pchVar) == 0)) 
							{
								if(pVar->eType == WVAR_FLOAT)
								{
									f += pVar->fFloatVal;
								}
								else if(pVar->eType == WVAR_INT)
								{
									f += (F32)pVar->iIntVal;
								}

								break;
							}
						}
					}
				}
				
			}
		}
	}

	return f;
}


// Character
// Inputs: Character, Character Target
// Returns: 1 if the target character is a new recruit of the source character.  This is the new Referal/Buddy system.
AUTO_EXPR_FUNC(CEFuncsCharacter);
int TargetIsNewRecruit(SA_PARAM_OP_VALID Character *sourceCharacter,
					SA_PARAM_OP_VALID Character *targetCharacter)
{
	if(sourceCharacter && sourceCharacter->pEntParent &&
		targetCharacter && targetCharacter->pEntParent)
	{
		return(entity_IsNewRecruit(sourceCharacter->pEntParent,targetCharacter->pEntParent));
	}

	return(0);
}

// Character
// Inputs: Character, Character Target
// Returns: 1 if the target character is a new recruiter of the source character.  This is the new Referal/Buddy system.
AUTO_EXPR_FUNC(CEFuncsCharacter);
int TargetIsNewRecruiter(SA_PARAM_OP_VALID Character *sourceCharacter,
					  SA_PARAM_OP_VALID Character *targetCharacter)
{
	if(sourceCharacter && sourceCharacter->pEntParent &&
		targetCharacter && targetCharacter->pEntParent)
	{
		return(entity_IsNewRecruiter(sourceCharacter->pEntParent,targetCharacter->pEntParent));
	}

	return(0);
}

// Returns the number of times the character has the specified personal power mode against any target.
AUTO_EXPR_FUNC(CEFuncsCharacter) ACMD_NAME("CountOutgoingPersonalPowerMode");
S32 exprCountOutgoingPersonalPowerMode(SA_PARAM_OP_VALID Character *pChar, ACMD_EXPR_ENUM(PowerMode) const char *modeName)
{
	int iMode = StaticDefineIntGetInt(PowerModeEnum,modeName);
	if (pChar)
	{
		return character_CountModePersonalAnyTarget(pChar,iMode);
	}
	return 0;
}

// returns the time in seconds since the character was created. 
// Only truly valid for entities created via EntCreate and ProjectileCreate
AUTO_EXPR_FUNC(CEFuncsCharacter) ACMD_NAME("SecondsSincePowersCreatedEntity");
F32 exprSecondsSincePowersCreatedEntity(SA_PARAM_OP_VALID Character *pChar)
{
#if !GAMESERVER && !GAMECLIENT
	assert(0);
#else
	if (pChar)
	{
		if (pChar->uiPowersCreatedEntityTime)
		{
			U32 uiTime = pmTimestamp(0.f);
			uiTime = (uiTime - pChar->uiPowersCreatedEntityTime);
			if (uiTime > 0)
			{
				return uiTime * MM_SECONDS_PER_PROCESS_COUNT;
			}
		}
	}
#endif
	return 0.f;
}

// Self
//  Inputs: PowerDef name
//  Return: 1 if the Character owns the Power and has placed it in a PowerSlot, otherwise 0
AUTO_EXPR_FUNC(CEFuncsSelf);
int SlottedPower(ExprContext* pContext,
				 ACMD_EXPR_DICT(PowerDef) const char* powerDefName)
{
	int r = 0;
	Entity *pent = exprContextGetSelfPtr(pContext);
	if(pent && pent->pChar)
	{
		Power *ppow = character_FindPowerByName(pent->pChar,powerDefName);
		if(ppow)
		{
			r = character_PowerSlotted(pent->pChar,ppow);
		}
	}
	return r;
}

// Self
//  Inputs: PowerDef name, The index to check if the power is slotted in
//  Return: 1 if the Character owns the Power and has placed it in a PowerSlot, otherwise 0
AUTO_EXPR_FUNC(CEFuncsSelf);
int IsPowerSlottedInSlot(ExprContext* pContext,
							ACMD_EXPR_DICT(PowerDef) const char* powerDefName, 
							S32 iPowerSlotIndex)
{
	
	Entity *pent = exprContextGetSelfPtr(pContext);
	if(pent && pent->pChar)
	{
		Power *ppow = character_FindPowerByName(pent->pChar,powerDefName);
		if(ppow)
		{
			S32 iSlot = character_GetPowerSlot(pent->pChar, ppow);
			return iSlot == iPowerSlotIndex;
		}
	}
	return false;
}

// Self
//  Inputs: PowerDef name, PowerSlot type
//  Return: 1 if the Character owns the Power and has placed it in a PowerSlot of the specified type, otherwise 0
AUTO_EXPR_FUNC(CEFuncsSelf) ACMD_NOTESTCLIENT;
int SlottedPowerInType(ExprContext* pContext,
					   ACMD_EXPR_DICT(PowerDef) const char* powerDefName,
					   const char* powerSlotType)
{
	int r = 0;
	Entity *pent = exprContextGetSelfPtr(pContext);
	if(pent && pent->pChar)
	{
		Power *ppow = character_FindPowerByName(pent->pChar,powerDefName);
		if(ppow)
		{
			const char *pchSlotType = character_PowerIDSlotType(pent->pChar,ppow->uiID);
			if(powerSlotType && pchSlotType)
			{
				r = !stricmp(powerSlotType,pchSlotType);
			}
			else
			{
				r = (powerSlotType==pchSlotType);
			}
		}
	}
	return r;
}


// Self
//  Inputs: PowerTreeNodeDef name
//  Return: The rank of the named node if the entity owns it, otherwise 0
//  Override: The output of this function can be overridden
AUTO_EXPR_FUNC(CEFuncsSelf);
int NodeRank(ExprContext* pContext,
			 ACMD_EXPR_DICT(PowerTreeNodeDef) const char* powerTreeNodeDefName)
{
	int r = 0;
	if(g_CombatEvalOverrides.bEnabled && g_CombatEvalOverrides.bNodeRank)
	{
		r = g_CombatEvalOverrides.iNodeRank;
	}
	else
	{
		Entity *pent = exprContextGetSelfPtr(pContext);

		if(pent && pent->pChar)
		{
			Character *pchar = pent->pChar;
			PTNode *pNode = character_GetNode(pchar,powerTreeNodeDefName);
			if(pNode && !pNode->bEscrow)
			{
				r = pNode->iRank + 1;
			}
		}
	}
	return r;
}

// Character
//  Inputs: Character, PowerTreeNodeDef name
//  Return: The rank of the named node if the character owns it, otherwise 0
//  Override: The output of this function can be overridden
AUTO_EXPR_FUNC(CEFuncsCharacter);
int CharNodeRank(SA_PARAM_OP_VALID Character *pChar, ACMD_EXPR_DICT(PowerTreeNodeDef) const char* powerTreeNodeDefName)
{
	int iRank = 0;
	if (g_CombatEvalOverrides.bEnabled && g_CombatEvalOverrides.bNodeRank)
	{
		iRank = g_CombatEvalOverrides.iNodeRank;
	}
	else if (pChar)
	{
		PTNode *pNode = character_GetNode(pChar, powerTreeNodeDefName);
		if (pNode && !pNode->bEscrow)
		{
			iRank = pNode->iRank + 1;
		}
	}
	return iRank;
}

// Character
//  Inputs: Character, PowerTreeNodeDef name, Enhancement PowerDef name
//  Return: The rank of the named enhancement for named node if the character owns the node and enhancement,
//   0 if the character owns the node but not the enhancement, and -1 if the character doesn't own the node at all
AUTO_EXPR_FUNC(CEFuncsCharacter);
int CharNodeEnhancementRank(SA_PARAM_OP_VALID Character *character,
							ACMD_EXPR_DICT(PowerTreeNodeDef) const char* powerTreeNodeDefName,
							ACMD_EXPR_DICT(PowerDef) const char* enhancementPowerDefName)
{
	int r = -1;
	if(character)
	{
		PTNode *pNode = character_GetNode(character,powerTreeNodeDefName);
		if(pNode)
		{
			PowerDef *pdefEnhancement = powerdef_Find(enhancementPowerDefName);
			r = powertreenode_FindEnhancementRankHelper(CONTAINER_NOCONST(PTNode, pNode),pdefEnhancement);
		}
	}
	return r;
}

// Character
//  Inputs: character
//  Return: The last falling impact speed
AUTO_EXPR_FUNC(CEFuncsCharacter);
F32 LastFallingImpactSpeed(SA_PARAM_OP_VALID Character *character)
{
	if (character)
	{
		return character->fLastFallingImpactSpeed;
	}
	return 0.f;
}

// Self
//  Inputs: PowerTreeNodeDef name, Enhancement PowerDef name
//  Return: The rank of the named enhancement for named node if the entity owns the node and enhancement,
//   0 if the entity owns the node but not the enhancement, and -1 if the entity doesn't own the node at all
AUTO_EXPR_FUNC(CEFuncsSelf);
int NodeEnhancementRank(ExprContext* pContext,
						ACMD_EXPR_DICT(PowerTreeNodeDef) const char* powerTreeNodeDefName,
						ACMD_EXPR_DICT(PowerDef) const char* enhancementPowerDefName)
{
	int r = -1;
	Entity *pent = exprContextGetSelfPtr(pContext);
	if(pent && pent->pChar)
		r = CharNodeEnhancementRank(pent->pChar,powerTreeNodeDefName,enhancementPowerDefName);
	return r;
}

// Self
//	Inputs: PowerTreeNodeDef name  PowerDef
//	Return: True if the power def came from the given power node
AUTO_EXPR_FUNC(CEFuncsPowerDef) ACMD_NOTESTCLIENT;
bool PowerFromNode(ExprContext *pContext,
				   ACMD_EXPR_DICT(PowerTreeNodeDef) const char *powerTreeNodeDefName,
				   SA_PARAM_OP_VALID PowerDef_ForExpr *pPowerDefExpr)
{
	Entity *pent = exprContextGetSelfPtr(pContext);
	Character *pChar = pent ? pent->pChar : NULL;
	PTNodeDef *pNode = RefSystem_ReferentFromString("PTNodeDef",powerTreeNodeDefName);
	PowerDef *pPowerDef = (PowerDef*)pPowerDefExpr;

	if(!pNode)
	{
		Errorf("Invalid node name (%s)",powerTreeNodeDefName);
		return false;
	}

	if(pChar)
	{
		int i,t;

		for(t=0;t<eaSize(&pChar->ppPowerTrees);t++)
		{
			PowerTree *pTree = pChar->ppPowerTrees[t];

			for(i=0;i<eaSize(&pTree->ppNodes);i++)
			{
				if(GET_REF(pTree->ppNodes[i]->hDef) == pNode)
				{
					int p;
					for(p=0;p<eaSize(&pTree->ppNodes[i]->ppPowers);p++)
					{
						int sp;
						if (GET_REF(pTree->ppNodes[i]->ppPowers[p]->hDef) == pPowerDef)
							return true;
						
						//Check sub powers
						for(sp=0;sp<eaSize(&pTree->ppNodes[i]->ppPowers[p]->ppSubPowers);sp++)
						{
							if(GET_REF(pTree->ppNodes[i]->ppPowers[p]->ppSubPowers[sp]->hDef) == pPowerDef)
								return true;
						}
					}
					
				}
			}
		}
	}

	return false;
}

// Character
//  Inputs: Character
//  Return: The Owner Character of the input Character, or NULL
AUTO_EXPR_FUNC(CEFuncsCharacter) ACMD_NOTESTCLIENT;
SA_RET_OP_VALID Character* OwnerChar(ACMD_EXPR_PARTITION iPartitionIdx, SA_PARAM_OP_VALID Character *character)
{
	if(character && character->pEntParent && character->pEntParent->erOwner)
	{
		Entity *e = entFromEntityRef(iPartitionIdx, character->pEntParent->erOwner);
		if(e)
		{
			return e->pChar;
		}
	}
	return character;
}

// Character
//  Inputs: Character
//  Return: The Creator Character of the input Character, or NULL
AUTO_EXPR_FUNC(CEFuncsCharacter) ACMD_NOTESTCLIENT;
SA_RET_OP_VALID Character* CreatorChar(ACMD_EXPR_PARTITION iPartitionIdx, SA_PARAM_OP_VALID Character *character)
{
	if(character && character->pEntParent && character->pEntParent->erCreator)
	{
		Entity *e = entFromEntityRef(iPartitionIdx, character->pEntParent->erCreator);
		if(e)
		{
			return e->pChar;
		}
	}
	return character;
}

// Character
//  Return: A NULL Character
//  Note: Do not use without talking to Jered about use case
AUTO_EXPR_FUNC(CEFuncsCharacter);
SA_RET_OP_VALID Character* NullChar(void)
{
	return NULL;
}

// Character
//  Inputs: Character A, Character B
//  Return: Combat distance between the two Characters, otherwise 0
AUTO_EXPR_FUNC(CEFuncsCharacter);
F32 DistChar(SA_PARAM_OP_VALID Character *characterA,
			 SA_PARAM_OP_VALID Character *characterB)
{
	F32 r = 0;
	if(characterA && characterB)
	{
		r = entGetDistance(characterA->pEntParent,NULL,characterB->pEntParent,NULL,NULL);
	}
	return r;
}

AUTO_EXPR_FUNC(CEFuncsCharacter);
F32 DistTarget(SA_PARAM_OP_VALID Character *source)
{
	if(source)
	{
		int iPartitionIdx = entGetPartitionIdx(source->pEntParent);
		Entity *pTarget = entFromEntityRef(iPartitionIdx,source->currentTargetRef);
		WorldInteractionNode* pNode = GET_REF(source->currentTargetHandle);
		Vec3 vPos;

	

		if(pTarget && IS_HANDLE_ACTIVE(pTarget->hCreatorNode))
			pNode = GET_REF(pTarget->hCreatorNode);

		if(pNode)
		{
			wlInterationNode_FindNearestPoint( vPos, pNode, vPos );
			return entGetDistance(source->pEntParent,NULL,NULL,vPos,NULL);
		}

		if(pTarget)
			return entGetDistance(source->pEntParent,NULL,pTarget,NULL,NULL);
	}

	return 0;
}

// Character
//  Inputs: Character A, Character B
//  Return: Distance between the base positions of the two Characters, otherwise 0
AUTO_EXPR_FUNC(CEFuncsCharacter);
F32 DistCharBase(SA_PARAM_OP_VALID Character *characterA,
				 SA_PARAM_OP_VALID Character *characterB)
{
	F32 r = 0;
	if(characterA && characterB)
	{
		Vec3 vecA, vecB;
		entGetPos(characterA->pEntParent,vecA);
		entGetPos(characterB->pEntParent,vecB);
		r = distance3(vecA,vecB);
	}
	return r;
}

AUTO_EXPR_FUNC(CEFuncsCharacter);
F32 DistTargetBase(SA_PARAM_OP_VALID Character *source)
{
	if(source)
	{
		int iPartitionIdx = entGetPartitionIdx(source->pEntParent);
		Entity *pTarget = entFromEntityRef(iPartitionIdx,source->currentTargetRef);
		WorldInteractionNode* pNode = GET_REF(source->currentTargetHandle);
		Vec3 vecA, vecB;

		if(!source)
			return 0;

		entGetPos(source->pEntParent,vecA);

		if(pTarget && IS_HANDLE_ACTIVE(pTarget->hCreatorNode))
			pNode = GET_REF(pTarget->hCreatorNode);

		if(pNode)
		{
			wlInteractionNodeGetWorldMid(pNode,vecB);
			return  distance3(vecA,vecB);
		}
		if(pTarget)
		{
			entGetPos(pTarget,vecB);
			return distance3(vecA,vecB);
		}
	}

	return 0;
}

// Character
// Inputs: Character
// Return: The current speed of the Character
AUTO_EXPR_FUNC(CEFuncsCharacter);
F32 SpeedChar(SA_PARAM_OP_VALID Character *character)
{
	F32 r = 0;
	if(character)
	{
		Vec3 vecVelocity;
		ZEROVEC3(vecVelocity);
		entCopyVelocityFG(character->pEntParent,vecVelocity);
		r = lengthVec3(vecVelocity);
	}
	return r;
}

// Character
// Inputs: Character
// Return: The current speed percentage of the Character.  If the Character is flying, this
//  is relative to the SpeedFlying attribute, whereas if the Character is not flying, this
//  is relative to the SpeedRunning attribute.  May return values greater than 1.
AUTO_EXPR_FUNC(CEFuncsCharacter);
F32 SpeedPct(SA_PARAM_OP_VALID Character *character)
{
	F32 r = 0;
	if(character && character->pattrBasic)
	{
		F32 fSpeed = SpeedChar(character);
		F32 fSpeedAttrib = character->pattrBasic->fSpeedRunning;
		if(character->pattrBasic->fFlight>0)
		{
			fSpeedAttrib = character->pattrBasic->fSpeedFlying;
		}
		r = fSpeedAttrib > 0 ? fSpeed / fSpeedAttrib : 1;
	}
	return r;
}

// Character
// Inputs: Character
// Return: The current XZ (horizontal) speed of the Character
AUTO_EXPR_FUNC(CEFuncsCharacter);
F32 SpeedXZChar(SA_PARAM_OP_VALID Character *character)
{
	F32 r = 0;
	if(character)
	{
		Vec3 vecVelocity;
		ZEROVEC3(vecVelocity);
		entCopyVelocityFG(character->pEntParent,vecVelocity);
		r = lengthVec3XZ(vecVelocity);
	}
	return r;
}


// Activation
//  Inputs: None
//  Return: The percent the power would be charged based on the input time charged
AUTO_EXPR_FUNC(CEFuncsActivation);
F32 ActPercentCharged(ExprContext *pContext)
{
	F32 r = 0.f;
	PowerActivation *pact = exprContextGetVarPointerUnsafePooled(pContext, s_pchActivation);
	PowerDef *pdef = pact ? GET_REF(pact->ref.hdef) : NULL;

	if(pact && pdef && pdef->fTimeCharge && pact->fTimeCharged > 0.f)
	{
		r = pact->fTimeCharged / pdef->fTimeCharge;
		MIN1(r, 1.0f);
	}

	return r;
}

// Activation
//  Inputs: None
//  Return: The time the activation was charged
AUTO_EXPR_FUNC(CEFuncsActivation);
F32 ActCharged(ExprContext *pContext)
{
	F32 r = 0.f;
	PowerActivation *pact = exprContextGetVarPointerUnsafePooled(pContext, s_pchActivation);

	if(pact && pact->fTimeCharged > 0.f)
	{
		r = pact->fTimeCharged;
	}

	return r;
}


// Application
//  Inputs: Character
//  Return: The Character's root distance from the Application's source (Character root or position), otherwise 0
AUTO_EXPR_FUNC(CEFuncsApplication);
F32 DistAppSource(ExprContext *pContext,
				  SA_PARAM_OP_VALID Character *character)
{
	F32 r = 0;
	PowerApplication *papp = exprContextGetVarPointerUnsafePooled(pContext,s_pchApplication);
	if(character && papp)
	{
		Vec3 vecA, vecB;
		entGetPos(character->pEntParent,vecA);
		if(papp->pcharSource)
		{
			entGetPos(papp->pcharSource->pEntParent,vecB);
		}
		else
		{
			copyVec3(papp->vecSourcePos,vecB);
		}
		r = distance3(vecA,vecB);
	}
	return r;
}

// Application
//  Inputs: Character
//  Return: The Character's capsule distance from the Application's source (Character capsule or position), otherwise 0
AUTO_EXPR_FUNC(CEFuncsApplication);
F32 DistAppSourceCapsule(ExprContext *pContext,
						 SA_PARAM_OP_VALID Character *character)
{
	F32 r = 0;
	PowerApplication *papp = exprContextGetVarPointerUnsafePooled(pContext,s_pchApplication);
	if(character && papp)
	{
		r = entGetDistance(character->pEntParent,NULL,papp->pcharSource?papp->pcharSource->pEntParent:NULL,papp->vecSourcePos,NULL);
	}
	return r;
}

// Application
//  Inputs: Character
//  Return: The Character's root distance from the Application's primary target (Character root or position), otherwise 0
//  TODO(JW): Add support for target volumes used by the mission system
AUTO_EXPR_FUNC(CEFuncsApplication);
F32 DistAppTarget(ExprContext *pContext,
				  SA_PARAM_OP_VALID Character *character)
{
	F32 r = 0;
	PowerApplication *papp = exprContextGetVarPointerUnsafePooled(pContext,s_pchApplication);
	if(character && papp)
	{
		Vec3 vecA, vecB;
		entGetPos(character->pEntParent,vecA);
		if(papp->pentTargetEff)
		{
			entGetPos(papp->pentTargetEff,vecB);
		}
		else
		{
			copyVec3(papp->vecTargetEff,vecB);
		}
		r = distance3(vecA,vecB);
	}
	return r;
}

// Application
//  Inputs: Character
//  Return: The Character's capsule distance from the Application's primary target (Character capsule or position), otherwise 0
//  TODO(JW): Add support for target volumes used by the mission system
AUTO_EXPR_FUNC(CEFuncsApplication);
F32 DistAppTargetCapsule(ExprContext *pContext,
						 SA_PARAM_OP_VALID Character *character)
{
	F32 r = 0;
	PowerApplication *papp = exprContextGetVarPointerUnsafePooled(pContext,s_pchApplication);
	if(character && papp)
	{
		r = entGetDistance(character->pEntParent,NULL,papp->pentTargetEff,papp->vecTargetEff,NULL);
	}
	return r;
}

// Application
//  Return: the number of targets hit excluding the source of the application
AUTO_EXPR_FUNC(CEFuncsApplication);
S32 ApplicationGetNumHitTargetsExcludingSelf(ExprContext *pContext)
{
	PowerApplication *papp = exprContextGetVarPointerUnsafePooled(pContext,s_pchApplication);
	if(papp)
	{
		if (papp->iNumTargets > 0)
		{
			return papp->iNumTargets - papp->bHitSelf;
		}
	}
	return 0;
}



// Character
//	Inputs: Character
//	Return: The Character's distance from the ground
//	Prediction: NONE, server wins
AUTO_EXPR_FUNC(CEFuncsCharacter) ACMD_NOTESTCLIENT;
F32 DistGround(SA_PARAM_OP_VALID Character *character)
{

	if(character)
	{
		Entity *pent = character->pEntParent;

		if(pent)
		{
			return entHeight(pent,2000.0f);
		}
	}

	return 0.0f;
}

// Self
//  Inputs: none
//  Return: 1 if the entity is not flying and not on the ground, otherwise 0
//  Prediction: True: Server wins
//  Prediction: False: Server agrees
AUTO_EXPR_FUNC(CEFuncsSelf) ACMD_NOTESTCLIENT;
int Falling(ExprContext *pContext)
{
#if !GAMESERVER && !GAMECLIENT
	assert(0);
#else
	S32 r = false;
	Entity *pent = exprContextGetSelfPtr(pContext);

	if(pent)
	{
		S32 bFlying = (pent->pChar && pent->pChar->pattrBasic->fFlight > 0);
		S32 bOnGround = mrSurfaceGetOnGround(pent->mm.mrSurface);
		r = !bFlying && !bOnGround;

		if(entIsServer())
		{
			CombatEvalPrediction ePredict = combateval_GetPrediction(pContext);
			if(!r && ePredict==kCombatEvalPrediction_True)
			{
				// TODO(JW): Prediction: See if this is close to true
			}
			else if(r && ePredict==kCombatEvalPrediction_False)
			{
				// TODO(JW): Prediction: Come up with something reasonable
				r = false;
			}
		}
	}

	return r;
#endif
}

// Self
//  Inputs: PowerDef name
//  Return: 1 if the entity can currently combo (or follow up) the named PowerDef, otherwise 0
//  Prediction: True: Server agrees for finished activations up to .25s older than normal limit
//  Prediction: False: Server agrees for finished activations at least .25s newer than normal limit
AUTO_EXPR_FUNC(CEFuncsSelf) ACMD_NOTESTCLIENT;
int Combo(ExprContext *pContext,
		  ACMD_EXPR_DICT(PowerDef) const char* powerDefName)
{
	int r = false;
	Entity *pent = exprContextGetSelfPtr(pContext);
	if(pent && pent->pChar)
	{
		Character *pchar = pent->pChar;
		CombatEvalPrediction ePredict = combateval_GetPrediction(pContext);
		PowerActivation *pactQueued = pchar->pPowActQueued;
		PowerActivation *pactCurrent = pchar->pPowActCurrent;
		PowerActivation *pactFinished = pchar->pPowActFinished;
		PowerDef *pdefCombo = powerdef_Find(powerDefName);

		/*
		printf("<<<>>> %s Combo test for %s;\n  <>   Que %d %s %d, Cur %d %s (%f), Fin %d %s (%f)%s\n",
		be->name,pchName,
		pactQueued?pactQueued->uchID:0,
		pactQueued?GET_REF(pactQueued->hdef)->pchName:"NULL",
		pactQueued?pactQueued->bCommit:0,
		pactCurrent?pactCurrent->uchID:0,
		pactCurrent?GET_REF(pactCurrent->hdef)->pchName:"NULL",
		pactCurrent?pactCurrent->ppow->fTimer:0,
		pactFinished?pactFinished->uchID:0,
		pactFinished?GET_REF(pactFinished->hdef)->pchName:"NULL",
		pactFinished?pactFinished->fTimeFinished:0,
		ePredict==kCombatEvalPrediction_True?" PREDICT True":ePredict==kCombatEvalPrediction_False?" PREDICT False":"");
		*/

		if(pactQueued && pactQueued->bCommit)
		{
			PowerDef *pdef = GET_REF(pactQueued->hdef);
			if(pdef && pdef==pdefCombo)
			{
				r = true;
			}
		}
		else if(pactCurrent)// && (pactCurrent->bActivated || ePredict==kCombatEvalPrediction_True))
		{
			PowerDef *pdef = GET_REF(pactCurrent->hdef);
			if(pdef && pdef==pdefCombo)
			{
				r = true;
			}
		}
		else if(pactFinished)
		{
			// Adjust the time allowed on finished activations based on prediction
			F32 fTimeFinishedAllowed = 0.5f;
			if(ePredict==kCombatEvalPrediction_True) fTimeFinishedAllowed = 0.75f;
			else if(ePredict==kCombatEvalPrediction_False) fTimeFinishedAllowed = 0.25f;

			if((pactFinished->fTimeFinished + pchar->fTimeSlept) <= fTimeFinishedAllowed)
			{
				PowerDef *pdef = GET_REF(pactFinished->hdef);
				if(pdef && pdef==pdefCombo)
				{
					r = true;
				}
			}
		}

	}
	return r;
}

// Returns 1 if the Entity is activating a Power with the given power category, 0 if it's not
AUTO_EXPR_FUNC(CEFuncsCharacter) ACMD_NAME("EntIsActivatingPowerOfCategory");
S32 exprEntIsActivatingPowerOfCategory(	SA_PARAM_OP_VALID Character *character,
										ACMD_EXPR_ENUM(PowerCategory) const char *categoryName)
{
	if (character && character->pPowActCurrent)
	{
		S32 eCat = StaticDefineIntGetInt(PowerCategoriesEnum,categoryName);
		PowerDef *pDef = GET_REF(character->pPowActCurrent->hdef);
		if (pDef)
		{
			return eaiFind(&pDef->piCategories, eCat) >= 0;
		}
	}
	return 0;
}

// Self
//  Inputs: Power Category name
//  Return: Number of Toggle PowerActivations you have where the Power has the named PowerCategory
AUTO_EXPR_FUNC(CEFuncsSelf);
S32 ToggleActsWithPowerCat(ExprContext *pContext,
						   ACMD_EXPR_ENUM(PowerCategory) const char *categoryName)
{
	S32 r = 0, i;
	Entity *pent = exprContextGetSelfPtr(pContext);
	if(pent && pent->pChar && (i=eaSize(&pent->pChar->ppPowerActToggle)))
	{
		S32 eCat = StaticDefineIntGetInt(PowerCategoriesEnum,categoryName);;
		for(i=i-1; i>=0; i--)
		{
			PowerDef *pdef = GET_REF(pent->pChar->ppPowerActToggle[i]->ref.hdef);
			if(pdef && -1!=eaiFind(&pdef->piCategories,eCat))
			{
				r++;
			}
		}
	}
	return r;
}

// Self
//  Inputs: mass that you can pick up
//  Return: 1 if you can pick up the target, otherwise 0
AUTO_EXPR_FUNC(CEFuncsSelf);
S32 CanPickUp(ExprContext* pContext, F32 mass)
{
	S32 bSuccess = false;
	Character *pchar = exprContextGetVarPointerUnsafePooled(pContext,s_pchSource);
	if(pchar)
	{
		Character *pcharTarget = exprContextGetVarPointerUnsafePooled(pContext,s_pchTarget);
		if(pcharTarget)
		{
			Critter *pCritter = pcharTarget->pEntParent->pCritter;
			if(pCritter && pCritter->eInteractionFlag & kCritterOverrideFlag_Throwable)
			{
				if(mass >= pCritter->fMass)
				{
					bSuccess = true;
				}
			}
		}
		else
		{
			// No target, but this can be called on the client as part of a ComboPower's Target expression
			//  which means we may be trying to pick up a node.  In that case, we call a function to fetch
			//  the node under consideration by the client, and get the mass from that directly.
#ifdef GAMECLIENT
			WorldInteractionNode *pnode = target_GetPotentialLegalNode();
			if(pnode && im_IsNotDestructibleOrCanThrowObject(NULL, pnode, NULL))
			{
				F32 fMass = im_GetMass(pnode);
				if(mass >= fMass)
				{
					bSuccess = true;
				}
			}
#endif
		}
	}
	return bSuccess;
}

// Self
//  Inputs: none
//  Return: 1 if you are physically holding an object (which is no longer an entity), otherwise 0
AUTO_EXPR_FUNC(CEFuncsSelf);
S32 HoldingObject(ExprContext* pContext)
{
	Entity *pent = exprContextGetSelfPtr(pContext);
	S32 bSuccess = (pent && pent->pChar && IS_HANDLE_ACTIVE(pent->pChar->hHeldNode));
	return bSuccess;
}

// Self
//  Inputs: none
//  Return: 1 if you are non-physically holding an object (which is still an entity), otherwise 0
AUTO_EXPR_FUNC(CEFuncsSelf);
S32 HoldingObjectTele(ExprContext* pContext, ACMD_EXPR_PARTITION iPartitionIdx)
{
	Entity *pent = exprContextGetSelfPtr(pContext);
	S32 bSuccess = (pent && pent->pChar && pent->pChar->erHeld && entFromEntityRef(iPartitionIdx, pent->pChar->erHeld));
	return bSuccess;
}

// Self
//  Inputs: none
//  Return: Mass of object you are holding, or your mass if you are an object, otherwise 0
AUTO_EXPR_FUNC(CEFuncsSelf);
F32 HeldObjectMass(ExprContext* pContext, ACMD_EXPR_PARTITION iPartitionIdx)
{
	F32 fMass = 0;
	Entity *pent = exprContextGetSelfPtr(pContext);
	if(pent)
	{
		// First check for holding an existing entity
		if(pent->pChar && pent->pChar->erHeld && entFromEntityRef(iPartitionIdx, pent->pChar->erHeld))
		{
			Entity *pentHeld = entFromEntityRef(iPartitionIdx, pent->pChar->erHeld);
			if(pentHeld && pentHeld->pCritter)
			{
				fMass = pentHeld->pCritter->fMass;
			}
		}

		// Next check for cached mass for held node
		if(!fMass && pent->pChar)
			fMass = pent->pChar->fHeldMass;

		// Next check for held node
		if(!fMass && pent->pChar && IS_HANDLE_ACTIVE(pent->pChar->hHeldNode))
		{
			WorldInteractionNode *pnode = GET_REF(pent->pChar->hHeldNode);
			if(pnode)
			{
				fMass = im_GetMass(pnode);
			}
		}

		// Last use your own mass
		if(!fMass && pent->pCritter && IS_HANDLE_ACTIVE(pent->hCreatorNode))
		{
			fMass = pent->pCritter->fMass;
		}
	}
	return fMass;
}


static S32 HeldObjectCostumeBase(SA_PARAM_OP_VALID Entity *pent, SA_PARAM_NN_VALID char **ppchCostumeOut)
{
	if(pent)
	{
		const char *pchNode = NULL;
		int iPartitionIdx = entGetPartitionIdx(pent);
		if(pent->pChar && pent->pChar->erHeld && entFromEntityRef(iPartitionIdx, pent->pChar->erHeld))
		{
			Entity *pentHeld = entFromEntityRef(iPartitionIdx, pent->pChar->erHeld);
			if(pentHeld && IS_HANDLE_ACTIVE(pentHeld->hCreatorNode))
			{
				pchNode = REF_STRING_FROM_HANDLE(pentHeld->hCreatorNode);
			}
		}

		if(!pchNode && pent->pChar && IS_HANDLE_ACTIVE(pent->pChar->hHeldNode))
		{
			pchNode = REF_STRING_FROM_HANDLE(pent->pChar->hHeldNode);
		}

		if(!pchNode && IS_HANDLE_ACTIVE(pent->hCreatorNode))
		{
			pchNode = REF_STRING_FROM_HANDLE(pent->hCreatorNode);
		}

		if(pchNode)
		{
			estrCopy2(ppchCostumeOut,pchNode);
			return true;
		}
	}

	return false;
}

// Self
//  Inputs: optional pivot string, which is appended to the costume name to get a special costume
//  Return: Costume of object you are holding, or your costume if you are an object, otherwise a null string
AUTO_EXPR_FUNC(CEFuncsSelf);
char *HeldObjectCostume(ExprContext* pContext, SA_PARAM_OP_STR const char *pivot)
{
	F32 fMass = 0;
	Entity *pent = exprContextGetSelfPtr(pContext);
	
	static char *s_pchCostume = NULL;
	if(!s_pchCostume)
	{
		estrCreate(&s_pchCostume);
	}
	estrClear(&s_pchCostume);

	if(HeldObjectCostumeBase(pent,&s_pchCostume))
	{
		if(pivot && *pivot)
		{
			estrConcatChar(&s_pchCostume, ' ');
			estrAppend2(&s_pchCostume,pivot);
		}
	}
	
	return s_pchCostume;
}

// Self
//  Inputs: None
//  Return: Vec3 representing the translation between the HandPivot costume and MassPivot 
//   costume of object you are holding, or your costume if you are an object, otherwise <0, 0, 0>
AUTO_EXPR_FUNC(CEFuncsSelf);
void HeldObjectThrowTranslate(ExprContext* pContext, ACMD_EXPR_LOC_MAT4_OUT matOut)
{
	Entity *pent = exprContextGetSelfPtr(pContext);

	char *pchCostume = NULL;
	estrStackCreate(&pchCostume);


	matOut[3][0] = 0;
	matOut[3][1] = 0;
	matOut[3][2] = 0;

	if(HeldObjectCostumeBase(pent,&pchCostume))
	{
		WLCostume* pHandPivotCostume = NULL;
		WLCostume* pMassPivotCostume = NULL;
		char *pchCostumeTemp = NULL;
		estrStackCreate(&pchCostumeTemp);

		estrCopy(&pchCostumeTemp,&pchCostume);
		estrAppend2(&pchCostumeTemp," HandPivot");
		pHandPivotCostume = wlCostumeFromName(pchCostumeTemp);

		estrCopy(&pchCostumeTemp,&pchCostume);
		estrAppend2(&pchCostumeTemp," MassPivot");
		pMassPivotCostume = wlCostumeFromName(pchCostumeTemp);

		if (pHandPivotCostume && pMassPivotCostume && eaSize(&pHandPivotCostume->eaCostumeParts) > 0 && eaSize(&pMassPivotCostume->eaCostumeParts) > 0)
		{
			subVec3(pHandPivotCostume->eaCostumeParts[0]->mTransform[3], pMassPivotCostume->eaCostumeParts[0]->mTransform[3], matOut[3]);
		}
		estrDestroy(&pchCostumeTemp);
	}

	estrDestroy(&pchCostume);
}

// Self
//  Inputs: None
//  Return: Vec3 representing the rotation between the HandPivot costume and MassPivot 
//   costume of object you are holding, or your costume if you are an object, otherwise <0, 0, 0>
AUTO_EXPR_FUNC(CEFuncsSelf);
void HeldObjectThrowRotate(ExprContext* pContext, ACMD_EXPR_LOC_MAT4_OUT matOut)
{
	Entity *pent = exprContextGetSelfPtr(pContext);

	char *pchCostume = NULL;
	estrStackCreate(&pchCostume);

	matOut[3][0] = 0;
	matOut[3][1] = 0;
	matOut[3][2] = 0;

	if(HeldObjectCostumeBase(pent,&pchCostume))
	{
		WLCostume* pHandPivotCostume = NULL;
		WLCostume* pMassPivotCostume = NULL;
		char *pchCostumeTemp = NULL;
		estrStackCreate(&pchCostumeTemp);

		estrCopy(&pchCostumeTemp,&pchCostume);
		estrAppend2(&pchCostumeTemp," HandPivot");
		pHandPivotCostume = wlCostumeFromName(pchCostumeTemp);

		estrCopy(&pchCostumeTemp,&pchCostume);
		estrAppend2(&pchCostumeTemp," MassPivot");
		pMassPivotCostume = wlCostumeFromName(pchCostumeTemp);

		if (pHandPivotCostume && pMassPivotCostume && eaSize(&pHandPivotCostume->eaCostumeParts) > 0 && eaSize(&pMassPivotCostume->eaCostumeParts) > 0)
		{
			// Currently, don't do anything with rotation, since we copy the hand rotation onto the mass rotation so we don't get an awkward transition
		}


		estrDestroy(&pchCostumeTemp);
	}

	estrDestroy(&pchCostume);
}

// Self
//  Inputs: chance
//  Return: 1 if the client generates a random number [0 .. 1) less than chance
//  Prediction: True: Server agrees
//  Prediction: False: Server agrees
//  Unvalidated -- THIS FUNCTION IS NOT VALIDATED BY THE SERVER IN ANY WAY
AUTO_EXPR_FUNC(CEFuncsSelf);
S32 ChanceUnvalidated(ExprContext *pContext, F32 chance)
{
	CombatEvalPrediction ePredict = combateval_GetPrediction(pContext);
	S32 bSuccess = (ePredict==kCombatEvalPrediction_None) ? (randomPositiveF32() < chance) : (ePredict==kCombatEvalPrediction_True) ? true : false;
	return bSuccess;
}




// DD-specific expression functions (don't want to move because of the static context handles)

// Generic
//  Inputs: die count, die size
//  Return: the result of rolling dice as given.  Automatically account for criticals if in an Application context.
AUTO_EXPR_FUNC(CEFuncsGeneric);
S32 DDDieRoll(ExprContext *pContext,
			  S32 iDieCount,
			  S32 iDieSize)
{
	S32 iResult = 0;
	PowerApplication *papp = exprContextGetVarPointerUnsafePooled(pContext,s_pchApplication);

	if((papp && papp->critical.bSuccess) || (g_CombatEvalOverrides.bEnabled && g_CombatEvalOverrides.bMaxDamage))
	{
		iResult = iDieCount * iDieSize;
	}
	else if ((papp && papp->bGlancing) || (g_CombatEvalOverrides.bEnabled && g_CombatEvalOverrides.bMinDamage))
	{
		iResult = iDieCount * 1;
	}
	else
	{
		iResult = randomDieRolls(iDieCount,iDieSize);
	}
	
	return iResult;
}

// Generic
//  Inputs: ability value
//  Return: The D&D Modifier derived from the base value
AUTO_EXPR_FUNC(CEFuncsGeneric);
S32 DDAbilityModGeneric(F32 value)
{
	S32 r = combat_DDAbilityMod(value); 
	return r;
}

// Character
//  Inputs: Character, Attrib name
//  Return: The D&D Modifier derived from the base stat
AUTO_EXPR_FUNC(CEFuncsCharacter, UIGen);
S32 DDAbilityMod(SA_PARAM_OP_VALID Character *character,
				ACMD_EXPR_ENUM(AttribType) const char *attribName)
{
	S32 r = 0; 
	if(character && character->pattrBasic)
	{
		S32 eAttrib = StaticDefineIntGetInt(AttribTypeEnum,attribName);
		if(eAttrib >= 0 && IS_NORMAL_ATTRIB(eAttrib))
		{
			r = combat_DDAbilityMod(*F32PTR_OF_ATTRIB(character->pattrBasic,eAttrib));
		}
	}
#ifdef GAMECLIENT
	else if (g_CharacterCreationData)
	{
		r = combat_DDAbilityMod(DDGetAbilityScoreFromAssignedStats(g_CharacterCreationData->assignedStats, attribName));
	}
#endif
	return r;
}

// Character
//  Inputs: Character, Attrib name
//  Return: The D&D Bonus (positive or 0) derived from the base stat
AUTO_EXPR_FUNC(CEFuncsCharacter, UIGen);
S32 DDAbilityBonus(SA_PARAM_OP_VALID Character *character,
				ACMD_EXPR_ENUM(AttribType) const char *attribName)
{
	S32 r = 0;
	if(character && character->pattrBasic)
	{
		S32 eAttrib = StaticDefineIntGetInt(AttribTypeEnum,attribName);
		if(eAttrib >= 0 && IS_NORMAL_ATTRIB(eAttrib))
		{
			r = combat_DDAbilityBonus(*F32PTR_OF_ATTRIB(character->pattrBasic,eAttrib));
		}
	}
#ifdef GAMECLIENT
	else if (g_CharacterCreationData)
	{
		r = combat_DDAbilityBonus(DDGetAbilityScoreFromAssignedStats(g_CharacterCreationData->assignedStats, attribName));
	}
#endif
	return r;
}

// Character
//  Inputs: Character, Attrib name
//  Return: The D&D Penality (negative or 0) derived from the base stat
AUTO_EXPR_FUNC(CEFuncsCharacter, UIGen);
S32 DDAbilityPenalty(SA_PARAM_OP_VALID Character *character,
				  ACMD_EXPR_ENUM(AttribType) const char *attribName)
{
	S32 r = 0;
	if(character && character->pattrBasic)
	{
		S32 eAttrib = StaticDefineIntGetInt(AttribTypeEnum,attribName);
		if(eAttrib >= 0 && IS_NORMAL_ATTRIB(eAttrib))
		{
			r = combat_DDAbilityPenalty(*F32PTR_OF_ATTRIB(character->pattrBasic,eAttrib));
		}
	}
#ifdef GAMECLIENT
	else if (g_CharacterCreationData)
	{
		r = combat_DDAbilityPenalty(DDGetAbilityScoreFromAssignedStats(g_CharacterCreationData->assignedStats, attribName));
	}
#endif
	return r;
}

// Character
//  Inputs: Character, Species name
//  Return: 1 if the Character belongs to the named Species, otherwise 0
AUTO_EXPR_FUNC(CEFuncsCharacter);
int IsSpecies(SA_PARAM_OP_VALID Character *character, ACMD_EXPR_RES_DICT(SpeciesDef) const char* pchSpecies)
{
	SpeciesDef *pSpeciesDefArg = RefSystem_ReferentFromString("SpeciesDef", pchSpecies);

	if(character) {
		SpeciesDef *pSpeciesDef = GET_REF(character->hSpecies);
		if(pSpeciesDef && pSpeciesDefArg && pSpeciesDef == pSpeciesDefArg) {
			return 1;
		}
	}

	return 0;
}

// Character
//  Inputs: Character, Allegiance name
//  Return: 1 if the Character belongs to the named Allegiance, otherwise 0
AUTO_EXPR_FUNC(CEFuncsCharacter);
int IsOfAllegiance(SA_PARAM_OP_VALID Character *character, ACMD_EXPR_RES_DICT(AllegianceDef) const char *pchAllegiance)
{
	AllegianceDef *pAllegiance = RefSystem_ReferentFromString("Allegiance", pchAllegiance);
	if(pAllegiance && character && character->pEntParent)
	{
		AllegianceDef *a = GET_REF(character->pEntParent->hAllegiance);
		AllegianceDef *sa = GET_REF(character->pEntParent->hSubAllegiance);
		if (pAllegiance == a || pAllegiance == sa)
			return 1;
	}
	return 0;
}

// Character
//  Inputs: Character, Faction name
//  Return: 1 if the Character currently follows the named Faction, otherwise 0
AUTO_EXPR_FUNC(CEFuncsCharacter) ACMD_NAME(CharacterIsFaction);
int CharacterIsFaction(SA_PARAM_OP_VALID Character *pCharacter, ACMD_EXPR_RES_DICT(CritterFaction) const char *pchFaction)
{
	CritterFaction *pFaction = RefSystem_ReferentFromString("CritterFaction", pchFaction);
	if (pFaction && pCharacter && pCharacter->pEntParent)
	{
		CritterFaction *pCharFaction = entGetFaction(pCharacter->pEntParent);
		CritterFaction *pCharSubFaction = entGetSubFaction(pCharacter->pEntParent);
		if (pCharFaction == pFaction || pCharSubFaction == pFaction)
			return 1;
	}
	return 0;
}

// Character
//  Inputs: Character
//  Return: The XP level of the Character, or 0 if the Character is not valid
AUTO_EXPR_FUNC(CEFuncsCharacter) ACMD_NAME(CharacterXPLevel);
int CharacterXPLevel(SA_PARAM_OP_VALID Character *pCharacter)
{
	int iLevel = 0;
	if (pCharacter && pCharacter->pEntParent)
	{
		iLevel = entity_GetSavedExpLevel(pCharacter->pEntParent);
	}

	return iLevel;
}

// Character
//  Inputs: Character
//  Return: The combat level of the Character, or 0 if the Character is not valid
AUTO_EXPR_FUNC(CEFuncsCharacter) ACMD_NAME(CharacterCombatLevel);
int CharacterCombatLevel(SA_PARAM_OP_VALID Character *pCharacter)
{
	int iLevel = 0;
	if (pCharacter)
	{
		iLevel = pCharacter->iLevelCombat;
	}

	return iLevel;
}

// Returns the CombatEventTracker directly in the context, if it exists, or else it tries to
//  fetch it from the context's application.
SA_RET_OP_VALID static CombatEventTracker* GetTriggerEvent(ExprContext *pContext)
{
	CombatEventTracker *pTriggerEvent = exprContextGetVarPointerUnsafePooled(pContext,s_pchTriggerEvent);
	if(pTriggerEvent)
	{
		return pTriggerEvent;
	}
	else
	{
		PowerApplication *papp = exprContextGetVarPointerUnsafePooled(pContext,s_pchApplication);
		if(papp)
			return papp->trigger.pCombatEventTracker;
	}
	return NULL;
}

// Trigger
//  Inputs: none
//  Return: The magnitude of the event that caused the trigger to fire, NOT the
//   magnitude of the trigger AttribMod itself.  0 if the Application was not the
//   result of a trigger.  Replacement for Application.DamageTrigger.Mag.
AUTO_EXPR_FUNC(CEFuncsTrigger);
F32 TriggerMag(ExprContext *pContext)
{
	CombatEventTracker *pTriggerEvent = GetTriggerEvent(pContext);
	if(pTriggerEvent)
	{
		return pTriggerEvent->fMag;
	}
	else
	{
		PowerApplication *papp = exprContextGetVarPointerUnsafePooled(pContext,s_pchApplication);
		if(papp)
			return papp->trigger.fMag;
	}
	return 0;
}

// Trigger
//  Inputs: none
//  Return: Like TriggerMag(), except the magnitude is pre-resistance by the target.
//   Replacement for Application.DamageTrigger.MagPreResist.
AUTO_EXPR_FUNC(CEFuncsTrigger);
F32 TriggerMagPreResist(ExprContext *pContext)
{
	CombatEventTracker *pTriggerEvent = GetTriggerEvent(pContext);
	if(pTriggerEvent)
	{
		return pTriggerEvent->fMagPreResist;
	}
	else
	{
		PowerApplication *papp = exprContextGetVarPointerUnsafePooled(pContext,s_pchApplication);
		if(papp)
			return papp->trigger.fMagPreResist;
	}
	return 0;
}

// Trigger
//  Inputs: none
//  Return: Like TriggerMag(), except the magnitude is divided by the target's maximum health.
//   Replacement for Application.DamageTrigger.MagScale.
AUTO_EXPR_FUNC(CEFuncsTrigger);
F32 TriggerMagScale(ExprContext *pContext)
{
	PowerApplication *papp = exprContextGetVarPointerUnsafePooled(pContext,s_pchApplication);
	if(papp)
		return papp->trigger.fMagScale;
	return 0;
}

// Trigger
//  Inputs: none
//  Return: The Character that was at the other end of the event that triggered this response.
//   For example, a CriticalOut event occurs when you land a critical hit, so the "Other" Character
//   is the one who was hit by the critical.  Returns NULL for failure cases.
AUTO_EXPR_FUNC(CEFuncsTrigger);
SA_RET_OP_VALID Character* TriggerOtherChar(ExprContext *pContext)
{
	CombatEventTracker *pTriggerEvent = GetTriggerEvent(pContext);
	if(pTriggerEvent)
	{
		Entity *pentOther = entFromEntityRefAnyPartition(pTriggerEvent->erOther);
		if(pentOther)
			return pentOther->pChar;
	}
	return NULL;
}

// Trigger
//  Inputs: none
//  Return: The AttribModDef that caused the event that triggered this response, or NULL
AUTO_EXPR_FUNC(CEFuncsTrigger);
SA_RET_OP_VALID AttribModDef_ForExpr* TriggerModDef(ExprContext *pContext)
{
	CombatEventTracker *pTriggerEvent = GetTriggerEvent(pContext);
	if(pTriggerEvent)
		return pTriggerEvent->pAttribModDef;
	return NULL;
}

// Trigger
//  Inputs: none
//  Return: The PowerDef that caused the event that triggered this response, or NULL
AUTO_EXPR_FUNC(CEFuncsTrigger);
SA_RET_OP_VALID PowerDef_ForExpr* TriggerPowerDef(ExprContext *pContext)
{
	CombatEventTracker *pTriggerEvent = GetTriggerEvent(pContext);
	if(pTriggerEvent)
		return pTriggerEvent->pPowerDef;
	return NULL;
}

// Self
//  Inputs: ItemCategory name
//  Return: Sum of (Basic Damage/DataDefined Magnitudes / TimeRecharge) of first Power of
//   all equipped items with the given ItemCategory.  If a Power does not have a TimeRecharge
//   it is considered a TimeRecharge of 1.
AUTO_EXPR_FUNC(CEFuncsSelf);
F32 ItemDPS(ExprContext *pContext, ACMD_EXPR_PARTITION iPartitionIdx,
			ACMD_EXPR_ENUM(ItemCategory) const char* itemCategoryName)
{
	int i,s;
	int iCat = -1;
	Item **ppItems = NULL;
	F32 fDPS = 0;
	Entity *pEnt = exprContextGetSelfPtr(pContext);
	GameAccountDataExtract *pExtract;

	// Critters attempt a POWERTABLE_ITEMDPS PowerTable lookup.
	//  For now they do this even if they have Items, because the Items aren't really
	//  intended to be relevant for damage yet.
	if(pEnt && pEnt->pChar && !entIsPlayer(pEnt))
	{
		ANALYSIS_ASSUME(pEnt->pChar != NULL); 
		fDPS = character_PowerTableLookup(pEnt->pChar,POWERTABLE_ITEMDPS);
		return fDPS;
	}

	pExtract = entity_GetCachedGameAccountDataExtract(pEnt);
	iCat = StaticDefineIntGetInt(ItemCategoryEnum,itemCategoryName);
	inv_ent_FindEquippedItemsByCategory(pEnt,iCat,&ppItems,pExtract);

	s = eaSize(&ppItems);
	for(i=s-1; i>=0; i--)
	{
		ItemDef *pdefItem = GET_REF(ppItems[i]->hItem);
		if(pdefItem && eaSize(&pdefItem->ppItemPowerDefRefs))
		{
			// Assumption: The first ItemPower on the ItemDef describes the Item's damage for this function
			ItemPowerDefRef *pItemPowerDefRef = pdefItem->ppItemPowerDefRefs[0];
			ItemPowerDef *pdefItemPower = GET_REF(pItemPowerDefRef->hItemPowerDef);
			if(pdefItemPower)
			{
				// Alright, we've actually got the PowerDef that should describe the Item's damage
				PowerDef *pdef = GET_REF(pdefItemPower->hPower);
				if(pdef)
				{
					int j;
					F32 fMagnitude = 0;
					F32 fTimeRecharge = pdef->fTimeRecharge ? pdef->fTimeRecharge : 1;
					for(j=eaSize(&pdef->ppOrderedMods)-1; j>=0; j--)
					{
						AttribModDef *pmoddef = pdef->ppOrderedMods[j];
						if(pmoddef->offAspect==kAttribAspect_BasicAbs
							&& (ATTRIB_DAMAGE(pmoddef->offAttrib)
								|| ATTRIB_DATADEFINED(pmoddef->offAttrib)))
						{
							// Assumption: The AttribModDef's magnitude expression is simple
							F32 fMagnitudeMod = combateval_EvalNew(iPartitionIdx,pmoddef->pExprMagnitude,kCombatEvalContext_Simple,NULL);
							if((pmoddef->eType & kModType_Magnitude) && pmoddef->pchTableDefault)
							{
								F32 fTable;
								// TODO(JW): Support sidekicking
								S32 iLevel = item_GetLevel(ppItems[i]);
								if(!iLevel)
								{
									PowerApplication *papp = exprContextGetVarPointerUnsafePooled(pContext,s_pchApplication);
									if(papp)
										iLevel = papp->iLevel;
									else if(pEnt && pEnt->pChar)
										iLevel = pEnt->pChar->iLevelCombat;
								}
								if(!iLevel)
									iLevel = 1;

								fTable = powertable_Lookup(pmoddef->pchTableDefault,iLevel-1);
								fMagnitudeMod *= fTable;
							}

							fMagnitude += fMagnitudeMod;
						}
					}
					fDPS += (pItemPowerDefRef->ScaleFactor * fMagnitude / fTimeRecharge);
				}
			}
		}
	}

	// Allow the Class to scale how much it gets from this function, under the assumption
	//  that some Classes will be dual-wielding or otherwise have odd Item combinations.
	if(pEnt && pEnt->pChar)
	{
		F32 fScale = character_PowerTableLookup(pEnt->pChar,POWERTABLE_ITEMDPSSCALE);
		if(fScale > 0)
			fDPS *= fScale;
	}

	return fDPS;
}


// Self
//  Inputs: ItemCategory name
//  Return: true if any equipped weapon (item equipped in specific bags/slots?) meets the requirement of the given ItemCategory, otherwise false
AUTO_EXPR_FUNC(CEFuncsSelf);
S32 DDWeaponReq(ExprContext *pContext,
				ACMD_EXPR_ENUM(ItemCategory) const char* itemCategoryName)
{
	int i;
	int iCat = StaticDefineIntGetInt(ItemCategoryEnum,itemCategoryName);
	Item **ppItems = NULL;
	GameAccountDataExtract *pExtract = NULL; // No entity to use

	inv_ent_FindEquippedItemsByCategory(exprContextGetSelfPtr(pContext),kItemCategory_DDWeapon,&ppItems,pExtract);

	for(i=eaSize(&ppItems)-1; i>=0; i--)
	{
		ItemDef *pItemDef = GET_REF(ppItems[i]->hItem);
		assert(pItemDef);
		if(-1!=eaiFind(&pItemDef->peCategories,iCat))
		{
			break;
		}
	}

	eaDestroy(&ppItems);
	return (i>=0);
}

// Self
//  Inputs: ItemCategory name
//  Return: true if any equipped Shield (item equipped in specific bags/slots?) meets the requirement of the given ItemCategory, otherwise false
AUTO_EXPR_FUNC(CEFuncsSelf);
S32 DDShieldReq(ExprContext *pContext,
				ACMD_EXPR_ENUM(ItemCategory) const char* itemCategoryName)
{
	int i;
	int iCat = StaticDefineIntGetInt(ItemCategoryEnum,itemCategoryName);
	Item **ppItems = NULL;
	GameAccountDataExtract *pExtract = NULL; // No entity to use

	inv_ent_FindEquippedItemsByCategory(exprContextGetSelfPtr(pContext),kItemCategory_DDShield,&ppItems,pExtract);

	for(i=eaSize(&ppItems)-1; i>=0; i--)
	{
		ItemDef *pItemDef = GET_REF(ppItems[i]->hItem);
		assert(pItemDef);
		if(-1!=eaiFind(&pItemDef->peCategories,iCat))
		{
			break;
		}
	}

	eaDestroy(&ppItems);
	return (i>=0);
}

// Self
//  Inputs: ItemCategory name
//  Return: true if any equipped Implement (item equipped in specific bags/slots?) meets the requirement of the given ItemCategory, otherwise false
AUTO_EXPR_FUNC(CEFuncsSelf);
S32 DDImplementReq(ExprContext *pContext,
				ACMD_EXPR_ENUM(ItemCategory) const char* itemCategoryName)
{
	int i;
	int iCat = StaticDefineIntGetInt(ItemCategoryEnum,itemCategoryName);
	Item **ppItems = NULL;
	GameAccountDataExtract *pExtract = NULL; // No entity to use

	inv_ent_FindEquippedItemsByCategory(exprContextGetSelfPtr(pContext),kItemCategory_DDImplement,&ppItems,pExtract);

	for(i=eaSize(&ppItems)-1; i>=0; i--)
	{
		ItemDef *pItemDef = GET_REF(ppItems[i]->hItem);
		assert(pItemDef);
		if(-1!=eaiFind(&pItemDef->peCategories,iCat))
		{
			break;
		}
	}

	eaDestroy(&ppItems);
	return (i>=0);
}

// Self
//  Inputs: ItemCategory name
//  Return: true if any equipped Implement (item equipped in specific bags/slots?) meets the requirement of the given ItemCategory, otherwise false
AUTO_EXPR_FUNC(CEFuncsSelf);
S32 HasItemCategoryInBag(ExprContext *pContext,
	ACMD_EXPR_ENUM(ItemCategory) const char* itemCategoryName,
	const char* pchBagName)
{
	int iCat = StaticDefineIntGetInt(ItemCategoryEnum,itemCategoryName);
	Item **ppItems = NULL;
	bool ret = false;
	GameAccountDataExtract *pExtract = NULL; // No entity to use

	inv_ent_FindItemsInBagByCategory(exprContextGetSelfPtr(pContext), iCat, pchBagName, &ppItems, pExtract);
	ret = eaSize(&ppItems) > 0;

	eaDestroy(&ppItems);
	return (ret);
}

// Self
//  Inputs: ItemCategory name
//  Return: true if any equipped Implement (item equipped in specific bags/slots?) meets the requirement of the given ItemCategory, otherwise false
AUTO_EXPR_FUNC(CEFuncsSelf);
S32 NumItemsWithCategoryInBag(ExprContext *pContext,
	ACMD_EXPR_ENUM(ItemCategory) const char* itemCategoryName,
	const char* pchBagName)
{
	int iCat = StaticDefineIntGetInt(ItemCategoryEnum,itemCategoryName);
	Item **ppItems = NULL;
	bool ret = false;
	GameAccountDataExtract *pExtract = NULL; // No entity to use

	inv_ent_FindItemsInBagByCategory(exprContextGetSelfPtr(pContext), iCat, pchBagName, &ppItems, pExtract);
	ret = eaSize(&ppItems);

	eaDestroy(&ppItems);
	return (ret);
}

// Returns the damage for a single weapon
F32 ItemWeaponDamageFromItemDef(int iPartitionIdx, SA_PARAM_OP_VALID PowerApplication *papp, SA_PARAM_OP_VALID ItemDef *pItemDef, CombatEvalMagnitudeCalculationMethod eCalcMethod)
{
	F32 fDamage = 0.f;

	if (pItemDef && pItemDef->eType != kItemType_Weapon)
	{
		Errorf("ItemWeaponDamageFromItemDef: \"%s\" is not a weapon\n", pItemDef->pchName);
		pItemDef = NULL;
	}

	if (papp && papp->pdef && pItemDef == NULL)
	{
		Errorf("ItemWeaponDamageFromItemDef: Invalid use of power (%s) that doesn't use weapon", papp->pdef->pchName);
	}

	if(pItemDef)
	{
		if (pItemDef->pItemDamageDef)
		{
			if (pItemDef->pItemDamageDef->pExprMagnitude)
			{
				// Calculate the magnitude
				fDamage = combateval_EvalNew(iPartitionIdx, pItemDef->pItemDamageDef->pExprMagnitude,
											kCombatEvalContext_Simple,NULL);

				// Apply the variance
				if (eCalcMethod == kCombatEvalMagnitudeCalculationMethod_Min || (g_CombatEvalOverrides.bEnabled && g_CombatEvalOverrides.bMinDamage))
				{
					fDamage *= (1.f - pItemDef->pItemDamageDef->fVariance);
				}
				else if (eCalcMethod == kCombatEvalMagnitudeCalculationMethod_Max || (g_CombatEvalOverrides.bEnabled && g_CombatEvalOverrides.bMaxDamage))
				{
					fDamage *= (1.f + pItemDef->pItemDamageDef->fVariance);
				}
				else if (eCalcMethod != kCombatEvalMagnitudeCalculationMethod_Average)
				{	// Normal calculation
					fDamage *= mod_VarianceAdjustment(pItemDef->pItemDamageDef->fVariance);
				}
			}

			if (pItemDef->pItemDamageDef->pchTableName && 
				pItemDef->pItemDamageDef->pchTableName[0])
			{
				fDamage *= powertable_Lookup(pItemDef->pItemDamageDef->pchTableName, pItemDef->iLevel - 1);
			}

			// Finally apply the quality multiplier
			fDamage *= item_GetWeaponDamageMultiplierByQuality(pItemDef->Quality);
		}
		else
		{
			Errorf("ItemWeaponDamageFromItemDef: Weapon \"%s\" has no damage info\n", pItemDef->pchName);
		}
	}

	return fDamage;
}

static F32 _itemWeaponDamage(ExprContext *pContext, int iPartitionIdx, CombatEvalMagnitudeCalculationMethod eMethod)
{
	// Get the power application in the context
	PowerApplication *papp = exprContextGetVarPointerUnsafePooled(pContext, s_pchApplication);

	F32 fTotalDamage = 0.f;

	if(papp && eaSize(&papp->ppEquippedItems) > 0)
	{
		FOR_EACH_IN_EARRAY(papp->ppEquippedItems, Item, pItem)
		{
			if (pItem)
			{
				fTotalDamage += ItemWeaponDamageFromItemDef(iPartitionIdx, papp, GET_REF(pItem->hItem), eMethod);
			}
		}
		FOR_EACH_END
	}
	// Fall back to override items in case of no power application or no equipped weapons in the application
	else if(g_CombatEvalOverrides.bEnabled && eaSize(&g_CombatEvalOverrides.ppWeapons) > 0)
	{
		FOR_EACH_IN_EARRAY(g_CombatEvalOverrides.ppWeapons, Item, pWeapon)
		{
			if (pWeapon)
			{
				fTotalDamage += ItemWeaponDamageFromItemDef(iPartitionIdx, papp, GET_REF(pWeapon->hItem), eMethod);
			}
		}
		FOR_EACH_END
	}
	// retiring this error for now - generally just means you're not holding a weapon while mousing over a power
	/*else if (papp && papp->pdef && isDevelopmentMode())
	{
		Errorf("Invalid use of ItemWeaponDamage: No weapons available to evaluate damage for power (%s)", papp->pdef->pchName);
	}*/

	return fTotalDamage;
}

// Self
//  Inputs: none
//  Return: Damage calculated from all equipped weapons
AUTO_EXPR_FUNC(CEFuncsSelf);
F32 ItemWeaponDamage(ExprContext *pContext, ACMD_EXPR_PARTITION iPartitionIdx)
{
	return _itemWeaponDamage(pContext, iPartitionIdx, kCombatEvalMagnitudeCalculationMethod_Normal);
}

// Self
//  Inputs: none
//  Return: Damage calculated from all equipped weapons
AUTO_EXPR_FUNC(CEFuncsSelf);
F32 ItemWeaponDamageNoVariance(ExprContext *pContext, ACMD_EXPR_PARTITION iPartitionIdx)
{
	return _itemWeaponDamage(pContext, iPartitionIdx, kCombatEvalMagnitudeCalculationMethod_Average);
}

// Returns the damage for a single weapon
static S32 DDWeaponDamageFromItemDef(int iPartitionIdx, PowerApplication *papp, ItemDef *pItemDef, F32 factor)
{
	S32 iDamage = 0;

	if (pItemDef && pItemDef->eType != kItemType_Weapon)
	{
		Errorf("DDWeaponDamageFromItemDef: \"%s\" is not a weapon\n",pItemDef->pchName);
		pItemDef = NULL;
	}
	
	if (papp && papp->pdef && pItemDef == NULL)
	{
		Errorf("DDWeaponDamageFromItemDef: Invalid use of power (%s) that doesn't use weapon",papp->pdef->pchName);
	}
	
	if(pItemDef)
	{
		if (pItemDef->pItemWeaponDef)
		{
			const int iDieSize = pItemDef->pItemWeaponDef->iDieSize;
			int iDieCount = pItemDef->pItemWeaponDef->iNumDice;

			// Apply the input factor
			iDieCount *= factor;

			if((papp && papp->critical.bSuccess) || (g_CombatEvalOverrides.bEnabled && g_CombatEvalOverrides.bMaxDamage))
			{
				// Critical means max on each die
				iDamage = iDieCount * iDieSize;
			}
			else if ((papp && papp->bGlancing) || (g_CombatEvalOverrides.bEnabled && g_CombatEvalOverrides.bMinDamage))
			{
				iDamage = iDieCount * 1;
			}
			else
			{
				// Do the standard DD die rolls
				iDamage = randomDieRolls(iDieCount,iDieSize);
			}

			if (pItemDef->pItemWeaponDef->pAdditionalDamageExpr)
			{
				iDamage += combateval_EvalNew(iPartitionIdx,pItemDef->pItemWeaponDef->pAdditionalDamageExpr,kCombatEvalContext_Simple,NULL);
			}
		}
		else
		{
			Errorf("DDWeaponDamageFromItemDef: Weapon \"%s\" has no damage info\n",pItemDef->pchName);
		}
	}

	return iDamage;
}

// Self
//  Inputs: factor
//  Return: damage as a result of calculating the Application's Item's "Weapon" PowerDef's damage a factor number of times
AUTO_EXPR_FUNC(CEFuncsSelf);
F32 DDWeaponDamage(ExprContext *pContext, ACMD_EXPR_PARTITION iPartitionIdx, F32 factor)
{
	S32 iDamage = 0;
	S32 iItemCount = 0;

	// The context should have all the necessary data.  Right now this function makes tons of assumptions.

	PowerDef *pdef = NULL;

	PowerApplication *papp = exprContextGetVarPointerUnsafePooled(pContext,s_pchApplication);

	if(papp && eaSize(&papp->ppEquippedItems) > 0)
	{
		FOR_EACH_IN_EARRAY(papp->ppEquippedItems, Item, pItem)
		{
			if (pItem)
			{
				iDamage += DDWeaponDamageFromItemDef(iPartitionIdx, papp, GET_REF(pItem->hItem), factor);
				++iItemCount;
			}
		}
		FOR_EACH_END
	}
	else if(g_CombatEvalOverrides.bEnabled && eaSize(&g_CombatEvalOverrides.ppWeapons) > 0)
	{
		FOR_EACH_IN_EARRAY(g_CombatEvalOverrides.ppWeapons, Item, pWeapon)
		{
			if (pWeapon)
			{
				iDamage += DDWeaponDamageFromItemDef(iPartitionIdx, papp, GET_REF(pWeapon->hItem), factor);
				++iItemCount;
			}
		}
		FOR_EACH_END
	}
	else if (papp && papp->pdef && isDevelopmentMode())
	{
		Errorf("Invalid use of DDWeaponDamage: No weapons available to evaluate damage for power (%s)",papp->pdef->pchName);
	}
	
#ifdef GAMECLIENT
	if (iItemCount == 0)
	{
		int iDieSize, iDieCount;
		iDieSize = 4;	//pretend we're wielding a 1d4 weapon in char creation (fists are 1d4, and we can be unarmed in-game as well)
		iDieCount = 1;
		if((papp && papp->critical.bSuccess) || (g_CombatEvalOverrides.bEnabled && g_CombatEvalOverrides.bMaxDamage))
		{
			// Critical means max on each die
			iDamage = iDieCount * iDieSize * factor;
		}
		else if ((papp && papp->bGlancing) || (g_CombatEvalOverrides.bEnabled && g_CombatEvalOverrides.bMinDamage))
		{
			iDamage = iDieCount * 1 * factor;
		}
	}
#endif
	return (F32)iDamage;
}

// Self
//  Inputs: none
//  Return: Power factor of weapon in the PowerApplication context
AUTO_EXPR_FUNC(CEFuncsSelf);
ExprFuncReturnVal ItemGetPowerFactor(ExprContext *pContext, ACMD_EXPR_INT_OUT intOut, ACMD_EXPR_ERRSTRING errString)
{
	Item *pSourceItem = exprContextGetVarPointerUnsafePooled(pContext,s_pchSourceItem);
	
	if (pSourceItem)
	{
		*intOut = item_GetPowerFactor(pSourceItem);
		return ExprFuncReturnFinished;
	}

	estrPrintf(errString, "Invalid use of ItemGetPowerFactor inside power that doesn't use an item");
	return ExprFuncReturnError;

}

// Self
//  Inputs: none
//  Return: Quality of source item in the PowerApplication context
AUTO_EXPR_FUNC(CEFuncsSelf);
ExprFuncReturnVal SourceItemGetQuality(ExprContext *pContext, ACMD_EXPR_INT_OUT intOut, ACMD_EXPR_ERRSTRING errString)
{
	Item *pSourceItem = exprContextGetVarPointerUnsafePooled(pContext,s_pchSourceItem);

	if (pSourceItem)
	{
		*intOut = item_GetQuality(pSourceItem);
		return ExprFuncReturnFinished;
	}

	estrPrintf(errString, "Invalid use of ItemGetQuality inside power that doesn't use an item");
	return ExprFuncReturnError;

}

//designer-friendly wrapper functions that may have different functionality later.
AUTO_EXPR_FUNC(CEFuncsSelf);
ExprFuncReturnVal DDGetEnhancementBonus(ExprContext* pContext, ACMD_EXPR_INT_OUT intOut, ACMD_EXPR_ERRSTRING errString)
{
	return ItemGetPowerFactor(pContext, intOut, errString);
}

AUTO_EXPR_FUNC(CEFuncsSelf);
ExprFuncReturnVal DDGetScaledEnhancementBonus(ExprContext* pContext, ACMD_EXPR_INT_OUT intOut, F32 scalar, ACMD_EXPR_ERRSTRING errString)
{
	ExprFuncReturnVal retval = DDGetEnhancementBonus(pContext, intOut, errString);
	*intOut *= scalar;
	return retval;
}

static void GetNearestCreatedEntityWithTag(	ACMD_EXPR_ENTARRAY_OUT entsOut, const Vec3 vSourcePos, 
											Character *pChar, EntityRef erIgnore, SA_PARAM_OP_VALID const char* pchTag)
{
	Entity *pClosest = NULL;
	F32 fClosestDistSQR = FLT_MAX;
	S32 powerTag = pchTag ? StaticDefineIntGetInt(PowerTagsEnum, pchTag) : -1;
	EntityRef erSource = entGetRef(pChar->pEntParent);
	S32 iPartitionIdx = entGetPartitionIdx(pChar->pEntParent);
	

	FOR_EACH_IN_EARRAY(pChar->modArray.ppMods, AttribMod, pAttribMod)
	{
		if (pAttribMod->erCreated && pAttribMod->erCreated != erSource)
		{
			AttribModDef *pDef = mod_GetDef(pAttribMod);
			if (pDef->offAttrib == kAttribType_ProjectileCreate || pDef->offAttrib == kAttribType_EntCreate)
			{
				if (powerTag == -1 || eaiFind(&pDef->tags.piTags, powerTag) >= 0)
				{
					Entity *pEnt = entFromEntityRef(iPartitionIdx, pAttribMod->erCreated);
					if (pEnt)
					{
						Vec3 vPos;
						F32 fDistSQR;
						entGetPos(pEnt, vPos);
						fDistSQR = distance3Squared(vPos, vSourcePos);
						if (fDistSQR < fClosestDistSQR)
						{
							fClosestDistSQR = fDistSQR;
							pClosest = pEnt;
						}
					}
				}
			}
		}
	}
	FOR_EACH_END

	if (pClosest)
	{
		eaPush(entsOut, pClosest);
	}
}

// gets the nearest sibling entity to the given entity by looking at the entity's creator and searching
// through all entCreated and projectileCreated attribs to find those entities.
AUTO_EXPR_FUNC(CEFuncsTeleport) ACMD_NAME(TeleportGetNearestSiblingEntityWithPowerTag);
ExprFuncReturnVal exprFuncTeleportGetNearestSiblingEntityWithTag(	ACMD_EXPR_ENTARRAY_OUT entsOut, 
																	SA_PARAM_OP_VALID Character* sourceChar, 
																	SA_PARAM_OP_VALID const char* pchTag)
{
	eaClear(entsOut);

	if (sourceChar)
	{
		Vec3 vSourcePos;
		Entity *pCreatorEnt = entFromEntityRef(entGetPartitionIdx(sourceChar->pEntParent), sourceChar->pEntParent->erCreator);

		entGetPos(sourceChar->pEntParent, vSourcePos);

		if (pCreatorEnt && pCreatorEnt->pChar)
		{
			GetNearestCreatedEntityWithTag(entsOut, vSourcePos, pCreatorEnt->pChar, entGetRef(sourceChar->pEntParent), pchTag);
		}
	}

	return ExprFuncReturnFinished;
}

// gets the nearest created entity by searching through all entCreated and projectileCreated attribs to find those entities.
AUTO_EXPR_FUNC(CEFuncsTeleport) ACMD_NAME(TeleportGetNearestChildEntity);
ExprFuncReturnVal exprFuncTeleportGetNearestChildEntity(ACMD_EXPR_ENTARRAY_OUT entsOut, 
														SA_PARAM_OP_VALID Character* sourceChar, 
														SA_PARAM_OP_VALID const char* pchTag)
{
	eaClear(entsOut);

	if (sourceChar)
	{
		Vec3 vSourcePos;
	
		entGetPos(sourceChar->pEntParent, vSourcePos);

		GetNearestCreatedEntityWithTag(entsOut, vSourcePos, sourceChar, 0, pchTag);
	}

	return ExprFuncReturnFinished;
}


// Utility lookup functions
CombatEvalPrediction combateval_GetPrediction(ExprContext *pContext)
{
	if(entIsServer())
	{
		MultiVal *pmv = exprContextGetSimpleVarPooled(pContext,s_pchPrediction);
		if(pmv)
		{
			bool bValid;
			CombatEvalPrediction ePrediction = MultiValGetInt(pmv,&bValid);
			if(bValid)
			{
				return ePrediction;
			}
		}
	}
	return kCombatEvalPrediction_None;
}

static void SetAttribOverride(const char *pchAttrib, F32 fValue)
{
	S32 eAttrib = StaticDefineIntGetInt(AttribTypeEnum, pchAttrib);
	if(eAttrib>=0)
	{
		F32 *pf = F32PTR_OF_ATTRIB(&g_CombatEvalOverrides.attribs, eAttrib);
		*pf = fValue;
	}
}

// Normalizes attribs for STO so that auto descriptions using them can display correctly
void STONormalizeAttribs(Entity *pEnt)
{
	static int bInit = false;
	static AttribType eCrewMax = kAttribType_Null;
	if(!bInit)
	{
		memset(&g_CombatEvalOverrides.attribs,0,g_iCharacterAttribSizeUsed);
		SetAttribOverride("WeaponPowerCur", 50);
		SetAttribOverride("ShieldPowerCur", 50);
		SetAttribOverride("EnginePowerCur", 50);
		SetAttribOverride("AuxiliaryPowerCur", 50);
		eCrewMax = StaticDefineIntGetIntDefault(AttribTypeEnum, "CrewMax", kAttribType_Null);
		bInit = true;
	}
	if (eCrewMax != kAttribType_Null)
	{
		F32 fCrew = pEnt && pEnt->pChar ? character_GetClassAttrib(pEnt->pChar, kClassAttribAspect_Basic, eCrewMax) : 0;
		if (fCrew == 0)
			fCrew = 100;
		SetAttribOverride("CrewCur", fCrew);
		SetAttribOverride("CrewMax", fCrew);
	}
}


AUTO_STARTUP(ExpressionSCRegister);
void combateval_RegisterStaticCheck(void)
{
	exprRegisterStaticCheckArgumentType("PowerTable", NULL, StaticCheckPowerTable);
	exprRegisterStaticCheckArgumentType("PowerVar", NULL, StaticCheckPowerVar);
}

// Self
//  Inputs: None
//  Return: 1 if the player is in a guild, otherwise 0
AUTO_EXPR_FUNC(CEFuncsSelf);
int PlayerInGuildSelf(ExprContext* pContext)
{
    Entity *pent = exprContextGetSelfPtr(pContext);
    return guild_IsMember(pent);
}


// Gets the value of a GroupProjectNumeric from a player.
AUTO_EXPR_FUNC(CEFuncsSelf) ACMD_NAME(GetGuildProjectNumericValueFromPlayerSelf);
ExprFuncReturnVal
exprGroupProject_GetGuildProjectNumericValueFromPlayerSelf(ExprContext *pContext, ACMD_EXPR_INT_OUT pRet, 
    const char *projectName, const char *numericName, ACMD_EXPR_ERRSTRING errString)
{
    Entity *playerEnt = exprContextGetSelfPtr(pContext);

    if ( GroupProject_GetNumericFromPlayerExprHelper(playerEnt, GroupProjectType_Guild, projectName, numericName, pRet, errString) )
    {
        return ExprFuncReturnFinished;
    }
    else
    {
        return ExprFuncReturnError;
    }
}

// Gets the value of a GroupProjectUnlock from a player.
AUTO_EXPR_FUNC(CEFuncsSelf) ACMD_NAME(GetGuildProjectUnlockFromPlayerSelf);
ExprFuncReturnVal
exprGroupProject_GetGuildProjectUnlockFromPlayerSelf(ExprContext *pContext, ACMD_EXPR_INT_OUT pRet, 
    const char *projectName, const char *unlockName, ACMD_EXPR_ERRSTRING errString)
{
    Entity *playerEnt = exprContextGetSelfPtr(pContext);

    if ( GroupProject_GetUnlockFromPlayerExprHelper(playerEnt, GroupProjectType_Guild, projectName, unlockName, pRet, errString) )
    {
        return ExprFuncReturnFinished;
    }
    else
    {
        return ExprFuncReturnError;
    }
}

// Initialize the combateval contexts
void combateval_Init(S32 bInit)
{
	ContextInitSimple(bInit);
	ContextInitEnhance(bInit);
	ContextInitActivate(bInit);
	ContextInitTarget(bInit);
	ContextInitApply(bInit);
	ContextInitAffects(bInit);
	ContextInitExpiration(bInit);
	ContextInitEntCreateEnhancements(bInit);
	ContextInitTeleport(bInit);
}
