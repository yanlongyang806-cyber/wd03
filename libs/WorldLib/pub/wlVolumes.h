#ifndef __WLVOLUMES_H__
#define __WLVOLUMES_H__
GCC_SYSTEM

#include "UtilitiesLibEnums.h"

typedef struct WorldVolume WorldVolume;
typedef struct WorldVolumeQueryCache WorldVolumeQueryCache;
typedef struct GConvexHull GConvexHull;
typedef struct GMesh GMesh;
typedef struct Capsule Capsule;

typedef void (*WorldVolumeQueryCacheChangeCallback)(WorldVolume *volume, WorldVolumeQueryCache *query_cache);
typedef void (*WorldVolumeDataFreeCallback)(void *volume_data);

AUTO_ENUM;
typedef enum WorldVolumeShape
{
	WL_VOLUME_BOX,
	WL_VOLUME_SPHERE,
	WL_VOLUME_HULL,
} WorldVolumeShape;
extern StaticDefineInt WorldVolumeShapeEnum[];

AUTO_STRUCT;
typedef struct WorldVolumeElement
{
	WorldVolumeShape volume_shape;
	VolumeFaces face_bits;	AST(INT)	// for box
	Vec3 local_min;				// for box
	Vec3 local_max;				// for box
	F32 radius;					// for sphere
	Mat4 world_mat;
	Mat4 inv_world_mat;			NO_AST // for internal use

	// hull data: need both plane (via hull) and point (via mesh) data for collision checking
	GConvexHull *hull;			NO_AST // not parsable, but not used in world cell entries so not necessary
	GMesh *mesh;				NO_AST // not parsable, but not used in world cell entries so not necessary
} WorldVolumeElement;
extern ParseTable parse_WorldVolumeElement[];
#define TYPE_parse_WorldVolumeElement WorldVolumeElement

AUTO_STRUCT AST_STARTTOK("") AST_ENDTOK(EndDOFValues) AST_IGNORE(blurSize);
typedef struct DOFValues
{
	F32		time;					AST( NAME(Time) WIKI("Time of day (0 - 24) at which this SkyTime applies.  Only useful if you are placing this in a sky.") )
    
	F32		nearDist;					AST( WIKI("The weight of the depth-of-field blur equals nearValue at this depth." ) )
	F32		nearValue;					AST( WIKI("At this depth and closer, the depth-of-field has this blur factor, ranged [0-1], where one is maximum blur." ) )
	F32		focusDist;					AST( WIKI("This is the depth of the focal plane of the camera. The blur weight is focusValue at this depth, and is typically zero or a low blur weight because this depth should be (but is not required to be) in focus." ) )
	F32		focusValue;					AST( WIKI("The in-focus blur factor. See nearValue for range and interpretation." ) )
	F32		farDist;					AST( WIKI("All depths equal-to or further-than this depth have the blur factor farValue." ) )
	F32		farValue;					AST( WIKI("The furthest blur factor. See nearValue for range and interpretation." ) )
	F32		skyValue;					AST( WIKI("The blur factor for the sky. See nearValue for range and interpretation." ) )

	Vec3	borderColorHSV;				AST( FORMAT_HSV WIKI("Additive border color (can be negative)." ) )
	F32		borderColorScale;			AST( WIKI("Scale on the border color for allowing negative colors." ) )
	F32		borderRamp;					AST( WIKI("Border ramp.  Smaller values start the border nearer the edge." ) )
	F32		borderBlur;					AST( WIKI("Amount of blur to apply to border pixels." ) )

	Vec3	depthAdjustFgHSV;			AST( FORMAT_HSV WIKI("Saturation and value adjustment for the foreground." ) )
	Vec3	depthAdjustBgHSV;			AST( FORMAT_HSV WIKI("Saturation and value adjustment for the background." ) )
	Vec3	depthAdjustSkyHSV;			AST( FORMAT_HSV WIKI("Saturation and value adjustment for the sky." ) )
	F32		depthAdjustNearDist;		AST( WIKI("Distance to start background adjustment.") )
	F32		depthAdjustFadeDist;		AST( WIKI("Distance across which to fade from FG to BG.") )

	U32		bfParamsSpecified[1];		AST( USEDFIELD USERFLAG(TOK_USEROPTIONBIT_1) )
} DOFValues;

