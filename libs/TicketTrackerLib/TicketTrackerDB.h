#pragma once

#define TICKETTRACKER_MERGER_NAME "TTMerger"
void TicketTrackerDBInit(void);
int getSnapshotInterval(void);

void TicketTrackerCreateSnapshot(void);
int TicketTrackerGetMergerPID(void);