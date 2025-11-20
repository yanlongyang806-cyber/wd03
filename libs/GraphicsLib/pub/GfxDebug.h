#ifndef GFXDEBUG_H
#define GFXDEBUG_H
GCC_SYSTEM

#include "WorldLibEnums.h"
#include "RdrEnums.h"

#define GRAPHICSLIB_Z 30000 // Must be above UI_TOP_Z and UI_PANE_Z and UI_INFINITE_Z

typedef struct GPolySet GPolySet;
typedef struct GMesh GMesh;
typedef struct RdrSurface RdrSurface;
typedef struct GeoRenderInfo GeoRenderInfo;
typedef struct RdrDrawListPassStats RdrDrawListPassStats;
typedef struct BasicTexture BasicTexture;
typedef U64 TexHandle;
typedef struct GeoMemUsage GeoMemUsage;
typedef struct WorldRegion WorldRegion;

#undef gfxStatusPrintf
int gfxStatusPrintf(FORMAT_STR char const *fmt, ...);
#define gfxStatusPrintf(fmt, ...) gfxStatusPrintf(FORMAT_STRING_CHECKED(fmt), __VA_ARGS__)
#define TEXT_JUSTIFY 8192
#define gfxXYZprintf(x, y, z, fmt, ...) gfxXYZprintfColor_s(x, y, z, 255, 255, 255, 255, FORMAT_STRING_CHECKED(fmt), __VA_ARGS__)
#define gfxXYprintf(x, y, fmt, ...) gfxXYZprintfColor_s(x, y, GRAPHICSLIB_Z+1, 255, 255, 255, 255, FORMAT_STRING_CHECKED(fmt), __VA_ARGS__)
void gfxXYZprintfColor_s(int x, int y, int z, U8 r, U8 g, U8 b, U8 a, FORMAT_STR char const *fmt, ...);
#define gfxXYZprintfColor(x, y, z, r, g, b, a, fmt, ...) gfxXYZprintfColor_s(x, y, z, r, g, b, a, FORMAT_STRING_CHECKED(fmt), __VA_ARGS__)
#define gfxXYprintfColor(x, y, r, g, b, a, fmt, ...) gfxXYZprintfColor_s(x, y, GRAPHICSLIB_Z+3, r, g, b, a, FORMAT_STRING_CHECKED(fmt), __VA_ARGS__)
void gfxXYZprintfColor2_s(int x, int y, int z, U32 color, FORMAT_STR char const *fmt, ...);
#define gfxXYprintfColor2(x, y, color, fmt, ...) gfxXYZprintfColor2_s(x, y, GRAPHICSLIB_Z+3, color, FORMAT_STRING_CHECKED(fmt), __VA_ARGS__)
void gfxDebugPrintfQueue(FORMAT_STR char const *fmt, ...);
#define gfxDebugPrintfQueue(fmt, ...) gfxDebugPrintfQueue(FORMAT_STRING_CHECKED(fmt), __VA_ARGS__)
void gfxDebugPrintfQueueColor(U32 color,FORMAT_STR char const *fmt, ...);
#define gfxDebugPrintfQueueColor(color, fmt, ...) gfxDebugPrintfQueueColor(color, FORMAT_STRING_CHECKED(fmt), __VA_ARGS__)

void gfxDisplayScreenLog(int display);
void gfxClearScreenLog(void);
void gfxSetScreenLogPriority(int min_message_priority);
void gfxPrintToScreenLog(int message_priority, FORMAT_STR char const *fmt, ...);
#define gfxPrintToScreenLog(message_priority, fmt, ...) gfxPrintToScreenLog(message_priority, FORMAT_STRING_CHECKED(fmt), __VA_ARGS__)

// show_alpha == 2 -> draw alpha as red
void gfxDebugThumbnailsAdd(TexHandle tex_handle, const char *title, int show_alpha, bool is_shadow_map);
void gfxDebugThumbnailsAddTexture(BasicTexture *tex, const char *title, int show_alpha, S8 force_mip);
void gfxDebugThumbnailsAddSurface(RdrSurface *surface, RdrSurfaceBuffer buffer, int snapshot_idx, const char *title, int show_alpha);
void gfxDebugThumbnailsAddSurfaceCopy(RdrSurface *surface, RdrSurfaceBuffer buffer, int snapshot_idx, const char *title, int show_alpha);
void gfxDebugThumbnailsAddSurfaceMaybeCopy(RdrSurface *surface, RdrSurfaceBuffer buffer, int snapshot_idx, const char *title, int show_alpha, bool bCopy);
void gfxDebugThumbnailsDisplay(void);

void gfxDrawGPolySet(GPolySet *set, const Vec3 camera_mid);
void gfxDrawGMesh(GMesh *mesh, Color c, int use_mesh_colors);

void gfxDisplayDebugInterface2D(int in_editor);
void  gfxDebugDrawDynDebug2D(void);
void  gfxDebugDrawDynDebug3D(void);

