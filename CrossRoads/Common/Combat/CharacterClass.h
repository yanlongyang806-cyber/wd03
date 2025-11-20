/***************************************************************************
*     Copyright (c) 2006-2007, Cryptic Studios
*     All Rights Reserved
*     Confidential Property of Cryptic Studios
***************************************************************************/
#ifndef CHARACTERCLASS_H__
#define CHARACTERCLASS_H__
GCC_SYSTEM

#include "referencesystem.h"
#include "textparser.h" // For StaticDefineInt
#include "StashTable.h" // For StashTable struct
#include "Message.h" 

#include "CharacterAttribsMinimal.h" // For enums
#include "EntEnums.h"
#include "CombatEnums.h"
#include "WorldLibEnums.h"

#define MAX_CHARACTERCLASS_LEN		40
#define MAX_LEVELS					70

// Forward declarations
typedef struct AttribCurve					AttribCurve;
typedef struct Character					Character;
typedef struct NOCONST(Character)			NOCONST(Character);
typedef struct CharacterAttribs				CharacterAttribs;
typedef struct CharacterPath				CharacterPath;
typedef struct CombatReactivePowerDef		CombatReactivePowerDef;
typedef struct Expression					Expression;
typedef struct ItemArt						ItemArt;
typedef struct MultiVal						MultiVal;
typedef struct NearDeathConfig				NearDeathConfig;
typedef struct PowerDef						PowerDef;
typedef struct PowerSlot					PowerSlot;
typedef struct PowerStat					PowerStat;
typedef struct PowerTable					PowerTable;
typedef struct PowerVar						PowerVar;
typedef struct CritterDef					CritterDef;
typedef struct DefaultInventory				DefaultInventory;
typedef struct DefaultItemDef				DefaultItemDef;
typedef struct ItemDefRef					ItemDefRef;
typedef struct DefaultTray					DefaultTray;
typedef struct Entity						Entity;
typedef struct PowerTreeDef					PowerTreeDef;
typedef struct PTNode						PTNode;
typedef struct PTNodeDef					PTNodeDef;
typedef struct AssignedStats				AssignedStats;
typedef struct NOCONST(AssignedStats)		NOCONST(AssignedStats);
typedef struct SpeciesDefRef				SpeciesDefRef;
typedef struct TacticalRequesterAimDef		TacticalRequesterAimDef;
typedef struct TacticalRequesterRollDef		TacticalRequesterRollDef;
typedef struct TacticalRequesterSprintDef	TacticalRequesterSprintDef;
typedef struct NOCONST(Entity)				NOCONST(Entity);
typedef struct PuppetEntity					PuppetEntity;
typedef struct NOCONST(GameAccountData)		NOCONST(GameAccountData);
typedef struct GameAccountData				GameAccountData;
typedef struct PlayerCostume				PlayerCostume;
typedef struct PowerTreeDefRef				PowerTreeDefRef;
typedef struct RegionRules					RegionRules;


#define CHAR_TYPE_ID_FILE "CharacterType"
#define CHAR_CATEGORY_ID_FILE "CharacterClassCategories"
#define CHAR_PATH_TYPES_ID_FILE "CharacterPathTypes"

AUTO_STRUCT;
typedef struct CharClassTypeExtraIDs
{
	const char **eachID; AST(NAME(ID))

} CharClassTypeExtraIDs;

AUTO_STRUCT;
typedef struct CharClassCategories
{
	const char **pchNames; AST(NAME(Category))
} CharClassCategories;

extern DefineContext *g_pExtraCharClassCategories;
AUTO_ENUM AEN_EXTEND_WITH_DYNLIST(g_pExtraCharClassCategories);
typedef enum CharClassCategory
{
	CharClassCategory_None = 0, ENAMES(None)
} CharClassCategory;

extern StaticDefineInt CharClassCategoryEnum[];

AUTO_STRUCT;
typedef struct CharClassCategorySet
{
	const char* pchName;				AST(POOL_STRING STRUCTPARAM KEY)
	REF_TO(Message) hDisplayName;		AST(NAME(DisplayName))
	CharClassTypes eClassType;			AST(NAME(ClassType) SUBTABLE(CharClassTypesEnum))
	U32 *eaCategories;					AST(NAME(Category) SUBTABLE(CharClassCategoryEnum))
	bool bAllowDeletionWhileActive;		AST(NAME(AllowDeletionWhileActive) BOOLFLAG)
	bool bDefaultPreferredSet;			AST(NAME(DefaultPreferredSet) BOOLFLAG)
} CharClassCategorySet;

