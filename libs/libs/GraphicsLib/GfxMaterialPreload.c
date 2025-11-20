#include "GfxMaterialPreload.h"
#include "GfxMaterials.h"
#include "GfxMaterialProfile.h"
#include "GfxGeo.h"
#include "GfxDebug.h"
#include "GfxPostprocess.h"
#include "Materials.h"
#include "dynFxFastParticle.h"
#include "RdrState.h"
#include "DynamicCache.h"
#include "utilitiesLib.h"
#include "RdrShader.h"
#include "WorldGrid.h"
#include "GfxWorld.h"
#include "GfxLights.h"
#include "GfxSky.h"
#include "GfxLoadScreens.h"
#include "GfxTextures.h"
#include "GfxShader.h"
#include "GfxPrimitive.h"
#include "GfxLightOptions.h"
#include "Clipper.h" //include this before GfxSprite.h when inside GraphicsLib to get the fully inlined clipper functions
#include "GfxSprite.h"
#include "inputLib.h"
#include "fileutil2.h"
#include "FolderCache.h"
#include "ConsoleDebug.h"
#include "wlPerf.h"

AUTO_RUN_ANON(memBudgetAddMapping(__FILE__, BUDGET_Materials););

void gfxMaterialPreloadFinishPreloading(void);
static ShaderTemplate **templates_to_preload=NULL;
int preload_count_remaining=0;

int gfxMaterialPreloadGetLoadingCount(void)
{
	return preload_count_remaining;
}

typedef struct TemplatePreload
{
	const char *folder;
	bool recursive;
	bool templateNameNotPath;
	int count;
} TemplatePreload;

typedef struct TemplatePreloadLists
{
	TemplatePreload **early;
	TemplatePreload **late;
} TemplatePreloadLists;

static bool templateIsPreloaded(const ShaderTemplate *templ, TemplatePreload **template_preload_list)
{
	int slenMaterials = 10; // (int)strlen("Materials/");
	if (strstri(templ->filename, "NoPreload"))
		return false;

	FOR_EACH_IN_EARRAY(template_preload_list, TemplatePreload, tp)
	{
		if (tp->templateNameNotPath)
		{
			if (stricmp(templ->template_name, tp->folder)==0)
			{
				tp->count++;
				return true;
			}
		} else if (tp->recursive) {
			if (strStartsWith(templ->filename + slenMaterials, tp->folder)) {
				tp->count++;
				return true;
			}
		} else {
			char dirname[MAX_PATH];
			strcpy(dirname, templ->filename + slenMaterials);
			getDirectoryName(dirname);
			if (stricmp(dirname, tp->folder)==0) {
				tp->count++;
				return true;
			}
		}
	}
	FOR_EACH_END;
	return false;
}

static void gfxMaterialFlagPreloadedTemplates(TemplatePreload **template_preload_list)
{
	// Start all relevant shaders compiling/getting sent to renderer.  Flush them/draw them later.
	FOR_EACH_IN_EARRAY(material_load_info.templates, ShaderTemplate, templ)
	{
		if( !shaderTemplateIsSupported( templ ))
		{
			templ->graph->graph_render_info->has_been_preloaded = 1;
			continue;
		}
		assert(templ->graph->graph_render_info);
		if (templ->graph->graph_render_info->has_been_preloaded)
			continue;
		if (templateIsPreloaded(templ, template_preload_list))
		{
			eaPush(&templates_to_preload, templ);
			templ->graph->graph_render_info->has_been_preloaded = 1;
		}
	}
	FOR_EACH_END;

	if (0)
	{
		// Not useful since we're doing the per-map info now
		verbose_printf("\n");
		verbose_printf("Template folder breakdown:\n");
		FOR_EACH_IN_EARRAY(template_preload_list, TemplatePreload, tp)
			verbose_printf("  %s : %d\n", tp->folder, tp->count);
		FOR_EACH_END;
	}

	//if (gfx_state.allow_preload_materials)
	gfx_state.debug.error_on_non_preloaded_materials = 1;
}

static ParseTable parse_preload_file[] = {
	{ "recursive",				TOK_BOOL(TemplatePreload,recursive, 0)},
	{ "templateNameNotPath",	TOK_BOOL(TemplatePreload,templateNameNotPath, 1)},
	{ "name",					TOK_STRUCTPARAM|TOK_STRING(TemplatePreload,folder,0)},
	{ "\n",						TOK_END,			0},
	{ "", 0, 0 }
};
static ParseTable parse_preload_dir[] = {
	{ "recursive",				TOK_BOOL(TemplatePreload,recursive, 1)},
	{ "templateNameNotPath",	TOK_BOOL(TemplatePreload,templateNameNotPath, 0)},
	{ "name",					TOK_STRUCTPARAM|TOK_STRING(TemplatePreload,folder,0)},
	{ "\n",						TOK_END,			0},
	{ "", 0, 0 }
};

static ParseTable parse_TemplatePreloadLists[] = {
	{ "Template",	TOK_STRUCT_X | TOK_EARRAY | TOK_INDIRECT,	offsetof(TemplatePreloadLists, early),	sizeof(TemplatePreload),		parse_preload_file},
	{ "Folder",		TOK_REDUNDANTNAME | TOK_STRUCT_X | TOK_EARRAY | TOK_INDIRECT,	offsetof(TemplatePreloadLists, early),	sizeof(TemplatePreload),		parse_preload_dir },
	{ "LateTemplate",	TOK_STRUCT_X | TOK_EARRAY | TOK_INDIRECT,	offsetof(TemplatePreloadLists, late),	sizeof(TemplatePreload),		parse_preload_file},
	{ "LateFolder",		TOK_REDUNDANTNAME | TOK_STRUCT_X | TOK_EARRAY | TOK_INDIRECT,	offsetof(TemplatePreloadLists, late),	sizeof(TemplatePreload),		parse_preload_dir },
	{ "", 0, 0 }
};

AUTO_RUN;
void initMatPreloadTPIs(void)
{
	ParserSetTableInfo(parse_preload_dir, sizeof(TemplatePreload), "parse_preload_dir", NULL, __FILE__, SETTABLEINFO_ALLOW_CRC_CACHING);
	ParserSetTableInfo(parse_preload_file, sizeof(TemplatePreload), "parse_preload_file", NULL, __FILE__, SETTABLEINFO_ALLOW_CRC_CACHING);
	ParserSetTableInfo(parse_TemplatePreloadLists, sizeof(TemplatePreloadLists), "parse_TemplatePreloadLists", NULL, __FILE__, SETTABLEINFO_ALLOW_CRC_CACHING);
}

static TemplatePreloadLists g_preload_list;

static void gfxMaterialPreloadLoadLists(void)
{
	if (!g_preload_list.late)
		ParserLoadFiles(NULL, "client/MaterialPreloadList.txt", "MaterialPreloadList.bin", 0, parse_TemplatePreloadLists, &g_preload_list);
}

void gfxMaterialFlagPreloadedGlobalTemplates(void)
{
	gfxMaterialPreloadLoadLists();
	gfxMaterialFlagPreloadedTemplates(g_preload_list.early);
}

void gfxMaterialFlagPreloadedForZoneMap(ZoneMap *zone_map)
{
	ShaderTemplate **templates = materialGetTemplatesUsedByMap(zmapGetFilename(zone_map));
	int countNew=0;
	int countNonEarly=0;

	gfxMaterialPreloadLoadLists();

	// Count how many non-early-loaded templates are referenced
	FOR_EACH_IN_EARRAY(templates, ShaderTemplate, templ)
	{
		if( !shaderTemplateIsSupported( templ ))
		{
			templ->graph->graph_render_info->has_been_preloaded = 1;
			countNew++;
			continue;
		}
		assert(templ->graph->graph_render_info);
		if (templ->graph->graph_render_info->has_been_preloaded)
			continue;
		if (!templateIsPreloaded(templ, g_preload_list.early))
		{
			countNonEarly++;
		}
	}
	FOR_EACH_END;
	if (countNonEarly > 25)
	{
		ErrorFilenamef(zmapGetFilename(zone_map), "More than 25 unique Material Templates referenced in the map.");
	}

	FOR_EACH_IN_EARRAY(templates, ShaderTemplate, templ)
	{
		if( !shaderTemplateIsSupported( templ ))
		{
			templ->graph->graph_render_info->has_been_preloaded = 1;
			countNew++;
			continue;
		}
		assert(templ->graph->graph_render_info);
		if (templ->graph->graph_render_info->has_been_preloaded)
			continue;
		if (templateIsPreloaded(templ, g_preload_list.late))
		{
			eaPush(&templates_to_preload, templ);
			templ->graph->graph_render_info->has_been_preloaded = 1;
			countNew++;
		} else {
			ErrorFilenameGroupf(zmapGetFilename(zone_map), "Art", 14,
				"Map is referencing disallowed/non-preloaded template %s", templ->graph_parser.filename);
		}
	}
	FOR_EACH_END;
	if (countNew || countNonEarly)
	{
		loadupdate_printf("Currently loaded map references %d total templates, %d non-early, %d new to be preloaded.\n", eaSize(&templates), countNonEarly, countNew);
	}
	eaDestroy(&templates);
	gfx_state.debug.error_on_non_preloaded_materials = 1;
}

