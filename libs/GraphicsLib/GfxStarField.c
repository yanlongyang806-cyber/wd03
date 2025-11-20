#include "MemRef.h"
#include "rand.h"
#include "rgb_hsv.h"
#include "earray.h"

#include "GfxStarField.h"
#include "GfxSky.h"
#include "GfxGeo.h"
#include "Materials.h"


AUTO_RUN_ANON(memBudgetAddMapping(__FILE__, BUDGET_Renderer););

static void getStarFieldInfo(GeoRenderInfo *geo_render_info, SkyDrawable *drawable, int param, GeoRenderInfoDetails *details)
{
	char buf[128];
	sprintf(buf, "StarField_%s_%d", drawable->material->material_name, drawable->star_field->seed);
	details->name = allocAddString(buf);
	details->filename = NULL;
	details->far_dist = 10000;
	details->uv_density = -1;
}

static bool CALLBACK fillStarField(GeoRenderInfo *geo_render_info, SkyDrawable *drawable, int param)
{
	U8 *extra_data_ptr;
	U32 subobj_size = sizeof(int);
	int i, j;
	int idxcount;
	U32 total,vert_total,tri_bytes,subobj_bytes;
	int star_count = eaSize(&drawable->star_data);

	RdrVertexDeclaration *vdecl;
	assert(geo_render_info->usage.iNumPrimaryStreams == 1);
	vdecl = rdrGetVertexDeclaration(geo_render_info->usage.bits[0]);

	geo_render_info->tri_count = star_count * 2;
	geo_render_info->vert_count = star_count * 4;
	geo_render_info->subobject_count = 1;

	idxcount = geo_render_info->tri_count * 3;
	tri_bytes = idxcount * sizeof(U16);
	tri_bytes += tri_bytes % 4; // align end to 4 byte boundary so floats can be copied
	vert_total = vdecl->stride * geo_render_info->vert_count;
	subobj_bytes = sizeof(int);
	total = vert_total + tri_bytes + subobj_bytes + subobj_bytes;

	geo_render_info->vbo_data_primary.data_size = total;
	extra_data_ptr = geo_render_info->vbo_data_primary.data_ptr = memrefAlloc(total);

	geo_render_info->vbo_data_primary.tris = (U16 *)extra_data_ptr;
	extra_data_ptr += tri_bytes;

	for (i = 0; i < star_count; i++)
	{
		geo_render_info->vbo_data_primary.tris[i*6+0] = i*4+0;
		geo_render_info->vbo_data_primary.tris[i*6+1] = i*4+1;
		geo_render_info->vbo_data_primary.tris[i*6+2] = i*4+2;

		geo_render_info->vbo_data_primary.tris[i*6+3] = i*4+0;
		geo_render_info->vbo_data_primary.tris[i*6+4] = i*4+2;
		geo_render_info->vbo_data_primary.tris[i*6+5] = i*4+3;
	}

	geo_render_info->vbo_data_primary.vert_data = extra_data_ptr;
	for (i = 0; i < star_count; i++)
	{
		StarData *star = drawable->star_data[i];

		for (j = 0; j < 4; ++j)
		{
			Vec3 normal;
			F32 *vert = (F32*)(extra_data_ptr + vdecl->position_offset);
			Vec3_Packed *norm = (Vec3_Packed*)(extra_data_ptr + vdecl->normal_offset);
			F16 *texcoord = (F16*)(extra_data_ptr + vdecl->texcoord_offset);
			F32 *boneweights = (F32*)(extra_data_ptr + vdecl->boneweight_offset);

			copyVec3(star->center, vert);

			if (j == 1 || j == 2)
			{
				copyVec3(star->xvec, normal);
				texcoord[0] = F32toF16(0);
			}
			else
			{
				negateVec3(star->xvec, normal);
				texcoord[0] = F32toF16(1);
			}

			if (j & 2)
			{
				addVec3(normal, star->yvec, normal);
				texcoord[1] = F32toF16(0);
			}
			else
			{
				subVec3(normal, star->yvec, normal);
				texcoord[1] = F32toF16(1);
			}

			copyVec3Packed(normal, *norm);

			texcoord[2] = F32toF16(star->random1);
			texcoord[3] = F32toF16(star->random2);

			copyVec3(star->color, boneweights);
			boneweights[3] = star->size;

			extra_data_ptr += vdecl->stride;
		}
	}

	geo_render_info->vbo_data_primary.subobject_tri_bases = (int*)extra_data_ptr;
	extra_data_ptr += subobj_bytes;

	geo_render_info->vbo_data_primary.subobject_tri_bases[0] = 0;

	geo_render_info->vbo_data_primary.subobject_tri_counts = (int*)extra_data_ptr;
	extra_data_ptr += subobj_bytes;

	geo_render_info->vbo_data_primary.subobject_tri_counts[0] = geo_render_info->tri_count;

	return true;
}

static void CALLBACK freeStarFieldMemory(GeoRenderInfo *geo_render_info, SkyDrawable *drawable, int param)
{
	// Nothing to free
}

