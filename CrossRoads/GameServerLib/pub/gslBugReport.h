#ifndef __GSLBUGREPORT_H__
#define __GSLBUGREPORT_H__

typedef struct Entity Entity;
typedef struct Packet Packet;
typedef struct AIDebug AIDebug;
typedef bool (*CategoryCustomDataFunc) (void **ppStruct, ParseTable** ppti, Entity *ent, const char *pDataString, char **estrLabel);

bool gslBug_AddCustomDataCallback (const char *category, CategoryCustomDataFunc callback, bool bIsDelayed);
bool gslBug_RemoveCustomDataCallback (const char *category, CategoryCustomDataFunc callback);

void gslHandleBugOrTicket(Packet *pak, Entity *ent);

typedef struct MissionTrackingStruct MissionTrackingStruct;
typedef struct MissionEventContainer MissionEventContainer;
typedef struct GameEvent GameEvent;
typedef enum MissionState MissionState;

AUTO_STRUCT AST_IGNORE_STRUCT(eventLog);
typedef struct MissionTrackingStruct
{ 
	char *refKeyString;
	MissionState eState;
	U32 startTime;
	U32 endTime;
	U32 timesCompleted;

	MissionEventContainer **eaEventCounts;
	GameEvent** trackedEvents;

	MissionTrackingStruct **children;
} MissionTrackingStruct;

AUTO_STRUCT;
typedef struct PowersBugStruct
{ 
	char *pchName;
} PowersBugStruct;

AUTO_STRUCT;
typedef struct AIBugTargetStruct
{
	AIDebug *debugInfo;

	S64 requestTime;
} AIBugTargetStruct;

typedef struct TicketEntityData TicketEntityData;
TicketEntityData * FindTicketEntityTarget(U32 uID);
TicketEntityData * FindTicketEntityTargetDone(U32 uID);

U32 gslTicket_EntityTarget(U32 entRef);

void SubmitTicketFromServerCode(Entity *pEnt, char *pMainCategory, char *pCategory, char *pSummary, char *pDescription, char *pTicketLabel);

#endif
