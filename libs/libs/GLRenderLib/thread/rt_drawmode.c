#include "rt_drawmode.h"
#include "rt_state.h"
#include "RdrDrawable.h"

#define SHADER_PATH "shaders/"

enum
{
	VP_SPRITE,
	VP_POSTPROCESS,
	VP_TERRAIN,
	VP_TERRAIN_SHADOWRENDER,
	VP_NORMAL,
};

static RwglProgramDef vertexPrograms[] = 
{
	{ SHADER_PATH "wingl/sprite.vp", { 0 } },
	{ SHADER_PATH "wingl/sprite.vp", { "POSTPROCESSING", 0 } },
	{ SHADER_PATH "wingl/terrain.vp", { 0 } },
	{ SHADER_PATH "wingl/terrain.vp", { "SHADOWMAP_RENDER", 0 } },
	{ SHADER_PATH "wingl/normal.vp", { 0 } },
	{ SHADER_PATH "wingl/normal.vp", { "HAS_SKIN", 0 } },
	{ SHADER_PATH "wingl/normal.vp", { "SHADOWMAP_RENDER", 0 } },
	{ SHADER_PATH "wingl/normal.vp", { "SHADOWMAP_RENDER", "HAS_SKIN", 0 } },
};

//////////////////////////////////////////////////////////////////////////

__forceinline static void initVPs(RdrDeviceWinGL *device, int force_reload)
{
	int first_time = 1;

	if (device->vertex_programs)
		first_time = 0;
	else
		device->vertex_programs = malloc(ARRAY_SIZE(vertexPrograms) * sizeof(*device->vertex_programs));

	if (force_reload || first_time)
		rwglLoadPrograms(device, GLC_VERTEX_PROG, vertexPrograms, device->vertex_programs, ARRAY_SIZE(vertexPrograms), first_time);
}

void rwglSetupPrimitiveDrawMode(RdrDeviceWinGL *device)
{
	CHECKDEVICELOCK(device);

	// vertex data arrays
	rwglEnableArrays(0);
	rwglBindVBO(GL_ELEMENT_ARRAY_BUFFER_ARB, 0);
	rwglBindVBO(GL_ARRAY_BUFFER_ARB, 0);

	// vertex program
	initVPs(device, 0);
	rwglEnableProgram(GLC_VERTEX_PROG, 1);
	rwglBindProgram(GLC_VERTEX_PROG, device->vertex_programs[VP_SPRITE]);
}

void rwglSetupPostProcessDrawMode(RdrDeviceWinGL *device, int is_shape)
{
	CHECKDEVICELOCK(device);

	// vertex data arrays
	rwglEnableArrays(is_shape?GLC_VERTEX_ARRAY:0);
	rwglBindVBO(GL_ELEMENT_ARRAY_BUFFER_ARB, 0);
	rwglBindVBO(GL_ARRAY_BUFFER_ARB, 0);

	// vertex program
	initVPs(device, 0);
	rwglEnableProgram(GLC_VERTEX_PROG, 1);
	rwglBindProgram(GLC_VERTEX_PROG, device->vertex_programs[VP_POSTPROCESS]);
}

void rwglSetupSpriteDrawMode(RdrDeviceWinGL *device)
{
	CHECKDEVICELOCK(device);

	// vertex data arrays
	rwglEnableArrays(GLC_VERTEX_ARRAY | GLC_COLOR_ARRAY | GLC_TEXTURE_COORD_ARRAY_0);
	rwglBindVBO(GL_ELEMENT_ARRAY_BUFFER_ARB, 0);
	rwglBindVBO(GL_ARRAY_BUFFER_ARB, 0);

	// vertex program
	initVPs(device, 0);
	rwglEnableProgram(GLC_VERTEX_PROG, 1);
	rwglBindProgram(GLC_VERTEX_PROG, device->vertex_programs[VP_SPRITE]);
}

void rwglSetupParticleDrawMode(RdrDeviceWinGL *device)
{
	CHECKDEVICELOCK(device);

	// vertex data arrays
	rwglEnableArrays(GLC_VERTEX_ARRAY | GLC_COLOR_ARRAY | GLC_TEXTURE_COORD_ARRAY_0);
	rwglBindVBO(GL_ELEMENT_ARRAY_BUFFER_ARB, 0);
	rwglBindVBO(GL_ARRAY_BUFFER_ARB, 0);

	// vertex program
	initVPs(device, 0);
	rwglEnableProgram(GLC_VERTEX_PROG, 1);
	rwglBindProgram(GLC_VERTEX_PROG, device->vertex_programs[VP_SPRITE]);
}

void rwglSetupTerrainDrawMode(RdrDeviceWinGL *device, int shadowmap_render)
{
	CHECKDEVICELOCK(device);

	// vertex data arrays
	rwglEnableArrays(GLC_TEXTURE_COORD_ARRAY_0);

	// vertex program
	initVPs(device, 0);
	rwglEnableProgram(GLC_VERTEX_PROG, 1);
	rwglBindProgram(GLC_VERTEX_PROG, device->vertex_programs[shadowmap_render?VP_TERRAIN_SHADOWRENDER:VP_TERRAIN]);
}

