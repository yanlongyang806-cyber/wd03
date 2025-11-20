#include "wlDebrisField.h"

#include "error.h"
#include "group.h"
#include "groupProperties.h"
#include "ObjectLibrary.h"
#include "rand.h"
#include "WorldGrid.h"

AUTO_RUN_ANON(memBudgetAddMapping(__FILE__, BUDGET_World););

#define MAX_DEBRIS_OBJECTS 100000
#define DENSITY_MULTI 7.4506e-9 // 1.0f/CUBE(512)
#define DEBRIS_CUT_SIZE 256.0f

static bool wlDebrisFieldBoxInsideSphere(const Vec3 min1, const Vec3 max1, const Vec3 mid2, F32 radius2)
{
	Vec3 mid1;
	F32 radius1;

	radius1 = distance3(min1, max1) * 0.5f;
	centerVec3(min1, max1, mid1);
	if(distance3(mid1, mid2) < radius2 - radius1)
		return true;
	return false;
}

static bool wlDebrisFieldBoxInsideEllipsode(const Vec3 min1, const Vec3 max1, const Vec3 mid2, F32 radius2, Vec3 scale2)
{
	Vec3 min, max, mid;
	mulVecVec3(min1, scale2, min);
	mulVecVec3(max1, scale2, max);
	mulVecVec3(mid2, scale2, mid);
	return wlDebrisFieldBoxInsideSphere(min, max, mid, radius2);
}

static bool wlDebrisFieldBoxEllipsodeCollision(const Vec3 min1, const Vec3 max1, const Vec3 mid2, F32 radius2, Vec3 scale2)
{
	Vec3 min, max, mid;
	mulVecVec3(min1, scale2, min);
	mulVecVec3(max1, scale2, max);
	mulVecVec3(mid2, scale2, mid);
	return boxSphereCollision(min, max, mid, radius2);
}