static void updateStarField(SkyDrawable *drawable)
{
	RdrGeoUsage usage = {0};
	if (drawable->geo_render_info)
		gfxFreeGeoRenderInfo(drawable->geo_render_info, false);

	usage.bits[0] = usage.key = RUSE_POSITIONS|RUSE_NORMALS|RUSE_TEXCOORDS|RUSE_TEXCOORD2S|RUSE_BONEWEIGHTS|RUSE_BONEWEIGHTS_HI_FLAG;
	usage.iNumPrimaryStreams = 1;
	usage.bHasSecondary = false;

	drawable->geo_render_info = gfxCreateGeoRenderInfo(fillStarField, freeStarFieldMemory, getStarFieldInfo, 
								false, drawable, 0, usage, WL_FOR_WORLD|WL_FOR_UTIL, true);
}

static void createStarData(SkyDrawable *drawable)
{
	U32 i;
	F32 slice_angle = RAD(CLAMP(drawable->star_field->slice_angle, 0.1, 180));
	F32 sin_half_angle = sinf(0.5f * slice_angle);
	F32 inv_sin_half_angle = 1.f / sin_half_angle;
	Mat3 axis_orient_mat;
	MersenneTable* pRandTable = mersenneTableCreate(drawable->star_field->seed);

	if (normalVec3(drawable->star_field->slice_axis) > 0)
		mat3FromUpVector(drawable->star_field->slice_axis, axis_orient_mat);
	else
		copyMat3(unitmat, axis_orient_mat);

	for (i = 0; i < drawable->star_field->count; i++)
	{
		StarData *star = calloc(1, sizeof(StarData));
		Vec3 hsvcolor, temp_vec;
		F32 multiplier = 1, randomval1, randomval2;

		randomMersenneSphereShellSlice(pRandTable, TWOPI, slice_angle, 1, star->center);

		randomval1 = randomMersennePositiveF32(pRandTable); // outside any if statements so it is invariant
		randomval2 = randomMersennePositiveF32(pRandTable); // outside any if statements so it is invariant

		if (drawable->star_field->slice_fade > 0)
		{
			F32 newy = star->center[1] * randomval1;

			multiplier = powf(inv_sin_half_angle * (sin_half_angle - ABS(star->center[1])), drawable->star_field->slice_fade);

			// shift y values toward 0 and renormalize position
			star->center[1] = lerp(newy, star->center[1], multiplier);
			normalVec3(star->center);

			multiplier = powf(inv_sin_half_angle * (sin_half_angle - ABS(star->center[1])), drawable->star_field->slice_fade);
		}

		star->size = multiplier * randomMersennePositiveF32(pRandTable) * (drawable->star_field->size_max - drawable->star_field->size_min);
		star->size += drawable->star_field->size_min;

		// do axis change BEFORE orientation so unrotated stars stay aligned
		mulVecMat3(star->center, axis_orient_mat, temp_vec);
		copyVec3(temp_vec, star->center);

		if (drawable->star_field->half_dome)
			star->center[1] = ABS(star->center[1]);

		if (drawable->star_field->random_rotation)
		{
			Mat3 rotation_mat, orient_mat, result_mat;
			Vec3 pyr, neg_star_pos;
			negateVec3(star->center, neg_star_pos);
			orientMat3(orient_mat, neg_star_pos);
			setVec3(pyr, 0, 0, TWOPI * randomval2);
			createMat3YPR(rotation_mat, pyr);
			mulMat3(orient_mat, rotation_mat, result_mat);
			copyVec3(result_mat[0], star->xvec);
			copyVec3(result_mat[1], star->yvec);
		}
		else
		{
			setVec3(star->xvec, 1, 0, 0);
			setVec3(star->yvec, 0, -1, 0);
		}

		hsvLerp(drawable->star_field->color_min, drawable->star_field->color_max, randomMersennePositiveF32(pRandTable), hsvcolor);
		gfxHsvToRgb(hsvcolor, star->color);

		star->random1 = randomMersennePositiveF32(pRandTable);
		star->random2 = randomMersennePositiveF32(pRandTable);

		eaPush(&drawable->star_data, star);
	}

	mersenneTableFree(pRandTable);
}

GeoHandle gfxDemandLoadStarField(SkyDrawable *drawable, F32 fov_y, int *tri_count, Vec4 starfield_param, bool *camera_facing)
{
	GeoHandle geo_handle;

	PERFINFO_AUTO_START_FUNC();

	if (!drawable->star_data)
		createStarData(drawable);

	if (!drawable->geo_render_info)
		updateStarField(drawable);

	ANALYSIS_ASSUME(drawable->geo_render_info);

	geo_handle = gfxGeoDemandLoad(drawable->geo_render_info);

	if (tri_count)
		*tri_count = drawable->geo_render_info->tri_count;

	setVec4same(starfield_param, 0);
	if (drawable->star_field->scale_with_fov)
		starfield_param[0] = RAD(fov_y) * 0.01f;
	else
		starfield_param[0] = RAD(55) * 0.01f;

	*camera_facing = !drawable->star_field->random_rotation;

	PERFINFO_AUTO_STOP_FUNC();

	return geo_handle;
}
