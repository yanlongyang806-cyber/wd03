/***************************************************************************



***************************************************************************/

#include "MemRef.h"
#include "../StaticWorld/WorldCellStreaming.h"
#include "ScratchStack.h"
#include "Color.h"
#include "partition_enums.h"

#include "GenericMesh.h"

#include "GfxStaticLights.h"
#include "GfxLightCache.h"
#include "GfxLightsPrivate.h"
#include "GfxWorld.h"
#include "GfxGeo.h"
#include "GfxModelInline.h"
#include "GfxTexturesPublic.h"
#include "GfxTextures.h"
#include "GfxDXT.h"
#include "GfxSky.h"
#include "bounds.h"

AUTO_RUN_ANON(memBudgetAddMapping(__FILE__, BUDGET_World););

//////////////////////////////////////////////////////////////////////////
// C versions of some HLSL functions

__forceinline static void toTangentSpace(const Vec3 tangent, const Vec3 binormal, const Vec3 normal, const Vec3 invec, Vec3 outvec)
{
	outvec[0] = dotVec3(tangent, invec);
	outvec[1] = dotVec3(binormal, invec);
	outvec[2] = dotVec3(normal, invec);
}

//////////////////////////////////////////////////////////////////////////

static void getInfoLightCacheVertexLight(GeoRenderInfo *geo_render_info, void *light_cache_in, int lod, GeoRenderInfoDetails *details)
{
	GfxStaticObjLightCache *light_cache = light_cache_in;
	VertexLightData *vertex_light = light_cache->lod_vertex_light_data[lod];
	
	assert(vertex_light);

	if (!vertex_light->name)
	{
		char buf[MAX_PATH];
		const Model *model = worldDrawableEntryGetModel(light_cache->entry, NULL, NULL, NULL, NULL);
		sprintf(buf, "VertexColors:%s", SAFE_MEMBER(model, name));
		vertex_light->name = allocAddString(buf);
	}

	details->name = vertex_light->name;
	details->uv_density = -1;
}

static bool CALLBACK fillLightCacheVertexLight(GeoRenderInfo *geo_render_info, void *light_cache_in, int lod)
{
	GfxStaticObjLightCache *light_cache = light_cache_in;
	VertexLightData *vertex_light = light_cache->lod_vertex_light_data[lod];
	U32 vert_total;
	RdrVertexDeclaration *vdecl;
	U8 *extra_data_ptr;
	U8 *vert_data;
	int i;

	assert(vertex_light);
	assert(geo_render_info);

	assert(geo_render_info->usage.iNumPrimaryStreams == 1);
	vdecl = rdrGetVertexDeclaration(geo_render_info->usage.bits[0]);

	vert_total = vdecl->stride * geo_render_info->vert_count;

	geo_render_info->tri_count = 0;
	geo_render_info->subobject_count = 0;

	extra_data_ptr = geo_render_info->vbo_data_primary.data_ptr = memrefAlloc(vert_total);
	geo_render_info->vbo_data_primary.data_size = vert_total;

	geo_render_info->vbo_data_primary.tris = NULL;
	geo_render_info->vbo_data_primary.subobject_tri_bases = NULL;
	geo_render_info->vbo_data_primary.subobject_tri_counts = NULL;

	geo_render_info->vbo_data_primary.vert_data = vert_data = extra_data_ptr;
	extra_data_ptr += vert_total;

	if (geo_render_info->usage.bits[0] & RUSE_COLORS)
	{
		for (i = 0; i < geo_render_info->vert_count; i++)
		{
			*((U32 *)(vert_data + vdecl->stride * i + vdecl->color_offset)) = vertex_light->vertex_colors[i];
		}
	}

	return true;
}

static void CALLBACK fillLightCacheVertexLightFreeMemory(GeoRenderInfo *geo_render_info, void *light_cache_in, int lod)
{
	GfxStaticObjLightCache *light_cache = light_cache_in;
	VertexLightData *vertex_light = light_cache->lod_vertex_light_data[lod];

	assert(vertex_light);
	ea32Destroy(&vertex_light->vertex_colors);
}

__forceinline static F32 calcDistanceFalloff(F32 lightdir_length, F32 light_inner_radius, F32 light_inv_dradius)
{
	return 1 - saturate((lightdir_length - light_inner_radius) * light_inv_dradius);
}

__forceinline static F32 calcAngularFalloff(F32 cur_val, F32 min_val, F32 max_val)
{
	return interp(min_val, max_val, cur_val);
}

