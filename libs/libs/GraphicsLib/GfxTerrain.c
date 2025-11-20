#include "GfxTerrain.h"

#include "MemRef.h"
#include "EventTimingLog.h"
#include "UnitSpec.h"

#include "../StaticWorld/WorldCell.h"
#include "../AutoGen/WorldCellEntry_h_ast.h"
#include "wlTerrainSource.h"
#include "wlTerrainInline.h"
#include "WorldCellClustering.h"
#include "wlModelInline.h"
#include "Materials.h"

#include "GfxMaterials.h"
#include "GfxLightCache.h"
#include "GfxGeo.h"
#include "GfxPrimitive.h"
#include "GfxOcclusion.h"
#include "rgb_hsv.h"
#include "GfxTexturesInline.h"
#include "GfxDrawFrame.h"
#include "GfxTerrainVis.h"
#include "bounds.h"

// These are needed for the terrain lighting calculation and binning...
#include "GfxModel.h"
#include "GfxLightCache.h"
#include "GfxModelCache.h"
#include "GfxWorld.h"
#include "GenericMesh.h"
#include "hoglib.h"
#include "serialize.h"
#include "utilitieslib.h"

#include "ReadPNG.h"
#include "RdrTexture.h"
#include "CrypticDXT.h"
#include "GfxTextureTools.h"
#include "DirectDrawTypes.h"


AUTO_RUN_ANON(memBudgetAddMapping(__FILE__, BUDGET_Terrain_System););

GfxTerrainDebugState terrain_state;

BasicTexture *g_DebugHeightMapColorTex[MAX_TERRAIN_LODS-2];

static Material *g_TerrainEdgeMaterial;

static bool debug_atlas_lod = false;
static int force_draw_lod = -1;


int debug_atlas_x = 0;
int debug_atlas_z = 0;
int debug_atlas_lod_num = -1;

// Forces all terrain tiles to draw at this LOD level, or not draw at all if they do not have this LOD level loaded
AUTO_CMD_INT(force_draw_lod, terrainForceDrawLOD) ACMD_CATEGORY(Debug);

AUTO_COMMAND;
void terrain_debug_lods(int on)
{ 
	debug_atlas_lod = !!on;
}

// Highlights, with wireframe, a given block & lod of all clusters layers.
AUTO_COMMAND;
void debug_atlas(int x, int z, int lod)
{
	debug_atlas_x = x;
	debug_atlas_z = z;
	debug_atlas_lod_num = lod;
}

#define ENABLE_TRACE_VERTEX_LIGHT_HOGG 0
#if ENABLE_TRACE_VERTEX_LIGHT_HOGG
static void atlasTraceHoggf(const char * format, ...)
{
	va_list va;
	char buf[1024]={0};

	va_start(va, format);
	vsprintf(buf, format, va);
	va_end(va);

	if (IsDebuggerPresent())
	{
		OutputDebugString(buf);
	} 
}
#else
#define atlasTraceHoggf(fmt, ...)
#endif


/********************************
   Utility
 ********************************/

// TODO DJR standardize this code
#define WORLD_TRAVERSAL_STATS 1
#if WORLD_TRAVERSAL_STATS
#define INC_FRAME_COUNT(M_sCounter) ++gfx_state.debug.frame_counts. M_sCounter
#else
#define INC_FRAME_COUNT(M_sCounter)
#endif

#define ADD_MATERIAL_TEXTURE(mat_texture_list, mat_op, mat_input, mat_texture) \
	if (mat_texture) \
	{\
		MaterialNamedTexture *tex = StructCreate(parse_MaterialNamedTexture); \
		tex->op = mat_op; tex->input = mat_input; \
		tex->texture = mat_texture; eaPush(&(mat_texture_list), tex); \
	}

static const char *s_Terrain, *s_ColorTexture, *s_Scale, *s_SpecularValue, *s_SpecularExponent;
static const char *s_DetailTexture, *s_DetailTexture1, *s_DetailTexture2, *s_DetailTexture3, *s_DetailTexture4;
static const char *s_DetailNormalTexture, *s_DetailNormalTexture1, *s_DetailNormalTexture2, *s_DetailNormalTexture3, *s_DetailNormalTexture4;
static const char *s_UVScale1, *s_UVScale2, *s_DebugColor, *s_DetailUVParams, *s_SpecularValueIn, *s_SpecularExponentIn;
static const char *s_DetailSwitchSelect1, *s_DetailSwitchSelect2, *s_DetailSwitchSelect3;

AUTO_RUN;
void initGfxTerrainPooledStrings(void)
{
	s_Terrain = allocAddStaticString("Terrain");
	s_ColorTexture = allocAddStaticString("ColorTexture");
	s_DetailTexture = allocAddStaticString("DetailTexture");
	s_DetailTexture1 = allocAddStaticString("DetailTexture1");
	s_DetailTexture2 = allocAddStaticString("DetailTexture2");
	s_DetailTexture3 = allocAddStaticString("DetailTexture3");
	s_DetailTexture4 = allocAddStaticString("DetailTexture4");
	s_DetailNormalTexture = allocAddStaticString("DetailNormalTexture");
	s_DetailNormalTexture1 = allocAddStaticString("DetailNormalTexture1");
	s_DetailNormalTexture2 = allocAddStaticString("DetailNormalTexture2");
	s_DetailNormalTexture3 = allocAddStaticString("DetailNormalTexture3");
	s_DetailNormalTexture4 = allocAddStaticString("DetailNormalTexture4");
	s_Scale = allocAddStaticString("Scale");
	s_SpecularValue = allocAddStaticString("SpecularValue");
	s_SpecularExponent = allocAddStaticString("SpecularExponent");
	s_UVScale1 = allocAddStaticString("UVScale1");
	s_UVScale2 = allocAddStaticString("UVScale2");
	s_DebugColor = allocAddStaticString("DebugColor");
	s_DetailUVParams = allocAddStaticString("DetailUVParams");
	s_SpecularValueIn = allocAddStaticString("SpecularValueIn");
	s_SpecularExponentIn = allocAddStaticString("SpecularExponentIn");
	s_DetailSwitchSelect1 = allocAddStaticString("DetailSwitchSelect1");
	s_DetailSwitchSelect2 = allocAddStaticString("DetailSwitchSelect2");
	s_DetailSwitchSelect3 = allocAddStaticString("DetailSwitchSelect3");
}

static void updateDebugTextures(void)
{
	static U8 colors[] = {  255, 255, 255,  255, 128, 128,  255, 0, 0,  0, 255, 0,  128, 255, 255,  0, 128, 255 };
	if (!g_DebugHeightMapColorTex[0])
	{
		int i, x, y;
		Color *colordata;

		for (i = 0; i < 6; i++)
		{
			colordata = memrefAlloc(sizeof(Color)*16*16);
			for (y = 0; y < 16; y++)
			{
				for (x = 0; x < 16; x++)
				{
					colordata[x+y*16].r = colors[i*3];
					colordata[x+y*16].g = colors[i*3+1];
					colordata[x+y*16].b = colors[i*3+2];
					colordata[x+y*16].a = 255;
				}
			}
			g_DebugHeightMapColorTex[i] = texGenNew(16, 16, "TerrainColorDebug", TEXGEN_NORMAL, WL_FOR_TERRAIN);
			texGenUpdate(g_DebugHeightMapColorTex[i], (U8*)colordata, RTEX_2D, RTEX_BGRA_U8, 1, true, false, true, true);
			memrefDecrement(colordata);
		}
	}
}

/********************************
   Atlas Rendering
 ********************************/

typedef enum TerrainShaderParams
{
	TSP_LODS,
	TSP_COLOR_AND_GEO_SIZE,

	TSP_COUNT,
} TerrainShaderParams;

typedef enum TerrainGfxDataMaterialNamedConstants
{
	TER_GFX_UV_SCALE_1 = 0,			//Must be 0
	TER_GFX_UV_SCALE_2,				//Must be 1
//	TER_GFX_DEBUG_COLOR,
	TER_GFX_SPECULARITY,
	TER_GFX_SPECULAR_EXPONENT,
	TER_GFX_DETAIL_SWITCH_SELECT1,
	TER_GFX_DETAIL_SWITCH_SELECT2,
	TER_GFX_DETAIL_SWITCH_SELECT3,
	TER_GFX_DETAIL_SCALE,           //Editor only

	TER_GFX_MAT_NAMED_CONST_CNT,	//Must be last
} TerrainGfxDataMaterialNamedConstants;

typedef struct TerrainMaterialParams
{
	Material				*material;
	MaterialNamedTexture	**texture_list;
	BasicTexture 			*detail_textures[3];
	BasicTexture 			*detail_normal_textures[3];
	MaterialNamedConstant	**mat_named_consts;
} TerrainMaterialParams;

typedef struct HeightMapAtlasGfxData
{
	struct 
	{
		U32						last_update_time;
		U8						occluded_bits;
	} occlusion_data;

	bool						needs_texture_update;

	BasicTexture 				*color_tex;

	GeoRenderInfo				*edge_render_info[4];
    bool                        edge_needs_update;

	Vec4						shader_params[TSP_COUNT];

	int							material_count;
	TerrainMaterialParams		*material_params;

	BasicTexture				**cluster_tex_swaps;
} HeightMapAtlasGfxData;

void freeMaterialNamedConstant(MaterialNamedConstant * pConstant)
{
	StructDestroy(parse_MaterialNamedConstant, pConstant);
}

static void clearMaterialParams(HeightMapAtlasGfxData *gfx_data)
{
	int i;

	for (i = 0; i < gfx_data->material_count; i++)
	{
		TerrainMaterialParams *material_params = &gfx_data->material_params[i];
		eaDestroyStruct(&material_params->texture_list, parse_MaterialNamedTexture);
		eaDestroyEx(&material_params->mat_named_consts, freeMaterialNamedConstant);
	}

	SAFE_FREE(gfx_data->material_params);
	gfx_data->material_count = 0;
}

static void heightMapAtlasGfxDataFree(HeightMapAtlasGfxData *pack)
{
	bool found = false;
	int j;

	for (j = 0; j <= ATLAS_MAX_LOD; j++)
	{
		if (pack->color_tex == g_DebugHeightMapColorTex[j])
		{
			found = true;
			break;
		}
	}
	if (pack->color_tex && !found)
	{
		texGenFree(pack->color_tex);
		pack->color_tex = NULL;
	}

	for (j = eaSize(&pack->cluster_tex_swaps) - 1; j >= 0; j--) {
		if (j & 1) {	// every other texture is a dynamic texture.
			texUnregisterDynamic(pack->cluster_tex_swaps[j]->name);
			eaRemove(&pack->cluster_tex_swaps,j);
		}
	}
	eaDestroy(&pack->cluster_tex_swaps);	// I don't actually "own" the basic textures since they should always be present in the system, so do not free them.  Just free the earray.

	clearMaterialParams(pack);

	SAFE_FREE(pack);
}

static void updateTexture(TerrainTextureData *tex_data, BasicTexture **tex_ptr, const char *name, BasicTexture *tex_override)
{
	if (tex_override && *tex_ptr)
	{
		if (*tex_ptr != tex_override)
			texGenFree(*tex_ptr);
		*tex_ptr = NULL;
	}

	if (tex_data->data)
	{
		if (!tex_override)
		{
			RdrTexFormat tex_format;
			if (tex_data->is_dxt)
				tex_format = tex_data->has_alpha?RTEX_DXT5:RTEX_DXT1;
			else
				tex_format = tex_data->has_alpha?RTEX_BGRA_U8:RTEX_BGR_U8;

			if (*tex_ptr && ((*tex_ptr)->rdr_format != tex_format || (*tex_ptr)->width != tex_data->width))
			{
				if (*tex_ptr != tex_override)
					texGenFree(*tex_ptr);
				*tex_ptr = NULL;
			}

			if (!*tex_ptr)
				*tex_ptr = texGenNew(tex_data->width, tex_data->width, name, TEXGEN_NORMAL, WL_FOR_TERRAIN);
			texGenUpdate(*tex_ptr, tex_data->data, RTEX_2D, tex_format, 1, true, false, false, true);
		}

		memrefDecrement(tex_data->data);
		tex_data->data = NULL;
	}

	if (tex_override)
	{
		*tex_ptr = tex_override;
	}
}

