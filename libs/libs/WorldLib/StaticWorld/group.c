#include "group.h"

#include "WorldGridPrivate.h"
#include "wlState.h"
#include "wlEncounter.h"
#include "wlAutoLOD.h"
#include "wlModelLoad.h"
#include "dynWind.h"
#include "dynFxInfo.h"
#include "bounds.h"

#include "StringCache.h"
#include "timing.h"
#include "rgb_hsv.h"
#include "EString.h"
#include "StateMachine.h"
#include "SimpleParser.h"
#include "MultiVal.h"
#include "fileutil2.h"
#include "logging.h"
#include "tokenstore.h"
#include "GimmeDLLWrapper.h"
#include "wlGenesisMissionsGameStructs.h"

#include "ActivityLogEnums.h"

#include "autogen/worldLibEnums_h_ast.h"
#include "autogen/group_h_ast.c"
#include "autogen/wlLight_h_ast.c"
#include "autogen/ActivityLogEnums_h_ast.h"
#include "autogen/wlGroupPropertyStructs_h_ast.c"

AUTO_RUN_ANON(memBudgetAddMapping(__FILE__, BUDGET_World););
AUTO_RUN_ANON(memBudgetAddMapping("wlGroupPropertyStructs.h", BUDGET_World););
AUTO_RUN_ANON(memBudgetAddMapping("WorldVariable.h", BUDGET_GameSystems););
AUTO_RUN_ANON(memBudgetAddMapping("wlLight.h", BUDGET_World););

static WorldLightProperties *g_pDefualtLightProperties = NULL;

const WorldLightProperties* groupGetDefaultLightProperties()
{
	if(!g_pDefualtLightProperties)
		g_pDefualtLightProperties = StructCreate(parse_WorldLightProperties);
	return g_pDefualtLightProperties;
}

Model *groupGetModel(GroupDef *def)
{
	groupSetModelRecursive(def, false);

	return def->model;
}

Model *groupModelFind(const char *name, WLUsageFlags extra_use_flags)
{
	Model *model;
	const char* cs;

	if (!name)
		return NULL;

	PERFINFO_AUTO_START_FUNC();

	cs = strrchr(name,'/');
	if (!cs++)
		cs = name;

	model = modelFind(cs, true, WL_FOR_WORLD|extra_use_flags);
	PERFINFO_AUTO_STOP_FUNC();
	return model;
}

U32 defContainCount(GroupDef *container,GroupDef *def)
{
	int total = 0, i;

	if (!container)
		return 0;
	if (container == def)
		return 1;
	for(i=0;i<eaSize(&container->children);i++)
	{
		GroupDef *child_def = groupChildGetDef(container, container->children[i], false);
		if (child_def)
			total += defContainCount(child_def,def);
	}
	return total;
}

static void groupGetDrawVisDistRecursiveInternal(GroupDef *def, const WorldLODOverride *lod_override, F32 lod_scale, F32 *near_lod_near_dist, F32 *far_lod_near_dist, F32 *far_lod_far_dist)
{
	F32 near_lod_near = FLT_MAX, far_lod_near = 0, far_lod_far = 0;
	int i;
	GroupChild **def_children;

	if (!def)
		return;

	if (def->property_structs.lod_override)
		lod_override = def->property_structs.lod_override;

	lod_scale *= def->property_structs.physical_properties.oLodProps.fLodScale;
	if (def->property_structs.physical_properties.oLodProps.bIgnoreLODOverride)
		lod_override = NULL;

	def_children = groupGetChildren(def);

	if (def->model)
	{
		far_lod_far = loddistFromLODInfo(def->model, NULL, SAFE_MEMBER(lod_override, lod_distances), lod_scale, def->model_scale, true, &near_lod_near, &far_lod_near);
		if (!far_lod_far)
			far_lod_near = far_lod_far = def->model->radius * 10;
	}
	if (def->property_structs.planet_properties)
	{
		MAX1(far_lod_far, 100 * def->property_structs.planet_properties->geometry_radius);
	}
	if (def->property_structs.volume)
	{
		F32 radius;
		if(def->property_structs.volume->eShape == GVS_Sphere) {
			radius = def->property_structs.volume->fSphereRadius;
		} else {
			radius = (0.5f * distance3(def->property_structs.volume->vBoxMin, def->property_structs.volume->vBoxMax));
		}
		MAX1(far_lod_far, 40 * radius);
	}

	if (def->property_structs.fx_properties)
	{
		const char *fx_name = def->property_structs.fx_properties->pcName;
		if (fx_name)
		{
			F32 max_vis_distance = 300.0f;
			if (dynFxInfoExists(fx_name))
			{
				// figure out what the visdist should be for this fx emitter
				F32 fx_distance = dynFxInfoGetDrawDistance(fx_name);
				if (fx_distance > 0.0f)
					max_vis_distance = fx_distance;
			}
			MAX1(far_lod_far, max_vis_distance);
		}
	}

	if (def->property_structs.light_properties)
	{
		F32 light_radius = fillF32FromStr(groupDefFindProperty(def, "LightRadius"), 0);
		MAX1(far_lod_far, light_radius * 60);
	}


	if (!far_lod_near)
		far_lod_near = far_lod_far;

	MIN1(*near_lod_near_dist, near_lod_near);
	MAX1(*far_lod_near_dist, far_lod_near);
	MAX1(*far_lod_far_dist, far_lod_far);

	for (i = 0; i < eaSize(&def_children); ++i)
	{
		GroupDef *child_def = groupChildGetDef(def, def_children[i], false);
		if (child_def)
			groupGetDrawVisDistRecursiveInternal(child_def, lod_override, lod_scale, near_lod_near_dist, far_lod_near_dist, far_lod_far_dist);
	}
}

void groupGetDrawVisDistRecursive(GroupDef *def, const WorldLODOverride *lod_override, F32 lod_scale, F32 *near_lod_near_dist, F32 *far_lod_near_dist, F32 *far_lod_far_dist)
{
	*near_lod_near_dist = FLT_MAX;
	*far_lod_near_dist = 0;
	*far_lod_far_dist = 0;
	groupGetDrawVisDistRecursiveInternal(def, lod_override, lod_scale, near_lod_near_dist, far_lod_near_dist, far_lod_far_dist);
	MIN1(*near_lod_near_dist, *far_lod_near_dist);
}

// helper function for my visual only version
void groupTransformMinMax(const Mat4 mat,F32 scale,Vec3 min,Vec3 max)
{
	Vec3	minScale,maxScale;

	if (scale == 0)
		scale = 1;

	scaleVec3(min, scale, minScale);
	scaleVec3(max, scale, maxScale);
	mulBoundsAA(minScale, maxScale, mat, min, max);
}

void groupGetBoundsMinMax(GroupDef *def,const Mat4 mat,F32 scale,Vec3 min,Vec3 max)
{
	int		j;
	Vec3	minScale,maxScale;
	Vec3	minCheck,maxCheck;

	if (def==NULL)
		return;

	if (scale == 0)
		scale = 1;

	scaleVec3(def->bounds.min, scale, minScale);
	scaleVec3(def->bounds.max, scale, maxScale);
	mulBoundsAA(minScale, maxScale, mat, minCheck, maxCheck);

	// Check if the bounding box is smaller than the diameter.
	for(j=0;j<3;j++)
	{
		if(maxCheck[j] - minCheck[j] < def->bounds.radius*scale*2)
			break;
	}
	if(j == 3)
	{
		// The bounding box is LARGER than the diameter in all dimensions, so use the radius.
		Vec3 mi;
		mulVecMat4(def->bounds.mid,mat,mi);
		for(j=0;j<3;j++)
		{
			max[j] = MAX(max[j],mi[j] + def->bounds.radius * scale);
			min[j] = MIN(min[j],mi[j] - def->bounds.radius * scale);
		}
	}
	else
	{
		for(j=0;j<3;j++)
		{
			max[j] = MAX(max[j],maxCheck[j]);
			min[j] = MIN(min[j],minCheck[j]);
		}
	}
}

typedef struct GroupCurveRecurseInfo
{
	Vec3 min;
	Vec3 max;
} GroupCurveRecurseInfo;

static bool groupSetBoundsCurveRecurseCallback(GroupCurveRecurseInfo *rec_info, GroupDef *def, GroupInfo *info, GroupInheritedInfo *inherited_info, bool needs_entry)
{
	vec3RunningMin(info->world_min, rec_info->min);
	vec3RunningMax(info->world_max, rec_info->max);
	return true;
}

static void _updateMaxExtents(Vec3 const vLocalMin,Vec3 const vLocalMax,Vec3 const vLocalSphereCenter,F32 fLocalRadius,Vec3 vMin,Vec3 vMax,Vec3 vCenter,F32 * pfMaxRadius)
{
	if (*pfMaxRadius >= 0.0f)
		*pfMaxRadius = sphereUnion(vCenter,*pfMaxRadius,vLocalSphereCenter,fLocalRadius,vCenter);
	else
	{
		*pfMaxRadius = fLocalRadius;
		copyVec3(vLocalSphereCenter,vCenter);
	}
	vec3RunningMin(vLocalMin, vMin);
	vec3RunningMax(vLocalMax, vMax);
}

// gets the raw minimum bounds data for the object this GroupDef contributes at this level.  Returns false if none
static bool groupCalcNodeItemLocalBounds(GroupDef const *group,Mat4 const matrix,F32 fScale,Vec3 vMin,Vec3 vMax,Vec3 vSphereCenter,F32 *pfRadius)
{
	bool bFound = false;
	float fMaxRadius = -1.0f;
	int i;

	// in order to calculate an accurate aggregate radius, the midpoint from which the radius was calculated is germane
	setVec3(vMin,8e8f,8e8f,8e8f);
	setVec3(vMax,-8e8f,-8e8f,-8e8f);
	if (group->model || group->model_name)
	{
		// this case is slightly simpler than below, because it can only be first
		if (group->model)
		{
			mulVecVec3(group->model->min, group->model_scale, vMin);
			mulVecVec3(group->model->max, group->model_scale, vMax);
			bFound = true;
		}
		else
		{
			ModelHeader *header = wlModelHeaderFromName(group->model_name);
			if (header)
			{
				mulVecVec3(header->min, group->model_scale, vMin);
				mulVecVec3(header->max, group->model_scale, vMax);
				bFound = true;
			}
		}
		if (bFound)
		{
			// for camera facing models, we must grow the bounds to contain any possible orientation of the model about its local pivot point (0, 0, 0).
			if(group->property_structs.physical_properties.bCameraFacing || group->property_structs.physical_properties.bAxisCameraFacing)
				calcBoundsForAnyOrientation(vMin, vMax, vMin, vMax);

			groupTransformMinMax(matrix,fScale,vMin,vMax);

			// get the radius and the center
			fMaxRadius = boxCalcMid(vMin,vMax,vSphereCenter);
		}
	}

	if (group->property_structs.planet_properties)
	{
		Vec3 vLocalMin,vLocalMax;
		F32 fRad = MAX(group->property_structs.planet_properties->collision_radius, group->property_structs.planet_properties->geometry_radius);
		setVec3same(vLocalMin, -fRad);
		setVec3same(vLocalMax, fRad);
		if (fScale)
			fRad *= fScale;
		groupTransformMinMax(matrix,fScale,vLocalMin,vLocalMax);
		_updateMaxExtents(vLocalMin,vLocalMax,matrix[3],fRad,vMin,vMax,vSphereCenter,&fMaxRadius);
		bFound = true;
	}
	if (group->property_structs.volume)
	{
		Vec3 vLocalMin,vLocalMax;
		Vec3 vLocalSphereCenter;
		F32 fLocalRad;
		if (group->property_structs.volume->eShape == GVS_Sphere)
		{
			setVec3same(vLocalMin, -group->property_structs.volume->fSphereRadius);
			setVec3same(vLocalMax, group->property_structs.volume->fSphereRadius);
			fLocalRad = group->property_structs.volume->fSphereRadius;
			copyVec3(matrix[3],vLocalSphereCenter);
		}
		else
		{
			Vec3 vTempCenter;

			copyVec3(group->property_structs.volume->vBoxMin, vLocalMin);
			copyVec3(group->property_structs.volume->vBoxMax, vLocalMax);

			// this will find the local center of the box (and sphere)
			fLocalRad = boxCalcMid(vLocalMin,vLocalMax,vTempCenter);

			// this will find the new midpoint of the sphere
			mulVecMat4(vTempCenter,matrix,vLocalSphereCenter);
		}
		// this will find the new AABB of the box
		groupTransformMinMax(matrix,fScale,vLocalMin,vLocalMax);
		if (fScale)
			fLocalRad *= fScale;
		_updateMaxExtents(vLocalMin,vLocalMax,vLocalSphereCenter,fLocalRad,vMin,vMax,vSphereCenter,&fMaxRadius);
		bFound = true;
	}
	if (group->property_structs.patrol_properties)
	{
		Vec3 vLocalMax = {-8e8,-8e8,-8e8}, vLocalMin = {8e8,8e8,8e8};
		Vec3 vLocalSphereCenter;
		F32 fLocalRad=0.0f;
		for (i = 0; i < eaSize(&group->property_structs.patrol_properties->patrol_points); i++)
		{
			Vec3 vPos;
			mulVecMat4(group->property_structs.patrol_properties->patrol_points[i]->pos,matrix,vPos);
			vec3RunningMin(vPos, vLocalMin);
			vec3RunningMax(vPos, vLocalMax);
			if (i==0)
			{
				copyVec3(vPos,vLocalSphereCenter);
			}
			else
			{
				fLocalRad = sphereUnion(vLocalSphereCenter,fLocalRad,vPos,0.0f,vLocalSphereCenter);
			}
		}
		if (i > 0)
		{
			_updateMaxExtents(vLocalMin,vLocalMax,vLocalSphereCenter,fLocalRad,vMin,vMax,vSphereCenter,&fMaxRadius);
			bFound = true;
		}
	}
	if (group->property_structs.encounter_properties)
	{
		Vec3 vLocalMax = {-8e8,-8e8,-8e8}, vLocalMin = {8e8,8e8,8e8};
		Vec3 vLocalSphereCenter;
		F32 fLocalRad=0.0f;
		for (i = 0; i < eaSize(&group->property_structs.encounter_properties->eaActors); i++)
		{
			Vec3 vPos;
			mulVecMat4(group->property_structs.encounter_properties->eaActors[i]->vPos,matrix,vPos);
			vec3RunningMin(vPos, vLocalMin);
			vec3RunningMax(vPos, vLocalMax);
			if (i==0)
			{
				copyVec3(vPos,vLocalSphereCenter);
			}
			else
			{
				fLocalRad = sphereUnion(vLocalSphereCenter,fLocalRad,vPos,0.0f,vLocalSphereCenter);
			}
		}
		if (i > 0)
		{
			_updateMaxExtents(vLocalMin,vLocalMax,vLocalSphereCenter,fLocalRad,vMin,vMax,vSphereCenter,&fMaxRadius);
			bFound = true;
		}
	}
	if (group->property_structs.spawn_properties)
	{
		_updateMaxExtents(matrix[3],matrix[3],matrix[3],0.0f,vMin,vMax,vSphereCenter,&fMaxRadius);
	}

	if (group->property_structs.light_properties)
	{
		Vec3 vLocalMin,vLocalMax;
		F32 fLocalRad = fillF32FromStr(groupDefFindProperty((GroupDef *)group, "LightRadius"), 0);
		// Shawn Fenton changed lights to have a radius of "5" rather than the radius of the light.  He didn't say why, and forgot to reference the Jira.
		// Furthermore, he did this only for the AABB bounds, and not the sphere bounds.
		// He says it might have been related to trying to get lights into the correct world cells during binning.
		setVec3same(vLocalMin, -5.0f);
		setVec3same(vLocalMax, 5.0f);
		groupTransformMinMax(matrix,fScale,vLocalMin,vLocalMax);
		_updateMaxExtents(vLocalMin,vLocalMax,matrix[3],fLocalRad,vMin,vMax,vSphereCenter,&fMaxRadius);
	}

	{
		bool bHadBuffers = false;
		Vec3 vLocalMax = {-8e8,-8e8,-8e8}, vLocalMin = {8e8,8e8,8e8};
		for (i = 0; i < eaSize(&group->instance_buffers); i++)
		{
			int j;
			GroupInstanceBuffer *instance_buffer = group->instance_buffers[i];
			for (j = 0; j < eaSize(&instance_buffer->instances); j++)
			{
				Vec3 pt, position;
				setVec3(position, instance_buffer->instances[j]->world_matrix_x[3], instance_buffer->instances[j]->world_matrix_y[3], instance_buffer->instances[j]->world_matrix_z[3]);
				addVec3(position, instance_buffer->model->min, pt);
				vec3RunningMin(pt, vLocalMin);
				addVec3(position, instance_buffer->model->max, pt);
				vec3RunningMax(pt, vLocalMax);
				bHadBuffers = true;
			}
		}
		if (bHadBuffers)
		{
			Vec3 vLocalSphereCenter;
			F32 fLocalRad;
			groupTransformMinMax(matrix,fScale,vLocalMin,vLocalMax);
			fLocalRad = boxCalcMid(vLocalMin,vLocalMax,vLocalSphereCenter);
			_updateMaxExtents(vLocalMin,vLocalMax,vLocalSphereCenter,fLocalRad,vMin,vMax,vSphereCenter,&fMaxRadius);
			bFound = true;
		}
	}

	if (bFound)
		*pfRadius = fMaxRadius;

	return bFound;
}

