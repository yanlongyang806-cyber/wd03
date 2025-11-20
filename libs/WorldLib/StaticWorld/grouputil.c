#include "rand.h"

#include "ScratchStack.h"
#include "StringUtil.h"
#include "WorldCell.h"
#include "error.h"
#include "grouputil.h"
#include "wlDebrisField.h"
#include "wlState.h"

AUTO_RUN_ANON(memBudgetAddMapping(__FILE__, BUDGET_World););

int groupInfoOverrideIntParameterValue = -1;

//////////////////////////////////////////////////////////////////////////

WorldDrawableEntry* worldFindNearestDrawableEntry(const Vec3 pos, int entry_type, const char *modelname, bool near_fade, bool welded)
{
	F32 closest_dist_sqrd = FLT_MAX;
	WorldDrawableEntry* result = NULL;
	WorldCell **cells = NULL;
	WorldRegion *region = worldGetWorldRegionByPos(pos);
	int i, j, k;

	if (!region)
		return NULL;

	if (region->root_world_cell)
		eaPush(&cells, region->root_world_cell);

	for (i = 0; i < eaSize(&cells); ++i)
	{
		WorldCell *cell = cells[i];

		if (cell->cell_state != WCS_OPEN || cell->no_drawables)
			continue;

		if (near_fade)
		{
			for (j = eaSize(&cell->drawable.near_fade_entries) - 1; j >= 0; --j)
			{
				WorldDrawableEntry *entry = cell->drawable.near_fade_entries[j];
				F32 dist_sqrd;

				if (entry->base_entry.type != entry_type)
					continue;

				if (modelname)
				{
					const Model *model = worldDrawableEntryGetModel(entry, NULL, NULL, NULL, NULL);
					if (!model || !strstri(model->name, modelname))
						continue;
				}

				dist_sqrd = distance3Squared(entry->base_entry.bounds.world_mid, pos);
				if (dist_sqrd < closest_dist_sqrd)
				{
					closest_dist_sqrd = dist_sqrd;
					result = entry;
				}
			}
		}
		else if (welded)
		{
			for (j = eaSize(&cell->drawable.bins) - 1; j >= 0; --j)
			{
				WorldCellWeldedBin *bin = cell->drawable.bins[j];
				for (k = eaSize(&bin->drawable_entries) - 1; k >= 0; --k)
				{
					WorldDrawableEntry *entry = bin->drawable_entries[k];
					F32 dist_sqrd;

					if (entry->base_entry.type != entry_type)
						continue;

					if (modelname)
					{
						const Model *model = worldDrawableEntryGetModel(entry, NULL, NULL, NULL, NULL);
						if (!model || !strstri(model->name, modelname))
							continue;
					}

					dist_sqrd = distance3Squared(entry->base_entry.bounds.world_mid, pos);
					if (dist_sqrd < closest_dist_sqrd)
					{
						closest_dist_sqrd = dist_sqrd;
						result = entry;
					}
				}
			}
		}
		else
		{
			for (j = eaSize(&cell->drawable.drawable_entries) - 1; j >= 0; --j)
			{
				WorldDrawableEntry *entry = cell->drawable.drawable_entries[j];
				F32 dist_sqrd;

				if (entry->base_entry.type != entry_type)
					continue;

				if (modelname)
				{
					const Model *model = worldDrawableEntryGetModel(entry, NULL, NULL, NULL, NULL);
					if (!model || !strstri(model->name, modelname))
						continue;
				}

				dist_sqrd = distance3Squared(entry->base_entry.bounds.world_mid, pos);
				if (dist_sqrd < closest_dist_sqrd)
				{
					closest_dist_sqrd = dist_sqrd;
					result = entry;
				}
			}
		}

		for (j = 0; j < ARRAY_SIZE(cell->children); ++j)
		{
			WorldCell *child_cell = cell->children[j];
			if (child_cell)
				eaPush(&cells, child_cell);
		}
	}

	eaDestroy(&cells);

	return result;
}

U32 getLayerIDBits(ZoneMapLayer *layer)
{
	U32 layer_id_bits;
	if (layer && layer->zmap_parent)
	{
		if (layer->dummy_layer)
			layer_id_bits = eaFind(&layer->zmap_parent->layers, layer->dummy_layer);
		else
			layer_id_bits = eaFind(&layer->zmap_parent->layers, layer);
		assert(layer_id_bits != 0xffffffff && layer_id_bits <= (GROUP_ID_LAYER_MASK >> GROUP_ID_LAYER_OFFSET));
		layer_id_bits = (layer_id_bits << GROUP_ID_LAYER_OFFSET) & GROUP_ID_LAYER_MASK;
	}
	else
	{
		layer_id_bits = GROUP_ID_LAYER_MASK;
	}
	return layer_id_bits;
}

void initGroupInfo(GroupInfo *info, ZoneMapLayer *layer)
{
	ZeroStruct(info);
	ANALYSIS_ASSUME(info);
	setVec4same(info->color, 1);
	setVec3same(info->tint_offset, 0);
	identityMat4(info->curve_matrix);
	info->uniform_scale = 1.f;
	info->current_scale = 1.f;
	info->visible = true;
	info->parent_entry_child_idx = -1;
	info->visible_child = -1;
	info->layer = layer;
	info->layer_id_bits = getLayerIDBits(layer);
	info->room = NULL;
	info->room_partition = NULL;
	info->room_portal = NULL;
	info->lod_scale = worldRegionGetLodScale(layerGetWorldRegion(layer));;
	info->zmap = layer && layer->zmap_parent ? layer->zmap_parent : worldGetActiveMap();

	info->collisionFilterBits = WC_FILTER_BIT_WORLD_NORMAL |
								WC_FILTER_BIT_MOVEMENT |
								WC_FILTER_BIT_CAMERA_BLOCKING |
								WC_FILTER_BIT_POWERS |
								WC_FILTER_BIT_TARGETING |
								WC_FILTER_BIT_FX_SPLAT;
}