char const * g_pchColorTexOverride = NULL;

static void gfxAtlasUpdateMaterials(HeightMapAtlas *atlas)
{
	HeightMapAtlasData *atlas_data = atlas->data;
	int lod = atlas->lod;
	GfxTerrainViewMode *view_params = terrain_state.view_params;
	HeightMapAtlasGfxData *gfx_data = atlas_data->client_data.gfx_data;
	Material **detail_material_pointers;
	const char **detail_material_names;
	ModelLOD *model;
	int i, j, k;
	TexID *tex_idx;
	BasicTexture *color_tex;
	Vec2 texcoord_offset;
	F32 texcoord_scale, tex_width;
	ZoneMap *primary_zmap = worldGetPrimaryMap();

	PERFINFO_AUTO_START_FUNC();

	updateTexture(&atlas_data->client_data.color_texture, &gfx_data->color_tex, "TerrainColor", debug_atlas_lod?g_DebugHeightMapColorTex[atlas->lod]:NULL);

	if (!gfx_data->color_tex && atlas->parent)
	{
		HeightMapAtlasData *parent_atlas_data = atlas->parent->data;
		HeightMapAtlasGfxData *parent_gfx_data = parent_atlas_data->client_data.gfx_data;
		updateTexture(&parent_atlas_data->client_data.color_texture, &parent_gfx_data->color_tex, "TerrainColor", debug_atlas_lod?g_DebugHeightMapColorTex[atlas->parent->lod]:NULL);
		color_tex = parent_gfx_data->color_tex;
		setVec2(texcoord_offset, (atlas->local_pos[0] % 2) ? 0.5f : 0, (atlas->local_pos[1] % 2) ? 0.5f : 0);
		texcoord_scale = 0.5f;
		tex_width = parent_atlas_data->client_data.color_texture.width;
	}
	else
	{
		color_tex = gfx_data->color_tex;
		zeroVec2(texcoord_offset);
		texcoord_scale = 1;
		tex_width = atlas_data->client_data.color_texture.width;
	}


	//////////////////////////////////////////////////////////////////////////
	// Clear previous texture pointers
	clearMaterialParams(gfx_data);

	assert(atlas_data->client_data.draw_model);

	//////////////////////////////////////////////////////////////////////////
	// Assign new texture pointers

	model = modelGetLOD(atlas_data->client_data.draw_model, 0);

	if(!model->vert_count) {
		// This is an empty piece of terrain. Possibly removed entirely by the cut brush.
		PERFINFO_AUTO_STOP_FUNC();
		return;
	}

	gfx_data->material_count = model->data->tex_count;
	assert(gfx_data->material_count);
	gfx_data->material_params = calloc(gfx_data->material_count, sizeof(*gfx_data->material_params));
	assert(gfx_data->material_params);
	detail_material_names = _alloca(sizeof(char *) * 3 * gfx_data->material_count);
	detail_material_pointers = _alloca(sizeof(Material *) * 3 * gfx_data->material_count);
	tex_idx = model->data->tex_idx;

	assert(gfx_data->material_count == model->data->tex_count);
	memset((char **)detail_material_names, 0, sizeof(char *) * 3 * gfx_data->material_count);
	memset(detail_material_pointers, 0, sizeof(Material *) * 3 * gfx_data->material_count);

	assert(eaSize(&atlas_data->client_data.model_materials) > 0);

	for (i = 0; i < gfx_data->material_count; ++i)
	{
		int detail_count;
		TerrainMaterialParams *material_params = &gfx_data->material_params[i];
		TerrainMeshRenderMaterial *terrain_material;
		int tex_id = tex_idx[i].id;

		assert(tex_id < eaSize(&atlas_data->client_data.model_materials));
		terrain_material = atlas_data->client_data.model_materials[tex_id];
		for (j = 0; j < 3; ++j)
		{
			if (terrain_material->detail_material_ids[j] < eaSize(&atlas_data->client_data.model_detail_material_names))
			{
				detail_material_names[i*3+j] = atlas_data->client_data.model_detail_material_names[terrain_material->detail_material_ids[j]];
				for (k = 0; k < eaSize(&primary_zmap->map_info.terrain_material_swaps); ++k)
				{
					// pooled strings
					if (detail_material_names[i*3+j] == primary_zmap->map_info.terrain_material_swaps[k]->orig_name)
						detail_material_names[i*3+j] = primary_zmap->map_info.terrain_material_swaps[k]->replace_name;
				}
			}
		}

		for (detail_count = 0; detail_count < 3; ++detail_count)
		{
			const char *detail_texture, *detail_normal_texture;

			if (!detail_material_names[i*3+detail_count])
				break;
			
			if (view_params && view_params->view_mode != TE_Regular || (atlas->height_map && atlas->height_map->hide_detail_in_editor))
				detail_material_pointers[i*3+detail_count] = materialFind("TerrainBlank", WL_FOR_WORLD);
			else
				detail_material_pointers[i*3+detail_count] = materialFind(detail_material_names[i*3+detail_count], WL_FOR_WORLD);

			detail_texture = materialGetStringValue(detail_material_pointers[i*3+detail_count], s_DetailTexture);
			material_params->detail_textures[detail_count] = texLoadBasic(detail_texture, TEX_LOAD_IN_BACKGROUND, WL_FOR_WORLD);

			detail_normal_texture = materialGetStringValue(detail_material_pointers[i*3+detail_count], s_DetailNormalTexture);
			material_params->detail_normal_textures[detail_count] = texLoadBasic(detail_normal_texture, TEX_LOAD_IN_BACKGROUND, WL_FOR_WORLD);
		}

		if (detail_count == 3)
			material_params->material = materialFind("Terrain_3Detail_default", WL_FOR_TERRAIN);
		else if (detail_count == 2)
			material_params->material = materialFind("Terrain_2Detail_default", WL_FOR_TERRAIN);
		else if (detail_count == 1)
			material_params->material = materialFind("Terrain_1Detail_default", WL_FOR_TERRAIN);
		else
			material_params->material = materialFind("Terrain_0Detail_default", WL_FOR_TERRAIN);

		if (eaSize(&material_params->mat_named_consts) == 0)
		{
			for (j = 0; j < TER_GFX_MAT_NAMED_CONST_CNT; ++j)
			{
				MaterialNamedConstant *new_const = StructCreate(parse_MaterialNamedConstant);
				eaPush(&material_params->mat_named_consts, new_const);
			}
		}

		ANALYSIS_ASSUME(material_params->mat_named_consts);

		material_params->mat_named_consts[TER_GFX_UV_SCALE_1]->name = s_UVScale1;
		material_params->mat_named_consts[TER_GFX_UV_SCALE_2]->name = s_UVScale2;
		material_params->mat_named_consts[TER_GFX_DETAIL_SCALE]->name = s_DetailUVParams;
		material_params->mat_named_consts[TER_GFX_SPECULARITY]->name = s_SpecularValueIn;
		material_params->mat_named_consts[TER_GFX_SPECULAR_EXPONENT]->name = s_SpecularExponentIn;
		material_params->mat_named_consts[TER_GFX_DETAIL_SWITCH_SELECT1]->name = s_DetailSwitchSelect1;
		material_params->mat_named_consts[TER_GFX_DETAIL_SWITCH_SELECT2]->name = s_DetailSwitchSelect2;
		material_params->mat_named_consts[TER_GFX_DETAIL_SWITCH_SELECT3]->name = s_DetailSwitchSelect3;

		for (j = 0; j < detail_count; ++j)
		{
			Vec4 val;

			val[0] = val[1] = 16.f;
			materialGetVecValue(detail_material_pointers[i*3+j], s_Scale, val);
			if (j == 0)
				copyVec2(val, &material_params->mat_named_consts[TER_GFX_UV_SCALE_1]->value[0]);
			else if (j == 1)
				copyVec2(val, &material_params->mat_named_consts[TER_GFX_UV_SCALE_1]->value[2]);
			else
				copyVec2(val, &material_params->mat_named_consts[TER_GFX_UV_SCALE_2]->value[0]);

			val[0] = 0;
			materialGetVecValue(detail_material_pointers[i*3+j], s_SpecularValue, val);
			material_params->mat_named_consts[TER_GFX_SPECULARITY]->value[j] = val[0];

			val[0] = 0.0625f;
			materialGetVecValue(detail_material_pointers[i*3+j], s_SpecularExponent, val);
			material_params->mat_named_consts[TER_GFX_SPECULAR_EXPONENT]->value[j] = val[0];

			if (detail_count > 1)
			{
				zeroVec4(material_params->mat_named_consts[TER_GFX_DETAIL_SWITCH_SELECT1+j]->value);
				if (terrain_material->color_idxs[j] < 4)
					material_params->mat_named_consts[TER_GFX_DETAIL_SWITCH_SELECT1+j]->value[terrain_material->color_idxs[j]] = 1;
			}
		}

		if (view_params && view_params->view_mode != TE_Regular)
		{
			setVec4(material_params->mat_named_consts[TER_GFX_UV_SCALE_1]->value, 16.f, 16.f, 16.f, 16.f);
			setVec4(material_params->mat_named_consts[TER_GFX_UV_SCALE_2]->value, 16.f, 16.f, 16.f, 16.f);
		}

		if (g_pchColorTexOverride)
		{
			BasicTexture * pNoiseTex;
			pNoiseTex = texLoadBasic(g_pchColorTexOverride, TEX_LOAD_IN_BACKGROUND, WL_FOR_WORLD);
			ADD_MATERIAL_TEXTURE(material_params->texture_list, s_Terrain, s_ColorTexture, pNoiseTex);
		}
		else
		{
			ADD_MATERIAL_TEXTURE(material_params->texture_list, s_Terrain, s_ColorTexture, color_tex);
		}
		ADD_MATERIAL_TEXTURE(material_params->texture_list, s_Terrain, s_DetailTexture1, material_params->detail_textures[0]);
		ADD_MATERIAL_TEXTURE(material_params->texture_list, s_Terrain, s_DetailTexture2, material_params->detail_textures[1]);
		ADD_MATERIAL_TEXTURE(material_params->texture_list, s_Terrain, s_DetailTexture3, material_params->detail_textures[2]);
		ADD_MATERIAL_TEXTURE(material_params->texture_list, s_Terrain, s_DetailNormalTexture1, material_params->detail_normal_textures[0]);
		ADD_MATERIAL_TEXTURE(material_params->texture_list, s_Terrain, s_DetailNormalTexture2, material_params->detail_normal_textures[1]);
		ADD_MATERIAL_TEXTURE(material_params->texture_list, s_Terrain, s_DetailNormalTexture3, material_params->detail_normal_textures[2]);
	}

    {
        S32 width = atlas_data->client_data.color_texture.width;
        if(!width)
            width = color_tex->width;
#if _PS3
	    setVec4(gfx_data->shader_params[TSP_COLOR_AND_GEO_SIZE],
		    texcoord_scale * (width - 1.f) / width,
		    texcoord_offset[0],
		    texcoord_offset[1],
		    GRID_BLOCK_SIZE * heightmapAtlasGetLODSize(lod));
#else
	    setVec4(gfx_data->shader_params[TSP_COLOR_AND_GEO_SIZE],
		    texcoord_scale * (width - 1.f) / width,
		    texcoord_offset[0] + (0.5f / width),
		    texcoord_offset[1] + (0.5f / width),
		    GRID_BLOCK_SIZE * heightmapAtlasGetLODSize(lod));
#endif
    }

	PERFINFO_AUTO_STOP_FUNC();
}

static void gfxAtlasUpdateTextures(HeightMapAtlas *atlas)
{
	HeightMapAtlasData *atlas_data = atlas->data;
	HeightMapAtlasGfxData *gfx_data = atlas_data->client_data.gfx_data;

	updateDebugTextures();
	gfxAtlasUpdateMaterials(atlas);
	assert(gfx_data->material_params);
}