// Defines adjustments to attributes this class has relative to the default
// Forward declared so that we can use it in CharacterClass, but actualled defined after so that
//  the ClassAttribAspect enum is properly defined.
typedef struct CharacterClassAdjustAttrib CharacterClassAdjustAttrib;

AUTO_STRUCT;
typedef struct PowerTableAdjustment
{
	char *pchName;		AST(STRUCTPARAM)
		// Name of the table

	F32 *pfValues;		AST(NAME(Values))
		// F32 EArray for normal tables

	char **ppchTables;	AST(NAME(Tables))
		// char* EArray for multi-tables

	Expression *pExpr; AST(NAME(ExpressionBlock) REDUNDANT_STRUCT(Expression, parse_Expression_StructParam) LATEBIND)
		//Expression to be used;

	int ilevel;		AST(NAME(Level))
		//-1 if used on every level
}PowerTableAdjustment;

// Data about a particular Attribute for the Class
AUTO_STRUCT;
typedef struct CharacterClassAttrib
{
	F32 *pfBasic;
		// Compressed earray of base Basic values.  If not allocated, the value is assumed to be 0.

	F32 *pfStrength;
		// Compressed earray of base Strength values.  If not allocated, the value is assumed to be 1.

	F32 *pfResist;
		// Compressed earray of base Resist values.  If not allocated, the value is assumed to be 1.
	
	AttribCurve **ppCurves;
		// Curves

} CharacterClassAttrib;

// Data about a Power granted by a Class
AUTO_STRUCT;
typedef struct CharacterClassPower
{
	REF_TO(PowerDef) hdef;	AST(NAME(Def))
		// The PowerDef of the granted power

} CharacterClassPower;

typedef struct CharacterClass CharacterClass;

AUTO_STRUCT;
typedef struct ClassAssignedStats
{
	// The name of the class.
	char *pchName; AST(STRUCTPARAM KEY)
		
	// The stats assigned to the class.
	AssignedStats **eaAssignedStats;	AST(NAME(AssignedStat))	
} ClassAssignedStats;