int groupGetRandomChildIdx(GroupChild **children, U32 seed)
{
	int i;
	F32 total_weight=0;
	F32 rand_val;
	for ( i=0; i < eaSize(&children); i++ ) {
		total_weight += (children[i]->weight ? children[i]->weight : 1.0f);
	}
	if(total_weight == (F32)eaSize(&children)) {
		return (eaSize(&children) ? (seed % eaSize(&children)) : 0);
	}
	rand_val = fmod(seed/100.0f, total_weight);
	for ( i=eaSize(&children)-1; i >= 0 ; i-- ) {
		F32 child_weight = (children[i]->weight ? children[i]->weight : 1.0f);
		if(rand_val < child_weight)
			return i;
		else
			rand_val -= child_weight;
	}
	return 0;
}

static void applyInteractToGroupInfo(GroupInfo *info, GroupInheritedInfo *inherited_info, GroupDef *def, GroupDef * parent_def)
{
	if (def->property_structs.interaction_properties) 
	{
		// any model-having children of this node need to have FX
		info->childrenNeedInteractFX = true;
	}
	else if (parent_def && parent_def->property_structs.interaction_properties)
	{
		if (!parent_def->model)
		{
			info->childrenNeedInteractFX = true;
		}
		else
		{
			info->childrenNeedInteractFX = false;
		}
	}
	// else, inherit your parent's property
}

static void applyCollisionToGroupInfo(GroupInfo *info, WorldPhysicalProperties *props)
{
	if(!props->bPhysicalCollision)
		info->collisionFilterBits &= ~(WC_FILTER_BIT_WORLD_NORMAL | WC_FILTER_BIT_MOVEMENT);

	if(!props->bSplatsCollision)
		info->collisionFilterBits &= ~WC_FILTER_BIT_FX_SPLAT;

	switch(props->eCameraCollType) {
	xcase WLCCT_ObjectVanishes:
		{
			info->collisionFilterBits &= ~(WC_FILTER_BIT_CAMERA_BLOCKING | WC_FILTER_BIT_CAMERA_FADE);
			info->collisionFilterBits |= WC_FILTER_BIT_CAMERA_VANISH;
		}
	xcase WLCCT_ObjectFades:
		{
			info->collisionFilterBits &= ~(WC_FILTER_BIT_CAMERA_BLOCKING | WC_FILTER_BIT_CAMERA_VANISH);
			info->collisionFilterBits |= WC_FILTER_BIT_CAMERA_FADE;
		}
	xcase WLCCT_NoCamCollision:
		info->collisionFilterBits &= ~(WC_FILTER_BIT_CAMERA_BLOCKING | WC_FILTER_BIT_CAMERA_FADE | WC_FILTER_BIT_CAMERA_VANISH);
	}

	switch(props->eGameCollType) {
	xcase WLGCT_TargetableOnly:
		info->collisionFilterBits &= ~WC_FILTER_BIT_TARGETING;
	xcase WLGCT_FullyPermeable:
		info->collisionFilterBits &= ~WC_FILTER_BIT_POWERS;
		info->collisionFilterBits &= ~WC_FILTER_BIT_TARGETING;
	}
}

AUTO_STRUCT;
typedef struct OverrideParameter
{
	int int_value;
	const char *string_value;	AST( POOL_STRING )
} OverrideParameter;
extern ParseTable parse_OverrideParameter[];
#define TYPE_parse_OverrideParameter OverrideParameter

static StashTable s_OverrideParameters;

int groupInfoGetIntParameter(GroupInfo *info, const char *param_name, int default_value)
{
	int idx = 0;
	OverrideParameter *parameter_value = NULL;
	
	if (groupInfoOverrideIntParameterValue >= 0) {
		return groupInfoOverrideIntParameterValue;
	}

	if( stashFindPointer( s_OverrideParameters, param_name, &parameter_value )) {
		if( parameter_value )
			return parameter_value->int_value;
	}

	while (idx < GROUP_CHILD_MAX_PARAMETERS && info->parameters[idx].parameter_name)
	{
		GroupChildParameter* param = &info->parameters[idx];
		
		if( param->parameter_name == param_name ) {
			if( param->inherit_value_name ) {
				return groupInfoGetIntParameter( info, param->inherit_value_name, default_value );
			} else {
				return param->int_value;
			}
		}
		idx++;
	}
	return default_value;
}


const char *groupInfoGetStringParameter(GroupInfo *info, const char *param_name, const char *default_value)
{
	int idx = 0;
	OverrideParameter *parameter_value = NULL;

	if( stashFindPointer( s_OverrideParameters, param_name, &parameter_value )) {
		if( parameter_value )
			return parameter_value->string_value;
	}

	while (idx < GROUP_CHILD_MAX_PARAMETERS && info->parameters[idx].parameter_name)
	{
		GroupChildParameter* param = &info->parameters[idx];

		if( param->parameter_name == param_name ) {
			if( param->inherit_value_name ) {
				return groupInfoGetStringParameter( info, param->inherit_value_name, default_value );
			} else {
				return param->string_value;
			}
		}
		idx++;
	}
	return default_value;
}


void groupClearOverrideParameters( void )
{
	stashTableClearStruct( s_OverrideParameters, NULL, parse_OverrideParameter );
}

void groupSetOverrideIntParameter( const char* parameterName, int value )
{
	OverrideParameter *parameter_value = NULL;

	if( nullStr( parameterName )) {
		return;
	}
	if( !s_OverrideParameters ) {
		s_OverrideParameters = stashTableCreateAddress( 4 );
	}
	parameter_value = StructCreate( parse_OverrideParameter );
	parameter_value->int_value = value;
	stashAddressAddPointer( s_OverrideParameters, allocAddString( parameterName ), parameter_value, true );
}

