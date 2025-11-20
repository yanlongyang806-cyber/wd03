#include "ogl.h"
#include "error.h"

#include "mathutil.h"
#include "utils.h"
#include "MemoryMonitor.h"
#include "Color.h"

#include "RenderLib.h"
#include "rt_state.h"
#include "rt_surface.h"


#ifndef RT_STAT_INC
#define RT_STAT_INC(x)
#define RT_STAT_TEXBIND_CHANGE(x)
#define RT_STAT_BUFFER_CHANGE
#define RT_STAT_VP_CHANGE
#define RT_STAT_FP_CHANGE
#endif

#define ENABLE_VBO_STATEMANAGEMENT 1
#define ENABLE_STATEMANAGEMENT 1
#define SAFE_COLORS 0

static RdrStateWinGL *current_state = 0;

enum
{
	ARRAY_VERTEX		= 0,
	ARRAY_COLOR			= 1,
	ARRAY_NORMAL		= 2,
	ARRAY_TANGENT		= 3,
	ARRAY_BINORMAL		= 4,
	ARRAY_BONEIDX		= 5,
	ARRAY_BONEWEIGHT	= 6,
};

enum // Enum to keep track of what vertex program local param is used for what
{
	VP_PP_TEX_SIZE,
	VP_BASEPOSE_OFFSET,
};

enum // Enum to keep track of what fragment program env param is used for what
{
	FPENV_LIGHT_AMBIENT,
	FPENV_LIGHT_POSDIR,
	FPENV_LIGHT_DIFFUSE,
	FPENV_LIGHT_SPECULAR,
	FPENV_COLOR0,
	FPENV_COLOR1,
};

__forceinline static void doEnableClientState(GLClientStateBits array)
{
	CHECKGLTHREAD;
	switch (array)
	{
		case GLC_TEXTURE_COORD_ARRAY_0:
			glClientActiveTextureARB(GL_TEXTURE0_ARB);
			glEnableClientState(GL_TEXTURE_COORD_ARRAY);
			break;
		case GLC_TEXTURE_COORD_ARRAY_1:
			glClientActiveTextureARB(GL_TEXTURE1_ARB);
			glEnableClientState(GL_TEXTURE_COORD_ARRAY);
			break;
		case GLC_TEXTURE_COORD_ARRAY_2:
			glClientActiveTextureARB(GL_TEXTURE2_ARB);
			glEnableClientState(GL_TEXTURE_COORD_ARRAY);
			break;
		case GLC_TEXTURE_COORD_ARRAY_3:
			glClientActiveTextureARB(GL_TEXTURE3_ARB);
			glEnableClientState(GL_TEXTURE_COORD_ARRAY);
			break;
		case GLC_TEXTURE_COORD_ARRAY_4:
			glClientActiveTextureARB(GL_TEXTURE4_ARB);
			glEnableClientState(GL_TEXTURE_COORD_ARRAY);
			break;
		case GLC_TEXTURE_COORD_ARRAY_5:
			glClientActiveTextureARB(GL_TEXTURE5_ARB);
			glEnableClientState(GL_TEXTURE_COORD_ARRAY);
			break;
		case GLC_TEXTURE_COORD_ARRAY_6:
			glClientActiveTextureARB(GL_TEXTURE6_ARB);
			glEnableClientState(GL_TEXTURE_COORD_ARRAY);
			break;
		case GLC_TEXTURE_COORD_ARRAY_7:
			glClientActiveTextureARB(GL_TEXTURE7_ARB);
			glEnableClientState(GL_TEXTURE_COORD_ARRAY);
			break;

		case GLC_VERTEX_ARRAY:
			glEnableVertexAttribArrayARB(ARRAY_VERTEX);
			break;
		case GLC_NORMAL_ARRAY:
			glEnableVertexAttribArrayARB(ARRAY_NORMAL);
			break;
		case GLC_COLOR_ARRAY:
#if SAFE_COLORS
			glEnableClientState(GL_COLOR_ARRAY);
#else
			glEnableVertexAttribArrayARB(ARRAY_COLOR);
#endif
			break;
		case GLC_TANGENT_ARRAY:
			glEnableVertexAttribArrayARB(ARRAY_TANGENT);
			break;
		case GLC_BINORMAL_ARRAY:
			glEnableVertexAttribArrayARB(ARRAY_BINORMAL);
			break;
		case GLC_BONE_IDXS_ARRAY:
			glEnableVertexAttribArrayARB(ARRAY_BONEIDX);
			break;
		case GLC_BONE_WEIGHTS_ARRAY:
			glEnableVertexAttribArrayARB(ARRAY_BONEWEIGHT);
			break;

		default:
			assertmsg(0, "Bad vertex data array!");
			break;
	}
	CHECKGL;
}

__forceinline static void doDisableClientState(GLClientStateBits array)
{
	CHECKGLTHREAD;
	switch (array)
	{
		case GLC_TEXTURE_COORD_ARRAY_0:
			glClientActiveTextureARB(GL_TEXTURE0_ARB);
			glDisableClientState(GL_TEXTURE_COORD_ARRAY);
			break;
		case GLC_TEXTURE_COORD_ARRAY_1:
			glClientActiveTextureARB(GL_TEXTURE1_ARB);
			glDisableClientState(GL_TEXTURE_COORD_ARRAY);
			break;
		case GLC_TEXTURE_COORD_ARRAY_2:
			glClientActiveTextureARB(GL_TEXTURE2_ARB);
			glDisableClientState(GL_TEXTURE_COORD_ARRAY);
			break;
		case GLC_TEXTURE_COORD_ARRAY_3:
			glClientActiveTextureARB(GL_TEXTURE3_ARB);
			glDisableClientState(GL_TEXTURE_COORD_ARRAY);
			break;
		case GLC_TEXTURE_COORD_ARRAY_4:
			glClientActiveTextureARB(GL_TEXTURE4_ARB);
			glDisableClientState(GL_TEXTURE_COORD_ARRAY);
			break;
		case GLC_TEXTURE_COORD_ARRAY_5:
			glClientActiveTextureARB(GL_TEXTURE5_ARB);
			glDisableClientState(GL_TEXTURE_COORD_ARRAY);
			break;
		case GLC_TEXTURE_COORD_ARRAY_6:
			glClientActiveTextureARB(GL_TEXTURE6_ARB);
			glDisableClientState(GL_TEXTURE_COORD_ARRAY);
			break;
		case GLC_TEXTURE_COORD_ARRAY_7:
			glClientActiveTextureARB(GL_TEXTURE7_ARB);
			glDisableClientState(GL_TEXTURE_COORD_ARRAY);
			break;

		case GLC_VERTEX_ARRAY:
			glDisableVertexAttribArrayARB(ARRAY_VERTEX);
			break;
		case GLC_NORMAL_ARRAY:
			glDisableVertexAttribArrayARB(ARRAY_NORMAL);
			break;
		case GLC_COLOR_ARRAY:
#if SAFE_COLORS
			glDisableClientState(GL_COLOR_ARRAY);
#else
			glDisableVertexAttribArrayARB(ARRAY_COLOR);
#endif
			break;
		case GLC_TANGENT_ARRAY:
			glDisableVertexAttribArrayARB(ARRAY_TANGENT);
			break;
		case GLC_BINORMAL_ARRAY:
			glDisableVertexAttribArrayARB(ARRAY_BINORMAL);
			break;
		case GLC_BONE_IDXS_ARRAY:
			glDisableVertexAttribArrayARB(ARRAY_BONEIDX);
			break;
		case GLC_BONE_WEIGHTS_ARRAY:
			glDisableVertexAttribArrayARB(ARRAY_BONEWEIGHT);
			break;

		default:
			assertmsg(0, "Bad vertex data array!");
			break;
	}
	CHECKGL;
}

__forceinline static void resetVBOState(void)
{
	int i;
	current_state->vbo.last_element_array_id = -1;
	current_state->vbo.last_vertex_array_id = -1;
	current_state->vbo.verts = (void *)-1;
	current_state->vbo.colors = (void *)-1;
	current_state->vbo.norms = (void *)-1;
	current_state->vbo.tangents = (void *)-1;
	current_state->vbo.binorms = (void *)-1;
	current_state->vbo.boneidxs = (void *)-1;
	current_state->vbo.boneweights = (void *)-1;
	for (i = 0; i < MAX_TEXTURE_COORDS; i++)
		current_state->vbo.texcoords[i] = (void *)-1;
}

__forceinline static void resetProgramState(ProgramType target)
{
	current_state->program[target].enabled = -1;
	current_state->program[target].bound_program = -1;
	ZeroStruct(&current_state->program[target].local_parameters_set);
	ZeroStruct(&current_state->program[target].local_parameters);
	ZeroStruct(&current_state->program[target].env_parameters_set);
	ZeroStruct(&current_state->program[target].env_parameters);
}

__forceinline static void releaseSurface(int tex_unit)
{
	RdrSurfaceWinGL *bound_surface = current_state->tex[tex_unit].bound_surface;
	RdrSurfaceBuffer bound_surface_buffer = current_state->tex[tex_unit].bound_surface_buffer;
	// Clear first so that this doesn't happen recursively!
	current_state->tex[tex_unit].bound_surface = 0;
	current_state->tex[tex_unit].bound_surface_buffer = 0;
	rwglReleaseSurfaceDirect(bound_surface, tex_unit, bound_surface_buffer);
}

