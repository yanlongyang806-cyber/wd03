#ifndef GFXTEXTURETOOLS_H
#define GFXTEXTURETOOLS_H
GCC_SYSTEM

#include "GfxTextureEnums.h"
#include "WorldLibEnums.h"
#include "RdrEnums.h"

typedef struct ShaderTemplate ShaderTemplate;
typedef struct Material Material;
typedef struct RdrShaderPerformanceValues RdrShaderPerformanceValues;
typedef struct ShaderGraph ShaderGraph;
typedef struct ShaderGraphRenderInfo ShaderGraphRenderInfo;
typedef struct TexWord TexWord;
typedef struct TexOpt TexOpt;
typedef int RdrTexFormat;
typedef struct TaskProfile TaskProfile;

typedef struct MaterialPreview
{
	char *op_name; // EString
	char *fallback_name; // EString

	ShaderTemplate *main_template;
	Material *main_material;

	ShaderTemplate *preview_template;
	Material *preview_material;
} MaterialPreview;


// Texture related functions used by tools (exposes some implementation-dependent/private data)

bool texWriteTimestamp(
	const char *filename,
	const char *src_tga_path,
	const TexOpt *src_texopt);

void makeTexOptString(char *filename, char *buf, size_t buf_size, TexOpt *texopt, TexOptFlags texopt_flags);

int texPrintInfo(const char *filename);
void texForceTexLoaderToComplete(int force_texword_flush);

void gfxMaterialsFillSpecificRenderValues(ShaderTemplate *shader_template, Material *material);
void gfxMaterialsDeinitMaterial(Material *material);
void gfxMaterialsGetPerformanceValues(ShaderGraphRenderInfo *graph_render_info, RdrShaderPerformanceValues *perf_values);
void gfxMaterialsGetPerformanceValuesSynchronous(ShaderGraphRenderInfo *graph_render_info, RdrShaderPerformanceValues *perf_values);
void gfxMaterialsGetPerformanceValuesEx(ShaderGraphRenderInfo *graph_render_info, RdrShaderPerformanceValues *perf_values, RdrMaterialShader shader_num, bool synch);
void gfxMaterialsGetMemoryUsage(SA_PARAM_NN_VALID Material *material, SA_PRE_NN_FREE SA_POST_NN_VALID U32 *totalMem, SA_PRE_NN_FREE SA_POST_NN_VALID U32 *sharedMem);
void gfxMeasureMaterialBrightness(SA_PARAM_NN_VALID Material *material, Vec3 brightness_values);

void gfxMatPreviewUpdate(MaterialPreview *preview, Material *material, ShaderTemplate *shader_template, ShaderGraph *shader_graph, const char *op_name, const char *fallback_name, bool use_lit);
void gfxMatPreviewFreeData(MaterialPreview *preview);
void gfxMaterialsDeinitShaderTemplate(ShaderTemplate *templ);

const TexWord *texFindTexWord(const BasicTexture *texbind);
const char *texWordFindFilename(const TexWord *texWord);
const char *texWordsGetLocale(void);
TexWord *texWordFind(const char *texName, int search);

// TexOpts
typedef struct TexOpt TexOpt;
TexOpt *texoptFromTextureName(const char *name, TexOptFlags *texopt_flags);
TexOptMipFilterType texoptGetMipFilterType(const TexOpt *texopt);
TexOptQuality texoptGetQuality(const TexOpt *texopt);
TexOptCompressionType texoptGetCompressionType(const TexOpt *texopt, TexOptFlags texopt_flags);
Color texoptGetBorderColor(const TexOpt *texopt);
F32 texoptGetAlphaMipThreshold(const TexOpt *texopt);
bool texoptShouldCrunch(const TexOpt *texopt, TexOptFlags texopt_flags);
TexOptMipSharpening texoptGetMipSharpening(const TexOpt *texopt);
const char *texoptGetMipSharpeningString(const TexOpt *texopt);

typedef void (*TexOptReloadCallback)(const char *path);
void texoptSetReloadCallback(TexOptReloadCallback callback);

typedef void (*NewTextureCallback)(BasicTexture *bind);
void texSetNewTextureCallback(NewTextureCallback callback);

typedef struct TexMemUsageData {
	U32 loaded[WL_FOR_MAXCOUNT];
	U32 loadedTotal;
	U32 recent[WL_FOR_MAXCOUNT];
	U32 recentTotal;
} TexMemUsageData;

#define TEX_NUM_DIVISIONS 16 // Number of divisions for doing just partial updates each frame

typedef struct TexMemUsageDataSet
{
	TexMemUsageData video;
	//TexMemUsageData halfRes; // -reduce_mip 1 equivalent
	TexMemUsageData original; // before reductions/all full resolution
} TexMemUsageDataSet;

typedef struct TexMemUsage {
	TexMemUsageDataSet data;
	U32 count[WL_FOR_MAXCOUNT];
	U32 countTotal;

	// Internal data:
	U32 savedCount[WL_FOR_MAXCOUNT];
	U32 savedCountTotal;
	TexMemUsageDataSet saved;
	TexMemUsageDataSet partial[TEX_NUM_DIVISIONS];

	int current_array_index;
	int current_tex_index;
	U32 recent_time;
	WLUsageFlags flags_for_total;
} TexMemUsage;

void texGetMemUsage(SA_PRE_NN_FREE SA_POST_NN_VALID TexMemUsage *usage, WLUsageFlags flags_for_total);

AUTO_STRUCT;
typedef struct TexMemUsageEntry
{
	const char *directory; AST( POOL_STRING )
	const char *filename; AST( POOL_STRING )
	int memory_use;
	int memory_use_original; // Before texReduce/reduce_mip, etc
	int width;
	int height;
	int countInScene;
	F32 dist;
	F32 uv_density;
	int reduced;
	int origWidth;
	int origHeight;
	bool recent;
	bool isLowMips;
	bool shared;
} TexMemUsageEntry;
void texGetMemUsageDetailed(WLUsageFlags flags, TexMemUsageEntry *** entries); // Allocates or reuses an earray of textures/memory sizes

extern ParseTable parse_TexMemUsageEntry[];
#define TYPE_parse_TexMemUsageEntry TexMemUsageEntry

bool gfxMaterialUsesTexture(Material *material, const char *texturename);

extern int sizeof_DDSURFACEDESC2;

void gfxCreateAtmosphereLookupTexture(const char *atmosphere_filename);

bool gfxTexWriteWtex(char* filedata, int width, int height, const char* filename, RdrTexFormat destFormat, TexOptFlags tex_flags);

bool gfxSaveTextureAsPNG(const char *texName, const char *fname, bool invertVerticalAxis, SA_PRE_OP_VALID TaskProfile *saveProfile);

typedef struct StuffBuff StuffBuff;
bool gfxSaveTextureAsPNG_StuffBuff(const char *texName, StuffBuff *sb, bool invertVerticalAxis);

#endif