void groupSetOverrideStringParameter( const char* parameterName, const char *value )
{
	OverrideParameter *parameter_value = NULL;

	if( nullStr( parameterName )) {
		return;
	}
	if( !s_OverrideParameters ) {
		s_OverrideParameters = stashTableCreateAddress( 4 );
	}
	parameter_value = StructCreate( parse_OverrideParameter );
	parameter_value->string_value = allocAddString(value);
	stashAddressAddPointer( s_OverrideParameters, allocAddString( parameterName ), parameter_value, true );
}

static void groupInfoSetParameter(GroupInfo *info, GroupChildParameter *parameter)
{
	int idx = 0;
	while (idx < GROUP_CHILD_MAX_PARAMETERS && info->parameters[idx].parameter_name)
	{
		idx++;
	}
	assert(idx < GROUP_CHILD_MAX_PARAMETERS);
	memcpy(&info->parameters[idx], parameter, sizeof(GroupChildParameter));
}

void applyDefToGroupInfo(GroupInfo *info, GroupInheritedInfo *inherited_info,
						 GroupDef *current_def, GroupDef *parent_def,
						 int idx_in_parent, bool is_client, bool is_drawable, 
						 bool in_spline, bool update_spline)
{
	GroupChild **current_def_children = groupGetChildren(current_def);
	Mat4 group_matrix;

	if (parent_def)
	{
		GroupChild **parent_def_children = groupGetChildren(parent_def);
        if (idx_in_parent >= 0 && idx_in_parent < eaSize(&parent_def_children))
        {
			info->always_use_seed = info->always_use_seed || parent_def_children[idx_in_parent]->always_use_seed;

            if (!in_spline && !current_def->property_structs.debris_field_properties && eaSize(&current_def_children) <= 1 && !info->always_use_seed)
                info->seed = parent_def_children[idx_in_parent]->seed; // I honestly don't remember why this case exists, but it's too late to change it. --TomY
            else
                info->seed = info->seed ^ parent_def_children[idx_in_parent]->seed;

            copyMat4(parent_def_children[idx_in_parent]->mat, group_matrix);
			scaleVec3(group_matrix[3], info->current_scale, group_matrix[3]);

			if (parent_def_children[idx_in_parent]->scale != 0 &&
				parent_def_children[idx_in_parent]->scale != 1)
			{
				info->uniform_scale *= parent_def_children[idx_in_parent]->scale;
				info->current_scale = parent_def_children[idx_in_parent]->scale;
			}
			else
			{
				info->current_scale = 1;
			}

			FOR_EACH_IN_EARRAY(parent_def_children[idx_in_parent]->simpleData.params, GroupChildParameter, parameter)
			{
				groupInfoSetParameter(info, parameter);
			}
			FOR_EACH_END;
        }
        else
        {
            info->seed = info->seed ^ (idx_in_parent * 7829);
            identityMat4(group_matrix);
        }

		if (current_def->property_structs.curve_gaps)
		{
			info->inherited_gaps = current_def->property_structs.curve_gaps->gaps;
		}
		else
		{
			if (current_def->property_structs.child_curve && !current_def->property_structs.child_curve->avoid_gaps)
			{
				info->inherited_gaps = NULL;
			}
		}

		if (parent_def->spline_params)
            info->spline = parent_def->spline_params[idx_in_parent];

		// The Physical "Child Select Param" overrides any child select properties on interactables
		if (!parent_def->property_structs.physical_properties.pcChildSelectParam) {
			if(parent_def->property_structs.interaction_properties || parent_def->property_structs.physical_properties.bIsChildSelect) {
				info->parent_entry_child_idx = idx_in_parent;
				info->visible_child = -1;
			}
		}

		if (parent_def->property_structs.building_properties)
			info->in_dynamic_object = 1;

		{
			if (parent_def->property_structs.physical_properties.bNoChildOcclusion)
			{
				info->parent_no_occlusion = true;
				info->no_occlusion = true;
			}
		}
	}
	else
	{
		identityMat4(group_matrix);
	}

	if (update_spline)
	{
		if (current_def->property_structs.curve)
		{
			Spline transformed_spline = { 0 };

			splineDestroy(&info->inherited_spline);
			StructCopyAll(parse_Spline, &current_def->property_structs.curve->spline, &transformed_spline);
			splineTransformMatrix(&transformed_spline, info->curve_matrix);
			curveCalculateChild(&transformed_spline, &info->inherited_spline, current_def->property_structs.child_curve, info->seed, NULL, unitmat);
			splineDestroy(&transformed_spline);
		}
		else if (parent_def && (parent_def->property_structs.curve || 
			(parent_def->property_structs.child_curve && parent_def->property_structs.child_curve->child_type == CURVE_CHILD_INHERIT)))
		{
			Spline spline = { 0 };
			curveCalculateChild(&info->inherited_spline, &spline, current_def->property_structs.child_curve, info->seed, current_def, group_matrix);
			splineDestroy(&info->inherited_spline);
			info->inherited_spline = spline;
		}
		else
		{
			splineDestroy(&info->inherited_spline);
		}
	}

	// use the visible child set by the previous iteration
	if (info->visible_child >= 0 && parent_def && idx_in_parent != info->visible_child)
		info->visible = false;

	// set the visible child for the next iteration
	if (current_def->property_structs.physical_properties.pcChildSelectParam)
		info->visible_child = groupInfoGetIntParameter(info, current_def->property_structs.physical_properties.pcChildSelectParam, 0);
	else if (current_def->property_structs.physical_properties.bIsChildSelect)
		info->visible_child = current_def->property_structs.physical_properties.iChildSelectIdx;
	else if ((current_def->property_structs.physical_properties.bRandomSelect) && eaSize(&current_def_children))
		info->visible_child = groupGetRandomChildIdx(current_def_children, info->seed);
	else
		info->visible_child = -1;

	if (current_def->property_structs.physical_properties.iTagID)
	{
		info->tag_id = current_def->property_structs.physical_properties.iTagID;
		assert(!(info->tag_id & (1<<31)));
	}


	if (!current_def->property_structs.physical_properties.bVisible)
		info->editor_only = true;
	if (current_def->property_structs.physical_properties.bHeadshotVisible)
		info->headshot_visible = true;
	applyCollisionToGroupInfo(info, &current_def->property_structs.physical_properties);

	{
		if (current_def->property_structs.physical_properties.bNoOcclusion)
			info->no_occlusion = true;
		if (current_def->property_structs.physical_properties.bDummyGroup)
			info->dummy_group = true;
		if (current_def->property_structs.physical_properties.bMapSnapHidden
		||  current_def->property_structs.physical_properties.bMapSnapFade)	//SIP TODO: make bMapSnapFade only darken things.
			info->map_snap_hidden = true;
		if (current_def->property_structs.physical_properties.oLodProps.bHighFillDetail && !info->low_detail && !info->high_detail)
		{
			info->high_fill_detail = true;
			info->collisionFilterBits = 0;
		}
		if (current_def->property_structs.physical_properties.oLodProps.bHighDetail && !info->low_detail && !info->high_fill_detail)
		{
			info->high_detail = true;
			info->collisionFilterBits = 0;
		}
		if (current_def->property_structs.physical_properties.oLodProps.bLowDetail && !info->high_detail && !info->high_fill_detail)
			info->low_detail = true;
		if (current_def->property_structs.physical_properties.bDontCastShadows)
			info->no_shadow_cast = true;
		if (current_def->property_structs.physical_properties.bDontReceiveShadows)
			info->no_shadow_receive = true;
		if (current_def->property_structs.physical_properties.bDoubleSidedOccluder)
			info->double_sided_occluder = true;
		if (current_def->property_structs.physical_properties.bIsDebris)
			info->is_debris = true;
		if (current_def->property_structs.physical_properties.bRoomExcluded)
			info->exclude_from_room = true;
		if (current_def->property_structs.physical_properties.bForceTrunkWind)
			info->force_trunk_wind = true;
	}

	if (current_def->property_structs.physical_properties.bNoVertexLighting)
		info->no_vertex_lighting = true;
	if (current_def->property_structs.physical_properties.bUseCharacterLighting)
		info->use_character_lighting = true;

	if (current_def->property_structs.room_properties &&
		current_def->property_structs.room_properties->eRoomType == WorldRoomType_Room &&
		current_def->property_structs.room_properties->bOccluder)
		info->no_occlusion = true;

	if (is_client && is_drawable)
	{
		info->color[3] *= current_def->property_structs.physical_properties.fAlpha;
		info->lod_scale *= current_def->property_structs.physical_properties.oLodProps.fLodScale;
		if (current_def->property_structs.physical_properties.oLodProps.bIgnoreLODOverride)
			info->ignore_lod_override = true;

		if (current_def->group_flags & GRP_HAS_TINT)
		{
			copyVec3(current_def->tint_color0, info->color);
			info->apply_tint_offset = true;
		}
		if (current_def->group_flags & GRP_HAS_TINT_OFFSET)
			addVec3(current_def->tint_color0_offset, info->tint_offset, info->tint_offset);

		// textures and materials
		if (current_def->group_flags & GRP_HAS_MATERIAL_REPLACE)
			info->material_replace = current_def->replace_material_name;

		if (current_def->group_flags & GRP_HAS_MATERIAL_PROPERTIES)
			eaPushEArray(&inherited_info->material_property_list, &current_def->material_properties);

		if (current_def->group_flags & GRP_HAS_MATERIAL_SWAPS)
		{
			int j;
			for (j = 0; j < eaSize(&current_def->material_swaps); ++j)
			{
				eaPush(&inherited_info->material_swap_list, current_def->material_swaps[j]->orig_name);
				eaPush(&inherited_info->material_swap_list, current_def->material_swaps[j]->replace_name);
			}
		}

		if (current_def->group_flags & GRP_HAS_TEXTURE_SWAPS)
		{
			int j;
			for (j = 0; j < eaSize(&current_def->texture_swaps); ++j)
			{
				eaPush(&inherited_info->texture_swap_list, current_def->texture_swaps[j]->orig_name);
				eaPush(&inherited_info->texture_swap_list, current_def->texture_swaps[j]->replace_name);
			}
		}

		if (current_def->property_structs.lod_override)
			info->lod_override = current_def->property_structs.lod_override;
	}

	applyInteractToGroupInfo(info,inherited_info,current_def,parent_def);
}