__forceinline static void resetTextureState(int reset_bias)
{
	int i;
	for (i = 0; i < MAX_TEXTURE_UNITS_TOTAL; i++)
	{
		if (current_state->tex[i].bound_surface)
		{
			releaseSurface(i);
		}
		current_state->tex[i].bound_id = 0;
		current_state->tex[i].bound_target = GL_TEXTURE_2D;
		if (reset_bias)
			current_state->tex[i].lod_bias = -9999;
		glActiveTextureARB(GL_TEXTURE0_ARB+i);
		glBindTexture(GL_TEXTURE_2D, 0);
	}
}

void rwglResetTextureState(void)
{
	if (!current_state)
		return;
	resetTextureState(0);
	resetVBOState();
}

void rwglResetState(void)
{
	int i;

	CHECKGLTHREAD;

	if (!current_state)
		return;

	// stencil
	current_state->stencil.func = -1;
	current_state->stencil.ref = -1;
	current_state->stencil.mask = -1;
	current_state->stencil.fail = -1;
	current_state->stencil.zfail = -1;
	current_state->stencil.zpass = -1;
	current_state->stencil.enabled = -1;

	// textures
	resetTextureState(1);

	// vertex program
	resetProgramState(GLC_VERTEX_PROG);

	// fragment program
	resetProgramState(GLC_FRAGMENT_PROG);

	// blend function stack
	current_state->blend_func.stack_frozen = 0;
	current_state->blend_func.stack_idx = 0;

	// fog
	current_state->fog.on = -1;
	current_state->fog.doneonce = 0;
	current_state->fog.old[0] = -1;
	current_state->fog.old[1] = -1;

	// vbo
	resetVBOState();

	// arrays enabled
	for (i = 1; i != GLC_MAXBIT; i<<=1)
		doDisableClientState(i);
	current_state->clientstatebits = 0;
#ifdef DOING_CHECKGL
	glGetError(); // Quite likely not all these client states are supported on all cards
#endif

	// depth mask
	current_state->depthmask = -1;

	// projection
	current_state->width_2d = 0;
	glMatrixMode(GL_PROJECTION);
	glLoadMatrixf(&current_state->projection_mat3d[0][0]);

	// ambient light
	setVec3same(current_state->ambient_light, -1);
}

RdrStateWinGL *rwglGetCurrentState(void)
{
	return current_state;
}

__forceinline static void setProgramStateActive(ProgramType target)
{
	GLenum gltarget = (target==GLC_VERTEX_PROG)?GL_VERTEX_PROGRAM_ARB:GL_FRAGMENT_PROGRAM_ARB;
	assert(target < 2);
	if (current_state->program[target].enabled == 1 && current_state->program[target].bound_program != -1)
	{
		int i;
		glEnable(gltarget);
		CHECKGL;
		glBindProgramARB(gltarget, current_state->program[target].bound_program);
		CHECKGL;
		for (i = 0; i < ARRAY_SIZE(current_state->program[target].local_parameters); i++)
		{
			if (current_state->program[target].local_parameters_set[i])
				glProgramLocalParameter4fvARB(gltarget, i, current_state->program[target].local_parameters[i]);
			CHECKGL;
		}
		for (i = 0; i < ARRAY_SIZE(current_state->program[target].env_parameters); i++)
		{
			if (current_state->program[target].env_parameters_set[i])
				glProgramEnvParameter4fvARB(gltarget, i, current_state->program[target].env_parameters[i]);
			CHECKGL;
		}
	}
	else if (current_state->program[target].enabled == 0)
	{
		glDisable(gltarget);
	}
}

void rwglSetStateActive(RdrStateWinGL *state, int firsttime, int surface_width, int surface_height)
{
	int i;

	if (current_state)
	{
		for (i = 0; i < MAX_TEXTURE_UNITS_TOTAL; i++)
		{
			if (current_state->tex[i].bound_surface)
			{
				releaseSurface(i);
			}
		}
	}

	current_state = state;

	if (!state)
		return;

	CHECKGLTHREAD;

	if (firsttime)
	{
		rwglResetState();

		CHECKGL;
		glClearColor(0.0, 0.0, 0.0, 0.0);

		copyMat4(unitmat, current_state->viewmat);
		copyMat4(unitmat, current_state->inv_viewmat);
		copyMat44(unitmat44, current_state->projection_mat3d);
		current_state->width_2d = 1; // force the state manager to load the new matrices
		rwglSet3DMode();

		CHECKGL;
		glCullFace(GL_BACK);
		glFrontFace(GL_CW);
		glEnable(GL_CULL_FACE);
		CHECKGL;
		glDepthFunc(GL_LEQUAL);
		glEnable(GL_DEPTH_TEST);
		CHECKGL;
		glShadeModel(GL_SMOOTH);
		rwglBlendFunc(GL_SRC_ALPHA, GL_ONE_MINUS_SRC_ALPHA, BLENDOP_ADD);
		glEnable(GL_BLEND);
		CHECKGL;
		glDisable(GL_ALPHA_TEST);
		CHECKGL;
		rwglFog(1);
		glPixelStorei(GL_UNPACK_ALIGNMENT, 1);
		CHECKGL;
		glHint(GL_PERSPECTIVE_CORRECTION_HINT, GL_NICEST);
		CHECKGL;
		glDisable(GL_COLOR_MATERIAL);
		glDisable(GL_LIGHTING);
		CHECKGL;

		return;
	}

	// viewport
	glViewport(0, 0, surface_width, surface_height);

	// projection
	if (current_state->width_2d)
	{
		int width = current_state->width_2d;
		int height = current_state->height_2d;
		current_state->width_2d = current_state->height_2d = 0;
		rwglSet2DMode(width, height);
	}
	else
	{
		current_state->width_2d = 1; // force the state manager to load matrices
		rwglSet3DMode();
	}

	// stencil
	if (current_state->stencil.enabled == 0)
		glDisable(GL_STENCIL_TEST);
	else if (current_state->stencil.enabled == 1)
		glEnable(GL_STENCIL_TEST);
	CHECKGL;
	if (current_state->stencil.func != -1)
		glStencilFunc(current_state->stencil.func, current_state->stencil.ref, current_state->stencil.mask);
	CHECKGL;
	if (current_state->stencil.fail != -1)
		glStencilOp(current_state->stencil.fail, current_state->stencil.zfail, current_state->stencil.zpass);
	CHECKGL;

	// textures
	for (i = 0; i < MAX_TEXTURE_UNITS_TOTAL; i++)
	{
		glActiveTextureARB(GL_TEXTURE0_ARB+i);
		CHECKGL;
		if (current_state->tex[i].bound_target > 0)
			glBindTexture(current_state->tex[i].bound_target, current_state->tex[i].bound_id);
		CHECKGL;
		if (current_state->tex[i].lod_bias > -9999)
			glTexEnvf(GL_TEXTURE_FILTER_CONTROL_EXT, GL_TEXTURE_LOD_BIAS_EXT, current_state->tex[i].lod_bias);
		CHECKGL;
	}

	// vertex program
	setProgramStateActive(GLC_VERTEX_PROG);

	// fragment program
	setProgramStateActive(GLC_FRAGMENT_PROG);

	// blend function
	if (current_state->blend_func.stack_idx >= 0)
		glBlendFunc(current_state->blend_func.stack[current_state->blend_func.stack_idx].sfactor, current_state->blend_func.stack[current_state->blend_func.stack_idx].dfactor);
	CHECKGL;

	// fog
	glFogf(GL_FOG_START, current_state->fog.current[0]);
	CHECKGL;
	glFogf(GL_FOG_END, current_state->fog.current[1]);
	CHECKGL;
	glFogfv(GL_FOG_COLOR, current_state->fog.color);
	CHECKGL;

	// vbo
	resetVBOState();

	// arrays enabled
	for (i = 1; i != GLC_MAXBIT; i<<=1)
	{
		if (current_state->clientstatebits & i)
			doEnableClientState(i);
		else
			doDisableClientState(i);
	}
#ifdef DOING_CHECKGL
	glGetError(); // Quite likely not all these client states are supported on all cards
#endif

	// depth mask
	if (current_state->depthmask != -1)
		glDepthMask(current_state->depthmask);
	CHECKGL;

	// ambient light
	if (current_state->ambient_light[0] < 0)
	{
		Vec4 lightambient;
		copyVec3(current_state->ambient_light, lightambient);
		lightambient[3] = 0;
		rwglProgramEnvParameter(GLC_FRAGMENT_PROG, FPENV_LIGHT_AMBIENT, lightambient);
	}
}

//////////////////////////////////////////////////////////////////////////

void rwglSet3DProjection(RdrStateWinGL *state, const Mat44 projection)
{
	copyMat44(projection, state->projection_mat3d);
	invertMat44Copy(projection, state->inv_projection_mat3d);
	if (state == current_state)
	{
		current_state->width_2d = 1; // force the state manager to load the new projection matrix
		rwglSet3DMode();
	}
}

