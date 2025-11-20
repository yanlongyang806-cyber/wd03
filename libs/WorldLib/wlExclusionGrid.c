#include "wlExclusionGrid.h"

#include "earray.h"
#include "mathutil.h"
#include "group.h"
#include "grouputil.h"
#include "groupproperties.h"
#include "rand.h"
#include "WorldColl.h"
#include "WorldGrid.h"

#define GRID_SPACING 64

AUTO_RUN_ANON(memBudgetAddMapping(__FILE__, BUDGET_Terrain_System););

void exclusionObjectFree(ExclusionObject *object)
{
	if (--object->ref_count <= 0)
	{
		if (object->volume_group_owned)
		{
			eaDestroyEx(&object->volume_group->volumes, NULL);
			SAFE_FREE(object->volume_group);
		}
		eaiDestroy(&object->provisional_positions);
		if( object->userdata_owned ) {
			SAFE_FREE(object->userdata);
		}
		SAFE_FREE(object);
	}
}

void exclusionGridClear(HeightMapExcludeGrid* grid)
{
	int i;
	for (i = 0; i < grid->width*grid->height; i++)
	{
		eaDestroyEx(&grid->elements[i].excluders, exclusionObjectFree);
	}
}

bool exclusionGridIsIntersectingPlanes(int plane_id1, int plane_id2)
{
	if (plane_id1 == 0 || plane_id2 == 0)
		return true;

	return plane_id1 == plane_id2;
}

HeightMapExcludeGrid *exclusionGridCreate(S32 min_x, S32 min_z, S32 width, S32 height)
{
	HeightMapExcludeGrid *grid = calloc(1, sizeof(HeightMapExcludeGrid));
	setVec2(grid->min, min_x, min_z);
	grid->width = (width+(GRID_SPACING-1))/GRID_SPACING;
	grid->height = (height+(GRID_SPACING-1))/GRID_SPACING;
	grid->elements = calloc(1, grid->width*grid->height*sizeof(HeightMapExcludeGridElement));
	return grid;
}

void exclusionGridFree(HeightMapExcludeGrid *grid)
{
	if (!grid)
		return;
	exclusionGridClear(grid);
	SAFE_FREE(grid->elements);
	SAFE_FREE(grid);
}

void exclusionGridAddObject(HeightMapExcludeGrid *grid, ExclusionObject *object, F32 radius, bool provisional)
{
	int x, z;
	IVec2 min = { (int)(object->mat[3][0]-grid->min[0]-radius)/GRID_SPACING, (int)(object->mat[3][2]-grid->min[1]-radius)/GRID_SPACING };
	IVec2 max = { (int)(object->mat[3][0]-grid->min[0]+radius)/GRID_SPACING, (int)(object->mat[3][2]-grid->min[1]+radius)/GRID_SPACING };
	min[0] = CLAMP(min[0], 0, grid->width-1);
	min[1] = CLAMP(min[1], 0, grid->height-1);
	max[0] = CLAMP(max[0], 0, grid->width-1);
	max[1] = CLAMP(max[1], 0, grid->height-1);

	if (provisional)
		eaPush(&grid->provisional_objects, object);

	for (x = min[0]; x <= max[0]; x++)
		for (z = min[1]; z <= max[1]; z++)
		{
			if (provisional)
			{
				object->provisional_ref_count++;
				eaiPush(&object->provisional_positions, x);
				eaiPush(&object->provisional_positions, z);
			}
			else
			{
				object->ref_count++;
			}
			eaPush(&grid->elements[x+z*grid->width].excluders, object);
		}
}

void exclusionGridPurgeProvisionalObjects(HeightMapExcludeGrid *grid)
{
	int i, j;
	for (i = 0; i < eaSize(&grid->provisional_objects); i++)
	{
		ExclusionObject *object = grid->provisional_objects[i];
		for (j = 0; j < eaiSize(&object->provisional_positions); j += 2)
		{
			int x = object->provisional_positions[j];
			int z = object->provisional_positions[j+1];
			assert(grid->elements[x+z*grid->width].excluders);
			eaFindAndRemove(&grid->elements[x+z*grid->width].excluders, object);
		}
		eaiDestroy(&object->provisional_positions);
		if( object->userdata_owned ) {
			SAFE_FREE(object->userdata);
		}
		SAFE_FREE(object);
	}
	eaDestroy(&grid->provisional_objects);
}