__forceinline static bool needsEntry(SA_PARAM_NN_VALID GroupInfo *info, GroupDef *def, GroupDef *parent_def, bool in_editor, bool is_client)
{
	bool is_drawable, has_volume, create_collision;
	Model *model;
	WorldPlanetProperties *planet;
	WorldAnimationProperties *animation;
	bool no_coll = (!info->collisionFilterBits), editor_only = info->editor_only;

	if (!info->visible)
		return false;

    if (eafSize(&info->inherited_spline.spline_points) > 0)
        return false;

	model = def->model;
	planet = def->property_structs.planet_properties;
	animation = def->property_structs.animation_properties;
	has_volume = !!(def->property_structs.volume);
	is_drawable = model || planet || has_volume; // this will be refined later


	create_collision = in_editor || !no_coll;

	if (def->property_structs.physical_properties.bOcclusionOnly && model && modelIsTemp(model))
	{
		// force collisions off for occlusion models generated by curves
		create_collision = false;
	}

	if (groupIsVolumeType(def, "Occluder") || def->property_structs.physical_properties.bOcclusionOnly)
	{
		// occluders need to be traversed out of the editor as well
		no_coll = true;
		editor_only = false;
	}

	is_drawable = model || planet || (in_editor && has_volume);
	if (no_coll && !is_client)
		is_drawable = false;
	if (!is_drawable)
		create_collision = false;
	if (editor_only && !in_editor)
		is_drawable = false;

	// Parent defs can foist entries in a few cases
	if (parent_def) {
		if (groupHasMotionProperties(parent_def)) {
			return true;
		}
		if (SAFE_MEMBER(parent_def->property_structs.interaction_properties, pChildProperties)) {
			return true;
		}
	}
	if(		!is_client &&
			(def->property_structs.physical_properties.bCivilianGenerator || 
			def->property_structs.physical_properties.bForbiddenPosition ||
			def->property_structs.interact_location_properties ))
		return true;

	if (def->property_structs.physical_properties.bHandPivot ||
		def->property_structs.physical_properties.bMassPivot ||
		def->property_structs.physical_properties.eCarryAnimationBit)
		return true;
	if ((def->property_structs.interaction_properties || animation) && !info->dummy_group)
		return true;
	if (create_collision && !info->dummy_group)
		return true;
	if (def->property_structs.volume && !info->dummy_group)
		return true;
	if (def->property_structs.room_properties && !info->dummy_group)
		return true;
	if (def->property_structs.physical_properties.iTagID && !info->dummy_group)
		return true;
	if (!info->dummy_group && // JDJ: remove the dummy group check
		(def->is_layer || groupHasScope(def) ||
		 def->property_structs.physical_properties.bNamedPoint || 
		 def->property_structs.encounter_hack_properties || def->property_structs.encounter_properties || 
		 def->property_structs.spawn_properties || def->property_structs.patrol_properties ||
		 def->property_structs.trigger_condition_properties || def->property_structs.layer_fsm_properties))
		return true;

	if (is_client)
	{
		if (def->property_structs.physical_properties.iWeldInstances)
			return true;
		if (def->property_structs.fx_properties && def->property_structs.fx_properties->pcName)
			return true;
		if (wl_state.update_light_func && wl_state.remove_light_func && groupHasLight(def))
			return true;
		if (wl_state.create_sound_func && wl_state.remove_sound_func && wl_state.sound_radius_func && !info->dummy_group && 
			def->property_structs.sound_sphere_properties)
			return true;
		if (is_drawable)
			return true;
		if (def->property_structs.wind_source_properties)
			return true;
		if (def->property_structs.path_node_properties)
			return true;
	}

	return false;
}

