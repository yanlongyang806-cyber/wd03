#pragma once

typedef struct DefineContext DefineContext;

#define MAP_NOTIFICATION_MAX_LIFESPAN 30.f

extern DefineContext *g_pDefineMapNotificationTypes;
AUTO_ENUM AEN_EXTEND_WITH_DYNLIST(g_pDefineMapNotificationTypes);
typedef enum MapNotificationType
{
	MapNotificationType_None = 0, ENAMES(None)
	// Data-defined ...
} MapNotificationType;

AUTO_STRUCT;
typedef struct MapNotificationDef
{
	// The name of the map notification type
	const char *pchName;				AST(KEY STRUCTPARAM POOL_STRING)

	// The numeric value for the map notification type
	MapNotificationType eType;			NO_AST

	// Indicates how long this notification will stay alive. 
	// Defined in seconds. Must be a positive value
	F32 fLifespan;						AST(DEFAULT(3))
} MapNotificationDef;
extern ParseTable parse_MapNotificationDef[];
#define TYPE_parse_MapNotificationDef MapNotificationDef

AUTO_STRUCT;
typedef struct MapNotificationDefs
{
	MapNotificationDef **ppNotifications;	AST(NAME(MapNotificationDef))
} MapNotificationDefs;
extern ParseTable parse_MapNotificationDefs[];
#define TYPE_parse_MapNotificationDefs MapNotificationDefs

AUTO_STRUCT;
typedef struct MapNotificationEntry
{
	// Map notification type
	const char *pchNotificationType;	AST(POOL_STRING KEY)

	// The time notification is received
	S64 iTimestamp;
} MapNotificationEntry;
extern ParseTable parse_MapNotificationEntry[];
#define TYPE_parse_MapNotificationEntry MapNotificationEntry

AUTO_STRUCT;
typedef struct EntityMapNotifications
{
	// Reference to the entity
	EntityRef erEntity;			AST(KEY)

	// Notifications received by the entity
	MapNotificationEntry **ppNotifications;
} EntityMapNotifications;
extern ParseTable parse_EntityMapNotifications[];
#define TYPE_parse_EntityMapNotifications EntityMapNotifications

// Returns the MapNotificationDef with the given notification type
const MapNotificationDef * mapNotification_DefFromName(SA_PARAM_OP_STR const char *pchNotificationType);

// Returns the MapNotificationDef with the given notification type
const MapNotificationDef * mapNotification_DefFromType(MapNotificationType eType);