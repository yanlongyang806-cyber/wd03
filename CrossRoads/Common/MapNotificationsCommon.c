#include "MapNotificationsCommon.h"

#include "error.h"
#include "textparser.h"

#ifdef GAMECLIENT
#include "UIGen.h"
#endif

#include "MapNotificationsCommon_h_ast.h"

AUTO_RUN_ANON(memBudgetAddMapping(__FILE__, BUDGET_GameSystems););

// Context to track all data-defined map notification types
DefineContext *g_pDefineMapNotificationTypes = NULL;

// Global data associated with each MissionUIType
MapNotificationDefs g_MapNotificationDefs = {0};

static void mapNotification_FixupAndValidate(SA_PARAM_NN_VALID MapNotificationDef *pDef)
{
	// Validate the lifetime of the notification
	if (pDef->fLifespan <= 0.f)
	{
		Errorf("Map notification '%s' has an invalid life span value. Lifespan must be positive. Setting the lifespan to 3 seconds.", 
			pDef->pchName);
		// Set to the default of 3 seconds
		pDef->fLifespan = 3.f;
	}

	if (pDef->fLifespan > MAP_NOTIFICATION_MAX_LIFESPAN)
	{
		Errorf("Map notification '%s' has an invalid life span value. Lifespan cannot be greater than %.0f seconds. Setting the lifespan to %.0f seconds.", 
			pDef->pchName,
			MAP_NOTIFICATION_MAX_LIFESPAN,
			MAP_NOTIFICATION_MAX_LIFESPAN);

		pDef->fLifespan = MAP_NOTIFICATION_MAX_LIFESPAN;
	}
}

AUTO_STARTUP(MapNotificationsLoad);
void MapNotificationsStartup(void)
{
	S32 i, s;

	loadstart_printf("Loading Map Notification Defs...");
	ParserLoadFiles(NULL, "defs/config/MapNotifications.def", "MapNotifications.bin", PARSER_OPTIONALFLAG, parse_MapNotificationDefs, &g_MapNotificationDefs);
	g_pDefineMapNotificationTypes = DefineCreate();
	s = eaSize(&g_MapNotificationDefs.ppNotifications);

	for (i = 0; i < s; i++) 
	{
		MapNotificationDef *pDef = g_MapNotificationDefs.ppNotifications[i];
		pDef->eType = i + 1;

		mapNotification_FixupAndValidate(pDef);

		DefineAddInt(g_pDefineMapNotificationTypes, pDef->pchName, pDef->eType);
	}

#ifdef GAMECLIENT
	ui_GenInitStaticDefineVars(MapNotificationTypeEnum, "MapNotificationType_");
#endif

	loadend_printf(" done (%d Map Notification Types).", s);
}

// Returns the MapNotificationDef with the given notification type
const MapNotificationDef * mapNotification_DefFromName(SA_PARAM_OP_STR const char *pchNotificationType)
{
	S32 iIndexFound = eaIndexedFindUsingString(&g_MapNotificationDefs.ppNotifications, pchNotificationType);

	if (iIndexFound >= 0)
	{
		return g_MapNotificationDefs.ppNotifications[iIndexFound];
	}

	return NULL;
}

// Returns the MapNotificationDef with the given notification type
const MapNotificationDef * mapNotification_DefFromType(MapNotificationType eType)
{
	return mapNotification_DefFromName(StaticDefineIntRevLookup(MapNotificationTypeEnum, eType));
}

#include "MapNotificationsCommon_h_ast.c"