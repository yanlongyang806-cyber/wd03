
#include "wininclude.h"
#include "DirectDrawTypes.h"
#include "fileutil2.h"
#include "GfxDebug.h"
#include "GfxDXT.h"
#include "GfxHeadshot.h"
#include "GfxTexAtlas.h"
#include "GfxTextures.h"
#include "GfxTextureTools.h"
#include "GfxSky.h"
#include "GfxLoadScreens.h"
#include "GfxPostprocess.h"
#include "GfxSurface.h"
#include "GfxMaterials.h"
#include "Materials.h"
#include "GraphicsLibPrivate.h"
#include "jpeg.h"
#include "tga.h"
#include "LogParsing.h"
#include "RdrState.h"
#include "WorldGrid.h"
#include "wlSaveDXT.h"
#include "CrypticDXT.h"
#include "dynwind.h"
#include "Color.h"
#include "GlobalTypes.h"
#include "GfxCommonSnap.h"
#include "MapSnap.h"

#include "qsortG.h"

#include "GfxMapSnap.h"

AUTO_RUN_ANON(memBudgetAddMapping(__FILE__, BUDGET_Renderer););

bool g_MapSnapNoTimeOut = false;
bool g_MakeHeatMapImages = false;
bool g_SaveHeatMapsAsTGAs = false;
bool g_ForceAllowMissionMapHeatMaps = false;
bool g_bMapSnapDisableMaterialOverride = false;
bool g_bMapSnapMaterialOverrideSimple = false;
bool g_bMapSnapUseSunIndoors = false;
float g_fMapSnapMaximumPixelsPerWorldUnit = 0;
float g_fMapSnapMinimumPixelsPerWorldUnit = 0;
float g_fMapSnapMaximumSizeThreshold = 1;
float g_fMapSnapMinimumSizeThreshold = 0;
AUTO_CMD_INT(g_MapSnapNoTimeOut, MapSnapNoTimeOut) ACMD_CMDLINE;
AUTO_CMD_INT(g_MakeHeatMapImages, MakeHeatMapImages) ACMD_CMDLINE;
AUTO_CMD_INT(g_SaveHeatMapsAsTGAs, SaveHeatMapsAsTGAs) ACMD_CMDLINE;
AUTO_CMD_INT(g_ForceAllowMissionMapHeatMaps, ForceAllowMissionMapHeatMaps) ACMD_CMDLINE;
AUTO_CMD_FLOAT(gConf.fMapSnapOutdoorRes, MapSnapOutdoorRes) ACMD_CMDLINE;
AUTO_CMD_FLOAT(gConf.fMapSnapIndoorRes, MapSnapIndoorRes) ACMD_CMDLINE;
AUTO_CMD_INT(g_bMapSnapDisableMaterialOverride, MapSnapDisableMaterialOverride) ACMD_CMDLINE;
AUTO_CMD_INT(g_bMapSnapMaterialOverrideSimple, MapSnapMaterialOverrideSimple) ACMD_CMDLINE;

#define MS_OUTDOOR_SIZE 1320.0f		//In feet

static bool taking_map_photos = false;
static mapRoomPhoto **map_room_photo_list=NULL;
static WorldRegion **regions=NULL;
static int heatmap_width = 0;
static int heatmap_height = 0;
static TemplateMapInfo *heatmap_data;
static int heatmap_offset = 0;
static U8 *heatmap_jpeg = NULL;
static GfxTempSurface * s_pTempMapSnapTex;
static bool	s_bMapSnapReady = false;
static int s_DownsampleTimer=0;

bool gfxIsTakingMapPhotos(void)
{
	return taking_map_photos;
}

static F32 gfxMapPhotoOutdoorScale(WorldRegion *region)
{
	if(region) {
		ZoneMapInfo *zmap_info = zmapGetInfo(worldRegionGetZoneMap(region));
		if(	zmapInfoGetMapSnapOutdoorRes(zmap_info) )
			return zmapInfoGetMapSnapOutdoorRes(zmap_info);
	}
	return (gConf.fMapSnapOutdoorRes ? gConf.fMapSnapOutdoorRes : 0.1f);
}

static F32 gfxMapPhotoIndoorScale(WorldRegion *region)
{
	if(region) {
		ZoneMapInfo *zmap_info = zmapGetInfo(worldRegionGetZoneMap(region));
		if(	zmapInfoGetMapSnapIndoorRes(zmap_info) )
			return zmapInfoGetMapSnapIndoorRes(zmap_info);
	}
	return (gConf.fMapSnapIndoorRes ? gConf.fMapSnapIndoorRes : 1.0f);
}

static F32 gfxMapPhotoScale(WorldRegion *region, int width, int height, bool bUseLinearScaling)
{
	if(bUseLinearScaling)
	{
		float fMaxDim = sqrt((float)(width * height));
		float fMaxRes = g_fMapSnapMaximumPixelsPerWorldUnit;
		float fMinRes = g_fMapSnapMinimumPixelsPerWorldUnit;
		float maxThreshold = g_fMapSnapMaximumSizeThreshold - g_fMapSnapMinimumSizeThreshold;
		float result = 1;

		fMaxDim -= g_fMapSnapMinimumSizeThreshold;

		if(fMaxDim <= 0)
			fMaxDim = 0;

		if(fMaxDim > maxThreshold)
			fMaxDim = maxThreshold;

		result = lerp(fMaxRes, fMinRes, fMaxDim / maxThreshold);

		return result;
	}

	if(width > MS_OUTDOOR_SIZE || height > MS_OUTDOOR_SIZE)
		return gfxMapPhotoOutdoorScale(region);
	return gfxMapPhotoIndoorScale(region);
}

// issues a command to the render system to composite the hi-res photos onto the low-res surface (float values [0..1])
void gfxMapSnapQueueCompositeAction(GfxTempSurface * pSurface,BasicTexture * pMapSnap,float fX,float fY,float fWidth,float fHeight)
{
	RdrScreenPostProcess ppscreen = {0};
	F32 afTopLeft[2] = {fX,fY+fHeight};
	F32 afBottomRight[2] = {fX+fWidth,fY};

	devassert(pSurface->surface);
	gfxSetActiveSurface(pSurface->surface);

	ppscreen.material.constants = 0;
	ppscreen.material.const_count = 0;

	ppscreen.material.tex_count = 1;
	ppscreen.material.textures = &pMapSnap->tex_handle;

	rdrChangeTexHandleFlags(&pMapSnap->tex_handle, RTF_CLAMP_U|RTF_CLAMP_V);
	ppscreen.shader_handle = gfxDemandLoadSpecialShader(GSS_ALPHABLIT);

	ppscreen.blend_type = RPPBLEND_REPLACE;

	ppscreen.write_depth = 0;
	ppscreen.depth_test_mode = 0;

	ppscreen.tex_width = pMapSnap->width;
	ppscreen.tex_height = pMapSnap->height;

	ppscreen.use_texcoords = 1;
	setVec4(ppscreen.texcoords[0], 1.0f, 0.0f, 0.0f, 1.0f);
	setVec4(ppscreen.texcoords[1], 0.0f, 0.0f, 0.0f, 1.0f);
	setVec4(ppscreen.texcoords[2], 0.0f, 1.0f, 0.0f, 1.0f);
	setVec4(ppscreen.texcoords[3], 1.0f, 1.0f, 0.0f, 1.0f);

	ppscreen.exact_quad_coverage = 1;

	gfxPostprocessScreenPart(&ppscreen,afTopLeft,afBottomRight);
}

void gfxMapSnapQueueDownsampleAction(GfxTempSurface * pSurface,BasicTexture * pSourceTexture)
{
	RdrScreenPostProcess ppscreen = {0};
	F32 afTopLeft[2] = {0,1.0f};
	F32 afBottomRight[2] = {1.0f,0};

	devassert(pSurface->surface);
	gfxSetActiveSurface(pSurface->surface);

	ppscreen.material.constants = 0;
	ppscreen.material.const_count = 0;

	ppscreen.material.tex_count = 1;
	ppscreen.material.textures = &pSourceTexture->tex_handle;

	rdrChangeTexHandleFlags(&pSourceTexture->tex_handle, RTF_CLAMP_U|RTF_CLAMP_V);
	ppscreen.shader_handle = gfxDemandLoadSpecialShader(GSS_ALPHABLIT);

	ppscreen.blend_type = RPPBLEND_REPLACE;

	ppscreen.write_depth = 0;
	ppscreen.depth_test_mode = 0;

	ppscreen.tex_width = pSourceTexture->width;
	ppscreen.tex_height = pSourceTexture->height;

	ppscreen.use_texcoords = 1;
	setVec4(ppscreen.texcoords[0], 1.0f, 0.0f, 0.0f, 1.0f);
	setVec4(ppscreen.texcoords[1], 0.0f, 0.0f, 0.0f, 1.0f);
	setVec4(ppscreen.texcoords[2], 0.0f, 1.0f, 0.0f, 1.0f);
	setVec4(ppscreen.texcoords[3], 1.0f, 1.0f, 0.0f, 1.0f);

	gfxPostprocessScreenPart(&ppscreen,afTopLeft,afBottomRight);

//	gfxPostprocessScreen(&ppscreen);
}

