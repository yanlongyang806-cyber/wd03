#include "RenderLib.h"

#include "rt_xdrawmode.h"
#include "rt_xgeo.h"
//#include "rt_xshader.h"
//#include "rt_xshaderdata.h"
#include "rt_xtextures.h"
#include "rt_xStateEnums.h"
#include "RdrLightAssembly.h"
#include "RdrState.h"
#include "utils.h"

AUTO_RUN_ANON(memBudgetAddMapping(__FILE__, BUDGET_Renderer););

RxbxProgramDef hullShaderDefs[] =
{
	{SHADER_PATH "error_hull_shader.hhl", "error_hullshader", {"TESSELLATION", 0} },
	{SHADER_PATH "standard_hull_shader.hhl", "standardHullShader", {"TESSELLATION", "PN_TRIANGLES", 0} },
	{SHADER_PATH "standard_hull_shader.hhl", "standardHullShader", {"TESSELLATION", "HAS_HEIGHTMAP", "IGNORE_FACING", 0} },
};

RxbxProgramDef domainShaderDefs[] =
{
	{SHADER_PATH "error_domain_shader.dhl", "error_domainshader", {"TESSELLATION", 0} },
	{SHADER_PATH "standard_domain_shader.dhl", "standardDomainShader", {"TESSELLATION", "PN_TRIANGLES", 0} },
	{SHADER_PATH "standard_domain_shader.dhl", "standardDomainShader", {"TESSELLATION", "HAS_HEIGHTMAP", 0} },
};

/*static*/ RxbxProgramDef vertexShaderDefsMinimal[] = 
{
	// The minimal vertex shaders, loaded from rt_xshaderdata.c
	{ NULL, "error_vertexshader" },
	{ NULL, "sprite_vertexshader" },
	{ NULL, "sprite_vertexshader_srgb" },
	{ NULL, "primitive_vertexshader" },
	{ NULL, "primitive_vertexshader_srgb" },
};
STATIC_ASSERT(ARRAY_SIZE(vertexShaderDefsMinimal) == VS_MINIMAL_MAX);

/*static*/ RxbxProgramDef vertexShaderDefsSpecial[] = 
{
	// The normal vertex shaders
	{ SHADER_PATH "sprite.vhl", "sprite_vertexshader", { "POSTPROCESSING", "NO_TEXCOORD", 0 } },
	{ SHADER_PATH "sprite.vhl", "sprite_vertexshader", { "POSTPROCESSING", "NO_TEXCOORD", "NO_OFFSET", 0 } },
	{ SHADER_PATH "sprite.vhl", "sprite_vertexshader", { "POSTPROCESSING", "NO_TEXCOORD", 0 } },
	{ SHADER_PATH "sprite.vhl", "sprite_vertexshader", { "POSTPROCESSING", "NO_TEXCOORD", "NO_OFFSET", 0 } },

	{ SHADER_PATH "normal.vhl", "normal_vertexshader", { "PARTICLE", "HAS_VARIABLE_COLOR", "TIGHTEN_UP", 0 } },
	{ SHADER_PATH "normal.vhl", "normal_vertexshader", { "PARTICLE", "HAS_VARIABLE_COLOR", "TIGHTEN_UP", "NO_NORMALMAP", 0 } },
	{ SHADER_PATH "normal.vhl", "normal_vertexshader", { "HAS_VARIABLE_COLOR", "CYLINDER_TRAIL", "NO_TEXCOORD", 0 } },

	{ SHADER_PATH "normal.vhl", "normal_vertexshader", { "PARTICLE", "HAS_VARIABLE_COLOR", "TIGHTEN_UP", "FAR_DEPTH_RANGE", 0 } },
	{ SHADER_PATH "normal.vhl", "normal_vertexshader", { "PARTICLE", "HAS_VARIABLE_COLOR", "TIGHTEN_UP", "NO_NORMALMAP", "FAR_DEPTH_RANGE", 0 } },
	{ SHADER_PATH "normal.vhl", "normal_vertexshader", { "HAS_VARIABLE_COLOR", "CYLINDER_TRAIL", "NO_TEXCOORD", "FAR_DEPTH_RANGE", 0 } },

	{ SHADER_PATH "fast_particles.vhl", "fastparticle_cpu_vertexshader", { "FAST_PARTICLE", "CPU", 0 } },
	{ SHADER_PATH "fast_particles.vhl", "fastparticle_cpu_vertexshader", { "FAST_PARTICLE", "CPU", "FAR_DEPTH_RANGE", 0 } },

	{ SHADER_PATH "fast_particles.vhl", "fastparticle_vertexshader", { "FAST_PARTICLE", 0 } },
	{ SHADER_PATH "fast_particles.vhl", "fastparticle_vertexshader", { "FAST_PARTICLE", "LINKSCALE", 0 } },
	{ SHADER_PATH "fast_particles.vhl", "fastparticle_vertexshader", { "FAST_PARTICLE", "STREAK", 0 } },

	{ SHADER_PATH "fast_particles.vhl", "fastparticle_vertexshader", { "FAST_PARTICLE", "FAR_DEPTH_RANGE", 0 } },
	{ SHADER_PATH "fast_particles.vhl", "fastparticle_vertexshader", { "FAST_PARTICLE", "LINKSCALE", "FAR_DEPTH_RANGE", 0 } },
	{ SHADER_PATH "fast_particles.vhl", "fastparticle_vertexshader", { "FAST_PARTICLE", "STREAK", "FAR_DEPTH_RANGE", 0 } },

	{ SHADER_PATH "fast_particles.vhl", "fastparticle_vertexshader", { "FAST_PARTICLE", "RGB_BLEND", 0 } },
	{ SHADER_PATH "fast_particles.vhl", "fastparticle_vertexshader", { "FAST_PARTICLE", "LINKSCALE", "RGB_BLEND", 0 } },
	{ SHADER_PATH "fast_particles.vhl", "fastparticle_vertexshader", { "FAST_PARTICLE", "STREAK", "RGB_BLEND", 0 } },

	{ SHADER_PATH "fast_particles.vhl", "fastparticle_vertexshader", { "FAST_PARTICLE", "FAR_DEPTH_RANGE", "RGB_BLEND", 0 } },
	{ SHADER_PATH "fast_particles.vhl", "fastparticle_vertexshader", { "FAST_PARTICLE", "LINKSCALE", "FAR_DEPTH_RANGE", "RGB_BLEND", 0 } },
	{ SHADER_PATH "fast_particles.vhl", "fastparticle_vertexshader", { "FAST_PARTICLE", "STREAK", "FAR_DEPTH_RANGE", "RGB_BLEND", 0 } },

	{ SHADER_PATH "fast_particles.vhl", "fastparticle_vertexshader", { "FAST_PARTICLE", "ANIMATEDTEXTURE", 0 } },
	{ SHADER_PATH "fast_particles.vhl", "fastparticle_vertexshader", { "FAST_PARTICLE", "LINKSCALE", "ANIMATEDTEXTURE", 0 } },
	{ SHADER_PATH "fast_particles.vhl", "fastparticle_vertexshader", { "FAST_PARTICLE", "STREAK", "ANIMATEDTEXTURE", 0 } },

	{ SHADER_PATH "fast_particles.vhl", "fastparticle_vertexshader", { "FAST_PARTICLE", "FAR_DEPTH_RANGE", "ANIMATEDTEXTURE", 0 } },
	{ SHADER_PATH "fast_particles.vhl", "fastparticle_vertexshader", { "FAST_PARTICLE", "LINKSCALE", "FAR_DEPTH_RANGE", "ANIMATEDTEXTURE", 0 } },
	{ SHADER_PATH "fast_particles.vhl", "fastparticle_vertexshader", { "FAST_PARTICLE", "STREAK", "FAR_DEPTH_RANGE", "ANIMATEDTEXTURE", 0 } },

	{ SHADER_PATH "fast_particles.vhl", "fastparticle_vertexshader", { "FAST_PARTICLE", "RGB_BLEND", "ANIMATEDTEXTURE", 0 } },
	{ SHADER_PATH "fast_particles.vhl", "fastparticle_vertexshader", { "FAST_PARTICLE", "LINKSCALE", "RGB_BLEND", "ANIMATEDTEXTURE", 0 } },
	{ SHADER_PATH "fast_particles.vhl", "fastparticle_vertexshader", { "FAST_PARTICLE", "STREAK", "RGB_BLEND", "ANIMATEDTEXTURE", 0 } },

	{ SHADER_PATH "fast_particles.vhl", "fastparticle_vertexshader", { "FAST_PARTICLE", "FAR_DEPTH_RANGE", "RGB_BLEND", "ANIMATEDTEXTURE", 0 } },
	{ SHADER_PATH "fast_particles.vhl", "fastparticle_vertexshader", { "FAST_PARTICLE", "LINKSCALE", "FAR_DEPTH_RANGE", "RGB_BLEND", "ANIMATEDTEXTURE", 0 } },
	{ SHADER_PATH "fast_particles.vhl", "fastparticle_vertexshader", { "FAST_PARTICLE", "STREAK", "FAR_DEPTH_RANGE", "RGB_BLEND", "ANIMATEDTEXTURE", 0 } },

	{ SHADER_PATH "postprocess.vhl", "postprocess_vertexshader", { "IS_POSTPROCESS", 0 } },

	{ SHADER_PATH "starfield.vhl", "starfield_vertexshader", { "NO_NORMALMAP", "SINGLE_DIRLIGHT", 0 } },
	{ SHADER_PATH "starfield.vhl", "starfield_vertexshader", { "NO_NORMALMAP", "SINGLE_DIRLIGHT", "CAMERA_FACING", 0 } },

	{ SHADER_PATH "starfield.vhl", "starfield_vertexshader", { "NO_NORMALMAP", "VERTEX_ONLY_LIGHTING", 0 } },
	{ SHADER_PATH "starfield.vhl", "starfield_vertexshader", { "NO_NORMALMAP", "VERTEX_ONLY_LIGHTING", "CAMERA_FACING", 0 } },
};
STATIC_ASSERT(ARRAY_SIZE(vertexShaderDefsSpecial) == VS_SPECIAL_MAX);

