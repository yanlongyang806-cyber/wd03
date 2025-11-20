#define GENESIS_ALLOW_OLD_HEADERS
#include "wlTerrainSource.h"
#include "wlTerrainBrush.h"
#include "wlGenesis.h"
#include "wlGenesisExterior.h"
#include "wlGenesisPopulate.h"
#include "wlGenesisRoom.h"
#include "wlGenesisMissions.h"
#include "wlGenesisExteriorNode.h"
#include "wlGenesisExteriorDesign.h"
#include "wlModelInline.h"
#include "LineDist.h"

#include "Expression.h"
#include "error.h"
#include "WorldGrid.h"
#include "ObjectLibrary.h"
#include "ScratchStack.h"
#include "StringCache.h"

AUTO_RUN_ANON(memBudgetAddMapping(__FILE__, BUDGET_World););

#ifndef NO_EDITORS

#define CLEARING_FALLOFF 0.9
#define ROAD_CLEARING_FALLOFF 0.9

void genesisPathNodeGetPosition(GenesisNodeConnection *conn, F32 offset, Vec3 out_pos, Vec3 out_up, Vec3 out_tan)
{
	Vec3 in = { 0, 0, 0 };
	Mat4 parent_mat, matrices[2];
	int path_length = eafSize(&conn->path->spline_points)/3-1;
	S32 i = offset * path_length;
	F32 t = offset * path_length - i;
	identityMat4(parent_mat);
	splineGetMatrices(parent_mat, zerovec3, 1.f, conn->path, i*3, matrices, false);
	splineEvaluate(matrices, t, in, out_pos, out_up, out_tan);
}

GenesisNode* genesisGetNodeFromUID(GenesisZoneNodeLayout *layout, U32 uid)
{
	int i;
	for( i=0; i < eaSize(&layout->nodes); i++ )
	{
		if(layout->nodes[i]->uid == uid)
			return layout->nodes[i];
	}
	return NULL;
}

static int genesisPathPointCompare(const GenesisNodePathPoint **point_1, const GenesisNodePathPoint **point_2)
{
	return ((*point_1)->offset < (*point_2)->offset) ? -1 : (((*point_1)->offset > (*point_2)->offset) ? 1 : 0);
}

void genesisMakeDefindedConnection(GenesisNodeConnection *node_connection, GenesisNode *node_start, GenesisNode *node_end)
{
	int i;
	Vec3 dir;
	Vec3 road_vec;
	Vec2 road_dir;
	Vec2 road_dir_orth;
	Vec3 abs_pos;
	Vec3 next_abs_pos;
	Vec3 start_pos, end_pos;
	GenesisNodePathPoint *point = NULL;
	GenesisNodePathPoint *next_point = NULL;
	if(!node_start || !node_end)
		return;

	if(node_connection->path)
		StructDestroy(parse_Spline, node_connection->path);

	//Sort
	eaQSort(node_connection->path_points, genesisPathPointCompare);
	//Insert start point
	if(node_connection->path_points[0]->offset != 0.0)
	{
		GenesisNodePathPoint *new_point = StructCreate(parse_GenesisNodePathPoint);
		new_point->offset = 0.0f;
		new_point->radius = node_connection->radius;
		setVec3same(new_point->rel_pos, 0);
		eaInsert(&node_connection->path_points, new_point, 0);
	}
	//Insert end point
	if(node_connection->path_points[eaSize(&node_connection->path_points)-1]->offset != 1.0)
	{
		GenesisNodePathPoint *new_point = StructCreate(parse_GenesisNodePathPoint);
		new_point->offset = 1.0f;
		new_point->radius = node_connection->radius;
		setVec3same(new_point->rel_pos, 0);
		eaPush(&node_connection->path_points, new_point);
	}
	//Convert to Spline
	node_connection->path = StructCreate(parse_Spline);
	subVec3(node_end->pos, node_start->pos, road_vec);
	road_dir[0] = road_vec[0];
	road_dir[1] = road_vec[2];
	normalVec2(road_dir);
	road_dir_orth[0] = road_dir[1];
	road_dir_orth[1] = -road_dir[0];

	normalVec3(road_vec);
	copyVec3(road_vec, start_pos);
	scaleVec3(start_pos, node_start->draw_size, start_pos);
	addVec3(node_start->pos, start_pos, start_pos);
	start_pos[1] = node_start->pos[1];
	copyVec3(road_vec, end_pos);
	scaleVec3(end_pos, node_end->draw_size, end_pos);
	subVec3(node_end->pos, end_pos, end_pos);
	end_pos[1] = node_end->pos[1];
	subVec3(end_pos, start_pos, road_vec);

	for( i=0; i < eaSize(&node_connection->path_points)-1; i++ )
	{
		Vec3 offset;
		point = node_connection->path_points[i];
		next_point = node_connection->path_points[i+1];

		scaleVec3(road_vec, point->offset, abs_pos);
		addVec3(abs_pos, start_pos, abs_pos);
		offset[0] = point->rel_pos[0]*road_dir_orth[0] + point->rel_pos[2]*road_dir[0];
		offset[2] = point->rel_pos[0]*road_dir_orth[1] + point->rel_pos[2]*road_dir[1];
		offset[1] = point->rel_pos[1];
		addVec3(abs_pos, offset, abs_pos);

		scaleVec3(road_vec, next_point->offset, next_abs_pos);
		addVec3(next_abs_pos, start_pos, next_abs_pos);
		offset[0] = next_point->rel_pos[0]*road_dir_orth[0] + next_point->rel_pos[2]*road_dir[0];
		offset[2] = next_point->rel_pos[0]*road_dir_orth[1] + next_point->rel_pos[2]*road_dir[1];
		offset[1] = next_point->rel_pos[1];
		addVec3(next_abs_pos, offset, next_abs_pos);

		subVec3(next_abs_pos, abs_pos, dir);
		normalVec3(dir);

		splineAppendCP(node_connection->path, abs_pos, upvec, dir, 0, point->radius);
	}
	assert(point && next_point);
	splineAppendCP(node_connection->path, next_abs_pos, upvec, dir, 0, next_point->radius);
}