void exclusionGridCommitProvisionalObjects(HeightMapExcludeGrid *grid)
{
	int i;
	for (i = 0; i < eaSize(&grid->provisional_objects); i++)
	{
		eaiDestroy(&grid->provisional_objects[i]->provisional_positions);
		grid->provisional_objects[i]->ref_count = grid->provisional_objects[i]->provisional_ref_count;
		grid->provisional_objects[i]->provisional_ref_count = 0;
	}
	eaDestroy(&grid->provisional_objects);
}

ExclusionVolume *exclusionCreateVolume(GroupDef *child_def)
{
	if (groupIsVolumeType(child_def, "TerrainExclusion") || child_def->property_structs.terrain_exclusion_properties)
	{
		ExclusionVolume *new_vol = calloc(1, sizeof(ExclusionVolume));
		if (child_def->property_structs.terrain_exclusion_properties)
		{
			WorldTerrainExclusionProperties* properties = child_def->property_structs.terrain_exclusion_properties;
				
			new_vol->is_on_path = (properties->collision_type == WorldTerrainCollisionType_Collide_All_Except_Paths);
			new_vol->collides = (properties->collision_type != WorldTerrainCollisionType_Collide_None);
			new_vol->col_type = properties->collision_type;
			new_vol->above_terrain = (properties->exclusion_type == WorldTerrainExclusionType_Above_Terrain);
			new_vol->below_terrain = (properties->exclusion_type == WorldTerrainExclusionType_Below_Terrain);
			new_vol->platform_type = properties->platform_type;
			new_vol->challenges_only = properties->challenges_only;
		}
		else
		{
			new_vol->is_on_path = false;
			new_vol->collides = true;
			new_vol->above_terrain = false;
			new_vol->below_terrain = false;
			new_vol->platform_type = WorldPlatformType_None;
			new_vol->challenges_only = false;
		}
		
		if (child_def->property_structs.volume && child_def->property_structs.volume->eShape == GVS_Box)
		{
			
			new_vol->type = EXCLUDE_VOLUME_BOX;
			copyVec3(child_def->property_structs.volume->vBoxMin, new_vol->extents[0]);
			copyVec3(child_def->property_structs.volume->vBoxMax, new_vol->extents[1]);
			new_vol->begin_radius = new_vol->end_radius = MAX(lengthVec3(new_vol->extents[0]), lengthVec3(new_vol->extents[1]));
		}
		else if (child_def->property_structs.volume && child_def->property_structs.volume->eShape == GVS_Sphere)
		{
			new_vol->type = EXCLUDE_VOLUME_SPHERE;
			new_vol->begin_radius = new_vol->end_radius = child_def->property_structs.volume->fSphereRadius; 
		}
		else
		{
			SAFE_FREE(new_vol);
			return NULL;
		}
		return new_vol;
	}
	return NULL;
}