#define DEBUG_TRACE_SHADER 0

#if DEBUG_TRACE_SHADER
#define SHADER_LOG OutputDebugStringf
#else
#define SHADER_LOG( ... )
#endif

//////////////////////////////////////////////////////////////////////////

static void initPixelShaders(RdrDeviceDX *device, int force_reload, bool only_minimal);
static int initHullShaders(RdrDeviceDX *device, bool force_reload);
static int initDomainShaders(RdrDeviceDX *device, bool force_reload);

RxbxProgramDef *initHullShaderDef(RdrDeviceDX *device)
{
	static RxbxProgramDef static_def = {0};
	RxbxProgramDef *def = &static_def;

	ZeroStruct(def);
	def->filename = SHADER_PATH "standard_hull_shader.hhl";
	def->entry_funcname = "standardHullShader";
	return def;
}

RxbxProgramDef *initDomainShaderDef(RdrDeviceDX *device)
{
	static RxbxProgramDef static_def = {0};
	RxbxProgramDef *def = &static_def;

	ZeroStruct(def);
	def->filename = SHADER_PATH "standardDomainShader.dhl";
	def->entry_funcname = "standardDomainShader";
	return def;
}

/*static*/ RxbxProgramDef *initVertexShaderDef(RdrDeviceDX *device, int shader_num)
{
	static RxbxProgramDef static_def = {0};
	RxbxProgramDef *def = &static_def;
	int i = shader_num & (DRAWBIT_MAX-1);
	int k = 0;
	int j = 0;

	for (shader_num -= DRAWBIT_MAX; shader_num > 0; ++k)
		shader_num -= DRAWBIT_MAX;

	assert(k < VS_STANDARD_MAX);

	ZeroStruct(def);

	//////////////////////////////////////////////////////////////////////////
	// vertex shader type:
	switch (k)
	{
		xcase VS_NORMAL:
			def->filename = SHADER_PATH "normal.vhl";
			def->entry_funcname = "normal_vertexshader";
			
		xcase VS_TERRAIN:
			def->filename = SHADER_PATH "terrain_heightmap.vhl";
			def->entry_funcname = "terrain_heightmap_vertexshader";

			// not supported:
			if (i & (DRAWBIT_BEND|DRAWBIT_SKINNED|DRAWBIT_VERTEX_LIGHT
                |DRAWBIT_WORLD_TEX_COORDS|DRAWBIT_SCREEN_TEX_COORDS
                |DRAWBIT_INSTANCED|DRAWBIT_ALPHA_FADE_PLANE|DRAWBIT_SKINNED_ONLY_TWO_BONES
                |DRAWBIT_WIND|DRAWBIT_TRUNK_WIND))
				def->skip_me = true;

		xdefault:
			assert(0);
	}

	//////////////////////////////////////////////////////////////////////////
	// geometry changing bits:
	if (i & DRAWBIT_TESSELLATION) {
		def->defines[j++] = "TESSELLATION";
		if (i & (DRAWBIT_DEPTHONLY|DRAWBIT_NOPIXELSHADER))
			def->skip_me = true;
	}
	if (i & DRAWBIT_BEND)
	{
		def->defines[j++] = "HAS_BEND";
		if (i & (DRAWBIT_SKINNED|DRAWBIT_MORPH|DRAWBIT_INSTANCED|DRAWBIT_ALPHA_FADE_PLANE|DRAWBIT_WIND|DRAWBIT_TRUNK_WIND|DRAWBIT_SKINNED_ONLY_TWO_BONES))
			def->skip_me = true;
	}
	else if (i & DRAWBIT_SKINNED)
	{
		def->defines[j++] = "HAS_SKIN";
		if (i & (DRAWBIT_MORPH|DRAWBIT_INSTANCED|DRAWBIT_ALPHA_FADE_PLANE|DRAWBIT_WIND|DRAWBIT_TRUNK_WIND))
			def->skip_me = true;
		if (i & DRAWBIT_SKINNED_ONLY_TWO_BONES)
			def->defines[j++] = "TWO_BONE_SKINNING";
	}
	else if (i & DRAWBIT_SKINNED_ONLY_TWO_BONES)
	{
		// Only valid if we also have DRAWBIT_SKINNED
		def->skip_me = true;
	}
	else if (i & DRAWBIT_MORPH)
	{
		def->defines[j++] = "HAS_MORPH";
		if (i & (DRAWBIT_INSTANCED|DRAWBIT_ALPHA_FADE_PLANE|DRAWBIT_WIND|DRAWBIT_TRUNK_WIND))
			def->skip_me = true;
	}
	else if (i & DRAWBIT_INSTANCED)
	{
		def->defines[j++] = "IS_INSTANCED";
		if (i & DRAWBIT_WIND)
		{
			if (i & DRAWBIT_TRUNK_WIND)
				def->skip_me = true; //cant have both at the same time
			else if (i & DRAWBIT_VERTEX_COLORS)
				def->defines[j++] = "HAS_WIND";
			else
				def->skip_me = true;
		}
		else if (i & DRAWBIT_TRUNK_WIND)
		{
			def->defines[j++] = "HAS_TRUNK_WIND";
		}
		if (i & DRAWBIT_ALPHA_FADE_PLANE)
			def->skip_me = true;
	}
	else if (i & DRAWBIT_WIND)
	{
		if (i & DRAWBIT_TRUNK_WIND)
			def->skip_me = true; //cant have both at the same time
		else if (i & DRAWBIT_VERTEX_COLORS)
			def->defines[j++] = "HAS_WIND";
		else
			def->skip_me = true;
		if (i & DRAWBIT_ALPHA_FADE_PLANE)
			def->skip_me = true;
	}
	else if (i & DRAWBIT_TRUNK_WIND)
	{
		def->defines[j++] = "HAS_TRUNK_WIND";
	}

	if (!def->skip_me)
	{
		if (i & DRAWBIT_WORLD_TEX_COORDS)
		{
			if (i & (DRAWBIT_SCREEN_TEX_COORDS|DRAWBIT_VS_TEXCOORD_SPLAT))
				def->skip_me = true;
			def->defines[j++] = "WORLD_TEX_COORDS";
		}

		if (i & DRAWBIT_SCREEN_TEX_COORDS)
		{
			if (i & DRAWBIT_VS_TEXCOORD_SPLAT)
				def->skip_me = true;
			else
				def->defines[j++] = "SCREEN_TEX_COORDS";
		}

		if (i & DRAWBIT_ALPHA_FADE_PLANE)
		{
			if (i & DRAWBIT_DEPTHONLY)
				def->skip_me = true;
			else
				def->defines[j++] = "ALPHA_FADE_PLANE";
		}

		if (i & DRAWBIT_VS_TEXCOORD_SPLAT)
		{
			if (i & DRAWBIT_DEPTHONLY)
				def->skip_me = true;
			else
				def->defines[j++] = "VS_TEXCOORD_SPLAT";
		}

		//////////////////////////////////////////////////////////////////////////
		// pixel changing bits:
		if (i & DRAWBIT_NOPIXELSHADER)
		{
			if (i & (DRAWBIT_VERTEX_LIGHT|DRAWBIT_DEPTHONLY|DRAWBIT_SINGLE_DIRLIGHT|DRAWBIT_VERTEX_ONLY_LIGHTING|DRAWBIT_VERTEX_ONLY_LIGHTING_ONLY_ONE_LIGHT))
				def->skip_me = true;
			else {
				def->defines[j++] = "NOPIXELSHADER";
				def->defines[j++] = "DEPTH_ONLY";
				if (i & DRAWBIT_VERTEX_COLORS)
				{
					def->defines[j++] = "HAS_VERTEX_COLOR";
				}
				if (i & DRAWBIT_NO_NORMALMAP)
				{
					def->defines[j++] = "NO_NORMALMAP";
				}
				if (i & DRAWBIT_NO_NORMAL_NO_TEXCOORD)
				{
					def->defines[j++] = "NO_NORMAL_NO_TEXCOORD";
				}
			}
		}
		else if (i & DRAWBIT_DEPTHONLY)
		{
			if (i & (DRAWBIT_VERTEX_LIGHT|DRAWBIT_SINGLE_DIRLIGHT|DRAWBIT_VERTEX_ONLY_LIGHTING|DRAWBIT_VERTEX_ONLY_LIGHTING_ONLY_ONE_LIGHT))
				def->skip_me = true;
			else {
				def->defines[j++] = "DEPTH_ONLY";
				if (i & DRAWBIT_VERTEX_COLORS)
				{
					def->defines[j++] = "HAS_VERTEX_COLOR";
				}
				if (i & DRAWBIT_NO_NORMALMAP)
				{
					def->defines[j++] = "NO_NORMALMAP";
				}
				if (i & DRAWBIT_NO_NORMAL_NO_TEXCOORD)
				{
					def->defines[j++] = "NO_NORMAL_NO_TEXCOORD";
				}
			}
		}
		else
		{
			if (i & DRAWBIT_SINGLE_DIRLIGHT)
			{
				if (i & DRAWBIT_VERTEX_ONLY_LIGHTING)
					def->skip_me = true;
				else
					def->defines[j++] = "SINGLE_DIRLIGHT";
			}
			if (i & DRAWBIT_VERTEX_LIGHT)
			{
				def->defines[j++] = "HAS_VERTEX_LIGHT";
			}
			if (i & DRAWBIT_VERTEX_COLORS)
			{
				def->defines[j++] = "HAS_VERTEX_COLOR";
			}
			if (i & (DRAWBIT_NO_NORMALMAP | DRAWBIT_VS_TEXCOORD_SPLAT))
			{
				def->defines[j++] = "NO_NORMALMAP";
			}
			if (i & DRAWBIT_NO_NORMAL_NO_TEXCOORD)
			{
				def->defines[j++] = "NO_NORMAL_NO_TEXCOORD";
			}
			if (i & DRAWBIT_VERTEX_ONLY_LIGHTING)
			{
				if (!(i & DRAWBIT_NO_NORMALMAP))
					def->skip_me = true;
				else {
					def->defines[j++] = "VERTEX_ONLY_LIGHTING";
					if (i & DRAWBIT_VERTEX_ONLY_LIGHTING_ONLY_ONE_LIGHT)
						def->defines[j++] = "ONE_VERTEX_LIGHT";
				}
			} else if (i & DRAWBIT_VERTEX_ONLY_LIGHTING_ONLY_ONE_LIGHT)
				def->skip_me = true;
		}
	}

	assert(j<ARRAY_SIZE(def->defines));

	def->defines[j] = NULL;

	if (def->skip_me)
	{
		return NULL;
	}
	return def;
}