GfxTempSurface * gfxMapSnapCreateTempSurface(int iWidth,int iHeight)
{
	RdrSurfaceParams surfaceparams = {0};
	surfaceparams.name = "MapsnapTemp";
	rdrSurfaceParamSetSizeSafe(&surfaceparams, iWidth, iHeight);
	surfaceparams.desired_multisample_level = 1;
	surfaceparams.required_multisample_level = 1;

	// Set up parameters for surface
	surfaceparams.flags = 0;
	surfaceparams.buffer_types[0] = SBT_RGBA;

	rdrSetDefaultTexFlagsForSurfaceParams(&surfaceparams);

	return gfxGetTempSurface(&surfaceparams);
}

static void gfxTakeMapPhotoCB(mapRoomPhotoAction *photo_action, U8* cap_data, int iHeadshotWidth, int iHeadshotHeight, char **ppExtraInfo)
{
	BasicTexture * pTex = photo_action->headshot_tex;

	photo_action->headshot_tex = NULL;
	photo_action->waiting_for_headshot = false;

	if(!cap_data)
		Errorf("Headshots code did not return a buffer, map photos will come out incorrect.");

	if(photo_action->cell->w*2 != iHeadshotWidth || photo_action->cell->h*2 != iHeadshotHeight)
	{
		Errorf("Mis-matched map snap image dimensions.  Possible data leakage and image not taken.");
		return;
	}

	// Tell the graphics card to blit this headshot into the final map-snap image
	s_pTempMapSnapTex = gfxMapSnapCreateTempSurface(photo_action->cell->w,photo_action->cell->h);
	if (s_pTempMapSnapTex == NULL)
	{
		Errorf("Unable to allocate temp surface for map snap!");
		return;
	}

	rdrSurfaceUpdateMatrices( s_pTempMapSnapTex->surface, unitmat44, unitmat44, unitmat44, unitmat, unitmat, unitmat, 0.1, 1, 0.95,
								  0, 1, 0, 1,
								  zerovec3 );

	gfxMapSnapQueueDownsampleAction(s_pTempMapSnapTex,pTex);
	photo_action->waiting_for_downsample = true;

	// Actually, the low-res image quality can probably be improved by using a less crude down-sampling method.  We are not getting good filtering here, since
	// tri-linear is not available.  TODO - improve [RMARR - 1/24/12]
	if (photo_action->cell->parent->pOverviewTextureBuffer)
	{
		// Tell the graphics card to blit this headshot into the low-res "overview" mapsnap
		const F32 fFullWidthWorld = photo_action->cell->parent->max[0]-photo_action->cell->parent->min[0];
		const F32 fFullHeightWorld = photo_action->cell->parent->max[2]-photo_action->cell->parent->min[2];
		// this prevents stretching the texture when there is no resolution to be gained.
		const F32 fScaleX = MAX(MS_OV_IMAGE_SIZE/(F32)photo_action->cell->parent->full_w,1.0f);
		const F32 fScaleY = MAX(MS_OV_IMAGE_SIZE/(F32)photo_action->cell->parent->full_h,1.0f);

		const F32 fFunctionalWidth = fFullWidthWorld*fScaleX;
		const F32 fFunctionalHeight = fFullHeightWorld*fScaleY;
		F32 fDestWidth = photo_action->cell->world_w/fFunctionalWidth;
		F32 fDestHeight = photo_action->cell->world_h/fFunctionalHeight;
		F32 fDestStartX = ((photo_action->cell->center[0]-photo_action->cell->world_w*0.5f)-photo_action->cell->parent->min[0])/fFunctionalWidth;
		F32 fDestStartY = 1.0f/fScaleY-(((photo_action->cell->center[2]-photo_action->cell->world_h*0.5f)-photo_action->cell->parent->min[2])/fFunctionalHeight)-fDestHeight;
		gfxMapSnapQueueCompositeAction(photo_action->cell->parent->pOverviewTextureBuffer,pTex,fDestStartX,fDestStartY,fDestWidth,fDestHeight);
	}
}

void FinishMapSnap(RoomInstanceMapSnapAction *action, U8 *cap_data, int cell_x, int cell_y, int cell_w, int cell_h, int full_w, int full_h, U8 **raw_data)
{
	int y;
	bool do_draw_check;
	Vec2 min, max;
	int iSnapWidth = cell_w;
	int iSnapHeight = cell_h;
	int minPixelX = 0;
	int maxPixelX = cell_w;
	int minPixelY = 0;
	int maxPixelY = cell_h;
	int y0, y1;
	int ellipse_a, ellipse_b, ellipse_h, ellipse_k;
	S32 *eaScanlineValues = NULL;

	PERFINFO_AUTO_START_FUNC();

	if(*raw_data == NULL)
		*raw_data = calloc(1, iSnapWidth*iSnapHeight*sizeof(U8)*4);

	do_draw_check = (action && !nearSameVec2(action->min_sel, action->max_sel));
	if(do_draw_check)
	{
		vec2MinMax(action->min_sel, action->max_sel, min, max);

		// We can just quickly reject regions of the image outside the Map Snap Action shape's bounding box if we know that bounding box in image space pixels
		minPixelX = min[0] * full_w - cell_x;
		maxPixelX = max[0] * full_w - cell_x;
		maxPixelY = -min[1] * full_h + cell_y + iSnapHeight - 1;
		minPixelY = -max[1] * full_h + cell_y + iSnapHeight - 1;

		// equation of ellipse in terms of a, b, h, and k.
		ellipse_a = (maxPixelX - minPixelX) >> 1;
		ellipse_b = (maxPixelY - minPixelY) >> 1;
		ellipse_h = (maxPixelX + minPixelX) >> 1;
		ellipse_k = (maxPixelY + minPixelY) >> 1;
	}

	eaiStackCreate(&eaScanlineValues, 4); // these are for the X values that make up our scanline of included pixels

	y0 = max(0, do_draw_check ? minPixelY : 0);
	y1 = min(iSnapHeight, do_draw_check ? maxPixelY : iSnapHeight);
	for( y=y0; y<y1; y++ )
	{
		int scanline;
		eaiClearFast(&eaScanlineValues);
		if(do_draw_check && action->action_type == MSNAP_Ellipse)
		{
			// Given y, solve for the 2 x values. That is our span.
			int sqrt_term = ellipse_a * sqrt(1.0f - SQR(y - ellipse_k) / (F32)SQR(ellipse_b));
			int x0 = sqrt_term + ellipse_h;
			int x1 = -sqrt_term + ellipse_h;
			eaiPush(&eaScanlineValues, min(x0, x1));
			eaiPush(&eaScanlineValues, max(x0, x1));
		}
		else if(do_draw_check && action->action_type == MSNAP_Free)
		{
			// Given y, solve for all the x values and sort them. This gives us our spans. See the following:
			// http://www.ecse.rpi.edu/Homepages/wrf/Research/Short_Notes/pnpoly.html
			// For simple (convex and concave) and complex (self-intersecting) polygons. They are assumed to be closed.
			// The idea behind this is pretty simple. It is based on the observation that a test point is within a
			// polygon if when projected on the y-axis it's x value is below odd number of polygon edges.
			int u, v, nvert = eafSize(&action->points) / 2;
			for(u = 0, v = nvert - 1; u < nvert; v = u++)
			{
				F32 vertx_u = action->points[u*2];
				F32 verty_u = action->points[u*2+1];
				F32 vertx_v = action->points[v*2];
				F32 verty_v = action->points[v*2+1];
				int i_vertx_u = vertx_u * full_w - cell_x;
				int i_verty_u = -verty_u * full_h + cell_y + iSnapHeight - 1;
				int i_vertx_v = vertx_v * full_w - cell_x;
				int i_verty_v = -verty_v * full_h + cell_y + iSnapHeight - 1;
				if((i_verty_u > y) != (i_verty_v > y)) // our scanline y value falls between segment endpoint y values
				{
					int x = ((i_vertx_v - i_vertx_u) * (y - i_verty_u)) / (i_verty_v - i_verty_u) + i_vertx_u;
					eaiPush(&eaScanlineValues, x);
				}
			}
			eaiQSortG(eaScanlineValues, intCmp);
		}
		else // handles MSNAP_Rectangle or entire image if not being cut with an action
		{
			eaiPush(&eaScanlineValues, minPixelX);
			eaiPush(&eaScanlineValues, maxPixelX);
		}

		// Okay, the scanline values array always has an even bumber of X values. On this scanline, we include pixels along the X axis
		// from eaScanlineValues[0] to eaScanlineValues[1], eaScanlineValues[2] to eaScanlineValues[3], and so on.
		// The cases of ellipse and rectangle will always have exactly one scanline here. The polygon should have at least 1, but
		// degenerates may produce none.
		for( scanline=0; scanline < eaiSize(&eaScanlineValues) / 2; scanline++ )
		{
			int x0 = eaScanlineValues[scanline*2];
			int x1 = eaScanlineValues[scanline*2+1];
			x0 = max(0, x0);
			x1 = min(iSnapWidth, x1);
			if(x1 > x0) // otherwise, degenerate
				memcpy(&(*raw_data)[(x0+y*iSnapWidth)*4], &cap_data[(x0+y*iSnapWidth)*4], (x1 - x0) * 4);
		}
	}

	eaiDestroy(&eaScanlineValues);

	PERFINFO_AUTO_STOP();
}

