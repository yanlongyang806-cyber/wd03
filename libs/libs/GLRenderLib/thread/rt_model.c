#include "timing.h"

#include "rt_model.h"
#include "rt_state.h"
#include "rt_drawmode.h"
#include "rt_geo.h"
#include "DebugState.h"

#ifndef RT_STAT_DRAW_TRIS
#define RT_STAT_DRAW_TRIS(x)
#endif


__forceinline static void setRenderSettings(RdrDrawableGeo *draw, RdrMaterial *material)
{
	RdrMaterialFlags flags = material->flags | draw->add_material_flags;

	if (!(draw->wireframe || flags))
		return;

	if (flags & RMATERIAL_ADDITIVE)
	{
		rwglBlendFuncPush(GL_SRC_ALPHA, GL_ONE, BLENDOP_ADD);
		rwglFogPush(0);
	}
	else if (flags & RMATERIAL_SUBTRACTIVE)
	{
		if (rdr_caps.supports_subtract)
		{
			rwglBlendFuncPush(GL_SRC_ALPHA, GL_ONE, BLENDOP_SUBTRACT);
		}
		else
		{
			// this does not subtract, but fakes it by masking
			rwglBlendFuncPush(GL_ZERO, GL_ONE_MINUS_SRC_COLOR, BLENDOP_ADD);
		}
		rwglFogPush(0);
	}
	else if (flags & RMATERIAL_NOFOG)
	{
		rwglFogPush(0);
	}

	if (flags & RMATERIAL_DOUBLESIDED)
		glDisable(GL_CULL_FACE);

	if (flags & RMATERIAL_NOZTEST)
		glDisable(GL_DEPTH_TEST);

	if (flags & RMATERIAL_NOZWRITE)
		rwglDepthMask(0);

	if (draw->wireframe & 1)
	{
		glEnable(GL_POLYGON_OFFSET_FILL);
		glPolygonOffset(1,1);
	}
}

__forceinline static void unsetRenderSettings(RdrDrawableGeo *draw, RdrMaterial *material)
{
	RdrMaterialFlags flags = material->flags | draw->add_material_flags;
	if (!(draw->wireframe || flags))
		return;

	if (flags & RMATERIAL_ADDITIVE
		|| flags & RMATERIAL_SUBTRACTIVE)
	{
		rwglBlendFuncPop();
		rwglFogPop();
	}
	else if (flags & RMATERIAL_NOFOG)
	{
		rwglFogPop();
	}

	if (flags & RMATERIAL_DOUBLESIDED)
		glEnable(GL_CULL_FACE);

	if (flags & RMATERIAL_NOZTEST)
		glEnable(GL_DEPTH_TEST);

	if (flags & RMATERIAL_NOZWRITE)
		rwglDepthMask(1);

	if (draw->wireframe & 1)
		glDisable(GL_POLYGON_OFFSET_FILL);
}

__forceinline static void drawTriangles(int tri_count, int tri_base, U16 *tris)
{
	PERFINFO_AUTO_START("drawTriangles", 1);
	glDrawElements(GL_TRIANGLES,tri_count*3,GL_UNSIGNED_SHORT,&tris[tri_base*3]);
	CHECKGL;
	RT_STAT_DRAW_TRIS(tri_count);
	PERFINFO_AUTO_STOP();
}

