#include "wlTerrain_h_ast.h"
#include "wlTerrainPrivate.h"
#include "wlTerrainSource.h"
#include "wlTerrainBrush.h"
#include "wlModelBinningLOD.h"
#include "wlModelBinning.h"
#include "wlModelBinningPrivate.h"
#include "WorldCellStreamingPrivate.h"
#include "WorldCellStreaming.h"
#include "wlState.h"

#include "StringCache.h"
#include "ScratchStack.h"
#include "rgb_hsv.h"
#include "timing.h"
#include "hoglib.h"
#include "ThreadManager.h"
#include "qsortG.h"
#include "serialize.h"
#include "utilitieslib.h"
#include "gimmeDLLWrapper.h"
#include "estring.h"
#include "ContinuousBuilderSupport.h"
#include "Color.h"

AUTO_RUN_ANON(memBudgetAddMapping(__FILE__, BUDGET_Terrain_System););

extern ParseTable parse_TerrainObjectEntry[];
#define TYPE_parse_TerrainObjectEntry TerrainObjectEntry

static bool disable_threaded_binning;
static int terrain_mesh_logging;
static IVec2 save_debug_terrain_mesh = {-10000, -10000};

bool g_TerrainUseOptimalVertPlacement = true;
F32 g_TerrainScaleByArea = 0.25f;

// turns off threaded terrain tile binning
AUTO_CMD_INT(disable_threaded_binning, terrainDisableThreadedBinning) ACMD_CATEGORY(Debug) ACMD_CMDLINE;

// turns on terrain tile mesh logging
AUTO_CMD_INT(terrain_mesh_logging, terrainMeshLogging) ACMD_CATEGORY(Debug) ACMD_CMDLINE;

AUTO_COMMAND ACMD_CATEGORY(Debug) ACMD_CMDLINE;
void terrainSaveDebugMesh(char *x, char *y)
{
	int ix, iy;
	if (strStartsWith(x, "neg"))
		ix = -atoi(x+3);
	else
		ix = atoi(x);
	if (strStartsWith(y, "neg"))
		iy = -atoi(y+3);
	else
		iy = atoi(y);
	setVec2(save_debug_terrain_mesh, ix, iy);
}

#ifdef _FULLDEBUG
static void checkMeshLogMatch(const char *str, FILE *logfile)
{
	char logstr[2048], *s2;
	const char *s1;
	int logstrlen;

	logstr[0] = 0;
	fgets(logstr, ARRAY_SIZE(logstr)-1, logfile);

	logstrlen = (int)strlen(logstr);
	if (logstrlen)
		if (logstr[logstrlen-1] == 10)
			logstr[logstrlen-1] = 0;

	s1 = str;
	s2 = logstr;
	if (s1 && s2 && strcmp(s1, s2) != 0)
		_DbgBreak();
}

#define TER_LOG(fmt, ...)	if (terrain_mesh_log) \
							{ \
								if (terrain_mesh_logging == 2) \
								{ \
									char *str = NULL; \
									estrPrintf(&str, fmt, __VA_ARGS__); \
									checkMeshLogMatch(str, terrain_mesh_log); \
									estrDestroy(&str); \
								} \
								else \
								{ \
									fprintf(terrain_mesh_log, fmt "\n", __VA_ARGS__); \
								} \
							}
#else
#define TER_LOG(fmt, ...)
#endif


typedef struct TerrainThreadProcInfo {
	ZoneMapLayer *layer;
	HeightMap **map_cache;
	TerrainBlockRange *range;
	F32 *ranges_array;
	int ranges_array_size;
	HeightMapBinAtlas ***atlas_list;
	HogFile *intermediate_output_hog_file;
	SimpleBuffer **output_files;
	int remaining, thread_count;
} TerrainThreadProcInfo;

static TerrainThreadProcInfo g_TerrainThreadProcInfo;
static CRITICAL_SECTION terrain_bins_cs;


/********************************
   Saving BIN format
 ********************************/

static void terrainWriteBinnedObject(const TerrainBinnedObject *obj, SimpleBufHandle buf)
{
	SimpleBufWriteU32(obj->group_id, buf);
	SimpleBufWriteU32(obj->x, buf);
	SimpleBufWriteU32(obj->z, buf);
	SimpleBufWriteU32(obj->seed, buf);
	SimpleBufWriteF32(obj->position[0], buf);
	SimpleBufWriteF32(obj->position[1], buf);
	SimpleBufWriteF32(obj->position[2], buf);
	SimpleBufWriteF32(obj->normal[0], buf);
	SimpleBufWriteF32(obj->normal[1], buf);
	SimpleBufWriteF32(obj->normal[2], buf);
	SimpleBufWriteF32(obj->scale, buf);
	SimpleBufWriteF32(obj->rotation, buf);
	SimpleBufWriteF32(obj->intensity, buf);
	SimpleBufWriteU8(obj->tint[0], buf);
	SimpleBufWriteU8(obj->tint[1], buf);
	SimpleBufWriteU8(obj->tint[2], buf);
	SimpleBufWriteU8(obj->weld, buf);
	SimpleBufWriteU8(obj->weld_group, buf);
}

static void terrainReadBinnedObject(TerrainBinnedObject *obj, SimpleBufHandle buf)
{
	SimpleBufReadU32(&obj->group_id, buf);
	SimpleBufReadU32(&obj->x, buf);
	SimpleBufReadU32(&obj->z, buf);
	SimpleBufReadU32(&obj->seed, buf);
	SimpleBufReadF32(&obj->position[0], buf);
	SimpleBufReadF32(&obj->position[1], buf);
	SimpleBufReadF32(&obj->position[2], buf);
	SimpleBufReadF32(&obj->normal[0], buf);
	SimpleBufReadF32(&obj->normal[1], buf);
	SimpleBufReadF32(&obj->normal[2], buf);
	SimpleBufReadF32(&obj->scale, buf);
	SimpleBufReadF32(&obj->rotation, buf);
	SimpleBufReadF32(&obj->intensity, buf);
	SimpleBufReadU8(&obj->tint[0], buf);
	SimpleBufReadU8(&obj->tint[1], buf);
	SimpleBufReadU8(&obj->tint[2], buf);
	SimpleBufReadU8(&obj->weld, buf);
	SimpleBufReadU8(&obj->weld_group, buf);

	if (!obj->normal[0] && !obj->normal[1] && !obj->normal[2])
		setVec3(obj->normal, 0, 1, 0);
}

static bool terrainReadTimestamps(ZoneMapLayer *layer, TerrainBlockRange *block, TerrainTimestamps *timestamps)
{
	readTerrainFileHogg(layer, block->block_idx, NULL);
	if (block->interm_file)
	{
		return ParserOpenReadBinaryFile(block->interm_file, "dependencies.dat", parse_TerrainTimestamps, timestamps, NULL, NULL, NULL, NULL, 0, 0, 0);
	}
	return false;
}

static void terrainWriteTimestamps(HogFile *hog_file, const TerrainTimestamps *timestamps)
{
	ParserWriteBinaryFile("dependencies.dat", NULL, parse_TerrainTimestamps, (void*)timestamps, NULL, NULL, NULL, NULL, 0, 0, hog_file, PARSERWRITE_ZEROTIMESTAMP, 0);
}