__forceinline static char* genConCat(const char* prefix, const char* suffix)
{
	char* conCattedString;
	size_t concatSize = strlen(suffix) + strlen(prefix) + 1;
	conCattedString = calloc(concatSize, sizeof(char));
	strcpy_unsafe(conCattedString,prefix);
	strcat_s(conCattedString, concatSize,suffix);
	return conCattedString;
}

static int initVertexShadersMinimalInternal(RdrDeviceDX *device)
{
	static const char* preloaded_text[] = {
		{dx_vshader_data_error},
		{dx_vshader_data_sprite},
		{dx_vshader_data_sprite_srgb},
		{dx_vshader_data_primitive},
		{dx_vshader_data_primitive_srgb},
	};
	char* assembled_text[ARRAY_SIZE(preloaded_text)];
	RxbxPreloadedShaderData assembled_vertex_shader[ARRAY_SIZE(preloaded_text)];
	int shader_success, i;

	for (i = 0; i < ARRAY_SIZE(preloaded_text); i++) {
		if (device->d3d11_device) {
			assembled_text[i] = genConCat(dx_11_vshader_prefix,preloaded_text[i]);
			assembled_vertex_shader[i].data = assembled_text[i];
		} else {
			assembled_text[i] = genConCat(dx_9_vshader_prefix,preloaded_text[i]);
			assembled_vertex_shader[i].data = assembled_text[i];
		}
	}

	if (!device->minimal_vertex_shaders)
	{
		device->minimal_vertex_shaders = calloc(VS_SPECIAL_MAX, sizeof(*device->minimal_vertex_shaders));
	}

	shader_success = rxbxLoadVertexShaders(device, vertexShaderDefsMinimal, device->minimal_vertex_shaders, VS_MINIMAL_MAX, assembled_vertex_shader, ARRAY_SIZE(preloaded_text));

	for (i = 0; i <  ARRAY_SIZE(preloaded_text); i++) {
		free(assembled_text[i]);
	}

	if (!shader_success)
	{
		int shader_index;
		for (shader_index = 0; shader_index < VS_MINIMAL_MAX; ++shader_index)
		{
			RxbxVertexShader * vertex_shader = NULL;
			if (stashIntFindPointer(device->vertex_shaders, device->minimal_vertex_shaders[shader_index], &vertex_shader))
				rxbxFreeVertexShaderInternal(device, device->minimal_vertex_shaders[shader_index], vertex_shader, false);
		}
		device->error_vertex_shader = NULL;
		free(device->minimal_vertex_shaders);
		device->minimal_vertex_shaders = NULL;
	}

	return shader_success;
}

static int initVertexShadersFullInternal(RdrDeviceDX *device)
{
	if (!device->special_vertex_shaders)
	{
		device->special_vertex_shaders = calloc(VS_SPECIAL_MAX, sizeof(*device->special_vertex_shaders));
	}

	return rxbxLoadVertexShaders(device, vertexShaderDefsSpecial, device->special_vertex_shaders, VS_SPECIAL_MAX, NULL, 0);
}

static struct  
{
	int count;
	RdrDeviceDX *device;
	struct {
		int shader_num;
		ShaderHandleAndFlags *handle;
	} cache[16];
} vertexShaderCache;

static ShaderHandleAndFlags *initSpecificVertexShader(RdrDeviceDX *device, int shader_num)
{
	bool bRet;
	ShaderHandleAndFlags *handleAndFlags;

	if (vertexShaderCache.device == device)
	{
		int i, j;
		for (i=0; i<vertexShaderCache.count; i++)
		{
			if (vertexShaderCache.cache[i].shader_num == shader_num)
			{
				ShaderHandleAndFlags *ret = vertexShaderCache.cache[i].handle;
				if (i!=0)
				{
					for (j=i; j>0; j--)
					{
						vertexShaderCache.cache[j] = vertexShaderCache.cache[j-1];
					}
					vertexShaderCache.cache[0].handle = ret;
					vertexShaderCache.cache[0].shader_num = shader_num;
				}
				assert(ret->loaded);
				return ret;
			}
		}
	} else {
		vertexShaderCache.count = 0;
		vertexShaderCache.device = device;
	}

	if (!device->standard_vertex_shaders_table)
	{
		device->standard_vertex_shaders_table = stashTableCreateInt(256);
	}

	if (!stashIntFindPointer(device->standard_vertex_shaders_table, shader_num+1, &handleAndFlags))
	{
		if (!device->standard_vertex_shaders_mem_left)
		{
#define SHADER_HANDLE_CHUNK_SIZE 64
			ShaderHandleAndFlags *newblock = calloc(sizeof(ShaderHandleAndFlags), SHADER_HANDLE_CHUNK_SIZE);
			eaInsert(&device->standard_vertex_shaders_mem, newblock, 0);
			device->standard_vertex_shaders_mem_left = SHADER_HANDLE_CHUNK_SIZE;
		}
		handleAndFlags = &device->standard_vertex_shaders_mem[0][--device->standard_vertex_shaders_mem_left];
		stashIntAddPointer(device->standard_vertex_shaders_table, shader_num+1, handleAndFlags, false);
	}
	assert(handleAndFlags);

	if (!handleAndFlags->loaded)
	{
		RxbxProgramDef *def = initVertexShaderDef(device, shader_num);
		assertmsg(def, "Attempted to use unsupported combination of DrawModeBits, either use a supported combination or unlock this combination for use in initVertexShaders().");
#if !_PS3
		if (!rdr_state.disableAsyncVshaderLoad)
        {
			rxbxLoadVertexShaderAsync(device, def, handleAndFlags);
			bRet = true;
		} else
#endif
		{
			ShaderHandle handle = handleAndFlags->handle;
			rxbxLoadVertexShaders(device, def, &handle, 1, NULL, 0);
			handleAndFlags->handle = handle;
			handleAndFlags->loaded = 1;
			bRet = true;
		}
	} else {
		// It's either loaded, or in the process of being loaded
	}

	{
		// Add to the cache
		int j;
		bRet = true;
		for (j=MIN(ARRAY_SIZE(vertexShaderCache.cache)-1, vertexShaderCache.count); j>0; j--)
		{
			vertexShaderCache.cache[j] = vertexShaderCache.cache[j-1];
		}
		vertexShaderCache.cache[0].handle = handleAndFlags;
		vertexShaderCache.cache[0].shader_num = shader_num;
		if (vertexShaderCache.count<ARRAY_SIZE(vertexShaderCache.cache))
			vertexShaderCache.count++;
	}
	return handleAndFlags;
}