//If you change the order of things in this function, you must also change genesisApplyEncouterMats
void exclusionGetDefVolumes(GroupDef *def, ExclusionVolume ***volumes, const Mat4 parent_mat, bool add_encounters, int room_level, ExclusionVolumeGroup ***groups)
{
	int i, j;
	int restrict_child = -1;
	char **tag_list = NULL;
	ExclusionVolume *new_vol;
	DivideString(def->tags, ",", &tag_list, DIVIDESTRING_POSTPROCESS_STRIP_WHITESPACE | DIVIDESTRING_POSTPROCESS_DONT_PUSH_EMPTY_STRINGS);

	if (eaFindString(&tag_list, "UgcPopulateSet") != -1)
	{
		eaDestroyEx(&tag_list, NULL);
		return;
	}
	if (room_level >= 0 && eaFindString(&tag_list, "UgcRoomLevels") != -1)
	{
		restrict_child = room_level;
	}

	new_vol = exclusionCreateVolume(def);
	if (new_vol)
	{
		copyMat4(parent_mat, new_vol->mat);
		eaPush(volumes, new_vol);
	}

	//Make volume groups for any encounters we find
	if(add_encounters && def->property_structs.encounter_properties)
	{
		WorldEncounterProperties *enc_props = def->property_structs.encounter_properties;
		for ( j=0; j < eaSize(&enc_props->eaActors); j++ )
		{
			ExclusionVolumeGroup *new_group = calloc(1, sizeof(ExclusionVolumeGroup));
			ExclusionVolume *enc_vol = calloc(1, sizeof(ExclusionVolume));
			WorldActorProperties *actor = enc_props->eaActors[j];
			Mat4 point_mat;

			new_group->is_actor = true;
			new_group->optional = false;

			//Find Matrix
			createMat3YPR(point_mat, actor->vRot);
			copyVec3(actor->vPos, point_mat[3]); 
			mulMat4(parent_mat, point_mat, new_group->mat_offset);
			new_group->mat_offset[3][1] = 0.0f;

			//Setup properties
			enc_vol->type = EXCLUDE_VOLUME_BOX;
			enc_vol->extents[0][0] = enc_vol->extents[0][2] = -1.0f;
			enc_vol->extents[0][1] = 2.0f;//Little bit off the floor so it can place on ramps
			enc_vol->extents[1][0] = enc_vol->extents[1][2] =  1.0f;
			enc_vol->extents[1][1] = 12.0f;
			enc_vol->begin_radius = 3.0f;
			enc_vol->collides = true;
			enc_vol->is_on_path = true;
			identityMat4(enc_vol->mat);

			eaPush(&new_group->volumes, enc_vol);
			eaPush(groups, new_group);
		}
	}
	for (i = 0; i < eaSize(&def->children); i++)
	{
		GroupChild *child = def->children[i];
		GroupDef *child_def = groupChildGetDef(def, child, false);
		if (child_def && (restrict_child < 0 || restrict_child == i))
		{
			Mat4 child_mat;
			mulMat4(parent_mat, child->mat, child_mat);
			exclusionGetDefVolumes(child_def, volumes, child_mat, add_encounters, room_level, groups);
		}
	}
	eaDestroyEx(&tag_list, NULL);
}

int exclusionGetDefVolumeGroups(GroupDef *def, ExclusionVolumeGroup ***groups, bool add_encounters, int room_level)
{
	int i;
	int num_required = def->property_structs.terrain_properties.fMultiExclusionVolumesRequired;
	if (num_required == 0)
	{
		ExclusionVolumeGroup *group = calloc(1, sizeof(ExclusionVolumeGroup));
		group->idx_in_parent = 0;
		group->optional = false;
		identityMat4(group->mat_offset);
		exclusionGetDefVolumes(def, &group->volumes, unitmat, add_encounters, room_level, groups);
		if (group->volumes || eaSize(groups) > 0)
			eaInsert(groups, group, 0);
		else
			SAFE_FREE(group);
		return 0;
	}
	for (i = 0; i < eaSize(&def->children); i++)
	{
		GroupDef *child_def = groupChildGetDef(def, def->children[i], false);
		if (child_def)
		{
			ExclusionVolumeGroup *group = calloc(1, sizeof(ExclusionVolumeGroup));
			group->idx_in_parent = i;
			group->optional = (i>=num_required);
			copyMat4(def->children[i]->mat, group->mat_offset);
			exclusionGetDefVolumes(child_def, &group->volumes, unitmat, false, room_level, NULL);
			eaPush(groups, group);
		}
	}
	return eaSize(&def->children);
}