static GMesh *heightMapCalcOcclusion(F32 *height_buf, U8 *occ_buf, int size, bool calc_colors)
{
    int x, z;
    F32 min_height = height_buf[0];
    IVec2 min_pos = { 0, 0 };
    IVec2 max_pos = { size-1, size-1 };
	GMesh *mesh = calloc(1, sizeof(GMesh));

    for (z = 0; z < size; z++)
	{
        for (x = 0; x < size; x++)
		{
            if (height_buf[x+z*size] < min_height)
                min_height = height_buf[x+z*size];
		}
	}

	gmeshSetUsageBits(mesh, USE_POSITIONS | (calc_colors?USE_COLORS:0));
    occlusion_quad_recurse(height_buf, size, GRID_BLOCK_SIZE/(size-1), HEIGHTMAP_MIN_HEIGHT, min_height, min_pos, max_pos, mesh, occ_buf);
	gmeshPool(mesh, true, false, false); // pool verts and tris

	return mesh;
}


void layerHeightmapCalcOcclusion(HeightMap *height_map)
{
	HeightMapAtlasData *atlas_data = SAFE_MEMBER(height_map, data);
	GMesh *occlusion_mesh;
	TerrainBuffer *height_buf, *occlusion_buf;

	if (!atlas_data)
		return;

	height_buf = heightMapGetBuffer(height_map, TERRAIN_BUFFER_HEIGHT, height_map->loaded_level_of_detail);
	occlusion_buf = heightMapGetBuffer(height_map, TERRAIN_BUFFER_OCCLUSION, 5);
	occlusion_mesh = heightMapCalcOcclusion(height_buf->data_f32, occlusion_buf ? occlusion_buf->data_byte : NULL, height_buf->size, true);

	if (!atlas_data->client_data.occlusion_model)
		atlas_data->client_data.occlusion_model = tempModelAlloc(TERRAIN_TILE_OCC_MODEL_NAME, &default_material, 1, WL_FOR_TERRAIN);

	modelFromGmesh(atlas_data->client_data.occlusion_model, occlusion_mesh);

	gmeshFreeData(occlusion_mesh);
	free(occlusion_mesh);
}

static int *calculateVarColorLookup(TerrainMesh *tmesh, HeightMap *height_map, TerrainBuffer *material_buffer, char **material_table, int *material_lookup)
{
	int i, j;
	int *lookup_table = ScratchAlloc(NUM_MATERIALS * sizeof(int));
	
	for (i = 0; i < NUM_MATERIALS; ++i)
		lookup_table[i] = -1;

	for (i = 0; i < material_buffer->size * material_buffer->size; ++i)
	{
		for (j = 0; j < NUM_MATERIALS; ++j)
		{
			if (material_buffer->data_material[i].weights[j] > TERRAIN_MIN_WEIGHT && lookup_table[j] < 0)
			{
				char *material_name;
				int material_id;
				
				material_id = height_map->material_ids[j]; // convert to layer material table indices

				if (material_table && material_lookup)
				{
					int midx;
					assert(material_id >= 0 && material_id < eaiSize(&material_lookup));
					midx = material_lookup[material_id];
					assert(midx >= 0 && midx < eaSize(&material_table));
					material_name = material_table[midx];
				}
				else
				{
					material_name = terrainGetLayerMaterial(height_map->zone_map_layer, material_id);
				}

				lookup_table[j] = eaFindString(&tmesh->material_names, material_name);
				if (lookup_table[j] < 0)
					lookup_table[j] = eaPush(&tmesh->material_names, allocAddString(material_name));
			}
		}
	}

	assert(eaSize(&tmesh->material_names) <= NUM_MATERIALS);

	return lookup_table;
}

static void getMaterialWeightColor(TerrainBuffer *material_buffer, int x, int y, int *varcolor_lookup, U8 *varcolor, int size)
{
	int j, index, total = 0, max_val = -1, max_idx = -1;

	index = x + y*material_buffer->size;

	for (j = 0; j < NUM_MATERIALS; ++j)
	{
		int val = round(material_buffer->data_material[index].weights[j]);
		int i = varcolor_lookup[j];

		if (i >= 0 && i < size)
		{
			varcolor[i] = CLAMP(val, 0, 255);
			if (varcolor[i] <= TERRAIN_MIN_WEIGHT)
				varcolor[i] = 0; // everything below weight of TERRAIN_MIN_WEIGHT goes to weight 0 to match calculateVarColorLookup
			if (total >= 255)
				varcolor[i] = 0;
			else if (total + varcolor[i] > 255)
				varcolor[i] = 255 - total;
			total += varcolor[i];
			if (varcolor[i] > max_val)
			{
				max_val = varcolor[i];
				max_idx = i;
			}
		}
	}

	if (total < 255)
		varcolor[max_idx] += 255 - total;
}

// to get the x and y offsets for the verts of each face:
static const int vdx[] = { 0, 0, 1, 1, 0 };
static const int vdy[] = { 0, 1, 1, 0, 0 };

static void getFaceNormals(HeightMap *height_map, TerrainBuffer *height_buffers[3][3], Vec3 *all_face_normals[3][3], int lod, FILE *terrain_mesh_log)
{
	TerrainBuffer *height_buffer;
	int size = GRID_LOD(lod) - 1;
	int x, z, i, j, k, p;
	Vec3 *face_normals;
	F32 position_mult = GRID_BLOCK_SIZE / (F32)(size);
	int height_size_mul = (height_buffers[1][1]->size - 1) / (size);

	for (z = 0; z < 3; ++z)
	{
		for (x = 0; x < 3; ++x)
		{
			// height buffer arrays are in x major order, face normal arrays are in z major order
			if (!height_buffers[x][z])
				all_face_normals[z][x] = NULL;
			else if (z == 1 && x == 1)
				all_face_normals[z][x] = ScratchAlloc((size)*(size)*2*sizeof(Vec3));
			else if (z == 1 || x == 1)
				all_face_normals[z][x] = ScratchAlloc((size)*2*sizeof(Vec3));
			else
				all_face_normals[z][x] = ScratchAlloc(2 * sizeof(Vec3));
		}
	}

	// Find which way the normals are facing
	face_normals = all_face_normals[1][1];
	height_buffer = height_buffers[1][1];
	for (i = 0; i < (size); i++)
	{
		for (j = 0; j < (size); j++)
		{
			for (p=0; p<2; ++p)
			{
				IVec2 points[3];
				Vec3 pp[3];
				int idx = (i + j * (size)) * 2 + p;
				for (k = 0; k < 3; k++)
				{
					points[k][0] = i + vdx[k+p*2];
					points[k][1] = j + vdy[k+p*2];
					pp[k][0] = points[k][0] * position_mult;
					pp[k][2] = points[k][1] * position_mult;
					pp[k][1] = height_buffer->data_f32[points[k][0] * height_size_mul + points[k][1] * height_size_mul * height_buffer->size];
				}
				makePlaneNormal(pp[0], pp[1], pp[2], face_normals[idx]);
				TER_LOG("Face normal (1, 1) %d %d %d -> (%f, %f, %f)", i, j, p, face_normals[idx][0], face_normals[idx][1], face_normals[idx][2]);
			}
		}
	}

	for (z = 0; z < 3; ++z)
	{
		for (x = 0; x < 3; ++x)
		{
			// height buffer arrays are in x major order, face normal arrays are in z major order
			face_normals = all_face_normals[z][x];
			height_buffer = height_buffers[x][z];

			if (!face_normals)
				continue;
			if (z == 1 && x == 1)
			{
				continue;
			}
			else if (z == 1)
			{
				i = (x == 0)?(size-1):0;
				for (j = 0; j < (size); j++)
				{
					for (p=0; p<2; ++p)
					{
						IVec2 points[3];
						Vec3 pp[3];
						int idx = j * 2 + p;
						for (k = 0; k < 3; k++)
						{
							points[k][0] = i + vdx[k+p*2];
							points[k][1] = j + vdy[k+p*2];
							pp[k][0] = points[k][0] * position_mult;
							pp[k][2] = points[k][1] * position_mult;
							pp[k][1] = height_buffer->data_f32[points[k][0] * height_size_mul + points[k][1] * height_size_mul * height_buffer->size];
						}
						makePlaneNormal(pp[0], pp[1], pp[2], face_normals[idx]);
						TER_LOG("Face normal (%d, %d) %d %d %d -> (%f, %f, %f)", z, x, i, j, p, face_normals[idx][0], face_normals[idx][1], face_normals[idx][2]);
					}
				}
			}
			else if (x == 1)
			{
				j = (z == 0)?(size-1):0;
				for (i = 0; i < (size); i++)
				{
					for (p=0; p<2; ++p)
					{
						IVec2 points[3];
						Vec3 pp[3];
						int idx = i * 2 + p;
						for (k = 0; k < 3; k++)
						{
							points[k][0] = i + vdx[k+p*2];
							points[k][1] = j + vdy[k+p*2];
							pp[k][0] = points[k][0] * position_mult;
							pp[k][2] = points[k][1] * position_mult;
							pp[k][1] = height_buffer->data_f32[points[k][0] * height_size_mul + points[k][1] * height_size_mul * height_buffer->size];
						}
						makePlaneNormal(pp[0], pp[1], pp[2], face_normals[idx]);
						TER_LOG("Face normal (%d, %d) %d %d %d -> (%f, %f, %f)", z, x, i, j, p, face_normals[idx][0], face_normals[idx][1], face_normals[idx][2]);
					}
				}
			}
			else
			{
				i = (x == 0)?(size-1):0;
				j = (z == 0)?(size-1):0;
				for (p=0; p<2; ++p)
				{
					IVec2 points[3];
					Vec3 pp[3];
					int idx = p;
					for (k = 0; k < 3; k++)
					{
						points[k][0] = i + vdx[k+p*2];
						points[k][1] = j + vdy[k+p*2];
						pp[k][0] = points[k][0] * position_mult;
						pp[k][2] = points[k][1] * position_mult;
						pp[k][1] = height_buffer->data_f32[points[k][0] * height_size_mul + points[k][1] * height_size_mul * height_buffer->size];
					}
					makePlaneNormal(pp[0], pp[1], pp[2], face_normals[idx]);
					TER_LOG("Face normal (%d, %d) %d %d %d -> (%f, %f, %f)", z, x, i, j, p, face_normals[idx][0], face_normals[idx][1], face_normals[idx][2]);
				}
			}
		}
	}
}