AUTO_STRUCT AST_IGNORE(exprKPM);
typedef struct CharacterClass
{
	char *pchName; AST(STRUCTPARAM KEY)
		// Internal name

	
	// Base attributes: This has GOT to be shrunk

	CharacterClassAttrib **ppAttributes;
		// Attribute data for the Class

	int *piAttribCurveBasic;
		// Derived.  EArray of Attributes that have AttribCurves on Basic aspects.
	
	CharacterAttribs **ppAttrBasic;					AST(NAME(AttrBasic) LATEBIND SERVER_ONLY)
		// Array of base basic attribs for the class

	CharacterAttribs **ppAttrStr;					AST(NAME(AttrStr) LATEBIND SERVER_ONLY)
		// Array of base strength attribs for the class

	CharacterAttribs **ppAttrRes;					AST(NAME(AttrRes) LATEBIND SERVER_ONLY)
		// Array of base resist attribs for the class

	AssignedStats **ppAutoSpendStatPoints;			AST(NAME(AutoSpend) LATEBIND SERVER_ONLY)
		// Array of how many points you should automatically spend and where.


	// Stats
		
	PowerStat **ppStats;							AST(NAME(PowerStat), SERVER_ONLY)
		// Array of changed or new PowerStats

	PowerStat **ppStatsFull;						AST(NAME(PowerStatPostInheritance))
		// Array of PowerStats, including core and overrides
	
	
	// Tables
		
	PowerTableAdjustment **ppTableAdjustments;		AST(NAME(PowerTable), SERVER_ONLY)
		// Array of changed PowerTables

	PowerTable **ppTables;							AST(NAME(PowerTablePostInheritance))
		// Array of PowerTables

	StashTable stTables;						
		// StashTable of PowerTables, used on server during runtime

	
	// Vars

	PowerVar **ppVars;								AST(NAME(PowerVar))
		// Array of changed PowerVars

	StashTable stVars;							
		// StashTable of PowerVars, used on server during runtime


	// AttribCurves
		
	AttribCurve **ppAttribCurve;					AST(NAME(AttribCurve))
		// Array of overridden diminishing returns settings

	// Custom inheritance system data

	CONST_REF_TO(CharacterClass) hParentClass;		AST(REFDICT(CharacterClass), NAME(ParentClass), SERVER_ONLY)
		// Optional parent, used at load-time to perform custom 'inheritance'

	CharacterClassAdjustAttrib **ppAdjustAttrib;	AST(NAME(AdjustAttrib), ADDNAMES(Adjustment), SERVER_ONLY)
		// Array of Attribute value adjustments


	CharacterClassPower **ppPowers;					AST(NAME(Power))
		// Array of Powers granted (INHERITED WITH NO OVERRIDES)

	PowerSlot **ppPowerSlots;						AST(NAME(PowerSlot))
		// Array of PowerSlots (INHERITED WITH NO OVERRIDES)

	CONST_EARRAY_OF(CharacterClassPower) ppExamplePowers; AST(NAME(ExamplePower))
		// Array of PowerDefs to show on the class selection screen

		
	// UI/Display data

	DisplayMessage msgDisplayName;					AST(STRUCT(parse_DisplayMessage))
		// Message to display the name

	DisplayMessage msgDescription;					AST(STRUCT(parse_DisplayMessage))
		// Message to display in the description

	DisplayMessage msgDescriptionLong;				AST(STRUCT(parse_DisplayMessage))
		// Message to display for long detailed description

	const char *pchIconName;						AST(POOL_STRING, DEFAULT("CharacterClass_Icon_Default"))
		// Icon name (for e.g. character creation)

	const char *pchPortraitName;					AST(POOL_STRING, DEFAULT("CharacterClass_Portrait_Default"))
		// Portrait name (for e.g. character creation)

	S32 iLevelRequired;								AST(DEFAULT(0))
		// The level required to get this role

	Expression *exprRequires;						AST(NAME(exprRequiresBlock) REDUNDANT_STRUCT(exprRequires, parse_Expression_StructParam) LATEBIND)
		// Other requirements for the class.  Evaluated when leveling, acquiring new 'tags' for the account, or switching classes

	U32 bPlayerClass : 1;
		// If this is a player class

	U32 bPlayerClassRestricted : 1;
		// Restricts a player class from access level == 0 players

	U32 bIgnoreClassRestrictionsOnItems : 1;		AST(NAME(IgnoreItemRestriction))

	U32 bStrafing : 1;
		// sets the movement of the entity to be in strafing mode. 
		// NOTE: the combatConfig flag bCharacterClassSpecifiesStrafing must be set otherwise this field is ignored.

	U32 bUseProximityTargetingAssistEnt : 1;
		// used for player entities only- 
		// if set, will set the proximity targeting assist entity target on the player's character. 
		// can be used to show targeted 

	// Random stuff

	REF_TO(DefaultInventory) hInventorySet;			AST(NAME(InventorySet) REFDICT(DefaultInventory))
		// The inventory model that this class must obey

	REF_TO(DefaultTray) hDefaultTray;				AST(NAME(DefaultTray) REFDICT(DefaultTray) SERVER_ONLY)
		//The default tray model for this class

	NearDeathConfig *pNearDeathConfig;			
		// The way this Class uses the NearDeath system

	REF_TO(ItemArt) hArt;							AST(NAME(Art) REFDICT(ItemArt))
		// Optional itemart for a class

	const char *pchFX;								AST(POOL_STRING STRUCTPARAM)
		// Optional Name of the FX to accompany hArt when creating itemart state 

	F32 fAutoItemDamageChance;
		// [0 .. 1] Chance that some damage dealt to the Character will automatically damage items

	F32 fAutoItemDamageProportion;
		// [0 .. 1] When automatic item damage occurs, this is the proportion of damage that goes to the items

	S32 iBasicFactBonusHitPointsMaxLevel;
		// The base Level used to provide "BasicFactBonus" HitPointsMax using the "BasicFactBonusHitPointsMax" PowerTable.
		//  Note that this Level is adjusted by LevelCombatControl rules as if it's an Item.

	F32 fNativeSpeedRunning;
		// The native running speed of the class. This is set on class load by reading the SpeedRunning attribute of the class.

	CharClassTypes eType;							AST(SUBTABLE(CharClassTypesEnum) DEFAULT(CharClassTypes_None))

	CharClassCategory eCategory;					AST(NAME(Category) SUBTABLE(CharClassCategoryEnum) DEFAULT(CharClassCategory_None))

	const char *pchDefaultPlayingStyle;				AST(POOL_STRING)

	SpeciesDefRef **eaPermittedSpecies;				AST(NAME(PermittedSpecies))
		// The list of species that can use this class, if this list is empty then it will be unrestricted
	
	TacticalRequesterRollDef	*pTacticalRollDef;
		// optional, override to the combat config's roll def

	TacticalRequesterSprintDef *pTacticalSprintDef;
		// optional, override to the combat config's sprint def

	TacticalRequesterAimDef *pTacticalAimDef;
		// optional, override to the combat config's aim def

	const char *pchCombatReactivePowerDef;			AST(NAME(CombatReactivePowerDef))
		// optional, if the character has a reactive power

	const char *pchCombatPowerStateSwitchingDef;	AST(NAME(CombatPowerStateSwitchingDef))
		// optional, if the character has a reactive power


	const char *pchReticleDef;						AST(NAME(ReticleDef))
		// optional, the ClientReticleDef that the character uses
	
	const char *cpchFile;							AST(NAME(File), CURRENTFILE)
		// Current file (required for reloading)

	const char* pchStanceWords;					AST(NAME(StanceWords))				

} CharacterClass;

