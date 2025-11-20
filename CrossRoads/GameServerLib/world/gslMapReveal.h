#ifndef GSL_MAP_REVEAL_H
#define GSL_MAP_REVEAL_H

typedef struct MapRevealInfo MapRevealInfo;
typedef struct WorldRegion WorldRegion;
typedef struct Room Room;

// Return the MapRevealInfo for the given region, creating it if it does not exist.
SA_RET_OP_VALID MapRevealInfo *gslMapRevealInfoGetOrCreateByRegion(SA_PARAM_OP_VALID Entity *pEnt, SA_PARAM_OP_VALID WorldRegion *pRegion);

// Mark the entity's current location as revealed. Needs to be called periodically,
// since there's no events for non-room-based movement.
void gslMapRevealCurrentLocation(SA_PARAM_OP_VALID Entity *pEnt);

// Mark this position (with X/Z radius) as revealed.
void gslMapRevealCircle(SA_PARAM_OP_VALID Entity *pEnt, SA_PARAM_OP_VALID WorldRegion *pRegion, SA_PARAM_OP_VALID Vec3 v3Position, F32 fRadius);

// Reset reveal information for this region.
void gslMapRevealReset(SA_PARAM_OP_VALID Entity *pEnt, SA_PARAM_OP_VALID WorldRegion *pRegion);

//called when the player enters a room volume, forcing a map reveal (instead of every 20 feet)
void gslMapRevealEnterRoomVolumeCB(Entity *pEnt);

// Get/set the bits corresponding to the given position. Returns true if it revealed anything.
bool mapRevealGetBits(MapRevealInfo *pInfo, Vec3 v3Pos, F32 fRadius, Room ***peaRooms, S32 **peaiBits, bool bFillBit);

#endif