void rwglSet3DMode(void)
{
#if ENABLE_STATEMANAGEMENT
	if (current_state->width_2d)
#endif
	{
		Mat44 viewmat44, worldclipmat44;

		glMatrixMode(GL_PROJECTION);
		glLoadMatrixf(&current_state->projection_mat3d[0][0]);
		CHECKGL;

		glMatrixMode(GL_MODELVIEW);
		mat43to44(current_state->viewmat, viewmat44);
		glLoadMatrixf(&viewmat44[0][0]);
		CHECKGL;

		glActiveTextureARB(GL_TEXTURE0_ARB);
		glMatrixMode(GL_TEXTURE);
		mulMat44Inline(current_state->projection_mat3d, viewmat44, worldclipmat44);
		glLoadMatrixf(&worldclipmat44[0][0]);
		CHECKGL;

		glActiveTextureARB(GL_TEXTURE1_ARB);
		glMatrixMode(GL_TEXTURE);
		glLoadMatrixf(&viewmat44[0][0]);
		CHECKGL;

		glActiveTextureARB(GL_TEXTURE2_ARB);
		glMatrixMode(GL_TEXTURE);
		glLoadMatrixf(&current_state->inv_projection_mat3d[0][0]);
		CHECKGL;

		rwglFog(1);

		current_state->width_2d = 0;
		current_state->height_2d = 0;
	}
}

void rwglSet2DMode(int width, int height)
{
	CHECKGLTHREAD;

#if ENABLE_STATEMANAGEMENT
	if (width != current_state->width_2d || height != current_state->height_2d)
#endif
	{
		Mat44 proj44;

		rdrSetupOrthoGL(proj44, 0, (F32)width, 0, (F32)height, -1, 1000);

		glMatrixMode(GL_PROJECTION);
		glLoadMatrixf(&proj44[0][0]);
		CHECKGL;

		glMatrixMode(GL_MODELVIEW);
		glLoadIdentity();
		CHECKGL;

		rwglFog(0);

		current_state->width_2d = width;
		current_state->height_2d = height;
	}
}

void rwglSetViewMatrix(RdrStateWinGL *state, const Mat4 view_mat, const Mat4 inv_view_mat)
{
	Mat44 viewmat44, worldclipmat44;

	copyMat4(view_mat, state->viewmat);
	copyMat4(inv_view_mat, state->inv_viewmat);

	if (state == current_state)
	{
		CHECKGLTHREAD;

		glMatrixMode(GL_MODELVIEW);
		mat43to44(current_state->viewmat, viewmat44);
		glLoadMatrixf(&viewmat44[0][0]);
		CHECKGL;

		glActiveTextureARB(GL_TEXTURE0_ARB);
		glMatrixMode(GL_TEXTURE);
		mulMat44Inline(current_state->projection_mat3d, viewmat44, worldclipmat44);
		glLoadMatrixf(&worldclipmat44[0][0]);
		CHECKGL;

		glActiveTextureARB(GL_TEXTURE1_ARB);
		glMatrixMode(GL_TEXTURE);
		glLoadMatrixf(&viewmat44[0][0]);
		CHECKGL;
	}
}

void rwglPushModelMatrix(const Mat4 model_mat)
{
	Mat44 modelmat44;

	copyMat4(model_mat, current_state->modelmat);
	mat43to44(current_state->modelmat, modelmat44);

	CHECKGLTHREAD;

	rwglProgramLocalParameter(GLC_VERTEX_PROG, VP_BASEPOSE_OFFSET, modelmat44[3]);

	glMatrixMode(GL_MODELVIEW);
	glPushMatrix();
	glMultMatrixf(&modelmat44[0][0]);
	CHECKGL;
}

void rwglPopModelMatrix(void)
{
	CHECKGLTHREAD;

	glMatrixMode(GL_MODELVIEW);
	glPopMatrix();
	CHECKGL;
}

void rwglSetPPTextureSize(int width, int height, int vwidth, int vheight)
{
	Vec4 tex_size = {width, height, 1.f/vwidth, 1.f/vheight};
	rwglProgramLocalParameter(GLC_VERTEX_PROG, VP_PP_TEX_SIZE, tex_size);
}

//////////////////////////////////////////////////////////////////////////

__forceinline static void setBlendState(GLenum sfactor, GLenum dfactor, BlendOp op)
{
	glBlendFunc(sfactor, dfactor);
	CHECKGL;
	if (rdr_caps.supports_subtract)
	{
		if (op == BLENDOP_ADD)
			glBlendEquationEXT(GL_FUNC_ADD_EXT);
		else if (op == BLENDOP_SUBTRACT)
			glBlendEquationEXT(GL_FUNC_REVERSE_SUBTRACT_EXT);
		CHECKGL;
	}
}

void rwglBlendFunc(GLenum sfactor, GLenum dfactor, BlendOp op)
{
	CHECKGLTHREAD;

	setBlendState(sfactor, dfactor, op);

	current_state->blend_func.stack[current_state->blend_func.stack_idx].sfactor = sfactor;
	current_state->blend_func.stack[current_state->blend_func.stack_idx].dfactor = dfactor;
	current_state->blend_func.stack[current_state->blend_func.stack_idx].op = op;
}

void rwglBlendFuncPush(GLenum sfactor, GLenum dfactor, BlendOp op)
{
	CHECKGLTHREAD;

	if (current_state->blend_func.stack_frozen)
		return;

	setBlendState(sfactor, dfactor, op);

	current_state->blend_func.stack_idx++;
	assert(current_state->blend_func.stack_idx < 32);
	current_state->blend_func.stack[current_state->blend_func.stack_idx].sfactor = sfactor;
	current_state->blend_func.stack[current_state->blend_func.stack_idx].dfactor = dfactor;
	current_state->blend_func.stack[current_state->blend_func.stack_idx].op = op;
}

void rwglBlendFuncPushNop(void)
{
	CHECKGLTHREAD;

	if (current_state->blend_func.stack_frozen)
		return;

	current_state->blend_func.stack_idx++;
	assert(current_state->blend_func.stack_idx < 32);
	current_state->blend_func.stack[current_state->blend_func.stack_idx].sfactor = current_state->blend_func.stack[current_state->blend_func.stack_idx-1].sfactor;
	current_state->blend_func.stack[current_state->blend_func.stack_idx].dfactor = current_state->blend_func.stack[current_state->blend_func.stack_idx-1].dfactor;
	current_state->blend_func.stack[current_state->blend_func.stack_idx].op = current_state->blend_func.stack[current_state->blend_func.stack_idx-1].op;
}

void rwglBlendFuncPop(void)
{
	CHECKGLTHREAD;

	if (current_state->blend_func.stack_frozen)
		return;

	if (current_state->blend_func.stack_idx > 0)
	{
		current_state->blend_func.stack_idx--;
		setBlendState(current_state->blend_func.stack[current_state->blend_func.stack_idx].sfactor, current_state->blend_func.stack[current_state->blend_func.stack_idx].dfactor, current_state->blend_func.stack[current_state->blend_func.stack_idx].op);
	}
}

void rwglBlendStackFreeze(int freeze)
{
	CHECKGLTHREAD;
	current_state->blend_func.stack_frozen = freeze;
}

//////////////////////////////////////////////////////////////////////////

void rwglFogRange(RdrStateWinGL *state, F32 near_dist, F32 far_dist)
{
	if (!state->fog.on)
	{
		// Fog is off, set the restore parameter
		state->fog.old[0] = near_dist;
		state->fog.old[1] = far_dist;
	}
	else if (near_dist != state->fog.old[0] || far_dist != state->fog.old[1])
	{
		state->fog.old[0] = state->fog.current[0] = near_dist;
		state->fog.old[1] = state->fog.current[1] = far_dist;
		if (current_state == state)
		{
			CHECKGLTHREAD;
			glFogf(GL_FOG_START,near_dist);
			CHECKGL;
			glFogf(GL_FOG_END,far_dist);
			CHECKGL;
		}
	}
}

void rwglFogColor(RdrStateWinGL *state, Vec4 clr)
{
	copyVec4(clr, state->fog.color);
	if (state == current_state)
	{
		CHECKGLTHREAD;
		glFogfv(GL_FOG_COLOR, state->fog.color);
		CHECKGL;
	}
}

void rwglFog(int on)
{
	CHECKGLTHREAD;

	if (current_state->fog.force_off)
		on = 0;

#if ENABLE_STATEMANAGEMENT
	if (on == current_state->fog.on)
		return;
#endif

	if (!current_state->fog.doneonce)
	{
		current_state->fog.doneonce = 1;
		glEnable(GL_FOG);
		glFogi(GL_FOG_MODE,GL_LINEAR);
		CHECKGL;
	}

	if (on)
	{
		if (current_state->fog.old[0]==-1 && current_state->fog.old[1]==-1)
		{
			// State has been reset, restore defaults!
			current_state->fog.old[0] = 1000;
			current_state->fog.old[1] = 2000;
		}
		current_state->fog.current[0] = current_state->fog.old[0];
		current_state->fog.current[1] = current_state->fog.old[1];
		glFogf(GL_FOG_START,current_state->fog.current[0]);
		CHECKGL;
		glFogf(GL_FOG_END,current_state->fog.current[1]);
		CHECKGL;
	}
	else
	{
		current_state->fog.old[0] = current_state->fog.current[0];
		current_state->fog.old[1] = current_state->fog.current[1];
		current_state->fog.current[0] = 1000000;
		current_state->fog.current[1] = 1000001;
		glFogf(GL_FOG_START,current_state->fog.current[0]);
		CHECKGL;
		glFogf(GL_FOG_END,current_state->fog.current[1]);
		CHECKGL;
	}
	current_state->fog.on = on;
}