static Vec3 *getDiffuseWarpData(BasicTexture *tex, int *len)
{
	static BasicTexture *cached_tex=NULL;
	static Vec3 *cached_data;
	static int cached_len;
	if (cached_tex == tex)
	{
		// Nothing to do
	} else {
		int i;
		BasicTextureRareData *rare_data;
		TexReadInfo *rawInfo;
		int y;
		texLoadRawDataInternal(tex, TEX_LOAD_NOW_CALLED_FROM_MAIN_THREAD, WL_FOR_WORLD);

		rare_data = texGetRareData(tex);
		rawInfo = SAFE_MEMBER(rare_data, bt_rawInfo);
		assert(rawInfo);
		verify(uncompressRawTexInfo(rawInfo,textureMipsReversed(tex)));

		cached_tex = tex;
		cached_len = rawInfo->width;
		cached_data = realloc(cached_data, cached_len*sizeof(Vec3));
		y = rawInfo->height/2;
		assert(rawInfo->tex_format == RTEX_BGRA_U8);

		for (i=0; i<cached_len; i++)
		{
			int j;
			for (j=0; j<3; j++)
			{
				cached_data[i][j] = (1.f/255.f)*rawInfo->texture_data[(i + y*rawInfo->width)*4 + j];
			}
		}

		texUnloadRawData(tex);
	}

	*len = cached_len;
	return cached_data;
}

static void sampleDiffuseWarp(BasicTexture *diffuse_warp_tex, Vec3 out, F32 val)
{
	Vec3 *data;
	int len;
	F32 fval;
	int ival;
	int i;
	data = getDiffuseWarpData(diffuse_warp_tex, &len);
	val = saturate(val) * (len-1);
	ival = (int)val;
	fval = val - ival;
	if (ival == len-1)
	{
		copyVec3(data[len-1], out);
	} else {
		for (i=0; i<3; i++)
		{
			out[i] = lerp(data[ival][i], data[ival+1][i], fval);
		}
	}
}

static void applyStandardLightModel(BasicTexture *diffuse_warp_tex, Vec3 color, const Vec3 norm, F32 ambient_occlusion, const Vec3 light_dir, const Vec3 light_ambient_term, const Vec3 light_diffuse_term, F32 light_falloff_term)
{
	if (light_falloff_term > 0)
	{
		Vec3 temp_vec;
		F32 diffuse_val = dotVec3(norm, light_dir);
		light_falloff_term *= light_falloff_term;

		if (diffuse_warp_tex)
		{
			Vec3 diffuse_warp;
			sampleDiffuseWarp(diffuse_warp_tex, diffuse_warp, diffuse_val * 0.5 + 0.5);
			scaleVec3(light_ambient_term, ambient_occlusion, temp_vec);
			temp_vec[0] = lerp(temp_vec[0], light_diffuse_term[0], diffuse_warp[0]);
			temp_vec[1] = lerp(temp_vec[1], light_diffuse_term[1], diffuse_warp[1]);
			temp_vec[2] = lerp(temp_vec[2], light_diffuse_term[2], diffuse_warp[2]);
		} else {
			F32 secondary_diffuse_val = saturate(1 - diffuse_val);
			diffuse_val = saturate(diffuse_val);
			scaleVec3(light_ambient_term, ambient_occlusion*secondary_diffuse_val, temp_vec);
			scaleAddVec3(light_diffuse_term, diffuse_val, temp_vec, temp_vec);
		}

		scaleAddVec3(temp_vec, light_falloff_term, color, color);
	}
}

static U32 occluder_volume_type;
static WorldVolumeQueryCache *occluder_cache_query;

__forceinline static const WorldVolume **getOcclusionVolumes(const GfxLight *gfx_light, const Vec3 obj_bounds_min, const Vec3 obj_bounds_max)
{
	const RdrLight *light = &gfx_light->rdr_light;
	Vec3 light_bounds_min, light_bounds_max;

	if (!occluder_volume_type)
		occluder_volume_type = wlVolumeTypeNameToBitMask("Occluder");

	if (!occluder_cache_query)
		occluder_cache_query = wlVolumeQueryCacheCreate(PARTITION_CLIENT, wlVolumeQueryCacheTypeNameToBitMask("RdrLight"), NULL);

	if (rdrGetSimpleLightType(light->light_type) == RDRLIGHT_DIRECTIONAL)
	{
		// extrude object bounds in light direction
		scaleAddVec3(light->world_mat[1], -1000, obj_bounds_min, light_bounds_min);
		scaleAddVec3(light->world_mat[1], -1000, obj_bounds_max, light_bounds_max);
		MINVEC3(light_bounds_min, obj_bounds_min, light_bounds_min);
		MINVEC3(light_bounds_max, obj_bounds_max, light_bounds_max);
	}
	else
	{
		// use light bounds
		copyVec3(gfx_light->static_entry.bounds.min, light_bounds_min);
		copyVec3(gfx_light->static_entry.bounds.max, light_bounds_max);
	}

	return wlVolumeCacheQueryBoxByType(occluder_cache_query, unitmat, light_bounds_min, light_bounds_max, occluder_volume_type);
}


static U32 skyfade_volume_type;
static WorldVolumeQueryCache *skyfade_cache_query;