#define MAX_UNIQUE_VERTS_PER_POS 8

static void heightMapCalcFastNormal(Vec3 normal, TerrainBuffer *height_buffer, int x, int z)
{
	int xn = CLAMP(x+1, 0, height_buffer->size-1);
	int xp = CLAMP(x-1, 0, height_buffer->size-1);
	int zn = CLAMP(z+1, 0, height_buffer->size-1);
	int zp = CLAMP(z-1, 0, height_buffer->size-1);
	normal[0] = 0.5f * (height_buffer->data_f32[xp+z*height_buffer->size] - height_buffer->data_f32[xn+z*height_buffer->size]);
	normal[1] = 1;
	normal[2] = 0.5f * (height_buffer->data_f32[x+zp*height_buffer->size] - height_buffer->data_f32[x+zn*height_buffer->size]);
	normalVec3(normal);
}

static TerrainMesh *heightMapCreateMesh(HeightMap *height_map, TerrainBuffer *height_buffer, Vec3 *all_face_normals[3][3], 
										TerrainBuffer *material_buffer, char **material_table, int *material_lookup, 
										TerrainBuffer *cut_buffer, int lod, bool dynamic_normals, bool for_bins, FILE *terrain_mesh_log)
{
	int size = GRID_LOD(lod);
	F32 position_mult = GRID_BLOCK_SIZE / (F32)(size-1);
	F32 texcoord_mult = 1 / (F32)(size-1);
	int i, j, k, x, z, p;
	TerrainMesh *tmesh;
	U8 *face_buffer;
	Vec3 *face_normals;
	int *vert_cache;
	int height_size_mul = (height_buffer->size - 1) / (size - 1);
	int material_size_mul = (material_buffer->size - 1) / (size - 1);
	int cut_size_mul = cut_buffer ? ((cut_buffer->size - 1) / (size - 1)) : 1;
	int *varcolor_lookup;
	U8 *varcolor;

	PERFINFO_AUTO_START_FUNC();

	SET_FP_CONTROL_WORD_DEFAULT;

	assert(height_buffer && material_buffer);

	tmesh = calloc(1, sizeof(TerrainMesh));
	face_buffer = ScratchAlloc((size-1)*(size-1)*2*sizeof(U8));
	vert_cache = ScratchAlloc(size*size*MAX_UNIQUE_VERTS_PER_POS*sizeof(int));
 
	face_normals = all_face_normals[1][1];

	for (i = 0; i < (size-1); i++)
	{
		for (j = 0; j < (size-1); j++)
		{
			for (p=0; p<2; ++p)
			{
				int idx = (i + j * (size-1)) * 2 + p;
				face_buffer[idx] = tmeshClassifyNormal(face_normals[idx]);
			}
		}
	}

	tmeshFixupFaceClassifications(face_buffer, size);

	varcolor_lookup = calculateVarColorLookup(tmesh, height_map, material_buffer, material_table, material_lookup);

	// make GMesh
	tmesh->mesh = calloc(1, sizeof(GMesh));
	gmeshEnsureTrisFit(tmesh->mesh, (size-1)*(size-1)*2);
	gmeshSetUsageBits(tmesh->mesh, USE_POSITIONS | USE_VARCOLORS | USE_NORMALS | (for_bins ? 0 : USE_TEX1S));
	gmeshSetVarColorSize(tmesh->mesh, eaSize(&tmesh->material_names));

	varcolor = _alloca(tmesh->mesh->varcolor_size * sizeof(U8));

	memset(vert_cache, 0xffffffff, size*size*MAX_UNIQUE_VERTS_PER_POS*sizeof(int));

	for (i = 0; i < size-1; ++i)
	{
		for (j = 0; j < size-1; ++j)
		{
			for (p = 0; p < 2; ++p)
			{
				int vertex_idxs[3] = {-1, -1, -1};
				TerrainMaterialWeight *weights[3];
				int fidx = (j * (size-1) + i) * 2 + p;
				U8 facing = face_buffer[fidx];

				if (cut_buffer)
				{
					bool is_cut = false;
					for (k = 0; k < 3; k++)
					{
						x = i + vdx[k + p*2];
						z = j + vdy[k + p*2];
						if (cut_buffer->data_byte[x * cut_size_mul + z * cut_size_mul * cut_buffer->size] < 128)
						{
							is_cut = true;
							break;
						}
					}

					if (is_cut)
						continue;
				}

				for (k = 0; k < 3; k++)
				{
					Vec3 position, normal;
					Vec2 texcoord;
					F32 y;
					int v;

					x = i + vdx[k + p*2];
					z = j + vdy[k + p*2];
					y = height_buffer->data_f32[x * height_size_mul + z * height_size_mul * height_buffer->size];

					// determine which detail materials affect this triangle
					weights[k] = &material_buffer->data_material[x * material_size_mul + z * material_size_mul * material_buffer->size];

					getMaterialWeightColor(material_buffer, x, z, varcolor_lookup, varcolor, tmesh->mesh->varcolor_size);

					// set position
					setVec3(position, x * position_mult, y, z * position_mult);

					if (dynamic_normals)
					{
						//setVec3(normal, 0, 1, 0);
						heightMapCalcFastNormal(normal, height_buffer, x*height_size_mul, z*height_size_mul);
					}
					else
					{
						tmeshCalcNormal(normal, all_face_normals, fidx, x, z, p, k, size);
						TER_LOG("Normal %d %d %d %d -> (%f, %f, %f)", x, z, p, k, normal[0], normal[1], normal[2]);
					}

					if (for_bins)
					{
						zeroVec2(texcoord);
					}
					else
					{
						tmeshCalcTexcoord(texcoord, x * position_mult, y, z * position_mult, facing);
					}
					
					// look for this vertex in the vertex cache
					for (v = 0; v < MAX_UNIQUE_VERTS_PER_POS; ++v)
					{
						int v_idx = (z*size + x) * MAX_UNIQUE_VERTS_PER_POS + v;
						if (vert_cache[v_idx] < 0)
						{
							vertex_idxs[k] = vert_cache[v_idx] = gmeshAddVert(tmesh->mesh, position, NULL, normal, NULL, NULL, NULL, for_bins?NULL:texcoord, NULL, NULL, varcolor, NULL, NULL, false, false, false);
							break;
						}

						if (gmeshNearSameVert(tmesh->mesh, vert_cache[v_idx], position, NULL, normal, NULL, NULL, NULL, for_bins?NULL:texcoord, NULL, NULL, varcolor, false, false))
						{
							vertex_idxs[k] = vert_cache[v_idx];
							break;
						}
					}

					assert(vertex_idxs[k] >= 0);
                }

				gmeshAddTri(tmesh->mesh, vertex_idxs[0], vertex_idxs[1], vertex_idxs[2], facing, false);
			}
		}
	}

	if (for_bins)
	{
		U8 terrain_min_weight = TERRAIN_MIN_WEIGHT;
		char path[MAX_PATH];

		if (sameVec2(height_map->map_local_pos, save_debug_terrain_mesh))
		{
			SimpleBufHandle buf;
			sprintf(path, "%s/testmesh.msh", fileTempDir());
			buf = SimpleBufOpenWrite(path, true, NULL, false, false);
			gmeshWriteBinData(tmesh->mesh, buf);
			SimpleBufClose(buf);
		}

		for (i = 0; i < tmesh->mesh->vert_count; ++i)
		{
			bool changed = false;
			for (j = 0; j < tmesh->mesh->varcolor_size; ++j)
			{
				if (tmesh->mesh->varcolors[i*tmesh->mesh->varcolor_size+j] <= terrain_min_weight)
				{
					tmesh->mesh->varcolors[i*tmesh->mesh->varcolor_size+j] = 0;
					changed = true;
				}
			}
			if (changed)
				VarColorNormalize(&tmesh->mesh->varcolors[i*tmesh->mesh->varcolor_size], tmesh->mesh->varcolor_size);
		}

		sprintf(path, "%d_%d", height_map->map_local_pos[0], height_map->map_local_pos[1]);

		if(tmesh->mesh->vert_count) { // Skip reduction if the terrain is just empty. (Cut brush removed everything.)

			gmeshReduceDebug(
				tmesh->mesh, tmesh->mesh,
				terrainGetDesiredMeshError(0),
				terrainGetDesiredMeshTriCount(0, 1),
				TRICOUNT_AND_ERROR_RMETHOD,
				g_TerrainScaleByArea, 0,
				g_TerrainUseOptimalVertPlacement,
				false, true, false, path,
				height_map->zone_map_layer ? height_map->zone_map_layer->filename : NULL);

			for (i = 0; i < tmesh->mesh->vert_count; ++i)
			{
				for (j = 0; j < tmesh->mesh->varcolor_size; ++j)
				{
					if (tmesh->mesh->varcolors[i*tmesh->mesh->varcolor_size+j] <= terrain_min_weight)
						tmesh->mesh->varcolors[i*tmesh->mesh->varcolor_size+j] = 0;
				}
			}
		}
	}

	ScratchFree(varcolor_lookup);
	ScratchFree(vert_cache);
	ScratchFree(face_buffer);

	for (z = 0; z < 3; ++z)
	{
		for (x = 0; x < 3; ++x)
		{
			ScratchFree(all_face_normals[z][x]);
		}
	}

	PERFINFO_AUTO_STOP();

	return tmesh;
}