bool groupHasLight(GroupDef *def)
{
	if(!def || !def->property_structs.light_properties)
		return false;
	if(def->property_structs.light_properties->eLightType == WleAELightController)
		return false;
	if(def->property_structs.light_properties->eLightType == WleAELightNone)
		return false;
	return true;
}

bool groupHasMotionProperties(GroupDef* def)
{
	if( !def || !def->property_structs.interaction_properties ) {
		return false;
	} else {
		int it;
		for( it = 0; it != eaSize( &def->property_structs.interaction_properties->eaEntries ); ++it ) {
			WorldInteractionPropertyEntry* entry = def->property_structs.interaction_properties->eaEntries[ it ];
			if( entry->pMotionProperties ) {
				return true;
			}
		}

		return false;
	}
}

bool groupIsDynamic(GroupDef *def)
{
	return (def->property_structs.debris_field_properties != NULL) ||
		(def->property_structs.encounter_properties != NULL) ||
		(def->property_structs.patrol_properties != NULL) ||
		(def->property_structs.building_properties != NULL);
}


static void groupRefDynamicChildrenRecurse(GroupDef *parent, GroupChild *child)
{
	GroupDef *child_def;
	if (!child)
		return;
	if ((child_def = groupChildGetDef(parent, child, false)) != NULL)
	{
		int i;
		for (i = 0; i < eaSize(&child_def->children); i++)
			groupRefDynamicChildrenRecurse(child_def, child_def->children[i]);
		if (child_def->is_dynamic)
		{
			child_def->dynamic_ref_count++;
		}
	}
}

static void groupFreeDynamicChildrenRecurse(GroupDef *parent, GroupChild *child)
{
	GroupDef *child_def;
	if (!child)
		return;
	if ((child_def = groupChildGetDef(parent, child, false)) != NULL)
	{
		int i;
		for (i = 0; i < eaSize(&child_def->children); i++)
			groupFreeDynamicChildrenRecurse(child_def, child_def->children[i]);
		if (child_def->is_dynamic)
		{
			if ((--child_def->dynamic_ref_count) == 0)
				groupDefFree(child_def);
		}
	}
}