void genesisMakeDefaultConnection(GenesisNodeConnection *node_connection, GenesisNode *node_start, GenesisNode *node_end)
{
	Vec3 dir;
	Vec3 start_pos, end_pos;

	if(!node_start || !node_end)
		return;

	node_connection->path = StructCreate(parse_Spline);

	subVec3(node_end->pos, node_start->pos, dir);
	normalVec3(dir);

	copyVec3(dir, start_pos);
	scaleVec3(start_pos, node_start->draw_size, start_pos);
	addVec3(node_start->pos, start_pos, start_pos);
	start_pos[1] = node_start->pos[1];
	copyVec3(dir, end_pos);
	scaleVec3(end_pos, node_end->draw_size, end_pos);
	subVec3(node_end->pos, end_pos, end_pos);
	end_pos[1] = node_end->pos[1];

	splineAppendCP(node_connection->path, start_pos, upvec, dir, 0, node_connection->radius);
	splineAppendCP(node_connection->path, end_pos, upvec, dir, 0, node_connection->radius);
}

void genesisNodesFixup(GenesisZoneNodeLayout *layout)
{
	int i, cg;

	for (i = 0; i < eaSize(&layout->nodes); i++)
		if (layout->nodes[i]->node_type == 0)
			layout->nodes[i]->node_type = GENESIS_NODE_Clearing;

	for ( cg=0; cg < eaSize(&layout->connection_groups); cg++ )
	{
		GenesisNodeConnectionGroup *connection_group = layout->connection_groups[cg];
		for( i=0; i < eaSize(&connection_group->connections); i++ )
		{
			GenesisNodeConnection *node_connection = connection_group->connections[i];
			if(node_connection->radius == 0)
				node_connection->radius = 40;
			if(eaSize(&node_connection->path_points) > 0)
			{
				GenesisNode *node_start = genesisGetNodeFromUID(layout, node_connection->start_uid);
				GenesisNode *node_end = genesisGetNodeFromUID(layout, node_connection->end_uid);
				genesisMakeDefindedConnection(node_connection, node_start, node_end);
			}
			else if(!node_connection->path)
			{
				GenesisNode *node_start = genesisGetNodeFromUID(layout, node_connection->start_uid);
				GenesisNode *node_end = genesisGetNodeFromUID(layout, node_connection->end_uid);
				genesisMakeDefaultConnection(node_connection, node_start, node_end);
			}
		}
	}
}

void genesisGetHeightFromPathPoint(F32 x, F32 z, Vec3 path_pos, bool *inside_path, F32 *height, F32 *w_div, F32 *dist)
{
	F32 dx = x - path_pos[0];
	F32 dz = z - path_pos[2];
	F32 dist_to_center = fsqrt(SQR(dx) + SQR(dz));
	if(*dist < 0 || dist_to_center < *dist)
		*dist = dist_to_center;
	if(dist_to_center <= 0)
	{
		*inside_path = true;
		*height = path_pos[1];
		return;
	}
	dist_to_center = 1/SQR(dist_to_center);
	*height += path_pos[1]*dist_to_center;
	*w_div += dist_to_center;
}

bool genesisGetHeightFromPath(Spline *spline, F32 x, F32 z, F32 *height_out, F32 *dist_out)
{
	int i;
	F32 t, length;
	F32 height=0;
	F32 w_div=0.0f;
	bool inside_path = false;
	Vec3 cur_up, cur_tangent;
	Vec3 start, end;
	Vec3 path_pos;
	F32 step = 0.25f;
	Vec3 point = {x, 0, z};

	if(eafSize(&spline->spline_points) < 6)
		return false;

	*dist_out = -1.0f;

	for( i=3; i < eafSize(&spline->spline_points); i+=3 )
	{
		copyVec3(&spline->spline_points[i-3], start);
		copyVec3(&spline->spline_points[i], end);
		start[1] = end[1] = 0.0f;
		pointProjectOnLine(start, end, point, path_pos);
		length = distance3(start, end);
		t = distance3(start, path_pos);
		if(length != 0)
			t /= length;
		t = CLAMP(t, 0.0f, 1.0f);
		splineTransform(spline, i-3, t, zerovec3, path_pos, cur_up, cur_tangent, false);
		genesisGetHeightFromPathPoint(x,z,path_pos,&inside_path,&height,&w_div, dist_out);
	}

	if(!inside_path && w_div != 0.0f)
		height /= w_div;

	*height_out = height;
	return true;
}

static F32 gensesisDistToBorder(GenesisNodeBorder *boarder, F32 x, F32 z, F32 *height)
{
	int i;
	F32 rx=0, rz=0;
	F32 ret = PointLineDist2DSquared(x, z, boarder->start[0], boarder->start[2], boarder->end[0]-boarder->start[0], boarder->end[2]-boarder->start[2], &rx, &rz);
	F32 point_dist = boarder->horiz ? rx-boarder->start[0] : rz-boarder->start[2];
	F32 prev_height = boarder->start[1];
	F32 next_height = boarder->end[1];
	F32 prev_dist = 0;
	F32 next_dist = boarder->horiz ? boarder->end[0]-boarder->start[0] : boarder->end[2]-boarder->start[2];
	F32 check_dist = boarder->step;
	F32 t;

	for ( i=0; i < eafSize(&boarder->heights); i++ )
	{
		if(check_dist > point_dist)
		{
			next_height = boarder->heights[i];
			next_dist = check_dist;
			break;
		}
		prev_height = boarder->heights[i];
		prev_dist = check_dist;
		check_dist += boarder->step;
	}

	t = (point_dist-prev_dist)/(next_dist-prev_dist);
	t = CLAMP(t, 0.0f, 1.0f);
	*height = lerp(prev_height, next_height, t);
	return sqrt(ret);
}