__forceinline static const WorldVolume **getSkyFadeVolumes(const Vec3 obj_bounds_min, const Vec3 obj_bounds_max)
{
	if (!skyfade_volume_type)
		skyfade_volume_type = wlVolumeTypeNameToBitMask("SkyFade");

	if (!skyfade_cache_query)
		skyfade_cache_query = wlVolumeQueryCacheCreate(PARTITION_CLIENT, wlVolumeQueryCacheTypeNameToBitMask("SkyFade"), NULL);

	return wlVolumeCacheQueryBoxByType(skyfade_cache_query, unitmat, obj_bounds_min, obj_bounds_max, skyfade_volume_type);
}

static void applyDirLight(BasicTexture *diffuse_warp_tex, const GfxLight *gfx_light, const Vec3 *verts, const Vec3 *norms, const U8 *vertex_colors, Vec3 *colors, int vert_count, const Vec3 obj_bounds_min, const Vec3 obj_bounds_max)
{
	const RdrLight *light = &gfx_light->rdr_light;
	const WorldVolume **volumes = getOcclusionVolumes(gfx_light, obj_bounds_min, obj_bounds_max);
	bool do_occlusion = eaSize(&volumes) > 0;
	Vec3 dir;
	int i;

	scaleVec3(light->world_mat[1], -1, dir);

	for (i = 0; i < vert_count; ++i)
	{
		//F32 secondary_diffuse_val = saturate(-dotVec3(norms[i], dir));
		Vec3 ambient_term, ray_start;

		scaleAddVec3(light->light_colors[RLCT_WORLD].secondary_diffuse, 1, light->light_colors[RLCT_WORLD].ambient, ambient_term);

		scaleAddVec3(light->world_mat[1], -1000, verts[i], ray_start);
		if (do_occlusion && wlVolumeRayCollideSpecifyVolumes(ray_start, verts[i], volumes, NULL))
			continue;

		applyStandardLightModel(diffuse_warp_tex, colors[i], norms[i], vertex_colors?colorComponentToF32(vertex_colors[i*4+3]):1.f, dir, ambient_term, light->light_colors[RLCT_WORLD].diffuse, 1);
	}
}

static void applyPointLight(BasicTexture *diffuse_warp_tex, const GfxLight *gfx_light, const Vec3 *verts, const Vec3 *norms, const U8 *vertex_colors, Vec3 *colors, int vert_count, const Vec3 obj_bounds_min, const Vec3 obj_bounds_max)
{
	const RdrLight *light = &gfx_light->rdr_light;
	const WorldVolume **volumes = getOcclusionVolumes(gfx_light, obj_bounds_min, obj_bounds_max);
	bool do_occlusion = eaSize(&volumes) > 0;
	int i;
	F32 rad_sqrd = SQR(light->point_spot_params.outer_radius);
	F32 light_inv_dradius = 1.f / (light->point_spot_params.outer_radius - light->point_spot_params.inner_radius);

	for (i = 0; i < vert_count; ++i)
	{
		Vec3 lightdir;
		F32 lightdir_length, light_falloff_term;

		if (distance3Squared(verts[i], light->world_mat[3]) > rad_sqrd)
			continue;

		subVec3(light->world_mat[3], verts[i], lightdir);
		lightdir_length = normalVec3(lightdir);

		// distance falloff
		light_falloff_term = calcDistanceFalloff(lightdir_length, light->point_spot_params.inner_radius, light_inv_dradius);

		if (light_falloff_term <= 0)
			continue;

		if (do_occlusion && wlVolumeRayCollideSpecifyVolumes(light->world_mat[3], verts[i], volumes, NULL))
			continue;

		applyStandardLightModel(diffuse_warp_tex, colors[i], norms[i], vertex_colors?colorComponentToF32(vertex_colors[i*4+3]):1.f, lightdir, light->light_colors[RLCT_WORLD].ambient, light->light_colors[RLCT_WORLD].diffuse, light_falloff_term);
	}
}

static void applySpotLight(BasicTexture *diffuse_warp_tex, const GfxLight *gfx_light, const Vec3 *verts, const Vec3 *norms, const U8 *vertex_colors, Vec3 *colors, int vert_count, const Vec3 obj_bounds_min, const Vec3 obj_bounds_max)
{
	const RdrLight *light = &gfx_light->rdr_light;
	const WorldVolume **volumes = getOcclusionVolumes(gfx_light, obj_bounds_min, obj_bounds_max);
	bool do_occlusion = eaSize(&volumes) > 0;
	int i;
	F32 rad_sqrd = SQR(light->point_spot_params.outer_radius);
	F32 light_inv_dradius = 1.f / (light->point_spot_params.outer_radius - light->point_spot_params.inner_radius);
	F32 light_cos_inner_cone_angle = cosf(light->point_spot_params.inner_cone_angle);
	F32 light_cos_outer_cone_angle = cosf(light->point_spot_params.outer_cone_angle);
	Vec3 dir;

	scaleVec3(light->world_mat[1], -1, dir);

	for (i = 0; i < vert_count; ++i)
	{
		Vec3 lightdir;
		F32 lightdir_length, light_falloff_term;

		if (distance3Squared(verts[i], light->world_mat[3]) > rad_sqrd)
			continue;

		subVec3(light->world_mat[3], verts[i], lightdir);
		lightdir_length = normalVec3(lightdir);

		// distance falloff
		light_falloff_term = calcDistanceFalloff(lightdir_length, light->point_spot_params.inner_radius, light_inv_dradius);

		// angular falloff
		light_falloff_term *= calcAngularFalloff(-dotVec3(lightdir, dir), light_cos_outer_cone_angle, light_cos_inner_cone_angle);

		if (light_falloff_term <= 0)
			continue;

		if (do_occlusion && wlVolumeRayCollideSpecifyVolumes(light->world_mat[3], verts[i], volumes, NULL))
			continue;

		applyStandardLightModel(diffuse_warp_tex, colors[i], norms[i], vertex_colors?colorComponentToF32(vertex_colors[i*4+3]):1.f, lightdir, light->light_colors[RLCT_WORLD].ambient, light->light_colors[RLCT_WORLD].diffuse, light_falloff_term);
	}
}