//Update groupIsDynamic if you add a new type
GroupChild **groupGetDynamicChildren(GroupDef *def, GroupTracker *tracker, GroupInfo *info, const Mat4 in_mat)
{
	int i;
	GroupChild **ret = NULL;
	Mat4 world_mat;
	if (in_mat)
		copyMat4(in_mat, world_mat);
	else
		identityMat4(world_mat);

	if(def->property_structs.patrol_properties)
	{
		if (tracker && !wl_state.debug.hide_encounter_2_patrols && wl_state.wle_patrol_point_get_mat_func)
		{
			GroupDef *arrow_def = objectLibraryGetGroupDefByName("core_icons_patrol", true);
			GroupDef *point_def = objectLibraryGetGroupDefByName("core_icons_X", true);
			if(arrow_def && point_def)
			{
				WorldPatrolProperties *patrol = def->property_structs.patrol_properties;
				for ( i=0; i < eaSize(&patrol->patrol_points); i++ )
				{
					WorldPatrolPointProperties *point = patrol->patrol_points[i];
					GroupChild *new_child = StructCreate(parse_GroupChild);
					wl_state.wle_patrol_point_get_mat_func(patrol, i, world_mat, new_child->mat, false, false, true);
					if(wlePatrolPointIsEndpoint(patrol, i)) {
						new_child->name_uid = point_def->name_uid;
						new_child->name = point_def->name_str;
					} else {
						new_child->name_uid = arrow_def->name_uid;
						new_child->name = arrow_def->name_str;
					}
					eaPush(&ret, new_child);
				}
			}					
		}
	}
	else if (def->property_structs.encounter_properties)
	{
		if (tracker && !wl_state.debug.hide_encounter_2_actors && wl_state.wle_is_actor_disabled_func)
		{
			GroupDef *actor_def = objectLibraryGetGroupDefByName("core_icons_humanoid", true);
			GroupDef *actor_disabled_def = objectLibraryGetGroupDefByName("core_icons_humanoid_disabled", true);
			if(actor_def && actor_disabled_def)
			{
				for ( i=0; i < eaSize(&def->property_structs.encounter_properties->eaActors); i++ )
				{
					WorldActorProperties *actor = def->property_structs.encounter_properties->eaActors[i];
					GroupChild *new_child = StructCreate(parse_GroupChild);
					Mat4 actorMat;
					bool disabled = wl_state.wle_is_actor_disabled_func(def->property_structs.encounter_properties, actor);
					createMat3YPR(actorMat, actor->vRot);
					copyVec3(actor->vPos, actorMat[3]);
					mulMat4(world_mat, actorMat, new_child->mat);
					if(!disabled) {
						new_child->name_uid = actor_def->name_uid;
						new_child->name = actor_def->name_str;
					} else {
						new_child->name_uid = actor_disabled_def->name_uid;
						new_child->name = actor_disabled_def->name_str;
					}
					eaPush(&ret, new_child);
				}
			}
		}
	}
	else if (def->property_structs.debris_field_properties)
	{
		WorldDebrisFieldGenProps gen_props = { 0 };
		GroupChild **children = groupGetChildren(def);
		GroupDef *fallback_def;
		F32 coll_rad = wlDebrisFieldFindColRad(def, def->property_structs.debris_field_properties, &fallback_def);
		int point_count;
		
		if (tracker)
			point_count = wlDebrisFieldFindLocations(def->property_structs.debris_field_properties, &gen_props, def, 
				(tracker->debris_cont_tracker) ? tracker->debris_cont_tracker->debris_excluders : NULL, 
				world_mat, trackerGetSeed(tracker), coll_rad);
		else
			point_count = wlDebrisFieldFindLocations(def->property_structs.debris_field_properties, &gen_props, def, 
				info ? info->debris_excluders : NULL, world_mat, info ? info->seed : 0, coll_rad);

		if (point_count > 0 && (eaSize(&children) > 0 || fallback_def))
		{
			for (i = 0; i < point_count; i++)
			{
				GroupDef *new_child_def = NULL;
				GroupChild *new_child = StructCreate(parse_GroupChild);
				Mat4 debris_mat;
				new_child->seed = wlDebrisFieldGetLocationAndSeed(def->property_structs.debris_field_properties, &gen_props, i, debris_mat);
				mulMat4(world_mat, debris_mat, new_child->mat);
				if (eaSize(&children) > 0)
				{
					new_child_def = groupChildGetDef(def, children[i%eaSize(&children)], false);
				}
				if (!new_child_def)
					new_child_def = fallback_def;
				new_child->always_use_seed = true;
				new_child->name_uid = new_child_def->name_uid;
				new_child->name = new_child_def->name_str;

				eaPush(&ret, new_child);
			}
		}

		wlDebrisFieldCleanUp(&gen_props);
	}
	else if (def->property_structs.building_properties)
	{
		U32 parent_seed = 1;
		if (tracker)
			parent_seed = trackerGetSeed(tracker);
		else if (info)
			parent_seed = info->seed;
		ret = generateBuildingChildren(def, world_mat, parent_seed);
	}

	for (i = 0; i < eaSize(&ret); i++)
	{
		groupRefDynamicChildrenRecurse(def, ret[i]);
	}

	return ret;
}

void groupFreeDynamicChildren(GroupDef *parent, GroupChild **children)
{
	int i;
	for (i = 0; i < eaSize(&children); i++)
	{
		groupFreeDynamicChildrenRecurse(parent, children[i]);
	}
	eaDestroyStruct(&children, parse_GroupChild);
}

static int traverse_id = 0;