F32 genesisGetHeightFromNodes(GenesisZoneNodeLayout *layout, F32 x, F32 z)
{
	int i, cg;
	Color color;
	GenesisGeotype *geotype = GET_REF(layout->geotype);
	F32 path_weight = (geotype ? 1.0f-geotype->node_data.ignore_paths : 1.0f);
	bool inside_node = false;
	F32 w_div=0.0f;
	F32 height=0;
	color.a = 0;

	for( i=0; i < eaSize(&layout->nodes); i++ )
	{
		GenesisNode *node = layout->nodes[i];
		F32 dx = x - node->pos[0];
		F32 dz = z - node->pos[2];
		F32 dist_to_center = (fsqrt(SQR(dx) + SQR(dz))) - node->draw_size;
		F32 weight;
		if(dist_to_center <= 0)
		{
			inside_node = true;
			height = node->pos[1];
			break;
		}
		weight = 1/pow(dist_to_center, (geotype ? geotype->node_data.interp_pow : 3));
		height += node->pos[1]*weight;
		w_div += weight;
	}
	if(!inside_node && path_weight > 0.001f)
	{
		//Add the distances to the paths
		for ( cg=0; cg < eaSize(&layout->connection_groups); cg++ )
		{
			GenesisNodeConnectionGroup *connection_group = layout->connection_groups[cg];
			for( i=0; i < eaSize(&connection_group->connections); i++ )
			{
				GenesisNodeConnection *conn = connection_group->connections[i];
				F32 path_height, dist_to_center, weight;
				if (conn->path && genesisGetHeightFromPath(conn->path, x, z, &path_height, &dist_to_center))
				{
					if(dist_to_center <= 0)
					{
						inside_node = true;
						height = path_height;
						break;
					}
					dist_to_center /= path_weight;
					weight = 1/pow(dist_to_center, (geotype ? geotype->node_data.interp_pow : 3));
					height += path_height*weight;
					w_div += weight;
				}
			}
		}
	}
	if(!inside_node)
	{
		for ( i=0; i < eaSize(&layout->node_borders); i++ )
		{
			GenesisNodeBorder *boarder = layout->node_borders[i];
			F32 border_height, dist_to_center, weight;
			dist_to_center = gensesisDistToBorder(boarder, x, z, &border_height);
			if(dist_to_center <= 0)
			{
				inside_node = true;
				height = border_height;
				break;
			}
			weight = 1/pow(dist_to_center, (geotype ? geotype->node_data.interp_pow : 3));
			height += border_height*weight;
			w_div += weight;
		}
	}

	if(!inside_node && w_div != 0.0f)
	{
		height /= w_div;
	}

	return height;
}

void genesisCenterNodesInsideBorders(GenesisZoneNodeLayout *layout)
{
	int i, j, cg;
	F32 src_mid;
	bool height_found = false;
	F32 min_height=0, max_height=0;
	if(eaSize(&layout->node_borders) <= 0)
		return;
	min_height = max_height = layout->node_borders[0]->start[1];
	for ( i=0; i < eaSize(&layout->node_borders); i++ )
	{
		GenesisNodeBorder *border = layout->node_borders[i];
		min_height = MIN(border->start[1], min_height);
		min_height = MIN(border->end[1], min_height);
		max_height = MAX(border->start[1], max_height);
		max_height = MAX(border->end[1], max_height);
		for ( j=0; j < eafSize(&border->heights); j++ )
		{
			min_height = MIN(border->heights[j], min_height);
			max_height = MAX(border->heights[j], max_height);
		}
	}
	src_mid = (max_height+min_height)/2.0f;
	height_found = false;
	for( i=0; i < eaSize(&layout->nodes); i++ )
	{
		if(!height_found)
		{
			height_found = true;
			min_height = layout->nodes[i]->pos[1];
			max_height = layout->nodes[i]->pos[1];
			continue;
		}
		min_height = MIN(layout->nodes[i]->pos[1], min_height);
		max_height = MAX(layout->nodes[i]->pos[1], max_height);
	}
	if(height_found)
	{
		F32 dest_mid = (max_height+min_height)/2.0f;
		F32 move_amt = src_mid - dest_mid;
		for( i=0; i < eaSize(&layout->nodes); i++ )
			layout->nodes[i]->pos[1] += move_amt;
		for ( cg=0; cg < eaSize(&layout->connection_groups); cg++ )
		{
			GenesisNodeConnectionGroup *connection_group = layout->connection_groups[cg];
			for ( i=0; i < eaSize(&connection_group->connections); i++ )
			{
				GenesisNodeConnection *connection = connection_group->connections[i];
				if(connection->path)
				{
					for ( j=0; j < eafSize(&connection->path->spline_points); j+=3 )
						connection->path->spline_points[j+1] += move_amt;
				}
			}
		}
	}
}

static GenesisNodeBorder *genesisNewBorderNodes(GenesisZoneNodeLayout *src, bool horiz, F32 lmin, F32 lmax, F32 opos, F32 hoffset)
{
	GenesisNodeBorder *border = StructCreate(parse_GenesisNodeBorder);

	border->step = 50.0f;
	border->horiz = horiz;
	if(horiz)
	{
		border->start[0] = lmin;
		border->end[0] = lmax;
		border->start[2] = border->end[2] = opos;
	}
	else
	{
		border->start[2] = lmin;
		border->end[2] = lmax;
		border->start[0] = border->end[0] = opos;
	}

	if(src)
	{
		F32 i, j;
		border->start[1] = genesisGetHeightFromNodes(src, border->start[0], border->start[2]) + hoffset;
		border->end[1] = genesisGetHeightFromNodes(src, border->end[0], border->end[2]) + hoffset;
		j = opos;
		for( i = (lmin + border->step); i <= lmax-border->step; i += border->step)
			eafPush(&border->heights, genesisGetHeightFromNodes(src, horiz ? i : j, horiz ? j : i) + hoffset);
	}
	else
	{
		//If no src, then we just want to pull down the edges.
		border->start[1] = border->end[1] = -100.0f;
	}

	return border;
}

static GenesisNodeBorder* genesisMakeInnerNodeBorder(GenesisNodeBorder *border, bool start)
{
	#define GEN_VISTA_INNER_HEIGHT_OFFSET 20
	#define GEN_VISTA_INNER_OFFSET 35
	int i;
	F32 orig_size, new_size;
	GenesisNodeBorder *border_copy;
	int idx = border->horiz ? 0 : 2;
	int inv_idx = border->horiz ? 2 : 0;
	Vec3 start_xz = {0,0,0};
	border_copy = StructClone(parse_GenesisNodeBorder, border);
	assert(border_copy);

	orig_size = border_copy->end[idx]-border_copy->start[idx];
	border_copy->start[idx] += GEN_VISTA_INNER_OFFSET;
	border_copy->end[idx] -= GEN_VISTA_INNER_OFFSET;
	if(start)
	{
		border_copy->start[inv_idx] += GEN_VISTA_INNER_OFFSET;
		border_copy->end[inv_idx] += GEN_VISTA_INNER_OFFSET;
	}
	else 
	{
		border_copy->start[inv_idx] -= GEN_VISTA_INNER_OFFSET;
		border_copy->end[inv_idx] -= GEN_VISTA_INNER_OFFSET;
	}

	new_size = border_copy->end[idx]-border_copy->start[idx];
	border_copy->step *= (new_size/orig_size);

	start_xz[0] = border_copy->start[0];
	start_xz[2] = border_copy->start[2];
	for ( i=0; i < eafSize(&border_copy->heights); i++ )
	{
		F32 ret_height;
		start_xz[idx] += border_copy->step;
		gensesisDistToBorder(border, start_xz[0], start_xz[2], &ret_height);
		border_copy->heights[i] = ret_height + GEN_VISTA_INNER_HEIGHT_OFFSET;
	}
	border_copy->start[1] += GEN_VISTA_INNER_HEIGHT_OFFSET;
	border_copy->end[1] += GEN_VISTA_INNER_HEIGHT_OFFSET;
	return border_copy;
}