void groupCalcBounds(GroupDef const *group, Mat4 const matrix,F32 fScale,Vec3 min,Vec3 max,Vec3 vSphereCenter,F32 *pfMaxRadius)
{
	int			i;
	GroupChild const	*ent;
	int			child_count;
	GroupChild **child_list;
	bool		dynamic_child_list;

	F32 fMaxRadius = -1.0f;

	setVec3(min,8e8f,8e8f,8e8f);
	setVec3(max,-8e8f,-8e8f,-8e8f);
	child_list = groupGetDynamicChildren((GroupDef *)group, NULL, NULL, NULL);
	if (child_list)
	{
		child_count = eaSize(&child_list);
		dynamic_child_list = true;
	}
	else
	{
		child_count = eaSize(&group->children);
		for (i = 0; i < child_count; i++)
			eaPush(&child_list, group->children[i]);
		dynamic_child_list = false;
	}

	groupCalcNodeItemLocalBounds(group, matrix, fScale,min,max,vSphereCenter,&fMaxRadius);

	for (i = 0; i < child_count; i++)
	{
		GroupDef const *child_def;
		ent = child_list[i];
		child_def = groupChildGetDef((GroupDef *)group, (GroupChild *)ent, true);

		if (child_def)
		{
			Vec3 vChildMin,vChildMax;
			Mat4 m;
			F32 fChildScale;
			Vec3 vLocalSphereCenter;
			F32 fLocalRad;
			if (!child_def || child_def->bounds_null)
				continue;

			fChildScale = ent->scale;
			if (fChildScale == 0.0f)
				fChildScale = 1.0f;
			if (fScale)
				fChildScale *= fScale;
			mulMat4(matrix,ent->mat,m);
			groupCalcBounds(child_def,m,fChildScale,vChildMin,vChildMax,vLocalSphereCenter,&fLocalRad);
			_updateMaxExtents(vChildMin,vChildMax,vLocalSphereCenter,fLocalRad,min,max,vSphereCenter,&fMaxRadius);
		}
	}

	*pfMaxRadius = fMaxRadius;

	if (dynamic_child_list)
		groupFreeDynamicChildren((GroupDef *)group, child_list);
	else
		eaDestroy(&child_list);
}

void groupSetBounds(GroupDef *group, bool force)
{
	int			i;
	GroupChild	*ent;
	Vec3		min = {8e8,8e8,8e8}, max = {-8e8,-8e8,-8e8};
	Vec3		vSphereCenter;
	F32			fMaxRadius = -1.0f;
	int			child_count;
	GroupChild **child_list;
	bool		dynamic_child_list;

	if (!group || group->bounds_valid && !force)
		return;

	child_list = groupGetDynamicChildren(group, NULL, NULL, NULL);
	if (child_list)
	{
		child_count = eaSize(&child_list);
		dynamic_child_list = true;
	}
	else
	{
		child_count = eaSize(&group->children);
		for (i = 0; i < child_count; i++)
			eaPush(&child_list, group->children[i]);
		dynamic_child_list = false;
	}

	group->bounds_null = 1;

	if (groupCalcNodeItemLocalBounds(group,unitmat,1.0f,min,max,vSphereCenter,&fMaxRadius))
	{
		group->bounds_null = 0;
	}

	for (i = 0; i < child_count; i++)
	{
		GroupDef *child_def;
		ent = child_list[i];
		child_def = groupChildGetDef(group, ent, true);

		if (child_def)
		{ 
			Vec3 vChildMin,vChildMax;
			Vec3 vLocalSphereCenter;
			F32 fLocalRad;
			groupSetBounds(child_def, false); // recurse
			if (!child_def || child_def->bounds_null)
				continue;
			groupCalcBounds(child_def,ent->mat,ent->scale,vChildMin,vChildMax,vLocalSphereCenter,&fLocalRad);
			_updateMaxExtents(vChildMin,vChildMax,vLocalSphereCenter,fLocalRad,min,max,vSphereCenter,&fMaxRadius);

			group->bounds_null = 0;
		}
	}
    
	if (group->bounds_null)
	{
		zeroVec3(min);
		zeroVec3(max);
	}

    if (group->property_structs.curve)
    {
#if 1 // This is the new bounds calculation: --TomY

		Mat4 id_matrix;
		Vec3 vDiff;
		F32 fDist;
		GroupCurveRecurseInfo rec_info = { { 0, 0, 0 }, { 0, 0, 0 } };
		identityMat4(id_matrix);
		groupTreeTraverse(NULL, group, id_matrix, NULL, groupSetBoundsCurveRecurseCallback, &rec_info, false, false);
        
        group->bounds.radius = boxCalcMid(rec_info.min, rec_info.max, group->bounds.mid);
		if (fMaxRadius >= 0.0f)
		{
			subVec3(group->bounds.mid,vSphereCenter,vDiff);
			fDist = lengthVec3(vDiff);
			MIN1(group->bounds.radius,fDist+fMaxRadius);
		}
        copyVec3(rec_info.max,group->bounds.max);
        copyVec3(rec_info.min,group->bounds.min);

#else // This is the old bounds calculation:

        F32 rad = boxCalcMid(min, max, mid);
        Vec3 point_min = { 0, 0, 0 };
        Vec3 point_max = { 0, 0, 0 };
        for (i = 0; i < eafSize(&group->property_structs.curve->spline.spline_points); i += 3)
        {
            vec3RunningMinMax(&group->property_structs.curve->spline.spline_points[i], point_min, point_max);
        }
        point_min[0] -= rad;
        point_min[1] -= rad;
        point_min[2] -= rad;
        point_max[0] += rad;
        point_max[1] += rad;
        point_max[2] += rad;
        
        group->bounds.radius = boxCalcMid(point_min, point_max, mid);
        copyVec3(point_max,group->bounds.max);
        copyVec3(point_min,group->bounds.min);
        copyVec3(mid,group->bounds.mid);
#endif
    }
    else
    {
		// the implicit rule seems to be: the box must share a center with the sphere, but the sphere can be up the size where it contains the entire box,
		// or small enough to just fit in the box.
		// Soo.... if the center doesn't match... I'll have to calculate a new sphere that contains the old sphere but is centered at the center, I suppose
		Vec3 vDiff;
		F32 fDist;
        group->bounds.radius = boxCalcMid(min, max, group->bounds.mid);
		if (fMaxRadius >= 0.0f)
		{
			subVec3(group->bounds.mid,vSphereCenter,vDiff);
			fDist = lengthVec3(vDiff);
			MIN1(group->bounds.radius,fDist+fMaxRadius);
		}
        copyVec3(max,group->bounds.max);
        copyVec3(min,group->bounds.min);
    }

	if (dynamic_child_list)
		groupFreeDynamicChildren(group, child_list);
	else
		eaDestroy(&child_list);

	group->bounds_valid = 1;
}

bool groupGetVisibleBounds(GroupDef *group, Mat4 const matrix, F32 fScale, Vec3 vMin, Vec3 vMax)
{
	int			i;
	GroupChild	*ent;
	int			child_count;
	GroupChild **child_list;
	bool		dynamic_child_list;
	bool bHasAnyBounds = false;

	setVec3(vMin,8e8f,8e8f,8e8f);
	setVec3(vMax,-8e8f,-8e8f,-8e8f);

	if (!group)
		return false;

	child_list = groupGetDynamicChildren(group, NULL, NULL, NULL);
	if (child_list)
	{
		child_count = eaSize(&child_list);
		dynamic_child_list = true;
	}
	else
	{
		child_count = eaSize(&group->children);
		for (i = 0; i < child_count; i++)
			eaPush(&child_list, group->children[i]);
		dynamic_child_list = false;
	}

	if (group->model || group->model_name)
	{
		if (group->model)
		{
			mulVecVec3(group->model->min, group->model_scale, vMin);
			mulVecVec3(group->model->max, group->model_scale, vMax);
			bHasAnyBounds = true;
		}
		else
		{
			ModelHeader *header = wlModelHeaderFromName(group->model_name);
			if (header)
			{
				mulVecVec3(header->min, group->model_scale, vMin);
				mulVecVec3(header->max, group->model_scale, vMax);
				bHasAnyBounds = true;
			}
		}
		if (bHasAnyBounds)
		{
			groupTransformMinMax(matrix,fScale,vMin,vMax);
		}
	}

	for (i = 0; i < child_count; i++)
	{
		GroupDef *child_def;
		ent = child_list[i];
		child_def = groupChildGetDef((GroupDef *)group, (GroupChild *)ent, true);

		if (child_def)
		{
			Vec3 vChildMin,vChildMax;
			Mat4 m;
			F32 fChildScale;
			if (!child_def || child_def->bounds_null)
				continue;

			fChildScale = ent->scale;
			if (fChildScale == 0.0f)
				fChildScale = 1.0f;
			if (fScale)
				fChildScale *= fScale;
			mulMat4(matrix,ent->mat,m);
			if (groupGetVisibleBounds(child_def,m,fChildScale,vChildMin,vChildMax))
			{
				vec3RunningMin(vChildMin, vMin);
				vec3RunningMax(vChildMax, vMax);
				bHasAnyBounds = true;
			}
		}
	}

	if (dynamic_child_list)
		groupFreeDynamicChildren(group, child_list);
	else
		eaDestroy(&child_list);

	return bHasAnyBounds;
}