static void groupTreeTraverseRecurse(GroupInfo *parent_info, GroupInheritedInfo *inherited_info,
											  GroupTreeTraverserCallback pre_callback, GroupTreeTraverserCallback post_callback,
											  void *userdata, const Mat4 world_mat,
											  bool is_client, bool in_spline, bool in_editor, bool skip_debris_field_cont)
{
	GroupInfo info = *parent_info;
	int i, def_idx = eaSize(&inherited_info->parent_defs)-1;
	int material_swap_count = eaSize(&inherited_info->material_swap_list);
	int texture_swap_count = eaSize(&inherited_info->texture_swap_list);
	int material_property_count = eaSize(&inherited_info->material_property_list);
	bool needs_entry;
	GroupDef *def, *parent_def;
	Mat4 instance_mat;
	Vec3 instance_tint;
	GroupChild **def_children;
	bool created_debris_excluders=false;

	ANALYSIS_ASSUME(def_idx < 0 || inherited_info->parent_defs);

	info.parent_info = parent_info;
	memset(&info.inherited_spline, 0, sizeof(Spline));
	StructCopyAll(parse_Spline, &parent_info->inherited_spline, &info.inherited_spline);

	info.traverse_id = ++traverse_id;

	def = (def_idx >= 0) ? inherited_info->parent_defs[def_idx] : NULL;

	if (!def)
	{
		StructDeInit(parse_Spline, &info.inherited_spline);
		return;
	}

	def_children = groupGetChildren(def);

	if (def->property_structs.curve)
		copyMat4(world_mat, info.curve_matrix);

	if(!skip_debris_field_cont && !info.debris_excluders && def->property_structs.physical_properties.bIsDebrisFieldCont)
	{
		groupTreeTraverse(info.layer, def, world_mat, wlDebrisFieldTraversePre, wlDebrisFieldTraversePost, &info.debris_excluders, true, true);
		if(info.debris_excluders)
			created_debris_excluders = true;
	}

	info.parent_no_occlusion = parent_info->no_occlusion;

	// apply def properties
	parent_def = (def_idx > 0) ? inherited_info->parent_defs[def_idx-1] : NULL;
	applyDefToGroupInfo(&info, inherited_info, def, parent_def, 
		inherited_info->idxs_in_parent[def_idx], is_client, true, in_spline, true);

	needs_entry = needsEntry(&info, def, parent_def, in_editor, is_client);
	
	groupGetBounds(def, info.spline, world_mat, info.current_scale, info.world_min, info.world_max, info.world_mid, &info.radius, info.world_matrix);

	if (SAFE_MEMBER(def->property_structs.genesis_properties, iNodeType))
		info.node_height = world_mat[3][1];

	if (!info.has_fade_node && (def->property_structs.physical_properties.oLodProps.bFadeNode || def->property_structs.building_properties))
	{
		copyVec3(info.world_mid, info.fade_mid);
		info.fade_radius = info.radius;
		info.has_fade_node = true;
	}

	// create entry
	if (pre_callback)
	{
		if (!pre_callback(userdata, def, &info, inherited_info, needs_entry))
		{
			// reset swap lists
			eaSetSize(&inherited_info->material_swap_list, material_swap_count);
			eaSetSize(&inherited_info->texture_swap_list, texture_swap_count);
			eaSetSize(&inherited_info->material_property_list, material_property_count);

			StructDeInit(parse_Spline, &info.inherited_spline);
			return;
		}
	}

	if (info.instance_info)
	{
		copyMat4(info.instance_info->world_matrix, instance_mat);
		copyVec3(info.instance_info->tint, instance_tint);
	}

    if (!def->property_structs.curve && eafSize(&info.inherited_spline.spline_points) > 0 && !in_spline &&
		(!def->property_structs.child_curve || def->property_structs.child_curve->child_type != CURVE_CHILD_INHERIT))
    {
        // spline recurse
        F32 distance_offset = 0;
        bool curved_spline = def->property_structs.child_curve ? def->property_structs.child_curve->deform : false; 
        int count = eafSize(&info.inherited_spline.spline_points)/3 - (curved_spline ? 1 : 0);
        Spline inherited_backup = info.inherited_spline;
        Mat4 parent_matrix;
        GroupInfo child_info;
        GroupDef *dummy_def = StructCreate(parse_GroupDef);
        GroupChild **parent_children = groupGetChildren(inherited_info->parent_defs[def_idx-1]);
        U32 current_uid;
		MersenneTable *table = mersenneTableCreate(1);
		memset(&info.inherited_spline, 0, sizeof(Spline)); // Clear spline after one generation

		if (def_idx > 0)
        {
            // Undo the transform, since it is applied in the curve
			Mat4 temp, temp2 = { 0 };
			Mat4 temp_matrix;
			identityMat3(temp);
            transposeMat3Copy(parent_children[inherited_info->idxs_in_parent[def_idx]]->mat, temp2);
            scaleVec3(parent_children[inherited_info->idxs_in_parent[def_idx]]->mat[3], -1, temp[3]);
            mulMat4Inline(world_mat, temp2, temp_matrix);
            mulMat4Inline(temp_matrix, temp, parent_matrix);
        }
        else
        {
            copyMat4(world_mat, parent_matrix);
        }

        child_info = *parent_info;
        child_info.seed = info.seed;
		child_info.in_dynamic_object = 1;

        dummy_def->name_str = allocAddString("Spline Dummy String");
		groupDefSetChildCount(dummy_def, NULL, count);

        current_uid = parent_children[inherited_info->idxs_in_parent[def_idx]]->uid_in_parent;
        
        for (i = 0; i < count; i++)
        {
            Mat4 geom_matrix;
            GroupSplineParams *params;
			GroupDef *child_def = curveGetGeometry(unitmat, def, &inherited_backup, info.inherited_gaps, info.curve_matrix,
                                                   def->property_structs.child_curve ? def->property_structs.child_curve->uv_scale : 1.f,
                                                   def->property_structs.child_curve ? def->property_structs.child_curve->uv_rot : 0.f,
                                                   def->property_structs.child_curve ? def->property_structs.child_curve->stretch : 1.f,
                                                   i, geom_matrix, &params, &distance_offset, info.uniform_scale);
            if (child_def)
            {
                // Recurse as a sibling of this def
                dummy_def->children[i]->uid_in_parent = current_uid;
                dummy_def->children[i]->seed = randomMersenneInt(table);
                
                inherited_info->parent_defs[eaSize(&inherited_info->parent_defs)-1] = dummy_def;

                eaPush(&inherited_info->parent_defs, child_def);
                eaiPush(&inherited_info->idxs_in_parent, i); 

                child_info.spline = params;

                groupTreeTraverseRecurse(&child_info, inherited_info, pre_callback, post_callback, userdata, geom_matrix, is_client, true, in_editor, skip_debris_field_cont);

                eaPop(&inherited_info->parent_defs);
                eaiPop(&inherited_info->idxs_in_parent);
            }
			else
			{
				eaPush(&dummy_def->children, NULL);
			}
        }
		mersenneTableFree(table);
		eaDestroyStruct(&dummy_def->children, parse_GroupChild);
        splineDestroy(&inherited_backup);
		StructDestroy(parse_GroupDef, dummy_def);
    }
	else if (groupIsDynamic(def))
	{
		GroupChild **child_list = groupGetDynamicChildren(def, NULL, &info, world_mat);
		if (child_list)
		{
			GroupInfo child_info;
			GroupChild **parent_children = (def_idx >= 1) ? groupGetChildren(inherited_info->parent_defs[def_idx-1]) : NULL;
			GroupDef *dummy_def = StructCreate(parse_GroupDef);
			U32 current_uid;
			child_info = info;
			child_info.seed = info.seed;
			child_info.in_dynamic_object = 1;

			dummy_def->name_str = allocAddString("Dynamic Dummy String");
			groupDefSetChildCount(dummy_def, NULL, eaSize(&child_list));

			current_uid = parent_children ? parent_children[inherited_info->idxs_in_parent[def_idx]]->uid_in_parent : 1;

			for (i = 0; i < eaSize(&child_list); i++ )
			{
				GroupChild *child = child_list[i];
				GroupDef *child_def = groupChildGetDef(def, child, false);
				if (child_def)
				{
					GroupDef *old_parent;
					// Recurse as a sibling of this def

					dummy_def->children[i]->uid_in_parent = current_uid;
					dummy_def->children[i]->seed = child->seed;

					old_parent = inherited_info->parent_defs[eaSize(&inherited_info->parent_defs)-1];
					inherited_info->parent_defs[eaSize(&inherited_info->parent_defs)-1] = dummy_def;

					eaPush(&inherited_info->parent_defs, child_def);
					eaiPush(&inherited_info->idxs_in_parent, i); 

					groupTreeTraverseRecurse(&child_info, inherited_info, pre_callback, post_callback, userdata, child->mat, is_client, in_spline, in_editor, skip_debris_field_cont);

					eaPop(&inherited_info->parent_defs);
					eaiPop(&inherited_info->idxs_in_parent);
					inherited_info->parent_defs[eaSize(&inherited_info->parent_defs)-1] = old_parent;
				}
			}
			eaDestroyStruct(&dummy_def->children, parse_GroupChild);
			groupFreeDynamicChildren(def, child_list);
			StructDestroy(parse_GroupDef, dummy_def);
		}
	}
    else
    {
        // normal recurse
        for (i = 0; i < eaSize(&def_children); ++i)
        {
            GroupDef *child_def = groupChildGetDef(def, def_children[i], false);
            Mat4 child_mat, temp_mat;
            if (!child_def)
                continue;
			copyMat3(def_children[i]->mat, temp_mat);
			scaleVec3(def_children[i]->mat[3], info.current_scale, temp_mat[3]);
            mulMat4Inline(world_mat, temp_mat, child_mat);
            eaPush(&inherited_info->parent_defs, child_def);
            eaiPush(&inherited_info->idxs_in_parent, i);

            if (info.instance_info)
            {
                mulMat4Inline(instance_mat, def_children[i]->mat, info.instance_info->world_matrix);
                if (child_def->group_flags & GRP_HAS_TINT)
                    mulVecVec3(instance_tint, child_def->tint_color0, info.instance_info->tint);
            }

            groupTreeTraverseRecurse(&info, inherited_info, pre_callback, post_callback, userdata, child_mat, is_client, in_spline, in_editor, skip_debris_field_cont);

            eaPop(&inherited_info->parent_defs);
            eaiPop(&inherited_info->idxs_in_parent);
        }
    }

	if (info.instance_info)
	{
		copyMat4(instance_mat, info.instance_info->world_matrix);
		copyVec3(instance_tint, info.instance_info->tint);
	}

	if (post_callback)
	{
		post_callback(userdata, def, &info, inherited_info, needs_entry);
	}

	// reset swap lists
	eaSetSize(&inherited_info->material_swap_list, material_swap_count);
	eaSetSize(&inherited_info->texture_swap_list, texture_swap_count);
	eaSetSize(&inherited_info->material_property_list, material_property_count);

	if(created_debris_excluders)
		eafDestroy(&info.debris_excluders);
	StructDeInit(parse_Spline, &info.inherited_spline);
}

