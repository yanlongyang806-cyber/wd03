#ifndef _MAPSNAP_H
#define _MAPSNAP_H
GCC_SYSTEM

typedef struct WorldRegion WorldRegion;

AUTO_STRUCT;
typedef struct MapSnapRoomPartitionData
{
	const char **image_name_list;			AST( POOL_STRING FILENAME )
	const char *overview_image_name;		AST( POOL_STRING FILENAME )
	Vec2 vMin;
	Vec2 vMax;
	int image_width;							// actual width of image
	int image_height;							// actual height of image
} MapSnapRoomPartitionData;

AUTO_STRUCT;
typedef struct MapSnapRegionData
{
	F32 fGroundFocusHeight;
} MapSnapRegionData;

extern F32 gfCurrentMapOrthoSkewX;
extern F32 gfCurrentMapOrthoSkewZ;

void mapSnapCalculateRegionData(WorldRegion * pRegion);
void mapSnapWorldPosToMapPos(const Vec3 v3RegionMin, const Vec3 v3RegionMax, const Vec3 v3WorldPos, Vec2 v2MapPos, F32 fRegionFocusHeight);

// Get the map bounds for a region. If the room graph is available the extents of the
// room boundaries are used, otherwise the region boundaries are used (however these are
// often too large).
void mapSnapRegionGetMapBounds(WorldRegion *pRegion, Vec3 v3Min, Vec3 v3Max);
// adjusts for skew when necessary
void mapSnapGetExtendedBounds(Vec3 const v3Min, Vec3 const v3Max, Vec2 const vOrthoSkew,F32 fFocusHeight, Vec2 v2Min, Vec2 v2Max);

void mapSnapUpdateRegion(WorldRegion *pRegion);

bool mapSnapMapNameIsUGC(const char* mapName);

#endif