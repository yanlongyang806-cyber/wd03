#include "GfxModelCache.h"
#include "GfxModel.h"
#include "GfxModelInline.h"
#include "GfxGeo.h"
#include "GenericMesh.h"

AUTO_RUN_ANON(memBudgetAddMapping(__FILE__, BUDGET_Geometry_Art););

#define EXTRA_DEBUGGING 1

void gfxModelEnterUnpackCS(void)
{
	EnterCriticalSection(&model_unpack_cs);
	in_model_unpack_cs++;
}

void gfxModelLeaveUnpackCS(void)
{
	in_model_unpack_cs--;
	LeaveCriticalSection(&model_unpack_cs);
}


void gfxModelSetupZOTris(ModelLOD *model)
{
	if (!model->unpack.tris)
	{
		int i = 0;

		assert(modelLODIsLoaded(model) && model->data);

		modelGetTris(model);
#ifdef EXTRA_DEBUGGING
		assert(model->unpack.tris);
		for (i = 0; i < model->data->tri_count*3; ++i)
		{
			if (model->unpack.tris[i] >= (U32)model->vert_count)
			{
				char buffer[1024];
				sprintf(buffer,"Bad triangle data for model \"%s\" in file \"%s\" found after uncompressing!",model->model_parent->name,model->model_parent->header->filename);
				assertmsg(0,buffer);
			}
		}
#endif
	}

	modelGetVerts(model);
}

void gfxModelUnpackVertexData(ModelLOD *model)
{
	modelGetVerts(model);
	modelGetNorms(model);
}

// used exclusively for splats at this time.  [RMARR - 10/18/12]
void gfxModelLODUpdateFromRawGeometry(ModelLOD *model, RdrGeoUsageBits primary_usage, bool load_in_foreground)
{
	// store the unpacked raw geometry data
	GeoRenderInfo * geo_render_info = model->geo_render_info;

	if (geo_render_info)
	{
		if (geo_render_info->send_queued)
		{
			gfxFreeGeoRenderInfo(geo_render_info, false);
			model->geo_render_info = NULL;
			gfxFillModelRenderInfo(model);
			geo_render_info = model->geo_render_info;

			ANALYSIS_ASSUME(model->geo_render_info);
		}
		else
		{
			gfxFreeGeoRenderInfo(geo_render_info, true);
		}

		geo_render_info->load_in_foreground = load_in_foreground;
		geo_render_info->use_flags |= WL_FOR_FX;
		geo_render_info->usage.iNumPrimaryStreams = 1;
		geo_render_info->usage.bHasSecondary = false;
		geo_render_info->usage.bits[0] = geo_render_info->usage.key = primary_usage | ((gfx_state.allRenderersFeatures & FEATURE_DECL_F16_2)?0:RUSE_TEXCOORDS_HI_FLAG);
	}
	modelLODInitFromData(model);
}

// Takes ownership of verts*, texcoords*, and normals*
void gfxModelLODFromRawGeometry(ModelLOD *model, Vec3 *verts, int tri_count, 
						  Vec2 *texcoords, Vec3 *normals, RdrGeoUsageBits primary_usage, bool load_in_foreground)
{
	int i, j;
	int *modeltri;
	Vec3 vertMin, vertMax;

	PERFINFO_AUTO_START_FUNC();

	if (model->geo_render_info && !model->geo_render_info->send_queued)
	{
		gfxFreeGeoRenderInfo(model->geo_render_info, true);
	}
	else
	{
		if (model->geo_render_info)
			gfxFreeGeoRenderInfo(model->geo_render_info, false);
		model->geo_render_info = NULL;
		gfxFillModelRenderInfo(model);
	}

	ANALYSIS_ASSUME(model->geo_render_info);
	assert(model->data);

	model->data->tri_count = tri_count;
	model->data->vert_count = tri_count * 3;

	if (tri_count)
	{
		copyVec3(verts[0], vertMin);
		copyVec3(verts[0], vertMax);
		for (i = 1, j = model->data->vert_count; i < j; ++i)
		{
			vec3RunningMin(verts[i], vertMin);
			vec3RunningMax(verts[i], vertMax);
		}
	}
	else
	{
		setVec3same(vertMin, 0);
		setVec3same(vertMax, 0);
	}
	copyVec3(vertMin, model->model_parent->min);
	copyVec3(vertMax, model->model_parent->max);
	centerVec3(vertMin, vertMax, model->model_parent->mid);
	model->model_parent->radius = distance3(model->model_parent->mid, vertMax);

	model->geo_render_info->load_in_foreground = load_in_foreground;

	// Free any old data and zero unpacked structures
	modelLODFreeUnpacked(model);

	// take ownership of the specified vertex & tex coord data
	model->unpack.verts = verts;
	model->unpack.sts = texcoords;
	model->unpack.norms = normals;
	model->model_parent->use_flags |= WL_FOR_FX;
	model->geo_render_info->use_flags |= WL_FOR_FX;
	model->geo_render_info->usage.bits[0] = model->geo_render_info->usage.key = primary_usage | ((gfx_state.allRenderersFeatures & FEATURE_DECL_F16_2)?0:RUSE_TEXCOORDS_HI_FLAG);
	model->geo_render_info->usage.iNumPrimaryStreams = 1;
	model->geo_render_info->usage.bHasSecondary = false;

	modeltri = model->unpack.tris = malloc(tri_count * sizeof(int) * 3);
	for (i = 0, j = 0; i < tri_count; ++i, j += 3)
	{
		modeltri[ j + 0 ] = j + 0;
		modeltri[ j + 1 ] = j + 1;
		modeltri[ j + 2 ] = j + 2;
	}
	assert(model->data->tex_idx && model->data->tex_count == 1);
	model->data->tex_idx[0].id = 0;
	model->data->tex_idx[0].count = tri_count;
	model->uv_density = GMESH_LOG_MIN_UV_DENSITY;	//TODO: should calculate actual uv density here.

	modelLODInitFromData(model);

	model->loadstate = GEO_LOADED;

	PERFINFO_AUTO_STOP();
}