void genesisMakeNodeBorders(GenesisZoneNodeLayout *dest, GenesisZoneNodeLayout *src, Vec2 min_pos, Vec2 max_pos)
{
	if(nearSameF32(min_pos[0], max_pos[0]) || nearSameF32(min_pos[1], max_pos[1]))
		return;

	//////////
	// Outer
	//////////
	#define GEN_VISTA_OUTER_HEIGHT_OFFSET 45
	//North
	eaPush(&dest->node_borders, genesisNewBorderNodes(src, true, min_pos[0], max_pos[0], max_pos[1], GEN_VISTA_OUTER_HEIGHT_OFFSET));
	//South
	eaPush(&dest->node_borders, genesisNewBorderNodes(src, true, min_pos[0], max_pos[0], min_pos[1], GEN_VISTA_OUTER_HEIGHT_OFFSET));
	//East
	eaPush(&dest->node_borders, genesisNewBorderNodes(src, false, min_pos[1], max_pos[1], max_pos[0], GEN_VISTA_OUTER_HEIGHT_OFFSET));
	//West
	eaPush(&dest->node_borders, genesisNewBorderNodes(src, false, min_pos[1], max_pos[1], min_pos[0], GEN_VISTA_OUTER_HEIGHT_OFFSET));

	//////////
	// Inner
	//////////
	if(src)
	{
		eaPush(&dest->node_borders, genesisMakeInnerNodeBorder(dest->node_borders[0], false));//North
		eaPush(&dest->node_borders, genesisMakeInnerNodeBorder(dest->node_borders[1], true));//South
		eaPush(&dest->node_borders, genesisMakeInnerNodeBorder(dest->node_borders[2], false));//East
		eaPush(&dest->node_borders, genesisMakeInnerNodeBorder(dest->node_borders[3], true));//West
	}
}

void genesisDoNodesToDesign(TerrainEditorSource *source, GenesisZoneNodeLayout *layout)
{
	GenesisGeotype *geotype = GET_REF(layout->geotype);
	int granularity = source->visible_lod;
	int step = (1<<granularity);
	F32 x, z;
	F32 height;
	HeightMapCache cache = { 0 };
	Vec2 min_pos, max_pos;
	F32 max_water_depth = -4.0f;

	if(eaSize(&layout->nodes) <= 0 || granularity == 0)//Any conditions you add, also add to terrainNodeToDesign
		return;

	terrainGetEditableBounds(source, min_pos, max_pos);

	if(geotype && geotype->node_data.max_water_depth)
		max_water_depth = geotype->node_data.max_water_depth;

	//Generate heights from all nodes
	for(x=min_pos[0]; x <= max_pos[0]; x += step)
	{
		for(z=min_pos[1]; z <= max_pos[1]; z += step)
		{
			F32 orig_height;
			height = genesisGetHeightFromNodes(layout, x, z);
			height += (geotype ? geotype->node_data.noise_level : 20.0f)*terEdPerlinNoise(x*0.005, z*0.005);
			height = MAX(height, max_water_depth);
			if (terrainSourceGetHeight(source, x, z, &orig_height, &cache))
				terrainSourceDrawHeight(source, x, z, source->visible_lod, height-orig_height, &cache );
		}
	}
}

static void genesisNodeEnsureDetailKit(GenesisNode *node, GenesisDetailKitAndDensity *kit)
{
	if (node->node_type != GENESIS_NODE_OffMap && !GET_REF(kit->details))
	{
		GenesisDetailKit *empty_kit = RefSystem_ReferentFromString(GENESIS_DETAIL_DICTIONARY, "EmptySpace");
		if(!empty_kit)
		{
			genesisRaiseErrorInternal(GENESIS_FATAL_ERROR, GENESIS_DETAIL_DICTIONARY, "EmptySpace",
									  "Could not find EmptySpace detail kit");
			return;
		}
		SET_HANDLE_FROM_REFERENT(GENESIS_DETAIL_DICTIONARY, empty_kit, kit->details);
		kit->detail_density = 100.0f;
	}
}

void genesisExteriorCreateMissionVolumes(GenesisToPlaceState *to_place, Vec3 min, Vec3 max, const char *layout_name, GenesisMissionRequirements **mission_reqs, GenesisBackdrop *backdrop)
{
	int j;
	GenesisToPlaceObject *mission_parent;

	mission_parent = calloc(1, sizeof(GenesisToPlaceObject));
	mission_parent->object_name = allocAddString("Missions");
	mission_parent->uid = 0;
	identityMat4(mission_parent->mat);
	mission_parent->parent = NULL;
	if (eaSize(&mission_reqs) > 1)
	{
		mission_parent->params = genesisCreateMultiMissionWrapperParams();
	}
	eaPush(&to_place->objects, mission_parent);

	for (j = 0; j < eaSize(&mission_reqs); j++)
	{
		GenesisToPlaceObject *mission_object = calloc(1, sizeof(GenesisToPlaceObject));
		mission_object->object_name = genesisMissionVolumeName(layout_name, mission_reqs[j]->missionName);
		mission_object->challenge_name = strdup(mission_object->object_name);
		mission_object->challenge_is_unique = true;
		identityMat3(mission_object->mat);
		mission_object->uid = 0;
		mission_object->parent = mission_parent;
		mission_object->params = StructClone(parse_GenesisProceduralObjectParams, mission_reqs[j]->params);
		mission_object->params->volume_properties = StructCreate(parse_GroupVolumeProperties);
		mission_object->params->volume_properties->eShape = GVS_Box;
		copyVec3(min, mission_object->params->volume_properties->vBoxMin);
		copyVec3(max, mission_object->params->volume_properties->vBoxMax);
		if (backdrop && eaSize(&backdrop->amb_sounds) > 0) {
			mission_object->params->sound_sphere_properties = StructCreate(parse_WorldSoundSphereProperties);	
			mission_object->params->sound_sphere_properties->pcEventName = allocAddString(backdrop->amb_sounds[0]);
		}
		eaPush(&to_place->objects, mission_object);
	}
}

