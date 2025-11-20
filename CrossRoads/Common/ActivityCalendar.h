/***************************************************************************
*     Copyright (c) 2006-2011, Cryptic Studios
*     All Rights Reserved
*     Confidential Property of Cryptic Studios
***************************************************************************/

#ifndef ActivityCalendar_H
#define ActivityCalendar_H

#include "Message.h" //For DisplayMessage

typedef struct QueueDef QueueDef;

AUTO_ENUM AEN_EXTEND_WITH_DYNLIST(g_pTagCategories);
typedef enum ActivityDisplayTags
{
	kActivityDisplayTags_None, ENAMES(None)
	//Data defined....
}ActivityDisplayTags;

AUTO_STRUCT;
typedef struct ActivityDisplayTagData
{
	const char** eaData; AST(NAME(Tag))
} ActivityDisplayTagData;

AUTO_STRUCT;
typedef struct CalendarTiming
{
	U32 uStartDate;
	U32 uEndDate;
	U32 bDirty : 1; NO_AST
} CalendarTiming;

AUTO_STRUCT;
typedef struct CalendarEvent
{
	const char* pchEventName;			AST(KEY POOL_STRING)

	CalendarTiming** eaTiming;

	DisplayMessage msgDisplayName;		AST(STRUCT(parse_DisplayMessage))
	DisplayMessage msgDisplayDescShort;	AST(STRUCT(parse_DisplayMessage))
	DisplayMessage msgDisplayDescLong;	AST(STRUCT(parse_DisplayMessage))

	char* pchDisplayName;
	char* pchDisplayDescShort;
	char* pchDisplayDescLong;

	const char *pchIcon;				AST(POOL_STRING)
	const char *pchBackground;			AST(POOL_STRING)

	const char *pchQueue;				AST(NAME(Queue) POOL_STRING)

	const char *pchParent;				AST(POOL_STRING)

	U32 *uDisplayTags;					AST(NAME(DisplayTag) SUBTABLE(ActivityDisplayTagsEnum))
	U32 bEventActiveOnServer : 1;		AST(NAME(EventActiveOnServer))
	U32 bEventMapMove : 1;				AST(NAME(EventMapMove))
	U32 bEventContact : 1;				AST(NAME(EventContact))
	U32 bEventAffectsCurrentMap : 1;	AST(NAME(EventAffectsCurrentMap))
	U32 bDirty : 1;						NO_AST
} CalendarEvent;

AUTO_STRUCT;
typedef struct CalendarRequest
{
	CalendarEvent** eaEvents;
} CalendarRequest;

AUTO_STRUCT;
typedef struct PendingCalendarRequest
{
	U32 uStartDate;
	U32 uEndDate;
	U32* piTagsInclude;
	U32* piTagsExclude;
} PendingCalendarRequest;

bool ActivityCalendarFilterByTag(U32* piTags, U32* piTagsInclude, U32* piTagsExclude);

#endif