mapRoomPhoto* gfxMakeMapPhoto(	const char *image_prefix,
								MapSnapRoomPartitionData * pPartitionData, WorldRegion *region,
								Vec3 room_min, Vec3 room_mid, Vec3 room_max, 
								RoomInstanceMapSnapAction **action_list,
								const char *debug_filename, const char *debug_def_name,
								bool override_image, const char *override_name)
{
	int i,j;
	F32 width, height;
	int tex_width, tex_height;
	mapRoomPhoto *new_photo;
	int iMapSnapImageCellSize = (gConf.iMapSnapImageCellSize ? gConf.iMapSnapImageCellSize : MS_DEFAULT_IMAGE_CELL_SIZE);

	Vec3 vSnapMin,vSnapMax;
	Vec3 vAdjustedMin,vAdjustedMax;

	F32 fFocusHeight = room_mid[1];
	F32 fRegionFocusHeight = 0.0f;

	mapSnapUpdateRegion(region);

	copyVec3(room_min,vSnapMin);
	// Some rooms are defined implicitly from the geo.  We want a small vertical buffer for those rooms
	vSnapMin[1] -= 0.01f;
	copyVec3(room_max,vSnapMax);

	if (region)
	{
		MapSnapRegionData *pRegionData = worldRegionGetMapSnapData(region);
		fRegionFocusHeight = fFocusHeight = pRegionData->fGroundFocusHeight;

		if (fFocusHeight < room_min[1] || fFocusHeight > room_max[1])
		{
			// try to use the minimum map snap action height
			const int iNumActions = eaSize(&action_list);
			if(iNumActions > 0)
			{
				int iAction;
				F32 fMaxHeight = -FLT_MAX;
				F32 fMinHeight = FLT_MAX;
				for (iAction=0;iAction<iNumActions;iAction++)
				{
					RoomInstanceMapSnapAction * pAction = action_list[iAction];
					F32 fLowHeight = pAction->far_plane + room_min[1];
					F32 fHighHeight = pAction->near_plane + room_min[1];
					if (fLowHeight < fMinHeight)
					{
						fMinHeight = fLowHeight;
					}
					if (fHighHeight > fMaxHeight)
					{
						fMaxHeight = fHighHeight;
					}
				}

				fFocusHeight = fMinHeight;

				vSnapMin[1] = fMinHeight;
				vSnapMax[1] = fMaxHeight;
			}
			else
			{
				// point at the bottom of the room partition
				vSnapMin[1] = fFocusHeight = room_min[1] - 1.0f;
			}
		}
	}

	// calculate the FULL extents of this snapshot.
	vAdjustedMax[0] = vSnapMax[0]+gfCurrentMapOrthoSkewX*(vSnapMax[1]-fFocusHeight);
	vAdjustedMax[1] = vSnapMax[1];
	vAdjustedMax[2] = vSnapMax[2]+gfCurrentMapOrthoSkewZ*(vSnapMax[1]-fFocusHeight);

	vAdjustedMin[0] = vSnapMin[0]+gfCurrentMapOrthoSkewX*(vSnapMin[1]-fFocusHeight);
	vAdjustedMin[1] = vSnapMin[1];
	vAdjustedMin[2] = vSnapMin[2]+gfCurrentMapOrthoSkewZ*(vSnapMin[1]-fFocusHeight);

	width = vAdjustedMax[0]-vAdjustedMin[0];
	height = vAdjustedMax[2]-vAdjustedMin[2];

	if(override_image) {
		AtlasTex *atlas = atlasLoadTexture(override_name);
		if(!atlas) {
			ErrorFilenamef(debug_filename,
				"Room volume %s failed to load override texture %s.",
				debug_def_name, override_name);
			return NULL;
		}
		tex_width = atlas->width;
		tex_height = atlas->height;
	} else {
		F32 fPixelsPerWorldUnit;
		if(region && worldRegionGetType(region) == WRT_Space) {
			tex_width = width;
			tex_height = height;
		} else {
			tex_width = width;
			tex_height = height;	
			if(tex_width*tex_height > 100000000)
			{
				ErrorFilenamef(debug_filename,
					"Room volume %s is too big for MapSnap.  Width (x) times Depth (z) must be less than 100 million.",
					debug_def_name);
				return NULL;
			}
		}

		gfxMapSnapPhotoScaleOptions(region);
		fPixelsPerWorldUnit = gfxMapPhotoScale(region, tex_width, tex_height, gConf.bUseLinearMapSnapResolutionScaling);
		tex_width *= fPixelsPerWorldUnit;
		tex_height *= fPixelsPerWorldUnit;
	}

	//Don't even take images that are less than 4 pixels
	if(tex_width < 4 || tex_height < 4)
		return NULL;
	//If the current resolution will result in us taking an image that is less than 4 pixels, cut off the extra.
	//Some graphics cards will not allow textures of less than 4 pixels
	if(tex_width > MS_IMAGE_CUT_SIZE && tex_width % iMapSnapImageCellSize < 4)
		tex_width -= (tex_width % iMapSnapImageCellSize);
	if(tex_height > MS_IMAGE_CUT_SIZE && tex_height % iMapSnapImageCellSize < 4)
		tex_height -= (tex_height % iMapSnapImageCellSize);			

	if(tex_width*tex_height > 170000000)
	{
		ErrorFilenamef(debug_filename,
			"Room volume %s is too big for MapSnap's set resolution.  Width times Depth in Pixels must be less than 170 million.",
			debug_def_name);
		return NULL;
	}

	if(tex_width > MS_IMAGE_CUT_SIZE || tex_height > MS_IMAGE_CUT_SIZE)
	{
		if(tex_height > heatmap_height)
			heatmap_height = tex_height;
		heatmap_width += tex_width;
	}

	if(pPartitionData)
	{
		Vec3 vRegionMin,vRegionMax;
		Vec2 vA,vB;

		devassert(region);
		mapSnapRegionGetMapBounds(region, vRegionMin,vRegionMax);

		pPartitionData->image_width = tex_width;
		pPartitionData->image_height = tex_height;

		mapSnapWorldPosToMapPos(vRegionMin, vRegionMax, vSnapMin, vA,fRegionFocusHeight);
		mapSnapWorldPosToMapPos(vRegionMin, vRegionMax, vSnapMax, vB,fRegionFocusHeight);

		pPartitionData->vMin[0] = vA[0];
		pPartitionData->vMin[1] = vB[1];

		pPartitionData->vMax[0] = vB[0];
		pPartitionData->vMax[1] = vA[1];
	}

	new_photo = calloc(1, sizeof(*new_photo));
	new_photo->prefix = StructAllocString(image_prefix);
	new_photo->action_list = action_list;
	copyVec3(room_min,new_photo->vPartitionMin); // used later to translate mapsnap actions
	copyVec3(vAdjustedMin, new_photo->min); // used later to determine "total width" of the partition photo
	copyVec3(vAdjustedMax, new_photo->max); // used later to determine "total width" of the partition photo
	new_photo->full_w = tex_width;
	new_photo->full_h = tex_height;
	new_photo->override_image = override_image;
	new_photo->override_name = override_name;
	new_photo->pPartitionData = pPartitionData;
	new_photo->fFocusHeight = fFocusHeight;
	new_photo->region = region;

	if(tex_width > MS_IMAGE_CUT_SIZE || tex_height > MS_IMAGE_CUT_SIZE)
	{
		for( j=0; j < tex_height; j+=iMapSnapImageCellSize )
		{
			for( i=0; i < tex_width; i+=iMapSnapImageCellSize )
			{
				mapRoomPhotoCell *new_cell;
				new_cell = calloc(1, sizeof(*new_cell));
				new_cell->off_x = i;
				new_cell->off_y = j;
				new_cell->w = MIN(iMapSnapImageCellSize, tex_width-i);
				new_cell->h = MIN(iMapSnapImageCellSize, tex_height-j);
				new_cell->parent = new_photo;
				new_cell->world_w = new_cell->w * width   / tex_width;
				new_cell->world_h = new_cell->h * height  / tex_height;
				new_cell->center[0] = vAdjustedMin[0] + new_cell->off_x * width/tex_width   + new_cell->world_w/2.0f;
				new_cell->center[1] = fFocusHeight;
				new_cell->center[2] = vAdjustedMin[2] + new_cell->off_y * height/tex_height + new_cell->world_h/2.0f;
				eaPush(&new_photo->cells, new_cell);
			}
		}
	}
	else
	{
		mapRoomPhotoCell *new_cell;
		new_cell = calloc(1, sizeof(*new_cell));
		new_cell->off_x = 0;
		new_cell->off_y = 0;
		new_cell->w = tex_width;
		new_cell->h = tex_height;
		new_cell->parent = new_photo;
		new_cell->world_w = width;
		new_cell->world_h = height;
		// we offset the center of the camera since that will be mapped to the center of the image, so while the center of the image is
		// at focus height, it's not in the center of the partition
		new_cell->center[0] = vAdjustedMin[0] + new_cell->world_w/2.0f;
		new_cell->center[1] = fFocusHeight;
		new_cell->center[2] = vAdjustedMin[2] + new_cell->world_h/2.0f;
		eaPush(&new_photo->cells, new_cell);
	}
	return new_photo;
}