void gfxDebugGrabFrame(int index);
void gfxDebugShowGrabbedFrame(int index, U32 color, F32 effect_weight);
BasicTexture *gfxDebugGetGrabbedFrame(int index);
void gfxDebugUpdateGrabbedFrameTimestamp(int index);
void gfxDebugDoneWithGrabbedFrame(int index);

void gfxDebugClearAccessLevelCmdWarnings(void);
void gfxDebugDisableAccessLevelWarnings(bool disable);
void gfxDebugDisableAccessLevelWarningsTemporarily(float fTimeout);

void gfxDebugPrintString(const char *cmdstr);

int drawButton2D(int x1, int y1, int dx, int dy, int centered, char* text, float scale, char* command, int* setMe);
void printDebugString(char* outputString, int x, int y, float scale, int lineHeight, int overridecolor, int defaultcolor, U8 alpha, int* getWidthHeightBuffer);

void gfxGeoGetMemUsage(SA_PRE_NN_FREE SA_POST_NN_VALID GeoMemUsage *usage, WLUsageFlags flags_for_total);
U32 gfxModelGetHighDetailAdjust(void);

AUTO_STRUCT;
typedef struct GeoMemUsageEntry
{
	const char *filename; AST( POOL_STRING )
	const char *name; AST( POOL_STRING )
	int vid_mem;
	int sys_mem_packed; // And overhead/structure size
	int sys_mem_unpacked;
	int total_mem;
	int verts;
	int tris;
	int sub_objects;
	int countInScene;
	int uniqueInScene;
	int trisInScene;
	int far_dist;
	F32 uv_density;
	bool recent;
	bool shared;
	const char *lod_template_name; AST( POOL_STRING )
	GeoRenderInfo *geo_render_info; NO_AST
} GeoMemUsageEntry;
void gfxGeoGetMemUsageDetailed(WLUsageFlags flags, GeoMemUsageEntry ***entries);
void gfxGeoGetWhoUsesTexture(const char *textureName, bool currentSceneOnly, GeoMemUsageEntry ***entries);
U32 gfxGetTotalTrackedVideoMemory(void);

void gfxGeoSelectByRenderInfo(GeoRenderInfo *geo_render_info);

void gfxTextureSelect(BasicTexture *tex);

extern ParseTable parse_GeoMemUsageEntry[];
#define TYPE_parse_GeoMemUsageEntry GeoMemUsageEntry

void gfxGeoGetDrawCallsDetailed(WLUsageFlags flags, GeoMemUsageEntry ***entries);

typedef struct VSCost {
	const char *name;
	int triangles;
	F32 ms;
} VSCost;
#define VS_COST_SIZE 4
F32 gfxGetApproxPassVertexShaderTime(RdrDrawListPassStats *pass_stats, SA_PRE_OP_FREE SA_POST_OP_VALID VSCost costs[VS_COST_SIZE], bool high_detail_only, bool old_intel_times);

int gfxGetBottleneck(void);
const char *gfxGetBottleneckString(int val);

// This structure helps implement dummy frame loops. Call gfxDummyFrameSequenceStart before the frame loop, and
// gfxDummyFrameSequenceEnd after.
typedef struct GfxDummyFrameInfo
{
	bool was_in_ui;
	bool was_in_misc;
	bool was_ignoringInput;

	bool preframe_assert_on_misnested_budgets;
} GfxDummyFrameInfo;

// You must bracket dummy frame loops with these functions. See GfxDummyFrameInfo.
void gfxDummyFrameSequenceStart(GfxDummyFrameInfo * frame_info);
void gfxDummyFrameSequenceEnd(GfxDummyFrameInfo * frame_info);

// Note, you must initialize (and clean up) a dummy frame loop state struct (GfxDummyFrameInfo) surrounding the dummy frame sequence. See GfxDummyFrameInfo.
void gfxDummyFrameTopEx(GfxDummyFrameInfo * frame_info, F32 elapsed, bool allow_offscreen_render);
__forceinline static void gfxDummyFrameTop(GfxDummyFrameInfo * frame_info, F32 elapsed) { gfxDummyFrameTopEx( frame_info, elapsed, false ); }
void gfxDummyFrameBottom(GfxDummyFrameInfo * frame_info, WorldRegion **regions, bool load_all);

void gfxShowPleaseWaitMessage(const char *message); // For slow things like /reloadshaders

void gfxDebugViewport(IVec2 viewportMin, IVec2 viewportMax);
int gfxXYgetY(int y);
int gfxXYgetX(int x);

void gfxSetErrorfDelay(int delayFrames);
bool gfxDelayingErrorf(void);

bool gfxShouldShowRestoreButtons(void);
F32 gfxTimeSinceWindowChanged(void);
void gfxWindowRestoreToggle(void);
void gfxWindowMinimize(void);

void gfxDebugDrawCollision();

void gfxModelSetLODOffset(int lod_offset);
int gfxModelGetLODOffset();
void gfxModelSetLODForce(int lod_force);
int gfxModelGetLODForce();

void gfxDebugOncePerFrame(F32 fDeltaTime);

//used by the 2d debug HUD
LATELINK;
const char *GetGameServerIDAndPartitionString(void);

#endif