// Handle to the class dictionary
extern DictionaryHandle g_hCharacterClassDict;

// The dictionary holding the assigned stats for each class
extern DictionaryHandle g_hClassAssignedStats;

// The dictionary handle for the character paths
extern DictionaryHandle g_hCharacterPathDict;

// The dictionary handle for the character class category sets
extern DictionaryHandle g_hCharacterClassCategorySetDict;

AUTO_STRUCT;
typedef struct CharacterClassRef
{
	REF_TO(CharacterClass) hClass;	AST(REFDICT(CharacterClass) STRUCTPARAM)
} CharacterClassRef;

AUTO_STRUCT;
typedef struct CharacterPathRef
{
	REF_TO(CharacterPath) hPath;	AST(REFDICT(CharacterPath) STRUCTPARAM)
} CharacterPathRef;

AUTO_STRUCT;
typedef struct CharacterClassList
{
	CharacterClassRef **ppCharacterClassList;
} CharacterClassList;

AUTO_STRUCT;
typedef struct CharacterClassNameList
{
	char **ppCharacterClassNameList;
} CharacterClassNameList;

typedef enum ClassAttribAspect
{
	kClassAttribAspect_Basic = offsetof(CharacterClass,ppAttrBasic), ENAMES(Basic)
	kClassAttribAspect_Str = offsetof(CharacterClass,ppAttrStr), ENAMES(Str)
	kClassAttribAspect_Res = offsetof(CharacterClass,ppAttrRes), ENAMES(Res)

} ClassAttribAspect;

extern StaticDefineInt ClassAttribAspectEnum[];

// Defines adjustments to attributes this class has relative to the default
AUTO_STRUCT;
typedef struct CharacterClassAdjustAttrib
{
	AttribType eType;				AST(NAME(Attrib), SUBTABLE(AttribTypeEnum))
		// Attrib

	ClassAttribAspect eAspect;		AST(NAME(Aspect), SUBTABLE(ClassAttribAspectEnum))
		// Aspect

	Expression *pExpr;				AST(NAME(ExpressionBlock) REDUNDANT_STRUCT(Expr, parse_Expression_StructParam) LATEBIND)
		// Expression that defines the adjustment

	int level;						AST(NAME(Level))

	F32 fValues[MAX_LEVELS];		AST(NAME(Values))
} CharacterClassAdjustAttrib;


AUTO_STRUCT;
typedef struct CharacterClassInfo
{
	char *pchName; AST( STRUCTPARAM KEY)
		// Internal name

	char **ppchNormal;	AST(NAME(Normal))
	char **ppchWeak;	AST(NAME(Weak))
	char **ppchTough;	AST(NAME(Tough))

	const char *cpchFile;	AST(NAME(File), CURRENTFILE)
}CharacterClassInfo;

AUTO_STRUCT;
typedef struct CharacterPathSuggestedNode
{
	// The power tree node to buy
	REF_TO(PTNodeDef) hNodeDef;								AST(NAME(NodeDef) STRUCTPARAM)

	// The number of ranks to buy from this node. The default of 0 buys all ranks from this node
	const S32 iMaxRanksToBuy;								AST(NAME(MaxRanksToBuy) ADDNAMES(Ranks,Rank))

} CharacterPathSuggestedNode;

AUTO_STRUCT;
typedef struct CharacterPathChoice
{
	// The list of power tree nodes to be purchased in order of priority
	CharacterPathSuggestedNode **eaSuggestedNodes;			AST(NAME(Node))

	PowerTreeDefRef **eaPowerTreeDefs;						AST(NAME(PowerTree))
} CharacterPathChoice;