/********************************
   Edge-Effect Rendering
 ********************************/

static void gfxTerrainEdgeGetInfo(GeoRenderInfo *geo_render_info, HeightMap *height_map, int param, GeoRenderInfoDetails *details)
{
	char buf[64];
	int size = param & 0xfff;
	int edge = param >> 12;
	sprintf(buf, "TerrainEdge_%d_%d", size, edge);
	details->name = allocAddString(buf);
	details->filename = NULL;
	details->uv_density = -1;
}

static bool CALLBACK gfxTerrainEdgeGetData(GeoRenderInfo *geo_render_info, HeightMap *height_map, int param)
{
    int lod = height_map->level_of_detail;
	int size = GRID_LOD(lod);
	int edge = param >> 12;
	F32 texcoord_mult = 1 / (F32)(size-1);
	U8 *extra_data_ptr;
	U32 subobj_size = sizeof(int);
	int i, lod_diff;
	int tri_counter;
	int idxcount;
	U32 total,vert_total,tri_bytes,subobj_bytes;

	RdrVertexDeclaration *vdecl = rdrGetVertexDeclaration(geo_render_info->usage.bits[0]);
    TerrainBuffer *height_buffer = heightMapGetBuffer(height_map, TERRAIN_BUFFER_HEIGHT, height_map->loaded_level_of_detail);
    
	assert(geo_render_info->usage.iNumPrimaryStreams == 1);
    lod_diff = lod-height_map->loaded_level_of_detail;
 
	geo_render_info->tri_count = (size-1)*2;
	geo_render_info->vert_count = size*2;
	geo_render_info->subobject_count = 1;

	idxcount = geo_render_info->tri_count * 3;
	tri_bytes = idxcount * sizeof(U16);
	tri_bytes += tri_bytes % 4; // align end to 4 byte boundary so floats can be copied
	vert_total = rdrGeoGetTotalVertSize(&geo_render_info->usage) * geo_render_info->vert_count;
	subobj_bytes = sizeof(int);
	total = vert_total + tri_bytes + subobj_bytes + subobj_bytes;

	geo_render_info->vbo_data_primary.data_size = total;
	extra_data_ptr = geo_render_info->vbo_data_primary.data_ptr = memrefAlloc(total);

	geo_render_info->vbo_data_primary.tris = (U16 *)extra_data_ptr;
	extra_data_ptr += tri_bytes;

	tri_counter = 0;
	for (i=0; i<size-1; i++)
	{
		geo_render_info->vbo_data_primary.tris[tri_counter * 3 + 0] = i*2;
		geo_render_info->vbo_data_primary.tris[tri_counter * 3 + 1] = i*2 + 1;
		geo_render_info->vbo_data_primary.tris[tri_counter * 3 + 2] = i*2 + 3;
		geo_render_info->vbo_data_primary.tris[tri_counter * 3 + 3] = i*2;
		geo_render_info->vbo_data_primary.tris[tri_counter * 3 + 4] = i*2 + 3;
		geo_render_info->vbo_data_primary.tris[tri_counter * 3 + 5] = i*2 + 2;
		tri_counter += 2;
	}
	assert(tri_counter == geo_render_info->tri_count);

	geo_render_info->vbo_data_primary.vert_data = extra_data_ptr;
	for (i = 0; i < geo_render_info->vert_count; i++)
	{
        F32 y;
		U32 x = i / 2, z;
		F32 *vert = (F32*)(extra_data_ptr + vdecl->position_offset);
		F16 *texcoord = (F16*)(extra_data_ptr + vdecl->texcoord_offset);
        Vec3 norm_vec;
        Vec3_Packed *norm = (Vec3_Packed*)(extra_data_ptr + vdecl->normal_offset);
        Vec3_Packed *tan = (Vec3_Packed*)(extra_data_ptr + vdecl->tangent_offset);
		Vec3_Packed *binorm = (Vec3_Packed*)(extra_data_ptr + vdecl->binormal_offset);

		if (edge == 0)
		{
            z = 0;
            y = height_buffer->data_f32[(x<<lod_diff)+(z<<lod_diff)*height_buffer->size];
			setVec3(vert, x * texcoord_mult * GRID_BLOCK_SIZE, y+(i%2)*40, (i%2));
			setVec2(texcoord, F32toF16(x * texcoord_mult), F32toF16(0));
		}
		else if (edge == 2)
		{
			z = (size-1);
			y = height_buffer->data_f32[(x<<lod_diff)+(z<<lod_diff)*height_buffer->size];
			setVec3(vert, x * texcoord_mult * GRID_BLOCK_SIZE, y+(i%2)*40, z * texcoord_mult * GRID_BLOCK_SIZE - (i%2));
			setVec2(texcoord, F32toF16(x * texcoord_mult), F32toF16(0));
		}
		else if (edge == 1)
		{
			z = 0;
			y = height_buffer->data_f32[(z<<lod_diff)+(x<<lod_diff)*height_buffer->size];
			setVec3(vert, (i%2), y+(i%2)*40, x * texcoord_mult * GRID_BLOCK_SIZE);
			setVec2(texcoord, F32toF16(x * texcoord_mult), F32toF16(0));
		}
		else if (edge == 3)
		{
            z = (size-1);
            y = height_buffer->data_f32[(z<<lod_diff)+(x<<lod_diff)*height_buffer->size];
			setVec3(vert, z * texcoord_mult * GRID_BLOCK_SIZE - (i%2), y+(i%2)*40, x * texcoord_mult * GRID_BLOCK_SIZE);
			setVec2(texcoord, F32toF16(x * texcoord_mult), F32toF16(0));
		}

        setVec3(norm_vec, 0.f, 1.f, 0.f);
        *norm = Vec3toPacked(norm_vec);
        *tan = Vec3toPacked(norm_vec);
		*binorm = Vec3toPacked(norm_vec);
         
		extra_data_ptr += vdecl->stride;
	}

	geo_render_info->vbo_data_primary.subobject_tri_bases = (int*)extra_data_ptr;
	extra_data_ptr += subobj_bytes;

	geo_render_info->vbo_data_primary.subobject_tri_bases[0] = 0;

	geo_render_info->vbo_data_primary.subobject_tri_counts = (int*)extra_data_ptr;
	extra_data_ptr += subobj_bytes;

	geo_render_info->vbo_data_primary.subobject_tri_counts[0] = geo_render_info->tri_count;

	return true;
}

static void CALLBACK gfxTerrainEdgeFreeData(GeoRenderInfo *geo_render_info, void *unused, int param)
{
	// Nothing to free
}

static GeoRenderInfo *getEdgeGeoRenderInfo(HeightMap *height_map, HeightMapAtlasGfxData *gfx_data, int edge)
{
	if (!gfx_data->edge_render_info[edge] || gfx_data->edge_needs_update)
	{
		RdrGeoUsage usage = {0};

		if (gfx_data->edge_render_info[edge])
			gfxFreeGeoRenderInfo(gfx_data->edge_render_info[edge], false);
        
		usage.bits[0] = usage.key = RUSE_POSITIONS|RUSE_TEXCOORDS|RUSE_NORMALS|RUSE_BINORMALS|RUSE_TANGENTS;
		usage.iNumPrimaryStreams = 1;
		usage.bHasSecondary = false;
        gfx_data->edge_render_info[edge] = gfxCreateGeoRenderInfo(gfxTerrainEdgeGetData, gfxTerrainEdgeFreeData, gfxTerrainEdgeGetInfo, false, height_map, (edge << 12), usage, WL_FOR_TERRAIN, true);
    }

	return gfx_data->edge_render_info[edge];
}


/********************************
   Draw Functions
 ********************************/

static void drawHeightMapEdge(HeightMap *height_map, HeightMapAtlasGfxData *gfx_data, int side, int color)
{
	GfxGlobalDrawParams *gdraw = &gfx_state.currentAction->gdraw;
	U32 LOD = height_map->level_of_detail;
	U32 size = GRID_LOD(LOD);
	bool needs_update = false;
	GeoRenderInfo *info;
	GeoHandle geo_handle;

	info = getEdgeGeoRenderInfo(height_map, gfx_data, side);
	geo_handle = gfxGeoDemandLoad(info);
	if (!geo_handle)
		return;

	if (!g_TerrainEdgeMaterial)
        g_TerrainEdgeMaterial = materialFind("TerrainEdge_default", WL_FOR_TERRAIN);

	//////////////////////////////////////////////////////////////////////////
	// Draw it
	{
		RdrDrawableGeo *geo_draw;
		RdrAddInstanceParams instance_params={0};
		RdrLightParams light_params;
		RdrInstancePerDrawableData per_drawable_data={0};

		geo_draw = rdrDrawListAllocGeo(gdraw->draw_list, RTYPE_MODEL, NULL, 1, 0, 0);

		if (!geo_draw)
			return;

		geo_draw->geo_handle_primary = geo_handle;
		instance_params.per_drawable_data = &per_drawable_data;

		RDRALLOC_SUBOBJECT_PTRS(instance_params, 1);
		instance_params.subobjects[0] = rdrDrawListAllocSubobject(gdraw->draw_list, (size-1)*2);
		gfxDemandLoadMaterialAtQueueTimeEx(instance_params.subobjects[0], g_TerrainEdgeMaterial, gdraw->scene_texture_swaps, NULL, NULL, NULL, false, per_drawable_data.instance_param, 0.0f, TEX_DEMAND_LOAD_DEFAULT_UV_DENSITY);

		switch (color)
		{
		xcase 1:
			setVec4(instance_params.instance.color, 1.f, 0.7f, 2.f, 1.f); // Gold / Selected
		xcase 2:
			setVec4(instance_params.instance.color, 1.f, 0.0f, 0.0f, 1.f); // Red / View Mode
		xcase 3:
			setVec4(instance_params.instance.color, 0.0f, 1.f, 0.0f, 1.f); // Green / Edit Mode
		xcase 4:
			setVec4(instance_params.instance.color, 0.0f, 0.0f, 1.f, 1.f); // Blue / Locked edge
		};

		copyMat3(unitmat, instance_params.instance.world_matrix);
		addVec3(height_map->bounds.offset, gdraw->pos_offset, instance_params.instance.world_matrix[3]);
		addVec3(height_map->bounds.world_mid, gdraw->pos_offset, instance_params.instance.world_mid);
		instance_params.distance_offset = height_map->bounds.radius;
		instance_params.frustum_visible = gdraw->visual_frustum_bit;
		setVec3same(instance_params.ambient_multiplier, 1);

		instance_params.light_params = &light_params;
		gfxGetObjectLightsUncached(&light_params, NULL, instance_params.instance.world_mid, height_map->bounds.radius, false);

		instance_params.wireframe = gdraw->global_wireframe;
		rdrDrawListAddGeoInstance(gdraw->draw_list, geo_draw, &instance_params, RST_AUTO, ROC_EDITOR_ONLY, true);
    }
}

int sortTerrain = 1;
AUTO_CMD_INT(sortTerrain,sortTerrain);

__forceinline static bool checkAtlasBoundsNotVisible(HeightMapAtlasGfxData *gfx_data, const Vec3 bounds_min, const Vec3 bounds_max, RdrDrawListPassData *pass, int clipped, bool is_leaf, GfxGlobalDrawParams *gdraw)
{
	if (pass == gdraw->visual_pass && gdraw->occlusion_buffer)
	{
		int nearClipped;
		U8 inherited_bits = 0;
		Vec4_aligned eye_bounds[8];

		mulBounds(bounds_min, bounds_max, pass->viewmat, eye_bounds);

		nearClipped = frustumCheckBoxNearClipped(pass->frustum, eye_bounds);
		if (nearClipped == 2)
		{
			zoSetOccluded(gdraw->occlusion_buffer, &gfx_data->occlusion_data.last_update_time, &gfx_data->occlusion_data.occluded_bits);
			return true;
		}
		else if (frustumCheckBoxNearClippedInView(pass->frustum,bounds_min,bounds_max,pass->viewmat))
		{
			return false;
		}
		// only test non-leaf nodes if they are not clipped by the near plane
		else if ((!nearClipped || is_leaf) && !zoTestBounds(gdraw->occlusion_buffer, eye_bounds, nearClipped, &gfx_data->occlusion_data.last_update_time, &gfx_data->occlusion_data.occluded_bits, &inherited_bits, NULL, NULL))
		{
			return true;
		}

		return false;
	}

	return clipped != FRUSTUM_CLIP_NONE && !frustumCheckBoxWorld(pass->frustum, clipped, bounds_min, bounds_max, NULL, false);
}