static void applyProjectorLight(BasicTexture *diffuse_warp_tex, const GfxLight *gfx_light, const Vec3 *verts, const Vec3 *norms, const U8 *vertex_colors, Vec3 *colors, int vert_count, const Vec3 obj_bounds_min, const Vec3 obj_bounds_max)
{
	const RdrLight *light = &gfx_light->rdr_light;
	const WorldVolume **volumes = getOcclusionVolumes(gfx_light, obj_bounds_min, obj_bounds_max);
	bool do_occlusion = eaSize(&volumes) > 0;
	F32 light_inv_dradius = 1.f / (light->point_spot_params.outer_radius - light->point_spot_params.inner_radius);
	Mat4 inv_world_mat;
	int i;

	invertMat4Copy(light->world_mat, inv_world_mat);

	for (i = 0; i < vert_count; ++i)
	{
		Vec3 lightspace_pos, lightdir_ws;
		F32 light_falloff_term;

		subVec3(light->world_mat[3], verts[i], lightdir_ws);
		mulVecMat3(lightdir_ws, inv_world_mat, lightspace_pos);
		normalVec3(lightdir_ws);

		light_falloff_term = calcDistanceFalloff(lightspace_pos[1], light->point_spot_params.inner_radius, light_inv_dradius);
		if (lightspace_pos[1] < 0)
		{
			light_falloff_term = 0;
		}
		else if (light_falloff_term)
		{
			Vec2 projected_pos;
			F32 scale = 1.f / lightspace_pos[1];
			setVec2(projected_pos, lightspace_pos[0] * scale, lightspace_pos[2] * scale);

			light_falloff_term *= calcAngularFalloff(ABS(projected_pos[0]), light->projector_params.angular_falloff[1], light->projector_params.angular_falloff[0]);
			light_falloff_term *= calcAngularFalloff(ABS(projected_pos[1]), light->projector_params.angular_falloff[3], light->projector_params.angular_falloff[2]);
		}

		if (light_falloff_term <= 0)
			continue;

		if (do_occlusion && wlVolumeRayCollideSpecifyVolumes(light->world_mat[3], verts[i], volumes, NULL))
			continue;

		applyStandardLightModel(diffuse_warp_tex, colors[i], norms[i], vertex_colors?colorComponentToF32(vertex_colors[i*4+3]):1.f, lightdir_ws, 
			zerovec3, light->light_colors[RLCT_WORLD].diffuse, light_falloff_term);
	}
}