void gfxMaterialFlagPreloadedMapSpecificTemplates(bool bEarly)
{
	int i;
	gfxMaterialPreloadLoadLists();
	// Load global things like characters, FX
	gfxMaterialFlagPreloadedTemplates(g_preload_list.early);

	// Load just that referenced by the currently loaded map
	for (i=0; i<worldGetLoadedZoneMapCount(); i++)
	{
		ZoneMap *zone_map = worldGetLoadedZoneMapByIndex(i);
		gfxMaterialFlagPreloadedForZoneMap(zone_map);
	}
}

AUTO_COMMAND;
void printMapSpecificTemplates(void)
{
	int i;
	char *estr=NULL;
	gfxMaterialPreloadLoadLists();
	// Load just that referenced by the currently loaded map
	for (i=0; i<worldGetLoadedZoneMapCount(); i++)
	{
		ZoneMap *zone_map = worldGetLoadedZoneMapByIndex(i);
		ShaderTemplate **templates = materialGetTemplatesUsedByMap(zmapGetFilename(zone_map));

		estrPrintf(&estr, "Total templates referenced: %d\n", eaSize(&templates));
		FOR_EACH_IN_EARRAY(templates, ShaderTemplate, templ)
		{
			if( !shaderTemplateIsSupported( templ ))
				continue;
			if (templateIsPreloaded(templ, g_preload_list.late))
			{
				if (templateIsPreloaded(templ, g_preload_list.early))
					estrConcatf(&estr, "  %s (always loaded)\n", templ->template_name);
				else
					estrConcatf(&estr, "  %s (map-specific)\n", templ->template_name);

			}
		}
		FOR_EACH_END;
	}
	Alertf("%s", estr);
	estrDestroy(&estr);
}


void gfxMaterialFlagPreloadedAllTemplates(void)
{
	gfxMaterialPreloadLoadLists();
	gfxMaterialFlagPreloadedTemplates(g_preload_list.early);
	gfxMaterialFlagPreloadedTemplates(g_preload_list.late);
}

bool gfxMaterialShaderTemplateIsPreloaded(const ShaderTemplate *shader_template)
{
	gfxMaterialPreloadLoadLists();
	return templateIsPreloaded(shader_template, g_preload_list.early) || templateIsPreloaded(shader_template, g_preload_list.late);
}

bool gfxMaterialShaderTemplateIsPreloadedForFX(const ShaderTemplate *shader_template)
{
	gfxMaterialPreloadLoadLists();
	return templateIsPreloaded(shader_template, g_preload_list.early);
}

bool gfxMaterialValidateForFx(Material *material, const char **ppcTemplateName, bool isForSplat)
{
	ShaderTemplate *templ;
	assert(material);
	if (isForSplat)
	{
		// TODO Verify if the material has an alpha-bordered texture or not
	}
	templ = materialGetData(material)->graphic_props.shader_template;
	*ppcTemplateName = templ->template_name;
	return gfxMaterialShaderTemplateIsPreloadedForFX(templ);
}

static Model *preloadGetModel(bool bSkinned)
{
	ModelToDraw models[NUM_MODELTODRAWS];
	Model *model;

	// Find and load model, and make sure it's loaded and sent to the video card
	if (bSkinned) {
		model = modelFind("M_Corey_Chest.GEO_chest", true, WL_FOR_ENTITY|WL_FOR_WORLD);
	} else {
		model = modelFind("sys_unit_box", true, WL_FOR_WORLD);
	}
	assertmsgf(model, "Missing system model %s (used for preloading)", bSkinned?"M_Corey_Chest.GEO_chest":"sys_unit_box");

	// use LOD 0
	while (!gfxDemandLoadModel(model, models, ARRAY_SIZE(models), 0, 1, 0, NULL, model->radius))
	{
		gfxLockActiveDevice();
		gfxGeoOncePerFramePerDevice();
		gfxGeoDoQueuedFrees();
		gfxUnlockActiveDevice();
		Sleep(1);
	}
	return model;
}

static Model *preloadGetPlaneModel(void)
{
	ModelToDraw models[NUM_MODELTODRAWS];
	Model *model;

	// Find and load model, and make sure it's loaded and sent to the video card
	model = modelFind("Plane_DoubleSided_100ft", true, WL_FOR_ENTITY|WL_FOR_WORLD);
	assertmsgf(model, "Missing system model %s (used for preloading)", "Plane_DoubleSided_100ft");

	// use LOD 0
	while (!gfxDemandLoadModel(model, models, ARRAY_SIZE(models), 0, 1, 0, NULL, model->radius))
	{
		gfxLockActiveDevice();
		gfxGeoOncePerFramePerDevice();
		gfxGeoDoQueuedFrees();
		gfxUnlockActiveDevice();
		Sleep(1);
	}
	return model;
}

/*static*/ void preloadBeginEndFrame(int begin, SingleModelParams *params, bool bSkinned)
{
	static F32 saved_time;
	static GfxCameraView saved_camera;
	static SkinningMat4 *bone_infos;
	static bool in_time_misc;
	if (!gfx_state.currentDevice)
		return;
	if (begin) {
		WorldGraphicsData *world_graphics_data = worldGetWorldGraphicsData();
		Mat4 camera_matrix;
		Mat44 temp_mat, scale_mat;
		Vec3 ypr;
		in_time_misc = world_perf_info.in_time_misc;

		saved_time = gfx_state.frame_time;
		gfxCameraSaveSettings(&saved_camera, gfxGetActiveCameraView());

		if (!in_time_misc)
		{
			gfxResetFrameCounters();
			wlPerfStartMiscBudget();
		}

		gfxOncePerFrame(0, 0.01, 0, false);

		gfx_state.frame_time = 0;
		gfx_state.client_frame_timestamp++;

		rdrSetupPerspectiveProjection(gfx_state.currentCameraView->projection_matrix, 90, 1.0, gfx_state.near_plane_dist, gfx_state.far_plane_dist);
		rdrSetupPerspectiveProjection(temp_mat, 90, 1.0, gfx_state.near_plane_dist, gfx_state.far_plane_dist * 17);
		copyMat44(unitmat44,scale_mat);
		scaleByMat44(scale_mat,17);
		scale_mat[3][3] = 1;
		mulMat44Inline(temp_mat,scale_mat,gfx_state.currentCameraView->far_projection_matrix);
		rdrSetupPerspectiveProjection(temp_mat, 90, 1.0, gfx_state.near_plane_dist, gfx_state.far_plane_dist * 4250);
		copyMat44(unitmat44,scale_mat);
		scaleByMat44(scale_mat,4250);
		scale_mat[3][3] = 1;
		mulMat44Inline(temp_mat,scale_mat,gfx_state.currentCameraView->sky_projection_matrix);
		frustumSet(&gfx_state.currentCameraView->new_frustum, 90, 1.0f, gfx_state.near_plane_dist, gfx_state.far_plane_dist);

		gfxSetActiveDevice(gfxGetActiveOrPrimaryDevice());
		inpUpdateEarly(gfxGetActiveInputDevice());
		gfxDisplayDebugInterface2D(false);
		inpUpdateLate(gfxGetActiveInputDevice());
		if (utilitiesLibShouldQuit()) {
			winAboutToExitMaybeCrashed();
			exit(0);
		}

		setVec3(ypr, 0.5, 0.3, 0);
		createMat3YPR(camera_matrix, ypr);
		setVec3(camera_matrix[3], 20, 30, 50);
		if (gfx_state.debug.material_preload_debug)
			scaleVec3(camera_matrix[3], 0.125, camera_matrix[3]);
		gfxSetActiveCameraMatrix(camera_matrix, false);
		gfxFlipCameraFrustum(gfx_state.currentCameraView);
		gfxTellWorldLibCameraPosition(); // Call this only on the primary camera

		gfxStartMainFrameAction(false, false, true, false, true);
		gfxFillDrawList(true, NULL);
		if (world_graphics_data) {
			gfxRemoveLight(world_graphics_data->sun_light);
			gfxRemoveLight(world_graphics_data->sun_light_2);
		}

		if (params) {
			copyMat4(unitmat, params->world_mat);
			setVec3(params->world_mat[3], 0, 0, 0);

			params->model = preloadGetModel(bSkinned);
			setVec3(params->color, 0.3, 0.5, 0.7);
			params->alpha = 255;

			if (bSkinned) {
				int i;
				params->num_bones = 50;
				bone_infos = aligned_calloc(params->num_bones, sizeof(*bone_infos), VEC_ALIGNMENT);
				params->bone_infos = bone_infos;
				for (i=0; i<params->num_bones; i++) {
					copyMat34(unitmat44, bone_infos[i]);
				}
			}
		}

	} else {
		ZeroStruct(&draw_list_debug_state);

		if (!gfx_state.debug.material_preload_debug)
			gfxLoadingDisplayScreen(true);

		gfxDrawFrame();

		// since we are changing lights after every frame, we don't want consider lights
		// from prior frames for fading shadow transitions
		gfxClearCurrentDeviceShadowmapLightHistory();

		
		gfxCameraRestoreSettings(&saved_camera, gfxGetActiveCameraView());
		gfx_state.frame_time = saved_time;
		SAFE_FREE(bone_infos);

		gfxOncePerFrameEnd(false);

		if (gfx_state.debug.material_preload_debug)
			FolderCacheDoCallbacks(); // Allow shader reloads for debugging

		if (!in_time_misc)
			wlPerfEndMiscBudget();
	}
}