void gfxAddMapPhoto(const char *image_prefix, MapSnapRoomPartitionData * pPartitionData, Vec3 room_min, Vec3 room_mid, Vec3 room_max, RoomInstanceMapSnapAction **action_list, WorldRegion *region, const char *debug_filename, const char *debug_def_name, bool override_image, const char *override_name)
{
	mapRoomPhoto *new_photo;
	new_photo = gfxMakeMapPhoto(image_prefix, pPartitionData, region, room_min, room_mid, room_max, action_list, debug_filename, debug_def_name, override_image, override_name);
	if(new_photo)
	{
		new_photo->region = region;
		eaPush(&map_room_photo_list, new_photo);
		eaPushUnique(&regions, region);
	}
}


void gfxMapSnapCamFunc(GfxCameraController *camera, GfxCameraView *camera_view, F32 elapsed, F32 real_elapsed)
{
	Mat44 mungeMatrix,tempMatrix;
	identityMat44(mungeMatrix);
	mungeMatrix[2][0] = gfCurrentMapOrthoSkewX;
	mungeMatrix[2][1] = gfCurrentMapOrthoSkewZ;

	gfxEditorCamFunc(camera,camera_view,elapsed,real_elapsed);

	// tweak the ortho matrix that has been set up
	mulMat44Inline(gfx_state.currentCameraView->projection_matrix,mungeMatrix,tempMatrix);
	copyMat44(tempMatrix,gfx_state.currentCameraView->projection_matrix);
}

static void gfxSetupCameraAndTakePhoto(GfxCameraController *camera, mapRoomPhoto *room_photo, mapRoomPhotoCell *cell, mapRoomPhotoAction *photo_action)
{
	const char *sky_override = "map_snap_outdoor";
	bool is_space = false;
	F32 fFocusHeight = cell->center[1];

	if (room_photo->pPartitionData)
	{
		fFocusHeight = room_photo->fFocusHeight;
	}

	camera->ortho_aspect = 1;
	camera->ortho_width = cell->world_w;
	camera->ortho_height = cell->world_h;
	// default cull bounds
	camera->ortho_cull_width = 0;
	camera->ortho_cull_height = 0;
	if (gfCurrentMapOrthoSkewX || gfCurrentMapOrthoSkewZ)
	{
		// TODO - figure out tighter bounds
		camera->ortho_cull_width = cell->world_w*2;
		camera->ortho_cull_height = cell->world_h*2;
	}
	copyVec3(cell->center, camera->camcenter);
	camera->camcenter[1] = fFocusHeight;

	// pad headshots to provide a buffer for all manner of bi-linear filtering, etc. artifacts (4 pixels)
	// Update: we solved the outlining issue, so this feature is less necessary
	//camera->ortho_width *= (cell->w+4.0f)/cell->w;
	//camera->ortho_height *= (cell->h+4.0f)/cell->h;

	if(photo_action->action)
	{
		// actions are stored relative to the partition minimum, so that if maybe a partition (and contents) is moved they will still be roughly in place
		camera->ortho_far = camera->camcenter[1] - (photo_action->action->far_plane + room_photo->vPartitionMin[1]);
		camera->ortho_near = camera->camcenter[1] - (photo_action->action->near_plane + room_photo->vPartitionMin[1]);
	}
	else
	{
		PhotoOptions *photo_options = zmapInfoGetPhotoOptions(NULL, false);
		camera->ortho_far = camera->camcenter[1] - room_photo->min[1];
		camera->ortho_near = camera->camcenter[1] - room_photo->max[1];
		if(photo_options)
		{
			camera->ortho_far += photo_options->far_plane_offset;
			camera->ortho_near += photo_options->near_plane_offset;
		}
	}

	if(room_photo->region)
	{
		if(worldRegionGetType(room_photo->region) == WRT_Space)
		{
			sky_override = "map_snap_space";
			is_space = true;
		}
		else
		{
			SkyInfoGroup *sky_group = worldRegionGetSkyGroup(room_photo->region);
			sky_override = (gfxSkyGroupIsIndoor(sky_group) ? "map_snap_indoor" : "map_snap_outdoor");
		}
	}
	gfxCameraControllerSetSkyOverride(camera, sky_override, NULL);

	// Disable validating the size for mapsnaps, since they can be
	// many odd sizes and are never made during runtime anyway.
	{
		// take images at double resolution, so we can downsample, for quality
		int iCellHeadShotW = cell->w*2;
		int iCellHeadShotH = cell->h*2;
		bool oldDisableValidateSize = gfxHeadshotDisableValidateSize;
		gfxHeadshotDisableValidateSize = true;

		if(is_space)
		{
			BasicTexture* bgTex = texFindAndFlag("Minimap_Space_Grid", true, WL_FOR_UI);
			float pixelXMin = (cell->center[0] - cell->world_w / 2) * gfxMapPhotoOutdoorScale(room_photo->region);
			float pixelZMin = (cell->center[2] - cell->world_h / 2) * gfxMapPhotoOutdoorScale(room_photo->region);
			float pixelXMax = (cell->center[0] + cell->world_w / 2) * gfxMapPhotoOutdoorScale(room_photo->region);
			float pixelZMax = (cell->center[2] + cell->world_h / 2) * gfxMapPhotoOutdoorScale(room_photo->region);
			Vec4 bgTexcoords;
			assertmsg(bgTex, "Missing Solar System Background Image");
			setVec4( bgTexcoords, pixelXMin / bgTex->width, pixelZMin / bgTex->height, pixelXMax / bgTex->width, pixelZMax / bgTex->height );
	
			photo_action->headshot_tex = gfxHeadshotCaptureConstructedScene("MapSnapImage", iCellHeadShotW, iCellHeadShotH, camera, bgTex, bgTexcoords, ColorBlack, gfxSnapGetOutliningEnabled(), gfxTakeMapPhotoCB, photo_action);
		}
		else
		{
			photo_action->headshot_tex = gfxHeadshotCaptureConstructedScene("MapSnapImage", iCellHeadShotW, iCellHeadShotH, camera, NULL, NULL, ColorBlack, gfxSnapGetOutliningEnabled(), gfxTakeMapPhotoCB, photo_action);
		}

		gfxHeadshotDisableValidateSize = oldDisableValidateSize;
	}
}

// This is now ONLY used for override images.  It would be nice to do all such things in hardware
static void gfxMapBlitTexToTex(	U8 *data_in, int w_in, int h_in, U8 *data_out, int w_out, int h_out,
								int pos_x, int pos_y, F32 scale_x, F32 scale_y)
{
	int i, j;
	int bi, bj;
	int new_w = (w_in*scale_x) + 1;
	int new_h = (h_in*scale_y) + 1;
	F32 pix_per_pnt_x = 1.0f/scale_x;
	F32 pix_per_pnt_y = 1.0f/scale_y;
	F32 x_remainder = 0.0f;
	F32 y_remainder = 0.0f;
	int pix_x;
	int pix_y;
	int ix_pos=0;
	int iy_pos=0;

	for( j=0; j < new_h; j++ )
	{
		x_remainder = 0.0f;
		pix_y = y_remainder + pix_per_pnt_y;
		y_remainder = (y_remainder + pix_per_pnt_y) - pix_y;
		ix_pos = 0;
		for( i=0; i < new_w; i++ )
		{
			int pix_cnt = 0;
			Vec3 color;
			int ox = pos_x + i;
			int oy = pos_y + j;

			pix_x = x_remainder + pix_per_pnt_x;
			x_remainder = (x_remainder + pix_per_pnt_x) - pix_x;

			if(ox >= w_out || oy >= h_out)
			{
				ix_pos += pix_x;
				continue;
			}

			setVec3same(color, 0);
			for( bj=0; bj < pix_y; bj++ )
			{
				for( bi=0; bi < pix_x; bi++ )
				{
					int ix = ix_pos + bi;
					int iy = iy_pos + bj;
					if(ix >= w_in || iy >= h_in)
						continue;
					pix_cnt++;
					color[0] += data_in[(ix + iy*w_in)*4 + 0];
					color[1] += data_in[(ix + iy*w_in)*4 + 1];
					color[2] += data_in[(ix + iy*w_in)*4 + 2];
				}
			}
			ix_pos += pix_x;
			if(pix_cnt == 0)
				continue;
			data_out[(ox + oy*w_out)*4 + 0] = color[0]/pix_cnt;
			data_out[(ox + oy*w_out)*4 + 1] = color[1]/pix_cnt;
			data_out[(ox + oy*w_out)*4 + 2] = color[2]/pix_cnt;
		}
		iy_pos += pix_y;
	}
}
static const char* gfxMapDXTCompressAndSave(U8 *raw_data, int w, int h, int true_w, const char *prefix, int idx, const char *zmap_path, bool bDataIsRGBA)
{
	int i, j;
	char full_path[MAX_PATH];
	char full_name[256];
	U8 *file_data;
	U8 *buf;
	U8 *uncompressed_buf;
	int compressed_size;
	int file_size;
	int new_width;
	int new_height;
	DDSURFACEDESC2 header_data;

	if(!raw_data)
		return NULL;

	sprintf(full_name, "%s_%02d.wtex", prefix, idx);
	sprintf(full_path, "%s/bin/geobin/%s/map_snap_images", fileTempDir(), zmap_path);
	assert(makeDirectories(full_path));
	sprintf(full_path, "%s/bin/geobin/%s/map_snap_images/%s_%02d.wtex", fileTempDir(), zmap_path, prefix, idx);
	new_width  = ALIGNUP(w, 4);
	new_height = ALIGNUP(h, 4);
	compressed_size = ((new_width*new_height)/2)*sizeof(U8);

	file_size = compressed_size + 4 + sizeof(DDSURFACEDESC2);
	file_data = calloc(1, file_size);
	buf = file_data;

	//File Type
	memcpy(buf, "DDS ", 4);
	buf += 4;

	//Header
	memset(&header_data, 0, sizeof(DDSURFACEDESC2));
	header_data.ddpfPixelFormat.dwFourCC = FOURCC_DXT1;
	header_data.dwWidth = new_width;
	header_data.dwHeight = new_height;
	memcpy(buf, &header_data, sizeof(DDSURFACEDESC2));
	buf += sizeof(DDSURFACEDESC2);

	//Compress Data
	uncompressed_buf = calloc(new_width*new_height, sizeof(U8)*4);

	if (bDataIsRGBA)
	{
		for( j=0; j < new_height; j++ )
		{
			for( i=0; i < new_width; i++ )
			{
				int rx = (i < w ? i : w-1);
				int ry = (j < h ? j : h-1);
				uncompressed_buf[(i+j*new_width)*4 + 0] = raw_data[(rx+ry*true_w)*4 + 2];
				uncompressed_buf[(i+j*new_width)*4 + 1] = raw_data[(rx+ry*true_w)*4 + 1];
				uncompressed_buf[(i+j*new_width)*4 + 2] = raw_data[(rx+ry*true_w)*4 + 0];
				uncompressed_buf[(i+j*new_width)*4 + 3] = 255;
			}
		}
	}
	else
	{
		for( j=0; j < new_height; j++ )
		{
			for( i=0; i < new_width; i++ )
			{
				int rx = (i < w ? i : w-1);
				int ry = (j < h ? j : h-1);
				uncompressed_buf[(i+j*new_width)*4 + 0] = raw_data[(rx+ry*true_w)*4 + 0];
				uncompressed_buf[(i+j*new_width)*4 + 1] = raw_data[(rx+ry*true_w)*4 + 1];
				uncompressed_buf[(i+j*new_width)*4 + 2] = raw_data[(rx+ry*true_w)*4 + 2];
				uncompressed_buf[(i+j*new_width)*4 + 3] = 255;
			}
		}
	}

	nvdxtCompress(uncompressed_buf, buf, new_width, new_height, RTEX_DXT1, 1, 0);
	texWriteData(full_path, file_data, file_size, NULL, w, h, true, TEXOPT_NOMIP | TEXOPT_CLAMPS | TEXOPT_CLAMPT, NULL, NULL);

/*	sprintf(full_path, "C:/tmp/%s_%02d.tga", prefix, idx);
	tgaSave(full_path, uncompressed_buf, new_width, new_height, 4);*/
	SAFE_FREE(uncompressed_buf);

	SAFE_FREE(file_data);
	return allocAddFilename(full_name);
}

