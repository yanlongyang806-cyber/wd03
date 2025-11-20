
#include "ogl.h"
#include "rt_sprite.h"
#include "rt_state.h"
#include "rt_drawmode.h"
#include "DebugState.h"

#ifndef RT_STAT_ADDSPRITE
#define RT_STAT_ADDSPRITE(x)
#define RT_STAT_DRAW_TRIS(x)
#endif


__forceinline static void bindVerticesInline(RdrSpriteVertex *verts)
{
	rwglVertexPointer(2, sizeof(RdrSpriteVertex), &verts->point);
	rwglColorPointer(4, sizeof(RdrSpriteVertex), &verts->color);
	rwglTexCoordPointer(0, 2, sizeof(RdrSpriteVertex), &verts->texcoord);
}

__forceinline static U32 drawSpriteLinesInline(RdrSpriteState *state, RdrSpriteVertex *verts, U32 sprite_count, U32 offset)
{
	U32 vertex_count = sprite_count * 2;

	RT_STAT_ADDSPRITE(sprite_count * 2);

	if (state->use_scissor)
	{
		glEnable(GL_SCISSOR_TEST);
		CHECKGL;

		glScissor(state->scissor_x, state->scissor_y, state->scissor_width, state->scissor_height);
		CHECKGL;
	}
	else
	{
		glDisable(GL_SCISSOR_TEST);
		CHECKGL;
	}

	glLineWidth(state->prim_line_width);
	CHECKGL;

	if (state->is_prim_antialiased) {
		glEnable(GL_LINE_SMOOTH); // Note: this causes a memory leak on ATI!
		CHECKGL;
	}

	if (state->additive)
	{
		rwglBlendFunc(GL_SRC_ALPHA, GL_ONE, BLENDOP_ADD);
		CHECKGL;
	}
	else
	{
		rwglBlendFunc(GL_SRC_ALPHA, GL_ONE_MINUS_SRC_ALPHA, BLENDOP_ADD);
		CHECKGL;
	}

	rwglBindWhiteTexture(0);

	glDrawArrays(GL_LINES, offset, vertex_count);
	CHECKGL;

	if (state->is_prim_antialiased) {
		glDisable(GL_LINE_SMOOTH);
		CHECKGL;
	}
	glLineWidth(1);

	return vertex_count;
}

__forceinline static U32 drawSpritesInline(RdrSpriteState *state, U32 sprite_count, U32 offset)
{
	U32 vertex_count = sprite_count * 4;

	RT_STAT_ADDSPRITE(sprite_count * 2);

	rwglBindTexture(GL_TEXTURE_2D, 0, state->tex_handle);

	if (state->use_scissor)
	{
		glEnable(GL_SCISSOR_TEST);
		CHECKGL;

		glScissor(state->scissor_x, state->scissor_y, state->scissor_width, state->scissor_height);
		CHECKGL;
	}
	else
	{
		glDisable(GL_SCISSOR_TEST);
		CHECKGL;
	}

	if (state->additive)
	{
		rwglBlendFunc(GL_SRC_ALPHA, GL_ONE, BLENDOP_ADD);
		CHECKGL;
	}
	else
	{
		rwglBlendFunc(GL_SRC_ALPHA, GL_ONE_MINUS_SRC_ALPHA, BLENDOP_ADD);
		CHECKGL;
	}

	glDrawArrays(GL_QUADS, offset, vertex_count);
	CHECKGL;

	RT_STAT_DRAW_TRIS(sprite_count * 2);

	return vertex_count;
}

void rwglDrawSpritesDirect(RdrDeviceWinGL *device, RdrSpritesPkg *pkg)
{
   	U32 i, j, offset;
	RdrSpriteState	*states = (RdrSpriteState *) (pkg+1);
	RdrSpriteVertex *verts = (RdrSpriteVertex *) (((char *)states) + (pkg->sprite_count * sizeof(RdrSpriteState)));

	CHECKGLTHREAD;
	CHECKDEVICELOCK(device);

	rwglSet2DMode(pkg->screen_width, pkg->screen_height);

	rwglSetupSpriteDrawMode(device);
	rwglSetDefaultBlendMode(device);

	glDisable(GL_DEPTH_TEST);
	CHECKGL;

	rwglDepthMask(0);
	rwglBlendFuncPush(GL_SRC_ALPHA, GL_ONE_MINUS_SRC_ALPHA, BLENDOP_ADD);

	bindVerticesInline(verts);

	offset = 0;
	for (j = 0; j < pkg->sprite_count; )
	{
		int count = 1;
		for (i = j+1; i < pkg->sprite_count; i++)
		{
			if (!spriteStatesEqual(states + j, states + i))
				break;
			count++;
		}
		if (states[j].is_prim_line) {
			offset += drawSpriteLinesInline(states + j, verts + offset, count, offset);
		} else {
			offset += drawSpritesInline(states + j, count, offset);
		}
		j += count;
	}

	glDisable(GL_SCISSOR_TEST);
	CHECKGL;

	// reset these
	rwglBlendFuncPop();
	rwglDepthMask(1);

	glEnable(GL_DEPTH_TEST);
	CHECKGL;
}