static bool shouldPreloadMultipleLights(void)
{
	return gfx_state.debug.preload_multiple_lights || !gfx_state.uberlighting && !gfx_state.vertexOnlyLighting && !gfx_state.cclighting;
}

typedef struct {
	const char *intrinsic_defines;
	const char *platform;
	const char *shader_model;
	RdrMaterialShader extra_shader_flags;
	int shader_model_int;
	enum {
		Platform_SM30Plus = 1 << 0,
		Platform_ManualDepthTest = 1 << 1,
		Platform_VertexOnlyLighting = 1 << 2,
	} flags;
} PlatformDefines;

static PlatformDefines d3d9_platforms[] = {
	// PLATFORM DEFINES WARNING: These must match the order and contents exactly of what's defined in rxbxGetIntrinsicDefines()
	// NVIDIA
	{"SM30 VFETCH DEPTH_TEXTURE", "NVIDIA", "ps_3_0", 0, 0x30, Platform_SM30Plus},
	{"SM30 VFETCH DEPTH_TEXTURE RAWZ_DEPTH", "NVIDIA", "ps_3_0", 0, 0x30, Platform_SM30Plus|Platform_VertexOnlyLighting}, // 7000 series // 95% use vertexOnlyLighting

	// Intel
	{"SM30 VFETCH", "INTEL", "ps_3_0", 0, 0x30, Platform_SM30Plus|Platform_VertexOnlyLighting}, // 4 Series and G965
	{"SM30 VFETCH DEPTH_TEXTURE", "INTEL", "ps_3_0", 0, 0x30, Platform_SM30Plus}, // Intel HD w/ depth texture

	// ATI
	{"SM30 VFETCH DEPTH_TEXTURE", "ATI", "ps_3_0", 0, 0x30, Platform_SM30Plus}, // HD 3xxx and 4xxx
	{"SM30 DEPTH_TEXTURE", "ATI", "ps_3_0", 0, 0x30, Platform_ManualDepthTest|Platform_VertexOnlyLighting}, // X1xxx
	// Not flagging "simple lighting" because we instead use VertexOnlyLighting, which pre-empts it
	{"SM2B DEPTH_TEXTURE", "ATI", "ps_2_b", /*MATERIAL_SHADER_SIMPLE_LIGHTING*/0, 0x2B, Platform_ManualDepthTest|Platform_VertexOnlyLighting}, // Xxxx
	{"SM20 DEPTH_TEXTURE", "ATI", "ps_2_0", /*MATERIAL_SHADER_SIMPLE_LIGHTING*/0, 0x20, Platform_VertexOnlyLighting}, // 9800

	// Transgaming
	{"SM30 VFETCH DEPTH_TEXTURE TRANSGAMING", "NVIDIA", "ps_3_0", 0, 0x30, Platform_SM30Plus},
	{"SM30 VFETCH DEPTH_TEXTURE RAWZ_DEPTH TRANSGAMING", "NVIDIA", "ps_3_0", 0, 0x30, Platform_SM30Plus|Platform_VertexOnlyLighting}, // 7000 series // 95% use vertexOnlyLighting
	{"SM30 VFETCH TRANSGAMING", "INTEL", "ps_3_0", 0, 0x30, Platform_SM30Plus|Platform_VertexOnlyLighting}, // 4 Series and G965
	{"SM30 VFETCH DEPTH_TEXTURE TRANSGAMING", "INTEL", "ps_3_0", 0, 0x30, Platform_SM30Plus}, // Intel HD w/ depth texture
	{"SM30 VFETCH DEPTH_TEXTURE TRANSGAMING", "ATI", "ps_3_0", 0, 0x30, Platform_SM30Plus}, // HD 3xxx and 4xxx
	{"SM30 DEPTH_TEXTURE TRANSGAMING", "ATI", "ps_3_0", 0, 0x30, Platform_ManualDepthTest|Platform_VertexOnlyLighting}, // X1xxx
	{"SM2B DEPTH_TEXTURE TRANSGAMING", "ATI", "ps_2_b", /*MATERIAL_SHADER_SIMPLE_LIGHTING*/0, 0x2B, Platform_ManualDepthTest|Platform_VertexOnlyLighting}, // Xxxx
	{"SM20 DEPTH_TEXTURE TRANSGAMING", "ATI", "ps_2_0", /*MATERIAL_SHADER_SIMPLE_LIGHTING*/0, 0x20, Platform_VertexOnlyLighting}, // 9800

	// PLATFORM DEFINES WARNING: These must match the order and contents exactly of what's defined in rxbxGetIntrinsicDefines()


};

static PlatformDefines d3d11_platforms[] = {
	// PLATFORM DEFINES WARNING: These must match the order and contents exactly of what's defined in rxbxGetIntrinsicDefines()
	// NVIDIA
	{"SM30 VFETCH DEPTH_TEXTURE D3D11", "NVIDIA", "ps_4_0", 0, 0x30, Platform_SM30Plus},

	// Intel
	{"SM30 VFETCH DEPTH_TEXTURE D3D11", "INTEL", "ps_4_0", 0, 0x30, Platform_SM30Plus|Platform_VertexOnlyLighting}, // 4 Series and G965

	// ATI
	{"SM30 VFETCH DEPTH_TEXTURE D3D11", "ATI", "ps_4_0", 0, 0x30, Platform_SM30Plus}, // HD 3xxx and 4xxx
};

__forceinline void initializeD3DPlatform(PlatformDefines* platformDefs, const char* global_defines, int platformDefSize, int isD3D11)
{
	int i;

	// Add per-project global defines
	for (i=0; i<platformDefSize; i++)
	{
		char newbuf[1024];
		strcpy(newbuf, platformDefs[i].intrinsic_defines);
		strcat(newbuf, " ");
		strcat(newbuf, global_defines);
		strcat(newbuf, platformDefs[i].platform);
		platformDefs[i].intrinsic_defines = strdup(newbuf);
		if (isD3D11)
			platformDefs[i].extra_shader_flags.shaderMask |= MATERIAL_SHADER_D3D11;
	}
}

__forceinline bool findPlatformDef(PlatformDefines* platformDefs, const char *platformDef, int platformDefSize)
{
	int i;

	for (i=0; i<platformDefSize; i++)
	{
		if (stricmp(platformDef, platformDefs[i].intrinsic_defines)==0)
			return true;
	}
	return false;
}

static void initializeAllPlatformDefines(void)
{
	int i;
	static bool doneOnce=false;
	const char *my_defines = rdrGetIntrinsicDefines(gfx_state.currentDevice->rdr_device);
	char global_defines[128];
	global_defines[0] = 0;
	if (doneOnce)
		return;
	doneOnce = true;

	for (i=0; i<rdrShaderGetGlobalDefineCount(); i++)
	{
		const char *def = rdrShaderGetGlobalDefine(i);
		if (def && def[0] && stricmp(def, "0")!=0)
		{
			ANALYSIS_ASSUME(def);
			strcat(global_defines, def);
			strcat(global_defines, " ");
		}
	}
	initializeD3DPlatform(d3d9_platforms, global_defines,ARRAY_SIZE(d3d9_platforms),0);
	initializeD3DPlatform(d3d11_platforms, global_defines,ARRAY_SIZE(d3d11_platforms),1);
	// Check for sanity
	if (!(findPlatformDef(d3d9_platforms,my_defines,ARRAY_SIZE(d3d9_platforms)) || findPlatformDef(d3d11_platforms,my_defines,ARRAY_SIZE(d3d11_platforms))))
	{
		assertmsgf(0, "Intrinsic defines do not match pre-compile defines.  Defines have been reordered, not updated, or this is being run on a non-NV SM30 card, or something else horrible has gone wrong.  Intrinsic defines:\n%s\nFirst pre-compile defines:\n%s", my_defines, d3d9_platforms[0].intrinsic_defines);
	}
}

static void gfxCreateShader(RdrMaterialShader shader_num, ShaderGraphRenderInfo *graph_render_info, ShaderGraphReflectionType reflection_type, S64 extraFlags,
		const char *intrinsic_defines, const char *shader_model, const MaterialAssemblerProfile *mat_profile, int *preloading_count)
{
	RdrMaterialShader matShader = getRdrMaterialShaderByKey((shader_num).key|extraFlags);
	gfxCacheShaderGraphForOtherTarget(graph_render_info, reflection_type,
		matShader, intrinsic_defines, shader_model, mat_profile);
	(*preloading_count)++;
	if (graph_render_info->shader_graph->graph_flags & SGRAPH_ALPHA_TO_COVERAGE)
	{
		matShader.shaderMask |= MATERIAL_SHADER_COVERAGE_DISABLE;
		gfxCacheShaderGraphForOtherTarget(graph_render_info, reflection_type,
			matShader, intrinsic_defines, shader_model, mat_profile);
		(*preloading_count)++;
	}
}

#define DOIT(shader_num) gfxCreateShader(shader_num, graph_render_info, reflection_type, platformDefs[i].extra_shader_flags.key, platformDefs[i].intrinsic_defines, platformDefs[i].shader_model, mat_profile, preloading_count)