extern ParseTable parse_DOFValues[];
#define TYPE_parse_DOFValues DOFValues

AUTO_STRUCT AST_STARTTOK("") AST_ENDTOK(EndLowEnd);
typedef struct WorldVolumeWaterLowEnd {
	Vec3		waterColorHSV;			AST(FORMAT_HSV)
	F32			minPercent;				AST(DEFAULT(0.6))
	F32			maxPercent;				AST(DEFAULT(0.95))
	F32			maxDist;				AST(DEFAULT(100))
	const char* materialNearSurface;	AST(DEFAULT("WaterNearSurface_default"))
} WorldVolumeWaterLowEnd;

AUTO_STRUCT AST_STARTTOK("Water") AST_ENDTOK("EndWater");
typedef struct WorldVolumeWater {
    const char* filename;        AST(CURRENTFILE KEY)

    const char* materialName;    AST(DEFAULT("WaterVolume_default"))
    F32 refractMin;              AST(DEFAULT(0.5))
    F32 refractMax;              AST(DEFAULT(6))
    F32 rippleMin;               AST(DEFAULT(1))
    F32 rippleMax;               AST(DEFAULT(12))
    DOFValues dofValues;
	WorldVolumeWaterLowEnd lowEnd; AST(NAME(LowEnd))
} WorldVolumeWater;

const char*** wlVolumeWaterDefKeys( const char*** buffer );
WorldVolumeWater* wlVolumeWaterFromKey( const char* key );
bool wlVolumeWaterReloadedThisFrame(void);
void wlVolumeWaterClearReloadedThisFrame(void);

//////////////////////////////////////////////////////////////////////////
// WorldVolume query caches have types that identify which kind of object
// owns the cache.  Store these values once here so that they don't need
// to be re-calculated everywhere they're used.
// Functions using these types should check that they're initialized before
// using them (and initialize them if necessary).
//////////////////////////////////////////////////////////////////////////

extern U32 s_EntityVolumeQueryType;


//////////////////////////////////////////////////////////////////////////
// These two bit masks allow you to mask off types that are server only or client only.
// Array index 0 is the client-only mask and array index 1 is the server-only mask.
//////////////////////////////////////////////////////////////////////////

extern U32 volume_type_bit_masks[2];


//////////////////////////////////////////////////////////////////////////
// WorldVolume objects can be created from either OOB or sphere bounds.
// They contain a user data pointer and a type bit so that query objects
// can find the particular volume types they are looking for and pull
// type specific data off of them.
//////////////////////////////////////////////////////////////////////////

void wlVolumeStartup(void);
void wlVolumeCreatePartition(int iPartitionIdx);
void wlVolumeDestroyPartition(int iPartitionIdx);
bool wlVolumePartitionExists(int iPartitionIdx);
int wlVolumeMaxPartitionIndex(void);

// mapping function from volume type name to volume type bit (please cache the result!)
// if your volume type is client-only or server-only, please add it to the lists inside wlVolumeTypeNameToBitMask
U32 wlVolumeTypeNameToBitMask(SA_PARAM_NN_STR const char *volume_type);
SA_RET_OP_VALID const char *wlVolumeBitMaskToTypeName(U32 bit);

// volume creation and free functions
SA_RET_NN_VALID WorldVolume *wlVolumeCreate_dbg(int iPartitionIdx, U32 volume_type, void *volume_data, const WorldVolumeElement **elements MEM_DBG_PARMS);
#define wlVolumeCreate(iPartitionIdx, volume_type, volume_data, elements) wlVolumeCreate_dbg(iPartitionIdx, volume_type, volume_data, elements MEM_DBG_PARMS_INIT)

SA_RET_NN_VALID WorldVolume *wlVolumeCreateBox_dbg(int iPartitionIdx, U32 volume_type, void *volume_data, SA_PRE_NN_RBYTES(sizeof(Mat4)) const Mat4 world_mat, SA_PRE_NN_RBYTES(sizeof(Vec3)) const Vec3 local_min, SA_PRE_NN_RBYTES(sizeof(Vec3)) const Vec3 local_max, VolumeFaces face_bits MEM_DBG_PARMS);
#define wlVolumeCreateBox(iPartitionIdx, volume_type, volume_data, world_mat, local_min, local_max, face_bits) wlVolumeCreateBox_dbg(iPartitionIdx, volume_type, volume_data, world_mat, local_min, local_max, face_bits MEM_DBG_PARMS_INIT)