static HeightMapAtlas * searchAtlasForCollision(HeightMapAtlas *atlas, Model * collision_model, WorldCollisionEntry * collision_entry)
{
	int is_leaf;
	HeightMapAtlas * search_result = NULL;

	if (atlas->data)
	{
		if (atlas->data->collision_entry == collision_entry)
			return atlas;
		if (atlas->data->collision_model == collision_model)
			return atlas;
	}

	is_leaf = atlas->atlas_active;
	if (!is_leaf)
	{
		if (atlas->children[0])
			search_result = searchAtlasForCollision(atlas->children[0], collision_model, collision_entry);
		if (!search_result && atlas->children[1])
			search_result = searchAtlasForCollision(atlas->children[1], collision_model, collision_entry);
		if (!search_result && atlas->children[2])
			search_result = searchAtlasForCollision(atlas->children[2], collision_model, collision_entry);
		if (!search_result && atlas->children[3])
			search_result = searchAtlasForCollision(atlas->children[3], collision_model, collision_entry);
	}

	return search_result;
}

static const U8 terrain_lod_color_code[MAX_TERRAIN_LODS][3] =
{
	{ 128, 128, 128 },
	{ 255, 0, 0 },
	{ 255, 127, 0 },
	{ 255, 255, 0 },
	{ 0, 255, 0 },
	{ 0, 255, 255 },
	{ 0, 127, 255 },
	{ 0, 0, 255 }
};

static __inline Color atlasDebugLODColor(int lod)
{
	return CreateColorRGB(terrain_lod_color_code[lod][0],terrain_lod_color_code[lod][1],terrain_lod_color_code[lod][2]);
}

static void drawDebugAtlasGraphics(HeightMapAtlas *atlas, bool root_node)
{
	Vec3 bounds_min, bounds_max, bounds_mid, debug_color3;
	Color atlas_debug_color;

	switch(atlas->load_state) {
	case WCS_LOADED:
		if (root_node)
			atlas_debug_color = CreateColorRGB(0,128,0);
		else
		{
			atlas_debug_color = atlasDebugLODColor(atlas->lod);
		}
		break;
	case WCS_LOADING_BG:
	case WCS_LOADING_FG:
		{
			int blink = 127 + round(128 * sin(gfx_state.client_loop_timer*PI*4));
			atlas_debug_color = CreateColorRGB(255,blink,blink);
		}
		break;
	default:
		atlas_debug_color = CreateColorRGB(128,0,128);
	}

	//if (eaSize(&atlas->data->client_data.world_drawables)) {
	//if (eaSize(&atlas->world_objects)) {
	//	atlas_debug_color = CreateColorRGB(0,0,128);	// Turn blue if contains an object.  Don't check in since this would mess up other debug colors by overriding them.
	//}

	debug_color3[0] = atlas_debug_color.r / 255.0;
	debug_color3[1] = atlas_debug_color.g / 255.0;
	debug_color3[2] = atlas_debug_color.b / 255.0;

	setVec3(bounds_min, (F32)atlas->local_pos[0]*GRID_BLOCK_SIZE, atlas->min_height, (F32)atlas->local_pos[1]*GRID_BLOCK_SIZE);
	setVec3(bounds_max, bounds_min[0]+heightmapAtlasGetLODSize(atlas->lod)*GRID_BLOCK_SIZE, 
		atlas->max_height, 
		bounds_min[2]+heightmapAtlasGetLODSize(atlas->lod)*GRID_BLOCK_SIZE);

	setVec3(bounds_mid, (bounds_min[0] + bounds_max[0])*0.5f, 
		(bounds_min[1] + bounds_max[1])*0.5f, 
		(bounds_min[2] + bounds_max[2])*0.5f);
	gfxDrawBox3D(bounds_min, bounds_max, unitmat, atlas_debug_color, 1);

	// For debug display purposes.
	if (0)
	{
		// Draws an object at the center of the top of the bounding box.  Currently only draws a teapot but perhaps later could be replaced by something to signify what lod level it is at.
		HeightMapAtlasData *data = atlas->data;
		Vec3	model_placement;
		SingleModelParams smparams = {0};
		Model	*model;

		smparams.world_mat[0][0] = smparams.world_mat[1][1] = smparams.world_mat[2][2] = (int)(1 << (atlas->lod));

		model = modelFind("mated_primitives_teapot", true, WL_FOR_WORLD);
		setVec3(model_placement, ((F32)atlas->local_pos[0] * GRID_BLOCK_SIZE)  + (heightmapAtlasGetLODSize(atlas->lod) * GRID_BLOCK_SIZE / 2),
			atlas->max_height,
			((F32)atlas->local_pos[1] * GRID_BLOCK_SIZE) +   + (heightmapAtlasGetLODSize(atlas->lod) * GRID_BLOCK_SIZE / 2));
		copyVec3(model_placement,smparams.world_mat[3]);
		smparams.model = model;
		smparams.dist = -1;
		smparams.wireframe = gfx_state.wireframe;
		copyVec3(debug_color3,smparams.color);
		smparams.eaNamedConstants = gfxMaterialStaticTintColorArray(debug_color3);
		smparams.alpha = 255;
		smparams.force_lod = true;
		smparams.lod_override = 0;
		gfxQueueSingleModelTinted(&smparams, -1);
	}

}

static void drawDebugAtlas(HeightMapAtlas *atlas, bool root_node)
{
	int i;
	for (i = 0; i < 4; i++) {
		if (atlas->children[i] && atlas->children[i]->load_state != WCS_CLOSED) {
			drawDebugAtlas(atlas->children[i],false);
		}
	}
	drawDebugAtlasGraphics(atlas,root_node);
}

void gfxSetupClusterTextureSwaps(HeightMapAtlasGfxData *gfx_data, const char* fileprefix) {
	char texFileName[MAX_PATH];
	// Setup texture swaps here
	{
		sprintf(texFileName,"%s_D.wtex", fileprefix);
		eaPush(&gfx_data->cluster_tex_swaps,white_tex);
		eaPush(&gfx_data->cluster_tex_swaps,texRegisterDynamic(texFileName));
	}
	{
		sprintf(texFileName,"%s_S.wtex", fileprefix);
		eaPush(&gfx_data->cluster_tex_swaps,black_tex);
		eaPush(&gfx_data->cluster_tex_swaps,texRegisterDynamic(texFileName));
	}
	{
		sprintf(texFileName,"%s_N.wtex", fileprefix);
		eaPush(&gfx_data->cluster_tex_swaps,texFindAndFlag( "Default_Flatnormal_Nx", false, WL_FOR_WORLD ));
		eaPush(&gfx_data->cluster_tex_swaps,texRegisterDynamic(texFileName));
	}
}

