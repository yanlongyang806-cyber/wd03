#include "TicketLocation.h"
#include "TicketTracker.h"
#include "TicketEntry.h"
#include "TicketEntry_h_ast.h"

#include "StashTable.h"
#include "estring.h"
#include "objContainer.h"

static StashTable sStashZoneName = NULL;

// Distances all ignore the Y coordinate (height) and are calculated as XZ-plane splats

#define BUCKET_DIMENSION 500 // 500 ft on each side
#define MAX_DISTANCE_SQUARED 250000 // 500^2
#define MAX_STUCK_DISTANCE_SQUARED 100 // 10^2

void initializeTicketLocation(void)
{
	sStashZoneName = stashTableCreateWithStringKeys(25, StashDeepCopyKeys_NeverRelease);
}

static int roundDown(float x)
{
	if (x >= 0.)
		return (int) x;
	if (x == (float) ((int) x))
		return x;
	return (((int) x) - 1);
}

// Bucket string "0,0" gives { [0,BUCKET_DIMENSION),[0,BUCKET_DIMENSION) }
// "-0,-0" gives { (0,-BUCKET_DIMENSION),(0,-BUCKET_DIMENSION) }
static char * tlocGetStringKey (SA_PARAM_NN_VALID char **estr, const Vec3 position)
{
	// Use X,Z, ignore Y
	estrClear(estr);
	estrPrintf(estr, "%d,%d", roundDown(position[0] / BUCKET_DIMENSION), roundDown(position[2] / BUCKET_DIMENSION));
	return *estr;
}

void addTicketToBucket(TicketEntry *ticket)
{
	StashTable pZoneTable = NULL;
	char *pLocationKey = NULL;
	TicketEntry ***eaTickets = NULL;
	if (!ticket || !ticket->gameLocation.zoneName || !ticket->gameLocation.zoneName[0])
		return;

	stashFindPointer(sStashZoneName, ticket->gameLocation.zoneName, &pZoneTable);
	if (!pZoneTable)
	{
		pZoneTable = stashTableCreateWithStringKeys(25, StashDeepCopyKeys_NeverRelease);
		stashAddPointer(sStashZoneName, ticket->gameLocation.zoneName, pZoneTable, true);
	}

	tlocGetStringKey (&pLocationKey, ticket->gameLocation.position);
	stashFindPointer(pZoneTable, pLocationKey, (void**) &eaTickets);
	if (!eaTickets)
	{
		eaTickets = (TicketEntry***) calloc(1, sizeof(TicketEntry**));
		eaCreate(eaTickets);

		assert(stashAddPointer(pZoneTable, pLocationKey, eaTickets, false));
	}

	eaPush(eaTickets, ticket);
}

void removeTicketFromGrid(TicketEntry *ticket)
{
	StashTable pZoneTable = NULL;
	char *pLocationKey = NULL;
	TicketEntry ***eaTickets = NULL;

	if (!ticket || !ticket->gameLocation.zoneName || !ticket->gameLocation.zoneName[0])
		return;	
	stashFindPointer(sStashZoneName, ticket->gameLocation.zoneName, &pZoneTable);
	if (!pZoneTable)
		return;
	
	tlocGetStringKey (&pLocationKey, ticket->gameLocation.position);
	stashFindPointer(pZoneTable, pLocationKey, (void**) &eaTickets);
	eaFindAndRemove(eaTickets, ticket);
}

// truncated down
static int distanceSquared(SA_PARAM_NN_VALID TicketClientGameLocation *location1, SA_PARAM_NN_VALID TicketClientGameLocation *location2)
{
	float dx = location1->position[0] - location2->position[0];
	float dz = location1->position[2] - location2->position[2];

	return (dx*dx + dz*dz);
}
static float exactDistanceSquared(SA_PARAM_NN_VALID TicketClientGameLocation *location1, SA_PARAM_NN_VALID TicketClientGameLocation *location2)
{
	float dx = location1->position[0] - location2->position[0];
	float dz = location1->position[2] - location2->position[2];

	return (dx*dx + dz*dz);
}

void findNearbyTickets(TicketEntry ***eaTickets, TicketClientGameLocation *location)
{
	StashTable pZoneTable = NULL;
	char *pLocationKey = NULL;
	int index[2];
	int x,y;
	int i, size;

	if (!location->zoneName || !location->zoneName[0])
		return;

	stashFindPointer(sStashZoneName, location->zoneName, &pZoneTable);
	if (!pZoneTable)
		return;

	index[0] = roundDown(location->position[0] / (float) BUCKET_DIMENSION);
	index[1] = roundDown(location->position[2] / (float) BUCKET_DIMENSION);

	for (x=index[0]-1; x<index[0]+2; x++)
	{
		for (y=index[1]-1; y<index[1]+2; y++)
		{
			TicketEntry ***eaGridTickets = NULL;

			// TODO special case 0, -0?
			estrClear(&pLocationKey);
			estrPrintf(&pLocationKey, "%d,%d", x, y);

			stashFindPointer(pZoneTable, pLocationKey, (void**) &eaGridTickets);
			if (!eaGridTickets)
				continue;
			size = eaSize(eaGridTickets);
			for (i=0; i<size; i++)
			{
				TicketEntry *ticket = (*eaGridTickets)[i];
				ticket->fDistance = distanceSquared(location, (TicketClientGameLocation*) &ticket->gameLocation);
				if (ticket->fDistance < MAX_DISTANCE_SQUARED)
				{
					eaPush(eaTickets, (*eaGridTickets)[i]);
				}
			}
		}
	}
}

int ticketDistanceCmp(const TicketEntry **p1, const TicketEntry **p2, const void *ign)
{
	if      ((*p1)->fDistance < (*p2)->fDistance) return -1;
	else if ((*p1)->fDistance > (*p2)->fDistance) return  1;
	else return 0;
}

TicketEntry * findClosestStuckTicket(TicketClientGameLocation *location)
{
	StashTable pZoneTable = NULL;
	char *pLocationKey = NULL;
	int index[2];
	int x,y;
	int i, size;
	TicketEntry *closest = NULL;
	float fClosestDistance = (float) MAX_STUCK_DISTANCE_SQUARED;

	if (!location->zoneName || !location->zoneName[0])
		return NULL;

	stashFindPointer(sStashZoneName, location->zoneName, &pZoneTable);
	if (!pZoneTable)
		return NULL;

	index[0] = roundDown(location->position[0] / (float) BUCKET_DIMENSION);
	index[1] = roundDown(location->position[2] / (float) BUCKET_DIMENSION);

	for (x=index[0]-1; x<index[0]+2; x++)
	{
		for (y=index[1]-1; y<index[1]+2; y++)
		{
			TicketEntry ***eaGridTickets = NULL;

			estrClear(&pLocationKey);
			estrPrintf(&pLocationKey, "%d,%d", x, y);

			stashFindPointer(pZoneTable, pLocationKey, (void**) &eaGridTickets);
			if (!eaGridTickets)
				continue;
			size = eaSize(eaGridTickets);
			for (i=0; i<size; i++)
			{
				TicketEntry *ticket = (*eaGridTickets)[i];
				if (ticket->pLabel && stricmp(ticket->pLabel, "stuck_killme_cmd") == 0)
				{
					ticket->fDistance = exactDistanceSquared(location, (TicketClientGameLocation*) &ticket->gameLocation);
					if (ticket->fDistance < fClosestDistance)
					{
						closest = ticket;
						fClosestDistance = ticket->fDistance;
					}
				}
			}
		}
	}
	return closest;
}