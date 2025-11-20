
#include "ogl.h"
#include "rt_primitive.h"
#include "rt_tex.h"
#include "rt_drawmode.h"

void rwglDrawPrimitiveDirect(RdrDeviceWinGL *device, RdrDrawablePrimitive *primitive)
{
	CHECKDEVICELOCK(device);
	CHECKGLTHREAD;

	rwglSetupPrimitiveDrawMode(device);
	rwglSetDefaultBlendMode(device);
	rwglBindWhiteTexture(0);
	if (!primitive->filled)
		glLineWidth(primitive->linewidth);
	CHECKGL;
	glDisable(GL_CULL_FACE);
	CHECKGL;

	if (primitive->in_3d || !primitive->screen_width_2d || !primitive->screen_height_2d)
		rwglSet3DMode();
	else {
		if (primitive->screen_width_2d==-1) { // Need to get width/height here (in thread) since size changes are asynchronous
			rwglSet2DMode(device->active_surface->width, device->active_surface->height);
		} else {
			rwglSet2DMode(primitive->screen_width_2d, primitive->screen_height_2d);
		}
	}

	if (primitive->antialiased)
		glEnable(GL_LINE_SMOOTH);

	if (primitive->no_ztest)
	{
		glDisable(GL_DEPTH_TEST);
		CHECKGL;
		rwglDepthMask(0);
	}

	switch (primitive->type)
	{
		xcase RP_LINE:
			glBegin(GL_LINES);
			rwglColorf(primitive->vertices[0].color);
			rwglVertex(primitive->vertices[0].pos);
			rwglColorf(primitive->vertices[1].color);
			rwglVertex(primitive->vertices[1].pos);
			glEnd();

		xcase RP_TRI:
			if (primitive->filled)
				glBegin(GL_TRIANGLES);
			else
				glBegin(GL_LINE_LOOP);
			rwglColorf(primitive->vertices[0].color);
			rwglVertex(primitive->vertices[0].pos);
			rwglColorf(primitive->vertices[1].color);
			rwglVertex(primitive->vertices[1].pos);
			rwglColorf(primitive->vertices[2].color);
			rwglVertex(primitive->vertices[2].pos);
			glEnd();

		xcase RP_QUAD:
			if (primitive->filled)
				glBegin(GL_QUADS);
			else
				glBegin(GL_LINE_LOOP);
			rwglColorf(primitive->vertices[0].color);
			rwglVertex(primitive->vertices[0].pos);
			rwglColorf(primitive->vertices[1].color);
			rwglVertex(primitive->vertices[1].pos);
			rwglColorf(primitive->vertices[2].color);
			rwglVertex(primitive->vertices[2].pos);
			rwglColorf(primitive->vertices[3].color);
			rwglVertex(primitive->vertices[3].pos);
			glEnd();

		xdefault:
			break;
	}

	if (primitive->antialiased)
		glDisable(GL_LINE_SMOOTH);

	if (primitive->no_ztest)
	{
		glEnable(GL_DEPTH_TEST);
		CHECKGL;
		rwglDepthMask(1);
	}

	CHECKGL;
	glEnable(GL_CULL_FACE);
	CHECKGL;
	if (!primitive->filled)
		glLineWidth(1);
	CHECKGL;
}

void rwglDrawQuadDirect(RdrDeviceWinGL *device, RdrQuadDrawable *quad)
{
	int i;
	rwglSetupPrimitiveDrawMode(device);
	// Bind shader and materials
	rwglBindMaterial(device, &quad->material, MATERIAL_SHADER_DEFAULT);
	CHECKGL;
	glDisable(GL_CULL_FACE);
	CHECKGL;

	rwglBlendFunc(GL_ONE, GL_ZERO, BLENDOP_ADD);

	glDisable(GL_DEPTH_TEST);
	CHECKGL;
	rwglDepthMask(0);

	rwglSet2DMode(device->active_surface->width, device->active_surface->height);

	glBegin(GL_QUADS);
	for (i=0; i<4; i++) {
		Vec3 point;
		rwglColorf(quad->vertices[i].color);
		rwglTexCoord(0, quad->vertices[i].texcoord);
		setVec3(point, quad->vertices[i].point[0], quad->vertices[i].point[1], 0.0f);
		rwglVertex(point);
	}
	glEnd();

	glEnable(GL_CULL_FACE);
	CHECKGL;
	glEnable(GL_DEPTH_TEST);
	CHECKGL;
	rwglDepthMask(1);
}