TextureSwap *createTextureSwap(const char *filename, const char *orig_name, const char *replace_name)
{
	TextureSwap *dts = StructCreate(parse_TextureSwap);
	// the original names must be kept around so that they will be saved even if not found
	dts->orig_name = allocAddString(orig_name);
	dts->replace_name = allocAddString(replace_name);
	if (wl_state.tex_is_volume_func)
	{
		BasicTexture *tex1 = wl_state.tex_find_func(dts->orig_name, 0, false);
		BasicTexture *tex2 = wl_state.tex_find_func(dts->replace_name, 0, false);
		if (tex1 && tex2)
		{
			if (wl_state.tex_is_volume_func(tex1) != wl_state.tex_is_volume_func(tex2))
			{
				ErrorFilenameGroupRetroactivef(filename, "Art", 14, 12, 8, 2010, "Trying to swap 2D texture with Volume texture, this is not allowed.  (%s and %s)",
					dts->orig_name, dts->replace_name);
			}
			if (wl_state.tex_is_cubemap_func(tex1) != wl_state.tex_is_cubemap_func(tex2) && stricmp(dts->orig_name, "0_From_Sky_File")!=0)
			{
				ErrorFilenameGroupRetroactivef(filename, "Art", 14, 12, 8, 2010, "Trying to swap 2D texture with Cubemap texture, this is not allowed.  (%s and %s)",
					dts->orig_name, dts->replace_name);
			}
			if (wl_state.tex_is_normalmap_func(tex1) != wl_state.tex_is_normalmap_func(tex2))
			{
				ErrorFilenameGroupRetroactivef(filename, "Art", 14, 12, 8, 2010, "Trying to swap Normalmap texture with non-Normalmap texture, this is not allowed.  (%s and %s)",
					dts->orig_name, dts->replace_name);
			}
		}
	}
	return dts;
}

void freeTextureSwap(TextureSwap *dts)
{
	if (!dts)
		return;
	StructDestroy(parse_TextureSwap, dts);
}

TextureSwap *dupTextureSwap(const char *filename, TextureSwap *dts)
{
	if (!dts)
		return NULL;
	return createTextureSwap(filename, dts->orig_name, dts->replace_name);
}

MaterialSwap *createMaterialSwap(const char *orig_name, const char *replace_name)
{
	MaterialSwap *dts = StructCreate(parse_MaterialSwap);
	// the original names must be kept around so that they will be saved even if not found
	dts->orig_name = allocAddString(orig_name);
	dts->replace_name = allocAddString(replace_name);
	return dts;
}

void freeMaterialSwap(MaterialSwap *dts)
{
	if (!dts)
		return;
	StructDestroy(parse_MaterialSwap, dts);
}

MaterialSwap *dupMaterialSwap(MaterialSwap *dts)
{
	if (!dts)
		return NULL;
	return createMaterialSwap(dts->orig_name, dts->replace_name);
}

bool groupIsPrivate(GroupDef *def)
{
	if (groupIsObjLib(def) && def->root_id != 0)
		return true;
	return false;
}

bool groupIsPublic(GroupDef *def)
{
	if (groupIsObjLib(def) && def->root_id == 0)
		return true;
	return false;
}

bool groupHasScope(GroupDef *def)
{
	return (groupIsPublic(def) || def->path_to_name);
}

bool groupIsObjLib(GroupDef *def)
{
	if (!def || !def->def_lib)
		return false;
	return (!def->def_lib->zmap_layer);
}

bool groupIsObjLibUID(int uid)
{
	return uid < 0;
}

bool groupIsInCoreDir(GroupDef *def)
{
	char fullname[MAX_PATH];
	const char *coredir = fileCoreDataDir();
	if (!def->filename || !coredir)
		return false;
	fileLocateWrite(def->filename, fullname);
	return strStartsWith(fullname, coredir);
}

bool groupIsCore(GroupDef *def)
{
	return groupIsObjLib(def) && groupIsInCoreDir(def);
}

bool groupIsCoreUID(int uid)
{
	return (uid & 1);
}

void splineParamsGetBounds(GroupSplineParams *params, GroupDef *def, Vec3 min, Vec3 max)
{
	F32 width = (def->bounds.max[0] - def->bounds.min[0]) * params->spline_matrices[1][2][1]; // X stretch
	F32 height = def->bounds.max[1] - def->bounds.min[1];
	F32 rad = sqrtf(width*width + height*height);
	setVec3(min, 8e16, 8e16, 8e16);
	setVec3(max, -8e16, -8e16, -8e16);
	
	vec3RunningMinMax(params->spline_matrices[0][0], min, max);
	vec3RunningMinMax(params->spline_matrices[0][1], min, max);
	vec3RunningMinMax(params->spline_matrices[0][2], min, max);
	vec3RunningMinMax(params->spline_matrices[0][3], min, max);

	min[0] -= rad;
	min[1] -= rad;
	min[2] -= rad;
	max[0] += rad;
	max[1] += rad;
	max[2] += rad;
}

bool groupIsEditable(GroupDef *def)
{
	if (def->def_lib && def->def_lib->dummy)
		return true; // Dummy objects are always "locked"

	if (def->def_lib && def->def_lib->zmap_layer)
	{
		if (def->def_lib->zmap_layer->dummy_layer)
			return true; // Terrain objects are "editable"; never show upin the tracker tree

		return layerGetLocked(def->def_lib->zmap_layer); // Layer def is locked if the layer is
	}
	return objectLibraryGroupEditable(def); // Object library piece
}

//////////////////////////////////////////////////////////////////////////

bool lightPropertyIsSet(WorldLightProperties *pProps, const char *property_name)
{
	if(!pProps)
		return false;
	return (eaFind(&pProps->ppcSetFields, property_name) != -1);
}

static bool groupLightPropertyIsSet(GroupDef *def, const char *property_name)
{
	if(!def)
		return false;
	return lightPropertyIsSet(def->property_structs.light_properties, property_name);
}

GroupTracker *groupGetLightPropertyBool(GroupTracker *tracker, GroupDef **def_chain, GroupDef *def, const char *property_name, bool *val)
{
	GroupTracker *source = NULL;
	int idx = eaSize(&def_chain) - 1;
	int column;

	if (tracker)
		def = tracker->def;

	property_name = allocAddString(property_name);
	assert(ParserFindColumn(parse_WorldLightProperties, property_name, &column));
	*val = !!TokenStoreGetBit(parse_WorldLightProperties, column, groupGetDefaultLightProperties(), 0, NULL);
	while (def)
	{
		if (groupLightPropertyIsSet(def, property_name))
		{
			*val = !!TokenStoreGetBit(parse_WorldLightProperties, column, def->property_structs.light_properties, 0, NULL);
			source = tracker;
			break;
		}

		if (def_chain)
		{
			--idx;
			def = idx >= 0 ? def_chain[idx] : NULL;
		}
		else
		{
			if (tracker)
				tracker = tracker->parent;
			def = SAFE_MEMBER(tracker, def);
		}
	}

	return source;
}

GroupTracker *groupGetLightPropertyFloat(GroupTracker *tracker, GroupDef **def_chain, GroupDef *def, const char *property_name, F32 *val)
{
	GroupTracker *source = NULL;
	int idx = eaSize(&def_chain) - 1;
	int column;

	if (tracker)
		def = tracker->def;
	property_name = allocAddString(property_name);
	assert(ParserFindColumn(parse_WorldLightProperties, property_name, &column));
	if (stricmp(property_name, "LightRadiusInner")==0) {
		groupGetLightPropertyFloat(tracker, def_chain, def, "LightRadius", val);
		*val *= 0.1f;
	} else {
		*val = TokenStoreGetF32(parse_WorldLightProperties, column, groupGetDefaultLightProperties(), 0, NULL);
	}
	while (def)
	{
		if (groupLightPropertyIsSet(def, property_name))
		{
			*val = TokenStoreGetF32(parse_WorldLightProperties, column, def->property_structs.light_properties, 0, NULL);
			source = tracker;
			break;
		}

		if (def_chain)
		{
			--idx;
			def = idx >= 0 ? def_chain[idx] : NULL;
		}
		else
		{
			if (tracker)
				tracker = tracker->parent;
			def = SAFE_MEMBER(tracker, def);
		}
	}

	return source;
}

GroupTracker *groupGetLightPropertyVec3(GroupTracker *tracker, GroupDef **def_chain, GroupDef *def, const char *property_name, Vec3 val)
{
	GroupTracker *source = NULL;
	int idx = eaSize(&def_chain) - 1;
	int column;

	if (tracker)
		def = tracker->def;

	property_name = allocAddString(property_name);
	assert(ParserFindColumn(parse_WorldLightProperties, property_name, &column));
	val[0] = TokenStoreGetF32(parse_WorldLightProperties, column, groupGetDefaultLightProperties(), 0, NULL);
	val[1] = TokenStoreGetF32(parse_WorldLightProperties, column, groupGetDefaultLightProperties(), 1, NULL);
	val[2] = TokenStoreGetF32(parse_WorldLightProperties, column, groupGetDefaultLightProperties(), 2, NULL);
	while (def)
	{
		if (groupLightPropertyIsSet(def, property_name))
		{
			val[0] = TokenStoreGetF32(parse_WorldLightProperties, column, def->property_structs.light_properties, 0, NULL);
			val[1] = TokenStoreGetF32(parse_WorldLightProperties, column, def->property_structs.light_properties, 1, NULL);
			val[2] = TokenStoreGetF32(parse_WorldLightProperties, column, def->property_structs.light_properties, 2, NULL);
			source = tracker;
			break;
		}

		if (def_chain)
		{
			--idx;
			def = idx >= 0 ? def_chain[idx] : NULL;
		}
		else
		{
			if (tracker)
				tracker = tracker->parent;
			def = SAFE_MEMBER(tracker, def);
		}
	}

	return source;
}

GroupTracker *groupGetLightPropertyInt(SA_PARAM_OP_VALID GroupTracker *tracker, GroupDef **def_chain, GroupDef *def, SA_PARAM_NN_STR const char *property_name, SA_PARAM_NN_VALID int *val)
{
	GroupTracker *source = NULL;
	int idx = eaSize(&def_chain) - 1;
	int ret = 0;
	int column;

	if (tracker)
		def = tracker->def;

	property_name = allocAddString(property_name);
	assert(ParserFindColumn(parse_WorldLightProperties, property_name, &column));
	*val = TokenStoreGetInt(parse_WorldLightProperties, column, groupGetDefaultLightProperties(), 0, NULL);
	while (def)
	{
		if (groupLightPropertyIsSet(def, property_name))
		{
			*val = TokenStoreGetInt(parse_WorldLightProperties, column, def->property_structs.light_properties, 0, NULL);
			source = tracker;
			break;
		}

		if (def_chain)
		{
			--idx;
			def = idx >= 0 ? def_chain[idx] : NULL;
		}
		else
		{
			if (tracker)
				tracker = tracker->parent;
			def = SAFE_MEMBER(tracker, def);
		}
	}

	return source;
}


GroupTracker *groupGetLightPropertyString(GroupTracker *tracker, GroupDef **def_chain, GroupDef *def, const char *property_name, const char **val)
{
	GroupTracker *source = NULL;
	int idx = eaSize(&def_chain) - 1;
	int column;

	if (tracker)
		def = tracker->def;

	*val = NULL;
	property_name = allocAddString(property_name);
	assert(ParserFindColumn(parse_WorldLightProperties, property_name, &column));
	*val = TokenStoreGetString(parse_WorldLightProperties, column, groupGetDefaultLightProperties(), 0, NULL);
	if((*val) && !(*val)[0])
		*val = NULL;
	while (def)
	{
		if (groupLightPropertyIsSet(def, property_name))
		{
			const char *new_val = TokenStoreGetString(parse_WorldLightProperties, column, def->property_structs.light_properties, 0, NULL);
			*val = StructAllocString(new_val);
			source = tracker;
			break;
		}

		if (def_chain)
		{
			--idx;
			def = idx >= 0 ? def_chain[idx] : NULL;
		}
		else
		{
			if (tracker)
				tracker = tracker->parent;
			def = SAFE_MEMBER(tracker, def);
		}
	}

	return source;
}

