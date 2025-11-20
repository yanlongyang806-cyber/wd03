// Functions specific to Exclusion Volumes, whether
// they be terrain objects, Genesis interior objects, etc.
#pragma once
GCC_SYSTEM

#include "WorldLibEnums.h"

typedef struct GroupDef GroupDef;
typedef struct GroupInfo GroupInfo;
typedef struct GroupInheritedInfo GroupInheritedInfo;
typedef struct MersenneTable MersenneTable;

// It's over nine million!!!!
#define EXCLUSION_TERRAIN_HEIGHT (-9000000)

AUTO_ENUM;
typedef enum ExclusionVolumeType
{
	EXCLUDE_VOLUME_SPHERE,
	EXCLUDE_VOLUME_BOX,
} ExclusionVolumeType;

typedef enum ExclusionRotationType
{
	EXCLUDE_ROT_TYPE_IN_PLACE=0,			// In-place rotation of all subobjects; positions rigidly rotate around pivot
	EXCLUDE_ROT_TYPE_FULL,					// Full rotation around multi-excluder pivot and local object pivot
	EXCLUDE_ROT_TYPE_NO_ROT					// Rotate the entire multi-excluder rigidly
} ExclusionRotationType;

typedef enum ExclusionVolumeFlags
{
	EXCLUDE_VOLUME_FLOOR	= 1<<0,			//Floor, Wall, and Ceiling must be first
	EXCLUDE_VOLUME_WALL		= 1<<1,
	EXCLUDE_VOLUME_CEILING	= 1<<2,
	EXCLUDE_VOLUME_SUBTRACT	= 1<<3,
} ExclusionVolumeFlags;

AUTO_STRUCT;
typedef struct ExclusionVolume
{
	ExclusionVolumeType type;
									// For Box
	Vec3 extents[2];						AST( INDEX(0, "MinExtent") INDEX (1, "MaxExtent"))
	F32 begin_radius;						// For Sphere
	F32 end_radius;							// For Sphere
	Mat4 mat;
	bool is_a_path;							// Temporary flag
	bool is_on_path;						// Temporary flag

	bool above_terrain;						// This volume is always above the terrain (Terrain objects only)
	bool below_terrain;						// This volume is always below the terrain (Terrain objects only)
	bool collides;							// This volume collides with other colliding volumes
	WorldTerrainCollisionType col_type;		// Sometimes used to determine if two volumes should collide with each other
	
	// This volume can support objects above it (Interiors only)
	WorldPlatformType platform_type;		AST( NAME(Platform) )
	bool challenges_only;					// If volume is platform, only let challenges be on it (Interiors only)
} ExclusionVolume;
extern ParseTable parse_ExclusionVolume[];
#define TYPE_parse_ExclusionVolume ExclusionVolume

typedef struct ExclusionVolumeGroup
{
	ExclusionVolume **volumes;
	int idx_in_parent;
	bool optional;							// Don't fail on collision with this volume
	bool is_actor;
	Mat4 mat_offset;

	bool was_placed;
	Mat4 final_mat;							// Valid after placement success
} ExclusionVolumeGroup;

typedef struct ExclusionObject
{
	U32 id;

	Mat4 mat;

	ExclusionVolumeGroup *volume_group;
	F32 max_radius;

	U32 test_number_volume;
	U32 ref_count;
	U32 provisional_ref_count;
	int *provisional_positions;
	U8 flags;

	// Objects in different planes do not exclude each other.
	//
	// If PLANE-ID is 0, then this is in the shared plane, which
	// excludes with every other plane
	int plane_id;

	// If set, volume_group should be freed as well
	bool volume_group_owned;
	
	void *userdata;
	bool userdata_owned;
} ExclusionObject;

typedef struct HeightMapExcludeGridElement
{
	bool full;
	ExclusionObject **excluders;
} HeightMapExcludeGridElement;

typedef struct HeightMapExcludeGrid
{
	HeightMapExcludeGridElement *elements;
	ExclusionObject **provisional_objects;
	int width, height;
	IVec2 min;
} HeightMapExcludeGrid;

typedef struct HeightMapExcludeStat
{
	int exclusion_debug_sphere_sphere_count;
	int exclusion_debug_sphere_box_count;
	int exclusion_debug_box_box_count;
} HeightMapExcludeStat;

void exclusionObjectFree(ExclusionObject *object);
void exclusionGridClear(HeightMapExcludeGrid* grid);
bool exclusionGridVolumeGroupCollision(ExclusionVolume **volumes_1, Mat4 vols_parent_mat_1, ExclusionVolume **volumes_2, Mat4 vols_parent_mat_2);
bool exclusionGridVolumeGridCollision(HeightMapExcludeGrid *grid, Mat4 item_matrix, F32 max_radius, ExclusionVolume **volumes, int plane_id, HeightMapExcludeStat *stats, U64 test_num, U32 *collide_id);
F32 exclusionCollideLine(HeightMapExcludeGrid *grid, const Vec3 start, const Vec3 end, U8 flags, int plane_id, U64 test_num, Vec3 nearest_point, ExclusionObject** out_nearest_object);
HeightMapExcludeGrid *exclusionGridCreate(S32 min_x, S32 min_z, S32 width, S32 height);
void exclusionGridFree(HeightMapExcludeGrid *grid);
void exclusionGridAddObject(HeightMapExcludeGrid *grid, ExclusionObject *object, F32 radius, bool provisional);
void exclusionGridPurgeProvisionalObjects(HeightMapExcludeGrid *grid);
void exclusionGridCommitProvisionalObjects(HeightMapExcludeGrid *grid);
void exclusionGetDefVolumes(GroupDef *def, ExclusionVolume ***volumes, const Mat4 parent_mat, bool add_encounters, int room_level, ExclusionVolumeGroup ***groups);
int exclusionGetDefVolumeGroups(GroupDef *def, ExclusionVolumeGroup ***groups, bool add_encounters, int room_level);
bool exclusionVolumesTraverseCallback(ExclusionObject *world_excluders, GroupDef *def, GroupInfo *info, GroupInheritedInfo *inherited_info, bool needs_entry);
void exclusionVolumeGroupDestroy(ExclusionVolumeGroup *group);
bool exclusionCheckVolumesInRange(ExclusionVolumeGroup *group, Mat4 item_matrix, F32 min_x, F32 min_z, F32 width, F32 height);
void exclusionCalculateObjectHeight(HeightMapExcludeGrid *grid, Mat4 item_matrix, F32 max_radius, U64 test_num, bool is_challenge, int iPartitionIdx, F32 **ret_heights, F32 **ret_normals);
F32 exclusionCalculateObjectHeightDefault(HeightMapExcludeGrid *grid, Mat4 item_matrix, F32 max_radius, U64 test_num, bool is_challenge, int iPartitionIdx);
bool exclusionGridIsIntersectingPlanes(int plane_id1, int plane_id2);