__forceinline static int initVertexShaders(RdrDeviceDX *device, int force_reload, bool onlyMinimal)
{
	if( onlyMinimal ) {
		if (!device->minimal_vertex_shaders || force_reload) // Probably don't ever need to do a force reload, they can't change!
			return initVertexShadersMinimalInternal(device);
	} else {
		if (!device->special_vertex_shaders || force_reload)
			return initVertexShadersFullInternal(device);
	}
	return 1;
}

int rxbxCompileMinimalVertexShaders(RdrDeviceDX *device)
{
	CHECKDEVICELOCK(device);

	// vertex program
	return initVertexShaders(device, 0, true);
}

void rxbxCompileMinimalShaders(RdrDeviceDX *device)
{
	CHECKDEVICELOCK(device);

	initVertexShaders(device, 0, true);
	initPixelShaders(device, 0, true);
	if (device->d3d11_device && (device->rdr_caps_new.features_supported & FEATURE_TESSELLATION)) {
		initHullShaders(device, false);
		initDomainShaders(device, false);
	}
}

void rxbxSetupPrimitiveDrawMode(RdrDeviceDX *device)
{
	int srgb;

	CHECKDEVICELOCK(device);

	srgb = (device->primary_surface.buffer_types[0] & SBT_SRGB) != 0;

	rxbxBindVertexShader(device, device->minimal_vertex_shaders[VS_MINIMAL_PRIMITIVE + srgb]);
	assert(device->primitive_vertex_declaration.typeless_decl);
	rxbxSetVertexDeclaration(device, device->primitive_vertex_declaration);
}

void rxbxSetupPrimitiveVdecl(RdrDeviceDX *device, const VertexComponentInfo * components)
{
	int srgb;

	CHECKDEVICELOCK(device);

	srgb = (device->primary_surface.buffer_types[0] & SBT_SRGB) != 0;

	// vertex program
	initVertexShaders(device, 0, true);
	rxbxBindVertexShader(device, device->minimal_vertex_shaders[VS_MINIMAL_PRIMITIVE + srgb]);
	assert(!device->primitive_vertex_declaration.typeless_decl);
	rxbxCreateVertexDeclarationFromComponents(device, components, 0, device->device_state.active_vertex_shader_wrapper, &device->primitive_vertex_declaration);
}

void rxbxSetupPostProcessScreenDrawMode(RdrDeviceDX *device, const VertexComponentInfo * components, RdrVertexDeclarationObj * vertex_declaration, bool no_offset)
{
	CHECKDEVICELOCK(device);

	// vertex program
	initVertexShaders(device, 0, false);
	rxbxBindVertexShader(device, device->special_vertex_shaders[no_offset?VS_SPECIAL_POSTPROCESS_SCREEN_NO_OFFSET:VS_SPECIAL_POSTPROCESS_SCREEN]);
	if (!vertex_declaration->typeless_decl)
		rxbxCreateVertexDeclarationFromComponents(device, components, 0, device->device_state.active_vertex_shader_wrapper, vertex_declaration);
	rxbxSetVertexDeclaration(device, *vertex_declaration);
}

void rxbxSetupPostProcessShapeDrawMode(RdrDeviceDX *device, const VertexComponentInfo * components, RdrVertexDeclarationObj * vertex_declaration, bool no_offset)
{
	CHECKDEVICELOCK(device);

	// vertex program
	initVertexShaders(device, 0, false);
	rxbxBindVertexShader(device, device->special_vertex_shaders[no_offset?VS_SPECIAL_POSTPROCESS_SHAPE_NO_OFFSET:VS_SPECIAL_POSTPROCESS_SHAPE]);
	if (!vertex_declaration->typeless_decl)
		rxbxCreateVertexDeclarationFromComponents(device, components, 0, device->device_state.active_vertex_shader_wrapper, vertex_declaration);
	rxbxSetVertexDeclaration(device, *vertex_declaration);
}

void rxbxSetupSpriteDrawMode(RdrDeviceDX *device, RdrVertexDeclarationObj vertex_declaration)
{
	int srgb;

	CHECKDEVICELOCK(device);

	srgb = (device->primary_surface.buffer_types[0] & SBT_SRGB) != 0;

	// shader and vdecl should have been created
	assert(vertex_declaration.typeless_decl);
	rxbxBindVertexShader(device, device->minimal_vertex_shaders[VS_MINIMAL_SPRITE + srgb]);
	rxbxSetVertexDeclaration(device, vertex_declaration);
}

void rxbxSetupSpriteVdecl(RdrDeviceDX *device, const VertexComponentInfo * components, RdrVertexDeclarationObj * vertex_declaration)
{
	int srgb;

	CHECKDEVICELOCK(device);

	srgb = (device->primary_surface.buffer_types[0] & SBT_SRGB) != 0;

	initVertexShaders(device, 0, true);
	rxbxBindVertexShader(device, device->minimal_vertex_shaders[VS_MINIMAL_SPRITE + srgb]);
	assert(!vertex_declaration->typeless_decl);
	rxbxCreateVertexDeclarationFromComponents(device, components, 0, device->device_state.active_vertex_shader_wrapper, vertex_declaration);
}

void rxbxSetupParticleDrawMode(RdrDeviceDX *device, const VertexComponentInfo * components, RdrVertexDeclarationObj * vertex_declaration, bool no_normalmap, bool far_depth_range)
{
	CHECKDEVICELOCK(device);

	// vertex program
	initVertexShaders(device, 0, false);
	if (no_normalmap)
		rxbxBindVertexShader(device, device->special_vertex_shaders[far_depth_range?VS_SPECIAL_PARTICLE_NO_NORMALMAP_FAR_DEPTH_RANGE:VS_SPECIAL_PARTICLE_NO_NORMALMAP]);
	else
		rxbxBindVertexShader(device, device->special_vertex_shaders[far_depth_range?VS_SPECIAL_PARTICLE_FAR_DEPTH_RANGE:VS_SPECIAL_PARTICLE]);
	if (!vertex_declaration->typeless_decl)
		rxbxCreateVertexDeclarationFromComponents(device, components, 0, device->device_state.active_vertex_shader_wrapper, vertex_declaration);
	rxbxSetVertexDeclaration(device, *vertex_declaration);
}

void rxbxSetupFastParticleCPUDrawMode(RdrDeviceDX *device, RdrVertexDeclarationObj vertex_declaration, bool far_depth_range)
{
	CHECKDEVICELOCK(device);

	// vertex program
	initVertexShaders(device, 0, false);
	rxbxBindVertexShader(device, device->special_vertex_shaders[far_depth_range?VS_SPECIAL_FAST_PARTICLE_CPU_FAR_DEPTH_RANGE:VS_SPECIAL_FAST_PARTICLE_CPU]);
	rxbxSetVertexDeclaration(device, vertex_declaration);
}

void rxbxSetupFastParticleDrawMode(RdrDeviceDX *device, const VertexComponentInfo *components, RdrVertexDeclarationObj *vertex_declaration,
								   bool bLinkedScale, bool bStreak, bool far_depth_range, bool bRGBBlend, bool bAnimatedTexture)
{
	U32 vertex_shader_index;
	CHECKDEVICELOCK(device);

	// vertex program
	initVertexShaders(device, 0, false);

	if (bStreak)
	{
		vertex_shader_index = far_depth_range?VS_SPECIAL_FAST_PARTICLE_STREAK_FAR_DEPTH_RANGE:VS_SPECIAL_FAST_PARTICLE_STREAK;
	}
	else if (bLinkedScale)
	{
		vertex_shader_index = far_depth_range?VS_SPECIAL_FAST_PARTICLE_LINKSCALE_FAR_DEPTH_RANGE:VS_SPECIAL_FAST_PARTICLE_LINKSCALE;
	}
	else
	{
		vertex_shader_index = far_depth_range?VS_SPECIAL_FAST_PARTICLE_FAR_DEPTH_RANGE:VS_SPECIAL_FAST_PARTICLE;
	}

	if (bRGBBlend)
		vertex_shader_index += (VS_SPECIAL_FAST_PARTICLE_RGB_BLEND - VS_SPECIAL_FAST_PARTICLE);

	if (bAnimatedTexture)
		vertex_shader_index += (VS_SPECIAL_FAST_PARTICLE_ANIMATEDTEXTURE - VS_SPECIAL_FAST_PARTICLE);

	rxbxBindVertexShader(device, device->special_vertex_shaders[vertex_shader_index]);
	if (!vertex_declaration->typeless_decl)
		rxbxCreateVertexDeclarationFromComponents(device, components, 0, device->device_state.active_vertex_shader_wrapper, vertex_declaration);
	rxbxSetVertexDeclaration(device, *vertex_declaration);
}

void rxbxSetupCylinderTrailDrawMode(RdrDeviceDX *device, const VertexComponentInfo *components, RdrVertexDeclarationObj *vertex_declaration, bool far_depth_range)
{
	CHECKDEVICELOCK(device);

	// vertex program
	initVertexShaders(device, 0, false);
	rxbxBindVertexShader(device, device->special_vertex_shaders[far_depth_range?VS_SPECIAL_CYLINDER_TRAIL_FAR_DEPTH_RANGE:VS_SPECIAL_CYLINDER_TRAIL]);
	if (!vertex_declaration->typeless_decl)
		rxbxCreateVertexDeclarationFromComponents(device, components, 0, device->device_state.active_vertex_shader_wrapper, vertex_declaration);
	rxbxSetVertexDeclaration(device, *vertex_declaration);
}