bool exclusionVolumesTraverseCallback(ExclusionObject *world_excluders, GroupDef *def, GroupInfo *info, GroupInheritedInfo *inherited_info, bool needs_entry)
{
	ExclusionVolume *new_vol = exclusionCreateVolume(def);
	if (!world_excluders->volume_group)
	{
		world_excluders->volume_group = calloc(1, sizeof(ExclusionVolumeGroup));
	}
	if (new_vol)
	{
		new_vol->is_a_path = true;
		copyMat4(info->world_matrix, new_vol->mat);
		eaPush(&world_excluders->volume_group->volumes, new_vol);
	}
	if (def->property_structs.curve && def->property_structs.curve->terrain_exclusion && def->property_structs.curve->spline.spline_widths)
	{
		SplineCurrentPoint it = { 0 };
		F32 radius = splineGetWidth(&def->property_structs.curve->spline, 0, 0);
		while (splineGetNextPoint(&def->property_structs.curve->spline, &it, MAX(1,radius)) > 0.001f)
		{
			Vec3 out_pos;
			new_vol = calloc(1, sizeof(ExclusionVolume));
			new_vol->is_a_path = true;
			mulVecMat4(it.position, info->curve_matrix, out_pos);
			//printf("At %f,%f,%f\n", out_pos[0],out_pos[1],out_pos[2]);

			identityMat4(new_vol->mat);
			copyVec3(out_pos, new_vol->mat[3]);
			new_vol->type = EXCLUDE_VOLUME_SPHERE;
			new_vol->begin_radius = new_vol->end_radius = radius;
			new_vol->collides = true;
			eaPush(&world_excluders->volume_group->volumes, new_vol);

			radius = splineGetWidth(&def->property_structs.curve->spline, it.index, it.t);
		}
	}
	return true;
}

// Projects upward from point to the top of the volume
static bool exclusionFindTopPoint(ExclusionVolume *volume, Mat4 vol_matrix, Vec3 point, F32 *height_out)
{
	bool found_point = false;
	if (volume->type == EXCLUDE_VOLUME_SPHERE)
	{
		F32 d = SQR(volume->begin_radius) - SQR(point[0]-vol_matrix[3][0]) - SQR(point[2]-vol_matrix[3][2]);
		if (d > 0)
		{
			if( volume->platform_type != WorldPlatformType_Volume_Floor ) {
				(*height_out) = vol_matrix[3][1] + sqrt(d) + 0.05f; // Extra padding (0.05) on top
			} else {
				(*height_out) = vol_matrix[3][1] - (sqrt(d) + 0.05f); // Extra padding (0.05) on top
			}
			found_point = true;
		}
	}
	else if (volume->type == EXCLUDE_VOLUME_BOX)
	{
		int side, side2;
		Vec3 ref_pt;//The point in the center of the side we are checking
		Vec3 normal, delta;
		F32 dp;
		bool outside;
		for (side = 0; side < 3; side++)
		{
			F32 py;
			// Is Y coefficient of this axis positive?

			if( volume->platform_type == WorldPlatformType_Volume_Floor && side != 1 ) {
				continue;
			}
			
			setVec3(normal, vol_matrix[side][0], vol_matrix[side][1], vol_matrix[side][2]);
			if (normal[1] > 0 && volume->platform_type != WorldPlatformType_Volume_Floor)
			{
				// Use max extent
				setVec3(ref_pt, vol_matrix[3][0]+normal[0]*volume->extents[1][side],
					vol_matrix[3][1]+normal[1]*volume->extents[1][side],
					vol_matrix[3][2]+normal[2]*volume->extents[1][side]);
			}
			else if (normal[1] < 0 || volume->platform_type == WorldPlatformType_Volume_Floor)
			{
				// Use min extent
				setVec3(ref_pt, vol_matrix[3][0]+normal[0]*volume->extents[0][side],
					vol_matrix[3][1]+normal[1]*volume->extents[0][side],
					vol_matrix[3][2]+normal[2]*volume->extents[0][side]);
			}
			else
				continue;

			// Projecting point onto the plane
			// Extra padding (0.05) on top
			py = ref_pt[1] - ((point[0]-ref_pt[0])*normal[0] + (point[2]-ref_pt[2])*normal[2])/normal[1] + 0.05f;

			delta[0] = point[0] - ref_pt[0];
			delta[1] = py - ref_pt[1];
			delta[2] = point[2] - ref_pt[2];

			outside = false;
			for (side2 = 0; side2 < 3; side2++)
				if (side != side2)
				{
					dp = delta[0]*vol_matrix[side2][0] + delta[1]*vol_matrix[side2][1] + delta[2]*vol_matrix[side2][2];
					if (dp < volume->extents[0][side2] || dp > volume->extents[1][side2])
					{
						outside = true;
						break;
					}
				}
			if (outside)
				continue;

			if(!found_point || py > (*height_out))
			{
				found_point = true;
				(*height_out) = py;
			}
		}
	}
	return found_point;
}