static void gfxShaderGraphCompileForAllPlatforms(ShaderGraph *graph, int *preloading_count, bool alpha_unlit_only)
{
	int i;
	ShaderGraphReflectionType reflection_type;
	ShaderGraphRenderInfo *graph_render_info = graph->graph_render_info;

	initializeAllPlatformDefines();

	if (rdr_state.compile_all_shader_types & CompileShaderType_PC)
	{
		int j;

		gfxLoadingSetLoadingMessage("Compiling shaders");
		for (j = 0; j < 2; j++) {
			const MaterialAssemblerProfile *mat_profile;
			PlatformDefines *platformDefs;
			int platformSize;
			if (j == 0) {
				mat_profile = getMaterialProfileForDevice("D3D9");
				platformDefs = d3d9_platforms;
				platformSize = ARRAY_SIZE(d3d9_platforms);
			} else {
				// Trash shader text if it exists because it will be set according to D3D9 otherwise!
				estrDestroy(&graph_render_info->shader_text);
				mat_profile = getMaterialProfileForDevice("D3D11");
				platformDefs = d3d11_platforms;
				platformSize = ARRAY_SIZE(d3d11_platforms);
			}

			for (i=0; i<platformSize; i++)
			{
				if (platformDefs[i].shader_model_int < 0x20 && (graph_render_info->shader_graph->graph_features & SGFEAT_SM20))
					continue;
				if (platformDefs[i].shader_model_int < 0x2A && (graph_render_info->shader_graph->graph_features & SGFEAT_SM20_PLUS))
					continue;
				if (platformDefs[i].shader_model_int < 0x30 && (graph_render_info->shader_graph->graph_features & SGFEAT_SM30))
					continue;
				if (!(platformDefs[i].flags & Platform_SM30Plus) && (graph_render_info->shader_graph->graph_features & SGFEAT_SM30_PLUS))
					continue;

				for (reflection_type=0; reflection_type <= graph_render_info->shader_graph->graph_reflection_type; reflection_type++)
				{

					if (platformDefs[i].shader_model_int < 0x30 && reflection_type == SGRAPH_REFLECT_CUBEMAP)
						continue;

					if (alpha_unlit_only)
					{
						DOIT(getRdrMaterialShader(MATERIAL_RENDERMODE_HAS_ALPHA,0));
						continue;
					}

			
					// depth-only shaders
					if (!(graph->graph_flags & SGRAPH_ALPHA_PASS_ONLY) || graph_render_info->need_texture_screen_depth)
					{
						// Alpha-only shaders need not be compiled for zprepass, shadows, etc
						// Unless they're refraction shaders, which draw depth with a second pass
						DOIT(getRdrMaterialShader(MATERIAL_RENDERMODE_DEPTH_ONLY,0));
					}

					if (platformDefs[i].flags & Platform_VertexOnlyLighting)
					{
						// Needs only the vertex-only lighting shaders
						if (platformDefs[i].flags & Platform_ManualDepthTest)
						{
							DOIT(getRdrMaterialShader(MATERIAL_RENDERMODE_HAS_ALPHA | MATERIAL_SHADER_MANUAL_DEPTH_TEST | MATERIAL_SHADER_VERTEX_ONLY_LIGHTING,0));
						}
						DOIT(getRdrMaterialShader(MATERIAL_RENDERMODE_HAS_ALPHA | MATERIAL_SHADER_VERTEX_ONLY_LIGHTING, 0));
						// Also need the single-directional light shaders for higherSettingsInTailor option
						DOIT(getRdrMaterialShader(MATERIAL_RENDERMODE_HAS_ALPHA, rdrGetMaterialShaderType(RDRLIGHT_DIRECTIONAL, 0)));
						if (!(graph->graph_flags & SGRAPH_ALPHA_PASS_ONLY))
						{
							if (platformDefs[i].shader_model_int <= 0x20)
							{
								// No z-prepass on these cards (just ATI 9800 line) because they also don't support 24-bit depth
							} else {
								DOIT(getRdrMaterialShader(MATERIAL_RENDERMODE_OPAQUE_AFTER_ZPRE | MATERIAL_SHADER_VERTEX_ONLY_LIGHTING,0));
								// Also need the single-directional light shaders for higherSettingsInTailor option
								DOIT(getRdrMaterialShader(MATERIAL_RENDERMODE_OPAQUE_AFTER_ZPRE, rdrGetMaterialShaderType(RDRLIGHT_DIRECTIONAL, 0)));
							}
						}
					}
					else if (!(platformDefs[i].flags & Platform_SM30Plus))
					{
						// Uses unique lighting shaders
						// No shadows by default, not supported on almost all of these cards (all of them?)
						int light0, light1;
						for (light0=0; light0<RDRLIGHT_TYPE_MAX; light0++)
						{
							for (light1=0; light1<RDRLIGHT_TYPE_MAX; light1++) 
							{
								RdrMaterialShader lightpart = {0};

								lightpart.lightMask = rdrGetMaterialShaderType(light0, 0) |
									rdrGetMaterialShaderType(light1, 1);
								// Skip invalid light types
								if (light0 == 0 && light1)
									continue;
								if (light0 && light1 && light1 < light0)
									continue;

								if (platformDefs[i].flags & Platform_ManualDepthTest)
								{
									DOIT(getRdrMaterialShader(MATERIAL_RENDERMODE_HAS_ALPHA | MATERIAL_SHADER_MANUAL_DEPTH_TEST, lightpart.lightMask));
								}
								DOIT(getRdrMaterialShader(MATERIAL_RENDERMODE_HAS_ALPHA, lightpart.lightMask));
								if (!(graph->graph_flags & SGRAPH_ALPHA_PASS_ONLY))
								{
									if (platformDefs[i].shader_model_int <= 0x20)
									{
										// No z-prepass on these cards (just ATI 9800 line) because they also don't support 24-bit depth
									} else {
										DOIT(getRdrMaterialShader(MATERIAL_RENDERMODE_OPAQUE_AFTER_ZPRE, lightpart.lightMask));
									}
								}
							}
						}
					} else {
						// Uses CCLighting
						// Preload the light combinations from LightCombos.txt
						// Oh, so much simpler!
						FOR_EACH_IN_EARRAY(preloaded_light_combos, PreloadedLightCombo, light_combo)
						{
							DOIT(getRdrMaterialShader(MATERIAL_RENDERMODE_HAS_ALPHA, light_combo->light_mask));
							DOIT(getRdrMaterialShader(MATERIAL_RENDERMODE_HAS_ALPHA | MATERIAL_SHADER_SHADOW_BUFFER, light_combo->light_mask));
							if (!(light_combo->light_mask & rdrGetMaterialShaderType(RDRLIGHT_SHADOWED, 0)))
							{
								// Shadowed lights are either rendered with shadow_buffer (and not flagged as a shadowed light) or alpha
								if (!(graph->graph_flags & SGRAPH_ALPHA_PASS_ONLY))
								{
									DOIT(getRdrMaterialShader(MATERIAL_RENDERMODE_OPAQUE_AFTER_ZPRE, light_combo->light_mask));
									DOIT(getRdrMaterialShader(MATERIAL_RENDERMODE_OPAQUE_AFTER_ZPRE | MATERIAL_SHADER_SHADOW_BUFFER, light_combo->light_mask));
								}
							}
						}
						FOR_EACH_END;
					}
	#undef DOIT
				}
			}
		}
	}
}

StashTable stInvalidatedFiles;
bool bPrintedOnce=false;
static void cacheInvalidatedCallback(const char *filename)
{
	if (!stInvalidatedFiles)
		stInvalidatedFiles = stashTableCreateWithStringKeys(16, StashDefault);
	if (stashAddPointer(stInvalidatedFiles, filename, filename, false))
	{
		if (!bPrintedOnce)
		{
			printf("\n    Global shader cache invalidated because of changes to source files.  Shader build time will be long (rebuilding all shaders)\n");
			bPrintedOnce = true;
		}
		printf("      Changed source file: %s\n", filename);
	}
}

static void finishInvalidatingFiles(void)
{
	if (stInvalidatedFiles)
	{
		stashTableDestroy(stInvalidatedFiles);
		stInvalidatedFiles = NULL;
	}
}