void rxbxSetupStarFieldDrawMode(RdrDeviceDX *device, RdrGeoUsage const * pUsage, bool camera_facing, bool vertex_only_lighting)
{
	int vertex_shader_available;
	CHECKDEVICELOCK(device);

	// vertex program
	initVertexShaders(device, 0, false);
	vertex_shader_available = rxbxBindVertexShader(device, device->special_vertex_shaders[
			vertex_only_lighting?
				(camera_facing?VS_SPECIAL_STARFIELD_VERTEXONLYLIGHTING_CAMERA_FACING:VS_SPECIAL_STARFIELD_VERTEXONLYLIGHTING):
				(camera_facing?VS_SPECIAL_STARFIELD_CAMERA_FACING:VS_SPECIAL_STARFIELD)
		]);
	if (vertex_shader_available)
	{
		RdrVertexDeclarationObj vertex_declaration;
		vertex_declaration = rxbxGetVertexDeclarationDirect(device, pUsage);

		rxbxSetVertexDeclaration(device, vertex_declaration);
	}
}

__forceinline static bool bindVertexShaderWithBits(RdrDeviceDX *device, int vs_idx, RdrGeoUsage const * pUsage, DrawModeBits bits)
{
	int ret;
	ShaderHandleAndFlags vertex_shader;
	RdrVertexDeclarationObj vertex_declaration = { 0 };

	CHECKDEVICELOCK(device);

	assert(bits >= 0);
	assert(bits < DRAWBIT_MAX);
	assert(vs_idx < VS_STANDARD_MAX);

	vertex_shader = *initSpecificVertexShader(device, vs_idx * DRAWBIT_MAX + bits);
	assert(vertex_shader.handle);
	ret = rxbxBindVertexShader(device, vertex_shader.handle);
	if (ret)
	{
		vertex_declaration = rxbxGetVertexDeclarationDirect(device, pUsage);

		rxbxSetVertexDeclaration(device, vertex_declaration);
	}
	return ret;
}

bool rxbxSetupTerrainDrawMode(RdrDeviceDX *device, RdrGeoUsage const * pUsage, DrawModeBits bits)
{
	return bindVertexShaderWithBits(device, VS_TERRAIN, pUsage, bits);
}

bool rxbxSetupNormalDrawMode(RdrDeviceDX *device, RdrGeoUsage const * pUsage, DrawModeBits bits)
{
	return bindVertexShaderWithBits(device, VS_NORMAL, pUsage, bits);
}

bool rxbxSetupNormalClothDrawMode(RdrDeviceDX *device, const VertexComponentInfo * components, DrawModeBits bits, RdrVertexDeclarationObj * vertex_declaration)
{
	int ret;
	ShaderHandleAndFlags vertex_shader;

	CHECKDEVICELOCK(device);

	assert(bits >= 0);
	assert(bits < DRAWBIT_MAX);

	vertex_shader = *initSpecificVertexShader(device, VS_NORMAL * DRAWBIT_MAX + bits);
	assert(vertex_shader.handle);
	ret = rxbxBindVertexShader(device, vertex_shader.handle);
	if (ret)
	{
		if (!vertex_declaration->typeless_decl)
		{
			HRESULT hr = rxbxCreateVertexDeclarationFromComponents(device, components, 0, device->device_state.active_vertex_shader_wrapper, vertex_declaration);
			if (FAILED(hr))
				ret = false;
		}
		rxbxSetVertexDeclaration(device, *vertex_declaration);
	}

	return ret;
}

void rxbxSetupPostprocessNormalPsDrawMode(RdrDeviceDX *device, const VertexComponentInfo * components, RdrVertexDeclarationObj * vertex_declaration)
{
	int ret;
	initVertexShaders(device, 0, false);
	ret = rxbxBindVertexShader(device, device->special_vertex_shaders[VS_SPECIAL_POSTPROCESS_NORMAL_PS]);
	if (ret)
	{
		if (!vertex_declaration->typeless_decl)
		{
			HRESULT hr = rxbxCreateVertexDeclarationFromComponents(device, components, 0, device->device_state.active_vertex_shader_wrapper, vertex_declaration);
			if (FAILED(hr))
				ret = false;
		}
		rxbxSetVertexDeclaration(device, *vertex_declaration);
	}
}

//////////////////////////////////////////////////////////////////////////

// TODO DJR permanently enable unmanaged bones on Xbox
int bDebugStateManageBones = 0;
AUTO_CMD_INT(bDebugStateManageBones,bDebugStateManageBones);

void rxbxSetupSkinning(RdrDeviceDX *device, RdrDrawableSkinnedModel* draw_skin, bool bCopyScale)
{
	int i;

	CHECKDEVICELOCK(device);

	PREFETCH(draw_skin->skinning_mat_array);
	if (draw_skin->skinning_mat_indices)
		PREFETCH(draw_skin->skinning_mat_indices);
	
	if (!bDebugStateManageBones && !device->d3d11_device) // DX11TODO: should get the quicker function working once we know how we're handling vertex shader constants
	{
		rxbxSetBoneMatricesBatch(device, draw_skin, bCopyScale);
	}
	else
	if (draw_skin->skinning_mat_indices)
	{
		for (i = 0; i < draw_skin->num_bones; ++i)
		{
			rxbxSetBoneInfo(device, i, draw_skin->skinning_mat_array[draw_skin->skinning_mat_indices[i]]);
		}
	}
	else
	{
		for (i = 0; i < draw_skin->num_bones; ++i)
		{
			rxbxSetBoneInfo(device, i, draw_skin->skinning_mat_array[i]);
		}
	}
}

//////////////////////////////////////////////////////////////////////////
//////////////////////////////////////////////////////////////////////////

static void initPixelShaders(RdrDeviceDX *device, int force_reload, bool only_minimal)
{
    // PC and PS3 need null shaders, XBOX does not
#if _XBOX
	pixelShaderDefs[PS_DEFAULT_NULL].skip_me = 1;
#else
	pixelShaderDefs[PS_DEFAULT_NULL].is_nullshader = 1;
#endif

	if (!device->default_pixel_shaders)
    {
        device->default_pixel_shaders = calloc(PS_MAX * sizeof(*device->default_pixel_shaders), 1);
    }

	if (only_minimal)
	{
		if (force_reload || !device->loaded_minimal_pixel_shaders)
		{
			static struct {
				int shader_num;
				RxbxPreloadedShaderData data;
			} preloaded_data[] = {
#if _PS3
				{PS_SPRITE_DEFAULT, {sd_pshader_data_sprite, sizeof(sd_pshader_data_sprite)} },
#else
				{PS_SPRITE_DEFAULT, {dx_pshader_data_sprite} },
#endif
			};
			int i;
			for (i=0; i<ARRAY_SIZE(preloaded_data); i++)
			{
				pixelShaderDefs[preloaded_data[i].shader_num].is_minimal = 1;
				rxbxLoadPixelShaders(device, pixelShaderDefs + preloaded_data[i].shader_num, device->default_pixel_shaders + preloaded_data[i].shader_num,
					1, &preloaded_data[i].data, 1);
			}
			device->loaded_minimal_pixel_shaders = 1;
		}
	} else {
		if (force_reload || !device->loaded_default_pixel_shaders)
		{
			rxbxLoadPixelShaders(device, pixelShaderDefs, device->default_pixel_shaders, ARRAY_SIZE(pixelShaderDefs), NULL, 0);
			device->loaded_default_pixel_shaders = 1;
		}
	}
}

void rxbxSetDepthResolveMode(RdrDeviceDX *device, int multisample_count)
{
	int msaa_resolve_shader = 0;
	CHECKDEVICELOCK(device);

	if (multisample_count == 2)
		msaa_resolve_shader = PS_DEPTH_MSAA_2_RESOLVE;
	else
	if (multisample_count == 4)
		msaa_resolve_shader = PS_DEPTH_MSAA_4_RESOLVE;
	else
	if (multisample_count == 8)
		msaa_resolve_shader = PS_DEPTH_MSAA_8_RESOLVE;
	else
	if (multisample_count == 16)
		msaa_resolve_shader = PS_DEPTH_MSAA_16_RESOLVE;
	else
		assert(!"Unsupported MSAA resolution for DX11 depth buffer resolve!");

	initPixelShaders(device, 0, false);
	rxbxBindPixelShader(device, device->default_pixel_shaders[msaa_resolve_shader], NULL);
}


void rxbxSetDefaultBlendMode(RdrDeviceDX *device)
{
	CHECKDEVICELOCK(device);

	initPixelShaders(device, 0, false);
	rxbxBindPixelShader(device, device->default_pixel_shaders[PS_DEFAULT], NULL);
}