LightData *groupGetLightData(GroupDef **def_chain, const Mat4 world_mat)
{
	GroupDef *def = eaSize(&def_chain)?def_chain[eaSize(&def_chain)-1]:NULL;
	LightType light_type;
	Vec3 tempvec;
	LightData *light_data;
	int tempInt;

	if (!def || !def->property_structs.light_properties)
		return NULL;

	switch(def->property_structs.light_properties->eLightType) {
	case WleAELightPoint:
		light_type = WL_LIGHT_POINT;
		break;
	case WleAELightSpot:
		light_type = WL_LIGHT_SPOT;
		break;
	case WleAELightProjector:
		light_type = WL_LIGHT_PROJECTOR;
		break;
	case WleAELightDirectional:
		light_type = WL_LIGHT_DIRECTIONAL;
		break;
	default:
		Errorf("Unknown light type: %s", StaticDefineIntRevLookup(WleAELightTypeEnum, def->property_structs.light_properties->eLightType));
		return NULL;		
	}

	light_data = StructAlloc(parse_LightData);
	light_data->light_type = light_type;

	copyMat4(world_mat, light_data->world_mat);

	groupGetLightPropertyInt(NULL, def_chain, def, "LightAffects", &tempInt);
	light_data->light_affect_type = tempInt;

	groupGetLightPropertyBool(NULL, def_chain, def, "LightIsKey", &light_data->key_light);
	groupGetLightPropertyBool(NULL, def_chain, def, "LightIsSun", &light_data->is_sun);

	groupGetLightPropertyBool(NULL, def_chain, def, "LightCastsShadows", &light_data->cast_shadows);

	groupGetLightPropertyVec3(NULL, def_chain, def, "LightAmbientHSV", light_data->ambient_hsv);
	groupGetLightPropertyVec3(NULL, def_chain, def, "LightAmbientMultiplier", tempvec);
	mulVecVec3(light_data->ambient_hsv, tempvec, light_data->ambient_hsv);
	groupGetLightPropertyVec3(NULL, def_chain, def, "LightAmbientOffset", tempvec);
	addVec3(light_data->ambient_hsv, tempvec, light_data->ambient_hsv);
	hsvMakeLegal(light_data->ambient_hsv, true);

	groupGetLightPropertyVec3(NULL, def_chain, def, "LightDiffuseHSV", light_data->diffuse_hsv);
	groupGetLightPropertyVec3(NULL, def_chain, def, "LightDiffuseMultiplier", tempvec);
	mulVecVec3(light_data->diffuse_hsv, tempvec, light_data->diffuse_hsv);
	groupGetLightPropertyVec3(NULL, def_chain, def, "LightDiffuseOffset", tempvec);
	addVec3(light_data->diffuse_hsv, tempvec, light_data->diffuse_hsv);
	hsvMakeLegal(light_data->diffuse_hsv, true);

	groupGetLightPropertyVec3(NULL, def_chain, def, "LightSecondaryDiffuseHSV", light_data->secondary_diffuse_hsv);
	groupGetLightPropertyVec3(NULL, def_chain, def, "LightSecondaryDiffuseMultiplier", tempvec);
	mulVecVec3(light_data->secondary_diffuse_hsv, tempvec, light_data->secondary_diffuse_hsv);
	groupGetLightPropertyVec3(NULL, def_chain, def, "LightSecondaryDiffuseOffset", tempvec);
	addVec3(light_data->secondary_diffuse_hsv, tempvec, light_data->secondary_diffuse_hsv);
	hsvMakeLegal(light_data->secondary_diffuse_hsv, true);

	groupGetLightPropertyVec3(NULL, def_chain, def, "LightShadowColorHSV", light_data->shadow_color_hsv);
	groupGetLightPropertyVec3(NULL, def_chain, def, "LightShadowColorMultiplier", tempvec);
	mulVecVec3(light_data->shadow_color_hsv, tempvec, light_data->shadow_color_hsv);
	groupGetLightPropertyVec3(NULL, def_chain, def, "LightShadowColorOffset", tempvec);
	addVec3(light_data->shadow_color_hsv, tempvec, light_data->shadow_color_hsv);
	hsvMakeLegal(light_data->shadow_color_hsv, true);

	groupGetLightPropertyVec3(NULL, def_chain, def, "LightSpecularHSV", light_data->specular_hsv);
	groupGetLightPropertyVec3(NULL, def_chain, def, "LightSpecularMultiplier", tempvec);
	mulVecVec3(light_data->specular_hsv, tempvec, light_data->specular_hsv);
	groupGetLightPropertyVec3(NULL, def_chain, def, "LightSpecularOffset", tempvec);
	addVec3(light_data->specular_hsv, tempvec, light_data->specular_hsv);
	hsvMakeLegal(light_data->specular_hsv, true);

	groupGetLightPropertyFloat(NULL, def_chain, def, "LightRadius", &light_data->outer_radius);
	groupGetLightPropertyFloat(NULL, def_chain, def, "LightRadiusInner", &light_data->inner_radius);

	if (light_type == WL_LIGHT_PROJECTOR || light_type == WL_LIGHT_SPOT)
	{
		groupGetLightPropertyFloat(NULL, def_chain, def, "LightConeInner", &light_data->inner_cone_angle);
		groupGetLightPropertyFloat(NULL, def_chain, def, "LightConeOuter", &light_data->outer_cone_angle);

		if (light_data->key_light && light_data->cast_shadows)
			groupGetLightPropertyFloat(NULL, def_chain, def, "LightShadowNearDist", &light_data->shadow_near_plane);
	}

	if (light_type == WL_LIGHT_PROJECTOR)
	{
		groupGetLightPropertyFloat(NULL, def_chain, def, "LightCone2Inner", &light_data->inner_cone_angle2);
		groupGetLightPropertyFloat(NULL, def_chain, def, "LightCone2Outer", &light_data->outer_cone_angle2);
		groupGetLightPropertyString(NULL, def_chain, def, "LightProjectedTexture", &light_data->texture_name);
	}

	if (light_type == WL_LIGHT_DIRECTIONAL && light_data->key_light && light_data->cast_shadows)
	{
		F32 cloud_multiplier;

		groupGetLightPropertyString(NULL, def_chain, def, "LightCloudTexture", &light_data->cloud_texture_name);

		groupGetLightPropertyFloat(NULL, def_chain, def, "LightCloudMultiplier1", &cloud_multiplier);
		if (cloud_multiplier)
		{
			light_data->cloud_layers[0].multiplier = cloud_multiplier;
			groupGetLightPropertyFloat(NULL, def_chain, def, "LightCloudScale1", &light_data->cloud_layers[0].scale);
			MAX1(light_data->cloud_layers[0].scale, 0.1f);
			groupGetLightPropertyFloat(NULL, def_chain, def, "LightCloudScrollX1", &light_data->cloud_layers[0].scroll_rate[0]);
			groupGetLightPropertyFloat(NULL, def_chain, def, "LightCloudScrollY1", &light_data->cloud_layers[0].scroll_rate[1]);
		}

		groupGetLightPropertyFloat(NULL, def_chain, def, "LightCloudMultiplier2", &cloud_multiplier);
		if (cloud_multiplier)
		{
			light_data->cloud_layers[1].multiplier = cloud_multiplier;
			groupGetLightPropertyFloat(NULL, def_chain, def, "LightCloudScale2", &light_data->cloud_layers[1].scale);
			MAX1(light_data->cloud_layers[1].scale, 0.1f);
			groupGetLightPropertyFloat(NULL, def_chain, def, "LightCloudScrollX2", &light_data->cloud_layers[1].scroll_rate[0]);
			groupGetLightPropertyFloat(NULL, def_chain, def, "LightCloudScrollY2", &light_data->cloud_layers[1].scroll_rate[1]);
		}

		groupGetLightPropertyBool(NULL, def_chain, def, "LightInfiniteShadows", &light_data->infinite_shadows);
	}

	groupGetLightPropertyFloat(NULL, def_chain, def, "LightVisualLODScale", &light_data->visual_lod_scale);

	return light_data;
}

void groupSetTintColor(GroupDef *def, const Vec3 tint_color)
{
	if (!def)
		return;

	copyVec3(tint_color, def->tint_color0);
	def->group_flags |= GRP_HAS_TINT;
}

void groupRemoveTintColor(GroupDef *def)
{
	if (!def)
		return;

	def->group_flags &= ~GRP_HAS_TINT;
}

void groupSetTintOffset(GroupDef *def, const Vec3 tint_offset)
{
	if (!def)
		return;

	copyVec3(tint_offset, def->tint_color0_offset);
	def->group_flags |= GRP_HAS_TINT_OFFSET;
}

void groupRemoveTintOffset(GroupDef *def)
{
	if (!def)
		return;

	def->group_flags &= ~GRP_HAS_TINT_OFFSET;
}

bool groupGetMaterialPropertyF32(GroupDef *def, const char *property_name, F32 *value)
{
	Vec4 v;
	bool ret = groupGetMaterialPropertyVec4(def, property_name, v);
	*value = v[0];
	return ret;
}

bool groupGetMaterialPropertyVec2(GroupDef *def, const char *property_name, Vec2 value)
{
	Vec4 v;
	bool ret = groupGetMaterialPropertyVec4(def, property_name, v);
	copyVec2(v, value);
	return ret;
}

bool groupGetMaterialPropertyVec3(GroupDef *def, const char *property_name, Vec3 value)
{
	Vec4 v;
	bool ret = groupGetMaterialPropertyVec4(def, property_name, v);
	copyVec3(v, value);
	return ret;
}

bool groupGetMaterialPropertyVec4(GroupDef *def, const char *property_name, Vec4 value)
{
	int i;

	setVec4same(value, 0);

	if (!def)
		return false;

	property_name = allocAddString(property_name);
	for (i = 0; i < eaSize(&def->material_properties); ++i)
	{
		if (property_name == def->material_properties[i]->name)
		{
			copyVec4(def->material_properties[i]->value, value);
			return true;
		}
	}

	return false;
}

void groupSetMaterialPropertyF32(GroupDef *def, const char *property_name, F32 value)
{
	Vec4 v;
	setVec4(v, value, 1, 1, 1);
	groupSetMaterialPropertyVec4(def, property_name, v);
}

void groupSetMaterialPropertyVec2(GroupDef *def, const char *property_name, const Vec2 value)
{
	Vec4 v;
	copyVec2(value, v);
	v[2] = v[3] = 1;
	groupSetMaterialPropertyVec4(def, property_name, v);
}

void groupSetMaterialPropertyVec3(GroupDef *def, const char *property_name, const Vec3 value)
{
	Vec4 v;
	copyVec3(value, v);
	v[3] = 1;
	groupSetMaterialPropertyVec4(def, property_name, v);
}

void groupSetMaterialPropertyVec4(GroupDef *def, const char *property_name, const Vec4 value)
{
	MaterialNamedConstant *prop = NULL;
	int i;

	if (!def)
		return;

	property_name = allocAddString(property_name);
	for (i = 0; i < eaSize(&def->material_properties); ++i)
	{
		if (property_name == def->material_properties[i]->name)
		{
			prop = def->material_properties[i];
			break;
		}
	}

	if (!prop)
	{
		prop = StructAlloc(parse_MaterialNamedConstant);
		eaPush(&def->material_properties, prop);
	}

	prop->name = property_name;
	copyVec4(value, prop->value);
	def->group_flags |= GRP_HAS_MATERIAL_PROPERTIES;
}

void groupRemoveMaterialProperty(GroupDef *def, const char *property_name)
{
	int i;

	if (!def)
		return;

	property_name = allocAddString(property_name);
	for (i = 0; i < eaSize(&def->material_properties); ++i)
	{
		if (property_name == def->material_properties[i]->name)
		{
			StructDestroy(parse_MaterialNamedConstant, def->material_properties[i]);
			eaRemove(&def->material_properties, i);
			--i;
		}
	}

	if (eaSize(&def->material_properties) == 0)
		def->group_flags &= ~GRP_HAS_MATERIAL_PROPERTIES;
}

bool groupIsVolumeType(GroupDef *def, const char *volume_type)
{
	int i;
	const char *pooled_string;
	if (!def || !def->property_structs.hull || !volume_type)
		return false;

	pooled_string = allocAddString(volume_type);
	for (i = 0; i < eaSize(&def->property_structs.hull->ppcTypes); ++i)
	{
		if(def->property_structs.hull->ppcTypes[i] == pooled_string)
			return true;
	}

	return false;
}

void groupGetBounds(GroupDef *def, GroupSplineParams *spline, const Mat4 world_mat_in, F32 scale,
					Vec3 world_min, Vec3 world_max, Vec3 world_mid, F32 *radius, Mat4 world_mat_out)
{
	if (world_mat_out != world_mat_in)
		copyMat4(world_mat_in, world_mat_out);

	if (spline)
	{
		splineParamsGetBounds(spline, def, world_min, world_max);
		if (radius)
			*radius = 0.5f * distance3(world_min, world_max);
		if (world_mid)
			centerVec3(world_min, world_max, world_mid);
		copyMat4(unitmat, world_mat_out);
	}
	else if(def->property_structs.physical_properties.bCameraFacing || def->property_structs.physical_properties.bAxisCameraFacing)
	{
		Vec3 bounds_min, bounds_max;
		Vec3 minScaled, midScaled, maxScaled;

		// for camera facing models, we must grow the bounds to contain any possible orientation of the model about its local pivot point (0, 0, 0).
		calcBoundsForAnyOrientation(def->bounds.min, def->bounds.max, bounds_min, bounds_max);

		scaleVec3(bounds_min, scale, minScaled);
		scaleVec3(bounds_max, scale, maxScaled);
		mulBoundsAA(minScaled, maxScaled, world_mat_out, world_min, world_max);
		if (radius)
			*radius = lengthVec3(bounds_max) * scale;
		if (world_mid)
		{
			Vec3 bounds_mid = {0.0f, 0.0f, 0.0f};
			scaleVec3(bounds_mid, scale, midScaled);
			mulVecMat4(midScaled, world_mat_out, world_mid);
		}
	}
	else
	{
		Vec3 minScaled, midScaled, maxScaled;
		scaleVec3(def->bounds.min, scale, minScaled);
		scaleVec3(def->bounds.max, scale, maxScaled);
		mulBoundsAA(minScaled, maxScaled, world_mat_out, world_min, world_max);
		if (radius)
			*radius = def->bounds.radius * scale;
		if (world_mid)
		{
			scaleVec3(def->bounds.mid, scale, midScaled);
			mulVecMat4(midScaled, world_mat_out, world_mid);
		}
    }

	assert(world_max[0] >= world_min[0]);
	assert(world_max[1] >= world_min[1]);
	assert(world_max[2] >= world_min[2]);
}

void atmosphereBuildDynamicConstantSwaps_dbg(F32 atmosphere_size, F32 atmosphere_radius, MaterialNamedConstant ***material_property_list MEM_DBG_PARMS)
{
	static MaterialNamedConstant atmosphere_radius_constant = {0};

	F32 offset = SQR(0.15f * atmosphere_size);
	MIN1(offset, 1);
	atmosphere_radius_constant.name = allocAddString("AtmosphereRadiusSquared");
	setVec4same(atmosphere_radius_constant.value, SQR(atmosphere_radius - offset));
	seaPush(material_property_list, &atmosphere_radius_constant);
}

//////////////////////////////////////////////////////////////////////////

bool groupDefNeedsUniqueName(GroupDef *def)
{
	if (!def)
		return false;

	return (def->property_structs.encounter_hack_properties ||
			def->property_structs.encounter_properties ||
			def->property_structs.interaction_properties ||
			def->property_structs.spawn_properties ||
			def->property_structs.patrol_properties ||
			def->property_structs.trigger_condition_properties ||
			def->property_structs.layer_fsm_properties ||
			!StructIsZero(&def->property_structs.server_volume) ||
			def->property_structs.physical_properties.bNamedPoint || 
			SAFE_MEMBER(def->property_structs.room_properties, eRoomType) == WorldRoomType_Room ||
			SAFE_MEMBER(def->property_structs.room_properties, eRoomType) == WorldRoomType_Partition);
}