void freeTerrainMesh(TerrainMesh *terrain_mesh)
{
	if (terrain_mesh)
	{
		eaDestroy(&terrain_mesh->material_names);
		gmeshFreeData(terrain_mesh->mesh);
		free(terrain_mesh->mesh);
		subdivClear(&terrain_mesh->subdiv);
		free(terrain_mesh);
	}
}


// this is called from a single thread
Model *heightMapGenerateModel(HeightMap *height_maps[3][3], char **material_table, int *material_lookup, bool dynamic_normals)
{
	TerrainBuffer *height_buffers[3][3] = {0}, *cut_buffer, *material_buffer;
	HeightMap *height_map = height_maps[1][1];
	HeightMapAtlasData *atlas_data = height_map->data;
	TerrainMesh *terrain_mesh;
	Model *model;
	int i, j, tex_count;
	GMesh output_mesh = {0};
	F32 min_height = HEIGHTMAP_MAX_HEIGHT, max_height = HEIGHTMAP_MIN_HEIGHT;
	Vec3 *all_face_normals[3][3];

	for (i = 0; i < 3; ++i)
	{
		for (j = 0; j < 3; ++j)
		{
			height_buffers[i][j] = heightMapGetBuffer(height_maps[i][j], TERRAIN_BUFFER_HEIGHT, height_map->loaded_level_of_detail);
		}
	}
	material_buffer = heightMapGetBuffer(height_map, TERRAIN_BUFFER_MATERIAL, height_map->loaded_level_of_detail);
	cut_buffer = heightMapGetBuffer(height_map, TERRAIN_BUFFER_ALPHA, height_map->loaded_level_of_detail);

	if (!height_buffers[1][1] || !material_buffer)
		return NULL;

	getFaceNormals(height_map, height_buffers, all_face_normals, height_map->level_of_detail, NULL);

	terrain_mesh = heightMapCreateMesh(height_map, height_buffers[1][1], all_face_normals, material_buffer, material_table, material_lookup, cut_buffer, height_map->level_of_detail, dynamic_normals, false, NULL);

	eaDestroy(&atlas_data->client_data.model_detail_material_names);
	atlas_data->client_data.model_detail_material_names = terrain_mesh->material_names;
	terrain_mesh->material_names = NULL;

	eaDestroyEx(&atlas_data->client_data.model_materials, NULL);
	atlas_data->client_data.model_materials = terrainConvertHeightMapMeshToRenderable(terrain_mesh->mesh, NULL, NULL, NULL, &output_mesh, 1, &min_height, &max_height, true, dynamic_normals, NULL);
	tex_count = gmeshSortTrisByTexID(&output_mesh, NULL);
	assert(tex_count == eaSize(&atlas_data->client_data.model_materials));

	model = tempModelAlloc(TERRAIN_TILE_MODEL_NAME, NULL, tex_count, WL_FOR_WORLD);
	modelFromGmesh(model, &output_mesh);

	model->model_lods[0]->data->texel_density_avg = gmeshUvDensity(terrain_mesh->mesh);
	model->model_lods[0]->data->texel_density_stddev = 0.0f;

	gmeshFreeData(&output_mesh);
	freeTerrainMesh(terrain_mesh);

	return model;
}

