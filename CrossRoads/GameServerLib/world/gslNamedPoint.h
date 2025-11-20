/***************************************************************************
*     Copyright (c) 2006, Cryptic Studios
*     All Rights Reserved
*     Confidential Property of Cryptic Studios
***************************************************************************/

#pragma once

typedef struct WorldScope WorldScope;
typedef struct WorldNamedPoint WorldNamedPoint;
typedef struct ZoneMap ZoneMap;

typedef struct GameNamedPoint
{
	// The named point's map-level name
	const char *pcName;

	// The world named point
	WorldNamedPoint *pWorldPoint;
} GameNamedPoint;

// Gets a named point, if one exists
SA_RET_OP_VALID GameNamedPoint *namedpoint_GetByName(SA_PARAM_NN_STR const char *pcNamedPointName, SA_PARAM_OP_VALID const WorldScope *pScope);
SA_RET_OP_VALID GameNamedPoint *namedpoint_GetByPosition(const Vec3 vPos);

// Check if a named point exists
bool namedpoint_NamedPointExists(const char *pcName, const WorldScope *pScope);

// Get position.  Returns true if it exists
bool namedpoint_GetPositionByName(const char *pcName, Vec3 vPosition, Quat qRot);
bool namedpoint_GetPosition(GameNamedPoint *pPoint, Vec3 vPosition, Quat qRot);

// Called on map load and unload
void namedpoint_MapLoad(ZoneMap *pZoneMap);
void namedpoint_MapUnload(void);
void namedpoint_MapValidate(ZoneMap *pZoneMap);