SA_RET_NN_VALID WorldVolume *wlVolumeCreateSphere_dbg(int iPartitionIdx, U32 volume_type, void *volume_data, SA_PRE_NN_RBYTES(sizeof(Mat4)) const Mat4 world_mat, SA_PRE_NN_RBYTES(sizeof(Vec3)) const Vec3 local_mid, F32 radius MEM_DBG_PARMS);
#define wlVolumeCreateSphere(iPartitionIdx, volume_type, volume_data, world_mat, local_mid, radius) wlVolumeCreateSphere_dbg(iPartitionIdx, volume_type, volume_data, world_mat, local_mid, radius MEM_DBG_PARMS_INIT)

#define wlVolumeCreateHull(iPartitionIdx, volume_type, volume_data, world_mat, local_min, local_max, mesh, hull) wlVolumeCreateHull_dbg(iPartitionIdx, volume_type, volume_data, world_mat, local_min, local_max, mesh, hull MEM_DBG_PARMS_INIT)

void wlVolumeUpdateBox(SA_PARAM_NN_VALID WorldVolume *volume, SA_PRE_NN_RBYTES(sizeof(Mat4)) const Mat4 world_mat, SA_PRE_NN_RBYTES(sizeof(Vec3)) const Vec3 local_min, SA_PRE_NN_RBYTES(sizeof(Vec3)) const Vec3 local_max);
void wlVolumeUpdateSphere(SA_PARAM_NN_VALID WorldVolume *volume, SA_PRE_NN_RBYTES(sizeof(Mat4)) const Mat4 world_mat, SA_PRE_NN_RBYTES(sizeof(Vec3)) const Vec3 local_mid, F32 radius);
void wlVolumeFree(SA_PRE_OP_VALID SA_POST_P_FREE WorldVolume *volume);

// volume accessors
void *wlVolumeGetVolumeData(SA_PARAM_NN_VALID const WorldVolume *volume);
U32 wlVolumeGetVolumeType(SA_PARAM_NN_VALID const WorldVolume *volume);
bool wlVolumeIsType(SA_PARAM_NN_VALID const WorldVolume *volume, U32 volume_type);
void wlVolumeGetVolumeWorldMid(SA_PARAM_NN_VALID const WorldVolume *volume, Vec3 world_mid);
void wlVolumeGetWorldMinMax(SA_PARAM_NN_VALID const WorldVolume *volume, Vec3 world_min, Vec3 world_max);
void wlVolumeGetWorldPosRotMinMax(SA_PARAM_NN_VALID const WorldVolume *volume, Vec3 world_pos, Quat rot, Vec3 local_min, Vec3 local_max);
WorldVolumeQueryCache **wlVolumeGetCachedQueries(SA_PARAM_NN_VALID const WorldVolume *volume);
void wlVolumeSetQueryCallbacks(SA_PARAM_NN_VALID WorldVolume *volume, WorldVolumeQueryCacheChangeCallback entered_callback, WorldVolumeQueryCacheChangeCallback exited_callback, WorldVolumeQueryCacheChangeCallback remain_callback);
void wlVolumeSetDataFreeCallback(SA_PARAM_NN_VALID WorldVolume *volume, WorldVolumeDataFreeCallback free_callback);
F32 wlVolumeGetSize(SA_PARAM_NN_VALID const WorldVolume *volume);

F32 wlVolumeGetProgressZ(SA_PARAM_NN_VALID const WorldVolume *volume, SA_PRE_NN_RBYTES(sizeof(Vec3)) const Vec3 world_point);

//////////////////////////////////////////////////////////////////////////
// WorldVolumeQueryCache objects are used to find volumes intersecting
// either OOB or sphere bounds.  They automatically cache results.  They
// also contain a user pointer and type bit so that volumes can store what
// cached query objects are contained within them.
//////////////////////////////////////////////////////////////////////////

// mapping function from query type name to query type bit (please cache the result!)
U32 wlVolumeQueryCacheTypeNameToBitMask(SA_PARAM_NN_STR const char *query_type);