static SimpleBufHandle writeTileBin(HeightMap *height_maps[3][3], int ranges_idx, IVec2 rel_pos, int loaded_lod)
{
	TerrainBuffer *height_buffers[3][3], *height_buffer, *cut_buffer, *material_buffer, *occlusion_buffer;
	HeightMap *height_map = height_maps[1][1];
	int stride = 1;
	int color_lod = 0;
	int mat_lod = 2;
	int color_size = GRID_LOD(color_lod);
	int normal_stride = stride / 4;
	TerrainMesh *terrain_mesh;
	GMesh *occlusion_mesh;
	int x, y;
	Geo2LoadData *gld = NULL;
	char path[MAX_PATH];
	SimpleBufHandle mesh_buf = NULL;
	Vec3 *all_face_normals[3][3];
	FILE *terrain_mesh_log = NULL;

	EnterCriticalSection(&terrain_bins_cs);

	if (terrain_mesh_logging)
	{
		sprintf(path, "%s/tile_%d_%d.log", fileTempDir(), height_map->map_local_pos[0], height_map->map_local_pos[1]);
		terrain_mesh_log = fopen(path, (terrain_mesh_logging==2)?"rt":"wt");
	}

	for (y = 0; y < 3; ++y)
	{
		for (x = 0; x < 3; ++x)
		{
			if (height_maps[y][x])
			{
				height_buffers[y][x] = heightMapGetBuffer(height_maps[y][x], TERRAIN_BUFFER_HEIGHT, loaded_lod);
				TER_LOG("Height buffer %s for %d %d", height_buffers[y][x] ? "found" : "not found", height_maps[y][x]->map_local_pos[0], height_maps[y][x]->map_local_pos[1]);
			}
			else
			{
				height_buffers[y][x] = NULL;
			}
		}
	}
	height_buffer = height_buffers[1][1];

	getFaceNormals(height_map, height_buffers, all_face_normals, loaded_lod, terrain_mesh_log);

	material_buffer = heightMapGetBuffer(height_map, TERRAIN_BUFFER_MATERIAL, loaded_lod); // material buffer for mesh creation
	cut_buffer = heightMapGetBuffer(height_map, TERRAIN_BUFFER_ALPHA, loaded_lod);
	occlusion_buffer = heightMapGetBuffer(height_map, TERRAIN_BUFFER_OCCLUSION, 5);

	LeaveCriticalSection(&terrain_bins_cs);

	terrain_mesh = heightMapCreateMesh(height_map, height_buffer, all_face_normals, material_buffer, NULL, NULL, cut_buffer, loaded_lod, false, true, terrain_mesh_log);

	for (y = 0; y < height_buffer->size; y += stride)
	{
		for (x = 0; x < height_buffer->size; x += stride)
		{
			F32 height = height_buffer->data_f32[x+y*height_buffer->size];
			MIN1(g_TerrainThreadProcInfo.ranges_array[ranges_idx], height);
			MAX1(g_TerrainThreadProcInfo.ranges_array[ranges_idx+1], height);
		}
	}

	occlusion_mesh = heightMapCalcOcclusion(height_buffer->data_f32, occlusion_buffer ? occlusion_buffer->data_byte : NULL, height_buffer->size, false);


	sprintf(path, "x%d_z%d.msh_part", rel_pos[0], rel_pos[1]);
	mesh_buf = SimpleBufOpenWrite(path, true, g_TerrainThreadProcInfo.intermediate_output_hog_file, true, false);

	// Models
	gmeshWriteBinData(terrain_mesh->mesh, mesh_buf);
	gmeshWriteBinData(occlusion_mesh, mesh_buf);

	SimpleBufWriteF32(g_TerrainThreadProcInfo.ranges_array[ranges_idx], mesh_buf);
	SimpleBufWriteF32(g_TerrainThreadProcInfo.ranges_array[ranges_idx+1], mesh_buf);

	SimpleBufWriteU32(!g_TerrainThreadProcInfo.layer->terrain.non_playable, mesh_buf); // needs_collision

	// Materials
	if (eaSize(&terrain_mesh->material_names) > 0)
	{
		SimpleBufWriteU8(eaSize(&terrain_mesh->material_names), mesh_buf);
		for (x = 0; x < eaSize(&terrain_mesh->material_names); ++x)
			SimpleBufWriteString(terrain_mesh->material_names[x], mesh_buf);
	}
	else
	{
		SimpleBufWriteU8(1, mesh_buf);
		SimpleBufWriteString("TerrainDefault", mesh_buf);
	}

	//////////////////////////////////////////////////////////////////////////
	// Cleanup

	EnterCriticalSection(&terrain_bins_cs);
		assert(!height_map->terrain_mesh);
		height_map->terrain_mesh = terrain_mesh;
	LeaveCriticalSection(&terrain_bins_cs);

	gmeshFreeData(occlusion_mesh);
	free(occlusion_mesh);

	if (terrain_mesh_log)
		fclose(terrain_mesh_log);

	return mesh_buf;
}

static void heightMapSaveBin(HeightMap *height_maps[3][3])
{
	int l, x, y, stride, normal_size = 0;
	IVec2 rel_pos;
	HeightMap *height_map = height_maps[1][1];
	HeightMapBinAtlas *atlas = NULL;
	IVec2 height_atlas_pos, color_atlas_pos;
	int old_lod = height_map->loaded_level_of_detail;
	bool has_cutout = false;
	int ranges_idx;
	F32 color_shift = height_map->zone_map_layer->terrain.color_shift;
	Vec3 rgb, hsv;

	ranges_idx = 2 * (
						(g_TerrainThreadProcInfo.range->range.max_block[0]+1-g_TerrainThreadProcInfo.range->range.min_block[0]) * 
						(height_map->map_local_pos[1]-g_TerrainThreadProcInfo.range->range.min_block[2]) + 
						(height_map->map_local_pos[0]-g_TerrainThreadProcInfo.range->range.min_block[0])
					 );
	assert(ranges_idx+1 < g_TerrainThreadProcInfo.ranges_array_size*2);

	height_map->unsaved = true;

	verbose_printf("Saving bins for %d, %d (%d)\n", height_map->map_local_pos[0], height_map->map_local_pos[1], height_map->loaded_level_of_detail);
	rel_pos[0] = height_map->map_local_pos[0] - g_TerrainThreadProcInfo.range->range.min_block[0];
	rel_pos[1] = height_map->map_local_pos[1] - g_TerrainThreadProcInfo.range->range.min_block[2];

	g_TerrainThreadProcInfo.ranges_array[ranges_idx] = HEIGHTMAP_MAX_HEIGHT;
	g_TerrainThreadProcInfo.ranges_array[ranges_idx+1] = HEIGHTMAP_MIN_HEIGHT;


	//////////////////////////////////////////////////////////////////////////
	// write out high lod mesh bin file
	{
		SimpleBufHandle mesh_output = writeTileBin(height_maps, ranges_idx, rel_pos, old_lod);
		if (mesh_output)
		{
			EnterCriticalSection(&terrain_bins_cs);
			eaPush(&g_TerrainThreadProcInfo.output_files, mesh_output);
			LeaveCriticalSection(&terrain_bins_cs);
		}
	}

	EnterCriticalSection(&terrain_bins_cs);

	// resample heightmap to LOD 2 (highest LOD)
	if (2 != height_map->loaded_level_of_detail)
		heightMapResample(height_map, 2);

	//////////////////////////////////////////////////////////////////////////
	// create atlas partial bin data (just downsampled colors and height range)
	for (l = 2, stride = 2; l < MAX_TERRAIN_LODS; l++, stride <<= 1)
	{
		TerrainBuffer *color_buffer;
		int color_size = GRID_LOD(l-2);

		color_buffer = heightMapGetBuffer(height_map, TERRAIN_BUFFER_COLOR, l-2);

		atlas = atlasFindByLocation(g_TerrainThreadProcInfo.atlas_list, height_map->map_local_pos, l-2, height_atlas_pos, color_atlas_pos);
		if (!atlas->color_array)
			atlas->color_array = calloc(HEIGHTMAP_ATLAS_COLOR_SIZE*HEIGHTMAP_ATLAS_COLOR_SIZE, 4*sizeof(U8));

		if (color_buffer)
		{
			for (y = 0; y < color_size; y++)
			{
				for (x = 0; x < color_size; x++)
				{
					int r = color_buffer->data_u8vec3[y*color_buffer->size + x][2];
					int g = color_buffer->data_u8vec3[y*color_buffer->size + x][1];
					int b = color_buffer->data_u8vec3[y*color_buffer->size + x][0];
					if(color_shift)
					{
						rgb[0] = b;
						rgb[1] = g;
						rgb[2] = r;
						rgbToHsv(rgb, hsv);
						hsv[0] -= color_shift;
						if(hsv[0] >= 360.0f)
							hsv[0] -= 360.0f;
						else if(hsv[0] < 0.0f)
							hsv[0] += 360.0f;
						hsvToRgb(hsv, rgb);
						b = rgb[0];
						g = rgb[1];
						r = rgb[2];
					}
					atlas->color_array[(y+color_atlas_pos[1])*HEIGHTMAP_ATLAS_COLOR_SIZE*4 + (x+color_atlas_pos[0])*4 + 0] = MIN(b, 255);
					atlas->color_array[(y+color_atlas_pos[1])*HEIGHTMAP_ATLAS_COLOR_SIZE*4 + (x+color_atlas_pos[0])*4 + 1] = MIN(g, 255);
					atlas->color_array[(y+color_atlas_pos[1])*HEIGHTMAP_ATLAS_COLOR_SIZE*4 + (x+color_atlas_pos[0])*4 + 2] = MIN(r, 255);
					if (x < color_size - 1 && y < color_size - 1)
						atlas->color_array[(y+color_atlas_pos[1])*HEIGHTMAP_ATLAS_COLOR_SIZE*4 + (x+color_atlas_pos[0])*4 + 3] = 255;
				}
			}
		}
	}

	//////////////////////////////////////////////////////////////////////////
	// resample terrain back to what it was
	heightMapResample(height_map, old_lod);
	LeaveCriticalSection(&terrain_bins_cs);
}