static void gfxMapAddToHeatMap(mapRoomPhoto *room_photo, mapRoomPhotoCell *cell)
{
	int i, j;
	if(!heatmap_jpeg)
		return;

	for( j=0; j < cell->h; j++ )
	{
		for( i=0; i < cell->w ; i++ )
		{
			int hx = cell->off_x + heatmap_offset + i;
			int hy = (room_photo->full_h - cell->off_y - cell->h) + j;
			if(hx >= heatmap_width || hy >= heatmap_height || hy < 0)
				continue;
			heatmap_jpeg[(hx+hy*heatmap_width)*3 + 2] = cell->raw_data[(i+j*cell->w)*4 + 0];
			heatmap_jpeg[(hx+hy*heatmap_width)*3 + 1] = cell->raw_data[(i+j*cell->w)*4 + 1];
			heatmap_jpeg[(hx+hy*heatmap_width)*3 + 0] = cell->raw_data[(i+j*cell->w)*4 + 2];
		}
	}
}

static void gfxMapCellFinished(mapRoomPhoto *room_photo, mapRoomPhotoCell *cell, int idx)
{
	const char *new_name;
	if(	!cell->raw_data || 
		cell->w < 4 || 
		cell->h < 4 || 
		cell->w > 512 || 
		cell->h > 512 )
	{
		Errorf("Bad data on MapSnap Cell Finish");
		return;
	}

	new_name = gfxMapDXTCompressAndSave(cell->raw_data, cell->w, cell->h, cell->w, room_photo->prefix, idx+1, room_photo->zmap_path, false);
	if(room_photo->pPartitionData && new_name)
		eaPush(&room_photo->pPartitionData->image_name_list, new_name);

	//Add data to overview map
	// I ONLY do this in the one case of override images now, because that is the only case I need it.  gfxMapBlitTexToTex introduces an offset to the image,
	// because it is naive, and also provides no filtering.
	if(eaSize(&room_photo->cells) > 1) {

		if(room_photo->override_image) {

			F32 scale_x = (F32)MS_OV_IMAGE_SIZE/room_photo->full_w;
			F32 scale_y = (F32)MS_OV_IMAGE_SIZE/room_photo->full_h;//TODO need to handle one being over 512 and the other not

			scale_x = MIN(1.0f, scale_x);
			scale_y = MIN(1.0f, scale_y);

			assert(scale_x > 0.0f && scale_y > 0.0f); //TODO Need to handle this better.

			if(!room_photo->ov_raw_data)
				room_photo->ov_raw_data = calloc(MS_OV_IMAGE_SIZE*MS_OV_IMAGE_SIZE, sizeof(U8)*4);

			assert(room_photo->full_h-cell->off_y-cell->h >= 0);
			gfxMapBlitTexToTex(	cell->raw_data, cell->w, cell->h, 
								room_photo->ov_raw_data, MS_OV_IMAGE_SIZE, MS_OV_IMAGE_SIZE, 
								cell->off_x*scale_x, (room_photo->full_h-cell->off_y-cell->h)*scale_y, scale_x, scale_y);
		}

		gfxMapAddToHeatMap(room_photo, cell);
	}

	free(cell->raw_data);
	cell->raw_data = NULL;
}

void gfxMapPhotoFromOverride(mapRoomPhoto *room_photo)
{
	int i;
	int x, y;	
	BasicTexture *tex;
	TexReadInfo *raw_info;
	BasicTextureRareData *rare_data;

	tex = texLoadRawData(room_photo->override_name, TEX_LOAD_NOW_CALLED_FROM_MAIN_THREAD, WL_FOR_UI);
	if(!tex)
		return;
	rare_data = texGetRareData(tex);
	if(!rare_data)
		return;
	raw_info = SAFE_MEMBER(rare_data, bt_rawInfo);
	if(!raw_info)
		return;
	assert(raw_info->texture_data);
	verify(uncompressRawTexInfo(raw_info,textureMipsReversed(tex)));

	for ( i=0; i < eaSize(&room_photo->cells); i++ ) {
		mapRoomPhotoCell *cell = room_photo->cells[i];
		int w = cell->w;
		int h = cell->h;
		U8 *cell_data;

		if(!cell->raw_data)
			cell->raw_data = calloc(1, w*h*sizeof(U8)*4);
		cell_data = cell->raw_data;

		for ( y=0; y < cell->h; y++ ) {
			for ( x=0; x < cell->w; x++ ) {
				int tex_x = x + cell->off_x;
				int tex_y = (cell->h-y-1) + cell->off_y;
				tex_y = raw_info->height-tex_y-1;

				if(tex_x < 0 || tex_y < 0 || tex_x >= raw_info->width || tex_y >= raw_info->height) {
					//Error Case
					cell_data[(x+y*w)*4 + 0] = x%255;
					cell_data[(x+y*w)*4 + 1] = y%255;
					cell_data[(x+y*w)*4 + 2] = 255;
					cell_data[(x+y*w)*4 + 3] = 255;
				} else {
					cell_data[(x+y*w)*4 + 0] = raw_info->texture_data[(tex_x + tex_y*raw_info->width)*4 + 0];
					cell_data[(x+y*w)*4 + 1] = raw_info->texture_data[(tex_x + tex_y*raw_info->width)*4 + 1];
					cell_data[(x+y*w)*4 + 2] = raw_info->texture_data[(tex_x + tex_y*raw_info->width)*4 + 2];
					cell_data[(x+y*w)*4 + 3] = raw_info->texture_data[(tex_x + tex_y*raw_info->width)*4 + 3];
				}
			}
		}
	}

	texUnloadRawData(tex);
}