AUTO_STRUCT;
typedef struct CharacterPathSuggestedPurchase
{
	// The name of the power table
	const char *pchPowerTable;								AST(POOL_STRING NAME(PowerTable))

	//The choices for this path
	CharacterPathChoice **eaChoices;						AST(NAME(SuggestedNode) ADDNAMES(Choice))
} CharacterPathSuggestedPurchase;

AUTO_STRUCT;
typedef struct PlayerDefaultCostumeRef
{
	REF_TO(PlayerCostume) hCostume;								AST(STRUCTPARAM REQUIRED)
} PlayerDefaultCostumeRef;

AUTO_STRUCT;
typedef struct CharacterPathTypeInfo
{
	const char* pchName;	AST(STRUCTPARAM)
	S32 iMaxNumberOwned;	AST(DEF(1))
	S32 iMinLevel;
} CharacterPathTypeInfo;

AUTO_STRUCT;
typedef struct CharacterPathTypeStructs
{
	CharacterPathTypeInfo** eaTypes; AST(NAME(Type))
} CharacterPathTypeStructs;

extern DefineContext *g_pExtraCharacterPathTypes;
AUTO_ENUM AEN_EXTEND_WITH_DYNLIST(g_pExtraCharacterPathTypes);
typedef enum CharacterPathType
{
	kCharacterPathType_Primary = -1, ENAMES(Primary)
} CharacterPathType;


AUTO_STRUCT;
typedef struct CharacterPath
{
	// The internal name of the character path
	char *pchName;											AST(STRUCTPARAM KEY)

	// Current file (required for reloading)
	const char *pchFile;									AST(CURRENTFILE)		

	// Translated display name which players see
	DisplayMessage pDisplayName;							AST(STRUCT(parse_DisplayMessage) NAME(DisplayName))

	// A header to display above description1
	DisplayMessage pHeader1;								AST(STRUCT(parse_DisplayMessage) NAME(Header1))

	// A header to display above description2
	DisplayMessage pHeader2;								AST(STRUCT(parse_DisplayMessage) NAME(Header2))

	// A header to display above description3
	DisplayMessage pHeader3;								AST(STRUCT(parse_DisplayMessage) NAME(Header3))

	// Translated description which players see
	DisplayMessage pDescription;							AST(STRUCT(parse_DisplayMessage) NAME(Description) NAME(Description1))

	// Another translated description which players see
	DisplayMessage pDescription2;							AST(STRUCT(parse_DisplayMessage) NAME(Description2))

	// Yet another translated description which players see!
	DisplayMessage pDescription3;							AST(STRUCT(parse_DisplayMessage) NAME(Description3))

	// Translated flavor text which players see
	DisplayMessage pFlavorText;								AST(STRUCT(parse_DisplayMessage) NAME(FlavorText))

	// The icon displayed to the players for this character path
	const char *pchIconName;								AST(POOL_STRING NAME(IconName))

	// The large image used for this character path. Probably going to get deprecated. 
	const char *pchLargeImage;								AST(POOL_STRING NAME(LargeImage))

	//The requirement expression to use this charater path
	Expression *pExprRequires;								AST(NAME(exprRequiresBlock) REDUNDANT_STRUCT(exprRequires, parse_Expression_StructParam) LATEBIND)

	// The power tree which this path belongs to
	REF_TO(PowerTreeDef) hPowerTree;						AST(NAME(PowerTree))

	// The suggested purchase list for each power table
	CharacterPathSuggestedPurchase **eaSuggestedPurchases;	AST(NAME(SuggestedPurchase) ADDNAMES(Purchase))

	// Assigned stats that determines the initial stats
	union
	{
		NOCONST(AssignedStats) **ppAssignedStats;			NO_AST
		AssignedStats **ppConstAssignedStats;				AST(LATEBIND NAME("AssignedStat"))
	};

	// The list of items which must be added to character's inventory during the character creation
	DefaultItemDef **eaDefaultItems;						AST(NAME(GrantItem))

	// The list of classes that the character path can limit what players can become, if no classes are required, then all classes are allowed
	CharacterClassRef **eaRequiredClasses;					AST(NAME(RequiredClass))

	// The list of items to show when previewing the character path with a costume, this is mainly for character creator UI.
	ItemDefRef **eaPreviewItems;							AST(NAME(PreviewItem))

	// The hue used for Powers
	F32 fHue;

	// Min character level to use this path.
	CharacterPathType eType;								AST(DEF(-1))

	// The Value in a game permission used to determine if this is unlocked. Used with free CharacterPaths in CO
	// Added as the expression can't be used unless there is an ent and not in a transactions
	const char *pcGamePermissionValue;						

	// The list of items which are added to the character upon a free re-train
	DefaultItemDef **eaFreeRetrainItems;					AST(NAME(FreeRetrainItems))
	
	PlayerDefaultCostumeRef **eaCostumeRefs;				AST(NAME(Costume))
		
	// Restricts a player class from being displayed unless <TODO>
	U32 bPlayerPathDevRestricted : 1;

	// Hide this path if it can't be used
	U32 bHideIfCantUse : 1;

} CharacterPath;

