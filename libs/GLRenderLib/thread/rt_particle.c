/*
#include "ogl.h"
#include "rt_particle.h"
#include "rt_state.h"
#include "rt_drawmode.h"

#ifndef RT_STAT_ADDPARTICLE
#define RT_STAT_ADDPARTICLE(x)
#define RT_STAT_DRAW_TRIS(x)
#endif


__forceinline static void bindVerticesInline(RdrParticleVertex *verts)
{
	rwglVertexPointer(3, sizeof(RdrParticleVertex), &verts->point);
	rwglColorPointer(4, sizeof(RdrParticleVertex), &verts->color);
	rwglTexCoordPointer(0, 2, sizeof(RdrParticleVertex), &verts->texcoord);
}

__forceinline static U32 drawParticlesInline(RdrDrawableParticle *state, U32 particle_count, U32 offset)
{
	U32 vertex_count = particle_count * 4;

	RT_STAT_ADDPARTICLE(particle_count * 2);

	rwglBindTexture(GL_TEXTURE_2D, 0, state->material.textures[0]);

	if (state->material.flags & RMATERIAL_ADDITIVE)
	{
		rwglBlendFunc(GL_SRC_ALPHA, GL_ONE, BLENDOP_ADD);
		rwglFog(0);
	}
	else if (state->material.flags & RMATERIAL_SUBTRACTIVE)
	{
		if (rdr_caps.supports_subtract)
		{
			rwglBlendFunc(GL_SRC_ALPHA, GL_ONE, BLENDOP_SUBTRACT);
		}
		else
		{
			// this does not subtract, but fakes it by masking
			rwglBlendFunc(GL_ZERO, GL_ONE_MINUS_SRC_COLOR, BLENDOP_ADD);
		}
		rwglFog(0);
	}
	else
	{
		rwglBlendFunc(GL_SRC_ALPHA, GL_ONE_MINUS_SRC_ALPHA, BLENDOP_ADD);
		rwglFog(1);
	}

	glDrawArrays(GL_QUADS, offset, vertex_count);
	CHECKGL;

	RT_STAT_DRAW_TRIS(particle_count * 2);

	return vertex_count;
}

void rwglDrawParticlesDirect(RdrDeviceWinGL *device, RdrDrawableParticle *states)
{
	CHECKGLTHREAD;
	CHECKDEVICELOCK(device);

	rwglSet3DMode();

	rwglSetupParticleDrawMode(device);
	rwglSetDefaultBlendMode(device);

	rwglFogPush(1);
	rwglDepthMask(0);
	rwglBlendFuncPush(GL_SRC_ALPHA, GL_ONE_MINUS_SRC_ALPHA, BLENDOP_ADD);

	while (states->count)
	{
		int header_size = rdrParticleStateSizePacked(states);
		RdrParticleVertex *verts = (RdrParticleVertex *) ((U8 *)states + header_size);

		rwglBindProgram(GLC_FRAGMENT_PROG, states->shader_handle);
		bindVerticesInline(verts);

		drawParticlesInline(states, states->count, 0);
		states = (RdrParticleState*)((U8*)verts + sizeof(RdrParticleVertex)*4*states->count);
	}


	// reset these
	rwglBlendFuncPop();
	rwglDepthMask(1);
	rwglFogPop();
}

*/