bool genesisNodeDoObjectPlacement(int iPartitionIdx, GenesisZoneMapData *genesis_data, GenesisToPlaceState *to_place, GenesisZoneNodeLayout *layout, GenesisMissionRequirements **mission_reqs, bool detail, bool no_sharing)
{
	int i, j, k, cg;
	GenesisToPlaceObject *nature_nodes_object = calloc(1, sizeof(GenesisToPlaceObject));
	Vec3 layer_min, layer_max;

	nature_nodes_object->object_name = allocAddString("NatureNodes");
	identityMat4(nature_nodes_object->mat);
	eaPush(&to_place->objects, nature_nodes_object);

	if(genesis_data) {
		genesisGetBoundsForLayer(genesis_data, GenesisMapType_Exterior, 0, layer_min, layer_max);
	} else {
		setVec3same(layer_min, -8000);
		setVec3same(layer_max,  8000);
	}

	// Mission volumes
	genesisExteriorCreateMissionVolumes(to_place, layer_min, layer_max, layout->layout_name, mission_reqs, layout->backdrop);

	for (i = 0; i < eaSize(&layout->nodes); i++ )
	{
		GenesisNode *node = layout->nodes[i];
		int seed = node->seed;
		F32 width = node->actual_size, depth = node->actual_size;
		GenesisPopulateArea area = { 0 };
		GenesisDetailKitAndDensity *detail_kits[3] = {&node->detail_kit_1, &node->detail_kit_2, NULL};
		setVec3(area.min, node->pos[0]-width, node->pos[1], node->pos[2]-width);
		setVec3(area.max, node->pos[0]+width, node->pos[1], node->pos[2]+width);
		area.radius = width;

		genesisNodeEnsureDetailKit(node, detail_kits[GDKT_Detail_1]);

		if (node->node_type != GENESIS_NODE_Nature && !node->do_not_pop)
		{
			GenesisToPlaceObject *shared_parent;
			GenesisToPlaceObject *mission_select_parent;
			GenesisToPlaceObject **mission_parents = NULL;
			GenesisToPlaceObject *node_parent = calloc(1, sizeof(GenesisToPlaceObject));
			GenesisRoomMission **room_missions_with_static_obj = NULL;
			GenesisRoomMission static_obj_mission = { 0 };
			Mat3 off_map_mat = {0};
			Vec3 extents[2];

			if(node->node_type == GENESIS_NODE_OffMap)
			{
				GenesisNode *link_node = genesisGetNodeFromUID(layout, node->off_map_uid);
				F32 rad = node->actual_size;
				F32 dist;
				assert(link_node);
				subVec3(link_node->pos, node->pos, off_map_mat[2]);
				dist = normalVec3(off_map_mat[2]);
				setVec3(off_map_mat[1], 0, 1, 0);
				crossVec3(off_map_mat[1], off_map_mat[2], off_map_mat[0]);
				normalVec3(off_map_mat[0]);
				crossVec3(off_map_mat[2], off_map_mat[0], off_map_mat[1]);
				normalVec3(off_map_mat[1]);
				setVec3(extents[0], -rad, -rad, rad*0.5f);
				setVec3(extents[1], rad, rad, dist);
			}

			node_parent->object_name = allocAddString(node->name ? node->name : "Node");
			node_parent->uid = 0;
			node_parent->parent = NULL;
			identityMat4(node_parent->mat);
			copyVec3(node->pos, node_parent->mat[3]);
			eaPush(&to_place->objects, node_parent);

			mission_select_parent = calloc(1, sizeof( GenesisToPlaceObject ));
			mission_select_parent->object_name = allocAddString( "Missions" );
			mission_select_parent->parent = node_parent;
			if (eaSize(&mission_reqs) > 1)
			{
				mission_select_parent->params = genesisCreateMultiMissionWrapperParams();
			}
			identityMat4(mission_select_parent->mat);
			copyVec3(node->pos, mission_select_parent->mat[3]);
			eaPush(&to_place->objects, mission_select_parent);

			for (j = 0; j < eaSize(&mission_reqs); j++) {
				GenesisToPlaceObject *mission_object = calloc(1, sizeof(GenesisToPlaceObject));
				eaPush(&to_place->objects, mission_object);
				mission_object->parent = mission_select_parent;
				mission_object->object_name = genesisMissionRoomVolumeName(layout->layout_name, node->name, mission_reqs[j]->missionName);
				mission_object->challenge_name = strdup(mission_object->object_name);
				mission_object->challenge_is_unique = true;
				identityMat4(mission_object->mat);
				copyVec3(node->pos, mission_object->mat[3]);
				mission_object->params = StructClone(parse_GenesisProceduralObjectParams, genesisFindMissionRoomProceduralParams(mission_reqs[j], layout->layout_name, node->name));
				if (!mission_object->params)
					mission_object->params = StructCreate(parse_GenesisProceduralObjectParams);
				genesisProceduralObjectAddVolumeType(mission_object->params, "GenesisNode");
				if(node->node_type == GENESIS_NODE_OffMap)
				{
					GenesisToPlaceObject *mission_volume = calloc(1, sizeof(GenesisToPlaceObject));
					mission_volume->parent = mission_object;
					mission_volume->object_name = allocAddString("MissionSubVolume");
					copyMat3(off_map_mat, mission_volume->mat);
					copyVec3(node->pos, mission_volume->mat[3]);
					mission_volume->params = StructCreate(parse_GenesisProceduralObjectParams);
					mission_volume->params->volume_properties = StructCreate(parse_GroupVolumeProperties);
					mission_volume->params->volume_properties->bSubVolume = 1;
					mission_volume->params->volume_properties->eShape = GVS_Box;
					copyVec3(extents[0], mission_volume->params->volume_properties->vBoxMin);
					copyVec3(extents[1], mission_volume->params->volume_properties->vBoxMax);
					eaPush(&to_place->objects, mission_volume);
					//There has to be a volume size on the top level for sub volumes to work, 
					//and we don't want there to be a volume there, so we just shove it way underground
					mission_object->params->volume_properties = StructCreate(parse_GroupVolumeProperties);
					mission_object->params->volume_properties->eShape = GVS_Box;
					setVec3(mission_object->params->volume_properties->vBoxMin, -1, -901, -1);
					setVec3(mission_object->params->volume_properties->vBoxMax,  1, -900,  1);
				}
				else
				{
					mission_object->params->volume_properties = StructCreate(parse_GroupVolumeProperties);
					mission_object->params->volume_properties->eShape = GVS_Sphere;
					mission_object->params->volume_properties->fSphereRadius = node->actual_size;
				}
				eaPush(&mission_parents, mission_object);
			}

			if (no_sharing) {
				shared_parent = NULL;
			} else {
				shared_parent = calloc(1, sizeof(GenesisToPlaceObject));
				eaPush(&to_place->objects, shared_parent);
				shared_parent->object_name = allocAddString("Shared");
				shared_parent->parent = node_parent;
				identityMat4(shared_parent->mat);
				copyVec3(node->pos, shared_parent->mat[3]);

				if (!shared_parent->params)
					shared_parent->params = StructCreate(parse_GenesisProceduralObjectParams);
				genesisProceduralObjectAddVolumeType(shared_parent->params, "GenesisNode");
				if(node->node_type == GENESIS_NODE_OffMap)
				{
					GenesisToPlaceObject *mission_volume = calloc(1, sizeof(GenesisToPlaceObject));
					mission_volume->parent = shared_parent;
					mission_volume->object_name = allocAddString("MissionSubVolume");
					copyMat3(off_map_mat, mission_volume->mat);
					copyVec3(node->pos, mission_volume->mat[3]);
					mission_volume->params = StructCreate(parse_GenesisProceduralObjectParams);
					mission_volume->params->volume_properties = StructCreate(parse_GroupVolumeProperties);
					mission_volume->params->volume_properties->bSubVolume = 1;
					mission_volume->params->volume_properties->eShape = GVS_Box;
					copyVec3(extents[0], mission_volume->params->volume_properties->vBoxMin);
					copyVec3(extents[1], mission_volume->params->volume_properties->vBoxMax);
					eaPush(&to_place->objects, mission_volume);
					//There has to be a volume size on the top level for sub volumes to work, 
					//and we don't want there to be a volume there, so we just shove it way underground
					shared_parent->params->volume_properties = StructCreate(parse_GroupVolumeProperties);
					shared_parent->params->volume_properties->eShape = GVS_Box;
					setVec3(shared_parent->params->volume_properties->vBoxMin, -1, -901, -1);
					setVec3(shared_parent->params->volume_properties->vBoxMax,  1, -900,  1);
				}
				else
				{
					shared_parent->params->volume_properties = StructCreate(parse_GroupVolumeProperties);
					shared_parent->params->volume_properties->eShape = GVS_Sphere;
					shared_parent->params->volume_properties->fSphereRadius = node->actual_size;
				}

				if(!shared_parent->params->genesis_properties)
					shared_parent->params->genesis_properties = StructCreate(parse_WorldGenesisProperties);
				shared_parent->params->genesis_properties->iNodeType = node->node_type;
			}

			// TODO: add to the shared mission..
			eaCopy(&room_missions_with_static_obj, &node->missions);
			eaPush(&room_missions_with_static_obj, &static_obj_mission);
			static_obj_mission.mission_uid = -1;
			static_obj_mission.objects = node->static_objects;
			
			for ( cg=0; cg < eaSize(&layout->connection_groups); cg++ )
			{
				GenesisNodeConnectionGroup *connection_group = layout->connection_groups[cg];
				for (j = 0; j < eaSize(&connection_group->connections); j++)
				{
					GenesisNodeConnection *connection = connection_group->connections[j];
					Vec3 start_point, end_point, use_point;
					int found = 0;
					int end_pt;
					Vec3 dir;

					if(connection->to_off_map)
						continue;

					end_pt = eafSize(&connection->path->spline_points)-3;
					copyVec3(&connection->path->spline_points[0], start_point);
					copyVec3(&connection->path->spline_points[end_pt], end_point);

					if (connection->start_uid == node->uid)
					{
						copyVec3(start_point, use_point);
						subVec3(end_point, start_point, dir);
						found = 1;
					}
					else if (connection->end_uid == node->uid)
					{
						copyVec3(end_point, use_point);
						subVec3(start_point, end_point, dir);
						found = 2;
					}

					// Off Path Nodes have had their splines moved to the center so that they connect.
					// However, we still want the doors for populate to be on the outer edges, so we push them there.
					if(node->node_type == GENESIS_NODE_OffMap)
					{
						normalVec3(dir);
						scaleVec3(dir, node->actual_size, dir);
						addVec3(use_point, dir, use_point);
					}

					if (found > 0)
					{
						GenesisPopulateDoor *door = calloc(1, sizeof(GenesisPopulateDoor));
						copyVec3(use_point, door->point);
						door->entrance = (found == 2);
						door->exit = (found == 1);
						door->dest_name = NULL; 
						eaPush(&area.doors, door);
					}
				}
			}

			printf("GENESIS POPULATE %d / %s - (%f,%f)-(%f,%f)\n", i, node->name,
				   area.min[0],area.min[2],area.max[0],area.max[2]);
			if (!genesisPopulateArea(iPartitionIdx, i, node->source_context, to_place, &area, seed, shared_parent, mission_parents, mission_reqs, 
									 room_missions_with_static_obj, NULL, detail_kits, node->node_type != GENESIS_NODE_OffMap,
									 node->node_type == GENESIS_NODE_SideTrail, false, false, no_sharing, -1, false))
			{
				genesisRaiseError(GENESIS_ERROR, 
					genesisMakeTempErrorContextRoom(node->name, layout->layout_name), 
					"Failed to place objects in room.");
			}
			eaDestroy(&room_missions_with_static_obj);
			eaDestroy(&mission_parents);
			eaDestroyEx(&area.doors, NULL);
		}
		else
		{
			GenesisToPlaceObject *node_object = calloc(1, sizeof(GenesisToPlaceObject));
			node_object->object_name = allocAddString("NatureNode");
			node_object->parent = nature_nodes_object;
			identityMat4(node_object->mat);
			copyVec3(node->pos, node_object->mat[3]);

			if (!node_object->params)
				node_object->params = StructCreate(parse_GenesisProceduralObjectParams);
			genesisProceduralObjectAddVolumeType(node_object->params, "GenesisNode");
			node_object->params->volume_properties = StructCreate(parse_GroupVolumeProperties);
			node_object->params->volume_properties->eShape = GVS_Sphere;
			node_object->params->volume_properties->fSphereRadius = 1;
			if(!node_object->params->genesis_properties)
				node_object->params->genesis_properties = StructCreate(parse_WorldGenesisProperties);
			node_object->params->genesis_properties->iNodeType = node->node_type;

			eaPush(&to_place->objects, node_object);
		}
	}
	for (cg = 0; cg < eaSize(&layout->connection_groups); cg++)
	{
		GenesisNodeConnectionGroup *connection_group = layout->connection_groups[cg];
		GenesisToPlaceObject *path_parent = calloc(1, sizeof(GenesisToPlaceObject));
		char path_name[256];
		sprintf(path_name, "Connection_%d", cg);

		path_parent->object_name = allocAddString(path_name);
		path_parent->uid = 0;
		path_parent->parent = NULL;
		identityMat4(path_parent->mat);
		eaPush(&to_place->objects, path_parent);

		for ( i=0; i < eaSize(&connection_group->connections); i++ )
		{
			GenesisNodeConnection *connection = connection_group->connections[i];
			if (connection->path)
			{
				// Create spline object (for exclusion)
				GenesisToPlaceObject *object = calloc(1, sizeof(GenesisToPlaceObject));
				sprintf(path_name, "PathCurve_%d", i);
				object->object_name = allocAddString("PathCurve");
				object->parent = path_parent;
				identityMat4(object->mat);
				object->params = StructCreate(parse_GenesisProceduralObjectParams);
				object->params->curve = StructCreate(parse_WorldCurve);
				StructCopy(parse_Spline, connection->path, &object->params->curve->spline, 0, 0, 0);
				splineResetRotation(&object->params->curve->spline, object->mat);
				object->params->curve->terrain_exclusion = true;
				object->params->curve->terrain_filter = !connection_group->side_trail;
				object->params->curve->genesis_path = !connection_group->side_trail;
				eaPush(&to_place->objects, object);
			}
		}
		for (j = 0; j < eaSize(&mission_reqs); j++)
		{
			int nodenum;
			GenesisNodeMission *mission = NULL;

			// Create volume & terrain excluder object
			GenesisToPlaceObject *mission_object = calloc(1, sizeof(GenesisToPlaceObject));
			mission_object->parent = path_parent;
			identityMat4(mission_object->mat);
			eaPush(&to_place->objects, mission_object);

			for (k = 0; k < eaSize(&connection_group->missions); k++)
				if (!strcmp(connection_group->missions[k]->mission_name, mission_reqs[j]->missionName))
				{
					mission = connection_group->missions[k];
					break;
				}

			if (mission)
			{
				for (nodenum = 0; nodenum < eaSize(&mission->objects); ++nodenum)
				{
					Vec3 pos, up, tan;
					GenesisNodeObject *site = mission->objects[nodenum];
					F32 offset = site->offset;
					
					assert(site->path_idx < eaSize(&connection_group->connections));
					genesisPathNodeGetPosition(connection_group->connections[site->path_idx], offset, pos, up, tan);
					{
						int seed = site->seed;
						F32 width = site->actual_size, depth = site->actual_size;
						GenesisPopulateArea area = { 0 };
						GenesisRoomMission hack_room = { 0 };
						GenesisRoomMission **hack_rooms = NULL;
						GenesisDetailKitAndDensity *detail_kits[3] = {NULL, NULL, NULL};
						setVec3(area.min, pos[0]-width, pos[1], pos[2]-depth);
						setVec3(area.max, pos[0]+width, pos[1], pos[2]+depth);

						// Place the single object in a "room" so that
						// it can be populated
						//
						// MJF TODO: This is a hack, this placement
						// algorithm will need to be rewritten.
						eaPush( &hack_rooms, &hack_room );
						hack_room.mission_uid = -1;
						eaPush(&hack_room.objects, StructClone(parse_GenesisObject, site->object));
						
						printf("GENESIS POPULATE %d / %s (site %d) - (%f,%f)-(%f,%f)\n", cg, connection_group->name, j,
								area.min[0],area.min[2],area.max[0],area.max[2]);
						if (!genesisPopulateArea(iPartitionIdx, cg + eaSize(&layout->nodes), connection_group->source_context, to_place, &area, seed+j,
												 path_parent, NULL, NULL, hack_rooms, NULL,
												 detail_kits, false, false, true, true, false, -1, false))
						{
							genesisRaiseError(GENESIS_ERROR, 
								genesisMakeTempErrorContextPath(connection_group->name, layout->layout_name), 
								"Failed to place objects in path.");
						}

						StructDeInit(parse_GenesisRoomMission, &hack_room);
						eaDestroy(&hack_rooms);
					}
				}
				for(nodenum = 0; nodenum < eaSize(&mission->patrols); ++nodenum)
				{
					GenesisNodePatrol *patrol = mission->patrols[nodenum];
					
					if (patrol->patrol->type == GENESIS_PATROL_Path || patrol->patrol->type == GENESIS_PATROL_Path_OneWay)
					{
						int pat_pt;
						char patrol_name[256];
						GenesisToPlacePatrol *new_patrol = calloc(1, sizeof(GenesisToPlacePatrol));
						sprintf(patrol_name, "%s_%02d_Patrol", patrol->patrol->owner_challenge->challenge_name, patrol->patrol->owner_challenge->challenge_id);
						new_patrol->patrol_name = StructAllocString(patrol_name);
						if( patrol->patrol->type == GENESIS_PATROL_Path_OneWay) {
							new_patrol->patrol_properties.route_type = PATROL_ONEWAY;
						} else {
							new_patrol->patrol_properties.route_type = PATROL_PINGPONG;
						}
						for (pat_pt = 0; pat_pt < 10; pat_pt++)
						{
							Vec3 pos, up, tan;
							WorldPatrolPointProperties *patrol_point = StructCreate(parse_WorldPatrolPointProperties);
							assert(patrol->path_idx < eaSize(&connection_group->connections));
							genesisPathNodeGetPosition(connection_group->connections[patrol->path_idx], pat_pt*0.09f+0.05f, pos, up, tan);
							copyVec3(pos, patrol_point->pos);
							eaPush(&new_patrol->patrol_properties.patrol_points, patrol_point);
						}
						eaPush(&to_place->patrols, new_patrol);
					}
				}
			}
		}
	}
	return true;
}

