/***************************************************************************
*     Copyright (c) 2006-2011, Cryptic Studios
*     All Rights Reserved
*     Confidential Property of Cryptic Studios
***************************************************************************/
#include "ActivityCommon.h"



AUTO_STRUCT;
typedef struct ActiveActivity
{
	const char* pchActivityName;		AST(KEY POOL_STRING)
	ActivityDef *pActivityDef;			AST(UNOWNED)

	U32 uTimeStart;
	U32 uTimeEnd;		 // 0xffffffff if it will not end
}ActiveActivity;

AUTO_STRUCT;
typedef struct ActiveActivites
{
	ActiveActivity **ppActivities;
}ActiveActivites;

AUTO_STRUCT;
typedef struct PlayerEventSubscriptionList
{
	// The event name
	const char *pchEventName;		AST(KEY POOL_STRING)

	// The list of player entity references
	EntityRef *perSubscribedPlayers;
} PlayerEventSubscriptionList;

extern ActiveActivites g_Activities;

U32 gslActivity_GetEventClockSecondsSince2000(void);

bool gslActivity_IsActive(const char *pchActivity);
U32 gslActivity_TimeActive(const char *pchActivity);
U32 gslActivity_EventClockEndingTime(const char *pchActivity);

// Indicates whether the event is active at the moment
bool gslEvent_IsActive(EventDef *pDef);

// Update active and pending requested events for the player
void gslActivity_UpdateEvents(Entity *pEntity);

// Adds all player event subscriptions to the server list
void gslEvent_AddAllPlayerSubscriptions(SA_PARAM_NN_VALID Entity *pEntity);

// Removes all player event subscriptions from the server list
void gslEvent_RemoveAllPlayerSubscriptions(SA_PARAM_NN_VALID Entity *pEntity);

// The event system tick function
void gslEvent_Tick(F32 fTimeStep);
