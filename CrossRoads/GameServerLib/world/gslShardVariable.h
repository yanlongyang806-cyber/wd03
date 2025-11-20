/***************************************************************************
*     Copyright (c) 2006, Cryptic Studios
*     All Rights Reserved
*     Confidential Property of Cryptic Studios
***************************************************************************/

#pragma once

typedef struct ZoneMap ZoneMap;

// Called on map load and unload
void shardvariable_MapLoad(ZoneMap *pZoneMap);
void shardvariable_MapUnload(void);
void shardvariable_MapValidate(ZoneMap *pZoneMap);