static void drawAtlas(HeightMapAtlas *atlas, int trivial_accept, int trivial_reject, bool drawing_editable)
{
	GfxGlobalDrawParams *gdraw = &gfx_state.currentAction->gdraw;
	HeightMapAtlasData *atlas_data = atlas->data;
	HeightMapAtlasGfxData *gfx_data = SAFE_MEMBER(atlas_data, client_data.gfx_data);
	Vec3 bounds_mid, bounds_min, bounds_max;
	int clipped;
	int entry_frustum_visible = gdraw->all_frustum_bits;
	F32 radius;
	F32 fTexDist;
	ModelToDraw models[NUM_MODELTODRAWS];
	ModelToDraw lightModels[NUM_MODELTODRAWS];
	ModelToDraw cluster_models[NUM_MODELTODRAWS];
	int numLightModels = 0;
	ModelLOD *model, *cluster_model = NULL;
	int i, k, max_lod;
	bool is_leaf;

	if (!atlas_data || atlas->load_state != WCS_LOADED)
		return;

	if (!gfx_data)
	{
		gfx_data = calloc(1, sizeof(HeightMapAtlasGfxData));
		atlas_data->client_data.gfx_data = gfx_data;
		atlas_data->client_data.gfx_free_func = heightMapAtlasGfxDataFree;
	}

	model = modelGetLOD(atlas_data->client_data.draw_model, 0);
	if (atlas_data->client_data.draw_model_cluster)
		cluster_model = modelGetLOD(atlas_data->client_data.draw_model_cluster, 0);

    if (atlas_data->client_data.color_texture.data || !gfx_data->material_count || gfx_data->needs_texture_update)
    {
		gfx_data->needs_texture_update = true;
		if (!model || model && !model->data)
		{
			// do nothing, wait for model to be loaded
		}
		else
        {
			if(model->vert_count) {
				gfxAtlasUpdateTextures(atlas);
				gfx_data->needs_texture_update = false;
			}
        }
	}

	setVec3(bounds_min, (F32)atlas->local_pos[0]*GRID_BLOCK_SIZE, atlas->min_height, (F32)atlas->local_pos[1]*GRID_BLOCK_SIZE);
	setVec3(bounds_max, bounds_min[0]+heightmapAtlasGetLODSize(atlas->lod)*GRID_BLOCK_SIZE, 
		atlas->max_height, 
		bounds_min[2]+heightmapAtlasGetLODSize(atlas->lod)*GRID_BLOCK_SIZE);

	setVec3(bounds_mid, (bounds_min[0] + bounds_max[0])*0.5f, 
		(bounds_min[1] + bounds_max[1])*0.5f, 
		(bounds_min[2] + bounds_max[2])*0.5f);

	radius = distance3(bounds_min, bounds_mid);

	INC_FRAME_COUNT(terrain_hits);

	is_leaf = atlas->atlas_active || (force_draw_lod != -1 && force_draw_lod == atlas->lod);

	for (k = 0; k < gdraw->pass_count; ++k)
	{
		RdrDrawListPassData *pass = gdraw->passes[k];

		if (trivial_accept & pass->frustum_set_bit)
		{
			// trivially accepted
			entry_frustum_visible |= pass->frustum_set_bit;
		}
		else if (trivial_reject & pass->frustum_set_bit)
		{
			// trivially rejected
			entry_frustum_visible &= pass->frustum_clear_bits;
		}
		else if (entry_frustum_visible & pass->frustum_set_bit)
		{
			INC_FRAME_COUNT(terrain_tests);
			if (!(clipped = frustumCheckSphereWorld(pass->frustum, bounds_mid, radius)))
			{
				INC_FRAME_COUNT(terrain_culls);
				trivial_reject |= pass->frustum_set_bit;
				entry_frustum_visible &= pass->frustum_clear_bits;
				continue;
			}

			INC_FRAME_COUNT(terrain_tests);
			if (checkAtlasBoundsNotVisible(gfx_data, bounds_min, bounds_max, pass, clipped, is_leaf, gdraw))
			{
				INC_FRAME_COUNT(terrain_zoculls);
				entry_frustum_visible &= pass->frustum_clear_bits;
				continue;
			}

			if (clipped == FRUSTUM_CLIP_NONE)
				trivial_accept |= pass->frustum_set_bit;
		}
	}

	if (gfx_state.debug.no_clip_terrain)
	{
		trivial_accept = gdraw->all_frustum_bits;
		trivial_reject = 0;
		entry_frustum_visible = gdraw->all_frustum_bits;
	}

	if (!(entry_frustum_visible & gdraw->visual_frustum_bit))
	{
		// Debug
		if (terrain_state.show_terrain_bounds) {
			drawDebugAtlas(atlas, false);
		}
	}
	if (entry_frustum_visible == 0)
		return;

	if (!is_leaf)
	{
		if (atlas->children[0])
            drawAtlas(atlas->children[0], trivial_accept, trivial_reject, false);
		if (atlas->children[1])
            drawAtlas(atlas->children[1], trivial_accept, trivial_reject, false);
		if (atlas->children[2])
            drawAtlas(atlas->children[2], trivial_accept, trivial_reject, false);
		if (atlas->children[3])
            drawAtlas(atlas->children[3], trivial_accept, trivial_reject, false);
		return;
	}

	max_lod = 4 - round((1 - gfx_state.settings.terrainDetailLevel) * 10);
	max_lod = CLAMP(max_lod, 1, 3);

	if (force_draw_lod >= 0 && force_draw_lod != atlas->lod || 
		gfx_state.settings.terrainDetailLevel < 1 && atlas->lod >= max_lod ||
		atlas->hidden || !model)
		return;

	INC_FRAME_COUNT(terrain_draw_hits);

    if (!gfxDemandLoadModel(atlas_data->client_data.draw_model, models, ARRAY_SIZE(models), 0, 1, 0, NULL, 0))
        return;
	if (atlas_data->client_data.draw_model_cluster)
	{
		if (!gfxDemandLoadModel(atlas_data->client_data.draw_model_cluster, cluster_models, ARRAY_SIZE(cluster_models), 0, 1, 0, NULL, 0))
			return;
	}


	// Load light models, if any.
	if(atlas_data->client_data.static_vertex_light_model) {
		numLightModels = gfxDemandLoadModel(atlas_data->client_data.static_vertex_light_model, lightModels, ARRAY_SIZE(lightModels), 0, 1, 0, NULL, 0);
	}

	if (gfx_data->needs_texture_update)
	{
		if (model && !model->data)
		{
			// model not loaded, can't update textures yet
			return;
		}
		else
		{
			gfxAtlasUpdateTextures(atlas);
			gfx_data->needs_texture_update = false;
		}
	}

	if (!terrain_state.terrain_occlusion_disable && gdraw->occlusion_buffer)
    {
		Model *occlusion_model = atlas_data->client_data.occlusion_model;
		ModelLOD *model_lod = occlusion_model ? modelLODLoadAndMaybeWait(occlusion_model, 0, false) : NULL;
		if (model_lod)
        {
            Mat4 occ_mat;
			assert(model_lod);
            identityMat4(occ_mat);
            copyVec3(bounds_min, occ_mat[3]);
            occ_mat[3][1] = 0.f;
            zoCheckAddOccluder(gdraw->occlusion_buffer, model_lod, occ_mat, 0xffffffff, false);

            if (terrain_state.show_terrain_occlusion)
            {
                SingleModelParams smparams = { 0 };
                smparams.model = occlusion_model;
                copyMat4(occ_mat, smparams.world_mat);
                smparams.wireframe = 1;
                smparams.double_sided = false;
                smparams.unlit = false;
                smparams.alpha = 255;
                setVec3(smparams.color, 100.f, 100.f, 100.f);
                gfxQueueSingleModel(&smparams);
            }  
        }
    }

	if (!atlas_data->client_data.light_cache)
	{
		atlas_data->client_data.light_cache = gfxCreateDynLightCache(worldRegionGetGraphicsData(worldCellGetLightRegion(atlas->region)), 
																	 bounds_min, bounds_max, unitmat, 
																	 LCT_TERRAIN);
	}

	fTexDist = distance3(bounds_mid, gdraw->cam_pos) - radius;

	//////////////////////////////////////////////////////////////////////////
	// Draw it
	if (!terrain_state.show_terrain_occlusion && 
		(terrain_state.show_terrain_bounds < 2 || (atlas->local_pos[0] == debug_atlas_x && atlas->local_pos[1] == debug_atlas_z && atlas->lod == debug_atlas_lod_num)))
	{
		RdrDrawableGeo *geo_draw;
		RdrAddInstanceParams instance_params={0};

		assert(models[0].model->geo_render_info->subobject_count >= gfx_data->material_count);

		assert(gfx_data->material_count);
		assert(gfx_data->material_params);

		geo_draw = rdrDrawListAllocGeo(gdraw->draw_list, RTYPE_TERRAIN, models[0].model, gfx_data->material_count, TSP_COUNT, 0);

		if (geo_draw)
		{
			geo_draw->geo_handle_primary = models[0].geo_handle_primary;
			
			if(numLightModels) {
				// We have light models. Send in the lighting
				// information as the second vertex stream.

				// I think it would be neat if we didn't keep a whole second geo handle around for the extra vert data. [RMARR - 10/22/12]
				geo_draw->geo_handle_terrainlight = lightModels[0].geo_handle_primary;
			} else {
				geo_draw->geo_handle_terrainlight = 0;
			}

			instance_params.per_drawable_data = _alloca(geo_draw->subobject_count * sizeof(instance_params.per_drawable_data[0]));

			RDRALLOC_SUBOBJECTS(gdraw->draw_list, instance_params, models[0].model, i);

			for (i = 0; i < geo_draw->subobject_count; ++i)
			{
				TerrainMaterialParams *material_params = &gfx_data->material_params[i];
				gfxDemandLoadMaterialAtQueueTimeEx(instance_params.subobjects[i], material_params->material, gdraw->scene_texture_swaps, 
					material_params->mat_named_consts, material_params->texture_list, NULL, false, instance_params.per_drawable_data[i].instance_param, fTexDist, models[0].model->uv_density);
			}

			setVec4same(instance_params.instance.color, 1);
			setVec3same(instance_params.ambient_multiplier, 1);

			copyMat3(unitmat, instance_params.instance.world_matrix);
			setVec3(instance_params.instance.world_matrix[3], atlas->local_pos[0]*GRID_BLOCK_SIZE, 0, atlas->local_pos[1]*GRID_BLOCK_SIZE);
			addVec3(instance_params.instance.world_matrix[3], gdraw->pos_offset, instance_params.instance.world_matrix[3]);

			if (sortTerrain)
				addVec3(bounds_mid, gdraw->pos_offset, instance_params.instance.world_mid);

			instance_params.distance_offset = 1000.f;
			instance_params.frustum_visible = entry_frustum_visible;
			instance_params.wireframe = gdraw->global_wireframe;

			if (entry_frustum_visible & gdraw->visual_frustum_bit)
			{
				gfxUpdateDynLightCachePosition(atlas_data->client_data.light_cache, atlas->region->graphics_data, bounds_min, bounds_max, unitmat);
				instance_params.light_params = gfxDynLightCacheGetLights(atlas_data->client_data.light_cache);
				if (atlas->height_map && eaSize(&atlas_data->client_data.model_detail_material_names) > NUM_MATERIALS)
				{
					setVec3(instance_params.ambient_offset, 
						0.6f + 0.3f * qsin(gfx_state.client_loop_timer*4), 
						0.2f + 0.1f * qsin(gfx_state.client_loop_timer*4), 
						0.2f + 0.1f * qsin(gfx_state.client_loop_timer*4));
					subVec3(instance_params.ambient_offset, instance_params.light_params->ambient_light->ambient[RLCT_WORLD], instance_params.ambient_offset);
				}
			}

			assert(atlas->corner_lods[0] >= atlas->lod && atlas->corner_lods[0] <= atlas->lod+1);
			assert(atlas->corner_lods[1] >= atlas->lod && atlas->corner_lods[1] <= atlas->lod+1);
			assert(atlas->corner_lods[2] >= atlas->lod && atlas->corner_lods[2] <= atlas->lod+1);
			assert(atlas->corner_lods[3] >= atlas->lod && atlas->corner_lods[3] <= atlas->lod+1);

			memcpy(&geo_draw->vertex_shader_constants[0][0], &gfx_data->shader_params[0][0], sizeof(Vec4) * TSP_COUNT);

			setVec4(geo_draw->vertex_shader_constants[TSP_LODS], 
				atlas->corner_lods[0]-atlas->lod, atlas->corner_lods[1]-atlas->lod,
				atlas->corner_lods[2]-atlas->lod, atlas->corner_lods[3]-atlas->lod);

			rdrDrawListAddGeoInstance(gdraw->draw_list, geo_draw, &instance_params, RST_AUTO, ROC_TERRAIN, true);
			gfxGeoIncrementUsedCount(models[0].model->geo_render_info, models[0].model->geo_render_info->subobject_count, true);
		}

		// Debug
		if (terrain_state.show_terrain_bounds) {
			drawDebugAtlas(atlas, atlas->local_pos[0] == debug_atlas_x && atlas->local_pos[1] == debug_atlas_z && atlas->lod == debug_atlas_lod_num);
		}
	}

	for (i = 0; i < gdraw->pass_count; ++i)
	{
		if (entry_frustum_visible & gdraw->passes[i]->frustum_set_bit)
			frustumUpdateBounds(gdraw->passes[i]->frustum, bounds_min, bounds_max);
	}
}