void rxbxSetLoadingPixelShader(RdrDeviceDX *device)
{
	CHECKDEVICELOCK(device);

	initPixelShaders(device, 0, false);
	rxbxBindPixelShader(device, device->default_pixel_shaders[rdr_state.showDebugLoadingPixelShader?PS_LOADING_DEBUG:PS_LOADING], NULL);
}

void rxbxSetWireframePixelShader(RdrDeviceDX *device)
{
	CHECKDEVICELOCK(device);

	initPixelShaders(device, 0, false);
	rxbxBindPixelShader(device, device->default_pixel_shaders[PS_WIREFRAME], NULL);
}

void rxbxSetPrimitiveBlendMode(RdrDeviceDX *device)
{
	CHECKDEVICELOCK(device);

	initPixelShaders(device, 0, false);
	rxbxBindPixelShader(device, device->default_pixel_shaders[PS_DEFAULT_SPRITE], NULL);
}

void rxbxSetSpriteEffectBlendMode(RdrDeviceDX *device, RdrSpriteEffect sprite_effect)
{
	CHECKDEVICELOCK(device);

	if (sprite_effect == RdrSpriteEffect_None)
	{
		initPixelShaders(device, 0, true);
	} else {
		initPixelShaders(device, 0, false);
	}

	rxbxBindPixelShader(device, device->default_pixel_shaders[PS_SPRITE_DEFAULT + sprite_effect], NULL);
}


void rxbxSetParticleBlendMode(RdrDeviceDX *device)
{
	CHECKDEVICELOCK(device);

	initPixelShaders(device, 0, false);
	rxbxBindPixelShader(device, device->default_pixel_shaders[PS_DEFAULT_PARTICLE], NULL);
}

void rxbxSetFastParticleBlendMode(RdrDeviceDX *device, bool no_tonemap, bool soft_particles, bool cpu_particles, bool manual_depth_test)
{
	int pixel_shader;

	CHECKDEVICELOCK(device);

	initPixelShaders(device, 0, false);

	pixel_shader = PS_FAST_PARTICLE;
	if (soft_particles)
		pixel_shader += 1;
	if (no_tonemap)
		pixel_shader += 2;
	if (manual_depth_test)
		pixel_shader += 4;

	rxbxBindPixelShader(device, device->default_pixel_shaders[pixel_shader], NULL);
}

void rxbxSetCylinderTrailBlendMode(RdrDeviceDX *device)
{
	CHECKDEVICELOCK(device);

	initPixelShaders(device, 0, false);
	rxbxBindPixelShader(device, device->default_pixel_shaders[PS_CYLINDER_TRAIL], NULL);
}

ShaderHandle rxbxGetCopyBlendMode(RdrDeviceDX *device, RxbxSurfaceType type, int has_depth)
{
	CHECKDEVICELOCK(device);
	assert(type >= SURF_SINGLE && type <= SURF_QUADRUPLE);
	STATIC_INFUNC_ASSERT(SURF_QUADRUPLE == SURF_SINGLE + 2);

	initPixelShaders(device, 0, false);
	return device->default_pixel_shaders[PS_NOALPHA + (type - SURF_SINGLE) + 3*has_depth];
}

ShaderHandle rxbxGetPerfTestShader(RdrDeviceDX *device)
{
	CHECKDEVICELOCK(device);

	initPixelShaders(device, 0, false);
	return device->default_pixel_shaders[PS_PERF_TEST];
}


void rxbxBindBlendModeTextures(RdrDeviceDX *device, TexHandle *textures, U32 tex_count)
{
	CHECKDEVICELOCK(device);

	// TODO this is for default blend mode only

	if (!tex_count)
		rxbxBindTexture(device, 0, 0);
	else
		rxbxBindTexture(device, 0, textures[0]);
}

__forceinline void rxbxUnbindTessellationShaders(RdrDeviceDX *device)
{
	if (!device->d3d11_device)
		return;
	rxbxBindHullShaderObject(device,NULL);
	rxbxBindDomainShaderObject(device,NULL);
}

int rxbxBindTessellationShaders(RdrDeviceDX *device, RdrNonPixelMaterial *rdr_domain_material, DrawModeBits bits)
{
	if (bits & DRAWBIT_TESSELLATION) {
		RdrTessellationFlags tessellation_flags = (rdr_domain_material?rdr_domain_material->tessellation_flags:TESSELLATE_PN_TRIANGLES);//(rdr_domain_material && (rdr_domain_material->textures)? TESSELLATE_HAS_HEIGHTMAP : TESSELLATE_PN_TRIANGLES);	// needs to have rdr_material->tessellation_flags set directly.  For now, calculating before binding the shader.
		rxbxBindHullShader(device,device->default_hull_shaders[tessellation_flags]);
		rxbxBindDomainShader(device,device->default_domain_shaders[tessellation_flags]);
	} else {
		rxbxUnbindTessellationShaders(device);
		return 1;
	}
	rxbxMarkTexturesUnused(device, TEXTURE_DOMAINSHADER);
	if (rdr_domain_material && rdr_domain_material->tex_count) {
		U32 i;
		for (i=0; i<rdr_domain_material->tex_count; i++)
		{
			PERFINFO_AUTO_STOP_START("rxbxBindDomainTexture", 1);
			rxbxBindDomainTexture(device, i, rdr_domain_material->textures[i]);
		}
		rxbxDomainShaderParameters(device,DS_CONSTANT_SCALE_OFFSET,rdr_domain_material->constants,1);
	}
	rxbxUnbindUnusedTextures(device, TEXTURE_DOMAINSHADER);
	return 1;
}