AUTO_STRUCT AST_CONTAINER;
typedef struct AdditionalCharacterPath
{
	CONST_REF_TO(CharacterPath) hPath;		AST(PERSIST SUBSCRIBE REFDICT(CharacterPath))
} AdditionalCharacterPath;

// Handle to the class info dictionary
extern DictionaryHandle g_hCharacterClassInfoDict;

// Sets the Character's class.  This is an autotransaction helper, and doesn't do any sanity checks.
void character_SetClassHelper(ATH_ARG SA_PARAM_NN_VALID NOCONST(Character) *pchar, SA_PARAM_NN_STR const char *cpchClass);

// returns character class info def
CharacterClassInfo * CharacterClassInfo_FindByName( char * pchName );


// Returns a pointer to Character's Class' attrib table for the given aspect at the Character's combat level
//  May return null in cases of a bad class, table, etc.
SA_RET_OP_VALID CharacterAttribs *character_GetClassAttribs(SA_PARAM_NN_VALID Character *pchar, ClassAttribAspect eAspect);

// Returns the Character's Class' attrib for the given aspect at the Character's combat level
//  Potentially faster/safer/smarter than getting the entire table
F32 character_GetClassAttrib(SA_PARAM_NN_VALID Character *pchar, ClassAttribAspect eAspect, AttribType eAttrib);


// Loads character classes
void characterclasses_Load(void);

// returns character class
CharacterClass * characterclasses_FindByName( char * pchName );




// Returns the named variable, or NULL if the variable doesn't exist.  Looks in the Class first.
SA_RET_OP_VALID MultiVal *powervar_FindInClass(SA_PARAM_NN_STR const char *pchName, SA_PARAM_NN_VALID CharacterClass *pClass);

// Returns the named power table, or NULL if the table doesn't exist.  Looks in the Class first.
SA_RET_OP_VALID PowerTable *powertable_FindInClass(SA_PARAM_NN_STR const char *pchName, SA_PARAM_NN_VALID CharacterClass *pClass);

// Returns the best value in the named table, or 0 if the table doesn't exist.
//  If the class doesn't specify the table itself, it will fall back to the default table.
//  If the table is a multi-table, it will recurse up to one level to find the proper table.
F32 class_powertable_LookupMulti(SA_PARAM_NN_VALID CharacterClass *pClass, SA_PARAM_NN_STR const char *pchName, S32 idx, S32 idxMulti);

// Returns the best value in the named table, or 0 if the table doesn't exist.
//  If the class doesn't specify the table itself, it will fall back to the default table.
//  If the table is a multi-table, it uses the 0th subtable.
F32 class_powertable_Lookup(SA_PARAM_NN_VALID CharacterClass *pClass, SA_PARAM_NN_STR const char *pchName, S32 idx);



// Returns the entire CharacterClassAttrib structure for a particular Attribute
SA_RET_OP_VALID CharacterClassAttrib *class_GetAttrib(SA_PARAM_NN_VALID CharacterClass *pClass, AttribType eAttrib);

// Returns the Class's base Basic value for the Attribute at the specified level
F32 class_GetAttribBasic(SA_PARAM_NN_VALID CharacterClass *pClass, AttribType eAttrib, S32 iLevel);

// Returns the Class's base Strength value for the Attribute at the specified level
F32 class_GetAttribStrength(SA_PARAM_NN_VALID CharacterClass *pClass, AttribType eAttrib, S32 iLevel);

// Returns the Class's base Resist value for the Attribute at the specified level
F32 class_GetAttribResist(SA_PARAM_NN_VALID CharacterClass *pClass, AttribType eAttrib, S32 iLevel);