void groupTreeTraverse(ZoneMapLayer *layer, GroupDef *root_def, const Mat4 world_matrix, 
						   GroupTreeTraverserCallback pre_callback, GroupTreeTraverserCallback post_callback, 
						   void *userdata, bool in_editor, bool skip_debris_field_cont)
{
	GroupInfo info;
	GroupInheritedInfo inherited_info = { 0 };

	if (!world_matrix)
		world_matrix = unitmat;

	traverse_id = 0;

	initGroupInfo(&info, layer);
	info.region = layer ? layerGetWorldRegion(layer) : worldGetWorldRegionByPos(zerovec3);

	eaPush(&inherited_info.parent_defs, root_def);
	eaiPush(&inherited_info.idxs_in_parent, -1);
	groupTreeTraverseRecurse(&info, &inherited_info, pre_callback, post_callback, userdata,
		world_matrix, wlIsClient(), false, in_editor, skip_debris_field_cont);

	groupInheritedInfoDestroy(&inherited_info);
}

void groupInheritedInfoDestroy(GroupInheritedInfo *info)
{
	eaDestroy(&info->parent_defs);
	eaiDestroy(&info->idxs_in_parent);
	eaDestroy(&info->material_swap_list);
	eaDestroy(&info->texture_swap_list);
	eaDestroy(&info->material_property_list);
}

// This does a linear search and is not smart at all, so should only be used for very specific purposes.
GroupChild * groupGetChildByName( GroupDef * pGroup, const char * pchName )
{
	const int iNumChildren = eaSize(&pGroup->children);
	int i;
	for (i = 0; i < iNumChildren; i++)
	{
		GroupDef * pDef = groupChildGetDef(pGroup,pGroup->children[i],false);
		if (strcmp(pDef->name_str,pchName) == 0)
		{
			return pGroup->children[i];
		}
	}

	return NULL;
}

#include "AutoGen/grouputil_c_ast.c"