//Returns true if image is finished being taken
bool gfxMapPhotoProcess(GfxCameraController *camera, mapRoomPhoto *room_photo, mapRoomPhotoAction *photo_action, int *frame_count, GfxMapPhotoCellFinishedFunc cell_finished)
{
	static int cell_idx=0;
	static int action_idx=0;
	if(room_photo->override_image) {
		int i;
		gfxMapPhotoFromOverride(room_photo);
		for ( i=0; i < eaSize(&room_photo->cells); i++ ) {
			cell_finished(room_photo, room_photo->cells[i], i);
		}
		return true;
	}
	if(!room_photo->started)
	{
		cell_idx = 0;
		action_idx = 0;
		photo_action->waiting_for_headshot = false;
		room_photo->started = true;
		(*frame_count) = MAX_FRAMES_PER_PIC;
	}
	if(photo_action->waiting_for_headshot)
	{
		(*frame_count)--;
		return false;
	}
	if (photo_action->waiting_for_downsample)
	{
		// This is a stop-gap.  This timer is a bad solution, and the code here is confusing
		s_DownsampleTimer++;
		if (s_DownsampleTimer > 5)
		{
			U8 * pCapData;
			gfxSetActiveSurface(s_pTempMapSnapTex->surface);
			gfxLockActiveDevice();
			pCapData = rdrGetActiveSurfaceData(gfx_state.currentDevice->rdr_device,SURFDATA_BGRA,photo_action->cell->w,photo_action->cell->h);
			FinishMapSnap(photo_action->action, pCapData, photo_action->cell->off_x, photo_action->cell->off_y, photo_action->cell->w, photo_action->cell->h, room_photo->full_w, room_photo->full_h, &photo_action->cell->raw_data);
			free(pCapData);
			gfxUnlockActiveDevice();
			gfxReleaseTempSurface(s_pTempMapSnapTex);
			s_pTempMapSnapTex = NULL;
			s_DownsampleTimer = 0;
			photo_action->waiting_for_downsample = false;
		}
		return false;
	}
	(*frame_count) = MAX_FRAMES_PER_PIC;
	photo_action->waiting_for_headshot = true;

	if(eaSize(&room_photo->action_list) > 0)
	{
		if(action_idx < eaSize(&room_photo->action_list))
		{
			photo_action->cell = room_photo->cells[cell_idx];
			photo_action->action = room_photo->action_list[action_idx];
			gfxSetupCameraAndTakePhoto(camera, room_photo, room_photo->cells[cell_idx], photo_action);
			action_idx++;
			return false;
		}
		cell_finished(room_photo, room_photo->cells[cell_idx], cell_idx);
		action_idx = 0;
		cell_idx++;
		if(cell_idx < eaSize(&room_photo->cells))
		{
			photo_action->cell = room_photo->cells[cell_idx];
			photo_action->action = room_photo->action_list[action_idx];
			gfxSetupCameraAndTakePhoto(camera, room_photo, room_photo->cells[cell_idx], photo_action);
			action_idx++;
			return false;
		}
		return true;
	}
	else
	{
		if(cell_idx > 0)
			cell_finished(room_photo, room_photo->cells[cell_idx-1], cell_idx-1);
		if(cell_idx < eaSize(&room_photo->cells))
		{
			photo_action->cell = room_photo->cells[cell_idx];
			photo_action->action = NULL;
			gfxSetupCameraAndTakePhoto(camera, room_photo, room_photo->cells[cell_idx], photo_action);
			cell_idx++;
			return false;
		}
		return true;
	}
	return true;
}

static void gfxMapRoomPhotoCellDestroy(mapRoomPhotoCell *cell)
{
	if(cell->raw_data)
		free(cell->raw_data);
	free(cell);
}

void gfxMapRoomPhotoDestroy(mapRoomPhoto *room_photo)
{
	eaDestroyEx(&room_photo->cells, gfxMapRoomPhotoCellDestroy);
	if(room_photo->prefix)
		StructFreeString(room_photo->prefix);
	if(room_photo->ov_raw_data)
		free(room_photo->ov_raw_data);
	gfxReleaseTempSurface(room_photo->pOverviewTextureBuffer);
	free(room_photo);
}

static bool gfxMapSnapShouldMakeHeatMaps()
{
	if(!g_MakeHeatMapImages)
		return false;

	if(map_room_photo_list[0])
	{
		ZoneMap *zmap = worldRegionGetZoneMap(map_room_photo_list[0]->region);
		if(zmap)
		{
			ZoneMapInfo *zmap_info = zmapGetInfo(zmap);
			if(zmap_info)
			{
				ZoneMapType map_type = zmapInfoGetMapType(zmap_info);
				if(map_type == ZMTYPE_MISSION && gConf.bDisableMissionMapHeatmaps && !g_ForceAllowMissionMapHeatMaps)
					return false;
			}
		}
	}

	return true;
}

void DEFAULT_LATELINK_gfxMapSnapSetupOptions(WorldRegion * region)
{
	// STO
	if(region && worldRegionGetType(region) == WRT_Space)
	{
		strcpy(g_strSimpleMaterials, "MapSnap_Override_Space_01");
		if( g_bMapSnapMaterialOverrideSimple ) {
			strcpy(g_strSimpleAlphaMaterials, "MapSnap_Override_Space_01");
		} else {
			strcpy(g_strSimpleAlphaMaterials, "Invisible");
		}

		//set options for hiding obscuring details, outlining, and simple material overrides:
		gfxSnapApplyOptions(true, true, true);
	}else{
		//set options for hiding obscuring details, outlining, and simple material overrides:
		gfxSnapApplyOptions(true, false, false);
	}
}

void DEFAULT_LATELINK_gfxMapSnapPhotoScaleOptions(WorldRegion * region)
{
	// STO
	if (region)
	{
		WorldRegionType wrt;
		ANALYSIS_ASSUME(region);
		wrt = worldRegionGetType(region);
		if ((wrt == WRT_Space) || (wrt == WRT_SectorSpace))
		{
			g_fMapSnapMaximumPixelsPerWorldUnit = 0.45f;
			g_fMapSnapMinimumPixelsPerWorldUnit = 0.15f;
			g_fMapSnapMaximumSizeThreshold = 12000.0f;
			g_fMapSnapMinimumSizeThreshold = 4000.0f;
			return;
		}
	}

	g_fMapSnapMaximumPixelsPerWorldUnit = 1.5f;
	g_fMapSnapMinimumPixelsPerWorldUnit = 0.5f;
	g_fMapSnapMaximumSizeThreshold = 2000.0f;
	g_fMapSnapMinimumSizeThreshold = 100.0f;
}

void gfxMapFinishOverviewPhoto(mapRoomPhoto *map_room_photo)
{
	int a;
	U8 *pCapData = NULL;

	if(map_room_photo->pOverviewTextureBuffer)
	{
		gfxSetActiveSurface(map_room_photo->pOverviewTextureBuffer->surface);
		gfxLockActiveDevice();
		pCapData = rdrGetActiveSurfaceData(gfx_state.currentDevice->rdr_device,SURFDATA_BGRA,MS_OV_IMAGE_SIZE,MS_OV_IMAGE_SIZE);
		for(a = 0; a < eaSize(&map_room_photo->action_list); a++)
		{
			RoomInstanceMapSnapAction *action = map_room_photo->action_list[a];
			FinishMapSnap(action, pCapData, 0, 0, MS_OV_IMAGE_SIZE, MS_OV_IMAGE_SIZE, MS_OV_IMAGE_SIZE, MS_OV_IMAGE_SIZE, &map_room_photo->ov_raw_data);
		}

		// in case there were no actions
		if(map_room_photo->ov_raw_data == NULL)
			FinishMapSnap(NULL, pCapData, 0, 0, MS_OV_IMAGE_SIZE, MS_OV_IMAGE_SIZE, MS_OV_IMAGE_SIZE, MS_OV_IMAGE_SIZE, &map_room_photo->ov_raw_data);

		free(pCapData);
		gfxUnlockActiveDevice();
	}
}

#define GFX_MAP_SNAP_FLUSH_TEXTURE_FRAME_ITERATIONS 10

void gfxMapSnapFlushTextures(GfxDummyFrameInfo * frame_loop_info)
{
	int frame;
	TexUnloadMode push_dynamicUnload = texDynamicUnloadEnabled();

	texDynamicUnload(TEXUNLOAD_ENABLE_FORCE_UNLOAD_ALL);

	for (frame = 0; frame < GFX_MAP_SNAP_FLUSH_TEXTURE_FRAME_ITERATIONS; ++frame)
	{
		gfxDummyFrameTopEx(frame_loop_info, 0.01f, true);
		gfx_state.client_loop_timer = 0.0f;

		texUnloadTexturesToFitMemory();

		gfxDummyFrameBottom(frame_loop_info, regions, true);

		Sleep(5);
	}

	texDynamicUnload(push_dynamicUnload);
}