#ifndef NO_EDITORS
static void gfxHeightmapDrawSource(TerrainEditorSourceLayer *layer, HeightMapTracker *tracker)
{
	HeightMap *height_map = tracker->height_map;
	HeightMapAtlasData *atlas_data = height_map->data;
	HeightMapAtlasGfxData *gfx_data = atlas_data->client_data.gfx_data;
	bool using_visualization = false, needs_texture_update = height_map->texture_time != height_map->texture_update_time;
	int i;

	if (!tracker->atlas)
	{
		tracker->atlas = StructCreate(parse_HeightMapAtlas);
		copyVec2(tracker->world_pos, tracker->atlas->local_pos);
		tracker->atlas->atlas_active = true;
		tracker->atlas->height_map = height_map;
		tracker->atlas->region = worldGetWorldRegionByPos(height_map->bounds.world_mid);
	}

	tracker->atlas->min_height = height_map->bounds.local_min[1];
	tracker->atlas->max_height = height_map->bounds.local_max[1];
	tracker->atlas->data = height_map->data;
	tracker->atlas->load_state = WCS_LOADED;

	if (!gfx_data)
	{
		gfx_data = calloc(1, sizeof(HeightMapAtlasGfxData));
		atlas_data->client_data.gfx_data = gfx_data;
		atlas_data->client_data.gfx_free_func = heightMapAtlasGfxDataFree;
	}

	if (terrain_state.view_params && terrain_state.view_params->view_mode != TE_Regular)
		using_visualization = true;

	if (height_map->height_time != height_map->height_update_time)
	{
		HeightMap *height_maps[3][3] = {0};
		static HeightMap **map_cache = NULL;

		needs_texture_update = true;

		tempModelFree(&atlas_data->client_data.draw_model);
		terrainLock();
		for (i = 0; i < eaSize(&layer->heightmap_trackers); i++)
			eaPush(&map_cache, layer->heightmap_trackers[i]->height_map);
		height_maps[1][1] = height_map;
		heightMapGetNeighbors(height_maps, map_cache);
		atlas_data->client_data.draw_model = heightMapGenerateModel(height_maps, terrain_state.source_data->material_table, layer->material_lookup, layer->disable_normals);
		eaClear(&map_cache);
		terrainUnlock();

		if (!layer->disable_normals)
			tempModelFree(&atlas_data->client_data.occlusion_model);

		gfxFreeDynLightCache(atlas_data->client_data.light_cache);
		atlas_data->client_data.light_cache = NULL;
		gfx_data->edge_needs_update = true;

		height_map->height_update_time = height_map->height_time;

		// We're going to need new vertex lighting calculations now.
		if(tracker->atlas)
			tracker->atlas->needs_static_light_update = true;
	}

	if (!atlas_data->client_data.occlusion_model)
		layerHeightmapCalcOcclusion(height_map);

	// update color texture from height map buffers
	if (needs_texture_update || 
		using_visualization && height_map->visualization_time != height_map->visualization_update_time)
	{
		int lod = height_map->level_of_detail;
		TerrainBuffer *buffer;

		if ((buffer = heightMapGetBuffer(height_map, TERRAIN_BUFFER_COLOR, GET_COLOR_LOD(lod))) != NULL)
		{
			F32 color_shift = layer->color_shift;
			int color_size = buffer->size;

			atlas_data->client_data.color_texture.has_alpha = false;
			atlas_data->client_data.color_texture.is_dxt = false;
			atlas_data->client_data.color_texture.width = color_size;
			atlas_data->client_data.color_texture.data = memrefAlloc(color_size * color_size * 3);
			if (color_shift)
			{
				for (i = 0; i < color_size * color_size; ++i)
				{
					Vec3 rgb, hsv;
					rgb[0] = buffer->data_byte[i*3 + 0];
					rgb[1] = buffer->data_byte[i*3 + 1];
					rgb[2] = buffer->data_byte[i*3 + 2];
					rgbToHsv(rgb, hsv);
					hsv[0] -= color_shift;
					if (hsv[0] >= 360.0f)
						hsv[0] -= 360.0f;
					else if (hsv[0] < 0.0f)
						hsv[0] += 360.0f;
					hsvToRgb(hsv, rgb);
					atlas_data->client_data.color_texture.data[i*3 + 0] = rgb[0];
					atlas_data->client_data.color_texture.data[i*3 + 1] = rgb[1];
					atlas_data->client_data.color_texture.data[i*3 + 2] = rgb[2];
				}
			}
			else
			{
				memcpy(atlas_data->client_data.color_texture.data, buffer->data_byte, color_size * color_size * 3);
			}

			//Draw Visualization if needed
			if (terrain_state.view_params && terrain_state.view_params->view_mode != TE_Regular)
			{
				switch(terrain_state.view_params->view_mode)
				{
				case TE_Object_Density:
					if((buffer = heightMapGetBuffer(height_map, TERRAIN_BUFFER_OBJECTS, height_map->loaded_level_of_detail)) != NULL)
						gfxHeightMapUseVisualizationObjectDensity(layer, terrain_state.view_params, height_map, buffer, lod, atlas_data->client_data.color_texture.data, color_size);
					break;
				case TE_Material_Weight:
					if((buffer = heightMapGetBuffer(height_map, TERRAIN_BUFFER_MATERIAL, height_map->loaded_level_of_detail)) != NULL)
						gfxHeightMapUseVisualizationMaterialWeight(layer, terrain_state.view_params, height_map, buffer, lod, atlas_data->client_data.color_texture.data, color_size);
					break;
				case TE_Soil_Depth:
					if((buffer = heightMapGetBuffer(height_map, TERRAIN_BUFFER_SOIL_DEPTH, height_map->loaded_level_of_detail)) != NULL)
						gfxHeightMapUseVisualizationSoilDepth(terrain_state.view_params, height_map, buffer, lod, atlas_data->client_data.color_texture.data, color_size);
					break;
				case TE_Extreme_Angles:
					if((buffer = heightMapGetBuffer(height_map, TERRAIN_BUFFER_NORMAL, lod)) != NULL)
						gfxHeightMapUseVisualizationExtremeAngles(terrain_state.view_params, height_map, buffer, lod, atlas_data->client_data.color_texture.data, color_size);
					break;
				case TE_Grid:
					gfxHeightMapUseVisualizationGrid(terrain_state.view_params, height_map, lod, atlas_data->client_data.color_texture.data, color_size);
					break;
				case TE_Filters:
					if(terrain_state.view_params->filter_cb)
						terrain_state.view_params->filter_cb(height_map, atlas_data->client_data.color_texture.data, color_size, GET_COLOR_LOD(lod));
					break;
				case TE_Selection:
					buffer = heightMapGetBuffer(height_map, TERRAIN_BUFFER_SELECTION, height_map->loaded_level_of_detail);
					gfxHeightMapUseVisualizationSelection(terrain_state.view_params, height_map, buffer, lod, atlas_data->client_data.color_texture.data, color_size);
					break;
				case TE_Shadow:
					if (buffer = heightMapGetBuffer(height_map, TERRAIN_BUFFER_SHADOW, height_map->loaded_level_of_detail))
						gfxHeightMapUseVisualizationShadow(terrain_state.view_params, height_map, buffer, lod, atlas_data->client_data.color_texture.data, color_size);
					break;
				case TE_DesignAttr:
					if((buffer = heightMapGetBuffer(height_map, TERRAIN_BUFFER_ATTR, height_map->loaded_level_of_detail)) != NULL)
						gfxHeightMapUseVisualizationDesignAttr(terrain_state.view_params, height_map, buffer, lod, atlas_data->client_data.color_texture.data, color_size);
					break;
				}
			}
		}
		else
		{
			assert(false);
		}

		height_map->visualization_update_time = height_map->visualization_time;
		height_map->texture_update_time = height_map->texture_time;
	}

	if (gfx_state.hide_detail_in_editor != height_map->hide_detail_in_editor)
	{
		height_map->hide_detail_in_editor = gfx_state.hide_detail_in_editor;
		gfx_data->needs_texture_update = true;
	}

	if(tracker->atlas && tracker->atlas->needs_static_light_update) {
		// Either the lighting or height map have changed in the
		// editor. We need to recompute the vertex lights.
		
		// Get rid of the old vertex lights model, if it exists.
		if(tracker->atlas->data->client_data.static_vertex_light_model) {
			tempModelFree(&tracker->atlas->data->client_data.static_vertex_light_model);
		}

		if(tracker->atlas->region) {

			// Check static terrain lighting flag on the ZoneMap.
			ZoneMap *zmap = worldRegionGetZoneMap(tracker->atlas->region);
			ZoneMapInfo *zmapInfo = zmapGetInfo(zmap);
			if(zmapInfoGetTerrainStaticLighting(zmapInfo)) {

				// Recompute the lights.
				tracker->atlas->data->client_data.static_vertex_light_model = computeAtlasVertexLightsSingleModel(tracker->atlas, tracker->atlas->region);
			}
		}

		tracker->atlas->needs_static_light_update = false;
	}

	//////////////////////////////////////////////////////////////////////////
	// draw
	drawAtlas(tracker->atlas, 0, 0, true);

	if (height_map->edge_color[0] > 0)
		drawHeightMapEdge(height_map, gfx_data, 0, height_map->edge_color[0]);
	if (height_map->edge_color[1] > 0)
		drawHeightMapEdge(height_map, gfx_data, 1, height_map->edge_color[1]);
	if (height_map->edge_color[2] > 0)
		drawHeightMapEdge(height_map, gfx_data, 2, height_map->edge_color[2]);
	if (height_map->edge_color[3] > 0)
		drawHeightMapEdge(height_map, gfx_data, 3, height_map->edge_color[3]);

	if (gfx_data)
		gfx_data->edge_needs_update = false;



}
#endif

GfxLightCacheBase * gfxFindTerrainLightCache(Model * collision_model, WorldCollisionEntry * collision_entry)
{
	const GfxGlobalDrawParams *gdraw = &gfx_state.currentAction->gdraw;
	int i, j;
	HeightMapAtlas * search_result = NULL;
	GfxLightCacheBase * light_cache = NULL;

	if (!gdraw || !eaSize(&gdraw->regions))
		return NULL;

	for (i = 0; i < eaSize(&gdraw->regions) && !search_result; ++i)
	{
		const WorldRegion *region = gdraw->regions[i];
		if (region->atlases)
		{
			gfxSetCurrentRegion(i);
			for (j = 0; j < eaSize(&region->atlases->root_atlases) && !search_result; j++)
				search_result = searchAtlasForCollision(region->atlases->root_atlases[j], collision_model, collision_entry);
		}
	}
	
	if (search_result && search_result->data && search_result->data->client_data.light_cache)
		light_cache = &search_result->data->client_data.light_cache->base;

	return light_cache;
}

void gfxDrawTerrainCellsCombined(void)
{
	GfxGlobalDrawParams *gdraw = &gfx_state.currentAction->gdraw;
	int i, j;

	if (!gdraw || 
		!eaSize(&gdraw->regions) ||
        gfx_state.debug.no_draw_terrain ||
		gfx_state.currentRendererIndex != 0 ||
		!gfx_state.currentCameraView->can_see_outdoors)
	{
		return;
	}

	PERFINFO_AUTO_START_FUNC();
	PERFINFO_AUTO_PIX_START(__FUNCTION__);

	etlAddEvent(gfx_state.currentDevice->event_timer, "Queue terrain objects", ELT_CODE, ELTT_BEGIN);

	#ifndef NO_EDITORS
	{
		int idx;
		
		if (terrain_state.source_data)
		{
			for (i = 0; i < eaSize(&terrain_state.source_data->layers); i++)
			{
				TerrainEditorSourceLayer *layer = terrain_state.source_data->layers[i];
				if (layer->loaded)
				{
					if ((idx = eaFind(&gdraw->regions, zmapGetWorldRegionByName(NULL, layer->layer->region_name))) >= 0 &&
						eaSize(&layer->heightmap_trackers) > 0)
					{
						gfxSetCurrentRegion(idx);
						for (j = eaSize(&layer->heightmap_trackers) - 1; j >= 0; --j)
						{
							if (layer->heightmap_trackers[j])
								gfxHeightmapDrawSource(layer, layer->heightmap_trackers[j]);
						}
					}
				}
			}
		}
	}
	#endif

	for (i = 0; i < eaSize(&gdraw->regions); ++i)
	{
		WorldRegion *region = gdraw->regions[i];
		if (region->atlases)
		{
			gfxSetCurrentRegion(i);
			for (j = 0; j < eaSize(&region->atlases->root_atlases); j++)
				drawAtlas(region->atlases->root_atlases[j], 0, 0, false);
		}
	}

	gfxSetCurrentRegion(0);

	etlAddEvent(gfx_state.currentDevice->event_timer, "Queue terrain objects", ELT_CODE, ELTT_END);

	PERFINFO_AUTO_PIX_STOP();
	PERFINFO_AUTO_STOP_FUNC();
}

/********************************
   Memory Reporting
 ********************************/

typedef struct AtlasMemoryReport
{
	struct 
	{
		int geo_bytes, geo_vert_count, geo_video_mem_bytes;
		int occlusion_bytes, occ_vert_count;
		int collision_bytes, coll_vert_count;

		int color_src_bytes, color_tex_bytes;
	} data[2];

	int lod_count[ATLAS_MAX_LOD+1];
} AtlasMemoryReport;

void runAtlasMemoryReport(HeightMapAtlas *atlas, AtlasMemoryReport *report)
{
	HeightMapAtlasData *atlas_data = atlas->data;
	int active = atlas->atlas_active;

	report->lod_count[atlas->lod] += active;

	if (atlas_data)
	{
		if (atlas_data->client_data.draw_model)
		{
			ModelLOD *lod = modelGetLOD(atlas_data->client_data.draw_model, 0);
			report->data[active].geo_bytes += modelLODGetBytesTotal(lod) + modelLODGetBytesUncompressed(lod);
			report->data[active].geo_video_mem_bytes += atlas_data->client_data.video_memory_usage;
			report->data[active].geo_vert_count += SAFE_MEMBER(lod, vert_count);
		}

		if (atlas_data->client_data.occlusion_model)
		{
			ModelLOD *lod = modelGetLOD(atlas_data->client_data.occlusion_model, 0);
			report->data[active].occlusion_bytes += modelLODGetBytesTotal(lod) + modelLODGetBytesUncompressed(lod);
			report->data[active].occ_vert_count += SAFE_MEMBER(lod, vert_count);
		}

		if (atlas_data->collision_model)
		{
			ModelLOD *lod = modelGetLOD(atlas_data->collision_model, 0);
			report->data[active].collision_bytes += modelLODGetBytesTotal(lod) + modelLODGetBytesUncompressed(lod);
			report->data[active].coll_vert_count += SAFE_MEMBER(lod, vert_count);
		}

		if (atlas_data->client_data.gfx_data)
		{
			F32 bpp = 3; // RGB
			if (atlas_data->client_data.color_texture.has_alpha)
			{
				if (atlas_data->client_data.color_texture.is_dxt)
					bpp = 1; // DXT5
				else
					bpp = 4; // RGBA
			}
			else if (atlas_data->client_data.color_texture.is_dxt)
			{
				bpp = 0.5f; // DXT1
			}

			if (atlas_data->client_data.color_texture.data)
				report->data[active].color_src_bytes += round(SQR(atlas_data->client_data.color_texture.width) * bpp);
			if (atlas_data->client_data.gfx_data->color_tex)
				report->data[active].color_tex_bytes += round(atlas_data->client_data.gfx_data->color_tex->width * atlas_data->client_data.gfx_data->color_tex->height * bpp);
		}
	}
}