static void wlDebrisFieldFindPoints(Vec3 min, Vec3 size, F32 coll_rad, F32 radius, Vec3 scale, F32 density, F32 *occluders, Vec3 *position_list, U32 *objects_placed, U32 max_objects, MersenneTable *random_table, WorldDebrisFieldProperties *properties, F32 even_distb_dist)
{
	bool collided;
	U32 i, j, k, num_to_place;

	if(*objects_placed >= max_objects)
		return;

	//If we have not gotten too small
	if(size[0] > DEBRIS_CUT_SIZE && size[1] > DEBRIS_CUT_SIZE && size[2] > DEBRIS_CUT_SIZE)
	{
		U32 num_occluders = eafSize(&occluders)/4;
		Vec3 position_2 = {0,0,0};
		Vec3 max;
		addVec3(min, size, max);
		//If completely outside the placement radius
		if(!properties->is_box && !wlDebrisFieldBoxEllipsodeCollision(min, max, position_2, radius, scale))
			return;
		collided = false;
		for( i=0; i < num_occluders; i++ )
		{
			//If completely inside an occluder
			if(wlDebrisFieldBoxInsideSphere(min, max, (occluders + i*4), occluders[i*4 + 3]))
				return;
			//If touching an occluder
			if(boxSphereCollision(min, max, (occluders + i*4), occluders[i*4 + 3]))
			{
				collided = true;
				break;
			}
		}

		//If not completely inside the placement radius
		if(collided || (!properties->is_box && !wlDebrisFieldBoxInsideEllipsode(min, max, position_2, radius, scale)))
		{
			Vec3 new_size;
			scaleVec3(size, 0.5f, new_size);
			copyVec3(min, position_2);
			wlDebrisFieldFindPoints(position_2, new_size, coll_rad, radius, scale, density, occluders, position_list, objects_placed, max_objects, random_table, properties, even_distb_dist);
			position_2[0] += new_size[0];
			wlDebrisFieldFindPoints(position_2, new_size, coll_rad, radius, scale, density, occluders, position_list, objects_placed, max_objects, random_table, properties, even_distb_dist);
			position_2[1] += new_size[1];
			wlDebrisFieldFindPoints(position_2, new_size, coll_rad, radius, scale, density, occluders, position_list, objects_placed, max_objects, random_table, properties, even_distb_dist);
			position_2[2] += new_size[2];
			wlDebrisFieldFindPoints(position_2, new_size, coll_rad, radius, scale, density, occluders, position_list, objects_placed, max_objects, random_table, properties, even_distb_dist);
			position_2[0] -= new_size[0];
			wlDebrisFieldFindPoints(position_2, new_size, coll_rad, radius, scale, density, occluders, position_list, objects_placed, max_objects, random_table, properties, even_distb_dist);
			position_2[1] -= new_size[1];
			wlDebrisFieldFindPoints(position_2, new_size, coll_rad, radius, scale, density, occluders, position_list, objects_placed, max_objects, random_table, properties, even_distb_dist);
			position_2[0] += new_size[0];
			wlDebrisFieldFindPoints(position_2, new_size, coll_rad, radius, scale, density, occluders, position_list, objects_placed, max_objects, random_table, properties, even_distb_dist);
			position_2[0] = min[0];
			position_2[1] = min[1] + new_size[1];
			position_2[2] = min[2];
			wlDebrisFieldFindPoints(position_2, new_size, coll_rad, radius, scale, density, occluders, position_list, objects_placed, max_objects, random_table, properties, even_distb_dist);
			return;
		}
	}
	//Place objects
	if(properties->even_distb)
	{
		F32 fx, fy, fz;
		Vec3 start, end;
		start[0] = ceilf(min[0]/even_distb_dist)*even_distb_dist;
		start[1] = ceilf(min[1]/even_distb_dist)*even_distb_dist;
		start[2] = ceilf(min[2]/even_distb_dist)*even_distb_dist;
		end[0] = floorf((min[0]+size[0]-0.000000001)/even_distb_dist)*even_distb_dist + even_distb_dist/2.0f;
		end[1] = floorf((min[1]+size[1]-0.000000001)/even_distb_dist)*even_distb_dist + even_distb_dist/2.0f;
		end[2] = floorf((min[2]+size[2]-0.000000001)/even_distb_dist)*even_distb_dist + even_distb_dist/2.0f;
		if(start[0] > end[0] || start[1] > end[1] || start[2] > end[2])
			return;

		for( fx=start[0]; fx <= end[0]; fx+=even_distb_dist )
		{
			for( fy=start[1]; fy <= end[1]; fy+=even_distb_dist )
			{
				for( fz=start[2]; fz <= end[2]; fz+=even_distb_dist )
				{
					position_list[*objects_placed][0] = fx;
					position_list[*objects_placed][1] = fy;
					position_list[*objects_placed][2] = fz;
					(*objects_placed)++;
					if(*objects_placed >= max_objects)
						return;
				}
			}
		}
	}
	else
	{
		num_to_place = density*size[0]*size[1]*size[2] + (randomMersenneF32(random_table)+1.0f)/2.0f;
		for( i=0; i < num_to_place; i++ )
		{
			for( j=0; j < 100; j++ )
			{
				position_list[*objects_placed][0] = min[0] + size[0]*(randomMersenneF32(random_table)+1.0f)/2.0f;
				position_list[*objects_placed][1] = min[1] + size[1]*(randomMersenneF32(random_table)+1.0f)/2.0f;
				position_list[*objects_placed][2] = min[2] + size[2]*(randomMersenneF32(random_table)+1.0f)/2.0f;
				collided = false;
				for( k=0; k < (*objects_placed); k++ )
				{
					if(sphereSphereCollision(position_list[k], coll_rad, position_list[*objects_placed], coll_rad))
					{
						collided = true;
						break;
					}
				}
				if(!collided)
					break;
			}
			if(collided)
				break;
			(*objects_placed)++;
			if(*objects_placed >= max_objects)
				return;
		}
	}
}

