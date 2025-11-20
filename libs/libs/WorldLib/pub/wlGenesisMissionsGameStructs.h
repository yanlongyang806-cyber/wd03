//// A collection of structures that are needed at the Gameplay level
//// and by Genesis.
#pragma once
GCC_SYSTEM


// How the mission level is determined
AUTO_ENUM;
typedef enum MissionLevelType
{
	MissionLevelType_Specified,
	MissionLevelType_PlayerLevel,
	MissionLevelType_MapLevel,
	MissionLevelType_MapVariable,
} MissionLevelType;
extern StaticDefineInt MissionLevelTypeEnum[];

AUTO_ENUM;
typedef enum MissionLevelClampType
{
	MissionLevelClampType_Specified, 
	MissionLevelClampType_MapLevel,
	MissionLevelClampType_MapVariable,
} MissionLevelClampType;
extern StaticDefineInt MissionLevelClampTypeEnum[];

AUTO_STRUCT;
typedef struct MissionLevelClamp
{
	// This is the level range to clamp at
	// Zero on specified min/max means don't clamp this value
	MissionLevelClampType eClampType;					AST( NAME("ClampType") )
	int iClampSpecifiedMin;								AST( NAME("MinLevel") )
	int iClampSpecifiedMax;								AST( NAME("MaxLevel") )
	int iClampOffsetMin;								AST( NAME("ClampOffsetMin") )
	int iClampOffsetMax;								AST( NAME("ClampOffsetMax") )
	char *pcClampMapVariable;							AST( NAME("ClampMapVariable") )
} MissionLevelClamp;
extern ParseTable parse_MissionLevelClamp[];
#define TYPE_parse_MissionLevelClamp MissionLevelClamp

AUTO_STRUCT;
typedef struct MissionLevelDef
{
	// How the mission level is determined
	MissionLevelType eLevelType;			AST( NAME("LevelType", "UsePlayerLevel"))

	// Level of the mission if eLevelType is set to specified.  Offsets from this are used to determine if you can get it
	// Also, used to seed the reward system based on length and relative level of mission to player
	int missionLevel;						AST( NAME("Level", "Level:") DEF(1) )

	// Map variable to determine the level from if eLevelType is set to Map Variable.
	char* pchLevelMapVar;					AST( NAME("MapVariableForLevel"))

	// How the level should be clamped if using the player's level as the mission level.
	MissionLevelClamp* pLevelClamp;			AST( NAME("LevelClamp"))
} MissionLevelDef;
extern ParseTable parse_MissionLevelDef[];
#define TYPE_parse_MissionLevelDef MissionLevelDef

AUTO_ENUM;
typedef enum MissionShareableType
{
	MissionShareableType_Auto,
	MissionShareableType_Never,
} MissionShareableType;
extern StaticDefineInt MissionShareableTypeEnum[];

AUTO_ENUM;
typedef enum ContactMapVarOverrideType
{
	ContactMapVarOverrideType_Specified,
	ContactMapVarOverrideType_MapVar,
} ContactMapVarOverrideType;
extern StaticDefineInt ContactMapVarOverrideTypeEnum[];

AUTO_ENUM;
typedef enum SpecialDialogFlags
{
	SpecialDialogFlags_ForceOnTeam			= (1 << 0), // Special dialog should be forced onto all teammates, and queued behind any existing dialogs
	SpecialDialogFlags_Synchronized			= (1 << 1), // All teammates must complete this dialog before continuing
	SpecialDialogFlags_RandomOptionOrder	= (1 << 2), // Randomize the order that the options are displayed for this dialog
	SpecialDialogFlags_CriticalDialog		= (1 << 3)	// Critical dialogs are seen by all team members and only the player who initiated the dialog control the options selected
} SpecialDialogFlags;
extern StaticDefineInt SpecialDialogFlagsEnum[];

AUTO_ENUM AEN_EXTEND_WITH_DYNLIST(g_ExtraMissionPlayTypes);
typedef enum MissionPlayType
{
	MissionPlay_Untyped,		ENAMES(Untyped)
	
	MissionPlay_End,			EIGNORE
} MissionPlayType;
extern StaticDefineInt MissionPlayTypeEnum[];
