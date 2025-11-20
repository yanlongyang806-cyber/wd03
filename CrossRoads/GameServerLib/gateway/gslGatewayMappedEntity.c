/***************************************************************************
 *     Copyright (c) 2012-, Cryptic Studios
 *     All Rights Reserved
 *     Confidential Property of Cryptic Studios
 *
 * Functions for mapping containers. There are used by the GatewayServer
 * to map regular game entities into something more directly useful by a
 * web client. Typically this means executing whatever "game logic" is
 * necessary to get something appropriate for display.
 * 
 * For example, a character's Star Fleet rank is a mix of data on the entity,
 * a data table (unavailable as a resource), and some logic which scans the
 * table. The mapper will figure all of that out and provide the result to
 * client.
 *
 ***************************************************************************/
#include "stdtypes.h"

#include "Entity.h"
#include "EntitySavedData.h"
#include "gslEntity.h"
#include "Character.h"
#include "Character_tick.h"
#include "Player.h"

#include "gslGatewayMappedEntity.h"
#include "gslGatewaySession.h"



/////////////////////////////////////////////////////////////////////////////
//
// Some helpers for mapping containers.
//


//
// cmap_CreateOfflineEntity
//
// Gets a copy of the entity from the tracker which has had an update tick
//   run on it so all it's attributes and statistics are up to date and
//   available.
//
Entity *cmap_CreateOfflineEntity(ContainerTracker *ptracker)
{
	Entity *pOfflineCopy = NULL;

	if(GlobalTypeParent(ptracker->pMapping->globaltype) == GLOBALTYPE_ENTITY)
	{
		Entity *pRefEnt = GET_REF(ptracker->hEntity);

		pOfflineCopy = StructCreateWithComment(parse_Entity, "Offline copy ent from cmap_CreateOfflineEntity");

		// Copy all persisted fields that aren't marked as TOK_PUPPET_NO_COPY
		StructCopy(parse_Entity, pRefEnt, pOfflineCopy, STRUCTCOPYFLAG_DONT_COPY_NO_ASTS, TOK_PERSIST, 0);

		if(pOfflineCopy->pChar)
		{
			pOfflineCopy->pChar->iLevelCombat = entity_GetSavedExpLevel(pOfflineCopy);

			pOfflineCopy->pChar->pEntParent = pOfflineCopy;
			pOfflineCopy->iPartitionIdx_UseAccessor = 1;

			character_TickOffline(1, pOfflineCopy->pChar, NULL);
		}

		if(SAFE_MEMBER2(pOfflineCopy,pPlayer,pPlayerAccountData) && SAFE_MEMBER2(pRefEnt,pPlayer,pPlayerAccountData))
			COPY_HANDLE(pOfflineCopy->pPlayer->pPlayerAccountData->hTempData, pRefEnt->pPlayer->pPlayerAccountData->hTempData);
	}

	return pOfflineCopy;
}

//
// cmap_DestroyOfflineEntity
//
// Destroys the entity created by cmap_CreateOfflineEntity
//
void cmap_DestroyOfflineEntity(Entity *pOfflineEnt)
{
	gslCleanupEntityEx(1, pOfflineEnt, false, false);
	StructDestroy(parse_Entity, pOfflineEnt);
}

/////////////////////////////////////////////////////////////////////////////

// End of File