void exclusionCalculateObjectHeight(HeightMapExcludeGrid *grid, Mat4 item_matrix, F32 max_radius, U64 test_num, bool is_challenge, int iPartitionIdx, F32 **ret_heights, F32 **ret_normals)
{
	int j, k, x, z;
	IVec2 min = { (int)(item_matrix[3][0]-grid->min[0]-max_radius)/GRID_SPACING, (int)(item_matrix[3][2]-grid->min[1]-max_radius)/GRID_SPACING };
	IVec2 max = { (int)(item_matrix[3][0]-grid->min[0]+max_radius)/GRID_SPACING, (int)(item_matrix[3][2]-grid->min[1]+max_radius)/GRID_SPACING };
	F32 maxBlockingY = -FLT_MAX;
	min[0] = CLAMP(min[0], 0, grid->width-1);
	min[1] = CLAMP(min[1], 0, grid->height-1);
	max[0] = CLAMP(max[0], 0, grid->width-1);
	max[1] = CLAMP(max[1], 0, grid->height-1);

	for (x = min[0]; x <= max[0]; x++)
	{
		for (z = min[1]; z <= max[1]; z++)
		{
			for (j = 0; j < eaSize(&grid->elements[x+z*grid->width].excluders); j++)
			{
				ExclusionObject *exclude = grid->elements[x+z*grid->width].excluders[j];
				if (exclude->test_number_volume != test_num)
				{
					exclude->test_number_volume = test_num;
					if (distance3XZ(item_matrix[3], exclude->mat[3]) > max_radius + exclude->max_radius)
						continue;

					for (k = 0; k < eaSize(&exclude->volume_group->volumes); k++)
					{
						ExclusionVolume *exclusion_volume = exclude->volume_group->volumes[k];
						if (exclusion_volume->platform_type && (is_challenge || !exclusion_volume->challenges_only))
						{
							F32 height=0;
							Mat4 exclude_volume_mat;
							mulMat4(exclude->mat, exclusion_volume->mat, exclude_volume_mat);
							if(exclusionFindTopPoint(exclusion_volume, exclude_volume_mat, item_matrix[3], &height)) {
								if( exclusion_volume->platform_type == WorldPlatformType_InsideCollidable ) {
									// not actually doing a collision, just want to know if its valid
									if( iPartitionIdx == -1 ) {
										eafPush( ret_heights, EXCLUSION_TERRAIN_HEIGHT );
									} else {
										Vec3 source;
										Vec3 target;
										WorldCollCollideResults results = { 0 };
										copyVec3( item_matrix[3], source );
										copyVec3( item_matrix[3], target );
										source[1] = height;
										target[1] = -9001;

										wcRayCollide( worldGetActiveColl( iPartitionIdx ), source, target, WC_FILTER_BIT_WORLD_NORMAL | WC_FILTER_BIT_TERRAIN, &results );
										if( results.hitSomething ) {
											eafPush( ret_heights, results.posWorldImpact[ 1 ]);
											if(ret_normals)
											{
												eafPush( ret_normals, results.normalWorld[0] );
												eafPush( ret_normals, results.normalWorld[1] );
												eafPush( ret_normals, results.normalWorld[2] );
											}
										}
									}
								} else if( exclusion_volume->platform_type == WorldPlatformType_BlockingVolume_AllSides ) {
									maxBlockingY = MAX( maxBlockingY, height );
								} else {
									eafPush(ret_heights, height);
								}
							}
						}
					}
				}
			}
		}
	}

	for(j = eafSize(ret_heights) - 1; j >= 0; --j) {
		if( (*ret_heights)[j] <= maxBlockingY ) {
			eafRemove(ret_heights, j);
		}
	}
}

// Returns the topmost, height which should be a sensible default
F32 exclusionCalculateObjectHeightDefault(HeightMapExcludeGrid *grid, Mat4 item_matrix, F32 max_radius, U64 test_num, bool is_challenge, int iPartitionIdx)
{
	F32* positions = NULL;
	exclusionCalculateObjectHeight(grid, item_matrix, max_radius, test_num, is_challenge, iPartitionIdx, &positions, NULL);
			
	if(eafSize(&positions) > 0)
	{
		int j;
		F32 topmost = positions[0];
		for ( j=1; j < eafSize(&positions); j++ )
		{
			if(positions[j] > topmost)
				topmost = positions[j];
		}

		return topmost;
	}
	else
	{
		return item_matrix[3][1];
	}
}