U32 wlDebrisFieldFindLocations(WorldDebrisFieldProperties *properties, WorldDebrisFieldGenProps *gen_props, GroupDef *def, F32 *vol_occluders, const Mat4 world_mat, U32 seed, F32 coll_rad)
{
	int i;
	U32 j;
	F32 radius;
	Vec3 scale;
	Vec3 start_pos, start_size;
	U32 objects_placed=0, max_objects;
	F32 even_distb_dist;
	F32 *occluders=NULL;
	Mat3 trans_world_mat;

	copyMat3(world_mat, trans_world_mat);
	transposeMat3(trans_world_mat);

	if(eafSize(&properties->occluders)%4 != 0)
	{
		Errorf("Occluders array of wrong size");
		return 0;
	}
	if(vol_occluders)
		eafCopy(&occluders, &vol_occluders);
	for( i=0; i < eafSize(&occluders)/4 ; i++ )
	{
		Vec3 temp_vec;
		subVec3(occluders + i*4, world_mat[3], temp_vec);
		mulVecMat3(temp_vec, trans_world_mat, occluders + i*4);
	}
	for( i=0; i < eafSize(&properties->occluders); i++)
	{
		eafPush(&occluders, properties->occluders[i]);
	}
	assert(eafSize(&occluders)%4 == 0);

	if(properties->density == 0.0f)
		return 0;

	if(def->property_structs.volume && def->property_structs.volume->eShape == GVS_Sphere)
	{
		radius = def->property_structs.volume->fSphereRadius;
		setVec3same(gen_props->center, 0.0f);
		setVec3same(scale, 1.0f);
	}
	else
	{
		if(def->property_structs.volume && def->property_structs.volume->eShape == GVS_Box)
		{
			Vec3 min_max[2];
			copyVec3(def->property_structs.volume->vBoxMin, min_max[0]);
			copyVec3(def->property_structs.volume->vBoxMax, min_max[1]);
			radius = min_max[1][0] - min_max[0][0];
			for( i=0; i < 3; i++ )
			{
				scale[i] = min_max[1][i] - min_max[0][i];
				gen_props->center[i] = min_max[0][i] + scale[i]/2.0f;
				if(scale[i] < radius)
					radius = scale[i];
			}
			for( i=0; i < 3; i++ )
			{
				scale[i] /= radius;
			}
			radius /= 2.0f;
		}
		else
		{
			return 0;
		}
	}

	if(radius <= 0.0f)
		return 0;

	gen_props->random_table = mersenneTableCreate(seed);

	for( i=0; i < eafSize(&occluders)/4 ; i++ )
	{
		subVec3(occluders + i*4, gen_props->center, occluders + i*4);
	}

	setVec3same(start_pos, -radius);
	setVec3same(start_size, radius*2);
	mulVecVec3(start_pos, scale, start_pos);
	mulVecVec3(start_size, scale, start_size);
	if(properties->density*DENSITY_MULTI*start_size[0]*start_size[1]*start_size[2] > MAX_DEBRIS_OBJECTS)
	{
		max_objects = MAX_DEBRIS_OBJECTS;
		properties->density = 0.9f*(MAX_DEBRIS_OBJECTS/(start_size[0]*start_size[1]*start_size[2]))/DENSITY_MULTI;
	}
	else 
	{
		max_objects = properties->density*DENSITY_MULTI*start_size[0]*start_size[1]*start_size[2]*1.1;
	}

	gen_props->position_list = calloc(sizeof(Vec3), max_objects);
	scale[0] = 1.0f/scale[0];
	scale[1] = 1.0f/scale[1];
	scale[2] = 1.0f/scale[2];
	even_distb_dist = pow(1.0f/(properties->density*DENSITY_MULTI), 1.0f/3.0f);
	wlDebrisFieldFindPoints(start_pos, start_size, coll_rad, radius, scale, properties->density*DENSITY_MULTI, occluders, gen_props->position_list, &objects_placed, max_objects, gen_props->random_table, properties, even_distb_dist);
	assert(objects_placed <= max_objects);

	// remove objects based off of falloff and collision
	for( j=0; j < objects_placed; j++ )
	{
		Vec3 scaled_pos;
		F32 dist;
		bool collided = false;
		for( i=0; i < eafSize(&occluders)/4; i++ )
		{
			//If touching an occluder
			if(sphereSphereCollision(gen_props->position_list[j], coll_rad, (occluders + i*4), occluders[i*4 + 3]))
			{
				collided = true;
				break;
			}
		}
		if(properties->center_occluder && sphereSphereCollision(gen_props->position_list[j], coll_rad, zerovec3, properties->center_occluder))
			collided = true;
		if(!collided)
		{
			if(properties->is_box)
				continue;
			mulVecVec3(gen_props->position_list[j], scale, scaled_pos);
			dist = lengthVec3(scaled_pos) / radius;
			if(dist < properties->falloff)
				continue;
			if(dist < 1.0f)
			{
				F32 rand_val = (randomMersenneF32(gen_props->random_table)+1.0f)/2.0f;
				dist = (dist-properties->falloff)/(1.0f-properties->falloff);
				if(dist < CUBE(rand_val))
					continue;
			}
		}
		objects_placed--;
		copyVec3(gen_props->position_list[objects_placed], gen_props->position_list[j]);
		j--;
	}

	return objects_placed;
}