AUTO_COMMAND;
void TerrainMemoryReport()
{
	int i, j, total_geo, total_tex;
	AtlasMemoryReport report = { 0 };
	WorldRegion **regions = worldGetAllWorldRegions();

	for (i = 0; i < eaSize(&regions); i++)
	{
		if (regions[i]->atlases)
		{
			for (j = 0; j < eaSize(&regions[i]->atlases->all_atlases); j++)
				runAtlasMemoryReport(regions[i]->atlases->all_atlases[j], &report);
		}
	}

	total_geo = report.data[0].geo_bytes + report.data[1].geo_bytes + report.data[0].geo_video_mem_bytes + report.data[1].geo_video_mem_bytes;
	total_tex = report.data[0].color_tex_bytes + report.data[1].color_tex_bytes + report.data[0].color_src_bytes + report.data[1].color_src_bytes;

	printf("\n*** Begin Terrain Memory Report ***\n");
	printf("\n");
	for (i = 0; i < ATLAS_MAX_LOD+1; ++i)
		printf("Active LOD %d tiles: %d\n", i, report.lod_count[i]);
	printf("\n");
	printf("Geometry:            %s\n", friendlyBytes(total_geo));
	printf("  Active vid mem:    %s\n", friendlyBytes(report.data[1].geo_video_mem_bytes));
	printf("  Active sys mem:    %s\n", friendlyBytes(report.data[1].geo_bytes));
	printf("    Vert cnt:        %d\n", report.data[1].geo_vert_count);
	printf("  Inactive vid mem:  %s\n", friendlyBytes(report.data[0].geo_video_mem_bytes));
	printf("  Inactive sys mem:  %s\n", friendlyBytes(report.data[0].geo_bytes));
	printf("    Vert cnt:        %d\n", report.data[0].geo_vert_count);
	printf("\n");
	printf("Occlusion:\n");
	printf("  Active mem:        %s\n", friendlyBytes(report.data[1].occlusion_bytes));
	printf("    Vert cnt:        %d\n", report.data[1].occ_vert_count);
	printf("  Inactive mem:      %s\n", friendlyBytes(report.data[0].occlusion_bytes));
	printf("    Vert cnt:        %d\n", report.data[0].occ_vert_count);
	printf("\n");
	printf("Collision:\n");
	printf("  Active mem:        %s\n", friendlyBytes(report.data[1].collision_bytes));
	printf("    Vert cnt:        %d\n", report.data[1].coll_vert_count);
	printf("  Inactive mem:      %s\n", friendlyBytes(report.data[0].collision_bytes));
	printf("    Vert cnt:        %d\n", report.data[0].coll_vert_count);
	printf("\n");
	printf("Textures:            %s\n", friendlyBytes(total_tex));
	printf("  Active vid mem:    %s\n", friendlyBytes(report.data[1].color_tex_bytes));
	printf("  Active sys mem:    %s\n", friendlyBytes(report.data[1].color_src_bytes));
	printf("  Inactive vid mem:  %s\n", friendlyBytes(report.data[0].color_tex_bytes));
	printf("  Inactive sys mem:  %s\n", friendlyBytes(report.data[0].color_src_bytes));
	printf("\n");
	printf("Total accounted for: %s\n", 
		friendlyBytes(	total_geo + total_tex + 
						report.data[0].occlusion_bytes + report.data[1].occlusion_bytes + 
						report.data[0].collision_bytes + report.data[1].collision_bytes));
	printf("\n");
	printf("*** End Terrain Memory Report ***\n");
}

static void terrainPrintAtlasTreeHelper(HeightMapAtlas *atlas, int depth)
{
	int i;
	char spaces[64];
	assert(depth < 32);
	memset(spaces, ' ', depth*2);
	spaces[depth*2] = '\0';
	printf("%sAtlas %X (%d, %s):\n", spaces, (int)(intptr_t)atlas, atlas->lod, atlas->atlas_active ? "Active" : "Inactive");
	printf("%sLOD %0.2f %0.2f, %0.2f %0.2f\n", spaces, atlas->corner_lods[0], atlas->corner_lods[1], atlas->corner_lods[2], atlas->corner_lods[3]);
	for (i = 0; i < 8; i++)
	{
		if (atlas->neighbors[i])
			printf("%sN %d: %X\n", spaces, i, (int)(intptr_t)atlas->neighbors[i]);
	}

	if (atlas->children[0])
	{
		printf("\n%sCHILD 0, 0\n", spaces);
		terrainPrintAtlasTreeHelper(atlas->children[0], depth+1);
	}
	if (atlas->children[1])
	{
		printf("\n%sCHILD 1, 0\n", spaces);
		terrainPrintAtlasTreeHelper(atlas->children[1], depth+1);
	}
	if (atlas->children[2])
	{
		printf("\n%sCHILD 0, 1\n", spaces);
		terrainPrintAtlasTreeHelper(atlas->children[2], depth+1);
	}
	if (atlas->children[3])
	{
		printf("\n%sCHILD 1, 1\n", spaces);
		terrainPrintAtlasTreeHelper(atlas->children[3], depth+1);
	}
}

AUTO_COMMAND;
void terrainPrintAtlasTree(void)
{
	int l;
	WorldRegion *region = zmapGetWorldRegionByName(NULL, NULL);

	if (region->atlases)
	{
		for (l = 0; l < eaSize(&region->atlases->root_atlases); l++)
			terrainPrintAtlasTreeHelper(region->atlases->root_atlases[l], 0);
	}
}


/********************************
   Terrain static lighting
 ********************************/

static GMesh *computeAtlasVertexLightsMesh(HeightMapAtlas *atlas, bool keepData, bool forceIdxOrder, WorldRegion *region, BinFileList *file_list)
{
	Vec3 mid;
	Vec3 min;
	Vec3 max;
	float radius;
	ModelLOD *model = NULL;
	GfxStaticObjLightCache *light_cache;
	GMesh *mesh = calloc(sizeof(GMesh), 1);
	int j;

	if(!atlas->data || !atlas->data->client_data.draw_model) {
		return NULL;
	}

	assert(atlas->data && atlas->data->client_data.draw_model);

	// Make sure all the model information is really available to us.
	model = modelLODLoadAndMaybeWait(atlas->data->client_data.draw_model, 0, true);
	model->load_in_foreground = true;
	gfxFillModelRenderInfo(model);
	model->load_in_foreground = false;

	assert(model->geo_render_info);

	if(!model->vert_count) {
		// Empty piece of terrain (caused by cut brush?).
		return NULL;
	}

	// Set the bounds based on the atlas information. The bounds in
	// the model were based off the weird information in the
	// position field for terrain, so we don't want that.

	setVec3(min,
		GRID_BLOCK_SIZE * atlas->local_pos[0],
		atlas->min_height,
		GRID_BLOCK_SIZE * atlas->local_pos[1]);

	setVec3(max,
		GRID_BLOCK_SIZE * heightmapAtlasGetLODSize(atlas->lod) + min[0],
		atlas->max_height,
		GRID_BLOCK_SIZE * heightmapAtlasGetLODSize(atlas->lod) + min[2]);

	setVec3(mid, (min[0] + max[0])*0.5f, 
		(min[1] + max[1])*0.5f, 
		(min[2] + max[2])*0.5f);

	radius = sqrtf(SQR(heightmapAtlasGetLODSize(atlas->lod)*0.5f*GRID_BLOCK_SIZE) * 2.f + SQR((atlas->max_height-atlas->min_height)*0.5f));

	{
		// Lighting calculations are done by making a fake WorldCell,
		// copying atlas model data into it, running the normal static
		// light cache code on it, and constructing a separate model
		// from that which contains the lighting values for each
		// vertex.

		// Allocate the minimal elements we'll need for a WorldCell to
		// calculate lighting with.
		WorldModelEntry *fakeEntry          = calloc(1, sizeof(WorldModelEntry));
		WorldCell *fakeCell                 = calloc(1, sizeof(WorldCell));
		NOCONST(WorldDrawableList) *fakeDrawableList = calloc(1, sizeof(NOCONST(WorldDrawableList)) + sizeof(NOCONST(WorldDrawableLod)));
		NOCONST(WorldDrawableLod) *fakeLod           = (NOCONST(WorldDrawableLod)*)(fakeDrawableList + 1);
		NOCONST(WorldDrawableSubobject) *fakeSubOb   = calloc(1, sizeof(NOCONST(WorldDrawableSubobject)));
		NOCONST(ModelDraw) *fakeModelDraw            = calloc(1, sizeof(NOCONST(ModelDraw)));

		// Get the relevant parts of the terrain model and make sure
		// the information is unpacked.
		const Vec3 *modelVerts  = modelGetVerts(model);
		const Vec2 *modelSts    = modelGetSts(model);

		// Original vertex positions for the terrain model. Actually
		// contains height information the x and y fields instead of
		// normal position coordinates.
		Vec3 *oldVerts = calloc(sizeof(Vec3), model->vert_count * 2);

		assert(modelVerts);
		assert(modelSts);

		// Unload the old version of the light model.
		tempModelFree(&atlas->data->client_data.static_vertex_light_model);

		// Set up our fake WorldModelEntry.
		fakeEntry->base_drawable_entry.base_entry.type = WCENT_MODEL;
		fakeEntry->base_drawable_entry.base_entry.shared_bounds = calloc(1, sizeof(WorldCellEntrySharedBounds));
		fakeEntry->base_drawable_entry.base_entry.shared_bounds->model = model->model_parent;
		fakeEntry->base_drawable_entry.base_entry.shared_bounds->use_model_bounds = false;
		fakeEntry->base_drawable_entry.base_entry.shared_bounds->radius = radius;
		fakeEntry->base_drawable_entry.draw_list = STRUCT_RECONST(WorldDrawableList, fakeDrawableList);
		copyVec3(min, fakeEntry->base_drawable_entry.base_entry.shared_bounds->local_min);
		copyVec3(max, fakeEntry->base_drawable_entry.base_entry.shared_bounds->local_max);
		copyVec3(mid, fakeEntry->base_drawable_entry.base_entry.bounds.world_mid);
		identityMat4(fakeEntry->base_drawable_entry.base_entry.bounds.world_matrix);

		// World position.
		/*
		// I'm leaving this disabled for now and opting to just do this
		// transform in the vertex coordinates instead. -Cliff
		setVec3(fakeEntry->base_drawable_entry.base_entry.bounds.world_matrix[3],
			atlas->local_pos[0]*GRID_BLOCK_SIZE,
			0,
			atlas->local_pos[1]*GRID_BLOCK_SIZE);*/

		// Set up our fake WorldCell.
		fakeEntry->base_entry_data.cell = fakeCell;
		fakeCell->region = atlas->region;
		eaPush(&fakeCell->drawable.drawable_entries, (WorldDrawableEntry*)fakeEntry);

		// Fake WorldDrawableList and WorldDrawableLod, containing just
		// the one subobject at one LOD level.
		fakeDrawableList->lod_count = 1;
		fakeDrawableList->drawable_lods = fakeLod;
		fakeLod->subobject_count = 1;
		fakeLod->subobjects = calloc(sizeof(WorldDrawableSubobject*), 1);
		fakeLod->subobjects[0] = fakeSubOb;

		// And the one model.
		fakeSubOb->model = fakeModelDraw;
		fakeModelDraw->model = model->model_parent;

		// Convert the terrain coordinates into something more sane
		// (and what the lighting system expects from normal models).
		for(j = 0; j < model->vert_count; j++) {

			// Back up the old vertex position.
			copyVec3(model->unpack.verts[j], oldVerts[j]);

			assert(FINITEVEC3(model->unpack.verts[j]));

			// Modify the model's unpacked data. Positions are actually stored
			// in the texture coordinates on the model, and they still have to
			// be transformed based on the GRID_BLOCK_SIZE and LOD level.
			model->unpack.verts[j][0] = min[0] + model->unpack.sts[j][0] * GRID_BLOCK_SIZE * heightmapAtlasGetLODSize(atlas->lod); // + min[0];
			model->unpack.verts[j][2] = min[2] + model->unpack.sts[j][1] * GRID_BLOCK_SIZE * heightmapAtlasGetLODSize(atlas->lod); // + min[1];
			model->unpack.verts[j][1] = oldVerts[j][0];
		}

		// Create the light cache.
		gStaticLightForBin = true;
		light_cache = gfxCreateStaticLightCache(&fakeEntry->base_drawable_entry, region);
		assert(light_cache);
		light_cache->need_vertex_light_update = true;

		// Ensure the light cache is up to date and all the lights are
		// calculated.
		while(light_cache->need_vertex_light_update) {
			gfxStaticLightCacheGetLights(light_cache, -1, file_list);
			geoForceBackgroundLoaderToFinish();
		}
		gStaticLightForBin = false;

		// Light data is calculated. Time to save it.
		if(light_cache->lod_vertex_light_data) {

			const U32 *tris = modelGetTris(model);

			// Create a GMesh to store the new lighting data in.
			mesh->vert_count = model->vert_count;
			mesh->usagebits = USE_POSITIONS | USE_POSITIONS2 | USE_NORMALS2;
			mesh->normals2 = calloc(sizeof(Vec3), mesh->vert_count);   // normals2 is needed so we can properly shoehorn it all into morph data.
			mesh->positions = calloc(sizeof(Vec3), mesh->vert_count);  // Positions are needed as part of the model saving process to calculate bounds.
			mesh->positions2 = calloc(sizeof(Vec3), mesh->vert_count); // Calculated lighting data will go into positions2.
			mesh->tri_count = model->tri_count;
			mesh->tris = calloc(sizeof(GTriIdx), mesh->tri_count);

			// Copy the triangles from the terrain model into the new mesh.
			for(j = 0; j < mesh->tri_count; j++) {

				if(forceIdxOrder) {
					// Set the tex_id just to ensure the triangles are
					// sorted identically to the terrain model's.
					mesh->tris[j].tex_id = j;
				} else {
					// For when the triangles and verts aren't sorted,
					// leave them in the same order as the source data.
					mesh->tris[j].tex_id = 0;
				}

				// Copy the triangle indices.
				mesh->tris[j].idx[0] = tris[j * 3];
				mesh->tris[j].idx[1] = tris[j * 3 + 1];
				mesh->tris[j].idx[2] = tris[j * 3 + 2];
			}
		}

		// Iterate through the vertices in the model and mesh, copying
		// calculated light values into normals2.
		for(j = 0; j < model->vert_count; j++) {

			if(light_cache->lod_vertex_light_data && light_cache->lod_vertex_light_data[0]) {

				// Pull the light value for this vertex out of
				// the static light cache and assign it to the
				// positions2 value in the new mesh.
				float offset = light_cache->lod_vertex_light_data[0]->rdr_vertex_light.vlight_offset;
				float multiplier = light_cache->lod_vertex_light_data[0]->rdr_vertex_light.vlight_multiplier;
				mesh->positions2[j][0] = offset + multiplier * (float)(light_cache->lod_vertex_light_data[0]->vertex_colors[j] & 0xff) / 255.0;
				mesh->positions2[j][1] = offset + multiplier * (float)((light_cache->lod_vertex_light_data[0]->vertex_colors[j] & 0xff00) >> 8) / 255.0;
				mesh->positions2[j][2] = offset + multiplier * (float)((light_cache->lod_vertex_light_data[0]->vertex_colors[j] & 0xff0000) >> 16) / 255.0;

			} else {

				// No lighting! Set it all to black. (Leave it
				// as calloc left it.)

			}

			// Copy actual positions into the new mesh, just
			// for proper bounds calculations and such.
			copyVec3(model->unpack.verts[j], mesh->positions[j]);
			copyVec3(oldVerts[j], model->unpack.verts[j]);
		}

		// Done with this...
		gfxFreeStaticLightCache(light_cache);

		// This is no longer needed. Freeing it returns the terrain
		// back to the state it normally is in at the end of
		// atlasRequestDataLoad().
		if(model && !keepData) {
			modelLODFreeUnpacked(model);

			// FIXME: This should return the terrain to its normal
			// state, but in rare cases causes it to crash when taking
			// map photos.
			//modelLODFreePacked(model);
		}

		// Clean up our fake world cell.
		SAFE_FREE(fakeEntry);
		SAFE_FREE(fakeCell);
		SAFE_FREE(fakeDrawableList);
		fakeLod = NULL;
		SAFE_FREE(fakeSubOb);
		SAFE_FREE(fakeModelDraw);
		SAFE_FREE(oldVerts);
	}

	return mesh;

}