//Finds the nearest point to start on the line from start to end
F32 exclusionCollideLine(HeightMapExcludeGrid *grid, const Vec3 start, const Vec3 end, U8 flags, int plane_id, U64 test_num, Vec3 nearest_point, ExclusionObject** out_nearest_object)
{
	int j, k, x, z;
	F32 nearest_dist;
	Vec3 line_min, line_max;
	IVec2 min, max;
	ExclusionObject* dummy;
	Vec3 dummyVec3;
	MINVEC3(start, end, line_min);
	MAXVEC3(start, end, line_max);
	min[0] = (int)(line_min[0] - grid->min[0] - 1.0f)/GRID_SPACING; //Adding 1ft buffer room just make sure
	min[1] = (int)(line_min[2] - grid->min[1] - 1.0f)/GRID_SPACING;
	max[0] = (int)(line_max[0] - grid->min[0] + 1.0f)/GRID_SPACING;
	max[1] = (int)(line_max[2] - grid->min[1] + 1.0f)/GRID_SPACING;
	min[0] = CLAMP(min[0], 0, grid->width-1);
	min[1] = CLAMP(min[1], 0, grid->height-1);
	max[0] = CLAMP(max[0], 0, grid->width-1);
	max[1] = CLAMP(max[1], 0, grid->height-1);

	if( !nearest_point ) {
		nearest_point = dummyVec3;
	}
	if( !out_nearest_object ) {
		out_nearest_object = &dummy;
	}

	nearest_dist = distance3(start, end);
	*out_nearest_object = NULL;
	copyVec3(end, nearest_point);

	for (x = min[0]; x <= max[0]; x++)
	{
		for (z = min[1]; z <= max[1]; z++)
		{
			for (j = 0; j < eaSize(&grid->elements[x+z*grid->width].excluders); j++)
			{
				ExclusionObject *exclude = grid->elements[x+z*grid->width].excluders[j];
				if (exclude->test_number_volume != test_num)
				{
					exclude->test_number_volume = test_num;
					if (flags && !(exclude->flags & flags))
						continue;
					if (!exclusionGridIsIntersectingPlanes(exclude->plane_id, plane_id))
						continue;

					for (k = 0; k < eaSize(&exclude->volume_group->volumes); k++)
					{
						ExclusionVolume *exclusion_volume = exclude->volume_group->volumes[k];
						if (exclusion_volume->collides && exclusion_volume->col_type < WorldTerrainCollisionType_Collide_SimilarTypes)
						{
							Vec3 collided_point;
							bool collided = false;

							if (exclusion_volume->type == EXCLUDE_VOLUME_SPHERE)
							{
							}
							else if (exclusion_volume->type == EXCLUDE_VOLUME_BOX)
							{
								Mat4 vol_mat, vol_mat_inv;
								mulMat4(exclude->mat, exclusion_volume->mat, vol_mat);
								invertMat4(vol_mat, vol_mat_inv);		
								collided = lineOrientedBoxCollision(start, end, vol_mat, vol_mat_inv, exclusion_volume->extents[0], exclusion_volume->extents[1], collided_point); 
							}

							if(collided)
							{
								F32 dist = distance3(start, collided_point);
								if(dist < nearest_dist)
								{
									nearest_dist = dist;
									*out_nearest_object = exclude;
									copyVec3(collided_point, nearest_point);
								}
							}
						}
					}
				}
			}
		}
	}
	return nearest_dist;
}