U32 wlDebrisFieldGetLocationAndSeed(WorldDebrisFieldProperties *properties, WorldDebrisFieldGenProps *gen_props, int item, Mat4 ent_mat)
{
	U32 ent_seed = randomMersenneU32(gen_props->random_table);
	copyMat4(unitmat, ent_mat);
	if(properties->rotate)
	{
		pitchMat3(randomMersenneF32(gen_props->random_table)*PI, ent_mat);
		yawMat3(randomMersenneF32(gen_props->random_table)*PI, ent_mat);
		rollMat3(randomMersenneF32(gen_props->random_table)*PI, ent_mat);
	}
	addVec3(gen_props->position_list[item], gen_props->center, ent_mat[3]);
	if(properties->even_distb && properties->rand_offset != 0.0f)
	{
		ent_mat[3][0] += randomMersenneF32(gen_props->random_table)*properties->rand_offset;
		ent_mat[3][1] += randomMersenneF32(gen_props->random_table)*properties->rand_offset;
		ent_mat[3][2] += randomMersenneF32(gen_props->random_table)*properties->rand_offset;
	}
	return ent_seed;
}

void wlDebrisFieldCleanUp(WorldDebrisFieldGenProps *gen_props)
{
	SAFE_FREE(gen_props->position_list);
	if(gen_props->random_table)
		mersenneTableFree(gen_props->random_table);
}

F32 wlDebrisFieldFindColRad(GroupDef *parent, WorldDebrisFieldProperties *properties, GroupDef **fallback_def /*out*/)
{
	int i;
	F32 coll_rad = 1.0f;
	GroupChild **children = groupGetChildren(parent);

	(*fallback_def) = GET_REF(properties->group_ref);
	if(*fallback_def)
		groupSetBounds(*fallback_def, true);

	if(eaSize(&children) > 0)
	{
		for( i=0; i < eaSize(&children); i++ )
		{
			GroupDef *child_def = groupChildGetDef(parent, children[i], false);
			if(child_def)
				coll_rad = MAX(child_def->bounds.radius, coll_rad);
		}
	}
	else if((*fallback_def))
	{
		coll_rad = MAX((*fallback_def)->bounds.radius, coll_rad);
	}
	return coll_rad;
}

bool wlDebrisFieldTraversePre(void *user_data, GroupDef *def, GroupInfo *info, GroupInheritedInfo *inherited_info, bool needs_entry)
{
	//Don't search inside debris fields for excluder volumes
	if(def->property_structs.debris_field_properties)
		return false;
	return true;
}

bool wlDebrisFieldTraversePost(F32 **debris_excluders, GroupDef *def, GroupInfo *info, GroupInheritedInfo *inherited_info, bool needs_entry)
{
	if((def->property_structs.volume && !def->property_structs.volume->bSubVolume) && groupIsVolumeType(def, "DebrisFieldExclusion"))
	{
		if(def->property_structs.volume && def->property_structs.volume->eShape == GVS_Sphere)
		{
			F32 rad = def->property_structs.volume->fSphereRadius;
			if(rad > 0)
			{
				eafPush(debris_excluders, info->world_matrix[3][0]);
				eafPush(debris_excluders, info->world_matrix[3][1]);
				eafPush(debris_excluders, info->world_matrix[3][2]);
				eafPush(debris_excluders, rad);
			}
		}
	}
	return true;
}