void rwglSetupNormalDrawMode(RdrDeviceWinGL *device, DrawModeBits bits)
{
	int arrays, vp_idx;

	CHECKDEVICELOCK(device);

	// vertex data arrays
	arrays = GLC_VERTEX_ARRAY | GLC_NORMAL_ARRAY | GLC_TEXTURE_COORD_ARRAY_0;
	if (bits & DRAWBIT_SKINNED)
		arrays |= GLC_BONE_IDXS_ARRAY | GLC_BONE_WEIGHTS_ARRAY;
	if (1)
		arrays |= GLC_TANGENT_ARRAY | GLC_BINORMAL_ARRAY;
	rwglEnableArrays(arrays);

	// vertex program
	initVPs(device, 0);
	rwglEnableProgram(GLC_VERTEX_PROG, 1);
	vp_idx = VP_NORMAL;
	if (bits & DRAWBIT_SKINNED)
		vp_idx += 1;
	if (bits & DRAWBIT_SHADOWRENDER)
		vp_idx += 2;
	rwglBindProgram(GLC_VERTEX_PROG, device->vertex_programs[vp_idx]);
}

//////////////////////////////////////////////////////////////////////////

void rwglSetupSkinning(RdrDeviceWinGL *device, int bone_count, SkinningMat4 bone_infos[])
{
	int j, k;
	Vec4 bonevec;

	CHECKDEVICELOCK(device);

	assert(bone_count <= 16);
	for (j = 0; j < bone_count; j++)
	{
		Mat4Ptr mp = bone_infos[j];
		for (k = 0; k < 3; k++)     
		{
			setVec4(bonevec, mp[0][k], mp[1][k], mp[2][k], mp[3][k]);
			rwglProgramLocalParameter(GLC_VERTEX_PROG, 16 + j * 3 + k, bonevec);
		}
//		rwglProgramLocalParameter(GLC_VERTEX_PROG, 16 + j*4 + 3, bi->scale);
	}
}

//////////////////////////////////////////////////////////////////////////
//////////////////////////////////////////////////////////////////////////

// TODO move this when new shader tech is implemented

static RwglProgramDef fragmentPrograms[] = 
{
	{ SHADER_PATH "wingl/default.fp", { 0 } },
};

__forceinline static void initFPs(RdrDeviceWinGL *device, int force_reload)
{
	int first_time = 1;
	if (device->fragment_programs)
		first_time = 0;
	else
		device->fragment_programs = malloc(ARRAY_SIZE(fragmentPrograms) * sizeof(*device->fragment_programs));

	if (force_reload || first_time)
		rwglLoadPrograms(device, GLC_FRAGMENT_PROG, fragmentPrograms, device->fragment_programs, ARRAY_SIZE(fragmentPrograms), first_time);
}


void rwglSetDefaultBlendMode(RdrDeviceWinGL *device)
{
	CHECKDEVICELOCK(device);

	initFPs(device, 0);
	rwglEnableProgram(GLC_FRAGMENT_PROG, 1);
	rwglBindProgram(GLC_FRAGMENT_PROG, device->fragment_programs[0]);
}

void rwglBindBlendModeTextures(RdrDeviceWinGL *device, TexHandle *textures, U32 tex_count)
{
	CHECKDEVICELOCK(device);

	// TODO this is for default blend mode only

	if (!tex_count)
		rwglBindTexture(GL_TEXTURE_2D, 0, 0);
	else
		rwglBindTexture(GL_TEXTURE_2D, 0, textures[0]);
	rwglTexLODBias(0, -0.5);
}

void rwglBindMaterial(RdrDeviceWinGL *device, RdrMaterial *rdr_material, ShaderHandle handle)
{
	unsigned int i;
	CHECKDEVICELOCK(device);
	// TODO: State management

	if (!handle) {
		rwglSetDefaultBlendMode(device);
		rwglBindBlendModeTextures(device, rdr_material->textures, rdr_material->tex_count);
	} else {
		rwglEnableProgram(GLC_FRAGMENT_PROG, 1);
		rwglBindProgram(GLC_FRAGMENT_PROG, handle);
		for (i=0; i<rdr_material->tex_count; i++) {
			rwglBindTexture(GL_TEXTURE_2D, i, rdr_material->textures[i]);
		}
		for (i=0; i<rdr_material->const_count; i++) {
			rwglProgramLocalParameter(GLC_FRAGMENT_PROG, i, rdr_material->constants[i]);
		}
	}
}

//////////////////////////////////////////////////////////////////////////

void rwglReloadDefaultShadersDirect(RdrDeviceWinGL *device)
{
	stashTableDestroy(device->lpc_crctable);
	device->lpc_crctable = 0;
	initVPs(device, 1);
	initFPs(device, 1);
}