static bool exclusionGridVolumesCollision(ExclusionVolume *volume_1, Mat4 mat_1, ExclusionVolume *volume_2, Mat4 mat_2)
{
	bool ret = false;
	bool same_type = true;
	int ss_cnt = 0;
	int sb_cnt = 0;
	int bb_cnt = 0;

	if(	volume_1->col_type > WorldTerrainCollisionType_Collide_SimilarTypes ||
		volume_2->col_type > WorldTerrainCollisionType_Collide_SimilarTypes)
	{
		same_type = (volume_2->col_type == volume_1->col_type);
	}

	if (volume_2->collides && same_type && (!volume_1->is_on_path || !volume_2->is_a_path))
	{
		// Do fast sphere-sphere collision first
		ss_cnt++;
		if (distance3(mat_1[3], mat_2[3]) < volume_1->begin_radius + volume_2->begin_radius)
		{
			if (volume_2->type == EXCLUDE_VOLUME_SPHERE &&
				volume_1->type == EXCLUDE_VOLUME_SPHERE)
			{
				// Sphere-Sphere collision
				// We already failed our sphere-sphere check
				ret = true;
			}
			else if (	volume_2->type == EXCLUDE_VOLUME_BOX &&
						volume_1->type == EXCLUDE_VOLUME_BOX)
			{
				// Box-Box collision
				bb_cnt++;
				if (orientBoxBoxCollision(volume_2->extents[0], volume_2->extents[1], mat_2, volume_1->extents[0], volume_1->extents[1], mat_1))
					ret = true;
			}
			else if (	volume_2->type == EXCLUDE_VOLUME_BOX &&
						volume_1->type == EXCLUDE_VOLUME_SPHERE)
			{
				// Box-Sphere collision
				Mat4 mat_2_inv;
				Vec3 sphere_rel_pos;

				sb_cnt++;
				invertMat4Copy(mat_2, mat_2_inv);
				// Sphere center -> box relative space
				mulVecMat4(mat_1[3], mat_2_inv, sphere_rel_pos);
				if (boxSphereCollision(volume_2->extents[0], volume_2->extents[1], sphere_rel_pos, volume_1->begin_radius))
					ret = true;
			}
			else if (	volume_2->type == EXCLUDE_VOLUME_SPHERE &&
						volume_1->type == EXCLUDE_VOLUME_BOX)
			{
				// Sphere-Box collision
				Mat4 mat_1_inv;
				Vec3 sphere_rel_pos;

				sb_cnt++;
				invertMat4Copy(mat_1, mat_1_inv);

				// Sphere center -> box relative space
				mulVecMat4(mat_2[3], mat_1_inv, sphere_rel_pos);
				if (boxSphereCollision(volume_1->extents[0], volume_1->extents[1], sphere_rel_pos, volume_2->begin_radius))
					ret = true;
			}
		}
	}

#ifdef EXCLUSION_DEBUG
	stats[exclude->group_id].exclusion_debug_sphere_sphere_count += ss_cnt;
	stats[exclude->group_id].exclusion_debug_sphere_box_count += sb_cnt;
	stats[exclude->group_id].exclusion_debug_box_box_count += bb_cnt;
#endif

	return ret;
}

bool exclusionGridVolumeGroupCollision(ExclusionVolume **volumes_1, Mat4 vols_parent_mat_1, ExclusionVolume **volumes_2, Mat4 vols_parent_mat_2)
{
	int i, j;
	for ( i=0; i < eaSize(&volumes_1); i++ )
	{
		Mat4 vol_mat_1;
		mulMat4(vols_parent_mat_1, volumes_1[i]->mat, vol_mat_1);
		for ( j=0; j < eaSize(&volumes_2); j++ )
		{
			Mat4 vol_mat_2;
			mulMat4(vols_parent_mat_2, volumes_2[j]->mat, vol_mat_2);
			if(exclusionGridVolumesCollision(volumes_1[i], vol_mat_1, volumes_2[j], vol_mat_2))
				return true;
		}
	}
	return false;
}