Model *computeAtlasVertexLightsSingleModel(HeightMapAtlas *atlas, WorldRegion *region) {

	Model *lightModel = NULL;

	// Compute the lights.
	GMesh *mesh = computeAtlasVertexLightsMesh(atlas, true, false, region, NULL);

	if(!mesh) return NULL;

	// Create a model from it.
	lightModel = tempModelAlloc(TERRAIN_TILE_LIGHTING_MODEL_NAME, NULL, 1, WL_FOR_TERRAIN);
	modelFromGmesh(lightModel, mesh);
	modelLODLoadAndMaybeWait(lightModel, 0, true);
	lightModel->model_lods[0]->load_in_foreground = true;
	gfxFillModelRenderInfo(lightModel->model_lods[0]);
	lightModel->model_lods[0]->load_in_foreground = false;

	// Clean up.
	gmeshFreeData(mesh);
	free(mesh);

	return lightModel;
}

static void computeAtlasVertexLightsAndSave(HeightMapAtlas *atlas, WorldRegion *region, const char ***file_list, BinFileList *file_list2, WorldClusterState *simplygonMeshes) {

	static int depth = 0;
	int i;
	ModelLOD *model = NULL;
	HogFile *light_model_hogg_file = NULL;
	char filename[MAX_PATH];
	char realFileName[MAX_PATH];
	bool created;
	int error;
	GMesh *mesh = NULL;
	char base_dir[MAX_PATH];

	if(!atlas) return;

	if(!depth) {
		// At the top level, reload all the atlases in a way that they keep
		// the PackData that goes with the model. Normally this is just freed
		// immediately after loading (leaving just the VBO in video memory),
		// but we need it for lighting calculations.
		atlasReloadEverythingForLightBin(atlas, true);
	}

	// Immediately recurse to children, so we actually end up calculating
	// lights from the lowest LODs (highest quality) first, then going up.
	depth++;
	for(i = 0; i < 4; i++) {
		if(atlas->children[i])
			computeAtlasVertexLightsAndSave(atlas->children[i], region, file_list, file_list2,simplygonMeshes);
	}
	depth--;

	// Do the actual light computation.
	mesh = computeAtlasVertexLightsMesh(atlas, false, true, region, file_list2);

	if(!mesh) return;

	// Piece together the filename and get the absolute
	// path.
	worldGetClientBaseDir(zmapGetFilename(atlas->region->zmap_parent), SAFESTR(base_dir));
	sprintf(filename, "%s/terrain_%s_light_models_%d.hogg",
		base_dir,
		atlas->region->name ? atlas->region->name : "Default",
		atlas->lod);

	fileLocateWrite(filename, realFileName);

	// Add it to our list of output files.
	eaPush(file_list, allocAddString(filename));

	// If we already have the hogg open for reading, we
	// need to close it.
	if(atlas->region->terrain_light_model_hoggs[atlas->lod]) {
		atlasTraceHoggf("Destroying terrain vertex light hogg for binning 0x%p \"%s\"\n", atlas->region->terrain_light_model_hoggs[atlas->lod], hogFileGetArchiveFileName(atlas->region->terrain_light_model_hoggs[atlas->lod]));
		hogFileDestroy(atlas->region->terrain_light_model_hoggs[atlas->lod], false);
		atlas->region->terrain_light_model_hoggs[atlas->lod] = NULL;
	}

	// Open the .hogg file for writing.
	light_model_hogg_file = hogFileRead(realFileName, &created, PIGERR_ASSERT, &error, HOG_MUST_BE_WRITABLE|HOG_NO_INTERNAL_TIMESTAMPS);

	if(light_model_hogg_file)
	{
		char headerFileName[MAX_PATH];
		char modelFileName[MAX_PATH];
		SimpleBufHandle headerBuf = NULL;
		SimpleBufHandle modelBuf = NULL;
		Geo2LoadData *gld = NULL;

		// Put together the file names for the header
		// and the model.
		sprintf(headerFileName, "x%d_z%d_light.atl", atlas->local_pos[0], atlas->local_pos[1]);
		sprintf(modelFileName, "x%d_z%d_lights.mset", atlas->local_pos[0], atlas->local_pos[1]);

		// Open the header and model separately.
		headerBuf = SimpleBufOpenWrite(headerFileName, true, light_model_hogg_file, true, false);
		modelBuf  = SimpleBufOpenWrite(modelFileName,  true, light_model_hogg_file, true, false);

		// Write them out.
		modelAddGMeshToGLD(&gld, mesh, (atlas->lod==0) ? TERRAIN_TILE_LIGHTING_MODEL_NAME : TERRAIN_ATLAS_LIGHTING_MODEL_NAME, NULL, 0, true, false, false, headerBuf);
		modelWriteAndFreeBinGLD(gld, modelBuf, false);

		// Close everything.
		SimpleBufClose(headerBuf);
		SimpleBufClose(modelBuf);
		atlasTraceHoggf("Destroying binning generated hogg 0x%p \"%s\"\n", light_model_hogg_file, hogFileGetArchiveFileName(light_model_hogg_file));
		hogFileDestroy(light_model_hogg_file, false);

	} else {
		// Could not open the .hogg file?
	}

	// Clean up our temporary mesh.
	gmeshFreeData(mesh);
	free(mesh);

	if(!depth) {
		// Reopen the hogg files so we can start rendering
		// with these lights immediately.
		for(i = 0; i < 5; i++) {
			worldRegionGetTerrainHogFile(atlas->region, THOG_LIGHTMODEL, i);
		}
	}
}

void gfxComputeTerrainLightingForBinning(ZoneMap *zmap, const char ***file_list, BinFileList *file_list2) {
	int j, k;

	// Preserve the original camera focus.
	Vec3 origCamFocus;
	copyVec3(gfxGetActiveCameraController()->camfocus, origCamFocus);

	// Iterate through the regions, check for atlases.
	for(k = 0; k < eaSize(&(zmap->map_info.regions)); k++) {
		WorldRegion *region = zmap->map_info.regions[k];

		if(region->atlases) {
			// Focus the camera on the center of the region.
			Vec3 middleOfRegion;
			addVec3(region->world_bounds.world_min, region->world_bounds.world_max, middleOfRegion);
			scaleVec3(middleOfRegion, 0.5, middleOfRegion);
			copyVec3(middleOfRegion, gfxGetActiveCameraController()->camfocus);

			// Make sure it uses the right sky.
			gfxUpdateActiveCameraViewVolumes(region);
			gfxTickSkyData();

			for(j = 0; j < eaSize(&region->atlases->root_atlases); j++) {
				// Compute lights for each root atlas.
				computeAtlasVertexLightsAndSave(region->atlases->root_atlases[j], region, file_list, file_list2, NULL);
			}
		}
	}

	// Restore camera focus.
	copyVec3(origCamFocus, gfxGetActiveCameraController()->camfocus);
	gfxCameraClearLastRegionData(gfx_state.currentCameraView);
}
