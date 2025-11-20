#pragma once
GCC_SYSTEM
/***************************************************************************
*     Copyright (c) 2006-2007, Cryptic Studios
*     All Rights Reserved
*     Confidential Property of Cryptic Studios
***************************************************************************/

// PowerVars and PowerTables are named variables and tables that are exposed
//  to expressions in the powers system.  Globally defined vars and tables
//  (in defs/powervars) can be overridden by a var or table in a 
//  CharacterClass, so generally when a lookup is done with respect to a
//  Character, it is done through the the character's class.

// PowerStats work the same way, but are more complicated.  They're a named
//  binding of a source attrib (the stat) to a target attrib, aspect and expression.
//  This stat applies the expression to the target attrib/aspect pair during
//  innate processing.  This allows an attribute like "Strength" to innately affect
//  a character's physical damage output by affecting the StrFactPos of the various
//  physical damage attributes.

#include "referencesystem.h"
#include "MultiVal.h" // For multival struct
#include "structDefines.h"	// For StaticDefineInt

#include "CharacterAttribsMinimal.h" // For enums
#include "CharacterClass.h"

// Forward declarations
typedef struct	Expression			Expression;
typedef struct	Character			Character;
typedef struct	CharacterAttribs	CharacterAttribs;
typedef struct	CharacterClass		CharacterClass;
typedef struct	PowerDef			PowerDef;
typedef struct	PTNode				PTNode;
typedef struct	PTNodeDef			PTNodeDef;
typedef struct	StashTableImp*		StashTable;
extern StaticDefineInt AttribTypeEnum[];
extern StaticDefineInt AttribAspectEnum[];

// Defines for special tables
#define POWERTABLE_ITEMDPS "Auto_Attack"
#define POWERTABLE_ITEMDPSSCALE "ItemDPSScale"
#define POWERTABLE_BASICFACTBONUSHITPOINTSMAX "BasicFactBonusHitPointsMax"


/***** ENUMS *****/

/***** END ENUMS *****/

// A named MultiVal
AUTO_STRUCT;
typedef struct PowerVar
{
	char *pchName;			AST(STRUCTPARAM)
		// Name of the variable

	MultiVal mvValue;		AST(NAME(Value))
		// MultiVal that defines the variable

	const char *cpchFile;	AST(NAME(File), CURRENTFILE)
		// Current file (required for reloading)
} PowerVar;

// A named array of either F32 or other tables (multi-table)
AUTO_STRUCT;
typedef struct PowerTable
{
	const char *pchName;			AST(STRUCTPARAM, POOL_STRING)
		// Name of the table

	F32 *pfValues;					AST(NAME(Values))
		// F32 EArray for normal tables

	const char **ppchTables;		AST(NAME(Tables), POOL_STRING)
		// char* EArray for multi-tables

	const char *pchNumericOverride;	AST(NAME(Numeric), POOL_STRING)
		// Override numeric for this power table

	U32 bValuesNegative : 1;		AST(NO_TEXT_SAVE)
		// Set to true if this is a normal table and any values are negative

	U32 bValuesZero : 1;			AST(NO_TEXT_SAVE)
		// Set to true if this is a normal table and any values are zero

	U32 bUsePointsEarned : 1;		AST(NAME(UsePointsEarned))
		// Set to true if this table uses points earned rather than points
		// spent for determining training level

	const char *cpchFile;			AST(NAME(File), CURRENTFILE)
		// Current file (required for reloading)
} PowerTable;

AUTO_STRUCT;
typedef struct PowerStatSource
{
	AttribType offSourceAttrib;		AST(NAME(AttribSource), SUBTABLE(AttribTypeEnum), STRUCTPARAM, REQUIRED)
		// The source attrib for this factor of the stat

	F32 fMultiplier;				AST(NAME(Multiplier), DEFAULT(1.0f), STRUCTPARAM)
		// The multiplier this of this attrib
} PowerStatSource;

