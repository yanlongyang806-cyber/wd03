#include "gclMapNotifications.h"
#include "Entity.h"
#include "EntityLib.h"
#include "MapNotificationsCommon.h"
#include "gclEntity.h"
#include "Team.h"
#include "AutoGen/MapNotificationsCommon_h_ast.h"
#include "AutoGen/GameServerLib_autogen_ServerCmdWrappers.h"

AUTO_RUN_ANON(memBudgetAddMapping(__FILE__, BUDGET_GameSystems););

// The list of entity map notifications
EntityMapNotifications ** s_ppEntityMapNotifications = NULL;

AUTO_RUN;
void gclMapNotifications_Register(void)
{
	// Enable indexing for the entity map notifications
	eaIndexedEnable(&s_ppEntityMapNotifications, parse_EntityMapNotifications);
}

// Add a "saved" waypoint.
AUTO_EXPR_FUNC(UIGen) ACMD_NAME(GenMapPushEntityNotification);
void gclMapNotifications_exprGenMapPushEntityNotification(S32 eNotificationType)
{
	Entity *pEnt = entActivePlayerPtr();

	if (pEnt && team_IsMember(pEnt))
	{
		ServerCmd_PushEntityMapNotification(eNotificationType);
	}
}

AUTO_COMMAND ACMD_ACCESSLEVEL(0) ACMD_CLIENTCMD ACMD_PRIVATE ACMD_NAME(ReceiveEntityMapNotification);
void gclMapNotifications_cmdReceiveEntityMapNotification(S32 eEntType, U32 uEntID, S32 eNotificationType)
{
	const MapNotificationDef *pNotificationDef = mapNotification_DefFromType((MapNotificationType)eNotificationType);
	Entity *pEnt = entFromContainerIDAnyPartition((GlobalType)eEntType, uEntID);
	if (pEnt && pNotificationDef)
	{
		EntityMapNotifications *pEntNotifications;
		MapNotificationEntry *pEntry = NULL;
		EntityRef erEnt;
		S32 iIndexFound;
		S32 iNotificationIndexFound;

		ANALYSIS_ASSUME(pEnt);
		erEnt = entGetRef(pEnt);
		iIndexFound = eaIndexedFindUsingInt(&s_ppEntityMapNotifications, erEnt);

		if (iIndexFound >= 0)
		{
			pEntNotifications = s_ppEntityMapNotifications[iIndexFound];
		}
		else
		{
			pEntNotifications = StructCreate(parse_EntityMapNotifications);
			pEntNotifications->erEntity = erEnt;
			eaIndexedEnable(&pEntNotifications->ppNotifications, parse_MapNotificationEntry);
			eaIndexedAdd(&s_ppEntityMapNotifications, pEntNotifications);
		}

		iNotificationIndexFound = eaIndexedFindUsingString(&pEntNotifications->ppNotifications, pNotificationDef->pchName);

		if (iNotificationIndexFound >= 0)
		{
			pEntry = pEntNotifications->ppNotifications[iNotificationIndexFound];
		}
		else
		{
			pEntry = StructCreate(parse_MapNotificationEntry);
			pEntry->pchNotificationType = pNotificationDef->pchName;
			eaIndexedAdd(&pEntNotifications->ppNotifications, pEntry);
		}

		// Set the timestamp
		pEntry->iTimestamp = timeMsecsSince2000();
	}
}

// Indicates whether the entity 
bool gclMapNotifications_EntityHasNotification(EntityRef erRef, MapNotificationType eType)
{
	const char *pchNotificationType = StaticDefineIntRevLookup(MapNotificationTypeEnum, eType);
	S32 iEntIndexFound = eaIndexedFindUsingInt(&s_ppEntityMapNotifications, erRef);

	if (iEntIndexFound >= 0)
	{
		return eaIndexedFindUsingString(&s_ppEntityMapNotifications[iEntIndexFound]->ppNotifications, pchNotificationType) >= 0;
	}
	return false;
}

// Tick function for the map notifications
void gclMapNotifications_Tick(void)
{
	S32 i, j;
	S64 iNow = timeMsecsSince2000();

	for (i = eaSize(&s_ppEntityMapNotifications) - 1; i >= 0; i--)
	{
		EntityMapNotifications *pNotifications = s_ppEntityMapNotifications[i];
		Entity *pEnt = entFromEntityRefAnyPartition(pNotifications->erEntity);

		if (pEnt == NULL)
		{
			// Entity no longer exists, delete notifications.
			StructDestroy(parse_EntityMapNotifications, pNotifications);
			eaRemove(&s_ppEntityMapNotifications, i);
			continue;
		}

		for (j = eaSize(&pNotifications->ppNotifications) - 1; j >= 0; j--)
		{
			MapNotificationEntry *pEntry = pNotifications->ppNotifications[j];
			const MapNotificationDef *pDef = mapNotification_DefFromName(pEntry->pchNotificationType);

			devassert(pDef);

			if (pDef == NULL)
			{
				StructDestroy(parse_MapNotificationEntry, pEntry);
				eaRemove(&pNotifications->ppNotifications, j);
				continue;
			}

			if (iNow - pEntry->iTimestamp > pDef->fLifespan * 1000.f)
			{
				// Notification expired
				StructDestroy(parse_MapNotificationEntry, pEntry);
				eaRemove(&pNotifications->ppNotifications, j);
				continue;
			}
		}
	}
}