// Get the specified AttribCurve, returns NULL if none exists
SA_RET_OP_VALID AttribCurve *class_GetAttribCurve(SA_PARAM_NN_VALID CharacterClass *pClass,
											   AttribType offAttrib,
											   AttribAspect offAspect);

// Get the array of AttribCurves for the attrib, returns NULL if none exists
SA_RET_OP_VALID AttribCurve **class_GetAttribCurveArray(SA_PARAM_NN_VALID CharacterClass *pClass,
													 AttribType offAttrib);

// Returns true if the Character's Class's type is in the types earray
S32 character_ClassTypeInTypes(SA_PARAM_NN_VALID Character *pchar, SA_PARAM_NN_VALID CharClassTypes *peTypes);



CharacterClass * characterclass_GetAdjustedClass( char * pchClass, int iTeamSize, const char *pcSubRank, CritterDef* pCritterDef );

CharClassTypes GetCharacterClassEnum(SA_PARAM_OP_VALID Entity *pent);

void characterclasses_SendClassList(Entity *pEnt);

bool entity_PlayerCanBecomeClass(SA_PARAM_NN_VALID Entity *pEnt, SA_PARAM_NN_VALID CharacterClass *pClass);

CharacterClass* ActivatePowerClassFromStrength(const char *pcStrength);

// Populates the given array with the matching CharacterPath structures for the given power tree
void CharacterPath_GetCharacterPaths(SA_PARAM_NN_VALID CharacterPath ***peaCharacterPaths, SA_PARAM_OP_STR const char *pchPowerTreeFilter, bool bIncludeDevRestricted, bool bPrimaryPathsOnly );

//Populates the given array with character paths that the entity in question can use.
void Entity_GetCharacterPaths(Entity *pEnt, CharacterPath ***peaCharacterPaths, const char *pchPowerTreeFilter);

//Returns whether the entity can use the specific character path
bool Entity_EvalCharacterPathRequiresExpr(SA_PARAM_NN_VALID Entity *pEnt, SA_PARAM_NN_VALID CharacterPath *pPath);

bool character_trh_CanPickSecondaryPath(ATH_ARG NOCONST(Character)* pChar, CharacterPath* pPathToAdd, bool bObeyRequiredLevel);
#define character_CanPickSecondaryPath(pChar, pPath, bObeyLevel) character_trh_CanPickSecondaryPath(CONTAINER_NOCONST(Character, pChar), pPath, bObeyLevel)
// Returns the next suggested node from the given cost table
PTNodeDef * CharacterPath_GetNextSuggestedNodeFromCostTable(int iPartitionIdx, SA_PARAM_NN_VALID Entity *pEnt, SA_PARAM_NN_STR const char *pchCostTable, bool bIgnoreCanBuy);

// Returns the next choice the entity has to make on this character path
CharacterPathChoice * CharacterPath_GetNextChoiceFromCostTable(Entity *pEnt, CharacterPath* pPath, const char *pchCostTable, bool bIgnoreCanBuy);

// Returns if the given node is the next suggested node from the given cost table
bool CharacterPath_IsNextSuggestedNodeFromCostTable(int iPartitionIdx, SA_PARAM_NN_VALID Entity *pEnt, SA_PARAM_NN_STR const char *pchCostTable, SA_PARAM_NN_VALID PTNodeDef *pNodeDef);

// Returns if the given node is the a suggested node from the given cost table in case of character creation
// Certain assumptions such as the player is level 1 and all level 1 powers have single ranks for this function
bool CharacterPath_IsSuggestedNodeFromCostTableInCharacterCreation(SA_PARAM_NN_VALID Entity *pEnt, SA_PARAM_NN_STR const char *pchCostTable, SA_PARAM_NN_VALID PTNodeDef *pNodeDef);

// Returns the names of the suggested nodes from the given cost table in case of character creation
// Certain assumptions such as the player is level 1 and all level 1 powers have single ranks for this function
void CharacterPath_GetSuggestNodeNamesFromCostTableInCharacterCreation(SA_PARAM_NN_VALID Entity *pEnt, SA_PARAM_NN_STR const char *pchCharacterPathName, SA_PARAM_NN_STR const char *pchCostTable, SA_PARAM_NN_VALID char **pestrNodeNames);

// Get the node the character chose for this path choice.  Returns null if this choice hasn't been made yet.
PTNode *CharacterPath_GetChosenNode(SA_PARAM_NN_VALID Entity *pEnt, SA_PARAM_NN_VALID CharacterPathChoice *pChoice);