void rwglFogPush(int on)
{
	CHECKGLTHREAD;

	if (current_state->fog.stack_depth >= ARRAY_SIZE(current_state->fog.stack))
		printf("Fog stack overflow");
	else
		current_state->fog.stack[current_state->fog.stack_depth] = current_state->fog.on;
	rwglFog(on);
	current_state->fog.stack_depth++;
}

void rwglFogPop(void)
{
	CHECKGLTHREAD;

	if (current_state->fog.stack_depth==0)
		printf("Fog stack underflow");
	else
		current_state->fog.stack_depth--;
	rwglFog(current_state->fog.stack[current_state->fog.stack_depth]);
}

//////////////////////////////////////////////////////////////////////////

void rwglEnableStencilTest(int enable)
{
	CHECKGLTHREAD;

#if ENABLE_STATEMANAGEMENT
	if (current_state->stencil.enabled != enable)
#endif
	{
		if (enable)
			glEnable(GL_STENCIL_TEST);
		else
			glDisable(GL_STENCIL_TEST);
		CHECKGL;
		current_state->stencil.enabled = enable;
	}
}

void rwglStencilFunc(GLenum func, GLint ref, GLuint mask)
{
	CHECKGLTHREAD;
#if ENABLE_STATEMANAGEMENT
	if (current_state->stencil.func == func && current_state->stencil.ref == ref && current_state->stencil.mask == mask)
	   return;
#endif
	glStencilFunc(func, ref, mask);
	CHECKGL;
	current_state->stencil.func = func;
	current_state->stencil.ref  = ref;
	current_state->stencil.mask = mask;
}

void rwglStencilOp(GLenum fail, GLenum zfail, GLenum zpass)
{
	CHECKGLTHREAD;
#if ENABLE_STATEMANAGEMENT
	if (current_state->stencil.fail == fail && current_state->stencil.zfail == zfail && current_state->stencil.zpass == zpass)
	   return;
#endif
	glStencilOp(fail, zfail, zpass);
	CHECKGL;
	current_state->stencil.fail  = fail;
	current_state->stencil.zfail = zfail;
	current_state->stencil.zpass = zpass;
}

//////////////////////////////////////////////////////////////////////////

void rwglDepthMask(GLboolean mask)
{
	CHECKGLTHREAD;
#if ENABLE_STATEMANAGEMENT
	if (current_state->depthmask != mask) 
#endif
	{
		current_state->depthmask = mask;
		glDepthMask(mask);
		CHECKGL;
	}
}

//////////////////////////////////////////////////////////////////////////

void rwglEnableArrays(GLClientStateBits enable)
{
	int i;
	CHECKGLTHREAD;

	for (i = 1; i != GLC_MAXBIT; i<<=1)
	{
		int current = current_state->clientstatebits & i;
		int need = enable & i;
#if ENABLE_STATEMANAGEMENT
		if (current != need)
#endif
		{
			if (need)
				doEnableClientState(i);
			else
				doDisableClientState(i);
		}
	}

	current_state->clientstatebits = enable;
}

//////////////////////////////////////////////////////////////////////////

void rwglUpdateVBOData(GLenum target, GLsizeiptrARB size, const GLvoid *data, GLenum usage, const char *memname)
{
	CHECKGLTHREAD;
	glBufferDataARB(target, size, data, usage);
	CHECKGL;
	memMonitorTrackUserMemory(memname, 1, size, MM_ALLOC);
}

void rwglDeleteVBO(GLenum target, U32 buffer_id, const char *memname) 
{
	int size;

	CHECKGLTHREAD;
	if (!buffer_id)
		return;
	CHECKGL;
	rwglBindVBO(target, buffer_id);
	CHECKGL;
	glGetBufferParameterivARB(target, GL_BUFFER_SIZE_ARB, &size);
	CHECKGL;
	memMonitorTrackUserMemory(memname, 1, -size, MM_FREE);
	glDeleteBuffersARB(1, &buffer_id);
	CHECKGL;
	resetVBOState();
}

void rwglBindVBO(GLenum target, U32 buffer_id)
{
	CHECKGLTHREAD;
	if (current_state->vbo.disable || !rdr_caps.supports_vbos)
	{
		if (current_state->vbo.used && current_state->vbo.last_element_array_id == -1)
		{
			current_state->vbo.last_element_array_id = 0;
			glBindBufferARB(GL_ARRAY_BUFFER_ARB, 0);
			glBindBufferARB(GL_ELEMENT_ARRAY_BUFFER_ARB, 0);
		}
		return;
	}

	current_state->vbo.used = 1;

	if (target == GL_ELEMENT_ARRAY_BUFFER_ARB)
	{
#if ENABLE_VBO_STATEMANAGEMENT
		if (buffer_id == current_state->vbo.last_element_array_id)
			return;
#endif
		current_state->vbo.last_element_array_id = buffer_id;
	}

	if (target == GL_ARRAY_BUFFER_ARB)
	{
		int i;
		RT_STAT_INC(buffer_calls);
#if ENABLE_VBO_STATEMANAGEMENT
		if (buffer_id == current_state->vbo.last_vertex_array_id)
			return;
#endif
		current_state->vbo.last_vertex_array_id = buffer_id;
		current_state->vbo.verts = (void *)-1;
		current_state->vbo.colors = (void *)-1;
		current_state->vbo.norms = (void *)-1;
		current_state->vbo.tangents = (void *)-1;
		current_state->vbo.binorms = (void *)-1;
		current_state->vbo.boneidxs = (void *)-1;
		current_state->vbo.boneweights = (void *)-1;
		for (i = 0; i < MAX_TEXTURE_COORDS; i++)
			current_state->vbo.texcoords[i] = (void *)-1;
		RT_STAT_BUFFER_CHANGE;
	}

	glBindBufferARB(target, buffer_id);
	CHECKGL;
}

// Note pointer managers assume that a given pointer will always have the same params
void rwglVertexPointer(GLint size, GLsizei stride, const GLvoid *pointer)
{   
	CHECKGLTHREAD;
#if ENABLE_VBO_STATEMANAGEMENT
	if (current_state->vbo.verts != pointer || !current_state->vbo.last_vertex_array_id) 
#endif
	{
		glVertexAttribPointerARB(ARRAY_VERTEX, size, GL_FLOAT, GL_FALSE, stride, pointer);
		CHECKGL;
		current_state->vbo.verts = pointer;
	}
}

void rwglVertex(Vec3 v)
{
	glVertexAttrib3fvARB(ARRAY_VERTEX, v);
//	glVertex3fv(v);
}

void rwglNormalPointer(GLsizei stride, const GLvoid *pointer)
{
	CHECKGLTHREAD;
#if ENABLE_VBO_STATEMANAGEMENT
	if (current_state->vbo.norms != pointer || !current_state->vbo.last_vertex_array_id)
#endif
	{
		glVertexAttribPointerARB(ARRAY_NORMAL, 3, GL_FLOAT, GL_FALSE, stride, pointer);
		CHECKGL;
		current_state->vbo.norms = pointer;
	}
}

void rwglColorPointer(GLint size, GLsizei stride, const GLvoid *pointer)
{   
	CHECKGLTHREAD;
#if ENABLE_VBO_STATEMANAGEMENT
	if (current_state->vbo.colors != pointer || !current_state->vbo.last_vertex_array_id) 
#endif
	{
#if SAFE_COLORS
		glColorPointer(size, GL_FLOAT, stride, pointer);
#else
		glVertexAttribPointerARB(ARRAY_COLOR, size, GL_FLOAT, GL_FALSE, stride, pointer);
#endif
		CHECKGL;
		current_state->vbo.colors = pointer;
	}
}

void rwglColor(Color color)
{
	Vec4 clr;
	colorToVec4(clr, color);
#if SAFE_COLORS
	glColor4fv(clr);
#else
	glVertexAttrib4fvARB(ARRAY_COLOR, clr);
#endif
}

void rwglColorf(Vec4 color)
{
#if SAFE_COLORS
	glColor4fv(color);
#else
	glVertexAttrib4fvARB(ARRAY_COLOR, color);
#endif
}

void rwglModelColorf(int index, Vec4 color)
{
	rwglProgramEnvParameter(GLC_FRAGMENT_PROG, FPENV_COLOR0 + index, color);
}


void rwglTangentPointer(GLsizei stride, const GLvoid *pointer)
{
	CHECKGLTHREAD;
#if ENABLE_VBO_STATEMANAGEMENT
	if (current_state->vbo.tangents != pointer || !current_state->vbo.last_vertex_array_id)
#endif
	{
		glVertexAttribPointerARB(ARRAY_TANGENT, 3, GL_FLOAT, GL_FALSE, stride, pointer);
		CHECKGL;
		current_state->vbo.tangents = pointer;
	}
}

