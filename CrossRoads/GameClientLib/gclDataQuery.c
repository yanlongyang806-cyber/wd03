/***************************************************************************
*     Copyright (c) 2005-2006, Cryptic Studios
*     All Rights Reserved
*     Confidential Property of Cryptic Studios
***************************************************************************/

#include "gclEntity.h"
#include "gclDataQuery.h"
#include "ScratchStack.h"

#include "WorldGrid.h"
#include "objPath.h"
#include "MapDescription.h"
#include "EntityIterator.h"

extern ParseTable parse_SavedMapDescription[];
#define TYPE_parse_SavedMapDescription SavedMapDescription

// This file is for implementing general data querying commands and functions,
// for the TestClient or other modules on the client

// Returns a ContainerRef containing the player's information. Useful for testclient
AUTO_COMMAND ACMD_CATEGORY(Test);
ContainerRef *GetPlayerContainer(void)
{
	ContainerRef *conRef;
	Entity *pPlayer = entActivePlayerPtr();

	if (!pPlayer)
	{
		return 0;
	}
	conRef = CreateContainerRef(entGetType(pPlayer),entGetContainerID(pPlayer));
	return conRef;
}

// Returns a ContainerRefArray for all entities near the player. Useful for test client
AUTO_COMMAND ACMD_CATEGORY(Test);
ContainerRefArray *GetNearbyEntities(void)
{
	ContainerRefArray *refArray = CreateContainerRefArray();
	Entity *playerEnt = entActivePlayerPtr();
	Entity** buf = ScratchAlloc(MAX_ENTITIES_PRIVATE * sizeof(Entity *));
	Entity* foundEnt;
	EntityIterator * iter;

	if (!playerEnt)
	{
		return refArray;
	}

	iter = entGetIteratorAllTypesAllPartitions(0,0);

	while ((foundEnt = EntityIteratorGetNext(iter)))
	{
		if (!entIsVisible(foundEnt))
		{
			continue;
		}

		AddToContainerRefArray(refArray,entGetType(foundEnt),entGetContainerID(foundEnt));
	}

	ScratchFree(buf);
	EntityIteratorRelease(iter);
	return refArray;
}

// This is an example command that goes through the different type of query commands,
// and how to use them
AUTO_COMMAND ACMD_CATEGORY(Test);
void TestClientQueries(void)
{
	ContainerRef *conRef;
	char queryString[1000];
	char *resultString;
	ContainerRefArray *refArray;

	conRef = GetPlayerContainer();
	// This gives you a container Ref, which has the type and ID of the player
	
	sprintf(queryString,"%s[%d].pChar.pattrBasic.fHitPoints",GlobalTypeToName(conRef->containerType),conRef->containerID);
	// GlobalTypeToName converts a type int to the string version, which object path needs

	resultString = strdup(ReadObjectPath(queryString));
	// Calling the test client command will give you a string you must free
	// In this case, because we directly asked for a field, we'll get it (the float) back in string form

	if (resultString)
	{	
		printf("Current HP = %s\n",resultString);
		SAFE_FREE(resultString);
	}
	else
	{
		printf("Error in query!\n");
	}

	sprintf(queryString,"%s[%d].pPlayer.currentMap",GlobalTypeToName(conRef->containerType),conRef->containerID);
	resultString = strdup(ReadObjectPath(queryString));
	// Because we asked for an entire structure, we'll get it back in string form, which must be converted to a structure

	if (resultString)
	{
		SavedMapDescription *mapDescription = StructCreateFromString(parse_SavedMapDescription,resultString);
		if (mapDescription)
		{
			printf("Current map = %s\n",mapDescription->mapDescription);
		}
		else
		{
			printf("Error in query!\n");
		}
		SAFE_FREE(resultString);
	}
	else
	{
		printf("Error in query!\n");
	}
	DestroyContainerRef(conRef);

	refArray = GetNearbyEntities();

	printf("Nearby Entities = %d\n",eaSize(&refArray->containerRefs));
	DestroyContainerRefArray(refArray);
}