#ifndef TICKETENUMS_H
#define TICKETENUMS_H
#pragma once
GCC_SYSTEM

AUTO_ENUM;
typedef enum TicketStatus
{
	TICKETSTATUS_UNKNOWN = 0,			ENAMES(Ticket.Status.Unknown UNKNOWN)
	TICKETSTATUS_OPEN,					ENAMES(Ticket.Status.Open OPEN)
	TICKETSTATUS_CLOSED,				ENAMES(Ticket.Status.Closed CLOSED)
	TICKETSTATUS_RESOLVED,				ENAMES(Ticket.Status.Resolved RESOLVED)
	TICKETSTATUS_IN_PROGRESS,			ENAMES(Ticket.Status.InProgress IN_PROGRESS)
	TICKETSTATUS_PENDING,				ENAMES(Ticket.Status.Pending PENDING)
	TICKETSTATUS_MERGED,				ENAMES(Ticket.Status.Merged MERGED)
	TICKETSTATUS_PROCESSED,				ENAMES(Ticket.Status.Processed PROCESSED)
	TICKETSTATUS_PLAYEREDITED,			ENAMES(Ticket.Status.Edited EDITED)

	TICKETSTATUS_COUNT,					EIGNORE
} TicketStatus;

// Deprecated and not used
AUTO_ENUM;
typedef enum TicketInternalStatus
{
	TICKETINTERNALSTATUS_NONE,		ENAMES(Ticket.InternalStatus.NoEscalation NONE)
	TICKETINTERNALSTATUS_TIER2,		ENAMES(Ticket.InternalStatus.Tier2 TIER2)
	TICKETINTERNALSTATUS_QA,		ENAMES(Ticket.InternalStatus.QA QA)
	TICKETINTERNALSTATUS_DEV,		ENAMES(Ticket.InternalStatus.Dev DEVELOPER)
	TICKETINTERNALSTATUS_SUPER,		ENAMES(Ticket.InternalStatus.Super SUPER)
} TicketInternalStatus;

AUTO_ENUM;
typedef enum TicketVisibility
{
	TICKETVISIBLE_UNKNOWN = -1,         ENAMES(Ticket.Visibility.Unknown UNKNOWN)
	TICKETVISIBLE_PRIVATE = 0,			ENAMES(Ticket.Visibility.Private PRIVATE)
	TICKETVISIBLE_PUBLIC,				ENAMES(Ticket.Visibility.Public PUBLIC)
	TICKETVISIBLE_HIDDEN,				ENAMES(Ticket.Visibility.Hidden HIDDEN)
} TicketVisibility;

#endif