bool exclusionGridVolumeGridCollision(HeightMapExcludeGrid *grid, Mat4 item_matrix, F32 max_radius, ExclusionVolume **volumes, int plane_id, HeightMapExcludeStat *stats, U64 test_num, U32 *collide_id)
{
	int i, j, k, x, z;
	IVec2 min = { (int)(item_matrix[3][0]-grid->min[0]-max_radius)/GRID_SPACING, (int)(item_matrix[3][2]-grid->min[1]-max_radius)/GRID_SPACING };
	IVec2 max = { (int)(item_matrix[3][0]-grid->min[0]+max_radius)/GRID_SPACING, (int)(item_matrix[3][2]-grid->min[1]+max_radius)/GRID_SPACING };
	min[0] = CLAMP(min[0], 0, grid->width-1);
	min[1] = CLAMP(min[1], 0, grid->height-1);
	max[0] = CLAMP(max[0], 0, grid->width-1);
	max[1] = CLAMP(max[1], 0, grid->height-1);

	for (x = min[0]; x <= max[0]; x++)
	{
		for (z = min[1]; z <= max[1]; z++)
		{
			for (j = 0; j < eaSize(&grid->elements[x+z*grid->width].excluders); j++)
			{
				ExclusionObject *exclude = grid->elements[x+z*grid->width].excluders[j];
				if (exclude->test_number_volume != test_num)
				{
					exclude->test_number_volume = test_num;
					if (distance3XZ(item_matrix[3], exclude->mat[3]) > max_radius + exclude->max_radius)
						continue;
					if (!exclusionGridIsIntersectingPlanes(exclude->plane_id, plane_id))
						continue;

					for (i = 0; i < eaSize(&volumes); i++)
					{
						if (volumes[i]->collides)
						{
							Mat4 item_mat;
							mulMat4(item_matrix, volumes[i]->mat, item_mat);
							for (k = 0; k < eaSize(&exclude->volume_group->volumes); k++)
							{
								ExclusionVolume *exclusion_volume = exclude->volume_group->volumes[k];
								Mat4 exclude_volume_mat;
								mulMat4(exclude->mat, exclusion_volume->mat, exclude_volume_mat);

								if(exclusionGridVolumesCollision(volumes[i], item_mat, exclusion_volume, exclude_volume_mat))
								{
									if (collide_id)
										*collide_id = exclude->id;
									return true;								
								}
							}
						}
					}
				}
			}
		}
	}
	return false;
}

void exclusionVolumeGroupDestroy(ExclusionVolumeGroup *group)
{
	eaDestroyEx(&group->volumes, NULL);
	SAFE_FREE(group);
}

bool exclusionCheckVolumesInRange(ExclusionVolumeGroup *group, Mat4 item_matrix, F32 min_x, F32 min_z, F32 width, F32 height)
{
	int i;
	for (i = 0; i < eaSize(&group->volumes); i++)
	{
		ExclusionVolume *volume = group->volumes[i];
		if (volume->collides)
		{
			if (volume->type == EXCLUDE_VOLUME_SPHERE)
			{
				Vec3 center;
				addVec3(item_matrix[3], volume->mat[3], center);
				if (center[0] < min_x+volume->begin_radius ||
						center[2] < min_z+volume->begin_radius ||
						center[0] > (min_x+width-volume->begin_radius) ||
						center[2] > (min_z+height-volume->begin_radius))
					return false;
			}
			else if (volume->type == EXCLUDE_VOLUME_BOX)
			{
				Mat4 vol_mat;
				int x, y, z;
				mulMat4(item_matrix, volume->mat, vol_mat);
				for (x = 0; x < 2; ++x)
				{
					F32 val_x = volume->extents[x][0];
					Vec3 pos_x = { vol_mat[0][0]*val_x, vol_mat[1][0]*val_x, vol_mat[2][0]*val_x };
					for (y = 0; y < 2; ++y)
					{
						F32 val_y = volume->extents[y][1];
						Vec3 pos_y = { pos_x[0]+vol_mat[0][1]*val_y, pos_x[1]+vol_mat[1][1]*val_y, pos_x[2]+vol_mat[2][1]*val_y };
						for (z = 0; z < 2; ++z)
						{
							F32 val_z = volume->extents[z][2];
							Vec3 pos = { pos_y[0]+vol_mat[0][2]*val_z + vol_mat[3][0], pos_y[1]+vol_mat[1][2]*val_z + vol_mat[3][1], pos_y[2]+vol_mat[2][2]*val_z + vol_mat[3][2] };
							if (pos[0] < min_x ||
									pos[2] < min_z ||
									pos[0] > (min_x+width) ||
									pos[2] > (min_z+height))
								return false;
						}
					}
				}
			}
		}
	}
	return true;
}

#include "wlExclusionGrid_h_ast.c"