__forceinline static void drawModel(RdrDeviceWinGL *device, RdrDrawableGeo *draw, RdrSortNode *sort_node, U32 num_bones, SkinningMat4 *bone_infos)
{
	int draw_bits = num_bones ? DRAWBIT_SKINNED : 0;
	RdrGeometryDataWinGL *geo_data;
	RdrVertexDeclaration *vdecl;

	rwglSetupNormalDrawMode(device, draw_bits);
	rwglBindMaterial(device, sort_node->material, sort_node->draw_shader_handle[RDRSHDM_VISUAL]);

	geo_data = rwglBindGeometryDirect(device, sort_node->geo_handle_primary);

	rwglSetupAmbientLight(draw->ambient);

	vdecl = geo_data->vertex_declaration;
	rwglVertexPointer(3, vdecl->stride, geo_data->base_data.vert_data + vdecl->position_offset);
	assertmsg(0, "Normals/binormals/etc converted to 10:10:10s, doom!");
	rwglNormalPointer(vdecl->stride, geo_data->base_data.vert_data + vdecl->normal_offset);
	assertmsg(0, "Texcoords converted to F16s, doom!");
	rwglTexCoordPointer(0, 2, vdecl->stride, geo_data->base_data.vert_data + vdecl->texcoord_offset);

	if (1)
	{
		rwglTangentPointer(vdecl->stride, geo_data->base_data.vert_data + vdecl->tangent_offset);
		rwglBinormalPointer(vdecl->stride, geo_data->base_data.vert_data + vdecl->binormal_offset);
	}

	if (num_bones)
	{
		rwglBoneIdxPointer(4, vdecl->stride, geo_data->base_data.vert_data + vdecl->boneid_offset);
		rwglBoneWeightPointer(4, vdecl->stride, geo_data->base_data.vert_data + vdecl->boneweight_offset);
		rwglSetupSkinning(device, num_bones, bone_infos);
	}

	rwglModelColorf(0, draw->colors[sort_node->subobject_idx]);
	//rwglModelColorf(1, draw->color);
	CHECKGL;

	drawTriangles(geo_data->base_data.subobject_tri_counts[sort_node->subobject_idx], geo_data->base_data.subobject_tri_bases[sort_node->subobject_idx], geo_data->base_data.tris);
}

__forceinline static void drawModelWireframe(RdrDeviceWinGL *device, RdrDrawableGeo *draw, RdrSortNode *sort_node, U32 num_bones, SkinningMat4 *bone_infos)
{
	int draw_bits = num_bones ? DRAWBIT_SKINNED : 0;
	RdrGeometryDataWinGL *geo_data;
	RdrVertexDeclaration *vdecl;

	rwglSetupNormalDrawMode(device, draw_bits);
	rwglSetDefaultBlendMode(device);

	geo_data = rwglBindGeometryDirect(device, sort_node->geo_handle_primary);
	rwglBindWhiteTexture(0);

	rwglFogPush(0);
	glLineWidth(1);
	CHECKGL;
	glDisable(GL_CULL_FACE);
	CHECKGL;
	glPolygonMode(GL_FRONT_AND_BACK, GL_LINE);
	CHECKGL;

	vdecl = geo_data->vertex_declaration;
	rwglVertexPointer(3, vdecl->stride, geo_data->base_data.vert_data + vdecl->position_offset);
	assertmsg(0, "Normals/binormals/etc converted to 10:10:10s, doom!");
	rwglNormalPointer(vdecl->stride, geo_data->base_data.vert_data + vdecl->normal_offset);
	assertmsg(0, "Texcoords converted to F16s, doom!");
	rwglTexCoordPointer(0, 2, vdecl->stride, geo_data->base_data.vert_data + vdecl->texcoord_offset);
	if (num_bones)
	{
		rwglBoneIdxPointer(4, vdecl->stride, geo_data->base_data.vert_data + vdecl->boneid_offset);
		rwglBoneWeightPointer(4, vdecl->stride, geo_data->base_data.vert_data + vdecl->boneweight_offset);
		rwglSetupSkinning(device, num_bones, bone_infos);
	}

	rwglColor(draw->wireframe_color);
	CHECKGL;

	drawTriangles(geo_data->base_data.subobject_tri_counts[sort_node->subobject_idx], geo_data->base_data.subobject_tri_bases[sort_node->subobject_idx], geo_data->base_data.tris);

	glPolygonMode(GL_FRONT_AND_BACK, GL_FILL);
	CHECKGL;
	glEnable(GL_CULL_FACE);
	CHECKGL;
	rwglFogPop();

}