void genesisMoveNodesToDesign(TerrainTaskQueue *queue, TerrainEditorSource *source, GenesisZoneNodeLayout *layout, int flags)
{
	int i, j;
	GenesisGeotype *geotype = GET_REF(layout->geotype);

	//Fixup Data
	genesisNodesFixup(layout);

	for (i = 0; i < eaSize(&source->layers); i++)
	{
		TerrainEditorSourceLayer *layer = source->layers[i];
		if (layer->effective_mode == LAYER_MODE_EDITABLE)
		{
			layer->color_shift = layout->color_shift;
		}
	}
	terEdApplyBrushToTerrain(queue, source, "SYS_TER_Clear_All", false, false, flags);
	terrainQueueGenesisNodesToDesign(queue, source, layout, source->visible_lod, flags);
	terrainQueueUpdateNormals(queue, flags);
	terrainQueueFinishTask(queue, NULL, NULL);
	if(geotype)
	{
		for( i=0; i < eaSize(&geotype->node_data.brushes); i++ )
		{
			char *brush_name = geotype->node_data.brushes[i]->brush_name;
			int itr = geotype->node_data.brushes[i]->count;
			for( j=0; j < itr; j++ )
			{
				terEdApplyBrushToTerrain(queue, source, brush_name, false, false, flags);
			}
		}
	}
	terrainQueueFinishTask(queue, NULL, NULL);
}