void gfxCalcVertexLightingForTransformedGMesh(GMesh *mesh) {

	Vec3 min = {FLT_MAX, FLT_MAX, FLT_MAX};
	Vec3 max = {-FLT_MAX, -FLT_MAX, -FLT_MAX};
	Vec3 mid;
	int i;
	int j;
	float radius;
	WorldRegionGraphicsData *regionGraphicsData = worldRegionGetGraphicsData(NULL);
	GfxLight **tempLights = NULL;
	Vec3 *tmpColors = calloc(mesh->vert_count, sizeof(Vec3));

	// Find midpoint radius, radius, and bounds.

	for(i = 0; i < mesh->vert_count; i++) {
		for(j = 0; j < 3; j++) {
			min[j] = MIN(mesh->positions[i][j], min[j]);
			max[j] = MAX(mesh->positions[i][j], max[j]);
		}
	}
	lerpVec3(min, 0.5f, max, mid);
	radius = MAX(MAX((max[0] - min[0]), (max[1] - min[1])), (max[2] - min[2]));

	octreeFindInBoxEA(
		regionGraphicsData->static_light_octree,
		&tempLights, min, max, unitmat, unitmat);

	gmeshSetUsageBits(mesh, mesh->usagebits | USE_COLORS);

	for(i = 0; i < eaSize(&tempLights); i++) {

		GfxLight *light = tempLights[i];

		switch (rdrGetSimpleLightType(light->rdr_light.light_type)) {

			case RDRLIGHT_DIRECTIONAL:
				applyDirLight(
					NULL, light, mesh->positions, mesh->normals, NULL,
					tmpColors, mesh->vert_count, min,
					max);
				break;

			case RDRLIGHT_POINT:
				applyPointLight(
					NULL, light, mesh->positions, mesh->normals, NULL,
					tmpColors, mesh->vert_count, min,
					max);
				break;

			case RDRLIGHT_SPOT:
				applySpotLight(
					NULL, light, mesh->positions, mesh->normals, NULL,
					tmpColors, mesh->vert_count, min,
					max);
				break;

			case RDRLIGHT_PROJECTOR:
				applyProjectorLight(
					NULL, light, mesh->positions, mesh->normals, NULL,
					tmpColors, mesh->vert_count, min,
					max);
				break;
		}
	}

	for(i = 0; i < mesh->vert_count; i++) {
		mesh->colors[i].r = (int)(CLAMPF(tmpColors[i][0] / WORLD_CELL_GRAPHICS_VERTEX_LIGHT_MULTIPLIER, 0.0f, 1.0f) * 255);
		mesh->colors[i].g = (int)(CLAMPF(tmpColors[i][1] / WORLD_CELL_GRAPHICS_VERTEX_LIGHT_MULTIPLIER, 0.0f, 1.0f) * 255);
		mesh->colors[i].b = (int)(CLAMPF(tmpColors[i][2] / WORLD_CELL_GRAPHICS_VERTEX_LIGHT_MULTIPLIER, 0.0f, 1.0f) * 255);
		mesh->colors[i].a = 255;
	}

	free(tmpColors);

}

void gfxCalcSimpleLightValueForPoint(const Vec3 vPos, Vec3 vColor) {

	int i = 0;
	RdrLightParams light_params = {0};
	int vertex_color_count = 6;

	Vec3 verts[6];
	Vec3 norms[6];
	Vec3 colors[6];

	Vec3 obj_bounds_min = { vPos[0] - 1, vPos[1] - 1, vPos[2] - 1 };
	Vec3 obj_bounds_max = { vPos[0] + 1, vPos[1] + 1, vPos[2] + 1 };

	// Create 6 vertices at the position we were given. Each with a
	// normal facing in a different direction.
	for(i = 0; i < 6; i++) {

		setVec3same(colors[i], 0);
		copyVec3(vPos, verts[i]);

		setVec3same(norms[i], 0);

		if(i < 3) {
			// Positive axis directions.
			norms[i][i] = 1;
		} else {
			// Negative axis directions.
			norms[i][i - 3] = -1;
		}
	}

	i = 0;

	setVec3same(vColor, 0);

	gfxGetObjectLightsUncached(&light_params, NULL, vPos, 1, true);

	while(i < MAX_NUM_OBJECT_LIGHTS && light_params.lights[i]) {

		GfxLight *light = GfxLightFromRdrLight(light_params.lights[i]);

		switch (rdrGetSimpleLightType(light->rdr_light.light_type)) {
			xcase RDRLIGHT_DIRECTIONAL:
				applyDirLight(NULL, light, verts, norms, NULL, colors, vertex_color_count, obj_bounds_min, obj_bounds_max);

			xcase RDRLIGHT_POINT:
				applyPointLight(NULL, light, verts, norms, NULL, colors, vertex_color_count, obj_bounds_min, obj_bounds_max);

			xcase RDRLIGHT_SPOT:
				applySpotLight(NULL, light, verts, norms, NULL, colors, vertex_color_count, obj_bounds_min, obj_bounds_max);

			xcase RDRLIGHT_PROJECTOR:
				applyProjectorLight(NULL, light, verts, norms, NULL, colors, vertex_color_count, obj_bounds_min, obj_bounds_max);
		}

		i++;
	}

	// Add up the light values for all the sides.
	for(i = 0; i < 6; i++) {
		addVec3(vColor, colors[i], vColor);
	}

	// Add in all the ambient junk too. (All these count for one side
	// each of our "cube".)
	addVec3(light_params.ambient_light->sky_light_color_front[RLCT_WORLD], vColor, vColor);
	addVec3(light_params.ambient_light->sky_light_color_back[RLCT_WORLD], vColor, vColor);
	addVec3(light_params.ambient_light->sky_light_color_side[RLCT_WORLD], vColor, vColor);

	// Average the light values for the sides. Also double it here, as
	// an approximation to favor the unlit side.
	scaleVec3(vColor, 1.0/3.0, vColor);

	// Add the ambient value to that.
	addVec3(light_params.ambient_light->ambient[RLCT_WORLD], vColor, vColor);
}

bool gStaticLightForBin = false;