AUTO_STRUCT;
typedef struct PowerStatBonusEntry
{
	// Offset to the attribute in CharacterAttribs, defines the target attrib
	AttribType offTargetAttrib;			AST(NAME(TargetAttrib), SUBTABLE(AttribTypeEnum) SELF_ONLY)

	// Offset to the CharacterAttribs structure in Character, defines which aspect of the target attrib is affected
	AttribAspect offTargetAspect;		AST(NAME(Aspect), SUBTABLE(AttribAspectEnum) SELF_ONLY)		

	// The absolute bonus amount
	F32 fBonus;							AST(SELF_ONLY)
} PowerStatBonusEntry;
extern ParseTable parse_PowerStatBonusEntry[];
#define TYPE_parse_PowerStatBonusEntry PowerStatBonusEntry

AUTO_STRUCT;
typedef struct PowerStatBonus
{
	// Offset to the attribute in CharacterAttribs, defines the source attrib
	AttribType offSourceAttrib;			AST(KEY NAME(SourceAttrib), SUBTABLE(AttribTypeEnum) SELF_ONLY)

	// The list of attributes this source attribute affects
	PowerStatBonusEntry **ppEntries;	AST(SELF_ONLY)
} PowerStatBonus;
extern ParseTable parse_PowerStatBonus[];
#define TYPE_parse_PowerStatBonus PowerStatBonus

AUTO_STRUCT;
typedef struct PowerStatBonusData
{
	// The list of all attributes in the power stat tables affecting other attributes
	PowerStatBonus **ppBonusList;		AST(SELF_ONLY)

	// The dirty bit always comes last
	DirtyBit dirtyBit;					AST(NO_NETSEND)
} PowerStatBonusData;
extern ParseTable parse_PowerStatBonusData[];
#define TYPE_parse_PowerStatBonusData PowerStatBonusData

// A named stat effect
AUTO_STRUCT;
typedef struct PowerStat
{
	char *pchName;					AST(STRUCTPARAM)
		// Name of the effect

	PowerStatSource **ppSourceStats;	AST(NAME(Stat))
		// Offset to the attribute in CharacterAttribs, defines the source attribs
		//  of this effect, also known as the 'stat'

	const char *pchSourceNumericItem;	AST(NAME(StatItem), POOL_STRING)
		// Instead of an attribute, we can use a numeric item as the 'stat'

	AttribType offTargetAttrib;		AST(NAME(Attrib), SUBTABLE(AttribTypeEnum))
		// Offset to the attribute in CharacterAttribs, defines the target attrib
		//  of this effect

	AttribAspect offTargetAspect;	AST(NAME(Aspect), SUBTABLE(AttribAspectEnum))
		// Offset to the CharacterAttribs structure in Character, defines which
		//  aspect of the target attrib is affected

	Expression* expr;				AST(NAME(ExprBlock), REDUNDANT_STRUCT(expr, parse_Expression_StructParam), LATEBIND)
		// The expression that defines the change to the target attrib/aspect based
		//  on the source attrib

	const char *pchPowerTreeNodeRequired;	AST(NAME(PowerTreeNodeRequired), POOL_STRING)
		// If set, the Character must own this PowerTreeNode for the stat to be active
		//  NOTE: The Character may own the PowerDefRequired instead

	const char *pchPowerDefRequired;		AST(NAME(PowerDefRequired), POOL_STRING)
		// If set, the Character must own this Power for the stat to be active
		//  NOTE: The Character may own the PowerTreeNodeRequired instead
		//  Not REF_TO because these are loaded into SM directly before PowerDefs are loaded

	CharClassCategory eClassCategoryRequired;	AST(NAME(ClassCategoryRequired) SUBTABLE(CharClassCategoryEnum) DEFAULT(CharClassCategory_None))

	U32 bInactive : 1;					NO_AST
		// Set to true for all stats that don't actually do anything
		//  which can be the case in an override situation

	const char *pchTag;				AST(NAME(Tag), POOL_STRING)
		// A UI hint for sorting stat tooltips based 
		// Currently just used for SuperStats and Secondary SuperStats

	const char *cpchFile;			AST(NAME(File), CURRENTFILE)
		// Current file (required for reloading)
} PowerStat;