void rwglDrawModelDirect(RdrDeviceWinGL *device, RdrDrawableModel *model_draw, RdrSortNode *sort_node)
{
	RdrDrawableGeo *draw = &model_draw->base_geo_drawable;

	CHECKGLTHREAD;
	CHECKDEVICELOCK(device);

	rwglSet3DMode();

	if (draw->camera_facing)
	{
		RdrStateWinGL *state = rwglGetCurrentState();
		copyMat3(state->inv_viewmat, draw->world_matrix);
	}

	rwglPushModelMatrix(draw->world_matrix);
	setRenderSettings(draw, sort_node->material);

	if (draw->wireframe != 2)
		drawModel(device, draw, sort_node, 0, 0);

	if (draw->wireframe)
		drawModelWireframe(device, draw, sort_node, 0, 0);

	unsetRenderSettings(draw, sort_node->material);
	rwglPopModelMatrix();
}

void rwglDrawSkinnedModelDirect(RdrDeviceWinGL *device, RdrDrawableSkinnedModel *skin_draw, RdrSortNode *sort_node)
{
	RdrDrawableGeo *draw = &skin_draw->base_geo_drawable;

	CHECKGLTHREAD;
	CHECKDEVICELOCK(device);

	rwglSet3DMode();

	if (draw->camera_facing)
	{
		RdrStateWinGL *state = rwglGetCurrentState();
		copyMat3(state->inv_viewmat, draw->world_matrix);
	}

	rwglPushModelMatrix(draw->world_matrix);
	setRenderSettings(draw, sort_node->material);

	if (draw->wireframe != 2)
		drawModel(device, draw, sort_node, skin_draw->num_bones, skin_draw->bone_infos);

	if (draw->wireframe)
		drawModelWireframe(device, draw, sort_node, skin_draw->num_bones, skin_draw->bone_infos);

	unsetRenderSettings(draw, sort_node->material);
	rwglPopModelMatrix();
}


__forceinline static void drawTerrain(RdrDeviceWinGL *device, RdrDrawableGeo *draw, RdrSortNode *sort_node)
{
	int draw_bits = 0;
	RdrGeometryDataWinGL *geo_data;
	RdrVertexDeclaration *vdecl;

	rwglBindMaterial(device, sort_node->material, sort_node->draw_shader_handle[RDRSHDM_VISUAL]);
	rwglSetupTerrainDrawMode(device, 0);

	geo_data = rwglBindGeometryDirect(device, sort_node->geo_handle_primary);

	rwglSetupAmbientLight(draw->ambient);

	vdecl = geo_data->vertex_declaration;
	assertmsg(0, "Texcoords converted to F16s, doom!");
	rwglTexCoordPointer(0, 2, vdecl->stride, geo_data->base_data.vert_data + vdecl->texcoord_offset);
	rwglColorf(draw->colors[sort_node->subobject_idx]);

	assert(0); // No longer works, I bet X_x
	rwglModelColorf(0, draw->colors[sort_node->subobject_idx]);
	//rwglModelColorf(1, draw->color[1]); // Sending width/height to vertex shader
	CHECKGL;

	drawTriangles(geo_data->base_data.subobject_tri_counts[sort_node->subobject_idx], geo_data->base_data.subobject_tri_bases[sort_node->subobject_idx], geo_data->base_data.tris);
}

void rwglDrawTerrainDirect(RdrDeviceWinGL *device, RdrDrawableGeo *draw, RdrSortNode *sort_node)
{
	CHECKGLTHREAD;
	CHECKDEVICELOCK(device);

	rwglSet3DMode();

	rwglPushModelMatrix(draw->world_matrix);
	setRenderSettings(draw, sort_node->material);

	if (draw->wireframe != 2)
		drawTerrain(device, draw, sort_node);

//	if (draw->wireframe)
//		drawTerrainWireframe(device, draw, 0, 0);

	unsetRenderSettings(draw, sort_node->material);
	rwglPopModelMatrix();
}
