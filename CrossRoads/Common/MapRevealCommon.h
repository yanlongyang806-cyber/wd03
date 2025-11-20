#pragma once
GCC_SYSTEM

typedef struct RoomConnGraph RoomConnGraph;
typedef struct WorldRegion WorldRegion;
typedef struct Room Room;
typedef struct Entity Entity;

AUTO_ENUM;
typedef enum MapRevealType
{
	kMapRevealType_All,				// Reveal the entire region.
	kMapRevealType_EnteredRooms,	// Reveal all entered rooms.
	kMapRevealType_Grid,			// Reveal using a NxM grid over the region.
	kMapRevealType_MAX, EIGNORE
} MapRevealType;

AUTO_STRUCT AST_CONTAINER;
typedef struct MapRevealInfo
{
	// The name of this map region - <ZoneMapName>.<RegionName>
	const char *pchName;	AST(NAME(Name) KEY STRUCTPARAM POOL_STRING PERSIST NO_TRANSACT)

	// The type of reveal we should do on this map, stored here so we can
	// reset reveal data if it changes.
	MapRevealType eType;	AST(NAME(Type) PERSIST NO_TRANSACT)

	// If revealing by room, the number of rooms this map has.
	// Needs to be stored so we can wipe reveal info if it changes.
	U32 uiRoomCount;		AST(NAME(RoomCount) PERSIST NO_TRANSACT)

	// Store region size so we can wipe the map when it changes.
	Vec3 v3RegionMin;		AST(NAME(RegionBoundsMin) PERSIST NO_TRANSACT)
	Vec3 v3RegionMax;		AST(NAME(RegionBoundsMax) PERSIST NO_TRANSACT)

	F32  fGroundFocusHeight; // ground focus height for the same reason, for ortho skew

	// Bitfield describing what is revealed - if a bit is on, that room
	// or section of an overhead map should be drawn. Unpersisted, derived
	// at load from pchRevealedString, but sent to the client.
	U32 *eaiRevealed;

	// Zipped version of eaiRevealed, for database performance.
	char *pchRevealedString; AST(NAME(RevealedString) PERSIST NO_TRANSACT SERVER_ONLY ESTRING)

	// TODO: Do we want to just send pchRevealedString to the client and make it
	// handle the zipping/unzipping? Need to compare cost of memcmp of eaiRevealed
	// versus cost of unzipping.
} MapRevealInfo;

// <ZoneName>.<RegionName>	both of these are up to MAX_PATH long.
#define MAPREVEAL_MAX_KEY (MAX_PATH * 4)

// If a map is bigger than this value (in feet) in any dimension, it's considered
// an "outdoor" map as opposed to an "indoor" one, and the default reveal algorithm
// changes to grid-based instead of room-based.
#define MAPREVEAL_OUTSIDE_SIZE (5280.f/4.f)

// use this many bits at most for grid-based map reveals in each dimension, calculate
// the other dimension in a way that keeps reveals square.
#define MAPREVEAL_MAX_BITS_PER_DIMENSION (256)

// The distance to reveal on exterior maps.  Put into gMapRevealPlayerDist and can be set with 
// autocmd SetMapRevealDistance x.
#define MAPREVEAL_PLAYER_DISTANCE (80.f)

// A lot of rooms are setup slightly above the floor collision. They get revealed if they are 
// within this threshold of the player. (especially important at the moment the "enter volume" 
// callback is tripped)
#define MAPREVEAL_TOLERANCE (2.0f)

// if for some reason the zone/region don't have public names, these
// are used instead.
#define MAPREVEAL_DEFAULT_ZONE_NAME "DefaultMapRevealZone"
#define MAPREVEAL_DEFAULT_REGION_NAME "DefaultMapRevealRegion"

#define MAPREVEAL_GRID_BIT(iCellX, iCellZ) (((iCellX) * MAPREVEAL_MAX_BITS_PER_DIMENSION) + (iCellZ))

//for map reveal debugging:
//#define MAPREVEAL_PRINTF(fmt, ...) printf(fmt "\n", __VA_ARGS__)
#define MAPREVEAL_PRINTF(fmt, ...)

// Return the entity's map reveal info for the given region, or the entity's current region if pRegion is NULL.
SA_RET_OP_VALID MapRevealInfo *mapRevealInfoGetByRegion(SA_PARAM_OP_VALID Entity *pEnt, SA_PARAM_OP_VALID WorldRegion *pRegion);

// Return the percentage of this region's map revealed so far.
F32 mapRevealGetPercentage(SA_PARAM_OP_VALID MapRevealInfo *pInfo);

// Returns whether or not the position has been revealed
bool mapRevealHasBeenRevealed(MapRevealInfo *pInfo, Vec3 v3Pos, F32 fRadius, Room ***peaRooms);

//puts "zoneName.regionName" into strBufferOut.
void mapRevealGetZoneAndRegionName(WorldRegion *pRegion, char *strBufferOut, int strBufferOut_size);