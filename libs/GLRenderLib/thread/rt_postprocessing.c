
#include "ogl.h"
#include "rt_primitive.h"
#include "rt_tex.h"
#include "rt_drawmode.h"
#include "rt_state.h"
#include "rt_geo.h"

void rwglPostProcessScreenDirect(RdrDeviceWinGL *device, RdrScreenPostProcess *ppscreen)
{
	Vec3 vertices[4];
	int w, h;

	rwglSetupPostProcessDrawMode(device, 0);

	// Bind shader and materials
	rwglBindMaterial(device, &ppscreen->material, MATERIAL_SHADER_DEFAULT);
	CHECKGL;
	glDisable(GL_CULL_FACE);
	CHECKGL;

	switch (ppscreen->blend_type)
	{
		xcase BLEND_REPLACE:
			rwglBlendFunc(GL_ONE, GL_ZERO, BLENDOP_ADD);

		xcase BLEND_ALPHA:
			rwglBlendFunc(GL_SRC_ALPHA, GL_ONE_MINUS_SRC_ALPHA, BLENDOP_ADD);

		xcase BLEND_ADD:
			rwglBlendFunc(GL_ONE, GL_ONE, BLENDOP_ADD);
	}

	if (ppscreen->test_depth)
		glDepthFunc(GL_LESS);
	else
		glDisable(GL_DEPTH_TEST);
	CHECKGL;
	if (!ppscreen->write_depth)
		rwglDepthMask(0);

	w = device->active_surface->width;
	h = device->active_surface->height;
	rwglSet2DMode(w, h);

	rwglSetPPTextureSize(ppscreen->tex_width, ppscreen->tex_height, ppscreen->tex_vwidth, ppscreen->tex_vheight);

	setVec3(vertices[0], w, 0, 0);
	setVec3(vertices[1], 0, 0, 0);
	setVec3(vertices[2], 0, h, 0);
	setVec3(vertices[3], w, h, 0);

	glBegin(GL_QUADS);
		rwglVertex(vertices[0]);
		rwglVertex(vertices[1]);
		rwglVertex(vertices[2]);
		rwglVertex(vertices[3]);
	glEnd();

	glEnable(GL_CULL_FACE);
	CHECKGL;
	if (ppscreen->test_depth)
		glDepthFunc(GL_LEQUAL);
	else
		glEnable(GL_DEPTH_TEST);
	CHECKGL;
	if (!ppscreen->write_depth)
		rwglDepthMask(1);
}

void rwglPostProcessShapeDirect(RdrDeviceWinGL *device, RdrShapePostProcess *ppshape)
{
	RdrGeometryDataWinGL *geo_data;
	RdrVertexDeclaration *vdecl;

	rwglSetupPostProcessDrawMode(device, 1);

	// Bind shader and materials
	rwglBindMaterial(device, &ppshape->material, MATERIAL_SHADER_DEFAULT);
	CHECKGL;

	if (ppshape->draw_back_faces)
		glCullFace(GL_FRONT);
	CHECKGL;

	switch (ppshape->blend_type)
	{
		xcase BLEND_REPLACE:
			rwglBlendFunc(GL_ONE, GL_ZERO, BLENDOP_ADD);

		xcase BLEND_ALPHA:
			rwglBlendFunc(GL_SRC_ALPHA, GL_ONE_MINUS_SRC_ALPHA, BLENDOP_ADD);

		xcase BLEND_ADD:
			rwglBlendFunc(GL_ONE, GL_ONE, BLENDOP_ADD);
	}

	if (!ppshape->test_depth)
		glDisable(GL_DEPTH_TEST);
	CHECKGL;
	if (!ppshape->write_depth)
		rwglDepthMask(0);

	rwglSet3DMode();

	rwglSetPPTextureSize(ppshape->tex_width, ppshape->tex_height, ppshape->tex_vwidth, ppshape->tex_vheight);

	rwglPushModelMatrix(ppshape->world_matrix);
	geo_data = rwglBindGeometryDirect(device, ppshape->geometry);
	vdecl = geo_data->vertex_declaration;
	rwglVertexPointer(3, vdecl->stride, geo_data->base_data.vert_data + vdecl->position_offset);
	glDrawElements(GL_TRIANGLES,geo_data->base_data.tri_count*3,GL_UNSIGNED_INT,geo_data->base_data.tris);
	rwglPopModelMatrix();


	if (ppshape->draw_back_faces)
		glCullFace(GL_BACK);
	CHECKGL;
	if (!ppshape->test_depth)
		glEnable(GL_DEPTH_TEST);
	CHECKGL;
	if (!ppshape->write_depth)
		rwglDepthMask(1);
}