void gfxMaterialPreloadTemplates(void)
{
	int preloading_count = 0;
	int countdown;
	int num_loops_per_refresh=20;
	int actual_preloading_template_count=0;
	int skipped_count=0;
	int alpha_only_count=0;
	int timer;
	int orig_bloom_quality=gfx_state.settings.bloomQuality;
	int orig_max_inactive_fps=gfx_state.settings.maxInactiveFps;
	static bool bFirstTime=true;

	if (!eaSize(&templates_to_preload))
		return;

	rdr_state.nvidiaLowerOptimization = rdr_state.nvidiaLowerOptimization_default;
	rdr_state.echoShaderPreloadLog = 0;
	gfx_state.settings.maxInactiveFps = 0;

	timer = timerAlloc();

	gfxLoadingSetLoadingMessage(isAprilFools()?"Preparing Virgin Shaders for Sacrifice":"Compiling shaders");
	gfxLoadingStartWaiting();
	loadstart_printf("Starting shader pre-loading...");
	loadstart_printf("Queuing shader loads...");

	preloadBeginEndFrame(1, NULL, false);

	dynamicCacheSetGlobalDepsInvalidatedCallback(gfx_state.shaderCache, cacheInvalidatedCallback);

	//Pre-load rarely used sprite shaders
	{
		int i;
		for (i=0; i<RdrSpriteEffect_MAX; i++)
		{
			if (i == RdrSpriteEffect_DistField1Layer)
				display_sprite_distance_field_one_layer(white_tex, 0, 0, 0, 1, 1, 0, 0, 0, 0, 0, 0, 1, 1, 0, 0, 0, 
					clipperGetCurrent(), 0, 0, 0, 0, 0, 0, 0, 0, NULL);
			else if (i == RdrSpriteEffect_DistField1LayerGradient)
				display_sprite_distance_field_one_layer_gradient(white_tex, 0, 0, 0, 1, 1, 0, 0, 0, 0, 0, 0, 1, 1, 0, 0, 0, 
				clipperGetCurrent(), 0, 0, 0, 0, 0, 0, 0, 0, NULL);
			else if (i == RdrSpriteEffect_DistField2Layer)
				display_sprite_distance_field_two_layers(white_tex, 0, 0, 0, 1, 1, 0, 0, 0, 0, 0, 0, 1, 1, 0, 0, 0, 
					clipperGetCurrent(), 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, NULL);
			else if (i == RdrSpriteEffect_DistField2LayerGradient)
				display_sprite_distance_field_two_layers_gradient(white_tex, 0, 0, 0, 1, 1, 0, 0, 0, 0, 0, 0, 1, 1, 0, 0, 0, 
				clipperGetCurrent(), 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, NULL);
			else
				display_sprite_effect_tex(white_tex, 0, 0, 0, 1, 1, 0x00000000, 1, i, 0.5);
		}
	}

	countdown = num_loops_per_refresh;
	FOR_EACH_IN_EARRAY_FORWARDS(templates_to_preload, ShaderTemplate, templ)
	{
		// Cards without 24-bit depth use "alpha" shaders everywhere
		bool bDoOpaqueShaders = gfx_state.currentAction->gdraw.do_zprepass && rdrSupportsFeature(gfx_state.currentDevice->rdr_device, FEATURE_24BIT_DEPTH_TEXTURE);
		if (!rdrShaderPreloadSkip(templ->filename))
			actual_preloading_template_count++;

		if (rdrShaderPreloadSkipEvenCompiling(templ->filename))
			continue;

		if (rdrShaderPreloadSkip(templ->filename))
			skipped_count++;

		gfxLockActiveDevice();

		if (rdr_state.compile_all_shader_types)
		{
			gfxShaderGraphCompileForAllPlatforms(templ->graph, &preloading_count, false);
		}
		else // Don't preload our own specific configuration as well because it might use the same cache names as the custom code in *ForAllPlatforms, causing duplicate compiles and cache contention
		{
			RdrMaterialShader shader_num_opaque;
			RdrMaterialShader shader_num_alpha;
			RdrMaterialShader depth_shader_num;

			shader_num_opaque.lightMask = 0;
			shader_num_opaque.shaderMask = MATERIAL_RENDERMODE_OPAQUE_AFTER_ZPRE;
			shader_num_alpha.lightMask = 0;
			shader_num_alpha.shaderMask = MATERIAL_RENDERMODE_HAS_ALPHA;
			depth_shader_num.lightMask = 0;
			depth_shader_num.shaderMask = MATERIAL_RENDERMODE_DEPTH_ONLY;

			assert(templ->graph->graph_render_info);
			templ->graph->graph_render_info->has_been_preloaded = 1;

			if (gfx_state.currentAction->gdraw.do_shadow_buffer)
			{
				shader_num_opaque.shaderMask |= MATERIAL_SHADER_SHADOW_BUFFER;
				shader_num_alpha.shaderMask |= MATERIAL_SHADER_SHADOW_BUFFER;
			}

			if (gfx_state.uberlighting)
			{
				// Preloading for uberlighting mostly does not work anymore, code was simplified for CCLighting
				shader_num_opaque.shaderMask |= MATERIAL_SHADER_UBERLIGHT;
				shader_num_alpha.shaderMask |= MATERIAL_SHADER_UBERLIGHT;
			}
			if (!rdrSupportsFeature(gfx_state.currentDevice->rdr_device, FEATURE_SM30) || gfx_state.debug.simpleLighting)
			{
				shader_num_opaque.shaderMask |= MATERIAL_SHADER_SIMPLE_LIGHTING;
				shader_num_alpha.shaderMask |= MATERIAL_SHADER_SIMPLE_LIGHTING;
			}

			#define DOIT(shader_num) 								\
				{													\
					gfxMaterialFillShader(templ->graph->graph_render_info, shader_num, 1); \
					if (!rdrShaderPreloadSkip(templ->filename))		\
						preloading_count++;							\
				}

			if (templ->graph->graph_flags & SGRAPH_ALPHA_PASS_ONLY)
			{
				// Alpha-only shaders need not be compiled for zprepass, shadows, etc
				alpha_only_count++;
				// Unless they're refraction shaders, which draw depth with a second pass
				if (templ->graph->graph_render_info->need_texture_screen_depth)
				{
					DOIT(depth_shader_num);
				}
			} else {
				if (!gfx_state.disable_zprepass) {
					DOIT(depth_shader_num);
				}
			}

			if (gfx_state.uberlighting)
			{
				// Not generally supported anymore
				DOIT(shader_num_alpha);
				if (bDoOpaqueShaders && !(templ->graph->graph_flags & SGRAPH_ALPHA_PASS_ONLY))
					DOIT(shader_num_opaque);
			}
			if (gfx_state.vertexOnlyLighting)
			{
				// Preload vertex-only lighting versions here
				DOIT(getRdrMaterialShader(shader_num_opaque.shaderMask | MATERIAL_SHADER_VERTEX_ONLY_LIGHTING, shader_num_opaque.lightMask));
				DOIT(getRdrMaterialShader(shader_num_alpha.shaderMask | MATERIAL_SHADER_VERTEX_ONLY_LIGHTING,shader_num_alpha.lightMask));
				// Also single-directional light versions if appropriate
				if (gfx_state.settings.higherSettingsInTailor)
				{
					DOIT(getRdrMaterialShader(shader_num_opaque.shaderMask, shader_num_opaque.lightMask | rdrGetMaterialShaderType(RDRLIGHT_DIRECTIONAL, 0)));
					DOIT(getRdrMaterialShader(shader_num_alpha.shaderMask, shader_num_alpha.lightMask | rdrGetMaterialShaderType(RDRLIGHT_DIRECTIONAL, 0)));
				}
			}
			if (shouldPreloadMultipleLights()) // regular unique shaders, non-CCLighting, non-uberlighting
			{
				int j, k;
				int sj;
				// Do all 2-light variations, first light optionally shadowed (except maybe none of these cards that get here can do shadows anyway)
				for (j=0; j<RDRLIGHT_TYPE_MAX; j++)
				{
					for (sj=0; sj<((j==0)?1:2); sj++)
					{
						RdrLightType light0 = j | (sj?RDRLIGHT_SHADOWED:0);
						if (sj && !gfxFeatureEnabled(GFEATURE_SHADOWS))
							continue;
						for (k=0; k<RDRLIGHT_TYPE_MAX; k++) 
						{
							RdrLightType light1 = k;
							if (k>0 && k<j && !sj)
								continue;
							if (k>0 && j==0)
								continue;
							{
								RdrMaterialShader lightpart;
								RdrMaterialShader lightshader_alpha;
								RdrMaterialShader lightshader_opaque;

								lightpart.shaderMask = 0;
								lightpart.lightMask = rdrGetMaterialShaderType(light0, 0) |
									rdrGetMaterialShaderType(light1, 1);
								lightshader_alpha.key = shader_num_alpha.key | lightpart.key;
								lightshader_opaque.key = shader_num_opaque.key | lightpart.key;

								if (rdr_state.lowResAlphaHighResNeedsManualDepthTest)
								{
									DOIT(getRdrMaterialShader(lightshader_alpha.shaderMask | MATERIAL_SHADER_MANUAL_DEPTH_TEST, lightshader_alpha.lightMask));
								}
								DOIT(lightshader_alpha);
								if (bDoOpaqueShaders && !(templ->graph->graph_flags & SGRAPH_ALPHA_PASS_ONLY))
									DOIT(lightshader_opaque);
							}
						}
					}
				}
			}
			if (gfx_state.cclighting && !gfx_state.vertexOnlyLighting)
			{
				// Preload the light combinations from LightCombos.txt
				// Oh, so much simpler!
				FOR_EACH_IN_EARRAY(preloaded_light_combos, PreloadedLightCombo, light_combo)
				{
					if (gfx_state.settings.maxLightsPerObject <=2 && rdrGetLightType(light_combo->light_mask, 2))
						continue; // 3+ light shader on max lights per object 2 settings
					if (!gfxFeatureEnabled(GFEATURE_SHADOWS) && rdrIsShadowedLightType(rdrGetLightType(light_combo->light_mask, 0)))
						continue;
					DOIT(getRdrMaterialShader(shader_num_alpha.shaderMask, shader_num_alpha.lightMask | light_combo->light_mask));
					// Skip opaque shader if it is shadowed, as the shadowed opaque shaders with shadow buffer use the "non-shadowed" shaders
					if (bDoOpaqueShaders && !(templ->graph->graph_flags & SGRAPH_ALPHA_PASS_ONLY) && !rdrIsShadowedLightType(rdrGetLightType(light_combo->light_mask, 0)))
						DOIT(getRdrMaterialShader(shader_num_opaque.shaderMask, shader_num_opaque.lightMask | light_combo->light_mask));
				}
				FOR_EACH_END;
			}

			#undef DOIT
		}

		gfxUnlockActiveDevice();

		preload_count_remaining = (eaSize(&templates_to_preload) - itemplIndex) * (rdr_state.compile_all_shader_types?28:8);
		countdown--;
		if (countdown<=0 || timerElapsed(timer) > 0.2) {
			utilitiesLibOncePerFrame(0, 1); // Needed for TimedCallbacks to get ran
			preloadBeginEndFrame(0, NULL, false);

            gfxSyncActiveDevice();

			preloadBeginEndFrame(1, NULL, false);
			countdown = num_loops_per_refresh;
			timerStart(timer);
		}
		while (rdrShaderGetBackgroundShaderCompileCount() > 2000)
		{
			utilitiesLibOncePerFrame(0, 1); // Needed for TimedCallbacks to get ran
			preloadBeginEndFrame(0, NULL, false);
			preloadBeginEndFrame(1, NULL, false);
			Sleep(60);
		}

        gfxMaterialTemplateUnloadShaderText(templ);
	}
	FOR_EACH_END;

	gfx_state.settings.bloomQuality = orig_bloom_quality;
	preload_count_remaining = 0;

	// Special cases
	if (bFirstTime)
	{
		int i;
		// TODO: make data-driven
		static const char *special_alphas[] = {
			"FxParticle",
			"WaterVolume",
			"WaterNearSurface",
		};
		// Some shaders which are often used, but may not be drawn in our preload frame.
		// Just load them, doesn't actually get warmed
		for (i=0; i<ARRAY_SIZE(special_alphas); i++) 
		{
			ShaderTemplate *shader_template = materialGetTemplateByName(special_alphas[i]);
			if (shader_template) {
				gfxMaterialFillShader(shader_template->graph->graph_render_info, getRdrMaterialShader(MATERIAL_RENDERMODE_HAS_ALPHA,0), 1);
				preloading_count++;
				
                if (rdr_state.compile_all_shader_types)
					gfxShaderGraphCompileForAllPlatforms(shader_template->graph, &preloading_count, true);
			}
		}

		gfxPostprocessDoPreloadNextFrame();
	}

	// Wait for things to finish the foreground-thread compiles and sent to the
	//  renderer so we don't ping-pong the disk.
	{
		int frame_count=0;
		timerStart(timer);
		do {
			gfxLockActiveDevice();
			gfxDemandLoadMaterials(false);
			gfxUnlockActiveDevice();
			//fileLoaderCheck();
			//dynamicCacheCheckAll(0);
			utilitiesLibOncePerFrame(0, 1); // Needed for TimedCallbacks to get ran
			Sleep(1);
			frame_count++;
			preloadBeginEndFrame(0, NULL, false);
			// Need to flush the render thread so it actually compiles all the postprocessing shaders sent to it
			timerPause(timer);
			rdrFlush(gfx_state.currentDevice->rdr_device, false);
			timerUnpause(timer);
			preloadBeginEndFrame(1, NULL, false);
			utilitiesLibOncePerFrame(0, 1); // Needed for TimedCallbacks to get ran, must do after rdrFlush
			if (utilitiesLibLoadsPending() || rdrShaderGetBackgroundShaderCompileCount())
			{
				timerStart(timer);
				frame_count = 0;
			}
			// 100ms + 5 frames leeway in case things need a run-through or two here to clear out
		} while(timerElapsed(timer) < 0.1 || frame_count < 5);
	}

	loadend_printf("done.");

	if(rdr_state.compile_all_shader_types)
	{
		char src[MAX_PATH];
		char dst[MAX_PATH];
		char srcXbox[MAX_PATH];
		char dstXbox[MAX_PATH];

		DynamicCache *destCache;
		static bool bDoneOnce=false;
		bool bNeedToUpdate;

		assert(!bDoneOnce);
		bDoneOnce = true;

		assert(gfx_state.compile_all_shader_types_timestamp);

		// Before pruning, we need to extract any redirect files, as they might redirect to something
		//  about to be pruned!
		loadstart_printf("Expanding shader redirects...");
		dynamicCacheExpandRedirects(gfx_state.shaderCache);
		if (gfx_state.shaderCacheXboxOnPC)
			dynamicCacheExpandRedirects(gfx_state.shaderCacheXboxOnPC);
		loadend_printf(" done.");

		loadstart_printf("Pruning cache and merging into platformbin...");
		// Prune files which were not touched just now, they must be old
		dynamicCachePruneOldFiles(gfx_state.shaderCache, gfx_state.compile_all_shader_types_timestamp);
		if (gfx_state.shaderCacheXboxOnPC)
			dynamicCachePruneOldFiles(gfx_state.shaderCacheXboxOnPC, gfx_state.compile_all_shader_types_timestamp);

		// Update the shader caches in the bin folder
		// Instead of copying, do a merge, so that timestamps are left untouched
		sprintf(src, "%s/shaderCache.hogg", fileCacheDir());
		sprintf(dst, "%s/platformbin/PC/shaderbin/shaderCache.hogg", fileDataDir());
		sprintf(srcXbox, "%s/shaderCacheXbox.hogg", fileCacheDir());
		sprintf(dstXbox, "%s/platformbin/Xbox/shaderbin/shaderCache.hogg", fileDataDir());

		if (rdr_state.compile_all_shader_types & CompileShaderType_PC)
		{
			destCache = dynamicCacheCreate(dst, dynamicCacheGetVersion(gfx_state.shaderCache), 100*1024*1024, 100*1024*1024, 3600*24*365, DYNAMIC_CACHE_NO_TIMESTAMPS);
			bNeedToUpdate = (dynamicCacheMergePrecise(destCache, src, true) > 0) || !dynamicCacheHasGlobalDeps(destCache);
			dynamicCacheDestroy(destCache);
			loadend_printf(" done.");
			if (bNeedToUpdate)
			{
				loadstart_printf("Consistifying cache...");
				dynamicCacheConsistify(dst);
				loadend_printf(" done.");
			}
		}

		if (rdr_state.compile_all_shader_types & CompileShaderType_Xbox)
		{
			loadstart_printf("Merging Xbox cache into platformbin...");
			destCache = dynamicCacheCreate(dstXbox, dynamicCacheGetVersion(gfx_state.shaderCacheXboxOnPC), 100*1024*1024, 100*1024*1024, 3600*24*365, DYNAMIC_CACHE_NO_TIMESTAMPS);
			bNeedToUpdate = (dynamicCacheMergePrecise(destCache, srcXbox, true) > 0) || !dynamicCacheHasGlobalDeps(destCache);
			dynamicCacheDestroy(destCache);
			loadend_printf(" done.");
			if (bNeedToUpdate)
			{
				loadstart_printf("Consistifying Xbox cache...");
				dynamicCacheConsistify(dstXbox);
				loadend_printf(" done.");
			}
		}
	}

	preloadBeginEndFrame(0, NULL, false);
	gfxLoadingSetLoadingMessage(NULL);
	preloadBeginEndFrame(1, NULL, false);
	preloadBeginEndFrame(0, NULL, false);

	// At this point, all of the foreground thread work has been done, and all
	//  shaders are compiled (although possibly not submitted to D3D if it was
	//  background-compiled)
	// We will finish in gfxMaterialPreloadFinishPreloading()

	timerFree(timer);

	loadend_printf(" done (%d full, %d alpha, %d sys, %d shaders).", actual_preloading_template_count-alpha_only_count, alpha_only_count, skipped_count, preloading_count);
	gfxLoadingFinishWaiting();

#ifdef _FULLDEBUG
	rdr_state.echoShaderPreloadLog = 1;
	rdr_state.echoShaderPreloadLogIgnoreVirgins = 1;
#endif

	gfxMaterialPreloadFinishPreloading(); // Should this happen later?

	dynamicCacheSetGlobalDepsInvalidatedCallback(gfx_state.shaderCache, NULL);
	finishInvalidatingFiles();

	gfx_state.settings.maxInactiveFps = orig_max_inactive_fps;
	bFirstTime = false;
}

