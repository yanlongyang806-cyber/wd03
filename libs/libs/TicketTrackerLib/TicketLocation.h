#ifndef TICKETLOCATION_H
#define TICKETLOCATION_H

typedef struct TicketEntryConst_AutoGen_NoConst TicketEntry;
typedef struct TicketClientGameLocation TicketClientGameLocation;

void initializeTicketLocation(void);
void addTicketToBucket(TicketEntry *ticket);
void removeTicketFromGrid(TicketEntry *ticket);
void findNearbyTickets(SA_PARAM_NN_VALID TicketEntry ***eaTickets, SA_PARAM_NN_VALID TicketClientGameLocation *location);

TicketEntry * findClosestStuckTicket(SA_PARAM_NN_VALID TicketClientGameLocation *location);

#endif