bool groupDefScopeSetPathName(GroupDef *scope_def, const char *path, const char *unique_name, bool overwrite)
{
	char *temp_name = NULL;
	char *temp_path = NULL;
	const char *key_str;

	if (!scope_def->path_to_name)
		scope_def->path_to_name = stashTableCreateWithStringKeys(16, StashDeepCopyKeys_NeverRelease);
	if (!scope_def->name_to_path)
		scope_def->name_to_path = stashTableCreateWithStringKeys(16, StashDeepCopyKeys_NeverRelease);

	if (overwrite && stashFindPointer(scope_def->name_to_path, unique_name, &temp_path))
		stashRemovePointer(scope_def->path_to_name, temp_path, NULL);
	stashAddPointer(scope_def->name_to_path, unique_name, NULL, true);
	assert(stashGetKey(scope_def->name_to_path, unique_name, &key_str));
	if (overwrite && stashFindPointer(scope_def->path_to_name, path, &temp_name))
		stashRemovePointer(scope_def->name_to_path, temp_name, NULL);
	stashAddPointer(scope_def->path_to_name, path, key_str, true);
	assert(stashGetKey(scope_def->path_to_name, path, &key_str));
	stashAddPointer(scope_def->name_to_path, unique_name, key_str, true);

	return true;
}

static bool groupDefScopeCheckMapNameUniqueness(const char *name)
{
	int i;

	// check global map scope first
	if (stashFindPointer(world_grid.active_map->zmap_scope->scope.name_to_obj, name, NULL))
		return false;

	// check individual layers for reserved names
	for (i = 0; i < eaSize(&world_grid.active_map->layers); i++)
	{
		GroupDef *def;
		
		if (!world_grid.active_map->layers[i])
			continue;

		def = layerGetDef(world_grid.active_map->layers[i]);
		if (world_grid.active_map->layers[i]->reserved_unique_names && stashFindInt(world_grid.active_map->layers[i]->reserved_unique_names, name, NULL))
			return false;
	}

	return true;
}

bool groupDefScopeIsNameUnique(GroupDef *scope_def, const char *name)
{
	int i;

	if (!name || !name[0] || !scope_def)
		return false;

	// map scope uniqueness
	if (scope_def->is_layer)
	{
		if (!groupDefScopeCheckMapNameUniqueness(name))
			return false;
	}

	if (scope_def->name_to_path && stashFindPointer(scope_def->name_to_path, name, NULL))
		return false;

	for (i = 0; i < eaSize(&scope_def->logical_groups); i++)
	{
		if (strcmpi(scope_def->logical_groups[i]->group_name, name) == 0)
			return false;
	}

	return true;
}

GroupNameReturnVal groupDefScopeCreateUniqueName(GroupDef *scope_def, const char *path, const char *base_name, char *output, size_t output_size, bool error_check)
{
	char *val = NULL;
	char *estrBaseName = NULL;

	// Ensure base name follows proper rules
	if (!resFixName(base_name, &estrBaseName))
		estrPrintf(&estrBaseName, "%s", base_name);

	// no need to create a new name if the path to the object already exists; just use its existing name
	if (path && scope_def->path_to_name && stashFindPointer(scope_def->path_to_name, path, &val))
	{
		estrDestroy( &estrBaseName );

		if (error_check && scope_def->is_layer && !groupDefScopeCheckMapNameUniqueness(val))
		{
			ErrorFilenamef(scope_def->filename, "Object with duplicate unique name \"%s\" found.", val);
			return GROUP_NAME_DUPLICATE;
		}
		strcpy_s(output, output_size, val);
		return GROUP_NAME_EXISTS;
	}
	else if (!path && base_name && base_name[0])
	{
		estrDestroy( &estrBaseName );

		if (error_check && scope_def->is_layer && !groupDefScopeCheckMapNameUniqueness(base_name))
		{
			ErrorFilenamef(scope_def->filename, "Logical group with duplicate unique name \"%s\" found.", base_name);
			return GROUP_NAME_DUPLICATE;
		}
		strcpy_s(output, output_size, base_name);
		return GROUP_NAME_EXISTS;
	}

	sprintf_s(SAFESTR2(output), "%s", estrBaseName);
	while (!groupDefScopeIsNameUnique(scope_def, output))
		sprintf_s(SAFESTR2(output), "%s_%i", estrBaseName, ++scope_def->starting_index);	

	estrDestroy(&estrBaseName);

	return GROUP_NAME_CREATED;
}

void groupDefLayerScopeAddInstanceData(GroupDef *layer_def, const char *unique_name, InstanceData *data)
{
	if (!data || strncmp(GROUP_UNNAMED_PREFIX, unique_name, strlen(GROUP_UNNAMED_PREFIX)) == 0)
		return;

	if (!layer_def->name_to_instance_data)
		layer_def->name_to_instance_data = stashTableCreateWithStringKeys(16, StashDeepCopyKeys_NeverRelease);
	stashAddPointer(layer_def->name_to_instance_data, unique_name, data, true);
}

InstanceData *groupDefLayerScopeGetInstanceData(GroupDef *layer_def, const char *unique_name)
{
	InstanceData *ret = NULL;
	if (!layer_def->name_to_instance_data)
		layer_def->name_to_instance_data = stashTableCreateWithStringKeys(16, StashDeepCopyKeys_NeverRelease);
	stashFindPointer(layer_def->name_to_instance_data, unique_name, &ret);
	return ret;
}

bool groupDefScopeIsNameUsed(GroupDef *scope_def, const char *name)
{
	char *path_copy, *path, *c;
	int i;

	if (!scope_def->name_to_path || !stashFindPointer(scope_def->name_to_path, name, &path))
	{
		for (i = 0; i < eaSize(&scope_def->logical_groups); i++)
		{
			if (strcmpi(scope_def->logical_groups[i]->group_name, name) == 0)
				return true;
		}
		return false;
	}

	strdup_alloca(path_copy, path);
	c = strtok_r(path_copy, ",", &path_copy);
	while (c)
	{
		int uid = atoi(c);
		bool found = false;
		if (!uid)
			return groupDefScopeIsNameUsed(scope_def, c);

		for (i = 0; i < eaSize(&scope_def->children); i++)
		{
			if (scope_def->children[i]->uid_in_parent == uid)
			{
				scope_def = groupChildGetDef(scope_def, scope_def->children[i], false);
				found = true;
				break;
			}
		}

		if (!found || !scope_def)
			return false;
		c = strtok_r(path_copy, ",", &path_copy);

		if (scope_def->name_to_path && c)
			return groupDefScopeIsNameUsed(scope_def, c);
	}

	return groupDefNeedsUniqueName(scope_def) || (strnicmp(name, GROUP_UNNAMED_PREFIX, strlen(GROUP_UNNAMED_PREFIX)) == 0);
}

bool groupDefFindScopeNameByFullPath(GroupDef *scope_def, int *path, int path_size, const char **scope_name)
{
	int path_idx, child_idx;
	char path_str[256] = { 0 };
	GroupDef *child_def = scope_def;

	if (scope_def->path_to_name)
	{
		// Look for a child in this scope
		for (path_idx = 0; path_idx < path_size; path_idx++)
		{
			strcatf(path_str, "%d,", path[path_idx]);
		}
		if (stashFindPointer(scope_def->path_to_name, path_str, (char**)scope_name))
			return true;
	}

	path_str[0] = '\0';

	// Find child scope
	for (path_idx = 0; path_idx < path_size-1; path_idx++)
	{
		bool found = false;

		strcatf(path_str, "%d,", path[path_idx]);

		for (child_idx = 0; child_idx < eaSize(&child_def->children); child_idx++)
		{
			if (child_def->children[child_idx]->uid_in_parent == path[path_idx])
			{
				child_def = groupChildGetDef(child_def, child_def->children[child_idx], false);
				found = true;
				break;
			}
		}

		if (!found)
			return false;
		
		if (child_def->path_to_name)
		{
			// Look in the child scope
			char *child_scope_name = NULL;
			if (groupDefFindScopeNameByFullPath(child_def, &path[path_idx+1], path_size-(path_idx+1), &child_scope_name))
			{
				char path_str_2[256];
				sprintf(path_str_2, "%s%s,", path_str, child_scope_name);
				if (stashFindPointer(scope_def->path_to_name, path_str_2, (char**)scope_name))
					return true;
			}
			break;
		}
	}

	return false;
}

static bool groupDefScopeClearLogicalGroupsRecurse(LogicalGroup *logical_group, GroupDef *scope_def, StashTable name_to_lg, bool increment_mod_time)
{
	LogicalGroup *child;
	int i;

	for (i = eaSize(&logical_group->child_names) - 1; i >= 0 ; i--)
	{
		// recurse into logical groups
		if (stashFindPointer(name_to_lg, logical_group->child_names[i], &child))
		{
			if (!groupDefScopeClearLogicalGroupsRecurse(child, scope_def, name_to_lg, increment_mod_time))
			{
				StructFreeString(eaRemove(&logical_group->child_names, i));
				if (increment_mod_time)
					groupDefModify(scope_def, UPDATE_GROUP_PROPERTIES, true);
			}
		}
		// remove invalid children
		else if (!groupDefScopeIsNameUsed(scope_def, logical_group->child_names[i]))
		{
			char *path;
			if (stashRemovePointer(scope_def->name_to_path, logical_group->child_names[i], &path))
				stashRemovePointer(scope_def->path_to_name, path, NULL);
			StructFreeString(eaRemove(&logical_group->child_names, i));
			if (increment_mod_time)
				groupDefModify(scope_def, UPDATE_GROUP_PROPERTIES, true);
		}
	}

	// delete empty groups
	if (eaSize(&logical_group->child_names) == 0)
	{
		int index = eaFind(&scope_def->logical_groups, logical_group);
		stashRemovePointer(name_to_lg, logical_group->group_name, NULL);
		StructDestroy(parse_LogicalGroup, eaRemove(&scope_def->logical_groups, index));
		if (increment_mod_time)
			groupDefModify(scope_def, UPDATE_GROUP_PROPERTIES, true);
		return false;
	}
	return true;
}

void groupDefScopeClearInvalidEntries(GroupDef *scope_def, bool increment_mod_time)
{
	StashTableIterator iter;
	StashElement el;
	StashTable name_to_lg;
	StashTable name_to_parent_lg;
	int i, j;

	// only applies to scope defs
	if (!scope_def->path_to_name)
		return;

	// compile name-to-logical group stash for easy lookup without strcmp's
	name_to_lg = stashTableCreateWithStringKeys(16, StashDeepCopyKeys_NeverRelease);
	for (i = 0; i < eaSize(&scope_def->logical_groups); i++)
		assert(stashAddPointer(name_to_lg, scope_def->logical_groups[i]->group_name, scope_def->logical_groups[i], false));

	// compile parent group stash
	name_to_parent_lg = stashTableCreateWithStringKeys(16, StashDeepCopyKeys_NeverRelease);
	for (i = 0; i < eaSize(&scope_def->logical_groups); i++)
	{
		for (j = 0; j < eaSize(&scope_def->logical_groups[i]->child_names); j++)
		{
			if (stashFindPointer(name_to_lg, scope_def->logical_groups[i]->child_names[j], NULL))
				assert(stashAddPointer(name_to_parent_lg, scope_def->logical_groups[i]->child_names[j], scope_def->logical_groups[i], false));
		}
	}

	// starting at top-level logical groups (within the scope), remove invalid children from logical groups and remove empty groups
	for (i = eaSize(&scope_def->logical_groups) - 1; i >= 0; i--)
	{
		if (!stashFindPointer(name_to_parent_lg, scope_def->logical_groups[i], NULL))
			groupDefScopeClearLogicalGroupsRecurse(scope_def->logical_groups[i], scope_def, name_to_lg, increment_mod_time);
	}

	// remove remaining scope names that are invalid
	stashGetIterator(scope_def->path_to_name, &iter);
	while (stashGetNextElement(&iter, &el))
	{
		char *path = strdup(stashElementGetStringKey(el));
		char *name = strdup(stashElementGetPointer(el));

		// validate scope path
		if (!groupDefScopeIsNameUsed(scope_def, name))
		{
			stashRemovePointer(scope_def->path_to_name, path, NULL);
			stashRemovePointer(scope_def->name_to_path, name, NULL);
			if (increment_mod_time)
				groupDefModify(scope_def, UPDATE_GROUP_PROPERTIES, true);
		}

		SAFE_FREE(path);
#pragma warning(suppress:6001) // /analyze "Using uninitialized memory '*name'"
		SAFE_FREE(name);
	}

	stashTableDestroy(name_to_lg);
	stashTableDestroy(name_to_parent_lg);
}

/// Convert path (a comma separated list that maps to uid_in_parent)
/// to indxes in the GroupChild** children earray of each group.c
int* groupDefScopeGetIndexesFromPath(GroupDef* scope_def, const char* path)
{
	int* pathIndexes = NULL;
	
	if (path == NULL)
		return NULL;

	{
		GroupDef *defIt = scope_def;
		const char *pathIt = path;
		while (*pathIt)
		{
			int uid;
			int numChars;
			int it;

			if (defIt && sscanf(pathIt, "%d,%n", &uid, &numChars) > 0) {
				for( it = 0; it != eaSize(&defIt->children); ++it) {
					if( defIt->children[it]->uid_in_parent == uid ) {
						eaiPush( &pathIndexes, it );
						defIt = groupChildGetDef(defIt, defIt->children[it], false);
						pathIt += numChars;
						goto next;		//< can't use continue. :(
					}
				}
			}

			eaiDestroy( &pathIndexes );
			return NULL;

		next:;
		}

		if (!defIt)
		{
			eaiDestroy( &pathIndexes );
			return NULL;
		}
	}

	return pathIndexes;
}