// Ent will be most likely be NULL on the app server and at login. This is ok as the app server will use the login link and the client will use possible character choices to get
// the game account data
// These are the non-expression versions that can take a NULL ent. The character path must have the field pcGamePermissionValue set
// missing character path means this is a free-form character and will always return true. Use of this type is gated through other expressions
bool CharacterPath_trh_FromNameCanUse(ATH_ARG NOCONST(GameAccountData) *pData, const char *pcCharPathName);

// This checks the key
bool CharacterPath_trh_CanUseEx(ATH_ARG NOCONST(GameAccountData) *pData, CharacterPath *pCharPath, const char *pKey);

bool CharacterPath_trh_FromNameHasKey(ATH_ARG NOCONST(GameAccountData) *pData, const char *pcCharPathName, const char *pKey);

// This checks owned and free
bool CharacterPath_trh_CanUse(ATH_ARG NOCONST(GameAccountData) *pData, CharacterPath *pCharPath);

// returns true if the path is free but not owned
bool CharacterPath_FreeNotOwned(GameAccountData *pData, const CharacterPath *pPath);

CharacterPath* entity_trh_GetPrimaryCharacterPath(NOCONST(Entity) *pEnt);
#define entity_GetPrimaryCharacterPath(pEnt) entity_trh_GetPrimaryCharacterPath(CONTAINER_NOCONST(Entity, pEnt))
void entity_trh_GetChosenCharacterPaths(SA_PARAM_NN_VALID NOCONST(Entity) *pEnt, CharacterPath*** peaPathsOut);
#define entity_GetChosenCharacterPaths(pEnt, peaPathsOut) entity_trh_GetChosenCharacterPaths(CONTAINER_NOCONST(Entity, pEnt), peaPathsOut)

void entity_trh_GetChosenCharacterPathsOfType(NOCONST(Entity) *pEnt, CharacterPath*** peaPathsOut, CharacterPathType eType);
#define entity_GetChosenCharacterPathsOfType(pEnt, peaPathsOut, pchType) entity_trh_GetChosenCharacterPathsOfType(CONTAINER_NOCONST(Entity, pEnt), peaPathsOut, pchType)

bool entity_trh_HasCharacterPath(SA_PARAM_NN_VALID NOCONST(Entity) *pEnt, const char* pchPath);
#define entity_HasCharacterPath(pEnt, pchPath) entity_trh_HasCharacterPath(CONTAINER_NOCONST(Entity, pEnt), pchPath)

bool entity_trh_HasAnyCharacterPath(SA_PARAM_NN_VALID NOCONST(Entity) *pEnt);
#define entity_HasAnyCharacterPath(pEnt) entity_trh_HasAnyCharacterPath(CONTAINER_NOCONST(Entity, pEnt))

CharClassCategory CharClassCategory_getCategoryFromEntity(Entity *pEnt);
CharClassCategory CharClassCategory_getCategoryFromPuppetEntity(int iPartitionIndex, PuppetEntity *pPuppet);

CharClassCategorySet *CharClassCategorySet_getCategorySet(CharClassCategory eCategory, CharClassTypes eType);
CharClassCategorySet *CharClassCategorySet_getCategorySetFromClass(CharacterClass *pClass);
CharClassCategorySet *CharClassCategorySet_getCategorySetFromEntity(Entity *pEnt, CharClassTypes eType);
CharClassCategorySet *CharClassCategorySet_getCategorySetFromPuppetEntity(int iPartitionIndex, PuppetEntity *pPuppet);

CharClassCategorySet *CharClassCategorySet_getPreferredSetForRegion(Entity* pEnt, RegionRules *pRegionRules);
CharClassCategorySet *CharClassCategorySet_getPreferredSet(Entity* pEnt);

bool CharClassCategorySet_checkIfPass(CharClassCategorySet *pSet, CharClassCategory eCategory, CharClassTypes eType);
bool CharClassCategorySet_checkIfPassClass(CharClassCategorySet *pSet, CharacterClass *pClass);
bool CharClassCategorySet_checkIfPassEntity(CharClassCategorySet *pSet, Entity* pEnt);
bool CharClassCategorySet_checkIfPassPuppetEntity(CharClassCategorySet *pSet, int iPartitionIdx, PuppetEntity* pPuppet);

#ifndef AILIB

#include "AutoGen/CharacterClass_h_ast.h"

#endif

#endif
