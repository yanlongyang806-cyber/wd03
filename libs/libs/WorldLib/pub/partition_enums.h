/***************************************************************************



***************************************************************************/

#pragma once

// This is an illegal partition value, but is used during the transition to mean "not converted yet"
#define PARTITION_UNINITIALIZED		0

// This value is used on the client when the concept of partition is not relevant
#define PARTITION_CLIENT			0

// This value is only legal when used with the event system to subscribe across all partitions
#define PARTITION_ANY				-111

// This value is used on entities that are in the process of being destroyed
#define PARTITION_ENT_BEING_DESTROYED	-333

// This value is used on the client in tools and such when the concept of partition is not relevant
#define PARTITION_IN_TRANSACTION	-666

// This value is used on the server when a pet arrives and the owner isn't present
#define PARTITION_ORPHAN_PET		-777

// This value is used when doing static checking of expressions to avoid getting real partition usage
#define PARTITION_STATIC_CHECK		-999

//idx 0 is never used... so the highest IDX is the same as the number of actual partitions
#define MAX_LEGAL_PARTITION_IDX 31
#define MAX_ACTUAL_PARTITIONS (MAX_LEGAL_PARTITION_IDX)

//this enum is used by TestPartitionForImmediateDeath
AUTO_ENUM;
typedef enum PartitionDeathReason
{
	PARTITIONDEATH_PLAYER_TRANSFERRED_OFF,
} PartitionDeathReason;