static void heightMapSaveBinsThreadFunc(int i)
{
	HeightMap **map_cache = g_TerrainThreadProcInfo.map_cache;
	TerrainBlockRange *range = g_TerrainThreadProcInfo.range;
	assert(map_cache);
	assert(i >= 0 && i < g_TerrainThreadProcInfo.thread_count);
	for (; i < eaSize(&map_cache); i += g_TerrainThreadProcInfo.thread_count)
	{
		if (map_cache[i]->map_local_pos[0] >= range->range.min_block[0] && map_cache[i]->map_local_pos[0] <= range->range.max_block[0] &&
			map_cache[i]->map_local_pos[1] >= range->range.min_block[2] && map_cache[i]->map_local_pos[1] <= range->range.max_block[2])
		{
			HeightMap *height_maps[3][3] = {0};
			height_maps[1][1] = map_cache[i];
			heightMapGetNeighbors(height_maps, map_cache);
			heightMapSaveBin(height_maps);
		}
	}

	EnterCriticalSection(&terrain_bins_cs);
	g_TerrainThreadProcInfo.remaining--;
	LeaveCriticalSection(&terrain_bins_cs);
}

static DWORD WINAPI heightMapSaveBinsThreadProc( LPVOID lpParam )
{
	EXCEPTION_HANDLER_BEGIN

	SET_FP_CONTROL_WORD_DEFAULT;
	heightMapSaveBinsThreadFunc(*(int*)lpParam);

	devassert(ScratchPerThreadOutstandingAllocSize() == 0);
	ScratchFreeThisThreadsStack();

	EXCEPTION_HANDLER_END
	return 0;
}

static void terrainAddTimestamp(TerrainTimestamps *timestamps, const char *filename)
{
	TerrainTimestamp *dep = StructCreate(parse_TerrainTimestamp);
	dep->filename = StructAllocString(filename);
	dep->time = fileLastChanged(filename);
	MAX1(timestamps->bin_time, dep->time);
	eaPush(&timestamps->deplist, dep);
}

static void terrainGetTimestamps(ZoneMapLayer *layer, TerrainBlockRange *block, TerrainTimestamps *timestamps)
{
	int i;
	char filename[MAX_PATH];
	IVec2 pos = { 0, 0 };

	timestamps->version = TERRAIN_BIN_VERSION + (gConf.iDXTQuality * 0.0001f);
	timestamps->bin_time = 0;

	terrainAddTimestamp(timestamps, layer->filename);

	if(layer->zmap_parent && layer->zmap_parent->map_info.genesis_data)
	{
		//Add zone map
		sprintf(filename, "%s", zmapGetFilename(layer->zmap_parent));
		if (fileExists(filename))
			terrainAddTimestamp(timestamps, filename);
		//Add vista maps
		for ( i=0; i < eaSize(&layer->zmap_parent->map_info.secondary_maps); i++ )
		{
			ZoneMapInfo *secondary_zmap = worldGetZoneMapByPublicName(layer->zmap_parent->map_info.secondary_maps[i]->map_name);
			sprintf(filename, "%s", zmapInfoGetFilename(secondary_zmap));
			if (fileExists(filename))
				terrainAddTimestamp(timestamps, filename);
		}
	}


	getTerrainSourceFilename(filename, MAX_PATH, &heightmap_def, block, pos, layer);
	if (fileExists(filename))
		terrainAddTimestamp(timestamps, filename);

	getTerrainSourceFilename(filename, MAX_PATH, &holemap_def, block, pos, layer);
	if (fileExists(filename))
		terrainAddTimestamp(timestamps, filename);

	getTerrainSourceFilename(filename, MAX_PATH, &colormap_def, block, pos, layer);
	if (fileExists(filename))
		terrainAddTimestamp(timestamps, filename);

	getTerrainSourceFilename(filename, MAX_PATH, &tiffcolormap_def, block, pos, layer);
	if (fileExists(filename))
		terrainAddTimestamp(timestamps, filename);

	getTerrainSourceFilename(filename, MAX_PATH, &materialmap_def, block, pos, layer);
	if (fileExists(filename))
		terrainAddTimestamp(timestamps, filename);

	getTerrainSourceFilename(filename, MAX_PATH, &objectmap_def, block, pos, layer);
	if (fileExists(filename))
		terrainAddTimestamp(timestamps, filename);

	//Currently changing soil depth does not need to rebin.  If that changes more needs to be added here.
}