bool genesisCreateNodeLayoutHelper(GenesisZoneNodeLayout *layout, GroupDef *def, GroupInfo *info, GroupInheritedInfo *inherited_info, bool needs_entry)
{
	int node_type = SAFE_MEMBER(def->property_structs.genesis_properties, iNodeType);
	if (node_type != 0)
	{
		int i;
		bool found = false;
		for (i = 0; i < eaSize(&layout->nodes); i++)
			if (nearSameVec3(layout->nodes[i]->pos, info->world_matrix[3]))
			{
				found = true;
				break;
			}
		if (!found)
		{
			GenesisNode *new_node = StructCreate(parse_GenesisNode);
			new_node->name = StructAllocString(def->name_str);
			new_node->uid = 1;
			if(def->property_structs.volume && def->property_structs.volume->eShape == GVS_Sphere)
				new_node->draw_size = new_node->actual_size = def->property_structs.volume->fSphereRadius;
			else 
				new_node->draw_size = new_node->actual_size = 10;
			new_node->node_type = node_type;
			copyVec3(info->world_matrix[3], new_node->pos);
			eaPush(&layout->nodes, new_node);
		}
	}
	if (def->property_structs.curve && def->property_structs.curve->genesis_path)
	{
		GenesisNodeConnectionGroup *connection_group = StructCreate(parse_GenesisNodeConnectionGroup);
		GenesisNodeConnection *connection = StructCreate(parse_GenesisNodeConnection);
		connection->path = StructCreate(parse_Spline);
		StructCopy(parse_Spline, &def->property_structs.curve->spline, connection->path, 0, 0, 0);
		splineTransformMatrix(connection->path, info->curve_matrix);
		eaPush(&connection_group->connections, connection);
		eaPush(&layout->connection_groups, connection_group);
		connection_group->source_context = genesisMakeErrorAutoGen(genesisMakeTempErrorContextPath( "Connection Group", layout->layout_name ));
	}
	return true;
}