/// Get the list of indexes into GroupChild** children from a logical
/// name.
int* groupDefScopeGetIndexesFromName(GroupDef* scope_def, const char* logical_name)
{
	char* path = NULL;
	
	if (!scope_def)
		return NULL;

	stashFindPointer(scope_def->name_to_path, logical_name, &path);
	if (!path) {
		return NULL;
	}
	
	return groupDefScopeGetIndexesFromPath(scope_def, path);
}

static void groupDefFixupMessage(DisplayMessage *message, GroupDef *def)
{
	int count;
	
	langMakeEditorCopy(parse_DisplayMessage, message, false);
	if( !message->pEditorCopy || !message->pEditorCopy->pcMessageKey ) {
		return;
	}
	
	//printf( "Changing key %s ", message->pEditorCopy->pcMessageKey );
	
	// Assume the format is as in groupDefMessageKey,
	// <filename>.<scope>.<def-name> or <filename>.<scope>.<def-name>.<int>
	count = strchrCount( message->pEditorCopy->pcMessageKey, '.' );
	if( count == 2 )
	{
		const char* firstDot;
		const char* lastDot;
		char scope[1024];
		
		firstDot = strchr( message->pEditorCopy->pcMessageKey, '.' );
		lastDot = strrchr( message->pEditorCopy->pcMessageKey, '.' );
		assert( firstDot && lastDot && firstDot < lastDot );
		
		memcpy( scope, firstDot + 1, lastDot - firstDot - 1 );
		scope[ lastDot - firstDot - 1 ] = '\0';
		groupDefFixupMessageKey( &message->pEditorCopy->pcMessageKey, def, scope, NULL );
	}
	else if( count == 3 )
	{
		const char* firstDot;
		const char* secondDot;
		const char* lastDot;
		char scope[1024];
		int optInt;
		
		firstDot = strchr( message->pEditorCopy->pcMessageKey, '.' );
		secondDot = strchr( firstDot + 1, '.' );
		lastDot = strchr( secondDot + 1, '.' );
		assert( firstDot && secondDot && lastDot );

		memcpy( scope, firstDot + 1, secondDot - firstDot - 1 );
		scope[ secondDot - firstDot - 1 ] = '\0';
		optInt = atoi( lastDot + 1 );
		groupDefFixupMessageKey( &message->pEditorCopy->pcMessageKey, def, scope, &optInt );
	}
	else
	{
		// Must be a badly formed message key... warn about it but DO NOT TOUCH!
		ErrorFilenamef( msgGetFilename(message->pEditorCopy),
						"Message key %s is badly formed.  It needs to be manually fixed up.  Talk to Jared F for more details.",
						message->pEditorCopy->pcMessageKey );
	}
	
	//printf( "to %s...\n", message->pEditorCopy->pcMessageKey );
}

void groupDefFixupMessages(GroupDef *def)
{
	langForEachDisplayMessage(parse_GroupProperties, &def->property_structs, groupDefFixupMessage, def);
}

void groupDefFixupMessageKey(const char** outMessageKey, GroupDef *def, const char* scope, const int* num)
{
	const char* filename = NULL;
	if( def->filename ) {
		filename = def->filename;
	} else if( SAFE_MEMBER2( def->def_lib, zmap_layer, filename )) {
		filename = def->def_lib->zmap_layer->filename;
	} else {
		// Shouldn't ever happen due to code structure...
		assert( false );
	}

	*outMessageKey = groupDefMessageKeyRaw(filename, def->name_str, scope, num, strStartsWith( *outMessageKey, "Tempbin_" ));
}

// preferTempbinName exists to prevent Genesis-created messages from
// ever getting updated.  It would be fine for preferTempbinName to be
// false if you were okay with every Genesis map's messages getting
// another translation pass.
const char* groupDefMessageKeyRaw(const char *layer_fname, const char *group_name, const char* scope, const int* num, bool preferTempbinName)
{
	char keyname[4096];
	char relative_fname[MAX_PATH] = { 0 };
	char filename[MAX_PATH];
	char def_name[4096];
	char ns[RESOURCE_NAME_MAX_SIZE];
	char *fixed_keyname = NULL;

	strcpy( def_name, group_name );
	strchrReplace( def_name, '.', '_' );

	fileRelativePath( layer_fname, relative_fname );

	// Prevent "_Autosave" from ever ending up in the message key 
	if( strEndsWith( relative_fname, ".autosave" )) {
		relative_fname[ strlen( relative_fname ) - strlen( ".autosave" )] = '\0';

		// detect ".%i" before the autosave.
		{
			char* ext = NULL;
			char* afterDigit = NULL;
			ext = strrchr( relative_fname, '.' );
			if( ext ) {
				long l = strtol(ext + 1, &afterDigit, 10 );
				if( (l || *(ext + 1) == '0') && afterDigit && !afterDigit[0]) {
					*ext = '\0';
				}
			}
		}

		// detect autosave path too
		strstriReplace( relative_fname, "/autosaves/", "/" );
	}
	
	if( resExtractNameSpace( relative_fname, ns, filename )) {
		changeFileExt( filename, "", filename );
		strchrReplace( filename, '.', '_' );
		strchrReplace( filename, '/', '_' );
		strchrReplace( filename, '\\', '_' );
		strcat( ns, ":" );
	} else {
		changeFileExt( relative_fname, "", filename );
		strchrReplace( filename, '.', '_' );
		strchrReplace( filename, '/', '_' );
		strchrReplace( filename, '\\', '_' );
	}

	// by now, ns is either the empty string, or the namespace name,
	// with a colon after it.

	if( num )
	{
		if( preferTempbinName )
		{
			sprintf( keyname, "%sTempbin_%s_Layer_Autogen.%s.%s.%d",
					 ns, filename, scope, def_name, *num );
		}
		else
		{
			sprintf( keyname, "%s%s.%s.%s.%d", ns, filename, scope, def_name, *num );
		}
	}
	else
	{
		if( preferTempbinName )
		{
			sprintf( keyname, "%sTempbin_%s_Layer_Autogen.%s.%s", ns, filename, scope, def_name );
		}
		else
		{
			sprintf( keyname, "%s%s.%s.%s", ns, filename, scope, def_name );
		}
	}
	
	if( resFixName( keyname, &fixed_keyname ))
	{
		strcpy( keyname, fixed_keyname );
		estrDestroy( &fixed_keyname );
	}
	return allocAddString( keyname );
}

////////////////////////////////////////////////////////////////////////////////
// GroupDefLib functions
////////////////////////////////////////////////////////////////////////////////

void groupLibInit(GroupDefLib *lib)
{
	lib->defs = stashTableCreateInt(1024);
	lib->def_name_table = stashTableCreateAddress(1024);
	lib->temporary_defs = stashTableCreateInt(1024);
	lib->dummy = false;
	lib->zmap_layer = NULL;
	lib->editing_lib = false;
	lib->was_fixed_up = false;
}

GroupDefLib *groupLibCreate(bool dummy)
{
	GroupDefLib *lib = calloc(1, sizeof(GroupDefLib));
	groupLibInit(lib);
	lib->dummy = dummy;
	return lib;
}

void groupLibFree(GroupDefLib *def_lib)
{
	FOR_EACH_IN_STASHTABLE(def_lib->defs, GroupDef, def) {
		groupDefFree(def);
	} FOR_EACH_END
	stashTableDestroy(def_lib->defs);
	stashTableDestroy(def_lib->def_name_table);
	stashTableDestroy(def_lib->temporary_defs);
	SAFE_FREE(def_lib);
}

void groupLibClear(GroupDefLib *def_lib)
{
	FOR_EACH_IN_STASHTABLE(def_lib->defs, GroupDef, def) {
		groupDefFree(def);
	} FOR_EACH_END
	stashTableDestroy(def_lib->defs);
	stashTableDestroy(def_lib->def_name_table);
	stashTableDestroy(def_lib->temporary_defs);
}

bool groupLibAddGroupDef(GroupDefLib *def_lib, GroupDef *def, GroupDef **dup)
{
	GroupDef *dup_internal = NULL;
	bool debug = objectLibraryInited() && (def_lib == objectLibraryGetDefLib() || def_lib == objectLibraryGetEditingDefLib()) && !isProductionMode();

	if (dup)
		*dup = NULL;
	else
		dup = &dup_internal;

	PERFINFO_AUTO_START_FUNC();

	if (!def_lib->dummy)
	{
		if (debug)
		{
			if (def_lib == objectLibraryGetDefLib())
				filelog_printf("objectLog", "groupLibAddGroupDef: %d %s to Object Library", def->name_uid, def->name_str);
			else if (def_lib == objectLibraryGetEditingDefLib())
				filelog_printf("objectLog", "groupLibAddGroupDef: %d %s to Editing Library", def->name_uid, def->name_str);

			devassert(strStartsWith(def->filename, "object_library"));
		}

		if (def_lib->editing_lib)
		{
			devassert(strStartsWith(def->filename, "object_library"));
		}

		if (!stashIntAddPointer(def_lib->defs, def->name_uid, def, false))
		{
			stashIntFindPointer(def_lib->defs, def->name_uid, dup);

			if (debug)
				filelog_printf("objectLog", "groupLibAddGroupDef failed: ID taken");

			assert((*dup) && (*dup)->name_uid == def->name_uid);
			PERFINFO_AUTO_STOP();
			return false; // ID already taken in this library
		}

		// Don't add invalid names or those of private children into names table
		if (!def->name_str)
		{
			ErrorFilenamef(def->filename, "Attempting to add group with invalid name: %d", def->name_uid);
		}
		else if (def->root_id == 0 && !stashAddInt(def_lib->def_name_table, def->name_str, def->name_uid, false))
		{
			if (debug)
			{
				int dup_id;
				stashFindInt(def_lib->def_name_table, def->name_str, &dup_id);
				filelog_printf("objectLog", "groupLibAddGroupDef: Name taken by group %d", dup_id);
			}

			ErrorFilenamef(def->filename, "Attempting to add group with non-unique name: %d (%s)", def->name_uid, def->name_str);
		}
	}

	def->def_lib = def_lib;

	PERFINFO_AUTO_STOP();
	return true;
}

void groupLibRemoveGroupDef(GroupDefLib *def_lib, GroupDef *def)
{
	stashIntRemovePointer(def_lib->defs, def->name_uid, NULL);
	stashRemoveInt(def_lib->def_name_table, def->name_str, NULL);
}

void groupLibAddTemporaryDef(GroupDefLib *def_lib, GroupDef *def, int uid)
{
	stashIntAddPointer(def_lib->temporary_defs, uid, def, false);
}

void groupLibRemoveTemporaryDef(GroupDefLib *def_lib, int uid)
{
	GroupDef *def = NULL;
	stashIntRemovePointer(def_lib->temporary_defs, uid, &def);
}

GroupDef *groupLibFindGroupDef(GroupDefLib *def_lib, int name_uid, bool allow_temporary_defs)
{
	GroupDef *def = NULL;

	if (stashIntFindPointer(def_lib->defs, name_uid, &def))
	{
		// Sanity checks
		assert(def->name_uid == name_uid);
		assert(def->def_lib == def_lib);
		return def;
	}

	if (allow_temporary_defs && stashIntFindPointer(def_lib->temporary_defs, name_uid, &def))
	{
		return def;
	}

	return NULL;
}

GroupDef *groupLibFindGroupDefByName(GroupDefLib *def_lib, const char *defname, bool allow_temporary_defs)
{
	int id;
	GroupDef *def = NULL;

	if (stashFindInt(def_lib->def_name_table, allocAddString(defname), &id))
		def = groupLibFindGroupDef(def_lib, id, allow_temporary_defs);
	return def;
}

bool groupLibIsValidGroupName(GroupDefLib *def_lib, const char *name, int parent_id)
{
	const char *alloced_name;
	if (parent_id == 0)
	{
		if (groupLibFindGroupDefByName(def_lib, name, true))
			return false;
		return true;
	}

	alloced_name = allocAddString(name);
	FOR_EACH_IN_STASHTABLE(def_lib->defs, GroupDef, def)
	{
		if (def->root_id == parent_id && def->name_str == alloced_name)
			return false;
	}
	FOR_EACH_END;
	return true;
}

void groupLibMakeGroupName(GroupDefLib *def_lib, const char *oldname, char *newname, int newname_size, int parent_id)
{
	char *s = NULL, namebase[1024];
	int idx = 1;

	if (def_lib->dummy && oldname)
	{
		strcpy_s(SAFESTR2(newname), oldname);
		return;
	}

	if (!oldname)
		oldname = "grp";

	strcpy(namebase, oldname);
	if ((s = strrchr(namebase, '#')) != 0)
		*s = 0;
	strcpy_s(SAFESTR2(newname), namebase);
	while (!groupLibIsValidGroupName(def_lib, newname, parent_id))
	{
		sprintf_s(SAFESTR2(newname), "%s#%d", namebase, idx++);
	}
}