bool gfxTakeMapPhotos(const char *path, char ***output_list, bool debug_run)
{
	int i, j;
	int frame_count=0;
	bool took_too_long=false;
	GfxCameraController *camera;
	F32 total_elapsed = 0;
	F32 elapsed = 0;
	U32 frame_timer;
	F32 orig_max_fps;
	int orig_outliningAllowedGFEATURE;
	bool orig_wind_flag;
	bool orig_inc_text_flag;
	bool orig_fade_in_flag;
	F32 orig_override_time;
	F32 orig_client_loop_timer;
	int orig_draw_high_detail;
	int orig_draw_high_fill_detail;
	F32 orig_worldDetailLevel;
	bool orig_disable_terrain_collision;
//	bool orig_gfxHeadshotDoBadImageChecks;
	int orig_gfxHeadshotFramesToWait;
	RdrDebugTypeFlags orig_dbg_type_flags;
	mapRoomPhotoAction photo_action;
	extern bool disable_terrain_collision;
	bool bStashedOutlineState = (gfx_state.settings.features_desired & GFEATURE_OUTLINING) != 0;
	GfxDummyFrameInfo frame_loop_info = { 0 };

	heatmap_data = StructCreate(parse_TemplateMapInfo);

	if(eaSize(&map_room_photo_list)==0)
		return true;

	gfxDummyFrameSequenceStart(&frame_loop_info);

	gfxResetFrameCounters();


	gfxLoadingSetLoadingMessage("Taking Map Photos - This may cause the screen to flash.");

	if(	heatmap_width > 0 && heatmap_height > 0 && gfxMapSnapShouldMakeHeatMaps())
		heatmap_jpeg = calloc(heatmap_width*heatmap_height, 3*sizeof(U8));
	photo_action.headshot_tex = NULL;

	//Save and Change States
	orig_max_fps = gfx_state.settings.maxInactiveFps;
	gfx_state.settings.maxInactiveFps = 0;
	orig_fade_in_flag = gfx_state.debug.no_fade_in;
	gfx_state.debug.no_fade_in = true;
	orig_override_time = gfx_state.debug.overrideTime;
	gfx_state.debug.overrideTime = 12.0f;
	orig_client_loop_timer = gfx_state.client_loop_timer;
	gfx_state.client_loop_timer = 0.0f;
	orig_inc_text_flag = gfx_state.debug.disableIncrementalTex;
	gfx_state.debug.disableIncrementalTex = true;
	orig_wind_flag = dynWindGetEnabled();
	dynWindSetEnabled(false);
	orig_draw_high_detail = gfx_state.settings.draw_high_detail;
	orig_draw_high_fill_detail = gfx_state.settings.draw_high_fill_detail;
	gfx_state.settings.draw_high_detail = false;
	gfx_state.settings.draw_high_fill_detail = false;
	orig_worldDetailLevel = gfx_state.settings.worldDetailLevel;
	gfx_state.settings.worldDetailLevel = 5.0f*5.0f;
	orig_disable_terrain_collision = disable_terrain_collision;
	disable_terrain_collision = true;
//	orig_gfxHeadshotDoBadImageChecks = gfxHeadshotDoBadImageChecks;
	//gfxHeadshotDoBadImageChecks = true;
	orig_gfxHeadshotFramesToWait = gfxHeadshotFramesToWait;
	orig_outliningAllowedGFEATURE = gfx_state.features_allowed & GFEATURE_OUTLINING;
	
	gfxHeadshotUseSunIndoors = g_bMapSnapUseSunIndoors;

	gfxHeadshotFramesToWait = 4;
	orig_dbg_type_flags = rdr_state.dbg_type_flags;
	rdr_state.dbg_type_flags &= ~RDRTYPE_PARTICLE;
	gfxSkyClearAllVisibleSkies();
	taking_map_photos = true;

	// Force clearing the actions, in case there are any leftover
	// headshots.
	gfxDummyFrameTopEx(&frame_loop_info, 0.01f, false);
	gfxLoadingDisplayScreen(true);
	gfxDummyFrameBottom(&frame_loop_info, NULL, true);

	PERFINFO_AUTO_START_FUNC();
	camera = calloc(1, sizeof(*camera));
	gfxInitCameraController(camera, gfxMapSnapCamFunc, NULL);
	gfxCameraControllerSetSkyOverride(camera, "default_sky", NULL);

	camera->override_hide_editor_objects = true;
	camera->ortho_mode_ex = true;
	// these values are invalid
	camera->campyr[0] = -PI;
	camera->campyr[2] = -PI;
	camera->camdist = 0;
	camera->inited = true;
	camera->override_bg_color = true;
	setVec4(camera->clear_color, 255, 255, 255, 255);

	frame_timer = timerAlloc();
	timerStart(frame_timer);
	for( i=0; i < eaSize(&map_room_photo_list); i++ )
	{
		bool bDone = false;
		int iStallFrames = 0;
		mapRoomPhoto *map_room_photo = map_room_photo_list[i];
		WorldRegion *region = map_room_photo->region;
		elapsed = 0;
		total_elapsed = 0;
		took_too_long = true;
		map_room_photo->zmap_path = path;

		if (!g_bMapSnapDisableMaterialOverride)
		{
			gfxMapSnapSetupOptions(region);
		}

		mapSnapUpdateRegion(region);

		// do we need an overview image?
		if(eaSize(&map_room_photo->cells) > 1)
		{
			map_room_photo->pOverviewTextureBuffer = gfxMapSnapCreateTempSurface(MS_OV_IMAGE_SIZE,MS_OV_IMAGE_SIZE);
		}
		for( j=0; j < 100000 && total_elapsed < 240; j++ )
		{
			total_elapsed += elapsed;
			if (map_room_photo->pOverviewTextureBuffer)
			{
				gfxMarkTempSurfaceUsed(map_room_photo->pOverviewTextureBuffer);
			}
			if (s_pTempMapSnapTex)
			{
				gfxMarkTempSurfaceUsed(s_pTempMapSnapTex);
			}
			if (!bDone)
			{
				bDone = gfxMapPhotoProcess(camera, map_room_photo, &photo_action, &frame_count, gfxMapCellFinished);
			}
			else
			{
				iStallFrames++;
			}

			if(bDone && iStallFrames > 5)
			{
				const char *new_name = NULL;

				if(map_room_photo->pOverviewTextureBuffer && map_room_photo->ov_raw_data == NULL)
					gfxMapFinishOverviewPhoto(map_room_photo);

				if (map_room_photo->ov_raw_data)
				{
					new_name = gfxMapDXTCompressAndSave(map_room_photo->ov_raw_data, 
													MIN(MS_OV_IMAGE_SIZE, map_room_photo->full_w), 
													MIN(MS_OV_IMAGE_SIZE, map_room_photo->full_h), 
													MS_OV_IMAGE_SIZE,
													map_room_photo->prefix, 0, path, false);
				}

				if(heatmap_data && heatmap_jpeg && new_name)
				{
					TemplateMapSingleZoneInfo *new_heatmap_entry = StructCreate(parse_TemplateMapSingleZoneInfo);
					new_heatmap_entry->iMapMinX = heatmap_offset;
					new_heatmap_entry->iMapMinY = 0;
					new_heatmap_entry->iMapMaxX = heatmap_offset + map_room_photo->full_w;
					new_heatmap_entry->iMapMaxY = map_room_photo->full_h;
					new_heatmap_entry->iZoneMinX = map_room_photo->min[0];
					new_heatmap_entry->iZoneMinZ = map_room_photo->min[2];
					new_heatmap_entry->iZoneMaxX = map_room_photo->max[0];
					new_heatmap_entry->iZoneMaxZ = map_room_photo->max[2];
					assert(map_room_photo->region);
					new_heatmap_entry->pZoneName = StructAllocString(worldRegionGetRegionName(map_room_photo->region));
					eaPush(&heatmap_data->ppZones, new_heatmap_entry);
				}
				if(new_name && map_room_photo->pPartitionData)
				{
					map_room_photo->pPartitionData->overview_image_name = new_name;
					free(map_room_photo->ov_raw_data);
					map_room_photo->ov_raw_data = NULL;
				}
				took_too_long = false;
				break;
			}
			else if(frame_count < 0)
			{
				took_too_long = true;
				break;
			}
			else if(frame_count == MAX_FRAMES_PER_PIC)
			{
				total_elapsed = 0;
				timerElapsedAndStart(frame_timer);
			}

			gfxDummyFrameTopEx(&frame_loop_info, 0.01f, true);
			gfx_state.client_loop_timer = 0.0f;
			gfxLoadingDisplayScreen(true);
			gfxDummyFrameBottom(&frame_loop_info, regions, true);
			Sleep(5);
			if(g_MapSnapNoTimeOut) {
				took_too_long = false;
				total_elapsed = 0;
				j = 0;
				frame_count = MAX_FRAMES_PER_PIC-1;
			}
			elapsed = timerElapsedAndStart(frame_timer);
		}
		if(eaSize(&map_room_photo->cells) > 1)
			heatmap_offset += map_room_photo->full_w;
		if(took_too_long)
		{
			printf("\nFinished %d of %d\n", i, eaSize(&map_room_photo_list));
			printf("Frame %d\n", j);
			printf("Time %f\n", total_elapsed);
			for( j=0; j < eaSize(&map_room_photo_list); j++ )
			{
				printf("Image %d: %f %f %f : %f %f %f\n", j, 
					map_room_photo_list[j]->min[0], 
					map_room_photo_list[j]->min[1], 
					map_room_photo_list[j]->min[2], 
					map_room_photo_list[j]->max[0], 
					map_room_photo_list[j]->max[1], 
					map_room_photo_list[j]->max[2]);
			}
			Errorf("%s: Took too long to make map images.  Client Bins not fully made.", path);
			break;
		}
		if(debug_run)
		{
			map_room_photo->started = false;
			if (map_room_photo->pPartitionData)
			{
				eaDestroy(&map_room_photo->pPartitionData->image_name_list);
			}
		}
	}
	
	//Save Heatmap Data
	if(heatmap_jpeg && heatmap_data)
	{
		char full_file_name[MAX_PATH];
		char file_name[255];
		sprintf(file_name, "%s", path);
		for( i=0; i < (int)strlen(file_name); i++ )
		{
			if(file_name[i] == '\\' || file_name[i] == '/' || file_name[i] == '.')
				file_name[i] = '_';
		}
		sprintf(full_file_name, "%s/server/HeatMapTemplates", fileDataDir());
		if(makeDirectories(full_file_name))
		{
			char rel_file_name[MAX_PATH];
			sprintf(full_file_name, "server\\HeatMapTemplates\\%s.template", file_name);
			ParserWriteTextFile(full_file_name, parse_TemplateMapInfo, heatmap_data, 0, 0);
			eaPush(output_list, StructAllocString(full_file_name));
			if(g_SaveHeatMapsAsTGAs)
			{
				U8 *heatmap_tga = calloc(heatmap_width*heatmap_height, 4*sizeof(U8));
				for( i=0; i < heatmap_width*heatmap_height; i++ )
				{
					heatmap_tga[i*4+0] = heatmap_jpeg[i*3+0];
					heatmap_tga[i*4+1] = heatmap_jpeg[i*3+1];
					heatmap_tga[i*4+2] = heatmap_jpeg[i*3+2];
				}
				sprintf(rel_file_name, "server/HeatMapTemplates/%s.tga", file_name);
				sprintf(full_file_name, "%s\\%s", fileDataDir(), rel_file_name);
				tgaSave(full_file_name, heatmap_tga, heatmap_width, heatmap_height, 3);
				SAFE_FREE(heatmap_tga);
			}
			else
			{
				sprintf(rel_file_name, "server/HeatMapTemplates/%s.jpg", file_name);
				sprintf(full_file_name, "%s\\%s", fileDataDir(), rel_file_name);
				jpgSave(full_file_name, heatmap_jpeg, 3, heatmap_width, heatmap_height, 95);
			}
			eaPush(output_list, StructAllocString(rel_file_name));
		}
	}


	if(photo_action.headshot_tex)
		gfxHeadshotRelease(photo_action.headshot_tex);
	if(!debug_run)
	{
		eaDestroyEx(&map_room_photo_list, gfxMapRoomPhotoDestroy);
		map_room_photo_list = NULL;
	}
	eaDestroy(&regions);
	regions = NULL;
	timerFree(frame_timer);
	heatmap_offset = 0;
	heatmap_height = 0;
	heatmap_width  = 0;
	SAFE_FREE(heatmap_jpeg);
	StructDestroy(parse_TemplateMapInfo, heatmap_data);
	heatmap_data = NULL;
	gfxDeinitCameraController(camera);
	free(camera);
	//Restore States
	gfx_state.debug.no_fade_in = orig_fade_in_flag;
	gfx_state.settings.maxInactiveFps = orig_max_fps;
	gfx_state.debug.overrideTime = orig_override_time;
	gfx_state.client_loop_timer = orig_client_loop_timer;
	gfx_state.debug.disableIncrementalTex = orig_inc_text_flag;
	dynWindSetEnabled(orig_wind_flag);
	gfx_state.settings.draw_high_detail = orig_draw_high_detail;
	gfx_state.settings.draw_high_fill_detail = orig_draw_high_fill_detail;
	gfx_state.settings.worldDetailLevel = orig_worldDetailLevel;
	disable_terrain_collision = orig_disable_terrain_collision;
//	gfxHeadshotDoBadImageChecks = orig_gfxHeadshotDoBadImageChecks;
	gfxHeadshotFramesToWait = orig_gfxHeadshotFramesToWait;
	gfxHeadshotUseSunIndoors = false;
	rdr_state.dbg_type_flags = orig_dbg_type_flags;

	gfxSnapUndoOptions();

	taking_map_photos = false;

	gfxMapSnapFlushTextures(&frame_loop_info);

	gfxLoadingSetLoadingMessage("Loading data");
	
	gfxDummyFrameSequenceEnd(&frame_loop_info);

	PERFINFO_AUTO_STOP();
	if(took_too_long)
		return false;
	return true;
}