int rxbxBindMaterial(RdrDeviceDX *device, RdrMaterial *rdr_material, 
					 RdrLightData *lights[MAX_NUM_OBJECT_LIGHTS], RdrLightColorType color_type, 
					 ShaderHandle handle, RdrMaterialShader uberlight_shader_num, 
					 bool uses_shadowbuffer, bool force_no_shadows, bool manual_depth_test)
{
	static BOOL boolean_constants[MAX_PS_BOOL_CONSTANTS] = {0};
	U32 i, tex_count = rdr_material->tex_count, mat_const_count = rdr_material->const_count, ret=1, light_const_count = 0;

	CHECKDEVICELOCK(device);
	// TODO: State management

	rxbxMarkTexturesUnused(device, TEXTURE_PIXELSHADER);

	if (force_no_shadows)
		uses_shadowbuffer = false;

	if (!handle)
	{
		PERFINFO_AUTO_START("rxbxSetDefaultBlendMode", 1);
		rxbxSetDefaultBlendMode(device);
		PERFINFO_AUTO_STOP_START("rxbxBindBlendModeTextures", 1);
		rxbxBindBlendModeTextures(device, rdr_material->textures, tex_count);
		PERFINFO_AUTO_STOP();
	}
	else
	{
		BOOL *boolean_constants_to_bind = NULL;
		bool has_shadowed_point_light = false;

		rxbxSetShadowBufferTextureActive(device, uses_shadowbuffer);

		PERFINFO_AUTO_START("", 1);
		if (handle >= 0) {
			for (i=0; i<tex_count; i++) {
				PERFINFO_AUTO_STOP_START("rxbxBindTexture", 1);
				rxbxBindTexture(device, i, rdr_material->textures[i]);
			}
			if (manual_depth_test)
				rxbxBindTexture(device, 15, RdrTexHandleToTexHandle(device->active_surface->rendertarget[SBUF_DEPTH].textures[0].tex_handle));
			if (mat_const_count) {
				PERFINFO_AUTO_STOP_START("rxbxPixelShaderMaterialParameters", 1);
				rxbxPixelShaderConstantParameters(device, 0, rdr_material->constants, mat_const_count, PS_CONSTANT_BUFFER_MATERIAL);
			}
			if (lights)
			{
				int j;

				if (uberlight_shader_num.shaderMask & (MATERIAL_SHADER_UBERLIGHT|MATERIAL_SHADER_UBERSHADOW))
				{
					RdrUberLightParameters *uber_params = &rdr_uber_params[((uberlight_shader_num.shaderMask&MATERIAL_SHADER_UBERSHADOW)?UBER_SHADOWS:UBER_LIGHTING)
																		   + ((uberlight_shader_num.shaderMask&MATERIAL_SHADER_FORCE_2_LIGHTS)?UBER_SHADOWS_2LIGHTS-UBER_SHADOWS:0)];
					int bLightSlotUsedForOverflow[MAX_NUM_SHADER_LIGHTS + 2] = {0};
					bool needs_shadow_test = !force_no_shadows && !uses_shadowbuffer && !(uberlight_shader_num.shaderMask&MATERIAL_SHADER_UBERSHADOW);
					int chunk_idx = 0, total_chunk_textures = 0;
					
					for (j = 0; j < uber_params->max_lights; ++j)
					{
						RdrLightData *light_data = lights[j];
						
						if (light_data)
						{
							RdrLightShaderData *light_shader_data = uber_params->simple_lighting ? &light_data->simple[color_type] : &light_data->normal[color_type];
							int chunk_count1 = round(ceil(light_shader_data->tex_count / (F32)uber_params->chunk_tex_count));
							int chunk_count2 = round(ceil(light_shader_data->const_count / (F32)uber_params->chunk_const_count));
							int chunk_count = MAX(chunk_count1, chunk_count2);
							int k;

							// If the light won't fit, skip activating textures and shader constants
							if (!(chunk_idx + chunk_count > uber_params->max_lights ||
								(light_shader_data->tex_count && total_chunk_textures + light_shader_data->tex_count > uber_params->max_textures_total)))
							{
								for (k = 1; k < chunk_count; ++k)
									bLightSlotUsedForOverflow[chunk_idx + k] = true;

								for (i = 0; i < light_shader_data->tex_count; ++i)
								{
									PERFINFO_AUTO_STOP_START("lights:rxbxBindTexture", 1);
									rxbxBindTexture(device, tex_count + i, light_shader_data->tex_handles[i]);
								}

								if (light_shader_data->const_count)
								{
									PERFINFO_AUTO_STOP_START("lights:rxbxPixelShaderLightParameters", 1);
									rxbxPixelShaderLightParameters(device,light_const_count,light_shader_data->constants, mat_const_count, light_shader_data->const_count);
								}
							}

							if (total_chunk_textures + chunk_count * uber_params->chunk_tex_count > uber_params->max_textures_total)
							{
								tex_count += uber_params->max_textures_total - total_chunk_textures;
								total_chunk_textures = uber_params->max_textures_total;
							}
							else
							{
								tex_count += chunk_count * uber_params->chunk_tex_count;
								total_chunk_textures += chunk_count * uber_params->chunk_tex_count;
							}
							light_const_count += chunk_count * uber_params->chunk_const_count;
							chunk_idx += chunk_count;
						}
					}

					if (chunk_idx < uber_params->max_lights)
					{
						// shadow test constants start past all chunks, so increment the counters appropriately if some chunks were unused
						int chunk_count = uber_params->max_lights - chunk_idx;

						if (total_chunk_textures + chunk_count * uber_params->chunk_tex_count > uber_params->max_textures_total)
						{
							tex_count += uber_params->max_textures_total - total_chunk_textures;
							total_chunk_textures = uber_params->max_textures_total;
						}
						else
						{
							tex_count += chunk_count * uber_params->chunk_tex_count;
							total_chunk_textures += chunk_count * uber_params->chunk_tex_count;
						}
						light_const_count += chunk_count * uber_params->chunk_const_count;
						chunk_idx += chunk_count;
					}

					if (needs_shadow_test && lights[0])
					{
						RdrLightData *light_data = lights[0];

						if (!light_data->shadow_test.tex_count && !light_data->shadow_test.const_count)
							needs_shadow_test = false;
						else if (!(rdrGetLightType(uberlight_shader_num.lightMask, 0) & RDRLIGHT_SHADOWED))
							needs_shadow_test = false;
						else
						{
							for (i = 0; i < light_data->shadow_test.tex_count; ++i)
							{
								PERFINFO_AUTO_STOP_START("shadow_test:rxbxBindTexture", 1);
								rxbxBindTexture(device, tex_count + i, light_data->shadow_test.tex_handles[i]);
							}

							if (light_data->shadow_test.const_count)
							{
								PERFINFO_AUTO_STOP_START("shadow_test:rxbxPixelShaderLightParameters", 1);
								rxbxPixelShaderLightParameters(device,light_const_count,light_data->shadow_test.constants, mat_const_count, light_data->shadow_test.const_count);
							}

							if (rdrGetLightType(uberlight_shader_num.lightMask, 0) == (RDRLIGHT_POINT|RDRLIGHT_SHADOWED))
								has_shadowed_point_light = true;
						}

						tex_count += light_data->shadow_test.tex_count;
						light_const_count += light_data->shadow_test.const_count;
					}

					{
						int bit_index=0;
						int light_index = 0;
						STATIC_INFUNC_ASSERT(UBERLIGHT_BITS_PER_LIGHT == 3);

						boolean_constants[bit_index++] = needs_shadow_test;

						for (j=0; j<uber_params->max_lights; j++)
						{
							RdrLightType light_type = RDRLIGHT_NONE;
							if (!bLightSlotUsedForOverflow[j])
								light_type = rdrGetLightType(uberlight_shader_num.lightMask, light_index++);
							boolean_constants[bit_index++] = light_type & 1;
							boolean_constants[bit_index++] = (light_type>>1) & 1;
							boolean_constants[bit_index++] = (light_type>>2) & 1;
						}

						boolean_constants_to_bind = boolean_constants;
					}
				} else {
					// Non-uberlighting
					for (j = 0; j < MAX_NUM_SHADER_LIGHTS; ++j)
					{
						RdrLightData *light_data = lights[j];
						if (light_data)
						{
							RdrLightShaderData *light_shader_data = (uberlight_shader_num.shaderMask & MATERIAL_SHADER_SIMPLE_LIGHTING) ? &light_data->simple[color_type] : &light_data->normal[color_type];
							if (light_data->light_type == (RDRLIGHT_POINT|RDRLIGHT_SHADOWED))
								has_shadowed_point_light = true;

							if (!force_no_shadows)
							{
								for (i = 0; i < light_data->shadow_test.tex_count; ++i) {
									PERFINFO_AUTO_STOP_START("shadow_test:rxbxBindTexture", 1);
									rxbxBindTexture(device, tex_count + i, light_data->shadow_test.tex_handles[i]);
								}
							}
							else
							{
								for (i = 0; i < light_data->shadow_test.tex_count; ++i) {
									PERFINFO_AUTO_STOP_START("shadow_test:rxbxBindTexture", 1);
									if(rdrGetTexHandleFlags(&(light_data->shadow_test.tex_handles[i])) & RTF_COMPARISON_LESS_EQUAL) {
										// Use the white depth texture here because it supports comparison sampling,
										// just like the texture that would normally be in this slot.
										rxbxBindTexture(device, tex_count + i, device->white_depth_tex_handle);
									} else {
										rxbxBindTexture(device, tex_count + i, device->white_tex_handle);
									}
								}
							}
							if (light_data->shadow_test.const_count) {
								PERFINFO_AUTO_STOP_START("shadow_test:rxbxPixelShaderLightParameters", 1);
								rxbxPixelShaderLightParameters(device,light_const_count,light_data->shadow_test.constants, mat_const_count, light_data->shadow_test.const_count);
								light_const_count += light_data->shadow_test.const_count;
							}

							tex_count += light_data->shadow_test.tex_count;

							for (i = 0; i < light_shader_data->tex_count; ++i) {
								PERFINFO_AUTO_STOP_START("lights:rxbxBindTexture", 1);
								rxbxBindTexture(device, tex_count + i, light_shader_data->tex_handles[i]);
							}

							if (light_shader_data->const_count) {
								PERFINFO_AUTO_STOP_START("lights:rxbxPixelShaderLightParameters", 1);
								rxbxPixelShaderLightParameters(device,light_const_count,light_shader_data->constants, mat_const_count, light_shader_data->const_count);
								light_const_count += light_shader_data->const_count;
							}
							tex_count += light_shader_data->tex_count;
						}
					}
				}
			}
		}
		PERFINFO_AUTO_STOP();

		rxbxSetCubemapLookupTextureActive(device, has_shadowed_point_light);
		ret = rxbxBindPixelShader(device, handle, boolean_constants_to_bind);
	}

	rxbxUnbindUnusedTextures(device, TEXTURE_PIXELSHADER);

	return ret;
}

int rxbxBindMaterialForDepth(RdrDeviceDX *device, RdrMaterial *rdr_material, ShaderHandle handle)
{
	static BOOL boolean_constants[MAX_PS_BOOL_CONSTANTS] = {0};
	U32 i, tex_count = rdr_material->tex_count, const_count = rdr_material->const_count, ret=1;

	CHECKDEVICELOCK(device);
	// TODO: State management

	rxbxMarkTexturesUnused(device, TEXTURE_PIXELSHADER);

	if (!handle)
	{
		PERFINFO_AUTO_START("rxbxSetDefaultBlendMode", 1);
		rxbxSetDefaultBlendMode(device);
		PERFINFO_AUTO_STOP_START("rxbxBindBlendModeTextures", 1);
		rxbxBindBlendModeTextures(device, rdr_material->textures, tex_count);
		PERFINFO_AUTO_STOP();
	}
	else
	{
		BOOL *boolean_constants_to_bind = NULL;

		rxbxSetShadowBufferTextureActive(device, false);

		PERFINFO_AUTO_START("", 1);
		if (handle >= 0) {
			for (i=0; i<tex_count; i++) {
				RdrTexHandle rdrTexHandle = TexHandleToRdrTexHandle(rdr_material->textures[i]);
				RdrTextureDataDX *texdata = NULL;
				bool bSkip = false;

				// lots of hash lookups, not good
				if (rdrTexHandle.texture.hash_value)
				{
					stashIntFindPointer(device->texture_data, rdrTexHandle.texture.hash_value, &texdata);
					if (texdata && texdata->tex_type != RTEX_2D)
					{
						bSkip = true;
					}
				}

				PERFINFO_AUTO_STOP_START("rxbxBindTexture", 1);
				if (!bSkip)
				{
					rxbxBindTexture(device, i, rdr_material->textures[i]);
				}
				else
				{
					rxbxBindTexture(device, i, 0);
				}
			}
			if (const_count) {
				PERFINFO_AUTO_STOP_START("rxbxPixelShaderMaterialParameters", 1);
				rxbxPixelShaderConstantParameters(device, 0, rdr_material->constants, const_count, PS_CONSTANT_BUFFER_MATERIAL);
			}
		}
		PERFINFO_AUTO_STOP();

		rxbxSetCubemapLookupTextureActive(device, false);
		ret = rxbxBindPixelShader(device, handle, boolean_constants_to_bind);
	}

	rxbxUnbindUnusedTextures(device, TEXTURE_PIXELSHADER);

	return ret;
}

