/***************************************************************************
*     Copyright (c) 2005-2011, Cryptic Studios
*     All Rights Reserved
*     Confidential Property of Cryptic Studios
***************************************************************************/

#pragma once

#include "referencesystem.h"
#include "GlobalTypeEnum.h"
#include "queue_common_structs.h"

#include "AutoGen/queue_common_structs_h_ast.h"

typedef struct QueueDef QueueDef;

#define GAME_CONTENT_NODE_BASE_DIR "defs/suggestedcontent"

///////////////////////////////////////////////////////////////////////////////////////////////////////////////
///////////////////////////////////////////////////////////////////////////////////////////////////////////////

AUTO_ENUM;
typedef enum SuggestedContentType
{
	SuggestedContentType_None = 0, // Invalid type
	SuggestedContentType_Event,
	SuggestedContentType_Queue,
	SuggestedContentType_StoryNode,
} SuggestedContentType;


AUTO_STRUCT;
typedef struct SuggestedContentNode
{
	// The type of the content
	SuggestedContentType eType;

	// The unique name of an event. 
	const char *pchContentEventName;				AST(POOL_STRING NAME("EventName"))

	// .story story node name to search through
	const char *pchContentStoryName;				AST(POOL_STRING NAME("StoryName"))

	// Reference to a queue def
	REF_TO(QueueDef) hContentQueue;					AST(NAME("Queue") REFDICT(QueueDef))

} SuggestedContentNode;

extern ParseTable parse_SuggestedContentNode[];
#define TYPE_parse_SuggestedContentNode SuggestedContentNode


AUTO_STRUCT;
typedef struct SuggestedContentForLevel	
{
	// Player level
	S32 iLevel;								AST(KEY STRUCTPARAM)

	// The list of content
	SuggestedContentNode **ppContent;	AST(NAME("ContentInfo"))
	
} SuggestedContentForLevel;

AUTO_STRUCT;
typedef struct SuggestedContentList
{
	// The unique name of this group of content (client/uigen can request different lists)
	const char *pchName;					AST(KEY STRUCTPARAM)

	// Used for error reporting
	const char* pchFilename;				AST(CURRENTFILE)

	// An array of suggested content per level
	SuggestedContentForLevel **ppSuggestedContentForLevels;	AST(NAME("Level"))
	
} SuggestedContentList;


//////////////////////////////////////////////////////////////////////////////////////////////

// This is what gets bundled up on the server and passed down to the client:
AUTO_STRUCT;
typedef struct SuggestedContentInfo
{
	/////////////////////////
	/// Static Data
	// The name for this List Name
	const char* strListName;								AST( NAME( "ListName" ))

	// What the content ended up being
	SuggestedContentType eType;

	// The content data. As much as we have. It's possible to have both a queue and event, for example.
	const char *pchEventName;							AST(POOL_STRING NAME("EventName"))
	const char *pchStoryName;							AST(POOL_STRING NAME("StoryName"))
	REF_TO(QueueDef) hQueue;							AST(NAME("Queue") REFDICT(QueueDef))

	// Display name for the game progression node
	const char* strDisplayName;								AST( NAME( "DisplayName" ) )

	// Summary
	const char* strSummary;									AST( NAME( "Summary" ) )
	
	// The art file name
	const char *pchArtFileName;								AST( POOL_STRING )

	/////////////////////////
	/// Dynamic Data

	// The date content starts
	U32 uStartDate;

	// The date content ends
	U32 uEndDate;

	// Indicates whether the content is active at the moment
	U32 bEventActive : 1;

	// Indicates whether the queue or event is valid for the current player.
	//   Note that queues have events and events have queues so these may be valid for both types.
	//   If there is no queue or no event these will be set to true (or QueueCannotUseReason_None)
	U32 bEventIsValidForPlayer : 1;
	
	QueueCannotUseReason eCannotUseQueueReason;  // Will be QueueCannotUseReason_None if there is no queue or the queue is usable
	const char* CannotUseDisplayMessage;		 // Blank if no queue or if it is usable

} SuggestedContentInfo;


// Dictionary holding the game progression nodes
extern DictionaryHandle g_hSuggestedContentListDictionary;

// Loads the suggested player content
void suggestedContent_LoadSuggestedContent(void);

// Gets the SuggestedContent from the dictionary
SuggestedContentList * suggestedContent_SuggestedContentListFromName(const char *pchName);