GenesisZoneNodeLayout *genesisCreateNodeLayoutFromWorld()
{
	int i;
	GenesisZoneNodeLayout *layout = StructCreate(parse_GenesisZoneNodeLayout);
	for (i = zmapGetLayerCount(NULL)-1; i >= 0; --i)
	{
		ZoneMapLayer *layer = zmapGetLayer(NULL, i);
		layerGroupTreeTraverse(layer, genesisCreateNodeLayoutHelper, layout, false, false);
	}
	return layout;
}

void genesisMoveNodesToDesign_Temp(TerrainTaskQueue *queue, TerrainEditorSource *source, GenesisGeotype *geotype, int flags)
{
	int i, j;
	static GenesisZoneNodeLayout *layout = NULL;

	if (layout)
		StructDestroy(parse_GenesisZoneNodeLayout, layout);

	layout = genesisCreateNodeLayoutFromWorld();

	terEdApplyBrushToTerrain(queue, source, "SYS_TER_Clear_All", false, false, flags);
	terrainQueueGenesisNodesToDesign(queue, source, layout, source->visible_lod, flags);
	terrainQueueUpdateNormals(queue, flags);
	terrainQueueFinishTask(queue, NULL, NULL);
	if(geotype)
	{
		for( i=0; i < eaSize(&geotype->node_data.brushes); i++ )
		{
			char *brush_name = geotype->node_data.brushes[i]->brush_name;
			int itr = geotype->node_data.brushes[i]->count;
			for( j=0; j < itr; j++ )
			{
				terEdApplyBrushToTerrain(queue, source, brush_name, false, false, flags);
			}
		}
	}
	terrainQueueFinishTask(queue, NULL, NULL);
}

void genesisConstraintDestroy(GenesisNodeConstraint *constraint)
{
	StructDestroy(parse_GenesisNodeConstraint, constraint);
}

void genesisNodeDestroy(GenesisNode *node)
{
	eaDestroy(&node->parents);
	StructDestroy(parse_GenesisNode, node);
}

void genesisNodesLayoutDestroy(GenesisZoneNodeLayout *node_layout)
{
	eaDestroyEx(&node_layout->nodes, genesisNodeDestroy);
	StructDestroy(parse_GenesisZoneNodeLayout, node_layout);
}


int genesisGetNodeIndex(GenesisZoneNodeLayout *layout, const char *name)
{
	int i;
	for( i=0; i < eaSize(&layout->nodes) ; i++ )
	{
		if(stricmp(name, layout->nodes[i]->name) == 0)
			return i;
	}
	return -1;
}

GenesisNodeConstraint* genesisMakeMinDistConstraint(GenesisNode *target1, GenesisNode *target2, F32 dist)
{
	GenesisNodeConstraint *new_constraint = StructCreate(parse_GenesisNodeConstraint);

	new_constraint->function = GCF_MinDist;
	new_constraint->values.dist = dist;
	new_constraint->values.start = target1;
	new_constraint->values.end = target2;

	return new_constraint;
}

GenesisNodeConstraint* genesisMakeMaxDistConstraint(GenesisNode *target1, GenesisNode *target2, F32 dist)
{
	GenesisNodeConstraint *new_constraint = StructCreate(parse_GenesisNodeConstraint);

	new_constraint->function = GCF_MaxDist;
	new_constraint->values.dist = dist;
	new_constraint->values.start = target1;
	new_constraint->values.end = target2;

	return new_constraint;
}

GenesisNodeConstraint* genesisMakeAngleConstraint(GenesisNode *target1, GenesisNode *target2, F32 angle)
{
	GenesisNodeConstraint *new_constraint = StructCreate(parse_GenesisNodeConstraint);

	new_constraint->function = GCF_MaxVertAngleDiff;
	new_constraint->values.angle = angle;
	new_constraint->values.start = target1;
	new_constraint->values.end = target2;

	return new_constraint;
}

#endif

#include "wlGenesisExteriorNode_h_ast.c"