GroupDef *groupLibNewGroupDef(GroupDefLib *def_lib, const char *filename, int defname_uid, const char *defname, int parent_id, bool update_mod_time, bool is_new)
{
	GroupDef *def;
	GroupDef *dup;

	if (!def_lib)
	{
		if (!world_grid.dummy_lib)
			world_grid.dummy_lib = groupLibCreate(true);
		def_lib = world_grid.dummy_lib;
	}

	if (!def_lib->dummy && !def_lib->editing_lib)
	{
		if (defname_uid && objectLibraryGetGroupDef(defname_uid, false))
		{
			if (!isProductionMode())
				filelog_printf("objectLog", "groupLibNewGroupDef FAILED: UID in use %d", defname_uid);
			return NULL;
		}
	}

	def = StructCreate(parse_GroupDef);

	if (update_mod_time && (!def_lib->zmap_layer || !def_lib->zmap_layer->dummy_layer))
		journalDef(def);

	def->def_lib = def_lib;
	def->filename = filename;
	def->name_uid = defname_uid;
	def->name_str = allocAddString(defname);
	def->is_new = is_new;
	def->is_dynamic = false;
	def->root_id = parent_id;
	setVec3(def->model_scale, 1, 1, 1);

	if (!def->name_uid)
	{
		char write_file[MAX_PATH];
		if (filename)
			fileLocateWrite(filename, write_file);
		else
			write_file[0] = 0;
		def->name_uid = worldGenerateDefNameUID(def->name_str, def, !def_lib->zmap_layer, strStartsWith(write_file, fileCoreDataDir()));
	}

	if (!groupLibAddGroupDef(def_lib, def, &dup))
	{
		if (!isProductionMode())
			filelog_printf("objectLog", "groupLibNewGroupDef FAILED: %d %s IN %s", def->name_uid, def->name_str, def->filename);
		StructDestroy(parse_GroupDef, def);
		return NULL;
	}

	if (def_lib->editing_lib)
	{
		objectLibraryAddEditingCopy(def);
	}

	if (update_mod_time)
		groupDefModify(def, UPDATE_GROUP_PROPERTIES, true);

	if (!def_lib->dummy && !def_lib->editing_lib && !isProductionMode())
		filelog_printf("objectLog", "groupLibNewGroupDef SUCCEEDED: %d %s IN %s", def->name_uid, def->name_str, def->filename);

	return def;
}

void groupGetInheritedName(char *newName, int newName_size, GroupDef *parent, GroupDef *child)
{
	// if we're moving the child of an object library piece, we need to make it private
	if (groupIsPublic(parent))
		sprintf_s(SAFESTR2(newName), "%s_1&", parent->name_str);
	else if (groupIsPrivate(parent))
		strcpy_s(SAFESTR2(newName), parent->name_str);
	else
		strcpy_s(SAFESTR2(newName), child->name_str);
}

bool groupShouldDoTransfer(GroupDef *parent, GroupDef *child)
{
	if (groupIsObjLib(parent) && (!groupIsObjLib(child) || groupIsPrivate(child)))
		return true; // Moving a layer def into an Object Library piece
	if (!groupIsObjLib(parent) && groupIsPrivate(child))
		return true; // Instancing a private def from the Object Library to the layer
	if (!groupIsObjLib(parent) && !groupIsObjLib(child) && child->filename != parent->filename)
		return true; // Moving a def from layer to layer
	return false;
}

static GroupDef *groupLibCopyGroupDefRecurse(GroupDefLib *destlib, const char *filename, GroupDef *srcgroup, const char *srcname, bool increment_mod_time, bool keep_old_message_keys, bool copy_identical, StashTable copy_table, int parent_id, bool force_new)
{
	GroupDef *destgroup;
	char newdefname[1024];
	const char *oldname;
	const char *name;
	int i, uid;
	bool created_table = false;
	static int nest = 0;
	bool is_new = true;

	PERFINFO_AUTO_START("groupLibCopyGroupDef",1);

	if (!destlib->editing_lib)
	{
		if (!isProductionMode() && !isProductionEditMode())
			filelog_printf("objectLog", "groupLibCopyGroupDef SOURCE %d: %d %s IN %s", ++nest, srcgroup->name_uid, srcgroup->name_str, srcgroup->filename);
	}
	else
	{
		if (!isProductionMode() && !isProductionEditMode())
			filelog_printf("objectLog", "groupLibCopyGroupDef to Editing Lib SOURCE %d: %d %s IN %s", ++nest, srcgroup->name_uid, srcgroup->name_str, srcgroup->filename);
		if (!srcgroup->is_new && !force_new)
			is_new = false;
	}

	if (copy_table == NULL)
	{
		copy_table = stashTableCreateAddress(10);
		created_table = true;
	}

	assert(destlib->zmap_layer || destlib->editing_lib || destlib->dummy);

	if (!filename && destlib->zmap_layer && destlib->zmap_layer->filename)
		filename = destlib->zmap_layer->filename;
	filename = allocAddFilename(filename);

	if (srcname)
	{
		oldname = srcname;
	}
	else
	{
		oldname = srcgroup->name_str;
	}

	if (destlib && !copy_identical)
		groupLibMakeGroupName(destlib, oldname, SAFESTR(newdefname), parent_id);
	else
		strcpy(newdefname, oldname);
	destgroup = groupLibNewGroupDef(destlib, filename, copy_identical ? srcgroup->name_uid : 0, newdefname, parent_id, increment_mod_time, is_new);

	if (!destgroup)
	{
		ErrorFilenamef(srcgroup->filename, "Error loading layer: Duplicate ID %d - two groups with same ID or private group referenced by two Object Library pieces!", srcgroup->name_uid);
		return objectLibraryGetGroupDef(-1, false); // Return a "deleted" dummy
	}

	// TODO: copy scope data?

	uid = destgroup->name_uid;
	name = destgroup->name_str;
	StructCopy(parse_GroupDef, srcgroup, destgroup, STRUCTCOPYFLAG_DONT_COPY_NO_ASTS, 0, 0);
	destgroup->name_uid = uid;
	destgroup->name_str = name;
	destgroup->def_lib = destlib;
	destgroup->filename = filename;
	destgroup->texture_swaps = NULL;
	destgroup->material_swaps = NULL;
	destgroup->instance_buffers = NULL;
	destgroup->root_id = parent_id;

	groupFixupAfterRead(destgroup, false);

	stashAddPointer(copy_table, srcgroup, destgroup, false);

	for (i = 0; i < eaSize(&destgroup->children); i++)
	{
		GroupDef *child_def = groupChildGetDef(srcgroup, srcgroup->children[i], false);

		if (groupShouldDoTransfer(destgroup, child_def))
		{
			GroupDef *existing_def = NULL;
			if (stashFindPointer(copy_table, child_def, &existing_def))
			{
				child_def = existing_def;
			}
			else
			{
				int new_parent_id = 0;
				if (!destlib->zmap_layer &&
					(!groupIsObjLib(child_def) || groupIsPrivate(child_def)))
					new_parent_id = (parent_id != 0) ? parent_id : destgroup->name_uid;
				child_def = groupLibCopyGroupDefRecurse(destlib, filename, child_def, child_def->name_str, increment_mod_time, keep_old_message_keys, copy_identical, copy_table, new_parent_id, force_new);
			}
		}
		groupChildInitialize(destgroup, i, child_def, srcgroup->children[i]->mat, srcgroup->children[i]->scale, srcgroup->children[i]->seed, srcgroup->children[i]->uid_in_parent);
	}

	if (destlib && destgroup->name_str[0] != '^' && !copy_identical && increment_mod_time)
		groupDefModify(destgroup, UPDATE_GROUP_PROPERTIES, true);

	if (!keep_old_message_keys)
		groupDefFixupMessages(destgroup);

	destgroup->group_flags = srcgroup->group_flags;

	groupPostLoad(destgroup);

	if (created_table)
		stashTableDestroy(copy_table);

	if (!isProductionMode() && !isProductionEditMode())
	{
		if (!destlib->editing_lib)
			filelog_printf("objectLog", "groupLibCopyGroupDef DEST %d: %d %s IN %s (ROOT %d)", nest--, destgroup->name_uid, destgroup->name_str, destgroup->filename, destgroup->root_id);
		else
			filelog_printf("objectLog", "groupLibCopyGroupDef to Editing Lib DEST %d: %d %s IN %s (ROOT %d)", nest--, destgroup->name_uid, destgroup->name_str, destgroup->filename, destgroup->root_id);
	}

	PERFINFO_AUTO_STOP();

	return destgroup;
}

GroupDef *groupLibCopyGroupDef(GroupDefLib *destlib, const char *filename, GroupDef *srcgroup, const char *srcname, bool increment_mod_time, bool keep_old_message_keys, bool copy_identical, int parent_id, bool force_new)
{
	StashTable copy_table = stashTableCreateAddress( 10 );
	GroupDef* copy_def = groupLibCopyGroupDefRecurse(destlib, filename, srcgroup, srcname, increment_mod_time, keep_old_message_keys, copy_identical, copy_table, parent_id, force_new);

	// Fix up golden path links
	FOR_EACH_IN_STASHTABLE( copy_table, GroupDef, newDef ) {
		if( newDef->property_structs.path_node_properties ) {
			FOR_EACH_IN_EARRAY( newDef->property_structs.path_node_properties->eaConnections, WorldPathEdge, connection ) {
				GroupDef* oldOtherDef = groupLibFindGroupDef( srcgroup->def_lib, connection->uOther, false );
				GroupDef* newOtherDef = NULL;
				stashFindPointer( copy_table, oldOtherDef, &newOtherDef );

				// MJF (Sep/25/2012) -- When duplicating a node, it
				// may reference things that AREN'T copied here.  This
				// code will leave those as one-way links.  Then
				// layerUpdatePathNodeTrackers fixes up those one way
				// links.
				if( newOtherDef ) {
					connection->uOther = newOtherDef->name_uid;
				}
			} FOR_EACH_END;
		}
	} FOR_EACH_END;
		
	stashTableDestroy( copy_table );
	return copy_def;
}


GroupDef **groupLibGetDefEArray(GroupDefLib *def_lib)
{
	static GroupDef **defs;
	StashTableIterator iter;
	StashElement elem;

	eaSetSize(&defs, 0);

	stashGetIterator(def_lib->defs, &iter);

	while (stashGetNextElement(&iter, &elem))
	{
		GroupDef *def = stashElementGetPointer(elem);
		if (def)
			eaPush(&defs, def);
	}

	return defs;
}

void groupLibMarkBadChildren(GroupDefLib *def_lib)
{
	GroupDef **lib_defs = groupLibGetDefEArray(def_lib);
	FOR_EACH_IN_EARRAY(lib_defs, GroupDef, def)
	{
		// Check children
		FOR_EACH_IN_EARRAY(def->children, GroupChild, child)
		{
			GroupDef *child_def = groupChildGetDef(def, child, true);
			if (child_def == objectLibraryGetDummyGroupDef())
			{
				ErrorFilenamef(def->filename, "GroupDef %s (%d) references missing child %s (%d). The child will show up as (deleted).",
					def->name_str, def->name_uid, child->debug_name, child->name_uid);
			}
		}
		FOR_EACH_END;
	}
	FOR_EACH_END;
}

static bool groupLibConsistencyCheckIsTrue(GroupDef *def, bool check, const char *message, const char *other_str, GroupDef *other_def, int other_id, bool do_assert)
{
	if (!check && do_assert)
	{
		char srv_log_file[MAX_PATH], cli_log_file[MAX_PATH], out_file[MAX_PATH];
		char *temp;
		U32 timestamp = timeSecondsSince2000();
		const char *username = gimmeDLLQueryUserName();
		filelog_printf("objectLog", "groupLibConsistencyCheckIsTrue FAILED!");
		filelog_printf("objectLog", "Failed def: %d %s", def->name_uid, def->name_str);
		if (other_def)
			filelog_printf("objectLog", "Other def: %d %s", other_def->name_uid, other_def->name_str);
		if (other_str)
			filelog_printf("objectLog", "Other name: %s", other_str);
		if (other_id)
			filelog_printf("objectLog", "Invalid ID: %d", other_id);
		filelog_printf("objectLog", "Failure message: %s", message);

		logFlushFile("objectLog");
		logWaitForQueueToEmpty();
		logGetFilename("objectLog.log", SAFESTR(srv_log_file));
		strcpy(cli_log_file, srv_log_file);

		if (wlIsServer())
		{
			if (temp = strstr(cli_log_file, "server"))
				memcpy(temp, "client", 6);
			else
				cli_log_file[0] = '\0';
		}
		else
		{
			if (temp = strstr(srv_log_file, "client"))
				memcpy(temp, "server", 6);
			else
				srv_log_file[0] = '\0';
		}
		sprintf(out_file, "N:\\ObjectCrashLogs\\%s_%X\\Server_objectLog.log", username, timestamp);
		makeDirectoriesForFile(out_file);
		fileCopy(srv_log_file, out_file);
		sprintf(out_file, "N:\\ObjectCrashLogs\\%s_%X\\Client_objectLog.log", username, timestamp);
		fileCopy(cli_log_file, out_file);
		assertmsg(0, "Object Library internal error!");
	}
	if (!check)
	{
		printf("CONSISTENCY CHECK FAILED!\n");
		printf("Failed def: %d %s\n", def->name_uid, def->name_str);
		if (other_def)
			printf("Other def: %d %s\n", other_def->name_uid, other_def->name_str);
		if (other_str)
			printf("Other name: %s\n", other_str);
		if (other_id)
			printf("Invalid ID: %d\n", other_id);
		printf("Failure message: %s\n", message);
	}
	return check;
}