// volume cached query cache creation and free functions
SA_RET_NN_VALID WorldVolumeQueryCache *wlVolumeQueryCacheCreate_dbg(int iPartitionIdx, U32 query_type, void *query_data MEM_DBG_PARMS);
#define wlVolumeQueryCacheCreate(iPartitionIdx, query_type, query_data) wlVolumeQueryCacheCreate_dbg(iPartitionIdx, query_type, query_data MEM_DBG_PARMS_INIT)
void wlVolumeQueryCacheFree(SA_PRE_OP_VALID SA_POST_P_FREE WorldVolumeQueryCache *query_cache);

// query accessors
void *wlVolumeQueryCacheGetData(SA_PARAM_NN_VALID const WorldVolumeQueryCache *query_cache);
U32 wlVolumeQueryCacheGetType(SA_PARAM_NN_VALID const WorldVolumeQueryCache *query_cache);
bool wlVolumeQueryCacheIsType(SA_PARAM_NN_VALID const WorldVolumeQueryCache *query_cache, U32 query_type);
void wlVolumeQuerySetCallbacks(SA_PARAM_NN_VALID WorldVolumeQueryCache *query_cache, WorldVolumeQueryCacheChangeCallback entered_callback, WorldVolumeQueryCacheChangeCallback exited_callback, WorldVolumeQueryCacheChangeCallback remain_callback);

// volume cached query functions - bytype functions filter out non-volume_type volumes
const WorldVolume **wlVolumeCacheQueryBox(SA_PARAM_NN_VALID WorldVolumeQueryCache *query_cache, SA_PRE_NN_RBYTES(sizeof(Mat4)) const Mat4 world_mat, SA_PRE_NN_RBYTES(sizeof(Vec3)) const Vec3 local_min, SA_PRE_NN_RBYTES(sizeof(Vec3)) const Vec3 local_max);
const WorldVolume **wlVolumeCacheQueryBoxByType(SA_PARAM_NN_VALID WorldVolumeQueryCache *query_cache, SA_PRE_NN_RBYTES(sizeof(Mat4)) const Mat4 world_mat, SA_PRE_NN_RBYTES(sizeof(Vec3)) const Vec3 local_min, SA_PRE_NN_RBYTES(sizeof(Vec3)) const Vec3 local_max, U32 volume_type);
const WorldVolume **wlVolumeCacheQuerySphere(SA_PARAM_NN_VALID WorldVolumeQueryCache *query_cache, SA_PRE_NN_RBYTES(sizeof(Vec3)) const Vec3 world_mid, F32 radius);
const WorldVolume **wlVolumeCacheQuerySphereByType(SA_PARAM_NN_VALID WorldVolumeQueryCache *query_cache, SA_PRE_NN_RBYTES(sizeof(Vec3)) const Vec3 world_mid, F32 radius, U32 volume_type);
const WorldVolume **wlVolumeCacheQueryCapsule(SA_PARAM_NN_VALID WorldVolumeQueryCache *query_cache, SA_PARAM_NN_VALID const Capsule *cap, SA_PRE_NN_RBYTES(sizeof(Mat4)) const Mat4 world_mat);
const WorldVolume **wlVolumeCacheQueryCapsuleByType(SA_PARAM_NN_VALID WorldVolumeQueryCache *query_cache, SA_PARAM_NN_VALID const Capsule *cap, SA_PRE_NN_RBYTES(sizeof(Mat4)) const Mat4 world_mat, U32 volume_type);
const WorldVolume **wlVolumeCacheGetCachedVolumes(SA_PARAM_NN_VALID WorldVolumeQueryCache *query_cache);

// ray collide
bool wlVolumeRayCollide(int iPartitionIdx, SA_PRE_NN_RBYTES(sizeof(Vec3)) const Vec3 start, SA_PRE_NN_RBYTES(sizeof(Vec3)) const Vec3 end, U32 volume_type, SA_PRE_OP_BYTES(sizeof(Vec3)) Vec3 hit_location);
bool wlVolumeRayCollideSpecifyVolumes(SA_PRE_NN_RBYTES(sizeof(Vec3)) const Vec3 start, SA_PRE_NN_RBYTES(sizeof(Vec3)) const Vec3 end, const WorldVolume **volumes, SA_PRE_OP_BYTES(sizeof(Vec3)) Vec3 hit_location);

U32 wlVolumeGetWorldVolumeStateTimestamp(int iPartitionIdx);
#endif //__WLVOLUMES_H__