void rwglBinormalPointer(GLsizei stride, const GLvoid *pointer)
{
	CHECKGLTHREAD;
#if ENABLE_VBO_STATEMANAGEMENT
	if (current_state->vbo.binorms != pointer || !current_state->vbo.last_vertex_array_id)
#endif
	{
		glVertexAttribPointerARB(ARRAY_BINORMAL, 3, GL_FLOAT, GL_FALSE, stride, pointer);
		CHECKGL;
		current_state->vbo.binorms = pointer;
	}
}

void rwglBoneIdxPointer(GLint size, GLsizei stride, const GLvoid *pointer)
{
	CHECKGLTHREAD;
#if ENABLE_VBO_STATEMANAGEMENT
	if (current_state->vbo.binorms != pointer || !current_state->vbo.last_vertex_array_id)
#endif
	{
		glVertexAttribPointerARB(ARRAY_BONEIDX, size, GL_UNSIGNED_SHORT, GL_FALSE, stride, pointer);
		CHECKGL;
		current_state->vbo.binorms = pointer;
	}
}

void rwglBoneWeightPointer(GLint size, GLsizei stride, const GLvoid *pointer)
{
	CHECKGLTHREAD;
#if ENABLE_VBO_STATEMANAGEMENT
	if (current_state->vbo.binorms != pointer || !current_state->vbo.last_vertex_array_id)
#endif
	{
		glVertexAttribPointerARB(ARRAY_BONEWEIGHT, size, GL_FLOAT, GL_FALSE, stride, pointer);
		CHECKGL;
		current_state->vbo.binorms = pointer;
	}
}

void rwglTexCoordPointer(U32 tex_unit, GLint size, GLsizei stride, const GLvoid *pointer)
{
	CHECKGLTHREAD;

	devassert(tex_unit < MAX_TEXTURE_COORDS);

#if ENABLE_VBO_STATEMANAGEMENT
	if (current_state->vbo.texcoords[tex_unit] != pointer || !current_state->vbo.last_vertex_array_id)
#endif
	{
		glClientActiveTextureARB(GL_TEXTURE0_ARB+tex_unit);
		CHECKGL;
		glTexCoordPointer(size, GL_FLOAT, stride, pointer);		
		CHECKGL;
		current_state->vbo.texcoords[tex_unit] = pointer;
	}
}


void rwglTexCoord(U32 tex_unit, Vec2 tex_coord)
{
	glMultiTexCoord2fvARB(GL_TEXTURE0_ARB+tex_unit,tex_coord);
}

//////////////////////////////////////////////////////////////////////////

void rwglBindTexture(GLenum target, U32 tex_unit, TexHandle tex)
{
	static bool recursive_call=false;
	RdrSurfaceBuffer buffer = 0;
	int set_index = 0;
	RdrSurfaceWinGL *surface = NULL;

	CHECKGLTHREAD;

	devassert(tex_unit < MAX_TEXTURE_UNITS_TOTAL);

	if (!tex)
		tex = current_state->white_tex_handle;

	if (tex < 0)
	{
		surface = (RdrSurfaceWinGL *)rdrGetSurfaceForTexHandle(tex, &buffer, &set_index);
#if ENABLE_STATEMANAGEMENT
		if (surface == current_state->tex[tex_unit].bound_surface && buffer == current_state->tex[tex_unit].bound_surface_buffer)
		{
			glActiveTextureARB(GL_TEXTURE0_ARB+tex_unit);
			return;
		}
#endif
		tex = 0;
	}

	if (current_state->tex[tex_unit].bound_surface)
	{
		assert(current_state->tex[tex_unit].bound_id != tex);
		assert(!recursive_call);
		releaseSurface(tex_unit);
	}

	recursive_call = true;

	if (surface)
	{
		int i;
		// if this buffer is already bound somewhere else, unbind it
		for (i = 0; i < MAX_TEXTURE_UNITS_TOTAL; i++)
		{
			if (i != tex_unit && current_state->tex[i].bound_surface == surface && current_state->tex[i].bound_surface_buffer == buffer)
			{
				releaseSurface(i);
				glActiveTextureARB(GL_TEXTURE0_ARB+i);
				glBindTexture(target, current_state->white_tex_handle);
				current_state->tex[i].bound_id = (U32)current_state->white_tex_handle;
				current_state->tex[i].bound_target = target;
			}
		}

		// bind the surface
		rwglBindSurfaceDirect(surface, tex_unit, buffer);
		current_state->tex[tex_unit].bound_surface = surface;
		current_state->tex[tex_unit].bound_surface_buffer = buffer;
		recursive_call = false;
		return;
	}

	RT_STAT_INC(texbind_calls);

	CHECKGL;
	glActiveTextureARB(GL_TEXTURE0_ARB+tex_unit);
	CHECKGL;

#if ENABLE_STATEMANAGEMENT
	if (current_state->tex[tex_unit].bound_id != tex || current_state->tex[tex_unit].bound_target != target)
#endif
	{
		glBindTexture(target, (U32)tex);
		CHECKGL;
		current_state->tex[tex_unit].bound_id = (U32)tex;
		current_state->tex[tex_unit].bound_target = target;

		RT_STAT_TEXBIND_CHANGE(tex_unit);
	}
	recursive_call = false;
}

void rwglBindWhiteTexture(U32 tex_unit)
{
	rwglBindTexture(GL_TEXTURE_2D, tex_unit, 0);
}

void rwglTexLODBias(U32 tex_unit, F32 newbias)
{	
	CHECKGLTHREAD;

	devassert(tex_unit < MAX_TEXTURE_UNITS_TOTAL);

	if (newbias > -8)
		newbias = MIN(0.01, newbias); // This number is minned to 0.01 because it's almost the same as 0, and avoids some bug in ATI's driver's statemanager, apparently.

#if ENABLE_STATEMANAGEMENT
	if (current_state->tex[tex_unit].lod_bias != newbias)
#endif
	{
		glActiveTextureARB(GL_TEXTURE0_ARB+tex_unit);
		CHECKGL;
		glTexEnvf(GL_TEXTURE_FILTER_CONTROL_EXT, GL_TEXTURE_LOD_BIAS_EXT, newbias);
		CHECKGL;
		current_state->tex[tex_unit].lod_bias = newbias;
	}
}

//////////////////////////////////////////////////////////////////////////

void rwglEnableProgram(ProgramType target, int enable)
{
	CHECKGLTHREAD;
#if ENABLE_STATEMANAGEMENT
	if (current_state->program[target].enabled != enable)
#endif
	{
		GLenum gltarget = (target==GLC_VERTEX_PROG)?GL_VERTEX_PROGRAM_ARB:GL_FRAGMENT_PROGRAM_ARB;
		if (enable)
			glEnable(gltarget);
		else
			glDisable(gltarget);
		CHECKGL;
		assert(target < 2);
		current_state->program[target].enabled = enable;
	}
}

void rwglBindProgram(ProgramType target, GLuint id)
{
	CHECKGLTHREAD;
#if ENABLE_STATEMANAGEMENT
	if (id != current_state->program[target].bound_program)
#endif
	{
		GLenum gltarget = (target==GLC_VERTEX_PROG)?GL_VERTEX_PROGRAM_ARB:GL_FRAGMENT_PROGRAM_ARB;
		if (target == GLC_VERTEX_PROG)
			RT_STAT_VP_CHANGE;
		else
			RT_STAT_FP_CHANGE;
		glBindProgramARB(gltarget, id);
		CHECKGL;
		assert(target < 2);
		ZeroStruct(&current_state->program[target].local_parameters_set);
		ZeroStruct(&current_state->program[target].local_parameters);
		current_state->program[target].bound_program = id;
	}
}

void rwglProgramLocalParameter(ProgramType target, GLuint n, const GLfloat *vec4)
{
	CHECKGLTHREAD;
#if ENABLE_STATEMANAGEMENT
	if (!current_state->program[target].local_parameters_set[n] || memcmp(current_state->program[target].local_parameters[n], vec4, sizeof(GLfloat)*4)!=0)
#endif
	{
		GLenum gltarget = (target==GLC_VERTEX_PROG)?GL_VERTEX_PROGRAM_ARB:GL_FRAGMENT_PROGRAM_ARB;
		assert(target < 2);
		current_state->program[target].local_parameters_set[n] = 1;
		copyVec4(vec4, current_state->program[target].local_parameters[n]);
		glProgramLocalParameter4fvARB(gltarget, n, vec4);
		CHECKGL;
	}
}

void rwglProgramEnvParameter(ProgramType target, GLuint n, const GLfloat *vec4)
{
	CHECKGLTHREAD;
#if ENABLE_STATEMANAGEMENT
	if (!current_state->program[target].env_parameters_set[n] || memcmp(current_state->program[target].env_parameters[n], vec4, sizeof(GLfloat)*4)!=0)
#endif
	{
		GLenum gltarget = (target==GLC_VERTEX_PROG)?GL_VERTEX_PROGRAM_ARB:GL_FRAGMENT_PROGRAM_ARB;
		assert(target < 2);
		current_state->program[target].env_parameters_set[n] = 1;
		copyVec4(vec4, current_state->program[target].env_parameters[n]);
		glProgramEnvParameter4fvARB(gltarget, n, vec4);
		CHECKGL;
	}
}