AtlasTex* gfxMapPhotoRegister(const char *texture_name)
{
	BasicTexture *new_tex;
	if(texture_name[0] != '#')
		return atlasFindTexture(texture_name);
	new_tex = texRegisterDynamic(texture_name);
	new_tex->bt_texopt_flags |= TEXOPT_CLAMPS;
	new_tex->bt_texopt_flags |= TEXOPT_CLAMPT;
	//new_tex->texopt_flags |= TEXOPT_MAGFILTER_POINT; //If we want to get rid of seams
	return atlasFindTexture(texture_name);
}

void gfxMapPhotoUnregister(AtlasTex *tex)
{
	if(tex && tex->name[0] == '#')
		texUnregisterDynamic(tex->name);
}

void gfxUpdateMapPhoto(U8 *new_data, U8 *old_data, int data_size)
{
	int i,j;
	int blockSize = 8;
	int num_blocks;
	U8 new_data_dest[64];
	U8 old_data_dest[64];
	RdrTexFormat tex_format;
	DDSURFACEDESC2	new_ddsd;
	DDSURFACEDESC2	old_ddsd;

	new_data += sizeof(TextureFileHeader);
	old_data += sizeof(TextureFileHeader);

	if (strncmp(new_data, "DDS ", 4)!=0 || strncmp(old_data, "DDS ", 4)!=0)
		return;

	new_data+=4;
	old_data+=4;
	memcpy(&new_ddsd, new_data, sizeof(DDSURFACEDESC2));
	memcpy(&old_ddsd, old_data, sizeof(DDSURFACEDESC2));
	new_data+=sizeof(DDSURFACEDESC2);
	old_data+=sizeof(DDSURFACEDESC2);

	if(memcmp(&new_ddsd, &old_ddsd, sizeof(DDSURFACEDESC2) != 0))
		return;

	tex_format = texFormatFromDDSD(&new_ddsd);
	if(tex_format != RTEX_DXT1)
		return;

	num_blocks = ((new_ddsd.dwWidth+3)/4) * ((new_ddsd.dwHeight+3)/4);
	if(num_blocks*blockSize != data_size-4-sizeof(DDSURFACEDESC2)-sizeof(TextureFileHeader))
		return;

	// Walk through all blocks and compare them, copy if nearly the same
	for( i=0; i < num_blocks; i++ )
	{
		int diff_sum = 0;
		dxtuncompress_block(tex_format, new_data, new_data_dest, 16);
		dxtuncompress_block(tex_format, old_data, old_data_dest, 16);
		for( j=0; j < 64; j++ )
		{
			int diff;
			if(j%4 == 3)//ignore alpha
				continue;
			diff = new_data_dest[j] - old_data_dest[j];
			diff_sum += SQR(diff);
		}
		if(diff_sum < 650)
			memcpy(new_data, old_data, blockSize);
		new_data += blockSize;
		old_data += blockSize;
	}
}

void gfxDownRezMapPhoto(U8 *data, int *data_size)
{
	DDSURFACEDESC2 ddsd;
	RdrTexFormat tex_format;
	int num_blocks;
	int blockSize = 8;
	U8 *new_image, *uncompressed_data;
	U8 *data_ptr = data;
	int block_width, block_height, block_idx;
	int padded_width, padded_height;
	int compressed_size;
	DDSURFACEDESC2 *header_data;
	TextureFileHeader *tfh, *old_tfh;
	
	old_tfh = (TextureFileHeader*)data_ptr;
	data_ptr += sizeof(TextureFileHeader);
	data_ptr += 4;
	memcpy(&ddsd, data_ptr, sizeof(DDSURFACEDESC2));
	data_ptr += sizeof(DDSURFACEDESC2);

	if (ddsd.dwWidth < 16 || ddsd.dwHeight < 16)
		return;

	tex_format = texFormatFromDDSD(&ddsd);
	if(tex_format != RTEX_DXT1)
		return;

	block_width = (ddsd.dwWidth+3)/4;
	block_height = (ddsd.dwHeight+3)/4;
	num_blocks = block_width * block_height;

	padded_width = pow2(block_width);
	padded_height = pow2(block_height);

	uncompressed_data = calloc(1, ddsd.dwWidth*ddsd.dwHeight*4);
	dxtDecompressDirect(data_ptr, uncompressed_data, ddsd.dwWidth, ddsd.dwHeight, tex_format);

	new_image = calloc(1, padded_width * padded_height * 4);
	for (block_idx = 0; block_idx < num_blocks; block_idx++)
	{
		int out_addr = (block_idx/block_width) * padded_width * 4 + (block_idx%block_width)*4;
		int px, py, r = 0, g = 0, b = 0, a = 0;
		for (py = 0; py < 4; py++)
		{
			int scanline = (py+(block_idx/block_width)*4) * (ddsd.dwWidth * 4);
			for (px = 0; px < 4; px++)
			{
				int idx = scanline + (px+(block_idx%block_width)*4) * 4;
				r += uncompressed_data[idx + 0];
				g += uncompressed_data[idx + 1];
				b += uncompressed_data[idx + 2];
				a += uncompressed_data[idx + 3];
			}
		}
		new_image[out_addr + 0] = r>>4;
		new_image[out_addr + 1] = g>>4;
		new_image[out_addr + 2] = b>>4;
		new_image[out_addr + 3] = a>>4;
	}
	SAFE_FREE(uncompressed_data);

	compressed_size = ((padded_width*padded_height)/2)*sizeof(U8);
	*data_size = compressed_size + 4 + sizeof(DDSURFACEDESC2) + sizeof(TextureFileHeader);

	// Update headers
	tfh = (TextureFileHeader*)data;
	tfh->width = old_tfh->width/4;
	tfh->height = old_tfh->height/4;
	tfh->file_size += compressed_size - num_blocks*blockSize;
	data += sizeof(TextureFileHeader);

	data += 4;

	header_data = (DDSURFACEDESC2*)data;
	header_data->dwWidth = padded_width;
	header_data->dwHeight = padded_height;
	data += sizeof(DDSURFACEDESC2);

	nvdxtCompress(new_image, data, padded_width, padded_height, RTEX_DXT1, 1, 0);
	SAFE_FREE(new_image);
}