static bool groupLibConsistencyCheck(GroupDefLib *def_lib, GroupDef *def, bool do_assert)
{
	GroupDef *check_def;
	char *name_str = NULL;
	int check_id = 0;
	bool def_is_good = true;

	if (def->name_uid == -1)
		return true; // Dummy group

	// Check filename
	if (!groupLibConsistencyCheckIsTrue(def, (def->filename != NULL), "ObjectLibrary: Def filename is NULL", NULL, NULL, 0, do_assert))
		return false;
	if (def_lib->zmap_layer)
	{
		if (!groupLibConsistencyCheckIsTrue(def, stricmp(def->filename, def_lib->zmap_layer->filename) == 0, "ObjectLibrary: Def filename does not match layer filename", NULL, NULL, 0, do_assert))
			return false;
	}
	else
	{
		if (!groupLibConsistencyCheckIsTrue(def, strStartsWith(def->filename, "object_library"), "ObjectLibrary: Def filename is not in object library", NULL, NULL, 0, do_assert))
			return false;
	}

	// Check def lib pointer
	if (!groupLibConsistencyCheckIsTrue(def, (def->def_lib == def_lib), "ObjectLibrary: Def library pointer is correct", NULL, NULL, 0, do_assert))
		return false;

	if (def->root_id != 0)
	{
		// Check root def
		GroupDef *root_def = groupLibFindGroupDef(def_lib, def->root_id, false);

		// If the root def is not in the passed in GroupDefLib, check the object library. We only
		// want to know if the root_def exists, we don't care if it's in the same GroupDefLib
		if (!root_def)
			root_def = objectLibraryGetGroupDef(def->root_id, false);

		if (!groupLibConsistencyCheckIsTrue(def, root_def != NULL, "ObjectLibrary: Root ID references invalid def", NULL, NULL, def->root_id, do_assert))
			return false;
	}

	// Check defs table
	if (!groupLibConsistencyCheckIsTrue(def, stashIntFindPointer(def_lib->defs, def->name_uid, &check_def), "ObjectLibrary: Def in defs list", NULL, NULL, 0, do_assert))
		return false;
	if (!groupLibConsistencyCheckIsTrue(def, check_def == def, "ObjectLibrary: Def matches defs list def", NULL, check_def, 0, do_assert))
		return false;

	return true;
}

bool groupLibConsistencyCheckAll(GroupDefLib *def_lib, bool do_assert)
{
	GroupDef **lib_defs = groupLibGetDefEArray(def_lib);
	FOR_EACH_IN_EARRAY(lib_defs, GroupDef, def)
	{
		if (!groupLibConsistencyCheck(def_lib, def, do_assert))
			return false;
	}
	FOR_EACH_END;
	return true;
}

void groupTrackerBuildPathNodeTrackerTable(GroupTracker *tracker, StashTable stTrackersByDefID)
{
	if (tracker)
	{
		S32 i;

		for (i = 0; i < tracker->child_count; ++i)
		{
			groupTrackerBuildPathNodeTrackerTable(tracker->children[i], stTrackersByDefID);
		}

		if (tracker->world_path_node)
		{
			stashIntAddPointer( stTrackersByDefID, tracker->def->name_uid, tracker, false );
		}
	}
}

// ----------------------------------------------------------------------------------
// WorldOptionalActionCategoryDef initialization
// ----------------------------------------------------------------------------------

// Context to track all data-defined modes
WorldOptionalActionCategoryDef **g_eaOptionalActionCategoryDefs = NULL;

AUTO_STARTUP(WorldOptionalActionCategories);
void worldoptionalactioncategory_LoadCategories(void)
{
	WorldOptionalActionCategoryDefs loaderStruct = {0};

	loadstart_printf("Loading Optional Action Categories... ");

	ParserLoadFiles(NULL, "defs/config/optionalactioncategories.def", "optionalactioncategories.bin", PARSER_OPTIONALFLAG, parse_WorldOptionalActionCategoryDefs, &loaderStruct);
	g_eaOptionalActionCategoryDefs = loaderStruct.eaCategories;

	loadend_printf(" done (%d OptionalActionCategories)", eaSize(&g_eaOptionalActionCategoryDefs));
}

/// Fixup function for DoorInteractionProperties
TextParserResult fixupWorldDoorInteractionProperties(WorldDoorInteractionProperties *pDoorProperties, enumTextParserFixupType eType, void *pExtraData)
{
	switch (eType)
	{
		case FIXUPTYPE_POST_TEXT_READ: case FIXUPTYPE_POST_BIN_READ: {
			// Fixup the old variable list to the new variable def list
			int it;
			eaSetCapacity( &pDoorProperties->eaVariableDefs, eaSize( &pDoorProperties->eaVariableDefs ) + eaSize( &pDoorProperties->eaOldVariables ));
			for( it = 0; it != eaSize( &pDoorProperties->eaOldVariables ); ++it ) {
				WorldVariable* var = pDoorProperties->eaOldVariables[ it ];
				WorldVariableDef* new_def = StructCreate( parse_WorldVariableDef );
				new_def->pcName = var->pcName;
				var->pcName = NULL;
				new_def->eType = var->eType;
				new_def->eDefaultType = WVARDEF_SPECIFY_DEFAULT;
				new_def->pSpecificValue = var;
				
				eaPush( &pDoorProperties->eaVariableDefs, new_def );
			}
			eaClear( &pDoorProperties->eaOldVariables );

			// Fixup the old doorDest format into the new format
			if (pDoorProperties->bOldUseChoiceTable || pDoorProperties->pcOldMapName || pDoorProperties->pcOldSpawnTargetName) {
				pDoorProperties->doorDest.eType = WVAR_MAP_POINT;
					
				if (pDoorProperties->bOldUseChoiceTable) {
					pDoorProperties->doorDest.eDefaultType = WVARDEF_CHOICE_TABLE;
					COPY_HANDLE(pDoorProperties->doorDest.choice_table, pDoorProperties->hOldChoiceTable);
					StructCopyString(&pDoorProperties->doorDest.choice_name, pDoorProperties->pcOldChoiceName);
				} else {
					pDoorProperties->doorDest.eDefaultType = WVARDEF_SPECIFY_DEFAULT;
					pDoorProperties->doorDest.pSpecificValue = StructCreate( parse_WorldVariable );
					pDoorProperties->doorDest.pSpecificValue->eType = WVAR_MAP_POINT;
					StructCopyString(&pDoorProperties->doorDest.pSpecificValue->pcZoneMap, pDoorProperties->pcOldMapName);
					StructCopyString(&pDoorProperties->doorDest.pSpecificValue->pcStringVal, pDoorProperties->pcOldSpawnTargetName);
				}
			}
			pDoorProperties->bOldUseChoiceTable = false;
			StructFreeStringSafe(&pDoorProperties->pcOldMapName);
			StructFreeStringSafe(&pDoorProperties->pcOldSpawnTargetName);
			REMOVE_HANDLE(pDoorProperties->hOldChoiceTable);
			StructFreeStringSafe(&pDoorProperties->pcOldChoiceName);
		}
	}

	return PARSERESULT_SUCCESS;
}

/// Fixup function for WorldWarpVolumeProperties
TextParserResult fixupWorldWarpVolumeProperties(WorldWarpVolumeProperties *pWarpProperties, enumTextParserFixupType eType, void *pExtraData)
{
	switch (eType)
	{
		case FIXUPTYPE_POST_TEXT_READ: case FIXUPTYPE_POST_BIN_READ: {
			// Fixup the old variable list to the new variable def list
			int it;
			eaSetCapacity( &pWarpProperties->variableDefs, eaSize( &pWarpProperties->variableDefs ) + eaSize( &pWarpProperties->oldVariables ));
			for( it = 0; it != eaSize( &pWarpProperties->oldVariables ); ++it ) {
				WorldVariable* var = pWarpProperties->oldVariables[ it ];
				WorldVariableDef* new_def = StructCreate( parse_WorldVariableDef );
				new_def->pcName = var->pcName;
				var->pcName = NULL;
				new_def->eType = var->eType;
				new_def->eDefaultType = WVARDEF_SPECIFY_DEFAULT;
				new_def->pSpecificValue = var;

				eaPush( &pWarpProperties->variableDefs, new_def );
			}
			eaClear( &pWarpProperties->oldVariables );

			// Fixup the old warp destination format
			if (pWarpProperties->oldUseChoiceTable || pWarpProperties->old_map_name || pWarpProperties->old_spawn_target_name) {
				pWarpProperties->warpDest.eType = WVAR_MAP_POINT;
				
				if (pWarpProperties->oldUseChoiceTable) {
					pWarpProperties->warpDest.eDefaultType = WVARDEF_CHOICE_TABLE;
					COPY_HANDLE(pWarpProperties->warpDest.choice_table, pWarpProperties->old_choice_table);
					StructCopyString(&pWarpProperties->warpDest.choice_name, pWarpProperties->old_choice_name);
				} else {
					pWarpProperties->warpDest.eDefaultType = WVARDEF_SPECIFY_DEFAULT;
					pWarpProperties->warpDest.pSpecificValue = StructCreate( parse_WorldVariable );
					pWarpProperties->warpDest.pSpecificValue->eType = WVAR_MAP_POINT;
					StructCopyString(&pWarpProperties->warpDest.pSpecificValue->pcZoneMap, pWarpProperties->old_map_name);
					StructCopyString(&pWarpProperties->warpDest.pSpecificValue->pcStringVal, pWarpProperties->old_spawn_target_name);
				}
			}
			pWarpProperties->oldUseChoiceTable = false;
			StructFreeStringSafe(&pWarpProperties->old_map_name);
			StructFreeStringSafe(&pWarpProperties->old_spawn_target_name);
			REMOVE_HANDLE(pWarpProperties->old_choice_table);
			StructFreeStringSafe(&pWarpProperties->old_choice_name);
		}
	}
	
	return PARSERESULT_SUCCESS;
}


/// Fixup function for WorldWarpActionProperties
TextParserResult fixupWorldWarpActionProperties(WorldWarpActionProperties *pWarpProperties, enumTextParserFixupType eType, void *pExtraData)
{
	switch (eType)
	{
		case FIXUPTYPE_POST_TEXT_READ: case FIXUPTYPE_POST_BIN_READ: {
			// Fixup the old variable list to the new variable def list
			int it;
			eaSetCapacity( &pWarpProperties->eaVariableDefs, eaSize( &pWarpProperties->eaVariableDefs ) + eaSize( &pWarpProperties->eaOldVariables ));
			for( it = 0; it != eaSize( &pWarpProperties->eaOldVariables ); ++it ) {
				WorldVariable* var = pWarpProperties->eaOldVariables[ it ];
				WorldVariableDef* new_def = StructCreate( parse_WorldVariableDef );
				new_def->pcName = var->pcName;
				var->pcName = NULL;
				new_def->eType = var->eType;
				new_def->eDefaultType = WVARDEF_SPECIFY_DEFAULT;
				new_def->pSpecificValue = var;

				eaPush( &pWarpProperties->eaVariableDefs, new_def );
			}
			eaClear( &pWarpProperties->eaOldVariables );

			// Fixup the old warp destination format
			if (pWarpProperties->pcOldMapName || pWarpProperties->pcOldSpawnTargetName) {
				pWarpProperties->warpDest.eType = WVAR_MAP_POINT;
				
				pWarpProperties->warpDest.eDefaultType = WVARDEF_SPECIFY_DEFAULT;
				pWarpProperties->warpDest.pSpecificValue = StructCreate( parse_WorldVariable );
				pWarpProperties->warpDest.pSpecificValue->eType = WVAR_MAP_POINT;
				StructCopyString(&pWarpProperties->warpDest.pSpecificValue->pcZoneMap, pWarpProperties->pcOldMapName);
				StructCopyString(&pWarpProperties->warpDest.pSpecificValue->pcStringVal, pWarpProperties->pcOldSpawnTargetName);
			}
			StructFreeStringSafe(&pWarpProperties->pcOldMapName);
			StructFreeStringSafe(&pWarpProperties->pcOldSpawnTargetName);
		}
	}
	
	return PARSERESULT_SUCCESS;
}

TextParserResult fixupWorldSendNotificationActionProperties( WorldSendNotificationActionProperties* pSendNotificationProperties, enumTextParserFixupType eType, void* pExtraData )
{
	switch( eType ) {
		xcase FIXUPTYPE_POST_TEXT_READ:
			if( pSendNotificationProperties->pchSound_Deprecated ) {
				pSendNotificationProperties->notifyMsg.bHasVO = true;
				pSendNotificationProperties->notifyMsg.astrLegacyAudioEvent = allocAddString( pSendNotificationProperties->pchSound_Deprecated );
				pSendNotificationProperties->pchSound_Deprecated = NULL;
			}
	}

	return PARSERESULT_SUCCESS;
}


// Runtime defined activity types
DefineContext *g_pDefineActivityLogTypes = NULL;
DefineContext *g_ExtraMissionPlayTypes = NULL;

//
// Load the enums separately because they are needed for schema writing and the other config loads are not
//
AUTO_STARTUP(AS_ActivityLogEntryTypes);
void ActivityLog_LoadEntryTypes(void)
{
	// Don't load on app servers, other than login server and auction server
	if (IsAppServerBasedType() && !IsLoginServer() && !IsAuctionServer() && !IsGuildServer()) {
		return;
	}

	g_pDefineActivityLogTypes = DefineCreate();
	DefineLoadFromFile(g_pDefineActivityLogTypes, "ActivityLogEntryType", "ActivityLogEntryTypes", NULL,  "defs/config/ActivityLogEntryTypes.def", "ActivityLogEntryTypes.bin", ActivityLogEntryType_MAX);
}

AUTO_STARTUP(MissionPlayTypes);
void MissiosnPlayTypes_Load(void)
{
	// Load the extra mission types
	g_ExtraMissionPlayTypes = DefineCreate();
	DefineLoadFromFile(g_ExtraMissionPlayTypes, "MissionPlayType", "MissionPlayTypes", NULL, "defs/config/MissionPlayTypes.def", "MissionPlayTypes.bin", MissionPlay_End);
	
	// WOLF[1Jun12] There was an unfortunate problem with the enum definition for MissionPlayType which resulted in the keyword "End" being used in many
	//   old projects as meaning "NonCombat". To fix this, we are going to add "End" to the enum with the same value as "NonCombat", which is to say
	//   MissionPlay_End. We do it here, and by hand, so that the enum value '1' will correctly write out 'NonCombat' in text files. We just need it to
	//   read in correctly. There will obviously be problems if values are added to the dyn file before NonCombat.

	{
		bool bOverlapOkay=true;
		DefineAddIntByHandle(&g_ExtraMissionPlayTypes, "End", MissionPlay_End, bOverlapOkay); // This is what is called internally by DefineLoadFromFile
	}
}

#include "ActivityLogEnums_h_ast.c"