//////////////////////////////////////////////////////////////////////////

void rwglSetupAmbientLight(const Vec3 ambient)
{
	Vec4 lightambient;

#if ENABLE_STATEMANAGEMENT
	if (nearSameVec3(ambient, current_state->ambient_light))
		return;
#endif

	copyVec3(ambient, current_state->ambient_light);

	// light ambient
	copyVec3(ambient, lightambient);
	lightambient[3] = 0;
	rwglProgramEnvParameter(GLC_FRAGMENT_PROG, FPENV_LIGHT_AMBIENT, lightambient);
}

//////////////////////////////////////////////////////////////////////////
//////////////////////////////////////////////////////////////////////////

typedef struct GlState
{
	int activetexture;
	int alphatest;
	int alphatestfunc;
	int alphatestref;
	int attribstackdepth;
	int blend;
	int clientactivetexture;
	int clientattribstackdepth;
	float color[4];
	int colorarray_enabled;
	int colorarray_size;
	int colorarray_type;
	F32 *colorarray_pointer;
	int colormaterial;
	int colormaterialparameter;
	int colorwritemask[4];
	int cullface_enabled;
	int cullface_mode;
	int depthtest;
	int depthwritemask;
	int fog;
	float fogcolor[4];
	float fogdist[2];
	int fogdensity;
	int light0;
	int lighting;
	F32 lightmodelambient[4];
	int matrixmode;
	F32 modelviewmatrix[4][4];
	int normalarray_enabled;
	int normalarray_type;
	F32 *normalarray_pointer;
	int normalize;
	F32 projectionmatrix[4][4];
	int projectionstackdepth;
	int rendermode;
	int rescalenormal;
	int rgbamode;
	int shademodel;
	int stencilbits;
	int stenciltest;
	int stencilvaluemask;
	int stencilwritemask;
	struct {
		int texture2d;
		int texturebinding2d;
		int texturecoordarray_enabled;
		int texturecoordarraybufferbinding;
		int texturecoordarray_size;
		int texturecoordarray_type;
		F32 *texturecoordarray_pointer;
		F32 mat[4][4];
	} texunit[MAX_TEXTURE_UNITS_TOTAL];
	int texturestackdepth;
	int vertexarray_enabled;
	int vertexarray_size;
	int vertexarray_type;
	F32 *vertexarray_pointer;
	float viewport[4];

	float ambient[4];
	float diffuse[4];
	float specular[4];
	float position[4];

	int arraybufferbinding;
	int vertexarraybufferbinding;
	int normalarraybufferbinding;
	int colorarraybufferbinding;
	int indexarraybufferbinding;
	int elementarraybufferbinding;

	struct {
		int enabled;
		struct {
			int enabled;
			int size;
			int stride;
			int type;
			int normalized;
			F32 *pointer;
		} vertex_attrib_array[16];
		struct {
			int name;
			int length;
			char *string;
			int instructions;
			int temporaries;
			int parameters;
			int attribs;
			int address_registers;
			int native_instructions;
			int native_temporaries;
			int native_parameters;
			int native_attribs;
			int under_native_limits;
		} program;
	} vertex_program_arb;

} GlState;

GlState gls; 

void getGlState()
{
	int i;
	glGetIntegerv(	GL_ARRAY_BUFFER_BINDING_ARB,		&gls.arraybufferbinding);
	glGetIntegerv(	GL_VERTEX_ARRAY_BUFFER_BINDING_ARB,	&gls.vertexarraybufferbinding);
	glGetIntegerv(	GL_NORMAL_ARRAY_BUFFER_BINDING_ARB,	&gls.normalarraybufferbinding);
	glGetIntegerv(	GL_COLOR_ARRAY_BUFFER_BINDING_ARB,	&gls.colorarraybufferbinding);
	glGetIntegerv(	GL_INDEX_ARRAY_BUFFER_BINDING_ARB,	&gls.indexarraybufferbinding);
	glGetIntegerv(	GL_ELEMENT_ARRAY_BUFFER_BINDING_ARB,&gls.elementarraybufferbinding);
	glGetIntegerv(	GL_ACTIVE_TEXTURE_ARB,			&gls.activetexture);
	glGetIntegerv(	GL_ALPHA_TEST,					&gls.alphatest);
	glGetIntegerv(	GL_ALPHA_TEST_FUNC,				&gls.alphatestfunc);
	glGetIntegerv(	GL_ALPHA_TEST_REF,				&gls.alphatestref);
	glGetIntegerv(	GL_ATTRIB_STACK_DEPTH,			&gls.attribstackdepth);
	glGetIntegerv(	GL_BLEND,						&gls.blend);
	glGetIntegerv(	GL_CLIENT_ACTIVE_TEXTURE_ARB,	&gls.clientactivetexture);
	glGetIntegerv(	GL_CLIENT_ATTRIB_STACK_DEPTH,	&gls.clientattribstackdepth);
	glGetFloatv(	GL_CURRENT_COLOR,				 gls.color);
	glGetIntegerv(	GL_COLOR_ARRAY,					&gls.colorarray_enabled);
	glGetIntegerv(	GL_COLOR_ARRAY_SIZE,			&gls.colorarray_size);
	glGetIntegerv(	GL_COLOR_ARRAY_TYPE,			&gls.colorarray_type);
	glGetPointerv(	GL_COLOR_ARRAY_POINTER,			&gls.colorarray_pointer);
	glGetIntegerv(	GL_COLOR_MATERIAL,				&gls.colormaterial);
	glGetIntegerv(	GL_COLOR_MATERIAL_PARAMETER,	&gls.colormaterialparameter);
	glGetIntegerv(	GL_COLOR_WRITEMASK,				gls.colorwritemask);
	glGetIntegerv(	GL_CULL_FACE,					&gls.cullface_enabled);
	glGetIntegerv(	GL_CULL_FACE_MODE,				&gls.cullface_mode);
	glGetIntegerv(	GL_DEPTH_TEST,					&gls.depthtest);
	glGetIntegerv(	GL_DEPTH_WRITEMASK,				&gls.depthwritemask);
	glGetIntegerv(	GL_FOG,							&gls.fog);
	glGetFloatv(	GL_FOG_COLOR,					 gls.fogcolor);
	glGetFloatv(	GL_FOG_START,					 &gls.fogdist[0]);
	glGetFloatv(	GL_FOG_END,						 &gls.fogdist[1]);
	glGetIntegerv(	GL_FOG_DENSITY,					&gls.fogdensity);
	glGetIntegerv(	GL_LIGHT0,						&gls.light0);
	glGetIntegerv(	GL_LIGHTING,					&gls.lighting);
	glGetFloatv(	GL_LIGHT_MODEL_AMBIENT,			 gls.lightmodelambient);
	glGetIntegerv(	GL_MATRIX_MODE,					&gls.matrixmode);
	glGetFloatv(	GL_MODELVIEW_MATRIX,			&gls.modelviewmatrix[0][0]);
	glGetIntegerv(	GL_NORMAL_ARRAY,				&gls.normalarray_enabled);
	glGetIntegerv(	GL_NORMAL_ARRAY_TYPE,			&gls.normalarray_type);
	glGetPointerv(	GL_NORMAL_ARRAY_POINTER,		&gls.normalarray_pointer);
	glGetIntegerv(	GL_NORMALIZE,					&gls.normalize);
	glGetFloatv(	GL_PROJECTION_MATRIX,			&gls.projectionmatrix[0][0]);
	glGetIntegerv(	GL_PROJECTION_STACK_DEPTH,		&gls.projectionstackdepth);
	glGetIntegerv(	GL_RENDER_MODE,					&gls.rendermode);
	glGetIntegerv(	GL_RESCALE_NORMAL,				&gls.rescalenormal);
	glGetIntegerv(	GL_RGBA_MODE,					&gls.rgbamode);
	glGetIntegerv(	GL_SHADE_MODEL,					&gls.shademodel);
	glGetIntegerv(	GL_STENCIL_BITS,				&gls.stencilbits);
	glGetIntegerv(	GL_STENCIL_TEST,				&gls.stenciltest);
	glGetIntegerv(	GL_STENCIL_VALUE_MASK,			&gls.stencilvaluemask);
	glGetIntegerv(	GL_STENCIL_WRITEMASK,			&gls.stencilwritemask);
	for (i=0; i<ARRAY_SIZE(gls.texunit) && i<MAX_TEXTURE_COORDS; i++) {
		if (glActiveTextureARB)
			glActiveTextureARB(GL_TEXTURE0_ARB + i);
		if (glClientActiveTextureARB)
			glClientActiveTextureARB(GL_TEXTURE0_ARB + i);
		glGetIntegerv(	GL_TEXTURE_2D,					&gls.texunit[i].texture2d);
		glGetIntegerv(	GL_TEXTURE_BINDING_2D,			&gls.texunit[i].texturebinding2d);
		glGetIntegerv(	GL_TEXTURE_COORD_ARRAY,			&gls.texunit[i].texturecoordarray_enabled);
		glGetIntegerv(	GL_TEXTURE_COORD_ARRAY_BUFFER_BINDING_ARB,		&gls.texunit[i].texturecoordarraybufferbinding);
		glGetIntegerv(	GL_TEXTURE_COORD_ARRAY_SIZE,	&gls.texunit[i].texturecoordarray_size);
		glGetIntegerv(	GL_TEXTURE_COORD_ARRAY_TYPE,	&gls.texunit[i].texturecoordarray_type);
		glGetPointerv(  GL_TEXTURE_COORD_ARRAY_POINTER, &gls.texunit[i].texturecoordarray_pointer);
		glGetFloatv(  GL_TEXTURE_MATRIX, &gls.texunit[i].mat[0][0] );
	}
	if (glActiveTextureARB)
		glActiveTextureARB(gls.activetexture);
	if (glClientActiveTextureARB)
		glClientActiveTextureARB(gls.clientactivetexture);
	glGetIntegerv(	GL_TEXTURE_STACK_DEPTH,			&gls.texturestackdepth);
	glGetIntegerv(	GL_VERTEX_ARRAY,				&gls.vertexarray_enabled);
	glGetIntegerv(	GL_VERTEX_ARRAY_SIZE,			&gls.vertexarray_size);
	glGetIntegerv(	GL_VERTEX_ARRAY_TYPE,			&gls.vertexarray_type);
	glGetPointerv(	GL_VERTEX_ARRAY_POINTER,		&gls.vertexarray_pointer);
	glGetFloatv(	GL_VIEWPORT,					 gls.viewport);

	glGetLightfv(	GL_LIGHT0, GL_AMBIENT,			 gls.ambient);
	glGetLightfv(	GL_LIGHT0, GL_DIFFUSE,			 gls.diffuse);
	glGetLightfv(	GL_LIGHT0, GL_SPECULAR,			 gls.specular);
	glGetLightfv(	GL_LIGHT0, GL_POSITION,			 gls.position);

	gls.vertex_program_arb.enabled = glIsEnabled(GL_VERTEX_PROGRAM_ARB);
	if (glGetVertexAttribivARB) {
		for (i=0; i<ARRAY_SIZE(gls.vertex_program_arb.vertex_attrib_array); i++) {
			glGetVertexAttribivARB(i, GL_VERTEX_ATTRIB_ARRAY_ENABLED_ARB, &gls.vertex_program_arb.vertex_attrib_array[i].enabled);
			glGetVertexAttribivARB(i, GL_VERTEX_ATTRIB_ARRAY_SIZE_ARB, &gls.vertex_program_arb.vertex_attrib_array[i].size);
			glGetVertexAttribivARB(i, GL_VERTEX_ATTRIB_ARRAY_STRIDE_ARB, &gls.vertex_program_arb.vertex_attrib_array[i].stride);
			glGetVertexAttribivARB(i, GL_VERTEX_ATTRIB_ARRAY_TYPE_ARB, &gls.vertex_program_arb.vertex_attrib_array[i].type);
			glGetVertexAttribivARB(i, GL_VERTEX_ATTRIB_ARRAY_NORMALIZED_ARB, &gls.vertex_program_arb.vertex_attrib_array[i].normalized);
			glGetVertexAttribPointervARB(i, GL_VERTEX_ATTRIB_ARRAY_POINTER_ARB, &gls.vertex_program_arb.vertex_attrib_array[i].pointer);
		}
	}
	if (glGetProgramivARB) {
		glGetProgramivARB(GL_VERTEX_PROGRAM_ARB, GL_PROGRAM_BINDING_ARB, &gls.vertex_program_arb.program.name);
		glGetProgramivARB(GL_VERTEX_PROGRAM_ARB, GL_PROGRAM_LENGTH_ARB, &gls.vertex_program_arb.program.length);
		gls.vertex_program_arb.program.string = _alloca(gls.vertex_program_arb.program.length+1);
		if (gls.vertex_program_arb.program.string)
			glGetProgramStringARB(GL_VERTEX_PROGRAM_ARB, GL_PROGRAM_STRING_ARB, gls.vertex_program_arb.program.string);
		glGetProgramivARB(GL_VERTEX_PROGRAM_ARB, GL_PROGRAM_INSTRUCTIONS_ARB, &gls.vertex_program_arb.program.instructions);
		glGetProgramivARB(GL_VERTEX_PROGRAM_ARB, GL_PROGRAM_TEMPORARIES_ARB, &gls.vertex_program_arb.program.temporaries);
		glGetProgramivARB(GL_VERTEX_PROGRAM_ARB, GL_PROGRAM_PARAMETERS_ARB, &gls.vertex_program_arb.program.parameters);
		glGetProgramivARB(GL_VERTEX_PROGRAM_ARB, GL_PROGRAM_ATTRIBS_ARB, &gls.vertex_program_arb.program.attribs);
		glGetProgramivARB(GL_VERTEX_PROGRAM_ARB, GL_PROGRAM_ADDRESS_REGISTERS_ARB, &gls.vertex_program_arb.program.address_registers);
		glGetProgramivARB(GL_VERTEX_PROGRAM_ARB, GL_PROGRAM_NATIVE_INSTRUCTIONS_ARB, &gls.vertex_program_arb.program.native_instructions);
		glGetProgramivARB(GL_VERTEX_PROGRAM_ARB, GL_PROGRAM_NATIVE_TEMPORARIES_ARB, &gls.vertex_program_arb.program.native_temporaries);
		glGetProgramivARB(GL_VERTEX_PROGRAM_ARB, GL_PROGRAM_NATIVE_PARAMETERS_ARB, &gls.vertex_program_arb.program.native_parameters);
		glGetProgramivARB(GL_VERTEX_PROGRAM_ARB, GL_PROGRAM_NATIVE_ATTRIBS_ARB, &gls.vertex_program_arb.program.native_attribs);
		glGetProgramivARB(GL_VERTEX_PROGRAM_ARB, GL_PROGRAM_UNDER_NATIVE_LIMITS_ARB, &gls.vertex_program_arb.program.under_native_limits);
	}

	glGetError(); // Clear the errors invariably set by this function
}