void rxbxPreloadVertexShaders(RdrDeviceDX *device, void *data_UNUSED, WTCmdPacket *packet)
{
	bool oldv = rdr_state.disableAsyncVshaderLoad;
	rdr_state.disableAsyncVshaderLoad = true;
	initVertexShaders(device, 0, false);
	if (rdr_state.preloadAllVertexShaders)
	{
		int i;
		int c=0;
		int t=timerAlloc();
		for (i=0; i<DRAWBIT_MAX*VS_STANDARD_MAX; i++)
		{
			RxbxProgramDef *def = initVertexShaderDef(device, i);
			if (def && !def->skip_me) {
				initSpecificVertexShader(device, i);
				c++;
			}
		}
		rdrStatusPrintfFromDeviceThread(&device->device_base,"Preloaded %d vertex shaders in %1.2fs\n", c, timerElapsed(t));
		timerFree(t);
	}
	rdr_state.disableAsyncVshaderLoad = oldv;
}

//////////////////////////////////////////////////////////////////////////

void rxbxReloadDefaultShadersDirect(RdrDeviceDX *device, void *data_UNUSED, WTCmdPacket *packet)
{
	SHADER_LOG("rxRDSD\n");
	//stashTableDestroy(device->lpc_crctable);
	//device->lpc_crctable = 0;
	initVertexShaders(device, 1, false);
	initPixelShaders(device, 1, false);
	if (device->d3d11_device && (device->rdr_caps_new.features_supported & FEATURE_TESSELLATION)) {
		initHullShaders(device, true);
		initDomainShaders(device, true);
	}
	FOR_EACH_IN_STASHTABLE(device->standard_vertex_shaders_table, ShaderHandleAndFlags, handle)
	{
		// TODO: do something if it's in the process of loading?
		handle->loaded = 0;
	}
	FOR_EACH_END;
	vertexShaderCache.count = 0;
	SHADER_LOG("rxRDSD Done\n");
}

//////////////////////////////////////////////////////////////////////////
#if !_PS3
void rxbxSetupQuadIndexList(RdrDeviceDX *device, U16 quad_count)
{
	PERFINFO_AUTO_START_FUNC();
	if (device->quad_index_list_count < quad_count)
	{
		HRESULT hr;
		U32 i;
		U16 *index_ptr;
		U32 index_bytes;
		if (device->quad_index_list_ibo.typeless_index_buffer)
		{
			rxbxReleaseIndexBuffer(device, device->quad_index_list_ibo);
			rxbxNotifyIndexBufferFreed(device, device->quad_index_list_ibo);
			device->quad_index_list_ibo.typeless_index_buffer = NULL;
		}
		device->quad_index_list_count = min(pow2(quad_count), 65535);
		index_bytes = device->quad_index_list_count * 6 * sizeof(U16);
		hr = rxbxCreateIndexBuffer(device, index_bytes, BUF_DYNAMIC, &device->quad_index_list_ibo);
		rxbxFatalHResultErrorf(device, hr, "creating quad index buffer", "");

		rdrTrackUserMemoryDirect(&device->device_base, "VideoMemory:QuadIBO", 1, device->quad_index_list_count*6*sizeof(U16));

		index_ptr = rxbxIndexBufferLockWrite(device, device->quad_index_list_ibo, RDRLOCK_WRITEONCE, 0, 0, NULL);
		assert(index_ptr);
		for (i = 0; i < device->quad_index_list_count; ++i)
		{
			*(index_ptr++) = i * 4;
			*(index_ptr++) = i * 4 + 1;
			*(index_ptr++) = i * 4 + 2;

			*(index_ptr++) = i * 4;
			*(index_ptr++) = i * 4 + 2;
			*(index_ptr++) = i * 4 + 3;
		}
		rxbxIndexBufferUnlock(device, device->quad_index_list_ibo);
	}
	rxbxSetIndices(device, device->quad_index_list_ibo, false);
	PERFINFO_AUTO_STOP_FUNC();
}
#endif


//////////////////////////////////////////////////////////////////////////////////
// Hull Shaders
//////////////////////////////////////////////////////////////////////////////////


static int initHullShaders(RdrDeviceDX *device, bool force_reload)
{
	int shader_success;

	if (!(device->rdr_caps_new.features_supported & FEATURE_TESSELLATION))
		return 0;

	if (force_reload || !device->default_hull_shaders) {
		if (!device->default_hull_shaders)
		{
			device->default_hull_shaders = calloc(ARRAY_SIZE(hullShaderDefs), sizeof(*device->default_hull_shaders));
		}

		shader_success = rxbxLoadHullShaders(device, hullShaderDefs, device->default_hull_shaders, ARRAY_SIZE(hullShaderDefs), NULL, 0);

		if (!shader_success)
		{
			int shader_index;
			for (shader_index = 0; shader_index < ARRAY_SIZE(hullShaderDefs); ++shader_index)
			{
				RxbxHullShader * hull_shader = NULL;
				if (stashIntFindPointer(device->hull_shaders, device->default_hull_shaders[shader_index], &hull_shader))
					rxbxFreeHullShaderInternal(device, device->default_hull_shaders[shader_index], hull_shader, false);
			}
			device->error_hull_shader = NULL;
			free(device->default_hull_shaders);
			device->default_hull_shaders = NULL;
		}

		return shader_success;
	}
	return 1;
}

void rxbxPreloadHullShaders(RdrDeviceDX *device, void *data_UNUSED, WTCmdPacket *packet)
{
	bool oldv = rdr_state.disableAsyncVshaderLoad;
	RxbxProgramDef *def = initHullShaderDef(device);
	int t;
	rdr_state.disableAsyncVshaderLoad = true;
	if (!device->d3d11_device)
		return;
	t = timerAlloc();
	if (initHullShaders(device, false))
		rdrStatusPrintfFromDeviceThread(&device->device_base,"Preloaded %d hull shader(s) in %1.2fs\n", ARRAY_SIZE(hullShaderDefs), timerElapsed(t));
	else
		rdrStatusPrintfFromDeviceThread(&device->device_base,"Tessellation not supported, skipping hull shader creation.\n");
	timerFree(t);
	rdr_state.disableAsyncVshaderLoad = oldv;
}

//////////////////////////////////////////////////////////////////////////////////
// Domain Shaders
//////////////////////////////////////////////////////////////////////////////////


static int initDomainShaders(RdrDeviceDX *device, bool force_reload)
{
	int shader_success;

	if (!(device->rdr_caps_new.features_supported & FEATURE_TESSELLATION))
		return 0;

	if (!device->default_domain_shaders || force_reload) {
		if (!device->default_domain_shaders)
		{
			device->default_domain_shaders = calloc(ARRAY_SIZE(domainShaderDefs), sizeof(*device->default_domain_shaders));
		}

		shader_success = rxbxLoadDomainShaders(device, domainShaderDefs, device->default_domain_shaders, ARRAY_SIZE(domainShaderDefs), NULL, 0);

		if (!shader_success)
		{
			int shader_index;
			for (shader_index = 0; shader_index < ARRAY_SIZE(domainShaderDefs); ++shader_index)
			{
				RxbxDomainShader * domain_shader = NULL;
				if (stashIntFindPointer(device->domain_shaders, device->default_domain_shaders[shader_index], &domain_shader))
					rxbxFreeDomainShaderInternal(device, device->default_domain_shaders[shader_index], domain_shader, false);
			}
			device->error_domain_shader = NULL;
			free(device->default_domain_shaders);
			device->default_domain_shaders = NULL;
		}

		return shader_success;
	}
	return 1;
}

void rxbxPreloadDomainShaders(RdrDeviceDX *device, void *data_UNUSED, WTCmdPacket *packet)
{
	bool oldv = rdr_state.disableAsyncVshaderLoad;
	RxbxProgramDef *def = initDomainShaderDef(device);
	int c=0;
	int t;
	rdr_state.disableAsyncVshaderLoad = true;
	if (!device->d3d11_device)
		return;
	t = timerAlloc();
	if (initDomainShaders(device,false))
		rdrStatusPrintfFromDeviceThread(&device->device_base,"Preloaded %d domain shader in %1.2fs\n", c, timerElapsed(t));
	else
		rdrStatusPrintfFromDeviceThread(&device->device_base,"Tessellation not supported, skipping domain shader creation.\n");
	timerFree(t);
	rdr_state.disableAsyncVshaderLoad = oldv;
}