// 4 light types, shadowed and non
LightData preload_lights[8];

static void initPreloadLights(void)
{
	static bool bInited=false;
	int i;
	if (bInited)
		return;
	bInited = true;

	for (i=0; i<ARRAY_SIZE(preload_lights); i++) {
		preload_lights[i].light_type = (i % 4) + 1;
		preload_lights[i].light_affect_type = WL_LIGHTAFFECT_ALL;
		copyMat4(unitmat, preload_lights[i].world_mat);
		setVec3(preload_lights[i].world_mat[3], -4, 8, 2);
		setVec3(preload_lights[i].ambient_hsv, 0.01, 0.01, 0.01);
		setVec3(preload_lights[i].diffuse_hsv, 0, 0.4, 1);
		setVec3(preload_lights[i].specular_hsv, 0.4, 0.9, 0.4);
		setVec3(preload_lights[i].secondary_diffuse_hsv, 0.4, 0.4, 0.7);
		setVec3(preload_lights[i].shadow_color_hsv, 0, 0, 0);
		preload_lights[i].inner_radius = 20;
		preload_lights[i].outer_radius = 100;
		preload_lights[i].inner_cone_angle = 45;
		preload_lights[i].outer_cone_angle = 46;
		preload_lights[i].inner_cone_angle2 = 45;
		preload_lights[i].outer_cone_angle2 = 90;
		preload_lights[i].min_shadow_val = 0.1;
		preload_lights[i].texture_name = "default";
		//preload_lights[i].controller_name;
		//preload_lights[i].region;
		//preload_lights[i].tracker;
		preload_lights[i].cast_shadows = i >= 4;
		if (preload_lights[i].light_type==RDRLIGHT_DIRECTIONAL)
		{
			preload_lights[i].dynamic = 0;
			preload_lights[i].is_sun = 1;
		} else {
			if (gfx_state.debug.material_preload_debug)
				preload_lights[i].dynamic = 1; // Slow, but actually gets it drawn
			else 
				preload_lights[i].dynamic = 0;
			preload_lights[i].is_sun = 0;
		}
		if (preload_lights[i].light_type == RDRLIGHT_SPOT)
		{
			setVec3(preload_lights[i].world_mat[3], 0, 4, 1.5);
			setVec3(preload_lights[i].diffuse_hsv, 0, 2, 2);
		}
		preload_lights[i].key_light = 1;
	}
}