static void terrainBlockSaveBins(ZoneMapLayer *layer, TerrainBlockRange *range)
{
	int i;
	static bool crit_initialized = false;
    HeightMap **map_cache = range->map_cache;
	HeightMapBinAtlas **atlas_list = NULL;
	int ranges_array_size = (range->range.max_block[0]+1-range->range.min_block[0])*(range->range.max_block[2]+1-range->range.min_block[2]);
	F32 *ranges_array = calloc(ranges_array_size, 2*sizeof(F32));
	char intermediate_bin_filename[MAX_PATH];
	int old_material_table_size;
	int *begin;
	bool created;
	int err_return;
	char bin_filename_write[MAX_PATH];
	TerrainTimestamps *source_timestamps;
	U32 gimme_timestamp;
	SimpleBufHandle buf;
	char base_dir[MAX_PATH];

	if (!map_cache)
		return;

	old_material_table_size = eaSize(&layer->terrain.material_table);

	for (i = 0; i < eaSize(&map_cache); i++)
	{
		HeightMap *height_maps[3][3];
		height_maps[1][1] = map_cache[i];
		heightMapGetNeighbors(height_maps, map_cache);
	}

	if (!crit_initialized)
	{
		InitializeCriticalSection(&terrain_bins_cs);
		crit_initialized = true;
	}

	// Create intermediate hogg file
	worldGetTempBaseDir(layer->filename, SAFESTR(base_dir));
	sprintf(intermediate_bin_filename, "%s/%s_intermediate.hogg", base_dir, layer->terrain.blocks[range->block_idx]->range_name);
	fileLocateWrite(intermediate_bin_filename, bin_filename_write);
    if (true)
    {
		char new_name[MAX_PATH];
		strcpy(new_name, bin_filename_write);
		strcat(new_name, ".old");
		if (wl_state.delete_hoggs)
		{
			if (fileExists(bin_filename_write) && rename(bin_filename_write, new_name) != 0)
			{
				Errorf("Failed to remove bin %s!", bin_filename_write);
			}
		}
		else
		{
			if (fileExists(bin_filename_write) && fileCopy(bin_filename_write, new_name) != 0)
			{
				Errorf("Failed to remove bin %s!", bin_filename_write);
			}
		}
    }

	g_TerrainThreadProcInfo.intermediate_output_hog_file = hogFileReadEx(bin_filename_write, &created, PIGERR_ASSERT, &err_return, HOG_MUST_BE_WRITABLE|HOG_NO_INTERNAL_TIMESTAMPS, 1024);
	hogFileLock(g_TerrainThreadProcInfo.intermediate_output_hog_file);
	hogDeleteAllFiles(g_TerrainThreadProcInfo.intermediate_output_hog_file);

	// Calculate timestamps
	source_timestamps = StructCreate(parse_TerrainTimestamps);
    terrainGetTimestamps(layer, range, source_timestamps);
    
	g_TerrainThreadProcInfo.layer = layer;
	g_TerrainThreadProcInfo.map_cache = map_cache;
	g_TerrainThreadProcInfo.range = range;
	g_TerrainThreadProcInfo.ranges_array = ranges_array;
	g_TerrainThreadProcInfo.ranges_array_size = ranges_array_size;
	g_TerrainThreadProcInfo.atlas_list = &atlas_list;

	systemSpecsInit();
	g_TerrainThreadProcInfo.thread_count = MAX(1, system_specs.numVirtualCPUs);
	if (disable_threaded_binning)
		g_TerrainThreadProcInfo.thread_count = 1;
	begin = _alloca(g_TerrainThreadProcInfo.thread_count * sizeof(int));

	SET_FP_CONTROL_WORD_DEFAULT;
	if (g_TerrainThreadProcInfo.thread_count == 1)
	{
		heightMapSaveBinsThreadFunc(0);
	}
	else
	{
		ManagedThread **threads;
		g_TerrainThreadProcInfo.remaining = g_TerrainThreadProcInfo.thread_count;
		threads = _alloca(g_TerrainThreadProcInfo.thread_count * sizeof(*threads));
		for (i = 0; i < g_TerrainThreadProcInfo.thread_count; i++)
		{
			begin[i] = i;
			threads[i] = tmCreateThread(heightMapSaveBinsThreadProc, &begin[i]);
			assert(threads[i]);
		}

		while (g_TerrainThreadProcInfo.remaining > 0)
		{
			Sleep(10);
		}
		
		for (i = 0; i < g_TerrainThreadProcInfo.thread_count; i++)
			tmDestroyThread(threads[i], false);
	}

	//////////////////////////////////////////////////////////////////////////
	// write out terrain object bins

	layerInitObjectWrappers(layer, eaSize(&layer->terrain.object_table));
    terrainCreateObjectGroups(map_cache, range, layer, layer->terrain.object_defs, NULL, layer->terrain.exclusion_version, layer->terrain.color_shift);

	for (i = 0; i < eaSize(&map_cache); ++i)
	{
		if (map_cache[i]->map_local_pos[0] >= range->range.min_block[0] && map_cache[i]->map_local_pos[0] <= range->range.max_block[0] &&
			map_cache[i]->map_local_pos[1] >= range->range.min_block[2] && map_cache[i]->map_local_pos[1] <= range->range.max_block[2])
		{
			HeightMap *height_map = map_cache[i];
			HeightMap *height_maps[3][3] = {0};
			height_maps[1][1] = height_map;
			heightMapGetNeighbors(height_maps, map_cache);

			if (eaSize(&height_map->object_instances) > 0 && height_map->terrain_mesh)
			{
				char path[MAX_PATH];
				U32 inst, count;
				IVec2 rel_pos;

				rel_pos[0] = height_map->map_local_pos[0] - range->range.min_block[0];
				rel_pos[1] = height_map->map_local_pos[1] - range->range.min_block[2];

				sprintf(path, "x%d_z%d.obj", rel_pos[0], rel_pos[1]);
				buf = SimpleBufOpenWrite(path, true, g_TerrainThreadProcInfo.intermediate_output_hog_file, true, false);

				count = eaSize(&height_map->object_instances);

				SimpleBufWriteU32(source_timestamps->bin_time, buf);
				SimpleBufWriteU32(count, buf);

				for (inst = 0; inst < count; ++inst)
					terrainWriteBinnedObject(height_map->object_instances[inst], buf);

				eaPush(&g_TerrainThreadProcInfo.output_files, buf);
			}

			freeTerrainMesh(height_map->terrain_mesh);
			height_map->terrain_mesh = NULL;
		}
	}

	// make partial atlases
	for (i = 0; i < eaSize(&atlas_list); i++)
		layerSaveBlockAtlas(layer, range->block_idx, atlas_list[i], g_TerrainThreadProcInfo.intermediate_output_hog_file, &g_TerrainThreadProcInfo.output_files);

	eaDestroyEx(&atlas_list, atlasBinFree);

	// write all data to hoggs in filename order
	eaQSortG(g_TerrainThreadProcInfo.output_files, SimpleBufFilenameComparator);
	eaDestroyEx(&g_TerrainThreadProcInfo.output_files, SimpleBufClose);

	// write bounds
	buf = SimpleBufOpenWrite("bounds.dat", true, g_TerrainThreadProcInfo.intermediate_output_hog_file, true, false);
	SimpleBufWriteF32Array(ranges_array, ranges_array_size*2, buf);
	SimpleBufClose(buf);

	// don't write out timestamps files until the end, in case binning crashes
	terrainWriteTimestamps(g_TerrainThreadProcInfo.intermediate_output_hog_file, source_timestamps);

	hogFileUnlock(g_TerrainThreadProcInfo.intermediate_output_hog_file);
	hogFileDestroy(g_TerrainThreadProcInfo.intermediate_output_hog_file, true);
	g_TerrainThreadProcInfo.intermediate_output_hog_file = NULL;

	// Move .hogg to final location, unless the existing file matches
	bflUpdateOutputFile(intermediate_bin_filename);

	if (!bin_manifest_cache)
		bin_manifest_cache = gimmeDLLCacheManifestBinFiles(intermediate_bin_filename);

	if (gimmeDLLCheckBinFileMatchesManifest(bin_manifest_cache, intermediate_bin_filename, &gimme_timestamp) )
	{
		fileSetTimestamp(bin_filename_write, gimme_timestamp);
	}
	else if (0)
	{
		U32 file_timestamp = bflGetVersionTimestamp(BFLT_TERRAIN_BIN);
		MAX1(file_timestamp, source_timestamps->bin_time);
		fileSetTimestamp(bin_filename_write, file_timestamp);
	}
	StructDestroy(parse_TerrainTimestamps, source_timestamps);
}

