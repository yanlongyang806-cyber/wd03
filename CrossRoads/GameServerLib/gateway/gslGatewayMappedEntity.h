/***************************************************************************
 *     Copyright (c) 2012-, Cryptic Studios
 *     All Rights Reserved
 *     Confidential Property of Cryptic Studios
 *
 ***************************************************************************/
#ifndef GSLGATEWAYMAPPEDENTITY_H__
#define GSLGATEWAYMAPPEDENTITY_H__
#pragma once
GCC_SYSTEM


typedef struct Entity Entity;
typedef struct ContainerTracker ContainerTracker;

//
// Common entity mapping helpers
//
Entity *cmap_CreateOfflineEntity(ContainerTracker *ptracker);
	// Gets a copy of the entity from the tracker which has had an update tick
	//   run on it so all it's attributes and statistics are up to date and
	//   available.

void cmap_DestroyOfflineEntity(Entity *pOfflineEnt);
	// Destroys the entity created by cmap_CreateOfflineEntity


#endif /* #ifndef GSLGATEWAYMAPPEDENTITY_H__ */

/* End of File */