static int preloadLightForType(RdrLightType light_type)
{
	int i;
	for (i=0; i<ARRAY_SIZE(preload_lights); i++)
	{
		if (preload_lights[i].light_type == rdrGetSimpleLightType(light_type) && preload_lights[i].cast_shadows == !!(light_type & RDRLIGHT_SHADOWED))
			return i;
	}
	assert(0);
}

__forceinline static int rdrMaterialNeedsScreenGrab(SA_PARAM_NN_VALID const RdrMaterial *material)
{
	return material->need_texture_screen_color | material->need_texture_screen_depth | material->need_texture_screen_color_blurred;
}

__forceinline static int materialNeedsScreenGrab(SA_PARAM_NN_VALID const Material *material)
{
	return material->graphic_props.render_info && rdrMaterialNeedsScreenGrab(&material->graphic_props.render_info->rdr_material);
}

bool gfxMaterialNeedsScreenGrab(const Material *material)
{
	return materialNeedsScreenGrab(material);
}

static void finishPreload(int iStart, int iEnd, bool bSkinned)
{
	int i, j;
	SingleModelParams params = {0};
	BasicTexture **eaTextureSwaps=NULL;

	preloadBeginEndFrame(1, &params, bSkinned);

	if (gfx_state.debug.material_preload_debug)
	{
		// Draw dummy scene to test shadows
		int num_bones_saved;
		// ground/shadow receiver
		setVec3(params.world_mat[3], -50, -1, -50);
		params.model = preloadGetPlaneModel();
		num_bones_saved = params.num_bones;
		params.num_bones = 0;
		gfxQueueSingleModel(&params);
		// shadow caster
		setVec3(params.world_mat[0], 0, 1, 0);
		setVec3(params.world_mat[1], 1, 0, 0);
		setVec3(params.world_mat[2], 0, 0, 1);
		setVec3(params.world_mat[3], -1, 0, 0.5);
		gfxQueueSingleModel(&params);
		params.num_bones = num_bones_saved;

		setVec3(params.world_mat[0], 1, 0, 0);
		setVec3(params.world_mat[1], 0, 1, 0);
		setVec3(params.world_mat[2], 0, 0, 1);
		setVec3(params.world_mat[3], 0, 0, 0);
		params.model = preloadGetModel(bSkinned);
	}
	if (bSkinned)
		setVec3(params.world_mat[3], 0, -4, 0);

	draw_list_debug_state.overridePerMaterialFlags = 1;
	draw_list_debug_state.overrideNoAlphaKill = 1;
	draw_list_debug_state.overrideSortBucket = 1;
	draw_list_debug_state.overrideNoAlphaKillValue = 0;

/*	if (iStart==0) {
		// Extra stuff
		draw_list_debug_state.overrideSortBucketValue = RSBT_ALPHA;

		params.material_replace = materialFindNoDefault("FxParticle_default", WL_FOR_NOTSURE);
		assert(params.material_replace);
		params.unlit = true;
		gfxQueueSingleModel(gfxGetDefaultDrawList(), &params);
		params.unlit = false;
	}
*/

	for (i=iStart; i<iEnd; i++) {
		char material_name[256];
		bool bAlphaPassOnly;
		// Draw something with this template + variations
		if (strEndsWith(templates_to_preload[i]->template_name, "_OneOff")) {
			strcpy(material_name, templates_to_preload[i]->template_name);
			material_name[strlen(material_name) - 7] = '\0';
		} else {
			sprintf(material_name, "%s_default", templates_to_preload[i]->template_name);
		}
		if (rdrShaderPreloadSkip(templates_to_preload[i]->filename))
		{
			//printf("Skipping preload of material \"%s\"\n", material_name);
			preload_count_remaining--;
			continue;
		}

		bAlphaPassOnly = !!(templates_to_preload[i]->graph->graph_flags & SGRAPH_ALPHA_PASS_ONLY);
		if (gfx_state.debug.material_preload_debug && 0)
		{
			printf( "Preloading material %s %s\n", material_name, templates_to_preload[i]->filename );
		}

		params.material_replace = materialFindNoDefault(material_name, WL_FOR_NOTSURE);
		assert(params.material_replace);
		if (1) {
			// Swap textures to not actually load textures!
			if (!params.material_replace->graphic_props.render_info)
				gfxMaterialsInitMaterial(params.material_replace, true);
			assert(params.material_replace->graphic_props.render_info);
			eaSetSize(&eaTextureSwaps, 0);
			for (j=0; j<(int)params.material_replace->graphic_props.render_info->rdr_material.tex_count; j++) {
				BasicTexture * originalTex = params.material_replace->graphic_props.render_info->textures[j];
				eaPush(&eaTextureSwaps, originalTex);
				eaPush(&eaTextureSwaps, texIsVolume(originalTex) ? dummy_volume_tex : texIsCubemap(originalTex) ? dummy_cube_tex : white_tex);
			}
			params.eaTextureSwaps = eaTextureSwaps;
		}

		// if the material requires a depth or color screen grab, allow it to render
		// in the normal pass for it, instead of overriding
		draw_list_debug_state.overrideSortBucket = !materialNeedsScreenGrab(params.material_replace) && !bAlphaPassOnly;
		draw_list_debug_state.overrideNoAlphaKill = !bAlphaPassOnly;

		// Opaque with and without alpha kill
		// Need to run depth-only pass and alpha pass and opaque pass
		if (!bAlphaPassOnly)
		{
			draw_list_debug_state.overrideSortBucketValue = RSBT_OPAQUE_PRE_GRAB;
			draw_list_debug_state.overrideNoAlphaKillValue = 0;
			gfxQueueSingleModel(&params);
			draw_list_debug_state.overrideNoAlphaKillValue = 1;
			gfxQueueSingleModel(&params);
		}

		// Alpha
		draw_list_debug_state.overrideSortBucketValue = RSBT_ALPHA_PRE_DOF;
		draw_list_debug_state.overrideNoAlphaKill = 0;
		gfxQueueSingleModel(&params);

		// turn overriding back on
		draw_list_debug_state.overrideSortBucket = 1;

		preload_count_remaining--;
		// This assert is valid if everything is working correctly:
		//assert(!utilitiesLibLoadsPending()); // Otherwise we just queued up a new shader we didn't pre-load!
	}
	preloadBeginEndFrame(0, &params, bSkinned);
	eaDestroy(&eaTextureSwaps);
}

void gfxPreloadFastParticleSystems(void)
{
	int i;
	static const char *particle_preloads[] = {
		"PreloadNormal",
		"PreloadLinkScale",
		"PreloadStreak",
		"PreloadStreakChain",
		"PreloadNormalNoTonemap",
		"PreloadLinkScaleNoTonemap",
		"PreloadStreakNoTonemap",
		"PreloadStreakChainNoTonemap",
	};
	DynFxFastParticleSet *pSet;
	DynNode node = {0};

	preload_count_remaining += ARRAY_SIZE(particle_preloads);

	for (i=0; i<ARRAY_SIZE(particle_preloads); i++) 
	{
		DynFPSetParams params = {0};
		params.pInfo = dynFxFastParticleInfoFromName(particle_preloads[i]);
		params.pLocation = &node;
		params.bMaxBuffer = true;
		if (!params.pInfo)
		{
			FatalErrorf("Particle system %s is requried for preloading", particle_preloads[i]);
			continue;
		}
		pSet = dynFxFastParticleSetCreate(&params);
		if (!pSet)
		{
			FatalErrorf("Particle system %s is requried for preloading, but set could not be created", particle_preloads[i]);
			continue;
		}

		ANALYSIS_ASSUME(pSet);
		preloadBeginEndFrame(1, NULL, false);

		{
			Vec3 p1 = {0, 0, 0};
			Vec3 p2 = {5, 10, 0};
			Vec3 p3 = {10, 0, 0};
			gfxDrawTriangle3D(p1, p2, p3, ColorMagenta);
		}

		dynFxFastParticleSetUpdate(pSet, unitvec3, 0.1f, false, true);
		dynFxFastParticleSetUpdate(pSet, unitvec3, 0.1f, false, true);

		gfxQueueSingleFastParticleSet(pSet, NULL, 1.0f);
		preload_count_remaining--;

		preloadBeginEndFrame(0, NULL, false);

		dynFxFastParticleSetDestroy(pSet);
	}

}