static void heightMapServerDownsampleColors(HeightMap **map_cache)
{
	int i, k;
	U32 min_lod = MAX_TERRAIN_LODS;
	for (i = 0; i < eaSize(&map_cache); i++)
		if (map_cache[i]->loaded_level_of_detail < min_lod)
			min_lod = map_cache[i]->loaded_level_of_detail;
	for (k=GET_COLOR_LOD(min_lod) ; k < GET_COLOR_LOD(MAX_TERRAIN_LODS); k++)
	{
		for (i = 0; i < eaSize(&map_cache); i++)
		{
			HeightMap *height_maps[3][3] = { 0 };
			height_maps[1][1] = map_cache[i];
			heightMapGetNeighbors(height_maps, map_cache);
			heightMapUpdateColors(height_maps, k);
		}
	}
}

void terrainSaveBins(ZoneMapLayer *layer, int current_layer, int total_layers)
{
	int block;
	char bin_filename[256];
	char bin_filename_write[256];

	assert(wlIsServer());

	layerSetMode(layer, LAYER_MODE_GROUPTREE, false, true, false);

	if (eaSize(&layer->terrain.blocks) > 0)
	{
		for (block = 0; block < eaSize(&layer->terrain.blocks); block++)
		{
			TerrainBlockRange *range = layer->terrain.blocks[block];
			if (range->interm_file)
			{
				hogFileDestroy(range->interm_file, true);
				range->interm_file = NULL;
			}
		}

		for (block = 0; block < eaSize(&layer->terrain.blocks); block++)
		{
			TerrainBlockRange *range = layer->terrain.blocks[block];

			if (!range->need_bins)
				continue;

			loadstart_printf("Binning terrain layer %d of %d, block %d of %d: (%d, %d) - (%d, %d)...", 
				current_layer+1, total_layers, range->block_idx+1, eaSize(&layer->terrain.blocks),
				range->range.min_block[0], range->range.min_block[2], range->range.max_block[0], range->range.max_block[2]);
			SendStringToCB(CBSTRING_COMMENT, "Binning terrain layer %d of %d, block %d of %d: (%d, %d) - (%d, %d)", 
				current_layer+1, total_layers, range->block_idx+1, eaSize(&layer->terrain.blocks),
				range->range.min_block[0], range->range.min_block[2], range->range.max_block[0], range->range.max_block[2]);

			terrainBlockLoadSource(layer, range, true);
			heightMapServerDownsampleColors(range->map_cache);
			terrainBlockSaveBins(layer, range);

			eaDestroyEx(&range->map_cache, heightMapDestroy);

			loadend_printf(" done.");

			if (layer->bin_status_callback)
			{
				layer->bin_status_callback(layer->bin_status_userdata, block+1, eaSize(&layer->terrain.blocks));
			}
		}

		layerUpdateBounds(layer);
		updateTerrainBlocks(layer);
		layerUpdateGeometry(layer, false);
	}

	// Create header bin file
	layerGetHeaderBinFile(bin_filename, ARRAY_SIZE_CHECKED(bin_filename), layer);
	fileLocateWrite(bin_filename, bin_filename_write);
	layer->terrain.layer_timestamp = bflFileLastChanged(layer->filename);
	ParserWriteTextFile(bin_filename_write, parse_ZoneMapTerrainLayer, &layer->terrain, 0, 0);

	layer->bin_status_userdata = layer->bin_status_callback = NULL;
}

/*
GenesisZoneNodeLayout* terrainMakeVistaNodes(TerrainEditorSource *source, GenesisGeotype *geo_type, GenesisEcosystem *ecosystem, U32 seed)
{
	GenesisZoneNodeLayout *ret_layout = NULL;
	GenesisZoneNodeLayout *orig_layout = NULL;
	GenesisRoomLayout *vista_room_layout;
	GenesisRoomLayout *layout_copy;
	U32 room_seed = 0;

	vista_room_layout = RefSystem_ReferentFromString(GENESIS_ROOM_DICTIONARY, "SystemVista");
	if(!vista_room_layout)
		return NULL;

	room_seed = genesisGetVistaSeed(geo_type, ecosystem, seed);

	orig_layout = source->node_layout;
	source->node_layout = NULL;

	layout_copy = StructClone(parse_GenesisRoomLayout, vista_room_layout);
	if(genesisMoveRoomsToNodes(source, layout_copy, room_seed, true, true, true))
		ret_layout = genesisMakeVistaNodeLayout(source, source->node_layout);
	StructDestroy(parse_GenesisRoomLayout, layout_copy);

	if(source->node_layout)
		StructDestroy(parse_GenesisRoomLayout, source->node_layout);
	source->node_layout = orig_layout;

	return ret_layout;
}
*/

void terrainBinDownsampleAndSave(ZoneMapLayer *layer, TerrainBlockRange *range)
{
	heightMapServerDownsampleColors(range->map_cache);
	terrainBlockSaveBins(layer, range);
}

int terrainBlockCheckTimestamps(ZoneMapLayer *layer, TerrainBlockRange *block)
{
	bool matches = false;
	TerrainTimestamps *source_timestamps = StructCreate(parse_TerrainTimestamps);
	TerrainTimestamps *bin_timestamps = StructCreate(parse_TerrainTimestamps);

	terrainGetTimestamps(layer, block, source_timestamps);

	if (terrainReadTimestamps(layer, block, bin_timestamps))
	{
		source_timestamps->bin_time = bin_timestamps->bin_time;
		if (!StructCompare(parse_TerrainTimestamps, source_timestamps, bin_timestamps, 0, 0, 0))
		{
			matches = true;
		}
	}
	StructDestroy(parse_TerrainTimestamps, source_timestamps);
	StructDestroy(parse_TerrainTimestamps, bin_timestamps);
	return matches ? 0 : 1;
}

int heightMapLoadTerrainObjects(ZoneMapLayer *layer, TerrainBlockRange *range, IVec2 rel_pos, bool new_block)
{
	char relpath[MAX_PATH];
	SimpleBufHandle buf;
	U32 inst_count = 0;

	sprintf(relpath, "x%d_z%d.obj", rel_pos[0], rel_pos[1]);
	if (buf = readTerrainFileHogg(layer, range->block_idx, relpath))
	{
		TerrainBinnedObjectGroup *instance_group;
		U32 inst;
		U32 bin_time;

		SimpleBufReadU32(&bin_time, buf);
		if (!SimpleBufReadU32(&inst_count, buf))
		{
			SimpleBufClose(buf);
			return 0;
		}

		instance_group = calloc(1, sizeof(TerrainBinnedObjectGroup));
		copyVec2(rel_pos, instance_group->rel_pos);
		eaPush(&layer->terrain.binned_instance_groups, instance_group);
		eaSetSize(&instance_group->objects, inst_count);

		for (inst = 0; inst < inst_count; inst++)
		{
			TerrainBinnedObject *obj = calloc(1, sizeof(TerrainBinnedObject));
			terrainReadBinnedObject(obj, buf);
			instance_group->objects[inst] = obj;
		}

		SimpleBufClose(buf);
	}
	return inst_count;
}