// for debugging from the command window
void recordGlState(char *name)
{
	static int num = 1;
	FILE *f = 0;
	int i;
	char filename[MAX_PATH];

	getGlState();

	if (!name)
		sprintf_s(SAFESTR(filename), "c:\\glstate%d.txt", num++);
	else
		sprintf_s(SAFESTR(filename), "c:\\glstate_%s.txt", name);

	f = fopen(filename, "wt");
	if (!f)
		return;

#define PRINTSTATE(current_state, value) fprintf(f, #current_state ": %d\n", value);
#define PRINTSTATE4(current_state, value) fprintf(f, #current_state ": %d, %d, %d, %d\n", value[0], value[1], value[2], value[3]);
#define PRINTSTATEF(current_state, value) fprintf(f, #current_state ": %f\n", value);
#define PRINTSTATEF4(current_state, value) fprintf(f, #current_state ": %f, %f, %f, %f\n", value[0], value[1], value[2], value[3]);

	PRINTSTATE(	GL_ACTIVE_TEXTURE_ARB,			gls.activetexture);
	PRINTSTATE(	GL_ALPHA_TEST,					gls.alphatest);
	PRINTSTATE(	GL_ALPHA_TEST_FUNC,				gls.alphatestfunc);
	PRINTSTATE(	GL_ALPHA_TEST_REF,				gls.alphatestref);
	PRINTSTATE(	GL_ARRAY_BUFFER_BINDING_ARB,		gls.arraybufferbinding);
	PRINTSTATE(	GL_ATTRIB_STACK_DEPTH,			gls.attribstackdepth);
	PRINTSTATE(	GL_BLEND,						gls.blend);
	PRINTSTATE(	GL_CLIENT_ACTIVE_TEXTURE_ARB,	gls.clientactivetexture);
	PRINTSTATE(	GL_CLIENT_ATTRIB_STACK_DEPTH,	gls.clientattribstackdepth);
	PRINTSTATEF4(GL_CURRENT_COLOR,				gls.color);
	PRINTSTATE(	GL_COLOR_ARRAY,					gls.colorarray_enabled);
	PRINTSTATE(	GL_COLOR_ARRAY_BUFFER_BINDING_ARB,	gls.colorarraybufferbinding);
	PRINTSTATE(	GL_COLOR_ARRAY_SIZE,			gls.colorarray_size);
	PRINTSTATE(	GL_COLOR_ARRAY_TYPE,			gls.colorarray_type);
	PRINTSTATE(	GL_COLOR_ARRAY_POINTER,			PTR_TO_U32(gls.colorarray_pointer));
	PRINTSTATE(	GL_COLOR_MATERIAL,				gls.colormaterial);
	PRINTSTATE(	GL_COLOR_MATERIAL_PARAMETER,	gls.colormaterialparameter);
	PRINTSTATE4(GL_COLOR_WRITEMASK,				gls.colorwritemask);
	PRINTSTATE(	GL_CULL_FACE,					gls.cullface_enabled);
	PRINTSTATE(	GL_CULL_FACE_MODE,				gls.cullface_mode);
	PRINTSTATE(	GL_DEPTH_TEST,					gls.depthtest);
	PRINTSTATE(	GL_DEPTH_WRITEMASK,				gls.depthwritemask);
	PRINTSTATE(	GL_ELEMENT_ARRAY_BUFFER_BINDING_ARB,gls.elementarraybufferbinding);
	PRINTSTATE(	GL_FOG,							gls.fog);
	PRINTSTATEF4(GL_FOG_COLOR,					gls.fogcolor);
	PRINTSTATEF(GL_FOG_START,					gls.fogdist[0]);
	PRINTSTATEF(GL_FOG_END,						gls.fogdist[1]);
	PRINTSTATE(	GL_FOG_DENSITY,					gls.fogdensity);
	PRINTSTATE(	GL_INDEX_ARRAY_BUFFER_BINDING_ARB,	gls.indexarraybufferbinding);
	PRINTSTATE(	GL_LIGHT0,						gls.light0);
	PRINTSTATE(	GL_LIGHTING,					gls.lighting);
	PRINTSTATEF4(GL_LIGHT_MODEL_AMBIENT,		gls.lightmodelambient);
	PRINTSTATE(	GL_MATRIX_MODE,					gls.matrixmode);
	PRINTSTATE(	GL_NORMAL_ARRAY,				gls.normalarray_enabled);
	PRINTSTATE(	GL_NORMAL_ARRAY_BUFFER_BINDING_ARB,	gls.normalarraybufferbinding);
	PRINTSTATE(	GL_NORMAL_ARRAY_TYPE,			gls.normalarray_type);
	PRINTSTATE(	GL_NORMAL_ARRAY_POINTER,		PTR_TO_U32(gls.normalarray_pointer));
	PRINTSTATE(	GL_NORMALIZE,					gls.normalize);
	PRINTSTATE(	GL_PROJECTION_STACK_DEPTH,		gls.projectionstackdepth);
	PRINTSTATE(	GL_RENDER_MODE,					gls.rendermode);
	PRINTSTATE(	GL_RESCALE_NORMAL,				gls.rescalenormal);
	PRINTSTATE(	GL_RGBA_MODE,					gls.rgbamode);
	PRINTSTATE(	GL_SHADE_MODEL,					gls.shademodel);
	PRINTSTATE(	GL_STENCIL_BITS,				gls.stencilbits);
	PRINTSTATE(	GL_STENCIL_TEST,				gls.stenciltest);
	PRINTSTATE(	GL_STENCIL_VALUE_MASK,			gls.stencilvaluemask);
	PRINTSTATE(	GL_STENCIL_WRITEMASK,			gls.stencilwritemask);
	PRINTSTATE(	GL_TEXTURE_STACK_DEPTH,			gls.texturestackdepth);
	PRINTSTATE(	GL_VERTEX_ARRAY,				gls.vertexarray_enabled);
	PRINTSTATE(	GL_VERTEX_ARRAY_BUFFER_BINDING_ARB,	gls.vertexarraybufferbinding);
	PRINTSTATE(	GL_VERTEX_ARRAY_SIZE,			gls.vertexarray_size);
	PRINTSTATE(	GL_VERTEX_ARRAY_TYPE,			gls.vertexarray_type);
	PRINTSTATE(	GL_VERTEX_ARRAY_POINTER,		PTR_TO_U32(gls.vertexarray_pointer));
	PRINTSTATEF4(GL_VIEWPORT,					gls.viewport);

	fprintf(f, "\nLight\n");
	PRINTSTATEF4(GL_AMBIENT,				gls.ambient);
	PRINTSTATEF4(GL_DIFFUSE,				gls.diffuse);
	PRINTSTATEF4(GL_SPECULAR,				gls.specular);
	PRINTSTATEF4(GL_POSITION,				gls.position);

	for (i=0; i<ARRAY_SIZE(gls.texunit); i++) {
		fprintf(f, "\nTexture %d\n", i);
		PRINTSTATE(	GL_TEXTURE_2D,					gls.texunit[i].texture2d);
		PRINTSTATE(	GL_TEXTURE_BINDING_2D,			gls.texunit[i].texturebinding2d);
		PRINTSTATE(	GL_TEXTURE_COORD_ARRAY,			gls.texunit[i].texturecoordarray_enabled);
		PRINTSTATE(	GL_TEXTURE_COORD_ARRAY_BUFFER_BINDING_ARB,	gls.texunit[i].texturecoordarraybufferbinding);
		PRINTSTATE(	GL_TEXTURE_COORD_ARRAY_SIZE,	gls.texunit[i].texturecoordarray_size);
		PRINTSTATE(	GL_TEXTURE_COORD_ARRAY_TYPE,	gls.texunit[i].texturecoordarray_type);
		PRINTSTATE( GL_TEXTURE_COORD_ARRAY_POINTER, PTR_TO_U32(gls.texunit[i].texturecoordarray_pointer));
		PRINTSTATEF4( GL_TEXTURE_MATRIX, gls.texunit[i].mat[0] );
		PRINTSTATEF4( GL_TEXTURE_MATRIX, gls.texunit[i].mat[1] );
		PRINTSTATEF4( GL_TEXTURE_MATRIX, gls.texunit[i].mat[2] );
		PRINTSTATEF4( GL_TEXTURE_MATRIX, gls.texunit[i].mat[3] );
	}

	fprintf(f, "\n");
	PRINTSTATEF4( GL_MODELVIEW_MATRIX, gls.modelviewmatrix[0] );
	PRINTSTATEF4( GL_MODELVIEW_MATRIX, gls.modelviewmatrix[1] );
	PRINTSTATEF4( GL_MODELVIEW_MATRIX, gls.modelviewmatrix[2] );
	PRINTSTATEF4( GL_MODELVIEW_MATRIX, gls.modelviewmatrix[3] );

	fprintf(f, "\n");
	PRINTSTATEF4( GL_PROJECTION_MATRIX, gls.projectionmatrix[0] );
	PRINTSTATEF4( GL_PROJECTION_MATRIX, gls.projectionmatrix[1] );
	PRINTSTATEF4( GL_PROJECTION_MATRIX, gls.projectionmatrix[2] );
	PRINTSTATEF4( GL_PROJECTION_MATRIX, gls.projectionmatrix[3] );

#undef PRINTSTATE
#undef PRINTSTATEF
#undef PREINTSTATEF4

	fclose(f);
}