void gfxMaterialPreloadFinishPreloading(void)
{
#define SUBMIT_SINGLE_PRELOADS 0
#if SUBMIT_SINGLE_PRELOADS
	// for debugging, set chunk size to 1
	int chunk_size = 1;
#else
	int num_chunks = 8;
	int chunk_size = (eaSize(&templates_to_preload)/num_chunks)+1;
#endif
	int material_preload_debug = gfx_state.debug.material_preload_debug;
	if (material_preload_debug)
		chunk_size = 1;

#if !PLATFORM_CONSOLE
	{
    	int i, j, k, bit=0;
		bool bPreloadMultipleLights = shouldPreloadMultipleLights();
		F32 saved_maxInactiveFps = gfx_state.settings.maxInactiveFps;
		bool saved_disableAsyncVshaderLoad = rdr_state.disableAsyncVshaderLoad;
		int saved_static_lights_per_object = gfx_lighting_options.max_static_lights_per_object;
		int saved_static_lights_per_character = gfx_lighting_options.max_static_lights_per_character;
		bool bSkipBigLights=false;
		gfx_lighting_options.max_static_lights_per_object = 5; // All our fake lights are "static", and we might create up to 5 of them, need them to go through!
		gfx_lighting_options.max_static_lights_per_character = 5;
		gfx_state.settings.maxInactiveFps = 0;
		rdr_state.disableAsyncVshaderLoad = true; // Stall and load vshader synchronously so we make sure our preload material is drawn!
		gfxLoadingSetLoadingMessage(isAprilFools()?"Presacrificing Virgin Shaders":"Preloading shaders");
		gfxLoadingStartWaiting();

		if (!gfx_state.uberlighting && gfx_state.cclighting && !gfx_state.vertexOnlyLighting)
		{
			// LightCombos active
			preload_count_remaining = eaSize(&preloaded_light_combos) * eaSize(&templates_to_preload);
			preload_count_remaining -= eaSize(&templates_to_preload); // not doing "no lights"
		}
		else if (bPreloadMultipleLights)
		{
			// Not uberlighting, preload all 2-light pairs
			preload_count_remaining = (ARRAY_SIZE(preload_lights) * (ARRAY_SIZE(preload_lights)+1)/2 + ARRAY_SIZE(preload_lights)) * eaSize(&templates_to_preload);
		} else if (!(system_specs.material_hardware_supported_features & SGFEAT_SM30)) {
			bSkipBigLights = true;
			preload_count_remaining = (ARRAY_SIZE(preload_lights)+1)/2 * eaSize(&templates_to_preload);
		} else {
			preload_count_remaining = ARRAY_SIZE(preload_lights) * eaSize(&templates_to_preload);
		}
		preload_count_remaining += eaSize(&templates_to_preload); // no lights

		loadstart_printf("Finishing shader pre-loading (%d templates, %d combinations)...", eaSize(&templates_to_preload), preload_count_remaining);

		initPreloadLights();

		// Preload vertex shaders
		gfxPreloadFastParticleSystems();

		// Preload pixel shaders

		if (!gfx_state.uberlighting && gfx_state.cclighting && !gfx_state.vertexOnlyLighting)
		{
			FOR_EACH_IN_EARRAY(preloaded_light_combos, PreloadedLightCombo, light_combo)
			{
				GfxLight *lights[MAX_NUM_OBJECT_LIGHTS] = {0};
				for (i=0; i<MAX_NUM_OBJECT_LIGHTS; i++)
				{
					if (light_combo->light_type[i])
						lights[i] = gfxUpdateLight(NULL, &preload_lights[preloadLightForType(light_combo->light_type[i])], 10000, NULL);
				}

				for (i=0; i<eaSize(&templates_to_preload); i+=chunk_size) {
					int frame, frames_to_preload = material_preload_debug;
					if ( !frames_to_preload )
						frames_to_preload = 1;
					preload_count_remaining += frames_to_preload - 1;
					for (frame = 0; frame < frames_to_preload; ++frame )
					{
						finishPreload(i, MIN(i+chunk_size, eaSize(&templates_to_preload)), bit);
						bit^=1;
					}
				}

				for (i=0; i<MAX_NUM_OBJECT_LIGHTS; i++)
				{
					if (lights[i])
						gfxRemoveLight(lights[i]);
				}
			}
			FOR_EACH_END;
		} else {
			// No lights
			for (i=0; i<eaSize(&templates_to_preload); i+=chunk_size) {
				int frame, frames_to_preload = material_preload_debug;
				if ( !frames_to_preload )
					frames_to_preload = 1;
				preload_count_remaining += frames_to_preload - 1;
				for (frame = 0; frame < frames_to_preload; ++frame )
				{
					finishPreload(i, MIN(i+chunk_size, eaSize(&templates_to_preload)), bit);
					bit^=1;
				}
			}

			for (j=0; j<ARRAY_SIZE(preload_lights); j++)
			{
				GfxLight *light;

				if (preload_lights[j].cast_shadows && bSkipBigLights)
					continue; // Shadowed lights need SM20PLUS
			
				light = gfxUpdateLight(NULL, &preload_lights[j], 10000, NULL);

				// Single light
				for (i=0; i<eaSize(&templates_to_preload); i+=chunk_size) {
					int frame, frames_to_preload = material_preload_debug;
					if ( !frames_to_preload )
						frames_to_preload = 1;
					preload_count_remaining += frames_to_preload - 1;
					for (frame = 0; frame < frames_to_preload; ++frame )
					{
						finishPreload(i, MIN(i+chunk_size, eaSize(&templates_to_preload)), bit);
						bit^=1;
					}
				}
				if (bPreloadMultipleLights) {
					// Multiple lights
					for (k=j; k<ARRAY_SIZE(preload_lights); k++) {
						GfxLight *light2 = gfxUpdateLight(NULL, &preload_lights[k], 10000, NULL);
						for (i=0; i<eaSize(&templates_to_preload); i+=chunk_size) {
							int frame, frames_to_preload = material_preload_debug;
							if ( !frames_to_preload )
								frames_to_preload = 1;
							preload_count_remaining += frames_to_preload - 1;
							for (frame = 0; frame < frames_to_preload; ++frame )
								finishPreload(i, MIN(i+chunk_size, eaSize(&templates_to_preload)), bit);
							bit^=1;
						}
						gfxRemoveLight(light2);
					}
				}
				gfxRemoveLight(light);
			}
		}
		eaDestroy(&templates_to_preload);

		rdrFlush(gfx_state.currentDevice->rdr_device, true);

		rdr_state.nvidiaLowerOptimization = 1; // Min setting

		devassert(preload_count_remaining == 0);
		preload_count_remaining = 0;

		loadend_printf(" done.");
		gfxLoadingFinishWaiting();
		gfxLoadingSetLoadingMessage("Loading/binning world data");
		preloadBeginEndFrame(1, NULL, false);
		preloadBeginEndFrame(0, NULL, false);
		preloadBeginEndFrame(1, NULL, false);
		preloadBeginEndFrame(0, NULL, false);

		gfx_lighting_options.max_static_lights_per_object = saved_static_lights_per_object;
		gfx_lighting_options.max_static_lights_per_character = saved_static_lights_per_character;
		rdr_state.disableAsyncVshaderLoad = saved_disableAsyncVshaderLoad;
		gfx_state.settings.maxInactiveFps = saved_maxInactiveFps;
#ifdef _FULLDEBUG
		rdr_state.echoShaderPreloadLog = 1;
		rdr_state.echoShaderPreloadLogIgnoreVirgins = 0;
#endif
		if (isDevelopmentMode() && rdr_state.showDebugLoadingPixelShader==0)
			rdr_state.showDebugLoadingPixelShader = 2; // Set to 2, will get set to 0 if a reload occurrs.  Command line/console overrides this
	}
#endif

	eaClear(&templates_to_preload);
	gfxMaterialsFreeShaderText();
}

void gfxMaterialPreloadOncePerFrame(bool bEarly)
{
	// Possible TODOs: change gfxMaterialPreloadTemplates to simply loop through and flag
	//  the templates as preloading (move this earlier in the game startup?)
	// Change Start and Finish preloading into a state-based thing and call the
	//  appropriate functions each frame.
	// Maybe remove the drawing loop from within these functions altogether.
	// Caveats: Make sure when we remove the drawing loop that we're still
	//  getting the preloading benefit!

	static const char *lastMapFileName;
	const char *currentMapFileName = zmapGetFilename(NULL);
	if (currentMapFileName != lastMapFileName && !worldZoneMapPatching(false))
	{
		lastMapFileName = currentMapFileName;
		gfxPreloadCheckStartMapSpecific(bEarly);
		gfxSetErrorfDelay(2); 
	}

	gfxResetFrameRateStabilizerCounts();
}