__forceinline void gfxCreateVertexLightGeoRenderInfo(GeoRenderInfo **renderInfo, GfxStaticObjLightCache *light_cache, const unsigned int lod)
{
	RdrGeoUsage usage = {0};
	usage.bits[0] = usage.key = RUSE_COLORS;
	usage.iNumPrimaryStreams = 1;
	usage.bHasSecondary = false;
	*renderInfo = gfxCreateGeoRenderInfo(	fillLightCacheVertexLight, fillLightCacheVertexLightFreeMemory, 
		getInfoLightCacheVertexLight, 
		false, light_cache, lod, usage, 
		WL_FOR_WORLD, true);
}

static void gfxCreateVertexLightDataInternal(GfxStaticObjLightCache *light_cache, int lod_offset, BinFileList *file_list)
{
	VertexLightData *vertex_light;
	Mat4 world_matrix;
	Vec3 *verts, *norms, *colors;
	Vec3 obj_bounds_min, obj_bounds_max;
	U8 *vertex_colors = NULL;
	F32 max_brightness = -8e16, min_brightness = 8e16;
	int i;

	int lod_idx;
	Mat4 spline_matrices[2];
	bool has_spline;
	F32 *model_scale;
	const Model *model;
	ModelLOD *model_lod;
	int vertex_color_count;
	int actual_lod;

	BasicTexture *diffuse_warp_tex=NULL;

	if (!light_cache)
		return;

	PERFINFO_AUTO_START_FUNC();

	model = worldDrawableEntryGetModel(light_cache->entry, &lod_idx, spline_matrices, &has_spline, &model_scale);
	
	actual_lod = lod_idx + lod_offset;
	model_lod = model ? modelLODLoadAndMaybeWait(model, actual_lod, true) : NULL;

	if (actual_lod >= eaSize(&light_cache->lod_vertex_light_data))
		eaSetSize(&light_cache->lod_vertex_light_data, actual_lod+1);

	ANALYSIS_ASSUME(light_cache->lod_vertex_light_data);
	vertex_light = light_cache->lod_vertex_light_data[actual_lod];

	if (!model_lod || !eaSize(&light_cache->secondary_lights))
	{
		if (vertex_light)
			gfxFreeVertexLight(vertex_light);

		light_cache->lod_vertex_light_data[actual_lod] = NULL;
		PERFINFO_AUTO_STOP();
		return;
	}

	if (!modelLODIsLoaded(model_lod))
	{
		if (vertex_light)
			gfxFreeVertexLight(vertex_light);

		light_cache->lod_vertex_light_data[actual_lod] = NULL;
		light_cache->need_vertex_light_update = 1;

		PERFINFO_AUTO_STOP();
		return;
	}

	if (!model_lod->data)
	{
		if (vertex_light)
			gfxFreeVertexLight(vertex_light);

		light_cache->lod_vertex_light_data[actual_lod] = NULL;

		PERFINFO_AUTO_STOP();
		return;
	}

	if (!vertex_light)
	{
		vertex_light = calloc(1, sizeof(VertexLightData));
		light_cache->lod_vertex_light_data[actual_lod] = vertex_light;
	}

	if (vertex_light->geo_render_info && !vertex_light->geo_render_info->send_queued && !gStaticLightForBin)
	{
		gfxFreeGeoRenderInfo(vertex_light->geo_render_info, true);
	}
	else if (vertex_light->geo_render_info)
	{
		gfxFreeGeoRenderInfo(vertex_light->geo_render_info, false);
		vertex_light->geo_render_info = NULL;
	}

	if (!vertex_light->geo_render_info && !gStaticLightForBin)
	{
		gfxCreateVertexLightGeoRenderInfo(&vertex_light->geo_render_info, light_cache, actual_lod);
	}

	vertex_color_count = model_lod->vert_count;
	
	if (vertex_light->geo_render_info)
	{
		vertex_light->geo_render_info->vert_count = vertex_color_count;
		vertex_light->geo_render_info->is_auxiliary = true;
	}

	// get necessary data
	verts = ScratchAlloc(vertex_color_count * sizeof(Vec3));
	norms = ScratchAlloc(vertex_color_count * sizeof(Vec3));
	colors = ScratchAlloc(vertex_color_count * sizeof(Vec3));
	memcpy(verts, modelGetVerts(model_lod), vertex_color_count * sizeof(Vec3));
	memcpy(norms, modelGetNorms(model_lod), vertex_color_count * sizeof(Vec3));
	if (modelHasColors(model_lod) && model_lod->mem_usage_bitindex != WL_FOR_TERRAIN_BITINDEX) // Terrain vertex colors are *not* ambient occlusion values
	{
		vertex_colors = ScratchAlloc(vertex_color_count * 4 * sizeof(U8));
		memcpy(vertex_colors, modelGetColors(model_lod), vertex_color_count * 4 * sizeof(U8));
	}
	ZeroStructsForce(colors, vertex_color_count);

	copyMat4(light_cache->entry->base_entry.bounds.world_matrix, world_matrix);
	if (model_scale)
		scaleMat3Vec3(world_matrix, model_scale);

	// transform verts
	for (i = 0; i < vertex_color_count; ++i)
	{
		Vec3 vert, norm;

		if (has_spline)
		{
			Vec3 in, up, tangent;
			setVec3(in, verts[i][0], verts[i][1], 0);
			splineEvaluateEx(spline_matrices, -verts[i][2]/spline_matrices[1][2][0], in, vert, up, tangent, norms[i], norm);
			if (vert[0] < -1e6 || vert[0] > 1e6 ||
				vert[1] < -1e6 || vert[1] > 1e6 ||
				vert[2] < -1e6 || vert[2] > 1e6)
				setVec3(vert, 0, 0, 0);
			copyVec3(vert, verts[i]);
			if (norm[0] < -1e6 || norm[0] > 1e6 ||
				norm[1] < -1e6 || norm[1] > 1e6 ||
				norm[2] < -1e6 || norm[2] > 1e6)
				setVec3(norm, 0, 1, 0);
			copyVec3(norm, norms[i]);
		}

		mulVecMat4(verts[i], world_matrix, vert);
		copyVec3(vert, verts[i]);

		mulVecMat3(norms[i], world_matrix, norm);
		normalVec3(norm);
		copyVec3(norm, norms[i]);
	}

	mulBoundsAA(light_cache->entry->base_entry.shared_bounds->local_min, light_cache->entry->base_entry.shared_bounds->local_max, 
				light_cache->entry->base_entry.bounds.world_matrix, obj_bounds_min, obj_bounds_max);

	if (gfx_lighting_options.enableDiffuseWarpTex)
	{
		SkyInfo *last_sky = NULL;
		if (light_cache->region)
		{
			SkyInfoGroup *pSkyInfoGroup = worldRegionGetSkyGroup(light_cache->region);
			if (pSkyInfoGroup)
			{
				FOR_EACH_IN_EARRAY_FORWARDS(pSkyInfoGroup->override_list, SkyInfoOverride, sky_override)
				{
					SkyInfo *sky = GET_REF(sky_override->sky);
					if (sky && sky->diffuseWarpTextureWorld)
					{
						diffuse_warp_tex = texFind(sky->diffuseWarpTextureWorld, 1);
						if (diffuse_warp_tex)
						{
							last_sky = sky;
							break;
						}
					}
				}
				FOR_EACH_END;
			}

		}

		{
			const WorldVolume **volumes = getSkyFadeVolumes(obj_bounds_min, obj_bounds_max);
			FOR_EACH_IN_EARRAY_FORWARDS(volumes, const WorldVolume, volume)
			{
				WorldVolumeEntry *volume_entry= wlVolumeGetVolumeData(volume);
				SkyInfoGroup *sky_group = &volume_entry->client_volume.sky_volume_properties->sky_group;
				bool b=false;
				FOR_EACH_IN_EARRAY_FORWARDS(sky_group->override_list, SkyInfoOverride, sky_override)
				{
					SkyInfo *sky = GET_REF(sky_override->sky);
					if (sky && sky->diffuseWarpTextureWorld)
					{
						diffuse_warp_tex = texFind(sky->diffuseWarpTextureWorld, 1);
						if (diffuse_warp_tex)
						{
							last_sky = sky;
							b = true;
							break;
						}
					}
				}
				FOR_EACH_END;
				if (b)
					break;
			}
			FOR_EACH_END;
		}

		if (diffuse_warp_tex && file_list)
		{
			// Add to dependencies
			bflAddSourceFile(file_list, texFindFullPath(diffuse_warp_tex));
			worldDepsReportTexture(texFindName(diffuse_warp_tex));
			if (last_sky)
				bflAddSourceFile(file_list, last_sky->filename);
		}
	}

	// calculate light colors
	for (i = 0; i < eaSize(&light_cache->secondary_lights); ++i)
	{
		GfxLight *light = light_cache->secondary_lights[i];
		switch (rdrGetSimpleLightType(light->rdr_light.light_type))
		{
			xcase RDRLIGHT_DIRECTIONAL:
				applyDirLight(diffuse_warp_tex, light, verts, norms, vertex_colors, colors, vertex_color_count, obj_bounds_min, obj_bounds_max);

			xcase RDRLIGHT_POINT:
				applyPointLight(diffuse_warp_tex, light, verts, norms, vertex_colors, colors, vertex_color_count, obj_bounds_min, obj_bounds_max);

			xcase RDRLIGHT_SPOT:
				applySpotLight(diffuse_warp_tex, light, verts, norms, vertex_colors, colors, vertex_color_count, obj_bounds_min, obj_bounds_max);
			
			xcase RDRLIGHT_PROJECTOR:
				applyProjectorLight(diffuse_warp_tex, light, verts, norms, vertex_colors, colors, vertex_color_count, obj_bounds_min, obj_bounds_max);

			xdefault:
				assertmsg(0, "Unknown light type!");
		}
	}

	for (i = 0; i < vertex_color_count; ++i)
	{
		// since it is just for a multiplier, take the brightest channel
		// instead of calculating actual brightness
		MAX1(max_brightness, colors[i][0]);
		MAX1(max_brightness, colors[i][1]);
		MAX1(max_brightness, colors[i][2]);
		MIN1(min_brightness, colors[i][0]);
		MIN1(min_brightness, colors[i][1]);
		MIN1(min_brightness, colors[i][2]);
	}

	ea32SetSize(&vertex_light->vertex_colors, vertex_color_count);
	if (max_brightness - min_brightness >= 0)
	{
		F32 multiplier;
		int val0, val1, val2;

		if (min_brightness == max_brightness)
			multiplier = 255.f;
		else
			multiplier = 255.f / (max_brightness - min_brightness);

		for (i = 0; i < vertex_color_count; ++i)
		{
			val0 = round((colors[i][0] - min_brightness) * multiplier);
			val0 = CLAMP(val0, 0, 255);

			val1 = round((colors[i][1] - min_brightness) * multiplier);
			val1 = CLAMP(val1, 0, 255);

			val2 = round((colors[i][2] - min_brightness) * multiplier);
			val2 = CLAMP(val2, 0, 255);

			vertex_light->vertex_colors[i] = val0 | (val1<<8) | (val2<<16);
		}
	}
	else
	{
		min_brightness = max_brightness = 0;
	}

	vertex_light->rdr_vertex_light.vlight_multiplier = max_brightness - min_brightness;
	vertex_light->rdr_vertex_light.vlight_offset = min_brightness;
	vertex_light->rdr_vertex_light.geo_handle_vertex_light = 0; //clear this out incase we are reusing an old one

	if (max_brightness == min_brightness && max_brightness == 0)
	{
		gfxFreeVertexLight(vertex_light);
		light_cache->lod_vertex_light_data[actual_lod] = NULL;
	}


	// free temp data
	if (vertex_colors)
		ScratchFree(vertex_colors);
	ScratchFree(colors);
	ScratchFree(norms);
	ScratchFree(verts);

	PERFINFO_AUTO_STOP();
}