// convenience function for the command window
void recordGlState0()
{
	recordGlState(NULL);
}


struct {
	int val;
	char *name;
} gl_errors[] = {
#define PAIR(x) {x, #x},
	PAIR(GL_NO_ERROR)
		PAIR(GL_INVALID_ENUM)
		PAIR(GL_INVALID_VALUE)
		PAIR(GL_INVALID_OPERATION)
		PAIR(GL_STACK_OVERFLOW)
		PAIR(GL_STACK_UNDERFLOW)
		PAIR(GL_OUT_OF_MEMORY)
		PAIR(GL_FRAMEBUFFER_COMPLETE_EXT                       )
		PAIR(GL_FRAMEBUFFER_INCOMPLETE_ATTACHMENT_EXT          )
		PAIR(GL_FRAMEBUFFER_INCOMPLETE_MISSING_ATTACHMENT_EXT  )
		PAIR(GL_FRAMEBUFFER_INCOMPLETE_DUPLICATE_ATTACHMENT_EXT)
		PAIR(GL_FRAMEBUFFER_INCOMPLETE_DIMENSIONS_EXT          )
		PAIR(GL_FRAMEBUFFER_INCOMPLETE_FORMATS_EXT             )
		PAIR(GL_FRAMEBUFFER_INCOMPLETE_DRAW_BUFFER_EXT         )
		PAIR(GL_FRAMEBUFFER_INCOMPLETE_READ_BUFFER_EXT         )
		PAIR(GL_FRAMEBUFFER_UNSUPPORTED_EXT                    )
		PAIR(GL_FRAMEBUFFER_STATUS_ERROR_EXT                   )
#undef PAIR
};

char *glErrorFromInt(int v)
{
	int i;
	for (i=0; i<ARRAY_SIZE(gl_errors); i++) {
		if (gl_errors[i].val == v)
			return gl_errors[i].name;
	}
	return "Unknown";
}

//Remember, you can't put checkgl in between glBegin and glEnd
//Also, minimizing the game causes glerror to freak out, I don't know why
void checkgl(char *fname,int line)
{
	GLenum err = glGetError();

	//return;
	if ( GL_NO_ERROR != err )
	{
		/* ErrorCodes */
		//#define GL_INVALID_ENUM                   0x0500 (1280)
		//#define GL_INVALID_VALUE                  0x0501 (1281)
		//#define GL_INVALID_OPERATION              0x0502 (1282)
		//#define GL_STACK_OVERFLOW                 0x0503 (1283)
		//#define GL_STACK_UNDERFLOW                0x0504 (1284)
		//#define GL_OUT_OF_MEMORY                  0x0505 (1285)
		Errorf( "GL! %s:%d (0x%x) %s",fname,line,err, glErrorFromInt(err));
	}

	{
		//		GLuint t;
		//		glActiveTextureARB(GL_TEXTURE1_ARB);
		//		glGetIntegerv(GL_TEXTURE_BINDING_2D, &t);
		//		if(t == 0)
		//			printf("");
	}
	if(0)
		getGlState(); //slow but exhaustive
}

int glge(void)
{
	int error = glGetError();
	// Convenience function for running in debugger
	OutputDebugStringf("%d: %s\n", error, glErrorFromInt(error));
	return error;
}