// Wrapper for list of all power vars
AUTO_STRUCT;
typedef struct PowerVars
{
	PowerVar **ppPowerVars;			AST(NAME(PowerVar))
		// EArray of all power vars

	StashTable stPowerVars;			AST(USERFLAG(TOK_NO_WRITE))
		// Handy stash table of the power vars, built at load
} PowerVars;

// Wrapper for list of all power tables
AUTO_STRUCT;
typedef struct PowerTables
{
	PowerTable **ppPowerTables;		AST(NAME(PowerTable))
		// EArray of all power tables

	StashTable stPowerTables;		AST(USERFLAG(TOK_NO_WRITE))
		// Handy stash table of the power tables, built at load

	U32 bBasicFactBonusHitPointsMax : 1;
		// True if the POWERTABLE_BASICFACTBONUSHITPOINTSMAX exists
} PowerTables;

extern PowerVars g_PowerVars;
extern PowerTables g_PowerTables;

// Wrapper for list of all power stats
AUTO_STRUCT;
typedef struct PowerStats
{
	PowerStat **ppPowerStats;		AST(NAME(PowerStat))
		// EArray of all power stats
} PowerStats;

extern PowerStats g_PowerStats;




// Returns the named variable, or NULL if the variable doesn't exist
SA_RET_OP_VALID MultiVal *powervar_Find(SA_PARAM_NN_STR const char *pchName);


// Returns the named power table, or NULL if the table doesn't exist
SA_RET_OP_VALID PowerTable *powertable_Find(SA_PARAM_NN_STR const char *pchName);

// Returns the best value in the named table, or 0 if the table doesn't exist.
//  If the table is a multi-table, it will recurse up to one level to find the proper table.
F32 powertable_LookupMulti(SA_PARAM_NN_STR const char *pchName, S32 idx, S32 idxMulti);
F32 powertable_SumMulti(const char *pchName, S32 iMin, S32 iMax, S32 idxMulti);

// Returns the value in the named table, or 0 if the table or index doesn't exist.
//  If the table is a multi-table, it uses the 0th subtable.
F32 powertable_Lookup(SA_PARAM_NN_STR const char *pchName, S32 idx);

// Validates that a PowerTable is properly constructed
S32 powertable_Validate(SA_PARAM_NN_VALID PowerTable *pTable, SA_PARAM_OP_STR const char *cpchFile);

// Does load-time generation of derived data in a PowerTable
void powertable_Generate(SA_PARAM_NN_VALID PowerTable *pTable);

// Compresses a PowerTable
void powertable_Compress(SA_PARAM_NN_VALID PowerTable *pTable);


// Returns the output value of the stat, given the input stats, character level, and optional node rank
F32 powerstat_Eval(SA_PARAM_NN_VALID PowerStat *pStat,
				   SA_PARAM_OP_VALID CharacterClass *pClass,
				   SA_PARAM_NN_VALID F32 *pfStats,
				   S32 iLevel,
				   S32 iNodeRank);

// Returns if the PowerStat would be active for the Character
//  Does not check if the Character's actually has access to the PowerStat
//  Returns the PowerStat's required PTNode if ppNodeOut is provided
S32 powerstat_Active(SA_PARAM_NN_VALID PowerStat *pStat,
						SA_PARAM_NN_VALID Character *pchar,
						SA_PARAM_OP_VALID PTNode **ppNodeOut);

// Returns true if the attribs that define the stats have changed
bool powerstats_CheckDirty(SA_PRE_NN_NN_VALID PowerStat **ppStats, SA_PARAM_NN_VALID CharacterAttribs *pOldAttribs, SA_PARAM_NN_VALID CharacterAttribs *pNewAttribs);

// Fills the given char* earray with the names of the power tables
void powertables_FillNameEArray(const char ***pppchNames);
void powertables_FillAllocdNameEArray(const char ***pppchNames);