void gfxCreateVertexLightData(GfxStaticObjLightCache *light_cache, BinFileList *file_list)
{
	light_cache->need_vertex_light_update = 0;

	//only make LODs 0 and 1
	gfxCreateVertexLightDataInternal(light_cache, 0, file_list);
	gfxCreateVertexLightDataInternal(light_cache, 1, file_list);

	// I don't know why we would want to destroy this part of the cached info.  I have commented it out so that the early-out checks in gfxUpdateStaticLightCache
	// evaluate correctly [RMARR - 11/29/11]
	//eaDestroy(&light_cache->secondary_lights);
}

static void gfxCreateVertexLightDataStreamingInternal(GfxStaticObjLightCache *light_cache, int lod, WorldDrawableEntryVertexLightColors *vertex_light_color_src)
{
	VertexLightData* vertex_light;

	if (!light_cache)
		return;


	PERFINFO_AUTO_START_FUNC();

	if (eaSize(&light_cache->lod_vertex_light_data) <= lod)
		eaSetSize(&light_cache->lod_vertex_light_data, lod+1);

	ANALYSIS_ASSUME(light_cache->lod_vertex_light_data);
	vertex_light = light_cache->lod_vertex_light_data[lod];

	if (!vertex_light)
	{
		vertex_light = calloc(1, sizeof(VertexLightData));
		light_cache->lod_vertex_light_data[lod] = vertex_light;
	}
	else
	{
		free(vertex_light->vertex_colors);
	}

	ea32PushArray(&vertex_light->vertex_colors, &vertex_light_color_src->vertex_light_colors);
	vertex_light->rdr_vertex_light.vlight_multiplier = vertex_light_color_src->multipler;
	vertex_light->rdr_vertex_light.vlight_offset = vertex_light_color_src->offset;

	// create GeoRenderInfo
	gfxCreateVertexLightGeoRenderInfo(&vertex_light->geo_render_info, light_cache, lod);
	vertex_light->geo_render_info->vert_count = ea32Size(&vertex_light->vertex_colors);
	vertex_light->geo_render_info->is_auxiliary = true;

	PERFINFO_AUTO_STOP();
}

void gfxCreateVertexLightDataStreaming(GfxStaticObjLightCache *light_cache, WorldDrawableEntry *draw_entry)
{
	int i;
	for (i = 0; i < eaSize(&draw_entry->lod_vertex_light_colors); i++)
	{
		if (!draw_entry->lod_vertex_light_colors[i] || !draw_entry->lod_vertex_light_colors[i]->vertex_light_colors)
			continue;

		gfxCreateVertexLightDataStreamingInternal(light_cache, i, draw_entry->lod_vertex_light_colors[i]);
	}
}

void gfxFreeVertexLight(VertexLightData *vertex_light)
{
	gfxFreeGeoRenderInfo(vertex_light->geo_render_info, false);
	ea32Destroy(&vertex_light->vertex_colors); // must be freed after the GeoRenderInfo
	free(vertex_light);
}
