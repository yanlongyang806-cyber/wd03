/***************************************************************************
*     Copyright (c) 2009, Cryptic Studios
*     All Rights Reserved
*     Confidential Property of Cryptic Studios
***************************************************************************/

#pragma once
GCC_SYSTEM

#include "Message.h"

typedef struct DefineContext DefineContext;
typedef struct GameEvent GameEvent;
typedef struct PlayerStatListener PlayerStatListener;
typedef struct PlayerStatsInfo PlayerStatsInfo;

//
// START PlayerStat Category
//
AUTO_STRUCT;
typedef struct PlayerStatCategoryDef
{
	// The name of the category
	char *pchName;						AST(KEY STRUCTPARAM POOL_STRING)
	
	const char *pchIconName;			AST( NAME("IconName") POOL_STRING )

	// The file name. Used for reloading
	const char *pchFileName;			AST(NAME(File), CURRENTFILE)
} PlayerStatCategoryDef;

// Array of the PlayerStatTags, loaded and indexed directly
AUTO_STRUCT;
typedef struct PlayerStatCategories
{
	PlayerStatCategoryDef **eaPlayerStatCategories;	AST(NAME(Category))
} PlayerStatCategories;

// 
extern PlayerStatCategories g_PlayerStatCategories;

typedef enum PlayerStatCategory
{
	kPlayerStatCategory_None,
	kPlayerStatCategory_FIRST_DATA_DEFINED
} PlayerStatCategory;

extern StaticDefineInt PlayerStatCategoryEnum[];
//
// END PlayerStat Category
//

//
// PlayerStat tags
//
extern DefineContext *s_pDefinePlayerStatTags;
AUTO_ENUM AEN_EXTEND_WITH_DYNLIST(s_pDefinePlayerStatTags);
typedef enum PlayerStatTag
{
	// Defined in data/defs/PlayerStatTags.def
} PlayerStatTag;
extern StaticDefineInt PlayerStatTagEnum[];
// 




AUTO_ENUM;
typedef enum PlayerStatUpdateType {
	// Adds events cumulatively
	PlayerStatUpdateType_Sum,

	// Takes the max value of a single event (only interesting for Damage events and such)
	PlayerStatUpdateType_Max,

} PlayerStatUpdateType;

extern StaticDefineInt PlayerStatUpdateTypeEnum[];

// Used to hold a copy of the events on the playerStatDef for the editor.  Without this, the editor won't display properly
AUTO_STRUCT;
typedef struct PlayerStatEventsEditorData
{
	GameEvent *pEvent;					AST( SERVER_ONLY )
} PlayerStatEventsEditorData;
extern ParseTable parse_PlayerStatEventsEditorData[];
#define TYPE_parse_PlayerStatEventsEditorData PlayerStatEventsEditorData

// This is a dictionary object that defines a PlayerStat
AUTO_STRUCT;
typedef struct PlayerStatDef
{
	const char *pchName;						AST( STRUCTPARAM KEY POOL_STRING )
	const char *pchScope;						AST( SERVER_ONLY POOL_STRING )
	const char *pchFilename;					AST( CURRENTFILE )
	char *pchNotes;								AST( SERVER_ONLY )

	// for sorting and showing relevant player stats
	S32 iRank;
	const char *pchIconName;					AST( NAME("IconName") POOL_STRING )

	// Display fields
	PlayerStatCategory eCategory;				AST( NAME("Category"), SUBTABLE(PlayerStatCategoryEnum))
	DisplayMessage displayNameMsg;				AST( STRUCT(parse_DisplayMessage) )  
	DisplayMessage descriptionMsg;				AST( STRUCT(parse_DisplayMessage) )  


	// Functional fields
	GameEvent **eaEvents;						AST( NAME("Event", "EventData") SERVER_ONLY )
	PlayerStatUpdateType eUpdateType;			AST( NAME("UpdateType") SERVER_ONLY )

	// Crazy field to get the editor to display events properly (DONT TOUCH!)
	PlayerStatEventsEditorData **eaEditorData;	AST(NAME(EditorData) NO_TEXT_SAVE)

	// The mission tags assigned to this mission
	S32 *piTags;								AST(NAME(PlayerStatTag), SUBTABLE(PlayerStatTagEnum))

	// if true, this stat should be tracked per-match 
	bool bPlayerPerMatchStat;

	// if true, sends a notification to the player that the stat changed
	bool bNotifyPlayerOnChange;
} PlayerStatDef;

// This is stored on a player, and tracks the value of a particular stat
AUTO_STRUCT AST_CONTAINER;
typedef struct PlayerStat
{
	const char *pchStatName;			AST( PERSIST NO_TRANSACT STRUCTPARAM KEY POOL_STRING )
	U32 uValue;							AST( PERSIST NO_TRANSACT )

	// Things listening to this stat (only on the server)
	PlayerStatListener** eaListeners;	AST( NO_WRITE NO_NETSEND LATEBIND)
} PlayerStat;

extern ParseTable parse_PlayerStat[];
#define TYPE_parse_PlayerStat PlayerStat

// Container for all PlayerStat-related info
AUTO_STRUCT AST_CONTAINER;
typedef struct PlayerStatsInfo
{
	DirtyBit dirtyBit;							AST( NO_NETSEND )

	// Stats that are being tracked for this player
	PlayerStat** eaPlayerStats;					AST( PERSIST NO_TRANSACT SELF_ONLY)

	// These are placeholders for missions to listen to - they'll be removed the first time a stat is updated
	PlayerStat** eaPlayerStatPlaceholders;		AST( SERVER_ONLY )

} PlayerStatsInfo;

extern ParseTable parse_PlayerStatsInfo[];
#define TYPE_parse_PlayerStatsInfo PlayerStatsInfo



bool playerstatdef_Validate(PlayerStatDef *pStatDef);

// ----------------------------------------------------------------------------
//  Accessors
// ----------------------------------------------------------------------------

U32 playerstat_GetValue(PlayerStatsInfo *pStatsInfo, const char *pchStatName);



extern DictionaryHandle g_PlayerStatDictionary;
